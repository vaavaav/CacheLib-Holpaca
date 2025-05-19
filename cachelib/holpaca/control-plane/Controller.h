#pragma once
#include <cachelib/holpaca/control-plane/CacheProxy.h>
#include <cachelib/holpaca/control-plane/algorithms/ControlAlgorithm.h>
#include <grpcpp/server.h>

#include <atomic>
#include <shared_mutex>
#include <thread>
#include <unordered_map>

namespace facebook {
namespace cachelib {
namespace holpaca {
class Controller {
  std::shared_ptr<CacheProxy> const m_kProxy;
  std::vector<std::unique_ptr<ControlAlgorithm>> m_controlAlgorithms;

 public:
  Controller(const std::string& kCacheAddress,
             CacheProxy::CommunicationType type);
  ~Controller();

  template <typename T, typename... Args>
  Controller& addAlgorithm(Args... args) {
    m_controlAlgorithms.emplace_back(std::make_unique<T>(m_kProxy, args...));
    return *this;
  }
};
} // namespace holpaca
} // namespace cachelib
} // namespace facebook
