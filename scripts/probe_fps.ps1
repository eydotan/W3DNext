<#
.SYNOPSIS
  Measure D3D11 fps and its NOISE FLOOR against DX8, mips on vs off.

.DESCRIPTION
  Answers "did the mip upload cost frames?" - which a single run cannot, because
  this metric is known to vary run to run and machine to machine (11-14 on one
  machine, 16/19 on another, for one build). So: N repeats per config, report
  min/median/max, and only call a difference real if the two spreads do not
  overlap.

  fps comes from the `t_ms=` stamp on the framedump log lines (added
  2026-07-26), NOT from the on-screen overlay: fps = (fB - fA) / (tB - tA)/1000
  over the window between two dumped rungs. Reading the overlay would need OCR of
  the dump, which is how an unverifiable fps claim got made in the first place.

  Configs (one binary, env-toggled - the two-build A/B is untrustworthy here for
  the same reason the texcache A/B was, see the parity log):
    d3d11_mips_on   W3DNEXT_D3D11_MIPS unset (default)
    d3d11_mips_off  W3DNEXT_D3D11_MIPS=0     (level 0 only = pre-fix behaviour)
    dx8             reference; should land on its ~30 cap

  FALSIFICATION built in:
    - Each D3D11 run must LOG the mips state it actually ran with, and the
      script fails if a config's runs did not log the state it asked for. A
      toggle that silently no-ops would otherwise produce two identical
      distributions and read as "no regression".
    - DX8 must read ~30 (its frame cap). If the parser returned ~30 for
      everything, or nonsense for DX8, the measurement is broken, not the fix.

.PARAMETER Repeats  Runs per config (default 3).
.PARAMETER Frames   Two rungs; fps is measured over the window between them.
#>
param(
  [string]$GameDir = 'C:\Program Files (x86)\Steam\steamapps\common\Command & Conquer Generals - Zero Hour',
  [string]$BuildDir = 'build\win32-vcpkg-debug',  # non-vcpkg boxes pass build\win32-debug
  # Debug|Release: which build config's generalszh.exe to measure. Release
  # builds land in the multi-config tree (e.g. -BuildDir build\win32
  # -Config Release). Spooky's step-8 probe had to hand-stage a Release exe
  # because this was hardcoded to Debug.
  [ValidateSet('Debug','Release')][string]$Config = 'Debug',
  [int]$Repeats    = 3,
  [string]$Frames  = '300,900',
  [int]$Timeout    = 420
)
$ErrorActionPreference = 'Stop'

$repo      = Split-Path $PSScriptRoot -Parent
$buildDir  = Join-Path $repo $BuildDir
$gameExe   = Join-Path $buildDir "GeneralsMD\$Config\generalszh.exe"
$stagedExe = Join-Path $GameDir 'generalszh_fpsprobe.exe'

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$out   = Join-Path $buildDir "fps_probe_$stamp"
New-Item -ItemType Directory -Force -Path $out | Out-Null

$ladder = [int[]]($Frames -split ',' | ForEach-Object { $_.Trim() })
if ($ladder.Count -lt 2) { throw 'need two rungs to measure an fps window' }
$fA, $fB = $ladder[0], $ladder[-1]

Write-Host "=== fps + noise floor  ($stamp) ===" -ForegroundColor Cyan
Write-Host "  window : f$fA -> f$fB   repeats: $Repeats per config"
Write-Host "  results: $out`n"
if (-not (Test-Path $gameExe)) { throw "missing $gameExe" }

function Wait-FieldClear([int]$sec = 90) {
  $deadline = (Get-Date).AddSeconds($sec)
  while ((Get-Date) -lt $deadline) {
    if (-not (Get-Process -Name 'generalszh','generalszh_texverify','generalszh_driftprobe','generalszh_fpsprobe','generals' -ErrorAction SilentlyContinue)) { return $true }
    Start-Sleep -Milliseconds 1500
  }
  return $false
}

# One run -> @{ Fps; MipsLogged; Alive }.  $Mips: 'on'|'off'|$null (dx8)
function Invoke-FpsRun {
  param([string]$Label, [switch]$D3D11, [string]$Mips)
  if (-not (Wait-FieldClear 90)) {
    Write-Host "  [$Label] another generals process is running; skipped" -ForegroundColor Red
    return $null
  }
  $prefix = Join-Path $out $Label
  $log    = Join-Path $out "$Label.log"
  Get-ChildItem "$prefix*_f*.ppm" -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
  Remove-Item $log -Force -ErrorAction SilentlyContinue

  $env:W3DNEXT_FRAMEDUMP_FRAMES = $Frames
  $env:W3DNEXT_D3D11_LOG        = $log
  if ($D3D11) { $env:W3DNEXT_D3D11_FRAMEDUMP = $prefix; Remove-Item Env:\W3DNEXT_DX8_FRAMEDUMP -ErrorAction SilentlyContinue }
  else        { $env:W3DNEXT_DX8_FRAMEDUMP   = $prefix; Remove-Item Env:\W3DNEXT_D3D11_FRAMEDUMP -ErrorAction SilentlyContinue }
  if ($Mips -eq 'off') { $env:W3DNEXT_D3D11_MIPS = '0' } else { Remove-Item Env:\W3DNEXT_D3D11_MIPS -ErrorAction SilentlyContinue }

  $gameArgs = @('-win','-noaudio','-ignoreAsserts','-navalSandbox')
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
  foreach ($e in 'W3DNEXT_FRAMEDUMP_FRAMES','W3DNEXT_D3D11_LOG','W3DNEXT_D3D11_FRAMEDUMP','W3DNEXT_DX8_FRAMEDUMP','W3DNEXT_D3D11_MIPS') {
    Remove-Item "Env:\$e" -ErrorAction SilentlyContinue
  }

  # Parse t_ms for the two rungs out of the dump lines.
  $fps = $null; $mipsLogged = $null
  if (Test-Path $log) {
    $tA = $null; $tB2 = $null
    foreach ($line in Get-Content $log) {
      if ($line -match 'framedump\] f(\d+) .*t_ms=(\d+)') {
        $fr = [int]$Matches[1]; $t = [double]$Matches[2]
        if ($fr -eq $fA) { $tA = $t }
        if ($fr -eq $fB) { $tB2 = $t }
      }
      if ($line -match 'mips=(on|off)') { $mipsLogged = $Matches[1] }
    }
    if ($null -ne $tA -and $null -ne $tB2 -and $tB2 -gt $tA) {
      $fps = ($fB - $fA) / (($tB2 - $tA) / 1000.0)
    }
  }
  $fpsTxt = if ($null -ne $fps) { '{0:N2}' -f $fps } else { 'n/a' }
  Write-Host ("  [{0,-16}] fps={1,6}  mips_logged={2,-4} alive={3}" -f $Label, $fpsTxt, ($mipsLogged ?? '-'), $alive)
  return @{ Fps = $fps; MipsLogged = $mipsLogged; Alive = $alive }
}

& (Join-Path $PSScriptRoot 'deploy_mod_content.ps1') -Target $GameDir | Out-Null
Copy-Item $gameExe $stagedExe -Force

$configs = @(
  @{ Name = 'd3d11_mips_on';  D3D11 = $true;  Mips = 'on'  },
  @{ Name = 'd3d11_mips_off'; D3D11 = $true;  Mips = 'off' },
  @{ Name = 'dx8';            D3D11 = $false; Mips = $null }
)

$data = [ordered]@{}
foreach ($c in $configs) {
  Write-Host "`n--- $($c.Name) ---" -ForegroundColor Cyan
  $rows = @()
  for ($i = 1; $i -le $Repeats; $i++) {
    $r = if ($c.D3D11) { Invoke-FpsRun -Label "$($c.Name)_r$i" -D3D11 -Mips $c.Mips }
         else          { Invoke-FpsRun -Label "$($c.Name)_r$i" }
    if ($null -ne $r) { $rows += $r }
  }
  $data[$c.Name] = @{ Rows = $rows; Want = $c.Mips }
}

function Stats($vals) {
  $v = @($vals | Where-Object { $null -ne $_ } | Sort-Object)
  if ($v.Count -eq 0) { return $null }
  $mid = if ($v.Count % 2) { $v[[int]($v.Count/2)] } else { ($v[$v.Count/2 - 1] + $v[$v.Count/2]) / 2 }
  return @{ N = $v.Count; Min = $v[0]; Max = $v[-1]; Med = $mid }
}

Write-Host "`n=================== VERDICT ===================" -ForegroundColor Cyan
$summary = @(); $st = @{}; $bad = @()
foreach ($k in $data.Keys) {
  $s = Stats ($data[$k].Rows | ForEach-Object { $_.Fps })
  $st[$k] = $s
  if ($null -eq $s) { $bad += "$k produced no fps samples"; continue }
  $line = "{0,-15} n={1}  min={2,6:N2}  median={3,6:N2}  max={4,6:N2}  spread={5,5:N2}" -f $k, $s.N, $s.Min, $s.Med, $s.Max, ($s.Max - $s.Min)
  Write-Host "  $line"; $summary += $line
  # Toggle straddle: the runs must have LOGGED the state the config asked for.
  $want = $data[$k].Want
  if ($want) {
    $got = @($data[$k].Rows | ForEach-Object { $_.MipsLogged } | Sort-Object -Unique)
    if ($got -notcontains $want -or $got.Count -ne 1) {
      $bad += "$k asked for mips=$want but logged [$($got -join ',')] - the toggle did not engage, so this config is not what it claims"
    }
  }
}
# Parser sanity: DX8 must land near its 30 cap.
if ($st['dx8'] -and ($st['dx8'].Med -lt 20 -or $st['dx8'].Med -gt 40)) {
  $bad += ("dx8 median {0:N2} is not near its ~30 cap - the fps PARSER is suspect, not the backend" -f $st['dx8'].Med)
}

$on = $st['d3d11_mips_on']; $off = $st['d3d11_mips_off']
if ($on -and $off) {
  $overlap = ($on.Min -le $off.Max) -and ($off.Min -le $on.Max)
  $line = if ($overlap) {
    "READING: mips on [{0:N2}-{1:N2}] and off [{2:N2}-{3:N2}] OVERLAP - no fps difference is demonstrated; the medians differ by {4:N2} but that is inside the noise floor." -f $on.Min,$on.Max,$off.Min,$off.Max,[Math]::Abs($on.Med-$off.Med)
  } else {
    "READING: mips on [{0:N2}-{1:N2}] and off [{2:N2}-{3:N2}] are DISJOINT - a real difference of {4:N2} fps ({5})." -f $on.Min,$on.Max,$off.Min,$off.Max,[Math]::Abs($on.Med-$off.Med), $(if ($on.Med -lt $off.Med) { 'mips COST frames' } else { 'mips GAINED frames' })
  }
  Write-Host "  $line" -ForegroundColor Yellow; $summary += $line
}
if ($bad.Count) {
  Write-Host "`n  INVALID:" -ForegroundColor Red
  foreach ($b in $bad) { Write-Host "    - $b" -ForegroundColor Red; $summary += "INVALID: $b" }
}
$summary | Set-Content (Join-Path $out 'verdict.txt')
Write-Host "`n  artifacts: $out" -ForegroundColor Cyan
if ($bad.Count) { exit 1 } else { exit 0 }
