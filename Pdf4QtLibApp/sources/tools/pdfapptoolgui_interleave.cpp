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

#include "pdfapptoolgui_interleave.h"

#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentpanels_common.h"
#include "pdfappdocumentpanels_thumbnails.h"
#include "pdfappdocumentregistry.h"
#include "pdfappfiledrop.h"
#include "pdfapphomewidgets.h"
#include "pdfapplocale.h"
#include "pdfappmainwindow.h"
#include "pdfapppagelayout.h"
#include "pdfapptoolhost.h"
#include "pdfapptoolgui_images.h"
#include "pdfapptoolinputlist.h"
#include "pdfapptoolmergemodel.h"
#include "pdfapptoolpagegrid.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptoolresultdialog.h"
#include "pdfapptooltoolbar.h"
#include "pdfdocumenttoolrunner.h"

#include "pdfcomponentdelegates.h"
#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdfdocumentpageplan.h"
#include "pdfdocumenttool_images.h"
#include "pdfstylepainter.h"
#include "pdfthemefonts.h"

#include <QHBoxLayout>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

#include <limits>

namespace pdf
{

/// Helpers of the GUI of Interleave Pages
class PDFAppInterleaveToolGUIHelper
{
public:
    PDFAppInterleaveToolGUIHelper() = delete;

    /// Widths of the panels of the toolbar (385:13970)
    static constexpr int BACK_SIDES_PANEL_WIDTH = 300;
    static constexpr int EXTRA_PAGES_PANEL_WIDTH = 300;
    static constexpr int CHANGE_PANEL_WIDTH = 290;

    /// Key of a row of the panel of Change… (empty = the file dialog, else the id of a document)
    static constexpr const char* ROW_DOCUMENT = "pdf4qtInterleaveDocument";

    static QString name(const char* parameter)
    {
        return QString::fromLatin1(parameter);
    }

    static int toInt(PDFInteger value)
    {
        return int(qBound(PDFInteger(0), value, PDFInteger(std::numeric_limits<int>::max())));
    }

    static QStringList splitIds(const QString& text)
    {
        QStringList ids;
        for (const QString& part : text.split(QLatin1Char(','), Qt::SkipEmptyParts))
        {
            ids << part.trimmed();
        }
        return ids;
    }
};

// ------------------------------------------------------------- PDFAppInterleaveCanvas

PDFAppInterleaveCanvas::PDFAppInterleaveCanvas(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("interleaveCanvas"));

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(PADDING - PDFAppToolPageGrid::TILE_SPACING, PADDING, PADDING - PDFAppToolPageGrid::TILE_SPACING, PADDING);
    layout->setSpacing(0);

    // preview-bar 385:13263: "Result · 12 pages" Semi Bold 13 text/primary
    m_header = PDFAppPageWidgets::createLabel(QString(), QFont::DemiBold, 13, PDFThemeColors::TextPrimary, this);
    m_header->setObjectName(QStringLiteral("interleaveHeader"));
    m_header->setFixedHeight(BAR_HEIGHT);
    m_header->setContentsMargins(PDFAppToolPageGrid::TILE_SPACING, 0, PDFAppToolPageGrid::TILE_SPACING, 0);
    m_header->setAlignment(Qt::AlignLeading | Qt::AlignVCenter);
    layout->addWidget(m_header);
    layout->addSpacing(BAR_GAP - PDFAppToolPageGrid::TILE_SPACING);

    // The tiles: the view of the page grids, the pages of the plan from the inputs of the list
    m_view = new PDFAppToolPageGridView(this);
    m_view->setObjectName(QStringLiteral("interleaveView"));
    PDFAppPanelWidgets::prepareView(m_view);
    m_view->setAccessibleName(tr("Pages of the result"));
    m_model = new PDFAppToolMergePageModel(m_view);
    // The delegate of the tool tiles: the dots of HueRole (front blue, back green) and the
    // captions of SubtitleRole ("front 1", "back 6", 385:13263)
    m_delegate = new PDFAppToolPageTileDelegate(m_view);
    m_view->setModel(m_model);
    m_view->setItemDelegate(m_delegate);
    m_scheduler = new PDFAppThumbnailScheduler(m_model, m_view, this);
    layout->addWidget(m_view, 1);
    layout->addSpacing(HINT_GAP - PDFAppToolPageGrid::TILE_SPACING);

    // stage 385:13263: the hint Regular 12 text/secondary
    m_hint = PDFAppPageWidgets::createText(QString(), PDFThemeFonts::getFont(QFont::Normal, 12), 15, PDFThemeColors::TextSecondary, this);
    m_hint->setObjectName(QStringLiteral("interleaveHint"));
    m_hint->setContentsMargins(PDFAppToolPageGrid::TILE_SPACING, 0, PDFAppToolPageGrid::TILE_SPACING, 0);
    layout->addWidget(m_hint);

    // drop-zone 385:13738: "Add the two scans" with Choose Files…
    m_emptyState = new QWidget(this);
    m_emptyState->setObjectName(QStringLiteral("interleaveEmptyState"));
    QVBoxLayout* emptyLayout = new QVBoxLayout(m_emptyState);
    emptyLayout->setContentsMargins(0, 0, 0, 0);
    emptyLayout->addStretch(1);
    PDFAppCard* card = PDFAppPageWidgets::createCard(28, 12, m_emptyState);
    card->setObjectName(QStringLiteral("interleaveDropCard"));
    card->setMaximumWidth(560);
    PDFLabel* title = PDFAppPageWidgets::createLabel(tr("Add the two scans"), QFont::DemiBold, 16, PDFThemeColors::TextPrimary, card);
    title->setAlignment(Qt::AlignCenter);
    card->getLayout()->addWidget(title);
    PDFAppParagraph* text = PDFAppPageWidgets::createText(tr("Drop two PDF files here – the first becomes the front sides, the second the back sides."),
                                                          PDFThemeFonts::getFont(QFont::Normal, 12), 17, PDFThemeColors::TextSecondary, card);
    text->setAlignment(Qt::AlignCenter);
    card->getLayout()->addWidget(text);
    m_chooseFilesButton = PDFAppPageWidgets::createButton(tr("Choose Files…"), QStringLiteral("folder-open"), true, card);
    m_chooseFilesButton->setObjectName(QStringLiteral("interleaveChooseFiles"));
    card->getLayout()->addWidget(m_chooseFilesButton, 0, Qt::AlignHCenter);
    emptyLayout->addWidget(card, 0, Qt::AlignHCenter);
    emptyLayout->addStretch(2);
    layout->addWidget(m_emptyState, 1);
    m_emptyState->hide();

    setTileSize(PDFAppToolPageGrid::TileSize::Small);
}

PDFAppInterleaveCanvas::~PDFAppInterleaveCanvas() = default;

void PDFAppInterleaveCanvas::setHeaderText(const QString& text)
{
    m_header->setText(text);
}

void PDFAppInterleaveCanvas::setHintText(const QString& text)
{
    m_hint->setText(text);
}

void PDFAppInterleaveCanvas::setEmpty(bool isEmpty)
{
    m_emptyState->setVisible(isEmpty);
    m_view->setVisible(!isEmpty);
    m_hint->setVisible(!isEmpty);
}

bool PDFAppInterleaveCanvas::isEmpty() const
{
    return !m_emptyState->isHidden();
}

void PDFAppInterleaveCanvas::setTileSize(PDFAppToolPageGrid::TileSize size)
{
    // K8: Small is the slot of the frame (102 x 140), Medium and Large are larger steps
    const double scale = PDFAppToolPageGrid::getTileScale(size);
    m_delegate->setSlotSize(QSize(int(SLOT_WIDTH * scale), int(SLOT_HEIGHT * scale)));

    // New keys of the pictures (their size), then one layout of all tiles (as PDFAppToolPageGrid)
    m_model->setPictureSize(m_delegate->getSlotSize(), m_view->devicePixelRatioF());
    if (m_model->rowCount() > 0)
    {
        Q_EMIT m_delegate->sizeHintChanged(m_model->index(0, 0));
    }
}

void PDFAppInterleaveCanvas::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    // document-canvas: bg/track
    QPainter painter(this);
    PDFStylePainter::fillRect(&painter, QRectF(rect()), PDFThemeColors::BgTrack);
}

// ------------------------------------------------------------ PDFAppInterleaveToolGUI

PDFAppInterleaveToolGUI::PDFAppInterleaveToolGUI(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppInterleaveToolGUI::~PDFAppInterleaveToolGUI()
{
    // The panel of Change… is a child of the left panel; it may outlive this GUI only until its
    // parent goes
    if (m_changePanel)
    {
        m_changePanel->deleteLater();
    }
}

PDFAppToolInputRules PDFAppInterleaveToolGUI::getInputRules() const
{
    // K41: exactly two PDF files in the slots Front sides and Back sides (385:13263)
    PDFAppToolInputRules rules;
    rules.minimum = 2;
    rules.maximum = 2;
    rules.acceptsPdf = true;
    rules.acceptsImages = false;
    rules.slotTitles = { tr("Front sides"), tr("Back sides") };
    return rules;
}

std::array<QString, PDFAppInterleaveToolGUI::SlotCount> PDFAppInterleaveToolGUI::getSlotInputIds() const
{
    std::array<QString, SlotCount> ids;
    for (int i = 0; m_list && i < m_list->getCount() && i < SlotCount; ++i)
    {
        ids[size_t(i)] = m_list->getInput(i)->id;
    }
    return ids;
}

PDFInteger PDFAppInterleaveToolGUI::getSlotPageCount(Slot slot) const
{
    const PDFAppToolInput* input = m_list ? m_list->getInput(int(slot)) : nullptr;
    return input && input->isUsable() ? input->pageCount : 0;
}

bool PDFAppInterleaveToolGUI::hasBothScans() const
{
    return m_list && m_list->getCount() == SlotCount && m_list->isRunnable(SlotCount);
}

// ------------------------------------------------------------- the list and the values

void PDFAppInterleaveToolGUI::connectList()
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
        // K41: the list takes two PDF files (the tab may come without the start page)
        const PDFAppToolInputRules rules = getInputRules();
        m_list->setAcceptedInputs(rules.maximum, rules.acceptsPdf, rules.acceptsImages, tr("Interleave needs PDF files"));
        connect(m_list.data(), &PDFAppToolInputList::inputsChanged, this, &PDFAppInterleaveToolGUI::onListChanged);
        connect(m_list.data(), &PDFAppToolInputList::inputChanged, this, &PDFAppInterleaveToolGUI::onListInputChanged);
        connect(m_list.data(), &QObject::destroyed, this, &PDFAppInterleaveToolGUI::onListDestroyed);
    }
    initializeValues();
}

void PDFAppInterleaveToolGUI::initializeValues()
{
    using Helper = PDFAppInterleaveToolGUIHelper;

    PDFAppToolParameterModel* model = getModel();
    if (m_isValuesInitialized || !model || !m_list)
    {
        return;
    }

    // K21: the inputs, which the tab came with, are no change of the user (no undo step)
    m_isValuesInitialized = true;
    if (Helper::splitIds(model->getValue(Helper::name(PDFDocumentToolInterleave::PARAMETER_INPUTS)).toString()).isEmpty() && m_list->getCount() > 0)
    {
        model->setFollowingValue(Helper::name(PDFDocumentToolInterleave::PARAMETER_INPUTS), m_list->getInputIds().join(QLatin1Char(',')));
    }
    readValues();
}

void PDFAppInterleaveToolGUI::readValues()
{
    using Helper = PDFAppInterleaveToolGUIHelper;

    const PDFAppToolParameterModel* model = getModel();
    if (!model || !m_list)
    {
        return;
    }

    // The inputs of the list follow the values (Undo / Redo: a parked scan comes back without a
    // second reading); the list announces it, the slot waits
    const QStringList ids = Helper::splitIds(model->getValue(Helper::name(PDFDocumentToolInterleave::PARAMETER_INPUTS)).toString());
    QPointer<PDFAppInterleaveToolGUI> guard(this);
    if (ids != m_list->getInputIds())
    {
        m_isChangingList = true;
        m_list->setInputIds(ids);
        if (!guard)
        {
            return;
        }
        m_isChangingList = false;
        if (!m_list)
        {
            return;
        }
    }

    // The parked scans, which no step of the history names, are freed (R12 of step 2)
    QStringList historyIds;
    for (const QVariant& value : model->getHistoryValues(Helper::name(PDFDocumentToolInterleave::PARAMETER_INPUTS)))
    {
        historyIds << Helper::splitIds(value.toString());
    }
    historyIds << m_list->getInputIds();
    m_list->dropParkedInputs(historyIds);
    if (guard)
    {
        updateAll();
    }
}

void PDFAppInterleaveToolGUI::setInputIds(const QStringList& ids)
{
    // One undo step; the model announces it (readValues)
    if (PDFAppToolParameterModel* model = getModel())
    {
        QPointer<PDFAppInterleaveToolGUI> guard(this);
        const QString error = model->setValue(PDFAppInterleaveToolGUIHelper::name(PDFDocumentToolInterleave::PARAMETER_INPUTS), ids.join(QLatin1Char(',')));
        if (guard && !error.isEmpty() && getHost())
        {
            getHost()->showTransientMessage(error);
        }
    }
}

void PDFAppInterleaveToolGUI::onListChanged()
{
    if (!m_isChangingList)
    {
        readValues();
    }
}

void PDFAppInterleaveToolGUI::onListInputChanged(const QString& inputId)
{
    // A reading finished, a password: the rows and the pages, no undo step
    Q_UNUSED(inputId);
    if (!m_isChangingList)
    {
        updateAll();
    }
}

void PDFAppInterleaveToolGUI::onListDestroyed()
{
    m_list = nullptr;
}

// ------------------------------------------------------------- the actions of the user

void PDFAppInterleaveToolGUI::swapSlots()
{
    if (!m_list || m_isRunning || m_list->getCount() != SlotCount)
    {
        return;
    }

    QStringList ids = m_list->getInputIds();
    std::swap(ids[0], ids[1]);
    setInputIds(ids);
}

void PDFAppInterleaveToolGUI::setSlotFile(Slot slot, const QString& filePath)
{
    if (!m_list || m_isRunning || filePath.isEmpty())
    {
        return;
    }

    // The old scan of the slot is parked (Undo takes it back), the new one takes its place;
    // the list announces the addition, the values follow at once as one undo step
    QStringList ids = m_list->getInputIds();
    const QString oldId = int(slot) < ids.size() ? ids.at(int(slot)) : QString();
    QPointer<PDFAppInterleaveToolGUI> guard(this);
    QPointer<PDFAppToolInputList> list(m_list);
    m_isChangingList = true;
    if (!oldId.isEmpty())
    {
        list->removeInput(oldId);
        if (!guard || !list)
        {
            return;
        }
    }
    const PDFAppToolInputList::AddResult result = list->addFile(filePath);
    if (!guard)
    {
        return;
    }
    m_isChangingList = false;
    if (!list)
    {
        return;
    }

    if (result.id.isEmpty())
    {
        // Refused (the same file in the other slot, K29): the old scan stays
        list->setInputIds(ids);
        if (guard && getHost())
        {
            getHost()->showTransientMessage(PDFAppToolInputList::getRefusalText(result.refusal, result.name, list->getMaximumCount()));
        }
        return;
    }

    if (oldId.isEmpty())
    {
        ids.insert(qMin(int(slot), int(ids.size())), result.id);
    }
    else
    {
        ids[int(slot)] = result.id;
    }
    setInputIds(ids);
}

void PDFAppInterleaveToolGUI::setSlotDocument(Slot slot, const QString& documentId)
{
    const PDFAppDocumentRegistry* registry = getContext() ? getContext()->getDocumentRegistry() : nullptr;
    const PDFAppDocument* document = registry ? registry->findDocument(documentId) : nullptr;
    if (!m_list || m_isRunning || !document)
    {
        return;
    }

    QStringList ids = m_list->getInputIds();
    const QString oldId = int(slot) < ids.size() ? ids.at(int(slot)) : QString();
    QPointer<PDFAppInterleaveToolGUI> guard(this);
    QPointer<PDFAppToolInputList> list(m_list);
    m_isChangingList = true;
    if (!oldId.isEmpty())
    {
        list->removeInput(oldId);
        if (!guard || !list)
        {
            return;
        }
    }
    const PDFAppToolInputList::AddResult result = list->addOpenDocument(document);
    if (!guard)
    {
        return;
    }
    m_isChangingList = false;
    if (!list)
    {
        return;
    }

    if (result.id.isEmpty())
    {
        list->setInputIds(ids);
        if (guard && getHost())
        {
            getHost()->showTransientMessage(PDFAppToolInputList::getRefusalText(result.refusal, result.name, list->getMaximumCount()));
        }
        return;
    }

    if (oldId.isEmpty())
    {
        ids.insert(qMin(int(slot), int(ids.size())), result.id);
    }
    else
    {
        ids[int(slot)] = result.id;
    }
    setInputIds(ids);
}

void PDFAppInterleaveToolGUI::chooseSlotFile(Slot slot)
{
    if (m_isRunning)
    {
        return;
    }

    // The native dialog runs its own event loop: the GUI may go meanwhile
    QPointer<PDFAppInterleaveToolGUI> guard(this);
    QWidget* parent = m_leftPanel ? m_leftPanel->window() : nullptr;
    const QStringList files = PDFAppFileDialogs::getOpenFileNames(parent, QString());
    if (guard && !files.isEmpty())
    {
        setSlotFile(slot, files.front());
    }
}

void PDFAppInterleaveToolGUI::onChooseFrontClicked()
{
    chooseSlotFile(FrontSlot);
}

void PDFAppInterleaveToolGUI::onChooseBackClicked()
{
    chooseSlotFile(BackSlot);
}

void PDFAppInterleaveToolGUI::onCanvasChooseFiles()
{
    // Choose Files… of the drop zone (385:13738): the first file is the front sides, the
    // second the back sides; the slots fill from the first empty one
    if (m_isRunning || !m_list)
    {
        return;
    }

    QPointer<PDFAppInterleaveToolGUI> guard(this);
    QWidget* parent = m_canvas ? m_canvas->window() : nullptr;
    const QStringList files = PDFAppFileDialogs::getOpenFileNames(parent, QString());
    for (const QString& file : files)
    {
        if (!guard || !m_list)
        {
            return;
        }
        const int count = m_list->getCount();
        if (count >= SlotCount)
        {
            break;
        }
        setSlotFile(Slot(count), file);
    }
}

void PDFAppInterleaveToolGUI::onSlotLinkActivated(const QString& inputId)
{
    // Change… (K28): the menu of the files and of the open documents at the row
    for (int slot = 0; slot < SlotCount; ++slot)
    {
        if (m_slotRows[slot] && m_slotRows[slot]->getInputId() == inputId)
        {
            openChangePanel(Slot(slot), m_slotRows[slot]);
            return;
        }
    }
}

void PDFAppInterleaveToolGUI::openChangePanel(Slot slot, QWidget* anchor)
{
    using Helper = PDFAppInterleaveToolGUIHelper;

    if (!anchor || !m_leftPanel || m_isRunning)
    {
        return;
    }

    // A new panel for each opening (the open documents change); the old one goes later
    if (m_changePanel)
    {
        m_changePanel->deleteLater();
    }
    m_changeSlot = slot;
    m_changePanel = new PDFAppToolPanelPopup(nullptr, Helper::CHANGE_PANEL_WIDTH, m_leftPanel);
    m_changePanel->setObjectName(QStringLiteral("interleaveChangePanel"));
    PDFTileRow* fileRow = m_changePanel->addRow(tr("PDF file…"), tr("Choose a file from the disk"), QStringLiteral("folder-open"), PDFTileRow::Kind::MenuItem);
    fileRow->setObjectName(QStringLiteral("interleaveChangeFile"));
    fileRow->setProperty(Helper::ROW_DOCUMENT, QString());
    connect(fileRow, &PDFTileRow::clicked, this, &PDFAppInterleaveToolGUI::onChangeRowClicked);

    // The ready documents of the tabs of this window
    const PDFAppDocumentRegistry* registry = getContext() ? getContext()->getDocumentRegistry() : nullptr;
    const PDFAppMainWindow* window = PDFAppPanelWidgets::findMainWindow(m_leftPanel);
    bool hasCaption = false;
    for (PDFAppDocument* document : registry ? registry->getDocuments() : std::vector<PDFAppDocument*>())
    {
        if (!document->isReady() || (window && !window->findDocumentTab(document->getId())))
        {
            continue;
        }
        if (!hasCaption)
        {
            m_changePanel->addGroupCaption(tr("Open documents"));
            hasCaption = true;
        }
        const int pageCount = Helper::toInt(document->getPageCount());
        PDFTileRow* row = m_changePanel->addRow(document->getTitle(), tr("%Ln pages", nullptr, pageCount), QStringLiteral("tab"), PDFTileRow::Kind::MenuItem);
        row->setProperty(Helper::ROW_DOCUMENT, document->getId());
        connect(row, &PDFTileRow::clicked, this, &PDFAppInterleaveToolGUI::onChangeRowClicked);
    }

    m_changePanel->popup(anchor, anchor->mapToGlobal(QPoint(0, anchor->height())).y() + PDFAppToolToolbar::PANEL_GAP);
}

void PDFAppInterleaveToolGUI::onChangeRowClicked()
{
    const QObject* row = sender();
    if (!row)
    {
        return;
    }

    // The panel closes first, the change is the last thing (it may run a dialog)
    const QString documentId = row->property(PDFAppInterleaveToolGUIHelper::ROW_DOCUMENT).toString();
    const Slot slot = m_changeSlot;
    QPointer<PDFAppInterleaveToolGUI> guard(this);
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

void PDFAppInterleaveToolGUI::setTileSize(PDFAppToolPageGrid::TileSize size)
{
    m_tileSize = size;
    m_isTileSizeRead = true;
    if (m_canvas)
    {
        m_canvas->setTileSize(size);
    }
    if (m_tileSizeMenu)
    {
        m_tileSizeMenu->setSize(size);
    }

    // The shared setting of the page grids; the store announces it (the last call)
    PDFAppToolTileSizeMenu::storeSize(getContext(), size);
}

void PDFAppInterleaveToolGUI::onTileSizeChosen(PDFAppToolPageGrid::TileSize size)
{
    setTileSize(size);
}

// ------------------------------------------------------------- the parts

void PDFAppInterleaveToolGUI::describeToolbar(PDFAppToolPanelBuilder& builder)
{
    using Helper = PDFAppInterleaveToolGUIHelper;
    connectList();

    // tb-Back sides, tb-Extra pages 385:13263 with their panels 385:13970
    builder.addEnumMenu(Helper::name(PDFDocumentToolInterleave::PARAMETER_BACK_SIDES), QStringLiteral("reverse"), tr("Back sides: %1"), Helper::BACK_SIDES_PANEL_WIDTH);
    builder.addEnumMenu(Helper::name(PDFDocumentToolInterleave::PARAMETER_EXTRA_PAGES), QStringLiteral("insert-last"), tr("Extra pages: %1"), Helper::EXTRA_PAGES_PANEL_WIDTH);

    // Thumbnails: Small (K8, the menu of the size of the tiles)
    if (!m_isTileSizeRead)
    {
        m_isTileSizeRead = true;
        m_tileSize = PDFAppToolTileSizeMenu::readSize(getContext());
    }
    builder.addActionDivider();
    PDFAppToolPanelPopup* panel = builder.addMenu(QStringLiteral("thumbnailSize"), QStringLiteral("pages"), QString(), PDFAppToolTileSizeMenu::PANEL_WIDTH);
    m_tileSizeMenu = new PDFAppToolTileSizeMenu(panel, tr("Thumbnails: %1"), panel);
    m_tileSizeMenu->setSize(m_tileSize);
    connect(m_tileSizeMenu.data(), &PDFAppToolTileSizeMenu::sizeChosen, this, &PDFAppInterleaveToolGUI::onTileSizeChosen);
}

void PDFAppInterleaveToolGUI::describeLeftPanel(PDFAppToolPanelBuilder& builder)
{
    connectList();
    m_leftPanel = builder.getWidget();

    // sidebar-left 385:13263: "Sources" / "2 files · 6 + 6 pages", the slots, Swap, the hint
    m_sourcesHeader = builder.addPanelHeader(tr("Sources"), QString());
    m_sourcesHeader->setObjectName(QStringLiteral("interleaveSourcesHeader"));
    for (int slot = 0; slot < SlotCount; ++slot)
    {
        m_slotCaptions[slot] = PDFAppPageWidgets::createLabel(QString(), QFont::DemiBold, 11, PDFThemeColors::TextSecondary, builder.getWidget());
        m_slotCaptions[slot]->setObjectName(QStringLiteral("interleaveSlotCaption%1").arg(slot));
        builder.addWidget(m_slotCaptions[slot]);

        m_slotRows[slot] = new PDFAppToolInputRow(builder.getWidget());
        m_slotRows[slot]->setObjectName(QStringLiteral("interleaveSlotRow%1").arg(slot));
        m_slotRows[slot]->setDraggable(false);
        connect(m_slotRows[slot].data(), &PDFAppToolInputRow::linkActivated, this, &PDFAppInterleaveToolGUI::onSlotLinkActivated);
        builder.addWidget(m_slotRows[slot]);

        // slot-A 385:13738: the dashed place "Drop a PDF here" with Choose…
        QWidget* empty = new QWidget(builder.getWidget());
        empty->setObjectName(QStringLiteral("interleaveSlotEmpty%1").arg(slot));
        QHBoxLayout* emptyLayout = new QHBoxLayout(empty);
        emptyLayout->setContentsMargins(0, 0, 0, 0);
        emptyLayout->setSpacing(8);
        PDFAppDropZone* zone = new PDFAppDropZone(PDFAppDropZone::Variant::Compact, empty);
        if (PDFLabel* dropText = zone->findChild<PDFLabel*>(QStringLiteral("dropText")))
        {
            dropText->setText(tr("Drop a PDF here"));
        }
        emptyLayout->addWidget(zone, 1);
        QPushButton* choose = PDFAppPageWidgets::createButton(tr("Choose…"), QString(), false, empty);
        choose->setObjectName(QStringLiteral("interleaveChoose%1").arg(slot));
        connect(choose, &QPushButton::clicked, this, slot == FrontSlot ? &PDFAppInterleaveToolGUI::onChooseFrontClicked : &PDFAppInterleaveToolGUI::onChooseBackClicked);
        emptyLayout->addWidget(choose, 0, Qt::AlignVCenter);
        m_slotEmpty[slot] = empty;
        builder.addWidget(empty);

        if (slot == FrontSlot)
        {
            // Swap (K28): one undo step
            m_swapButton = PDFAppPageWidgets::createButton(tr("Swap"), QStringLiteral("swap"), false, builder.getWidget());
            m_swapButton->setObjectName(QStringLiteral("interleaveSwap"));
            connect(m_swapButton.data(), &QPushButton::clicked, this, &PDFAppInterleaveToolGUI::swapSlots);
            QWidget* swapRow = new QWidget(builder.getWidget());
            QHBoxLayout* swapLayout = new QHBoxLayout(swapRow);
            swapLayout->setContentsMargins(0, 0, 0, 0);
            swapLayout->addStretch(1);
            m_swapButton->setParent(swapRow);
            swapLayout->addWidget(m_swapButton);
            swapLayout->addStretch(1);
            builder.addWidget(swapRow);
        }
    }

    builder.addHint(tr("Typical use: a stack scanned on a one-sided scanner – first all front sides, then the flipped stack with the back sides."));
    updateSources();
}

void PDFAppInterleaveToolGUI::describeRightPanel(PDFAppToolPanelBuilder& builder)
{
    connectList();

    // callout-amber 385:14487: the sides without a pair
    m_unevenCallout = builder.addCallout(PDFAccentHue::Amber, QStringLiteral("alert-triangle"), QString(), QString());
    m_unevenCallout->setObjectName(QStringLiteral("interleaveUneven"));

    // section-Summary 385:13263: Front sides, Back sides, Extra pages, Result
    m_summarySection = new QWidget(builder.getWidget());
    m_summarySection->setObjectName(QStringLiteral("interleaveSummarySection"));
    QVBoxLayout* summaryLayout = new QVBoxLayout(m_summarySection);
    summaryLayout->setContentsMargins(0, 0, 0, 0);
    summaryLayout->setSpacing(PDFAppToolPanelBuilder::SECTION_GAP);
    summaryLayout->addWidget(new PDFAppToolSectionHeader(tr("Summary"), QString(), m_summarySection));
    PDFAppToolCard* card = new PDFAppToolCard(m_summarySection);
    m_summary = new PDFKeyValueList(card);
    m_summary->setObjectName(QStringLiteral("interleaveSummary"));
    m_summary->setRowGap(8);
    m_summary->addRow(tr("Front sides"), QString());
    m_summary->addRow(tr("Back sides"), QString());
    m_summary->addRow(tr("Extra pages"), QString());
    m_summary->addRow(tr("Result"), QString());
    card->getBodyLayout()->addWidget(m_summary);
    summaryLayout->addWidget(card);
    builder.addWidget(m_summarySection);

    // section-How Interleave works 385:13738 (no scans)
    m_howSection = new QWidget(builder.getWidget());
    m_howSection->setObjectName(QStringLiteral("interleaveHowSection"));
    QVBoxLayout* howLayout = new QVBoxLayout(m_howSection);
    howLayout->setContentsMargins(0, 0, 0, 0);
    howLayout->setSpacing(PDFAppToolPanelBuilder::SECTION_GAP);
    howLayout->addWidget(new PDFAppToolSectionHeader(tr("How Interleave works"), QString(), m_howSection));
    PDFAppToolCard* howCard = new PDFAppToolCard(m_howSection);
    howCard->getBodyLayout()->setContentsMargins(PDFAppToolCard::PADDING_H, PDFAppToolCard::PADDING_H, PDFAppToolCard::PADDING_H, PDFAppToolCard::PADDING_H);
    PDFAppParagraph* howText = PDFAppPageWidgets::createText(tr("1. Choose the file with the front sides and the file with the back sides.\n2. Check the first pages of the result in the preview.\n3. Create PDF – the result opens as a new document."),
                                                             PDFThemeFonts::getFont(QFont::Normal, 12), 15, PDFThemeColors::TextSecondary, howCard);
    howText->setObjectName(QStringLiteral("interleaveHowText"));
    howCard->getBodyLayout()->addWidget(howText);
    howLayout->addWidget(howCard);
    builder.addWidget(m_howSection);

    updateSummary();
}

QWidget* PDFAppInterleaveToolGUI::createCanvasPage(QWidget* parent)
{
    connectList();
    if (!m_isTileSizeRead)
    {
        m_isTileSizeRead = true;
        m_tileSize = PDFAppToolTileSizeMenu::readSize(getContext());
    }

    m_canvas = new PDFAppInterleaveCanvas(parent);
    m_canvas->getModel()->setInputList(m_list);
    m_canvas->setTileSize(m_tileSize);
    connect(m_canvas->getChooseFilesButton(), &QPushButton::clicked, this, &PDFAppInterleaveToolGUI::onCanvasChooseFiles);

    // The reset of the model is said to its views only (the canvas is not shown yet)
    QPointer<PDFAppInterleaveCanvas> canvas = m_canvas;
    updateCanvas();
    return canvas.data();
}

void PDFAppInterleaveToolGUI::setContentEnabled(bool isEnabled)
{
    // The actions and the slots are disabled while the PDF is created (10.3 point 46); the
    // pages stay as they are
    BaseClass::setContentEnabled(isEnabled);
    if (m_canvas)
    {
        m_canvas->setEnabled(true);
    }
}

// ------------------------------------------------------------- updates

void PDFAppInterleaveToolGUI::updateAll()
{
    // The parts first, then the button and the status bar (signals, the last call); a change of
    // the visibility or a reset of a model may run an observer, which deletes this GUI
    QPointer<PDFAppInterleaveToolGUI> guard(this);
    updateSources();
    if (!guard)
    {
        return;
    }
    updateCanvas();
    if (!guard)
    {
        return;
    }
    updateSummary();
    if (!guard)
    {
        return;
    }
    updateConfirmState();
    if (guard)
    {
        updateStatusText();
    }
    if (guard)
    {
        updateOwnTabState();
    }
}

PDFAppToolInputRow::Content PDFAppInterleaveToolGUI::createRowContent(Slot slot, const PDFAppToolInput& input) const
{
    PDFAppToolInputRow::Content content;
    content.inputId = input.id;
    content.letter = PDFAppToolInputList::getLetter(int(slot));
    content.hue = PDFAppToolInputList::getHue(int(slot), &input);
    content.name = input.name;
    content.linkText = m_isRunning ? QString() : tr("Change…");
    if (input.isUsable())
    {
        // 385:13263 "6 pages · A4" (a size without a name by its millimeters)
        const int pageCount = PDFAppInterleaveToolGUIHelper::toInt(input.pageCount);
        QString sizeName = PDFAppToolGUIImages::getPresetName(input.firstPageSize);
        if (sizeName.isEmpty())
        {
            sizeName = PDFAppToolMergePageModel::getSizeText(input.firstPageSize, false);
        }
        content.firstLine = sizeName.isEmpty() ? tr("%Ln pages", nullptr, pageCount) : tr("%Ln pages", nullptr, pageCount) + QStringLiteral(" · ") + sizeName;
        const bool isReversed = getModel() && getModel()->getValue(PDFAppInterleaveToolGUIHelper::name(PDFDocumentToolInterleave::PARAMETER_BACK_SIDES)).toString() != QLatin1String(PDFDocumentToolInterleave::BACK_SIDES_IN_ORDER);
        if (slot == BackSlot && isReversed)
        {
            content.secondLine = tr("Read from the last page to the first");
        }
    }
    else
    {
        content.firstLine = input.reason.isEmpty() ? PDFAppToolInputList::getStateText(input.state) : input.reason;
        content.isFirstLineProblem = input.state != PDFAppToolInputState::Loading;
        content.toolTip = content.firstLine;
    }
    return content;
}

QString PDFAppInterleaveToolGUI::getResultPagesText(Slot slot) const
{
    // 385:13263 "result pages 1, 3, 5 …" / "result pages 2, 4, 6 …"
    return slot == FrontSlot ? tr("result pages 1, 3, 5 …") : tr("result pages 2, 4, 6 …");
}

void PDFAppInterleaveToolGUI::updateSources()
{
    if (!m_sourcesHeader)
    {
        return;
    }

    const int count = m_list ? m_list->getCount() : 0;
    if (count == SlotCount && m_list->isRunnable(SlotCount))
    {
        // The noun agrees with the last count (%Ln), the first one is a formatted number
        const int backPages = int(qMin<PDFInteger>(getSlotPageCount(BackSlot), std::numeric_limits<int>::max()));
        const QString pages = tr("%1 + %Ln pages", nullptr, backPages).arg(PDFAppLocale::formatNumber(getSlotPageCount(FrontSlot)));
        m_sourcesHeader->setTexts(tr("Sources"), tr("%Ln files", nullptr, count) + QStringLiteral(" · ") + pages);
    }
    else
    {
        // 385:13738
        m_sourcesHeader->setTexts(tr("Sources"), tr("Two files are needed"));
    }

    // The texts first; the visibility and the states last, each followed by a guard (a change of
    // the layout may run an observer, which deletes this GUI)
    const PDFAppToolInputRules rules = getInputRules();
    std::array<bool, SlotCount> hasInput = { false, false };
    for (int slot = 0; slot < SlotCount; ++slot)
    {
        const PDFAppToolInput* input = m_list ? m_list->getInput(slot) : nullptr;
        hasInput[size_t(slot)] = input != nullptr;
        if (m_slotCaptions[slot])
        {
            m_slotCaptions[slot]->setText(rules.slotTitles.value(slot) + QStringLiteral(" · ") + getResultPagesText(Slot(slot)));
        }
        if (m_slotRows[slot] && input)
        {
            m_slotRows[slot]->setContent(createRowContent(Slot(slot), *input));
        }
    }

    QPointer<PDFAppInterleaveToolGUI> guard(this);
    const bool isRunning = m_isRunning;
    for (int slot = 0; slot < SlotCount && guard; ++slot)
    {
        QPointer<QWidget> row(m_slotRows[slot].data());
        QPointer<QWidget> empty(m_slotEmpty[slot].data());
        if (row)
        {
            row->setVisible(hasInput[size_t(slot)]);
        }
        if (empty)
        {
            empty->setVisible(!hasInput[size_t(slot)]);
        }
        if (empty)
        {
            empty->setEnabled(!isRunning);
        }
    }
    if (guard && m_swapButton)
    {
        m_swapButton->setEnabled(count == SlotCount && !isRunning);
    }
}

QStringList PDFAppInterleaveToolGUI::getTileCaptions() const
{
    // The caption of each page of the result: its side and its page in its scan (385:13263)
    QStringList captions;
    if (!hasBothScans() || !getModel())
    {
        return captions;
    }

    QVariantMap parameters = getModel()->getValues();
    const PDFDocumentPagePlan::InterleaveSettings settings = PDFDocumentToolInterleave::createSettings(parameters);
    for (const PDFDocumentPagePlan::Item& item : PDFDocumentPagePlan::interleave(getSlotPageCount(FrontSlot), getSlotPageCount(BackSlot), settings))
    {
        const QString page = PDFAppLocale::formatNumber(item.pageIndex + 1);
        captions << (item.input == FrontSlot ? tr("front %1").arg(page) : tr("back %1").arg(page));
    }
    return captions;
}

void PDFAppInterleaveToolGUI::updateCanvas()
{
    if (!m_canvas)
    {
        return;
    }

    // Everything is computed first; the calls of the canvas and of its model come last: the model
    // resets its views and the visibility changes the layout - the observers of their signals
    // may delete this GUI (the lifetime sweep found it), each call is followed by a guard
    const bool hasScans = hasBothScans();
    std::vector<PDFAppToolMergePlan::Item> items;
    if (hasScans && getModel())
    {
        // The plan of the core (5.7) on the ids of the slots: the pages of the preview are the
        // pages of the result
        const std::array<QString, SlotCount> ids = getSlotInputIds();
        const PDFDocumentPagePlan::InterleaveSettings settings = PDFDocumentToolInterleave::createSettings(getModel()->getValues());
        for (const PDFDocumentPagePlan::Item& item : PDFDocumentPagePlan::interleave(getSlotPageCount(FrontSlot), getSlotPageCount(BackSlot), settings))
        {
            PDFAppToolMergePlan::Item planItem;
            planItem.inputId = ids[size_t(item.input)];
            planItem.pageIndex = item.pageIndex;
            items.push_back(planItem);
        }
    }

    const QString front = m_list && m_list->getInput(FrontSlot) ? m_list->getInput(FrontSlot)->name : QString();
    const QString back = m_list && m_list->getInput(BackSlot) ? m_list->getInput(BackSlot)->name : QString();
    const QString header = tr("Result · %Ln pages", nullptr, int(items.size()));
    const QString hint = tr("Blue pages come from %1, green pages from %2. Check that every sheet has its two sides next to each other.").arg(front, back);
    const QStringList captions = getTileCaptions();
    PDFAppToolMergePlan plan(std::move(items));

    QPointer<PDFAppInterleaveToolGUI> guard(this);
    QPointer<PDFAppInterleaveCanvas> canvas(m_canvas);
    canvas->setHeaderText(header);
    canvas->setHintText(hint);
    canvas->setEmpty(!hasScans);
    if (!guard || !canvas)
    {
        return;
    }
    QPointer<PDFAppToolMergePageModel> model(canvas->getModel());
    if (model && !(plan == model->getPlan()))
    {
        model->setPlan(std::move(plan));
    }
    if (guard && model)
    {
        model->setTileMarks(captions, true);
    }
}

QString PDFAppInterleaveToolGUI::getSidesText() const
{
    return tr("%1 front and %2 back sides").arg(PDFAppLocale::formatNumber(getSlotPageCount(FrontSlot)), PDFAppLocale::formatNumber(getSlotPageCount(BackSlot)));
}

void PDFAppInterleaveToolGUI::updateSummary()
{
    // The texts first, the visibility last with guards (as updateSources)
    const bool hasScans = hasBothScans();
    if (!m_summary || !getModel())
    {
        return;
    }

    const QVariantMap values = getModel()->getValues();
    const PDFDocumentPagePlan::InterleaveSettings settings = PDFDocumentToolInterleave::createSettings(values);
    const PDFInteger front = getSlotPageCount(FrontSlot);
    const PDFInteger back = getSlotPageCount(BackSlot);
    const PDFInteger unpaired = qAbs(front - back);
    const int resultCount = int(PDFDocumentPagePlan::interleave(front, back, settings).size());

    m_summary->setValue(0, tr("%Ln pages", nullptr, PDFAppInterleaveToolGUIHelper::toInt(front)));
    const QString backText = tr("%Ln pages", nullptr, PDFAppInterleaveToolGUIHelper::toInt(back));
    m_summary->setValue(1, settings.isBackReversed ? backText + QStringLiteral(" · ") + tr("reversed") : backText);
    if (unpaired == 0)
    {
        m_summary->setValue(2, tr("None"));
    }
    else
    {
        // 385:14487 "2 · at the end"; Leave out (no frame) the same way
        m_summary->setValue(2, settings.areExtrasKept ? tr("%Ln · at the end", nullptr, int(unpaired)) : tr("%Ln · left out", nullptr, int(unpaired)));
    }
    m_summary->setValue(3, tr("%Ln pages", nullptr, resultCount));

    QPointer<PDFAppInterleaveToolGUI> guard(this);
    QPointer<QWidget> howSection(m_howSection.data());
    QPointer<QWidget> summarySection(m_summarySection.data());
    if (howSection)
    {
        howSection->setVisible(!hasScans);
    }
    if (summarySection)
    {
        summarySection->setVisible(hasScans);
    }
    if (guard && m_unevenCallout)
    {
        // 385:14487: "2 front sides have no back side"
        const bool isFrontLonger = front > back;
        const QString frontName = m_list && m_list->getInput(FrontSlot) ? m_list->getInput(FrontSlot)->name : QString();
        const QString backName = m_list && m_list->getInput(BackSlot) ? m_list->getInput(BackSlot)->name : QString();
        const QString title = isFrontLonger ? tr("%Ln front sides have no back side", nullptr, int(unpaired)) : tr("%Ln back sides have no front side", nullptr, int(unpaired));
        const QString longer = isFrontLonger ? frontName : backName;
        const QString shorter = isFrontLonger ? backName : frontName;
        const QString text = tr("%1 has %Ln pages, %2 only %3. The extra pages are placed at the end of the result. Check whether a sheet was skipped while scanning, or choose Extra pages: Leave out.", nullptr, PDFAppInterleaveToolGUIHelper::toInt(qMax(front, back)))
                                 .arg(longer, shorter, PDFAppLocale::formatNumber(qMin(front, back)));
        m_unevenCallout->setTexts(title, text);
        m_unevenCallout->setVisible(hasScans && unpaired > 0);
    }
}

// ------------------------------------------------------------- texts

QString PDFAppInterleaveToolGUI::getConfirmText() const
{
    // 385:13263 "Create PDF"
    return tr("Create PDF");
}

QString PDFAppInterleaveToolGUI::getConfirmIconName() const
{
    return QStringLiteral("file-plus");
}

bool PDFAppInterleaveToolGUI::isConfirmEnabled() const
{
    return BaseClass::isConfirmEnabled() && hasBothScans() && !getTileCaptions().isEmpty();
}

QString PDFAppInterleaveToolGUI::getStatusText() const
{
    const QString title = getTool() ? getTool()->getTitle() : tr("Interleave Pages");
    if (!hasBothScans())
    {
        // 385:13738
        return tr("%1 · choose the file with the front sides and the file with the back sides").arg(title);
    }

    const PDFInteger front = getSlotPageCount(FrontSlot);
    const PDFInteger back = getSlotPageCount(BackSlot);
    if (front != back)
    {
        // 385:14487
        const int difference = int(qAbs(front - back));
        const QString sides = front > back ? tr("%Ln front sides have no back side", nullptr, difference) : tr("%Ln back sides have no front side", nullptr, difference);
        return tr("%1 · the files differ by %Ln pages – %2", nullptr, difference).arg(title, sides);
    }

    // 385:13263
    const int resultCount = getTileCaptions().size();
    return tr("%1 · %2 → %Ln pages · the source files are not changed", nullptr, resultCount).arg(title, getSidesText());
}

QString PDFAppInterleaveToolGUI::getRunningStatusText(const QString& stepText) const
{
    const QString title = getTool() ? getTool()->getTitle() : tr("Interleave Pages");
    return stepText.isEmpty() ? tr("%1 · creating the PDF").arg(title) : tr("%1 · %2").arg(title, stepText);
}

void PDFAppInterleaveToolGUI::fillResultInput(PDFAppToolResultInput* input) const
{
    // 385:14973 "Interleave finished"
    input->title = tr("Interleave finished");
}

QString PDFAppInterleaveToolGUI::getOwnTabLossText() const
{
    // K16: the dot of the tab and the question at its close
    const int count = m_list ? m_list->getCount() : (getInputList() ? getInputList()->getCount() : 0);
    return count > 0 ? tr("The list of %Ln files is lost.", nullptr, count) : QString();
}

QString PDFAppInterleaveToolGUI::getOwnTabPageText() const
{
    // K48: "12 pages"
    const int count = getTileCaptions().size();
    return count > 0 ? tr("%Ln pages", nullptr, count) : tr("No pages");
}

QString PDFAppInterleaveToolGUI::getOwnTabZoomText() const
{
    // No zoom in the own tab of Interleave (as Merge, K12 of step 2)
    return QString();
}

// ------------------------------------------------------------- flow

QVariantMap PDFAppInterleaveToolGUI::getRunParameters() const
{
    // The run gets the two usable scans in their slots
    QVariantMap parameters = BaseClass::getRunParameters();
    QStringList ids;
    for (const QString& id : getSlotInputIds())
    {
        if (!id.isEmpty())
        {
            ids << id;
        }
    }
    parameters.insert(PDFAppInterleaveToolGUIHelper::name(PDFDocumentToolInterleave::PARAMETER_INPUTS), ids.join(QLatin1Char(',')));
    return parameters;
}

void PDFAppInterleaveToolGUI::prepareEnvironment(PDFAppToolRunEnvironment* environment) const
{
    // R16 of step 2: the inputs of the run are the documents of the context (immutable shared
    // pointers, values)
    const PDFAppToolInputList* list = getInputList();
    for (int i = 0; list && i < list->getCount() && i < SlotCount; ++i)
    {
        const PDFAppToolInput* input = list->getInput(i);
        if (input->isUsable() && input->isPdf())
        {
            environment->additionalDocuments.push_back(input->createToolInputDocument());
        }
    }
}

void PDFAppInterleaveToolGUI::onParametersChanged(const QString& name)
{
    BaseClass::onParametersChanged(name);
    if (name == QLatin1String(PDFDocumentToolInterleave::PARAMETER_INPUTS))
    {
        readValues();
        return;
    }
    updateAll();
}

void PDFAppInterleaveToolGUI::onRunStateChanged(bool isRunning)
{
    m_isRunning = isRunning;
    BaseClass::onRunStateChanged(isRunning);
    if (m_changePanel)
    {
        m_changePanel->close();
    }
    updateSources();
}

}   // namespace pdf

#include "moc_pdfapptoolgui_interleave.cpp"
