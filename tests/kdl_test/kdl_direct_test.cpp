// Glaze Library
// For the license information refer to glaze.hpp

#include <array>
#include <cstdlib>
#include <iostream>
#include <new>
#include <span>

#include "glaze/kdl.hpp"

namespace
{
   bool count_allocations{};
   size_t allocations{};
}

// A numeric schema and preallocated buffers need no heap allocation. Counting
// allocations catches an accidental intermediate document, even when its output
// would be identical. Long comments and many skipped nodes must not change this.
void* operator new(std::size_t size)
{
   if (count_allocations) ++allocations;
   if (void* p = std::malloc(size ? size : 1)) return p;
   std::abort();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
#if defined(__cpp_sized_deallocation)
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
#endif

struct numeric_config
{
   int port{};
   std::array<int, 3> values{};
   std::vector<int> dynamic{};
};

struct borrowed_config
{
   std::string_view plain{};
   std::string_view encoded{};
};
template <>
struct glz::meta<borrowed_config>
{
   static constexpr auto value =
      object("plain", &borrowed_config::plain, "encoded", glz::raw_string<&borrowed_config::encoded>);
};

int main()
{
   numeric_config value{};
   value.dynamic.reserve(4);
   std::string input = "/*" + std::string(100000, 'x') + "*/\n";
   input += "/- disabled {\n";
   for (size_t i = 0; i < 10000; ++i) input += " n 1 key=2 { c 3 }\n";
   input += "}\nunknown {\n";
   for (size_t i = 0; i < 10000; ++i) input += " n 1 key=2 { c 3 }\n";
   input += "}\nport /* inline */ 80 // trailing\nvalues 1 /- 999 2 3\ndynamic 4 5 6 7\n";

   count_allocations = true;
   const auto read_ec = glz::read<glz::opts{.format = glz::KDL, .error_on_unknown_keys = false}>(value, input);
   count_allocations = false;
   const auto read_allocations = allocations;
   if (read_ec || read_allocations || value.port != 80 || value.values != std::array{1, 2, 3} ||
       value.dynamic != std::vector{4, 5, 6, 7}) {
      std::cerr << "Direct read failed: " << read_allocations << " allocations, error " << int(read_ec.ec) << '\n';
      return 1;
   }

   std::array<char, 256> buffer{};
   allocations = 0;
   count_allocations = true;
   const auto write_ec = glz::write_kdl(value, std::span{buffer});
   count_allocations = false;
   if (write_ec || allocations ||
       std::string_view{buffer.data(), write_ec.count} != "port 80\nvalues 1 2 3\ndynamic 4 5 6 7\n") {
      std::cerr << "Direct write failed: " << allocations << " allocations, error " << int(write_ec.ec) << '\n';
      return 1;
   }
   borrowed_config borrowed{};
   const auto borrowed_input =
      "plain \"" + std::string(65536, 'p') + "\"; encoded \"" + std::string(32768, 'e') + "\\n\\u{4e2d}\"";
   allocations = 0;
   count_allocations = true;
   const auto borrow_ec = glz::read_kdl(borrowed, borrowed_input);
   count_allocations = false;
   if (borrow_ec || allocations || borrowed.plain.data() != borrowed_input.data() + 7 ||
       borrowed.encoded.find("\\n") == std::string_view::npos) {
      std::cerr << "Borrowed KDL read failed: " << allocations << " allocations, error " << int(borrow_ec.ec) << '\n';
      return 1;
   }
   const auto multiline_input = "plain \"\"\"\n    " + std::string(65536, 'm') + "\n    \"\"\"";
   allocations = 0;
   count_allocations = true;
   const auto multiline_ec = glz::read_kdl(borrowed, multiline_input);
   count_allocations = false;
   if (multiline_ec || allocations || borrowed.plain.size() != 65536 ||
       borrowed.plain.data() != multiline_input.data() + 14) {
      std::cerr << "Multiline borrowed read failed: " << allocations << " allocations\n";
      return 1;
   }
   std::cout << "Direct KDL read/write: zero allocations with preallocated targets and buffers\n";
}
