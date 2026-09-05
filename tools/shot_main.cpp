#include <juce_gui_extra/juce_gui_extra.h>

#include "model/BuildInfo.h"
#include "model/ProjectFactory.h"
#include "model/ProjectSerializer.h"
#include "ui/RenderPanel.h"
#include "ui/MainComponent.h"
#include "ui/ScoreEditorComponent.h"
#include "ui/design/DewLookAndFeel.h"
#include "ui/design/Theme.h"
#include "ui/design/DewGallery.h"
#include "CliArgs.h"
#include "IconRaster.h"
#include "ShotPanels.h"
#include "DocsSamples.h"
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
  dew_shot samples <out.json> <a.score...>  scores, with their token runs
  dew_shot icon <out.png> <in.svg>          the app icon, rasterised
  dew_shot audio <out.png>                  the audio settings panel
  dew_shot midi <out.png>                   the MIDI settings panel
  dew_shot render <out.png> [--project f] [--format wav|flac|mp3|midi]
  dew_shot randomize <out.png>              the piano roll's randomize dialog
  dew_shot confirm <out.png>                the confirmation a deletion asks
  dew_shot mcp-consent <out.png>            what a client is allowed by
  dew_shot mcp <out.png>                    the MCP settings
  dew_shot preferences <out.png> [--page <name>] [--filter <text>]
  dew_shot about <out.png>                  what this build is

Options:
  --project <file.dew>   Project to load (default: the built-in demo)
  --tab <name>           channel-rack | piano-roll | playlist | mixer | score
  --completions          open the score tab's completion popup before shooting
  --size <WxH>           Default 1440x900
  --scale <n>            Render at n times the size, 1..4. Default 1
  --px <n>               icon only: edge length in pixels, 16..2048. Default 1024
  --page <name>          preferences only: appearance | audio | midi | rendering
                         | connections
  --filter <text>        preferences only: apply a search, and shoot the result
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

    if (mode == "samples")
    {
        juce::Array<juce::File> scores;

        for (auto i = 2; i < args.positional.size(); ++i)
            scores.add (juce::File::getCurrentWorkingDirectory().getChildFile (args.positional[i]));

        if (scores.isEmpty())
            return fail ("samples needs at least one .score file");

        const auto destination = juce::File::getCurrentWorkingDirectory().getChildFile (
            args.positional[1]);

        if (! destination.replaceWithText (dew::docs::samplesJson (scores), false, false, "\n"))
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

    // Rasterised from the SVG gen-theme.mjs writes out of the palette. The
    // work is in IconRaster.h; what is here is the mode's arguments.
    if (mode == "icon")
    {
        if (args.positional.size() < 3)
            return fail ("icon needs an .svg to rasterise");

        const auto cwd = juce::File::getCurrentWorkingDirectory();
        const auto edge = juce::jlimit (16, 2048, args.value ("--px", "1024").getIntValue());
        const auto destination = cwd.getChildFile (args.positional[1]);

        if (const auto result = dew::shot::rasteriseIcon (cwd.getChildFile (args.positional[2]),
                                                          destination, edge);
            result.failed())
            return fail (result.getErrorMessage());

        std::cout << "wrote " << destination.getFullPathName() << "  (" << edge << "x" << edge
                  << ")" << std::endl;
        return 0;
    }

    const auto size = parseSize (args.value ("--size", "1440x900"));
    const auto scale = juce::jlimit (1, 4, args.value ("--scale", "1").getIntValue());

    // Before anything is constructed. Components copy colours when they are
    // built, so a palette chosen after the fact would be half applied - which
    // is the whole reason theme::apply exists for the running application and
    // the reason a renderer does not need it.
    dew::theme::applyPalette (dew::theme::kindFor (args.value ("--theme", "dark")));

    // AFTER the palette, because a panel copies colours when it is built - and
    // --theme would otherwise be a flag these eight modes quietly ignored.
    if (const auto code = dew::shot::shootPanel (
            mode, juce::File::getCurrentWorkingDirectory().getChildFile (args.positional[1]), scale,
            args.value ("--page"), args.value ("--filter"));
        code >= 0)
        return code;

    if (mode == "gallery")
    {
        dew::DewGallery gallery;
        gallery.setVisible (true);
        gallery.setSize (size.getWidth(),
                         juce::jmax (size.getHeight(), gallery.getRequiredHeight()));

        const auto destination = juce::File::getCurrentWorkingDirectory().getChildFile (
            args.positional[1]);

        if (const auto result = dew::shot::writePng (gallery, destination, scale); result.failed())
            return fail (result.getErrorMessage());

        std::cout << "wrote " << destination.getFullPathName() << "  (" << gallery.getWidth() << "x"
                  << gallery.getHeight() << ")" << std::endl;
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

        if (const auto result = dew::shot::writePng (panel, destination, scale); result.failed())
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

        if (const auto result = dew::shot::writePng (component, destination, scale);
            result.failed())
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
