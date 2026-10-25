// MIT License
//
// Copyright (c) 2018-2026 Jakub Melka and Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "pdfappsettingssection_colormodes.h"
#include "pdfappcolormodepresets.h"
#include "pdfappcolormodepreview.h"
#include "pdfappcontext.h"
#include "pdfapppagelayout.h"
#include "pdfappsettingcontrol.h"
#include "pdfappsettingrow.h"
#include "pdfappworkspace.h"
#include "pdfsettingdefinition.h"
#include "pdfsettingdefinitions.h"
#include "pdfsettingsrepository.h"

#include "pdfcomponentbadges.h"
#include "pdfcomponentcards.h"
#include "pdfcomponentpanels.h"
#include "pdffluentwidgets.h"
#include "pdfpreviewpainter.h"
#include "pdfthemefonts.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace pdf
{

/// Helpers of the section Color modes
class PDFAppColorModesSettingsSectionHelper
{
public:
    PDFAppColorModesSettingsSectionHelper() = delete;

    /// Dynamic property of a card with its value (mode or preset id)
    static constexpr const char* VALUE_PROPERTY = "pdf4qtSettingValue";

    /// Picture of the card of the mode
    static PDFPreviewPainter::Preview getPreview(const QString& mode)
    {
        using Preview = PDFPreviewPainter::Preview;
        if (mode == QLatin1String("inverted")) { return Preview::ModeInverted; }
        if (mode == QLatin1String("grayscale")) { return Preview::ModeGrayscale; }
        if (mode == QLatin1String("highContrast")) { return Preview::ModeHighContrast; }
        if (mode == QLatin1String("bitonal")) { return Preview::ModeBitonal; }
        if (mode == QLatin1String("customColors")) { return Preview::ModeCustomColors; }
        return Preview::ModeNormal;
    }

    /// Adds the translated text and its source text (once, when equal)
    static void addTexts(QStringList& texts, const QString& translated, const QString& source)
    {
        if (!translated.isEmpty())
        {
            texts << translated;
        }
        if (!source.isEmpty() && source != translated)
        {
            texts << source;
        }
    }
};

PDFAppColorModesSettingsSection::PDFAppColorModesSettingsSection(PDFAppContext* context, const QString& sectionId, QWidget* parent) :
    BaseClass(context, sectionId, parent)
{

}

PDFAppColorModesSettingsSection::~PDFAppColorModesSettingsSection()
{
    // A click of a card is never lost: it is written, but nobody is notified (the
    // receivers may be being destroyed with the page)
    writePending(false);
}

QStringList PDFAppColorModesSettingsSection::getModeIds()
{
    return { QStringLiteral("normal"), QStringLiteral("inverted"), QStringLiteral("grayscale"),
             QStringLiteral("highContrast"), QStringLiteral("bitonal"), QStringLiteral("customColors") };
}

PDFPreviewCard* PDFAppColorModesSettingsSection::getModeCard(const QString& mode) const
{
    auto it = m_modeCards.find(mode);
    return it != m_modeCards.end() ? it->second.data() : nullptr;
}

PDFReadingThemeCard* PDFAppColorModesSettingsSection::getPresetCard(const QString& preset) const
{
    auto it = m_presetCards.find(preset);
    return it != m_presetCards.end() ? it->second.data() : nullptr;
}

QStringList PDFAppColorModesSettingsSection::getKeywords(const PDFSettingDefinition& definition)
{
    using Helper = PDFAppColorModesSettingsSectionHelper;

    QStringList keywords = definition.getKeywords();
    Helper::addTexts(keywords, definition.getTitle(), QString::fromUtf8(definition.getTitleSource()));
    Helper::addTexts(keywords, definition.getDescription(), QString::fromUtf8(definition.getDescriptionSource()));
    if (definition.getType() == PDFSettingDefinition::Type::Option)
    {
        for (const PDFSettingOption& option : definition.getOptions())
        {
            Helper::addTexts(keywords, option.getTitle(), option.title);
        }
    }
    return keywords;
}

PDFAppSettingRow* PDFAppColorModesSettingsSection::createRow(const PDFSettingDefinition& definition)
{
    if (definition.getKey() == PDFSettingKeys::viewingColorMode())
    {
        return createModeRow(definition);
    }
    if (definition.getKey() == PDFSettingKeys::colorModesCustomPreset())
    {
        return createPresetRow(definition);
    }

    PDFAppSettingRow* row = BaseClass::createRow(definition);
    if (row && row->getControl() && (definition.getKey() == PDFSettingKeys::colorModesTextColor() || definition.getKey() == PDFSettingKeys::colorModesPageColor()))
    {
        // A color picked by the user switches the preset to Custom
        connect(row->getControl(), &PDFAppSettingControl::valueWritten, this, &PDFAppColorModesSettingsSection::onColorWritten);
    }
    return row;
}

PDFAppSettingRow* PDFAppColorModesSettingsSection::createModeRow(const PDFSettingDefinition& definition)
{
    // modes 160:4351: six cards of the same width, 8 apart, padding 10
    PDFAppSettingRow* row = new PDFAppSettingRow(definition.getTitle(), definition.getDescription(), definition.getAnchor());
    row->setSearchKeywords(getKeywords(definition));

    PDFAppGrid* grid = new PDFAppGrid(MODE_CARD_MINIMUM_WIDTH, MODE_COLUMNS, CARD_SPACING, CARD_SPACING);
    grid->setObjectName(QStringLiteral("modeCards"));
    m_modeGroup = new QButtonGroup(this);
    m_modeGroup->setExclusive(true);

    for (const QString& mode : getModeIds())
    {
        QString title = mode;
        for (const PDFSettingOption& option : definition.getOptions())
        {
            if (option.id == mode)
            {
                title = option.getTitle();
            }
        }

        PDFPreviewCard* card = new PDFPreviewCard(title, PDFAppColorModesSettingsSectionHelper::getPreview(mode), grid);
        card->setObjectName(QStringLiteral("mode-%1").arg(mode));
        card->setProperty(PDFAppColorModesSettingsSectionHelper::VALUE_PROPERTY, mode);
        m_modeGroup->addButton(card);
        connect(card, &QAbstractButton::clicked, this, &PDFAppColorModesSettingsSection::onModeCardClicked);
        grid->addWidget(card);
        m_modeCards[mode] = card;
    }

    row->setBlockWidget(grid, QMargins(10, 10, 10, 10));
    updateModeCards();
    return row;
}

PDFAppSettingRow* PDFAppColorModesSettingsSection::createPresetRow(const PDFSettingDefinition& definition)
{
    // themes 160:4417: two rows of five cards, 8 apart, paddings 12 / 14
    PDFAppSettingRow* row = new PDFAppSettingRow(definition.getTitle(), definition.getDescription(), definition.getAnchor());
    row->setSearchKeywords(getKeywords(definition));

    PDFAppGrid* grid = new PDFAppGrid(PRESET_CARD_MINIMUM_WIDTH, PRESET_COLUMNS, CARD_SPACING, CARD_SPACING);
    grid->setObjectName(QStringLiteral("presetCards"));
    m_presetGroup = new QButtonGroup(this);
    m_presetGroup->setExclusive(true);

    for (const PDFAppColorModePreset& preset : PDFAppColorModePresets::getPresets())
    {
        PDFReadingThemeCard* card = new PDFReadingThemeCard(preset.getTitle(), preset.pageToken, preset.textToken, grid);
        card->setObjectName(QStringLiteral("preset-%1").arg(preset.id));
        card->setProperty(PDFAppColorModesSettingsSectionHelper::VALUE_PROPERTY, preset.id);
        m_presetGroup->addButton(card);
        connect(card, &QAbstractButton::clicked, this, &PDFAppColorModesSettingsSection::onPresetCardClicked);
        grid->addWidget(card);
        m_presetCards[preset.id] = card;
    }

    row->setBlockWidget(grid, QMargins(12, 14, 12, 14));
    updatePresetCards();
    return row;
}

PDFAppSettingRow* PDFAppColorModesSettingsSection::createPreviewRow()
{
    // text-preview 160:4430: the caption Medium 11 text/muted and the panel, 8 apart,
    // paddings 16 / 14 / 16 / 16
    PDFAppSettingRow* row = new PDFAppSettingRow(tr("Preview"), tr("The page and the text in the custom colors"), QStringLiteral("color-preview"));
    row->setSearchKeywords({ QStringLiteral("preview"), QStringLiteral("sample"), QStringLiteral("custom colors") });

    QWidget* block = new QWidget();
    block->setObjectName(QStringLiteral("previewBlock"));
    QVBoxLayout* layout = new QVBoxLayout(block);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(PREVIEW_SPACING);
    // the caption in its line of 13 ("leading normal" of Medium 11), not the taller line of a label
    PDFLabel* caption = PDFAppPageWidgets::createText(tr("Preview"), PDFThemeFonts::getFont(PDFThemeFonts::Role::Caption), PREVIEW_CAPTION_LINE_HEIGHT, PDFThemeColors::TextMuted, block);
    caption->setObjectName(QStringLiteral("previewCaption"));
    layout->addWidget(caption);
    m_preview = new PDFAppColorModePreview(block);
    layout->addWidget(m_preview);

    row->setBlockWidget(block, QMargins(16, 14, 16, 16));
    return row;
}

PDFAppSettingRow* PDFAppColorModesSettingsSection::createContrastRow()
{
    // row-Contrast 160:4447: the status circle 20 (icon 12), the ratio Semi Bold 13
    // text/primary and the verdict Regular 12 text/tertiary, 8 apart
    PDFAppSettingRow* row = new PDFAppSettingRow(tr("Contrast"), tr("Text against the page"), QStringLiteral("contrast"));
    row->setSearchKeywords({ QStringLiteral("contrast"), QStringLiteral("readability"), QStringLiteral("wcag") });

    QWidget* control = new QWidget();
    control->setObjectName(QStringLiteral("contrast"));
    QHBoxLayout* layout = new QHBoxLayout(control);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(CONTRAST_SPACING);
    m_contrastBadge = new PDFIconBadge(QStringLiteral("check"), CONTRAST_BADGE, CONTRAST_ICON, control);
    m_contrastBadge->setObjectName(QStringLiteral("contrastBadge"));
    m_contrastBadge->setRadius(-1.0);
    layout->addWidget(m_contrastBadge);
    m_contrastRatio = PDFAppPageWidgets::createLabel(QString(), PDFThemeFonts::Role::BodySemiBold, PDFThemeColors::TextPrimary, control);
    m_contrastRatio->setObjectName(QStringLiteral("contrastRatio"));
    layout->addWidget(m_contrastRatio);
    m_contrastVerdict = PDFAppPageWidgets::createLabel(QString(), PDFThemeFonts::Role::Small, PDFThemeColors::TextTertiary, control);
    m_contrastVerdict->setObjectName(QStringLiteral("contrastVerdict"));
    layout->addWidget(m_contrastVerdict);
    layout->addStretch(1);

    row->setControlWidget(control);
    m_contrastRow = row;
    return row;
}

void PDFAppColorModesSettingsSection::finishContent()
{
    BaseClass::finishContent();

    // Custom colors 160:4416: the presets, the preview, the two colors, the contrast
    if (PDFAppSettingGroup* group = findGroup(QStringLiteral("Custom colors")))
    {
        group->addRow(createPreviewRow(), 1);
        group->addRow(createContrastRow());
    }

    updateColors();
}

QString PDFAppColorModesSettingsSection::getStoredOption(const QString& key) const
{
    PDFAppContext* context = getContext();
    if (!context || !context->getDatabase())
    {
        const PDFSettingDefinition* definition = context ? context->getSettingRegistry()->findDefinition(key) : nullptr;
        return definition ? definition->getDefaultValue().toString() : QString();
    }
    return context->getSettings().getString(key);
}

QColor PDFAppColorModesSettingsSection::getColor(const QString& key) const
{
    // A hostile stored text is the default color
    PDFAppContext* context = getContext();
    const PDFSettingDefinition* definition = context ? context->getSettingRegistry()->findDefinition(key) : nullptr;
    const QColor defaultColor = definition ? PDFAppColorModePresets::parseColor(definition->getDefaultValue().toString()) : QColor();
    if (!context || !context->getDatabase())
    {
        return defaultColor;
    }

    const QColor color = PDFAppColorModePresets::parseColor(context->getSettings().getString(key));
    return color.isValid() ? color : defaultColor;
}

QColor PDFAppColorModesSettingsSection::getTextColor() const
{
    return getColor(PDFSettingKeys::colorModesTextColor());
}

QColor PDFAppColorModesSettingsSection::getPageColor() const
{
    return getColor(PDFSettingKeys::colorModesPageColor());
}

void PDFAppColorModesSettingsSection::updateModeCards()
{
    const QString mode = getStoredOption(PDFSettingKeys::viewingColorMode());
    for (const auto& item : m_modeCards)
    {
        if (PDFPreviewCard* card = item.second.data())
        {
            const QSignalBlocker blocker(card);
            card->setSelected(item.first == mode);
        }
    }
}

void PDFAppColorModesSettingsSection::updatePresetCards()
{
    const QString presetId = getStoredOption(PDFSettingKeys::colorModesCustomPreset());
    const QColor pageColor = getPageColor();
    const QColor textColor = getTextColor();
    for (const auto& item : m_presetCards)
    {
        if (PDFReadingThemeCard* card = item.second.data())
        {
            const QSignalBlocker blocker(card);
            card->setSelected(item.first == presetId);
            if (item.first == PDFAppColorModePresets::getCustomId())
            {
                // Custom… shows the colors of the user
                card->setCustomColors(pageColor, textColor);
            }
        }
    }
}

void PDFAppColorModesSettingsSection::updateColors()
{
    const QColor pageColor = getPageColor();
    const QColor textColor = getTextColor();

    if (m_preview)
    {
        m_preview->setColors(pageColor, textColor);
    }

    // The contrast of the pair (WCAG): green check for Excellent and Good, amber
    // warning for Low, red cross for Poor
    const double ratio = PDFAppColorModePresets::getContrastRatio(textColor, pageColor);
    const PDFAppColorModePresets::Verdict verdict = PDFAppColorModePresets::getVerdict(ratio);
    if (m_contrastBadge)
    {
        switch (verdict)
        {
            case PDFAppColorModePresets::Verdict::Excellent:
            case PDFAppColorModePresets::Verdict::Good:
                m_contrastBadge->setIconName(QStringLiteral("check"));
                m_contrastBadge->setHue(PDFAccentHue::Green);
                break;
            case PDFAppColorModePresets::Verdict::Low:
                m_contrastBadge->setIconName(QStringLiteral("alert-triangle"));
                m_contrastBadge->setHue(PDFAccentHue::Amber);
                break;
            case PDFAppColorModePresets::Verdict::Poor:
                m_contrastBadge->setIconName(QStringLiteral("x"));
                m_contrastBadge->setHue(PDFAccentHue::Red);
                break;
        }
    }

    const QString ratioText = PDFAppColorModePresets::formatRatio(ratio);
    const QString verdictText = PDFAppColorModePresets::getVerdictText(verdict);
    if (m_contrastRatio)
    {
        m_contrastRatio->setText(ratioText);
    }
    if (m_contrastVerdict)
    {
        m_contrastVerdict->setText(verdictText);
    }
    if (m_contrastRow && m_contrastRow->getControlWidget())
    {
        m_contrastRow->getControlWidget()->setAccessibleName(tr("Contrast %1, %2").arg(ratioText, verdictText));
    }

    // The color buttons name the colors of a preset ("Amber", "Near black" of 160:4441)
    const PDFAppColorModePreset* preset = PDFAppColorModePresets::findPreset(getStoredOption(PDFSettingKeys::colorModesCustomPreset()));
    if (!preset || preset->isCustom() || preset->pageColor.rgb() != pageColor.rgb() || preset->textColor.rgb() != textColor.rgb())
    {
        preset = PDFAppColorModePresets::findPreset(PDFAppColorModePresets::findPresetId(pageColor, textColor));
    }

    const std::pair<QString, bool> buttons[] = { { QStringLiteral("text-color"), true }, { QStringLiteral("page-color"), false } };
    for (const std::pair<QString, bool>& item : buttons)
    {
        PDFAppSettingRow* row = findRow(item.first);
        PDFColorButton* button = row && row->getControl() ? row->getControl()->getColorButton() : nullptr;
        if (button && preset && !preset->isCustom())
        {
            const QColor& presetColor = item.second ? preset->textColor : preset->pageColor;
            if (button->getColor().rgb() == presetColor.rgb())
            {
                button->setColorName(item.second ? preset->getTextColorName() : preset->getPageColorName());
            }
        }
    }
}

void PDFAppColorModesSettingsSection::refresh(const QString& topic, const QString& key)
{
    BaseClass::refresh(topic, key);

    if (!topic.isEmpty() && topic != PDFSettingsRepository::getTopic())
    {
        return;
    }

    const bool isEverything = topic.isEmpty() || key.isEmpty();
    if (isEverything || key == PDFSettingKeys::viewingColorMode())
    {
        updateModeCards();
    }
    if (isEverything || key == PDFSettingKeys::colorModesCustomPreset() || key == PDFSettingKeys::colorModesTextColor() || key == PDFSettingKeys::colorModesPageColor())
    {
        updatePresetCards();
        updateColors();
    }
}

void PDFAppColorModesSettingsSection::commitPendingValues()
{
    QPointer<PDFAppColorModesSettingsSection> guard(this);
    BaseClass::commitPendingValues();
    if (guard)
    {
        writePending(true);
    }
}

void PDFAppColorModesSettingsSection::queueWrite(std::vector<std::pair<QString, QVariant>> values)
{
    // The latest click wins for each key
    for (std::pair<QString, QVariant>& value : values)
    {
        bool isMerged = false;
        for (std::pair<QString, QVariant>& pending : m_pendingValues)
        {
            if (pending.first == value.first)
            {
                pending.second = value.second;
                isMerged = true;
            }
        }
        if (!isMerged)
        {
            m_pendingValues.push_back(std::move(value));
        }
    }

    if (!m_isWriteQueued)
    {
        m_isWriteQueued = true;
        QMetaObject::invokeMethod(this, &PDFAppColorModesSettingsSection::onWriteQueued, Qt::QueuedConnection);
    }
}

void PDFAppColorModesSettingsSection::onWriteQueued()
{
    m_isWriteQueued = false;
    writePending(true);
}

void PDFAppColorModesSettingsSection::writePending(bool isNotified)
{
    if (m_pendingValues.empty())
    {
        return;
    }

    std::vector<std::pair<QString, QVariant>> values;
    values.swap(m_pendingValues);

    PDFAppContext* context = getContext();
    if (!context || !context->getDatabase())
    {
        return;
    }

    // One transaction: the preset and its two colors compile a document once
    const PDFDatabaseError error = context->getSettings().setValues(values);
    if (!isNotified || !error.isError())
    {
        return;
    }

    // The slots may delete the page with this section
    QPointer<PDFAppColorModesSettingsSection> guard(this);
    Q_EMIT statusMessageRequested(tr("The setting could not be saved: %1").arg(error.toString()));
    if (guard)
    {
        // The cards show the stored values again
        updateModeCards();
        updatePresetCards();
        updateColors();
    }
}

void PDFAppColorModesSettingsSection::onModeCardClicked()
{
    // The cards do not identify themselves in the signal
    QAbstractButton* card = qobject_cast<QAbstractButton*>(sender());
    if (!card)
    {
        return;
    }

    queueWrite({ { PDFSettingKeys::viewingColorMode(), card->property(PDFAppColorModesSettingsSectionHelper::VALUE_PROPERTY).toString() } });
}

void PDFAppColorModesSettingsSection::onPresetCardClicked()
{
    QAbstractButton* card = qobject_cast<QAbstractButton*>(sender());
    const PDFAppColorModePreset* preset = card ? PDFAppColorModePresets::findPreset(card->property(PDFAppColorModesSettingsSectionHelper::VALUE_PROPERTY).toString()) : nullptr;
    if (!preset)
    {
        return;
    }

    // A preset writes its two colors with it; Custom… keeps the colors of the user.
    // A chosen Reading theme is shown: the mode switches to Custom colors in the
    // same transaction (owner 2026-10-25, one compilation of the open documents).
    std::vector<std::pair<QString, QVariant>> values = { { PDFSettingKeys::viewingColorMode(), QStringLiteral("customColors") },
                                                         { PDFSettingKeys::colorModesCustomPreset(), preset->id } };
    if (!preset->isCustom())
    {
        values.emplace_back(PDFSettingKeys::colorModesTextColor(), PDFAppColorModePresets::formatColor(preset->textColor));
        values.emplace_back(PDFSettingKeys::colorModesPageColor(), PDFAppColorModePresets::formatColor(preset->pageColor));
    }
    queueWrite(std::move(values));
}

void PDFAppColorModesSettingsSection::onColorWritten(const QString& key)
{
    Q_UNUSED(key);

    // A color of the user, which is not the pair of the stored preset, switches
    // the preset to Custom… (Choosing a color switches the preset to Custom)
    const PDFAppColorModePreset* preset = PDFAppColorModePresets::findPreset(getStoredOption(PDFSettingKeys::colorModesCustomPreset()));
    if (!preset || preset->isCustom())
    {
        return;
    }

    if (preset->pageColor.rgb() != getPageColor().rgb() || preset->textColor.rgb() != getTextColor().rgb())
    {
        queueWrite({ { PDFSettingKeys::colorModesCustomPreset(), PDFAppColorModePresets::getCustomId() } });
    }
}

PDFAppSettingsSection* PDFAppColorModesSettingsSectionFactory::createSection(PDFAppContext* context, const QString& sectionId, QWidget* parent) const
{
    return new PDFAppColorModesSettingsSection(context, sectionId, parent);
}

}   // namespace pdf
