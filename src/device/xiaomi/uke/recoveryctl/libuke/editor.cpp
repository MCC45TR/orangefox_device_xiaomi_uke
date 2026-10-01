// SPDX-License-Identifier: Apache-2.0
#include "uke.h"
#include <algorithm>

namespace ure {
bool utf8(std::string_view text) {
    std::size_t i=0;
    while(i<text.size()) {
        const auto c=static_cast<unsigned char>(text[i++]);
        if(c<0x80) { if(c==0)return false; continue; }
        unsigned n=0; std::uint32_t value=0, minimum=0;
        if(c>=0xc2 && c<=0xdf) { n=1; value=c&31U; minimum=0x80; }
        else if(c>=0xe0 && c<=0xef) { n=2; value=c&15U; minimum=0x800; }
        else if(c>=0xf0 && c<=0xf4) { n=3; value=c&7U; minimum=0x10000; }
        else return false;
        if(i+n>text.size())return false;
        for(unsigned k=0;k<n;++k) { const auto b=static_cast<unsigned char>(text[i++]); if((b&0xc0U)!=0x80U)return false; value=(value<<6)|(b&63U); }
        if(value<minimum || value>0x10ffff || (value>=0xd800 && value<=0xdfff))return false;
    }
    return true;
}
Editor::Editor(const Root& root, std::string path, std::string profile):path_(std::move(path)),profile_(std::move(profile)) {
    original_=root.read(path_); require(utf8(original_),"binary-file","Editor accepts valid UTF-8 text only");
    text_=original_; original_identity_=transaction_plan(root,path_,original_,profile_)["source_state"];
    require(original_identity_["sha256"]==sha256(original_),"stale-source","File changed while loading the editor");
}
void Editor::change(const std::string& value) {
    require(value.size()<=1024*1024 && utf8(value),"invalid-text","Editor buffer exceeds limits or is not UTF-8");
    if(text_==value)return;
    undo_.push_back(text_); std::size_t bytes=0;
    for(const auto& item:undo_)bytes+=item.size();
    while(undo_.size()>32 || bytes>4*1024*1024) { bytes-=undo_.front().size(); undo_.erase(undo_.begin()); }
    redo_.clear(); text_=value;
}
std::vector<std::string> Editor::lines() const {
    std::vector<std::string> result; std::size_t begin=0;
    do { const auto end=text_.find('\n',begin); result.push_back(text_.substr(begin,end==text_.npos ? end : end-begin)); if(end==text_.npos)break; begin=end+1; } while(begin<=text_.size());
    return result;
}
static std::string join(const std::vector<std::string>& lines) {
    std::string value;
    for(std::size_t i=0;i<lines.size();++i) { if(i)value+='\n'; value+=lines[i]; } return value;
}
void Editor::line(std::size_t index,const std::string& value) {
    auto rows=lines(); require(index<rows.size() && value.find('\n')==value.npos,"invalid-line","Line index or content is invalid"); rows[index]=value; change(join(rows));
}
void Editor::insert(std::size_t index,const std::string& value) {
    auto rows=lines(); require(index<=rows.size() && value.find('\n')==value.npos,"invalid-line","Insertion index or content is invalid"); rows.insert(rows.begin()+static_cast<std::ptrdiff_t>(index),value); change(join(rows));
}
void Editor::erase(std::size_t index) {
    auto rows=lines(); require(index<rows.size(),"invalid-line","Deletion index is invalid"); rows.erase(rows.begin()+static_cast<std::ptrdiff_t>(index)); change(join(rows));
}
void Editor::undo() { require(!undo_.empty(),"history-empty","Undo history is empty"); redo_.push_back(text_); text_=undo_.back(); undo_.pop_back(); }
void Editor::redo() { require(!redo_.empty(),"history-empty","Redo history is empty"); undo_.push_back(text_); text_=redo_.back(); redo_.pop_back(); }
std::size_t Editor::replace(const std::string& find,const std::string& replacement) {
    require(!find.empty() && utf8(find) && utf8(replacement),"invalid-search","Replacement requires nonempty valid UTF-8 search text");
    std::string value=text_; std::size_t count=0, pos=0;
    while((pos=value.find(find,pos))!=value.npos) { value.replace(pos,find.size(),replacement); pos+=replacement.size(); ++count; require(value.size()<=1024*1024,"size-limit","Replacement exceeds editor capacity"); }
    change(value); return count;
}
Value Editor::plan(const Root& root) const {
    auto result=transaction_plan(root,path_,text_,profile_);
    require(json(result["source_state"])==json(original_identity_),"stale-source","File changed outside the editor; reload before saving"); return result;
}
} // namespace ure
