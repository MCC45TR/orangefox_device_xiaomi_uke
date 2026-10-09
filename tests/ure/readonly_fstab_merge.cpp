// SPDX-License-Identifier: GPL-3.0-or-later
#include "ure-readonly-fstab-import.hpp"

#include <cstdlib>
#include <iostream>
#include <sys/mount.h>

class TWPartitionManager;
class TWPartition {
    std::string Primary_Block_Device = "/trusted/unchanged-userdata";
    std::string Fstab_File_System = "f2fs";
    std::string Current_File_System = "f2fs";
    bool Mount_Read_Only = true;
    int Mount_Flags = 0;
    std::string Mount_Options = "discard,gc_merge";
    std::string Key_Directory;
    bool Can_Be_Wiped = false;
    unsigned saved_flags = 0;
    struct Flags {
        std::string filesystem;
        int flags;
        std::string options;
    };
    std::vector<Flags> fs_flags{{"f2fs", 0, "discard"}, {"ext4", 0, "discard"}};
    void Save_FS_Flags(const std::string& filesystem, int flags, const std::string& options) {
        ++saved_flags;
        fs_flags.push_back({filesystem, flags, options});
    }
    friend class TWPartitionManager;
};

class TWPartitionManager {
    static void require(bool condition, const char* message) {
        if (condition) return;
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
public:
    static void Run() {
        // This include is extracted from the patched production translation
        // unit. A mutant lacking the effective RO assignment must fail below.
#include "readonly-fstab-merge.inc"
        for (const std::string filesystem : {"f2fs", "ext4"}) {
            for (const std::string mount_point : {"/metadata", "/data"}) {
                TWPartition partition;
                partition.Mount_Read_Only = false;
                const std::string options = (filesystem == "f2fs" ? "norecovery" : "noload") +
                    std::string(mount_point == "/data" ? ",inlinecrypt" : "");
                ure::readonly_fstab::Entry entry;
                entry.block_device = "/untrusted/import-cannot-replace-identity";
                entry.mount_point = mount_point;
                entry.filesystem = filesystem;
                entry.mount_options = options;
                entry.key_directory = ure::readonly_fstab::kKeyDirectory;
                merge_readonly(&partition, entry);
                require(partition.Mount_Read_Only, "effective partition RO state lost");
                require(partition.Mount_Flags == (MS_RDONLY | MS_NOSUID | MS_NODEV | MS_NOATIME),
                    "effective mount flags lost");
                require(partition.Mount_Options == options, "effective replay prevention lost");
                require(partition.Fstab_File_System == filesystem && partition.Current_File_System == filesystem,
                    "reviewed filesystem not merged");
                require(partition.Primary_Block_Device == "/trusted/unchanged-userdata" && !partition.Can_Be_Wiped,
                    "trusted identity or baseline write capability replaced");
                require(partition.saved_flags == 1 && partition.fs_flags.size() == 1 &&
                    partition.fs_flags[0].options == options &&
                    partition.fs_flags[0].flags == partition.Mount_Flags,
                    "unsafe alternate filesystem flags survived import");
                require(partition.Key_Directory == (mount_point == "/data" ? entry.key_directory : ""),
                    "key directory applied to an unrelated partition");
            }
        }
        std::cout << "Production fstab merge preserves identity, write policy, RO state and no-replay options for all four filesystem/target combinations.\n";
    }
};

int main() { TWPartitionManager::Run(); }
