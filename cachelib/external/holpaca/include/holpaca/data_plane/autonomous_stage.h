#pragma once
#include <holpaca/data_plane/stage.h>
#include <holpaca/data_plane/cache.h>
#include <holpaca/control_algorithm/control_algorithm.h>
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_OFF
#include <spdlog/spdlog.h>
#include <memory>

using namespace holpaca::control_algorithm;

namespace holpaca::data_plane {
    class AutonomousStage : public Stage {
        std::shared_ptr<spdlog::logger> m_logger;
        std::vector<std::shared_ptr<ControlAlgorithm>> m_control_algorithms;

        public: 
            AutonomousStage(
                std::shared_ptr<Cache> cache,
                char const* log_file = { config::stage_log_file }
            );
    };
}