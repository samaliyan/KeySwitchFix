# Writing memory and vocabulary packs (3.1)

## Writing memory

Setting: **Learn my writing (kept on this PC)** on the dashboard, or
**Writing memory → Learn my writing** in the tray menu. Off by default.

When it is on, KeySwitchFix learns two things from the way you type:

**Your repairs.** When you type a word, notice a mistake, and fix it by hand,
the pair is remembered. You can fix it any of the usual ways:

- delete the last letters and retype them (`عسیسم` ← ← ← `زیزم`);
- delete the whole word and type it again;
- press Space, then Backspace back into the word, and fix it.

The second time you make the same repair, it is learned; from then on the
typo is replaced as soon as you finish the word (Space, Enter, or a period).
If the typo is itself a real word (`then` for `than`), the repair is needed a
third time, and it is applied only while you repair that word more often than
you leave it as it is. One Backspace right after an automatic repair restores
what you typed and makes the memory unlearn that repair.

Only real repairs are learned: the result must be a real word (or a word you
type often), the two spellings must be at most two letters apart (one for
words of four letters or fewer), and an unfinished word (`عزی` → `عزیزم`), a
dropped ending (`کتابها` → `کتاب`) or a different word are not repairs.

**Your words.** Every word you finish with Space is counted. A word you have
typed three times — a name, a brand, jargon, your own spelling — becomes a
known word: it is never "corrected", it is repaired to when you mistype it
(`سیاوس` → `سیاوش`), and it counts for layout repair (typed on the wrong
layout, it is switched). Dictionary words you use often rank higher, so an
ambiguous typo resolves toward the word you actually write.

### What is stored, and where

`%LOCALAPPDATA%\KeySwitchFix\writing-memory.txt`, plain UTF-8 text, on this
computer only:

```
[fixes]
عسیسم	عزیزم	3
[words]
عزیزم	57
```

- Only words typed **at least twice** are written to the file; words seen
  once stay in memory until the program exits.
- Nothing is learned in password fields Windows can identify, in excluded
  apps, in code editors, terminals and remote-desktop windows, or with the
  setting off. Words are learned only when they end with **Space**: a
  password in a web form that Windows cannot identify ends with Enter or Tab
  and is therefore never recorded.
- Only letters-only words of 2–32 letters are kept; nothing with digits or
  symbols.
- The file holds at most 4,096 words and 512 repairs. When the word table is
  full, the least used eighth is forgotten (the least recent first among
  equal counts) and the counts of the rest are reduced by about a quarter:
  a little less for words you typed since the previous clean-up (a word you
  use stays known), a little more, and at least one, for words you did not,
  so words you stopped using fade out.
  Repairs are forgotten least used first.
- **Writing memory → Open writing memory…** opens the file in Notepad. Delete
  any line you like and save: the change is picked up within two seconds
  (your edit wins over anything learned in the meantime).
- **Writing memory → Forget everything learned…** deletes the file.
- Turning the setting off stops learning and stops using the memory; the
  file stays until you delete it.

## Vocabulary packs

Setting: **IT & computing terms** (on by default), also in the tray under
**Typing helpers**.

About 1,100 English and 320 Persian terms from IT, networking, databases,
security, development and operations: `kubernetes`, `nginx`, `tablespace`,
`rman`, `dataguard`, `failover`, `localhost`, `middleware`, `کانفیگ`,
`دیتابیس`, `فایروال`, `سرور`, `بکاپ`, `کوئری`, `دیپلوی`, ...

A pack word:

- is never treated as a spelling mistake;
- is a spelling candidate, so `kubernets` becomes `kubernetes` and
  `tablspace` becomes `tablespace`;
- counts as a word for layout repair: `kubectl` typed on the Persian layout
  becomes `kubectl`, and while you type `kuber…` on the English layout it is
  protected as the beginning of a known word.

The lists live in `tools/domains/*.txt` (one word per line, easy to extend);
`tools/generate_domain_words.py` compiles them into `src/domain_words.inc`.
Words whose wrong-layout reading is a common word of the other language
(`sdk` ↔ `سین`) and two-word phrases written together (`highavailability`,
`هوشمصنوعی`) are left out on purpose.
