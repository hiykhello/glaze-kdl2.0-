// Glaze Library
// For the license information refer to glaze.hpp

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace glz::kdl
{
   // Numbers outside native storage retain their validated KDL spelling.
   struct number
   {
      static constexpr bool glaze_reflect = false;
      std::string text{};
      bool operator==(const number&) const = default;
   };

   struct value
   {
      static constexpr bool glaze_reflect = false;
      using data_type = std::variant<std::monostate, bool, int64_t, uint64_t, double, std::string, number>;
      data_type data{};
      std::optional<std::string> tag{};
      bool operator==(const value&) const = default;
   };

   struct node
   {
      static constexpr bool glaze_reflect = false;
      std::string name{};
      std::optional<std::string> tag{};
      std::vector<value> arguments{};
      // Duplicate property keys are resolved by the reader: the last value wins.
      std::vector<std::pair<std::string, value>> properties{};
      std::vector<node> children{};
      bool operator==(const node&) const = default;
   };

   using document = std::vector<node>;
   using properties = std::vector<std::pair<std::string, value>>;

   // Each section may be typed or dynamic. The enclosing member supplies the name.
   template <class Arguments = std::vector<value>, class Properties = properties, class Children = document>
   struct mixed
   {
      static constexpr bool glaze_reflect = false;
      Arguments arguments{};
      Properties properties{};
      Children children{};
      std::optional<std::string> tag{};
      bool operator==(const mixed&) const = default;
   };
}
