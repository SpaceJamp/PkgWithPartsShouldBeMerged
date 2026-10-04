# SPDX-License-Identifier: GPL-3.0-only
# PkgWithPartsShouldBeMerged - local bug-check gate (Windows)
#
# Runs the passes that can be checked locally, in order, and prints a summary.
# Every pass that fails is reported; the script exits non-zero if any failed.
#
#   .\tools\check_all.ps1                 # full run, including the >2 GiB case
#   .\tools\check_all.ps1 -SkipLarge      # skip the 2.5 GiB merge
#   .\tools\check_all.ps1 -NoBuild        # reuse existing build folders
#
# Pass 7 (foreign platforms) is NOT covered here - only CI can judge that.
# See AGENTS.md.
#
# SPDX-License-Identifier: GPL-3.0-only. See LICENSE for the licence text.
[CmdletBinding()]
param(
    [string[]]$Arch = @('x64', 'Win32'),
    [switch]$SkipLarge,
    [switch]$NoBuild,
    [switch]$SingleConfig
)

$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$results = [System.Collections.Generic.List[object]]::new()

function Add-Pass([string]$Name, [bool]$Ok, [string]$Detail = '') {
    $results.Add([pscustomobject]@{ Pass = $Name; Ok = $Ok; Detail = $Detail })
    $tag = if ($Ok) { 'ok  ' } else { 'FAIL' }
    $colour = if ($Ok) { 'Green' } else { 'Red' }
    Write-Host "[$tag] $Name $Detail" -ForegroundColor $colour
}

$script:LastOutput = ''

function Invoke-Check([string]$Name, [string]$Exe, [scriptblock]$Action) {
    Write-Host "`n--- $Name ---" -ForegroundColor Cyan
    $output = & $Action 2>&1 | Out-String
    $script:LastOutput = $output
    $code = $LASTEXITCODE
    if ($code -ne 0) {
        Write-Host $output
    }
    Add-Pass $Name ($code -eq 0) "(exit $code)"
    return $code
}

# Reports how many checks a suite claims to have run, taken from its own summary
# line. This is the real coverage signal: counting assertion call sites in the
# source is not, because a helper can run several times per call.
#
# A suite that skips a section for platform reasons still reports those checks in
# its total, so the coverage floor measures intended coverage and cannot be
# lowered by removing assertions. "N total" wins over the bare count when
# present; the bare count is the fallback for a suite that never skips.
function Get-ReportedChecks([string]$Output) {
    if ($Output -match '(\d+)\s+total\b') { return [int]$Matches[1] }
    if ($Output -match '(\d+)\s+checks succeeded') { return [int]$Matches[1] }
    return -1
}

# --- Pass 1: build every shipping configuration ------------------------------
if (-not $NoBuild) {
    foreach ($a in $Arch) {
        $dir = "build\$($a.ToLower())"
        $ok = $true
        & cmake -S . -B $dir -G "Visual Studio 17 2022" -A $a -DPKG_MERGE_WERROR=ON *> $null
        if ($LASTEXITCODE -ne 0) { $ok = $false } else {
            & cmake --build $dir --config Release --parallel *> $null
            if ($LASTEXITCODE -ne 0) { $ok = $false }
        }
        Add-Pass "pass 1 build $a Release (/W4 /WX)" $ok
    }

    if ($SingleConfig) {
        $vcvars = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
        if (-not (Test-Path $vcvars)) {
            $vcvars = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
        }
        if (Test-Path $vcvars) {
            $ok = $true
            & cmd /c "call `"$vcvars`" >nul 2>&1 && cd /d `"$root`" && cmake -S . -B build\nmake -G `"NMake Makefiles`" -DPKG_MERGE_WERROR=ON && cmake --build build\nmake" *> $null
            if ($LASTEXITCODE -ne 0) { $ok = $false }
            Add-Pass "pass 1 build single-config (NMake defaults to Release)" $ok
        } else {
            Write-Host "[skip] no vcvars64.bat found for the single-config check" -ForegroundColor Yellow
        }
    }
}

# --- Pass 3: test-harness integrity ------------------------------------------
# A suite that cannot fail proves nothing, so validate the harness itself: the
# scripts must parse, and every helper they call must actually be defined. (Two
# suites in this repo's history aborted mid-run because a helper was missing.)
Write-Host "`n--- pass 3 test-harness integrity ---" -ForegroundColor Cyan
$requiredHelpers = @{
    'tests\smoke_test.ps1'      = @('Assert-True', 'Assert-Equal', 'Assert-MergedContent', 'New-PartFile', 'Invoke-Tool', 'Reset-Dir')
    'tests\robustness_test.ps1' = @('Assert-True', 'Assert-Equal', 'New-Part', 'Invoke-Tool', 'Reset-Dir')
}
foreach ($suite in $requiredHelpers.Keys) {
    if (-not (Test-Path $suite)) {
        Add-Pass "pass 3 $suite exists" $false
        continue
    }
    $parseErrors = $null
    $ast = [System.Management.Automation.Language.Parser]::ParseFile((Resolve-Path $suite), [ref]$null, [ref]$parseErrors)
    Add-Pass "pass 3 $suite parses" ($null -eq $parseErrors -or $parseErrors.Count -eq 0)

    $defined = @($ast.FindAll({ param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] }, $true) |
        ForEach-Object { $_.Name })
    $missing = @($requiredHelpers[$suite] | Where-Object { $defined -notcontains $_ })
    Add-Pass "pass 3 $suite defines its helpers" ($missing.Count -eq 0) `
        $(if ($missing.Count) { "missing: $($missing -join ', ')" } else { '' })
}

# --- Passes 4/5/6 on the primary architecture -------------------------------
$primary = if ($Arch -contains 'x64') { 'build\x64\Release\pkg_merge.exe' } else { "build\$($Arch[0].ToLower())\Release\pkg_merge.exe" }

# The target is named pkg_merge (see add_executable in CMakeLists.txt), which is
# also what CI runs and what the version resource declares as OriginalFilename.
foreach ($a in $Arch) {
    $dir = if ($a -eq 'x64') { 'build\x64\Release' } else { "build\$($a.ToLower())\Release" }
    $exe = Join-Path $root "$dir\pkg_merge.exe"
    if (-not (Test-Path $exe)) {
        Add-Pass "pass 6 verify binary ($a)" $false "not built"
        continue
    }
    $null = Invoke-Check "pass 6 verify binary ($a)" $exe {
        python tools\verify_binary.py $exe 3.1.0
    }
    $smokeCode = Invoke-Check "pass 4 smoke test ($a)" $exe {
        powershell -NoProfile -ExecutionPolicy Bypass -File tests\smoke_test.ps1 -Exe $exe -WorkRoot "$env:TEMP\PkgWithPartsShouldBeMerged-check-$a"
    }
    # Coverage must not silently shrink: 65 checks are expected on Windows.
    # Only meaningful when the suite passed: assert_merged_content stops after
    # its first check when the merge failed, so a failing run reports a lower
    # count that says nothing about coverage.
    if ($smokeCode -eq 0) {
        $reported = Get-ReportedChecks $script:LastOutput
        Add-Pass "pass 4 smoke coverage ($a)" ($reported -ge 73) "reported $reported, expected >= 73"
    } else {
        Write-Host "[skip] pass 4 smoke coverage ($a): suite failed, count not comparable" -ForegroundColor Yellow
    }
}

$robustArgs = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', 'tests\robustness_test.ps1',
    '-Exe', $primary, '-WorkRoot', "$env:TEMP\PkgWithPartsShouldBeMerged-check-robust")
if ($SkipLarge) { $robustArgs += '-SkipLarge' }
$robustCode = Invoke-Check "pass 5 robustness test$(if ($SkipLarge) { ' (without >2 GiB)' })" $primary {
    powershell @robustArgs
}
$robustFloor = if ($SkipLarge) { 24 } else { 27 }
if ($robustCode -eq 0) {
    $robustReported = Get-ReportedChecks $script:LastOutput
    Add-Pass "pass 5 robustness coverage" ($robustReported -ge $robustFloor) "reported $robustReported, expected >= $robustFloor"
} else {
    Write-Host "[skip] pass 5 robustness coverage: suite failed, count not comparable" -ForegroundColor Yellow
}

# --- Summary -----------------------------------------------------------------
Write-Host "`n================ summary ================" -ForegroundColor Cyan
$failed = 0
foreach ($r in $results) {
    if (-not $r.Ok) { $failed++ }
    $tag = if ($r.Ok) { 'ok  ' } else { 'FAIL' }
    $colour = if ($r.Ok) { 'Green' } else { 'Red' }
    Write-Host ("  [{0}] {1} {2}" -f $tag, $r.Pass, $r.Detail) -ForegroundColor $colour
}
Write-Host ""
Write-Host "pass 7 (foreign platforms) is not covered here - check the CI run."
Write-Host "https://github.com/SpaceJamp/PkgWithPartsShouldBeMerged/actions"
Write-Host ""
if ($failed -eq 0) {
    Write-Host "LOCAL GATE PASSED ($($results.Count) checks)" -ForegroundColor Green
    exit 0
}
Write-Host "LOCAL GATE FAILED ($failed of $($results.Count) checks failed)" -ForegroundColor Red
exit 1