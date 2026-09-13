# KeySwitchFix 3.0.0 — review record

Two independent adversarial reviews were run on the 3.0 changes before
release, each against the full diff from 2.9.0 and the sources; no Windows
toolchain was available, so the Win32 sources were type-checked with gcc and
clang against a stub header under `-Wall -Wextra -Werror`, and the
platform-free modules (`core.c`, `spell.c`, `typing.c`) were exercised by the
native tests, ASan/UBSan builds and a 300-year Jalali round trip.

## The `staدیشقی` / `quiزن` defect (2.9.1 fix, carried into 3.0)

Reproduced with the real dictionaries: typed on the Persian layout,
`standard` and `quick` are repaired live after the third key (`سفش`, `ضاه`
are not Persian prefixes), and the application is asked to switch to
English. Two causes made the rest of the word render Persian:

1. The layout was read from, and the switch sent to, the foreground
   window's thread. For Store/UWP applications that is ApplicationFrameHost,
   not the application; the frame's layout changed, the app kept typing
   Persian, and the hook believed the switch had happened.
2. A live correction discarded its own keys, so the tail (`dard`, `ck`) was
   judged as a separate word.

Fixes: the real input thread is resolved (focused window, or the UWP
CoreWindow); the switch is verified on that thread and re-posted to sibling
windows; while a requested switch is pending and keys still arrive in the
old layout, the hook types the requested layout's characters itself (letters,
digits, punctuation, ZWNJ), so the wrong alphabet never reaches the screen;
a live correction's word is resumed when typing continues. Review rounds
added: per-key key-up suppression, one 50 ms synchronous send inside the
hook, request closed by manual switch / click / navigation / whole-word
deletion, a 10 s lifetime cap, the "ignored" diagnostic only after 250 ms,
numeric keypad excluded, physical Shift state, exclusions matching both the
host and the focused process.

## Typing helpers — findings and resolutions

| # | Finding | Resolution |
| --- | --- | --- |
| 1 | `?`/`!` shaped by a stale context; a deliberate `؟` after an English word was overruled | context recorded for every non-word key, corrected language after corrections, 5 s window, cleared by any manual switch |
| 2 | lone `i` → `I` fired in `for i in`, `i = 0` | prose only: word after a Space, previous English word within 5 s, Space boundary |
| 3 | template shortcuts were real Persian words | non-word shortcuts, documented rule |
| 4 | `ۀ` (Shift+G) rewritten to `ه` | mapping removed |
| 5 | capitalised first word never spell-checked | checked lower-case, capital restored; undo protects both forms |
| 6 | file I/O inside the hook (snippets, stats) | snippet reload on a 2 s timer, stats saved via a posted message; 256 K cap |
| 7 | clipboard: non-text formats lost, races, modifiers, terminals | all HGLOBAL formats snapshotted and restored, retries, sequence check with the clipboard open, all modifiers released with a deadline, refused in terminals/remote/excluded apps |
| 8 | URL digits converted inside Persian text | per-token shaping; script decided per word |
| 9 | tile labels overlapped the card | widths from the tile macro |
| 10 | `?` in formulas armed capitalisation | only after a real English word |
| 11 | remote-desktop clients reshaped remote sessions | added to the suppression list |
| 12 | double Space dropped the pending capital | kept |
| 13 | stale tracked Ctrl turned `Ctrl+S` into a letter | physical modifiers checked |
| 14 | `\n` in a chat snippet sends the message | documented; template has none |
| 15 | stats lost at shutdown | `WM_QUERYENDSESSION`/`WM_ENDSESSION` |
| 16 | abbreviation list gaps and common words | revised |
| 17 | clipped combobox strings | shortened |
| 18 | `wcslen` on a foreign HGLOBAL | bounded by `GlobalSize` |

Second pass: the prose-context flag for the lone `i` was never set (fixed),
an oversized snippets file was re-read every two seconds (timestamp
remembered), undoing a capitalised spelling fix did not protect the
lower-case form (both forms ignored), the restore race was narrowed to the
open clipboard, and the wait before restoring the clipboard was raised to
900 ms for slow editors.

## Known limits

- The hook cannot see what an application really rendered; the translation
  fallback trusts the layout of the focused thread. If an application types
  with a layout that belongs to yet another thread, the diagnostics line
  (`… did not switch …`) will say so — report the application.
- Sentence capitalisation cannot know that a period ended an abbreviation
  the list does not contain; Backspace and retype is the remedy, and the
  helper can be switched off.
- The clean-up hotkey depends on the application honouring Ctrl+C / Ctrl+V.
