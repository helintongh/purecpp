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
  uint64_t receiver_id = 0;
  std::string content;
};

struct pm_message_view {
  uint64_t id = 0;
  uint64_t sender_id = 0;
  uint64_t receiver_id = 0;
  std::string content;
  uint32_t is_read = 0;
  uint64_t created_at = 0;
};
YLT_REFL(pm_message_view, id, sender_id, receiver_id, content, is_read, created_at);

struct pm_conversation_view {
  uint64_t peer_id = 0;
  std::string peer_name;
  std::string last_message;
  uint64_t last_at = 0;
  uint64_t unread = 0;
};
YLT_REFL(pm_conversation_view, peer_id, peer_name, last_message, last_at, unread);

struct pm_mark_read_req {
  uint64_t sender_id = 0;
};

struct pm_block_req {
  uint64_t target_user_id = 0;
};

// ==================== Helpers ====================

inline bool user_has_pm_privilege(uint64_t user_id) {
  auto conn = get_db_pool().get();
  if (!conn) return false;
  uint64_t now = get_timestamp_milliseconds();
  auto privs = conn->query_s<user_privileges_t>(
      "user_id = ? AND is_active = 1 AND end_time > ?", user_id, now);
  if (privs.empty()) return false;
  for (auto &p : privs) {
    auto ps = conn->query_s<privileges_t>(
        "id = ? AND privilege_type = ? AND is_active = 1",
        p.privilege_id, static_cast<int32_t>(PrivilegeType::PRIVATE_MESSAGE));
    if (!ps.empty()) return true;
  }
  return false;
}

// 检查 user_id 是否被 target_user_id 拉黑
inline bool is_blocked_by(uint64_t target_user_id, uint64_t user_id) {
  auto conn = get_db_pool().get();
  if (!conn) return false;
  auto rows = conn->query_s<pm_blocklist_t>(
      "user_id = ? AND blocked_user_id = ?", target_user_id, user_id);
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
    if (!user_has_pm_privilege(sender_id)) {
      resp.set_status_and_content(status_type::forbidden, make_error("需要私信特权", 403));
      return;
    }
    pm_send_req body{};
    std::string err;
    iguana::from_json(body, req.get_body(), err);
    if (!err.empty() || body.receiver_id == 0 || body.content.empty()) {
      resp.set_status_and_content(status_type::bad_request, make_error("参数错误"));
      return;
    }
    if (body.receiver_id == sender_id) {
      resp.set_status_and_content(status_type::bad_request, make_error("不能给自己发私信"));
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
    auto receivers = conn->query_s<users_t>("id = ?", body.receiver_id);
    if (receivers.empty()) {
      resp.set_status_and_content(status_type::bad_request, make_error("接收者不存在"));
      return;
    }
    // 接收者也需要有私信权限，否则无法读取消息
    if (!user_has_pm_privilege(body.receiver_id)) {
      resp.set_status_and_content(status_type::bad_request, make_error("对方未开通私信功能"));
      return;
    }
    // 检查是否被对方拉黑
    if (is_blocked_by(body.receiver_id, sender_id)) {
      resp.set_status_and_content(status_type::forbidden, make_error("对方已拒收你的私信"));
      return;
    }
    private_message_t msg{};
    msg.sender_id = sender_id;
    msg.receiver_id = body.receiver_id;
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
    if (!user_has_pm_privilege(user_id)) {
      resp.set_status_and_content(status_type::forbidden, make_error("需要私信特权", 403));
      return;
    }
    auto conn = get_db_pool().get();
    if (!conn) { set_server_internel_error(resp); return; }

    // 只取对当前用户可见的消息（软删除过滤）
    auto msgs = conn->query_s<private_message_t>(
        "(sender_id = ? AND deleted_by_sender = 0) OR "
        "(receiver_id = ? AND deleted_by_receiver = 0) "
        "ORDER BY created_at DESC",
        user_id, user_id);

    std::unordered_map<uint64_t, private_message_t *> latest;
    for (auto &m : msgs) {
      uint64_t peer = (m.sender_id == user_id) ? m.receiver_id : m.sender_id;
      if (latest.find(peer) == latest.end()) latest[peer] = &m;
    }
    std::unordered_map<uint64_t, uint64_t> unread_count;
    for (auto &m : msgs) {
      if (m.receiver_id == user_id && m.is_read == 0 && m.deleted_by_receiver == 0)
        unread_count[m.sender_id]++;
    }
    std::unordered_map<uint64_t, std::string> peer_names;
    for (auto &[pid, _] : latest) {
      auto users = conn->query_s<users_t>("id = ?", pid);
      if (!users.empty()) peer_names[pid] = array_to_string(users[0].user_name);
    }
    std::vector<pm_conversation_view> convs;
    for (auto &[pid, mp] : latest) {
      pm_conversation_view cv{};
      cv.peer_id = pid;
      cv.peer_name = peer_names.count(pid) ? peer_names[pid] : "";
      cv.last_message = mp->content.size() > 50 ? mp->content.substr(0, 50) + "..." : mp->content;
      cv.last_at = mp->created_at;
      cv.unread = unread_count.count(pid) ? unread_count[pid] : 0;
      convs.push_back(std::move(cv));
    }
    std::sort(convs.begin(), convs.end(),
              [](const auto &a, const auto &b) { return a.last_at > b.last_at; });
    resp.set_status_and_content(status_type::ok,
        make_data(convs, "获取收件箱成功", static_cast<int>(convs.size())));
  }

  // GET /api/v1/pm/history?peer_id=123&page=1&page_size=20
  void get_history(coro_http_request &req, coro_http_response &resp) {
    resp.set_content_type<resp_content_type::json>();
    uint64_t user_id = get_user_id_from_token(req);
    if (user_id == 0) {
      resp.set_status_and_content(status_type::unauthorized, make_error("未登录", 401));
      return;
    }
    if (!user_has_pm_privilege(user_id)) {
      resp.set_status_and_content(status_type::forbidden, make_error("需要私信特权", 403));
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
    auto msgs = conn->query_s<private_message_t>(
        "(sender_id = ? AND receiver_id = ? AND deleted_by_sender = 0) OR "
        "(sender_id = ? AND receiver_id = ? AND deleted_by_receiver = 0) "
        "ORDER BY created_at DESC LIMIT ? OFFSET ?",
        user_id, peer_id, peer_id, user_id, page_size, offset);

    auto all = conn->query_s<private_message_t>(
        "(sender_id = ? AND receiver_id = ? AND deleted_by_sender = 0) OR "
        "(sender_id = ? AND receiver_id = ? AND deleted_by_receiver = 0)",
        user_id, peer_id, peer_id, user_id);
    int total = static_cast<int>(all.size());

    std::vector<pm_message_view> views;
    views.reserve(msgs.size());
    for (auto &m : msgs)
      views.push_back({m.id, m.sender_id, m.receiver_id, m.content, m.is_read, m.created_at});
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
    auto id_str = req.get_path_param("id");
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
    auto rows = conn->query_s<private_message_t>("id = ?", msg_id);
    if (rows.empty()) {
      resp.set_status_and_content(status_type::not_found, make_error("消息不存在"));
      return;
    }
    auto &m = rows[0];
    if (m.sender_id != user_id && m.receiver_id != user_id) {
      resp.set_status_and_content(status_type::forbidden, make_error("无权操作"));
      return;
    }
    if (m.sender_id == user_id) m.deleted_by_sender = 1;
    if (m.receiver_id == user_id) m.deleted_by_receiver = 1;
    conn->update(m);
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
    std::string err;
    iguana::from_json(body, req.get_body(), err);
    if (!err.empty() || body.sender_id == 0) {
      resp.set_status_and_content(status_type::bad_request, make_error("参数错误"));
      return;
    }
    auto conn = get_db_pool().get();
    if (!conn) { set_server_internel_error(resp); return; }
    auto unread = conn->query_s<private_message_t>(
        "sender_id = ? AND receiver_id = ? AND is_read = 0", body.sender_id, user_id);
    for (auto &m : unread) { m.is_read = 1; conn->update(m); }
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
    if (!user_has_pm_privilege(user_id)) {
      resp.set_status_and_content(status_type::forbidden, make_error("需要私信特权", 403));
      return;
    }
    auto conn = get_db_pool().get();
    if (!conn) { set_server_internel_error(resp); return; }
    auto unread = conn->query_s<private_message_t>(
        "receiver_id = ? AND is_read = 0 AND deleted_by_receiver = 0", user_id);
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
    std::string err;
    iguana::from_json(body, req.get_body(), err);
    if (!err.empty() || body.target_user_id == 0 || body.target_user_id == user_id) {
      resp.set_status_and_content(status_type::bad_request, make_error("参数错误"));
      return;
    }
    auto conn = get_db_pool().get();
    if (!conn) { set_server_internel_error(resp); return; }
    // 已拉黑则幂等返回成功
    auto existing = conn->query_s<pm_blocklist_t>(
        "user_id = ? AND blocked_user_id = ?", user_id, body.target_user_id);
    if (!existing.empty()) {
      resp.set_status_and_content(status_type::ok, make_success("已在黑名单中"));
      return;
    }
    pm_blocklist_t bl{};
    bl.user_id = user_id;
    bl.blocked_user_id = body.target_user_id;
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
    auto target_str = req.get_path_param("target_id");
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
    auto rows = conn->query_s<pm_blocklist_t>(
        "user_id = ? AND blocked_user_id = ?", user_id, target_id);
    for (auto &bl : rows) conn->delete_records<pm_blocklist_t>("id = ?", bl.id);
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
    auto rows = conn->query_s<pm_blocklist_t>("user_id = ?", user_id);

    struct blocked_user_view { uint64_t user_id = 0; std::string user_name; uint64_t created_at = 0; };
    YLT_REFL(blocked_user_view, user_id, user_name, created_at);

    std::vector<blocked_user_view> result;
    for (auto &bl : rows) {
      auto users = conn->query_s<users_t>("id = ?", bl.blocked_user_id);
      blocked_user_view v{};
      v.user_id = bl.blocked_user_id;
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
  bool ok1 = conn->create_datatable<private_message_t>(
      ormpp_auto_key{"id"},
      ormpp_not_null{{"sender_id", "receiver_id", "content", "is_read",
                      "deleted_by_sender", "deleted_by_receiver", "created_at"}});
  bool ok2 = conn->create_datatable<pm_blocklist_t>(
      ormpp_auto_key{"id"},
      ormpp_not_null{{"user_id", "blocked_user_id", "created_at"}});
  if (!ok1) CINATRA_LOG_ERROR << "Table 'private_messages' create error.";
  if (!ok2) CINATRA_LOG_ERROR << "Table 'pm_blocklist' create error.";
  return ok1 && ok2;
}

} // namespace purecpp
