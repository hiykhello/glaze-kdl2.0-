// Glaze Library
// For the license information refer to glaze.hpp

#include "glaze/kdl.hpp"
#include "ut/ut.hpp"

using namespace ut;

static_assert(glz::has_reflection26);

// These classes deliberately have no glz::meta and cannot use aggregate reflection.
class reflected_endpoint
{
   std::string host_{};
   int port_{};

  public:
   reflected_endpoint() = default;
   reflected_endpoint(std::string host, int port) : host_(std::move(host)), port_(port) {}
   bool operator==(const reflected_endpoint&) const = default;
};

struct reflected_arrays
{
   int numbers[3]{};
   double matrix[2][2]{};
   reflected_endpoint endpoints[2]{};
};

struct reflected_options : glz::opts
{
   bool reflect_enums = true;
   constexpr reflected_options() : glz::opts{.format = glz::KDL} {}
};

enum class reflected_mode { first = -1, second = 4, alias = 4 };
enum class named_mode { first, second };

template <>
struct glz::meta<named_mode>
{
   using enum named_mode;
   static constexpr auto value = enumerate("one", first, "two", second);
};

enum class automatic_mode { development, production };
template <>
struct glz::meta<automatic_mode> : glz::reflect_enum
{};
struct automatic_modes
{
   automatic_mode mode{};
   std::unique_ptr<automatic_mode> pointer{};
};

struct reflected_modes
{
   reflected_mode mode{};
   std::optional<reflected_mode> optional{};
   std::vector<reflected_mode> sequence{};
   named_mode named{};
   bool operator==(const reflected_modes&) const = default;
};

struct renamed_endpoint
{
   int port{};
};

template <>
struct glz::meta<renamed_endpoint>
{
   using T = renamed_endpoint;
   static constexpr auto value = object("listen-port", &T::port);
};

suite kdl_reflection_tests = [] {
   "private non-aggregate members"_test = [] {
      static_assert(!std::is_aggregate_v<reflected_endpoint>);
      static_assert(glz::reflect<reflected_endpoint>::size == 2);
      static_assert(glz::member_names<reflected_endpoint>[0] == "host_");
      reflected_endpoint value{};
      expect(!glz::read_kdl(value, "host_ localhost; port_ 8080"));
      expect(value == reflected_endpoint{"localhost", 8080});
      auto text = glz::write_kdl(value);
      expect(text && *text == "host_ \"localhost\"\nport_ 8080\n");
      if (text) {
         auto result = glz::read_kdl<reflected_endpoint>(*text);
         expect(result && *result == value);
      }
   };

   "C arrays and multidimensional arrays"_test = [] {
      reflected_arrays value{};
      expect(!glz::read_kdl(value, R"(
         numbers 1 2 3
         matrix { - 1.5 2.5; - 3.5 4.5 }
         endpoints { - host_=first port_=80; - host_=second port_=443 }
      )"));
      expect(value.numbers[0] == 1 && value.numbers[2] == 3);
      expect(value.matrix[0][1] == 2.5 && value.matrix[1][0] == 3.5);
      expect(value.endpoints[0] == reflected_endpoint{"first", 80});
      expect(value.endpoints[1] == reflected_endpoint{"second", 443});
      auto text = glz::write_kdl(value);
      expect(bool(text));
      if (!text) return;
      auto result = glz::read_kdl<reflected_arrays>(*text);
      expect(bool(result));
      if (!result) return;
      expect(std::ranges::equal(value.numbers, result->numbers));
      expect(std::ranges::equal(value.matrix[0], result->matrix[0]));
      expect(std::ranges::equal(value.matrix[1], result->matrix[1]));
      expect(std::ranges::equal(value.endpoints, result->endpoints));
      expect(glz::read_kdl(value, "numbers 1 2") == glz::error_code::exceeded_static_array_size);
      expect(glz::read_kdl(value, "matrix { - 1 2 3; - 4 5 }") == glz::error_code::exceeded_static_array_size);
      expect(glz::read_kdl(value, "endpoints { - host_=one port_=1 }") == glz::error_code::exceeded_static_array_size);
   };

   "reflected enums and metadata precedence"_test = [] {
      reflected_modes value{};
      expect(!glz::read<reflected_options{}>(value, "mode second; optional first; sequence first second; named two"));
      expect(value.mode == reflected_mode::second && value.optional == reflected_mode::first);
      expect(value.sequence == std::vector{reflected_mode::first, reflected_mode::second});
      expect(value.named == named_mode::second);
      auto text = glz::write<reflected_options{}>(value);
      expect(text && text->find("mode \"second\"") != std::string::npos);
      expect(text && text->find("named \"two\"") != std::string::npos);
      if (text) {
         reflected_modes result{};
         expect(!glz::read<reflected_options{}>(result, *text));
         expect(result == value);
      }
      expect(!glz::read<reflected_options{}>(value, "mode alias"));
      expect(value.mode == reflected_mode::second);
      for (auto input : {"mode missing", "mode 4", "mode #null"}) {
         expect(glz::read<reflected_options{}>(value, input) == glz::error_code::unexpected_enum);
      }
      value.mode = static_cast<reflected_mode>(99);
      auto invalid = glz::write<reflected_options{}>(value);
      expect(!invalid && invalid.error() == glz::error_code::unexpected_enum);
      // With the option off, unnamed values still use the underlying integer.
      auto numeric = glz::write_kdl(value);
      expect(numeric && numeric->find("mode 99") != std::string::npos);
      expect(!glz::read_kdl(value, "mode -1"));
      expect(value.mode == reflected_mode::first);
      expect(glz::read_kdl(value, "mode first") == glz::error_code::syntax_error);
   };

   "reflect enum marker without option macros"_test = [] {
      static_assert(glz::is_reflect_enum<automatic_mode>);
      automatic_modes value{};
      expect(!glz::read_kdl(value, "mode production; pointer development"));
      expect(value.mode == automatic_mode::production && value.pointer &&
             *value.pointer == automatic_mode::development);
      auto out = glz::write_kdl(value);
      expect(out && out->find("mode \"production\"") != std::string::npos);
      if (out) expect(bool(glz::read_kdl<automatic_modes>(*out)));
      expect(glz::read_kdl(value, "mode invalid") == glz::error_code::unexpected_enum);
      expect(glz::read_kdl(value, "mode 1") == glz::error_code::unexpected_enum);
   };

   "schema export uses static reflection"_test = [] {
      auto enums = glz::kdl::write_schema<automatic_modes>();
      expect(enums && enums->find("development") != std::string::npos &&
             enums->find("production") != std::string::npos);
      auto object = glz::kdl::write_schema<reflected_endpoint>();
      expect(object && object->find("host_") != std::string::npos && object->find("port_") != std::string::npos);
   };

   "metadata object names still win"_test = [] {
      renamed_endpoint value{};
      expect(!glz::read_kdl(value, "listen-port 443"));
      expect(value.port == 443);
      expect(glz::write_kdl(value).value_or("") == "listen-port 443\n");
   };
};

int main() {}
