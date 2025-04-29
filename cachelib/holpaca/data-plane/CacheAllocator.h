#pragma once
#include <Shards/Shards.h>
#include <grpc/grpc.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

#include "../protos/Holpaca.grpc.pb.h"
#include "../protos/Holpaca.pb.h"
#include "cachelib/allocator/CacheAllocator.h"
#include "cachelib/holpaca/data-plane/CacheAllocatorConfig.h"

namespace facebook {
namespace cachelib {
namespace holpaca {
template <typename CacheTrait>
class CacheAllocator : public ::facebook::cachelib::CacheAllocator<CacheTrait>,
                       ::holpaca::Stage::Service {
  using Super = ::facebook::cachelib::CacheAllocator<CacheTrait>;
  std::shared_ptr<::holpaca::Stage::Service> m_stage;
  std::unordered_map<int32_t, std::shared_ptr<Shards>> m_shards;
  std::thread m_serverThread;
  std::shared_ptr<grpc::Server> m_server;
  std::thread m_keepAliveThread;
  std::atomic_bool m_stop{false};

  grpc::Status GetStatus(grpc::ServerContext* context,
                         const ::holpaca::GetStatusRequest* request,
                         ::holpaca::GetStatusResponse* response) override final;
  grpc::Status Resize(grpc::ServerContext* context,
                      const ::holpaca::ResizeRequest* request,
                      ::holpaca::ResizeResponse* response) override final;

  PoolId const kGhostPoolId;
  static constexpr double s_kGhostPoolRelativeSize = 1 / 10;

 public:
  using Config = CacheAllocatorConfig<CacheAllocator<CacheTrait>>;
  using Trait = CacheTrait;

  static constexpr std::chrono::milliseconds s_KeepAlivePeriodicity =
      std::chrono::milliseconds(1000);

  CacheAllocator(Config& config);
  ~CacheAllocator();

  PoolId addPool(std::string name, size_t size);

  void registerAccess(PoolId id,
                      const std::string& key,
                      uint32_t& size,
                      bool reset = false);
};

using LruAllocator = CacheAllocator<LruCacheTrait>;
using LruWithSpinAllocator = CacheAllocator<LruCacheWithSpinBucketsTrait>;
using Lru2QAllocator = CacheAllocator<Lru2QCacheTrait>;
using TinyLFUAllocator = CacheAllocator<TinyLFUCacheTrait>;

} // namespace holpaca
} // namespace cachelib
} // namespace facebook
