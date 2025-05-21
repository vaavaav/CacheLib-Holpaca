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

  ProxyManager* const m_kProxyManager;

 protected:
  virtual void loop(ProxyManager* const kProxyManager) = 0;

 public:
  ControlAlgorithm(ProxyManager* const kProxyManager,
                   std::chrono::milliseconds const kPeriodicity)
      : m_kProxyManager(kProxyManager), m_kPeriodicity(kPeriodicity) {
    m_thread = std::thread([this]() {
      while (!m_stop) {
        loop(m_kProxyManager);
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
