// Glaze Library
// For the license information refer to glaze.hpp

#include <memory>
#include <random>
#include <span>

#include "glaze/kdl.hpp"
#include "ut/ut.hpp"

#if defined(__unix__) && !defined(__EMSCRIPTEN__)
#include <sys/mman.h>
#include <unistd.h>
#endif

using namespace ut;
namespace scan = glz::kdl::detail::scan;

// Run every classifier at all register/tail offsets, including every byte value.
template <scan::kind Kind>
bool differential()
{
   constexpr char fill = Kind == scan::kind::spaces || Kind == scan::kind::lines ? ' ' : 'a';
   auto check = [](std::string_view input, size_t begin) {
      size_t expected = begin;
      while (expected < input.size() && !scan::stop<Kind>(uint8_t(input[expected]))) ++expected;
      return expected == scan::run<Kind>(input, begin);
   };
   for (size_t size : {0u, 1u, 15u, 16u, 17u, 31u, 32u, 33u, 63u, 64u, 65u, 127u, 128u, 129u, 130u}) {
      auto data = std::make_unique<char[]>(size);
      std::fill_n(data.get(), size, fill);
      const std::string_view input{data.get(), size};
      for (size_t begin = 0; begin <= size; ++begin)
         if (!check(input, begin)) return false;
      for (size_t pos = 0; pos < size; ++pos) {
         for (unsigned byte = 0; byte < 256; ++byte) {
            data[pos] = char(byte);
            if (!check(input, 0)) return false;
         }
         data[pos] = fill;
      }
   }
   std::mt19937 rng{42};
   std::string random(1024, fill);
   for (size_t trial = 0; trial < 256; ++trial) {
      for (auto& c : random) c = char(rng() & 255);
      for (size_t begin = 0; begin <= random.size(); ++begin)
         if (!check(random, begin)) return false;
   }
   return true;
}

struct scan_config
{
   std::string text{};
   int port{};
   bool operator==(const scan_config&) const = default;
};
suite scan_tests = [] {
   "SIMD masks match scalar at every byte and boundary"_test = [] {
      expect(differential<scan::kind::validation>());
      expect(differential<scan::kind::quoted>());
      expect(differential<scan::kind::raw>());
      expect(differential<scan::kind::line_comment>());
      expect(differential<scan::kind::block_comment>());
      expect(differential<scan::kind::spaces>());
      expect(differential<scan::kind::lines>());
      expect(differential<scan::kind::token>());
      expect(differential<scan::kind::identifier>());
   };
   "long strings comments Unicode and sized buffers"_test = [] {
      for (size_t offset = 0; offset < 129; ++offset) {
         const std::string text = std::string(offset, 'a') + "中文\\\"\n" + std::string(2048, 'z');
         scan_config original{text, 443};
         auto out = glz::write_kdl(original);
         expect(bool(out));
         if (!out) continue;
         const auto input = "/*" + std::string(offset + 256, 'c') + "/*nested*/end*/\n//" +
                            std::string(offset + 128, 'l') + "\xC2\x85" + *out;
         auto result = glz::read_kdl<scan_config>(input);
         expect(result && *result == original);
         std::vector<char> exact(out->size() + 1, '!');
         const auto ec = glz::write_kdl(original, std::span{exact.data(), out->size()});
         expect(!ec && exact.back() == '!' && std::string_view{exact.data(), out->size()} == *out);
         auto truncated = glz::write_kdl(original, std::span{exact.data(), out->size() - 1});
         expect(truncated == glz::error_code::buffer_overflow);
         const auto invalid = std::string(offset + 128, ' ') + "\xff";
         scan_config c{};
         const auto error = glz::read_kdl(c, invalid);
         expect(error && error.count == offset + 128);
      }
   };
#if defined(__unix__) && !defined(__EMSCRIPTEN__)
   "un-padded inputs ending at a protected page"_test = [] {
      const auto page = size_t(sysconf(_SC_PAGESIZE));
      auto* memory =
         static_cast<char*>(mmap(nullptr, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
      expect(memory != MAP_FAILED);
      if (memory == MAP_FAILED) return;
      const auto protected_page = mprotect(memory + page, page, PROT_NONE);
      expect(protected_page == 0);
      if (protected_page == 0) {
         for (size_t size = 0; size < 256; ++size) {
            const std::string input = "text \"" + std::string(size, 'a') + "\"";
            auto* data = memory + page - input.size();
            std::copy(input.begin(), input.end(), data);
            const auto result = glz::read_kdl<scan_config>(std::string_view{data, input.size()});
            expect(result && result->text == std::string(size, 'a'));
         }
      }
      munmap(memory, page * 2);
   };
#endif
};
int main() {}
