#!/usr/bin/env bash
# Regenerates src/ui/fonts/*.cpp from Noto Sans SC (SIL OFL 1.1).
#
#   EPAPER75_FONT_DIR=~/Library/Caches/epaper75-fonts tools/gen/gen_fonts.sh
#
# Requires macOS (CoreText via swift) and python3. The font files stay outside
# Git; only the generated glyph tables are committed. Rerun after adding new
# Chinese UI strings so their glyphs are included.
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
app_dir="$(cd -- "$script_dir/../.." && pwd -P)"
font_dir="${EPAPER75_FONT_DIR:-$HOME/Library/Caches/epaper75-fonts}"
regular="$font_dir/NotoSansSC-Regular.otf"
bold="$font_dir/NotoSansSC-Bold.otf"
out_dir="$app_dir/src/ui/fonts"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

for f in "$regular" "$bold"; do
  [[ -f "$f" ]] || { echo "Missing font: $f" >&2; exit 1; }
done

# Every non-ASCII character used in UI/calendar/weather source code.
python3 -I - "$app_dir" > "$work/ui.txt" <<'PY'
import pathlib, sys
root = pathlib.Path(sys.argv[1])
chars = set()
for sub in ("src/ui", "src/cal", "src/weather", "include/ui", "include/cal", "include/weather"):
    for path in (root / sub).rglob("*"):
        if path.suffix not in (".cpp", ".h") or "fonts" in path.parts:
            continue
        chars.update(c for c in path.read_text(encoding="utf-8") if ord(c) > 0x7E and ord(c) <= 0xFFFF)
print("".join(sorted(chars)))
PY
python3 -I -c 'print("".join(chr(c) for c in range(0x20, 0x7F)))' > "$work/ascii.txt"
python3 -I - > "$work/gb1.txt" <<'PY'
out = []
for hi in range(0xB0, 0xD8):
    for lo in range(0xA1, 0xFF):
        if hi == 0xD7 and lo > 0xF9:
            break
        out.append(bytes([hi, lo]).decode("gb2312"))
print("".join(out))
PY
printf '0123456789-.:' > "$work/hero.txt"
printf '0123456789-.:%%°C 一二三四五六七八九十月日历' > "$work/heading.txt"

cat "$work/ascii.txt" "$work/ui.txt" > "$work/ui_ascii.txt"
cat "$work/ascii.txt" "$work/ui.txt" "$work/gb1.txt" > "$work/full.txt"

mkdir -p "$out_dir"
gen() {  # file symbol face px charset
  swift "$script_dir/gen_font.swift" "$3" "$4" "$2" "$5" > "$out_dir/$1.cpp"
}
gen FontSmall kFontSmall "$regular" 13 "$work/ui_ascii.txt"
gen FontSmallBold kFontSmallBold "$bold" 13 "$work/ui_ascii.txt"
gen FontBody kFontBody "$regular" 17 "$work/full.txt"
gen FontBodyBold kFontBodyBold "$bold" 17 "$work/ui_ascii.txt"
gen FontNumber kFontNumber "$bold" 22 "$work/ascii.txt"
gen FontTitle kFontTitle "$bold" 26 "$work/full.txt"
gen FontHeading kFontHeading "$bold" 36 "$work/heading.txt"
gen FontHero kFontHero "$bold" 80 "$work/hero.txt"
echo "UI characters: $(python3 -I -c 'import sys; print(len(open(sys.argv[1], encoding="utf-8").read().strip()))' "$work/ui.txt")"
