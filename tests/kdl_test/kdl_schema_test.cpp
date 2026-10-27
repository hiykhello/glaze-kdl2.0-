// Glaze Library
// For the license information refer to glaze.hpp
#include <map>

#include "glaze/kdl.hpp"
#include "ut/ut.hpp"
using namespace ut;
enum class schema_mode { fast, slow };
template <>
struct glz::meta<schema_mode>
{
   using enum schema_mode;
   static constexpr auto value = enumerate(fast, slow);
};
struct schema_recursive
{
   int value{};
   std::vector<schema_recursive> children;
};
struct schema_props
{
   bool enabled{};
   std::optional<int> port;
};
struct schema_config
{
   schema_recursive recursive;
   glz::kdl::mixed<std::tuple<int, std::string>, schema_props, glz::kdl::document> mixed;
   schema_mode mode{};
   glz::lazy<int> lazy;
   std::chrono::milliseconds timeout{};
   std::filesystem::path file;
};
struct schema_inexact
{
   std::optional<schema_recursive> nullable;
   std::variant<int, std::string> choice;
};
struct schema_simple
{
   int port{};
   std::string host;
};

// Check the exported grammar independently of the exporter. Vocabulary and
// cardinalities follow the official SCHEMA-SPEC and examples/kdl-schema.kdl.
void check_schema(const glz::kdl::document& schema)
{
   expect(schema.size() == 1 && schema[0].name == "document");
   if (schema.size() != 1) return;
   using namespace glz::kdl;
   std::map<std::string, const node*> ids;
   std::vector<std::pair<std::string, const node*>> refs;
   const std::vector<std::string_view> validation{"tag",    "type", "enum", "pattern", "min-length", "max-length",
                                                  "format", "%",    ">",    ">=",      "<",          "<="};
   auto walk = [&](auto&& self, const node& n) -> void {
      std::vector<std::string_view> allowed;
      size_t max_args{};
      if (n.name == "document")
         allowed = {"info", "node",      "definitions",       "node-names", "other-nodes-allowed",
                    "tag",  "tag-names", "other-tags-allowed"};
      else if (n.name == "definitions")
         allowed = {"node", "prop", "value", "children", "tag"};
      else if (n.name == "info")
         allowed = {"title", "description"};
      else if (n.name == "node") {
         allowed = {"min", "max", "prop-names", "other-props-allowed", "tag", "prop", "value", "children"};
         max_args = 1;
      }
      else if (n.name == "children")
         allowed = {"node", "node-names", "other-nodes-allowed", "tag", "tag-names", "other-tags-allowed"};
      else if (n.name == "prop" || n.name == "value" || n.name == "tag" || n.name == "node-names" ||
               n.name == "prop-names" || n.name == "tag-names") {
         allowed = validation;
         if (n.name == "prop") {
            allowed.push_back("required");
            max_args = 1;
         }
         if (n.name == "value") {
            allowed.push_back("min");
            allowed.push_back("max");
         }
      }
      else
         max_args = std::numeric_limits<size_t>::max();
      expect(n.arguments.size() <= max_args);
      expect(!n.tag);
      for (const auto& [key, v] : n.properties) {
         expect(key == "id" || key == "ref" || key == "description");
         expect(n.name == "node" || n.name == "prop" || n.name == "value" || n.name == "children" || n.name == "tag");
         const auto* text = std::get_if<std::string>(&v.data);
         expect(text != nullptr);
         if (!text) continue;
         if (key == "id") expect(ids.emplace(*text, &n).second);
         if (key == "ref") refs.emplace_back(*text, &n);
      }
      for (const auto& child : n.children) {
         expect(std::find(allowed.begin(), allowed.end(), child.name) != allowed.end());
         self(self, child);
      }
   };
   walk(walk, schema[0]);
   expect(std::count_if(schema[0].children.begin(), schema[0].children.end(),
                        [](const auto& n) { return n.name == "info"; }) == 1);
   for (const auto& [ref, n] : refs) {
      expect(ref.starts_with("[id=\"") && ref.ends_with("\"]"));
      if (ref.size() < 8) continue;
      auto found = ids.find(ref.substr(5, ref.size() - 7));
      expect(found != ids.end());
      if (found != ids.end()) expect(found->second->name == n->name);
   }
}

struct schema_dates
{
   std::chrono::sys_days day;
   std::chrono::system_clock::time_point instant;
   long double number{};
};

struct arrays
{
   std::array<int, 3> fixed{};
   std::vector<int> dynamic;
};

enum class schema_extra_mode { slow, turbo };
template <>
struct glz::meta<schema_extra_mode>
{
   static constexpr auto value = enumerate(schema_extra_mode::slow, schema_extra_mode::turbo);
};
struct schema_enum_union
{
   std::variant<schema_mode, schema_extra_mode> choice;
};
struct schema_scalar_union
{
   std::variant<bool, std::string, std::monostate> choice;
   std::tuple<std::variant<int, std::string, bool>> arguments;
   std::variant<int, int> same_format;
};
struct schema_exact_union
{
   std::variant<bool, std::string, std::monostate> choice;
};
suite schema_tests = [] {
   "scalar variants merge types and preserve common constraints"_test = [] {
      auto schema = glz::kdl::write_schema<schema_exact_union>();
      expect(bool(schema));
      if (schema) expect(schema->find("type \"boolean\" \"string\" \"null\"") != schema->npos);
      auto enums = glz::kdl::write_schema<schema_enum_union>();
      expect(bool(enums));
      if (enums) expect(enums->find("enum \"fast\" \"slow\" \"turbo\"") != enums->npos);
      auto report = glz::kdl::export_schema<schema_scalar_union>();
      expect(report.diagnostics.size() == 1);
      check_schema(report.value);
      auto text = glz::write_kdl(report.value);
      expect(bool(text));
      if (text) {
         expect(text->find("type \"number\" \"string\" \"boolean\"") != text->npos);
         expect(text->find("format \"i32\"") != text->npos);
      }
   };

   "official vocabulary recursive refs mixed props and scalar rules"_test = [] {
      auto report = glz::kdl::export_schema<schema_config>();
      expect(report.diagnostics.empty());
      check_schema(report.value);
      auto text = glz::kdl::write_schema<schema_config>();
      expect(bool(text));
      if (!text) return;
      expect(text->starts_with("document {"));
      expect(text->find("enum \"fast\" \"slow\"") != text->npos);
      expect(text->find("format \"i32\"") != text->npos);
      expect(text->find("prop \"enabled\"") != text->npos);
      expect(text->find("required #true") != text->npos);
      expect(text->find("type \"number\" \"null\"") != text->npos);
      expect(bool(glz::read_kdl<glz::kdl::document>(*text)));
   };
   "exact stable schema for canonical object output"_test = [] {
      auto actual = glz::kdl::schema<schema_simple>();
      auto expected = glz::read_kdl<glz::kdl::document>(R"(
         document {
            info { title "schema_simple"; }
            other-tags-allowed #true
            node "port" ref="[id=\"t0\"]" { min 1; max 1; }
            node "host" ref="[id=\"t1\"]" { min 1; max 1; }
            definitions {
               node id="t0" { value { min 1; max 1; type number; format i32; } }
               node id="t1" { value { min 1; max 1; type string; } }
            }
         }
      )");
      expect(actual && expected && glz::write_kdl(*actual) == glz::write_kdl(*expected));
      if (actual) check_schema(*actual);
   };
   "unsupported alternatives require explicit approximation"_test = [] {
      auto exact = glz::kdl::schema<schema_inexact>();
      expect(!exact && exact.error().ec == glz::error_code::feature_not_supported);
      auto written = glz::kdl::write_schema<schema_inexact>();
      expect(!written && written.error().ec == glz::error_code::feature_not_supported);
      auto report = glz::kdl::export_schema<schema_inexact>();
      expect(report.diagnostics.size() == 2);
      check_schema(report.value);
      expect(report.diagnostics[0].find("null-or-compound") != std::string::npos);
      expect(report.diagnostics[1].find("variant") != std::string::npos);
   };
   "dates and numeric formats match writer representation"_test = [] {
      auto dates = glz::kdl::write_schema<schema_dates>();
      expect(dates && dates->find("format \"date\"") != dates->npos);
      expect(dates && dates->find("format \"date-time\"") != dates->npos);
      expect(dates && dates->find("format \"f64\"") != dates->npos);
   };
   "dynamic roots and fixed arrays use official rules"_test = [] {
      auto dynamic = glz::kdl::schema<glz::kdl::document>();
      expect(bool(dynamic));
      if (dynamic) check_schema(*dynamic);
      auto fixed = glz::kdl::write_schema<arrays>();
      expect(fixed && fixed->find("min 3") != std::string::npos && fixed->find("max 3") != std::string::npos);
   };
};
int main() {}
