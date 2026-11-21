KeySwitchFix embeds Bloom representations derived from:

1. dictionary-fa 2.0.0 / Lilak Persian Spell Checking Dictionary
   License: Apache License 2.0
   Full notice: dictionary-fa-LICENSE.txt

2. dictionary-en 4.0.0 / en_US Hunspell Dictionary derived from SCOWL
   License: SCOWL permissive license plus upstream notices
   Full notice: dictionary-en-LICENSE.txt

3. wordfreq 3.1.1 / combined frequency estimates for English and Persian,
   and for the languages of the language packs (languages\*.kslang:
   Arabic, Bulgarian, Dutch, French, German, Greek, Hebrew, Italian,
   Polish, Portuguese, Russian, Spanish, Turkish, Ukrainian)
   Software license: Apache License 2.0
   Derived frequency-data license: Creative Commons BY-SA 4.0
   (this covers every language pack built from wordfreq)
   Full notices: wordfreq-LICENSE.txt and wordfreq-NOTICE.txt

4. Persian Wikipedia (dump of 2018-10-01) / word-pair statistics for
   spelling in context (resources/fa-pairs.bin)
   Text license: Creative Commons BY-SA 3.0, (c) Wikipedia contributors
   Derived statistics: Creative Commons BY-SA 3.0
   Full notice: fa-pairs-NOTICE.txt

The application resources and the language packs contain probabilistic
membership bits, compact physical-key collision priors, and (in the packs)
the language's one- and two-letter words; not the original word-list files.

English word pairs (resources/en-pairs.bin)
   Derived from the SymSpell frequency lists in symspellpy 6.10.2 (MIT),
   which come from the Google Books Ngram data (CC BY 3.0) and SCOWL.
   Full notice: en-pairs-NOTICE.txt
