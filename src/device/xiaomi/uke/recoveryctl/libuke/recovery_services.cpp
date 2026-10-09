// SPDX-License-Identifier: Apache-2.0
#include "recovery_services.hpp"
#include <algorithm>
#include <charconv>
#include <cerrno>
#include <cmath>
#include <chrono>
#include <fcntl.h>
#include <limits>
#include <linux/magic.h>
#include <openssl/crypto.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/vfs.h>
#include <unistd.h>

namespace ure {
namespace {
class Credential {
    std::array<unsigned char,4097> bytes_{};
    std::size_t used_=0;
    bool locked_=false;
public:
    Credential() { locked_=::mlock(bytes_.data(),bytes_.size())==0; require(locked_,"credential-memory-unavailable","Cannot retain credentials in locked memory"); }
    ~Credential() { OPENSSL_cleanse(bytes_.data(),bytes_.size()); if(locked_)::munlock(bytes_.data(),bytes_.size()); }
    Credential(const Credential&)=delete;
    Credential& operator=(const Credential&)=delete;
    void read(int fd) {
        Fd retained(fd<0 ? -1 : ::fcntl(fd,F_DUPFD_CLOEXEC,3)); fd=retained.get();
        struct stat info{};
        require(fd>=0 && ::fstat(fd,&info)==0 && ((S_ISFIFO(info.st_mode) && info.st_uid==::geteuid() && (info.st_mode&07777)==0600) ||
            (S_ISREG(info.st_mode) && info.st_nlink==0 && info.st_uid==::geteuid() && (info.st_mode&07777)==0600 && info.st_size>0 && info.st_size<=4096)),
            "unsafe-credential-source","Use a private anonymous descriptor or bounded pipe; never a persistent password file");
        if(S_ISREG(info.st_mode)) {
            constexpr int seals=F_SEAL_WRITE|F_SEAL_GROW|F_SEAL_SHRINK|F_SEAL_SEAL;
            const int actual=::fcntl(fd,F_GET_SEALS);
            require(actual>=0 && (actual&seals)==seals,"unsafe-credential-source","Anonymous credential must be sealed before use");
            // pread avoids the caller's shared descriptor offset. Only a full
            // immutable credential is accepted, including embedded zero bytes.
            while(used_<static_cast<std::size_t>(info.st_size)) {
                const auto count=::pread(fd,bytes_.data()+used_,static_cast<std::size_t>(info.st_size)-used_,static_cast<off_t>(used_));
                if(count<0 && errno==EINTR)continue;
                require(count>0,"credential-read-failed","Cannot receive a complete anonymous credential");
                used_+=static_cast<std::size_t>(count);
            }
            return;
        } else {
            struct statfs filesystem{};
            require(::fstatfs(fd,&filesystem)==0 && static_cast<unsigned long>(filesystem.f_type)==PIPEFS_MAGIC,
                "unsafe-credential-source","Credential transport must be an anonymous kernel pipe, not a named FIFO");
            const int flags=::fcntl(fd,F_GETFL);
            require(flags>=0 && (flags&O_NONBLOCK)!=0 && (flags&O_ACCMODE)!=O_WRONLY,"unsafe-credential-source","Anonymous credential pipes must use nonblocking reads");
        }
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        while(std::chrono::steady_clock::now()<deadline) {
            pollfd descriptor{fd,POLLIN|POLLHUP,0}; const auto ready=::poll(&descriptor,1,50);
            if(ready<0 && errno==EINTR)continue;
            require(ready>=0 && !(descriptor.revents&(POLLERR|POLLNVAL)),"credential-read-failed","Cannot receive a credential");
            if(!ready)continue;
            const auto count=::read(fd,bytes_.data()+used_,bytes_.size()-used_);
            if(count<0 && (errno==EINTR || errno==EAGAIN))continue;
            require(count>=0,"credential-read-failed","Cannot receive a credential");
            if(count==0) { require(used_>0,"empty-credential","A nonempty credential is required"); return; }
            used_+=static_cast<std::size_t>(count);
            require(used_<=4096,"credential-too-large","Credential exceeds the bounded input limit");
        }
        throw Error("credential-timeout","Credential input was not completed within its deadline");
    }
    std::span<const unsigned char> bytes() const { return {bytes_.data(),used_}; }
};
}
Value recovery_services_status(const Root& system) {
    Value out; out["schema"]=1; out["read_only"]=true; out["physical_test_record"]=false;
    out["android_fbe"]["unlock_available"]=false;
    out["android_fbe"]["reason"]="Installed KeyMint/TEE, metadata-key and fscrypt adapters are not accepted; credentials are not requested";
    out["linux_luks"]["tool_packaged"]=tool_available("cryptsetup"); out["linux_luks"]["unlock_available"]=false;
    out["linux_luks"]["reason"]="Read-only LUKS mapping and retained owner/cleanup adapter need VM and device acceptance";
    out["decryption_session"]["secret_transport"]="anonymous-fd-or-pipe";
    out["decryption_session"]["secrets_in_argv_or_json"]=false;
    out["decryption_session"]["automatic_retry"]=false;
    auto& temperatures=out["temperatures"]; temperatures=Value(Json::arrayValue);
    try {
        for(const auto& name:system.list("sys/class/thermal",128)) {
            if(!name.starts_with("thermal_zone") || name.size()>32)continue;
            const auto path="sys/class/thermal/"+name;
            Value row; row["name"]=name;
            try {
                auto directory=system.open_resolved(path,O_RDONLY|O_DIRECTORY); Root zone(std::move(directory));
                auto type=zone.read("type",128),text=zone.read("temp",64);
                while(!type.empty() && (type.back()=='\n' || type.back()=='\r'))type.pop_back();
                while(!text.empty() && (text.back()=='\n' || text.back()=='\r'))text.pop_back();
                int value=0; const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
                require(!text.empty() && parsed.ec==std::errc{} && parsed.ptr==text.data()+text.size(),"invalid-temperature","Invalid thermal driver value");
                require(!type.empty() && std::all_of(type.begin(),type.end(),[](unsigned char c){ return c>=32 && c<=126; }),"invalid-temperature-type","Invalid thermal type");
                row["type"]=type; row["raw_value"]=value; row["available"]=true;
                // Qualcomm BCL and other proxy zones can expose current or
                // voltage through thermal sysfs. Do not infer a Celsius unit
                // from the magnitude of an unreviewed driver reading.
                const bool cpu=type=="cpu_therm" || (type.starts_with("cpuss-") && type.size()>6 &&
                    std::all_of(type.begin()+6,type.end(),[](char c){ return c>='0' && c<='9'; }));
                row["unit"]=cpu ? "millidegrees-celsius" : "driver-defined-unverified";
                if(cpu && value>=-100000 && value<=250000)row["millidegrees_celsius"]=value;
                else if(cpu)row["available"]=false;
            } catch(const Error&) { row["available"]=false; }
            temperatures.append(row);
        }
    } catch(const Error&) { out["thermal_inventory_available"]=false; }
    if(!out.isMember("thermal_inventory_available"))out["thermal_inventory_available"]=true;
    out["automatic_display"]["calibrated_sensor_provider_available"]=false;
    out["automatic_display"]["render_thread_adapter_available"]=false;
    out["automatic_display"]["reason"]="Reviewed SSC/HAL stream, natural-display orientation and panel actuator need acceptance";
    out["automatic_display"]["controller_implemented"]=true;
    out["automatic_display"]["automatic_enable"]=false;
    return out;
}
void AutomaticDisplayController::configure(bool brightness,bool rotation,std::uint64_t generation) {
    *this=AutomaticDisplayController{}; brightness_=brightness; rotation_=rotation; generation_=generation;
}
void AutomaticDisplayController::manual_override(std::uint64_t now) {
    manual_until_=now>UINT64_MAX-5000 ? UINT64_MAX : now+5000; candidate_=-1; have_lux_=false;
}
AutomaticDisplayDecision AutomaticDisplayController::update(const SensorFrame& frame,std::uint64_t now) {
    AutomaticDisplayDecision result;
    if(!generation_ || frame.provider_generation!=generation_) { candidate_=-1; have_lux_=false; return result; }
    if(frame.sample_ms>now || now-frame.sample_ms>2000 || frame.sequence<=last_sequence_ || (last_sequence_ && frame.sample_ms<=last_sample_)) {
        candidate_=-1; have_lux_=false; result.reason="stale-or-invalid-sensor-frame"; return result;
    }
    if(last_sequence_ && frame.sample_ms-last_sample_>2000) { candidate_=-1; have_lux_=false; }
    last_sequence_=frame.sequence; last_sample_=frame.sample_ms;
    if(now<manual_until_) { result.reason="manual-override"; return result; }
    result.reason="waiting-for-stable-sample";
    if(brightness_ && frame.lux && std::isfinite(*frame.lux) && *frame.lux>=0 && *frame.lux<=1000000) {
        filtered_lux_=have_lux_ ? filtered_lux_*0.8+*frame.lux*0.2 : *frame.lux; have_lux_=true;
        const auto level=std::clamp(static_cast<int>(10.0+std::log2(1.0+filtered_lux_)*6.0),10,95);
        if((applied_brightness_<0 || std::abs(level-applied_brightness_)>=4) && now>=last_brightness_ && now-last_brightness_>=1000) {
            result.brightness_percent=level; applied_brightness_=level; last_brightness_=now;
        }
    } else if(brightness_)have_lux_=false;
    if(rotation_ && frame.acceleration) {
        const auto [x,y,z]=*frame.acceleration;
        const double norm=x*x+y*y+z*z;
        if(!std::isfinite(norm) || norm<49 || norm>144 || std::abs(z)>7 || std::max(std::abs(x),std::abs(y))<6.5 || std::abs(std::abs(x)-std::abs(y))<1.5) {
            candidate_=-1; return result;
        }
        const int candidate=std::abs(y)>std::abs(x) ? (y>0 ? 0 : 180) : (x>0 ? 270 : 90);
        if(candidate!=candidate_) { candidate_=candidate; candidate_since_=frame.sample_ms; }
        else if(frame.sample_ms>=candidate_since_ && frame.sample_ms-candidate_since_>=750 && candidate!=applied_rotation_) {
            result.rotation_degrees=candidate; applied_rotation_=candidate;
        }
    } else candidate_=-1;
    if(result.rotation_degrees || result.brightness_percent)result.reason="calibrated-sample-decision";
    return result;
}
DecryptionSession::DecryptionSession(std::shared_ptr<DecryptionProvider> provider):provider_(std::move(provider)) {
    require(provider_!=nullptr,"decryption-unavailable","No accepted decryption provider is available");
}
DecryptionSession::~DecryptionSession() { if(!close())provider_->quarantine(); }
bool DecryptionSession::close() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if(!open_ && !quarantined_)return true;
    if(provider_->close()) { open_=false; quarantined_=false; return true; }
    quarantined_=true; provider_->quarantine(); return false;
}
UnlockOutcome DecryptionSession::unlock_from_fd(int fd,unsigned user,std::uint64_t now) {
    std::lock_guard<std::mutex> lock(mutex_);
    require(!open_ && !quarantined_,"decryption-session-busy","Close or recover the existing encrypted session first");
    const auto before=provider_->readiness();
    require(before.accepted && before.read_only_access && before.provider_generation!=0,"decryption-unavailable","Device decryption provider is not accepted; no credential was read");
    require(user<=1023 && (provider_->system()==EncryptedSystem::AndroidFbe || user==0),"invalid-encrypted-user","Select an Android user, or zero for Linux LUKS");
    require(now>=retry_until_,"credential-retry-delay","The authentication provider's retry delay has not expired");
    Credential secret; secret.read(fd);
    const auto current=provider_->readiness();
    require(current.accepted && current.read_only_access && current.provider_generation==before.provider_generation,
        "decryption-provider-changed","Provider identity changed before authentication");
    UnlockOutcome result;
    try { result=provider_->unlock(secret.bytes(),user); }
    catch(...) { quarantined_=true; provider_->quarantine(); throw Error("decryption-provider-failed","Authentication failed; provider cleanup must be inspected"); }
    UnlockReadiness after;
    try { after=provider_->readiness(); }
    catch(...) {
        if(!provider_->close()) { quarantined_=true; provider_->quarantine(); }
        throw Error("decryption-provider-failed","Cannot verify the authenticated provider; access was not accepted");
    }
    if(result.retry_after_seconds>86400 || now>UINT64_MAX-result.retry_after_seconds || !after.accepted ||
        !after.read_only_access || after.provider_generation!=before.provider_generation || (result.authenticated && !result.read_only_access)) {
        if(!provider_->close()) { quarantined_=true; provider_->quarantine(); }
        throw Error("invalid-decryption-outcome","Authentication result or provider identity is unsafe; access was not accepted");
    }
    if(result.authenticated) {
        open_=true;
    } else if(!provider_->close()) {
        quarantined_=true; provider_->quarantine();
        throw Error("decryption-cleanup-required","Failed authentication did not verify complete cleanup");
    }
    retry_until_=now+result.retry_after_seconds;
    return result;
}
} // namespace ure
