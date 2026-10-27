// Glaze Library
// For the license information refer to glaze.hpp

#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <ratio>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>

#include "glaze/concepts/container_concepts.hpp"
#include "glaze/core/chrono.hpp"
#include "glaze/core/common.hpp"
#include "glaze/core/constraint.hpp"
#include "glaze/core/context.hpp"
#include "glaze/core/custom.hpp"
#include "glaze/core/feature_test.hpp"
#include "glaze/core/lazy.hpp"
#include "glaze/core/meta.hpp"
#include "glaze/core/opts.hpp"
#include "glaze/core/raw_string.hpp"
#include "glaze/core/read.hpp" // IWYU pragma: keep
#include "glaze/core/reflect.hpp"
#include "glaze/core/tuple.hpp"
#include "glaze/core/write_wrappers.hpp"
#include "glaze/forward.hpp"
#include "glaze/kdl/chrono.hpp"
#include "glaze/kdl/common.hpp"
#include "glaze/kdl/parse.hpp"
#include "glaze/kdl/token.hpp"
#include "glaze/util/for_each.hpp"
#include "glaze/util/nullable_traits.hpp"
#include "glaze/util/tuple.hpp"
#include "glaze/util/variant.hpp"

namespace glz::kdl::detail
{
   template <class T>
   concept object = !is_std_tuple<T> && !is_specialization_v<T, std::pair> && !is_specialization_v<T, glz::tuple> &&
                    (glaze_object_t<T> || reflectable<T>) && !always_null_t<T>;

   template <size_t I, class T>
   decltype(auto) member(T&& obj)
   {
      using V = std::remove_cvref_t<T>;
      if constexpr (reflectable<V>)
         return get<I>(to_tie(obj));
      else
         return get_member(obj, get<I>(reflect<V>::values));
   }

   template <class T>
   concept string_map = readable_map_t<T> && std::same_as<typename T::key_type, std::string>;

   template <class T>
   concept sequence = readable_array_t<T> || (std::is_array_v<T> && !str_t<T>);

   template <class T>
   concept scalar_type =
      std::is_arithmetic_v<T> || always_null_t<T> || std::is_enum_v<T> || std::same_as<T, std::string> ||
      std::same_as<T, std::string_view> || filesystem_path<T> || is_duration<T> || is_time_point<T> ||
      is_year_month_day<T> || is_specialization_v<T, epoch_time> || std::same_as<T, value>;

   template <class T>
   struct serialized_type
   {
      using type = T;
   };
   template <class T>
      requires(glaze_value_t<T> && !custom_write<T>)
   struct serialized_type<T>
   {
      using type = typename serialized_type<
         std::remove_cvref_t<decltype(get_member(std::declval<T&>(), meta_wrapper_v<T>))>>::type;
   };
   template <class T, class From, class To>
   struct serialized_type<custom_t<T, From, To>>
   {
      using type = typename serialized_type<typename custom_getter_result<custom_t<T, From, To>>::type>::type;
   };
   template <is_read_constraint T>
   struct serialized_type<T>
   {
      using type = typename serialized_type<typename T::value_type>::type;
   };
   template <class T>
   using serialized_type_t = typename serialized_type<T>::type;

   template <class T>
   inline constexpr bool document_type = [] {
      if constexpr (transparent_write_wrapper<T>)
         return document_type<serialized_type_t<T>>;
      else
         return (!scalar_type<T> && object<T>) || string_map<T> || std::same_as<T, document> || std::same_as<T, node> ||
                custom_read<T> || custom_write<T>;
   }();

   template <class T>
   inline constexpr bool is_mixed = false;
   template <class A, class P, class C>
   inline constexpr bool is_mixed<mixed<A, P, C>> = true;

   template <class T>
   concept positional = is_std_tuple<T> || is_specialization_v<T, std::pair> || is_specialization_v<T, glz::tuple>;

   template <size_t I, class T>
   decltype(auto) position(T&& x)
   {
      if constexpr (is_specialization_v<std::remove_cvref_t<T>, glz::tuple>)
         return glz::get<I>(std::forward<T>(x));
      else
         return std::get<I>(std::forward<T>(x));
   }

   template <class T>
   constexpr size_t position_count = [] {
      if constexpr (is_specialization_v<T, glz::tuple>)
         return glz::tuple_size_v<T>;
      else
         return std::tuple_size_v<T>;
   }();

   template <class T>
   constexpr std::string_view discriminator_key = [] {
      if constexpr (requires { meta<T>::tag; })
         return std::string_view{meta<T>::tag};
      else
         return std::string_view{"type"};
   }();

   template <class T>
   inline constexpr bool compound = [] {
      if constexpr (is_read_constraint<T>)
         return compound<serialized_type_t<T>>;
      else if constexpr (nullable_like<T>)
         return compound<std::remove_cvref_t<decltype(*std::declval<T>())>>;
      else
         return (!scalar_type<T> && !custom_read<T> && !custom_write<T> && object<T>) || glaze_value_t<T> ||
                is_specialization_v<T, custom_t> || string_map<T> || sequence<T> || positional<T> || is_variant<T> ||
                is_mixed<T> || is_specialization_v<T, lazy> || std::same_as<T, node>;
   }();

   template <auto Opts, class T, class Ctx>
   void read_scalar(T& target, const auto& v, Ctx& ctx, std::string_view source = {});

   template <class To, class From, class Ctx>
   bool checked_duration_cast(const From& source, To& target, Ctx& ctx)
   {
      using Ratio = std::ratio_divide<typename From::period, typename To::period>;
      using Rep = typename To::rep;
      if constexpr (std::same_as<To, From>) {
         target = source;
         return true;
      }
      const auto ticks = (static_cast<long double>(source.count()) * Ratio::num) / Ratio::den;
      if constexpr (std::integral<Rep>) {
         const auto limit = std::ldexp(1.L, std::numeric_limits<Rep>::digits);
         const auto truncated = std::trunc(ticks);
         if (!std::isfinite(ticks) || truncated >= limit || truncated < (std::is_signed_v<Rep> ? -limit : 0.L)) {
            ctx.error = error_code::parse_number_failure;
            return false;
         }
      }
      else if (!std::isfinite(ticks) || ticks > (std::numeric_limits<Rep>::max)() ||
               ticks < -(std::numeric_limits<Rep>::max)()) {
         ctx.error = error_code::parse_number_failure;
         return false;
      }
      target = To{static_cast<Rep>(ticks)};
      return true;
   }

   template <class Ctx>
   std::optional<std::string_view> raw_source_text(const auto& v, std::string_view source, Ctx& ctx)
   {
      if (auto borrowed = raw_text(v)) return borrowed;
      if (!text(v) || source.empty()) return {};
      parser probe{source, ctx};
      std::optional<std::string> annotation;
      if (!probe.tag(annotation)) return {};
      source.remove_prefix(probe.pos);
      size_t hashes{};
      while (hashes < source.size() && source[hashes] == '#') ++hashes;
      if (source.substr(hashes, 1) != "\"") return source;
      const size_t quotes = source.substr(hashes, 3) == "\"\"\"" ? 3 : 1;
      return source.substr(hashes + quotes, source.size() - 2 * (hashes + quotes));
   }

   template <auto Opts, class T, class Ctx>
   void read_duration(T& target, const auto& v, Ctx& ctx)
   {
      using Rep = typename T::rep;
      if (auto s = text(v)) {
         long double count{};
         const auto* begin = s->data();
         const auto* end = begin + s->size();
         if (begin != end && *begin == '+') {
            ++begin;
            if (begin != end && (*begin == '+' || *begin == '-')) {
               ctx.error = error_code::parse_number_failure;
               return;
            }
         }
         const auto r = std::from_chars(begin, end, count);
         const std::string_view unit{r.ptr, size_t(end - r.ptr)};
         uint64_t numerator{}, denominator = 1;
         if (unit == "ns")
            numerator = 1, denominator = 1000000000;
         else if (unit == "us" || unit == "µs")
            numerator = 1, denominator = 1000000;
         else if (unit == "ms")
            numerator = 1, denominator = 1000;
         else if (unit == "s")
            numerator = 1;
         else if (unit == "min")
            numerator = 60;
         else if (unit == "h")
            numerator = 3600;
         else if (unit == "d")
            numerator = 86400;
         else if (unit == "w")
            numerator = 604800;
         if (r.ec != std::errc{} || numerator == 0 || !std::isfinite(count)) {
            ctx.error = error_code::parse_number_failure;
            return;
         }
         if constexpr (std::integral<Rep>) {
            integral_duration(target, std::string_view{begin, size_t(r.ptr - begin)}, numerator, denominator, ctx);
         }
         else {
            const auto ticks = (count * (static_cast<long double>(numerator) * T::period::den)) /
                               (static_cast<long double>(denominator) * T::period::num);
            if (!std::isfinite(ticks) || ticks > (std::numeric_limits<Rep>::max)() ||
                ticks < -(std::numeric_limits<Rep>::max)()) {
               ctx.error = error_code::parse_number_failure;
               return;
            }
            target = T{static_cast<Rep>(ticks)};
         }
      }
      else {
         Rep count{};
         read_scalar<Opts>(count, v, ctx);
         if (!bool(ctx.error)) target = T{count};
      }
   }

   template <auto Opts, class T, class Ctx>
   void read_scalar(T& target, const auto& v, Ctx& ctx, std::string_view source)
   {
      if constexpr (custom_read<T>) {
         if (source.empty()) {
            ctx.error = error_code::syntax_error;
            return;
         }
         const char* it = source.data();
         const auto* end = it + source.size();
         from<KDL, T>::template op<Opts>(target, ctx, it, end);
         if (!bool(ctx.error) && it != end) ctx.error = error_code::syntax_error;
      }
      else if constexpr (is_read_constraint<T>) {
         glz::detail::dispatch_read_constraint<Opts>(target, ctx,
                                                     [&](auto& input) { read_scalar<Opts>(input, v, ctx, source); });
      }
      else if constexpr (is_specialization_v<T, raw_string_t>)
         read_scalar<opt_true<Opts, raw_string_opt_tag{}>>(target.val, v, ctx, source);
      else if constexpr (is_specialization_v<T, escaped_t>)
         read_scalar<opt_false<Opts, raw_string_opt_tag{}>>(target.val, v, ctx, source);
      else if constexpr (is_specialization_v<T, custom_t>) {
         glz::detail::dispatch_custom_read<T>(
            target, ctx, [&](auto& input) { read_scalar<Opts>(input, v, ctx, source); }, [] {});
      }
      else if constexpr (glaze_value_t<T>) {
         auto&& inner = get_member(target, meta_wrapper_v<T>);
         read_scalar<Opts>(inner, v, ctx, source);
      }
      else if constexpr (std::same_as<T, value>)
         target = v;
      else if constexpr (always_null_t<T>) {
         if (!std::holds_alternative<std::monostate>(v.data)) ctx.error = error_code::syntax_error;
      }
      else if constexpr (nullable_like<T>) {
         if (std::holds_alternative<std::monostate>(v.data))
            target = {};
         else if (target || nullable_emplace<Opts>(target, ctx))
            read_scalar<Opts>(*target, v, ctx, source);
      }
      else if constexpr (filesystem_path<T>) {
         if (auto s = text(v))
            target = *s;
         else
            ctx.error = error_code::syntax_error;
      }
      else if constexpr (is_duration<T>)
         read_duration<Opts>(target, v, ctx);
      else if constexpr (is_count_time_point<T>) {
         typename T::duration count{};
         read_duration<Opts>(count, v, ctx);
         if (!bool(ctx.error)) target = T{count};
      }
      else if constexpr (is_calendar_time_point<T> || is_year_month_day<T>) {
         auto s = text(v);
         if (!s) {
            ctx.error = error_code::syntax_error;
            return;
         }
         if constexpr (is_year_month_day<T>)
            chrono_detail::parse_ymd(*s, target, ctx.error);
         else {
            using Duration = typename T::duration;
            if constexpr (std::ratio_equal_v<typename Duration::period, std::ratio<86400>>) {
               if (s->size() == 10) {
                  std::chrono::year_month_day date{};
                  chrono_detail::parse_ymd(*s, date, ctx.error);
                  if (!bool(ctx.error)) target = std::chrono::time_point_cast<Duration>(std::chrono::sys_days{date});
                  return;
               }
            }
            chrono_detail::parse_iso8601(*s, target, ctx.error);
         }
      }
      else if constexpr (is_specialization_v<T, epoch_time>) {
         []<class D>(epoch_time<D>& wrapper, const auto& input, Ctx& context) {
            typename D::rep count{};
            read_scalar<Opts>(count, input, context);
            if (!bool(context.error)) {
               std::chrono::system_clock::duration elapsed{};
               if (checked_duration_cast(D{count}, elapsed, context))
                  wrapper.value = std::chrono::system_clock::time_point{elapsed};
            }
         }(target, v, ctx);
      }
      else if constexpr (std::same_as<T, std::string_view>) {
         const auto s = [&] {
            if constexpr (check_raw_string(Opts))
               return raw_source_text(v, source, ctx);
            else
               return borrowed_text(v);
         }();
         if (s)
            target = *s;
         else
            ctx.error = (raw_text(v) || text(v)) ? error_code::invalid_escape : error_code::syntax_error;
      }
      else if constexpr (std::same_as<T, std::string>) {
         const auto s = [&] {
            if constexpr (check_raw_string(Opts))
               return raw_source_text(v, source, ctx);
            else
               return text(v);
         }();
         if (s)
            target = *s;
         else
            ctx.error = error_code::syntax_error;
      }
      else if constexpr (std::same_as<T, bool>) {
         if (auto b = std::get_if<bool>(&v.data))
            target = *b;
         else
            ctx.error = error_code::syntax_error;
      }
      else if constexpr (std::is_arithmetic_v<T>) {
         std::visit(
            [&](const auto& x) {
               using X = std::decay_t<decltype(x)>;
               if constexpr (std::is_arithmetic_v<X> && !std::same_as<X, bool>) {
                  if constexpr (std::integral<T>) {
                     if constexpr (std::integral<X>) {
                        bool fits{};
                        if constexpr (std::is_signed_v<X>) {
                           fits = x < 0 ? (std::is_signed_v<T> && x >= int64_t(std::numeric_limits<T>::lowest()))
                                        : uint64_t(x) <= uint64_t((std::numeric_limits<T>::max)());
                        }
                        else
                           fits = x <= uint64_t((std::numeric_limits<T>::max)());
                        if (fits)
                           target = static_cast<T>(x);
                        else
                           ctx.error = error_code::parse_number_failure;
                     }
                     else {
                        // Use exclusive powers of two to avoid rounded integer maxima.
                        const auto limit = std::ldexp(1.0, std::numeric_limits<T>::digits);
                        if (std::isfinite(x) && std::trunc(x) == x && x < limit &&
                            x >= (std::is_signed_v<T> ? -limit : 0.0))
                           target = static_cast<T>(x);
                        else
                           ctx.error = error_code::parse_number_failure;
                     }
                  }
                  else {
                     if (std::isfinite(x) && (static_cast<long double>(x) > (std::numeric_limits<T>::max)() ||
                                              static_cast<long double>(x) < -(std::numeric_limits<T>::max)()))
                        ctx.error = error_code::parse_number_failure;
                     else
                        target = static_cast<T>(x);
                  }
               }
               else
                  ctx.error = error_code::syntax_error;
            },
            v.data);
      }
      else if constexpr (is_variant<T>) {
         bool matched{};
         for_each<std::variant_size_v<T>>([&]<size_t I>() {
            using A = std::variant_alternative_t<I, T>;
            if (matched || (v.tag && *v.tag != std::string_view{ids_v<T>[I]})) return;
            if constexpr ((!compound<A> || (transparent_write_wrapper<A> && scalar_type<serialized_type_t<A>>)) &&
                          std::is_default_constructible_v<A>) {
               A candidate{};
               context attempt{};
               read_scalar<Opts>(candidate, v, attempt, source);
               if (!bool(attempt.error)) {
                  target.template emplace<I>(std::move(candidate));
                  matched = true;
               }
            }
         });
         if (!matched) ctx.error = error_code::no_matching_variant_type;
      }
      else if constexpr (glaze_enum_t<T>) {
         auto s = text(v);
         bool found{};
         for_each<reflect<T>::size>([&]<size_t I>() {
            if (s && *s == get<I>(reflect<T>::keys)) target = get<I>(reflect<T>::values), found = true;
         });
         if (!found) ctx.error = error_code::unexpected_enum;
      }
      else if constexpr (std::is_enum_v<T>) {
#if GLZ_REFLECTION26
         if constexpr (check_reflect_enums(Opts) || is_reflect_enum<T>) {
            auto name = text(v);
            const auto result = name ? string_to_enum<T>(*name) : std::optional<T>{};
            if (result)
               target = *result;
            else
               ctx.error = error_code::unexpected_enum;
         }
         else
#endif
         {
            std::underlying_type_t<T> x{};
            read_scalar<Opts>(x, v, ctx);
            if (!bool(ctx.error)) target = static_cast<T>(x);
         }
      }
      else {
         static_assert(sizeof(T) == 0, "Unsupported KDL scalar target");
      }
   }

   template <auto Opts, class T, class Ctx>
   value write_scalar(const T& x, Ctx& ctx)
   {
      value v;
      if constexpr (transparent_write_wrapper<T>) {
         unwrap_write_value(x, ctx, [&](const auto& inner) { v = write_scalar<Opts>(inner, ctx); });
      }
      else if constexpr (filesystem_path<T>)
         v.data = x.string();
      else if constexpr (is_duration<T>)
         return write_scalar<Opts>(x.count(), ctx);
      else if constexpr (is_count_time_point<T>)
         return write_scalar<Opts>(x.time_since_epoch().count(), ctx);
      else if constexpr (is_calendar_time_point<T> || is_year_month_day<T>) {
         std::array<char, 64> data{};
         size_t ix{};
         if constexpr (is_calendar_time_point<T>)
            chrono_detail::write_iso_time_point<false>(x, ctx, data, ix);
         else {
            if (!x.ok()) {
               ctx.error = error_code::constraint_violated;
               return v;
            }
            chrono_detail::write_iso_date<false>(int(x.year()), unsigned(x.month()), unsigned(x.day()), ctx, data, ix);
         }
         if (!bool(ctx.error)) v.data = std::string{data.data(), ix};
      }
      else if constexpr (is_specialization_v<T, epoch_time>) {
         return []<class D>(const epoch_time<D>& wrapper, Ctx& context) {
            D elapsed{};
            if (!checked_duration_cast(wrapper.value.time_since_epoch(), elapsed, context)) return value{};
            return write_scalar<Opts>(elapsed.count(), context);
         }(x, ctx);
      }
      else if constexpr (always_null_t<T>)
         return v;
      else if constexpr (nullable_like<T>) {
         if (x) return write_scalar<Opts>(*x, ctx);
      }
      else if constexpr (std::same_as<T, std::string> || std::same_as<T, std::string_view>)
         v.data = std::string{x};
      else if constexpr (std::same_as<T, bool>)
         v.data = x;
      else if constexpr (std::signed_integral<T>)
         v.data = int64_t(x);
      else if constexpr (std::unsigned_integral<T>)
         v.data = uint64_t(x);
      else if constexpr (std::floating_point<T>) {
         if (std::isfinite(x) &&
             (x > (std::numeric_limits<double>::max)() || x < -(std::numeric_limits<double>::max)()))
            ctx.error = error_code::parse_number_failure;
         else
            v.data = double(x);
      }
      else if constexpr (std::same_as<T, value>)
         return x;
      else if constexpr (is_variant<T>) {
         if (x.valueless_by_exception()) {
            ctx.error = error_code::invalid_variant_object;
            return v;
         }
         std::visit(
            [&](const auto& element) {
               using A = std::decay_t<decltype(element)>;
               if constexpr (!compound<A>)
                  v = write_scalar<Opts>(element, ctx);
               else
                  ctx.error = error_code::invalid_variant_object;
            },
            x);
         v.tag = std::string{ids_v<T>[x.index()]};
      }
      else if constexpr (glaze_enum_t<T>) {
         bool found{};
         for_each<reflect<T>::size>([&]<size_t I>() {
            if (x == get<I>(reflect<T>::values)) v.data = std::string{get<I>(reflect<T>::keys)}, found = true;
         });
         if (!found) ctx.error = error_code::unexpected_enum;
      }
      else if constexpr (std::is_enum_v<T>) {
#if GLZ_REFLECTION26
         if constexpr (check_reflect_enums(Opts) || is_reflect_enum<T>) {
            const auto name = enum_to_string(x);
            if (name.empty())
               ctx.error = error_code::unexpected_enum;
            else
               v.data = std::string{name};
         }
         else
#endif
         {
            v = write_scalar<Opts>(static_cast<std::underlying_type_t<T>>(x), ctx);
         }
      }
      else
         static_assert(sizeof(T) == 0, "Unsupported KDL scalar source");
      return v;
   }

}

namespace glz
{
   // Scalar to/from adapters are available to user serializers, while complete
   // KDL reads/writes retain the document-shaped root contract.
   template <class T>
   inline constexpr bool top_level_supported<KDL, T> = kdl::detail::document_type<T>;
}
