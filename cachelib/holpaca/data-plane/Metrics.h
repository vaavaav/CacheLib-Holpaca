#pragma once
#include <Shards/Shards.h>

#include <cstdint>

namespace facebook {
namespace cachelib {
namespace holpaca {

struct Metrics {
  std::shared_ptr<Shards> m_shards;
  uint32_t m_diskIOPS{0};
  uint32_t m_misses{0};
  uint32_t m_lookups{0};
};

} // namespace holpaca
} // namespace cachelib
} // namespace facebook
