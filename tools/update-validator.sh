#!/bin/bash
# Refresh tools/omarchy-plugin-validate from an omarchy checkout on the quattro branch.
# Usage: tools/update-validator.sh /path/to/omarchy
set -euo pipefail
src="${1:?usage: $0 /path/to/omarchy}/bin/omarchy-plugin-validate"
dest="$(dirname "$0")/omarchy-plugin-validate"
[[ -f $src ]] || { echo "not found: $src" >&2; exit 1; }
{
  echo '#!/bin/bash'
  echo
  echo '# Vendored from omacom/omarchy (quattro branch), bin/omarchy-plugin-validate.'
  echo '# Copyright (c) David Heinemeier Hansson. MIT License, see'
  echo '# https://github.com/omacom/omarchy/blob/quattro/LICENSE'
  echo '# Used in CI so the plugin is checked exactly as `omarchy plugin validate` would.'
  echo '# Refresh with tools/update-validator.sh when the upstream schema changes.'
  tail -n +2 "$src"
} > "$dest"
chmod +x "$dest"
echo "updated $dest"
