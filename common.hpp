#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>
#include <string_view>

#include "config.hpp"
#include "entity.hpp"
#include "user_dto.hpp"
#include <cinatra.hpp>
#include <cinatra/smtp_client.hpp>
#include <iguana/json_reader.hpp>
#include <iguana/json_writer.hpp>

using namespace cinatra;

namespace purecpp {
inline uint64_t get_timestamp_milliseconds() {
  auto now = std::chrono::system_clock::now();
  auto duration = now.time_since_epoch();
  auto milliseconds =
      std::chrono::duration_cast<std::chrono::milliseconds>(duration);
  return static_cast<uint64_t>(milliseconds.count());
}

inline uint64_t get_timestamp_seconds() {
  auto now = std::chrono::system_clock::now();
  auto duration = now.time_since_epoch();
  auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);
  return static_cast<uint64_t>(seconds.count());
}

template<std::size_t N>
std::string array_to_string(const std::array<char, N>& arr) {
    const char* first = arr.data();
    const char* last  = std::find(first, first + N, '\0');
    // 如果未找到 '\0'，则使用整个数组（注意：可能包含未初始化数据）
    return std::string(first, last);
}

inline std::string trim_trailing_slash(std::string url) {
  while (url.size() > 1 && url.back() == '/') {
    url.pop_back();
  }
  return url;
}

inline std::string get_base_url(coro_http_request &req) {
  auto &config = purecpp_config::get_instance();
  if (!config.user_cfg_.web_server_url.empty()) {
    return trim_trailing_slash(config.user_cfg_.web_server_url);
  }

  auto host = req.get_header_value("Host");
  if (host.empty()) {
    return {};
  }

  auto scheme = req.get_scheme();
  std::string_view normalized_scheme = scheme.empty() ? "https" : scheme;
  return std::string(normalized_scheme) + "://" + std::string(host);
}

inline std::string make_absolute_url(std::string base_url, std::string_view path) {
  if (base_url.empty()) {
    return std::string(path);
  }

  if (!path.empty() && path.front() == '/') {
    return std::move(base_url) + std::string(path);
  }

  return std::move(base_url) + "/" + std::string(path);
}

inline std::string format_rss_pub_date(uint64_t timestamp_milliseconds) {
  auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::milliseconds(timestamp_milliseconds));
  std::time_t timestamp = static_cast<std::time_t>(seconds.count());
  std::tm utc_tm{};

#ifdef _WIN32
  if (gmtime_s(&utc_tm, &timestamp) != 0) {
    return {};
  }
#else
  if (gmtime_r(&timestamp, &utc_tm) == nullptr) {
    return {};
  }
#endif

  std::ostringstream oss;
  oss.imbue(std::locale::classic());
  oss << std::put_time(&utc_tm, "%a, %d %b %Y %H:%M:%S GMT");
  return oss.str();
}

/**
 * @brief 生成简单的成功响应
 * @param msg 成功消息
 * @return JSON格式的响应字符串
 */
inline std::string make_success(std::string msg = "") {
  rest_response<empty_data> data{};
  data.success = true;
  data.message = std::move(msg);
  data.data = empty_data{};
  // 设置当前时间戳
  auto now = get_timestamp_milliseconds();
  data.timestamp = std::to_string(now);

  std::string json;
  try {
    iguana::to_json(data, json);
  } catch (std::exception &e) {
    json = "";
    CINATRA_LOG_ERROR << e.what();
  }

  return json;
}

/**
 * @brief 生成错误响应
 * @param err_msg 错误消息
 * @param code 错误码，默认为400
 * @return JSON格式的响应字符串
 */
inline std::string make_error(std::string_view err_msg, int code = 400) {
  rest_response<std::string> data{false, std::string(err_msg)};
  data.code = code;
  auto now = get_timestamp_milliseconds();
  data.timestamp = std::to_string(now);
  std::string json;
  iguana::to_json(data, json);
  return json;
}

template <typename T>
inline std::string make_data(T t, std::string msg = "", int total_count = 0) {
  rest_response<T> data{};
  data.success = true;
  data.message = std::move(msg);
  data.data = std::move(t);
  // 设置当前时间戳
  auto now = get_timestamp_milliseconds();
  data.timestamp = std::to_string(now);
  data.total_count = total_count;

  std::string json;
  try {
    iguana::to_json(data, json);
  } catch (std::exception &e) {
    json = "";
    CINATRA_LOG_ERROR << e.what();
  }

  return json;
}

inline void set_server_internel_error(auto &resp) {
  resp.set_status_and_content(
      status_type::internal_server_error,
      make_error(to_http_status_string(status_type::internal_server_error)));
}

// 改进的安全Token生成函数
inline std::string generate_token(TokenType token_type) {
  // 使用64位随机数生成器，提高熵值
  std::random_device rd;
  std::mt19937_64 gen(rd());
  std::uniform_int_distribution<uint64_t> dis(0, UINT64_MAX);

  // 生成128位（16字节）随机数据，确保安全性
  uint64_t random1 = dis(gen);
  uint64_t random2 = dis(gen);

  // 构建原始token字节序列
  std::string raw_token;
  raw_token.reserve(16);

  // 编码random1（8字节）
  for (int i = 0; i < 8; i++) {
    raw_token += static_cast<char>((random1 >> (i * 8)) & 0xFF);
  }

  // 编码random2（8字节）
  for (int i = 0; i < 8; i++) {
    raw_token += static_cast<char>((random2 >> (i * 8)) & 0xFF);
  }

  // Base64 URL安全编码
  std::string encoded = cinatra::base64_encode(raw_token);

  // 转换为URL安全格式（RFC 4648）
  std::replace(encoded.begin(), encoded.end(), '+', '-');
  std::replace(encoded.begin(), encoded.end(), '/', '_');
  // 移除填充符
  while (!encoded.empty() && encoded.back() == '=') {
    encoded.pop_back();
  }

  // 获取毫秒级时间戳（用于防止批量生成碰撞）
  auto now = std::chrono::system_clock::now();
  auto now_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(now);
  uint64_t timestamp = now_ms.time_since_epoch().count();

  // 添加类型前缀和时间戳后缀
  std::stringstream ss;
  switch (token_type) {
  case TokenType::RESET_PASSWORD:
    ss << "rst_";
    break;
  case TokenType::VERIFY_EMAIL:
    ss << "vrf_";
    break;
  case TokenType::REFRESH_TOKEN:
    ss << "rfr_";
    break;
  default:
    ss << "tok_";
    break;
  }

  // 添加随机数据和时间戳（后8位十六进制，约支持到 year 10889）
  ss << encoded << "_" << std::setw(8) << std::setfill('0') << std::hex
     << (timestamp & 0xFFFFFFFF);

  return ss.str();
}

// 获取Token过期时间
inline uint64_t get_token_expires_at(TokenType token_type) {
  auto now = std::chrono::system_clock::now();
  std::chrono::milliseconds duration;

  switch (token_type) {
  case TokenType::VERIFY_EMAIL:
    // 邮箱验证token：24小时
    duration = std::chrono::hours(24);
    break;
  case TokenType::RESET_PASSWORD:
    // 重置密码token：1小时
    duration = std::chrono::hours(1);
    break;
  case TokenType::REFRESH_TOKEN:
    // 刷新token：7天
    duration = std::chrono::hours(24 * 7);
    break;
  default:
    // 默认1小时
    duration = std::chrono::hours(1);
    break;
  }

  auto expires = now + duration;
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             expires.time_since_epoch())
      .count();
}

// 通用邮件发送函数
inline async_simple::coro::Lazy<bool> send_email(const std::string &to_email,
                                                 const std::string &subject,
                                                 const std::string &content,
                                                 bool is_html = true) {
  auto &conf = purecpp_config::get_instance();
  auto &user_conf = conf.user_cfg_;

  // 检查必要的配置是否存在
  if (user_conf.smtp_host.empty() || user_conf.smtp_user.empty() ||
      user_conf.smtp_password.empty()) {
    CINATRA_LOG_ERROR << "SMTP配置不完整";
    co_return false;
  }

  try {
    // 创建SMTP客户端（使用SSL）
    auto client = smtp::get_smtp_client(coro_io::get_global_executor());
    bool r = co_await client.connect(user_conf.smtp_host,
                                     std::to_string(user_conf.smtp_port));
    // 连接SMTP服务器
    if (!r) {
      CINATRA_LOG_ERROR << "SMTP连接失败";
      co_return false;
    }

    // 设置邮件内容
    cinatra::smtp::email_data email_data;
    email_data.user_name = user_conf.smtp_user;
    email_data.auth_pwd = user_conf.smtp_password;
    email_data.from_email = user_conf.smtp_from_email;
    email_data.to_email.push_back(to_email);
    email_data.subject = subject;

    email_data.is_html = is_html;
    email_data.text = content;

    r = co_await client.send_email(email_data);
    if (!r) {
      CINATRA_LOG_ERROR << "邮件发送失败: " << to_email;
      co_return false;
    }

    CINATRA_LOG_INFO << "邮件发送成功: " << to_email;
    co_return true;
  } catch (const std::exception &e) {
    CINATRA_LOG_ERROR << "发送邮件时发生异常: " << e.what();
    co_return false;
  }
}

// 发送邮箱验证邮件
inline async_simple::coro::Lazy<bool>
send_verify_email(const std::string &email, const std::string &token) {
  auto &conf = purecpp_config::get_instance();
  auto &user_conf = conf.user_cfg_;
  // 构建验证链接
  std::string verify_link =
      user_conf.web_server_url + "/verify_email.html?token=" + token;

  try {
    // 构建邮件内容
    std::string email_content;
    email_content += "<html><body>";
    email_content += "<h3>邮箱验证</h3>";
    email_content += "<p>欢迎注册PureCpp！请点击以下链接完成邮箱验证：</p>";
    email_content +=
        "<a href=\"" + verify_link + "\">" + verify_link + "</a><br/><br/>";
    email_content += "<p>如果您没有注册PureCpp账号，请忽略此邮件。</p>";
    email_content += "<p>此链接有效期为24小时。</p>";
    email_content += "<p>感谢您使用PureCpp！</p>";
    email_content += "</body></html>";

    // 发送真实邮件
    bool r = co_await send_email(email, "PureCpp邮箱验证", email_content);
    co_return r;
  } catch (const std::exception &e) {
    CINATRA_LOG_ERROR << "发送邮箱验证邮件时发生异常: " << e.what();
    co_return false;
  }
}

// 发送密码重置邮件
inline async_simple::coro::Lazy<bool>
send_reset_email(const std::string &email, const std::string &token) {
  auto &conf = purecpp_config::get_instance();
  auto &user_conf = conf.user_cfg_;

  try {
    // 生成重置链接
    std::string reset_link =
        user_conf.web_server_url + "/reset_password.html?token=" + token;

    // 构建邮件内容
    std::string email_content;
    email_content += "<html><body>";
    email_content += "<h3>密码重置请求</h3>";
    email_content +=
        "<p>您请求重置您的PureCpp密码。请点击以下链接进行重置：</p>";
    email_content +=
        "<a href=\"" + reset_link + "\">" + reset_link + "</a><br/><br/>";
    email_content += "<p>如果您没有请求重置密码，请忽略此邮件。</p>";
    email_content += "<p>此链接有效期为1小时。</p>";
    email_content += "<p>感谢您使用PureCpp！</p>";
    email_content += "</body></html>";
    // 发送真实邮件
    bool r = co_await send_email(email, "PureCpp密码重置", email_content);
    co_return r;
  } catch (const std::exception &e) {
    CINATRA_LOG_ERROR << "发送密码重置邮件时发生异常: " << e.what();
    co_return false;
  }
}

/**
 * @brief 获取客户端IP地址
 */
inline std::string get_client_ip(coro_http_request &req) {
  // 优先从X-Forwarded-For获取（反向代理场景）
  auto forward_for = req.get_header_value("X-Forwarded-For");
  if (!forward_for.empty()) {
    // X-Forwarded-For可能包含多个IP，取第一个
    auto comma_pos = forward_for.find(',');
    if (comma_pos != std::string::npos) {
      return std::string(forward_for.substr(0, comma_pos));
    }
    return std::string(forward_for);
  }

  // 其次从X-Real-IP获取
  auto real_ip = req.get_header_value("X-Real-IP");
  if (!real_ip.empty()) {
    return std::string(real_ip);
  }

  // 从conn的连接中获取
  auto conn_ip = req.get_conn()->remote_address();
  if (!conn_ip.empty()) {
    // 获取:冒号前面的IP地址
    auto colon_pos = conn_ip.find(':');
    if (colon_pos != std::string::npos) {
      return std::string(conn_ip.substr(0, colon_pos));
    }
    return std::string(conn_ip);
  }

  // 如果都无法获取，返回默认值
  return "unknown";
}

inline uint64_t generate_user_id() {
  // 获取当前系统时间
  auto now = std::chrono::system_clock::now();

  // 转换为time_t类型，用于格式化年月日时分秒
  auto time_t_now = std::chrono::system_clock::to_time_t(now);

  // 转换为tm结构体，用于格式化
  auto tm_now = *std::localtime(&time_t_now);

  // 格式化年月日时分秒，格式为YYYYMMDDHHmmss
  char time_buf[20];
  std::strftime(time_buf, sizeof(time_buf), "%Y%m%d%H%M%S", &tm_now);

  // 获取毫秒部分
  auto duration = now.time_since_epoch();
  auto millisenconds =
      std::chrono::duration_cast<std::chrono::milliseconds>(duration);
  auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);

  // 格式化为3位毫秒，避免 snprintf 整数格式符的跨平台差异。
  auto mill_part = millisenconds - seconds;
  std::ostringstream mill_buf;
  mill_buf << std::setw(3) << std::setfill('0') << mill_part.count();

  // 组合时间和纳秒部分
  std::string id = time_buf;
  id += mill_buf.str();

  return std::stoull(id);
}

} // namespace purecpp
