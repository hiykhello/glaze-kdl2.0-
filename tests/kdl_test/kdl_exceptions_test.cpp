// Glaze Library
// For the license information refer to glaze.hpp

#include "glaze/exceptions/kdl_exceptions.hpp"

#include <span>

#include "glaze/glaze_exceptions.hpp"
#include "scratch_directory.hpp"
#include "ut/ut.hpp"

using namespace ut;

struct error_config
{
   int port{};
   std::string host{};
};
static const glz_test::scratch_directory scratch{"kdl_exceptions_test"};

suite error_tests = [] {
   "named throwing reads"_test = [] {
      auto c = glz::ex::read_kdl<error_config>("port 80; host local");
      expect(c.port == 80 && c.host == "local");
      glz::ex::read_kdl(c, "port 443");
      expect(c.port == 443);
      const std::string_view text = "port bad";
      try {
         glz::ex::read_kdl(c, text);
         expect(false);
      }
      catch (const std::runtime_error& e) {
         error_config other{};
         const auto ec = glz::read_kdl(other, text);
         expect(std::string{e.what()} == "read_kdl error: " + glz::format_error(ec, text));
      }
      expect(throws([] { [[maybe_unused]] auto result = glz::ex::read_kdl<error_config>("port bad"); }));
      expect(throws([&] { glz::ex::read<glz::opts{.format = glz::KDL}>(c, "port bad"); }));
   };
   "throwing writes and raw buffers"_test = [] {
      error_config c{80, "local"};
      const auto text = glz::ex::write_kdl(c);
      std::string buffer;
      glz::ex::write_kdl(c, buffer);
      expect(buffer == text);
      std::array<char, 128> raw{};
      const auto size = glz::ex::write_kdl(c, raw.data());
      expect(std::string_view{raw.data(), size} == text);
      const auto generic_size = glz::ex::write<glz::opts{.format = glz::KDL}>(c, raw.data());
      expect(generic_size == size);
      expect(glz::ex::write<glz::opts{.format = glz::KDL}>(c) == text);
      std::array<char, 1> small{};
      expect(throws([&] { glz::ex::write_kdl(c, std::span{small}); }));
      c.host = std::string{"invalid\xff"};
      expect(throws([&] { [[maybe_unused]] auto result = glz::ex::write_kdl(c); }));
      expect(throws([&] { glz::ex::write_kdl(c, buffer); }));
      expect(throws([&] { glz::ex::write<glz::opts{.format = glz::KDL}>(c, buffer); }));
      expect(throws([&] { [[maybe_unused]] auto result = glz::ex::write_kdl(c, raw.data()); }));
   };
   "file failures and parse diagnostics"_test = [] {
      error_config c{80, "local"};
      std::string buffer;
      glz::ex::write_file_kdl(c, "good.kdl", buffer);
      error_config result{};
      glz::ex::read_file_kdl(result, "good.kdl", buffer);
      expect(result.port == 80 && result.host == "local");
      expect(throws([&] { glz::ex::read_file_kdl(result, "missing.kdl", buffer); }));
      expect(throws([&] { glz::ex::write_file_kdl(c, "missing-directory/file.kdl", buffer); }));
      buffer = "port bad";
      expect(!bool(glz::buffer_to_file(buffer, "bad.kdl")));
      try {
         glz::ex::read_file_kdl(result, "bad.kdl", buffer);
         expect(false);
      }
      catch (const std::runtime_error& e) {
         expect(std::string_view{e.what()}.find("bad.kdl") != std::string::npos);
         expect(std::string_view{e.what()}.find("syntax_error") != std::string::npos);
      }
      buffer = "host local; unknown 1";
      expect(!bool(glz::buffer_to_file(buffer, "extra.kdl")));
      glz::ex::read_file_kdl<glz::opts{.error_on_unknown_keys = false}>(result, "extra.kdl", buffer);
   };
};

int main() {}
