# Architecture and maintenance notes

`pam_fprintd` / desktop → D-Bus fprintd → this scoped libfprint → USB sensor.
See the [fprint project](https://fprint.freedesktop.org/) for the library and
daemon's respective roles. Nothing in this package runs in the kernel.

## Reconstructing the source

`SOURCE.json` records the base revision. `scripts/build.sh` fetches the exact
commit from lbssousa/libfprint, uses `git archive` to construct `.work/source`,
and applies the combined patch with zero fuzz. It builds only `goodixtls53xd`.
Do not enable other Goodix TLS drivers without reviewing shared API changes.

The patch contains:

- `goodix_psk.[ch]`: absolute-path, regular-file, single-link, ownership and
  0600 checks; 32 raw bytes, nonzero; SHA-256 comparison against fresh sensor
  pairing-hash data. Secret buffers are cleared on cleanup where implemented.
- `goodixtls.[ch]`: TLS 1.2 `PSK-AES128-CBC-SHA256`, memory BIO transport,
  bounded handshake and explicit session cleanup. Key agreement alone is not
  success: the real Finished exchange must authenticate before the device ACK.
- `goodix.[ch]`: asynchronous TLS pumping, bounded protocol handling, request
  errors, transfer draining and idle acknowledgment during cancellation.
- `goodix53xd.c`: key/hash checks, firmware gating, cleanup paths, image buffer
  release, and a 200 ms placement delay followed by a fresh verification frame.
- Meson changes: include the key loader and fix test dictionary iteration when
  introspection is disabled.

The matcher comes from the pinned fork. The patch retains its score threshold
and corroboration policy. A delayed fresh capture improved the observed
same-finger result without reducing that threshold.

## Storage and process boundaries

The Linux key copy is `/opt/goodix-538d-existing/private/existing.psk`; only its
path appears in the service environment. It is a credential, not configuration
to include in bug reports. The driver checks the sensor's pairing hash again
when opening it; an earlier successful install is not permanent validation.

fprintd stores enrolled prints in its normal protected state directory,
typically `/var/lib/fprint`. This fork's template format includes raw sample
frames, and its calibration file includes image-derived data. Both are
sensitive biometric material, even when not named `.pgm` or `.raw`. Do not
publish the state directory or a serialized print. Default umask 0077 protects
new files; existing files retain their existing modes.

Hardware probes use a temporary private state directory and disable core
dumps. The service override removes image-dump and transport-only environment
variables, disables core dumps, and does not enable debug logging. Developers
must still avoid USB tracing, process dumps and image export with real fingers
unless deliberately collecting protected local research material.

Normal initialization sends runtime reset and MCU configuration commands.
The transport-only probe uses an allowlist and omits those steps. Neither
mode provisions a key or flashes firmware. A successful Windows unlock after
one mode should not be reported as validation of every later operation.

## Installer behavior

`manage.py` has fixed installation paths, an exclusive operation lock and an
installation marker. It never builds as root, and stage refuses existing
targets. Probe/activation temporarily mask fprintd; normal completion or failure
unmasks it and restores prior activity. A killed process or power loss can
require the recovery steps in INSTALL.md.

Activation writes a drop-in only after hardware and linkage checks. It removes
that drop-in if daemon validation fails. Update backs up the installed
artifacts and restores them on normal validation failure. Deactivation refuses
to remove an edited or foreign override. This protects deliberate local
configuration from accidental replacement; it is not a sandbox against a
malicious build checkout or administrator.
