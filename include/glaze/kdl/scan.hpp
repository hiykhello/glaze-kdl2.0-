// Glaze Library
// For the license information refer to glaze.hpp

#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "glaze/simd/simd.hpp"
#include "glaze/util/inline.hpp"

namespace glz::kdl::detail::scan
{
   enum class kind { validation, quoted, raw, line_comment, block_comment, spaces, lines, token, identifier };

   // Stop before a byte requiring grammar handling or Unicode decoding. This is
   // also the scalar tail/reference predicate; no input padding is required.
   template <kind Kind>
   constexpr bool stop(uint8_t c) noexcept
   {
      if constexpr (Kind == kind::validation)
         return c < 9 || (c >= 14 && c < 32) || c >= 127;
      else if constexpr (Kind == kind::quoted)
         return c < 32 || c >= 127 || c == '"' || c == '\\';
      else if constexpr (Kind == kind::raw)
         return c < 32 || c >= 127 || c == '"';
      else if constexpr (Kind == kind::line_comment)
         return c >= 128 || (c >= 10 && c <= 13);
      else if constexpr (Kind == kind::block_comment)
         return c == '/' || c == '*';
      else if constexpr (Kind == kind::spaces)
         return c != ' ' && c != '\t';
      else if constexpr (Kind == kind::lines)
         return c != ' ' && !(c >= 9 && c <= 13);
      else
         return c <= 32 || c >= 127 || c == '\\' || c == '/' || c == '(' || c == ')' || c == '{' || c == '}' ||
                c == ';' || c == '[' || c == ']' || c == '"' || c == '=' || (Kind == kind::identifier && c == '#');
   }

#if defined(GLZ_USE_AVX512BW)
   inline constexpr std::string_view backend = "AVX512BW";
   inline constexpr size_t width = 64;
   struct block
   {
      __m512i v;
      explicit block(const char* p) : v(_mm512_loadu_si512(p)) {}
      GLZ_ALWAYS_INLINE uint64_t eq(char c) const noexcept { return _mm512_cmpeq_epi8_mask(v, _mm512_set1_epi8(c)); }
      GLZ_ALWAYS_INLINE uint64_t lt(char c) const noexcept { return _mm512_cmplt_epi8_mask(v, _mm512_set1_epi8(c)); }
   };
#elif defined(GLZ_USE_AVX2)
   inline constexpr std::string_view backend = "AVX2";
   inline constexpr size_t width = 32;
   struct block
   {
      __m256i v;
      explicit block(const char* p) : v(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(p))) {}
      GLZ_ALWAYS_INLINE uint64_t eq(char c) const noexcept
      {
         return uint32_t(_mm256_movemask_epi8(_mm256_cmpeq_epi8(v, _mm256_set1_epi8(c))));
      }
      GLZ_ALWAYS_INLINE uint64_t lt(char c) const noexcept
      {
         return uint32_t(_mm256_movemask_epi8(_mm256_cmpgt_epi8(_mm256_set1_epi8(c), v)));
      }
   };
#elif defined(GLZ_USE_SSE2)
   inline constexpr std::string_view backend = "SSE2";
   inline constexpr size_t width = 16;
   struct block
   {
      __m128i v;
      explicit block(const char* p) : v(_mm_loadu_si128(reinterpret_cast<const __m128i*>(p))) {}
      GLZ_ALWAYS_INLINE uint64_t eq(char c) const noexcept
      {
         return uint32_t(_mm_movemask_epi8(_mm_cmpeq_epi8(v, _mm_set1_epi8(c))));
      }
      GLZ_ALWAYS_INLINE uint64_t lt(char c) const noexcept
      {
         return uint32_t(_mm_movemask_epi8(_mm_cmplt_epi8(v, _mm_set1_epi8(c))));
      }
   };
#elif defined(GLZ_USE_NEON64)
   inline constexpr std::string_view backend = "NEON64";
   inline constexpr size_t width = 16;
   struct block
   {
      int8x16_t v;
      explicit block(const char* p) : v(vld1q_s8(reinterpret_cast<const int8_t*>(p))) {}
      GLZ_ALWAYS_INLINE static uint64_t mask(uint8x16_t x) noexcept
      {
         static constexpr uint8_t weights[16]{1, 2, 4, 8, 16, 32, 64, 128, 1, 2, 4, 8, 16, 32, 64, 128};
         const auto bits = vandq_u8(x, vld1q_u8(weights));
         return uint64_t(vaddv_u8(vget_low_u8(bits))) | (uint64_t(vaddv_u8(vget_high_u8(bits))) << 8);
      }
      GLZ_ALWAYS_INLINE uint64_t eq(char c) const noexcept { return mask(vceqq_s8(v, vdupq_n_s8(c))); }
      GLZ_ALWAYS_INLINE uint64_t lt(char c) const noexcept { return mask(vcltq_s8(v, vdupq_n_s8(c))); }
   };
#elif defined(GLZ_USE_WASM_SIMD128)
   inline constexpr std::string_view backend = "WASM_SIMD128";
   inline constexpr size_t width = 16;
   struct block
   {
      v128_t v;
      explicit block(const char* p) : v(wasm_v128_load(p)) {}
      GLZ_ALWAYS_INLINE uint64_t eq(char c) const noexcept
      {
         return wasm_i8x16_bitmask(wasm_i8x16_eq(v, wasm_i8x16_splat(c)));
      }
      GLZ_ALWAYS_INLINE uint64_t lt(char c) const noexcept
      {
         return wasm_i8x16_bitmask(wasm_i8x16_lt(v, wasm_i8x16_splat(c)));
      }
   };
#else
   inline constexpr std::string_view backend = "scalar";
   inline constexpr size_t width = 0;
#endif

#if defined(GLZ_USE_AVX512BW) || defined(GLZ_USE_AVX2) || defined(GLZ_USE_SSE2) || defined(GLZ_USE_NEON64) || \
   defined(GLZ_USE_WASM_SIMD128)
   template <kind Kind>
   GLZ_ALWAYS_INLINE uint64_t events(const block& b) noexcept
   {
      // Signed byte compares intentionally include non-ASCII bytes in lt(0)
      // and all positive thresholds. Unicode returns to the strict decoder.
      if constexpr (Kind == kind::validation)
         return b.lt(9) | (b.lt(32) & ~b.lt(14)) | b.eq(127);
      else if constexpr (Kind == kind::quoted)
         return b.lt(32) | b.eq(127) | b.eq('"') | b.eq('\\');
      else if constexpr (Kind == kind::raw)
         return b.lt(32) | b.eq(127) | b.eq('"');
      else if constexpr (Kind == kind::line_comment)
         return b.lt(0) | (b.lt(14) & ~b.lt(10));
      else if constexpr (Kind == kind::block_comment)
         return b.eq('/') | b.eq('*');
      else if constexpr (Kind == kind::spaces)
         return ~(b.eq(' ') | b.eq('\t'));
      else if constexpr (Kind == kind::lines)
         return ~(b.eq(' ') | (b.lt(14) & ~b.lt(9)));
      else {
         auto result = b.lt(33) | b.eq(127) | b.eq('\\') | b.eq('/') | b.eq('(') | b.eq(')') | b.eq('{') | b.eq('}') |
                       b.eq(';') | b.eq('[') | b.eq(']') | b.eq('"') | b.eq('=');
         if constexpr (Kind == kind::identifier) result |= b.eq('#');
         return result;
      }
   }
#endif

   template <kind Kind>
   GLZ_ALWAYS_INLINE size_t run(std::string_view input, size_t pos) noexcept
   {
#if defined(GLZ_USE_AVX512BW) || defined(GLZ_USE_AVX2) || defined(GLZ_USE_SSE2) || defined(GLZ_USE_NEON64) || \
   defined(GLZ_USE_WASM_SIMD128)
      // Like JSON's scanners, load only complete registers; never read beyond
      // the caller's buffer, including at a page boundary or with no padding.
      while (input.size() - pos >= width) {
         const auto mask = events<Kind>(block{input.data() + pos}) & (UINT64_MAX >> (64 - width));
         if (mask) {
            return pos + size_t(std::countr_zero(mask));
         }
         pos += width;
      }
#endif
      while (pos < input.size() && !stop<Kind>(uint8_t(input[pos]))) ++pos;
      return pos;
   }
}

namespace glz::kdl
{
   // Compile-time backend for this translation unit, matching Glaze's SIMD policy.
   constexpr std::string_view simd_backend = detail::scan::backend;
}
