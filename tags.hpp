#pragma once

#include "common.hpp"
#include <vector>

using namespace cinatra;

namespace purecpp {

struct tag_response_item {
  int tag_id;
  std::string name;
  int tag_group;
};

class tags {
public:
  void get_tags(coro_http_request &req, coro_http_response &resp) {
    auto conn = get_db_pool().get();
    if (conn == nullptr) {
      set_server_internel_error(resp);
      return;
    }
    std::vector<tags_t> vec = conn->select(ormpp::all).from<tags_t>().collect();
    std::vector<tag_response_item> data;
    data.reserve(vec.size());
    for (const auto &tag : vec) {
      data.push_back(
          {tag.tag_id, array_to_string(tag.name), tag.tag_group});
    }

    std::string json = make_data(data, "获取标签成功");
    resp.set_status_and_content(status_type::ok, std::move(json));
  }
};
} // namespace purecpp
