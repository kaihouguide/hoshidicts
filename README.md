# hoshidicts

This library implements a dictionary backend that works similarly to [Yomitan](https://github.com/yomidevs/yomitan). It was made for [Hoshi Reader](https://github.com/Manhhao/Hoshi-Reader), and now includes broad multilingual deinflection support ported from Yomitan's descriptor/table-driven language transform data.

hoshidicts can import Yomitan dictionaries and run language-aware preprocessing, deinflection, postprocessing, and lookup for multiple languages. This includes Japanese and Korean text processing, French apostrophe normalization, German `ss`/`ß` variants and separable verbs, English phrasal verbs, Spanish stem/pronominal rules, Tagalog callback-style transforms, and other language-specific rules.

## Supported languages

The generated language data currently supports:

| Code | Language |
| ---- | -------- |
| `ar` | Arabic |
| `de` | German |
| `el` | Modern Greek |
| `en` | English |
| `eo` | Esperanto |
| `es` | Spanish |
| `eu` | Basque |
| `fr` | French |
| `ga` | Irish |
| `grc` | Ancient Greek |
| `ja` | Japanese |
| `kat` | Georgian |
| `ko` | Korean |
| `la` | Latin |
| `sga` | Old Irish |
| `sq` | Albanian |
| `tl` | Tagalog |
| `yi` | Yiddish |

Aliases are also recognized for `ka` -> `kat` and `arz` -> `ar`.

Multilingual behavior is covered by the language test suite, including paragraph-derived output checks across all supported languages.

A MIT version of the library is available on the [main-mit](https://github.com/Manhhao/hoshidicts/tree/main-mit) branch.

## Reference

### importer
```cpp
ImportResult dictionary_importer::import(const std::string& zip_path, const std::string& output_dir, bool low_ram = false)
```
Imports a Yomitan `.zip` dictionary file into a custom format. The resulting folder is stored in `output_dir/<dict_title>`. Glossaries are compressed using zstd. Term, frequency and pitch dictionaries are generally supported, but only a small part of the pitch accent spec was implemented. Setting `low_ram` to `true` can reduce memory usage significantly at the cost of slightly lower import speed.

### query
```cpp
void DictionaryQuery::add_term_dict(const std::string& path)
```
Adds an imported term dictionary to the query.

```cpp
void DictionaryQuery::add_freq_dict(const std::string& path)
```
Adds an imported frequency dictionary to the query.

```cpp
void DictionaryQuery::add_pitch_dict(const std::string& path)
```
Adds an imported pitch dictionary to the query.

```cpp
std::vector<TermResult> DictionaryQuery::query(const std::string& expression) const
```
Queries all added dictionaries for the given expression. TermResult includes glossary, frequency and pitch data in the order dictionaries were added. Glossaries are decompressed.

```cpp
std::vector<DictionaryStyle> DictionaryQuery::get_styles() const
```
Returns CSS styles for all dictionaries, if present.

```cpp
std::vector<char> DictionaryQuery::get_media_file(const std::string& dict_name, const std::string& media_path) const
```
Returns raw bytes for file originally stored at `media_path` in term dictionary `dict_name` or an empty vector if the file does not exist.

### deinflector
```cpp
Deinflector::Deinflector(std::string language = "ja")
```
Creates a deinflector for a supported language code. The default remains Japanese for compatibility.

```cpp
std::vector<DeinflectionResult> Deinflector::deinflect(const std::string& text) const
```
Deinflects a given string using the selected language's generated Yomitan-compatible transform rules. As this doesn't use any dictionary data, the result may include invalid deinflections.

```cpp
void Deinflector::set_language(std::string language)
```
Changes the active deinflection language.

```cpp
std::string_view Deinflector::language() const
```
Returns the active language code.

```cpp
static std::vector<std::string> Deinflector::supported_languages()
```
Returns the list of supported language codes.

```cpp
static uint32_t Deinflector::pos_to_conditions(const std::vector<std::string>& part_of_speech)
```
Converts a vector of Japanese part-of-speech tags into a bitmask used for deinflection filtering.

```cpp
uint32_t Deinflector::get_condition_flags_from_parts_of_speech(const std::vector<std::string>& part_of_speech) const
```
Converts dictionary part-of-speech tags into language-specific condition flags for lookup filtering.

### lookup
```cpp
Lookup::Lookup(DictionaryQuery& query, Deinflector& deinflector)
```
Creates a Lookup object using a given query with dictionaries added and a deinflector.

```cpp
std::vector<LookupResult> Lookup::lookup(const std::string& lookup_string, int max_results = 16, size_t scan_length = 16) const
```
Follows a parsing strategy similar to Yomitan. Substrings of `lookup_string` are tested from length `scan_length` down to 1. Each substring is preprocessed for the active deinflector language, deinflected, postprocessed if needed, then queried using the query object.

Results are filtered by part-of-speech tags defined in dictionaries, or added directly if none are present. The results are sorted by matched length first, then by preprocessing steps, then deinflection trace length and finally by frequency.

## Acknowledgements

- [Yomitan](https://github.com/yomidevs/yomitan): Dictionary format, language transform descriptors, deinflection rules and descriptions, Japanese/Korean text processing references | GPLv3
- [glaze](https://github.com/stephenberry/glaze): MIT
- [libdeflate](https://github.com/ebiggers/libdeflate.git): MIT
- [xxHash](https://github.com/Cyan4973/xxHash): BSD-2-Clause
- [zstd](https://github.com/facebook/zstd): BSD
- [utfcpp](https://github.com/nemtrif/utfcpp): BSL-1.0
- [unordered_dense](https://github.com/martinus/unordered_dense.git): MIT

## License
hoshidicts (main) is licensed under the GNU General Public License v3.0. See [LICENSE](LICENSE) for details.
