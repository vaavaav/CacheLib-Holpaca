#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "../ProxyManager.h"
#include "ControlAlgorithm.h"
#include "Optimizable.h"
#include "Spline.h"

namespace facebook {
namespace cachelib {
namespace holpaca {

class HitRatioMaximization : public ControlAlgorithm, public Optimizable {
  struct PoolConfig {
    uint32_t const m_kId;
    uint64_t m_optimalSize;
    uint64_t const m_kCurrentSize;
    tk::spline m_MRC;
    uint64_t const m_kLowerBound{0};
    uint64_t const m_kUpperBound{0};
    double getMissRatio() const { return m_MRC(m_optimalSize); }
  };

  struct CacheConfig {
    std::string const m_kName;
    uint64_t const m_kCurrentSize;
    uint64_t const m_kMaxSize;
    uint64_t m_optimalSize;
    std::vector<PoolConfig> m_poolConfigs;
  };
  std::vector<CacheConfig> m_cacheConfigs;

  double const m_kDelta;
  const uint32_t m_kMRCMinLength{3};
  std::chrono::milliseconds const m_kPeriodicity{1000};
  std::unordered_map<std::string, double> const m_kHitRatioQoS{};
  ProxyManager* const m_kProxyManager;
  std::atomic_bool m_stop{false};
  std::thread m_thread;

  void collect();
  bool skip();
  void step() override final;
  double energy() const override final;
  double distance(Optimizable const* other) const override final;
  void enforce();

 public:
  HitRatioMaximization(
      ProxyManager* const kProxyManager,
      std::chrono::milliseconds const kPeriodicity,
      double const kDelta,
      const std::unordered_map<std::string, double>& kHitRatioQoS);
  ~HitRatioMaximization();
};

} // namespace holpaca
} // namespace cachelib
} // namespace facebook
