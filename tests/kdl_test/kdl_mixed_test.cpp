// Glaze Library
// For the license information refer to glaze.hpp

#include <map>

#include "glaze/core/std_error_code.hpp"
#include "glaze/kdl.hpp"
#include "ut/ut.hpp"

using namespace ut;

struct server_properties
{
   int port{};
   bool enabled{};
   bool operator==(const server_properties&) const = default;
};
struct typed_children
{
   std::string path{};
   bool operator==(const typed_children&) const = default;
};
using compound_server = glz::kdl::mixed<std::tuple<std::string, int>, server_properties, typed_children>;
struct compound_config
{
   compound_server server{};
};
struct hybrid_config
{
   int version{};
   compound_server server{};
   glz::kdl::node plugin{};
   glz::kdl::document extensions{};
   glz::kdl::value setting{};
   std::unique_ptr<glz::kdl::node> optional{};
};
struct circle
{
   double radius{};
};
struct rectangle
{
   int width{};
   int height{};
};
using shape = std::variant<circle, rectangle>;
template <>
struct glz::meta<shape>
{
   static constexpr auto ids = std::array{"circle", "rectangle"};
};
using property_shape = std::variant<rectangle, circle>;
template <>
struct glz::meta<property_shape>
{
   static constexpr auto tag = "type";
   static constexpr auto ids = std::array{"rectangle", "circle"};
};
struct polymorphic_config
{
   shape object{};
   property_shape property{};
   std::unique_ptr<shape> pointer{};
   std::vector<shape> objects{};
};
struct positions
{
   std::tuple<int, std::string, bool> tuple{};
   std::pair<double, int> pair{};
   std::unique_ptr<std::pair<int, int>> pointer{};
};
using scalar_choice = std::variant<int, std::string>;
template <>
struct glz::meta<scalar_choice>
{
   static constexpr auto ids = std::array{"integer", "text"};
};
struct scalar_variants
{
   glz::kdl::mixed<std::tuple<scalar_choice>, std::map<std::string, scalar_choice>> entry{};
   std::vector<scalar_choice> list{};
};
struct required_options : glz::opts
{
   constexpr required_options() : opts{.format = glz::KDL, .error_on_missing_keys = true} {}
};
struct tab_options : glz::opts
{
   char indentation_char = '\t';
   uint8_t indentation_width = 1;
   constexpr tab_options() : opts{.format = glz::KDL} {}
};
struct wide_options : glz::opts
{
   uint8_t indentation_width = 4;
   constexpr wide_options() : opts{.format = glz::KDL} {}
};

suite mixed_tests = [] {
   "typed compound and dynamic members"_test = [] {
      constexpr std::string_view text = R"kdl(
         version 2
         (service)server production 4 port=8080 enabled=#true { path "/srv" }
         (custom)plugin 1 name=(id)demo { child 2 }
         extensions { arbitrary #true; repeated 1; repeated 2 }
         setting (custom)"value"
         (maybe)optional arg { nested #null }
      )kdl";
      hybrid_config c{};
      expect(!glz::read_kdl(c, text));
      expect(c.version == 2 && c.server.tag == "service");
      expect(c.server.arguments == std::tuple{std::string{"production"}, 4});
      expect(c.server.properties == server_properties{8080, true} && c.server.children.path == "/srv");
      expect(c.plugin.name == "plugin" && c.plugin.tag == "custom" && c.plugin.arguments.size() == 1);
      expect(c.extensions.size() == 3 && c.setting.tag == "custom");
      expect(c.optional && c.optional->name == "optional" && c.optional->tag == "maybe");
      auto out = glz::write_kdl(c);
      expect(bool(out));
      if (out) {
         auto result = glz::read_kdl<hybrid_config>(*out);
         expect(bool(result));
         if (result) {
            expect(result->server == c.server && result->plugin == c.plugin && result->extensions == c.extensions &&
                   result->setting == c.setting && result->optional && *result->optional == *c.optional);
         }
      }
      expect(!glz::read_kdl(c, "optional #null"));
      expect(!c.optional);
      expect(glz::read_kdl(c, "extensions 1") == glz::error_code::syntax_error);
   };

   "all dynamic compound and standalone node"_test = [] {
      std::map<std::string, glz::kdl::mixed<>> root;
      expect(!glz::read_kdl(root, "(tag)n 1 2 key=first key=second { child value }"));
      expect(root["n"].arguments.size() == 2 && root["n"].properties.size() == 1 && root["n"].children.size() == 1);
      expect(std::get<std::string>(root["n"].properties[0].second.data) == "second");
      auto out = glz::write_kdl(root);
      expect(out && bool(glz::read_kdl<decltype(root)>(*out)));
      auto node = glz::read_kdl<glz::kdl::node>("(tag)n 1 key=2 { child #true }");
      expect(bool(node));
      if (node) {
         auto serialized = glz::write_kdl(*node);
         auto parsed = serialized ? glz::read_kdl<glz::kdl::node>(*serialized) : decltype(node){};
         expect(parsed && *parsed == *node);
      }
      expect(!glz::read_kdl<glz::kdl::node>(""));
      expect(!glz::read_kdl<glz::kdl::node>("a; b"));
   };

   "tuple pair and smart pointers"_test = [] {
      positions p{};
      expect(!glz::read_kdl(p, "tuple 1 one #true; pair 2.5 3; pointer 4 5"));
      expect(p.tuple == std::tuple{1, std::string{"one"}, true} && p.pair == std::pair{2.5, 3});
      expect(p.pointer && *p.pointer == std::pair{4, 5});
      auto text = glz::write_kdl(p);
      expect(bool(text));
      if (text) {
         auto result = glz::read_kdl<positions>(*text);
         expect(result && result->tuple == p.tuple && result->pair == p.pair && result->pointer &&
                *result->pointer == *p.pointer);
      }
      expect(!glz::read_kdl(p, "pointer #null"));
      expect(!p.pointer);
      expect(glz::read_kdl(p, "tuple 1 one") == glz::error_code::exceeded_static_array_size);
      expect(glz::read_kdl(p, "pair 1 2 3") == glz::error_code::exceeded_static_array_size);
   };

   "tagged and property variants"_test = [] {
      polymorphic_config c{};
      expect(!glz::read_kdl(c, R"(
         (rectangle)object width=4 height=5
         property type=circle radius=2.5
         (circle)pointer radius=1.5
         objects { (circle)- radius=1; (rectangle)- width=2 height=3 }
      )"));
      expect(std::holds_alternative<rectangle>(c.object) && std::get<rectangle>(c.object).width == 4);
      expect(std::holds_alternative<circle>(c.property) && std::get<circle>(c.property).radius == 2.5);
      expect(c.pointer && std::get<circle>(*c.pointer).radius == 1.5 && c.objects.size() == 2);
      auto out = glz::write_kdl(c);
      expect(out && out->find("property type=\"circle\"") != std::string::npos);
      if (out) {
         auto result = glz::read_kdl<polymorphic_config>(*out);
         expect(result && std::get<rectangle>(result->object).height == 5 && result->objects.size() == 2);
      }
      // Discriminator may appear last; comments and duplicate properties obey grammar.
      expect(!glz::read_kdl(c, "object radius=3 type=wrong /- type=ignored type=circle"));
      expect(std::get<circle>(c.object).radius == 3);
      expect(!glz::read_kdl(c, "object radius=4 type=42 type=circle"));
      expect(std::get<circle>(c.object).radius == 4);
      expect(!glz::read_kdl(c, "pointer #null"));
      expect(!c.pointer);
      for (auto input : {"object type=unknown", "(circle)object type=rectangle radius=1", "object radius=1"})
         expect(glz::read_kdl(c, input) == glz::error_code::no_matching_variant_type);
      expect(glz::read_kdl(c, "object type=42") == glz::error_code::invalid_variant_object);
      expect(bool(glz::read_kdl(c, "(circle)object radius=bad")));
   };

   "scalar variants and mixed validation"_test = [] {
      scalar_variants c{};
      expect(!glz::read_kdl(c, R"kdl(
         entry (text)"one" count=(integer)2
         list {
         (integer) - 3;
         (text) - four }
      )kdl"));
      expect(std::get<std::string>(std::get<0>(c.entry.arguments)) == "one");
      expect(std::get<int>(c.entry.properties.at("count")) == 2 && c.list.size() == 2);
      auto out = glz::write_kdl(c);
      expect(bool(out));
      if (out) {
         auto result = glz::read_kdl<scalar_variants>(*out);
         expect(result && result->entry == c.entry && result->list == c.list);
      }
      expect(glz::read_kdl(c, "entry (unknown)1") == glz::error_code::no_matching_variant_type);
      compound_config target{};
      expect(glz::read<required_options{}>(target, "server production 2 port=80 { path local }") ==
             glz::error_code::missing_key);
      expect(glz::read<required_options{}>(target, "server production 2 port=80 enabled=#true") ==
             glz::error_code::missing_key);
      expect(glz::read_kdl(target, "server production 2 extra=3") == glz::error_code::unknown_key);
      expect(!glz::read<glz::opts{.format = glz::KDL, .error_on_unknown_keys = false}>(
         target, "server production 2 unknown=3 { unused { nested 1 }; path local }"));
      expect(bool(glz::read<glz::opts{.format = glz::KDL, .error_on_unknown_keys = false}>(
         target, "server production 2 { unused { nested \\")));
      glz::context ctx{};
      std::string input = "server production 2 port=80 enabled=#true { path local }";
      expect(!glz::read<required_options{}>(target, input, ctx));
      expect(ctx.depth == 0);
   };

   "indentation and error styles"_test = [] {
      std::map<std::string, typed_children> value{{"node", {"data"}}};
      auto tabs = glz::write<tab_options{}>(value);
      auto spaces = glz::write<wide_options{}>(value);
      expect(tabs && *tabs == "node {\n\tpath \"data\"\n}\n");
      expect(spaces && *spaces == "node {\n    path \"data\"\n}\n");
      auto doc = glz::read_kdl<glz::kdl::document>(*tabs);
      expect(bool(doc));
      if (doc) expect(glz::write<tab_options{}>(*doc) == tabs);
      positions target{};
      std::string input = "tuple 1 wrong nope";
      const auto ec = glz::read_kdl(target, input);
      expect(bool(ec));
      expect(!glz::format_error(ec, input).empty());
      std::error_code standard = ec.ec;
      expect(standard.message() == glz::format_error(ec.ec));
      auto result = glz::read_kdl<positions>(input);
      expect(!result && result.error().ec == ec.ec && result.error().count == ec.count);
   };
};

int main() {}
