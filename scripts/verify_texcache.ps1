<#
.SYNOPSIS
  One-shot GPU verification for the D3D11 uploaded-texture cache (checkpoint 147f5cc).

.DESCRIPTION
  Must run in an INTERACTIVE, GPU-backed session (the console session that owns the
  RTX). The SSH/service session cannot create a D3D11 device (DXGI_ERROR_NOT_
  CURRENTLY_AVAILABLE), which is why this was split out as a script.

  It verifies the texture cache flagged in docs/architecture/d3d11-parity-log.md as
  "the single highest-value perf item on the backend": Set_Texture used to re-upload
  every texture on every bind (~170 uploads/frame -> 12 fps vs DX8's 30 cap). The v1
  cache keyed on the raw IDirect3DTexture8* and aliased freed/realloc'd textures ->
  striped-garbage world, so it shipped gated OFF. The checkpoint re-keys on the
  never-reused TextureBaseClass id, versioned by a D3D-generation bumped on every
  surface mutation, evicted on destroy, and turns the cache ON by default.

  Five oracles (see the table printed at the end):
    1. SMOKE   - rebuild + run w3d_d3d11_smoke; asserts the identity contract (check R).
    2. PERF    - parse [D3D11 texcache] counters from the cache-ON run: uploads must
                 collapse (~101k/600f -> hundreds) with evictions > 0 (churn witness).
    3. CORRECT - pixel-diff D3D11 cache-ON vs cache-OFF at a matched frame. A sound
                 cache only elides redundant uploads, so the frames must be ~identical;
                 the v1 corruption would blow this up.
    4. SANITY  - DX8-vs-(D3D11 cache-ON) parity diff: confirms a real world renders
                 (normal MAE / near-black), not garbage.
    5. CRASH   - Crash*.dmp count + ReleaseCrashInfo.txt mtime unchanged, alive-at-kill.

  Never kills a pre-existing generals* process (your own game session); it waits for
  the field to clear before each launch and only kills the instance it started.

.PARAMETER GameDir       Steam Zero Hour install (has the *ZH.big assets + debug DLLs).
.PARAMETER SkipGameBuild Skip the (long) z_generals rebuild; use the exe already built.
.PARAMETER Frames        Comma list of flip-frame indices to dump (last one is compared).
.PARAMETER Timeout       Per-run seconds. The cache-OFF run is the slow one (~12 fps).

.EXAMPLE
  pwsh scripts/verify_texcache.ps1
  pwsh scripts/verify_texcache.ps1 -SkipGameBuild -Frames 900,1500
#>
param(
  [string]$GameDir = 'C:\Program Files (x86)\Steam\steamapps\common\Command & Conquer Generals - Zero Hour',
  [switch]$SkipGameBuild,
  [string]$Frames = '900,1500',
  [int]$Timeout = 300
)
$ErrorActionPreference = 'Stop'

$repo     = Split-Path $PSScriptRoot -Parent
$buildDir = Join-Path $repo 'build\win32-vcpkg-debug'
$gameExe  = Join-Path $buildDir 'GeneralsMD\Debug\generalszh.exe'
$smokeExe = Join-Path $buildDir 'Core\Debug\w3d_d3d11_smoke.exe'
$diffExe  = Join-Path $buildDir 'Core\Debug\w3d_parity_diff.exe'
$stagedExe= Join-Path $GameDir 'generalszh_texverify.exe'
$vcvars   = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat'

$stamp   = Get-Date -Format 'yyyyMMdd-HHmmss'
$out     = Join-Path $buildDir "texcache_verify_$stamp"
New-Item -ItemType Directory -Force -Path $out | Out-Null
$lastFrame = ([int[]]($Frames -split ',' | ForEach-Object { $_.Trim() }) | Measure-Object -Maximum).Maximum

Write-Host "=== texcache verify  ($stamp) ===" -ForegroundColor Cyan
Write-Host "  repo   : $repo"
Write-Host "  game   : $GameDir"
Write-Host "  frames : $Frames  (comparing f$lastFrame)"
Write-Host "  results: $out`n"

$results = [ordered]@{}   # oracle -> @{ Pass=<bool>; Detail=<string> }

function Build-Target([string]$target) {
  $bat = Join-Path $env:TEMP "texverify_build_$target.bat"
  @('@echo off', "call `"$vcvars`" x86 >nul || exit /b 1",
    "cd /d `"$repo`"",
    "cmake --build build\win32-vcpkg-debug --config Debug --target $target") |
    Set-Content $bat -Encoding Ascii
  cmd /c "`"$bat`"" 2>&1 | Tee-Object -FilePath (Join-Path $out "build_$target.log") | Out-Null
  return ($LASTEXITCODE -eq 0)
}

# Crash oracle: Crash*.dmp under the game + exe dirs, and the newest ReleaseCrashInfo.txt mtime.
function Get-CrashState {
  $dirs  = @($GameDir, (Split-Path $gameExe -Parent)) | Select-Object -Unique
  $dumps = 0; $rci = $null
  foreach ($d in $dirs) {
    if (Test-Path $d) {
      $dumps += (Get-ChildItem -Path $d -Filter 'Crash*.dmp' -ErrorAction SilentlyContinue).Count
      $r = Get-ChildItem -Path $d -Filter 'ReleaseCrashInfo.txt' -ErrorAction SilentlyContinue | Select-Object -First 1
      if ($r -and (($null -eq $rci) -or ($r.LastWriteTime -gt $rci))) { $rci = $r.LastWriteTime }
    }
  }
  return @{ Dumps = $dumps; Rci = $rci }
}

# Wait for any pre-existing game instance (the user's own session) to clear. Never kills it.
function Wait-FieldClear([int]$sec = 60) {
  $deadline = (Get-Date).AddSeconds($sec)
  while ((Get-Date) -lt $deadline) {
    $others = Get-Process -Name 'generalszh','generalszh_texverify','generals' -ErrorAction SilentlyContinue
    if (-not $others) { return $true }
    Start-Sleep -Milliseconds 1500
  }
  return -not (Get-Process -Name 'generalszh','generalszh_texverify','generals' -ErrorAction SilentlyContinue)
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

# One time-boxed game run. Returns @{ Alive; Dump; Log; Crashed }.
function Invoke-GameRun {
  param([string]$Label, [switch]$D3D11, [switch]$CacheOff, [switch]$Toggle, [string]$RunFrames)
  Write-Host "[run] $Label ..." -ForegroundColor Yellow
  $frames = if ($RunFrames) { $RunFrames } else { $Frames }
  $runLast = ([int[]]($frames -split ',' | ForEach-Object { $_.Trim() }) | Measure-Object -Maximum).Maximum
  if (-not (Wait-FieldClear 90)) {
    Write-Host "  another generals process is running and did not exit; skipping $Label" -ForegroundColor Red
    return @{ Alive = $false; Dump = $null; Log = $null; Crashed = $null; Skipped = $true }
  }
  $prefix = Join-Path $out $Label
  $log    = Join-Path $out "$Label.log"
  Get-ChildItem "$prefix*_f*.ppm" -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
  Remove-Item $log -Force -ErrorAction SilentlyContinue

  $env:ZP_FRAMEDUMP_FRAMES = $frames
  $env:ZP_D3D11_LOG        = $log
  if ($D3D11) { $env:ZP_D3D11_FRAMEDUMP = $prefix; Remove-Item Env:\ZP_DX8_FRAMEDUMP -ErrorAction SilentlyContinue }
  else        { $env:ZP_DX8_FRAMEDUMP   = $prefix; Remove-Item Env:\ZP_D3D11_FRAMEDUMP -ErrorAction SilentlyContinue }
  if ($CacheOff) { $env:ZP_D3D11_TEXCACHE = '0' } else { Remove-Item Env:\ZP_D3D11_TEXCACHE -ErrorAction SilentlyContinue }
  if ($Toggle)   { $env:ZP_D3D11_TEXCACHE_TOGGLE = '1' } else { Remove-Item Env:\ZP_D3D11_TEXCACHE_TOGGLE -ErrorAction SilentlyContinue }

  $gameArgs = @('-win','-noaudio','-ignoreAsserts','-navalSandbox')
  if ($D3D11) { $gameArgs += '-d3d11' }

  $pre = Get-CrashState
  $p = Start-Process -FilePath $stagedExe -ArgumentList $gameArgs -WorkingDirectory $GameDir -PassThru
  $target = "${prefix}_f$runLast.ppm"
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
  $post = Get-CrashState
  $crashed = ($post.Dumps -ne $pre.Dumps) -or ($pre.Rci -ne $post.Rci)

  foreach ($e in 'ZP_FRAMEDUMP_FRAMES','ZP_D3D11_LOG','ZP_D3D11_FRAMEDUMP','ZP_DX8_FRAMEDUMP','ZP_D3D11_TEXCACHE','ZP_D3D11_TEXCACHE_TOGGLE') {
    Remove-Item "Env:\$e" -ErrorAction SilentlyContinue
  }
  $dump = if (Test-Path $target) { $target } else { $null }
  Write-Host ("  alive-at-kill={0}  dump={1}  crashed={2}" -f $alive, [bool]$dump, $crashed)
  return @{ Alive = $alive; Dump = $dump; Log = $log; Crashed = $crashed; Skipped = $false }
}

# ---------------------------------------------------------------- 1. SMOKE
Write-Host "`n--- 1. SMOKE (identity contract / check R) ---" -ForegroundColor Cyan
if (Build-Target 'w3d_d3d11_smoke') {
  $smokeOut = & $smokeExe 2>&1
  $smokeOut | Set-Content (Join-Path $out 'smoke.log')
  $pass = ($smokeOut -match 'SMOKE PASS') -and ($smokeOut -match 'texCacheIdentity.*OK')
  $idLine = ($smokeOut | Select-String 'texCacheIdentity').Line
  $results['SMOKE'] = @{ Pass = [bool]$pass; Detail = if ($idLine) { $idLine.Trim() } else { ($smokeOut | Select-Object -Last 1) } }
} else {
  $results['SMOKE'] = @{ Pass = $false; Detail = 'smoke build FAILED (see build_w3d_d3d11_smoke.log)' }
}

# ---------------------------------------------------------------- game build + stage
Write-Host "`n--- game exe ---" -ForegroundColor Cyan
$gameOk = $true
if (-not $SkipGameBuild) {
  Write-Host "[build] z_generals (debug) - this is the long one..."
  $gameOk = Build-Target 'z_generals'
  if (-not $gameOk) { Write-Host "  z_generals build FAILED (see build_z_generals.log)" -ForegroundColor Red }
}
if ($gameOk -and (Test-Path $gameExe)) {
  & (Join-Path $PSScriptRoot 'deploy_mod_content.ps1') -Target $GameDir | Out-Null
  Copy-Item $gameExe $stagedExe -Force
} else {
  $gameOk = $false
  Write-Host "  game exe unavailable; skipping in-game oracles" -ForegroundColor Red
}

# ---------------------------------------------------------------- in-game runs
$runOn = $runOff = $runDx8 = $runTog = $null
# The toggle run dumps an ADJACENT pair: same process, one frame apart, cache on
# for one and off for the other. See oracle 3 for why the cross-run A/B it
# replaced could not work.
$togFrames = '900,901'
if ($gameOk) {
  Write-Host "`n--- 2-5. in-game runs (navalSandbox) ---" -ForegroundColor Cyan
  $runDx8 = Invoke-GameRun -Label 'dx8'        # reference
  $runOn  = Invoke-GameRun -Label 'd3d11_on'  -D3D11            # cache ON (default)
  $runOff = Invoke-GameRun -Label 'd3d11_off' -D3D11 -CacheOff  # cache OFF (PERF negative control)
  $runTog = Invoke-GameRun -Label 'd3d11_tog' -D3D11 -Toggle -RunFrames $togFrames
}

# ---------------------------------------------------------------- 2. PERF
Write-Host "`n--- 2. PERF (upload collapse) ---" -ForegroundColor Cyan
if ($runOn -and $runOn.Log -and (Test-Path $runOn.Log)) {
  $tc = Select-String -Path $runOn.Log -Pattern '\[D3D11 texcache\]'
  $tc | ForEach-Object { Write-Host "  $($_.Line.Trim())" }
  $last = $tc | Select-Object -Last 1
  if ($last -and $last.Line -match 'hits/600f=(\d+) uploads/600f=(\d+) evictions/600f=(\d+)') {
    $hits = [int]$Matches[1]; $ups = [int]$Matches[2]; $evs = [int]$Matches[3]
    $pass = ($ups -lt 3000) -and ($hits -gt 3000)
    # NEGATIVE CONTROL: the same check run against the cache-OFF log must FAIL.
    # The counters only increment on the cache path, so a disabled cache reports
    # hits=0 and trips the hits>3000 arm - i.e. this check can distinguish a live
    # cache from a dead one rather than passing on any input.
    $ncFails = $null
    if ($runOff -and $runOff.Log -and (Test-Path $runOff.Log)) {
      $ncLine = (Select-String -Path $runOff.Log -Pattern '\[D3D11 texcache\]' | Select-Object -Last 1)
      if ($ncLine -and $ncLine.Line -match 'hits/600f=(\d+) uploads/600f=(\d+)') {
        $ncHits = [int]$Matches[1]; $ncUps = [int]$Matches[2]
        $ncFails = -not (($ncUps -lt 3000) -and ($ncHits -gt 3000))
      }
    }
    if ($ncFails -eq $false) { $pass = $false }   # NC passed => the check proves nothing
    $ncTxt = if ($null -eq $ncFails) { 'NC=absent' } elseif ($ncFails) { 'NC=RED (cache-off correctly fails)' } else { 'NC=LEAKED (cache-off also passed - check is vacuous)' }
    $results['PERF'] = @{ Pass = $pass; Detail = "last window: hits=$hits uploads=$ups evictions=$evs; $ncTxt" }
  } else {
    $results['PERF'] = @{ Pass = $false; Detail = 'no [D3D11 texcache] counter line (run too short to reach frame 600, or no textures bound)' }
  }
} else {
  $results['PERF'] = @{ Pass = $false; Detail = 'cache-ON run produced no log' }
}

# ---------------------------------------------------------------- diff helper
function Invoke-Diff([string]$a, [string]$b, [int]$tol, [string]$diffOut, [string[]]$extraMask) {
  if (-not ($a -and $b -and (Test-Path $a) -and (Test-Path $b))) { return $null }
  # Mask the fps/clock overlay (top-left) - it differs by construction (ON ~30 fps vs OFF ~12).
  $sz = Get-PpmSize $a
  $mask = @()
  if ($sz) { $mask += ('--mask'); $mask += ("0,0,{0},{1}" -f [int]($sz[0]*0.42), [int]($sz[1]*0.09)) }
  foreach ($m in $extraMask) { $mask += '--mask'; $mask += $m }
  # Select the PARITY summary line explicitly. w3d_parity_diff prints an
  # "exceeding pixels (first 16): ..." detail line AFTER the summary, so a
  # Select-Object -Last 1 here silently captured the wrong line and every
  # correctness verdict came back "diff did not parse" (2026-07-25).
  $out = & $diffExe $a $b '--tol' $tol '--diff-out' $diffOut @mask 2>&1
  $line = ($out | Select-String -Pattern '^PARITY' | Select-Object -First 1).Line
  if (-not $line) { $line = ($out | Select-Object -Last 1) }
  return $line
}

# Parse a PARITY line into a verdict against the soundness thresholds.
# Returns @{ Ok; Mae; Frac; Line } or $null when the line does not parse.
function Test-ParityLine([string]$line, [double]$maxMae = 3.0, [double]$maxFrac = 0.05) {
  if ($line -match 'PARITY (?:PASS|FAIL) maxdelta=(\d+) mae=([\d.]+) over=(\d+)/(\d+)') {
    $mae  = [double]$Matches[2]
    $over = [int]$Matches[3]; $total = [int]$Matches[4]
    $frac = if ($total) { $over / $total } else { 1 }
    return @{ Ok = (($mae -lt $maxMae) -and ($frac -lt $maxFrac)); Mae = $mae; Frac = $frac; Line = $line }
  }
  return $null
}

# ---------------------------------------------------------------- 3. CORRECTNESS (ON vs OFF)
Write-Host "`n--- 3. CORRECTNESS (in-process cache toggle, adjacent frames) ---" -ForegroundColor Cyan
# WHY NOT the obvious cross-run A/B (one run cache-ON, one cache-OFF, diff them):
# measured 2026-07-25, two independent runs of this same scene differ by mae 19.4
# over 53.9% of pixels - the SAME magnitude as a DX8-vs-D3D11 diff (mae 18.8 /
# 53.3%). The backend is not frame-stable run to run, so that comparison has no
# signal left to detect a corrupting cache with. (Baseline for contrast: DX8
# moves mae 0.88 over 2.3% of pixels across 600 frames of the same run.)
#
# So the cache is toggled INSIDE one process on the flip-frame parity and two
# ADJACENT frames are dumped. One frame apart the scene is nearly static, so a
# cache that serves stale or aliased bytes has nowhere to hide.
$togA = if ($runTog) { Join-Path $out ('d3d11_tog_f{0}.ppm' -f (($togFrames -split ',')[0].Trim())) } else { $null }
$togB = if ($runTog) { Join-Path $out ('d3d11_tog_f{0}.ppm' -f (($togFrames -split ',')[1].Trim())) } else { $null }
if ($togA -and $togB -and (Test-Path $togA) -and (Test-Path $togB)) {
  # The toggle must actually have engaged: the log records the cache state per
  # dumped frame, and the pair must straddle on/off. Without this the diff would
  # trivially pass by comparing two frames rendered the SAME way.
  $states = @()
  if ($runTog.Log -and (Test-Path $runTog.Log)) {
    $states = Select-String -Path $runTog.Log -Pattern 'framedump\] f\d+ .*texcache=(\w+)' |
              ForEach-Object { $_.Matches[0].Groups[1].Value }
  }
  $straddles = (($states | Sort-Object -Unique).Count -eq 2)
  $line = Invoke-Diff $togA $togB 2 (Join-Path $out 'toggle_pair_delta.png') @()
  Write-Host "  toggle states: $($states -join '/')   straddles on+off: $straddles"
  Write-Host "  $line"
  $v = Test-ParityLine $line
  if ($null -eq $v) {
    $results['CORRECT'] = @{ Pass = $false; Detail = "diff did not parse: $line" }
  } elseif (-not $straddles) {
    $results['CORRECT'] = @{ Pass = $false; Detail = "toggle did not engage (states: $($states -join '/')) - comparison is vacuous" }
  } else {
    # NEGATIVE CONTROL: the same threshold applied to a known-different pair
    # (DX8 vs D3D11 at the same frame) must FAIL, or a passing toggle diff
    # would only mean the threshold accepts anything.
    $ncLine = Invoke-Diff $runDx8.Dump $runOn.Dump 2 (Join-Path $out 'nc_dx8_vs_on.png') @()
    $ncV = Test-ParityLine $ncLine
    $ncRed = ($null -ne $ncV) -and (-not $ncV.Ok)
    $pass = $v.Ok -and $ncRed
    $ncTxt = if ($ncRed) { ('NC=RED (dx8-vs-d3d11 mae={0:N2} correctly fails)' -f $ncV.Mae) } else { 'NC=LEAKED (known-different pair also passed)' }
    $results['CORRECT'] = @{ Pass = $pass
      Detail = ('{0}  states={1}  mae={2:N2} over={3:P1}; {4}' -f $line, ($states -join '/'), $v.Mae, $v.Frac, $ncTxt) }
  }
} else {
  $results['CORRECT'] = @{ Pass = $false; Detail = 'missing toggle-run framedump pair (run/crash?) - see d3d11_tog.log' }
}

# ---------------------------------------------------------------- 4. SANITY (DX8 vs ON)
Write-Host "`n--- 4. SANITY (DX8 reference vs cache-ON) ---" -ForegroundColor Cyan
if ($runDx8 -and $runOn -and $runDx8.Dump -and $runOn.Dump) {
  # navalSandbox is not seed-locked across runs, so this is a magnitude sanity, not a
  # pass/fail parity gate: a correct world lands near the parity log's world numbers;
  # striped garbage would be an order of magnitude worse. Reported, not asserted.
  $line = Invoke-Diff $runDx8.Dump $runOn.Dump 4 (Join-Path $out 'dx8_vs_on_delta.png') @()
  Write-Host "  $line"
  $results['SANITY'] = @{ Pass = $null; Detail = "$line  (magnitude sanity, not a gate - see note)" }
} else {
  $results['SANITY'] = @{ Pass = $null; Detail = 'missing DX8 and/or ON framedump' }
}

# ---------------------------------------------------------------- 5. CRASH oracle
Write-Host "`n--- 5. CRASH / stability ---" -ForegroundColor Cyan
$runs = @{ 'dx8' = $runDx8; 'd3d11_on' = $runOn; 'd3d11_off' = $runOff; 'd3d11_tog' = $runTog }
$crashPass = $true; $crashDetail = @()
foreach ($k in 'dx8','d3d11_on','d3d11_off','d3d11_tog') {
  $r = $runs[$k]
  if ($null -eq $r -or $r.Skipped) { $crashDetail += "$k=skipped"; continue }
  $ok = $r.Alive -and (-not $r.Crashed)
  if (-not $ok) { $crashPass = $false }
  $crashDetail += ("{0}=alive:{1}/clean:{2}" -f $k, $r.Alive, (-not $r.Crashed))
}
$results['CRASH'] = @{ Pass = $crashPass; Detail = ($crashDetail -join '  ') }

# ---------------------------------------------------------------- SUMMARY
Write-Host "`n================= SUMMARY =================" -ForegroundColor Cyan
$gate = @('SMOKE','PERF','CORRECT','CRASH')   # SANITY is informational only
$allPass = $true
foreach ($k in 'SMOKE','PERF','CORRECT','SANITY','CRASH') {
  $r = $results[$k]
  $tag = if ($null -eq $r.Pass) { 'INFO' } elseif ($r.Pass) { 'PASS' } else { 'FAIL' }
  $col = if ($null -eq $r.Pass) { 'Gray' } elseif ($r.Pass) { 'Green' } else { 'Red' }
  if (($gate -contains $k) -and (-not $r.Pass)) { $allPass = $false }
  Write-Host ("  [{0}] {1,-8} {2}" -f $tag, $k, $r.Detail) -ForegroundColor $col
}
Write-Host "===========================================" -ForegroundColor Cyan
Write-Host "  artifacts: $out"
if ($allPass) {
  Write-Host "  VERDICT: PASS - texcache is sound, fast, and non-corrupting." -ForegroundColor Green
  exit 0
} else {
  Write-Host "  VERDICT: FAIL - see the failing oracle above and the per-run logs." -ForegroundColor Red
  exit 1
}
