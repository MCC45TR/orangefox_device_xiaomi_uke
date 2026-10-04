// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <json/json.h>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <sys/stat.h>

namespace ure {
namespace fs = std::filesystem;
using Value = Json::Value;
class OperationLease;
struct Error : std::runtime_error {
    std::string code;
    Error(std::string code_value, const std::string& message)
        : std::runtime_error(message), code(std::move(code_value)) {}
};
void require(bool condition, const std::string& code, const std::string& message);
class Fd {
    int fd_ = -1;
public:
    explicit Fd(int value = -1) : fd_(value) {}
    ~Fd();
    Fd(Fd&& other) noexcept;
    Fd& operator=(Fd&& other) noexcept;
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    int get() const { return fd_; }
};
class Root {
    Fd fd_;
public:
    explicit Root(const fs::path& path);
    explicit Root(Fd directory);
    int fd() const { return fd_.get(); }
    Fd open(std::string_view relative, int flags, mode_t mode = 0) const;
    // Read installed-system aliases inside this root. Absolute symlinks remain
    // relative to the selected root; magic links and all writes are refused.
    Fd open_resolved(std::string_view relative, int flags) const;
    std::string read_resolved(std::string_view relative, std::size_t limit = 1024 * 1024) const;
    bool exists_resolved(std::string_view relative) const;
    std::string read(std::string_view relative, std::size_t limit = 1024 * 1024) const;
    bool exists(std::string_view relative) const;
    struct stat stat(std::string_view relative) const;
    std::vector<std::string> list(std::string_view relative, std::size_t limit = 4096) const;
    std::string link(std::string_view relative) const;
    void atomic_save(std::string_view relative, std::string_view contents,
                     std::string_view expected_sha, bool preserve_metadata = true,
                     std::size_t limit = 1024 * 1024) const;
    void save_record(const std::string& relative, const Value& value, bool replace = false) const;
};
Root private_directory(const fs::path& path, bool create);
Root private_subdirectory(const Root& parent, const std::string& name, bool create);
std::vector<std::string> components(std::string_view relative);
bool identifier(std::string_view input);
bool uuid(std::string_view input);
bool hash_valid(std::string_view input);
std::string sha256(int fd);
std::string sha256(std::string_view bytes);
std::string json(const Value& value);
Value parse_json(std::string_view text);
Value json_file(const fs::path& path);
std::string bounded_read(const fs::path& path, std::size_t limit = 1024 * 1024);
void save_json(const fs::path& path, const Value& value, bool replace = false);
std::string operation_id();
std::uint64_t monotonic_ms();
std::string utc();
Value envelope(const Value& data);
std::string redact(std::string text);
struct ProcessResult { int status = -1; bool timed_out = false; std::string output; };
ProcessResult run_tool(const std::string& name, const std::vector<std::string>& args,
                       int timeout_seconds = 15, std::string_view input = {},
                       const std::vector<int>& inherited_fds = {});
bool tool_available(const std::string& name);

Value storage_graph(const Root& system);
Value storage_usage(const Root& system, const std::string& stable_id);
Value storage_usage_policy(const Value& graph, const Value& observations, const std::string& stable_id);
struct StorageTarget {
    Fd descriptor;
    Value identity;
    // Set only after the selector successfully opens the real block with
    // O_EXCL. Linux clears O_EXCL from f_flags after ->open, so F_GETFL cannot
    // recover whether this descriptor acquired a block claim.
    bool exclusive_claim=false;
};
StorageTarget storage_image(const fs::path& path, std::uint32_t sector, bool writable = false);
StorageTarget storage_select(const Root& system, const std::string& stable_id, bool exclusive_claim = false);
void storage_revalidate(const StorageTarget& target, const Root* system = nullptr);
void storage_write_gate(const StorageTarget& target);
std::uint64_t storage_bytes(int fd);
std::string storage_read(int fd, std::uint64_t offset, std::size_t bytes);
struct StorageRange { std::string name; std::uint64_t offset; std::string bytes; };
std::vector<StorageRange> gpt_regions(int fd, std::uint32_t sector);
std::vector<StorageRange> gpt_repair_regions(int fd, std::uint32_t sector);
std::vector<StorageRange> gpt_stock_regions(const fs::path& inputs, std::uint64_t capacity, unsigned lun,
                                          const Value& identities, const std::string& profile, Value& source);
Value gpt_stock_preview(const fs::path& inputs, std::uint64_t capacity, unsigned lun,
                        const std::string& profile, const fs::path& destination);
Value gpt_stock_plan(const StorageTarget& target, const fs::path& inputs, unsigned lun,
                     const std::string& profile, const fs::path& identity_backup = {}, const Root* system = nullptr);
std::vector<StorageRange> gpt_stock_plan_regions(const StorageTarget& target, const Value& plan);
Value gpt_backup(const StorageTarget& target, const fs::path& directory, const std::string& profile,
                 const Root* system = nullptr);
Value gpt_backup_verify(const fs::path& directory);
Value gpt_compare(const StorageTarget& target, const fs::path& directory, const std::string& profile,
                  const Root* system = nullptr);
Value gpt_plan(const StorageTarget& target, const std::string& operation, const std::string& profile,
               const fs::path& backup = {}, const Root* system = nullptr);
Value gpt_execute(StorageTarget& target, const Value& plan, const fs::path& journal,
                  const std::string& confirmation, const Root* system = nullptr);
Value gpt_rollback(StorageTarget& target, const fs::path& journal, const std::string& confirmation,
                   const Root* system = nullptr);
Value gpt_journal_inspect(const StorageTarget& target, const fs::path& journal,
                         const Root* system = nullptr);
Value gpt_resume(const StorageTarget& target, const fs::path& journal, const std::string& confirmation,
                 const Root* system = nullptr);
Value filesystem_probe(int fd);
Value filesystem_probe_range(int fd, std::uint64_t offset, std::uint64_t bytes);
Value partition_map(const StorageTarget& target, const Root* system = nullptr);
std::uint64_t layout_size_bytes(const std::string& amount, const std::string& unit, std::uint64_t pool);
Value partition_layout(const StorageTarget& target, const Value& request, const std::string& profile, const Root* system = nullptr);
Value partition_layout_bar(const Value& layout, unsigned width);
std::string partition_layout_text(const Value& layout);
std::vector<StorageRange> gpt_layout_regions(const StorageTarget& target, const Value& request,
                                            const std::string& profile, Value& source, const Root* system = nullptr);
Value gpt_layout_plan(const StorageTarget& target, const Value& request, const std::string& profile, const Root* system = nullptr);
struct DisplayLayout { float density; int canvas_width,canvas_height,percent; };
int display_scale_parse(const std::string& text);
DisplayLayout display_layout(int width,int height,double base_width,double base_height,int percent);
Value display_settings_load(const fs::path& directory);
Value display_settings_save(const fs::path& directory,int percent);
Value filesystem_check(int fd);
Value image_tool(const std::string& command, const std::string& operation, int fd);
Value gpt_inspect(int fd, std::uint32_t sector_size);
Value linux_detect(const Root& root, const Root* esp = nullptr);
Value linux_boot_audit(const Root& root, const Root* esp = nullptr);
Value linux_rescue_plan(const Root& root, const Value& request, const Root* esp = nullptr);
Value linux_rescue_execute(const Root& root, const Value& plan, const fs::path& journal,
                           const std::string& confirmation, const Root* esp = nullptr);
void filesystem_tree_gate(int fd);
void filesystem_tree_outside(int source, int destination);
Value descriptor_identity(int fd);
Value storage_preflight(const Root& system, const StorageTarget& target, const std::string& profile);
Value device_profile_admission_status(const Root& system, const std::string& requested_profile);
// Host declarations only; never live write admission or a physical receipt.
Value device_profile_compare_fixture(const Value& contract, const Value& observation);
Value filesystem_capabilities();
Value filesystem_operation_plan(const Root& system, const StorageTarget& target, const Value& request, const std::string& profile);
// Prepare and independently check a private replacement. Never write the source.
Value filesystem_prepare(const Root& system, StorageTarget& source, const Value& plan,
                         const fs::path& directory, const std::string& confirmation, OperationLease* parent = nullptr);
Value filesystem_operation_execute(const Root& system, StorageTarget& target, const Value& plan, const fs::path& journal, const std::string& confirmation);
Value filesystem_operation_recover(const Root& system, StorageTarget& target, const fs::path& journal, const std::string& operation, const std::string& confirmation = {});
Value filesystem_replacement_backup(const Root& system, const StorageTarget& target, const Root& staged, const std::string& file,
                                    const fs::path& destination, const std::string& profile, OperationLease* parent = nullptr);
enum class ReplacementOrigin { FilesystemTransformation, RecoveryImage };
Value prepared_replacement_backup(const Root& system, const StorageTarget& target, const Root& staged, const std::string& file,
                                  const fs::path& destination, const std::string& profile, ReplacementOrigin origin, OperationLease* parent = nullptr);
// Executable transaction model for private regular images only. Neither a
// declared firmware name nor a fixture slot establishes device admission.
Value recovery_install_prepare(const Root& system, const StorageTarget& target, const StorageTarget& fallback,
                              const Root& staged, const std::string& file, const Value& request, const fs::path& backup);
Value recovery_install_execute(const Root& system, StorageTarget& target, const StorageTarget& fallback,
                              const Value& plan, const fs::path& journal, const std::string& confirmation);
Value recovery_install_recover(const Root& system, StorageTarget& target, const StorageTarget& fallback,
                              const fs::path& journal, const std::string& action, const std::string& confirmation = {});
Value partition_job_plan(const Root& system, const StorageTarget& target, const Value& request, const std::string& profile);
Value partition_job_execute(const Root& system, StorageTarget& target, const Value& plan,
                            const fs::path& journal, const std::string& confirmation);
Value partition_job_recover(const Root& system, StorageTarget& target, const fs::path& journal,
                            const std::string& action, const std::string& confirmation = {});
// Canonical logical-content digests give holes and allocated zeros the same
// ordinary SHA-256 leaves. All image helpers require regular files.
std::string storage_image_range_digest(int fd, std::uint64_t offset, std::uint64_t bytes);
void storage_copy_image_range(int source, int destination, std::uint64_t offset, std::uint64_t bytes);
Value stock_image_inspect(int source);
Value stock_image_expand(int source, int fresh_private_destination, bool zero_sparse_holes);
Value stock_job_plan(const Value& request);
Value stock_job_execute(const Value& plan, const fs::path& journal, const std::string& confirmation);
Value stock_job_recover(const fs::path& journal, const std::string& action, const std::string& confirmation = {});
bool management_command(const std::vector<std::string>& args);
Value management_dispatch(std::vector<std::string> args);
Value btrfs_subvolume_info(const Root& root, const std::string& relative);
Value btrfs_snapshot_plan(const Root& root, const std::string& source, const std::string& parent, const std::string& name, const std::string& profile, const fs::path& store);
Value btrfs_snapshot_execute(const Root& root, const fs::path& store, const std::string& confirmation);
Value btrfs_send_plan(const Root& root, const std::string& source, const std::string& parent, const std::string& profile, const fs::path& store);
Value btrfs_send_capture(const Root& root, const fs::path& store, const std::string& confirmation);
Value btrfs_backup_inspect(const fs::path& store);
Value btrfs_send_verify(const fs::path& store);
Value btrfs_stream_check(int fd);
Value btrfs_native_info(const Root& root, const std::string& operation);
Value btrfs_manage_plan(const Root& root, const Value& request, const std::string& profile);
Value btrfs_manage_execute(const Root& root, const Value& plan, const fs::path& journal, const std::string& confirmation);
Value btrfs_manage_control(const Root& captured_root, const Value& running_plan, const fs::path& running_journal,
                          const std::string& action, const std::string& confirmation);
Value windows_detect(const Root& root, const Root* esp = nullptr);
Value config_validate(const Root& root, const std::string& file, const std::string& kind);
Value files_list(const Root& root, const std::string& directory);
Value files_search(const Root& root, const std::string& directory, const std::string& pattern);
Value diagnose(const Root& system, const std::string& scope);
Value public_report(const Root& system);
Value capabilities(const Root& system);
Value boot_targets(const Root& root, const Root* esp);
Value boot_request(const Root& root, const Root& esp, const std::string& target,
                   const std::string& entry);
// Real EFI inventory is read-only. Mutations require private file fixtures.
Value boot_route_inventory(const Root& esp, const Root& variables);
Value boot_route_plan(const Root& esp, const Root& variables, const Value& request);
Value boot_route_execute(const Root& esp, const Root& variables, const Value& plan,
                         const fs::path& journal, const std::string& confirmation);
Value boot_route_history(const fs::path& journal);
Value boot_route_action(const Root& esp, const Root& variables, const fs::path& journal,
                        const std::string& action, const std::string& confirmation = {}, const Value& receipt = {});
Value transaction_plan(const Root& root, const std::string& file,
                       const std::string& new_contents, const std::string& firmware);
void validate_plan(const Root& root, const Value& plan);
Value transaction_run(const Root& root, const Value& plan, const fs::path& journal,
                      const std::string& confirmation);
Value transaction_rollback(const Root& root, const fs::path& journal,
                           const std::string& confirmation);
Value transaction_inspect(const Root& root, const fs::path& journal);
Value transaction_resume(const Root& root, const fs::path& journal, const std::string& confirmation);
Value transaction_cancel(const Root& root, const fs::path& journal, const std::string& confirmation);
Value transaction_list(const Root& root, const fs::path& directory);
Value backup_file(const Root& root, const std::string& relative, const fs::path& destination, OperationLease* parent = nullptr);
Value backup_tree_plan(const Root& root, const std::string& relative, const std::string& profile, const fs::path& directory);
Value backup_tree_capture(const Root& root, const fs::path& directory, const std::string& confirmation);
Value backup_tree_verify(const fs::path& directory);
Value backup_tree_inspect(const fs::path& directory);
Value backup_tree_restore(const fs::path& directory, const fs::path& destination, const std::string& confirmation);
Value backup_tree_recover(const fs::path& directory, const fs::path& destination, const std::string& restore_plan_record,
                          const std::string& action, const std::string& confirmation = {});
Value backup_plan(const Root& root, const std::string& relative, const std::string& profile,
                  std::uint64_t chunk_bytes = 16 * 1024 * 1024);
Value backup_storage_plan(const Root& system, const StorageTarget& source, const std::string& profile,
                          std::uint64_t chunk_bytes = 16 * 1024 * 1024);
Value backup_capture(const Root& root, const Value& plan, const fs::path& directory, bool resume);
Value backup_verify(const fs::path& directory);
void backup_export(const Root& root, const Value& plan, std::uint64_t index, int output_fd);
void backup_store_export(const fs::path& directory, std::uint64_t index, int output_fd);
Value restore_stream_plan(const Root& system, const StorageTarget& target, const Value& desired,
                          const std::string& profile);
Value restore_stream_backup_plan(const Value& plan);
Value restore_host_receipt(const Value& plan, const fs::path& before, const fs::path& after);
Value restore_receipt_input(int input_fd);
Value restore_stream_begin(const Root& system, const StorageTarget& target, const Value& plan,
                           const Value& receipt, const fs::path& journal, const std::string& confirmation);
Value restore_stream_status(const Root& system, const StorageTarget& target, const fs::path& journal);
Value restore_stream_chunk(const Root& system, StorageTarget& target, const fs::path& journal,
                          std::uint64_t index, int input_fd, const std::string& confirmation);
Value restore_stream_finish(const Root& system, const StorageTarget& target, const fs::path& journal,
                           const std::string& confirmation);
Value restore_stream_rollback(const Root& system, const StorageTarget& target, const fs::path& journal,
                             const std::string& confirmation);
Value restore_stream_cancel(const Root& system, const StorageTarget& target, const fs::path& journal,
                           const std::string& confirmation);
Value restore_plan(const Root& system, const StorageTarget& target, const fs::path& backup,
                   const std::string& profile);
Value restore_execute(const Root& system, StorageTarget& target, const Value& plan,
                      const fs::path& journal, const std::string& confirmation, const Root* retained_parent = nullptr, OperationLease* parent = nullptr);
Value restore_inspect(const Root& system, const StorageTarget& target, const fs::path& journal);
Value restore_resume(const Root& system, StorageTarget& target, const fs::path& journal,
                     const std::string& confirmation, OperationLease* parent = nullptr);
Value restore_rollback(const Root& system, StorageTarget& target, const fs::path& journal,
                       const std::string& confirmation, OperationLease* parent = nullptr);
Value restore_cancel(const Root& system, const StorageTarget& target, const fs::path& journal,
                     const std::string& confirmation, OperationLease* parent = nullptr);
class Editor {
    std::string original_, text_, path_, profile_;
    Value original_identity_;
    std::vector<std::string> undo_, redo_;
    void change(const std::string& value);
public:
    Editor(const Root& root, std::string path, std::string profile);
    const std::string& text() const { return text_; }
    std::vector<std::string> lines() const;
    void line(std::size_t index, const std::string& value);
    void insert(std::size_t index, const std::string& value);
    void erase(std::size_t index);
    void undo();
    void redo();
    std::size_t replace(const std::string& find, const std::string& replacement);
    Value plan(const Root& root) const;
};
bool utf8(std::string_view text);
Value tool_operation(const std::vector<std::string>& args, const Root& root, const Root& system);
int dispatch(std::vector<std::string> args);
} // namespace ure
