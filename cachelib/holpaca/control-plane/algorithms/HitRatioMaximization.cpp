#include "HitRatioMaximization.h"

namespace facebook {
namespace cachelib {
namespace holpaca {

HitRatioMaximization::HitRatioMaximization(
    ProxyManager* proxyManager,
    std::chrono::milliseconds periodicity,
    double delta,
    const std::unordered_map<std::string, double>& hitRatioQoS)
    : m_proxyManager(proxyManager),
      m_periodicity(periodicity),
      m_delta(delta),
      m_hitRatioQoS(hitRatioQoS) {
  m_thread = std::thread([this] {
    while (!m_stop) {
      run();
      std::this_thread::sleep_for(m_periodicity);
    }
  });
}

HitRatioMaximization::~HitRatioMaximization() {
  m_stop = true;
  if (m_thread.joinable()) {
    m_thread.join();
  }
}

void HitRatioMaximization::run() {
  // collect metadata from all caches

  std::unordered_map<std::string, std::unordered_map<int32_t, PoolStatus>>
      metadata;

  for (const auto& [address, proxy] : m_proxyManager->getCaches()) {
    metadata[address] = proxy->getStatus();
  }
}

} // namespace holpaca
} // namespace cachelib
} // namespace facebook
