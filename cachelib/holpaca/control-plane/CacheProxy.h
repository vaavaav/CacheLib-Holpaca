#pragma once

#include <cachelib/allocator/memory/Slab.h>
#include <cachelib/holpaca/protos/Holpaca.grpc.pb.h>
#include <cachelib/holpaca/protos/Holpaca.pb.h>
#include <grpc/grpc.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

namespace facebook {
namespace cachelib {
namespace holpaca {

struct PoolStatus {
  uint64_t m_maxSize{0};
  uint64_t m_usedSize{0};
  uint32_t m_diskIOPS{0};
  uint32_t m_lookups{0};
  uint32_t m_misses{0};
  uint32_t m_evictions{0};
  std::map<ClassId, uint32_t> m_tailAccesses{};
  std::map<uint64_t, float> m_MRC{};
};

struct CacheStatus {
  uint64_t m_maxSize{0};
  std::unordered_map<PoolId, PoolStatus> m_pools{};
};

class CacheProxy {
 public:
  enum CommunicationType {
    kAll = 0,
    kSingle = 1,
  };

 private:
  std::unique_ptr<::holpaca::Stage::Stub> const m_kStub;
  CommunicationType const m_kType;

 public:
  CacheProxy(const std::string& address, CommunicationType type);

  void resize(const std::unordered_map<PoolId, int64_t>& deltaSizes);
  CacheStatus getStatus();
};
} // namespace holpaca
} // namespace cachelib
} // namespace facebook
