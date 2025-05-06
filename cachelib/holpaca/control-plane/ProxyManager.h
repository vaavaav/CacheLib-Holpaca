#pragma once

#include <cachelib/holpaca/control-plane/CacheProxy.h>

namespace facebook {
namespace cachelib {
namespace holpaca {

class ProxyManager {
 public:
  virtual std::unordered_map<std::string, std::shared_ptr<CacheProxy>>
  getCaches() = 0;
  virtual std::shared_ptr<CacheProxy> getCache(const std::string& address) = 0;
};
} // namespace holpaca
} // namespace cachelib
} // namespace facebook
