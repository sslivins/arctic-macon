// Native unit test for the Macon bus wire parameters (macon_bus.h).
// Framework-free: prints failures and returns non-zero on any failure.

#include "macon_bus.h"

#include <cstdio>

using namespace arctic;

static int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

int main() {
    // The canonical Macon bus is 4800 8-E-1. These are protocol facts; a
    // consumer must be able to configure its UART straight from them.
    CHECK(MACON_BUS_PARAMS.baud == 4800);
    CHECK(MACON_BUS_PARAMS.data_bits == 8);
    CHECK(MACON_BUS_PARAMS.parity == MaconParity::Even);
    CHECK(MACON_BUS_PARAMS.stop_bits == 1);

    // Parity enum values are stable (a transport may switch on them).
    CHECK(static_cast<uint8_t>(MaconParity::None) == 0);
    CHECK(static_cast<uint8_t>(MaconParity::Even) == 1);
    CHECK(static_cast<uint8_t>(MaconParity::Odd) == 2);

    // constexpr usability: must be a compile-time constant.
    static_assert(MACON_BUS_PARAMS.baud == 4800, "baud must be constexpr");

    if (g_failures) {
        std::printf("%d CHECK(s) failed\n", g_failures);
        return 1;
    }
    std::printf("all bus-params checks passed\n");
    return 0;
}
