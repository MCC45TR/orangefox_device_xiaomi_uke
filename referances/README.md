# Reference archive

This directory holds unchanged upstream clones and offline Git bundles. Large sources and private inputs remain local and are excluded from this repository.

The workspace catalog `manifests/sources.yaml` assigns each reference to one component. `manifests/sources.lock.json` records commit/tree identities, full-history verification, bundle checksums, offline restoration and missing dependencies. Run the workspace `scripts/sources.sh report` for current coverage.

A successful Git restore does not include missing submodule or LFS objects, prove a build, grant redistribution rights, or establish device support. Develop outside this directory. Never run archived partitioning or flashing scripts on a device.
