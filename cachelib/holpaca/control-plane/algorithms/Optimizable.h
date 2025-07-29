#pragma once
extern "C" {
#include <gsl/gsl_siman.h>
}

namespace facebook {
namespace cachelib {
namespace holpaca {

template <typename T>
class Optimizable {
  gsl_rng* m_r;

  static void step(const gsl_rng* r, void* xp, double step_size /*unused*/) {
    (static_cast<T*>(xp))->step();
  }
  static double energy(void* xp) {
    return (static_cast<T const*>(xp))->energy();
  }
  static double distance(void* xp, void* yp) {
    return static_cast<T const*>(xp)->distance(static_cast<T const*>(yp));
  }

 public:
  Optimizable() {
    gsl_rng_env_setup();
    m_r = gsl_rng_alloc(gsl_rng_default);
  }
  ~Optimizable() { gsl_rng_free(m_r); }

  int randomUniformInt(int max) {
    if (max <= 0) {
      return 0;
    }
    return gsl_rng_uniform_int(m_r, max);
  }

  virtual bool skip() const { return false; };
  virtual void step() = 0;
  virtual double energy() const = 0;
  virtual double distance(Optimizable const* other) const = 0;

  void run(unsigned int const kMaxTries,
           unsigned int const kIterationsPerTemperature,
           double const kNormalizingFactor,
           double const kInitialTemperature,
           double const kMinTemperature,
           double const kCoolingRate) {
    if (!skip()) {
      gsl_siman_solve(
          m_r, static_cast<T*>(this), Optimizable<T>::energy,
          Optimizable<T>::step, Optimizable<T>::distance, NULL, NULL, NULL,
          NULL, sizeof(*static_cast<T*>(this)),
          {
              .n_tries = static_cast<int>(kMaxTries),
              .iters_fixed_T = static_cast<int>(kIterationsPerTemperature),
              .step_size = 1.0, // dummy value
              .k = 1.0,
              .t_initial = kInitialTemperature,
              .mu_t = kCoolingRate,
              .t_min = kMinTemperature,
          });
    }
  }
};

} // namespace holpaca
} // namespace cachelib
} // namespace facebook
