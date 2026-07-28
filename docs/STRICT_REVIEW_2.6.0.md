# KeySwitchFix 2.6.0 — five-round strict review

These scores evaluate engineering safety, missed corrections, false
corrections, responsiveness, privacy, reproducibility, and testability. They do
not claim that a compact offline classifier understands language like a
cloud-scale semantic model.

## Round 1 — repair isolated misses

**Candidate:** Keep the 2.5 word evaluator and add exceptions for `تا` and
`how`.

**Strict critique:** A special-case list would repair the reported examples but
would not address the underlying causes. `تا` needs the complete two-key
boundary/idle path, while `how` is a genuine collision: both English `how` and
the Persian-layout output `اخص` exist in the dictionaries. The existing
frequent tiers were loaded but did not affect the collision decision.

**Score: 6.1/10**

**Revision:** Recognize exact one- and two-key function words, route `تا`
through adaptive-pause and boundary evaluation, and make asymmetric frequent
membership worth 45 collision points. Correct English `how` remains unchanged.

## Round 2 — use adjacent words

**Candidate:** Trust the scalar "recent language" accumulator more strongly
when one word is unclear.

**Strict critique:** A scalar cannot revisit the exact earlier token and cannot
distinguish a coherent phrase from several unrelated words. Increasing its
weight would also flatten code-switching.

**Score: 7.0/10**

**Revision:** Retain a bounded token history and score the longest available
two-to-four-word suffix in both physical-key interpretations. The model rewards
known and frequent words, penalizes unknown words, and adds a completeness
bonus to a full same-language run. This recognizes
`nv clhkd ;i` → `در زمانی که`.

## Round 3 — attack false phrase corrections

**Candidate:** Rewrite a phrase whenever one language has the higher aggregate
score.

**Strict critique:** A partially known target could delete legitimate names,
code, or mixed Persian/English text. Cached text also becomes unsafe after a
caret move, extra punctuation, or a window change.

**Score: 8.0/10**

**Revision:** Require every target word to be known and at least two words to
agree. Reject mixed runs such as `سلام test`. Keep only Space-separated text,
and clear phrase history after mouse clicks, navigation, manual layout changes,
unsupported characters, setting changes, or foreground-window changes.

## Round 4 — make every decision reversible

**Candidate:** Keep the existing `Ctrl + Win + Backspace` global hotkey.

**Strict critique:** The shortcut is hard to remember and does not match the
Google Keyboard interaction requested by the user. The old hook also
invalidated its Undo record before processing an ordinary Backspace.

**Score: 8.6/10**

**Revision:** Intercept one unmodified Backspace for 15 seconds after a
correction, remove the complete replacement and delimiter, and restore the
exact original word or phrase. Suppress the physical Backspace only when the
restore succeeds. Keep the global shortcut as an accessibility fallback.

## Round 5 — final adversarial and release audit

**Candidate:** Ship after the reported examples pass.

**Strict critique:** Example-only testing misses correct-layout regressions,
mixed-language damage, phrase-length bounds, Windows layout differences,
resource corruption, stale versions, and packaging errors.

**Score before revision: 9.0/10**

**Revision:** Add positive and negative tests for `تا`, `how`,
`در زمانی که`, `how are you`, and mixed Persian/English text; retain the
existing common-word, prefix, collision, both-layout, and `اثممخ` → `hello`
matrix. Compile C with warnings as errors, cross-build all x64 GUI binaries,
verify PE architecture and embedded payload identity, verify metadata, and
validate package checksums and ZIP integrity.

**Final score: 9.5/10**

The remaining 0.5 reflects irreducible ambiguity when both physical-key
interpretations are valid and surrounding text supplies no decisive evidence,
the probabilistic nature of Bloom filters, and application-specific Windows
input restrictions. Plain Backspace Undo is the explicit safety valve for a
rare probabilistic mistake.
