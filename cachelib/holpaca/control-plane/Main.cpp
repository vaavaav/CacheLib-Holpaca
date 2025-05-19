#include <cachelib/holpaca/control-plane/CacheProxy.h>
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
  if (argc < 3) {
    std::cerr
        << "NAME\n"
        << "  " << argv[0]
        << " - run with specified cache settings and control algorithms\n\n"

        << "SYNOPSIS\n"
        << "  " << argv[0]
        << " <cache address> <communication type> "
           "[<control-algorithm> <arg0:arg1:...:argn>]...\n\n"

        << "DESCRIPTION\n"
        << "  Launches the program using the given cache address and "
           "communication type.\n"
        << "  Optionally, one or more control algorithms may be specified, "
           "each followed by\n"
        << "  a colon-separated list of arguments.\n\n"

        << "OPTIONS\n"
        << "  <cache address>\n"
        << "      The IP address or hostname of the cache to connect to.\n\n"

        << "  <communication type>\n"
        << "      Type of communication to use. One of:\n"
        << "        all     Broadcast to all recipients.\n"
        << "        single  Communicate with a single target.\n\n"

        << "  <control-algorithm>\n"
        << "      (Optional) Name of a control algorithm module to run.\n\n"

        << "  <arg0:arg1:...:argn>\n"
        << "      (Optional) Colon-separated arguments passed to the control "
           "algorithm.\n\n"

        << "EXAMPLES\n"
        << "  " << argv[0] << " 127.0.0.1 all\n"
        << "  " << argv[0]
        << " 192.168.0.1 single HitRatioMaximization 1000:0.5\n\n"

        << "NOTES\n"
        << "  Multiple control algorithms can be specified in sequence, each "
           "with their\n"
        << "  own set of colon-separated arguments.\n"
        << std::endl;

    return 1;
  }

  CacheProxy::CommunicationType type;
  if (argv[2] == std::string("all")) {
    type = CacheProxy::CommunicationType::kAll;
  } else if (argv[2] == std::string("single")) {
    type = CacheProxy::CommunicationType::kSingle;
  } else {
    std::cerr << "Unknown communication type: " << argv[2] << std::endl;
    return 1;
  }

  Controller controller(argv[1], type);

  for (int i = 3; i < argc; i += 2) {
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
