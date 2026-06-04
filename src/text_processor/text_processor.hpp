#pragma once

#include <string>
#include <vector>

struct TextVariant {
  std::string text;
  int steps;
};

namespace text_processor {
std::vector<TextVariant> process(const std::string& src);
std::vector<TextVariant> process(const std::string& src, const std::string& language);
std::vector<TextVariant> postprocess(const std::string& src, const std::string& language);
}
