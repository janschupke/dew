# Host toolchain for dew.
#
# Run `brew bundle` to install. Homebrew does not pin versions cleanly, so this
# file fixes the tool *set*, not exact versions; the versions actually used are
# recorded in THIRD_PARTY.md at configure time and in CI logs.
#
#   cmake  >= 3.25 required (JUCE 9 needs 3.22; CMakePresets v6 needs 3.25)
#   ninja  the generator every preset uses
#   ccache compile cache - optional, but a cold JUCE rebuild is minutes without it

brew "cmake"
brew "ninja"
brew "ccache"

# MP3 export. dew ships no LAME code: juce::LAMEEncoderAudioFormat drives this
# binary as a child process, which is why MP3 needs no build dependency and
# raises no LGPL question. Without it the app simply greys the MP3 option out,
# so this is the one entry here that is optional.
brew "lame"

# The website. dew's own build needs none of this - the site reads committed
# JSON and committed PNGs, so `cmake --build --preset ci` never touches node -
# but ./scripts/check.sh checks the site when node is present, and says loudly
# when it is not.
#
# npm rather than pnpm or yarn: it ships with node, so this one line is the
# whole bootstrap, and actions/setup-node caches it with no third-party action
# to pin. Unpinned like everything else here - this file fixes the tool SET.
# Which node is website/.nvmrc's job, and that is the one CI reads.
brew "node"
