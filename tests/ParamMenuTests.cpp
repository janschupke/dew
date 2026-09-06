#include <catch2/catch_test_macros.hpp>

#include "ControlWalkHarness.h"
#include "model/Ids.h"
#include "model/AutomationTargets.h"
#include "model/GeneratorCatalog.h"
#include "model/ModuleCatalog.h"
#include "app/ProjectDocument.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"
#include "ui/MainComponent.h"
#include "ui/MenuSeam.h"
#include "ui/ParamContextMenu.h"

using namespace dew;
using dew::testing::findDescendantWithID;

namespace
{

/** A document plus the half of a Context a test supplies for itself. */
struct MenuHarness
{
    MenuHarness()
    {
        document.setState (ProjectFactory::createDefault(), true);
        host.document = &document;
        host.startBar = [] { return 0; };
    }

    paramMenu::Context contextFor (juce::ValueTree node, const ParamSpec& spec)
    {
        return host.contextFor ([node] { return node; }, spec);
    }

    juce::ValueTree channel() const
    {
        return document.getState().getChildWithName (ids::CHANNEL);
    }

    ProjectDocument document;
    paramMenu::Host host;
};

} // namespace

TEST_CASE ("an automatable control offers to make a curve, and others do not", "[ui][paramMenu]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    MenuHarness h;

    const auto items = [&h] (const juce::Identifier& property)
    {
        const auto menu = paramMenu::build (h.document.getState(), h.channel(),
                                            requireInstrumentParamSpec (property));
        return menuItems (menu);
    };

    // Volume is automatable, so the item is there.
    INFO ("volume: " << items (ids::volume).joinIntoString (", "));
    REQUIRE (items (ids::volume).contains ("Create automation clip"));

    // Base pitch is declared not automatable, so it is not - rather than an item
    // that opens and then quietly fails.
    INFO ("basePitch: " << items (ids::basePitch).joinIntoString (", "));
    REQUIRE_FALSE (items (ids::basePitch).contains ("Create automation clip"));

    // But everything still offers a reset, which is the other half of why a
    // control has a menu at all.
    REQUIRE (items (ids::basePitch).contains ("Reset to default"));
}

TEST_CASE ("creating a curve from a control points it at that control's parameter",
           "[ui][paramMenu]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    MenuHarness h;

    const auto before = h.document.getState().getNumChildren();

    paramMenu::apply ((int) paramMenu::Item::createClip,
                      h.contextFor (h.channel(), requireInstrumentParamSpec (ids::volume)));

    REQUIRE (h.document.getState().getNumChildren() > before);

    juce::ValueTree automation;

    for (const auto& child : h.document.getState())
        if (child.hasType (ids::AUTOMATION))
            automation = child;

    REQUIRE (automation.isValid());

    // It names the node the control was built for, and the property it turns.
    CHECK (automation[ids::scope].toString() == "channel");
    CHECK ((int) automation[ids::targetId] == (int) h.channel()[ids::id]);
    CHECK (automation[ids::param].toString() == ids::volume.toString());

    // And there is a clip for it, because a definition nothing refers to is a
    // menu item that appears to do nothing.
    auto found = false;

    for (const auto& track : h.document.getState().getChildWithName (ids::PLAYLIST))
        for (const auto& clip : track)
            if (clip.hasType (ids::CLIP) && ProjectEdits::isAutomationClip (clip)
                && (int) clip[ids::automationId] == (int) automation[ids::id])
                found = true;

    REQUIRE (found);
}

TEST_CASE ("a curve is placed even when every lane is taken", "[ui][paramMenu]")
{
    // This used to undo the definition and return nothing - a menu item that did
    // nothing at all. Rare from one button; routine once every knob offers it.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    MenuHarness h;

    juce::UndoManager scratch;

    for (auto track : h.document.getState().getChildWithName (ids::PLAYLIST))
        if (track.hasType (ids::PLAYLIST_TRACK))
            ProjectEdits::addClip (track, 1, 0, 1, &scratch);

    const auto
        tracksBefore = h.document.getState().getChildWithName (ids::PLAYLIST).getNumChildren();

    paramMenu::apply ((int) paramMenu::Item::createClip,
                      h.contextFor (h.channel(), requireInstrumentParamSpec (ids::volume)));

    const auto playlist = h.document.getState().getChildWithName (ids::PLAYLIST);
    REQUIRE (playlist.getNumChildren() == tracksBefore + 1);

    // On the new lane, and it is an automation clip.
    const auto added = playlist.getChild (playlist.getNumChildren() - 1);
    REQUIRE (added.getNumChildren() == 1);
    REQUIRE (ProjectEdits::isAutomationClip (added.getChild (0)));
}

TEST_CASE ("reset puts a control back to what the catalog declares", "[ui][paramMenu]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    MenuHarness h;

    auto channel = h.channel();
    const auto& spec = requireInstrumentParamSpec (ids::volume);

    channel.setProperty (ids::volume, 0.123, nullptr);
    REQUIRE_FALSE (juce::approximatelyEqual ((double) channel[ids::volume], spec.defaultValue));

    paramMenu::apply ((int) paramMenu::Item::resetToDefault, h.contextFor (channel, spec));

    REQUIRE (juce::approximatelyEqual ((double) channel[ids::volume], spec.defaultValue));
}

TEST_CASE ("every spec-built control in the window has a right-click", "[ui][paramMenu]")
{
    // The coverage guarantee, ENFORCED rather than asserted in prose. "Any
    // control in the UI" is the whole point of the feature, and a knob added to
    // a panel without its menu is exactly the kind of omission nobody notices.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    MainComponent component (false);
    component.setSize (1400, 900);

    int knobs = 0, withMenu = 0;
    juce::StringArray missing;

    /** Where a control lives, as the chain of component IDs above it - so a
        failure names the panel to go and look at rather than a count. */
    const auto describe = [] (juce::Component& c)
    {
        juce::StringArray path;

        for (auto* p = &c; p != nullptr; p = p->getParentComponent())
            if (p->getComponentID().isNotEmpty())
                path.insert (0, p->getComponentID());

        return path.joinIntoString (" > ") + " @" + c.getBounds().toString();
    };

    const std::function<void (juce::Component&)> walk = [&] (juce::Component& root)
    {
        for (auto* child : root.getChildren())
        {
            if (auto* knob = dynamic_cast<DewKnob*> (child))
            {
                ++knobs;

                if (knob->onContextMenu != nullptr)
                    ++withMenu;
                else
                    missing.add (describe (*knob));
            }

            if (auto* field = dynamic_cast<DewNumberField*> (child))
            {
                ++knobs;

                if (field->onContextMenu != nullptr)
                    ++withMenu;
                else
                    missing.add (describe (*field));
            }

            walk (*child);
        }
    };

    walk (component);

    // A control case: a walk that found nothing would pass for the wrong reason.
    INFO (knobs << " spec-built controls, " << withMenu << " with a menu");
    INFO ("without one:\n" << missing.joinIntoString ("\n"));
    REQUIRE (knobs > 0);

    // ONE exception now, and a NAMED one so it cannot quietly grow back.
    //
    // The transport bar's pattern-length field WAS the other, and is gone: a
    // pattern's length stopped being something anything could set when it
    // became a value derived from the notes in the pattern. The tempo field
    // beside it was one before that, until tempo became an automation target.
    // This count is exact so that both had to come back here, which is exactly
    // what happened.
    //
    // A channel row's mixer field is the second, for the reason mute has no
    // solo beside it: which track a channel plays through is a RELATION between
    // two objects rather than a value on one, so there is nothing for a curve
    // over it to interpolate. It is the one number field in the window with no
    // ParamSpec at all, which is also why it has nothing to be reset to.
    // describe() reports the whole chain of named ancestors, so the panel a
    // control sits in is the LAST segment rather than the first - which is why
    // the rack's row is matched by "> channelHeader @" and not by a prefix.
    const auto expected = [] (const juce::String& path)
    { return path.contains ("> channelHeader @"); };

    for (const auto& one : missing)
    {
        INFO ("unexpected control with no menu: " << one);
        REQUIRE (expected (one));
    }

    // One per channel row, and the demo project the window opens with has four
    // channels.
    REQUIRE (missing.size() == 4);
    REQUIRE (withMenu == knobs - missing.size());
}

TEST_CASE ("a panel with no host has no menus at all", "[ui][paramMenu]")
{
    // A null host is a window with no automation menus, which is what a test
    // that builds one panel on its own should get - rather than a crash or a
    // menu that reaches for a document that is not there.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    document.setState (ProjectFactory::createDefault(), true);

    EditorState state;
    EffectChainComponent chain { document, state };
    chain.setSize (400, 600);

    chain.setParamMenuHost (nullptr);

    auto* knob = findDescendantWithID (chain, "nothing-in-particular");
    REQUIRE (knob == nullptr);
}

TEST_CASE ("every instrument face offers a curve from its own knobs", "[ui][paramMenu]")
{
    // build() was only ever exercised against a CHANNEL, which is why three
    // whole faces of the instrument panel could offer nothing but a reset
    // without a test noticing.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    MenuHarness h;

    const auto items = [&h] (const juce::ValueTree& node, const juce::Identifier& property)
    {
        return menuItems (
            paramMenu::build (h.document.getState(), node, requireInstrumentParamSpec (property)));
    };

    auto channel = h.channel();
    REQUIRE (channel.isValid());

    // The amplitude envelope. Latched at note-on, so a curve moves the NEXT
    // note - which is what every automatable parameter but a wavetable position
    // already does, and is not a reason to withhold it.
    const auto amp = channel.getChildWithName (ids::INSTRUMENT).getChildWithName (ids::AMP);
    REQUIRE (amp.isValid());

    INFO ("attack: " << items (amp, ids::attack).joinIntoString (", "));
    CHECK (items (amp, ids::attack).contains ("Create automation clip"));
    CHECK (items (amp, ids::release).contains ("Create automation clip"));

    // A soundfont channel's offsets into the font it plays.
    auto font = ProjectEdits::addSoundFontChannel (h.document.getState(), "Font", nullptr);
    const auto soundFont = font.getChildWithName (ids::SOUNDFONT);
    REQUIRE (soundFont.isValid());

    INFO ("transpose: " << items (soundFont, ids::transpose).joinIntoString (", "));
    CHECK (items (soundFont, ids::transpose).contains ("Create automation clip"));
    CHECK (items (soundFont, ids::filterOffset).contains ("Create automation clip"));

    // And a recording's, which stay a reset: a fade is set once for a take, and
    // reversing a sample is a discontinuity in a read pointer.
    const auto sample = channel.getChildWithName (ids::SAMPLE);
    REQUIRE (sample.isValid());

    CHECK_FALSE (items (sample, ids::fadeInMs).contains ("Create automation clip"));
    CHECK (items (sample, ids::fadeInMs).contains ("Reset to default"));
}

TEST_CASE ("a wavetable knob reaches the same target the picker offers", "[ui][paramMenu]")
{
    // The defect: a knob for a generator-owned parameter is built from that
    // generator's OWN node - generatorNodeFor returns the WAVETABLE child - and
    // automationTargetFor knew only about OSC. So Position, Mod, Rate and
    // Spread showed a reset and nothing else, while the "+ Automation" picker
    // listed those very targets. Two directions of one answer, disagreeing.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    MenuHarness h;

    auto channel = h.channel();
    auto slot = channel.getChildWithName (ids::INSTRUMENT).getChildWithName (ids::OSC);
    REQUIRE (slot.isValid());

    slot.setProperty (ids::mode, "wavetable", nullptr);

    const auto wavetable = generatorNodeFor (slot, ids::wavePosition);
    REQUIRE (wavetable.isValid());
    REQUIRE (wavetable != slot);

    const auto items = menuItems (paramMenu::build (
        h.document.getState(), wavetable, requireInstrumentParamSpec (ids::wavePosition)));

    INFO ("wavePosition: " << items.joinIntoString (", "));
    CHECK (items.contains ("Create automation clip"));

    // And it resolves to the SLOT's address, not to a node of its own, which is
    // what keeps it the same target availableAutomationTargets offers.
    const auto target = automationTargetFor (h.document.getState(), wavetable, ids::wavePosition);
    REQUIRE (target.has_value());
    CHECK (target->scope == AutomationScope::channelOsc);
    CHECK (target->slot == 0);
    CHECK (target->targetId == (int) channel[ids::id]);
}
