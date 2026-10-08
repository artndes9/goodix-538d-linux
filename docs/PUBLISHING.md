# Publishing this project on GitHub

Publish the contents of **goodix-538d-linux only**. Do not publish the parent
working directory, original fingerprint-driver investigation, Windows handoff
ZIPs or any live installation/state directory. This project uses an explicit
source allowlist to keep those out of release archives.

## Prepare and inspect a release

```sh
python3 scripts/package.py --check
python3 scripts/package.py
unzip -l dist/goodix-538d-linux-0.1.0.zip
```

The version comes from VERSION. The tool creates ZIP and tar.gz source archives
plus SHA256SUMS under `dist/`. Each has a `goodix-538d-linux/` top-level folder.
No `.work`, Git history, binary library, log, key or biometric data is included.
It packages only PACKAGE_FILES, even if ignored secrets accidentally exist
elsewhere in the checkout. This is a boundary, not a general secret scanner:
review modifications to every allowlisted source file before release.

Extract the tar.gz into a separate clean directory outside the investigation
workspace. This avoids accidentally initializing Git in the parent repository
and preserves executable bits. ZIP users whose extractor loses modes can run:

```sh
chmod +x scripts/build.sh scripts/test.sh
```

Create an empty GitHub repository, for example `goodix-538d-linux`. Do not
initialize it remotely with a different README/license. In the extracted root:

```sh
git init -b main
git add --pathspec-from-file=PACKAGE_FILES
git diff --cached --stat
git diff --cached --check
git status --short
git commit -m "Publish experimental Goodix 538d existing-key integration"
```

After reviewing the staged file list and diff, substitute your actual account
and repository in the remote URL:

```sh
git remote add origin https://github.com/YOUR_ACCOUNT/goodix-538d-linux.git
git push -u origin main
```

GitHub Actions runs an offline build/test workflow. Review its outcome after
pushing. It does not test USB hardware or recover any credentials.

## Suggested description

> Experimental Linux libfprint integration for Goodix 27c6:538d using the
> sensor's existing Windows pairing key, without firmware flashing.

Suggested topics: `goodix`, `fingerprint`, `libfprint`, `linux`, `fprintd`.

Keep the key prerequisite and experimental compatibility scope prominent.
Do not call the package universal, upstream-supported, or security-certified.
Retain LICENSE, NOTICE.md, SOURCE.json and the pinned patch. Enable private
vulnerability reporting in repository settings. No maintainer account or
contact address is pre-filled.

## Tag and release

After reviewing CI and the source list:

```sh
git tag -a v0.1.0 -m "Experimental existing-key integration"
git push origin v0.1.0
```

Create a GitHub release for that tag and attach only the generated source ZIP,
tar.gz and SHA256SUMS. Describe the confirmed hardware, own-key requirement,
known limits and rollback link. Future releases should update VERSION,
SOURCE.json and validation notes together and regenerate archives. Do not
upload built libraries without also preparing their exact corresponding source
and applicable license notices.
