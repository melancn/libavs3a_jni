#include "avs3a/timeline.h"
#include <cassert>
#include <cstdio>

using namespace avs3a;

int timeline_test_main() {
    printf("=== Timeline tests ===\n");

    {
        Timeline tl;
        assert(!tl.has_anchor());
        assert(tl.compute_pts(0, 48000, 1024) == TIME_UNSET);
        printf("  no anchor: PASS\n");
    }

    {
        Timeline tl;
        tl.set_anchor(1000000, 1);
        assert(tl.has_anchor());
        int64_t pts0 = tl.compute_pts(0, 48000, 1024);
        assert(pts0 == 1000000);
        int64_t pts1 = tl.compute_pts(1, 48000, 1024);
        assert(pts1 == 1000000 + (1024 * 1000000LL) / 48000);
        printf("  anchor + advance: PASS\n");
    }

    {
        Timeline tl;
        tl.set_anchor(0, 1);
        int64_t pts100 = tl.compute_pts(100, 48000, 1024);
        int64_t expected = (100 * 1024 * 1000000LL) / 48000;
        assert(pts100 == expected);
        printf("  long accumulation: PASS\n");
    }

    {
        Timeline tl;
        tl.set_anchor(5000000, 1);
        tl.flush();
        assert(!tl.has_anchor());
        printf("  flush: PASS\n");
    }

    printf("=== Timeline tests: ALL PASS ===\n");
    return 0;
}
