// Glaze Library
// For the license information refer to glaze.hpp

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <istream>
#include <memory>
#include <string>
#include <string_view>

#include "glaze/core/context.hpp"
#include "glaze/core/opts.hpp"
#include "glaze/forward.hpp"
#include "glaze/kdl/common.hpp"
#include "glaze/kdl/parse.hpp"
#include "glaze/kdl/read.hpp" // IWYU pragma: keep
#include "glaze/kdl/read_direct.hpp"

namespace glz::kdl
{
   // Keeping a record keeps borrowed fields and lazy members alive after refill.
   template <class T>
   struct stream_record
   {
      std::shared_ptr<const std::string> input;
      T value{};
   };

   // Incremental top-level node framing. The grammar/type reader validates each
   // completed record; no document DOM is needed for typed records.
   template <class T = node, auto Opts = opts{.format = KDL}>
   class stream_reader
   {
     public:
      template <class Consumer>
      [[nodiscard]] error_ctx feed(std::string_view chunk, Consumer&& consume)
      {
         if (finished_) return {offset_ + pos_, error_code::end_reached};
         if (error_) return error_;
         pending_.append(chunk);
         return process(consume, false);
      }
      template <class Consumer>
      [[nodiscard]] error_ctx finish(Consumer&& consume)
      {
         if (error_) return error_;
         if (finished_) return {};
         process(consume, true);
         if (!error_ && !pending_.empty()) emit(pending_.size(), consume);
         finished_ = true;
         return error_;
      }
      [[nodiscard]] size_t buffered_bytes() const { return pending_.size(); }

     private:
      enum class mode { normal, quoted, line_comment, block_comment };
      std::string pending_;
      size_t pos_{}, offset_{}, braces_{}, comments_{}, hashes_{};
      mode mode_{};
      bool multiline_{}, raw_{}, escaped_{}, continuation_{}, discard_{}, cr_{}, finished_{};
      error_ctx error_{};

      template <class Consumer>
      void emit(size_t end, Consumer& consume)
      {
         auto backing = std::make_shared<const std::string>(pending_.substr(0, end));
         context ctx{};
         detail::parser parser{std::string_view{*backing}, ctx};
         detail::direct_reader<Opts, context> reader{parser};
         stream_record<T> result{backing, {}};
         size_t count{};
         if (parser.validate())
            parser.nodes_each([&](const auto& name, const auto& tag) {
               if (++count > 1)
                  parser.fail();
               else
                  reader.node(result.value, name, tag);
            });
         if (ctx.error != error_code::none) {
            error_ = {offset_ + parser.pos, ctx.error, ctx.custom_error_message};
            return;
         }
         if (count) consume(std::move(result));
         pending_.erase(0, end);
         offset_ += end;
         pos_ = 0;
      }

      template <class Consumer>
      error_ctx process(Consumer& consume, bool final)
      {
         while (pos_ < pending_.size() && !error_) {
            const std::string_view s{pending_};
            const auto remaining = s.size() - pos_;
            const char c = s[pos_];
            if (mode_ == mode::block_comment) {
               if ((c == '/' || c == '*') && remaining < 2 && !final) break;
               if (s.substr(pos_, 2) == "/*") {
                  ++comments_;
                  pos_ += 2;
               }
               else if (s.substr(pos_, 2) == "*/") {
                  pos_ += 2;
                  if (!--comments_) mode_ = mode::normal;
               }
               else
                  ++pos_;
               continue;
            }
            if (mode_ == mode::quoted) {
               if (!raw_ && escaped_) {
                  escaped_ = false;
                  ++pos_;
                  continue;
               }
               if (!raw_ && c == '\\') {
                  escaped_ = true;
                  ++pos_;
                  continue;
               }
               if (c == '"') {
                  const size_t quotes = multiline_ ? 3 : 1;
                  const size_t length = quotes + hashes_;
                  if (remaining < length && !final) break;
                  bool close = remaining >= length;
                  for (size_t j = 0; j < quotes && close; ++j) close = s[pos_ + j] == '"';
                  for (size_t j = 0; j < hashes_ && close; ++j) close = s[pos_ + quotes + j] == '#';
                  if (close) {
                     pos_ += length;
                     mode_ = mode::normal;
                     continue;
                  }
               }
               ++pos_;
               continue;
            }
            size_t next = pos_;
            uint32_t cp{};
            if (!detail::decode(s, next, cp)) {
               if (!final && remaining < 4) break;
               error_ = {offset_ + pos_, error_code::syntax_error};
               break;
            }
            if (detail::newline(cp)) {
               const bool paired_lf = cr_ && cp == '\n';
               cr_ = cp == '\r';
               const bool end = mode_ == mode::line_comment || mode_ == mode::normal;
               mode_ = mode::normal;
               pos_ = next;
               if (end && !braces_ && !continuation_ && !discard_ && !paired_lf) emit(pos_, consume);
               continuation_ = false;
               continue;
            }
            cr_ = false;
            if (mode_ == mode::line_comment) {
               pos_ = next;
               continue;
            }
            if (c == '/') {
               if (remaining < 2 && !final) break;
               if (s.substr(pos_, 2) == "/*") {
                  mode_ = mode::block_comment;
                  comments_ = 1;
                  pos_ += 2;
                  continue;
               }
               if (s.substr(pos_, 2) == "//") {
                  mode_ = mode::line_comment;
                  pos_ += 2;
                  continue;
               }
               if (s.substr(pos_, 2) == "/-") {
                  discard_ = true;
                  pos_ += 2;
                  continue;
               }
            }
            if (!detail::space(cp) && c != '\\') discard_ = false;
            if (c == '"' || c == '#') {
               size_t quote = pos_;
               while (quote < s.size() && s[quote] == '#') ++quote;
               if (quote == s.size() && !final) break;
               if (quote < s.size() && s[quote] == '"') {
                  if (s.size() - quote < 3 && !final) break;
                  hashes_ = quote - pos_;
                  raw_ = hashes_ != 0;
                  multiline_ = s.substr(quote, 3) == "\"\"\"";
                  pos_ = quote + (multiline_ ? 3 : 1);
                  mode_ = mode::quoted;
                  continue;
               }
            }
            if (c == '{') ++braces_;
            if (c == '}') {
               if (!braces_) {
                  error_ = {offset_ + pos_, error_code::syntax_error};
                  break;
               }
               --braces_;
            }
            if (c == '\\') continuation_ = true;
            pos_ = next;
            if (c == ';' && !braces_) emit(pos_, consume);
         }
         return error_;
      }
   };

   template <class T = node, auto Opts = opts{.format = KDL}, class Consumer>
   [[nodiscard]] error_ctx read_stream(std::istream& input, Consumer&& consume)
   {
      stream_reader<T, Opts> reader;
      std::array<char, 8192> chunk;
      while (input) {
         input.read(chunk.data(), chunk.size());
         if (auto ec = reader.feed({chunk.data(), size_t(input.gcount())}, consume)) return ec;
      }
      if (input.bad()) return {0, error_code::file_open_failure};
      return reader.finish(consume);
   }
}
