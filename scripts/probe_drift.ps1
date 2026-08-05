<#
.SYNOPSIS
  Frame-ladder probe for the D3D11 progressive-drift defect.

.DESCRIPTION
  docs/architecture/d3d11-parity-log.md records that D3D11 moves 51% of pixels
  across 600 frames of a SINGLE run while DX8 moves 2.3%, and that adjacent
  D3D11 frames are effectively identical (186px / 3.7M). So the divergence is
  progressive drift, not per-frame noise. This probe asks the one question that
  splits the remaining hypotheses:

    monotonic  -> state ACCUMULATES (leaked/never-reset render state, or a
                  texture-LOD / mip-streaming path only D3D11 advances)
    plateau    -> the image CONVERGES to a different-but-stable steady state
                  (a settling artifact: e.g. a one-time LOD or fog ramp)

  Method: one run per backend, dumping a ladder of flip frames, then diffing
  every later rung against the FIRST rung of the same run. Same run = same
  process, same scene seed, so nothing cross-run contaminates the series (the
  parity log records that two independent runs differ by mae ~19, which is why
  cross-run comparison was abandoned for the texcache oracle).

  CONTROL (this is why the DX8 run exists): the scene is a live game, so the
  image legitimately changes over 1200 frames for ANY backend. A rising D3D11
  curve alone would be unreadable. DX8 renders the same scene over the same
  ladder; its curve is the floor that scene motion alone produces. The finding
  is the SHAPE and MAGNITUDE of D3D11 relative to that floor, never D3D11 alone.

  Must run in the INTERACTIVE, GPU-backed console session - an SSH/service
  session cannot create a D3D11 device (DXGI_ERROR_NOT_CURRENTLY_AVAILABLE).
  Never kills a pre-existing generals* process; it waits for the field to clear
  and only kills the instance it started.

.PARAMETER GameDir  Steam Zero Hour install (has the *ZH.big assets + debug DLLs).
.PARAMETER Frames   Ladder of flip-frame indices. Rung 1 is the reference.
.PARAMETER Timeout  Per-run seconds. D3D11 is the slow one (~11-14 fps).
.PARAMETER Backends Which runs to do: d3d11, dx8, or both (default).

.EXAMPLE
  pwsh scripts/probe_drift.ps1
  pwsh scripts/probe_drift.ps1 -Frames 300,600,900,1200,1500,1800
#>
param(
  [string]$GameDir = 'C:\Program Files (x86)\Steam\steamapps\common\Command & Conquer Generals - Zero Hour',
  [string]$Frames  = '300,600,900,1200,1500',
  [int]$Timeout    = 420,
  [ValidateSet('both','d3d11','dx8')][string]$Backends = 'both'
)
$ErrorActionPreference = 'Stop'

$repo      = Split-Path $PSScriptRoot -Parent
$buildDir  = Join-Path $repo 'build\win32-vcpkg-debug'
$gameExe   = Join-Path $buildDir 'GeneralsMD\Debug\generalszh.exe'
$diffExe   = Join-Path $buildDir 'Core\Debug\w3d_parity_diff.exe'
# Renamed copy: a sibling session's cleanup kills by image name 'generalszh'.
$stagedExe = Join-Path $GameDir 'generalszh_driftprobe.exe'

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$out   = Join-Path $buildDir "drift_probe_$stamp"
New-Item -ItemType Directory -Force -Path $out | Out-Null

$ladder = [int[]]($Frames -split ',' | ForEach-Object { $_.Trim() })
if ($ladder.Count -lt 3) { throw "need at least 3 rungs to read a shape; got $($ladder.Count)" }
$refFrame  = $ladder[0]
$lastFrame = ($ladder | Measure-Object -Maximum).Maximum

Write-Host "=== d3d11 drift ladder probe  ($stamp) ===" -ForegroundColor Cyan
Write-Host "  ladder : $($ladder -join ', ')   (reference rung = f$refFrame)"
Write-Host "  results: $out`n"

foreach ($p in @($gameExe, $diffExe)) {
  if (-not (Test-Path $p)) { throw "missing build artifact: $p (build it first)" }
}

# ---- helpers (mirrored from verify_texcache.ps1; kept local so this probe
# ---- stands alone and cannot be broken by edits to the texcache gate) ----

function Wait-FieldClear([int]$sec = 90) {
  $deadline = (Get-Date).AddSeconds($sec)
  while ((Get-Date) -lt $deadline) {
    if (-not (Get-Process -Name 'generalszh','generalszh_texverify','generalszh_driftprobe','generals' -ErrorAction SilentlyContinue)) { return $true }
    Start-Sleep -Milliseconds 1500
  }
  return -not (Get-Process -Name 'generalszh','generalszh_texverify','generalszh_driftprobe','generals' -ErrorAction SilentlyContinue)
}

# P6 header -> @(width,height). Reads only the small ASCII header.
function Get-PpmSize([string]$path) {
  $fs = [System.IO.File]::OpenRead($path)
  try {
    $buf = New-Object byte[] 64; $n = $fs.Read($buf, 0, 64)
    $txt = [System.Text.Encoding]::ASCII.GetString($buf, 0, $n)
    if ($txt -match 'P6\s+(\d+)\s+(\d+)') { return @([int]$Matches[1], [int]$Matches[2]) }
  } finally { $fs.Dispose() }
  return $null
}

function Invoke-GameRun {
  param([string]$Label, [switch]$D3D11)
  Write-Host "[run] $Label  (frames $Frames) ..." -ForegroundColor Yellow
  if (-not (Wait-FieldClear 90)) {
    Write-Host "  another generals process is running and did not exit; skipping $Label" -ForegroundColor Red
    return @{ Skipped = $true }
  }
  $prefix = Join-Path $out $Label
  $log    = Join-Path $out "$Label.log"
  Get-ChildItem "$prefix*_f*.ppm" -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
  Remove-Item $log -Force -ErrorAction SilentlyContinue

  $env:W3DNEXT_FRAMEDUMP_FRAMES = $Frames
  $env:W3DNEXT_D3D11_LOG        = $log
  if ($D3D11) { $env:W3DNEXT_D3D11_FRAMEDUMP = $prefix; Remove-Item Env:\W3DNEXT_DX8_FRAMEDUMP   -ErrorAction SilentlyContinue }
  else        { $env:W3DNEXT_DX8_FRAMEDUMP   = $prefix; Remove-Item Env:\W3DNEXT_D3D11_FRAMEDUMP -ErrorAction SilentlyContinue }
  # Cache left at its default (ON). The parity log records the drift reproduces
  # cache-on and cache-off alike, so the cache is not a variable here.

  $gameArgs = @('-win','-noaudio','-ignoreAsserts','-navalSandbox')
  if ($D3D11) { $gameArgs += '-d3d11' }

  $p = Start-Process -FilePath $stagedExe -ArgumentList $gameArgs -WorkingDirectory $GameDir -PassThru
  $target = "${prefix}_f$lastFrame.ppm"
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
  Start-Sleep -Milliseconds 500

  foreach ($e in 'W3DNEXT_FRAMEDUMP_FRAMES','W3DNEXT_D3D11_LOG','W3DNEXT_D3D11_FRAMEDUMP','W3DNEXT_DX8_FRAMEDUMP') {
    Remove-Item "Env:\$e" -ErrorAction SilentlyContinue
  }
  $got = @($ladder | Where-Object { Test-Path "${prefix}_f$_.ppm" })
  Write-Host ("  alive-at-kill={0}  rungs dumped={1}/{2}" -f $alive, $got.Count, $ladder.Count)
  return @{ Skipped = $false; Alive = $alive; Prefix = $prefix; Log = $log; Got = $got }
}

# Diff two dumps, masking the fps/clock overlay (top-left) - it differs by
# construction between backends and ticks within a run, so it would forge drift.
function Invoke-Diff([string]$a, [string]$b, [string]$diffOut) {
  if (-not ((Test-Path $a) -and (Test-Path $b))) { return $null }
  $sz = Get-PpmSize $a
  $mask = @()
  if ($sz) { $mask += '--mask'; $mask += ("0,0,{0},{1}" -f [int]($sz[0]*0.42), [int]($sz[1]*0.09)) }
  # Select the PARITY summary explicitly: an "exceeding pixels" detail line
  # follows it, and a -Last 1 here silently grabs the wrong line.
  $o = & $diffExe $a $b '--tol' 2 '--diff-out' $diffOut @mask 2>&1
  $line = ($o | Select-String -Pattern '^PARITY' | Select-Object -First 1).Line
  if (-not $line) { $line = ($o | Select-Object -Last 1) }
  if ($line -match 'PARITY (?:PASS|FAIL) maxdelta=(\d+) mae=([\d.]+) over=(\d+)/(\d+)') {
    $over = [int]$Matches[3]; $total = [int]$Matches[4]
    return @{ Mae = [double]$Matches[2]; Over = $over; Total = $total
              Frac = $(if ($total) { $over / $total } else { 0 }); Line = $line }
  }
  return $null
}

# ---- stage the exe ----
& (Join-Path $PSScriptRoot 'deploy_mod_content.ps1') -Target $GameDir | Out-Null
Copy-Item $gameExe $stagedExe -Force

# ---- runs ----
$runs = [ordered]@{}
if ($Backends -in @('both','d3d11')) { $runs['d3d11'] = Invoke-GameRun -Label 'd3d11' -D3D11 }
if ($Backends -in @('both','dx8'))   { $runs['dx8']   = Invoke-GameRun -Label 'dx8' }

# ---- ladder diffs -------------------------------------------------------
# Three families, because on 2026-07-25 the first two each turned out to be
# unreadable on their own and only the third explained the other two:
#
#   A. vs the FIRST rung   - dominated by a startup transient (fade-in / camera
#      settle) that saturates at ~54% of pixels in BOTH backends by f600. On its
#      own this family says nothing; kept only to expose the transient.
#   B. CONSECUTIVE rungs   - the actual step-to-step motion, and the series that
#      tests accumulation. Also computed against the SECOND rung (post-transient)
#      so "distance to reference" can be checked for growth over the full run.
#   C. CROSS-BACKEND at the SAME frame index - the static parity gap. If this is
#      flat and the same magnitude as A and B, then A and B carry no temporal
#      information at all: they are re-measuring the static gap. That is exactly
#      what happened.
$series = [ordered]@{}   # backend -> @{ First=<pts>; Consec=<pts>; VsPost=<pts> }
foreach ($k in $runs.Keys) {
  $r = $runs[$k]
  if ($r.Skipped) { continue }
  $postRef = $ladder[1]   # first rung after the startup transient
  Write-Host "`n--- ladder: $k ---" -ForegroundColor Cyan

  $first = @(); $consec = @(); $vsPost = @()
  Write-Host "  [A] vs f$refFrame (expect a startup transient here - not a finding)"
  foreach ($f in $ladder | Select-Object -Skip 1) {
    $d = Invoke-Diff "$($r.Prefix)_f$refFrame.ppm" "$($r.Prefix)_f$f.ppm" (Join-Path $out "diff_${k}_f${refFrame}_f$f.ppm")
    if ($d) { $first += @{ Frame=$f; Mae=$d.Mae; Frac=$d.Frac }
              Write-Host ("      f{0,-5} -> f{1,-5}  mae={2,8:N4}  ({3,6:P2})" -f $refFrame,$f,$d.Mae,$d.Frac) }
  }
  Write-Host "  [B] consecutive rungs (step-to-step motion)"
  for ($i = 1; $i -lt $ladder.Count; $i++) {
    $a = $ladder[$i-1]; $b = $ladder[$i]
    $d = Invoke-Diff "$($r.Prefix)_f$a.ppm" "$($r.Prefix)_f$b.ppm" (Join-Path $out "diff_${k}_step_f${a}_f$b.ppm")
    if ($d) { $consec += @{ Frame=$b; Mae=$d.Mae; Frac=$d.Frac }
              Write-Host ("      f{0,-5} -> f{1,-5}  mae={2,8:N4}  ({3,6:P2})" -f $a,$b,$d.Mae,$d.Frac) }
  }
  Write-Host "  [B'] vs f$postRef (post-transient reference - does distance GROW?)"
  foreach ($f in $ladder | Select-Object -Skip 2) {
    $d = Invoke-Diff "$($r.Prefix)_f$postRef.ppm" "$($r.Prefix)_f$f.ppm" (Join-Path $out "diff_${k}_f${postRef}_f$f.ppm")
    if ($d) { $vsPost += @{ Frame=$f; Mae=$d.Mae; Frac=$d.Frac }
              Write-Host ("      f{0,-5} -> f{1,-5}  mae={2,8:N4}  ({3,6:P2})" -f $postRef,$f,$d.Mae,$d.Frac) }
  }
  $series[$k] = @{ First = $first; Consec = $consec; VsPost = $vsPost }
}

# [C] cross-backend, same frame index. Needs both runs.
$cross = @()
if ($series.Contains('d3d11') -and $series.Contains('dx8')) {
  Write-Host "`n--- [C] DX8 vs D3D11 at the SAME frame index (static parity gap) ---" -ForegroundColor Cyan
  foreach ($f in $ladder) {
    $d = Invoke-Diff (Join-Path $out "dx8_f$f.ppm") (Join-Path $out "d3d11_f$f.ppm") (Join-Path $out "diff_cross_f$f.ppm")
    if ($d) { $cross += @{ Frame=$f; Mae=$d.Mae; Frac=$d.Frac }
              Write-Host ("      f{0,-5}  mae={1,8:N4}  ({2,6:P2})" -f $f,$d.Mae,$d.Frac) }
  }
}

# ---- verdict ------------------------------------------------------------
# ACCUMULATING requires distance-to-a-fixed-post-transient-reference to GROW
# monotonically. A bounded, non-monotonic series is NOT drift however large it
# is - that was the 2026-07-25 mistake, made on a single sample pair.
function Get-Shape($pts, [string]$what) {
  if ($pts.Count -lt 3) { return "INDETERMINATE (need >=3 points, have $($pts.Count))" }
  $mae = @($pts | ForEach-Object { $_.Mae })
  $steps = @(); for ($i = 1; $i -lt $mae.Count; $i++) { $steps += ($mae[$i] - $mae[$i-1]) }
  $allUp  = -not ($steps | Where-Object { $_ -le 0 })
  $spread = ($mae | Measure-Object -Maximum).Maximum - ($mae | Measure-Object -Minimum).Minimum
  $growth = $mae[-1] - $mae[0]
  if ($allUp -and $growth -gt 0.25 * $mae[0]) {
    return ("ACCUMULATING ({0} grows {1:N2} -> {2:N2}, every step up)" -f $what, $mae[0], $mae[-1])
  }
  return ("BOUNDED ({0} stays {1:N2}-{2:N2}, spread {3:N2}, net {4:+0.00;-0.00} - no accumulation)" -f
          $what, ($mae | Measure-Object -Minimum).Minimum, ($mae | Measure-Object -Maximum).Maximum, $spread, $growth)
}

Write-Host "`n=================== VERDICT ===================" -ForegroundColor Cyan
$summary = @()
foreach ($k in $series.Keys) {
  $s = $series[$k]
  $line = "{0,-6} consecutive: {1}" -f $k, (($s.Consec | ForEach-Object { '{0:N2}' -f $_.Mae }) -join ' -> ')
  Write-Host "  $line"; $summary += $line
  $line = "{0,-6} {1}" -f $k, (Get-Shape $s.VsPost 'distance to post-transient ref')
  Write-Host "  $line"; $summary += $line
}
if ($cross.Count) {
  $cm  = @($cross | ForEach-Object { $_.Mae })
  $lo  = ($cm | Measure-Object -Minimum).Minimum; $hi = ($cm | Measure-Object -Maximum).Maximum
  $line = "CROSS  static parity gap: {0} (flat {1:N2}-{2:N2} across all rungs)" -f (($cm | ForEach-Object { '{0:N2}' -f $_ }) -join ' / '), $lo, $hi
  Write-Host "  $line" -ForegroundColor Yellow; $summary += $line
  # The interpretation trap: if the per-backend temporal numbers sit inside the
  # cross-backend band, they are the static gap re-measured and carry no
  # temporal signal. Say so, rather than reporting a large number as a finding.
  $tem = @(); foreach ($k in $series.Keys) { $tem += @($series[$k].Consec | ForEach-Object { $_.Mae }) }
  if ($tem.Count) {
    $tmax = ($tem | Measure-Object -Maximum).Maximum
    if ($tmax -le $hi * 1.25) {
      $line = "READING: temporal spread (max {0:N2}) does not exceed the static gap ({1:N2}) - the temporal numbers carry NO signal beyond the static parity defect. Chase the static gap." -f $tmax, $hi
      Write-Host "  $line" -ForegroundColor Yellow; $summary += $line
    }
  }
} else {
  Write-Host "  CROSS: MISSING - without the same-frame cross-backend gap, the temporal numbers are uninterpretable." -ForegroundColor Red
  $summary += 'CROSS: MISSING - temporal numbers uninterpretable.'
}

$summary | Set-Content (Join-Path $out 'verdict.txt')
Write-Host "`n  artifacts: $out" -ForegroundColor Cyan
