# LatticeKeys Grand Piano

These QSound and ES5506 instruments use the same 42 whole-tone-spaced piano
anchors as LatticeKeys. The recordings come from the VCSL Grand Piano,
Steinway B bank by Versilian Studios LLC and were released under CC0 1.0.

Source: <https://github.com/sgossner/VCSL>, revision
`c1ea7bcc3c7309650ab0da9d15c9cd1fbc4a4c7e`.

The QSound version converts the recordings to 13,132 Hz, 8-bit PCM and keeps
up to 4.9 seconds per anchor. The ES5506 version converts the full recordings
to 26,250 Hz, 16-bit PCM. Each instrument calibrates the center rate of every
anchor so that its 12-EDO source pitch still tracks Furnace's exact 31-EDO
note domain.

Rebuild both instruments from a sibling LatticeKeys checkout with:

```sh
uv run scripts/build_vcsl_piano_instruments.py
```
