#pragma once
#include <string>
namespace android::hardware {
template<class T> class Return {
    T value_;
public:
    Return(T value):value_(value) {}
    operator T() const { return value_; }
};
template<> class Return<void> { public: Return()=default; };
inline Return<void> Void() { return {}; }
using hidl_string=std::string;
}
