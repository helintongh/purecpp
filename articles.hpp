#pragma once

#include "articles_dto.hpp"
#include "common.hpp"
#include "user_aspects.hpp"

#include <iguana/xml_writer.hpp>

#include <map>
#include <random>

using namespace cinatra;

namespace purecpp {
struct client_artilce {
  std::string_view title;
  std::string_view excerpt;
  std::string_view content;
  std::string_view tag_ids;
};

struct article_page_request {
  int tag_id = 0;       // 0表示所有标签
  uint64_t user_id = 0; // 0表示所有用户
  int current_page;
  int per_page;
  std::string search; // 搜索关键词
};

struct article_list {
  std::string title;
  std::string summary;
  std::string slug;
  std::string author_name;
  uint64_t author_id;
  std::string tag_ids;
  uint64_t created_at;
  uint64_t updated_at;
  uint32_t views_count;
  uint32_t comments_count;
  int featured_weight;
};

struct pending_article_list {
  std::string title;
  std::string summary;
  std::string content;
  std::string slug;
  std::string author_name;
  std::string tag_ids;
  uint64_t created_at;
  uint64_t updated_at;
  uint32_t views_count;
  uint32_t comments_count;
};

static std::string_view REVIEW_REJECTED = "rejected"; // 审核_已拒绝
static std::string_view REVIEW_ACCEPTED =
    "accepted"; // 审核_已接受(只有审核通过才会是已发布)
struct review_opinion {
  std::string reviewer_name;
  std::string slug;
  std::string review_status;
  std::string review_comment; // 审核内容
};

struct article_detail {
  std::string title;
  std::string summary;
  std::string content;
  std::string author_name;
  std::string tag_ids;
  uint64_t created_at;
  uint64_t updated_at;
  uint32_t views_count;
  uint32_t comments_count;
  int featured_weight;
};

struct comments {
  std::string author_name;
  std::string parent_name;
  std::string_view content;
  uint64_t created_at;
  uint64_t updated_at;
};

struct article_upload_response {
  std::string url;
  std::string filename;
};

struct article_delete_request {
  std::string slug;
};

struct article_toggle_featured_request {
  std::string slug;
};

struct rss_item {
  std::string title;
  std::string link;
  std::string description;
  std::string author;
  std::string pubDate;
  std::string guid;
};
YLT_REFL(rss_item, title, link, description, author, pubDate, guid);

struct rss_channel {
  std::string title;
  std::string link;
  std::string description;
  std::string language;
  std::string lastBuildDate;
  std::vector<rss_item> item;
};
YLT_REFL(rss_channel, title, link, description, language, lastBuildDate, item);

struct rss_feed {
  rss_channel channel;
};
YLT_REFL(rss_feed, channel);

inline constexpr std::string_view get_alias_struct_name(rss_feed *) {
  return "rss";
}

struct rss_article_row {
  std::string title;
  std::string abstraction;
  std::string slug;
  std::string user_name;
  uint64_t created_at;
};
YLT_REFL(rss_article_row, title, abstraction, slug, user_name, created_at);

inline void generate_random_string(auto &random_str) {
  static const std::string chars = "abcdefghijklmnopqrstuvwxyz"
                                   "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                   "0123456789";

  static std::random_device rd;

  static std::mt19937 generator(rd());

  static std::uniform_int_distribution<> distribution(0, chars.length() - 1);

  for (std::size_t i = 0; i < random_str.size(); ++i) {
    random_str[i] = chars[distribution(generator)];
  }
}

class articles {
public:
  void handle_new_article(coro_http_request &req, coro_http_response &resp) {
    auto body = req.get_body();
    if (body.empty()) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("无效的请求参数，请求体不能为空"));
      return;
    }

    client_artilce art{};
    std::error_code ec;
    iguana::from_json(art, body, ec);
    if (ec) {
      resp.set_status_and_content(
          status_type::bad_request,
          make_error("无效的请求参数，JSON格式错误: " + ec.message()));
      return;
    }

    // 验证标题
    if (art.title.empty()) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("标题不能为空"));
      return;
    }
    if (art.title.size() > 100) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("标题太长，不要超过100个字符"));
      return;
    }

    // 验证摘要
    if (art.excerpt.empty()) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("摘要不能为空"));
      return;
    }
    if (art.excerpt.size() > 300) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("摘要太长，不要超过300个字符"));
      return;
    }

    // 验证内容
    if (art.content.empty()) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("内容不能为空"));
      return;
    }
    if (art.content.size() > 64 * 1024) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("内容太长，不要超过64KB个字符"));
      return;
    }

    // 验证标签ID
    if (art.tag_ids.empty()) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("请至少选择一个标签"));
      return;
    }

    // 从token中提取用户ID
    auto user_id = purecpp::get_user_id_from_token(req);
    if (user_id == 0) {
      resp.set_status_and_content(status_type::unauthorized,
                                  make_error("用户未登录或登录已过期"));
      return;
    }

    articles_t article{};
    article.tag_ids = art.tag_ids;
    article.title = art.title;
    article.abstraction = art.excerpt;
    article.content = art.content;
    article.created_at = get_timestamp_milliseconds();
    article.updated_at = get_timestamp_milliseconds();
    article.author_id = user_id;
    article.status = PENDING_REVIEW;
    article.is_deleted = false;
    article.views_count = 0;
    article.comments_count = 0;
    generate_random_string(article.slug);

    size_t pos = 0;

    // 查找并替换所有 \" 为 "
    while ((pos = article.content.find("\\\"", pos)) != std::string::npos) {
      article.content.replace(pos, 2, "\""); // 替换 2 个字符(\") 为 1 个字符(")
      pos += 1;
    }

    auto conn = get_db_pool().get();
    if (conn == nullptr) {
      set_server_internel_error(resp);
      return;
    }

    int retry = 5;
    uint64_t article_id = 0;
    for (; retry > 0; retry--) {
      article_id = conn->get_insert_id_after_insert(article);
      if (article_id > 0) {
        break;
      }
      generate_random_string(article.slug);
    }

    if (retry == 0) {
      auto err = conn->get_last_error();
      CINATRA_LOG_ERROR << "提交文章失败: " << err;
      set_server_internel_error(resp);
      return;
    }

    resp.set_status_and_content(status_type::ok,
                                make_success("文章提交成功，等待审核"));
  }

  void show_article(coro_http_request &req, coro_http_response &resp) {
    auto it = req.params_.find("slug");
    if (it == req.params_.end()) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("无效的请求参数，缺少文章标识符"));
      return;
    }

    auto slug = it->second;
    auto conn = get_db_pool().get();
    if (conn == nullptr) {
      set_server_internel_error(resp);
      return;
    }

    // 先更新浏览量
    conn->update<articles_t>()
        .set(col(&articles_t::views_count),
             ormpp::raw_sql("views_count + 1"))
        .where(col(&articles_t::slug) == std::string(slug))
        .execute();

    // 再获取文章详情
    auto list =
        conn->select(col(&articles_t::title), col(&articles_t::abstraction),
                     col(&articles_t::content), col(&users_t::user_name),
                     col(&articles_t::tag_ids), col(&articles_t::created_at),
                     col(&articles_t::updated_at),
                     col(&articles_t::views_count),
                     col(&articles_t::comments_count),
                     col(&articles_t::featured_weight))
            .from<articles_t>()
            .inner_join(col(&articles_t::author_id), col(&users_t::id))
            .where(col(&articles_t::slug).param() &&
                   col(&articles_t::is_deleted) == 0)
            .collect<article_detail>(slug);

    if (!list.empty()) {
      std::string json = make_data(std::move(list[0]), "获取文章详情成功");
      resp.set_status_and_content(status_type::ok, std::move(json));
    } else {
      resp.set_status_and_content(status_type::not_found,
                                  make_error("文章不存在或已被删除"));
    }
  }

  void edit_article(coro_http_request &req, coro_http_response &resp) {
    edit_article_info info =
        std::any_cast<edit_article_info>(req.get_user_data());
    auto conn = get_db_pool().get();
    if (conn == nullptr) {
      set_server_internel_error(resp);
      return;
    }

    // 文章编辑以后，上次审核结果也删掉
    int n = conn->update<articles_t>()
                .set(col(&articles_t::tag_ids), info.tag_ids)
                .set(col(&articles_t::title), info.title)
                .set(col(&articles_t::abstraction), info.excerpt)
                .set(col(&articles_t::content), info.content)
                .set(col(&articles_t::status),
                     std::string(PENDING_REVIEW.data()))
                .set(col(&articles_t::reviewer_id), 0)
                .set(col(&articles_t::review_comment), std::string{})
                .set(col(&articles_t::review_date), 0)
                .set(col(&articles_t::updated_at),
                     get_timestamp_milliseconds())
                .where(col(&articles_t::slug) == info.slug)
                .execute();

    if (n == 0) {
      set_server_internel_error(resp);
      return;
    }
    std::string json = make_success("修改成功");
    resp.set_status_and_content(status_type::ok, std::move(json));
  }

  void get_articles(coro_http_request &req, coro_http_response &resp) {
    auto conn = get_db_pool().get();
    if (conn == nullptr) {
      set_server_internel_error(resp);
      return;
    }

    // 从请求体中获取分页信息
    auto body = req.get_body();
    article_page_request page_req{};
    std::error_code ec;
    iguana::from_json(page_req, body, ec);
    if (ec) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("无效的请求参数"));
      return;
    }

    int page = 1;
    int per_page = 10;

    if (page_req.current_page > 0) {
      page = page_req.current_page;
    }
    if (page_req.per_page > 0 && page_req.per_page <= 50) { // 限制每页最多50条
      per_page = page_req.per_page;
    }

    // 查询TECH_ARTICLES分组下的所有标签ID
    auto tech_articles_tags =
        conn->select(col(&tags_t::tag_id))
            .from<tags_t>()
            .where(col(&tags_t::tag_group) ==
                   static_cast<int>(TagGroupType::TECH_ARTICLES))
            .collect();

    if (tech_articles_tags.empty()) {
      // 如果没有TECH_ARTICLES分组的标签，则返回空列表
      std::string json =
          make_data(std::vector<article_list>(), "获取文章列表成功", 0);
      resp.set_status_and_content(status_type::ok, std::move(json));
      return;
    }

    // 构建查询条件：文章已发布且未删除，并且tag_ids包含至少一个TECH_ARTICLES分组的标签
    auto where_cond0 = col(&articles_t::is_deleted) == 0 &&
                       col(&articles_t::status) == PUBLISHED.data();

    // 构建标签ID的OR条件
    bool first = true;
    decltype(where_cond0) col_tags;
    for (const auto &tag : tech_articles_tags) {
      int tag_id = std::get<0>(tag);
      if (first) {
        col_tags =
            col(&articles_t::tag_ids).like("%" + std::to_string(tag_id) + "%");
        first = false;
      } else {
        col_tags =
            col_tags ||
            col(&articles_t::tag_ids).like("%" + std::to_string(tag_id) + "%");
      }
    }
    auto where_cond = where_cond0 && col_tags;

    // tag_ids字段存储多个标签
    if (page_req.tag_id > 0) {
      where_cond = where_cond0 &&
                   (col(&articles_t::tag_ids)
                        .like("%" + std::to_string(page_req.tag_id) + "%"));
    }
    if (page_req.user_id > 0) {
      where_cond =
          where_cond && (col(&articles_t::author_id) == page_req.user_id);
    }
    // 搜索功能
    if (!page_req.search.empty()) {
      std::string search_pattern = "%" + page_req.search + "%";
      where_cond = where_cond && col(&articles_t::content).like(search_pattern);
    }
    // 计算总记录数(根据查询条件)
    size_t total_count =
        conn->select(ormpp::count())
            .from<articles_t>()
            .inner_join(col(&articles_t::author_id), col(&users_t::id))
            .where(where_cond)
            .collect();

    auto select_cond =
        conn->select(col(&articles_t::title), col(&articles_t::abstraction),
                     col(&articles_t::slug), col(&users_t::user_name),
                     col(&articles_t::author_id), col(&articles_t::tag_ids),
                     col(&articles_t::created_at), col(&articles_t::updated_at),
                     col(&articles_t::views_count),
                     col(&articles_t::comments_count),
                     col(&articles_t::featured_weight))
            .from<articles_t>()
            .inner_join(col(&articles_t::author_id), col(&users_t::id))
            .where(where_cond);
    size_t limit = per_page;
    size_t offset = (page - 1) * per_page;
    auto list = select_cond.order_by(col(&articles_t::created_at).desc())
                    .limit(ormpp::token)
                    .offset(ormpp::token)
                    .collect<article_list>(limit, offset);

    std::string json =
        make_data(std::move(list), "获取文章列表成功", total_count);
    if (json.empty()) {
      set_server_internel_error(resp);
      return;
    }

    resp.set_status_and_content(status_type::ok, std::move(json));
  }

  void get_pending_articles(coro_http_request &req, coro_http_response &resp) {
    auto conn = get_db_pool().get();
    if (conn == nullptr) {
      set_server_internel_error(resp);
      return;
    }

    size_t limit = 20; // will update, it's from web front end.
    size_t offset = 0; // will update, it's from web front end.
    std::string search;

    // 从请求体中获取分页和搜索参数
    auto body = req.get_body();
    if (!body.empty()) {
      article_page_request page_req{};
      std::error_code ec;
      iguana::from_json(page_req, body, ec);
      if (!ec) {
        if (page_req.per_page > 0 &&
            page_req.per_page <= 50) { // 限制每页最多50条
          limit = page_req.per_page;
        }
        if (page_req.current_page > 0) {
          offset = (page_req.current_page - 1) * limit;
        }
        search = page_req.search;
      }
    }

    // 构建查询条件
    auto where_cond = col(&articles_t::is_deleted) == 0 &&
                      col(&articles_t::status) == PENDING_REVIEW.data();

    // 搜索功能
    if (!search.empty()) {
      std::string search_pattern = "%" + search + "%";
      where_cond = where_cond && col(&articles_t::content).like(search_pattern);
    }

    // 计算总记录数
    size_t total_count =
        conn->select(ormpp::count())
            .from<articles_t>()
            .inner_join(col(&articles_t::author_id), col(&users_t::id))
            .where(where_cond)
            .collect();

    auto list =
        conn->select(col(&articles_t::title), col(&articles_t::abstraction),
                     col(&articles_t::content), col(&articles_t::slug),
                     col(&users_t::user_name), col(&articles_t::tag_ids),
                     col(&articles_t::created_at), col(&articles_t::updated_at),
                     col(&articles_t::views_count),
                     col(&articles_t::comments_count))
            .from<articles_t>()
            .inner_join(col(&articles_t::author_id), col(&users_t::id))
            .where(where_cond)
            .order_by(col(&articles_t::created_at).desc())
            .limit(ormpp::token)
            .offset(ormpp::token)
            .collect<pending_article_list>(limit, offset);

    std::string json =
        make_data(std::move(list), "获取待审核文章列表成功", total_count);
    if (json.empty()) {
      set_server_internel_error(resp);
      return;
    }

    resp.set_status_and_content(status_type::ok, std::move(json));
  }

  void handle_review_article(coro_http_request &req, coro_http_response &resp) {
    auto body = req.get_body();
    if (body.empty()) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("无效的请求参数，请求体不能为空"));
      return;
    }

    review_opinion request{};
    std::error_code ec;
    iguana::from_json(request, body, ec);
    if (ec) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("无效的请求参数，JSON格式错误"));
      return;
    }
    auto conn = get_db_pool().get();
    if (conn == nullptr) {
      set_server_internel_error(resp);
      return;
    }
    // 检查审核人是否是管理员
    auto user_id = get_user_id_from_token(req);
    if (user_id == 0) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("无效的请求参数"));
      return;
    }
    auto users_vect = conn->select(ormpp::all)
                          .from<users_t>()
                          .where(col(&users_t::id) == user_id)
                          .collect();
    if (users_vect.empty()) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("无效的请求参数"));
      return;
    }

    auto &review_user = users_vect.front();
    if (review_user.role != "admin" && review_user.role != "superadmin") {
      resp.set_status_and_content(
          status_type::bad_request,
          make_error("无效的请求参数，审核人必须是管理员"));
      return;
    }
    // 检查审核人名称是否匹配
    if (request.reviewer_name.empty() &&
        strcmp(request.reviewer_name.data(), review_user.user_name.data()) !=
            0) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("无效的请求参数，审核人不能为空"));
      return;
    }
    // 检查审核结论
    if (request.review_status != REVIEW_ACCEPTED &&
        request.review_status != REVIEW_REJECTED) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("无效的请求参数，审核状态必须是" +
                                             std::string(REVIEW_ACCEPTED) +
                                             "或" +
                                             std::string(REVIEW_REJECTED)));
      return;
    }

    // 更新最近一次审核状态及意见
    int n = conn->update<articles_t>()
                .set(col(&articles_t::reviewer_id), review_user.id)
                .set(col(&articles_t::review_date),
                     get_timestamp_milliseconds())
                .set(col(&articles_t::review_comment),
                     request.review_comment)
                .set(col(&articles_t::status),
                     request.review_status == REVIEW_ACCEPTED
                         ? std::string(PUBLISHED)
                         : std::string(REJECTED))
                .where(col(&articles_t::slug) == request.slug)
                .execute();
    if (n == 0) {
      set_server_internel_error(resp);
      return;
    }
    std::string json = make_success("审核成功");
    resp.set_status_and_content(status_type::ok, std::move(json));
  }

  void upload_file(coro_http_request &req, coro_http_response &resp) {
    auto info = std::any_cast<upload_file_info>(req.get_user_data());

    // 解码base64图片数据
    auto file_data = cinatra::base64_decode(std::string(info.file_data));
    if (!file_data.has_value()) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("base64图片数据解码失败"));
      return;
    }

    std::string &file_data_str = file_data.value();

    if (file_data_str.size() > MAX_FILE_SIZE) {
      resp.set_status_and_content(
          status_type::bad_request,
          make_error(PURECPP_ERROR_UPLOAD_FILE_SIZE_EXCEED));
      return;
    }

    std::filesystem::path upload_dir = "html/uploads/articles";
    if (!std::filesystem::exists(upload_dir)) {
      std::filesystem::create_directories(upload_dir);
    }

    auto ext = cinatra::get_extension(info.filename);

    // 生成唯一文件名
    std::string unique_filename =
        std::to_string(get_timestamp_milliseconds()) + std::string(ext);
    std::filesystem::path file_path = upload_dir / unique_filename;

    // 保存文件
    std::ofstream out_file(file_path, std::ios::binary);
    if (!out_file) {
      resp.set_status_and_content(status_type::internal_server_error,
                                  make_error("保存文件失败"));
      return;
    }
    out_file.write(reinterpret_cast<const char *>(file_data_str.data()),
                   file_data_str.size());
    out_file.close();

    // 生成文件URL
    std::string file_url = "/uploads/articles/" + unique_filename;
    // 构建响应
    article_upload_response data{file_url, unique_filename};
    std::string json = make_data(data, "文件上传成功");
    resp.set_status_and_content(status_type::ok, std::move(json));
  }

  // 获取用户的文章列表
  void get_my_articles(coro_http_request &req, coro_http_response &resp) {
    auto body = req.get_body();
    if (body.empty()) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("无效的请求参数，请求体不能为空"));
      return;
    }

    // 从请求体中获取分页信息
    my_article_request page_req{};
    std::error_code ec;
    iguana::from_json(page_req, body, ec);
    if (ec) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("无效的请求参数"));
      return;
    }

    int page = 1;
    int per_page = 10;

    if (page_req.current_page > 0) {
      page = page_req.current_page;
    }
    if (page_req.per_page > 0 && page_req.per_page <= 50) { // 限制每页最多50条
      per_page = page_req.per_page;
    }

    // 验证用户ID
    if (page_req.user_id == 0) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("无效的请求参数，用户ID不能为空"));
      return;
    }

    // 检查当前用户是否有权限查看
    auto current_user_id = purecpp::get_user_id_from_token(req);
    if (current_user_id == 0) {
      resp.set_status_and_content(status_type::unauthorized,
                                  make_error("用户未登录或登录已过期"));
      return;
    }

    // 只有自己可以查看自己的文章列表
    if (current_user_id != page_req.user_id) {
      resp.set_status_and_content(status_type::forbidden,
                                  make_error("没有权限查看其他用户的文章"));
      return;
    }

    auto conn = get_db_pool().get();
    if (conn == nullptr) {
      set_server_internel_error(resp);
      return;
    }

    // 构建查询条件
    auto where_cond = col(&articles_t::author_id) == page_req.user_id &&
                      col(&articles_t::is_deleted) == 0;

    // 计算总记录数
    size_t total_count = conn->select(ormpp::count())
                             .from<articles_t>()
                             .where(where_cond)
                             .collect();

    // 计算分页参数
    size_t limit = per_page;
    size_t offset = (page - 1) * per_page;

    // 获取用户的文章列表
    auto articles_list =
        conn->select(col(&articles_t::article_id), col(&articles_t::title),
                     col(&articles_t::abstraction), col(&articles_t::content),
                     col(&articles_t::slug), col(&articles_t::status),
                     col(&articles_t::created_at), col(&articles_t::updated_at),
                     col(&articles_t::views_count),
                     col(&articles_t::comments_count),
                     col(&articles_t::review_comment))
            .from<articles_t>()
            .where(where_cond)
            .order_by(col(&articles_t::created_at).desc())
            .limit(ormpp::token)
            .offset(ormpp::token)
            .collect<my_article_item>(limit, offset);

    std::string json = make_data(std::move(articles_list),
                                 "获取用户文章列表成功", total_count);
    if (json.empty()) {
      set_server_internel_error(resp);
      return;
    }

    resp.set_status_and_content(status_type::ok, std::move(json));
  }

  // 删除文章
  void delete_my_article(coro_http_request &req, coro_http_response &resp) {
    auto body = req.get_body();
    if (body.empty()) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("无效的请求参数，请求体不能为空"));
      return;
    }

    // 解析请求参数
    article_delete_request request{};
    std::error_code ec;
    iguana::from_json(request, body, ec);
    if (ec) {
      resp.set_status_and_content(
          status_type::bad_request,
          make_error("无效的请求参数，JSON格式错误: " + ec.message()));
      return;
    }

    // 验证文章ID
    if (request.slug.empty()) {
      resp.set_status_and_content(
          status_type::bad_request,
          make_error("无效的请求参数，文章Slug不能为空"));
      return;
    }

    // 获取当前用户ID
    auto current_user_id = purecpp::get_user_id_from_token(req);
    if (current_user_id == 0) {
      resp.set_status_and_content(status_type::unauthorized,
                                  make_error("用户未登录或登录已过期"));
      return;
    }

    auto conn = get_db_pool().get();
    if (conn == nullptr) {
      set_server_internel_error(resp);
      return;
    }

    // 检查文章是否存在，并且是否是当前用户的文章
    auto articles = conn->select(col(&articles_t::author_id))
                        .from<articles_t>()
                        .where(col(&articles_t::slug).param() &&
                               col(&articles_t::is_deleted).param())
                        .collect(request.slug, 0);

    if (articles.empty()) {
      resp.set_status_and_content(status_type::not_found,
                                  make_error("文章不存在或已被删除"));
      return;
    }

    uint64_t article_author_id = std::get<0>(articles.front());

    // 检查当前用户是否是文章作者
    if (current_user_id != article_author_id) {
      resp.set_status_and_content(status_type::forbidden,
                                  make_error("没有权限删除其他用户的文章"));
      return;
    }

    // 标记文章为已删除
    int n = conn->update<articles_t>()
                .set(col(&articles_t::is_deleted), true)
                .set(col(&articles_t::updated_at),
                     get_timestamp_milliseconds())
                .where(col(&articles_t::slug) == request.slug)
                .execute();
    if (n == 0) {
      set_server_internel_error(resp);
      return;
    }

    std::string json = make_success("文章删除成功");
    resp.set_status_and_content(status_type::ok, std::move(json));
  }

  // 获取社区服务文章
  void get_community_service(coro_http_request &req, coro_http_response &resp) {
    auto conn = get_db_pool().get();
    if (conn == nullptr) {
      set_server_internel_error(resp);
      return;
    }

    // 从请求体中获取分页信息
    auto body = req.get_body();
    article_page_request page_req{};
    std::error_code ec;
    if (!body.empty()) {
      iguana::from_json(page_req, body, ec);
    }

    int page = 1;
    int per_page = 10;

    if (page_req.current_page > 0) {
      page = page_req.current_page;
    }
    if (page_req.per_page > 0 && page_req.per_page <= 50) {
      per_page = page_req.per_page;
    }

    // 查询SERVICES分组下的所有标签ID
    auto services_tags = conn->select(col(&tags_t::tag_id))
                             .from<tags_t>()
                             .where(col(&tags_t::tag_group) ==
                                    static_cast<int>(TagGroupType::SERVICES))
                             .collect();

    if (services_tags.empty()) {
      // 如果没有SERVICES分组的标签，则返回空列表
      std::string json =
          make_data(std::vector<article_list>(), "获取社区服务文章列表成功", 0);
      resp.set_status_and_content(status_type::ok, std::move(json));
      return;
    }

    // 构建查询条件：文章已发布且未删除，并且tag_ids包含至少一个SERVICES分组的标签
    auto where_cond = col(&articles_t::is_deleted) == 0 &&
                      col(&articles_t::status) == PUBLISHED.data();

    // 构建标签ID的OR条件
    bool first = true;
    decltype(where_cond) col_tags;
    for (const auto &tag : services_tags) {
      int tag_id = std::get<0>(tag);
      if (first) {
        col_tags =
            col(&articles_t::tag_ids).like("%" + std::to_string(tag_id) + "%");
        first = false;
      } else {
        col_tags =
            col_tags ||
            col(&articles_t::tag_ids).like("%" + std::to_string(tag_id) + "%");
      }
    }
    where_cond = where_cond && col_tags;

    // 计算总记录数
    size_t total_count =
        conn->select(ormpp::count())
            .from<articles_t>()
            .inner_join(col(&articles_t::author_id), col(&users_t::id))
            .where(where_cond)
            .collect();

    // 计算分页参数
    size_t limit = per_page;
    size_t offset = (page - 1) * per_page;

    // 获取社区服务文章列表
    auto articles_list =
        conn->select(col(&articles_t::title), col(&articles_t::abstraction),
                     col(&articles_t::slug), col(&users_t::user_name),
                     col(&articles_t::author_id), col(&articles_t::tag_ids),
                     col(&articles_t::created_at), col(&articles_t::updated_at),
                     col(&articles_t::views_count),
                     col(&articles_t::comments_count))
            .from<articles_t>()
            .inner_join(col(&articles_t::author_id), col(&users_t::id))
            .where(where_cond)
            .order_by(col(&articles_t::created_at).desc())
            .limit(ormpp::token)
            .offset(ormpp::token)
            .collect<article_list>(limit, offset);

    std::string json = make_data(std::move(articles_list),
                                 "获取社区服务文章列表成功", total_count);
    if (json.empty()) {
      set_server_internel_error(resp);
      return;
    }

    resp.set_status_and_content(status_type::ok, std::move(json));
  }

  // 获取purecpp大会文章
  void get_purecpp_conference(coro_http_request &req,
                              coro_http_response &resp) {
    auto conn = get_db_pool().get();
    if (conn == nullptr) {
      set_server_internel_error(resp);
      return;
    }

    // 从请求体中获取分页信息
    auto body = req.get_body();
    article_page_request page_req{};
    std::error_code ec;
    if (!body.empty()) {
      iguana::from_json(page_req, body, ec);
    }

    int page = 1;
    int per_page = 10;

    if (page_req.current_page > 0) {
      page = page_req.current_page;
    }
    if (page_req.per_page > 0 && page_req.per_page <= 50) {
      per_page = page_req.per_page;
    }

    // 查询CPP_PARTY分组下的所有标签ID
    auto cpp_party_tags = conn->select(col(&tags_t::tag_id))
                              .from<tags_t>()
                              .where(col(&tags_t::tag_group) ==
                                     static_cast<int>(TagGroupType::CPP_PARTY))
                              .collect();

    if (cpp_party_tags.empty()) {
      // 如果没有CPP_PARTY分组的标签，则返回空列表
      std::string json = make_data(std::vector<article_list>(),
                                   "获取purecpp大会文章列表成功", 0);
      resp.set_status_and_content(status_type::ok, std::move(json));
      return;
    }

    // 构建查询条件：文章已发布且未删除，并且tag_ids包含至少一个CPP_PARTY分组的标签
    auto where_cond = col(&articles_t::is_deleted) == 0 &&
                      col(&articles_t::status) == PUBLISHED.data();

    // 构建标签ID的OR条件
    bool first = true;
    decltype(where_cond) col_tags;
    for (const auto &tag : cpp_party_tags) {
      int tag_id = std::get<0>(tag);
      if (first) {
        col_tags =
            col(&articles_t::tag_ids).like("%" + std::to_string(tag_id) + "%");
        first = false;
      } else {
        col_tags =
            col_tags ||
            col(&articles_t::tag_ids).like("%" + std::to_string(tag_id) + "%");
      }
    }
    where_cond = where_cond && col_tags;

    // 计算总记录数
    size_t total_count =
        conn->select(ormpp::count())
            .from<articles_t>()
            .inner_join(col(&articles_t::author_id), col(&users_t::id))
            .where(where_cond)
            .collect();

    // 计算分页参数
    size_t limit = per_page;
    size_t offset = (page - 1) * per_page;

    // 获取purecpp大会文章列表
    auto articles_list =
        conn->select(col(&articles_t::title), col(&articles_t::abstraction),
                     col(&articles_t::slug), col(&users_t::user_name),
                     col(&articles_t::author_id), col(&articles_t::tag_ids),
                     col(&articles_t::created_at), col(&articles_t::updated_at),
                     col(&articles_t::views_count),
                     col(&articles_t::comments_count),
                     col(&articles_t::featured_weight))
            .from<articles_t>()
            .inner_join(col(&articles_t::author_id), col(&users_t::id))
            .where(where_cond)
            .order_by(col(&articles_t::created_at).desc())
            .limit(ormpp::token)
            .offset(ormpp::token)
            .collect<article_list>(limit, offset);

    std::string json = make_data(std::move(articles_list),
                                 "获取purecpp大会文章列表成功", total_count);
    if (json.empty()) {
      set_server_internel_error(resp);
      return;
    }

    resp.set_status_and_content(status_type::ok, std::move(json));
  }

  // 处理文章加精华/取消精华
  void toggle_featured(coro_http_request &req, coro_http_response &resp) {
    auto body = req.get_body();
    if (body.empty()) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("无效的请求参数，请求体不能为空"));
      return;
    }

    article_toggle_featured_request request{};
    std::error_code ec;
    iguana::from_json(request, body, ec);
    if (ec) {
      resp.set_status_and_content(
          status_type::bad_request,
          make_error("无效的请求参数，JSON格式错误: " + ec.message()));
      return;
    }

    // 检查用户是否是管理员
    auto user_id = get_user_id_from_token(req);
    if (user_id == 0) {
      resp.set_status_and_content(status_type::unauthorized,
                                  make_error("用户未登录或登录已过期"));
      return;
    }

    auto conn = get_db_pool().get();
    if (conn == nullptr) {
      set_server_internel_error(resp);
      return;
    }

    auto users_vect = conn->select(ormpp::all)
                          .from<users_t>()
                          .where(col(&users_t::id) == user_id)
                          .collect();
    if (users_vect.empty()) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("无效的请求参数"));
      return;
    }

    auto &user = users_vect.front();
    if (user.role != "admin" && user.role != "superadmin") {
      resp.set_status_and_content(status_type::forbidden,
                                  make_error("权限不足，只有管理员可以加精华"));
      return;
    }

    // 获取当前文章的featured_weight值
    auto article_vect = conn->select(col(&articles_t::tag_ids))
                            .from<articles_t>()
                            .where(col(&articles_t::slug) == request.slug &&
                                   col(&articles_t::is_deleted) == 0)
                            .collect();
    if (article_vect.empty()) {
      resp.set_status_and_content(status_type::not_found,
                                  make_error("文章不存在或已被删除"));
      return;
    }

    std::string current_tag_ids = std::get<0>(article_vect.front());
    std::string new_tag_ids = current_tag_ids;
    if (current_tag_ids.find("108") != std::string::npos) {
      new_tag_ids.erase(new_tag_ids.find("108"), 3);
    } else {
      new_tag_ids += ((current_tag_ids.length() > 0 &&
                       current_tag_ids.at(current_tag_ids.length() - 1) == '|')
                          ? "108"
                          : "|108");
    }

    if (new_tag_ids.length() < 3) {
      resp.set_status_and_content(
          status_type::bad_request,
          make_error("文章只有‘社区精华’标签，不能取消精华，文章标签不能为空"));
      return;
    }

    // 更新tag_ids值
    int n = conn->update<articles_t>()
                .set(col(&articles_t::tag_ids), new_tag_ids)
                .set(col(&articles_t::updated_at),
                     get_timestamp_milliseconds())
                .where(col(&articles_t::slug) == request.slug)
                .execute();

    if (n == 0) {
      set_server_internel_error(resp);
      return;
    }

    std::string message = (new_tag_ids.find("108") != std::string::npos)
                              ? "文章已成功加精华"
                              : "文章已取消精华";
    std::string json = make_success(message);
    resp.set_status_and_content(status_type::ok, std::move(json));
  }

  // 获取统计数据
  void get_stats(coro_http_request &req, coro_http_response &resp) {
    auto &config = purecpp_config::get_instance();
    auto conn = get_db_pool().get();
    if (conn == nullptr) {
      set_server_internel_error(resp);
      return;
    }

    // 获取注册会员数
    int user_count = conn->select(ormpp::count()).from<users_t>().collect();

    // 获取技术文章数
    int article_count =
        conn->select(ormpp::count()).from<articles_t>().collect();

    // 参会人数（这里使用模拟数据，实际项目中可能需要从专门的表中获取）
    int conference_attendees = 12000;

    stats_data data{.user_count =
                        user_count + config.user_cfg_.default_user_count,
                    .article_count = article_count};

    std::string json = make_data(data, "获取统计数据成功");
    if (json.empty()) {
      set_server_internel_error(resp);
      return;
    }
    resp.set_status_and_content(status_type::ok, std::move(json));
  }

  void get_rss_feed(coro_http_request &req, coro_http_response &resp) {
    auto conn = get_db_pool().get();
    if (conn == nullptr) {
      set_server_internel_error(resp);
      return;
    }

    // ormpp maps selected columns to the target type positionally,
    // so RSS uses a dedicated 5-field projection instead of article_list.
    auto articles_list =
        conn->select(col(&articles_t::title), col(&articles_t::abstraction),
                     col(&articles_t::slug), col(&users_t::user_name),
                     col(&articles_t::created_at))
            .from<articles_t>()
            .inner_join(col(&articles_t::author_id), col(&users_t::id))
            .where(col(&articles_t::is_deleted) == 0 &&
                   col(&articles_t::status) == std::string(PUBLISHED))
            .order_by(col(&articles_t::created_at).desc())
            .limit(20)
            .collect<rss_article_row>();

    auto base_url = get_base_url(req);
    auto site_link = make_absolute_url(base_url, "/");

    rss_feed feed{};
    feed.channel.title = "PureCpp";
    feed.channel.link = site_link;
    feed.channel.description = "PureCpp 最新技术文章订阅";
    feed.channel.language = "zh-cn";
    feed.channel.lastBuildDate =
        format_rss_pub_date(get_timestamp_milliseconds());
    feed.channel.item.reserve(articles_list.size());

    for (const auto &article : articles_list) {
      rss_item item{};
      item.title = article.title;
      item.link = make_absolute_url(
          base_url, "/article.html?slug=" + article.slug);
      item.description = article.abstraction;
      item.author = article.user_name;
      item.pubDate = format_rss_pub_date(article.created_at);
      item.guid = item.link;
      feed.channel.item.emplace_back(std::move(item));
    }

    iguana::xml_attr_t<rss_feed, std::map<std::string, std::string>> xml_feed;
    xml_feed.attr()["version"] = "2.0";
    xml_feed.value() = std::move(feed);

    std::string xml;
    try {
      iguana::to_xml<true>(xml_feed, xml);
    } catch (const std::exception &e) {
      CINATRA_LOG_ERROR << "generate rss feed failed: " << e.what();
      set_server_internel_error(resp);
      return;
    }

    resp.add_header("Content-Type", "application/rss+xml; charset=utf-8");
    resp.set_status_and_content(status_type::ok, std::move(xml));
  }
};
} // namespace purecpp
