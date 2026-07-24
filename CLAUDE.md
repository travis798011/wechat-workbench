# WeChatHub 开发规范

## 核心原则

1. **要充分考虑代码结构化，代码要最低成本维护**
2. **代码模块化，功能边界明确**
3. **极易扩展** — 加入新功能时最低化更改现有代码
4. **运用合适的 coding pattern**，方便扩展和分层
5. **代码不直接加入常量**，配置文件结构清晰
6. **代码和配置加入工作流（workflow）概念**
7. **代码最少化** — 业务代码与复用代码低耦合
8. **每个功能单元必须有单元测试和功能测试脚本**（每次改动做回归测试）

## 开发流程

1. **先出 Plan** — 所有改动必须先提交计划，通过审核后才能改代码
2. **维护 TODO.md** — 跟踪任务状态
3. **维护 CHANGELOG.md** — 记录功能更新和改动
4. **维护项目结构文档** — `docs/` 下的说明文件每次改动后及时更新
5. **回归测试** — 每次改动要跑已有测试

## 代码规约

### 后端 (server)
- TypeScript + Fastify 5
- 路由按业务模块拆分到 `routes/` 目录
- 数据库操作统一走 `db/repository.ts`
- iLink SDK 封装在 `ilink/` 目录
- WebSocket 处理在 `ws/` 目录

### 前端 (web)
- TypeScript + React 19 + Vite 6 + Tailwind 4
- 组件按功能拆分到 `components/` 目录
- 自定义 hooks 在 `hooks/` 目录
- API 调用和 WebSocket 封装在 `lib/` 目录

### APK (phone-agent)
- Kotlin + Android AccessibilityService
- 网络通信、无障碍逻辑、UI 按模块分离
- 后端地址等配置集中管理
