# song info

- **Name**: the track's title.
- **Author**: the author(s) of this track.
- **Album**: the associated album name (or the name of the game the song is from).
- **System**: the name of the game console or computer the track is designed for. this is automatically set when creating a new tune, but can be changed to anything. the **Auto** button will provide a guess based on the chips in use.

all of this metadata will be included in a VGM export. this isn't the case for an audio export, however.

- **Reference note** and **Sounds at**: choose a 31-EDO spelling and set the exact frequency it should produce. choosing another spelling preserves the current concert-pitch ratio, then pins the new spelling to its conventional 12-EDO frequency. A at 440 Hz is the default. Furnace stores the equivalent A-4 tuning internally. this preserves the tuning field itself, but does not convert 12-EDO note data in vanilla `.fur` files or foreign tracker imports; see the [31-EDO caveats](../31-edo.md#caveats).

to change song speeds/parameters or edit sub-song information, see the [subsongs.md](subsongs) and [speed.md](speed) sections of this manual.
