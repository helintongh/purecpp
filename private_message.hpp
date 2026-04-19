#pragma once

#include "common.hpp"
#include "entity.hpp"
#include "jwt_token.hpp"
#include "sensitive_word_filter.hpp"

using namespace cinatra;
using namespace ormpp;

namespace purecpp {

// ==================== DTOs ====================

struct pm_send_req {
  std::string receiver_id;
  std::string receiver_name;
  std::string content;
};

struct pm_message_view {
  std::string id;
  std::string sender_id;
  std::string receiver_id;
  std::string sender_name;
  bool is_own = false;
  std::string content;
  uint32_t is_read = 0;
  uint64_t created_at = 0;
};
YLT_REFL(pm_message_view, id, sender_id, receiver_id, sender_name, is_own, content, is_read, created_at);

struct pm_conversation_view {
  std::string peer_id;
  std::string peer_name;
  std::string last_message;
  uint64_t last_at = 0;
  uint64_t unread = 0;
};
YLT_REFL(pm_conversation_view, peer_id, peer_name, last_message, last_at, unread);

struct pm_mailbox_item_view {
  std::string id;
  std::string peer_id;
  std::string peer_name;
  std::string sender_id;
  std::string sender_name;
  std::string receiver_id;
  std::string receiver_name;
  std::string content;
  bool is_own = false;
  uint32_t is_read = 0;
  uint64_t created_at = 0;
};
YLT_REFL(pm_mailbox_item_view, id, peer_id, peer_name, sender_id, sender_name,
         receiver_id, receiver_name, content, is_own, is_read, created_at);

struct pm_mark_read_req {
  std::string sender_id;
};

struct pm_block_req {
  std::string target_user_id;
};

struct blocked_user_view {
  std::string user_id;
  std::string user_name;
  uint64_t created_at = 0;
};
YLT_REFL(blocked_user_view, user_id, user_name, created_at);

// ==================== Helpers ====================

inline bool can_use_private_message(uint64_t user_id) {
  auto conn = get_db_pool().get();
  if (!conn) return false;
  auto users = conn->select(ormpp::all)
                   .from<users_t>()
                   .where(col(&users_t::id).param())
                   .collect(user_id);
  return !users.empty();
}

inline bool parse_u64(std::string_view raw, uint64_t &value) {
  if (raw.empty()) return false;
  try {
    value = std::stoull(std::string(raw));
    return value != 0;
  } catch (...) {
    return false;
  }
}

template <typename Conn>
inline std::string get_user_name_by_id(Conn &conn, uint64_t user_id) {
  auto users = conn->select(ormpp::all)
                   .from<users_t>()
                   .where(col(&users_t::id).param())
                   .collect(user_id);
  return users.empty() ? "" : array_to_string(users[0].user_name);
}

// 检查 user_id 是否被 target_user_id 拉黑
inline bool is_blocked_by(uint64_t target_user_id, uint64_t user_id) {
  auto conn = get_db_pool().get();
  if (!conn) return false;
  auto rows = conn->select(ormpp::all)
                  .from<pm_blocklist_t>()
                  .where(col(&pm_blocklist_t::user_id).param() &&
                         col(&pm_blocklist_t::blocked_user_id).param())
                  .collect(target_user_id, user_id);
  return !rows.empty();
}

// ==================== Handler ====================

class private_message_handler_t {
public:
  // POST /api/v1/pm/send
  void send_message(coro_http_request &req, coro_http_response &resp) {
    resp.set_content_type<resp_content_type::json>();
    uint64_t sender_id = get_user_id_from_token(req);
    if (sender_id == 0) {
      resp.set_status_and_content(status_type::unauthorized, make_error("未登录", 401));
      return;
    }
    if (!can_use_private_message(sender_id)) {
      resp.set_status_and_content(status_type::forbidden, make_error("当前账号不可使用私信功能", 403));
      return;
    }
    pm_send_req body{};
    std::error_code ec;
    iguana::from_json(body, req.get_body(), ec);
    if (ec || body.content.empty() ||
        (body.receiver_id.empty() && body.receiver_name.empty())) {
      resp.set_status_and_content(status_type::bad_request, make_error("参数错误"));
      return;
    }
    size_t cp_count = 0;
    for (unsigned char c : body.content)
      if ((c & 0xC0u) != 0x80u) ++cp_count;
    if (cp_count > 2000) {
      resp.set_status_and_content(status_type::bad_request, make_error("消息内容不能超过2000字"));
      return;
    }
    if (sensitive_word_filter::instance().contains(body.content)) {
      resp.set_status_and_content(status_type::bad_request, make_error("消息包含违禁词汇"));
      return;
    }
    auto conn = get_db_pool().get();
    if (!conn) { set_server_internel_error(resp); return; }
    uint64_t receiver_id = 0;
    std::vector<users_t> receivers;
    if (!body.receiver_name.empty()) {
      receivers = conn->select(ormpp::all)
                      .from<users_t>()
                      .where(col(&users_t::user_name).param())
                      .collect(body.receiver_name);
      if (!receivers.empty()) {
        receiver_id = receivers[0].id;
      }
    } else {
      if (!parse_u64(body.receiver_id, receiver_id)) {
        resp.set_status_and_content(status_type::bad_request, make_error("接收者参数错误"));
        return;
      }
      receivers = conn->select(ormpp::all)
                      .from<users_t>()
                      .where(col(&users_t::id).param())
                      .collect(receiver_id);
    }
    if (receivers.empty()) {
      resp.set_status_and_content(status_type::bad_request, make_error("接收者不存在"));
      return;
    }
    if (receiver_id == sender_id) {
      resp.set_status_and_content(status_type::bad_request, make_error("不能给自己发私信"));
      return;
    }
    if (!can_use_private_message(receiver_id)) {
      resp.set_status_and_content(status_type::bad_request, make_error("接收者不存在或不可用"));
      return;
    }
    // 检查是否被对方拉黑
    if (is_blocked_by(receiver_id, sender_id)) {
      resp.set_status_and_content(status_type::forbidden, make_error("对方已拒收你的私信"));
      return;
    }
    private_message_t msg{};
    msg.sender_id = sender_id;
    msg.receiver_id = receiver_id;
    msg.content = body.content;
    msg.is_read = 0;
    msg.deleted_by_sender = 0;
    msg.deleted_by_receiver = 0;
    msg.created_at = get_timestamp_milliseconds();
    if (conn->insert(msg) < 0) { set_server_internel_error(resp); return; }
    resp.set_status_and_content(status_type::ok, make_success("私信发送成功"));
  }

  // GET /api/v1/pm/inbox
  void get_inbox(coro_http_request &req, coro_http_response &resp) {
    resp.set_content_type<resp_content_type::json>();
    uint64_t user_id = get_user_id_from_token(req);
    if (user_id == 0) {
      resp.set_status_and_content(status_type::unauthorized, make_error("未登录", 401));
      return;
    }
    if (!can_use_private_message(user_id)) {
      resp.set_status_and_content(status_type::forbidden, make_error("当前账号不可使用私信功能", 403));
      return;
    }
    auto conn = get_db_pool().get();
    if (!conn) { set_server_internel_error(resp); return; }

    auto msgs =
        conn->select(ormpp::all)
            .from<private_message_t>()
            .where(col(&private_message_t::receiver_id).param() &&
                   col(&private_message_t::deleted_by_receiver) == 0)
            .order_by(col(&private_message_t::created_at).desc())
            .collect(user_id);

    std::vector<pm_mailbox_item_view> convs;
    convs.reserve(msgs.size());
    for (auto &m : msgs) {
      pm_mailbox_item_view item{};
      item.id = std::to_string(m.id);
      item.peer_id = std::to_string(m.sender_id);
      item.peer_name = get_user_name_by_id(conn, m.sender_id);
      item.sender_id = std::to_string(m.sender_id);
      item.sender_name = item.peer_name;
      item.receiver_id = std::to_string(m.receiver_id);
      item.receiver_name = get_user_name_by_id(conn, m.receiver_id);
      item.content = m.content;
      item.is_own = false;
      item.is_read = m.is_read;
      item.created_at = m.created_at;
      convs.push_back(std::move(item));
    }
    resp.set_status_and_content(status_type::ok,
        make_data(convs, "获取收件箱成功", static_cast<int>(convs.size())));
  }

  // GET /api/v1/pm/sent
  void get_sentbox(coro_http_request &req, coro_http_response &resp) {
    resp.set_content_type<resp_content_type::json>();
    uint64_t user_id = get_user_id_from_token(req);
    if (user_id == 0) {
      resp.set_status_and_content(status_type::unauthorized, make_error("未登录", 401));
      return;
    }
    if (!can_use_private_message(user_id)) {
      resp.set_status_and_content(status_type::forbidden, make_error("当前账号不可使用私信功能", 403));
      return;
    }
    auto conn = get_db_pool().get();
    if (!conn) { set_server_internel_error(resp); return; }

    auto msgs =
        conn->select(ormpp::all)
            .from<private_message_t>()
            .where(col(&private_message_t::sender_id).param() &&
                   col(&private_message_t::deleted_by_sender) == 0)
            .order_by(col(&private_message_t::created_at).desc())
            .collect(user_id);

    std::vector<pm_mailbox_item_view> items;
    items.reserve(msgs.size());
    const std::string my_name = get_user_name_by_id(conn, user_id);
    for (auto &m : msgs) {
      pm_mailbox_item_view item{};
      item.id = std::to_string(m.id);
      item.peer_id = std::to_string(m.receiver_id);
      item.peer_name = get_user_name_by_id(conn, m.receiver_id);
      item.sender_id = std::to_string(m.sender_id);
      item.sender_name = my_name;
      item.receiver_id = std::to_string(m.receiver_id);
      item.receiver_name = item.peer_name;
      item.content = m.content;
      item.is_own = true;
      item.is_read = m.is_read;
      item.created_at = m.created_at;
      items.push_back(std::move(item));
    }
    resp.set_status_and_content(status_type::ok,
        make_data(items, "获取发件箱成功", static_cast<int>(items.size())));
  }

  // GET /api/v1/pm/history?peer_id=123&page=1&page_size=20
  void get_history(coro_http_request &req, coro_http_response &resp) {
    resp.set_content_type<resp_content_type::json>();
    uint64_t user_id = get_user_id_from_token(req);
    if (user_id == 0) {
      resp.set_status_and_content(status_type::unauthorized, make_error("未登录", 401));
      return;
    }
    if (!can_use_private_message(user_id)) {
      resp.set_status_and_content(status_type::forbidden, make_error("当前账号不可使用私信功能", 403));
      return;
    }
    auto peer_id_str = req.get_query_value("peer_id");
    if (peer_id_str.empty()) {
      resp.set_status_and_content(status_type::bad_request, make_error("缺少 peer_id 参数"));
      return;
    }
    uint64_t peer_id = 0;
    int page = 1, page_size = 20;
    try {
      peer_id = std::stoull(std::string(peer_id_str));
      auto page_str = req.get_query_value("page");
      auto page_size_str = req.get_query_value("page_size");
      if (!page_str.empty()) page = std::stoi(std::string(page_str));
      if (!page_size_str.empty()) page_size = std::stoi(std::string(page_size_str));
    } catch (...) {
      resp.set_status_and_content(status_type::bad_request, make_error("参数格式错误"));
      return;
    }
    if (peer_id == 0) {
      resp.set_status_and_content(status_type::bad_request, make_error("peer_id 无效"));
      return;
    }
    if (page < 1) page = 1;
    if (page_size < 1 || page_size > 100) page_size = 20;
    int offset = (page - 1) * page_size;

    auto conn = get_db_pool().get();
    if (!conn) { set_server_internel_error(resp); return; }

    // 软删除过滤：我发的且我没删，或对方发的且我没删
    auto history_where =
        (col(&private_message_t::sender_id) == user_id &&
         col(&private_message_t::receiver_id) == peer_id &&
         col(&private_message_t::deleted_by_sender) == 0) ||
        (col(&private_message_t::sender_id) == peer_id &&
         col(&private_message_t::receiver_id) == user_id &&
         col(&private_message_t::deleted_by_receiver) == 0);

    auto msgs = conn->select(ormpp::all)
                    .from<private_message_t>()
                    .where(history_where)
                    .order_by(col(&private_message_t::created_at).desc())
                    .limit(page_size)
                    .offset(offset)
                    .collect();

    auto all = conn->select(ormpp::all)
                   .from<private_message_t>()
                   .where(history_where)
                   .collect();
    int total = static_cast<int>(all.size());

    std::unordered_map<uint64_t, std::string> user_names;
    std::vector<pm_message_view> views;
    views.reserve(msgs.size());
    for (auto &m : msgs) {
      if (!user_names.count(m.sender_id)) {
        auto users = conn->select(ormpp::all)
                         .from<users_t>()
                         .where(col(&users_t::id).param())
                         .collect(m.sender_id);
        user_names[m.sender_id] = users.empty() ? "" : array_to_string(users[0].user_name);
      }
      views.push_back({std::to_string(m.id), std::to_string(m.sender_id),
                       std::to_string(m.receiver_id), user_names[m.sender_id],
                       m.sender_id == user_id, m.content, m.is_read,
                       m.created_at});
    }
    resp.set_status_and_content(status_type::ok, make_data(views, "获取聊天记录成功", total));
  }

  // DELETE /api/v1/pm/:id  — 软删除（仅对自己隐藏）
  void delete_message(coro_http_request &req, coro_http_response &resp) {
    resp.set_content_type<resp_content_type::json>();
    uint64_t user_id = get_user_id_from_token(req);
    if (user_id == 0) {
      resp.set_status_and_content(status_type::unauthorized, make_error("未登录", 401));
      return;
    }
    auto id_str = req.params_["id"];
    if (id_str.empty()) {
      resp.set_status_and_content(status_type::bad_request, make_error("缺少消息ID"));
      return;
    }
    uint64_t msg_id = 0;
    try { msg_id = std::stoull(std::string(id_str)); } catch (...) {
      resp.set_status_and_content(status_type::bad_request, make_error("消息ID格式错误"));
      return;
    }
    auto conn = get_db_pool().get();
    if (!conn) { set_server_internel_error(resp); return; }
    auto rows = conn->select(ormpp::all)
                    .from<private_message_t>()
                    .where(col(&private_message_t::id).param())
                    .collect(msg_id);
    if (rows.empty()) {
      resp.set_status_and_content(status_type::not_found, make_error("消息不存在"));
      return;
    }
    auto &m = rows[0];
    if (m.sender_id != user_id && m.receiver_id != user_id) {
      resp.set_status_and_content(status_type::forbidden, make_error("无权操作"));
      return;
    }
    if (m.sender_id == user_id) {
      conn->update<private_message_t>()
          .set(col(&private_message_t::deleted_by_sender), 1)
          .where(col(&private_message_t::id) == msg_id)
          .execute();
    }
    if (m.receiver_id == user_id) {
      conn->update<private_message_t>()
          .set(col(&private_message_t::deleted_by_receiver), 1)
          .where(col(&private_message_t::id) == msg_id)
          .execute();
    }
    resp.set_status_and_content(status_type::ok, make_success("删除成功"));
  }

  // POST /api/v1/pm/mark_read
  void mark_read(coro_http_request &req, coro_http_response &resp) {
    resp.set_content_type<resp_content_type::json>();
    uint64_t user_id = get_user_id_from_token(req);
    if (user_id == 0) {
      resp.set_status_and_content(status_type::unauthorized, make_error("未登录", 401));
      return;
    }
    pm_mark_read_req body{};
    std::error_code ec;
    iguana::from_json(body, req.get_body(), ec);
    uint64_t sender_id = 0;
    if (ec || !parse_u64(body.sender_id, sender_id)) {
      resp.set_status_and_content(status_type::bad_request, make_error("参数错误"));
      return;
    }
    auto conn = get_db_pool().get();
    if (!conn) { set_server_internel_error(resp); return; }
    auto unread = conn->select(ormpp::all)
                      .from<private_message_t>()
                      .where(col(&private_message_t::sender_id).param() &&
                             col(&private_message_t::receiver_id).param() &&
                             col(&private_message_t::is_read) == 0)
                      .collect(sender_id, user_id);
    for (auto &m : unread) {
      conn->update<private_message_t>()
          .set(col(&private_message_t::is_read), 1)
          .where(col(&private_message_t::id) == m.id)
          .execute();
    }
    resp.set_status_and_content(status_type::ok, make_success("已标记为已读"));
  }

  // GET /api/v1/pm/unread_count
  void get_unread_count(coro_http_request &req, coro_http_response &resp) {
    resp.set_content_type<resp_content_type::json>();
    uint64_t user_id = get_user_id_from_token(req);
    if (user_id == 0) {
      resp.set_status_and_content(status_type::unauthorized, make_error("未登录", 401));
      return;
    }
    if (!can_use_private_message(user_id)) {
      resp.set_status_and_content(status_type::forbidden, make_error("当前账号不可使用私信功能", 403));
      return;
    }
    auto conn = get_db_pool().get();
    if (!conn) { set_server_internel_error(resp); return; }
    auto unread = conn->select(ormpp::all)
                      .from<private_message_t>()
                      .where(col(&private_message_t::receiver_id).param() &&
                             col(&private_message_t::is_read) == 0 &&
                             col(&private_message_t::deleted_by_receiver) == 0)
                      .collect(user_id);
    resp.set_status_and_content(status_type::ok,
        make_data(static_cast<uint64_t>(unread.size()), "获取未读数成功"));
  }

  // POST /api/v1/pm/block  — 拉黑用户（拒收其私信）
  void block_user(coro_http_request &req, coro_http_response &resp) {
    resp.set_content_type<resp_content_type::json>();
    uint64_t user_id = get_user_id_from_token(req);
    if (user_id == 0) {
      resp.set_status_and_content(status_type::unauthorized, make_error("未登录", 401));
      return;
    }
    pm_block_req body{};
    std::error_code ec;
    iguana::from_json(body, req.get_body(), ec);
    uint64_t target_user_id = 0;
    if (ec || !parse_u64(body.target_user_id, target_user_id) ||
        target_user_id == user_id) {
      resp.set_status_and_content(status_type::bad_request, make_error("参数错误"));
      return;
    }
    auto conn = get_db_pool().get();
    if (!conn) { set_server_internel_error(resp); return; }
    // 已拉黑则幂等返回成功
    auto existing = conn->select(ormpp::all)
                        .from<pm_blocklist_t>()
                        .where(col(&pm_blocklist_t::user_id).param() &&
                               col(&pm_blocklist_t::blocked_user_id).param())
                        .collect(user_id, target_user_id);
    if (!existing.empty()) {
      resp.set_status_and_content(status_type::ok, make_success("已在黑名单中"));
      return;
    }
    pm_blocklist_t bl{};
    bl.user_id = user_id;
    bl.blocked_user_id = target_user_id;
    bl.created_at = get_timestamp_milliseconds();
    if (conn->insert(bl) < 0) { set_server_internel_error(resp); return; }
    resp.set_status_and_content(status_type::ok, make_success("已拉黑该用户"));
  }

  // DELETE /api/v1/pm/block/:target_id  — 解除拉黑
  void unblock_user(coro_http_request &req, coro_http_response &resp) {
    resp.set_content_type<resp_content_type::json>();
    uint64_t user_id = get_user_id_from_token(req);
    if (user_id == 0) {
      resp.set_status_and_content(status_type::unauthorized, make_error("未登录", 401));
      return;
    }
    auto target_str = req.params_["target_id"];
    if (target_str.empty()) {
      resp.set_status_and_content(status_type::bad_request, make_error("缺少 target_id"));
      return;
    }
    uint64_t target_id = 0;
    try { target_id = std::stoull(std::string(target_str)); } catch (...) {
      resp.set_status_and_content(status_type::bad_request, make_error("target_id格式错误"));
      return;
    }
    auto conn = get_db_pool().get();
    if (!conn) { set_server_internel_error(resp); return; }
    auto rows = conn->select(ormpp::all)
                    .from<pm_blocklist_t>()
                    .where(col(&pm_blocklist_t::user_id).param() &&
                           col(&pm_blocklist_t::blocked_user_id).param())
                    .collect(user_id, target_id);
    for (auto &bl : rows) {
      conn->remove<pm_blocklist_t>()
          .where(col(&pm_blocklist_t::id) == bl.id)
          .execute();
    }
    resp.set_status_and_content(status_type::ok, make_success("已解除拉黑"));
  }

  // GET /api/v1/pm/blocklist  — 查看我的黑名单
  void get_blocklist(coro_http_request &req, coro_http_response &resp) {
    resp.set_content_type<resp_content_type::json>();
    uint64_t user_id = get_user_id_from_token(req);
    if (user_id == 0) {
      resp.set_status_and_content(status_type::unauthorized, make_error("未登录", 401));
      return;
    }
    auto conn = get_db_pool().get();
    if (!conn) { set_server_internel_error(resp); return; }
    auto rows = conn->select(ormpp::all)
                    .from<pm_blocklist_t>()
                    .where(col(&pm_blocklist_t::user_id).param())
                    .collect(user_id);

    std::vector<blocked_user_view> result;
    for (auto &bl : rows) {
      auto users = conn->select(ormpp::all)
                       .from<users_t>()
                       .where(col(&users_t::id).param())
                       .collect(bl.blocked_user_id);
      blocked_user_view v{};
      v.user_id = std::to_string(bl.blocked_user_id);
      v.user_name = users.empty() ? "" : array_to_string(users[0].user_name);
      v.created_at = bl.created_at;
      result.push_back(std::move(v));
    }
    resp.set_status_and_content(status_type::ok,
        make_data(result, "获取黑名单成功", static_cast<int>(result.size())));
  }
};

// ==================== DB init ====================

inline bool init_pm_db() {
  auto conn = get_db_pool().get();
  if (!conn) return false;
  bool ok1 = conn->create_table<private_message_t>()
                 .auto_increment(col(&private_message_t::id))
                 .not_null(col(&private_message_t::sender_id),
                           col(&private_message_t::receiver_id),
                           col(&private_message_t::content),
                           col(&private_message_t::is_read),
                           col(&private_message_t::deleted_by_sender),
                           col(&private_message_t::deleted_by_receiver),
                           col(&private_message_t::created_at))
                 .execute();
  bool ok2 = conn->create_table<pm_blocklist_t>()
                 .auto_increment(col(&pm_blocklist_t::id))
                 .not_null(col(&pm_blocklist_t::user_id),
                           col(&pm_blocklist_t::blocked_user_id),
                           col(&pm_blocklist_t::created_at))
                 .execute();
  if (!ok1) CINATRA_LOG_ERROR << "Table 'private_messages' create error.";
  if (!ok2) CINATRA_LOG_ERROR << "Table 'pm_blocklist' create error.";
  return ok1 && ok2;
}

} // namespace purecpp
