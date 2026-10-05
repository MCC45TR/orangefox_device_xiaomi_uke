# Offline translation provenance

`uke-locale-review` prepares and validates offline translation work against the
current recovery sources. A receipt's own hash or `reviewed` claim cannot
establish source identity or semantic approval. The old catalog translation
commands remain disabled. No network service, unofficial provider parser,
translator runtime or Python dependency is added to recovery.

## Current-source binding

The tool regenerates the catalog from current English and supported-language
XML, maintainer pages, native messages and collection/validation code. The
supplied catalog must match it. Source hashes, source-file membership,
page/node and native-line contexts, exact locale and target metadata belong
to the work identity. Editing an input/context/tool or adding a native source
invalidates previous jobs. Regional Portuguese selects distinct `pt-BR` and
`pt-PT` metadata; these labels do not claim external provider support.

Each deterministic job has at most 32 source items and a 256 KiB item budget.
Conflicting sources for one missing key refuse. Matching sources merge ordered
contexts. Compact missing-source references resolve against the exact catalog's
unique rows. An immutable private catalog snapshot computes its canonical
digest once; it cannot trust a mutable caller's self-declared digest.

JSON is bounded to 32 MiB and rejects duplicate fields, comments, trailing
data, excessive depth, invalid UTF-8 and special/indirect final file components.
XML is read from bounded regular bytes with network loading disabled; DTD
documents refuse. After validation and a second current-source check, a private
staged file is atomically published. Invalid work preserves prior output.
Output cannot replace its catalog, bundle, response or native source input.
Host synchronization uncertainty is separate from tablet durability.

## Offline commands

These are host-only `BUILD_TESTING` targets using pinned JsonCpp and host
libxml2/OpenSSL. They do not contact a translation service.

```sh
bash tests/check-localization-review.sh native
bash tests/check-localization-review.sh sanitizer
build/ure-host/uke-locale-catalog prepare . build/locale-review
build/ure-host/uke-locale-review jobs . build/locale-review/catalog.json pt_BR build/locale-review/pt-BR-jobs.json
build/ure-host/uke-locale-review split . build/locale-review/catalog.json build/locale-review/pt-BR-jobs.json 0 build/locale-review/children.json
```

`import COMPONENT CATALOG BUNDLE INDEX RESPONSE OUTPUT` validates the exact
expected job digest, locale and target. Each ordered translation supplies its
name, source/context digests and text. Missing, extra, reordered, stale and
foreign items refuse. UTF-8, byte and placeholder limits are independent.
Raw response bytes and canonical parsed response have separate digest fields.

`combine` takes the complete child-response set and regenerates every child from
the expected current parent. Parent, child index, source/context and exact
locale must correspond. A foreign child's self-consistent hash is insufficient.
Old `validated.json` receipts are not accepted. The real CLI tests cover jobs,
split, import and combine. All actual locale bundles undergo a JSON round trip;
canonical-byte comparison preserves all expected fields while avoiding
JsonCpp's internal signed/unsigned storage distinction for identical integers.

## Wording admission

Every import is an `unreviewed-draft`. Structural provenance and preserved
placeholders do not prove the language or meaning. The negative deliberately
replaces an erase warning with a preservation claim while retaining its native
placeholder. It cannot pass the shipping gate, including with a self-declared
review flag.

No trusted competent-review ledger is configured. Materialization refuses
before output publication. Competent warning review must bind exact source,
contexts, locale and translated bytes before enabling it. Future publisher
authentication belongs to AUD-035. The [language-context controls](LOCALIZATION-REVIEW-CONTEXT.md)
already require renewed operation review after changing language. Complete
Android/native/sanitizer/GUI/VM and tablet acceptance remain separate.
