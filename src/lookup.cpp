#include "hoshidicts/lookup.hpp"

#include <utf8.h>
#include <utf8proc.h>
#include <glaze/glaze.hpp>

#include <algorithm>
#include <functional>
#include <map>
#include <ranges>
#include <sstream>
#include <tuple>
#include <unordered_set>
#include <utility>

#include "language/language_data.hpp"
#include "text_processor/text_processor.hpp"

namespace {
constexpr size_t GERMAN_SEPARATED_PREFIX_SCAN_LENGTH = 96;
constexpr size_t GERMAN_SEPARATED_PREFIX_MAX_WORDS = 12;
constexpr int MAX_REDIRECT_DEPTH = 4;

struct SearchCandidate {
  std::string matched;
  std::string text;
  int extra_steps = 0;
  int separated_prefix_index = -1;
};

struct RankedLookupResult {
  LookupResult result;
  int separated_prefix_index = -1;
};

struct WordSpan {
  std::string text;
  size_t start = 0;
  size_t end = 0;
};

using RedirectGlossary = std::vector<std::tuple<std::string, std::vector<std::string>>>;

std::string_view trim_left(std::string_view text) {
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' ||
                           text.front() == '\n')) {
    text.remove_prefix(1);
  }
  return text;
}

bool parse_redirect_glossary(std::string_view glossary, std::vector<std::string>* targets) {
  glossary = trim_left(glossary);
  if (!glossary.starts_with("[[")) {
    return false;
  }

  RedirectGlossary parsed;
  const auto error =
      glz::read<glz::opts{.error_on_unknown_keys = false, .error_on_missing_keys = false}>(parsed, glossary);
  if (error || parsed.empty()) {
    return false;
  }

  if (targets != nullptr) {
    for (const auto& [target, _] : parsed) {
      if (!target.empty()) {
        targets->push_back(target);
      }
    }
  }
  return true;
}

bool is_redirect_glossary(std::string_view glossary) {
  return parse_redirect_glossary(glossary, nullptr);
}

std::vector<std::string> redirect_targets(const TermResult& term) {
  std::vector<std::string> result;
  std::unordered_set<std::string> seen;
  for (const auto& glossary : term.glossaries) {
    std::vector<std::string> targets;
    if (!parse_redirect_glossary(glossary.glossary, &targets)) {
      continue;
    }

    for (auto& target : targets) {
      if (seen.insert(target).second) {
        result.push_back(std::move(target));
      }
    }
  }
  return result;
}

void remove_redirect_glossaries_if_mixed(TermResult& term) {
  bool has_redirect = false;
  bool has_non_redirect = false;
  for (const auto& glossary : term.glossaries) {
    if (is_redirect_glossary(glossary.glossary)) {
      has_redirect = true;
    } else {
      has_non_redirect = true;
    }
  }

  if (!has_redirect || !has_non_redirect) {
    return;
  }

  std::erase_if(term.glossaries, [](const GlossaryEntry& glossary) {
    return is_redirect_glossary(glossary.glossary);
  });
}

std::vector<std::string> split_whitespace(const std::string& str) {
  std::vector<std::string> result;
  std::istringstream iss(str);
  std::string token;
  while (iss >> token) {
    result.push_back(std::move(token));
  }
  return result;
}

std::vector<int> get_freq_values_for_dict(const TermResult& term, const std::string& dict_name) {
  for (const auto& frequency_entry : term.frequencies) {
    if (frequency_entry.dict_name != dict_name) {
      continue;
    }

    std::vector<int> values;
    for (const auto& frequency : frequency_entry.frequencies) {
      if (frequency.value >= 0) {
        values.push_back(frequency.value);
      }
    }
    std::ranges::sort(values);
    return values;
  }

  return {INT_MAX};
}

bool is_word_codepoint(char32_t c) {
  switch (utf8proc_category(static_cast<utf8proc_int32_t>(c))) {
    case UTF8PROC_CATEGORY_LU:
    case UTF8PROC_CATEGORY_LL:
    case UTF8PROC_CATEGORY_LT:
    case UTF8PROC_CATEGORY_LM:
    case UTF8PROC_CATEGORY_LO:
    case UTF8PROC_CATEGORY_MN:
    case UTF8PROC_CATEGORY_MC:
    case UTF8PROC_CATEGORY_ME:
    case UTF8PROC_CATEGORY_ND:
    case UTF8PROC_CATEGORY_NL:
    case UTF8PROC_CATEGORY_NO:
      return true;
    default:
      return c == U'_';
  }
}

std::vector<WordSpan> extract_word_spans(std::string_view text, size_t max_codepoints, size_t max_words) {
  std::vector<WordSpan> result;
  auto begin = text.begin();
  auto it = text.begin();
  bool in_word = false;
  size_t word_start = 0;
  size_t word_end = 0;
  size_t codepoints = 0;

  while (it != text.end() && codepoints < max_codepoints) {
    const auto before = it;
    const char32_t c = utf8::next(it, text.end());
    ++codepoints;

    const size_t before_index = static_cast<size_t>(before - begin);
    const size_t after_index = static_cast<size_t>(it - begin);
    if (is_word_codepoint(c)) {
      if (!in_word) {
        in_word = true;
        word_start = before_index;
      }
      word_end = after_index;
      continue;
    }

    if (in_word) {
      result.push_back({std::string(text.substr(word_start, word_end - word_start)), word_start, word_end});
      if (result.size() >= max_words) {
        return result;
      }
      in_word = false;
    }
  }

  if (in_word && result.size() < max_words) {
    result.push_back({std::string(text.substr(word_start, word_end - word_start)), word_start, word_end});
  }
  return result;
}

std::span<const language_data::Rule> german_rules() {
  const auto* language = language_data::find_language("de");
  return language == nullptr ? std::span<const language_data::Rule>{} : language->rules;
}

std::optional<std::string_view> german_separable_prefix_for(std::string_view word) {
  const auto variants = text_processor::process(std::string(word), "de");
  for (const auto& variant : variants) {
    for (const auto& rule : german_rules()) {
      if (rule.kind == language_data::RuleKind::GermanSeparatedPrefix && rule.a == variant.text) {
        return rule.a;
      }
    }
  }
  return std::nullopt;
}

std::vector<SearchCandidate> make_contiguous_candidates(const std::string& lookup_string, size_t scan_length) {
  std::vector<SearchCandidate> result;
  size_t text_len = utf8::distance(lookup_string.begin(), lookup_string.end());
  size_t start = std::min(scan_length, text_len);
  auto search_str_it = lookup_string.begin();
  utf8::advance(search_str_it, start, lookup_string.end());

  for (size_t i = std::min(scan_length, text_len); i > 0; i--) {
    std::string search_str(lookup_string.begin(), search_str_it);
    result.push_back({.matched = search_str, .text = search_str});
    if (i > 1) {
      utf8::prior(search_str_it, lookup_string.begin());
    }
  }
  return result;
}

std::vector<SearchCandidate> make_german_separated_prefix_candidates(const std::string& lookup_string,
                                                                     size_t scan_length) {
  const size_t max_scan = std::max(scan_length, GERMAN_SEPARATED_PREFIX_SCAN_LENGTH);
  const auto words = extract_word_spans(lookup_string, max_scan, GERMAN_SEPARATED_PREFIX_MAX_WORDS);
  if (words.size() < 2 || words.front().start != 0) {
    return {};
  }

  std::vector<SearchCandidate> result;
  const auto first_prefix = german_separable_prefix_for(words.front().text);
  if (first_prefix.has_value()) {
    for (size_t i = 1; i < words.size(); ++i) {
      result.push_back({
          .matched = std::string(lookup_string.substr(0, words[i].end)),
          .text = words[i].text + " " + std::string(*first_prefix),
          .extra_steps = 1,
          .separated_prefix_index = static_cast<int>(i),
      });
    }
    return result;
  }

  for (size_t i = 1; i < words.size(); ++i) {
    const auto prefix = german_separable_prefix_for(words[i].text);
    if (!prefix.has_value()) {
      continue;
    }
    result.push_back({
        .matched = std::string(lookup_string.substr(0, words[i].end)),
        .text = words.front().text + " " + std::string(*prefix),
        .extra_steps = 1,
        .separated_prefix_index = static_cast<int>(i),
    });
  }
  return result;
}

std::vector<SearchCandidate> make_search_candidates(const std::string& lookup_string, const std::string& language,
                                                    size_t scan_length) {
  auto result = language == "de" ? make_german_separated_prefix_candidates(lookup_string, scan_length)
                                 : std::vector<SearchCandidate>{};
  auto contiguous = make_contiguous_candidates(lookup_string, scan_length);
  result.insert(result.end(), std::make_move_iterator(contiguous.begin()), std::make_move_iterator(contiguous.end()));
  return result;
}
}

std::vector<LookupResult> Lookup::lookup(const std::string& lookup_string, int max_results, size_t scan_length) const {
  std::map<std::pair<std::string, std::string>, RankedLookupResult> result_map;
  std::function<std::vector<TermResult>(std::vector<TermResult>, const DeinflectionResult&,
                                        std::unordered_set<std::string>&, int)>
      resolve_redirects;
  resolve_redirects = [&](std::vector<TermResult> terms, const DeinflectionResult& deinflection,
                          std::unordered_set<std::string>& seen, int depth) {
    std::vector<TermResult> result;

    for (auto& term : terms) {
      seen.insert(term.expression);
      if (!term.reading.empty()) {
        seen.insert(term.reading);
      }

      query_.materialize(term);
      remove_redirect_glossaries_if_mixed(term);

      const auto targets = redirect_targets(term);
      if (targets.empty() || depth >= MAX_REDIRECT_DEPTH) {
        result.push_back(std::move(term));
        continue;
      }

      std::vector<TermResult> resolved_terms;
      bool attempted_target = false;
      for (const auto& target : targets) {
        if (target.empty() || target == term.expression || target == term.reading || seen.contains(target)) {
          continue;
        }

        attempted_target = true;
        seen.insert(target);
        auto target_terms = query_.query_raw(target);
        filter_by_pos(target_terms, deinflection, deinflector_);
        auto nested_terms = resolve_redirects(std::move(target_terms), deinflection, seen, depth + 1);
        resolved_terms.insert(resolved_terms.end(), std::make_move_iterator(nested_terms.begin()),
                              std::make_move_iterator(nested_terms.end()));
      }

      if (resolved_terms.empty()) {
        if (attempted_target || depth >= MAX_REDIRECT_DEPTH) {
          result.push_back(std::move(term));
        }
      } else {
        result.insert(result.end(), std::make_move_iterator(resolved_terms.begin()),
                      std::make_move_iterator(resolved_terms.end()));
      }
    }

    return result;
  };

  for (const auto& candidate : make_search_candidates(lookup_string, deinflector_.language(), scan_length)) {
    auto processor_results = text_processor::process(candidate.text, deinflector_.language());
    for (auto& variant : processor_results) {
      auto deinflection_results = deinflector_.deinflect(variant.text);
      for (auto& deinflection : deinflection_results) {
        auto postprocessor_results = text_processor::postprocess(deinflection.text, deinflector_.language());
        for (const auto& postprocessed : postprocessor_results) {
          auto terms = query_.query_raw(postprocessed.text);
          filter_by_pos(terms, deinflection, deinflector_);
          std::unordered_set<std::string> seen_redirects;
          seen_redirects.insert(postprocessed.text);
          terms = resolve_redirects(std::move(terms), deinflection, seen_redirects, 0);

          for (auto& term : terms) {
            // deduplicate glossaries
            auto key = std::make_pair(term.expression, term.reading);
            auto it = result_map.find(key);
            if (it != result_map.end()) {
              // we only need the longest matched form
              if (utf8::distance(candidate.matched.begin(), candidate.matched.end()) >
                  utf8::distance(it->second.result.matched.begin(), it->second.result.matched.end())) {
                it->second = RankedLookupResult{
                    .result = LookupResult{.matched = candidate.matched,
                                           .deinflected = postprocessed.text,
                                           .trace = deinflection.trace,
                                           .term = std::move(term),
                                           .preprocessor_steps = candidate.extra_steps + variant.steps +
                                                                 postprocessed.steps},
                    .separated_prefix_index = candidate.separated_prefix_index,
                };
              }
            } else {
              result_map.emplace(key,
                                 RankedLookupResult{
                                     .result = LookupResult{.matched = candidate.matched,
                                                            .deinflected = postprocessed.text,
                                                            .trace = deinflection.trace,
                                                            .term = std::move(term),
                                                            .preprocessor_steps = candidate.extra_steps +
                                                                                  variant.steps + postprocessed.steps},
                                     .separated_prefix_index = candidate.separated_prefix_index,
                                 });
            }
          }
        }
      }
    }
  }

  std::vector<RankedLookupResult> ranked_results;
  ranked_results.reserve(result_map.size());
  for (auto& [_, result] : result_map) {
    ranked_results.push_back(std::move(result));
  }
  const auto freq_dict_order = query_.get_freq_dict_order();
  auto middle_iter = std::ranges::next(ranked_results.begin(), max_results, ranked_results.end());
  std::ranges::partial_sort(ranked_results, middle_iter, [&freq_dict_order](const auto& ranked_a,
                                                                            const auto& ranked_b) {
    const auto& a = ranked_a.result;
    const auto& b = ranked_b.result;
    const bool a_separated = ranked_a.separated_prefix_index >= 0;
    const bool b_separated = ranked_b.separated_prefix_index >= 0;
    if (a_separated && b_separated && ranked_a.separated_prefix_index != ranked_b.separated_prefix_index) {
      return ranked_a.separated_prefix_index < ranked_b.separated_prefix_index;
    }

    auto len_a = utf8::distance(a.matched.begin(), a.matched.end());
    auto len_b = utf8::distance(b.matched.begin(), b.matched.end());
    if (len_a != len_b) {
      return len_a > len_b;
    }

    auto steps_a = a.preprocessor_steps;
    auto steps_b = b.preprocessor_steps;
    if (steps_a != steps_b) {
      return steps_a < steps_b;
    }

    auto trace_len_a = a.trace.size();
    auto trace_len_b = b.trace.size();
    if (trace_len_a != trace_len_b) {
      return trace_len_a < trace_len_b;
    }

    auto match_a = a.term.expression == a.deinflected;
    auto match_b = b.term.expression == b.deinflected;
    if (match_a != match_b) {
      return match_a > match_b;
    }

    for (const auto& dict_name : freq_dict_order) {
      const auto freq_a = get_freq_values_for_dict(a.term, dict_name);
      const auto freq_b = get_freq_values_for_dict(b.term, dict_name);
      if (freq_a != freq_b) {
        return freq_a < freq_b;
      }
    }

    auto a_reading_expr_match = a.term.expression == a.term.reading;
    auto b_reading_expr_match = b.term.expression == b.term.reading;
    return a_reading_expr_match > b_reading_expr_match;
  });

  if (ranked_results.size() > static_cast<size_t>(max_results)) {
    ranked_results.resize(max_results);
  }

  std::vector<LookupResult> results;
  results.reserve(ranked_results.size());
  for (auto& r : ranked_results) {
    query_.materialize(r.result.term);
    results.push_back(std::move(r.result));
  }

  return results;
}

void Lookup::filter_by_pos(std::vector<TermResult>& terms, const DeinflectionResult& d,
                           const Deinflector& deinflector) {
  if (d.conditions == 0) {
    return;
  }
  std::erase_if(terms, [&](const TermResult& term) {
    auto dict_conditions = deinflector.get_condition_flags_from_parts_of_speech(split_whitespace(term.rules));
    return (dict_conditions & d.conditions) == 0;
  });
}
