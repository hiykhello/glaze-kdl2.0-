// Glaze Library
// For the license information refer to glaze.hpp
#include <sstream>

#include "glaze/kdl.hpp"
#include "ut/ut.hpp"
using namespace ut;
struct stream_config
{
   int x{};
   std::string_view text{};
   glz::lazy<int> later{};
};
suite streaming_tests = [] {
   "every chunk boundary preserves native semantics"_test = [] {
      const std::string input = R"kdl(// preamble
/- ignored 42
(tag)first "hello\" world" key=##"raw " # text"## {
  nested """
    multiple
    lines
    """
  /* outer /* inner */ comment */ child 3
}
second 1 \
  2; third #true
)kdl";
      auto expected = glz::read_kdl<glz::kdl::document>(input);
      expect(bool(expected));
      if (!expected) return;
      for (size_t size = 1; size <= input.size(); ++size) {
         glz::kdl::stream_reader reader;
         glz::kdl::document actual;
         auto consume = [&](auto record) { actual.push_back(std::move(record.value)); };
         for (size_t i = 0; i < input.size(); i += size)
            expect(!reader.feed(std::string_view{input}.substr(i, size), consume));
         expect(!reader.finish(consume));
         expect(actual == *expected);
      }
   };
   "typed records retain borrowed and lazy input"_test = [] {
      glz::kdl::stream_reader<stream_config> reader;
      std::vector<glz::kdl::stream_record<stream_config>> records;
      auto consume = [&](auto record) { records.push_back(std::move(record)); };
      for (int i = 0; i < 200; ++i) {
         expect(!reader.feed("record { x 42; text \"borrowed\"; later 7; }\n", consume));
         expect(reader.buffered_bytes() == 0);
      }
      expect(!reader.finish(consume));
      expect(records.size() == 200);
      if (!records.empty()) {
         expect(records[0].value.text == "borrowed");
         expect(records[0].value.later.get()->get() == 7);
      }
   };
   "istream errors and unicode continuations"_test = [] {
      std::istringstream input{"a 1\r\nb 2\\\r\n 3\n"};
      glz::kdl::document doc;
      expect(!glz::kdl::read_stream(input, [&](auto record) { doc.push_back(std::move(record.value)); }));
      expect(doc.size() == 2 && doc[1].arguments.size() == 2);
      glz::kdl::stream_reader reader;
      auto consume = [](auto) {};
      expect(!reader.feed("a {", consume));
      expect(bool(reader.finish(consume)));
      glz::kdl::stream_reader bad;
      expect(bool(bad.feed("a 1\nb #invalid\n", consume)));
   };
};
int main() {}
