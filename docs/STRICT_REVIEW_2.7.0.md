# KeySwitchFix 2.7.0 — three-cycle strict implementation review

The review distinguishes core-algorithm tests from the real Windows keyboard
Hook. Scores include correctness, false corrections, responsiveness, privacy,
Windows-layout compatibility, and testability.

## Cycle 1 — reproduce the real failure

**Observed result:** `nv clhkd ;i` remained unchanged even though the core
sequence test recognized `در زمانی که`.

**Strict critique:** Passing a direct evaluator test did not prove that the Hook
ever delivered words to that evaluator. Runtime mapping used `ToUnicodeEx`
before boundary classification. `ToUnicodeEx` returns Space as a printable
character, so Space was appended to the word and the boundary path was skipped.
The advertised sentence feature was therefore unreachable during ordinary
Space-separated typing.

**Score before revision: 4.0/10**

**Revision:** Add an explicit word-scan-code gate before runtime layout
translation. Add a regression asserting that scan code `0x39` (Space) is never
a word token. Keep actual installed-layout translation after the gate.

**Score after revision: 7.8/10**

## Cycle 2 — monitor the sentence, not a sliding fragment

**Candidate:** Keep the four-word history after fixing Space.

**Strict critique:** Four words do not satisfy monitoring from the beginning of
the sentence. A second defect also removed a word that had already been
corrected live: `clear_word()` ran before the following Space could commit it to
history. Retrospective correction then cleared all earlier context.

**Revision:** Keep the current sentence from its beginning with explicit safety
limits of 32 words and 512 characters. Hold live/idle corrections in a pending
state until Space commits them. After phrase repair, update only the corrected
suffix and continue monitoring the existing sentence. Add a six-word
regression proving the old four-word cap is gone.

**Score after revision: 8.7/10**

## Cycle 3 — attack the fix itself

**Candidate:** Gate runtime mapping through the old static fallback table and
ship.

**Strict critique:** That would prevent Space from entering words, but it would
also reject the alternate OEM physical key used for `پ/ئ` by the two Windows
Persian layouts. Long corrected sentences could additionally overrun the
diagnostics formatting assumption, and unbounded sentence monitoring would
increase Hook latency and deletion risk.

**Revision:** Separate "allowed physical word key" from "has a portable
fallback character." Keep the alternate OEM key eligible for actual
`ToUnicodeEx` translation. Bound sentence tokens and reconstructed text, abandon
retrospective rewriting when those caps are exceeded, and show only bounded
diagnostic previews. Re-run the common-word, collision, mixed-language,
prefix, both-Persian-layout, `تا`, `how`, `hello`, and exact
`nv clhkd ;i` regressions.

**Final score: 9.4/10**

The remaining 0.6 reflects irreducible bilingual collisions, probabilistic
Bloom membership, and Windows applications that block or reinterpret
`SendInput`. Plain Backspace Undo remains the immediate recovery path for a
rare incorrect probabilistic decision.
