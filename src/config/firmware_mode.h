#pragma once
// The shipped firmware runs independently of a computer or USB gateway.
#ifndef ESP_STANDALONE
#define ESP_STANDALONE 1
#endif
#ifndef ESP_LOG_AI_ENABLED
#define ESP_LOG_AI_ENABLED 1
#endif
#ifndef ESP_BOARD_BOOT_PIN
#define ESP_BOARD_BOOT_PIN 0
#endif
#ifndef ESP_BOARD_RGB_PIN
#define ESP_BOARD_RGB_PIN 21
#endif
