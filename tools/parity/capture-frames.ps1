<#
.SYNOPSIS
    Parity capture harness - drive the game along a fixed frame schedule and
    collect its F9 PNG frames into a timestamped, backend-labelled output dir.
    Feeds w3d_parity_diff for the DX8-vs-D3D11 A/B parity pass (RENDERER_PORT.md
    step 10). See tools/parity/README.md for the full A/B procedure.

.DESCRIPTION
    The game's -stratagemShot auto-capture boots straight into an AI skirmish and
    writes zp_screenshot_NNN.png into %W3DNEXT_SCRSHOTS% on a DETERMINISTIC
    FRAME schedule (warm-up 150 ticks, then every 240 ticks, driven from the
    lockstep GameLogic path - not wall-clock), then exits itself. Same map + same
    build => the same frames are captured at the same ticks, which is exactly what
    a frame-by-frame backend A/B needs.

    This script redirects W3DNEXT_SCRSHOTS to  scrshots\parity\<label>_<stamp>\
    so a DX8 run and a (future) D3D11 run land in separate, comparable dirs.

    BACKEND SELECTION IS NOT WIRED YET. The D3D11 backend is not constructed by
    the running game, so -Backend only stamps the output dir name today. When
    step 10 adds the backend-select flag (see README - proposed  -gfxBackend
    d3d11 ), pass it through here via -ExtraArgs and the two dirs become a real
    DX8-vs-D3D11 A/B.

.PARAMETER GameDir
    Folder containing generalszh.exe (the deployed build). Required.

.PARAMETER Backend
    Label only: dx8 (default) or d3d11. Names the output dir; does NOT select a
    backend until step 10 wires the flag.

.PARAMETER Map
    Optional map path passed to -stratagemShot (else the first cached MP map).

.PARAMETER Shots
    Number of frames to capture (-stratagemShots). Default 16.

.PARAMETER ExtraArgs
    Extra raw args appended to the launch (e.g. the future -gfxBackend d3d11).

.EXAMPLE
    # Reference DX8 capture:
    .\capture-frames.ps1 -GameDir "D:\Games\Zero Hour" -Backend dx8

.EXAMPLE
    # Step-10 D3D11 capture (once the backend-select flag exists):
    .\capture-frames.ps1 -GameDir "D:\Games\Zero Hour" -Backend d3d11 `
        -ExtraArgs '-gfxBackend','d3d11'
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)] [string] $GameDir,
    [ValidateSet('dx8','d3d11')] [string] $Backend = 'dx8',
    [string] $Map = '',
    [int]    $Shots = 16,
    [string[]] $ExtraArgs = @(),
    [int]    $TimeoutSec = 300
)

$ErrorActionPreference = 'Stop'
$repoRoot = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$exe = Join-Path $GameDir 'generalszh.exe'
if (-not (Test-Path $exe)) { throw "generalszh.exe not found in $GameDir" }

$stamp   = Get-Date -Format 'yyyyMMdd_HHmmss'
$outDir  = Join-Path $repoRoot "scrshots\parity\${Backend}_${stamp}"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

# The game reads W3DNEXT_SCRSHOTS (must end in a separator) for the F9 / auto
# capture destination; point it at our per-run dir.
$env:W3DNEXT_SCRSHOTS = "$outDir\"

$launch = @('-win','-stratagemShot','-stratagemShots', "$Shots", '-ignoreAsserts')
if ($Map) { $launch += $Map }
$launch += $ExtraArgs

Write-Host "Parity capture -> $outDir"
Write-Host "  backend label : $Backend   (NOTE: backend not selectable until step 10)"
Write-Host "  launch        : $($launch -join ' ')"

Push-Location $GameDir
try {
    $p = Start-Process -FilePath $exe -ArgumentList $launch -PassThru
    if (-not $p.WaitForExit($TimeoutSec * 1000)) {
        Write-Warning "Game did not exit within $TimeoutSec s; killing."
        $p.Kill()
    }
} finally {
    Pop-Location
}

$pngs = Get-ChildItem -Path $outDir -Filter '*.png' -ErrorAction SilentlyContinue
Write-Host ("Captured {0} frame(s) into {1}" -f $pngs.Count, $outDir)
if ($pngs.Count -eq 0) {
    Write-Warning "No PNGs captured. Check the build, the map arg, and that this build's -stratagemShot path is intact."
    exit 1
}
# Emit the dir path on stdout (last line) so a wrapper can capture it.
Write-Output $outDir
