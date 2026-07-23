"""
微信 DLL 注入工具 (纯 Python，无需任何 exe)
用法: python inject.py
注意: 必须以管理员身份运行
"""

import ctypes
import ctypes.wintypes
import subprocess
import sys
import os

# ===== 配置 =====
# 这里只配置 DLL 路径，进程名自动检测
DLL_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "wxhelper.dll")
# 微信可能的进程名 (不同版本可能不同)
POSSIBLE_NAMES = ["WeChatAppEx.exe", "WeChat.exe", "Weixin.exe", "wechat.exe"]


def find_pid_by_name() -> int | None:
    """通过进程名查找 PID，尝试多个可能的进程名"""
    for name in POSSIBLE_NAMES:
        result = subprocess.run(
            ["tasklist", "/FI", f"IMAGENAME eq {name}", "/FO", "CSV"],
            capture_output=True, text=True,
        )
        for line in result.stdout.strip().split("\n"):
            if name.lower() in line.lower():
                try:
                    pid = int(line.split('"')[3])
                    print(f"  发现进程: {name} PID: {pid}")
                    return pid
                except (IndexError, ValueError):
                    continue
    return None


def inject_dll(pid: int, dll_path: str) -> bool:
    """将 DLL 注入到指定进程"""
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

    # 定义参数和返回类型
    kernel32.OpenProcess.argtypes = [ctypes.wintypes.DWORD, ctypes.wintypes.BOOL, ctypes.wintypes.DWORD]
    kernel32.OpenProcess.restype = ctypes.wintypes.HANDLE

    kernel32.VirtualAllocEx.argtypes = [ctypes.wintypes.HANDLE, ctypes.wintypes.LPVOID,
                                         ctypes.c_size_t, ctypes.wintypes.DWORD, ctypes.wintypes.DWORD]
    kernel32.VirtualAllocEx.restype = ctypes.wintypes.LPVOID

    kernel32.WriteProcessMemory.argtypes = [ctypes.wintypes.HANDLE, ctypes.wintypes.LPVOID,
                                             ctypes.wintypes.LPCVOID, ctypes.c_size_t,
                                             ctypes.POINTER(ctypes.c_size_t)]
    kernel32.WriteProcessMemory.restype = ctypes.wintypes.BOOL

    kernel32.GetModuleHandleW.argtypes = [ctypes.wintypes.LPCWSTR]
    kernel32.GetModuleHandleW.restype = ctypes.wintypes.HMODULE

    kernel32.GetProcAddress.argtypes = [ctypes.wintypes.HMODULE, ctypes.wintypes.LPCSTR]
    kernel32.GetProcAddress.restype = ctypes.wintypes.LPVOID

    kernel32.CreateRemoteThread.argtypes = [ctypes.wintypes.HANDLE, ctypes.wintypes.LPVOID,
                                             ctypes.c_size_t, ctypes.wintypes.LPVOID,
                                             ctypes.wintypes.LPVOID, ctypes.wintypes.DWORD,
                                             ctypes.wintypes.LPVOID]
    kernel32.CreateRemoteThread.restype = ctypes.wintypes.HANDLE

    kernel32.WaitForSingleObject.argtypes = [ctypes.wintypes.HANDLE, ctypes.wintypes.DWORD]
    kernel32.WaitForSingleObject.restype = ctypes.wintypes.DWORD

    kernel32.CloseHandle.argtypes = [ctypes.wintypes.HANDLE]
    kernel32.CloseHandle.restype = ctypes.wintypes.BOOL

    # 1. 打开目标进程
    PROCESS_ALL_ACCESS = 0x1F0FFF
    h_process = kernel32.OpenProcess(PROCESS_ALL_ACCESS, False, pid)
    if not h_process:
        err = ctypes.get_last_error()
        print(f"  OpenProcess 失败 (错误码: {err})")
        print(f"  请以管理员身份运行此脚本")
        return False

    # 2. 在目标进程分配内存
    dll_bytes = dll_path.encode("utf-8") + b"\x00"
    alloc_size = len(dll_bytes)
    p_dll_path = kernel32.VirtualAllocEx(h_process, None, alloc_size, 0x1000, 0x04)
    if not p_dll_path:
        print(f"  VirtualAllocEx 失败")
        kernel32.CloseHandle(h_process)
        return False

    # 3. 写入 DLL 路径字符串
    written = ctypes.c_size_t(0)
    kernel32.WriteProcessMemory(h_process, p_dll_path, dll_bytes, alloc_size, ctypes.byref(written))

    # 4. 获取 LoadLibraryA 地址
    h_kernel32 = kernel32.GetModuleHandleW("kernel32.dll")
    p_load_library = kernel32.GetProcAddress(h_kernel32, b"LoadLibraryA")
    if not p_load_library:
        print(f"  GetProcAddress(LoadLibraryA) 失败")
        kernel32.CloseHandle(h_process)
        return False

    # 5. 创建远程线程加载 DLL
    h_thread = kernel32.CreateRemoteThread(h_process, None, 0, p_load_library, p_dll_path, 0, None)
    if not h_thread:
        print(f"  CreateRemoteThread 失败")
        kernel32.CloseHandle(h_process)
        return False

    # 6. 等待线程完成
    kernel32.WaitForSingleObject(h_thread, 5000)
    kernel32.CloseHandle(h_thread)
    kernel32.CloseHandle(h_process)

    return True


def main():
    print("=" * 50)
    print("  微信 DLL 注入工具")
    print("=" * 50)
    print()

    # 检查管理员权限
    try:
        is_admin = ctypes.windll.shell32.IsUserAnAdmin()
    except:
        is_admin = False

    if not is_admin:
        print("⚠️  建议以管理员身份运行 (右键 CMD → 以管理员身份运行)")
        print()

    # 检查 DLL 是否存在
    dll_path = DLL_PATH
    if not os.path.exists(dll_path):
        print(f"❌ 找不到 DLL 文件: {dll_path}")
        print()
        print("   请修改脚本开头的 DLL_PATH 为正确的路径")
        print("   或把 wxhelper.dll 放当前目录")
        sys.exit(1)

    print(f"DLL 路径: {dll_path}")
    print()

    # 查找微信进程
    pid = find_pid_by_name()
    if not pid:
        print("❌ 未找到微信进程，请先启动微信")
        print(f"   检查以下进程名: {', '.join(POSSIBLE_NAMES)}")
        print()
        print("   也可以输入 PID 手动指定:")
        try:
            manual = input("   输入微信 PID (留空退出): ").strip()
            if manual:
                pid = int(manual)
            else:
                sys.exit(1)
        except (ValueError, EOFError):
            sys.exit(1)

    print(f"✅ 找到微信进程 PID: {pid}")
    print(f"   正在注入 DLL ...")
    print()

    # 注入
    success = inject_dll(pid, dll_path)
    if success:
        print("✅ DLL 注入成功！")
        print()
        print("   wxhelper 已启动，监听端口: 19088")
        print("   现在可以启动聚合桥了:")
        print()
        print("   python bridge/wcf-bridge.py --gateway http://127.0.0.1:3028 --ports 19088")
    else:
        print("❌ DLL 注入失败")

    print()
    input("按 Enter 退出...")


if __name__ == "__main__":
    main()
