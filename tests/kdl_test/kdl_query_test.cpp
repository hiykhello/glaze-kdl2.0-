// Glaze Library
// For the license information refer to glaze.hpp
#include "glaze/kdl.hpp"
#include "ut/ut.hpp"
using namespace ut;
suite query_tests = [] {
   "native filters projections slices and pipeline"_test = [] {
      auto doc = glz::read_kdl<glz::kdl::document>(
         R"((zone)server { host "a" port=80 enabled=#true; host "b" port=443 enabled=#false; host "c" port=8080 enabled=#true; }; other {host "d" port=80;})");
      expect(bool(doc));
      if (!doc) return;
      auto ports = glz::kdl::query(*doc, "/server/host[?@enabled == #true && @port >= 80]/@port");
      expect(ports && ports->items.size() == 2);
      auto all = glz::kdl::query(*doc, "//host | count()");
      expect(all && all->count == 4);
      auto reverse = glz::kdl::query(*doc, "/server/host[::-1]/#0");
      expect(reverse && reverse->items.size() == 3);
      if (reverse && !reverse->items.empty())
         expect(std::get<std::string>(std::get<const glz::kdl::value*>(reverse->items[0])->data) == "c");
      auto subset = glz::kdl::query(*doc, "/server/host[1:3]/:name | first()");
      expect(subset && subset->items.size() == 1 && std::get<std::string_view>(subset->items[0]) == "host");
      auto last = glz::kdl::query(*doc, "/server/host[-1]/#0");
      expect(last && last->items.size() == 1);
      auto missing = glz::kdl::query(*doc, "//host[?!@enabled || @port == 443] | count()");
      expect(missing && missing->count == 2);
      auto tag = glz::kdl::query(*doc, "/*[?:tag == \"zone\"]/:tag");
      expect(tag && tag->items.size() == 1);
      auto edge = glz::kdl::query(*doc, "/server/host[100::-1] | count()");
      expect(edge && edge->count == 3);
      auto too_negative = glz::kdl::query(*doc, "/server/host[-100] | count()");
      expect(too_negative && too_negative->count == 0);
      auto wire = glz::kdl::query_kdl("x 1; x 2;", "/x/#0 | reverse()");
      expect(wire && bool(glz::read_kdl<glz::kdl::document>(*wire)));
      for (auto invalid : {"bad", "/x[?]", "/x[::0]", "/x[?@a == ]", "/x | unknown()", "/x[?@a &&]"})
         expect(!glz::kdl::compile_query(invalid));
   };
};
int main() {}
