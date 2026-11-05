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

#include "pdfapptoolgui_protect.h"

#include "pdfappdocument.h"
#include "pdfappdocumentpanels_common.h"
#include "pdfappdocumentview.h"
#include "pdfapppagelayout.h"
#include "pdfapptoolhost.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolparametermodel.h"
#include "pdfdocumenttoolguiregistry.h"

#include "pdfcomponentbadges.h"
#include "pdfcomponentdelegates.h"
#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfdrawspacecontroller.h"
#include "pdfdrawwidget.h"
#include "pdffluentwidgets.h"
#include "pdfpainterutils.h"
#include "pdfthemefonts.h"
#include "pdfthemeiconengine.h"
#include "pdfthememanager.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLocale>
#include <QPainter>
#include <QScrollBar>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>

namespace pdf
{

/// Helpers of the GUI of the protect tools
class PDFAppToolGUIProtectHelper
{
public:
    PDFAppToolGUIProtectHelper() = delete;

    static constexpr const char* PRESET_PROPERTY = "pdf4qtPreset";
    static constexpr const char* FILTER_PROPERTY = "pdf4qtFilter";

    /// The paddings of the left panels of the protect tools: 12 / 13 with the border (438:12615)
    /// against 10 / 11 of the frame of the panels
    static constexpr int LEFT_EXTRA_PADDING = 2;

    /// A count of a text, the singular right in English also without a translation
    static QString plural(const char* context, int count, const char* singular, const char* pluralText)
    {
        return QCoreApplication::translate(context, count == 1 ? singular : pluralText, nullptr, count);
    }

    static int toInt(qint64 value)
    {
        return int(qBound<qint64>(0, value, qint64(std::numeric_limits<int>::max())));
    }

    /// The links are the same rows of the list (their ids, places and texts)
    static bool isSameLinks(const std::vector<PDFDocumentLinkOperations::ExternalLink>& first, const std::vector<PDFDocumentLinkOperations::ExternalLink>& second)
    {
        if (first.size() != second.size())
        {
            return false;
        }
        for (size_t i = 0; i < first.size(); ++i)
        {
            const PDFDocumentLinkOperations::ExternalLink& a = first[i];
            const PDFDocumentLinkOperations::ExternalLink& b = second[i];
            if (a.id != b.id || a.pageIndex != b.pageIndex || a.annotationIndex != b.annotationIndex || a.kind != b.kind ||
                a.title != b.title || a.address != b.address || a.rectangle != b.rectangle)
            {
                return false;
            }
        }
        return true;
    }

    static void setLeftPadding(PDFAppToolPanelBuilder& builder)
    {
        if (QLayout* layout = builder.getWidget() ? builder.getWidget()->layout() : nullptr)
        {
            layout->setContentsMargins(LEFT_EXTRA_PADDING, 0, LEFT_EXTRA_PADDING, 0);
        }
    }

    /// The values of the rows of a key value list: the same keys keep their widgets
    static void setRows(PDFKeyValueList* list, const std::vector<std::pair<QString, QString>>& rows)
    {
        if (!list)
        {
            return;
        }

        bool isSameKeys = list->getRowCount() == int(rows.size());
        for (int i = 0; isSameKeys && i < int(rows.size()); ++i)
        {
            isSameKeys = list->getKey(i) == rows[size_t(i)].first;
        }

        if (!isSameKeys)
        {
            list->clear();
            for (const auto& row : rows)
            {
                list->addRow(row.first, row.second);
            }
            return;
        }

        for (int i = 0; i < int(rows.size()); ++i)
        {
            list->setValue(i, rows[size_t(i)].second);
        }
    }
};

// ------------------------------------------------------------ PDFAppToolGUIProtect

void PDFAppToolGUIProtect::registerTools()
{
    const QString sanitizeId = QString::fromLatin1(PDFDocumentToolSanitize::TOOL_ID);
    if (!PDFDocumentToolRegistry::isImplemented(sanitizeId))
    {
        PDFDocumentToolRegistry::registerTool(std::make_shared<PDFDocumentToolSanitize>());
    }
    if (!PDFDocumentToolGUIRegistry::getFactory(sanitizeId))
    {
        PDFDocumentToolGUIRegistry::registerFactory(sanitizeId, std::make_shared<PDFDocumentToolGUIFactoryOf<PDFAppToolGUISanitize>>());
    }

    const QString linksId = QString::fromLatin1(PDFDocumentToolRemoveLinks::TOOL_ID);
    if (!PDFDocumentToolRegistry::isImplemented(linksId))
    {
        PDFDocumentToolRegistry::registerTool(std::make_shared<PDFDocumentToolRemoveLinks>());
    }
    if (!PDFDocumentToolGUIRegistry::getFactory(linksId))
    {
        PDFDocumentToolGUIRegistry::registerFactory(linksId, std::make_shared<PDFDocumentToolGUIFactoryOf<PDFAppToolGUIRemoveLinks>>());
    }
}

// ------------------------------------------------------------ PDFAppToolCanvasPage

PDFAppToolCanvasPage::PDFAppToolCanvasPage(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("toolCanvasPage"));
    setFrameShape(QFrame::NoFrame);
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    viewport()->setAutoFillBackground(false);
}

PDFAppToolCanvasPage::~PDFAppToolCanvasPage() = default;

void PDFAppToolCanvasPage::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    // The track of the canvas (document-canvas 438:12621)
    QPainter painter(viewport());
    painter.fillRect(viewport()->rect(), PDFThemeManager::getInstance()->getColor(PDFThemeColors::BgTrack));
}

// ----------------------------------------------------------- PDFAppToolGUISanitize

PDFAppToolGUISanitize::PDFAppToolGUISanitize(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppToolGUISanitize::~PDFAppToolGUISanitize() = default;

QToolButton* PDFAppToolGUISanitize::getPresetButton() const
{
    return m_presetPanel ? m_presetPanel->getButton() : nullptr;
}

PDFTileRow* PDFAppToolGUISanitize::getPresetRow(Preset preset) const
{
    const auto it = m_presetRows.find(preset);
    return it != m_presetRows.end() ? it->second.data() : nullptr;
}

PDFTileRow* PDFAppToolGUISanitize::getKindRow(Kind kind) const
{
    const QString name = PDFDocumentHiddenDataScan::getName(kind);
    for (const QPointer<PDFAppToolChecklist>& checklist : m_checklists)
    {
        if (PDFTileRow* row = checklist ? checklist->getRow(name) : nullptr)
        {
            return row;
        }
    }
    return nullptr;
}

std::vector<PDFAppToolGUISanitize::Kind> PDFAppToolGUISanitize::getShownKinds() const
{
    std::vector<Kind> kinds = PDFDocumentHiddenDataScan::getKinds();
    if (!m_scan || !m_scan->get(Kind::SignatureFields).isPresent)
    {
        // The tenth kind (owner answer Q10) is a row of a document with signature fields only
        kinds.pop_back();
    }
    return kinds;
}

std::vector<PDFAppToolGUISanitize::Kind> PDFAppToolGUISanitize::getPresentKinds() const
{
    std::vector<Kind> kinds;
    for (Kind kind : getShownKinds())
    {
        if (!m_scan || m_scan->get(kind).isPresent)
        {
            kinds.push_back(kind);
        }
    }
    return kinds;
}

std::vector<PDFAppToolGUISanitize::Kind> PDFAppToolGUISanitize::getRemovedKinds() const
{
    std::vector<Kind> kinds;
    const PDFAppToolParameterModel* model = getModel();
    if (!model || !m_scan)
    {
        return kinds;
    }

    for (Kind kind : getPresentKinds())
    {
        if (model->getValue(PDFDocumentHiddenDataScan::getName(kind)).toBool())
        {
            kinds.push_back(kind);
        }
    }
    return kinds;
}

PDFAppToolGUISanitize::Preset PDFAppToolGUISanitize::getCurrentPreset() const
{
    const PDFAppToolParameterModel* model = getModel();
    return model ? PDFDocumentToolSanitize::findPreset(model->getValues(), getPresentKinds()) : Preset::Custom;
}

QString PDFAppToolGUISanitize::getPresetTitle(Preset preset)
{
    switch (preset)
    {
        case Preset::Standard:
            return tr("Standard");
        case Preset::BeforeSending:
            return tr("Before sending");
        case Preset::BeforePublishing:
            return tr("Before publishing");
        case Preset::Everything:
            return tr("Everything");
        case Preset::Custom:
            return tr("Custom");
    }
    return QString();
}

QString PDFAppToolGUISanitize::getKindDescription(Kind kind) const
{
    using Helper = PDFAppToolGUIProtectHelper;
    static constexpr const char* CONTEXT = "pdf::PDFAppToolGUISanitize";

    if (!m_scan)
    {
        return PDFDocumentToolSanitize::getKindDescription(kind);
    }

    const PDFDocumentHiddenDataScan::KindResult& found = m_scan->get(kind);
    if (!found.isPresent)
    {
        return tr("Not found in this document");
    }

    // The counts of 438:12588
    const int count = Helper::toInt(found.count);
    switch (kind)
    {
        case Kind::Metadata:
            return Helper::plural(CONTEXT, count, QT_TR_NOOP("%Ln XMP metadata stream"), QT_TR_NOOP("%Ln XMP metadata streams"));
        case Kind::Comments:
            return Helper::plural(CONTEXT, count, QT_TR_NOOP("%Ln comment, highlight or other markup annotation"), QT_TR_NOOP("%Ln comments, highlights and other markup annotations"));
        case Kind::Attachments:
            return tr("%1 · %2").arg(Helper::plural(CONTEXT, count, QT_TR_NOOP("%Ln attachment"), QT_TR_NOOP("%Ln attachments")), PDFDocumentToolFormat::formatSize(found.bytes));
        case Kind::Outline:
            return Helper::plural(CONTEXT, count, QT_TR_NOOP("%Ln bookmark"), QT_TR_NOOP("%Ln bookmarks"));
        case Kind::PageLabels:
            return m_scan->pageLabelsText.isEmpty() ? PDFDocumentToolSanitize::getKindDescription(kind) : tr("Custom numbering %1").arg(m_scan->pageLabelsText);
        case Kind::Thumbnails:
            return Helper::plural(CONTEXT, count, QT_TR_NOOP("%Ln embedded thumbnail"), QT_TR_NOOP("%Ln embedded thumbnails"));
        case Kind::InvisibleText:
            return found.isComplete ? Helper::plural(CONTEXT, count, QT_TR_NOOP("On %Ln scanned page"), QT_TR_NOOP("On %Ln scanned pages"))
                                    : Helper::plural(CONTEXT, count, QT_TR_NOOP("On at least %Ln scanned page"), QT_TR_NOOP("On at least %Ln scanned pages"));
        case Kind::SignatureFields:
            return Helper::plural(CONTEXT, count, QT_TR_NOOP("%Ln signature field with the name of its signer"), QT_TR_NOOP("%Ln signature fields with the names of their signers"));
        default:
            return PDFDocumentToolSanitize::getKindDescription(kind);
    }
}

void PDFAppToolGUISanitize::describeToolbar(PDFAppToolPanelBuilder& builder)
{
    // 438:12831: the presets tick the list, nothing runs
    m_presetPanel = builder.addMenu(QStringLiteral("preset"), QStringLiteral("sliders"), QString(), PRESET_PANEL_WIDTH);
    m_presetRows.clear();
    if (!m_presetPanel)
    {
        return;
    }

    m_presetPanel->addGroupCaption(tr("Presets tick the list – adjust it freely afterwards"));
    const std::vector<std::pair<Preset, QString>> presets = {
        { Preset::Standard, QStringLiteral("hidden-data") },
        { Preset::BeforeSending, QStringLiteral("mail") },
        { Preset::BeforePublishing, QStringLiteral("globe") },
        { Preset::Everything, QStringLiteral("select-all") },
    };
    for (const auto& [preset, iconName] : presets)
    {
        PDFTileRow* row = m_presetPanel->addRow(getPresetTitle(preset), QString(), iconName, PDFTileRow::Kind::MenuItem);
        row->setObjectName(QStringLiteral("toolPreset-%1").arg(int(preset)));
        row->setProperty(PDFAppToolGUIProtectHelper::PRESET_PROPERTY, int(preset));
        row->setSelectionBehavior(PDFComponentButton::SelectionBehavior::Exclusive);
        connect(row, &PDFTileRow::clicked, this, &PDFAppToolGUISanitize::onPresetRowClicked);
        m_presetRows[preset] = row;
    }

    // Clear all is no preset: its values are Custom (K18)
    m_presetPanel->addGroupDivider();
    PDFTileRow* clearRow = m_presetPanel->addRow(tr("Clear all"), tr("Untick everything and choose by hand"), QStringLiteral("select-none"), PDFTileRow::Kind::MenuItem);
    clearRow->setObjectName(QStringLiteral("toolPreset-clear"));
    clearRow->setProperty(PDFAppToolGUIProtectHelper::PRESET_PROPERTY, int(Preset::Custom));
    connect(clearRow, &PDFTileRow::clicked, this, &PDFAppToolGUISanitize::onPresetRowClicked);
    m_presetRows[Preset::Custom] = clearRow;

    updateParts();
}

void PDFAppToolGUISanitize::describeLeftPanel(PDFAppToolPanelBuilder& builder)
{
    PDFAppToolGUIProtectHelper::setLeftPadding(builder);

    const PDFAppDocument* document = getDocument();
    m_leftHeader = builder.addPanelHeader(tr("Hidden data"), tr("Found in %1").arg(document ? document->getTitle() : QString()));

    // section-Scan 438:12690
    builder.addSection(tr("Scan"));
    m_scanRows = builder.addKeyValues();
    builder.endSection();

    PDFAppToolCallout* callout = builder.addCallout(PDFAccentHue::Blue, QStringLiteral("info"), tr("Page content is not changed"),
                                                    tr("Text and images on the pages stay. Comments and markup disappear from the pages when their row is ticked. Visible content is removed with Redact Content."));
    callout->setCompactStyle();
    updateParts();
}

void PDFAppToolGUISanitize::describeRightPanel(PDFAppToolPanelBuilder& builder)
{
    // The state of the result dialog (438:14342) shows the steps of the result in the panel
    if (!m_isShellConnected)
    {
        if (PDFDocumentToolShell* shell = getShell())
        {
            connect(shell, &PDFDocumentToolShell::stateChanged, this, &PDFAppToolGUISanitize::onShellStateChanged);
            m_isShellConnected = true;
        }
    }

    // 438:12622: After removing, Result and the note
    QWidget* editing = new QWidget(builder.getWidget());
    editing->setObjectName(QStringLiteral("sanitizeEditing"));
    PDFAppToolPanelBuilder editingBuilder(this, editing, Qt::Vertical, getRightPanelSpacing());
    editingBuilder.addPanelHeader(tr("After removing"), tr("Summary of the result"));
    editingBuilder.addSection(tr("Result"));
    m_summaryRows = editingBuilder.addKeyValues();
    editingBuilder.endSection();
    editingBuilder.addHint(tr("The size before and after is measured when the tool runs and shown in the result."));
    builder.addWidget(editing);
    m_editingContent = editing;

    // 438:14376: Removed, Steps, File size
    QWidget* result = new QWidget(builder.getWidget());
    result->setObjectName(QStringLiteral("sanitizeResult"));
    PDFAppToolPanelBuilder resultBuilder(this, result, Qt::Vertical, getRightPanelSpacing());
    resultBuilder.addPanelHeader(tr("Removed"), tr("Result of each step"));
    resultBuilder.addSection(tr("Steps"));
    PDFAppToolCard* steps = new PDFAppToolCard(result);
    steps->setObjectName(QStringLiteral("sanitizeSteps"));
    steps->getBodyLayout()->setContentsMargins(7, 7, 7, 7);
    steps->getBodyLayout()->setSpacing(2);
    resultBuilder.addWidget(steps);
    m_stepsCard = steps;
    resultBuilder.endSection();
    resultBuilder.addSection(tr("File size"));
    m_sizeRows = resultBuilder.addKeyValues();
    resultBuilder.endSection();
    builder.addWidget(result);
    m_resultContent = result;

    updateParts();
    updateResultContent();
}

QWidget* PDFAppToolGUISanitize::createCanvasPage(QWidget* parent)
{
    m_canvasPage = new PDFAppToolCanvasPage(parent);
    buildForm();
    return m_canvasPage.data();
}

void PDFAppToolGUISanitize::buildForm()
{
    if (!m_canvasPage)
    {
        return;
    }

    // The old form goes later: it may hold the sender of the change
    if (QWidget* old = m_canvasPage->takeWidget())
    {
        old->hide();
        old->deleteLater();
    }
    m_checklists.clear();

    QWidget* content = new QWidget();
    content->setObjectName(QStringLiteral("sanitizeContent"));
    QHBoxLayout* centering = new QHBoxLayout(content);
    centering->setContentsMargins(0, FORM_TOP, 0, FORM_TOP);

    // form 438:12709: 640 wide in the middle of the canvas
    QWidget* form = new QWidget(content);
    form->setObjectName(QStringLiteral("sanitizeForm"));
    form->setFixedWidth(FORM_WIDTH);
    QVBoxLayout* layout = new QVBoxLayout(form);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(FORM_GAP);
    centering->addStretch(1);
    centering->addWidget(form, 0, Qt::AlignTop);
    centering->addStretch(1);
    m_form = form;

    // heading 438:12710: Semi Bold 18 in lines of 22, the text Regular 12 text/secondary, 2 apart
    QWidget* heading = new QWidget(form);
    QVBoxLayout* headingLayout = new QVBoxLayout(heading);
    headingLayout->setContentsMargins(0, 0, 0, 0);
    headingLayout->setSpacing(2);
    PDFLabel* title = PDFAppPageWidgets::createLabel(tr("Choose what to remove"), QFont::DemiBold, 18, PDFThemeColors::TextPrimary, heading);
    title->setLineHeight(22);
    headingLayout->addWidget(title);
    headingLayout->addWidget(PDFAppPageWidgets::createText(tr("Ticked kinds of hidden data are removed from the document."), PDFThemeFonts::getFont(QFont::Normal, 12), 15, PDFThemeColors::TextSecondary, heading));
    layout->addWidget(heading);

    // 438:13839: nothing found
    m_cleanCallout = new PDFAppToolCallout(PDFAccentHue::Green, QStringLiteral("check-circle"), tr("Nothing hidden was found"),
                                           tr("This document has no document info, metadata, comments, attachments or other hidden data."), form);
    m_cleanCallout->setObjectName(QStringLiteral("sanitizeCleanCallout"));
    m_cleanCallout->setCompactStyle();
    layout->addWidget(m_cleanCallout);

    // The three cards of 438:12713, 438:12736, 438:12779; the signature fields go to the document
    const std::vector<Kind> shownKinds = getShownKinds();
    const std::vector<std::pair<QString, std::vector<Kind>>> groups = {
        { tr("About the document"), { Kind::DocumentInfo, Kind::Metadata } },
        { tr("Inside the document"), { Kind::Comments, Kind::Attachments, Kind::Outline, Kind::PageLabels, Kind::SignatureFields } },
        { tr("Technical data"), { Kind::SearchIndex, Kind::Thumbnails, Kind::InvisibleText } },
    };
    for (const auto& [caption, kinds] : groups)
    {
        std::vector<PDFAppToolFlagItem> items;
        for (Kind kind : kinds)
        {
            if (std::find(shownKinds.cbegin(), shownKinds.cend(), kind) == shownKinds.cend())
            {
                continue;
            }

            PDFAppToolFlagItem item;
            item.parameter = PDFDocumentHiddenDataScan::getName(kind);
            item.title = PDFDocumentToolSanitize::getKindTitle(kind);
            item.description = getKindDescription(kind);
            item.iconName = PDFDocumentToolSanitize::getKindIconName(kind);
            item.groupCaption = items.empty() ? caption : QString();
            items.push_back(std::move(item));
        }

        PDFAppToolChecklist* checklist = new PDFAppToolChecklist(getModel(), std::move(items), form);
        layout->addWidget(checklist);
        m_checklists.emplace_back(checklist);
    }

    // 438:13356: the OCR layer of scanned pages is ticked
    m_invisibleTextCallout = new PDFAppToolCallout(PDFAccentHue::Amber, QStringLiteral("alert-triangle"), QString(),
                                                   tr("The invisible text is the OCR layer of the scanned pages. Without it the pages are images only."), form);
    m_invisibleTextCallout->setObjectName(QStringLiteral("sanitizeInvisibleTextCallout"));
    m_invisibleTextCallout->setCompactStyle();
    layout->addWidget(m_invisibleTextCallout);

    m_canvasPage->setWidget(content);
    updateForm();
}

void PDFAppToolGUISanitize::updateForm()
{
    using Helper = PDFAppToolGUIProtectHelper;

    for (Kind kind : getShownKinds())
    {
        PDFTileRow* row = getKindRow(kind);
        if (!row)
        {
            continue;
        }

        const bool isAvailable = !m_scan || m_scan->get(kind).isPresent;
        row->setTag(isAvailable ? QString() : tr("Not found"));
        row->setDescription(getKindDescription(kind));
        for (const QPointer<PDFAppToolChecklist>& checklist : m_checklists)
        {
            if (checklist && checklist->getRow(PDFDocumentHiddenDataScan::getName(kind)))
            {
                checklist->setRowAvailable(PDFDocumentHiddenDataScan::getName(kind), isAvailable);
            }
        }
    }

    if (m_cleanCallout)
    {
        m_cleanCallout->setVisible(m_scan && getPresentKinds().empty());
    }

    if (m_invisibleTextCallout)
    {
        const std::vector<Kind> removed = getRemovedKinds();
        const bool isInvisibleRemoved = std::find(removed.cbegin(), removed.cend(), Kind::InvisibleText) != removed.cend();
        const int pages = m_scan ? Helper::toInt(m_scan->get(Kind::InvisibleText).count) : 0;
        m_invisibleTextCallout->setTexts(Helper::plural("pdf::PDFAppToolGUISanitize", pages, QT_TR_NOOP("Search and copy will stop working on %Ln page"), QT_TR_NOOP("Search and copy will stop working on %Ln pages")),
                                         tr("The invisible text is the OCR layer of the scanned pages. Without it the pages are images only."));
        m_invisibleTextCallout->setVisible(isInvisibleRemoved);
    }
}

void PDFAppToolGUISanitize::updateParts()
{
    using Helper = PDFAppToolGUIProtectHelper;
    static constexpr const char* CONTEXT = "pdf::PDFAppToolGUISanitize";

    const Preset preset = getCurrentPreset();
    if (QToolButton* button = getPresetButton())
    {
        button->setText(tr("Preset: %1").arg(getPresetTitle(preset)));
        button->setToolTip(button->text());
    }

    const int shownCount = int(getShownKinds().size());
    const std::vector<std::pair<Preset, const char*>> descriptions = {
        { Preset::Standard, QT_TR_NOOP("Everything except the invisible text") },
        { Preset::BeforeSending, QT_TR_NOOP("Info, metadata, comments, index and thumbnails") },
        { Preset::BeforePublishing, QT_TR_NOOP("Also attachments – outline and page labels stay") },
    };
    for (const auto& [rowPreset, description] : descriptions)
    {
        if (PDFTileRow* row = getPresetRow(rowPreset))
        {
            row->setDescription(tr(description));
        }
    }
    if (PDFTileRow* row = getPresetRow(Preset::Everything))
    {
        // "All nine kinds including the OCR text layer"; a document with signature fields has ten
        row->setDescription(shownCount > 9 ? tr("All ten kinds including the OCR text layer and the signature fields") : tr("All nine kinds including the OCR text layer"));
    }
    for (const auto& [rowPreset, row] : m_presetRows)
    {
        if (row && rowPreset != Preset::Custom)
        {
            const QSignalBlocker blocker(row);
            row->setChecked(rowPreset == preset);
        }
    }

    // Scan 438:12690: Kinds found, File size, Pages
    const PDFAppDocument* document = getDocument();
    const QString none = QStringLiteral("–");
    if (m_scan)
    {
        const qint64 fileSize = document ? document->getSnapshot().originalData.size() : 0;
        Helper::setRows(m_scanRows, {
            { tr("Kinds found"), tr("%1 of %2").arg(int(getPresentKinds().size())).arg(shownCount) },
            { tr("File size"), fileSize > 0 ? PDFDocumentToolFormat::formatSize(fileSize) : none },
            { tr("Pages"), QLocale().toString(qlonglong(qMax<qint64>(0, m_pageCount))) },
        });
    }
    else
    {
        Helper::setRows(m_scanRows, { { tr("Kinds found"), none }, { tr("File size"), none }, { tr("Pages"), none } });
    }

    // Result 438:12814: Removed, Kept, Invisible text
    if (m_scan)
    {
        const std::vector<Kind> removed = getRemovedKinds();
        const int presentCount = int(getPresentKinds().size());
        const int keptCount = presentCount - int(removed.size());
        const bool isInvisibleRemoved = std::find(removed.cbegin(), removed.cend(), Kind::InvisibleText) != removed.cend();
        Helper::setRows(m_summaryRows, {
            { tr("Removed"), Helper::plural(CONTEXT, int(removed.size()), QT_TR_NOOP("%Ln kind of data"), QT_TR_NOOP("%Ln kinds of data")) },
            { tr("Kept"), presentCount == 0 ? none : PDFDocumentToolSanitize::formatKindCount(keptCount, false) },
            { tr("Invisible text"), isInvisibleRemoved ? tr("Removed") : tr("Kept") },
        });
    }
    else
    {
        Helper::setRows(m_summaryRows, { { tr("Removed"), none }, { tr("Kept"), none }, { tr("Invisible text"), none } });
    }

    updateForm();
}

void PDFAppToolGUISanitize::updateResultContent()
{
    const PDFDocumentToolShell* shell = getShell();
    const bool isDeciding = shell && shell->getState() == PDFDocumentToolShell::State::Deciding && m_result.has_value();
    if (m_editingContent)
    {
        m_editingContent->setVisible(!isDeciding);
    }
    if (m_resultContent)
    {
        m_resultContent->setVisible(isDeciding);
    }
    if (!isDeciding || !m_stepsCard)
    {
        return;
    }

    // Steps 438:14571: a row per removed kind, the check of a done step
    PDFAppToolCard* steps = qobject_cast<PDFAppToolCard*>(m_stepsCard.data());
    if (!steps)
    {
        return;
    }
    for (PDFTileRow* row : steps->findChildren<PDFTileRow*>(QString(), Qt::FindDirectChildrenOnly))
    {
        delete row;
    }

    const QJsonArray removedKinds = m_result->data.value(QStringLiteral("removedKinds")).toArray();
    for (size_t i = 0; i < m_result->steps.size(); ++i)
    {
        const PDFDocumentToolStep& step = m_result->steps[i];
        QString title;
        const QString name = i < size_t(removedKinds.size()) ? removedKinds.at(qsizetype(i)).toString() : QString();
        for (Kind kind : PDFDocumentHiddenDataScan::getKinds())
        {
            if (PDFDocumentHiddenDataScan::getName(kind) == name)
            {
                title = PDFDocumentToolSanitize::getKindTitle(kind);
            }
        }

        PDFTileRow* row = new PDFTileRow(title, step.text, step.iconName, PDFTileRow::Kind::Result, steps);
        row->setObjectName(QStringLiteral("sanitizeStep-") + name);
        row->setTone(PDFTileRow::Tone::Muted);
        row->setEndIcon(QStringLiteral("check-circle"), PDFThemeColors::AccentGreenIcon);
        row->setClickable(false);
        steps->getBodyLayout()->addWidget(row);
    }

    // File size 438:14645: Before, After, Saved
    const qint64 before = m_result->data.value(QStringLiteral("sizeBefore")).toInteger();
    const qint64 after = m_result->data.value(QStringLiteral("sizeAfter")).toInteger();
    const qint64 saved = m_result->data.value(QStringLiteral("savedBytes")).toInteger();
    const int percent = m_result->data.value(QStringLiteral("savedPercent")).toInt();
    PDFAppToolGUIProtectHelper::setRows(m_sizeRows, {
        { tr("Before"), PDFDocumentToolFormat::formatSize(before) },
        { tr("After"), PDFDocumentToolFormat::formatSize(after) },
        { tr("Saved"), saved > 0 ? tr("%1 · %2 %").arg(PDFDocumentToolFormat::formatSize(saved)).arg(percent) : QStringLiteral("–") },
    });
}

QVariantMap PDFAppToolGUISanitize::getPreviewParameters() const
{
    // The scan does not depend on the ticks: one preview per version of the document
    return QVariantMap();
}

bool PDFAppToolGUISanitize::isConfirmEnabled() const
{
    // 438:13607, 438:13839: nothing is chosen, nothing is found
    return BaseClass::isConfirmEnabled() && m_scan.has_value() && !getRemovedKinds().empty();
}

QString PDFAppToolGUISanitize::getStatusText() const
{
    if (!m_scan)
    {
        return BaseClass::getStatusText();
    }
    if (getPresentKinds().empty())
    {
        return tr("No hidden data found in this document");
    }

    const int removed = int(getRemovedKinds().size());
    if (removed == 0)
    {
        return tr("Choose at least one kind of data to remove");
    }
    return tr("%1 will be removed · nothing is changed until you confirm").arg(PDFDocumentToolSanitize::formatKindCount(removed, true));
}

QString PDFAppToolGUISanitize::getRunningTitle(int percentage) const
{
    Q_UNUSED(percentage);

    // 438:14093: the title has no percentage, the bar says it
    return tr("Removing hidden data…");
}

QString PDFAppToolGUISanitize::getRunningStatusText(const QString& stepText) const
{
    // 438:14093: "Removing hidden data… step 3 of 8" of the step "Comments and markup · step 3 of 8"
    const qsizetype separator = stepText.lastIndexOf(QStringLiteral(" · "));
    const QString step = separator >= 0 ? stepText.mid(separator + 3) : stepText;
    return step.isEmpty() ? getRunningTitle(0) : tr("Removing hidden data… %1").arg(step);
}

QString PDFAppToolGUISanitize::getRunningTimeText(int seconds) const
{
    Q_UNUSED(seconds);

    // 438:14093: the card has no time left (74 high)
    return QString();
}

void PDFAppToolGUISanitize::fillResultInput(PDFAppToolResultInput* input) const
{
    // 438:14342
    input->title = tr("Hidden data removed");
    input->statusText = tr("Hidden data removed – choose what to do with the result");
    m_dialogResult = input->result;
}

void PDFAppToolGUISanitize::onParametersChanged(const QString& name)
{
    Q_UNUSED(name);
    updateParts();
}

void PDFAppToolGUISanitize::onPreviewChanged(const PDFDocumentToolResult* preview)
{
    const bool hadSignatures = getShownKinds().size() > 9;
    if (preview && preview->isSuccess())
    {
        m_scan = PDFDocumentHiddenDataScan::Result::fromJson(preview->data.value(QStringLiteral("scan")).toObject());
        m_pageCount = preview->data.value(QStringLiteral("pageCount")).toInteger();
    }
    else
    {
        // A new version of the document is scanned again
        m_scan.reset();
    }

    // The row of the signature fields comes and goes with them
    if ((getShownKinds().size() > 9) != hadSignatures)
    {
        buildForm();
    }

    QPointer<PDFAppToolGUISanitize> guard(this);
    updateParts();
    updateConfirmState();
    if (guard)
    {
        updateStatusText();
    }
}

void PDFAppToolGUISanitize::retranslate()
{
    buildForm();
}

void PDFAppToolGUISanitize::onPresetRowClicked()
{
    PDFTileRow* row = qobject_cast<PDFTileRow*>(sender());
    PDFAppToolParameterModel* model = getModel();
    if (!row || !model)
    {
        return;
    }

    const Preset preset = static_cast<Preset>(row->property(PDFAppToolGUIProtectHelper::PRESET_PROPERTY).toInt());

    // The panel closes first: the change of the values may start anything
    QPointer<PDFAppToolGUISanitize> guard(this);
    if (m_presetPanel)
    {
        m_presetPanel->close();
    }
    if (guard && getModel())
    {
        getModel()->setValues(PDFDocumentToolSanitize::getPresetValues(preset));
    }
}

void PDFAppToolGUISanitize::onShellStateChanged(PDFDocumentToolShell::State state)
{
    if (state == PDFDocumentToolShell::State::Deciding && m_dialogResult)
    {
        m_result = std::move(m_dialogResult);
        m_dialogResult.reset();
    }
    else if (state != PDFDocumentToolShell::State::Deciding)
    {
        m_result.reset();
    }
    updateResultContent();
}

// ---------------------------------------------------------- PDFAppToolLinkListModel

PDFAppToolLinkListModel::PDFAppToolLinkListModel(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppToolLinkListModel::~PDFAppToolLinkListModel() = default;

bool PDFAppToolLinkListModel::isShown(const PDFDocumentLinkOperations::ExternalLink& link, Filter filter)
{
    switch (filter)
    {
        case Filter::Web:
            return link.kind == PDFDocumentLinkOperations::LinkKind::Web;
        case Filter::EMail:
            return link.kind == PDFDocumentLinkOperations::LinkKind::EMail;
        default:
            return true;
    }
}

void PDFAppToolLinkListModel::setLinks(std::vector<PDFDocumentLinkOperations::ExternalLink> links)
{
    beginResetModel();
    m_links = std::move(links);
    rebuildRows();
    endResetModel();
}

void PDFAppToolLinkListModel::setKeptIds(std::set<QString> ids)
{
    if (m_keptIds == ids)
    {
        return;
    }

    m_keptIds = std::move(ids);
    if (!m_rows.empty())
    {
        Q_EMIT dataChanged(index(0), index(int(m_rows.size()) - 1), { Qt::CheckStateRole });
    }
}

void PDFAppToolLinkListModel::setFilter(Filter filter)
{
    if (m_filter == filter)
    {
        return;
    }

    beginResetModel();
    m_filter = filter;
    rebuildRows();
    endResetModel();
}

void PDFAppToolLinkListModel::rebuildRows()
{
    m_rows.clear();
    PDFInteger lastPage = -1;
    for (size_t i = 0; i < m_links.size(); ++i)
    {
        const PDFDocumentLinkOperations::ExternalLink& link = m_links[i];
        if (!isShown(link, m_filter))
        {
            continue;
        }

        // The caption of a page (438:14866 "Page 3")
        if (link.pageIndex != lastPage)
        {
            m_rows.push_back({ link.pageIndex, -1 });
            lastPage = link.pageIndex;
        }
        m_rows.push_back({ link.pageIndex, int(i) });
    }
}

const PDFDocumentLinkOperations::ExternalLink* PDFAppToolLinkListModel::getLink(int row) const
{
    if (row < 0 || row >= int(m_rows.size()) || m_rows[size_t(row)].linkIndex < 0)
    {
        return nullptr;
    }
    return &m_links[size_t(m_rows[size_t(row)].linkIndex)];
}

int PDFAppToolLinkListModel::getRow(const QString& id) const
{
    for (size_t i = 0; i < m_rows.size(); ++i)
    {
        if (const PDFDocumentLinkOperations::ExternalLink* link = getLink(int(i)); link && link->id == id)
        {
            return int(i);
        }
    }
    return -1;
}

std::vector<const PDFDocumentLinkOperations::ExternalLink*> PDFAppToolLinkListModel::getShownLinks() const
{
    std::vector<const PDFDocumentLinkOperations::ExternalLink*> links;
    for (const PDFDocumentLinkOperations::ExternalLink& link : m_links)
    {
        if (isShown(link, m_filter))
        {
            links.push_back(&link);
        }
    }
    return links;
}

int PDFAppToolLinkListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant PDFAppToolLinkListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= int(m_rows.size()))
    {
        return QVariant();
    }

    const Row& row = m_rows[size_t(index.row())];
    const PDFDocumentLinkOperations::ExternalLink* link = getLink(index.row());
    if (!link)
    {
        switch (role)
        {
            case Qt::DisplayRole:
                return tr("Page %1").arg(QLocale().toString(qlonglong(row.pageIndex + 1)));
            case PDFComponentRoles::TileRowKindRole:
                return int(PDFComponentPainter::TileRowKind::Caption);
            default:
                return QVariant();
        }
    }

    switch (role)
    {
        case Qt::DisplayRole:
            return link->title;
        case Qt::ToolTipRole:
            return link->address;
        case PDFComponentRoles::SubtitleRole:
            return link->address;
        case PDFComponentRoles::IconNameRole:
            return link->kind == PDFDocumentLinkOperations::LinkKind::EMail ? QStringLiteral("mail") : QStringLiteral("globe");
        case Qt::CheckStateRole:
            return m_keptIds.count(link->id) ? Qt::Unchecked : Qt::Checked;
        case Qt::AccessibleDescriptionRole:
            return m_keptIds.count(link->id) ? tr("Kept") : tr("Removed");
        default:
            return QVariant();
    }
}

bool PDFAppToolLinkListModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    const PDFDocumentLinkOperations::ExternalLink* link = index.isValid() ? getLink(index.row()) : nullptr;
    if (role != Qt::CheckStateRole || !link)
    {
        return false;
    }

    // The GUI writes the parameter, the check box follows it (setKeptIds); the signal is last
    const QString id = link->id;
    Q_EMIT linkChecked(id, value.toInt() == Qt::Checked);
    return true;
}

Qt::ItemFlags PDFAppToolLinkListModel::flags(const QModelIndex& index) const
{
    if (!getLink(index.row()))
    {
        // A caption is neither selected nor the current item
        return Qt::NoItemFlags;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable;
}

// ------------------------------------------------------------ PDFAppToolLinkOverlay

PDFAppToolLinkOverlay::PDFAppToolLinkOverlay(PDFWidget* pdfWidget, QWidget* canvasArea) :
    BaseClass(canvasArea),
    m_pdfWidget(pdfWidget),
    m_proxy(pdfWidget ? pdfWidget->getDrawWidgetProxy() : nullptr),
    m_canvasArea(canvasArea),
    m_drawWidget(pdfWidget && pdfWidget->getDrawWidget() ? pdfWidget->getDrawWidget()->getWidget() : nullptr)
{
    setObjectName(QStringLiteral("toolLinkOverlay"));
    setAttribute(Qt::WA_TransparentForMouseEvents);

    // preview-bar 438:14940: "Page 12" Semi Bold 13 in a bar of 36
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_caption = PDFAppPageWidgets::createLabel(QString(), QFont::DemiBold, 13, PDFThemeColors::TextPrimary, this);
    m_caption->setObjectName(QStringLiteral("toolLinkPageCaption"));
    layout->addWidget(m_caption, 0, Qt::AlignVCenter);

    if (m_canvasArea)
    {
        m_canvasArea->installEventFilter(this);
    }
    if (m_proxy)
    {
        m_proxy->registerDrawInterface(this);
    }
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, &PDFAppToolLinkOverlay::updateCanvas);
    updatePlacement();
}

PDFAppToolLinkOverlay::~PDFAppToolLinkOverlay()
{
    if (m_proxy)
    {
        m_proxy->unregisterDrawInterface(this);
    }
    if (m_drawWidget)
    {
        m_drawWidget->update();
    }
}

void PDFAppToolLinkOverlay::setLinks(const std::vector<PDFDocumentLinkOperations::ExternalLink>* links, const std::set<QString>* keptIds, const QString& selectedId)
{
    m_pageLinks.clear();
    if (links)
    {
        for (const PDFDocumentLinkOperations::ExternalLink& link : *links)
        {
            m_pageLinks[link.pageIndex].push_back(link);
        }
    }
    m_keptIds = keptIds ? *keptIds : std::set<QString>();
    m_selectedId = selectedId;
    updateCanvas();
}

void PDFAppToolLinkOverlay::setPageText(const QString& text)
{
    if (m_caption)
    {
        m_caption->setText(text);
    }
    updatePlacement();
}

QString PDFAppToolLinkOverlay::getPageText() const
{
    return m_caption ? m_caption->text() : QString();
}

PDFAppToolLinkOverlay::LinkState PDFAppToolLinkOverlay::getLinkState(const PDFDocumentLinkOperations::ExternalLink& link) const
{
    if (!m_selectedId.isEmpty() && link.id == m_selectedId)
    {
        return LinkState::Selected;
    }
    return m_keptIds.count(link.id) ? LinkState::Kept : LinkState::Removed;
}

void PDFAppToolLinkOverlay::drawPage(QPainter* painter,
                                     pdf::PDFInteger pageIndex,
                                     const PDFPrecompiledPage* compiledPage,
                                     PDFTextLayoutGetter& layoutGetter,
                                     const QTransform& pagePointToDevicePointMatrix,
                                     const PDFColorConvertor& convertor,
                                     QList<PDFRenderError>& errors) const
{
    Q_UNUSED(compiledPage);
    Q_UNUSED(layoutGetter);
    Q_UNUSED(convertor);
    Q_UNUSED(errors);

    const auto it = m_pageLinks.find(pageIndex);
    if (it == m_pageLinks.cend())
    {
        return;
    }

    // 438:14942: link-remove bg/accent at 13 % with a line of bg/accent, link-kept text/tertiary
    // at 18 %, link-selected bg/accent at 20 % with border/focus 2, all with the radius 2
    const PDFThemeManager* manager = PDFThemeManager::getInstance();
    PDFPainterStateGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    for (const PDFDocumentLinkOperations::ExternalLink& link : it->second)
    {
        const QRectF rect = pagePointToDevicePointMatrix.mapRect(link.rectangle).normalized();
        if (!rect.isValid())
        {
            continue;
        }

        QColor fill;
        QPen pen(Qt::NoPen);
        switch (getLinkState(link))
        {
            case LinkState::Selected:
                fill = manager->getColor(PDFThemeColors::BgAccent);
                fill.setAlphaF(0.20f);
                pen = QPen(manager->getColor(PDFThemeColors::BorderFocus), 2.0);
                break;
            case LinkState::Kept:
                fill = manager->getColor(PDFThemeColors::TextTertiary);
                fill.setAlphaF(0.18f);
                break;
            case LinkState::Removed:
                fill = manager->getColor(PDFThemeColors::BgAccent);
                fill.setAlphaF(0.13f);
                pen = QPen(manager->getColor(PDFThemeColors::BgAccent), 1.0);
                break;
        }

        painter->setBrush(fill);
        painter->setPen(pen);
        const qreal inset = pen.style() == Qt::NoPen ? 0.0 : pen.widthF() / 2.0;
        painter->drawRoundedRect(rect.adjusted(inset, inset, -inset, -inset), 2.0, 2.0);
    }
}

bool PDFAppToolLinkOverlay::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_canvasArea && (event->type() == QEvent::Resize || event->type() == QEvent::LayoutDirectionChange))
    {
        updatePlacement();
    }
    return BaseClass::eventFilter(watched, event);
}

void PDFAppToolLinkOverlay::updatePlacement()
{
    if (!m_canvasArea)
    {
        return;
    }

    // At 20, 20 of the canvas, at its leading edge
    const int width = qMax(0, m_canvasArea->width() - 2 * CAPTION_OFFSET);
    const int captionWidth = qMin(width, m_caption ? m_caption->sizeHint().width() : 0);
    const int left = m_canvasArea->layoutDirection() == Qt::RightToLeft ? m_canvasArea->width() - CAPTION_OFFSET - captionWidth : CAPTION_OFFSET;
    setGeometry(left, CAPTION_OFFSET, captionWidth, CAPTION_HEIGHT);
    setVisible(m_caption && !m_caption->text().isEmpty());
    raise();
}

void PDFAppToolLinkOverlay::updateCanvas()
{
    if (m_drawWidget)
    {
        m_drawWidget->update();
    }
}

// ---------------------------------------------------------- PDFAppToolGUIRemoveLinks

PDFAppToolGUIRemoveLinks::PDFAppToolGUIRemoveLinks(QObject* parent) :
    BaseClass(parent)
{
    m_model = new PDFAppToolLinkListModel(this);
    connect(m_model.data(), &PDFAppToolLinkListModel::linkChecked, this, &PDFAppToolGUIRemoveLinks::onLinkChecked);
}

PDFAppToolGUIRemoveLinks::~PDFAppToolGUIRemoveLinks() = default;

QToolButton* PDFAppToolGUIRemoveLinks::getShowButton() const
{
    return m_showPanel ? m_showPanel->getButton() : nullptr;
}

PDFTileRow* PDFAppToolGUIRemoveLinks::getFilterRow(Filter filter) const
{
    const auto it = m_filterRows.find(filter);
    return it != m_filterRows.end() ? it->second.data() : nullptr;
}

int PDFAppToolGUIRemoveLinks::getKeptCount() const
{
    int count = 0;
    const std::set<QString> keptIds = m_model ? m_model->getKeptIds() : std::set<QString>();
    for (const PDFDocumentLinkOperations::ExternalLink& link : m_links)
    {
        count += keptIds.count(link.id) ? 1 : 0;
    }
    return count;
}

int PDFAppToolGUIRemoveLinks::getRemovedCount() const
{
    return int(m_links.size()) - getKeptCount();
}

QString PDFAppToolGUIRemoveLinks::getFilterTitle(Filter filter)
{
    switch (filter)
    {
        case Filter::Web:
            return tr("Web addresses");
        case Filter::EMail:
            return tr("E-mail addresses");
        default:
            return tr("All links");
    }
}

QString PDFAppToolGUIRemoveLinks::getFilterIconName(Filter filter)
{
    switch (filter)
    {
        case Filter::Web:
            return QStringLiteral("globe");
        case Filter::EMail:
            return QStringLiteral("mail");
        default:
            return QStringLiteral("list");
    }
}

void PDFAppToolGUIRemoveLinks::describeToolbar(PDFAppToolPanelBuilder& builder)
{
    // 438:14771: Select All, Select None, a divider, Show
    m_selectAllButton = builder.addAction(tr("Select All"), QStringLiteral("select-all"), this, &PDFAppToolGUIRemoveLinks::selectAll);
    m_selectNoneButton = builder.addAction(tr("Select None"), QStringLiteral("select-none"), this, &PDFAppToolGUIRemoveLinks::selectNone);
    builder.addActionDivider();

    m_showPanel = builder.addMenu(QStringLiteral("show"), QStringLiteral("list"), QString(), SHOW_PANEL_WIDTH);
    m_filterRows.clear();
    if (m_showPanel)
    {
        m_showPanel->addGroupCaption(tr("Show in the list"));
        for (Filter filter : { Filter::All, Filter::Web, Filter::EMail })
        {
            PDFTileRow* row = m_showPanel->addRow(getFilterTitle(filter), QString(), getFilterIconName(filter), PDFTileRow::Kind::MenuItem);
            row->setObjectName(QStringLiteral("toolShow-%1").arg(int(filter)));
            row->setProperty(PDFAppToolGUIProtectHelper::FILTER_PROPERTY, int(filter));
            row->setSelectionBehavior(PDFComponentButton::SelectionBehavior::Exclusive);
            connect(row, &PDFTileRow::clicked, this, &PDFAppToolGUIRemoveLinks::onFilterRowClicked);
            m_filterRows[filter] = row;
        }
        m_showPanel->addGroupDivider();
        m_showPanel->addGroupCaption(tr("The filter changes the list only, not what is removed"));
    }
    updateParts();
}

void PDFAppToolGUIRemoveLinks::describeLeftPanel(PDFAppToolPanelBuilder& builder)
{
    PDFAppToolGUIProtectHelper::setLeftPadding(builder);
    m_leftHeader = builder.addPanelHeader(tr("External links"), QString());

    // card-links 438:14865: padding 6 + the border, the rows 2 apart
    PDFAppToolCard* card = new PDFAppToolCard(builder.getWidget());
    card->setObjectName(QStringLiteral("linksCard"));
    card->getBodyLayout()->setContentsMargins(LIST_PADDING, LIST_PADDING, LIST_PADDING, LIST_PADDING);

    PDFTileRowDelegate* delegate = new PDFTileRowDelegate(card);
    delegate->setKind(PDFTileRowDelegate::Kind::ListItem);
    delegate->setTitleKind(PDFTileRowDelegate::TextKind::Data);
    delegate->setDescriptionKind(PDFTileRowDelegate::TextKind::Data);

    PDFAppPanelListView* view = new PDFAppPanelListView(card);
    view->setObjectName(QStringLiteral("linksView"));
    view->setFrameShape(QFrame::NoFrame);
    view->setItemDelegate(delegate);
    view->setModel(m_model);
    view->setSelectionMode(QAbstractItemView::SingleSelection);
    view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->viewport()->setAutoFillBackground(false);
    view->setSpacing(1);
    view->setContentPaddings(QMargins(-1, -1, -1, -1));
    view->setAccessibleName(tr("External links"));
    card->getBodyLayout()->addWidget(view);
    m_view = view;
    m_listCard = card;
    builder.addWidget(card);

    connect(view->selectionModel(), &QItemSelectionModel::currentChanged, this, &PDFAppToolGUIRemoveLinks::onCurrentRowChanged);
    connect(view->verticalScrollBar(), &QScrollBar::valueChanged, this, &PDFAppToolGUIRemoveLinks::updateMoreHint);
    view->viewport()->installEventFilter(this);
    m_viewport = view->viewport();

    // 438:15852: none in this document
    PDFAppToolCard* empty = new PDFAppToolCard(builder.getWidget());
    empty->setObjectName(QStringLiteral("linksEmptyCard"));
    empty->getBodyLayout()->setContentsMargins(17, 17, 17, 17);
    empty->getBodyLayout()->setSpacing(8);
    PDFIconBadge* icon = new PDFIconBadge(QStringLiteral("link-off"), 32, 16, empty);
    icon->setColors(PDFThemeColors::BgAccentSubtle, PDFThemeColors::IconAccent);
    icon->setRadius(8);
    empty->getBodyLayout()->addWidget(icon, 0, Qt::AlignHCenter);
    PDFLabel* emptyTitle = PDFAppPageWidgets::createLabel(tr("No external links"), QFont::DemiBold, 13, PDFThemeColors::TextPrimary, empty);
    empty->getBodyLayout()->addWidget(emptyTitle, 0, Qt::AlignHCenter);
    PDFAppParagraph* emptyText = PDFAppPageWidgets::createText(tr("This document has no links to web pages or e-mail addresses. Internal links are not touched."),
                                                               PDFThemeFonts::getFont(QFont::Normal, 11), 13, PDFThemeColors::TextSecondary, empty);
    emptyText->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    emptyText->setMinimumWidth(1);
    empty->getBodyLayout()->addWidget(emptyText);
    m_emptyCard = empty;
    builder.addWidget(empty);

    // 438:14938: the links below the shown rows (K16)
    m_moreHint = builder.addHint(QString());
    m_moreHint->setObjectName(QStringLiteral("linksMoreHint"));
    builder.addHint(tr("Ticked links are removed – untick a link to keep it. Click a row to show the link on the page."));

    updateParts();
}

void PDFAppToolGUIRemoveLinks::describeRightPanel(PDFAppToolPanelBuilder& builder)
{
    // 438:14785: After removing, Links, the callout
    builder.addPanelHeader(tr("After removing"), tr("Summary of the result"));
    builder.addSection(tr("Links"));
    m_summaryRows = builder.addKeyValues();
    builder.endSection();
    PDFAppToolCallout* callout = builder.addCallout(PDFAccentHue::Blue, QStringLiteral("info"), tr("Only the link is removed"),
                                                    tr("The text or image under a link stays on the page, it just stops being clickable. Links that jump inside the document keep working."));
    callout->setCompactStyle();
    updateParts();
}

QWidget* PDFAppToolGUIRemoveLinks::createOverlay(QWidget* canvasArea)
{
    const PDFAppToolHost* host = getHost();
    PDFWidget* pdfWidget = host ? host->getPdfWidget() : nullptr;
    if (!pdfWidget || !canvasArea)
    {
        return nullptr;
    }

    m_overlay = new PDFAppToolLinkOverlay(pdfWidget, canvasArea);
    if (PDFAppDocumentView* view = host->getView(); view && !m_isViewConnected)
    {
        connect(view, &PDFAppDocumentView::displayedPageChanged, this, &PDFAppToolGUIRemoveLinks::onDisplayedPageChanged);
        m_isViewConnected = true;
    }
    onDisplayedPageChanged();
    updateOverlay();
    return m_overlay.data();
}

void PDFAppToolGUIRemoveLinks::onDisplayedPageChanged()
{
    const PDFAppToolHost* host = getHost();
    const PDFAppDocumentView* view = host ? host->getView() : nullptr;
    if (m_overlay && view)
    {
        const PDFInteger page = view->getDisplayedPageIndex();
        m_overlay->setPageText(page >= 0 ? tr("Page %1").arg(QLocale().toString(qlonglong(page + 1))) : QString());
    }
}

void PDFAppToolGUIRemoveLinks::updateOverlay()
{
    if (m_overlay && m_model)
    {
        m_overlay->setLinks(&m_links, &m_model->getKeptIds(), m_selectedId);
    }
}

void PDFAppToolGUIRemoveLinks::updateParts()
{
    using Helper = PDFAppToolGUIProtectHelper;
    static constexpr const char* CONTEXT = "pdf::PDFAppToolGUIRemoveLinks";

    const int total = int(m_links.size());
    const int removed = getRemovedCount();
    const int kept = total - removed;
    const bool hasLinks = m_isListed && total > 0;

    // 438:15852: nothing to choose without links
    for (QToolButton* button : { m_selectAllButton.data(), m_selectNoneButton.data(), getShowButton() })
    {
        if (button)
        {
            button->setEnabled(hasLinks);
        }
    }

    if (QToolButton* button = getShowButton())
    {
        button->setText(tr("Show: %1").arg(getFilterTitle(m_filter)));
        button->setToolTip(button->text());
        button->setIcon(PDFThemeIcons::getIcon(getFilterIconName(m_filter)));
    }

    // 438:15032: the counts of the filters
    int webCount = 0;
    int mailCount = 0;
    for (const PDFDocumentLinkOperations::ExternalLink& link : m_links)
    {
        webCount += link.kind == PDFDocumentLinkOperations::LinkKind::Web ? 1 : 0;
        mailCount += link.kind == PDFDocumentLinkOperations::LinkKind::EMail ? 1 : 0;
    }
    const std::vector<std::pair<Filter, QString>> descriptions = {
        { Filter::All, Helper::plural(CONTEXT, total, QT_TR_NOOP("%Ln external link"), QT_TR_NOOP("%Ln external links")) },
        { Filter::Web, Helper::plural(CONTEXT, webCount, QT_TR_NOOP("%Ln link to http and https pages"), QT_TR_NOOP("%Ln links to http and https pages")) },
        { Filter::EMail, Helper::plural(CONTEXT, mailCount, QT_TR_NOOP("%Ln mailto link"), QT_TR_NOOP("%Ln mailto links")) },
    };
    for (const auto& [filter, description] : descriptions)
    {
        if (PDFTileRow* row = getFilterRow(filter))
        {
            row->setDescription(description);
            const QSignalBlocker blocker(row);
            row->setChecked(filter == m_filter);
        }
    }

    // header 438:14861: "16 found · 14 will be removed", "2 of 16 shown · e-mail addresses", "None in this document"
    if (m_leftHeader)
    {
        QString subtitle;
        if (m_isListed && total == 0)
        {
            subtitle = tr("None in this document");
        }
        else if (m_isListed && m_filter != Filter::All && m_model)
        {
            const QString shownKind = m_filter == Filter::Web ? tr("web addresses") : tr("e-mail addresses");
            subtitle = tr("%1 of %2 shown · %3").arg(int(m_model->getShownLinks().size())).arg(total).arg(shownKind);
        }
        else if (m_isListed)
        {
            subtitle = tr("%1 found · %2 will be removed").arg(QLocale().toString(total), QLocale().toString(removed));
        }
        m_leftHeader->setTexts(tr("External links"), subtitle);
    }

    if (m_listCard)
    {
        m_listCard->setVisible(!m_isListed || (total > 0 && m_model && m_model->rowCount() > 0));
    }
    if (m_emptyCard)
    {
        m_emptyCard->setVisible(m_isListed && total == 0);
    }

    // Links 438:15010: External links, Removed, Kept, Internal links
    const QString none = QStringLiteral("–");
    Helper::setRows(m_summaryRows, {
        { tr("External links"), m_isListed ? QLocale().toString(total) : none },
        { tr("Removed"), m_isListed ? QLocale().toString(removed) : none },
        { tr("Kept"), m_isListed ? QLocale().toString(kept) : none },
        { tr("Internal links"), m_isListed ? tr("%1 · not touched").arg(QLocale().toString(qlonglong(m_internalLinkCount))) : none },
    });

    updateListHeight();
    updateMoreHint();
    updateOverlay();
}

void PDFAppToolGUIRemoveLinks::updateListHeight()
{
    if (!m_view || !m_listCard || !m_model)
    {
        return;
    }

    // The card hugs its rows up to 455 (438:14865); the rows of a long list scroll
    const int maximumHeight = LIST_MAXIMUM_HEIGHT - 2 * LIST_PADDING;
    int height = 0;
    for (int row = 0, count = m_model->rowCount(); row < count && height < maximumHeight; ++row)
    {
        height += m_view->sizeHintForRow(row) + 2;
    }
    m_view->setFixedHeight(qBound(1, height, maximumHeight));
}

void PDFAppToolGUIRemoveLinks::updateMoreHint()
{
    using Helper = PDFAppToolGUIProtectHelper;

    if (!m_moreHint)
    {
        return;
    }

    // K16: "9 more links on pages 520–1,590 · scroll to see them" of the rows below the shown ones
    int count = 0;
    PDFInteger firstPage = -1;
    PDFInteger lastPage = -1;
    if (m_view && m_model && m_view->isVisibleTo(m_view->window()))
    {
        const QModelIndex lastShown = m_view->indexAt(QPoint(m_view->viewport()->width() / 2, m_view->viewport()->height() - 1));
        const int firstHidden = lastShown.isValid() ? lastShown.row() + 1 : m_model->rowCount();
        for (int row = firstHidden; row < m_model->rowCount(); ++row)
        {
            if (const PDFDocumentLinkOperations::ExternalLink* link = m_model->getLink(row))
            {
                ++count;
                firstPage = firstPage < 0 ? link->pageIndex : qMin(firstPage, link->pageIndex);
                lastPage = qMax(lastPage, link->pageIndex);
            }
        }
    }

    if (count == 0)
    {
        m_moreHint->setVisible(false);
        return;
    }

    const QLocale locale;
    const QString links = Helper::plural("pdf::PDFAppToolGUIRemoveLinks", count, QT_TR_NOOP("%Ln more link"), QT_TR_NOOP("%Ln more links"));
    const QString pages = firstPage == lastPage ? tr("on page %1").arg(locale.toString(qlonglong(firstPage + 1)))
                                                : tr("on pages %1–%2").arg(locale.toString(qlonglong(firstPage + 1)), locale.toString(qlonglong(lastPage + 1)));
    m_moreHint->setText(tr("%1 %2 · scroll to see them").arg(links, pages));
    m_moreHint->setVisible(true);
}

bool PDFAppToolGUIRemoveLinks::eventFilter(QObject* watched, QEvent* event)
{
    // The address of the viewport only: the view may be in its destructor (no call through it)
    if ((event->type() == QEvent::Resize || event->type() == QEvent::Show) && watched && watched == m_viewport.data() && m_view)
    {
        // A new width wraps the descriptions again: the height of the card follows
        updateListHeight();
        updateMoreHint();
    }
    return BaseClass::eventFilter(watched, event);
}

std::set<QString> PDFAppToolGUIRemoveLinks::readKeptIds() const
{
    std::set<QString> ids;
    const PDFAppToolParameterModel* model = getModel();
    if (model)
    {
        PDFDocumentLinkOperations::parseIds(model->getValue(QString::fromLatin1(PDFDocumentToolRemoveLinks::PARAMETER_EXCLUDE)).toString(), &ids, nullptr);
    }
    return ids;
}

void PDFAppToolGUIRemoveLinks::writeKeptIds(std::set<QString> ids)
{
    PDFAppToolParameterModel* model = getModel();
    if (!model)
    {
        return;
    }

    // One undo step; a value, which the model refuses (too long), is said in the status bar
    const QString error = model->setValue(QString::fromLatin1(PDFDocumentToolRemoveLinks::PARAMETER_EXCLUDE), PDFDocumentToolRemoveLinks::createExcludeText(ids));
    if (!error.isEmpty())
    {
        if (PDFAppToolHost* host = getHost())
        {
            host->showTransientMessage(error);
        }
    }
}

void PDFAppToolGUIRemoveLinks::selectAll()
{
    // The shown links are removed; the links, which the filter hides, keep their state
    std::set<QString> ids = readKeptIds();
    if (m_model)
    {
        for (const PDFDocumentLinkOperations::ExternalLink* link : m_model->getShownLinks())
        {
            ids.erase(link->id);
        }
    }
    writeKeptIds(std::move(ids));
}

void PDFAppToolGUIRemoveLinks::selectNone()
{
    std::set<QString> ids = readKeptIds();
    if (m_model)
    {
        for (const PDFDocumentLinkOperations::ExternalLink* link : m_model->getShownLinks())
        {
            ids.insert(link->id);
        }
    }
    writeKeptIds(std::move(ids));
}

void PDFAppToolGUIRemoveLinks::setFilter(Filter filter)
{
    m_filter = filter;
    if (m_model)
    {
        m_model->setFilter(filter);
    }

    // The selected link stays selected, when it is shown
    if (m_view && m_model)
    {
        const int row = m_model->getRow(m_selectedId);
        if (row >= 0)
        {
            const QSignalBlocker blocker(m_view->selectionModel());
            m_view->setCurrentIndex(m_model->index(row));
        }
    }
    updateParts();
    updateStatusText();
}

void PDFAppToolGUIRemoveLinks::onFilterRowClicked()
{
    PDFTileRow* row = qobject_cast<PDFTileRow*>(sender());
    if (!row)
    {
        return;
    }

    const Filter filter = static_cast<Filter>(row->property(PDFAppToolGUIProtectHelper::FILTER_PROPERTY).toInt());
    QPointer<PDFAppToolGUIRemoveLinks> guard(this);
    if (m_showPanel)
    {
        m_showPanel->close();
    }
    if (guard)
    {
        setFilter(filter);
    }
}

void PDFAppToolGUIRemoveLinks::onLinkChecked(const QString& id, bool isChecked)
{
    std::set<QString> ids = readKeptIds();
    if (isChecked)
    {
        ids.erase(id);
    }
    else
    {
        ids.insert(id);
    }
    writeKeptIds(std::move(ids));
}

void PDFAppToolGUIRemoveLinks::onCurrentRowChanged(const QModelIndex& current)
{
    const PDFDocumentLinkOperations::ExternalLink* link = m_model ? m_model->getLink(current.row()) : nullptr;
    if (!link)
    {
        return;
    }

    // 438:14751: the row shows its link on the page
    m_selectedId = link->id;
    const PDFInteger pageIndex = link->pageIndex;
    updateOverlay();

    QPointer<PDFAppToolGUIRemoveLinks> guard(this);
    const PDFAppToolHost* host = getHost();
    if (PDFAppDocumentView* view = host ? host->getView() : nullptr)
    {
        view->goToPage(pageIndex);
    }
    if (guard)
    {
        onDisplayedPageChanged();
    }
}

QVariantMap PDFAppToolGUIRemoveLinks::getPreviewParameters() const
{
    // The links do not depend on the kept ones: one preview per version of the document
    return QVariantMap();
}

QString PDFAppToolGUIRemoveLinks::getConfirmText() const
{
    // 438:14776 "Remove 14 Links", 438:15578 "Remove 0 Links", 438:15852 "Remove Links"
    if (!m_isListed || m_links.empty())
    {
        return tr("Remove Links");
    }
    return PDFAppToolGUIProtectHelper::plural("pdf::PDFAppToolGUIRemoveLinks", getRemovedCount(), QT_TR_NOOP("Remove %Ln Link"), QT_TR_NOOP("Remove %Ln Links"));
}

bool PDFAppToolGUIRemoveLinks::isConfirmEnabled() const
{
    return BaseClass::isConfirmEnabled() && m_isListed && getRemovedCount() > 0;
}

QString PDFAppToolGUIRemoveLinks::getStatusText() const
{
    using Helper = PDFAppToolGUIProtectHelper;
    static constexpr const char* CONTEXT = "pdf::PDFAppToolGUIRemoveLinks";

    if (!m_isListed)
    {
        return BaseClass::getStatusText();
    }
    if (m_links.empty())
    {
        return tr("No external link annotations found");
    }

    const int removed = getRemovedCount();
    if (removed == 0)
    {
        return tr("Select at least one link to remove");
    }

    // 438:14786: "16 external links found · 14 will be removed, 2 are kept"
    const int kept = getKeptCount();
    return tr("%1 found · %2, %3").arg(Helper::plural(CONTEXT, int(m_links.size()), QT_TR_NOOP("%Ln external link"), QT_TR_NOOP("%Ln external links")),
                                       Helper::plural(CONTEXT, removed, QT_TR_NOOP("%Ln will be removed"), QT_TR_NOOP("%Ln will be removed")),
                                       Helper::plural(CONTEXT, kept, QT_TR_NOOP("%Ln is kept"), QT_TR_NOOP("%Ln are kept")));
}

void PDFAppToolGUIRemoveLinks::fillResultInput(PDFAppToolResultInput* input) const
{
    // 438:16073: "14 external links removed"
    const int removed = input->result.data.value(QStringLiteral("removed")).toInt();
    input->title = PDFAppToolGUIProtectHelper::plural("pdf::PDFAppToolGUIRemoveLinks", removed, QT_TR_NOOP("%Ln external link removed"), QT_TR_NOOP("%Ln external links removed"));
    input->statusText = tr("External links removed – choose what to do with the result");
}

void PDFAppToolGUIRemoveLinks::onParametersChanged(const QString& name)
{
    if (name == QLatin1String(PDFDocumentToolRemoveLinks::PARAMETER_EXCLUDE) && m_model)
    {
        m_model->setKeptIds(readKeptIds());
    }
    updateParts();
}

void PDFAppToolGUIRemoveLinks::onPreviewChanged(const PDFDocumentToolResult* preview)
{
    if (preview && preview->isSuccess())
    {
        m_links = PDFDocumentToolRemoveLinks::getPreviewLinks(*preview);
        m_internalLinkCount = preview->data.value(QStringLiteral("internalLinks")).toInteger();
        m_isListed = true;
    }
    else
    {
        // A new version of the document is listed again
        m_links.clear();
        m_internalLinkCount = 0;
        m_isListed = false;
    }

    // A kept link, which the new version has not, is no value of the parameter (no undo step)
    std::set<QString> keptIds = readKeptIds();
    std::set<QString> validIds;
    for (const PDFDocumentLinkOperations::ExternalLink& link : m_links)
    {
        if (keptIds.count(link.id))
        {
            validIds.insert(link.id);
        }
    }

    QPointer<PDFAppToolGUIRemoveLinks> guard(this);
    if (m_model)
    {
        // The cached preview comes again after every change of the kept links, also from inside
        // of the write of a check box of the list: the same links never reset the rows
        if (!PDFAppToolGUIProtectHelper::isSameLinks(m_model->getLinks(), m_links))
        {
            m_model->setLinks(m_links);
        }
        m_model->setKeptIds(validIds);
    }
    if (m_isListed && validIds != keptIds)
    {
        if (PDFAppToolParameterModel* model = getModel())
        {
            model->setFollowingValue(QString::fromLatin1(PDFDocumentToolRemoveLinks::PARAMETER_EXCLUDE), PDFDocumentToolRemoveLinks::createExcludeText(validIds));
        }
    }
    if (!guard)
    {
        return;
    }

    if (m_model && m_model->getRow(m_selectedId) < 0)
    {
        m_selectedId.clear();
    }

    updateParts();
    updateConfirmState();
    if (guard)
    {
        updateStatusText();
    }
}

}   // namespace pdf

#include "moc_pdfapptoolgui_protect.cpp"
