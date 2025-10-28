#pragma once
#include <Shards/Shards.h>
#include <cachelib/allocator/CacheAllocator.h>
#include <cachelib/holpaca/data-plane/CacheAllocatorConfig.h>
#include <cachelib/holpaca/protos/Holpaca.grpc.pb.h>
#include <cachelib/holpaca/protos/Holpaca.pb.h>
#include <grpc/grpc.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace facebook {
namespace cachelib {
namespace holpaca {
template <typename CacheTrait>
class CacheAllocator : public ::facebook::cachelib::CacheAllocator<CacheTrait>,
                       ::holpaca::Stage::Service {
  // GRPC stuff

  std::shared_ptr<::holpaca::Stage::Service> m_stage;
  std::shared_ptr<::holpaca::Controller::Stub> m_controller;
  std::thread m_serverThread;
  std::shared_ptr<grpc::Server> m_server{nullptr};
  std::string const m_kAddress;

  grpc::Status GetStatus(grpc::ServerContext* context,
                         const ::holpaca::GetStatusRequest* request,
                         ::holpaca::GetStatusResponse* response) override final;

  grpc::Status Resize(grpc::ServerContext* context,
                      const ::holpaca::ResizeRequest* request,
                      ::holpaca::ResizeResponse* response) override final;

  // end GRPC stuff

  using Super = ::facebook::cachelib::CacheAllocator<CacheTrait>;

  //  std::unordered_map<PoolId, std::mutex> m_shardMutexes;
  std::unordered_map<PoolId, std::shared_ptr<Shards>> m_shards;

  std::unordered_map<PoolId, std::tuple<uint32_t, double, uint32_t>>
      m_metrics; // diskIOPS, missRatio, throughput

  std::unordered_set<PoolId> m_activePools;

  std::unordered_map<PoolId, double> m_qosLevels;

  std::unordered_map<PoolId, double> m_proportions;

  uint64_t const m_kVirtualSize{0};

  double const m_kProportion{1.0};

 public:
  using Config = CacheAllocatorConfig<CacheAllocator<CacheTrait>>;
  using Trait = CacheTrait;
  using ReadHandle = typename Super::ReadHandle;
  using WriteHandle = typename Super::WriteHandle;
  using Key = typename Super::Key;

  CacheAllocator(Config& config);
  ~CacheAllocator();

  PoolId addPool(std::string name,
                 size_t size = 0,
                 double qosLevel = 0.0,
                 double proportion = 1.0);

  ReadHandle find(Key key);

  bool insert(const WriteHandle& handle);

  WriteHandle insertOrReplace(const WriteHandle& handle);

  void registerMetrics(PoolId poolId,
                       uint32_t diskIOPS,
                       double missRatio,
                       uint32_t throughput);

  void removePool(PoolId id);
};

using LruAllocator = CacheAllocator<LruCacheTrait>;
using LruWithSpinAllocator = CacheAllocator<LruCacheWithSpinBucketsTrait>;
using Lru2QAllocator = CacheAllocator<Lru2QCacheTrait>;
using TinyLFUAllocator = CacheAllocator<TinyLFUCacheTrait>;

} // namespace holpaca
} // namespace cachelib
} // namespace facebook
