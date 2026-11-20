// parallel_for for the decide phase: a persistent pool (threads are started once, not per tick).
// Each task writes only its own output slot, so results never depend on thread count or scheduling;
// tasks are handed out dynamically. Small batches run inline: below kMinParallel tasks a handoff
// costs more than it saves. Built without threads for WebAssembly (APSIS_NO_THREADS).
#pragma once
#include <cstddef>
#include <functional>
#include <vector>
#ifndef APSIS_NO_THREADS
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#endif

namespace apsis {

constexpr size_t kMinParallel = 4;

#ifndef APSIS_NO_THREADS
class Pool {
 public:
  explicit Pool(int threads) {
    for (int k = 1; k < threads; ++k) workers_.emplace_back([this](std::stop_token st) { loop(st); });
  }
  ~Pool() {
    { std::lock_guard lk(m_); stop_ = true; }
    cv_.notify_all();
  }
  int size() const { return static_cast<int>(workers_.size()) + 1; }
  void run(size_t n, const std::function<void(size_t)>& fn) {
    {
      std::lock_guard lk(m_);
      fn_ = &fn; n_ = n; next_.store(0); busy_ = static_cast<int>(workers_.size()); ++gen_;
    }
    cv_.notify_all();
    work();
    std::unique_lock lk(m_);
    done_.wait(lk, [&] { return busy_ == 0; });
    fn_ = nullptr;
  }

 private:
  void work() { for (size_t i; (i = next_.fetch_add(1)) < n_;) (*fn_)(i); }
  void loop(std::stop_token) {
    uint64_t seen = 0;
    for (;;) {
      {
        std::unique_lock lk(m_);
        cv_.wait(lk, [&] { return stop_ || gen_ != seen; });
        if (stop_) return;
        seen = gen_;
      }
      work();
      { std::lock_guard lk(m_); if (--busy_ == 0) done_.notify_one(); }
    }
  }
  std::vector<std::jthread> workers_;
  std::mutex m_;
  std::condition_variable cv_, done_;
  const std::function<void(size_t)>* fn_ = nullptr;
  size_t n_ = 0;
  std::atomic<size_t> next_{0};
  int busy_ = 0;
  uint64_t gen_ = 0;
  bool stop_ = false;
};
#else
class Pool {
 public:
  explicit Pool(int) {}
  int size() const { return 1; }
  void run(size_t n, const std::function<void(size_t)>& fn) { for (size_t i = 0; i < n; ++i) fn(i); }
};
#endif

inline void parallel_for(Pool* pool, size_t n, const std::function<void(size_t)>& fn) {
  if (pool && pool->size() > 1 && n >= kMinParallel) { pool->run(n, fn); return; }
  for (size_t i = 0; i < n; ++i) fn(i);
}

}  // namespace apsis
