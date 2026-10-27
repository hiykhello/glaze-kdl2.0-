// Glaze Library
// For the license information refer to glaze.hpp

#pragma once

#include <type_traits>

#include "glaze/core/opts.hpp"

namespace glz
{
   // do not decode/encode escaped characters for strings
   template <class T>
   struct raw_string_t
   {
      static constexpr bool glaze_wrapper = true;
      T& val;
   };

   // opposite of raw_string_t, turns off the option
   template <class T>
   struct escaped_t
   {
      static constexpr bool glaze_wrapper = true;
      T& val;
   };

   template <auto MemPtr>
   GLZ_ALWAYS_INLINE constexpr decltype(auto) raw_string_impl() noexcept
   {
      return [](auto&& val) { return raw_string_t<std::remove_reference_t<decltype(val.*MemPtr)>>{val.*MemPtr}; };
   }

   template <auto MemPtr>
   GLZ_ALWAYS_INLINE constexpr decltype(auto) escaped_impl() noexcept
   {
      return [](auto&& val) { return escaped_t<std::remove_reference_t<decltype(val.*MemPtr)>>{val.*MemPtr}; };
   }

   template <auto MemPtr>
   constexpr auto raw_string = raw_string_impl<MemPtr>();

   template <auto MemPtr>
   constexpr auto escaped = escaped_impl<MemPtr>();
}
