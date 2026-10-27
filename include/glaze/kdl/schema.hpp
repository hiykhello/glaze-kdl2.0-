// Glaze Library
// For the license information refer to glaze.hpp

#pragma once

#include <algorithm>
#include <climits>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <ratio>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "glaze/concepts/container_concepts.hpp"
#include "glaze/core/chrono.hpp"
#include "glaze/core/common.hpp"
#include "glaze/core/context.hpp"
#include "glaze/core/feature_test.hpp"
#include "glaze/core/lazy.hpp"
#include "glaze/core/meta.hpp"
#include "glaze/core/opts.hpp"
#include "glaze/core/raw_string.hpp"
#include "glaze/core/reflect.hpp"
#include "glaze/core/write_wrappers.hpp"
#include "glaze/forward.hpp"
#include "glaze/kdl/common.hpp"
#include "glaze/kdl/types.hpp"
#include "glaze/kdl/write.hpp" // IWYU pragma: keep
#include "glaze/util/expected.hpp"
#include "glaze/util/variant.hpp"

namespace glz::kdl
{
   struct schema_export
   {
      document value;
      // Nonempty when the official vocabulary cannot express a binding exactly.
      // The document then describes a conservative superset of writer output.
      std::vector<std::string> diagnostics;
   };

   namespace detail
   {
      template <class T>
      inline const char schema_identity{};

      template <auto Opts>
      struct schema_builder
      {
         document definitions;
         std::vector<std::pair<const void*, size_t>> registered;
         std::vector<std::string> diagnostics;

         static node rule(std::string name) { return {std::move(name), {}, {}, {}, {}}; }
         static node one(std::string name, value v)
         {
            auto n = rule(std::move(name));
            n.arguments.push_back(std::move(v));
            return n;
         }
         static void count(node& n, std::string key, size_t value)
         {
            n.children.push_back(one(std::move(key), {uint64_t(value), {}}));
         }
         static void flag(node& n, std::string key, bool value)
         {
            n.children.push_back(one(std::move(key), {value, {}}));
         }
         static void description(node& n, std::string text)
         {
            n.properties.push_back({"description", {std::move(text), {}}});
         }
         static std::string query(size_t index) { return "[id=\"t" + std::to_string(index) + "\"]"; }
         template <class T>
         std::string define()
         {
            const auto* identity = &schema_identity<T>;
            for (auto [known, index] : registered)
               if (identity == known) return query(index);
            const auto index = definitions.size();
            registered.emplace_back(identity, index);
            definitions.emplace_back(); // Register before following recursive types.
            auto n = rule("node");
            n.properties.push_back({"id", {"t" + std::to_string(index), {}}});
            body<T>(n);
            definitions[index] = std::move(n);
            return query(index);
         }
         template <class T>
         node reference(std::optional<std::string> name = {})
         {
            auto n = rule("node");
            if (name) n.arguments.push_back({std::move(*name), {}});
            n.properties.push_back({"ref", {define<T>(), {}}});
            return n;
         }
         template <class T>
         void limitation(node& n, std::string_view reason)
         {
            auto message = std::string{name_v<T>} + ": " + std::string{reason};
            diagnostics.push_back(message);
            if (n.name == "info")
               n.children.push_back(one("description", {std::move(message), {}}));
            else
               description(n, std::move(message));
         }
         void any_body(node& n)
         {
            auto values = rule("value");
            count(values, "min", 0);
            n.children.push_back(std::move(values));
            n.children.push_back(rule("prop")); // Unnamed property applies to every key.
            auto children = rule("children");
            flag(children, "other-tags-allowed", true);
            children.children.push_back(reference<node>());
            n.children.push_back(std::move(children));
         }
         template <class T>
         static constexpr bool scalar_alternatives = [] {
            if constexpr (is_variant<T>) {
               return []<size_t... I>(std::index_sequence<I...>) {
                  return ((!compound<std::variant_alternative_t<I, T>>) && ...);
               }(std::make_index_sequence<std::variant_size_v<T>>{});
            }
            else
               return false;
         }();

         template <class T>
         void scalar_union(node& out)
         {
            std::vector<node> alternatives;
            for_each<std::variant_size_v<T>>([&]<size_t I>() {
               auto n = rule("value");
               scalar<std::variant_alternative_t<I, T>>(n);
               alternatives.push_back(std::move(n));
            });
            auto types = rule("type");
            bool unrestricted{};
            bool widened{};
            const bool all_enumerated = std::all_of(alternatives.begin(), alternatives.end(), [](const node& n) {
               return std::any_of(n.children.begin(), n.children.end(), [](const node& c) { return c.name == "enum"; });
            });
            auto choices = rule("enum");
            for (const auto& alternative : alternatives) {
               const auto t = std::find_if(alternative.children.begin(), alternative.children.end(),
                                           [](const node& n) { return n.name == "type"; });
               if (t == alternative.children.end())
                  unrestricted = true;
               else
                  for (const auto& v : t->arguments)
                     if (std::find(types.arguments.begin(), types.arguments.end(), v) == types.arguments.end())
                        types.arguments.push_back(v);
               for (const auto& constraint : alternative.children) {
                  if (constraint.name == "type") continue;
                  if (constraint.name == "enum" && all_enumerated) {
                     for (const auto& v : constraint.arguments)
                        if (std::find(choices.arguments.begin(), choices.arguments.end(), v) == choices.arguments.end())
                           choices.arguments.push_back(v);
                     continue;
                  }
                  const bool common = std::all_of(alternatives.begin(), alternatives.end(), [&](const node& a) {
                     return std::find(a.children.begin(), a.children.end(), constraint) != a.children.end();
                  });
                  if (common) {
                     if (std::find(out.children.begin(), out.children.end(), constraint) == out.children.end())
                        out.children.push_back(constraint);
                  }
                  else
                     widened = true;
               }
            }
            if (!unrestricted) out.children.push_back(std::move(types));
            if (all_enumerated) out.children.push_back(std::move(choices));
            if (widened)
               limitation<T>(out, "scalar variant type union omits alternative-specific format or enum correlations");
         }
         template <class T>
         void scalar(node& out)
         {
            auto type = [&](std::string text) { out.children.push_back(one("type", {std::move(text), {}})); };
            if constexpr (transparent_write_wrapper<T>)
               scalar<serialized_type_t<T>>(out);
            else if constexpr (is_specialization_v<T, raw_string_t> || is_specialization_v<T, escaped_t>)
               scalar<std::remove_cvref_t<decltype(std::declval<T>().val)>>(out);
            else if constexpr (nullable_like<T>) {
               scalar<std::remove_cvref_t<decltype(*std::declval<T>())>>(out);
               for (auto& n : out.children) {
                  if (n.name == "type" || n.name == "enum")
                     n.arguments.push_back(n.name == "type" ? value{std::string{"null"}, {}} : value{});
               }
            }
            else if constexpr (custom_write<T>)
               limitation<T>(out, "custom serializer constraints are unknown");
            else if constexpr (std::same_as<T, value>) {
            } // Any native scalar, including tags.
            else if constexpr (scalar_alternatives<T>)
               scalar_union<T>(out);
            else if constexpr (is_variant<T>)
               limitation<T>(out, "scalar variant contains compound alternatives");
            else if constexpr (glaze_enum_t<T>) {
               type("string");
               auto choices = rule("enum");
               for_each<reflect<T>::size>(
                  [&]<size_t I>() { choices.arguments.push_back({std::string{get<I>(reflect<T>::keys)}, {}}); });
               out.children.push_back(std::move(choices));
            }
#if GLZ_REFLECTION26
            else if constexpr (std::is_enum_v<T> && (check_reflect_enums(Opts) || is_reflect_enum<T>)) {
               type("string");
               auto choices = rule("enum");
               constexpr std::meta::info items = std::meta::reflect_constant_array(std::meta::enumerators_of(^^T));
               template for (constexpr std::meta::info item : [:items:])
               {
                  choices.arguments.push_back({std::string{std::meta::identifier_of(item)}, {}});
               }
               out.children.push_back(std::move(choices));
            }
#endif
            else if constexpr (std::is_enum_v<T>)
               scalar<std::underlying_type_t<T>>(out);
            else if constexpr (std::same_as<T, bool>)
               type("boolean");
            else if constexpr (std::is_arithmetic_v<T>) {
               type("number");
               std::string format = std::floating_point<T> ? "f" : std::is_signed_v<T> ? "i" : "u";
               format += std::to_string((std::floating_point<T> ? (std::min)(sizeof(T), sizeof(double)) : sizeof(T)) *
                                        CHAR_BIT);
               out.children.push_back(one("format", {std::move(format), {}}));
            }
            else if constexpr (is_duration<T>)
               scalar<typename T::rep>(out);
            else if constexpr (is_count_time_point<T>)
               scalar<typename T::duration::rep>(out);
            else if constexpr (is_specialization_v<T, epoch_time>) {
               []<class D>(schema_builder& self, node& n, epoch_time<D>*) { self.template scalar<typename D::rep>(n); }(
                  *this, out, static_cast<T*>(nullptr));
            }
            else if constexpr (always_null_t<T>)
               type("null");
            else if constexpr (is_calendar_time_point<T> || is_year_month_day<T>) {
               type("string");
               constexpr bool date_only = [] {
                  if constexpr (is_year_month_day<T>)
                     return true;
                  else
                     return std::ratio_equal_v<typename T::duration::period, std::ratio<86400>>;
               }();
               out.children.push_back(one("format", {std::string{date_only ? "date" : "date-time"}, {}}));
            }
            else if constexpr (std::same_as<T, std::string> || std::same_as<T, std::string_view> || filesystem_path<T>)
               type("string");
            else
               limitation<T>(out, "scalar serialization constraints are unknown");
         }
         template <class T>
         node values(size_t minimum = 1, std::optional<size_t> maximum = 1)
         {
            auto n = rule("value");
            count(n, "min", minimum);
            if (maximum) count(n, "max", *maximum);
            scalar<T>(n);
            return n;
         }
         template <class T>
         static constexpr std::optional<size_t> extent = []() -> std::optional<size_t> {
            if constexpr (std::is_bounded_array_v<T>)
               return std::extent_v<T>;
            else if constexpr (requires { std::tuple_size<T>::value; })
               return std::tuple_size_v<T>;
            else
               return {};
         }();

         template <class T>
         void fields(node& out)
         {
            if constexpr (string_map<T>)
               out.children.push_back(reference<typename T::mapped_type>());
            else {
               for_each<reflect<T>::size>([&]<size_t I>() {
                  if constexpr (!skipped_on_write<Opts, T, I>) {
                     using M = std::remove_cvref_t<decltype(member<I>(std::declval<T&>()))>;
                     auto field = reference<M>(std::string{get<I>(reflect<T>::keys)});
                     constexpr bool optional = Opts.skip_null_members && (nullable_like<serialized_type_t<M>> ||
                                                                          always_null_t<serialized_type_t<M>>);
                     count(field, "min", optional ? 0 : 1);
                     count(field, "max", 1);
                     out.children.push_back(std::move(field));
                  }
               });
            }
         }
         template <class T>
         void arguments(node& out)
         {
            if constexpr (positional<T>) {
               for_each<position_count<T>>([&]<size_t I>() {
                  out.children.push_back(values<std::remove_cvref_t<decltype(position<I>(std::declval<T&>()))>>());
               });
            }
            else if constexpr (sequence<T>)
               out.children.push_back(values<std::ranges::range_value_t<T>>(extent<T>.value_or(0), extent<T>));
            else
               limitation<T>(out, "argument section has an unsupported binding");
         }
         template <class T>
         void properties(node& out)
         {
            auto prop = [&]<class M>(std::optional<std::string> key, bool required) {
               auto n = rule("prop");
               if (key) n.arguments.push_back({std::move(*key), {}});
               flag(n, "required", required);
               scalar<M>(n);
               out.children.push_back(std::move(n));
            };
            if constexpr (std::same_as<T, kdl::properties>)
               prop.template operator()<value>({}, false);
            else if constexpr (string_map<T>)
               prop.template operator()<typename T::mapped_type>({}, false);
            else if constexpr (object<T>) {
               for_each<reflect<T>::size>([&]<size_t I>() {
                  if constexpr (!skipped_on_write<Opts, T, I>) {
                     using M = std::remove_cvref_t<decltype(member<I>(std::declval<T&>()))>;
                     constexpr bool optional = Opts.skip_null_members && (nullable_like<serialized_type_t<M>> ||
                                                                          always_null_t<serialized_type_t<M>>);
                     prop.template operator()<M>(std::string{get<I>(reflect<T>::keys)}, !optional);
                  }
               });
            }
            else
               limitation<T>(out, "property section has an unsupported binding");
         }
         template <class T>
         void children(node& out)
         {
            auto n = rule("children");
            flag(n, "other-tags-allowed", true);
            if constexpr (std::same_as<T, document>)
               n.children.push_back(reference<node>());
            else if constexpr (object<T> || string_map<T>)
               fields<T>(n);
            else if constexpr (sequence<T>) {
               auto element = reference<std::ranges::range_value_t<T>>(std::string{"-"});
               count(element, "min", extent<T>.value_or(0));
               if (extent<T>) count(element, "max", *extent<T>);
               n.children.push_back(std::move(element));
            }
            else
               limitation<T>(n, "children section has an unsupported binding");
            out.children.push_back(std::move(n));
         }
         template <class T>
         void body(node& out)
         {
            if constexpr (transparent_write_wrapper<T>)
               body<serialized_type_t<T>>(out);
            else if constexpr (is_specialization_v<T, raw_string_t> || is_specialization_v<T, escaped_t>)
               body<std::remove_cvref_t<decltype(std::declval<T>().val)>>(out);
            else if constexpr (custom_write<T>) {
               limitation<T>(out, "custom serializer node constraints are unknown");
               any_body(out);
            }
            else if constexpr (is_specialization_v<T, lazy>)
               body<typename T::value_type>(out);
            else if constexpr (nullable_like<T>) {
               using U = std::remove_cvref_t<decltype(*std::declval<T>())>;
               if constexpr (!compound<U>)
                  out.children.push_back(values<T>());
               else {
                  limitation<T>(out, "null-or-compound node alternatives are not expressible");
                  any_body(out);
               }
            }
            else if constexpr (scalar_alternatives<T> && !requires { meta<T>::tag; })
               out.children.push_back(values<T>());
            else if constexpr (is_variant<T>) {
               limitation<T>(out, "variant body/discriminator correlations are not expressible");
               any_body(out);
            }
            else if constexpr (std::same_as<T, node>)
               any_body(out);
            else if constexpr (std::same_as<T, document>)
               children<T>(out);
            else if constexpr (is_mixed<T>) {
               arguments<decltype(T::arguments)>(out);
               properties<decltype(T::properties)>(out);
               children<decltype(T::children)>(out);
            }
            else if constexpr (positional<T>)
               arguments<T>(out);
            else if constexpr (!scalar_type<T> && (object<T> || string_map<T>))
               children<T>(out);
            else if constexpr (sequence<T>) {
               if constexpr (compound<std::ranges::range_value_t<T>>)
                  children<T>(out);
               else
                  arguments<T>(out);
            }
            else
               out.children.push_back(values<T>());
         }
         template <class T>
         void root(node& out)
         {
            if constexpr (transparent_write_wrapper<T>)
               root<serialized_type_t<T>>(out);
            else if constexpr (std::same_as<T, document> || std::same_as<T, node>) {
               flag(out, "other-tags-allowed", true);
               auto n = reference<node>();
               if constexpr (std::same_as<T, node>) {
                  count(n, "min", 1);
                  count(n, "max", 1);
               }
               out.children.push_back(std::move(n));
            }
            else if constexpr (custom_write<T>) {
               limitation<T>(out.children.front(), "custom document serializer constraints are unknown");
               flag(out, "other-tags-allowed", true);
               out.children.push_back(reference<node>());
            }
            else if constexpr (!scalar_type<T> && (object<T> || string_map<T>)) {
               flag(out, "other-tags-allowed", true);
               fields<T>(out);
            }
            else
               limitation<T>(out.children.front(), "KDL requires a document-shaped root");
         }
      };
   }

   // Official KDL Schema 1.0 vocabulary. Describes canonical write_kdl output,
   // rather than every alternate spelling/layout accepted by the typed reader.
   // Inspect diagnostics before using a conservative approximation for validation.
   template <class T, auto Opts = opts{.format = KDL}>
   [[nodiscard]] schema_export export_schema()
   {
      detail::schema_builder<Opts> builder;
      auto root = detail::schema_builder<Opts>::rule("document");
      root.children.push_back({"info", {}, {}, {}, {{"title", {}, {{std::string{name_v<T>}, {}}}, {}, {}}}});
      builder.template root<T>(root);
      if (!builder.definitions.empty())
         root.children.push_back({"definitions", {}, {}, {}, std::move(builder.definitions)});
      return {{std::move(root)}, std::move(builder.diagnostics)};
   }

   template <class T, auto Opts = opts{.format = KDL}>
   [[nodiscard]] expected<document, error_ctx> schema()
   {
      auto result = export_schema<T, Opts>();
      if (!result.diagnostics.empty())
         return unexpected(
            error_ctx{0, error_code::feature_not_supported,
                      "KDL Schema cannot express this binding exactly; inspect export_schema<T>().diagnostics"});
      return std::move(result.value);
   }

   template <class T, auto Opts = opts{.format = KDL}>
   [[nodiscard]] expected<std::string, error_ctx> write_schema()
   {
      auto result = schema<T, Opts>();
      if (!result) return unexpected(result.error());
      return write<set_kdl<Opts>()>(*result);
   }
}
