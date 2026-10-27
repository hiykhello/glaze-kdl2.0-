// Glaze Library
// For the license information refer to glaze.hpp

#pragma once

#include <type_traits>

#include "glaze/core/raw_string.hpp"
#include "glaze/json/read.hpp"
#include "glaze/json/write.hpp"

namespace glz
{
   template <class T>
   struct from<JSON, raw_string_t<T>>
   {
      template <auto Opts>
      GLZ_ALWAYS_INLINE static void op(auto&& value, auto&&... args)
      {
         parse<JSON>::op<opt_true<Opts, raw_string_opt_tag{}>>(value.val, args...);
      }
   };

   template <class T>
   struct to<JSON, raw_string_t<T>>
   {
      template <auto Opts>
      GLZ_ALWAYS_INLINE static void op(auto&& value, is_context auto&& ctx, auto&&... args)
      {
         using val_t = std::remove_cvref_t<decltype(value.val)>;
         to<JSON, val_t>::template op<opt_true<Opts, raw_string_opt_tag{}>>(value.val, ctx, args...);
      }
   };

   template <class T>
   struct from<JSON, escaped_t<T>>
   {
      template <auto Opts>
      GLZ_ALWAYS_INLINE static void op(auto&& value, auto&&... args)
      {
         parse<JSON>::op<opt_false<Opts, raw_string_opt_tag{}>>(value.val, args...);
      }
   };

   template <class T>
   struct to<JSON, escaped_t<T>>
   {
      template <auto Opts>
      GLZ_ALWAYS_INLINE static void op(auto&& value, is_context auto&& ctx, auto&&... args)
      {
         using val_t = std::remove_cvref_t<decltype(value.val)>;
         to<JSON, val_t>::template op<opt_false<Opts, raw_string_opt_tag{}>>(value.val, ctx, args...);
      }
   };

}
