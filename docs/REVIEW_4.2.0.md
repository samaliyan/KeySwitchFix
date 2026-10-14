# Review of 4.2.0

4.2 adds spelling in context: Persian word pairs from Wikipedia, the user's
own phrases, and a fix of the previous word when the next word settles it.

## Method

1. **Data.** Pairs were counted in the Persian Wikipedia dump of 2018-10-01 (79 million words). Articles whose Id is divisible by 20 were never counted; they are the test sentences.
2. **Measurement.** 40,000 typos were placed in held-out sentences, and 30,000 correct words were kept in context. Both were measured three ways: with no context, with the word before, and with both neighbours. Then the pair table size was chosen (4 MB against 8 MB: 0.3–0.4 points less).
3. **Tests.**
   - A fixed part of the set runs at every build with limits.
   - Hook simulations cover:
     - the fix of the previous word and its undo;
     - learning the user's own phrases;
     - half-spaces, Tab and long sentences.
   - Each new behaviour was removed in turn to confirm that a test fails.
4. **Independent review.** Two reviewers checked the engine, the memory and the app; a fresh reviewer then checked the fixes, and its findings were fixed and tested too.

## Findings and fixes

| Finding | Severity | Fix |
| --- | --- | --- |
| A typo repaired from the next word was learned as a phrase ("به بزان", "بزان مادری") before the repair. After a few repeats the learned typo phrases argued for keeping the typo, and the repair stopped. | major | Words spelling doubts never form phrases. The repaired phrase is learned, also for immediate fixes. |
| Once 2,048 phrases were known, every new phrase evicted the one before it, so nothing new was learned. | major | Phrases seen in the last 512 sightings are spared from eviction. |
| The word history was dropped entirely after 31 words or 512 characters, so long sentences lost their context and the layout repair of earlier words. | minor | Only the oldest words are dropped. |
| `ks_spell_pair_score` key buffer was one character short for two 33-character words (not reachable from the app). | minor | Buffer enlarged. |
| Undo of a previous-word fix miscounted the words in the writing memory. | minor | It counts back the typo and takes back the fix. |
| A fix that ended with Tab gave its word as context to the next field. | minor | Only a Space carries the context on. |
| The second part of a half-space word could be respelled as a word of its own. | minor | Skipped. |
| Hand-edited phrase lines with extra spaces or capitals were dropped or never used; a huge count was not capped. | minor | Trimmed, lower-cased, capped. |
| After a restart the whole saved phrase list fell inside the 512-sighting grace, so the next new phrase evicted the strongest one. | major | The clock starts after the grace when the file is read; when every phrase is spared, the weakest one goes. |
| In a code editor below Aggressive, the fix of the previous word was skipped but its phrases were still learned. | minor | Learning follows the same checks as the fix. |
| Undo of a fix kept the phrases learned with the fix. | minor | Undo takes those phrases back. |
| An immediate fix learned a phrase with a previous word that was itself in doubt. | minor | No phrase with a doubtful word, also on this path. |

## Not verified here

- The Windows build: the Zig compiler download is blocked in the review environment, so GitHub Actions builds and checks it.
- Real applications.
- Colloquial writing: the test sentences are written Persian, so the user's own phrases are only tested in simulation.
