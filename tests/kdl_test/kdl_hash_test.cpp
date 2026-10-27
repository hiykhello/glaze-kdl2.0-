// Glaze Library
// For the license information refer to glaze.hpp
#include "glaze/json/read.hpp"
#include "glaze/kdl.hpp"
#include "ut/ut.hpp"
using namespace ut;
struct large_config
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
   int field_64{};
   int field_65{};
   int field_66{};
   int field_67{};
   int field_68{};
   int field_69{};
   int field_70{};
   int field_71{};
   int field_72{};
   int field_73{};
   int field_74{};
   int field_75{};
   int field_76{};
   int field_77{};
   int field_78{};
   int field_79{};
   int field_80{};
   int field_81{};
   int field_82{};
   int field_83{};
   int field_84{};
   int field_85{};
   int field_86{};
   int field_87{};
   int field_88{};
   int field_89{};
   int field_90{};
   int field_91{};
   int field_92{};
   int field_93{};
   int field_94{};
   int field_95{};
   int field_96{};
   int field_97{};
   int field_98{};
   int field_99{};
   int field_100{};
   int field_101{};
   int field_102{};
   int field_103{};
   int field_104{};
   int field_105{};
   int field_106{};
   int field_107{};
   int field_108{};
   int field_109{};
   int field_110{};
   int field_111{};
   int field_112{};
   int field_113{};
   int field_114{};
   int field_115{};
   int field_116{};
   int field_117{};
   int field_118{};
   int field_119{};
   int field_120{};
   int field_121{};
   int field_122{};
   int field_123{};
   int field_124{};
   int field_125{};
   int field_126{};
   int field_127{};
};
struct massive_config
{
   std::array<int, 512> values{};
};
template <size_t I>
inline constexpr auto massive_key = [] {
   std::array<char, 11> key{"field_0000"};
   auto value = I;
   for (size_t digit = 10; digit-- > 6;) {
      key[digit] = char('0' + value % 10);
      value /= 10;
   }
   return key;
}();
template <>
struct glz::meta<massive_config>
{
   static constexpr auto value = []<size_t... I>(std::index_sequence<I...>) {
      return object([]<size_t J>() {
         if constexpr (J % 2 == 0)
            return massive_key<J / 2>.data();
         else
            return +[](massive_config& config) -> int& { return config.values[J / 2]; };
      }.template operator()<I>()...);
   }(std::make_index_sequence<1024>{});
};
// Synthetic key arrays exercise index widths at the 256 boundary, independent
// of reflection/member instantiation and UTF-8 restrictions on document text.
inline constexpr auto byte_key_storage = [] {
   std::array<std::array<char, 2>, 512> keys{};
   for (size_t i = 0; i < keys.size(); ++i) keys[i] = {char(i % 256), char(i / 256)};
   return keys;
}();
inline constexpr auto byte_keys = [] {
   std::array<std::string_view, 256> keys{};
   for (size_t i = 0; i < keys.size(); ++i) keys[i] = {byte_key_storage[i].data(), 1};
   return keys;
}();
inline constexpr auto front_key_storage = [] {
   std::array<std::array<char, 2>, 512> keys{};
   for (size_t i = 0; i < keys.size(); ++i) keys[i] = {char('A' + i % 32), char('a' + i / 32)};
   return keys;
}();
inline constexpr auto front_keys = [] {
   std::array<std::string_view, 512> keys{};
   for (size_t i = 0; i < keys.size(); ++i) keys[i] = {front_key_storage[i].data(), 2};
   return keys;
}();
template <const auto& Keys>
void check_hash_indices()
{
   using T = glz::keys_wrapper<Keys>;
   constexpr auto& hash = glz::hash_info<T>;
   static_assert(hash.type != glz::hash_type::invalid);
   for (size_t i = 0; i < Keys.size(); ++i) {
      const auto key = Keys[i];
      expect(glz::decode_hash_with_size<glz::KDL, T, hash, hash.type>::op(key.data(), key.data() + key.size(),
                                                                          key.size()) == i);
   }
   if constexpr (Keys.size() < hash.table.size())
      expect(std::find(hash.table.begin(), hash.table.end(), Keys.size()) != hash.table.end());
   constexpr std::string_view empty;
   expect(glz::decode_hash_with_size<glz::KDL, T, hash, hash.type>::op(empty.data(), empty.data(), 0) == Keys.size());
}
suite hash_tests = [] {
   "hash indices and invalid sentinel widen beyond 255"_test = [] {
      static_assert(glz::hash_info<glz::keys_wrapper<byte_keys>>.type == glz::hash_type::unique_index);
      static_assert(glz::hash_info<glz::keys_wrapper<front_keys>>.type == glz::hash_type::front_hash);
      constexpr std::array<std::string_view, 4> sized{"a", "aa", "b", "bb"};
      static_assert(glz::find_unique_sized_index(sized) == 0);
      constexpr std::array<std::string_view, 2> quoted{"a", "a\""};
      static_assert(!glz::find_unique_sized_index(quoted));
      check_hash_indices<byte_keys>();
      check_hash_indices<front_keys>();
   };

   "512 metadata fields share JSON hash study and reject near matches"_test = [] {
      massive_config value{};
      std::string input;
      for (int i = 511; i >= 0; --i) {
         auto key = std::string{"field_"} + std::string(4 - std::to_string(i).size(), '0') + std::to_string(i);
         input += key + " " + std::to_string(i) + "\n";
      }
      expect(!glz::read_kdl(value, input));
      for (size_t i = 0; i < value.values.size(); ++i) expect(value.values[i] == int(i));
      for (const auto key : {"field_0512", "field_9999", "field_0000suffix", "field_", "f"})
         expect(glz::read_kdl(value, std::string{key} + " 1000") == glz::error_code::unknown_key);
      expect(value.values[0] == 0 && value.values[511] == 511);
      std::string json{"{"};
      for (size_t i = 0; i < value.values.size(); ++i) {
         if (i) json += ',';
         json += '\"';
         json += std::string{"field_"} + std::string(4 - std::to_string(i).size(), '0') + std::to_string(i);
         json += "\":" + std::to_string(i);
      }
      json += '}';
      massive_config json_value{};
      expect(!glz::read_json(json_value, json));
      expect(json_value.values == value.values);
   };

   "shared perfect hash exact keys and collisions"_test = [] {
      std::string input;
      for (int i = 127; i >= 0; --i) input += "field_" + std::to_string(i) + " " + std::to_string(i) + "\n";
      auto value = glz::read_kdl<large_config>(input);
      expect(bool(value));
      if (!value) return;
      glz::for_each<128>([&]<size_t I>() { expect(glz::kdl::detail::member<I>(*value) == int(I)); });
      // Many unknown keys exercise out-of-range hash results and collisions.
      for (int i = 0; i < 1000; ++i) {
         const auto unknown = "field_" + std::to_string(i) + "_unknown 42";
         expect(glz::read_kdl(*value, unknown) == glz::error_code::unknown_key);
         expect(!glz::read<glz::opts{.format = glz::KDL, .error_on_unknown_keys = false}>(*value, unknown));
      }
      expect(value->field_0 == 0 && value->field_127 == 127);
      auto roundtrip = glz::write_kdl(*value);
      expect(bool(roundtrip));
   };
};
int main() {}
