#include "test_framework.h"
#include <iostream>
#include <cstring>

int main(int argc, char* argv[]) {
    std::string suite_filter = "";
    std::string test_filter = "";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--suite" && i + 1 < argc) {
            suite_filter = argv[++i];
        } else if (arg == "--test" && i + 1 < argc) {
            test_filter = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: porcelain_tests [options]\n"
                      << "Options:\n"
                      << "  --suite <name>     Run only tests in this suite\n"
                      << "  --test <name>      Run only tests matching this substring\n"
                      << "  -h, --help         Show this help\n\n"
                      << "Available suites:\n"
                      << "  StressFEMTests          - Stress FEM algorithm tests\n"
                      << "  WashburnModelTests      - Washburn penetration tests\n"
                      << "  BendingTestTests        - Four-point bending tests\n"
                      << std::endl;
            return 0;
        }
    }

    return porcelain_test::TestRegistry::instance().run_all(suite_filter, test_filter);
}
