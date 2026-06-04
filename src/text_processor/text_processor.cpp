#include "text_processor.hpp"

#include <ankerl/unordered_dense.h>
#include <utf8.h>
#include <utf8proc.h>

#include <array>
#include <cstdint>
#include <fstream>
#include <functional>
#include <glaze/glaze.hpp>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace internal {
struct KanjiMapping {
  std::string oyaji;
  std::vector<std::string> itaiji;
};
}

namespace {
struct TextProcessor {
  std::vector<int> options;
  std::function<std::u32string(const std::u32string&, int)> process;
};

// https://github.com/yomidevs/yomitan/blob/81d17d877fb18c62ba826210bf6db2b7f4d4deed/ext/js/language/ja/japanese.js#L21
constexpr uint32_t KATAKANA_SMALL_KA = 0x30f5;
constexpr uint32_t KATAKANA_SMALL_KE = 0x30f6;
constexpr uint32_t KANA_PROLONGED_SOUND_MARK = 0x30fc;

constexpr uint32_t HIRAGANA_CONVERSION_RANGE_START = 0x3041;
constexpr uint32_t HIRAGANA_CONVERSION_RANGE_END = 0x3096;

constexpr uint32_t KATAKANA_CONVERSION_RANGE_START = 0x30a1;
constexpr uint32_t KATAKANA_CONVERSION_RANGE_END = 0x30f6;

// https://github.com/yomidevs/yomitan/blob/81d17d877fb18c62ba826210bf6db2b7f4d4deed/ext/js/language/ja/japanese.js#L121
const std::unordered_map<char32_t, std::u32string> VOWEL_TO_KANA{
    {U'a', U"ぁあかがさざただなはばぱまゃやらゎわヵァアカガサザタダナハバパマャヤラヮワヵヷ"},
    {U'i', U"ぃいきぎしじちぢにひびぴみりゐィイキギシジチヂニヒビピミリヰヸ"},
    {U'u', U"ぅうくぐすずっつづぬふぶぷむゅゆるゥウクグスズッツヅヌフブプムュユルヴ"},
    {U'e', U"ぇえけげせぜてでねへべぺめれゑヶェエケゲセゼテデネヘベペメレヱヶヹ"},
    {U'o', U"ぉおこごそぞとどのほぼぽもょよろをォオコゴソゾトドノホボポモョヨロヲヺ"}};

// https://github.com/yomidevs/yomitan/blob/81d17d877fb18c62ba826210bf6db2b7f4d4deed/ext/js/language/ja/japanese.js#L131
std::unordered_map<char32_t, char32_t> build_kana_to_vowel_map() {
  std::unordered_map<char32_t, char32_t> map;
  for (const auto& [vowel, kana_string] : VOWEL_TO_KANA) {
    for (char32_t c : kana_string) {
      map.try_emplace(c, vowel);
    }
  }
  return map;
}

char32_t kana_to_vowel(char32_t kana) {
  static const auto KANA_TO_VOWEL = build_kana_to_vowel_map();
  auto it = KANA_TO_VOWEL.find(kana);
  if (it != KANA_TO_VOWEL.end()) {
    return it->second;
  }
  return 0;
}

// https://github.com/yomidevs/yomitan/blob/81d17d877fb18c62ba826210bf6db2b7f4d4deed/ext/js/language/ja/japanese.js#L155
char32_t get_prolonged_hiragana(char32_t prev) {
  switch (kana_to_vowel(prev)) {
    case U'a':
      return U'あ';
    case U'i':
      return U'い';
    case U'u':
      return U'う';
    case U'e':
      return U'え';
    case U'o':
      return U'う';
    default:
      return 0;
  }
}

bool is_in_range(uint32_t c, uint32_t range_start, uint32_t range_end) { return c >= range_start && c <= range_end; }

// https://github.com/yomidevs/yomitan/blob/81d17d877fb18c62ba826210bf6db2b7f4d4deed/ext/js/language/ja/japanese.js#L472
std::u32string hiragana_to_katakana(const std::u32string& text) {
  std::u32string result;
  const uint32_t offset = (KATAKANA_CONVERSION_RANGE_START - HIRAGANA_CONVERSION_RANGE_START);
  for (char32_t c : text) {
    if (is_in_range(c, HIRAGANA_CONVERSION_RANGE_START, HIRAGANA_CONVERSION_RANGE_END)) {
      c = static_cast<char32_t>(c + offset);
    }
    result += c;
  }
  return result;
}

// https://github.com/yomidevs/yomitan/blob/81d17d877fb18c62ba826210bf6db2b7f4d4deed/ext/js/language/ja/japanese.js#L441
std::u32string katakana_to_hiragana(const std::u32string& text) {
  std::u32string result;
  const uint32_t offset = (HIRAGANA_CONVERSION_RANGE_START - KATAKANA_CONVERSION_RANGE_START);
  for (char32_t c : text) {
    switch (c) {
      case KATAKANA_SMALL_KA:
      case KATAKANA_SMALL_KE:
        break;
      case KANA_PROLONGED_SOUND_MARK:
        if (result.length() > 0) {
          const auto prolonged = get_prolonged_hiragana(result.at(result.length() - 1));
          if (prolonged != 0) {
            c = prolonged;
          }
        }
        break;
      default:
        if (is_in_range(c, KATAKANA_CONVERSION_RANGE_START, KATAKANA_CONVERSION_RANGE_END)) {
          c = static_cast<char32_t>(c + offset);
        }
        break;
    }
    result += c;
  }
  return result;
}

std::u32string nfkc(const std::u32string& text) {
  std::string utf8 = utf8::utf32to8(text);
  utf8proc_uint8_t* out = utf8proc_NFKC(reinterpret_cast<const utf8proc_uint8_t*>(utf8.c_str()));
  if (!out) {
    return text;
  }
  std::string result(reinterpret_cast<char*>(out));
  utf8proc_free(out);
  return utf8::utf8to32(result);
}

// https://github.com/yomidevs/yomitan/blob/3440451aecb23a43f308857969c890a55ce34a91/ext/js/language/ja/japanese.js#L489
std::u32string alphanumeric_to_fullwidth(const std::u32string& text) {
  std::u32string result;
  for (char32_t c : text) {
    if (is_in_range(c, U'0', U'9')) {
      c = static_cast<char32_t>(c + (0xff10 - 0x30));
    } else if (is_in_range(c, U'A', U'Z')) {
      c = static_cast<char32_t>(c + (0xff21 - 0x41));
    } else if (is_in_range(c, U'a', U'z')) {
      c = static_cast<char32_t>(c + (0xff41 - 0x61));
    }
    result += c;
  }
  return result;
}

std::string load_kanji_mapping_json() {
  for (const auto& path : {
           "external/kanji-processor/src/full_list.json",
           "../external/kanji-processor/src/full_list.json",
           "../../external/kanji-processor/src/full_list.json",
       }) {
    std::ifstream file(path, std::ios::binary);
    if (file) {
      return {std::istreambuf_iterator<char>(file), {}};
    }
  }
  return {};
}

std::u32string standardize_kanji(const std::u32string& text) {
  static const auto map = [] {
    const auto mapping_json = load_kanji_mapping_json();
    if (mapping_json.empty()) {
      return ankerl::unordered_dense::map<char32_t, char32_t>{};
    }

    std::vector<internal::KanjiMapping> list;
    if (glz::read_json(list, mapping_json)) {
      return ankerl::unordered_dense::map<char32_t, char32_t>{};
    };

    ankerl::unordered_dense::map<char32_t, char32_t> m;
    for (const auto& [oyaji, itaiji] : list) {
      const char32_t parent = utf8::utf8to32(oyaji).front();
      for (const auto& variant : itaiji) {
        m[utf8::utf8to32(variant).front()] = parent;
      }
    }
    return m;
  }();

  std::u32string result;
  for (char32_t c : text) {
    auto it = map.find(c);
    result += it != map.end() ? it->second : c;
  }
  return result;
}

std::u32string ascii_lowercase(const std::u32string& text) {
  std::u32string result = text;
  for (auto& c : result) {
    if (c >= U'A' && c <= U'Z') {
      c = c - U'A' + U'a';
    }
  }
  return result;
}

std::u32string ascii_capitalize_first(const std::u32string& text) {
  std::u32string result = text;
  if (!result.empty() && result.front() >= U'a' && result.front() <= U'z') {
    result.front() = result.front() - U'a' + U'A';
  }
  return result;
}

std::u32string replace_all(std::u32string text, const std::u32string& from, const std::u32string& to) {
  if (from.empty()) {
    return text;
  }
  size_t pos = 0;
  while ((pos = text.find(from, pos)) != std::u32string::npos) {
    text.replace(pos, from.size(), to);
    pos += to.size();
  }
  return text;
}

std::vector<TextProcessor> get_common_latin_processors() {
  return {
      {.options = {0, 1},
       .process = [](const std::u32string& text, int opt) -> std::u32string {
         return opt == 1 ? ascii_lowercase(text) : text;
       }},
      {.options = {0, 1},
       .process = [](const std::u32string& text, int opt) -> std::u32string {
         return opt == 1 ? ascii_capitalize_first(text) : text;
       }},
  };
}

std::vector<TextProcessor> get_french_processors() {
  auto processors = get_common_latin_processors();
  processors.push_back({.options = {0, 1, 2},
                        .process = [](const std::u32string& text, int opt) -> std::u32string {
                          switch (opt) {
                            case 1:
                              return replace_all(text, U"'", U"\u2019");
                            case 2:
                              return replace_all(text, U"\u2019", U"'");
                            default:
                              return text;
                          }
                        }});
  return processors;
}

std::vector<TextProcessor> get_german_processors() {
  auto processors = get_common_latin_processors();
  processors.push_back({.options = {0, 1, 2},
                        .process = [](const std::u32string& text, int opt) -> std::u32string {
                          switch (opt) {
                            case 1:
                              return replace_all(replace_all(text, U"\u1e9e", U"SS"), U"\u00df", U"ss");
                            case 2:
                              return replace_all(replace_all(text, U"SS", U"\u1e9e"), U"ss", U"\u00df");
                            default:
                              return text;
                          }
                        }});
  return processors;
}

constexpr std::array<char32_t, 19> HANGUL_INITIALS = {
    U'\u3131', U'\u3132', U'\u3134', U'\u3137', U'\u3138', U'\u3139', U'\u3141',
    U'\u3142', U'\u3143', U'\u3145', U'\u3146', U'\u3147', U'\u3148', U'\u3149',
    U'\u314a', U'\u314b', U'\u314c', U'\u314d', U'\u314e'};

constexpr std::array<char32_t, 21> HANGUL_MEDIALS = {
    U'\u314f', U'\u3150', U'\u3151', U'\u3152', U'\u3153', U'\u3154', U'\u3155',
    U'\u3156', U'\u3157', U'\u3158', U'\u3159', U'\u315a', U'\u315b', U'\u315c',
    U'\u315d', U'\u315e', U'\u315f', U'\u3160', U'\u3161', U'\u3162', U'\u3163'};

constexpr std::array<std::u32string_view, 28> HANGUL_FINALS_DECOMPOSED = {
    U"",       U"\u3131", U"\u3132", U"\u3131\u3145", U"\u3134", U"\u3134\u3148", U"\u3134\u314e",
    U"\u3137", U"\u3139", U"\u3139\u3131", U"\u3139\u3141", U"\u3139\u3142", U"\u3139\u3145", U"\u3139\u314c",
    U"\u3139\u314d", U"\u3139\u314e", U"\u3141", U"\u3142", U"\u3142\u3145", U"\u3145", U"\u3146",
    U"\u3147", U"\u3148", U"\u314a", U"\u314b", U"\u314c", U"\u314d", U"\u314e"};

template <size_t N>
std::optional<int> index_of(const std::array<char32_t, N>& values, char32_t value) {
  for (size_t i = 0; i < values.size(); ++i) {
    if (values[i] == value) {
      return static_cast<int>(i);
    }
  }
  return std::nullopt;
}

std::optional<int> hangul_final_index(char32_t value) {
  switch (value) {
    case U'\u3131':
      return 1;
    case U'\u3132':
      return 2;
    case U'\u3134':
      return 4;
    case U'\u3137':
      return 7;
    case U'\u3139':
      return 8;
    case U'\u3141':
      return 16;
    case U'\u3142':
      return 17;
    case U'\u3145':
      return 19;
    case U'\u3146':
      return 20;
    case U'\u3147':
      return 21;
    case U'\u3148':
      return 22;
    case U'\u314a':
      return 23;
    case U'\u314b':
      return 24;
    case U'\u314c':
      return 25;
    case U'\u314d':
      return 26;
    case U'\u314e':
      return 27;
    default:
      return std::nullopt;
  }
}

std::optional<int> hangul_double_final_index(char32_t first, char32_t second) {
  if (first == U'\u3131' && second == U'\u3145') {
    return 3;
  }
  if (first == U'\u3134' && second == U'\u3148') {
    return 5;
  }
  if (first == U'\u3134' && second == U'\u314e') {
    return 6;
  }
  if (first == U'\u3139' && second == U'\u3131') {
    return 9;
  }
  if (first == U'\u3139' && second == U'\u3141') {
    return 10;
  }
  if (first == U'\u3139' && second == U'\u3142') {
    return 11;
  }
  if (first == U'\u3139' && second == U'\u3145') {
    return 12;
  }
  if (first == U'\u3139' && second == U'\u314c') {
    return 13;
  }
  if (first == U'\u3139' && second == U'\u314d') {
    return 14;
  }
  if (first == U'\u3139' && second == U'\u314e') {
    return 15;
  }
  if (first == U'\u3142' && second == U'\u3145') {
    return 18;
  }
  return std::nullopt;
}

std::u32string disassemble_hangul(const std::u32string& text) {
  std::u32string result;
  for (char32_t c : text) {
    if (c >= 0xac00 && c <= 0xd7a3) {
      const int syllable = static_cast<int>(c - 0xac00);
      const int initial = syllable / (21 * 28);
      const int medial = (syllable % (21 * 28)) / 28;
      const int final = syllable % 28;
      result += HANGUL_INITIALS[initial];
      result += HANGUL_MEDIALS[medial];
      result += HANGUL_FINALS_DECOMPOSED[final];
    } else {
      result += c;
    }
  }
  return result;
}

std::u32string assemble_hangul(const std::u32string& text) {
  std::u32string result;
  for (size_t i = 0; i < text.size();) {
    auto initial = index_of(HANGUL_INITIALS, text[i]);
    auto medial = i + 1 < text.size() ? index_of(HANGUL_MEDIALS, text[i + 1]) : std::nullopt;
    size_t medial_offset = 1;

    if (!initial.has_value()) {
      initial = 11;
      medial = index_of(HANGUL_MEDIALS, text[i]);
      medial_offset = 0;
    }

    if (!initial.has_value() || !medial.has_value()) {
      result += text[i++];
      continue;
    }

    size_t next = i + medial_offset + 1;
    int final = 0;
    if (next < text.size()) {
      const auto first_final = hangul_final_index(text[next]);
      const bool first_is_next_initial = next + 1 < text.size() && index_of(HANGUL_MEDIALS, text[next + 1]).has_value();
      if (first_final.has_value() && !first_is_next_initial) {
        final = *first_final;
        if (next + 1 < text.size()) {
          const auto double_final = hangul_double_final_index(text[next], text[next + 1]);
          const bool double_is_followed_by_vowel =
              next + 2 < text.size() && index_of(HANGUL_MEDIALS, text[next + 2]).has_value();
          if (double_final.has_value() && !double_is_followed_by_vowel) {
            final = *double_final;
            ++next;
          }
        }
        ++next;
      }
    }

    result += static_cast<char32_t>(0xac00 + ((*initial * 21 + *medial) * 28) + final);
    i = next;
  }
  return result;
}

std::vector<TextProcessor> get_korean_preprocessors() {
  return {
      {.options = {0, 1},
       .process = [](const std::u32string& text, int opt) -> std::u32string {
         return opt == 1 ? disassemble_hangul(text) : text;
       }},
  };
}

std::vector<TextProcessor> get_korean_postprocessors() {
  return {
      {.options = {0, 1},
       .process = [](const std::u32string& text, int opt) -> std::u32string {
         return opt == 1 ? assemble_hangul(text) : text;
       }},
  };
}

std::vector<TextProcessor> get_japanese_processors() {
  return {
      // https://github.com/yomidevs/yomitan/blob/81d17d877fb18c62ba826210bf6db2b7f4d4deed/ext/js/language/ja/japanese-text-preprocessors.js#L66
      {.options = {0, 1, 2},
       .process = [](const std::u32string& text, int opt) -> std::u32string {
         switch (opt) {
           case 1:
             return katakana_to_hiragana(text);
           case 2:
             return hiragana_to_katakana(text);
           default:
             return text;
         }
       }},
      {.options = {0, 1},
       .process = [](const std::u32string& text, int opt) -> std::u32string { return opt == 1 ? nfkc(text) : text; }},
      {.options = {0, 1},
       .process = [](const std::u32string& text, int opt) -> std::u32string {
         return opt == 1 ? alphanumeric_to_fullwidth(text) : text;
       }},
      {.options = {0, 1}, .process = [](const std::u32string& text, int opt) -> std::u32string {
         return opt == 1 ? standardize_kanji(text) : text;
      }}};
}

std::vector<TextProcessor> get_processors(std::string_view language) {
  if (language == "ja") {
    return get_japanese_processors();
  }
  if (language == "ko") {
    return get_korean_preprocessors();
  }
  if (language == "fr") {
    return get_french_processors();
  }
  if (language == "de") {
    return get_german_processors();
  }
  return get_common_latin_processors();
}

std::vector<TextProcessor> get_postprocessors(std::string_view language) {
  if (language == "ko") {
    return get_korean_postprocessors();
  }
  return {};
}

std::vector<TextVariant> run_processors(const std::string& src, const std::vector<TextProcessor>& processors) {
  std::u32string text = utf8::utf8to32(src);
  std::map<std::u32string, int> variants = {{text, 0}};

  for (const auto& processor : processors) {
    std::map<std::u32string, int> next;

    for (const auto& [variant, steps] : variants) {
      for (int option : processor.options) {
        auto processed = processor.process(variant, option);
        int new_steps = (processed == variant) ? steps : steps + 1;

        auto [it, inserted] = next.try_emplace(processed, new_steps);
        if (!inserted && new_steps < it->second) {
          it->second = new_steps;
        }
      }
    }
    variants = std::move(next);
  }

  std::vector<TextVariant> result;
  result.reserve(variants.size());
  for (const auto& [variant, steps] : variants) {
    result.emplace_back(TextVariant{utf8::utf32to8(variant), steps});
  }
  return result;
}
}

// https://github.com/yomidevs/yomitan/blob/81d17d877fb18c62ba826210bf6db2b7f4d4deed/ext/js/language/translator.js#L564
std::vector<TextVariant> text_processor::process(const std::string& src) {
  return process(src, "ja");
}

std::vector<TextVariant> text_processor::process(const std::string& src, const std::string& language) {
  return run_processors(src, get_processors(language));
}

std::vector<TextVariant> text_processor::postprocess(const std::string& src, const std::string& language) {
  return run_processors(src, get_postprocessors(language));
}
