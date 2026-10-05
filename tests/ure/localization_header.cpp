// Independent compile/lookup oracle for generated empty, one and full catalogs.
#include "ure-localization.hpp"
#include <json/json.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>

void require(bool condition) { if (!condition) throw std::runtime_error("Generated key closure failed"); }
int main(int argc, char** argv) {
    try {
        require(argc == 2);
        std::ifstream stream(argv[1], std::ios::binary);
        const std::string bytes{std::istreambuf_iterator<char>(stream), {}};
        Json::Value owner; std::string errors; Json::CharReaderBuilder builder;
        std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
        require(reader->parse(bytes.data(), bytes.data() + bytes.size(), &owner, &errors));
        std::vector<std::pair<std::string, std::string>> expected;
        bool has_extra = false;
        for (const auto& item : owner["strings"]) {
            expected.emplace_back(item["source"].asString(), item["name"].asString());
            if (item["source"] == "Extra") { has_extra = true; require(item["name"] == "ure_extra_tab"); }
        }
        if (!has_extra) expected.emplace_back("Extra", "ure_extra_tab");
        std::sort(expected.begin(), expected.end());
        const auto count = static_cast<std::size_t>(std::end(ure_locale::entries) - std::begin(ure_locale::entries));
        require(count == expected.size());
        std::set<std::string> names;
        for (std::size_t i = 0; i < count; ++i) {
            const auto& entry = ure_locale::entries[i];
            require(entry.source == expected[i].first && entry.key == expected[i].second);
            require(names.emplace(entry.key).second);
            if (i != 0) require(ure_locale::entries[i - 1].source < entry.source);
            bool called = false;
            const ure_locale::Lookup lookup = [&](const std::string& key, const std::string& fallback) {
                require(key == entry.key && fallback == entry.source); called = true; return "first:" + key;
            };
            require(ure_locale::translate(entry.source, lookup) == "first:" + std::string(entry.key) && called);
            const ure_locale::Lookup next = [](const std::string& key, const std::string&) { return "second:" + key; };
            require(ure_locale::translate(entry.source, next) == "second:" + std::string(entry.key));
        }
        const std::string unknown = "\x01uncatalogued\x02";
        require(ure_locale::translate(unknown, [](const auto&, const auto&) -> std::string { throw std::runtime_error("Unknown text reached lookup"); }) == unknown);
        const ure_locale::Lookup lookup = [](const std::string& key, const std::string&) { return "translated:" + key; };
        const auto& first = ure_locale::entries[0];
        const std::string opaque = "/dev/example/" + std::string(first.source) + " 0123456789 %userdata%";
        ure_locale::Message message; message.prose(std::string(first.source)).data(opaque);
        require(message.render(lookup) == "translated:" + std::string(first.key) + opaque);
        std::string displayed;
        ure_locale::remember("ure_status", message);
        require(ure_locale::display_variable("ure_status", message.text(), lookup, displayed) && displayed == message.render(lookup));
        require(!ure_locale::display_variable("ure_raw_source", opaque, lookup, displayed));
        std::cout << "Compiled generated header and actual lookup adapter: " << count << " exact ordered entries; original UTF-8 and opaque data preserved.\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
