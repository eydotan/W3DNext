<#
.SYNOPSIS
  Framedump A/B for the shoreline wave tracks (backlog R2a).

.DESCRIPTION
  W3DWaterTracks raw-locks its D3D8 vertex buffer AFTER the buffer was bound, so
  the D3D11 bind-time upload reads stale bytes. Scene: the main-menu shell map
  (ShellMapMD.wak, 95 tracks; scripted, so frames line up across backends), or
  -navalSandbox <Map> for a map with a .wak file. Dumps a short frame ladder for
  frame_oracle.py (mae vs dx8 in the wave region, and motion).

  Runs against an exe already staged in the game dir (-Exe). The caller stages
  the pre-fix and post-fix builds under distinct names; the pre-fix D3D11 run is
  the negative control.
#>
param(
  [string]$GameDir = 'C:\Program Files (x86)\Steam\steamapps\common\Command & Conquer Generals - Zero Hour',
  [Parameter(Mandatory)][string]$Exe,      # file name inside GameDir
  [Parameter(Mandatory)][string]$Label,    # e.g. dx8_truth / d3d11_pre / d3d11_post
  [switch]$D3D11,
  [string]$Map     = '',                 # empty = main-menu shell map (ShellMapMD.wak: 95 tracks)
  [string]$Frames  = '600,620,640',      # both backends honor FRAMES; only D3D11 honors EVERY
  [int]$Timeout    = 300,
  [Parameter(Mandatory)][string]$OutDir
)
$ErrorActionPreference = 'Stop'

$stagedExe = Join-Path $GameDir $Exe
if (-not (Test-Path -LiteralPath $stagedExe)) { throw "missing staged exe $stagedExe" }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$deadline = (Get-Date).AddSeconds(90)
while (Get-Process -Name 'generalszh*','generals' -ErrorAction SilentlyContinue) {
  if ((Get-Date) -gt $deadline) { throw "[$Label] another generals process is running" }
  Start-Sleep -Milliseconds 1500
}

$prefix = Join-Path $OutDir $Label
$log    = Join-Path $OutDir "$Label.log"
Get-ChildItem "$prefix*_f*.ppm" -ErrorAction SilentlyContinue | Remove-Item -Force

$env:W3DNEXT_FRAMEDUMP_FRAMES = $Frames
$To = ($Frames -split ',')[-1].Trim()
$env:W3DNEXT_D3D11_LOG       = $log
if ($D3D11) { $env:W3DNEXT_D3D11_FRAMEDUMP = $prefix } else { $env:W3DNEXT_DX8_FRAMEDUMP = $prefix }

# parseNavalSandbox re-joins space-split map tokens, so the path goes in unquoted.
$argLine = '-win -noaudio -ignoreAsserts'
if ($Map) { $argLine += " -navalSandbox $Map" } else { $argLine += ' -nologo' }  # skip intro movies, keep the shell map
if ($D3D11) { $argLine += ' -d3d11' }

$p = Start-Process -FilePath $stagedExe -ArgumentList $argLine -WorkingDirectory $GameDir -PassThru
$target = "${prefix}_f$To.ppm"
$end = (Get-Date).AddSeconds($Timeout); $stable = 0; $lastLen = -1
while ((Get-Date) -lt $end) {
  if ($p.HasExited) { break }
  if (Test-Path -LiteralPath $target) {
    $len = (Get-Item -LiteralPath $target).Length
    if ($len -gt 0 -and $len -eq $lastLen) { if (++$stable -ge 3) { break } } else { $stable = 0 }
    $lastLen = $len
  }
  Start-Sleep -Milliseconds 1200
}
$alive = -not $p.HasExited
if ($alive) { try { $p.Kill(); $p.WaitForExit(5000) } catch {} }
foreach ($e in 'W3DNEXT_FRAMEDUMP_FRAMES','W3DNEXT_D3D11_LOG','W3DNEXT_D3D11_FRAMEDUMP','W3DNEXT_DX8_FRAMEDUMP') {
  Remove-Item "Env:\$e" -ErrorAction SilentlyContinue
}
$dumps = @(Get-ChildItem "$prefix*_f*.ppm" -ErrorAction SilentlyContinue)
Write-Host ("[{0}] dumps={1} alive-at-kill={2}" -f $Label, $dumps.Count, $alive)
if (-not (Test-Path -LiteralPath $target)) { Write-Host "[$Label] FAIL: no dump at f$To"; exit 3 }
exit 0
