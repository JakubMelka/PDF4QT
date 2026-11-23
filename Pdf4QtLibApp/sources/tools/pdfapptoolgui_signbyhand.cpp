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

#include "pdfapptoolgui_signbyhand.h"
#include "pdfappdocument.h"
#include "pdfappdocumentsearch.h"
#include "pdfappdocumentsignatures.h"
#include "pdfappdocumentview.h"
#include "pdfapplocale.h"
#include "pdfapppagelayout.h"
#include "pdfapptoolhost.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolparametermodel.h"
#include "pdfappworkspace.h"
#include "pdfdocumenttoolguiregistry.h"
#include "pdfdocumenttoolshell.h"

#include "pdfcomponentbadges.h"
#include "pdfdeferreddeletion.h"
#include "pdfdocumenttool_signbyhand.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfdrawwidget.h"
#include "pdfmessagebox.h"
#include "pdfthemefonts.h"
#include "pdfthemeiconengine.h"

#include <QPushButton>
#include <QVBoxLayout>

namespace pdf
{

/// Sizes of Sign by Hand (435:8619, 435:8908)
class PDFAppToolGUISignByHandHelper
{
public:
    PDFAppToolGUISignByHandHelper() = delete;

    /// empty-state 435:8987: padding 24 + the border, the parts 8 apart, the tile 32 with the icon 16
    static constexpr int EMPTY_PADDING = 25;
    static constexpr int EMPTY_GAP = 8;
    static constexpr int EMPTY_TILE = 32;
    static constexpr int EMPTY_ICON = 16;
    static constexpr int EMPTY_TILE_RADIUS = 8;
};

PDFAppToolGUISignByHand::PDFAppToolGUISignByHand(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppToolGUISignByHand::~PDFAppToolGUISignByHand()
{
    // The questions go with the tool (they report nothing more, they delete themselves)
    for (PDFMessageBox* dialog : { m_discardDialog.data(), m_clearDialog.data() })
    {
        if (dialog)
        {
            disconnect(dialog, nullptr, this, nullptr);
            dialog->close();
        }
    }
}

void PDFAppToolGUISignByHand::registerSignByHandTools()
{
    const QString toolId = QString::fromLatin1(PDFDocumentToolSignByHand::TOOL_ID);
    if (!PDFDocumentToolRegistry::isImplemented(toolId))
    {
        PDFDocumentToolRegistry::registerTool(std::make_shared<PDFDocumentToolSignByHand>());
    }
    if (!PDFDocumentToolGUIRegistry::getFactory(toolId))
    {
        PDFDocumentToolGUIRegistry::registerFactory(toolId, std::make_shared<PDFDocumentToolGUIFactoryOf<PDFAppToolGUISignByHand>>());
    }
}

void PDFAppToolGUISignByHand::ensureDrawingPart()
{
    if (m_drawingPart)
    {
        return;
    }

    // Ownership: a child object of this GUI; it leaves the view, when it dies with the GUI
    m_drawingPart = new PDFAppToolDrawingPart(getContext(), PDFAppToolDrawingPart::Layout::SignByHand, this);
    connect(m_drawingPart.data(), &PDFAppToolDrawingPart::drawingChanged, this, &PDFAppToolGUISignByHand::onDrawingChanged);
    connect(m_drawingPart.data(), &PDFAppToolDrawingPart::selectionChanged, this, &PDFAppToolGUISignByHand::onSelectionChanged);
    connect(m_drawingPart.data(), &PDFAppToolDrawingPart::clearRequested, this, &PDFAppToolGUISignByHand::onClearRequested);
    m_drawingPart->bindParameter(getModel(), QString::fromLatin1(PDFDocumentToolSignByHand::PARAMETER_DRAWING));
    if (PDFWidget* widget = getHost() ? getHost()->getPdfWidget() : nullptr)
    {
        m_drawingPart->attachToView(widget->getDrawWidgetProxy());
    }
}

// ------------------------------------------------------------------- panels

void PDFAppToolGUISignByHand::describeToolbar(PDFAppToolPanelBuilder& builder)
{
    ensureDrawingPart();
    m_drawingPart->describeToolbar(builder);
}

void PDFAppToolGUISignByHand::describeLeftPanel(PDFAppToolPanelBuilder& builder)
{
    using Helper = PDFAppToolGUISignByHandHelper;

    ensureDrawingPart();

    // header 435:8762: Elements, "4 elements on 2 pages" / "Nothing drawn yet" (435:8908)
    m_leftHeader = builder.addPanelHeader(tr("Elements"), QString());
    m_leftHeader->setObjectName(QStringLiteral("signByHandElementsHeader"));

    // empty-state 435:8987: the tile of the tool, "Draw your signature" and its text
    PDFAppToolCard* empty = new PDFAppToolCard(builder.getWidget());
    empty->setObjectName(QStringLiteral("signByHandEmptyState"));
    empty->getBodyLayout()->setContentsMargins(Helper::EMPTY_PADDING, Helper::EMPTY_PADDING, Helper::EMPTY_PADDING, Helper::EMPTY_PADDING);
    empty->getBodyLayout()->setSpacing(Helper::EMPTY_GAP);
    PDFIconBadge* tile = new PDFIconBadge(getTool() ? getTool()->getIconName() : QStringLiteral("signature"), Helper::EMPTY_TILE, Helper::EMPTY_ICON, empty);
    tile->setColors(PDFThemeColors::BgAccentSubtle, PDFThemeColors::IconAccent);
    tile->setRadius(Helper::EMPTY_TILE_RADIUS);
    empty->getBodyLayout()->addWidget(tile, 0, Qt::AlignHCenter);
    empty->getBodyLayout()->addWidget(PDFAppPageWidgets::createLabel(tr("Draw your signature"), QFont::DemiBold, 13, PDFThemeColors::TextPrimary, empty), 0, Qt::AlignHCenter);
    PDFAppParagraph* emptyText = PDFAppPageWidgets::createText(tr("Use the freehand curve to sign with the mouse or a pen, add your name as a text label, or insert an SVG image of your signature."),
                                                               PDFThemeFonts::getFont(QFont::Normal, 11), 13, PDFThemeColors::TextSecondary, empty);
    emptyText->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    emptyText->setMinimumWidth(1);
    empty->getBodyLayout()->addWidget(emptyText);
    builder.addWidget(empty);
    m_emptyCard = empty;

    // card-elements 435:8765 and the hint 435:8810
    QWidget* list = m_drawingPart->createElementList(builder.getWidget());
    builder.addWidget(list);
    m_elementList = list;
    m_listHint = builder.addHint(tr("Click an element to select it on the page. Delete removes it, Undo brings it back."));
    updateParts();
}

void PDFAppToolGUISignByHand::describeRightPanel(PDFAppToolPanelBuilder& builder)
{
    ensureDrawingPart();

    // header 435:8871: Style, the selected element / "Applies to new elements" (435:8908)
    m_rightHeader = builder.addPanelHeader(tr("Style"), QString());
    m_rightHeader->setObjectName(QStringLiteral("signByHandStyleHeader"));
    builder.addWidget(m_drawingPart->createStylePanel(builder.getWidget()));

    // 435:8902: what the drawing is
    m_notDigitalCallout = builder.addCallout(PDFAccentHue::Blue, QStringLiteral("info"), tr("Not a digital ID signature"),
                                             tr("The drawing becomes part of the page and does not prove who signed. Use Sign with Certificate for a digital signature."));
    m_notDigitalCallout->setObjectName(QStringLiteral("signByHandNotDigitalCallout"));

    // Plan 2.6: a signed document - the drawing is a change after the signatures (the certified
    // document has the callout of the shell)
    m_signedCallout = builder.addCallout(PDFAccentHue::Amber, QStringLiteral("alert-triangle"), tr("The document is signed"),
                                         tr("The drawing is added after the signatures. They stay valid for their version, but the Signatures panel reports a change made after signing."));
    m_signedCallout->setObjectName(QStringLiteral("signByHandSignedCallout"));
    updateParts();
}

QWidget* PDFAppToolGUISignByHand::createOverlay(QWidget* canvasArea)
{
    ensureDrawingPart();
    QWidget* hint = m_drawingPart->createCanvasHint(canvasArea);
    updateParts();
    return hint;
}

void PDFAppToolGUISignByHand::setContentEnabled(bool isEnabled)
{
    // K6.8 (review RS6APP): the drawing on the page is no input while the tool runs
    BaseClass::setContentEnabled(isEnabled);
    if (m_drawingPart)
    {
        m_drawingPart->setInputEnabled(isEnabled);
    }
}

// ------------------------------------------------------------------- state

QString PDFAppToolGUISignByHand::getPagesText() const
{
    const int count = m_drawingPart ? m_drawingPart->getElementCount() : 0;
    const std::vector<PDFInteger> pages = m_drawingPart ? m_drawingPart->getPageIndices() : std::vector<PDFInteger>();
    if (pages.size() == 1)
    {
        const QString page = PDFAppLocale::formatNumber(qint64(pages.front() + 1));
        return count == 1 ? tr("1 element on page %1").arg(page) : tr("%Ln elements on page %1", nullptr, count).arg(page);
    }

    QStringList numbers;
    for (const PDFInteger pageIndex : pages)
    {
        numbers << PDFAppLocale::formatNumber(qint64(pageIndex + 1));
    }
    const QString last = numbers.isEmpty() ? QString() : numbers.takeLast();
    return tr("%Ln elements on pages %1 and %2", nullptr, count).arg(numbers.join(tr(", ", "Separator of the numbers of pages")), last);
}

QString PDFAppToolGUISignByHand::getCountText(int elementCount, int pageCount)
{
    // "4 elements on 2 pages" (435:8763)
    const QString elements = elementCount == 1 ? tr("1 element") : tr("%Ln elements", nullptr, elementCount);
    return pageCount == 1 ? tr("%1 on 1 page").arg(elements) : tr("%1 on %Ln pages", nullptr, pageCount).arg(elements);
}

bool PDFAppToolGUISignByHand::isSigned() const
{
    const PDFAppDocument* document = getDocument();
    if (!document)
    {
        return false;
    }
    PDFAppDocumentSearchCommandHandler* handler = PDFAppDocumentSearchCommandHandler::findHandler(document);
    PDFAppDocumentSignatureVerifier* verifier = handler ? handler->getSignatureVerifier() : nullptr;
    return (verifier && verifier->hasSignatures()) || !document->getSignatureResults().empty();
}

void PDFAppToolGUISignByHand::updateParts()
{
    const bool hasElements = m_drawingPart && !m_drawingPart->isEmpty();
    const int count = m_drawingPart ? m_drawingPart->getElementCount() : 0;
    const int pageCount = m_drawingPart ? int(m_drawingPart->getPageIndices().size()) : 0;

    if (m_leftHeader)
    {
        m_leftHeader->setTexts(tr("Elements"), hasElements ? getCountText(count, pageCount) : tr("Nothing drawn yet"));
    }
    if (m_emptyCard)
    {
        m_emptyCard->setVisible(!hasElements);
    }
    if (m_elementList)
    {
        m_elementList->setVisible(hasElements);
    }
    if (m_listHint)
    {
        m_listHint->setVisible(hasElements);
    }
    if (m_rightHeader)
    {
        // 435:8872: the title of the selected element, else the style of the next one
        QString subtitle = tr("Applies to new elements");
        if (m_drawingPart)
        {
            const std::set<PDFInteger> selected = m_drawingPart->getSelectedElements();
            if (!selected.empty())
            {
                subtitle = selected.size() == 1 ? m_drawingPart->getElementTitle(*selected.begin()) : tr("%Ln elements selected", nullptr, int(selected.size()));
            }
        }
        m_rightHeader->setTexts(tr("Style"), subtitle);
    }
    if (m_signedCallout)
    {
        m_signedCallout->setVisible(isSigned());
    }
    if (m_drawingPart)
    {
        // 435:8908 / 435:8619: the hint of the canvas
        m_drawingPart->setCanvasHintText(hasElements ? tr("Drag elements to move them · the handles resize · double-click a text label to edit it")
                                                     : tr("Pick a tool in the toolbar, then click or drag on the page"));
    }
}

QString PDFAppToolGUISignByHand::getConfirmText() const
{
    // K6.2, 435:8619
    return tr("Apply to Pages");
}

bool PDFAppToolGUISignByHand::isConfirmEnabled() const
{
    // 435:8908: Disabled without a drawing
    return BaseClass::isConfirmEnabled() && m_drawingPart && !m_drawingPart->isEmpty();
}

QString PDFAppToolGUISignByHand::getStatusText() const
{
    // 435:8908 "Pick a tool and draw on the page", 435:8619 "4 elements on pages 3 and 308 · they
    // become part of the pages when you apply"
    if (!m_drawingPart || m_drawingPart->isEmpty())
    {
        return tr("Pick a tool and draw on the page");
    }
    return tr("%1 · they become part of the pages when you apply").arg(getPagesText());
}

void PDFAppToolGUISignByHand::fillResultInput(PDFAppToolResultInput* input) const
{
    // 435:10812
    input->title = tr("The drawing is part of the pages");
    input->statusText = tr("Drawing applied – choose what to do with the result");
}

void PDFAppToolGUISignByHand::onParametersChanged(const QString& name)
{
    Q_UNUSED(name);
    updateParts();
}

void PDFAppToolGUISignByHand::onDocumentChanged()
{
    updateParts();
}

void PDFAppToolGUISignByHand::retranslate()
{
    // The panels are made again by the shell; the drawing stays in the part (the value of the model)
    updateParts();
}

// ------------------------------------------------------------------- questions

bool PDFAppToolGUISignByHand::askDiscard()
{
    // A question answered already is hidden until it is deleted (WA_DeleteOnClose, deleteLater): it is no open
    // question, a new one is asked (RESCCODE L1)
    if (m_discardDialog && m_discardDialog->isVisible())
    {
        return true;
    }
    if (!m_drawingPart || m_drawingPart->isEmpty())
    {
        return false;
    }

    // 435:10500: Keep Drawing (Primary) and Discard Drawing (Secondary, trash) in the order of the frame
    const int count = m_drawingPart->getElementCount();
    QWidget* parent = getHost() && getHost()->getKeyWidget() ? getHost()->getKeyWidget()->window() : nullptr;
    PDFMessageBox* dialog = new PDFMessageBox(PDFThemeIcons::Badge::Warning, tr("Discard the drawing?"),
                                              count == 1 ? tr("Closing the tool removes the element you have drawn. The document stays as it was.")
                                                         : tr("Closing the tool removes the %Ln elements you have drawn. The document stays as it was.", nullptr, count),
                                              QDialogButtonBox::NoButton, parent);
    // A dialog of the window owned by its creator (step A): retire closes it unanswered
    PDFDeferredDeletion::setOwner(dialog, this);
    dialog->setObjectName(QStringLiteral("signByHandDiscardDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    QPushButton* keepButton = dialog->addActionButton(tr("Keep Drawing"), PDFMessageBox::ActionPosition::Trailing, true);
    QPushButton* discardButton = dialog->addActionButton(tr("Discard Drawing"), PDFMessageBox::ActionPosition::Trailing, false);
    discardButton->setIcon(PDFThemeIcons::getIcon(QStringLiteral("trash")));
    keepButton->setObjectName(QStringLiteral("signByHandDiscardKeep"));
    discardButton->setObjectName(QStringLiteral("signByHandDiscard"));
    connect(keepButton, &QPushButton::clicked, dialog, &QDialog::reject);
    connect(discardButton, &QPushButton::clicked, this, &PDFAppToolGUISignByHand::onDiscardClicked);
    m_discardDialog = dialog;
    dialog->open();
    return true;
}

void PDFAppToolGUISignByHand::onDiscardClicked()
{
    if (m_discardDialog)
    {
        disconnect(m_discardDialog.data(), nullptr, this, nullptr);
        m_discardDialog->close();
    }

    // The last call: the shell and this GUI go (as Cancel, without the question again)
    if (PDFDocumentToolShell* shell = getShell())
    {
        shell->closeAfterQuestion();
    }
}

bool PDFAppToolGUISignByHand::handleEscape()
{
    return m_drawingPart && (m_drawingPart->endActiveElement() || m_drawingPart->clearSelection());
}

QString PDFAppToolGUISignByHand::getEscapeModeHint() const
{
    return m_drawingPart ? m_drawingPart->getEscapeHint() : QString();
}

QDialog* PDFAppToolGUISignByHand::askClose()
{
    return askDiscard() ? m_discardDialog.data() : nullptr;
}

bool PDFAppToolGUISignByHand::hasUnsavedWork() const
{
    return m_drawingPart && !m_drawingPart->isEmpty();
}

void PDFAppToolGUISignByHand::askClearAll()
{
    if (m_clearDialog || !m_drawingPart || m_drawingPart->isEmpty())
    {
        return;
    }

    const int count = m_drawingPart->getElementCount();
    QWidget* parent = getHost() && getHost()->getKeyWidget() ? getHost()->getKeyWidget()->window() : nullptr;
    PDFMessageBox* dialog = new PDFMessageBox(PDFThemeIcons::Badge::Warning, tr("Clear the drawing?"),
                                              count == 1 ? tr("Clear All removes the element you have drawn. Undo brings it back.")
                                                         : tr("Clear All removes the %Ln elements you have drawn. Undo brings them back.", nullptr, count),
                                              QDialogButtonBox::NoButton, parent);
    // A dialog of the window owned by its creator (step A): retire closes it unanswered
    PDFDeferredDeletion::setOwner(dialog, this);
    dialog->setObjectName(QStringLiteral("signByHandClearDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    QPushButton* keepButton = dialog->addActionButton(tr("Keep Drawing"), PDFMessageBox::ActionPosition::Trailing, true);
    QPushButton* clearButton = dialog->addActionButton(tr("Clear All"), PDFMessageBox::ActionPosition::Trailing, false);
    clearButton->setIcon(PDFThemeIcons::getIcon(QStringLiteral("eraser")));
    connect(keepButton, &QPushButton::clicked, dialog, &QDialog::reject);
    connect(clearButton, &QPushButton::clicked, this, &PDFAppToolGUISignByHand::onClearConfirmed);
    m_clearDialog = dialog;
    dialog->open();
}

void PDFAppToolGUISignByHand::onClearConfirmed()
{
    if (m_clearDialog)
    {
        disconnect(m_clearDialog.data(), nullptr, this, nullptr);
        m_clearDialog->close();
    }
    if (m_drawingPart)
    {
        // One step of Undo (the last call: the model and the panels follow it)
        m_drawingPart->clearAll();
    }
}

// ------------------------------------------------------------------- slots

void PDFAppToolGUISignByHand::onDrawingChanged()
{
    updateParts();
    updateConfirmState();
    if (!PDFDeferredDeletion::isRetired(this))
    {
        updateStatusText();
    }
}

void PDFAppToolGUISignByHand::onSelectionChanged()
{
    updateParts();
}

void PDFAppToolGUISignByHand::onClearRequested()
{
    askClearAll();
}

}   // namespace pdf

#include "moc_pdfapptoolgui_signbyhand.cpp"
