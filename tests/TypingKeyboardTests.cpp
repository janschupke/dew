#include <catch2/catch_test_macros.hpp>

#include "app/ProjectDocument.h"
#include "engine/AudioEngine.h"
#include "model/ProjectFactory.h"
#include "ui/EditorState.h"
#include "ui/TypingKeyboard.h"
#include "ui/TypingKeys.h"
#include "app/Settings.h"
#include "TestSupport.h"

#include <set>

using namespace dew;

namespace
{

/** A KeyPress the way a real one arrives: a code, and the character it typed.

    Both halves, because keys::matches deliberately reads either - a KeyPress
    built from a code alone carries no text character, and the punctuation at
    the end of each row is exactly where the two disagree on some layouts.
*/
juce::KeyPress pressOf (int character, int modifiers = 0)
{
    return juce::KeyPress (character, juce::ModifierKeys (modifiers), (juce::juce_wchar) character);
}

struct Fixture
{
    Fixture()
    {
        document.setState (ProjectFactory::createDefault(), true);
        editorState.setSelectedChannelId (1);
        keyboard.setEnabled (true);
    }

    /** The keys the fake keyboard is holding down. */
    TypingKeyboard::KeyStateSource holding (std::set<int> down) const
    {
        return [keys = std::move (down)] (int code) { return keys.count (code) > 0; };
    }

    juce::ScopedJuceInitialiser_GUI juceInit;
    ProjectDocument document;
    AudioEngine engine;
    EditorState editorState;
    TypingKeyboard keyboard { document, engine, editorState };
};

} // namespace

TEST_CASE ("the letter rows are a piano keyboard", "[ui][keys][typing]")
{
    // Octave 4 puts the bottom-left key at middle C, which is 60 - the same
    // relationship FL Studio ships, and the reason the default is 4.
    CHECK (typingKeys::pitchFor (typingKeys::semitoneFor (pressOf ('z')), 4) == 60);
    CHECK (typingKeys::pitchFor (typingKeys::semitoneFor (pressOf ('x')), 4) == 62);
    CHECK (typingKeys::pitchFor (typingKeys::semitoneFor (pressOf ('s')), 4) == 61);

    // The upper row is one octave above the lower.
    CHECK (typingKeys::pitchFor (typingKeys::semitoneFor (pressOf ('q')), 4) == 72);

    // ...and the two rows OVERLAP: the top of the bottom row is the bottom of
    // the top one, which is what lets a phrase cross the seam.
    CHECK (typingKeys::semitoneFor (pressOf (',')) == typingKeys::semitoneFor (pressOf ('q')));

    // A key that is not on the keyboard says so rather than answering zero.
    CHECK (typingKeys::semitoneFor (pressOf ('a')) == -1);
    CHECK (typingKeys::semitoneFor (pressOf ('f')) == -1);
    CHECK (typingKeys::semitoneFor (pressOf (juce::KeyPress::spaceKey)) == -1);
}

TEST_CASE ("every key in the map is on the keyboard exactly once", "[ui][keys][typing]")
{
    std::set<int> seen;

    for (const auto& note : typingKeys::table())
    {
        INFO ("key " << juce::String::charToString ((juce::juce_wchar) note.keyCode));
        CHECK (seen.insert (note.keyCode).second);
        CHECK (note.semitone >= 0);
        CHECK (note.semitone <= typingKeys::highestSemitone);
    }

    // The two octave keys are not notes, or moving the keyboard would play one.
    CHECK (seen.count (typingKeys::octaveDownKey) == 0);
    CHECK (seen.count (typingKeys::octaveUpKey) == 0);

    // The whole map fits inside MIDI at the highest octave it can be moved to.
    CHECK (typingKeys::pitchFor (typingKeys::highestSemitone, typingKeys::highestOctave) > 0);
}

TEST_CASE ("a modified key is never a note", "[ui][keys][typing]")
{
    Fixture f;

    // Every modifier, including SHIFT - which keys::matches ignores by design,
    // and which this has to compare for itself. That is what keeps shift-R
    // opening Randomize while the mode is on.
    CHECK_FALSE (f.keyboard.handleKeyPress (pressOf ('z', juce::ModifierKeys::commandModifier)));
    CHECK_FALSE (f.keyboard.handleKeyPress (pressOf ('r', juce::ModifierKeys::shiftModifier)));
    CHECK_FALSE (f.keyboard.handleKeyPress (pressOf ('q', juce::ModifierKeys::altModifier)));
    CHECK_FALSE (f.keyboard.handleKeyPress (pressOf ('1', juce::ModifierKeys::ctrlModifier)));

    // ...and the control case: bare, the same keys ARE notes, so the four above
    // are declined for their modifier rather than because nothing works.
    CHECK (f.keyboard.handleKeyPress (pressOf ('z')));
    CHECK (f.keyboard.handleKeyPress (pressOf ('r')));
}

TEST_CASE ("a mapped key is consumed, and an unmapped one is not", "[ui][keys][typing]")
{
    Fixture f;

    // The keys that mean something else in an editor: `q` quantizes, `r`
    // records and `0` zooms to fit. While the mode is on, this owns all three -
    // which is the whole point of it being a mode rather than a binding.
    CHECK (f.keyboard.handleKeyPress (pressOf ('q')));
    CHECK (f.keyboard.handleKeyPress (pressOf ('r')));
    CHECK (f.keyboard.handleKeyPress (pressOf ('0')));

    // `1` is NOT in the map - the upper row's black keys are 2 3 5 6 7 9 0, so
    // 1, 4 and 8 fall between the pairs and triples of a keyboard - which means
    // the piano roll's select tool still answers to it while the mode is on.
    CHECK_FALSE (f.keyboard.handleKeyPress (pressOf ('1')));
    CHECK_FALSE (f.keyboard.handleKeyPress (pressOf ('4')));
    CHECK_FALSE (f.keyboard.handleKeyPress (pressOf ('8')));

    // Space and Home are not in the map, so play and rewind still work.
    CHECK_FALSE (f.keyboard.handleKeyPress (pressOf (juce::KeyPress::spaceKey)));
    CHECK_FALSE (f.keyboard.handleKeyPress (pressOf (juce::KeyPress::homeKey)));
}

TEST_CASE ("nothing is a note while the mode is off", "[ui][keys][typing]")
{
    Fixture f;
    f.keyboard.setEnabled (false);

    CHECK_FALSE (f.keyboard.handleKeyPress (pressOf ('z')));
    CHECK_FALSE (f.keyboard.handleKeyPress (pressOf (typingKeys::octaveUpKey)));

    f.keyboard.refreshHeldKeys (f.holding ({ 'z', 'x' }));
    CHECK (f.keyboard.getSoundingCount() == 0);
}

TEST_CASE ("the brackets move the keyboard and stop at the ends", "[ui][keys][typing]")
{
    Fixture f;

    auto announced = 0;
    auto lastOctave = -1;
    f.keyboard.onOctaveChanged = [&] (int octave)
    {
        ++announced;
        lastOctave = octave;
    };

    REQUIRE (f.keyboard.getOctave() == typingKeys::defaultOctave);

    CHECK (f.keyboard.handleKeyPress (pressOf (typingKeys::octaveUpKey)));
    CHECK (f.keyboard.getOctave() == typingKeys::defaultOctave + 1);
    CHECK (announced == 1);
    CHECK (lastOctave == typingKeys::defaultOctave + 1);

    CHECK (f.keyboard.handleKeyPress (pressOf (typingKeys::octaveDownKey)));
    CHECK (f.keyboard.getOctave() == typingKeys::defaultOctave);

    for (int i = 0; i < 20; ++i)
        f.keyboard.handleKeyPress (pressOf (typingKeys::octaveUpKey));

    CHECK (f.keyboard.getOctave() == typingKeys::highestOctave);

    for (int i = 0; i < 20; ++i)
        f.keyboard.handleKeyPress (pressOf (typingKeys::octaveDownKey));

    CHECK (f.keyboard.getOctave() == typingKeys::lowestOctave);

    // The key is consumed at the end of its travel too: it is still this mode's
    // key, and letting it fall through would open a menu at one octave and not
    // at another.
    CHECK (f.keyboard.handleKeyPress (pressOf (typingKeys::octaveDownKey)));
}

TEST_CASE ("holding two keys for the same note keeps it sounding", "[ui][keys][typing]")
{
    Fixture f;

    // `,` and `q` are both the octave above the row's base.
    f.keyboard.refreshHeldKeys (f.holding ({ ',', 'q' }));
    CHECK (f.keyboard.getSoundingCount() == 1);

    // Letting one of them go must not silence the other.
    f.keyboard.refreshHeldKeys (f.holding ({ 'q' }));
    CHECK (f.keyboard.getSoundingCount() == 1);

    f.keyboard.refreshHeldKeys (f.holding ({}));
    CHECK (f.keyboard.getSoundingCount() == 0);
}

TEST_CASE ("re-reading the same keys sounds nothing new", "[ui][keys][typing]")
{
    Fixture f;

    const auto held = f.holding ({ 'z', 'x', 'c' });

    f.keyboard.refreshHeldKeys (held);
    REQUIRE (f.keyboard.getSoundingCount() == 3);

    // Idempotent, which is what makes it safe for the same object to be reached
    // twice in one key dispatch - once at the focused component and again at
    // the window.
    f.keyboard.refreshHeldKeys (held);
    CHECK (f.keyboard.getSoundingCount() == 3);
}

TEST_CASE ("moving the octave lets go of what is held", "[ui][keys][typing]")
{
    Fixture f;

    f.keyboard.refreshHeldKeys (f.holding ({ 'z' }));
    REQUIRE (f.keyboard.getSoundingCount() == 1);

    // The pitch under the finger is about to be a different one, and the note
    // that is sounding would have no note-off ahead of it.
    f.keyboard.handleKeyPress (pressOf (typingKeys::octaveUpKey));
    CHECK (f.keyboard.getSoundingCount() == 0);
}

TEST_CASE ("switching the mode off lets go of what is held", "[ui][keys][typing]")
{
    Fixture f;

    f.keyboard.refreshHeldKeys (f.holding ({ 'z', 'x' }));
    REQUIRE (f.keyboard.getSoundingCount() == 2);

    f.keyboard.setEnabled (false);
    CHECK (f.keyboard.getSoundingCount() == 0);
}

TEST_CASE ("playing from the keyboard changes nothing in the project", "[ui][keys][typing]")
{
    Fixture f;

    const auto before = f.document.getState().toXmlString();

    for (const auto& note : typingKeys::table())
    {
        f.keyboard.handleKeyPress (pressOf (note.keyCode));
        f.keyboard.refreshHeldKeys (f.holding ({ note.keyCode }));
    }

    f.keyboard.releaseAll();

    // The whole promise of "playback only", stated: not one property written,
    // nothing on the undo stack, and the document is not dirty.
    CHECK (f.document.getState().toXmlString() == before);
    CHECK_FALSE (f.document.getUndoManager().canUndo());
    CHECK_FALSE (f.document.hasChangedSinceSaved());
}

TEST_CASE ("a held key makes the selected channel sound", "[ui][keys][typing]")
{
    Fixture f;

    constexpr auto blockSize = 256;
    f.engine.prepare (48000.0, blockSize);

    juce::StringArray warnings;
    f.engine.setProject (f.document.getState(), &warnings);

    juce::AudioBuffer<float> block (2, blockSize);

    // Stopped, deliberately: a preview is heard whether or not the transport is
    // running, which is what "playback only" has to mean for a keyboard.
    f.keyboard.refreshHeldKeys (f.holding ({ 'z' }));

    auto peak = 0.0f;

    for (int i = 0; i < 16; ++i)
    {
        f.engine.processBlock (block);
        peak = juce::jmax (peak, block.getMagnitude (0, blockSize));
    }

    CHECK (peak > 0.0f);
}

// Kept here rather than in SettingsTests.cpp, which is at the four hundred code
// lines the tree allows a file. The three values below are this feature's, and
// this is this feature's file.
TEST_CASE ("keyboard input mode and its octave survive a relaunch", "[settings][typing]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    testing::TempDir temp { "dew-typing-settings-" };

    {
        Settings settings { temp.dir };

        // Off by default: a mode that takes every letter key must not arrive
        // switched on for somebody who has never asked for it.
        CHECK_FALSE (settings.getKeyboardInputEnabled());
        CHECK (settings.getKeyboardInputOctave() == Settings::defaultKeyboardOctave);

        settings.setKeyboardInputEnabled (true);
        settings.setKeyboardInputOctave (2);
    }

    {
        Settings settings { temp.dir };
        CHECK (settings.getKeyboardInputEnabled());
        CHECK (settings.getKeyboardInputOctave() == 2);

        // Clamped on the way in as well as on the way out, because a settings
        // file is a thing somebody can edit - and an octave off the top of the
        // map would be a keyboard that plays nothing.
        settings.setKeyboardInputOctave (99);
        CHECK (settings.getKeyboardInputOctave() == Settings::maxKeyboardOctave);

        settings.setKeyboardInputOctave (-7);
        CHECK (settings.getKeyboardInputOctave() == Settings::minKeyboardOctave);
    }
}
