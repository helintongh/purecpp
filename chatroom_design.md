# PureCpp 聊天室系统设计文档

## 1. 概述

PureCpp 聊天室是基于 WebSocket 的实时通信系统，集成在 PureCpp 社区论坛中。采用 Slack 风格 UI，支持多频道、消息反应、图片上传、@提及等功能。

### 技术栈

| 层级 | 技术 |
|------|------|
| 后端框架 | cinatra（C++20 协程 HTTP/WebSocket） |
| ORM | ormpp（支持 SQLite/MySQL/PostgreSQL） |
| 认证 | 自定义 JWT（HMAC-SHA1 签名） |
| 前端 | 原生 JavaScript + CSS（无框架依赖） |
| 数据库 | MySQL（生产默认），可切换 SQLite/PostgreSQL |

### 架构概览

```
┌──────────────────┐     ┌──────────────────┐     ┌──────────────────┐
│   Browser (JS)   │◄───►│   cinatra HTTP   │◄───►│  MySQL / SQLite  │
│                  │     │   + WebSocket    │     │                  │
│  chatroom.html   │     │  chatroom.hpp    │     │  5 tables        │
└──────────────────┘     └──────────────────┘     └──────────────────┘
        │                        │
        │  REST API (channels,   │  ormpp query_s
        │  history, search,      │  parameterized queries
        │  upload, mark_read)    │
        │                        │
        │  WebSocket (message,   │  chat_hub singleton
        │  react, edit, delete,  │  shared_mutex 读写锁
        │  typing, subscribe,    │  broadcast to channel
        │  switch_channel)       │  subscribers
```

---

## 2. 数据库设计

### 2.1 实体关系

```
users_t (主系统)
  │
  ├──< chat_messages (user_id)
  ├──< chat_reactions (user_id)
  ├──< chat_read_positions (user_id)
  └──< chat_channel_members (user_id)

chat_channels
  │
  ├──< chat_messages (channel_id)
  ├──< chat_channel_members (channel_id)
  └──< chat_read_positions (channel_id)

chat_messages
  └──< chat_reactions (message_id)
```

### 2.2 表定义

**chat_channels — 频道表**

| 字段 | 类型 | 约束 | 说明 |
|------|------|------|------|
| id | uint64_t | PK, AUTO_INCREMENT | |
| name | char[64] | UNIQUE | 频道名称 |
| topic | char[256] | | 频道主题描述 |
| creator_id | uint64_t | FK → users | 创建者 |
| is_private | uint32_t | | 0=公开, 1=私有 |
| created_at | uint64_t | | 毫秒时间戳 |
| message_count | uint64_t | | 频道消息总数（用于未读计算） |

初始种子数据：通用聊天、项目讨论、技术问题。

**chat_messages — 消息表**

| 字段 | 类型 | 说明 |
|------|------|------|
| id | uint64_t | PK, AUTO_INCREMENT |
| channel_id | uint64_t | 所属频道 |
| user_id | uint64_t | 发送者 |
| user_name | char[64] | 冗余存储（避免 JOIN） |
| content | string | 消息正文，≤ 2000 字符 |
| created_at | uint64_t | 毫秒时间戳 |
| channel_seq | uint64_t | 频道内递增序列号（用于未读计算） |

**chat_reactions — 表情反应表**

| 字段 | 类型 | 说明 |
|------|------|------|
| id | uint64_t | PK, AUTO_INCREMENT |
| message_id | uint64_t | 关联消息 |
| user_id | uint64_t | 操作用户 |
| emoji | char[16] | emoji 字符串 |
| created_at | uint64_t | 毫秒时间戳 |

**chat_read_positions — 已读位置表**

| 字段 | 类型 | 说明 |
|------|------|------|
| id | uint64_t | PK, AUTO_INCREMENT |
| user_id | uint64_t | 用户 |
| channel_id | uint64_t | 频道 |
| last_read_message_id | uint64_t | 最后已读消息 ID |
| last_read_channel_seq | uint64_t | 最后已读序列号 |
| updated_at | uint64_t | 更新时间 |

未读数 = `channel.message_count - read_position.last_read_channel_seq`

**chat_channel_members — 频道成员表**

| 字段 | 类型 | 说明 |
|------|------|------|
| id | uint64_t | PK, AUTO_INCREMENT |
| channel_id | uint64_t | 频道 |
| user_id | uint64_t | 成员 |
| joined_at | uint64_t | 加入时间 |

### 2.3 索引

| 索引名 | 表 | 列 | 说明 |
|--------|----|----|------|
| idx_chat_messages_channel_created | chat_messages | (channel_id, created_at) | 历史消息查询 |
| idx_chat_messages_channel_id | chat_messages | (channel_id, id) | 频道消息查询 |
| idx_chat_messages_channel_seq | chat_messages | (channel_id, channel_seq) | 未读计算 |
| idx_chat_reactions_message | chat_reactions | (message_id) | 消息反应查询 |
| idx_chat_read_positions_user_channel | chat_read_positions | (user_id, channel_id) | 已读位置查询 |
| idx_chat_channel_members_channel_user | chat_channel_members | (channel_id, user_id) | 成员查询 |
| ft_chat_messages_content | chat_messages | content (FULLTEXT) | 全文搜索（仅 MySQL） |

---

## 3. 后端架构

### 3.1 核心类

#### chat_hub（连接管理器，单例）

管理所有活跃的 WebSocket 会话，使用 `std::shared_mutex` 读写锁实现高并发。

```cpp
class chat_hub {
  struct session {
    uint64_t user_id;
    std::string user_name;
    shared_ptr<coro_http_connection> conn;
    std::atomic<uint64_t> last_msg_time{0};  // 原子操作，无需写锁
    uint64_t active_channel_id = 0;          // 当前查看的频道
    std::set<uint64_t> subscribed_channels;  // 订阅的频道集合
    std::mutex queue_mtx;                    // 每连接独立的发送队列锁
    std::deque<queued_frame> send_queue;     // 异步发送队列
  };

  // 写操作 — unique_lock
  add_result add(key, user_id, user_name, conn);
  remove_result remove(key);
  void subscribe_channel(key, channel_id);
  void set_active_channel(key, channel_id);

  // 读操作 — shared_lock
  vector<pair<uint64_t, string>> online_users();  // 从缓存读取
  bool check_msg_rate(key);                        // atomic + shared_lock

  // 广播 — shared_lock 下直接遍历，零拷贝
  Lazy<void> broadcast(payload, priority);
  Lazy<void> broadcast_active_channel(channel_id, payload, priority);
  Lazy<void> broadcast_inactive_subscribers(channel_id, payload, priority);

private:
  std::shared_mutex mtx_;
  unordered_map<uint64_t, shared_ptr<session>> sessions_;
  unordered_map<uint64_t, string> online_users_cache_;  // 增量维护
  unordered_map<uint64_t, unordered_set<uint64_t>> active_channel_keys_;
  unordered_map<uint64_t, unordered_set<uint64_t>> channel_subscriber_keys_;
};
```

**消息优先级系统：**

| 优先级 | 类型 | 行为 |
|--------|------|------|
| normal | 聊天消息、反应更新 | 队列满时驱逐低优先级帧，驱逐失败则关闭慢客户端 |
| low | typing、presence、online_update | 队列满时直接丢弃 |

**慢客户端保护：** 每连接最多 256 帧 / 512KB 待发送数据，超限后优先丢弃 low 优先级帧，仍不够则断开连接。

#### chat_channel_cache（频道缓存，单例）

缓存频道元数据（30 秒 TTL），使用 `std::shared_mutex` 两阶段锁：

```cpp
class chat_channel_cache {
  // cache hit → shared_lock（快速路径）
  // cache miss → DB 查询 → unique_lock 写入
  bool get(channel_id, out, force_refresh = false);

  // channel_mutex() 也用两阶段锁
  // 每个频道有独立的 mutex，用于消息插入时的序列号分配
  shared_ptr<mutex> channel_mutex(channel_id);
};
```

#### chat_access_cache（权限缓存，单例）

缓存用户的频道访问权限（30 秒 TTL），使用 `std::shared_mutex`。

#### chat_handler_t（请求处理器）

处理所有 REST API 和 WebSocket 连接。

### 3.2 REST API 端点

所有端点均需 JWT 认证（`check_token{}` 切面）和限流（`rate_limiter_aspect{}`）。

| 方法 | 路径 | 功能 | 入参 | 返回 |
|------|------|------|------|------|
| GET | `/api/v1/chat/channels` | 频道列表 | — | 含未读数和权限的频道数组 |
| GET | `/api/v1/chat/history` | 消息历史 | `channel_id`, `limit` | 按时间倒序消息数组 |
| GET | `/api/v1/chat/search` | 消息搜索 | `q`, `channel_id`(可选) | 匹配消息数组（≤100） |
| POST | `/api/v1/chat/channel` | 创建频道 | `name`, `topic`, `is_private` | 新频道对象 |
| POST | `/api/v1/chat/mark_read` | 标记已读 | `channel_id`, `last_message_id` | 成功/失败 |
| POST | `/api/v1/chat/upload` | 上传文件 | `filename`, `file_data`(base64) | `url`, `filename` |
| GET | `/ws/chat` | WebSocket | `token`(query) | 升级为 WS 连接 |

**统一响应格式：**

```json
{
  "success": true,
  "message": "操作成功",
  "code": 200,
  "data": { ... }
}
```

### 3.3 WebSocket 协议

#### 客户端 → 服务端

```
┌──────────────────┬───────────────┬─────────────────────┐
│ type             │ 字段          │ 说明                │
├──────────────────┼───────────────┼─────────────────────┤
│ message          │ channel_id,   │ 发送消息            │
│                  │ text          │ (500ms 冷却)        │
│ subscribe_channel│ channel_id    │ 订阅频道通知        │
│ switch_channel   │ channel_id    │ 切换当前查看频道    │
│ react            │ message_id,   │ 切换表情反应        │
│                  │ emoji         │ (存在则移除)        │
│ edit_message     │ message_id,   │ 编辑自己的消息      │
│                  │ text          │ (验证作者)          │
│ delete_message   │ message_id    │ 删除自己的消息      │
│                  │               │ (验证作者)          │
│ typing           │ channel_id    │ 打字指示            │
│                  │               │ (客户端3秒节流)     │
└──────────────────┴───────────────┴─────────────────────┘
```

#### 服务端 → 客户端

```
┌──────────────────┬────────────────────────────────────────┐
│ type             │ 说明                                   │
├──────────────────┼────────────────────────────────────────┤
│ hello            │ 连接成功，返回用户信息和在线列表       │
│ message          │ 新消息广播（含完整 msg 对象）          │
│ channel_activity │ 非活跃频道的未读增量通知               │
│ presence_join    │ 用户上线                               │
│ presence_leave   │ 用户下线                               │
│ online_update    │ 在线用户列表更新                       │
│ reaction_update  │ 消息的反应变更（含分组后的完整反应）   │
│ message_edited   │ 消息已编辑（message_id, text）         │
│ message_deleted  │ 消息已删除（message_id, channel_id）   │
│ typing           │ 某用户正在输入（user_name, channel_id）│
│ error            │ 错误消息                               │
└──────────────────┴────────────────────────────────────────┘
```

**消息广播策略：**
- `message` → 仅广播给 active_channel 的用户（normal 优先级）
- `channel_activity` → 仅广播给订阅但非 active 的用户（low 优先级）
- `presence_join/leave`, `online_update` → 全局广播（low 优先级）

#### 连接生命周期

```
Client                              Server
  │                                   │
  │── GET /ws/chat?token=JWT ────────►│
  │                                   ├── validate_jwt_token()
  │                                   ├── query users_t for user_name
  │                                   ├── chat_hub.add(session)
  │◄── {"type":"hello",...} ──────────│
  │◄── {"type":"presence_join",...} ──│── broadcast to all (low)
  │                                   │
  │── {"type":"subscribe_channel"} ──►│── 订阅频道通知
  │── {"type":"switch_channel"} ─────►│── 切换活跃频道
  │                                   │
  │── {"type":"message",...} ────────►│── check_msg_rate() [shared_lock]
  │                                   ├── channel_mutex lock
  │                                   ├── DB transaction: insert + update seq
  │◄── {"type":"message",...} ────────│── broadcast_active_channel (normal)
  │◄── {"type":"channel_activity"} ──│── broadcast_inactive_subscribers (low)
  │                                   │
  │── {"type":"react",...} ──────────►│── toggle reaction in DB
  │◄── {"type":"reaction_update"} ───│── broadcast_active_channel
  │                                   │
  │     ... (message loop) ...        │
  │                                   │
  │── [connection close] ────────────►│
  │                                   ├── chat_hub.remove()
  │◄── {"type":"presence_leave"} ─────│── broadcast (low)
  │◄── {"type":"online_update"} ──────│── broadcast (low)
```

### 3.4 辅助函数

| 函数 | 功能 |
|------|------|
| `json_escape(s)` | JSON 字符串转义（`"`, `\`, `\n`, `\r`, `\t`） |
| `build_msg_json(m)` | 构建单条消息 JSON（含 reactions） |
| `batch_build_reactions(msgs)` | 批量查询消息反应（避免 N+1） |
| `build_online_json(users)` | 构建在线用户 JSON 数组 |
| `chat_format_time(ts_ms)` | 今天→`HH:MM`，非今天→`MM-DD HH:MM`（线程安全） |
| `arr_to_str(arr)` / `str_to_arr(arr, s)` | char 数组与 string 互转（安全 strnlen） |

---

## 4. 前端架构

### 4.1 页面布局

```
┌─────────────────────────────────────────────────────────┐
│                      Header                             │
├────────────┬────────────────────────────┬────────────────┤
│  Sidebar   │       Main Chat           │  Channel       │
│  (260px)   │       (flex: 1)           │  Users         │
│            │                           │  (220px)       │
│ ┌────────┐ │ ┌──────────────────────┐  │ ┌────────────┐│
│ │Team    │ │ │Chat Header + Search  │  │ │ Online     ││
│ │Header  │ │ └──────────────────────┘  │ │ Members    ││
│ ├────────┤ │                           │ │            ││
│ │User    │ │ ┌──────────────────────┐  │ │ Avatar     ││
│ │Profile │ │ │                      │  │ │ + Name     ││
│ ├────────┤ │ │   Chat Messages      │  │ │ ...        ││
│ │Channels│ │ │   (scrollable)       │  │ │            ││
│ │ # 通用 │ │ │                      │  │ └────────────┘│
│ │ # 项目 │ │ │                      │  │              │
│ │ + 新建 │ │ └──────────────────────┘  │              │
│ ├────────┤ │                           │              │
│ │Online  │ │ ┌──────────────────────┐  │              │
│ │Members │ │ │ Typing indicator     │  │              │
│ │        │ │ │ [textarea] [emoji]   │  │              │
│ └────────┘ │ │ [send]              │  │              │
│            │ └──────────────────────┘  │              │
├────────────┴────────────────────────────┴────────────────┤
│                      Footer                             │
└─────────────────────────────────────────────────────────┘
```

移动端（≤768px）：侧边栏缩至 70px（仅图标），右侧成员栏缩至 60px（仅头像）。

### 4.2 状态管理

```javascript
// 用户与连接
currentUser = { id, name, avatar, color }  // 从 apiService.getUserInfo() 获取
accessToken        // 从 apiService.getAccessToken() 获取
ws                 // WebSocket 实例
reconnectAttempts  // 指数退避计数器

// 数据
channels           // [{id, name, topic, is_private, creator_id, unread}]
messages           // { channelId: [{ id, userId, userName, text, time, reactions, edited }] }
onlineUsers        // [{id, name}]
currentChannelId   // 当前活跃频道

// UI 状态
messageLoadCount   // 分页加载数（默认 50，"阅读更多" 每次 +20）
isSearchMode       // 搜索模式开关
savedMessages      // 搜索模式下的消息备份
```

### 4.3 核心数据流

**初始化流程：**

```
initApp()
  ├── apiService.getAccessToken() → accessToken
  ├── apiService.getUserInfo() → currentUser
  │   └── 未登录 → 跳转 /login.html
  └── startChat()
        ├── loadChannels()        → GET /api/v1/chat/channels
        ├── loadHistory(ch[0].id) → GET /api/v1/chat/history
        ├── renderChannels()
        ├── renderMessages()
        ├── connectWebSocket()    → wss://host/ws/chat?token=...
        └── setupEventListeners()
```

**消息发送流程：**

```
用户输入 → Enter / 点击发送
  └── sendMessage()
        ├── 验证: 非空, ≤ 2000 字符, 有活跃频道
        ├── wsSend({type: "message", channel_id, text})
        └── 清空输入框

服务端广播 → 活跃频道用户收到完整消息，其他订阅者收到 channel_activity
  └── handleWsMessage({type: "message", msg: {...}})
        ├── normalizeMsg(msg) → 统一格式
        ├── messages[channel_id].push(msg)
        ├── 当前频道 → appendSingleMessage(msg)  // 增量追加 DOM
        └── 其他频道 → channel.unread++ → renderChannels()
```

**WebSocket 重连策略：**

```
断开连接
  └── onclose()
        ├── delay = min(1000 * 2^attempts, 30000)
        ├── attempts++
        ├── updateConnectionStatus('reconnecting')
        │     └── 禁用发送按钮, 显示 "重新连接中..."
        └── setTimeout(connectWebSocket, delay)

重连成功
  └── onopen()
        ├── attempts = 0
        ├── restoreChatStateAfterReconnect()
        │     ├── loadChannels()       // 刷新频道列表
        │     ├── loadHistory()        // 刷新当前频道历史
        │     ├── markChannelRead()    // 标记已读
        │     └── syncWsChannelState() // 重新订阅所有频道
        └── updateConnectionStatus('connected')
```

### 4.4 消息渲染

`formatMessageText(text)` 处理链：

```
原始文本
  → escapeHtml()           // 防 XSS: & < > " '
  → 图片 markdown          // ![alt](url) → <img>（验证 URL 协议）
  → @提及高亮              // @name → <span class="mention-*">
  → URL 自动链接           // https://... → <a target="_blank">
```

消息 UI 结构：

```
┌──────┬──────────────────────────────────────┐
│ 头像 │ 用户名                    14:30      │
│  T   │ 消息文本内容                         │
│      │ 👍 2  ❤️ 1                            │
│      │ [表情] [回复] [编辑] [删除]          │
└──────┴──────────────────────────────────────┘
         ↑ 自己的消息: 绿色背景 + 编辑/删除按钮
         ↑ 已编辑消息: 时间旁显示 "(已编辑)"
```

**渲染优化：** 新消息通过 `appendSingleMessage()` 增量追加 DOM，仅在切换频道、搜索等场景做全量重建。

### 4.5 功能模块

| 模块 | 交互方式 | 说明 |
|------|----------|------|
| 频道切换 | 点击左侧频道 | 懒加载历史, 标记已读, 清除未读 badge |
| 消息搜索 | 头部搜索框 | MySQL FULLTEXT / SQLite LIKE, 保存/恢复消息状态 |
| 表情选择 | 输入框旁按钮 | 72 个常用 emoji 网格, 插入到光标位置 |
| 消息反应 | 消息下方按钮 | 15 个常用 emoji, toggle 切换 |
| @提及 | 输入 `@` 触发 | 自动完成在线用户列表 |
| 图片上传 | 输入框旁按钮 | 选择文件 → base64 → 上传 → markdown 消息 |
| 打字指示 | 自动 | 3 秒节流, 对方输入时显示, 3 秒后消失 |
| 频道创建 | `+` 按钮 | prompt 输入名称/主题, confirm 选择公开/私有 |

---

## 5. 安全设计

### 5.1 认证

- REST API：Bearer token 通过 `check_token{}` 切面验证
- WebSocket：token 通过 query 参数传递（WS 协议不支持自定义 header），服务端在握手阶段验证
- 上传接口：需登录验证（`get_user_id_from_token`），未登录返回 401
- 用户信息：复用主系统 `users_t` 表，不维护独立用户表

### 5.2 SQL 注入防护

所有数据库查询使用 ormpp 参数化查询：

```cpp
// 安全: 参数化绑定
conn->query_s<chat_reaction_t>(
    "message_id = ? AND user_id = ? AND emoji = ?",
    message_id, user_id, emoji);

conn->delete_records_s<chat_reaction_t>("id = ?", id);
```

HTTP 参数解析使用 `try-catch` 包装 `std::stoull`/`std::stoi`，防止恶意输入导致异常。

### 5.3 XSS 防护

- 前端：所有用户输入经 `escapeHtml()` 转义后再插入 DOM
- 前端：图片 markdown URL 验证协议（仅允许 `http://`、`https://`、`/` 开头）
- 后端：JSON 输出经 `json_escape()` 转义引号和控制字符

### 5.4 输入验证

| 字段 | 限制 | 校验位置 |
|------|------|----------|
| 消息内容 | ≤ 2000 字符 | 前端 maxlength + 后端 |
| 频道名称 | ≤ 60 字符 | 后端 |
| 频道主题 | ≤ 250 字符 | 后端 |
| emoji | ≤ 14 字节 | 后端 |
| 搜索关键词 | ≤ 100 字符 | 后端 |
| 上传文件 | ≤ 5MB, 仅图片格式 | 前端 accept + 后端白名单 |

**上传文件扩展名白名单：** `.jpg`, `.jpeg`, `.png`, `.gif`, `.bmp`, `.webp`, `.svg`

### 5.5 权限控制

| 操作 | 权限要求 |
|------|----------|
| 查看公开频道 | 无需登录 |
| 查看私有频道 | 创建者 或 频道成员（未登录不可见） |
| 发送消息 | 已登录 + 频道访问权限 |
| 编辑消息 | 仅消息作者 |
| 删除消息 | 仅消息作者（同时更新 message_count） |
| 创建频道 | 已登录 |
| 上传文件 | 已登录 |

### 5.6 限流

- HTTP 路由：`rate_limiter_aspect{}` 令牌桶算法
- WebSocket 消息：每连接 500ms 冷却期（atomic + shared_lock，无写锁争用）
- 打字指示器：客户端 3 秒节流

---

## 6. 性能设计

### 6.1 并发模型

```
┌──────────────────────────────────────────┐
│          chat_hub (shared_mutex)          │
│                                          │
│  写操作 (unique_lock, 低频):             │
│    add / remove / subscribe / switch     │
│                                          │
│  读操作 (shared_lock, 高频):             │
│    broadcast* / check_msg_rate /         │
│    online_users                          │
│                                          │
│  每连接独立:                              │
│    queue_mtx → send_queue → drain coroutine│
└──────────────────────────────────────────┘
```

**设计要点：**
- `shared_mutex` 读写分离：broadcast（热路径）只需 shared_lock，多个 broadcast 可并发执行
- broadcast 零拷贝：直接在 shared_lock 下遍历 session map 并 enqueue，不再复制 session vector
- `last_msg_time` 使用 `std::atomic<uint64_t>`，rate check 只需 shared_lock
- `online_users_cache_` 增量维护：add/remove 时更新，查询 O(unique_users)
- 连接 ID 使用 `atomic<uint64_t>` 单调递增，避免指针复用冲突
- 每连接独立的 drain coroutine 负责异步发送，不阻塞 broadcast

**锁嵌套顺序：** `shared_lock(mtx_)` → `scoped_lock(queue_mtx)`，无死锁风险。

### 6.2 缓存层

| 缓存 | TTL | 锁 | 用途 |
|------|-----|-----|------|
| chat_channel_cache | 30s | shared_mutex（两阶段锁） | 频道元数据 + 每频道 mutex |
| chat_access_cache | 30s | shared_mutex | 用户频道访问权限 |

### 6.3 数据库优化

- 批量查询 reactions 避免 N+1（`batch_build_reactions`）
- 批量查询 read_positions 和 memberships（`get_channels`）
- 启动时仅在存在 `channel_seq=0` 的消息时才执行全量回填
- MySQL FULLTEXT 索引支持高效全文搜索

### 6.4 压测数据（SQLite，本地 Windows 开发机）

| 连接数 | Sender | 发送 QPS | 投递 FPS | 延迟 avg | 延迟 p99 |
|--------|--------|----------|----------|----------|----------|
| 200 | 10 | 17 | 3,400 | 47ms | 80ms |
| 500 | 20 | 34 | 17,000 | 111ms | 157ms |
| 1,000 | 40 | 68 | 65,500 | 156ms | 254ms |
| 2,000 | 40 | 68 | 121,600 | 244ms | 356ms |
| 5,000 | 40 | 64 | 319,000 | 169ms | 647ms |
| 5,000 | 100 | 94 | 267,040 | 1,681ms | 2,785ms |

> 注：5000 连接 + 100 sender 时延迟升高主要因 SQLite 单写者瓶颈，MySQL 生产环境下预计大幅改善。

---

## 7. 文件结构

```
purecpp1/
├── chatroom.hpp          # 后端核心: 实体定义 + 缓存 + chat_hub + chat_handler_t
├── db_backend.hpp        # 数据库后端抽象（SQLite/MySQL/PostgreSQL）
├── feather.cpp           # 路由注册（8 个聊天路由）
├── html/
│   ├── chatroom.html     # 前端: HTML + CSS + JavaScript 单文件
│   ├── header.html       # 导航栏（含聊天室链接）
│   ├── index.html        # 首页（含聊天室卡片链接）
│   ├── script/
│   │   ├── api_service.js  # 主系统 token 管理（聊天室复用）
│   │   └── layout.js       # 页头/页脚/主题切换
│   └── uploads/
│       └── chat/           # 上传文件存储目录
├── entity.hpp            # users_t 定义（聊天室复用）
├── jwt_token.hpp         # JWT 生成/验证
├── user_aspects.hpp      # check_token 切面
├── rate_limiter.hpp      # 限流器
├── cfg/
│   ├── db_config.json    # 数据库连接配置
│   └── user_config.json  # 应用配置（限流规则等）
└── .codex_bench/
    └── Program.cs        # 压测工具（HTTP QPS + WebSocket 场景）
```

---

## 8. 部署注意事项

1. **数据库**：默认使用 MySQL，CMake 构建时 `-DPURECPP_DB_BACKEND=sqlite` 可切换到 SQLite
2. **MySQL 建库**：`CREATE DATABASE purecpp CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;`
3. **表自动创建**：`init_chat_db()` 自动创建 5 张表和索引
4. **上传目录**：确保 `html/uploads/chat/` 目录存在且可写
5. **HTTPS**：WebSocket 在生产环境应使用 `wss://` 协议
6. **连接池**：`cfg/db_config.json` 中 `db_conn_num` 建议 100-200（匹配并发量）
7. **系统调优**：Linux 生产环境需调整 `ulimit -n`（文件描述符）以支持万人连接
