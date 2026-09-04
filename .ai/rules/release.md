# Releasing

## Cutting one

1. Edit the version in [`CMakeLists.txt`](../../CMakeLists.txt) — `project(dew VERSION …)`
   for the triple, `DEW_VERSION_SUFFIX` for `-rc.1` or `-beta.2`, empty for a release.
2. `./scripts/check.sh`.
3. Commit, then `git tag v<version> && git push --follow-tags`.

That is the whole procedure. `.github/workflows/release.yml` does the rest: it refuses a
tag that disagrees with the file, builds macOS, Windows and Linux in parallel, packages
each with `./scripts/package.sh`, writes `SHA256SUMS.txt`, attests the lot through
Sigstore, and publishes a release that was a draft until every asset was attached.

`workflow_dispatch` with `dry_run` builds and packages all three and publishes nothing.
Use it after touching anything here.

## The version is declared in exactly two lines

`project(dew VERSION …)` and `DEW_VERSION_SUFFIX`, both in the top-level `CMakeLists.txt`,
and **nothing else may spell a version**. `DEW_VERSION_STRING` is the two concatenated and
is what `BuildInfo::version()` returns; `tests/BuildInfoTests.cpp` reads those two lines
and holds the running binary against them, and `release.yml` reads the same two lines and
holds the git tag against them.

The suffix is deliberately not a cache variable, so `-DDEW_VERSION_SUFFIX=` cannot make a
binary disagree with the file the tag is checked against. It also never reaches
`juce_add_gui_app`'s `VERSION` argument: `CFBundleShortVersionString` is specified as a
numeric triple.

SemVer, `v`-prefixed tags, `0.x` while the status line says working prototype. A hyphen in
the version makes the release a prerelease, and GitHub keeps prereleases out of
`releases/latest` — which is what makes an `-rc` rehearsal invisible to the website.

## Asset names carry no version

`dew-macos-universal.dmg`, not `dew-0.1.0-macos.dmg`. The website links
`releases/latest/download/<asset>`, which GitHub resolves server-side, so the download
buttons need no API call, no generated file and no rebuild — and they cannot advertise a
version whose builds have not finished. `website/tests/download.test.ts` holds every name
on the page against `release.yml`.

## Packaging is a script, not YAML

`./scripts/package.sh` turns a `dist` build into `dist/`. The workflow runs exactly that,
so a packaging failure can be reproduced on a laptop rather than by pushing a tag.

`DEW_WRITE_THIRD_PARTY=OFF` in the `dist` preset. The manifest is written into the SOURCE
tree at configure time and records the toolchain that wrote it, so a packaging build would
otherwise leave the tree dirty for a reason that has nothing to do with the dependencies.

## Signing is guarded, and today there is nothing to guard

Every signing step is skipped when its secret is absent, in `package.sh` as well as in the
workflow, so a fork and a laptop behave the way CI does. Buying a certificate is a secrets
change and no edit here:

| Secret | What it is |
| --- | --- |
| `MACOS_CERT_P12`, `MACOS_CERT_PASSWORD` | a Developer ID Application certificate, base64 |
| `MACOS_SIGN_IDENTITY` | `Developer ID Application: Name (TEAMID)` |
| `MACOS_NOTARY_KEY`, `MACOS_NOTARY_KEY_ID`, `MACOS_NOTARY_ISSUER` | an App Store Connect API key, base64, and its two ids |

**Nothing is signed today, and that has a cost worth knowing before it surprises a user.**
macOS 15 removed the Control-click bypass, so an unsigned build sends its user to System
Settings → Privacy & Security → Open Anyway, with an admin password, within about an hour
of the refusal. Signing *without* notarizing buys nothing — the dialog is identical.
Windows shows SmartScreen's "Windows protected your PC", and a machine with Smart App
Control on blocks the build outright rather than warning about it.

The macOS bundle is ad-hoc signed regardless. That buys no Gatekeeper relief at all; it is
what makes an arm64 binary runnable, and a universal link invalidates the signature the
linker applies by itself.

## Where the artefacts live

GitHub Releases, and nowhere else. Free, no bandwidth charge, 2 GiB a file, and every tag
keeps its assets indefinitely — so the archive of old versions is a thing that exists
rather than a thing to build.

## Why it is this way

**The tag is the trigger and the file is the truth.** Deriving the version from the tag
would mean a binary built from a branch cannot say what it is, and deriving the tag from
nothing would mean the two can disagree. So both are read from the same two lines and a
job compares them before three builds are spent.

**The website may not fetch.** `website/next.config.ts` states that every input the site
has is a committed file; a browser-side call to `api.github.com` would break that and is a
bad idea anyway, since the limit is sixty requests an hour keyed on the **IP** — one
office behind one address exhausts it for everybody in it. Version-free asset names remove
the question: the link is static, correct, and never has a window where it is wrong.

**Linux builds in a container.** `ubuntu:22.04`, not the runner image, so the glibc the
AppImage is built against is pinned rather than moving the day GitHub retires an image.
An AppImage built against a newer glibc silently stops running on older distributions.

**Windows uses the Visual Studio generator.** The `dist-windows` preset differs from
`dist` in that one thing. Ninja needs `cl.exe` on `PATH`, which means a developer command
prompt — either a third-party action to run `vcvars`, or a hard-coded path that differs
between runner images. The Visual Studio generator finds its own toolchain.

**Inno Setup rather than NSIS.** NSIS is not on the `windows-2025` image, which is what
`windows-latest` now means, and CPack's NSIS generator exposes no execution level, so a
per-user install would mean overriding its template. Inno Setup is on both images and
takes any `[Setup]` directive, so `PrivilegesRequired=lowest` is one line. Per-user
matters: an unsigned installer that also asks for administrator gets two alarming dialogs
where it could have had one.

**`actions/attest`, not `actions/attest-build-provenance`,** which since v4 is only a
wrapper over it and whose own README says new work should use it directly. Its subject is
`SHA256SUMS.txt`, so one step covers every asset. No `cosign sign-blob` beside it: same
Sigstore instance, same transparency log, one guarantee paid for twice — and `cosign
verify-blob-attestation --new-bundle-format` already reads what this produces.

**No nightlies.** They would spend macOS runner minutes every day for an audience that can
build the tree in three commands. If they ever arrive, they need **dated** tags rather
than a rolling `Nightly` one, because immutable releases forbid moving a tag.
