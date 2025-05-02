#pragma once
#include <random>
extern "C" {
#include <gsl/gsl_siman.h>
}

namespace facebook {
namespace cachelib {
namespace holpaca {

class Optimizable {
 public:
  struct Parameters {
    unsigned int const m_kMaxTries;
    unsigned int const m_kIterationsPerTemperature;
    double const m_kInitialTemperature;
    double const m_kMinTemperature;
    double const m_kCoolingRate;
  };

 private:
  gsl_siman_params_t const m_kParams;
  gsl_rng* m_r;

 public:
  Optimizable(const Parameters& parameters);
  ~Optimizable();

  int getRandomUniformInt(int max);

  virtual void step() = 0;
  virtual double energy() const = 0;
  virtual double distance(Optimizable const* other) const = 0;

  void run();
};

} // namespace holpaca
} // namespace cachelib
} // namespace facebook
