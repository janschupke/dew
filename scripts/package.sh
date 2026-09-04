#!/usr/bin/env bash
# ==============================================================================
# Turns a `dist` build into the files a person downloads.
#
#     cmake --preset dist && cmake --build --preset dist
#     ./scripts/package.sh                    # reads build/dist
#     ./scripts/package.sh build/dist-windows # or wherever it was built
#
# Everything lands in dist/.
#
# One script for three platforms, for the reason check.sh is one command: the
# release workflow runs exactly what a person can run on their own machine. A
# packaging step that only exists as YAML is one nobody can debug except by
# pushing a tag.
#
# NAMES CARRY NO VERSION, deliberately. The website links
# releases/latest/download/<name>, which GitHub resolves server-side to the
# newest release holding an asset of that name - so the download button needs no
# API call and no generated file, and it cannot advertise a build that has not
# finished. The version is in the DMG's volume name, in the installer, and in
# the binary. See .ai/rules/release.md.
#
# SIGNING IS GUARDED on the certificate being there, and the guard is here
# rather than only in the workflow so that both behave the same way. With no
# certificate this produces an unsigned artefact and says so; with one it
# produces a signed and notarized artefact by the same path.
# ==============================================================================
set -euo pipefail

cd "$(dirname "$0")/.."

BUILD_DIR=${1:-build/dist}
OUT_DIR=$PWD/dist

[[ -d $BUILD_DIR ]] || { echo "FAIL  no build at ${BUILD_DIR} - run cmake --preset dist first"; exit 1; }

mkdir -p "$OUT_DIR"

step() { printf '\n\033[1m==> %s\033[0m\n' "$*"; }

# The version, from the two lines that declare it. Comments are skipped for the
# reason BuildInfoTests skips them: the lines above both declarations talk about
# version numbers.
version=$(sed -n 's/^project(dew VERSION \([0-9][0-9.]*\).*/\1/p' CMakeLists.txt)
suffix=$(sed -n 's/^set(DEW_VERSION_SUFFIX "\(.*\)")$/\1/p' CMakeLists.txt)
VERSION="${version}${suffix}"

[[ -n $version ]] || { echo "FAIL  could not read the version out of CMakeLists.txt"; exit 1; }

echo "dew ${VERSION}, from ${BUILD_DIR}"

# cpack, once per generator. CPack has no per-generator name variable and the
# two Windows artefacts need two different names out of one build, so the name
# is passed per invocation rather than set once in Packaging.cmake.
pack() {
  local generator=$1 name=$2
  cpack --config "${BUILD_DIR}/CPackConfig.cmake" \
        -G "${generator}" \
        -C RelWithDebInfo \
        -B "${OUT_DIR}" \
        -D CPACK_PACKAGE_FILE_NAME="${name}"
}

case "$(uname -s)" in

  # ----------------------------------------------------------------------------
  Darwin)
    APP="${BUILD_DIR}/src/dew_artefacts/RelWithDebInfo/dew.app"
    [[ -d $APP ]] || { echo "FAIL  no bundle at ${APP}"; exit 1; }

    if [[ -n ${MACOS_SIGN_IDENTITY:-} ]]; then
      step "signing with ${MACOS_SIGN_IDENTITY}"

      # Inside out, and NOT --deep, which Apple deprecates for signing: every
      # nested binary is signed on its own terms and the bundle last, so the
      # bundle's seal covers signatures that are already final.
      while IFS= read -r -d '' nested; do
        codesign --force --timestamp --options runtime \
                 --sign "${MACOS_SIGN_IDENTITY}" "$nested"
      done < <(find "${APP}/Contents" -type f -perm -u+x -print0)

      codesign --force --timestamp --options runtime \
               --sign "${MACOS_SIGN_IDENTITY}" "${APP}"
      codesign --verify --strict --verbose=2 "${APP}"
    else
      # Ad hoc, which is not nothing: an arm64 binary with no signature at all
      # will not execute, and building for two architectures invalidates the
      # signature the linker applies by itself. It buys the user no Gatekeeper
      # relief - see the download page - it only makes the program runnable.
      step "ad-hoc signing (no MACOS_SIGN_IDENTITY - this build is not notarized)"
      codesign --force --deep --sign - "${APP}"
    fi

    step "dmg"
    pack DragNDrop dew-macos-universal

    DMG="${OUT_DIR}/dew-macos-universal.dmg"

    if [[ -n ${MACOS_SIGN_IDENTITY:-} ]]; then
      codesign --force --timestamp --sign "${MACOS_SIGN_IDENTITY}" "${DMG}"

      if [[ -n ${MACOS_NOTARY_KEY_FILE:-} ]]; then
        step "notarizing"
        xcrun notarytool submit "${DMG}" --wait \
              --key "${MACOS_NOTARY_KEY_FILE}" \
              --key-id "${MACOS_NOTARY_KEY_ID}" \
              --issuer "${MACOS_NOTARY_ISSUER}"
        xcrun stapler staple "${DMG}"
      else
        echo "note  signed but NOT notarized - Gatekeeper will still refuse the first launch"
      fi
    fi
    ;;

  # ----------------------------------------------------------------------------
  Linux)
    step "tarball"
    arch=$(uname -m)
    pack TGZ "dew-linux-${arch}"

    step "appimage"

    # Pinned to a release and checked against its hash, for the reason every
    # dependency in this tree is: `continuous` is a tag upstream moves, and a
    # build tool that can change under us is one that can change what ships.
    LINUXDEPLOY_TAG=1-alpha-20251107-1
    case "$arch" in
      x86_64)  LINUXDEPLOY_SHA=c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d ;;
      aarch64) LINUXDEPLOY_SHA=620095110d693282b8ebeb244a95b5e911cf8f65f76c88b4b47d16ae6346fcff ;;
      *) echo "FAIL  no linuxdeploy pinned for ${arch}"; exit 1 ;;
    esac

    TOOL_DIR=${TMPDIR:-/tmp}/dew-linuxdeploy
    TOOL="${TOOL_DIR}/linuxdeploy-${arch}.AppImage"
    mkdir -p "$TOOL_DIR"

    if [[ ! -f $TOOL ]]; then
      curl -fsSL -o "$TOOL" \
        "https://github.com/linuxdeploy/linuxdeploy/releases/download/${LINUXDEPLOY_TAG}/linuxdeploy-${arch}.AppImage"
    fi

    echo "${LINUXDEPLOY_SHA}  ${TOOL}" | sha256sum -c - \
      || { echo "FAIL  linuxdeploy does not match its pinned hash"; exit 1; }
    chmod +x "$TOOL"

    APPDIR="${OUT_DIR}/AppDir"
    rm -rf "$APPDIR"
    cmake --install "${BUILD_DIR}" --prefix "${APPDIR}/usr" >/dev/null

    # linuxdeploy wants a desktop entry and an icon, and refuses without them.
    # Written here rather than committed because every field in it is already
    # declared somewhere else - the name, the version, the .dew document type -
    # and a committed copy would be a third place to forget.
    mkdir -p "${APPDIR}/usr/share/applications" \
             "${APPDIR}/usr/share/icons/hicolor/scalable/apps"

    cp resources/icon/dew.svg "${APPDIR}/usr/share/icons/hicolor/scalable/apps/dew.svg"

    cat > "${APPDIR}/usr/share/applications/dew.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=dew
GenericName=Digital Audio Workstation
Comment=A desktop digital synth DAW
Exec=dew %f
Icon=dew
Categories=AudioVideo;Audio;AudioVideoEditing;
MimeType=application/x-dew;
Terminal=false
DESKTOP

    # No FUSE in a container or on most CI runners, so the tool unpacks itself
    # rather than mounting itself.
    # No update information. It would name a .zsync this release does not
    # publish, and an updater pointed at a file that is not there is worse than
    # no updater. EXTRACT_AND_RUN because a container has no FUSE.
    APPIMAGE_EXTRACT_AND_RUN=1 \
    OUTPUT="${OUT_DIR}/dew-linux-${arch}.AppImage" \
      "$TOOL" --appdir "$APPDIR" --output appimage \
              --executable "${APPDIR}/usr/bin/dew" \
              --desktop-file "${APPDIR}/usr/share/applications/dew.desktop" \
              --icon-file "${APPDIR}/usr/share/icons/hicolor/scalable/apps/dew.svg"

    rm -rf "$APPDIR"
    ;;

  # ----------------------------------------------------------------------------
  MINGW* | MSYS* | CYGWIN*)
    step "installer"
    pack INNOSETUP dew-windows-x64-setup

    step "portable zip"
    pack ZIP dew-windows-x64
    ;;

  *)
    echo "FAIL  no packaging defined for $(uname -s)"
    exit 1
    ;;
esac

# CPack leaves its staging trees behind in -B.
find "$OUT_DIR" -mindepth 1 -maxdepth 1 -name '_CPack_Packages' -exec rm -rf {} +

step "packaged"
ls -lh "$OUT_DIR"
