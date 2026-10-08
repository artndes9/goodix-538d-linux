# Troubleshooting

Start with `lsusb -d 27c6:538d`, `fprintd-list "$USER"`, and the exact failing
step in INSTALL.md. Do not enable image dumps or share a full debug bundle.

| Symptom | Next step |
| --- | --- |
| No device in `lsusb` | Check the physical/firmware device availability. A library change cannot enumerate an absent USB device. Recheck Windows if dual-booting. |
| No devices in fprintd | Confirm activation succeeded; inspect the service override and library mapping. Only 538d is built. |
| Wrong firmware | Stop. This package supports one firmware string and does not provide a flashing procedure. |
| Private key/file error | Confirm raw 32 bytes, mode 0600, correct owner and one hard link. Use the Linux filesystem copy. Never display its contents. |
| Existing PSK does not match | Use the validated key belonging to this sensor. Do not bypass the fresh hash check or provision a new key. |
| TLS failure or timeout | Confirm the key was recovered for the current pairing and close competing fingerprint tools. Report the ordinary error message; do not attach USB payloads. |
| Busy / already claimed | Close KDE/GNOME fingerprint settings and other enrollment processes, then retry. Do not run fprintd and direct probes concurrently. |
| Installer refuses existing setup | Inspect the existing prefix/override. `update` only accepts this package's marker and exact drop-in. |
| fprintd inactive after idle | D-Bus activation can stop the idle daemon normally. Try `fprintd-list` to activate it; persistent activation failure is different. |
| Undefined symbol / daemon will not start | Distro fprintd may expect a newer library ABI. Deactivate, rebuild on that distro, and review compatibility; do not overwrite system libraries. |
| Repeated same-finger no-match | Re-enroll with centered stable touches and slight variations. Fully lift between touches. Verify repeatedly; do not lower thresholds or fake success. |
| Fingerprint succeeds, KDE shows Unlock | Press Enter or click. This matches [KDE bug 497904](https://bugs.kde.org/show_bug.cgi?id=497904); the driver does not modify the lock screen. |
| Works in CLI but not login | Check the distribution's PAM/desktop configuration using its supported tool. Keep password authentication. The driver installer does not edit PAM. |
| Broken after OS upgrade | Deactivate the scoped override and review/rebuild against the new fprintd dependencies. |

## Read-only service checks

```sh
systemctl status fprintd.service --no-pager
systemctl cat fprintd.service
systemctl show fprintd.service -p DropInPaths -p Environment -p UMask -p LimitCORE
sudo journalctl -u fprintd.service -b --no-pager -n 60
```

These show paths and ordinary diagnostics, not intentional key dumps. Still
review output for personal paths/usernames and unexpected debug payloads before
posting. Never upload `/var/lib/fprint`, the private key directory, NTFS private
recovery files, process/core dumps or unfiltered USB traces.

## Emergency fallback

Use password authentication. See the exact manual drop-in removal and runtime
mask recovery commands in [INSTALL.md](INSTALL.md#rollback-and-removal).
If the sensor disappears or remains unresponsive, stop repeated probes, return
to the distribution integration and perform your normal complete shutdown/
boot sequence. Recheck Windows recognition separately. Do not recover by
flashing firmware or resetting enrollment based on an unverified workaround.
