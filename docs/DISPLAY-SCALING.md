# Tablet interface scale

Open **Settings → Interface scale for tablet**, or the same entry in the Uke
Recovery Environment menu. Choose 50, 60, 70, 75, 80, 90 or 100 percent, or enter
any whole percentage from 50 to 100. The tablet default is 75 percent; selecting
that preset resets the setting. Smaller percentages reduce text, icons, row
dimensions and their touch rectangles together. The current percentage appears
in the page title. This is a recovery interface setting, not an Android display
density or framebuffer-resolution change.

The theme originally scales horizontal and vertical dimensions independently.
The project hook uses its existing minimum density (also used for fonts), then
applies the selected percentage uniformly. Full-screen widths, bottom anchors,
centers and input fields use a logical canvas derived from the actual rotated
framebuffer. Touch input retains framebuffer coordinates; rendered objects
and their hit rectangles use the same existing scaling functions. No additional
inverse touch transform is applied. Rotation and brightness are independent.

Applying a scale defers resource rebuilding to the render thread and returns to
the scale page. The density reload uses the built-in reviewed theme, with no
upstream settings flush, userdata mount, custom-theme lookup or calibration
write. A failed reload retries the previously applied percentage. If that also
fails, the result explicitly requests a recovery restart rather than claiming
that the old interface was restored. Reload requests use atomic flags.

The active setting is held in memory until explicitly saved. In **Custom
percentage and settings directory**, select a dedicated directory on an already
mounted Linux or external filesystem. **Save current scale** creates a private
0700 directory if needed and an atomic, synced 0600 `display.json`, then reads
it back. Loading validates the schema, percentage, ownership, permissions and
single-link regular-file identity. Calibration, Android userdata and metadata
roots and their separately mounted filesystem aliases are refused. These
commands never mount storage. A record on tmpfs or ramfs is reported as volatile.

At startup, a valid record in an already accessible `/mnt/uke-settings` is used.
If the storage is unavailable or the record is invalid, the tablet default is
used. The loader does not mount a volume or search Android storage. For another
directory, mount it through the separately reviewed storage workflow and use
**Load scale** after each boot. Saving a record does not make its volume
automatically available on future boots.

```sh
uke-recoveryctl display preview 2136 3200 1080 3200 75
uke-recoveryctl display settings-save /mnt/uke-settings 75
uke-recoveryctl display settings-load /mnt/uke-settings
```

Preview dimensions are explicit test inputs, not a fresh tablet measurement.
Host tests compile the actual project density/variable hooks and reviewed
upstream text, coordinate and deferred-reload functions with platform stand-ins.
They cover portrait/landscape presets, bottom-row rectangles, percentage labels,
failed reload recovery, private settings and absence of mount/flush calls.
Android compilation, extracted AArch64 CLI fixtures, graphical rendering and
physical touch acceptance are separate evidence classes. This feature does not
close outstanding GUI rendering or hardware gates.
