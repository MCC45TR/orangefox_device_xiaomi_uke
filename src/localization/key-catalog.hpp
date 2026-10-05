// Host-only structural key generation. This does not approve translation meaning.
#pragma once
#include <json/json.h>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace ure_locale_host {
using KeyTable = std::map<std::string, std::string>;
KeyTable validate_keys(const Json::Value& catalog);
std::string read_regular(const std::filesystem::path& input);
Json::Value parse_strict(std::string_view bytes);
bool valid_utf8(std::string_view text);
void publish_regular(const std::filesystem::path& output, const std::string& bytes);
std::string key_header(const KeyTable& keys);
void generate_keys(const std::filesystem::path& input, const std::filesystem::path& output);
}
