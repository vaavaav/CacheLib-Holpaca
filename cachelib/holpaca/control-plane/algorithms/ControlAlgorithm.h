#pragma once

#include <cachelib/holpaca/control-plane/ProxyManager.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <unordered_map>

namespace facebook {
namespace cachelib {
namespace holpaca {

class ControlAlgorithm {
  std::chrono::milliseconds const m_kPeriodicity;
  std::atomic_bool m_stop{false};
  std::thread m_thread;

 protected:
  std::shared_ptr<CacheProxy> const m_kCacheProxy;
  virtual void loop(CacheStatus&& cacheStatus) = 0;

 public:
  ControlAlgorithm(std::shared_ptr<CacheProxy> const kCacheProxy,
                   std::chrono::milliseconds const kPeriodicity)
      : m_kCacheProxy(kCacheProxy), m_kPeriodicity(kPeriodicity) {
    m_thread = std::thread([this, kCacheProxy]() {
      while (!m_stop) {
        loop(kCacheProxy->getStatus());
        std::this_thread::sleep_for(m_kPeriodicity);
      }
    });
  }

  ~ControlAlgorithm() {
    m_stop = true;
    if (m_thread.joinable()) {
      m_thread.join();
    }
  }
};
} // namespace holpaca
} // namespace cachelib
} // namespace facebook
