#pragma once

#include "MessageCallback.h"
#include "WeChatBridge.h"

#include <atomic>
#include <string>

class HttpServer {
public:
    HttpServer(WeChatBridge& bridge, MessageCallback& callback);
    ~HttpServer();

    bool Start(const std::string& prefix);
    void Stop();
    void ServeForever();

private:
    std::string Dispatch(const std::string& method, const std::string& path, const std::string& body);

    WeChatBridge& bridge_;
    MessageCallback& callback_;
    void* queue_ = nullptr;
    std::atomic<bool> running_{ false };
};
