#include <juce_gui_extra/juce_gui_extra.h>

#include "model/BuildInfo.h"
#include "model/ProjectFactory.h"
#include "model/ProjectSerializer.h"
#include "ui/AudioSettingsPanel.h"
#include "ui/MidiSettingsPanel.h"
#include "ui/RenderPanel.h"
#include "ui/MainComponent.h"
#include "ui/ScoreEditorComponent.h"
#include "ui/ConfirmPanel.h"
#include "ui/RandomizePanel.h"
#include "ui/design/DewLookAndFeel.h"
#include "ui/design/Theme.h"
#include "ui/design/DewGallery.h"
#include "CliArgs.h"
#include "DocsTokens.h"

namespace
{

constexpr const char* usage = R"(dew_shot - render dew's UI to a PNG without a display

Neither a screenshot nor a running app: components are laid out and painted into
an offscreen image, so this works over SSH, in CI, and on machines where screen
recording permission is not granted.

Usage:
  dew_shot editor <out.png> [options]
  dew_shot tabs <out-prefix> [options]      one PNG per tab
  dew_shot gallery <out.png> [options]      the design system
  dew_shot tokens <out.json>                the design system, as data
  dew_shot audio <out.png>                  the audio settings panel
  dew_shot midi <out.png>                   the MIDI settings panel
  dew_shot render <out.png> [--project f] [--format wav|flac|mp3|midi]
  dew_shot randomize <out.png>              the piano roll's randomize dialog
  dew_shot confirm <out.png>                the confirmation a deletion asks

Options:
  --project <file.dew>   Project to load (default: the built-in demo)
  --tab <name>           channel-rack | piano-roll | playlist | mixer | score
  --completions          open the score tab's completion popup before shooting
  --size <WxH>           Default 1440x900
  --theme <name>         dark | highContrast
  --help
)";

int fail (const juce::String& message)
{
    std::cerr << "dew_shot: " << message << std::endl;
    return 1;
}

juce::Rectangle<int> parseSize (const juce::String& text)
{
    const auto w = text.upToFirstOccurrenceOf ("x", false, true).getIntValue();
    const auto h = text.fromLastOccurrenceOf ("x", false, true).getIntValue();

    return { 0, 0, juce::jlimit (320, 8000, w > 0 ? w : 1440),
             juce::jlimit (240, 8000, h > 0 ? h : 900) };
}

juce::Component* findDescendantWithID (juce::Component& parent, const juce::String& id)
{
    for (auto* child : parent.getChildren())
    {
        if (child->getComponentID() == id)
            return child;

        if (auto* found = findDescendantWithID (*child, id))
            return found;
    }

    return nullptr;
}

int tabIndexFor (const juce::String& name)
{
    if (name == "channel-rack")
        return 0;
    if (name == "piano-roll")
        return 1;
    if (name == "playlist")
        return 2;
    if (name == "mixer")
        return 3;
    if (name == "score")
        return 4;
    return -1;
}

juce::Result writePng (juce::Component& component, const juce::File& destination)
{
    juce::Image image (juce::Image::ARGB, component.getWidth(), component.getHeight(), true);

    {
        juce::Graphics g (image);
        component.paintEntireComponent (g, true);
    }

    destination.getParentDirectory().createDirectory();
    destination.deleteFile();

    auto stream = std::unique_ptr<juce::FileOutputStream> (destination.createOutputStream());

    if (stream == nullptr)
        return juce::Result::fail ("could not create " + destination.getFullPathName());

    juce::PNGImageFormat png;

    if (! png.writeImageToStream (image, *stream))
        return juce::Result::fail ("could not encode a PNG");

    return juce::Result::ok();
}

} // namespace

int main (int argc, char* argv[])
{
    const dew::CliArgs args (argc, argv);

    if (args.has ("--help") || args.has ("-h") || args.positional.isEmpty())
    {
        std::cout << usage << std::endl;
        return args.positional.isEmpty() ? 1 : 0;
    }

    const auto mode = args.positional[0];

    if (args.positional.size() < 2)
        return fail ("expected an output path");

    // Before the GUI initialiser, and before applyPalette: this mode paints
    // nothing and reads darkPalette() directly, so it needs neither. It lives
    // here rather than in dew_docs because dew_docs links dew_lang and nothing
    // else - the tokens are in dew_design, and dew_shot already links dew_ui
    // and already exists to show what the design system looks like. This is
    // that job in a second format.
    if (mode == "tokens")
    {
        const auto destination = juce::File::getCurrentWorkingDirectory().getChildFile (
            args.positional[1]);

        // "\n" is passed, and that is not decoration. replaceWithText defaults
        // its lineEndings parameter to "\r\n", so the obvious call writes CRLF -
        // which the in-process gate would compare happily against itself while
        // cmp in CI compared it against an LF copy and failed.
        if (! destination.replaceWithText (dew::docs::tokensJson(), false, false, "\n"))
            return fail ("could not write " + destination.getFullPathName());

        std::cout << "wrote " << destination.getFullPathName() << std::endl;
        return 0;
    }

    const juce::ScopedJuceInitialiser_GUI juceInit;

    // The look and feel is installed by MainComponent's constructor, so the
    // panel modes - which build a panel on its own - were painting JUCE's stock
    // combo boxes and ticks rather than dew's. A shot that does not look like
    // the app is worse than no shot, since the whole point is seeing what the
    // user will see. Declared before any component so it outlives all of them.
    dew::DewLookAndFeel lookAndFeel;
    juce::Desktop::getInstance().setDefaultLookAndFeel (&lookAndFeel);

    const struct ClearLookAndFeel
    {
        ~ClearLookAndFeel()
        {
            juce::Desktop::getInstance().setDefaultLookAndFeel (nullptr);
        }
    } clearLookAndFeel;

    const auto size = parseSize (args.value ("--size", "1440x900"));

    // Before anything is constructed. Components copy colours when they are
    // built, so a palette chosen after the fact would be half applied - which
    // is the whole reason theme::apply exists for the running application and
    // the reason a renderer does not need it.
    dew::theme::applyPalette (dew::theme::kindFor (args.value ("--theme", "dark")));

    if (mode == "gallery")
    {
        dew::DewGallery gallery;
        gallery.setVisible (true);
        gallery.setSize (size.getWidth(),
                         juce::jmax (size.getHeight(), gallery.getRequiredHeight()));

        const auto destination = juce::File::getCurrentWorkingDirectory().getChildFile (
            args.positional[1]);

        if (const auto result = writePng (gallery, destination); result.failed())
            return fail (result.getErrorMessage());

        std::cout << "wrote " << destination.getFullPathName() << "  (" << gallery.getWidth() << "x"
                  << gallery.getHeight() << ")" << std::endl;
        return 0;
    }

    if (mode == "audio")
    {
        // Without a device open, which is both the CI case and the one worth
        // looking at: the panel has to be honest rather than blank.
        dew::AudioEngine engine;
        dew::LiveAudioHost host { engine };

        dew::AudioSettingsPanel panel { host, engine };
        panel.setVisible (true);
        panel.setSize (dew::AudioSettingsPanel::preferredWidth,
                       dew::AudioSettingsPanel::preferredHeight);

        const auto destination = juce::File::getCurrentWorkingDirectory().getChildFile (
            args.positional[1]);

        if (const auto result = writePng (panel, destination); result.failed())
            return fail (result.getErrorMessage());

        std::cout << "wrote " << destination.getFullPathName() << "  (" << panel.getWidth() << "x"
                  << panel.getHeight() << ")" << std::endl;
        return 0;
    }

    if (mode == "render")
    {
        // A project with something in it, and a selection, so the Selection scope
        // is one of the choices rather than a case you have to go and set up.
        dew::ProjectDocument document;

        const auto projectPath = args.value ("--project");

        if (projectPath.isNotEmpty())
        {
            const auto loaded = dew::ProjectSerializer::readFromFile (
                juce::File::getCurrentWorkingDirectory().getChildFile (projectPath));

            if (! loaded.ok())
                return fail (loaded.result.getErrorMessage());

            document.setState (loaded.tree, true);

            // Relative audio paths resolve against the document's file, so a
            // project loaded without one draws every recording as missing.
            document.setFile (juce::File::getCurrentWorkingDirectory().getChildFile (projectPath));
        }
        else
        {
            document.setState (dew::ProjectFactory::createDemo(), true);
        }

        dew::EditorState editorState;
        editorState.setSelectedBarRange ({ 1, 5 });

        dew::RenderPanel panel { document, editorState, nullptr };
        panel.setVisible (true);
        panel.setSize (dew::RenderPanel::preferredWidth, panel.getRequiredHeight());
        panel.resized();

        // Which rows show depends entirely on the format, so the shot has to be
        // able to ask for one - the same reason the editor mode takes --tab.
        const auto wanted = args.value ("--format").toLowerCase();

        if (wanted.isNotEmpty())
        {
            if (wanted == "wav")
                panel.setFormatForTesting (dew::RenderFormat::wav);
            else if (wanted == "flac")
                panel.setFormatForTesting (dew::RenderFormat::flac);
            else if (wanted == "mp3")
                panel.setFormatForTesting (dew::RenderFormat::mp3);
            else if (wanted == "midi")
                panel.setFormatForTesting (dew::RenderFormat::midi);
            else
                return fail ("unknown --format '" + wanted + "'");

            // Which rows show has just changed, and with them the height.
            panel.setSize (dew::RenderPanel::preferredWidth, panel.getRequiredHeight());
            panel.resized();
        }

        const auto destination = juce::File::getCurrentWorkingDirectory().getChildFile (
            args.positional[1]);

        if (const auto result = writePng (panel, destination); result.failed())
            return fail (result.getErrorMessage());

        std::cout << "wrote " << destination.getFullPathName() << "  (" << panel.getWidth() << "x"
                  << panel.getHeight() << ")" << std::endl;
        return 0;
    }

    if (mode == "midi")
    {
        // The panel with whatever this machine actually has attached, which on
        // CI is nothing - and "nothing" is the state most worth looking at.
        dew::AudioEngine engine;
        dew::LiveAudioHost host { engine };
        dew::MidiInputHost midiHost { host.getDeviceManager(), engine };

        dew::MidiSettingsPanel panel { midiHost, nullptr };
        panel.setVisible (true);
        panel.setSize (dew::MidiSettingsPanel::preferredWidth,
                       dew::MidiSettingsPanel::preferredHeight);

        const auto destination = juce::File::getCurrentWorkingDirectory().getChildFile (
            args.positional[1]);

        if (const auto result = writePng (panel, destination); result.failed())
            return fail (result.getErrorMessage());

        std::cout << "wrote " << destination.getFullPathName() << "  (" << panel.getWidth() << "x"
                  << panel.getHeight() << ")" << std::endl;
        return 0;
    }

    if (mode == "confirm")
    {
        // Bare, like the randomize dialog below and for the same reason:
        // dew_shot cannot capture a DialogWindow, so the content sizes itself.
        dew::ConfirmPanel panel { { "Delete pattern",
                                    "Delete \"Groove\"? Every clip that plays it goes with it.",
                                    "Delete" } };
        panel.setVisible (true);
        panel.setSize (dew::ConfirmPanel::preferredWidth, dew::ConfirmPanel::preferredHeight);

        const auto destination = juce::File::getCurrentWorkingDirectory().getChildFile (
            args.positional[1]);

        if (const auto result = writePng (panel, destination); result.failed())
            return fail (result.getErrorMessage());

        std::cout << "wrote " << destination.getFullPathName() << "  (" << panel.getWidth() << "x"
                  << panel.getHeight() << ")" << std::endl;
        return 0;
    }

    if (mode == "randomize")
    {
        // Shot as a bare component: dew_shot cannot capture a DialogWindow, which
        // is why the dialog's content sizes itself and says so publicly.
        dew::RandomizePanel panel { {}, "Applies to the 12 selected notes" };
        panel.setVisible (true);
        panel.setSize (dew::RandomizePanel::preferredWidth, dew::RandomizePanel::preferredHeight);

        const auto destination = juce::File::getCurrentWorkingDirectory().getChildFile (
            args.positional[1]);

        if (const auto result = writePng (panel, destination); result.failed())
            return fail (result.getErrorMessage());

        std::cout << "wrote " << destination.getFullPathName() << "  (" << panel.getWidth() << "x"
                  << panel.getHeight() << ")" << std::endl;
        return 0;
    }

    // No audio device: this only draws.
    dew::MainComponent component (false);

    if (const auto path = args.value ("--project"); path.isNotEmpty())
    {
        const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (path);
        const auto loaded = dew::ProjectSerializer::readFromFile (file);

        if (! loaded.ok())
            return fail (loaded.result.getErrorMessage());

        component.getDocument().setState (loaded.tree, true);

        // As above: without the file, a relative audio path has nothing to be
        // relative to and every waveform draws as missing.
        component.getDocument().setFile (file);
    }
    else
    {
        component.getDocument().setState (dew::ProjectFactory::createDemo(), true);
    }

    component.documentWasReplaced();
    component.setVisible (true);

    component.setSize (size.getWidth(), size.getHeight());

    auto* tabs = dynamic_cast<juce::TabbedComponent*> (
        findDescendantWithID (component, "editorTabs"));

    if (tabs == nullptr)
        return fail ("could not find the editor tabs");

    // The completion popup only exists while it is open, so a picture of it has
    // to be asked for. Reviewing it as a PNG is the only way anybody looks at a
    // transient panel in a build with no display.
    const auto openCompletions = [&component]
    {
        auto* score = dynamic_cast<dew::ScoreEditorComponent*> (
            findDescendantWithID (component, "scoreEditor"));

        if (score == nullptr)
            return;

        // Inside the harmony block, which is where completion has the most to
        // say: the chords are ranked by the key the score declares.
        const auto text = score->getSourceDocument().getAllContent();
        const auto anchor = text.indexOf ("harmony loop {\n  ");
        const auto caret = anchor < 0 ? 0 : anchor + juce::String ("harmony loop {\n  ").length();

        score->getEditor().moveCaretTo (
            juce::CodeDocument::Position (score->getSourceDocument(), caret), false);
        score->showCompletions();
    };

    const auto shoot = [&] (int tabIndex, const juce::File& destination) -> int
    {
        if (tabIndex >= 0)
        {
            tabs->setCurrentTabIndex (tabIndex, true);
            component.resized();
        }

        if (const auto result = writePng (component, destination); result.failed())
            return fail (result.getErrorMessage());

        std::cout << "wrote " << destination.getFullPathName() << "  (" << component.getWidth()
                  << "x" << component.getHeight() << ")" << std::endl;
        return 0;
    };

    if (mode == "editor")
    {
        const auto destination = juce::File::getCurrentWorkingDirectory().getChildFile (
            args.positional[1]);

        const auto tabIndex = tabIndexFor (args.value ("--tab", "channel-rack"));

        if (args.has ("--completions"))
        {
            tabs->setCurrentTabIndex (tabIndex, true);
            component.resized();
            openCompletions();
        }

        return shoot (tabIndex, destination);
    }

    if (mode == "tabs")
    {
        const juce::StringArray names { "channel-rack", "piano-roll", "playlist", "mixer",
                                        "score" };

        for (int i = 0; i < names.size(); ++i)
        {
            const auto destination = juce::File::getCurrentWorkingDirectory().getChildFile (
                args.positional[1] + "-" + names[i] + ".png");

            if (const auto code = shoot (i, destination); code != 0)
                return code;
        }

        return 0;
    }

    std::cerr << usage << std::endl;
    return fail ("unknown mode: " + mode);
}
