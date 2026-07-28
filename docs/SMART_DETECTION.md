# Smart detection review

KeySwitchFix 2.3.0 was designed through three explicit
design → adversarial review → revision cycles. The target failures were:

- `;l;` → `کمک`
- `fdk` → `بین`
- `jh` → `تا`
- `of` → `خب`
- `mdnh` → `پیدا`

## Round 1 — Lower the minimum length

**Proposal:** Change Balanced mode from four keys to three.

**Strict review:** This fixes `کمک`, `بین`, and `پیدا`, but it cannot recognize
two-key words because the base dictionary intentionally excludes them. Lowering
the threshold without new evidence also increases false positives.

**Score: 5.5/10**

## Round 2 — Exact short-word lists

**Proposal:** Add compact exact lists for common two-key Persian and English
words, then correct them at a delimiter or after the adaptive idle timer.

**Strict review:** This safely fixes unambiguous pairs such as `jh` → `تا` and
`;k` → `کن`. It cannot safely choose between two simultaneously valid words.
For example, English `of` and Persian `خب` use the same physical keys.

**Score: 8.1/10**

## Round 3 — Evidence scoring with sentence context

**Proposal:** Score every candidate using all locally available evidence:

- candidate membership in the opposite-language dictionary;
- current-layout membership;
- word length;
- active-language proper-prefix status;
- live, idle, or word-boundary phase;
- recently confirmed sentence language;
- optional explicit preference for irreducibly ambiguous short words.

Three-key and longer high-confidence mistakes are corrected immediately.
Two-key words wait only for an adaptive typing pause, so a longer word can
continue uninterrupted. Recent confirmed words and corrections create a
90-second, per-window language context. The optional **Prefer Persian** or
**Prefer English** mode resolves the first ambiguous short word when no prior
sentence context exists.

**Strict review:** The method is deterministic, offline, testable, and avoids
rewriting a genuine English `of` in Auto mode. No classifier can infer whether
an isolated first token `of` means English `of` or Persian `خب` from those two
keystrokes alone; the preference setting is an explicit and reversible solution
for that information-theoretic ambiguity.

**Score: 9.2/10**

## Regression expectations

| Physical keys | Intended word | Balanced result |
|---|---|---|
| `;l;` | `کمک` | immediate |
| `fdk` | `بین` | immediate |
| `mdnh` | `پیدا` | immediate |
| `jh` | `تا` | adaptive pause or boundary |
| `;k` | `کن` | adaptive pause or boundary |
| `of` | `خب` | Persian context or Prefer Persian |
| `of` | English `of` | unchanged in Auto/English context |
| `بهسازی` | `بهسازی` | unchanged |

The native core test suite covers every row and the prior `بهسازی` regression.

## 2.3.1 orthography pass

The dictionary stores the canonical `آیا`, while many users physically type
`ایا` without Shift. Detection now performs an additional canonical lookup when
a Persian candidate begins with `ا`. Only membership lookup uses `آ`; the
replacement preserves the user's original `ایا` spelling. Regression tests
also cover `اقا`, `اب`, and the no-reverse-conversion case for correct Persian.

## 2.4.0 common-word and collision pass

The base spell-checking dictionaries are supplemented by a filtered set of
4,897 high-frequency spoken-English entries and 616 common Persian entries.
Malformed Persian source fragments are rejected before the compact Bloom files
are generated. Regression coverage includes previously absent `really` and
`مرسی`.

`مثل` and English `leg` are a genuine three-key collision: both are complete
words and `leg` is also a prefix of longer English words. Auto mode therefore
uses preserved per-window sentence intent and waits for the adaptive pause when
necessary. **Prefer Persian for collisions** is an explicit stronger signal and
converts `leg` to `مثل` on the third key. Genuine English `leg` remains unchanged
in Auto mode without Persian context and in Prefer English mode.

The writing-language selector is now available directly from the tray menu and
is no longer mislabeled as a setting only for short words.

## 2.5.0 bilingual context pass

The common layer now contains the top 20,000 eligible English and top 20,000
eligible Persian entries from wordfreq 3.1.1, plus 2,000-word frequent tiers.
The base Hunspell dictionaries remain available underneath, for an effective
union of 159,852 offline spellings. Common-prefix guards are generated from the
same data so a thinking pause in `بهسا...` cannot trigger `fish`.

The evaluator separates two fundamentally different cases:

1. If only the opposite-layout candidate is known, it is objective layout
   evidence. Context and a tray preference cannot suppress it. This fixes
   Persian-layout `اثممخ` to English `hello`.
2. If both candidates are words, only then are weighted document language,
   sentence position, normalized cross-language frequency, typing phase, and
   explicit preference considered.

Frequency priors come from the same multilingual estimator for both languages;
mixing unlike English and Persian corpora is deliberately avoided. A full
prior can help at the beginning of a sentence, while mid-sentence context is
weighted more heavily. Exact two-key collisions such as `of`/`خب` are never
guessed from corpus frequency alone.

The application preserves weighted language evidence between sentences in the
same window for 90 seconds. It resets sentence position after `.`, `!`, `?`, or
Enter, but the surrounding document still informs the new sentence. Navigation
or switching to another window clears unsafe context.

Finally, live character mapping uses the actual installed Windows English and
Persian layout through `ToUnicodeEx`. This supports both **Persian** and
**Persian (Standard)**, whose `پ` and `ئ` keys differ, instead of assuming one
hard-coded Persian layout.

## 2.6.0 bounded phrase review

The application retains physical-key tokens for at most three completed words
plus the current word. At a Space boundary it evaluates the longest
two-to-four-word suffix under both layouts. Each known word contributes
membership evidence, frequent words contribute additional evidence, unknown
words are penalized, and a complete same-language run receives a coherence
bonus. The target must recognize every word, so mixed-language text is not
flattened.

This bounded look-back lets a later word resolve an earlier ambiguity. For
example, all three English-layout strings in `nv clhkd ;i` are implausible as
English while their Persian physical-key interpretations form
`در زمانی که`. The algorithm remains deterministic, offline, and testable; it
does not claim full grammatical or semantic understanding.

Phrase history is invalidated whenever the cached caret position cannot be
trusted. The exact latest replacement is retained for 15 seconds only, allowing
one plain Backspace to restore the original word or phrase.

## 2.7.0 current-sentence monitor

The 2.6 algorithm was correct in isolation but its Windows Hook accepted every
printable `ToUnicodeEx` result before checking boundaries. Because Space is a
printable character, it entered the current token and prevented the Space
boundary evaluator from running. Version 2.7 gates runtime translation through
an explicit physical word-key allowlist. Space, digits, and non-word
punctuation reach their proper handlers, while the alternate OEM key used by
the two Windows Persian layout variants remains supported.

The prior four-word sliding window is replaced with the current sentence from
its beginning, bounded to 32 words or 512 characters. Every Space triggers a
full-sentence and longest-suffix review. Live or idle word corrections remain
pending until their following Space, and retrospective correction updates the
tracked visible language without deleting earlier sentence context.

These bounds make memory and Hook latency predictable. If the cap is exceeded
or the caret model becomes unreliable, retrospective rewriting is disabled
rather than risking unrelated text.
