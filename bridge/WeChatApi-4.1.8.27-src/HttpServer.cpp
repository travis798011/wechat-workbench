#include "HttpServer.h"

#include "JsonLite.h"
#include "LoginInitHook.h"

#include <Windows.h>
#include <http.h>

#include <iostream>
#include <vector>

#pragma comment(lib, "httpapi.lib")

HttpServer::HttpServer(WeChatBridge& bridge, MessageCallback& callback)
    : bridge_(bridge), callback_(callback) {}

HttpServer::~HttpServer() {
    Stop();
}

bool HttpServer::Start(const std::string& prefix) {
    ULONG ret = ::HttpInitialize(HTTPAPI_VERSION_2, HTTP_INITIALIZE_SERVER, nullptr);
    if (ret != NO_ERROR) return false;

    HANDLE q = nullptr;
    ret = ::HttpCreateHttpHandle(&q, 0);
    if (ret != NO_ERROR) return false;
    queue_ = q;

    std::wstring wprefix(prefix.begin(), prefix.end());
    ret = ::HttpAddUrl(q, wprefix.c_str(), nullptr);
    if (ret != NO_ERROR) {
        ::CloseHandle(q);
        queue_ = nullptr;
        return false;
    }

    running_ = true;
    return true;
}

void HttpServer::Stop() {
    if (queue_) {
        ::CloseHandle(static_cast<HANDLE>(queue_));
        queue_ = nullptr;
    }
    ::HttpTerminate(HTTP_INITIALIZE_SERVER, nullptr);
    running_ = false;
}

void HttpServer::ServeForever() {
    std::vector<BYTE> buffer(sizeof(HTTP_REQUEST) + 16 * 1024);
    while (running_) {
        auto* req = reinterpret_cast<PHTTP_REQUEST>(buffer.data());
        ULONG bytes = 0;
        ULONG ret = ::HttpReceiveHttpRequest(static_cast<HANDLE>(queue_), HTTP_NULL_ID, 0,
            req, static_cast<ULONG>(buffer.size()), &bytes, nullptr);
        if (ret == ERROR_MORE_DATA) {
            buffer.resize(bytes);
            continue;
        }
        if (ret != NO_ERROR) continue;

        std::string body;
        if (req->Flags & HTTP_REQUEST_FLAG_MORE_ENTITY_BODY_EXISTS) {
            char chunk[4096];
            for (;;) {
                ULONG read = 0;
                ret = ::HttpReceiveRequestEntityBody(static_cast<HANDLE>(queue_), req->RequestId,
                    0, chunk, sizeof(chunk), &read, nullptr);
                if (ret == NO_ERROR || ret == ERROR_HANDLE_EOF) {
                    body.append(chunk, chunk + read);
                    if (ret == ERROR_HANDLE_EOF) break;
                } else {
                    break;
                }
            }
        }

        std::string method(req->pRawUrl ? "" : "");
        switch (req->Verb) {
        case HttpVerbGET: method = "GET"; break;
        case HttpVerbPOST: method = "POST"; break;
        default: method = "OTHER"; break;
        }

        std::string path = "/";
        if (req->CookedUrl.pAbsPath) {
            int wchar_count = static_cast<int>(req->CookedUrl.AbsPathLength / sizeof(wchar_t));
            int needed = ::WideCharToMultiByte(CP_UTF8, 0, req->CookedUrl.pAbsPath, wchar_count,
                nullptr, 0, nullptr, nullptr);
            if (needed > 0) {
                path.assign(static_cast<size_t>(needed), '\0');
                ::WideCharToMultiByte(CP_UTF8, 0, req->CookedUrl.pAbsPath, wchar_count,
                    &path[0], needed, nullptr, nullptr);
            }
        }
        std::string resp = Dispatch(method, path, body);

        HTTP_RESPONSE response{};
        response.StatusCode = 200;
        response.pReason = "OK";
        response.ReasonLength = 2;
        response.Headers.KnownHeaders[HttpHeaderContentType].pRawValue = "application/json";
        response.Headers.KnownHeaders[HttpHeaderContentType].RawValueLength = 16;

        HTTP_DATA_CHUNK data{};
        data.DataChunkType = HttpDataChunkFromMemory;
        data.FromMemory.pBuffer = const_cast<char*>(resp.data());
        data.FromMemory.BufferLength = static_cast<ULONG>(resp.size());
        response.EntityChunkCount = 1;
        response.pEntityChunks = &data;
        ::HttpSendHttpResponse(static_cast<HANDLE>(queue_), req->RequestId, 0, &response, nullptr, nullptr, nullptr, 0, nullptr, nullptr);
    }
}

std::string HttpServer::Dispatch(const std::string& method, const std::string& path, const std::string& body) {
    if (method != "POST") {
        return JsonLite::Error(405, "GET not supported, use POST");
    }

    JsonObject json = JsonLite::ParseObject(body);

    if (path == "/api/get_profile_cache") {
        return JsonLite::Ok(bridge_.GetProfileCacheJson());
    }

    if (path == "/api/debug_status") {
        return JsonLite::Ok("{"
            + JsonLite::StringField("api", "debug_status") + ","
            + std::string("\"bridge\":") + bridge_.DebugStatusJson() + ","
            + std::string("\"login_init_hook\":") + LoginInitHookStatusJson()
            + "}");
    }

    if (path == "/api/get_a8key") {
        A8KeyRequest req;
        req.url = json.GetString("url");
        req.url_type = static_cast<int>(json.GetInt("urlType", json.GetInt("url_type", 0)));
        req.scene = static_cast<int>(json.GetInt("scene", 0));
        std::string out, err;
        if (!bridge_.GetA8Key(req, &out, &err)) return JsonLite::Error(400, err);
        return JsonLite::Ok(out);
    }

    if (path == "/api/send_text_msg") {
        std::string err;
        if (!bridge_.SendTextMsg(json.GetString("wxid"), json.GetString("msg"), &err)) {
            return JsonLite::Error(500, err);
        }
        return JsonLite::Ok("{}");
    }

    if (path == "/api/send_xml") {
        XmlRequest req;
        req.wxid = json.GetString("wxid");
        req.title = json.GetString("title");
        req.description = json.GetString("description");
        req.thumb_url = json.GetString("thumbUrl", json.GetString("thumb_url"));
        req.url = json.GetString("url");
        std::string out, err;
        if (!bridge_.SendXml(req, &out, &err)) return JsonLite::Error(500, err);
        return JsonLite::Ok(out);
    }

    if (path == "/api/send_file_msg") {
        std::string out, err;
        if (!bridge_.SendFileMsgDebug(json.GetString("wxid"), json.GetString("filepath"),
                                      json.GetString("stage", "send"), &out, &err)) {
            return JsonLite::Error(500, err);
        }
        return JsonLite::Ok(out);
    }

    if (path == "/api/set_callback") {
        callback_.SetUrl(json.GetString("url"));
        return JsonLite::Ok("{\"callback\":\"" + JsonLite::Escape(callback_.GetUrl()) + "\"}");
    }

    if (path == "/api/mock_recv_msg") {
        WeChatMessage msg;
        msg.wxid = json.GetString("wxid");
        msg.sender = json.GetString("sender");
        msg.roomid = json.GetString("roomid");
        msg.content = json.GetString("content");
        msg.msgid = json.GetInt("msgid", 0);
        msg.type = static_cast<int>(json.GetInt("type", 1));
        msg.timestamp = json.GetInt("timestamp", 0);
        std::string err;
        if (!callback_.PublishIncomingMessage(msg, &err)) return JsonLite::Error(502, err);
        return JsonLite::Ok("{}");
    }

    return JsonLite::Error(404, "the url is not supported");
}
