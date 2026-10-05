// Host-only structural key generation. This does not approve translation meaning.
#pragma once
#include <json/json.h>
#include <filesystem>
#include <map>
#include <string>

namespace ure_locale_host {
using KeyTable = std::map<std::string, std::string>;
KeyTable validate_keys(const Json::Value& catalog);
std::string key_header(const KeyTable& keys);
void generate_keys(const std::filesystem::path& input, const std::filesystem::path& output);
}
