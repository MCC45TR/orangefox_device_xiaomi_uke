// SPDX-License-Identifier: Apache-2.0
// Actual offline validator and CLI, using isolated source fixtures only.
#include "review-plan.hpp"
#include "catalog-source.hpp"
#include "key-catalog.hpp"
#include <csignal>
#include <fstream>
#include <functional>
#include <iostream>
#include <vector>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs=std::filesystem;
namespace {
unsigned refusals=0;
void check(bool condition,const char* message) { if(!condition)throw std::runtime_error(message); }
void refuse(const std::function<void()>& action) {
    try { action(); } catch(const std::exception&) { ++refusals; return; }
    throw std::runtime_error("Foreign translation was accepted after "+std::to_string(refusals)+" refusals");
}
void write(const fs::path& path,const std::string& bytes) {
    fs::create_directories(path.parent_path()); std::ofstream stream(path,std::ios::binary); stream<<bytes;
    check(static_cast<bool>(stream),"Cannot write private source fixture");
}
Json::Value response(const Json::Value& job) {
    Json::Value value(Json::objectValue); value["schema_version"]=1;
    value["job_sha256"]=digest(json(job)); value["locale"]=job["locale"]; value["target_locale"]=job["target_locale"];
    value["translations"]=Json::arrayValue;
    for(const auto& source:job["items"]) {
        Json::Value row(Json::objectValue);
        for(const auto* field:{"name","source_sha256","contexts_sha256"})row[field]=source[field];
        row["text"]=source["source"]; value["translations"].append(row);
    }
    return value;
}
int run(const std::vector<std::string>& arguments) {
    const auto pid=::fork(); check(pid>=0,"Cannot create offline CLI fixture process");
    if(pid==0) {
        std::vector<char*> pointers; for(const auto& argument:arguments)pointers.push_back(const_cast<char*>(argument.c_str()));
        pointers.push_back(nullptr); ::execv(pointers[0],pointers.data()); _exit(127);
    }
    int status=0; check(::waitpid(pid,&status,0)==pid,"Cannot collect offline CLI fixture");
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128;
}
}
int main(int argc,char** argv) {
    const auto work=fs::path(argc>1 ? argv[1] : ".")/("locale-review-"+std::to_string(getpid()));
    try {
        check((argc==4 || argc==5) && fs::create_directory(work),"Invalid review fixture arguments");
        const auto component=work/"component", catalog_path=work/"catalog.json", bundle_path=work/"bundle.json";
        const auto configuration=ure_locale_host::parse_strict(ure_locale_host::read_regular(fs::path(argv[3])/"configs/localization-inputs.json"));
        write(component/"configs/localization-inputs.json",json(configuration));
        for(const auto* file:{"catalog.cpp","catalog-source.hpp","key-catalog.cpp","key-catalog.hpp","review-plan.cpp","review-plan.hpp","review-main.cpp"})
            write(component/"src/localization"/file,ure_locale_host::read_regular(fs::path(argv[3])/"src/localization"/file));
        const std::string english="<language><resources><string name=\"ure_warning\">Erase %device% now</string></resources></language>";
        const auto language_dir=component/"src/upstream/orangefox-android16/bootable/recovery/gui/theme/common/languages";
        fs::create_directories(component/"src/upstream/orangefox-android16/bootable/recovery/gui/theme/extra-languages/languages");
        for(const auto& locale:configuration["language_codes"])
            write(language_dir/(locale.asString()+".xml"),locale=="en" ? english :
                "<language><resources><string name=\"ure_warning\"></string></resources></language>");
        const auto gui=component/"src/device/xiaomi/uke/ure-gui.cpp", native=component/"src/device/xiaomi/uke/recoveryctl/libuke/operation.cpp";
        write(gui,"const char* warning=\"Erase %device% now\";\n");
        write(native,"const char* keep=\"Keep %device% data\";\n");
        const auto maintainer=component/"src/device/xiaomi/uke/maintainer.xml";
        write(maintainer,"<recovery><pages><page name=\"erase\"><text>Erase %device% now</text></page><page name=\"backup\"><text>Keep %device% data</text></page></pages></recovery>");
        auto catalog=collect_catalog(component); write(catalog_path,json(catalog));
        check(ure_locale_host::current_catalog(component,catalog_path)==catalog,"Fresh fixture catalog did not bind source");
        const ure_locale_host::ReviewCatalog bound(catalog);
        const auto br=ure_locale_host::review_bundle(bound,"pt_BR"), pt=ure_locale_host::review_bundle(bound,"pt_PT");
        check(br["target_locale"]=="pt-BR" && pt["target_locale"]=="pt-PT" && br!=pt,"Regional Portuguese targets collapsed");
        const auto bundle=ure_locale_host::review_bundle(bound,"tr_TR");
        write(bundle_path,json(bundle));
        refuse([&] { static_cast<void>(ure_locale_host::review_bundle(bound,"en")); });
        refuse([&] { static_cast<void>(ure_locale_host::review_bundle(bound,"unknown")); });
        unsigned job_index=0; Json::ArrayIndex warning_index=0; bool found=false;
        for(Json::ArrayIndex j=0;j<bundle["jobs"].size();++j)for(Json::ArrayIndex i=0;i<bundle["jobs"][j]["items"].size();++i)
            if(bundle["jobs"][j]["items"][i]["name"]=="ure_warning") { job_index=j; warning_index=i; found=true; }
        check(found,"Destructive-warning source disappeared from fixture");
        auto raw=response(bundle["jobs"][job_index]);
        // Deliberately wrong meaning with exact original placeholder closure.
        raw["translations"][warning_index]["text"]="Preserve %device% forever";
        const auto accepted=ure_locale_host::import_review(bound,bundle,job_index,raw);
        check(accepted["validation"]["placeholder_closure"]==true && accepted["validation"]["competent_semantic_review"]==false &&
            accepted["validation"]["shipping_accepted"]==false,"Structural draft was mislabeled as semantic acceptance");
        refuse([&] { ure_locale_host::require_shipping_review(accepted); });
        auto claimed=accepted; claimed["validation"]["shipping_accepted"]=true; claimed["validation"]["competent_semantic_review"]=true;
        refuse([&] { ure_locale_host::require_shipping_review(claimed); });
        for(const auto* field:{"schema_version","job_sha256","locale","target_locale","translations"}) {
            auto bad=raw; bad.removeMember(field);
            refuse([&] { static_cast<void>(ure_locale_host::import_review(bound,bundle,job_index,bad)); });
        }
        for(const auto* field:{"job_sha256","locale","target_locale"}) {
            auto bad=raw; bad[field]="foreign";
            refuse([&] { static_cast<void>(ure_locale_host::import_review(bound,bundle,job_index,bad)); });
        }
        for(const auto* field:{"name","source_sha256","contexts_sha256"}) {
            auto bad=raw; bad["translations"][warning_index][field]="foreign";
            refuse([&] { static_cast<void>(ure_locale_host::import_review(bound,bundle,job_index,bad)); });
        }
        for(const auto& text:std::vector<std::string>{"No placeholder",std::string("bad\0text",8),std::string("\xff",1),std::string(65537,'x')}) {
            auto bad=raw; bad["translations"][warning_index]["text"]=text;
            refuse([&] { static_cast<void>(ure_locale_host::import_review(bound,bundle,job_index,bad)); });
        }
        auto bad=raw; bad["reviewed"]=true;
        refuse([&] { static_cast<void>(ure_locale_host::import_review(bound,bundle,job_index,bad)); });
        bad=raw; bad["translations"].append(raw["translations"][0]);
        refuse([&] { static_cast<void>(ure_locale_host::import_review(bound,bundle,job_index,bad)); });
        bad=raw; std::swap(bad["translations"][0],bad["translations"][1]);
        refuse([&] { static_cast<void>(ure_locale_host::import_review(bound,bundle,job_index,bad)); });
        auto changed_bundle=bundle; changed_bundle["jobs"][job_index]["items"][warning_index]["contexts"].append("foreign");
        refuse([&] { static_cast<void>(ure_locale_host::import_review(bound,changed_bundle,job_index,raw)); });
        refuse([&] { static_cast<void>(ure_locale_host::import_review(bound,br,job_index,raw)); });
        const auto split=ure_locale_host::review_children(bound,bundle,job_index);
        Json::Value combined(Json::objectValue); combined["schema_version"]=1;
        for(const auto* field:{"parent_job_sha256","locale","target_locale"})combined[field]=split[field];
        combined["children"]=Json::arrayValue;
        for(const auto& child:split["children"])combined["children"].append(response(child));
        check(ure_locale_host::combine_review(bound,bundle,job_index,combined)["translations"].size()==raw["translations"].size(),
            "Expected complete child source correspondence failed");
        auto foreign_split=ure_locale_host::review_children(bound,br,job_index);
        auto foreign=combined; foreign["children"][0]=response(foreign_split["children"][0]);
        refuse([&] { static_cast<void>(ure_locale_host::combine_review(bound,bundle,job_index,foreign)); });
        foreign=combined; foreign["parent_job_sha256"]="foreign";
        refuse([&] { static_cast<void>(ure_locale_host::combine_review(bound,bundle,job_index,foreign)); });
        foreign=combined; foreign["children"].append(combined["children"][0]);
        refuse([&] { static_cast<void>(ure_locale_host::combine_review(bound,bundle,job_index,foreign)); });
        foreign=combined; std::swap(foreign["children"][0],foreign["children"][1]);
        refuse([&] { static_cast<void>(ure_locale_host::combine_review(bound,bundle,job_index,foreign)); });
        for(const auto& path:std::vector<fs::path>{gui,native,maintainer,language_dir/"pt_PT.xml",component/"src/localization/review-plan.cpp"}) {
            const auto original=ure_locale_host::read_regular(path); write(path,original+"\n");
            refuse([&] { static_cast<void>(ure_locale_host::current_catalog(component,catalog_path)); });
            write(path,original);
        }
        write(component/"src/device/xiaomi/uke/recoveryctl/libuke/new.cpp","// New source member\n");
        refuse([&] { static_cast<void>(ure_locale_host::current_catalog(component,catalog_path)); });
        fs::remove(component/"src/device/xiaomi/uke/recoveryctl/libuke/new.cpp");
        // Verify real CLI publication preserves the old output on invalid work.
        const auto response_path=work/"response.json", output=work/"draft.json";
        write(response_path,json(raw));
        const std::vector<std::string> command={argv[2],"import",component.string(),catalog_path.string(),bundle_path.string(),std::to_string(job_index),response_path.string(),output.string()};
        check(run(command)==0 && ure_locale_host::parse_strict(ure_locale_host::read_regular(output))["raw_response_sha256"]==digest(json(raw)),
            "Real offline import did not bind raw response bytes");
        const auto original_output=ure_locale_host::read_regular(output);
        const auto cli_bundle=work/"cli-bundle.json", cli_children=work/"cli-children.json", combined_path=work/"children.json";
        check(run({argv[2],"jobs",component.string(),catalog_path.string(),"tr_TR",cli_bundle.string()})==0 &&
            ure_locale_host::parse_strict(ure_locale_host::read_regular(cli_bundle))==bundle,"Real CLI did not regenerate expected locale jobs");
        write(work/"expected-children.json",json(split));
        check(run({argv[2],"split",component.string(),catalog_path.string(),bundle_path.string(),std::to_string(job_index),cli_children.string()})==0 &&
            json(ure_locale_host::parse_strict(ure_locale_host::read_regular(cli_children)))==json(split),"Real CLI did not bind complete child jobs");
        write(combined_path,json(combined));
        check(run({argv[2],"combine",component.string(),catalog_path.string(),bundle_path.string(),std::to_string(job_index),combined_path.string(),output.string()})==0,
            "Real CLI did not rebind original child responses");
        // Restore the original import draft for subsequent preservation checks.
        write(output,original_output);
        auto alias=command; alias.back()=response_path.string();
        check(run(alias)!=0 && ure_locale_host::read_regular(response_path)==json(raw),"CLI replaced its own response input");
        alias.back()=gui.string();
        const auto gui_before=ure_locale_host::read_regular(gui);
        check(run(alias)!=0 && ure_locale_host::read_regular(gui)==gui_before,"CLI replaced current native source");
        bad=raw; bad["locale"]="pt_BR"; write(response_path,json(bad));
        check(run(command)!=0 && ure_locale_host::read_regular(output)==original_output,"Foreign CLI import replaced prior output");
        check(run({argv[2],"materialize",output.string()})!=0 && ure_locale_host::read_regular(output)==original_output,
            "Unreviewed warning was allowed into materialization");
        const auto fifo=work/"response-fifo"; check(::mkfifo(fifo.c_str(),0600)==0,"Cannot create private FIFO negative");
        refuse([&] { static_cast<void>(ure_locale_host::read_regular(fifo)); });
        fs::create_symlink(response_path,work/"linked-response");
        refuse([&] { static_cast<void>(ure_locale_host::read_regular(work/"linked-response")); });
        refuse([&] { static_cast<void>(ure_locale_host::parse_strict("{\"x\":1,\"x\":2}")); });
        refuse([&] { static_cast<void>(ure_locale_host::parse_strict("{ /* comment */ \"x\":1 }")); });
        if(argc==5) {
            const auto full=ure_locale_host::current_catalog(argv[3],argv[4]);
            const ure_locale_host::ReviewCatalog full_bound(full);
            unsigned locales=0,jobs=0,items=0;
            for(const auto& locale:full["languages"])if(locale["locale"]!="en") {
                const auto actual=ure_locale_host::review_bundle(full_bound,locale["locale"].asString());
                const auto decoded=ure_locale_host::parse_strict(json(actual));
                ure_locale_host::check_bundle(full_bound,decoded);
                ++locales; jobs+=actual["jobs"].size();
                for(const auto& job:actual["jobs"])items+=job["items"].size();
            }
            check(locales==31,"Current non-English locale inventory is incomplete");
            std::cout<<"Current full catalog: "<<full["strings"].size()<<" source rows, "<<locales<<" offline locale bundles, "<<jobs<<" jobs, "<<items<<" source items; no provider request or wording acceptance.\n";
        }
        fs::remove_all(work);
        std::cout<<"Offline current-source review: exact jobs, source/context edits and new members, complete parent/child, pt_BR/pt_PT, wrong-meaning shipping refusal, preserved publication; "<<refusals<<" real refusals. No competent wording or tablet acceptance.\n";
    } catch(const std::exception& error) { std::cerr<<error.what()<<"\nPrivate review fixture retained at "<<work<<'\n'; return 1; }
}
