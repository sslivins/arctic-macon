#pragma once

// ---------------------------------------------------------------------------
// Macon/Tuya MCU bus wire parameters.
//
// The serial line parameters (baud rate + character framing) are a property of
// the Macon/Tuya MCU protocol, NOT of any particular board: every device that
// talks to a Macon mainboard must open its UART at exactly these settings. They
// therefore belong with the rest of the protocol knowledge in this library, so
// no consumer hardcodes a magic baud/parity of its own.
//
// This header is deliberately pure data (no ESP-IDF / FreeRTOS / driver types):
// the platform transport (e.g. the controller's ESP-IDF UART shim) reads these
// values and maps them onto whatever its UART driver expects. The library never
// touches a UART itself.
// ---------------------------------------------------------------------------

#include <cstdint>

namespace arctic {

// Character parity, expressed protocol-neutrally so no driver enum leaks in.
enum class MaconParity : uint8_t {
    None = 0,
    Even = 1,
    Odd  = 2,
};

// Complete wire settings for the Macon RS485/UART bus.
struct MaconBusParams {
    uint32_t    baud;       // bits per second
    uint8_t     data_bits;  // data bits per character
    MaconParity parity;     // parity mode
    uint8_t     stop_bits;  // stop bits per character
};

// The canonical Macon bus settings: 4800 baud, 8 data bits, even parity, 1 stop
// bit (4800 8-E-1). Validated against the real Arctic (Macon) mainboard bus.
constexpr MaconBusParams MACON_BUS_PARAMS = {
    /* baud      */ 4800,
    /* data_bits */ 8,
    /* parity    */ MaconParity::Even,
    /* stop_bits */ 1,
};

}  // namespace arctic
