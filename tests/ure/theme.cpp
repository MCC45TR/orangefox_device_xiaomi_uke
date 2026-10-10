// SPDX-License-Identifier: GPL-3.0-or-later
#include "ure-theme.hpp"
#include "rapidxml.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <vector>

namespace rapidxml {
void parse_error_handler(const char*, void*) { std::abort(); }
}

static void parse(std::string xml) {
    std::vector<char> buffer(xml.begin(), xml.end()); buffer.push_back(0);
    rapidxml::xml_document<> document;
    document.parse<0>(buffer.data());
    assert(document.first_node("recovery"));
}
struct DataManager {
    static inline std::map<std::string, std::string> values;
    static int GetIntValue(const char* name) { return std::atoi(values[name].c_str()); }
    static std::string GetStrValue(const char* name) { return values[name]; }
    static void SetValue(const char* name, int value) { values[name] = std::to_string(value); }
    static int SetValue(const char* name, const std::string& value, int = 0) { values[name] = value; return 0; }
    static int Flush() { return 0; } // Handoff fixture; no device persistence.
    static void QueuePreferences() {}
};
struct PageManager { static inline unsigned requests = 0; static void RequestUreThemeReload() { ++requests; } };
static void gui_err(const char*) {}
struct GUIAction { int uretheme(std::string); };
// The only substituted boundary is the packaged root used by this host test.
static const char* fixture_root;
namespace ure::theme {
inline bool fixture_prepare(const std::string& style, const std::string& name, const std::string& light,
        const std::string& dark, bool use_dark, Overlay& result) {
    return prepare(style, name, light, dark, use_dark, result, fixture_root);
}
}
#define prepare fixture_prepare
#include "theme-handler.inc"
#undef prepare

static void palettes(rapidxml::xml_node<>* node, std::vector<std::map<std::string, std::string>>& all) {
    for (; node; node = node->next_sibling()) {
        if (std::string(node->name()) == "button") {
            std::map<std::string, std::string> fields;
            for (auto* action = node->first_node("action"); action; action = action->next_sibling("action")) {
                auto* function = action->first_attribute("function");
                if (!function || std::string(function->value()) != "set") continue;
                const std::string value = action->value(); const auto equal = value.find('=');
                if (equal != std::string::npos) fields[value.substr(0, equal)] = value.substr(equal + 1);
            }
            if (fields.count("theme_accent_tmp") && fields.count("theme_accent_light_tmp") && fields.count("theme_accent_dark_tmp")) all.push_back(fields);
        }
        palettes(node->first_node(), all);
    }
}
int main(int argc, char** argv) {
    assert(argc == 3); fixture_root = argv[1];
    std::ifstream input(std::string(argv[1]) + "/pages/customization.xml");
    std::string xml((std::istreambuf_iterator<char>(input)), {});
    std::vector<char> buffer(xml.begin(), xml.end()); buffer.push_back(0);
    rapidxml::xml_document<> document; document.parse<0>(buffer.data());
    std::vector<std::map<std::string, std::string>> choices;
    palettes(document.first_node(), choices); assert(choices.size() >= 12);
    std::set<std::string> names; unsigned applied = 0;
    GUIAction action;
    for (const char* style : {"Black", "Cream", "Dark", "Gray", "Light"}) {
        for (const auto& choice : choices) {
            for (const char* mode : {"Default", "Black"}) {
                DataManager::values = choice;
                DataManager::values["theme_style_tmp"] = style;
                DataManager::values["theme_style_act_tmp"] = mode;
                assert(action.uretheme("") == 0);
                names.insert(choice.at("theme_accent_tmp"));
                char* style_xml = nullptr; char* accent_xml = nullptr;
                assert(ure::theme::buffer("/owner/Fox/.theme/style.xml", "/owner/Fox/.theme", &style_xml) && style_xml);
                assert(ure::theme::buffer("/owner/Fox/.theme/accent.xml", "/owner/Fox/.theme", &accent_xml) && accent_xml);
                parse(style_xml); parse(accent_xml);
                const std::string expected = choice.at(std::string(mode) == "Default" ? "theme_accent_dark_tmp" : "theme_accent_light_tmp");
                assert(std::string(accent_xml).find(expected) != std::string::npos);
                assert(std::string(accent_xml).find("#COLOR") == std::string::npos);
                std::free(style_xml); std::free(accent_xml); ++applied;
            }
        }
    }
    assert(names.size() >= 10 && PageManager::requests == applied);
    auto requests = PageManager::requests;
    DataManager::values["theme_style_tmp"] = "../../etc/passwd";
    assert(action.uretheme("") == 1 && PageManager::requests == requests);
    DataManager::values["theme_style_tmp"] = "Dark";
    for (auto invalid : {"#fff", "#FFFFFF\"/>", "#GGGGGG", "red"}) {
        DataManager::values["theme_accent_light_tmp"] = invalid;
        assert(action.uretheme("") == 1 && PageManager::requests == requests);
    }
    DataManager::values["theme_accent_light_tmp"] = "#112233";
    DataManager::values["theme_accent_tmp"] = "\"/><action>";
    assert(action.uretheme("") == 1);
    DataManager::values["theme_accent_tmp"] = "Fox";
    DataManager::values["update_fonts"] = "1";
    assert(action.uretheme("") == 1 && PageManager::requests == requests);
    DataManager::values["update_fonts"] = "0";
    DataManager::values["remove_theme"] = "1";
    assert(action.uretheme("") == 0);
    char* inactive = nullptr;
    assert(!ure::theme::buffer("/owner/Fox/.theme/style.xml", "/owner/Fox/.theme", &inactive));
    ure::theme::rollback();
    assert(ure::theme::buffer("/owner/Fox/.theme/style.xml", "/owner/Fox/.theme", &inactive) && inactive);
    std::free(inactive);
    // No-follow guard is tested on a throwaway root, never packaged sources.
    const std::string root = argv[2];
    assert(::mkdir((root + "/themes").c_str(), 0700) == 0);
    assert(::symlink((std::string(argv[1]) + "/themes/styles").c_str(), (root + "/themes/styles").c_str()) == 0);
    ure::theme::Overlay rejected;
    assert(!ure::theme::prepare("Dark", "Fox", "#112233", "#445566", false, rejected, root.c_str()));
    std::cout << applied << " real style/palette/mode combinations parsed; exact native action, rollback and invalid/symlink refusal passed.\n";
}
