#!/usr/bin/env bash
# Remove what install.sh installed. User presets ($XDG_DATA_HOME/soundgineer/
# presets) and favorites/session state are left untouched. No sudo.
set -euo pipefail

prefix="$HOME/.local"
data="${XDG_DATA_HOME:-$HOME/.local/share}"

systemctl --user disable --now soundgineer.socket 2>/dev/null || true
systemctl --user stop soundgineer.service 2>/dev/null || true
rm -f "$data/systemd/user/soundgineer.socket" "$data/systemd/user/soundgineer.service"
systemctl --user daemon-reload 2>/dev/null || true

rm -f "$data/applications/soundgineer.desktop" "$prefix/bin/soundgineer"
rm -f "$prefix/bin/soundgineer-engine" "$prefix/bin/sg-render" "$prefix/bin/sg-ctl" "$prefix/bin/soundgineer-standalone"
rm -rf "$data/soundgineer/factory"
rm -rf "$HOME/.lv2/soundgineer.lv2" "$HOME/.vst3/soundgineer.vst3"
rm -rf "${XDG_CACHE_HOME:-$HOME/.cache}/soundgineer"

echo "Soundgineer engine removed. Your presets in $data/soundgineer/presets were kept."
echo "To remove the bar widget too: omarchy plugin remove <plugin id>"
