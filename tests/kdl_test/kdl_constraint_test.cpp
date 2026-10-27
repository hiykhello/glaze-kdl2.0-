// Glaze Library
// For the license information refer to glaze.hpp
#include <functional>
#include <map>

#include "glaze/json/read.hpp"
#include "glaze/json/write.hpp"
#include "glaze/kdl.hpp"
#include "ut/ut.hpp"
using namespace ut;
struct constrained_child
{
   int low{};
   int high{};
};
template <>
struct glz::meta<constrained_child>
{
   static constexpr auto value = object(&constrained_child::low, &constrained_child::high);
   static constexpr auto self_constraint =
      glz::self_constraint<[](const constrained_child& x) { return x.low < x.high; }, "low must be below high">;
};
struct constrained_config
{
   int port = 80;
   std::string name = "initial";
   std::vector<int> values{1};
   constrained_child child{1, 2};
   bool valid_port(int x) const { return x > 0 && x < 65536; }
   std::function<bool(const std::vector<int>&)> positive = [](const auto& x) {
      return std::ranges::all_of(x, [](int n) { return n > 0; });
   };
};
template <>
struct glz::meta<constrained_config>
{
   using T = constrained_config;
   static constexpr auto value = object(
      "port", read_constraint<&T::port, &T::valid_port, "invalid port">, "name",
      read_constraint<&T::name, [](const T&, const std::string& x) { return x.size() < 8; }, "name too long">, "values",
      read_constraint<&T::values, &T::positive, "values must be positive">, "child",
      read_constraint<&T::child, [](const T&, const constrained_child& x) { return x.high < 100; }, "high too large">);
};
struct callable_constraint
{
   int count = 3;
};
template <>
struct glz::meta<callable_constraint>
{
   static constexpr auto value = object(
      "count", read_constraint<[](auto& x) -> decltype(auto) { return (x.count); },
                               [](const callable_constraint&, int n) { return n > 0; }, "count must be positive">);
};
struct constrained_outer
{
   constrained_config config;
};

struct constraint_opts : glz::opts
{
   bool skip_read_constraint = true;
   constexpr constraint_opts() : glz::opts{.format = glz::KDL} {}
};
struct self_opts : glz::opts
{
   bool skip_self_constraint = true;
   constexpr self_opts() : glz::opts{.format = glz::KDL} {}
};
struct child_list
{
   std::vector<constrained_child> items;
   std::optional<constrained_child> optional;
   glz::lazy<constrained_child> deferred;
};
suite constraint_tests = [] {
   "callable targets use shared assignment and writer unwrapping"_test = [] {
      callable_constraint target;
      expect(!glz::read_kdl(target, "count 7"));
      expect(target.count == 7);
      expect(glz::read_kdl(target, "count 0").ec == glz::error_code::constraint_violated);
      expect(target.count == 7);
      const auto& ref = target;
      expect(glz::write_kdl(ref).value_or("") == "count 7\n");
      expect(glz::write_json(ref).value_or("") == R"({"count":7})");
   };
   "same shared field rules in KDL and JSON"_test = [] {
      constrained_config value;
      expect(!glz::read_kdl(value, "port 8080; name hello; values 1 2 3; child {low 1; high 9;}"));
      expect(value.port == 8080 && value.values.size() == 3);
      auto error = glz::read_kdl(value, "port -1");
      expect(error.ec == glz::error_code::constraint_violated && error.custom_error_message == "invalid port");
      expect(value.port == 8080 && error.count > 0);
      expect(glz::read_json(value, R"({"port":-1})").ec == error.ec);
      expect(glz::read_kdl(value, "values 1 -2").ec == glz::error_code::constraint_violated);
      expect(value.values == std::vector{1, 2, 3});
      expect(glz::read_kdl(value, "child {low 1; high 101;}").custom_error_message == "high too large");
      expect(value.child.high == 9);
      expect(glz::read_kdl(value, "child {low 10; high 2;}").custom_error_message == "low must be below high");
      expect(value.child.high == 9);
      expect(!glz::read_kdl(value, "")); // Missing members do not run field checks.
      expect(!glz::read_kdl(value, "port 8; /- port -1"));
      expect(value.port == 8);
   };
   "properties duplicates syntax precedence and skip options"_test = [] {
      constrained_outer value;
      expect(!glz::read_kdl(value, "config port=42 name=hello"));
      expect(value.config.port == 42);
      auto error = glz::read_kdl(value, "config port=-2");
      expect(error.ec == glz::error_code::constraint_violated && value.config.port == 42);
      constrained_config target;
      expect(glz::read_kdl(target, "port 12; port -1").ec == glz::error_code::constraint_violated);
      expect(target.port == 12);
      auto syntax = glz::read_kdl(target, "port #invalid");
      expect(syntax.ec != glz::error_code::constraint_violated && target.port == 12);
      expect(!glz::read<constraint_opts{}>(target, "port -1"));
      expect(target.port == -1);
      constrained_child child;
      expect(!glz::read<self_opts{}>(child, "low 10; high 1"));
      expect(child.low == 10);
      auto self = glz::read_kdl(child, "low 10; high 1");
      expect(self.ec == glz::error_code::constraint_violated && child.high == 1);
   };
   "nested nullable sequence lazy and writer wrappers"_test = [] {
      const std::string input = "items { - low=1 high=2; }\noptional {low 1; high 3;}\ndeferred {low 4; high 2;}";
      child_list target;
      expect(!glz::read_kdl(target, input));
      auto deferred = target.deferred.get();
      expect(!deferred && deferred.error().custom_error_message == "low must be below high");
      expect(glz::read_kdl(target, "items { - low=4 high=2; }").ec == glz::error_code::constraint_violated);
      expect(glz::read_kdl(target, "optional {low 4; high 2;}").ec == glz::error_code::constraint_violated);
      constrained_config original;
      auto text = glz::write_kdl(original);
      expect(bool(text));
      if (text) expect(bool(glz::read_kdl<constrained_config>(*text)));
      auto schema = glz::kdl::write_schema<constrained_config>();
      expect(bool(schema)); // Writer schema unwraps constraints, as writer does.
   };
};
int main() {}
