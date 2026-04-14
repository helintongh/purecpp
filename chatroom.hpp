#pragma once

#include <algorithm>
#include <atomic>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <sstream>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

#include "common.hpp"
#include "entity.hpp"
#include "jwt_token.hpp"

using namespace cinatra;
using namespace ormpp;

namespace purecpp {

// ==================== DB Entity (flat structs for ormpp) ====================

struct chat_channel_t {
  uint64_t id;
  std::array<char, 64> name;
  std::array<char, 256> topic;
  uint64_t creator_id;
  uint32_t is_private; // 0=public, 1=private
  uint64_t created_at;
  uint64_t message_count;

  static constexpr std::string_view get_alias_struct_name(chat_channel_t *) {
    return "chat_channels";
  }
};
REGISTER_AUTO_KEY(chat_channel_t, id);

struct chat_message_t {
  uint64_t id;
  uint64_t channel_id;
  uint64_t user_id;
  std::array<char, 64> user_name;
  std::string content;
  uint64_t created_at;
  uint64_t channel_seq;

  static constexpr std::string_view get_alias_struct_name(chat_message_t *) {
    return "chat_messages";
  }
};
REGISTER_AUTO_KEY(chat_message_t, id);

struct chat_reaction_t {
  uint64_t id;
  uint64_t message_id;
  uint64_t user_id;
  std::array<char, 16> emoji;
  uint64_t created_at;

  static constexpr std::string_view get_alias_struct_name(chat_reaction_t *) {
    return "chat_reactions";
  }
};
REGISTER_AUTO_KEY(chat_reaction_t, id);

struct chat_read_position_t {
  uint64_t id;
  uint64_t user_id;
  uint64_t channel_id;
  uint64_t last_read_message_id;
  uint64_t last_read_channel_seq;
  uint64_t updated_at;

  static constexpr std::string_view get_alias_struct_name(
      chat_read_position_t *) {
    return "chat_read_positions";
  }
};
REGISTER_AUTO_KEY(chat_read_position_t, id);

struct chat_channel_member_t {
  uint64_t id;
  uint64_t channel_id;
  uint64_t user_id;
  uint64_t joined_at;

  static constexpr std::string_view get_alias_struct_name(
      chat_channel_member_t *) {
    return "chat_channel_members";
  }
};
REGISTER_AUTO_KEY(chat_channel_member_t, id);

struct chat_mark_read_req {
  uint64_t channel_id;
  uint64_t last_message_id;
};

struct chat_create_channel_req {
  std::string name;
  std::string topic;
  uint32_t is_private;
};

struct chat_delete_channel_req {
  uint64_t channel_id = 0;
};

struct chat_upload_req {
  std::string filename;
  std::string file_data; // base64 encoded
};

struct chat_ws_in {
  std::string type;
  uint64_t channel_id;
  std::string text;
  uint64_t message_id;
  std::string emoji;
};

// ==================== Helpers ====================

inline std::string arr_to_str(const auto &arr) {
  return std::string(arr.data(),
                     strnlen(arr.data(), arr.size()));
}

inline void str_to_arr(auto &arr, const std::string &s) {
  std::memset(arr.data(), 0, arr.size());
  std::strncpy(arr.data(), s.c_str(), arr.size() - 1);
}

inline std::tm chat_localtime(std::time_t t) {
  std::tm result{};
#ifdef _WIN32
  localtime_s(&result, &t);
#else
  localtime_r(&t, &result);
#endif
  return result;
}

inline std::string chat_format_time(uint64_t ts_ms) {
  auto tp = std::chrono::system_clock::time_point(
      std::chrono::milliseconds(ts_ms));
  auto tt = std::chrono::system_clock::to_time_t(tp);
  auto tm = chat_localtime(tt);
  auto now_t = std::chrono::system_clock::to_time_t(
      std::chrono::system_clock::now());
  auto now_tm = chat_localtime(now_t);
  char buf[32];
  if (tm.tm_year == now_tm.tm_year && tm.tm_yday == now_tm.tm_yday) {
    std::strftime(buf, sizeof(buf), "%H:%M", &tm);
  } else {
    std::strftime(buf, sizeof(buf), "%m-%d %H:%M", &tm);
  }
  return buf;
}

inline std::string escape_sql_like(std::string_view text) {
  std::string escaped;
  escaped.reserve(text.size() * 2);
  for (char ch : text) {
    if (ch == '\\' || ch == '%' || ch == '_') {
      escaped.push_back('\\');
    }
    escaped.push_back(ch);
  }
  return escaped;
}

inline std::string url_decode(const std::string &s) {
  std::string r;
  r.reserve(s.size());
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '%' && i + 2 < s.size()) {
      auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
      };
      int hi = hex(s[i + 1]), lo = hex(s[i + 2]);
      if (hi >= 0 && lo >= 0) {
        r += static_cast<char>(hi * 16 + lo);
        i += 2;
        continue;
      }
    }
    r += s[i];
  }
  return r;
}

inline std::string json_escape(std::string_view input) {
  std::string escaped;
  escaped.reserve(input.size());
  for (unsigned char ch : input) {
    switch (ch) {
    case '\\':
      escaped += R"(\\)";
      break;
    case '"':
      escaped += R"(\")";
      break;
    case '\b':
      escaped += R"(\b)";
      break;
    case '\f':
      escaped += R"(\f)";
      break;
    case '\n':
      escaped += R"(\n)";
      break;
    case '\r':
      escaped += R"(\r)";
      break;
    case '\t':
      escaped += R"(\t)";
      break;
    default:
      if (ch < 0x20) {
        constexpr char hex[] = "0123456789abcdef";
        escaped += "\\u00";
        escaped += hex[(ch >> 4) & 0x0F];
        escaped += hex[ch & 0x0F];
      } else {
        escaped += static_cast<char>(ch);
      }
      break;
    }
  }
  return escaped;
}

// ==================== JSON DTO helpers ====================

struct chat_reaction_view {
  std::string emoji;
  int count = 0;
  std::vector<uint64_t> users;
};
YLT_REFL(chat_reaction_view, emoji, count, users);

struct chat_message_view {
  uint64_t id = 0;
  uint64_t channel_id = 0;
  uint64_t user_id = 0;
  std::string user_name;
  std::string text;
  std::string time;
  std::vector<chat_reaction_view> reactions;
};
YLT_REFL(chat_message_view, id, channel_id, user_id, user_name, text, time,
         reactions);

struct chat_online_user_view {
  uint64_t id = 0;
  std::string name;
};
YLT_REFL(chat_online_user_view, id, name);

struct chat_channel_view {
  uint64_t id = 0;
  std::string name;
  std::string topic;
  uint32_t is_private = 0;
  uint64_t creator_id = 0;
  uint64_t created_at = 0;
  uint64_t unread = 0;
  bool joined = true;
};
YLT_REFL(chat_channel_view, id, name, topic, is_private, creator_id,
         created_at, unread, joined);

struct chat_upload_resp {
  std::string url;
  std::string filename;
};
YLT_REFL(chat_upload_resp, url, filename);

struct chat_ws_hello {
  std::string type;
  chat_online_user_view user;
  std::vector<chat_online_user_view> online;
};
YLT_REFL(chat_ws_hello, type, user, online);

struct chat_ws_presence_join {
  std::string type;
  chat_online_user_view user;
};
YLT_REFL(chat_ws_presence_join, type, user);

struct chat_ws_presence_leave {
  std::string type;
  uint64_t user_id = 0;
};
YLT_REFL(chat_ws_presence_leave, type, user_id);

struct chat_ws_online_update {
  std::string type;
  std::vector<chat_online_user_view> online;
};
YLT_REFL(chat_ws_online_update, type, online);

struct chat_ws_message {
  std::string type;
  chat_message_view msg;
};
YLT_REFL(chat_ws_message, type, msg);

struct chat_ws_channel_activity {
  std::string type;
  uint64_t channel_id = 0;
  uint64_t unread_delta = 0;
};
YLT_REFL(chat_ws_channel_activity, type, channel_id, unread_delta);

struct chat_ws_reaction_update {
  std::string type;
  uint64_t message_id = 0;
  uint64_t channel_id = 0;
  std::vector<chat_reaction_view> reactions;
};
YLT_REFL(chat_ws_reaction_update, type, message_id, channel_id, reactions);

struct chat_ws_message_edited {
  std::string type;
  uint64_t message_id = 0;
  uint64_t channel_id = 0;
  std::string text;
  uint64_t edited_at = 0;
};
YLT_REFL(chat_ws_message_edited, type, message_id, channel_id, text,
         edited_at);

struct chat_ws_message_deleted {
  std::string type;
  uint64_t message_id = 0;
  uint64_t channel_id = 0;
};
YLT_REFL(chat_ws_message_deleted, type, message_id, channel_id);

struct chat_ws_typing {
  std::string type;
  uint64_t user_id = 0;
  std::string user_name;
  uint64_t channel_id = 0;
};
YLT_REFL(chat_ws_typing, type, user_id, user_name, channel_id);

template <typename T>
inline std::string to_json_string(const T &value) {
  std::string json;
  try {
    iguana::to_json(value, json);
  } catch (const std::exception &e) {
    CINATRA_LOG_ERROR << "iguana::to_json failed: " << e.what();
  }
  return json;
}

inline std::vector<chat_reaction_view>
build_reaction_views_from(const std::vector<chat_reaction_t> &rs) {
  struct reaction_group {
    int count = 0;
    std::vector<uint64_t> users;
  };

  std::map<std::string, reaction_group> grouped;
  for (auto &r : rs) {
    auto &group = grouped[arr_to_str(r.emoji)];
    ++group.count;
    group.users.push_back(r.user_id);
  }

  std::vector<chat_reaction_view> result;
  result.reserve(grouped.size());
  for (auto &[emoji, group] : grouped) {
    result.push_back({emoji, group.count, std::move(group.users)});
  }
  return result;
}

inline std::vector<chat_reaction_view> build_reaction_views(uint64_t msg_id) {
  auto conn = get_db_pool().get();
  if (!conn) return {};
  auto rs = conn->query_s<chat_reaction_t>("message_id = ?", msg_id);
  return build_reaction_views_from(rs);
}

inline std::unordered_map<uint64_t, std::vector<chat_reaction_view>>
batch_build_reactions(const std::vector<chat_message_t> &msgs) {
  std::unordered_map<uint64_t, std::vector<chat_reaction_view>> result;
  if (msgs.empty()) return result;

  auto conn = get_db_pool().get();
  if (!conn) {
    for (auto &m : msgs) result[m.id] = {};
    return result;
  }

  std::ostringstream id_list;
  id_list << "message_id IN (";
  for (size_t i = 0; i < msgs.size(); i++) {
    if (i) id_list << ",";
    id_list << msgs[i].id;
  }
  id_list << ")";

  auto all_reactions = conn->query<chat_reaction_t>(id_list.str());
  std::unordered_map<uint64_t, std::vector<chat_reaction_t>> grouped;
  for (auto &r : all_reactions) {
    grouped[r.message_id].push_back(r);
  }

  for (auto &m : msgs) {
    auto it = grouped.find(m.id);
    if (it != grouped.end()) {
      result[m.id] = build_reaction_views_from(it->second);
    } else {
      result[m.id] = {};
    }
  }
  return result;
}

inline chat_message_view
make_chat_message_view(const chat_message_t &m,
                       std::vector<chat_reaction_view> reactions = {}) {
  return {m.id,
          m.channel_id,
          m.user_id,
          arr_to_str(m.user_name),
          m.content,
          chat_format_time(m.created_at),
          std::move(reactions)};
}

inline std::vector<chat_online_user_view> make_online_user_views(
    const std::vector<std::pair<uint64_t, std::string>> &users) {
  std::vector<chat_online_user_view> result;
  result.reserve(users.size());
  for (auto &[id, name] : users) {
    result.push_back({id, name});
  }
  return result;
}

inline chat_channel_view make_chat_channel_view(const chat_channel_t &ch,
                                                uint64_t unread) {
  return {ch.id,        arr_to_str(ch.name),  arr_to_str(ch.topic),
          ch.is_private, ch.creator_id,        ch.created_at,
          unread,       true};
}

inline std::string
build_msg_json(const chat_message_t &m,
               std::vector<chat_reaction_view> reactions = {}) {
  return to_json_string(make_chat_message_view(m, std::move(reactions)));
}

inline std::string build_online_json(
    const std::vector<std::pair<uint64_t, std::string>> &users) {
  return to_json_string(make_online_user_views(users));
}

inline std::string build_reactions_json(uint64_t message_id) {
  return to_json_string(build_reaction_views(message_id));
}

class chat_perf_stats {
public:
  static chat_perf_stats &instance() {
    static chat_perf_stats stats;
    return stats;
  }

  static uint64_t now_ns() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
  }

  void record_message(uint64_t total_ns, uint64_t db_ns,
                      uint64_t json_build_ns,
                      uint64_t active_broadcast_ns,
                      uint64_t inactive_broadcast_ns) {
    handled_messages_.fetch_add(1, std::memory_order_relaxed);
    message_total_ns_.fetch_add(total_ns, std::memory_order_relaxed);
    message_db_ns_.fetch_add(db_ns, std::memory_order_relaxed);
    message_json_build_ns_.fetch_add(json_build_ns, std::memory_order_relaxed);
    message_active_broadcast_ns_.fetch_add(active_broadcast_ns,
                                           std::memory_order_relaxed);
    message_inactive_broadcast_ns_.fetch_add(inactive_broadcast_ns,
                                             std::memory_order_relaxed);
    maybe_log_summary();
  }

  void record_enqueue(uint64_t enqueue_ns, size_t queue_size,
                      size_t queued_bytes) {
    enqueue_calls_.fetch_add(1, std::memory_order_relaxed);
    enqueue_total_ns_.fetch_add(enqueue_ns, std::memory_order_relaxed);
    update_max(max_queue_size_, queue_size);
    update_max(max_queue_bytes_, queued_bytes);
  }

  void record_ws_write(uint64_t write_ns, bool success) {
    ws_write_frames_.fetch_add(1, std::memory_order_relaxed);
    ws_write_total_ns_.fetch_add(write_ns, std::memory_order_relaxed);
    if (!success) {
      ws_write_failures_.fetch_add(1, std::memory_order_relaxed);
    }
  }

private:
  static constexpr uint64_t log_every_messages_ = 200;

  static void update_max(std::atomic<uint64_t> &slot, uint64_t candidate) {
    auto current = slot.load(std::memory_order_relaxed);
    while (candidate > current &&
           !slot.compare_exchange_weak(current, candidate,
                                       std::memory_order_relaxed,
                                       std::memory_order_relaxed)) {
    }
  }

  static double avg_ms(uint64_t total_ns, uint64_t count) {
    if (count == 0) return 0.0;
    return static_cast<double>(total_ns) / static_cast<double>(count) / 1e6;
  }

  void maybe_log_summary() {
    const auto handled = handled_messages_.load(std::memory_order_relaxed);
    if (handled < log_every_messages_) return;

    const auto bucket = handled / log_every_messages_;
    auto last = last_logged_bucket_.load(std::memory_order_relaxed);
    while (bucket > last) {
      if (last_logged_bucket_.compare_exchange_weak(
              last, bucket, std::memory_order_relaxed,
              std::memory_order_relaxed)) {
        const auto enqueue_calls =
            enqueue_calls_.load(std::memory_order_relaxed);
        const auto ws_frames = ws_write_frames_.load(std::memory_order_relaxed);
        CINATRA_LOG_INFO
            << "chat_perf messages=" << handled
            << " avg_total_ms="
            << avg_ms(message_total_ns_.load(std::memory_order_relaxed),
                      handled)
            << " avg_db_ms="
            << avg_ms(message_db_ns_.load(std::memory_order_relaxed), handled)
            << " avg_json_build_ms="
            << avg_ms(message_json_build_ns_.load(std::memory_order_relaxed),
                      handled)
            << " avg_broadcast_active_ms="
            << avg_ms(message_active_broadcast_ns_.load(std::memory_order_relaxed),
                      handled)
            << " avg_broadcast_inactive_ms="
            << avg_ms(
                   message_inactive_broadcast_ns_.load(
                       std::memory_order_relaxed),
                   handled)
            << " avg_enqueue_ms="
            << avg_ms(enqueue_total_ns_.load(std::memory_order_relaxed),
                      enqueue_calls)
            << " avg_ws_write_ms="
            << avg_ms(ws_write_total_ns_.load(std::memory_order_relaxed),
                      ws_frames)
            << " max_queue_size="
            << max_queue_size_.load(std::memory_order_relaxed)
            << " max_queue_bytes="
            << max_queue_bytes_.load(std::memory_order_relaxed)
            << " ws_write_failures="
            << ws_write_failures_.load(std::memory_order_relaxed);
        break;
      }
    }
  }

  std::atomic<uint64_t> handled_messages_{0};
  std::atomic<uint64_t> message_total_ns_{0};
  std::atomic<uint64_t> message_db_ns_{0};
  std::atomic<uint64_t> message_json_build_ns_{0};
  std::atomic<uint64_t> message_active_broadcast_ns_{0};
  std::atomic<uint64_t> message_inactive_broadcast_ns_{0};
  std::atomic<uint64_t> enqueue_calls_{0};
  std::atomic<uint64_t> enqueue_total_ns_{0};
  std::atomic<uint64_t> ws_write_frames_{0};
  std::atomic<uint64_t> ws_write_total_ns_{0};
  std::atomic<uint64_t> ws_write_failures_{0};
  std::atomic<uint64_t> max_queue_size_{0};
  std::atomic<uint64_t> max_queue_bytes_{0};
  std::atomic<uint64_t> last_logged_bucket_{0};
};

struct chat_channel_cache_entry {
  bool exists = false;
  uint64_t creator_id = 0;
  uint32_t is_private = 0;
  uint64_t message_count = 0;
  uint64_t expires_at = 0;
};

class chat_channel_cache {
public:
  static chat_channel_cache &instance() {
    static chat_channel_cache cache;
    return cache;
  }

  bool get(uint64_t channel_id, chat_channel_cache_entry &out,
           bool force_refresh = false) {
    if (channel_id == 0) return false;

    const auto now = get_timestamp_milliseconds();
    {
      std::shared_lock lk(mtx_);
      auto it = entries_.find(channel_id);
      if (!force_refresh && it != entries_.end() && it->second.expires_at > now) {
        out = it->second;
        return out.exists;
      }
    }

    auto conn = get_db_pool().get();
    if (!conn) return false;

    chat_channel_cache_entry entry{};
    entry.expires_at = now + ttl_ms_;
    auto rows = conn->query_s<chat_channel_t>("id = ?", channel_id);
    if (!rows.empty()) {
      entry.exists = true;
      entry.creator_id = rows[0].creator_id;
      entry.is_private = rows[0].is_private;
      entry.message_count = rows[0].message_count;
    }

    {
      std::unique_lock lk(mtx_);
      entries_[channel_id] = entry;
      auto &channel_mtx = channel_mutexes_[channel_id];
      if (!channel_mtx) channel_mtx = std::make_shared<std::mutex>();
    }

    out = entry;
    return entry.exists;
  }

  void put(const chat_channel_t &channel) {
    std::unique_lock lk(mtx_);
    auto &entry = entries_[channel.id];
    entry.exists = true;
    entry.creator_id = channel.creator_id;
    entry.is_private = channel.is_private;
    entry.message_count = channel.message_count;
    entry.expires_at = get_timestamp_milliseconds() + ttl_ms_;
    auto &channel_mtx = channel_mutexes_[channel.id];
    if (!channel_mtx) channel_mtx = std::make_shared<std::mutex>();
  }

  void set_message_count(uint64_t channel_id, uint64_t message_count) {
    std::unique_lock lk(mtx_);
    auto &entry = entries_[channel_id];
    entry.exists = true;
    entry.message_count = message_count;
    entry.expires_at = get_timestamp_milliseconds() + ttl_ms_;
    auto &channel_mtx = channel_mutexes_[channel_id];
    if (!channel_mtx) channel_mtx = std::make_shared<std::mutex>();
  }

  void decrement_message_count(uint64_t channel_id) {
    std::unique_lock lk(mtx_);
    auto it = entries_.find(channel_id);
    if (it != entries_.end() && it->second.message_count > 0) {
      it->second.message_count--;
    }
  }

  void erase(uint64_t channel_id) {
    std::unique_lock lk(mtx_);
    entries_.erase(channel_id);
    channel_mutexes_.erase(channel_id);
  }

  std::shared_ptr<std::mutex> channel_mutex(uint64_t channel_id) {
    {
      std::shared_lock lk(mtx_);
      auto it = channel_mutexes_.find(channel_id);
      if (it != channel_mutexes_.end()) return it->second;
    }
    std::unique_lock lk(mtx_);
    auto &channel_mtx = channel_mutexes_[channel_id];
    if (!channel_mtx) channel_mtx = std::make_shared<std::mutex>();
    return channel_mtx;
  }

private:
  static constexpr uint64_t ttl_ms_ = 30 * 1000;

  std::shared_mutex mtx_;
  std::unordered_map<uint64_t, chat_channel_cache_entry> entries_;
  std::unordered_map<uint64_t, std::shared_ptr<std::mutex>> channel_mutexes_;
};

struct chat_access_cache_key {
  uint64_t user_id = 0;
  uint64_t channel_id = 0;

  bool operator==(const chat_access_cache_key &other) const {
    return user_id == other.user_id && channel_id == other.channel_id;
  }
};

struct chat_access_cache_key_hash {
  size_t operator()(const chat_access_cache_key &key) const {
    auto h1 = std::hash<uint64_t>{}(key.user_id);
    auto h2 = std::hash<uint64_t>{}(key.channel_id);
    return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
  }
};

struct chat_access_cache_entry {
  bool allowed = false;
  uint64_t expires_at = 0;
};

class chat_access_cache {
public:
  static chat_access_cache &instance() {
    static chat_access_cache cache;
    return cache;
  }

  bool get(uint64_t user_id, uint64_t channel_id, bool &allowed) {
    {
      std::shared_lock lk(mtx_);
      auto it = entries_.find({user_id, channel_id});
      if (it == entries_.end()) return false;
      if (it->second.expires_at <= get_timestamp_milliseconds()) {
        // expired — need write lock to erase
      } else {
        allowed = it->second.allowed;
        return true;
      }
    }
    // Erase expired entry under write lock
    std::unique_lock lk(mtx_);
    auto it = entries_.find({user_id, channel_id});
    if (it != entries_.end() && it->second.expires_at <= get_timestamp_milliseconds()) {
      entries_.erase(it);
    }
    return false;
  }

  void put(uint64_t user_id, uint64_t channel_id, bool allowed) {
    std::unique_lock lk(mtx_);
    entries_[{user_id, channel_id}] = {
        allowed, get_timestamp_milliseconds() + ttl_ms_};
  }

private:
  static constexpr uint64_t ttl_ms_ = 30 * 1000;

  std::shared_mutex mtx_;
  std::unordered_map<chat_access_cache_key, chat_access_cache_entry,
                     chat_access_cache_key_hash>
      entries_;
};

// ==================== init_chat_db ====================

inline bool init_chat_db() {
  try {
    auto &pool = get_db_pool();
    auto conn = pool.get();
    if (!conn) {
      CINATRA_LOG_ERROR << "Failed to get database connection";
      return false;
    }

#if defined(PURECPP_DB_SQLITE)
    auto table_exists = [&](std::string_view table_name) {
      auto rows = conn->query_s<std::tuple<int>>(
          "SELECT 1 FROM sqlite_master WHERE type='table' AND name=? LIMIT 1",
          std::string(table_name));
      return !rows.empty();
    };

    auto column_exists = [&](std::string_view table_name,
                             std::string_view column_name) {
      std::string sql = "SELECT 1 FROM pragma_table_info('" +
                        std::string(table_name) +
                        "') WHERE name=? LIMIT 1";
      auto rows = conn->query_s<std::tuple<int>>(sql, std::string(column_name));
      return !rows.empty();
    };

    auto index_exists = [&](std::string_view, std::string_view index_name) {
      auto rows = conn->query_s<std::tuple<int>>(
          "SELECT 1 FROM sqlite_master WHERE type='index' AND name=? LIMIT 1",
          std::string(index_name));
      return !rows.empty();
    };
#elif defined(PURECPP_DB_MYSQL)
    auto table_exists = [&](std::string_view table_name) {
      auto rows = conn->query_s<std::tuple<int>>(
          "SELECT 1 FROM information_schema.tables "
          "WHERE table_schema = DATABASE() AND table_name = ? LIMIT 1",
          std::string(table_name));
      return !rows.empty();
    };

    auto column_exists = [&](std::string_view table_name,
                             std::string_view column_name) {
      auto rows = conn->query_s<std::tuple<int>>(
          "SELECT 1 FROM information_schema.columns "
          "WHERE table_schema = DATABASE() AND table_name = ? "
          "AND column_name = ? LIMIT 1",
          std::string(table_name), std::string(column_name));
      return !rows.empty();
    };

    auto index_exists = [&](std::string_view table_name,
                            std::string_view index_name) {
      auto rows = conn->query_s<std::tuple<int>>(
          "SELECT 1 FROM information_schema.statistics "
          "WHERE table_schema = DATABASE() AND table_name = ? "
          "AND index_name = ? LIMIT 1",
          std::string(table_name), std::string(index_name));
      return !rows.empty();
    };
#else
    auto table_exists = [&](std::string_view) { return true; };
    auto column_exists = [&](std::string_view, std::string_view) {
      return true;
    };
    auto index_exists = [&](std::string_view, std::string_view) {
      return true;
    };
#endif

    auto exec_or_throw = [&](const std::string &sql) {
      if (!conn->execute(sql)) {
        throw std::runtime_error("SQL failed: " + sql +
                                 ", error: " + conn->get_last_error());
      }
    };

    auto ensure_index = [&](std::string_view table_name,
                            std::string_view index_name,
                            const std::string &sql) {
      if (!index_exists(table_name, index_name)) {
        exec_or_throw(sql);
      }
    };

    auto migrate_legacy_table = [&](std::string_view legacy_name,
                                    const std::string &merge_sql,
                                    std::string_view new_name) {
      if (!table_exists(legacy_name)) {
        return;
      }

      exec_or_throw(merge_sql);
      exec_or_throw("DROP TABLE IF EXISTS " + std::string(legacy_name));
      CINATRA_LOG_INFO << "Migrated legacy chat table " << legacy_name << " -> "
                       << new_name;
    };

#if defined(PURECPP_DB_SQLITE)
    if (!table_exists("chat_channels")) {
      exec_or_throw(
          "CREATE TABLE IF NOT EXISTS chat_channels("
          "id INTEGER PRIMARY KEY AUTOINCREMENT,"
          "name varchar(64),"
          "topic varchar(256),"
          "creator_id INTEGER DEFAULT 0,"
          "is_private INTEGER DEFAULT 0,"
          "created_at INTEGER,"
          "message_count INTEGER DEFAULT 0,"
          "UNIQUE (name))");
    }

    if (!table_exists("chat_messages")) {
      exec_or_throw(
          "CREATE TABLE IF NOT EXISTS chat_messages("
          "id INTEGER PRIMARY KEY AUTOINCREMENT,"
          "channel_id INTEGER,"
          "user_id INTEGER,"
          "user_name varchar(64),"
          "content TEXT,"
          "created_at INTEGER,"
          "channel_seq INTEGER DEFAULT 0)");
    }

    if (!table_exists("chat_reactions")) {
      exec_or_throw(
          "CREATE TABLE IF NOT EXISTS chat_reactions("
          "id INTEGER PRIMARY KEY AUTOINCREMENT,"
          "message_id INTEGER,"
          "user_id INTEGER,"
          "emoji varchar(16),"
          "created_at INTEGER)");
    }

    if (!table_exists("chat_read_positions")) {
      exec_or_throw(
          "CREATE TABLE IF NOT EXISTS chat_read_positions("
          "id INTEGER PRIMARY KEY AUTOINCREMENT,"
          "user_id INTEGER,"
          "channel_id INTEGER,"
          "last_read_message_id INTEGER,"
          "last_read_channel_seq INTEGER DEFAULT 0,"
          "updated_at INTEGER)");
    }

    if (!table_exists("chat_channel_members")) {
      exec_or_throw(
          "CREATE TABLE IF NOT EXISTS chat_channel_members("
          "id INTEGER PRIMARY KEY AUTOINCREMENT,"
          "channel_id INTEGER,"
          "user_id INTEGER,"
          "joined_at INTEGER)");
    }

    if (!column_exists("chat_channels", "creator_id")) {
      exec_or_throw(
          "ALTER TABLE chat_channels ADD COLUMN creator_id INTEGER DEFAULT 0");
    }

    if (!column_exists("chat_channels", "is_private")) {
      exec_or_throw(
          "ALTER TABLE chat_channels ADD COLUMN is_private INTEGER DEFAULT 0");
    }

    if (!column_exists("chat_channels", "message_count")) {
      exec_or_throw(
          "ALTER TABLE chat_channels ADD COLUMN message_count INTEGER DEFAULT 0");
    }

    if (!column_exists("chat_messages", "channel_seq")) {
      exec_or_throw(
          "ALTER TABLE chat_messages ADD COLUMN channel_seq INTEGER DEFAULT 0");
    }

    if (!column_exists("chat_read_positions", "last_read_channel_seq")) {
      exec_or_throw(
          "ALTER TABLE chat_read_positions ADD COLUMN last_read_channel_seq INTEGER DEFAULT 0");
    }

    migrate_legacy_table(
        "chat_channel_t",
        "INSERT OR IGNORE INTO chat_channels("
        "id, name, topic, creator_id, is_private, created_at, message_count) "
        "SELECT id, name, topic, 0, 0, created_at, 0 FROM chat_channel_t",
        "chat_channels");
    migrate_legacy_table(
        "chat_message_t",
        "INSERT OR IGNORE INTO chat_messages("
        "id, channel_id, user_id, user_name, content, created_at, channel_seq) "
        "SELECT id, channel_id, user_id, user_name, content, created_at, 0 "
        "FROM chat_message_t",
        "chat_messages");
    migrate_legacy_table(
        "chat_reaction_t",
        "INSERT OR IGNORE INTO chat_reactions("
        "id, message_id, user_id, emoji, created_at) "
        "SELECT id, message_id, user_id, emoji, created_at "
        "FROM chat_reaction_t",
        "chat_reactions");
    migrate_legacy_table(
        "chat_read_position_t",
        "INSERT OR IGNORE INTO chat_read_positions("
        "id, user_id, channel_id, last_read_message_id, last_read_channel_seq, updated_at) "
        "SELECT id, user_id, channel_id, last_read_message_id, 0, updated_at "
        "FROM chat_read_position_t",
        "chat_read_positions");
    migrate_legacy_table(
        "chat_channel_member_t",
        "INSERT OR IGNORE INTO chat_channel_members("
        "id, channel_id, user_id, joined_at) "
        "SELECT id, channel_id, user_id, joined_at "
        "FROM chat_channel_member_t",
        "chat_channel_members");

    exec_or_throw("DROP TABLE IF EXISTS chat_users");
    exec_or_throw("DROP TABLE IF EXISTS chat_user_t");
#else
    if (!conn->create_datatable<chat_channel_t>(ormpp_auto_key{"id"},
                                                ormpp_unique{{"name"}})) {
      throw std::runtime_error(conn->get_last_error());
    }
    if (!conn->create_datatable<chat_message_t>(ormpp_auto_key{"id"})) {
      throw std::runtime_error(conn->get_last_error());
    }
    if (!conn->create_datatable<chat_reaction_t>(ormpp_auto_key{"id"})) {
      throw std::runtime_error(conn->get_last_error());
    }
    if (!conn->create_datatable<chat_read_position_t>(ormpp_auto_key{"id"})) {
      throw std::runtime_error(conn->get_last_error());
    }
    if (!conn->create_datatable<chat_channel_member_t>(ormpp_auto_key{"id"})) {
      throw std::runtime_error(conn->get_last_error());
    }

    if (!column_exists("chat_channels", "creator_id")) {
      exec_or_throw(
          "ALTER TABLE chat_channels ADD COLUMN creator_id BIGINT DEFAULT 0");
    }
    if (!column_exists("chat_channels", "is_private")) {
      exec_or_throw(
          "ALTER TABLE chat_channels ADD COLUMN is_private INT DEFAULT 0");
    }
    if (!column_exists("chat_channels", "message_count")) {
      exec_or_throw(
          "ALTER TABLE chat_channels ADD COLUMN message_count BIGINT DEFAULT 0");
    }
    if (!column_exists("chat_messages", "channel_seq")) {
      exec_or_throw(
          "ALTER TABLE chat_messages ADD COLUMN channel_seq BIGINT DEFAULT 0");
    }
    if (!column_exists("chat_read_positions", "last_read_channel_seq")) {
      exec_or_throw(
          "ALTER TABLE chat_read_positions ADD COLUMN last_read_channel_seq BIGINT DEFAULT 0");
    }
#endif

    // Create indexes for query performance
    ensure_index("chat_messages", "idx_chat_messages_channel_created",
                 "CREATE INDEX idx_chat_messages_channel_created "
                 "ON chat_messages(channel_id, created_at)");
    ensure_index("chat_messages", "idx_chat_messages_channel_id",
                 "CREATE INDEX idx_chat_messages_channel_id "
                 "ON chat_messages(channel_id, id)");
    ensure_index("chat_messages", "idx_chat_messages_channel_seq",
                 "CREATE INDEX idx_chat_messages_channel_seq "
                 "ON chat_messages(channel_id, channel_seq)");
    ensure_index("chat_reactions", "idx_chat_reactions_message",
                 "CREATE INDEX idx_chat_reactions_message "
                 "ON chat_reactions(message_id)");
    ensure_index("chat_read_positions", "idx_chat_read_positions_user_channel",
                 "CREATE INDEX idx_chat_read_positions_user_channel "
                 "ON chat_read_positions(user_id, channel_id)");
    ensure_index("chat_channel_members", "idx_chat_channel_members_channel_user",
                 "CREATE INDEX idx_chat_channel_members_channel_user "
                 "ON chat_channel_members(channel_id, user_id)");

#if defined(PURECPP_DB_MYSQL)
    if (!index_exists("chat_messages", "ft_chat_messages_content")) {
      exec_or_throw(
          "CREATE FULLTEXT INDEX ft_chat_messages_content "
          "ON chat_messages(content)");
    }
#endif

    // Seed channels
    auto channels = conn->query<chat_channel_t>();
    if (channels.empty()) {
      const char *seeds[][2] = {
        {"通用聊天", "欢迎来到 PureCpp 社区聊天室！"},
        {"项目讨论", "讨论项目相关事宜"},
        {"技术问题", "技术问题求助与解答"},
      };
      for (auto &s : seeds) {
        chat_channel_t ch{};
        str_to_arr(ch.name, s[0]);
        str_to_arr(ch.topic, s[1]);
        ch.created_at = get_timestamp_milliseconds();
        if (conn->insert(ch) <= 0) {
          throw std::runtime_error("Failed to seed chat channel: " +
                                   conn->get_last_error());
        }
      }
      CINATRA_LOG_INFO << "Chat channels seeded";
    }

    // Check if backfill is needed (any message with channel_seq = 0)
    auto need_backfill = conn->query<chat_message_t>("channel_seq = 0 LIMIT 1");
    std::unordered_map<uint64_t, uint64_t> message_seq_by_id;
    auto all_channels = conn->query<chat_channel_t>();

    if (!need_backfill.empty()) {
      CINATRA_LOG_INFO << "Backfilling channel_seq for messages...";
      for (auto &channel : all_channels) {
        auto msgs = conn->query<chat_message_t>(
            "channel_id=" + std::to_string(channel.id) +
            " ORDER BY created_at ASC, id ASC");
        uint64_t seq = 0;
        for (auto &msg : msgs) {
          ++seq;
          if (msg.channel_seq != seq) {
            msg.channel_seq = seq;
            if (conn->update_some<&chat_message_t::channel_seq>(
                    msg, "id=" + std::to_string(msg.id)) != 1) {
              throw std::runtime_error("Failed to backfill chat_message.channel_seq");
            }
          }
          message_seq_by_id[msg.id] = seq;
        }

        if (channel.message_count != seq) {
          channel.message_count = seq;
          if (conn->update_some<&chat_channel_t::message_count>(
                  channel, "id=" + std::to_string(channel.id)) != 1) {
            throw std::runtime_error("Failed to backfill chat_channel.message_count");
          }
        }
        chat_channel_cache::instance().put(channel);
      }

      auto read_positions = conn->query<chat_read_position_t>();
      for (auto &position : read_positions) {
        uint64_t seq = 0;
        auto it = message_seq_by_id.find(position.last_read_message_id);
        if (it != message_seq_by_id.end()) {
          seq = it->second;
        }

        if (position.last_read_channel_seq != seq) {
          position.last_read_channel_seq = seq;
          if (conn->update_some<&chat_read_position_t::last_read_channel_seq>(
                  position, "id=" + std::to_string(position.id)) != 1) {
            throw std::runtime_error(
                "Failed to backfill chat_read_position.last_read_channel_seq");
          }
        }
      }
      CINATRA_LOG_INFO << "Backfill complete";
    } else {
      // No backfill needed, just load channel cache
      for (auto &channel : all_channels) {
        chat_channel_cache::instance().put(channel);
      }
    }

    CINATRA_LOG_INFO << "Chat database initialized";
    return true;
  } catch (const std::exception &e) {
    CINATRA_LOG_ERROR << "init_chat_db failed: " << e.what();
    return false;
  }
}

// ==================== chat_hub ====================

class chat_hub {
public:
  static chat_hub &instance() {
    static chat_hub h;
    return h;
  }

  enum class priority { normal, low };

  struct queued_frame {
    std::shared_ptr<const std::string> payload;
    bool droppable = false;
  };

  struct session {
    uint64_t user_id;
    std::string user_name;
    std::shared_ptr<coro_http_connection> conn;
    std::atomic<uint64_t> last_msg_time{0}; // rate limiting
    uint64_t active_channel_id = 0;
    std::set<uint64_t> subscribed_channels;
    std::mutex queue_mtx;
    std::deque<queued_frame> send_queue;
    size_t queued_bytes = 0;
    bool sender_running = false;
    bool closing = false;
    uint64_t dropped_low_priority = 0;
  };

  struct add_result {
    std::shared_ptr<session> state;
    bool user_became_online = false;
  };

  struct remove_result {
    uint64_t user_id = 0;
    std::string user_name;
    bool user_became_offline = false;
  };

  add_result add(uint64_t key, uint64_t user_id, std::string user_name,
                 std::shared_ptr<coro_http_connection> conn) {
    auto state = std::make_shared<session>();
    state->user_id = user_id;
    state->user_name = std::move(user_name);
    state->conn = std::move(conn);
    add_result result{};
    result.state = state;
    std::unique_lock lk(mtx_);
    auto &count = user_session_counts_[user_id];
    result.user_became_online = (count == 0);
    ++count;
    sessions_[key] = state;
    if (result.user_became_online) {
      online_users_cache_[user_id] = state->user_name;
    }
    return result;
  }

  remove_result remove(uint64_t key) {
    std::shared_ptr<session> target;
    remove_result result{};
    {
      std::unique_lock lk(mtx_);
      auto it = sessions_.find(key);
      if (it == sessions_.end()) return result;
      target = std::move(it->second);
      sessions_.erase(it);

      if (target->active_channel_id != 0) {
        erase_index_key(active_channel_keys_, target->active_channel_id, key);
      }
      for (auto channel_id : target->subscribed_channels) {
        erase_index_key(channel_subscriber_keys_, channel_id, key);
      }

      result.user_id = target->user_id;
      result.user_name = target->user_name;

      auto count_it = user_session_counts_.find(target->user_id);
      if (count_it != user_session_counts_.end()) {
        if (count_it->second > 1) {
          --count_it->second;
        } else {
          user_session_counts_.erase(count_it);
          online_users_cache_.erase(target->user_id);
          result.user_became_offline = true;
        }
      }
    }

    std::scoped_lock qlk(target->queue_mtx);
    target->closing = true;
    target->queued_bytes = 0;
    target->send_queue.clear();
    target->sender_running = false;
    return result;
  }

  async_simple::coro::Lazy<void> send_to(const std::shared_ptr<session> &target,
                                         std::string payload,
                                         priority prio = priority::normal) {
    if (target) {
      auto shared_payload =
          std::make_shared<const std::string>(std::move(payload));
      enqueue_frame(target, std::move(shared_payload), prio);
    }
    co_return;
  }

  void subscribe_channel(uint64_t key, uint64_t channel_id) {
    if (channel_id == 0) return;
    std::unique_lock lk(mtx_);
    auto it = sessions_.find(key);
    if (it == sessions_.end()) return;
    auto inserted = it->second->subscribed_channels.insert(channel_id);
    if (inserted.second) {
      channel_subscriber_keys_[channel_id].insert(key);
    }
  }

  void set_active_channel(uint64_t key, uint64_t channel_id) {
    if (channel_id == 0) return;
    std::unique_lock lk(mtx_);
    auto it = sessions_.find(key);
    if (it == sessions_.end()) return;

    auto &state = *it->second;
    auto inserted = state.subscribed_channels.insert(channel_id);
    if (inserted.second) {
      channel_subscriber_keys_[channel_id].insert(key);
    }

    if (state.active_channel_id == channel_id) return;

    if (state.active_channel_id != 0) {
      erase_index_key(active_channel_keys_, state.active_channel_id, key);
    }
    state.active_channel_id = channel_id;
    active_channel_keys_[channel_id].insert(key);
  }

  void remove_channel(uint64_t channel_id) {
    if (channel_id == 0) return;

    std::unique_lock lk(mtx_);
    active_channel_keys_.erase(channel_id);
    channel_subscriber_keys_.erase(channel_id);
    for (auto &[key, state] : sessions_) {
      if (!state) continue;
      if (state->active_channel_id == channel_id) {
        state->active_channel_id = 0;
      }
      state->subscribed_channels.erase(channel_id);
    }
  }

  std::vector<std::pair<uint64_t, std::string>> online_users() {
    std::shared_lock lk(mtx_);
    std::vector<std::pair<uint64_t, std::string>> result;
    result.reserve(online_users_cache_.size());
    for (auto &[id, name] : online_users_cache_) {
      result.emplace_back(id, name);
    }
    return result;
  }

  // Returns true if the message is allowed (rate limit not exceeded)
  bool check_msg_rate(uint64_t key) {
    std::shared_lock lk(mtx_);
    auto it = sessions_.find(key);
    if (it == sessions_.end()) return false;
    auto now = get_timestamp_milliseconds();
    auto prev = it->second->last_msg_time.load(std::memory_order_relaxed);
    if (now - prev < 500) return false; // 500ms cooldown
    it->second->last_msg_time.store(now, std::memory_order_relaxed);
    return true;
  }

  async_simple::coro::Lazy<void> broadcast(std::string payload,
                                           priority prio = priority::normal) {
    auto shared_payload =
        std::make_shared<const std::string>(std::move(payload));
    std::shared_lock lk(mtx_);
    for (auto &[_, s] : sessions_) {
      enqueue_frame(s, shared_payload, prio);
    }
    co_return;
  }

  async_simple::coro::Lazy<void> broadcast_active_channel(
      uint64_t channel_id, std::string payload,
      priority prio = priority::normal) {
    auto shared_payload =
        std::make_shared<const std::string>(std::move(payload));
    std::shared_lock lk(mtx_);
    auto it = active_channel_keys_.find(channel_id);
    if (it == active_channel_keys_.end()) co_return;
    for (auto key : it->second) {
      auto session_it = sessions_.find(key);
      if (session_it != sessions_.end()) {
        enqueue_frame(session_it->second, shared_payload, prio);
      }
    }
    co_return;
  }

  async_simple::coro::Lazy<void> broadcast_inactive_subscribers(
      uint64_t channel_id, std::string payload,
      priority prio = priority::low) {
    auto shared_payload =
        std::make_shared<const std::string>(std::move(payload));
    std::shared_lock lk(mtx_);
    auto it = channel_subscriber_keys_.find(channel_id);
    if (it == channel_subscriber_keys_.end()) co_return;
    for (auto key : it->second) {
      auto session_it = sessions_.find(key);
      if (session_it == sessions_.end()) continue;
      if (session_it->second->active_channel_id == channel_id) continue;
      enqueue_frame(session_it->second, shared_payload, prio);
    }
    co_return;
  }

private:
  static constexpr size_t max_pending_messages_ = 256;
  static constexpr size_t max_pending_bytes_ = 512 * 1024;

  template <typename IndexMap>
  static void erase_index_key(IndexMap &index, uint64_t channel_id,
                              uint64_t key) {
    auto it = index.find(channel_id);
    if (it == index.end()) return;
    it->second.erase(key);
    if (it->second.empty()) {
      index.erase(it);
    }
  }

  static bool queue_limit_exceeded(const session &target, size_t incoming_size) {
    return target.send_queue.size() + 1 > max_pending_messages_ ||
           target.queued_bytes + incoming_size > max_pending_bytes_;
  }

  static void clear_queue_locked(session &target) {
    target.send_queue.clear();
    target.queued_bytes = 0;
  }

  static void enqueue_locked(session &target,
                             std::shared_ptr<const std::string> payload,
                             priority prio) {
    target.queued_bytes += payload->size();
    queued_frame frame{std::move(payload), prio == priority::low};
    if (prio == priority::low) {
      target.send_queue.push_back(std::move(frame));
      return;
    }

    auto insert_it = std::find_if(target.send_queue.begin(),
                                  target.send_queue.end(),
                                  [](const queued_frame &item) {
                                    return item.droppable;
                                  });
    target.send_queue.insert(insert_it, std::move(frame));
  }

  static bool evict_one_droppable_locked(session &target) {
    for (auto it = target.send_queue.begin(); it != target.send_queue.end();
         ++it) {
      if (!it->droppable || !it->payload) continue;
      target.queued_bytes -= it->payload->size();
      target.send_queue.erase(it);
      ++target.dropped_low_priority;
      return true;
    }
    return false;
  }

  void enqueue_frame(const std::shared_ptr<session> &target,
                     std::shared_ptr<const std::string> payload,
                     priority prio) {
    if (!target || !payload || !target->conn) return;

    const auto enqueue_begin = chat_perf_stats::now_ns();
    bool start_sender = false;
    bool close_slow_client = false;
    size_t queue_size_after = 0;
    size_t queued_bytes_after = 0;
    {
      std::scoped_lock lk(target->queue_mtx);
      if (target->closing || target->conn->has_closed()) return;

      const auto payload_size = payload->size();
      if (prio == priority::low && queue_limit_exceeded(*target, payload_size)) {
        ++target->dropped_low_priority;
        return;
      }

      while (prio != priority::low &&
             queue_limit_exceeded(*target, payload_size)) {
        if (!evict_one_droppable_locked(*target)) {
          target->closing = true;
          clear_queue_locked(*target);
          close_slow_client = true;
          break;
        }
      }

      if (close_slow_client) {
        CINATRA_LOG_WARNING << "closing slow websocket client user_id="
                            << target->user_id << " user_name="
                            << target->user_name << " pending_bytes="
                            << target->queued_bytes << " dropped_low_priority="
                            << target->dropped_low_priority;
      } else {
        enqueue_locked(*target, std::move(payload), prio);
        queue_size_after = target->send_queue.size();
        queued_bytes_after = target->queued_bytes;
        if (!target->sender_running) {
          target->sender_running = true;
          start_sender = true;
        }
      }
    }

    chat_perf_stats::instance().record_enqueue(
        chat_perf_stats::now_ns() - enqueue_begin, queue_size_after,
        queued_bytes_after);

    if (close_slow_client) {
      target->conn->close();
      return;
    }

    if (start_sender) {
      drain_session_queue(target).via(target->conn->get_executor()).detach();
    }
  }

  async_simple::coro::Lazy<void>
  drain_session_queue(std::shared_ptr<session> target) {
    while (true) {
      std::shared_ptr<const std::string> payload;
      {
        std::scoped_lock lk(target->queue_mtx);
        if (target->closing || !target->conn || target->conn->has_closed()) {
          target->sender_running = false;
          clear_queue_locked(*target);
          co_return;
        }
        if (target->send_queue.empty()) {
          target->sender_running = false;
          co_return;
        }

        payload = std::move(target->send_queue.front().payload);
        if (payload) {
          target->queued_bytes -= payload->size();
        }
        target->send_queue.pop_front();
      }

      if (!payload) continue;

      const auto write_begin = chat_perf_stats::now_ns();
      auto ec = co_await target->conn->write_websocket(*payload);
      chat_perf_stats::instance().record_ws_write(
          chat_perf_stats::now_ns() - write_begin, !ec);
      if (ec) {
        {
          std::scoped_lock lk(target->queue_mtx);
          target->closing = true;
          target->sender_running = false;
          clear_queue_locked(*target);
        }
        target->conn->close();
        co_return;
      }
    }
  }

  chat_hub() = default;
  std::shared_mutex mtx_;
  std::unordered_map<uint64_t, std::shared_ptr<session>> sessions_;
  std::unordered_map<uint64_t, size_t> user_session_counts_;
  std::unordered_map<uint64_t, std::string> online_users_cache_;
  std::unordered_map<uint64_t, std::unordered_set<uint64_t>>
      active_channel_keys_;
  std::unordered_map<uint64_t, std::unordered_set<uint64_t>>
      channel_subscriber_keys_;
};

// ==================== chat_handler_t ====================

class chat_handler_t {
public:
  // GET /api/v1/chat/channels
  void get_channels(coro_http_request &req, coro_http_response &resp) {
    auto conn = get_db_pool().get();
    if (!conn) { set_server_internel_error(resp); return; }

    uint64_t user_id = get_user_id_from_token(req);

    auto channels = conn->query<chat_channel_t>();
    if (channels.empty()) {
      resp.set_content_type<resp_content_type::json>();
      resp.set_status_and_content(
          status_type::ok,
          R"({"success":true,"message":"获取频道列表成功","code":200,"data":[]})");
      return;
    }

    // Batch load user's read positions and memberships (avoid N+1)
    std::unordered_map<uint64_t, uint64_t> last_read_seq_map;
    std::set<uint64_t> joined_private_channels;
    if (user_id > 0) {
      auto positions = conn->query_s<chat_read_position_t>(
          "user_id = ?", user_id);
      for (auto &p : positions) {
        last_read_seq_map[p.channel_id] = p.last_read_channel_seq;
      }
      auto memberships = conn->query_s<chat_channel_member_t>(
          "user_id = ?", user_id);
      for (auto &m : memberships) {
        joined_private_channels.insert(m.channel_id);
      }
    }

    // Filter visible channels
    std::vector<chat_channel_t> visible;
    visible.reserve(channels.size());
    for (auto &ch : channels) {
      if (ch.is_private) {
        if (user_id == 0 ||
            (joined_private_channels.find(ch.id) == joined_private_channels.end()
             && ch.creator_id != user_id)) {
          continue; // skip private channels for unauthenticated or non-member users
        }
      }
      visible.push_back(ch);
    }

    std::ostringstream os;
    os << R"({"success":true,"message":"获取频道列表成功","code":200,"data":[)";
    bool first = true;
    for (auto &ch : visible) {
      if (!first) os << ",";
      first = false;

      uint64_t last_read_seq = 0;
      auto read_it = last_read_seq_map.find(ch.id);
      if (read_it != last_read_seq_map.end()) {
        last_read_seq = read_it->second;
      }
      uint64_t unread =
          ch.message_count > last_read_seq ? ch.message_count - last_read_seq
                                           : 0;

      os << R"({"id":)" << ch.id
         << R"(,"name":")" << json_escape(arr_to_str(ch.name))
         << R"(","topic":")" << json_escape(arr_to_str(ch.topic))
         << R"(","is_private":)" << ch.is_private
         << R"(,"creator_id":)" << ch.creator_id
         << R"(,"created_at":)" << ch.created_at
         << R"(,"unread":)" << unread
         << R"(,"joined":true})";
    }
    os << "]}";
    resp.set_content_type<resp_content_type::json>();
    resp.set_status_and_content(status_type::ok, os.str());
  }

  // POST /api/v1/chat/mark_read
  void mark_read(coro_http_request &req, coro_http_response &resp) {
    uint64_t user_id = get_user_id_from_token(req);
    if (user_id == 0) {
      resp.set_status_and_content(status_type::unauthorized,
                                  make_error("未授权"));
      return;
    }

    auto body = req.get_body();
    chat_mark_read_req info{};
    std::error_code ec;
    iguana::from_json(info, body, ec);
    if (ec || info.channel_id == 0) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("参数错误"));
      return;
    }

    auto conn = get_db_pool().get();
    if (!conn) { set_server_internel_error(resp); return; }

    uint64_t last_read_seq = 0;
    if (info.last_message_id > 0) {
      auto msgs = conn->query_s<chat_message_t>(
          "id = ? AND channel_id = ?", info.last_message_id, info.channel_id);
      if (msgs.empty()) {
        resp.set_status_and_content(status_type::bad_request,
                                    make_error("消息不存在"));
        return;
      }
      last_read_seq = msgs[0].channel_seq;
    }

    auto positions = conn->query_s<chat_read_position_t>(
        "user_id = ? AND channel_id = ?", user_id, info.channel_id);
    if (positions.empty()) {
      chat_read_position_t p{};
      p.user_id = user_id;
      p.channel_id = info.channel_id;
      p.last_read_message_id = info.last_message_id;
      p.last_read_channel_seq = last_read_seq;
      p.updated_at = get_timestamp_milliseconds();
      conn->insert(p);
    } else if (last_read_seq > positions[0].last_read_channel_seq) {
      positions[0].last_read_message_id = info.last_message_id;
      positions[0].last_read_channel_seq = last_read_seq;
      positions[0].updated_at = get_timestamp_milliseconds();
      conn->update(positions[0]);
    }

    resp.set_content_type<resp_content_type::json>();
    resp.set_status_and_content(status_type::ok, make_success("标记已读成功"));
  }

  // POST /api/v1/chat/upload
  void upload_file(coro_http_request &req, coro_http_response &resp) {
    // 鉴权检查
    uint64_t user_id = get_user_id_from_token(req);
    if (user_id == 0) {
      resp.set_status_and_content(status_type::unauthorized,
                                  make_error("请先登录"));
      return;
    }

    auto body = req.get_body();
    if (body.empty()) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("请求体不能为空"));
      return;
    }

    chat_upload_req info{};
    std::error_code ec;
    iguana::from_json(info, body, ec);
    if (ec || info.filename.empty() || info.file_data.empty()) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("文件名和数据不能为空"));
      return;
    }

    auto file_data = cinatra::base64_decode(info.file_data);
    if (!file_data.has_value()) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("base64数据解码失败"));
      return;
    }

    constexpr size_t MAX_CHAT_FILE_SIZE = 5 * 1024 * 1024; // 5MB
    if (file_data.value().size() > MAX_CHAT_FILE_SIZE) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("文件大小不能超过5MB"));
      return;
    }

    std::filesystem::path upload_dir = "html/uploads/chat";
    if (!std::filesystem::exists(upload_dir)) {
      std::filesystem::create_directories(upload_dir);
    }

    auto ext = cinatra::get_extension(info.filename);
    // 校验扩展名只包含安全字符
    std::string ext_str(ext);
    static const std::set<std::string> allowed_exts = {
        ".jpg", ".jpeg", ".png", ".gif", ".bmp", ".webp", ".svg"};
    std::transform(ext_str.begin(), ext_str.end(), ext_str.begin(), ::tolower);
    if (allowed_exts.find(ext_str) == allowed_exts.end()) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("不支持的文件类型"));
      return;
    }

    std::string unique_filename =
        std::to_string(get_timestamp_milliseconds()) + ext_str;
    std::filesystem::path file_path = upload_dir / unique_filename;

    std::ofstream out_file(file_path, std::ios::binary);
    if (!out_file) {
      resp.set_status_and_content(status_type::internal_server_error,
                                  make_error("保存文件失败"));
      return;
    }
    out_file.write(file_data.value().data(), file_data.value().size());
    out_file.close();

    std::string file_url = "/uploads/chat/" + unique_filename;
    std::ostringstream os;
    os << R"({"success":true,"message":"上传成功","code":200,"data":{)"
       << R"("url":")" << json_escape(file_url)
       << R"(","filename":")" << json_escape(unique_filename)
       << R"("}})";
    resp.set_content_type<resp_content_type::json>();
    resp.set_status_and_content(status_type::ok, os.str());
  }

  // GET /api/v1/chat/history?channel_id=1&limit=50
  void get_history(coro_http_request &req, coro_http_response &resp) {
    auto ch_str = std::string(req.get_query_value("channel_id"));
    auto lim_str = std::string(req.get_query_value("limit"));
    if (ch_str.empty()) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("缺少 channel_id 参数"));
      return;
    }
    uint64_t channel_id = 0;
    int limit = 50;
    try {
      channel_id = std::stoull(ch_str);
      if (!lim_str.empty()) limit = std::stoi(lim_str);
    } catch (...) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("参数格式错误"));
      return;
    }
    if (limit <= 0 || limit > 200) limit = 50;

    auto conn = get_db_pool().get();
    if (!conn) { set_server_internel_error(resp); return; }

    auto msgs = conn->query<chat_message_t>(
        "channel_id=" + std::to_string(channel_id) +
        " ORDER BY created_at DESC LIMIT " + std::to_string(limit));
    std::reverse(msgs.begin(), msgs.end());

    // Batch load reactions for all messages (avoid N+1)
    auto reactions_map = batch_build_reactions(msgs);

    std::ostringstream os;
    os << R"({"success":true,"message":"获取消息历史成功","code":200,"data":[)";
    for (size_t i = 0; i < msgs.size(); i++) {
      if (i) os << ",";
      os << build_msg_json(msgs[i], reactions_map[msgs[i].id]);
    }
    os << "]}";
    resp.set_content_type<resp_content_type::json>();
    resp.set_status_and_content(status_type::ok, os.str());
  }

  // GET /api/v1/chat/search?q=keyword&channel_id=1
  void search_messages(coro_http_request &req, coro_http_response &resp) {
    auto q = std::string(req.get_decode_query_value("q"));
    auto ch_str = std::string(req.get_query_value("channel_id"));
    if (q.empty()) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("缺少搜索关键词"));
      return;
    }
    if (q.size() > 100) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("搜索关键词过长"));
      return;
    }

    auto conn = get_db_pool().get();
    if (!conn) { set_server_internel_error(resp); return; }

    uint64_t channel_id = 0;
    if (!ch_str.empty()) {
      try {
        channel_id = std::stoull(ch_str);
      } catch (...) {
        resp.set_status_and_content(status_type::bad_request,
                                    make_error("channel_id 格式错误"));
        return;
      }
    }

    std::vector<chat_message_t> msgs;
    std::string like_pattern = "%" + escape_sql_like(q) + "%";
    if (ch_str.empty()) {
        msgs = conn->query_s<chat_message_t>(
            "content LIKE ? ORDER BY created_at DESC LIMIT 100",
            like_pattern);
    } else {
        msgs = conn->query_s<chat_message_t>(
            "channel_id = ? AND content LIKE ? ORDER BY created_at DESC LIMIT 100",
            channel_id, like_pattern);
    }

    // Batch load reactions (avoid N+1)
    auto reactions_map = batch_build_reactions(msgs);

    std::ostringstream os;
    os << R"({"success":true,"message":"搜索成功","code":200,"data":[)";
    for (size_t i = 0; i < msgs.size(); i++) {
      if (i) os << ",";
      os << build_msg_json(msgs[i], reactions_map[msgs[i].id]);
    }
    os << "]}";
    resp.set_content_type<resp_content_type::json>();
    resp.set_status_and_content(status_type::ok, os.str());
  }

  // POST /api/v1/chat/channel
  void create_channel(coro_http_request &req, coro_http_response &resp) {
    auto creator_id = get_user_id_from_token(req);
    if (creator_id == 0) {
      resp.set_status_and_content(status_type::unauthorized,
                                  make_error("未授权"));
      return;
    }
    if (!is_chat_admin(creator_id)) {
      resp.set_status_and_content(status_type::forbidden,
                                  make_error("仅管理员可创建频道", 403));
      return;
    }

    auto body = req.get_body();
    if (body.empty()) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("请求体不能为空"));
      return;
    }
    chat_create_channel_req ci{};
    std::error_code ec;
    iguana::from_json(ci, body, ec);
    if (ec || ci.name.empty()) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("频道名称不能为空"));
      return;
    }
    if (ci.name.size() > 60) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("频道名称不能超过60个字符"));
      return;
    }
    if (ci.topic.size() > 250) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("频道主题不能超过250个字符"));
      return;
    }

    chat_channel_t ch{};
    str_to_arr(ch.name, ci.name);
    str_to_arr(ch.topic, ci.topic.empty() ? "新创建的频道" : ci.topic);
    ch.creator_id = creator_id;
    ch.is_private = 0;
    ch.created_at = get_timestamp_milliseconds();
    ch.message_count = 0;

    auto conn = get_db_pool().get();
    if (!conn) { set_server_internel_error(resp); return; }

    auto id = conn->get_insert_id_after_insert(ch);
    if (id <= 0) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("频道已存在或创建失败"));
      return;
    }

    ch.id = id;
    chat_channel_cache::instance().put(ch);

    std::ostringstream os;
    os << R"({"success":true,"message":"创建频道成功","code":200,"data":{)"
       << R"("id":)" << id
       << R"(,"name":")" << json_escape(ci.name)
       << R"(","topic":")" << json_escape(ci.topic.empty() ? "新创建的频道" : ci.topic)
       << R"(","is_private":)" << ch.is_private
       << R"(,"creator_id":)" << ch.creator_id
       << R"(,"created_at":)" << ch.created_at
       << R"(,"unread":0,"joined":true}})";
    resp.set_content_type<resp_content_type::json>();
    resp.set_status_and_content(status_type::ok, os.str());
  }

  // POST /api/v1/chat/delete_channel
  void delete_channel(coro_http_request &req, coro_http_response &resp) {
    auto operator_id = get_user_id_from_token(req);
    if (operator_id == 0) {
      resp.set_status_and_content(status_type::unauthorized,
                                  make_error("未授权"));
      return;
    }
    if (!is_chat_admin(operator_id)) {
      resp.set_status_and_content(status_type::forbidden,
                                  make_error("仅管理员可删除频道", 403));
      return;
    }

    auto body = req.get_body();
    if (body.empty()) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("请求体不能为空"));
      return;
    }

    chat_delete_channel_req info{};
    std::error_code ec;
    iguana::from_json(info, body, ec);
    if (ec || info.channel_id == 0) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("频道参数错误"));
      return;
    }

    if (info.channel_id <= 3) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("默认频道不允许删除"));
      return;
    }

    auto conn = get_db_pool().get();
    if (!conn) { set_server_internel_error(resp); return; }

    auto channels = conn->query_s<chat_channel_t>("id = ?", info.channel_id);
    if (channels.empty()) {
      resp.set_status_and_content(status_type::not_found,
                                  make_error("频道不存在", 404));
      return;
    }

    if (!conn->begin()) {
      set_server_internel_error(resp);
      return;
    }

    auto msgs = conn->query_s<chat_message_t>("channel_id = ?", info.channel_id);
    for (auto &msg : msgs) {
      conn->delete_records_s<chat_reaction_t>("message_id = ?", msg.id);
    }
    conn->delete_records_s<chat_message_t>("channel_id = ?", info.channel_id);
    conn->delete_records_s<chat_read_position_t>("channel_id = ?", info.channel_id);
    conn->delete_records_s<chat_channel_member_t>("channel_id = ?", info.channel_id);
    conn->delete_records_s<chat_channel_t>("id = ?", info.channel_id);

    if (!conn->commit()) {
      conn->rollback();
      set_server_internel_error(resp);
      return;
    }

    chat_hub::instance().remove_channel(info.channel_id);
    chat_channel_cache::instance().erase(info.channel_id);

    std::ostringstream ws_os;
    ws_os << R"({"type":"channel_deleted","channel_id":)" << info.channel_id << "}";
    async_simple::coro::syncAwait(chat_hub::instance().broadcast(
        ws_os.str(), chat_hub::priority::normal));

    resp.set_content_type<resp_content_type::json>();
    resp.set_status_and_content(status_type::ok, make_success("删除频道成功"));
  }

  // GET /ws/chat?token=<jwt>
  async_simple::coro::Lazy<void> handle_ws(coro_http_request &req,
                                           coro_http_response &resp) {
    auto token = req.get_decode_query_value("token");
    if (token.empty()) {
      resp.set_status_and_content(status_type::unauthorized,
                                  make_error("缺少 token"));
      co_return;
    }

    auto [result, info] = validate_jwt_token(token);
    if (result != TokenValidationResult::Valid || !info) {
      resp.set_status_and_content(status_type::unauthorized,
                                  make_error("无效的 token"));
      co_return;
    }

    uint64_t user_id = info->user_id;
    std::string user_name;
    {
      auto c = get_db_pool().get();
      if (c) {
        auto us = c->query_s<users_t>("id = ?", user_id);
        if (!us.empty()) user_name = arr_to_str(us[0].user_name);
      }
    }
    if (user_name.empty()) user_name = "User_" + std::to_string(user_id);

    auto *conn_ptr = req.get_conn();
    auto conn = conn_ptr->shared_from_this();
    static std::atomic<uint64_t> s_conn_id_gen{0};
    uint64_t conn_key = s_conn_id_gen.fetch_add(1, std::memory_order_relaxed);
    auto add_result =
        chat_hub::instance().add(conn_key, user_id, user_name, conn);
    auto session_state = add_result.state;

    // Send hello
    {
      auto online = chat_hub::instance().online_users();
      std::ostringstream os;
      os << R"({"type":"hello","user":{"id":)" << user_id
         << R"(,"name":")" << json_escape(user_name)
         << R"("},"online":)" << build_online_json(online) << "}";
      co_await chat_hub::instance().send_to(session_state, os.str());
    }

    // Broadcast presence_join
    if (add_result.user_became_online) {
      std::ostringstream os;
      os << R"({"type":"presence_join","user":{"id":)" << user_id
         << R"(,"name":")" << json_escape(user_name) << R"("}})";
      co_await chat_hub::instance().broadcast(os.str(),
                                              chat_hub::priority::low);
    }

    CINATRA_LOG_INFO << "WS connected: " << user_name << " (id=" << user_id << ")";

    // Message loop
    while (true) {
      auto ws_result = co_await conn_ptr->read_websocket();
      if (ws_result.ec) break;
      if (ws_result.type == ws_frame_type::WS_CLOSE_FRAME) break;
      if (ws_result.type == ws_frame_type::WS_PING_FRAME ||
          ws_result.type == ws_frame_type::WS_PONG_FRAME) continue;
      if (ws_result.type != ws_frame_type::WS_TEXT_FRAME) continue;

      // Parse client msg — only need type, channel_id, text, message_id, emoji
      // Use iguana for this simple flat struct
      chat_ws_in cm{};
      std::error_code ec;
      iguana::from_json(cm, ws_result.data, ec);
      if (ec) continue;

      if (cm.type == "message") {
        if (cm.text.size() > 2000) {
          co_await chat_hub::instance().send_to(
              session_state,
              R"({"type":"error","msg":"消息不能超过2000字"})");
          continue;
        }
        if (!chat_hub::instance().check_msg_rate(conn_key)) {
          co_await chat_hub::instance().send_to(
              session_state,
              R"({"type":"error","msg":"发送过于频繁，请稍后再试"})");
          continue;
        }
        co_await handle_chat_message(user_id, user_name, cm.channel_id, cm.text);
      } else if (cm.type == "subscribe_channel") {
        if (user_can_access_channel(user_id, cm.channel_id)) {
          chat_hub::instance().subscribe_channel(conn_key, cm.channel_id);
        }
      } else if (cm.type == "switch_channel") {
        if (user_can_access_channel(user_id, cm.channel_id)) {
          chat_hub::instance().set_active_channel(conn_key, cm.channel_id);
        }
      } else if (cm.type == "react") {
        co_await handle_reaction(user_id, cm.message_id, cm.emoji);
      } else if (cm.type == "edit_message") {
        if (cm.text.size() > 2000) {
          co_await chat_hub::instance().send_to(
              session_state,
              R"({"type":"error","msg":"编辑内容不能超过2000字"})");
          continue;
        }
        co_await handle_edit_message(user_id, cm.message_id, cm.text);
      } else if (cm.type == "delete_message") {
        co_await handle_delete_message(user_id, cm.message_id);
      } else if (cm.type == "typing") {
        co_await handle_typing(user_id, user_name, cm.channel_id);
      }
    }

    // Cleanup
    auto remove_result = chat_hub::instance().remove(conn_key);
    if (remove_result.user_became_offline) {
      {
        std::ostringstream os;
        os << R"({"type":"presence_leave","user_id":)" << user_id << "}";
        co_await chat_hub::instance().broadcast(os.str(),
                                                chat_hub::priority::low);
      }
      {
        auto online = chat_hub::instance().online_users();
        std::ostringstream os;
        os << R"({"type":"online_update","online":)"
           << build_online_json(online) << "}";
        co_await chat_hub::instance().broadcast(os.str(),
                                                chat_hub::priority::low);
      }
    }

    CINATRA_LOG_INFO << "WS disconnected: " << user_name;
    co_return;
  }

private:
  bool is_chat_admin(uint64_t user_id) {
    if (user_id == 0) return false;

    auto conn = get_db_pool().get();
    if (!conn) return false;

    auto users = conn->query_s<users_t>("id = ?", user_id);
    if (users.empty()) return false;

    auto role = users[0].role;
    return role == "admin" || role == "superadmin";
  }

  bool user_can_access_channel(uint64_t user_id, uint64_t channel_id) {
    if (channel_id == 0) return false;

    chat_channel_cache_entry channel{};
    if (!chat_channel_cache::instance().get(channel_id, channel)) return false;
    if (!channel.is_private) return true;
    if (channel.creator_id == user_id) return true;

    bool allowed = false;
    if (chat_access_cache::instance().get(user_id, channel_id, allowed)) {
      return allowed;
    }

    auto conn = get_db_pool().get();
    if (!conn) return false;

    auto members = conn->query_s<chat_channel_member_t>(
        "channel_id = ? AND user_id = ?", channel_id, user_id);
    allowed = !members.empty();
    chat_access_cache::instance().put(user_id, channel_id, allowed);
    return allowed;
  }

  async_simple::coro::Lazy<void>
  handle_chat_message(uint64_t user_id, const std::string &user_name,
                      uint64_t channel_id, const std::string &text) {
    const auto total_begin = chat_perf_stats::now_ns();
    uint64_t db_ns = 0;
    uint64_t json_build_ns = 0;
    uint64_t active_broadcast_ns = 0;
    uint64_t inactive_broadcast_ns = 0;

    if (text.empty() || channel_id == 0) co_return;
    // Input validation
    if (text.size() > 2000) co_return;

    // Permission check: verify user has access to the channel
    if (!user_can_access_channel(user_id, channel_id)) co_return;

    chat_channel_cache_entry channel{};
    if (!chat_channel_cache::instance().get(channel_id, channel)) co_return;

    chat_message_t m{};
    m.channel_id = channel_id;
    m.user_id = user_id;
    str_to_arr(m.user_name, user_name);
    m.content = text;
    m.created_at = get_timestamp_milliseconds();
    m.channel_seq = 0;

    uint64_t msg_id = 0;
    {
      const auto db_begin = chat_perf_stats::now_ns();
      auto channel_mtx = chat_channel_cache::instance().channel_mutex(channel_id);
      std::scoped_lock channel_lock(*channel_mtx);

      auto conn = get_db_pool().get();
      if (!conn) co_return;
      if (!conn->begin()) co_return;

      auto latest = conn->query_s<chat_channel_t>("id = ?", channel_id);
      if (latest.empty()) {
        conn->rollback();
        co_return;
      }

      auto next_seq = latest[0].message_count + 1;
      m.channel_seq = next_seq;
      msg_id = conn->get_insert_id_after_insert(m);
      if (msg_id == 0) {
        conn->rollback();
        co_return;
      }

      latest[0].message_count = next_seq;
      if (conn->update_some<&chat_channel_t::message_count>(
              latest[0], "id=" + std::to_string(channel_id)) != 1) {
        conn->rollback();
        co_return;
      }

      if (!conn->commit()) {
        conn->rollback();
        co_return;
      }

      chat_channel_cache::instance().set_message_count(channel_id, next_seq);
      db_ns = chat_perf_stats::now_ns() - db_begin;
    }
    if (msg_id == 0) co_return;

    m.id = msg_id;
    const auto json_begin = chat_perf_stats::now_ns();
    std::ostringstream active_os;
    active_os << R"({"type":"message","msg":)" << build_msg_json(m)
              << "}";
    json_build_ns = chat_perf_stats::now_ns() - json_begin;
    const auto active_begin = chat_perf_stats::now_ns();
    co_await chat_hub::instance().broadcast_active_channel(channel_id,
                                                           active_os.str());
    active_broadcast_ns = chat_perf_stats::now_ns() - active_begin;

    std::ostringstream activity_os;
    activity_os << R"({"type":"channel_activity","channel_id":)" << channel_id
                << R"(,"unread_delta":1})";
    const auto inactive_begin = chat_perf_stats::now_ns();
    co_await chat_hub::instance().broadcast_inactive_subscribers(
        channel_id, activity_os.str());
    inactive_broadcast_ns = chat_perf_stats::now_ns() - inactive_begin;
    chat_perf_stats::instance().record_message(
        chat_perf_stats::now_ns() - total_begin, db_ns, json_build_ns,
        active_broadcast_ns, inactive_broadcast_ns);
  }

  async_simple::coro::Lazy<void>
  handle_reaction(uint64_t user_id, uint64_t message_id,
                  const std::string &emoji) {
    if (message_id == 0 || emoji.empty()) co_return;
    if (emoji.size() > 14) co_return; // emoji field is char[16]

    uint64_t channel_id = 0;
    {
      auto conn = get_db_pool().get();
      if (!conn) co_return;
      auto msgs = conn->query_s<chat_message_t>("id = ?", message_id);
      if (msgs.empty()) co_return;
      channel_id = msgs[0].channel_id;
      if (!user_can_access_channel(user_id, channel_id)) co_return;

      auto existing = conn->query_s<chat_reaction_t>(
          "message_id = ? AND user_id = ? AND emoji = ?",
          message_id, user_id, emoji);
      if (!existing.empty()) {
        conn->delete_records_s<chat_reaction_t>(
            "id = ?", existing[0].id);
      } else {
        chat_reaction_t r{};
        r.message_id = message_id;
        r.user_id = user_id;
        str_to_arr(r.emoji, emoji);
        r.created_at = get_timestamp_milliseconds();
        conn->insert(r);
      }
    }

    std::ostringstream os;
    os << R"({"type":"reaction_update","message_id":)" << message_id
       << R"(,"channel_id":)" << channel_id
       << R"(,"reactions":)" << build_reactions_json(message_id) << "}";
    co_await chat_hub::instance().broadcast_active_channel(channel_id,
                                                           os.str());
  }

  async_simple::coro::Lazy<void>
  handle_edit_message(uint64_t user_id, uint64_t message_id,
                      const std::string &new_text) {
    if (message_id == 0 || new_text.empty()) co_return;
    if (new_text.size() > 2000) co_return;

    auto conn = get_db_pool().get();
    if (!conn) co_return;

    // Verify ownership
    auto msgs = conn->query_s<chat_message_t>("id = ?", message_id);
    if (msgs.empty() || msgs[0].user_id != user_id) co_return;
    auto channel_id = msgs[0].channel_id;

    // Update content
    msgs[0].content = new_text;
    conn->update(msgs[0]);

    std::ostringstream os;
    os << R"({"type":"message_edited","message_id":)" << message_id
       << R"(,"channel_id":)" << channel_id
       << R"(,"text":")" << json_escape(new_text)
       << R"(","edited_at":)" << get_timestamp_milliseconds() << "}";
    co_await chat_hub::instance().broadcast_active_channel(channel_id,
                                                           os.str());
  }

  async_simple::coro::Lazy<void>
  handle_delete_message(uint64_t user_id, uint64_t message_id) {
    if (message_id == 0) co_return;

    auto conn = get_db_pool().get();
    if (!conn) co_return;

    // Verify ownership
    auto msgs = conn->query_s<chat_message_t>("id = ?", message_id);
    if (msgs.empty() || msgs[0].user_id != user_id) co_return;

    auto channel_id = msgs[0].channel_id;

    // Delete reactions first, then message
    conn->delete_records_s<chat_reaction_t>("message_id = ?", message_id);
    conn->delete_records_s<chat_message_t>("id = ?", message_id);

    // Decrement channel message_count
    conn->execute(
        "UPDATE chat_channels SET message_count = CASE WHEN message_count > 0 "
        "THEN message_count - 1 ELSE 0 END WHERE id = " +
        std::to_string(channel_id));
    chat_channel_cache::instance().decrement_message_count(channel_id);

    std::ostringstream os;
    os << R"({"type":"message_deleted","message_id":)" << message_id
       << R"(,"channel_id":)" << channel_id << "}";
    co_await chat_hub::instance().broadcast_active_channel(channel_id,
                                                           os.str());
  }

  async_simple::coro::Lazy<void>
  handle_typing(uint64_t user_id, const std::string &user_name,
                uint64_t channel_id) {
    if (channel_id == 0) co_return;

    std::ostringstream os;
    os << R"({"type":"typing","user_id":)" << user_id
       << R"(,"user_name":")" << json_escape(user_name)
       << R"(","channel_id":)" << channel_id << "}";
    co_await chat_hub::instance().broadcast_active_channel(channel_id,
                                                           os.str(),
                                                           chat_hub::priority::low);
  }
};

} // namespace purecpp
