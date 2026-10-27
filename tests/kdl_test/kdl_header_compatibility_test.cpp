// Glaze Library
// For the license information refer to glaze.hpp

// Match the project's Windows header CI contract without requiring Windows:
// public KDL headers must compile with min/max macros and preserve them.
#include <numeric>

#include "glaze/glaze.hpp"
#define min(a, b) kdl_unexpected_min_macro(a, b)
#define max(a, b) kdl_unexpected_max_macro(a, b)
#include "glaze/exceptions/kdl_exceptions.hpp"
#include "glaze/kdl.hpp"
#ifndef min
#error min macro was not preserved
#endif
#ifndef max
#error max macro was not preserved
#endif
int main()
{
   glz::kdl::document doc;
   auto text = glz::write_kdl(doc);
   return !text;
}
