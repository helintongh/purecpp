#pragma once
#include "articles_aspects.hpp"
#include "articles_dto.hpp"
#include "common.hpp"

#include <string>
#include <vector>
#include <algorithm>

#include <cinatra.hpp>

using namespace cinatra;

namespace purecpp {

struct article_user_comments_request {
  uint64_t user_id;
  int current_page;
  int per_page;
};

struct article_delete_comment_request {
  uint64_t comment_id;
};

class articles_comment {
public:
  // 获取文章评论
  void get_article_comment(coro_http_request &req, coro_http_response &resp) {
    auto request = std::any_cast<get_comments_request>(req.get_user_data());

    auto conn = get_db_pool().get();
    if (conn == nullptr) {
      set_server_internel_error(resp);
      return;
    }

    // 获取文章id
    auto article_vec = conn->select(col(&articles_t::article_id))
                           .from<articles_t>()
                           .where(col(&articles_t::slug).param())
                           .collect(request.slug);
    if (article_vec.empty()) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("评论文章未找到"));
      return;
    }

    uint64_t article_id = std::get<0>(article_vec.front());

    // 获取评论列表
    auto comments =
        conn->select(col(&article_comments_t::comment_id),
                     col(&article_comments_t::article_id),
                     col(&article_comments_t::user_id),
                     col(&users_t::user_name),
                     col(&article_comments_t::content),
                     col(&article_comments_t::parent_comment_id),
                     col(&article_comments_t::parent_user_name),
                     col(&article_comments_t::ip),
                     col(&article_comments_t::comment_status),
                     col(&article_comments_t::created_at),
                     col(&article_comments_t::updated_at))
            .from<article_comments_t>()
            .inner_join(col(&article_comments_t::user_id), col(&users_t::id))
            .where(col(&article_comments_t::article_id).param())
            .order_by(col(&article_comments_t::created_at).desc())
            .collect<get_comments_response>(article_id);
    // 如果评论没有子评论，那就不显示该评论了。如果评论有子评论，那正常显示该评论，内容修改为：原评论也删除。
    std::erase_if(comments, [comments](get_comments_response &comment) -> bool {
      if (comment.comment_status !=
          static_cast<int32_t>(CommentStatus::DELETED)) {
        return false;
      }
      for (const auto &child_comment : comments) {
        if (child_comment.comment_id != comment.comment_id &&
            child_comment.parent_comment_id == comment.comment_id &&
            child_comment.comment_status ==
                static_cast<int32_t>(CommentStatus::PUBLISH)) {
          comment.content = "该评论已被删除";
          return false;
        }
      }
      return true;
    });

    // 对引用的评论进行用户信息加工
    std::string json =
        make_data(comments, std::string("Comments retrieved successfully"));
    resp.set_status_and_content(status_type::ok, std::move(json));
  }

  // 添加文章评论
  void add_article_comment(coro_http_request &req, coro_http_response &resp) {
    auto request = std::any_cast<add_comment_request>(req.get_user_data());

    auto conn = get_db_pool().get();
    if (conn == nullptr) {
      set_server_internel_error(resp);
      return;
    }

    uint64_t now = get_timestamp_milliseconds();

    // 检查用户是否存在
    auto user_vec = conn->select(col(&users_t::id))
                        .from<users_t>()
                        .where(col(&users_t::user_name).param())
                        .collect(request.author_name);
    if (user_vec.empty()) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("无效用户信息"));
      return;
    }

    uint64_t user_id = std::get<0>(user_vec.front());

    // 检查文章是否存在
    auto article_vec = conn->select(col(&articles_t::article_id))
                           .from<articles_t>()
                           .where(col(&articles_t::slug).param())
                           .collect(request.slug);
    if (article_vec.empty()) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("评论文章未找到"));
      return;
    }
    uint64_t article_id = std::get<0>(article_vec.front());

    // 获取客户端IP地址
    auto client_ip = get_client_ip(req);

    // 插入评论
    article_comments_t new_comment{.comment_id = 0,
                                   .article_id = article_id,
                                   .user_id = user_id,
                                   .content = request.content,
                                   .parent_comment_id =
                                       request.parent_comment_id,
                                   .ip = {},
                                   .comment_status = CommentStatus::PUBLISH,
                                   .created_at = now,
                                   .updated_at = now};
    // 复制IP地址到std::array
    std::copy_n(client_ip.data(),
                (std::min)(client_ip.size(), new_comment.ip.size()),
                new_comment.ip.data());
    // 检查parent_comment_id评论是否存在
    if (request.parent_comment_id > 0) {
      auto comments =
          conn->select(ormpp::all)
              .from<article_comments_t>()
              .where(col(&article_comments_t::comment_id).param())
              .collect<article_comments_t>(request.parent_comment_id);
      if (comments.empty()) {
        resp.set_status_and_content(status_type::bad_request,
                                    make_error("父级评论未找到"));
        return;
      }
      auto &parent_comment = comments.front();
      new_comment.parent_user_id = parent_comment.user_id;

      // 查询parent用户信息
      auto user_vec = conn->select(col(&users_t::id), col(&users_t::user_name))
                          .from<users_t>()
                          .where(col(&users_t::id).param())
                          .collect<users_t>(parent_comment.user_id);
      if (user_vec.empty()) {
        resp.set_status_and_content(status_type::bad_request,
                                    make_error("无效用户信息"));
        return;
      }
      auto &parent_user = user_vec.front();
      std::copy_n(parent_user.user_name.begin(), parent_user.user_name.size(),
                  new_comment.parent_user_name.begin());
    }
    // 插入评论
    auto comment_id = conn->get_insert_id_after_insert(new_comment);
    if (comment_id <= 0) {
      set_server_internel_error(resp);
      return;
    }
    new_comment.comment_id = comment_id;

    // 更新文章评论计数
    auto total_comment =
        conn->select(count())
            .from<article_comments_t>()
            .where(col(&article_comments_t::article_id).param())
            .collect(article_id);
    articles_t update_article;
    update_article.comments_count = total_comment;
    std::string condition = "article_id=" + std::to_string(article_id);
    conn->update_some<&articles_t::comments_count>(update_article, condition);
    // 返回新评论信息
    add_comment_response response{
        .comment_id = new_comment.comment_id,
        .article_id = new_comment.article_id,
        .user_id = new_comment.user_id,
        .author_name = request.author_name,
        .content = new_comment.content,
        .parent_comment_id = new_comment.parent_comment_id,
        .parent_user_name = new_comment.parent_user_name.data(),
        .ip = new_comment.ip.data(),
        .created_at = new_comment.created_at,
        .updated_at = new_comment.updated_at};

    std::string json = make_data(response, "新增评论成功");
    if (json.empty()) {
      set_server_internel_error(resp);
      return;
    }

    resp.set_status_and_content(status_type::ok, std::move(json));
  }

  // 获取用户的评论列表
  void get_my_comments(coro_http_request &req, coro_http_response &resp) {
    auto body = req.get_body();
    if (body.empty()) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("无效的请求参数，请求体不能为空"));
      return;
    }

    // 解析请求参数
    article_user_comments_request request{};
    std::error_code ec;
    iguana::from_json(request, body, ec);
    if (ec) {
      resp.set_status_and_content(
          status_type::bad_request,
          make_error("无效的请求参数，JSON格式错误: " + ec.message()));
      return;
    }

    // 验证用户ID
    if (request.user_id == 0) {
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

    // 只有自己可以查看自己的评论列表
    if (current_user_id != request.user_id) {
      resp.set_status_and_content(status_type::forbidden,
                                  make_error("没有权限查看其他用户的评论"));
      return;
    }

    auto conn = get_db_pool().get();
    if (conn == nullptr) {
      set_server_internel_error(resp);
      return;
    }

    // 设置默认分页参数
    int current_page = request.current_page > 0 ? request.current_page : 1;
    int per_page = request.per_page > 0 ? request.per_page : 10;
    int offset = (current_page - 1) * per_page;
    int limit = per_page;

    // 计算总评论数
    auto total_count =
        conn->select(ormpp::count())
            .from<article_comments_t>()
            .where(col(&article_comments_t::user_id).param() &&
                   col(&article_comments_t::comment_status).param())
            .collect(request.user_id, CommentStatus::PUBLISH);

    // 获取用户的评论列表，同时关联文章标题
    auto comments_list =
        conn->select(col(&article_comments_t::comment_id),
                     col(&article_comments_t::article_id),
                     col(&articles_t::title), col(&article_comments_t::content),
                     col(&article_comments_t::parent_comment_id),
                     col(&article_comments_t::parent_user_name),
                     col(&article_comments_t::created_at),
                     col(&article_comments_t::updated_at))
            .from<article_comments_t>()
            .inner_join(col(&article_comments_t::article_id),
                        col(&articles_t::article_id))
            .where(col(&article_comments_t::user_id).param() &&
                   col(&article_comments_t::comment_status).param())
            .order_by(col(&article_comments_t::created_at).desc())
            .limit(ormpp::token)
            .offset(ormpp::token)
            .collect<user_comment_item>(request.user_id, CommentStatus::PUBLISH,
                                        limit, offset);

    std::string json = make_data(std::move(comments_list),
                                 "获取用户评论列表成功", total_count);
    resp.set_status_and_content(status_type::ok, std::move(json));
  }

  // 删除评论
  void delete_my_comment(coro_http_request &req, coro_http_response &resp) {
    auto body = req.get_body();
    if (body.empty()) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("无效的请求参数，请求体不能为空"));
      return;
    }

    // 解析请求参数
    article_delete_comment_request request{};
    std::error_code ec;
    iguana::from_json(request, body, ec);
    if (ec) {
      resp.set_status_and_content(
          status_type::bad_request,
          make_error("无效的请求参数，JSON格式错误: " + ec.message()));
      return;
    }

    // 验证评论ID
    if (request.comment_id == 0) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("无效的请求参数，评论ID不能为空"));
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

    // 检查评论是否存在，并且是否是当前用户的评论
    auto comments = conn->select(col(&article_comments_t::user_id),
                                 col(&article_comments_t::article_id))
                        .from<article_comments_t>()
                        .where(col(&article_comments_t::comment_id).param() &&
                               col(&article_comments_t::comment_status).param())
                        .collect(request.comment_id, CommentStatus::PUBLISH);

    if (comments.empty()) {
      resp.set_status_and_content(status_type::not_found,
                                  make_error("评论不存在或已被删除"));
      return;
    }

    uint64_t comment_user_id = std::get<0>(comments.front());
    uint64_t article_id = std::get<1>(comments.front());

    // 检查审核人是否是管理员(只有管理员、超级管理员和评论作者才能删除评论)
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
    // 检查审核人是否是管理员(只有管理员、超级管理员和评论作者才能删除评论)
    if (review_user.role != "admin" && review_user.role != "superadmin" &&
        current_user_id != comment_user_id) {
      resp.set_status_and_content(status_type::forbidden,
                                  make_error("没有权限删除其他用户的评论"));
      return;
    }

    // 删除评论（标记为已删除）
    article_comments_t comment;
    comment.comment_status = CommentStatus::DELETED;
    comment.updated_at = get_timestamp_milliseconds();

    int n = conn->update_some<&article_comments_t::comment_status,
                              &article_comments_t::updated_at>(
        comment, "comment_id=" + std::to_string(request.comment_id));

    if (n == 0) {
      set_server_internel_error(resp);
      return;
    }

    // 更新文章评论计数
    auto total_comment =
        conn->select(ormpp::count())
            .from<article_comments_t>()
            .where(col(&article_comments_t::article_id).param() &&
                   col(&article_comments_t::comment_status).param())
            .collect(article_id, CommentStatus::PUBLISH);
    articles_t update_article;
    update_article.comments_count = total_comment;
    std::string condition = "article_id=" + std::to_string(article_id);
    conn->update_some<&articles_t::comments_count>(update_article, condition);

    std::string json = make_success("评论删除成功");
    resp.set_status_and_content(status_type::ok, std::move(json));
  }
};
} // namespace purecpp
