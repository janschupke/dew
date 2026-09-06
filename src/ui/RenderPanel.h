#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "i18n/Strings.h"

#include "app/Settings.h"
#include "io/OfflineRenderer.h"
#include "app/ProjectDocument.h"
#include "ui/DewDialog.h"
#include "ui/EditorState.h"
#include "ui/primitives/DewControls.h"
#include "ui/primitives/DewNumberField.h"

namespace dew
{

/** Choosing what to get out of a project, in dew's own idiom.

    The third of the settings panels, built like the other two: the same tokens,
    the same label rectangles drawn in paint(), the same re-entrancy guard, the
    same live summary under the controls.

    Unlike them, it has an OK and a Cancel, because a render is a thing you ask
    for rather than a setting you change. That is new here, so the footer sets
    the convention: the action on the right in the accent role, its way out
    beside it as a ghost.

    Controls that do not apply to the chosen format are HIDDEN and the panel
    relaid out, not greyed. A MIDI export showing a bit-depth box is a lie about
    what it is going to do.

    It does not choose the file itself. The caller does, from onRender, so that
    this stays constructible with no message loop, no file system and no engine -
    which is what lets a test and dew_shot both build one.
*/
class RenderPanel : public dialog::Panel, private juce::ChangeListener
{
public:
    /** What the user asked for. The destination is the caller's problem. */
    struct Request
    {
        RenderOptions options;
        bool stems = false;

        /** A name to offer in the file chooser, without an extension. */
        juce::String suggestedName;
    };

    /** `settingsToUpdate` may be null, in which case nothing is remembered -
        which is how the screenshot tool and the tests build one.
    */
    RenderPanel (ProjectDocument&, EditorState&, Settings* settingsToUpdate = nullptr);
    ~RenderPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Called when Render is clicked. The caller picks the destination and
        starts the job; this panel never touches a file.
    */
    std::function<void (const Request&)> onRender;

    /** Called when the panel wants to go away, for either button. */
    std::function<void()> onClose;

    /** Rebuilds from the document and the editor's state.

        Normally driven by EditorState's own callback, which is asynchronous -
        so a test with no message loop to pump calls this instead. Same idiom as
        MidiSettingsPanel::refresh.
    */
    void refresh();

    // --- for tests -----------------------------------------------------------
    juce::String getSummaryText() const
    {
        return summaryText;
    }
    Request getRequest() const;

    /** Which rows are showing, so a test can assert that MIDI hides the ones
        that would be lying.
    */
    bool isRowVisible (const juce::String& label) const;

    int getNumFormats() const
    {
        return formatBox.getNumItems();
    }
    int getNumScopes() const
    {
        return scopeBox.getNumItems();
    }

    void setFormatForTesting (RenderFormat);
    void setScopeForTesting (int itemId);

    /** How tall the panel needs to be for the rows currently showing.

        The formats differ by eight rows between them, so one fixed height either
        crops MP3 or leaves MIDI as two controls stranded at the top of an empty
        box. Same idea as DewGallery::getRequiredHeight.
    */
    int getRequiredHeight() const;

    static constexpr int preferredWidth = 460;

    /** Only a starting size; the panel sizes itself from its content. */
    static constexpr int preferredHeight = 470;

    /** Item ids for the scope box. Ids rather than indices, because which
        entries exist depends on whether there is a selection.
    */
    enum ScopeId
    {
        songScope = 1,
        patternScope,
        selectionScope
    };

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    /** Asks the dialog to follow the content's height, if there is one. */
    void applyRequiredHeight();

    void rebuildFormats();
    void rebuildScopes();
    void updateVisibility();
    void updateSummary();

    /** One labelled row. Keeping the label with its control is what makes
        hiding a row one flag rather than two lists that have to stay in step.
    */
    struct Row
    {
        // Spelled out rather than aggregate-initialised: the ci preset builds
        // with -Werror, and -Wmissing-field-initializers objects to a braced
        // list that stops before the members with defaults.
        Row (juce::String rowLabel, juce::Component* rowControl)
            : label (std::move (rowLabel))
            , control (rowControl)
        {
        }

        juce::String label;
        juce::Component* control = nullptr;
        bool visible = true;
        juce::Rectangle<int> labelBounds;
    };

    RenderFormat currentFormat() const;

    ProjectDocument& document;
    EditorState& editorState;
    Settings* settings = nullptr;

    DewDropdown scopeBox, formatBox, rateBox, depthBox, mp3QualityBox;
    DewNumberField tailField, peakField;
    DewCheckbox normalizeToggle { tr (StringId::render_normalize_label) };
    DewCheckbox fadeToggle { tr (StringId::render_fade_label) };
    DewCheckbox ditherToggle { tr (StringId::render_dither_label) };
    DewCheckbox stemsToggle { tr (StringId::render_stems_label) };

    DewButton renderButton { tr (StringId::render_start_label), DewButton::Role::primary };
    DewButton cancelButton { tr (StringId::dialog_cancel), DewButton::Role::ghost };

    std::vector<Row> rows;

    /** Where the summary goes: under the last row that is showing, not pinned
        above the footer. The panel's height has to fit the format with the most
        rows, so pinning it left a hand's width of nothing in every other case.
    */
    juce::Rectangle<int> summaryBounds;

    juce::String summaryText;
    juce::String unavailableNote;
    bool updating = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RenderPanel)
};

} // namespace dew
