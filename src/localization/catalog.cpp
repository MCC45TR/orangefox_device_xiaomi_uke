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

namespace fs = std::filesystem;
using Strings = std::map<std::string, std::string>;
std::string read(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot read input: " + path.filename().string());
    return {std::istreambuf_iterator<char>(stream), {}};
}
void write(const fs::path& path, const std::string& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << bytes;
    if (!stream) throw std::runtime_error("Cannot write output: " + path.filename().string());
}
std::string digest(const std::string& bytes) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size(), hash);
    std::ostringstream output;
    for (const auto byte : hash) output << std::hex << std::setw(2) << std::setfill('0') << unsigned(byte);
    return output.str();
}
Json::Value parse(const std::string& bytes) {
    Json::Value value; std::string errors;
    Json::CharReaderBuilder builder;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    if (!reader->parse(bytes.data(), bytes.data() + bytes.size(), &value, &errors))
        throw std::runtime_error("Invalid JSON input");
    return value;
}
std::string json(const Json::Value& value) {
    Json::StreamWriterBuilder builder; builder["indentation"] = "  ";
    return Json::writeString(builder, value) + "\n";
}
struct Document {
    xmlDocPtr value;
    explicit Document(const fs::path& path) : value(xmlReadFile(path.c_str(), nullptr,
        XML_PARSE_NONET | XML_PARSE_NOBLANKS | XML_PARSE_NOERROR | XML_PARSE_NOWARNING)) {
        if (!value) throw std::runtime_error("Invalid XML input: " + path.filename().string());
        if (value->intSubset || value->extSubset) {
            xmlFreeDoc(value); value = nullptr;
            throw std::runtime_error("XML DTDs are not permitted");
        }
    }
    ~Document() { xmlFreeDoc(value); }
    Document(const Document&) = delete;
    void save(const fs::path& path) const {
        fs::create_directories(path.parent_path());
        if (xmlSaveFormatFileEnc(path.c_str(), value, "UTF-8", 1) < 0)
            throw std::runtime_error("Cannot save XML output");
    }
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
        {"zh_CN", "zh-CN"}, {"zh_TW", "zh-TW"}, {"sr_Cyrl", "sr"}, {"he_IL", "he"}};
    const auto found = special.find(locale);
    return found == special.end() ? locale.substr(0, locale.find('_')) : found->second;
}
Json::Value prepare(const fs::path& component, const fs::path& output) {
    const auto theme = component / "src/upstream/orangefox-android16/bootable/recovery/gui/theme";
    Document english(theme / "common/languages/en.xml");
    const auto base = strings(english);
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
            collect(content(node), "maintainer:text");
        if (type == "listitem") {
            collect(attribute(node, "name"), "maintainer:listitem");
            collect(attribute(node, "description"), "maintainer:description");
        }
        if (type == "variable" && attribute(node, "value").find(' ') != std::string::npos)
            collect(attribute(node, "value"), "maintainer:default");
    });
    // Runtime human messages are catalogued separately from paths, command names,
    // field IDs and JSON. A later GUI adapter only translates whitelisted display variables.
    std::vector<fs::path> native = {component / "src/device/xiaomi/uke/ure-gui.cpp"};
    for (const auto& entry : fs::directory_iterator(component / "src/device/xiaomi/uke/recoveryctl/libuke"))
        if (entry.path().extension() == ".cpp") native.push_back(entry.path());
    const std::regex literal(R"REGEX("((?:\\.|[^"\\])*)")REGEX");
    for (const auto& path : native) {
        const auto cpp = read(path);
        for (std::sregex_iterator item(cpp.begin(), cpp.end(), literal), end; item != end; ++item) {
            try {
                const auto text = parse(item->str()).asString();
                if (text.find(' ') != std::string::npos && text.find("/dev/") == std::string::npos &&
                    text.find("/sys/") == std::string::npos && text.find("/mnt/") == std::string::npos &&
                    text.find("/tmp/") == std::string::npos)
                    collect(text, "native:" + path.filename().string());
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
    for (const auto& path : languages(component)) {
        Document language(path); const auto present = strings(language);
        const auto locale = path.stem().string();
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
            for (const auto& text : catalog["strings"]) if (!present.count(text["name"].asString()))
                row["missing"].append(text);
        }
        catalog["languages"].append(row);
    }
    write(output / "catalog.json", json(catalog));
    return catalog;
}
std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}
Json::Value mask(const Json::Value& source) {
    Json::Value result = source;
    const auto text = source["source"].asString();
    const auto first = text.find_first_not_of(" \t\r\n");
    const auto last = text.find_last_not_of(" \t\r\n");
    result["leading"] = first == std::string::npos ? text : text.substr(0, first);
    result["trailing"] = last == std::string::npos ? "" : text.substr(last + 1);
    const auto middle = trim(text);
    static const std::regex pattern(
        R"(%[A-Za-z_][A-Za-z_0-9]*%|%(?:[0-9]+\$)?[-+ #0]*[0-9]*(?:\.[0-9]+)?(?:hh|ll|[hljztL])?[sdiuoxXfFeEgGaAcCp]|\{[0-9]+\}|`[^`]*`|"yes"|\b[A-Z][A-Z0-9]+(?:[-_][A-Z0-9]+)+\b|\b[A-Za-z]+_[A-Za-z0-9_]+\b|\b(?:userdata|metadata|ESP|UEFI|GPT|PARTUUID|UUID|ADB|USB|MiB|GiB|GB|MB|ext4|F2FS|f2fs|FAT32|fat32|exFAT|exfat|NTFS|ntfs|Btrfs|btrfs|Linux|Android|Windows|KeyMint|TEE|AVB|OTA|UKI|BLS|SELinux|RAM|CPU|Hz|FIFO|DTBO|DTB)\b|\r\n|\n|\t)");
    std::string wire; std::size_t previous = 0, index = 0;
    result["tokens"] = Json::arrayValue;
    for (std::sregex_iterator item(middle.begin(), middle.end(), pattern), end; item != end; ++item) {
        const auto position = static_cast<std::size_t>(item->position());
        wire += middle.substr(previous, position - previous);
        std::ostringstream marker; marker << "ZXQ" << std::setw(4) << std::setfill('0') << index++ << "QXZ";
        Json::Value token(Json::objectValue); token["marker"] = marker.str(); token["source"] = item->str();
        result["tokens"].append(token); wire += marker.str(); previous = position + item->length();
    }
    wire += middle.substr(previous); result["wire"] = wire;
    return result;
}
void jobs(const fs::path& catalog_path, const fs::path& output) {
    const auto catalog = parse(read(catalog_path));
    unsigned total = 0;
    for (const auto& language : catalog["languages"]) {
        if (language["locale"] == "en") continue;
        Json::Value job;
        unsigned number = 0;
        auto reset = [&] {
            job = Json::objectValue; job["locale"] = language["locale"]; job["target"] = language["target"];
            job["items"] = Json::arrayValue; job["query"] = "";
        };
        auto flush = [&] {
            if (job["items"].empty()) return;
            std::ostringstream suffix; suffix << language["locale"].asString() << '-' << std::setw(4) << std::setfill('0') << number++;
            write(output / (suffix.str() + ".json"), json(job)); ++total; reset();
        };
        reset(); std::set<std::string> names;
        for (const auto& source : language["missing"]) {
            if (!names.insert(source["name"].asString()).second) continue;
            auto item = mask(source);
            const auto make_line = [&](unsigned index) {
                std::ostringstream marker; marker << '[' << std::setw(6) << std::setfill('0') << index << "] ";
                return marker.str() + item["wire"].asString() + "\n";
            };
            if (job["query"].asString().size() + make_line(job["items"].size()).size() > 4300 || job["items"].size() >= 70) flush();
            job["query"] = job["query"].asString() + make_line(job["items"].size()); job["items"].append(item);
        }
        flush();
    }
    std::cout << "Prepared " << total << " bounded host translation batches\n";
}
void import_response(const fs::path& job_path, const fs::path& response_path, const fs::path& output) {
    const auto job = parse(read(job_path)), response = parse(read(response_path));
    if (!response.isArray() || !response[0].isArray()) throw std::runtime_error("Invalid translation response");
    std::string translated, echoed;
    for (const auto& segment : response[0]) {
        if (!segment.isArray() || !segment[0].isString() || !segment[1].isString()) throw std::runtime_error("Invalid translation segment");
        translated += segment[0].asString();
        echoed += segment[1].asString();
    }
    if (trim(echoed) != trim(job["query"].asString()))
        throw std::runtime_error("Translation response does not match this job's source text");
    const std::regex marker(R"(\[([0-9]{6})\])");
    const bool single = job.get("single_message", false).asBool();
    std::vector<std::pair<std::size_t, std::smatch>> markers;
    for (std::sregex_iterator item(translated.begin(), translated.end(), marker), end; item != end; ++item)
        markers.emplace_back(static_cast<std::size_t>(item->position()), *item);
    if (single && job["items"].size() != 1) throw std::runtime_error("Invalid single-message job");
    if (!single && markers.size() != job["items"].size()) throw std::runtime_error("Translation dropped or added a message marker");
    Json::Value result(Json::objectValue); result["locale"] = job["locale"]; result["strings"] = Json::objectValue;
    result["job_sha256"] = digest(read(job_path)); result["response_sha256"] = digest(read(response_path));
    for (std::size_t index = 0; index < job["items"].size(); ++index) {
        if (!single && std::stoul(markers[index].second[1].str()) != index) throw std::runtime_error("Translation reordered message markers");
        const auto begin = single ? 0 : markers[index].first + markers[index].second.length();
        auto text = single ? trim(translated) : trim(translated.substr(begin, (index + 1 < markers.size() ? markers[index + 1].first : translated.size()) - begin));
        const auto& source = job["items"][static_cast<Json::ArrayIndex>(index)];
        for (const auto& token : source["tokens"]) {
            const auto key = token["marker"].asString(); const auto position = text.find(key);
            if (position == std::string::npos || text.find(key, position + key.size()) != std::string::npos)
                throw std::runtime_error("Translation changed a protected token");
            text.replace(position, key.size(), token["source"].asString());
        }
        text = source["leading"].asString() + text + source["trailing"].asString();
        if (text.empty() || placeholders(text) != placeholders(source["source"].asString()) || text.find("ZXQ") != std::string::npos)
            throw std::runtime_error("Translation failed placeholder closure");
        result["strings"][source["name"].asString()] = text;
    }
    write(output, json(result));
}
void split_job(const fs::path& path, const fs::path& output) {
    const auto parent = parse(read(path));
    unsigned index = 0;
    for (const auto& item : parent["items"]) {
        auto child = parent; child["items"] = Json::arrayValue; child["items"].append(item);
        child["query"] = item["wire"]; child["single_message"] = true;
        std::ostringstream suffix; suffix << std::setw(6) << std::setfill('0') << index++;
        write(output / (suffix.str() + ".json"), json(child));
    }
}
void combine_job(const fs::path& job_path, const fs::path& input, const fs::path& output) {
    const auto parent = parse(read(job_path));
    Json::Value result(Json::objectValue); result["locale"] = parent["locale"];
    result["job_sha256"] = digest(read(job_path)); result["strings"] = Json::objectValue;
    result["single_message_response_sha256"] = Json::arrayValue;
    unsigned index = 0;
    for (const auto& item : parent["items"]) {
        std::ostringstream suffix; suffix << std::setw(6) << std::setfill('0') << index++;
        const auto child_path = input / (suffix.str() + ".json");
        const auto value = parse(read(input / suffix.str() / "validated.json"));
        const auto key = item["name"].asString();
        if (value["locale"] != parent["locale"] || value["job_sha256"] != digest(read(child_path)) ||
            value["strings"].size() != 1 || !value["strings"][key].isString() ||
            placeholders(value["strings"][key].asString()) != placeholders(item["source"].asString()))
            throw std::runtime_error("Single-message import does not match its parent");
        result["strings"][key] = value["strings"][key];
        result["single_message_response_sha256"].append(value["response_sha256"]);
    }
    write(output, json(result));
}
void write_keys(const fs::path& catalog_path, const fs::path& output) {
    ure_locale_host::generate_keys(catalog_path, output);
}
void materialize(const fs::path& component, const fs::path& catalog_path, const fs::path& results, const fs::path& output) {
    const auto catalog = parse(read(catalog_path));
    Document base_doc(component / "src/upstream/orangefox-android16/bootable/recovery/gui/theme/common/languages/en.xml");
    auto base = strings(base_doc);
    Strings by_text;
    const Strings extra = {{"en","Extra"},{"ar_SA","أدوات إضافية"},{"bg_BG","Допълнително"},
        {"bn_BD","অতিরিক্ত"},{"ca_ES","Extres"},{"cs_CZ","Doplňky"},{"de_DE","Extras"},{"el_GR","Πρόσθετα"},
        {"es-ES","Extras"},{"fa_IR","ابزارهای بیشتر"},{"fr_FR","Extras"},{"he_IL","תוספות"},{"hi_IN","अतिरिक्त"},
        {"hu","Extra"},{"id_ID","Ekstra"},{"it_IT","Extra"},{"ja_JP","追加機能"},{"ko_KR","추가 기능"},
        {"nl_NL","Extra"},{"no_NO","Ekstra"},{"pl_PL","Dodatki"},{"pt_BR","Extras"},{"pt_PT","Extras"},
        {"ro_RO","Extra"},{"ru","Дополнительно"},{"sr_Cyrl","Додатно"},{"th_TH","เพิ่มเติม"},{"tr_TR","Ekstra"},
        {"uk_UA","Додатково"},{"vi_VN","Bổ sung"},{"zh_CN","扩展"},{"zh_TW","擴充"}};
    base["ure_extra_tab"] = "Extra"; by_text["Extra"] = "ure_extra_tab";
    for (const auto& row : catalog["strings"]) {
        const auto key = row["name"].asString(), text = row["source"].asString();
        if (base.count(key) && base.at(key) != text) throw std::runtime_error("English key collision");
        base[key] = text; by_text[text] = key;
    }
    std::map<std::string, Strings> imported;
    Json::Value provenance(Json::objectValue); provenance["schema_version"] = 1;
    provenance["catalog_sha256"] = digest(read(catalog_path)); provenance["tool_source_sha256"] = digest(read(component / "src/localization/catalog.cpp"));
    provenance["generation"] = "Host-only machine-assisted translation; existing valid upstream wording retained";
    provenance["review"] = "Placeholder and key closure checked; native-speaker review remains pending";
    provenance["languages"] = Json::arrayValue;
    std::map<std::string, std::vector<std::string>> import_hashes;
    for (const auto& directory : fs::directory_iterator(results)) {
        if (!directory.is_directory() || !fs::exists(directory.path() / "validated.json")) continue;
        const auto value = parse(read(directory.path() / "validated.json"));
        const auto locale = value["locale"].asString();
        if (!value["strings"].isObject()) throw std::runtime_error("Invalid import strings");
        for (const auto& key : value["strings"].getMemberNames()) {
            if (imported[locale].count(key) && imported[locale].at(key) != value["strings"][key].asString())
                throw std::runtime_error("Conflicting imported translations");
            imported[locale][key] = value["strings"][key].asString();
        }
        import_hashes[locale].push_back(digest(read(directory.path() / "validated.json")));
    }
    for (const auto& language : catalog["languages"]) {
        const auto locale = language["locale"].asString();
        const auto source = component / language["input_path"].asString();
        if (digest(read(source)) != language["input_sha256"].asString()) throw std::runtime_error("Upstream language input changed");
        Document document(source); auto values = strings(document);
        values["ure_extra_tab"] = extra.at(locale);
        for (const auto& required : language["missing"]) if (!imported[locale].count(required["name"].asString()))
            throw std::runtime_error("Missing validated translation: " + locale + ": " + required["name"].asString());
        for (const auto& [key, text] : imported[locale]) values[key] = text;
        for (const auto& [key, text] : base) {
            if (locale == "en") values[key] = text;
            else if (!values.count(key) || values[key].empty() || placeholders(values[key]) != placeholders(text)) {
                if (human(text)) throw std::runtime_error("Incomplete language: " + locale + ": " + key);
                values[key] = text; // Empty/universal technical literals are not prose.
            }
            if (placeholders(values[key]) != placeholders(text)) throw std::runtime_error("Placeholder mismatch: " + locale + ": " + key);
        }
        xmlNodePtr resources = nullptr;
        visit(xmlDocGetRootElement(document.value), [&](auto* node) { if (name(node) == "resources") resources = node; });
        if (!resources) throw std::runtime_error("Missing language resources");
        for (auto* node = resources->children; node;) {
            auto* next = node->next;
            if (node->type == XML_ELEMENT_NODE && name(node) == "string") { xmlUnlinkNode(node); xmlFreeNode(node); }
            node = next;
        }
        for (const auto& [key, text] : values) {
            auto* node = xmlNewChild(resources, nullptr, BAD_CAST "string", nullptr);
            xmlNewProp(node, BAD_CAST "name", BAD_CAST key.c_str());
            xmlAddChild(node, xmlNewText(BAD_CAST text.c_str()));
        }
        document.save(output / "languages" / (locale + ".xml"));
        Json::Value row(Json::objectValue); row["locale"] = locale; row["upstream_sha256"] = language["input_sha256"];
        row["generated_sha256"] = digest(read(output / "languages" / (locale + ".xml")));
        row["keys"] = static_cast<Json::UInt64>(values.size()); row["added_or_repaired"] = static_cast<Json::UInt64>(imported[locale].size());
        auto hashes = import_hashes[locale]; std::sort(hashes.begin(), hashes.end());
        std::string identity; for (const auto& hash : hashes) identity += hash + "\n";
        row["validated_imports_sha256"] = digest(identity); provenance["languages"].append(row);
    }
    write_keys(catalog_path, output / "ure-locale-keys.hpp");
    Document maintainer(component / "src/device/xiaomi/uke/maintainer.xml");
    auto bind = [&](const std::string& text) { const auto found = by_text.find(text); return found == by_text.end() ? text : "{@" + found->second + "}"; };
    visit(xmlDocGetRootElement(maintainer.value), [&](auto* node) {
        if (name(node) == "text" && (!node->children || node->children->type == XML_TEXT_NODE)) {
            const auto original = content(node), bound = bind(original);
            if (bound != original) xmlNodeSetContent(node, BAD_CAST bound.c_str());
        }
        if (name(node) == "listitem") for (const auto* field : {"name", "description"}) {
            const auto original = attribute(node, field), bound = bind(original);
            if (bound != original) xmlSetProp(node, BAD_CAST field, BAD_CAST bound.c_str());
        }
    });
    maintainer.save(output / "maintainer.xml");
    write(output / "ORIGINS.json", json(provenance));
    std::cout << "Materialized complete, deduplicated language files and GUI bindings\n";
}
int main(int argc, char** argv) {
    try {
        if (argc == 4 && std::string(argv[1]) == "prepare") {
            const auto catalog = prepare(fs::canonical(argv[2]), fs::absolute(argv[3]));
            std::cout << "Catalogued " << catalog["strings"].size() << " GUI messages across "
                      << catalog["languages"].size() << " supported languages\n";
        } else if (argc == 4 && std::string(argv[1]) == "keys") write_keys(argv[2], argv[3]);
        else if (argc > 1 && (std::string(argv[1]) == "jobs" || std::string(argv[1]) == "split" ||
            std::string(argv[1]) == "combine" || std::string(argv[1]) == "materialize" || std::string(argv[1]) == "import"))
            throw std::runtime_error("Translation import/materialization is disabled pending provenance validation");
        else throw std::runtime_error("Usage: uke-locale-catalog prepare COMPONENT OUTPUT | keys CATALOG OUTPUT");
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
