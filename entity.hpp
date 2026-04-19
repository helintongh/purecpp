#pragma once
#include <string>

#include "db_backend.hpp"
using namespace ormpp;

namespace purecpp {
// 积分变动类型枚举
enum class ExperienceChangeType : int32_t {
  REGISTER = 0,            // 注册
  DAILY_LOGIN = 1,         // 每日登录
  PUBLISH_ARTICLE = 2,     // 发布文章
  ARTICLE_ACCEPTED = 3,    // 文章被接受
  ARTICLE_REJECTED = 4,    // 文章被拒绝
  PUBLISH_COMMENT = 5,     // 发布评论
  COMMENT_LIKED = 6,       // 评论被点赞
  ARTICLE_LIKED = 7,       // 文章被点赞
  ARTICLE_VIEWED = 8,      // 文章被浏览
  SYSTEM_REWARD = 9,       // 系统奖励
  ADMIN_OPERATION = 10,    // 管理员操作
  POINT_CONSUMPTION = 11,  // 积分消费
  PURCHASE_PRIVILEGE = 12, // 购买特权
  GIFT_TO_USER = 13        // 打赏用户
};

// 用户等级枚举
enum class UserLevel : int32_t {
  LEVEL_1 = 1,  // 等级1 - 新手
  LEVEL_2 = 2,  // 等级2 - 入门
  LEVEL_3 = 3,  // 等级3 - 进阶
  LEVEL_4 = 4,  // 等级4 - 熟练
  LEVEL_5 = 5,  // 等级5 - 专家
  LEVEL_6 = 6,  // 等级6 - 大师
  LEVEL_7 = 7,  // 等级7 - 宗师
  LEVEL_8 = 8,  // 等级8 - 传奇
  LEVEL_9 = 9,  // 等级9 - 神话
  LEVEL_10 = 10 // 等级10 - 不朽
};

// 用户头衔枚举
enum class UserTitle : int32_t {
  NEWBIE = 0,           // 新手
  DEVELOPER = 1,        // 开发者
  SENIOR_DEVELOPER = 2, // 高级开发者
  ENGINEER = 3,         // 工程师
  SENIOR_ENGINEER = 4,  // 高级工程师
  ARCHITECT = 5,        // 架构师
  TECH_LEAD = 6,        // 技术负责人
  EXPERT = 7,           // 专家
  MASTER = 8,           // 大师
  LEGEND = 9            // 传奇
};

enum EmailVerifyStatus : int32_t {
  UNVERIFIED = 0, // 未验证
  VERIFIED = 1,   // 已验证
};

// 在线状态
inline constexpr std::string_view STATUS_OF_OFFLINE = "Offline";
inline constexpr std::string_view STATUS_OF_ONLINE = "Online";
inline constexpr std::string_view STATUS_OF_AWAY = "Away";

// database config
struct db_config {
  std::string db_ip;
  int db_port;
  std::string db_name;
  std::string db_user_name;
  std::string db_pwd;

  int db_conn_num;
  int db_conn_timeout; // seconds
};

struct users_t {
  uint64_t id;
  std::array<char, 254> user_name; // unique, not null
  std::array<char, 254> email;     // unique, not null
  std::string_view pwd_hash;       // not null
  std::string status;              // 在线状态Online, Offline, Away
  EmailVerifyStatus is_verifyed; // 邮箱是否已验证(0:未验证, 1:已验证)
  uint64_t created_at;
  uint64_t last_active_at; // 最后活跃时间

  // 用户身份信息
  UserTitle title;     // 头衔枚举
  std::string role;    // 角色，如"user"、"admin"、"superadmin"
  uint64_t experience; // 经验值
  UserLevel level;     // 用户等级枚举

  // 个人资料信息
  std::optional<std::string> bio;      // 个人简介
  std::optional<std::string> avatar;   // 头像URL
  std::optional<std::string> location; // 所在地
  std::optional<std::string> skills;   // 技能标签，逗号分隔

  // 登录安全相关字段
  uint32_t login_attempts;    // 登录失败次数
  uint64_t last_failed_login; // 最后一次登录失败时间戳
};

inline constexpr std::string_view get_alias_struct_name(users_t *) {
  return "users"; // 表名默认结构体名字(users_t), 这里可以修改表名
}

// 用户信息临时表
struct users_tmp_t {
  uint64_t id;
  std::array<char, 254> user_name; // unique, not null
  std::array<char, 254> email;     // unique, not null
  std::string_view pwd_hash;       // not null
  EmailVerifyStatus is_verifyed; // 邮箱是否已验证(0:未验证, 1:已验证)
  uint64_t created_at;
};
// 注册users_t的主键
inline constexpr std::string_view get_alias_struct_name(users_tmp_t *) {
  return "users_tmp"; // 用户注册临时表
}

// 用户token表
enum class TokenType : int32_t {
  RESET_PASSWORD = 0, // 重置密码
  VERIFY_EMAIL = 1,   // 验证邮箱
  REFRESH_TOKEN = 2,  // 刷新token
};

struct users_token_t {
  uint64_t id;
  uint64_t user_id;
  TokenType token_type; // 0: 重置密码, 1: 验证邮箱
  std::array<char, 129> token;
  uint64_t created_at;
  uint64_t expires_at;
};
// 注册users_token_t的主键
REGISTER_AUTO_KEY(users_token_t, id);

constexpr std::string_view get_alias_struct_name(users_token_t *) {
  return "user_tokens"; // 表名
}

static std::string_view DRAFT = "draft";                   // 草稿
static std::string_view PENDING_REVIEW = "pending_review"; // 待审核
static std::string_view PUBLISHED = "published";           // 已发布
static std::string_view REJECTED = "rejected";             // 已拒绝

// 文章相关的表
struct articles_t {
  uint64_t article_id = 0;
  std::string tag_ids; // 多个标签ID，用竖线|分割
  std::string title;
  std::string abstraction; // 摘要
  std::string content;
  std::array<char, 8> slug; // 随机数，用于生成url的后缀
  uint64_t author_id;       // 外键
  uint64_t created_at;
  uint64_t updated_at;
  uint32_t views_count;
  uint32_t comments_count;
  uint64_t reviewer_id;       // 审核人id 外键
  std::string review_comment; // 审核意见
  int featured_weight;        // 置顶
  uint64_t review_date;       // 审核完成时间
  std::string status; // 状态：draft(草稿), pending_review (待审核),
                      // published(已发布), rejected (已拒绝)
  bool is_deleted = false; // 0: 未删除, 1: 已删除
};
constexpr std::string_view get_alias_struct_name(articles_t *) {
  return "articles";
}

// 文章评论状态枚举
enum class CommentStatus : int32_t {
  DELETED = 0, // 已删除
  PUBLISH = 1, // 已发布
};

struct article_comments_t {
  uint64_t comment_id = 0;
  uint64_t article_id; // 外键
  uint64_t user_id;    // 外键
  std::string content;
  uint64_t parent_comment_id; // 指向父评论
  uint64_t parent_user_id;
  std::array<char, 254> parent_user_name; // unique, not null
  std::array<char, 16> ip;                // 评论者IP地址
  CommentStatus comment_status;           // 评论状态：CommentStatus
  uint64_t created_at;
  uint64_t updated_at;
};
REGISTER_AUTO_KEY(article_comments_t, comment_id);
constexpr std::string_view get_alias_struct_name(article_comments_t *) {
  return "article_comments";
}

// 特权类型枚举
enum class PrivilegeType : int32_t {
  PREMIUM_MEMBER = 0,     // 高级会员
  NO_ADS = 1,             // 无广告
  UNLIMITED_COMMENTS = 2, // 无限评论
  CUSTOM_TITLE = 3,       // 自定义头衔
  ARTICLE_PROMOTION = 4,  // 文章推广
  PRIVATE_MESSAGE = 5     // 私信功能
};

// 特权表
struct privileges_t {
  uint64_t id = 0;
  PrivilegeType privilege_type; // 特权类型
  std::string name;             // 特权名称
  std::string description;      // 特权描述
  uint64_t points_cost;         // 积分消耗
  uint64_t duration_days;       // 有效期（天）
  bool is_active;               // 是否激活
};
REGISTER_AUTO_KEY(privileges_t, id);
constexpr std::string_view get_alias_struct_name(privileges_t *) {
  return "privileges";
}

// 用户特权表
struct user_privileges_t {
  uint64_t id = 0;
  uint64_t user_id;      // 外键，关联用户表
  uint64_t privilege_id; // 外键，关联特权表
  uint64_t start_time;   // 生效时间
  uint64_t end_time;     // 过期时间
  bool is_active;        // 是否激活
  uint64_t created_at;   // 创建时间
};
REGISTER_AUTO_KEY(user_privileges_t, id);
constexpr std::string_view get_alias_struct_name(user_privileges_t *) {
  return "user_privileges";
}

// 打赏记录表
struct user_gifts_t {
  uint64_t id = 0;
  uint64_t sender_id;                 // 打赏者ID
  uint64_t receiver_id;               // 接收者ID
  uint64_t article_id;                // 关联文章ID（可选）
  uint64_t comment_id;                // 关联评论ID（可选）
  int64_t experience_amount;          // 打赏经验值数量
  std::optional<std::string> message; // 打赏留言
  uint64_t created_at;                // 打赏时间
};
REGISTER_AUTO_KEY(user_gifts_t, id);
constexpr std::string_view get_alias_struct_name(user_gifts_t *) {
  return "user_gifts";
}

// 经验值交易记录表
struct user_experience_detail_t {
  uint64_t id = 0;
  uint64_t user_id;                 // 外键，关联用户表
  ExperienceChangeType change_type; // 经验值变动类型
  int64_t experience_change; // 经验值变动量，正数表示增加，负数表示减少
  uint64_t balance_after_experience; // 变动后的经验值余额
  std::optional<uint64_t> related_id; // 关联的实体ID（如文章ID、评论ID）
  std::optional<std::string>
      related_type; // 关联的实体类型（如"article"、"comment"）
  std::optional<std::string> description; // 交易描述
  uint64_t created_at;                    // 交易时间
};
REGISTER_AUTO_KEY(user_experience_detail_t, id);
constexpr std::string_view get_alias_struct_name(user_experience_detail_t *) {
  return "user_experience_detail";
}

// 私信表
struct private_message_t {
  uint64_t id = 0;
  uint64_t sender_id;
  uint64_t receiver_id;
  std::string content;
  uint32_t is_read = 0;                  // 0=未读, 1=已读
  uint32_t deleted_by_sender = 0;        // 发送者软删除
  uint32_t deleted_by_receiver = 0;      // 接收者软删除
  uint64_t created_at;
};

// 私信黑名单表
struct pm_blocklist_t {
  uint64_t id = 0;
  uint64_t user_id;                      // 拉黑操作者
  uint64_t blocked_user_id;             // 被拉黑的用户
  uint64_t created_at;
};
REGISTER_AUTO_KEY(pm_blocklist_t, id);
constexpr std::string_view get_alias_struct_name(pm_blocklist_t *) {
  return "pm_blocklist";
}
REGISTER_AUTO_KEY(private_message_t, id);
constexpr std::string_view get_alias_struct_name(private_message_t *) {
  return "private_messages";
}

enum class TagGroupType : int32_t {
  TECH_ARTICLES = 1, // 技术文章标签组
  CPP_PARTY = 2,     // 社区大会标签组
  SERVICES = 3       // 社区服务标签组
};
struct tags_t {
  int tag_id;
  std::array<char, 50> name;
  int tag_group;
};
constexpr std::string_view get_alias_struct_name(tags_t *) { return "tags"; }

template <typename T> struct rest_response {
  bool success = true;
  std::string message;
  std::optional<std::vector<std::string>> errors;
  std::string timestamp;
  int code = 200;
  int total_count = 0; // 总记录数，用于分页
  std::optional<T> data;
};
} // namespace purecpp
