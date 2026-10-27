// Glaze Library
// For the license information refer to glaze.hpp

#include <filesystem>
#include <fstream>
#include <iostream>

#include "glaze/kdl.hpp"

namespace
{
   struct ignored_document
   {};

   long double numeric(const glz::kdl::value& v)
   {
      return std::visit(
         [](const auto& x) -> long double {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_arithmetic_v<T>)
               return static_cast<long double>(x);
            else if constexpr (std::same_as<T, glz::kdl::number>) {
               std::string text;
               for (char c : x.text)
                  if (c != '_') text += c;
               std::string_view s{text};
               const bool negative = s.starts_with('-');
               if (s.starts_with('-') || s.starts_with('+')) s.remove_prefix(1);
               int base{};
               if (s.starts_with("0x"))
                  base = 16;
               else if (s.starts_with("0o"))
                  base = 8;
               else if (s.starts_with("0b"))
                  base = 2;
               long double result{};
               if (base) {
                  s.remove_prefix(2);
                  for (char c : s) result = result * base + glz::kdl::detail::hex(c);
               }
               else {
                  const auto parsed = std::from_chars(s.data(), s.data() + s.size(), result);
                  // Platforms with a double-sized long double cannot represent the corpus's
                  // extreme exponents. Equal spellings are compared before reaching here.
                  if (parsed.ec != std::errc{}) return std::numeric_limits<long double>::quiet_NaN();
               }
               return negative ? -result : result;
            }
            else
               return std::numeric_limits<long double>::quiet_NaN();
         },
         v.data);
   }

   bool equal(const glz::kdl::value& a, const glz::kdl::value& b)
   {
      if (a.tag != b.tag) return false;
      if (a.data == b.data) return true;
      if (std::holds_alternative<std::string>(a.data) || std::holds_alternative<std::string>(b.data) ||
          std::holds_alternative<bool>(a.data) || std::holds_alternative<bool>(b.data) ||
          std::holds_alternative<std::monostate>(a.data) || std::holds_alternative<std::monostate>(b.data))
         return false;
      const auto x = numeric(a), y = numeric(b);
      if (std::isnan(x) || std::isnan(y)) {
         const auto* dx = std::get_if<double>(&a.data);
         const auto* dy = std::get_if<double>(&b.data);
         return dx && dy && std::isnan(*dx) && std::isnan(*dy);
      }
      if (x == y) return true;
      if (!std::isfinite(x) || !std::isfinite(y)) return false;
      return std::abs(x - y) <= std::max(std::abs(x), std::abs(y)) * 1e-14L;
   }

   bool equal(const glz::kdl::document& a, const glz::kdl::document& b)
   {
      if (a.size() != b.size()) return false;
      for (size_t i = 0; i < a.size(); ++i) {
         const auto& x = a[i];
         const auto& y = b[i];
         if (x.name != y.name || x.tag != y.tag || x.arguments.size() != y.arguments.size() ||
             x.properties.size() != y.properties.size() || !equal(x.children, y.children))
            return false;
         for (size_t j = 0; j < x.arguments.size(); ++j)
            if (!equal(x.arguments[j], y.arguments[j])) return false;
         for (const auto& [key, value] : x.properties) {
            const auto found =
               std::find_if(y.properties.begin(), y.properties.end(), [&](const auto& p) { return p.first == key; });
            if (found == y.properties.end() || !equal(value, found->second)) return false;
         }
      }
      return true;
   }

   std::string load(const std::filesystem::path& file)
   {
      std::ifstream input(file, std::ios::binary);
      return {std::istreambuf_iterator<char>{input}, {}};
   }
}

int main(int argc, char** argv)
{
   if (argc != 2) return 2;
   const std::filesystem::path root{argv[1]};
   size_t total{}, failed{};
   for (const auto& file : std::filesystem::directory_iterator(root / "input")) {
      if (file.path().extension() != ".kdl") continue;
      ++total;
      const auto name = file.path().filename();
      const bool invalid = file.path().stem().string().ends_with("_fail");
      const auto input = load(file.path());
      auto result = glz::read_kdl<glz::kdl::document>(input);
      ignored_document ignored;
      const auto skipped = glz::read<glz::opts{.format = glz::KDL, .error_on_unknown_keys = false}>(ignored, input);
      bool ok = bool(result) != invalid && bool(skipped) == invalid;
      if (ok && result) {
         auto expected = glz::read_kdl<glz::kdl::document>(load(root / "expected_kdl" / name));
         ok = expected && equal(*result, *expected);
         auto serialized = glz::write_kdl(*result);
         auto reparsed = serialized
                            ? glz::read_kdl<glz::kdl::document>(*serialized)
                            : glz::expected<glz::kdl::document, glz::error_ctx>{glz::unexpected(serialized.error())};
         ok = ok && reparsed && equal(*result, *reparsed);
      }
      for (size_t chunk : {size_t{1}, size_t{7}, size_t{8192}}) {
         glz::kdl::stream_reader stream;
         glz::kdl::document streamed;
         auto consume = [&](auto record) { streamed.push_back(std::move(record.value)); };
         glz::error_ctx error;
         for (size_t i = 0; i < input.size() && !error; i += chunk)
            error = stream.feed(std::string_view{input}.substr(i, chunk), consume);
         if (!error) error = stream.finish(consume);
         if (bool(error) != invalid || (result && !equal(*result, streamed))) {
            std::cerr << name << ": streaming failure with chunk " << chunk << " at " << error.count << "\n";
            ok = false;
         }
      }
      if (!ok) {
         ++failed;
         std::cerr << name << ": conformance failure\n";
      }
   }
   std::cout << total << " KDL cases, " << failed << " failures\n";
   return total == 0 || failed != 0;
}
