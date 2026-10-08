# SPDX-License-Identifier: LGPL-2.1-or-later
# Hash-bound, read-only MCU context inspection. No sensor calls or process writes.
[CmdletBinding()]
param([string]$CompareCandidate)
$ErrorActionPreference = 'Stop'
$statusPath = Join-Path $PSScriptRoot 'windows-context-status.json'
trap {
    [ordered]@{completed=$false; error=$_.Exception.Message} | ConvertTo-Json |
        Set-Content -LiteralPath $statusPath -Encoding UTF8
    exit 1
}
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Elevation required.' }
if (-not [Environment]::Is64BitProcess) { throw '64-bit process required.' }
$expectedHash = 'F1F15235CC5D6B7785470251E49F1287A08CB91D3DBDDBE99648EED252C23B07'
$dll = Join-Path $env:SystemRoot 'System32\drivers\UMDF\wbdi.dll'
if ((Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash -ne $expectedHash) { throw 'DLL hash mismatch.' }
$private = Join-Path $PSScriptRoot 'windows-private'
if (-not (Test-Path -LiteralPath $private)) { New-Item -ItemType Directory -Path $private | Out-Null }
if ((Get-Item -LiteralPath $private).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Private directory is a reparse point.' }
$acl = [Security.AccessControl.DirectorySecurity]::new()
$acl.SetAccessRuleProtection($true, $false)
foreach ($sid in @($identity.User.Value, 'S-1-5-18')) {
    $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new(
        [Security.Principal.SecurityIdentifier]::new($sid), 'FullControl',
        'ContainerInherit,ObjectInherit', 'None', 'Allow'))
}
Set-Acl -LiteralPath $private -AclObject $acl
$runDir = Join-Path $private ('context-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-ffff'))
New-Item -ItemType Directory -Path $runDir | Out-Null

Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
public static class GoodixContextRead {
    public static int BytesRead;
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool CloseHandle(IntPtr handle);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool ReadProcessMemory(IntPtr h, IntPtr address, byte[] buffer, UIntPtr size, out UIntPtr read);
    [StructLayout(LayoutKind.Sequential)]
    struct MBI { public IntPtr BaseAddress, AllocationBase; public uint AllocationProtect;
        public UIntPtr RegionSize; public uint State, Protect, Type; }
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern UIntPtr VirtualQueryEx(IntPtr h, IntPtr address, out MBI info, UIntPtr size);
    [DllImport("psapi.dll", SetLastError=true)]
    static extern bool EnumProcessModulesEx(IntPtr h, [Out] IntPtr[] modules, uint size, out uint needed, uint filter);
    [DllImport("psapi.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern uint GetModuleFileNameEx(IntPtr h, IntPtr module, StringBuilder path, uint size);
    public static IntPtr[] Modules(IntPtr h) {
        var modules = new IntPtr[1024]; uint needed;
        if (!EnumProcessModulesEx(h, modules, (uint)(modules.Length * IntPtr.Size), out needed, 3))
            throw new Win32Exception(Marshal.GetLastWin32Error());
        if (needed > modules.Length * IntPtr.Size) throw new Exception("Module bound exceeded.");
        Array.Resize(ref modules, (int)needed / IntPtr.Size); return modules;
    }
    public static string ModulePath(IntPtr h, IntPtr module) {
        var path = new StringBuilder(32768);
        if (GetModuleFileNameEx(h, module, path, (uint)path.Capacity) == 0)
            throw new Win32Exception(Marshal.GetLastWin32Error());
        return path.ToString();
    }
    public static byte[] Read(IntPtr h, long address, int size) {
        if (address < 0x10000 || address > 0x7fffffffffff - size || size < 1 || size > 128 || BytesRead + size > 4096)
            throw new Exception("Read bound exceeded.");
        MBI info;
        if (VirtualQueryEx(h, new IntPtr(address), out info, (UIntPtr)Marshal.SizeOf(typeof(MBI))) == UIntPtr.Zero)
            throw new Win32Exception(Marshal.GetLastWin32Error());
        if (info.State != 0x1000 || (info.Protect & 0x101) != 0 ||
            (info.Protect & 0xee) == 0 || address + size > info.BaseAddress.ToInt64() + (long)info.RegionSize.ToUInt64())
            throw new Exception("Uncommitted, protected, or cross-region read rejected.");
        var bytes = new byte[size]; UIntPtr read;
        BytesRead += size;
        if (!ReadProcessMemory(h, new IntPtr(address), bytes, (UIntPtr)size, out read) || read.ToUInt64() != (ulong)size) {
            Array.Clear(bytes, 0, bytes.Length);
            throw new Win32Exception(Marshal.GetLastWin32Error());
        }
        return bytes;
    }
    public static long Pointer(IntPtr h, long address) { return BitConverter.ToInt64(Read(h,address,8),0); }
    public static uint Dword(IntPtr h, long address) { return BitConverter.ToUInt32(Read(h,address,4),0); }
    public static bool Equal(byte[] a, byte[] b) {
        if (a == null || b == null || a.Length != b.Length) return false;
        int difference = 0;
        for (int i=0; i<a.Length; i++) difference |= a[i] ^ b[i];
        return difference == 0;
    }
    public static bool Nonzero(byte[] a) { int result=0; foreach(byte b in a) result |= b; return result != 0; }
    public static byte[] FileRva(byte[] pe, int rva, int count) {
        int nt = BitConverter.ToInt32(pe,0x3c);
        int sectionCount = BitConverter.ToUInt16(pe,nt+6);
        int sections = nt+24+BitConverter.ToUInt16(pe,nt+20);
        for (int i=0; i<sectionCount; i++) {
            int s=sections+i*40, va=BitConverter.ToInt32(pe,s+12), rawSize=BitConverter.ToInt32(pe,s+16);
            if (rva >= va && rva+count <= va+rawSize) {
                byte[] result=new byte[count];
                Array.Copy(pe,BitConverter.ToInt32(pe,s+20)+rva-va,result,0,count); return result;
            }
        }
        throw new Exception("RVA not backed by file data.");
    }
}
'@

$report = [ordered]@{utc=[DateTime]::UtcNow.ToString('o'); driver_sha256=$expectedHash;
    sensor_commands=0; process_writes=0; read_budget_bytes=4096; processes=@(); candidates_saved=0; validated_keys_saved=0}
foreach ($process in @(Get-CimInstance Win32_Process -Filter "Name='WUDFHost.exe'")) {
    $row = [ordered]@{pid=$process.ProcessId; modules=@(); error=$null}
    $handle = [GoodixContextRead]::OpenProcess(0x410,$false,$process.ProcessId)
    if ($handle -eq [IntPtr]::Zero) {
        $row.error='OpenProcess error ' + [Runtime.InteropServices.Marshal]::GetLastWin32Error()
        $report.processes += $row
        continue
    }
    try {
        foreach ($module in [GoodixContextRead]::Modules($handle)) {
            $path=[GoodixContextRead]::ModulePath($handle,$module)
            if ([IO.Path]::GetFileName($path) -ine 'wbdi.dll') { continue }
            $item=[ordered]@{module=$path; module_base=('0x{0:x}' -f $module.ToInt64());
                stage='hash'; active_path=$null; candidate_saved=$false; validation='Not established'; error=$null}
            $key=$null; $second=$null; $tlsKey=$null; $tlsSecond=$null; $prior=$null
            try {
                if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $expectedHash) { throw 'Loaded DLL hash mismatch.' }
                $base=$module.ToInt64()
                $disk=[IO.File]::ReadAllBytes($path)
                # These entry signatures are RIP-relative and contain no relocated addresses.
                foreach ($rva in @(0x469c4,0x73550,0x733dc,0x73a88,0x72c2c,0x3f5f4,0x75d0,0x8210,0xdbd0)) {
                    if (-not [GoodixContextRead]::Equal([GoodixContextRead]::Read($handle,$base+$rva,32),
                        [GoodixContextRead]::FileRva($disk,$rva,32))) { throw 'Live code signature mismatch.' }
                }
                $item.stage='loader root'
                $loader=[GoodixContextRead]::Pointer($handle,$base+0x167838)
                if ($loader -eq 0) { throw 'Loader root at RVA 0x167838 is null.' }
                $table=[GoodixContextRead]::Pointer($handle,$loader)
                $context=[GoodixContextRead]::Pointer($handle,$loader+8)
                $ops=[GoodixContextRead]::Pointer($handle,$loader+0x10)
                $item.loader=('0x{0:x}' -f $loader)
                $item.context=('0x{0:x}' -f $context)
                $item.table_rva=('0x{0:x}' -f ($table-$base))
                $item.ops_rva=('0x{0:x}' -f ($ops-$base))
                $item.stage='dispatch validation'
                if ($table -eq $base+0x13e020 -and $ops -eq $base+0x13df50) {
                    $active='G'; $create=0x68e94; $validate=0x6991c
                } elseif ($table -eq $base+0x13ecb0 -and $ops -eq $base+0x13ebe0) {
                    $active='R'; $create=0x753e8; $validate=0x74964
                } else { throw 'Unknown active MCU table; context read stopped.' }
                if ([GoodixContextRead]::Pointer($handle,$table) -ne $base+$create -or
                    [GoodixContextRead]::Pointer($handle,$table+0x38) -ne $ops -or
                    [GoodixContextRead]::Pointer($handle,$ops+0x80) -ne $base+$validate -or
                    [GoodixContextRead]::Pointer($handle,$ops+0x88) -ne $base+0x72c2c) { throw 'Dispatch pointer mismatch.' }
                $item.active_path=$active
                $item.validation_wrapper_rva=('0x{0:x}' -f $validate)
                $item.stage='context validation'
                if ([GoodixContextRead]::Dword($handle,$context) -ne 0x64696f70 -or
                    [GoodixContextRead]::Pointer($handle,$context+0x20) -ne $table) { throw 'Context marker/table mismatch.' }
                $item.context_marker_valid=$true
                $initialized=[GoodixContextRead]::Dword($handle,$context+0xb8)
                $handshake=[GoodixContextRead]::Dword($handle,$context+0xc8)
                $keyPointer=[GoodixContextRead]::Pointer($handle,$context+0xe0)
                $length=[GoodixContextRead]::Dword($handle,$context+0xe8)
                $item.tls_initialized=$initialized
                $item.tls_handshake_complete=$handshake
                $item.psk_length=$length
                $item.stage='bounded PSK read'
                if ($length -ne 32) { throw 'Context PSK length is not 32; no key bytes read.' }
                $key=[GoodixContextRead]::Read($handle,$keyPointer,32)
                $second=[GoodixContextRead]::Read($handle,$keyPointer,32)
                if (-not [GoodixContextRead]::Equal($key,$second) -or
                    [GoodixContextRead]::Pointer($handle,$base+0x167838) -ne $loader -or
                    [GoodixContextRead]::Pointer($handle,$loader) -ne $table -or
                    [GoodixContextRead]::Pointer($handle,$loader+8) -ne $context -or
                    [GoodixContextRead]::Pointer($handle,$loader+0x10) -ne $ops -or
                    [GoodixContextRead]::Dword($handle,$context) -ne 0x64696f70 -or
                    [GoodixContextRead]::Pointer($handle,$context+0x20) -ne $table -or
                    [GoodixContextRead]::Pointer($handle,$context+0xe0) -ne $keyPointer -or
                    [GoodixContextRead]::Dword($handle,$context+0xe8) -ne $length -or
                    [GoodixContextRead]::Dword($handle,$context+0xb8) -ne $initialized -or
                    [GoodixContextRead]::Dword($handle,$context+0xc8) -ne $handshake) { throw 'Context changed during read; candidate rejected.' }
                if (-not [GoodixContextRead]::Nonzero($key)) { throw 'All-zero candidate rejected.' }
                $file='host-' + $process.ProcessId + '-existing-psk.candidate.bin'
                [IO.File]::WriteAllBytes((Join-Path $runDir $file),$key)
                $item.candidate_file=$file
                $item.candidate_saved=$true
                $item.stable_double_read=$true
                $item.validation='Stable live PSK; static validated-setter provenance. Independent validation pending.'
                $report.candidates_saved++
                $item.stage='active TLS cross-validation'
                # SecTLS.c copies the MCU PSK into the SSL configuration. These are
                # exact RIP-relative globals in tls_init (RVA 0x75d0), not a scan.
                $sslConfig=[GoodixContextRead]::Pointer($handle,$base+0x20bdc0)
                $sslState=[GoodixContextRead]::Dword($handle,$base+0x20bdc8)
                $tlsIoContext=[GoodixContextRead]::Pointer($handle,$base+0x20bdf0)
                $tlsPointer=[GoodixContextRead]::Pointer($handle,$base+0x20bff0)
                $tlsLength=[GoodixContextRead]::Pointer($handle,$base+0x20bff8)
                $item.ssl_state=$sslState
                $item.tls_psk_length=$tlsLength
                $item.tls_io_context_matches=($tlsIoContext -eq $context)
                if ($sslConfig -ne $base+0x20bf18 -or $tlsIoContext -ne $context) { throw 'TLS configuration/context does not match this MCU.' }
                if ($tlsLength -ne 32 -or $tlsPointer -eq $keyPointer) { throw 'Expected independent 32-byte TLS PSK allocation absent.' }
                $tlsKey=[GoodixContextRead]::Read($handle,$tlsPointer,32)
                $tlsSecond=[GoodixContextRead]::Read($handle,$tlsPointer,32)
                if (-not [GoodixContextRead]::Equal($tlsKey,$tlsSecond) -or
                    -not [GoodixContextRead]::Equal($key,$tlsKey)) { throw 'TLS PSK copy changed or does not match the MCU PSK.' }
                $item.independent_tls_psk_matches=$true
                if ($initialized -ne 1 -or $handshake -ne 1 -or $sslState -ne 16) { throw 'Active completed TLS handshake not established.' }
                if ([GoodixContextRead]::Pointer($handle,$base+0x20bdc0) -ne $sslConfig -or
                    [GoodixContextRead]::Pointer($handle,$base+0x20bdf0) -ne $context -or
                    [GoodixContextRead]::Dword($handle,$base+0x20bdc8) -ne 16 -or
                    [GoodixContextRead]::Pointer($handle,$base+0x20bff0) -ne $tlsPointer -or
                    [GoodixContextRead]::Pointer($handle,$base+0x20bff8) -ne 32 -or
                    [GoodixContextRead]::Pointer($handle,$base+0x167838) -ne $loader -or
                    [GoodixContextRead]::Pointer($handle,$loader+8) -ne $context -or
                    [GoodixContextRead]::Pointer($handle,$context+0xe0) -ne $keyPointer -or
                    [GoodixContextRead]::Dword($handle,$context+0xe8) -ne 32 -or
                    [GoodixContextRead]::Dword($handle,$context+0xb8) -ne 1 -or
                    [GoodixContextRead]::Dword($handle,$context+0xc8) -ne 1) { throw 'State changed during TLS validation.' }
                if ($CompareCandidate) {
                    $priorInfo=Get-Item -LiteralPath $CompareCandidate
                    if ($priorInfo.Length -ne 32 -or $priorInfo.PSIsContainer -or
                        ($priorInfo.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
                        -not $priorInfo.FullName.StartsWith(([IO.Path]::GetFullPath($private)+'\'),[StringComparison]::OrdinalIgnoreCase)) {
                        throw 'Previous candidate must be a private local 32-byte regular file.'
                    }
                    $prior=[IO.File]::ReadAllBytes($priorInfo.FullName)
                    if (-not [GoodixContextRead]::Equal($key,$prior)) { throw 'Candidate changed between collection runs.' }
                    $item.previous_candidate_matches=$true
                }
                $validatedFile='goodix-538d-existing-psk.bin'
                [IO.File]::WriteAllBytes((Join-Path $runDir $validatedFile),$key)
                $item.validated_key_file=$validatedFile
                $item.validation='Validated against the active Windows TLS PSK copy and completed handshake state; not a fresh sensor-hash read or Linux handshake.'
                $item.stage='completed'
                $report.validated_keys_saved++
            } catch { $item.error=$_.Exception.Message }
            finally {
                if ($null -ne $key) { [Array]::Clear($key,0,$key.Length) }
                if ($null -ne $second) { [Array]::Clear($second,0,$second.Length) }
                if ($null -ne $tlsKey) { [Array]::Clear($tlsKey,0,$tlsKey.Length) }
                if ($null -ne $tlsSecond) { [Array]::Clear($tlsSecond,0,$tlsSecond.Length) }
                if ($null -ne $prior) { [Array]::Clear($prior,0,$prior.Length) }
            }
            $row.modules += $item
        }
    } catch { $row.error=$_.Exception.Message }
    finally { [void][GoodixContextRead]::CloseHandle($handle) }
    $report.processes += $row
}
$report.bytes_read=[GoodixContextRead]::BytesRead
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $runDir 'metadata.json') -Encoding UTF8
[ordered]@{completed=$true; metadata=(Join-Path $runDir 'metadata.json'); candidates_saved=$report.candidates_saved;
    validated_keys_saved=$report.validated_keys_saved; bytes_read=$report.bytes_read} |
    ConvertTo-Json | Set-Content -LiteralPath $statusPath -Encoding UTF8
