# SFV Ember's own words

These catalogs hold the few messages where SFV Ember says something other than the Ember library's shared catalogs (`<library>/locales/*.po`): the shared text names SF4 Ember, Ultra Street Fighter IV, its files, or features SFV does not have. Everything else SFV Ember shows comes from the shared catalogs. The build lays these over them (`ember::ProductIdentity::localeOverlays`; EMBER_LIBRARY.md section 30).

- One catalog for every language the library declares in its `locales/locales.json`, named by its tag. A missing one stops the configure.
- `en.po` is the base. Every other catalog has exactly its msgids, in its order, with the same `{0}` placeholders and line breaks (`tests/LocaleOverlayTests.cxx` checks this, that nothing names SF4 or its game, and that the interface's fonts draw every character).
- A msgid here must exist in the library's `en.po`: the library refuses an overlay msgid it does not have.
- Product names stay as they are: SFV Ember, SFV, Ember, Street Fighter V (Simplified Chinese writes 《街头霸王V》, as its shared catalog writes the IV game's name), Steam, file and folder names, key names.

## How the translations were made, and how to change one

Each language's catalog starts from that language's shared translation of the same msgid; only what SFV's English changes is rewritten (the product, the game, files and folders, or a sentence of SFV's own). That keeps the shared terms (room, table, Ready, invitation, build, Practice...) the same as on every other screen, and the shared catalog's tone (German's pick and Home screens say "du", its launcher "Sie").

To change a message: edit `en.po`, then the same msgid in every other catalog, starting from that language's shared translation of the msgid and from SFV's English. All but the English are drafts for native review, as the library's own translated catalogs are.

Weblate can take these as a monolingual gettext component of their own: `locales/en.po` the base, `locales/*.po` the translations, the tags exactly as above.
