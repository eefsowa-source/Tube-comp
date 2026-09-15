#!/usr/bin/env bash
set -euo pipefail

PRODUCT_NAME="EON-Vari mu DSP"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

AU_SOURCE="${SCRIPT_DIR}/AU/${PRODUCT_NAME}.component"
VST3_SOURCE="${SCRIPT_DIR}/VST3/${PRODUCT_NAME}.vst3"
AU_DESTINATION="${HOME}/Library/Audio/Plug-Ins/Components/${PRODUCT_NAME}.component"
VST3_DESTINATION="${HOME}/Library/Audio/Plug-Ins/VST3/${PRODUCT_NAME}.vst3"

fail() {
    echo "Installation failed: $1" >&2
    exit 1
}

verifyBundle() {
    local bundle="$1"
    local executableName

    [[ -d "${bundle}" ]] || fail "Missing bundle: ${bundle}"
    [[ -f "${bundle}/Contents/Info.plist" ]] || fail "Invalid bundle: ${bundle}"

    executableName="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "${bundle}/Contents/Info.plist" 2>/dev/null)" \
        || fail "Could not read bundle metadata: ${bundle}"
    [[ -x "${bundle}/Contents/MacOS/${executableName}" ]] \
        || fail "Bundle executable is missing: ${bundle}"

    /usr/bin/codesign --verify --deep --strict "${bundle}" \
        || fail "Code-signature verification failed: ${bundle}"
}

installBundle() {
    local sourceBundle="$1"
    local destinationBundle="$2"

    /bin/mkdir -p "$(/usr/bin/dirname "${destinationBundle}")"

    # Cleanly replace only this exact same-name bundle, without prompting.
    /bin/rm -rf -- "${destinationBundle}"
    /usr/bin/ditto "${sourceBundle}" "${destinationBundle}"
    verifyBundle "${destinationBundle}"
}

echo "Installing ${PRODUCT_NAME}..."
verifyBundle "${AU_SOURCE}"
verifyBundle "${VST3_SOURCE}"

installBundle "${AU_SOURCE}" "${AU_DESTINATION}"
installBundle "${VST3_SOURCE}" "${VST3_DESTINATION}"

echo
echo "Installed successfully. Restart or rescan your DAW to load ${PRODUCT_NAME}."
