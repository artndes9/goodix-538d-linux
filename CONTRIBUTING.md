# Contributing

Useful contributions include reproducible device reports, lifecycle fixes,
matching evaluation using private data, and reviewed support for another exact
Windows driver build. Start with docs/ARCHITECTURE.md and docs/VALIDATION.md.

Build and test with `./scripts/build.sh`. Driver changes belong in the combined
patch against the revision in SOURCE.json. Generated `.work/source` changes
are discarded on rebuild; export a new patch before rebuilding. Preserve
upstream attribution and license notices. Do not enable other drivers just
because they share the Goodix name.

Keep secret loading, fresh hash validation, real TLS authentication and bounded
cleanup intact. A matching change needs controlled positive and negative
results, not merely successful enrollment or a lower threshold.

Use synthetic keys/images in tests. Never commit real pairing keys, hashes of
those keys, templates, calibration, raw images, memory dumps or proprietary
driver/firmware binaries. Include only sanitized error messages in issues.

Before opening a pull request:

```sh
./scripts/build.sh
python3 scripts/package.py --check
git diff --check
```

Describe what changed, the failure it fixes, the tests run, and which hardware
claims remain unverified. Update PACKAGE_FILES when intentionally adding a
public source file; release archives include only that explicit allowlist.
