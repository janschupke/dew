#include <catch2/catch_test_macros.hpp>

#include "model/Ids.h"
#include "model/ModuleCatalog.h"
#include "model/ProjectDocument.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"
#include "ui/MainComponent.h"
#include "ui/MenuSeam.h"
#include "ui/ParamContextMenu.h"

using namespace dew;

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

    juce::ValueTree channel() const { return document.getState().getChildWithName (ids::CHANNEL); }

    ProjectDocument document;
    paramMenu::Host host;
};

juce::Component* findDescendantWithID (juce::Component& root, const juce::String& id)
{
    for (auto* child : root.getChildren())
    {
        if (child->getComponentID() == id)
            return child;

        if (auto* found = findDescendantWithID (*child, id))
            return found;
    }

    return nullptr;
}

} // namespace

TEST_CASE ("an automatable control offers to make a curve, and others do not",
           "[ui][paramMenu]")
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

    const auto tracksBefore = h.document.getState().getChildWithName (ids::PLAYLIST).getNumChildren();

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

    // The transport bar's two number fields are the exception, and a NAMED one
    // so it has to shrink rather than being forgotten. Neither is built from a
    // ParamSpec yet: the tempo field states its own range by hand and gets a
    // spec when tempo becomes an automatable target, and steps-per-beat is a
    // property of the grid rather than a parameter of anything.
    //
    // An exact count rather than a floor: when the tempo field is migrated this
    // must be edited, which is the point.
    for (const auto& one : missing)
    {
        INFO ("unexpected control with no menu: " << one);
        REQUIRE (one.startsWith ("transportBar"));
    }

    REQUIRE (missing.size() == 2);
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
