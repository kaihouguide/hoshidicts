#include "hoshidicts/deinflector.hpp"

#include <utf8.h>
#include <utf8proc.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "language/language_data.hpp"

namespace {
constexpr uint32_t NONE = 0;

bool starts_with(std::string_view text, std::string_view prefix) {
  return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

bool ends_with(std::string_view text, std::string_view suffix) {
  return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

std::string replace_suffix(std::string_view text, std::string_view suffix, std::string_view replacement) {
  return std::string(text.substr(0, text.size() - suffix.size())) + std::string(replacement);
}

std::string replace_prefix(std::string_view text, std::string_view prefix, std::string_view replacement) {
  return std::string(replacement) + std::string(text.substr(prefix.size()));
}

bool conditions_match(uint32_t current_conditions, uint32_t next_conditions) {
  return current_conditions == NONE || (current_conditions & next_conditions) != 0;
}

bool is_ascii_or_german_letter(unsigned char c) {
  return std::isalpha(c) != 0 || c >= 0x80;
}

bool is_german_stem(std::string_view stem) {
  return !stem.empty() &&
         std::ranges::all_of(stem, [](unsigned char c) { return is_ascii_or_german_letter(c); });
}

bool is_ascii_word(std::string_view text) {
  return !text.empty() && std::ranges::all_of(text, [](unsigned char c) {
           return std::isalnum(c) != 0 || c == '_';
         });
}

bool is_arabic_letter(char32_t c) {
  return (c >= 0x0620 && c <= 0x065f) || (c >= 0x066e && c <= 0x06d3) || c == 0x06d5 ||
         c == 0x06ee || c == 0x06ef || (c >= 0x06fa && c <= 0x06fc) || c == 0x06ff;
}

bool is_arabic_letters(std::string_view text) {
  if (text.empty()) {
    return false;
  }
  auto it = text.begin();
  while (it != text.end()) {
    const char32_t c = utf8::next(it, text.end());
    if (!is_arabic_letter(c)) {
      return false;
    }
  }
  return true;
}

std::vector<std::string_view> split_pipe(std::string_view text) {
  std::vector<std::string_view> result;
  size_t start = 0;
  while (start <= text.size()) {
    const size_t pos = text.find('|', start);
    if (pos == std::string_view::npos) {
      result.push_back(text.substr(start));
      break;
    }
    result.push_back(text.substr(start, pos - start));
    start = pos + 1;
  }
  return result;
}

std::optional<std::string_view> get_matching_suffix(std::string_view text, std::string_view suffixes) {
  for (std::string_view suffix : split_pipe(suffixes)) {
    if (ends_with(text, suffix)) {
      return suffix;
    }
  }
  return std::nullopt;
}

std::string replace_first(std::string_view text, std::string_view from, std::string_view to) {
  const size_t pos = text.find(from);
  if (pos == std::string_view::npos) {
    return std::string(text);
  }
  return std::string(text.substr(0, pos)) + std::string(to) + std::string(text.substr(pos + from.size()));
}

std::optional<std::string> replace_suffix_alternative(std::string_view text, std::string_view suffixes,
                                                      std::string_view replacement) {
  auto suffix = get_matching_suffix(text, suffixes);
  if (!suffix.has_value()) {
    return std::nullopt;
  }
  return replace_suffix(text, *suffix, replacement);
}

bool ascii_lower_between(std::string_view text, size_t begin, size_t end) {
  if (begin > end || end > text.size()) {
    return false;
  }
  for (size_t i = begin; i < end; ++i) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (c < 'a' || c > 'z') {
      return false;
    }
  }
  return true;
}

std::optional<std::string> apply_spanish_stem_suffix(std::string_view text, std::string_view inflected_stem,
                                                     std::string_view deinflected_stem, std::string_view suffixes,
                                                     std::string_view deinflected_suffix) {
  const size_t stem_pos = text.find(inflected_stem);
  if (stem_pos == std::string_view::npos) {
    return std::nullopt;
  }
  auto suffix = get_matching_suffix(text, suffixes);
  if (!suffix.has_value()) {
    return std::nullopt;
  }
  const size_t suffix_pos = text.size() - suffix->size();
  if (suffix_pos < stem_pos + inflected_stem.size() ||
      !ascii_lower_between(text, stem_pos + inflected_stem.size(), suffix_pos)) {
    return std::nullopt;
  }
  auto stem_replaced = replace_first(text, inflected_stem, deinflected_stem);
  return replace_suffix(stem_replaced, *suffix, deinflected_suffix);
}

std::string strip_combining_diacritics(std::string_view text) {
  utf8proc_uint8_t* decomposed = nullptr;
  const auto length = utf8proc_map(reinterpret_cast<const utf8proc_uint8_t*>(text.data()),
                                  static_cast<utf8proc_ssize_t>(text.size()), &decomposed, UTF8PROC_DECOMPOSE);
  if (length < 0 || decomposed == nullptr) {
    return std::string(text);
  }

  std::string decomposed_string(reinterpret_cast<char*>(decomposed), static_cast<size_t>(length));
  utf8proc_free(decomposed);

  std::string result;
  auto it = decomposed_string.begin();
  while (it != decomposed_string.end()) {
    auto before = it;
    const char32_t c = utf8::next(it, decomposed_string.end());
    if (c < 0x0300 || c > 0x036f) {
      result.append(before, it);
    }
  }
  return result;
}

std::string replace_all(std::string_view text, std::string_view from, std::string_view to, bool skip_start) {
  std::string result(text);
  if (from.empty()) {
    return result;
  }
  size_t pos = 0;
  while ((pos = result.find(from, pos)) != std::string::npos) {
    if (skip_start && pos == 0) {
      pos += from.size();
      continue;
    }
    result.replace(pos, from.size(), to);
    pos += to.size();
  }
  return result;
}

constexpr auto ENGLISH_PHRASAL_PARTICLES = std::to_array<std::string_view>({
    "aboard",    "about",   "above",    "across",    "ahead",     "alongside", "apart",   "around",
    "aside",     "astray",  "away",     "back",      "before",    "behind",    "below",   "beneath",
    "besides",   "between", "beyond",   "by",        "close",     "down",      "east",    "west",
    "north",     "south",   "eastward", "westward",  "northward", "southward", "forward", "backward",
    "backwards", "forwards","home",     "in",        "inside",    "instead",   "near",    "off",
    "on",        "opposite","out",      "outside",   "over",      "overhead",  "past",    "round",
    "since",     "through", "throughout","together", "under",     "underneath", "up",      "within",
    "without"});

constexpr auto ENGLISH_PHRASAL_WORDS = std::to_array<std::string_view>({
    "aboard",    "about",   "above",    "across",    "ahead",     "alongside", "apart",    "around",
    "aside",     "astray",  "away",     "back",      "before",    "behind",    "below",    "beneath",
    "besides",   "between", "beyond",   "by",        "close",     "down",      "east",     "west",
    "north",     "south",   "eastward", "westward",  "northward", "southward", "forward",  "backward",
    "backwards", "forwards","home",     "in",        "inside",    "instead",   "near",     "off",
    "on",        "opposite","out",      "outside",   "over",      "overhead",  "past",     "round",
    "since",     "through", "throughout","together", "under",     "underneath", "up",       "within",
    "without",   "aback",   "after",    "against",   "along",     "among",     "as",       "at",
    "even",      "for",     "forth",    "from",      "into",      "of",        "onto",     "open",
    "to",        "toward",  "towards",  "upon",      "way",       "with"});

template <size_t N>
bool starts_with_any(std::string_view text, const std::array<std::string_view, N>& words) {
  return std::ranges::any_of(words, [&](std::string_view word) { return starts_with(text, word); });
}

template <size_t N>
bool contains_word(const std::array<std::string_view, N>& words, std::string_view value) {
  return std::ranges::any_of(words, [&](std::string_view word) { return word == value; });
}

std::vector<std::string_view> split_spaces(std::string_view text) {
  std::vector<std::string_view> result;
  size_t start = 0;
  while (start < text.size()) {
    while (start < text.size() && text[start] == ' ') {
      ++start;
    }
    if (start >= text.size()) {
      break;
    }
    size_t end = text.find(' ', start);
    if (end == std::string_view::npos) {
      end = text.size();
    }
    result.push_back(text.substr(start, end - start));
    start = end + 1;
  }
  return result;
}

bool is_tagalog_consonant(char c, std::string_view consonants) {
  return consonants.find(c) != std::string_view::npos;
}

bool is_tagalog_vowel(char c) { return std::string_view("aeiou").find(c) != std::string_view::npos; }

bool is_yiddish_mutation_candidate(char32_t c) {
  switch (c) {
    case U'\u05e2':
    case U'\u05f0':
    case U'\u05d0':
    case U'\ufb2e':
    case U'\u05f1':
    case U'\u05d5':
    case U'\u05f2':
    case U'\ufb1d':
    case U'\ufb1f':
    case U'\u05d9':
    case U'\ufb2f':
      return true;
    default:
      return false;
  }
}

std::optional<std::string> apply_rule(const language_data::Rule& rule, std::string_view text) {
  using language_data::RuleKind;
  switch (rule.kind) {
    case RuleKind::Suffix:
      if (!ends_with(text, rule.a)) {
        return std::nullopt;
      }
      if (rule.a.empty()) {
        return std::string(rule.b);
      }
      return replace_suffix(text, rule.a, rule.b);

    case RuleKind::Prefix:
      if (!starts_with(text, rule.a)) {
        return std::nullopt;
      }
      return replace_prefix(text, rule.a, rule.b);

    case RuleKind::WholeWord:
      if (text != rule.a) {
        return std::nullopt;
      }
      return std::string(rule.b);

    case RuleKind::Sandwich:
      if (!starts_with(text, rule.a) || !ends_with(text, rule.c) || text.size() < rule.a.size() + rule.c.size()) {
        return std::nullopt;
      }
      return std::string(rule.b) + std::string(text.substr(rule.a.size(), text.size() - rule.a.size() - rule.c.size())) +
             std::string(rule.d);

    case RuleKind::AsciiWordSandwich: {
      if (!starts_with(text, rule.a) || !ends_with(text, rule.c) || text.size() < rule.a.size() + rule.c.size()) {
        return std::nullopt;
      }
      const auto inner = text.substr(rule.a.size(), text.size() - rule.a.size() - rule.c.size());
      if (!is_ascii_word(inner)) {
        return std::nullopt;
      }
      return std::string(rule.b) + std::string(inner) + std::string(rule.d);
    }

    case RuleKind::GuardedPrefix: {
      if (!starts_with(text, rule.a)) {
        return std::nullopt;
      }
      const auto stem = text.substr(rule.a.size());
      if (!starts_with(stem, rule.e)) {
        return std::nullopt;
      }
      return std::string(rule.b) + std::string(stem);
    }

    case RuleKind::GuardedSuffix: {
      if (!ends_with(text, rule.a)) {
        return std::nullopt;
      }
      const auto stem = text.substr(0, text.size() - rule.a.size());
      if (!ends_with(stem, rule.e)) {
        return std::nullopt;
      }
      return std::string(stem) + std::string(rule.b);
    }

    case RuleKind::ArabicSandwich: {
      if (!starts_with(text, rule.a) || !ends_with(text, rule.c) || text.size() < rule.a.size() + rule.c.size()) {
        return std::nullopt;
      }
      const auto inner = text.substr(rule.a.size(), text.size() - rule.a.size() - rule.c.size());
      if (!starts_with(inner, rule.e) || !ends_with(inner, rule.f) || inner.size() < rule.e.size() + rule.f.size()) {
        return std::nullopt;
      }
      const auto core = inner.substr(rule.e.size(), inner.size() - rule.e.size() - rule.f.size());
      if (!is_arabic_letters(core)) {
        return std::nullopt;
      }
      return std::string(rule.b) + std::string(inner) + std::string(rule.d);
    }

    case RuleKind::GuardedSuffixNotAfterJ:
      if (!ends_with(text, rule.a) || text.size() <= rule.a.size()) {
        return std::nullopt;
      }
      if (text[text.size() - rule.a.size() - 1] == 'j') {
        return std::nullopt;
      }
      return replace_suffix(text, rule.a, rule.b);

    case RuleKind::ReplaceAll:
      if (text.find(rule.a) == std::string_view::npos) {
        return std::nullopt;
      }
      return replace_all(text, rule.a, rule.b, false);

    case RuleKind::ReplaceAllNotAtStart: {
      const auto pos = text.find(rule.a, 1);
      if (pos == std::string_view::npos) {
        return std::nullopt;
      }
      return replace_all(text, rule.a, rule.b, true);
    }

    case RuleKind::GreekXiAnaNormalize:
      if (!starts_with(text, rule.a)) {
        return std::nullopt;
      }
      return strip_combining_diacritics(text.substr(rule.a.size()));

    case RuleKind::EnglishPhrasalSuffix: {
      const size_t space = text.find(' ');
      if (space == std::string_view::npos) {
        return std::nullopt;
      }
      const auto first_word = text.substr(0, space);
      const auto rest = text.substr(space + 1);
      if (!ends_with(first_word, rule.a) || !starts_with_any(rest, ENGLISH_PHRASAL_WORDS)) {
        return std::nullopt;
      }
      return replace_suffix(first_word, rule.a, rule.b) + " " + std::string(rest);
    }

    case RuleKind::EnglishInterposedPhrasalObject: {
      const auto words = split_spaces(text);
      if (words.size() < 3) {
        return std::nullopt;
      }
      for (size_t particle_index = 2; particle_index < words.size(); ++particle_index) {
        if (!contains_word(ENGLISH_PHRASAL_PARTICLES, words[particle_index])) {
          continue;
        }
        bool middle_has_phrasal_word = false;
        for (size_t i = 1; i < particle_index; ++i) {
          if (contains_word(ENGLISH_PHRASAL_WORDS, words[i])) {
            middle_has_phrasal_word = true;
            break;
          }
        }
        if (middle_has_phrasal_word) {
          continue;
        }
        std::string result(words[0]);
        for (size_t i = particle_index; i < words.size(); ++i) {
          result += " ";
          result += words[i];
        }
        return result;
      }
      return std::nullopt;
    }

    case RuleKind::SpanishStemSuffix:
      return apply_spanish_stem_suffix(text, rule.a, rule.b, rule.c, rule.d);

    case RuleKind::SpanishJugarStemSuffix: {
      if (starts_with(text, "jue")) {
        auto stem_replaced = replace_first(text, "ue", "u");
        return replace_suffix_alternative(stem_replaced, rule.e, rule.d);
      }
      return apply_spanish_stem_suffix(text, rule.a, rule.b, rule.c, rule.d);
    }

    case RuleKind::SpanishOlerStemSuffix: {
      if (starts_with(text, "hue")) {
        auto stem_replaced = replace_first(text, "hue", "o");
        return replace_suffix_alternative(stem_replaced, rule.c, rule.d);
      }
      return apply_spanish_stem_suffix(text, rule.a, rule.b, rule.c, rule.d);
    }

    case RuleKind::SpanishPronominal: {
      const auto words = split_spaces(text);
      static constexpr auto pronouns = std::to_array<std::string_view>({"me", "te", "se", "nos", "os"});
      for (size_t i = 0; i + 1 < words.size(); ++i) {
        if (!contains_word(pronouns, words[i])) {
          continue;
        }
        const auto verb = words[i + 1];
        auto ending = get_matching_suffix(verb, "ar|er|ir");
        if (!ending.has_value()) {
          continue;
        }
        std::string result;
        for (size_t j = 0; j < i; ++j) {
          if (!result.empty()) {
            result += " ";
          }
          result += words[j];
        }
        if (!result.empty()) {
          result += " ";
        }
        result += verb;
        result += "se";
        for (size_t j = i + 2; j < words.size(); ++j) {
          result += " ";
          result += words[j];
        }
        return result;
      }
      return std::nullopt;
    }

    case RuleKind::GermanSeparatedPrefix: {
      const auto prefix = rule.a;
      if (!ends_with(text, prefix) || text.size() <= prefix.size() + 2) {
        return std::nullopt;
      }
      const auto before_prefix = text.substr(0, text.size() - prefix.size());
      if (!ends_with(before_prefix, " ")) {
        return std::nullopt;
      }
      const auto first_space = before_prefix.find(' ');
      if (first_space == std::string_view::npos || first_space == 0) {
        return std::nullopt;
      }
      return std::string(before_prefix.substr(0, first_space)) + " " + std::string(prefix);
    }

    case RuleKind::GermanBasicPastParticiple: {
      if (!starts_with(text, "ge") || !ends_with(text, "t") || text.size() <= 3) {
        return std::nullopt;
      }
      const auto stem = text.substr(2, text.size() - 3);
      if (!is_german_stem(stem)) {
        return std::nullopt;
      }
      return std::string(stem) + std::string(rule.b);
    }

    case RuleKind::GermanSeparablePastParticiple: {
      const auto prefix = rule.a;
      const auto ge_prefix = std::string(prefix) + "ge";
      if (!starts_with(text, ge_prefix) || !ends_with(text, "t") || text.size() <= ge_prefix.size() + 1) {
        return std::nullopt;
      }
      const auto stem = text.substr(ge_prefix.size(), text.size() - ge_prefix.size() - 1);
      if (!is_german_stem(stem)) {
        return std::nullopt;
      }
      return std::string(prefix) + std::string(stem) + std::string(rule.b);
    }

    case RuleKind::TagalogRemoveFirst: {
      const size_t pos = text.find(rule.a);
      if (pos == std::string_view::npos) {
        return std::nullopt;
      }
      return std::string(text.substr(0, pos)) + std::string(text.substr(pos + rule.a.size()));
    }

    case RuleKind::TagalogSuffixOToU: {
      static constexpr std::string_view consonants = "bcdfghjklmnpqrstvwxyzBCDFGHJKLMNPQRSTVWXYZ";
      if (!ends_with(text, rule.a)) {
        return std::nullopt;
      }
      const auto stem = text.substr(0, text.size() - rule.a.size());
      for (size_t i = 0; i + 1 < stem.size(); ++i) {
        if (stem[i] != 'u') {
          continue;
        }
        const auto consonant_tail = stem.substr(i + 1);
        if (!consonant_tail.empty() &&
            std::ranges::all_of(consonant_tail, [&](char c) { return is_tagalog_consonant(c, consonants); })) {
          return std::string(stem.substr(0, i)) + "o" + std::string(consonant_tail) + std::string(rule.b);
        }
      }
      return std::nullopt;
    }

    case RuleKind::TagalogPrefixReduplication: {
      if (!starts_with(text, rule.a)) {
        return std::nullopt;
      }
      const auto rest = text.substr(rule.a.size());
      size_t vowel_index = std::string_view::npos;
      for (size_t i = 0; i < rest.size(); ++i) {
        if (is_tagalog_vowel(rest[i])) {
          vowel_index = i;
          break;
        }
        if (!is_tagalog_consonant(rest[i], rule.c)) {
          return std::nullopt;
        }
      }
      if (vowel_index == std::string_view::npos) {
        return std::nullopt;
      }
      const auto syllable = rest.substr(0, vowel_index + 1);
      const auto after_syllable = rest.substr(syllable.size());
      if (!starts_with(after_syllable, syllable)) {
        return std::nullopt;
      }
      return std::string(rule.b) + std::string(syllable) + std::string(after_syllable.substr(syllable.size()));
    }

    case RuleKind::TagalogSandwichOToU: {
      static constexpr std::string_view consonants = "bcdfghjklmnpqrstvwxyzBCDFGHJKLMNPQRSTVWXYZ";
      if (!starts_with(text, rule.a) || !ends_with(text, rule.c) || text.size() < rule.a.size() + rule.c.size()) {
        return std::nullopt;
      }
      const auto inner = text.substr(rule.a.size(), text.size() - rule.a.size() - rule.c.size());
      for (size_t i = inner.size(); i-- > 1;) {
        if (inner[i] != 'u') {
          continue;
        }
        const auto word_part = inner.substr(0, i);
        const auto consonant_tail = inner.substr(i + 1);
        if (is_ascii_word(word_part) && !consonant_tail.empty() &&
            std::ranges::all_of(consonant_tail, [&](char c) { return is_tagalog_consonant(c, consonants); })) {
          return std::string(rule.b) + std::string(word_part) + "o" + std::string(consonant_tail) +
                 std::string(rule.d);
        }
      }
      return std::nullopt;
    }

    case RuleKind::YiddishUmlautSuffix: {
      if (!ends_with(text, rule.a)) {
        return std::nullopt;
      }
      const auto stem = text.substr(0, text.size() - rule.a.size());
      const auto inflected = utf8::utf8to32(std::string(rule.c));
      if (inflected.empty()) {
        return std::nullopt;
      }

      std::optional<size_t> match_begin;
      size_t match_end = 0;
      auto it = stem.begin();
      while (it != stem.end()) {
        auto before = it;
        const char32_t c = utf8::next(it, stem.end());
        if (is_yiddish_mutation_candidate(c)) {
          match_begin = static_cast<size_t>(before - stem.begin());
          match_end = static_cast<size_t>(it - stem.begin());
          if (c != inflected.front()) {
            match_begin.reset();
          }
        }
      }
      if (!match_begin.has_value()) {
        return std::nullopt;
      }
      return std::string(stem.substr(0, *match_begin)) + std::string(rule.d) + std::string(stem.substr(match_end)) +
             std::string(rule.b);
    }
  }
  return std::nullopt;
}

const language_data::Language& current_language_data(const std::string& language_code) {
  const auto* language = language_data::find_language(language_code);
  if (language == nullptr) {
    language = language_data::find_language("ja");
  }
  return *language;
}

}  // namespace

Deinflector::Deinflector(std::string language) : language_(std::move(language)) {
  if (language_data::find_language(language_) == nullptr) {
    language_ = "ja";
  }
}

void Deinflector::set_language(const std::string& language) {
  language_ = language_data::find_language(language) == nullptr ? "ja" : language;
}

const std::string& Deinflector::language() const { return language_; }

std::vector<DeinflectionResult> Deinflector::deinflect(const std::string& text) const {
  std::vector<DeinflectionResult> result{};
  std::vector<TransformGroup> trace{};
  deinflect_recursive(text, NONE, trace, result);
  return result;
}

uint32_t Deinflector::get_condition_flags_from_parts_of_speech(
    const std::vector<std::string>& part_of_speech) const {
  const auto& data = current_language_data(language_);
  uint32_t result = 0;
  for (const auto& p : part_of_speech) {
    for (const auto& condition : data.conditions) {
      if (condition.is_dictionary_form && condition.name == p) {
        result |= condition.flags;
        break;
      }
    }
  }
  return result;
}

uint32_t Deinflector::get_condition_flags_from_condition_type(const std::string& condition_type) const {
  const auto& data = current_language_data(language_);
  for (const auto& condition : data.conditions) {
    if (condition.name == condition_type) {
      return condition.flags;
    }
  }
  return 0;
}

std::vector<std::string> Deinflector::supported_languages() {
  std::vector<std::string> result;
  for (const auto& language : language_data::all_languages()) {
    result.emplace_back(language.code);
  }
  return result;
}

uint32_t Deinflector::pos_to_conditions(const std::vector<std::string>& part_of_speech) {
  return Deinflector("ja").get_condition_flags_from_parts_of_speech(part_of_speech);
}

void Deinflector::deinflect_recursive(const std::string& text, uint32_t conditions, std::vector<TransformGroup>& trace,
                                      std::vector<DeinflectionResult>& results) const {
  if (text.empty()) {
    return;
  }

  results.emplace_back(text, conditions, trace);
  if (utf8::distance(text.begin(), text.end()) <= 1 || trace.size() >= 16) {
    return;
  }

  const auto& data = current_language_data(language_);
  for (const auto& rule : data.rules) {
    if (!conditions_match(conditions, rule.conditions_in)) {
      continue;
    }

    auto transformed = apply_rule(rule, text);
    if (!transformed.has_value() || transformed->empty() || *transformed == text) {
      continue;
    }

    const auto& transform = data.transforms[rule.transform_index];
    trace.insert(trace.begin(),
                 TransformGroup{.name = std::string(transform.name), .description = std::string(transform.description)});
    deinflect_recursive(*transformed, rule.conditions_out, trace, results);
    trace.erase(trace.begin());
  }
}
