// Glaze Library
// For the license information refer to glaze.hpp

#pragma once

#include <concepts>
#include <cstddef>
#include <string_view>
#include <utility>

#include "glaze/core/common.hpp"
#include "glaze/core/context.hpp"
#include "glaze/core/meta.hpp"
#include "glaze/core/opts.hpp"
#include "glaze/core/read.hpp"
#include "glaze/file/file_ops.hpp"
#include "glaze/forward.hpp"
#include "glaze/kdl/common.hpp"
#include "glaze/kdl/parse.hpp"
#include "glaze/kdl/read_direct.hpp"
#include "glaze/kdl/token.hpp"
#include "glaze/kdl/types.hpp"
#include "glaze/util/expected.hpp"
#include "glaze/util/string_literal.hpp"

namespace glz
{
   template <>
   struct parse<KDL>
   {
      template <auto Opts, class T, class Ctx, class It, class End>
      static void op(T& value, Ctx& ctx, It& it, End end)
      {
         from<KDL, T>::template op<Opts>(value, ctx, it, end);
      }
   };

   template <class T>
      requires(kdl::detail::document_type<T> && !custom_read<T>)
   struct from<KDL, T>
   {
      template <auto Opts, class Ctx, class It, class End>
      static void op(T& value, Ctx& ctx, It& it, End end)
      {
         kdl::detail::parser parser{std::string_view{it, size_t(end - it)}, ctx};
         if (parser.validate()) {
            if constexpr (std::same_as<T, kdl::document>) {
               kdl::document document;
               if (parser.nodes(document)) value = std::move(document);
            }
            else {
               kdl::detail::direct_reader<Opts, Ctx> reader{parser};
               if constexpr (std::same_as<T, kdl::node>) {
                  size_t count{};
                  parser.nodes_each([&](const auto& name, const auto& annotation) {
                     if (++count != 1)
                        parser.fail();
                     else
                        reader.node(value, name, annotation);
                  });
                  if (!count) parser.fail(error_code::no_read_input);
               }
               else
                  reader.document(value);
            }
         }
         it += parser.pos;
      }
   };

   // A scalar adapter for low-level custom serializers. A public KDL document
   // still needs a document-shaped root (see top_level_supported).
   template <class T>
      requires((kdl::detail::scalar_type<T> || glaze_value_t<T>) && !custom_read<T> && !kdl::detail::document_type<T>)
   struct from<KDL, T>
   {
      template <auto Opts, class V, class Ctx, class It, class End>
      static void op(V&& target, Ctx& ctx, It& it, End end)
      {
         kdl::detail::parser p{std::string_view{it, size_t(end - it)}, ctx};
         kdl::detail::scalar_token scalar;
         if (p.validate() && p.entry(scalar)) kdl::detail::read_scalar<Opts>(target, scalar, ctx);
         it += p.pos;
      }
   };

   template <read_supported<KDL> T, is_buffer Buffer>
   [[nodiscard]] error_ctx read_kdl(T& value, Buffer&& buffer)
   {
      return read<opts{.format = KDL}>(value, std::forward<Buffer>(buffer));
   }

   template <read_supported<KDL> T, is_buffer Buffer>
   [[nodiscard]] expected<T, error_ctx> read_kdl(Buffer&& buffer)
   {
      T value{};
      const auto ec = read_kdl(value, std::forward<Buffer>(buffer));
      if (ec) return unexpected(ec);
      return value;
   }

   template <auto Opts = opts{.format = KDL}, read_supported<KDL> T, is_buffer Buffer>
   [[nodiscard]] error_ctx read_file_kdl(T& value, const sv file_name, Buffer&& buffer)
   {
      const auto ec = file_to_buffer(buffer, file_name);
      if (bool(ec)) return {0, ec};
      return read<set_kdl<Opts>()>(value, buffer);
   }
}
