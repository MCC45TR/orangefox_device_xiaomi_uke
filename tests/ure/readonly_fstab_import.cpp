// SPDX-License-Identifier: GPL-3.0-or-later
#include "ure-readonly-fstab-import.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sys/stat.h>

namespace {
void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

const std::string metadata =
    "/dev/block/bootdevice/by-name/metadata /metadata f2fs "
    "noatime,nosuid,nodev,discard wait,check,formattable,wrappedkey,first_stage_mount\n";
const std::string data =
    "/dev/block/bootdevice/by-name/userdata /data f2fs "
    "noatime,nosuid,nodev,discard,reserve_root=32768,resgid=1065,fsync_mode=nobarrier,"
    "inlinecrypt,gc_merge,compress_mode=user,compress_cache,atgc,age_extent_cache "
    "latemount,wait,check,formattable,fileencryption=aes-256-xts:aes-256-cts:"
    "v2+inlinecrypt_optimized+wrappedkey_v0,keydirectory=/metadata/vold/metadata_encryption,"
    "metadata_encryption=aes-256-xts:wrappedkey_v0,quota,reservedsize=128M,checkpoint=fs,fscompress\n";

std::string replace_once(std::string value, const std::string& before, const std::string& after) {
    const auto position = value.find(before);
    require(position != std::string::npos, "test mutation has a matching field");
    value.replace(position, before.size(), after);
    return value;
}
}  // namespace

int main(int argc, char** argv) {
    using namespace ure::readonly_fstab;
    require(argc == 2, "private test directory supplied");
    const auto imported = sanitize(metadata + data);
    require(imported.accepted, "measured wrapped-key Uke options accepted");
    require(imported.metadata.mount_options == "norecovery", "metadata journal replay disabled");
    require(imported.data.mount_options == "norecovery,inlinecrypt", "data replay disabled and inline crypto retained");
    require(imported.data.key_directory == kKeyDirectory, "reviewed metadata key directory retained");
    require(imported.additional_fstab.find(kFileEncryption) != std::string::npos &&
        imported.additional_fstab.find(kMetadataEncryption) != std::string::npos,
        "wrapped-key metadata remains available to a future reviewed consumer");
    for (const char* forbidden : {"discard", "gc_merge", "atgc", "age_extent_cache", "fsync_mode",
            "check,", "formattable", "quota", "fscompress", "checkpoint", "compress_mode"})
        require(imported.additional_fstab.find(forbidden) == std::string::npos, forbidden);
    require(sanitize(imported.additional_fstab).additional_fstab == imported.additional_fstab,
        "sanitized file is stable on repeated processing");
    require(sanitize(data + metadata).additional_fstab == imported.additional_fstab,
        "vendor entry order does not affect the approved pair");
    require(sanitize("# vendor map\n\n" + metadata + data +
        "vendor /vendor erofs ro wait,logical,slotselect\n").additional_fstab == imported.additional_fstab,
        "foreign mount entries are excluded from the additional crypto map");
    const auto ext4 = sanitize(replace_once(metadata, " f2fs ", " ext4 ") + data);
    require(ext4.accepted && ext4.metadata.mount_options == "noload", "ext4 metadata replay disabled");
    require(!sanitize(metadata).accepted && !sanitize(data).accepted, "incomplete pairs refused without partial output");
    require(!sanitize(metadata + data + data).accepted && !sanitize(metadata + metadata + data).accepted,
        "duplicate data and metadata rows refused");
    require(!sanitize(metadata + data + replace_once(data, " f2fs ", " ext4 ")).accepted,
        "ambiguous alternate filesystem rows refused");
    require(!sanitize(replace_once(metadata + data, "by-name/userdata", "by-name/persist")).accepted,
        "foreign block device refused");
    require(!sanitize(replace_once(metadata + data, "keydirectory=/metadata", "keydirectory=/persist")).accepted,
        "foreign key directory refused");
    require(!sanitize(replace_once(metadata + data, "wrappedkey_v0", "wrappedkey_v1")).accepted,
        "unsupported key-wrapping version refused");
    require(!sanitize(replace_once(metadata + data, "inlinecrypt,gc_merge", "gc_merge")).accepted,
        "missing inline encryption refused");
    require(!sanitize(replace_once(metadata + data, ",quota,", ",forceencrypt=footer,quota,")).accepted,
        "legacy encryption mode cannot be mixed into measured FBE");
    require(!sanitize(replace_once(metadata + data, ",quota,", ",fileencryption,quota,")).accepted,
        "bare malformed crypto flag refused");
    require(!sanitize(replace_once(metadata + data, ",quota,", ",keydirectory=/metadata/vold/metadata_encryption,quota,")).accepted,
        "duplicate crypto key-directory flag refused");
    require(!sanitize(replace_once(metadata + data, " f2fs ", " mifs ")).accepted,
        "unreviewed filesystem refused");
    require(!sanitize(replace_once(metadata + data, ",quota,", ",,quota,")).accepted,
        "empty manager option refused");
    require(!sanitize(metadata + replace_once(data, "\n", " trailing-field\n")).accepted,
        "extra target fields refused");
    require(!sanitize(metadata + data + std::string(1, '\0')).accepted, "embedded NUL refused");
    require(!sanitize(std::string(kMaxFileBytes + 1, 'x')).accepted, "oversized file refused");
    require(!sanitize(std::string(kMaxLineBytes + 1, '#') + "\n" + metadata + data).accepted,
        "oversized line cannot be split into a second synthetic row");
    // Mutate every ASCII control byte in a valid file. Only whitespace controls
    // accepted by the parser may survive admission; all output remains fixed.
    for (unsigned value = 0; value != 32; ++value) {
        if (value == '\t' || value == '\n' || value == '\r') continue;
        require(!sanitize(metadata + std::string(1, static_cast<char>(value)) + data).accepted,
            "control-byte injection refused");
    }

    const std::string directory = argv[1];
    const std::string source = directory + "/vendor.fstab";
    const std::string destination = directory + "/additional.fstab";
    { std::ofstream output(source); output << metadata << data; }
    std::string contents;
    require(read_bounded(source, &contents) && contents == metadata + data, "bounded regular-file read");
    require(publish_ramdisk_copy(destination, imported.additional_fstab), "atomic approved-copy publication");
    require(read_bounded(destination, &contents) && contents == imported.additional_fstab,
        "published file contains only the complete approved pair");
    struct stat info{};
    require(stat(destination.c_str(), &info) == 0 && (info.st_mode & 0777) == 0600,
        "additional fstab retains restrictive permissions");
    require(symlink(source.c_str(), (directory + "/source-symlink").c_str()) == 0,
        "source symlink fixture created");
    require(!read_bounded(directory + "/source-symlink", &contents) && contents.empty(),
        "source symlink refused without partial content");
    require(mkfifo((directory + "/source-fifo").c_str(), 0600) == 0, "FIFO fixture created");
    require(!read_bounded(directory + "/source-fifo", &contents), "special file refused without blocking");
    require(!read_bounded(directory, &contents), "directory source refused");
    { std::ofstream output(source); output << std::string(kMaxFileBytes + 1, 'x'); }
    require(!read_bounded(source, &contents) && contents.empty(), "oversized source refused before parsing");
    require(!publish_ramdisk_copy(directory + "/absent/additional.fstab", imported.additional_fstab),
        "publication failure is reported before any partition update");
    require(!same_block_device(source, source) && !same_block_device("/dev/null", "/dev/null"),
        "regular files and character devices cannot satisfy block identity");
    std::cout << "Read-only vendor fstab admission, replay prevention, identity refusals and atomic file controls passed.\n";
}
