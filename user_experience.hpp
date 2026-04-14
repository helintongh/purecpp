#pragma once

#include "common.hpp"
#include "config.hpp"
#include "entity.hpp"
#include <cinatra.hpp>

using namespace cinatra;

namespace purecpp {

struct purchase_privilege_request {
  uint64_t privilege_id;
};

struct gift_user_request {
  uint64_t receiver_id;
  int64_t points_amount;
  std::optional<uint64_t> article_id;
  std::optional<uint64_t> comment_id;
  std::optional<std::string> message;
};

/**
 * @brief 用户等级和积分管理类
 */
class user_level_t {
public:
  /**
   * @brief 根据经验值计算用户等级
   * @param experience 用户经验值
   * @return 用户等级
   */
  static UserLevel calculate_level(uint64_t experience) {
    auto &config = purecpp_config::get_instance().user_cfg_;
    const auto &level_rules = config.level_rules;

    // 如果没有配置等级规则，使用默认值
    if (level_rules.empty()) {
      // 默认等级规则，保持原逻辑
      if (experience >= 38400)
        return UserLevel::LEVEL_10;
      if (experience >= 19200)
        return UserLevel::LEVEL_9;
      if (experience >= 9600)
        return UserLevel::LEVEL_8;
      if (experience >= 4800)
        return UserLevel::LEVEL_7;
      if (experience >= 2400)
        return UserLevel::LEVEL_6;
      if (experience >= 1200)
        return UserLevel::LEVEL_5;
      if (experience >= 600)
        return UserLevel::LEVEL_4;
      if (experience >= 300)
        return UserLevel::LEVEL_3;
      if (experience >= 100)
        return UserLevel::LEVEL_2;
      return UserLevel::LEVEL_1;
    }

    // 从高到低遍历等级规则，找到匹配的等级
    UserLevel result = UserLevel::LEVEL_1;
    for (const auto &rule : level_rules) {
      if (experience >= rule.experience_threshold) {
        result = static_cast<UserLevel>(rule.level);
      } else {
        break;
      }
    }

    return result;
  }

  /**
   * @brief 获取升级到下一级所需的经验值
   * @param current_level 当前等级
   * @return 升级所需经验值
   */
  static uint64_t get_required_experience(UserLevel current_level) {
    auto &config = purecpp_config::get_instance().user_cfg_;
    const auto &level_rules = config.level_rules;

    // 如果没有配置等级规则，使用默认值
    if (level_rules.empty()) {
      // 默认等级规则，保持原逻辑
      switch (current_level) {
      case UserLevel::LEVEL_1:
        return 100;
      case UserLevel::LEVEL_2:
        return 300;
      case UserLevel::LEVEL_3:
        return 600;
      case UserLevel::LEVEL_4:
        return 1200;
      case UserLevel::LEVEL_5:
        return 2400;
      case UserLevel::LEVEL_6:
        return 4800;
      case UserLevel::LEVEL_7:
        return 9600;
      case UserLevel::LEVEL_8:
        return 19200;
      case UserLevel::LEVEL_9:
        return 38400;
      case UserLevel::LEVEL_10:
        return 0; // 已达最高等级
      default:
        return 0;
      }
    }

    // 查找当前等级和下一级的经验值阈值
    uint64_t current_threshold = 0;
    uint64_t next_threshold = 0;
    bool found_current = false;

    for (size_t i = 0; i < level_rules.size(); ++i) {
      if (static_cast<int>(current_level) == level_rules[i].level) {
        current_threshold = level_rules[i].experience_threshold;
        found_current = true;

        // 检查是否有下一级
        if (i + 1 < level_rules.size()) {
          next_threshold = level_rules[i + 1].experience_threshold;
        } else {
          // 已达最高等级
          return 0;
        }
        break;
      }
    }

    if (!found_current) {
      return 0;
    }

    return next_threshold;
  }

  /**
   * @brief 获取当前等级的经验值下限
   * @param current_level 当前等级
   * @return 经验值下限
   */
  static uint64_t get_level_experience_min(UserLevel current_level) {
    auto &config = purecpp_config::get_instance().user_cfg_;
    const auto &level_rules = config.level_rules;

    // 如果没有配置等级规则，使用默认值
    if (level_rules.empty()) {
      // 默认等级规则，保持原逻辑
      switch (current_level) {
      case UserLevel::LEVEL_1:
        return 0;
      case UserLevel::LEVEL_2:
        return 100;
      case UserLevel::LEVEL_3:
        return 300;
      case UserLevel::LEVEL_4:
        return 600;
      case UserLevel::LEVEL_5:
        return 1200;
      case UserLevel::LEVEL_6:
        return 2400;
      case UserLevel::LEVEL_7:
        return 4800;
      case UserLevel::LEVEL_8:
        return 9600;
      case UserLevel::LEVEL_9:
        return 19200;
      case UserLevel::LEVEL_10:
        return 38400;
      default:
        return 0;
      }
    }

    // 查找当前等级的经验值阈值
    for (const auto &rule : level_rules) {
      if (static_cast<int>(current_level) == rule.level) {
        return rule.experience_threshold;
      }
    }

    return 0;
  }

  /**
   * @brief 计算当前等级的进度百分比
   * @param experience 当前经验值
   * @param current_level 当前等级
   * @return 进度百分比（0-100）
   */
  static int calculate_level_progress(uint64_t experience,
                                      UserLevel current_level) {
    if (current_level == UserLevel::LEVEL_10)
      return 100;

    uint64_t min_exp = get_level_experience_min(current_level);
    uint64_t next_level_min = get_level_experience_min(
        static_cast<UserLevel>(static_cast<int>(current_level) + 1));
    uint64_t level_range = next_level_min - min_exp;
    uint64_t current_in_level = experience - min_exp;

    return static_cast<int>(
        (static_cast<double>(current_in_level) / level_range) * 100);
  }

  /**
   * @brief 获取当天的起始时间戳（毫秒）
   * @return 当天起始时间戳
   */
  static uint64_t get_today_start_timestamp() {
    uint64_t now = get_timestamp_milliseconds();
    uint64_t one_day_ms = 24 * 60 * 60 * 1000;
    return now - (now % one_day_ms);
  }

  /**
   * @brief 检查用户当日获取的经验值是否超过上限
   * @param user_id 用户ID
   * @param experience_add 将要增加的经验值
   * @param change_type 经验值变动类型
   * @return 是否可以增加经验值
   */
  static bool check_experience_limit(uint64_t user_id, int64_t experience_add,
                                     ExperienceChangeType change_type) {
    auto conn = get_db_pool().get();
    if (conn == nullptr) {
      return false;
    }

    // 从配置获取经验值上限
    auto &config = purecpp_config::get_instance().user_cfg_;
    const auto &limits = config.experience_limits;

    // 计算当天的起始时间戳
    uint64_t today_start = get_today_start_timestamp();

    // 使用ORMpp的查询方式获取总经验值
    auto total_experience =
        conn->select(sum(col(&user_experience_detail_t::experience_change)))
            .from<user_experience_detail_t>()
            .where(col(&user_experience_detail_t::user_id).param() &&
                   col(&user_experience_detail_t::created_at).param())
            .collect(user_id, today_start);

    // sum函数直接返回结果值，而不是容器
    uint64_t total_today = static_cast<uint64_t>(total_experience);

    // 检查总上限
    if (total_today + experience_add > limits.daily_total_limit) {
      return false;
    }

    // 简化实现：只检查总上限，暂时不检查各类型上限
    // 这样可以避免复杂的ORMpp查询问题，同时保留核心功能
    // 各类型上限可以后续通过优化ORMpp查询来实现

    // 目前只实现总上限检查，各类型上限检查可以后续优化
    return true;
  }

  /**
   * @brief 增加用户经验值
   * @param user_id 用户ID
   * @param experience_add 增加的经验值
   * @return 操作是否成功
   */
  static bool add_experience(uint64_t user_id, uint64_t experience_add) {
    // 默认使用系统奖励类型
    return add_experience(user_id, experience_add,
                          ExperienceChangeType::SYSTEM_REWARD);
  }

  /**
   * @brief 增加用户经验值
   * @param user_id 用户ID
   * @param experience_add 增加的经验值
   * @param change_type 经验值变动类型
   * @param related_id 关联的实体ID（可选）
   * @param related_type 关联的实体类型（可选）
   * @param description 交易描述（可选）
   * @return 操作是否成功
   */
  static bool
  add_experience(uint64_t user_id, int64_t experience_add,
                 ExperienceChangeType change_type,
                 std::optional<uint64_t> related_id = std::nullopt,
                 std::optional<std::string> related_type = std::nullopt,
                 std::optional<std::string> description = std::nullopt) {
    auto conn = get_db_pool().get();
    if (conn == nullptr) {
      return false;
    }

    // 检查经验值上限
    if (!check_experience_limit(user_id, experience_add, change_type)) {
      return false;
    }

    // 查询用户当前信息
    auto users = conn->select(ormpp::all)
                     .from<users_t>()
                     .where(col(&users_t::id).param())
                     .collect(user_id);
    if (users.empty()) {
      return false;
    }

    users_t user = users[0];
    uint64_t new_experience = user.experience + experience_add;
    UserLevel new_level = calculate_level(new_experience);

    // 开启事务
    conn->begin();

    // 更新用户经验值和等级
    users_t update_user;
    update_user.experience = new_experience;
    update_user.level = new_level;
    if (conn->update_some<&users_t::experience, &users_t::level>(
            update_user, "id=" + std::to_string(user.id)) != 1) {
      conn->rollback();
      return false;
    }

    // 记录经验值变动
    user_experience_detail_t transaction{
        .user_id = user_id,
        .change_type = change_type,
        .experience_change = experience_add,
        .balance_after_experience = new_experience,
        .related_id = related_id,
        .related_type = related_type,
        .description = description,
        .created_at = get_timestamp_milliseconds()};

    if (conn->insert(transaction) == 0) {
      conn->rollback();
      return false;
    }

    // 提交事务
    conn->commit();
    return true;
  }

  /**
   * @brief 减少用户经验值
   * @param user_id 用户ID
   * @param experience_reduce 减少的经验值
   * @param change_type 经验值变动类型
   * @param related_id 关联的实体ID（可选）
   * @param related_type 关联的实体类型（可选）
   * @param description 交易描述（可选）
   * @return 操作是否成功
   */
  static bool
  reduce_experience(uint64_t user_id, int64_t experience_reduce,
                    ExperienceChangeType change_type,
                    std::optional<uint64_t> related_id = std::nullopt,
                    std::optional<std::string> related_type = std::nullopt,
                    std::optional<std::string> description = std::nullopt) {
    auto conn = get_db_pool().get();
    if (conn == nullptr) {
      return false;
    }

    // 查询用户当前信息
    auto users = conn->select(ormpp::all)
                     .from<users_t>()
                     .where(col(&users_t::id).param())
                     .collect(user_id);
    if (users.empty()) {
      return false;
    }

    users_t user = users[0];
    if (user.experience < experience_reduce) {
      return false; // 经验值不足
    }

    uint64_t new_experience = user.experience - experience_reduce;
    UserLevel new_level = calculate_level(new_experience);

    // 开启事务
    conn->begin();

    // 更新用户经验值和等级
    users_t update_user;
    update_user.experience = new_experience;
    update_user.level = new_level;
    if (conn->update_some<&users_t::experience, &users_t::level>(
            update_user, "id=" + std::to_string(user.id)) != 1) {
      conn->rollback();
      return false;
    }

    // 记录经验值变动
    user_experience_detail_t transaction{
        .user_id = user_id,
        .change_type = change_type,
        .experience_change = -experience_reduce,
        .balance_after_experience = new_experience,
        .related_id = related_id,
        .related_type = related_type,
        .description = description,
        .created_at = get_timestamp_milliseconds()};

    if (conn->insert(transaction) == 0) {
      conn->rollback();
      return false;
    }

    // 提交事务
    conn->commit();
    return true;
  }

  /**
   * @brief 获取用户等级和经验值信息
   * @param user_id 用户ID
   * @param[out] user_level_info 用户等级信息
   * @return 操作是否成功
   */
  static bool get_user_level_info(uint64_t user_id, users_t &user_level_info) {
    auto conn = get_db_pool().get();
    if (conn == nullptr) {
      return false;
    }

    auto users = conn->select(ormpp::all)
                     .from<users_t>()
                     .where(col(&users_t::id).param())
                     .collect(user_id);
    if (users.empty()) {
      return false;
    }

    user_level_info = users[0];
    return true;
  }

  /**
   * @brief 购买特权
   * @param user_id 用户ID
   * @param privilege_id 特权ID
   * @return 操作是否成功
   */
  static bool purchase_privilege(uint64_t user_id, uint64_t privilege_id) {
    auto conn = get_db_pool().get();
    if (conn == nullptr) {
      return false;
    }

    // 查询特权信息
    auto privileges = conn->select(ormpp::all)
                          .from<privileges_t>()
                          .where(col(&privileges_t::id).param() &&
                                 col(&privileges_t::is_active).param())
                          .collect(privilege_id, true);
    if (privileges.empty()) {
      return false;
    }

    privileges_t privilege = privileges[0];

    // 开启事务
    conn->begin();

    // 减少用户经验值
    if (!reduce_experience(user_id, privilege.points_cost,
                           ExperienceChangeType::PURCHASE_PRIVILEGE,
                           privilege_id, "privilege",
                           "购买特权：" + privilege.name)) {
      conn->rollback();
      return false;
    }

    // 添加用户特权
    uint64_t now = get_timestamp_milliseconds();
    uint64_t end_time = now + (privilege.duration_days * 24 * 60 * 60 * 1000);

    user_privileges_t user_privilege{.user_id = user_id,
                                     .privilege_id = privilege_id,
                                     .start_time = now,
                                     .end_time = end_time,
                                     .is_active = true,
                                     .created_at = now};

    if (conn->insert(user_privilege) == 0) {
      conn->rollback();
      return false;
    }

    // 提交事务
    conn->commit();
    return true;
  }

  /**
   * @brief 打赏用户
   * @param sender_id 打赏者ID
   * @param receiver_id 接收者ID
   * @param experience_amount 打赏经验值数量
   * @param article_id 关联文章ID（可选）
   * @param comment_id 关联评论ID（可选）
   * @param message 打赏留言（可选）
   * @return 操作是否成功
   */
  static bool gift_user(uint64_t sender_id, uint64_t receiver_id,
                        int64_t experience_amount,
                        std::optional<uint64_t> article_id = std::nullopt,
                        std::optional<uint64_t> comment_id = std::nullopt,
                        std::optional<std::string> message = std::nullopt) {
    auto conn = get_db_pool().get();
    if (conn == nullptr) {
      return false;
    }

    // 验证接收者是否存在
    auto receivers = conn->select(ormpp::all)
                         .from<users_t>()
                         .where(col(&users_t::id).param())
                         .collect(receiver_id);
    if (receivers.empty()) {
      return false;
    }

    // 开启事务
    conn->begin();

    // 减少打赏者经验值
    if (!reduce_experience(sender_id, experience_amount,
                           ExperienceChangeType::GIFT_TO_USER, article_id,
                           "gift", "打赏用户")) {
      conn->rollback();
      return false;
    }

    // 增加接收者经验值
    if (!add_experience(receiver_id, experience_amount,
                        ExperienceChangeType::SYSTEM_REWARD, article_id, "gift",
                        "收到打赏")) {
      conn->rollback();
      return false;
    }

    // 记录打赏记录
    user_gifts_t gift{.sender_id = sender_id,
                      .receiver_id = receiver_id,
                      .article_id = article_id.value_or(0),
                      .comment_id = comment_id.value_or(0),
                      .experience_amount = experience_amount,
                      .message = message,
                      .created_at = get_timestamp_milliseconds()};

    if (conn->insert(gift) == 0) {
      conn->rollback();
      return false;
    }

    // 提交事务
    conn->commit();
    return true;
  }
};

// 等级和经验值相关的API响应结构体
struct user_level_info {
  uint64_t user_id;
  std::string username;
  int level; // 使用int代替枚举，确保前端兼容性
  uint64_t experience;
  int level_progress;
  uint64_t next_level_required;
};

struct experience_transaction_info {
  uint64_t id;
  int change_type; // 使用int代替枚举，确保前端兼容性
  int64_t experience_change;
  uint64_t balance_after_experience;
  std::optional<uint64_t> related_id;
  std::optional<std::string> related_type;
  std::optional<std::string> description;
  uint64_t created_at;
};

struct experience_transactions_resp {
  std::vector<experience_transaction_info> transactions;
  int total_count;
  int current_page;
  int page_size;
};

/**
 * @brief 用户等级和积分API处理类
 */
class user_level_api_t {
public:
  /**
   * @brief 获取用户等级和积分信息
   * @param req HTTP请求
   * @param resp HTTP响应
   */
  void get_user_level(coro_http_request &req, coro_http_response &resp) {
    // 从请求中获取用户ID
    auto user_id_str = req.get_header_value("X-User-ID");
    if (user_id_str.empty()) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("用户未登录"));
      return;
    }

    uint64_t user_id = std::stoull(std::string(user_id_str));
    users_t user_info;

    if (!user_level_t::get_user_level_info(user_id, user_info)) {
      resp.set_status_and_content(status_type::internal_server_error,
                                  make_error("获取用户信息失败"));
      return;
    }

    // 计算等级进度
    int level_progress = user_level_t::calculate_level_progress(
        user_info.experience, user_info.level);
    uint64_t next_level_required =
        user_level_t::get_required_experience(user_info.level);

    // 构建响应数据
    user_level_info resp_data{.user_id = user_info.id,
                              .username =
                                  std::string(user_info.user_name.data()),
                              .level = static_cast<int>(user_info.level),
                              .experience = user_info.experience,
                              .level_progress = level_progress,
                              .next_level_required = next_level_required};

    resp.set_status_and_content(status_type::ok,
                                make_data(resp_data, "获取用户等级信息成功"));
  }

  /**
   * @brief 获取用户经验值交易记录
   * @param req HTTP请求
   * @param resp HTTP响应
   */
  void get_experience_transactions(coro_http_request &req,
                                   coro_http_response &resp) {
    // 从请求中获取用户ID
    auto user_id_str = req.get_header_value("X-User-ID");
    if (user_id_str.empty()) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("用户未登录"));
      return;
    }

    uint64_t user_id = std::stoull(std::string(user_id_str));

    // 获取分页参数
    int page = 1;
    int page_size = 20;
    auto page_str = req.get_query_value("page");
    auto page_size_str = req.get_query_value("page_size");

    if (!page_str.empty()) {
      page = std::stoi(std::string(page_str));
    }
    if (!page_size_str.empty()) {
      page_size = std::stoi(std::string(page_size_str));
    }

    // 查询经验值交易记录
    auto conn = get_db_pool().get();
    if (conn == nullptr) {
      set_server_internel_error(resp);
      return;
    }

    // 计算总记录数
    auto total_count =
        conn->select(count(col(&user_experience_detail_t::id)))
            .from<user_experience_detail_t>()
            .where(col(&user_experience_detail_t::user_id).param())
            .collect(user_id);

    // 查询分页数据
    int offset = (page - 1) * page_size;
    auto transactions =
        conn->select(ormpp::all)
            .from<user_experience_detail_t>()
            .where(col(&user_experience_detail_t::user_id).param())
            .order_by(col(&user_experience_detail_t::created_at).desc())
            .limit(page_size)
            .offset(offset)
            .collect(user_id);

    // 构建响应数据
    std::vector<experience_transaction_info> transaction_infos;
    for (const auto &t : transactions) {
      transaction_infos.push_back(
          {.id = t.id,
           .change_type = static_cast<int>(t.change_type),
           .experience_change = t.experience_change,
           .balance_after_experience = t.balance_after_experience,
           .related_id = t.related_id,
           .related_type = t.related_type,
           .description = t.description,
           .created_at = t.created_at});
    }

    experience_transactions_resp resp_data{.transactions = transaction_infos,
                                           .total_count =
                                               static_cast<int>(total_count),
                                           .current_page = page,
                                           .page_size = page_size};

    resp.set_status_and_content(status_type::ok,
                                make_data(resp_data, "获取经验值交易记录成功"));
  }

  /**
   * @brief 购买特权
   * @param req HTTP请求
   * @param resp HTTP响应
   */
  void purchase_privilege(coro_http_request &req, coro_http_response &resp) {
    // 从请求中获取用户ID
    auto user_id_str = req.get_header_value("X-User-ID");
    if (user_id_str.empty()) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("用户未登录"));
      return;
    }

    uint64_t user_id = std::stoull(std::string(user_id_str));

    // 解析请求体
    auto body = req.get_body();
    purchase_privilege_request info{};

    std::error_code ec;
    iguana::from_json(info, body, ec);
    if (ec) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("请求参数无效"));
      return;
    }

    // 购买特权
    if (!user_level_t::purchase_privilege(user_id, info.privilege_id)) {
      resp.set_status_and_content(
          status_type::bad_request,
          make_error("购买特权失败，可能是积分不足或特权不存在"));
      return;
    }

    resp.set_status_and_content(status_type::ok, make_success("购买特权成功"));
  }

  /**
   * @brief 打赏用户
   * @param req HTTP请求
   * @param resp HTTP响应
   */
  void user_gifts(coro_http_request &req, coro_http_response &resp) {
    // 从请求中获取用户ID
    auto user_id_str = req.get_header_value("X-User-ID");
    if (user_id_str.empty()) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("用户未登录"));
      return;
    }

    uint64_t sender_id = std::stoull(std::string(user_id_str));

    // 解析请求体
    auto body = req.get_body();
    gift_user_request info{};

    std::error_code ec;
    iguana::from_json(info, body, ec);
    if (ec) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("请求参数无效"));
      return;
    }

    // 打赏用户
    if (!user_level_t::gift_user(sender_id, info.receiver_id,
                                 info.points_amount, info.article_id,
                                 info.comment_id, info.message)) {
      resp.set_status_and_content(
          status_type::bad_request,
          make_error("打赏失败，可能是积分不足或接收者不存在"));
      return;
    }

    resp.set_status_and_content(status_type::ok, make_success("打赏成功"));
  }

  /**
   * @brief 获取可用特权列表
   * @param req HTTP请求
   * @param resp HTTP响应
   */
  void get_available_privileges(coro_http_request &req,
                                coro_http_response &resp) {
    auto conn = get_db_pool().get();
    if (conn == nullptr) {
      set_server_internel_error(resp);
      return;
    }

    // 查询可用特权
    auto privileges = conn->select(ormpp::all)
                          .from<privileges_t>()
                          .where(col(&privileges_t::is_active).param())
                          .collect(true);

    resp.set_status_and_content(status_type::ok,
                                make_data(privileges, "获取可用特权列表成功"));
  }
};
} // namespace purecpp
