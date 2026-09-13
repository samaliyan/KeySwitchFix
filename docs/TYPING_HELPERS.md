# Typing helpers (3.0)

KeySwitchFix 3.0 grows from a layout-and-spelling corrector into a small
typing assistant for Persian and English. Every helper is offline, runs inside
the same keyboard hook, costs microseconds per key, and can be switched off
individually on the dashboard (**Correction settings**) or from the tray
menu (**Typing helpers**).

None of the helpers touch code editors, terminals, remote-desktop clients,
password fields, or applications on the **Excluded apps** list — the same
places spelling correction already avoids — so `for i in range` in VS Code
and `123` in a terminal stay exactly as typed.

## Digits

Setting: **Digits** — *As the layout types them* / *Follow the layout* (default) /
*Always Persian* / *Always English*.

The legacy Windows Persian layout types ASCII digits (`123`) while the Persian
(Standard) layout types Persian digits (`۱۲۳`); most people want Persian digits
in Persian text and ASCII digits in English text regardless of which layout
they installed. *Follow the layout* gives exactly that: a digit key on the
Persian layout produces `۱۲۳`, on the English layout `123`. The two *Always*
options force one form everywhere; *As the layout types them* disables the
helper. The numeric keypad is never touched.

## Persian punctuation

Setting: **Persian ؟ ، ؛ after Persian words** (default on).

`?`, `,` and `;` typed after a Persian word become `؟`, `،` and `؛`; the Persian
marks typed after an English word become `?`, `,` and `;`. "After" means the
word being typed, or the last word finished in the same window within five
seconds. A manual layout switch (Alt+Shift, Win+Space, a click on the language
bar) clears that context, so a user who deliberately switches to Persian to
type `؟` after an English word is never overruled.

## Persian letters

Setting: **Persian letters: ي ك → ی ک while typing** (default on).

The legacy Persian layout types the Arabic yeh (`ي`, U+064A) and kaf (`ك`,
U+0643); Persian text uses `ی` (U+06CC) and `ک` (U+06A9), and the wrong forms
break sorting, searching, and word joining in many applications. The helper
types the Persian forms instead. `ۀ` (Shift+G on the standard layout) and every
other letter are left alone.

## Capitalise English sentences

Setting: **Capitalise English sentences** (default on).

The first letter after a sentence end becomes a capital: `done. next` → `done.
Next`, and `i think` → `I think`. The rule is deliberately narrow:

- a period counts as a sentence end only after a real English word — two or
  more letters, not an abbreviation (`dr.`, `e.g.`, `etc.`, `www.`, `jan.`,
  file extensions) and not a number (`3.14`);
- `!` and `?` count after a real English word (`Really? yes` → `Really? Yes`),
  not on their own (`a ? b`);
- the capital is applied only when the next word starts with a plain letter on
  the English layout — Shift or Caps Lock already held means the user chose
  the case;
- the lone `i` becomes `I` only in prose: after a Space, following an English
  word typed within five seconds, before a Space — never in `for i in`,
  `i = 0` or `j.i.`;
- a word the hook capitalised is still spell-checked as if lower-case, so
  `teh` at a sentence start still becomes `The`, and its replacement keeps the
  capital.

One plain Backspace right after `I` restores `i`. A capitalised first letter is
simply deleted and retyped like any other letter.

## Snippets

Setting: **Expand shortcuts (Space / Enter / Tab)** (default on) and the
**Edit snippets…** button (also on the tray menu).

Snippets live in `%LOCALAPPDATA%\KeySwitchFix\snippets.txt`, one per line:

```
shortcut = text
tarikh = {jdate}
brgds = Best regards,
tsh = با تشکر و احترام
```

Type the shortcut as a word and press Space, Enter or Tab: the shortcut is
replaced by the text and the key you pressed follows it. Backspace right after
an expansion brings the shortcut back (the ordinary Undo). Shortcuts may be
Persian or English, up to 32 characters without spaces; choose ones that are
not real words, or they will expand every time you type them. The file is
re-read within two seconds of being saved (UTF-8 or UTF-16 as Notepad saves
it), holds up to 256 snippets of up to 400 characters, and `#` starts a
comment.

Macros inside the text are expanded at the moment of typing:

| Macro | Result | Macro | Result |
| --- | --- | --- | --- |
| `{jdate}` | `۱۴۰۵/۰۶/۲۱` | `{date}` | `2026-09-12` |
| `{jdate:en}` | `1405/06/21` | `{date:long}` | `12 September 2026` |
| `{jdate:long}` | `۲۱ شهریور ۱۴۰۵` | `{weekday}` | `Saturday` |
| `{jweekday}` | `شنبه` | `{time}` | `14:05` |
| `{jyear}` `{jmonth}` `{jday}` | `۱۴۰۵` `شهریور` `۲۱` | `{time:fa}` | `۱۴:۰۵` |
| `{year}` `{month}` `{day}` `{hour}` `{minute}` | Gregorian parts | `\n`, `{n}`, `{t}` | Enter, Enter, Tab |

The Jalali date is computed with the 33-year-cycle algorithm (the same one
`jalaali-js` and most Persian calendar libraries use), verified against known
dates and by a full round trip of every day from 1900 to 2200 in the tests.
`\n` is replayed as the Enter key: in chat applications that sends the
message, so keep multi-line snippets for editors and e-mail.

## Clean up selected text

Hotkey: `Ctrl + Win + X`, in any application.

Select text — a paragraph pasted from a web page, an old document, a message
— and press the hotkey. The selection is replaced by a cleaned copy:

- Arabic `ي` `ك` → Persian `ی` `ک`, Arabic-Indic digits → Persian digits;
- digits follow the **Digits** setting for the text's own script
  (`123` → `۱۲۳` in a Persian paragraph with *Follow the layout*);
- `?` `,` `;` → `؟` `،` `؛` in a Persian paragraph when the punctuation helper
  is on.

Words that contain Latin letters — a URL, an e-mail address, a product code —
keep their digits and marks even inside Persian text. The clean-up works
through the clipboard (copy, clean, paste); your own clipboard content, in
every text-like format, is put back afterwards unless something new was copied
meanwhile. The hotkey does nothing in terminals, remote sessions, password
managers and excluded apps, where an injected Ctrl+C could mean something
else.

## Statistics

The dashboard's **Live diagnostics and statistics** card shows layout and
spelling fixes today, all-time fixes, keys today, active days, an estimate of
time saved (four seconds per fix), and the words you mistype most often.
Counters are saved to `stats.ini` in the settings folder every ten minutes, at
exit and at shutdown; the most-corrected words live only in memory and are
never written to disk. See [Privacy design](PRIVACY.md).

## Settings file

```
[Typing]
Digits=1            ; 0 off, 1 follow the layout, 2 always Persian, 3 always English
Punctuation=1
PersianLetters=1
Capitalize=1
Snippets=1
```
