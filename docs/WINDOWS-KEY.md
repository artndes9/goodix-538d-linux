# Recovering your own existing pairing key

This integration needs the sensor's current TLS PSK. It cannot derive it from
the public pairing hash. Someone else's key is not a replacement. Firmware
flashing and key provisioning are outside this project.

The optional `tools/windows/recover-goodix-context.ps1` helper was used
successfully on the reference laptop. It supports **only** the x64 `wbdi.dll`
whose SHA-256 is:

```text
f1f15235cc5d6b7785470251e49f1287a08cb91d3dbddbe99648eed252c23b07
```

This is the public **driver binary's hash**, not a pairing-key hash. The DLL is
not included. Driver version labels alone are insufficient: the helper checks
the on-disk and loaded DLL and selected code signatures before following
version-specific pointers. Do not edit its hash check or offsets to make a
different DLL pass. A different build needs its own reviewed analysis.

## Procedure

1. Boot the laptop's existing Windows installation. Confirm Windows Hello
   fingerprint recognition works with the existing enrollment.
2. Copy **only** `recover-goodix-context.ps1` to a private local directory
   outside any Git checkout, cloud-synced folder or shared drive.
3. Open **64-bit Windows PowerShell as Administrator**, change to that private
   directory, and inspect the script. Ordinary elevated read access resolved
   the access denial on the reference laptop; no security feature was disabled.
4. Run the script in a fresh process:

   ```powershell
   powershell.exe -NoProfile -File .\recover-goodix-context.ps1
   ```

   Follow your organization's normal script-signing/execution policy. This
   guide does not require changing a machine-wide execution policy.

5. Inspect the status metadata, not key contents:

   ```powershell
   Get-Content -LiteralPath .\windows-context-status.json
   ```

   Require `completed: true` and **exactly one** `validated_keys_saved` for the
   intended sensor. A completed run with zero validated keys is a failed
   recovery, not a usable result. Use the metadata path in the status file to
   locate the generated private run directory. The validated filename is
   `goodix-538d-existing-psk.bin`.

6. Confirm Windows fingerprint unlock still works. Privately transfer the
   validated file to Linux using encrypted storage or a trusted encrypted
   channel. Copy it onto a Linux filesystem with mode 0600 as described in
   [INSTALL.md](INSTALL.md#4-put-your-private-key-outside-the-repository).

The helper creates `windows-private/context-.../` with an ACL allowing only
the current user and SYSTEM. Status and detailed metadata can include process
IDs, paths and runtime addresses: review them before sharing. The private
directory also may contain candidate files; do not distribute the directory.
Windows ACLs are not encryption and do not protect files from offline access
to the disk. Do not use `Format-Hex`, `xxd`, screenshots or console output to
transfer the key.

## What the helper validates

It opens query/read process handles, locates `wbdi.dll` in WUDFHost, verifies
the exact DLL and entry signatures, resolves the active MCU dispatch/context,
and double-reads its nonzero 32-byte PSK. It independently compares the retained
TLS PSK allocation, verifies it belongs to the same MCU context, checks
completed-handshake state, and rechecks pointer/state stability. Each explicit
read is at most 128 bytes, with a 4096-byte run budget.

It does not scan arbitrary process memory, dump a process, inject code, write
process memory, restart services, issue sensor commands, re-enroll fingers or
modify firmware. The script's API flags and checks are available for review.
Windows context validation is followed by a **fresh sensor hash and real TLS
handshake check on Linux**; a candidate alone is not trusted by the driver.

## If recovery fails

- **DLL hash mismatch:** unsupported Windows driver build. Stop; changing the
  check would make the offsets untrusted. Report the DLL version/hash and USB
  ID without uploading proprietary binaries or private material.
- **OpenProcess access denied:** confirm the shell is elevated and 64-bit.
  If ordinary administrator read access still fails, stop. Do not disable
  protected processes, security software or driver enforcement for this tool.
- **Null context / no completed handshake:** use Windows Hello normally and
  retry in a fresh PowerShell process. The helper does not force initialization.
- **Only `*.candidate.bin` files:** these did not pass full independent TLS
  validation. Do not rename one to the validated filename or bypass checks.
- **Multiple validated results:** do not guess which device owns which key;
  inspect the context/device association before proceeding.
- **State changed / read bound exceeded:** stop that attempt; normal runtime
  changes can invalidate a read. Retry only in a fresh process.

There is no universal extraction method bundled here. A machine with a
different protected driver configuration may remain blocked even though the
Linux integration itself is buildable.
