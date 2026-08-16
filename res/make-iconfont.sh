#!/bin/bash
# run after exporting icons.ttf from icons.sfd

set -euo pipefail

cd -- "$(dirname -- "$0")"

if ! command -v ttfautohint >/dev/null 2>&1; then
  echo "make-iconfont.sh: ttfautohint is required" >&2
  exit 1
fi

hint_dir=$(mktemp -d "${TMPDIR:-/tmp}/furnace-icons.XXXXXX")
trap 'rm -rf -- "$hint_dir"' EXIT

# treat the private-use glyphs as symbols, hint composites too, and preserve the
# flat's one-pixel stem at the default pattern size; 114 is the 100-unit stem
# normalized from Furnace Icons' 1792-unit em to ttfautohint's 2048-unit em
if ttfautohint -s -c -n -H 114 -x 0 icons.ttf "$hint_dir/icons.ttf" 2>"$hint_dir/ttfautohint.log"; then
  mv -- "$hint_dir/icons.ttf" icons.ttf
elif grep -q "already been processed with ttfautohint" "$hint_dir/ttfautohint.log"; then
  echo "make-iconfont.sh: icons.ttf is already hinted; keeping it"
else
  cat "$hint_dir/ttfautohint.log" >&2
  exit 1
fi

echo "#include \"fonts.h\"" > ../src/gui/font_furicon.cpp
zlib-flate -compress=9 < icons.ttf | xxd -i -n "furIcons_compressed_data" | sed -E "s/^ +//g;s/, /,/g;s/ = /=/g;s/unsigned/const unsigned/g;s/compressed_data_len/compressed_size/" >> ../src/gui/font_furicon.cpp
