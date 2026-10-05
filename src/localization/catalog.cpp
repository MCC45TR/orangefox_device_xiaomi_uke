// Host-only localization tooling. Uses pinned JsonCpp and host libxml2/OpenSSL.
// No translator, XML tooling or network client is installed in recovery.
#include <json/json.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <openssl/sha.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include "key-catalog.hpp"
#include "catalog-source.hpp"

namespace fs = std::filesystem;
using Strings = std::map<std::string, std::string>;
std::string read(const fs::path& path) {
    return ure_locale_host::read_regular(path);
}
void write(const fs::path& path, const std::string& bytes) {
    ure_locale_host::publish_regular(path, bytes);
}
std::string digest(const std::string& bytes) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size(), hash);
    std::ostringstream output;
    for (const auto byte : hash) output << std::hex << std::setw(2) << std::setfill('0') << unsigned(byte);
    return output.str();
}
Json::Value parse(const std::string& bytes) {
    return ure_locale_host::parse_strict(bytes);
}
std::string json(const Json::Value& value) {
    Json::StreamWriterBuilder builder; builder["indentation"] = "  ";
    return Json::writeString(builder, value) + "\n";
}
struct Document {
    xmlDocPtr value;
    explicit Document(const fs::path& path) : value(nullptr) {
        const auto bytes = read(path);
        value = xmlReadMemory(bytes.data(), static_cast<int>(bytes.size()), nullptr, nullptr,
            XML_PARSE_NONET | XML_PARSE_NOBLANKS | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
        if (!value) throw std::runtime_error("Invalid XML input: " + path.filename().string());
        if (value->intSubset || value->extSubset) {
            xmlFreeDoc(value); value = nullptr;
            throw std::runtime_error("XML DTDs are not permitted");
        }
    }
    ~Document() { xmlFreeDoc(value); }
    Document(const Document&) = delete;
};
std::string content(xmlNodePtr node) {
    auto* data = xmlNodeGetContent(node); std::string result = data ? reinterpret_cast<char*>(data) : "";
    xmlFree(data); return result;
}
std::string attribute(xmlNodePtr node, const char* name) {
    auto* data = xmlGetProp(node, BAD_CAST name); std::string result = data ? reinterpret_cast<char*>(data) : "";
    xmlFree(data); return result;
}
std::string name(xmlNodePtr node) { return reinterpret_cast<const char*>(node->name); }
std::string site(xmlNodePtr node) {
    std::vector<std::string> parts;
    for(auto* current=node;current && current->type==XML_ELEMENT_NODE;current=current->parent) {
        unsigned index=0;
        for(auto* previous=current->prev;previous;previous=previous->prev)
            if(previous->type==XML_ELEMENT_NODE && name(previous)==name(current))++index;
        auto part=name(current)+"["+std::to_string(index)+"]";
        if(name(current)=="page")part+=":"+attribute(current,"name");
        parts.push_back(std::move(part));
    }
    std::string result="maintainer";
    for(auto part=parts.rbegin();part!=parts.rend();++part)result+="/"+*part;
    return result;
}
void visit(xmlNodePtr node, const std::function<void(xmlNodePtr)>& callback) {
    for (; node; node = node->next) if (node->type == XML_ELEMENT_NODE) {
        callback(node); visit(node->children, callback);
    }
}
Strings strings(Document& document) {
    Strings result;
    visit(xmlDocGetRootElement(document.value), [&](auto* node) {
        if (name(node) == "string") {
            const auto key = attribute(node, "name");
            if (key.empty()) throw std::runtime_error("Empty language key");
            // The pinned upstream files repeat several keys, sometimes with
            // different wording. Match ResourceManager's last-definition policy;
            // materialized project files will contain exactly one definition.
            result[key] = content(node);
        }
    });
    return result;
}
const std::regex variables(R"(%[A-Za-z_][A-Za-z_0-9]*%)");
std::multiset<std::string> placeholders(const std::string& text) {
    static const std::regex pattern(R"(%[A-Za-z_][A-Za-z_0-9]*%|%(?:[0-9]+\$)?[-+ #0]*[0-9]*(?:\.[0-9]+)?(?:hh|ll|[hljztL])?[sdiuoxXfFeEgGaAcCp]|\{[0-9]+\})");
    std::multiset<std::string> result;
    for (std::sregex_iterator item(text.begin(), text.end(), pattern), end; item != end; ++item)
        result.insert(item->str());
    return result;
}
bool human(const std::string& input) {
    if (input.empty() || input.find("{@") != std::string::npos) return false;
    const auto text = std::regex_replace(input, variables, "");
    if (!std::regex_search(text, std::regex("[A-Za-z]"))) return false;
    if (text.find("https://") != std::string::npos || text.find("http://") != std::string::npos ||
        text.find("==") != std::string::npos || text.find(" ? ") != std::string::npos ||
        text.find("\\\"") != std::string::npos) return false;
    if (std::regex_match(text, std::regex(R"([A-Za-z0-9_.-]+\.(?:json|xml|efi|conf|img|log|bin|sh))"))) return false;
    static const std::set<std::string> technical = {"Android", "Linux", "Windows", "ESP", "UEFI", "GPT", "ADB",
        "USB", "MB", "GB", "MiB", "GiB", "ext4", "f2fs", "F2FS", "FAT", "FAT32", "fat32", "exfat", "exFAT",
        "ntfs", "NTFS", "btrfs", "Btrfs", "512", "4096", "auto", "none", "yes", "no", "standard", "advanced",
        "Xiaomi Pad 7", "POCO Pad X1", "OrangeFox", "3000", "3200"};
    return !technical.count(text);
}
std::vector<fs::path> languages(const fs::path& component) {
    std::vector<fs::path> result;
    const auto theme = component / "src/upstream/orangefox-android16/bootable/recovery/gui/theme";
    for (const auto* directory : {"common/languages", "extra-languages/languages"})
        for (const auto& entry : fs::directory_iterator(theme / directory))
            if (entry.path().extension() == ".xml") result.push_back(entry.path());
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.filename() < b.filename(); });
    return result;
}
std::string target(const std::string& locale) {
    static const Strings special = {{"en", "en"}, {"ru", "ru"}, {"hu", "hu"}, {"es-ES", "es"},
        {"zh_CN", "zh-CN"}, {"zh_TW", "zh-TW"}, {"sr_Cyrl", "sr-Cyrl"}, {"he_IL", "he"},
        {"pt_BR", "pt-BR"}, {"pt_PT", "pt-PT"}};
    const auto found = special.find(locale);
    return found == special.end() ? locale.substr(0, locale.find('_')) : found->second;
}
Json::Value collect_catalog(const fs::path& component) {
    const auto theme = component / "src/upstream/orangefox-android16/bootable/recovery/gui/theme";
    Document english(theme / "common/languages/en.xml");
    const auto base = strings(english);
    std::map<std::string,std::string> source_inputs;
    const auto record = [&](const fs::path& path) {
        source_inputs.emplace(fs::relative(path,component).generic_string(),digest(read(path)));
    };
    record(theme / "common/languages/en.xml");
    record(component / "src/device/xiaomi/uke/maintainer.xml");
    record(component / "src/localization/catalog.cpp");
    record(component / "src/localization/catalog-source.hpp");
    record(component / "src/localization/key-catalog.cpp");
    record(component / "src/localization/key-catalog.hpp");
    record(component / "src/localization/review-plan.cpp");
    record(component / "src/localization/review-plan.hpp");
    record(component / "src/localization/review-main.cpp");
    record(component / "configs/localization-inputs.json");
    Strings by_text;
    for (const auto& [key, text] : base) by_text.emplace(text, key);
    std::map<std::string, std::set<std::string>> contexts;
    auto collect = [&](const std::string& text, const std::string& context) {
        if (human(text)) contexts[text].insert(context);
    };
    Document maintainer(component / "src/device/xiaomi/uke/maintainer.xml");
    visit(xmlDocGetRootElement(maintainer.value), [&](auto* node) {
        const auto type = name(node);
        if (type == "text" && (!node->children || node->children->type == XML_TEXT_NODE))
            collect(content(node), site(node)+":text");
        if (type == "listitem") {
            collect(attribute(node, "name"), site(node)+":name");
            collect(attribute(node, "description"), site(node)+":description");
        }
        if (type == "variable" && attribute(node, "value").find(' ') != std::string::npos)
            collect(attribute(node, "value"), site(node)+":default");
    });
    // Runtime human messages are catalogued separately from paths, command names,
    // field IDs and JSON. A later GUI adapter only translates whitelisted display variables.
    std::vector<fs::path> native = {component / "src/device/xiaomi/uke/ure-gui.cpp"};
    for (const auto& entry : fs::directory_iterator(component / "src/device/xiaomi/uke/recoveryctl/libuke"))
        if (entry.path().extension() == ".cpp") native.push_back(entry.path());
    const std::regex literal(R"REGEX("((?:\\.|[^"\\])*)")REGEX");
    std::sort(native.begin(), native.end());
    for (const auto& path : native) {
        record(path);
        const auto cpp = read(path);
        for (std::sregex_iterator item(cpp.begin(), cpp.end(), literal), end; item != end; ++item) {
            try {
                const auto text = parse(item->str()).asString();
                if (text.find(' ') != std::string::npos && text.find("/dev/") == std::string::npos &&
                    text.find("/sys/") == std::string::npos && text.find("/mnt/") == std::string::npos &&
                    text.find("/tmp/") == std::string::npos)
                    collect(text, "native:" + fs::relative(path,component).generic_string() + ":" +
                        std::to_string(1 + std::count(cpp.begin(),cpp.begin()+item->position(),'\n')));
            } catch (const std::exception&) { /* C++-specific byte escapes are not prose. */ }
        }
    }
    for (const auto* text : {"Idle", "Running", "Completed", "Cancelled", "Failed", "Read only", "Writable",
        "Standard", "Advanced", "After userdata", "Before userdata", "Preserve data", "Erase and recreate",
        "Format", "Check", "Repair", "Resize", "Image", "Live storage", "Full backup", "Incremental backup",
        "Snapshot", "Scrub", "Balance", "Rollback", "Delete", "Remaining space", "Automatic", "On", "Off"})
        collect(text, "native:display-alias");
    Json::Value catalog(Json::objectValue); catalog["schema_version"] = 1;
    catalog["base_english_sha256"] = digest(read(theme / "common/languages/en.xml"));
    catalog["strings"] = Json::arrayValue;
    for (const auto& [text, sites] : contexts) {
        const auto existing = by_text.find(text);
        const auto key = text == "Extra" ? "ure_extra_tab" :
            existing == by_text.end() ? "ure_text_" + digest(text).substr(0, 16) : existing->second;
        Json::Value row(Json::objectValue); row["name"] = key; row["source"] = text;
        row["upstream_key"] = existing != by_text.end(); row["contexts"] = Json::arrayValue;
        for (const auto& site : sites) row["contexts"].append(site);
        catalog["strings"].append(row);
    }
    catalog["languages"] = Json::arrayValue;
    const auto configuration=ure_locale_host::parse_strict(read(component / "configs/localization-inputs.json"));
    if(!configuration["language_codes"].isArray() || configuration["language_codes"].size()!=32)
        throw std::runtime_error("Expected the reviewed 32-language configuration");
    std::set<std::string> expected_locales,present_locales;
    for(const auto& locale:configuration["language_codes"]) {
        if(!locale.isString() || !expected_locales.insert(locale.asString()).second)
            throw std::runtime_error("Invalid or duplicate configured language");
    }
    for (const auto& path : languages(component)) {
        record(path);
        Document language(path); const auto present = strings(language);
        const auto locale = path.stem().string();
        if(!present_locales.insert(locale).second || !expected_locales.count(locale))
            throw std::runtime_error("Duplicate or unexpected language resource");
        Json::Value row(Json::objectValue); row["locale"] = locale; row["target"] = target(locale);
        row["input_path"] = fs::relative(path, component).generic_string(); row["input_sha256"] = digest(read(path));
        row["missing"] = Json::arrayValue;
        if (locale != "en") {
            for (const auto& [key, text] : base) if (human(text) && (!present.count(key) || present.at(key).empty() ||
                placeholders(text) != placeholders(present.at(key)) || present.at(key) == text)) {
                Json::Value missing(Json::objectValue); missing["name"] = key; missing["source"] = text;
                missing["contexts"] = Json::arrayValue; missing["contexts"].append("upstream:missing-or-invalid");
                row["missing"].append(missing);
            }
            for (const auto& text : catalog["strings"]) if (!present.count(text["name"].asString())) {
                Json::Value reference(Json::objectValue); reference["name"]=text["name"]; reference["catalog_source"]=true;
                row["missing"].append(reference);
            }
        }
        catalog["languages"].append(row);
    }
    if(present_locales!=expected_locales)throw std::runtime_error("Missing supported language resource");
    catalog["source_inputs"] = Json::objectValue;
    for (const auto& [path,hash] : source_inputs)catalog["source_inputs"][path] = hash;
    catalog["source_inputs_sha256"] = digest(json(catalog["source_inputs"]));
    return catalog;
}
#ifndef UKE_LOCALE_SOURCE_ONLY
int main(int argc, char** argv) {
    try {
        if (argc == 4 && std::string(argv[1]) == "prepare") {
            const auto catalog = collect_catalog(fs::canonical(argv[2]));
            write(fs::absolute(argv[3])/"catalog.json",json(catalog));
            std::cout << "Catalogued " << catalog["strings"].size() << " GUI messages across "
                      << catalog["languages"].size() << " supported languages\n";
        } else if (argc == 4 && std::string(argv[1]) == "keys") ure_locale_host::generate_keys(argv[2], argv[3]);
        else if (argc > 1 && (std::string(argv[1]) == "jobs" || std::string(argv[1]) == "split" ||
            std::string(argv[1]) == "combine" || std::string(argv[1]) == "materialize" || std::string(argv[1]) == "import"))
            throw std::runtime_error("Translation import/materialization is disabled pending provenance validation");
        else throw std::runtime_error("Usage: uke-locale-catalog prepare COMPONENT OUTPUT | keys CATALOG OUTPUT");
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
#endif
