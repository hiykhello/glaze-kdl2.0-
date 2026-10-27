// Glaze Library
// For the license information refer to glaze.hpp

#pragma once

#if __cpp_exceptions

#include <stdexcept>

#include "glaze/exceptions/core_exceptions.hpp"
#include "glaze/kdl.hpp"

namespace glz::ex
{
   template <read_supported<KDL> T, is_buffer Buffer>
   void read_kdl(T& value, Buffer&& buffer)
   {
      const auto ec = glz::read_kdl(value, std::forward<Buffer>(buffer));
      if (ec) throw std::runtime_error("read_kdl error: " + glz::format_error(ec, buffer));
   }

   template <read_supported<KDL> T, is_buffer Buffer>
   [[nodiscard]] T read_kdl(Buffer&& buffer)
   {
      auto result = glz::read_kdl<T>(std::forward<Buffer>(buffer));
      if (!result) throw std::runtime_error("read_kdl error: " + glz::format_error(result.error(), buffer));
      return std::move(*result);
   }

   template <auto Opts = opts{.format = KDL}, read_supported<KDL> T, is_buffer Buffer>
   void read_file_kdl(T& value, const sv file_name, Buffer&& buffer)
   {
      const auto ec = glz::read_file_kdl<Opts>(value, file_name, buffer);
      if (ec)
         throw std::runtime_error(
            "read_file_kdl error for " + std::string{file_name} + ": " +
            (ec == error_code::file_open_failure ? glz::format_error(ec) : glz::format_error(ec, buffer)));
   }

   template <write_supported<KDL> T, output_buffer Buffer>
   void write_kdl(T&& value, Buffer&& buffer)
   {
      const auto ec = glz::write_kdl(std::forward<T>(value), buffer);
      if (ec) throw std::runtime_error("write_kdl error: " + glz::format_error(ec));
   }

   template <write_supported<KDL> T, raw_buffer Buffer>
   [[nodiscard]] size_t write_kdl(T&& value, Buffer&& buffer)
   {
      const auto ec = glz::write_kdl(std::forward<T>(value), std::forward<Buffer>(buffer));
      if (ec) throw std::runtime_error("write_kdl error: " + glz::format_error(ec));
      return ec.count;
   }

   template <write_supported<KDL> T>
   [[nodiscard]] std::string write_kdl(T&& value)
   {
      auto result = glz::write_kdl(std::forward<T>(value));
      if (!result) throw std::runtime_error("write_kdl error: " + glz::format_error(result.error()));
      return std::move(*result);
   }

   template <auto Opts = opts{.format = KDL}, write_supported<KDL> T, output_buffer Buffer>
   void write_file_kdl(T&& value, const sv file_name, Buffer&& buffer)
   {
      const auto ec = glz::write_file_kdl<Opts>(std::forward<T>(value), file_name, buffer);
      if (ec)
         throw std::runtime_error("write_file_kdl error for " + std::string{file_name} + ": " + glz::format_error(ec));
   }
}

#endif
