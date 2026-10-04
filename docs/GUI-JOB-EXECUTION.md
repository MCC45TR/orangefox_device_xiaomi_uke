# Owned management jobs

Long URE management actions run in one joinable native worker. Queue admission
is separate from backend completion: the action returns when its reviewed
inputs have been captured, and the GUI thread later collects one bounded result.
Display application and monitor requests remain short GUI actions. Loading or
saving display preferences uses the worker because it accesses mounted storage.

The worker owns a management session, its reviewed plans, the editor and retained
root descriptors. It reads a frozen variable map, never live DataManager values.
Its native I/O runs outside the GUI state mutex. An additional management job is
refused while that session is owned. Display controls, job status and stop requests
remain reachable. Completed results are collected by the render loop, including
while idle; active jobs shorten the existing idle-input wait to 50 ms.

## Results and changed selections

`Extra > Job status` exposes running state, the current phase, a stop request and
the final result. A completion is consumed once. Changing captured selections or
the explicit view epoch prevents its updates and reviewed plans from replacing
the current selection. The result remains inspectable, and confirmation hashes
and apply affordances are invalidated. This is deliberately conservative:
captured preferences include the current scale, so changing it can also require
a fresh management review.

Selected roots remain owned by the session; target identity and plan bindings
are revalidated by the existing native backend before effects. A frozen GUI
variable map is not a claim that several independent DataManager reads form an
atomic application-wide snapshot. A completion whose selection no longer matches
is discarded from the current view.

The adapter admits at most 512 KiB of captured variable text, with a 64 KiB limit
per field. The general executor checks escaped JSON bytes, finite numbers, valid
UTF-8 without NUL, 16 levels, 131,072 nodes and at most 64 retained descriptors.
Its input limit is 16 MiB and its single result mailbox is 256 KiB. Adapter
updates have a 192 KiB total text budget, 128 KiB per value and 256 keys. Results
that exceed publication limits are reported as failures; backend execution and
cleanup are never inferred from publication failure. Unpublished reviews are
invalidated. Editor previews preserve complete UTF-8 scalars at their boundary;
binary console records remain available through ADB.

Six-LUN stock review retains every programming extent, complete layout changes,
warnings, source observations and model/SKU limitations. Its GUI projection
replaces repeated nested GPT tables with their hashes and policy metadata. It is
explicitly marked as a display projection, not an executable sealed plan. The
complete original plan remains owned by the native session and is passed to the
existing executor unchanged.

## Stop requests and lifetime boundaries

`job-cancel` identifies the exact job and sets an advisory flag. It can prevent
entry at a reviewed safe checkpoint. A started native hashing, copy, filesystem
tool or rollback call may finish before the flag can stop it. The acknowledgement
is `ADVISORY_FLAG_ONLY`, with backend cleanup unverified. A returned worker is
not evidence of canceled kernel maintenance, empty namespaces, restored bytes or
retired durable ownership; inspect the native result and journal separately.

The previous detached scrub/balance worker has been removed. It runs inside the
same joinable executor, and concurrent shutdown callers wait for that owned
thread. This closes the detached publication path, but does not finish AUD-014.
The global lifecycle registry, orderly application teardown and separate GUI
controllers for exact-owner rescue/Btrfs cancellation remain to be connected.
Existing native exact-owner controls remain separate CLI interfaces. Do not
treat the GUI advisory flag as their acknowledgement or enable live writes.

## Host acceptance scope

Portable tests exercise the executor's input/descriptor ownership, explicit safe
checkpoint, result limits, invalid UTF-8, concurrent joins and stale epochs.
Actual OrangeFox management callbacks are compiled with host UI stand-ins.
Timed controls use real 512 MiB image hashing and backup plus 320 MiB ext4 resize
and rollback. They continue sampling status, acknowledge stop requests, reject a
second job, check single collection, detect worker access to GUI variables and
independently verify the backup, ext4 superblock size and original-image digest.

These are source and reference-host tests. They do not measure rendered frames,
tablet input latency, the shipping Android coordinator, six physical UFS LUNs or
forced-restart behavior. Fresh target, combined VM, visual, localization and
physical acceptance remain separate gates.
