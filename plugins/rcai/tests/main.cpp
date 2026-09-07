#include <cstdio>

#include "../src/util/test.h"

int main(int argc, char** argv) {
    const std::string filter = argc > 1 ? argv[1] : "";
    const int failed = rcai::test::runAll(filter);
    const int total = int(rcai::test::registry().size());
    std::printf("\n%d/%d tests passed\n", total - failed, total);
    return failed == 0 ? 0 : 1;
}
