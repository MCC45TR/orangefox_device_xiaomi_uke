# Encrypted storage and sensor readiness

`uke-recoveryctl services status` and the Extra menu expose a bounded,
read-only inventory of decryption prerequisites, thermal observations and
automatic-display readiness. A tool being present does not authorize an unlock
or prove that an installed ROM's hardware services work.

## Credentials and session ownership

The native decryption session supports separate Android FBE and Linux LUKS
providers. It requires an accepted provider with a retained generation and
read-only access before consuming any credential. Production providers must
retain verified target and firmware identities, operation leases and teardown
ownership. There is no environment variable or imported JSON that can register
an accepted provider.

Credentials arrive through a private nonblocking pipe or an owned, immutable,
sealed anonymous descriptor. Named password files, command-line arguments and
JSON are not credential transports. Input is bounded to 4096 bytes, stored in
locked memory and cleansed when the call ends. Provider messages cannot expose
the credential in a public error. Authentication retry delays are enforced;
there are no automatic retries. An uncertain authentication or failed cleanup
quarantines the session until its retained provider completes teardown.

**Android FBE and Linux LUKS unlock are currently unavailable in the shipping
profile.** Android needs a matched and accepted KeyMint/TEE, metadata-key and
fscrypt chain. Linux needs the integrated read-only mapping/lease/cleanup
adapter. The new session and host fixtures do not establish either physical
capability. The UI inspects readiness without requesting a password until those
adapters are accepted. BitLocker remains outside the selected scope.

## Thermal observations

The inventory reads bounded thermal type/value records. Reviewed CPU types
`cpu_therm` and numeric `cpuss-*` use signed millidegrees Celsius. Other zones
retain their raw driver value and an unverified unit. Qualcomm BCL current and
voltage proxies must not be mislabeled as temperatures. Missing, malformed or
inaccessible records remain unavailable; no magnitude heuristic fabricates a
Celsius value. The inventory does not change calibration or thermal policy.

## Automatic brightness and rotation

The native policy consumes fresh, sequenced observations from one reviewed
provider generation. Light samples require finite, bounded lux values and use
smoothing, hysteresis and a minimum adjustment interval. Acceleration uses
calibrated SI values in the reviewed natural-display coordinate system;
rotation requires at least 750 ms of acquired stable orientation. Flat-tablet,
ambiguous, stale, repeated and nonfinite samples do not trigger rotation.
Manual input suspends automatic decisions for five seconds.

The policy emits decisions; it does not operate the panel or rendering thread.
An accepted SSC/HAL stream, orientation transform, panel brightness adapter and
render-thread rotation adapter are still required. Current source reports
automatic controls as unavailable and keeps the existing manual controls.
Host sensor fixtures are separate from VM integration and tablet acceptance.
