#!/usr/bin/env bash
set -euo pipefail

# Build a Finder-customized macOS DMG for Framelit.
#
# Expected usage from repo root:
#   scripts/dmg.sh
#
# Optional overrides:
#   APP_PATH=/path/to/Framelit.app OUT_DMG=dist/Framelit.dmg scripts/dmg.sh

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

APP_NAME="Framelit"
VOL_NAME="Framelit"
APP_PATH="${APP_PATH:-${ROOT_DIR}/build/${APP_NAME}.app}"
OUT_DMG="${OUT_DMG:-${ROOT_DIR}/${APP_NAME}.dmg}"
BACKGROUND_IMG="${BACKGROUND_IMG:-${SCRIPT_DIR}/dmg-background.png}"

if [[ ! -d "${APP_PATH}" ]]; then
  echo "error: app not found at ${APP_PATH}" >&2
  echo "build first (e.g. cmake -B build && cmake --build build) or set APP_PATH" >&2
  exit 1
fi

if [[ ! -f "${BACKGROUND_IMG}" ]]; then
  echo "warning: background image not found at ${BACKGROUND_IMG}" >&2
  echo "warning: DMG will be created without custom Finder background" >&2
fi

WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/framelit-dmg.XXXXXX")"
STAGE_DIR="${WORK_DIR}/stage"
RW_DMG="${WORK_DIR}/${APP_NAME}-rw.dmg"
DEVICE=""

cleanup() {
  set +e
  if [[ -n "${DEVICE}" ]]; then
    hdiutil detach "${DEVICE}" >/dev/null 2>&1 || hdiutil detach -force "${DEVICE}" >/dev/null 2>&1
  fi
  rm -rf "${WORK_DIR}"
}
trap cleanup EXIT

mkdir -p "${STAGE_DIR}"
cp -R "${APP_PATH}" "${STAGE_DIR}/${APP_NAME}.app"
ln -s /Applications "${STAGE_DIR}/Applications"

if [[ -f "${BACKGROUND_IMG}" ]]; then
  mkdir -p "${STAGE_DIR}/.background"
  cp "${BACKGROUND_IMG}" "${STAGE_DIR}/.background/background.png"
fi

# Create read/write image so Finder can store icon positions and window settings.
hdiutil create \
  -volname "${VOL_NAME}" \
  -srcfolder "${STAGE_DIR}" \
  -fs HFS+ \
  -format UDRW \
  "${RW_DMG}" >/dev/null

DEVICE="$(hdiutil attach -readwrite -noverify -noautoopen "${RW_DMG}" | awk '/Apple_HFS/ {print $1; exit}')"
if [[ -z "${DEVICE}" ]]; then
  echo "error: failed to mount temporary DMG" >&2
  exit 1
fi

# Give Finder a moment to notice the mounted volume before scripting it.
sleep 1

HAS_BG="false"
if [[ -f "${BACKGROUND_IMG}" ]]; then
  HAS_BG="true"
fi

osascript <<APPLESCRIPT
tell application "Finder"
  tell disk "${VOL_NAME}"
    open
    set current view of container window to icon view
    set toolbar visible of container window to false
    set statusbar visible of container window to false
    set bounds of container window to {100, 100, 780, 520}

    set viewOptions to the icon view options of container window
    set arrangement of viewOptions to not arranged
    set icon size of viewOptions to 160
    set text size of viewOptions to 13

    if "${HAS_BG}" is "true" then
      set background picture of viewOptions to file ".background:background.png"
    end if

    set iconY to 170
    set position of item "${APP_NAME}.app" of container window to {180, iconY}
    set position of item "Applications" of container window to {500, iconY}

    close
    open
    update without registering applications
    delay 1
  end tell
end tell
APPLESCRIPT

# Ensure Finder metadata is flushed before converting.
sync
hdiutil detach "${DEVICE}" >/dev/null
DEVICE=""

mkdir -p "$(dirname "${OUT_DMG}")"
rm -f "${OUT_DMG}"
hdiutil convert "${RW_DMG}" -format UDZO -imagekey zlib-level=9 -o "${OUT_DMG}" >/dev/null

echo "Created ${OUT_DMG}"
if [[ -f "${BACKGROUND_IMG}" ]]; then
  echo "Using background image: ${BACKGROUND_IMG}"
else
  echo "Tip: add ${SCRIPT_DIR}/dmg-background.png with a left->right arrow for the classic app-to-Applications look."
fi
