# Attribution and source provenance

This is a community integration of existing libfprint work, with an
existing-key transport/lifecycle patch and packaging tools. It is not a driver
developed wholly from scratch and is not affiliated with Goodix or Dell.

- [libfprint / fprint](https://fprint.freedesktop.org/): the userspace fingerprint
  library, device API, asynchronous state machines and distribution integration.
- [lbssousa/libfprint](https://github.com/lbssousa/libfprint), pinned commit
  `037912c17992d2abc81c79c40e85b45ba1d3871e`: the starting `goodixtls53xd` driver,
  its 538d capture/matching work and self-contained SIGFM implementation.
- The pinned driver's headers credit
  [infinytum/libfprint](https://github.com/infinytum/libfprint),
  [goodix-fp-linux-dev](https://github.com/goodix-fp-linux-dev), and
  [AndyHazz/goodix53x5-libfprint](https://github.com/AndyHazz/goodix53x5-libfprint)
  for transport/capture and FpDevice/SIGFM architecture. The 53x5 wire protocol
  is different; this repository does not claim 53x5 compatibility.
- Original copyright notices remain in the upstream source reconstructed by
  the build. See the individual files for their authors and terms.

The changes supplied here add existing nonzero key loading and validation,
OpenSSL memory BIO TLS, cancellation/cleanup fixes, fresh settled verification
capture, synthetic tests, bounded probes and service integration tooling.
New integration code and documentation are offered under LGPL-2.1-or-later;
see LICENSE. The patch applies to LGPL-2.1-or-later source; original notices and
any component-specific upstream terms continue to apply.

The optional PowerShell helper is original analysis tooling. No proprietary
Windows driver, driver disassembly, firmware image, pairing key or biometric
record is included. Its expected DLL hash identifies the one supported build.

Release 0.1.0 is distributed as source and a patch, not prebuilt libfprint
binaries. If distributing binaries separately, include their exact corresponding
source and modifications and preserve the applicable license notices. The
GitHub/source URL alone is not a substitute for preparing that source release.
