#pragma once

#include "common.hpp"
#include "email_verify.hpp"
#include "md5.hpp"
#include "user_aspects.hpp"
#include "user_experience.hpp"
#include <cinatra/smtp_client.hpp>
#include <openssl/sha.h>
#include <regex>
#include <algorithm>

using namespace cinatra;

namespace purecpp {
inline std::string sha256_simple(std::string_view input) {
  unsigned char hash[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const unsigned char *>(input.data()), input.size(),
         hash);

  std::string hex(SHA256_DIGEST_LENGTH * 2, '\0');

  for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
    char buf[2];

    // 高4位
    std::to_chars(buf, buf + 1, hash[i] >> 4, 16);
    hex[i * 2] = buf[0];

    // 低4位
    std::to_chars(buf, buf + 1, hash[i] & 0x0F, 16);
    hex[i * 2 + 1] = buf[0];
  }

  return hex;
}

inline std::string password_encrypt(std::string_view password) {
  return sha256_simple(md5::md5_string(password.data()));
}

class user_register_t {
public:
  // 处理用户注册请求（改为异步方法）
  async_simple::coro::Lazy<void> handle_register(coro_http_request &req,
                                                 coro_http_response &resp) {
    register_info info = std::any_cast<register_info>(req.get_user_data());
    const auto &cfg = purecpp_config::get_instance().user_cfg_;

    // save to temporary database first
    users_tmp_t user_tmp{.id = generate_user_id(),
                         .is_verifyed = EmailVerifyStatus::UNVERIFIED,
                         .created_at = get_timestamp_milliseconds()};
    std::string pwd_sha = password_encrypt(info.password);
    user_tmp.pwd_hash = pwd_sha;
    // 安全地复制用户名，确保不超过缓冲区大小
    std::copy_n(info.username.begin(),
                (std::min)(info.username.size(), user_tmp.user_name.size() - 1),
                user_tmp.user_name.begin());
    user_tmp.user_name[user_tmp.user_name.size() - 1] = '\0';

    // 安全地复制邮箱，确保不超过缓冲区大小
    std::copy_n(info.email.begin(),
                (std::min)(info.email.size(), user_tmp.email.size() - 1),
                user_tmp.email.begin());
    user_tmp.email[user_tmp.email.size() - 1] = '\0';

    auto conn = get_db_pool().get();
    if (conn == nullptr) {
      set_server_internel_error(resp);
      co_return;
    }

    // 将用户数据插入到临时表
    auto result = conn->insert(user_tmp);
    if (result == 0) {
      auto err = conn->get_last_error();
      CINATRA_LOG_ERROR << err;
      resp.set_status_and_content(status_type::bad_request, make_error(err));
      co_return;
    }

    // 注册成功后，创建邮箱验证token
    auto [token_created, token] =
        email_verify_t::create_verify_token(user_tmp.id, info.email);

    if (!token_created) {
      CINATRA_LOG_ERROR << "创建邮箱验证token失败";
      resp.set_status_and_content(
          status_type::internal_server_error,
          make_error("注册成功，但发送验证邮件失败，请稍后手动验证"));
      co_return;
    }

    // 发送邮箱验证邮件
    bool email_result =
        co_await email_verify_t::send_verify_email(info.email, token);

    if (!email_result) {
      CINATRA_LOG_ERROR << "发送验证邮件失败";
      // 即使邮件发送失败，也返回注册成功，因为用户已创建成功
      std::string json = make_data(
          user_resp_data{user_tmp.id, info.username, info.email,
                         static_cast<int>(user_tmp.is_verifyed), 0,
                         "user", // 初始头衔和角色
                         0, 1},  // 初始经验值和等级
          "注册成功！请前往邮箱验证账号（如果未收到邮件，请检查垃圾邮件夹或重新"
          "发送验证邮件）");
      resp.set_status_and_content(status_type::ok, std::move(json));
      co_return;
    }

    std::string json =
        make_data(user_resp_data{user_tmp.id, info.username, info.email,
                                 static_cast<int>(user_tmp.is_verifyed), 0,
                                 "user", // 初始头衔和角色
                                 0, 1},  // 初始经验值和等级
                  "注册成功！请前往邮箱验证账号。");
    resp.set_status_and_content(status_type::ok, std::move(json));
  }

  // 处理邮箱验证请求
  static void handle_verify_email(coro_http_request &req,
                                  coro_http_response &resp) {
    verify_email_info info =
        std::any_cast<verify_email_info>(req.get_user_data());

    auto conn = get_db_pool().get();
    if (conn == nullptr) {
      set_server_internel_error(resp);
      return;
    }

    // 先获取token对应的用户ID，因为verify_email_token会删除token
    auto users_token = conn->select(ormpp::all)
                           .from<users_token_t>()
                           .where(col(&users_token_t::token).param() &&
                                  col(&users_token_t::token_type).param())
                           .collect(info.token, TokenType::VERIFY_EMAIL);

    if (users_token.empty()) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("无效或过期的token"));
      return;
    }

    auto user_id = users_token[0].user_id;

    // 验证token（会自动删除token）
    bool valid = email_verify_t::verify_email_token(info.token);
    if (!valid) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("无效或过期的token"));
      return;
    }

    // 查询临时表中的用户数据
    auto users_tmp = conn->select(ormpp::all)
                         .from<users_tmp_t>()
                         .where(col(&users_tmp_t::id).param())
                         .collect(user_id);
    if (users_tmp.empty()) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("用户不存在"));
      return;
    }

    auto user_tmp = users_tmp[0];
    const auto &cfg = purecpp_config::get_instance().user_cfg_;

    // 开启事务(先插入正式表，再删除临时表)
    conn->begin();

    // 创建正式用户数据，使用相同的用户id
    users_t user{.id = user_tmp.id, // 使用临时表中的用户id
                 .status = STATUS_OF_OFFLINE.data(),
                 .is_verifyed = EmailVerifyStatus::VERIFIED,
                 .created_at = user_tmp.created_at,
                 .last_active_at = get_timestamp_milliseconds(),
                 .experience = 0,             // 初始经验值
                 .level = UserLevel::LEVEL_1, // 初始等级
                 .avatar = cfg.default_avatar_url.data()};

    // 复制用户名和邮箱
    std::copy_n(std::begin(user_tmp.user_name), user_tmp.user_name.size(),
                std::begin(user.user_name));
    std::copy_n(std::begin(user_tmp.email), user_tmp.email.size(),
                std::begin(user.email));
    user.pwd_hash = user_tmp.pwd_hash;

    // 将用户数据插入到正式表
    auto insert_result = conn->insert(user);
    if (insert_result == 0) {
      conn->rollback();
      resp.set_status_and_content(status_type::internal_server_error,
                                  make_error("创建正式用户失败"));
      return;
    }

    // 删除临时表中的用户数据
    auto delete_result = conn->delete_records_s<users_tmp_t>("id = ?", user_id);
    if (delete_result == 0) {
      conn->rollback();
      resp.set_status_and_content(status_type::internal_server_error,
                                  make_error("删除临时用户数据失败"));
      return;
    }

    // 提交事务
    conn->commit();

    // 返回成功响应
    std::string json = make_success("邮箱验证成功！");
    resp.set_status_and_content(status_type::ok, std::move(json));
  }

  // 处理重新发送验证邮件请求
  static async_simple::coro::Lazy<void>
  handle_resend_verify_email(coro_http_request &req, coro_http_response &resp) {
    resend_verify_email_info info =
        std::any_cast<resend_verify_email_info>(req.get_user_data());

    // 查询数据库中是否已存在该邮箱的用户，先查临时表再查正式表
    auto conn = get_db_pool().get();
    if (conn == nullptr) {
      set_server_internel_error(resp);
      co_return;
    }

    // 先查询临时表
    auto users_tmp = conn->select(ormpp::all)
                         .from<users_tmp_t>()
                         .where(col(&users_tmp_t::email).param())
                         .collect(info.email);

    uint64_t user_id = 0;
    bool is_verified = false;
    bool found = false;

    if (!users_tmp.empty()) {
      // 临时表中找到用户
      user_id = users_tmp[0].id;
      is_verified = users_tmp[0].is_verifyed;
      found = true;
    } else {
      // 临时表中没有找到，查询正式表
      auto users = conn->select(ormpp::all)
                       .from<users_t>()
                       .where(col(&users_t::email).param())
                       .collect(info.email);

      if (!users.empty()) {
        user_id = users[0].id;
        is_verified = users[0].is_verifyed;
        found = true;
      }
    }

    if (!found) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("邮箱不存在"));
      co_return;
    }

    // 检查是否已经验证
    if (is_verified) {
      resp.set_status_and_content(status_type::ok,
                                  make_success("该邮箱已经验证"));
      co_return;
    }

    // 注册成功后，创建邮箱验证token
    auto [token_created, token] =
        email_verify_t::create_verify_token(user_id, info.email);

    if (!token_created) {
      CINATRA_LOG_ERROR << "创建邮箱验证token失败";
      resp.set_status_and_content(status_type::internal_server_error,
                                  make_error("发送邮件失败，请检查邮箱地址!"));
      co_return;
    }

    // 发送邮箱验证邮件
    bool email_result =
        co_await email_verify_t::send_verify_email(info.email, token);

    if (!email_result) {
      CINATRA_LOG_ERROR << "发送验证邮件失败";
      resp.set_status_and_content(status_type::internal_server_error,
                                  make_error("发送邮件失败，请检查邮箱地址!"));
      co_return;
    }

    // 返回成功响应
    std::string json = make_success("验证邮件已发送，请检查您的邮箱");
    resp.set_status_and_content(status_type::ok, std::move(json));
  }
};
} // namespace purecpp
