#pragma once

#include <grpc/grpc.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

#include <chrono>

#include "cachelib/holpaca/protos/Holpaca.grpc.pb.h"
#include "cachelib/holpaca/protos/Holpaca.pb.h"

namespace facebook {
namespace cachelib {
namespace holpaca {

struct PoolStatus {
  uint64_t maxSize;
  uint64_t usedSize;
  std::map<uint64_t, uint32_t> tailAccesses;
  std::map<uint64_t, float> mrc;
};

class CacheProxy {
  std::unique_ptr<::holpaca::Stage::Stub> m_stub;
  std::chrono::nanoseconds m_lastKeepAlive;
  static constexpr std::chrono::nanoseconds s_kKeepAliveTimeout =
      std::chrono::seconds(3);

 public:
  CacheProxy(const std::string& address, std::chrono::nanoseconds timestamp);
  void keepAlive(std::chrono::nanoseconds timestamp);
  bool isAlive(std::chrono::nanoseconds now) const;

  void resize(std::unordered_map<int32_t, uint64_t> newSizes);
  std::unordered_map<int32_t, PoolStatus> getStatus();
};
} // namespace holpaca
} // namespace cachelib
} // namespace facebook
