# Language pairs and language packs (4.0)

KeySwitchFix repairs words typed on the wrong keyboard layout between **two
languages you choose**. English and Persian are built into the program;
every other language is a *language pack*, a `.kslang` file.

## Choosing the two languages

1. Open the dashboard (tray icon, or the desktop shortcut).
2. On the **Correction** page, pick the first and the second language under
   **Languages**. Picking the language the other list already shows swaps
   the two.
3. Install the keyboard of each language in Windows:
   **Settings → Time & language → Language & region → (language) → Language
   options → Add a keyboard**.

The new pair applies at once. The status card at the top of the dashboard
names a keyboard that is missing, and the **Statistics** page shows which of
the two languages the keyboard you are typing with belongs to.

**Writing language** (the list under **Languages**, and the tray menu) uses
the names of your pair: *Prefer German for collisions* and so on.

## Available languages

| Code | Language | Notes |
| --- | --- | --- |
| `en` | English | built in; spelling, capitalisation, IT vocabulary |
| `fa` | Persian | built in; spelling, Persian digits, punctuation and letters, half-space, IT vocabulary |
| `ar` | Arabic | |
| `bg` | Bulgarian | |
| `de` | German | |
| `el` | Greek | |
| `es` | Spanish | |
| `fr` | French | apostrophes inside words (`l'eau`) |
| `he` | Hebrew | niqqud is ignored |
| `it` | Italian | apostrophes inside words |
| `nl` | Dutch | |
| `pl` | Polish | |
| `pt` | Portuguese | |
| `ru` | Russian | |
| `tr` | Turkish | |
| `uk` | Ukrainian | |

Each pack holds up to 120,000 words of [wordfreq 3.1.1](https://github.com/rspeer/wordfreq)
(the 20,000 and 2,000 most frequent marked as such), the beginnings of those
words, and the language's one- and two-letter words. Like the built-in
dictionaries they are Bloom filters: compact, and apart from the short
words they hold no text.

What works for every pair: repairing words and phrases typed on the wrong
layout (while typing and at the end of a word), switching the application's
layout, typing the keys for an application that switches late, one
Backspace to undo, snippets, the writing memory, and `Ctrl + Win + X`. What
stays with English and Persian: spelling correction, the IT vocabulary,
English capitalisation and the Persian helpers; with a pair that lacks
them, those settings are greyed out.

## Which keyboard belongs to which language

Windows files every keyboard under a language. A keyboard belongs to a
language of your pair when it is filed under that language and types that
language's alphabet (a German keyboard under German). English and Persian
keep the rules of earlier versions: a Latin keyboard filed under English or
Persian is English, an Arabic-script keyboard filed under Persian or English
is Persian. A non-Latin keyboard filed under English (a Russian keyboard
added to the English language) belongs to the one language of your pair
that writes its alphabet. Any other keyboard is *Unsupported*: nothing is
corrected while it is active.

## Where packs are found

- `languages\` next to `KeySwitchFix.exe`: Setup installs the packs there,
  and the release zip has the same folder for a copy run without Setup.
- `%LOCALAPPDATA%\KeySwitchFix\languages`: your own packs.
  **Language packs…** on the **Correction** page creates and opens this
  folder; a pack copied there appears the next time you open one of the
  lists. A pack must be named after its code (`ka.kslang`); copies under
  other names are ignored.

When the same language is in both places, the one next to the program wins.
Setup replaces the packs next to the program on every upgrade, so keep your
own packs in the data folder. Uninstalling with your data removed (or with
`/purge`) deletes the packs in the data folder too.
If the pack of a chosen language is missing or damaged, KeySwitchFix uses
English (or Persian) instead, says so on the dashboard, and keeps your
choice: it is used again as soon as the pack is back.

## Building a pack

The packs of a release are built by GitHub Actions. To build one yourself
you need Python 3 and, for the languages above, `pip install wordfreq==3.1.1`:

```
python3 tools/build_language_pack.py --languages de ru --out my-packs
```

For a language wordfreq does not cover, give a word list (UTF-8, one word
per line, most frequent first, at least 5,000 words), the language's names,
its Windows primary language id (see Microsoft's *Language Identifier
Constants*: Georgian is `0x37`) and its flags:

```
python3 tools/build_language_pack.py --code ka --word-list georgian.txt \
    --english-name Georgian --native-name ქართული --langid 0x37 --flags 0 --out my-packs
```

| Flag | Meaning |
| --- | --- |
| `0x0001` | the language has upper and lower case: words are compared in lower case |
| `0x0004` | one apostrophe may stand inside a word (`l'eau`) |
| `0x0008` | Arabic-script diacritics are ignored |
| `0x0020` | Hebrew points (niqqud) are ignored |
| `0x0040` | Latin alphabet (abbreviations such as `cfg` are recognised) |

Copy the `.kslang` file into the folder that **Language packs…** opens.
To ship a language with the program, add it to `LANGUAGES` in
`tools/build_language_pack.py`; `build-native.sh` builds every listed
language and Setup installs them.

## The file format

All numbers are little-endian.

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | `KSLP` |
| 4 | 4 | format version, `1` |
| 8 | 8 | language code, ASCII `a-z 0-9 -`, 2–7 characters, zero-padded |
| 16 | 4 | flags (above) |
| 20 | 8 | up to four Windows primary language ids (u16, zero = unused) |
| 28 | 4 | model: `0` (only English and Persian have models; a pack's value is ignored) |
| 32 | 48 | English name, UTF-8, zero-padded |
| 80 | 48 | native name, UTF-8, zero-padded |
| 128 | 4 | number of sections (at most 16) |
| 132 | 12 × n | sections: type, offset from the start of the file, size |

Section types: `1` words, `2` common words, `3` frequent words, `4` word
beginnings, `5` beginnings of common words (each a `KSWB` Bloom filter, the
format of `resources/*.bloom`), `6` one- and two-letter words (UTF-8, one per
line). Unknown section types are skipped, so newer packs stay readable. A
pack needs at least sections 1 and 4; everything is bounds-checked, and a
damaged file is refused as a whole.

Words are stored in their *lookup form*: lower case for cased languages
(with the program's own case table, not the system's), marks removed for
Arabic and Hebrew, and only when the whole word is letters of one of the
supported alphabets (with one inner apostrophe where allowed). The builder
and the program apply exactly the same rules; `tests/verify_language_rules.py`
checks that for every character.

Setup carries all packs in one resource, `KSLB`: a count, then for each pack
its file name (24 bytes), offset and size. Setup accepts only names of the
form `code.kslang` and writes the packs into `languages\`, removing packs an
earlier version installed that the new one no longer has.

## Known limits

- Dead keys (`^` `´` on German and French keyboards) cannot be followed: the
  word they begin is left as typed. The same holds for a key that types two
  characters (the Arabic lam-alef, on the key of English `b`): a word that
  uses it is left alone in that pair.
- A digit is never part of a word. With a keyboard that has letters on the
  digit row (French AZERTY), a French word with `é` or `à` typed while the
  English keyboard is active cannot be repaired: those keys are digits there.
- With Persian and Arabic as the pair, an Arabic keyboard that Windows files
  under English counts as Persian (the rule of earlier versions); file it
  under Arabic.
- **Writing language** stores the preferred language by position; when you
  swap the two languages, the preference moves with its language.
- Where the full-stop key types a letter in the other language (Russian,
  Ukrainian and Bulgarian `ю`), that key is part of words in that pair, so
  English capitalisation does not start after a full stop there.
- Two languages with the same alphabet and similar words (Russian and
  Ukrainian, Spanish and Portuguese) give the engine little to go on: many
  words exist in both, and it then waits for the sentence to decide or
  leaves them alone.
- Packs are built from word frequencies, not from curated dictionaries: rare
  but real words may be missing, and frequent misspellings may be present.
