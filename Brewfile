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
