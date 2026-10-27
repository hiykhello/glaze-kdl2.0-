// Glaze Library
// For the license information refer to glaze.hpp

#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include "glaze/core/context.hpp"
#include "glaze/kdl/common.hpp"
#include "glaze/kdl/read.hpp"
#include "glaze/kdl/write.hpp"
#include "glaze/util/expected.hpp"

namespace glz::kdl
{
   enum class patch_kind { insert, erase, replace, name, tag, arguments, properties };
   struct patch_operation
   {
      patch_kind kind{};
      // Absolute sibling indices, preserving repeated names and ordering.
      std::vector<size_t> path;
      node payload;
   };
   using patch = std::vector<patch_operation>;

   inline patch diff(const document& before, const document& after)
   {
      patch result;
      std::vector<size_t> path;
      auto walk = [&](auto&& self, const document& a, const document& b) -> void {
         const auto common = (std::min)(a.size(), b.size());
         for (size_t i = 0; i < common; ++i) {
            path.push_back(i);
            auto add = [&](patch_kind kind) -> node& {
               result.push_back({kind, path, {}});
               return result.back().payload;
            };
            if (a[i].name != b[i].name) add(patch_kind::name).name = b[i].name;
            if (a[i].tag != b[i].tag) add(patch_kind::tag).tag = b[i].tag;
            if (a[i].arguments != b[i].arguments) add(patch_kind::arguments).arguments = b[i].arguments;
            if (a[i].properties != b[i].properties) add(patch_kind::properties).properties = b[i].properties;
            self(self, a[i].children, b[i].children);
            path.pop_back();
         }
         for (size_t i = a.size(); i > common; --i) {
            path.push_back(i - 1);
            result.push_back({patch_kind::erase, path, {}});
            path.pop_back();
         }
         for (size_t i = common; i < b.size(); ++i) {
            path.push_back(i);
            result.push_back({patch_kind::insert, path, b[i]});
            path.pop_back();
         }
      };
      walk(walk, before, after);
      return result;
   }

   // Transactional: a failed operation leaves the caller's document intact.
   [[nodiscard]] inline error_ctx apply_patch(document& target, const patch& changes)
   {
      auto copy = target;
      for (size_t i = 0; i < changes.size(); ++i) {
         const auto& op = changes[i];
         if (op.path.empty()) return {i, error_code::invalid_query};
         auto* siblings = &copy;
         for (size_t j = 0; j + 1 < op.path.size(); ++j) {
            if (op.path[j] >= siblings->size()) return {i, error_code::key_not_found};
            siblings = &(*siblings)[op.path[j]].children;
         }
         const auto index = op.path.back();
         if (op.kind == patch_kind::insert) {
            if (index > siblings->size()) return {i, error_code::key_not_found};
            siblings->insert(siblings->begin() + index, op.payload);
            continue;
         }
         if (index >= siblings->size()) return {i, error_code::key_not_found};
         auto& n = (*siblings)[index];
         switch (op.kind) {
         case patch_kind::erase:
            siblings->erase(siblings->begin() + index);
            break;
         case patch_kind::replace:
            n = op.payload;
            break;
         case patch_kind::name:
            n.name = op.payload.name;
            break;
         case patch_kind::tag:
            n.tag = op.payload.tag;
            break;
         case patch_kind::arguments:
            n.arguments = op.payload.arguments;
            break;
         case patch_kind::properties:
            n.properties = op.payload.properties;
            break;
         default:
            return {i, error_code::invalid_body};
         }
      }
      target = std::move(copy);
      return {};
   }

   [[nodiscard]] inline expected<std::string, error_ctx> write_patch(const patch& changes)
   {
      constexpr std::array names{"insert",  "erase",         "replace",       "set-name",
                                 "set-tag", "set-arguments", "set-properties"};
      document wire;
      for (const auto& op : changes) {
         const auto kind = static_cast<size_t>(op.kind);
         if (kind >= names.size() || op.path.empty()) return unexpected(error_ctx{0, error_code::invalid_body});
         node n;
         n.name = names[kind];
         std::string path;
         for (auto index : op.path) {
            path += '/';
            path += std::to_string(index);
         }
         n.arguments.push_back({std::move(path), {}});
         if (op.kind == patch_kind::insert || op.kind == patch_kind::replace)
            n.children.push_back(op.payload);
         else if (op.kind == patch_kind::name)
            n.arguments.push_back({op.payload.name, {}});
         else if (op.kind == patch_kind::tag) {
            value v;
            if (op.payload.tag) v.data = *op.payload.tag;
            n.arguments.push_back(std::move(v));
         }
         else if (op.kind == patch_kind::arguments)
            n.arguments.insert(n.arguments.end(), op.payload.arguments.begin(), op.payload.arguments.end());
         else if (op.kind == patch_kind::properties)
            n.properties = op.payload.properties;
         wire.push_back(std::move(n));
      }
      return write_kdl(wire);
   }

   [[nodiscard]] inline expected<patch, error_ctx> read_patch(std::string_view source)
   {
      auto wire = read_kdl<document>(source);
      if (!wire) return unexpected(wire.error());
      constexpr std::array names{"insert",  "erase",         "replace",       "set-name",
                                 "set-tag", "set-arguments", "set-properties"};
      patch result;
      for (size_t i = 0; i < wire->size(); ++i) {
         const auto& n = (*wire)[i];
         auto bad = [&] { return unexpected(error_ctx{i, error_code::invalid_body}); };
         auto found = std::find(names.begin(), names.end(), n.name);
         if (found == names.end() || n.tag || n.arguments.empty() || n.arguments[0].tag) return bad();
         const auto* text = std::get_if<std::string>(&n.arguments[0].data);
         if (!text || text->empty() || text->front() != '/') return bad();
         patch_operation op{static_cast<patch_kind>(found - names.begin()), {}, {}};
         size_t begin = 1;
         while (true) {
            const auto slash = text->find('/', begin);
            auto token =
               std::string_view{*text}.substr(begin, slash == text->npos ? text->size() - begin : slash - begin);
            size_t index{};
            auto r = std::from_chars(token.data(), token.data() + token.size(), index);
            if (token.empty() || r.ec != std::errc{} || r.ptr != token.data() + token.size()) return bad();
            op.path.push_back(index);
            if (slash == text->npos) break;
            begin = slash + 1;
         }
         if (op.kind != patch_kind::properties && !n.properties.empty()) return bad();
         if (op.kind == patch_kind::insert || op.kind == patch_kind::replace) {
            if (n.children.size() != 1 || n.arguments.size() != 1) return bad();
            op.payload = n.children[0];
         }
         else {
            if (!n.children.empty()) return bad();
            if (op.kind == patch_kind::name || op.kind == patch_kind::tag) {
               if (n.arguments.size() != 2 || n.arguments[1].tag) return bad();
               const auto* s = std::get_if<std::string>(&n.arguments[1].data);
               if (op.kind == patch_kind::name) {
                  if (!s) return bad();
                  op.payload.name = *s;
               }
               else if (s)
                  op.payload.tag = *s;
               else if (!std::holds_alternative<std::monostate>(n.arguments[1].data))
                  return bad();
            }
            else if (op.kind == patch_kind::arguments)
               op.payload.arguments.assign(n.arguments.begin() + 1, n.arguments.end());
            else if (n.arguments.size() != 1)
               return bad();
            if (op.kind == patch_kind::properties) op.payload.properties = n.properties;
         }
         result.push_back(std::move(op));
      }
      return result;
   }
}
