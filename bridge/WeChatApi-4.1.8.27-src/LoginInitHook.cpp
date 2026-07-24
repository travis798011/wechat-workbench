#include "LoginInitHook.h"

#include "JsonLite.h"
#include "MessageCallback.h"
#include "WeChatBridge.h"
#include "WeChatOffsets.h"

#include <Windows.h>

#include <cstring>
#include <deque>
#include <set>
#include <vector>
#include <string>

namespace {
using namespace WeChatOffsets;

constexpr size_t kLoginInitPatchSize = 16;
constexpr size_t kProfileCachePatchSize = 22;
constexpr size_t kWeixinPrintPbPatchSize = 18;
constexpr bool kEnableUnsafeWeixinPrintPbHook = true;

const uint8_t kLoginInitPrologue[kLoginInitPatchSize] = {
    0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x57,
    0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57
};

const uint8_t kProfileCachePrologue[] = {
    0x48, 0x89, 0x5C, 0x24, 0x10,
    0x48, 0x89, 0x74, 0x24, 0x18,
    0x55, 0x57, 0x41, 0x56
};

const uint8_t kWeixinPrintPbPrologue[kWeixinPrintPbPatchSize] = {
    0x55,
    0x56,
    0x53,
    0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00,
    0x48, 0x8D, 0xAC, 0x24, 0x80, 0x00, 0x00, 0x00
};

using LoginInitFn = __int64(__fastcall*)(void* wxid, void* nickname);
using ProfileCacheFn = __int64(__fastcall*)(void* profile_json);
using WeixinPrintPbFn = __int64(__fastcall*)(void* scene, void* data, __int64 len);

WeChatBridge* g_bridge = nullptr;
MessageCallback* g_callback = nullptr;
LoginInitFn g_login_original = nullptr;
ProfileCacheFn g_profile_original = nullptr;
WeixinPrintPbFn g_weixin_printpb_original = nullptr;
void* g_login_trampoline = nullptr;
void* g_profile_trampoline = nullptr;
void* g_weixin_printpb_trampoline = nullptr;
uintptr_t g_login_target = 0;
uintptr_t g_profile_target = 0;
uintptr_t g_weixin_printpb_target = 0;
char g_login_status[256] = "not installed";
char g_profile_status[256] = "not installed";
char g_weixin_printpb_status[256] = "not installed";
CRITICAL_SECTION g_msg_seen_lock;
bool g_msg_seen_lock_ready = false;
std::set<unsigned long long> g_seen_msg_ids;
std::deque<unsigned long long> g_seen_msg_order;
volatile LONG g_msg_callback_count = 0;

void SetStatus(char (&dst)[256], const char* text) {
    strcpy_s(dst, text);
}

bool TryCopyMsvcString(void* value, char* out, size_t out_cap, size_t* out_size) {
    if (!value) return false;

    __try {
        auto* raw = reinterpret_cast<uint8_t*>(value);
        size_t size = *reinterpret_cast<size_t*>(raw + 0x10);
        size_t capacity = *reinterpret_cast<size_t*>(raw + 0x18);
        if (size == 0 || size >= out_cap) return false;

        const char* data = nullptr;
        if (capacity > 0xF) {
            data = *reinterpret_cast<const char**>(raw);
        } else {
            data = reinterpret_cast<const char*>(raw);
        }
        if (!data) return false;

        memcpy(out, data, size);
        out[size] = 0;
        *out_size = size;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::string ReadMsvcString(void* value) {
    constexpr size_t kMaxStringBytes = 256 * 1024;
    char* buf = static_cast<char*>(::HeapAlloc(::GetProcessHeap(), HEAP_ZERO_MEMORY, kMaxStringBytes));
    if (!buf) return "";

    size_t size = 0;
    if (!TryCopyMsvcString(value, buf, kMaxStringBytes, &size)) {
        ::HeapFree(::GetProcessHeap(), 0, buf);
        return "";
    }

    std::string result(buf, size);
    ::HeapFree(::GetProcessHeap(), 0, buf);
    return result;
}

bool WriteAbsoluteJump(uint8_t* at, size_t patch_size, void* to) {
    uint8_t patch[32]{};
    if (patch_size > sizeof(patch) || patch_size < 12) return false;

    DWORD old_protect = 0;
    if (!::VirtualProtect(at, patch_size, PAGE_EXECUTE_READWRITE, &old_protect)) {
        return false;
    }

    memset(patch, 0x90, patch_size);
    patch[0] = 0x48;
    patch[1] = 0xB8;
    *reinterpret_cast<uint64_t*>(patch + 2) = reinterpret_cast<uint64_t>(to);
    patch[10] = 0xFF;
    patch[11] = 0xE0;

    memcpy(at, patch, patch_size);
    ::FlushInstructionCache(::GetCurrentProcess(), at, patch_size);

    DWORD ignored = 0;
    ::VirtualProtect(at, patch_size, old_protect, &ignored);
    return true;
}

bool BuildTrampoline(uint8_t* target, size_t patch_size, void** trampoline_out) {
    uint8_t* tramp = static_cast<uint8_t*>(::VirtualAlloc(
        nullptr,
        patch_size + 16,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE));
    if (!tramp) return false;

    memcpy(tramp, target, patch_size);
    uint8_t* jump_back = tramp + patch_size;
    jump_back[0] = 0x48;
    jump_back[1] = 0xB8;
    *reinterpret_cast<uint64_t*>(jump_back + 2) = reinterpret_cast<uint64_t>(target + patch_size);
    jump_back[10] = 0xFF;
    jump_back[11] = 0xE0;

    ::FlushInstructionCache(::GetCurrentProcess(), tramp, patch_size + 12);
    *trampoline_out = tramp;
    return true;
}

bool IsExpectedFunction(uint8_t* target, const uint8_t* prologue, size_t prologue_size) {
    __try {
        return memcmp(target, prologue, prologue_size) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::string HexPtrLocal(uintptr_t value) {
    char buf[32]{};
    sprintf_s(buf, "0x%llX", static_cast<unsigned long long>(value));
    return buf;
}

std::string ExtractWxidFromProfileJson(const std::string& json) {
    size_t user = json.find("\"userName\"");
    if (user == std::string::npos) return "";
    size_t key = json.find("\"String\"", user);
    if (key == std::string::npos) return "";
    size_t colon = json.find(':', key);
    if (colon == std::string::npos) return "";
    size_t quote = json.find('"', colon + 1);
    if (quote == std::string::npos) return "";
    size_t end = json.find('"', quote + 1);
    if (end == std::string::npos || end <= quote + 1) return "";
    return json.substr(quote + 1, end - quote - 1);
}

bool IsLikelyPhone(const std::string& s) {
    if (s.size() < 7 || s.size() > 20) return false;
    size_t digits = 0;
    for (char c : s) {
        if (c >= '0' && c <= '9') ++digits;
        else if (c != '+' && c != '-' && c != ' ') return false;
    }
    return digits >= 7;
}

bool IsValidUtf8(const std::string& s) {
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            if (c < 0x20 && c != '\t' && c != '\r' && c != '\n') return false;
            ++i;
            continue;
        }

        size_t need = 0;
        uint32_t code = 0;
        if (c >= 0xC2 && c <= 0xDF) {
            need = 1;
            code = c & 0x1F;
        } else if (c >= 0xE0 && c <= 0xEF) {
            need = 2;
            code = c & 0x0F;
        } else if (c >= 0xF0 && c <= 0xF4) {
            need = 3;
            code = c & 0x07;
        } else {
            return false;
        }

        if (i + need >= s.size()) return false;
        for (size_t j = 1; j <= need; ++j) {
            unsigned char cc = static_cast<unsigned char>(s[i + j]);
            if ((cc & 0xC0) != 0x80) return false;
            code = (code << 6) | (cc & 0x3F);
        }

        if ((need == 2 && code < 0x800) || (need == 3 && code < 0x10000)) return false;
        if (code >= 0xD800 && code <= 0xDFFF) return false;
        if (code > 0x10FFFF) return false;
        i += need + 1;
    }
    return true;
}

bool HasUtf8NonAscii(const std::string& s) {
    for (unsigned char c : s) {
        if (c >= 0x80) return true;
    }
    return false;
}

bool IsUsefulProfileString(const std::string& s) {
    if (s.empty() || s.size() > 512) return false;
    if (!IsValidUtf8(s)) return false;
    if (s.find("http://") == 0 || s.find("https://") == 0) return false;
    if (s.find(".proto") != std::string::npos) return false;
    return true;
}

bool ReadVarint(const uint8_t* data, size_t len, size_t* pos, uint64_t* out) {
    uint64_t value = 0;
    size_t shift = 0;
    size_t i = *pos;
    while (i < len && shift < 64) {
        uint8_t b = data[i++];
        value |= static_cast<uint64_t>(b & 0x7F) << shift;
        if ((b & 0x80) == 0) {
            *pos = i;
            *out = value;
            return true;
        }
        shift += 7;
    }
    return false;
}

bool LooksLikeMessage(const uint8_t* data, size_t len);

bool SkipProtoValue(const uint8_t* data, size_t len, size_t* pos, uint64_t wire) {
    if (wire == 0) {
        uint64_t ignored = 0;
        return ReadVarint(data, len, pos, &ignored);
    }
    if (wire == 1) {
        if (*pos + 8 > len) return false;
        *pos += 8;
        return true;
    }
    if (wire == 2) {
        uint64_t n = 0;
        if (!ReadVarint(data, len, pos, &n) || n > len - *pos) return false;
        *pos += static_cast<size_t>(n);
        return true;
    }
    if (wire == 5) {
        if (*pos + 4 > len) return false;
        *pos += 4;
        return true;
    }
    return false;
}

bool ParseBuiltinString(const uint8_t* data, size_t len, std::string* value) {
    size_t pos = 0;
    while (pos < len) {
        uint64_t key = 0;
        if (!ReadVarint(data, len, &pos, &key)) return false;
        uint64_t field = key >> 3;
        uint64_t wire = key & 7;
        if (field == 1 && wire == 2) {
            uint64_t n = 0;
            if (!ReadVarint(data, len, &pos, &n) || n > len - pos || n > 1024 * 1024) return false;
            value->assign(reinterpret_cast<const char*>(data + pos), static_cast<size_t>(n));
            return IsValidUtf8(*value);
        }
        if (!SkipProtoValue(data, len, &pos, wire)) return false;
    }
    return false;
}

bool LooksLikeUserName(const std::string& s) {
    if (s.size() < 3 || s.size() > 128) return false;
    if (s.find("wxid_") == 0) return true;
    if (s.find("@chatroom") != std::string::npos) return true;
    if (s.find("gh_") == 0) return true;
    return false;
}

bool AlreadySeenMessage(unsigned long long id) {
    if (!id) return false;
    if (!g_msg_seen_lock_ready) return false;
    EnterCriticalSection(&g_msg_seen_lock);
    bool seen = g_seen_msg_ids.find(id) != g_seen_msg_ids.end();
    if (!seen) {
        g_seen_msg_ids.insert(id);
        g_seen_msg_order.push_back(id);
        while (g_seen_msg_order.size() > 512) {
            unsigned long long old = g_seen_msg_order.front();
            g_seen_msg_order.pop_front();
            g_seen_msg_ids.erase(old);
        }
    }
    LeaveCriticalSection(&g_msg_seen_lock);
    return seen;
}

bool ParseAddMsg(const uint8_t* data, size_t len, WeChatMessage* msg) {
    if (!data || len < 8 || len > 1024 * 1024) return false;
    std::string from;
    std::string to;
    std::string content;
    long long msgid = 0;
    long long new_msgid = 0;
    int msg_type = 0;
    long long create_time = 0;

    size_t pos = 0;
    while (pos < len) {
        uint64_t key = 0;
        if (!ReadVarint(data, len, &pos, &key)) return false;
        uint64_t field = key >> 3;
        uint64_t wire = key & 7;
        if (field == 0 || wire > 5) return false;

        if (wire == 0) {
            uint64_t value = 0;
            if (!ReadVarint(data, len, &pos, &value)) return false;
            if (field == 1) msgid = static_cast<long long>(value);
            else if (field == 4) msg_type = static_cast<int>(value);
            else if (field == 9) create_time = static_cast<long long>(value);
            else if (field == 12) new_msgid = static_cast<long long>(value);
        } else if (wire == 2) {
            uint64_t n64 = 0;
            if (!ReadVarint(data, len, &pos, &n64) || n64 > len - pos) return false;
            const uint8_t* p = data + pos;
            size_t n = static_cast<size_t>(n64);
            if (field == 2) {
                ParseBuiltinString(p, n, &from);
            } else if (field == 3) {
                ParseBuiltinString(p, n, &to);
            } else if (field == 5) {
                ParseBuiltinString(p, n, &content);
            }
            pos += n;
        } else {
            if (!SkipProtoValue(data, len, &pos, wire)) return false;
        }
    }

    if (content.empty() || msg_type <= 0) return false;
    if (!LooksLikeUserName(from) && !LooksLikeUserName(to)) return false;
    unsigned long long dedup_id = new_msgid ? static_cast<unsigned long long>(new_msgid)
                                            : static_cast<unsigned long long>(msgid);
    if (AlreadySeenMessage(dedup_id)) return false;

    std::string roomid;
    std::string sender = from;
    if (from.find("@chatroom") != std::string::npos) {
        roomid = from;
        size_t sep = content.find(":\n");
        if (sep != std::string::npos && sep > 0 && sep < 128) {
            sender = content.substr(0, sep);
            content = content.substr(sep + 2);
        }
    } else if (to.find("@chatroom") != std::string::npos) {
        roomid = to;
    }

    msg->wxid = roomid.empty() ? from : roomid;
    msg->sender = sender;
    msg->roomid = roomid;
    msg->content = content;
    msg->msgid = dedup_id ? static_cast<long long>(dedup_id) : msgid;
    msg->type = msg_type;
    msg->timestamp = create_time;
    return true;
}

void ParseAddMsgRecursive(const uint8_t* data, size_t len, int depth, std::vector<WeChatMessage>* out) {
    if (depth > 5 || !data || len < 8 || len > 1024 * 1024) return;

    WeChatMessage parsed;
    if (ParseAddMsg(data, len, &parsed)) {
        out->push_back(parsed);
        return;
    }

    size_t pos = 0;
    while (pos < len) {
        uint64_t key = 0;
        if (!ReadVarint(data, len, &pos, &key)) return;
        uint64_t field = key >> 3;
        uint64_t wire = key & 7;
        if (field == 0) return;
        if (wire == 2) {
            uint64_t n64 = 0;
            if (!ReadVarint(data, len, &pos, &n64) || n64 > len - pos) return;
            size_t n = static_cast<size_t>(n64);
            if (n >= 8 && LooksLikeMessage(data + pos, n)) {
                ParseAddMsgRecursive(data + pos, n, depth + 1, out);
            }
            pos += n;
        } else if (!SkipProtoValue(data, len, &pos, wire)) {
            return;
        }
    }
}

bool LooksLikeMessage(const uint8_t* data, size_t len) {
    if (len < 2 || len > 65536) return false;
    size_t pos = 0;
    int fields = 0;
    while (pos < len && fields < 3) {
        uint64_t key = 0;
        if (!ReadVarint(data, len, &pos, &key)) return false;
        uint64_t field = key >> 3;
        uint64_t wire = key & 7;
        if (field == 0 || wire > 5) return false;
        if (wire == 0) {
            uint64_t ignored = 0;
            if (!ReadVarint(data, len, &pos, &ignored)) return false;
        } else if (wire == 1) {
            if (pos + 8 > len) return false;
            pos += 8;
        } else if (wire == 2) {
            uint64_t n = 0;
            if (!ReadVarint(data, len, &pos, &n) || n > len - pos) return false;
            pos += static_cast<size_t>(n);
        } else if (wire == 5) {
            if (pos + 4 > len) return false;
            pos += 4;
        } else {
            return false;
        }
        ++fields;
    }
    return fields > 0;
}

void ParseProtoStrings(const uint8_t* data, size_t len, int depth, std::vector<std::string>* out) {
    if (depth > 8 || len < 2) return;

    size_t pos = 0;
    while (pos < len) {
        uint64_t key = 0;
        if (!ReadVarint(data, len, &pos, &key)) return;
        uint64_t field = key >> 3;
        uint64_t wire = key & 7;
        if (field == 0) return;

        if (wire == 0) {
            uint64_t ignored = 0;
            if (!ReadVarint(data, len, &pos, &ignored)) return;
        } else if (wire == 1) {
            if (pos + 8 > len) return;
            pos += 8;
        } else if (wire == 2) {
            uint64_t n64 = 0;
            if (!ReadVarint(data, len, &pos, &n64) || n64 > len - pos) return;
            size_t n = static_cast<size_t>(n64);
            const uint8_t* p = data + pos;
            if (n > 0 && n <= 4096) {
                std::string s(reinterpret_cast<const char*>(p), n);
                if (IsUsefulProfileString(s)) {
                    out->push_back(s);
                }
            }
            if (n >= 2 && LooksLikeMessage(p, n)) {
                ParseProtoStrings(p, n, depth + 1, out);
            }
            pos += n;
        } else if (wire == 5) {
            if (pos + 4 > len) return;
            pos += 4;
        } else {
            return;
        }
    }
}

void PickProfileStrings(const std::vector<std::string>& strings, std::string* wxid, std::string* nickname, std::string* mobile) {
    for (const std::string& s : strings) {
        if (wxid->empty() && s.find("wxid_") == 0) {
            *wxid = s;
            continue;
        }
        if (mobile->empty() && IsLikelyPhone(s)) {
            *mobile = s;
            continue;
        }
    }

    for (const std::string& s : strings) {
        if (!nickname->empty()) break;
        if (s == *wxid || s == *mobile) continue;
        if (!HasUtf8NonAscii(s)) continue;
        *nickname = s;
    }
}

std::string BuildCapturedProfileJson(const std::string& wxid, const std::string& nickname, const std::string& mobile) {
    return "{"
        "\"baseResponse\":{\"ret\":0,\"errMsg\":{}},"
        "\"userInfo\":{"
            "\"userName\":{\"String\":\"" + JsonLite::Escape(wxid) + "\"},"
            "\"nickName\":{\"String\":\"" + JsonLite::Escape(nickname) + "\"},"
            "\"bindMobile\":{\"String\":\"" + JsonLite::Escape(mobile) + "\"}"
        "},"
        "\"userInfoExt\":{},"
        "\"source\":\"weixin_printpb_hook\""
        "}";
}

void CaptureProfileFromPb(void* data_arg, __int64 len_arg) {
    if (!g_bridge || !data_arg || len_arg <= 0 || len_arg > 1024 * 1024) return;

    size_t len = static_cast<size_t>(len_arg);
    uint8_t* copy = static_cast<uint8_t*>(::HeapAlloc(::GetProcessHeap(), 0, len));
    if (!copy) return;

    SIZE_T bytes_read = 0;
    if (!::ReadProcessMemory(::GetCurrentProcess(), data_arg, copy, len, &bytes_read) || bytes_read != len) {
        ::HeapFree(::GetProcessHeap(), 0, copy);
        return;
    }

    std::vector<std::string> strings;
    ParseProtoStrings(copy, len, 0, &strings);
    g_bridge->CaptureA8KeyResponseBytes(copy, len, "weixin_printpb_hook");

    std::vector<WeChatMessage> messages;
    ParseAddMsgRecursive(copy, len, 0, &messages);
    if (g_callback) {
        for (const WeChatMessage& msg : messages) {
            if (g_callback->PublishIncomingMessageAsync(msg)) {
                ::InterlockedIncrement(&g_msg_callback_count);
            }
        }
    }

    ::HeapFree(::GetProcessHeap(), 0, copy);

    std::string wxid, nickname, mobile;
    PickProfileStrings(strings, &wxid, &nickname, &mobile);

    if (!wxid.empty() && (!nickname.empty() || !mobile.empty())) {
        g_bridge->UpdateProfileCacheJson(BuildCapturedProfileJson(wxid, nickname, mobile), wxid);
    }
}

__int64 __fastcall LoginInitHook(void* wxid_arg, void* nick_arg) {
    std::string wxid = ReadMsvcString(wxid_arg);
    std::string nickname = ReadMsvcString(nick_arg);

    if (g_bridge && !wxid.empty()) {
        g_bridge->UpdateLoginInitProfile(wxid, nickname, "ida_login_init_hook");
    }

    return g_login_original ? g_login_original(wxid_arg, nick_arg) : 0;
}

__int64 __fastcall ProfileCacheHook(void* profile_json_arg) {
    std::string profile_json = ReadMsvcString(profile_json_arg);
    if (g_bridge && !profile_json.empty() && profile_json.find("\"userInfo\"") != std::string::npos) {
        g_bridge->UpdateProfileCacheJson(profile_json, ExtractWxidFromProfileJson(profile_json));
    }

    return g_profile_original ? g_profile_original(profile_json_arg) : 0;
}

__int64 __fastcall WeixinPrintPbHook(void* scene, void* data, __int64 len) {
    CaptureProfileFromPb(data, len);
    return g_weixin_printpb_original ? g_weixin_printpb_original(scene, data, len) : 0;
}
}

bool InstallLoginInitHook(WeChatBridge* bridge, MessageCallback* callback) {
    g_bridge = bridge;
    g_callback = callback;

    if (!g_msg_seen_lock_ready) {
        ::InitializeCriticalSection(&g_msg_seen_lock);
        g_msg_seen_lock_ready = true;
    }

    HMODULE module = ::GetModuleHandleA(kLibGlesModuleName);
    if (!module) {
        SetStatus(g_login_status, "libGLESv1.dll is not loaded");
        SetStatus(g_profile_status, "libGLESv1.dll is not loaded");
    }

    HMODULE weixin = ::GetModuleHandleA(kWeixinModuleName);
    if (!weixin) {
        SetStatus(g_weixin_printpb_status, "Weixin.dll is not loaded");
    }

    if (!kEnableUnsafeWeixinPrintPbHook) {
        SetStatus(g_weixin_printpb_status, "disabled: unsafe direct Weixin hook");
    } else if (!g_weixin_printpb_original && weixin) {
        auto* target = reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(weixin) + kWeixinPrintPbRva);
        if (!IsExpectedFunction(target, kWeixinPrintPbPrologue, sizeof(kWeixinPrintPbPrologue))) {
            SetStatus(g_weixin_printpb_status, "target prologue mismatch; skip hook");
        } else if (!BuildTrampoline(target, kWeixinPrintPbPatchSize, &g_weixin_printpb_trampoline)) {
            SetStatus(g_weixin_printpb_status, "failed to allocate trampoline");
        } else {
            g_weixin_printpb_original = reinterpret_cast<WeixinPrintPbFn>(g_weixin_printpb_trampoline);
            if (!WriteAbsoluteJump(target, kWeixinPrintPbPatchSize, reinterpret_cast<void*>(&WeixinPrintPbHook))) {
                SetStatus(g_weixin_printpb_status, "failed to patch target");
                g_weixin_printpb_original = nullptr;
            } else {
                g_weixin_printpb_target = reinterpret_cast<uintptr_t>(target);
                SetStatus(g_weixin_printpb_status, "installed");
            }
        }
    }

    if (!module) {
        return g_weixin_printpb_original != nullptr;
    }

    if (!g_login_original) {
        auto* target = reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(module) + kOriginalLoginInitRva);
        if (!IsExpectedFunction(target, kLoginInitPrologue, sizeof(kLoginInitPrologue))) {
            SetStatus(g_login_status, "target prologue mismatch; skip hook");
        } else if (!BuildTrampoline(target, kLoginInitPatchSize, &g_login_trampoline)) {
            SetStatus(g_login_status, "failed to allocate trampoline");
        } else {
            g_login_original = reinterpret_cast<LoginInitFn>(g_login_trampoline);
            if (!WriteAbsoluteJump(target, kLoginInitPatchSize, reinterpret_cast<void*>(&LoginInitHook))) {
                SetStatus(g_login_status, "failed to patch target");
                g_login_original = nullptr;
            } else {
                g_login_target = reinterpret_cast<uintptr_t>(target);
                SetStatus(g_login_status, "installed");
            }
        }
    }

    if (!g_profile_original) {
        auto* target = reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(module) + kOriginalProfileCacheRva);
        if (!IsExpectedFunction(target, kProfileCachePrologue, sizeof(kProfileCachePrologue))) {
            SetStatus(g_profile_status, "target prologue mismatch; skip hook");
        } else if (!BuildTrampoline(target, kProfileCachePatchSize, &g_profile_trampoline)) {
            SetStatus(g_profile_status, "failed to allocate trampoline");
        } else {
            g_profile_original = reinterpret_cast<ProfileCacheFn>(g_profile_trampoline);
            if (!WriteAbsoluteJump(target, kProfileCachePatchSize, reinterpret_cast<void*>(&ProfileCacheHook))) {
                SetStatus(g_profile_status, "failed to patch target");
                g_profile_original = nullptr;
            } else {
                g_profile_target = reinterpret_cast<uintptr_t>(target);
                SetStatus(g_profile_status, "installed");
            }
        }
    }

    return g_weixin_printpb_original != nullptr || (g_login_original != nullptr && g_profile_original != nullptr);
}

std::string LoginInitHookStatusJson() {
    return "{"
        "\"login_init\":{"
            + JsonLite::StringField("status", g_login_status) + ","
            + JsonLite::StringField("target_hex", g_login_target ? HexPtrLocal(g_login_target) : "0x0")
        + "},"
        "\"profile_cache\":{"
            + JsonLite::StringField("status", g_profile_status) + ","
            + JsonLite::StringField("target_hex", g_profile_target ? HexPtrLocal(g_profile_target) : "0x0")
        + "},"
        "\"weixin_printpb\":{"
            + JsonLite::StringField("status", g_weixin_printpb_status) + ","
            + JsonLite::StringField("target_hex", g_weixin_printpb_target ? HexPtrLocal(g_weixin_printpb_target) : "0x0")
        + "},"
        "\"message_callback\":{"
            + JsonLite::StringField("status", g_callback ? "installed" : "not configured") + ","
            + JsonLite::NumberField("callback_count", ::InterlockedCompareExchange(&g_msg_callback_count, 0, 0))
        + "}"
        + "}";
}
