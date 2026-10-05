// Host-only offline translation provenance. Drafts cannot authorize shipping.
#pragma once
#include <json/json.h>
#include <filesystem>
#include <string>

namespace ure_locale_host {
class ReviewCatalog {
    Json::Value value_;
    std::string digest_;
public:
    explicit ReviewCatalog(Json::Value value);
    const Json::Value& value() const { return value_; }
    const std::string& sha256() const { return digest_; }
};
Json::Value current_catalog(const std::filesystem::path& component,const std::filesystem::path& catalog);
Json::Value review_bundle(const ReviewCatalog& catalog,const std::string& locale);
void check_bundle(const ReviewCatalog& catalog,const Json::Value& bundle);
Json::Value review_children(const ReviewCatalog& catalog,const Json::Value& bundle,unsigned index);
Json::Value import_review(const ReviewCatalog& catalog,const Json::Value& bundle,unsigned index,const Json::Value& response);
Json::Value combine_review(const ReviewCatalog& catalog,const Json::Value& bundle,unsigned index,const Json::Value& responses);
void require_shipping_review(const Json::Value& draft);
}
