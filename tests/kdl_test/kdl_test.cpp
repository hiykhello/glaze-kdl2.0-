// Glaze Library
// For the license information refer to glaze.hpp

#include "glaze/kdl.hpp"

#include <array>
#include <list>
#include <map>
#include <optional>
#include <span>

#include "glaze/containers/inplace_vector.hpp"
#include "minimal_buffer.hpp"
#include "scratch_directory.hpp"
#include "ut/ut.hpp"

using namespace ut;

struct endpoint
{
   std::string host{};
   int port{};
   bool operator==(const endpoint&) const = default;
};

struct config
{
   std::string title{};
   bool enabled{};
   std::vector<int> numbers{};
   endpoint server{};
   std::vector<endpoint> peers{};
   std::map<std::string, int> limits{};
   std::optional<std::string> note{};
   bool operator==(const config&) const = default;
};

struct explicit_meta
{
   int count{};
   std::string name{};
};

template <>
struct glz::meta<explicit_meta>
{
   using T = explicit_meta;
   static constexpr auto value = object("total", &T::count, "label", &T::name);
};

struct explicit_arrays
{
   int numbers[2]{};
   int matrix[2][2]{};
};

template <>
struct glz::meta<explicit_arrays>
{
   using T = explicit_arrays;
   static constexpr auto value = object("numbers", &T::numbers, "matrix", &T::matrix);
};

struct skipped
{
   int visible{};
   int hidden{};
};

template <>
struct glz::meta<skipped>
{
   static constexpr bool skip(std::string_view key, const meta_context&) { return key == "hidden"; }
};

enum class color { red, blue };
template <>
struct glz::meta<color>
{
   using enum color;
   static constexpr auto value = enumerate(red, blue);
};

struct palette
{
   color c{};
};

struct containers
{
   std::array<int, 2> fixed{};
   std::vector<std::vector<int>> nested{};
   std::vector<bool> flags{};
   std::optional<endpoint> server{};
};

struct strings
{
   std::string text{};
};

struct numbers
{
   int64_t low{};
   uint64_t high{};
   double d{};
   int small{};
};

struct optional_sequences
{
   std::optional<std::vector<std::optional<int>>> values{};
   std::optional<std::vector<endpoint>> peers{};
};

struct sequence_limits
{
   glz::inplace_vector<int, 2> values{};
   std::list<int> linked{};
};

struct recursive_config
{
   int value{};
   std::vector<recursive_config> children{};
};

static const glz_test::scratch_directory scratch{"kdl_test"};

suite kdl_tests = [] {
   "document model"_test = [] {
      const std::string_view source = R"kdl(
         /- ignored 1 { child; }
         (service)server host=localhost port=(u16)8080 port=9090 /- secret=hidden {
            label "hello\s世界\u{1f600}"
            flags #true #false #null #inf #-inf #nan
         }
      )kdl";
      auto doc = glz::read_kdl<glz::kdl::document>(source);
      expect(bool(doc));
      if (!doc) return;
      expect(doc->size() == 1);
      const auto& n = (*doc)[0];
      expect(n.name == "server");
      expect(n.tag == "service");
      expect(n.properties.size() == 2);
      expect(std::get<int64_t>(n.properties[1].second.data) == 9090);
      expect(!n.properties[1].second.tag);
      expect(std::get<std::string>(n.children[0].arguments[0].data) == "hello 世界😀");
      const auto& args = n.children[1].arguments;
      expect(std::get<bool>(args[0].data));
      expect(!std::get<bool>(args[1].data));
      expect(std::holds_alternative<std::monostate>(args[2].data));
      expect(std::isinf(std::get<double>(args[3].data)));
      expect(std::get<double>(args[4].data) < 0);
      expect(std::isnan(std::get<double>(args[5].data)));
      auto out = glz::write_kdl(*doc);
      expect(bool(out));
      if (out) expect(bool(glz::read_kdl<glz::kdl::document>(*out)));
   };

   "native object roundtrip"_test = [] {
      config value{"demo", true, {1, 2, 3}, {"localhost", 8080}, {{"one", 80}, {"two", 443}}, {{"cpu", 4}}, "hi"};
      auto text = glz::write_kdl(value);
      expect(bool(text));
      if (!text) return;
      auto result = glz::read_kdl<config>(*text);
      expect(bool(result));
      if (result) expect(*result == value);
      config properties{};
      expect(!glz::read_kdl(properties, "server host=example port=443; numbers 4 5; note #null"));
      expect(properties.server == endpoint{"example", 443});
      expect(properties.numbers == std::vector{4, 5});
      expect(!properties.note);
   };

   "metadata and format dispatch"_test = [] {
      explicit_meta value{};
      expect(!glz::read<glz::opts{.format = glz::KDL}>(value, "total 7; label test"));
      expect(value.count == 7);
      expect(value.name == "test");
      auto out = glz::write<glz::opts{.format = glz::KDL}>(value);
      expect(out && *out == "total 7\nlabel \"test\"\n");
      skipped s{1, 2};
      expect(!glz::read_kdl(s, "visible 3; hidden 4"));
      expect(s.visible == 3 && s.hidden == 2);
      expect(glz::write_kdl(s).value_or("") == "visible 3\n");
      palette p{};
      expect(!glz::read_kdl(p, "c blue"));
      expect(p.c == color::blue);
      expect(glz::write_kdl(p).value_or("") == "c \"blue\"\n");
   };

   "containers and optional"_test = [] {
      containers value{};
      expect(!glz::read_kdl(value, "fixed 1 2; nested { - 3 4; - 5 }; flags #true #false; server host=x port=2"));
      expect(value.fixed == std::array{1, 2});
      expect(value.nested == std::vector<std::vector<int>>{{3, 4}, {5}});
      expect(value.flags == std::vector<bool>{true, false});
      expect(value.server == std::optional{endpoint{"x", 2}});
      auto out = glz::write_kdl(value);
      expect(bool(out));
      if (out) {
         decltype(value) result{};
         expect(!glz::read_kdl(result, *out));
         expect(result.nested == value.nested && result.flags == value.flags && result.server == value.server);
      }
      expect(bool(glz::read_kdl(value, "fixed 1 2 3")));
      expect(!glz::read_kdl(value, "server #null"));
      expect(!value.server);
   };

   "C arrays with explicit metadata"_test = [] {
      explicit_arrays value{};
      expect(!glz::read_kdl(value, "numbers 1 2; matrix { - 3 4; - 5 6 }"));
      expect(value.numbers[0] == 1 && value.numbers[1] == 2);
      expect(value.matrix[0][1] == 4 && value.matrix[1][0] == 5);
      auto text = glz::write_kdl(value);
      expect(bool(text));
      if (text) {
         auto result = glz::read_kdl<explicit_arrays>(*text);
         expect(result && result->matrix[1][1] == 6 && result->numbers[1] == 2);
      }
      expect(glz::read_kdl(value, "numbers 1 2 3") == glz::error_code::exceeded_static_array_size);
   };

   "strings and multiline"_test = [] {
      strings v{};
      expect(!glz::read_kdl(v, "text \"\"\"\r\n  one \\\r\ntwo\r\n  three\r\n  \\   \"\"\""));
      expect(v.text == "one two\nthree");
      expect(!glz::read_kdl(v, "text #\"\"\"\n  a\\b\n  \"\"\"#"));
      expect(v.text == "a\\b");
      expect(!glz::read_kdl(v, "text \"\"\"\n\t\"\"\""));
      expect(v.text.empty());
      v.text = std::string{"\0\x7f", 2} + "\u202e\ufeff";
      auto out = glz::write_kdl(v);
      expect(bool(out));
      if (out) {
         auto result = glz::read_kdl<strings>(*out);
         expect(result && result->text == v.text);
      }
   };

   "comments continuations and unicode whitespace"_test = [] {
      auto doc = glz::read_kdl<glz::kdl::document>(
         "\xef\xbb\xbf/* outer /* inner */ */node\u00a0 1 \\ // continuation\n 2 /- 3 { a } /- { b }\u2028");
      expect(bool(doc));
      if (!doc) return;
      expect(doc->size() == 1 && (*doc)[0].arguments.size() == 2 && (*doc)[0].children.size() == 1);
   };

   "typed configuration comments"_test = [] {
      const std::string_view input = R"kdl(
         // Application configuration
         title "https://example/*literal*/" // trailing comment
         enabled /* outer /* nested */ comment */ #true
         numbers 1 /- 999 2 \ // continue the argument list
            3
         /- unknown-node invalid-for-the-schema { nested 1 2 3 }
         server host=localhost /- port=bad port=8080 /- { unused }
         peers {
            /- - host=disabled port=bad
            - host="// literal" port=443
         }
         limits { cpu 4 }
         /- note "commented out"
      )kdl";
      config value{};
      expect(!glz::read_kdl(value, input));
      expect(value.title == "https://example/*literal*/" && value.enabled);
      expect(value.numbers == std::vector{1, 2, 3});
      expect(value.server == endpoint{"localhost", 8080});
      expect(value.peers == std::vector{endpoint{"// literal", 443}});
      expect(value.limits == std::map<std::string, int>{{"cpu", 4}} && !value.note);
      std::string buffer{input};
      expect(!bool(glz::buffer_to_file(buffer, "commented.kdl")));
      config file_value{};
      expect(!glz::read_file_kdl(file_value, "commented.kdl", buffer));
      expect(file_value == value);
      auto text = glz::write_kdl(value);
      expect(text && bool(glz::read_kdl<config>(*text)));
   };

   "direct skip validation and error locations"_test = [] {
      endpoint value{};
      constexpr auto options = glz::opts{.format = glz::KDL, .error_on_unknown_keys = false};
      expect(!glz::read<options>(value, R"(ignored key=1 { child (x)1; child2 #"raw"# }; host local; port 80)"));
      expect(value == endpoint{"local", 80});
      for (auto input : {"ignored { bad 0x_1 }", "ignored { node a= }", "ignored /* unclosed",
                         "/- ignored { node 1e_1 }", "/- ignored { node", "host good; port 80; /* open"}) {
         expect(bool(glz::read<options>(value, input))) << input;
      }
      skipped meta{};
      expect(!glz::read_kdl(meta, "hidden key=value { child 1 }; visible 3"));
      expect(glz::read_kdl(meta, "hidden { child 1e_1 }") == glz::error_code::syntax_error);
      const std::string_view bad = "host local; port nope";
      const auto ec = glz::read_kdl(value, bad);
      expect(bool(ec) && ec.count == bad.size());
      // Like the other typed readers, successful earlier fields remain assigned on error.
      expect(value.host == "local");
   };

   "direct duplicates and required fields"_test = [] {
      config value{};
      expect(!glz::read_kdl(value, "server host=first port=1 port=2 { port 3 }; numbers 1 2; numbers 4 5"));
      expect(value.server == endpoint{"first", 3});
      expect(value.numbers == std::vector{4, 5});
      constexpr auto required = glz::opts{.format = glz::KDL, .error_on_missing_keys = true};
      expect(!glz::read<required>(value, R"(
         title x; enabled #true; numbers; peers; limits
         server host=x { port 80 }
      )"));
      // A slashdashed field does not satisfy a required member.
      expect(glz::read<required>(value.server, "host x; /- port 80") == glz::error_code::missing_key);
      expect(glz::read<required>(value, "title x; enabled #true; numbers; peers; limits; server host=x") ==
             glz::error_code::missing_key);
   };

   "direct nullable sequences and capacity"_test = [] {
      optional_sequences value{};
      expect(!glz::read_kdl(value, "values #null 1 #null; peers { - host=x port=80 }"));
      expect(value.values && *value.values == std::vector<std::optional<int>>{{}, 1, {}});
      expect(value.peers && *value.peers == std::vector{endpoint{"x", 80}});
      auto text = glz::write_kdl(value);
      expect(bool(text));
      if (text) {
         auto result = glz::read_kdl<optional_sequences>(*text);
         expect(result && result->values == value.values && result->peers == value.peers);
      }
      expect(!glz::read_kdl(value, "values #null; peers #null"));
      expect(!value.values && !value.peers);
      expect(!glz::read_kdl(value, "values #null {}; peers #null { /- ignored }"));
      expect(!value.values && !value.peers);
      expect(!glz::read_kdl(value, "values; peers {}"));
      expect(value.values && value.values->empty() && value.peers && value.peers->empty());
      sequence_limits limits{};
      expect(!glz::read_kdl(limits, "values 1 2; linked 3 4 5"));
      expect(limits.values.size() == 2 && limits.linked == std::list{3, 4, 5});
      expect(glz::read_kdl(limits, "values 1 2 3") == glz::error_code::exceeded_static_array_size);
      expect(!glz::read_kdl(limits, "values 4"));
      expect(limits.values.size() == 1 && limits.values[0] == 4);
   };

   "numbers and range validation"_test = [] {
      numbers v{};
      expect(!glz::read_kdl(v, "low -0x8000000000000000; high 18446744073709551615; d +1_2.5e+1; small -0b10"));
      expect(v.low == std::numeric_limits<int64_t>::min());
      expect(v.high == std::numeric_limits<uint64_t>::max());
      expect(v.d == 125 && v.small == -2);
      for (auto text : {"small 2147483648", "small 1.5", "low 9223372036854775808", "high -1",
                        "high 1.8446744073709552e19", "low #inf", "d #true"}) {
         expect(bool(glz::read_kdl(v, text))) << text;
      }
      auto doc = glz::read_kdl<glz::kdl::document>("node 0xABCDEF0123456789abcdef 1.23E+1000 1e-1000");
      expect(bool(doc));
      if (doc) {
         expect(std::holds_alternative<glz::kdl::number>((*doc)[0].arguments[0].data));
         auto out = glz::write_kdl(*doc);
         expect(out && bool(glz::read_kdl<glz::kdl::document>(*out)));
      }
   };

   "malformed documents"_test = [] {
      for (auto text : {"node true",
                        "node null",
                        "node .1",
                        "node 0x_1",
                        "node 1._2",
                        "node 1e_2",
                        "node #wat",
                        "node \"\\q\"",
                        "node \"\\u{d800}\"",
                        "node \"\\u{110000}\"",
                        "node { child",
                        "node \"line\nbreak\"",
                        "node a=",
                        "node =1",
                        "node (x)prop=1",
                        "node {} {}",
                        "node {} arg",
                        "node /-",
                        "/* unclosed",
                        "node \"a\"\"b\"",
                        "}",
                        "node \"\"\"x\"\"\"",
                        "node r\"v1 raw\""}) {
         expect(!bool(glz::read_kdl<glz::kdl::document>(text))) << text;
      }
      for (auto text : {std::string{"node \"\xc0\xaf\""}, std::string{"node \"\xed\xa0\x80\""},
                        std::string{"node \"\0\"", 9}, std::string{"node \"\u202e\""}}) {
         expect(!bool(glz::read_kdl<glz::kdl::document>(text)));
      }
   };

   "options and empty documents"_test = [] {
      endpoint v{};
      expect(glz::read_kdl(v, "unknown 1") == glz::error_code::unknown_key);
      expect(!glz::read<glz::opts{.format = glz::KDL, .error_on_unknown_keys = false}>(v, "unknown 1"));
      expect(glz::read<glz::opts{.format = glz::KDL, .error_on_missing_keys = true}>(v, "host x") ==
             glz::error_code::missing_key);
      expect(!glz::read<glz::opts{.format = glz::KDL, .error_on_missing_keys = true}>(v, "host x; port 80"));
      for (auto text : {"", "  \n", "// empty", "/* empty */"}) {
         auto doc = glz::read_kdl<glz::kdl::document>(text);
         expect(doc && doc->empty());
      }
      std::vector<char> empty;
      glz::kdl::document doc{{.name = "previous"}};
      expect(!glz::read<glz::opts{.format = glz::KDL}>(doc, empty));
      expect(doc.empty());
      expect(glz::write_kdl(doc).value_or("error").empty());
   };

   "character ranges and null members"_test = [] {
      std::map<std::string, char> chars;
      expect(!glz::read_kdl(chars, "letter 65"));
      expect(chars["letter"] == 'A');
      expect(bool(glz::read_kdl(chars, "letter 1000")));
      std::map<std::string, std::monostate> nulls;
      expect(!glz::read_kdl(nulls, "nothing #null"));
      auto out = glz::write<glz::opts{.format = glz::KDL, .skip_null_members = false}>(nulls);
      expect(out && *out == "nothing #null\n");
      expect(glz::write_kdl(nulls).value_or("error").empty());
   };

   "empty annotations and writer validation"_test = [] {
      auto doc = glz::read_kdl<glz::kdl::document>(R"(("")node ("")1)");
      expect(bool(doc));
      if (!doc) return;
      expect((*doc)[0].tag.has_value() && (*doc)[0].tag->empty());
      auto out = glz::write_kdl(*doc);
      expect(bool(out));
      if (out) {
         auto result = glz::read_kdl<glz::kdl::document>(*out);
         expect(result && *result == *doc);
      }
      (*doc)[0].arguments[0].data = glz::kdl::number{"1; injected"};
      expect(!bool(glz::write_kdl(*doc)));
      (*doc)[0].arguments.clear();
      (*doc)[0].name = std::string{"bad\xff"};
      expect(!bool(glz::write_kdl(*doc)));
   };

   "root maps nullable objects and scalar arrays"_test = [] {
      std::map<std::string, endpoint> endpoints;
      expect(!glz::read_kdl(endpoints, "api host=local port=80"));
      expect(endpoints["api"] == endpoint{"local", 80});
      auto text = glz::write_kdl(endpoints);
      expect(text && bool(glz::read_kdl<decltype(endpoints)>(*text)));
      std::map<std::string, std::unique_ptr<endpoint>> pointers;
      expect(!glz::read_kdl(pointers, "one host=local port=80; two #null"));
      expect(pointers["one"] && pointers["one"]->port == 80 && !pointers["two"]);
      auto nulls = glz::write<glz::opts{.format = glz::KDL, .skip_null_members = false}>(pointers);
      expect(nulls && nulls->find("two #null") != std::string::npos);
      std::map<std::string, std::vector<std::optional<int>>> arrays;
      expect(!glz::read_kdl(arrays, "a 1 #null 3"));
      expect(arrays["a"] == std::vector<std::optional<int>>{1, {}, 3});
      auto encoded = glz::write_kdl(arrays);
      expect(encoded && bool(glz::read_kdl<decltype(arrays)>(*encoded)));
   };

   "buffers and file interfaces"_test = [] {
      endpoint value{"local", 443};
      test_buffers::qt_style_buffer out;
      expect(!glz::write_kdl(value, out));
      endpoint result{};
      const test_buffers::read_only_buffer input{{out.data(), out.size()}};
      expect(!glz::read_kdl(result, input));
      expect(result == value);
      std::array<char, 64> bounded{};
      const auto bounded_ec = glz::write_kdl(value, std::span{bounded});
      expect(!bounded_ec && bounded_ec.count == out.size());
      expect(std::string_view{bounded.data(), bounded_ec.count} == std::string_view{out.data(), out.size()});
      std::array<char, 8> small{};
      expect(bool(glz::write_kdl(value, std::span{small})));
      std::vector<char> bytes;
      expect(!glz::write_kdl(value, bytes));
      std::string buffer;
      expect(!glz::write_file_kdl(value, "config.kdl", buffer));
      expect(!glz::read_file_kdl(result, "config.kdl", buffer));
      expect(result == value);
      expect(glz::read_file_kdl(result, "missing.kdl", buffer) == glz::error_code::file_open_failure);
   };

   "direct recursion limits and reused contexts"_test = [] {
      std::string skipped_input;
      for (size_t i = 0; i < 300; ++i) skipped_input += "unknown {";
      for (size_t i = 0; i < 300; ++i) skipped_input += "}";
      endpoint endpoint_value{};
      glz::context ctx{};
      constexpr auto options = glz::opts{.format = glz::KDL, .error_on_unknown_keys = false};
      expect(glz::read<options>(endpoint_value, skipped_input, ctx) == glz::error_code::exceeded_max_recursive_depth);
      expect(ctx.depth == 0);
      expect(!glz::read<options>(endpoint_value, "port 80", ctx));
      expect(ctx.depth == 0 && endpoint_value.port == 80);

      recursive_config tree{};
      auto* level = &tree;
      for (size_t i = 0; i < 140; ++i) {
         level->children.emplace_back();
         level = &level->children.back();
      }
      auto too_deep = glz::write_kdl(tree);
      expect(!too_deep && too_deep.error() == glz::error_code::exceeded_max_recursive_depth);
      std::string input;
      for (size_t i = 0; i < 140; ++i) input += "children { - {";
      input += "value 7";
      for (size_t i = 0; i < 140; ++i) input += "} }";
      expect(glz::read_kdl(tree, input) == glz::error_code::exceeded_max_recursive_depth);
      expect(!glz::read_kdl(tree, "value 9; children { - { value 10 } }"));
      expect(tree.value == 9 && tree.children.size() == 1 && tree.children[0].value == 10);
   };

   "nesting limits and context reuse"_test = [] {
      std::string input;
      for (size_t i = 0; i < 300; ++i) input += "n {";
      input += "leaf";
      for (size_t i = 0; i < 300; ++i) input += "}";
      glz::kdl::document doc;
      glz::context ctx{};
      expect(glz::read<glz::opts{.format = glz::KDL}>(doc, input, ctx) ==
             glz::error_code::exceeded_max_recursive_depth);
      expect(ctx.depth == 0);
      expect(!glz::read<glz::opts{.format = glz::KDL}>(doc, "node", ctx));
      expect(ctx.depth == 0);
      doc = {{.name = "n"}};
      auto* level = &doc[0];
      for (size_t i = 0; i < 300; ++i) {
         level->children.push_back({.name = "n"});
         level = &level->children.back();
      }
      auto out = glz::write_kdl(doc);
      expect(!out && out.error() == glz::error_code::exceeded_max_recursive_depth);
   };
};

int main() {}
