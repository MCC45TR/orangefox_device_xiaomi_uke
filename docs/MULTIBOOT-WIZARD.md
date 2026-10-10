# Setup Multiboot

Open **Extra → Setup Multiboot**. The wizard first inspects the current layout.
An existing multiboot layout offers **Change Multiboot** or **Restore Default**;
a stock layout offers setup. Nothing is changed during these checks.

Choose ESP, linux_boot, linux, windows, shared, linux_swap and linux2 as needed.
Userdata is retained by default; omitting it is an explicit advanced choice.
All allocations use the original userdata extent. Factory partitions outside
that extent remain protected. An existing layout needs the original GPT backup
from the same target before its complete pool or default restoration is accepted.

Sizes accept MB, MiB, GB, GiB or a percentage of the original pool. The keyboard
opens for size entry. The aligned minimums are:

| Role | Minimum | Default filesystem |
|---|---:|---|
| userdata, when retained | 64 GiB | F2FS |
| esp | 512 MB; maximum 4 GiB | FAT32, fixed |
| linux_boot | 512 MB | ext4 |
| linux / linux2 | 40 GiB each | ext4, recommended |
| windows | 50 GiB | NTFS, fixed |
| linux_swap | 1 GiB | Linux swap, fixed |

Advanced mode enables role names and ordering. Ordinary setup uses the canonical
order and skips that page. Other roles allow filesystem selection; a displayed
filesystem is usable only when its formatter and checker are packaged and
accepted. ZFS, XFS and swap adapters are not currently available.

The review compares the old and proposed layouts, shows sizes, filesystems and
the commands, and lists blockers. Changing a selection invalidates the review.
The final screen requires the displayed phrase in the current recovery language;
the native operation also verifies the complete plan digest. Default restoration
is restricted to the original userdata pool and must not be confused with a
whole-device firmware restore.

**Current limitation:** live repartitioning is unavailable. A physical-tablet
preview must not display an Apply action until its storage, encryption, slot,
ownership and recovery-route prerequisites pass. Disposable regular-image
execution uses the existing filesystem/GPT journal, readback and rollback.
Those image tests do not qualify tablet application. Android encrypted-data
access is also unavailable; this wizard does not pretend to preserve encrypted
files by shrinking only their GPT boundary.

The [shell dualboot interface](DUALBOOT-SETUP.md) is a separate earlier five-role
interface. For the default recovery backup selections and private raw-image
transfer, see [host partition backups](HOST-PARTITION-BACKUP.md).
