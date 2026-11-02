#pragma once
#include "../../domain/switch_policy.h"
namespace h3ccli {
inline const char* command(const std::string& name){
  if(name=="version")return "display version";
  if(name=="device")return "display device";
  if(name=="interfaces")return "display interface brief";
  if(name=="cpu")return "display cpu-usage";
  if(name=="memory")return "display memory";
  if(name=="down")return "display interface brief down";
  if(name=="loops")return "display loopback-detection";
  if(name=="stp")return "display stp brief";
  if(name=="aggregation")return "display link-aggregation verbose";
  if(name=="logs")return "display logbuffer reverse";
  if(name=="mac_moves")return "display mac-address mac-move";
  return nullptr;
}
inline std::string prompt(const std::string& tail){
    auto end=tail.find_last_not_of(" \t");if(end==std::string::npos)return "";
    auto start=tail.find_first_not_of(" \t");auto text=tail.substr(start,end-start+1);
    if(text.size()<3||text.size()>128)return "";
    if(!((text.front()=='<'&&text.back()=='>')||(text.front()=='['&&text.back()==']')))return "";
    if(text.find_first_of("\r\n",1)!=std::string::npos)return "";
    return text;
  }
inline bool failed(const std::string& output){return output.find("% Unrecognized")!=std::string::npos||output.find("% Incomplete")!=std::string::npos||output.find("% Ambiguous")!=std::string::npos||output.find("Permission denied")!=std::string::npos||output.find("Error:")!=std::string::npos||output.find("% Too many")!=std::string::npos||output.find("% Failed")!=std::string::npos||output.find("% Permission")!=std::string::npos||output.find("% Wrong parameter")!=std::string::npos;}
inline std::string interfaceCommand(const std::string& input){
 const auto port=switchmodel::normalizePort(input);if(port.empty())return "";
 const auto at=port.find_first_of("0123456789");return "interface "+port.substr(0,at)+" "+port.substr(at);
}
}
