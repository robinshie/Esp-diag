#include <iomanip>
#include <iostream>
#include "inference.h"
int main(){
  std::string line;
  while(std::getline(std::cin,line)){
    const auto p=tinylog::predict(line);
    std::cout<<tinylog::LABELS[p.category]<<' '<<p.accepted<<' '<<p.truncated;
    for(auto score:p.probabilities)std::cout<<' '<<std::setprecision(9)<<score;
    std::cout<<'\n';
  }
}
