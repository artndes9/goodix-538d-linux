# Validation record

Recorded 7 October 2026. Separate the driver observations from the newly
packaged installer: the working system was installed with the earlier local
scripts; the public wrapper was validated with offline tests, not reinstalled
over that working system.

## Reference hardware observations

| Item | Observation |
| --- | --- |
| Laptop | Dell Inspiron 7501 |
| USB | Goodix `27c6:538d` |
| Firmware | `GF5298_GM168SEC_APP_13016` |
| OS / architecture | Kubuntu/Ubuntu 26.04.1, x86_64 |
| libfprint base | 1.94.10 at the revision in SOURCE.json |
| Existing pairing key | Windows-context validated; fresh sensor hash and TLS authenticated on Linux |
| Transport-only open / close | Passed |
| Normal open / close | Two cycles in one process passed |
| Cancellation | 250 ms, 1200 ms, and final reopen passed |
| Enrollment | 16-sample right index enrollment completed |
| Controlled positive checks | Two right-index matches after settling fix |
| Controlled negative check | Unenrolled left index rejected |
| KDE screen unlock | User-confirmed; extra Unlock/Enter confirmation remained |
| Windows preservation | User-confirmed after transport-only probe |

Initial enrollment/verification attempts failed, including with the same
finger. More consistent enrollment and the fresh settled capture improved the
observed results. This is useful working evidence, not a claim that all finger
placements or environmental conditions work reliably.

## Offline checks included in the release

- Key-loader tests: synthetic keys, wrong size, permissions, zero key,
  relative path, symlink, hardlink, FIFO and directory rejection.
- Five TLS tests: fragmented successful handshakes and reopen, wrong key,
  authenticated-record tampering, timeout/cancel, missing/zero key.
- Six transport tests: request/write errors, timeout/cancel, transfer cleanup
  and retry, authenticated-success acknowledgment, truncated/restricted
  commands, and a canceled in-flight image before idle acknowledgment.
- AddressSanitizer and UndefinedBehaviorSanitizer instrument the custom test
  builds; GLib warnings are fatal. This does not mean every upstream dependency
  is sanitizer-instrumented.
- Upstream unit/data suites, including device/state-machine checks. Cairo
  assembling and AppStream tests depend on installed tools. udev-hwdb and
  introspection-based emulation are not enabled in this restricted build.
- Packaging/installer tests: private file constraints, changed artifact
  detection, mask restoration, foreign-override protection, activation/update
  failure rollback, and source-only release allowlisting.

The GitHub workflow builds/tests without hardware. Its presence is not a claim
that GitHub CI has already run. The prepared source package was built locally
from a fresh upstream checkout using the available development dependencies;
the distribution APT installation recipe was not rerun on a clean OS image.

## Still needed

- More laptops with the exact sensor/firmware; more enrolled and unenrolled
  fingers, repeated attempts, varied placement and environment.
- Suspend/resume and cold boot coverage.
- Another Windows fingerprint check after full Linux initialization/capture.
- Statistical false-accept/false-reject measurements and spoof-resistance review.
- Hardware tests of the public installer on a fresh machine; other distro ABI
  and service layouts.
- Additional independently analyzed Windows driver builds for key recovery.

Private raw evidence stays outside this repository. Reports should provide
counts and outcomes, not keys, fingerprint frames, templates or process dumps.
