#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace language_data {

enum class RuleKind : uint8_t {
  Suffix,
  Prefix,
  WholeWord,
  Sandwich,
  AsciiWordSandwich,
  GuardedPrefix,
  GuardedSuffix,
  ArabicSandwich,
  GuardedSuffixNotAfterJ,
  ReplaceAll,
  ReplaceAllNotAtStart,
  GreekXiAnaNormalize,
  EnglishPhrasalSuffix,
  EnglishInterposedPhrasalObject,
  SpanishStemSuffix,
  SpanishJugarStemSuffix,
  SpanishOlerStemSuffix,
  SpanishPronominal,
  GermanSeparatedPrefix,
  GermanBasicPastParticiple,
  GermanSeparablePastParticiple,
  TagalogRemoveFirst,
  TagalogSuffixOToU,
  TagalogPrefixReduplication,
  TagalogSandwichOToU,
  YiddishUmlautSuffix,
};

struct Condition {
  std::string_view name;
  uint32_t flags;
  bool is_dictionary_form;
};

struct Transform {
  std::string_view id;
  std::string_view name;
  std::string_view description;
};

struct Rule {
  RuleKind kind;
  std::string_view a;
  std::string_view b;
  std::string_view c;
  std::string_view d;
  std::string_view e;
  std::string_view f;
  uint32_t conditions_in;
  uint32_t conditions_out;
  uint16_t transform_index;
};

struct Language {
  std::string_view code;
  std::span<const Condition> conditions;
  std::span<const Transform> transforms;
  std::span<const Rule> rules;
};

std::span<const Language> all_languages();
const Language* find_language(std::string_view code);

}  // namespace language_data
