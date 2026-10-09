// SPDX-License-Identifier: Apache-2.0
#include "recovery_services.hpp"
#include <algorithm>
#include <cmath>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <limits>
#include <sys/mman.h>
#include <unistd.h>
namespace {
unsigned checks=0;
void check(bool condition,const char* why) { ++checks; if(!condition)throw std::runtime_error(why); }
template<class F> void refused(const char* code,F operation) {
    try { operation(); } catch(const ure::Error& error) {
        check(error.code==code,"Unexpected refusal code");
        check(std::string(error.what()).find("fixture-only")==std::string::npos,"Credential escaped in an error"); return;
    }
    throw std::runtime_error("Unsafe credential operation was accepted");
}
ure::Fd credential(std::string_view bytes="fixture-only",bool seal=true) {
    ure::Fd fd(::memfd_create("credential",MFD_CLOEXEC|MFD_ALLOW_SEALING));
    check(fd.get()>=0 && ::fchmod(fd.get(),0600)==0,"Cannot create anonymous credential");
    check(::write(fd.get(),bytes.data(),bytes.size())==static_cast<ssize_t>(bytes.size()),"Cannot populate fixture credential");
    if(seal)check(::fcntl(fd.get(),F_ADD_SEALS,F_SEAL_WRITE|F_SEAL_GROW|F_SEAL_SHRINK|F_SEAL_SEAL)==0,"Cannot seal credential");
    return fd;
}
class Provider final:public ure::DecryptionProvider {
public:
    ure::EncryptedSystem kind=ure::EncryptedSystem::AndroidFbe;
    ure::UnlockReadiness ready{true,true,7};
    ure::UnlockOutcome result{true,true,0};
    mutable unsigned probes=0;
    unsigned calls=0,closes=0,quarantines=0;
    unsigned changed_at=0,throw_probe_at=0;
    bool throws=false,cleanup=true,complete_credential=false;
    ure::EncryptedSystem system()const noexcept override { return kind; }
    ure::UnlockReadiness readiness()const override {
        ++probes; if(probes==throw_probe_at)throw std::runtime_error("fixture-only provider error");
        auto out=ready; if(probes==changed_at)++out.provider_generation; return out;
    }
    ure::UnlockOutcome unlock(std::span<const unsigned char> bytes,unsigned) override {
        ++calls; complete_credential=std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size())=="fixture-only";
        if(throws)throw std::runtime_error("fixture-only credential must not escape");
        return result;
    }
    bool close()noexcept override { ++closes; return cleanup; }
    void quarantine()noexcept override { ++quarantines; }
};
void write(const ure::fs::path& path,std::string_view text) { ure::fs::create_directories(path.parent_path()); std::ofstream file(path); file<<text; check(file.good(),"Cannot write fixture"); }
void sensors() {
    ure::AutomaticDisplayController controller;
    auto frame=[](std::uint64_t seq,std::uint64_t ms,double lux,std::array<double,3> gravity) { return ure::SensorFrame{7,seq,ms,lux,gravity}; };
    check(!controller.update(frame(1,1000,100,{0,9.81,0}),1000).brightness_percent,"Unreviewed provider enabled automatic brightness");
    controller.configure(true,true,7);
    auto first=controller.update(frame(1,1000,100,{0,9.81,0}),1000);
    check(first.brightness_percent && *first.brightness_percent>=10 && *first.brightness_percent<=95 && !first.rotation_degrees,"Initial sample rotated immediately");
    check(!controller.update(frame(2,1700,100,{0,9.81,0}),1700).rotation_degrees,"Unstable orientation was accepted");
    auto stable=controller.update(frame(3,1800,101,{0,9.81,0}),1800);
    check(stable.rotation_degrees && *stable.rotation_degrees==0 && !stable.brightness_percent,"Stable rotation or brightness hysteresis failed");
    check(!controller.update(frame(4,1800,9000,{9.81,0,0}),2600).rotation_degrees,"Repeated acquisition timestamp accumulated stability");
    check(!controller.update(frame(4,10000,9000,{9.81,0,0}),2600).brightness_percent,"Future sample was accepted");
    check(!controller.update(frame(4,1900,9000,{9.81,0,0}),5000).brightness_percent,"Stale light sample was accepted");
    controller.manual_override(5000);
    check(!controller.update(frame(4,6000,9000,{9.81,0,0}),6000).brightness_percent,"Manual override was ignored");
    auto resume=controller.update(frame(5,10001,9000,{9.81,0,0}),10001);
    check(resume.brightness_percent && !resume.rotation_degrees,"Override resume reused old orientation evidence");
    check(controller.update(frame(6,10801,9000,{9.81,0,0}),10801).rotation_degrees==270,"Landscape decision is incorrect");
    auto wrong=frame(7,11000,10,{0,-9.81,0}); wrong.provider_generation=8;
    check(!controller.update(wrong,11000).brightness_percent,"Different provider generation was trusted");
    auto nan=frame(7,11001,std::numeric_limits<double>::quiet_NaN(),{0,std::numeric_limits<double>::infinity(),0});
    auto invalid=controller.update(nan,11001); check(!invalid.brightness_percent && !invalid.rotation_degrees,"Nonfinite data reached the actuator decision");
    check(!controller.update(frame(8,12000,-5,{0,0,9.81}),12000).rotation_degrees,"Flat tablet or negative lux was accepted");
    controller.configure(false,true,7);
    check(!controller.update(frame(1,1000,1000000,{0,-9.81,0}),1000).brightness_percent,"Disabled brightness emitted a decision");
    check(controller.update(frame(2,1800,1000000,{0,-9.81,0}),1800).rotation_degrees==180,"Inverted orientation is incorrect");
    controller.configure(true,false,7);
    check(controller.update(frame(1,1000,0,{-9.81,0,0}),1000).brightness_percent==10,"Dark-room lower bound failed");
    check(!controller.update(frame(2,1800,1000000,{-9.81,0,0}),1800).rotation_degrees,"Disabled rotation emitted a decision");
    check(controller.update(frame(3,2100,1000000,{-9.81,0,0}),2100).brightness_percent==95,"High-lux upper bound failed");
}
void secrets(const ure::fs::path& root) {
    auto fd=credential(); auto provider=std::make_shared<Provider>();
    {
        ure::DecryptionSession session(provider);
        check(session.unlock_from_fd(fd.get(),0,100).authenticated && provider->complete_credential,"Sealed credential offset or complete read failed");
        refused("decryption-session-busy",[&]{ session.unlock_from_fd(fd.get(),0,100); });
        check(session.close() && provider->closes==1,"Session cleanup was not retained");
    }
    provider=std::make_shared<Provider>(); provider->ready.accepted=false;
    { ure::DecryptionSession session(provider); refused("decryption-unavailable",[&]{ session.unlock_from_fd(-1,0,0); }); check(provider->calls==0,"Unavailable provider received credentials"); }
    provider=std::make_shared<Provider>(); provider->result={false,true,30};
    { ure::DecryptionSession session(provider); check(!session.unlock_from_fd(fd.get(),0,100).authenticated,"Wrong credential was accepted");
      refused("credential-retry-delay",[&]{ session.unlock_from_fd(-1,0,129); }); check(provider->calls==1,"Automatic retry bypassed delay");
      check(!session.unlock_from_fd(fd.get(),0,130).authenticated && provider->calls==2,"Retry delay did not expire"); }
    provider=std::make_shared<Provider>(); provider->kind=ure::EncryptedSystem::LinuxLuks;
    { ure::DecryptionSession session(provider); refused("invalid-encrypted-user",[&]{ session.unlock_from_fd(-1,1,0); });
      check(session.unlock_from_fd(fd.get(),0,100).authenticated,"Linux zero-user credential was refused"); }
    for(unsigned probe:{2U,3U}) {
        provider=std::make_shared<Provider>(); provider->changed_at=probe;
        ure::DecryptionSession session(provider);
        refused(probe==2 ? "decryption-provider-changed" : "invalid-decryption-outcome",[&]{ session.unlock_from_fd(fd.get(),0,100); });
        check(probe==2 ? provider->calls==0 : provider->closes==1,"Provider change was not contained");
    }
    provider=std::make_shared<Provider>(); provider->throw_probe_at=3;
    { ure::DecryptionSession session(provider); refused("decryption-provider-failed",[&]{ session.unlock_from_fd(fd.get(),0,100); }); check(provider->closes==1,"Post-authentication inspection exception leaked mapping"); }
    provider=std::make_shared<Provider>(); provider->result.read_only_access=false;
    { ure::DecryptionSession session(provider); refused("invalid-decryption-outcome",[&]{ session.unlock_from_fd(fd.get(),0,100); }); check(provider->closes==1,"Writable access was retained"); }
    provider=std::make_shared<Provider>(); provider->result.retry_after_seconds=86401;
    { ure::DecryptionSession session(provider); refused("invalid-decryption-outcome",[&]{ session.unlock_from_fd(fd.get(),0,100); }); }
    provider=std::make_shared<Provider>(); provider->throws=true;
    { ure::DecryptionSession session(provider); refused("decryption-provider-failed",[&]{ session.unlock_from_fd(fd.get(),0,100); });
      check(provider->quarantines==1,"Uncertain provider failure was not quarantined"); refused("decryption-session-busy",[&]{ session.unlock_from_fd(fd.get(),0,100); }); }
    provider=std::make_shared<Provider>(); provider->cleanup=false;
    { ure::DecryptionSession session(provider); check(session.unlock_from_fd(fd.get(),0,100).authenticated,"Cleanup fixture failed to open");
      check(!session.close() && provider->quarantines==1,"Failed teardown did not quarantine");
      refused("decryption-session-busy",[&]{ session.unlock_from_fd(fd.get(),0,100); }); provider->cleanup=true; check(session.close(),"Explicit cleanup recovery failed"); }
    write(root/"persistent-password","fixture-only"); ::chmod((root/"persistent-password").c_str(),0600);
    ure::Fd persistent(::open((root/"persistent-password").c_str(),O_RDONLY|O_CLOEXEC));
    auto unsealed=credential("fixture-only",false),empty=credential(""),large=credential(std::string(4097,'x'));
    provider=std::make_shared<Provider>(); ure::DecryptionSession session(provider);
    for(int source:{persistent.get(),unsealed.get(),empty.get(),large.get()})refused("unsafe-credential-source",[&]{ session.unlock_from_fd(source,0,100); });
    check(::mkfifo((root/"named-pipe").c_str(),0600)==0,"Cannot create named FIFO refusal fixture");
    ure::Fd named(::open((root/"named-pipe").c_str(),O_RDONLY|O_NONBLOCK|O_CLOEXEC));
    refused("unsafe-credential-source",[&]{ session.unlock_from_fd(named.get(),0,100); });
    int blocking[2]; check(::pipe2(blocking,O_CLOEXEC)==0,"Cannot create blocking pipe refusal fixture");
    ure::Fd blocked(blocking[0]),writer(blocking[1]);
    refused("unsafe-credential-source",[&]{ session.unlock_from_fd(blocked.get(),0,100); });
    int pipe[2]; check(::pipe2(pipe,O_CLOEXEC|O_NONBLOCK)==0,"Cannot create private pipe"); ure::Fd input(pipe[0]),output(pipe[1]);
    check(::write(output.get(),"fixture-only",12)==12,"Cannot populate pipe"); output=ure::Fd{};
    check(session.unlock_from_fd(input.get(),0,100).authenticated && provider->complete_credential,"Bounded nonblocking pipe was not accepted");
}
} // namespace
int main() {
    std::array<char,48> buffer{}; constexpr std::string_view pattern="/tmp/ure-recovery-services-XXXXXX"; std::copy(pattern.begin(),pattern.end(),buffer.begin());
    const char* directory=::mkdtemp(buffer.data()); if(!directory)return 1; const ure::fs::path root(directory);
    try {
        sensors(); secrets(root);
        write(root/"sys/class/thermal/thermal_zone0/type","cpuss-0\n"); write(root/"sys/class/thermal/thermal_zone0/temp","43500\n");
        write(root/"sys/class/thermal/thermal_zone1/type","bcl_current\n"); write(root/"sys/class/thermal/thermal_zone1/temp","3200\n");
        write(root/"sys/class/thermal/thermal_zone2/type","cpu_therm\n"); write(root/"sys/class/thermal/thermal_zone2/temp","not-a-value\n");
        ure::Root system(root); auto status=ure::recovery_services_status(system);
        check(status["temperatures"].size()==3 && status["temperatures"][0]["millidegrees_celsius"].asInt()==43500,"Thermal observation failed");
        check(!status["temperatures"][1].isMember("millidegrees_celsius") && status["temperatures"][1]["unit"]=="driver-defined-unverified","Proxy driver was mislabeled as Celsius");
        check(status["temperatures"][2]["available"]==false,"Malformed thermal value was exposed");
        check(status["android_fbe"]["unlock_available"]==false && status["linux_luks"]["unlock_available"]==false && status["automatic_display"]["automatic_enable"]==false && status["physical_test_record"]==false,"Host fixture enabled unaccepted hardware");
        check(ure::management_command({"services","status"}) && ure::management_dispatch({"services","status","--system-root",root.string()})["temperatures"].size()==3,"Read-only CLI status was not dispatched");
        ure::fs::remove_all(root); std::cout<<checks<<" credential transport, retained-provider teardown, retry, sensor policy and thermal-unit checks passed; host fixtures only.\n"; return 0;
    } catch(const std::exception& error) { ure::fs::remove_all(root); std::cerr<<error.what()<<'\n'; return 1; }
}
