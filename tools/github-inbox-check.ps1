# github-inbox-check.ps1 — masok gate: is anyone waiting on a reply from us on GitHub?
#
# Definition of "needs a reply": an issue/PR/discussion ANYWHERE that eydotan is
# involved in, updated inside the window, whose NEWEST comment was written by
# someone else. That is the machine-checkable form of "we owe them an answer".
#
# Exit codes (this is the oracle — masok reads the code, not the prose):
#   0 = inbox clear, nothing owed
#   2 = at least one thread is waiting on us  -> BREAK MASOK
#   1 = the check itself failed (auth/network) -> treat as unknown, do NOT report clean
#
# Usage: .\tools\github-inbox-check.ps1 [-Hours 4] [-User eydotan]
param(
    [int]$Hours = 4,
    [string]$User = 'eydotan'
)
$ErrorActionPreference = 'Stop'

function Invoke-Native {
    # gh writes progress/notices to stderr; EAP=Stop would turn exit-0 runs into
    # NativeCommandError mid-pipeline (cookie-os CLAUDE.md §9).
    param([string[]]$GhArgs)
    $prev = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    $out = & gh @GhArgs 2>$null
    $code = $LASTEXITCODE
    $ErrorActionPreference = $prev
    if ($code -ne 0) { throw "gh $($GhArgs -join ' ') failed (exit $code)" }
    return $out
}

$cutoff = (Get-Date).ToUniversalTime().AddHours(-$Hours)
# GitHub search date granularity is per-day; widen then filter precisely below.
$searchDay = $cutoff.AddDays(-1).ToString('yyyy-MM-dd')

try {
    $raw = Invoke-Native @('api', '-X', 'GET', 'search/issues',
                           '-f', "q=involves:$User updated:>=$searchDay",
                           '--jq', '.items[] | {url: .html_url, api: .url, title: .title, repo: .repository_url, updated: .updated_at, state: .state, comments: .comments}')
} catch {
    Write-Output "CHECK FAILED: $($_.Exception.Message)"
    exit 1
}

$items = @()
foreach ($line in @($raw)) { if ($line) { $items += ($line | ConvertFrom-Json) } }

$waiting = @()
foreach ($it in $items) {
    if (([datetime]$it.updated).ToUniversalTime() -lt $cutoff) { continue }
    if ($it.comments -lt 1) { continue }
    try {
        $lastRaw = Invoke-Native @('api', "$($it.api)/comments?per_page=100", '--paginate',
                                   '--jq', '.[-1] | {user: .user.login, created: .created_at, body: .body[0:400]}')
    } catch { continue }
    if (-not $lastRaw) { continue }
    $last = ($lastRaw | Select-Object -Last 1) | ConvertFrom-Json
    if ($last.user -eq $User) { continue }                                  # we spoke last — nothing owed
    if ($last.user -match '\[bot\]$') { continue }                          # CI/review bots never await a human reply
    if (([datetime]$last.created).ToUniversalTime() -lt $cutoff) { continue } # their comment predates the window
    $waiting += [pscustomobject]@{
        Repo = ($it.repo -replace '.*/repos/', ''); Title = $it.title; Url = $it.url
        From = $last.user; When = $last.created; Excerpt = ($last.body -replace '\s+', ' ')
    }
}

if ($waiting.Count -eq 0) {
    Write-Output "GITHUB INBOX CLEAR - no thread awaiting a reply in the last $Hours h (checked $($items.Count) involved thread(s))"
    exit 0
}

Write-Output "GITHUB INBOX: $($waiting.Count) thread(s) awaiting a reply in the last $Hours h"
foreach ($w in $waiting) {
    Write-Output ""
    Write-Output "  [$($w.Repo)] $($w.Title)"
    Write-Output "  last: @$($w.From) at $($w.When)"
    Write-Output "  $($w.Url)"
    Write-Output "  > $($w.Excerpt.Substring(0, [Math]::Min(300, $w.Excerpt.Length)))..."
}
exit 2
