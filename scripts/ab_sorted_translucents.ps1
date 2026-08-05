<#
.SYNOPSIS
  Three-way framedump A/B for the sorted-translucent transform fix
  (invisible rotor / rockets / launch smoke family).

.DESCRIPTION
  Scene: -stratagemShot AI-vs-AI skirmish (deterministic logic; runs to
  f3750), dumps at logic frames 1800/2700 where AI construction/combat
  particles exist.

  Runs, in order, against the exe staged as generalszh_fpsprobe.exe:
    dx8_truth      no -d3d11    (ground truth; fix-independent path)
    d3d11_<Tag>    -d3d11       (Tag names the build under test)

  The caller stages the exe (pre-fix vs post-fix) and picks -Tag; the
  negative control is running this once with the pre-fix exe (particles
  absent -> high mae vs dx8) and once with the post-fix exe (mae drops).
  Skips the dx8 run if -SkipDx8 (truth doesn't change between stagings).

  fps side-effect check: t_ms stamps ride the same dump lines, so the log
  doubles as a non-regression breadcrumb.
#>
param(
  [string]$GameDir = 'C:\Program Files (x86)\Steam\steamapps\common\Command & Conquer Generals - Zero Hour',
  [string]$BuildDir = 'build\win32-debug',
  [Parameter(Mandatory)][string]$Tag,     # e.g. 'prefix' / 'postfix'
  [string]$Frames  = '1800,2700',
  [int]$Timeout    = 420,
  [switch]$SkipDx8,
  [string]$OutDir  = ''                    # reuse one out dir across stagings
)
$ErrorActionPreference = 'Stop'

$repo      = Split-Path $PSScriptRoot -Parent
$buildDir  = Join-Path $repo $BuildDir
$stagedExe = Join-Path $GameDir 'generalszh_fpsprobe.exe'
if (-not (Test-Path $stagedExe)) { throw "missing staged exe $stagedExe" }

if ($OutDir -eq '') {
  $stamp  = Get-Date -Format 'yyyyMMdd-HHmmss'
  $OutDir = Join-Path $buildDir "ab_sorted_$stamp"
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
Write-Host "=== sorted-translucent A/B (tag=$Tag) -> $OutDir ===" -ForegroundColor Cyan

$ladder = [int[]]($Frames -split ',' | ForEach-Object { $_.Trim() })
$fB = $ladder[-1]

function Wait-FieldClear([int]$sec = 90) {
  $deadline = (Get-Date).AddSeconds($sec)
  while ((Get-Date) -lt $deadline) {
    if (-not (Get-Process -Name 'generalszh','generalszh_texverify','generalszh_driftprobe','generalszh_fpsprobe','generals' -ErrorAction SilentlyContinue)) { return $true }
    Start-Sleep -Milliseconds 1500
  }
  return $false
}

function Invoke-DumpRun {
  param([string]$Label, [switch]$D3D11)
  if (-not (Wait-FieldClear 90)) { throw "[$Label] another generals process is running" }
  $prefix = Join-Path $OutDir $Label
  $log    = Join-Path $OutDir "$Label.log"
  Get-ChildItem "$prefix*_f*.ppm" -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
  Remove-Item $log -Force -ErrorAction SilentlyContinue

  $env:W3DNEXT_FRAMEDUMP_FRAMES = $Frames
  $env:W3DNEXT_D3D11_LOG        = $log
  if ($D3D11) { $env:W3DNEXT_D3D11_FRAMEDUMP = $prefix; Remove-Item Env:\W3DNEXT_DX8_FRAMEDUMP -ErrorAction SilentlyContinue }
  else        { $env:W3DNEXT_DX8_FRAMEDUMP   = $prefix; Remove-Item Env:\W3DNEXT_D3D11_FRAMEDUMP -ErrorAction SilentlyContinue }

  $gameArgs = @('-win','-noaudio','-ignoreAsserts','-stratagemShot')
  if ($D3D11) { $gameArgs += '-d3d11' }

  $p = Start-Process -FilePath $stagedExe -ArgumentList $gameArgs -WorkingDirectory $GameDir -PassThru
  $target = "${prefix}_f$fB.ppm"
  $deadline = (Get-Date).AddSeconds($Timeout); $stable = 0; $lastLen = -1
  while ((Get-Date) -lt $deadline) {
    if ($p.HasExited) { break }
    if (Test-Path $target) {
      $len = (Get-Item $target).Length
      if ($len -gt 0 -and $len -eq $lastLen) { if (++$stable -ge 3) { break } } else { $stable = 0 }
      $lastLen = $len
    }
    Start-Sleep -Milliseconds 1200
  }
  $alive = -not $p.HasExited
  if ($alive) { try { $p.Kill(); $p.WaitForExit(5000) } catch {} }
  Start-Sleep -Milliseconds 400
  foreach ($e in 'W3DNEXT_FRAMEDUMP_FRAMES','W3DNEXT_D3D11_LOG','W3DNEXT_D3D11_FRAMEDUMP','W3DNEXT_DX8_FRAMEDUMP') {
    Remove-Item "Env:\$e" -ErrorAction SilentlyContinue
  }
  $dumps = @(Get-ChildItem "$prefix*_f*.ppm" -ErrorAction SilentlyContinue)
  Write-Host ("  [{0}] dumps={1} alive-at-kill={2}" -f $Label, $dumps.Count, $alive)
  if ($dumps.Count -lt $ladder.Count) { Write-Host "  [$Label] WARN: expected $($ladder.Count) dumps" -ForegroundColor Yellow }
}

if (-not $SkipDx8) { Invoke-DumpRun -Label 'dx8_truth' }
Invoke-DumpRun -Label "d3d11_$Tag" -D3D11

Write-Host "out: $OutDir"
