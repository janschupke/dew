#include "control/ControlGuide.h"

namespace dew::control
{

const std::vector<GuideSection>& guide()
{
    static const std::vector<GuideSection> pages {
        { "index",
          "Driving dew",
          "What dew is, and which tool answers which question.",
          { "dew is a desktop DAW in the FL Studio shape. A project holds CHANNELS, each "
            "carrying one instrument - a three-oscillator synth, a recording, or a "
            "soundfont - and its own chain of effects. PATTERNS hold notes. The PLAYLIST "
            "arranges clips of those patterns into a song. A MIXER routes channels "
            "through inserts to a master.",

            "Start with project_describe. It is the cheap read: the tempo, the metre, and "
            "every channel, pattern, lane, insert and automation by name and id, with not "
            "one note in it. Everything else is addressed by the ids it gives you.",

            "Then: params_list and params_write reach every value in the document - a "
            "volume, an oscillator's detune, an envelope, any effect's anything, a fader, "
            "the tempo. channels_write, effects_write and mixer_write add and arrange the "
            "objects those values live on. notes_write and clips_write are the piano roll "
            "and the arrangement. score_compile is the fast way to write a lot of music at "
            "once.",

            "Read dew://guide/addressing before writing anything, and dew://guide/undo "
            "before writing a lot of it." } },

        { "addressing",
          "How things are addressed",
          "Ids, indexes, and the five fields that name any parameter.",
          { "Channels, patterns, mixer inserts and automations carry IDS, and an id is "
            "stable for as long as the thing exists. Playlist lanes do not: lanes are "
            "positional, so a lane is addressed by its index, and removing one renumbers "
            "the lanes after it. Clips are addressed by the lane and the bar they sit on, "
            "which is how they are identified on screen.",

            "Every parameter in the project is addressed by the same five fields. `target` "
            "is project, channel, mixerTrack or master. `id` is the channel or mixer track "
            "id. `group` says which part of the thing: leave it empty for the target's own "
            "parameters, or give oscillators, amp, sample, soundfont or effects. `slot` "
            "picks which oscillator or which effect. `param` is the parameter's name.",

            "What a channel offers depends on what instrument it carries, which "
            "project_describe reports as its source: a synth has oscillators and an amp "
            "envelope, an audio channel has a sample, a soundfont channel has a soundfont. "
            "Call params_list at an address to see exactly what is there, with each "
            "parameter's range, its default and whether it can be automated.",

            "automation_targets_list answers in the same five fields, so a parameter you "
            "have just set can be automated without translating anything. Not every "
            "parameter can be: whether one is worth a curve is declared beside the "
            "parameter itself." } },

        { "arrangement",
          "Notes, patterns and the arrangement",
          "Steps and bars, and why a pattern is not an instrument.",
          { "Time inside a pattern is whole STEPS. A step is one over stepsPerBeat of a "
            "beat - at the default of 4, a step is a sixteenth note. There is no "
            "fractional step, so a rhythm the grid does not divide cannot be written; "
            "raising stepsPerBeat is how you get triplets and finer values.",

            "A PATTERN holds every channel's notes for its span. It is a section of the "
            "song rather than one instrument's part, which is the thing most likely to "
            "surprise you: a drum pattern and a bass pattern covering the same four bars "
            "are usually one pattern, not two.",

            "The PLAYLIST is measured in BARS, and a bar is beatsPerBar steps. Placing two "
            "clips of one pattern is how a section repeats identically. Duplicating the "
            "pattern first, with patterns_write and duplicateOf, is how it repeats with "
            "variation. A lane can carry a clip of any pattern; lanes are not instruments "
            "either.",

            "Changing the metre redefines what a bar IS, so every clip's start and length "
            "is rescaled in the same undo step to hold its position in time. The answer "
            "says whether every clip landed on a whole bar.",

            "Patterns and the song both GROW to fit what you write and never shrink. "
            "Trailing empty bars and a pattern longer than its notes are a deliberate "
            "silence, and nothing trims one behind your back." } },

        { "score",
          "The score language",
          "When to write music as text instead of as notes.",
          { "dew carries a declarative language for arrangement. You say what the music IS "
            "- a key, a harmonic progression, rhythms, voicings, a counterpoint - and the "
            "compiler works out the notes, the patterns and the clips. For anything longer "
            "than a few bars this is worth far more per call than placing notes one at a "
            "time.",

            "score_write stores the text and compiles nothing; score_compile turns it into "
            "real notes as one undo step. They are separate because a compile writes notes, "
            "and text being worked on should not fill the undo stack with them. Both report "
            "diagnostics with a code, a line and a column.",

            "The language owns notes, patterns and clips. You own channels, instruments, "
            "effects and the mixer - so set up the sounds with the other tools, and let the "
            "score put the music on them. Recompiling is an update rather than a second "
            "copy, and edits made by hand are kept unless you ask for them to be discarded.",

            "Three things it cannot say, because the host cannot hold them: time is whole "
            "steps, so a rhythm needing a finer grid than the project has is an error "
            "naming both offenders; there is one tempo and one metre for the whole project, "
            "so a section in another metre is not expressible; and a pattern holds every "
            "channel's notes for its span.",

            "The same source always compiles to the same notes, on any machine. The full "
            "reference is on the website." } },

        { "undo",
          "Changes, undo and consent",
          "What a grant covers, and what protects the user.",
          { "Every operation that writes is exactly ONE undo step, however many entries its "
            "batch carried. Two hundred notes in one notes_write call is one press of "
            "Cmd-Z. That is deliberate, and it is the user's real protection: they approved "
            "a client, not each of its actions, so being able to take back an action whole "
            "is what makes the approval reasonable.",

            "Prefer one batched call over many small ones for the same reason, and not only "
            "for speed. Ten separate calls are ten undo steps and ten chances to leave the "
            "document half-changed.",

            "A batch is checked before any of it is written. An entry naming a channel that "
            "does not exist fails the whole call and changes nothing, rather than applying "
            "the entries it understood - half a batch is the worst answer available, "
            "because the caller is told it failed and the document has moved anyway.",

            "A grant is read-only or read-and-write, chosen by the user when they approved "
            "the client. Under a read-only grant every writing operation is refused before "
            "it runs. project_command can undo and redo, so a client can take back its own "
            "work - and so can the person at the keyboard.",

            "Nothing here can approve itself, change a grant, or reach outside the open "
            "project except by writing the files that render_audio and export_midi are "
            "asked for." } }
    };

    return pages;
}

const GuideSection* findGuideSection (juce::StringRef id)
{
    for (const auto& section : guide())
        if (id == juce::StringRef (section.id))
            return &section;

    return nullptr;
}

juce::String guideMarkdown (const GuideSection& section)
{
    juce::String text;
    text << "# " << section.title << "\n\n";

    for (const auto* paragraph : section.paragraphs)
        text << paragraph << "\n\n";

    return text.trimEnd() + "\n";
}

} // namespace dew::control
