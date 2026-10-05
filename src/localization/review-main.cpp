// SPDX-License-Identifier: Apache-2.0
// Offline-only validation; no fetcher, provider response parser or tablet tool.
#include "review-plan.hpp"
#include "catalog-source.hpp"
#include "key-catalog.hpp"
#include <charconv>
#include <iostream>
#include <stdexcept>
#include <string_view>

int main(int argc,char** argv) {
    try {
        if(argc>1 && std::string_view(argv[1])=="materialize")ure_locale_host::require_shipping_review(Json::Value());
        if(argc<6)throw std::runtime_error("Usage: uke-locale-review jobs COMPONENT CATALOG LOCALE OUTPUT | split COMPONENT CATALOG BUNDLE INDEX OUTPUT | import/combine COMPONENT CATALOG BUNDLE INDEX RESPONSE OUTPUT");
        const auto catalog=ure_locale_host::current_catalog(argv[2],argv[3]);
        const ure_locale_host::ReviewCatalog bound(catalog);
        Json::Value output;
        const auto command=std::string_view(argv[1]);
        if(command=="jobs" && argc==6) {
            output=ure_locale_host::review_bundle(bound,argv[4]);
        } else if((command=="split" && argc==7) || ((command=="import" || command=="combine") && argc==8)) {
            const auto bundle=ure_locale_host::parse_strict(ure_locale_host::read_regular(argv[4]));
            const auto text=std::string_view(argv[5]); unsigned index=0;
            const auto result=std::from_chars(text.data(),text.data()+text.size(),index);
            if(text.empty() || result.ec!=std::errc() || result.ptr!=text.data()+text.size())throw std::runtime_error("Invalid translation index");
            if(command=="split")output=ure_locale_host::review_children(bound,bundle,index);
            else {
                const auto response_bytes=ure_locale_host::read_regular(argv[6]);
                const auto response=ure_locale_host::parse_strict(response_bytes);
                output=command=="import" ? ure_locale_host::import_review(bound,bundle,index,response) :
                    ure_locale_host::combine_review(bound,bundle,index,response);
                output["raw_response_sha256"]=digest(response_bytes);
            }
        } else throw std::runtime_error("Unknown offline translation operation or argument count");
        // Rebind once more before publication, including catalog and tool bytes.
        if(catalog!=ure_locale_host::current_catalog(argv[2],argv[3]))throw std::runtime_error("Translation source changed during validation");
        const auto destination=std::filesystem::absolute(argv[argc-1]).lexically_normal();
        for(const auto& source:catalog["source_inputs"].getMemberNames())
            if(destination==(std::filesystem::canonical(argv[2])/source).lexically_normal())
                throw std::runtime_error("Translation output must not replace a source input");
        for(const auto argument:{3,4,6})if(argument<argc-1 && (argument!=4 || command!="jobs")) {
            std::error_code error;
            if(std::filesystem::equivalent(destination,argv[argument],error) && !error)
                throw std::runtime_error("Translation output must not replace its catalog, bundle or response input");
        }
        ure_locale_host::publish_regular(argv[argc-1],json(output));
        std::cout<<"Current-source offline translation work validated; wording and shipping remain unreviewed.\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
