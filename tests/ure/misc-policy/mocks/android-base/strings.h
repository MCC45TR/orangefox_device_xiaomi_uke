#pragma once
#include <string>
#include <vector>
namespace android::base {
inline std::vector<std::string> Split(const std::string& input,const std::string& separators) {
    std::vector<std::string> result; std::size_t begin=0;
    for(;;) { const auto end=input.find_first_of(separators,begin); result.push_back(input.substr(begin,end-begin)); if(end==std::string::npos) return result; begin=end+1; }
}
}
