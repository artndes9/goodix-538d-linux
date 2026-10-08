# Goodix 538d fingerprint support for Linux

Experimental **existing-key** libfprint integration for **Goodix `27c6:538d`**.
It reuses the sensor's current Windows pairing key and does not flash firmware
or provision a replacement key. Tested on a Dell Inspiron 7501 running
Kubuntu/Ubuntu 26.04.1, including enrollment, verification and KDE unlock.

**You need your own sensor's existing 32-byte pairing key.** A key is not
included. A narrowly version-bound Windows recovery tool is included; it does
not work with arbitrary Windows drivers. Read [key recovery](docs/WINDOWS-KEY.md)
before starting if you do not already have a validated key.

This is a userspace libfprint integration, not a kernel module or DKMS package.
It builds the driver from a pinned community fork and scopes the resulting
library to the distribution's `fprintd` service. It does not overwrite the
distribution libfprint library or edit PAM.

## Start here

1. [Compatibility and prerequisites](docs/INSTALL.md#1-check-compatibility)
2. [Recover your own existing key in Windows](docs/WINDOWS-KEY.md), if needed
3. [Build, install, enroll and enable authentication](docs/INSTALL.md)
4. [Troubleshooting and rollback](docs/TROUBLESHOOTING.md)

| Requirement | Supported by this package |
| --- | --- |
| USB device | `27c6:538d` only |
| Firmware | `GF5298_GM168SEC_APP_13016` only; verified during open |
| Architecture / service | x86_64 Linux, systemd, root-run distribution fprintd |
| Driver | `goodixtls53xd` only; other fingerprint readers are not enabled |
| Pairing | Existing nonzero raw 32-byte key matching this sensor |
| Confirmed hardware | One Dell Inspiron 7501 |
| Windows recovery helper | One exact `wbdi.dll` hash; see the recovery guide |

Other USB IDs, firmware versions, Windows DLL builds and Linux distributions
are **not validated**. The Ubuntu dependency recipe and GitHub Actions workflow
are build aids, not evidence of hardware support on those systems.

## What changed

The patch loads a private existing key, checks it against a fresh sensor pairing
hash, and completes a real TLS 1.2 PSK handshake through OpenSSL memory BIOs.
It fixes asynchronous transfer cleanup, cancellation and reopen behavior,
rejects pairing writes, and adds a fresh capture after 200 ms of settling before
matching. The existing matching threshold and two-sample corroboration remain.

No Windows DLL, firmware image, pairing key, device-specific log or biometric
data is distributed. The optional Windows helper reads bounded, validated
process locations without issuing sensor commands or writing process memory.

## Evidence and limits

On the test laptop: transport authentication, repeated open/close, two
cancellation timings and enrollment passed. Two controlled enrolled-finger
checks matched; an unenrolled finger was rejected; KDE unlock worked. Those
few checks do not establish statistical accuracy or spoof resistance.
Suspend/resume is not yet validated. Windows unlock was confirmed after the
transport-only probe; another Windows check after full Linux capture remains
outstanding. See [validation](docs/VALIDATION.md).

KDE may show an additional **Unlock** button after fingerprint authentication;
Enter activates it. This matches [KDE bug 497904](https://bugs.kde.org/show_bug.cgi?id=497904).

## Source, license and contribution

Based on [lbssousa/libfprint](https://github.com/lbssousa/libfprint) at
`037912c17992d2abc81c79c40e85b45ba1d3871e`, with attribution to its driver,
transport and SIGFM contributors in [NOTICE.md](NOTICE.md). The build fetches
that exact revision and applies `patches/0001-existing-key-integration.patch`.
This repository contains the changes and integration tools, not a copy of the
entire upstream tree. It is an independent community project, not an official
Goodix, Dell or libfprint release.

LGPL-2.1-or-later; see [LICENSE](LICENSE). Contributions, device reports and
review are welcome. Read [CONTRIBUTING.md](CONTRIBUTING.md) and
[SECURITY.md](SECURITY.md) before attaching diagnostics. Maintainers can use
[the publishing guide](docs/PUBLISHING.md) to create a source-only release.
