// Glaze Library
// For the license information refer to glaze.hpp

#pragma once

#include <charconv>
#include <concepts>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "glaze/core/context.hpp"
#include "glaze/core/opts.hpp"
#include "glaze/forward.hpp"
#include "glaze/kdl/common.hpp"
#include "glaze/kdl/parse.hpp"
#include "glaze/kdl/read.hpp" // IWYU pragma: keep
#include "glaze/kdl/read_direct.hpp"
#include "glaze/kdl/token.hpp"
#include "glaze/kdl/types.hpp"
#include "glaze/util/expected.hpp"

namespace glz::kdl
{
   enum class path_kind { node, argument, property, name, tag };
   struct path_step
   {
      path_kind kind = path_kind::node;
      std::string key;
      size_t index{};
   };
   using path = std::vector<path_step>;

   // /server[1]/host, /server/@port, /server/#0, /server/:tag.
   // ~0..~6 escape ~ / [ ] @ # : respectively in node/property names.
   [[nodiscard]] inline expected<path, error_ctx> parse_path(std::string_view text)
   {
      path result;
      if (text.empty()) return result;
      if (text.front() != '/') return unexpected(error_ctx{0, error_code::invalid_query});
      size_t start = 1;
      do {
         const auto slash = text.find('/', start);
         auto token = text.substr(start, slash == text.npos ? text.size() - start : slash - start);
         path_step step;
         if (token == ":name")
            step.kind = path_kind::name;
         else if (token == ":tag")
            step.kind = path_kind::tag;
         else if (token.starts_with('@')) {
            step.kind = path_kind::property;
            token.remove_prefix(1);
         }
         else if (token.starts_with('#')) {
            step.kind = path_kind::argument;
            token.remove_prefix(1);
         }
         if (step.kind == path_kind::argument) {
            const auto r = std::from_chars(token.data(), token.data() + token.size(), step.index);
            if (token.empty() || r.ec != std::errc{} || r.ptr != token.data() + token.size())
               return unexpected(error_ctx{start, error_code::invalid_query});
         }
         else if (step.kind == path_kind::node && token.ends_with(']')) {
            const auto open = token.rfind('[');
            if (open == token.npos) return unexpected(error_ctx{start, error_code::invalid_query});
            const auto number = token.substr(open + 1, token.size() - open - 2);
            const auto r = std::from_chars(number.data(), number.data() + number.size(), step.index);
            if (number.empty() || r.ec != std::errc{} || r.ptr != number.data() + number.size())
               return unexpected(error_ctx{start, error_code::invalid_query});
            token = token.substr(0, open);
         }
         if (step.kind == path_kind::node || step.kind == path_kind::property) {
            constexpr std::string_view escaped = "~/[]@#:";
            for (size_t i = 0; i < token.size(); ++i) {
               if (token[i] != '~')
                  step.key += token[i];
               else {
                  if (++i == token.size() || token[i] < '0' || token[i] > '6')
                     return unexpected(error_ctx{start + i, error_code::invalid_query});
                  step.key += escaped[size_t(token[i] - '0')];
               }
            }
         }
         result.push_back(std::move(step));
         if (slash == text.npos) break;
         if (result.back().kind != path_kind::node) return unexpected(error_ctx{slash, error_code::invalid_query});
         start = slash + 1;
      } while (true);
      return result;
   }

   template <bool Const>
   using basic_path_target =
      std::variant<std::conditional_t<Const, const document*, document*>, std::conditional_t<Const, const node*, node*>,
                   std::conditional_t<Const, const value*, value*>,
                   std::conditional_t<Const, const std::string*, std::string*>,
                   std::conditional_t<Const, const std::optional<std::string>*, std::optional<std::string>*>>;
   using path_target = basic_path_target<false>;
   using const_path_target = basic_path_target<true>;

   template <class Doc>
   auto get(Doc& doc, const path& address) -> expected<basic_path_target<std::is_const_v<Doc>>, error_ctx>
   {
      using Result = basic_path_target<std::is_const_v<Doc>>;
      auto* nodes = &doc;
      using Node = std::conditional_t<std::is_const_v<Doc>, const node, node>;
      Node* current{};
      for (size_t i = 0; i < address.size(); ++i) {
         const auto& step = address[i];
         if (step.kind == path_kind::node) {
            size_t found{};
            current = nullptr;
            for (auto& n : *nodes)
               if (n.name == step.key && found++ == step.index) {
                  current = &n;
                  break;
               }
            if (!current) return unexpected(error_ctx{i, error_code::key_not_found});
            nodes = &current->children;
         }
         else {
            if (!current || i + 1 != address.size()) return unexpected(error_ctx{i, error_code::invalid_query});
            if (step.kind == path_kind::name) return Result{&current->name};
            if (step.kind == path_kind::tag) return Result{&current->tag};
            if (step.kind == path_kind::argument && step.index < current->arguments.size())
               return Result{&current->arguments[step.index]};
            if (step.kind == path_kind::property) {
               for (auto& [key, v] : current->properties)
                  if (key == step.key) return Result{&v};
            }
            return unexpected(error_ctx{i, error_code::key_not_found});
         }
      }
      if (current) return Result{current};
      return Result{nodes};
   }

   template <class Doc>
   auto get(Doc& doc, std::string_view text) -> expected<basic_path_target<std::is_const_v<Doc>>, error_ctx>
   {
      auto address = parse_path(text);
      if (!address) return unexpected(address.error());
      return get(doc, *address);
   }

   // Scans/validates native source, retaining only the selected source span.
   [[nodiscard]] inline expected<std::string_view, error_ctx> get_view(std::string_view source, const path& address)
   {
      context ctx{};
      detail::parser p{source, ctx};
      std::optional<std::string_view> selected;
      auto walk = [&](auto&& self, size_t level, bool child) -> void {
         size_t seen{};
         p.nodes_each(
            [&](const auto& name, const auto&) {
               const auto begin = p.node_begin;
               if (level >= address.size() || address[level].kind != path_kind::node || address[level].key != name ||
                   seen++ != address[level].index) {
                  p.skip_body();
                  return;
               }
               if (level + 1 == address.size()) {
                  if (p.skip_body()) selected = source.substr(begin, p.pos - begin);
                  return;
               }
               const auto& next = address[level + 1];
               size_t argument{};
               p.body(
                  [&](const auto&, std::string_view raw) {
                     if (next.kind == path_kind::argument && argument == next.index) selected = raw;
                     ++argument;
                  },
                  [&](std::string_view key, const auto&, std::string_view raw) {
                     if (next.kind == path_kind::property && key == next.key) selected = raw;
                  },
                  [&] { self(self, level + 1, true); });
            },
            child);
      };
      if (p.validate()) walk(walk, 0, false);
      if (ctx.error != error_code::none) return unexpected(error_ctx{p.pos, ctx.error});
      if (address.empty()) return source;
      if (!selected) return unexpected(error_ctx{p.pos, error_code::key_not_found});
      return *selected;
   }
   [[nodiscard]] inline expected<std::string_view, error_ctx> get_view(std::string_view source, std::string_view text)
   {
      auto address = parse_path(text);
      if (!address) return unexpected(address.error());
      return get_view(source, *address);
   }

   template <class T, auto Opts = opts{.format = KDL}>
   [[nodiscard]] expected<T, error_ctx> read_at(std::string_view source, std::string_view text)
   {
      auto address = parse_path(text);
      if (!address) return unexpected(address.error());
      auto view = get_view(source, *address);
      if (!view) return unexpected(view.error());
      T result{};
      context ctx{};
      detail::parser p{*view, ctx};
      detail::direct_reader<Opts, context> reader{p};
      if (address->empty()) {
         if constexpr (std::same_as<T, node>) {
            size_t count{};
            p.nodes_each([&](const auto& name, const auto& tag) {
               if (++count != 1)
                  p.fail();
               else
                  reader.node(result, name, tag);
            });
            if (!count) p.fail(error_code::no_read_input);
         }
         else if constexpr (detail::document_type<T>)
            reader.document(result);
         else
            p.fail(error_code::invalid_query);
      }
      else if (address->back().kind == path_kind::node)
         p.nodes_each([&](const auto& name, const auto& tag) { reader.node(result, name, tag); });
      else {
         if constexpr (detail::scalar_type<T>) {
            detail::scalar_token scalar;
            if (p.entry(scalar)) detail::read_scalar<Opts>(result, scalar, ctx, *view);
         }
         else
            p.fail(error_code::invalid_query);
      }
      if (ctx.error != error_code::none) return unexpected(error_ctx{p.pos, ctx.error});
      return result;
   }

   template <class T>
   [[nodiscard]] error_ctx set(document& doc, std::string_view address, T replacement)
   {
      auto target = get(doc, address);
      if (!target) return target.error();
      bool assigned{};
      std::visit(
         [&](auto* ptr) {
            if constexpr (std::same_as<std::remove_cvref_t<decltype(*ptr)>, T>) {
               *ptr = std::move(replacement);
               assigned = true;
            }
         },
         *target);
      return {0, assigned ? error_code::none : error_code::get_wrong_type};
   }
}
