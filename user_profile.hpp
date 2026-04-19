#pragma once

#include "entity.hpp"
#include "user_aspects.hpp"

#include <cinatra.hpp>

using namespace cinatra;

namespace purecpp {

struct profile_lookup_request {
  uint64_t user_id = 0;
  std::string username;
};

struct avatar_upload_response {
  std::string url;
  std::string filename;
};

// 用户个人信息服务类
class user_profile_t {
public:
  /**
   * @brief 获取用户的个人信息，支持通过user_id或username查询
   */
  void get_user_profile(coro_http_request &req, coro_http_response &resp) {
    // 从请求中获取用户ID或用户名
    auto body = req.get_body();

    // 定义可以接收user_id或username的请求结构体
    profile_lookup_request request{};
    std::error_code ec;
    iguana::from_json(request, body, ec);
    if (ec) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error(ec.message()));
      return;
    }

    // 用户id和username不能同时为空
    if (request.user_id == 0 && request.username.empty()) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("用户ID或用户名不能为空"));
      return;
    }

    // 查询数据库
    auto conn = get_db_pool().get();
    if (conn == nullptr) {
      set_server_internel_error(resp);
      return;
    }

    // 查询用户信息
    std::vector<users_t> users;
    if (request.user_id != 0) {
      // 通过user_id查询
      users = conn->select(ormpp::all)
                  .from<users_t>()
                  .where(col(&users_t::id).param())
                  .collect<users_t>(request.user_id);
    } else {
      // 通过username查询
      users = conn->select(ormpp::all)
                  .from<users_t>()
                  .where(col(&users_t::user_name).param())
                  .collect<users_t>(request.username);
    }

    if (users.empty()) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("用户不存在"));
      return;
    }

    auto &user = users[0];

    // 构建响应
    get_profile_response profile;
    // 使用安全的字符串转换，避免未终止字符串问题
    profile.username = array_to_string(user.user_name);
    profile.email = array_to_string(user.email);
    profile.location = user.location;
    profile.bio = user.bio;
    profile.avatar = user.avatar;
    profile.skills = user.skills;
    profile.created_at = user.created_at;
    profile.last_active_at = user.last_active_at;
    profile.title = user.title;
    profile.role = user.role;
    profile.experience = user.experience;
    profile.level = user.level;
    profile.status = user.status;

    std::string json = make_data(profile, "获取用户信息成功");
    resp.set_status_and_content(status_type::ok, std::move(json));
  }

  /**
   * @brief 更新当前用户的个人信息
   */
  void update_user_profile(coro_http_request &req, coro_http_response &resp) {
    // 从请求体中获取更新信息
    auto body = req.get_body();
    user_profile_request update_info;
    std::error_code ec;
    iguana::from_json(update_info, body, ec);
    if (ec) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error(ec.message()));
      return;
    }

    // 用户id不能不能为空
    if (update_info.user_id == 0) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("用户ID不能为空"));
      return;
    }

    // 查询数据库
    auto conn = get_db_pool().get();
    if (conn == nullptr) {
      set_server_internel_error(resp);
      return;
    }

    // 获取现有用户信息
    auto users = conn->select(ormpp::all)
                     .from<users_t>()
                     .where(col(&users_t::id).param())
                     .collect(update_info.user_id);

    if (users.empty()) {
      resp.set_status_and_content(status_type::bad_request,
                                  make_error("用户不存在"));
      return;
    }

    users_t user = users[0];

    // 更新数据库中的指定字段
    bool update_success = true;
    users_t update_user;
    std::string condition = "id=" + std::to_string(user.id);

    if (update_info.location.has_value()) {
      update_user.location = update_info.location;
      if (conn->update_some<&users_t::location>(update_user, condition) != 1) {
        update_success = false;
      }
    }

    if (update_info.bio.has_value() && update_success) {
      update_user.bio = update_info.bio;
      if (conn->update_some<&users_t::bio>(update_user, condition) != 1) {
        update_success = false;
      }
    }

    if (update_info.avatar.has_value() && update_success) {
      update_user.avatar = update_info.avatar;
      if (conn->update_some<&users_t::avatar>(update_user, condition) != 1) {
        update_success = false;
      }
    }

    if (update_info.skills.has_value() && update_success) {
      update_user.skills = update_info.skills;
      if (conn->update_some<&users_t::skills>(update_user, condition) != 1) {
        update_success = false;
      }
    }

    if (!update_success) {
      resp.set_status_and_content(status_type::internal_server_error,
                                  make_error("更新用户信息失败"));
      return;
    }

    resp.set_status_and_content(status_type::ok,
                                make_success("更新用户信息成功"));
  }

  /**
   * @brief 处理用户头像上传
   */
  void upload_avatar(coro_http_request &req, coro_http_response &resp) {
    try {
      // 获取请求体
      auto body = req.get_body();

      avatar_upload_request upload_req;
      std::error_code ec;
      iguana::from_json(upload_req, body, ec);
      if (ec) {
        resp.set_status_and_content(status_type::bad_request,
                                    make_error(ec.message()));
        return;
      }

      // 验证请求参数
      if (upload_req.user_id == 0) {
        resp.set_status_and_content(status_type::bad_request,
                                    make_error("用户ID不能为空"));
        return;
      }

      if (upload_req.avatar_data.empty() || upload_req.filename.empty()) {
        resp.set_status_and_content(status_type::bad_request,
                                    make_error("没有找到上传的头像文件"));
        return;
      }

      // 检查文件类型
      std::string ext(cinatra::get_extension(upload_req.filename).substr(1));
      std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
      if (ext != "jpg" && ext != "jpeg" && ext != "png" && ext != "gif") {
        resp.set_status_and_content(
            status_type::bad_request,
            make_error("只支持JPG、PNG、GIF格式的图片"));
        return;
      }

      // 解码base64图片数据
      auto opt_avatar_data = cinatra::base64_decode(upload_req.avatar_data);
      if (!opt_avatar_data.has_value()) {
        resp.set_status_and_content(status_type::bad_request,
                                    make_error("base64图片数据解码失败"));
        return;
      }

      std::string &avatar_data = opt_avatar_data.value();
      // 检查文件大小（2MB限制）
      const size_t MAX_SIZE = 512 * 1024;
      if (avatar_data.length() > MAX_SIZE) {
        resp.set_status_and_content(status_type::bad_request,
                                    make_error("图片大小不能超过512KB"));
        return;
      }

      // 确保uploads目录存在
      std::filesystem::path upload_dir = "html/uploads/avatars";
      if (!std::filesystem::exists(upload_dir)) {
        std::filesystem::create_directories(upload_dir);
      }

      // 生成唯一文件名
      std::string unique_filename =
          "avatar_" + std::to_string(upload_req.user_id) + "_" +
          std::to_string(get_timestamp_milliseconds()) + "." + ext;
      std::filesystem::path file_path = upload_dir / unique_filename;

      // 保存文件
      std::ofstream out_file(file_path, std::ios::binary);
      if (!out_file) {
        resp.set_status_and_content(status_type::internal_server_error,
                                    make_error("保存文件失败"));
        return;
      }
      out_file.write(reinterpret_cast<const char *>(avatar_data.data()),
                     avatar_data.size());
      out_file.close();

      // 生成文件URL
      std::string file_url = "/uploads/avatars/" + unique_filename;

      // 更新用户的avatar字段
      auto conn = get_db_pool().get();
      if (conn == nullptr) {
        set_server_internel_error(resp);
        return;
      }

      // 获取现有用户信息
      auto users = conn->select(ormpp::all)
                       .from<users_t>()
                       .where(col(&users_t::id).param())
                       .collect(upload_req.user_id);

      if (users.empty()) {
        resp.set_status_and_content(status_type::bad_request,
                                    make_error("用户不存在"));
        return;
      }

      users_t update_user;
      update_user.avatar = file_url;

      // 更新数据库
      if (conn->update_some<&users_t::avatar>(
              update_user, "id=" + std::to_string(upload_req.user_id)) != 1) {
        resp.set_status_and_content(status_type::internal_server_error,
                                    make_error("更新用户头像失败"));
        return;
      }

      // 构建响应
      avatar_upload_response data;
      data.url = file_url;
      data.filename = unique_filename;

      std::string json = make_data(data, "头像上传成功");
      resp.set_status_and_content(status_type::ok, std::move(json));
      return;
    } catch (const std::exception &e) {
      CINATRA_LOG_ERROR << "头像上传失败: " << e.what();
      resp.set_status_and_content(
          status_type::internal_server_error,
          make_error(std::string("头像上传失败: ") + e.what()));
      return;
    }
  }

private:
  /**
   * @brief 检查字符是否为Base64字符
   */
  bool is_base64(unsigned char c) {
    return (isalnum(c) || (c == '+') || (c == '/'));
  }
};

} // namespace purecpp
