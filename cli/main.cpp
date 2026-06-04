#include <utf8.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

#include "../src/text_processor/text_processor.hpp"
#include "hoshidicts/deinflector.hpp"
#include "hoshidicts/importer.hpp"
#include "hoshidicts/lookup.hpp"
#include "hoshidicts/query.hpp"

std::vector<std::string> get_utf8_args(int argc, char* argv[]) {
#ifdef _WIN32
  int wide_argc = 0;
  LPWSTR* wide_argv = CommandLineToArgvW(GetCommandLineW(), &wide_argc);
  if (wide_argv == nullptr) {
    return {};
  }

  std::vector<std::string> args;
  args.reserve(wide_argc);
  for (int i = 0; i < wide_argc; ++i) {
    int size = WideCharToMultiByte(CP_UTF8, 0, wide_argv[i], -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
      args.emplace_back();
      continue;
    }
    std::string arg(size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide_argv[i], -1, arg.data(), size, nullptr, nullptr);
    args.push_back(std::move(arg));
  }
  LocalFree(wide_argv);
  return args;
#else
  std::vector<std::string> args;
  args.reserve(argc);
  for (int i = 0; i < argc; ++i) {
    args.emplace_back(argv[i]);
  }
  return args;
#endif
}

void print_usage(const char* program) {
  std::cout << std::format("Usage:\n");
  std::cout << std::format("{} import <path/to/dictionary.zip>\n", program);
  std::cout << std::format("{} deinflect <word>\n", program);
  std::cout << std::format("{} deinflect <language> <word>\n", program);
  std::cout << std::format("{} preprocess <word>\n", program);
  std::cout << std::format("{} preprocess <language> <word>\n", program);
  std::cout << std::format("{} query <path/to/dictionary> <word>\n", program);
  std::cout << std::format("{} lookup <path/to/dictionary> <lookup_string>\n", program);
  std::cout << std::format("{} lookup <language> <path/to/dictionary> <lookup_string>\n", program);
  std::cout << std::format("{} freq <path/to/dictionary> <word>\n", program);
}

bool is_supported_language(std::string_view language) {
  return Deinflector(std::string(language)).language() == language;
}

void cmd_import(const std::string& path) {
  std::filesystem::path zip_path(path);
  std::string output_dir = zip_path.parent_path().string();
  if (output_dir.empty()) {
    output_dir = ".";
  }
  ImportResult result = dictionary_importer::import(path, output_dir);

  if (result.success) {
    std::cout << std::format("title: {}\n", result.title);
    std::cout << std::format("term_count: {}\n", result.term_count);
    std::cout << std::format("meta_count: {}\n", result.meta_count);
    std::cout << std::format("freq_count: {}\n", result.freq_count);
    std::cout << std::format("pitch_count: {}\n", result.pitch_count);
    std::cout << std::format("media_count: {}\n", result.media_count);
  } else {
    std::cout << std::format("could not import dictionary:\n");
    for (const auto& error : result.errors) {
      std::cout << std::format(" {}\n", error);
    }
  }
}

void cmd_deinflect(const std::string& language, const std::string& inflected) {
  Deinflector deinflector(language);
  auto results = deinflector.deinflect(inflected);

  std::cout << std::format("deinflections for: {} length: {}\n", inflected,
                           utf8::distance(inflected.begin(), inflected.end()));
  std::cout << std::format("found {} candidates\n\n", results.size());

  for (const auto& r : results) {
    std::cout << std::format("{} (conditions: {})", r.text, r.conditions);
    if (!r.trace.empty()) {
      std::cout << std::format("  ");
      for (size_t i = 0; i < r.trace.size(); ++i) {
        std::cout << std::format("{}{}", r.trace[i].name, i < r.trace.size() - 1 ? " -> " : "");
      }
      std::cout << std::format("\n");
    } else {
      std::cout << std::format("\n");
    }
  }
}

void cmd_preprocess(const std::string& language, const std::string& text) {
  auto results = text_processor::process(text, language);

  std::cout << std::format("preprocessing for: {} length: {}\n", text, utf8::distance(text.begin(), text.end()));
  std::cout << std::format("found {} variants\n", results.size());

  for (const auto& r : results) {
    std::cout << std::format("{}\n", r.text);
  }
}

void cmd_query(const std::string& db_path, const std::string& expression) {
  DictionaryQuery dict_query;
  dict_query.add_term_dict(db_path);
  auto result = dict_query.query(expression);

  std::cout << std::format("query results for: {} length: {}\n", expression,
                           utf8::distance(expression.begin(), expression.end()));
  std::cout << std::format("{} entries\n", result.size());
  for (const auto& r : result) {
    std::cout << std::format("---------------------------------------------------------------\n");
    std::cout << std::format("{} {} {}\n", r.expression, r.reading, r.rules);
    std::cout << std::format("{} glossary entries\n", r.glossaries.size());
    for (const auto& g : r.glossaries) {
      std::cout << std::format("------\n");
      std::cout << std::format("{}\n", g.dict_name);
      std::cout << std::format("{}\n", g.glossary);
    }
  }
}

void cmd_freq(const std::string& path, const std::string& expression, const std::string& reading) {
  std::vector<TermResult> terms;
  terms.emplace_back(TermResult{.expression = expression, .reading = reading});

  DictionaryQuery query;
  query.add_freq_dict(path);
  query.query_freq(terms);
  std::cout << std::format("frequency entries for: {}\n", expression);
  int count = 0;
  for (auto& freq : terms[0].frequencies) {
    std::cout << std::format("dict: {}\n", freq.dict_name);
    for (auto& freq_entry : freq.frequencies) {
      std::cout << std::format("val: {} display_val: {}\n", freq_entry.value, freq_entry.display_value);
      count++;
    }
  }
  std::cout << std::format("count: {}\n", count);
}

void cmd_lookup(const std::string& language, const std::vector<std::string>& db_paths, const std::string& lookup_string,
                int max_results = 8, int scan_length = 16) {
  DictionaryQuery dict_query;
  for (const auto& path : db_paths) {
    dict_query.add_term_dict(path);
  }
  Deinflector deinflect(language);
  Lookup lookup(dict_query, deinflect);
  auto result = lookup.lookup(lookup_string, max_results, scan_length);

  std::cout << std::format("lookup results for: {} language: {} max_results: {} scan_length: {}\n", lookup_string,
                           deinflect.language(), max_results, scan_length);
  std::cout << std::format("{} results\n", result.size());

  for (const auto& r : result) {
    std::cout << std::format("---------------------------------------------------------------\n");
    std::cout << std::format("{}\n", r.matched);
    if (!r.trace.empty()) {
      std::cout << std::format("  ");
      for (size_t i = 0; i < r.trace.size(); ++i) {
        std::cout << std::format("{}{}", r.trace[i].name, i < r.trace.size() - 1 ? " -> " : "");
      }
      std::cout << std::format("\n");
    }
    std::cout << std::format("{} {}\n", r.term.expression, r.term.reading);
    for (const auto& g : r.term.glossaries) {
      std::cout << std::format("------\n");
      std::cout << std::format("{}\n", g.dict_name);
      std::cout << std::format("{}\n", g.glossary);
    }
  }

  std::cout << std::format("styles: \n");
  for (const auto& s : dict_query.get_styles()) {
    std::cout << std::format("{}\n", s.dict_name);
    std::cout << std::format("{}\n", s.styles);
  }
}

int main(int argc, char* argv[]) {
  const auto args = get_utf8_args(argc, argv);
  if (args.size() < 2) {
    print_usage(args.empty() ? "hoshidicts-cli" : args[0].c_str());
    return 1;
  }

  const auto begin = std::chrono::steady_clock::now();
  std::string_view command = args[1];

  if (command == "import" && args.size() >= 3) {
    cmd_import(args[2]);
  } else if (command == "deinflect" && args.size() >= 4) {
    cmd_deinflect(args[2], args[3]);
  } else if (command == "deinflect" && args.size() >= 3) {
    cmd_deinflect("ja", args[2]);
  } else if (command == "preprocess" && args.size() >= 4) {
    cmd_preprocess(args[2], args[3]);
  } else if (command == "preprocess" && args.size() >= 3) {
    cmd_preprocess("ja", args[2]);
  } else if (command == "query" && args.size() >= 4) {
    cmd_query(args[2], args[3]);
  } else if (command == "lookup" && args.size() >= 4) {
    std::string language = "ja";
    size_t first_dict_arg = 2;
    if (args.size() >= 5 && is_supported_language(args[2])) {
      language = args[2];
      first_dict_arg = 3;
    }

    std::vector<std::string> db_paths;
    db_paths.reserve(args.size() - first_dict_arg - 1);
    for (size_t i = first_dict_arg; i < args.size() - 1; ++i) {
      db_paths.emplace_back(args[i]);
    }
    std::string term = args.back();
    cmd_lookup(language, db_paths, term);
  } else if (command == "freq" && args.size() >= 5) {
    cmd_freq(args[2], args[3], args[4]);
  } else {
    print_usage(args[0].c_str());
    return 1;
  }

  const auto end = std::chrono::steady_clock::now();
  std::chrono::duration<double, std::milli> duration = end - begin;
  std::cout << std::format("runtime: {}ms\n", duration.count());

  return 0;
}
