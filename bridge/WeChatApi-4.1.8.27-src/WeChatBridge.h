#pragma once

#include <mutex>
#include <string>

struct A8KeyRequest {
    std::string url;
    int url_type = 0;
    int scene = 0;
};

struct XmlRequest {
    std::string wxid;
    std::string title;
    std::string description;
    std::string thumb_url;
    std::string url;
};

class WeChatBridge {
public:
    bool Initialize();
    bool IsLogin() const;

    std::string GetProfileCacheJson() const;
    std::string DebugStatusJson() const;
    void UpdateProfileCacheJson(const std::string& json, const std::string& account_wxid);
    void UpdateLoginInitProfile(const std::string& wxid, const std::string& nickname, const std::string& source);
    void CaptureA8KeyResponseBytes(const void* data, size_t size, const char* source);

    bool GetA8Key(const A8KeyRequest& req, std::string* out_json, std::string* err);
    bool SendTextMsg(const std::string& wxid, const std::string& msg, std::string* err);
    bool SendXml(const XmlRequest& req, std::string* out_json, std::string* err);
    bool SendFileMsg(const std::string& wxid, const std::string& filepath, std::string* err);
    bool SendFileMsgDebug(const std::string& wxid, const std::string& filepath,
                          const std::string& stage, std::string* out_json, std::string* err);

private:
    uintptr_t WeixinBase() const;
    bool RefreshProfileCacheLocked() const;

    mutable std::mutex mu_;
    mutable std::string profile_cache_json_;
    mutable std::string account_wxid_;
};
