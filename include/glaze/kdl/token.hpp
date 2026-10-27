// Glaze Library
// For the license information refer to glaze.hpp

#pragma once

#include <concepts>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "glaze/kdl/common.hpp"

namespace glz::kdl::detail
{
   // A parser token may borrow unescaped text. Only explicitly dynamic targets
   // materialize an owning value; a typed string_view never borrows a temporary.
   struct scalar_token
   {
      using data_type =
         std::variant<std::monostate, bool, int64_t, uint64_t, double, std::string, number, std::string_view>;
      data_type data{};
      std::optional<std::string> tag{};
      mutable std::optional<std::string> decoded{};
      void (*decode_text)(std::string_view, std::string&){};
      std::string_view string() const
      {
         const auto source = std::get<std::string_view>(data);
         if (!decode_text) return source;
         if (!decoded) {
            decoded.emplace();
            decode_text(source, *decoded);
         }
         return *decoded;
      }
      operator value() const&
      {
         value result;
         result.tag = tag;
         std::visit(
            [&](const auto& x) {
               if constexpr (std::same_as<std::remove_cvref_t<decltype(x)>, std::string_view>)
                  result.data = std::string{string()};
               else
                  result.data = x;
            },
            data);
         return result;
      }
      operator value() &&
      {
         value result;
         result.tag = std::move(tag);
         std::visit(
            [&](auto&& x) {
               if constexpr (std::same_as<std::remove_cvref_t<decltype(x)>, std::string_view>) {
                  const auto decoded_text = string();
                  if (decoded)
                     result.data = std::move(*decoded);
                  else
                     result.data = std::string{decoded_text};
               }
               else
                  result.data = std::move(x);
            },
            data);
         return result;
      }
   };
   inline std::optional<std::string_view> text(const value& v)
   {
      if (const auto* s = std::get_if<std::string>(&v.data)) return *s;
      return {};
   }
   inline std::optional<std::string_view> text(const scalar_token& v)
   {
      if (std::holds_alternative<std::string_view>(v.data)) return v.string();
      if (const auto* s = std::get_if<std::string>(&v.data)) return *s;
      return {};
   }
   inline std::optional<std::string_view> borrowed_text(const scalar_token& v)
   {
      if (!v.decode_text) {
         if (const auto* s = std::get_if<std::string_view>(&v.data)) return *s;
      }
      return {};
   }
   inline std::optional<std::string_view> raw_text(const scalar_token& v)
   {
      if (const auto* s = std::get_if<std::string_view>(&v.data)) return *s;
      return {};
   }
   inline std::optional<std::string_view> raw_text(const value&) { return {}; }
   inline std::optional<std::string_view> borrowed_text(const value&) { return {}; }
}
