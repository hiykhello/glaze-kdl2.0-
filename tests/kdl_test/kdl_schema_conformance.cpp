// Glaze Library
// For the license information refer to glaze.hpp

// Test-only interpreter for the official metaschema vocabulary exercised by the
// exporter. It loads the upstream schema, including its definitions/references,
// rather than validating an export only against an exporter-owned model.
#include <fstream>
#include <iostream>
#include <map>

#include "glaze/kdl.hpp"

namespace schema_test
{
   using namespace glz::kdl;
   struct validator
   {
      std::map<std::string, const node*> ids;
      bool failed{};
      void collect(const document& nodes)
      {
         for (const auto& n : nodes) {
            for (const auto& [key, v] : n.properties)
               if (key == "id") ids.emplace(std::get<std::string>(v.data), &n);
            collect(n.children);
         }
      }
      node resolve(const node& n, size_t depth = 0)
      {
         if (depth > 256) {
            failed = true;
            return {};
         }
         auto result = n;
         for (const auto& [key, v] : n.properties)
            if (key == "ref") {
               const auto& ref = std::get<std::string>(v.data);
               if (!ref.starts_with("[id=\"") || !ref.ends_with("\"]")) {
                  failed = true;
                  return {};
               }
               auto found = ids.find(ref.substr(5, ref.size() - 7));
               if (found == ids.end()) {
                  failed = true;
                  return {};
               }
               auto base = resolve(*found->second, depth + 1);
               for (const auto& child : n.children) {
                  if (child.name == "min" || child.name == "max")
                     std::erase_if(base.children, [&](const auto& old) { return old.name == child.name; });
                  base.children.push_back(child);
               }
               if (result.arguments.empty()) result.arguments = base.arguments;
               result.children = std::move(base.children);
            }
         return result;
      }
      static size_t count(const node& n, std::string_view name, size_t fallback)
      {
         for (const auto& c : n.children)
            if (c.name == name && !c.arguments.empty())
               return std::visit(
                  [&](const auto& x) -> size_t {
                     if constexpr (std::integral<std::remove_cvref_t<decltype(x)>>)
                        return size_t(x);
                     else
                        return fallback;
                  },
                  c.arguments[0].data);
         return fallback;
      }
      static bool flag(const node& n, std::string_view name)
      {
         for (const auto& c : n.children)
            if (c.name == name && !c.arguments.empty()) {
               if (auto b = std::get_if<bool>(&c.arguments[0].data)) return *b;
            }
         return false;
      }
      static bool numeric(const value& v)
      {
         return std::holds_alternative<int64_t>(v.data) || std::holds_alternative<uint64_t>(v.data) ||
                std::holds_alternative<double>(v.data) || std::holds_alternative<number>(v.data);
      }
      bool scalar(const value& v, const node& specification)
      {
         auto rule = resolve(specification);
         for (const auto& constraint : rule.children) {
            if (constraint.name == "type") {
               bool matches{};
               for (const auto& type : constraint.arguments) {
                  const auto& name = std::get<std::string>(type.data);
                  matches |= (name == "string" && std::holds_alternative<std::string>(v.data)) ||
                             (name == "boolean" && std::holds_alternative<bool>(v.data)) ||
                             (name == "null" && std::holds_alternative<std::monostate>(v.data)) ||
                             (name == "number" && numeric(v));
               }
               if (!matches) return false;
            }
            if (constraint.name == "enum" &&
                std::find(constraint.arguments.begin(), constraint.arguments.end(), v) == constraint.arguments.end())
               return false;
         }
         return !failed;
      }
      bool body(const node& n, const node& specification)
      {
         auto rule = resolve(specification);
         size_t index{};
         document child_rules;
         for (const auto& c : rule.children) {
            if (c.name == "value") {
               const auto v = resolve(c);
               const auto available = n.arguments.size() - index;
               const auto max = count(v, "max", available);
               const auto size = std::min(available, max);
               if (size < count(v, "min", 0)) return false;
               for (size_t j = 0; j < size; ++j)
                  if (!scalar(n.arguments[index++], v)) return false;
            }
            if (c.name == "children") {
               auto children = resolve(c);
               child_rules.insert(child_rules.end(), children.children.begin(), children.children.end());
            }
            if (c.name == "prop") {
               auto p = resolve(c);
               if (!p.arguments.empty() && flag(p, "required")) {
                  const auto& key = std::get<std::string>(p.arguments[0].data);
                  if (std::none_of(n.properties.begin(), n.properties.end(),
                                   [&](const auto& pair) { return pair.first == key; }))
                     return false;
               }
            }
         }
         if (index != n.arguments.size()) return false;
         for (const auto& [key, v] : n.properties) {
            bool matched{};
            for (const auto& c : rule.children)
               if (c.name == "prop") {
                  const auto p = resolve(c);
                  if (p.arguments.empty() || std::get<std::string>(p.arguments[0].data) == key) {
                     matched = true;
                     if (!scalar(v, p)) return false;
                  }
               }
            if (!matched && !flag(rule, "other-props-allowed")) return false;
         }
         return nodes(n.children, child_rules);
      }
      bool nodes(const document& input, const document& specifications)
      {
         node scope{"children", {}, {}, {}, specifications};
         for (const auto& spec : specifications)
            if (spec.name == "node") {
               auto rule = resolve(spec);
               const auto occurrences = std::count_if(input.begin(), input.end(), [&](const auto& n) {
                  return rule.arguments.empty() || std::get<std::string>(rule.arguments[0].data) == n.name;
               });
               if (size_t(occurrences) < count(rule, "min", 0) || size_t(occurrences) > count(rule, "max", SIZE_MAX))
                  return false;
            }
         for (const auto& n : input) {
            bool matched{};
            for (const auto& spec : specifications)
               if (spec.name == "node") {
                  auto rule = resolve(spec);
                  if (rule.arguments.empty() || std::get<std::string>(rule.arguments[0].data) == n.name) {
                     matched = true;
                     if (!body(n, spec)) {
                        std::cerr << "invalid schema node: " << n.name << '\n';
                        return false;
                     }
                  }
               }
            if (!matched && !flag(scope, "other-nodes-allowed")) return false;
         }
         return !failed;
      }
   };
   enum class mode { first, second };
   struct recursive
   {
      int value{};
      std::vector<recursive> children;
   };
   struct props
   {
      bool enabled{};
      std::optional<int> port;
   };
   struct config
   {
      recursive tree;
      glz::kdl::mixed<std::tuple<int, std::string>, props, document> mixed;
      std::array<int, 2> fixed{};
      std::map<std::string, std::string> map;
      std::optional<mode> nullable;
      std::chrono::milliseconds timeout{};
      std::variant<bool, std::string, std::monostate> choice;
   };
}

template <>
struct glz::meta<schema_test::mode>
{
   static constexpr auto value = enumerate(schema_test::mode::first, schema_test::mode::second);
};

int main(int argc, char** argv)
{
   using namespace schema_test;
   if (argc != 2) return 2;
   std::ifstream input{argv[1], std::ios::binary};
   if (!input) return 2;
   const std::string source{std::istreambuf_iterator<char>{input}, {}};
   auto meta = glz::read_kdl<glz::kdl::document>(source);
   if (!meta || meta->size() != 1) return 2;
   validator validate;
   validate.collect(*meta);
   auto generated = glz::kdl::schema<config>();
   if (!generated || !validate.nodes(*generated, (*meta)[0].children)) return 1;
   // Negative controls ensure this checks rules, not merely successful KDL parsing.
   auto bad = *generated;
   bad[0].name = "schema";
   if (validate.nodes(bad, (*meta)[0].children)) return 1;
   bad = *generated;
   bad[0].properties.push_back({"cpp", {std::string{"private-dialect"}, {}}});
   if (validate.nodes(bad, (*meta)[0].children)) return 1;
   // Also validate real canonical writer output against the exported rules.
   config instance{};
   instance.tree.value = 12;
   instance.nullable = mode::second;
   instance.map["key"] = "value";
   auto wire = glz::write_kdl(instance);
   if (!wire) return 1;
   auto parsed = glz::read_kdl<glz::kdl::document>(*wire);
   validator instance_validator;
   instance_validator.collect(*generated);
   if (!parsed || !instance_validator.nodes(*parsed, (*generated)[0].children)) return 1;
   for (const std::variant<bool, std::string, std::monostate>& choice :
        {decltype(instance.choice){true}, decltype(instance.choice){std::string{"text"}},
         decltype(instance.choice){std::monostate{}}}) {
      instance.choice = choice;
      auto text = glz::write_kdl(instance);
      if (!text) return 1;
      auto data = glz::read_kdl<glz::kdl::document>(*text);
      if (!data || !instance_validator.nodes(*data, (*generated)[0].children)) return 1;
   }
   auto invalid = *parsed;
   invalid.back().arguments[0].data = int64_t{42};
   if (instance_validator.nodes(invalid, (*generated)[0].children)) return 1;
   invalid = *parsed;
   invalid.push_back({"unknown-field", {}, {}, {}, {}});
   if (instance_validator.nodes(invalid, (*generated)[0].children)) return 1;
   invalid = *parsed;
   invalid[0].children[0].arguments[0].data = std::string{"wrong numeric type"};
   if (instance_validator.nodes(invalid, (*generated)[0].children)) return 1;
   std::cout << "Official KDL metaschema and canonical output validation passed\n";
}
