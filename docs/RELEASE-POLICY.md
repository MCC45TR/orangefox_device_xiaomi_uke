# Explicit recovery release policy

Candidate names identify directories. They do not select tests or weaken safety
requirements. Every new package requires an explicit request, normalized against
[the reviewed policy](../configs/release-policy.json). The normalized policy is
bound to that candidate and included with its build completion in the package.
Changing its class or capabilities requires a new candidate directory.

| Class | Required evidence | Scope |
|---|---|---|
| `experimental` | Accepted fresh build, extracted-image audit, complete matching native/sanitizer catalogs and repeated package | Engineering candidate; no system-VM review claimed |
| `vm-reviewed` | Experimental requirements plus Btrfs ioctl, partition interruption, adapted GUI, stock namespace and write-gate VM receipts | Exact tested userspace/ELFs and identified generic guest/adapters |
| `function-reviewed` | VM-reviewed requirements plus core, filesystem, Linux-rescue and Btrfs shipping-CLI guest groups | Exact identified generic guest; complete device feature acceptance remains false |

There is no device-validated class. All classes retain false physical-device,
shipping-kernel and complete-feature acceptance. FBE, live storage, stock return,
boot routing, sensors and accessories keep their separate capability blockers.
VM receipts cannot accept either tablet model or its preserved stock kernel.

The ten capability domains describe the features present in this build. A
request must include all ten exactly once. Omitting a shipped feature cannot
avoid its review. Requirements are computed from class and capabilities, sorted
and bound to the complete policy digest. Supplied requirement lists, unknown
classes/features, duplicates and added request fields refuse. The policy is a
reviewed source input, not a runtime override or authenticated trust root.

## Collecting and sealing evidence

The following commands describe a future candidate after the ordered P1 changes
and a fresh accepted build. They do not upgrade historical receipts.

```sh
mkdir -p build/release-plans
bash scripts/release-policy.sh plan function-reviewed > build/release-plans/candidate.json
bash scripts/check-package-repeat.sh NEW-CANDIDATE build/release-plans/candidate.json
bash scripts/audit-recovery-image.sh artifacts/NEW-CANDIDATE/OrangeFox-uke-recovery.img artifacts/NEW-CANDIDATE/EXTRACTED-RAMDISK-AUDIT.json --qemu
bash scripts/archive-release-sources.sh NEW-CANDIDATE
```

Build the optional uninstalled VM fixture for classes requiring Btrfs ioctl
evidence. Run `tests/run-native.sh` and `tests/check-sanitizers.sh` against the
same unchanged inputs. Collect the exact guest receipts documented in
[functional tests](FUNCTIONAL-VM-TESTS.md),
[write-gate tests](WRITE-GATE-VM-TESTS.md),
[stock namespace](STOCK-VM-FIXTURE.md) and the existing VM review. Then seal:

```sh
bash scripts/describe-prerelease.sh NEW-CANDIDATE
```

`release-policy.sh preflight` checks required files, regular-file identity and
JSON parsing only. It cannot accept a build or guest result. The manifest driver
separately checks actual source inputs, build/service/payload binding, native CLI,
complete CTest names, sanitizer compiler/options and each applicable VM receipt's
exact runners, ELFs, adapters, scope and functional predicates. Required receipt
digests and the normalized policy are recorded in manifest schema 3.

Native and sanitizer producers record sorted test names and counts. Manifest
acceptance compares both to the current configured catalog, replacing historical
hardcoded counts. Matching counts alone cannot substitute different tests. New
source changes invalidate both receipts. The current host policy controls do not
constitute a fresh complete native/sanitizer run or an Android image build.

[Localization inventories](LOCALIZATION-EVIDENCE.md) bind all supported language,
font, notice and theme inputs to build/native/payload/GUI evidence. Packages
carry their source and actual resource inventories. Current source or resource
changes invalidate older results; recording their identities does not accept
glyph coverage, translation meaning or font licensing.

`check-package-repeat.sh` compares two packages from one accepted build and
records both policy and completion identity. This is package repeatability,
not independent clean-build reproducibility. A package cannot borrow a repeat
result for different content, policy or build identity. Source archives remain acquisition/rebuild
material; they do not provide all 399 repositories' offline dependency closure.

## Historical and failed candidates

Package, manifest, source-archive and repeat drivers refuse existing seals.
Image-audit reports and build-completion exports also refuse paths inside sealed
candidate directories, including resolved aliases. Indirect/non-file entries
in a writable candidate refuse before packaging effects. These are cooperating
host-tool protections; filesystem owners can still edit files directly.

The existing public alpha is unchanged. Its historical source, manifest and
test scope remain historical. Missing or mismatched current evidence must lead
to a new candidate and new tests. A local content digest is not an authenticated
update: signed manifest/payload verification and production rollback trust are
separate AUD-035 work.
