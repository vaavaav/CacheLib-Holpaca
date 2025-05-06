#pragma once
#include <cachelib/holpaca/control-plane/ControllerConfig.h>
#include <cachelib/holpaca/control-plane/ProxyManager.h>
#include <cachelib/holpaca/control-plane/algorithms/ControlAlgorithm.h>
#include <grpcpp/server.h>

#include <atomic>
#include <shared_mutex>
#include <thread>
#include <unordered_map>

namespace facebook {
namespace cachelib {
namespace holpaca {
class Controller : public ::holpaca::Controller::Service, public ProxyManager {
  grpc::Status KeepAlive(grpc::ServerContext* context,
                         const ::holpaca::KeepAliveRequest* request,
                         ::holpaca::KeepAliveResponse* response) override;

  std::unordered_map<std::string, std::shared_ptr<CacheProxy>> getCaches()
      override final;
  std::shared_ptr<CacheProxy> getCache(
      const std::string& address) override final;

  std::shared_ptr<grpc::Server> m_server;
  std::thread m_serverThread;

  std::vector<std::unique_ptr<ControlAlgorithm>> m_controlAlgorithms;

  std::shared_timed_mutex m_mutex;
  std::unordered_map<std::string, std::shared_ptr<CacheProxy>> m_proxies;
  std::thread m_cleanerThread;
  std::atomic_bool m_stop{false};
  static constexpr std::chrono::nanoseconds s_kCleanerPeriodicity =
      std::chrono::seconds(5);

 public:
  Controller(std::string address);
  ~Controller();

  template <typename T, typename... Args>
  Controller& addAlgorithm(Args... args) {
    m_controlAlgorithms.emplace_back(std::make_unique<T>(this, args...));
    return *this;
  }
};
} // namespace holpaca
} // namespace cachelib
} // namespace facebook
