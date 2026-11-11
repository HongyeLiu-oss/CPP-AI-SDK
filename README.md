# ChatSDK · C++ AI 聊天 SDK

一个用 C++17 实现的 AI 聊天 SDK，封装了 **DeepSeek / 智谱 GLM** 两个云端模型的调用，并提供会话持久化、多用户注册登录与数据隔离。项目同时附带一个基于 HTTP + 原生前端的 Web 聊天界面。

## 功能特性

- **多模型接入**：统一的 `LLMProvider` 抽象，已实现 DeepSeek 与智谱 GLM，新增厂商只需实现接口并注册
- **流式输出**：支持 SSE 流式返回（`data: {"content":"..."}` + `data: [DONE]`），Web 端实时渲染
- **会话管理**：新建 / 列表 / 历史 / 删除，消息自动落库
- **SQLite 持久化**：会话与消息重启不丢，带 `PRAGMA user_version` 版本化迁移
- **用户体系**：注册 / 登录 / 登出，服务端 Session Token 认证（7 天有效期）
- **密码安全**：每用户独立随机盐 + SHA256，数据库不存明文
- **数据隔离**：所有会话与消息查询都带 `userId` 条件，跨用户访问返回 404 / 空
- **Web 界面**：登录注册页 + 聊天主界面，无额外前端构建依赖

## 架构分层

```
┌─────────────────────────────────────────────┐
│  server/static/index.html   Web 前端        │
├─────────────────────────────────────────────┤
│  ChatServer     HTTP 路由 / JSON / SSE      │
├─────────────────────────────────────────────┤
│  ChatSDK        门面，编排下面四个管理器     │
├──────────────┬──────────────┬───────────────┤
│  AuthManager │ SessionManager│  LLMManager  │
│  注册/登录    │  会话/消息    │  模型调度     │
├──────────────┴──────────────┴───────────────┤
│  DataManager    SQLite 持久化               │
├─────────────────────────────────────────────┤
│  LLMProvider → DeepSeekProvider / GLMProvider│
└─────────────────────────────────────────────┘
```

- `LLMProvider`：模型能力的抽象接口（初始化、可用性、全量/流式对话）
- `LLMManager`：持有 `modelName → provider` 映射，按模型名分发请求
- `SessionManager`：会话热缓存 + 懒加载（用到才回库读），按 `userId` 校验归属
- `DataManager`：唯一的 SQL 出口，prepared statement + 事务，保证多表写入原子性
- `AuthManager`：注册、登录、登出、token 校验，无状态，线程安全由 `DataManager` 保证
- `ChatSDK`：对外唯一入口，调用方不接触 SQL、密码哈希和具体 provider

## 目录结构

```
.
├── sdk/                    # 核心 SDK（静态库 ai_chat_sdk）
│   ├── include/
│   │   ├── common.h        # Message / Session / User / 配置结构体
│   │   ├── chat_sdk.h      # 门面
│   │   ├── llmprovider.h   # 模型抽象接口
│   │   ├── llmmanager.h
│   │   ├── deepseekprovider.h
│   │   ├── glmprovider.h
│   │   ├── sessionmanager.h
│   │   ├── datamanager.h
│   │   ├── authmanager.h
│   │   └── util/           # log.h / crypto.h
│   ├── src/
│   └── CMakeLists.txt
├── server/                 # Web 服务（可执行文件 chat_server）
│   ├── src/main.cpp        # 启动参数、模型注册
│   ├── src/chatserver.cpp  # HTTP 路由与鉴权
│   ├── include/chatserver.h
│   ├── static/index.html   # 前端页面
│   └── CMakeLists.txt
└── test/                   # 模型调用测试程序
```

## 依赖

- CMake ≥ 3.10、支持 C++17 的编译器
- [cpp-httplib](https://github.com/yhirose/cpp-httplib)（需 OpenSSL 支持，用于 HTTPS 请求大模型）
- jsoncpp、spdlog、fmt、sqlite3、gflags、OpenSSL

Ubuntu / Debian 安装示例：

```bash
sudo apt install build-essential cmake \
    libcpp-httplib-dev libjsoncpp-dev libspdlog-dev libfmt-dev \
    libsqlite3-dev libgflags-dev libssl-dev
```

## 构建

```bash
# 构建 Web 服务（会自动把 sdk 作为子目录一起编译）
cd server
mkdir -p build && cd build
cmake ..
make -j
```

产物为 `server/build/chat_server`，同时会把 `server/static` 复制到可执行文件旁。

## 运行

API Key 只从环境变量读取，避免出现在命令行历史和进程列表中：

```bash
export DEEPSEEK_API_KEY=sk-xxxx
export ZHIPU_API_KEY=xxxx

cd server/build
./chat_server --port=8083 --db_path=chat.db --static_dir=static
```

浏览器访问 `http://localhost:8083`，先注册账号再登录。

### 启动参数

| 参数 | 默认值 | 说明 |
| --- | --- | --- |
| `--port` | `8083` | HTTP 监听端口 |
| `--db_path` | `chat.db` | SQLite 文件路径，不存在会自动创建 |
| `--static_dir` | `static` | 前端静态目录 |
| `--deepseek_model` | `deepseek-flash` | DeepSeek 模型 ID |
| `--glm_model` | `glm-5.3-flash` | 智谱 GLM 模型 ID |

## HTTP API

统一响应格式：

```json
{ "success": true, "message": "ok", "data": {} }
```

除注册、登录、模型列表外，其余接口都需要请求头：

```
Authorization: Bearer <token>
```

| 方法 | 路径 | 鉴权 | 说明 |
| --- | --- | --- | --- |
| POST | `/api/register` | 否 | 注册，body `{username, password}`，不签发 token |
| POST | `/api/login` | 否 | 登录，返回 `{token, username}` |
| POST | `/api/logout` | 是 | 登出，作废当前 token |
| GET | `/api/models` | 否 | 可用模型列表 |
| GET | `/api/sessions` | 是 | 当前用户的会话列表 |
| POST | `/api/session` | 是 | 新建会话，body `{model}` |
| GET | `/api/session/:id/history` | 是 | 会话历史消息 |
| POST | `/api/message` | 是 | 发送消息（全量返回），body `{sessionId, content}` |
| POST | `/api/message/async` | 是 | 发送消息（SSE 流式返回） |
| DELETE | `/api/session/:id` | 是 | 删除会话（级联删除消息） |

## 数据库设计

SQLite 单文件，启动时自动建表并通过 `PRAGMA user_version` 记录 schema 版本。

| 表 | 说明 |
| --- | --- |
| `users` | 用户，`username` 唯一；存 `passwordHash` 与每用户独立 `salt` |
| `authTokens` | 登录态，`token` 为主键，带 `expireTime`（7 天） |
| `sessions` | 会话，`userId` 标识归属，外键关联用户 |
| `messages` | 消息，外键关联 `sessions(sessionId)`，`ON DELETE CASCADE` |

已开启外键约束并建立索引：`messages(sessionId)`、`sessions(userId)`、`authTokens(userId)`。

## 安全说明

- 密码存储：`SHA256(salt + password)`，salt 为每用户 16 字节随机值
- Token：32 字节随机数转 hex，服务端落库校验，登出即删除
- 数据隔离：所有会话查询都带 `userId`，越权访问返回 404 或空结果
- API Key：仅从环境变量读取，不写入配置文件和数据库
