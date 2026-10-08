# Installation

The reference system is Kubuntu/Ubuntu 26.04.1 x86_64 with systemd and fprintd.
Build and review as your normal user; only integration steps require `sudo`.
Keep a working password login while testing fingerprint authentication.

## 1. Check compatibility

```sh
uname -m
lsusb -d 27c6:538d
cat /etc/os-release
```

Require `x86_64` and USB ID `27c6:538d`. The hardware probe checks firmware
`GF5298_GM168SEC_APP_13016`; an identical USB ID alone is insufficient. Do not
flash different firmware or change the driver's firmware check to get past a
mismatch. This package builds only one fingerprint driver, so while active
other readers handled by the distribution libfprint will not be available to
fprintd.

Obtain a validated, nonzero **raw 32-byte key for your sensor**. See
[WINDOWS-KEY.md](WINDOWS-KEY.md). Hex text is not the correct format. The Linux
driver checks the supplied key against the sensor before attempting TLS. A
key from a different laptop will not work.

## 2. Download and install build dependencies

Extract the source release, then enter its `goodix-538d-linux` directory. If
using Git, clone the published repository and enter its root. All relative
commands below run from that root.

For Debian/Ubuntu package names:

```sh
sudo apt update
sudo apt install build-essential git patch pkg-config python3 meson ninja-build \
  libglib2.0-dev libgusb-dev libusb-1.0-0-dev libssl-dev \
  libcairo2-dev libxml2-utils appstream usbutils fprintd libpam-fprintd
```

The source requires GLib >= 2.68, OpenSSL >= 3.0 and Meson >= 0.59. Cairo and
AppStream enable additional upstream tests. Recent Ubuntu splits some GLib/GIO
development packages; APT resolves the dependencies of `libglib2.0-dev`.
Other distributions need equivalent development packages; their service/PAM
layout is not covered by this recipe.

Installing `libpam-fprintd` can update distribution-managed PAM choices; the
scripts in this repository do not edit PAM. Review your distribution's choices
and retain password authentication.

## 3. Build and run offline tests

```sh
./scripts/build.sh
```

Internet access is needed the first time to fetch the pinned upstream commit.
No key or sensor is needed to build. Do not run the build with `sudo`.

The script reconstructs `.work/source` from the pinned commit plus patch,
builds `.work/build`, runs synthetic tests with AddressSanitizer/UBSan and
upstream unit/data tests, compiles hardware probes, and stages artifacts in
`.work/stage`. `.work/tested.json` records artifact checksums. These detect
accidental changes; they are not a publisher signature or a trust boundary.
Repeated builds reconstruct generated sources; make driver changes in the
patch, not in `.work/source`.

Stop on a build/test error. Do not install a partially built tree or use
`sudo meson install`, which would bypass the integration checks.

## 4. Put your private key outside the repository

Transfer the key privately from Windows if necessary, then create a protected
Linux copy. Replace the source path below with the actual transferred file:

```sh
install -d -m 0700 "$HOME/.local/share/goodix-private"
install -m 0600 -- /absolute/path/to/your/recovered-key.bin \
  "$HOME/.local/share/goodix-private/existing.psk"
stat -c '%s bytes; mode %a; links %h' "$HOME/.local/share/goodix-private/existing.psk"
```

Expected: `32 bytes; mode 600; links 1`. Do not print the contents or its hash,
paste it into a command, or store it in the checkout. ACLs on NTFS do not
replace Linux file permissions; copy to a Linux filesystem before staging.

## 5. Stage the driver and key

```sh
sudo python3 scripts/manage.py stage \
  --key "$HOME/.local/share/goodix-private/existing.psk"
```

This creates `/opt/goodix-538d-existing`, with a root-owned mode 0700 private
directory and mode 0600 key. It validates the staged artifacts and key format.
It does not change fprintd or PAM. The installer refuses to overwrite an
existing installation or override; it also checks the sensor, systemd service,
and root-run service model.

If the target already exists from an earlier manual experiment, stop and
inspect that setup. The installer deliberately does not adopt it. Back up its
private key outside the installation and deactivate the old integration before
moving its directory aside. Do not delete a working setup simply to silence an
installer error. `update` is only for installations created by this package.

## 6. Test transport and check Windows

Remove your finger from the sensor. Close fingerprint settings/enrollment
tools. Then run:

```sh
sudo python3 scripts/manage.py probe
```

The probe temporarily masks/stops fprintd to obtain exclusive USB access,
checks firmware and the existing pairing hash, completes authenticated TLS,
and closes the sensor. It skips reset, OTP reading, MCU configuration and
fingerprint capture. It does issue the limited commands required to establish
transport; it is not a passive USB observation. It restores the runtime mask
and previously active service on ordinary success/failure. A pre-existing mask
is never overwritten. Timeouts are bounded.

If Windows is installed, reboot into Windows and confirm fingerprint unlock
still works, then shut down Windows normally and return to Linux. Do not
re-enroll or reset pairing to complete this checkpoint. Retain any disk unlock
recovery credentials needed for your normal dual-boot process. A failed
transport check is a reason to diagnose, not to flash/provision the sensor.

## 7. Activate fprintd integration

Keep all fingers off the sensor during these checks:

```sh
sudo python3 scripts/manage.py activate
```

Activation requires a successful transport probe. It runs normal
initialization with two open/close cycles, then enrollment cancellation at
250 ms and 1200 ms followed by another reopen. The cancellation probe saves no
template or image. Normal initialization resets runtime state, reads OTP and
uploads the driver's MCU configuration; it does not flash firmware or replace
pairing data.

The installer checks daemon symbol resolution, adds
`/etc/systemd/system/fprintd.service.d/90-goodix-538d-existing.conf`, starts
fprintd and checks `/proc/<pid>/maps` to confirm the intended library loaded.
An activation failure removes the newly created override. Staged files remain
for diagnosis. Firmware/key mismatches are not bypassed.

The override sets the library/key paths only for fprintd, disables core dumps
and debug image export, and uses umask 0077. The packaged library stays in
place. No global linker setting, kernel module, udev rule or PAM file is added.

## 8. Enroll and validate before enabling login

Run as your ordinary user:

```sh
fprintd-list "$USER"
fprintd-enroll -f right-index-finger "$USER"
fprintd-verify -f right-index-finger "$USER"
```

Enrollment needs 16 accepted samples. Use the same finger throughout, with
centered, stable contact and slight placement variation; fully lift between
touches when prompted. Very inconsistent placement gave unreliable templates
on the test laptop. Do not turn off matching checks to compensate.

Confirm repeatable `verify-match` results with the enrolled finger. Next run
the same verify command and deliberately use a different, unenrolled finger:
it should return `verify-no-match`. Finally verify the enrolled finger again.
A CLI session may retry after a mismatch; Ctrl+C ends it. Do not enroll the
negative-test finger before that check.

If re-enrollment is needed, run `fprintd-enroll` for that same finger again.
`fprintd-delete "$USER"` removes **all** of that user's Linux fingerprints,
so use it only when that is intended. Windows and Linux enrollments are
separate; this package does not import Windows templates.

## 9. Enable desktop/login authentication

On Debian/Ubuntu, after positive and negative checks pass:

```sh
sudo pam-auth-update
```

Select **Fingerprint authentication** if offered; keep the existing password
authentication entries enabled. Use the distribution tool rather than copying
PAM snippets from another distribution. Fingerprint authentication may already
be enabled, in which case no PAM change is needed. The package cannot guarantee
every display manager, sudo policy or desktop integration.

In KDE, lock with Meta+L and try your enrolled finger. Some Plasma versions
show an additional Unlock confirmation; Enter or a click completes it. See
[troubleshooting](TROUBLESHOOTING.md). Keep the current desktop session open
until password and fingerprint recovery paths have been verified.

After activation, test a reboot, suspend/resume, and Windows fingerprint
unlock after full Linux capture on your own machine. Report each separately;
they are not all covered by the original laptop's validation.

## Update

Check out/extract a new source release, build it as your normal user, then:

```sh
./scripts/build.sh
sudo python3 scripts/manage.py update
```

This requires this package's existing installation marker and unmodified
override. It retains the key and enrollments, backs up installed artifacts,
tests transport/reopen/cancellation, and verifies fprintd loads the update.
An ordinary failure restores previous artifacts. Do not interrupt power during
an update; emergency recovery steps are below.

A distribution upgrade may change the fprintd ABI. Deactivate this override
before a major OS upgrade, rebuild on the new OS and revalidate. This narrowly
built library does not follow distribution libfprint security updates
automatically; monitor upstream and review/rebase updates.

## Rollback and removal

To restore the distribution service:

```sh
sudo python3 scripts/manage.py deactivate
```

This removes only this package's exact override. It retains the private key,
staged installation, calibration and enrolled prints. It does not undo any
PAM choices you made with `pam-auth-update`; use that tool if needed.

If the checkout is unavailable, manually remove **only this package's drop-in**:

```sh
sudo systemctl stop fprintd.service
sudo rm -- /etc/systemd/system/fprintd.service.d/90-goodix-538d-existing.conf
sudo systemctl daemon-reload
sudo systemctl start fprintd.service
```

If an abrupt power loss or killed installer left its runtime mask, inspect
`systemctl status fprintd.service` and `systemctl is-enabled fprintd.service`.
Only if the mask was created by this installer, restore it with
`sudo systemctl unmask --runtime fprintd.service` before starting the service.

For complete removal, deactivate first and confirm you have a protected backup
of the key if it is still needed. Then remove `/opt/goodix-538d-existing`
explicitly. This deletes its private key copy. Do not delete `/var/lib/fprint`
wholesale: it can contain other users' credentials. Use fprintd's supported
per-user deletion command if deleting your Linux fingerprints is intended.

Reactivating a retained staged installation uses `probe`, then `activate`.
