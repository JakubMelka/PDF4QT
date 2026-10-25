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

#include "pdfappoutlinetarget.h"
#include "pdfappannotationcreatetools.h"
#include "pdfappannotationcreator.h"
#include "pdfappannotationoperations.h"
#include "pdfappannotationtoolbar.h"
#include "pdfappdocument.h"
#include "pdfappdocumentpanels_common.h"
#include "pdfappdocumentstatusbar.h"
#include "pdfappdocumentview.h"
#include "pdfappfloatingbar.h"

#include "pdfcomponentbuttons.h"
#include "pdffluentwidgets.h"
#include "pdfthemefonts.h"

#include "pdfcatalog.h"
#include "pdfdocument.h"
#include "pdfdrawwidget.h"
#include "pdfwidgettool.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>

#include <algorithm>

namespace pdf
{

/// Values of the outline target picker and of the dialog of the named destinations
class PDFAppOutlineTargetHelper
{
public:
    PDFAppOutlineTargetHelper() = delete;

    /// Longest title of the item in the bar
    static constexpr int MAXIMUM_TITLE_LENGTH = 40;

    /// Role of the raw name of a row of the named destinations
    static constexpr int NameRole = Qt::UserRole + 1;

    /// Title of the item shortened for the bar
    static QString getShortTitle(const QString& title)
    {
        const QString simplified = PDFAppAnnotationOperations::sanitizeOutlineTitle(title);
        return simplified.size() > MAXIMUM_TITLE_LENGTH ? simplified.left(MAXIMUM_TITLE_LENGTH - 1) + QChar(0x2026) : simplified;
    }

    /// Order of the names (byte order - the names are PDF strings)
    static bool isNameLess(const QByteArray& left, const QByteArray& right)
    {
        return left < right;
    }
};

// ---------------------------------------------------------------------- PDFAppOutlineTargetPicker

PDFAppOutlineTargetPicker::PDFAppOutlineTargetPicker(PDFAppDocumentView* view, QObject* parent) :
    BaseClass(parent),
    m_view(view)
{
    setObjectName(QStringLiteral("outlineTargetPicker"));
}

PDFAppOutlineTargetPicker::~PDFAppOutlineTargetPicker()
{
    // The tool is turned off before it dies (it is a child of the picker); the chosen
    // mode of the view (Hand or Select text) becomes active again
    if (m_toolManager)
    {
        m_toolManager->disconnect(this);
        if (m_tool && m_toolManager->getActiveTool() == m_tool)
        {
            if (m_view && m_view->getToolManager() == m_toolManager.data())
            {
                m_view->activateDefaultCanvasTool();
            }
            else
            {
                m_toolManager->setActiveTool(PDFToolManager::SelectTextTool);
            }
        }
    }
    if (m_canvas)
    {
        m_canvas->removeEventFilter(this);
    }
    if (m_view && m_isActive && !m_statusHint.isEmpty())
    {
        PDFAppDocumentStatusBar* statusBar = m_view->getStatusBar();
        if (statusBar && statusBar->getMessage() == m_statusHint)
        {
            statusBar->showMessage(PDFAppDocumentView::tr("Ready"));
        }
    }
    delete m_bar.data();
}

bool PDFAppOutlineTargetPicker::isPickedType(DestinationType type)
{
    return PDFAppPageLinkTool::isTarget(type, true);
}

PDFToolManager* PDFAppOutlineTargetPicker::getToolManager() const
{
    return m_view ? m_view->getToolManager() : nullptr;
}

QWidget* PDFAppOutlineTargetPicker::getCanvasWidget() const
{
    PDFWidget* widget = m_view ? m_view->getPdfWidget() : nullptr;
    return widget && widget->getDrawWidget() ? widget->getDrawWidget()->getWidget() : nullptr;
}

PDFAppPageLinkTool* PDFAppOutlineTargetPicker::getTool() const
{
    return m_tool.data();
}

PDFAppFloatingBar* PDFAppOutlineTargetPicker::getBar() const
{
    return m_bar.data();
}

PDFLabel* PDFAppOutlineTargetPicker::getInstructionLabel() const
{
    return m_instruction.data();
}

PDFIconButton* PDFAppOutlineTargetPicker::getCloseButton() const
{
    return m_closeButton.data();
}

bool PDFAppOutlineTargetPicker::start(DestinationType type, bool keepZoom, const QString& itemTitle)
{
    PDFToolManager* toolManager = getToolManager();
    PDFAppDocument* document = m_view ? m_view->getDocument() : nullptr;
    if (!isPickedType(type) || !toolManager || !document || !document->isReady() || document->getDocument().isNull())
    {
        return false;
    }

    QPointer<PDFAppOutlineTargetPicker> guard(this);
    if (m_isActive)
    {
        cancel();
        if (!guard)
        {
            return false;
        }
    }

    // The tool of the step 2 of Link to page in its target-only mode. The signals of
    // the tool (its step, its target) may delete the tab with the picker: every
    // emission is followed by the check of the guards.
    QPointer<PDFToolManager> toolManagerGuard(toolManager);
    if (!m_tool)
    {
        m_tool = new PDFAppPageLinkTool(m_view->getProxy(), this);
        m_tool->setObjectName(QStringLiteral("outlineTargetTool"));
        connect(m_tool.data(), &PDFAppPageLinkTool::targetPicked, this, &PDFAppOutlineTargetPicker::onToolTargetPicked);
        m_tool->setTargetOnly(true);
        if (!guard || !m_tool || !toolManagerGuard)
        {
            return false;
        }
    }
    if (m_toolManager != toolManager)
    {
        if (m_toolManager)
        {
            m_toolManager->disconnect(this);
        }
        m_toolManager = toolManager;
        toolManager->addTool(m_tool);
        connect(toolManager, &PDFToolManager::activeToolChanged, this, &PDFAppOutlineTargetPicker::onActiveToolChanged);
    }

    // The tool gets the shown document (the later versions come from the tool manager)
    m_tool->setDocument(PDFModifiedDocument(document->getDocument(), nullptr, PDFModifiedDocument::Annotation));
    if (!guard || !m_tool || !toolManagerGuard)
    {
        return false;
    }
    m_tool->setDestinationType(type);
    if (!guard || !m_tool || !toolManagerGuard)
    {
        return false;
    }
    m_tool->setKeepZoom(keepZoom);

    m_type = type;
    m_itemTitle = itemTitle;
    m_isStarting = true;
    toolManager->setActiveTool(m_tool);
    if (!guard)
    {
        return false;
    }
    m_isStarting = false;
    if (!m_toolManager || m_toolManager->getActiveTool() != m_tool)
    {
        return false;
    }
    m_isActive = true;

    // Esc of the canvas ends the picking; the canvas takes the focus from the menu
    QWidget* canvas = getCanvasWidget();
    if (canvas != m_canvas)
    {
        if (m_canvas)
        {
            m_canvas->removeEventFilter(this);
        }
        m_canvas = canvas;
        if (canvas)
        {
            canvas->installEventFilter(this);
        }
    }
    if (m_canvas)
    {
        m_canvas->setFocus(Qt::OtherFocusReason);
        if (!guard)
        {
            return false;
        }
    }

    createBar();
    updateTexts();
    if (m_bar)
    {
        m_bar->placeAtTop();
        m_bar->show();
        if (!guard)
        {
            return false;
        }
        if (m_bar)
        {
            m_bar->raise();
        }
    }

    if (PDFAppDocumentStatusBar* statusBar = m_view ? m_view->getStatusBar() : nullptr)
    {
        m_statusHint = PDFAppAnnotationTools::getPageLinkInstruction(type);
        statusBar->showMessage(m_statusHint);
    }
    return guard && m_isActive;
}

void PDFAppOutlineTargetPicker::createBar()
{
    QWidget* canvasArea = m_view ? m_view->getCanvasArea() : nullptr;
    if (m_bar || !canvasArea)
    {
        return;
    }

    // The bar of a tool with a hint (1168:26016): the target 16 + its name Semi
    // Bold 13, the instruction Medium 13 (1170:27204), close 20 (207:6299)
    m_bar = new PDFAppFloatingBar(PDFAppFloatingBar::Size::Normal, canvasArea);
    m_bar->setObjectName(QStringLiteral("outlineTargetBar"));
    m_bar->setCardHeight(PDFAppAnnotationToolBar::HINT_HEIGHT);
    m_bar->installEventFilter(this);

    QWidget* toolBlock = new QWidget(m_bar);
    QHBoxLayout* toolLayout = new QHBoxLayout(toolBlock);
    toolLayout->setContentsMargins(0, 0, 0, 0);
    toolLayout->setSpacing(8);
    m_icon = new PDFAppFloatingBarIcon(QString(), 16, toolBlock);
    m_name = new PDFLabel(QString(), PDFThemeFonts::Role::BodySemiBold, PDFThemeColors::TextPrimary, toolBlock);
    m_name->setObjectName(QStringLiteral("outlineTargetName"));
    m_name->setTextFormat(Qt::PlainText);
    toolLayout->addWidget(m_icon);
    toolLayout->addWidget(m_name);
    m_bar->addWidget(toolBlock);
    m_bar->addDivider();

    m_instruction = new PDFLabel(QString(), PDFThemeFonts::Role::BodyMedium, PDFThemeColors::TextPrimary, m_bar);
    m_instruction->setObjectName(QStringLiteral("outlineTargetInstruction"));
    m_instruction->setTextFormat(Qt::PlainText);
    m_bar->addWidget(m_instruction);
    m_bar->addDivider();

    m_closeButton = new PDFIconButton(QStringLiteral("x"), PDFIconButton::Size::TabClose, m_bar);
    m_closeButton->setObjectName(QStringLiteral("outlineTargetClose"));
    connect(m_closeButton.data(), &QAbstractButton::clicked, this, &PDFAppOutlineTargetPicker::onCloseClicked);
    m_bar->addWidget(m_closeButton);
}

void PDFAppOutlineTargetPicker::updateTexts()
{
    if (m_icon)
    {
        m_icon->setIconName(PDFAppAnnotationTools::getPageLinkTargetIconName(m_type));
        m_icon->setToolTip(PDFAppAnnotationTools::getPageLinkTargetDescription(m_type));
    }
    if (m_name)
    {
        //: Floating bar of the picking of the target of an outline item: the target view and the item
        m_name->setText(tr("%1 for “%2”").arg(PDFAppAnnotationTools::getPageLinkTargetName(m_type), PDFAppOutlineTargetHelper::getShortTitle(m_itemTitle)));
        m_name->setToolTip(PDFAppAnnotationOperations::sanitizeOutlineTitle(m_itemTitle));
    }
    if (m_instruction)
    {
        m_instruction->setText(PDFAppAnnotationTools::getPageLinkInstruction(m_type));
    }
    if (m_closeButton)
    {
        m_closeButton->setToolTip(tr("Cancel (Esc)"));
        m_closeButton->setAccessibleName(tr("Cancel the picking of the target"));
    }
    if (m_bar)
    {
        m_bar->updatePlacement();
    }
}

void PDFAppOutlineTargetPicker::pickTarget(PDFInteger pageIndex, const QPointF& point, const QRectF& rectangle)
{
    if (m_isActive && m_tool)
    {
        m_tool->pickTarget(pageIndex, point, rectangle);
    }
}

void PDFAppOutlineTargetPicker::finish()
{
    if (!m_isActive)
    {
        return;
    }
    m_isActive = false;

    if (m_bar)
    {
        m_bar->hide();
    }
    if (m_view && !m_statusHint.isEmpty())
    {
        PDFAppDocumentStatusBar* statusBar = m_view->getStatusBar();
        if (statusBar && statusBar->getMessage() == m_statusHint)
        {
            statusBar->showMessage(PDFAppDocumentView::tr("Ready"));
        }
    }
    m_statusHint.clear();

    // The chosen mode of the view (Select text or Hand) becomes active again
    // (activeToolChanged comes back here and finds the picking ended)
    if (m_toolManager && m_tool && m_toolManager->getActiveTool() == m_tool)
    {
        if (m_view && m_view->getToolManager() == m_toolManager.data())
        {
            m_view->activateDefaultCanvasTool();
        }
        else
        {
            m_toolManager->setActiveTool(PDFToolManager::SelectTextTool);
        }
    }
}

void PDFAppOutlineTargetPicker::cancel()
{
    m_isPickPending = false;
    if (!m_isActive)
    {
        return;
    }

    QPointer<PDFAppOutlineTargetPicker> guard(this);
    finish();
    if (guard)
    {
        Q_EMIT canceled();
    }
}

void PDFAppOutlineTargetPicker::onToolTargetPicked(PDFDestination destination)
{
    if (!m_isActive || m_isPickPending)
    {
        return;
    }

    // The tool is in its mouse event: the picking ends from the event loop
    m_pickedDestination = destination;
    m_isPickPending = true;
    QMetaObject::invokeMethod(this, &PDFAppOutlineTargetPicker::onPickedQueued, Qt::QueuedConnection);
}

void PDFAppOutlineTargetPicker::onPickedQueued()
{
    if (!m_isPickPending || !m_isActive)
    {
        m_isPickPending = false;
        return;
    }
    m_isPickPending = false;

    const PDFDestination destination = m_pickedDestination;
    m_pickedDestination = PDFDestination();
    QPointer<PDFAppOutlineTargetPicker> guard(this);
    finish();
    if (guard)
    {
        Q_EMIT targetPicked(destination);
    }
}

void PDFAppOutlineTargetPicker::onActiveToolChanged(PDFWidgetTool* tool)
{
    // Another tool (the Select menu, a creation tool, Esc of the engine) ends the picking
    if (m_isStarting || !m_isActive || (tool && tool == m_tool))
    {
        return;
    }
    cancel();
}

void PDFAppOutlineTargetPicker::onCloseClicked()
{
    // The bar is hidden, not deleted from the signal of its button
    cancel();
}

bool PDFAppOutlineTargetPicker::eventFilter(QObject* watched, QEvent* event)
{
    if (m_isActive && (watched == m_canvas || watched == m_bar) && event->type() == QEvent::KeyPress)
    {
        const QKeyEvent* keyEvent = static_cast<const QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape && keyEvent->modifiers() == Qt::NoModifier)
        {
            event->accept();
            cancel();
            return true;
        }
    }
    if (watched == m_bar && event->type() == QEvent::LanguageChange)
    {
        updateTexts();
    }
    return BaseClass::eventFilter(watched, event);
}

// ---------------------------------------------------------------------- PDFAppNamedDestinationDialog

PDFAppNamedDestinationDialog::PDFAppNamedDestinationDialog(const PDFDocument* document, const QString& itemTitle, QWidget* parent) :
    BaseClass(PDFThemeIcons::Badge::Neutral, tr("Named Destination"),
              tr("The item “%1” goes to a place, which the document has named.").arg(PDFAppOutlineTargetHelper::getShortTitle(itemTitle)),
              QDialogButtonBox::NoButton, parent)
{
    setObjectName(QStringLiteral("namedDestinationDialog"));
    setBadgeVisible(false);
    setCardWidth(CARD_WIDTH);

    m_searchField = PDFAppPanelWidgets::createSearchField(this, tr("Search destinations…"), QStringLiteral("namedDestinationSearch"));
    m_searchField->setMaxLength(MAXIMUM_SHOWN_LENGTH);
    m_searchField->setAccessibleName(tr("Search destinations"));
    addContent(m_searchField);

    m_model = new QStandardItemModel(this);
    for (const QByteArray& name : getNames(document))
    {
        QStandardItem* item = new QStandardItem(getDisplayName(name));
        item->setEditable(false);
        item->setData(name, PDFAppOutlineTargetHelper::NameRole);
        item->setToolTip(getDisplayName(name));
        m_model->appendRow(item);
    }

    m_filterModel = new QSortFilterProxyModel(this);
    m_filterModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_filterModel->setSourceModel(m_model);

    m_list = new PDFTreeView(this);
    m_list->setObjectName(QStringLiteral("namedDestinationList"));
    m_list->setHeaderHidden(true);
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->setTextElideMode(Qt::ElideMiddle);
    m_list->setAccessibleName(tr("Named destinations"));
    m_list->setModel(m_filterModel);
    m_list->setFixedHeight(LIST_HEIGHT);
    addContent(m_list);

    // Empty state: a document without named destinations
    m_emptyLabel = new PDFLabel(tr("This document has no named destinations."), PDFThemeFonts::Role::Small, PDFThemeColors::TextMuted, this);
    m_emptyLabel->setObjectName(QStringLiteral("namedDestinationEmpty"));
    m_emptyLabel->setTextFormat(Qt::PlainText);
    m_emptyLabel->setWordWrap(true);
    addContent(m_emptyLabel);

    const bool isEmpty = m_model->rowCount() == 0;
    m_emptyLabel->setVisible(isEmpty);
    m_list->setVisible(!isEmpty);
    m_searchField->setVisible(!isEmpty);

    m_cancelButton = addActionButton(tr("Cancel"), ActionPosition::Trailing, false);
    m_cancelButton->setObjectName(QStringLiteral("namedDestinationCancel"));
    m_setButton = addActionButton(tr("Set Target"), ActionPosition::Trailing, true);
    m_setButton->setObjectName(QStringLiteral("namedDestinationSet"));

    connect(m_cancelButton.data(), &QPushButton::clicked, this, &QDialog::reject);
    connect(m_setButton.data(), &QPushButton::clicked, this, &PDFAppNamedDestinationDialog::onSetClicked);
    connect(m_searchField.data(), &QLineEdit::textChanged, this, &PDFAppNamedDestinationDialog::onFilterTextChanged);
    connect(m_searchField.data(), &QLineEdit::returnPressed, this, &PDFAppNamedDestinationDialog::onSetClicked);
    connect(m_list.data(), &QAbstractItemView::doubleClicked, this, &PDFAppNamedDestinationDialog::onDoubleClicked);
    connect(m_list->selectionModel(), &QItemSelectionModel::currentChanged, this, &PDFAppNamedDestinationDialog::onCurrentChanged);

    if (!isEmpty)
    {
        m_list->setCurrentIndex(m_filterModel->index(0, 0));
    }
    onCurrentChanged();
}

PDFAppNamedDestinationDialog::~PDFAppNamedDestinationDialog()
{
    if (m_list && m_list->selectionModel())
    {
        m_list->selectionModel()->disconnect(this);
    }
}

QList<QByteArray> PDFAppNamedDestinationDialog::getNames(const PDFDocument* document)
{
    QList<QByteArray> names;
    if (!document)
    {
        return names;
    }

    for (const auto& namedDestination : document->getCatalog()->getNamedDestinations())
    {
        const QByteArray& name = namedDestination.first;
        if (name.isEmpty() || name.size() > PDFAppAnnotationOperations::MAXIMUM_OUTLINE_TITLE_LENGTH)
        {
            continue;
        }
        names.push_back(name);
        if (names.size() >= MAXIMUM_NAMES)
        {
            break;
        }
    }
    std::sort(names.begin(), names.end(), PDFAppOutlineTargetHelper::isNameLess);
    return names;
}

QString PDFAppNamedDestinationDialog::getDisplayName(const QByteArray& name)
{
    // A name is a PDF string: UTF-8 when it is valid, else Latin-1
    QString text = QString::fromUtf8(name);
    if (text.contains(QChar::ReplacementCharacter))
    {
        text = QString::fromLatin1(name);
    }

    for (QChar& character : text)
    {
        const QChar::Category category = character.category();
        if (category == QChar::Other_Control || category == QChar::Other_Format || category == QChar::Other_Surrogate ||
            character == QChar::LineSeparator || character == QChar::ParagraphSeparator)
        {
            character = QChar(u' ');
        }
    }
    if (text.size() > MAXIMUM_SHOWN_LENGTH)
    {
        text = text.left(MAXIMUM_SHOWN_LENGTH - 1) + QChar(0x2026);
    }
    return text;
}

QByteArray PDFAppNamedDestinationDialog::getSelectedName() const
{
    const QModelIndex index = m_list ? m_list->currentIndex() : QModelIndex();
    return index.isValid() ? index.data(PDFAppOutlineTargetHelper::NameRole).toByteArray() : QByteArray();
}

bool PDFAppNamedDestinationDialog::selectName(const QByteArray& name)
{
    if (!m_list || !m_filterModel)
    {
        return false;
    }

    for (int row = 0; row < m_filterModel->rowCount(); ++row)
    {
        const QModelIndex index = m_filterModel->index(row, 0);
        if (index.data(PDFAppOutlineTargetHelper::NameRole).toByteArray() == name)
        {
            m_list->setCurrentIndex(index);
            m_list->scrollTo(index);
            return true;
        }
    }
    return false;
}

int PDFAppNamedDestinationDialog::getNameCount() const
{
    return m_model ? m_model->rowCount() : 0;
}

PDFLineEdit* PDFAppNamedDestinationDialog::getSearchField() const
{
    return m_searchField.data();
}

PDFTreeView* PDFAppNamedDestinationDialog::getList() const
{
    return m_list.data();
}

PDFLabel* PDFAppNamedDestinationDialog::getEmptyLabel() const
{
    return m_emptyLabel.data();
}

QPushButton* PDFAppNamedDestinationDialog::getSetButton() const
{
    return m_setButton.data();
}

QPushButton* PDFAppNamedDestinationDialog::getCancelButton() const
{
    return m_cancelButton.data();
}

void PDFAppNamedDestinationDialog::onFilterTextChanged(const QString& text)
{
    if (!m_filterModel || !m_list)
    {
        return;
    }

    m_filterModel->setFilterFixedString(text.trimmed());
    if (!m_list->currentIndex().isValid() && m_filterModel->rowCount() > 0)
    {
        m_list->setCurrentIndex(m_filterModel->index(0, 0));
    }
    onCurrentChanged();
}

void PDFAppNamedDestinationDialog::onCurrentChanged()
{
    if (m_setButton)
    {
        m_setButton->setEnabled(!getSelectedName().isEmpty());
    }
}

void PDFAppNamedDestinationDialog::onSetClicked()
{
    if (!getSelectedName().isEmpty())
    {
        accept();
    }
}

void PDFAppNamedDestinationDialog::onDoubleClicked()
{
    onSetClicked();
}

}   // namespace pdf
