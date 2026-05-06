#!/usr/bin/env bash
set -euo pipefail

# Notarize a DMG with Apple's notary service using a keychain profile.
#
# Usage:
#   scripts/notarize.sh
#   DMG_PATH=dist/Framelit.dmg scripts/notarize.sh

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

PROFILE="${PROFILE:-notarytool-profile}"
DMG_PATH="${DMG_PATH:-${ROOT_DIR}/Framelit.dmg}"

if [[ ! -f "${DMG_PATH}" ]]; then
	echo "error: DMG not found at ${DMG_PATH}" >&2
	echo "create it first (e.g. scripts/dmg.sh) or set DMG_PATH" >&2
	exit 1
fi

if ! xcrun notarytool history --keychain-profile "${PROFILE}" >/dev/null 2>&1; then
	echo "error: keychain profile '${PROFILE}' not found or not usable" >&2
	echo "create it first with notarytool store-credentials" >&2
	exit 1
fi

echo "Submitting ${DMG_PATH} for notarization with profile '${PROFILE}'..."
xcrun notarytool submit "${DMG_PATH}" --keychain-profile "${PROFILE}" --wait

echo "Stapling ticket to ${DMG_PATH}..."
xcrun stapler staple "${DMG_PATH}"

echo "Validating stapled ticket..."
xcrun stapler validate "${DMG_PATH}"

echo "Notarization complete: ${DMG_PATH}"
