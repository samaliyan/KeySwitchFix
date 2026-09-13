# KeySwitchFix 2.8.0 — full-source review

This pass read every file in the 2.7.0 tree (core, hook application,
installer, resources, generator, tests, workflows, and documentation) and
looked for three classes of problem: silent failure, false corrections, and
behaviour the user cannot see or undo. Findings are grouped by severity; each
one names the code that changed.

## Defects fixed

### 1. Silent hook loss (`app.c`, high)

`WH_KEYBOARD_LL` is removed by Windows when a callback exceeds the low-level
hook timeout, without any notification. The 2.7 dashboard kept reporting
`Keyboard hook: Running` because it only tested the `HHOOK` handle. A
watchdog timer now compares `GetLastInputInfo` with the tick of the last
event either hook delivered; a gap of more than four seconds means the system
delivered input the hooks never saw, so both hooks are reinstalled. The
watchdog re-arms once per quiet episode and waits for a real event before
considering another reinstall, because input to an elevated window (hidden by
UIPI) and input on the secure desktop look identical to a dead hook. Only a
failed `UnhookWindowsHookEx` (Windows had already discarded the handle) is
counted and reported as a real detachment; a hook that failed to install is
retried on its own every interval without disturbing the other hook.

### 2. `id`, `ms`, `pr` rewritten to Persian (`core.c`, high)

Two-key words rely on exact lists. Any English token missing from the English
list whose keys spell a listed Persian word is scored as one-sided Persian
evidence, which context and preference are deliberately not allowed to
suppress. `id`→`هی`, `ms`→`پس`, and `pr`→`حق` were all such cases: typing
`user id ` in English produced `user هی `. The three tokens are now on both
lists, so they behave like `of`/`خب`: unchanged without context, resolved by
Persian sentence context or an explicit preference. The frequent Blooms were
extended by the same union the generator would produce, and
`verify_metadata.py` now fails if the C and Python short-word lists diverge.

### 3. ZWNJ words could never be repaired (`core.c`, `app.c`, high for Persian)

Everyday Persian uses Shift+Space (zero-width non-joiner) inside
`می‌خواهم`, `کتاب‌ها`, `بزرگ‌تر`. The hook already treated Shift+Space as a
boundary, but `می`, `ها`, `تر`, `ام`, and `ات` were unknown to every
dictionary (two-letter words are excluded from the Blooms), so `ld` was never
corrected and the following `خواهم` was corrected on its own, leaving
`ld خواهم`. In addition, the replayed delimiter was a plain `VK_SPACE`, which
raced the layout switch and usually became a visible space. The five halves
are now short Persian words, the boundary records whether Shift was held, the
exact `U+200C` is injected when the target text is Persian, and phrase
history stores each word's separator so retrospective repair reproduces it.

### 4. Arabic yeh/kaf made correct Persian look unknown (`core.c`, `app.c`, medium)

The dictionaries are normalized to `ی` (U+06CC) and `ک` (U+06A9). A layout
that emits `ي`/`ك` (U+064A/U+0643) produced tokens that no Persian Bloom
contained, which is exactly the condition under which an accidental English
match rewrites correct Persian text. Runtime tokens are canonicalized before
lookup and before replacement. Diacritics are the related case: the
generator strips them, so `حتماً` typed with its tanwin was unknown. The
core now strips Arabic combining marks for every lookup (membership,
frequency, and prefix guards) while keeping the typed keys intact, so a
capitalized English word mistyped on the Persian layout (`Excel`, whose
Shift+E is a kasratan there) is still corrected.

### 5. `LoadKeyboardLayout` on every keystroke (`app.c`, medium)

When one of the two languages was not installed, `find_layout` fell back to
`LoadKeyboardLayoutW`, which adds that keyboard to the user's language bar as
a side effect — repeatedly, once per key. The fallback is removed; the static
table still translates keys, and the diagnostics say which layout is missing.

### 6. Uninstaller launched a bare `cmd.exe` (`installer.c`, low)

`CreateProcessW(NULL, L"cmd.exe ...")` searches the application and current
directories before `System32`. The helper now resolves the full path with
`GetSystemDirectoryW` and passes it as `lpApplicationName`.

### 7. Housekeeping (`app.c`, low)

The 500 ms diagnostics timer ran forever, even with the window hidden, and
rewrote every label on every tick (`SetWindowText` repaints unconditionally).
The timer now runs only while the window is visible and labels change only
when their text does. `TrackPopupMenu` from a tray icon needs a trailing
`WM_NULL` post or the menu stays open after a click elsewhere.

## Improvements added

- **Exclude this app** on the tray menu, based on the executable the user
  last typed in; the same item resumes correction if the app is already
  excluded. The list in the dashboard is updated and saved.
- `Ctrl + Win + K` toggles pause/resume globally.
- The dashboard scales all geometry and fonts by the system DPI and sizes its
  frame with `AdjustWindowRectEx`, so it is legible at 125–200 % scaling.
- Diagnostics show a missing-layout warning and the number of hook re-arms.

## Not changed, by design

- Undo semantics (plain Backspace only as the *immediate* next key) are kept.
- The decision thresholds, priors, and sentence scoring are untouched; every
  pre-existing regression still passes alongside the new ones.
- `fi` (`به`) is intentionally *not* added to the English list. `به` is one of
  the most frequent Persian words and `fi` only appears in English after a
  hyphen (`wi-fi`, `sci-fi`), where the hyphen already resets the word.

## Ideas for later versions

1. **UI Automation password detection.** Browser and Electron password
   fields are not Win32 `ES_PASSWORD` edits. `IUIAutomation::GetFocusedElement`
   plus `UIA_IsPasswordPropertyId` would cover Chrome, Edge, and Firefox.
   It must run outside the hook callback (post to the main thread and cache
   the result per focus change) because UIA cross-process calls can take
   tens of milliseconds.
2. **A "never change this word" list.** A small user dictionary consulted
   before scoring, editable from the dashboard, would give users a one-click
   fix for names, product codes, and jargon that collide with real words.
3. **Testable hook state machine.** The word/sentence/pending/undo state in
   `app.c` could move into a platform-free module fed by synthetic key
   events, so the 2.7 Space regression could have been caught by a unit
   test instead of a manual reproduction.
4. **Per-monitor DPI.** Handle `WM_DPICHANGED` and rebuild fonts when the
   window moves between monitors with different scaling.
5. **Persian dashboard.** The app is English-only; a right-to-left Persian
   UI (a resource-string table and `WS_EX_LAYOUTRTL`) would match the
   audience of the Persian README.
6. **Optional correction toast.** A brief, dismissable notification showing
   `original → replacement` for users who type without looking at the screen.
7. **Code signing.** A signing certificate would remove the SmartScreen
   warning; the release workflow already produces reproducible checksums.
