// Glaze Library
// For the license information refer to glaze.hpp

#include "glaze/kdl.hpp"
#include "ut/ut.hpp"

using namespace ut;
struct borrowed_config
{
   std::string_view plain{};
   std::string_view raw{};
   std::string_view bare{};
   std::vector<std::string_view> sequence{};
   std::optional<std::string_view> nullable{};
};
struct raw_config
{
   std::string_view text{};
};
template <>
struct glz::meta<raw_config>
{
   static constexpr auto value = object("text", glz::raw_string<&raw_config::text>);
};

struct multiline_config
{
   std::string_view text;
};
suite borrow_tests = [] {
   "multiline views borrow exactly contiguous decoded content"_test = [] {
      for (const std::string input :
           {"text \"\"\"\n    one indented line\n    \"\"\"", "text \"\"\"\nfirst\nsecond\n\"\"\"",
            "text #\"\"\"\n    literal \\n and \"quotes\"\n    \"\"\"#", "text \"\"\"\n    \"\"\""}) {
         auto view = glz::read_kdl<multiline_config>(input);
         auto owning = glz::read_kdl<glz::kdl::document>(input);
         expect(view && owning);
         if (!view || !owning) continue;
         expect(view->text == std::get<std::string>((*owning)[0].arguments[0].data));
         expect(view->text.data() >= input.data() &&
                view->text.data() + view->text.size() <= input.data() + input.size());
      }
      for (const std::string input :
           {"text \"\"\"\n    first\n    second\n    \"\"\"", "text \"\"\"\nfirst\r\nsecond\n\"\"\"",
            "text \"\"\"\nfirst\n   \nsecond\n\"\"\"", "text \"\"\"\n    escaped\\nline\n    \"\"\""}) {
         expect(!glz::read_kdl<multiline_config>(input));
         expect(bool(glz::read_kdl<glz::kdl::document>(input)));
      }
   };

   "views borrow original bytes"_test = [] {
      const std::string input = R"kdl(
         plain "a long unescaped string with 中文 characters"
         raw ##"raw \\ and "quotes" stay literal"##
         bare a-long-unquoted-identifier
         sequence "first long unescaped string" second
         nullable "optional long unescaped string"
      )kdl";
      auto result = glz::read_kdl<borrowed_config>(input);
      expect(bool(result));
      if (!result) return;
      for (auto view : {result->plain, result->raw, result->bare, result->nullable.value_or("")}) {
         expect(view.data() >= input.data() && view.data() + view.size() <= input.data() + input.size());
      }
      expect(result->raw == R"(raw \\ and "quotes" stay literal)");
      expect(result->sequence.size() == 2 && result->sequence[1] == "second");
      auto text = glz::write_kdl(*result);
      expect(bool(text));
      // Writing a view escapes its contents; owning targets decode the result.
      if (text) expect(bool(glz::read_kdl<glz::kdl::document>(*text)));
      expect(!glz::read_kdl(*result, "nullable #null"));
      expect(!result->nullable);
      expect(glz::read_kdl(*result, R"(plain "requires\ndecoding")") == glz::error_code::invalid_escape);
      expect(glz::read_kdl(*result, "plain 42") == glz::error_code::syntax_error);
   };
   "raw string wrappers borrow escaped source"_test = [] {
      const std::string input = R"(text "line\n\u{4e2d}\"value\"")";
      auto result = glz::read_kdl<raw_config>(input);
      expect(bool(result));
      if (!result) return;
      expect(result->text == R"(line\n\u{4e2d}\"value\")");
      expect(result->text.data() == input.data() + input.find('"') + 1);
      auto text = glz::write_kdl(*result);
      expect(text && *text == input + "\n");
      expect(!glz::read_kdl<raw_config>(R"(text "bad\q")"));
      result->text = "unescaped\nnewline";
      expect(!glz::write_kdl(*result));
   };
};
int main() {}
