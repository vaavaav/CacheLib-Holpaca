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

#include <shared_mutex>
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

  std::shared_timed_mutex m_externalSizeMutex;
  std::unordered_map<PoolId, std::unordered_map<std::string, uint32_t>>
      m_externalSize;

  std::shared_timed_mutex m_shardsMutex;
  std::unordered_map<PoolId, std::shared_ptr<Shards>> m_shards;

  std::shared_timed_mutex m_diskIOPSMutex;
  std::unordered_map<PoolId, uint32_t> m_diskIOPS;

  std::shared_timed_mutex m_activePoolsMutex;
  std::unordered_set<PoolId> m_activePools;

  std::shared_timed_mutex m_qosLevelsMutex;
  std::unordered_map<PoolId, double> m_qosLevels;

 public:
  using Config = CacheAllocatorConfig<CacheAllocator<CacheTrait>>;
  using Trait = CacheTrait;
  using ReadHandle = typename Super::ReadHandle;
  using WriteHandle = typename Super::WriteHandle;
  using Key = typename Super::Key;

  CacheAllocator(Config& config);
  ~CacheAllocator();

  PoolId addPool(std::string name, size_t size = 0, double qosLevel = 0.0);

  ReadHandle find(Key key);

  bool insert(const WriteHandle& handle);

  WriteHandle insertOrReplace(const WriteHandle& handle);

  void registerDiskIOPS(PoolId poolId, uint32_t diskIOPS);

  void removePool(PoolId id);
};

using LruAllocator = CacheAllocator<LruCacheTrait>;
using LruWithSpinAllocator = CacheAllocator<LruCacheWithSpinBucketsTrait>;
using Lru2QAllocator = CacheAllocator<Lru2QCacheTrait>;
using TinyLFUAllocator = CacheAllocator<TinyLFUCacheTrait>;

} // namespace holpaca
} // namespace cachelib
} // namespace facebook
