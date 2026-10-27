// Glaze Library
// For the license information refer to glaze.hpp

#include <map>

#include "glaze/kdl.hpp"
#include "ut/ut.hpp"

using namespace ut;

struct custom_leaf
{
   int value{};
   bool operator==(const custom_leaf&) const = default;
};
struct custom_config
{
   int count{};
   std::string name{};
   custom_leaf inner{};
   std::optional<int> nullable{};
   int calls{};
   void set_count(int x) { count = x * 2; }
   int get_count() const { return count + 1; }
   void set_inner(custom_leaf x) { inner = x; }
   const custom_leaf& get_inner() const { return inner; }
   void trigger() { ++calls; }
};
template <>
struct glz::meta<custom_config>
{
   using T = custom_config;
   static constexpr auto read_name = [](T& target, const std::string& input, glz::context& ctx) {
      if (input == "invalid") {
         ctx.error = glz::error_code::constraint_violated;
         ctx.custom_error_message = "bad name";
      }
      else
         target.name = "prefix:" + input;
   };
   static constexpr auto value =
      object("count", custom<&T::set_count, &T::get_count>, "name", custom<read_name, &T::name>, "inner",
             custom<&T::set_inner, &T::get_inner>, "nullable",
             custom<[](auto& t) -> auto& { return t.nullable; }, [](auto& t) -> auto& { return t.nullable; }>,
             "trigger", custom<&T::trigger, &T::calls>, "ignored", custom<glz::skip{}, &T::calls>);
};
struct wrapped_count
{
   int value{};
};
template <>
struct glz::meta<wrapped_count>
{
   static constexpr auto value =
      custom<[](wrapped_count& t, int x) { t.value = x; }, [](const wrapped_count& t) { return t.value; }>;
};
struct manual_count
{
   int value{};
};
template <>
struct glz::meta<manual_count>
{
   static constexpr bool custom_read = true;
   static constexpr bool custom_write = true;
};
template <>
struct glz::from<glz::KDL, manual_count>
{
   template <auto Opts>
   static void op(auto& target, auto& ctx, auto& it, auto end)
   {
      int count{};
      from<KDL, int>::template op<Opts>(count, ctx, it, end);
      if (!bool(ctx.error)) target.value = count * 3;
   }
};
template <>
struct glz::to<glz::KDL, manual_count>
{
   template <auto Opts>
   static void op(const auto& source, auto& ctx, auto& buffer, auto& ix)
   {
      to<KDL, int>::template op<Opts>(source.value, ctx, buffer, ix);
   }
};
struct wrapper_config
{
   wrapped_count wrapped{};
   manual_count manual{};
   glz::kdl::mixed<std::tuple<wrapped_count>, std::map<std::string, manual_count>> mixed{};
   std::vector<wrapped_count> sequence{};
};
struct document_wrapper
{
   custom_leaf data{};
};
template <>
struct glz::meta<document_wrapper>
{
   static constexpr auto value = &document_wrapper::data;
};
struct counted_getter
{
   int calls{};
   int data{};
   int get()
   {
      ++calls;
      return data;
   }
   void set(int v) { data = v; }
};
template <>
struct glz::meta<counted_getter>
{
   static constexpr auto value = object("data", custom<&counted_getter::set, &counted_getter::get>);
};

suite custom_tests = [] {
   "member callbacks and objects"_test = [] {
      custom_config c{};
      expect(!glz::read_kdl(c, "count 5; name value; inner value=7; nullable 8; trigger 1; ignored 99"));
      expect(c.count == 10 && c.name == "prefix:value" && c.inner.value == 7 && c.nullable == 8 && c.calls == 1);
      expect(!glz::read_kdl(c, "trigger { anything 1 }; nullable #null"));
      expect(c.calls == 2 && !c.nullable);
      auto text = glz::write_kdl(c);
      expect(text && text->find("count 11") != std::string::npos && text->find("nullable") == std::string::npos);
      if (text) {
         custom_config other{};
         expect(!glz::read_kdl(other, *text));
         expect(other.count == 22 && other.inner == c.inner);
      }
      expect(glz::read_kdl(c, "count invalid") == glz::error_code::syntax_error);
      const std::string input = "name invalid";
      const auto ec = glz::read_kdl(c, input);
      expect(ec == glz::error_code::constraint_violated);
      expect(glz::format_error(ec, input).find("bad name") != std::string::npos);
      expect(bool(glz::read_kdl(c, "trigger { invalid \\")));
      glz::kdl::mixed<std::tuple<>, counted_getter> property_custom{};
      std::map<std::string, decltype(property_custom)> property_root;
      expect(!glz::read_kdl(property_root, "node data=6"));
      expect(property_root.at("node").properties.data == 6);
      auto property_text = glz::write_kdl(property_root);
      expect(property_text && property_text->find("data=6") != std::string::npos);
      expect(property_root.at("node").properties.calls == 1);
   };
   "whole metadata wrappers and low level custom hooks"_test = [] {
      wrapper_config c{};
      expect(!glz::read_kdl(c, "wrapped 4; manual (integer)5; mixed 6 item=7; sequence { - 8; - 9 }"));
      expect(c.wrapped.value == 4 && c.manual.value == 15 && std::get<0>(c.mixed.arguments).value == 6);
      expect(c.mixed.properties.at("item").value == 21);
      expect(c.sequence.size() == 2 && c.sequence[1].value == 9);
      auto text = glz::write_kdl(c);
      expect(bool(text));
      if (text) {
         wrapper_config other{};
         expect(!glz::read_kdl(other, *text));
         expect(other.wrapped.value == 4 && other.manual.value == 45 && other.mixed.properties.at("item").value == 63);
      }
      static_assert(!glz::read_supported<wrapped_count, glz::KDL>);
      static_assert(!glz::write_supported<int, glz::KDL>);
      document_wrapper doc{};
      expect(!glz::read_kdl(doc, "value 10"));
      expect(doc.data.value == 10 && glz::write_kdl(doc).value_or("") == "value 10\n");
      counted_getter counted{0, 12};
      auto count_text = glz::write_kdl(counted);
      expect(count_text && *count_text == "data 12\n" && counted.calls == 1);
   };
};
int main() {}
