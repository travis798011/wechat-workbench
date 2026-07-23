# 微信多账号聚合桥（WCF 版）
# ===========================
#
# Windows 端运行，连接多个 wcf 注入实例，聚合消息到消息网关。
#
# 使用:
#   pip install requests pywcf
#   python wcf-bridge.py --gateway http://192.168.x.x:3028
#
# 依赖: Python 3.8+, requests
#
# 架构:
#   微信#1 → wcf.dll(TCP:10081) ─┐
#   微信#2 → wcf.dll(TCP:10082) ─┤
#   微信#3 → wcf.dll(TCP:10083) ─┤   wcf-bridge.py → Gateway API
#   ...                           ─┤
#   微信#N → wcf.dll(TCP:10080+N) ┘
"""
WeChat WCF Bridge — 多微信消息聚合桥

功能：
  - 自动发现/配置 wcf 实例（TCP 端口）
  - 接收每个微信的实时消息（基于 wcf 回调/轮询）
  - 转发到消息网关（HTTP/WS）
  - 从网关接收回复指令，路由到对应 wcf 实例发送
  - 心跳和重连
"""

import argparse
import asyncio
import json
import logging
import os
import socket
import struct
import sys
import time
from dataclasses import dataclass, field
from typing import Optional

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
)
log = logging.getLogger("wcf-bridge")

# ============================================================
# 配置
# ============================================================

DEFAULT_GATEWAY = "http://127.0.0.1:3028"
DEFAULT_PORTS = [10081]  # 默认 wcf 端口，可通过 --ports 覆盖
POLL_INTERVAL = 0.5  # 轮询间隔（秒）
HEARTBEAT_INTERVAL = 30  # 心跳间隔（秒）
RECONNECT_DELAY = 3  # 重连延迟（秒）

# ============================================================
# WCF TCP 协议封装（参考 wcf 官方 Python SDK）
# ============================================================

WCF_FUNCTIONS = {
    "WCF_GET_USER_INFO": 1,
    "WCF_GET_CONTACTS": 2,
    "WCF_GET_MSG": 3,
    "WCF_SEND_TEXT": 4,
    "WCF_SEND_IMAGE": 5,
    "WCF_SEND_FILE": 6,
    "WCF_CONNECT": 7,
    "WCF_IS_LOGIN": 8,
}


class WcfClient:
    """单个 wcf 实例的 TCP 客户端"""

    def __init__(self, host: str, port: int, label: str):
        self.host = host
        self.port = port
        self.label = label
        self.sock: Optional[socket.socket] = None
        self.connected = False

    def connect(self) -> bool:
        """连接到 wcf TCP 端口"""
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.settimeout(10)
            self.sock.connect((self.host, self.port))
            self.connected = True
            log.info(f"[{self.label}] Connected to wcf at {self.host}:{self.port}")
            return True
        except Exception as e:
            log.error(f"[{self.label}] Connect failed: {e}")
            self.connected = False
            return False

    def disconnect(self):
        if self.sock:
            try:
                self.sock.close()
            except:
                pass
        self.sock = None
        self.connected = False

    def _send_request(self, func_id: int, data: bytes = b"") -> bytes:
        """发送 wcf 请求: 4字节 func_id + 4字节 data_len + data"""
        if not self.sock:
            raise ConnectionError("Not connected")
        header = struct.pack("<II", func_id, len(data))
        self.sock.sendall(header + data)
        # 读取响应: 4字节 data_len
        resp_header = self._recv_exact(4)
        if not resp_header:
            raise ConnectionError("Connection closed")
        data_len = struct.unpack("<I", resp_header)[0]
        if data_len == 0:
            return b""
        return self._recv_exact(data_len)

    def _recv_exact(self, n: int) -> bytes:
        """精确接收 n 字节"""
        chunks = []
        received = 0
        while received < n:
            chunk = self.sock.recv(min(n - received, 4096))
            if not chunk:
                break
            chunks.append(chunk)
            received += len(chunk)
        return b"".join(chunks)

    # ---- wcf API ----

    def get_user_info(self) -> dict:
        """获取登录用户信息"""
        resp = self._send_request(WCF_FUNCTIONS["WCF_GET_USER_INFO"])
        try:
            return json.loads(resp.decode("utf-8"))
        except:
            return {"raw": resp.hex()}

    def is_login(self) -> bool:
        """检查是否已登录"""
        resp = self._send_request(WCF_FUNCTIONS["WCF_IS_LOGIN"])
        return resp == b"\x01"

    def get_contacts(self) -> list:
        """获取通讯录列表"""
        resp = self._send_request(WCF_FUNCTIONS["WCF_GET_CONTACTS"])
        try:
            return json.loads(resp.decode("utf-8"))
        except:
            return []

    def get_msg(self) -> list:
        """获取最近的消息（轮询模式）"""
        resp = self._send_request(WCF_FUNCTIONS["WCF_GET_MSG"])
        try:
            return json.loads(resp.decode("utf-8"))
        except:
            return []

    def send_text(self, wxid: str, text: str) -> bool:
        """发送文本消息"""
        req = json.dumps({"wxid": wxid, "text": text})
        resp = self._send_request(WCF_FUNCTIONS["WCF_SEND_TEXT"], req.encode("utf-8"))
        return resp == b"\x01"


# ============================================================
# 聚合桥核心逻辑
# ============================================================

@dataclass
class WeChatAccount:
    """一个微信账号的配置和状态"""
    port: int
    label: str
    wxid: str = ""  # 登录后获取
    name: str = ""
    wcf: Optional[WcfClient] = None
    last_poll: float = 0
    msg_cache: set = field(default_factory=set)  # 已处理消息 ID 去重


class WcfBridge:
    """多微信消息聚合桥"""

    def __init__(self, gateway_url: str, ports: list[int]):
        self.gateway_url = gateway_url.rstrip("/")
        self.ports = ports
        self.accounts: dict[int, WeChatAccount] = {}
        self._running = False

        for i, port in enumerate(ports):
            self.accounts[port] = WeChatAccount(
                port=port,
                label=f"wx-{i + 1}",
            )

    # ---- 连接管理 ----

    async def connect_all(self):
        """连接所有 wcf 实例"""
        for port, acct in self.accounts.items():
            wcf = WcfClient("127.0.0.1", port, acct.label)
            if wcf.connect():
                acct.wcf = wcf
                # 获取登录信息
                await asyncio.sleep(0.3)
                try:
                    info = wcf.get_user_info()
                    acct.wxid = info.get("wxid", "")
                    acct.name = info.get("name", info.get("nickName", ""))
                    log.info(
                        f"[{acct.label}] Logged in as: {acct.name} ({acct.wxid})"
                    )
                    # 报告到网关
                    self._report_account(acct)
                except Exception as e:
                    log.warning(f"[{acct.label}] Failed to get user info: {e}")
            else:
                log.warning(
                    f"[{acct.label}] Connection failed, will retry later"
                )

        online = sum(1 for a in self.accounts.values() if a.wcf and a.wcf.connected)
        log.info(f"Connected: {online}/{len(self.accounts)} accounts online")

    def _report_account(self, acct: WeChatAccount):
        """向网关注册账号"""
        try:
            import requests

            payload = {
                "wxid": acct.wxid,
                "name": acct.name,
                "label": acct.label,
                "port": acct.port,
                "status": "online" if acct.wcf else "offline",
            }
            requests.post(
                f"{self.gateway_url}/api/wcf/account",
                json=payload,
                timeout=5,
            )
        except Exception as e:
            log.warning(f"[gateway] Report account failed: {e}")

    # ---- 消息处理 ----

    def _dedup_msg(self, msg_obj: dict) -> bool:
        """消息去重"""
        msg_id = msg_obj.get("id", "")
        if not msg_id:
            return False
        acct = self._get_account_by_wxid(msg_obj.get("sender", ""))
        if not acct:
            return False
        if msg_id in acct.msg_cache:
            return True  # duplicate
        acct.msg_cache.add(msg_id)
        if len(acct.msg_cache) > 1000:
            acct.msg_cache.clear()
        return False

    def _get_account_by_wxid(self, wxid: str) -> Optional[WeChatAccount]:
        for acct in self.accounts.values():
            if acct.wxid == wxid:
                return acct
        return None

    def _get_account_by_port(self, port: int) -> Optional[WeChatAccount]:
        return self.accounts.get(port)

    async def poll_messages(self):
        """轮询所有 wcf 实例的消息"""
        for port, acct in self.accounts.items():
            wcf = acct.wcf
            if not wcf or not wcf.connected:
                continue
            try:
                msgs = wcf.get_msg()
                if not msgs:
                    continue

                for msg in msgs:
                    # wcf 消息格式: {id, type, content, sender, roomid, ...}
                    # type 1=文本, 3=图片, 34=语音, 47=表情, 43=视频, 49=文件/链接
                    if self._dedup_msg(msg):
                        continue

                    msg_type = msg.get("type", 0)
                    content = msg.get("content", "")
                    sender = msg.get("sender", "")
                    room_id = msg.get("roomid", "")

                    # 忽略群消息（如果将来需要群消息可以放开）
                    if room_id:
                        continue

                    # 忽略自己发的消息（避免回声）
                    if sender == acct.wxid:
                        continue

                    unified = {
                        "account_wxid": acct.wxid,
                        "account_name": acct.name,
                        "from_wxid": sender,
                        "from_name": msg.get("sender_name", ""),
                        "msg_type": self._map_type(msg_type),
                        "content": content,
                        "raw_type": msg_type,
                        "ts": int(time.time() * 1000),
                    }

                    await self._forward_to_gateway(unified)

            except Exception as e:
                log.warning(f"[{acct.label}] Poll error: {e}")
                # 可能是连接断开了
                if acct.wcf:
                    acct.wcf.disconnect()
                await asyncio.sleep(RECONNECT_DELAY)
                acct.wcf.connect()

    def _map_type(self, wcf_type: int) -> str:
        mapping = {
            1: "text",
            3: "image",
            34: "voice",
            43: "video",
            47: "sticker",
            49: "file",
            10000: "system",
            436207665: "transfer",
            419430449: "red_packet",
        }
        return mapping.get(wcf_type, f"unknown_{wcf_type}")

    async def _forward_to_gateway(self, msg: dict):
        """转发消息到网关"""
        try:
            import requests

            resp = requests.post(
                f"{self.gateway_url}/api/wcf/message",
                json=msg,
                timeout=5,
            )
            if resp.status_code != 200:
                log.warning(
                    f"[gateway] Forward failed: {resp.status_code} {resp.text[:200]}"
                )
        except Exception as e:
            log.warning(f"[gateway] Forward error: {e}")

    # ---- 发送消息（从网关接收指令） ----

    async def send_text(self, acct_wxid: str, to_wxid: str, text: str) -> bool:
        """通过 wcf 实例发送文本消息"""
        for acct in self.accounts.values():
            if acct.wxid == acct_wxid:
                return acct.wcf.send_text(to_wxid, text)
        log.warning(f"[send] Account {acct_wxid} not found")
        return False

    # ---- 心跳和自愈 ----

    async def heartbeat(self):
        """定期检查连接状态"""
        for port, acct in self.accounts.items():
            wcf = acct.wcf
            if not wcf or not wcf.connected:
                log.info(f"[{acct.label}] Reconnecting...")
                wcf = WcfClient("127.0.0.1", port, acct.label)
                if wcf.connect():
                    acct.wcf = wcf
                    try:
                        info = wcf.get_user_info()
                        acct.wxid = info.get("wxid", "")
                        acct.name = info.get("name", "")
                        self._report_account(acct)
                    except:
                        pass
                    log.info(f"[{acct.label}] Reconnected")

    # ---- 主循环 ----

    async def run(self):
        """主运行循环"""
        self._running = True
        await self.connect_all()

        last_heartbeat = time.time()

        # 启动发送监听（HTTP polling 模式 — 网关在另一台机器时用这个）
        # 如果作为子进程运行可以直接用 WebSocket，先做 HTTP polling 版本
        log.info("[bridge] Starting main loop...")

        while self._running:
            # 1. 轮询消息
            await self.poll_messages()

            # 2. 周期性心跳 + 发送队列检查
            if time.time() - last_heartbeat > HEARTBEAT_INTERVAL:
                await self.heartbeat()
                try:
                    incoming = self._check_send_queue()
                    for send_cmd in incoming:
                        ok = await self.send_text(
                            send_cmd["account_wxid"],
                            send_cmd["to_wxid"],
                            send_cmd["text"],
                        )
                        log.info(
                            f"[send] {'OK' if ok else 'FAIL'} to {send_cmd['to_wxid']}: {send_cmd['text'][:60]}"
                        )
                        self._ack_send(send_cmd.get("id"))
                except Exception as e:
                    log.warning(f"[send-queue] Error: {e}")

                last_heartbeat = time.time()

            await asyncio.sleep(POLL_INTERVAL)

    def _check_send_queue(self) -> list:
        """检查网关是否有待发送消息（HTTP polling 版）"""
        try:
            import requests

            resp = requests.get(
                f"{self.gateway_url}/api/wcf/send-queue?client=bridge-1",
                timeout=5,
            )
            if resp.status_code == 200:
                return resp.json()
        except:
            pass
        return []

    def _ack_send(self, msg_id: str):
        """确认消息已发送"""
        if not msg_id:
            return
        try:
            import requests

            requests.post(
                f"{self.gateway_url}/api/wcf/send-ack",
                json={"id": msg_id},
                timeout=3,
            )
        except:
            pass

    def stop(self):
        self._running = False
        for acct in self.accounts.values():
            if acct.wcf:
                acct.wcf.disconnect()


# ============================================================
# 命令行入口
# ============================================================

def parse_ports(port_str: str) -> list[int]:
    """解析端口列表: "10081,10082,10083" 或 "10081-10083" """
    ports = []
    for part in port_str.split(","):
        part = part.strip()
        if "-" in part:
            a, b = part.split("-", 1)
            ports.extend(range(int(a.strip()), int(b.strip()) + 1))
        else:
            ports.append(int(part))
    return ports


async def main():
    parser = argparse.ArgumentParser(description="WeChat WCF Bridge")
    parser.add_argument(
        "--gateway",
        default=DEFAULT_GATEWAY,
        help=f"消息网关地址 (默认: {DEFAULT_GATEWAY})",
    )
    parser.add_argument(
        "--ports",
        default="10081",
        help="WCF 端口列表，逗号或范围分隔，如: 10081,10082,10083 或 10081-10083",
    )
    parser.add_argument("--debug", action="store_true", help="调试模式")

    args = parser.parse_args()

    if args.debug:
        logging.getLogger().setLevel(logging.DEBUG)

    ports = parse_ports(args.ports)
    log.info(f"配置: gateway={args.gateway} ports={ports}")

    bridge = WcfBridge(args.gateway, ports)

    try:
        await bridge.run()
    except KeyboardInterrupt:
        log.info("[bridge] Shutting down...")
        bridge.stop()

    log.info("[bridge] Done")


if __name__ == "__main__":
    asyncio.run(main())
