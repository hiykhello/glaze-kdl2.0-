// Glaze Library
// For the license information refer to glaze.hpp

// Standalone, dependency-free benchmark. Build identical sources with/without
// GLZ_DISABLE_SIMD; this measures scanning, not parsing a different grammar.
#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>

#include "glaze/kdl.hpp"

struct benchmark_config
{
   int port{};
   bool enabled{};
   std::string text{};
};

struct benchmark_large_config
{
   int field_0{};
   int field_1{};
   int field_2{};
   int field_3{};
   int field_4{};
   int field_5{};
   int field_6{};
   int field_7{};
   int field_8{};
   int field_9{};
   int field_10{};
   int field_11{};
   int field_12{};
   int field_13{};
   int field_14{};
   int field_15{};
   int field_16{};
   int field_17{};
   int field_18{};
   int field_19{};
   int field_20{};
   int field_21{};
   int field_22{};
   int field_23{};
   int field_24{};
   int field_25{};
   int field_26{};
   int field_27{};
   int field_28{};
   int field_29{};
   int field_30{};
   int field_31{};
   int field_32{};
   int field_33{};
   int field_34{};
   int field_35{};
   int field_36{};
   int field_37{};
   int field_38{};
   int field_39{};
   int field_40{};
   int field_41{};
   int field_42{};
   int field_43{};
   int field_44{};
   int field_45{};
   int field_46{};
   int field_47{};
   int field_48{};
   int field_49{};
   int field_50{};
   int field_51{};
   int field_52{};
   int field_53{};
   int field_54{};
   int field_55{};
   int field_56{};
   int field_57{};
   int field_58{};
   int field_59{};
   int field_60{};
   int field_61{};
   int field_62{};
   int field_63{};
};

template <class Work>
void measure(std::string_view label, size_t bytes, Work&& work)
{
   const size_t iterations = std::clamp(size_t(16 * 1024 * 1024) / bytes, size_t(8), size_t(200000));
   std::array<double, 5> samples{};
   size_t checksum{};
   for (auto& sample : samples) {
      const auto begin = std::chrono::steady_clock::now();
      for (size_t i = 0; i < iterations; ++i) {
         checksum += work();
         std::atomic_signal_fence(std::memory_order_seq_cst);
      }
      sample = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
   }
   std::sort(samples.begin(), samples.end());
   const auto seconds = samples[2];
   std::cout << label << ": " << std::fixed << std::setprecision(1) << (double(bytes) * iterations / seconds / 1e6)
             << " MB/s, " << (seconds * 1e9 / iterations) << " ns/op; checksum=" << checksum << '\n';
}

int main()
{
   std::cout << "KDL scan backend: " << glz::kdl::simd_backend << '\n';
   const auto read = [](std::string_view label, const std::string& input) {
      benchmark_config target{};
      measure(label, input.size(), [&] {
         const auto ec = glz::read_kdl(target, input);
         if (ec) {
            std::cerr << glz::format_error(ec, input);
            std::exit(1);
         }
         return target.text.size() + size_t(target.port);
      });
   };
   read("read/small-config", "port 8080\nenabled #true\ntext \"localhost\"\n");
   read("read/long-string", "text \"" + std::string(65536, 'a') + "\"\n");
   read("read/block-comment", "/*" + std::string(65536, 'c') + "*/\nport 8080\n");
   read("read/whitespace", std::string(65536, ' ') + "port 8080\n");
   benchmark_large_config large;
   std::string large_input;
   for (int i = 0; i < 64; ++i) large_input += "field_" + std::to_string(i) + " " + std::to_string(i) + "\n";
   measure("read/64-fields-hash", large_input.size(), [&] {
      if (glz::read_kdl(large, large_input)) std::exit(3);
      return size_t(large.field_63);
   });
   benchmark_config source{8080, true, std::string(65536, 's')};
   std::string output;
   measure("write/long-string", source.text.size(), [&] {
      const auto ec = glz::write_kdl(source, output);
      if (ec) std::exit(2);
      return output.size();
   });
}
