# 31-EDO

this fork reinterprets Furnace's 180-slot note space as 31 equal divisions of the octave (31-EDO) instead of the standard 12. this document covers the fork's conventions and its known caveats.

31-EDO divides the octave into 31 steps of 38.71 cents each, rather than 12 steps of 100 cents. it approximates 12-EDO's intervals unevenly but reproduces quarter-comma meantone tuning closely, giving each of the 12 "chromatic" pitch classes up to five distinct spellings (e.g. `C#` and `Db` are different pitches, 77.4 cents apart) instead of collapsing them into one.


## slot mapping and anchors

a note is still stored as a single byte, 0 through 179, exactly as in stock Furnace. what changed is what that byte *means*: each unit step is now a 31-EDO step (38.71 cents) instead of a 12-EDO semitone (100 cents).

    slot = 31*(octave-2) + step        step in [0,30]

octave digits run 2 through 7, same range as before. two slots anchor the tuning:

- slot 62 is middle C, written `C-4`.
- slot 85 is A-4, tuned to `song.tuning` (440 Hz by default).

frequency for any slot follows directly from the A-4 anchor:

    freq(slot) = tuning * 2^((slot-85)/31)

adjacent slots are in a fixed ratio of 2^(1/31) (about 1.0231), and slots 31 apart are exactly one octave. the on-disk `.fur` pattern format is unchanged — note bytes still range 0..179 with the same sentinel values (`180`=note off, `181`=note release, etc.) — so files remain byte-compatible with stock Furnace at the file-format level. only the *interpretation* of the pitch differs.


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


## pattern-cell format

every note name occupies exactly 4 ASCII characters everywhere a note is displayed, copied, or parsed: the pattern view, the note-input widgets, and the clipboard. natural names (`C`) and two-character names (single sharp/flat `C#`/`Db`, and double-sharp `Cx`) are padded with a trailing space after the octave digit (`C-4 `, `C#4 `, `Cx4 `); the three-character double-flat names (`Dbb`) fill all 4 bytes with no padding, octave digit last (`Dbb4`). sentinel labels (`OFF`, `===`, `REL`, `RAW`, `...`, `???`, `BUG`) are padded to 4 bytes the same way.

this is a hard change from stock Furnace, which uses a fixed 3-byte note field (2-character name + 1-character octave digit, since 12-EDO names never exceed 2 characters). because of this:

- **clipboard exchange between this fork and stock Furnace, or between this fork and any other unpatched fork, does not work.** copying a pattern selection here and pasting into stock Furnace (or vice versa) will misread note fields — the byte counts don't line up. keep clipboard operations within builds of this fork.


## MIDI convention

MIDI input and output map 1:1 with slot numbers, not with 12-EDO semitones: MIDI note number *is* the 31-EDO step index, with slot 62 (middle C) corresponding to MIDI note 60. this is deliberately compatible with the Lumatone/Terpstra-style microtonal MIDI convention used by tools such as LatticeKeys, where a keyboard mapped to 31-EDO sends and receives raw step numbers rather than 12-EDO-equal-tempered note numbers.

practically: an external 31-EDO MIDI controller using this convention plays in tune against this fork out of the box. an ordinary 12-EDO MIDI keyboard or DAW, which does not know about this convention, will produce a 31-EDO scale that does not match the pitches printed on its keys.


## QWERTY chromatic rows

the default computer-keyboard note-entry layout is chromatic, not isomorphic: it maps four rows of physical keys to four consecutive runs of 31-EDO steps, offset from the current octave (`curOctave`):

| row | keys | step offsets |
|---|---|---|
| 1 | Z X C V B N M , . / | 0–9 |
| 2 | A S D F G H J K L ; ' | 10–20 |
| 3 | Q W E R T Y U I O P | 21–30 |
| 4 | 1 2 3 4 5 6 7 8 9 0 | 31–40 |

because a standard QWERTY keyboard has no fifth row, this layout cannot expose all 31 steps of a single octave as one contiguous row the way stock Furnace's 12-key rows do; the fourth row spills into the next octave. this is a hardware limitation of typing keyboards, not a design defect — the isomorphic, single-octave-per-shape experience lives in the Terpstra Keyboard window and in MIDI input (see above), not on the QWERTY layout. octave shift, transpose, and other note-key bindings work as in stock Furnace, just scaled to steps-of-31 instead of steps-of-12.


## Terpstra Keyboard window

the Terpstra Keyboard window (opened from the window menu, alongside the piano and other visualizers) is an isomorphic hexagonal note-entry surface modeled on Terpstra/LatticeKeys-style keyboards. each hexagonal cell is one slot; the hex grid's two axes correspond to fixed pitch intervals, so a given fingering shape produces the same interval anywhere on the grid — the defining property of an isomorphic layout, which the flat QWERTY rows above cannot offer.

each hex displays:

- the note name (letter + accidental, no octave) centered in the cell
- the octave digit as a small superscript in the upper-right corner
- a small badge in the lower-left corner showing the QWERTY key currently bound to that slot at the active octave, when one exists

cells are filled by accidental class — natural, sharp, flat, double-sharp, and double-flat each get a distinct color — and slots outside the valid 0..179 range are drawn dimmed and are not interactive. clicking or dragging across cells plays notes and (when the pattern editor's edit mode is active) writes them into the pattern, the same way the piano widget does; dragging produces a natural glissando across the pressed cells. the view supports panning (right-mouse drag) and zooming (ctrl + scroll wheel), and both persist between sessions.


## caveats

- **stock Furnace misreads fork files.** because the `.fur` file format is unchanged at the byte level, stock (unpatched) Furnace opens files saved by this fork without error or warning — but it interprets every note slot as 12-EDO, so pitches come out wrong. there is no format flag that distinguishes a 31-EDO song from a 12-EDO one; the reinterpretation happens entirely at the tracker level. treat fork-authored `.fur` files as incompatible with stock Furnace despite opening cleanly.

- **foreign format importers are unpatched.** import paths for other trackers' file formats (S3M, XM, MOD, FTM, Famicom Composer, and similar) are left as-is. notes coming from these formats land on the same raw slot numbers they would in stock Furnace, which this fork then plays back as 31-EDO steps — so imported songs from other trackers will sound retuned, not preserved. only Furnace's own DMF import and old-format `.fur` import are adjusted to embed 12-EDO pitches into the nearest 31-EDO step.

- **effect-nibble range limits what `00xy` arpeggio can reach.** the arpeggio effect's two nibbles are each 4 bits (0–15), which was already narrow for expressing intervals in 12-EDO but becomes a harder ceiling here: a 31-EDO perfect fifth is 18 steps, one more than the nibble's maximum of 15. `00xy` arpeggios cannot express a fifth or anything wider; wider intervals require the pattern's note column itself, an instrument macro, or pitch effects instead.

- **`E5xx` pitch offset is now ±38.7 cents per unit.** the fine-pitch effect steps in 31-EDO steps, same as it always stepped in whatever the note grid's unit was — so its per-unit size follows directly from the coarser grid: previously ±100 cents (a 12-EDO semitone) per unit, now ±38.71 cents (one 31-EDO step) per unit. existing patterns using `E5xx` for 12-EDO-scale pitch bends will bend by a different amount after conversion to this fork.

- **the piano/input-pad widget stays 12-key-per-octave.** it remains a legacy 12-banded layout and does not attempt to expose all 31 steps of an octave as piano-style keys. it is still functional for coarse note entry and preview, but the Terpstra Keyboard window is the intended primary input surface for 31-EDO.

- **the "use flats instead of sharps" and "use German notation" settings are no-ops.** both settings assume a single-spelling 12-EDO note table where sharp and flat names are interchangeable respellings of the same pitch. in this fork, sharps, flats, double-sharps, and double-flats each name a distinct pitch with no ambiguity to resolve, so there is nothing for these settings to toggle; they remain present in the settings window for compatibility but have no visible effect on note display.
