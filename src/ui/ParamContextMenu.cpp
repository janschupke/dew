#include "ui/ParamContextMenu.h"

#include "i18n/Strings.h"
#include "model/ParamNames.h"
#include "model/Ids.h"
#include "model/ProjectEdits.h"

namespace dew::paramMenu
{

namespace
{

/** How long a curve a control makes.

    Four bars, which is the length the "+ Automation" button has always used and
    long enough that the two seeded points are far enough apart to grab.
*/
constexpr int clipLengthBars = 4;

} // namespace

juce::PopupMenu build (const juce::ValueTree& project, const juce::ValueTree& owner,
                       const ParamSpec& spec)
{
    juce::PopupMenu menu;

    if (spec.property == nullptr || ! owner.isValid())
        return menu;

    // Only when the node really does name a target. A control on a parameter
    // nothing can automate - an envelope stage, a base pitch - simply has no
    // item, rather than one that opens and then fails.
    if (automationTargetFor (project, owner, *spec.property).has_value())
        menu.addItem ((int) Item::createClip, tr (StringId::param_menu_createClip));

    menu.addItem ((int) Item::resetToDefault, tr (StringId::param_menu_resetToDefault));
    return menu;
}

void apply (int choice, const Context& context)
{
    if (context.document == nullptr || context.owner == nullptr)
        return;

    const auto& spec = context.spec;
    auto owner = context.owner();

    if (! owner.isValid() || spec.property == nullptr)
        return;

    auto& undo = context.document->getUndoManager();

    switch ((Item) choice)
    {
        case Item::createClip:
        {
            const auto target = automationTargetFor (context.document->getState(), owner,
                                                     *spec.property);

            if (! target.has_value())
                return;

            undo.beginNewTransaction ("Add automation");

            const auto startBar = context.startBar != nullptr ? context.startBar() : 0;

            auto clip = ProjectEdits::addAutomationWithClip (context.document->getState(), *target,
                                                             juce::jmax (0, startBar),
                                                             clipLengthBars, &undo);

            if (clip.isValid() && context.reveal != nullptr)
                context.reveal (clip);

            break;
        }

        case Item::resetToDefault:
            undo.beginNewTransaction ("Reset " + tr (paramNameOf (*spec.property)));
            ProjectEdits::setProperty (owner, *spec.property, spec.defaultVar(), &undo,
                                       "Reset " + tr (paramNameOf (*spec.property)));
            break;
    }
}

void show (juce::Component& control, const Context& context)
{
    if (context.document == nullptr || context.owner == nullptr)
        return;

    auto menu = build (context.document->getState(), context.owner(), context.spec);

    if (menu.getNumItems() == 0)
        return;

    // Required, or the popup overrides do not apply - the same rule HeaderRow
    // states for the menus it opens.
    menu.setLookAndFeel (&control.getLookAndFeel());
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&control),
                        [context] (int choice)
                        {
                            if (choice > 0)
                                apply (choice, context);
                        });
}

Trigger::Trigger (juce::Component& c, Context ctx)
    : control (c)
    , context (std::move (ctx))
{
    control.addMouseListener (this, false);
}

Trigger::~Trigger()
{
    control.removeMouseListener (this);
}

void Trigger::mouseDown (const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu())
        show (control, context);
}

} // namespace dew::paramMenu
