// Glaze Library
// For the license information refer to glaze.hpp

#pragma once

#include <algorithm>
#include <charconv>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>

#include "glaze/core/context.hpp"
#include "glaze/kdl/common.hpp"
#include "glaze/kdl/scan.hpp"
#include "glaze/kdl/token.hpp"

namespace glz::kdl::detail
{
   inline bool forbidden(uint32_t c)
   {
      return c <= 8 || (c >= 14 && c <= 31) || c == 127 || (c >= 0xD800 && c <= 0xDFFF) ||
             (c >= 0x200E && c <= 0x200F) || (c >= 0x202A && c <= 0x202E) || (c >= 0x2066 && c <= 0x2069) ||
             c == 0xFEFF;
   }

   inline bool space(uint32_t c)
   {
      return c == 9 || c == 32 || c == 0xA0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) || c == 0x202F ||
             c == 0x205F || c == 0x3000;
   }

   inline bool newline(uint32_t c) { return (c >= 10 && c <= 13) || c == 0x85 || c == 0x2028 || c == 0x2029; }

   inline void utf8(std::string& out, uint32_t c)
   {
      if (c < 0x80)
         out += char(c);
      else if (c < 0x800) {
         out += char(0xC0 | (c >> 6));
         out += char(0x80 | (c & 63));
      }
      else if (c < 0x10000) {
         out += char(0xE0 | (c >> 12));
         out += char(0x80 | ((c >> 6) & 63));
         out += char(0x80 | (c & 63));
      }
      else {
         out += char(0xF0 | (c >> 18));
         out += char(0x80 | ((c >> 12) & 63));
         out += char(0x80 | ((c >> 6) & 63));
         out += char(0x80 | (c & 63));
      }
   }

   // Strict UTF-8 decoding, shared by document validation and string emission.
   inline bool decode(std::string_view s, size_t& p, uint32_t& c)
   {
      if (p == s.size()) return false;
      c = uint8_t(s[p++]);
      if (c < 0x80) return true;
      size_t n{};
      uint32_t min{};
      if (c >= 0xC2 && c <= 0xDF)
         n = 1, min = 0x80, c &= 31;
      else if (c >= 0xE0 && c <= 0xEF)
         n = 2, min = 0x800, c &= 15;
      else if (c >= 0xF0 && c <= 0xF4)
         n = 3, min = 0x10000, c &= 7;
      else
         return false;
      if (s.size() - p < n) return false;
      while (n--) {
         const auto b = uint8_t(s[p++]);
         if ((b & 0xC0) != 0x80) return false;
         c = (c << 6) | (b & 63);
      }
      return c >= min && c <= 0x10FFFF && !(c >= 0xD800 && c <= 0xDFFF);
   }

   inline bool digit(char c) { return c >= '0' && c <= '9'; }
   inline int hex(char c)
   {
      if (digit(c)) return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      return -1;
   }

   inline bool identifier(std::string_view s)
   {
      if (s.empty() || digit(s[0]) || s == "true" || s == "false" || s == "null" || s == "inf" || s == "-inf" ||
          s == "nan")
         return false;
      size_t i = (s[0] == '+' || s[0] == '-') ? 1 : 0;
      if (i < s.size() && s[i] == '.') ++i;
      if (i < s.size() && digit(s[i])) return false;
      size_t p{};
      uint32_t c{};
      while (p < s.size()) {
         p = scan::run<scan::kind::identifier>(s, p);
         if (p == s.size()) break;
         if (!decode(s, p, c) || forbidden(c) || space(c) || newline(c) ||
             (c < 128 && std::string_view{"\\/(){};[]\"#="}.find(char(c)) != std::string_view::npos))
            return false;
      }
      return true;
   }

   template <class Ctx>
   struct parser
   {
      std::string_view input;
      Ctx& ctx;
      size_t pos{};
      size_t node_begin{};

      bool fail(error_code ec = error_code::syntax_error)
      {
         if (!bool(ctx.error)) ctx.error = ec;
         return false;
      }
      bool at(std::string_view s) const { return input.substr(pos, s.size()) == s; }
      uint32_t peek() const
      {
         size_t p = pos;
         uint32_t c{};
         decode(input, p, c);
         return c;
      }
      void advance()
      {
         uint32_t c{};
         decode(input, pos, c);
         if (c == '\r' && at("\n")) ++pos;
      }
      bool validate()
      {
         size_t p{};
         uint32_t c{};
         while (p < input.size()) {
            p = scan::run<scan::kind::validation>(input, p);
            if (p == input.size()) break;
            const auto start = p;
            if (!decode(input, p, c) || (forbidden(c) && !(start == 0 && c == 0xFEFF))) {
               pos = start;
               return fail();
            }
         }
         if (at("\xEF\xBB\xBF")) pos += 3;
         return true;
      }
      bool block_comment()
      {
         pos += 2;
         size_t depth = 1;
         while (pos < input.size()) {
            pos = scan::run<scan::kind::block_comment>(input, pos);
            if (pos == input.size()) break;
            if (at("/*"))
               ++depth, pos += 2;
            else if (at("*/")) {
               pos += 2;
               if (--depth == 0) return true;
            }
            else
               ++pos;
         }
         return fail(error_code::unexpected_end);
      }
      void line_comment()
      {
         while (pos < input.size()) {
            pos = scan::run<scan::kind::line_comment>(input, pos);
            if (pos == input.size() || newline(peek())) break;
            advance();
         }
      }
      // Node whitespace excludes bare line endings and // comments.
      bool ws(bool lines = false)
      {
         const auto start = pos;
         while (pos < input.size() && !bool(ctx.error)) {
            pos = lines ? scan::run<scan::kind::lines>(input, pos) : scan::run<scan::kind::spaces>(input, pos);
            if (pos == input.size()) break;
            if (space(peek()) || (lines && newline(peek())))
               advance();
            else if (at("/*"))
               block_comment();
            else if (lines && at("//"))
               line_comment();
            else if (at("\\")) {
               ++pos;
               while (pos < input.size()) {
                  if (space(peek()))
                     advance();
                  else if (at("/*")) {
                     if (!block_comment()) return false;
                  }
                  else
                     break;
               }
               if (at("//")) line_comment();
               if (pos == input.size()) break; // escaped EOF is a valid continuation
               if (!newline(peek())) return fail();
               advance();
            }
            else
               break;
         }
         return pos != start;
      }
      bool escape(std::string& out)
      {
         ++pos;
         if (pos == input.size()) return fail(error_code::unexpected_end);
         if (space(peek()) || newline(peek())) {
            do {
               advance();
            } while (pos < input.size() && (space(peek()) || newline(peek())));
            return true;
         }
         const auto c = input[pos++];
         switch (c) {
         case 'n':
            out += '\n';
            return true;
         case 'r':
            out += '\r';
            return true;
         case 't':
            out += '\t';
            return true;
         case 'b':
            out += '\b';
            return true;
         case 'f':
            out += '\f';
            return true;
         case 's':
            out += ' ';
            return true;
         case '\\':
            out += '\\';
            return true;
         case '"':
            out += '"';
            return true;
         case 'u': {
            if (!at("{")) return fail();
            ++pos;
            uint32_t code{};
            size_t count{};
            while (pos < input.size() && hex(input[pos]) >= 0) {
               if (++count > 6) return fail();
               code = (code << 4) | uint32_t(hex(input[pos++]));
            }
            if (!count || !at("}") || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) return fail();
            ++pos;
            utf8(out, code);
            return true;
         }
         default:
            return fail();
         }
      }
      bool quoted(std::string& out)
      {
         size_t hashes{};
         while (at("#")) ++hashes, ++pos;
         if (!at("\"")) return fail();
         const bool multi = at("\"\"\"");
         const size_t quotes = multi ? 3 : 1;
         pos += quotes;
         if (multi) {
            if (pos == input.size() || !newline(peek())) return fail();
            advance();
         }
         const std::string closing = std::string(quotes, '"') + std::string(hashes, '#');
         std::string body;
         while (pos < input.size() && !at(closing)) {
            const auto begin = pos;
            pos = hashes ? scan::run<scan::kind::raw>(input, pos) : scan::run<scan::kind::quoted>(input, pos);
            body.append(input.substr(begin, pos - begin));
            if (pos == input.size() || at(closing)) break;
            if (newline(peek())) {
               if (!multi) return fail();
               body += '\n';
               advance();
            }
            else {
               // Keep escapes verbatim until after dedentation.
               if (!hashes && at("\\")) {
                  const auto begin = pos;
                  size_t next = pos + 1;
                  uint32_t next_code{};
                  decode(input, next, next_code);
                  const bool whitespace = space(next_code) || newline(next_code);
                  std::string ignored;
                  if (!escape(ignored)) return false;
                  if (!whitespace) body.append(input.substr(begin, pos - begin));
               }
               else {
                  const auto begin = pos;
                  advance();
                  body.append(input.substr(begin, pos - begin));
               }
            }
         }
         if (pos == input.size()) return fail(error_code::unexpected_end);
         pos += closing.size();
         if (multi) {
            const auto last = body.rfind('\n');
            const auto indent = last == std::string::npos ? body : body.substr(last + 1);
            size_t p{};
            uint32_t c{};
            while (p < indent.size()) {
               if (!decode(indent, p, c) || !space(c)) return fail();
            }
            if (last == std::string::npos) {
               body.clear();
            }
            else {
               body.resize(last);
               std::string dedented;
               size_t begin{};
               while (begin <= body.size()) {
                  auto end = body.find('\n', begin);
                  if (end == std::string::npos) end = body.size();
                  std::string_view line{body.data() + begin, end - begin};
                  bool blank = true;
                  p = 0;
                  while (p < line.size()) {
                     decode(line, p, c);
                     if (!space(c)) blank = false;
                  }
                  if (blank)
                     line = {};
                  else if (line.starts_with(indent))
                     line.remove_prefix(indent.size());
                  else
                     return fail();
                  dedented += line;
                  if (end == body.size()) break;
                  dedented += '\n';
                  begin = end + 1;
               }
               body = std::move(dedented);
            }
         }
         if (hashes)
            out = std::move(body);
         else {
            parser nested{std::string_view{body}, ctx};
            while (nested.pos < body.size()) {
               const auto begin = nested.pos;
               nested.pos = scan::run<scan::kind::quoted>(body, nested.pos);
               out.append(body, begin, nested.pos - begin);
               if (nested.pos == body.size()) break;
               if (nested.at("\\")) {
                  if (!nested.escape(out)) return false;
               }
               else
                  out += body[nested.pos++];
            }
         }
         return true;
      }
      std::string_view token()
      {
         const auto start = pos;
         while (pos < input.size()) {
            pos = scan::run<scan::kind::token>(input, pos);
            if (pos == input.size()) break;
            const auto c = peek();
            if (space(c) || newline(c) ||
                (c < 128 && std::string_view{"\\/(){};[]\"="}.find(char(c)) != std::string_view::npos))
               break;
            advance();
         }
         return input.substr(start, pos - start);
      }
      bool string(std::string& out)
      {
         if (at("\"") || at("#")) return quoted(out);
         const auto s = token();
         if (!identifier(s)) return fail();
         out = s;
         return true;
      }
      bool tag(std::optional<std::string>& out)
      {
         if (!at("(")) return true;
         out.emplace();
         return tag(*out);
      }
      bool tag(std::string& out)
      {
         if (!at("(")) return true;
         ++pos;
         ws();
         if (!string(out)) return false;
         ws();
         if (!at(")")) return fail();
         ++pos;
         ws();
         return !bool(ctx.error);
      }
      template <class V>
      bool number(std::string_view s, V& out)
      {
         size_t i{};
         bool negative{};
         if (s[i] == '+' || s[i] == '-') negative = s[i++] == '-';
         int base = 10;
         if (s.substr(i, 2) == "0x")
            base = 16, i += 2;
         else if (s.substr(i, 2) == "0o")
            base = 8, i += 2;
         else if (s.substr(i, 2) == "0b")
            base = 2, i += 2;
         std::string normalized;
         if (negative) normalized += '-';
         auto digits = [&]() {
            if (i == s.size() || hex(s[i]) < 0 || hex(s[i]) >= base) return false;
            do {
               if (s[i] != '_') normalized += s[i];
               ++i;
            } while (i < s.size() && (s[i] == '_' || (hex(s[i]) >= 0 && hex(s[i]) < base)));
            return true;
         };
         if (!digits()) return fail();
         bool floating{};
         if (base == 10 && i < s.size() && s[i] == '.') {
            floating = true;
            normalized += s[i++];
            if (!digits()) return fail();
         }
         if (base == 10 && i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
            floating = true;
            normalized += s[i++];
            if (i < s.size() && (s[i] == '+' || s[i] == '-')) normalized += s[i++];
            if (!digits()) return fail();
         }
         if (i != s.size()) return fail();
         if (floating) {
            double n{};
            const auto r = std::from_chars(normalized.data(), normalized.data() + normalized.size(), n);
            if (r.ec != std::errc{} || r.ptr != normalized.data() + normalized.size() || !std::isfinite(n)) {
               out.data = kdl::number{std::string{s}};
               return true;
            }
            out.data = n;
         }
         else {
            uint64_t n{};
            std::string_view magnitude{normalized};
            if (negative) magnitude.remove_prefix(1);
            const auto r = std::from_chars(magnitude.data(), magnitude.data() + magnitude.size(), n, base);
            if (r.ec != std::errc{} || r.ptr != magnitude.data() + magnitude.size()) {
               out.data = kdl::number{std::string{s}};
               return true;
            }
            if (negative) {
               constexpr auto limit = uint64_t((std::numeric_limits<int64_t>::max)()) + 1;
               if (n > limit) {
                  out.data = kdl::number{std::string{s}};
                  return true;
               }
               out.data = n == limit ? (std::numeric_limits<int64_t>::min)() : -int64_t(n);
            }
            else if (n <= uint64_t((std::numeric_limits<int64_t>::max)()))
               out.data = int64_t(n);
            else
               out.data = n;
         }
         return true;
      }
      // Dedentation can sometimes leave one contiguous source span: for example
      // a single indented content line, or several LF lines with zero indent.
      // Probe without allocating; otherwise quoted() performs the full decoding.
      bool borrow_multiline(std::string_view& out)
      {
         auto probe = *this;
         size_t hashes{};
         while (probe.at("#")) ++hashes, ++probe.pos;
         if (!probe.at("\"\"\"")) return false;
         probe.pos += 3;
         if (probe.pos == input.size() || !newline(probe.peek())) return false;
         probe.advance();
         const auto begin = probe.pos;
         size_t last_newline = begin;
         size_t closing_line = begin;
         bool has_newline{};
         while (probe.pos < input.size()) {
            if (probe.at("\"\"\"") && input.substr(probe.pos + 3, hashes) == input.substr(pos, hashes)) break;
            if (!hashes && probe.at("\\")) return false;
            if (newline(probe.peek())) {
               last_newline = probe.pos;
               has_newline = true;
               probe.advance();
               closing_line = probe.pos;
            }
            else
               probe.advance();
         }
         if (probe.pos == input.size()) return false;
         const auto indent = input.substr(closing_line, probe.pos - closing_line);
         size_t cursor{};
         uint32_t code{};
         while (cursor < indent.size())
            if (!decode(indent, cursor, code) || !space(code)) return false;
         const auto end = has_newline ? last_newline : begin;
         size_t span_begin = begin;
         size_t span_end = begin;
         bool started{};
         auto append = [&](size_t first, size_t last) {
            if (first == last) return true;
            if (started && first != span_end) return false;
            if (!started) span_begin = first;
            started = true;
            span_end = last;
            return true;
         };
         cursor = begin;
         while (cursor < end) {
            const auto line_begin = cursor;
            bool blank = true;
            while (cursor < end) {
               const auto at = cursor;
               decode(input, cursor, code);
               if (newline(code)) {
                  cursor = at;
                  break;
               }
               if (!space(code)) blank = false;
            }
            const auto line = input.substr(line_begin, cursor - line_begin);
            if (!blank) {
               if (!line.starts_with(indent) || !append(line_begin + indent.size(), cursor)) return false;
            }
            if (cursor == end) break;
            // Normalization of CRLF or Unicode line endings requires decoding.
            if (input[cursor] != '\n' || !append(cursor, cursor + 1)) return false;
            ++cursor;
         }
         out = input.substr(span_begin, started ? span_end - span_begin : 0);
         pos = probe.pos + 3 + hashes;
         return true;
      }
      // A single-line literal with no decoding can point into the input.
      bool borrow_quoted(std::string_view& out, bool& encoded)
      {
         size_t p = pos;
         while (p < input.size() && input[p] == '#') ++p;
         const auto hashes = p - pos;
         if (input.substr(p, 1) != "\"" || input.substr(p, 3) == "\"\"\"") return false;
         const auto begin = ++p;
         while (p < input.size()) {
            p = hashes ? scan::run<scan::kind::raw>(input, p) : scan::run<scan::kind::quoted>(input, p);
            if (p == input.size()) return false;
            if (input[p] == '"' && input.substr(p + 1, hashes) == input.substr(pos, hashes)) {
               out = input.substr(begin, p - begin);
               pos = p + 1 + hashes;
               return true;
            }
            if (!hashes && input[p] == '\\') {
               auto probe = *this;
               probe.pos = p;
               std::string ignored;
               if (!probe.escape(ignored)) {
                  pos = probe.pos;
                  return false;
               }
               encoded = true;
               p = probe.pos;
               continue;
            }
            uint32_t c{};
            if (!decode(input, p, c) || newline(c)) return false;
         }
         return false;
      }
      template <class V>
      bool entry(V& out)
      {
         if (!tag(out.tag)) return false;
         if (at("\"") || at("#\"") || at("##")) {
            if constexpr (std::same_as<V, scalar_token>) {
               std::string_view borrowed;
               bool encoded{};
               if (borrow_multiline(borrowed) || borrow_quoted(borrowed, encoded)) {
                  out.data = borrowed;
                  if (encoded)
                     out.decode_text = +[](std::string_view source, std::string& result) {
                        context context{};
                        parser<glz::context> nested{source, context};
                        while (nested.pos < source.size()) {
                           const auto begin = nested.pos;
                           nested.pos = scan::run<scan::kind::quoted>(source, nested.pos);
                           result.append(source.substr(begin, nested.pos - begin));
                           if (nested.pos == source.size()) break;
                           if (nested.at("\\"))
                              nested.escape(result);
                           else
                              result += source[nested.pos++];
                        }
                     };
                  return true;
               }
               if (bool(ctx.error)) return false;
            }
            std::string s;
            if (!quoted(s)) return false;
            out.data = std::move(s);
            return true;
         }
         const auto s = token();
         if (s.empty()) return fail();
         if (s == "#true")
            out.data = true;
         else if (s == "#false")
            out.data = false;
         else if (s == "#null")
            out.data = std::monostate{};
         else if (s == "#inf")
            out.data = std::numeric_limits<double>::infinity();
         else if (s == "#-inf")
            out.data = -std::numeric_limits<double>::infinity();
         else if (s == "#nan")
            out.data = std::numeric_limits<double>::quiet_NaN();
         else if (digit(s[0]) || ((s[0] == '+' || s[0] == '-') && s.size() > 1 && digit(s[1])))
            return number(s, out);
         else if (identifier(s)) {
            if constexpr (std::same_as<V, scalar_token>)
               out.data = s;
            else
               out.data = std::string{s};
         }
         else
            return fail();
         return true;
      }
      // Parse a node body once and deliver entries directly to its consumer. Both
      // typed reads and the explicit DOM reader share these grammar checks.
      template <class Argument, class Property, class Children>
      bool body(Argument&& argument, Property&& property, Children&& children)
      {
         bool children_seen{}, active_children{};
         while (!bool(ctx.error)) {
            const bool separated = ws();
            if (pos == input.size() || at("}") || newline(peek()) || at(";") || at("//")) break;
            const bool ignored = at("/-");
            if (ignored) {
               pos += 2;
               ws(true);
            }
            if (at("{")) {
               if (!ignored && active_children) return fail();
               children_seen = true;
               ++pos;
               if (ignored) {
                  if (!skip_nodes(true)) return false;
               }
               else {
                  active_children = true;
                  children(); // consumes the children and closing brace
               }
               continue;
            }
            if (children_seen || (!separated && !ignored)) return fail();
            scalar_token v;
            const auto entry_begin = pos;
            if (!entry(v)) return false;
            const auto after = pos;
            ws();
            if (at("=")) {
               auto key = text(v);
               if (!key || v.tag.has_value()) return fail();
               ++pos;
               ws();
               scalar_token prop;
               const auto prop_begin = pos;
               if (!entry(prop)) return false;
               if (!ignored) {
                  if constexpr (std::invocable<Property, std::string_view, scalar_token&, std::string_view>)
                     property(*key, prop, input.substr(prop_begin, pos - prop_begin));
                  else
                     property(*key, prop);
               }
            }
            else {
               pos = after;
               if (!ignored) {
                  if constexpr (std::invocable<Argument, scalar_token&, std::string_view>)
                     argument(v, input.substr(entry_begin, after - entry_begin));
                  else
                     argument(v);
               }
            }
         }
         return !bool(ctx.error);
      }

      template <class Node>
      bool nodes_each(Node&& consume, bool child = false)
      {
         depth_guard guard{ctx, 256};
         if (!guard) return false;
         while (!bool(ctx.error)) {
            ws(true);
            if (bool(ctx.error)) return false;
            if (pos == input.size()) return child ? fail(error_code::unexpected_end) : true;
            if (at("}")) {
               if (!child) return fail();
               ++pos;
               return true;
            }
            const bool discard = at("/-");
            if (discard) {
               pos += 2;
               ws(true);
            }
            std::optional<std::string> annotation;
            std::string name;
            const auto begin = pos;
            if (!tag(annotation) || !string(name)) return false;
            node_begin = begin;
            if (discard)
               skip_body();
            else
               consume(name, annotation);
            if (bool(ctx.error)) return false;
            if (at(";"))
               ++pos;
            else if (at("//"))
               line_comment();
            else if (pos < input.size() && newline(peek()))
               advance();
         }
         return false;
      }

      bool skip_body()
      {
         return body([](const auto&) {}, [](std::string_view, const auto&) {}, [&] { skip_nodes(true); });
      }

      bool skip_nodes(bool child = false)
      {
         return nodes_each([&](const auto&, const auto&) { skip_body(); }, child);
      }

      bool node_body(node& n)
      {
         return body([&](auto& v) { n.arguments.emplace_back(std::move(v)); },
                     [&](std::string_view key, auto& v) {
                        const auto found = std::find_if(n.properties.begin(), n.properties.end(),
                                                        [&](auto& p) { return p.first == key; });
                        if (found == n.properties.end())
                           n.properties.emplace_back(key, std::move(v));
                        else
                           found->second = std::move(v);
                     },
                     [&] { nodes(n.children, true); });
      }

      bool nodes(document& out, bool child = false)
      {
         return nodes_each(
            [&](const std::string& name, const std::optional<std::string>& annotation) {
               node n;
               n.name = name;
               n.tag = annotation;
               if (node_body(n)) out.emplace_back(std::move(n));
            },
            child);
      }
   };
}
