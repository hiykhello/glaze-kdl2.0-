// Glaze Library
// For the license information refer to glaze.hpp

#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <optional>
#include <ranges>
#include <string_view>
#include <type_traits>
#include <variant>

#include "glaze/concepts/container_concepts.hpp"
#include "glaze/core/common.hpp"
#include "glaze/core/constraint.hpp"
#include "glaze/core/context.hpp"
#include "glaze/core/lazy.hpp"
#include "glaze/core/meta.hpp"
#include "glaze/core/opts.hpp"
#include "glaze/core/raw_string.hpp"
#include "glaze/core/reflect.hpp"
#include "glaze/forward.hpp"
#include "glaze/kdl/common.hpp"
#include "glaze/kdl/parse.hpp"
#include "glaze/kdl/types.hpp"
#include "glaze/util/variant.hpp"

namespace glz::kdl::detail
{
   // Only schema-sized bookkeeping lives on the stack. No node or document is
   // constructed when reading a typed target, including skipped subtrees.
   template <class T, bool Nullable = nullable_like<T>>
   struct read_state
   {
      static constexpr size_t field_count = [] {
         if constexpr (object<T> && !custom_read<T>)
            return reflect<T>::size;
         else
            return size_t{};
      }();
      std::array<bool, field_count> seen{};
      size_t count{};
      bool arguments{};
      bool children{};
   };

   template <class T>
   struct read_state<T, true>
   {
      using inner_type = std::remove_cvref_t<decltype(*std::declval<T>())>;
      read_state<inner_type> inner{};
      bool pending_null{};
      bool started{};
   };

   template <class A, class P, class C>
   struct read_state<mixed<A, P, C>, false>
   {
      read_state<A> args{};
      read_state<P> props{};
      read_state<C> child{};
   };

   template <auto Opts, class Ctx>
   struct direct_reader
   {
      parser<Ctx>& p;

      template <class T>
      void prepare(T& target)
      {
         if constexpr (std::same_as<T, kdl::node>)
            target = {};
         else if constexpr (is_mixed<T>) {
            prepare(target.arguments);
            prepare(target.properties);
            prepare(target.children);
            target.tag.reset();
         }
         else if constexpr (string_map<T>)
            target.clear();
         else if constexpr (sequence<T> && resizable<T>)
            target.resize(0);
      }

      template <class T, class Read, class Skip>
      void field(T& target, read_state<T>& state, std::string_view key, Read&& read, Skip&& skip)
      {
         if constexpr (string_map<T>)
            read(target[std::string{key}]);
         else {
            bool found{};
            constexpr auto N = reflect<T>::size;
            if constexpr (N > 0) {
               constexpr auto& hash = hash_info<T>;
               const auto index =
                  decode_hash_with_size<KDL, T, hash, hash.type>::op(key.data(), key.data() + key.size(), key.size());
               if (index < N) {
                  visit<N>(
                     [&]<size_t I>() {
                        if (key != std::string_view{get<I>(reflect<T>::keys)}) return;
                        found = true;
                        state.seen[I] = true;
                        if constexpr (skipped_by_meta<T, I, operation::parse>)
                           skip();
                        else
                           read(member<I>(target));
                     },
                     index);
               }
            }
            if (!found) {
               if constexpr (Opts.error_on_unknown_keys)
                  p.fail(error_code::unknown_key);
               else
                  skip();
            }
         }
      }

      template <class T>
      void nodes(T& target, read_state<T>& state, bool child = false)
      {
         if constexpr (std::same_as<T, kdl::document>)
            p.nodes(target, child);
         else if constexpr (sequence<T>)
            p.nodes_each(
               [&](const auto& name, const auto& annotation) {
                  element(target, state, [&](auto& x) { node(x, name, annotation); });
               },
               child);
         else
            p.nodes_each(
               [&](const std::string& name, const auto& annotation) {
                  field(
                     target, state, name, [&](auto&& member) { node(member, name, annotation); },
                     [&] { p.skip_body(); });
               },
               child);
      }

      template <class T, class Consume>
      void nullable(T& target, read_state<T>& state, Consume&& consume)
      {
         if (!target && !nullable_emplace<Opts>(target, p.ctx)) return;
         if (!state.started) {
            prepare(*target);
            state.started = true;
         }
         if (state.pending_null) {
            state.pending_null = false;
            value null;
            argument(*target, state.inner, null);
         }
         if (!bool(p.ctx.error)) consume(*target, state.inner);
      }

      template <class T, class Consume>
      void element(T& target, read_state<T>& state, Consume&& consume)
      {
         if constexpr (resizable<T>) {
            if (exceeds_capacity(target, state.count + 1, p.ctx)) return;
            target.resize(state.count + 1);
         }
         else if (state.count >= std::size(target)) {
            p.fail(error_code::exceeded_static_array_size);
            return;
         }
         auto&& current = [&]() -> decltype(auto) {
            if constexpr (resizable<T> && requires { target.back(); })
               return target.back();
            else
               return *std::next(std::begin(target), state.count);
         }();
         ++state.count;
         using E = std::ranges::range_value_t<T>;
         if constexpr (std::same_as<std::remove_cvref_t<decltype(current)>, E>)
            consume(current);
         else {
            E temporary{};
            consume(temporary);
            current = std::move(temporary);
         }
      }

      template <class T>
      void argument(T& target, read_state<T>& state, const auto& v, std::string_view source = {})
      {
         if constexpr (is_read_constraint<T>) {
            glz::detail::dispatch_read_constraint<Opts>(target, p.ctx, [&](auto& input) {
               read_state<std::remove_cvref_t<decltype(input)>> inner;
               prepare(input);
               argument(input, inner, v, source);
               finish(input, inner);
            });
            ++state.count;
         }
         else if constexpr (nullable_like<T>) {
            if (!state.started && !state.pending_null && std::holds_alternative<std::monostate>(v.data)) {
               state.pending_null = true;
            }
            else
               nullable(target, state, [&](auto& inner, auto& s) { argument(inner, s, v, source); });
         }
         else if constexpr (is_mixed<T>)
            argument(target.arguments, state.args, v, source);
         else if constexpr (std::same_as<T, kdl::node> || std::same_as<T, kdl::document> ||
                            is_specialization_v<T, lazy>)
            p.fail();
         else if constexpr (positional<T>) {
            if (state.count >= position_count<T>) {
               p.fail(error_code::exceeded_static_array_size);
               return;
            }
            for_each<position_count<T>>([&]<size_t I>() {
               if (I == state.count) read_scalar<Opts>(position<I>(target), v, p.ctx, source);
            });
            ++state.count;
         }
         else if constexpr (custom_read<T>) {
            if (++state.count != 1)
               p.fail();
            else
               read_scalar<Opts>(target, v, p.ctx, source);
         }
         else if constexpr (object<T> || string_map<T>)
            p.fail();
         else if constexpr (sequence<T>) {
            if (state.children) {
               p.fail();
               return;
            }
            state.arguments = true;
            if constexpr (compound<std::ranges::range_value_t<T>>)
               p.fail();
            else
               element(target, state, [&](auto& x) { read_scalar<Opts>(x, v, p.ctx, source); });
         }
         else {
            if (++state.count != 1)
               p.fail();
            else
               read_scalar<Opts>(target, v, p.ctx, source);
         }
      }

      template <class T>
      void property(T& target, read_state<T>& state, std::string_view key, const auto& v, std::string_view source = {})
      {
         if constexpr (nullable_like<T>) {
            nullable(target, state, [&](auto& inner, auto& s) { property(inner, s, key, v, source); });
         }
         else if constexpr (is_mixed<T>)
            property(target.properties, state.props, key, v, source);
         else if constexpr (std::same_as<T, kdl::properties>) {
            const auto found =
               std::find_if(target.begin(), target.end(), [&](const auto& x) { return x.first == key; });
            if (found == target.end())
               target.emplace_back(key, v);
            else
               found->second = v;
         }
         else if constexpr (custom_read<T>)
            p.fail();
         else if constexpr (object<T> || string_map<T>) {
            field(
               target, state, key,
               [&](auto&& x) {
                  using M = std::remove_reference_t<decltype(x)>;
                  if constexpr (always_skipped<M>)
                     return;
                  else if constexpr (std::is_const_v<M>) {
                     if constexpr (check_error_on_const_read(Opts)) p.fail(error_code::attempt_const_read);
                  }
                  else {
                     if constexpr (glaze_value_t<M> || is_specialization_v<M, custom_t>) {
                        read_scalar<Opts>(x, v, p.ctx, source);
                     }
                     else {
                        read_state<M> member_state;
                        prepare(x);
                        argument(x, member_state, v, source);
                        if (!bool(p.ctx.error)) finish(x, member_state);
                     }
                  }
               },
               [] {});
         }
         else
            p.fail();
      }

      template <class T>
      void children(T& target, read_state<T>& state)
      {
         if constexpr (nullable_like<T>) {
            // An empty block has no child values, so a sole #null stays null.
            if (state.pending_null)
               p.nodes_each([&](const auto&, const auto&) { p.fail(); }, true);
            else
               nullable(target, state, [&](auto& inner, auto& s) { children(inner, s); });
         }
         else if constexpr (is_mixed<T>)
            nodes(target.children, state.child, true);
         else if constexpr (custom_read<T>)
            p.nodes_each([&](const auto&, const auto&) { p.fail(); }, true);
         else if constexpr (object<T> || string_map<T> || std::same_as<T, kdl::document>)
            nodes(target, state, true);
         else if constexpr (sequence<T>) {
            p.nodes_each(
               [&](const auto& name, const auto& annotation) {
                  if (state.arguments) {
                     p.fail();
                     return;
                  }
                  state.children = true;
                  element(target, state, [&](auto& x) { node(x, name, annotation); });
               },
               true);
         }
         else {
            // Empty child blocks carry no values, matching the DOM mapping.
            p.nodes_each([&](const auto&, const auto&) { p.fail(); }, true);
         }
      }

      template <class T>
      void finish(T& target, read_state<T>& state)
      {
         if (bool(p.ctx.error)) return;
         if constexpr (nullable_like<T>) {
            if (state.pending_null)
               target = {};
            else
               nullable(target, state, [&](auto& inner, auto& s) { finish(inner, s); });
         }
         else if constexpr (is_mixed<T>) {
            finish(target.arguments, state.args);
            finish(target.properties, state.props);
            finish(target.children, state.child);
         }
         else if constexpr (positional<T>) {
            if (state.count != position_count<T>) p.fail(error_code::exceeded_static_array_size);
         }
         else if constexpr (custom_read<T>) {
            if (state.count != 1) p.fail();
         }
         else if constexpr (object<T> && Opts.error_on_missing_keys) {
            for_each<reflect<T>::size>([&]<size_t I>() {
               if constexpr (required_fields<T, Opts>()[I]) {
                  if (!state.seen[I]) p.fail(error_code::missing_key);
               }
            });
         }
         else if constexpr (sequence<T>) {
            if constexpr (!resizable<T>) {
               if (state.count != std::size(target)) p.fail(error_code::exceeded_static_array_size);
            }
         }
         else if constexpr (!object<T> && !string_map<T>) {
            if (state.count != 1) p.fail();
         }
         if constexpr (object<T> && has_self_constraint_v<T> && !check_skip_self_constraint(Opts)) {
            if (!bool(p.ctx.error)) {
               auto wrapper = self_constraint_v<T>(target);
               const char* it = "";
               from<KDL, decltype(wrapper)>::template op<Opts>(wrapper, p.ctx, it, it);
            }
         }
      }

      template <class T>
      void node(T& target, std::string_view name = {}, const std::optional<std::string>& annotation = {},
                std::string_view ignored_property = {})
      {
         if constexpr (always_skipped<T>)
            p.skip_body();
         else if constexpr (is_specialization_v<T, lazy>) {
            const auto begin = p.node_begin;
            if (p.skip_body()) {
               target.reset(p.input.substr(begin, p.pos - begin), [](auto& inner, std::string_view source) {
                  context ctx{};
                  parser probe{source, ctx};
                  direct_reader<Opts, context> reader{probe};
                  if (probe.validate())
                     probe.nodes_each([&](const auto& key, const auto& tag) { reader.node(inner, key, tag); });
                  return error_ctx{probe.pos, ctx.error, ctx.custom_error_message};
               });
            }
         }
         else if constexpr (std::is_const_v<T>) {
            if constexpr (check_error_on_const_read(Opts))
               p.fail(error_code::attempt_const_read);
            else
               p.skip_body();
         }
         else if constexpr (is_read_constraint<T>) {
            glz::detail::dispatch_read_constraint<Opts>(
               target, p.ctx, [&](auto& input) { node(input, name, annotation, ignored_property); });
         }
         else if constexpr (is_specialization_v<T, raw_string_t>)
            direct_reader<opt_true<Opts, raw_string_opt_tag{}>, Ctx>{p}.node(target.val, name, annotation,
                                                                             ignored_property);
         else if constexpr (is_specialization_v<T, escaped_t>)
            direct_reader<opt_false<Opts, raw_string_opt_tag{}>, Ctx>{p}.node(target.val, name, annotation,
                                                                              ignored_property);
         else if constexpr (is_specialization_v<T, custom_t>) {
            glz::detail::dispatch_custom_read<T>(
               target, p.ctx, [&](auto& inner) { node(inner, name, annotation, ignored_property); },
               [&] { p.skip_body(); });
         }
         else if constexpr (glaze_value_t<T> && !custom_read<T>) {
            auto&& inner = get_member(target, meta_wrapper_v<T>);
            node(inner, name, annotation, ignored_property);
         }
         else if constexpr (std::same_as<T, kdl::node>) {
            prepare(target);
            target.name = name;
            target.tag = annotation;
            p.node_body(target);
         }
         else if constexpr (is_variant<T>)
            variant(target, name, annotation);
         else if constexpr (nullable_like<T> && special_nullable<T>()) {
            auto probe = p;
            size_t count{};
            bool null = true;
            probe.body(
               [&](const auto& v) {
                  ++count;
                  null = null && std::holds_alternative<std::monostate>(v.data);
               },
               [&](const auto&, const auto&) { null = false; },
               [&] {
                  probe.nodes_each(
                     [&](const auto&, const auto&) {
                        null = false;
                        probe.skip_body();
                     },
                     true);
               });
            if (bool(p.ctx.error)) {
               p.pos = probe.pos;
               return;
            }
            if (null && count == 1) {
               target = {};
               p.pos = probe.pos;
            }
            else if (target || nullable_emplace<Opts>(target, p.ctx))
               node(*target, name, annotation, ignored_property);
         }
         else {
            read_state<T> state;
            prepare(target);
            p.body([&](const auto& v, std::string_view source) { argument(target, state, v, source); },
                   [&](std::string_view key, const auto& v, std::string_view source) {
                      if (key != ignored_property || ignored_property.empty()) property(target, state, key, v, source);
                   },
                   [&] { children(target, state); });
            finish(target, state);
            header(target, annotation);
         }
      }

      template <class T>
      static consteval bool special_nullable()
      {
         if constexpr (nullable_like<T>) {
            using Inner = std::remove_cvref_t<decltype(*std::declval<T>())>;
            return std::same_as<Inner, kdl::node> || is_variant<Inner> || special_nullable<Inner>();
         }
         else
            return false;
      }

      template <class T>
      void header(T& target, const std::optional<std::string>& annotation)
      {
         if constexpr (is_mixed<T>)
            target.tag = annotation;
         else if constexpr (nullable_like<T>) {
            if (target) header(*target, annotation);
         }
      }

      template <class T>
      void variant(T& target, std::string_view name, const std::optional<std::string>& annotation)
      {
         // Inspect only the discriminator, retaining no node tree. Parse the
         // selected alternative directly from the original buffer afterwards.
         auto probe = p;
         std::optional<std::string> id;
         bool invalid_id{};
         probe.body([](const auto&) {},
                    [&](std::string_view key, const auto& v) {
                       if (key == discriminator_key<T>) {
                          if (auto s = text(v)) {
                             id = *s;
                             invalid_id = false;
                          }
                          else {
                             id.reset();
                             invalid_id = true;
                          }
                       }
                    },
                    [&] { probe.skip_nodes(true); });
         if (bool(p.ctx.error)) {
            p.pos = probe.pos;
            return;
         }
         if (invalid_id) {
            p.fail(error_code::invalid_variant_object);
            return;
         }
         if (annotation) {
            if (id && *id != *annotation) {
               p.fail(error_code::no_matching_variant_type);
               return;
            }
            id = annotation;
         }
         bool matched{};
         for_each<std::variant_size_v<T>>([&]<size_t I>() {
            if (matched || !id || *id != std::string_view{ids_v<T>[I]}) return;
            matched = true;
            if (target.index() != I) {
               using A = std::variant_alternative_t<I, T>;
               if constexpr (std::is_default_constructible_v<A>)
                  target.template emplace<I>();
               else {
                  p.fail(error_code::invalid_variant_object);
                  return;
               }
            }
            node(std::get<I>(target), name, annotation, discriminator_key<T>);
         });
         if (!matched) p.fail(error_code::no_matching_variant_type);
      }

      template <class T>
      void document(T& target)
      {
         if constexpr (is_specialization_v<T, custom_t>) {
            glz::detail::dispatch_custom_read<T>(
               target, p.ctx, [&](auto& inner) { document(inner); }, [&] { p.skip_nodes(); });
         }
         else if constexpr (glaze_value_t<T> && !custom_read<T>) {
            auto&& inner = get_member(target, meta_wrapper_v<T>);
            document(inner);
         }
         else {
            read_state<T> state;
            prepare(target);
            nodes(target, state);
            finish(target, state);
         }
      }
   };
}
