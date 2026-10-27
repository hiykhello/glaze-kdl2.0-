// Glaze Library
// For the license information refer to glaze.hpp

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include "glaze/core/context.hpp"
#include "glaze/core/opts.hpp"
#include "glaze/core/reflect.hpp"
#include "glaze/forward.hpp"
#include "glaze/kdl/common.hpp"
#include "glaze/kdl/parse.hpp"
#include "glaze/kdl/read.hpp" // IWYU pragma: keep
#include "glaze/kdl/read_direct.hpp"
#include "glaze/kdl/write.hpp"
#include "glaze/util/expected.hpp"
#include "glaze/util/type_traits.hpp"

namespace glz::kdl
{
   namespace detail
   {
      template <class T>
      struct rpc_inputs;
      template <class... Args>
      struct rpc_inputs<std::tuple<Args...>>
      {
         using type = std::tuple<std::remove_cvref_t<Args>...>;
      };
   }

   // Local dispatch only; transport and service lifetime belong to the caller.
   // Request: sum 2 3 id="r1"   or   sum x=2 y=3 id="r1"
   // Structured parameters: method id="r1" { parameter { ... } }
   class rpc_registry
   {
      using handler = std::function<expected<std::string, error_ctx>(std::string_view)>;
      std::unordered_map<std::string, handler> methods_;

     public:
      template <auto Method, class Service>
      rpc_registry& bind(
         std::string name, Service& service,
         std::array<std::string_view, std::tuple_size_v<typename inputs_as_tuple<decltype(Method)>::type>> names = {})
      {
         using Declared = typename inputs_as_tuple<decltype(Method)>::type;
         using Inputs = typename detail::rpc_inputs<Declared>::type;
         using Return = typename return_type<decltype(Method)>::type;
         constexpr auto count = std::tuple_size_v<Inputs>;
         // Copy names: registration must not borrow temporary caller strings.
         std::array<std::string, count> owned_names;
         for (size_t i = 0; i < count; ++i) owned_names[i] = names[i];
         methods_.insert_or_assign(
            std::move(name),
            [&service, count,
             owned_names = std::move(owned_names)](std::string_view source) -> expected<std::string, error_ctx> {
               Inputs inputs{};
               std::array<bool, count> seen{};
               context ctx{};
               detail::parser p{source, ctx};
               detail::direct_reader<opts{.format = KDL}, context> reader{p};
               std::optional<value> id;
               size_t positional{};
               auto assign = [&](size_t index, const auto& scalar, std::string_view raw) {
                  if (!std::cmp_less(index, count) || seen[index]) {
                     p.fail(error_code::invalid_body);
                     return;
                  }
                  seen[index] = true;
                  for_each<count>([&]<size_t I>() {
                     if (I != index) return;
                     using T = std::tuple_element_t<I, Inputs>;
                     detail::read_state<T> state;
                     reader.prepare(std::get<I>(inputs));
                     reader.argument(std::get<I>(inputs), state, scalar, raw);
                     reader.finish(std::get<I>(inputs), state);
                  });
               };
               p.nodes_each([&](const auto&, const auto& tag) {
                  if (tag) {
                     p.fail(error_code::invalid_body);
                     return;
                  }
                  p.body([&](const auto& scalar, std::string_view raw) { assign(positional++, scalar, raw); },
                         [&](std::string_view key, const auto& scalar, std::string_view raw) {
                            if (key == "id") {
                               if (id)
                                  p.fail(error_code::invalid_body);
                               else
                                  id = static_cast<value>(scalar);
                               return;
                            }
                            size_t index{};
                            while (std::cmp_less(index, count) && owned_names[index] != key) ++index;
                            assign(index, scalar, raw);
                         },
                         [&] {
                            p.nodes_each(
                               [&](const auto& key, const auto& annotation) {
                                  size_t index{};
                                  while (std::cmp_less(index, count) && owned_names[index] != key) ++index;
                                  if (index == count || seen[index]) {
                                     p.fail(error_code::invalid_body);
                                     return;
                                  }
                                  seen[index] = true;
                                  for_each<count>([&]<size_t I>() {
                                     if (index == I) reader.node(std::get<I>(inputs), key, annotation);
                                  });
                               },
                               true);
                         });
               });
               if (ctx.error == error_code::none && std::find(seen.begin(), seen.end(), false) != seen.end())
                  p.fail(error_code::missing_key);
               if (ctx.error != error_code::none) return unexpected(error_ctx{p.pos, ctx.error});
               auto invoke = [&]<size_t... I>(std::index_sequence<I...>) -> decltype(auto) {
                  return std::invoke(Method, service,
                                     static_cast<std::tuple_element_t<I, Declared>&&>(std::get<I>(inputs))...);
               };
               std::string output;
               size_t ix{};
               detail::writer<opts{.format = KDL}, context, std::string, size_t> writer{ctx, output, ix};
               writer.append("(ok)reply");
               if (id) {
                  writer.append(" id=");
                  writer.scalar(*id);
               }
               writer.append(" {\n");
               if constexpr (std::is_void_v<Return>) {
                  invoke(std::make_index_sequence<count>{});
                  writer.append("   result #null\n");
               }
               else {
                  decltype(auto) result = invoke(std::make_index_sequence<count>{});
                  if constexpr (is_expected<std::remove_cvref_t<Return>>) {
                     if (!result) return unexpected(error_ctx{0, error_code::invalid_call});
                     if constexpr (std::is_void_v<typename std::remove_cvref_t<Return>::value_type>)
                        writer.append("   result #null\n");
                     else
                        writer.typed_node("result", *result, 1);
                  }
                  else
                     writer.typed_node("result", result, 1);
               }
               writer.append("}\n");
               if (ctx.error != error_code::none) return unexpected(error_ctx{ix, ctx.error});
               output.resize(ix);
               return output;
            });
         return *this;
      }

      // Explicit metadata may expose member functions alongside data fields.
      template <class Service>
      rpc_registry& bind(Service& service)
      {
         for_each<reflect<Service>::size>([&]<size_t I>() {
            constexpr auto member = glz::get<I>(reflect<Service>::values);
            if constexpr (std::is_member_function_pointer_v<std::remove_cvref_t<decltype(member)>>)
               bind<member>(std::string{glz::get<I>(reflect<Service>::keys)}, service);
         });
         return *this;
      }

      [[nodiscard]] expected<std::string, error_ctx> call(std::string_view request) const
      {
         // Validate the complete request before any user function can run.
         context ctx{};
         detail::parser p{request, ctx};
         std::string method;
         size_t count{};
         if (p.validate())
            p.nodes_each([&](const auto& name, const auto&) {
               method = name;
               ++count;
               p.skip_body();
            });
         if (ctx.error != error_code::none) return unexpected(error_ctx{p.pos, ctx.error});
         if (count != 1) return unexpected(error_ctx{p.pos, error_code::invalid_body});
         const auto found = methods_.find(method);
         if (found == methods_.end()) return unexpected(error_ctx{0, error_code::method_not_found});
         return found->second(request);
      }

      // Wire-level errors remain native KDL. This helper is suitable for transports
      // that require one response even when validation or dispatch fails.
      std::string respond(std::string_view request) const
      {
         auto response = call(request);
         if (response) return std::move(*response);
         document error{
            {"reply",
             std::string{"error"},
             {},
             {{"code", {uint64_t(response.error().ec), {}}}, {"offset", {uint64_t(response.error().count), {}}}},
             {}}};
         auto wire = write_kdl(error);
         return wire ? std::move(*wire) : std::string{};
      }
   };
}
