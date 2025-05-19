#pragma once

#include <cachelib/allocator/CacheAllocatorConfig.h>

namespace facebook {
namespace cachelib {
namespace holpaca {

template <typename CacheT>
class CacheAllocatorConfig
    : public ::facebook::cachelib::CacheAllocatorConfig<
          ::facebook::cachelib::CacheAllocator<typename CacheT::Trait>> {
  std::string m_address;

 public:
  CacheAllocatorConfig& setAddress(std::string address) {
    m_address = address;
    return *this;
  }

  friend CacheT;
};
} // namespace holpaca
} // namespace cachelib
} // namespace facebook
