# Security and private data

Treat pairing keys as credentials and all stored fingerprint samples,
serialized prints and calibration data as sensitive biometric material.
Never publish them in issues, PRs, releases, screenshots or support bundles.
This fork's templates include sample frames; they are not anonymous counters.

The source release contains synthetic tests and no device credentials. The
Windows helper writes private output; run it outside the repository and transfer
only your own validated key privately. A key's hash is also excluded from
public reports. Firmware and Windows driver binaries are not redistributed.

Do not disable key/hash checks, lower matching thresholds, turn off Windows
security protections, or provision a fixed key to make an installation pass.
The driver is experimental and not independently security-certified. A small
number of successful/failed matches does not establish spoof resistance.

For a suspected vulnerability, use the repository's private vulnerability
reporting feature if the publisher has enabled it. Otherwise contact the
publisher privately first. Public issues should contain only a non-sensitive
description; do not attach exploit data involving actual biometric records or
credentials. No private contact address is invented by this template.

Maintainers should enable GitHub private vulnerability reporting before
publication and monitor the pinned upstream dependencies for security fixes.
