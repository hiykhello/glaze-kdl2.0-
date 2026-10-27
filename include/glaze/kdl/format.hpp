// Glaze Library
// For the license information refer to glaze.hpp

#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "glaze/core/context.hpp"
#include "glaze/core/opts.hpp"
#include "glaze/forward.hpp"
#include "glaze/kdl/common.hpp"
#include "glaze/kdl/read.hpp"
#include "glaze/kdl/write.hpp"
#include "glaze/util/expected.hpp"

namespace glz::kdl
{
   // Canonical semantic formatting: comments and original number/string spelling
   // need not survive; tags, repeated nodes and argument/child order do survive.
   template <auto Opts = opts{.format = KDL}>
   [[nodiscard]] expected<std::string, error_ctx> prettify(std::string_view source)
   {
      auto doc = read_kdl<document>(source);
      if (!doc) return unexpected(doc.error());
      return write<set_kdl<Opts>()>(*doc);
   }

   [[nodiscard]] inline expected<std::string, error_ctx> minify(std::string_view source)
   {
      auto doc = read_kdl<document>(source);
      if (!doc) return unexpected(doc.error());
      context ctx{};
      std::string output;
      size_t ix{};
      detail::writer<opts{.format = KDL}, context, std::string, size_t> writer{ctx, output, ix};
      auto nodes = [&](auto&& self, const document& values) -> void {
         for (const auto& n : values) {
            writer.tag(n.tag);
            writer.name(n.name);
            for (const auto& arg : n.arguments) {
               writer.append(" ");
               writer.scalar(arg);
            }
            for (const auto& [key, v] : n.properties) {
               writer.append(" ");
               writer.name(key);
               writer.append("=");
               writer.scalar(v);
            }
            if (!n.children.empty()) {
               writer.append(" {");
               self(self, n.children);
               writer.append("}");
            }
            writer.append(";");
         }
      };
      nodes(nodes, *doc);
      if (ctx.error != error_code::none) return unexpected(error_ctx{ix, ctx.error});
      output.resize(ix);
      return output;
   }
}
