// Actual host generator ownership, schema and publication controls.
#include "key-catalog.hpp"
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;
using ure_locale_host::validate_keys;
namespace {
unsigned refusals = 0;
void require(bool condition) { if (!condition) throw std::runtime_error("Localization key oracle failed"); }
void refuse(const std::function<void()>& action) {
    try { action(); } catch (const std::exception&) { ++refusals; return; }
    throw std::runtime_error("Invalid key input was accepted after " + std::to_string(refusals) + " refusals");
}
Json::Value catalog() { Json::Value value(Json::objectValue); value["schema_version"] = 1; value["strings"] = Json::arrayValue; return value; }
Json::Value row(const std::string& text, const std::string& key) {
    Json::Value value(Json::objectValue); value["source"] = text; value["name"] = key; return value;
}
void write(const fs::path& path, const std::string& bytes) {
    std::ofstream file(path, std::ios::binary); file << bytes;
    require(static_cast<bool>(file));
}
std::string read(const fs::path& path) { std::ifstream file(path, std::ios::binary); return {std::istreambuf_iterator<char>(file), {}}; }
std::string json(const Json::Value& value) { Json::StreamWriterBuilder builder; return Json::writeString(builder, value); }
}
int main(int argc, char** argv) {
    try {
        require(argc == 2);
        auto empty = catalog();
        require(validate_keys(empty) == ure_locale_host::KeyTable{{"Extra", "ure_extra_tab"}});
        auto one = empty; one["strings"].append(row("Confirm", "ure_confirm"));
        const auto table = validate_keys(one);
        require(table.size() == 2 && table.at("Confirm") == "ure_confirm" && table.at("Extra") == "ure_extra_tab");
        // Stress the lifetime with many independently owned rows, including non-BMP UTF-8.
        auto full = empty;
        for (unsigned i = 0; i < 16384; ++i) full["strings"].append(row("Message " + std::to_string(i), "ure_" + std::to_string(i)));
        const auto many = validate_keys(full);
        require(many.size() == 16385);
        for (const auto& entry : full["strings"]) require(many.at(entry["source"].asString()) == entry["name"].asString());
        full["strings"].append(row("Beyond", "ure_beyond")); refuse([&] { validate_keys(full); });
        auto alias = empty; alias["strings"].append(row("Extra", "ure_extra_tab")); require(validate_keys(alias).size() == 1);
        for (const auto& invalid : {Json::Value(), Json::Value(Json::arrayValue), Json::Value("strings")}) refuse([&] { validate_keys(invalid); });
        for (const auto& bad : {Json::Value(), Json::Value(0), Json::Value(2), Json::Value("1"), Json::Value(true)}) {
            auto value = one; value["schema_version"] = bad; refuse([&] { validate_keys(value); });
        }
        for (const auto& bad : {Json::Value(), Json::Value(Json::objectValue), Json::Value("array")}) {
            auto value = one; value["strings"] = bad; refuse([&] { validate_keys(value); });
        }
        for (const auto& bad : {Json::Value(), Json::Value("row"), Json::Value(Json::arrayValue)}) {
            auto value = empty; value["strings"].append(bad); refuse([&] { validate_keys(value); });
        }
        for (const auto* field : {"name", "source"}) for (const auto& bad : {Json::Value(), Json::Value(7), Json::Value(true), Json::Value(Json::arrayValue)}) {
            auto value = one; value["strings"][0][field] = bad; refuse([&] { validate_keys(value); });
        }
        for (const auto& text : {std::string(), std::string("a\0b", 3), std::string("\xc0\xaf", 2), std::string("\xed\xa0\x80", 3),
            std::string("\xf4\x90\x80\x80", 4), std::string("\xe2\x82", 2), std::string(65537, 'a')}) {
            auto value = empty; value["strings"].append(row(text, "ure_valid")); refuse([&] { validate_keys(value); });
        }
        for (const auto& key : {std::string(), std::string("1key"), std::string("key name"), std::string("key/name"), std::string("key%"), std::string(129, 'a')}) {
            auto value = empty; value["strings"].append(row("Valid", key)); refuse([&] { validate_keys(value); });
        }
        for (const auto& duplicate : {row("Confirm", "other_key"), row("Other", "ure_confirm"), row("Confirm", "ure_confirm"),
            row("Extra", "other_extra"), row("Other", "ure_extra_tab")}) {
            auto value = one; value["strings"].append(duplicate); refuse([&] { validate_keys(value); });
        }
        auto oversized = empty;
        for (unsigned i = 0; i < 34; ++i) oversized["strings"].append(row(std::to_string(i) + std::string(65530, 'a'), "ure_" + std::to_string(i)));
        refuse([&] { validate_keys(oversized); });
        const auto fixture = fs::path(argv[1]) / ("locale-key-fixture-" + std::to_string(getpid()));
        require(fs::create_directory(fixture));
        const auto input = fixture / "catalog.json", output = fixture / "keys.hpp";
        write(input, json(one));
        ure_locale_host::generate_keys(input, output);
        const auto good = read(output); require(good == ure_locale_host::key_header(table));
        for (const auto& malformed : {std::string("{"), std::string("{\"schema_version\":1,\"strings\":[],\"strings\":[]}"),
            std::string("{\"schema_version\":1,\"strings\":[]} trailing"), std::string("{/*comment*/\"schema_version\":1,\"strings\":[]}")}) {
            write(input, malformed); refuse([&] { ure_locale_host::generate_keys(input, output); }); require(read(output) == good);
        }
        write(input, "{\"schema_version\":1,\"strings\":[],\"nested\":" + std::string(80, '[') + "0" + std::string(80, ']') + "}");
        refuse([&] { ure_locale_host::generate_keys(input, output); }); require(read(output) == good);
        auto slash = empty; slash["strings"].append(row("Data / path // /* remains data */", "ure_slash"));
        write(input, json(slash)); ure_locale_host::generate_keys(input, output);
        require(read(output) == ure_locale_host::key_header(validate_keys(slash)));
        write(input, json(one)); ure_locale_host::generate_keys(input, output); require(read(output) == good);
        write(input, json(one)); refuse([&] { ure_locale_host::generate_keys(input, input); }); require(read(input) == json(one));
        fs::create_symlink(input, fixture / "input-link"); refuse([&] { ure_locale_host::generate_keys(fixture / "input-link", output); }); require(read(output) == good);
        fs::create_symlink(output, fixture / "output-link"); refuse([&] { ure_locale_host::generate_keys(input, fixture / "output-link"); }); require(read(output) == good);
        fs::create_hard_link(output, fixture / "output-hard"); refuse([&] { ure_locale_host::generate_keys(input, output); }); require(read(output) == good);
        fs::remove(fixture / "output-hard");
        fs::create_directory(fixture / "directory"); refuse([&] { ure_locale_host::generate_keys(input, fixture / "directory"); });
        require(mkfifo((fixture / "fifo").c_str(), 0600) == 0); refuse([&] { ure_locale_host::generate_keys(fixture / "fifo", output); }); require(read(output) == good);
        refuse([&] { ure_locale_host::generate_keys(input, fixture / "fifo"); }); require(read(output) == good);
        fs::create_symlink(fixture / "directory", fixture / "parent-link"); refuse([&] { ure_locale_host::generate_keys(input, fixture / "parent-link" / "keys.hpp"); });
        refuse([&] { ure_locale_host::generate_keys(input, fixture / "directory" / ".." / "keys.hpp"); });
        fs::resize_file(input, 32U * 1024U * 1024U + 1U); refuse([&] { ure_locale_host::generate_keys(input, output); }); require(read(output) == good);
        for (const auto& entry : fs::directory_iterator(fixture)) require(!entry.path().filename().string().starts_with(".ure-key-header-"));
        fs::remove_all(fixture);
        std::cout << "Key owner/schema controls passed: empty, one, 16384 rows and " << refusals << " actual refusals; original header preserved.\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
