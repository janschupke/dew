#include "ui/SoundFontSection.h"

#include "i18n/Strings.h"
#include "model/AssetPaths.h"
#include "model/ProjectEdits.h"
#include "ui/design/Tokens.h"

namespace dew
{

SoundFontSection::SoundFontSection (ProjectDocument& d, SoundFontPool* p)
    : document (d)
    , pool (p)
{
    setComponentID ("soundFontSection");

    // The third instrument face. The oscillator and sample sections have been
    // groups since the accessibility work and this one was missed, so its load
    // button and preset box belonged to the panel around it.
    setTitle (tr (StringId::soundFont_title));
    setFocusContainerType (FocusContainerType::focusContainer);

    fileLabel.setJustificationType (juce::Justification::centredLeft);
    fileLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (fileLabel);

    loadButton.setComponentID ("soundFontLoad");
    loadButton.setTooltip (tr (StringId::soundFont_load_help));
    loadButton.onClick = [this] { chooseFile(); };
    addAndMakeVisible (loadButton);

    presetLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (presetLabel);

    presetBox.setComponentID ("soundFontPreset");
    presetBox.setTooltip (tr (StringId::soundFont_preset_help));
    presetBox.onChange = [this]
    {
        if (! updating)
            applyPresetChoice (presetBox.getSelectedId());
    };
    addAndMakeVisible (presetBox);

    transposeKnob.setNumDecimalPlaces (0);
    transposeKnob.setBipolar (true);
    tuneKnob.setNumDecimalPlaces (0);
    tuneKnob.setBipolar (true);
    filterKnob.setNumDecimalPlaces (0);
    filterKnob.setBipolar (true);

    attachKnob (transposeKnob, ids::transpose, "Change soundfont pitch");
    attachKnob (tuneKnob, ids::tuneCents, "Change soundfont tuning");
    attachKnob (filterKnob, ids::filterOffset, "Change soundfont filter");
    attachKnob (attackKnob, ids::attackScale, "Change soundfont attack");
    attachKnob (releaseKnob, ids::releaseScale, "Change soundfont release");
    attachKnob (velocityKnob, ids::velocitySens, "Change velocity sensitivity");

    document.getState().addListener (this);
}

SoundFontSection::~SoundFontSection()
{
    document.getState().removeListener (this);
}

void SoundFontSection::setParamMenuHost (const paramMenu::Host* host)
{
    paramMenuHost = host;

    const std::function<juce::ValueTree()> self = [this] { return soundFont; };

    const std::pair<DewKnob*, const juce::Identifier*> knobs[] {
        { &transposeKnob, &ids::transpose },  { &tuneKnob, &ids::tuneCents },
        { &filterKnob, &ids::filterOffset },  { &attackKnob, &ids::attackScale },
        { &releaseKnob, &ids::releaseScale }, { &velocityKnob, &ids::velocitySens }
    };

    for (const auto& [knob, property] : knobs)
        paramMenu::attachTo (host, *knob, self, requireInstrumentParamSpec (*property));
}

void SoundFontSection::attachKnob (DewKnob& knob, const juce::Identifier& property,
                                   const juce::String& transactionName)
{
    // A drag is ONE undo step, and it is driven by the knob's own drag callbacks
    // rather than by the mouse: the pointer reads as "not down" in every
    // headless harness, so a guard built on it would be one no test could see.
    knob.onEditStart = [this]
    {
        inDrag = true;
        gestureActive = false;
    };
    knob.onEditEnd = [this]
    {
        inDrag = false;
        gestureActive = false;
    };

    knob.onValueChange = [this, &knob, property, transactionName]
    {
        if (updating)
            return;

        write (property, knob.getValue(), transactionName);
        gestureActive = inDrag;
    };

    addAndMakeVisible (knob);
}

void SoundFontSection::write (const juce::Identifier& property, const juce::var& value,
                              const juce::String& transactionName)
{
    ProjectEdits::setProperty (soundFont, property, value, &document.getUndoManager(),
                               transactionName, gestureActive);
}

void SoundFontSection::setOwner (juce::ValueTree soundFontNode)
{
    soundFont = std::move (soundFontNode);
    refresh();
}

const SoundFontPool::Entry* SoundFontSection::entry() const
{
    if (pool == nullptr || ! soundFont.isValid())
        return nullptr;

    const auto path = soundFont[ids::file].toString();

    if (path.isEmpty())
        return nullptr;

    const auto& found = pool->loadReference (path);
    return found.isValid() ? &found : nullptr;
}

juce::StringArray SoundFontSection::presetMenuItems() const
{
    juce::StringArray items;

    if (const auto* found = entry())
        for (const auto& preset : found->font->presets)
            items.add (juce::String (preset.bank) + ":" + juce::String (preset.program) + "  "
                       + preset.name);

    return items;
}

bool SoundFontSection::applyPresetChoice (int oneBasedChoice)
{
    const auto* found = entry();

    if (found == nullptr || ! soundFont.isValid())
        return false;

    const auto index = (size_t) (oneBasedChoice - 1);

    if (oneBasedChoice < 1 || index >= found->font->presets.size())
        return false;

    const auto& preset = found->font->presets[index];

    // One transaction for the whole choice: a bank, a program and a name are
    // three properties saying one thing, and three undo steps for one click is
    // exactly what this panel's knobs were fixed for. Through ProjectEdits
    // rather than by hand, which a source gate insists on.
    auto& undo = document.getUndoManager();
    undo.beginNewTransaction ("Choose soundfont preset");

    ProjectEdits::setSoundFontPreset (soundFont.getParent(), preset.bank, preset.program,
                                      preset.name, &undo);

    return true;
}

void SoundFontSection::loadFile (const juce::File& file)
{
    if (! soundFont.isValid() || pool == nullptr || file == juce::File())
        return;

    const auto stored = AssetPaths::relativise (file, pool->getProjectFile());

    auto& undo = document.getUndoManager();
    undo.beginNewTransaction ("Load soundfont");

    // Deliberately not through gatherAssetsInto: a soundfont is REFERENCED and
    // never copied into the project's asset folder. See ProjectDocument.
    ProjectEdits::setSoundFontSource (soundFont.getParent(), stored, 0, 0, {}, &undo);

    // Every font in a real library holds exactly one preset, so choosing it
    // here is the common case rather than a fallback - and a channel that
    // loaded a file and stayed silent behind an untouched list reads as broken.
    const auto& found = pool->loadReference (stored);

    if (found.isValid())
        if (const auto* first = found.font->firstPreset())
            ProjectEdits::setSoundFontSource (soundFont.getParent(), stored, first->bank,
                                              first->program, first->name, &undo);

    refresh();
}

void SoundFontSection::chooseFile()
{
    if (! soundFont.isValid())
        return;

    const auto startIn = pool != nullptr && pool->getProjectFile() != juce::File()
                             ? pool->getProjectFile().getParentDirectory()
                             : AssetPaths::defaultBrowseFolder();

    // Both spellings: two files in a real library are named .SF2, and a
    // lower-case-only wildcard hides them on a case-sensitive filesystem.
    chooser = std::make_unique<juce::FileChooser> (tr (StringId::file_chooseSoundFont), startIn,
                                                   "*.sf2;*.SF2");

    chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& result)
                          {
                              const auto file = result.getResult();

                              if (file != juce::File())
                                  loadFile (file);
                          });
}

juce::String SoundFontSection::getFileDescription() const
{
    if (! soundFont.isValid())
        return {};

    const auto path = soundFont[ids::file].toString();

    if (path.isEmpty())
        return "No soundfont";

    const auto name = juce::File::createFileWithoutCheckingPath (path).getFileName();

    // A font that is not on this machine is an ordinary situation - a project
    // can arrive before the library it refers to - so it says which file is
    // missing rather than showing nothing.
    return entry() != nullptr ? name : name + " (missing)";
}

void SoundFontSection::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier&)
{
    // By identity, not by type: this listens to the whole document, and every
    // other channel has a SOUNDFONT node too.
    if (tree == soundFont)
        refresh();
}

void SoundFontSection::refresh()
{
    const juce::ScopedValueSetter<bool> quiet (updating, true);

    const auto valid = soundFont.isValid();
    setEnabled (valid);

    fileLabel.setText (getFileDescription(), juce::dontSendNotification);

    presetBox.clear (juce::dontSendNotification);

    const auto items = presetMenuItems();

    for (int i = 0; i < items.size(); ++i)
        presetBox.addItem (items[i], i + 1);

    presetBox.setEnabled (valid && ! items.isEmpty());
    presetLabel.setText (tr (StringId::soundFont_preset_caption), juce::dontSendNotification);

    if (valid)
    {
        if (const auto* found = entry())
        {
            const auto bank = (int) soundFont[ids::bank];
            const auto program = (int) soundFont[ids::program];

            for (size_t i = 0; i < found->font->presets.size(); ++i)
                if (found->font->presets[i].bank == bank
                    && found->font->presets[i].program == program)
                    presetBox.setSelectedId ((int) i + 1, juce::dontSendNotification);
        }

        transposeKnob.setValue ((double) soundFont[ids::transpose], juce::dontSendNotification);
        tuneKnob.setValue ((double) soundFont[ids::tuneCents], juce::dontSendNotification);
        filterKnob.setValue ((double) soundFont[ids::filterOffset], juce::dontSendNotification);
        attackKnob.setValue ((double) soundFont[ids::attackScale], juce::dontSendNotification);
        releaseKnob.setValue ((double) soundFont[ids::releaseScale], juce::dontSendNotification);
        velocityKnob.setValue ((double) soundFont[ids::velocitySens], juce::dontSendNotification);
    }

    repaint();
}

void SoundFontSection::paint (juce::Graphics& g)
{
    using namespace tokens;

    // The file row reads as a field rather than as a label floating on the
    // panel, which is what says the name in it is a value somebody chose.
    auto row = getLocalBounds().removeFromTop (size::controlHeight);
    row.removeFromRight (size::gutterLabel + space::sm);

    g.setColour (colour::well);
    g.fillRoundedRectangle (row.toFloat(), radius::sm);
}

void SoundFontSection::resized()
{
    using namespace tokens;

    auto area = getLocalBounds();

    auto fileRow = area.removeFromTop (size::controlHeight);
    loadButton.setBounds (fileRow.removeFromRight (size::gutterLabel));
    fileRow.removeFromRight (space::sm);
    fileLabel.setBounds (fileRow.reduced (space::sm, 0));

    area.removeFromTop (space::sm);

    auto presetRow = area.removeFromTop (size::controlHeight);
    presetLabel.setBounds (presetRow.removeFromLeft (size::gutterLabel / 2));
    presetBox.setBounds (presetRow);

    area.removeFromTop (space::sm);

    const auto placeRow = [] (juce::Rectangle<int> bounds, std::initializer_list<DewKnob*> knobs)
    {
        const auto width = bounds.getWidth() / (int) knobs.size();

        for (auto* knob : knobs)
            knob->setBounds (bounds.removeFromLeft (width));
    };

    placeRow (area.removeFromTop (size::knobRow), { &transposeKnob, &tuneKnob, &filterKnob });
    area.removeFromTop (space::sm);
    placeRow (area.removeFromTop (size::knobRow), { &attackKnob, &releaseKnob, &velocityKnob });
}

} // namespace dew
