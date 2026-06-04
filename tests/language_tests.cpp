#include <algorithm>
#include <cctype>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <utf8.h>

#include "hoshidicts/deinflector.hpp"
#include "../src/text_processor/text_processor.hpp"

namespace {
struct DeinflectionExpectation {
  std::string source;
  std::string expected;
  std::string condition;
  std::vector<std::string> trace;
};

struct ParagraphCase {
  std::string language;
  std::vector<std::string> paragraphs;
  std::vector<DeinflectionExpectation> expectations;
};

struct ParagraphOutput {
  std::string source;
  std::string variant;
  DeinflectionResult result;
};

struct ParagraphRun {
  size_t candidate_count = 0;
  size_t result_count = 0;
  std::vector<ParagraphOutput> outputs;
};

bool conditions_match(uint32_t current_conditions, uint32_t expected_conditions) {
  return current_conditions == 0 || (current_conditions & expected_conditions) != 0;
}

bool is_ascii_punctuation_delimiter(char32_t codepoint) {
  if (codepoint > 0x7f) {
    return false;
  }

  const auto ch = static_cast<unsigned char>(codepoint);
  return std::ispunct(ch) != 0 && ch != '\'' && ch != '-';
}

bool is_unicode_delimiter(char32_t codepoint) {
  switch (codepoint) {
    case U' ':
    case U'\t':
    case U'\n':
    case U'\r':
    case U'.':
    case U',':
    case U';':
    case U':':
    case U'!':
    case U'?':
    case U'(':
    case U')':
    case U'[':
    case U']':
    case U'{':
    case U'}':
    case U'"':
    case U'\u00ab':
    case U'\u00bb':
    case U'\u061b':
    case U'\u061f':
    case U'\u060c':
    case U'\u3001':
    case U'\u3002':
    case U'\uff0c':
    case U'\uff01':
    case U'\uff1f':
      return true;
    default:
      return is_ascii_punctuation_delimiter(codepoint);
  }
}

void flush_candidate(std::u32string& current, std::vector<std::string>& candidates) {
  if (current.empty()) {
    return;
  }

  candidates.push_back(utf8::utf32to8(current));
  current.clear();
}

std::vector<std::string> extract_candidates(std::string_view paragraph) {
  std::vector<std::string> candidates;
  std::u32string current;
  const auto codepoints = utf8::utf8to32(paragraph);
  for (const auto codepoint : codepoints) {
    if (is_unicode_delimiter(codepoint)) {
      flush_candidate(current, candidates);
      continue;
    }

    current.push_back(codepoint);
  }
  flush_candidate(current, candidates);
  return candidates;
}

std::string join_tokens(const std::vector<std::string>& tokens, size_t offset, size_t count) {
  std::string joined;
  for (size_t i = 0; i < count; ++i) {
    if (!joined.empty()) {
      joined += ' ';
    }
    joined += tokens[offset + i];
  }
  return joined;
}

std::vector<std::string> extract_paragraph_candidates(const std::vector<std::string>& paragraphs,
                                                      size_t max_phrase_words) {
  std::set<std::string> unique_candidates;
  for (const auto& paragraph : paragraphs) {
    const auto tokens = extract_candidates(paragraph);
    for (size_t offset = 0; offset < tokens.size(); ++offset) {
      const auto remaining = tokens.size() - offset;
      const auto limit = std::min(max_phrase_words, remaining);
      for (size_t count = 1; count <= limit; ++count) {
        unique_candidates.insert(join_tokens(tokens, offset, count));
      }
    }
  }

  return {unique_candidates.begin(), unique_candidates.end()};
}

bool has_trace(const DeinflectionResult& result, const std::vector<std::string>& expected_trace) {
  if (result.trace.size() != expected_trace.size()) {
    return false;
  }
  for (size_t i = 0; i < expected_trace.size(); ++i) {
    if (result.trace[i].name != expected_trace[i]) {
      return false;
    }
  }
  return true;
}

std::string trace_to_string(const DeinflectionResult& result) {
  std::string trace;
  for (const auto& rule : result.trace) {
    if (!trace.empty()) {
      trace += " -> ";
    }
    trace += rule.name;
  }
  return trace;
}

bool paragraph_output_matches(const Deinflector& deinflector, const ParagraphOutput& output,
                              const DeinflectionExpectation& expectation) {
  const bool source_matches = output.source == expectation.source || output.variant == expectation.source;
  const auto expected_conditions = deinflector.get_condition_flags_from_condition_type(expectation.condition);
  return source_matches && output.result.text == expectation.expected &&
         conditions_match(output.result.conditions, expected_conditions) && has_trace(output.result, expectation.trace);
}

void expect_deinflection(const std::string& language, const std::string& source, const std::string& expected,
                         const std::string& condition, const std::vector<std::string>& trace) {
  Deinflector deinflector(language);
  const uint32_t expected_conditions = deinflector.get_condition_flags_from_condition_type(condition);
  for (const auto& result : deinflector.deinflect(source)) {
    if (result.text == expected && conditions_match(result.conditions, expected_conditions) && has_trace(result, trace)) {
      return;
    }
  }

  throw std::runtime_error("Missing deinflection: [" + language + "] " + source + " -> " + expected);
}

void expect_deinflection(const std::string& language, const DeinflectionExpectation& expectation) {
  expect_deinflection(language, expectation.source, expectation.expected, expectation.condition, expectation.trace);
}

void expect_variant(const std::string& language, const std::string& source, const std::string& expected) {
  for (const auto& variant : text_processor::process(source, language)) {
    if (variant.text == expected) {
      return;
    }
  }

  throw std::runtime_error("Missing text variant: [" + language + "] " + source + " -> " + expected);
}

void expect_postvariant(const std::string& language, const std::string& source, const std::string& expected) {
  for (const auto& variant : text_processor::postprocess(source, language)) {
    if (variant.text == expected) {
      return;
    }
  }

  throw std::runtime_error("Missing postprocessed text variant: [" + language + "] " + source + " -> " + expected);
}

void expect_supported_language(const std::string& expected) {
  for (const auto& language : Deinflector::supported_languages()) {
    if (language == expected) {
      return;
    }
  }

  throw std::runtime_error("Missing supported language: " + expected);
}

void test_supported_languages() {
  for (const auto& language : {"ar",  "de", "el",  "en",  "eo", "es", "eu", "fr", "ga",
                               "grc", "ja", "kat", "ko",  "la", "sga", "sq", "tl", "yi"}) {
    expect_supported_language(language);
  }
}

void test_french() {
  expect_variant("fr", "l'amour", "l\u2019amour");
  expect_variant("fr", "l\u2019amour", "l'amour");
  expect_deinflection("fr", "aiment", "aimer", "v", {"present indicative"});
  expect_deinflection("fr", "aimais", "aimer", "v", {"imperfect indicative"});
  expect_deinflection("fr", "mangeons", "manger", "v", {"present indicative"});
  expect_deinflection("fr", "mangeraient", "manger", "v", {"conditional"});
  expect_deinflection("fr", "finissait", "finir", "v", {"imperfect indicative"});
}

void test_german() {
  expect_variant("de", "Strasse", "Stra\u00dfe");
  expect_variant("de", "Stra\u00dfe", "Strasse");
  expect_deinflection("de", "reinigung", "reinigen", "v", {"nominalization"});
  expect_deinflection("de", "aufzur\u00e4umen", "aufr\u00e4umen", "v", {"zu-infinitive"});
  expect_deinflection("de", "dargestellt", "darstellen", "v", {"past participle"});
  expect_deinflection("de", "r\u00e4um den Tisch auf", "r\u00e4um auf", "v", {"separated prefix"});
  expect_deinflection("de", "anwendbarkeit", "anwenden", "v", {"-bar", "-heit"});
}

void test_english() {
  expect_deinflection("en", "walked", "walk", "v", {"past"});
  expect_deinflection("en", "walked up", "walk up", "v_phr", {"past"});
  expect_deinflection("en", "look the word up", "look up", "v_phr", {"interposed object"});
  expect_deinflection("en", "going to walk", "walk", "v", {"going-to future"});
  expect_deinflection("en", "walking", "walk", "v", {"ing"});
  expect_deinflection("en", "cats", "cat", "ns", {"plural"});
  expect_deinflection("en", "dirty", "dirt", "ns", {"-y"});
  expect_deinflection("en", "hazy", "haze", "ns", {"-y"});
  expect_deinflection("en", "cooler", "cool", "adj", {"comparative"});
  expect_deinflection("en", "quickly", "quick", "adj", {"adverb"});
}

void test_other_generated_languages() {
  expect_deinflection("ar", "\u0643\u062a\u0627\u0628\u0643", "\u0643\u062a\u0627\u0628", "n", {"pos. pron."});
  expect_deinflection("el", "\u03be\u03b1\u03bd\u03b1\u03c1\u03ce\u03c4\u03b7\u03c3\u03b5",
                      "\u03c1\u03ce\u03c4\u03b7\u03c3\u03b5", "v", {"\u03be\u03b1\u03bd\u03b1-"});
  expect_deinflection("el", "\u03be\u03b1\u03bd\u03b1\u03c1\u03ce\u03c4\u03b7\u03c3\u03b5",
                      "\u03c1\u03c9\u03c4\u03b7\u03c3\u03b5", "v", {"\u03be\u03b1\u03bd\u03b1-"});
  expect_deinflection("es", "gatos", "gato", "ns", {"plural"});
  expect_deinflection("es", "luces", "luz", "ns", {"plural"});
  expect_deinflection("es", "roja", "rojo", "adj", {"feminine adjective"});
  expect_deinflection("es", "pienso", "pensar", "v_ar", {"present indicative"});
  expect_deinflection("es", "juego", "jugar", "v_ar", {"present indicative"});
  expect_deinflection("es", "huele", "oler", "v_er", {"present indicative"});
  expect_deinflection("es", "me despertar", "despertarse", "v", {"pronominal"});
  expect_deinflection("eo", "malbona", "bona", "adj", {"mal-"});
  expect_deinflection("eo", "amikon", "amiko", "n", {"accusative"});
  expect_deinflection("eo", "amiketo", "amiko", "n", {"diminutive"});
  expect_deinflection("eu", "etxean", "etxe", "n", {"Inessive Singular"});
  expect_deinflection("ga", "gclann", "clann", "n", {"eclipsis"});
  expect_deinflection("sq", "fshiva", "fshij", "v", {"aorist first-person singular indicative"});
  expect_deinflection("sq", "fshijm\u00eb", "fshij", "v", {"present indicative first-person plural"});
  expect_deinflection("la", "fluvii", "fluvius", "n", {"plural"});
  expect_deinflection("la", "vocabulo", "vocabulum", "n", {"ablative"});
  expect_deinflection("sga", "cind", "cinn", "", {"nd for nn"});
  expect_deinflection("sga", "cgcg", "cc", "", {"cg for c"});
  expect_deinflection("kat", "\u10e1\u10d0\u10ee\u10da\u10d4\u10d1\u10d8", "\u10e1\u10d0\u10ee\u10da", "n",
                      {"noun-adj-suffix-stripping"});
  expect_variant("ko", "\uba39\ub2e4", "\u3141\u3153\u3131\u3137\u314f");
  expect_postvariant("ko", "\u3141\u3153\u3131\u3137\u314f", "\uba39\ub2e4");
  expect_deinflection("ko", "\u3131\u314f\u3142", "\u3131\u314f\u3142\u3137\u314f", "v",
                      {"\uc5b4\uac04"});
  expect_deinflection("ko", "\u3131\u314f\u3142", "\ub2e4", "ida", {"-\ub85c\ub77c"});
  expect_deinflection("tl", "bahayan", "bahay", "n", {"-an"});
  expect_deinflection("tl", "magbabasa", "basa", "n", {"mag- + rep1"});
  expect_deinflection("tl", "puktan", "pokt", "n", {"-an"});
  expect_deinflection("tl", "kapuktan", "pokt", "n", {"ka-...-an"});
  expect_deinflection("yi", "\u05d4\u05d5\u05e0\u05d8\u05e1", "\u05d4\u05d5\u05e0\u05d8", "ns", {"plural"});
  expect_deinflection("yi", "\u05e2\u05e2\u05e8", "\ufb2e", "ns", {"umlaut_plural"});
  expect_deinflection("grc", "\u03bb\u03cd\u03b5\u03b9\u03c2", "\u03bb\u03cd\u03c9", "v",
                      {"2nd person singular present active indicative"});
  expect_deinflection("grc", "\u03bb\u03cd\u03bf\u03bc\u03b5\u03bd", "\u03bb\u03cd\u03c9", "v",
                      {"1st person plural present active indicative"});
}

ParagraphRun run_paragraph_pipeline(const std::string& language, const std::vector<std::string>& paragraphs,
                                    size_t max_phrase_words) {
  Deinflector deinflector(language);
  ParagraphRun run;

  for (const auto& paragraph : paragraphs) {
    for (const auto& variant : text_processor::process(paragraph, language)) {
      (void)text_processor::postprocess(variant.text, language);
    }
  }

  for (const auto& candidate : extract_paragraph_candidates(paragraphs, max_phrase_words)) {
    ++run.candidate_count;
    for (const auto& variant : text_processor::process(candidate, language)) {
      for (const auto& result : deinflector.deinflect(variant.text)) {
        (void)text_processor::postprocess(result.text, language);
        ++run.result_count;
        run.outputs.push_back({candidate, variant.text, result});
      }
    }
  }

  if (run.candidate_count == 0) {
    throw std::runtime_error("Paragraph stress found no candidates for language: " + language);
  }
  if (run.result_count == 0) {
    throw std::runtime_error("Paragraph stress found no deinflection results for language: " + language);
  }

  return run;
}

void expect_paragraph_output(const std::string& language, const ParagraphRun& run,
                             const DeinflectionExpectation& expectation) {
  const Deinflector deinflector(language);
  for (const auto& output : run.outputs) {
    if (paragraph_output_matches(deinflector, output, expectation)) {
      return;
    }
  }

  std::string observed;
  size_t shown = 0;
  for (const auto& output : run.outputs) {
    if (output.source != expectation.source && output.variant != expectation.source) {
      continue;
    }
    if (!observed.empty()) {
      observed += "; ";
    }
    observed += output.source + " [" + output.variant + "] -> " + output.result.text;
    const auto trace = trace_to_string(output.result);
    if (!trace.empty()) {
      observed += " (" + trace + ")";
    }
    if (++shown == 8) {
      break;
    }
  }

  if (observed.empty()) {
    observed = "no paragraph-derived outputs for source";
  }

  throw std::runtime_error("Missing paragraph output: [" + language + "] " + expectation.source + " -> " +
                           expectation.expected + " (observed: " + observed + ")");
}

void test_paragraph_stress() {
  const std::vector<ParagraphCase> cases = {
      {"ar",
       {"\u0642\u0631\u0623\u062a \u0627\u0644\u0637\u0627\u0644\u0628\u0629 \u0643\u062a\u0627\u0628\u0643 \u0641\u064a \u0627\u0644\u0645\u0633\u0627\u0621.",
        "\u0641\u064a \u0627\u0644\u062f\u0631\u0633 \u0646\u062a\u062d\u062f\u062b \u0639\u0646 \u0643\u062a\u0627\u0628\u0646\u0627 \u0648\u0639\u0646 \u0643\u062a\u0627\u0628\u0647.",
        "\u0647\u0630\u0627 \u0645\u062b\u0627\u0644 \u0642\u0635\u064a\u0631 \u0648\u0627\u0644\u0643\u062a\u0627\u0628 \u064a\u0638\u0647\u0631 \u0641\u064a\u0647."},
       {{"\u0643\u062a\u0627\u0628\u0643", "\u0643\u062a\u0627\u0628", "n", {"pos. pron."}},
        {"\u0643\u062a\u0627\u0628\u0646\u0627", "\u0643\u062a\u0627\u0628", "n", {"pos. pron."}},
        {"\u0643\u062a\u0627\u0628\u0647", "\u0643\u062a\u0627\u0628", "n", {"pos. pron."}},
        {"\u0648\u0627\u0644\u0643\u062a\u0627\u0628", "\u0643\u062a\u0627\u0628", "n", {"the"}}}},
      {"de",
       {"Die Gruppe versucht aufzur\u00e4umen, bevor jemand den Plan dargestellt hat.",
        "Am Abend sagt sie: r\u00e4um den Tisch auf, danach testen wir die Anwendbarkeit.",
        "Auch Strasse, Stra\u00dfe und reinigung sollen im selben deutschen Absatz Varianten bilden."},
       {{"aufzur\u00e4umen", "aufr\u00e4umen", "v", {"zu-infinitive"}},
        {"dargestellt", "darstellen", "v", {"past participle"}},
        {"r\u00e4um den Tisch auf", "r\u00e4um auf", "v", {"separated prefix"}},
        {"anwendbarkeit", "anwenden", "v", {"-bar", "-heit"}},
        {"reinigung", "reinigen", "v", {"nominalization"}}}},
      {"el",
       {"\u039f \u03bc\u03b1\u03b8\u03b7\u03c4\u03ae\u03c2 \u03be\u03b1\u03bd\u03b1\u03c1\u03ce\u03c4\u03b7\u03c3\u03b5 \u03bc\u03b5 \u03ae\u03c1\u03b5\u03bc\u03bf \u03c4\u03c1\u03cc\u03c0\u03bf.",
        "\u03a3\u03c4\u03bf \u03ba\u03b5\u03af\u03bc\u03b5\u03bd\u03bf \u03b7 \u03bb\u03ad\u03be\u03b7 \u03be\u03b1\u03bd\u03b1\u03c1\u03ce\u03c4\u03b7\u03c3\u03b5 \u03b5\u03bc\u03c6\u03b1\u03bd\u03af\u03b6\u03b5\u03c4\u03b1\u03b9 \u03be\u03b1\u03bd\u03ac.",
        "\u0397 \u03b4\u03bf\u03ba\u03b9\u03bc\u03ae \u03ba\u03c1\u03b1\u03c4\u03ac \u03c4\u03bf\u03bd \u03c4\u03cc\u03bd\u03bf \u03ba\u03b1\u03b9 \u03c4\u03b7\u03bd \u03b5\u03bd\u03b1\u03bb\u03bb\u03b1\u03ba\u03c4\u03b9\u03ba\u03ae \u03bc\u03bf\u03c1\u03c6\u03ae."},
       {{"\u03be\u03b1\u03bd\u03b1\u03c1\u03ce\u03c4\u03b7\u03c3\u03b5", "\u03c1\u03ce\u03c4\u03b7\u03c3\u03b5", "v",
         {"\u03be\u03b1\u03bd\u03b1-"}},
        {"\u03be\u03b1\u03bd\u03b1\u03c1\u03ce\u03c4\u03b7\u03c3\u03b5", "\u03c1\u03c9\u03c4\u03b7\u03c3\u03b5", "v",
         {"\u03be\u03b1\u03bd\u03b1-"}}}},
      {"en",
       {"The hikers walked up the ridge, then look the word up before lunch.",
        "A teacher was going to walk slowly while cats watched from the wall.",
        "Some quickly written notes compare dirty water, hazy light, walking paths, and cooler air."},
       {{"walked up", "walk up", "v_phr", {"past"}},
        {"walked", "walk", "v", {"past"}},
        {"look the word up", "look up", "v_phr", {"interposed object"}},
        {"going to walk", "walk", "v", {"going-to future"}},
        {"walking", "walk", "v", {"ing"}},
        {"cats", "cat", "ns", {"plural"}},
        {"dirty", "dirt", "ns", {"-y"}},
        {"hazy", "haze", "ns", {"-y"}},
        {"cooler", "cool", "adj", {"comparative"}},
        {"quickly", "quick", "adj", {"adverb"}}}},
      {"eo",
       {"La malbona vetero ne haltigis la amikon dum la voja\u011do.",
        "En dua frazo aperas amiketo apud amikon por provi fina\u0135ojn.",
        "La simpla alineo miksas bona, malbona, kaj amiketo sen vortaro."},
       {{"malbona", "bona", "adj", {"mal-"}},
        {"amikon", "amiko", "n", {"accusative"}},
        {"amiketo", "amiko", "n", {"diminutive"}}}},
      {"es",
       {"Los gatos miran una luz roja y muchas luces mientras pienso en otra prueba.",
        "Cuando juego cerca de la puerta, huele a caf\u00e9 y me despertar parece raro.",
        "Otro p\u00e1rrafo mantiene gatos, luces, pienso, juego y huele juntos."},
       {{"gatos", "gato", "ns", {"plural"}},
        {"luces", "luz", "ns", {"plural"}},
        {"roja", "rojo", "adj", {"feminine adjective"}},
        {"pienso", "pensar", "v_ar", {"present indicative"}},
        {"juego", "jugar", "v_ar", {"present indicative"}},
        {"huele", "oler", "v_er", {"present indicative"}},
        {"me despertar", "despertarse", "v", {"pronominal"}}}},
      {"eu",
       {"Etxean dagoen testuak hitz arruntak eta etxetik forma bera ditu.",
        "Bigarren paragrafoak etxera eta etxeak esaten ditu beste ingurune batean.",
        "Azken lerroak etxean berriro jartzen du estres proba egiteko."},
       {{"etxean", "etxe", "n", {"Inessive Singular"}},
        {"etxetik", "etxe", "n", {"Ablative Singular"}},
        {"etxera", "etxe", "n", {"Allative Singular"}},
        {"etxeak", "etxe", "n", {"Absolutive Plural"}}}},
      {"fr",
       {"Dans l'amour et l\u2019amour, plusieurs lecteurs aiment les phrases simples.",
        "Elles finissait mal ici expr\u00e8s, puis mangeraient ensemble pendant que tu aimais.",
        "Le texte compare l'amour, l\u2019amour, aiment, mangeons, mangeraient, et finissait."},
       {{"aiment", "aimer", "v", {"present indicative"}},
        {"aimais", "aimer", "v", {"imperfect indicative"}},
        {"mangeons", "manger", "v", {"present indicative"}},
        {"mangeraient", "manger", "v", {"conditional"}},
        {"finissait", "finir", "v", {"imperfect indicative"}}}},
      {"ga",
       {"Scr\u00edobh an dalta gclann sa sliocht beag seo.",
        "Feictear gclann agus gcarr ar\u00eds chun an claochl\u00fa a dhearbh\u00fa.",
        "Baineann an tr\u00ed\u00fa habairt le gclann agus le focail eile."},
       {{"gclann", "clann", "n", {"eclipsis"}}, {"gcarr", "carr", "n", {"eclipsis"}}}},
      {"grc",
       {"\u03bb\u03cd\u03b5\u03b9\u03c2 \u03c4\u1f78\u03bd \u03bb\u03cc\u03b3\u03bf\u03bd \u03ba\u03b1\u1f76 \u03bb\u03cd\u03bf\u03bc\u03b5\u03bd \u03c4\u1f74\u03bd \u1f04\u03bb\u03bb\u03b7\u03bd \u03b4\u03bf\u03ba\u03b9\u03bc\u03ae\u03bd.",
        "\u1f10\u03bd \u03c4\u1ff7 \u03ba\u03b5\u03b9\u03bc\u03ad\u03bd\u1ff3 \u03bc\u03ad\u03bd\u03b5\u03b9 \u03bb\u03cd\u03b5\u03b9\u03c2 \u03ba\u03b1\u1f76 \u03bb\u03cd\u03bf\u03bc\u03b5\u03bd.",
        "\u03a4\u03b1\u1fe6\u03c4\u03b1 \u03c4\u1f70 \u03c1\u03ae\u03bc\u03b1\u03c4\u03b1 \u03b2\u03bf\u03b7\u03b8\u03b5\u1fd6 \u03c4\u1f74\u03bd \u03b4\u03bf\u03ba\u03b9\u03bc\u03ae\u03bd."},
       {{"\u03bb\u03cd\u03b5\u03b9\u03c2", "\u03bb\u03cd\u03c9", "v",
         {"2nd person singular present active indicative"}},
        {"\u03bb\u03cd\u03bf\u03bc\u03b5\u03bd", "\u03bb\u03cd\u03c9", "v",
         {"1st person plural present active indicative"}}}},
      {"ja",
       {"\u98df\u3079\u305f\u3002\u305d\u306e\u5f8c\u3067\u8aad\u3093\u3060\u6587\u3092\u78ba\u304b\u3081\u308b\u3002",
        "\u884c\u304d\u307e\u3059\u3002\u5148\u751f\u306f\u98df\u3079\u305f\u4f8b\u3092\u51fa\u3059\u3002",
        "\u3053\u306e\u6bb5\u843d\u306f\u98df\u3079\u305f\u3001\u8aad\u3093\u3060\u3001\u884c\u304d\u307e\u3059\u3092\u6df7\u305c\u308b\u3002"},
       {{"\u98df\u3079\u305f", "\u98df\u3079\u308b", "v1", {"-\u305f"}},
        {"\u8aad\u3093\u3060", "\u8aad\u3080", "v5", {"-\u305f"}},
        {"\u884c\u304d\u307e\u3059", "\u884c\u304f", "v5d", {"-\u307e\u3059"}}}},
      {"kat",
       {"\u10e1\u10d0\u10ee\u10da\u10d4\u10d1\u10d8 \u10d0\u10e5 \u10ec\u10d4\u10e0\u10d8\u10d0 \u10e2\u10d4\u10e1\u10e2\u10d8\u10e1\u10d7\u10d5\u10d8\u10e1.",
        "\u10db\u10d4\u10dd\u10e0\u10d4 \u10ec\u10d8\u10dc\u10d0\u10d3\u10d0\u10d3\u10d4\u10d1\u10d0\u10e8\u10d8 \u10d8\u10e1\u10d4\u10d5 \u10d0\u10e0\u10d8\u10e1 \u10e1\u10d0\u10ee\u10da\u10db\u10d0.",
        "\u10d1\u10dd\u10da\u10dd \u10db\u10dd\u10dc\u10d0\u10d9\u10d5\u10d4\u10d7\u10d8 \u10e1\u10d0\u10ee\u10da\u10e8\u10d8 \u10e4\u10dd\u10e0\u10db\u10d0\u10e1 \u10d0\u10db\u10dd\u10ec\u10db\u10d4\u10d1\u10e1."},
       {{"\u10e1\u10d0\u10ee\u10da\u10d4\u10d1\u10d8", "\u10e1\u10d0\u10ee\u10da", "n",
         {"noun-adj-suffix-stripping"}},
        {"\u10e1\u10d0\u10ee\u10da\u10db\u10d0", "\u10e1\u10d0\u10ee\u10da", "n", {"noun-adj-suffix-stripping"}},
        {"\u10e1\u10d0\u10ee\u10da\u10e8\u10d8", "\u10e1\u10d0\u10ee\u10da", "n", {"noun-adj-suffix-stripping"}}}},
      {"ko",
       {"\uba39\ub2e4\ub294 \ubb38\uc7a5\uacfc \u3131\u314f\u3142 \ud615\ud0dc\ub97c \ud55c \ub2e8\ub77d\uc5d0 \ub123\uc5c8\ub2e4.",
        "\ud14c\uc2a4\ud2b8\ub294 \uba39\ub2e4 \ubcc0\ud658\uacfc \u3131\u314f\u3142 \ud6c4\ucc98\ub9ac\ub97c \ud568\uaed8 \ubcf8\ub2e4.",
        "\ub9c8\uc9c0\ub9c9 \ubb38\uc7a5\uc5d0\uc11c \u3131\u314f\u3142 \uac12\uc744 \ub2e4\uc2dc \ud655\uc778\ud55c\ub2e4."},
       {{"\u3131\u314f\u3142", "\u3131\u314f\u3142\u3137\u314f", "v", {"\uc5b4\uac04"}},
        {"\u3131\u314f\u3142", "\ub2e4", "ida", {"-\ub85c\ub77c"}}}},
      {"la",
       {"Fluvii in tabula sunt, et vocabulo brevi exemplum tenetur.",
        "Alter paragraphus fluvii et vocabulo iterum continet.",
        "Tertia sententia vocabulum, fluvii, vocabulo miscet."},
       {{"fluvii", "fluvius", "n", {"plural"}}, {"vocabulo", "vocabulum", "n", {"ablative"}}}},
      {"sga",
       {"In hoc exemplo cind et cgcg intra eundem locum ponuntur.",
        "Secundus paragraphus cind rursus ponit, deinde cgcg sequitur.",
        "Tertia linea cind cgcgque servat pro antiquis regulis."},
       {{"cind", "cinn", "", {"nd for nn"}}, {"cgcg", "cc", "", {"cg for c"}}}},
      {"sq",
       {"Une fshiva sot dhe pastaj themi fshijm\u00eb n\u00eb fund.",
        "Paragrafi tjet\u00ebr p\u00ebrdor fshiva dhe fshijm\u00eb p\u00ebr rregullat.",
        "Kjo fjali mban fshiva pran\u00eb fshijm\u00eb pa shenja t\u00eb tjera."},
       {{"fshiva", "fshij", "v", {"aorist first-person singular indicative"}},
        {"fshijm\u00eb", "fshij", "v", {"present indicative first-person plural"}}}},
      {"tl",
       {"May bahayan sa kuwento, at magbabasa ang bata pagkatapos.",
        "Sinubukan din ang puktan at kapuktan sa loob ng maikling talata.",
        "Ulitin natin: bahayan, magbabasa, puktan, kapuktan para sa diin."},
       {{"bahayan", "bahay", "n", {"-an"}},
        {"magbabasa", "basa", "n", {"mag- + rep1"}},
        {"puktan", "pokt", "n", {"-an"}},
        {"kapuktan", "pokt", "n", {"ka-...-an"}}}},
      {"yi",
       {"\u05d4\u05d5\u05e0\u05d8\u05e1 \u05e9\u05d8\u05d9\u05d9\u05e2\u05df \u05d0\u05d9\u05df \u05d6\u05d0\u05e5, \u05d0\u05d5\u05df \u05e2\u05e2\u05e8 \u05e4\u05e8\u05d5\u05d5\u05d5\u05d8 \u05d3\u05d9 \u05e8\u05e2\u05d2\u05dc.",
        "\u05d0\u05b7 \u05e6\u05d5\u05d5\u05d9\u05d9\u05d8\u05e2\u05e8 \u05e4\u05d0\u05b7\u05e8\u05d0\u05b7\u05d2\u05e8\u05d0\u05b7\u05e3 \u05d4\u05d0\u05b7\u05dc\u05d8 \u05d4\u05d5\u05e0\u05d8\u05e1 \u05d0\u05d5\u05df \u05e2\u05e2\u05e8.",
        "\u05d3\u05d9 \u05dc\u05e2\u05e6\u05d8\u05e2 \u05e9\u05d5\u05e8\u05d4 \u05d7\u05d6\u05e8\u05d8 \u05d4\u05d5\u05e0\u05d8\u05e1 \u05de\u05d9\u05d8 \u05e2\u05e2\u05e8."},
       {{"\u05d4\u05d5\u05e0\u05d8\u05e1", "\u05d4\u05d5\u05e0\u05d8", "ns", {"plural"}},
        {"\u05e2\u05e2\u05e8", "\ufb2e", "ns", {"umlaut_plural"}}}},
  };

  size_t paragraph_count = 0;
  size_t candidate_count = 0;
  size_t result_count = 0;
  size_t expected_output_count = 0;
  for (const auto& test_case : cases) {
    if (test_case.expectations.size() < 2) {
      throw std::runtime_error("Paragraph output coverage is too low for language: " + test_case.language);
    }

    paragraph_count += test_case.paragraphs.size();
    const auto run = run_paragraph_pipeline(test_case.language, test_case.paragraphs, 4);
    candidate_count += run.candidate_count;
    result_count += run.result_count;
    size_t language_expected_output_count = 0;
    for (const auto& expectation : test_case.expectations) {
      expect_paragraph_output(test_case.language, run, expectation);
      ++expected_output_count;
      ++language_expected_output_count;
    }
    std::cout << "  " << test_case.language << ": verified " << language_expected_output_count
              << " expected paragraph outputs from " << test_case.paragraphs.size() << " paragraphs and "
              << run.candidate_count << " candidates\n";
  }

  if (paragraph_count < Deinflector::supported_languages().size() * 3) {
    throw std::runtime_error("Paragraph stress did not cover enough paragraphs");
  }
  if (expected_output_count < 60) {
    throw std::runtime_error("Paragraph output coverage did not verify enough expected outputs");
  }

  std::cout << "paragraph output verified " << expected_output_count << " expected outputs from " << paragraph_count
            << " paragraphs, " << candidate_count << " candidates, and " << result_count << " deinflection results\n";
}
}  // namespace

int main() {
  try {
    test_supported_languages();
    test_french();
    test_german();
    test_english();
    test_other_generated_languages();
    test_paragraph_stress();
    std::cout << "language tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << "\n";
    return 1;
  }
}
