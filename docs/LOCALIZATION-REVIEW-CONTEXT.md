# Language changes and operation review

A recovery plan contains machine values, paths and an exact digest. Its review
screen also contains warnings whose language matters to the person confirming
the operation. Changing the interface language therefore requires reviewing the
current plan or recovery journal again before a mutation can proceed. The
native request, journal bytes and digest are not translated or rewritten.

The actual management adapter now captures `tw_language` with the immutable
worker inputs. A completed job from the previous language cannot publish its
plan, confirmation hash or buttons into the changed view. A session that sees a
new language clears its reviewed plans and journal-action flags before opening
the target; mutation commands refuse until their normal review requirements
are met again. This includes the distinct `pt_BR` and `pt_PT` selections.

Controls for an already captured Btrfs backend also require a review in the
current language. They continue to address the original backend and journal,
rather than a newly selected root. The owned job cancellation route remains
available during a language change. Workers never read or write GUI variables.

## Reproduce the scoped controls

```sh
bash tests/check-gui-language.sh native
bash tests/check-gui-language.sh sanitizer
```

These resource-isolated producers execute four actual callback controls using
host GUI variables, private regular images, a real verified image backup and
explicit backend syscall stand-ins. Frozen before/after source manifests,
compiler/executable identities and the complete CTest catalog are retained
privately. Only the four named tests are accepted by this focused producer.

The original stale-language result was reproduced before the change. The
corrected normal and instrumented controls cover stale completion, regional
language changes, filesystem review, exact Btrfs control and a successful fresh
review. Separate copies of the actual hook generator also compiled and ran
the reviewed GUI change without the owner's unrelated uncommitted overlay.

This is the first AUD-031 correction. Translation source/context/parent
provenance and competent semantic review remain pending; translation import and
materialization commands remain disabled. Preserving placeholders cannot prove
the meaning of a warning. Full Android build, shipping GUI, combined VM,
complete current test catalogs and tablet acceptance remain separate.
