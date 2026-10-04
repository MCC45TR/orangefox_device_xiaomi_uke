// SPDX-License-Identifier: Apache-2.0
#include "operation_lease.hpp"
#include "lifecycle_policy.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <linux/magic.h>
#include <mutex>
#include <sys/file.h>
#include <sys/statfs.h>
#include <unistd.h>

namespace ure {
namespace {
constexpr std::size_t record_limit=65536;
constexpr const char* operation_lock="operation.lock";
constexpr const char* control_lock="control.lock";
constexpr const char* admission_lock="admission.lock";
constexpr const char* owner_file="owner.json";
constexpr const char* release_file="release.json";
struct Configuration {
    std::mutex mutex;
    fs::path path;
    bool frozen=false;
    Value observed_domain;
    Value uncertain_owner;
};
Configuration configuration;

void bounded_value(const Value& value,unsigned depth,std::size_t& nodes,std::size_t& bytes) {
    require(depth<=24 && ++nodes<=4096,"invalid-operation-binding","Operation binding exceeds its structural budget");
    require(value.type()!=Json::realValue,"invalid-operation-binding","Operation identities cannot contain floating-point values");
    if(value.isString()) {
        const char* begin=nullptr; const char* end=nullptr;
        require(value.getString(&begin,&end),"invalid-operation-binding","Operation identity text is malformed");
        const auto length=static_cast<std::size_t>(end-begin);
        require(length<=4096 && bytes<=record_limit-length,"invalid-operation-binding","Operation identity text exceeds its budget");
        bytes+=length;
    } else if(value.isArray()) {
        require(value.size()<=128,"invalid-operation-binding","Operation identity array exceeds its budget");
        for(const auto& item:value)bounded_value(item,depth+1,nodes,bytes);
    } else if(value.isObject()) {
        require(value.size()<=128,"invalid-operation-binding","Operation identity object exceeds its budget");
        for(const auto& name:value.getMemberNames()) {
            require(name.size()<=128 && bytes<=record_limit-name.size(),"invalid-operation-binding","Operation identity key exceeds its budget");
            bytes+=name.size(); bounded_value(value[name],depth+1,nodes,bytes);
        }
    }
}
Value binding_value(const OperationBinding& binding) {
    require(identifier(binding.operation) && identifier(binding.operation_id) && hash_valid(binding.plan_sha256),
        "invalid-operation-binding","An operation, request identity and exact plan hash are required");
    require(binding.targets.isArray() && !binding.targets.empty() && binding.targets.size()<=128 && binding.journal.isObject() && !binding.journal.empty(),
        "invalid-operation-binding","Operation targets and journal binding must be explicit");
    for(const auto& target:binding.targets)require(target.isObject() && !target.empty(),"invalid-operation-binding","Every operation target must have an identity");
    std::size_t nodes=0,bytes=0; bounded_value(binding.targets,0,nodes,bytes); bounded_value(binding.journal,0,nodes,bytes);
    Value value; value["operation"]=binding.operation; value["operation_id"]=binding.operation_id;
    value["plan_sha256"]=binding.plan_sha256; value["targets"]=binding.targets; value["journal"]=binding.journal;
    require(json(value).size()<=record_limit/2,"invalid-operation-binding","Serialized operation binding exceeds 32 KiB"); return value;
}
void check_binding_value(const Value& value) {
    require(value.isObject() && value.size()==5 && value["operation"].isString() && value["operation_id"].isString() && value["plan_sha256"].isString(),
        "unsafe-operation-owner","Retained operation binding is malformed");
    OperationBinding binding{value["operation"].asString(),value["operation_id"].asString(),value["plan_sha256"].asString(),value["targets"],value["journal"]};
    require(json(binding_value(binding))==json(value),"unsafe-operation-owner","Retained operation binding is not canonical");
}
[[maybe_unused]] fs::path checked_path(const fs::path& input) {
    const auto text=input.generic_string();
    require(input.is_absolute() && text.size()>1 && text.size()<=4096 && text.find('\0')==text.npos && text.find('\n')==text.npos,
        "ownership-unavailable","A bounded absolute coordinator path is required");
    const auto parts=components(text.substr(1));
    require(!parts.empty() && input.lexically_normal()==input,"ownership-unavailable","Coordinator path must have a canonical real directory spelling");
    return input;
}
fs::path policy_path() {
#ifdef __ANDROID__
    static_assert(!device_operation_coordinator_accepted(),"Connect an accepted Android coordinator to both management and lifecycle admission");
    throw Error("ownership-unavailable","No accepted persistent operation ownership backend is installed for Android");
#else
    std::lock_guard<std::mutex> held(configuration.mutex);
    if(configuration.path.empty()) {
        const auto* configured=::getenv("URE_OPERATION_COORDINATOR");
        require(configured!=nullptr && *configured!='\0',"ownership-unavailable","A single host operation coordinator must be configured before management effects");
        configuration.path=checked_path(fs::path(configured));
    }
    configuration.frozen=true; return configuration.path;
#endif
}
Fd directory_path(const fs::path& path,bool create) {
    const auto parts=components(path.generic_string().substr(1));
    Fd current(::open("/",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));
    require(current.get()>=0,"ownership-unavailable","Cannot open the coordinator path anchor");
    for(std::size_t i=0;i<parts.size();++i) {
        const bool final=i+1==parts.size();
        if(final && create) {
            const auto made=::mkdirat(current.get(),parts[i].c_str(),0700);
            require(made==0 || errno==EEXIST,"ownership-unavailable","Cannot create the private operation coordinator");
            if(made==0)require(::fsync(current.get())==0,"ownership-unavailable","Cannot sync operation coordinator creation");
        }
        Fd next(::openat(current.get(),parts[i].c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));
        require(next.get()>=0,"ownership-unavailable","Coordinator path is missing, inaccessible or contains a symlink");
        current=std::move(next);
    }
    return current;
}
Value identity(int fd,bool directory) {
    struct stat st{};
    require(::fstat(fd,&st)==0 && (directory ? S_ISDIR(st.st_mode) : S_ISREG(st.st_mode)) && st.st_uid==::geteuid() &&
        (st.st_mode&07777)==(directory ? 0700 : 0600) && (directory || st.st_nlink==1),
        "unsafe-operation-coordinator","Coordinator directory and files must be real, owned and private; files need one link");
    Value value; value["device"]=Json::UInt64(st.st_dev); value["inode"]=Json::UInt64(st.st_ino);
    value["uid"]=Json::UInt64(st.st_uid); value["mode"]=Json::UInt64(st.st_mode&07777); return value;
}
void durable_directory(int fd,bool sync) {
    struct statfs fsinfo{};
    require(::fstatfs(fd,&fsinfo)==0 && (fsinfo.f_type==EXT4_SUPER_MAGIC || fsinfo.f_type==BTRFS_SUPER_MAGIC ||
        fsinfo.f_type==F2FS_SUPER_MAGIC || fsinfo.f_type==XFS_SUPER_MAGIC),
        "ownership-unavailable","Retained ownership requires an accepted persistent ext4, Btrfs, F2FS or XFS filesystem");
    if(sync)require(::fsync(fd)==0,"ownership-unavailable","Operation coordinator directory durability is unavailable");
}
bool present(const Root& root,const char* name) {
    struct stat st{}; if(::fstatat(root.fd(),name,&st,AT_SYMLINK_NOFOLLOW)==0)return true;
    require(errno==ENOENT,"ownership-unavailable","Cannot inspect coordinator record presence"); return false;
}
Fd private_file(const Root& root,const char* name,bool create,bool writable=true) {
    const int flags=(writable ? O_RDWR : O_RDONLY)|(create ? O_CREAT : 0)|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK;
    Fd fd(::openat(root.fd(),name,flags,0600));
    require(fd.get()>=0,"unsafe-operation-coordinator","Coordinator file is missing, inaccessible or a symlink");
    static_cast<void>(identity(fd.get(),false)); return fd;
}
class Exclusion {
    Fd fd_;
    pid_t process_;
public:
    explicit Exclusion(Fd fd):fd_(std::move(fd)),process_(::getpid()) {}
    ~Exclusion() { if(fd_.get()>=0 && process_==::getpid())static_cast<void>(::flock(fd_.get(),LOCK_UN)); }
    Exclusion(Exclusion&& other) noexcept:fd_(std::move(other.fd_)),process_(other.process_) {}
    Exclusion& operator=(Exclusion&& other) noexcept {
        if(this!=&other) {
            if(fd_.get()>=0 && process_==::getpid())static_cast<void>(::flock(fd_.get(),LOCK_UN));
            fd_=std::move(other.fd_); process_=other.process_;
        }
        return *this;
    }
    Exclusion(const Exclusion&)=delete;
    Exclusion& operator=(const Exclusion&)=delete;
    int get() const { return fd_.get(); }
    void require_process() const { require(process_==::getpid(),"operation-lease-inactive","A child process cannot borrow its parent's explicit lease"); }
};
Exclusion exclusion(int fd,const std::string& code,const std::string& message) {
    Fd copy(::fcntl(fd,F_DUPFD_CLOEXEC,3)); require(copy.get()>=0,"ownership-unavailable","Cannot retain coordinator exclusion descriptor");
    const int result=::flock(copy.get(),LOCK_EX|LOCK_NB);
    if(result<0)require(errno==EWOULDBLOCK || errno==EAGAIN,"ownership-unavailable","Coordinator locking is unavailable");
    require(result==0,code,message); return Exclusion(std::move(copy));
}
void unlock(int fd) { require(::flock(fd,LOCK_UN)==0,"ownership-unavailable","Cannot release coordinator exclusion"); }
std::string record_seal(Value value) { value.removeMember("record_sha256"); return sha256(json(value)); }
Value read_record(const Root& root,const char* name) {
    auto fd=private_file(root,name,false,false); struct stat before{},after{};
    require(::fstat(fd.get(),&before)==0 && before.st_size>0 && static_cast<std::uint64_t>(before.st_size)<=record_limit,
        "unsafe-operation-owner","Coordinator record is empty or exceeds its budget");
    std::string bytes(static_cast<std::size_t>(before.st_size),'\0'); std::size_t done=0;
    while(done<bytes.size()) {
        const auto count=::pread(fd.get(),bytes.data()+done,bytes.size()-done,static_cast<off_t>(done));
        if(count<0 && errno==EINTR)continue;
        require(count>0,"unsafe-operation-owner","Coordinator record was truncated while reading"); done+=static_cast<std::size_t>(count);
    }
    require(::fstat(fd.get(),&after)==0 && before.st_size==after.st_size && before.st_mtim.tv_sec==after.st_mtim.tv_sec &&
        before.st_mtim.tv_nsec==after.st_mtim.tv_nsec && before.st_ctim.tv_sec==after.st_ctim.tv_sec && before.st_ctim.tv_nsec==after.st_ctim.tv_nsec,
        "unsafe-operation-owner","Coordinator record changed while reading");
    const auto value=parse_json(bytes);
    require(value.isObject() && value["schema"]==1 && value["record_sha256"].isString() && hash_valid(value["record_sha256"].asString()) &&
        record_seal(value)==value["record_sha256"].asString(),"unsafe-operation-owner","Coordinator record checksum or schema is invalid"); return value;
}
void save_record(const Root& root,const char* name,Value value,bool replace) {
    value["record_sha256"]=record_seal(value);
    require(json(value).size()<=record_limit,"unsafe-operation-owner","Coordinator record exceeds 64 KiB");
    root.save_record(name,value,replace);
}
struct Domain {
    fs::path path;
    Root root;
    Fd operation,control,admission;
    Value seal;
    Domain(fs::path selected,Root retained,Fd op,Fd ctrl,Fd serial,Value expected)
        :path(std::move(selected)),root(std::move(retained)),operation(std::move(op)),control(std::move(ctrl)),admission(std::move(serial)),seal(std::move(expected)) {}
    void verify(bool sync=false) const {
        auto current=directory_path(path,false);
        require(json(identity(current.get(),true))==json(seal["directory"]) && json(identity(root.fd(),true))==json(seal["directory"]),
            "changed-operation-coordinator","Operation coordinator directory was replaced or changed");
        for(const auto& item:std::array<std::pair<const char*,int>,3>{{{operation_lock,operation.get()},{control_lock,control.get()},{admission_lock,admission.get()}}}) {
            auto named=private_file(root,item.first,false,false);
            require(json(identity(named.get(),false))==json(seal[item.first]) && json(identity(item.second,false))==json(seal[item.first]),
                "changed-operation-coordinator","A retained coordinator lock was replaced");
        }
        require(json(read_record(root,"domain.json"))==json(seal),"changed-operation-coordinator","The coordinator identity record changed");
        durable_directory(root.fd(),sync);
    }
};
Domain open_domain(bool create) {
    const auto path=policy_path(); Value remembered;
    { std::lock_guard<std::mutex> held(configuration.mutex); remembered=configuration.observed_domain; }
    Root root(directory_path(path,create && remembered.isNull()));
    const auto root_identity=identity(root.fd(),true); durable_directory(root.fd(),create);
    if(!remembered.isNull())require(json(root_identity)==json(remembered["directory"]),"changed-operation-coordinator","Configured coordinator directory was replaced");
    const bool sealed=present(root,"domain.json");
    auto serial=private_file(root,admission_lock,create && !sealed);
    auto held=exclusion(serial.get(),"operation-coordinator-busy","Another process is updating coordinator admission");
    const bool initialized=present(root,"domain.json");
    auto op=private_file(root,operation_lock,create && !initialized);
    auto control=private_file(root,control_lock,create && !initialized);
    Value seal;
    if(initialized)seal=read_record(root,"domain.json");
    else {
        require(create && !present(root,owner_file),"unsafe-operation-coordinator","Unsealed retained ownership cannot be adopted");
        seal["schema"]=1; seal["directory"]=root_identity; seal[operation_lock]=identity(op.get(),false);
        seal[control_lock]=identity(control.get(),false); seal[admission_lock]=identity(serial.get(),false);
        seal["record_sha256"]=record_seal(seal); save_record(root,"domain.json",seal,false);
    }
    require(seal.size()==6,"unsafe-operation-coordinator","Coordinator identity record has unexpected fields");
    {
        std::lock_guard<std::mutex> guard(configuration.mutex);
        if(configuration.observed_domain.isNull())configuration.observed_domain=seal;
        else require(json(configuration.observed_domain)==json(seal),"changed-operation-coordinator","Configured coordinator identity was replaced");
    }
    Domain domain(path,std::move(root),std::move(op),std::move(control),std::move(serial),seal);
    domain.verify(); unlock(held.get()); return domain;
}
void check_owner(const Domain& domain,const Value& owner,bool retained) {
    require(owner.size()==9 && owner["binding"].isObject() && owner["phase"].isString() && identifier(owner["phase"].asString()) &&
        owner["nonce"].isString() && identifier(owner["nonce"].asString()) && owner["owner_pid"].isInt64() && owner["owner_pid"].asInt64()>0 &&
        owner["updated_at"].isString() && owner["updated_at"].asString().size()<=64 && owner["domain_sha256"]==domain.seal["record_sha256"] &&
        owner["retained"]==retained,"unsafe-operation-owner","Operation ownership has invalid fields or belongs to a different domain");
    check_binding_value(owner["binding"]);
}
bool terminal_state(const std::string& state) {
    return state=="COMMITTED" || state=="COMPLETE" || state=="ROLLED_BACK" || state=="CANCELLED_SAFE" || state=="FAILED_SAFE";
}
void check_release_receipt(const Domain& domain,const Value& released) {
    const auto receipt=read_record(domain.root,"last-terminal.json"); const auto& terminal=receipt["terminal"];
    require(receipt.size()==6 && receipt["nonce"]==released["nonce"] && json(receipt["binding"])==json(released["binding"]) &&
        receipt["updated_at"].isString() && receipt["updated_at"].asString().size()<=64 && terminal.isObject() &&
        terminal["verified"]==true && terminal["cleanup_complete"]==true && terminal["state"].isString() && terminal_state(terminal["state"].asString()),
        "unsafe-operation-owner","Completed owner release lacks its exact verified terminal receipt");
}
Value retained_owner(const Domain& domain) {
    Value pending,completed;
    if(present(domain.root,release_file)) {
        auto release=read_record(domain.root,release_file);
        require(release["phase"]=="RELEASE_PENDING" || release["phase"]=="RELEASED","unsafe-operation-owner","Owner release tombstone has an unknown phase");
        const bool unresolved=release["phase"]=="RELEASE_PENDING"; check_owner(domain,release,unresolved);
        if(unresolved)pending=std::move(release);
        else completed=std::move(release);
    }
    if(present(domain.root,owner_file)) {
        auto owner=read_record(domain.root,owner_file); check_owner(domain,owner,true);
        if(!pending.isNull()) {
            require(json(pending["binding"])==json(owner["binding"]),"unsafe-operation-owner","Pending release and owner bindings differ");
            require(pending["nonce"]==owner["nonce"],"unsafe-operation-owner","Pending owner release belongs to another owner generation");
        }
        return owner;
    }
    if(!pending.isNull())return pending;
    if(!completed.isNull())check_release_receipt(domain,completed);
    // A failed filesystem can refuse every restoration write. Before the
    // durable completion marker, owner.json has not been removed. After that
    // commit point its absence is safe only with the exact verified receipt.
    // The process that observed an error remains conservatively closed until
    // the same operation independently verifies retirement again.
    std::lock_guard<std::mutex> held(configuration.mutex);
    return configuration.uncertain_owner;
}
Value owner_record(const Domain& domain) {
    auto owner=retained_owner(domain);
    require(!owner.isNull(),"operation-owner-missing","No retained operation owner is present"); return owner;
}
void exact_owner(const Value& owner,const Value& binding) {
    require(json(owner["binding"])==json(binding),"operation-owner-mismatch","Only the exact retained plan, journal and targets may recover or control this operation");
}
void idle_control(const Domain& domain) {
    auto check=exclusion(domain.control.get(),"operation-control-busy","An owner-bound operation control is still active"); unlock(check.get());
}
} // namespace

void configure_operation_coordinator(const fs::path& path) {
#ifdef __ANDROID__
    static_assert(!device_operation_coordinator_accepted(),"Host configuration cannot enable management while Android lifecycle has no accepted coordinator");
    static_cast<void>(path); throw Error("ownership-unavailable","Host coordinator configuration cannot authorize Android operation ownership");
#else
    const auto checked=checked_path(path); std::lock_guard<std::mutex> held(configuration.mutex);
    require(!configuration.frozen,"operation-coordinator-configured","The global coordinator must be configured before first use");
    require(configuration.path.empty() || configuration.path==checked,"operation-coordinator-configured","The process cannot select a second coordinator domain");
    configuration.path=checked;
#endif
}

struct OperationLease::Impl {
    std::mutex mutex;
    Domain domain;
    Exclusion held;
    OperationBinding binding;
    Value value;
    std::string nonce;
    bool intent=false;
    bool released=false;
    Impl(Domain selected,Exclusion exclusion_fd,const OperationBinding& request,Value canonical)
        :domain(std::move(selected)),held(std::move(exclusion_fd)),binding(request),value(std::move(canonical)),nonce(operation_id()) {}
    void validate_active() const {
        require(!released && held.get()>=0,"operation-lease-inactive","The explicit operation lease is no longer active");
        domain.verify();
        if(intent) {
            const auto owner=owner_record(domain); exact_owner(owner,value);
            require(owner["nonce"]==nonce,"operation-owner-mismatch","Retained operation owner generation changed");
        } else require(retained_owner(domain).isNull(),"operation-owner-mismatch","An unexpected retained owner appeared before this operation's intent");
    }
};
OperationLease::OperationLease(std::unique_ptr<Impl> impl):impl_(std::move(impl)) {}
OperationLease::~OperationLease()=default;
OperationLease::OperationLease(OperationLease&&) noexcept=default;
OperationLease& OperationLease::operator=(OperationLease&&) noexcept=default;
OperationLease OperationLease::acquire(const OperationBinding& binding,LeaseAdmission admission) {
    auto value=binding_value(binding); auto domain=open_domain(true);
    auto serial=exclusion(domain.admission.get(),"operation-coordinator-busy","Another process is updating operation admission");
    domain.verify(true); idle_control(domain);
    auto held=exclusion(domain.operation.get(),"operation-busy","Another cooperating operation or lifecycle transition owns the coordinator");
    auto impl=std::make_unique<Impl>(std::move(domain),std::move(held),binding,std::move(value));
    const auto retained=retained_owner(impl->domain);
    if(!retained.isNull()) {
        const auto& owner=retained;
        require(admission!=LeaseAdmission::NewOperation,"operation-recovery-required","An interrupted operation retains ownership; inspect and recover its exact reviewed plan");
        exact_owner(owner,impl->value); impl->intent=true; impl->nonce=owner["nonce"].asString();
    } else require(admission!=LeaseAdmission::RecoverSameOperation,"operation-owner-missing","No retained ownership exists for the requested recovery");
    unlock(serial.get()); return OperationLease(std::move(impl));
}
void OperationLease::require_active() const {
    require(impl_!=nullptr,"operation-lease-inactive","The explicit operation lease is no longer active");
    impl_->held.require_process();
    std::lock_guard<std::mutex> guard(impl_->mutex); impl_->validate_active();
}
void OperationLease::require_binding(const OperationBinding& binding) const {
    require(impl_!=nullptr,"operation-lease-inactive","The explicit operation lease is no longer active"); impl_->held.require_process();
    std::lock_guard<std::mutex> guard(impl_->mutex); impl_->validate_active();
    require(json(binding_value(binding))==json(impl_->value),"operation-owner-mismatch","Nested use requires the exact explicit operation binding");
}
const OperationBinding& OperationLease::binding() const { require_active(); return impl_->binding; }
bool OperationLease::has_retained_intent() const {
    require(impl_!=nullptr,"operation-lease-inactive","The explicit operation lease is no longer active"); impl_->held.require_process();
    std::lock_guard<std::mutex> guard(impl_->mutex); impl_->validate_active(); return impl_->intent;
}
void OperationLease::checkpoint(const std::string& phase) {
    require(identifier(phase) && !terminal_state(phase),"invalid-operation-phase","An unresolved intent phase is required");
    require(impl_!=nullptr,"operation-lease-inactive","The explicit operation lease is no longer active"); impl_->held.require_process();
    std::lock_guard<std::mutex> guard(impl_->mutex); impl_->validate_active();
    auto serial=exclusion(impl_->domain.admission.get(),"operation-coordinator-busy","Another process is updating ownership admission");
    impl_->domain.verify(true); Value owner;
    if(impl_->intent) { owner=owner_record(impl_->domain); exact_owner(owner,impl_->value); }
    else { owner["schema"]=1; owner["binding"]=impl_->value; owner["nonce"]=impl_->nonce;
        owner["domain_sha256"]=impl_->domain.seal["record_sha256"]; owner["retained"]=true; }
    owner["phase"]=phase; owner["owner_pid"]=Json::Int64(::getpid()); owner["updated_at"]=utc();
    save_record(impl_->domain.root,owner_file,owner,impl_->intent); impl_->intent=true; unlock(serial.get());
}
void OperationLease::release_verified(const Value& terminal_result) {
    require(terminal_result.isObject() && terminal_result["verified"]==true && terminal_result["cleanup_complete"]==true &&
        terminal_result["state"].isString() && terminal_state(terminal_result["state"].asString()),
        "operation-verification-required","Ownership release requires independent terminal verification and complete child/mount cleanup");
    std::size_t nodes=0,bytes=0; bounded_value(terminal_result,0,nodes,bytes);
    require(json(terminal_result).size()<=record_limit/2,"operation-verification-required","Terminal verification record exceeds 32 KiB");
    require(impl_!=nullptr,"operation-lease-inactive","The explicit operation lease is no longer active"); impl_->held.require_process();
    std::lock_guard<std::mutex> held_token(impl_->mutex); impl_->validate_active();
    auto serial=exclusion(impl_->domain.admission.get(),"operation-coordinator-busy","Another process is updating ownership admission");
    impl_->domain.verify(true); idle_control(impl_->domain);
    if(impl_->intent) {
        const auto owner=owner_record(impl_->domain); exact_owner(owner,impl_->value);
        Value receipt; receipt["schema"]=1; receipt["binding"]=impl_->value; receipt["nonce"]=impl_->nonce;
        receipt["terminal"]=terminal_result; receipt["updated_at"]=utc();
        save_record(impl_->domain.root,"last-terminal.json",receipt,present(impl_->domain.root,"last-terminal.json"));
        auto pending=owner; pending["phase"]="RELEASE_PENDING"; pending["updated_at"]=utc(); pending["record_sha256"]=record_seal(pending);
        save_record(impl_->domain.root,release_file,pending,present(impl_->domain.root,release_file));
        try {
            // Commit the verified terminal receipt while owner.json still
            // exists. If publication or its directory fsync fails, no owner
            // removal has occurred, even when every restoration write fails.
            // A visible RELEASED marker can never hide an unretired owner.
            auto complete=pending; complete["phase"]="RELEASED"; complete["retained"]=false;
            save_record(impl_->domain.root,release_file,complete,true);
            if(present(impl_->domain.root,owner_file))require(::unlinkat(impl_->domain.root.fd(),owner_file,0)==0,
                "uncertain-owner-release","Cannot remove the verified operation owner");
            require(::fsync(impl_->domain.root.fd())==0,"uncertain-owner-release","Verified owner removal durability is uncertain");
            std::lock_guard<std::mutex> guard(configuration.mutex); configuration.uncertain_owner=Value();
        } catch(...) {
            // Owner removal follows a durable verified completion marker.
            // Restore conservative pending ownership when possible; failed
            // completion publication always leaves the original owner intact.
            { std::lock_guard<std::mutex> guard(configuration.mutex); configuration.uncertain_owner=pending; }
            try { save_record(impl_->domain.root,release_file,pending,true); } catch(...) {}
            try { save_record(impl_->domain.root,owner_file,pending,present(impl_->domain.root,owner_file)); } catch(...) {}
            throw Error("uncertain-owner-release","Owner retirement failed; the exact operation retains ownership for verified recovery");
        }
    }
    unlock(impl_->held.get()); impl_->released=true; unlock(serial.get());
}

struct OwnerControlLease::Impl {
    Domain domain;
    Exclusion held;
    Value binding;
    std::string nonce;
    Impl(Domain selected,Exclusion exclusion_fd,Value canonical,std::string generation)
        :domain(std::move(selected)),held(std::move(exclusion_fd)),binding(std::move(canonical)),nonce(std::move(generation)) {}
};
OwnerControlLease::OwnerControlLease(std::unique_ptr<Impl> impl):impl_(std::move(impl)) {}
OwnerControlLease::~OwnerControlLease()=default;
OwnerControlLease::OwnerControlLease(OwnerControlLease&&) noexcept=default;
OwnerControlLease& OwnerControlLease::operator=(OwnerControlLease&&) noexcept=default;
OwnerControlLease OwnerControlLease::acquire(const OperationBinding& binding) {
    auto value=binding_value(binding); auto domain=open_domain(false);
    auto serial=exclusion(domain.admission.get(),"operation-coordinator-busy","Another process is updating ownership admission"); domain.verify();
    require(!retained_owner(domain).isNull(),"operation-owner-missing","A control request requires an existing retained operation owner");
    const auto owner=owner_record(domain); exact_owner(owner,value);
    auto held=exclusion(domain.control.get(),"operation-control-busy","Another owner-bound operation control is active");
    unlock(serial.get()); return OwnerControlLease(std::make_unique<Impl>(std::move(domain),std::move(held),std::move(value),owner["nonce"].asString()));
}
void OwnerControlLease::require_active() const {
    require(impl_ && impl_->held.get()>=0,"operation-lease-inactive","The explicit control lease is no longer active"); impl_->domain.verify();
    impl_->held.require_process();
    const auto owner=owner_record(impl_->domain); exact_owner(owner,impl_->binding);
    require(owner["nonce"]==impl_->nonce,"operation-owner-mismatch","The controlled operation owner generation changed");
}
void OwnerControlLease::require_binding(const OperationBinding& binding) const {
    require_active(); require(json(binding_value(binding))==json(impl_->binding),"operation-owner-mismatch","A control token cannot target another operation");
}

struct LifecycleLease::Impl {
    RuntimeActivityLease activity;
    Domain domain;
    Exclusion held;
    Impl(RuntimeActivityLease runtime,Domain selected,Exclusion exclusion_fd):activity(std::move(runtime)),domain(std::move(selected)),held(std::move(exclusion_fd)) {}
};
LifecycleLease::LifecycleLease(std::unique_ptr<Impl> impl):impl_(std::move(impl)) {}
LifecycleLease::~LifecycleLease()=default;
LifecycleLease::LifecycleLease(LifecycleLease&&) noexcept=default;
LifecycleLease& LifecycleLease::operator=(LifecycleLease&&) noexcept=default;
LifecycleLease LifecycleLease::acquire(const std::string& action) {
    require(action=="reboot" || action=="unmount" || action=="mount" || action=="shutdown","invalid-lifecycle-action","An explicit managed lifecycle action is required");
    auto activity=RuntimeActivityLease::acquire(nullptr,true);
    require(activity.acquired() && activity.valid(),activity.error()[0] ? activity.error() : "gui-registry-domain-changed",
        "An active GUI job or unavailable runtime registry prevents this lifecycle transition");
    auto domain=open_domain(true); auto serial=exclusion(domain.admission.get(),"operation-coordinator-busy","Another process is updating lifecycle admission");
    domain.verify(true); idle_control(domain);
    auto held=exclusion(domain.operation.get(),"operation-busy","An operation or lifecycle transition still owns the coordinator");
    require(retained_owner(domain).isNull(),"operation-recovery-required","Retained operation ownership prevents an unreviewed lifecycle transition");
    unlock(serial.get()); return LifecycleLease(std::make_unique<Impl>(std::move(activity),std::move(domain),std::move(held)));
}
void LifecycleLease::require_active() const {
    require(impl_ && impl_->held.get()>=0,"operation-lease-inactive","The explicit lifecycle lease is no longer active");
    impl_->held.require_process();
    require(impl_->activity.valid(),"gui-registry-domain-changed","The lifecycle runtime registry changed while its exclusion was retained");
    impl_->domain.verify();
    require(retained_owner(impl_->domain).isNull(),"operation-owner-mismatch","Unexpected retained operation ownership appeared during a lifecycle transition");
}

Value operation_lease_status() {
    Value result; result["schema"]=1; result["read_only"]=true; result["private_record"]=true;
    result["cooperative_only"]=true; result["physical_test_record"]=false; result["atomic_snapshot"]=false;
    try {
        // Status must not create the domain, locks or any filesystem record.
        const auto path=policy_path(); Root root(directory_path(path,false)); identity(root.fd(),true); durable_directory(root.fd(),false);
        auto seal=read_record(root,"domain.json");
        require(seal.size()==6,"unsafe-operation-coordinator","Coordinator identity record has unexpected fields");
        auto op=private_file(root,operation_lock,false,false),ctrl=private_file(root,control_lock,false,false),serial=private_file(root,admission_lock,false,false);
        Domain domain(path,std::move(root),std::move(op),std::move(ctrl),std::move(serial),std::move(seal)); domain.verify();
        {
            std::lock_guard<std::mutex> held(configuration.mutex);
            if(configuration.observed_domain.isNull())configuration.observed_domain=domain.seal;
            else require(json(configuration.observed_domain)==json(domain.seal),"changed-operation-coordinator","Configured coordinator identity was replaced");
        }
        const int locked=::flock(domain.operation.get(),LOCK_SH|LOCK_NB);
        if(locked<0)require(errno==EWOULDBLOCK || errno==EAGAIN,"ownership-unavailable","Cannot observe operation exclusion");
        const auto owner=retained_owner(domain);
        result["available"]=true; result["active_exclusion"]=locked<0; result["retained_owner"]=!owner.isNull();
        if(result["retained_owner"]==true) { result["owner"]=owner; result["state"]="RECOVERY_REQUIRED"; }
        else result["state"]=locked<0 ? "ACTIVE_BEFORE_INTENT_OR_LIFECYCLE" : "IDLE";
        if(locked==0)unlock(domain.operation.get());
        return result;
    } catch(const Error& error) {
        result["available"]=false; result["state"]="UNAVAILABLE"; result["code"]=error.code; result["message"]=error.what(); return result;
    }
}
} // namespace ure
