#include "HttpServer.h"
#include "LoginInitHook.h"
#include "MessageCallback.h"
#include "WeChatBridge.h"

#include <Windows.h>

#include <memory>

namespace {
constexpr const char* kDefaultPrefix = "http://127.0.0.1:19088/api/";

WeChatBridge g_bridge;
MessageCallback g_callback;
std::unique_ptr<HttpServer> g_server;
HANDLE g_server_thread = nullptr;
HANDLE g_hook_thread = nullptr;
CRITICAL_SECTION g_lock;
bool g_lock_ready = false;
volatile LONG g_stopping = 0;

DWORD WINAPI ServeForeverThread(LPVOID) {
    if (g_server) {
        g_server->ServeForever();
    }
    ::OutputDebugStringA("[WeChatApiVs2019] server done\n");
    return 0;
}

extern "C" __declspec(dllexport) BOOL StartWeChatApiServer() {
    if (!g_lock_ready) return FALSE;

    EnterCriticalSection(&g_lock);
    if (g_server_thread) {
        LeaveCriticalSection(&g_lock);
        return TRUE;
    }
    LeaveCriticalSection(&g_lock);

    ::OutputDebugStringA("[WeChatApiVs2019] starting server\n");
    g_bridge.Initialize();

    EnterCriticalSection(&g_lock);
    g_server.reset(new HttpServer(g_bridge, g_callback));
    bool ok = g_server->Start(kDefaultPrefix);
    LeaveCriticalSection(&g_lock);

    if (!ok) {
        ::OutputDebugStringA("[WeChatApiVs2019] failed to start\n");
        return TRUE;
    }
    ::OutputDebugStringA("[WeChatApiVs2019] HTTP on :19088\n");

    g_server_thread = ::CreateThread(nullptr, 0, ServeForeverThread, nullptr, 0, nullptr);
    return TRUE;
}

DWORD WINAPI HookThread(LPVOID) {
    for (int i = 0; i < 120 && !::InterlockedCompareExchange(&g_stopping, 0, 0); ++i) {
        if (InstallLoginInitHook(&g_bridge, &g_callback)) {
            ::OutputDebugStringA("[WeChatApiVs2019] login init hook installed\n");
            return 0;
        }
        ::Sleep(500);
    }
    ::OutputDebugStringA("[WeChatApiVs2019] login init hook not installed\n");
    return 1;
}

extern "C" __declspec(dllexport) void StopWeChatApiServer() {
    if (!g_lock_ready) return;

    EnterCriticalSection(&g_lock);
    ::InterlockedExchange(&g_stopping, 1);
    if (g_server) {
        g_server->Stop();
    }
    HANDLE thread = g_server_thread;
    g_server_thread = nullptr;
    HANDLE hook_thread = g_hook_thread;
    g_hook_thread = nullptr;
    LeaveCriticalSection(&g_lock);

    if (thread) {
        ::WaitForSingleObject(thread, 3000);
        ::CloseHandle(thread);
    }
    if (hook_thread) {
        ::WaitForSingleObject(hook_thread, 3000);
        ::CloseHandle(hook_thread);
    }
}

extern "C" __declspec(dllexport) void UpdateWeChatProfileCache(const char* profile_json, const char* account_wxid) {
    if (!profile_json) return;
    g_bridge.UpdateProfileCacheJson(profile_json, account_wxid ? account_wxid : "");
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        ::DisableThreadLibraryCalls(module);
        ::InterlockedExchange(&g_stopping, 0);
        ::InitializeCriticalSection(&g_lock);
        g_lock_ready = true;
        StartWeChatApiServer();
        break;
    case DLL_PROCESS_DETACH:
        StopWeChatApiServer();
        if (g_lock_ready) {
            g_lock_ready = false;
            ::DeleteCriticalSection(&g_lock);
        }
        break;
    default:
        break;
    }
    return TRUE;
}
