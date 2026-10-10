# Tablet interface scale

Open **Settings → Interface scale for tablet**, or **Extra → Display and input**.
Choose Very small (55%), Small (65%), Medium (75%), Large (85%) or Very large
(95%). **Custom scale** offers 50, 55, 60, 65, 70, 75, 80, 85, 90, 95 and 100
percent. The tablet default is 75 percent. Select a size and inspect its text,
icon-gap and button preview, then press **Apply**. Resetting or loading selects
a size for review. At 70 percent or below, a warning explains that small touch
targets can be difficult to tap and suggests a mouse.
Smaller percentages reduce text, icons, row
dimensions and their touch rectangles together. The current percentage appears
above the preset cards beside the selected size. This is a recovery interface setting, not an Android display
density or framebuffer-resolution change.

The theme originally scales horizontal and vertical dimensions independently.
The project hook uses its existing minimum density (also used for fonts), then
applies the selected percentage uniformly. Full-screen widths, bottom anchors,
centers and input fields use a logical canvas derived from the actual rotated
framebuffer. Touch input retains framebuffer coordinates; rendered objects
and their hit rectangles use the same existing scaling functions. No additional
inverse touch transform is applied. Rotation and brightness are independent.

The built-in OrangeFox theme also derives its status-bar battery/clock edge,
trailing toolbar buttons, content/console widths, file-search background and
fields, right-side gesture strip and navigation slots from that viewport.
The five navigation targets span the full panel; their compact selection pills
stay centered on the same targets. Credits tabs use thirds of the available
width. Centered controls keep their relative offsets; ordinary text, icons,
spacing and vertical rows retain uniform density. These are explicit reviewed
theme anchors, rather than a general rewrite of every stored numeric variable.

Applying a scale defers resource rebuilding to the render thread and returns to
the scale page. The density reload uses the built-in reviewed theme, with no
upstream settings flush, userdata mount, custom ZIP-theme lookup or calibration
write. A failed reload retries the previously applied percentage. If that also
fails, the result explicitly requests a recovery restart rather than claiming
that the old interface was restored. Reload requests use atomic flags.
Package recreation starts directly on the scale page, so main-page reboot,
ADB/MTP and startup actions are not replayed. Existing URE selections and
review fields survive theme-variable loading; the original future start-page
route is restored after the reload.

The preview scales reference-counted theme fonts relative to the applied size
and redraws only when the selection changes. It does not rebuild the theme,
change touch coordinates or make its sample button interactive. Derived fonts
are released on replacement, invalid input and page destruction.

Applying a size schedules automatic persistence with the other validated UI
preferences. **Saved recovery preferences** shows the managed path and storage
result; there is no editable folder selector. **Save the applied size** retries
a durable save, while **Load a saved selection** loads a size for review before
Apply. See [recovery appearance preferences](RECOVERY-SERVICES.md#recovery-appearance-preferences)
for the bounded, private `persist` store and its calibration protections.

Startup restores a valid preference record before the first resource layout.
Unavailable or invalid storage keeps the tablet default and reports that
changes apply to the current session. Host fixtures cannot establish persistence
across a tablet restart.

The following directory-based CLI commands remain available for controlled
external/Linux fixtures. They do not configure the GUI's automatic preference
store, perform a mount, or make an external directory accessible on later boots.

```sh
uke-recoveryctl display preview 2136 3200 1080 3200 75
uke-recoveryctl display settings-save /mnt/uke-settings 75
uke-recoveryctl display settings-load /mnt/uke-settings
```

Preview dimensions are explicit test inputs, not a fresh tablet measurement.
Host tests compile the actual project density/variable hooks and reviewed
upstream text, coordinate and deferred-reload functions with platform stand-ins.
They cover portrait/landscape presets, bottom-row rectangles, percentage labels,
failed reload recovery, private records and scheduling without storage I/O in
the density/reload hooks.
They additionally load the real stock theme variables and check physical status
and toolbar edges, navigation target separation and pill alignment, content
widths and fixed-coordinate regressions in the patched XML. URE model/profile,
paths and JSON defaults remain literal strings; upstream geometry arithmetic
still applies to theme coordinates.
Android compilation, extracted AArch64 CLI fixtures, graphical rendering and
physical touch acceptance are separate evidence classes. This feature does not
close outstanding GUI rendering or hardware gates.

The gesture indicator is centered vertically in the reserved bottom navigation
area. Its home-swipe region covers that area without overlapping the five menu
targets. The stock and customized splash templates declare full-viewport centers
and dimensions before placing their logo and captions. HID keyboards and relative
mice keep their report synchronization separate from touchscreen translation;
mouse sync packets cannot synthesize a finger-up event that cancels a click.

Direct XML placement also resolves the whitelisted viewport geometry before
looking up immutable OrangeFox constants. This keeps vertical centers accurate
without rewriting the constant store. Optional back/home/console buttons occupy
three equally spaced cells with disjoint hit bounds. Action-only menus start at
their first entries; lists with a stored value retain selection scrolling.

**Monitor scale and output** opens the independent monitor settings. Select an
image size from 50 through 100 percent, resolution and refresh rate, then apply
them together. The renderer preserves aspect ratio, centers the image and fills
unused pixels with black; the tablet interface size does not change. An idle
frame is redrawn when only the monitor size changes. An unsupported output mode
keeps the active resolution, and the requested size is distinguished from the
reported active mode. Actual dock/monitor mode acceptance remains untested.

Automatic display settings report rotation and brightness as unavailable in
this recovery build and link to OrangeFox's manual screen controls. The observed
stock sensor descriptors and registry defaults are readiness evidence; see
[sensor readiness](SENSOR-READINESS.md).
