// Glaze Library
// For the license information refer to glaze.hpp

#pragma once

#include <algorithm>
#include <charconv>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "glaze/core/context.hpp"
#include "glaze/kdl/common.hpp"
#include "glaze/kdl/parse.hpp"
#include "glaze/kdl/path.hpp"
#include "glaze/kdl/read.hpp"
#include "glaze/kdl/write.hpp"
#include "glaze/util/expected.hpp"

namespace glz::kdl
{
   namespace detail
   {
      inline std::string_view trim_query(std::string_view s)
      {
         while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
         while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
         return s;
      }
      struct query_predicate
      {
         path_step selector;
         std::string comparison;
         value literal;
         bool negate{};
      };
      struct query_step
      {
         path_step selector;
         bool wildcard{}, recursive{};
         std::optional<ptrdiff_t> index, start, stop;
         ptrdiff_t stride = 1;
         bool sliced{};
         // Disjunction of conjunctions, evaluated with short circuiting.
         std::vector<std::vector<query_predicate>> filter;
      };
      inline expected<path_step, error_ctx> query_selector(std::string_view text)
      {
         auto parsed = parse_path(std::string{"/n/"} + std::string{text});
         if (!parsed || parsed->size() != 2 || parsed->back().kind == path_kind::node)
            return unexpected(error_ctx{0, error_code::invalid_query});
         return parsed->back();
      }
      inline std::optional<value> query_value(const node& n, const path_step& selector)
      {
         if (selector.kind == path_kind::name) return value{n.name, {}};
         if (selector.kind == path_kind::tag) {
            if (n.tag) return value{*n.tag, {}};
         }
         if (selector.kind == path_kind::argument && selector.index < n.arguments.size())
            return n.arguments[selector.index];
         if (selector.kind == path_kind::property)
            for (const auto& [key, v] : n.properties)
               if (key == selector.key) return v;
         return {};
      }
      inline bool compare_query(const value& a, const value& b, std::string_view op)
      {
         // Relational comparisons never coerce strings to numbers.
         std::optional<int> order;
         const auto* sa = std::get_if<std::string>(&a.data);
         const auto* sb = std::get_if<std::string>(&b.data);
         if (sa && sb)
            order = sa->compare(*sb);
         else
            std::visit(
               [&](const auto& x, const auto& y) {
                  using X = std::remove_cvref_t<decltype(x)>;
                  using Y = std::remove_cvref_t<decltype(y)>;
                  if constexpr (std::is_arithmetic_v<X> && std::is_arithmetic_v<Y> && !std::same_as<X, bool> &&
                                !std::same_as<Y, bool>) {
                     if constexpr (std::integral<X> && std::integral<Y>)
                        order = std::cmp_less(x, y) ? -1 : std::cmp_greater(x, y) ? 1 : 0;
                     else {
                        const auto xx = static_cast<long double>(x), yy = static_cast<long double>(y);
                        if (!std::isnan(xx) && !std::isnan(yy)) order = xx < yy ? -1 : xx > yy ? 1 : 0;
                     }
                  }
               },
               a.data, b.data);
         if (op == "==" || op == "!=") {
            const bool equal = a.tag == b.tag && ((order && *order == 0) || a.data == b.data);
            return op == "==" ? equal : !equal;
         }
         if (!order) return false;
         if (op == "<") return *order < 0;
         if (op == "<=") return *order <= 0;
         if (op == ">") return *order > 0;
         return *order >= 0;
      }
      inline bool matches_query(const node& n, const query_step& step)
      {
         if (step.filter.empty()) return true;
         for (const auto& conjunction : step.filter) {
            bool match = true;
            for (const auto& p : conjunction) {
               auto v = query_value(n, p.selector);
               const bool test = v && (p.comparison.empty() || compare_query(*v, p.literal, p.comparison));
               if (test == p.negate) {
                  match = false;
                  break;
               }
            }
            if (match) return true;
         }
         return false;
      }
   }

   struct compiled_query
   {
      std::vector<detail::query_step> steps;
      enum class function { none, count, first, last, reverse };
      std::vector<function> pipeline;
   };
   using query_item = std::variant<const node*, const value*, std::string_view>;
   struct query_result
   {
      std::vector<query_item> items;
      std::optional<uint64_t> count;
   };

   // Native path projections, recursive descent, filters and Python-style slices.
   // This is KDL query syntax inspired by JMESPath, rather than a JSON data model.
   [[nodiscard]] inline expected<compiled_query, error_ctx> compile_query(std::string_view expression)
   {
      compiled_query result;
      expression = detail::trim_query(expression);
      auto invalid = [] { return unexpected(error_ctx{0, error_code::invalid_query}); };
      size_t pos{};
      while (pos < expression.size() && expression[pos] != '|') {
         if (expression[pos++] != '/') return invalid();
         detail::query_step step;
         if (pos < expression.size() && expression[pos] == '/') {
            step.recursive = true;
            ++pos;
         }
         const auto begin = pos;
         bool quoted{}, escaped{};
         size_t brackets{};
         while (pos < expression.size()) {
            const auto c = expression[pos];
            if (escaped)
               escaped = false;
            else if (quoted && c == '\\')
               escaped = true;
            else if (c == '"')
               quoted = !quoted;
            else if (!quoted) {
               if (c == '[')
                  ++brackets;
               else if (c == ']') {
                  if (!brackets) return invalid();
                  --brackets;
               }
               else if ((c == '/' || c == '|') && !brackets)
                  break;
            }
            ++pos;
         }
         if (quoted || brackets) return invalid();
         auto token = detail::trim_query(expression.substr(begin, pos - begin));
         const auto open = token.find('[');
         const auto name = token.substr(0, open);
         step.wildcard = name == "*";
         if (!step.wildcard) {
            auto parsed = parse_path(std::string{"/"} + std::string{name});
            if (!parsed || parsed->size() != 1) return invalid();
            step.selector = parsed->front();
         }
         if (open != token.npos) {
            if (!token.ends_with(']') || step.selector.kind != path_kind::node) return invalid();
            auto suffix = detail::trim_query(token.substr(open + 1, token.size() - open - 2));
            auto number = [](std::string_view s, std::optional<ptrdiff_t>& out) {
               if (s.empty()) return true;
               ptrdiff_t n{};
               const auto r = std::from_chars(s.data(), s.data() + s.size(), n);
               if (r.ec != std::errc{} || r.ptr != s.data() + s.size()) return false;
               out = n;
               return true;
            };
            if (suffix.starts_with('?')) {
               suffix.remove_prefix(1);
               step.filter.emplace_back();
               while (!suffix.empty()) {
                  detail::query_predicate predicate;
                  suffix = detail::trim_query(suffix);
                  if (suffix.starts_with('!') && !suffix.starts_with("!=")) {
                     predicate.negate = true;
                     suffix.remove_prefix(1);
                  }
                  const auto end = suffix.find_first_of(" \t=!<>&|");
                  auto selector = detail::query_selector(suffix.substr(0, end));
                  if (!selector) return invalid();
                  predicate.selector = *selector;
                  suffix = end == suffix.npos ? std::string_view{} : detail::trim_query(suffix.substr(end));
                  for (auto op : {"==", "!=", "<=", ">=", "<", ">"})
                     if (suffix.starts_with(op)) {
                        predicate.comparison = op;
                        suffix = detail::trim_query(suffix.substr(predicate.comparison.size()));
                        context ctx{};
                        detail::parser parser{suffix, ctx};
                        if (!parser.entry(predicate.literal)) return invalid();
                        suffix = detail::trim_query(suffix.substr(parser.pos));
                        break;
                     }
                  step.filter.back().push_back(std::move(predicate));
                  if (suffix.empty()) break;
                  if (suffix.starts_with("||"))
                     step.filter.emplace_back();
                  else if (!suffix.starts_with("&&"))
                     return invalid();
                  suffix = suffix.substr(2);
                  if (detail::trim_query(suffix).empty()) return invalid();
               }
               if (step.filter.back().empty()) return invalid();
            }
            else if (suffix.find(':') != suffix.npos) {
               step.sliced = true;
               const auto colon = suffix.find(':');
               if (!number(suffix.substr(0, colon), step.start)) return invalid();
               suffix.remove_prefix(colon + 1);
               const auto second = suffix.find(':');
               if (!number(suffix.substr(0, second), step.stop)) return invalid();
               if (second != suffix.npos) {
                  std::optional<ptrdiff_t> stride;
                  if (!number(suffix.substr(second + 1), stride)) return invalid();
                  step.stride = stride.value_or(1);
                  if (!step.stride) return invalid();
               }
            }
            else if (!suffix.empty() && suffix != "*" && !number(suffix, step.index))
               return invalid();
         }
         if (step.selector.kind != path_kind::node && pos < expression.size() && expression[pos] == '/')
            return invalid();
         result.steps.push_back(std::move(step));
      }
      while (pos < expression.size()) {
         if (expression[pos++] != '|') return invalid();
         auto end = expression.find('|', pos);
         auto name =
            detail::trim_query(expression.substr(pos, end == expression.npos ? expression.size() - pos : end - pos));
         using F = compiled_query::function;
         F function;
         if (name == "count()")
            function = F::count;
         else if (name == "first()")
            function = F::first;
         else if (name == "last()")
            function = F::last;
         else if (name == "reverse()")
            function = F::reverse;
         else
            return invalid();
         result.pipeline.push_back(function);
         if (function == F::count && end != expression.npos) return invalid();
         pos = end == expression.npos ? expression.size() : end;
      }
      return result;
   }

   inline query_result query(const document& doc, const compiled_query& expression)
   {
      query_result result;
      std::vector<const document*> parents{&doc};
      std::vector<const node*> selected;
      for (const auto& step : expression.steps) {
         selected.clear();
         if (step.selector.kind != path_kind::node) {
            break;
         }
         for (auto* parent : parents) {
            std::vector<const node*> candidates;
            auto walk = [&](auto&& self, const document& nodes) -> void {
               for (const auto& n : nodes) {
                  if ((step.wildcard || n.name == step.selector.key) && detail::matches_query(n, step))
                     candidates.push_back(&n);
                  if (step.recursive) self(self, n.children);
               }
            };
            walk(walk, *parent);
            const auto size = static_cast<ptrdiff_t>(candidates.size());
            auto normalize = [&](ptrdiff_t i, bool negative = false) {
               if (i < 0) i = i < -size ? -1 : i + size;
               return std::clamp(i, negative ? ptrdiff_t{-1} : ptrdiff_t{0}, negative ? size - 1 : size);
            };
            if (step.index) {
               const auto index = normalize(*step.index);
               // An index before the beginning must not be clamped to element 0.
               if (*step.index >= -size && index < size) selected.push_back(candidates[size_t(index)]);
            }
            else if (step.sliced) {
               const auto start = step.start ? normalize(*step.start, step.stride < 0) : step.stride > 0 ? 0 : size - 1;
               const auto stop = step.stop ? normalize(*step.stop, step.stride < 0) : step.stride > 0 ? size : -1;
               for (auto i = start; i >= 0 && i < size && (step.stride > 0 ? i < stop : i > stop);) {
                  selected.push_back(candidates[size_t(i)]);
                  // Avoid signed overflow for extreme user-provided strides.
                  if ((step.stride > 0 && step.stride > size - i) || (step.stride < 0 && step.stride < -i - 1)) break;
                  i += step.stride;
               }
            }
            else
               selected.insert(selected.end(), candidates.begin(), candidates.end());
         }
         parents.clear();
         for (auto* n : selected) parents.push_back(&n->children);
      }
      // Evaluate the terminal projection against the selected nodes.
      const auto* projection = expression.steps.empty() ? nullptr : &expression.steps.back().selector;
      // Re-evaluate the node prefix when the final step is a scalar selector.
      if (projection && projection->kind != path_kind::node) {
         auto prefix = expression;
         prefix.steps.pop_back();
         prefix.pipeline.clear();
         auto nodes = query(doc, prefix);
         for (const auto& item : nodes.items)
            if (const auto* pn = std::get_if<const node*>(&item)) {
               const auto& n = **pn;
               if (projection->kind == path_kind::name)
                  result.items.emplace_back(std::string_view{n.name});
               else if (projection->kind == path_kind::tag) {
                  if (n.tag) result.items.emplace_back(std::string_view{*n.tag});
               }
               else if (projection->kind == path_kind::argument) {
                  if (projection->index < n.arguments.size())
                     result.items.emplace_back(&n.arguments[projection->index]);
               }
               else
                  for (const auto& [key, v] : n.properties)
                     if (key == projection->key) result.items.emplace_back(&v);
            }
      }
      else if (expression.steps.empty())
         for (const auto& n : doc) result.items.emplace_back(&n);
      else
         for (auto* n : selected) result.items.emplace_back(n);
      for (auto fn : expression.pipeline) {
         using F = compiled_query::function;
         if (fn == F::count) {
            result.count = result.items.size();
            result.items.clear();
         }
         else if (fn == F::reverse)
            std::reverse(result.items.begin(), result.items.end());
         else if (!result.items.empty()) {
            auto item = fn == F::first ? result.items.front() : result.items.back();
            result.items.assign(1, item);
         }
      }
      return result;
   }

   [[nodiscard]] inline expected<query_result, error_ctx> query(const document& doc, std::string_view expression)
   {
      auto compiled = compile_query(expression);
      if (!compiled) return unexpected(compiled.error());
      return query(doc, *compiled);
   }

   [[nodiscard]] inline expected<std::string, error_ctx> query_kdl(std::string_view source, std::string_view expression)
   {
      auto doc = read_kdl<document>(source);
      if (!doc) return unexpected(doc.error());
      auto selected = query(*doc, expression);
      if (!selected) return unexpected(selected.error());
      document output;
      if (selected->count) output.push_back({"count", {}, {{*selected->count, {}}}, {}, {}});
      for (const auto& item : selected->items)
         std::visit(
            [&](const auto& x) {
               using T = std::remove_cvref_t<decltype(x)>;
               if constexpr (std::same_as<T, const node*>)
                  output.push_back(*x);
               else if constexpr (std::same_as<T, const value*>)
                  output.push_back({"value", {}, {*x}, {}, {}});
               else
                  output.push_back({"value", {}, {{std::string{x}, {}}}, {}, {}});
            },
            item);
      return write_kdl(output);
   }
}
