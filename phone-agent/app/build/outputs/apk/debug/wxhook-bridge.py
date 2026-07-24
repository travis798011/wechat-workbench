"""
wxhook 桥 — HTTP API 版（对接 WeChatApi-DLL 19088 端口）

用法:
    pip install requests psutil
    # 先打开微信 4.1.8.27 并登录
    python wxhook-bridge.py --backend http://192.168.2.44:3028
"""

import argparse
import ctypes
import logging
import os
import sys
import threading
import time
from ctypes import wintypes
from typing import Optional

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] wxhook-bridge %(message)s",
)
log = logging.getLogger("wxhook-bridge")

BACKEND_DEFAULT = "http://127.0.0.1:3028"
POLL_INTERVAL = 3
HTTP_TIMEOUT = 10
DLL_PORT = 19088

# ============================================================
# DLL 注入（纯 ctypes）
# ============================================================

PROCESS_ALL_ACCESS = 0x1F0FFF
MEM_COMMIT = 0x1000
MEM_RESERVE = 0x2000
PAGE_READWRITE = 0x04

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)


def find_wechat_pid() -> Optional[int]:
    import psutil
    for proc in psutil.process_iter():
        name = proc.name().lower()
        if name in ("wechat.exe", "wechatappex.exe", "wechatexe.exe"):
            return proc.pid
    return None


def inject_dll(dll_path: str, pid: int) -> Optional[bool]:
    """注入 DLL 到目标进程，返回 True=成功/False=失败/None=可能已加载"""
    dll_path_bytes = dll_path.encode("utf-8")
    h_process = kernel32.OpenProcess(PROCESS_ALL_ACCESS, False, pid)
    if not h_process:
        log.error("OpenProcess 失败 (%d)", ctypes.get_last_error())
        return False
    try:
        dll_path_len = len(dll_path_bytes) + 1
        addr = kernel32.VirtualAllocEx(h_process, None, dll_path_len,
                                       MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)
        if not addr:
            log.error("VirtualAllocEx 失败 (%d)", ctypes.get_last_error())
            return False
        written = ctypes.c_size_t(0)
        kernel32.WriteProcessMemory(h_process, addr, dll_path_bytes,
                                    dll_path_len, ctypes.byref(written))

        kernel32.GetModuleHandleA.argtypes = [wintypes.LPCSTR]
        kernel32.GetModuleHandleA.restype = wintypes.HMODULE
        kernel32.GetProcAddress.argtypes = [wintypes.HMODULE, wintypes.LPCSTR]
        kernel32.GetProcAddress.restype = wintypes.LPVOID

        kh = kernel32.GetModuleHandleA(b"kernel32.dll")
        load_library_addr = kernel32.GetProcAddress(kh, b"LoadLibraryA")

        kernel32.CreateRemoteThread.argtypes = [
            wintypes.HANDLE, wintypes.LPVOID, ctypes.c_size_t,
            wintypes.LPVOID, wintypes.LPVOID, ctypes.c_ulong, wintypes.LPVOID,
        ]
        kernel32.CreateRemoteThread.restype = wintypes.HANDLE

        h_thread = kernel32.CreateRemoteThread(h_process, None, 0,
                                               load_library_addr, addr, 0, None)
        if not h_thread:
            log.error("CreateRemoteThread 失败 (%d)", ctypes.get_last_error())
            return False

        kernel32.WaitForSingleObject(h_thread, 10000)
        kernel32.CloseHandle(h_thread)
        log.info("DLL 注入成功")
        return True
    finally:
        kernel32.CloseHandle(h_process)


# ============================================================
# HTTP API 客户端
# ============================================================

_requests = None


def _req():
    global _requests
    if _requests is None:
        import requests as r
        _requests = r
    return _requests


def wxapi_get(path: str) -> Optional[dict]:
    try:
        r = _req().get(f"http://127.0.0.1:{DLL_PORT}/api/{path}", timeout=5)
        if r.status_code == 200:
            return r.json()
    except Exception as e:
        log.warning("API GET /api/%s 失败: %s", path, e)


def wxapi_post(path: str, data: dict = None) -> Optional[dict]:
    try:
        r = _req().post(f"http://127.0.0.1:{DLL_PORT}/api/{path}",
                        json=data or {}, timeout=5)
        if r.status_code == 200:
            return r.json()
    except Exception as e:
        log.warning("API POST /api/%s 失败: %s", path, e)


def http_post(url: str, data: dict) -> bool:
    try:
        r = _req().post(url, json=data, timeout=HTTP_TIMEOUT)
        return r.status_code == 200
    except:
        return False


def http_get(url: str) -> Optional[list]:
    try:
        r = _req().get(url, timeout=HTTP_TIMEOUT)
        if r.status_code == 200:
            return r.json()
    except:
        pass


# ============================================================
# 去重缓存
# ============================================================

SEEN_IDS = set()
MAX_SEEN = 5000


def is_duplicate(msg_id: str) -> bool:
    if not msg_id:
        return False
    if msg_id in SEEN_IDS:
        return True
    SEEN_IDS.add(msg_id)
    if len(SEEN_IDS) > MAX_SEEN:
        SEEN_IDS.clear()
    return False


# ============================================================
# 消息处理
# ============================================================

def forward_message(backend: str, bot_wxid: str, msg: dict):
    msg_id = str(msg.get("id", ""))
    if is_duplicate(msg_id):
        return
    msg_type = msg.get("type", 0)
    content = msg.get("content", "")
    sender = msg.get("sender", "") or msg.get("wxid", "")
    room_id = msg.get("roomid", "")
    contact_name = msg.get("contact_name", "") or msg.get("name", sender)
    if room_id:
        return
    if not sender or sender == bot_wxid:
        return
    type_map = {1: "text", 3: "image", 34: "voice", 43: "video", 47: "sticker", 49: "file"}
    unified_type = type_map.get(msg_type, f"raw_{msg_type}")
    payload = {
        "account_wxid": bot_wxid,
        "from_wxid": sender,
        "from_name": contact_name,
        "msg_type": unified_type,
        "content": content,
        "raw_type": msg_type,
        "ts": int(time.time() * 1000),
    }
    if http_post(f"{backend}/api/wcf/message", payload):
        log.info("→ %s: %s", contact_name or sender, content[:80])
    else:
        log.warning("转发失败")


def poll_messages(backend: str, bot_wxid: str):
    while True:
        try:
            result = wxapi_get("msgList")
            if result and isinstance(result, dict):
                msgs = result.get("data", result.get("msgList", []))
                for msg in msgs:
                    forward_message(backend, bot_wxid, msg)
        except Exception as e:
            log.debug("轮询消息异常: %s", e)
        time.sleep(0.5)


def poll_send_queue(client: None, backend: str):
    while True:
        try:
            tasks = http_get(f"{backend}/api/wcf/send-queue")
            if tasks:
                for task in tasks:
                    to_wxid = task.get("toWxid", "")
                    text = task.get("text", "")
                    task_id = task.get("id", "")
                    if to_wxid and text:
                        try:
                            resp = wxapi_post("sendTextMsg", {"wxid": to_wxid, "content": text})
                            ok = resp and resp.get("code") == 200
                            log.info("发 → %s: %s (%s)", to_wxid, text[:60], "OK" if ok else "FAIL")
                        except Exception as e:
                            log.error("发送失败 %s: %s", to_wxid, e)
                    if task_id:
                        http_post(f"{backend}/api/wcf/send-ack", {"id": task_id})
        except Exception as e:
            log.warning("出站轮询异常: %s", e)
        time.sleep(POLL_INTERVAL)


def wait_for_port(port: int, timeout: int = 15) -> bool:
    for i in range(timeout * 2):
        try:
            s = __import__("socket").socket()
            s.settimeout(1)
            s.connect(("127.0.0.1", port))
            s.close()
            return True
        except:
            pass
        time.sleep(0.5)
    return False


# ============================================================
# 主入口
# ============================================================

def main():
    parser = argparse.ArgumentParser(description="wxhook 桥 — HTTP API 版 (19088)")
    parser.add_argument("--backend", default=BACKEND_DEFAULT)
    parser.add_argument("--wxid", default="")
    args = parser.parse_args()
    backend = args.backend.rstrip("/")

    log.info("启动 wxhook 桥（HTTP API 版）...")
    log.info("后端地址: %s", backend)

    pid = find_wechat_pid()
    if not pid:
        log.error("未找到微信进程，请先打开微信并登录")
        sys.exit(1)
    log.info("微信进程 PID: %d", pid)

    if not wait_for_port(DLL_PORT, timeout=3):
        # 找 DLL
        dll_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "wxhook-418.dll")
        if not os.path.exists(dll_path):
            dll_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "wxhook.dll")
        if not os.path.exists(dll_path):
            try:
                import wxhook
                dll_path = os.path.join(os.path.dirname(wxhook.__file__), "tools", "wxhook.dll")
            except ImportError:
                pass
        if not os.path.exists(dll_path):
            log.error("找不到 wxhook.dll")
            sys.exit(1)

        log.info("正在注入 DLL: %s", dll_path)
        result = inject_dll(dll_path, pid)
        if result is False:
            log.error("DLL 注入失败，请以管理员权限运行")
            sys.exit(1)

        # DllMain 调 CreateThread 可能被静默忽略（loader lock）
        # 等待 3 秒后再检查端口，如果没起来再等
        log.info("等待 HTTP API 就绪 (端口 %d)...最长等待 90 秒", DLL_PORT)
        if not wait_for_port(DLL_PORT, timeout=90):
            log.error("HTTP API 未就绪，注入后 DLL 可能未启动 HTTP 服务")
            log.info("请检查: 1) DebugView 过滤 WeChatApiVs2019 看是否有错误日志")
            log.info("          2) wxhook.dll 是否已正确覆盖为 4.1.8.27 版 (大小 373KB)")
            log.info("          3) 微信版本是否为 4.1.8.27")
            sys.exit(1)

    log.info("HTTP API 已就绪 (端口 %d)", DLL_PORT)

    user_info = wxapi_get("userInfo")
    if user_info and user_info.get("code") == 200:
        data = user_info.get("data", {})
        bot_wxid = args.wxid or data.get("wxid", "")
        log.info("登录用户: %s (%s)", data.get("name", ""), bot_wxid)
        http_post(f"{backend}/api/wcf/account", {
            "wxid": bot_wxid,
            "name": data.get("name", ""),
            "label": f"wxhook-{bot_wxid[:8]}",
            "port": DLL_PORT,
            "status": "online",
        })
    else:
        bot_wxid = ""
        log.warning("获取用户信息失败")

    if not bot_wxid:
        log.error("无法获取 wxid")
        sys.exit(1)

    threading.Thread(target=poll_messages, args=(backend, bot_wxid), daemon=True).start()
    log.info("消息接收线程已启动")
    threading.Thread(target=poll_send_queue, args=(None, backend), daemon=True).start()
    log.info("出站发送线程已启动")
    log.info("桥接运行中...")
    try:
        while True:
            time.sleep(10)
    except KeyboardInterrupt:
        log.info("关闭")


if __name__ == "__main__":
    main()
