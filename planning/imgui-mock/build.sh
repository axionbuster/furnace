#!/bin/sh
# Build the headless ImGui mockup harness. Pure CPU, no SDL/GL/window.
# Usage: ./build.sh          (build only)
#        ./build.sh run      (build, then render all scenes to out/)
set -e
cd "$(dirname "$0")"
IMGUI=../../extern/imgui_patched
mkdir -p build
clang++ -O2 -std=c++17 -I"$IMGUI" \
  mock_main.cpp \
  "$IMGUI/imgui.cpp" \
  "$IMGUI/imgui_draw.cpp" \
  "$IMGUI/imgui_tables.cpp" \
  "$IMGUI/imgui_widgets.cpp" \
  -lz \
  -o build/mock
echo "built build/mock"
if [ "$1" = "run" ]; then
  ./build/mock out
fi
