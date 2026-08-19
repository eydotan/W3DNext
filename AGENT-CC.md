# Agent instructions — W3DNext

The D3D11 render backend for Command & Conquer Generals: Zero Hour, built against
the TheSuperHackers/GeneralsGameCode engine.

## GitHub is the working channel

Issues and discussions on this repository, and pull-request threads upstream. An
unanswered comment is unfinished work: someone is waiting on a reply, and the
project has no other channel where that surfaces.

**Every wrap-up check in this repository must also run:**

```
tools\github-inbox-check.ps1 -Hours 4
```

It flags any issue or pull request the maintainer is involved in, updated inside the
window, whose newest comment came from somebody else (bots excluded).

- **exit 0** — nothing awaits a reply; the wrap-up may report clean.
- **exit 2** — one or more threads await a reply. This is a failure, not a warning.
  Do not report the session clean. Draft the replies, ask before posting (posting is
  public), and re-run for a green.
- **exit 1** — the check itself failed (auth or network). That is unknown, not clear:
  say so rather than reporting clean.

Widen `-Hours` when the previous session was longer ago than the default window.

A green here means nothing unless a known-bad input would have gone red on the same
check. The control is `tools\github-inbox-check.ps1 -Hours 200 -User <someone else>`,
which must exit 2. Paste both outputs when a clean report rests on this check.

## Verify before reporting done

A change is done when a machine confirms it — a build exit code, the smoke test's
own verdict, an oracle's measured output. Not when it looks right and not when it
compiles.

Two specific traps in this repository:

- **A green game build proves nothing about the test target.** `w3d_d3d11_smoke`
  compiles `D3D11Backend.cpp` standalone and links no ww3d2, so anything it calls
  that lives in `Backend/RenderBackend.cpp` must have a duplicate definition under
  `#ifndef W3DNEXT_D3D11_W3D_TU`. Build the smoke target explicitly, every time.
- **Single-frame comparison cannot see frozen animation.** A frozen object matches
  its own first frame forever. Use the motion check, and run the oracle's `selftest`
  before trusting any of its greens.

## Never automate the Discord account

Reading or posting to Discord through a user account is self-botting and it has
already cost this account once. Reading is manual only, by screenshotting a window
the maintainer already has open. There is no exception for read-only access.
