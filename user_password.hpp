#pragma once

#include "common.hpp"
#include "config.hpp"
#include "entity.hpp"
#include "user_register.hpp"
#include <cinatra.hpp>

using namespace cinatra;

namespace purecpp {

class user_password_t {
public:
  /**
   * @brief 处理用户修改密码请求
   *
   * @param req HTTP请求对象
   * @param resp HTTP响应对象
   */
  void handle_change_password(coro_http_request &req,
                              coro_http_response &resp) {
    change_password_info info =
        std::any_cast<change_password_info>(req.get_user_data());

    // 查询数据库
    auto conn = get_db_pool().get();
    if (conn == nullptr) {
      set_server_internel_error(resp);
      return;
    }

    // 根据用户ID查找用户
    auto users = conn->select(ormpp::all)
                     .from<users_t>()
                     .where(col(&users_t::id).param())
                     .collect(info.user_id);

    if (users.empty()) {
      // 用户不存在
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("用户不存在"));
      return;
    }

    users_t user = users[0];

    // 验证旧密码
    if (user.pwd_hash != password_encrypt(info.old_password)) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("旧密码错误"));
      return;
    }

    // 更新新密码
    std::string pwd_sha = password_encrypt(info.new_password);
    if (conn->update<users_t>()
            .set(col(&users_t::pwd_hash), pwd_sha)
            .where(col(&users_t::id) == user.id)
            .execute() != 1) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("修改密码失败"));
      return;
    }

    // 返回修改成功响应
    std::string json = make_success("密码修改成功");
    resp.set_status_and_content(status_type::ok, std::move(json));
  }

  // 处理忘记密码请求
  async_simple::coro::Lazy<void>
  handle_forgot_password(coro_http_request &req, coro_http_response &resp) {
    forgot_password_info info =
        std::any_cast<forgot_password_info>(req.get_user_data());

    // 查询数据库
    auto conn = get_db_pool().get();
    if (conn == nullptr) {
      set_server_internel_error(resp);
      co_return;
    }
    // 查找用户
    auto users = conn->select(ormpp::all)
                     .from<users_t>()
                     .where(col(&users_t::email).param())
                     .collect(info.email);
    if (users.empty()) {
      resp.set_status_and_content(status_type::ok,
                                  make_error("如果邮箱存在，重置链接已发送"));
      co_return;
    }

    users_t user = users[0];

    // 使用统一的token生成函数
    std::string token = generate_token(TokenType::RESET_PASSWORD);

    // 获取重置token的过期时间
    uint64_t expires_at = get_token_expires_at(TokenType::RESET_PASSWORD);

    // 保存token到数据库
    users_token_t reset_token{.id = 0,
                              .user_id = user.id,
                              .token_type = TokenType::RESET_PASSWORD,
                              .created_at = get_timestamp_milliseconds(),
                              .expires_at = expires_at};

    // 安全地复制token，确保不溢出并添加null终止符
    std::copy_n(token.begin(),
                (std::min)(token.size(), reset_token.token.size() - 1),
                reset_token.token.begin());
    reset_token.token[reset_token.token.size() - 1] = '\0';

    // 删除该用户之前的所有重置token
    conn->remove<users_token_t>()
        .where(col(&users_token_t::user_id) == user.id &&
               col(&users_token_t::token_type) == TokenType::RESET_PASSWORD)
        .execute();

    // 插入新的token
    uint64_t insert_id = conn->get_insert_id_after_insert(reset_token);
    if (insert_id == 0) {
      auto err = conn->get_last_error();
      CINATRA_LOG_ERROR << err;
      resp.set_status_and_content(status_type::internal_server_error,
                                  make_error("生成重置链接失败，请稍后重试"));
      co_return;
    }

    // 发送重置邮件（使用common.hpp中的通用函数）
    bool r = co_await send_reset_email(info.email, token);
    if (!r) {
      // 邮件发送失败，返回错误信息
      CINATRA_LOG_ERROR << "邮件发送失败: " << info.email;
      resp.set_status_and_content(status_type::internal_server_error,
                                  make_error("发送邮件失败，请稍后重试"));
      co_return;
    }

    // 返回成功响应
    std::string json = make_data(
        empty_data{}, "密码重置链接已发送,请检查您的邮箱并完成后续操作");
    resp.set_status_and_content(status_type::ok, std::move(json));
  }

  // 处理密码重置请求
  void handle_reset_password(coro_http_request &req, coro_http_response &resp) {
    reset_password_info info =
        std::any_cast<reset_password_info>(req.get_user_data());

    // 查询数据库
    auto conn = get_db_pool().get();
    if (conn == nullptr) {
      set_server_internel_error(resp);
      return;
    }

    // 查找token
    auto tokens = conn->select(ormpp::all)
                      .from<users_token_t>()
                      .where(col(&users_token_t::token).param())
                      .collect(info.token);
    if (tokens.empty()) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("重置密码链接无效或已过期"));
      return;
    }

    users_token_t reset_token = tokens[0];

    // 检查token是否过期
    uint64_t now = get_timestamp_milliseconds();
    if (now > reset_token.expires_at) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("重置密码链接已过期"));
      return;
    }

    // 查找用户
    auto users = conn->select(ormpp::all)
                     .from<users_t>()
                     .where(col(&users_t::id).param())
                     .collect(reset_token.user_id);
    if (users.empty()) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("用户不存在"));
      return;
    }

    users_t user = users[0];

    // 更新用户密码
    std::string pwd_hash = purecpp::password_encrypt(info.new_password);
    if (conn->update<users_t>()
            .set(col(&users_t::pwd_hash), pwd_hash)
            .set(col(&users_t::login_attempts), 0)
            .set(col(&users_t::last_failed_login), 0)
            .where(col(&users_t::id) == user.id)
            .execute() != 1) {
      auto err = conn->get_last_error();
      CINATRA_LOG_ERROR << err;
      resp.set_status_and_content(status_type::internal_server_error,
                                  make_error("重置密码失败，请稍后重试"));
      return;
    }
    // 删除该用户之前的所有重置token
    conn->remove<users_token_t>()
        .where(col(&users_token_t::user_id) == user.id &&
               col(&users_token_t::token_type) == TokenType::RESET_PASSWORD)
        .execute();

    // 返回成功响应
    std::string json = make_success("密码重置成功");
    resp.set_status_and_content(status_type::ok, std::move(json));
  }
};
} // namespace purecpp
