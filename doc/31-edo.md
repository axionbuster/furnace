# 31-EDO

this fork reinterprets Furnace's 180-slot note space as 31 equal divisions of the octave (31-EDO) instead of the standard 12. this document covers the fork's conventions and its known caveats.

31-EDO divides the octave into 31 steps of 38.71 cents each, rather than 12 steps of 100 cents. it approximates 12-EDO's intervals unevenly but reproduces quarter-comma meantone tuning closely, giving each of the 12 "chromatic" pitch classes up to five distinct spellings (e.g. `C#` and `Db` are different pitches, 77.4 cents apart) instead of collapsing them into one.


## slot mapping and anchors

a note is still stored as a single byte, 0 through 179, exactly as in stock Furnace. what changed is what that byte *means*: each unit step is now a 31-EDO step (38.71 cents) instead of a 12-EDO semitone (100 cents).

    slot = 31*(octave-2) + step        step in [0,30]

because the 180 slots now span about 5.8 octaves, displayed octave digits run 2 through 7 rather than stock Furnace's much wider 12-EDO range. two slots anchor the tuning:

- slot 62 is middle C, written `C-4`.
- slot 85 is A-4, tuned to `song.tuning` (440 Hz by default).

frequency for any slot follows directly from the A-4 anchor:

    freq(slot) = tuning * 2^((slot-85)/31)

adjacent slots are in a fixed ratio of 2^(1/31) (about 1.0231), and slots 31 apart are exactly one octave. the on-disk `.fur` pattern payload is unchanged — note bytes still range 0..179 with the same sentinel values (`180`=note off, `181`=note release, etc.) — but fork saves are deliberately marked as a separate dialect. they use the existing `Furnace-B module` downstream magic recognized by stock Furnace and carry the tag `FUR31EDO` in the header's final eight reserved bytes. this fork uses that tag to distinguish native 31-EDO files from other downstream files.

the song information window can express this same anchor using any of the 31 spellings. the selected reference note is an interface preference; the song still stores the equivalent A-4 frequency, so changing or reopening the selector does not require a `.fur` format change. choosing a new spelling preserves the current concert-pitch ratio and pins that spelling to its conventional 12-EDO frequency. for example, A at standard pitch remains 440 Hz, while choosing C pins C-4 to about 261.62557 Hz and makes A-4 about 437.54731 Hz.


## canonical spelling

31-EDO steps are spelled using a chain-of-fifths (meantone) scheme: 7 natural names, 7 sharps, 7 flats, 5 double-sharps, and 5 double-flats, one spelling per step:

| step | name | | step | name | | step | name | | step | name |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | C  | | 8  | Eb | | 16 | Gb | | 24 | Bbb |
| 1 | Dbb| | 9  | Dx | | 17 | Fx | | 25 | A#  |
| 2 | C# | | 10 | E  | | 18 | G  | | 26 | Bb  |
| 3 | Db | | 11 | Fb | | 19 | Abb| | 27 | Ax  |
| 4 | Cx | | 12 | E# | | 20 | G# | | 28 | B   |
| 5 | D  | | 13 | F  | | 21 | Ab | | 29 | Cb  |
| 6 | Ebb| | 14 | Gbb| | 22 | Gx | | 30 | B#  |
| 7 | D# | | 15 | F# | | 23 | A  | | | |

each name is unambiguous and distinct: unlike 12-EDO, `C#` and `Db` are not the same key — they are two different pitches a "diesis" (about 19.4 cents) apart, and both exist as separate, reachable slots.

the octave digit follows the *letter*, in the scientific-pitch-notation sense, with one deliberate exception: `Cb` takes the digit of the C above it rather than the digit implied by its raw slot arithmetic (so the step below `C-4` is `Cb4`, not `Cb3`). `B#` keeps the digit of its own chunk of 31 slots. every other name's digit is simply `slot/31 + 2`.


## note text and pattern-cell format

the note-name tables, note-input text, and clipboard serialization use fixed 4-byte ASCII fields. natural names (`C`) and two-character names (single sharp/flat `C#`/`Db`, and double-sharp `Cx`) are padded with a trailing space after the octave digit (`C-4 `, `C#4 `, `Cx4 `); the three-character double-flat names (`Dbb`) fill all 4 bytes with no padding, octave digit last (`Dbb4`). sentinel labels (`OFF`, `===`, `REL`, `RAW`, `...`, `???`, `BUG`) are padded to 4 bytes the same way.

the pattern view itself remains three glyph cells wide. ordinary names occupy those three cells directly, while a double flat is drawn as a single custom glyph between the letter and octave digit. this visual compaction does not change the four-byte clipboard representation.

this is a hard change from stock Furnace, which uses a fixed 3-byte note field (2-character name + 1-character octave digit, since 12-EDO names never exceed 2 characters). because of this:

- **clipboard exchange between this fork and stock Furnace, or between this fork and any other unpatched fork, does not work.** copying a pattern selection here and pasting into stock Furnace (or vice versa) will misread note fields — the byte counts don't line up. the clipboard header still uses Furnace's generic `org.tildearrow.furnace - Pattern Data` prefix and has no dialect marker, so this incompatibility is not detected before parsing. keep clipboard operations within builds of this fork.


## MIDI convention

MIDI input and output use consecutive MIDI note numbers as consecutive 31-EDO steps, not as 12-EDO semitones. the mapping is anchored so MIDI note 60 is slot 62 (`C-4`): within the unclamped range, MIDI note *n* maps to slot *n*+2. this is deliberately compatible with the Lumatone/Terpstra-style microtonal MIDI convention used by tools such as LatticeKeys.

practically: an external 31-EDO MIDI controller using this convention plays in tune against this fork out of the box. an ordinary 12-EDO MIDI keyboard or DAW, which does not know about this convention, will produce a 31-EDO scale that does not match the pitches printed on its keys. MIDI output cannot represent the full 180-slot range: slots above 129 all clamp to MIDI note 127, and the lowest two slots clamp to MIDI note 0.


## QWERTY chromatic rows

the default computer-keyboard note-entry layout is chromatic, not isomorphic. the three letter rows cover one complete 31-EDO octave, offset from the current octave (`curOctave`); the bracket and number keys continue into the next:

| row | keys | step offsets |
|---|---|---|
| 1 | Z X C V B N M , . / | 0–9 |
| 2 | A S D F G H J K L ; ' | 10–20 |
| 3 | Q W E R T Y U I O P | 21–30 |
| continuation | [ ] | 31–32 |
| 4 | 2 3 4 5 6 7 8 9 0 | 32–40 |

`]` and `2` are duplicate bindings for step 32, and `1` keeps its stock role as note off rather than entering a pitch. because a standard QWERTY keyboard has no single 31-key row, the isomorphic, single-octave-per-shape experience lives in the Terpstra Keyboard window and in MIDI input (see above). octave shift, transpose, and other note-key bindings work as in stock Furnace, just scaled to steps-of-31 instead of steps-of-12.


## Terpstra Keyboard window

the Terpstra Keyboard window (opened from the window menu, alongside the piano and other visualizers) is an isomorphic hexagonal note-entry surface modeled on Terpstra/LatticeKeys-style keyboards. each hexagonal cell is one slot; the hex grid's two axes correspond to fixed pitch intervals, so a given fingering shape produces the same interval anywhere on the grid — the defining property of an isomorphic layout, which the flat QWERTY rows above cannot offer.

each hex displays:

- the note name (letter + accidental, no octave) centered in the cell
- the octave digit as a small superscript in the upper-right corner
- a small badge in the lower-left corner showing the QWERTY key currently bound to that slot at the active octave, when one exists

cells are filled by accidental class — natural, sharp, flat, double-sharp, and double-flat each get a distinct color — and slots outside the valid 0..179 range are drawn dimmed and are not interactive. clicking or dragging across cells plays notes and (when the pattern editor's edit mode is active) writes them into the pattern, the same way the piano widget does; dragging produces a natural glissando across the pressed cells. the view supports panning (right-mouse drag) and zooming (ctrl + scroll wheel), and both persist between sessions.

while the song plays, active notes light their matching cells and remain lit until their channel receives a note-off, is muted, or playback stops. when a channel changes pitch, its previous cell turns off and its new cell lights. mouse and QWERTY input use the selected or auto-assigned channel's color for the pressed cell and its octave echoes. by default all highlights use the same channel-color scheme as the per-channel oscilloscope; the options button in the keyboard toolbar can switch them to a custom solid color instead.


## caveats

- **stock Furnace warns but still misreads fork files.** stock recognizes the downstream magic, opens a fork file, and warns that it was created with a downstream version. it ignores the `FUR31EDO` tag and still interprets every note slot as 12-EDO, so pitches come out wrong. do not edit or re-save fork-authored files in stock Furnace.

- **unmarked `.fur` files are ambiguous.** vanilla Furnace files and 31-EDO files created before the dialect marker use the same vanilla magic, so this fork cannot tell them apart. it allows them to load with a warning, but does not attempt a complete 12-EDO conversion; notes, effects, and macros may be incorrect. save a copy before editing.

- **foreign format importers are unpatched.** import paths for other trackers' file formats (S3M, XM, MOD, FTM, Famicom Composer, and similar) are left as-is. notes coming from these formats land on the same raw slot numbers they would in stock Furnace, which this fork then plays back as 31-EDO steps — so imported songs from other trackers will sound retuned, not preserved. only Furnace's own DMF import and old-format `.fur` import are adjusted to embed 12-EDO pitches into the nearest 31-EDO step.

- **DMF export quantizes to 12-EDO.** exporting to DefleMask's `.dmf` format folds each 31-EDO step onto its nearest 12-EDO semitone (up to ~19 cents of error per note) so the exported file stays within DMF's fixed 12-tone note range. pitch relationships that depend on 31-EDO's finer grid (distinct sharps and flats, neutral-ish intervals) collapse in the export.

- **the startup intro tune is native 31-EDO content.** its notes were hand-respelled for this fork rather than mechanically remapped at load time. the embedded module carries the same downstream magic and `FUR31EDO` tag as saved fork files, so it loads as native content without the unmarked-file warning.

- **effect-nibble range limits what `00xy` arpeggio can reach.** the arpeggio effect's two nibbles are each 4 bits (0–15), which was already narrow for expressing intervals in 12-EDO but becomes a harder ceiling here: a 31-EDO perfect fifth is 18 steps, one more than the nibble's maximum of 15. `00xy` arpeggios cannot express a fifth or anything wider; wider intervals require the pattern's note column itself, an instrument macro, or pitch effects instead.

- **`E5xx` has a ±38.7-cent full-scale range.** `80` is neutral, `00` is one 31-EDO step (38.71 cents) down, and `FF` is nearly one step up; each change of one in `xx` is 1/128 of a step. existing patterns using `E5xx` for 12-EDO-scale pitch bends will bend by a different amount after conversion to this fork.

- **the piano/input-pad widget stays 12-key-per-octave.** it remains a legacy 12-banded layout and does not attempt to expose all 31 steps of an octave as piano-style keys. it is still functional for coarse note entry and preview, but the Terpstra Keyboard window is the intended primary input surface for 31-EDO.

- **the "use flats instead of sharps" and "use German notation" settings are no-ops.** both settings assume a single-spelling 12-EDO note table where sharp and flat names are interchangeable respellings of the same pitch. in this fork, sharps, flats, double-sharps, and double-flats each name a distinct pitch with no ambiguity to resolve, so there is nothing for these settings to toggle; they remain present in the settings window for compatibility but have no visible effect on note display.
