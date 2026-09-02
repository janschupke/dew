#pragma once

namespace dew
{

/** What the DSP assumes before a device has told it anything.

    Every one of these sites is a fallback, not a choice: a filter, a tap or a
    transport that has been constructed but not yet prepared still has to hold a
    number, and every one of them held its own copy of 44100.0. Fifteen copies of
    a fallback is fifteen chances for one of them to be a different fallback.

    Deliberately NOT used for the render dialog's rate list, the MP3 rate check
    or Settings' stored render rate. Those describe which rates are LEGAL, which
    is a different question that happens to share a number - folding them in here
    would make "the rate we assume" and "the rates we accept" impossible to tell
    apart the next time one of them changes.
*/
inline constexpr double kDefaultSampleRate = 44100.0;

/** The block size an offline render uses, and what a not-yet-prepared engine
    sizes its scratch for. A live device supplies its own; see
    LiveAudioHost::preferredBufferSize, which is deliberately smaller.
*/
inline constexpr int kDefaultBlockSize = 512;

} // namespace dew
