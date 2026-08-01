# KeySwitchFix — MASTER PROJECT HANDOFF
## Complete AI Continuation / Reconstruction Specification

**Project:** KeySwitchFix  
**Current stable version:** 2.7.0  
**Platform:** Windows 10/11 x64  
**Repository:** `samaliyan/KeySwitchFix`  
**Default branch:** `main`  
**Current language:** C11 / Native Win32  
**License:** MIT for application code  
**Status:** Working native application with installer, uninstaller, tray UI, offline dictionaries, CI/release pipeline, contextual detection, phrase correction, and Undo.

---

# 1. IMPORTANT INSTRUCTION FOR THE NEXT AI

You are continuing an existing production-oriented Windows project called **KeySwitchFix**.

Do NOT redesign the project from scratch unless explicitly instructed.

Before making changes:

1. Inspect the existing repository.
2. Read:
   - `README.md`
   - `README_FA.md`
   - `CHANGELOG.md`
   - `docs/ARCHITECTURE.md`
   - latest strict-review documents
   - `CONTRIBUTING.md`
3. Inspect current:
   - `src/app.c`
   - `src/core.c`
   - `src/core.h`
   - `src/installer.c`
   - tests
   - resource files
   - GitHub Actions workflows.
4. Treat the existing implementation as the source of truth.
5. Preserve all working functionality unless a requested change explicitly requires otherwise.
6. Do not casually simplify the detection engine.
7. Do not replace the native architecture with C#, Python, Electron, .NET, Java, or another runtime.
8. False automatic corrections are significantly worse than missed corrections.
9. Every important detection change must include regression tests.
10. Run the full native test/build verification before considering a change finished.

If the repository is unavailable and the project must be reconstructed, use this document as the product and architecture contract.

---

# 2. PROJECT PURPOSE

KeySwitchFix is a lightweight Windows utility that automatically detects text typed using the wrong Persian/English keyboard layout and repairs it.

Typical problem:

The user wants to type:

`hello`

but Windows is currently on Persian layout, so the physical keys produce:

`اثممخ`

KeySwitchFix should recognize that the physical keystrokes correspond to the English word `hello`, replace the wrongly typed text, and switch the target application's keyboard layout to English.

The reverse direction must also work.

Example:

Physical keys:

`sghl`

Typed under the wrong English layout:

`sghl`

Intended Persian:

`سلام`

Correction:

`sghl` → `سلام`

The program must work globally across ordinary Windows desktop applications.

---

# 3. CORE PRODUCT PHILOSOPHY

KeySwitchFix must remain:

- extremely lightweight;
- native Windows;
- fast;
- private;
- completely offline;
- deterministic;
- easy to uninstall;
- professional enough for public distribution;
- conservative about automatic correction.

The highest-priority rule is:

**False corrections are more harmful than missed corrections.**

If there is insufficient evidence, leave the user's text unchanged.

Never pretend an inherently ambiguous physical-key sequence can always be inferred automatically.

---

# 4. NON-NEGOTIABLE ARCHITECTURE

The current project is a:

**Native x64 Win32 C application using C11.**

It does NOT depend on:

- .NET
- Python runtime
- Node.js runtime
- Electron
- Java
- cloud APIs
- online AI
- web services
- Windows services.

The installed application should remain self-contained.

The application architecture is single-process.

Main application:

`KeySwitchFix.exe`

Installer:

`KeySwitchFix-Setup.exe`

Standalone uninstaller:

`KeySwitchFix-Uninstall.exe`

---

# 5. CURRENT HIGH-LEVEL INPUT PIPELINE

The runtime pipeline is approximately:

### Step 1 — Keyboard Hook

Use a global:

`WH_KEYBOARD_LL`

low-level keyboard hook.

The hook receives physical keyboard events.

The application tracks physical scan codes rather than simply trusting the Unicode characters Windows produced.

---

### Step 2 — Determine active target context

Determine:

- foreground window;
- target process;
- foreground thread;
- active keyboard layout;
- whether the layout is English;
- whether it is Persian;
- whether the application/context should be ignored.

Unsupported layouts should not trigger correction.

---

### Step 3 — Physical-key translation

For every eligible word key, produce the characters that the same physical key would generate under:

- English layout;
- Persian layout.

Prefer translating through the **actual Windows layouts** using:

`ToUnicodeEx`

rather than assuming only one static Persian layout.

The program must support both:

- Windows Persian
- Windows Persian (Standard)

These layouts differ on some physical keys, especially around Persian `پ/ئ`.

There is still a static fallback mapping in the core, but runtime Windows layout translation is preferred.

---

# 6. CRITICAL 2.7.0 SCAN-CODE RULE

Do NOT pass every printable `ToUnicodeEx` result into the current word.

This caused a major real-world bug before version 2.7.

Windows considers Space printable.

The old logic accidentally allowed Space into the word token instead of processing it as a boundary.

Consequences:

- Space boundaries were never reaching phrase evaluation;
- sentence analysis worked in unit tests but not during real typing;
- `nv clhkd ;i` was not being corrected during normal typing.

Version 2.7 fixed this with an explicit word scan-code gate.

Important core function:

`ks_is_word_scancode(...)`

Runtime logic must check whether a physical scan code is a valid **word key before** translating it as a word token.

Space, digits and unsupported punctuation must not accidentally become part of the word.

At the same time, the alternate OEM physical key required by the two Persian Windows layouts must remain eligible.

Do not regress this.

---

# 7. CURRENT WORD MODEL

Maximum physical tokens in one candidate word:

`KS_MAX_WORD = 32`

Each physical key is represented using something conceptually equivalent to:

`KS_TOKEN`

with parallel candidates:

- English character
- Persian character.

From the same sequence of physical tokens, the program can reconstruct:

- English interpretation;
- Persian interpretation.

---

# 8. OFFLINE DICTIONARIES

KeySwitchFix uses offline Bloom filters.

The current lexicon structure contains approximately:

### Base dictionaries
- English words
- Persian words

### Common vocabulary
- English common
- Persian common

### Frequent vocabulary
- English frequent
- Persian frequent

### Proper prefix dictionaries
- English prefixes
- Persian prefixes

### Common-word prefixes
- English common prefixes
- Persian common prefixes

Current effective union is approximately:

**159,852 English/Persian spellings**

Current wordfreq tiers include approximately:

- top 20,000 common English words
- top 20,000 common Persian words
- 2,000 frequent English words
- 2,000 frequent Persian words

wordfreq version currently used:

`wordfreq 3.1.1`

Base dictionaries come from pinned English/Persian dictionary packages.

Raw full word lists should not simply be bundled without checking their licenses.

Upstream notices/licenses must remain under:

`third-party/`

---

# 9. CURRENT BLOOM RESOURCE FILES

Expected embedded resources include:

`resources/en.bloom`

`resources/fa.bloom`

`resources/en-prefix.bloom`

`resources/fa-prefix.bloom`

`resources/en-common.bloom`

`resources/fa-common.bloom`

`resources/en-frequent.bloom`

`resources/fa-frequent.bloom`

`resources/en-common-prefix.bloom`

`resources/fa-common-prefix.bloom`

They are embedded directly into `KeySwitchFix.exe` through the Windows resource system.

Do not require external dictionary files after installation.

---

# 10. BLOOM RESOURCE GENERATOR

Current reproducible generator:

`tools/generate_blooms.py`

It is used to regenerate the dictionaries and prefix resources.

The generation process uses pinned source versions and validates expected source hashes before replacing resources.

Current reproduction roughly uses:

`dictionary-en@4.0.0`

`dictionary-fa@2.0.0`

`wordfreq==3.1.1`

Do not manually replace generated Bloom files with arbitrary Internet word lists.

Preserve reproducibility and license notices.

---

# 11. DETECTION MODEL

The current engine is not simply:

"opposite word exists → replace."

Detection combines multiple signals.

Important evidence includes:

- English candidate dictionary membership;
- Persian candidate dictionary membership;
- common vocabulary membership;
- frequent vocabulary membership;
- word length;
- proper-prefix safety;
- common-prefix safety;
- current keyboard layout;
- live/idle/boundary evaluation phase;
- recent document language;
- position inside a sentence;
- bilingual collision frequency;
- explicit writing-language preference;
- phrase coherence.

The decision must remain deterministic and offline.

---

# 12. OBJECTIVE ONE-SIDED MATCHES

A very important distinction:

If:

- wrong/current-layout candidate is NOT recognized;
- opposite-layout candidate IS clearly recognized;

this represents strong objective evidence.

Example:

Persian-layout output:

`اثممخ`

Opposite English candidate:

`hello`

If English `hello` is known and `اثممخ` is not a legitimate Persian candidate, it should be corrected.

Explicit "Prefer Persian" mode should NOT suppress objective one-sided evidence.

This specific design mistake happened in an older implementation and was fixed.

---

# 13. TRUE BILINGUAL COLLISIONS

Some physical key sequences are legitimate words in both languages.

Example:

English:

`leg`

The same physical keys under Persian correspond to:

`مثل`

Both are legitimate words.

Another example:

English:

`of`

Persian:

`خب`

These are inherently ambiguous in isolation.

Do NOT automatically guess every such collision.

For collisions, use:

- surrounding language context;
- sentence position;
- frequent/common evidence;
- writing-language preference.

Two-key collisions such as:

`of` ↔ `خب`

must NOT be decided from corpus frequency alone.

If there is insufficient evidence in Auto mode, leave the current text unchanged.

---

# 14. WRITING LANGUAGE MODES

The UI currently supports three modes:

### Auto

Use context and evidence.

Do not force ambiguous collisions without enough evidence.

### Prefer Persian

When there is a genuine collision and automatic evidence is insufficient, prefer the Persian interpretation.

### Prefer English

Same behavior in the opposite direction.

This preference affects ambiguous collisions.

It should NOT override objective one-sided layout evidence.

The tray menu exposes this through a:

**Writing language**

submenu.

---

# 15. LIVE CORRECTION

The program no longer requires the user to press Space for every obvious correction.

High-confidence mistakes can be corrected immediately while typing.

For example a sufficiently unambiguous three-key-or-longer wrong-layout word may be fixed as soon as confidence becomes high.

However this introduces prefix risk.

Example:

A partially typed legitimate word must not be corrected because its current prefix happens to form another valid word in the other language.

Therefore live correction must use proper-prefix guards.

---

# 16. PREFIX SAFETY

If the currently typed characters form a proper prefix of a likely valid current-language word, the application should often wait rather than correct immediately.

This prevents mistakes such as interpreting an incomplete Persian:

`بهسا...`

as an English word such as:

`fish`

during a short thinking pause.

Both base and common-prefix resources participate in this logic.

Do not weaken prefix protection without adversarial testing.

---

# 17. ADAPTIVE IDLE CORRECTION

Certain short or ambiguous words should wait briefly.

The application tracks the user's recent key timing and maintains something similar to:

`g_average_key_interval`

Default initial estimate is approximately:

150 ms.

The idle correction delay is derived adaptively.

Current core functions include concepts equivalent to:

`ks_update_key_interval_ms(...)`

and:

`ks_idle_delay_ms(...)`

Two-key words may be evaluated after this adaptive pause instead of always waiting for Space.

---

# 18. EVALUATION PHASES

The core distinguishes phases equivalent to:

`KS_PHASE_LIVE`

`KS_PHASE_IDLE`

`KS_PHASE_BOUNDARY`

Decisions can differ depending on whether:

- the person is actively typing;
- they have paused;
- the word boundary has been reached.

Do not collapse these into one universal threshold.

---

# 19. WORD BOUNDARIES

Important supported word-boundary events include:

- Space
- Enter
- Tab
- period in appropriate contexts.

Boundary handling is critical because ambiguous words that were unsafe to modify live may become safe once the word is complete.

---

# 20. CURRENT SENTENCE MONITOR

Version 2.7 changed phrase analysis substantially.

The application tracks the **current sentence from its beginning**, rather than only a tiny sliding window.

Hard limits:

`KS_MAX_SEQUENCE_WORDS = 32`

`KS_MAX_SEQUENCE_CHARS = 512`

These are safety limits.

At every Space, the program can review the sentence and suffixes under both language interpretations.

A later word can therefore provide evidence that an earlier ambiguous word was typed using the wrong layout.

---

# 21. EXAMPLE OF RETROSPECTIVE PHRASE CORRECTION

Important regression:

User physically types:

`nv clhkd ;i`

while the wrong layout is active.

Correct Persian interpretation:

`در زمانی که`

The system should eventually recognize the coherent Persian sequence and repair the phrase.

This is not full natural-language understanding.

It is a bounded deterministic sequence scorer.

---

# 22. PHRASE SCORING RULES

For a candidate phrase:

- known target words increase confidence;
- frequent target words can increase confidence;
- unknown target words are penalized;
- coherent same-language runs gain additional evidence;
- incomplete target phrases should not trigger unsafe rewriting;
- mixed-language sequences should not be flattened into one language.

A retrospective correction should require every target word in the rewritten run to be recognized strongly enough.

Mixed-language content must be protected.

Example:

`سلام test`

should not automatically become a single-language phrase merely because one interpretation obtains a slightly higher scalar score.

---

# 23. PENDING WORD STATE

Another important 2.7 fix:

When a word is corrected immediately or after idle evaluation, it must not disappear from sentence history before the following Space.

Current implementation keeps a pending word state.

Conceptually:

1. word corrected live;
2. visible corrected word stored as pending;
3. following Space commits that word into sentence history.

Do NOT call generic word-clear logic in a way that deletes pending evidence before it reaches the sentence monitor.

---

# 24. RETROSPECTIVE CORRECTION MUST PRESERVE EARLIER CONTEXT

An earlier implementation repaired a phrase and then cleared all history.

That meant the beginning of the same sentence was lost.

Version 2.7 fixed this.

After retrospective correction:

- update the corrected suffix/history;
- preserve the valid beginning of the sentence;
- continue monitoring the current sentence.

Do not reset all sentence state simply because one suffix was repaired.

---

# 25. LANGUAGE CONTEXT

The application maintains per-window weighted language context.

Conceptually represented by:

`KS_LANGUAGE_CONTEXT`

with:

- current language;
- strength;
- observed word count.

Recent confirmed words and corrections contribute evidence.

The language intent/document context persists for approximately:

**90 seconds**

inside the relevant window.

This helps resolve ambiguous words.

---

# 26. DOCUMENT LANGUAGE VS SENTENCE POSITION

Sentence position matters.

At the beginning of a sentence, normalized bilingual word frequency may provide some evidence.

Mid-sentence, surrounding document language generally deserves more weight.

Sentence-ending punctuation can reset sentence position while preserving useful document-level language evidence.

This is intentional.

Do not reset every contextual signal at every period.

---

# 27. CONTEXT INVALIDATION

The cached text/phrase model must be discarded whenever the assumed caret position becomes unsafe.

Examples include:

- foreground window changes;
- certain mouse clicks;
- navigation keys;
- unsupported punctuation;
- explicit manual keyboard-layout switches;
- relevant setting changes;
- sequence/history overflow.

When the program is no longer certain that the internally cached characters correspond to text immediately before the caret, retrospective rewriting should be disabled.

Safety beats correction coverage.

---

# 28. SENTENCE LIMIT BEHAVIOR

Hard bounds exist for both performance and safety.

Maximum:

32 words

or:

512 reconstructed characters.

If history exceeds safe bounds:

do NOT continue indefinitely.

Disable/clear retrospective rewriting rather than risking deletion of unrelated text.

Keyboard-hook latency must stay predictable.

---

# 29. PERSIAN `آ` / `ا` HANDLING

Many Persian users type words such as:

`ایا`

instead of canonical:

`آیا`

because typing `آ` requires Shift.

The dictionary may contain canonical `آیا`.

Detection therefore performs a controlled canonical lookup where an initial Persian `ا` may also be checked as `آ`.

Important:

This canonicalization is for **dictionary lookup only**.

Do NOT silently rewrite the user's spelling from:

`ایا`

to:

`آیا`

unless explicitly requested.

The user's typed spelling should be preserved.

Regression examples also include:

`اقا`

`اب`

---

# 30. UNDO SYSTEM

Automatic changes must be easily reversible.

Current primary Undo interaction:

**one normal Backspace**

If pressed within approximately:

**15 seconds**

after an automatic correction, it should restore the exact original word or phrase.

This interaction intentionally resembles keyboard autocorrect behavior.

---

# 31. UNDO RECORD

The latest correction record contains information conceptually equivalent to:

- whether Undo is valid;
- target HWND;
- original source language;
- delimiter;
- creation time;
- exact original string;
- exact replacement string.

It should retain the exact text needed to restore the previous state.

Only intercept plain Backspace when restoration can safely succeed.

Otherwise allow Backspace to behave normally.

---

# 32. FALLBACK UNDO HOTKEY

The old registered shortcut remains available as fallback:

`Ctrl + Win + Backspace`

Do not replace plain Backspace with this shortcut.

Plain Backspace is the main UX.

The shortcut is only a fallback/accessibility path.

F12 must NOT be reintroduced.

F12 was explicitly rejected because it conflicts with common browser/developer functionality.

There must also be no obsolete "Run Test Self" / self-test UI button.

---

# 33. KEYBOARD LAYOUT SWITCHING AFTER CORRECTION

When KeySwitchFix repairs a word, it should also request the target application/thread to switch to the intended keyboard layout.

For example:

Persian output incorrectly generated while intending English:

repair text to English;

then switch target layout to English.

Likewise in the Persian direction.

This should target the relevant foreground application rather than merely changing some internal KeySwitchFix state.

---

# 34. INPUT INJECTION

Corrections use marked:

`SendInput`

events.

Injected inputs generated by KeySwitchFix itself must be distinguishable so the keyboard hook does not recursively process its own correction.

Current marker concept:

`INPUT_MARKER`

Do not create input feedback loops.

---

# 35. WINDOWS INTEGRITY LIMITATION

Windows UIPI/integrity rules apply.

A normal non-elevated KeySwitchFix process generally cannot inject keyboard input into an elevated administrator application.

If the target application is running elevated, KeySwitchFix may need to be running at the same integrity level.

Do not attempt dangerous workarounds around Windows security boundaries.

---

# 36. PASSWORD / SECURITY EXCLUSIONS

The application must avoid processing sensitive password contexts whenever reasonably detectable.

Standard Win32 password edit controls should be skipped.

Common password-management processes are excluded by default.

Current default excluded process list includes approximately:

`1Password.exe`

`Bitwarden.exe`

`CredentialUIBroker.exe`

`KeePass.exe`

`KeePassXC.exe`

`LastPass.exe`

`LockApp.exe`

Custom browser/password widgets cannot always be identified perfectly using normal Win32 edit styles; this limitation should remain documented.

---

# 37. PRIVACY REQUIREMENTS

KeySwitchFix is deliberately offline.

It must NOT:

- connect to the Internet;
- contain analytics;
- contain telemetry;
- show advertisements;
- upload crashes;
- send typed words anywhere;
- persist typed text;
- write correction history to files;
- write typed words into the Registry;
- write typed words into Windows Event Log;
- run a background cloud service;
- store a long-term typing history.

Only bounded current-word/current-sentence information exists temporarily in process memory.

The latest exact correction is retained only temporarily for Undo.

---

# 38. NO NETWORKING

Do not introduce:

- HTTP clients;
- auto-update background services;
- cloud dictionaries;
- online language detection;
- telemetry SDKs.

One of the product's selling points is that typing never leaves the computer.

---

# 39. UI LANGUAGE

Application UI must remain:

**English only**

unless the user explicitly asks to change that.

Do not create mixed Persian/English application UI.

README_FA may obviously remain Persian.

---

# 40. DASHBOARD

The application has a native Win32 dashboard.

Relevant controls/settings include:

- Enabled / protection state
- Sensitivity
- Writing language
- Start with Windows
- Excluded processes
- Save
- Hide
- status information
- active layout/current context
- keyboard hook status
- activity/live diagnostics
- key/word/correction counts.

The dashboard should remain clean, professional, simple and lightweight.

It should not feel like a developer/debugging application.

---

# 41. TRAY BEHAVIOR

KeySwitchFix is primarily a tray application.

Expected behavior:

- starts enabled;
- can start automatically with Windows;
- tray icon remains available;
- double-click tray icon opens dashboard;
- desktop shortcut may open/show dashboard;
- closing dashboard hides it to tray;
- application continues running after dashboard is closed;
- choosing Exit actually terminates application;
- tray icon should restore if Explorer/taskbar restarts.

Right-click menu includes functions such as:

- Open
- Enable/Pause correction
- Writing language submenu
- Exit.

---

# 42. WRITING LANGUAGE TRAY SUBMENU

Expected items:

- Auto
- Prefer Persian for collisions
- Prefer English for collisions

Use labels that make it clear this is a collision preference, not a universal forced language conversion.

---

# 43. SETTINGS STORAGE

Application settings currently live under approximately:

`%LOCALAPPDATA%\KeySwitchFix\settings.ini`

General settings include approximately:

`Enabled`

`Sensitivity`

`LanguageMode`

`StartWithWindows`

`ExcludedProcesses`

Defaults:

Enabled = true

Sensitivity = balanced/default middle setting

LanguageMode = Auto

StartWithWindows = true

ExcludedProcesses = password/security oriented default list.

---

# 44. START WITH WINDOWS

Autostart uses current-user Registry state approximately under:

`HKCU\Software\Microsoft\Windows\CurrentVersion\Run`

No machine-wide Administrator installation should be required.

---

# 45. INSTALL MODEL

Installer is per-user.

Install location:

`%LOCALAPPDATA%\Programs\KeySwitchFix`

Settings:

`%LOCALAPPDATA%\KeySwitchFix`

Installer must not require Administrator rights.

It creates appropriate:

- application files;
- Installed Apps registration;
- Start Menu shortcut;
- desktop shortcut where applicable;
- optional startup registration.

---

# 46. UNINSTALLER

The standalone uninstaller and Windows Installed Apps entry share the uninstall implementation.

Expected uninstall behavior:

- stop the installed application safely;
- remove application files;
- remove startup registration;
- remove Installed Apps registration;
- remove KeySwitchFix shortcuts;
- optionally preserve or delete personal settings.

Do not kill arbitrary processes merely because they happen to be named `KeySwitchFix.exe`.

Resolve/check the installed executable path.

---

# 47. SETUP UI

Setup remains a simple native Windows dialog.

Typical content:

Title:

`Install KeySwitchFix`

Description:

`Automatic Persian / English keyboard layout repair`

Checkbox:

`Start KeySwitchFix with Windows`

Note:

`Installs for the current Windows user. Administrator access is not required.`

Buttons:

`Install`

`Cancel`

Upon successful completion the user can finish/launch KeySwitchFix.

Previous stale Setup version text bugs must not return.

---

# 48. IMPORTANT USER EXPERIENCE REQUIREMENTS

The application should feel like a polished utility, not a prototype.

Required characteristics:

- lightweight;
- no unnecessary windows;
- no console window;
- clean tray UX;
- proper icon;
- reliable startup;
- clean uninstall;
- no confusing debug buttons;
- English UI;
- low CPU use;
- low memory use;
- no obvious typing latency.

---

# 49. VERIFIED IMPORTANT TEST CASES

The following cases are part of the product contract.

### English intended while Persian layout active

Wrong:

`اثممخ`

Expected:

`hello`

This must work.

---

### Persian intended while English layout active

Physical:

`sghl`

Expected:

`سلام`

---

### Persian book example

Physical:

`;jhf`

Expected:

`کتاب`

---

### Contextual phrase

Physical:

`nv clhkd ;i`

Expected:

`در زمانی که`

---

### Persian common word

Physical:

`;l;`

Expected:

`کمک`

---

### Persian common word

Physical:

`fdk`

Expected:

`بین`

---

### Short word

Physical:

`jh`

Expected:

`تا`

Should become correct after adaptive pause or boundary when appropriate.

---

### Another short word

Physical:

`;k`

Expected:

`کن`

---

### Persian

Physical:

`mdnh`

Expected:

`پیدا`

Must support both Windows Persian layouts.

---

# 50. IMPORTANT COLLISION REGRESSIONS

### `of` / `خب`

Physical keys form both valid possibilities.

Auto without sufficient Persian context:

preserve legitimate English `of`.

With strong Persian context or Prefer Persian:

may convert to:

`خب`

Do not guess this two-key collision from frequency alone.

---

### `leg` / `مثل`

Both are legitimate three-key words.

Auto without Persian evidence:

legitimate English `leg` should remain English.

With Persian context or explicit Prefer Persian:

`leg` physical keys may become:

`مثل`

---

### `how`

There was an older failure involving a Persian-side candidate similar to:

`اخص`

Frequent-word evidence was added so genuine intended English `how` can be recognized appropriately.

Correct English `how` must not be damaged when typed under the correct layout.

---

# 51. VALID CORRECT-LAYOUT WORDS MUST SURVIVE

Tests should verify that words typed with the correct keyboard layout remain unchanged.

Especially test:

Persian:

`کمک`

`کن`

`بین`

`تا`

`خب`

`پیدا`

`ایا`

`مثل`

`بهسازی`

English:

`of`

`leg`

`how`

`really`

Correct text must not be "fixed" merely because an alternative exists.

---

# 52. MIXED-LANGUAGE SAFETY TEST

Example:

`سلام test`

This is legitimate mixed-language content.

The phrase engine must not flatten it into one language just because one language gets a slightly better score.

Code, usernames, technical names and mixed text are common in real typing.

Conservatism is required.

---

# 53. CURRENT CORE TYPES / CONTRACT

`src/core.h` currently exposes concepts including:

`KS_LANGUAGE`

Values roughly:

- OTHER
- ENGLISH
- PERSIAN

`KS_TOKEN`

parallel English/Persian character.

`KS_BLOOM`

Bloom resource descriptor.

`KS_LEXICONS`

all base/common/frequent/prefix lexicons.

`KS_LANGUAGE_CONTEXT`

weighted recent language context.

`KS_SEQUENCE_WORD`

physical token sequence for a completed word.

`KS_SEQUENCE_RESULT`

phrase scoring result.

`KS_DECISION`

correction decision including:

- should_correct;
- key_count;
- confidence;
- source language;
- target language;
- original;
- replacement.

`KS_LIVE_RESULT`

approximately:

- NONE
- WAIT_FOR_IDLE
- CORRECT_NOW

`KS_EVALUATION_PHASE`

approximately:

- LIVE
- IDLE
- BOUNDARY.

Do not casually break this core/application separation.

---

# 54. IMPORTANT CORE FUNCTIONS

Current API concepts include functions equivalent to:

`ks_bloom_init`

`ks_bloom_contains`

`ks_is_word_scancode`

`ks_map_scancode`

`ks_tokens_to_english`

`ks_tokens_to_persian`

`ks_word_membership`

`ks_classify_word`

`ks_collision_prior_points`

`ks_evaluate_sequence`

`ks_context_reset`

`ks_context_observe`

`ks_context_current`

`ks_evaluate_contextual`

`ks_evaluate_smart`

`ks_evaluate_smart_common`

`ks_evaluate`

`ks_evaluate_live`

`ks_update_key_interval_ms`

`ks_idle_delay_ms`

Existing callers/tests may depend on them.

Avoid gratuitous API churn.

---

# 55. PROJECT COMPONENT RESPONSIBILITIES

## `src/core.c`

Pure-ish detection core.

Responsibilities:

- fallback physical scan-code mapping;
- Bloom parsing;
- dictionary membership;
- prefix checks;
- candidate generation;
- word scoring;
- collision evidence;
- context scoring;
- sequence/phrase evaluation;
- confidence decision logic;
- adaptive timing helpers.

Keep Windows UI concerns out of this file wherever possible.

---

## `src/app.c`

Native Windows runtime application.

Responsibilities:

- keyboard hook;
- mouse hook;
- foreground/context detection;
- real Windows layout translation;
- current word;
- pending word;
- current sentence history;
- timers;
- context state;
- applying corrections;
- `SendInput`;
- keyboard layout switching;
- Undo;
- settings;
- startup;
- tray;
- dashboard;
- diagnostics.

---

## `src/installer.c`

Shared installer/uninstaller implementation.

Responsibilities:

- per-user installation;
- extraction/copying;
- Installed Apps registration;
- shortcuts;
- startup selection;
- uninstall;
- safe process termination.

---

# 56. VERIFIED PROJECT STRUCTURE

Important known files include approximately:

```text
KeySwitchFix/
│
├─ .github/
│  ├─ workflows/
│  │  ├─ ci.yml
│  │  └─ release.yml
│  └─ ISSUE_TEMPLATE/
│
├─ docs/
│  ├─ ARCHITECTURE.md
│  ├─ PRIVACY.md
│  ├─ STRICT_REVIEW_2.3.0.md
│  ├─ STRICT_REVIEW_2.5.0.md
│  ├─ STRICT_REVIEW_2.6.0.md
│  └─ STRICT_REVIEW_2.7.0.md
│
├─ resources/
│  ├─ app.ico
│  ├─ app-icon.png
│  ├─ app.rc
│  ├─ installer.rc
│  ├─ uninstaller.rc
│  ├─ resource.h
│  │
│  ├─ en.bloom
│  ├─ fa.bloom
│  ├─ en-prefix.bloom
│  ├─ fa-prefix.bloom
│  ├─ en-common.bloom
│  ├─ fa-common.bloom
│  ├─ en-frequent.bloom
│  ├─ fa-frequent.bloom
│  ├─ en-common-prefix.bloom
│  └─ fa-common-prefix.bloom
│
├─ src/
│  ├─ app.c
│  ├─ core.c
│  ├─ core.h
│  └─ installer.c
│
├─ tests/
│  ├─ core_tests.c
│  ├─ verify_metadata.py
│  └─ verify_pe.py
│
├─ tools/
│  └─ generate_blooms.py
│
├─ third-party/
│  ├─ README.txt
│  ├─ dictionary-en-LICENSE.txt
│  ├─ dictionary-fa-LICENSE.txt
│  ├─ wordfreq-LICENSE.txt
│  └─ wordfreq-NOTICE.txt
│
├─ build-native.sh
├─ package-release.sh
├─ VERSION
├─ README.md
├─ README_FA.md
├─ CHANGELOG.md
├─ CONTRIBUTING.md
├─ SECURITY.md
├─ LICENSE.txt
└─ .gitignore
```

If reconstructing from scratch, preserve approximately this separation.

---

# 57. WINDOWS RESOURCE EMBEDDING

`resources/app.rc` embeds:

- application icon;
- all Bloom dictionaries;
- executable version metadata.

Setup resources embed the built application.

For example Setup currently embeds:

`../dist/KeySwitchFix.exe`

and:

`../dist/KeySwitchFix-Uninstall.exe`

inside `KeySwitchFix-Setup.exe`.

The installer should therefore remain standalone.

---

# 58. BUILD SYSTEM

Current build system intentionally avoids Visual Studio project dependency.

Primary script:

`build-native.sh`

Native core tests compile using GCC.

Windows binaries cross-compile using Zig.

Current Zig package/version:

`@oven/zig-linux-x64@0.12.0-dev.1286`

Typical source build:

```bash
npm install --prefix /tmp/keyswitchfix-zig @oven/zig-linux-x64@0.12.0-dev.1286

ZIG=/tmp/keyswitchfix-zig/node_modules/@oven/zig-linux-x64/zig \
./build-native.sh
```

Required tools include:

- Bash
- GCC
- Python 3
- npm
- zip
- Zig cross compiler.

---

# 59. COMPILATION QUALITY GATE

C code must compile with strict warnings.

Current flags include:

`-std=c11`

`-Wall`

`-Wextra`

`-Werror`

Do not "fix" warnings by turning warnings-as-errors off.

Fix the underlying code.

---

# 60. BUILD OUTPUTS

Expected dist outputs:

`dist/KeySwitchFix.exe`

`dist/KeySwitchFix-Setup.exe`

`dist/KeySwitchFix-Uninstall.exe`

`dist/SHA256SUMS.txt`

Release packaging additionally generates approximately:

`KeySwitchFix-<VERSION>-Windows-x64.zip`

---

# 61. BUILD VALIDATION

The build pipeline performs:

- metadata validation;
- native core unit/regression tests;
- resource compilation;
- x64 Windows executable build;
- installer build;
- uninstaller build;
- PE architecture validation;
- embedded resource/payload verification;
- SHA-256 generation.

Do not consider "compiles successfully" sufficient.

All verification stages must pass.

---

# 62. VERSION CONSISTENCY

Current version:

`2.7.0`

Version metadata is intentionally duplicated in several user-visible/binary locations.

When bumping a release, update all required locations consistently.

Current metadata verification expects synchronization between:

`VERSION`

`src/app.c`

`src/installer.c`

`resources/app.rc`

`resources/installer.rc`

`resources/uninstaller.rc`

and relevant documentation/changelog.

Resource files require matching:

`FILEVERSION`

`PRODUCTVERSION`

`FileVersion`

`ProductVersion`

Run:

`python3 tests/verify_metadata.py`

before release.

Do not manually bump only `VERSION`.

---

# 63. METADATA REGRESSION REQUIREMENTS

The metadata verifier currently also intentionally checks important architecture guarantees including concepts such as:

`KS_MAX_SEQUENCE_WORDS 32`

`KS_MAX_SEQUENCE_CHARS 512`

presence of:

`ks_is_word_scancode`

presence of sentence evaluation;

pending-word behavior;

plain Backspace Undo.

It also ensures obsolete:

`F12`

and:

`self-test`

UI behavior does not return.

Treat these checks as deliberate regression guards.

---

# 64. CI

GitHub Actions CI:

`.github/workflows/ci.yml`

Runs on:

- pushes to `main`;
- PRs to `main`;
- manual workflow dispatch.

Build environment:

Ubuntu latest.

CI installs Zig, runs `build-native.sh`, then uploads verified Windows artifacts.

Current build artifacts are retained temporarily by GitHub Actions.

Do not add a separate inconsistent build procedure unless necessary.

---

# 65. RELEASE WORKFLOW

Release workflow:

`.github/workflows/release.yml`

Current release convention:

A push to `main` whose HEAD commit message contains:

`[release]`

triggers the Release workflow.

Example commit:

`Release KeySwitchFix 2.7.0 [release]`

Workflow then:

1. checks out full history;
2. installs pinned Zig;
3. runs release packaging;
4. reads `VERSION`;
5. creates tag:

`v<VERSION>`

6. pushes tag if required;
7. creates GitHub Release;
8. uploads Setup, Uninstaller, ZIP and SHA256 file.

---

# 66. IMPORTANT RELEASE-TAG FIX

There was a previous problem where repeated release attempts failed because:

`v2.1.1 already exists`

The workflow has since been made idempotent.

Current logic checks whether the tag already exists.

If the existing tag points to the same source tree and the release already exists:

do nothing successfully.

If the tag exists but points to a different source:

fail and require a version bump.

Do NOT revert to unconditional:

`git tag ...`

`git push origin ...`

without checking existing tags.

---

# 67. RELEASE PACKAGE CONTENT

The ZIP includes approximately:

- KeySwitchFix-Setup.exe
- KeySwitchFix-Uninstall.exe
- KeySwitchFix.exe
- SHA256SUMS.txt
- README.md
- LICENSE.txt
- third-party README
- English dictionary license
- Persian dictionary license
- wordfreq license
- wordfreq notice.

Third-party licensing files are part of the release contract.

Do not accidentally remove them.

---

# 68. CURRENT GITHUB NOTE

The repository owner has changed to:

`samaliyan`

Current canonical repository should therefore be treated as:

`samaliyan/KeySwitchFix`

However some README/changelog badge/compare links may still reference the old repository owner:

`silimore/KeySwitchFix`

This is technical debt.

A future cleanup should update stale repository links to the current canonical owner while avoiding breaking release history.

---

# 69. HISTORICAL DESIGN REQUIREMENTS FROM THE USER

The project originally required:

- global Persian ↔ English repair;
- both directions;
- tray utility;
- extremely lightweight;
- no unnecessary dependency;
- real standalone EXE;
- proper installer;
- proper uninstaller;
- desktop shortcut;
- close-to-tray behavior;
- English-only menus;
- professional public-quality presentation;
- no F12 UX;
- no useless test/self-test button;
- Undo after accidental correction;
- avoid false positives;
- support ordinary Persian words;
- offline dictionary management.

These requirements remain relevant unless explicitly changed.

---

# 70. DEVELOPMENT PRINCIPLE FOR FUTURE AI

Do not optimize only for examples the user reported.

Whenever a new example fails:

1. reproduce it;
2. identify the underlying general failure;
3. fix the general mechanism;
4. add the reported example as regression;
5. add opposite/correct-layout regression;
6. add ambiguous/mixed-language adversarial regression;
7. verify real Windows Hook path, not merely pure core unit tests.

The 2.7 Space bug is the clearest example of why passing `core_tests` alone is insufficient.

---

# 71. REQUIRED REVIEW STYLE

Historically the project was intentionally subjected to multiple strict review rounds.

When implementing a significant detection change, conduct at least three passes:

### Pass A — functionality
Does the requested case now work?

### Pass B — adversarial safety
What legitimate words/text can this new rule accidentally change?

### Pass C — integration
Does it work through the real keyboard hook, timing, SendInput, sentence history, both Persian layouts, Undo, installer and release build?

Do not give a high confidence score simply because one test case passes.

---

# 72. TESTING PHILOSOPHY

Tests must cover both:

**positive correction cases**

and:

**negative no-correction cases.**

For every new auto-correction example, ask:

"What legitimate text uses the same physical keys?"

"What happens if the user is still typing a longer word?"

"What happens at sentence start?"

"What happens mid-sentence?"

"What happens in Auto?"

"What happens with Prefer Persian?"

"What happens with Prefer English?"

"What happens after a mouse/caret movement?"

"What happens when Undo is immediately pressed?"

"What happens with Persian vs Persian Standard?"

---

# 73. PERFORMANCE REQUIREMENTS

The keyboard hook is latency-sensitive.

Do not perform:

- disk access per key;
- network access;
- expensive corpus processing;
- dynamic database queries;
- unbounded allocation;
- unbounded sentence parsing

inside the hook path.

Bloom membership and bounded scoring are intentional choices.

Any new algorithm must remain fast enough for live keyboard input.

---

# 74. MEMORY REQUIREMENTS

Current temporary history is deliberately bounded.

Do not create a permanent transcript.

Do not retain arbitrary documents in memory.

The project only needs enough local context to make a safe correction decision.

---

# 75. THREAD / HOOK SAFETY

Do not perform unsafe UI operations directly inside time-critical hook callbacks if they can be deferred.

Avoid recursion caused by injected events.

Maintain explicit state for:

- modifiers;
- suppressed keys;
- timing;
- foreground window;
- current word;
- pending word;
- sentence history;
- Undo;
- target layout.

State invalidation is part of correctness, not cleanup.

---

# 76. CURRENT IMPORTANT GLOBAL RUNTIME STATE

`src/app.c` currently contains state conceptually corresponding to:

- application instance/window;
- dashboard control handles;
- keyboard hook;
- mouse hook;
- tray icon;
- current settings;
- settings path;
- last English/Persian HKL;
- all Bloom resources;
- current word tokens;
- word overflow;
- current word target HWND/language;
- modifier states;
- smart-correction timer;
- average key interval;
- language intent/context;
- sentence history;
- pending corrected word;
- Undo record;
- diagnostics counters.

A future refactor may encapsulate this, but behavior must remain identical.

---

# 77. DIAGNOSTICS

The dashboard contains lightweight live diagnostics useful for troubleshooting.

Expected diagnostic ideas include:

- Protection is active
- Keyboard hook: Running
- Current context/layout
- Input observed
- number of keys seen
- words evaluated
- corrections made
- last activity.

Diagnostics must not persist typed content.

Version 2.7 also intentionally bounds displayed previews of corrected phrases rather than formatting arbitrarily long text.

---

# 78. KNOWN FUNDAMENTAL LIMITATIONS

Do not claim perfect detection.

Remaining unavoidable limitations include:

### True bilingual collisions

If the same physical keys form legitimate words in both languages and there is no context, automatic intent is impossible to know with certainty.

### Bloom false positives

Bloom filters are probabilistic.

The classifier reduces the impact but cannot mathematically eliminate false-positive membership.

### Windows input restrictions

Some applications restrict or reinterpret SendInput.

Elevated applications require matching integrity.

### Password control detection

Custom controls may hide their password nature from standard Win32 inspection.

### Code/mixed-language text

Users often intentionally mix Persian, English, code and names.

The algorithm should therefore remain conservative.

---

# 79. THINGS THE NEXT AI MUST NOT DO

Do NOT:

- convert project to C# simply because it is easier;
- add .NET;
- add Electron;
- add Python runtime;
- add cloud AI;
- add telemetry;
- upload typing;
- permanently log words;
- aggressively convert every dictionary collision;
- remove Undo;
- remove prefix safety;
- reduce phrase safety to simple majority language;
- reintroduce F12;
- reintroduce the removed self-test UI;
- make Space a word token;
- hard-code only one Persian Windows layout;
- lose live-corrected words before sentence commit;
- wipe beginning-of-sentence context after every phrase fix;
- remove third-party licenses from release package;
- disable `-Werror`;
- bypass failing regression tests;
- blindly overwrite release tags.

---

# 80. WHEN FIXING A BUG

Use this workflow:

```text
1. Reproduce
2. Find exact layer of failure
   - scan code?
   - Windows layout mapping?
   - dictionary?
   - prefix?
   - scoring?
   - context?
   - sentence state?
   - timer?
   - boundary?
   - input injection?
   - UI?
3. Add regression test if core-testable
4. Implement smallest general fix
5. Test correct-layout opposite case
6. Test collision case
7. Test Persian and Persian Standard
8. Test sentence/phrase interaction
9. Test Undo
10. Run full build-native.sh
11. Run metadata/PE verification
12. Review diff for privacy/performance regressions
```

---

# 81. WHEN ADDING A NEW DETECTION FEATURE

A new detection feature is acceptable only if:

- it meaningfully improves real correction coverage;
- it remains offline;
- it is deterministic;
- it is bounded;
- it doesn't cause obvious typing latency;
- it doesn't make false positives substantially worse;
- it has positive tests;
- it has negative tests;
- it respects bilingual ambiguity;
- it doesn't break Undo;
- it doesn't break both Persian layouts.

---

# 82. WHEN MAKING A RELEASE

Use semantic versioning.

Before release:

```text
1. Finish code
2. Add/update tests
3. Update CHANGELOG.md
4. Bump VERSION
5. Synchronize APP_VERSION
6. Synchronize app.rc version metadata
7. Synchronize installer.rc metadata
8. Synchronize uninstaller.rc metadata
9. Run tests/verify_metadata.py
10. Run build-native.sh
11. Run package-release.sh
12. Verify SHA256SUMS
13. Verify ZIP
14. Commit with [release]
15. Push main
16. Let release.yml create/reuse safe tag
17. Verify GitHub Release artifacts
```

Do not reuse an existing version number for different source code.

---

# 83. CURRENT RELEASE VERSION SNAPSHOT

At the time this handoff was created:

Version:

`2.7.0`

Important 2.7 achievements:

- fixed real Hook Space-boundary failure;
- explicit physical word-key allowlist;
- current-sentence monitoring;
- 32-word/512-character safety bounds;
- pending-word state;
- live correction preserved into sentence history;
- retrospective phrase corrections preserve earlier sentence context;
- supports alternate Persian physical key behavior;
- bounded diagnostics previews;
- three-cycle strict implementation review.

Any later AI must first check whether a newer version exists before assuming 2.7.0 is still current.

---

# 84. NEXT LOGICAL DEVELOPMENT AREAS

Do not automatically implement these. They are reasonable future directions to evaluate when requested.

### Detection quality

Continue improving hard Persian/English collisions without becoming aggressive.

### Real application compatibility

Test against:

- Chrome
- Edge
- Firefox
- Notepad
- Word
- Outlook
- Teams
- Telegram
- VS Code
- Revit text fields where practical.

### Better test harness

Separate:

- pure core tests;
- real keyboard-hook integration tests;
- simulated Windows layout tests.

The 2.7 incident showed the need for runtime-path tests.

### Repository cleanup

Replace remaining stale:

`silimore/KeySwitchFix`

links with:

`samaliyan/KeySwitchFix`

where safe.

### Signing

Public releases are currently unsigned, so SmartScreen may warn users.

A future code-signing strategy would improve professional distribution but should not add runtime dependencies.

---

# 85. HOW TO ANSWER THE USER ABOUT CHANGES

The user prefers concrete, practical answers.

When making code changes:

do not only explain theory.

Provide:

- exact files changed;
- what was wrong;
- how it was fixed;
- how it was tested;
- what could still fail;
- exact commands needed;
- final result.

If files need to be generated, generate complete files rather than vague snippets whenever practical.

---

# 86. IMPORTANT: DO NOT CLAIM SUCCESS WITHOUT TESTING

A particularly important historical lesson:

Version 2.6's core phrase evaluator could successfully convert:

`nv clhkd ;i`

to:

`در زمانی که`

in isolated tests.

But the actual Windows keyboard Hook never invoked the correct path because Space was mistakenly classified as a word token.

Therefore:

**Unit-test success does not prove real runtime success.**

For keyboard pipeline changes, inspect the full route:

```text
Physical key
↓
WH_KEYBOARD_LL
↓
scan code filtering
↓
foreground layout
↓
ToUnicodeEx mapping
↓
word buffer
↓
boundary handling
↓
pending word
↓
sentence history
↓
classifier
↓
SendInput
↓
target layout switch
↓
Undo state
```

---

# 87. MINIMUM ACCEPTANCE TEST BEFORE SHIPPING A DETECTION CHANGE

The following should remain correct:

```text
اثممخ          -> hello
sghl           -> سلام
;jhf           -> کتاب
;l;            -> کمک
fdk            -> بین
jh             -> تا
;k             -> کن
mdnh           -> پیدا
nv clhkd ;i    -> در زمانی که
```

Correct-layout safety:

```text
hello          -> hello
of             -> of (Auto/no Persian context)
leg            -> leg (Auto/no Persian context)
how            -> how
really         -> really

کمک            -> کمک
کن              -> کن
بین             -> بین
تا              -> تا
پیدا            -> پیدا
ایا             -> ایا
مثل             -> مثل when correctly typed Persian
بهسازی          -> بهسازی
```

Mixed text:

```text
سلام test
```

must not be aggressively flattened.

Collision behavior:

```text
of  <-> خب
leg <-> مثل
```

must respect context/preference.

Undo:

After an automatic word/phrase correction, plain Backspace within roughly 15 seconds should restore the exact original.

---

# 88. DEFINITION OF DONE

A KeySwitchFix change is only complete when:

- requested behavior works;
- existing behavior still works;
- false-positive risks were reviewed;
- tests were added/updated;
- strict C build passes;
- Windows binaries build;
- metadata verification passes;
- PE/resource verification passes;
- installer/uninstaller remain valid;
- no new networking or privacy issues exist;
- no unnecessary runtime dependency was introduced;
- documentation/changelog is updated when appropriate.

---

# 89. SOURCE OF TRUTH PRIORITY

If information conflicts, use this order:

```text
1. User's newest explicit request
2. Current repository implementation
3. Current regression tests
4. Current architecture documentation
5. CHANGELOG / strict reviews
6. This handoff document
7. Old conversation history
```

Never overwrite newer working project behavior merely because this snapshot is older.

---

# 90. FIRST ACTION FOR A FUTURE AI

If repository access exists, start with:

```bash
git clone https://github.com/samaliyan/KeySwitchFix.git
cd KeySwitchFix
git status
git log --oneline -10
cat VERSION
```

Then inspect:

```text
README.md
CHANGELOG.md
docs/ARCHITECTURE.md
docs/STRICT_REVIEW_2.7.0.md
src/core.h
src/core.c
src/app.c
src/installer.c
tests/core_tests.c
tests/verify_metadata.py
build-native.sh
.github/workflows/ci.yml
.github/workflows/release.yml
```

Before writing code, determine what version and commit are actually current.

---

# 91. IF RECONSTRUCTING THE PROJECT WITHOUT THE REPOSITORY

If the repository is completely lost:

1. Create the directory structure described above.
2. Implement `src/core.h` contracts first.
3. Implement deterministic candidate generation and Bloom support in `core.c`.
4. Write `core_tests.c`.
5. Build the Win32 hook/tray runtime in `app.c`.
6. Embed Bloom resources using `app.rc`.
7. Implement settings and exclusions.
8. Implement live/idle/boundary evaluation.
9. Implement per-window context.
10. Implement 32-word/512-char sentence history.
11. Implement pending-word state.
12. Implement retrospective phrase correction.
13. Implement marked SendInput replacement.
14. Implement layout switching.
15. Implement 15-second plain-Backspace Undo.
16. Implement installer/uninstaller.
17. Implement resource verification.
18. Implement CI.
19. Implement release packaging.
20. Run the entire regression matrix above.

Do not build only the visual shell.

The detection/safety behavior is the project's core value.

---

# 92. FINAL PRODUCT DESCRIPTION

KeySwitchFix is:

> A lightweight, completely offline native Windows utility that watches physical keyboard input, determines when Persian text was typed using an English layout or English text using a Persian layout, safely reconstructs the intended word or phrase using offline dictionaries and bounded context, replaces only sufficiently confident mistakes, switches the target application's keyboard layout, and allows immediate one-Backspace Undo.

The core differentiators are:

- Persian ↔ English bidirectional repair;
- physical-key interpretation;
- support for both Windows Persian layouts;
- immediate high-confidence correction;
- adaptive short-word handling;
- prefix protection;
- contextual collision handling;
- bounded current-sentence retrospective correction;
- plain Backspace Undo;
- zero cloud dependency;
- no typed-text logging;
- native lightweight Windows implementation.

---

# 93. CURRENT PROJECT IDENTITY

Product name:

**KeySwitchFix**

Canonical executable names:

```text
KeySwitchFix.exe
KeySwitchFix-Setup.exe
KeySwitchFix-Uninstall.exe
```

Canonical current repository:

```text
samaliyan/KeySwitchFix
```

Current snapshot version:

```text
2.7.0
```

Do not rename the product or binaries without an explicit user request.

---

# END OF MASTER HANDOFF

When receiving this document in a future conversation, do not ask the user to explain KeySwitchFix from the beginning.

Treat this document as project context, then inspect the newest repository state and continue from there.
