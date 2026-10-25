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

#ifndef PDFAPPSETTINGSSECTION_COLORMODES_H
#define PDFAPPSETTINGSSECTION_COLORMODES_H

#include "pdfappglobal.h"
#include "pdfappsettingssection.h"
#include "pdfappcolormodepreview.h"
#include "pdfappsettingrow.h"

#include "pdfcomponentbadges.h"
#include "pdfcomponentcards.h"
#include "pdffluentwidgets.h"

#include <QColor>
#include <QPointer>

#include <map>
#include <utility>
#include <vector>

class QButtonGroup;

namespace pdf
{

class PDFAppGrid;

/// Section Color modes (160:4203, package B of step 5):
///  - Color modes: the six cards of the modes (Color mode card 171:4545 =
///    PDFPreviewCard, an exclusive static group) - the key viewing.colorMode, the
///    same as the cards Page colors of the View panel of a document;
///  - Custom colors: the ten presets (Reading theme card = PDFReadingThemeCard,
///    Custom… last with the stored colors), the preview of the colors
///    (PDFAppColorModePreview), the color buttons of the text and of the page
///    (generic rows) and the contrast of the two colors (WCAG ratio and its verdict);
///  - Mode adjustments: the generic sliders.
/// A preset writes the preset and its two colors in one transaction (a document
/// is compiled once); a color picked by the user switches the preset to Custom.
/// The cards write from the event loop (a slot of a notification may delete the
/// page while a card emits its signals); a click, which is not written yet, is
/// written when the page is left.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppColorModesSettingsSection : public PDFAppGenericSettingsSection
{
    Q_OBJECT

private:
    using BaseClass = PDFAppGenericSettingsSection;

public:
    /// Geometry of 160:4350 / 160:4416 (cards of the modes 125 wide, of the presets 157)
    static constexpr int CARD_SPACING = 8;
    static constexpr int MODE_CARD_MINIMUM_WIDTH = 100;
    static constexpr int PRESET_CARD_MINIMUM_WIDTH = 124;
    static constexpr int MODE_COLUMNS = 6;
    static constexpr int PRESET_COLUMNS = 5;
    static constexpr int PREVIEW_SPACING = 8;
    static constexpr int PREVIEW_CAPTION_LINE_HEIGHT = 13;
    static constexpr int CONTRAST_SPACING = 8;
    static constexpr int CONTRAST_BADGE = 20;
    static constexpr int CONTRAST_ICON = 12;

    explicit PDFAppColorModesSettingsSection(PDFAppContext* context, const QString& sectionId, QWidget* parent = nullptr);
    virtual ~PDFAppColorModesSettingsSection() override;

    /// Ids of the color modes in the order of the cards (normal … customColors)
    static QStringList getModeIds();

    PDFPreviewCard* getModeCard(const QString& mode) const;
    PDFReadingThemeCard* getPresetCard(const QString& preset) const;
    PDFAppColorModePreview* getPreview() const { return m_preview; }
    PDFAppSettingRow* getContrastRow() const { return m_contrastRow; }
    PDFIconBadge* getContrastBadge() const { return m_contrastBadge; }
    PDFLabel* getContrastRatioLabel() const { return m_contrastRatio; }
    PDFLabel* getContrastVerdictLabel() const { return m_contrastVerdict; }

    /// Valid stored colors of the Custom colors mode (hostile values = defaults)
    QColor getTextColor() const;
    QColor getPageColor() const;

    /// A click of a card is waiting for the event loop to be written
    bool hasPendingWrite() const { return !m_pendingValues.empty(); }

    virtual void refresh(const QString& topic, const QString& key) override;
    virtual void commitPendingValues() override;

protected:
    virtual PDFAppSettingRow* createRow(const PDFSettingDefinition& definition) override;
    virtual void finishContent() override;

private:
    PDFAppSettingRow* createModeRow(const PDFSettingDefinition& definition);
    PDFAppSettingRow* createPresetRow(const PDFSettingDefinition& definition);
    PDFAppSettingRow* createPreviewRow();
    PDFAppSettingRow* createContrastRow();

    /// Search words of a block row of the definition (keywords, option titles)
    static QStringList getKeywords(const PDFSettingDefinition& definition);

    QColor getColor(const QString& key) const;
    QString getStoredOption(const QString& key) const;

    void updateModeCards();
    void updatePresetCards();
    void updateColors();

    /// Writes the values from the event loop (merged with values waiting already)
    void queueWrite(std::vector<std::pair<QString, QVariant>> values);
    void writePending(bool isNotified);

    void onModeCardClicked();
    void onPresetCardClicked();
    void onColorWritten(const QString& key);
    void onWriteQueued();

    QPointer<QButtonGroup> m_modeGroup;
    QPointer<QButtonGroup> m_presetGroup;
    std::map<QString, QPointer<PDFPreviewCard>> m_modeCards;
    std::map<QString, QPointer<PDFReadingThemeCard>> m_presetCards;
    QPointer<PDFAppColorModePreview> m_preview;
    QPointer<PDFAppSettingRow> m_contrastRow;
    QPointer<PDFIconBadge> m_contrastBadge;
    QPointer<PDFLabel> m_contrastRatio;
    QPointer<PDFLabel> m_contrastVerdict;
    std::vector<std::pair<QString, QVariant>> m_pendingValues;
    bool m_isWriteQueued = false;
};

/// Factory of the section
class PDF4QTLIBAPPSHARED_EXPORT PDFAppColorModesSettingsSectionFactory : public PDFAppSettingsSectionFactory
{
public:
    virtual PDFAppSettingsSection* createSection(PDFAppContext* context, const QString& sectionId, QWidget* parent) const override;
};

}   // namespace pdf

#endif // PDFAPPSETTINGSSECTION_COLORMODES_H
