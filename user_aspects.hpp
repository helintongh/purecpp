#pragma once
#include "common.hpp"
#include "entity.hpp"
#include "error_info.hpp"
#include "jwt_token.hpp"
#include "rate_limiter.hpp"
#include "user_dto.hpp"
#include <any>
#include <chrono>
#include <iomanip>
#include <regex>
#include <sstream>
#include <string_view>
#include <system_error>

#include <cinatra.hpp>
#include <iguana/json_reader.hpp>
#include <iguana/json_writer.hpp>

using namespace cinatra;
using namespace iguana;

namespace purecpp {
inline const std::vector<std::string_view> cpp_questions{
    "C++中声明指向int的常量指针, 语法是____ int* "
    "p。(请把空白部分的代码补充完整)",
    "sizeof(uint64_t)的返回值是?",
    "请输入C++中共享的智能指针。std::____ (请把空白部分的代码补充完整)",
    "请输入C++中独占的智能指针。std::____ (请把空白部分的代码补充完整)",
    "auto foo(){return new int(1);} void "
    "call_foo(){foo();} 这个call_foo函数有什么问题? ",
    "std::string str; str.reserve(100); 这个str的长度是多少?"};

inline const std::vector<std::string_view> cpp_answers{
    "const", "8", "shared_ptr", "unique_ptr", "内存泄漏", "0"};

struct check_register_input {
  bool before(coro_http_request &req, coro_http_response &res) {
    auto body = req.get_body();
    if (body.empty()) {
      res.set_status_and_content(status_type::bad_request,
                                 make_error("register info is empty"));
      return false;
    }

    register_info info{};
    std::error_code ec;
    iguana::from_json(info, body, ec);
    if (ec) {
      res.set_status_and_content(
          status_type::bad_request,
          make_error("register info is not a required json"));
      return false;
    }

    req.set_user_data(info);
    return true;
  }
};

struct check_cpp_answer {
  bool before(coro_http_request &req, coro_http_response &res) {
    register_info info = std::any_cast<register_info>(req.get_user_data());
    bool r = cpp_answers[info.question_index] == info.cpp_answer;

    if (!r) {
      res.set_status_and_content(status_type::bad_request,
                                 make_error("问题的答案不对。"));
      return false;
    }
    return true;
  }
};

std::string cleanup_markdown(const std::string &markdown_text) {
  std::string text = markdown_text;

  // 1. 清理链接和图片 (保留链接文本)
  // 匹配 [text](url) 或 ![text](url)
  text = std::regex_replace(text, std::regex("!\\[(.*?)\\]\\(.*?\\)"),
                            "$1"); // 图片
  text = std::regex_replace(text, std::regex("\\[(.*?)\\]\\(.*?\\)"),
                            "$1"); // 链接

  // 2. 清理粗体和斜体 (**bold** or *italic*)
  // 匹配 **...** 和 *...*
  text = std::regex_replace(text, std::regex("(\\*\\*|__)(.*?)\\1"), "$2");
  text = std::regex_replace(text, std::regex("(\\*|_)(.*?)\\1"), "$2");

  // 3. 清理代码块和行内代码 (```code``` or `code`)
  text = std::regex_replace(text, std::regex("```[\\s\\S]*?```"),
                            "");                                // 移除代码块
  text = std::regex_replace(text, std::regex("`(.*?)`"), "$1"); // 行内代码

  // 4. 清理标题 (# H1, ## H2, etc.)
  text = std::regex_replace(text, std::regex("^#+\\s*"), "");

  // 5. 清理列表和引用 (> quote)
  text = std::regex_replace(text, std::regex("^[*-+]\\s"), ""); // 列表
  text = std::regex_replace(text, std::regex("^>\\s"), "");     // 引用

  // 6. 最终清理多余的换行和空格
  // 替换多个换行符为一个空格（将段落连起来）
  text = std::regex_replace(text, std::regex("\\n+"), " ");

  return text;
}

struct check_user_name {
  bool before(coro_http_request &req, coro_http_response &res) {
    register_info info = std::any_cast<register_info>(req.get_user_data());
    if (info.username.empty() || info.username.size() > 20) {
      res.set_status_and_content(status_type::bad_request,
                                 make_error("用户名长度非法应改为1-20。"));
      return false;
    }

    const std::regex username_regex("^[a-zA-Z0-9_-]+$");

    bool r = std::regex_match(std::string(info.username), username_regex);
    if (!r) {
      res.set_status_and_content(status_type::bad_request,
                                 make_error("用户名只允许字母 (a-z, A-Z), 数字 "
                                            "(0-9), 下划线 (_), 连字符 (-)。"));
      return false;
    }
    return true;
  }
};

std::pair<bool, std::string> validate_email_format(std::string_view email) {
  if (email.empty() || email.size() > 254) {
    return {false, "邮箱格式不合法。"};
  }

  static const std::regex email_regex(
      R"([a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,})");
  bool r = std::regex_match(std::string{email}, email_regex);

  if (!r) {
    return {false, "邮箱格式不合法。"};
  }
  return {true, ""};
}

struct check_email {
  bool before(coro_http_request &req, coro_http_response &res) {
    register_info info = std::any_cast<register_info>(req.get_user_data());
    auto [valid, error_msg] = validate_email_format(info.email);
    if (!valid) {
      res.set_status_and_content(status_type::bad_request,
                                 make_error(error_msg));
      return false;
    }
    return true;
  }
};

// 验证密码复杂度的独立函数
std::pair<bool, std::string>
validate_password_complexity(const std::string &password) {
  // 检查密码长度
  if (password.size() < 6 || password.size() > 20) {
    return {false, "密码长度不合法，长度6-20位。"};
  }

  bool has_upper = false;
  bool has_lower = false;
  bool has_digit = false;

  // 检查字符类型要求
  for (char c : password) {
    if (std::isupper(static_cast<unsigned char>(c))) {
      has_upper = true;
    } else if (std::islower(static_cast<unsigned char>(c))) {
      has_lower = true;
    } else if (std::isdigit(static_cast<unsigned char>(c))) {
      has_digit = true;
    }
  }

  // 至少包含大小写字母和数字
  if (!(has_upper && has_lower && has_digit)) {
    return {false, "密码至少包含大小写字母和数字。"};
  }

  return {true, ""};
}

struct check_password {
  bool before(coro_http_request &req, coro_http_response &res) {
    register_info info = std::any_cast<register_info>(req.get_user_data());
    auto [valid, error_msg] = validate_password_complexity(info.password);
    if (!valid) {
      res.set_status_and_content(status_type::bad_request,
                                 make_error(error_msg));
      return false;
    }
    return true;
  }
};

struct check_user_exists {
  bool before(coro_http_request &req, coro_http_response &res) {
    register_info info = std::any_cast<register_info>(req.get_user_data());

    auto conn = get_db_pool().get();
    if (conn == nullptr) {
      res.set_status_and_content(status_type::internal_server_error,
                                 make_error("获取数据库连接失败"));
      return false;
    }

    // 检查用户名是否已存在于临时表
    auto users_tmp_by_username =
        conn->select(ormpp::count())
            .from<users_tmp_t>()
            .where(col(&users_tmp_t::user_name).param())
            .collect(std::string(info.username));
    if (users_tmp_by_username > 0) {
      res.set_status_and_content(status_type::bad_request,
                                 make_error("用户名或邮箱已被注册"));
      return false;
    }

    // 检查邮箱是否已存在于临时表
    auto users_tmp_by_email = conn->select(ormpp::count())
                                  .from<users_tmp_t>()
                                  .where(col(&users_tmp_t::email).param())
                                  .collect(std::string(info.email));
    if (users_tmp_by_email > 0) {
      res.set_status_and_content(status_type::bad_request,
                                 make_error("用户名或邮箱已被注册"));
      return false;
    }

    // 检查用户名是否已存在于正式表
    auto users_by_username = conn->select(ormpp::count())
                                 .from<users_t>()
                                 .where(col(&users_t::user_name).param())
                                 .collect(std::string(info.username));
    if (users_by_username > 0) {
      res.set_status_and_content(status_type::bad_request,
                                 make_error("用户名或邮箱已被注册"));
      return false;
    }

    // 检查邮箱是否已存在于正式表
    auto users_by_email = conn->select(ormpp::count())
                              .from<users_t>()
                              .where(col(&users_t::email).param())
                              .collect(std::string(info.email));
    if (users_by_email > 0) {
      res.set_status_and_content(status_type::bad_request,
                                 make_error("用户名或邮箱已被注册"));
      return false;
    }

    return true;
  }
};

// 登录相关的验证结构体
struct check_login_input {
  bool before(coro_http_request &req, coro_http_response &res) {
    auto body = req.get_body();
    if (body.empty()) {
      res.set_status_and_content(status_type::bad_request,
                                 make_error(PURECPP_ERROR_LOGIN_INFO_EMPTY));
      return false;
    }

    login_info info{};
    std::error_code ec;
    iguana::from_json(info, body, ec);
    if (ec) {
      res.set_status_and_content(status_type::bad_request,
                                 make_error(PURECPP_ERROR_LOGIN_JSON_INVALID));
      return false;
    }
    // 校验用户名、密码不能为空
    if (info.username.empty() || info.password.empty()) {
      res.set_status_and_content(
          status_type::bad_request,
          make_error(PURECPP_ERROR_LOGIN_CREDENTIALS_EMPTY));
      return false;
    }

    req.set_user_data(info);
    return true;
  }
};

struct check_logout_input {
  bool before(coro_http_request &req, coro_http_response &res) {
    auto body = req.get_body();
    if (body.empty()) {
      res.set_status_and_content(status_type::bad_request,
                                 make_error(PURECPP_ERROR_LOGOUT_INFO_EMPTY));
      return false;
    }

    logout_info info{};
    std::error_code ec;
    iguana::from_json(info, body, ec);
    if (ec) {
      res.set_status_and_content(status_type::bad_request,
                                 make_error(PURECPP_ERROR_LOGOUT_JSON_INVALID));
      return false;
    }

    // 校验user_id不能为空
    if (info.user_id == 0) {
      res.set_status_and_content(
          status_type::bad_request,
          make_error(PURECPP_ERROR_LOGOUT_USER_ID_EMPTY));
      return false;
    }

    req.set_user_data(info);
    return true;
  }
};

// Token验证结构体
struct check_token {
  bool before(coro_http_request &req, coro_http_response &res) {
    // 从请求头获取令牌
    std::string token;
    auto headers = req.get_headers();
    for (auto &header : headers) {
      if (cinatra::iequal0(header.name, "Authorization")) {
        // 提取Bearer令牌
        std::string_view auth_header = header.value;
        if (auth_header.size() > 7 && auth_header.substr(0, 7) == "Bearer ") {
          token = std::string(auth_header.substr(7));
          break;
        }
      }
    }

    // 检查令牌是否存在
    if (token.empty()) {
      res.set_status_and_content(status_type::unauthorized,
                                 make_error(PURECPP_ERROR_TOKEN_MISSING));
      return false;
    }

    // 验证令牌
    auto [result, info] = validate_jwt_token(token);

    if (result != TokenValidationResult::Valid) {
      // 令牌无效，返回错误信息
      std::string error_msg;
      switch (result) {
      case TokenValidationResult::InvalidFormat:
        error_msg = PURECPP_ERROR_TOKEN_INVALID;
        break;
      case TokenValidationResult::InvalidBase64:
        error_msg = PURECPP_ERROR_TOKEN_INVALID;
        break;
      case TokenValidationResult::InvalidSignature:
        error_msg = PURECPP_ERROR_TOKEN_INVALID;
        break;
      case TokenValidationResult::Expired:
        error_msg = PURECPP_ERROR_TOKEN_EXPIRED;
        break;
      default:
        error_msg = PURECPP_ERROR_TOKEN_INVALID;
      }

      res.set_status_and_content(status_type::unauthorized,
                                 make_error(error_msg));
      return false;
    }
    // 将token信息保存到切面中
    std::string payload;
    iguana::to_json(info, payload);
    req.params_["user_token"] = payload;
    return true;
  }
};

// 修改密码相关的验证结构体
struct check_change_password_input {
  bool before(coro_http_request &req, coro_http_response &res) {
    auto body = req.get_body();
    if (body.empty()) {
      res.set_status_and_content(
          status_type::bad_request,
          make_error(PURECPP_ERROR_CHANGE_PASSWORD_EMPTY));
      return false;
    }

    change_password_info info{};
    std::error_code ec;
    iguana::from_json(info, body, ec);
    if (ec) {
      res.set_status_and_content(
          status_type::bad_request,
          make_error(PURECPP_ERROR_CHANGE_PASSWORD_JSON_INVALID));
      return false;
    }

    // 校验用户ID、旧密码、新密码不能为空
    if (info.user_id == 0 || info.old_password.empty() ||
        info.new_password.empty()) {
      res.set_status_and_content(
          status_type::bad_request,
          make_error(PURECPP_ERROR_CHANGE_PASSWORD_REQUIRED_FIELDS));
      return false;
    }

    req.set_user_data(info);
    return true;
  }
};

inline constexpr std::array<std::string_view, 6> ALLOWED_EXTENSIONS = {
    ".jpg", ".jpeg", ".png", ".gif", ".pdf", ".txt"};

inline size_t MAX_FILE_SIZE = 1024 * 1024 * 4;

struct upload_file_info {
  uint64_t user_id;
  std::string_view file_data;
  std::string_view filename;
};

struct check_upload_file {
  bool before(coro_http_request &req, coro_http_response &res) {
    auto body = req.get_body();
    if (body.empty()) {
      res.set_status_and_content(status_type::bad_request,
                                 make_error(PURECPP_ERROR_UPLOAD_FILE_EMPTY));
      return false;
    }

    upload_file_info info{};
    std::error_code ec;
    iguana::from_json(info, body, ec);
    if (ec) {
      res.set_status_and_content(
          status_type::bad_request,
          make_error(PURECPP_ERROR_UPLOAD_FILE_JSON_INVALID));
      return false;
    }

    // if (info.file_data.size() > MAX_FILE_SIZE) {
    //   res.set_status_and_content(
    //       status_type::bad_request,
    //       make_error(PURECPP_ERROR_UPLOAD_FILE_SIZE_EXCEED));
    //   return false;
    // }

    std::string ext(cinatra::get_extension(info.filename));
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (std::find(ALLOWED_EXTENSIONS.begin(), ALLOWED_EXTENSIONS.end(), ext) ==
        ALLOWED_EXTENSIONS.end()) {
      res.set_status_and_content(
          status_type::bad_request,
          make_error(PURECPP_ERROR_UPLOAD_FILE_INVALID_EXTENSION));
      return false;
    }

    if (contains_dangerous_content(info.file_data, ext)) {
      res.set_status_and_content(
          status_type::bad_request,
          make_error(PURECPP_ERROR_UPLOAD_FILE_INVALID_CONTENT));
      return false;
    }

    req.set_user_data(info);
    return true;
  }

  bool contains_dangerous_content(std::string_view content,
                                  std::string_view ext) {
    // 对于文本文件，检查危险代码
    if (ext == "txt" || ext == "pdf") {
      // 防止 PHP 代码注入
      if (content.find("<?php") != std::string::npos ||
          content.find("<?=") != std::string::npos ||
          content.find("eval(") != std::string::npos ||
          content.find("base64_decode(") != std::string::npos) {
        return true;
      }
    }

    // 防止 Web Shell 特征
    std::vector<std::string> dangerousPatterns = {
        "system(", "exec(",     "popen(", "shell_exec(", "passthru(",
        "`",       "&&",        "||",     ";",           "|",
        "/bin/sh", "/bin/bash", "cmd.exe"};

    for (const auto &pattern : dangerousPatterns) {
      if (content.find(pattern) != std::string::npos) {
        return true;
      }
    }

    return false;
  }
};

struct check_new_password {
  bool before(coro_http_request &req, coro_http_response &res) {
    change_password_info info =
        std::any_cast<change_password_info>(req.get_user_data());

    // 验证新密码复杂度
    auto [valid, error_msg] = validate_password_complexity(info.new_password);
    if (!valid) {
      // 使用预定义的错误码
      if (error_msg.find("长度") != std::string::npos) {
        res.set_status_and_content(status_type::bad_request,
                                   make_error(PURECPP_ERROR_PASSWORD_LENGTH));
      } else {
        res.set_status_and_content(
            status_type::bad_request,
            make_error(PURECPP_ERROR_PASSWORD_COMPLEXITY));
      }
      return false;
    }

    // 新密码不能与旧密码相同
    if (info.new_password == info.old_password) {
      res.set_status_and_content(
          status_type::bad_request,
          make_error(PURECPP_ERROR_PASSWORD_NEW_SAME_AS_OLD));
      return false;
    }

    return true;
  }
};

// 忘记密码相关的验证结构体
struct check_forgot_password_input {
  bool before(coro_http_request &req, coro_http_response &res) {
    auto body = req.get_body();
    if (body.empty()) {
      res.set_status_and_content(status_type::bad_request,
                                 make_error(PURECPP_ERROR_EMAIL_EMPTY));
      return false;
    }

    forgot_password_info info{};
    std::error_code ec;
    iguana::from_json(info, body, ec);
    if (ec) {
      res.set_status_and_content(
          status_type::bad_request,
          make_error(PURECPP_ERROR_FORGOT_PASSWORD_JSON_INVALID));
      return false;
    }

    // 校验邮箱不能为空
    if (info.email.empty()) {
      res.set_status_and_content(status_type::bad_request,
                                 make_error(PURECPP_ERROR_EMAIL_EMPTY));
      return false;
    }

    req.set_user_data(info);
    return true;
  }
};

// 重置密码相关的验证结构体
struct check_reset_password_input {
  bool before(coro_http_request &req, coro_http_response &res) {
    auto body = req.get_body();
    if (body.empty()) {
      res.set_status_and_content(
          status_type::bad_request,
          make_error(PURECPP_ERROR_RESET_PASSWORD_EMPTY));
      return false;
    }

    reset_password_info info{};
    std::error_code ec;
    iguana::from_json(info, body, ec);
    if (ec) {
      res.set_status_and_content(
          status_type::bad_request,
          make_error(PURECPP_ERROR_RESET_PASSWORD_JSON_INVALID));
      return false;
    }

    // 校验token和新密码不能为空
    if (info.token.empty() || info.new_password.empty()) {
      res.set_status_and_content(
          status_type::bad_request,
          make_error(PURECPP_ERROR_RESET_PASSWORD_REQUIRED_FIELDS));
      return false;
    }

    req.set_user_data(info);
    return true;
  }
};

// 重置密码时的密码验证
struct check_reset_password {
  bool before(coro_http_request &req, coro_http_response &res) {
    reset_password_info info =
        std::any_cast<reset_password_info>(req.get_user_data());

    // 验证新密码复杂度
    auto [valid, error_msg] = validate_password_complexity(info.new_password);
    if (!valid) {
      // 使用预定义的错误码
      if (error_msg.find("长度") != std::string::npos) {
        res.set_status_and_content(status_type::bad_request,
                                   make_error(PURECPP_ERROR_PASSWORD_LENGTH));
      } else {
        res.set_status_and_content(
            status_type::bad_request,
            make_error(PURECPP_ERROR_PASSWORD_COMPLEXITY));
      }
      return false;
    }

    return true;
  }
};

// 日志切面工具
struct log_request_response {
  const int print_size = 150;
  // 在请求处理前记录请求信息
  bool before(coro_http_request &req, coro_http_response &res) {
    // 记录请求信息
    auto body = req.get_body();
    std::string sBody =
        (body.size() > print_size ? std::string(body.substr(0, print_size))
                                  : std::string(body));
    CINATRA_LOG_INFO << "[REQUEST ]" << req.get_method() << req.full_url()
                     << " Body:" << sBody;
    return true; // 继续处理请求
  }

  // 在请求处理后记录响应信息
  bool after(coro_http_request &req, coro_http_response &res) {
    // 记录响应信息（只记录前1000个字符以避免日志过长）
    auto body = res.content();
    std::string sBody =
        (body.size() > print_size ? std::string(body.substr(0, print_size))
                                  : std::string(body));
    CINATRA_LOG_INFO << "[ RESPONSE ]" << req.get_method() << req.full_url()
                     << " Status: " << static_cast<int>(res.status())
                     << " Body:" << sBody;
    return true; // 继续处理后续操作
  }
};

struct edit_article_info {
  std::string slug;
  std::string title;
  std::string excerpt; // 摘要
  std::string content;
  std::string tag_ids;
  std::string username;
};

inline bool has_login(std::string_view username, coro_http_response &resp) {
  auto conn = get_db_pool().get();
  if (conn == nullptr) {
    set_server_internel_error(resp);
    return false;
  }

  auto c = conn->select(count(col(&users_t::user_name)))
               .from<users_t>()
               .where(col(&users_t::user_name).param() &&
                      col(&users_t::status) == std::string(STATUS_OF_ONLINE))
               .collect(username);
  if (c == 0) {
    resp.set_status_and_content(
        status_type::bad_request,
        make_error(PURECPP_ERROR_USER_NOT_EXSIT_OR_LOGIN));

    return false;
  }
  return true;
}

struct check_edit_article {
  bool before(coro_http_request &req, coro_http_response &resp) {
    auto body = req.get_body();
    if (body.empty()) {
      resp.set_status_and_content(
          status_type::bad_request,
          make_error(PURECPP_ERROR_EDIT_ARTICLE_REQUIRED_FIELDS));
      return false;
    }
    edit_article_info info{};
    std::error_code ec;
    iguana::from_json(info, body, ec);
    if (ec) {
      resp.set_status_and_content(
          status_type::bad_request,
          make_error(PURECPP_ERROR_INVALID_EDIT_ARTICLE_INFO));
      return false;
    }

    if (!has_login(info.username, resp)) {
      return false;
    }

    req.set_user_data(info);
    return true;
  }
};

// 邮箱验证相关的验证结构体
struct check_verify_email_input {
  bool before(coro_http_request &req, coro_http_response &res) {
    auto body = req.get_body();
    if (body.empty()) {
      res.set_status_and_content(status_type::bad_request,
                                 make_error("邮箱验证信息不能为空"));
      return false;
    }

    verify_email_info info{};
    std::error_code ec;
    iguana::from_json(info, body, ec);
    if (ec) {
      res.set_status_and_content(status_type::bad_request,
                                 make_error("邮箱验证信息格式错误"));
      return false;
    }

    // 校验token不能为空
    if (info.token.empty()) {
      res.set_status_and_content(status_type::bad_request,
                                 make_error("验证令牌不能为空"));
      return false;
    }

    req.set_user_data(info);
    return true;
  }
};

struct check_resend_verification_input {
  bool before(coro_http_request &req, coro_http_response &res) {
    auto body = req.get_body();
    if (body.empty()) {
      res.set_status_and_content(status_type::bad_request,
                                 make_error("重新发送验证邮件信息不能为空"));
      return false;
    }
    // 获取入参
    resend_verify_email_info info{};
    std::error_code ec;
    iguana::from_json(info, body, ec);
    if (ec) {
      res.set_status_and_content(status_type::bad_request,
                                 make_error("重新发送验证邮件信息格式错误"));
      return false;
    }

    // 校验邮箱格式
    auto [valid, error_msg] = validate_email_format(info.email);
    if (!valid) {
      res.set_status_and_content(status_type::bad_request,
                                 make_error(error_msg));
      return false;
    }

    req.set_user_data(info);
    return true;
  }
};

// 频次检查切面
struct rate_limiter_aspect {
  bool before(coro_http_request &req, coro_http_response &res) {
    // 限流检查
    if (!check_rate_limit(req, res)) {
      return false; // 请求被限流，停止处理
    }
    return true;
  }
};

// 刷新token请求校验
struct check_refresh_token_input {
  bool before(coro_http_request &req, coro_http_response &res) {
    auto body = req.get_body();
    if (body.empty()) {
      res.set_status_and_content(status_type::bad_request,
                                 make_error("刷新令牌信息不能为空"));
      return false;
    }

    refresh_token_request info{};
    std::error_code ec;
    iguana::from_json(info, body, ec);
    if (ec) {
      res.set_status_and_content(status_type::bad_request,
                                 make_error("刷新令牌信息格式错误"));
      return false;
    }

    // 校验refresh token不能为空
    if (info.refresh_token.empty()) {
      res.set_status_and_content(status_type::bad_request,
                                 make_error("刷新令牌不能为空"));
      return false;
    }

    req.set_user_data(info);
    return true;
  }
};

} // namespace purecpp
