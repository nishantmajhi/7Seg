# Code Review — 7Seg

First reviewed 2026-09-04. Remediated 2026-10-01 on `cleanup/2026-10-01`,
nine commits, not pushed. This file is now the record of that pass; the
original findings are summarised under "The first review" below.

## For the author first

**One change alters what you see, with your agreement: the ninth
exception.** `clock.exe` was linked as a console program, so double-
clicking it opened a console window (a Windows Terminal window here)
beside the clock, and closing that window ended the clock. It is now a
Windows program. That is `a731fe6` on its own, so it can be reverted
alone. Everything else on screen is pixel-identical to your first commit;
see "Tests".

**The repository is public** (`github.com/nishantmajhi/7Seg` answers the
GitHub API anonymously). Nothing in it is sensitive: no credentials, no
personal data beyond the author line.

**Two small things only you can settle.** The `LICENSE` names **Manish
Majhi**, as the four other C projects in this archive do and as your git
email suggests. This README's author line says **Nishant Majhi**, and was
left as you wrote it. Make them agree whichever way is right. And the
`dist/` folder from March is gone; see "Deliberately not done".

## Corrections to the first review

**Its verdict was too kind to the lifecycle.** "There are no memory bugs
of note because there is barely any allocation" missed a use after free.
Launch the clock twice and GApplication opens a second window in the same
process. Close the first, and its timer keeps calling
`gtk_widget_queue_draw()` on the freed drawing area ten times a second,
for good. It printed GLib criticals here only because the memory had not
been reused yet.

**"No handling for a window resized smaller than the digits can render;
GTK will clip. Not a crash."** Confirmed rather than corrected. The face
drawn at 60×30 and at 20×5 clips, and cairo stays in a success state.
Both sizes are now render-test cases.

**The README's preview was not "broken".** `./icon.png` rendered fine; it
was just the 2 KB icon rather than the clock. Replaced by `screenshot.png`
all the same.

## What was fixed

1. **Choosing a colour in one window recoloured another.** The colour
   action was registered on the application, so each new window replaced
   it with one bound to the new window's state. With two windows open,
   choosing Red in the first turned the second red. Each window now has
   its own `win.led-color`. (`09063a4`)
2. **Closing one of two windows left its timer drawing into freed
   memory**, and no window's state was ever freed. The drawing area now
   owns its state through the draw function's destroy notify, which
   removes the timer first. (`09063a4`)
3. **Ten repaints a second for a display that changes twice.** This was
   the review's P2. The tick is now one-shot, scheduled from the wall
   clock for 1 ms past the next second or half-second, and re-armed each
   time. An idle window goes from 28 repaints in 3 s to 6 or 7, and the
   latest repaint after a change goes from 494 ms to under 60 ms. CPU
   with one idle window falls from 3.4–4.3% of a core to 1.2–1.35%; the
   remainder is GTK's own cost per frame. The review's alternative, a
   250 ms free-running timer, would have shown each change up to 250 ms
   late. (`4b3ee09`)
4. **No window icon when started from an MSYS2 shell**, which is how the
   old README said to run it. GLib looks for `share/` beside its DLL only
   while `XDG_DATA_DIRS` is unset, and MSYS2's profile sets it. The line
   meant for this, `g_setenv("XDG_DATA_DIRS", "./share:/mingw64/share",
   FALSE)`, could not work: it never overwrites, GLib on Windows splits
   on `;`, and `./share` is relative to the working directory. Replaced
   by adding `share/icons` beside the exe to GTK's icon search path.
   (`1e38299`)
5. **The console window**, above. (`a731fe6`)
6. **The file header** named `led_clock.c` and a compile line that failed.
   This was the review's P2. (`b1319ec`)
7. **One clock reading per frame.** The digits came from `time()` and the
   colons from a separate `clock_gettime()`, so the two could straddle a
   second. A NULL from `localtime()` reached `strftime`. Neither was ever
   observed, so neither has a regression test. (`b1319ec`)

Also done, with no visible effect: the source is in `src/`. What the face
shows is in `src/face.c`, with no GTK in it. The default colour is
`FACE_DEFAULT_COLOR` beside the table, not `6` in one place and `"Purple"`
in another. The dim and glow constants say what they do. `build.sh` takes
an output folder, refuses to `rm -rf` one that is not a previous build,
and compiles with `-lm` as the README always said. Also added: a
`LICENSE` (the README promised MIT), a real screenshot, `*.exe` ignored,
and a rewritten README.

## Tests

`tests/run.sh` from MSYS2 MINGW64. There was no test suite before.

| Suite | Checks | What it holds |
| --- | --- | --- |
| `face_test.c` | 58 | Segments per digit, stated as datasheet letters. The palette, digits from a time, colon phase, and the wait to the next change, swept across the whole second |
| `render_test.c` | 16 faces × 2 | `on_draw` into an off-screen surface at fixed instants, compared pixel for pixel with `tests/golden/`, which the first commit drew |
| `app_test.c` | 13 | Real windows. Two activations, Red chosen through the first window's own menu bar, pixels counted per window, windows closed while GLib warnings are counted, repaints of an idle window timed against the wall clock |
| `dist_test.ps1` | 8 | `build.sh`'s bundle run with a bare `PATH` from another folder, as a double-click and with `XDG_DATA_DIRS` set. Window, icon (`WM_GETICON`), empty stderr, PE subsystem |

`tests/run.sh --original` builds the same tests against the first commit,
using its own `build.sh` for the bundle. It requires the render test to
pass and every `regression:` check, and nothing else, to fail. It does:
8 of 8. **Every commit was checked out into its own worktree on D: and
tested there**, and passed. The suites grew across the commits: render
only, then 51 face checks, then 11 app checks, then 58 and 13, then the
bundle test at 7, then 8. `--original` also ran from a fresh worktree of
the last commit.

## Deliberately not done

- **The segment "glow" cannot be seen.** `DRAW_SEG` paints the halo under
  an opaque copy of the same shape. Only the colons' halo, which is
  larger, shows. That is design, so it was left alone and documented at
  `GLOW_ALPHA`.
- **A second launch opens a second window** rather than raising the first.
  That is how `on_activate` was written. It is kept, and the windows are
  now independent.
- **Startup takes 3.3–5.6 s from cold** on this machine, the same for
  the console and Windows builds and the original. It was about 3.4 s
  even with a session bus already running, so the bus is not the main
  cost. Not investigated further. Each cold start of a bundle also
  leaves a `gdbus.exe` session bus running for a while after the clock
  exits; that is GLib's design on Windows.
- **The March `dist/` (66 MB)** was the review's P2 to delete. It went
  sooner than planned: running `./build.sh` in the project during this
  pass replaced it, because the script begins with `rm -rf dist`. It was
  gitignored build output, and what mattered about it had already been
  recorded: it was a console program, and its window had an icon. The
  rebuilt one was deleted at the end, so the project folder holds no
  build output.

## Notes for the next session

- GTK is MSYS2 at `D:\dev-tools\msys64`, with gcc, pkgconf, gtk4, git and
  diffutils. Run the tests from its MINGW64 shell. From Git Bash, use
  `MSYSTEM=MINGW64 /d/dev-tools/msys64/usr/bin/bash.exe -lc "cd ... &&
  tests/run.sh"`.
- **Test a bundle with `XDG_DATA_DIRS` controlled.** The MSYS2 shell sets
  it and Git Bash does not, so the same bundle had an icon or not
  depending on which shell started the test. That looked like a missing
  `hicolor/index.theme` and then like a slow first launch before it was
  found. Two wrong theories, each "confirmed" by one lucky run.
- **`GtkWidgetPaintable` is not a reliable way to read a window's
  pixels.** About one run in five it returned no render node for a
  window that was mapped and focused. Calling the widget class's
  `snapshot` method is.
- If you change the geometry on purpose, regenerate the goldens from the
  new code, not with `make_golden.sh`, which draws the first commit.

## The first review, summarised

Reviewed 2026-09-04. Verdict: small, focused, clean drawing code, held
back by housekeeping. P2: delete the 66 MB `dist/`; fix the stale file
header; reduce the 10 Hz redraw. P3: a real screenshot for the README; a
`LICENSE`; name the magic constants and the default colour. Smaller
notes: document `AppState`'s lifetime, and tiny windows clip. All of these
are addressed above. Its claim of no memory bugs was wrong; see
"Corrections".

## Licence, 2026-10-02

The author chose one licence for all of their projects: free for personal,
non-commercial use, modifiable for the user's own use, and not to be
distributed, original or modified. No standard licence says that, so `LICENSE`
is a short custom text, the same in every project apart from the year and what
it excludes. The README's licence section says the same. Anything above that
calls the licence an open question, or calls the project MIT, is superseded.

The README's author line now says Manish Majhi, as the `LICENSE` does, which
settles the mismatch noted above.

## CI, 2026-10-02

`.github/workflows/ci.yml` sets up MSYS2 MINGW64 with GTK 4 and runs
`tests/run.sh` on `windows-latest`. Nothing has been pushed, so it has not run
on GitHub yet. Every job was run before committing, from a fresh clone of this
branch, the way a runner would run it: Windows jobs here, in PowerShell 7 or
the shell the step names, with only the tools the job sets up on `PATH`. The
face, render and app tests pass every time; `dist_test` is below.

**Not settled:** on this machine the bundle checks failed one or two of 8 on
the first launch of a freshly built bundle (the window not appearing within 15
s, or GLib warning that the bundled `gdbus.exe` "failed to launch bus") and
passed 8 of 8 when run again on the same bundle. That points at first-launch
latency here; the runner image switches Defender's real-time scanning off and
excludes C: and D:, so it should not apply there. If the job fails on GitHub,
look at `dist_test` first. This suite is not in `CONTINUE-HERE.md`'s sweep
list, so it had not been re-run since its own pass.
