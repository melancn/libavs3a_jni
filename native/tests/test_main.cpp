#include <cassert>
#include <cstdio>
#include <cstring>

extern int framer_test_main();
extern int session_test_main();
extern int timeline_test_main();
extern int registry_test_main();
extern int abi_access_test_main();

int main() {
    int failures = 0;
    failures += framer_test_main();
    failures += session_test_main();
    failures += timeline_test_main();
    failures += registry_test_main();
    failures += abi_access_test_main();

    if (failures == 0) {
        printf("ALL TESTS PASSED\n");
    } else {
        printf("%d TEST SUITE(S) FAILED\n", failures);
    }
    return failures ? 1 : 0;
}
