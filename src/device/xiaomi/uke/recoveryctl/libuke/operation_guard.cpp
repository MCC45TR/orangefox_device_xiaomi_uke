// SPDX-License-Identifier: Apache-2.0
#include "operation_guard.hpp"
#include <fcntl.h>
#include <unistd.h>

namespace ure {
Value operation_target(int fd,const std::string& role) {
    struct stat st{}; require(::fstat(fd,&st)==0,"operation-target-unavailable","Cannot bind a retained operation descriptor");
    Value value; value["role"]=role; value["device"]=Json::UInt64(st.st_dev); value["inode"]=Json::UInt64(st.st_ino);
    value["kind"]=S_ISDIR(st.st_mode) ? "directory" : S_ISREG(st.st_mode) ? "regular-file" : S_ISBLK(st.st_mode) ? "block-device" : "other";
    value["rdev"]=Json::UInt64(st.st_rdev); return value;
}
Value operation_targets(const Value& identity) {
    require(identity.isObject() && !identity.empty(),"invalid-operation-binding","An operation target identity is required");
    Value out(Json::arrayValue); out.append(identity); return out;
}
OperationBinding operation_binding(const std::string& operation,const Value& plan,const fs::path& journal,const Value& targets) {
    require(plan.isObject(),"invalid-operation-binding","A reviewed plan is required for operation admission");
    Value sealed=plan; sealed.removeMember("plan_sha256");
    const auto hash=plan["plan_sha256"].isString() ? plan["plan_sha256"].asString() : sha256(json(sealed));
    require(hash_valid(hash),"invalid-operation-binding","Operation plan hash is invalid");
    const auto path=fs::absolute(journal).lexically_normal();
    // The journal may not exist yet, but its immediate parent must exist.
    // Choosing a more distant ancestor would change the exact recovery binding
    // after preparation creates that parent.
    Root directory(path.parent_path());
    Value store=operation_target(directory.fd(),"journal-parent"); store["path"]=path.generic_string();
    std::string id=plan["operation_id"].isString() ? plan["operation_id"].asString() : hash;
    require(identifier(id),"invalid-operation-binding","Operation request identity is invalid");
    return {operation,id,hash,targets,store};
}
ManagedOperation::ManagedOperation(const OperationBinding& binding,bool recovery,OperationLease* parent) {
    if(parent) {
        parent->require_active();
        // The explicit parent token is authority only for targets covered by
        // its immutable compound plan. The caller still validates each derived
        // plan, journal and descriptor before any effect.
        const auto& selected=parent->binding().targets;
        for(const auto& target:binding.targets) {
            bool covered=false;
            for(const auto& original:selected) {
                if(json(original)==json(target))covered=true;
                if(original.isMember("device") && original.isMember("inode") && original.isMember("rdev") &&
                    original["kind"]==target["kind"] && original["device"]==target["device"] &&
                    original["inode"]==target["inode"] && original["rdev"]==target["rdev"])covered=true;
                if(original["kind"]=="regular-image" && target["kind"]=="regular-image" &&
                    original["file_device"]==target["file_device"] && original["file_inode"]==target["file_inode"] &&
                    original["bytes"]==target["bytes"] && original["logical_sector_bytes"]==target["logical_sector_bytes"])covered=true;
            }
            require(covered,"operation-owner-mismatch","Nested helper target is outside the explicitly retained operation");
        }
        lease_=parent;
    } else {
        owned_=std::make_unique<OperationLease>(OperationLease::acquire(binding,recovery ? LeaseAdmission::RecoverOrNewSameOperation : LeaseAdmission::NewOperation));
        lease_=owned_.get();
    }
}
OperationLease& ManagedOperation::token() const { require(lease_!=nullptr,"operation-lease-inactive","Missing operation token"); lease_->require_active(); return *lease_; }
void ManagedOperation::begin(const std::string& phase) { token().checkpoint(phase); }
Value ManagedOperation::finish(Value result,bool verified,bool cleanup_complete,const std::string& terminal) {
    token().require_active();
    if(owned_) {
        Value oracle; oracle["state"]=terminal.empty() ? result["state"] : Value(terminal);
        oracle["verified"]=verified; oracle["cleanup_complete"]=cleanup_complete;
        owned_->release_verified(oracle); result["operation_owner_released"]=true;
    }
    return result;
}
} // namespace ure
