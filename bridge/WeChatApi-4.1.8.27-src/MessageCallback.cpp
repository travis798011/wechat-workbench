#include "MessageCallback.h"

#include "JsonLite.h"

#include <Windows.h>
#include <winhttp.h>

#include <cstring>
#include <new>
#include <sstream>

namespace {
struct AsyncCallbackJob {
    const MessageCallback* callback = nullptr;
    WeChatMessage msg;
};

DWORD WINAPI CallbackThreadProc(void* arg) {
    AsyncCallbackJob* job = static_cast<AsyncCallbackJob*>(arg);
    if (job && job->callback) {
        std::string ignored;
        job->callback->PublishIncomingMessage(job->msg, &ignored);
    }
    delete job;
    return 0;
}
}

void MessageCallback::SetUrl(const std::string& url) {
    std::lock_guard<std::mutex> lock(mu_);
    callback_url_ = url;
}

std::string MessageCallback::GetUrl() const {
    std::lock_guard<std::mutex> lock(mu_);
    return callback_url_;
}

bool MessageCallback::PublishIncomingMessage(const WeChatMessage& msg, std::string* err) const {
    std::string url = GetUrl();
    if (url.empty()) return true;

    std::ostringstream body;
    body << "{"
         << JsonLite::NumberField("event_type", 1001) << ","
         << JsonLite::NumberField("msgid", msg.msgid) << ","
         << JsonLite::NumberField("type", msg.type) << ","
         << JsonLite::NumberField("timestamp", msg.timestamp) << ","
         << JsonLite::StringField("wxid", msg.wxid) << ","
         << JsonLite::StringField("sender", msg.sender) << ","
         << JsonLite::StringField("roomid", msg.roomid) << ","
         << JsonLite::StringField("content", msg.content)
         << "}";
    return PostJson(url, body.str(), err);
}

bool MessageCallback::PublishIncomingMessageAsync(const WeChatMessage& msg) const {
    if (GetUrl().empty()) return true;
    AsyncCallbackJob* job = new (std::nothrow) AsyncCallbackJob();
    if (!job) return false;
    job->callback = this;
    job->msg = msg;
    HANDLE thread = ::CreateThread(nullptr, 0, CallbackThreadProc, job, 0, nullptr);
    if (!thread) {
        delete job;
        return false;
    }
    ::CloseHandle(thread);
    return true;
}

bool MessageCallback::PostJson(const std::string& url, const std::string& body, std::string* err) const {
    std::wstring wurl(url.begin(), url.end());
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    wchar_t host[256]{};
    wchar_t path[2048]{};
    parts.lpszHostName = host;
    parts.dwHostNameLength = _countof(host);
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = _countof(path);

    if (!::WinHttpCrackUrl(wurl.c_str(), 0, 0, &parts)) {
        if (err) *err = "invalid callback url";
        return false;
    }

    HINTERNET session = ::WinHttpOpen(L"WeChatApiVs2019/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        if (err) *err = "WinHttpOpen failed";
        return false;
    }

    std::wstring whost(host, parts.dwHostNameLength);
    HINTERNET connect = ::WinHttpConnect(session, whost.c_str(), parts.nPort, 0);
    if (!connect) {
        ::WinHttpCloseHandle(session);
        if (err) *err = "WinHttpConnect failed";
        return false;
    }

    std::wstring wpath(path, parts.dwUrlPathLength);
    DWORD flags = parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = ::WinHttpOpenRequest(connect, L"POST", wpath.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!request) {
        ::WinHttpCloseHandle(connect);
        ::WinHttpCloseHandle(session);
        if (err) *err = "WinHttpOpenRequest failed";
        return false;
    }

    const wchar_t* headers = L"Content-Type: application/json\r\n";
    BOOL ok = ::WinHttpSendRequest(request, headers, -1L,
        const_cast<char*>(body.data()), static_cast<DWORD>(body.size()),
        static_cast<DWORD>(body.size()), 0);
    if (ok) ok = ::WinHttpReceiveResponse(request, nullptr);

    ::WinHttpCloseHandle(request);
    ::WinHttpCloseHandle(connect);
    ::WinHttpCloseHandle(session);

    if (!ok && err) *err = "callback post failed";
    return ok == TRUE;
}
