#pragma once
#include <Arduino.h>

// Hardware connections of the Nano ESP32 socket on the main carrier
// (09_团队开发资料_20261020/04_NanoESP32开发/board_pins.h).
// Arduino API calls use the D/A symbols; raw ESP-IDF calls use gpio_raw.
namespace board_pins {
constexpr int DEBUG_RX = D0, DEBUG_TX = D1;  // J605: 3 = RX, 2 = TX, 1 = GND
constexpr int AX_TX = D2, AX_RX = D3, AX_RTS = D4, AX_CTS = D5;
constexpr int AX_EVENT_IRQ = D6;
constexpr int I2S_BCLK = D7, I2S_WS = D8, I2S_SDOUT = D9;
constexpr int SD_CS = D10, SPI_MOSI = D11, SPI_MISO = D12, SPI_SCLK = D13;
constexpr int PN_IRQ = A0, SD_CD = A1, PN_CS = A3;
constexpr int PFAIL_N = A6, AMP_EN = A7;
namespace gpio_raw {
constexpr int DEBUG_RX = 44, DEBUG_TX = 43;
constexpr int AX_TX = 5, AX_RX = 6, AX_RTS = 7, AX_CTS = 8;
constexpr int AX_EVENT_IRQ = 9;
constexpr int I2S_BCLK = 10, I2S_WS = 17, I2S_SDOUT = 18;
constexpr int SD_CS = 21, SPI_MOSI = 38, SPI_MISO = 47, SPI_SCLK = 48;
constexpr int PN_IRQ = 1, SD_CD = 2, PN_CS = 4, PFAIL_N = 13, AMP_EN = 14;
}  // namespace gpio_raw
}  // namespace board_pins
