# 项目交接文档

## 项目目标
Android 手机监听微信通知 → 提取消息 → 自动转发给 ClawBot → iLink API 推送到后端 → 前端显示

## 当前状态

### 数据流
```
微信通知 → Android APK (AccessibilityService) ──HTTP──→ 后端 Fastify (:3028) ──WebSocket──→ 前端 React (:5173)
                                                        │
                                                        ├── sql.js DB (messages/accounts/contacts)
                                                        └── iLink Bot SDK → 微信消息收发
```

### 完成的模块
| 模块 | 状态 | 位置 |
|------|------|------|
| 后端 (Fastify + sql.js + WebSocket) | ✅ 可运行 | `packages/server/` |
| 前端客服 UI (React + Tailwind + 三栏布局) | ✅ 可运行 | `packages/web/` |
| 数据库 (accounts/contacts/messages/sync_state) | ✅ 可运行 | `packages/server/db/` |
| iLink Bot SDK 集成 | ✅ 可运行 | `packages/server/ilink/` |
| WCF 聚合桥路由 | ✅ 可用 | `packages/server/routes/wcf-bridge.ts` |
| Phone Agent APK 编译 | ✅ 编译通过 | `phone-agent/app/build/outputs/apk/debug/app-debug.apk` |

### Phone Agent APK 具体状态

**代码位置：** `phone-agent/`

**已安装环境：**
- JDK 17 → `~/java-17/jdk-17.0.2.jdk/Contents/Home`
- Android SDK → `~/android-sdk/` (platforms/android-34, build-tools/34.0.0)
- Gradle 9.6.1 (`brew install gradle`)
- 编译命令：`cd phone-agent && export JAVA_HOME=... ANDROID_HOME=... && gradle assembleDebug`

**当前问题：**
- APK 无障碍服务已安装到手机，微信消息能收到，但 APK 未监听到
- WeChatBridge.kt 当前只监听前台 ClawBot 聊天窗口变化，未监听系统通知
- 缺少 HTTP 客户端发送消息到后端

### 后端
- Fastify + sql.js + WebSocket 完整后端
- iLink Bot SDK 集成 (weixin-agent-sdk)
- REST API: accounts, contacts, messages, wcf-bridge
- WebSocket: 新消息推送、账号状态变更
- 启动：`pnpm -C packages/server dev`

### 前端
- React 19 + Vite 6 + Tailwind 4 三栏客服工作台
- 账号侧边栏 + 联系人侧边栏 + 消息窗口
- WebSocket 实时更新 + 3 秒轮询
- 启动：`pnpm -C packages/web dev`

### 研究结论
经过 40+ 项目调研，所有免费和商业方案（PadLocal/Wechaty/WeChatFerry/wxhelper/Gewechat/openwechat/WeChatTweak 等）均不可用或已废弃。
最终方案：**自建 Android 无障碍服务 APK + iLink Bot**。

## 项目结构

```
weixin-workbench/
├── packages/
│   ├── server/          # 后端 Fastify 5
│   │   ├── src/
│   │   │   ├── index.ts           # 入口
│   │   │   ├── app.ts             # Fastify 应用组装
│   │   │   ├── db/                # 数据库层 (sql.js)
│   │   │   │   ├── index.ts       # 初始化 + DDL
│   │   │   │   └── repository.ts  # CRUD 操作
│   │   │   ├── routes/            # REST 路由
│   │   │   │   ├── accounts.ts
│   │   │   │   ├── contacts.ts
│   │   │   │   ├── messages.ts
│   │   │   │   └── wcf-bridge.ts
│   │   │   ├── ilink/             # iLink Bot SDK 封装
│   │   │   │   ├── manager.ts     # 多账号管理
│   │   │   │   └── event-bus.ts   # 事件总线
│   │   │   └── ws/
│   │   │       └── handler.ts     # WebSocket
│   │   └── data/                  # SQLite 数据文件
│   ├── web/             # 前端 React
│   │   ├── src/
│   │   │   ├── App.tsx
│   │   │   ├── components/       # UI 组件
│   │   │   ├── hooks/            # 自定义 hooks
│   │   │   └── lib/              # API + WS 封装
│   │   └── dist/                 # 构建产物
│   └── shared/         # 共享类型
│       └── src/
│           ├── types.ts
│           └── ws-events.ts
├── phone-agent/        # Android APK
│   ├── app/src/main/java/com/wechathub/
│   │   ├── MainActivity.kt       # 配置界面
│   │   └── WeChatBridge.kt       # 无障碍服务
│   ├── build.gradle.kts
│   └── app/build.gradle.kts
├── bridge/             # 废弃 - Windows 方案
├── bridge-go/          # 废弃 - openwechat
├── docs/
├── CLAUDE.md
├── HANDOVER.md
├── TODO.md
└── CHANGELOG.md
```
