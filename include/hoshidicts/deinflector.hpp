#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <cstddef>

struct TransformGroup {
  std::string name;
  std::string description;
};

struct DeinflectionResult {
  std::string text;
  uint32_t conditions;
  std::vector<TransformGroup> trace;
};

class Deinflector {
 public:
  explicit Deinflector(std::string language = "ja");
  std::vector<DeinflectionResult> deinflect(const std::string& text) const;
  void set_language(const std::string& language);
  const std::string& language() const;
  uint32_t get_condition_flags_from_parts_of_speech(const std::vector<std::string>& part_of_speech) const;
  uint32_t get_condition_flags_from_condition_type(const std::string& condition_type) const;
  static std::vector<std::string> supported_languages();
  static uint32_t pos_to_conditions(const std::vector<std::string>& part_of_speech);

 private:
  void deinflect_recursive(const std::string& text, uint32_t conditions, std::vector<TransformGroup>& trace,
                           std::vector<DeinflectionResult>& results) const;

  std::string language_;
};
