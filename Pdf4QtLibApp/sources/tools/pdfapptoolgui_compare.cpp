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

#include "pdfapptoolgui_compare.h"

#include "pdfappcomparedifferencemodel.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentpanels_common.h"
#include "pdfappdocumentregistry.h"
#include "pdfappdocumentsearchindex.h"
#include "pdfappfiledrop.h"
#include "pdfapplocale.h"
#include "pdfappmainwindow.h"
#include "pdfapppagelayout.h"
#include "pdfapppassworddialog.h"
#include "pdfapptoolhost.h"
#include "pdfapptoolinputlist.h"
#include "pdfapptoolpanelbuilder.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptoolresultdialog.h"
#include "pdfapptooltab.h"
#include "pdfapptooltoolbar.h"
#include "pdfappworkspace.h"
#include "pdfdocumenttoolguiregistry.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfdocumenttoolshell.h"
#include "pdftoolparameterrepository.h"

#include "pdfcolorpickerdialog.h"
#include "pdfcomponentbadges.h"
#include "pdfcomponentbuttons.h"
#include "pdfcomponentdelegates.h"
#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdfdocumentmerger.h"
#include "pdfdocumentwriter.h"
#include "pdfdrawwidget.h"
#include "pdffluentwidgets.h"
#include "pdfstylepainter.h"
#include "pdfthemefonts.h"
#include "pdfthemeiconengine.h"

#include <QAction>
#include <QBuffer>
#include <QDropEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QStackedWidget>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <array>
#include <limits>

namespace pdf
{

/// Helpers of the GUI of Compare
class PDFAppToolGUICompareHelper
{
public:
    PDFAppToolGUICompareHelper() = delete;

    /// Widths of the panels of the toolbar (492:19823 … 492:22010)
    static constexpr int VIEW_PANEL_WIDTH = 320;
    static constexpr int FILTER_PANEL_WIDTH = 310;
    static constexpr int SHOW_PANEL_WIDTH = 330;
    static constexpr int OPTIONS_PANEL_WIDTH = 360;
    static constexpr int EXPORT_PANEL_WIDTH = 340;
    static constexpr int CHANGE_PANEL_WIDTH = 290;

    /// A count for %Ln (at most the largest int)
    static int toCount(size_t value)
    {
        return int(qMin(value, size_t((std::numeric_limits<int>::max)())));
    }

    /// The pairs of pages with a difference (the groups of the list, 492:19314 "on 6 pages")
    static size_t getPagePairCount(const PDFDiffResult& result)
    {
        std::vector<std::pair<PDFInteger, PDFInteger>> pairs = PDFAppCompareDifferenceModel::getPagePairs(result);
        std::sort(pairs.begin(), pairs.end());
        pairs.erase(std::unique(pairs.begin(), pairs.end()), pairs.end());
        return pairs.size();
    }

    /// The combo Page sizes (combo-page-sizes 493:19641) and its list (panel-page-sizes 493:19892)
    static constexpr int PAGE_SIZES_FIELD_WIDTH = 170;
    static constexpr int PAGE_SIZES_LIST_WIDTH = 320;

    /// The spin boxes of the scales and the shifts of the overlay (493:19647 ... 493:19677)
    static constexpr int OVERLAY_SPIN_BOX_WIDTH = 110;

    /// Width of the field of a page range (pages-Original 492:20095)
    static constexpr int RANGE_FIELD_WIDTH = 156;

    /// Key of a row of a menu
    static constexpr const char* ROW_KEY = "pdf4qtCompareRow";

    /// Ids of the menus
    static constexpr const char* MENU_VIEW = "view";
    static constexpr const char* MENU_FILTER = "filter";
    static constexpr const char* MENU_SHOW = "show";
    static constexpr const char* MENU_OPTIONS = "options";
    static constexpr const char* MENU_EXPORT = "export";

    /// Ids of the rows of Export
    static constexpr const char* EXPORT_REPORT = "exportReport";
    static constexpr const char* EXPORT_XML = "exportXml";

    static QString name(const char* text)
    {
        return QString::fromLatin1(text);
    }

    static int toInt(PDFInteger value)
    {
        return int(qBound(PDFInteger(0), value, PDFInteger(std::numeric_limits<int>::max())));
    }

    /// The range of all pages ("1-24", "1"; ASCII digits - the syntax of the range, the old Diff)
    static QString getAllPagesText(PDFInteger pageCount)
    {
        return pageCount <= 1 ? QStringLiteral("1") : QStringLiteral("1-%1").arg(pageCount);
    }

    /// The count of the pages for PDFPageRange (bounded)
    static int getRangePageCount(PDFInteger pageCount)
    {
        return int(qBound(PDFInteger(0), pageCount, PDFInteger(PDFPageRange::MAX_PAGE_COUNT)));
    }

    /// The id of the parameter view of a view
    static QString getViewId(PDFDiffReportView view)
    {
        switch (view)
        {
            case PDFDiffReportView::SideBySide:
                return name(PDFDocumentToolCompare::VIEW_SIDE_BY_SIDE);
            case PDFDiffReportView::Original:
                return name(PDFDocumentToolCompare::VIEW_ORIGINAL);
            case PDFDiffReportView::Revised:
                return name(PDFDocumentToolCompare::VIEW_REVISED);
            case PDFDiffReportView::Overlay:
                return name(PDFDocumentToolCompare::VIEW_OVERLAY);
        }
        return name(PDFDocumentToolCompare::VIEW_SIDE_BY_SIDE);
    }

    static QString getViewIcon(PDFDiffReportView view)
    {
        switch (view)
        {
            case PDFDiffReportView::SideBySide:
                return QStringLiteral("diff-side");
            case PDFDiffReportView::Original:
                return QStringLiteral("diff-original");
            case PDFDiffReportView::Revised:
                return QStringLiteral("diff-revised");
            case PDFDiffReportView::Overlay:
                return QStringLiteral("diff-overlay");
        }
        return QStringLiteral("diff-side");
    }

    static QString getViewTitle(PDFDiffReportView view)
    {
        // 492:20335
        switch (view)
        {
            case PDFDiffReportView::SideBySide:
                return PDFAppToolGUICompare::tr("Side by Side");
            case PDFDiffReportView::Original:
                return PDFAppToolGUICompare::tr("Original Only");
            case PDFDiffReportView::Revised:
                return PDFAppToolGUICompare::tr("Revised Only");
            case PDFDiffReportView::Overlay:
                return PDFAppToolGUICompare::tr("Overlay");
        }
        return QString();
    }

    static QString getViewDescription(PDFDiffReportView view)
    {
        switch (view)
        {
            case PDFDiffReportView::SideBySide:
                return PDFAppToolGUICompare::tr("Original on the left, Revised on the right");
            case PDFDiffReportView::Original:
                return PDFAppToolGUICompare::tr("Only the pages of the Original");
            case PDFDiffReportView::Revised:
                return PDFAppToolGUICompare::tr("Only the pages of the Revised");
            case PDFDiffReportView::Overlay:
                return PDFAppToolGUICompare::tr("Paired pages on top of each other, blended");
        }
        return QString();
    }

    /// The document of a view: Overlay shows the pairs as Side by Side (PDFDiffReportBuilder::createCombinedDocument)
    static PDFDiffReportView getDocumentView(PDFDiffReportView view)
    {
        return view == PDFDiffReportView::Overlay ? PDFDiffReportView::SideBySide : view;
    }

    /// The job of the document of the view: copies only (immutable snapshots, a copy of the result)
    static PDFDiffCombinedDocument buildViewDocument(PDFDiffReportView view, PDFDocumentPointer original, PDFDocumentPointer revised, PDFDiffResult result, std::shared_ptr<PDFAppCancelFlag> cancel)
    {
        return PDFDiffReportBuilder::createCombinedDocument(view, original, revised, result, cancel.get());
    }

    /// The job of the report: the document of the core written into bytes - one output of the dialog of the export
    static PDFDocumentToolResult buildReport(PDFDocumentToolInputDocument original, PDFDocumentToolInputDocument revised, PDFDiffResult result, PDFDiffReportSettings settings, std::shared_ptr<PDFAppCancelFlag> cancel, QString fileName)
    {
        QString errorMessage;
        std::optional<PDFDocument> report = PDFDiffReportBuilder::createReport(original, revised, result, settings, cancel.get(), &errorMessage);
        if (cancel->isOperationCanceled())
        {
            return PDFDocumentToolResult::createStopped();
        }
        if (!report)
        {
            return PDFDocumentToolResult::createFailed(errorMessage.isEmpty() ? PDFAppToolGUICompare::tr("The report cannot be created.") : errorMessage);
        }

        QByteArray data;
        QBuffer buffer(&data);
        buffer.open(QIODevice::WriteOnly);
        PDFDocumentWriter writer(nullptr);
        const PDFOperationResult writeResult = writer.write(&buffer, &report.value());
        buffer.close();
        if (!writeResult)
        {
            return PDFDocumentToolResult::createFailed(writeResult.getErrorMessage());
        }

        PDFDocumentToolResult exportResult;
        PDFDocumentToolOutputFile output;
        output.suggestedFileName = fileName;
        output.data = std::move(data);
        output.mimeType = QStringLiteral("application/pdf");
        exportResult.outputs.push_back(std::move(output));
        return exportResult;
    }

    /// The name of a document without its suffix (the suggested names of the exports)
    static QString getBaseName(const QString& name)
    {
        const QString baseName = QFileInfo(name).completeBaseName();
        return baseName.isEmpty() ? PDFAppToolGUICompare::tr("Document") : baseName;
    }

    /// The document permits the extraction of its texts (bit 5, Table 22) or its owner password was verified
    static bool isCopyPermitted(const PDFDocumentToolInputDocument& input)
    {
        const PDFSecurityHandler* handler = input.document ? input.document->getStorage().getSecurityHandler() : nullptr;
        return input.isOwnerPasswordVerified || !handler || handler->isAllowed(PDFSecurityHandler::Permission::CopyContent);
    }
};

// ------------------------------------------------------------- PDFAppCompareTool

PDFAppCompareTool::PDFAppCompareTool(std::shared_ptr<PDFDiffResultCollector> collector) :
    m_collector(std::move(collector))
{

}

PDFDocumentToolResult PDFAppCompareTool::execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    return compare(context, parameters, control, progress, m_collector.get());
}

// ------------------------------------------------------------- PDFAppCompareColorRow

PDFAppCompareColorRow::PDFAppCompareColorRow(const QString& name, const QColor& defaultColor, int defaultSlot, QWidget* parent) :
    BaseClass(parent)
{
    // color-removed 492:20127: the name, the swatches 4 apart and the "+"
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    PDFLabel* label = PDFAppPageWidgets::createLabel(name, QFont::Normal, 12, PDFThemeColors::TextSecondary, this);
    layout->addWidget(label, 1);

    const std::array<QColor, 5> colors = getSwatchColors(defaultColor, defaultSlot);
    const std::array<QString, 5> names = { tr("Red"), tr("Orange"), tr("Green"), tr("Blue"), tr("Black") };
    for (size_t i = 0; i < colors.size(); ++i)
    {
        const QString swatchName = int(i) == defaultSlot ? tr("Default color of %1").arg(name) : names[i];
        PDFColorSwatch* swatch = new PDFColorSwatch(colors[i], swatchName, this);
        swatch->setCheckable(true);
        swatch->setToolTip(swatchName);
        connect(swatch, &QAbstractButton::clicked, this, &PDFAppCompareColorRow::onSwatchClicked);
        layout->addWidget(swatch);
        m_swatches[i] = swatch;
    }

    PDFIconButton* custom = new PDFIconButton(QStringLiteral("plus"), PDFIconButton::Size::Medium, this);
    custom->setToolTip(tr("Custom color…"));
    custom->setAccessibleName(tr("Custom color of %1…").arg(name));
    connect(custom, &QAbstractButton::clicked, this, &PDFAppCompareColorRow::onCustomClicked);
    layout->addWidget(custom);
    m_custom = custom;
    setAccessibleName(name);
}

PDFAppCompareColorRow::~PDFAppCompareColorRow()
{
    // An open dialog of a custom color goes with the row (it is a window of its own)
    if (m_dialog)
    {
        m_dialog->disconnect(this);
        m_dialog->deleteLater();
    }
}

std::array<QColor, 5> PDFAppCompareColorRow::getSwatchColors(const QColor& defaultColor, int defaultSlot)
{
    // 492:20126: the annotation colors red, orange, green, blue and black (their canonical values, the built-in light
    // theme), the default color of the kind in the place of its hue
    PDFThemeColors themeColors;
    std::array<QColor, 5> colors = { themeColors.getColor(PDFThemeColors::AnnotationRed), themeColors.getColor(PDFThemeColors::AnnotationOrange),
                                     themeColors.getColor(PDFThemeColors::AnnotationGreen), themeColors.getColor(PDFThemeColors::AnnotationBlue),
                                     themeColors.getColor(PDFThemeColors::AnnotationBlack) };
    if (defaultSlot >= 0 && defaultSlot < int(colors.size()))
    {
        colors[size_t(defaultSlot)] = defaultColor;
    }
    return colors;
}

void PDFAppCompareColorRow::setColor(const QColor& color)
{
    m_color = color;
    for (const QPointer<PDFColorSwatch>& swatch : m_swatches)
    {
        if (swatch)
        {
            const QSignalBlocker blocker(swatch.data());
            swatch->setSelected(swatch->getColor().rgb() == color.rgb());
        }
    }
}

PDFColorSwatch* PDFAppCompareColorRow::getSwatch(int index) const
{
    return index >= 0 && index < int(m_swatches.size()) ? m_swatches[size_t(index)].data() : nullptr;
}

QAbstractButton* PDFAppCompareColorRow::getCustomButton() const
{
    return m_custom.data();
}

void PDFAppCompareColorRow::onSwatchClicked()
{
    PDFColorSwatch* swatch = qobject_cast<PDFColorSwatch*>(sender());
    if (!swatch)
    {
        return;
    }

    // The state first, the signal last (its slot may rebuild the panel)
    setColor(swatch->getColor());
    Q_EMIT colorChosen(m_color);
}

void PDFAppCompareColorRow::onCustomClicked()
{
    if (m_dialog)
    {
        m_dialog->raise();
        return;
    }

    // A popup is new + QPointer, never on the stack, and never exec (CLAUDE.md)
    PDFColorPickerDialog* dialog = new PDFColorPickerDialog(m_color, window());
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &PDFColorPickerDialog::colorSelected, this, &PDFAppCompareColorRow::onDialogColorSelected);
    m_dialog = dialog;
    dialog->open();
}

void PDFAppCompareColorRow::onDialogColorSelected(const QColor& color)
{
    if (!color.isValid())
    {
        return;
    }
    setColor(color);
    Q_EMIT colorChosen(m_color);
}

// ------------------------------------------------------------- PDFAppCompareSlotCard

/// The dashed place of a drop (drop-zone 492:18158): bg/subtle, border/strong dashed, radius 8
class PDFAppCompareDropArea : public QWidget
{
public:
    explicit PDFAppCompareDropArea(QWidget* parent) :
        QWidget(parent)
    {

    }

protected:
    virtual void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF shape = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        PDFStylePainter::fill(&painter, shape, PDFStylePainter::Radii(8), PDFThemeColors::BgSubtle);
        QPen pen(PDFStylePainter::color(PDFThemeColors::BorderStrong), 1.0);
        pen.setDashPattern({ 4.0, 3.0 });
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(PDFStylePainter::roundedPath(shape, PDFStylePainter::Radii(8)));
    }
};

PDFAppCompareSlotCard::PDFAppCompareSlotCard(const QString& title, const QString& subtitle, QWidget* parent) :
    BaseClass(parent)
{
    QVBoxLayout* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    PDFAppCard* card = PDFAppPageWidgets::createCard(14, 10, this);
    outer->addWidget(card);
    outer->addStretch(1);
    QVBoxLayout* layout = card->getLayout();

    // 492:18158: "Original" Semi Bold 14, "The older version" Regular 11 text/tertiary
    QVBoxLayout* head = new QVBoxLayout();
    head->setSpacing(1);
    m_title = PDFAppPageWidgets::createLabel(title, QFont::DemiBold, 14, PDFThemeColors::TextPrimary, card);
    head->addWidget(m_title);
    m_subtitle = PDFAppPageWidgets::createLabel(subtitle, QFont::Normal, 11, PDFThemeColors::TextTertiary, card);
    head->addWidget(m_subtitle);
    layout->addLayout(head);

    // The chosen document with Change… (492:18382: a text with the fill bg/accent-subtle of a chosen row)
    m_inputRow = new PDFTileRow(QString(), QString(), QStringLiteral("file"), PDFTileRow::Kind::ListItem, card);
    m_inputRow->setObjectName(QStringLiteral("compareSlotInput"));
    m_inputRow->setTitleKind(PDFTileRow::TextKind::Data);
    m_inputRow->setLinkText(tr("Change…"));
    m_inputRow->setClickable(false);
    m_inputRow->setStaticChosen(true);
    connect(m_inputRow.data(), &PDFTileRow::linkClicked, this, &PDFAppCompareSlotCard::changeRequested);
    layout->addWidget(m_inputRow);

    // 492:106266: an input, which cannot be read
    m_error = new PDFAppToolCallout(PDFAccentHue::Red, QStringLiteral("alert-circle"), QString(), QString(), card);
    m_error->setObjectName(QStringLiteral("compareSlotError"));
    m_error->setCompactStyle();
    layout->addWidget(m_error);

    // An encrypted input: Enter Password… (the dialog of step 2 opens at once, 492:106538)
    m_passwordRow = new QWidget(card);
    QHBoxLayout* passwordLayout = new QHBoxLayout(m_passwordRow);
    passwordLayout->setContentsMargins(0, 0, 0, 0);
    m_passwordLabel = PDFAppPageWidgets::createLabel(QString(), QFont::Normal, 12, PDFThemeColors::TextSecondary, m_passwordRow);
    m_passwordLabel->setWordWrap(true);
    passwordLayout->addWidget(m_passwordLabel, 1);
    m_passwordButton = PDFAppPageWidgets::createButton(tr("Enter Password…"), QStringLiteral("key"), false, m_passwordRow);
    m_passwordButton->setObjectName(QStringLiteral("compareSlotPassword"));
    connect(m_passwordButton.data(), &QPushButton::clicked, this, &PDFAppCompareSlotCard::passwordRequested);
    passwordLayout->addWidget(m_passwordButton);
    layout->addWidget(m_passwordRow);

    // drop-zone: the icon, "Drop a PDF here", Choose File…
    m_dropArea = new PDFAppCompareDropArea(card);
    m_dropArea->setObjectName(QStringLiteral("compareSlotDrop"));
    QVBoxLayout* dropLayout = new QVBoxLayout(m_dropArea);
    dropLayout->setContentsMargins(16, 20, 16, 20);
    dropLayout->setSpacing(8);
    PDFIconBadge* dropIcon = new PDFIconBadge(QStringLiteral("file-plus"), 24, 20, m_dropArea);
    dropIcon->setColors(std::nullopt, PDFThemeColors::IconDefault);
    dropLayout->addWidget(dropIcon, 0, Qt::AlignHCenter);
    m_dropText = PDFAppPageWidgets::createLabel(tr("Drop a PDF here"), QFont::Normal, 12, PDFThemeColors::TextSecondary, m_dropArea);
    m_dropText->setAlignment(Qt::AlignCenter);
    dropLayout->addWidget(m_dropText);
    m_chooseButton = PDFAppPageWidgets::createButton(tr("Choose File…"), QStringLiteral("folder-open"), false, m_dropArea);
    m_chooseButton->setObjectName(QStringLiteral("compareSlotChoose"));
    connect(m_chooseButton.data(), &QPushButton::clicked, this, &PDFAppCompareSlotCard::chooseFileRequested);
    dropLayout->addWidget(m_chooseButton, 0, Qt::AlignHCenter);
    layout->addWidget(m_dropArea);

    // OPEN TABS (Medium 11 text/tertiary)
    m_documentsCaption = PDFAppPageWidgets::createLabel(tr("OPEN TABS"), QFont::Medium, 11, PDFThemeColors::TextTertiary, card);
    layout->addWidget(m_documentsCaption);
    m_documentsLayout = new QVBoxLayout();
    m_documentsLayout->setSpacing(2);
    layout->addLayout(m_documentsLayout);
}

PDFAppCompareSlotCard::~PDFAppCompareSlotCard() = default;

void PDFAppCompareSlotCard::retranslate(const QString& title, const QString& subtitle)
{
    // The fixed texts of the card; the content (the input, the open tabs) is set again by its owner
    m_title->setText(title);
    m_subtitle->setText(subtitle);
    m_inputRow->setLinkText(tr("Change…"));
    m_passwordButton->setText(tr("Enter Password…"));
    m_dropText->setText(tr("Drop a PDF here"));
    m_chooseButton->setText(tr("Choose File…"));
    m_documentsCaption->setText(tr("OPEN TABS"));
}

std::vector<PDFTileRow*> PDFAppCompareSlotCard::getDocumentRows() const
{
    std::vector<PDFTileRow*> rows;
    for (const QPointer<PDFTileRow>& row : m_documentRows)
    {
        if (row)
        {
            rows.push_back(row.data());
        }
    }
    return rows;
}

void PDFAppCompareSlotCard::setContent(const Content& content)
{
    const bool hasInput = !content.inputName.isEmpty();
    m_inputRow->setTitle(content.inputName);
    m_inputRow->setDescription(content.inputDetails);
    m_error->setTexts(content.errorTitle, content.errorText);
    m_passwordLabel->setText(content.passwordText);

    // The rows of the open tabs are made again; the old ones go later (a row may be the sender)
    for (const QPointer<PDFTileRow>& row : m_documentRows)
    {
        if (row)
        {
            row->hide();
            row->deleteLater();
        }
    }
    m_documentRows.clear();
    if (!hasInput)
    {
        for (const OpenDocument& document : content.openDocuments)
        {
            PDFTileRow* row = new PDFTileRow(document.name, document.details, QStringLiteral("folder"), PDFTileRow::Kind::ListItem, m_documentsCaption->parentWidget());
            row->setObjectName(QStringLiteral("compareSlotTab"));
            row->setTitleKind(PDFTileRow::TextKind::Data);
            row->setProperty(PDFAppToolGUICompareHelper::ROW_KEY, document.documentId);
            if (document.isWarning)
            {
                row->setDescriptionColor(PDFThemeColors::TextDanger);
            }
            row->setEnabled(!document.isDisabled);
            connect(row, &PDFTileRow::clicked, this, &PDFAppCompareSlotCard::onDocumentRowClicked);
            m_documentsLayout->addWidget(row);
            m_documentRows.push_back(row);
        }
    }

    // The visibility last
    m_inputRow->setVisible(hasInput);
    m_error->setVisible(!content.errorTitle.isEmpty());
    m_passwordRow->setVisible(!content.passwordText.isEmpty());
    m_dropArea->setVisible(!hasInput);
    m_documentsCaption->setVisible(!hasInput && !content.openDocuments.empty());
}

void PDFAppCompareSlotCard::onDocumentRowClicked()
{
    const QObject* row = sender();
    if (row)
    {
        Q_EMIT documentChosen(row->property(PDFAppToolGUICompareHelper::ROW_KEY).toString());
    }
}

// ------------------------------------------------------------- PDFAppCompareCanvas

PDFAppCompareCanvas::PDFAppCompareCanvas(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("compareCanvas"));
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_stack = new QStackedWidget(this);
    layout->addWidget(m_stack);

    // 492:18158: "Choose the two documents to compare" Semi Bold 18, the hint, the two cards side by side
    m_choicePage = new QWidget(m_stack);
    m_choicePage->setObjectName(QStringLiteral("compareChoice"));
    QVBoxLayout* choiceLayout = new QVBoxLayout(m_choicePage);
    choiceLayout->setContentsMargins(PDFAppCompareView::PADDING, PDFAppCompareView::PADDING, PDFAppCompareView::PADDING, PDFAppCompareView::PADDING);
    choiceLayout->setSpacing(2);
    m_choiceTitle = PDFAppPageWidgets::createLabel(QString(), QFont::DemiBold, 18, PDFThemeColors::TextPrimary, m_choicePage);
    m_choiceTitle->setObjectName(QStringLiteral("compareChoiceTitle"));
    choiceLayout->addWidget(m_choiceTitle);
    m_choiceHint = PDFAppPageWidgets::createLabel(QString(), QFont::Normal, 12, PDFThemeColors::TextSecondary, m_choicePage);
    choiceLayout->addWidget(m_choiceHint);
    choiceLayout->addSpacing(14);
    QHBoxLayout* cards = new QHBoxLayout();
    cards->setSpacing(16);
    m_slotCards[0] = new PDFAppCompareSlotCard(QString(), QString(), m_choicePage);
    m_slotCards[0]->setObjectName(QStringLiteral("compareSlotCard0"));
    m_slotCards[1] = new PDFAppCompareSlotCard(QString(), QString(), m_choicePage);
    m_slotCards[1]->setObjectName(QStringLiteral("compareSlotCard1"));
    cards->addWidget(m_slotCards[0], 1);
    cards->addWidget(m_slotCards[1], 1);
    choiceLayout->addLayout(cards, 1);
    m_stack->addWidget(m_choicePage);

    m_view = new PDFAppCompareView(m_stack);
    m_stack->addWidget(m_view);
    m_stack->setCurrentWidget(m_choicePage);
    retranslate();
}

PDFAppCompareCanvas::~PDFAppCompareCanvas() = default;

void PDFAppCompareCanvas::retranslate()
{
    m_choiceTitle->setText(PDFAppToolGUICompare::tr("Choose the two documents to compare"));
    m_choiceHint->setText(PDFAppToolGUICompare::tr("Pick a file, drop it here or use a document that is already open in a tab."));
    m_slotCards[0]->retranslate(PDFAppToolGUICompare::tr("Original"), PDFAppToolGUICompare::tr("The older version"));
    m_slotCards[1]->retranslate(PDFAppToolGUICompare::tr("Revised"), PDFAppToolGUICompare::tr("The newer version"));
}

void PDFAppCompareCanvas::showChoice()
{
    m_stack->setCurrentWidget(m_choicePage);
}

void PDFAppCompareCanvas::showView()
{
    m_stack->setCurrentWidget(m_view);
}

bool PDFAppCompareCanvas::isChoiceShown() const
{
    return m_stack->currentWidget() == m_choicePage;
}

PDFAppCompareSlotCard* PDFAppCompareCanvas::getSlotCard(int slot) const
{
    return slot >= 0 && slot < int(m_slotCards.size()) ? m_slotCards[size_t(slot)].data() : nullptr;
}

void PDFAppCompareCanvas::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    // document-canvas: bg/track
    QPainter painter(this);
    PDFStylePainter::fillRect(&painter, QRectF(rect()), PDFThemeColors::BgTrack);
}

// ------------------------------------------------------------- PDFAppToolGUICompare

PDFAppToolGUICompare::PDFAppToolGUICompare(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppToolGUICompare::~PDFAppToolGUICompare()
{
    // The jobs end by themselves on their copies; their results go with the watchers (children)
    if (m_viewCancel)
    {
        m_viewCancel->cancel();
    }
    if (m_reportCancel)
    {
        m_reportCancel->cancel();
    }

    // The panel of Change… is a popup of the window; it may outlive this GUI only until the event loop
    if (m_changePanel)
    {
        m_changePanel->disconnect(this);
        m_changePanel->deleteLater();
    }
    if (m_keyWidget)
    {
        m_keyWidget->removeEventFilter(this);
        m_keyWidget->setAcceptDrops(m_wasDropAccepted);
    }
    for (const QPointer<QAction>& action : m_keyActions)
    {
        delete action.data();
    }
}

void PDFAppToolGUICompare::registerCompareTools()
{
    const QString toolId = QString::fromLatin1(PDFDocumentToolCompare::TOOL_ID);
    if (!PDFDocumentToolRegistry::isImplemented(toolId))
    {
        // The catalog (MCP, the shell) has the core tool; a run of the GUI has its own PDFAppCompareTool
        PDFDocumentToolRegistry::registerTool(std::make_shared<PDFDocumentToolCompare>());
    }
    if (!PDFDocumentToolGUIRegistry::getFactory(toolId))
    {
        PDFDocumentToolGUIRegistry::registerFactory(toolId, std::make_shared<PDFDocumentToolGUIFactoryOf<PDFAppToolGUICompare>>());
    }
}

PDFAppToolTab* PDFAppToolGUICompare::openCompareTab(PDFAppMainWindow* window, const QStringList& files)
{
    if (!window || files.size() != SlotCount)
    {
        return nullptr;
    }

    // Q7.13: the two files are the inputs of a new tab (never document tabs); the tab starts the comparison, when both
    // are read. The list goes to the tab in one call (R13); not taken by a tab, it stays here. The request belongs to
    // the pair of the command line: a file refused (the same file twice, FC-3) leaves a slot empty and no request - a
    // later choice of the user never starts the comparison by itself
    PDFAppToolInputList* inputList = new PDFAppToolInputList(nullptr);
    inputList->addFiles(files);
    if (inputList->getCount() == SlotCount)
    {
        inputList->setProperty(START_PROPERTY, inputList->getInputIds());
    }
    QPointer<PDFAppToolInputList> listGuard(inputList);
    PDFAppToolTab* tab = window->createToolTab(QString::fromLatin1(PDFDocumentToolCompare::TOOL_ID), inputList);
    if (listGuard && !listGuard->parent())
    {
        delete listGuard.data();
    }
    return tab;
}

PDFDocumentToolParameterDefinitions PDFAppToolGUICompare::getViewSettingDefinitions()
{
    using Definition = PDFDocumentToolParameterDefinition;
    using Helper = PDFAppToolGUICompareHelper;

    // The view settings of the GUI kept as the last values of the tool (the old Diff kept them in QSettings); the
    // defaults are the tokens diff/* and the items of Show of 492:20930
    const PDFDiffReportColors colors;
    PDFDocumentToolParameterDefinitions definitions;
    definitions.push_back(Definition::createColor(tr, Helper::name(SETTING_COLOR_REMOVED), QT_TR_NOOP("Removed"), QT_TR_NOOP("Color of the removed content"), colors.removed));
    definitions.push_back(Definition::createColor(tr, Helper::name(SETTING_COLOR_ADDED), QT_TR_NOOP("Added"), QT_TR_NOOP("Color of the added content"), colors.added));
    definitions.push_back(Definition::createColor(tr, Helper::name(SETTING_COLOR_REPLACED), QT_TR_NOOP("Replaced"), QT_TR_NOOP("Color of the replaced content"), colors.replaced));
    definitions.push_back(Definition::createColor(tr, Helper::name(SETTING_COLOR_MOVED), QT_TR_NOOP("Moved"), QT_TR_NOOP("Color of the moved pages"), colors.moved));
    definitions.push_back(Definition::createBool(tr, Helper::name(SETTING_HIGHLIGHTS), QT_TR_NOOP("Difference Highlights"), QT_TR_NOOP("Colored rectangles over the differences"), true));
    definitions.push_back(Definition::createBool(tr, Helper::name(SETTING_MARKERS), QT_TR_NOOP("Markers at the Edge"), QT_TR_NOOP("Arrows at the edge of the view"), true));
    definitions.push_back(Definition::createBool(tr, Helper::name(SETTING_ONLY_CHANGED), QT_TR_NOOP("Only Pages with Differences"), QT_TR_NOOP("Hide the pages that are the same"), false));
    definitions.push_back(Definition::createBool(tr, Helper::name(SETTING_FOLLOW), QT_TR_NOOP("Follow the Selected Difference"), QT_TR_NOOP("Selecting a difference scrolls the view to it"), true));
    return definitions;
}

QColor PDFAppToolGUICompare::getColor(const PDFDiffReportColors& colors, ColorKind kind)
{
    switch (kind)
    {
        case RemovedColor:
            return colors.removed;
        case AddedColor:
            return colors.added;
        case ReplacedColor:
            return colors.replaced;
        case MovedColor:
            return colors.moved;
        default:
            break;
    }
    return QColor();
}

PDFDiffReportColors PDFAppToolGUICompare::chooseColor(const PDFDiffReportColors& colors, ColorKind kind, const QColor& color)
{
    PDFDiffReportColors result = colors;
    std::array<QColor*, ColorKindCount> kindColors = { &result.removed, &result.added, &result.replaced, &result.moved };
    if (kind < 0 || kind >= ColorKindCount || !color.isValid())
    {
        return result;
    }

    // Plan 4.10 "colors with a swap on a collision": the kind, which has the color, takes the old color of this kind
    const QColor oldColor = *kindColors[size_t(kind)];
    for (int other = 0; other < ColorKindCount; ++other)
    {
        if (other != kind && kindColors[size_t(other)]->rgb() == color.rgb())
        {
            *kindColors[size_t(other)] = oldColor;
        }
    }
    *kindColors[size_t(kind)] = color;
    return result;
}

// ------------------------------------------------------------- the list of the inputs

PDFAppToolInputRules PDFAppToolGUICompare::getInputRules() const
{
    // The catalog: inputs {2, 2}; PDF files and open documents in the slots Original and Revised
    PDFAppToolInputRules rules;
    rules.minimum = 2;
    rules.maximum = 2;
    rules.acceptsPdf = true;
    rules.acceptsImages = false;
    rules.slotTitles = { tr("Original"), tr("Revised") };
    return rules;
}

void PDFAppToolGUICompare::connectList()
{
    PDFAppToolInputList* list = getInputList();
    if (list == m_list)
    {
        return;
    }

    if (m_list)
    {
        disconnect(m_list.data(), nullptr, this, nullptr);
    }
    m_list = list;
    if (m_list)
    {
        // The documents the tab came with (the first list of the tool) are no choice of the user (isChoiceChanged)
        if (!m_hasEntryChoice)
        {
            m_hasEntryChoice = true;
            m_entryChoice = m_list->getInputIds();
            m_entryChoice.sort();
        }

        const PDFAppToolInputRules rules = getInputRules();
        m_list->setAcceptedInputs(rules.maximum, rules.acceptsPdf, rules.acceptsImages, tr("Compare needs PDF files"));
        connect(m_list.data(), &PDFAppToolInputList::inputsChanged, this, &PDFAppToolGUICompare::onListChanged);
        connect(m_list.data(), &PDFAppToolInputList::inputChanged, this, &PDFAppToolGUICompare::onListInputChanged);
        connect(m_list.data(), &QObject::destroyed, this, &PDFAppToolGUICompare::onListDestroyed);
        m_lastInputIds = m_list->getInputIds();
        for (int slot = 0; slot < SlotCount; ++slot)
        {
            const PDFAppToolInput* input = m_list->getInput(slot);
            if (input && input->pageCount > 0)
            {
                m_rangeTexts[size_t(slot)] = PDFAppToolGUICompareHelper::getAllPagesText(input->pageCount);
            }
        }
    }

    // FC-5: the close of the source tab of a slot; the open tabs of the choice follow the registry
    if (PDFAppDocumentRegistry* registry = getContext() ? getContext()->getDocumentRegistry() : nullptr)
    {
        connect(registry, &PDFAppDocumentRegistry::documentRemoved, this, &PDFAppToolGUICompare::onDocumentRemoved, Qt::UniqueConnection);
        connect(registry, &PDFAppDocumentRegistry::documentAdded, this, &PDFAppToolGUICompare::onRegistryChanged, Qt::UniqueConnection);
        connect(registry, &PDFAppDocumentRegistry::documentChanged, this, &PDFAppToolGUICompare::onRegistryChanged, Qt::UniqueConnection);
    }
    startWhenRequested();
}

void PDFAppToolGUICompare::onListChanged()
{
    // The ids of the slots changed (an input replaced, swapped or removed): the result is of other documents. A change
    // of this GUI is one change (it calls onInputsReplaced at its end)
    if (m_isChangingList)
    {
        return;
    }
    const QStringList ids = m_list ? m_list->getInputIds() : QStringList();
    if (ids == m_lastInputIds)
    {
        updateAll();
        return;
    }
    onInputsReplaced();
}

void PDFAppToolGUICompare::onInputsReplaced()
{
    const QStringList oldIds = m_lastInputIds;
    m_lastInputIds = m_list ? m_list->getInputIds() : QStringList();

    // The page range of a slot, whose input changed, is all its pages again (the old Diff), the overlay resets
    for (int slot = 0; slot < SlotCount; ++slot)
    {
        if (oldIds.value(slot) != m_lastInputIds.value(slot))
        {
            const PDFAppToolInput* input = getSlotInput(Slot(slot));
            m_rangeTexts[size_t(slot)] = input && input->pageCount > 0 ? PDFAppToolGUICompareHelper::getAllPagesText(input->pageCount) : QString();
            m_rangeErrors[size_t(slot)].clear();
            setInputError(QStringLiteral("pages%1").arg(slot), QString());
        }
    }
    m_overlay = PDFAppCompareOverlaySettings();
    if (PDFAppCompareView* view = getCompareView())
    {
        view->setOverlaySettings(m_overlay);
    }

    // FC-4: a result of other documents is stale; without both inputs it is gone (FC-5: the state of one input)
    QPointer<PDFAppToolGUICompare> guard(this);
    if (m_hasResult)
    {
        if (!isSlotComparable(OriginalSlot) || !isSlotComparable(RevisedSlot))
        {
            clearResult();
        }
        m_staleReason = StaleReason::Documents;
        markResultStale();
        if (!guard)
        {
            return;
        }
    }

    // The values follow the inputs without an undo step
    if (PDFAppToolParameterModel* model = getModel())
    {
        for (int slot = 0; slot < SlotCount; ++slot)
        {
            PDFDocumentToolPageSelection selection;
            selection.pages = m_rangeTexts[size_t(slot)];
            model->setFollowingValue(PDFAppToolGUICompareHelper::name(slot == OriginalSlot ? PDFDocumentToolCompare::PARAMETER_ORIGINAL_PAGES : PDFDocumentToolCompare::PARAMETER_REVISED_PAGES), selection.toVariant());
            if (!guard)
            {
                return;
            }
        }
    }

    if (!m_hasResult && !m_isRunning)
    {
        m_content = Content::Ready;
        requestViewDocument();
    }
    updateAll();
    if (guard)
    {
        startWhenRequested();
    }
}

void PDFAppToolGUICompare::onListInputChanged(const QString& inputId)
{
    // A reading finished (the pages are known), a password: the page range of a new input is all its pages
    for (int slot = 0; slot < SlotCount; ++slot)
    {
        const PDFAppToolInput* input = getSlotInput(Slot(slot));
        if (input && input->id == inputId && input->pageCount > 0 && m_rangeTexts[size_t(slot)].isEmpty())
        {
            m_rangeTexts[size_t(slot)] = PDFAppToolGUICompareHelper::getAllPagesText(input->pageCount);
            if (PDFAppToolParameterModel* model = getModel())
            {
                PDFDocumentToolPageSelection selection;
                selection.pages = m_rangeTexts[size_t(slot)];
                QPointer<PDFAppToolGUICompare> guard(this);
                model->setFollowingValue(PDFAppToolGUICompareHelper::name(slot == OriginalSlot ? PDFDocumentToolCompare::PARAMETER_ORIGINAL_PAGES : PDFDocumentToolCompare::PARAMETER_REVISED_PAGES), selection.toVariant());
                if (!guard)
                {
                    return;
                }
            }
        }
    }

    // 492:106538: an encrypted input asks for its password at once (the dialog of step 2)
    const PDFAppToolInput* changed = m_list ? m_list->findInput(inputId) : nullptr;
    QPointer<PDFAppToolGUICompare> guard(this);
    if (!m_hasResult && !m_isRunning)
    {
        m_content = Content::Ready;
        requestViewDocument();
    }
    updateAll();
    if (!guard)
    {
        return;
    }
    if (changed && changed->state == PDFAppToolInputState::PasswordRequired && !changed->isPasswordWrong && !m_passwordDialog)
    {
        for (int slot = 0; slot < SlotCount; ++slot)
        {
            if (m_lastInputIds.value(slot) == inputId)
            {
                openPassword(Slot(slot));
                return;
            }
        }
    }
    startWhenRequested();
}

void PDFAppToolGUICompare::onListDestroyed()
{
    m_list = nullptr;
}

void PDFAppToolGUICompare::onDocumentRemoved(const QString& documentId)
{
    // FC-5: the source tab of a slot is closed - a running comparison stops, the slot empties (state of one input)
    if (!m_list)
    {
        return;
    }

    QStringList ids = m_list->getInputIds();
    bool isRemoved = false;
    for (int slot = 0; slot < SlotCount; ++slot)
    {
        const PDFAppToolInput* input = getSlotInput(Slot(slot));
        if (input && input->kind == PDFAppToolInputKind::OpenTab && input->documentId == documentId)
        {
            ids[slot] = QString();
            isRemoved = true;
        }
    }
    if (!isRemoved)
    {
        onRegistryChanged();
        return;
    }

    QPointer<PDFAppToolGUICompare> guard(this);
    if (m_isRunning && getShell())
    {
        getShell()->stop();
        if (!guard)
        {
            return;
        }
    }
    ids.removeAll(QString());
    setInputIds(ids);
}

void PDFAppToolGUICompare::onRegistryChanged()
{
    updateChoice();
}

void PDFAppToolGUICompare::setInputIds(const QStringList& ids)
{
    // The inputs of the slots; the inputs, which went, are freed at once (no undo history of the inputs - Change… and
    // Swap are no steps of Undo, they make the result stale)
    QPointer<PDFAppToolInputList> list(m_list);
    if (!list)
    {
        return;
    }
    list->setInputIds(ids);
    if (list)
    {
        list->dropParkedInputs(ids);
    }
}

void PDFAppToolGUICompare::startWhenRequested()
{
    // PDF4QT --compare (Q7.13): the comparison starts once, when both inputs are read; queued (the caller may be a slot
    // of the list or of the shell)
    if (!m_list || m_isStartQueued || !getShell())
    {
        return;
    }
    const QVariant request = m_list->property(START_PROPERTY);
    if (!request.isValid())
    {
        return;
    }

    // The request is of the two inputs of the command line. It ends unfulfilled, when the slots hold another pair (the
    // user changed one) or an input cannot be compared (a damaged file, no pages); an input, which needs its password,
    // keeps it (the dialog asks at once, the comparison starts after the password)
    const QStringList requestedIds = request.toStringList();
    bool isPossible = requestedIds.size() == SlotCount && m_list->getInputIds() == requestedIds;
    bool isWaiting = false;
    for (int slot = 0; slot < SlotCount && isPossible; ++slot)
    {
        const PDFAppToolInput* input = getSlotInput(Slot(slot));
        if (!input)
        {
            isPossible = false;
        }
        else if (input->state == PDFAppToolInputState::Loading || input->state == PDFAppToolInputState::PasswordRequired)
        {
            isWaiting = true;
        }
        else if (!isSlotComparable(Slot(slot)))
        {
            isPossible = false;
        }
    }
    if (isPossible && isWaiting)
    {
        return;
    }

    m_list->setProperty(START_PROPERTY, QVariant());
    if (isPossible && isConfirmEnabled())
    {
        m_isStartQueued = true;
        QMetaObject::invokeMethod(this, &PDFAppToolGUICompare::onStartQueued, Qt::QueuedConnection);
    }
}

void PDFAppToolGUICompare::onStartQueued()
{
    m_isStartQueued = false;
    if (PDFDocumentToolShell* shell = getShell(); shell && isConfirmEnabled() && shell->getState() == PDFDocumentToolShell::State::Editing)
    {
        // The last call (the run starts, its slots may delete this GUI)
        shell->confirm();
    }
}

const PDFAppToolInput* PDFAppToolGUICompare::getSlotInput(Slot slot) const
{
    const PDFAppToolInputList* list = m_list ? m_list.data() : getInputList();
    return list ? list->getInput(int(slot)) : nullptr;
}

bool PDFAppToolGUICompare::isSlotComparable(Slot slot) const
{
    // Q7.2: comparing on the screen is reading - a restricted PDF is compared too (its report and XML need the owner
    // password); a document without pages is an error (2.10)
    const PDFAppToolInput* input = getSlotInput(slot);
    return input && input->isPdf() && input->document && input->pageCount > 0 &&
           (input->state == PDFAppToolInputState::Ready || input->state == PDFAppToolInputState::Restricted);
}

// ------------------------------------------------------------- actions of the user

void PDFAppToolGUICompare::swapSlots()
{
    if (!m_list || m_isRunning || m_list->getCount() != SlotCount)
    {
        return;
    }

    // The ranges go with their documents; the result is of the swapped documents (FC-4)
    QStringList ids = m_list->getInputIds();
    std::swap(ids[0], ids[1]);
    const std::array<QString, SlotCount> texts = { m_rangeTexts[1], m_rangeTexts[0] };
    const std::array<QString, SlotCount> errors = { m_rangeErrors[1], m_rangeErrors[0] };

    QPointer<PDFAppToolGUICompare> guard(this);
    ChangingListGuard changing(this);
    setInputIds(ids);
    if (!guard)
    {
        return;
    }
    changing.end();
    onInputsReplaced();
    if (!guard)
    {
        return;
    }
    m_rangeTexts = texts;
    m_rangeErrors = errors;
    for (int slot = 0; slot < SlotCount; ++slot)
    {
        setInputError(QStringLiteral("pages%1").arg(slot), m_rangeErrors[size_t(slot)]);
    }
    if (PDFAppToolParameterModel* model = getModel())
    {
        for (int slot = 0; slot < SlotCount && guard; ++slot)
        {
            PDFDocumentToolPageSelection selection;
            selection.pages = m_rangeTexts[size_t(slot)];
            model->setFollowingValue(PDFAppToolGUICompareHelper::name(slot == OriginalSlot ? PDFDocumentToolCompare::PARAMETER_ORIGINAL_PAGES : PDFDocumentToolCompare::PARAMETER_REVISED_PAGES), selection.toVariant());
        }
    }
    if (guard)
    {
        updateAll();
    }
}

void PDFAppToolGUICompare::setSlotFile(Slot slot, const QString& filePath)
{
    if (!m_list || m_isRunning || filePath.isEmpty())
    {
        return;
    }

    addSlotInput(slot, filePath, QString());
}

void PDFAppToolGUICompare::addSlotInput(Slot slot, const QString& filePath, const QString& documentId)
{
    // The list holds two inputs at most: the old input of the slot is parked first (the list is not full then), the
    // new one is added, a refusal takes the old one back
    QPointer<PDFAppToolGUICompare> guard(this);
    QPointer<PDFAppToolInputList> list(m_list);
    const QStringList oldIds = list->getInputIds();
    const QString oldId = oldIds.value(int(slot));

    // The steps of the change are one change of the inputs (onInputsReplaced once, at the end); the guard ends the
    // change on every return (a slot of the list may delete the list)
    ChangingListGuard changing(this);
    if (!oldId.isEmpty())
    {
        list->removeInput(oldId);
        if (!guard || !list)
        {
            return;
        }
    }

    PDFAppToolInputList::AddResult result;
    if (documentId.isEmpty())
    {
        result = list->addFile(filePath);
    }
    else
    {
        const PDFAppDocumentRegistry* registry = getContext() ? getContext()->getDocumentRegistry() : nullptr;
        const PDFAppDocument* document = registry ? registry->findDocument(documentId) : nullptr;
        result = document ? list->addOpenDocument(document) : PDFAppToolInputList::AddResult();
    }
    if (!guard || !list)
    {
        return;
    }
    if (result.id.isEmpty())
    {
        // Refused (the same file in the other slot, FC-3): the slot keeps its input
        setInputIds(oldIds);
        if (!guard)
        {
            return;
        }
        changing.end();
        if (getHost())
        {
            getHost()->showTransientMessage(PDFAppToolInputList::getRefusalText(result.refusal, result.name, SlotCount));
        }
        return;
    }

    // The order of the list is the order of the slots; an empty Original is no place in the list, so a Revised alone
    // is in the first place until the Original comes
    QStringList ids = oldIds;
    while (ids.size() < SlotCount)
    {
        ids << QString();
    }
    ids[int(slot)] = result.id;
    if (slot == RevisedSlot && oldIds.isEmpty())
    {
        ids = QStringList{ result.id };
    }
    ids.removeAll(QString());
    setInputIds(ids);
    if (!guard)
    {
        return;
    }
    changing.end();
    onInputsReplaced();
}

PDFAppToolGUICompare::ChangingListGuard::ChangingListGuard(PDFAppToolGUICompare* gui) :
    m_gui(gui)
{
    if (m_gui)
    {
        m_gui->m_isChangingList = true;
    }
}

PDFAppToolGUICompare::ChangingListGuard::~ChangingListGuard()
{
    end();
}

void PDFAppToolGUICompare::ChangingListGuard::end()
{
    // A GUI deleted by a slot of the list is gone (the pointer is null then)
    if (m_gui)
    {
        m_gui->m_isChangingList = false;
        m_gui = nullptr;
    }
}

void PDFAppToolGUICompare::setSlotDocument(Slot slot, const QString& documentId)
{
    const PDFAppDocumentRegistry* registry = getContext() ? getContext()->getDocumentRegistry() : nullptr;
    const PDFAppDocument* document = registry ? registry->findDocument(documentId) : nullptr;
    if (!m_list || m_isRunning || !document)
    {
        return;
    }

    // R11: a snapshot of now - an unsaved tab is its working version
    addSlotInput(slot, QString(), documentId);
}

void PDFAppToolGUICompare::chooseSlotFile(Slot slot)
{
    if (m_isRunning || (m_fileRequest && m_fileRequest->isPending()))
    {
        return;
    }

    // A0: the dialog answers onSlotFileChosen (the request is a child of the GUI: a GUI gone meanwhile gets nothing)
    m_fileRequestSlot = slot;
    m_fileRequest = PDFAppFileDialogs::requestOpenFileNames(getDialogParent(), QString(), this, &PDFAppToolGUICompare::onSlotFileChosen);
}

void PDFAppToolGUICompare::onSlotFileChosen(const QStringList& files)
{
    // setSlotFile checks the state again (a run started meanwhile takes no file)
    if (!files.isEmpty())
    {
        setSlotFile(m_fileRequestSlot, files.front());
    }
}

void PDFAppToolGUICompare::openPassword(Slot slot)
{
    const PDFAppToolInput* input = getSlotInput(slot);
    if (!input || !m_list || m_passwordDialog)
    {
        return;
    }

    // The dialog of step 2 (a popup, deleted when it is closed); its Remove File empties the slot
    m_passwordDialog = PDFAppPasswordDialog::openDialog(m_list, input->id, getDialogParent());
    if (m_passwordDialog)
    {
        connect(m_passwordDialog.data(), &PDFAppPasswordDialog::removeRequested, this, &PDFAppToolGUICompare::onPasswordRemoveRequested);
    }
}

void PDFAppToolGUICompare::onPasswordRemoveRequested(const QString& inputId)
{
    if (!m_list || m_isRunning)
    {
        return;
    }
    QStringList ids = m_list->getInputIds();
    ids.removeAll(inputId);
    setInputIds(ids);
}

void PDFAppToolGUICompare::setPageRangeText(Slot slot, const QString& text)
{
    // K7.3: the parser and the texts of Page selection (PDFPageRange::parse: "The first page (30) is after the last
    // page (12).", "Page 60 does not exist – the document has 48 pages"); the value is the canonical text, which
    // the core reads (digits and separators of other scripts are taken)
    const PDFAppToolInput* input = getSlotInput(slot);
    const PDFInteger pageCount = input ? input->pageCount : 0;
    QString error;
    QString trimmed = text.trimmed();
    if (pageCount > 0 && !trimmed.isEmpty())
    {
        // The form of the range first (a number, a reversed range - 492:105816 "30-12" of 24 pages is the reversed
        // range), then the pages of the document
        error = PDFPageRange::parse(trimmed, PDFPageRange::MAX_PAGE_COUNT).errorMessage;
        if (error.isEmpty())
        {
            error = PDFPageRange::parse(trimmed, PDFAppToolGUICompareHelper::getRangePageCount(pageCount)).errorMessage;
        }
        if (error.isEmpty())
        {
            trimmed = PDFPageRange::toCanonicalText(trimmed);
        }
    }
    else if (pageCount > 0)
    {
        error = tr("Enter the pages to compare, such as 1-%1.").arg(PDFAppLocale::formatNumber(pageCount));
    }

    m_rangeTexts[size_t(slot)] = trimmed;
    m_rangeErrors[size_t(slot)] = error;
    QPointer<PDFAppToolGUICompare> guard(this);
    setInputError(QStringLiteral("pages%1").arg(int(slot)), error);
    if (!guard)
    {
        return;
    }

    // A valid range is a value of the user: one undo step, a result becomes stale (FC-4, the shell asks
    // isResultStaleBy)
    if (error.isEmpty())
    {
        if (PDFAppToolParameterModel* model = getModel())
        {
            PDFDocumentToolPageSelection selection;
            selection.pages = trimmed;
            model->setValue(PDFAppToolGUICompareHelper::name(slot == OriginalSlot ? PDFDocumentToolCompare::PARAMETER_ORIGINAL_PAGES : PDFDocumentToolCompare::PARAMETER_REVISED_PAGES), selection.toVariant());
            if (!guard)
            {
                return;
            }
        }
        if (!m_hasResult && !m_isRunning)
        {
            requestViewDocument();
        }
    }
    updateAll();
}

QString PDFAppToolGUICompare::getPageRangeText(Slot slot) const
{
    return m_rangeTexts[size_t(slot)];
}

QString PDFAppToolGUICompare::getPageRangeError(Slot slot) const
{
    return m_rangeErrors[size_t(slot)];
}

std::vector<PDFInteger> PDFAppToolGUICompare::getRangePages(Slot slot) const
{
    // The pages (from 0) of the range of the slot; an invalid range is all pages (the preview only)
    std::vector<PDFInteger> pages;
    const PDFAppToolInput* input = getSlotInput(slot);
    const PDFInteger pageCount = input ? input->pageCount : 0;
    if (pageCount <= 0)
    {
        return pages;
    }

    // The intervals are bounded by the pages (no unfold of a hostile range, B174)
    const int boundedCount = PDFAppToolGUICompareHelper::getRangePageCount(pageCount);
    PDFPageRange::Result range = PDFPageRange::parse(m_rangeTexts[size_t(slot)], boundedCount);
    if (!range.isValid() || range.intervals.isEmpty())
    {
        range.intervals = { PDFPageRange::Interval{ 1, boundedCount } };
    }
    for (const PDFPageRange::Interval& interval : range.intervals)
    {
        for (int page = qMax(1, interval.first); page <= qMin(interval.last, boundedCount); ++page)
        {
            pages.push_back(PDFInteger(page) - 1);
        }
    }
    return pages;
}

void PDFAppToolGUICompare::setView(PDFDiffReportView view)
{
    if (PDFAppToolParameterModel* model = getModel())
    {
        model->setValue(PDFAppToolGUICompareHelper::name(PDFDocumentToolCompare::PARAMETER_VIEW), PDFAppToolGUICompareHelper::getViewId(view));
    }
}

PDFDiffReportView PDFAppToolGUICompare::getView() const
{
    const PDFAppToolParameterModel* model = getModel();
    return PDFDocumentToolCompare::getView(model ? model->getValue(PDFAppToolGUICompareHelper::name(PDFDocumentToolCompare::PARAMETER_VIEW)).toString() : QString());
}

void PDFAppToolGUICompare::setShowOption(const QString& settingName, bool isOn)
{
    if (settingName == QLatin1String(SETTING_HIGHLIGHTS))
    {
        m_isHighlightsShown = isOn;
    }
    else if (settingName == QLatin1String(SETTING_MARKERS))
    {
        m_isMarkersShown = isOn;
    }
    else if (settingName == QLatin1String(SETTING_ONLY_CHANGED))
    {
        m_isOnlyChangedPages = isOn;
    }
    else if (settingName == QLatin1String(SETTING_FOLLOW))
    {
        m_isSelectionFollowed = isOn;
    }
    else
    {
        return;
    }

    storeViewSetting(settingName, isOn);
    if (PDFAppCompareView* view = getCompareView())
    {
        view->setShowOptions(m_isHighlightsShown, m_isMarkersShown, m_isOnlyChangedPages);
    }
    updateAll();
}

void PDFAppToolGUICompare::setColor(ColorKind kind, const QColor& color)
{
    const PDFDiffReportColors oldColors = m_colors;
    m_colors = chooseColor(m_colors, kind, color);
    const std::array<const char*, ColorKindCount> names = { SETTING_COLOR_REMOVED, SETTING_COLOR_ADDED, SETTING_COLOR_REPLACED, SETTING_COLOR_MOVED };
    for (int other = 0; other < ColorKindCount; ++other)
    {
        if (getColor(oldColors, ColorKind(other)) != getColor(m_colors, ColorKind(other)))
        {
            storeViewSetting(QString::fromLatin1(names[size_t(other)]), getColor(m_colors, ColorKind(other)));
        }
    }
    if (PDFAppCompareView* view = getCompareView())
    {
        view->setColors(m_colors);
    }
    if (m_differenceDelegate)
    {
        m_differenceDelegate->setColors(m_colors);
    }
    if (m_differenceView)
    {
        m_differenceView->viewport()->update();
    }
    updateAll();
}

void PDFAppToolGUICompare::setOverlaySettings(const PDFAppCompareOverlaySettings& settings)
{
    PDFAppCompareOverlaySettings bounded = settings;
    bounded.originalScale = qBound(PDFAppCompareOverlaySettings::MINIMUM_SCALE * 0.01, settings.originalScale, PDFAppCompareOverlaySettings::MAXIMUM_SCALE * 0.01);
    bounded.revisedScale = qBound(PDFAppCompareOverlaySettings::MINIMUM_SCALE * 0.01, settings.revisedScale, PDFAppCompareOverlaySettings::MAXIMUM_SCALE * 0.01);
    bounded.revisedOffsetMM = QPointF(qBound(-PDFAppCompareOverlaySettings::MAXIMUM_OFFSET, settings.revisedOffsetMM.x(), PDFAppCompareOverlaySettings::MAXIMUM_OFFSET),
                                      qBound(-PDFAppCompareOverlaySettings::MAXIMUM_OFFSET, settings.revisedOffsetMM.y(), PDFAppCompareOverlaySettings::MAXIMUM_OFFSET));
    bounded.blend = qBound(0, settings.blend, 100);
    m_overlay = bounded;
    if (PDFAppCompareView* view = getCompareView())
    {
        view->setOverlaySettings(m_overlay);
    }
    updateAll();
}

void PDFAppToolGUICompare::resetOverlay()
{
    // Reset of the old Diff: Original sizes, 100 %, no shift, the blend 50 / 50
    setOverlaySettings(PDFAppCompareOverlaySettings());
}

// ------------------------------------------------------------- the result

bool PDFAppToolGUICompare::isResultKeptInTool() const
{
    // Plan 4.1: Compare shows its result itself and stays open
    return true;
}

void PDFAppToolGUICompare::onResultKept(const PDFDocumentToolResult& result)
{
    // The whole result of the collector of the run (the data hold at most MAXIMUM_DIFFERENCES)
    const std::shared_ptr<PDFDiffResultCollector> collector = std::move(m_runCollector);
    m_runCollector.reset();
    const std::optional<PDFDiffResult> collected = collector ? collector->getResult() : std::nullopt;
    if (!collected || !collected->getResult() || m_runInputs.size() != SlotCount)
    {
        // FC-2: the comparison failed (never "no differences", B173)
        m_content = Content::Failed;
        m_failureText = collected && !collected->getResult() ? collected->getResult().getErrorMessage() : result.message;
        updateAll();
        return;
    }

    const std::vector<PDFDocumentToolInputDocument> inputs = std::move(m_runInputs);
    m_runInputs.clear();
    showResult(*collected, inputs[0], inputs[1]);
}

void PDFAppToolGUICompare::showResult(const PDFDiffResult& result, const PDFDocumentToolInputDocument& original, const PDFDocumentToolInputDocument& revised)
{
    m_resultInputs = { original, revised };
    m_isStopped = false;
    m_failureText.clear();
    applyResult(result);
}

void PDFAppToolGUICompare::applyResult(const PDFDiffResult& result)
{
    // A report of the previous result is never offered for this one
    cancelReport();
    m_result = result;
    m_hasResult = true;
    m_selected.reset();
    m_content = m_result.isSame() ? Content::NoDifferences : Content::Result;

    QPointer<PDFAppToolGUICompare> guard(this);
    updateShownResult();
    if (!guard)
    {
        return;
    }
    requestViewDocument();
    updateAll();
}

void PDFAppToolGUICompare::cancelReport()
{
    // The job of the report ends alone on its copies; its result goes with its watcher (never offered: the report is
    // of a result, which is not shown any more)
    if (m_reportCancel)
    {
        m_reportCancel->cancel();
        m_reportCancel.reset();
    }
    if (m_reportWatcher)
    {
        m_reportWatcher->disconnect(this);
        m_reportWatcher->deleteLater();
        m_reportWatcher = nullptr;
    }
}

void PDFAppToolGUICompare::clearResult()
{
    cancelReport();
    m_result = PDFDiffResult();
    m_shownResult = PDFDiffResult();
    m_resultInputs.clear();
    m_hasResult = false;
    m_selected.reset();
    if (m_differenceModel)
    {
        m_differenceModel->setResult(m_shownResult);
    }
}

void PDFAppToolGUICompare::updateShownResult()
{
    // The menu Filter over the whole result (the counts stay of the whole result)
    const PDFAppToolParameterModel* model = getModel();
    m_shownResult = m_hasResult && model ? PDFDocumentToolCompare::filter(m_result, model->getValues()) : PDFDiffResult();
    if (m_selected && *m_selected >= m_shownResult.getDifferencesCount())
    {
        m_selected.reset();
    }

    QPointer<PDFAppToolGUICompare> guard(this);
    if (m_differenceModel)
    {
        // The reset of the model runs the slots of its views
        m_differenceModel->setResult(m_shownResult);
        if (!guard)
        {
            return;
        }
        if (m_differenceView)
        {
            m_differenceView->expandAll();
        }
    }
    if (PDFAppCompareView* view = getCompareView())
    {
        view->setResult(m_hasResult ? m_shownResult : createPreviewSequence());
        view->setSelectedDifference(m_selected, false);
    }
}

PDFDiffResult PDFAppToolGUICompare::createPreviewSequence() const
{
    // Before the comparison (492:18424): the pages of the ranges side by side in their order
    PDFDiffResult preview;
    const std::vector<PDFInteger> original = getRangePages(OriginalSlot);
    const std::vector<PDFInteger> revised = getRangePages(RevisedSlot);
    PDFDiffResult::PageSequence sequence;
    const size_t count = qMax(original.size(), revised.size());
    sequence.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        PDFDiffResult::PageSequenceItem item;
        item.leftPage = i < original.size() ? original[i] : -1;
        item.rightPage = i < revised.size() ? revised[i] : -1;
        sequence.push_back(item);
    }
    preview.setPageSequence(std::move(sequence));
    return preview;
}

void PDFAppToolGUICompare::requestViewDocument()
{
    using Helper = PDFAppToolGUICompareHelper;

    // One job: the document of the view of the result, or of the inputs before the comparison
    PDFDocumentPointer original;
    PDFDocumentPointer revised;
    if (m_hasResult && m_resultInputs.size() == SlotCount)
    {
        original = m_resultInputs[0].document;
        revised = m_resultInputs[1].document;
    }
    else if (isSlotComparable(OriginalSlot) && isSlotComparable(RevisedSlot))
    {
        original = getSlotInput(OriginalSlot)->document;
        revised = getSlotInput(RevisedSlot)->document;
    }

    if (m_viewCancel)
    {
        m_viewCancel->cancel();
    }
    if (m_viewWatcher)
    {
        m_viewWatcher->disconnect(this);
        m_viewWatcher->deleteLater();
        m_viewWatcher = nullptr;
    }
    ++m_viewGeneration;
    if (!original || !revised)
    {
        m_isViewDocumentPending = false;
        if (PDFAppCompareView* view = getCompareView())
        {
            view->setDocument(PDFDiffCombinedDocument());
        }
        return;
    }

    const PDFDiffReportView documentView = Helper::getDocumentView(getView());
    const PDFDiffResult sequence = m_hasResult ? m_result : createPreviewSequence();
    m_viewDocumentView = documentView;
    m_viewCancel = std::make_shared<PDFAppCancelFlag>();
    m_isViewDocumentPending = true;
    m_viewWatcher = new QFutureWatcher<PDFDiffCombinedDocument>(this);
    m_viewWatcher->setProperty("pdf4qtGeneration", m_viewGeneration);
    connect(m_viewWatcher.data(), &QFutureWatcher<PDFDiffCombinedDocument>::finished, this, &PDFAppToolGUICompare::onViewDocumentReady);
    m_viewWatcher->setFuture(QtConcurrent::run(&Helper::buildViewDocument, documentView, original, revised, sequence, m_viewCancel));
}

void PDFAppToolGUICompare::onViewDocumentReady()
{
    QFutureWatcher<PDFDiffCombinedDocument>* watcher = m_viewWatcher.data();
    if (!watcher || sender() != watcher || watcher->property("pdf4qtGeneration").toULongLong() != m_viewGeneration)
    {
        return;
    }

    // Only the last request is shown
    const PDFDiffCombinedDocument document = watcher->result();
    m_viewWatcher = nullptr;
    watcher->deleteLater();
    m_isViewDocumentPending = false;

    QPointer<PDFAppToolGUICompare> guard(this);
    if (PDFAppCompareView* view = getCompareView())
    {
        view->setDocument(document);
        if (!guard)
        {
            return;
        }
        view->setResult(m_hasResult ? m_shownResult : createPreviewSequence());
        view->setSelectedDifference(m_selected, false);
        if (!document.isSuccess())
        {
            m_failureText = document.errorMessage;
        }
    }
    if (guard)
    {
        updateAll();
    }
}

// ------------------------------------------------------------- navigation

void PDFAppToolGUICompare::selectDifference(std::optional<size_t> index)
{
    if (index && *index >= m_shownResult.getDifferencesCount())
    {
        index.reset();
    }
    m_selected = index;

    QPointer<PDFAppToolGUICompare> guard(this);
    if (m_differenceView && m_differenceModel && !m_isUpdatingControls)
    {
        m_isUpdatingControls = true;
        const QModelIndex modelIndex = index ? m_differenceModel->findIndex(*index) : QModelIndex();
        if (modelIndex.isValid())
        {
            m_differenceView->setCurrentIndex(modelIndex);
            m_differenceView->scrollTo(modelIndex);
        }
        else
        {
            m_differenceView->clearSelection();
        }
        if (!guard)
        {
            return;
        }
        m_isUpdatingControls = false;
    }
    if (PDFAppCompareView* view = getCompareView())
    {
        view->setSelectedDifference(m_selected, m_isSelectionFollowed);
    }
    updateAll();
}

bool PDFAppToolGUICompare::canGoToNextDifference() const
{
    const size_t count = m_shownResult.getDifferencesCount();
    return m_hasResult && !m_isRunning && count > 0 && (!m_selected || *m_selected + 1 < count);
}

bool PDFAppToolGUICompare::canGoToPreviousDifference() const
{
    return m_hasResult && !m_isRunning && m_selected && *m_selected > 0;
}

void PDFAppToolGUICompare::goToNextDifference()
{
    if (canGoToNextDifference())
    {
        selectDifference(m_selected ? *m_selected + 1 : 0);
    }
}

void PDFAppToolGUICompare::goToPreviousDifference()
{
    if (canGoToPreviousDifference())
    {
        selectDifference(*m_selected - 1);
    }
}

int PDFAppToolGUICompare::getCategoryCount(const QString& categoryId) const
{
    int count = 0;
    const size_t total = m_hasResult ? m_result.getDifferencesCount() : 0;
    for (size_t i = 0; i < total; ++i)
    {
        if (PDFDocumentToolCompare::getCategoryId(m_result.getType(i)) == categoryId)
        {
            ++count;
        }
    }
    return count;
}

// ------------------------------------------------------------- export

bool PDFAppToolGUICompare::isExportEnabled() const
{
    // R7-9: no export without a shown difference (the old Diff, 492:105498)
    return m_hasResult && !m_isRunning && m_shownResult.isChanged() && !m_reportWatcher;
}

bool PDFAppToolGUICompare::checkExportPermission(bool isReport)
{
    // Q7.2: the XML holds the texts of the documents (bit 5), the report is a new document of their pages (the rule of
    // Merge). An input, whose owner password was verified since the run (the same file), is accepted; a restricted
    // input asks for its owner password (FC-6) and the export waits for the next click
    for (int slot = 0; slot < SlotCount && slot < int(m_resultInputs.size()); ++slot)
    {
        PDFDocumentToolInputDocument& input = m_resultInputs[size_t(slot)];
        const PDFAppToolInput* current = getSlotInput(Slot(slot));
        if (!input.isOwnerPasswordVerified && current && current->isOwnerPasswordVerified && current->id == input.inputId && current->originalData == input.originalData)
        {
            input.isOwnerPasswordVerified = true;
        }

        const bool isPermitted = isReport ? PDFDocumentMerger::getPermission(input.document.data(), input.isOwnerPasswordVerified) == PDFDocumentMerger::Permission::Permitted
                                          : PDFAppToolGUICompareHelper::isCopyPermitted(input);
        if (isPermitted)
        {
            continue;
        }

        QPointer<PDFAppToolGUICompare> guard(this);
        if (current && current->state == PDFAppToolInputState::Restricted && current->id == input.inputId)
        {
            openPassword(Slot(slot));
        }
        else if (getHost())
        {
            getHost()->showTransientMessage(tr("%1 does not permit the export of its content. Open it with its owner password and compare again.").arg(QFileInfo(input.fileName).fileName()));
        }
        return false;
    }
    return true;
}

bool PDFAppToolGUICompare::exportXml()
{
    using Helper = PDFAppToolGUICompareHelper;

    if (!isExportEnabled() || m_resultInputs.size() != SlotCount || !checkExportPermission(false))
    {
        return false;
    }

    // The shown differences (the menu Filter) as XML; the dialog of the export writes the file (492:108839)
    QByteArray data;
    m_shownResult.saveToXML(&data);
    PDFDocumentToolResult exportResult;
    PDFDocumentToolOutputFile output;
    output.suggestedFileName = tr("%1 vs %2 - differences.xml").arg(Helper::getBaseName(m_resultInputs[0].fileName), Helper::getBaseName(m_resultInputs[1].fileName));
    output.data = std::move(data);
    output.mimeType = QStringLiteral("application/xml");
    exportResult.outputs.push_back(std::move(output));
    m_exportKind = ExportKind::Xml;
    m_exportFacts = createExportFacts(m_shownResult, getView());
    return openExport(exportResult);
}

bool PDFAppToolGUICompare::exportReport()
{
    using Helper = PDFAppToolGUICompareHelper;

    if (!isExportEnabled() || m_resultInputs.size() != SlotCount || !checkExportPermission(true))
    {
        return false;
    }

    // The report of the view of now with the colors of the panel (492:107143) - in one job, then the dialog
    PDFDiffReportSettings settings;
    settings.view = getView();
    settings.colors = m_colors;
    settings.author = getHost() ? getHost()->createEnvironment().authorName : QString();
    const QString fileName = tr("%1 vs %2 - comparison.pdf").arg(Helper::getBaseName(m_resultInputs[0].fileName), Helper::getBaseName(m_resultInputs[1].fileName));
    m_reportFacts = createExportFacts(m_shownResult, settings.view);
    m_reportCancel = std::make_shared<PDFAppCancelFlag>();
    m_reportWatcher = new QFutureWatcher<PDFDocumentToolResult>(this);
    connect(m_reportWatcher.data(), &QFutureWatcher<PDFDocumentToolResult>::finished, this, &PDFAppToolGUICompare::onReportReady);
    m_reportWatcher->setFuture(QtConcurrent::run(&Helper::buildReport, m_resultInputs[0], m_resultInputs[1], m_shownResult, settings, m_reportCancel, fileName));
    updateAll();
    return true;
}

void PDFAppToolGUICompare::onReportReady()
{
    QFutureWatcher<PDFDocumentToolResult>* watcher = m_reportWatcher.data();
    if (!watcher || sender() != watcher)
    {
        return;
    }

    const PDFDocumentToolResult report = watcher->result();
    m_reportWatcher = nullptr;
    watcher->deleteLater();

    QPointer<PDFAppToolGUICompare> guard(this);
    updateAll();
    if (!guard)
    {
        return;
    }
    if (!report.isSuccess() || report.outputs.size() != 1)
    {
        if (report.status != PDFDocumentToolResultStatus::Stopped && getHost())
        {
            getHost()->showTransientMessage(report.message);
        }
        return;
    }

    // 492:107143: the pages of the view, the differences marked as annotations - of the result, the filter and the view
    // of the job (a change of the view or of the filter meanwhile is not in the report)
    m_exportKind = ExportKind::Report;
    m_exportFacts = m_reportFacts;
    openExport(report);
}

PDFAppToolGUICompare::ExportFacts PDFAppToolGUICompare::createExportFacts(const PDFDiffResult& shownResult, PDFDiffReportView view)
{
    ExportFacts facts;
    facts.view = view;
    facts.differenceCount = int(qMin(shownResult.getDifferencesCount(), size_t(std::numeric_limits<int>::max())));
    for (const PDFDiffResult::PageSequenceItem& item : shownResult.getPageSequence())
    {
        if (facts.pairCount == std::numeric_limits<int>::max())
        {
            break;
        }
        facts.pairCount += 1;
        facts.originalPageCount += item.leftPage >= 0 ? 1 : 0;
        facts.revisedPageCount += item.rightPage >= 0 ? 1 : 0;
    }
    return facts;
}

void PDFAppToolGUICompare::fillResultInput(PDFAppToolResultInput* input) const
{
    // The dialogs of the export (492:107143, 492:108839): the facts of the exported file
    const int count = m_exportFacts.differenceCount;
    if (m_exportKind == ExportKind::Report)
    {
        // "25 page pairs, side by side" - the pages of the view of the report
        QString pagesText;
        switch (m_exportFacts.view)
        {
            case PDFDiffReportView::Original:
                pagesText = tr("%Ln original pages", nullptr, m_exportFacts.originalPageCount);
                break;
            case PDFDiffReportView::Revised:
                pagesText = tr("%Ln revised pages", nullptr, m_exportFacts.revisedPageCount);
                break;
            case PDFDiffReportView::Overlay:
                pagesText = tr("%Ln page pairs, overlaid", nullptr, m_exportFacts.pairCount);
                break;
            case PDFDiffReportView::SideBySide:
                pagesText = tr("%Ln page pairs, side by side", nullptr, m_exportFacts.pairCount);
                break;
        }
        input->title = tr("Comparison report is ready");
        input->subtitle = tr("Nothing has been written yet – choose where to save the report.");
        input->fileConfirmText = tr("Save Report");
        input->fileOpenText = tr("Open the report in a new tab");
        input->isFileOpenChecked = true;
        input->statusText = tr("The comparison report is ready to be saved.");
        input->result.facts.push_back({ QStringLiteral("pages"), tr("Pages"), pagesText });
        input->result.facts.push_back({ QStringLiteral("differences"), tr("Differences"), tr("%Ln marked as annotations", nullptr, count) });
        input->result.facts.push_back({ QStringLiteral("annotation"), tr("Annotation"), tr("Colored polygon with the description") });
    }
    else if (m_exportKind == ExportKind::Xml)
    {
        // 492:108839 (no option to open the file; an existing file: the Replace File of the dialog, 492:108278)
        input->title = tr("List of differences is ready");
        input->subtitle = tr("Nothing has been written yet – choose where to save the XML file.");
        input->fileConfirmText = tr("Save File");
        input->fileOpenText = QString();
        input->isFileOpenChecked = false;
        input->statusText = tr("The list of differences is ready to be saved.");
        input->result.facts.push_back({ QStringLiteral("differences"), tr("Differences"), tr("%Ln shown differences", nullptr, count) });
        input->result.facts.push_back({ QStringLiteral("content"), tr("Content"), tr("Type, pages and changed text") });
        input->result.facts.push_back({ QStringLiteral("pageSequence"), tr("Page sequence"), tr("%Ln page pairs", nullptr, m_exportFacts.pairCount) });
    }
    if (m_resultInputs.size() == SlotCount)
    {
        const PDFAppToolInput* original = getSlotInput(OriginalSlot);
        if (original && !original->filePath.isEmpty())
        {
            input->initialFolder = QFileInfo(original->filePath).absolutePath();
        }
    }
}

void PDFAppToolGUICompare::onExportDecided(const PDFAppToolResultOutcome& outcome)
{
    // 492:107143 "Open the report in a new tab": the written file opens as a document (the last call)
    const ExportKind kind = m_exportKind;
    m_exportKind = ExportKind::None;
    if (kind == ExportKind::Report && outcome.action == PDFAppToolResultOutcome::Action::FileSaved && outcome.isFileOpenRequested && !outcome.filePath.isEmpty())
    {
        if (PDFAppMainWindow* window = m_canvas ? PDFAppPanelWidgets::findMainWindow(m_canvas) : nullptr)
        {
            window->openDocuments(QStringList{ outcome.filePath }, 0);
        }
    }
}

// ------------------------------------------------------------- flow

QVariantMap PDFAppToolGUICompare::getRunParameters() const
{
    using Helper = PDFAppToolGUICompareHelper;

    // The GUI runs the summary (the whole result goes by the collector); the exports are made from it later
    QVariantMap parameters = BaseClass::getRunParameters();
    parameters.insert(Helper::name(PDFDocumentToolCompare::PARAMETER_OUTPUT), Helper::name(PDFDocumentToolCompare::OUTPUT_SUMMARY));
    for (int slot = 0; slot < SlotCount; ++slot)
    {
        PDFDocumentToolPageSelection selection;
        selection.pages = m_rangeTexts[size_t(slot)];
        parameters.insert(Helper::name(slot == OriginalSlot ? PDFDocumentToolCompare::PARAMETER_ORIGINAL_PAGES : PDFDocumentToolCompare::PARAMETER_REVISED_PAGES), selection.toVariant());
    }
    return parameters;
}

void PDFAppToolGUICompare::prepareEnvironment(PDFAppToolRunEnvironment* environment) const
{
    // The snapshots of the slots (immutable shared pointers): the run and later the report use the same documents
    m_runInputs.clear();
    for (int slot = 0; slot < SlotCount; ++slot)
    {
        const PDFAppToolInput* input = getSlotInput(Slot(slot));
        if (input && isSlotComparable(Slot(slot)))
        {
            m_runInputs.push_back(input->createToolInputDocument());
        }
    }
    environment->additionalDocuments = m_runInputs;

    // RS7APP L3: the run tool with the collector of this run
    m_runCollector = std::make_shared<PDFDiffResultCollector>();
    environment->runTool = std::make_shared<PDFAppCompareTool>(m_runCollector);
}

void PDFAppToolGUICompare::onRunRefused()
{
    m_runCollector.reset();
    m_runInputs.clear();
}

bool PDFAppToolGUICompare::isResultStaleBy(const QString& parameterName) const
{
    // R7-2 / FC-4: the options and the page ranges change the comparison; the view and the filters only the showing
    const bool isOption = parameterName == QLatin1String(PDFDocumentToolCompare::PARAMETER_COMPARE_TEXT_AS_VECTOR) ||
                          parameterName == QLatin1String(PDFDocumentToolCompare::PARAMETER_COMPARE_CHARACTERS);
    const bool isRange = parameterName == QLatin1String(PDFDocumentToolCompare::PARAMETER_ORIGINAL_PAGES) ||
                         parameterName == QLatin1String(PDFDocumentToolCompare::PARAMETER_REVISED_PAGES);
    if (isOption)
    {
        m_staleReason = StaleReason::Options;
    }
    else if (isRange)
    {
        m_staleReason = StaleReason::Documents;
    }
    return isOption || isRange;
}

QString PDFAppToolGUICompare::getStaleTitle() const
{
    // 492:109399
    return m_staleReason == StaleReason::Options ? tr("Options changed") : tr("The documents changed");
}

QString PDFAppToolGUICompare::getStaleText() const
{
    return m_staleReason == StaleReason::Options ? tr("The shown differences come from the previous settings. Press Compare Again to refresh them.")
                                                 : tr("The shown differences come from the previous documents or pages. Press Compare Again to refresh them.");
}

PDFAppToolCalloutPlace PDFAppToolGUICompare::getStaleCalloutPlace() const
{
    // 492:109399: between Documents and Colors
    PDFAppToolCalloutPlace place;
    if (m_rightLayout && m_documentsSection)
    {
        place.layout = m_rightLayout.data();
        place.index = m_rightLayout->indexOf(m_documentsSection.data()) + 1;
    }
    return place;
}

void PDFAppToolGUICompare::onParametersChanged(const QString& name)
{
    using Helper = PDFAppToolGUICompareHelper;

    BaseClass::onParametersChanged(name);
    QPointer<PDFAppToolGUICompare> guard(this);
    if (name == QLatin1String(PDFDocumentToolCompare::PARAMETER_VIEW))
    {
        const PDFDiffReportView view = getView();
        if (PDFAppCompareView* compareView = getCompareView())
        {
            compareView->setView(view);
        }
        if (Helper::getDocumentView(view) != m_viewDocumentView || (!m_isViewDocumentPending && getCompareView() && !getCompareView()->getDocument()))
        {
            requestViewDocument();
        }
    }
    else if (name.startsWith(QLatin1String("include")))
    {
        updateShownResult();
    }
    else if (name == QLatin1String(PDFDocumentToolCompare::PARAMETER_ORIGINAL_PAGES) || name == QLatin1String(PDFDocumentToolCompare::PARAMETER_REVISED_PAGES))
    {
        // Undo / Redo of a range: the field follows the value
        const Slot slot = name == QLatin1String(PDFDocumentToolCompare::PARAMETER_ORIGINAL_PAGES) ? OriginalSlot : RevisedSlot;
        const std::optional<PDFDocumentToolPageSelection> selection = PDFDocumentToolPageSelection::fromVariant(getModel() ? getModel()->getValue(name) : QVariant(), nullptr);
        if (selection && !selection->pages.isEmpty() && selection->pages != m_rangeTexts[size_t(slot)])
        {
            m_rangeTexts[size_t(slot)] = selection->pages;
            m_rangeErrors[size_t(slot)].clear();
            setInputError(QStringLiteral("pages%1").arg(int(slot)), QString());
        }
    }
    if (guard)
    {
        updateAll();
    }
}

void PDFAppToolGUICompare::onRunStateChanged(bool isRunning)
{
    m_isRunning = isRunning;
    BaseClass::onRunStateChanged(isRunning);
    QPointer<PDFAppToolGUICompare> guard(this);
    if (isRunning && m_changePanel)
    {
        m_changePanel->close();
        if (!guard)
        {
            return;
        }
    }
    if (isRunning)
    {
        // 492:18869: the pages of the inputs, no differences until the end
        m_isStopped = false;
        m_failureText.clear();
        const bool hadResult = m_hasResult;
        clearResult();
        m_content = Content::Running;
        updateShownResult();
        if (!guard)
        {
            return;
        }
        if (hadResult)
        {
            requestViewDocument();
        }
    }
    else if (m_content == Content::Running)
    {
        // FC-1: Stop - the ready state, "The comparison was stopped" (a result or a failure comes after it)
        m_content = Content::Ready;
        m_isStopped = true;
    }
    if (guard)
    {
        updateAll();
    }
}

void PDFAppToolGUICompare::onRunNotAccepted(const PDFDocumentToolResult& result)
{
    // FC-2: a red callout in the canvas (the error of the core, the budget of the memory B191), Compare Again
    m_runCollector.reset();
    m_runInputs.clear();
    m_isStopped = false;
    m_content = Content::Failed;
    m_failureText = result.message;
    updateAll();
}

void PDFAppToolGUICompare::setContentEnabled(bool isEnabled)
{
    // The panels stay readable during the run; the slots and the actions are disabled by the updates
    BaseClass::setContentEnabled(isEnabled);
    if (m_canvas)
    {
        m_canvas->setEnabled(true);
    }
}

void PDFAppToolGUICompare::retranslate()
{
    // The canvas stays over a change of the language (the shell makes the panels again): its fixed texts, the cards of
    // the slots and the texts of the view follow
    BaseClass::retranslate();
    if (m_canvas)
    {
        m_canvas->retranslate();
    }
    QPointer<PDFAppToolGUICompare> guard(this);
    updateChoice();
    if (guard)
    {
        updateCanvas();
    }
}

// ------------------------------------------------------------- texts

QString PDFAppToolGUICompare::getSlotTitle(Slot slot) const
{
    return slot == OriginalSlot ? tr("Original") : tr("Revised");
}

QString PDFAppToolGUICompare::getInputName(Slot slot) const
{
    const PDFAppToolInput* input = getSlotInput(slot);
    return input ? input->name : QString();
}

QString PDFAppToolGUICompare::getConfirmText() const
{
    // K7.2 / Q7.17: Compare, after a result Compare Again (Disabled until the result is stale)
    return m_hasResult || getResultState() != PDFAppToolResultState::None ? tr("Compare Again") : tr("Compare");
}

QString PDFAppToolGUICompare::getConfirmIconName() const
{
    return QStringLiteral("compare");
}

bool PDFAppToolGUICompare::isConfirmEnabled() const
{
    return BaseClass::isConfirmEnabled() && !m_isRunning && isSlotComparable(OriginalSlot) && isSlotComparable(RevisedSlot) &&
           m_rangeErrors[0].isEmpty() && m_rangeErrors[1].isEmpty();
}

QString PDFAppToolGUICompare::getCancelText() const
{
    // The own tab: Close (492:18424)
    return tr("Close");
}

QString PDFAppToolGUICompare::getOwnTabIconName() const
{
    return QStringLiteral("compare");
}

QString PDFAppToolGUICompare::getOwnTabLossText() const
{
    // K16: the dot of the tab and the question at its close (the result is computed again, the choice is not); RESCUX
    // M5: the singular is a text of its own (the English .ts has no numerus forms)
    const PDFAppToolInputList* list = m_list ? m_list.data() : getInputList();
    const int count = list ? list->getCount() : 0;
    if (count <= 0 || !isChoiceChanged())
    {
        return QString();
    }
    return count == 1 ? tr("The chosen document is lost.") : tr("The %Ln chosen documents are lost.", nullptr, count);
}

bool PDFAppToolGUICompare::isChoiceChanged() const
{
    // Owner 2026-11-20: a document chosen by the user (Change…, a file added, one removed) is work; the documents the
    // tab came with and a pure change of their order (Swap) are not
    const PDFAppToolInputList* list = m_list ? m_list.data() : getInputList();
    QStringList ids = list ? list->getInputIds() : QStringList();
    ids.sort();
    return m_hasEntryChoice && ids != m_entryChoice;
}

PDFAppToolCloseQuestion PDFAppToolGUICompare::getOwnTabCloseQuestion(bool isTabClosing) const
{
    return PDFAppToolTabPage::createChoiceCloseQuestion(tr("Keep Comparing"), isTabClosing);
}

QString PDFAppToolGUICompare::getDifferencesSummary() const
{
    // "14 differences on 6 pages" (the shown differences, the pages of the original and of the revised one)
    // The pages are the pairs of pages with a difference (the groups of the list, 492:19314 "on 6 pages")
    const size_t count = m_shownResult.getDifferencesCount();
    std::vector<std::pair<PDFInteger, PDFInteger>> pairs = PDFAppCompareDifferenceModel::getPagePairs(m_shownResult);
    std::sort(pairs.begin(), pairs.end());
    pairs.erase(std::unique(pairs.begin(), pairs.end()), pairs.end());
    const size_t pairCount = pairs.size();
    const int countValue = int(qMin(count, size_t(std::numeric_limits<int>::max())));
    return tr("%Ln differences", nullptr, countValue) + QLatin1Char(' ') + tr("on %Ln pages", nullptr, int(qMin(pairCount, size_t(std::numeric_limits<int>::max()))));
}

QString PDFAppToolGUICompare::getFilteredSummary() const
{
    // 492:104504 "12 shown · 2 hidden by the filter"
    const size_t total = m_result.getDifferencesCount();
    const size_t shown = m_shownResult.getDifferencesCount();
    return tr("%1 shown · %2 hidden by the filter").arg(PDFAppLocale::formatNumber(qint64(shown)), PDFAppLocale::formatNumber(qint64(total - shown)));
}

QString PDFAppToolGUICompare::getPagesText() const
{
    return tr("%1 and %2 pages").arg(PDFAppLocale::formatNumber(qint64(getRangePages(OriginalSlot).size())), PDFAppLocale::formatNumber(qint64(getRangePages(RevisedSlot).size())));
}

QString PDFAppToolGUICompare::getStatusText() const
{
    using Helper = PDFAppToolGUICompareHelper;

    // An open menu of the toolbar says what it is (492:19823 … 492:22010)
    const QString menuText = getOpenMenuStatusText();
    if (!menuText.isEmpty())
    {
        return menuText;
    }

    // The status messages of the frames
    const bool isOriginal = isSlotComparable(OriginalSlot);
    const bool isRevised = isSlotComparable(RevisedSlot);
    for (int slot = 0; slot < SlotCount; ++slot)
    {
        const PDFAppToolInput* input = getSlotInput(Slot(slot));
        if (input && input->state == PDFAppToolInputState::PasswordRequired)
        {
            // 492:106538
            return tr("%1 is encrypted.").arg(input->name);
        }
        if (input && !isSlotComparable(Slot(slot)) && input->state != PDFAppToolInputState::Loading)
        {
            // 492:106266
            return tr("%1 cannot be read.").arg(input->name);
        }
    }
    if (!isOriginal && !isRevised)
    {
        return tr("Choose the Original and the Revised document.");
    }
    if (!isOriginal || !isRevised)
    {
        // 492:18158 "Original: Report_v1.pdf. Choose the Revised document."
        const Slot chosen = isOriginal ? OriginalSlot : RevisedSlot;
        const Slot missing = isOriginal ? RevisedSlot : OriginalSlot;
        return tr("%1: %2. Choose the %3 document.").arg(getSlotTitle(chosen), getInputName(chosen), getSlotTitle(missing));
    }
    for (int slot = 0; slot < SlotCount; ++slot)
    {
        if (!m_rangeErrors[size_t(slot)].isEmpty())
        {
            // 492:105816
            return tr("Fix the pages of the %1 to compare.").arg(getSlotTitle(Slot(slot)));
        }
    }
    if (getResultState() == PDFAppToolResultState::Stale)
    {
        // 492:109399
        return m_staleReason == StaleReason::Options ? tr("Options changed – compare again to refresh the differences.")
                                                     : tr("The documents changed – compare again to refresh the differences.");
    }

    switch (m_content)
    {
        case Content::Failed:
            return tr("The documents cannot be compared. %1").arg(m_failureText);
        case Content::NoDifferences:
            // 492:105498
            return tr("No differences found between the compared pages.");
        case Content::Result:
        {
            if (m_reportWatcher)
            {
                return tr("Creating the comparison report…");
            }
            if (m_shownResult.isSame())
            {
                return tr("All differences are hidden by the filter.");
            }
            if (getView() == PDFDiffReportView::Overlay)
            {
                // 493:18777, 493:19325
                if (m_overlay.scaleMode == PDFAppCompareOverlaySettings::ScaleMode::Manual || !m_overlay.revisedOffsetMM.isNull())
                {
                    const QLocale locale = PDFAppLocale::getUiLocale();
                    return tr("Overlay – %1, Revised shifted by %2 mm and %3 mm.").arg(m_overlay.scaleMode == PDFAppCompareOverlaySettings::ScaleMode::Manual ? tr("manual scale") : (m_overlay.scaleMode == PDFAppCompareOverlaySettings::ScaleMode::Fit ? tr("fit to the larger page") : tr("original sizes")),
                                                                                  locale.toString(m_overlay.revisedOffsetMM.x(), 'g', 4), locale.toString(m_overlay.revisedOffsetMM.y(), 'g', 4));
                }
                return tr("Overlay – both pages blended %1 % / %2 %. It applies to all paired pages; results and reports use the original documents.")
                    .arg(PDFAppLocale::formatNumber(100 - m_overlay.blend), PDFAppLocale::formatNumber(m_overlay.blend));
            }
            const size_t shown = m_shownResult.getDifferencesCount();
            const size_t total = m_result.getDifferencesCount();
            if (shown < total)
            {
                // 492:104504 "12 differences shown, 2 hidden by the filter. Only pages with differences."
                QString text = tr("%Ln differences shown", nullptr, Helper::toCount(shown)) + tr(", %Ln hidden by the filter.", nullptr, Helper::toCount(total - shown));
                if (m_isOnlyChangedPages)
                {
                    text += QLatin1Char(' ') + tr("Only pages with differences.");
                }
                return text;
            }

            // 492:19314 "14 differences found on 6 pages. F6 next difference, Shift+F6 previous."
            return tr("%Ln differences found", nullptr, Helper::toCount(shown)) + QLatin1Char(' ') + tr("on %Ln pages.", nullptr, Helper::toCount(Helper::getPagePairCount(m_shownResult))) + QLatin1Char(' ') +
                   tr("F6 next difference, Shift+F6 previous.");
        }
        case Content::Running:
            return tr("Comparing… Stop ends the comparison, no differences are shown.");
        case Content::Ready:
        case Content::Choice:
            break;
    }

    if (m_isStopped)
    {
        // FC-1
        return tr("The comparison was stopped. No differences are shown.");
    }
    // 492:18424 "Ready to compare 24 and 25 pages. F5 starts the comparison."
    return tr("Ready to compare %1. F5 starts the comparison.").arg(getPagesText());
}

QString PDFAppToolGUICompare::getOpenMenuStatusText() const
{
    using Helper = PDFAppToolGUICompareHelper;

    // The button of a menu is checked while its panel is open (PDFAppToolToolbar::openPanel)
    const std::array<std::pair<const char*, QString>, 5> menus = { std::make_pair(Helper::MENU_VIEW, tr("View menu.")),
                                                                   std::make_pair(Helper::MENU_FILTER, tr("Filter menu – choose the kinds of differences to show.")),
                                                                   std::make_pair(Helper::MENU_SHOW, tr("Show menu.")),
                                                                   std::make_pair(Helper::MENU_OPTIONS, tr("Options of the comparison.")),
                                                                   std::make_pair(Helper::MENU_EXPORT, tr("Export menu.")) };
    for (const auto& [menuId, text] : menus)
    {
        const PDFAppToolPanelPopup* panel = getMenuPanel(Helper::name(menuId));
        const QToolButton* button = panel ? panel->getButton() : nullptr;
        if (button && button->isChecked())
        {
            return text;
        }
    }
    return QString();
}

void PDFAppToolGUICompare::onMenuToggled()
{
    // The toolbar checks the button inside of its opening of the panel: the status bar, whose slots may delete the
    // window, follows from the event loop (once for more toggles)
    if (!m_isMenuStatusQueued)
    {
        m_isMenuStatusQueued = true;
        QMetaObject::invokeMethod(this, &PDFAppToolGUICompare::onMenuToggledQueued, Qt::QueuedConnection);
    }
}

void PDFAppToolGUICompare::onMenuToggledQueued()
{
    m_isMenuStatusQueued = false;
    updateStatusText();
}

QString PDFAppToolGUICompare::getRunningStatusText(const QString& stepText) const
{
    // 492:18869
    Q_UNUSED(stepText);
    return tr("Comparing… Stop ends the comparison, no differences are shown.");
}

QString PDFAppToolGUICompare::getRunningCardTitle(int percentage, const QString& stepText) const
{
    // 492:18869 "Comparing documents"
    Q_UNUSED(percentage);
    Q_UNUSED(stepText);
    return tr("Comparing documents");
}

QString PDFAppToolGUICompare::getRunningCardStepText(const QString& stepText) const
{
    // "Page 9 of 25 · text, graphics, images and shading" - the step of the core
    return stepText;
}

QString PDFAppToolGUICompare::getOwnTabPageText() const
{
    // 492:19314 "Page 3 of 25": the page in the view of its own document; no pages before both inputs (492:18158)
    const PDFAppCompareView* view = getCompareView();
    if (!view || !view->getDocument() || !isSlotComparable(OriginalSlot) || !isSlotComparable(RevisedSlot))
    {
        return tr("No pages");
    }

    const PDFInteger page = view->getCurrentPageIndex();
    const PDFInteger revised = view->getLayout().getRevisedPageIndex(page);
    const PDFInteger original = view->getLayout().getOriginalPageIndex(page);
    const PDFAppToolInput* revisedInput = getSlotInput(RevisedSlot);
    const PDFAppToolInput* originalInput = getSlotInput(OriginalSlot);
    if (revised >= 0)
    {
        return tr("Page %1 of %2").arg(PDFAppLocale::formatNumber(revised + 1), PDFAppLocale::formatNumber(revisedInput->pageCount));
    }
    if (original >= 0)
    {
        return tr("Page %1 of %2").arg(PDFAppLocale::formatNumber(original + 1), PDFAppLocale::formatNumber(originalInput->pageCount));
    }
    return tr("Page %1 of %2").arg(PDFAppLocale::formatNumber(1), PDFAppLocale::formatNumber(revisedInput->pageCount));
}

QString PDFAppToolGUICompare::getOwnTabZoomText() const
{
    // "151 %": the zoom of the view
    const PDFAppCompareView* view = getCompareView();
    return view ? tr("%1 %").arg(PDFAppLocale::formatNumber(qRound64(view->getZoom() * 100.0))) : QString();
}

QWidget* PDFAppToolGUICompare::getDialogParent() const
{
    if (m_canvas)
    {
        return m_canvas->window();
    }
    return getHost() && getHost()->getKeyWidget() ? getHost()->getKeyWidget()->window() : nullptr;
}

PDFAppCompareView* PDFAppToolGUICompare::getCompareView() const
{
    return m_canvas ? m_canvas->getView() : nullptr;
}

bool PDFAppToolGUICompare::isViewDocumentPending() const
{
    return m_isViewDocumentPending;
}

// ------------------------------------------------------------- the parts

void PDFAppToolGUICompare::readViewSettings()
{
    using Helper = PDFAppToolGUICompareHelper;

    if (m_areViewSettingsRead)
    {
        return;
    }
    m_areViewSettingsRead = true;

    // The last values of the view settings (a hostile or foreign stored value gives the default, the repository
    // validates it by the definition)
    const PDFDocumentToolParameterDefinitions definitions = getViewSettingDefinitions();
    QVariantMap values = PDFDocumentToolParameters::getDefaultValues(definitions);
    if (getContext() && getContext()->getDatabase())
    {
        values = PDFToolParameterRepository(getContext()->getDatabase()).getValues(QString::fromLatin1(PDFDocumentToolCompare::TOOL_ID), definitions);
    }
    PDFDiffReportColors colors;
    colors.removed = values.value(Helper::name(SETTING_COLOR_REMOVED)).value<QColor>();
    colors.added = values.value(Helper::name(SETTING_COLOR_ADDED)).value<QColor>();
    colors.replaced = values.value(Helper::name(SETTING_COLOR_REPLACED)).value<QColor>();
    colors.moved = values.value(Helper::name(SETTING_COLOR_MOVED)).value<QColor>();
    const PDFDiffReportColors defaults;
    m_colors.removed = colors.removed.isValid() ? colors.removed : defaults.removed;
    m_colors.added = colors.added.isValid() ? colors.added : defaults.added;
    m_colors.replaced = colors.replaced.isValid() ? colors.replaced : defaults.replaced;
    m_colors.moved = colors.moved.isValid() ? colors.moved : defaults.moved;
    m_isHighlightsShown = values.value(Helper::name(SETTING_HIGHLIGHTS), true).toBool();
    m_isMarkersShown = values.value(Helper::name(SETTING_MARKERS), true).toBool();
    m_isOnlyChangedPages = values.value(Helper::name(SETTING_ONLY_CHANGED), false).toBool();
    m_isSelectionFollowed = values.value(Helper::name(SETTING_FOLLOW), true).toBool();
}

void PDFAppToolGUICompare::storeViewSetting(const QString& name, const QVariant& value)
{
    if (!getContext() || !getContext()->getDatabase())
    {
        return;
    }
    const PDFDocumentToolParameterDefinitions definitions = getViewSettingDefinitions();
    if (const PDFDocumentToolParameterDefinition* definition = PDFDocumentToolParameters::find(definitions, name))
    {
        const PDFDatabaseError error = PDFToolParameterRepository(getContext()->getDatabase()).setValue(QString::fromLatin1(PDFDocumentToolCompare::TOOL_ID), *definition, value);
        if (error.isError())
        {
            qWarning("The view setting of Compare cannot be stored: %s", qPrintable(error.toString()));
        }
    }
}

void PDFAppToolGUICompare::describeToolbar(PDFAppToolPanelBuilder& builder)
{
    connectList();
    readViewSettings();
    createMenus(builder);
    createKeyActions();
}

void PDFAppToolGUICompare::createMenus(PDFAppToolPanelBuilder& builder)
{
    using Helper = PDFAppToolGUICompareHelper;

    m_menus.clear();
    m_menuRows.clear();

    // tb-view 492:19943 with panel-view 492:20335
    PDFAppToolPanelPopup* viewPanel = builder.addMenu(Helper::name(Helper::MENU_VIEW), Helper::getViewIcon(getView()), Helper::getViewTitle(getView()), Helper::VIEW_PANEL_WIDTH);
    m_menus[Helper::name(Helper::MENU_VIEW)] = viewPanel;
    for (PDFDiffReportView view : { PDFDiffReportView::SideBySide, PDFDiffReportView::Original, PDFDiffReportView::Revised, PDFDiffReportView::Overlay })
    {
        PDFTileRow* row = viewPanel->addRow(Helper::getViewTitle(view), Helper::getViewDescription(view), Helper::getViewIcon(view), PDFTileRow::Kind::MenuItem);
        const QString id = Helper::getViewId(view);
        row->setObjectName(QStringLiteral("compareView-") + id);
        row->setProperty(Helper::ROW_KEY, id);
        row->setSelectionBehavior(PDFComponentButton::SelectionBehavior::Exclusive);
        connect(row, &PDFTileRow::clicked, this, &PDFAppToolGUICompare::onViewRowClicked);
        m_menuRows[QStringLiteral("view-") + id] = row;
    }

    // tb-filter with 492:20366: "SHOW DIFFERENCES IN", the five kinds with their counts
    PDFAppToolPanelPopup* filterPanel = builder.addMenu(Helper::name(Helper::MENU_FILTER), QStringLiteral("filter"), tr("Filter"), Helper::FILTER_PANEL_WIDTH);
    m_menus[Helper::name(Helper::MENU_FILTER)] = filterPanel;
    filterPanel->addGroupCaption(tr("SHOW DIFFERENCES IN"));
    struct FilterRow
    {
        const char* parameter;
        QString title;
        QString iconName;
    };
    const std::array<FilterRow, 5> filterRows = { FilterRow{ PDFDocumentToolCompare::PARAMETER_INCLUDE_TEXT, tr("Text"), QStringLiteral("type") },
                                                  FilterRow{ PDFDocumentToolCompare::PARAMETER_INCLUDE_VECTOR_GRAPHICS, tr("Vector Graphics"), QStringLiteral("vector-shape") },
                                                  FilterRow{ PDFDocumentToolCompare::PARAMETER_INCLUDE_IMAGES, tr("Images"), QStringLiteral("image") },
                                                  FilterRow{ PDFDocumentToolCompare::PARAMETER_INCLUDE_SHADING, tr("Shading"), QStringLiteral("gradient") },
                                                  FilterRow{ PDFDocumentToolCompare::PARAMETER_INCLUDE_PAGE_MOVEMENT, tr("Page Movement"), QStringLiteral("page-moved") } };
    for (const FilterRow& filterRow : filterRows)
    {
        PDFTileRow* row = filterPanel->addRow(filterRow.title, QString(), filterRow.iconName, PDFTileRow::Kind::MenuItem);
        row->setObjectName(QStringLiteral("compareFilter-") + Helper::name(filterRow.parameter));
        row->setProperty(Helper::ROW_KEY, Helper::name(filterRow.parameter));
        row->setSelectionBehavior(PDFComponentButton::SelectionBehavior::Toggle);
        connect(row, &PDFTileRow::clicked, this, &PDFAppToolGUICompare::onFilterRowClicked);
        m_menuRows[QStringLiteral("filter-") + Helper::name(filterRow.parameter)] = row;
    }
    filterPanel->addGroupDivider();
    filterPanel->addGroupCaption(tr("A kind without any difference is unavailable."));

    // tb-show with 492:20930
    PDFAppToolPanelPopup* showPanel = builder.addMenu(Helper::name(Helper::MENU_SHOW), QStringLiteral("eye"), tr("Show"), Helper::SHOW_PANEL_WIDTH);
    m_menus[Helper::name(Helper::MENU_SHOW)] = showPanel;
    struct ShowRow
    {
        const char* setting;
        QString title;
        QString description;
        QString iconName;
        bool isDividerBefore;
    };
    const std::array<ShowRow, 4> showRows = { ShowRow{ SETTING_HIGHLIGHTS, tr("Difference Highlights"), tr("Colored rectangles over the differences"), QStringLiteral("highlighter"), false },
                                              ShowRow{ SETTING_MARKERS, tr("Markers at the Edge"), tr("Arrows at the edge of the view that point to each difference"), QStringLiteral("diff-markers"), false },
                                              ShowRow{ SETTING_ONLY_CHANGED, tr("Only Pages with Differences"), tr("Hide the pages that are the same"), QStringLiteral("diff-pages"), true },
                                              ShowRow{ SETTING_FOLLOW, tr("Follow the Selected Difference"), tr("Selecting a difference scrolls the view to it"), QStringLiteral("sync-view"), false } };
    for (const ShowRow& showRow : showRows)
    {
        if (showRow.isDividerBefore)
        {
            showPanel->addGroupDivider();
        }
        PDFTileRow* row = showPanel->addRow(showRow.title, showRow.description, showRow.iconName, PDFTileRow::Kind::MenuItem);
        row->setObjectName(QStringLiteral("compareShow-") + Helper::name(showRow.setting));
        row->setProperty(Helper::ROW_KEY, Helper::name(showRow.setting));
        row->setSelectionBehavior(PDFComponentButton::SelectionBehavior::Toggle);
        connect(row, &PDFTileRow::clicked, this, &PDFAppToolGUICompare::onShowRowClicked);
        m_menuRows[QStringLiteral("show-") + Helper::name(showRow.setting)] = row;
    }

    // tb-options with 492:21479 ("A changed option takes effect after Compare Again.")
    PDFAppToolPanelPopup* optionsPanel = builder.addMenu(Helper::name(Helper::MENU_OPTIONS), QStringLiteral("sliders"), tr("Options"), Helper::OPTIONS_PANEL_WIDTH);
    m_menus[Helper::name(Helper::MENU_OPTIONS)] = optionsPanel;
    const std::array<ShowRow, 2> optionRows = { ShowRow{ PDFDocumentToolCompare::PARAMETER_COMPARE_TEXT_AS_VECTOR, tr("Compare Text as Vector Graphics"), tr("Text is compared by its shape, not by its characters"), QStringLiteral("text-as-vector"), false },
                                                ShowRow{ PDFDocumentToolCompare::PARAMETER_COMPARE_CHARACTERS, tr("Compare Characters Instead of Words"), tr("Finer text differences, character by character"), QStringLiteral("chars"), false } };
    for (const ShowRow& optionRow : optionRows)
    {
        PDFTileRow* row = optionsPanel->addRow(optionRow.title, optionRow.description, optionRow.iconName, PDFTileRow::Kind::MenuItem);
        row->setObjectName(QStringLiteral("compareOption-") + Helper::name(optionRow.setting));
        row->setProperty(Helper::ROW_KEY, Helper::name(optionRow.setting));
        row->setSelectionBehavior(PDFComponentButton::SelectionBehavior::Toggle);
        connect(row, &PDFTileRow::clicked, this, &PDFAppToolGUICompare::onOptionRowClicked);
        m_menuRows[QStringLiteral("option-") + Helper::name(optionRow.setting)] = row;
    }
    optionsPanel->addGroupDivider();
    optionsPanel->addGroupCaption(tr("A changed option takes effect after Compare Again."));

    // tb-prev, tb-next (Shift+F6, F6)
    builder.addActionDivider();
    m_previousButton = builder.addAction(QString(), QStringLiteral("diff-prev"), this, &PDFAppToolGUICompare::onPreviousClicked);
    m_previousButton->setObjectName(QStringLiteral("comparePrevious"));
    m_previousButton->setToolTip(tr("Previous Difference (Shift+F6)"));
    m_previousButton->setAccessibleName(tr("Previous Difference"));
    m_nextButton = builder.addAction(QString(), QStringLiteral("diff-next"), this, &PDFAppToolGUICompare::onNextClicked);
    m_nextButton->setObjectName(QStringLiteral("compareNext"));
    m_nextButton->setToolTip(tr("Next Difference (F6)"));
    m_nextButton->setAccessibleName(tr("Next Difference"));

    // tb-export with 492:22010
    builder.addActionDivider();
    PDFAppToolPanelPopup* exportPanel = builder.addMenu(Helper::name(Helper::MENU_EXPORT), QStringLiteral("download"), tr("Export"), Helper::EXPORT_PANEL_WIDTH);
    m_menus[Helper::name(Helper::MENU_EXPORT)] = exportPanel;
    PDFTileRow* reportRow = exportPanel->addRow(tr("Report as PDF…"), tr("The pages as shown now, differences marked as annotations"), QStringLiteral("report"), PDFTileRow::Kind::MenuItem);
    reportRow->setObjectName(QStringLiteral("compareExportReport"));
    connect(reportRow, &PDFTileRow::clicked, this, &PDFAppToolGUICompare::onExportReportClicked);
    m_menuRows[Helper::name(Helper::EXPORT_REPORT)] = reportRow;
    PDFTileRow* xmlRow = exportPanel->addRow(tr("Differences as XML…"), tr("Machine-readable list of the shown differences"), QStringLiteral("xml"), PDFTileRow::Kind::MenuItem);
    xmlRow->setObjectName(QStringLiteral("compareExportXml"));
    connect(xmlRow, &PDFTileRow::clicked, this, &PDFAppToolGUICompare::onExportXmlClicked);
    m_menuRows[Helper::name(Helper::EXPORT_XML)] = xmlRow;

    // The status bar names the open menu (492:19859, 492:20402, 492:20966, 492:21515, 492:22046)
    for (const auto& item : m_menus)
    {
        if (QToolButton* button = item.second ? item.second->getButton() : nullptr)
        {
            connect(button, &QAbstractButton::toggled, this, &PDFAppToolGUICompare::onMenuToggled);
        }
    }

    updateMenus();
}

void PDFAppToolGUICompare::createKeyActions()
{
    // F5, F6, Shift+F6 on the key widget of the host: they reach every part of the tab (the old Diff); a drop of files
    // anywhere in the tab goes to a slot
    PDFAppToolHost* host = getHost();
    if (!host || !host->getKeyWidget() || !m_keyActions.empty())
    {
        return;
    }

    m_keyWidget = host->getKeyWidget();
    struct KeyAction
    {
        QKeySequence key;
        void (PDFAppToolGUICompare::*slot)();
        const char* name;
    };
    const std::array<KeyAction, 3> actions = { KeyAction{ QKeySequence(Qt::Key_F5), &PDFAppToolGUICompare::onConfirmKey, "compareStartAction" },
                                               KeyAction{ QKeySequence(Qt::Key_F6), &PDFAppToolGUICompare::onNextClicked, "compareNextAction" },
                                               KeyAction{ QKeySequence(Qt::SHIFT | Qt::Key_F6), &PDFAppToolGUICompare::onPreviousClicked, "comparePreviousAction" } };
    for (const KeyAction& keyAction : actions)
    {
        QAction* action = new QAction(m_keyWidget);
        action->setObjectName(QString::fromLatin1(keyAction.name));
        action->setShortcut(keyAction.key);
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        connect(action, &QAction::triggered, this, keyAction.slot);
        m_keyWidget->addAction(action);
        m_keyActions.push_back(action);
    }
    m_wasDropAccepted = m_keyWidget->acceptDrops();
    m_keyWidget->setAcceptDrops(true);
    m_keyWidget->installEventFilter(this);
}

void PDFAppToolGUICompare::describeLeftPanel(PDFAppToolPanelBuilder& builder)
{
    connectList();
    readViewSettings();

    // sidebar-left 492:19850: "Differences" / "14 differences on 6 pages", the list or its empty state
    m_differencesHeader = builder.addPanelHeader(tr("Differences"), tr("Not compared yet"));
    m_differencesHeader->setObjectName(QStringLiteral("compareDifferencesHeader"));
    m_differencesStack = new QStackedWidget(builder.getWidget());

    m_differenceView = new PDFTreeView(m_differencesStack);
    m_differenceView->setObjectName(QStringLiteral("compareDifferences"));
    PDFAppPanelWidgets::prepareView(m_differenceView);
    m_differenceView->setHeaderHidden(true);
    m_differenceView->setRootIsDecorated(false);
    m_differenceView->setIndentation(0);
    m_differenceView->setItemsExpandable(false);
    m_differenceView->setUniformRowHeights(false);
    m_differenceView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_differenceView->setMouseTracking(true);
    m_differenceView->setAccessibleName(tr("Differences"));
    m_differenceModel = new PDFAppCompareDifferenceModel(m_differenceView);
    m_differenceDelegate = new PDFAppCompareDifferenceDelegate(m_differenceView);
    m_differenceDelegate->setColors(m_colors);
    m_differenceView->setModel(m_differenceModel);
    m_differenceView->setItemDelegate(m_differenceDelegate);
    connect(m_differenceView->selectionModel(), &QItemSelectionModel::currentChanged, this, &PDFAppToolGUICompare::onDifferenceSelectionChanged);
    m_differencesStack->addWidget(m_differenceView);

    // 492:18424: the icon and the text of the state
    QWidget* emptyPage = new QWidget(m_differencesStack);
    QVBoxLayout* emptyLayout = PDFAppPageWidgets::createColumn(emptyPage, 8);
    emptyLayout->setContentsMargins(0, 24, 0, 0);
    m_emptyBadge = new PDFIconBadge(QStringLiteral("compare"), 24, 20, emptyPage);
    m_emptyBadge->setColors(std::nullopt, PDFThemeColors::IconDefault);
    emptyLayout->addWidget(m_emptyBadge, 0, Qt::AlignHCenter);
    m_emptyText = PDFAppPageWidgets::createText(QString(), PDFThemeFonts::getFont(QFont::Normal, 12), 15, PDFThemeColors::TextSecondary, emptyPage);
    m_emptyText->setObjectName(QStringLiteral("compareDifferencesEmpty"));
    m_emptyText->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(m_emptyText);
    emptyLayout->addStretch(1);
    m_differencesStack->addWidget(emptyPage);
    builder.addWidget(m_differencesStack);
    builder.setFillsPanel(m_differencesStack);

    QPointer<PDFAppToolGUICompare> guard(this);
    m_differenceModel->setResult(m_shownResult);
    if (guard)
    {
        updateDifferences();
    }
}

void PDFAppToolGUICompare::describeRightPanel(PDFAppToolPanelBuilder& builder)
{
    using Helper = PDFAppToolGUICompareHelper;

    connectList();
    readViewSettings();
    m_rightPanel = builder.getWidget();
    m_rightLayout = qobject_cast<QVBoxLayout*>(builder.getWidget()->layout());

    // 492:18158: "Two documents are compared"
    m_infoCallout = builder.addCallout(PDFAccentHue::Blue, QStringLiteral("info"), tr("Two documents are compared"),
                                       tr("Original is the older version, Revised the newer one. Both can be files or open tabs and you can swap them later."));
    m_infoCallout->setObjectName(QStringLiteral("compareInfo"));
    m_infoCallout->setCompactStyle();

    // section-Documents 492:20078
    m_documentsSection = new QWidget(builder.getWidget());
    m_documentsSection->setObjectName(QStringLiteral("compareDocuments"));
    QVBoxLayout* documentsLayout = PDFAppPageWidgets::createColumn(m_documentsSection, PDFAppToolPanelBuilder::SECTION_GAP);
    documentsLayout->addWidget(new PDFAppToolSectionHeader(tr("Documents"), QString(), m_documentsSection));
    PDFAppToolCard* card = new PDFAppToolCard(m_documentsSection);
    card->getBodyLayout()->setContentsMargins(PDFAppToolCard::PADDING_H, PDFAppToolCard::PADDING_H, PDFAppToolCard::PADDING_H, PDFAppToolCard::PADDING_H);
    card->getBodyLayout()->setSpacing(10);
    for (int slot = 0; slot < SlotCount; ++slot)
    {
        // item-Original: the tile with icon/file, "Original" / the name, Change…
        PDFTileRow* row = new PDFTileRow(getSlotTitle(Slot(slot)), QString(), QStringLiteral("file"), PDFTileRow::Kind::ListItem, card);
        row->setObjectName(QStringLiteral("compareSlotRow%1").arg(slot));
        row->setDescriptionKind(PDFTileRow::TextKind::Data);

        // icon-tile 492:20086: bg/accent-subtle with icon/file in the accent color
        row->setTone(PDFTileRow::Tone::Accent);
        row->setLinkText(tr("Change…"));
        row->setClickable(false);
        connect(row, &PDFTileRow::linkClicked, this, slot == OriginalSlot ? &PDFAppToolGUICompare::onChangeOriginal : &PDFAppToolGUICompare::onChangeRevised);
        card->getBodyLayout()->addWidget(row);
        m_slotRows[size_t(slot)] = row;

        // pages: "Pages" [1-24] "of 24"
        QWidget* pagesRow = new QWidget(card);
        QHBoxLayout* pagesLayout = new QHBoxLayout(pagesRow);
        pagesLayout->setContentsMargins(10, 0, 10, 0);
        pagesLayout->setSpacing(10);
        pagesLayout->addWidget(PDFAppPageWidgets::createLabel(tr("Pages"), QFont::Normal, 12, PDFThemeColors::TextSecondary, pagesRow));
        PDFLineEdit* field = new PDFLineEdit(pagesRow);
        field->setObjectName(QStringLiteral("compareRange%1").arg(slot));
        field->setFixedWidth(Helper::RANGE_FIELD_WIDTH);
        field->setAccessibleName(tr("Pages of the %1").arg(getSlotTitle(Slot(slot))));
        connect(field, &QLineEdit::editingFinished, this, slot == OriginalSlot ? &PDFAppToolGUICompare::onRangeEditedOriginal : &PDFAppToolGUICompare::onRangeEditedRevised);
        pagesLayout->addWidget(field, 1);
        m_rangeFields[size_t(slot)] = field;
        m_pageCountLabels[size_t(slot)] = PDFAppPageWidgets::createLabel(QString(), QFont::Normal, 12, PDFThemeColors::TextSecondary, pagesRow);
        pagesLayout->addWidget(m_pageCountLabels[size_t(slot)]);
        card->getBodyLayout()->addWidget(pagesRow);

        // 492:105816: the error below the field, Regular 11 text/danger
        m_rangeErrorLabels[size_t(slot)] = PDFAppPageWidgets::createLabel(QString(), QFont::Normal, 11, PDFThemeColors::TextDanger, card);
        m_rangeErrorLabels[size_t(slot)]->setObjectName(QStringLiteral("compareRangeError%1").arg(slot));
        m_rangeErrorLabels[size_t(slot)]->setWordWrap(true);
        m_rangeErrorLabels[size_t(slot)]->setContentsMargins(10, 0, 10, 0);
        card->getBodyLayout()->addWidget(m_rangeErrorLabels[size_t(slot)]);

        if (slot == OriginalSlot)
        {
            // swap-row: Swap
            m_swapButton = PDFAppPageWidgets::createButton(tr("Swap"), QStringLiteral("swap"), false, card);
            m_swapButton->setObjectName(QStringLiteral("compareSwap"));
            connect(m_swapButton.data(), &QPushButton::clicked, this, &PDFAppToolGUICompare::swapSlots);
            card->getBodyLayout()->addWidget(m_swapButton, 0, Qt::AlignHCenter);
        }
    }
    documentsLayout->addWidget(card);
    builder.addWidget(m_documentsSection);

    // section-Overlay 493:18777: the header with Reset, the blend, Page sizes, the scales (Manual), the shifts
    m_overlaySection = new QWidget(builder.getWidget());
    m_overlaySection->setObjectName(QStringLiteral("compareOverlay"));
    QVBoxLayout* overlayLayout = PDFAppPageWidgets::createColumn(m_overlaySection, PDFAppToolPanelBuilder::SECTION_GAP);
    PDFAppToolSectionHeader* overlayHeader = new PDFAppToolSectionHeader(tr("Overlay"), QString(), m_overlaySection);
    PDFTextLink* reset = new PDFTextLink(tr("Reset"), overlayHeader);
    reset->setObjectName(QStringLiteral("compareOverlayReset"));
    connect(reset, &QAbstractButton::clicked, this, &PDFAppToolGUICompare::resetOverlay);
    overlayHeader->addTrailingWidget(reset);
    m_overlayReset = reset;
    overlayLayout->addWidget(overlayHeader);
    PDFAppToolCard* overlayCard = new PDFAppToolCard(m_overlaySection);
    overlayCard->setFieldSpacing();
    QWidget* blendHead = new QWidget(overlayCard);
    QHBoxLayout* blendHeadLayout = new QHBoxLayout(blendHead);
    blendHeadLayout->setContentsMargins(0, 0, 0, 0);
    blendHeadLayout->addWidget(PDFAppPageWidgets::createLabel(tr("Original"), QFont::Normal, 11, PDFThemeColors::TextSecondary, blendHead));
    blendHeadLayout->addStretch(1);
    m_blendLabel = PDFAppPageWidgets::createLabel(QString(), QFont::Medium, 11, PDFThemeColors::TextPrimary, blendHead);
    m_blendLabel->setObjectName(QStringLiteral("compareBlendLabel"));
    blendHeadLayout->addWidget(m_blendLabel);
    blendHeadLayout->addStretch(1);
    blendHeadLayout->addWidget(PDFAppPageWidgets::createLabel(tr("Revised"), QFont::Normal, 11, PDFThemeColors::TextSecondary, blendHead));
    overlayCard->getBodyLayout()->addWidget(blendHead);
    m_blendSlider = new QSlider(Qt::Horizontal, overlayCard);
    m_blendSlider->setObjectName(QStringLiteral("compareBlend"));
    m_blendSlider->setRange(0, 100);
    m_blendSlider->setAccessibleName(tr("Blend of the Original and the Revised"));
    connect(m_blendSlider.data(), &QSlider::valueChanged, this, &PDFAppToolGUICompare::onBlendChanged);
    overlayCard->getBodyLayout()->addWidget(m_blendSlider);

    // K7.4: the combo of the three modes (combo-page-sizes 493:19641, 170 wide); its list is panel-page-sizes 493:19892:
    // rows of 320 with the icon tile, the title, the description and the check mark of the chosen mode - the rows of
    // the Fluent delegate of the tile rows (MenuItem, the chosen item by Qt::CheckStateRole)
    struct PageSizesItem
    {
        PDFAppCompareOverlaySettings::ScaleMode mode;
        QString title;
        QString description;
        QString iconName;
    };
    const std::array<PageSizesItem, 3> pageSizesItems = { PageSizesItem{ PDFAppCompareOverlaySettings::ScaleMode::Original, tr("Original sizes"), tr("Pages keep their sizes, aligned at the top"), QStringLiteral("page-same") },
                                                          PageSizesItem{ PDFAppCompareOverlaySettings::ScaleMode::Fit, tr("Fit smaller page to larger"), tr("The smaller page is enlarged proportionally, both centered"), QStringLiteral("fit-image") },
                                                          PageSizesItem{ PDFAppCompareOverlaySettings::ScaleMode::Manual, tr("Manual scale"), tr("Each document gets its own percentage"), QStringLiteral("scale") } };
    m_pageSizesCombo = new PDFComboBox(overlayCard);
    m_pageSizesCombo->setObjectName(QStringLiteral("comparePageSizes"));
    PDFTileRowDelegate* pageSizesDelegate = new PDFTileRowDelegate(m_pageSizesCombo);
    pageSizesDelegate->setKind(PDFTileRowDelegate::Kind::MenuItem);
    m_pageSizesCombo->setItemDelegate(pageSizesDelegate);
    for (const PageSizesItem& item : pageSizesItems)
    {
        const int index = m_pageSizesCombo->count();
        m_pageSizesCombo->addItem(item.title, int(item.mode));
        m_pageSizesCombo->setItemData(index, item.description, PDFComponentRoles::SubtitleRole);
        m_pageSizesCombo->setItemData(index, item.iconName, PDFComponentRoles::IconNameRole);
        m_pageSizesCombo->setItemData(index, item.description, Qt::ToolTipRole);
        m_pageSizesCombo->setItemData(index, Qt::Unchecked, Qt::CheckStateRole);
    }
    m_pageSizesCombo->view()->setMinimumWidth(Helper::PAGE_SIZES_LIST_WIDTH);
    connect(m_pageSizesCombo.data(), &QComboBox::currentIndexChanged, this, &PDFAppToolGUICompare::onPageSizesChanged);
    QWidget* pageSizesRow = PDFAppToolCard::createFieldRow(tr("Page sizes"), m_pageSizesCombo, overlayCard);
    m_pageSizesCombo->setFixedWidth(Helper::PAGE_SIZES_FIELD_WIDTH);
    overlayCard->getBodyLayout()->addWidget(pageSizesRow);
    for (int slot = 0; slot < SlotCount; ++slot)
    {
        PDFDoubleSpinBox* scale = new PDFDoubleSpinBox(overlayCard);
        scale->setObjectName(QStringLiteral("compareScale%1").arg(slot));
        scale->setRange(PDFAppCompareOverlaySettings::MINIMUM_SCALE, PDFAppCompareOverlaySettings::MAXIMUM_SCALE);
        scale->setDecimals(2);
        scale->setSuffix(tr(" %"));
        connect(scale, &QDoubleSpinBox::valueChanged, this, &PDFAppToolGUICompare::onOverlayValueChanged);
        m_scaleSpinBoxes[size_t(slot)] = scale;
        m_scaleRows[size_t(slot)] = PDFAppToolCard::createFieldRow(slot == OriginalSlot ? tr("Original scale") : tr("Revised scale"), scale, overlayCard);
        scale->setFixedWidth(Helper::OVERLAY_SPIN_BOX_WIDTH);
        overlayCard->getBodyLayout()->addWidget(m_scaleRows[size_t(slot)]);
    }
    for (int axis = 0; axis < 2; ++axis)
    {
        PDFDoubleSpinBox* offset = new PDFDoubleSpinBox(overlayCard);
        offset->setObjectName(axis == 0 ? QStringLiteral("compareOffsetX") : QStringLiteral("compareOffsetY"));
        offset->setRange(-PDFAppCompareOverlaySettings::MAXIMUM_OFFSET, PDFAppCompareOverlaySettings::MAXIMUM_OFFSET);
        offset->setDecimals(2);
        offset->setSingleStep(0.5);
        offset->setSuffix(tr(" mm"));
        connect(offset, &QDoubleSpinBox::valueChanged, this, &PDFAppToolGUICompare::onOverlayValueChanged);
        m_offsetSpinBoxes[size_t(axis)] = offset;
        QWidget* offsetRow = PDFAppToolCard::createFieldRow(axis == 0 ? tr("Horizontal shift") : tr("Vertical shift"), offset, overlayCard);
        offset->setFixedWidth(Helper::OVERLAY_SPIN_BOX_WIDTH);
        overlayCard->getBodyLayout()->addWidget(offsetRow);
    }
    overlayLayout->addWidget(overlayCard);
    builder.addWidget(m_overlaySection);

    // section-Colors 492:20123
    m_colorsSection = new QWidget(builder.getWidget());
    m_colorsSection->setObjectName(QStringLiteral("compareColors"));
    QVBoxLayout* colorsLayout = PDFAppPageWidgets::createColumn(m_colorsSection, PDFAppToolPanelBuilder::SECTION_GAP);
    colorsLayout->addWidget(new PDFAppToolSectionHeader(tr("Colors"), QString(), m_colorsSection));
    PDFAppToolCard* colorsCard = new PDFAppToolCard(m_colorsSection);
    colorsCard->getBodyLayout()->setContentsMargins(PDFAppToolCard::PADDING_H, PDFAppToolCard::PADDING_H, PDFAppToolCard::PADDING_H, PDFAppToolCard::PADDING_H);
    colorsCard->getBodyLayout()->setSpacing(6);
    const PDFDiffReportColors defaults;
    const std::array<QString, ColorKindCount> colorNames = { tr("Removed"), tr("Added"), tr("Replaced"), tr("Moved") };
    const std::array<int, ColorKindCount> defaultSlots = { 0, 2, 1, 3 };
    for (int kind = 0; kind < ColorKindCount; ++kind)
    {
        PDFAppCompareColorRow* row = new PDFAppCompareColorRow(colorNames[size_t(kind)], getColor(defaults, ColorKind(kind)), defaultSlots[size_t(kind)], colorsCard);
        row->setObjectName(QStringLiteral("compareColor%1").arg(kind));
        row->setProperty(Helper::ROW_KEY, kind);
        row->setColor(getColor(m_colors, ColorKind(kind)));
        connect(row, &PDFAppCompareColorRow::colorChosen, this, &PDFAppToolGUICompare::onColorChosen);
        colorsCard->getBodyLayout()->addWidget(row);
        m_colorRows[size_t(kind)] = row;
    }
    colorsLayout->addWidget(colorsCard);
    builder.addWidget(m_colorsSection);

    updateSlots();
    updateOverlayControls();
}

QWidget* PDFAppToolGUICompare::createCanvasPage(QWidget* parent)
{
    connectList();
    readViewSettings();
    m_canvas = new PDFAppCompareCanvas(parent);
    for (int slot = 0; slot < SlotCount; ++slot)
    {
        PDFAppCompareSlotCard* card = m_canvas->getSlotCard(slot);
        connect(card, &PDFAppCompareSlotCard::chooseFileRequested, this, slot == OriginalSlot ? &PDFAppToolGUICompare::onChooseOriginal : &PDFAppToolGUICompare::onChooseRevised);
        connect(card, &PDFAppCompareSlotCard::changeRequested, this, slot == OriginalSlot ? &PDFAppToolGUICompare::onChangeOriginal : &PDFAppToolGUICompare::onChangeRevised);
        connect(card, &PDFAppCompareSlotCard::passwordRequested, this, slot == OriginalSlot ? &PDFAppToolGUICompare::onPasswordOriginal : &PDFAppToolGUICompare::onPasswordRevised);
        connect(card, &PDFAppCompareSlotCard::documentChosen, this, slot == OriginalSlot ? &PDFAppToolGUICompare::onDocumentChosenOriginal : &PDFAppToolGUICompare::onDocumentChosenRevised);
    }

    PDFAppCompareView* view = m_canvas->getView();
    view->setView(getView());
    view->setColors(m_colors);
    view->setShowOptions(m_isHighlightsShown, m_isMarkersShown, m_isOnlyChangedPages);
    view->setOverlaySettings(m_overlay);
    connect(view->getPdfWidget()->getDrawWidgetProxy(), &PDFDrawWidgetProxy::repaintNeeded, this, &PDFAppToolGUICompare::onViewRepaint);

    QPointer<PDFAppCompareCanvas> canvas = m_canvas;
    QPointer<PDFAppToolGUICompare> guard(this);
    updateShownResult();
    if (guard && !m_viewWatcher)
    {
        requestViewDocument();
    }
    if (guard)
    {
        updateCanvas();
    }
    if (guard)
    {
        updateChoice();
    }
    return canvas.data();
}

// ------------------------------------------------------------- slots of the parts

void PDFAppToolGUICompare::onViewRowClicked()
{
    using Helper = PDFAppToolGUICompareHelper;

    const QObject* row = sender();
    if (!row)
    {
        return;
    }

    // The panel closes first; the change of the view is the last thing
    const PDFDiffReportView view = PDFDocumentToolCompare::getView(row->property(Helper::ROW_KEY).toString());
    QPointer<PDFAppToolGUICompare> guard(this);
    if (PDFAppToolPanelPopup* panel = getMenuPanel(Helper::name(Helper::MENU_VIEW)))
    {
        panel->close();
    }
    if (guard)
    {
        setView(view);
    }
}

void PDFAppToolGUICompare::onFilterRowClicked()
{
    using Helper = PDFAppToolGUICompareHelper;

    PDFTileRow* row = qobject_cast<PDFTileRow*>(sender());
    if (row && getModel())
    {
        // The row has toggled itself: the value of the model decides (refreshed by updateMenus)
        QPointer<PDFAppToolGUICompare> guard(this);
        getModel()->setValue(row->property(Helper::ROW_KEY).toString(), row->isChecked());
        if (guard)
        {
            updateMenus();
        }
    }
}

void PDFAppToolGUICompare::onShowRowClicked()
{
    using Helper = PDFAppToolGUICompareHelper;

    PDFTileRow* row = qobject_cast<PDFTileRow*>(sender());
    if (row)
    {
        setShowOption(row->property(Helper::ROW_KEY).toString(), row->isChecked());
    }
}

void PDFAppToolGUICompare::onOptionRowClicked()
{
    onFilterRowClicked();
}

void PDFAppToolGUICompare::onExportReportClicked()
{
    using Helper = PDFAppToolGUICompareHelper;

    QPointer<PDFAppToolGUICompare> guard(this);
    if (PDFAppToolPanelPopup* panel = getMenuPanel(Helper::name(Helper::MENU_EXPORT)))
    {
        panel->close();
    }
    if (guard)
    {
        exportReport();
    }
}

void PDFAppToolGUICompare::onExportXmlClicked()
{
    using Helper = PDFAppToolGUICompareHelper;

    QPointer<PDFAppToolGUICompare> guard(this);
    if (PDFAppToolPanelPopup* panel = getMenuPanel(Helper::name(Helper::MENU_EXPORT)))
    {
        panel->close();
    }
    if (guard)
    {
        exportXml();
    }
}

void PDFAppToolGUICompare::onPreviousClicked()
{
    goToPreviousDifference();
}

void PDFAppToolGUICompare::onNextClicked()
{
    goToNextDifference();
}

void PDFAppToolGUICompare::onConfirmKey()
{
    // F5 (the old Compare): the Primary button of the shell, only when it is enabled (ready or stale, Q7.17)
    PDFDocumentToolShell* shell = getShell();
    if (shell && shell->getState() == PDFDocumentToolShell::State::Editing && isConfirmEnabled() && getResultState() != PDFAppToolResultState::Current)
    {
        // The last call (the run may delete this GUI)
        shell->confirm();
    }
}

void PDFAppToolGUICompare::openChangePanel(Slot slot, QWidget* anchor)
{
    using Helper = PDFAppToolGUICompareHelper;

    if (!anchor || m_isRunning)
    {
        return;
    }

    // Change… (plan 4.10, the panel of Interleave): another file or a ready document of a tab of this window; a new
    // panel for each opening (the open documents change), the old one goes later
    if (m_changePanel)
    {
        m_changePanel->deleteLater();
    }
    m_changeSlot = slot;
    m_changePanel = new PDFAppToolPanelPopup(nullptr, Helper::CHANGE_PANEL_WIDTH, anchor->window());
    m_changePanel->setObjectName(QStringLiteral("compareChangePanel"));
    PDFTileRow* fileRow = m_changePanel->addRow(tr("PDF file…"), tr("Choose a file from the disk"), QStringLiteral("folder-open"), PDFTileRow::Kind::MenuItem);
    fileRow->setObjectName(QStringLiteral("compareChangeFile"));
    fileRow->setProperty(Helper::ROW_KEY, QString());
    connect(fileRow, &PDFTileRow::clicked, this, &PDFAppToolGUICompare::onChangeRowClicked);

    const PDFAppDocumentRegistry* registry = getContext() ? getContext()->getDocumentRegistry() : nullptr;
    const PDFAppMainWindow* window = PDFAppPanelWidgets::findMainWindow(anchor);
    const PDFAppToolInput* other = getSlotInput(Slot(1 - int(slot)));
    bool hasCaption = false;
    for (PDFAppDocument* document : registry ? registry->getDocuments() : std::vector<PDFAppDocument*>())
    {
        if (!document->isReady() || (window && !window->findDocumentTab(document->getId())))
        {
            continue;
        }
        if (!hasCaption)
        {
            m_changePanel->addGroupCaption(tr("OPEN TABS"));
            hasCaption = true;
        }
        PDFTileRow* row = m_changePanel->addRow(document->getTitle(), tr("%Ln pages", nullptr, Helper::toInt(document->getPageCount())), QStringLiteral("tab"), PDFTileRow::Kind::MenuItem);
        row->setProperty(Helper::ROW_KEY, document->getId());
        if (other && other->kind == PDFAppToolInputKind::OpenTab && other->documentId == document->getId())
        {
            // FC-3: the document of the other slot
            row->setDescription(slot == OriginalSlot ? tr("Already used as the Revised") : tr("Already used as the Original"));
            row->setEnabled(false);
        }
        connect(row, &PDFTileRow::clicked, this, &PDFAppToolGUICompare::onChangeRowClicked);
    }

    m_changePanel->popup(anchor, anchor->mapToGlobal(QPoint(0, anchor->height())).y() + PDFAppToolToolbar::PANEL_GAP);
}

void PDFAppToolGUICompare::onChangeRowClicked()
{
    const QObject* row = sender();
    if (!row)
    {
        return;
    }

    // The panel closes first, the change is the last thing (it may run the file dialog)
    const QString documentId = row->property(PDFAppToolGUICompareHelper::ROW_KEY).toString();
    const Slot slot = m_changeSlot;
    QPointer<PDFAppToolGUICompare> guard(this);
    if (m_changePanel)
    {
        m_changePanel->close();
    }
    if (!guard)
    {
        return;
    }
    if (documentId.isEmpty())
    {
        chooseSlotFile(slot);
    }
    else
    {
        setSlotDocument(slot, documentId);
    }
}

void PDFAppToolGUICompare::onChooseOriginal()
{
    chooseSlotFile(OriginalSlot);
}

void PDFAppToolGUICompare::onChooseRevised()
{
    chooseSlotFile(RevisedSlot);
}

void PDFAppToolGUICompare::onChangeOriginal()
{
    // Change… of the panel Documents or of the card of the slot: the panel at the link
    QWidget* anchor = qobject_cast<QWidget*>(sender());
    if (PDFAppCompareSlotCard* card = qobject_cast<PDFAppCompareSlotCard*>(anchor))
    {
        anchor = card->getInputRow();
    }
    openChangePanel(OriginalSlot, anchor ? anchor : m_slotRows[0].data());
}

void PDFAppToolGUICompare::onChangeRevised()
{
    QWidget* anchor = qobject_cast<QWidget*>(sender());
    if (PDFAppCompareSlotCard* card = qobject_cast<PDFAppCompareSlotCard*>(anchor))
    {
        anchor = card->getInputRow();
    }
    openChangePanel(RevisedSlot, anchor ? anchor : m_slotRows[1].data());
}

void PDFAppToolGUICompare::onPasswordOriginal()
{
    openPassword(OriginalSlot);
}

void PDFAppToolGUICompare::onPasswordRevised()
{
    openPassword(RevisedSlot);
}

void PDFAppToolGUICompare::onDocumentChosenOriginal(const QString& documentId)
{
    setSlotDocument(OriginalSlot, documentId);
}

void PDFAppToolGUICompare::onDocumentChosenRevised(const QString& documentId)
{
    setSlotDocument(RevisedSlot, documentId);
}

void PDFAppToolGUICompare::onRangeEditedOriginal()
{
    if (m_rangeFields[0] && m_rangeFields[0]->text().trimmed() != m_rangeTexts[0])
    {
        setPageRangeText(OriginalSlot, m_rangeFields[0]->text());
    }
}

void PDFAppToolGUICompare::onRangeEditedRevised()
{
    if (m_rangeFields[1] && m_rangeFields[1]->text().trimmed() != m_rangeTexts[1])
    {
        setPageRangeText(RevisedSlot, m_rangeFields[1]->text());
    }
}

void PDFAppToolGUICompare::onDifferenceSelectionChanged()
{
    if (m_isUpdatingControls || !m_differenceView || !m_differenceModel)
    {
        return;
    }

    // Qt reads the selection model after it has emitted currentChanged, and selectDifference runs code of the
    // application (the view, the status text), which may delete the tab with the list: the selection is queued
    QMetaObject::invokeMethod(this, &PDFAppToolGUICompare::onQueuedDifferenceSelectionChanged, Qt::QueuedConnection);
}

void PDFAppToolGUICompare::onQueuedDifferenceSelectionChanged()
{
    // The current row of now (the list may have changed since the signal)
    if (m_isUpdatingControls || !m_differenceView || !m_differenceModel)
    {
        return;
    }
    const std::optional<size_t> index = m_differenceModel->getDifferenceIndex(m_differenceView->currentIndex());
    if (index && index != m_selected)
    {
        selectDifference(index);
    }
}

void PDFAppToolGUICompare::onColorChosen(const QColor& color)
{
    const QObject* row = sender();
    if (row)
    {
        setColor(ColorKind(row->property(PDFAppToolGUICompareHelper::ROW_KEY).toInt()), color);
    }
}

void PDFAppToolGUICompare::onBlendChanged(int value)
{
    if (m_isUpdatingControls)
    {
        return;
    }
    PDFAppCompareOverlaySettings settings = m_overlay;
    settings.blend = value;
    setOverlaySettings(settings);
}

void PDFAppToolGUICompare::onPageSizesChanged(int index)
{
    if (m_isUpdatingControls || !m_pageSizesCombo || index < 0)
    {
        return;
    }
    PDFAppCompareOverlaySettings settings = m_overlay;
    settings.scaleMode = PDFAppCompareOverlaySettings::ScaleMode(m_pageSizesCombo->itemData(index).toInt());
    setOverlaySettings(settings);
}

void PDFAppToolGUICompare::onOverlayValueChanged()
{
    if (m_isUpdatingControls)
    {
        return;
    }
    PDFAppCompareOverlaySettings settings = m_overlay;
    if (m_scaleSpinBoxes[0] && m_scaleSpinBoxes[1])
    {
        settings.originalScale = m_scaleSpinBoxes[0]->value() * 0.01;
        settings.revisedScale = m_scaleSpinBoxes[1]->value() * 0.01;
    }
    if (m_offsetSpinBoxes[0] && m_offsetSpinBoxes[1])
    {
        settings.revisedOffsetMM = QPointF(m_offsetSpinBoxes[0]->value(), m_offsetSpinBoxes[1]->value());
    }
    setOverlaySettings(settings);
}

void PDFAppToolGUICompare::onViewRepaint()
{
    // The signal comes from inside of the proxy of the canvas (a new layout, a scroll): the status bar, whose slots may
    // delete the window with the canvas, is updated later from the event loop, once for more signals (the lifetime
    // sweep: a deletion from statusTextChanged inside of PDFWidget::setDocument)
    if (!m_isViewRepaintQueued)
    {
        m_isViewRepaintQueued = true;
        QMetaObject::invokeMethod(this, &PDFAppToolGUICompare::onViewRepaintQueued, Qt::QueuedConnection);
    }
}

void PDFAppToolGUICompare::onViewRepaintQueued()
{
    // The page of the view in the bar and in the status bar, the zoom of the canvas in the zoom item of the status bar
    // (492:19314 "151 %": a zoom by the wheel, by the keys, a fit) - only when one of them changed
    m_isViewRepaintQueued = false;
    const PDFAppCompareView* view = getCompareView();
    const PDFInteger page = view ? view->getCurrentPageIndex() : -1;
    const QString zoomText = getOwnTabZoomText();
    const bool isPageChanged = page != m_shownPage;
    if (!isPageChanged && zoomText == m_shownZoomText)
    {
        return;
    }

    m_shownPage = page;
    m_shownZoomText = zoomText;
    QPointer<PDFAppToolGUICompare> guard(this);
    if (isPageChanged)
    {
        updateViewTexts();
    }
    if (guard)
    {
        updateStatusText();
    }
}

bool PDFAppToolGUICompare::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_keyWidget)
    {
        switch (event->type())
        {
            case QEvent::DragEnter:
            case QEvent::DragMove:
            {
                QDropEvent* dropEvent = static_cast<QDropEvent*>(event);
                if (!m_isRunning && PDFAppFileDrop::hasLocalFiles(dropEvent->mimeData()))
                {
                    dropEvent->acceptProposedAction();
                    return true;
                }
                break;
            }

            case QEvent::Drop:
            {
                QDropEvent* dropEvent = static_cast<QDropEvent*>(event);
                if (!m_isRunning && PDFAppFileDrop::hasLocalFiles(dropEvent->mimeData()))
                {
                    // The files are added from the event handler, the last thing (a slot may close the tab)
                    dropEvent->acceptProposedAction();
                    const QPoint globalPosition = m_keyWidget->mapToGlobal(dropEvent->position().toPoint());
                    dropFiles(dropEvent->mimeData(), globalPosition);
                    return true;
                }
                break;
            }

            default:
                break;
        }
    }
    return BaseClass::eventFilter(watched, event);
}

bool PDFAppToolGUICompare::dropFiles(const QMimeData* mimeData, const QPoint& globalPosition)
{
    // A drop onto a card of a slot fills that slot; elsewhere the files fill the empty slots in their order (the first
    // one the Original); one file onto a full tab replaces the Revised
    const PDFAppFileDrop::Result drop = PDFAppFileDrop::getFiles(mimeData, SlotCount);
    if (drop.files.isEmpty())
    {
        if (!drop.refused.empty() && getHost())
        {
            getHost()->showTransientMessage(PDFAppFileDrop::getRefusalText(drop.refused.front().refusal));
        }
        return false;
    }

    std::optional<Slot> target;
    for (int slot = 0; slot < SlotCount && m_canvas; ++slot)
    {
        const PDFAppCompareSlotCard* card = m_canvas->getSlotCard(slot);
        if (card && card->isVisible() && card->rect().contains(card->mapFromGlobal(globalPosition)))
        {
            target = Slot(slot);
        }
    }

    QPointer<PDFAppToolGUICompare> guard(this);
    if (target)
    {
        setSlotFile(*target, drop.files.front());
        return true;
    }
    for (const QString& file : drop.files)
    {
        if (!guard)
        {
            return false;
        }
        Slot slot = RevisedSlot;
        if (!getSlotInput(OriginalSlot))
        {
            slot = OriginalSlot;
        }
        setSlotFile(slot, file);
    }
    return true;
}

// ------------------------------------------------------------- updates

PDFAppToolPanelPopup* PDFAppToolGUICompare::getMenuPanel(const QString& menuId) const
{
    const auto it = m_menus.find(menuId);
    return it != m_menus.cend() ? it->second.data() : nullptr;
}

PDFTileRow* PDFAppToolGUICompare::getMenuRow(const QString& rowId) const
{
    const auto it = m_menuRows.find(rowId);
    return it != m_menuRows.cend() ? it->second.data() : nullptr;
}

void PDFAppToolGUICompare::updateAll()
{
    // The parts first, then the button and the status bar (signals, the last calls); a change of the visibility or a
    // reset of a model may run an observer, which deletes this GUI
    QPointer<PDFAppToolGUICompare> guard(this);
    updateSlots();
    if (guard)
    {
        updateChoice();
    }
    if (guard)
    {
        updateDifferences();
    }
    if (guard)
    {
        updateCanvas();
    }
    if (guard)
    {
        updateMenus();
    }
    if (guard)
    {
        updateOverlayControls();
    }
    if (guard)
    {
        updateConfirmState();
    }
    if (guard)
    {
        updateStatusText();
    }
    if (guard)
    {
        updateOwnTabState();
    }
}

void PDFAppToolGUICompare::updateSlots()
{
    const bool hasBoth = isSlotComparable(OriginalSlot) && isSlotComparable(RevisedSlot);
    for (int slot = 0; slot < SlotCount; ++slot)
    {
        const PDFAppToolInput* input = getSlotInput(Slot(slot));
        if (m_slotRows[size_t(slot)])
        {
            m_slotRows[size_t(slot)]->setDescription(input ? input->name : QString());
            m_slotRows[size_t(slot)]->setLinkText(m_isRunning ? QString() : tr("Change…"));
        }
        if (m_rangeFields[size_t(slot)] && !m_rangeFields[size_t(slot)]->hasFocus())
        {
            const QSignalBlocker blocker(m_rangeFields[size_t(slot)].data());
            m_rangeFields[size_t(slot)]->setText(m_rangeTexts[size_t(slot)]);
        }
        if (m_rangeFields[size_t(slot)])
        {
            m_rangeFields[size_t(slot)]->setInvalid(!m_rangeErrors[size_t(slot)].isEmpty(), m_rangeErrors[size_t(slot)]);
            m_rangeFields[size_t(slot)]->setEnabled(!m_isRunning && isSlotComparable(Slot(slot)));
        }
        if (m_pageCountLabels[size_t(slot)])
        {
            m_pageCountLabels[size_t(slot)]->setText(input ? tr("of %1").arg(PDFAppLocale::formatNumber(input->pageCount)) : QString());
        }
        if (m_rangeErrorLabels[size_t(slot)])
        {
            m_rangeErrorLabels[size_t(slot)]->setText(m_rangeErrors[size_t(slot)]);
            m_rangeErrorLabels[size_t(slot)]->setVisible(!m_rangeErrors[size_t(slot)].isEmpty());
        }
    }

    // The visibility last (each followed by a guard)
    QPointer<PDFAppToolGUICompare> guard(this);
    if (m_swapButton)
    {
        m_swapButton->setEnabled(!m_isRunning && m_list && m_list->getCount() == SlotCount);
    }
    if (guard && m_infoCallout)
    {
        m_infoCallout->setVisible(!hasBoth);
    }
    if (guard && m_documentsSection)
    {
        m_documentsSection->setVisible(hasBoth);
    }
    if (guard && m_colorsSection)
    {
        m_colorsSection->setVisible(hasBoth);
    }
}

void PDFAppToolGUICompare::updateChoice()
{
    using Helper = PDFAppToolGUICompareHelper;

    if (!m_canvas)
    {
        return;
    }

    // The open documents of the window (ready ones), the document of the other slot Disabled (FC-3)
    const PDFAppDocumentRegistry* registry = getContext() ? getContext()->getDocumentRegistry() : nullptr;
    const PDFAppMainWindow* window = PDFAppPanelWidgets::findMainWindow(m_canvas);
    std::vector<PDFAppCompareSlotCard::Content> contents(SlotCount);
    for (int slot = 0; slot < SlotCount; ++slot)
    {
        PDFAppCompareSlotCard::Content& content = contents[size_t(slot)];
        const PDFAppToolInput* input = getSlotInput(Slot(slot));
        if (input && isSlotComparable(Slot(slot)))
        {
            content.inputName = input->name;
            QString details = tr("%Ln pages", nullptr, Helper::toInt(input->pageCount));
            if (input->kind == PDFAppToolInputKind::OpenTab)
            {
                details += QStringLiteral(" · ") + (input->isModified ? tr("open tab, the working version is used") : tr("open tab"));
            }
            content.inputDetails = details;
        }
        else if (input && input->state == PDFAppToolInputState::PasswordRequired)
        {
            content.passwordText = tr("%1 is encrypted.").arg(input->name);
        }
        else if (input && input->state != PDFAppToolInputState::Loading)
        {
            // 492:106266 "Scan_2019.pdf cannot be read" with the reason
            content.errorTitle = tr("%1 cannot be read").arg(input->name);
            const QString reason = input->reason.isEmpty() ? PDFAppToolInputList::getStateText(input->state) : input->reason;
            content.errorText = tr("%1 Choose another file.").arg(reason);
        }
        else if (input)
        {
            content.inputName = input->name;
            content.inputDetails = PDFAppToolInputList::getStateText(input->state);
        }

        if (content.inputName.isEmpty())
        {
            const PDFAppToolInput* other = getSlotInput(Slot(1 - slot));
            for (PDFAppDocument* document : registry ? registry->getDocuments() : std::vector<PDFAppDocument*>())
            {
                if (!document->isReady() || (window && !window->findDocumentTab(document->getId())))
                {
                    continue;
                }
                PDFAppCompareSlotCard::OpenDocument row;
                row.documentId = document->getId();
                row.name = document->getTitle();
                row.details = tr("%Ln pages", nullptr, Helper::toInt(document->getPageCount()));
                if (document->isModified())
                {
                    // 492:18158 "25 pages · unsaved changes, the working version is used"
                    row.details += QStringLiteral(" · ") + tr("unsaved changes, the working version is used");
                    row.isWarning = true;
                }
                if (other && other->kind == PDFAppToolInputKind::OpenTab && other->documentId == document->getId())
                {
                    row.details = slot == OriginalSlot ? tr("Already used as the Revised") : tr("Already used as the Original");
                    row.isWarning = false;
                    row.isDisabled = true;
                }
                content.openDocuments.push_back(row);
            }
        }
    }

    QPointer<PDFAppToolGUICompare> guard(this);
    for (int slot = 0; slot < SlotCount && guard && m_canvas; ++slot)
    {
        if (PDFAppCompareSlotCard* card = m_canvas->getSlotCard(slot))
        {
            card->setContent(contents[size_t(slot)]);
            card->setEnabled(!m_isRunning);
        }
    }
}

void PDFAppToolGUICompare::updateDifferences()
{
    if (!m_differencesHeader || !m_differencesStack || !m_emptyText)
    {
        return;
    }

    // The header and the empty states of 492:18158, 492:18424, 492:18869, 492:105498, FC-9
    QString subtitle = tr("Not compared yet");
    QString emptyText;
    QString emptyIcon = QStringLiteral("compare");
    bool isListShown = false;
    const bool hasBoth = isSlotComparable(OriginalSlot) && isSlotComparable(RevisedSlot);
    if (!hasBoth)
    {
        emptyText = tr("Choose the two documents first.");
    }
    else if (m_isRunning)
    {
        subtitle = tr("Comparing…");
        emptyText = tr("Differences appear here when the comparison is finished.");
    }
    else if (m_content == Content::NoDifferences)
    {
        subtitle = tr("No differences found");
        emptyText = tr("The compared pages are identical.");
        emptyIcon = QStringLiteral("check-circle");
    }
    else if (m_hasResult && m_shownResult.isSame())
    {
        subtitle = getFilteredSummary();
        emptyText = tr("All differences are hidden by the filter.");
        emptyIcon = QStringLiteral("filter");
    }
    else if (m_hasResult)
    {
        subtitle = m_shownResult.getDifferencesCount() < m_result.getDifferencesCount() ? getFilteredSummary() : getDifferencesSummary();
        isListShown = true;
    }
    else if (m_content == Content::Failed)
    {
        emptyText = tr("The documents could not be compared.");
        emptyIcon = QStringLiteral("alert-circle");
    }
    else
    {
        emptyText = tr("Differences appear here after you press Compare.");
    }

    m_differencesHeader->setTexts(tr("Differences"), subtitle);
    m_emptyText->setText(emptyText);
    if (m_emptyBadge)
    {
        m_emptyBadge->setIconName(emptyIcon);
        m_emptyBadge->setColors(std::nullopt, emptyIcon == QLatin1String("check-circle") ? PDFThemeColors::AccentGreenIcon : PDFThemeColors::IconDefault);
    }
    if (m_differenceDelegate)
    {
        m_differenceDelegate->setColors(m_colors);
    }
    m_differencesStack->setCurrentIndex(isListShown ? 0 : 1);
}

void PDFAppToolGUICompare::updateViewTexts()
{
    PDFAppCompareView* view = getCompareView();
    if (!view)
    {
        return;
    }

    // preview-bar 492:20191: "Page 3 of 24 · Page 3 of 25", "Difference 3 of 14"; "Not compared yet", "Comparing…",
    // "Overlay · page 3"
    QString title;
    QString detail;
    const PDFInteger page = view->getCurrentPageIndex();
    const PDFInteger original = view->getLayout().getOriginalPageIndex(page);
    const PDFInteger revised = view->getLayout().getRevisedPageIndex(page);
    const PDFAppToolInput* originalInput = getSlotInput(OriginalSlot);
    const PDFAppToolInput* revisedInput = getSlotInput(RevisedSlot);
    if (m_isRunning)
    {
        title = tr("Comparing…");
    }
    else if (!m_hasResult)
    {
        title = tr("Not compared yet");
    }
    else if (getView() == PDFDiffReportView::Overlay)
    {
        title = tr("Overlay · page %1").arg(PDFAppLocale::formatNumber(qint64((original >= 0 ? original : qMax<PDFInteger>(revised, 0)) + 1)));
    }
    else
    {
        // The page of the pair of the current page
        PDFInteger originalPage = original;
        PDFInteger revisedPage = revised;
        for (const PDFDiffResult::PageSequenceItem& item : m_shownResult.getPageSequence())
        {
            if ((original >= 0 && item.leftPage == original) || (revised >= 0 && item.rightPage == revised))
            {
                originalPage = item.leftPage;
                revisedPage = item.rightPage;
                break;
            }
        }
        QStringList parts;
        if (originalPage >= 0 && originalInput)
        {
            parts << tr("Page %1 of %2").arg(PDFAppLocale::formatNumber(qint64(originalPage) + 1), PDFAppLocale::formatNumber(originalInput->pageCount));
        }
        if (revisedPage >= 0 && revisedInput)
        {
            parts << tr("Page %1 of %2").arg(PDFAppLocale::formatNumber(qint64(revisedPage) + 1), PDFAppLocale::formatNumber(revisedInput->pageCount));
        }
        title = parts.join(QStringLiteral(" · "));
    }
    if (m_hasResult && m_isOnlyChangedPages && getView() != PDFDiffReportView::Overlay)
    {
        // 492:104504 "Only pages with differences · 4 of 25 page pairs"
        const size_t pairCount = m_shownResult.getPageSequence().size();
        detail = tr("Only pages with differences · %1 of %2 page pairs").arg(PDFAppLocale::formatNumber(qint64(PDFAppToolGUICompareHelper::getPagePairCount(m_shownResult))), PDFAppLocale::formatNumber(qint64(pairCount)));
    }
    else if (m_hasResult && m_selected)
    {
        detail = tr("Difference %1 of %2").arg(PDFAppLocale::formatNumber(qint64(*m_selected) + 1), PDFAppLocale::formatNumber(qint64(m_shownResult.getDifferencesCount())));
    }
    view->setBarTexts(title, detail);

    // "Original · Report_v1.pdf", "Revised · Report_v2.pdf"
    const QString originalName = m_hasResult && m_resultInputs.size() == SlotCount ? m_resultInputs[0].fileName : getInputName(OriginalSlot);
    const QString revisedName = m_hasResult && m_resultInputs.size() == SlotCount ? m_resultInputs[1].fileName : getInputName(RevisedSlot);
    view->setCaptions(originalName.isEmpty() ? QString() : tr("Original · %1").arg(originalName), revisedName.isEmpty() ? QString() : tr("Revised · %1").arg(revisedName));
}

void PDFAppToolGUICompare::updateCanvas()
{
    if (!m_canvas)
    {
        return;
    }

    PDFAppCompareView* view = m_canvas->getView();
    updateViewTexts();

    // 492:18424 "Press Compare to find the differences" before a result
    if (!m_hasResult && !m_isRunning && m_content != Content::Failed)
    {
        view->setInfo(tr("Press Compare to find the differences"), tr("Text, vector graphics, images, shading and moved pages are compared. Nothing is changed in either document."));
    }
    else
    {
        view->setInfo(QString(), QString());
    }

    const bool hasBoth = isSlotComparable(OriginalSlot) && isSlotComparable(RevisedSlot);
    QPointer<PDFAppToolGUICompare> guard(this);
    if (!hasBoth)
    {
        m_canvas->showChoice();
        return;
    }

    m_canvas->showView();
    if (!guard || !m_canvas)
    {
        return;
    }
    if (m_content == Content::Failed && !m_isRunning)
    {
        // FC-2: the red callout of a failure (the error of the core, the budget of the memory)
        view->showError(tr("The documents cannot be compared"), m_failureText.isEmpty() ? tr("The comparison failed. Check the documents and press Compare Again.") : m_failureText);
    }
    else if (m_content == Content::NoDifferences && !m_isRunning)
    {
        // 492:105498
        const QString originalName = m_resultInputs.size() == SlotCount ? m_resultInputs[0].fileName : getInputName(OriginalSlot);
        const QString revisedName = m_resultInputs.size() == SlotCount ? m_resultInputs[1].fileName : getInputName(RevisedSlot);
        view->showMessage(tr("No differences found"), tr("Pages %1 of %2 and pages %3 of %4 are identical in text, vector graphics, images and shading.")
                                                         .arg(m_rangeTexts[0], originalName, m_rangeTexts[1], revisedName));
    }
    else
    {
        view->showPages();
    }
}

void PDFAppToolGUICompare::updateMenus()
{
    using Helper = PDFAppToolGUICompareHelper;

    const PDFAppToolParameterModel* model = getModel();
    const bool hasBoth = isSlotComparable(OriginalSlot) && isSlotComparable(RevisedSlot);
    const bool hasResult = m_hasResult && !m_isRunning;

    // View: the check of the view, the title and the icon of the button
    const PDFDiffReportView view = getView();
    for (PDFDiffReportView rowView : { PDFDiffReportView::SideBySide, PDFDiffReportView::Original, PDFDiffReportView::Revised, PDFDiffReportView::Overlay })
    {
        if (PDFTileRow* row = getMenuRow(QStringLiteral("view-") + Helper::getViewId(rowView)))
        {
            const QSignalBlocker blocker(row);
            row->setChecked(rowView == view);
        }
    }
    if (PDFAppToolPanelPopup* panel = getMenuPanel(Helper::name(Helper::MENU_VIEW)))
    {
        if (QToolButton* button = panel->getButton())
        {
            button->setText(Helper::getViewTitle(view));
            button->setToolTip(Helper::getViewTitle(view));
            button->setIcon(PDFThemeIcons::getIcon(Helper::getViewIcon(view)));
            button->setEnabled(hasBoth && !m_isRunning);
        }
    }

    // Filter: the counts of the whole result, a kind without a difference Disabled (492:20366)
    const std::array<std::pair<const char*, const char*>, 5> categories = { std::make_pair(PDFDocumentToolCompare::PARAMETER_INCLUDE_TEXT, PDFDocumentToolCompare::CATEGORY_TEXT),
                                                                            std::make_pair(PDFDocumentToolCompare::PARAMETER_INCLUDE_VECTOR_GRAPHICS, PDFDocumentToolCompare::CATEGORY_VECTOR_GRAPHICS),
                                                                            std::make_pair(PDFDocumentToolCompare::PARAMETER_INCLUDE_IMAGES, PDFDocumentToolCompare::CATEGORY_IMAGES),
                                                                            std::make_pair(PDFDocumentToolCompare::PARAMETER_INCLUDE_SHADING, PDFDocumentToolCompare::CATEGORY_SHADING),
                                                                            std::make_pair(PDFDocumentToolCompare::PARAMETER_INCLUDE_PAGE_MOVEMENT, PDFDocumentToolCompare::CATEGORY_PAGE_MOVEMENT) };
    int includedCount = 0;
    for (const auto& [parameter, category] : categories)
    {
        includedCount += model && model->getValue(Helper::name(parameter)).toBool() ? 1 : 0;
        if (PDFTileRow* row = getMenuRow(QStringLiteral("filter-") + Helper::name(parameter)))
        {
            const int count = getCategoryCount(Helper::name(category));
            QString description = tr("%Ln differences", nullptr, count);
            if (QLatin1String(category) == QLatin1String(PDFDocumentToolCompare::CATEGORY_PAGE_MOVEMENT))
            {
                description = tr("%Ln differences – moved, added and removed pages", nullptr, count);
            }
            row->setDescription(description);
            row->setEnabled(count > 0);
            const QSignalBlocker blocker(row);
            row->setChecked(model && model->getValue(Helper::name(parameter)).toBool());
        }
    }
    if (PDFAppToolPanelPopup* panel = getMenuPanel(Helper::name(Helper::MENU_FILTER)))
    {
        if (QToolButton* button = panel->getButton())
        {
            // 492:104504 "Filter · 4 of 5": the button says that a kind is hidden
            const int kindCount = int(categories.size());
            const QString title = includedCount < kindCount ? tr("Filter · %1 of %2").arg(PDFAppLocale::formatNumber(qint64(includedCount)), PDFAppLocale::formatNumber(qint64(kindCount))) : tr("Filter");
            button->setText(title);
            button->setToolTip(title);
            button->setEnabled(hasResult && m_result.isChanged());
        }
    }

    // Show
    const std::array<std::pair<const char*, bool>, 4> shows = { std::make_pair(SETTING_HIGHLIGHTS, m_isHighlightsShown), std::make_pair(SETTING_MARKERS, m_isMarkersShown),
                                                                std::make_pair(SETTING_ONLY_CHANGED, m_isOnlyChangedPages), std::make_pair(SETTING_FOLLOW, m_isSelectionFollowed) };
    for (const auto& [setting, isOn] : shows)
    {
        if (PDFTileRow* row = getMenuRow(QStringLiteral("show-") + Helper::name(setting)))
        {
            const QSignalBlocker blocker(row);
            row->setChecked(isOn);
        }
    }
    if (PDFAppToolPanelPopup* panel = getMenuPanel(Helper::name(Helper::MENU_SHOW)))
    {
        if (panel->getButton())
        {
            panel->getButton()->setEnabled(hasResult && m_result.isChanged());
        }
    }

    // Options
    for (const char* parameter : { PDFDocumentToolCompare::PARAMETER_COMPARE_TEXT_AS_VECTOR, PDFDocumentToolCompare::PARAMETER_COMPARE_CHARACTERS })
    {
        if (PDFTileRow* row = getMenuRow(QStringLiteral("option-") + Helper::name(parameter)))
        {
            const QSignalBlocker blocker(row);
            row->setChecked(model && model->getValue(Helper::name(parameter)).toBool());
        }
    }
    if (PDFAppToolPanelPopup* panel = getMenuPanel(Helper::name(Helper::MENU_OPTIONS)))
    {
        if (panel->getButton())
        {
            panel->getButton()->setEnabled(hasBoth && !m_isRunning);
        }
    }

    // Previous / next, Export (R7-9)
    if (m_previousButton)
    {
        m_previousButton->setEnabled(canGoToPreviousDifference());
    }
    if (m_nextButton)
    {
        m_nextButton->setEnabled(canGoToNextDifference());
    }
    const bool isExport = isExportEnabled();
    if (PDFAppToolPanelPopup* panel = getMenuPanel(Helper::name(Helper::MENU_EXPORT)))
    {
        if (panel->getButton())
        {
            panel->getButton()->setEnabled(isExport);
        }
    }
    for (const char* rowId : { Helper::EXPORT_REPORT, Helper::EXPORT_XML })
    {
        if (PDFTileRow* row = getMenuRow(Helper::name(rowId)))
        {
            row->setEnabled(isExport);
        }
    }
}

void PDFAppToolGUICompare::updateOverlayControls()
{
    // The controls follow the values (no signal back)
    m_isUpdatingControls = true;
    if (m_blendSlider)
    {
        m_blendSlider->setValue(m_overlay.blend);
    }
    if (m_blendLabel)
    {
        // 493:18777 "50 % / 50 %": the original share, the revised share
        m_blendLabel->setText(tr("%1 % / %2 %").arg(PDFAppLocale::formatNumber(100 - m_overlay.blend), PDFAppLocale::formatNumber(m_overlay.blend)));
    }
    if (m_pageSizesCombo)
    {
        m_pageSizesCombo->setCurrentIndex(m_pageSizesCombo->findData(int(m_overlay.scaleMode)));

        // The check mark of the chosen mode in the list (493:19914)
        for (int i = 0; i < m_pageSizesCombo->count(); ++i)
        {
            m_pageSizesCombo->setItemData(i, i == m_pageSizesCombo->currentIndex() ? Qt::Checked : Qt::Unchecked, Qt::CheckStateRole);
        }
    }
    if (m_scaleSpinBoxes[0] && m_scaleSpinBoxes[1])
    {
        m_scaleSpinBoxes[0]->setValue(m_overlay.originalScale * 100.0);
        m_scaleSpinBoxes[1]->setValue(m_overlay.revisedScale * 100.0);
    }
    if (m_offsetSpinBoxes[0] && m_offsetSpinBoxes[1])
    {
        m_offsetSpinBoxes[0]->setValue(m_overlay.revisedOffsetMM.x());
        m_offsetSpinBoxes[1]->setValue(m_overlay.revisedOffsetMM.y());
    }
    m_isUpdatingControls = false;

    // The visibility last: the section only in Overlay (493:18777), the scales only in Manual (493:19325)
    QPointer<PDFAppToolGUICompare> guard(this);
    const bool isManual = m_overlay.scaleMode == PDFAppCompareOverlaySettings::ScaleMode::Manual;
    for (int slot = 0; slot < SlotCount && guard; ++slot)
    {
        if (QWidget* row = m_scaleRows[size_t(slot)].data())
        {
            row->setVisible(isManual);
        }
    }
    if (guard && m_overlaySection)
    {
        m_overlaySection->setVisible(getView() == PDFDiffReportView::Overlay && isSlotComparable(OriginalSlot) && isSlotComparable(RevisedSlot));
    }
}

}   // namespace pdf

#include "moc_pdfapptoolgui_compare.cpp"
