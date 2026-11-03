#pragma once
#include "console_session.h"

namespace switchtransport {
struct UsbSerialDevice {
  uint16_t vendor=0,product=0,revision=0;
  uint64_t generation=0;
};
struct UsbSerialSettings {
  uint16_t vendor=0x067b,product=0x2303;
  unsigned baud=9600;
  // This contract requires 8 data bits, no parity, 1 stop bit, no flow control.
};
// Driver boundary for the board's native Type-C USB OTG controller.
// No PL2303 hardware driver is implied by this interface. A concrete driver must:
// - initialize host mode with native USB CDC/JTAG released;
// - reject devices outside the requested VID/PID before serial initialization;
// - report ready only after PL2303 initialization and line coding succeed;
// - change generation on every attach/detach and discard old RX/TX buffers;
// - provide bounded, nonblocking byte IO; never replay a pending TX on reconnect.
// tick() must also return promptly; enumeration/control requests belong in the
// driver's event/task machinery, not a blocking loop in the BDI application.
// VBUS power is a board/cable requirement, not something begin() can create.
class IUsbSerialHost : public IConsolePort {
public:
  virtual bool begin(const UsbSerialSettings& settings)=0;
  virtual void end()=0;
  virtual void tick()=0;
  virtual bool ready()const=0;
  virtual UsbSerialDevice device()const=0;
  virtual const char* stateName()const=0;
};
}
