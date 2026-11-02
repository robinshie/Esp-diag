#pragma once
// Legacy fixed H3C helpers. The SSH worker receives IDeviceAdapter explicitly.
#include "command_result.h"
#include "terminal_capture.h"
#include "../adapters/h3c/cli.h"
namespace switchcli {
inline const char* command(const std::string& key){return h3ccli::command(key);}
class Capture:public TerminalCapture {
public:
 std::string prompt()const{return h3ccli::prompt(tailText());}
 bool failed()const{return h3ccli::failed(output);}
};
}
