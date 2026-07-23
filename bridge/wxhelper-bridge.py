"""
微信聚合桥 (wxhelper 版 v2)

wxhelper 所有 API 都是 POST 方式。
单个实例对接，支持多账号。
"""

import argparse
import logging
import time
import requests
import json

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s %(message)s",
)
log = logging.getLogger("bridge")

DEFAULT_GATEWAY = "http://127.0.0.1:3028"
POLL_INTERVAL = 0.5


def api_post(port, path, data=None):
    """调用 wxhelper POST API"""
    try:
        url = f"http://127.0.0.1:{port}{path}"
        r = requests.post(url, json=data or {}, timeout=10)
        if r.status_code == 200:
            return r.json()
        return {"code": -1, "msg": f"HTTP {r.status_code}"}
    except Exception as e:
        return {"code": -1, "msg": str(e)}


def check_wxhelper(port):
    res = api_post(port, "/api/userInfo")
    return res.get("code") == 200


def get_user_info(port):
    res = api_post(port, "/api/userInfo")
    if res.get("code") == 200:
        return res.get("data", {})
    return {}


def get_contacts(port):
    res = api_post(port, "/api/contactList")
    if res.get("code") == 200:
        return res.get("data", [])
    return []


def get_messages(port):
    res = api_post(port, "/api/msgList")
    if res.get("code") == 200:
        return res.get("data", [])
    return []


def send_text(port, wxid, text):
    res = api_post(port, "/api/sendTextMsg", {"wxid": wxid, "content": text})
    return res.get("code") == 200


def start_http(port):
    """一些 wxhelper 版本需要先调用 startHttp"""
    res = api_post(port, "/api/startHttp")
    return res.get("code") == 200


def register_account(gateway, port, info):
    try:
        payload = {
            "wxid": info.get("wxid", f"port-{port}"),
            "name": info.get("name", info.get("wxid", f"wx-{port}")),
            "label": f"port-{port}",
            "port": port,
            "status": "online",
        }
        requests.post(f"{gateway}/api/wcf/account", json=payload, timeout=5)
    except Exception as e:
        log.warning(f"Register failed (gateway not ready yet?): {e}")


def forward_message(gateway, msg, port, my_wxid):
    if not msg.get("wxid") or msg.get("wxid") == my_wxid:
        return
    if msg.get("roomid"):
        return

    content = msg.get("content", "")
    raw_type = msg.get("type", 1)
    type_map = {1: "text", 3: "image", 34: "voice", 43: "video", 47: "sticker", 49: "file"}

    payload = {
        "account_wxid": my_wxid,
        "from_wxid": msg.get("wxid", ""),
        "from_name": msg.get("wxid", ""),
        "msg_type": type_map.get(raw_type, f"raw_{raw_type}"),
        "content": content,
        "raw_type": raw_type,
        "ts": int(time.time() * 1000),
    }

    try:
        requests.post(f"{gateway}/api/wcf/message", json=payload, timeout=5)
    except:
        pass


def check_send_queue(gateway):
    try:
        r = requests.get(f"{gateway}/api/wcf/send-queue", timeout=5)
        if r.status_code == 200:
            return r.json()
    except:
        pass
    return []


def ack_send(gateway, msg_id):
    if not msg_id:
        return
    try:
        requests.post(f"{gateway}/api/wcf/send-ack", json={"id": msg_id}, timeout=3)
    except:
        pass


def run_instance(port, gateway):
    log.info(f"[{port}] Connecting to wxhelper...")

    if not check_wxhelper(port):
        log.warning(f"[{port}] wxhelper not responding")
        return

    info = get_user_info(port)
    my_wxid = info.get("wxid", "")
    my_name = info.get("name", info.get("wxid", f"wx-{port}"))
    contacts = get_contacts(port)

    log.info(f"[{port}] User: {my_name} ({my_wxid}), contacts: {len(contacts)}")

    # 等待网关
    for i in range(30):
        try:
            requests.get(f"{gateway}/api/health", timeout=3)
            break
        except:
            if i == 0:
                log.info(f"  Waiting for gateway at {gateway}...")
            time.sleep(2)

    register_account(gateway, port, info)

    seen = set()
    while True:
        try:
            msgs = get_messages(port)
            for msg in msgs:
                msg_id = str(msg.get("id", ""))
                if not msg_id or msg_id in seen:
                    continue
                seen.add(msg_id)
                if len(seen) > 2000:
                    seen.clear()
                forward_message(gateway, msg, port, my_wxid)

            queue = check_send_queue(gateway)
            for task in queue:
                if task.get("accountWxid") == my_wxid:
                    ok = send_text(port, task["toWxid"], task["text"])
                    log.info(f"[{port}] Send {'OK' if ok else 'FAIL'} to {task['toWxid']}")
                    ack_send(gateway, task.get("id"))

        except Exception as e:
            log.error(f"[{port}] {type(e).__name__}: {e}")

        time.sleep(POLL_INTERVAL)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--gateway", default=DEFAULT_GATEWAY)
    parser.add_argument("--instances", default="19088")

    args = parser.parse_args()
    ports = [int(p.strip()) for p in args.instances.split(",")]

    log.info(f"Starting: gateway={args.gateway} instances={ports}")

    import threading
    for port in ports:
        t = threading.Thread(target=run_instance, args=(port, args.gateway), daemon=True)
        t.start()

    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        log.info("Shutdown")


if __name__ == "__main__":
    main()
