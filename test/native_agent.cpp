#include <iostream>
#include "cluster_runtime.h"
std::string bdi_device;
std::function<void(const std::string&)> bdi_output;
std::function<void()> bdi_request_action;
int main(int argc, char** argv) {
  bdi_output = [](const std::string& s) { std::cout << s << '\n'; };
  ClusterRuntime runtime(argc > 1 ? argv[1] : "native-boot");
  runtime.model.output = bdi_output;
  if (argc > 2) runtime.model.max_devices = std::stoul(argv[2]);
  std::string line;
  while (std::getline(std::cin, line)) {
    if (line.compare(0, 5, "TICK ") == 0) runtime.model.tick(std::stoull(line.substr(5)));
    else runtime.model.receive(line);
    std::cout << "[SYNC]" << std::endl;
  }
}
