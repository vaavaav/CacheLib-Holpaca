#include "Optimizable.h"

#include <iostream>

namespace facebook {
namespace cachelib {
namespace holpaca {

Optimizable::Optimizable(const Parameters& parameters)
    : m_kParams({
          .n_tries = static_cast<int>(parameters.m_kMaxTries),
          .iters_fixed_T =
              static_cast<int>(parameters.m_kIterationsPerTemperature),
          .step_size = 1.0, // dummy value
          .k = 1.0,
          .t_initial = parameters.m_kInitialTemperature,
          .mu_t = parameters.m_kCoolingRate,
          .t_min = parameters.m_kMinTemperature,
      }) {
  gsl_rng_env_setup();
  m_r = gsl_rng_alloc(gsl_rng_default);
}

Optimizable::~Optimizable() { gsl_rng_free(m_r); }

int Optimizable::getRandomUniformInt(int max) {
  if (max <= 0) {
    return 0;
  }
  return gsl_rng_uniform_int(m_r, max);
}

void S1(const gsl_rng* r, void* xp, double step_size /* unused */) {
  (*static_cast<Optimizable**>(xp))->step();
}

double E1(void* xp) {
  return (*static_cast<Optimizable const**>(xp))->energy();
}

double M1(void* xp, void* yp) {
  return (*static_cast<Optimizable const**>(xp))
      ->distance(*static_cast<Optimizable const**>(yp));
}

void Optimizable::run() {
  Optimizable* ctx = this;

  gsl_siman_solve(m_r, &ctx, E1, S1, M1, NULL, NULL, NULL, NULL, sizeof ctx,
                  m_kParams);
}

} // namespace holpaca
} // namespace cachelib
} // namespace facebook
