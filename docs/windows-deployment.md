# 微信多账号聚合系统 — Windows 部署指南

## 系统概述

```
┌──────── Windows 机器 ──────────────────────────────────┐
│                                                          │
│  WeChat #1  ←→  wcf.dll (TCP:10081)  ←┐                │
│  WeChat #2  ←→  wcf.dll (TCP:10082)  ←┤                │
│  WeChat #3  ←→  wcf.dll (TCP:10083)  ←┤                │
│  ...                                   ←┤                │
│  WeChat #N  ←→  wcf.dll (TCP:10080+N) ←┘                │
│                                                          │
│  ┌────────── wcf-bridge.py (聚合桥) ──────────┐         │
│  │  轮询消息 → POST 到消息网关                  │         │
│  │  从网关取发送队列 → 发到微信                 │         │
│  └──────────────────┬─────────────────────────┘         │
└─────────────────────┼───────────────────────────────────┘
                      │ HTTP (localhost 或局域网)
┌─────────────────────┴───────────────────────────────────┐
│  消息网关 (Node.js, 端口 3028)                            │
│  └ 数据库 + WebSocket 推送                                │
│  前端 (Vite, 端口 5173)                                   │
│  └ 三栏客服工作台 UI                                       │
└─────────────────────────────────────────────────────────┘
```

---

## Step 0: 准备代码

确保你已经从 GitHub 或其它方式拿到了 `weixin-workbench` 目录，里面有这些关键文件：

```
weixin-workbench/
├── setup.bat                  ← 一键安装/启动脚本
├── bridge/
│   └── wcf-bridge.py          ← Python 聚合桥
├── packages/
│   ├── server/                ← 消息网关 (Node.js)
│   ├── web/                   ← 前端 (React)
│   └── shared/                ← 共享类型
└── docs/
    └── windows-deployment.md  ← 本文档
```

---

## Step 1: 安装基础环境

### 1.1 Git

下载: https://git-scm.com/download/win
安装: 一路 Next，保持默认选项

验证:
```cmd
git --version
```

### 1.2 Python 3.8+

下载: https://www.python.org/downloads/
安装: **务必勾选 "Add Python to PATH"**，然后点击 Install

验证:
```cmd
python --version
```

### 1.3 Node.js 22+

下载: https://nodejs.org/ (下载 v22 LTS 版本)
安装: 一路 Next

验证:
```cmd
node --version
```

### 1.4 pnpm

```cmd
npm install -g pnpm
```

验证:
```cmd
pnpm --version
```

---

## Step 2: 安装微信指定版本

wcf (WeChatFerry) 通过 DLL 注入工作，对微信版本有严格要求。

### 2.1 确认支持的版本

打开 wcf 仓库查看最新支持版本:
https://github.com/ziye0180/wcf

**当前已知兼容: WeChat 3.9.3.x 系列**

### 2.2 卸载现有微信

如果已安装其他版本，先卸载：
```
设置 → 应用 → 微信 → 卸载
```

### 2.3 下载指定版本

- 官网: https://pc.weixin.qq.com/
- 历史版本: 可以在百度/谷歌搜索 "WeChat 3.9.3 历史版本下载"

### 2.4 禁止自动更新

安装后立即设置：
1. 打开微信 → 左下角菜单 → 设置
2. 关于微信 → 取消勾选 "自动升级"

或手动删除自动更新程序（一劳永逸）：
```cmd
del "C:\Program Files\Tencent\WeChat\WeChatUpdate.exe" /f
del "C:\Program Files\Tencent\WeChat\WeChatUpdateSelf.exe" /f
```

---

## Step 3: 运行一键安装脚本

双击 `setup.bat`，或者：

```cmd
cd weixin-workbench
setup.bat
```

菜单中选择 **`[1] 检查环境`** 确认第 1 步的软件都已装好。

如果全部绿色 ✅，继续菜单 **`[3] 安装和配置 wcf`**：
- 自动从 GitHub 拉取 wcf 源码
- 安装 Python 依赖
- 自动创建:
  - 微信数据目录 `C:\wechat-data\`
  - 桌面快捷方式 `start-wechat.bat`
  - 桌面快捷方式 `start-bridge.bat`

---

## Step 4: 扫码登录微信

### 4.1 启动所有微信

双击桌面的 `start-wechat.bat`，或手动执行:

```cmd
start "WeChat-1" "C:\Program Files\Tencent\WeChat\WeChat.exe" -WeChatMsg=C:\wechat-data\user1
timeout /t 8
start "WeChat-2" "C:\Program Files\Tencent\WeChat\WeChat.exe" -WeChatMsg=C:\wechat-data\user2
timeout /t 8
start "WeChat-3" "C:\Program Files\Tencent\WeChat\WeChat.exe" -WeChatMsg=C:\wechat-data\user3
```

> 参数解释：
> - `-WeChatMsg` 指定数据目录，实现多开隔离
> - 有几个号就写几行
> - 每个号启动间隔 8 秒，避免冲突

### 4.2 扫码登录

每个微信弹窗后，用手机微信扫码登录。**第一次需要每号都扫码一次**，后续重启自动恢复。

---

## Step 5: 注入 wcf 到每个微信

### 5.1 查看 wcf 的注入命令

每种 wcf 版本的注入方式不同。打开 wcf 仓库查看:
https://github.com/ziye0180/wcf

常见方式:

```cmd
cd %USERPROFILE%\wcf

# 方式A — 自动注入 (注入正在运行的微信)
wcf.exe --port 10081

# 方式B — 按 PID 注入
wcf.exe --port 10081 --pid 1234

# 方式C — 使用 wcf.dll 手动注入
rundll32 wcf.dll,Inject
```

> 注意: Windows 可能会弹出安全警告，选择 "允许"

### 5.2 验证注入

```cmd
python -c "
from wcf import Wcf
wcf = Wcf(port=10081)
print('登录用户:', wcf.get_user_info())
print('联系人:', len(wcf.get_contacts()))
print('✅ wcf 工作正常')
"
```

正常输出类似:
```
登录用户: {'wxid': 'wxid_xxx', 'name': '我的微信号'}
联系人: 328
✅ wcf 工作正常
```

---

## Step 6: 启动聚合桥

打开一个 CMD 窗口，运行:

```cmd
cd weixin-workbench
python bridge/wcf-bridge.py --gateway http://127.0.0.1:3028 --ports 10081,10082,10083
```

如果聚合桥在另一台机器，`--gateway` 填消息网关所在机器的 IP。

显示:
```
[INFO] 配置: gateway=http://127.0.0.1:3028 ports=[10081, 10082, 10083]
[INFO] [wx-1] Connected to wcf at 127.0.0.1:10081
[INFO] [wx-1] Logged in as: 我的微信号 (wxid_xxx)
...
[INFO] Connected: 3/3 accounts online
[INFO] Starting main loop...
```

---

## Step 7: 启动消息网关 + 前端

打开第二个 CMD 窗口，运行:

```cmd
cd weixin-workbench

# 安装依赖 (首次需要)
pnpm install

# 启动消息网关 (端口 3028)
pnpm -C packages/server dev
```

打开第三个 CMD 窗口，运行:

```cmd
cd weixin-workbench

# 启动前端 (端口 5173)
pnpm -C packages/web dev
```

---

## Step 8: 打开工作台

浏览器打开: **http://localhost:5173**

看到左侧账号列表（3 个在线），中间聊天窗口，右侧联系人详情。

现在你可以：
- 让别人给你的微信号发消息 → 聚合桥收到 → 网关存储 → WebSocket 推送 → UI 实时显示 ✅
- 在 UI 中回复 → 网关写入发送队列 → 聚合桥取走 → wcf 发送 → 消息到达对方微信 ✅

---

## 完整启动顺序 (每日操作)

```cmd
步骤1: 双击 start-wechat.bat       → 启动所有微信
步骤2: 等待每个微信加载完成
步骤3: 扫码登录 (首次/过期时需要)
步骤4: 注入 wcf                     → 每个微信对应一个 TCP 端口
步骤5: python wcf-bridge.py ...     → 启动聚合桥
步骤6: pnpm -C packages/server dev  → 启动消息网关
步骤7: pnpm -C packages/web dev     → 启动前端
步骤8: 浏览器打开 http://localhost:5173
```

---

## 文件位置

| 文件/目录 | 位置 |
|---|---|
| 代码目录 | `weixin-workbench\` |
| wcf 源码 | `%USERPROFILE%\wcf\` |
| 微信数据 | `C:\wechat-data\user1\`, `user2\`, ... |
| 一键脚本 | `weixin-workbench\setup.bat` |
| 聚合桥 | `weixin-workbench\bridge\wcf-bridge.py` |
| 微信启动 | 桌面 `start-wechat.bat` |
| 聚合桥启动 | 桌面 `start-bridge.bat` |
| 消息网关日志 | CMD 窗口标准输出 |
| 数据库文件 | `weixin-workbench\packages\server\data\workbench.db` |

---

## 常见问题

### Q: WeChat 版本不匹配，wcf 注入了没反应？

> 确认 wcf 仓库首页标注的兼容版本号。
> 卸载现有微信，安装指定版本。
> 记住关闭自动更新。

### Q: Windows Defender 误报 wcf.dll？

> wcf.dll 是 DLL 注入程序，杀毒软件会报风险。
> 解决方法: 在 Windows Defender 中添加排除路径:
> 设置 → 隐私和安全性 → Windows 安全中心 → 病毒和威胁防护
→ 管理设置 → 排除项 → 添加 `%USERPROFILE%\wcf\`

### Q: 微信显示 "当前版本过低"？

> 这是腾讯在提示你升级。可以忽略这个提示，功能正常。
> 如果要升级，请等 wcf 也发布新版后再升。

### Q: 聚合桥显示 "Connection refused"？

> wcf 没有成功注入到微信进程。检查:
> 1. 微信是否在运行
> 2. wcf 注入命令是否正确
> 3. 端口号是否匹配 (注入时的 `--port` 和聚合桥的 `--ports`)

### Q: 微信过段时间自动退出？

> 这是微信的内存回收机制。长时间挂机会自动退出。
> 解决: 可以用定时任务每 30 分钟检查一次进程状态:
> 在聚合桥中已有心跳检测 + 自动重连逻辑。

### Q: 前端刷新后账号不见了？

> 前端每 10 秒轮询 `/api/accounts`。
> wcf 账号通过聚合桥注册后写入数据库，刷新后应该还在。
> 如果不在，检查消息网关日志 (`/api/wcf/account` 是否成功)。
