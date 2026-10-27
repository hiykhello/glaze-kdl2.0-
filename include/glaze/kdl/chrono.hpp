// Glaze Library
// For the license information refer to glaze.hpp

#pragma once

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <string>
#include <string_view>
#include <system_error>

#include "glaze/core/context.hpp"

namespace glz::kdl::detail
{
   // Convert decimal unit strings to an integral duration without passing the
   // decimal fraction through floating point. Cancel rational factors before
   // multiplication, so large counts do not overflow intermediate products.
   template <class T, class Ctx>
   bool integral_duration(T& target, std::string_view number, uint64_t unit_num, uint64_t unit_den, Ctx& ctx)
   {
      using Rep = typename T::rep;
      auto fail = [&] {
         ctx.error = error_code::parse_number_failure;
         return false;
      };
      const bool negative = number.starts_with('-');
      if (negative) number.remove_prefix(1);
      const auto e = number.find_first_of("eE");
      const auto significand = number.substr(0, e);
      const auto dot = significand.find('.');
      std::string digits;
      for (char c : significand)
         if (c != '.') digits += c;
      const auto first = digits.find_first_not_of('0');
      if (first == std::string::npos) {
         target = T{0};
         return true;
      }
      const auto last = digits.find_last_not_of('0');
      int64_t exponent{};
      if (e != std::string_view::npos) {
         auto text = number.substr(e + 1);
         if (text.starts_with('+')) text.remove_prefix(1);
         const auto r = std::from_chars(text.data(), text.data() + text.size(), exponent);
         if (r.ec != std::errc{} || r.ptr != text.data() + text.size()) return fail();
      }
      // The floating parser has already rejected enormous exponents. Bound the
      // adjustment as well before adding lengths from the source string.
      if (exponent < -1000000 || exponent > 1000000 || digits.size() > 1000000) return fail();
      exponent += int64_t(digits.size() - last - 1);
      if (dot != std::string_view::npos) exponent -= int64_t(significand.size() - dot - 1);
      if (exponent < -128 || exponent > 128) return fail();
      digits = digits.substr(first, last - first + 1);
      std::array<uint64_t, 2> numerator{unit_num, uint64_t(T::period::den)};
      std::array<uint64_t, 2> denominator{unit_den, uint64_t(T::period::num)};
      for (auto& n : numerator) {
         for (auto& d : denominator) {
            const auto common = std::gcd(n, d);
            n /= common;
            d /= common;
         }
      }
      // Divide the decimal significand before reducing it to a native integer.
      // A two-word remainder avoids overflow in remainder * 10 for wide periods.
      auto divide = [&](uint64_t divisor) {
         if (divisor == 1) return true;
         uint64_t remainder{};
         for (char& digit : digits) {
            const auto times8 = remainder << 3;
            const auto times2 = remainder << 1;
            uint64_t low = times8 + times2;
            uint64_t high = (remainder >> 61) + (remainder >> 63) + uint64_t(low < times8);
            const auto next = low + uint64_t(digit - '0');
            high += uint64_t(next < low);
            low = next;
            unsigned quotient{};
            while (high || low >= divisor) {
               high -= uint64_t(low < divisor);
               low -= divisor;
               ++quotient;
            }
            digit = char('0' + quotient);
            remainder = low;
         }
         const auto nonzero = digits.find_first_not_of('0');
         if (nonzero != std::string::npos) digits.erase(0, nonzero);
         return remainder == 0;
      };
      auto twos = size_t(exponent < 0 ? -exponent : exponent);
      auto fives = twos;
      auto cancel = [&](auto& factors) {
         for (auto& factor : factors) {
            while (twos && factor % 2 == 0) {
               factor /= 2;
               --twos;
            }
            while (fives && factor % 5 == 0) {
               factor /= 5;
               --fives;
            }
         }
      };
      if (exponent < 0)
         cancel(numerator);
      else
         cancel(denominator);
      for (auto d : denominator)
         if (!divide(d)) return fail();
      if (exponent < 0) {
         while (twos--)
            if (!divide(2)) return fail();
         while (fives--)
            if (!divide(5)) return fail();
      }
      uint64_t mantissa{};
      const auto r = std::from_chars(digits.data(), digits.data() + digits.size(), mantissa);
      if (r.ec != std::errc{}) return fail();
      if (negative && !std::is_signed_v<Rep>) return fail();
      const uint64_t limit =
         negative ? uint64_t((std::numeric_limits<Rep>::max)()) + 1 : uint64_t((std::numeric_limits<Rep>::max)());
      uint64_t result = mantissa;
      auto multiply = [&](uint64_t factor) {
         if (result > limit / factor) return false;
         result *= factor;
         return true;
      };
      for (auto n : numerator)
         if (!multiply(n)) return fail();
      if (exponent > 0) {
         while (twos--)
            if (!multiply(2)) return fail();
         while (fives--)
            if (!multiply(5)) return fail();
      }
      if (negative) {
         if (result == limit)
            target = T{std::numeric_limits<Rep>::lowest()};
         else
            target = T{static_cast<Rep>(-static_cast<int64_t>(result))};
      }
      else
         target = T{static_cast<Rep>(result)};
      return true;
   }
}
