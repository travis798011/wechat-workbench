# 开发原则

## 核心约定
1. 所有改动必须先出 Plan，审核通过后才能改代码
2. 每个功能单元必须有单元测试和功能测试脚本，每次改动做回归测试
3. 项目结构文档（本文件）每次改动后及时更新
4. 维护 TODO.md 和 CHANGELOG.md

## 第三方依赖准入规则
1. **必须在集成前做存活检查，确认项目还在活跃维护中**
   - npm 包：`npm view <pkg> time` — 检查最新版本发布日期
   - GitHub 仓库：查看最近 commit 日期和 issue 活跃度
   - 官方网站：验证域名可解析且页面正常加载
   - 社区：确认近一年内有用户讨论或教程产出
2. **优先选用过去 6 个月内有过更新的依赖**，超过 1 年未更新的标记为高风险
3. **Star 数高但已停更的项目比 Star 少的活跃项目更危险** — 不要被 star 数字误导
4. **付费/商业方案的官网打不开 = 直接毙掉**

## 代码规范
1. **结构化优先** — 模块化设计，功能边界明确，最低成本维护
2. **扩展优先** — 加入新功能最低化更改现有代码，遵循开闭原则
3. **Pattern 先行** — 运用适当的 Coding Pattern（Strategy、Chain、Adapter、Observer 等），分层清晰
4. **配置外提** — 业务代码不含硬编码常量，统一在 config/ 目录管理
5. **工作流概念** — 业务逻辑按工作流组织，每个工作流是一个独立可测试的单元
6. **最少化代码** — 业务代码和复用代码低耦合，复用代码抽到 shared/

## 测试要求
- 每个 Service/Workflow 必须有对应的 `__tests__/` 目录
- 功能测试脚本放在 `scripts/test-*.sh` (Linux/Mac) 或 `scripts/test-*.bat` (Windows)
- 回归测试：`pnpm test` 运行全部测试
- PR 前必须通过回归测试

## 配置管理
- 所有配置集中在 `config/` 目录
- 按环境分层：`default.json` → `{env}.json` → 环境变量覆盖
- 配置结构在 `shared/src/config-schema.ts` 中定义
- 严禁在业务代码中硬编码端口、URL、密钥

## 工作流约定
- 每个业务流程封装为独立的 Workflow 类
- Workflow 可组合、可测试、可观测（日志/指标）
- 命名规范：`{domain}-workflow.ts` (如 `message-receive-workflow.ts`)

## 文档要求
- 项目结构见本文档
- API 文档随代码（JSDoc + 类型）
- 架构决策记录在 `docs/adr/`
- 部署文档在 `docs/deploy/`
