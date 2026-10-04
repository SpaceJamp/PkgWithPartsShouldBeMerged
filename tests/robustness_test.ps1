# SPDX-License-Identifier: MIT
# pkg-merge robustness test - hostile input (bug-check pass 4)
#
# Every case must produce a clear diagnostic and a non-zero exit code, must not
# crash, and must never leave a partial or temporary file behind.
#
# Usage:  pwsh -File tests/robustness_test.ps1 -Exe path\to\pkg_merge.exe
#
# No license has been granted for this code - see LICENSE.md at the repository root.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [string]$WorkRoot = (Join-Path ([IO.Path]::GetTempPath()) "pkg-merge-hostile"),
    # The >2 GiB case needs a 3 GiB scratch file; opt out with -SkipLarge.
    [switch]$SkipLarge
)

$ErrorActionPreference = 'Stop'
$script:Failures = 0
$script:Checks = 0

function Assert-True([bool]$Condition, [string]$Message) {
    $script:Checks++
    if ($Condition) { Write-Host "  [ok]   $Message" -ForegroundColor DarkGreen }
    else { $script:Failures++; Write-Host "  [FAIL] $Message" -ForegroundColor Red }
}

function Assert-Equal($Expected, $Actual, [string]$Message) {
    Assert-True ($Expected -eq $Actual) "$Message (expected '$Expected', got '$Actual')"
}

function Reset-Dir([string]$Path) {
    if (Test-Path -LiteralPath $Path) {
        # Restore any deny ACEs left behind by the read-only test before deleting.
        & icacls $Path /remove:d "$env:USERNAME" 2>&1 | Out-Null
        Remove-Item -Recurse -Force -LiteralPath $Path -ErrorAction SilentlyContinue
    }
    New-Item -ItemType Directory -Force -Path $Path | Out-Null
    return (Resolve-Path -LiteralPath $Path).Path
}

function New-Part([string]$Path, [int]$Bytes, [switch]$Magic) {
    $full = Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $full)) { New-Item -ItemType Directory -Force -Path $full | Out-Null }
    $stream = [IO.File]::Create($Path)
    try {
        $chunk = New-Object byte[] 65536
        (New-Object Random 42).NextBytes($chunk)
        if ($Magic) { $chunk[0] = 0x7F; $chunk[1] = 0x43; $chunk[2] = 0x4E; $chunk[3] = 0x54 }
        $written = 0
        while ($written -lt $Bytes) {
            $n = [Math]::Min($chunk.Length, $Bytes - $written)
            $stream.Write($chunk, 0, $n)
            $written += $n
        }
    } finally { $stream.Dispose() }
}

function Invoke-Tool([string[]]$ToolArgs) {
    $output = & $Exe @ToolArgs 2>&1 | Out-String
    return [pscustomobject]@{ ExitCode = $LASTEXITCODE; Output = $output }
}

function Assert-NoLeftovers([string]$Dir, [string]$What) {
    $tmp = @(Get-ChildItem -LiteralPath $Dir -Filter '*.merging.tmp' -Recurse -ErrorAction SilentlyContinue)
    Assert-True ($tmp.Count -eq 0) "$What leaves no temporary file behind"
}

Write-Host "pkg-merge robustness test (pass 4)" -ForegroundColor Cyan
Write-Host "  exe: $Exe"

# --- 1. filenames that are not pieces ---------------------------------------
Write-Host "`n[1] hostile filenames"
$odd = Reset-Dir (Join-Path $WorkRoot 'names')
New-Part (Join-Path $odd '_0.pkg') 1024 -Magic
New-Part (Join-Path $odd 'no-number.pkg') 1024 -Magic
New-Part (Join-Path $odd 'trailing_.pkg') 1024 -Magic
New-Part (Join-Path $odd '_0.pkg.txt') 1024 -Magic
New-Part (Join-Path $odd 'CUSA00001_99999999999999999999.pkg') 1024
New-Part (Join-Path $odd 'CUSA00001_-1.pkg') 1024
New-Part (Join-Path $odd '.pkg') 512
New-Part (Join-Path $odd 'CUSA00002_0.pkg') 4096 -Magic
New-Part (Join-Path $odd 'CUSA00002_1.pkg') 4096
$r1 = Invoke-Tool @('-i', $odd, '--overwrite')
Assert-True ($r1.ExitCode -ne 130) "hostile filenames do not look like a cancellation"
Assert-True ($r1.Output -match 'exists twice|no root piece|does not match') "bad piece names are rejected by name"
Assert-True (Test-Path (Join-Path $odd 'CUSA00002-merged.pkg')) "the one valid game in the folder still merges"
Assert-True (-not (Test-Path (Join-Path $odd '_0-merged.pkg'))) "a nameless piece never becomes a merged file"
Assert-NoLeftovers $odd 'hostile filenames'

# --- 2. mixed case titles group together ------------------------------------
Write-Host "`n[2] case insensitive titles"
$case = Reset-Dir (Join-Path $WorkRoot 'case')
New-Part (Join-Path $case 'CUSA00003_0.pkg') 8192 -Magic
New-Part (Join-Path $case 'cusa00003_1.pkg') 8192
$mixed = Invoke-Tool @('-i', $case, '--overwrite', '--verify')
Assert-True ($mixed.ExitCode -eq 0) "CUSA00003_0 and cusa00003_1 merge as one game (exit $($mixed.ExitCode))"
Assert-True (Test-Path (Join-Path $case 'CUSA00003-merged.pkg')) "one merged file for the mixed case title"
Assert-Equal 16384 (Get-Item (Join-Path $case 'CUSA00003-merged.pkg')).Length "mixed case merge has both pieces"
Assert-NoLeftovers $case 'mixed case titles'

# --- 3. dots and spaces in the title ----------------------------------------
Write-Host "`n[3] title with dots and spaces"
$dotted = Reset-Dir (Join-Path $WorkRoot 'dotted')
New-Part (Join-Path $dotted 'Game Name v1.0_0.pkg') 8192 -Magic
New-Part (Join-Path $dotted 'Game Name v1.0_1.pkg') 8192
$dot = Invoke-Tool @('-i', $dotted, '--overwrite', '--verify')
Assert-True ($dot.ExitCode -eq 0) "a dotted title parses (exit $($dot.ExitCode))"
Assert-Equal 16384 (Get-Item (Join-Path $dotted 'Game Name v1.0-merged.pkg')).Length "dotted title merged both pieces"

# --- 4. a long sequence of pieces -------------------------------------------
Write-Host "`n[4] 200 pieces in order"
$many = Reset-Dir (Join-Path $WorkRoot 'many')
for ($i = 0; $i -lt 200; $i++) { New-Part (Join-Path $many ('CUSA00004_{0}.pkg' -f $i)) 1024 -Magic:($i -eq 0) }
$seq = Invoke-Tool @('-i', $many, '--overwrite', '--verify', '--quiet')
Assert-Equal 0 $seq.ExitCode "200 pieces merge successfully"
Assert-Equal 204800 (Get-Item (Join-Path $many 'CUSA00004-merged.pkg')).Length "all 200 pieces landed in order"
Assert-NoLeftovers $many '200 pieces'

# --- 5. pieces supplied out of order ----------------------------------------
Write-Host "`n[5] shuffled piece order on disk"
$shuffled = Reset-Dir (Join-Path $WorkRoot 'shuffled')
New-Part (Join-Path $shuffled 'CUSA00005_3.pkg') 1024
New-Part (Join-Path $shuffled 'CUSA00005_0.pkg') 1024 -Magic
New-Part (Join-Path $shuffled 'CUSA00005_1.pkg') 1024
New-Part (Join-Path $shuffled 'CUSA00005_2.pkg') 1024
$shuf = Invoke-Tool @('-i', $shuffled, '--overwrite', '--verify', '--quiet')
Assert-Equal 0 $shuf.ExitCode "shuffled input merges successfully"
Assert-Equal 4096 (Get-Item (Join-Path $shuffled 'CUSA00005-merged.pkg')).Length "shuffled merge has the right size"

# --- 6. read-only output directory ------------------------------------------
Write-Host "`n[6] read-only output directory"
$roIn = Reset-Dir (Join-Path $WorkRoot 'readonly-in')
New-Part (Join-Path $roIn 'CUSA00006_0.pkg') 4096 -Magic
New-Part (Join-Path $roIn 'CUSA00006_1.pkg') 4096
$roOut = Reset-Dir (Join-Path $WorkRoot 'readonly-out')
& icacls $roOut /deny "$env:USERNAME`:(OI)(CI)(W)" 2>&1 | Out-Null
$ro = Invoke-Tool @('-i', $roIn, '-o', $roOut, '--overwrite')
& icacls $roOut /remove:d "$env:USERNAME" 2>&1 | Out-Null
Assert-True ($ro.ExitCode -ne 0) "a read-only output directory fails (exit $($ro.ExitCode))"
Assert-True ($ro.Output -match 'error') "a read-only output directory is reported as an error"
Assert-NoLeftovers $roIn 'read-only output directory'

# --- 7. a source file locked by another process -----------------------------
Write-Host "`n[7] locked source file"
$lockIn = Reset-Dir (Join-Path $WorkRoot 'locked')
New-Part (Join-Path $lockIn 'CUSA00007_0.pkg') 65536 -Magic
New-Part (Join-Path $lockIn 'CUSA00007_1.pkg') 65536
$lockedFile = Join-Path $lockIn 'CUSA00007_1.pkg'
$handle = [IO.File]::Open($lockedFile, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::None)
try {
    $lk = Invoke-Tool @('-i', $lockIn, '--overwrite')
} finally {
    $handle.Dispose()
}
Assert-True ($lk.ExitCode -ne 0) "a locked source file fails (exit $($lk.ExitCode))"
Assert-True ($lk.Output -match 'error') "the locked file is reported"
Assert-NoLeftovers $lockIn 'locked source file'

# --- 8. very long path -------------------------------------------------------
Write-Host "`n[8] long path"
$long = Join-Path $WorkRoot ('l' * 60)
$deep = $long
for ($i = 0; $i -lt 5; $i++) { $deep = Join-Path $deep ('segment{0}_{1}' -f $i, ('x' * 40)) }
$canCreate = $true
try { New-Item -ItemType Directory -Force -Path $deep | Out-Null } catch { $canCreate = $false }
if (-not $canCreate) {
    Write-Host "  [skip] this system refuses paths longer than 260 characters (long path support off)" -ForegroundColor Yellow
} else {
    New-Part (Join-Path $deep 'CUSA00008_0.pkg') 4096 -Magic
    New-Part (Join-Path $deep 'CUSA00008_1.pkg') 4096
    $lp = Invoke-Tool @('-i', $deep, '--overwrite', '--verify', '--quiet')
    Assert-Equal 0 $lp.ExitCode "a deep path merges successfully (exit $($lp.ExitCode))"
    Assert-Equal 8192 (Get-Item (Join-Path $deep 'CUSA00008-merged.pkg')).Length "deep path merge has the right size"
}

# --- 9. more than 2 GiB ------------------------------------------------------
if ($SkipLarge) {
    Write-Host "`n[9] >2 GiB - skipped on request" -ForegroundColor Yellow
} else {
    Write-Host "`n[9] more than 2 GiB (32-bit size overflow regression)"
    $big = Reset-Dir (Join-Path $WorkRoot 'big')
    # Two 1.25 GiB pieces = 2.5 GiB total: past the 2 GiB mark where a signed
    # 32-bit size counter would wrap. Sparse allocation keeps it quick.
    $each = 1280MB
    foreach ($index in 0, 1) {
        $path = Join-Path $big ('CUSA00009_{0}.pkg' -f $index)
        $stream = [IO.File]::Create($path)
        try {
            $stream.Write([byte[]](0x7F, 0x43, 0x4E, 0x54), 0, 4)   # PKG magic on the root piece
            $stream.SetLength($each)
        } finally { $stream.Dispose() }
    }
    $bigRun = Invoke-Tool @('-i', $big, '--overwrite', '--verify', '--quiet')
    $merged = Join-Path $big 'CUSA00009-merged.pkg'
    $expected = 2 * $each
    $actual = if (Test-Path $merged) { (Get-Item $merged).Length } else { -1 }
    Assert-Equal 0 $bigRun.ExitCode "a 2.5 GiB merge succeeds (exit $($bigRun.ExitCode))"
    Assert-Equal $expected $actual "the merged file is exactly $expected bytes"
    Assert-NoLeftovers $big '2.5 GiB merge'
    Remove-Item -LiteralPath $big -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host ""
if ($script:Failures -eq 0) {
    Write-Host "PASS - $script:Checks checks succeeded" -ForegroundColor Green
    exit 0
}
Write-Host "FAIL - $script:Failures of $script:Checks checks failed" -ForegroundColor Red
exit 1