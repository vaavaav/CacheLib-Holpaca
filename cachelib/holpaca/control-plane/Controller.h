#pragma once
#include <cachelib/holpaca/control-plane/ProxyManager.h>
#include <cachelib/holpaca/control-plane/algorithms/ControlAlgorithm.h>
#include <cachelib/holpaca/protos/Holpaca.grpc.pb.h>
#include <cachelib/holpaca/protos/Holpaca.pb.h>
#include <grpcpp/server.h>

#include <atomic>
#include <shared_mutex>
#include <thread>
#include <unordered_map>

namespace facebook {
namespace cachelib {
namespace holpaca {
class Controller : public ::holpaca::Controller::Service, public ProxyManager {
  std::shared_ptr<grpc::Server> const m_kServer;
  std::thread m_serverThread;
  std::atomic_bool m_stop{false};

  grpc::Status Connect(grpc::ServerContext* context,
                       const ::holpaca::ConnectRequest* request,
                       ::holpaca::ConnectResponse* response);

  grpc::Status Disconnect(grpc::ServerContext* context,
                          const ::holpaca::DisconnectRequest* request,
                          ::holpaca::DisconnectResponse* response);

  std::shared_timed_mutex m_proxiesMutex;
  std::unordered_map<std::string, std::shared_ptr<::holpaca::Stage::Stub>>
      m_proxies;

  std::vector<std::unique_ptr<ControlAlgorithm>> m_controlAlgorithms;

  std::unordered_map<std::string, ProxyManager::CacheStatus> getStatus()
      override final;
  void resize(
      const std::vector<ProxyManager::CacheResize>& cacheResize) override final;

 public:
  Controller(const std::string& kControllerAddress);
  ~Controller();

  template <typename T, typename... Args>
  Controller& addAlgorithm(Args... args) {
    m_controlAlgorithms.emplace_back(
        std::make_unique<T>(dynamic_cast<ProxyManager* const>(this), args...));
    return *this;
  }
};
} // namespace holpaca
} // namespace cachelib
} // namespace facebook
