# Review of 4.4.0

4.4 implements the proposals that followed the review of 4.3: seven
features and the seven technical points that review left open.

## What was verified, and how

| Change | Check |
| --- | --- |
| Fix at end of word only; `name.exe:slow` | Hook simulation: nothing changes before Space, the repair comes at Space. |
| `name.exe:spelling` | Hook simulation: `recieve` stays, `sghl` is still repaired; a plain name still excludes. |
| Terminal prompts | Hook simulation: the first word of a line in `WindowsTerminal.exe` is untouched until Space. |
| Never change this word; My words | Hook simulation: `col` (taken as `زخم`) is kept after one call and converted again after removal; `ks_vocab_forget` on an empty, a part-filled and a full ring. |
| Convert selection | The conversion function both ways and on text without letters. The clipboard round trip is the clean-up's, already covered by review. |
| English pairs | `tests/spell_real_tests.c`: pair scores in order, a valid word unchanged by context, `thnak you` → `thank`. Measured: 82.1 % → 87.2 % fixed with the true word before; a random word before changes nothing. |
| Update check | The version comparison (later, same, older, pre-release, malformed, absurd). |
| Regenerated frequent-word Blooms and priors | Reproduced from the pinned dictionaries; every corpus threshold holds; CI compares them at every build. |
| Word-list rates (80,000 English, 27,143 Persian words) | Unchanged from 4.3: 450 and 164 correct words changed, 121 and 163 wrong-layout words not repaired. |

## Not verified here

- **The Windows build and everything that needs Windows**: the WinHTTP
  request of the update check, the silent install, the new dashboard
  controls (their position and tab order), the `Ctrl + Win + Z` hotkey and
  the UTF-16 conversion of `settings.ini` are compiled and checked by
  GitHub Actions but were not run on a Windows machine.
- **The new release workflow** runs for the first time with this release.

## Left as it is

- The clipboard snapshot and the settings writes still run on the thread
  that owns the hooks. A hung clipboard owner is now detected first; moving
  the hooks to a thread of their own would touch every shared variable of
  the engine and was not attempted without a Windows machine to test on.
- `ToUnicodeEx` is still called several times per key; only the process
  queries were cached.
- The update check announces a version; it does not install it.
