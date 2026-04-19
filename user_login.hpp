#pragma once

#include "entity.hpp"
#include "error_info.hpp"
#include "jwt_token.hpp"
#include "user_aspects.hpp"
#include "user_dto.hpp"
#include "user_register.hpp"
#include <algorithm>

using namespace cinatra;

namespace purecpp {
// 前向声明（已经在jwt_token.hpp中定义）
class token_blacklist;

class user_login_t {
public:
  /**
   * @brief 处理用户登录请求
   *
   * @param req HTTP请求对象
   * @param resp HTTP响应对象
   */
  void handle_login(coro_http_request &req, coro_http_response &resp) {
    // 移除可能导致崩溃的全局语言环境设置
    login_info info = std::any_cast<login_info>(req.get_user_data());

    // 查询数据库
    auto conn = get_db_pool().get();
    if (conn == nullptr) {
      set_server_internel_error(resp);
      return;
    }

    // 一次查询用户名和邮箱，减少数据库连接次数
    auto users = conn->select(ormpp::all)
                     .from<users_t>()
                     .where(col(&users_t::user_name).param() ||
                            col(&users_t::email).param())
                     .collect(info.username, info.username);

    users_t user{};
    bool found = false;

    // 如果找到用户
    if (!users.empty()) {
      user = users[0];
      found = true;
    }

    if (!found) {
      // 用户不存在
      resp.set_status_and_content(status_type::bad_request,
                                  make_error(PURECPP_ERROR_LOGIN_FAILED));
      return;
    }

    // 检查用户是否被锁定
    const uint32_t MAX_LOGIN_ATTEMPTS = 5;
    const uint64_t LOCK_DURATION = 10 * 60 * 1000; // 10分钟，单位毫秒
    const uint64_t current_time = get_timestamp_milliseconds();

    if (user.login_attempts >= MAX_LOGIN_ATTEMPTS) {
      // 检查锁定时间是否已过
      if (current_time - user.last_failed_login < LOCK_DURATION) {
        // 用户仍处于锁定状态
        uint64_t remaining_time =
            LOCK_DURATION - (current_time - user.last_failed_login);
        uint64_t remaining_minutes = remaining_time / (60 * 1000);
        uint64_t remaining_seconds = (remaining_time % (60 * 1000)) / 1000;

        std::string message = "登录失败次数过多，账号已被锁定。请在" +
                              std::to_string(remaining_minutes) + "分钟" +
                              std::to_string(remaining_seconds) + "秒后重试。";
        resp.set_status_and_content(status_type::bad_request,
                                    make_error(message));
        return;
      } else {
        // 锁定时间已过，重置失败次数
        user.login_attempts = 0;
      }
    }

    // 验证密码
    if (user.pwd_hash != purecpp::password_encrypt(info.password)) {
      // 密码错误，更新失败次数和最后失败时间
      // 保存更新到数据库
      if (conn->update<users_t>()
              .set(col(&users_t::login_attempts), user.login_attempts + 1)
              .set(col(&users_t::last_failed_login), current_time)
              .where(col(&users_t::id) == user.id)
              .execute() != 1) {
        resp.set_status_and_content(status_type::bad_request,
                                    make_error(PURECPP_ERROR_LOGIN_FAILED));
        return;
      }

      // 检查是否需要锁定账号
      if (user.login_attempts >= MAX_LOGIN_ATTEMPTS) {
        resp.set_status_and_content(
            status_type::bad_request,
            make_error("登录失败次数过多，账号已被锁定10分钟。"));
        return;
      }

      // 返回登录失败信息
      resp.set_status_and_content(status_type::bad_request,
                                  make_error(PURECPP_ERROR_LOGIN_FAILED));
      return;
    }

    // 安全地将std::array转换为std::string
    
    std::string user_name_str = array_to_string(user.user_name);
    std::string email_str = array_to_string(user.email);

    // 生成JWT token和refresh token
    token_response token_resp =
        generate_jwt_token(user.id, user_name_str, email_str);

    // 登录成功，更新状态
    if (conn->update<users_t>()
            .set(col(&users_t::login_attempts), 0)
            .set(col(&users_t::status), std::string(STATUS_OF_ONLINE))
            .set(col(&users_t::last_active_at),
                 get_timestamp_milliseconds())
            .where(col(&users_t::id) == user.id)
            .execute() != 1) {
      // 更新失败报错
      resp.set_status_and_content(status_type::bad_request,
                                  make_error(PURECPP_ERROR_LOGIN_FAILED));
      return;
    }

    // 返回登录成功响应
    std::string json = make_data(
        login_resp_data{user.id, user_name_str, email_str,
                        token_resp.access_token, token_resp.refresh_token,
                        token_resp.access_token_expires_at,
                        token_resp.refresh_token_expires_at,
                        token_resp.access_token_lifetime, user.title, user.role,
                        user.avatar, user.experience, user.level},
        std::string(PURECPP_LOGIN_SUCCESS));
    resp.set_status_and_content(status_type::ok, std::move(json));
  }

  /**
   * @brief 处理刷新token请求
   *
   * @param req HTTP请求对象
   * @param resp HTTP响应对象
   */
  void handle_refresh_token(cinatra::coro_http_request &req,
                            cinatra::coro_http_response &resp) {
    try {
      // 从请求中获取刷新令牌信息
      refresh_token_request refresh_info =
          std::any_cast<refresh_token_request>(req.get_user_data());

      // 刷新token，传入user_id进行校验
      token_response new_token_resp = refresh_access_token(
          refresh_info.refresh_token, refresh_info.user_id);

      // 返回新的token响应
      refresh_token_response resp_data;
      resp_data.user_id = refresh_info.user_id;
      resp_data.token = new_token_resp.access_token;
      resp_data.refresh_token = new_token_resp.refresh_token;
      resp_data.access_token_expires_at =
          new_token_resp.access_token_expires_at;
      resp_data.access_token_lifetime = new_token_resp.access_token_lifetime;
      resp_data.refresh_token_expires_at =
          new_token_resp.refresh_token_expires_at;

      std::string json =
          make_data(resp_data, std::string("Token refreshed successfully"));
      resp.set_status_and_content(status_type::ok, std::move(json));
    } catch (const std::exception &e) {
      resp.set_status_and_content(
          status_type::bad_request,
          make_error(std::string("Failed to refresh token: ") + e.what()));
    }
  }

  /**
   * @brief 处理用户退出登录请求
   *
   * @param req HTTP请求对象
   * @param resp HTTP响应对象
   */
  void handle_logout(cinatra::coro_http_request &req,
                     cinatra::coro_http_response &resp) {
    logout_info info = std::any_cast<logout_info>(req.get_user_data());
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

    // 如果请求头中没有令牌，尝试从查询参数获取
    if (token.empty()) {
      auto token_param = req.get_query_value("token");
      if (!token_param.empty()) {
        token = std::string(token_param);
      }
    }

    // 如果没有令牌，直接返回成功
    if (token.empty()) {
      resp.set_status_and_content(cinatra::status_type::ok,
                                  make_success("退出登录成功"));
      return;
    }

    // 将令牌添加到黑名单
    token_blacklist::instance().add(token);

    // 修改用户状态为登出
    // 从数据库中查询用户
    auto conn = get_db_pool().get();
    if (conn == nullptr) {
      set_server_internel_error(resp);
      return;
    }

    auto users_by_id = conn->select(ormpp::all)
                           .from<users_t>()
                           .where(col(&users_t::id).param())
                           .collect(info.user_id);

    if (users_by_id.empty()) {
      resp.set_status_and_content(
          cinatra::status_type::bad_request,
          make_error(PURECPP_ERROR_LOGOUT_USER_ID_INVALID));
      return;
    }

    // 更新用户状态为登出
    auto user = users_by_id[0];
    if (conn->update<users_t>()
            .set(col(&users_t::status), std::string(STATUS_OF_OFFLINE))
            .where(col(&users_t::id) == user.id)
            .execute() != 1) {
      resp.set_status_and_content(cinatra::status_type::bad_request,
                                  make_error(PURECPP_ERROR_LOGOUT_FAILED));
      return;
    }

    // 返回成功响应
    resp.set_status_and_content(cinatra::status_type::ok,
                                make_success("退出登录成功"));
  }
};
} // namespace purecpp
