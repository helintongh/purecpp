#pragma once

#include "config.hpp"
#include <algorithm>
#include <mutex>
#include <optional>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

namespace purecpp {

// 限流结果
enum class rate_limit_result : int32_t {
  ALLOWED,      // 允许
  RATE_LIMITED, // 被限流
  BLOCKED       // 被封禁
};

// 单个请求记录
struct request_record {
  std::string key;                       // 限流键（IP或邮箱）
  std::vector<uint64_t> timestamps;      // 请求时间戳列表
  std::optional<uint64_t> blocked_until; // 封禁截止时间（毫秒时间戳）
};

// 内部限流规则配置对象
struct rule_config : rate_limit_rule {
  bool is_regex = false; // 是否为正则表达式（自动检测）
  std::regex regex_pattern; // 编译后的正则表达式（仅当is_regex=true时有效）
};

// 限流管理器
class rate_limiter {
public:
  static rate_limiter &instance() {
    static rate_limiter instance;
    return instance;
  }
  /**
   * @brief 初始化限流器，从配置加载规则（从user_config.json）
   */
  void init_from_config() {
    std::lock_guard lock(mutex_);

    // 清除现有规则
    normal_rules_.clear();
    regex_rules_.clear();

    const auto &rules =
        purecpp_config::get_instance().user_cfg_.rate_limit_rules;

    for (const auto &rule : rules) {
      rule_config config;
      config.enabled = rule.enabled;
      config.max_requests = rule.max_requests;
      config.window_seconds = rule.window_seconds;
      config.path = rule.path;

      // 自动检测是否为正则表达式
      config.is_regex = is_regex_pattern(config.path);

      if (config.is_regex && !config.path.empty()) {
        // 编译正则表达式
        try {
          config.regex_pattern =
              std::regex(config.path, std::regex::ECMAScript);
          regex_rules_.push_back(config);
          CINATRA_LOG_INFO << "Loaded regex rate limit rule: " << config.path
                           << " (max=" << config.max_requests
                           << ", window=" << config.window_seconds
                           << "s, enabled="
                           << (config.enabled ? "true" : "false") << ")";
        } catch (const std::regex_error &e) {
          CINATRA_LOG_ERROR << "Invalid regex pattern: " << config.path
                            << ", error: " << e.what();
        }
      } else {
        // 普通字符串匹配
        normal_rules_[config.path] = config;
        CINATRA_LOG_INFO << "Loaded rate limit rule: " << config.path
                         << " (max=" << config.max_requests
                         << ", window=" << config.window_seconds
                         << "s, enabled=" << (config.enabled ? "true" : "false")
                         << ")";
      }
    }
  }

  /**
   * @brief 检查是否允许请求
   * @param key 限流键（通常是客户端IP）
   * @param path 请求路径
   * @return 限流结果
   */
  rate_limit_result check(const std::string &key, const std::string &path) {
    std::lock_guard lock(mutex_);

    // 先检查普通字符串规则
    auto rule_it = normal_rules_.find(path);
    if (rule_it != normal_rules_.end() && rule_it->second.enabled) {
      return check_rule(key, rule_it->second);
    }

    // 再检查正则表达式规则
    for (const auto &rule : regex_rules_) {
      if (rule.enabled && std::regex_match(path, rule.regex_pattern)) {
        return check_rule(key, rule);
      }
    }

    return rate_limit_result::ALLOWED; // 没有配置限流规则，允许请求
  }

private:
  /**
   * @brief 判断路径是否为正则表达式
   * @param path 路径字符串
   * @return true 如果包含正则表达式元字符
   */
  inline bool is_regex_pattern(const std::string &path) {
    // 检查是否包含正则表达式元字符
    static const std::regex regex_meta(R"([\^$.*+?()\[\]{}|\\])");
    return std::regex_search(path, regex_meta);
  }

  /**
   * @brief 检查具体规则
   */
  rate_limit_result check_rule(const std::string &key,
                               const rule_config &config) {
    uint64_t now = get_timestamp_milliseconds();
    uint64_t window_ms = static_cast<uint64_t>(config.window_seconds) * 1000;

    // 获取或创建请求记录
    auto &record = records_[key + ":" + config.path];
    auto &timestamps = record.timestamps;

    // 清理过期的时间戳
    timestamps.erase(std::remove_if(timestamps.begin(), timestamps.end(),
                                    [now, window_ms](uint64_t ts) {
                                      return now - ts > window_ms;
                                    }),
                     timestamps.end());

    // 检查是否超过限制
    if (static_cast<int>(timestamps.size()) >= config.max_requests) {
      // 检查是否在封禁期
      if (!record.blocked_until.has_value()) {
        // 首次触发限流，设置封禁时间（时间窗口的2倍）
        record.blocked_until = now + window_ms * 2;
        CINATRA_LOG_WARNING
            << "Rate limit exceeded for key: " << key
            << ", rule: " << config.path
            << ", blocking until: " << (record.blocked_until.value() / 1000);
        return rate_limit_result::RATE_LIMITED;
      }

      // 如果还在封禁期内
      if (now < record.blocked_until.value()) {
        return rate_limit_result::BLOCKED;
      }

      // 封禁期已过，重置状态
      record.blocked_until.reset();
      timestamps.clear();
    }

    // 记录请求
    timestamps.push_back(now);
    return rate_limit_result::ALLOWED;
  }

public:
  /**
   * @brief 清除所有记录（用于测试或手动重置）
   */
  void clear() {
    std::lock_guard lock(mutex_);
    records_.clear();
  }

  /**
   * @brief 获取重试时间（秒）
   */
  int get_retry_after(const std::string &key, const std::string &path) {
    std::lock_guard lock(mutex_);

    // 先检查普通字符串规则
    auto rule_it = normal_rules_.find(path);
    if (rule_it != normal_rules_.end()) {
      return get_retry_for_rule(key, rule_it->second);
    }

    // 再检查正则表达式规则
    for (const auto &rule : regex_rules_) {
      if (std::regex_match(path, rule.regex_pattern)) {
        return get_retry_for_rule(key, rule);
      }
    }

    return 0;
  }

private:
  /**
   * @brief 获取剩余请求次数（针对具体规则）
   */
  int get_remaining_for_rule(const std::string &key,
                             const rule_config &config) {
    uint64_t now = get_timestamp_milliseconds();
    uint64_t window_ms = static_cast<uint64_t>(config.window_seconds) * 1000;

    auto record_it = records_.find(key + ":" + config.path);
    if (record_it == records_.end()) {
      return config.max_requests;
    }

    // 检查是否被封禁
    if (record_it->second.blocked_until.has_value() &&
        now < record_it->second.blocked_until.value()) {
      return 0;
    }

    // 直接使用已清理过的时间戳列表
    auto &timestamps = record_it->second.timestamps;
    return (std::max)(0,
                    config.max_requests - static_cast<int>(timestamps.size()));
  }

  /**
   * @brief 获取重试时间（针对具体规则）
   */
  int get_retry_for_rule(const std::string &key, const rule_config &config) {
    auto record_it = records_.find(key + ":" + config.path);
    if (record_it == records_.end()) {
      return 0;
    }

    if (!record_it->second.blocked_until.has_value()) {
      return 0;
    }

    uint64_t now = get_timestamp_milliseconds();
    if (now >= record_it->second.blocked_until.value()) {
      return 0;
    }

    return static_cast<int>((record_it->second.blocked_until.value() - now) /
                            1000);
  }

  rate_limiter() = default;
  ~rate_limiter() = default;

  std::unordered_map<std::string, rule_config>
      normal_rules_;                     // 普通字符串规则映射
  std::vector<rule_config> regex_rules_; // 正则表达式规则列表
  std::unordered_map<std::string, request_record> records_; // 请求记录
  std::mutex mutex_;                                        // 互斥锁
};

/**
 * @brief 统一的限流检查函数
 * @param req HTTP请求对象
 * @param resp HTTP响应对象
 * @return true 允许继续处理请求，false 请求被限流
 *
 * 该函数会根据请求的路径和方法，从配置中查找对应的限流规则，
 * 并进行限流检查。如果请求被限流，会设置适当的响应状态码和头信息。
 */
inline bool check_rate_limit(coro_http_request &req, coro_http_response &resp) {
  std::string path(req.get_url());
  std::string method(req.get_method());

  // 移除查询参数，只保留路径
  auto query_pos = path.find('?');
  if (query_pos != std::string::npos) {
    path = path.substr(0, query_pos);
  }

  // 获取客户端IP
  std::string client_ip = get_client_ip(req);

  // 执行限流检查
  auto result = rate_limiter::instance().check(client_ip, path);

  if (result == rate_limit_result::BLOCKED ||
      result == rate_limit_result::RATE_LIMITED) {
    int retry_after = rate_limiter::instance().get_retry_after(client_ip, path);
    resp.set_status_and_content(status_type::bad_request,
                                make_error("请求过于频繁，请" +
                                           std::to_string(retry_after) +
                                           "秒后再试"));
    CINATRA_LOG_WARNING << "Rate limit RATE_LIMITED: ip=" << client_ip
                        << ", path=" << path << ", method=" << method;
    return false;
  }

  return true;
}

} // namespace purecpp
