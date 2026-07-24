#include "WeChatBridge.h"

#include "JsonLite.h"
#include "WeChatOffsets.h"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <ctime>
#include <map>
#include <memory>
#include <mutex>
#include <atomic>
#include <new>
#include <unordered_map>
#include <vector>

namespace {
using namespace WeChatOffsets;

constexpr size_t kTaskDispatchPatchSize = 17;
constexpr int kMpGetA8KeyCgiType = 0xEE;
constexpr int kSendAppMsgCgiType = 0xDE;

bool NonEmpty(const std::string& s) {
    return !s.empty();
}

std::string HexPtr(uintptr_t value) {
    char buf[32]{};
    sprintf_s(buf, "0x%llX", static_cast<unsigned long long>(value));
    return buf;
}

struct MsvcString {
    union {
        char small[16];
        char* ptr;
    };
    size_t size;
    size_t capacity;

    MsvcString() : size(0), capacity(15) {
        small[0] = 0;
    }

    explicit MsvcString(const std::string& value) : MsvcString() {
        assign(value);
    }

    ~MsvcString() {
        if (capacity > 15 && ptr) {
            ::operator delete(ptr);
        }
    }

    MsvcString(const MsvcString&) = delete;
    MsvcString& operator=(const MsvcString&) = delete;

    void assign(const std::string& value) {
        if (capacity > 15 && ptr) {
            ::operator delete(ptr);
        }
        size = value.size();
        if (size <= 15) {
            capacity = 15;
            memset(small, 0, sizeof(small));
            memcpy(small, value.data(), size);
        } else {
            capacity = size | 15;
            ptr = static_cast<char*>(::operator new(capacity + 1, std::nothrow));
            if (ptr) {
                memcpy(ptr, value.data(), size);
                ptr[size] = 0;
            } else {
                size = 0;
                capacity = 15;
                small[0] = 0;
            }
        }
    }

    const char* data_ptr() const {
        return capacity > 15 ? ptr : small;
    }
};

struct MsvcWString {
    union {
        wchar_t small[8];
        wchar_t* ptr;
    };
    size_t size;
    size_t capacity;
};

void AssignRemoteString(void* dst, const MsvcString& src) {
    auto* d = reinterpret_cast<MsvcString*>(dst);
    const char* p = src.data_ptr();
    d->assign(std::string(p, src.size));
}

void AssignFreshMsvcString(void* dst, const std::string& value) {
    auto* d = reinterpret_cast<MsvcString*>(dst);
    d->size = value.size();
    if (value.size() <= 15) {
        d->capacity = 15;
        memset(d->small, 0, sizeof(d->small));
        memcpy(d->small, value.data(), value.size());
        return;
    }

    d->capacity = value.size() | 15;
    d->ptr = static_cast<char*>(::operator new(d->capacity + 1, std::nothrow));
    if (!d->ptr) {
        d->size = 0;
        d->capacity = 15;
        d->small[0] = 0;
        return;
    }
    memcpy(d->ptr, value.data(), value.size());
    d->ptr[value.size()] = 0;
}

void AssignFreshMsvcWString(void* dst, const std::wstring& value) {
    auto* d = reinterpret_cast<MsvcWString*>(dst);
    d->size = value.size();
    if (value.size() <= 7) {
        d->capacity = 7;
        memset(d->small, 0, sizeof(d->small));
        memcpy(d->small, value.data(), value.size() * sizeof(wchar_t));
        return;
    }

    d->capacity = value.size() | 7;
    d->ptr = static_cast<wchar_t*>(::operator new((d->capacity + 1) * sizeof(wchar_t), std::nothrow));
    if (!d->ptr) {
        d->size = 0;
        d->capacity = 7;
        d->small[0] = 0;
        return;
    }
    memcpy(d->ptr, value.data(), value.size() * sizeof(wchar_t));
    d->ptr[value.size()] = 0;
}

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) return std::wstring();
    int needed = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, const_cast<char*>(value.data()),
        static_cast<int>(value.size()), nullptr, 0);
    UINT code_page = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    if (needed <= 0) {
        code_page = CP_ACP;
        flags = 0;
        needed = ::MultiByteToWideChar(code_page, flags, const_cast<char*>(value.data()),
            static_cast<int>(value.size()), nullptr, 0);
    }
    if (needed <= 0) return std::wstring(value.begin(), value.end());
    std::wstring out(static_cast<size_t>(needed), L'\0');
    ::MultiByteToWideChar(code_page, flags, const_cast<char*>(value.data()), static_cast<int>(value.size()),
        &out[0], needed);
    return out;
}

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) return std::string();
    int needed = ::WideCharToMultiByte(CP_UTF8, 0, const_cast<wchar_t*>(value.data()), static_cast<int>(value.size()),
        nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return std::string();
    std::string out(static_cast<size_t>(needed), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, const_cast<wchar_t*>(value.data()), static_cast<int>(value.size()),
        &out[0], needed, nullptr, nullptr);
    return out;
}

std::string WideToWeChatMb(const std::wstring& value) {
    if (value.empty()) return std::string();
    constexpr UINT kWeChatCodePage = 0xFDE9;
    int needed = ::WideCharToMultiByte(kWeChatCodePage, 0, const_cast<wchar_t*>(value.data()), static_cast<int>(value.size()),
        nullptr, 0, nullptr, nullptr);
    UINT code_page = kWeChatCodePage;
    if (needed <= 0) {
        code_page = CP_UTF8;
        needed = ::WideCharToMultiByte(code_page, 0, const_cast<wchar_t*>(value.data()), static_cast<int>(value.size()),
            nullptr, 0, nullptr, nullptr);
    }
    if (needed <= 0) return std::string();
    std::string out(static_cast<size_t>(needed), '\0');
    ::WideCharToMultiByte(code_page, 0, const_cast<wchar_t*>(value.data()), static_cast<int>(value.size()),
        &out[0], needed, nullptr, nullptr);
    return out;
}

std::wstring BaseNameOfPath(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return path;
    return path.substr(pos + 1);
}

uint64_t FileSizeOfPath(const std::wstring& path) {
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return 0;
    LARGE_INTEGER size{};
    BOOL ok = ::GetFileSizeEx(h, &size);
    ::CloseHandle(h);
    if (!ok || size.QuadPart < 0) return 0;
    return static_cast<uint64_t>(size.QuadPart);
}

std::string EscapeXml(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&apos;"; break;
        default: out.push_back(c); break;
        }
    }
    return out;
}

void* HeapAllocZero(size_t size) {
    return ::HeapAlloc(::GetProcessHeap(), HEAP_ZERO_MEMORY, size);
}

void WriteVarint(std::vector<uint8_t>* out, uint64_t value) {
    while (value >= 0x80) {
        out->push_back(static_cast<uint8_t>(value | 0x80));
        value >>= 7;
    }
    out->push_back(static_cast<uint8_t>(value));
}

void WriteTag(std::vector<uint8_t>* out, uint32_t field, uint32_t wire_type) {
    WriteVarint(out, (static_cast<uint64_t>(field) << 3) | wire_type);
}

void WriteVarintField(std::vector<uint8_t>* out, uint32_t field, uint64_t value) {
    WriteTag(out, field, 0);
    WriteVarint(out, value);
}

void WriteBytesField(std::vector<uint8_t>* out, uint32_t field, const void* data, size_t size) {
    WriteTag(out, field, 2);
    WriteVarint(out, size);
    const auto* p = static_cast<const uint8_t*>(data);
    out->insert(out->end(), p, p + size);
}

void WriteStringField(std::vector<uint8_t>* out, uint32_t field, const std::string& value) {
    WriteBytesField(out, field, value.data(), value.size());
}

std::vector<uint8_t> BuiltinString(const std::string& value) {
    std::vector<uint8_t> msg;
    WriteStringField(&msg, 1, value);
    return msg;
}

std::vector<uint8_t> BuildBaseRequest() {
    std::vector<uint8_t> msg;
    const char device[] = "Windows";
    WriteBytesField(&msg, 1, "", 0);
    WriteVarintField(&msg, 2, 0);
    WriteBytesField(&msg, 3, device, sizeof(device) - 1);
    WriteVarintField(&msg, 4, 0);
    WriteBytesField(&msg, 5, device, sizeof(device) - 1);
    WriteVarintField(&msg, 6, 0);
    return msg;
}

std::vector<uint8_t> BuildGetA8KeyReq(const A8KeyRequest& req) {
    std::vector<uint8_t> msg;
    std::vector<uint8_t> base = BuildBaseRequest();
    std::vector<uint8_t> req_url = BuiltinString(req.url);
    WriteBytesField(&msg, 1, base.data(), base.size());
    WriteVarintField(&msg, 2, 2);
    WriteBytesField(&msg, 7, req_url.data(), req_url.size());
    WriteVarintField(&msg, 10, static_cast<uint32_t>(req.scene));
    WriteVarintField(&msg, 14, 8);
    WriteVarintField(&msg, 20, static_cast<uint32_t>(req.url_type));
    WriteVarintField(&msg, 31, static_cast<uint32_t>(::GetTickCount64()));
    return msg;
}

std::string BuildLinkAppMsgXml(const std::string& from_wxid, const XmlRequest& req) {
    std::string xml;
    xml.reserve(req.title.size() + req.description.size() + req.thumb_url.size() + req.url.size() + 512);
    xml += "<appmsg appid=\"\" sdkver=\"0\">";
    xml += "<title>" + EscapeXml(req.title) + "</title>";
    xml += "<des>" + EscapeXml(req.description) + "</des>";
    xml += "<action></action>";
    xml += "<type>5</type>";
    xml += "<showtype>0</showtype>";
    xml += "<soundtype>0</soundtype>";
    xml += "<mediatagname></mediatagname>";
    xml += "<messageext></messageext>";
    xml += "<messageaction></messageaction>";
    xml += "<content></content>";
    xml += "<contentattr>0</contentattr>";
    xml += "<url>" + EscapeXml(req.url) + "</url>";
    xml += "<lowurl></lowurl>";
    xml += "<dataurl></dataurl>";
    xml += "<lowdataurl></lowdataurl>";
    xml += "<songalbumurl></songalbumurl>";
    xml += "<songlyric></songlyric>";
    xml += "<template_id></template_id>";
    xml += "<appattach>";
    xml += "<totallen>0</totallen><attachid></attachid><emoticonmd5></emoticonmd5>";
    xml += "<fileext></fileext><aeskey></aeskey>";
    xml += "</appattach>";
    xml += "<extinfo></extinfo>";
    xml += "<sourceusername></sourceusername>";
    xml += "<sourcedisplayname></sourcedisplayname>";
    xml += "<thumburl>" + EscapeXml(req.thumb_url) + "</thumburl>";
    xml += "<md5></md5>";
    xml += "<statextstr></statextstr>";
    xml += "<webviewshared></webviewshared>";
    xml += "<fromusername>" + EscapeXml(from_wxid) + "</fromusername>";
    xml += "</appmsg>";
    return xml;
}

std::string ClientMsgId() {
    char buf[64]{};
    sprintf_s(buf, "%llu%lu", static_cast<unsigned long long>(::GetTickCount64()), ::GetCurrentThreadId());
    return buf;
}

std::vector<uint8_t> BuildAppMsg(const std::string& from_wxid, const std::string& to_wxid,
                                 const std::string& xml, int type) {
    std::vector<uint8_t> msg;
    WriteStringField(&msg, 1, from_wxid);
    WriteVarintField(&msg, 3, 0);
    WriteStringField(&msg, 4, to_wxid);
    WriteVarintField(&msg, 5, static_cast<uint32_t>(type));
    WriteStringField(&msg, 6, xml);
    WriteVarintField(&msg, 7, static_cast<uint32_t>(std::time(nullptr)));
    WriteStringField(&msg, 8, ClientMsgId());
    WriteStringField(&msg, 12, "<msgsource><bizflag>0</bizflag></msgsource>");
    return msg;
}

std::vector<uint8_t> BuildSendAppMsgReq(const std::string& from_wxid, const XmlRequest& req) {
    std::vector<uint8_t> out;
    std::vector<uint8_t> base = BuildBaseRequest();
    std::string xml = BuildLinkAppMsgXml(from_wxid, req);
    std::vector<uint8_t> appmsg = BuildAppMsg(from_wxid, req.wxid, xml, 5);
    WriteBytesField(&out, 1, base.data(), base.size());
    WriteBytesField(&out, 2, appmsg.data(), appmsg.size());
    return out;
}

bool ReadVarintLocal(const uint8_t* data, size_t size, size_t* pos, uint64_t* value) {
    uint64_t result = 0;
    int shift = 0;
    while (*pos < size && shift <= 63) {
        uint8_t b = data[(*pos)++];
        result |= static_cast<uint64_t>(b & 0x7f) << shift;
        if ((b & 0x80) == 0) {
            *value = result;
            return true;
        }
        shift += 7;
    }
    return false;
}

void ParseA8KeyRespFields(const uint8_t* data, size_t size, std::string* full_url, std::string* a8key) {
    size_t pos = 0;
    while (pos < size) {
        uint64_t tag = 0;
        if (!ReadVarintLocal(data, size, &pos, &tag)) return;
        uint32_t field = static_cast<uint32_t>(tag >> 3);
        uint32_t wire = static_cast<uint32_t>(tag & 7);
        if (wire == 0) {
            uint64_t ignored = 0;
            if (!ReadVarintLocal(data, size, &pos, &ignored)) return;
        } else if (wire == 1) {
            if (pos + 8 > size) return;
            pos += 8;
        } else if (wire == 2) {
            uint64_t len = 0;
            if (!ReadVarintLocal(data, size, &pos, &len) || pos + len > size) return;
            const auto* p = data + pos;
            if (field == 2) {
                full_url->assign(reinterpret_cast<const char*>(p), static_cast<size_t>(len));
            } else if (field == 3) {
                a8key->assign(reinterpret_cast<const char*>(p), static_cast<size_t>(len));
            }
            pos += static_cast<size_t>(len);
        } else if (wire == 5) {
            if (pos + 4 > size) return;
            pos += 4;
        } else {
            return;
        }
    }
}

std::string HexPreview(const std::vector<uint8_t>& data, size_t max_bytes);

int ParseBaseResponseRet(const uint8_t* data, size_t size) {
    size_t pos = 0;
    while (pos < size) {
        uint64_t tag = 0;
        if (!ReadVarintLocal(data, size, &pos, &tag)) return 0;
        uint32_t field = static_cast<uint32_t>(tag >> 3);
        uint32_t wire = static_cast<uint32_t>(tag & 7);
        if (wire == 0) {
            uint64_t value = 0;
            if (!ReadVarintLocal(data, size, &pos, &value)) return 0;
            if (field == 1) return static_cast<int>(value);
        } else if (wire == 1) {
            if (pos + 8 > size) return 0;
            pos += 8;
        } else if (wire == 2) {
            uint64_t len = 0;
            if (!ReadVarintLocal(data, size, &pos, &len) || pos + len > size) return 0;
            pos += static_cast<size_t>(len);
        } else if (wire == 5) {
            if (pos + 4 > size) return 0;
            pos += 4;
        } else {
            return 0;
        }
    }
    return 0;
}

std::string BuildSendAppMsgResponseJson(const std::vector<uint8_t>& response) {
    int ret = 0;
    uint64_t msg_id = 0;
    uint64_t new_msg_id = 0;
    std::string client_msg_id;
    size_t pos = 0;
    while (pos < response.size()) {
        uint64_t tag = 0;
        if (!ReadVarintLocal(response.data(), response.size(), &pos, &tag)) break;
        uint32_t field = static_cast<uint32_t>(tag >> 3);
        uint32_t wire = static_cast<uint32_t>(tag & 7);
        if (wire == 0) {
            uint64_t value = 0;
            if (!ReadVarintLocal(response.data(), response.size(), &pos, &value)) break;
            if (field == 5) msg_id = value;
            else if (field == 9) new_msg_id = value;
        } else if (wire == 1) {
            if (pos + 8 > response.size()) break;
            pos += 8;
        } else if (wire == 2) {
            uint64_t len = 0;
            if (!ReadVarintLocal(response.data(), response.size(), &pos, &len) || pos + len > response.size()) break;
            const auto* p = response.data() + pos;
            if (field == 1) ret = ParseBaseResponseRet(p, static_cast<size_t>(len));
            else if (field == 6) client_msg_id.assign(reinterpret_cast<const char*>(p), static_cast<size_t>(len));
            pos += static_cast<size_t>(len);
        } else if (wire == 5) {
            if (pos + 4 > response.size()) break;
            pos += 4;
        } else {
            break;
        }
    }
    return "{"
        + JsonLite::NumberField("ret", ret) + ","
        + JsonLite::NumberField("msgId", static_cast<long long>(msg_id)) + ","
        + JsonLite::NumberField("newMsgId", static_cast<long long>(new_msg_id)) + ","
        + JsonLite::StringField("clientMsgId", client_msg_id) + ","
        + JsonLite::NumberField("rawSize", static_cast<long long>(response.size())) + ","
        + JsonLite::StringField("rawHexPreview", HexPreview(response, 128))
        + "}";
}

std::string HexPreview(const std::vector<uint8_t>& data, size_t max_bytes) {
    static const char kHex[] = "0123456789ABCDEF";
    size_t n = std::min(max_bytes, data.size());
    std::string out;
    out.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) {
        out.push_back(kHex[data[i] >> 4]);
        out.push_back(kHex[data[i] & 0x0F]);
    }
    return out;
}

std::string UrlDecode(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int hi = hex(value[i + 1]);
            int lo = hex(value[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(value[i] == '+' ? ' ' : value[i]);
    }
    return out;
}

std::string QueryParam(const std::string& url, const std::string& name) {
    size_t q = url.find('?');
    if (q == std::string::npos) return "";
    size_t pos = q + 1;
    while (pos < url.size()) {
        size_t next = url.find('&', pos);
        if (next == std::string::npos) next = url.size();
        size_t eq = url.find('=', pos);
        if (eq != std::string::npos && eq < next && url.compare(pos, eq - pos, name) == 0) {
            return UrlDecode(url.substr(eq + 1, next - eq - 1));
        }
        pos = next + 1;
    }
    return "";
}

std::string BuildA8KeyJson(const A8KeyRequest& req, const std::vector<uint8_t>& response) {
    std::string full_url;
    std::string a8key;
    if (!response.empty()) {
        ParseA8KeyRespFields(response.data(), response.size(), &full_url, &a8key);
    }
    return "{"
        + JsonLite::StringField("url", req.url) + ","
        + JsonLite::NumberField("urlType", req.url_type) + ","
        + JsonLite::NumberField("scene", req.scene) + ","
        + JsonLite::StringField("fullUrl", full_url) + ","
        + JsonLite::StringField("exportKey", QueryParam(full_url, "exportkey")) + ","
        + JsonLite::StringField("a8Key", a8key) + ","
        + JsonLite::NumberField("rawSize", static_cast<long long>(response.size())) + ","
        + JsonLite::StringField("rawHexPreview", HexPreview(response, 96)) + ","
        + JsonLite::StringField("source", "weixin_task_callback")
        + "}";
}

struct PendingA8Key {
    std::mutex mu;
    std::condition_variable cv;
    std::vector<uint8_t> request;
    std::vector<uint8_t> response;
    bool done = false;
};

std::mutex g_a8_mu;
std::unordered_map<int, PendingA8Key*> g_a8_pending;
void* g_task_manager = nullptr;
void* g_task_dispatch_trampoline = nullptr;
char g_task_dispatch_status[128] = "not installed";
std::atomic<long> g_a8_dispatch_count{0};
std::atomic<long> g_a8_prepare_count{0};
std::atomic<long> g_a8_callback_count{0};
std::atomic<long> g_a8_last_task_id{0};
std::atomic<long> g_a8_last_response_size{0};
std::atomic<long> g_a8_submit_count{0};
std::atomic<long> g_a8_last_submit_task_id{0};
std::atomic<long> g_a8_last_request_size{0};
std::atomic<long long> g_a8_last_dispatch_result{0};
std::atomic<long> g_a8_printpb_count{0};
std::atomic<long> g_a8_printpb_match_count{0};
std::atomic<long> g_a8_last_printpb_size{0};

using TaskDispatchFn = __int64(__fastcall*)(void* manager, void* task);
TaskDispatchFn g_task_dispatch_original = nullptr;

void SetA8Status(const char* status) {
    strcpy_s(g_task_dispatch_status, status);
}

bool WriteAbsoluteJumpLocal(uint8_t* at, size_t patch_size, void* to) {
    if (patch_size < 12 || patch_size > 32) return false;
    DWORD old_protect = 0;
    if (!::VirtualProtect(at, patch_size, PAGE_EXECUTE_READWRITE, &old_protect)) return false;
    uint8_t patch[32]{};
    memset(patch, 0x90, patch_size);
    patch[0] = 0x48;
    patch[1] = 0xB8;
    *reinterpret_cast<uint64_t*>(patch + 2) = reinterpret_cast<uint64_t>(to);
    patch[10] = 0xFF;
    patch[11] = 0xE0;
    memcpy(at, patch, patch_size);
    ::FlushInstructionCache(::GetCurrentProcess(), at, patch_size);
    ::VirtualProtect(at, patch_size, old_protect, &old_protect);
    return true;
}

bool BuildTrampolineLocal(uint8_t* target, size_t patch_size, void** trampoline) {
    uint8_t* mem = static_cast<uint8_t*>(::VirtualAlloc(nullptr, patch_size + 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!mem) return false;
    memcpy(mem, target, patch_size);
    uint8_t* tail = mem + patch_size;
    tail[0] = 0x48;
    tail[1] = 0xB8;
    *reinterpret_cast<uint64_t*>(tail + 2) = reinterpret_cast<uint64_t>(target + patch_size);
    tail[10] = 0xFF;
    tail[11] = 0xE0;
    *trampoline = mem;
    return true;
}

__int64 __fastcall TaskDispatchHook(void* manager, void* task) {
    if (manager && !g_task_manager) {
        g_task_manager = manager;
    }
    ++g_a8_dispatch_count;
    return g_task_dispatch_original ? g_task_dispatch_original(manager, task) : 0;
}

bool EnsureTaskDispatchHook(uintptr_t base) {
    if (g_task_dispatch_original) return true;
    auto* target = reinterpret_cast<uint8_t*>(base + kTaskDispatchRva);
    const uint8_t expected[] = {
        0x55, 0x41, 0x57, 0x41, 0x56, 0x41, 0x54, 0x56, 0x57, 0x53,
        0x48, 0x81, 0xEC, 0xA0, 0x03, 0x00, 0x00
    };
    if (memcmp(target, expected, sizeof(expected)) != 0) {
        SetA8Status("target prologue mismatch; skip hook");
        return false;
    }
    if (!BuildTrampolineLocal(target, kTaskDispatchPatchSize, &g_task_dispatch_trampoline)) {
        SetA8Status("failed to allocate trampoline");
        return false;
    }
    g_task_dispatch_original = reinterpret_cast<TaskDispatchFn>(g_task_dispatch_trampoline);
    if (!WriteAbsoluteJumpLocal(target, kTaskDispatchPatchSize, reinterpret_cast<void*>(&TaskDispatchHook))) {
        g_task_dispatch_original = nullptr;
        SetA8Status("failed to patch target");
        return false;
    }
    SetA8Status("installed");
    return true;
}

__int64 __fastcall A8TaskDestructor(void* self, __int64, __int64) {
    return reinterpret_cast<__int64>(self);
}

__int64 __fastcall A8SerializePrepare(void* self, void** out_holder, __int64, uint8_t* out_size) {
    int task_id = *reinterpret_cast<int*>(static_cast<uint8_t*>(self) + 8);
    ++g_a8_prepare_count;
    g_a8_last_task_id = task_id;
    PendingA8Key* pending = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_a8_mu);
        auto it = g_a8_pending.find(task_id);
        if (it != g_a8_pending.end()) pending = it->second;
    }
    if (!pending || !out_holder || !*out_holder) return 1;
    void* holder = *out_holder;
    void* buf = ::HeapAlloc(::GetProcessHeap(), HEAP_ZERO_MEMORY, pending->request.size() + 1);
    if (!buf) return 0;
    memcpy(buf, pending->request.data(), pending->request.size());
    *reinterpret_cast<void**>(holder) = buf;
    *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(holder) + 12) = static_cast<uint32_t>(pending->request.size());
    *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(holder) + 20) = static_cast<uint32_t>(pending->request.size());
    if (out_size) *out_size = static_cast<uint8_t>(pending->request.size() & 0xff);
    return 1;
}

bool TryReadResponseBlock(void** response_holder, const uint8_t** data, uint32_t* size) {
    __try {
        if (!response_holder || !*response_holder) return false;
        auto* block = reinterpret_cast<uint8_t*>(*response_holder);
        *data = *reinterpret_cast<const uint8_t**>(block);
        *size = *reinterpret_cast<uint32_t*>(block + 12);
        return *data && *size > 0 && *size < 8 * 1024 * 1024;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *data = nullptr;
        *size = 0;
        return false;
    }
}

__int64 __fastcall A8ResponseCallback(void* self, void** response_holder, __int64) {
    int task_id = *reinterpret_cast<int*>(static_cast<uint8_t*>(self) + 8);
    ++g_a8_callback_count;
    g_a8_last_task_id = task_id;
    std::vector<uint8_t> response;
    const uint8_t* data = nullptr;
    uint32_t size = 0;
    if (TryReadResponseBlock(response_holder, &data, &size)) {
        response.assign(data, data + size);
    }
    g_a8_last_response_size = static_cast<long>(response.size());

    PendingA8Key* pending = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_a8_mu);
        auto it = g_a8_pending.find(task_id);
        if (it != g_a8_pending.end()) pending = it->second;
    }
    if (pending) {
        {
            std::lock_guard<std::mutex> lock(pending->mu);
            pending->response.swap(response);
            pending->done = true;
        }
        pending->cv.notify_all();
    }
    return 1;
}

__int64 __fastcall A8GetInner(void* self) {
    return reinterpret_cast<__int64>(static_cast<uint8_t*>(self) + 232);
}

__int64 __fastcall A8GetWaiter(void* self) {
    return reinterpret_cast<__int64>(static_cast<uint8_t*>(self) + 344);
}

__int64 __fastcall A8Dummy1(void*, __int64 = 0, __int64 = 0, __int64 = 0) {
    return 1;
}

void* g_a8_vtable[] = {
    reinterpret_cast<void*>(&A8TaskDestructor),
    reinterpret_cast<void*>(&A8SerializePrepare),
    reinterpret_cast<void*>(&A8ResponseCallback),
    reinterpret_cast<void*>(&A8GetInner),
    reinterpret_cast<void*>(&A8Dummy1),
    reinterpret_cast<void*>(1),
};

void* g_a8_vtable2[] = {
    reinterpret_cast<void*>(&A8Dummy1),
    reinterpret_cast<void*>(&A8Dummy1),
    reinterpret_cast<void*>(&A8Dummy1),
    reinterpret_cast<void*>(&A8Dummy1),
};

bool SubmitPbRequest(uintptr_t base, const std::string& endpoint, int cgi_type,
                     const std::vector<uint8_t>& request, int wait_ms,
                     bool require_callback, std::vector<uint8_t>* response,
                     std::string* err) {
    EnsureTaskDispatchHook(base);
    if (!g_task_manager) {
        if (err) *err = "Weixin task manager has not been captured yet; trigger any normal network action once and retry";
        return false;
    }
    if (request.empty()) {
        if (err) *err = "protobuf request is empty";
        return false;
    }

    PendingA8Key pending;
    pending.request = request;
    g_a8_last_request_size = static_cast<long>(pending.request.size());

    auto* task_info = reinterpret_cast<uint64_t*>(HeapAllocZero(0x40));
    auto* task = reinterpret_cast<uint64_t*>(HeapAllocZero(0x1C8));
    auto* callback_holder = reinterpret_cast<uint64_t*>(HeapAllocZero(0x08));
    if (!task_info || !task || !callback_holder) {
        if (err) *err = "HeapAlloc failed while building protobuf task";
        return false;
    }

    MsvcString endpoint_str(endpoint);
    task_info[2] = 0x100000000ULL;
    task_info[6] = 0;
    task_info[7] = 15;
    *reinterpret_cast<uint8_t*>(task_info + 4) = 0;
    AssignRemoteString(task_info + 4, endpoint_str);
    *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(task_info) + 8) = static_cast<uint32_t>(cgi_type);
    *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(task_info) + 24) = static_cast<uint32_t>(cgi_type);

    using TaskCtorFn = void(__fastcall*)(void* task, void* task_info);
    auto ctor = reinterpret_cast<TaskCtorFn>(base + kTaskConstructorRva);
    ctor(task, task_info);

    task[0] = reinterpret_cast<uint64_t>(g_a8_vtable);
    callback_holder[0] = reinterpret_cast<uint64_t>(g_a8_vtable2);
    task[26] = reinterpret_cast<uint64_t>(callback_holder);

    int task_id = *reinterpret_cast<int*>(base + kTaskIdGlobalRva);
    *reinterpret_cast<int*>(reinterpret_cast<uint8_t*>(task) + 8) = task_id;
    *reinterpret_cast<int*>(reinterpret_cast<uint8_t*>(task) + 12) = static_cast<int>(cgi_type);
    AssignRemoteString(task + 3, endpoint_str);
    ++g_a8_submit_count;
    g_a8_last_submit_task_id = task_id;

    {
        std::lock_guard<std::mutex> lock(g_a8_mu);
        g_a8_pending[task_id] = &pending;
    }

    g_a8_last_dispatch_result = g_task_dispatch_original(g_task_manager, task);

    bool completed = true;
    if (wait_ms > 0) {
        std::unique_lock<std::mutex> lock(pending.mu);
        completed = pending.cv.wait_for(lock, std::chrono::milliseconds(wait_ms), [&pending]() {
            return pending.done;
        });
    }

    {
        std::lock_guard<std::mutex> lock(g_a8_mu);
        g_a8_pending.erase(task_id);
    }

    if (response) {
        std::lock_guard<std::mutex> lock(pending.mu);
        *response = pending.response;
    }
    if (require_callback && !completed) {
        if (err) *err = "protobuf task timed out waiting for Weixin response callback";
        return false;
    }
    return true;
}

bool BuildSceneContext(uintptr_t base, void* ctx) {
    auto make_node = [base](uintptr_t vtable_rva) -> void* {
        auto* node = reinterpret_cast<uint64_t*>(HeapAllocZero(0x40));
        if (!node) return nullptr;
        node[0] = base + vtable_rva;
        node[7] = reinterpret_cast<uint64_t>(node);
        return node;
    };

    void* reserved = HeapAllocZero(0x10);
    void* n1 = make_node(kSceneVtable1);
    void* n2 = make_node(kSceneVtable2);
    void* n3 = make_node(kSceneVtable3);
    if (!reserved || !n1 || !n2 || !n3) return false;

    uint64_t scene_global = *reinterpret_cast<uint64_t*>(base + kSceneGlobalPtr);
    auto setup = reinterpret_cast<int64_t(__fastcall*)(void*, void*, void*, void*, void*, uint64_t)>(base + kInitSceneContext);
    setup(ctx, n1, n2, n3, reserved, scene_global);
    return true;
}

bool CallWeixinSend2(uintptr_t fn, void* arg1, void* arg2) {
    using SendFn = int64_t(__fastcall*)(void*, void*);
    auto send = reinterpret_cast<SendFn>(fn);
    __try {
        send(arg1, arg2);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool CallWeixinInit1(uintptr_t fn, void* arg1) {
    using InitFn = void(__fastcall*)(void*);
    auto init = reinterpret_cast<InitFn>(fn);
    __try {
        init(arg1);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool TryCopyMemoryRaw(void* dst, const void* src, size_t size) {
    __try {
        memcpy(dst, src, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::string ReadMemoryHexPreview(uintptr_t address, size_t size) {
    uint8_t bytes[64]{};
    size = std::min<size_t>(size, sizeof(bytes));
    if (!TryCopyMemoryRaw(bytes, reinterpret_cast<const void*>(address), size)) return "";
    static const char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(size * 2);
    for (size_t i = 0; i < size; ++i) {
        out.push_back(kHex[bytes[i] >> 4]);
        out.push_back(kHex[bytes[i] & 0x0F]);
    }
    return out;
}

bool IsReadableMemory(DWORD protect) {
    if (protect & PAGE_GUARD) return false;
    if (protect & PAGE_NOACCESS) return false;

    DWORD p = protect & 0xff;
    return p == PAGE_READONLY
        || p == PAGE_READWRITE
        || p == PAGE_WRITECOPY
        || p == PAGE_EXECUTE_READ
        || p == PAGE_EXECUTE_READWRITE
        || p == PAGE_EXECUTE_WRITECOPY;
}

bool IsWxidChar(unsigned char c) {
    return std::isalnum(c) || c == '_' || c == '-' || c == '@' || c == '.';
}

bool LooksLikeWxid(const std::string& value) {
    if (value.size() < 6 || value.size() > 80) return false;
    if (value.find("wxid_") != 0) return false;
    for (unsigned char c : value) {
        if (!IsWxidChar(c)) return false;
    }
    return true;
}

void AddCandidate(std::map<std::string, int>* candidates, const std::string& value, int weight) {
    if (LooksLikeWxid(value)) {
        (*candidates)[value] += weight;
    }
}

void ScanAsciiWxids(const uint8_t* data, size_t size, std::map<std::string, int>* candidates) {
    const char needle[] = "wxid_";
    constexpr size_t needle_len = sizeof(needle) - 1;
    if (size < needle_len) return;

    for (size_t i = 0; i + needle_len <= size; ++i) {
        if (memcmp(data + i, needle, needle_len) != 0) continue;

        size_t end = i;
        while (end < size && end - i < 80 && IsWxidChar(data[end])) {
            ++end;
        }
        AddCandidate(candidates, std::string(reinterpret_cast<const char*>(data + i), end - i), 1);
    }
}

void ScanUtf16Wxids(const uint8_t* data, size_t size, std::map<std::string, int>* candidates) {
    const wchar_t needle[] = L"wxid_";
    constexpr size_t needle_chars = 5;
    if (size < needle_chars * sizeof(wchar_t)) return;

    for (size_t i = 0; i + needle_chars * sizeof(wchar_t) <= size; i += 2) {
        const auto* w = reinterpret_cast<const wchar_t*>(data + i);
        if (w[0] != needle[0] || w[1] != needle[1] || w[2] != needle[2] || w[3] != needle[3] || w[4] != needle[4]) {
            continue;
        }

        std::string value;
        for (size_t n = 0; i + (n + 1) * sizeof(wchar_t) <= size && n < 80; ++n) {
            wchar_t ch = w[n];
            if (ch > 0x7f || !IsWxidChar(static_cast<unsigned char>(ch))) break;
            value.push_back(static_cast<char>(ch));
        }
        AddCandidate(candidates, value, 1);
    }
}

std::string EnvString(const char* name) {
    char buf[MAX_PATH * 4]{};
    DWORD n = ::GetEnvironmentVariableA(name, buf, static_cast<DWORD>(sizeof(buf)));
    if (!n || n >= sizeof(buf)) return "";
    return std::string(buf, n);
}

void ScanWeChatFilesDirs(std::map<std::string, int>* candidates) {
    std::vector<std::string> roots;

    std::string user_profile = EnvString("USERPROFILE");
    if (!user_profile.empty()) {
        roots.push_back(user_profile + "\\Documents\\WeChat Files");
    }

    std::string documents = EnvString("USERPROFILE");
    if (!documents.empty()) {
        roots.push_back(documents + "\\OneDrive\\Documents\\WeChat Files");
    }

    for (const std::string& root : roots) {
        WIN32_FIND_DATAA fd{};
        HANDLE h = ::FindFirstFileA((root + "\\wxid_*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;

        do {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && strcmp(fd.cFileName, ".") != 0 && strcmp(fd.cFileName, "..") != 0) {
                AddCandidate(candidates, fd.cFileName, 20);
            }
        } while (::FindNextFileA(h, &fd));
        ::FindClose(h);
    }
}

void ScanCurrentProcessWxids(std::map<std::string, int>* candidates) {
    SYSTEM_INFO si{};
    ::GetSystemInfo(&si);

    uintptr_t cur = reinterpret_cast<uintptr_t>(si.lpMinimumApplicationAddress);
    uintptr_t max_addr = reinterpret_cast<uintptr_t>(si.lpMaximumApplicationAddress);
    std::vector<uint8_t> buffer(1024 * 1024 + 256);
    size_t scanned = 0;
    constexpr size_t kMaxScanBytes = 512ull * 1024ull * 1024ull;

    while (cur < max_addr && scanned < kMaxScanBytes) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (::VirtualQuery(reinterpret_cast<LPCVOID>(cur), &mbi, sizeof(mbi)) != sizeof(mbi)) {
            cur += 0x10000;
            continue;
        }

        uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        uintptr_t next = base + mbi.RegionSize;
        if (mbi.State == MEM_COMMIT && IsReadableMemory(mbi.Protect)) {
            for (uintptr_t off = base; off < next;) {
                size_t to_read = static_cast<size_t>(std::min<uintptr_t>(buffer.size(), next - off));
                SIZE_T bytes_read = 0;
                if (::ReadProcessMemory(::GetCurrentProcess(), reinterpret_cast<LPCVOID>(off), buffer.data(), to_read, &bytes_read) && bytes_read > 0) {
                    ScanAsciiWxids(buffer.data(), bytes_read, candidates);
                    ScanUtf16Wxids(buffer.data(), bytes_read, candidates);
                    scanned += bytes_read;
                    if (scanned >= kMaxScanBytes) break;
                }
                off += std::max<size_t>(to_read, 1);
            }
        }

        if (next <= cur) break;
        cur = next;
    }
}

std::string SelectBestWxid(const std::map<std::string, int>& candidates) {
    std::string best;
    int best_score = 0;
    for (const auto& kv : candidates) {
        if (kv.second > best_score) {
            best = kv.first;
            best_score = kv.second;
        }
    }
    return best;
}

std::string BuildProfileCacheJson(const std::string& wxid, const std::string& nickname, const std::string& source) {
    return "{"
        "\"baseResponse\":{\"ret\":0,\"errMsg\":{}},"
        "\"userInfo\":{"
            "\"bitFlag\":190,"
            "\"userName\":{\"String\":\"" + JsonLite::Escape(wxid) + "\"},"
            "\"nickName\":{\"String\":\"" + JsonLite::Escape(nickname) + "\"},"
            "\"bindUin\":0,"
            "\"bindEmail\":{},"
            "\"bindMobile\":{\"String\":\"\"},"
            "\"status\":234021,"
            "\"imgLen\":0,"
            "\"sex\":0,"
            "\"province\":\"\","
            "\"city\":\"\","
            "\"signature\":\"\","
            "\"personalCard\":0,"
            "\"disturbSetting\":{"
                "\"nightSetting\":0,"
                "\"nightTime\":{\"beginTime\":0,\"endTime\":0},"
                "\"allDaySetting\":0,"
                "\"allDayTime\":{\"beginTime\":0,\"endTime\":0}"
            "},"
            "\"pluginFlag\":0,"
            "\"verifyFlag\":0,"
            "\"point\":0,"
            "\"experience\":0,"
            "\"level\":0,"
            "\"levelLowExp\":0,"
            "\"levelHighExp\":0,"
            "\"pluginSwitch\":0,"
            "\"gmailList\":{\"count\":0},"
            "\"alias\":\"\","
            "\"weiboFlag\":0,"
            "\"faceBookFlag\":0,"
            "\"fbuserId\":\"0\","
            "\"albumStyle\":0,"
            "\"albumFlag\":0,"
            "\"txnewsCategory\":0,"
            "\"country\":\"\""
        "},"
        "\"userInfoExt\":{"
            "\"snsUserInfo\":{"
                "\"snsFlag\":0,"
                "\"snsBgimgId\":\"\","
                "\"snsBgobjectId\":\"0\","
                "\"snsFlagEx\":0,"
                "\"snsPrivacyRecent\":0"
            "},"
            "\"myBrandList\":\"\","
            "\"bigChatRoomSize\":0,"
            "\"bigChatRoomQuota\":0,"
            "\"bigChatRoomInvite\":0,"
            "\"bigHeadImgUrl\":\"\","
            "\"smallHeadImgUrl\":\"\","
            "\"mainAcctType\":0,"
            "\"extXml\":{},"
            "\"safeDeviceList\":{\"count\":0,\"list\":[]},"
            "\"safeDevice\":0,"
            "\"grayscaleFlag\":0,"
            "\"regCountry\":\"\","
            "\"linkedinContactItem\":{},"
            "\"patternLockInfo\":{\"patternVersion\":0,\"sign\":{\"iLen\":0,\"buffer\":\"\"},\"lockStatus\":0},"
            "\"payWalletType\":0,"
            "\"walletRegion\":0,"
            "\"extStatus\":\"0\","
            "\"userStatus\":1,"
            "\"paySetting\":\"\","
            "\"patSuffix\":\"\","
            "\"patSuffixVersion\":0,"
            "\"teenagerModeFinderSetting\":0,"
            "\"teenagerModeBizAcctSetting\":0,"
            "\"teenagerModeMiniProgramSetting\":0,"
            "\"xagreementInfo\":{\"funcsSwitch\":\"0\",\"funcsUserChoiceSwitch\":\"0\"},"
            "\"salt\":\"\","
            "\"finderSetting\":\"0\","
            "\"ringBackSetting\":{\"finderObjectId\":\"0\",\"startTs\":0,\"endTs\":0},"
            "\"smcryptoFlag\":0,"
            "\"globalRingBackSetting\":{"
                "\"type\":0,"
                "\"startTime\":0,"
                "\"endTime\":0,"
                "\"music\":{\"sid\":0},"
                "\"finder\":{\"finderObjectId\":\"0\"}"
            "},"
            "\"newcomeMsgDefaultVoiceNumber\":0,"
            "\"discoveryPageCtrlFlag\":\"0\","
            "\"extStatus2\":\"0\","
            "\"finderLiveAliasSync\":{\"updateTime\":\"0\",\"spamFlag\":0,\"deleteTime\":\"0\"},"
            "\"liveAliasRoleType\":0,"
            "\"verifyContentList\":{\"count\":0},"
            "\"lqtversion\":0,"
            "\"teenagerModeEmotionSetting\":0,"
            "\"notificationBannerDisplayContentSetting\":0"
        "},"
        "\"source\":\"" + JsonLite::Escape(source) + "\""
        "}";
}
}

bool WeChatBridge::Initialize() {
    uintptr_t base = WeixinBase();
    if (base) {
        EnsureTaskDispatchHook(base);
    }
    return base != 0;
}

bool WeChatBridge::IsLogin() const {
    std::lock_guard<std::mutex> lock(mu_);
    return !account_wxid_.empty() && !profile_cache_json_.empty();
}

uintptr_t WeChatBridge::WeixinBase() const {
    return reinterpret_cast<uintptr_t>(::GetModuleHandleA(kWeixinModuleName));
}

std::string WeChatBridge::GetProfileCacheJson() const {
    std::lock_guard<std::mutex> lock(mu_);
    if (profile_cache_json_.empty()) {
        RefreshProfileCacheLocked();
    }
    if (profile_cache_json_.empty()) {
        return "{\"type\":1711,\"data\":\"No Login!\",\"msg\":\"success\"}";
    }
    return profile_cache_json_;
}

std::string WeChatBridge::DebugStatusJson() const {
    std::lock_guard<std::mutex> lock(mu_);
    if (profile_cache_json_.empty()) {
        RefreshProfileCacheLocked();
    }
    uintptr_t base = WeixinBase();
    return "{"
        + JsonLite::NumberField("pid", static_cast<long long>(::GetCurrentProcessId())) + ","
        + JsonLite::StringField("weixin_module", base ? "loaded" : "not_loaded") + ","
        + JsonLite::StringField("weixin_base_hex", HexPtr(base)) + ","
        + JsonLite::StringField("account_wxid", account_wxid_) + ","
        + JsonLite::NumberField("profile_cache_size", static_cast<long long>(profile_cache_json_.size())) + ","
        + "\"a8key_task\":{"
            + JsonLite::StringField("dispatch_hook", g_task_dispatch_status) + ","
            + JsonLite::StringField("manager_hex", HexPtr(reinterpret_cast<uintptr_t>(g_task_manager))) + ","
            + JsonLite::NumberField("dispatch_count", g_a8_dispatch_count.load()) + ","
            + JsonLite::NumberField("submit_count", g_a8_submit_count.load()) + ","
            + JsonLite::NumberField("prepare_count", g_a8_prepare_count.load()) + ","
            + JsonLite::NumberField("callback_count", g_a8_callback_count.load()) + ","
            + JsonLite::NumberField("last_task_id", g_a8_last_task_id.load()) + ","
            + JsonLite::NumberField("last_submit_task_id", g_a8_last_submit_task_id.load()) + ","
            + JsonLite::NumberField("last_request_size", g_a8_last_request_size.load()) + ","
            + JsonLite::NumberField("last_dispatch_result", g_a8_last_dispatch_result.load()) + ","
            + JsonLite::NumberField("last_response_size", g_a8_last_response_size.load()) + ","
            + JsonLite::NumberField("printpb_count", g_a8_printpb_count.load()) + ","
            + JsonLite::NumberField("printpb_match_count", g_a8_printpb_match_count.load()) + ","
            + JsonLite::NumberField("last_printpb_size", g_a8_last_printpb_size.load())
        + "},"
        + JsonLite::StringField("note", profile_cache_json_.empty()
            ? "profile cache is empty; current account wxid was not found"
            : "profile cache has been populated")
        + "}";
}

void WeChatBridge::UpdateProfileCacheJson(const std::string& json, const std::string& account_wxid) {
    std::lock_guard<std::mutex> lock(mu_);
    profile_cache_json_ = json;
    account_wxid_ = account_wxid;
}

void WeChatBridge::UpdateLoginInitProfile(const std::string& wxid, const std::string& nickname, const std::string& source) {
    if (wxid.empty()) return;

    std::lock_guard<std::mutex> lock(mu_);
    account_wxid_ = wxid;
    profile_cache_json_ = BuildProfileCacheJson(wxid, nickname, source);
}

void WeChatBridge::CaptureA8KeyResponseBytes(const void* data, size_t size, const char* source) {
    if (!data || size == 0 || size > 8 * 1024 * 1024) return;
    ++g_a8_printpb_count;
    g_a8_last_printpb_size = static_cast<long>(size);

    std::string full_url;
    std::string a8key;
    ParseA8KeyRespFields(static_cast<const uint8_t*>(data), size, &full_url, &a8key);
    if (a8key.empty() && full_url.find("http") != 0) return;

    PendingA8Key* pending = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_a8_mu);
        long task_id = g_a8_last_submit_task_id.load();
        auto it = g_a8_pending.find(static_cast<int>(task_id));
        if (it != g_a8_pending.end()) {
            pending = it->second;
        } else if (!g_a8_pending.empty()) {
            pending = g_a8_pending.begin()->second;
        }
    }
    if (!pending) return;

    ++g_a8_printpb_match_count;
    {
        std::lock_guard<std::mutex> lock(pending->mu);
        const auto* p = static_cast<const uint8_t*>(data);
        pending->response.assign(p, p + size);
        pending->done = true;
    }
    g_a8_last_response_size = static_cast<long>(size);
    pending->cv.notify_all();
    (void)source;
}

bool WeChatBridge::RefreshProfileCacheLocked() const {
    if (!profile_cache_json_.empty()) return true;

    std::map<std::string, int> candidates;
    ScanWeChatFilesDirs(&candidates);
    if (candidates.size() != 1) {
        ScanCurrentProcessWxids(&candidates);
    }

    std::string wxid = SelectBestWxid(candidates);
    if (wxid.empty()) return false;

    account_wxid_ = wxid;
    profile_cache_json_ = BuildProfileCacheJson(wxid, "", "wechat_files_or_process_memory");
    return true;
}

bool WeChatBridge::GetA8Key(const A8KeyRequest& req, std::string* out_json, std::string* err) {
    if (!NonEmpty(req.url)) {
        if (err) *err = "url is required";
        return false;
    }

    uintptr_t base = WeixinBase();
    if (!base) {
        if (err) *err = "Weixin.dll not loaded in current process";
        return false;
    }
    EnsureTaskDispatchHook(base);
    if (!g_task_manager) {
        if (err) *err = "Weixin task manager has not been captured yet; trigger any normal network action once and retry";
        return false;
    }

    PendingA8Key pending;
    pending.request = BuildGetA8KeyReq(req);
    g_a8_last_request_size = static_cast<long>(pending.request.size());
    if (pending.request.empty()) {
        if (err) *err = "failed to build GetA8KeyReq protobuf";
        return false;
    }

    auto* task_info = reinterpret_cast<uint64_t*>(HeapAllocZero(0x40));
    auto* task = reinterpret_cast<uint64_t*>(HeapAllocZero(0x1C8));
    auto* callback_holder = reinterpret_cast<uint64_t*>(HeapAllocZero(0x08));
    if (!task_info || !task || !callback_holder) {
        if (err) *err = "HeapAlloc failed while building get_a8key task";
        return false;
    }

    MsvcString endpoint("/cgi-bin/micromsg-bin/mp-geta8key");
    task_info[2] = 0x100000000ULL;
    task_info[6] = 0;
    task_info[7] = 15;
    *reinterpret_cast<uint8_t*>(task_info + 4) = 0;
    AssignRemoteString(task_info + 4, endpoint);
    *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(task_info) + 8) = kMpGetA8KeyCgiType;
    *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(task_info) + 24) = kMpGetA8KeyCgiType;

    using TaskCtorFn = void(__fastcall*)(void* task, void* task_info);
    auto ctor = reinterpret_cast<TaskCtorFn>(base + kTaskConstructorRva);
    ctor(task, task_info);

    task[0] = reinterpret_cast<uint64_t>(g_a8_vtable);
    callback_holder[0] = reinterpret_cast<uint64_t>(g_a8_vtable2);
    task[26] = reinterpret_cast<uint64_t>(callback_holder);

    int task_id = *reinterpret_cast<int*>(base + kTaskIdGlobalRva);
    *reinterpret_cast<int*>(reinterpret_cast<uint8_t*>(task) + 8) = task_id;
    *reinterpret_cast<int*>(reinterpret_cast<uint8_t*>(task) + 12) = kMpGetA8KeyCgiType;
    AssignRemoteString(task + 3, endpoint);
    ++g_a8_submit_count;
    g_a8_last_submit_task_id = task_id;

    {
        std::lock_guard<std::mutex> lock(g_a8_mu);
        g_a8_pending[task_id] = &pending;
    }

    g_a8_last_dispatch_result = g_task_dispatch_original(g_task_manager, task);

    bool completed = false;
    {
        std::unique_lock<std::mutex> lock(pending.mu);
        completed = pending.cv.wait_for(lock, std::chrono::milliseconds(8000), [&pending]() {
            return pending.done;
        });
    }

    {
        std::lock_guard<std::mutex> lock(g_a8_mu);
        g_a8_pending.erase(task_id);
    }

    if (!completed) {
        if (err) *err = "get_a8key timed out waiting for Weixin response callback";
        return false;
    }

    if (out_json) {
        *out_json = BuildA8KeyJson(req, pending.response);
    }
    return true;
}

bool WeChatBridge::SendTextMsg(const std::string& wxid, const std::string& msg, std::string* err) {
    if (!NonEmpty(wxid) || !NonEmpty(msg)) {
        if (err) *err = "wxid and msg are required";
        return false;
    }

    uintptr_t base = WeixinBase();
    if (!base) {
        if (err) *err = "Weixin.dll not loaded in current process";
        return false;
    }

    using InitTextMessageFn = void(__fastcall*)(void*);
    auto init_text_message = reinterpret_cast<InitTextMessageFn>(base + kInitTextMessage);

    auto* message_block = reinterpret_cast<uint64_t*>(HeapAllocZero(0x10));
    if (!message_block) {
        if (err) *err = "HeapAlloc failed while creating Weixin text message handle";
        return false;
    }

    init_text_message(message_block);
    void* message_handle = reinterpret_cast<void*>(message_block[0]);
    if (!message_handle) {
        if (err) *err = "Weixin text message constructor returned null";
        return false;
    }

    auto* obj = reinterpret_cast<uint8_t*>(message_handle);
    *reinterpret_cast<uint64_t*>(obj - 8) = 0x200000004ULL;

    AssignFreshMsvcString(obj + 22 * sizeof(uint64_t), wxid);
    AssignFreshMsvcString(obj + 204 * sizeof(uint64_t), msg);
    *reinterpret_cast<uint64_t*>(obj + 49 * sizeof(uint64_t)) = msg.size();
    *reinterpret_cast<uint64_t*>(obj + 27 * sizeof(uint64_t)) = 1;
    AssignFreshMsvcString(obj + 208 * sizeof(uint64_t), std::string());

    auto* holder = reinterpret_cast<uint64_t*>(HeapAllocZero(0x10));
    auto* send_task = reinterpret_cast<uint64_t*>(HeapAllocZero(0x20));
    void* scene_ctx = HeapAllocZero(0xE8);
    if (!holder || !send_task || !scene_ctx) {
        if (err) *err = "HeapAlloc failed while building send_text_msg structures";
        return false;
    }

    holder[0] = reinterpret_cast<uint64_t>(message_handle);
    holder[1] = reinterpret_cast<uint64_t>(obj - 16);

    send_task[0] = base + kSendTextObjSeed;
    send_task[1] = reinterpret_cast<uint64_t>(holder);
    send_task[2] = reinterpret_cast<uint64_t>(holder + 2);
    send_task[3] = reinterpret_cast<uint64_t>(holder + 2);

    if (!BuildSceneContext(base, scene_ctx)) {
        if (err) *err = "failed to build Weixin scene context";
        return false;
    }

    using SendFn = int64_t(__fastcall*)(void*, void*);
    auto send = reinterpret_cast<SendFn>(base + kSendTextCall);
    send(send_task, scene_ctx);
    return true;
}

bool WeChatBridge::SendXml(const XmlRequest& req, std::string* out_json, std::string* err) {
    if (!NonEmpty(req.wxid) || !NonEmpty(req.url)) {
        if (err) *err = "wxid and url are required";
        return false;
    }

    uintptr_t base = WeixinBase();
    if (!base) {
        if (err) *err = "Weixin.dll not loaded in current process";
        return false;
    }

    std::string from_wxid;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (account_wxid_.empty()) {
            RefreshProfileCacheLocked();
        }
        from_wxid = account_wxid_;
    }
    if (from_wxid.empty()) {
        if (err) *err = "current account wxid is empty; call /api/get_profile_cache after login first";
        return false;
    }

    std::vector<uint8_t> request = BuildSendAppMsgReq(from_wxid, req);
    std::vector<uint8_t> response;
    if (!SubmitPbRequest(base, "/cgi-bin/micromsg-bin/sendappmsg", kSendAppMsgCgiType,
                         request, 8000, true, &response, err)) {
        return false;
    }
    if (out_json) *out_json = BuildSendAppMsgResponseJson(response);
    return true;
}

bool WeChatBridge::SendFileMsg(const std::string& wxid, const std::string& filepath, std::string* err) {
    std::string ignored;
    return SendFileMsgDebug(wxid, filepath, "send", &ignored, err);
}

bool WeChatBridge::SendFileMsgDebug(const std::string& wxid, const std::string& filepath,
                                    const std::string& stage, std::string* out_json, std::string* err) {
    if (!NonEmpty(wxid) || !NonEmpty(filepath)) {
        if (err) *err = "wxid and filepath are required";
        return false;
    }

    std::wstring wide_path = Utf8ToWide(filepath);
    DWORD attr = ::GetFileAttributesW(wide_path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        if (err) *err = "filepath does not exist";
        return false;
    }
    uint64_t file_size = FileSizeOfPath(wide_path);
    if (file_size == 0) {
        if (err) *err = "file size is 0";
        return false;
    }

    uintptr_t base = WeixinBase();
    if (!base) {
        if (err) *err = "Weixin.dll not loaded in current process";
        return false;
    }

    uintptr_t init_target = base + kInitFileMessage;
    auto* message_block = reinterpret_cast<uint64_t*>(HeapAllocZero(0x10));
    if (!message_block) {
        if (err) *err = "HeapAlloc failed while creating Weixin file message handle";
        return false;
    }

    if (!CallWeixinInit1(init_target, message_block)) {
        if (out_json) {
            *out_json = "{"
                + JsonLite::StringField("stage", "construct") + ","
                + JsonLite::StringField("initFileTarget", HexPtr(init_target)) + ","
                + JsonLite::StringField("initFilePrologue", ReadMemoryHexPreview(init_target, 32)) + ","
                + JsonLite::StringField("error", "init_file_message raised an exception")
                + "}";
        }
        if (err) *err = "Weixin file message constructor raised an exception";
        return false;
    }
    void* message_handle = reinterpret_cast<void*>(message_block[0]);
    if (!message_handle) {
        if (err) *err = "Weixin file message constructor returned null";
        return false;
    }
    if (stage == "construct") {
        if (out_json) {
            *out_json = "{"
                + JsonLite::StringField("stage", "construct") + ","
                + JsonLite::StringField("initFileTarget", HexPtr(init_target)) + ","
                + JsonLite::StringField("initFilePrologue", ReadMemoryHexPreview(init_target, 32)) + ","
                + JsonLite::StringField("messageHandle", HexPtr(reinterpret_cast<uintptr_t>(message_handle))) + ","
                + JsonLite::NumberField("fileSize", static_cast<long long>(file_size))
                + "}";
        }
        return true;
    }

    auto* obj = reinterpret_cast<uint8_t*>(message_handle);
    *reinterpret_cast<uint64_t*>(obj - 8) = 0x200000006ULL;

    std::string narrow_path = WideToWeChatMb(wide_path);
    std::string file_name = WideToWeChatMb(BaseNameOfPath(wide_path));
    AssignFreshMsvcString(obj + 176, wxid);
    AssignFreshMsvcWString(obj + 224, wide_path);
    AssignFreshMsvcString(obj + 264, narrow_path);
    *reinterpret_cast<uint64_t*>(obj + 208) = 0x100000001ULL;
    *reinterpret_cast<uint64_t*>(obj + 256) = 1;
    *reinterpret_cast<uint64_t*>(obj + 216) = 0x600000031ULL;
    AssignFreshMsvcString(obj + 360, file_name);
    auto* uuid_src = reinterpret_cast<MsvcString*>(obj + 1536);
    AssignFreshMsvcString(obj + 1568, std::string(uuid_src->data_ptr(), uuid_src->size));
    *reinterpret_cast<uint64_t*>(obj + 392) = file_size;
    if (stage == "fill") {
        if (out_json) {
            *out_json = "{"
                + JsonLite::StringField("stage", "fill") + ","
                + JsonLite::StringField("messageHandle", HexPtr(reinterpret_cast<uintptr_t>(message_handle))) + ","
                + JsonLite::NumberField("fileSize", static_cast<long long>(file_size)) + ","
                + JsonLite::StringField("fileName", WideToUtf8(BaseNameOfPath(wide_path))) + ","
                + JsonLite::NumberField("uuidSize", static_cast<long long>(uuid_src->size))
                + "}";
        }
        return true;
    }

    auto* holder = reinterpret_cast<uint64_t*>(HeapAllocZero(0x10));
    auto* send_task = reinterpret_cast<uint64_t*>(HeapAllocZero(0x20));
    void* scene_ctx = HeapAllocZero(0xE8);
    if (!holder || !send_task || !scene_ctx) {
        if (err) *err = "HeapAlloc failed while building send_file_msg structures";
        return false;
    }

    holder[0] = reinterpret_cast<uint64_t>(message_handle);
    holder[1] = reinterpret_cast<uint64_t>(obj - 16);

    send_task[0] = base + kSendFileObjSeed;
    send_task[1] = reinterpret_cast<uint64_t>(holder);
    send_task[2] = reinterpret_cast<uint64_t>(holder + 2);
    send_task[3] = reinterpret_cast<uint64_t>(holder + 2);

    if (!BuildSceneContext(base, scene_ctx)) {
        if (err) *err = "failed to build Weixin scene context";
        return false;
    }
    if (stage == "scene") {
        if (out_json) {
            *out_json = "{"
                + JsonLite::StringField("stage", "scene") + ","
                + JsonLite::StringField("messageHandle", HexPtr(reinterpret_cast<uintptr_t>(message_handle))) + ","
                + JsonLite::StringField("sendTask", HexPtr(reinterpret_cast<uintptr_t>(send_task))) + ","
                + JsonLite::StringField("sceneCtx", HexPtr(reinterpret_cast<uintptr_t>(scene_ctx))) + ","
                + JsonLite::NumberField("fileSize", static_cast<long long>(file_size))
                + "}";
        }
        return true;
    }
    if (!stage.empty() && stage != "send") {
        if (err) *err = "stage must be construct, fill, scene, or send";
        return false;
    }

    if (!CallWeixinSend2(base + kSendTextCall, send_task, scene_ctx)) {
        if (err) *err = "Weixin send_file_msg internal call raised an exception";
        return false;
    }
    if (out_json) {
        *out_json = "{"
            + JsonLite::StringField("stage", "send") + ","
            + JsonLite::StringField("messageHandle", HexPtr(reinterpret_cast<uintptr_t>(message_handle))) + ","
            + JsonLite::NumberField("fileSize", static_cast<long long>(file_size))
            + "}";
    }
    return true;
}
