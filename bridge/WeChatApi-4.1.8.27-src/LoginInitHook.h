#pragma once

#include <string>

class WeChatBridge;
class MessageCallback;

bool InstallLoginInitHook(WeChatBridge* bridge, MessageCallback* callback);
std::string LoginInitHookStatusJson();
