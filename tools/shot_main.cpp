#include <juce_gui_extra/juce_gui_extra.h>

#include "BuildInfo.h"
#include "model/ProjectFactory.h"
#include "model/ProjectSerializer.h"
#include "ui/AudioSettingsPanel.h"
#include "ui/MainComponent.h"
#include "ui/design/DewGallery.h"

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
  dew_shot audio <out.png>                  the audio settings panel

Options:
  --project <file.dew>   Project to load (default: the built-in demo)
  --tab <name>           channel-rack | piano-roll | playlist | mixer
  --size <WxH>           Default 1440x900
  --help
)";

int fail (const juce::String& message)
{
    std::cerr << "dew_shot: " << message << std::endl;
    return 1;
}

struct Args
{
    explicit Args (int argc, char* argv[])
    {
        juce::StringArray raw;

        for (int i = 1; i < argc; ++i)
            raw.add (juce::String::fromUTF8 (argv[i]));

        for (int i = 0; i < raw.size(); ++i)
        {
            const auto& arg = raw[i];

            if (! arg.startsWith ("-"))
            {
                positional.add (arg);
                continue;
            }

            const auto name = arg.upToFirstOccurrenceOf ("=", false, false);

            if (arg.contains ("="))
                options.set (name, arg.fromFirstOccurrenceOf ("=", false, false));
            else if (i + 1 < raw.size() && ! raw[i + 1].startsWith ("-"))
                options.set (name, raw[++i]);
            else
                options.set (name, "");
        }
    }

    bool has (const juce::String& n) const { return options.containsKey (n); }

    juce::String value (const juce::String& n, const juce::String& fallback = {}) const
    {
        return has (n) && options[n].isNotEmpty() ? options[n] : fallback;
    }

    juce::StringPairArray options;
    juce::StringArray positional;
};

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
    if (name == "channel-rack") return 0;
    if (name == "piano-roll")   return 1;
    if (name == "playlist")     return 2;
    if (name == "mixer")        return 3;
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
    const Args args (argc, argv);

    if (args.has ("--help") || args.has ("-h") || args.positional.isEmpty())
    {
        std::cout << usage << std::endl;
        return args.positional.isEmpty() ? 1 : 0;
    }

    const auto mode = args.positional[0];

    if (args.positional.size() < 2)
        return fail ("expected an output path");

    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto size = parseSize (args.value ("--size", "1440x900"));

    if (mode == "gallery")
    {
        dew::DewGallery gallery;
        gallery.setVisible (true);
        gallery.setSize (size.getWidth(), juce::jmax (size.getHeight(), gallery.getRequiredHeight()));

        const auto destination = juce::File::getCurrentWorkingDirectory()
                                     .getChildFile (args.positional[1]);

        if (const auto result = writePng (gallery, destination); result.failed())
            return fail (result.getErrorMessage());

        std::cout << "wrote " << destination.getFullPathName()
                  << "  (" << gallery.getWidth() << "x" << gallery.getHeight() << ")" << std::endl;
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

        const auto destination = juce::File::getCurrentWorkingDirectory()
                                     .getChildFile (args.positional[1]);

        if (const auto result = writePng (panel, destination); result.failed())
            return fail (result.getErrorMessage());

        std::cout << "wrote " << destination.getFullPathName()
                  << "  (" << panel.getWidth() << "x" << panel.getHeight() << ")" << std::endl;
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
    }
    else
    {
        component.getDocument().setState (dew::ProjectFactory::createDemo(), true);
    }

    component.documentWasReplaced();
    component.setVisible (true);

    component.setSize (size.getWidth(), size.getHeight());

    auto* tabs = dynamic_cast<juce::TabbedComponent*> (findDescendantWithID (component, "editorTabs"));

    if (tabs == nullptr)
        return fail ("could not find the editor tabs");

    const auto shoot = [&] (int tabIndex, const juce::File& destination) -> int
    {
        if (tabIndex >= 0)
        {
            tabs->setCurrentTabIndex (tabIndex, true);
            component.resized();
        }

        if (const auto result = writePng (component, destination); result.failed())
            return fail (result.getErrorMessage());

        std::cout << "wrote " << destination.getFullPathName()
                  << "  (" << component.getWidth() << "x" << component.getHeight() << ")"
                  << std::endl;
        return 0;
    };

    if (mode == "editor")
    {
        const auto destination = juce::File::getCurrentWorkingDirectory()
                                     .getChildFile (args.positional[1]);

        return shoot (tabIndexFor (args.value ("--tab", "channel-rack")), destination);
    }

    if (mode == "tabs")
    {
        const juce::StringArray names { "channel-rack", "piano-roll", "playlist", "mixer" };

        for (int i = 0; i < names.size(); ++i)
        {
            const auto destination = juce::File::getCurrentWorkingDirectory()
                                         .getChildFile (args.positional[1] + "-" + names[i] + ".png");

            if (const auto code = shoot (i, destination); code != 0)
                return code;
        }

        return 0;
    }

    std::cerr << usage << std::endl;
    return fail ("unknown mode: " + mode);
}
