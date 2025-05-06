#pragma once

#include <cachelib/holpaca/protos/Holpaca.grpc.pb.h>
#include <cachelib/holpaca/protos/Holpaca.pb.h>
#include <grpc/grpc.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

#include <chrono>

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
  std::map<uint64_t, uint32_t> m_tailAccesses{};
  std::map<uint64_t, float> m_MRC{};
};

struct CacheStatus {
  uint64_t m_maxSize{0};
  uint64_t m_usedSize{0};
  std::unordered_map<uint32_t, PoolStatus> m_pools{};
};

class CacheProxy {
  std::unique_ptr<::holpaca::Stage::Stub> m_stub;
  std::chrono::nanoseconds m_lastKeepAlive;
  static constexpr std::chrono::nanoseconds s_kKeepAliveTimeout =
      std::chrono::seconds(3);
  std::chrono::nanoseconds m_lastUpdate;
  static constexpr std::chrono::nanoseconds s_kUpdateValidity =
      std::chrono::seconds(1);

  CacheStatus m_status;

 public:
  CacheProxy(const std::string& address, std::chrono::nanoseconds timestamp);
  void keepAlive(std::chrono::nanoseconds timestamp);
  bool isAlive(std::chrono::nanoseconds now) const;

  void resize(std::unordered_map<int32_t, uint64_t> newSizes);
  CacheStatus& getStatus();
};
} // namespace holpaca
} // namespace cachelib
} // namespace facebook
