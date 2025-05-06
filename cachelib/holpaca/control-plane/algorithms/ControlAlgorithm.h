#pragma once

#include <cachelib/holpaca/control-plane/ProxyManager.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace facebook {
namespace cachelib {
namespace holpaca {

class ControlAlgorithm {
  std::chrono::milliseconds const m_kPeriodicity;
  std::atomic_bool m_stop{false};
  std::thread m_thread;

 protected:
  ProxyManager* const m_kProxyManager;
  virtual void loop(
      std::unordered_map<std::string, CacheStatus>& cacheStatus) = 0;

 public:
  ControlAlgorithm(ProxyManager* const kProxyManager,
                   std::chrono::milliseconds const kPeriodicity)
      : m_kProxyManager(kProxyManager), m_kPeriodicity(kPeriodicity) {
    m_thread = std::thread([this] {
      while (!m_stop) {
        std::unordered_map<std::string, CacheStatus> cacheStatus;
        for (const auto& [address, proxy] : m_kProxyManager->getCaches()) {
          cacheStatus[address] = proxy->getStatus();
        }
        loop(cacheStatus);
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
