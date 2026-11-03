#pragma once
// Build-selected physical channel. Saved switch credentials/target are shared.
// 0=Ethernet SSH, 1=UART1 -> 3.3V MAX3232 -> RS232, 2=TCP serial server, 3=SSH primary + UART Console backup.
#ifndef ESP_SWITCH_CHANNEL
#define ESP_SWITCH_CHANNEL 0
#endif
#if ESP_SWITCH_CHANNEL < 0 || ESP_SWITCH_CHANNEL > 3
#error Unsupported switch channel; USB host requires a concrete driver first
#endif
#ifndef ESP_CONSOLE_TX
#define ESP_CONSOLE_TX 17
#endif
#ifndef ESP_CONSOLE_RX
#define ESP_CONSOLE_RX 18
#endif
#if !((ESP_CONSOLE_TX == 17 && ESP_CONSOLE_RX == 18) || (ESP_CONSOLE_TX == 18 && ESP_CONSOLE_RX == 17))
#error Console UART must use the reserved GPIO17/18 pair on this board
#endif
#ifndef ESP_CONSOLE_BAUD
#define ESP_CONSOLE_BAUD 9600
#endif
#ifndef ESP_CONSOLE_BRIDGE_HOST
#define ESP_CONSOLE_BRIDGE_HOST "192.168.10.3"
#endif
#ifndef ESP_CONSOLE_BRIDGE_PORT
#define ESP_CONSOLE_BRIDGE_PORT 4001
#endif
