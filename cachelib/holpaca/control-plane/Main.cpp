#include <cachelib/holpaca/control-plane/Controller.h>
#include <cachelib/holpaca/control-plane/algorithms/ControlAlgorithm.h>
#include <cachelib/holpaca/control-plane/algorithms/MarginalHits.h>
#include <cachelib/holpaca/control-plane/algorithms/PerformanceMaximization.h>
#include <cachelib/holpaca/control-plane/algorithms/Printer.h>

#include <iostream>
#include <sstream>

using namespace facebook::cachelib::holpaca;

std::vector<std::string> split(const std::string& str, char delimiter) {
  std::vector<std::string> tokens;
  std::string token;
  std::istringstream tokenStream(str);
  while (std::getline(tokenStream, token, delimiter)) {
    tokens.push_back(token);
  }
  return tokens;
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0]
              << " <address> [<control-algorithm> <arg0:arg1:...:argn>]..."
              << std::endl;
    return 1;
  }

  Controller controller(argv[1]);

  for (int i = 2; i < argc; i += 2) {
    if (i + 1 >= argc) {
      std::cerr << "Control algorithm requires at least 1 argument: "
                   "<control-algorithm> <arg0:arg1:...:argn>"
                << std::endl;
      return 1;
    }
    auto args = split(argv[i + 1], ':');
    if (std::string(argv[i]) == "HitRatioMaximization") {
      if (args.size() < 2) {
        std::cerr
            << "HitRatioMaximization requires 2 arguments: <periodicity (ms)> "
               "<max delta>"
            << std::endl;
        return 1;
      }
      controller.addAlgorithm<PerformanceMaximization>(
          std::chrono::milliseconds(std::stoul(args[0])),
          PerformanceMaximization::MetricType::kHitRatio, std::stod(args[1]),
          std::unordered_map<std::string, double>{});
    } else if (std::string(argv[i]) == "ThroughputMaximization") {
      if (args.size() < 2) {
        std::cerr << "Throughput requires 2 arguments: <periodicity (ms)> "
                     "<max delta ([0,1])>"
                  << std::endl;
        return 1;
      }
      controller.addAlgorithm<PerformanceMaximization>(
          std::chrono::milliseconds(std::stoul(args[0])),
          PerformanceMaximization::MetricType::kThroughput, std::stod(args[1]),
          std::unordered_map<std::string, double>{});
    } else if (std::string(argv[i]) == "MarginalHits") {
      if (args.size() < 1) {
        std::cerr << "MarginalHits requires 1 argument: <periodicity (ms)>"
                  << std::endl;
        return 1;
      }
      controller.addAlgorithm<MarginalHits>(
          std::chrono::milliseconds(std::stoul(args[0])));
    } else if (std::string(argv[i]) == "Printer") {
      if (args.size() < 1) {
        std::cerr << "Printer requires 1 argument: <periodicity (ms)>"
                  << std::endl;
        return 1;
      }
      controller.addAlgorithm<Printer>(
          std::chrono::milliseconds(std::stoul(args[0])));
    } else {
      std::cerr << "Unknown control algorithm: " << argv[i] << std::endl;
      return 1;
    }
  }

  // If something gets in stdin then we will exit
  std::string ignore;
  std::getline(std::cin, ignore);
  return 0;
}
