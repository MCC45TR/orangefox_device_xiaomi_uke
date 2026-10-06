#pragma once
#include <unistd.h>
namespace android::base {
class unique_fd {
    int value_;
public:
    explicit unique_fd(int value):value_(value) {}
    ~unique_fd() { if(value_>=0) ::close(value_); }
    unique_fd(const unique_fd&)=delete;
    unique_fd& operator=(const unique_fd&)=delete;
    int get() const { return value_; }
    operator int() const { return value_; }
};
}
