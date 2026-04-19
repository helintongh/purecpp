#pragma once

#include "common.hpp"
#include "entity.hpp"
#include "user_dto.hpp"
#include "user_experience.hpp"
#include <cinatra.hpp>
#include <iguana/json_reader.hpp>

using namespace cinatra;
using namespace purecpp;

namespace purecpp {

struct experience_reward_response_data {
  uint64_t user_id;
};

struct experience_reward_response {
  bool success;
  std::string message;
  experience_reward_response_data data;
};

/**
 * @brief 经验值奖励切面类
 * 用于在用户执行特定操作后自动给予经验值奖励
 * 统一管理所有经验值奖励，包括原有的积分奖励和经验值奖励
 */
struct experience_reward_aspect {
  /**
   * @brief 在请求处理后执行经验值奖励
   * @param req HTTP请求
   * @param resp HTTP响应
   * @return 是否继续处理后续操作
   */
  bool after(coro_http_request &req, coro_http_response &resp) {
    // 获取请求路径
    auto full_url = req.full_url();

    // 从完整URL中提取路径部分
    size_t path_start = full_url.find('/');
    if (path_start == std::string::npos) {
      return true;
    }

    size_t query_start = full_url.find('?', path_start);
    std::string path;
    if (query_start == std::string::npos) {
      path = full_url.substr(path_start);
    } else {
      path = full_url.substr(path_start, query_start - path_start);
    }

    // 根据不同的请求路径给予不同的经验值奖励
    if (path == "/api/v1/register") {
      // 注册成功后给予经验值奖励
      handle_register_reward(req, resp);
    } else if (path == "/api/v1/login") {
      // 登录成功后给予经验值奖励
      handle_login_reward(req, resp);
    } else if (path == "/api/v1/new_article") {
      // 发布文章成功后给予经验值奖励
      handle_publish_article_reward(req, resp);
    } else if (path == "/api/v1/add_article_comment") {
      // 发布评论成功后给予经验值奖励
      handle_publish_comment_reward(req, resp);
    }

    return true;
  }

private:
  /**
   * @brief 处理注册成功后的经验值奖励
   * @param req HTTP请求
   * @param resp HTTP响应
   */
  void handle_register_reward(coro_http_request &req,
                              coro_http_response &resp) {
    // 从响应中获取用户信息
    auto resp_body = resp.content();
    if (resp_body.empty()) {
      return;
    }

    // 解析响应体获取用户ID
    experience_reward_response register_result{};

    std::error_code ec;
    iguana::from_json(register_result, resp_body, ec);
    if (ec || !register_result.success) {
      return;
    }

    // 从配置获取注册奖励经验值
    auto &config = purecpp_config::get_instance().user_cfg_;
    int32_t reward = config.experience_rewards.register_reward;

    // 给予注册经验值奖励（原积分奖励+经验值奖励合并）
    user_level_t::add_experience(register_result.data.user_id, reward,
                                 ExperienceChangeType::REGISTER, std::nullopt,
                                 std::nullopt, "注册奖励");
  }

  /**
   * @brief 处理登录成功后的经验值奖励
   * @param req HTTP请求
   * @param resp HTTP响应
   */
  void handle_login_reward(coro_http_request &req, coro_http_response &resp) {
    // 从响应中获取用户信息
    auto resp_body = resp.content();
    if (resp_body.empty()) {
      return;
    }

    // 解析响应体获取用户ID
    experience_reward_response login_result{};

    std::error_code ec;
    iguana::from_json(login_result, resp_body, ec);
    if (ec || !login_result.success) {
      return;
    }

    uint64_t user_id = login_result.data.user_id;

    // 检查今天是否已经获得过登录奖励
    auto conn = get_db_pool().get();
    if (conn == nullptr) {
      return;
    }

    // 获取今天的起始时间戳（毫秒）
    uint64_t now = get_timestamp_milliseconds();
    uint64_t one_day_ms = 24 * 60 * 60 * 1000;
    uint64_t today_start = now - (now % one_day_ms);

    // 直接使用SQL查询今天是否已经有过登录奖励记录
    auto experience_details =
        conn->select(ormpp::all)
            .from<user_experience_detail_t>()
            .where(ormpp::col(&user_experience_detail_t::user_id).param() &&
                   ormpp::col(&user_experience_detail_t::change_type).param() &&
                   ormpp::col(&user_experience_detail_t::created_at) >
                       today_start)
            .collect(user_id, ExperienceChangeType::DAILY_LOGIN);

    if (experience_details.size() > 0) {
      // 今天已经获得过登录奖励，不再重复奖励
      return;
    }

    // 从配置获取每日登录奖励经验值
    auto &config = purecpp_config::get_instance().user_cfg_;
    int32_t reward = config.experience_rewards.daily_login_reward;

    // 给予每日登录经验值奖励
    user_level_t::add_experience(user_id, reward,
                                 ExperienceChangeType::DAILY_LOGIN,
                                 std::nullopt, std::nullopt, "每日登录奖励");
  }

  /**
   * @brief 处理发布文章成功后的经验值奖励
   * @param req HTTP请求
   * @param resp HTTP响应
   */
  void handle_publish_article_reward(coro_http_request &req,
                                     coro_http_response &resp) {
    uint64_t user_id = get_user_id_from_token(req);
    if (user_id == 0) {
      return;
    }

    // 检查响应状态
    if (resp.status() != status_type::ok) {
      return;
    }

    // 从配置获取发布文章奖励经验值
    auto &config = purecpp_config::get_instance().user_cfg_;
    int32_t reward = config.experience_rewards.publish_article_reward;

    // 给予发布文章经验值奖励（原积分奖励+经验值奖励合并）
    user_level_t::add_experience(user_id, reward,
                                 ExperienceChangeType::PUBLISH_ARTICLE,
                                 std::nullopt, std::nullopt, "发布文章奖励");
  }

  /**
   * @brief 处理发布评论成功后的经验值奖励
   * @param req HTTP请求
   * @param resp HTTP响应
   */
  void handle_publish_comment_reward(coro_http_request &req,
                                     coro_http_response &resp) {
    uint64_t user_id = get_user_id_from_token(req);
    if (user_id == 0) {
      return;
    }

    // 检查响应状态
    if (resp.status() != status_type::ok) {
      return;
    }

    // 从配置获取发布评论奖励经验值
    auto &config = purecpp_config::get_instance().user_cfg_;
    int32_t reward = config.experience_rewards.publish_comment_reward;

    // 给予发布评论经验值奖励（原积分奖励+经验值奖励合并）
    user_level_t::add_experience(user_id, reward,
                                 ExperienceChangeType::PUBLISH_COMMENT,
                                 std::nullopt, std::nullopt, "发布评论奖励");
  }
};

} // namespace purecpp
