# 31-EDO fork clipboard format

when copying pattern data from Furnace, it's stored in the clipboard as plain text.

```
org.tildearrow.furnace - Pattern Data (250)
```

this top line of text is always the same except for the number in parentheses, which is the internal format version. this fork currently writes `250`. the prefix does not identify the 31-EDO dialect, so stock Furnace and this fork will each accept the other's header even though their note fields have different widths. do not exchange pattern clipboard text between them.

the second line is a number between 0 and 18 (decimal) which indicates which column the clip starts from.
- `0`: note.
- `1`: instrument.
- `2`: volume.
- `3`: effect 1 type.
- `4`: effect 1 value. effect type is always included in the clip, even if skipped over.
- `5`: effect 2 type.
- `6`: effect 2 value. effect type is always included in the clip, even if skipped over.
- `7`: effect 3 type...
- ...and so on.

examples of the starting column:

```
org.tildearrow.furnace - Pattern Data (250)
0
D-6 007F08080706|............|
................|............|
................|A#5 00..080F|
................|............|
```

```
org.tildearrow.furnace - Pattern Data (250)
1
007F08080706|...........|
............|...........|
............|A#5 00..080F|
............|...........|
```

```
org.tildearrow.furnace - Pattern Data (250)
2
7F08080706|...........|
..........|...........|
..........|A#5 00..080F|
..........|...........|
```

```
org.tildearrow.furnace - Pattern Data (250)
3
08080706|...........|
........|...........|
........|A#5 00..080F|
........|...........|
```

```
org.tildearrow.furnace - Pattern Data (250)
4
08080706|...........|
........|...........|
........|A#5 00..080F|
........|...........|
```

```
org.tildearrow.furnace - Pattern Data (250)
5
0706|...........|
....|...........|
....|A#5 00..080F|
....|...........|
```

```
org.tildearrow.furnace - Pattern Data (250)
6
0706|...........|
....|...........|
....|A#5 00..080F|
....|...........|
```

```
org.tildearrow.furnace - Pattern Data (250)
0
...........|
...........|
A#5 00..080F|
...........|
```

each line following the column number is a fixed-width text serialization of the pattern with channels separated by `|`. each line also ends in `|`. note fields in this fork are four bytes wide even though the pattern view compacts them into three visible glyph cells; all other pattern fields remain two bytes wide. raw-frequency notes are serialized as `r` followed by eight hexadecimal digits.

notes use the fork's canonical 31-EDO spellings. shorter names and sentinel labels are padded to four bytes: for example `C-4 `, `C#4 `, `OFF `, `=== `, and `REL `. a three-letter double-flat name fills the field without padding, as in `Dbb4`.
