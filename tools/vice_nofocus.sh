#!/bin/bash
# Install an x64sc that never takes the focus (macOS only).
#
#   tools/vice_nofocus.sh [vice dir]
#
# VICE comes to the front every time it starts, -minimized or not, so a batch
# of screenshots or the target test in `make prg` keeps pulling you out of
# whatever you were typing into. This puts a wrapper at
# ~/.local/vice-nofocus/bin/x64sc that starts VICE with its activation calls
# turned into no-ops (tools/vice_nofocus.m). Its window still shows briefly,
# but the keyboard stays where it was. vice_shot.sh and
# vice_dump.sh use it whenever it is there; nothing else changes, and VICE
# started from /Applications behaves as before.
#
# [vice dir] is the official build's folder, default the newest
# /Applications/vice-*. Run this again after upgrading VICE: the wrapper runs a
# copy of its x64sc binary, re-signed without the hardened runtime (which
# would refuse the injected library). Screenshots are byte-identical to stock.
set -eu

HERE=$(cd "$(dirname "$0")" && pwd -P)
VICE="${1:-$(ls -d /Applications/vice-*/ 2>/dev/null | tail -1)}"
VICE="${VICE%/}"
RES="$VICE/VICE.app/Contents/Resources"
[ -x "$RES/bin/x64sc" ] || { echo "vice_nofocus: no x64sc in $RES/bin" >&2; exit 1; }

DEST="$HOME/.local/vice-nofocus"
APP="$DEST/VICE.app/Contents/Resources"
mkdir -p "$DEST/bin" "$APP/bin"

# VICE finds its ROMs at bin/../share/vice, so the copy sits in the same
# layout as the original with everything but the binary linked back to it.
cp -f "$RES/bin/x64sc" "$APP/bin/x64sc"
chmod u+w "$APP/bin/x64sc"
xattr -d com.apple.quarantine "$APP/bin/x64sc" 2>/dev/null || true
codesign --force -s - "$APP/bin/x64sc"
for d in lib share etc; do ln -sfn "$RES/$d" "$APP/$d"; done

clang -dynamiclib -fobjc-arc -framework AppKit \
    -o "$DEST/libnofocus.dylib" "$HERE/vice_nofocus.m"

# The environment VICE.app's own launcher sets up (Contents/Resources/bin/*-runtime.sh).
cat > "$DEST/bin/x64sc" <<'EOF'
#!/bin/bash
# x64sc that never takes the focus; installed by tools/vice_nofocus.sh.
D="$(cd "$(dirname "$0")/.." && pwd -P)"
R="$D/VICE.app/Contents/Resources"
export XDG_CONFIG_DIRS="$R/etc/xdg" XDG_DATA_DIRS="$R/share"
export GTK_DATA_PREFIX="$R" GTK_EXE_PREFIX="$R" GTK_PATH="$R"
export GDK_PIXBUF_MODULE_FILE="$R/lib/gdk-pixbuf-2.0/2.10.0/loaders.cache"
export GTK_IM_MODULE_FILE="$R/lib/gtk-3.0/3.0.0/immodules.cache"
export LC_ALL=en_US
export DYLD_INSERT_LIBRARIES="$D/libnofocus.dylib"
exec "$R/bin/x64sc" -minimized "$@"
EOF
chmod +x "$DEST/bin/x64sc"

echo "installed $DEST/bin/x64sc (from $VICE)"
