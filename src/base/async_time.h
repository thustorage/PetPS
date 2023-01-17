#pragma once
#include <iostream>
#include <memory>
#include <thread>
#include <utility>

#include "base.h"

namespace base {

class AsyncTimeHelper {
 public:
  explicit AsyncTimeHelper(int update_interval)
      : update_interval_(update_interval) {
    stop_ = false;
    now_time_ = base::GetTimestamp();
    thread_.reset(new std::thread(&AsyncTimeHelper::Run, this));
  }
  ~AsyncTimeHelper() {
    stop_ = true;
    if (thread_) {
      thread_->join();
    }
  }
  inline int64 GetTime() { return now_time_; }
  static int64 GetTimestamp() {
    static AsyncTimeHelper helper(10);
    return helper.GetTime();
  }

 private:
  void Run() {
    while (!stop_) {
      now_time_ = base::GetTimestamp();
      std::this_thread::sleep_for(std::chrono::milliseconds(update_interval_));
    }
  }

  bool stop_;
  int update_interval_;
  int64 now_time_;
  std::unique_ptr<std::thread> thread_;
};

}  // namespace base
