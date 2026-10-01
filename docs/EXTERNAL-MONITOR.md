# USB-C monitor, mouse and keyboard

Open **Settings → USB monitor, mouse and keyboard**, or the same URE menu
entry. Connect one monitor to the hub's HDMI port and a USB HID mouse/keyboard
to its USB-A data ports. Automatic mirroring starts enabled. The enable and
disable controls affect the external output independently of the tablet panel.

Choose resolution and refresh rate, then **Apply selected output mode**.
Resolutions include 1280×720, 1600×900, 1920×1080, 2048×1080 and 2560×1440;
rates include automatic, 30, 50, 59.94, 60 and 75 Hz. For QHD75, select
**2560×1440** and **75 Hz**. The active line shows the applied mode separately
from the selection. **Show monitor's supported modes** lists the reported
progressive modes within recovery limits. Choices last for this recovery session.

Existing EDID timings are used without synthesis or overclocking. A 60 Hz
selection may use 59.94 Hz. An unavailable selection or failed DRM test/commit
retains the existing external mode. Without an active output, the reason is
reported. Automatic selection prefers a mode within 1080p60 where available;
explicit choices support progressive modes through 2560×1440 at nominal 75 Hz.

The complete recovery canvas and cursor are rotated like the GUI and fitted
with black borders to preserve aspect ratio. Interface scale also affects the
mirror. Both screens share GUI/pointer coordinates. Selected Hz controls
monitor scanout; recovery content updates are limited to about 30 frames/sec.
Idle canvases are not recopied, coordinate columns and DRM property IDs are
cached, and connector probes are limited to once per second.

Mouse controls are movement, left click/drag, right-click/side-button back and
wheel movement of focused list/scroll selection. Keyboard text uses the
upstream US layout; Esc goes back. **F6** toggles menu navigation: arrows/Tab
move focus, Shift+Tab moves backwards, Enter selects once. F6 returns to typing.
Unplugging or dropped evdev queues cancel held key/drag state without generating
an action-producing release. Pointer positions stay within valid pixels.

An unused CRTC and format-compatible unused plane on the renderer's existing
DRM fd carry the mirror. The panel connector, CRTC and reserved planes are
excluded. Initial/changed modes pass TEST_ONLY atomic commits with the panel
active. Mode changes prepare new buffers while old scanout remains intact;
old resources are released after successful activation. Failed disable retains
resources and reports failure. Screen blanking also blanks the mirror.
Render-thread processing owns resources; action threads publish preferences.

## Evidence and acceptance

Xiaomi's [Pad 7 FAQ](https://www.mi.com/global/support/faq/details/KA-537666/)
states DP 1.2 wired video and OTG support. Its
[adapter guidance](https://www.mi.com/uk/support/faq/details/KA-541817/)
lists USB-C HDMI/DP and multiport adapters. These product capabilities do not
prove recovery output.

The supplied test hub is **Juo JH925**, according to its
[product listing](https://www.maxitekno.com/juo-type-c-9-portlu-dual-4k-60hz-hdmi-mst-gigabit-ethernet-donusturucu-100w-pd-4usb-32-coklayici-hub).
The listing describes HDMI, USB-A data, USB-C data/PD and Ethernet; its dual
4K claim requires DP 1.4. This implementation targets one external monitor.
Dual-output MST, 4K, Ethernet, HDMI audio, hub PD, DisplayLink and Bluetooth
acceptance remain separate work. No new USB role, charging limit, calibration,
firmware or storage write is performed by these controls.

Pinned Uke OEM display DTS enables `sde_dp`, its `wcd_usbss` AUX switch and the
MDSS DP connector. The verified Global GKI config has evdev, generic/USB HID,
xHCI, DWC3 dual-role and Type-C DP Alt Mode built in. Qualcomm display is
supplied through OEM modules; the stock vendor ramdisk contains `msm_drm`, DP
helpers and USB PHY/switch modules. Module loading, Type-C negotiation, hotplug,
simultaneous HID and QHD75 scanout on the tablet still require physical tests.

Host tests compile the actual native sink with fake DRM IOCTLs and exact
keyboard/evdev functions with platform stand-ins. Rotations/padding, QHD75,
EDID refusal, failed mode changes, occupied resources, reconnect, blanking,
on/off, idle/cached paths, keyboard actions and input hotplug are covered.
They are software evidence. Android, extracted ramdisk and QEMU CLI checks
are separate; no GPU/USB/monitor success record has been created.
