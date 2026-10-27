// Glaze Library
// For the license information refer to glaze.hpp
#include "glaze/kdl.hpp"
#include "ut/ut.hpp"
using namespace ut;
suite tools_tests = [] {
   "native paths and source spans"_test = [] {
      const std::string source = "(tag)server 1 port=80 { host \"first\"; }\nserver 2 port=81 port=82 {host second;}";
      auto doc = glz::read_kdl<glz::kdl::document>(source);
      expect(bool(doc));
      if (!doc) return;
      auto port = glz::kdl::get(*doc, "/server[1]/@port");
      expect(port && std::get<glz::kdl::value*>(*port)->data == glz::kdl::value::data_type{int64_t{82}});
      auto host = glz::kdl::read_at<std::string>(source, "/server[1]/host");
      expect(host && *host == "second");
      auto arg = glz::kdl::read_at<int>(source, "/server/#0");
      expect(arg && *arg == 1);
      auto last = glz::kdl::read_at<int>(source, "/server[1]/@port");
      expect(last && *last == 82);
      expect(!glz::kdl::set(*doc, "/server/:name", std::string{"renamed"}));
      expect((*doc)[0].name == "renamed");
      expect(!glz::kdl::set(*doc, "/renamed/:tag", std::optional<std::string>{"new-tag"}));
      expect(!glz::kdl::parse_path("/bad[-1]"));
      expect(!glz::kdl::get_view(source + "\nbad #invalid", "/server"));
      expect(bool(glz::kdl::read_at<glz::kdl::node>("single 1", "")));
      expect(!glz::kdl::read_at<glz::kdl::node>("first 1; second 2", ""));
      auto escaped = glz::kdl::parse_path("/~4~1~2~3~0~5~6");
      expect(escaped && (*escaped)[0].key == "@/[]~#:");
      const auto& const_doc = *doc;
      expect(bool(glz::kdl::get(const_doc, "/server")));
   };
   "native transactional diff and patch wire format"_test = [] {
      auto before = glz::read_kdl<glz::kdl::document>("(a)n 1 p=2 {same 1; same 2;}\nremove 3");
      auto after =
         glz::read_kdl<glz::kdl::document>("(b)renamed (tag)4 p=5 q=6 {same 7; same 8; added 9;}\nnew 4\nlast 5");
      expect(before && after);
      if (!before || !after) return;
      auto changes = glz::kdl::diff(*before, *after);
      auto wire = glz::kdl::write_patch(changes);
      auto parsed = wire ? glz::kdl::read_patch(*wire)
                         : glz::expected<glz::kdl::patch, glz::error_ctx>{glz::unexpected(wire.error())};
      expect(bool(parsed));
      if (!parsed) return;
      expect(!glz::kdl::apply_patch(*before, *parsed));
      expect(*before == *after);
      const auto snapshot = *before;
      parsed->push_back({glz::kdl::patch_kind::erase, {999}, {}});
      expect(bool(glz::kdl::apply_patch(*before, *parsed)));
      expect(*before == snapshot);
      auto removals = glz::kdl::diff(*before, {});
      expect(!glz::kdl::apply_patch(*before, removals) && before->empty());
      expect(!glz::kdl::read_patch("set-tag \"/0\" 42"));
   };
   "minify prettify preserve native semantics"_test = [] {
      const std::string input = "// comment\n(tag)n \"a;\\\\n\" prop=##\"raw\"## { child #true; child #null; }";
      auto original = glz::read_kdl<glz::kdl::document>(input);
      auto compact = glz::kdl::minify(input);
      expect(original && compact);
      if (!original || !compact) return;
      auto reparsed = glz::read_kdl<glz::kdl::document>(*compact);
      expect(reparsed && *reparsed == *original);
      auto pretty = glz::kdl::prettify(*compact);
      expect(bool(pretty));
      expect(!glz::kdl::minify("bad #invalid"));
   };
};
int main() {}
