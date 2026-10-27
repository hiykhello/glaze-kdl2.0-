// Glaze Library
// For the license information refer to glaze.hpp

#pragma once

#include <functional>
#include <optional>
#include <string_view>

#include "glaze/core/context.hpp"
#include "glaze/util/expected.hpp"

namespace glz
{
   // A borrowed, validated source slice. The input must outlive this object and
   // any borrowed fields in the materialized value. Access is not synchronized.
   template <class T>
   class lazy
   {
     public:
      static constexpr bool glaze_reflect = false;
      using value_type = T;
      using reader = error_ctx (*)(T&, std::string_view);

      lazy() = default;
      lazy(T value) : value_(std::move(value)), attempted_(true) {}

      void reset(std::string_view source, reader read)
      {
         source_ = source;
         read_ = read;
         value_.reset();
         error_ = {};
         attempted_ = false;
      }
      [[nodiscard]] std::string_view source() const { return source_; }
      [[nodiscard]] bool materialized() const { return value_.has_value() && !error_; }
      [[nodiscard]] expected<std::reference_wrapper<T>, error_ctx> get()
      {
         materialize();
         if (error_) return unexpected(error_);
         return std::ref(*value_);
      }
      [[nodiscard]] expected<std::reference_wrapper<const T>, error_ctx> get() const
      {
         materialize();
         if (error_) return unexpected(error_);
         return std::cref(*value_);
      }

     private:
      void materialize() const
      {
         if (!attempted_) {
            attempted_ = true;
            if (!read_)
               error_.ec = error_code::no_read_input;
            else {
               value_.emplace();
               error_ = read_(*value_, source_);
            }
         }
      }

      std::string_view source_{};
      reader read_{};
      mutable std::optional<T> value_{};
      mutable error_ctx error_{};
      mutable bool attempted_{};
   };
}
