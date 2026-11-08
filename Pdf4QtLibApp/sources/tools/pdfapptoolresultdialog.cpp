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

#include "pdfapptoolresultdialog.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentregistry.h"
#include "pdfappdocumenttab.h"
#include "pdfappfiledrop.h"
#include "pdfappmainwindow.h"
#include "pdfapppagelayout.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolpathrules.h"
#include "pdfapptoolpresentation.h"
#include "pdfappworkspace.h"
#include "pdfdocumenttextexporter.h"
#include "pdfdocumenttoolrunner.h"
#include "pdfdocumenttoolusage.h"

#include "pdfcomponentbadges.h"
#include "pdfcomponentbuttons.h"
#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumenttoolsearch.h"
#include "pdfdocumenttoolsuggestions.h"
#include "pdffluentwidgets.h"
#include "pdfform.h"
#include "pdfstylepainter.h"
#include "pdfstylepainterguard.h"
#include "pdfthemefonts.h"
#include "pdfthemeiconengine.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyleproperties.h"

#include <algorithm>
#include <limits>

#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QStandardPaths>
#include <QStringDecoder>
#include <QUrl>
#include <QVBoxLayout>

namespace pdf
{

/// Panel of the facts (312:7382): bg/subtle, the radius 10, paddings 4 / 12
class PDFAppToolResultFactsPanel : public QWidget
{
public:
    explicit PDFAppToolResultFactsPanel(QWidget* parent) :
        QWidget(parent)
    {

    }

protected:
    virtual void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        PDFStylePainter::fill(&painter, QRectF(rect()), PDFStylePainter::Radii(RADIUS), PDFThemeColors::BgSubtle);
    }

private:
    static constexpr qreal RADIUS = 10.0;
};

/// Search of Another tool… of Then continue with (no frame of the design, built from the
/// parts of the catalog): every tool of the catalog (owner answer B of 2026-11-01)
class PDFAppToolChooserDialog : public PDFMessageBox
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppToolChooserDialog)

public:
    explicit PDFAppToolChooserDialog(QWidget* parent) :
        PDFMessageBox(PDFThemeIcons::Badge::Neutral, tr("Another tool"), tr("Choose the tool, which starts on the result."), QDialogButtonBox::NoButton, parent)
    {
        setObjectName(QStringLiteral("toolChooserDialog"));
        setBadgeVisible(false);
        setCardWidth(WIDTH);

        m_searchField = new PDFLineEdit(this);
        m_searchField->setObjectName(QStringLiteral("toolChooserSearch"));
        m_searchField->setPlaceholderText(tr("Search tools…"));
        m_searchField->setMaxLength(MAXIMUM_SEARCH_LENGTH);
        addContent(m_searchField);

        QScrollArea* scrollArea = new QScrollArea(this);
        scrollArea->setFrameShape(QFrame::NoFrame);
        scrollArea->setWidgetResizable(true);
        scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scrollArea->setFixedHeight(LIST_HEIGHT);
        PDFWidgetStyleProperties::set(scrollArea, PDFWidgetStyleProperties::OVERLAY_SCROLL_BARS);
        m_list = new QWidget(scrollArea);
        m_listLayout = new QVBoxLayout(m_list);
        m_listLayout->setContentsMargins(0, 0, 0, 0);
        m_listLayout->setSpacing(2);
        m_listLayout->addStretch(1);
        scrollArea->setWidget(m_list);
        addContent(scrollArea);

        QPushButton* cancelButton = addActionButton(tr("Cancel"), ActionPosition::Trailing, false);
        cancelButton->setObjectName(QStringLiteral("toolChooserCancel"));
        m_cancelButton = cancelButton;
        connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
        connect(m_searchField, &QLineEdit::textChanged, this, &PDFAppToolChooserDialog::updateRows);
        updateRows();
        m_searchField->setFocus();
    }

    /// The chosen tool (empty = none)
    const QString& getChosenToolId() const { return m_chosenToolId; }

protected:
    virtual void changeEvent(QEvent* event) override
    {
        // The texts and the names of the tools; the typed search stays
        if (event->type() == QEvent::LanguageChange && m_searchField)
        {
            setTitle(tr("Another tool"));
            setText(tr("Choose the tool, which starts on the result."));
            m_searchField->setPlaceholderText(tr("Search tools…"));
            if (m_cancelButton)
            {
                m_cancelButton->setText(tr("Cancel"));
            }
            updateRows();
        }
        PDFMessageBox::changeEvent(event);
    }

private:
    void updateRows()
    {
        while (m_listLayout->count() > 1)
        {
            QLayoutItem* item = m_listLayout->takeAt(0);
            if (QWidget* widget = item->widget())
            {
                widget->hide();
                widget->deleteLater();
            }
            delete item;
        }

        std::vector<const PDFDocumentToolCatalogEntry*> tools;
        const QString text = m_searchField->text().trimmed();
        if (text.isEmpty())
        {
            for (const PDFDocumentToolCategoryInfo& category : PDFDocumentToolCatalog::getCategories())
            {
                for (const PDFDocumentToolCatalogEntry* tool : PDFDocumentToolCatalog::getTools(category.getCategory()))
                {
                    tools.push_back(tool);
                }
            }
        }
        else
        {
            for (const PDFDocumentToolSearchMatch& match : PDFDocumentToolSearch::search(text))
            {
                tools.push_back(match.tool);
            }
        }

        int index = 0;
        for (const PDFDocumentToolCatalogEntry* tool : tools)
        {
            if (!text.isEmpty() && index >= PDFAppToolResultDialog::MAXIMUM_CHOOSER_ROWS)
            {
                break;
            }

            PDFToolRow* row = PDFAppToolPresentation::createToolRow(*tool, false, m_list);
            row->setStarVisible(false);
            connect(row, &PDFToolRow::clicked, this, &PDFAppToolChooserDialog::onRowClicked);
            m_listLayout->insertWidget(index++, row);
        }
    }

    void onRowClicked()
    {
        if (const QObject* row = sender())
        {
            m_chosenToolId = PDFAppToolPresentation::getToolId(row);
            accept();
        }
    }

    static constexpr int WIDTH = 440;
    static constexpr int LIST_HEIGHT = 288;
    static constexpr int MAXIMUM_SEARCH_LENGTH = 256;

    QPointer<PDFLineEdit> m_searchField;
    QPointer<QPushButton> m_cancelButton;
    QPointer<QWidget> m_list;
    QPointer<QVBoxLayout> m_listLayout;
    QString m_chosenToolId;
};

/// Helpers of the result dialog
class PDFAppToolResultDialogHelper
{
public:
    PDFAppToolResultDialogHelper() = delete;

    static constexpr int HEAD_BADGE = 36;
    static constexpr int HEAD_ICON = 20;
    static constexpr int HEAD_GAP = 12;
    static constexpr int HEAD_TEXT_GAP = 2;
    static constexpr int SECTION_GAP = 14;      ///< Gap of the dialog (312:7374)
    static constexpr int OPTION_GAP = 6;        ///< Gap of the options (312:7392)
    static constexpr int CALLOUT_RADIUS = 8;
    static constexpr int CALLOUT_PADDING_H = 13;///< Padding 12 + the border (316:7764)
    static constexpr int CALLOUT_PADDING_V = 11;///< Padding 10 + the border
    static constexpr int CALLOUT_GAP = 8;
    static constexpr int FIELD_HEIGHT = 34;     ///< File name of a copy (312:7410)
    static constexpr int PATH_HEIGHT = 36;      ///< Path of an export (339:10301)
    static constexpr int PATH_GAP = 6;          ///< Gap of the path and Browse… (355:9882)

    /// A callout of the dialog: an icon 16, a title Semi Bold 12 and a text
    static PDFTintedCard* createCallout(PDFAccentHue hue, const QString& iconName, PDFLabel** titleLabel, PDFAppParagraph** textLabel, int textPixelSize, QWidget* parent)
    {
        PDFTintedCard* card = new PDFTintedCard(hue, parent);
        card->setRadius(CALLOUT_RADIUS);

        QVBoxLayout* layout = new QVBoxLayout(card);
        layout->setContentsMargins(CALLOUT_PADDING_H, CALLOUT_PADDING_V, CALLOUT_PADDING_H, CALLOUT_PADDING_V);
        layout->setSpacing(CALLOUT_GAP);

        QHBoxLayout* headLayout = new QHBoxLayout();
        headLayout->setContentsMargins(0, 0, 0, 0);
        headLayout->setSpacing(CALLOUT_GAP);

        PDFIconBadge* icon = new PDFIconBadge(iconName, 16, 16, card);
        icon->setColors(std::nullopt, PDFAccentHueColors::getColor(hue, PDFAccentHueColors::Role::Icon));
        headLayout->addWidget(icon, 0, Qt::AlignVCenter);

        *titleLabel = PDFAppPageWidgets::createLabel(QString(), PDFThemeFonts::Role::SmallSemiBold, PDFThemeColors::TextPrimary, card);
        headLayout->addWidget(*titleLabel, 1);
        layout->addLayout(headLayout);

        *textLabel = PDFAppPageWidgets::createText(QString(), PDFThemeFonts::getFont(QFont::Normal, textPixelSize), 16, PDFThemeColors::TextSecondary, card);

        // A reason of the system may quote a long path: it never widens the card
        (*textLabel)->setMinimumWidth(1);
        layout->addWidget(*textLabel);
        return card;
    }

    /// A button of the dialog
    static QPushButton* createButton(const QString& text, const QString& iconName, QWidget* parent)
    {
        QPushButton* button = new QPushButton(text, parent);
        if (!iconName.isEmpty())
        {
            button->setIcon(PDFThemeIcons::getIcon(iconName));
        }
        return button;
    }

    /// Name of a file with the extension of a PDF
    static QString withPdfExtension(const QString& fileName)
    {
        return QFileInfo(fileName).suffix().compare(QLatin1String("pdf"), Qt::CaseInsensitive) == 0 ? fileName : fileName + QStringLiteral(".pdf");
    }
};

// ---------------------------------------------------------------- folder

PDFAppToolPathLabel::PDFAppToolPathLabel(QWidget* parent) :
    BaseClass(QString(), PDFThemeFonts::Role::Small, PDFThemeColors::TextSecondary, parent)
{
    setTextFormat(Qt::PlainText);
    setMinimumWidth(1);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);

    // A path is data: the ellipsis at its end also in a right-to-left interface
    setDataText(true);
}

PDFAppToolPathLabel::~PDFAppToolPathLabel() = default;

void PDFAppToolPathLabel::setPath(const QString& path)
{
    m_path = path;
    setToolTip(path);
    setAccessibleName(path);
    updateText();
}

void PDFAppToolPathLabel::resizeEvent(QResizeEvent* event)
{
    BaseClass::resizeEvent(event);
    updateText();
}

void PDFAppToolPathLabel::updateText()
{
    setText(PDFComponentPainter::elidedTextOfKind(m_path, font(), width(), PDFComponentPainter::TextKind::FileName));
}

// ---------------------------------------------------------------- option

PDFAppToolResultOption::PDFAppToolResultOption(QWidget* parent) :
    BaseClass(parent)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(PADDING_H, PADDING_V, PADDING_H, PADDING_V);
    layout->setSpacing(4);

    QHBoxLayout* topLayout = new QHBoxLayout();
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->setSpacing(DETAIL_SPACING);

    m_radioButton = new QRadioButton(this);
    topLayout->addWidget(m_radioButton, 0, Qt::AlignVCenter);
    connect(m_radioButton, &QRadioButton::toggled, this, &PDFAppToolResultOption::onRadioToggled);

    m_tag = new PDFTag(this);
    m_tag->setClickable(false);
    m_tag->setVisible(false);
    topLayout->addWidget(m_tag, 0, Qt::AlignVCenter);
    topLayout->addStretch(1);
    layout->addLayout(topLayout);

    QHBoxLayout* detailLayout = new QHBoxLayout();
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->setSpacing(0);
    detailLayout->addSpacing(DETAIL_INDENT);

    QVBoxLayout* columnLayout = new QVBoxLayout();
    columnLayout->setContentsMargins(0, 0, 0, 0);
    columnLayout->setSpacing(DETAIL_SPACING);

    m_description = PDFAppPageWidgets::createText(QString(), PDFThemeFonts::getFont(QFont::Normal, 12), DESCRIPTION_LINE, PDFThemeColors::TextSecondary, this);
    m_description->setObjectName(QStringLiteral("toolResultOptionDescription"));

    // The name of a file in the text may be long: it never widens the card
    m_description->setMinimumWidth(1);
    columnLayout->addWidget(m_description);

    m_controls = new QWidget(this);
    m_controlsLayout = new QVBoxLayout(m_controls);
    m_controlsLayout->setContentsMargins(0, 0, 0, 0);
    m_controlsLayout->setSpacing(DETAIL_SPACING);
    m_controls->setVisible(false);
    columnLayout->addWidget(m_controls);
    detailLayout->addLayout(columnLayout, 1);
    layout->addLayout(detailLayout);
}

PDFAppToolResultOption::~PDFAppToolResultOption() = default;

void PDFAppToolResultOption::setTexts(const QString& title, const QString& description)
{
    m_radioButton->setText(title);
    m_radioButton->setAccessibleDescription(description);
    m_description->setText(description);
}

QString PDFAppToolResultOption::getDescription() const
{
    return m_description->text();
}

void PDFAppToolResultOption::setTagText(const QString& text)
{
    m_tag->setText(text);
    m_tag->setVisible(!text.isEmpty());
}

bool PDFAppToolResultOption::isSelected() const
{
    return m_radioButton->isChecked();
}

void PDFAppToolResultOption::onRadioToggled(bool checked)
{
    if (m_controlsLayout->count() > 0)
    {
        m_controls->setVisible(checked);
    }
    update();
    Q_EMIT selectionChanged();
}

void PDFAppToolResultOption::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    const QRectF cardRect(rect());
    const PDFStylePainter::Radii radii(RADIUS);
    PDFStylePainter::fill(&painter, cardRect, radii, PDFThemeColors::BgSurface);
    if (isSelected())
    {
        // bg/accent 13 % over bg/surface (312:7400)
        QColor accent = PDFStylePainter::color(PDFThemeColors::BgAccent);
        accent.setAlphaF(0.13f);
        PDFStylePainter::fill(&painter, cardRect, radii, accent);
    }
    PDFStylePainter::strokeInside(&painter, cardRect, radii, isSelected() ? PDFThemeColors::BorderFocus : PDFThemeColors::BorderDefault);
}

void PDFAppToolResultOption::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && !isSelected() && m_radioButton->isEnabled())
    {
        m_radioButton->setChecked(true);
        m_radioButton->setFocus(Qt::MouseFocusReason);
        event->accept();
        return;
    }
    BaseClass::mousePressEvent(event);
}

// ---------------------------------------------------------------- rules

PDFAppToolResultDialog::ResultKind PDFAppToolResultDialog::getResultKind(const PDFDocumentToolResult& result)
{
    for (const PDFDocumentToolOutputDocument& output : result.documents)
    {
        if (output.kind == PDFDocumentToolOutputDocument::Kind::Changed)
        {
            return ResultKind::ChangeDocument;
        }
    }
    if (!result.documents.empty())
    {
        return ResultKind::NewDocument;
    }
    return result.outputs.empty() ? ResultKind::Analysis : ResultKind::Files;
}

std::vector<PDFAppToolResultDialog::Choice> PDFAppToolResultDialog::getChoices(const PDFDocumentToolCatalogEntry* tool, ResultKind kind, const DocumentState& state)
{
    switch (kind)
    {
        case ResultKind::ChangeDocument:
        {
            // O31: a tool, which rewrites the file, cannot keep a signature; only a copy
            // says so honestly. Without a file nothing is signed by this rule (F5).
            const bool isCopyOnly = PDFAppToolResultWriter::isCopyOnly(tool, state.isSigned, state.hasFile, state.isOnlyCopyWritable);
            std::vector<Choice> choices;
            if (!isCopyOnly && !state.isLocked)
            {
                choices.push_back(Choice::Keep);
            }
            choices.push_back(Choice::Copy);
            if (!isCopyOnly && !state.isLocked && state.hasFile)
            {
                choices.push_back(Choice::Overwrite);
            }
            return choices;
        }

        case ResultKind::NewDocument:
            return { Choice::NewTab, Choice::File };

        case ResultKind::Files:
            return { Choice::File, Choice::Clipboard };

        case ResultKind::Analysis:
            break;
    }
    return { };
}

PDFAppToolResultDialog::Choice PDFAppToolResultDialog::getPreselectedChoice(const PDFDocumentToolCatalogEntry* tool, const std::vector<Choice>& choices)
{
    Choice preselected = choices.empty() ? Choice::Keep : choices.front();
    if (tool)
    {
        switch (tool->getPreselectedResult())
        {
            case PDFDocumentToolPreselectedResult::Keep:
                preselected = Choice::Keep;
                break;
            case PDFDocumentToolPreselectedResult::Copy:
                preselected = Choice::Copy;
                break;
            case PDFDocumentToolPreselectedResult::NewTab:
                preselected = Choice::NewTab;
                break;
            case PDFDocumentToolPreselectedResult::File:
                preselected = Choice::File;
                break;
            case PDFDocumentToolPreselectedResult::None:
                break;
        }
    }

    return std::find(choices.cbegin(), choices.cend(), preselected) != choices.cend() ? preselected : (choices.empty() ? Choice::Keep : choices.front());
}

// ---------------------------------------------------------------- dialog

PDFAppToolResultDialog::PDFAppToolResultDialog(PDFAppContext* context, PDFAppDocumentTab* tab, PDFDocumentToolRunner* runner, PDFAppToolResultInput input, QWidget* parent) :
    BaseClass(PDFThemeIcons::Badge::Success, QString(), QString(), QDialogButtonBox::NoButton, parent),
    m_context(context),
    m_tab(tab),
    m_runner(runner),
    m_input(std::move(input))
{
    setObjectName(QStringLiteral("toolResultDialog"));
    setCardWidth(CARD_WIDTH);

    m_kind = getResultKind(m_input.result);
    m_isPartial = !m_input.result.leftOutInputs.empty() && m_kind != ResultKind::Analysis;
    if (m_runner)
    {
        m_snapshot = m_runner->getResultSnapshot();

        // F5: a changed document of a display version cannot be kept nor written. The shell
        // does not run such a tool; a dialog opened anyway says so, it never does nothing
        if (m_snapshot.isPagedXFA && m_kind == ResultKind::ChangeDocument)
        {
            m_isDisplayVersion = true;
            m_kind = ResultKind::Analysis;
        }
        connect(m_runner, &QObject::destroyed, this, &PDFAppToolResultDialog::onRunnerGone);
        connect(m_runner, &PDFDocumentToolRunner::documentLost, this, &PDFAppToolResultDialog::onRunnerGone);
    }

    const PDFAppDocument* document = m_runner ? m_runner->getDocument() : nullptr;
    m_state.hasFile = document && !document->getFilePath().isEmpty();
    m_state.isSigned = m_snapshot.isValid() && PDFForm::getSignatureCount(m_snapshot.document.data()) > 0;
    m_state.isLocked = document && document->isLocked();
    m_state.isOnlyCopyWritable = m_snapshot.isValid() && PDFAppDocumentFileWriter::isOnlyCopyWritable(m_snapshot);

    m_writer = new PDFAppToolResultWriter(this);
    connect(m_writer, &PDFAppToolResultWriter::finished, this, &PDFAppToolResultDialog::onWriterFinished);

    createHead();
    createBody();

    m_discardButton = addActionButton(QString(), ActionPosition::Leading, false);
    m_discardButton->setObjectName(QStringLiteral("discardResultButton"));
    m_discardButton->setIcon(PDFThemeIcons::getIcon(QStringLiteral("trash")));
    connect(m_discardButton, &QPushButton::clicked, this, &PDFAppToolResultDialog::onDiscardClicked);

    m_confirmButton = addActionButton(QString(), ActionPosition::Trailing, true);
    m_confirmButton->setObjectName(QStringLiteral("confirmResultButton"));
    connect(m_confirmButton, &QPushButton::clicked, this, &PDFAppToolResultDialog::onConfirmClicked);

    // An analysis has nothing to discard
    m_discardButton->setVisible(m_kind != ResultKind::Analysis);

    updateTexts();
    updateContinueVisibility();
    updateConfirmButton();
}

PDFAppToolResultDialog::~PDFAppToolResultDialog()
{
    // Deleted without a decision: the result is gone, the runner must not hold the
    // document for it (the deletion of the runner itself comes here decided)
    if (!m_isDecided && m_runner)
    {
        m_runner->endDecision();
    }
}

PDFAppToolResultDialog* PDFAppToolResultDialog::open(PDFAppDocumentTab* tab, PDFDocumentToolRunner* runner, PDFAppToolResultInput input)
{
    QWidget* window = tab ? tab->window() : nullptr;
    PDFAppToolResultDialog* dialog = new PDFAppToolResultDialog(tab ? tab->getContext() : nullptr, tab, runner, std::move(input), window);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->QDialog::open();
    return dialog;
}

void PDFAppToolResultDialog::createHead()
{
    using Helper = PDFAppToolResultDialogHelper;

    // The head of 312:7375: the texts have their natural lines (19 + 2 + 15), the
    // head of the message box has the lines of its text of 16
    QWidget* head = new QWidget(this);
    head->setObjectName(QStringLiteral("toolResultHead"));
    QHBoxLayout* headLayout = new QHBoxLayout(head);
    headLayout->setContentsMargins(0, 0, 0, 0);
    headLayout->setSpacing(Helper::HEAD_GAP);

    // A partial result has the warning of 378:4376 (alert-triangle in amber)
    PDFIconBadge* badge = new PDFIconBadge(m_isPartial ? QStringLiteral("alert-triangle") : QStringLiteral("check"), Helper::HEAD_BADGE, Helper::HEAD_ICON, head);
    badge->setObjectName(QStringLiteral("toolResultBadge"));
    badge->setHue(m_isPartial ? PDFAccentHue::Amber : PDFAccentHue::Green);
    headLayout->addWidget(badge, 0, Qt::AlignVCenter);

    QVBoxLayout* textLayout = new QVBoxLayout();
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(Helper::HEAD_TEXT_GAP);
    m_titleLabel = PDFAppPageWidgets::createText(QString(), PDFThemeFonts::getFont(PDFThemeFonts::Role::DialogTitle), 19, PDFThemeColors::TextPrimary, head);
    m_titleLabel->setObjectName(QStringLiteral("toolResultTitle"));
    m_subtitleLabel = PDFAppPageWidgets::createText(QString(), PDFThemeFonts::getFont(QFont::Normal, 12), 15, PDFThemeColors::TextSecondary, head);
    m_subtitleLabel->setObjectName(QStringLiteral("toolResultSubtitle"));
    if (m_input.reportText.isEmpty())
    {
        textLayout->addWidget(m_titleLabel);
    }
    else
    {
        // No frame of the design: the link Copy report of the panel (303:5373) cannot get a
        // click beside this modal dialog, so the dialog has it too, at the trailing edge of the
        // line of its title (the head keeps its height)
        QHBoxLayout* titleLayout = new QHBoxLayout();
        titleLayout->setContentsMargins(0, 0, 0, 0);
        titleLayout->setSpacing(Helper::HEAD_GAP);
        titleLayout->addWidget(m_titleLabel, 1);
        m_reportLink = new PDFTextLink(head);
        m_reportLink->setObjectName(QStringLiteral("toolResultReportLink"));
        connect(m_reportLink.data(), &PDFTextLink::clicked, this, &PDFAppToolResultDialog::onReportLinkClicked);
        titleLayout->addWidget(m_reportLink, 0, Qt::AlignVCenter);
        textLayout->addLayout(titleLayout);
    }
    textLayout->addWidget(m_subtitleLabel);
    headLayout->addLayout(textLayout, 1);
    setHeadWidget(head);
}

void PDFAppToolResultDialog::createBody()
{
    using Helper = PDFAppToolResultDialogHelper;

    // A low window scrolls the facts, the options and the next tool; the head and
    // the buttons stay
    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setObjectName(QStringLiteral("toolResultBody"));
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    PDFWidgetStyleProperties::set(scrollArea, PDFWidgetStyleProperties::OVERLAY_SCROLL_BARS);
    m_scrollArea = scrollArea;

    QWidget* body = new QWidget(scrollArea);
    body->setObjectName(QStringLiteral("toolResultBodyContent"));
    m_body = body;
    QVBoxLayout* layout = new QVBoxLayout(body);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(Helper::SECTION_GAP);

    createFacts(layout, body);
    createOptions(layout, body);
    createContinueWith(layout, body);
    layout->addStretch(1);

    scrollArea->setWidget(body);
    addContent(scrollArea);
}

void PDFAppToolResultDialog::createFacts(QVBoxLayout* layout, QWidget* parent)
{
    using Helper = PDFAppToolResultDialogHelper;

    if (!m_input.result.facts.empty() || m_isPartial)
    {
        PDFAppToolResultFactsPanel* panel = new PDFAppToolResultFactsPanel(parent);
        panel->setObjectName(QStringLiteral("toolResultFacts"));
        m_factsPanel = panel;
        QVBoxLayout* panelLayout = new QVBoxLayout(panel);
        panelLayout->setContentsMargins(12, 4, 12, 4);
        panelLayout->setSpacing(0);

        m_facts = new PDFKeyValueList(panel);
        for (const PDFDocumentToolFact& fact : m_input.result.facts)
        {
            m_facts->addRow(fact.title, fact.value);
        }

        // 378:4376: Left out "Annex_A.pdf · 9 pages", a row per input not in the result
        for (const PDFDocumentToolLeftOutInput& input : m_input.result.leftOutInputs)
        {
            const int pageCount = int(qBound(PDFInteger(0), input.pageCount, PDFInteger(std::numeric_limits<int>::max())));
            const QString pages = pageCount == 1 ? tr("%Ln page", nullptr, pageCount) : tr("%Ln pages", nullptr, pageCount);
            m_facts->addRow(tr("Left out"), pageCount > 0 ? tr("%1 · %2").arg(input.name.left(256), pages) : input.name.left(256));
        }
        panelLayout->addWidget(m_facts);
        layout->addWidget(panel);
    }

    // 378:4376: the amber callout of the reason below the facts - callout-amber 378:5028 is the
    // compact callout (radius 8, paddings 10 / 12, the head and the text 4 apart, the text
    // Regular 11 in lines of 13)
    if (m_isPartial)
    {
        PDFAppToolCallout* callout = new PDFAppToolCallout(PDFAccentHue::Amber, QStringLiteral("alert-triangle"), QString(), QString(), parent);
        callout->setCompactStyle();
        callout->setObjectName(QStringLiteral("partialWarning"));
        callout->getTitleLabel()->setObjectName(QStringLiteral("partialWarningTitle"));
        callout->getTextLabel()->setObjectName(QStringLiteral("partialWarningText"));
        // The reason comes with updateTexts: the text is not hidden as an empty one
        callout->getTextLabel()->setVisible(true);
        m_partialWarning = callout;
        layout->addWidget(m_partialWarning);
    }

    // The warning of chapter 4 below the facts (O31): no frame of the design, the
    // parts of 295:2621
    if (m_kind == ResultKind::ChangeDocument && (PDFAppToolResultWriter::isSignatureLost(m_input.tool, m_state.isSigned, m_state.isOnlyCopyWritable) ||
                                                 PDFAppToolResultWriter::isCopyOnly(m_input.tool, m_state.isSigned, m_state.hasFile, m_state.isOnlyCopyWritable)))
    {
        PDFLabel* title = nullptr;
        PDFAppParagraph* text = nullptr;
        m_signedWarning = Helper::createCallout(PDFAccentHue::Amber, QStringLiteral("alert-triangle"), &title, &text, 12, parent);
        m_signedWarning->setObjectName(QStringLiteral("signedWarning"));
        title->setObjectName(QStringLiteral("signedWarningTitle"));
        text->setObjectName(QStringLiteral("signedWarningText"));
        layout->addWidget(m_signedWarning);
    }
}

PDFAppToolResultOption* PDFAppToolResultDialog::addOption(Choice choice, QVBoxLayout* layout, QWidget* parent)
{
    PDFAppToolResultOption* option = new PDFAppToolResultOption(parent);
    m_options[choice] = option;
    m_choiceGroup->addButton(option->getRadioButton(), int(choice));
    layout->addWidget(option);
    connect(option, &PDFAppToolResultOption::selectionChanged, this, &PDFAppToolResultDialog::onChoiceToggled);
    return option;
}

void PDFAppToolResultDialog::createFileControls(PDFAppToolResultOption* option, bool isFullPath, bool isChecked)
{
    using Helper = PDFAppToolResultDialogHelper;

    QVBoxLayout* layout = option->getControlsLayout();
    QWidget* parent = layout->parentWidget();

    m_folder = getInitialFolder();
    m_consentedPath = m_folder;

    if (isFullPath)
    {
        // The whole path in one field and Browse… (355:9882)
        QHBoxLayout* row = new QHBoxLayout();
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(Helper::PATH_GAP);
        m_filePathField = new PDFLineEdit(parent);
        m_filePathField->setObjectName(QStringLiteral("resultFilePath"));
        m_filePathField->setFixedHeight(Helper::PATH_HEIGHT);
        m_filePathField->setText(QDir::toNativeSeparators(QDir(m_folder).absoluteFilePath(getSuggestedFileName())));
        row->addWidget(m_filePathField, 1);
        m_browseButton = Helper::createButton(QString(), QString(), parent);
        m_browseButton->setAutoDefault(false);
        row->addWidget(m_browseButton);
        layout->addLayout(row);
        connect(m_filePathField, &QLineEdit::textEdited, this, &PDFAppToolResultDialog::onTargetEdited);
    }
    else
    {
        // The name in its field, the folder and Browse… below it (312:7410, 809:140624)
        m_fileNameField = new PDFLineEdit(parent);
        m_fileNameField->setObjectName(QStringLiteral("resultFileName"));
        m_fileNameField->setFixedHeight(Helper::FIELD_HEIGHT);
        m_fileNameField->setText(getSuggestedFileName());
        layout->addWidget(m_fileNameField);
        connect(m_fileNameField, &QLineEdit::textEdited, this, &PDFAppToolResultDialog::onTargetEdited);

        QHBoxLayout* row = new QHBoxLayout();
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(PDFAppToolResultOption::DETAIL_SPACING);
        m_folderLabel = new PDFAppToolPathLabel(parent);
        m_folderLabel->setObjectName(QStringLiteral("resultFolder"));
        m_folderLabel->setPath(QDir::toNativeSeparators(m_folder));
        row->addWidget(m_folderLabel, 1, Qt::AlignVCenter);
        m_browseButton = Helper::createButton(QString(), QStringLiteral("folder-open"), parent);
        m_browseButton->setAutoDefault(false);
        row->addWidget(m_browseButton);
        layout->addLayout(row);
    }
    m_browseButton->setObjectName(QStringLiteral("browseButton"));
    connect(m_browseButton, &QPushButton::clicked, this, &PDFAppToolResultDialog::onBrowseClicked);

    // The reason of a refused name or of an existing file: below the name, in the
    // text of 448:15959
    m_nameError = PDFAppPageWidgets::createLabel(QString(), PDFThemeFonts::Role::CaptionRegular, PDFThemeColors::TextDanger, parent);
    m_nameError->setObjectName(QStringLiteral("resultNameError"));
    m_nameError->setTextFormat(Qt::PlainText);
    m_nameError->setWordWrap(true);
    m_nameError->setMinimumWidth(1);
    m_nameError->setVisible(false);
    layout->insertWidget(1, m_nameError);

    m_openCheckBox = new QCheckBox(parent);
    m_openCheckBox->setObjectName(QStringLiteral("resultOpenCheck"));
    m_openCheckBox->setChecked(isChecked);
    layout->addWidget(m_openCheckBox);
    connect(m_openCheckBox, &QCheckBox::toggled, this, &PDFAppToolResultDialog::onOpenCheckToggled);
}

void PDFAppToolResultDialog::createOptions(QVBoxLayout* layout, QWidget* parent)
{
    using Helper = PDFAppToolResultDialogHelper;

    const std::vector<Choice> choices = getChoices(m_input.tool, m_kind, m_state);
    if (choices.empty())
    {
        return;
    }

    m_choiceGroup = new QButtonGroup(this);
    m_choiceGroup->setExclusive(true);

    QVBoxLayout* optionsLayout = new QVBoxLayout();
    optionsLayout->setContentsMargins(0, 0, 0, 0);
    optionsLayout->setSpacing(Helper::OPTION_GAP);

    for (const Choice choice : choices)
    {
        PDFAppToolResultOption* option = addOption(choice, optionsLayout, parent);
        switch (choice)
        {
            case Choice::Keep:
                option->setObjectName(QStringLiteral("optionKeep"));
                break;
            case Choice::Copy:
                option->setObjectName(QStringLiteral("optionCopy"));
                createFileControls(option, false, true);
                break;
            case Choice::Overwrite:
                option->setObjectName(QStringLiteral("optionOverwrite"));
                break;
            case Choice::NewTab:
                option->setObjectName(QStringLiteral("optionNewTab"));
                break;
            case Choice::File:
                option->setObjectName(QStringLiteral("optionFile"));
                // A file of an export has the whole path (339:10301), the frame of a
                // new document does not draw it: the controls of a copy
                createFileControls(option, m_kind == ResultKind::Files, m_kind != ResultKind::Files);
                break;
            case Choice::Clipboard:
                option->setObjectName(QStringLiteral("optionClipboard"));
                break;
        }
    }
    layout->addLayout(optionsLayout);

    m_selectedChoice = getPreselectedChoice(m_input.tool, choices);
    if (PDFAppToolResultOption* option = getOption(m_selectedChoice))
    {
        option->getRadioButton()->setChecked(true);
    }
}

void PDFAppToolResultDialog::createContinueWith(QVBoxLayout* layout, QWidget* parent)
{
    // Not for an export (K20), not for an analysis, which has no result to go on with, and
    // not for a partial result (tools.md 9.3 point 12, 378:4376)
    if ((m_kind != ResultKind::ChangeDocument && m_kind != ResultKind::NewDocument) || m_isPartial)
    {
        return;
    }

    m_continueWith = new PDFContinueWith(parent);
    m_continueWith->setObjectName(QStringLiteral("toolResultContinueWith"));

    // The scroll area takes the larger of the height for the width and of the minimal
    // height of its widget, and that one is of the smallest width with more wrapped lines
    m_continueWith->setMinimumHeight(1);

    // Its minimal width has the longest word of each name: a long word of another language is
    // elided in its tile, the row is never wider than the card, which does not scroll sideways
    m_continueWith->setMinimumWidth(1);
    connect(m_continueWith, &PDFContinueWith::selectedChoiceChanged, this, &PDFAppToolResultDialog::onContinueChanged);
    connect(m_continueWith, &PDFContinueWith::anotherToolRequested, this, &PDFAppToolResultDialog::onAnotherToolRequested);

    if (m_input.tool)
    {
        // Every tool of the catalog (owner answer B): the suggestions are not limited to
        // the implemented ones
        QStringList toolIds = m_input.tool->getDefaultSuccessors();
        if (m_context && m_context->getDatabase())
        {
            PDFDocumentToolAvailability availability;
            availability.hasDocument = true;
            availability.isDocumentLocked = m_state.isLocked;
            toolIds = PDFDocumentToolUsage(m_context->getDatabase()).getSuggestions(m_input.tool->getId(), availability);
        }

        QList<PDFContinueWith::Choice> continueChoices;
        for (const QString& toolId : toolIds)
        {
            if (const PDFDocumentToolCatalogEntry* tool = PDFDocumentToolCatalog::findTool(toolId))
            {
                PDFContinueWith::Choice choice;
                choice.name = tool->getTitle();
                choice.iconName = tool->getIconName();
                choice.hue = PDFAppToolPresentation::getHue(tool->getHue());
                continueChoices << choice;
                m_continueToolIds << toolId;
            }
        }
        m_continueWith->setChoices(continueChoices);
    }
    layout->addWidget(m_continueWith);
}

PDFAppToolResultOption* PDFAppToolResultDialog::getOption(Choice choice) const
{
    const auto it = m_options.find(choice);
    return it != m_options.cend() ? it->second.data() : nullptr;
}

void PDFAppToolResultDialog::selectChoice(Choice choice)
{
    if (PDFAppToolResultOption* option = getOption(choice))
    {
        option->getRadioButton()->setChecked(true);
    }
}

QString PDFAppToolResultDialog::getDefaultTitle() const
{
    const QString toolTitle = m_input.tool ? m_input.tool->getTitle() : QString();
    if (m_isPartial)
    {
        const int count = getLeftOutCount();
        return count == 1 ? tr("%1 finished without %Ln file", nullptr, count).arg(toolTitle) : tr("%1 finished without %Ln files", nullptr, count).arg(toolTitle);
    }
    return tr("%1 finished").arg(toolTitle);
}

int PDFAppToolResultDialog::getLeftOutCount() const
{
    return int(qMin<size_t>(m_input.result.leftOutInputs.size(), size_t(std::numeric_limits<int>::max())));
}

QString PDFAppToolResultDialog::getDefaultSubtitle() const
{
    if (m_isPartial)
    {
        return tr("The new document is ready, but it is not complete.");
    }

    switch (m_kind)
    {
        case ResultKind::ChangeDocument:
            return tr("Nothing has been written yet – choose what to do with the result.");
        case ResultKind::NewDocument:
            return tr("The new document is ready – choose what to do with it.");
        case ResultKind::Files:
            return tr("Nothing has been written yet – choose what to do with the result.");
        case ResultKind::Analysis:
            return tr("Nothing has been changed.");
    }
    return QString();
}

void PDFAppToolResultDialog::updateContinueNames()
{
    if (!m_continueWith || m_continueToolIds.isEmpty())
    {
        return;
    }

    // The names come from the catalog in the current language; the chosen tool stays
    QList<PDFContinueWith::Choice> choices = m_continueWith->getChoices();
    for (qsizetype i = 0; i < choices.size() && i < m_continueToolIds.size(); ++i)
    {
        if (const PDFDocumentToolCatalogEntry* tool = PDFDocumentToolCatalog::findTool(m_continueToolIds[i]))
        {
            choices[i].name = tool->getTitle();
        }
    }

    const int selected = m_continueWith->getSelectedChoice();
    m_continueWith->setChoices(choices);
    m_continueWith->setSelectedChoice(selected);
}

void PDFAppToolResultDialog::updateTexts()
{
    const QString title = m_input.title.isEmpty() ? getDefaultTitle() : m_input.title;
    const QString subtitle = m_isDisplayVersion ? PDFAppDocument::getPagedXFARefusalText() : (m_input.subtitle.isEmpty() ? getDefaultSubtitle() : m_input.subtitle);
    setTitle(title);
    setText(subtitle);
    m_titleLabel->setText(title);
    m_subtitleLabel->setText(subtitle);
    if (m_reportLink)
    {
        m_reportLink->setText(m_isReportCopied ? tr("Report copied") : tr("Copy report"));
    }

    if (m_signedWarning)
    {
        // The strings of chapter 4 of the plan; without a file the result can be kept (F5), no
        // frame of the design (owner 2026-11-02)
        if (PDFLabel* warningTitle = m_signedWarning->findChild<PDFLabel*>(QStringLiteral("signedWarningTitle")))
        {
            warningTitle->setText(!m_state.isSigned ? tr("Only a copy can be saved")
                                                    : (m_state.hasFile ? tr("The signatures of the copy will not be valid") : tr("The signatures will stop being valid")));
        }
        if (PDFAppParagraph* warningText = m_signedWarning->findChild<PDFAppParagraph*>(QStringLiteral("signedWarningText")))
        {
            if (m_state.isOnlyCopyWritable)
            {
                // The file is damaged: the reason is not the tool
                warningText->setText(m_state.hasFile ? tr("The file is damaged, so PDF4QT does not change it. Only a copy can be saved; the open document and its file stay as they are.")
                                                     : tr("This document comes from a damaged file, so it cannot keep the signed revision. When you save the result, its signatures will no longer be valid."));
            }
            else
            {
                warningText->setText(m_state.hasFile ? tr("This tool rewrites the whole file, so it cannot keep the signed revision. Only a copy can be saved; the open document and its file stay as they are.")
                                                     : tr("This tool rewrites the whole file, so it cannot keep the signed revision. When you save the result, its signatures will no longer be valid."));
            }
        }
    }

    if (m_partialWarning && !m_input.result.leftOutInputs.empty())
    {
        // 378:4376 (K36 corrected): "Annex_A.pdf could not be copied" and the reason of the core (Q5: the copy of
        // the input failed during the run; the text of the frame about a change on the disk
        // is not used, K36)
        const std::vector<PDFDocumentToolLeftOutInput>& leftOut = m_input.result.leftOutInputs;
        const int count = getLeftOutCount();
        if (PDFLabel* warningTitle = m_partialWarning->findChild<PDFLabel*>(QStringLiteral("partialWarningTitle")))
        {
            warningTitle->setText(count == 1 ? tr("%1 could not be copied").arg(leftOut.front().name.left(256)) : tr("%Ln files could not be copied", nullptr, count));
        }
        if (PDFAppParagraph* warningText = m_partialWarning->findChild<PDFAppParagraph*>(QStringLiteral("partialWarningText")))
        {
            QStringList reasons;
            for (const PDFDocumentToolLeftOutInput& input : leftOut)
            {
                const QString reason = input.reason.isEmpty() ? tr("Its pages could not be copied.") : input.reason.left(512);
                reasons << (count == 1 ? reason : tr("%1: %2").arg(input.name.left(256), reason));
                if (reasons.size() >= MAXIMUM_LEFT_OUT_REASONS)
                {
                    break;
                }
            }
            reasons << tr("The other files are in the new document; Discard result returns to the list of files.");
            warningText->setText(reasons.join(QStringLiteral(" ")));
        }
    }

    const bool isCopyOnly = PDFAppToolResultWriter::isCopyOnly(m_input.tool, m_state.isSigned, m_state.hasFile, m_state.isOnlyCopyWritable);
    const Choice preselected = getPreselectedChoice(m_input.tool, getChoices(m_input.tool, m_kind, m_state));
    const QString documentFileName = QFileInfo(m_snapshot.filePath).fileName();
    for (const auto& item : m_options)
    {
        PDFAppToolResultOption* option = item.second.data();
        if (!option)
        {
            continue;
        }

        switch (item.first)
        {
            case Choice::Keep:
                option->setTexts(tr("Keep in this tab"), tr("The result replaces the document in this tab. It is not saved yet and Undo brings the original back."));
                break;
            case Choice::Copy:
                option->setTexts(tr("Save as a copy"), tr("A new file is written, the open document and its file stay as they are."));
                break;
            case Choice::Overwrite:
                option->setTexts(tr("Overwrite the original file"), tr("The result replaces the document and is saved to %1 right away.").arg(documentFileName));
                break;
            case Choice::NewTab:
                option->setTexts(tr("Open in a new tab"), tr("The new document opens unsaved. Save it when you are ready."));
                break;
            case Choice::File:
                if (m_kind == ResultKind::Files)
                {
                    option->setTexts(tr("Save as a file"), m_input.fileDescription.isEmpty() ? tr("The result is written to a new file. The document is not changed.") : m_input.fileDescription);
                }
                else
                {
                    option->setTexts(tr("Save as a file"), tr("The document is written to a file of your choice."));
                }
                break;
            case Choice::Clipboard:
                option->setTexts(tr("Copy to clipboard"), tr("The whole text is put on the clipboard. Nothing is written to disk."));
                break;
        }

        // A single choice recommends nothing (chapter 4), 378:3674 has no tag
        option->setTagText(!isCopyOnly && m_kind != ResultKind::NewDocument && item.first == preselected ? tr("Recommended") : QString());
    }

    if (m_browseButton)
    {
        m_browseButton->setText(tr("Browse…"));
    }
    if (m_openCheckBox)
    {
        m_openCheckBox->setText(m_kind == ResultKind::Files ? tr("Open the folder afterwards") : (m_kind == ResultKind::NewDocument ? tr("Open the file in a new tab") : tr("Open the copy in a new tab")));
    }
    if (m_discardButton)
    {
        m_discardButton->setText(tr("Discard result"));
    }
    if (m_writeErrorTitle)
    {
        m_writeErrorTitle->setText(getFailedWriteTitle());
    }
    if (m_chooseLocationButton)
    {
        m_chooseLocationButton->setText(tr("Choose location…"));
    }
    if (m_continueWith)
    {
        m_continueWith->setTitle(tr("Then continue with"));
        m_continueWith->setTitleNote(tr("optional"));
        m_continueWith->setLinkText(tr("Another tool…"));
        m_continueWith->setCaption(tr("Nothing is chosen – the tool just finishes."));
        m_continueWith->setCurrentToolName(m_input.tool ? m_input.tool->getTitle() : QString());
        updateContinueNames();
    }
    if (m_nameError && m_isReplaceConfirmed)
    {
        m_nameError->setText(tr("A file of this name exists already. Replace it, or enter another name."));
    }
    updateConfirmButton();
}

QString PDFAppToolResultDialog::getFailedWriteTitle() const
{
    if (m_isFileWrittenError || m_selectedChoice == Choice::Keep)
    {
        return tr("The document in this tab was not changed");
    }
    return m_selectedChoice == Choice::Copy ? tr("The copy could not be written") : tr("The file could not be written");
}

QString PDFAppToolResultDialog::getConfirmText() const
{
    if (m_isReplaceConfirmed)
    {
        return tr("Replace File");
    }

    const bool isContinuing = m_continueWith && !m_continueWith->isHidden() && m_continueWith->getSelectedChoice() >= 0;
    if (m_kind == ResultKind::Analysis)
    {
        return tr("Close");
    }

    switch (m_selectedChoice)
    {
        case Choice::Keep:
            return isContinuing ? tr("Keep and Continue") : tr("Keep in This Tab");
        case Choice::Copy:
            return isContinuing ? tr("Save Copy and Continue") : tr("Save Copy");
        case Choice::Overwrite:
            return isContinuing ? tr("Overwrite and Continue") : tr("Overwrite File");
        case Choice::NewTab:
            return isContinuing ? tr("Open and Continue") : tr("Open Document");
        case Choice::File:
            if (isContinuing)
            {
                return tr("Save and Continue");
            }
            return m_input.fileConfirmText.isEmpty() ? tr("Save File") : m_input.fileConfirmText;
        case Choice::Clipboard:
            return tr("Copy to Clipboard");
    }
    return QString();
}

QString PDFAppToolResultDialog::getConfirmIconName() const
{
    if (m_kind == ResultKind::Analysis)
    {
        return QStringLiteral("check");
    }

    switch (m_selectedChoice)
    {
        case Choice::Keep:
            return QStringLiteral("check");
        case Choice::Copy:
            // 296:2761, 438:14342 (icon/save-as)
            return QStringLiteral("save-as");
        case Choice::Overwrite:
            return QStringLiteral("save");
        case Choice::NewTab:
            return QStringLiteral("external-link");
        case Choice::File:
            // 339:10323 confirms an export by the check mark
            return m_kind == ResultKind::Files ? QStringLiteral("check") : QStringLiteral("save");
        case Choice::Clipboard:
            return QStringLiteral("copy");
    }
    return QString();
}

void PDFAppToolResultDialog::updateConfirmButton()
{
    if (m_confirmButton)
    {
        m_confirmButton->setText(getConfirmText());
        m_confirmButton->setIcon(PDFThemeIcons::getIcon(getConfirmIconName()));
    }
}

void PDFAppToolResultDialog::updateContinueVisibility()
{
    if (!m_continueWith)
    {
        return;
    }

    // Then continue with runs on the opened copy: without it there is nothing to run
    // on (F11)
    const bool isFileChoice = m_selectedChoice == Choice::Copy || m_selectedChoice == Choice::File;
    const bool isOffered = !isFileChoice || !m_openCheckBox || m_openCheckBox->isChecked();
    if (!isOffered && m_continueWith->getSelectedChoice() >= 0)
    {
        m_continueWith->setSelectedChoice(-1);
    }
    m_continueWith->setVisible(isOffered);
}

bool PDFAppToolResultDialog::isLocationChoice() const
{
    return m_selectedChoice == Choice::Copy || m_selectedChoice == Choice::File;
}

void PDFAppToolResultDialog::onChoiceToggled()
{
    if (!m_choiceGroup)
    {
        return;
    }

    const int id = m_choiceGroup->checkedId();
    if (id >= 0)
    {
        m_selectedChoice = Choice(id);
    }

    clearStates();
    updateContinueVisibility();
    updateConfirmButton();
    adaptToHost();
}

void PDFAppToolResultDialog::onOpenCheckToggled()
{
    updateContinueVisibility();
    updateConfirmButton();
    adaptToHost();
}

void PDFAppToolResultDialog::onContinueChanged()
{
    updateConfirmButton();
}

void PDFAppToolResultDialog::onTargetEdited()
{
    clearStates();
    updateConfirmButton();
    adaptToHost();
}

void PDFAppToolResultDialog::setTargetInvalid(bool isInvalid)
{
    if (m_fileNameField)
    {
        m_fileNameField->setInvalid(isInvalid);
    }
    if (m_filePathField)
    {
        m_filePathField->setInvalid(isInvalid);
    }
}

void PDFAppToolResultDialog::clearStates()
{
    m_isReplaceConfirmed = false;
    m_isFileWrittenError = false;
    setTargetInvalid(false);
    if (m_nameError)
    {
        m_nameError->setVisible(false);
    }
    if (m_writeError)
    {
        m_writeError->setVisible(false);
    }
    if (!m_statusError.isEmpty())
    {
        m_statusError.clear();
        Q_EMIT statusTextChanged(getStatusText());
    }
}

QString PDFAppToolResultDialog::getInitialFolder() const
{
    // The folder of the document (T1); no file: the documents of the user
    if (!m_snapshot.filePath.isEmpty())
    {
        return QFileInfo(m_snapshot.filePath).absolutePath();
    }

    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return documents.isEmpty() ? QDir::homePath() : documents;
}

QString PDFAppToolResultDialog::getOriginFilePath() const
{
    const PDFAppDocument* document = m_runner ? m_runner->getDocument() : nullptr;
    return document ? document->getOriginFilePath() : m_snapshot.filePath;
}

const PDFDocumentToolOutputDocument* PDFAppToolResultDialog::findOutput(PDFDocumentToolOutputDocument::Kind kind) const
{
    for (const PDFDocumentToolOutputDocument& output : m_input.result.documents)
    {
        if (output.kind == kind && !output.document.isNull())
        {
            return &output;
        }
    }
    return nullptr;
}

QString PDFAppToolResultDialog::getSuggestedFileName() const
{
    QString name;
    if (m_kind == ResultKind::Files && !m_input.result.outputs.empty())
    {
        name = m_input.result.outputs.front().suggestedFileName;
    }
    else if (!m_input.result.documents.empty())
    {
        name = m_input.result.documents.front().suggestedFileName;
    }

    // The name comes from the name of a file (T2): a hostile one is not offered
    if (PDFAppToolPathRules::checkFileName(name) == PDFAppToolPathRules::NameProblem::None)
    {
        return name;
    }

    // A copy never takes the name of its document (the file of the tab is refused): the
    // default name of files.save_copy of an assistant
    const QString base = QFileInfo(m_snapshot.filePath).completeBaseName();
    const bool isSafe = !base.isEmpty() && PDFAppToolPathRules::checkFileName(base + QStringLiteral("_copy.pdf")) == PDFAppToolPathRules::NameProblem::None;
    if (m_kind == ResultKind::Files)
    {
        return (isSafe ? base : tr("Result")) + QStringLiteral(".txt");
    }
    return isSafe ? base + QStringLiteral("_copy.pdf") : tr("Result") + QStringLiteral(".pdf");
}

QString PDFAppToolResultDialog::getTargetFilePath() const
{
    if (m_filePathField)
    {
        return QDir::fromNativeSeparators(m_filePathField->text().trimmed());
    }

    if (m_fileNameField)
    {
        return QDir(m_folder).absoluteFilePath(PDFAppToolResultDialogHelper::withPdfExtension(m_fileNameField->text()));
    }
    return QString();
}

void PDFAppToolResultDialog::showNameError(const QString& text)
{
    if (m_nameError)
    {
        m_nameError->setText(text);
        m_nameError->setVisible(true);
    }
    setTargetInvalid(true);
    adaptToHost();
}

bool PDFAppToolResultDialog::checkTarget()
{
    QString fileName;
    if (m_filePathField)
    {
        // The field of the whole path: a typed location of another computer or a device
        // needs Browse…, the conscious choice of the location (T1)
        const QString path = getTargetFilePath();
        const QFileInfo info(path);
        const QString folder = info.path();
        if (!QDir::isAbsolutePath(path))
        {
            showNameError(tr("Enter the whole path of the file."));
            return false;
        }
        if (PDFAppToolPathRules::hasStream(QDir::cleanPath(path)))
        {
            showNameError(PDFAppToolPathRules::getNameProblemText(PDFAppToolPathRules::NameProblem::Stream));
            return false;
        }
        if (QDir::cleanPath(folder).compare(QDir::cleanPath(m_consentedPath), Qt::CaseInsensitive) != 0 &&
            path.compare(m_consentedPath, Qt::CaseInsensitive) != 0 && PDFAppToolPathRules::isDeviceOrNetworkPath(path))
        {
            showNameError(tr("This location is on another computer or on a device. Choose it by Browse…"));
            return false;
        }
        fileName = info.fileName();
    }
    else if (m_fileNameField)
    {
        // The typed name itself first: the extension added to "a.pdf." hides its dot
        const PDFAppToolPathRules::NameProblem typedProblem = PDFAppToolPathRules::checkFileName(m_fileNameField->text());
        if (typedProblem != PDFAppToolPathRules::NameProblem::None)
        {
            showNameError(PDFAppToolPathRules::getNameProblemText(typedProblem));
            return false;
        }
        fileName = PDFAppToolResultDialogHelper::withPdfExtension(m_fileNameField->text());
    }

    const PDFAppToolPathRules::NameProblem problem = PDFAppToolPathRules::checkFileName(fileName);
    if (problem != PDFAppToolPathRules::NameProblem::None)
    {
        showNameError(PDFAppToolPathRules::getNameProblemText(problem));
        return false;
    }

    if (isTargetOpenInTab(getTargetFilePath()))
    {
        showNameError(tr("This file is open in a tab. Choose another name."));
        return false;
    }
    return true;
}

bool PDFAppToolResultDialog::isTargetOpenInTab(const QString& filePath) const
{
    const PDFAppDocumentRegistry* registry = m_context ? m_context->getDocumentRegistry() : nullptr;
    if (!registry)
    {
        return false;
    }

    if (registry->findByCanonicalPath(filePath))
    {
        return true;
    }

    // The links are resolved only on this computer: the thread of the user interface
    // never waits for another one
    const QString canonicalPath = PDFAppToolPathRules::isDeviceOrNetworkPath(filePath) ? QString() : QFileInfo(filePath).canonicalFilePath();
    return registry->findByCanonicalPath(canonicalPath) != nullptr;
}

void PDFAppToolResultDialog::placeWriteError()
{
    // The error stays at the choice, which failed (tools.md 9.3 point 16): in the
    // controls of a choice, above its last check box (316:7764)
    using Helper = PDFAppToolResultDialogHelper;

    PDFAppToolResultOption* option = getOption(m_selectedChoice);
    if (!m_writeError)
    {
        PDFLabel* title = nullptr;
        PDFAppParagraph* text = nullptr;
        m_writeError = Helper::createCallout(PDFAccentHue::Red, QStringLiteral("alert-circle"), &title, &text, 11, this);
        m_writeError->setObjectName(QStringLiteral("writeError"));
        m_writeErrorTitle = title;
        m_writeErrorTitle->setObjectName(QStringLiteral("writeErrorTitle"));
        m_writeErrorText = text;
        m_writeErrorText->setObjectName(QStringLiteral("writeErrorText"));
    }
    if (!m_chooseLocationButton)
    {
        m_chooseLocationButton = new QPushButton(tr("Choose location…"), m_writeError);
        m_chooseLocationButton->setObjectName(QStringLiteral("chooseLocationButton"));

        // Secondary also with the focus: Enter is the confirmation of the dialog (316:7770)
        m_chooseLocationButton->setAutoDefault(false);
        static_cast<QVBoxLayout*>(m_writeError->layout())->addWidget(m_chooseLocationButton, 0, Qt::AlignLeading);
        connect(m_chooseLocationButton, &QPushButton::clicked, this, &PDFAppToolResultDialog::onChooseLocationClicked);
    }

    if (option)
    {
        QVBoxLayout* layout = option->getControlsLayout();
        layout->removeWidget(m_writeError);
        m_writeError->setParent(layout->parentWidget());
        const int checkIndex = m_openCheckBox ? layout->indexOf(m_openCheckBox) : -1;
        layout->insertWidget(checkIndex >= 0 ? checkIndex : layout->count(), m_writeError);
        layout->parentWidget()->setVisible(true);
    }
}

void PDFAppToolResultDialog::showWriteError(const QString& text, bool isFileWritten)
{
    m_isFileWrittenError = isFileWritten;
    placeWriteError();
    m_chooseLocationButton->setVisible(isLocationChoice());
    setTargetInvalid(isLocationChoice());
    m_writeErrorTitle->setText(getFailedWriteTitle());
    if (isFileWritten)
    {
        // The file has the bytes of the result: nothing may say, that nothing was changed
        m_writeErrorText->setText(tr("%1 The file has been written, this tab keeps the older version until it is refreshed.").arg(text));
    }
    else
    {
        m_writeErrorText->setText(isLocationChoice() ? tr("%1 Nothing was changed – the result is still waiting. Choose another location and save again.").arg(text)
                                                     : tr("%1 Nothing was changed – the result is still waiting.").arg(text));
    }
    m_writeError->setVisible(true);

    // 316:7293: "Compress: the copy could not be written – <the reason>"
    const QString toolTitle = m_input.tool ? m_input.tool->getTitle() : QString();
    QString reason = text.trimmed();
    if (reason.endsWith(QLatin1Char('.')))
    {
        reason.chop(1);
    }
    if (isFileWritten || m_selectedChoice == Choice::Keep)
    {
        m_statusError = tr("%1: the document in this tab was not changed – %2").arg(toolTitle, reason);
    }
    else
    {
        m_statusError = m_selectedChoice == Choice::Copy ? tr("%1: the copy could not be written – %2").arg(toolTitle, reason)
                                                         : tr("%1: the file could not be written – %2").arg(toolTitle, reason);
    }
    adaptToHost();
    Q_EMIT statusTextChanged(getStatusText());

    if (m_scrollArea)
    {
        m_scrollArea->ensureWidgetVisible(m_writeError);
    }
    if (m_chooseLocationButton->isVisible())
    {
        m_chooseLocationButton->setFocus(Qt::OtherFocusReason);
    }
}

QString PDFAppToolResultDialog::getStatusText() const
{
    if (!m_statusError.isEmpty())
    {
        return m_statusError;
    }
    if (!m_input.statusText.isEmpty())
    {
        return m_input.statusText;
    }

    const QString toolTitle = m_input.tool ? m_input.tool->getTitle() : QString();
    if (m_isPartial)
    {
        // 378:4376 "Merge finished without 1 file · …"
        const int count = getLeftOutCount();
        return count == 1 ? tr("%1 finished without %Ln file · the incomplete document is waiting for your decision", nullptr, count).arg(toolTitle)
                          : tr("%1 finished without %Ln files · the incomplete document is waiting for your decision", nullptr, count).arg(toolTitle);
    }
    return m_kind == ResultKind::NewDocument ? tr("%1 finished · the new document is waiting for your decision").arg(toolTitle)
                                             : tr("%1 finished · the result is waiting for your decision").arg(toolTitle);
}

bool PDFAppToolResultDialog::isWriting() const
{
    return m_isOverwriteRunning || (m_writer && m_writer->isWriting());
}

void PDFAppToolResultDialog::setBusy(bool isBusy)
{
    if (m_confirmButton)
    {
        m_confirmButton->setEnabled(!isBusy);
    }
    if (m_discardButton)
    {
        m_discardButton->setEnabled(!isBusy);
    }
    if (m_body)
    {
        m_body->setEnabled(!isBusy);
    }
}

void PDFAppToolResultDialog::onBrowseClicked()
{
    QPointer<PDFAppToolResultDialog> guard(this);
    if (m_filePathField)
    {
        // The native dialog runs its own event loop: the dialog may be deleted meanwhile
        const QString path = PDFAppFileDialogs::getPrintFileName(this, getTargetFilePath(), QFileInfo(getTargetFilePath()).suffix());
        if (!guard || path.isEmpty())
        {
            return;
        }

        m_consentedPath = path;
        m_filePathField->setText(QDir::toNativeSeparators(path));
    }
    else
    {
        const QString folder = PDFAppFileDialogs::getExistingDirectory(this, m_folder);
        if (!guard || folder.isEmpty())
        {
            return;
        }

        m_folder = folder;
        m_consentedPath = folder;
        if (m_folderLabel)
        {
            m_folderLabel->setPath(QDir::toNativeSeparators(folder));
        }
    }

    clearStates();
    updateConfirmButton();
    adaptToHost();
}

void PDFAppToolResultDialog::onChooseLocationClicked()
{
    onBrowseClicked();
}

void PDFAppToolResultDialog::onConfirmClicked()
{
    confirm();
}

void PDFAppToolResultDialog::confirm()
{
    if (isWriting() || m_isDecided)
    {
        return;
    }

    PDFAppToolResultOutcome outcome;
    if (m_kind == ResultKind::Analysis)
    {
        // A refused display version: the result is gone, the tool was of no use
        outcome.action = m_isDisplayVersion ? PDFAppToolResultOutcome::Action::Discarded : PDFAppToolResultOutcome::Action::Closed;
        finishWith(outcome);
        return;
    }

    switch (m_selectedChoice)
    {
        case Choice::Keep:
            commitKeep();
            return;

        case Choice::Overwrite:
            startOverwrite();
            return;

        case Choice::Copy:
        {
            const PDFDocumentToolOutputDocument* output = findOutput(PDFDocumentToolOutputDocument::Kind::Changed);
            if (checkTarget() && output && m_writer->writeCopy(m_input.tool, m_snapshot, output->document, getTargetFilePath(), getOriginFilePath(), m_isReplaceConfirmed))
            {
                setBusy(true);
            }
            return;
        }

        case Choice::NewTab:
        {
            const PDFDocumentToolOutputDocument* output = findOutput(PDFDocumentToolOutputDocument::Kind::New);
            if (output && !output->writtenData.isEmpty())
            {
                // The run has written the document already (its size, plan of step 2 of
                // phase 2, 4.5): the new tab reads these bytes, nothing is written twice
                PDFAppToolResultWriter::Result written;
                written.isWritten = true;
                written.data = output->writtenData;
                onWriterFinished(written);
            }
            else if (output && m_writer->writeDocumentToMemory(output->document))
            {
                setBusy(true);
            }
            return;
        }

        case Choice::File:
        {
            if (!checkTarget())
            {
                return;
            }

            bool isStarted = false;
            if (m_kind == ResultKind::Files)
            {
                isStarted = !m_input.result.outputs.empty() && m_writer->writeFile(m_input.result.outputs.front().data, getTargetFilePath(), getOriginFilePath(), m_isReplaceConfirmed);
            }
            else if (const PDFDocumentToolOutputDocument* output = findOutput(PDFDocumentToolOutputDocument::Kind::New))
            {
                // The bytes of the run, when it has written the document (4.5: one write)
                isStarted = output->writtenData.isEmpty() ? m_writer->writeDocument(output->document, getTargetFilePath(), getOriginFilePath(), m_isReplaceConfirmed)
                                                          : m_writer->writeFile(output->writtenData, getTargetFilePath(), getOriginFilePath(), m_isReplaceConfirmed);
            }
            if (isStarted)
            {
                setBusy(true);
            }
            return;
        }

        case Choice::Clipboard:
            if (!m_input.result.outputs.empty())
            {
                // The charset of the output (Latin-1, the code page of the system)
                QStringDecoder decoder = PDFDocumentTextExporter::createDecoder(m_input.result.outputs.front().mimeType);
                const QString text = decoder.decode(m_input.result.outputs.front().data);
                QGuiApplication::clipboard()->setText(text);
                outcome.action = PDFAppToolResultOutcome::Action::CopiedToClipboard;
                finishWith(outcome);
            }
            return;
    }
}

void PDFAppToolResultDialog::commitKeep()
{
    if (!m_runner || !m_input.tool)
    {
        return;
    }

    QPointer<PDFAppToolResultDialog> guard(this);
    QString error;
    const PDFDocumentToolRunner::CommitStatus status = m_runner->commitResult(m_input.result, m_input.tool->getTitle(), &error);
    if (!guard)
    {
        return;
    }

    PDFAppToolResultOutcome outcome;
    switch (status)
    {
        case PDFDocumentToolRunner::CommitStatus::Committed:
            outcome.action = PDFAppToolResultOutcome::Action::Kept;
            finishWith(outcome);
            return;

        case PDFDocumentToolRunner::CommitStatus::Rerun:
            outcome.action = PDFAppToolResultOutcome::Action::Rerun;
            finishWith(outcome);
            return;

        case PDFDocumentToolRunner::CommitStatus::Refused:
            showWriteError(error);
            return;
    }
}

void PDFAppToolResultDialog::startOverwrite()
{
    PDFAppDocumentTab* tab = m_tab.data();
    PDFAppDocumentFileCommandHandler* handler = tab ? PDFAppDocumentFileCommandHandler::find(tab) : nullptr;
    PDFAppDocumentSaver* saver = handler ? handler->getSaver() : nullptr;
    PDFAppDocument* document = m_runner ? m_runner->getDocument() : nullptr;
    if (!saver || !document || !m_input.tool)
    {
        showWriteError(tr("The document is not available any more."));
        return;
    }

    if (document->getVersion() != m_snapshot.version)
    {
        // The document has changed under the result: the file is not touched, the
        // tool runs again on the new version
        commitKeep();
        return;
    }

    // The announcement of the write may delete the window with this dialog
    QPointer<PDFAppToolResultDialog> guard(this);
    m_isOverwriteRunning = true;
    setBusy(true);
    const bool isStarted = m_writer->overwrite(m_runner.data(), saver, m_input.result, m_input.tool->getTitle());
    if (guard && !isStarted)
    {
        m_isOverwriteRunning = false;
        setBusy(false);
        showWriteError(tr("The file cannot be written now."));
    }
}

void PDFAppToolResultDialog::onWriterFinished(const PDFAppToolResultWriter::Result& result)
{
    m_isOverwriteRunning = false;
    setBusy(false);

    if (result.isTargetExisting)
    {
        // The state of an existing file inside of the dialog (tools.md 9.3 point 11)
        m_isReplaceConfirmed = true;
        updateConfirmButton();
        showNameError(tr("A file of this name exists already. Replace it, or enter another name."));
        return;
    }

    if (!result.isWritten)
    {
        showWriteError(result.errorMessage, result.isCommitRefused);
        return;
    }

    PDFAppToolResultOutcome outcome;
    outcome.filePath = result.filePath;
    switch (m_selectedChoice)
    {
        case Choice::Copy:
            outcome.action = PDFAppToolResultOutcome::Action::CopySaved;
            break;
        case Choice::Overwrite:
            outcome.action = PDFAppToolResultOutcome::Action::Overwritten;
            break;
        case Choice::NewTab:
            outcome.action = PDFAppToolResultOutcome::Action::OpenedInNewTab;
            break;
        default:
            outcome.action = PDFAppToolResultOutcome::Action::FileSaved;
            break;
    }

    if (m_selectedChoice == Choice::NewTab)
    {
        // The bytes are kept by the new tab
        m_input.result.outputs.clear();
        PDFDocumentToolOutputFile memory;
        memory.data = result.data;
        m_input.result.outputs.push_back(std::move(memory));
    }
    finishWith(outcome);
}

void PDFAppToolResultDialog::onDiscardClicked()
{
    reject();
}

void PDFAppToolResultDialog::reject()
{
    if (isWriting() || m_isDecided)
    {
        return;
    }

    PDFAppToolResultOutcome outcome;
    outcome.action = m_kind == ResultKind::Analysis && !m_isDisplayVersion ? PDFAppToolResultOutcome::Action::Closed : PDFAppToolResultOutcome::Action::Discarded;
    finishWith(outcome);
}

void PDFAppToolResultDialog::onRunnerGone()
{
    if (m_isDecided)
    {
        return;
    }

    // The tab or the document has gone: the result has no place to go. The worker of a
    // write ends by itself.
    m_isDecided = true;
    m_writer->disconnect(this);
    hide();
    deleteLater();
}

void PDFAppToolResultDialog::openResult(PDFAppToolResultOutcome* outcome)
{
    // Opening a document runs slots of the application: they may delete this dialog
    QPointer<PDFAppToolResultDialog> guard(this);
    PDFAppMainWindow* window = qobject_cast<PDFAppMainWindow*>(parentWidget() ? parentWidget()->window() : nullptr);

    const int continueIndex = m_continueWith ? m_continueWith->getSelectedChoice() : -1;
    if (continueIndex >= 0 && continueIndex < m_continueToolIds.size())
    {
        outcome->continueToolId = m_continueToolIds[continueIndex];
    }

    const PDFAppDocument* document = m_runner ? m_runner->getDocument() : nullptr;
    switch (outcome->action)
    {
        case PDFAppToolResultOutcome::Action::Kept:
        case PDFAppToolResultOutcome::Action::Overwritten:
            outcome->continueDocumentId = document ? document->getId() : QString();
            break;

        case PDFAppToolResultOutcome::Action::CopySaved:
        case PDFAppToolResultOutcome::Action::FileSaved:
            if (m_kind == ResultKind::Files)
            {
                outcome->isOpenFolderRequested = m_openCheckBox && m_openCheckBox->isChecked();
            }
            else if (m_openCheckBox && m_openCheckBox->isChecked() && window)
            {
                // The copy opens in its own tab: the next tool runs on it (F11)
                window->openDocuments({ outcome->filePath }, 0);
                if (const PDFAppDocument* opened = guard && m_context && m_context->getDocumentRegistry() ? m_context->getDocumentRegistry()->findByCanonicalPath(outcome->filePath) : nullptr)
                {
                    outcome->continueDocumentId = opened->getId();
                }
            }
            break;

        case PDFAppToolResultOutcome::Action::OpenedInNewTab:
            if (window && !m_input.result.outputs.empty())
            {
                // After the own tab of the tool (4.5), else after the tab of the input document
                const QString title = getSuggestedFileName();
                const int index = m_input.anchorTab ? window->getTabIndexAfter(m_input.anchorTab.data()) : window->getTabIndexAfter(document);
                if (const PDFAppDocumentTab* tab = window->openDocumentFromMemoryAt(m_input.result.outputs.front().data, title, getOriginFilePath(), index))
                {
                    outcome->continueDocumentId = tab->getDocumentId();
                }
            }
            break;

        default:
            break;
    }

}

void PDFAppToolResultDialog::finishWith(PDFAppToolResultOutcome outcome)
{
    if (m_isDecided)
    {
        return;
    }
    m_isDecided = true;

    QPointer<PDFAppToolResultDialog> guard(this);

    // The operation ends with the decision (a commit has ended it already)
    if (m_runner && outcome.action != PDFAppToolResultOutcome::Action::Rerun)
    {
        m_runner->endDecision();
        if (!guard)
        {
            return;
        }
    }

    // A preview is no use of a tool, an accepted result is
    if (outcome.isAccepted() && m_context && m_context->getDatabase() && m_input.tool)
    {
        const PDFDatabaseError error = PDFDocumentToolUsage(m_context->getDatabase()).recordRun(m_input.tool->getId(), m_input.previousToolId);
        if (error.isError())
        {
            qWarning("The use of the tool was not recorded: %s", qPrintable(error.toString()));
        }
    }

    // The dialog is closed first: a modal dialog delays the opening of documents
    QDialog::done(outcome.isAccepted() ? QDialog::Accepted : QDialog::Rejected);
    if (!guard)
    {
        return;
    }

    openResult(&outcome);
    if (guard)
    {
        Q_EMIT decided(outcome);
    }
}

void PDFAppToolResultDialog::onAnotherToolRequested()
{
    // The search of the catalog over this dialog; no nested event loop: the window may
    // be deleted while it is open
    if (m_chooser)
    {
        return;
    }

    PDFAppToolChooserDialog* chooser = new PDFAppToolChooserDialog(this);
    m_chooser = chooser;
    chooser->setAttribute(Qt::WA_DeleteOnClose);
    connect(chooser, &QDialog::finished, this, &PDFAppToolResultDialog::onChooserFinished);
    chooser->QDialog::open();
}

void PDFAppToolResultDialog::onReportLinkClicked()
{
    // The link says it first; the clipboard is the last thing (its signals reach the application)
    m_isReportCopied = true;
    if (m_reportLink)
    {
        m_reportLink->setText(tr("Report copied"));
    }
    if (QClipboard* clipboard = QGuiApplication::clipboard())
    {
        clipboard->setText(m_input.reportText);
    }
}

void PDFAppToolResultDialog::onChooserFinished(int result)
{
    const QString toolId = m_chooser ? m_chooser->getChosenToolId() : QString();
    m_chooser = nullptr;

    const PDFDocumentToolCatalogEntry* tool = result == QDialog::Accepted ? PDFDocumentToolCatalog::findTool(toolId) : nullptr;
    if (!tool || !m_continueWith)
    {
        return;
    }

    // The chosen tool takes the last place of the row, when the row is full
    qsizetype index = m_continueToolIds.indexOf(toolId);
    if (index < 0)
    {
        QList<PDFContinueWith::Choice> choices = m_continueWith->getChoices();
        PDFContinueWith::Choice choice;
        choice.name = tool->getTitle();
        choice.iconName = tool->getIconName();
        choice.hue = PDFAppToolPresentation::getHue(tool->getHue());
        if (choices.size() >= 3)
        {
            choices.removeLast();
            m_continueToolIds.removeLast();
        }
        choices << choice;
        m_continueToolIds << toolId;
        m_continueWith->setChoices(choices);
        index = m_continueToolIds.size() - 1;
    }
    m_continueWith->setSelectedChoice(int(index));
    adaptToHost();
}

void PDFAppToolResultDialog::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange && m_titleLabel)
    {
        updateTexts();
        adaptToHost();
    }
    BaseClass::changeEvent(event);
}

void PDFAppToolResultDialog::showEvent(QShowEvent* event)
{
    BaseClass::showEvent(event);

    // The children are polished now: their heights are final
    adaptToHost();
}

void PDFAppToolResultDialog::setVisible(bool visible)
{
    if (visible)
    {
        // The card follows the window it covers
        QWidget* host = PDFAppMainWindow::findDialogHost(parentWidget());
        if (!host)
        {
            host = parentWidget();
        }

        if (host != m_host.data())
        {
            if (m_host)
            {
                m_host->removeEventFilter(this);
            }
            m_host = host;
            if (host)
            {
                host->installEventFilter(this);
            }
        }
        adaptToHost();
    }
    BaseClass::setVisible(visible);
}

bool PDFAppToolResultDialog::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_host.data() && event->type() == QEvent::Resize)
    {
        adaptToHost();
    }
    return BaseClass::eventFilter(watched, event);
}

void PDFAppToolResultDialog::adaptToHost()
{
    if (!m_scrollArea || !m_body)
    {
        return;
    }

    // Its controls are laid out again by the selection. The nested layouts too: a text set
    // before the event loop ran (the reason of the partial warning, 378:4376) has not
    // invalidated the cached height of its card yet, the body would be measured without it
    // and the last option would be cut off below a scroll bar
    for (QLayout* nestedLayout : m_body->findChildren<QLayout*>())
    {
        nestedLayout->invalidate();
    }
    if (QLayout* bodyLayout = m_body->layout())
    {
        bodyLayout->invalidate();
    }

    QWidget* host = m_host.data();
    const int availableWidth = host ? host->width() - 2 * HOST_MARGIN : CARD_WIDTH;
    setCardWidth(qBound(MINIMUM_CARD_WIDTH, availableWidth, int(CARD_WIDTH)));

    const int contentWidth = getContentWidth();
    const int bodyHeight = m_body->hasHeightForWidth() ? m_body->heightForWidth(contentWidth) : m_body->sizeHint().height();

    if (!host)
    {
        m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_scrollArea->setFixedHeight(bodyHeight);
        return;
    }

    // The parts of the card around the body (the shadow is outside of the card)
    const QMargins shadowMargins = getShadowMargins();
    const int otherHeight = sizeHint().height() - shadowMargins.top() - shadowMargins.bottom() - m_scrollArea->minimumHeight();
    const int availableHeight = host->height() - 2 * HOST_MARGIN - qMax(0, otherHeight);
    const int scrollAreaHeight = qMax(int(MINIMUM_BODY_HEIGHT), qMin(bodyHeight, availableHeight));
    // A body, which fits, has no scroll bar (378:3674): the overlay bar, which became visible
    // while the area was lower, sometimes stayed with the range 0 as a full slider over the
    // facts. The height of the body is measured with every nested layout invalidated above.
    m_scrollArea->setVerticalScrollBarPolicy(bodyHeight > scrollAreaHeight ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
    m_scrollArea->setFixedHeight(scrollAreaHeight);

    // The height for the width of the card is cached by the layout
    if (layout())
    {
        layout()->invalidate();
    }

    // The scroll bar lies over the content: a scrolled body keeps its width free
    if (QLayout* bodyLayout = m_body->layout())
    {
        const int scrollBarWidth = bodyHeight > scrollAreaHeight ? style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, m_scrollArea) : 0;
        bodyLayout->setContentsMargins(isRightToLeft() ? scrollBarWidth : 0, 0, isRightToLeft() ? 0 : scrollBarWidth, 0);
    }

    // The card lies over the canvas of the tool, when it fits; a small window: over its
    // whole body (312:7373, the case of the minimal window has no frame)
    QWidget* target = m_input.scrimTarget.data();
    const int cardHeight = otherHeight + scrollAreaHeight;
    if (!target || target->width() < getCardWidth() || target->height() < cardHeight)
    {
        target = host;
    }
    setScrimTarget(target);

    if (isVisible() && isWindow())
    {
        // A shown card takes its new size and stays centered over its target
        if (layout())
        {
            layout()->activate();
        }
        adjustSize();
        const QRect card = rect().marginsRemoved(shadowMargins);
        QRect cardRect(QPoint(0, 0), card.size());
        cardRect.moveCenter(QRect(target->mapToGlobal(QPoint(0, 0)), target->size()).center());
        move(cardRect.topLeft() - card.topLeft());
    }
}

}   // namespace pdf

#include "moc_pdfapptoolresultdialog.cpp"
