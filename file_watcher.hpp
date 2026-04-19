#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// 定时监视一组文件的修改时间，文件变更时自动调用对应回调。
// 在独立后台线程中运行，停止时立即唤醒不阻塞。
//
// 用法：
//   file_watcher w;
//   w.add("cfg/user_config.json", [] { /* reload */ });
//   w.add("sensitive_words.txt",  [] { /* reload */ });
//   w.start(std::chrono::seconds(5));
//   // ... server runs ...
//   // 析构时自动 stop()
class file_watcher {
 public:
  ~file_watcher() { stop(); }

  // 添加监视项。callback 在检测到文件修改时被调用（后台线程上下文）。
  // 必须在 start() 之前调用。
  void add(const std::string &path, std::function<void()> callback) {
    entry e;
    e.path = path;
    e.callback = std::move(callback);
    e.last_mtime = mtime_of(path);
    entries_.push_back(std::move(e));
  }

  // 启动后台监视线程，每隔 interval 检查一次。
  void start(std::chrono::seconds interval = std::chrono::seconds(5)) {
    running_ = true;
    thread_ = std::thread([this, interval] {
      std::unique_lock lk(cv_mtx_);
      while (running_) {
        cv_.wait_for(lk, interval, [this] { return !running_.load(); });
        if (!running_) break;
        check_all();
      }
    });
  }

  // 停止监视，立即唤醒后台线程并等待其退出。
  void stop() {
    running_ = false;
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
  }

 private:
  struct entry {
    std::string path;
    std::filesystem::file_time_type last_mtime;
    std::function<void()> callback;
  };

  static std::filesystem::file_time_type mtime_of(const std::string &path) {
    std::error_code ec;
    auto t = std::filesystem::last_write_time(path, ec);
    return ec ? std::filesystem::file_time_type{} : t;
  }

  void check_all() {
    for (auto &e : entries_) {
      auto mtime = mtime_of(e.path);
      if (mtime != e.last_mtime) {
        e.last_mtime = mtime;
        e.callback();
      }
    }
  }

  std::vector<entry> entries_;
  std::thread thread_;
  std::atomic<bool> running_{false};
  std::mutex cv_mtx_;
  std::condition_variable cv_;
};
