# SPDX-License-Identifier: GPL-3.0-only
# PkgWithPartsShouldBeMerged smoke test - Windows PowerShell
#
# Builds a synthetic set of "PKG pieces" (deterministic bytes, the root piece
# carries the 0x7F "CNT" magic) and exercises the merge tool end to end,
# including every exit code path.
#
# Usage:  pwsh -File tests/smoke_test.ps1 -Exe path\to\pkg_merge.exe
#
# SPDX-License-Identifier: GPL-3.0-only. See LICENSE for the licence text.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [string]$WorkRoot = (Join-Path ([IO.Path]::GetTempPath()) "PkgWithPartsShouldBeMerged-smoke")
)

$ErrorActionPreference = 'Stop'
$script:Failures = 0
$script:Checks = 0

function Assert-True([bool]$Condition, [string]$Message) {
    $script:Checks++
    if ($Condition) {
        Write-Host "  [ok]   $Message" -ForegroundColor DarkGreen
    } else {
        $script:Failures++
        Write-Host "  [FAIL] $Message" -ForegroundColor Red
    }
}

function Assert-Equal($Expected, $Actual, [string]$Message) {
    Assert-True ($Expected -eq $Actual) "$Message (expected '$Expected', got '$Actual')"
}

function Reset-Dir([string]$Path) {
    if (Test-Path -LiteralPath $Path) { Remove-Item -Recurse -Force -LiteralPath $Path }
    New-Item -ItemType Directory -Force -Path $Path | Out-Null
    return (Resolve-Path -LiteralPath $Path).Path
}

# Deterministic filler so merged output can be compared byte for byte.
function New-PartFile([string]$Path, [int]$Bytes, [int]$Seed, [switch]$Magic) {
    $buffer = New-Object byte[] $Bytes
    $state = [uint32]$Seed
    for ($i = 0; $i -lt $Bytes; $i++) {
        $state = [uint32](($state * 1103515245 + 12345) -band 0x7FFFFFFF)
        $buffer[$i] = [byte](($state -shr 16) -band 0xFF)
    }
    if ($Magic) { $buffer[0] = 0x7F; $buffer[1] = 0x43; $buffer[2] = 0x4E; $buffer[3] = 0x54 }
    [IO.File]::WriteAllBytes($Path, $buffer)
}

function New-Game([string]$Dir, [string]$TitleId, [int]$Parts, [int]$PartSize = 64 * 1024) {
    $paths = @()
    for ($i = 0; $i -lt $Parts; $i++) {
        $path = Join-Path $Dir ("{0}_{1}.pkg" -f $TitleId, $i)
        New-PartFile -Path $path -Bytes ($PartSize + $i) -Seed (1000 + $i) -Magic:($i -eq 0)
        $paths += $path
    }
    return $paths
}

function Invoke-Tool([string[]]$ToolArgs) {
    # A native command writing to stderr produces ErrorRecords, and under
    # $ErrorActionPreference = 'Stop' the first one terminates the script. The
    # suite sets 'Stop' so that a broken fixture fails loudly, but it must still
    # be able to capture a diagnostic from the tool - which is the whole point of
    # 2>&1 below. Relax the preference for the duration of the call only.
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = & $Exe @ToolArgs 2>&1 | Out-String
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previous
    }
    return [pscustomobject]@{ ExitCode = $code; Output = $output }
}

function Assert-MergedContent([string]$OutDir, [string]$TitleId, [string[]]$Sources) {
    $merged = Join-Path $OutDir "$TitleId-merged.pkg"
    Assert-True (Test-Path -LiteralPath $merged) "$TitleId-merged.pkg was created"
    if (-not (Test-Path -LiteralPath $merged)) { return }

    $expected = New-Object byte[] 0
    $stream = New-Object IO.MemoryStream
    foreach ($source in $Sources) {
        $bytes = [IO.File]::ReadAllBytes($source)
        $stream.Write($bytes, 0, $bytes.Length)
    }
    $expected = $stream.ToArray()
    $actual = [IO.File]::ReadAllBytes($merged)
    Assert-Equal $expected.Length $actual.Length "$TitleId-merged.pkg has the expected size"
    $same = $true
    if ($expected.Length -eq $actual.Length) {
        for ($i = 0; $i -lt $expected.Length; $i++) {
            if ($expected[$i] -ne $actual[$i]) { $same = $false; break }
        }
    }
    Assert-True $same "$TitleId-merged.pkg is byte identical to the concatenation of its pieces"
}

Write-Host "PkgWithPartsShouldBeMerged smoke test" -ForegroundColor Cyan
Write-Host "  exe: $Exe"
Assert-True (Test-Path -LiteralPath $Exe) "executable exists"

# --- 1. --help / --version ----------------------------------------------------
Write-Host "`n[1] help and version"
$help = Invoke-Tool @('--help')
Assert-Equal 0 $help.ExitCode "--help exits with 0"
Assert-True ($help.Output -match '--input') "--help documents --input"
Assert-True ($help.Output -match '--no-clobber') "--help documents --no-clobber"
$version = Invoke-Tool @('--version')
Assert-Equal 0 $version.ExitCode "--version exits with 0"

# --- 2. unknown option / missing value ----------------------------------------
Write-Host "`n[2] usage errors"
$unknown = Invoke-Tool @('--definitely-not-an-option')
Assert-Equal 1 $unknown.ExitCode "unknown option exits with 1"
Assert-True ($unknown.Output -match 'definitely-not-an-option') "the unknown option is named"
$missing = Invoke-Tool @('--input')
Assert-Equal 1 $missing.ExitCode "missing option value exits with 1"

# --- 3. not a directory -------------------------------------------------------
Write-Host "`n[3] bad input path"
$root = Reset-Dir $WorkRoot
$file = Join-Path $root "not-a-folder.pkg"
New-PartFile -Path $file -Bytes 1024 -Seed 7 -Magic
$notADir = Invoke-Tool @('-i', $file)
Assert-Equal 1 $notADir.ExitCode "input that is a file exits with 1"
Assert-True ($notADir.Output -match 'not-a-folder\.pkg') "the offending path is named"

$emptyIn = Join-Path $root "empty-in"; Reset-Dir $emptyIn | Out-Null
$emptyDir = Invoke-Tool @('-i', $emptyIn)
Assert-Equal 1 $emptyDir.ExitCode "empty folder exits with 1"
$missingOut = Invoke-Tool @('-i', $emptyIn, '-o', (Join-Path $root 'nope'))
Assert-Equal 1 $missingOut.ExitCode "missing output directory exits with 1"

# --- 4. two games in one folder (the old key-collision bug) -------------------
Write-Host "`n[4] two games sharing part numbers"
$games = Join-Path $root "games"; Reset-Dir $games | Out-Null
$outDir = Join-Path $root "out"; Reset-Dir $outDir | Out-Null
$gameA = New-Game -Dir $games -TitleId 'CUSA11111' -Parts 3
$gameB = New-Game -Dir $games -TitleId 'CUSA22222' -Parts 2
# noise that must be ignored
New-PartFile -Path (Join-Path $games 'readme.txt') -Bytes 512 -Seed 3
[IO.File]::WriteAllBytes((Join-Path $games 'notes.PKG'), (New-Object byte[] 64))
$merge = Invoke-Tool @('-i', $games, '-o', $outDir, '--verify', '--quiet')
Assert-Equal 0 $merge.ExitCode "merging two games exits with 0"
Assert-MergedContent -OutDir $outDir -TitleId 'CUSA11111' -Sources $gameA
Assert-MergedContent -OutDir $outDir -TitleId 'CUSA22222' -Sources $gameB

# --- 5. positional arguments (drag & drop compatibility) ----------------------
Write-Host "`n[5] positional input and output"
$posIn = Join-Path $root "positional-in"; Reset-Dir $posIn | Out-Null
$posOut = Join-Path $root "positional-out"; Reset-Dir $posOut | Out-Null
$posGame = New-Game -Dir $posIn -TitleId 'CUSA33333' -Parts 2
$pos = Invoke-Tool @($posIn, $posOut)
Assert-Equal 0 $pos.ExitCode "positional arguments exit with 0"
Assert-MergedContent -OutDir $posOut -TitleId 'CUSA33333' -Sources $posGame

# --- 6. dry run writes nothing ------------------------------------------------
Write-Host "`n[6] dry run"
$dryIn = Join-Path $root "dry-in"; Reset-Dir $dryIn | Out-Null
$dryGame = New-Game -Dir $dryIn -TitleId 'CUSA44444' -Parts 2
$dry = Invoke-Tool @('-i', $dryIn, '--dry-run', '--overwrite')
Assert-Equal 0 $dry.ExitCode "--dry-run exits with 0"
Assert-True (-not (Test-Path (Join-Path $dryIn 'CUSA44444-merged.pkg'))) "--dry-run wrote nothing"

# --- 7. gap in the sequence ---------------------------------------------------
Write-Host "`n[7] missing piece"
$gapIn = Join-Path $root "gap-in"; Reset-Dir $gapIn | Out-Null
New-PartFile -Path (Join-Path $gapIn 'CUSA55555_0.pkg') -Bytes 1024 -Seed 1 -Magic
New-PartFile -Path (Join-Path $gapIn 'CUSA55555_2.pkg') -Bytes 1024 -Seed 2
$gap = Invoke-Tool @('-i', $gapIn, '--overwrite')
Assert-Equal 1 $gap.ExitCode "gap in the part sequence fails"
# SPEC 6.2: the message must name the first missing number, which is 1 here.
Assert-True (($gap.Output -match 'CUSA55555') -and ($gap.Output -match '(?<!\d)1(?!\d)')) "the diagnostic names the set and the missing number 1"
Assert-True (-not (Test-Path (Join-Path $gapIn 'CUSA55555-merged.pkg'))) "no output written on a gap"

# --- 8. pieces without a root -------------------------------------------------
Write-Host "`n[8] pieces without a root piece"
$orphanIn = Join-Path $root "orphan-in"; Reset-Dir $orphanIn | Out-Null
New-PartFile -Path (Join-Path $orphanIn 'CUSA66666_1.pkg') -Bytes 1024 -Seed 4
New-PartFile -Path (Join-Path $orphanIn 'CUSA66666_2.pkg') -Bytes 1024 -Seed 5
$orphan = Invoke-Tool @('-i', $orphanIn, '--overwrite')
Assert-Equal 1 $orphan.ExitCode "missing root piece fails instead of crashing"
# SPEC 6.1: the message must name the missing file name.
Assert-True ($orphan.Output -match 'CUSA66666_0\.pkg') "the missing root piece's file name is named"

# --- 9. duplicate part number -------------------------------------------------
Write-Host "`n[9] duplicate part number"
$dupIn = Join-Path $root "dup-in"; Reset-Dir $dupIn | Out-Null
New-Game -Dir $dupIn -TitleId 'CUSA77777' -Parts 2 | Out-Null
New-PartFile -Path (Join-Path $dupIn 'CUSA77777_01.pkg') -Bytes 1024 -Seed 9
$dup = Invoke-Tool @('-i', $dupIn, '--overwrite')
Assert-Equal 1 $dup.ExitCode "duplicate part number fails"
# SPEC 6.3: the message must name both file names.
Assert-True (($dup.Output -match 'CUSA77777_1\.pkg') -and ($dup.Output -match 'CUSA77777_01\.pkg')) "both duplicate file names are named"

# --- 10. empty piece ----------------------------------------------------------
Write-Host "`n[10] empty piece"
$emptyPieceIn = Join-Path $root "emptypiece-in"; Reset-Dir $emptyPieceIn | Out-Null
New-PartFile -Path (Join-Path $emptyPieceIn 'CUSA88888_0.pkg') -Bytes 1024 -Seed 6 -Magic
[IO.File]::WriteAllBytes((Join-Path $emptyPieceIn 'CUSA88888_1.pkg'), (New-Object byte[] 0))
$emptyPiece = Invoke-Tool @('-i', $emptyPieceIn, '--overwrite')
Assert-Equal 1 $emptyPiece.ExitCode "an empty piece fails"
# SPEC 6.4: the message must name the offending file.
Assert-True ($emptyPiece.Output -match 'CUSA88888_1\.pkg') "the empty piece's file name is named"

# --- 11. overwrite / no-clobber ----------------------------------------------
Write-Host "`n[11] overwrite policy"
$overIn = Join-Path $root "over-in"; Reset-Dir $overIn | Out-Null
$overGame = New-Game -Dir $overIn -TitleId 'CUSA99999' -Parts 2
Invoke-Tool @('-i', $overIn, '--overwrite') | Out-Null
$mergedPath = Join-Path $overIn 'CUSA99999-merged.pkg'
$firstHash = (Get-FileHash -LiteralPath $mergedPath -Algorithm SHA256).Hash
$noClobber = Invoke-Tool @('-i', $overIn, '--no-clobber')
Assert-Equal 0 $noClobber.ExitCode "--no-clobber exits with 0"
Assert-True ($noClobber.Output -match 'CUSA99999-merged\.pkg') "--no-clobber names the file it skipped"
$secondHash = (Get-FileHash -LiteralPath $mergedPath -Algorithm SHA256).Hash
Assert-Equal $firstHash $secondHash "--no-clobber left the existing file untouched"
$conflict = Invoke-Tool @('-i', $overIn, '--no-clobber', '--overwrite')
Assert-Equal 1 $conflict.ExitCode "--no-clobber with --overwrite is rejected"

# non-interactive run without --overwrite must keep the old file, not destroy it
$mutateIn = Join-Path $root "mutate-in"; Reset-Dir $mutateIn | Out-Null
$mutateGame = New-Game -Dir $mutateIn -TitleId 'CUSA12121' -Parts 2
Invoke-Tool @('-i', $mutateIn, '--overwrite') | Out-Null
$mutateMerged = Join-Path $mutateIn 'CUSA12121-merged.pkg'
$before = (Get-FileHash -LiteralPath $mutateMerged -Algorithm SHA256).Hash
New-PartFile -Path (Join-Path $mutateIn 'CUSA12121_2.pkg') -Bytes 4096 -Seed 21
$declined = Invoke-Tool @('-i', $mutateIn)   # stdin/stdout are pipes: not interactive
$after = (Get-FileHash -LiteralPath $mutateMerged -Algorithm SHA256).Hash
Assert-Equal $before $after "declining the prompt keeps the previous merged file"
Assert-True ($declined.Output -match 'CUSA12121-merged\.pkg') "the kept file is named in the non-interactive skip"
$forced = Invoke-Tool @('-i', $mutateIn, '--overwrite', '--verify')
Assert-Equal 0 $forced.ExitCode "--overwrite replaces the file"
$mutateGame += (Join-Path $mutateIn 'CUSA12121_2.pkg')   # the third piece added above
Assert-MergedContent -OutDir $mutateIn -TitleId 'CUSA12121' -Sources $mutateGame

# --- 12. verify catches a corrupted merge ------------------------------------
Write-Host "`n[12] verification"
$verifyIn = Join-Path $root "verify-in"; Reset-Dir $verifyIn | Out-Null
$verifyGame = New-Game -Dir $verifyIn -TitleId 'CUSA13131' -Parts 2
$verifyRun = Invoke-Tool @('-i', $verifyIn, '--overwrite', '--verify')
Assert-Equal 0 $verifyRun.ExitCode "--verify succeeds on a good merge"
Assert-True ($verifyRun.Output -match 'CUSA13131-merged\.pkg') "--verify names the file it verified"

# --- 13. recursive scan -------------------------------------------------------
Write-Host "`n[13] recursive scan"
$recIn = Join-Path $root "rec-in"; Reset-Dir $recIn | Out-Null
$sub = Join-Path $recIn "sub"; New-Item -ItemType Directory -Force -Path $sub | Out-Null
$recGame = New-Game -Dir $sub -TitleId 'CUSA14141' -Parts 2
$flat = Invoke-Tool @('-i', $recIn, '--overwrite')
Assert-Equal 1 $flat.ExitCode "a flat scan that finds nothing fails"
Assert-True ($flat.Output.Trim().Length -gt 0) "the empty scan produces a diagnostic"
Assert-True ($flat.Output -match 'rec-in') "the diagnostic names the folder that was scanned"
Assert-True (-not (Test-Path (Join-Path $sub 'CUSA14141-merged.pkg'))) "nested game is skipped without -r"
$rec = Invoke-Tool @('-i', $recIn, '-r', '--overwrite', '--verify')
Assert-Equal 0 $rec.ExitCode "recursive scan exits with 0"
# The output directory defaults to the *input* directory, not to the sub folder.
Assert-MergedContent -OutDir $recIn -TitleId 'CUSA14141' -Sources $recGame

# --- 14. non-ASCII path -------------------------------------------------------
Write-Host "`n[14] non-ASCII path"
$uniRoot = Join-Path $root "Ünïcödé 日本語 🎮"
$uniIn = Join-Path $uniRoot "in"; Reset-Dir $uniIn | Out-Null
$uniOut = Join-Path $uniRoot "out"; Reset-Dir $uniOut | Out-Null
$uniGame = New-Game -Dir $uniIn -TitleId 'CUSA15151' -Parts 2
$uni = Invoke-Tool @('-i', $uniIn, '-o', $uniOut, '--overwrite', '--verify')
Assert-Equal 0 $uni.ExitCode "non-ASCII paths exit with 0"
Assert-MergedContent -OutDir $uniOut -TitleId 'CUSA15151' -Sources $uniGame
Assert-True ($uni.Output -notmatch '\?\?') "no mangled characters in the output"

# --- 15. already merged files are ignored ------------------------------------
Write-Host "`n[15] re-running is idempotent"
$idemIn = Join-Path $root "idem-in"; Reset-Dir $idemIn | Out-Null
$idemGame = New-Game -Dir $idemIn -TitleId 'CUSA16161' -Parts 2
# Snapshot after the fixture exists, before the tool ever runs.
# SPEC 7.1 and 18.2: no temporary file may be left behind, and the temporary
# file's name is the implementer's choice. A before/after comparison is naming
# independent; filtering on "is not a .pkg" would be wrong, because a fixture may
# legitimately contain files with other extensions.
$idemBefore = @(Get-ChildItem -LiteralPath $idemIn -Recurse -File | ForEach-Object { $_.FullName })
Invoke-Tool @('-i', $idemIn, '--overwrite') | Out-Null
$again = Invoke-Tool @('-i', $idemIn, '--overwrite', '--verify')
Assert-Equal 0 $again.ExitCode "a second run succeeds"
Assert-MergedContent -OutDir $idemIn -TitleId 'CUSA16161' -Sources $idemGame
Assert-Equal 1 (@(Get-ChildItem -LiteralPath $idemIn -Filter '*-merged.pkg')).Count "only one merged file exists"
$idemStray = @(Get-ChildItem -LiteralPath $idemIn -Recurse -File |
    Where-Object { $idemBefore -notcontains $_.FullName -and $_.Name -notmatch '-merged\.pkg$' })
Assert-Equal 0 $idemStray.Count "no temporary files are left behind (unexpected: $(if ($idemStray) { ($idemStray | ForEach-Object { Split-Path -Leaf $_.FullName }) -join ', ' } else { 'none' }))"

# --- 16. numeric piece ordering (SPEC 5) --------------------------------------
# SPEC 5 and 18.3 both single this out: ordering must be by piece number, not by
# file name. Past piece 9 a string sort puts _10 before _2 and produces a
# corrupt file with no error, so this needs 12 pieces AND a byte-exact
# comparison - a length check cannot see it. Every other set here has 3 pieces or
# fewer, where both orders agree, and the 200-piece set in the robustness suite
# is 200 byte-identical pieces, so nothing else would catch this.
Write-Host "`n[16] numeric ordering past piece 9"
$ordIn = Join-Path $root "ord-in"; Reset-Dir $ordIn | Out-Null
$ordGame = New-Game -Dir $ordIn -TitleId 'CUSA17171' -Parts 12
$ord = Invoke-Tool @('-i', $ordIn, '--overwrite', '--verify', '--quiet')
Assert-Equal 0 $ord.ExitCode "a 12-piece set merges"
Assert-MergedContent -OutDir $ordIn -TitleId 'CUSA17171' -Sources $ordGame

# --- 17. root piece without the PKG magic (SPEC 6.5) --------------------------
# SPEC 6.5 is a warning only and "must never prevent a merge". Every other
# fixture puts the magic on its root piece, so nothing here would otherwise
# prove that a root piece lacking it still merges, unchanged.
Write-Host "`n[17] root piece without the PKG magic"
$noMagicIn = Join-Path $root "nomagic-in"; Reset-Dir $noMagicIn | Out-Null
New-PartFile -Path (Join-Path $noMagicIn 'CUSA18181_0.pkg') -Bytes 2048 -Seed 31
New-PartFile -Path (Join-Path $noMagicIn 'CUSA18181_1.pkg') -Bytes 4096 -Seed 32
$noMagicGame = @(
    (Join-Path $noMagicIn 'CUSA18181_0.pkg'),
    (Join-Path $noMagicIn 'CUSA18181_1.pkg')
)
$noMagic = Invoke-Tool @('-i', $noMagicIn, '--overwrite', '--verify', '--quiet')
Assert-Equal 0 $noMagic.ExitCode "a root piece without the PKG magic still merges"
Assert-MergedContent -OutDir $noMagicIn -TitleId 'CUSA18181' -Sources $noMagicGame

# --- 18. backup, JSON output and build metadata ------------------------------
Write-Host "`n[18] backup, JSON and version metadata"
$bkIn = Join-Path $root "backup-in"; Reset-Dir $bkIn | Out-Null
New-Game -Dir $bkIn -TitleId 'CUSA19191' -Parts 2 | Out-Null
Invoke-Tool @('-i', $bkIn, '--overwrite') | Out-Null
$bkMerged = Join-Path $bkIn 'CUSA19191-merged.pkg'
$bkFirst = (Get-FileHash $bkMerged -Algorithm SHA256).Hash
# Different bytes, so the re-merge cannot produce the same output by accident.
New-PartFile -Path (Join-Path $bkIn 'CUSA19191_1.pkg') -Bytes 40000 -Seed 41
$bk = Invoke-Tool @('-i', $bkIn, '--overwrite', '--backup')
Assert-Equal 0 $bk.ExitCode "a merge with --backup succeeds"
$bkKept = @(Get-ChildItem -LiteralPath $bkIn -File |
    Where-Object { $_.Name -ne 'CUSA19191-merged.pkg' -and $_.Name -like 'CUSA19191-merged.pkg*' })
Assert-Equal 1 $bkKept.Count "--backup kept exactly one copy of the previous output"
Assert-Equal $bkFirst (Get-FileHash $bkKept[0].FullName -Algorithm SHA256).Hash "the kept copy is the previous output, byte for byte"
Assert-Equal $false ((Get-FileHash $bkMerged -Algorithm SHA256).Hash -eq $bkFirst) "the new output really did replace it"

# --json has to be the only thing on stdout, or a caller cannot parse it. The
# fixture has the container marker on its root piece, so there is no warning to
# interleave either.
$jsIn = Join-Path $root "json-in"; Reset-Dir $jsIn | Out-Null
New-Game -Dir $jsIn -TitleId 'CUSA20202' -Parts 2 | Out-Null
$js = Invoke-Tool @('-i', $jsIn, '--overwrite', '--json')
Assert-Equal 0 $js.ExitCode "a --json run succeeds"
$parsed = $null
try { $parsed = $js.Output | ConvertFrom-Json } catch { $parsed = $null }
Assert-True ($null -ne $parsed) "--json emits parseable JSON and nothing else"
Assert-Equal 1 $parsed.counts.merged "--json reports the merged count"
Assert-Equal 1 $parsed.sets.Count "--json reports one entry per set"

$ver = Invoke-Tool @('--version')
Assert-True ($ver.Output -match '\([0-9a-f]{7,}\)') "--version reports the commit it was built from"

# SPEC 10 and 14: the summary is informational, so --quiet suppresses it. This
# was ambiguous in the specification until it was pinned there; pin it here too.
$qIn = Join-Path $root "quiet-in"; Reset-Dir $qIn | Out-Null
New-Game -Dir $qIn -TitleId 'CUSA21212' -Parts 2 | Out-Null
$q = Invoke-Tool @('-i', $qIn, '--overwrite', '--quiet')
Assert-Equal 0 $q.ExitCode "--quiet still merges"
Assert-Equal '' $q.Output.Trim() "--quiet prints no informational output, not even the summary"
Assert-True (Test-Path (Join-Path $qIn 'CUSA21212-merged.pkg')) "--quiet still wrote the output"

# --- summary ------------------------------------------------------------------
Write-Host ""
if ($script:Failures -eq 0) {
    Write-Host "PASS - $script:Checks checks succeeded" -ForegroundColor Green
    exit 0
}
Write-Host "FAIL - $script:Failures of $script:Checks checks failed" -ForegroundColor Red
exit 1