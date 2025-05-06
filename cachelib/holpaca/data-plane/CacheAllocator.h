#pragma once
#include <cachelib/allocator/CacheAllocator.h>
#include <cachelib/holpaca/data-plane/CacheAllocatorConfig.h>
#include <cachelib/holpaca/data-plane/Metrics.h>
#include <cachelib/holpaca/protos/Holpaca.grpc.pb.h>
#include <cachelib/holpaca/protos/Holpaca.pb.h>
#include <grpc/grpc.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

#include <shared_mutex>

namespace facebook {
namespace cachelib {
namespace holpaca {
template <typename CacheTrait>
class CacheAllocator : public ::facebook::cachelib::CacheAllocator<CacheTrait>,
                       ::holpaca::Stage::Service {
  using Super = ::facebook::cachelib::CacheAllocator<CacheTrait>;
  std::shared_ptr<::holpaca::Stage::Service> m_stage;
  std::unordered_map<PoolId, Metrics> m_metrics;
  std::shared_timed_mutex m_metricsMutex;
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
                      bool isLookup,
                      bool isMiss,
                      bool reset = false);
  void registerMetrics(PoolId id, const uint32_t diskIOPS);
};

using LruAllocator = CacheAllocator<LruCacheTrait>;
using LruWithSpinAllocator = CacheAllocator<LruCacheWithSpinBucketsTrait>;
using Lru2QAllocator = CacheAllocator<Lru2QCacheTrait>;
using TinyLFUAllocator = CacheAllocator<TinyLFUCacheTrait>;

} // namespace holpaca
} // namespace cachelib
} // namespace facebook
