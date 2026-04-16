#include "easylog/easylog.hpp"
#define CINATRA_LOG_TRACE ELOG_TRACE
#define CINATRA_LOG_DEBUG ELOG_DEBUG
#define CINATRA_LOG_INFO ELOG_INFO
#define CINATRA_LOG_WARNING ELOG_WARN
#define CINATRA_LOG_ERROR ELOG_ERROR

#include <cinatra.hpp>

#include <algorithm>
#include <filesystem>
#include <iguana/json_reader.hpp>
#include <iguana/json_writer.hpp>
#include <random>
#include <vector>

#include "articles.hpp"
#include "articles_aspects.hpp"
#include "articles_comment.hpp"
#include "chatroom.hpp"
#include "file_watcher.hpp"
#include "sensitive_word_filter.hpp"
#include "entity.hpp"
#include "rate_limiter.hpp"
#include "tags.hpp"
#include "user_aspects.hpp"
#include "user_experience.hpp"
#include "user_experience_aspects.hpp"
#include "user_login.hpp"
#include "user_password.hpp"
#include "user_profile.hpp"
#include "user_register.hpp"

using namespace cinatra;
using namespace ormpp;
using namespace purecpp;

struct test_optional {
  int id;
  std::optional<std::string> name;
  std::optional<int> age;
};

/*
// 成功响应示例
{
    "success": true,
    "message": "注册成功",
    "data": {
        "user_id": 12345,
        "username": "testuser",
        "email": "test@example.com",
        "verification_required": true
    },
    "timestamp": "2024-01-15T10:30:00Z",
    "code": 200
}

// 失败响应示例
{
    "success": false,
    "message": "用户名已存在",
    "errors": {
        "username": "用户名必须大于4个字符。",
        "email": "该邮箱已存在。",
        "cpp_answer": "答案错误，请重新计算。"
    },
    "timestamp": "2024-01-15T10:30:00Z",
    "code": 400
}
*/

// database
bool init_db() {
  std::ifstream file("cfg/db_config.json", std::ios::in);
  if (!file.is_open()) {
    CINATRA_LOG_ERROR << "no config file";
    return false;
  }

  std::string json;
  json.resize(1024);
  file.read(json.data(), json.size());
  db_config conf;
  iguana::from_json(conf, json);

  auto &pool = get_db_pool();
  try {
#if defined(PURECPP_DB_SQLITE)
    std::string db_file = conf.db_name.empty() ? "purecpp.db" : conf.db_name;
    if (!db_file.ends_with(".db")) {
      db_file += ".db";
    }
    pool.init(conf.db_conn_num, "", "", conf.db_pwd, db_file,
              conf.db_conn_timeout, 0);
#elif defined(PURECPP_DB_MYSQL)
    pool.init(conf.db_conn_num, conf.db_ip, conf.db_user_name, conf.db_pwd,
              conf.db_name, conf.db_conn_timeout, conf.db_port);
#elif defined(PURECPP_DB_POSTGRESQL)
    pool.init(conf.db_conn_num, conf.db_ip, conf.db_user_name, conf.db_pwd,
              conf.db_name, conf.db_conn_timeout, conf.db_port);
#endif
  } catch (const std::exception &e) {
    CINATRA_LOG_ERROR << e.what();
    return false;
  }

  auto conn = pool.get();
  conn->create_datatable<users_t>(
      ormpp_key{"id"}, ormpp_unique{{"user_name"}}, ormpp_unique{{"email"}},
      ormpp_not_null{{"user_name", "email", "pwd_hash"}});

  conn->create_datatable<users_tmp_t>(
      ormpp_key{"id"}, ormpp_unique{{"user_name"}}, ormpp_unique{{"email"}},
      ormpp_not_null{{"user_name", "email", "pwd_hash"}});

  conn->create_datatable<tags_t>(ormpp_auto_key{"tag_id"},
                                 ormpp_unique{{"name"}});
  conn->create_datatable<article_comments_t>(ormpp_auto_key{"comment_id"});
  conn->create_datatable<articles_t>(ormpp_auto_key{"article_id"},
                                     ormpp_unique{{"slug"}});

  // 创建密码重置token表
  bool created = conn->create_datatable<users_token_t>(
      ormpp_auto_key{"id"}, ormpp_unique{{"user_id", "token_type"}},
      ormpp_unique{{"token"}},
      ormpp_not_null{
          {"user_id", "token_type", "token", "created_at", "expires_at"}});
  if (created) {
    CINATRA_LOG_INFO << "Table 'users_token' created successfully.";
  } else {
    CINATRA_LOG_ERROR << "Table 'users_token' create error.";
  }

  // 创建经验值交易表
  created = conn->create_datatable<user_experience_detail_t>(
      ormpp_auto_key{"id"},
      ormpp_not_null{{"user_id", "change_type", "experience_change",
                      "balance_after_experience", "created_at"}});
  if (created) {
    CINATRA_LOG_INFO << "Table 'user_experience_detail' created successfully.";
  } else {
    CINATRA_LOG_ERROR << "Table 'user_experience_detail' create error.";
  }

  // 创建特权表
  created = conn->create_datatable<privileges_t>(
      ormpp_auto_key{"id"},
      ormpp_not_null{{"privilege_type", "name", "description", "points_cost",
                      "duration_days", "is_active"}});
  if (created) {
    CINATRA_LOG_INFO << "Table 'privileges' created successfully.";
  } else {
    CINATRA_LOG_ERROR << "Table 'privileges' create error.";
  }

  // 创建用户特权表
  created = conn->create_datatable<user_privileges_t>(
      ormpp_auto_key{"id"},
      ormpp_not_null{{"user_id", "privilege_id", "start_time", "end_time",
                      "is_active", "created_at"}});
  if (created) {
    CINATRA_LOG_INFO << "Table 'user_privileges' created successfully.";
  } else {
    CINATRA_LOG_ERROR << "Table 'user_privileges' create error.";
  }

  // 创建打赏记录表
  created = conn->create_datatable<user_gifts_t>(
      ormpp_auto_key{"id"}, ormpp_not_null{{"sender_id", "receiver_id",
                                            "points_amount", "created_at"}});
  if (created) {
    CINATRA_LOG_INFO << "Table 'user_gifts' created successfully.";
  } else {
    CINATRA_LOG_ERROR << "Table 'user_gifts' create error.";
  }

  CINATRA_LOG_INFO << "Database pool initialized: " << database_backend_name();
  return true;
}

size_t get_question_index() {
  static unsigned seed =
      std::chrono::system_clock::now().time_since_epoch().count();
  static std::mt19937 generator(seed);

  std::uniform_int_distribution<size_t> distribution(0,
                                                     cpp_questions.size() - 1);
  size_t random_index = distribution(generator);
  return random_index;
}

struct question_resp {
  size_t index;
  std::string_view question;
};

int main() {
  std::shared_ptr<int> log_flush_guard(nullptr, [](auto) { easylog::flush(); });
  easylog::init_log(easylog::Severity::INFO, "purecpp.log", false, false,
                    50 * 1024 * 1024, 3);

  if (!init_db()) {
    return -1;
  }

  // 初始化聊天室数据库
  if (!init_chat_db()) {
    CINATRA_LOG_ERROR << "init chat db failed";
    return -1;
  }

  // 加载聊天室敏感词库
  sensitive_word_filter::instance().load("sensitive_words.txt");
  if (!sensitive_word_filter::instance().empty()) {
    CINATRA_LOG_INFO << "sensitive word filter loaded";
  }

  // 从配置文件加载配置
  purecpp_config::get_instance().load_config("cfg/user_config.json");

  // 初始化限流器
  rate_limiter::instance().init_from_config();

  // 启动文件热更新监视器（每 5 秒检查一次修改时间）
  file_watcher watcher;
  watcher.add("cfg/user_config.json", [] {
    purecpp_config::get_instance().reload_config();
    CINATRA_LOG_INFO << "cfg/user_config.json reloaded";
  });
  watcher.add("sensitive_words.txt", [] {
    sensitive_word_filter::instance().load("sensitive_words.txt");
    CINATRA_LOG_INFO << "sensitive_words.txt reloaded";
  });
  watcher.start(std::chrono::seconds(5));

  coro_http_server server(std::thread::hardware_concurrency(), 443);
  server.init_ssl("purecpp.pem", "purecpp.key");
  server.set_file_resp_format_type(file_resp_format_type::chunked);
  server.set_static_res_dir("", "html");
  server.set_http_handler<GET, POST>(
      "/",
      [](coro_http_request &req,
         coro_http_response &resp) -> async_simple::coro::Lazy<void> {
        auto url = req.get_url();
        std::string file_name = "html/index.html";
        coro_io::coro_file in_file{};
        if (!in_file.open(file_name, std::ios::in)) {
          resp.set_status(status_type::not_found);
          co_return;
        }
        std::string_view extension = get_extension(file_name);
        std::string_view mime = get_mime_type(extension);
        resp.add_header("Content-Type", std::string{mime});
        resp.set_format_type(format_type::chunked);

        // 开始chunked传输
        if (!co_await resp.get_conn()->begin_chunked()) {
          co_return;
        }
        std::string content;
        cinatra::detail::resize(content, in_file.file_size());
        while (true) {
          auto [ec, size] =
              co_await in_file.async_read(content.data(), content.size());
          if (ec) {
            resp.set_status(status_type::no_content);
            co_await resp.get_conn()->reply();
            co_return;
          }

          bool r = co_await resp.get_conn()->write_chunked(
              std::string_view(content.data(), size));
          if (!r) {
            co_return;
          }

          if (in_file.eof()) {
            co_await resp.get_conn()->end_chunked();
            break;
          }
        }
      });

  server.set_http_handler<GET>(
      "/api/v1/get_questions",
      [](coro_http_request &req, coro_http_response &resp) {
        size_t random_index = get_question_index();
        question_resp question{random_index, cpp_questions[random_index]};
        rest_response<question_resp> data{};
        data.data = question;

        std::string json = make_data(data, "获取问题成功");
        resp.set_content_type<resp_content_type::json>();
        resp.set_status_and_content(status_type::ok, std::move(json));
      });

  user_register_t usr_reg{};
  server.set_http_handler<POST>(
      "/api/v1/register", &user_register_t::handle_register, usr_reg,
      log_request_response{}, check_register_input{}, check_cpp_answer{},
      check_user_name{}, check_email{}, check_password{}, check_user_exists{},
      rate_limiter_aspect{}, experience_reward_aspect{});

  // 邮箱验证相关路由
  server.set_http_handler<POST>(
      "/api/v1/verify_email", &user_register_t::handle_verify_email, usr_reg,
      log_request_response{}, check_verify_email_input{});

  server.set_http_handler<POST>("/api/v1/resend_verify_email",
                                &user_register_t::handle_resend_verify_email,
                                usr_reg, log_request_response{},
                                rate_limiter_aspect{},
                                check_resend_verification_input{});

  user_login_t usr_login{};
  server.set_http_handler<POST>(
      "/api/v1/login", &user_login_t::handle_login, usr_login,
      log_request_response{}, check_login_input{}, experience_reward_aspect{});

  // 添加退出登录路由
  server.set_http_handler<POST, GET>(
      "/api/v1/logout", &user_login_t::handle_logout, usr_login,
      log_request_response{}, check_token{}, check_logout_input{});

  // 添加刷新token路由
  server.set_http_handler<POST>(
      "/api/v1/refresh_token", &user_login_t::handle_refresh_token, usr_login,
      log_request_response{}, check_refresh_token_input{});

  user_password_t usr_password{};
  server.set_http_handler<POST>(
      "/api/v1/change_password", &user_password_t::handle_change_password,
      usr_password, log_request_response{}, check_token{},
      check_change_password_input{}, check_new_password{});

  // 添加忘记密码和重置密码的路由
  server.set_http_handler<POST>(
      "/api/v1/forgot_password", &user_password_t::handle_forgot_password,
      usr_password, log_request_response{}, check_forgot_password_input{},
      rate_limiter_aspect{});

  server.set_http_handler<POST>(
      "/api/v1/reset_password", &user_password_t::handle_reset_password,
      usr_password, log_request_response{}, check_reset_password_input{},
      check_reset_password{});
  tags tag{};
  server.set_http_handler<GET>("/api/v1/get_tags", &tags::get_tags, tag,
                               log_request_response{});

  articles article{};
  server.set_http_handler<POST>(
      "/api/v1/new_article", &articles::handle_new_article, article,
      log_request_response{}, check_token{}, experience_reward_aspect{});
  server.set_http_handler<POST>("/api/v1/get_articles", &articles::get_articles,
                                article, log_request_response{});

  server.set_http_handler<GET>("/api/v1/article/:slug", &articles::show_article,
                               article, log_request_response{});
  server.set_http_handler<POST>("/api/v1/edit_article", &articles::edit_article,
                                article, log_request_response{}, check_token{},
                                check_edit_article{});
  server.set_http_handler<POST>("/api/v1/get_pending_articles",
                                &articles::get_pending_articles, article,
                                log_request_response{}, check_token{});
  server.set_http_handler<POST>("/api/v1/review_pending_article",
                                &articles::handle_review_article, article,
                                log_request_response{}, check_token{});
  server.set_http_handler<POST>("/api/v1/upload_file", &articles::upload_file,
                                article, log_request_response{}, check_token{},
                                check_upload_file{});

  // 评论相关路由
  articles_comment comment{};
  server.set_http_handler<GET>("/api/v1/get_article_comment/:slug",
                               &articles_comment::get_article_comment, comment,
                               log_request_response{}, check_get_comments{});
  server.set_http_handler<POST>(
      "/api/v1/add_article_comment", &articles_comment::add_article_comment,
      comment, log_request_response{}, check_token{}, check_add_comment{},
      experience_reward_aspect{});

  // 用户等级和积分相关路由
  user_level_api_t user_level_api{};
  server.set_http_handler<GET>(
      "/api/v1/user/level_info", &user_level_api_t::get_user_level,
      user_level_api, log_request_response{}, check_token{});
  server.set_http_handler<GET>("/api/v1/user/experience_transactions",
                               &user_level_api_t::get_experience_transactions,
                               user_level_api, log_request_response{},
                               check_token{});
  server.set_http_handler<POST>(
      "/api/v1/user/purchase_privilege", &user_level_api_t::purchase_privilege,
      user_level_api, log_request_response{}, check_token{});
  server.set_http_handler<POST>("/api/v1/user/gift_user",
                                &user_level_api_t::user_gifts, user_level_api,
                                log_request_response{}, check_token{});
  server.set_http_handler<GET>("/api/v1/user/available_privileges",
                               &user_level_api_t::get_available_privileges,
                               user_level_api, log_request_response{});

  // 用户个人信息相关路由
  user_profile_t user_profile{};
  server.set_http_handler<POST>("/api/v1/user/get_profile",
                                &user_profile_t::get_user_profile, user_profile,
                                log_request_response{});
  server.set_http_handler<POST>(
      "/api/v1/user/update_profile", &user_profile_t::update_user_profile,
      user_profile, log_request_response{}, check_token{});

  // 头像上传路由
  server.set_http_handler<POST>("/api/v1/user/upload_avatar",
                                &user_profile_t::upload_avatar, user_profile,
                                log_request_response{}, check_token{});
  // 处理上传到头像不能下载的问题
  server.set_http_handler<GET>(
      "/uploads/(.*)",
      [](coro_http_request &req,
         coro_http_response &resp) -> async_simple::coro::Lazy<void> {
        auto url = req.get_url();
        std::string file_name;
        file_name.append("html/").append(url);
        coro_io::coro_file in_file{};
        if (!in_file.open(file_name, std::ios::in)) {
          resp.set_status(status_type::not_found);
          co_return;
        }
        std::string_view extension = get_extension(file_name);
        std::string_view mime = get_mime_type(extension);
        resp.add_header("Content-Type", std::string{mime});
        resp.set_format_type(format_type::chunked);

        // 开始chunked传输
        if (!co_await resp.get_conn()->begin_chunked()) {
          co_return;
        }
        std::string content;
        cinatra::detail::resize(content, 10240);
        while (true) {
          auto [ec, size] =
              co_await in_file.async_read(content.data(), content.size());
          if (ec) {
            resp.set_status(status_type::no_content);
            co_await resp.get_conn()->reply();
            co_return;
          }

          bool r = co_await resp.get_conn()->write_chunked(
              std::string_view(content.data(), size));
          if (!r) {
            co_return;
          }

          if (in_file.eof()) {
            co_await resp.get_conn()->end_chunked();
            break;
          }
        }
      });

  // 用户文章相关路由
  server.set_http_handler<POST>("/api/v1/get_myarticles",
                                &articles::get_my_articles, article,
                                log_request_response{}, check_token{});

  // 用户评论相关路由
  server.set_http_handler<POST>("/api/v1/get_mycomments",
                                &articles_comment::get_my_comments, comment,
                                log_request_response{}, check_token{});

  // 删除文章路由
  server.set_http_handler<POST>("/api/v1/delete_myarticle",
                                &articles::delete_my_article, article,
                                log_request_response{}, check_token{});

  // 删除评论路由
  server.set_http_handler<POST>("/api/v1/delete_mycomment",
                                &articles_comment::delete_my_comment, comment,
                                log_request_response{}, check_token{});

  // 获取社区服务文章路由
  server.set_http_handler<POST>("/api/v1/get_community_service_articles",
                                &articles::get_community_service, article,
                                log_request_response{});

  // 获取purecpp大会文章路由
  server.set_http_handler<POST>("/api/v1/get_purecpp_conference_articles",
                                &articles::get_purecpp_conference, article,
                                log_request_response{});

  // 文章加精华/取消精华路由
  server.set_http_handler<POST>("/api/v1/toggle_featured",
                                &articles::toggle_featured, article,
                                log_request_response{}, check_token{});

  // 获取统计数据路由
  server.set_http_handler<GET>("/api/v1/stats", &articles::get_stats, article,
                               log_request_response{});
  // 聊天室路由
  chat_handler_t chat_h{};
  server.set_http_handler<GET>(
      "/api/v1/chat/channels", &chat_handler_t::get_channels, chat_h,
      log_request_response{}, check_token{}, rate_limiter_aspect{});
  server.set_http_handler<GET>(
      "/api/v1/chat/history", &chat_handler_t::get_history, chat_h,
      log_request_response{}, check_token{}, rate_limiter_aspect{});
  server.set_http_handler<GET>(
      "/api/v1/chat/search", &chat_handler_t::search_messages, chat_h,
      log_request_response{}, check_token{}, rate_limiter_aspect{});
  server.set_http_handler<POST>(
      "/api/v1/chat/channel", &chat_handler_t::create_channel, chat_h,
      log_request_response{}, check_token{}, rate_limiter_aspect{});
  server.set_http_handler<POST>(
      "/api/v1/chat/delete_channel", &chat_handler_t::delete_channel, chat_h,
      log_request_response{}, check_token{}, rate_limiter_aspect{});
  server.set_http_handler<POST>(
      "/api/v1/chat/mark_read", &chat_handler_t::mark_read, chat_h,
      log_request_response{}, check_token{}, rate_limiter_aspect{});
  server.set_http_handler<POST>(
      "/api/v1/chat/upload", &chat_handler_t::upload_file, chat_h,
      log_request_response{}, check_token{}, rate_limiter_aspect{});
  server.set_http_handler<POST>(
      "/api/v1/chat/mute_user", &chat_handler_t::mute_user, chat_h,
      log_request_response{}, check_token{}, rate_limiter_aspect{});
  server.set_http_handler<POST>(
      "/api/v1/chat/unmute_user", &chat_handler_t::unmute_user, chat_h,
      log_request_response{}, check_token{}, rate_limiter_aspect{});
  server.set_http_handler<GET>(
      "/ws/chat", &chat_handler_t::handle_ws, chat_h);

  server.sync_start();
}
