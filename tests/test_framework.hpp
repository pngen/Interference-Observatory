#pragma once
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>
#include <functional>
#include <sstream>
#include <exception>

namespace iobstest {

struct TestCase {
  std::string name;
  std::function<void()> fn;
};

inline std::vector<TestCase>& registry() { static std::vector<TestCase> r; return r; }

struct Registrar {
  Registrar(const std::string& name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

inline int failures = 0;
inline int runs = 0;
inline std::string current;

inline void report_fail(const char* file, int line, const std::string& msg) {
  std::fprintf(stderr, "FAIL [%s:%d] %s: %s\n", file, line, current.c_str(), msg.c_str());
  ++failures;
}

inline int run_all(int argc, char** argv) {
  std::string filter = (argc > 1) ? argv[1] : "";
  for (auto& t : registry()) {
    if (!filter.empty() && t.name.find(filter) == std::string::npos) continue;
    current = t.name;
    ++runs;
    try {
      t.fn();
    } catch (const std::exception& e) {
      report_fail("<exception>", 0, std::string("uncaught: ") + e.what());
    } catch (...) {
      report_fail("<exception>", 0, "uncaught: unknown");
    }
  }
  std::fprintf(stderr, "\n[%s] %d test(s), %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", runs, failures);
  return failures == 0 ? 0 : 1;
}

}  // namespace iobstest

#define TEST(name) static void test_##name(); static ::iobstest::Registrar reg_##name(#name, test_##name); static void test_##name()

#define CHECK(cond) do { if (!(cond)) ::iobstest::report_fail(__FILE__, __LINE__, "CHECK(" #cond ") failed"); } while (0)

#define CHECK_EQ(a, b) do { auto _a = (a); auto _b = (b); if (!(_a == _b)) { ::iobstest::report_fail(__FILE__, __LINE__, "CHECK_EQ(" #a ", " #b ") failed"); } } while (0)

#define CHECK_NEAR(a, b, eps) do { double _a = (a); double _b = (b); if (std::fabs(_a - _b) > (eps)) { ::iobstest::report_fail(__FILE__, __LINE__, "CHECK_NEAR(" #a ", " #b ") failed"); } } while (0)

#define CHECK_THROWS_AS(expr, Ex) do { bool _threw = false; try { (void)(expr); } catch (const Ex&) { _threw = true; } catch (...) {} if (!_threw) ::iobstest::report_fail(__FILE__, __LINE__, "CHECK_THROWS_AS(" #expr ", " #Ex ") did not throw"); } while (0)

#define RUN_ALL return ::iobstest::run_all(argc, argv)
