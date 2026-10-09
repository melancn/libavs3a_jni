#include "avs3a/types.h"
#include "avs3a/status.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdint>

using namespace avs3a;

static const int32_t ARM64_POINTER_BYTES = 8;
static const int32_t ARMV7_POINTER_BYTES = 4;
static const int32_t ARM64_STATE_BYTES = 264;
static const int32_t ARMV7_STATE_BYTES = 164;

static bool check_offset(size_t offset, size_t size, int32_t state_bytes) {
    return offset + size <= static_cast<size_t>(state_bytes) && offset + size >= offset;
}

int abi_access_test_main() {
    printf("=== ABI access tests ===\n");

    {
        uint8_t state[264] = {};
        assert(check_offset(0, 2, 264));
        assert(check_offset(60, 4, 264));
        assert(check_offset(248, 8, 264));
        assert(check_offset(256, 8, 264));
        assert(!check_offset(262, 4, 264));
        printf("  ARM64 offsets: PASS\n");
    }

    {
        uint8_t state[164] = {};
        assert(check_offset(0, 2, 164));
        assert(check_offset(60, 4, 164));
        assert(check_offset(156, 4, 164));
        assert(check_offset(160, 4, 164));
        assert(!check_offset(162, 4, 164));
        printf("  ARMv7 offsets: PASS\n");
    }

    {
        uint8_t state[264] = {};
        int32_t val = 48000;
        std::memcpy(state + 4, &val, 4);
        int32_t read_back;
        std::memcpy(&read_back, state + 4, 4);
        assert(read_back == 48000);
        printf("  memcpy read/write: PASS\n");
    }

    {
        assert(ARM64_STATE_BYTES == 64 + 25 * ARM64_POINTER_BYTES);
        assert(ARMV7_STATE_BYTES == 64 + 25 * ARMV7_POINTER_BYTES);
        printf("  state size formula: PASS\n");
    }

    printf("=== ABI access tests: ALL PASS ===\n");
    return 0;
}
