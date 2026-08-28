#pragma once
// Minimal header-only test harness (no external deps, no timeouts).
#include <cstdio>
#include <exception>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace po_test {
struct Failure { std::string msg; };
struct Case { std::string name; std::function<void()> fn; };
class Registry {
 public:
  static Registry& instance() { static Registry r; return r; }
  void add(std::string n, std::function<void()> f) { cases.push_back({std::move(n), std::move(f)}); }
  [[noreturn]] void fail(const std::string& m) { throw Failure{m}; }
  int run() {
    int fails = 0;
    for (auto& c : cases) {
      const int before = fails;
      try { c.fn(); }
      catch (const Failure& e) { ++fails; std::printf("FAIL %s: %s\n", c.name.c_str(), e.msg.c_str()); }
      catch (const std::exception& e) { ++fails; std::printf("FAIL %s (exception): %s\n", c.name.c_str(), e.what()); }
      catch (...) { ++fails; std::printf("FAIL %s (unknown throw)\n", c.name.c_str()); }
      if (fails != before) std::printf("  case '%s' failed\n", c.name.c_str());
    }
    std::printf("\n%d case(s), %d failure(s)\n", static_cast<int>(cases.size()), fails);
    return fails == 0 ? 0 : 1;
  }
  std::vector<Case> cases;
};
} // namespace po_test

#define PO_TEST(name) \
  static void po_fn_##name(); \
  namespace { struct po_reg_##name { po_reg_##name() { po_test::Registry::instance().add(#name, po_fn_##name); } } po_reg_##name##_inst{}; } \
  static void po_fn_##name()

#define PO_CHECK(cond) do { if (!(cond)) po_test::Registry::instance().fail(std::string("CHECK failed at ") + __FILE__ + ":" + std::to_string(__LINE__) + ": " #cond); } while (0)
#define PO_CHECK_EQ(a, b) do { const auto _a = (a); const auto _b = (b); if (!(_a == _b)) po_test::Registry::instance().fail(std::string("CHECK_EQ failed at ") + __FILE__ + ":" + std::to_string(__LINE__) + ": " #a " == " #b); } while (0)
#define PO_MAIN int main() { return po_test::Registry::instance().run(); }
