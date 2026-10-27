// Glaze Library
// For the license information refer to glaze.hpp

#pragma once

#include <charconv>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <variant>

#include "glaze/concepts/container_concepts.hpp"
#include "glaze/core/common.hpp"
#include "glaze/core/context.hpp"
#include "glaze/core/lazy.hpp"
#include "glaze/core/meta.hpp"
#include "glaze/core/opts.hpp"
#include "glaze/core/raw_string.hpp"
#include "glaze/core/reflect.hpp"
#include "glaze/core/write.hpp"
#include "glaze/core/write_wrappers.hpp"
#include "glaze/forward.hpp"
#include "glaze/kdl/common.hpp"
#include "glaze/kdl/parse.hpp"
#include "glaze/kdl/scan.hpp"
#include "glaze/kdl/types.hpp"
#include "glaze/util/dump.hpp"
#include "glaze/util/expected.hpp"
#include "glaze/util/string_literal.hpp"
#include "glaze/util/variant.hpp"

namespace glz::kdl::detail
{
   template <auto Opts, class Ctx, class B, class IX>
   struct writer
   {
      Ctx& ctx;
      B& buffer;
      IX& ix;

      void append(std::string_view s)
      {
         if (s.empty()) return;
         if (bool(ctx.error) || !ensure_space(ctx, buffer, ix + s.size())) return;
         dump<false>(s, buffer, ix);
      }
      void string(std::string_view s)
      {
         append("\"");
         size_t p{};
         uint32_t c{};
         while (p < s.size() && !bool(ctx.error)) {
            const auto start = p;
            p = scan::run<scan::kind::quoted>(s, p);
            append(s.substr(start, p - start));
            if (p == s.size()) break;
            const auto begin = p;
            if (!decode(s, p, c)) {
               ctx.error = error_code::syntax_error;
               return;
            }
            switch (c) {
            case '"':
               append("\\\"");
               break;
            case '\\':
               append("\\\\");
               break;
            case '\n':
               append("\\n");
               break;
            case '\r':
               append("\\r");
               break;
            case '\t':
               append("\\t");
               break;
            case '\b':
               append("\\b");
               break;
            case '\f':
               append("\\f");
               break;
            default:
               if (forbidden(c) || newline(c)) {
                  char hexbuf[8];
                  const auto r = std::to_chars(hexbuf, hexbuf + sizeof(hexbuf), c, 16);
                  append("\\u{");
                  append({hexbuf, size_t(r.ptr - hexbuf)});
                  append("}");
               }
               else
                  append(s.substr(begin, p - begin));
            }
         }
         append("\"");
      }
      void raw_string(std::string_view s)
      {
         append("\"");
         parser probe{s, ctx};
         while (probe.pos < s.size() && !bool(ctx.error)) {
            const auto start = probe.pos;
            probe.pos = scan::run<scan::kind::quoted>(s, probe.pos);
            append(s.substr(start, probe.pos - start));
            if (probe.pos == s.size()) break;
            const auto begin = probe.pos;
            if (probe.at("\\")) {
               std::string ignored;
               if (!probe.escape(ignored)) return;
            }
            else {
               uint32_t c{};
               if (!decode(s, probe.pos, c) || forbidden(c) || newline(c) || c == '"') {
                  ctx.error = error_code::syntax_error;
                  return;
               }
            }
            append(s.substr(begin, probe.pos - begin));
         }
         append("\"");
      }
      void name(std::string_view s)
      {
         if (identifier(s))
            append(s);
         else
            string(s);
      }
      void tag(const std::optional<std::string>& s)
      {
         if (!s) return;
         append("(");
         name(*s);
         append(")");
      }
      void scalar(const value& v)
      {
         tag(v.tag);
         std::visit(
            [&](const auto& x) {
               using T = std::decay_t<decltype(x)>;
               if constexpr (std::same_as<T, std::monostate>)
                  append("#null");
               else if constexpr (std::same_as<T, bool>)
                  append(x ? "#true" : "#false");
               else if constexpr (std::same_as<T, std::string>)
                  string(x);
               else if constexpr (std::same_as<T, number>) {
                  parser validator{std::string_view{x.text}, ctx};
                  value parsed;
                  if (x.text.empty() || !validator.number(x.text, parsed)) {
                     ctx.error = error_code::parse_number_failure;
                     return;
                  }
                  append(x.text);
               }
               else {
                  if constexpr (std::same_as<T, double>) {
                     if (std::isnan(x)) {
                        append("#nan");
                        return;
                     }
                     if (std::isinf(x)) {
                        append(x < 0 ? "#-inf" : "#inf");
                        return;
                     }
                  }
                  char chars[64];
                  const auto r = std::to_chars(chars, chars + sizeof(chars), x);
                  if (r.ec != std::errc{}) {
                     ctx.error = error_code::parse_number_failure;
                     return;
                  }
                  const std::string_view s{chars, size_t(r.ptr - chars)};
                  append(s);
                  if constexpr (std::same_as<T, double>) {
                     if (s.find_first_of(".eE") == std::string_view::npos) append(".0");
                  }
               }
            },
            v.data);
      }
      void indent(size_t level)
      {
         for (size_t i = 0; i < level * check_indentation_width(Opts); ++i) {
            constexpr char c = check_indentation_char(Opts);
            append({&c, 1});
         }
      }

      template <class V>
      void typed_scalar(V&& source)
      {
         using T = std::remove_cvref_t<V>;
         if constexpr (is_specialization_v<T, raw_string_t>)
            writer<opt_true<Opts, raw_string_opt_tag{}>, Ctx, B, IX>{ctx, buffer, ix}.typed_scalar(source.val);
         else if constexpr (is_specialization_v<T, escaped_t>)
            writer<opt_false<Opts, raw_string_opt_tag{}>, Ctx, B, IX>{ctx, buffer, ix}.typed_scalar(source.val);
         else if constexpr (transparent_write_wrapper<T>)
            unwrap_write_value(source, ctx, [&](auto&& inner) { typed_scalar(inner); });
         else if constexpr (custom_write<T>)
            to<KDL, T>::template op<Opts>(source, ctx, buffer, ix);
         else if constexpr (is_variant<T>) {
            if (source.valueless_by_exception()) {
               ctx.error = error_code::invalid_variant_object;
               return;
            }
            append("(");
            name(std::string_view{ids_v<T>[source.index()]});
            append(")");
            std::visit([&](auto&& inner) { typed_scalar(inner); }, source);
         }
         else if constexpr (std::same_as<T, value>)
            scalar(source);
         else if constexpr (nullable_like<T>) {
            if (source)
               typed_scalar(*source);
            else
               append("#null");
         }
         else if constexpr (std::same_as<T, std::string> || std::same_as<T, std::string_view>) {
            if constexpr (check_raw_string(Opts))
               raw_string(source);
            else
               string(source);
         }
         else
            scalar(write_scalar<Opts>(source, ctx));
      }

      template <class V>
      void typed_node(std::string_view key, V&& source, size_t level)
      {
         using T = std::remove_cvref_t<V>;
         if (bool(ctx.error)) return;
         if constexpr (is_specialization_v<T, lazy>) {
            auto result = source.get();
            if (!result)
               ctx.error = result.error().ec;
            else
               typed_node(key, result->get(), level);
         }
         else if constexpr (transparent_write_wrapper<T>)
            unwrap_write_value(source, ctx, [&](auto&& inner) { typed_node(key, inner, level); });
         else {
            indent(level);
            typed_tag(source);
            name(key);
            typed_body(source, level);
            append("\n");
         }
      }

      template <class T>
      void typed_tag(const T& source)
      {
         if constexpr (nullable_like<T>) {
            if (source) typed_tag(*source);
         }
         else if constexpr (is_variant<T>) {
            if (source.valueless_by_exception()) {
               ctx.error = error_code::invalid_variant_object;
               return;
            }
            if constexpr (!requires { meta<T>::tag; }) {
               append("(");
               name(std::string_view{ids_v<T>[source.index()]});
               append(")");
            }
         }
         else if constexpr (is_mixed<T> || std::same_as<T, node>)
            tag(source.tag);
      }

      template <class V>
      void arguments(V&& source)
      {
         using T = std::remove_cvref_t<V>;
         if constexpr (positional<T>) {
            for_each<position_count<T>>([&]<size_t I>() {
               append(" ");
               typed_scalar(position<I>(source));
            });
         }
         else {
            using E = std::ranges::range_value_t<T>;
            for (auto&& x : source) {
               append(" ");
               if constexpr (std::same_as<std::remove_cvref_t<decltype(x)>, E>)
                  typed_scalar(x);
               else
                  typed_scalar(static_cast<E>(x));
            }
         }
      }

      template <class V>
      void properties(V&& source)
      {
         using T = std::remove_cvref_t<V>;
         auto field = [&](std::string_view key, auto&& wrapped) {
            unwrap_write_value(wrapped, ctx, [&](auto&& x) {
               using M = std::remove_cvref_t<decltype(x)>;
               if constexpr (always_null_t<M> && Opts.skip_null_members)
                  return;
               else if constexpr (nullable_like<M> && Opts.skip_null_members) {
                  if (!x) return;
               }
               append(" ");
               name(key);
               append("=");
               typed_scalar(x);
            });
         };
         if constexpr (std::same_as<T, kdl::properties>) {
            for (auto&& [key, x] : source) {
               append(" ");
               name(key);
               append("=");
               scalar(x);
            }
         }
         else if constexpr (string_map<T>) {
            for (auto&& [key, x] : source) field(key, x);
         }
         else {
            for_each<reflect<T>::size>([&]<size_t I>() {
               if constexpr (!skipped_on_write<Opts, T, I>) field(get<I>(reflect<T>::keys), member<I>(source));
            });
         }
      }

      template <class V>
      void child_block(V&& source, size_t level)
      {
         using T = std::remove_cvref_t<V>;
         if constexpr (object<T> || string_map<T>)
            typed_object(source, level, true);
         else if (!std::ranges::empty(source)) {
            append(" {\n");
            if constexpr (std::same_as<T, document>)
               nodes(source, level + 1);
            else {
               depth_guard guard{ctx, 256};
               if (!guard) return;
               for (auto&& x : source) typed_node("-", x, level + 1);
            }
            indent(level);
            append("}");
         }
      }

      template <class V>
      void typed_object(V&& source, size_t level = 0, bool block = false)
      {
         using T = std::remove_cvref_t<V>;
         if constexpr (transparent_write_wrapper<T>)
            unwrap_write_value(source, ctx, [&](auto&& inner) { typed_object(inner, level, block); });
         else {
            depth_guard guard{ctx, 256};
            if (!guard) return;
            bool opened{};
            auto field = [&](std::string_view key, auto&& wrapped) {
               unwrap_write_value(wrapped, ctx, [&](auto&& x) {
                  using M = std::remove_cvref_t<decltype(x)>;
                  if (bool(ctx.error)) return;
                  if constexpr (always_null_t<M> && Opts.skip_null_members)
                     return;
                  else if constexpr (nullable_like<M> && Opts.skip_null_members) {
                     if (!x) return;
                  }
                  if (block && !opened) {
                     append(" {\n");
                     opened = true;
                  }
                  typed_node(key, x, level + size_t(block));
               });
            };
            if constexpr (string_map<T>) {
               for (auto&& [key, x] : source) field(key, x);
            }
            else {
               for_each<reflect<T>::size>([&]<size_t I>() {
                  if constexpr (!skipped_on_write<Opts, T, I>) field(get<I>(reflect<T>::keys), member<I>(source));
               });
            }
            if (opened) {
               indent(level);
               append("}");
            }
         }
      }

      template <class V>
      void typed_body(V&& source, size_t level)
      {
         using T = std::remove_cvref_t<V>;
         if constexpr (transparent_write_wrapper<T>)
            unwrap_write_value(source, ctx, [&](auto&& inner) { typed_body(inner, level); });
         else if constexpr (custom_write<T>) {
            append(" ");
            typed_scalar(source);
         }
         else if constexpr (nullable_like<T>) {
            if (source)
               typed_body(*source, level);
            else
               append(" #null");
         }
         else if constexpr (is_variant<T>) {
            if (source.valueless_by_exception()) {
               ctx.error = error_code::invalid_variant_object;
               return;
            }
            if constexpr (requires { meta<T>::tag; }) {
               append(" ");
               name(discriminator_key<T>);
               append("=");
               string(std::string_view{ids_v<T>[source.index()]});
            }
            std::visit([&](auto&& x) { typed_body(x, level); }, source);
         }
         else if constexpr (is_mixed<T> || std::same_as<T, node>) {
            arguments(source.arguments);
            properties(source.properties);
            child_block(source.children, level);
         }
         else if constexpr (std::same_as<T, document>)
            child_block(source, level);
         else if constexpr (positional<T>)
            arguments(source);
         else if constexpr (object<T> || string_map<T>)
            typed_object(source, level, true);
         else if constexpr (sequence<T>) {
            depth_guard guard{ctx, 256};
            if (!guard) return;
            using E = std::ranges::range_value_t<T>;
            bool opened{};
            for (auto&& x : source) {
               if (bool(ctx.error)) return;
               if constexpr (compound<E>) {
                  if (!opened) {
                     append(" {\n");
                     opened = true;
                  }
                  typed_node("-", x, level + 1);
               }
               else {
                  append(" ");
                  if constexpr (std::same_as<std::remove_cvref_t<decltype(x)>, E>)
                     typed_scalar(x);
                  else
                     typed_scalar(static_cast<E>(x));
               }
            }
            if (opened) {
               indent(level);
               append("}");
            }
         }
         else {
            append(" ");
            typed_scalar(source);
         }
      }

      void nodes(const document& doc, size_t level = 0)
      {
         depth_guard guard{ctx, 256};
         if (!guard) return;
         for (const auto& n : doc) {
            if (bool(ctx.error)) return;
            indent(level);
            tag(n.tag);
            name(n.name);
            for (const auto& arg : n.arguments) {
               append(" ");
               scalar(arg);
            }
            for (const auto& [key, v] : n.properties) {
               append(" ");
               name(key);
               append("=");
               scalar(v);
            }
            if (!n.children.empty()) {
               append(" {\n");
               nodes(n.children, level + 1);
               indent(level);
               append("}");
            }
            append("\n");
         }
      }
   };
}

namespace glz
{
   template <>
   struct serialize<KDL>
   {
      template <auto Opts, class T, class Ctx, class B, class IX>
      static void op(T&& value, Ctx& ctx, B& buffer, IX& ix)
      {
         to<KDL, std::remove_cvref_t<T>>::template op<Opts>(std::forward<T>(value), ctx, buffer, ix);
      }
   };

   template <class T>
      requires(kdl::detail::document_type<T> && !custom_write<T>)
   struct to<KDL, T>
   {
      template <auto Opts, class V, class Ctx, class B, class IX>
      static void op(V&& value, Ctx& ctx, B& buffer, IX& ix)
      {
         kdl::detail::writer<Opts, Ctx, B, IX> writer{ctx, buffer, ix};
         if constexpr (std::same_as<T, kdl::document>)
            writer.nodes(value);
         else if constexpr (std::same_as<T, kdl::node>)
            writer.typed_node(value.name, value, 0);
         else
            writer.typed_object(value);
      }
   };

   template <class T>
      requires((kdl::detail::scalar_type<T> || glaze_value_t<T>) && !custom_write<T> && !kdl::detail::document_type<T>)
   struct to<KDL, T>
   {
      template <auto Opts, class V, class Ctx, class B, class IX>
      static void op(V&& value, Ctx& ctx, B& buffer, IX& ix)
      {
         kdl::detail::writer<Opts, Ctx, B, IX>{ctx, buffer, ix}.typed_scalar(value);
      }
   };

   template <write_supported<KDL> T, output_buffer Buffer>
   [[nodiscard]] error_ctx write_kdl(T&& value, Buffer&& buffer)
   {
      return write<opts{.format = KDL}>(std::forward<T>(value), buffer);
   }

   template <write_supported<KDL> T, raw_buffer Buffer>
   [[nodiscard]] error_ctx write_kdl(T&& value, Buffer&& buffer)
   {
      return write<opts{.format = KDL}>(std::forward<T>(value), std::forward<Buffer>(buffer));
   }

   template <write_supported<KDL> T>
   [[nodiscard]] expected<std::string, error_ctx> write_kdl(T&& value)
   {
      return write<opts{.format = KDL}>(std::forward<T>(value));
   }

   template <auto Opts = opts{.format = KDL}, write_supported<KDL> T>
   [[nodiscard]] error_ctx write_file_kdl(T&& value, const sv file_name, auto&& buffer)
   {
      const auto ec = write<set_kdl<Opts>()>(std::forward<T>(value), buffer);
      if (ec) return ec;
      const auto file_ec = buffer_to_file(buffer, file_name);
      return {buffer.size(), file_ec};
   }
}
