// Glaze Library
// For the license information refer to glaze.hpp

#include "glaze/kdl.hpp"
#include "ut/ut.hpp"

using namespace ut;
using namespace std::chrono_literals;

struct standard_config
{
   std::chrono::milliseconds timeout{};
   std::chrono::duration<double> interval{};
   std::filesystem::path path{};
   std::optional<std::filesystem::path> optional{};
   std::vector<std::chrono::seconds> delays{};
   std::chrono::sys_time<std::chrono::milliseconds> timestamp{};
   std::chrono::sys_days day{};
   std::chrono::year_month_day date{std::chrono::year{1970} / 1 / 1};
   std::chrono::steady_clock::time_point elapsed{};
   glz::epoch_seconds epoch{};
   bool operator==(const standard_config&) const = default;
};
struct duration_boundaries
{
   std::chrono::minutes minutes{};
   std::chrono::duration<int64_t, std::ratio<9223372036854775783LL>> coarse{};
   std::chrono::duration<uint64_t, std::nano> unsigned_count{};
};

suite standard_tests = [] {
   "duration units and native counts"_test = [] {
      standard_config c{};
      expect(!glz::read_kdl(c, R"(timeout "5s"; interval "100ms"; delays "5s" 2 "1min")"));
      expect(c.timeout == 5000ms && c.interval.count() == 0.1 && c.delays == std::vector{5s, 2s, 60s});
      for (auto input : {R"(timeout "100ms")", R"(timeout "0.1s")", R"(timeout "+0.1s")", R"(timeout "100000us")"}) {
         expect(!glz::read_kdl(c, input));
         expect(c.timeout == 100ms);
      }
      expect(!glz::read_kdl(c, R"(timeout "1.5s"; interval "-5ms")"));
      expect(c.timeout == 1500ms && c.interval.count() == -0.005);
      expect(!glz::read_kdl(c, R"(timeout "9223372036854775807ms")"));
      expect(c.timeout.count() == std::numeric_limits<int64_t>::max());
      for (auto input : {R"(timeout "9223372036854775808ms")", R"(timeout "0.1ms")", R"(timeout "1wat")",
                         R"(timeout "1s ")", R"(timeout "nanms")", R"(timeout "1e999s")", R"(timeout "+-1s")"})
         expect(glz::read_kdl(c, input) == glz::error_code::parse_number_failure);
      expect(!glz::read_kdl(c, "timeout 123; interval 2.5"));
      expect(c.timeout == 123ms && c.interval == 2.5s);
      auto text = glz::write_kdl(c);
      expect(text && text->find("timeout 123\n") != std::string::npos);
      for (int i = 0; i < 1000; ++i) {
         const auto input = "timeout \"0." + std::to_string(1000 + i).substr(1) + "s\"";
         expect(!glz::read_kdl(c, input) && c.timeout.count() == i);
      }
      duration_boundaries edge{};
      expect(!glz::read_kdl(edge, R"(
         minutes "540000000000000000060s"
         coarse "83010348331692982047s"
         unsigned_count "18446744073709551615ns"
      )"));
      expect(edge.minutes.count() == 9000000000000000001LL && edge.coarse.count() == 9);
      expect(edge.unsigned_count.count() == UINT64_MAX);
   };
   "paths and chrono round trips"_test = [] {
      standard_config c{};
      expect(!glz::read_kdl(c, R"kdl(
         timeout "5s"
         interval 0.5
         path "/tmp/中文 path\\file"
         optional "relative path/file"
         delays 1 2 3
         timestamp "2026-10-27T12:34:56.123+08:00"
         day "2026-10-27"
         date "2024-02-29"
         elapsed 12345
         epoch 100
      )kdl"));
      expect(c.path == std::filesystem::path{"/tmp/中文 path\\file"});
      expect(c.optional == std::filesystem::path{"relative path/file"});
      expect(c.date == std::chrono::year{2024} / 2 / 29);
      expect(c.day == std::chrono::sys_days{std::chrono::year{2026} / 10 / 27});
      expect(c.epoch.value.time_since_epoch() == 100s);
      expect(glz::read_kdl(c, "epoch 9223372036854775807") == glz::error_code::parse_number_failure);
      expect(c.elapsed.time_since_epoch().count() == 12345);
      auto text = glz::write_kdl(c);
      expect(text && text->find("2026-10-27T04:34:56.123Z") != std::string::npos);
      if (text) {
         auto result = glz::read_kdl<standard_config>(*text);
         expect(result && *result == c);
      }
      expect(!glz::read_kdl(c, "optional #null"));
      expect(!c.optional);
      for (auto input : {"path 42", "timeout #true", "date bad", "date 2024", "timestamp bad", "day bad"})
         expect(bool(glz::read_kdl(c, input)));
      expect(bool(glz::read_kdl(c, R"(date "2023-02-29")")));
      c.date = std::chrono::year{2023} / 2 / 29;
      expect(!glz::write_kdl(c));
   };
};
int main() {}
