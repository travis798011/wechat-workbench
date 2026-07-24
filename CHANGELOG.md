# Changelog

## 2026-07-24 — 修复 APK 通知监听

### Changed
- `phone-agent/app/src/main/res/xml/accessibility_config.xml` — 加 `typeNotificationStateChanged` 事件
- `phone-agent/app/src/main/java/com/wechathub/WeChatBridge.kt` — 重写核心逻辑
  - 删除 ClawBot 自回复代码（`detectClawBotMessage` 全套方法 ~200 行）
  - 新增通知监听：`parseNotification()` 解析系统通知
  - 新增 `typeIntoClawBot()` 将消息输入 ClawBot 聊天窗口
  - 保留简化版辅助方法（`findEditableInput`, `findSendButton`, `findClawBotTitle`）
- `CLAUDE.md` — 恢复为开发规范（原被日内进度覆盖）

### 数据流变更
```
之前: APK 自回复 ClawBot（不走后端）
现在: 通知 → APK 解析 → ClawBot 输入框 → iLink Bot → 后端 → 前端
```

## 2026-07-24 — Android APK 开发

### Added
- `phone-agent/` — Android 无障碍服务 APK 项目
  - MainActivity.kt — 配置界面
  - WeChatBridge.kt — 核心逻辑（监听微信通知 → 转发到 ClawBot）
  - AndroidManifest.xml — 权限 + 服务声明
  - res/ — UI 资源和无障碍配置
  - build.gradle.kts — Gradle 构建配置

### Changed
- 研究结论更新：免费和商业方案全部不可用，决定自建 Android 无障碍 APK
- TODO.md 重新整理

## 已废弃的方案（保留参考）
- bridge/ — wxhelper/wcf 相关（Windows 方案，WeChatAppEx 不支持）
- bridge-go/ — openwechat Go 方案（协议被封）
- packages/server/ilink/ — iLink Bot 协议（不符合需求但代码保留）

## 0.1.0 — 2026-07-22/23 (初始探索阶段)

### 已完成
- ✅ 项目 monorepo 初始化 (pnpm workspace)
- ✅ shared 包：类型定义
- ✅ server 包：Fastify 5 + sql.js + WebSocket 完整后端
- ✅ web 包：React 19 + Vite 6 + Tailwind 4 客服工作台前端
- 调研 40+ 项目：免费和商业方案全部不可用

### 文档产出
- RESEARCH.md / WECHATY-ECOSYSTEM.md / CLAUDE.md / TODO.md / CHANGELOG.md
