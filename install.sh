#!/usr/bin/env bash
# Build and install the Soundgineer engine for the current user. No sudo.
#
#   ./install.sh            engine, the Soundgineer window app, CLI tools, presets, systemd units
#   ./install.sh --plugins  also build the VST3 / LV2 / standalone app (fetches DPF)
#   ./install.sh --no-start install without enabling soundgineer.socket
#
# Writes only to: ~/.cache/soundgineer (build), ~/.local/bin,
# ~/.local/share/soundgineer, ~/.local/share/systemd/user,
# ~/.local/share/applications, and with --plugins
# ~/.lv2 and ~/.vst3 (the standard per-user plugin folders hosts scan).
set -euo pipefail

src="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
plugins=0
start=1
for arg in "$@"; do
  case "$arg" in
    --plugins) plugins=1 ;;
    --no-start) start=0 ;;
    -h|--help) sed -n '2,10p' "$0"; exit 0 ;;
    *) echo "unknown option: $arg" >&2; exit 2 ;;
  esac
done

if [[ $EUID -eq 0 && -z ${SG_ALLOW_ROOT:-} ]]; then  # SG_ALLOW_ROOT: CI/containers only
  echo "Run install.sh as your normal user, not root." >&2
  exit 1
fi

prefix="$HOME/.local"
data="${XDG_DATA_HOME:-$HOME/.local/share}"
build="${XDG_CACHE_HOME:-$HOME/.cache}/soundgineer/build"

missing=()
command -v cmake >/dev/null || missing+=(cmake)
command -v c++ >/dev/null || command -v g++ >/dev/null || missing+=(base-devel)
command -v pkg-config >/dev/null || missing+=(pkgconf)
if command -v pkg-config >/dev/null && ! pkg-config --exists jack; then missing+=(pipewire-jack); fi
if ((${#missing[@]})); then
  echo "Soundgineer needs a few build tools first. Install them with:"
  echo
  echo "  sudo pacman -S --needed base-devel cmake ninja pipewire-jack"
  echo
  echo "(missing: ${missing[*]}), then run ./install.sh again."
  exit 1
fi

generator=()
command -v ninja >/dev/null && generator=(-G Ninja)

echo "==> Building in $build"
cmake -S "$src" -B "$build" "${generator[@]}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$prefix" \
  -DSG_BUILD_TESTS=OFF \
  -DSG_BUILD_PLUGINS=$([[ $plugins == 1 ]] && echo ON || echo OFF) >/dev/null
cmake --build "$build" --parallel

echo "==> Installing to $prefix"
cmake --install "$build" >/dev/null
mkdir -p "$data/soundgineer/factory" "$data/soundgineer/presets" "$data/systemd/user"
rm -f "$data/soundgineer/factory/"*.json
cp "$src"/presets/factory/*.json "$data/soundgineer/factory/"
install -m 0644 "$src/packaging/systemd/soundgineer.socket" "$data/systemd/user/soundgineer.socket"
install -m 0644 "$src/packaging/systemd/soundgineer.service" "$data/systemd/user/soundgineer.service"

if [[ -x "$prefix/bin/soundgineer" ]]; then
  mkdir -p "$data/applications"
  sed "s|^Exec=soundgineer|Exec=$prefix/bin/soundgineer|" "$src/app/soundgineer.desktop" > "$data/applications/soundgineer.desktop"
else
  echo "!!  Qt 6 Quick not found, so the Soundgineer window app was skipped (sudo pacman -S --needed qt6-declarative)."
fi

if [[ $plugins == 1 ]]; then
  echo "==> Installing VST3 / LV2 / standalone"
  mkdir -p "$HOME/.lv2" "$HOME/.vst3"
  rm -rf "$HOME/.lv2/soundgineer.lv2" "$HOME/.vst3/soundgineer.vst3"
  cp -r "$build/bin/soundgineer.lv2" "$HOME/.lv2/"
  cp -r "$build/bin/soundgineer.vst3" "$HOME/.vst3/"
  install -m 0755 "$build/bin/soundgineer" "$prefix/bin/soundgineer-standalone"
fi

if [[ $start == 1 ]]; then
  if systemctl --user daemon-reload 2>/dev/null; then
    systemctl --user restart soundgineer.service 2>/dev/null || true
    systemctl --user enable --now soundgineer.socket
    echo "==> soundgineer.socket enabled; the engine starts when the bar widget connects."
  else
    echo "!!  No systemd user session found. Start the engine by hand: $prefix/bin/soundgineer-engine"
  fi
fi

echo
echo "Done. Open Soundgineer from the app launcher (or run: soundgineer)."
echo "For the bar widget:"
echo "  omarchy plugin enable $(jq -r .id "$src/manifest.json" 2>/dev/null || echo '<plugin id>')"
echo "Try it from a terminal: sg-ctl load \"Super Saw Lead\" && sg-ctl note 60"
