# UTF-8 input with classic Quake text

SDL2 text entry and clipboard paste into console, chat, and native menu fields
approximate common Unicode characters using the existing Quake font. Accented
Latin, Cyrillic and Greek letters are transliterated, punctuation, currency and
maths symbols are spelled out, regional-indicator pairs become their country
code, and common emoji fall back to text. No font pack, setting,
or additional colours are required. For example:

| Input | Quake text |
| --- | --- |
| `café`, `Łódź`, `Straße` | `cafe`, `Lodz`, `Strasse` |
| `“hello”…`, `¿qué‽` | `"hello"...`, `?que?!` |
| `ＡＢＣ１２３` | `ABC123` |
| `25°C`, `½`, `©`, `™`, `€5`, `±`, `≥` | `25degC`, `1/2`, `(c)`, `(tm)`, `E5`, `+/-`, `>=` |
| `Привет, Женя!` | `Privet, Zhenya!` |
| `Αθήνα` | `Athina` |
| `🇺🇸`, `🇩🇪` | `US`, `DE` |
| `🙂`, `😉`, `😂`, `🤔`, `😍` | `:)`, `;)`, `:D`, `[hmm]`, `<3` |
| `❤️`, `💔` | `<3`, `</3` |
| `👍🏽`, `👎`, `🤷` | `[+1]`, `[-1]`, `[shrug]` |
| `✅`, `❌`, `⚠️` | `[ok]`, `[x]`, `[!]` |
| `🔥`, `💀`, `🎉`, `💥` | `[fire]`, `[skull]`, `[party]`, `[boom]` |
| `👋`, `🙏`, `🏆`, `🐐` | `[wave]`, `[thanks]`, `[win]`, `[goat]` |
| `🚀`, `⚡`, `🎯`, `👀`, `💪` | `[rocket]`, `[zap]`, `[target]`, `[eyes]`, `[flex]` |

This is lossy transliteration, not full Unicode rendering. Unsupported
characters become `?`. Characters that only decorate their neighbours are
dropped rather than replaced, so they never leave a stray `?` behind: combining
accents, variation selectors, skin-tone modifiers, soft hyphens, bidi controls,
zero-width spaces, joiners, and byte-order marks. Only the joiner itself is
ignored; surrounding characters are preserved. Multi-part emoji are converted
component by component, so `😶‍🌫️` becomes `:|?`. This also keeps conversion
consistent when an emoji arrives in separate text-input events. Clipboard/input
limits still apply to the resulting text.

Conversion occurs before text enters Quake's byte-oriented input buffers, so
editing and multiplayer transmission continue to use classic characters.
Copying text from chat or native menu fields exports plain text, translating
legacy coloured letters and Quake symbols so they remain readable on paste.
Incoming server messages, QC strings, demos, and legacy coloured names are not
automatically decoded: their high bytes can be Quake glyphs rather than UTF-8.
SDL1 typing and the legacy Windows SDL1 clipboard path retain their old behaviour.

The sorted fallback table and bounded conversion helpers live in `Quake/common.c`.
Use them only for sources known to contain UTF-8, before applying Quake colours.
Run `python3 Misc/stress/test_utf8_to_quake.py` for sanitizer tests covering
conversion, typing, paste, menu filtering, selection, and malformed input.
