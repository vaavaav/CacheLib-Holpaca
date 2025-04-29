#pragma once
#include <cachelib/holpaca/control-plane/algorithms/ControlAlgorithm.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace facebook {
namespace cachelib {
namespace holpaca {
class HitRatioMaximization : public ControlAlgorithm {
  std::thread m_thread;
  std::atomic_bool m_stop;
  void run() override final;

  ProxyManager* m_proxyManager;
  std::unordered_map<std::string, double> m_hitRatioQoS;
  std::chrono::milliseconds m_periodicity;
  std::set<std::string> m_active{};

  const uint32_t m_kMRCMinLength{3};
  double m_delta;

 public:
  struct Metadata;

 private:
  Metadata collect(
      std::unordered_map<std::string, std::shared_ptr<CacheProxy>> caches);
  std::unordered_map<int32_t, uint64_t> compute(Metadata& metadata);

 public:
  HitRatioMaximization(
      ProxyManager* proxyManager,
      std::chrono::milliseconds periodicity,
      double delta,
      const std::unordered_map<std::string, double>& hitRatioQoS);

  ~HitRatioMaximization();
};
} // namespace holpaca
} // namespace cachelib
} // namespace facebook
