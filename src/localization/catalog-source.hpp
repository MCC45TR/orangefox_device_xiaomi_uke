// Host-only source collection; this is not a translation approval.
#pragma once
#include <json/json.h>
#include <filesystem>
#include <set>
#include <string>

Json::Value collect_catalog(const std::filesystem::path& component);
std::multiset<std::string> placeholders(const std::string& text);
std::string digest(const std::string& bytes);
std::string json(const Json::Value& value);
