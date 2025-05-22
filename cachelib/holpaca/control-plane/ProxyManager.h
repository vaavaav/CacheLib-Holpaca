#pragma once

#include <cachelib/holpaca/control-plane/CacheProxy.h>

#include <string>
#include <vector>

namespace facebook {
namespace cachelib {
namespace holpaca {

class ProxyManager {
 public:
  struct PoolStatus {
    bool m_isActive{false};
    uint64_t m_maxSize{0};
    uint64_t m_usedSize{0};
    uint32_t m_diskIOPS{0};
    uint32_t m_evictions{0};
    std::unordered_map<std::string, uint64_t> m_externalSize{};
    std::unordered_map<ClassId, uint32_t> m_tailAccesses{};
    std::map<uint64_t, float> m_MRC{};
  };

  struct CacheStatus {
    uint64_t m_maxSize{0};
    std::unordered_map<PoolId, PoolStatus> m_pools{};
  };

  struct PoolResize {
    PoolId m_kId;
    int64_t m_kDeltaSize;
    std::unordered_map<std::string, int64_t> m_kExternalDeltaSize;
  };

  struct CacheResize {
    std::string m_kName;
    std::vector<PoolResize> m_kPoolResizes;
  };

  virtual std::unordered_map<std::string, CacheStatus> getStatus() = 0;
  virtual void resize(const std::vector<CacheResize>& cacheResize) = 0;
};

} // namespace holpaca
} // namespace cachelib
} // namespace facebook
