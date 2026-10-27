// Glaze Library
// For the license information refer to glaze.hpp
#include "glaze/kdl.hpp"
#include "ut/ut.hpp"
using namespace ut;
struct lazy_child
{
   int count{};
   std::string text{};
};
struct lazy_config
{
   int eager{};
   glz::lazy<lazy_child> child{};
   glz::lazy<int> scalar{};
};
suite lazy_tests = [] {
   "defer typed binding and cache"_test = [] {
      const std::string input = "eager 3\nchild { count 42; text \"hello\"; }\nscalar 7";
      auto value = glz::read_kdl<lazy_config>(input);
      expect(bool(value));
      if (!value) return;
      expect(value->eager == 3 && !value->child.materialized());
      expect(value->child.source().starts_with("child"));
      auto child = value->child.get();
      expect(child && child->get().count == 42 && child->get().text == "hello");
      if (child) child->get().count = 9;
      expect(value->child.get()->get().count == 9);
      expect(value->scalar.get()->get() == 7);
      auto written = glz::write_kdl(*value);
      expect(written && written->find("count 9") != std::string::npos);
      const auto copy = *value;
      expect(copy.child.get()->get().count == 9);
   };
   "validate grammar now and type on demand"_test = [] {
      lazy_config target;
      expect(!glz::read_kdl(target, "scalar wrong"));
      expect(!target.scalar.get());
      expect(!target.scalar.get());
      expect(bool(glz::read_kdl(target, "child { count \"unterminated }")));
      glz::lazy<int> empty;
      expect(!empty.get());
   };
};
int main() {}
