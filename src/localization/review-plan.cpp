// SPDX-License-Identifier: Apache-2.0
// Offline translation jobs rebound to current source/context, never self claims.
#include "review-plan.hpp"
#include "catalog-source.hpp"
#include "key-catalog.hpp"
#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <stdexcept>
#include <string_view>

namespace ure_locale_host {
namespace {
void require(bool condition,const char* message) { if(!condition)throw std::runtime_error(message); }
bool hash(const Json::Value& value) {
    if(!value.isString() || value.asString().size()!=64)return false;
    const auto text=value.asString();
    return std::all_of(text.begin(),text.end(),[](char c) { return (c>='0' && c<='9') || (c>='a' && c<='f'); });
}
void fields(const Json::Value& value,std::initializer_list<const char*> expected) {
    require(value.isObject(),"Expected an object in offline translation work");
    std::set<std::string> keys;
    for(const auto* key:expected)keys.insert(key);
    const auto names=value.getMemberNames();
    require(std::set<std::string>(names.begin(),names.end())==keys,"Unexpected or missing translation field");
}
Json::Value read_json(const std::filesystem::path& path) { return parse_strict(read_regular(path)); }
Json::Value identity(const ReviewCatalog& catalog,const std::string& locale,const std::string& target) {
    Json::Value value(Json::objectValue);
    value["schema_version"]=1; value["catalog_sha256"]=catalog.sha256();
    value["source_inputs_sha256"]=catalog.value()["source_inputs_sha256"];
    value["locale"]=locale; value["target_locale"]=target;
    return value;
}
Json::Value item(const Json::Value& row) {
    require(row.isObject() && row["name"].isString() && row["source"].isString() && row["contexts"].isArray(),
        "Invalid source/context row");
    require(!row["source"].asString().empty() && row["source"].asString().size()<=65536 && valid_utf8(row["source"].asString()),
        "Invalid source bytes");
    std::set<std::string> contexts;
    require(row["contexts"].size()>0 && row["contexts"].size()<=256,"Invalid context count");
    for(const auto& context:row["contexts"]) {
        require(context.isString() && !context.asString().empty() && context.asString().size()<=4096 && valid_utf8(context.asString()),
            "Invalid context bytes");
        contexts.insert(context.asString());
    }
    Json::Value value(Json::objectValue); value["name"]=row["name"]; value["source"]=row["source"];
    value["contexts"]=Json::arrayValue;
    for(const auto& context:contexts)value["contexts"].append(context);
    value["source_sha256"]=digest(value["source"].asString());
    value["contexts_sha256"]=digest(json(value["contexts"]));
    return value;
}
Json::Value parent(const ReviewCatalog& catalog,const Json::Value& bundle,unsigned index) {
    check_bundle(catalog,bundle);
    require(index<bundle["jobs"].size(),"Translation job index is outside the current bundle");
    return bundle["jobs"][index];
}
Json::Value children(const Json::Value& job) {
    Json::Value value=job;
    value.removeMember("items"); value.removeMember("job_index");
    value["parent_job_sha256"]=digest(json(job)); value["parent_job_index"]=job["job_index"];
    value["children"]=Json::arrayValue;
    for(Json::ArrayIndex index=0;index<job["items"].size();++index) {
        Json::Value child=job;
        child.removeMember("job_index"); child.removeMember("items");
        child["parent_job_sha256"]=value["parent_job_sha256"]; child["parent_job_index"]=job["job_index"];
        child["item_index"]=index; child["items"]=Json::arrayValue; child["items"].append(job["items"][index]);
        value["children"].append(child);
    }
    return value;
}
Json::Value validate_response(const Json::Value& expected,const Json::Value& response) {
    fields(response,{"schema_version","job_sha256","locale","target_locale","translations"});
    require(response["schema_version"].isUInt() && response["schema_version"].asUInt()==1 &&
        response["job_sha256"]==digest(json(expected)) && response["locale"]==expected["locale"] &&
        response["target_locale"]==expected["target_locale"],"Response does not match the current source job and exact locale");
    require(response["translations"].isArray() && response["translations"].size()==expected["items"].size(),
        "Unexpected translation count");
    Json::Value translations(Json::arrayValue);
    std::size_t total=0;
    for(Json::ArrayIndex index=0;index<expected["items"].size();++index) {
        const auto& source=expected["items"][index]; const auto& translated=response["translations"][index];
        fields(translated,{"name","source_sha256","contexts_sha256","text"});
        require(translated["name"]==source["name"] && translated["source_sha256"]==source["source_sha256"] &&
            translated["contexts_sha256"]==source["contexts_sha256"],"Translation item has stale, foreign or reordered source/context");
        require(translated["text"].isString(),"Translation text must be a string");
        const auto text=translated["text"].asString();
        require(!text.empty() && text.size()<=65536 && valid_utf8(text) && total+text.size()<=262144,
            "Translation exceeds text/UTF-8 budget");
        total+=text.size();
        require(placeholders(text)==placeholders(source["source"].asString()),"Translation changed a native placeholder");
        auto value=source; value["translation"]=text; value["translation_sha256"]=digest(text);
        translations.append(value);
    }
    return translations;
}
Json::Value draft(const Json::Value& job,const Json::Value& translations,const Json::Value& response) {
    Json::Value value=job; value.removeMember("items");
    value["job_sha256"]=digest(json(job)); value["response_sha256"]=digest(json(response));
    value["translations"]=translations;
    value["status"]="unreviewed-draft";
    value["validation"]["current_source_context_parent_and_exact_locale"]=true;
    value["validation"]["placeholder_closure"]=true;
    value["validation"]["language_meaning"]=false;
    value["validation"]["competent_semantic_review"]=false;
    value["validation"]["shipping_accepted"]=false;
    return value;
}
}
Json::Value current_catalog(const std::filesystem::path& component,const std::filesystem::path& path) {
    const auto supplied=read_json(path);
    const auto current=collect_catalog(std::filesystem::canonical(component));
    require(supplied==current,"Catalog no longer matches current source, context, tool or language inputs");
    static_cast<void>(validate_keys(current));
    return current;
}
ReviewCatalog::ReviewCatalog(Json::Value value):value_(std::move(value)) {
    const auto& catalog=value_;
    require(catalog.isObject() && catalog["schema_version"].isUInt() && catalog["schema_version"].asUInt()==1 &&
        catalog["source_inputs"].isObject() && hash(catalog["source_inputs_sha256"]) &&
        catalog["source_inputs_sha256"]==digest(json(catalog["source_inputs"])) && catalog["languages"].isArray(),
        "Invalid current source catalog identity");
    static_cast<void>(validate_keys(catalog));
    require(catalog["languages"].size()==32,"Incomplete supported-locale catalog");
    std::set<std::string> locales;
    for(const auto& language:catalog["languages"]) {
        require(language.isObject() && language["locale"].isString() && language["target"].isString() &&
            language["missing"].isArray() && language["missing"].size()<=16384 &&
            locales.insert(language["locale"].asString()).second,"Invalid supported-locale row");
    }
    digest_=digest(json(catalog));
}
Json::Value review_bundle(const ReviewCatalog& bound,const std::string& locale) {
    const auto& catalog=bound.value();
    const Json::Value* language=nullptr;
    for(const auto& candidate:catalog["languages"])if(candidate["locale"]==locale) {
        require(language==nullptr,"Duplicate current language"); language=&candidate;
    }
    require(language!=nullptr && locale!="en" && (*language)["target"].isString() && (*language)["missing"].isArray() &&
        (*language)["missing"].size()<=16384,"Unknown language or invalid missing-source inventory");
    Json::Value value=identity(bound,locale,(*language)["target"].asString()); value["jobs"]=Json::arrayValue;
    std::map<std::string,Json::Value> source_rows;
    for(const auto& row:catalog["strings"])source_rows.emplace(row["name"].asString(),row);
    std::map<std::string,Json::Value> messages;
    for(const auto& row:(*language)["missing"]) {
        auto expanded=row;
        if(row.isMember("catalog_source")) {
            fields(row,{"name","catalog_source"});
            require(row["catalog_source"]==true && row["name"].isString() && source_rows.count(row["name"].asString()),
                "Invalid compact source/context reference");
            expanded=source_rows.at(row["name"].asString());
        }
        auto source=item(expanded); const auto key=source["name"].asString();
        require(!key.empty() && key.size()<=128,"Invalid offline translation key");
        const auto existing=messages.find(key);
        if(existing==messages.end())messages.emplace(key,std::move(source));
        else {
            require(existing->second["source"]==source["source"],"A missing key maps to conflicting source bytes");
            std::set<std::string> contexts;
            for(const auto& context:existing->second["contexts"])contexts.insert(context.asString());
            for(const auto& context:source["contexts"])contexts.insert(context.asString());
            auto& merged=existing->second; merged["contexts"]=Json::arrayValue;
            for(const auto& context:contexts)merged["contexts"].append(context);
            require(merged["contexts"].size()<=256,"Merged context budget exceeded");
            merged["contexts_sha256"]=digest(json(merged["contexts"]));
        }
    }
    Json::Value job=identity(bound,locale,(*language)["target"].asString());
    job["job_index"]=0; job["items"]=Json::arrayValue;
    std::size_t bytes=0;
    const auto flush=[&] {
        if(job["items"].empty())return;
        value["jobs"].append(job); job["job_index"]=value["jobs"].size(); job["items"]=Json::arrayValue; bytes=0;
    };
    for(const auto& [key,source]:messages) {
        static_cast<void>(key);
        const auto item_bytes=json(source).size();
        require(item_bytes<=262144,"Single translation source exceeds job budget");
        if(job["items"].size()>=32 || bytes+item_bytes>262144)flush();
        job["items"].append(source); bytes+=item_bytes;
    }
    flush();
    require(value["jobs"].size()<=2048 && json(value).size()<=32U*1024U*1024U,"Offline locale bundle exceeds budget");
    return value;
}
void check_bundle(const ReviewCatalog& catalog,const Json::Value& bundle) {
    require(bundle.isObject() && bundle["locale"].isString(),"Invalid offline translation bundle");
    // JsonCpp stores generated array indexes as unsigned but decodes the same
    // wire integers as signed. Compare canonical bytes, retaining all fields.
    require(json(bundle)==json(review_bundle(catalog,bundle["locale"].asString())),
        "Bundle differs from regenerated current source/context/locale jobs");
}
Json::Value review_children(const ReviewCatalog& catalog,const Json::Value& bundle,unsigned index) {
    return children(parent(catalog,bundle,index));
}
Json::Value import_review(const ReviewCatalog& catalog,const Json::Value& bundle,unsigned index,const Json::Value& response) {
    const auto job=parent(catalog,bundle,index);
    return draft(job,validate_response(job,response),response);
}
Json::Value combine_review(const ReviewCatalog& catalog,const Json::Value& bundle,unsigned index,const Json::Value& response) {
    const auto job=parent(catalog,bundle,index), expected=children(job);
    fields(response,{"schema_version","parent_job_sha256","locale","target_locale","children"});
    require(response["schema_version"].isUInt() && response["schema_version"].asUInt()==1 &&
        response["parent_job_sha256"]==expected["parent_job_sha256"] && response["locale"]==expected["locale"] &&
        response["target_locale"]==expected["target_locale"] && response["children"].isArray() &&
        response["children"].size()==expected["children"].size(),"Child set does not match the complete current parent");
    Json::Value translations(Json::arrayValue);
    for(Json::ArrayIndex child=0;child<expected["children"].size();++child)
        translations.append(validate_response(expected["children"][child],response["children"][child])[0]);
    return draft(job,translations,response);
}
void require_shipping_review(const Json::Value& draft_value) {
    static_cast<void>(draft_value);
    // There is no configured trusted competent-review ledger. A response or
    // receipt's own reviewed=true claim cannot create that authority.
    throw std::runtime_error("Translation shipping is disabled: no trusted competent semantic review is configured");
}
}
