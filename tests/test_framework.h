#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <algorithm>

namespace porcelain_test {

struct TestResult {
    std::string suite_name;
    std::string test_name;
    bool passed;
    std::string error_message;
    double duration_ms;
};

class TestRegistry {
public:
    static inline TestRegistry& instance() {
        static TestRegistry inst;
        return inst;
    }

    inline void register_test(const std::string& suite, const std::string& name,
                       std::function<void()> fn) {
        tests_.push_back({suite, name, fn});
    }

    inline int run_all(const std::string& filter_suite = "", const std::string& filter_test = "") {
        std::vector<TestResult> results;
        int passed = 0, failed = 0;

        std::cout << "\n========================================" << std::endl;
        std::cout << "  Porcelain Monitor Test Suite v1.0" << std::endl;
        std::cout << "========================================" << std::endl;

        auto t_total_start = std::chrono::high_resolution_clock::now();

        for (auto& t : tests_) {
            if (!filter_suite.empty() && t.suite != filter_suite) continue;
            if (!filter_test.empty() && t.name.find(filter_test) == std::string::npos) continue;

            std::cout << "\n[ RUN      ] " << t.suite << "." << t.name << std::endl;

            auto t_start = std::chrono::high_resolution_clock::now();
            TestResult r{t.suite, t.name, true, "", 0.0};

            try {
                t.fn();
                r.passed = true;
            } catch (const std::exception& e) {
                r.passed = false;
                r.error_message = e.what();
            } catch (...) {
                r.passed = false;
                r.error_message = "Unknown exception";
            }

            auto t_end = std::chrono::high_resolution_clock::now();
            r.duration_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

            if (r.passed) {
                passed++;
                std::cout << "[       OK ] " << t.suite << "." << t.name
                          << " (" << std::fixed << std::setprecision(1) << r.duration_ms << " ms)" << std::endl;
            } else {
                failed++;
                std::cout << "[   FAILED ] " << t.suite << "." << t.name << std::endl;
                std::cout << "             " << r.error_message << std::endl;
            }

            results.push_back(r);
        }

        auto t_total_end = std::chrono::high_resolution_clock::now();
        double total_ms = std::chrono::duration<double, std::milli>(t_total_end - t_total_start).count();

        std::cout << "\n========================================" << std::endl;
        std::cout << "  Results: " << passed << " passed, " << failed << " failed, "
                  << (passed + failed) << " total" << std::endl;
        std::cout << "  Total time: " << std::fixed << std::setprecision(1) << total_ms << " ms" << std::endl;
        std::cout << "========================================" << std::endl;

        return failed > 0 ? 1 : 0;
    }

private:
    struct TestEntry {
        std::string suite;
        std::string name;
        std::function<void()> fn;
    };
    std::vector<TestEntry> tests_;
};

struct TestRegistrar {
    TestRegistrar(const std::string& suite, const std::string& name,
                  std::function<void()> fn) {
        TestRegistry::instance().register_test(suite, name, fn);
    }
};

inline void _assert_impl(bool cond, const std::string& expr_str,
                  const std::string& file, int line) {
    if (!cond) {
        std::ostringstream oss;
        oss << "Assertion failed: " << expr_str << " at " << file << ":" << line;
        throw std::runtime_error(oss.str());
    }
}

inline void _assert_near_impl(double actual, double expected, double tol_pct,
                        const std::string& file, int line) {
    double rel_error = 0.0;
    if (std::abs(expected) > 1e-15) {
        rel_error = std::abs(actual - expected) / std::abs(expected) * 100.0;
    } else if (std::abs(actual) > 1e-15) {
        rel_error = 100.0;
    }
    if (rel_error > tol_pct) {
        std::ostringstream oss;
        oss << "Relative error " << std::fixed << std::setprecision(3) << rel_error
            << "% exceeds tolerance " << tol_pct << "%: actual=" << actual
            << ", expected=" << expected << " at " << file << ":" << line;
        throw std::runtime_error(oss.str());
    }
}

inline void _assert_range_impl(double val, double min, double max,
                         const std::string& file, int line) {
    if (val < min || val > max) {
        std::ostringstream oss;
        oss << "Value " << val << " out of range [" << min << ", " << max
            << "] at " << file << ":" << line;
        throw std::runtime_error(oss.str());
    }
}

}

#define TEST(suite, name) \
    static void suite##_##name(); \
    static porcelain_test::TestRegistrar _reg_##suite##_##name(#suite, #name, suite##_##name); \
    static void suite##_##name()

#define ASSERT_TRUE(expr) \
    porcelain_test::_assert_impl(static_cast<bool>(expr), #expr, __FILE__, __LINE__)

#define ASSERT_FALSE(expr) \
    porcelain_test::_assert_impl(!static_cast<bool>(expr), "!(" #expr ")", __FILE__, __LINE__)

#define ASSERT_EQ(a, b) \
    porcelain_test::_assert_impl((a) == (b), #a " == " #b, __FILE__, __LINE__)

#define ASSERT_NEAR(actual, expected, tol_pct) \
    porcelain_test::_assert_near_impl(actual, expected, tol_pct, __FILE__, __LINE__)

#define ASSERT_RANGE(val, min, max) \
    porcelain_test::_assert_range_impl(val, min, max, __FILE__, __LINE__)

#define ASSERT_GT(a, b) \
    porcelain_test::_assert_impl((a) > (b), #a " > " #b, __FILE__, __LINE__)

#define ASSERT_LT(a, b) \
    porcelain_test::_assert_impl((a) < (b), #a " < " #b, __FILE__, __LINE__)

#define ASSERT_NE(a, b) \
    porcelain_test::_assert_impl((a) != (b), #a " != " #b, __FILE__, __LINE__)

#define ASSERT_GE(a, b) \
    porcelain_test::_assert_impl((a) >= (b), #a " >= " #b, __FILE__, __LINE__)

#define ASSERT_LE(a, b) \
    porcelain_test::_assert_impl((a) <= (b), #a " <= " #b, __FILE__, __LINE__)

#define RUN_ALL_TESTS() porcelain_test::TestRegistry::instance().run_all()

#define RUN_SUITE(suite) porcelain_test::TestRegistry::instance().run_all(#suite, "")
