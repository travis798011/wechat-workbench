#pragma once

#include <mutex>
#include <string>

struct WeChatMessage {
    std::string wxid;
    std::string sender;
    std::string roomid;
    std::string content;
    long long msgid = 0;
    int type = 0;
    long long timestamp = 0;
};

class MessageCallback {
public:
    void SetUrl(const std::string& url);
    std::string GetUrl() const;
    bool PublishIncomingMessage(const WeChatMessage& msg, std::string* err) const;
    bool PublishIncomingMessageAsync(const WeChatMessage& msg) const;

private:
    bool PostJson(const std::string& url, const std::string& body, std::string* err) const;

    mutable std::mutex mu_;
    std::string callback_url_;
};
