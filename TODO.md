# TODO

## ✅ 已完成

### WeChat 通道调研
- [x] wxhelper DLL 注入 (WeChatAppEx 不支持)
- [x] wcf/WeChatFerry (WeChatAppEx 不支持)
- [x] 降级微信 3.9.x (登录被腾讯封)
- [x] openwechat iPad 协议 (号被限制)
- [x] Gewechat (已废弃)
- [x] Wechaty/PadLocal 生态 (全部 2022-2023 停更)
- [x] Paimon (联系不到供应商)
- [x] PadChat-SDK (闭源壳)
- [x] WeChatTweak (版本不兼容)
- [x] wechat-selkies / WechatOnCloud (VNC 像素级，不能聚合)
- [x] iLink Bot (官方可用，但不能登录自己号)
- [x] IMAI.WORK-AI-Phone (商业产品，不适合直接用)

### 结论
免费和商业方案全部不可用 → 自建 Android 无障碍服务 APK

### 代码完成
- [x] 后端 (Fastify + sql.js + WebSocket)
- [x] 前端 (React + Tailwind + 三栏客服 UI)
- [x] 数据库 (accounts/contacts/messages/sync_state)
- [x] CLI 文档
- [x] 开发规范 (CLAUDE.md)

### Phone Agent（Android APK）
- [x] Project skeleton (build.gradle.kts, AndroidManifest, res)
- [x] MainActivity.kt (配置界面)
- [x] WeChatBridge.kt (AccessibilityService 核心逻辑)
- [x] JDK 17 安装
- [x] Android SDK 安装 (platforms/android-34, build-tools/34.0.0)
- [x] aapt2 资源编译和链接

## ❌ 待完成

### wxhook Windows 桥
- [x] `bridge/wxhook-bridge.py` — 桥接脚本（DLL注入 → 消息POST后端 + 出站轮询）
- [ ] Windows 上 pip install wxhook requests
- [ ] 运行测试：python bridge/wxhook-bridge.py --backend http://192.168.2.44:3028
- [x] 安装 gradle (`brew install gradle`)
- [x] 编译完整 APK (`gradle assembleDebug`)
- [x] 无障碍服务监听系统通知（通知 → ClawBot 输入框 → iLink → 后端 → 前端）
- [ ] 签名并安装到手机
- [ ] 测试通知监听

### 后续优化
- [ ] ClawBot 双向转发逻辑完善
- [ ] 群消息过滤
- [ ] 消息去重
- [ ] 自启动和保活
