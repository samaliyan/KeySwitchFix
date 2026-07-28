# KeySwitchFix 2.5.0 — five-round strict review

The score is an engineering review score, not a claim of linguistic
perfection. Each round was reviewed against false corrections, missed
corrections, determinism, offline operation, reproducibility, and testability.

## Round 1 — separate evidence from preference

**Candidate:** Add more language preference weight to the existing 2.4
classifier.

**Strict critique:** The classifier applied `Prefer Persian` to every
Persian-to-English candidate, although the UI promised that preference affected
collisions only. This suppressed the objective, one-sided
`اثممخ` → `hello` match. More weight would make the root error worse.

**Score: 5.8/10**

**Revision:** Split one-sided and both-known candidates. A known target with an
unknown active-layout output now corrects regardless of sentence context or
preference.

## Round 2 — broaden the vocabulary

**Candidate:** Add larger common and frequent layers while keeping the base
Hunspell dictionaries.

**Strict critique:** Coverage improved, but raw dictionary size is not
intelligence. Prefixes must be generated from the same common vocabulary, and
missing corpus observations must not become extreme language priors.

**Score: 7.2/10**

**Revision:** Add 20,000 eligible common words and 2,000-word frequent tiers per
language, generate common-prefix guards, reject malformed/non-typeable entries,
and leave missing frequency evidence neutral.

## Round 3 — model sentence position

**Candidate:** Resolve both-known words with a corpus-normalized frequency prior
at sentence start and recent word language in the middle of a sentence.

**Strict critique:** A single previous word can be code or a name, and comparing
frequencies from unlike corpus genres biases the decision. Two physical keys
such as `of`/`خب` contain too little information for a safe corpus-only guess.

**Score: 7.9/10**

**Revision:** Use a weighted, saturating language accumulator; reduce the
frequency prior in mid-sentence; reserve full priors for words of at least three
keys at sentence start; require context or explicit preference for two-key
collisions.

## Round 4 — adversarial typing behavior

**Candidate:** Correct high-confidence words live and defer prefixes to an
adaptive idle timer.

**Strict critique:** A thinking pause could still rewrite `بهسا` to `fish`, a
period could discard a pending correction, Undo could leave the wrong language
context behind, and resetting all context at every period discarded useful
document-level evidence.

**Score: 8.7/10**

**Revision:** Add common-prefix idle protection, evaluate plain period as a word
boundary, feed Undo back as strong source-language evidence, and preserve
weighted document language while resetting only sentence position.

## Round 5 — real Windows layouts and final audit

**Candidate:** Validate the best classifier against frequent words and both
Windows Persian layouts.

**Strict critique:** The fixed scan-code table represented Persian (Standard),
but Windows also ships a different Persian layout. In particular, `پ/ئ` occupy
different keys, so words such as `پیدا` could never work reliably for every
user even with a perfect dictionary. The earlier English/Persian frequency
sources also represented different text genres.

**Score before revision: 9.0/10**

**Revision:** Translate physical keys through the actual installed/last-used
Windows layouts with `ToUnicodeEx`; use wordfreq 3.1.1 for comparable,
multi-source English and Persian Zipf estimates; pin its data hashes; and add an
adversarial matrix covering common words, collisions, prefixes, sentence
context, punctuation, preference, and both `پیدا` key positions.

**Final score: 9.4/10**

The remaining 0.6 reflects unavoidable ambiguity in isolated physical-key
collisions, probabilistic Bloom membership, application-specific protected
fields, and Windows integrity restrictions. The reversible preference modes and
`Ctrl + Win + Backspace` Undo cover the first limitation without pretending the
keys contain semantic information that is not present.
