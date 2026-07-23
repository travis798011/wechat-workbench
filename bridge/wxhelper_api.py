"""
wxhelper HTTP API 封装
对接 ttttupup/wxhelper 的 HTTP 接口
端口默认: 19088
"""
import requests
import json
import time

API_BASE = "http://127.0.0.1:19088"


def get_user_info(port=19088):
    """获取登录用户信息"""
    try:
        resp = requests.get(f"http://127.0.0.1:{port}/api/userInfo", timeout=5)
        data = resp.json()
        if data.get("code") == 0:
            return data.get("data", {})
        return {}
    except Exception as e:
        return {"error": str(e)}


def get_contacts(port=19088):
    """获取联系人列表"""
    try:
        resp = requests.get(f"http://127.0.0.1:{port}/api/contactList", timeout=10)
        data = resp.json()
        if data.get("code") == 0:
            return data.get("data", [])
        return []
    except Exception as e:
        return []


def send_text(wxid, text, port=19088):
    """发送文本消息"""
    try:
        payload = {"wxid": wxid, "content": text}
        resp = requests.post(
            f"http://127.0.0.1:{port}/api/sendTextMsg",
            json=payload,
            timeout=5,
        )
        return resp.json().get("code") == 0
    except Exception as e:
        return False


def get_latest_msg(port=19088):
    """获取最新消息（轮询用）"""
    try:
        resp = requests.get(f"http://127.0.0.1:{port}/api/msgList", timeout=10)
        data = resp.json()
        if data.get("code") == 0 and data.get("data"):
            return data["data"]
        return []
    except Exception as e:
        return []


def check_health(port=19088):
    """检查 wxhelper 是否正常运行"""
    try:
        resp = requests.get(f"http://127.0.0.1:{port}/api/userInfo", timeout=3)
        return resp.status_code == 200
    except:
        return False


def start_http(port=19088):
    """发送 startHttp 指令 (部分版本需要)"""
    try:
        resp = requests.get(f"http://127.0.0.1:{port}/api/startHttp", timeout=3)
        return resp.json()
    except Exception as e:
        return {"error": str(e)}
