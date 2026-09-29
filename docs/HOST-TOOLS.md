# Pinned host tools

Android's official `repo` client is required to resolve and synchronize the multi-repository OrangeFox Android 16 manifest. It is an upstream Python program and runs **only on the development host**. The project-owned download, verification and report tools are Bash. No `repo` executable, Python interpreter, Python script or Python runtime library is packaged for the tablet.

The host client is archived as `android-repo` in the workspace source catalog at the peeled `v2.67` commit `d27d6829a84f488b7253ea693dcc429076c33914`. Its Git bundle and offline restore are verified separately. The synchronized Android source checkout is a build input, not a set of independent offline archives. Its 399 exact project revisions are recorded in `manifests/orangefox-android16-uke.lock.xml`; projects carrying an upstream `clone-depth` remain shallow by design and the lock records reproducibility at the checked-out commits rather than claiming complete history.

`unpack_bootimg` and `mkbootimg` are AOSP host tools for inspecting and constructing boot images. Their archived AOSP sources are pinned in the workspace. They are also excluded from tablet payloads. A future build container will pin actual binary versions and package closure; merely having a host executable on the machine does not satisfy that gate.
