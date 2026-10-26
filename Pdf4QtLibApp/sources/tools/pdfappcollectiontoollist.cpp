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

#include "pdfappcollectiontoollist.h"
#include "pdfapppagelayout.h"
#include "pdfapptoolpresentation.h"

#include "pdfcomponentbuttons.h"
#include "pdfcomponentrows.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdffluentwidgets.h"
#include "pdfstylepainter.h"
#include "pdfstylepainterguard.h"

#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QUuid>
#include <QVBoxLayout>

namespace pdf
{

PDFAppCollectionToolList::PDFAppCollectionToolList(QWidget* parent) :
    BaseClass(parent),
    m_instanceId(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    setObjectName(QStringLiteral("collectionToolList"));
    setAcceptDrops(true);

    // The paddings of the design lie inside of the border
    const int margin = PADDING + PDFAppPageWidgets::BORDER_WIDTH;
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(margin, margin, margin, margin);
    m_layout->setSpacing(SPACING);

    m_emptyLabel = new PDFLabel(QString(), PDFThemeFonts::Role::Small, PDFThemeColors::TextMuted, this);
    m_emptyLabel->setObjectName(QStringLiteral("collectionToolListEmpty"));
    m_emptyLabel->setTextFormat(Qt::PlainText);
    m_emptyLabel->setWordWrap(true);
    m_emptyLabel->setContentsMargins(8, 14, 8, 14);
    m_layout->addWidget(m_emptyLabel);

    retranslate();
    placeRows();
}

PDFAppCollectionToolList::~PDFAppCollectionToolList()
{
    // The rows are deleted by QWidget: their signals and events do not reach a
    // half destroyed list
    for (const Row& row : m_rows)
    {
        for (QObject* object : { static_cast<QObject*>(row.handle.data()), static_cast<QObject*>(row.toolRow.data()), static_cast<QObject*>(row.removeButton.data()) })
        {
            if (object)
            {
                object->removeEventFilter(this);
                object->disconnect(this);
            }
        }
    }
}

void PDFAppCollectionToolList::setTools(const QStringList& toolIds)
{
    QStringList tools;
    for (const QString& toolId : toolIds)
    {
        if (PDFDocumentToolCatalog::isValidToolId(toolId) && !tools.contains(toolId))
        {
            tools << toolId;
        }
    }

    if (tools == m_tools)
    {
        return;
    }

    // Rows of the remaining tools are kept (their focus too)
    std::vector<Row> rows;
    rows.reserve(size_t(tools.size()));
    for (const QString& toolId : tools)
    {
        const int index = findRowIndex(m_rows, toolId);
        if (index >= 0 && m_rows[size_t(index)].widget)
        {
            rows.push_back(m_rows[size_t(index)]);
            m_rows.erase(m_rows.begin() + index);
        }
        else
        {
            rows.push_back(createRow(toolId));
        }
    }

    for (const Row& row : m_rows)
    {
        if (row.widget)
        {
            // Later: the row may hold the sender of the current signal
            row.widget->hide();
            row.widget->deleteLater();
        }
    }

    m_rows = std::move(rows);
    m_tools = tools;
    placeRows();
}

QMimeData* PDFAppCollectionToolList::createMimeData(const QString& toolId) const
{
    QMimeData* mimeData = new QMimeData();
    mimeData->setData(getMimeType(), (m_instanceId + QLatin1Char('\n') + toolId).toUtf8());
    return mimeData;
}

QString PDFAppCollectionToolList::getDraggedTool(const QMimeData* mimeData) const
{
    if (!mimeData || !mimeData->hasFormat(getMimeType()))
    {
        return QString();
    }

    // Untrusted data: bounded, exactly the id of this list and a tool of the list
    const QByteArray payload = mimeData->data(getMimeType());
    if (payload.isEmpty() || payload.size() > MAXIMUM_MIME_DATA_SIZE)
    {
        return QString();
    }

    const QStringList parts = QString::fromUtf8(payload).split(QLatin1Char('\n'));
    if (parts.size() != 2 || parts.front() != m_instanceId || !m_tools.contains(parts.back()))
    {
        return QString();
    }

    return parts.back();
}

int PDFAppCollectionToolList::getDropIndex(const QPoint& position) const
{
    int index = 0;
    for (const Row& row : m_rows)
    {
        if (!row.widget)
        {
            continue;
        }

        if (position.y() < row.widget->geometry().center().y())
        {
            return index;
        }
        ++index;
    }
    return index;
}

bool PDFAppCollectionToolList::moveTool(const QString& toolId, int index)
{
    const qsizetype from = m_tools.indexOf(toolId);
    if (from < 0)
    {
        return false;
    }

    // The index is the place in the list before the move
    index = qBound(0, index, int(m_tools.size()));
    const qsizetype target = index > from ? index - 1 : index;
    if (target == from)
    {
        return false;
    }

    m_tools.move(from, target);
    const int rowIndex = findRowIndex(m_rows, toolId);
    if (rowIndex >= 0)
    {
        Row row = m_rows[size_t(rowIndex)];
        m_rows.erase(m_rows.begin() + rowIndex);
        m_rows.insert(m_rows.begin() + qMin<qsizetype>(target, qsizetype(m_rows.size())), row);
    }
    placeRows();

    QPointer<PDFAppCollectionToolList> guard(this);
    Q_EMIT toolsChanged(m_tools);
    return !guard.isNull();
}

bool PDFAppCollectionToolList::removeTool(const QString& toolId)
{
    const qsizetype index = m_tools.indexOf(toolId);
    if (index < 0)
    {
        return false;
    }

    const int rowIndex = findRowIndex(m_rows, toolId);
    bool hadFocus = false;
    if (rowIndex >= 0)
    {
        QWidget* rowWidget = m_rows[size_t(rowIndex)].widget.data();
        QWidget* focusWidget = QApplication::focusWidget();
        hadFocus = rowWidget && focusWidget && rowWidget->isAncestorOf(focusWidget);
        if (rowWidget)
        {
            // Later: the remove button of the row is the sender of the current click
            rowWidget->hide();
            rowWidget->deleteLater();
        }
        m_rows.erase(m_rows.begin() + rowIndex);
    }

    m_tools.removeAt(index);
    placeRows();
    if (hadFocus)
    {
        focusRowAfterRemoval(int(index));
    }

    QPointer<PDFAppCollectionToolList> guard(this);
    Q_EMIT toolsChanged(m_tools);
    return !guard.isNull();
}

QWidget* PDFAppCollectionToolList::findRow(const QString& toolId) const
{
    const Row* row = findRowData(toolId);
    return row ? row->widget.data() : nullptr;
}

PDFToolRow* PDFAppCollectionToolList::findToolRow(const QString& toolId) const
{
    const Row* row = findRowData(toolId);
    return row ? row->toolRow.data() : nullptr;
}

PDFIconButton* PDFAppCollectionToolList::findHandle(const QString& toolId) const
{
    const Row* row = findRowData(toolId);
    return row ? row->handle.data() : nullptr;
}

PDFIconButton* PDFAppCollectionToolList::findRemoveButton(const QString& toolId) const
{
    const Row* row = findRowData(toolId);
    return row ? row->removeButton.data() : nullptr;
}

bool PDFAppCollectionToolList::eventFilter(QObject* watched, QEvent* event)
{
    const QString toolId = getToolOfObject(watched);
    if (toolId.isEmpty())
    {
        return BaseClass::eventFilter(watched, event);
    }

    const Row* row = findRowData(toolId);
    const bool isHandle = row && watched == row->handle.data();

    switch (event->type())
    {
        case QEvent::MouseButtonPress:
        {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            if (isHandle && mouseEvent->button() == Qt::LeftButton)
            {
                m_pressedTool = toolId;
                m_pressPosition = mouseEvent->globalPosition().toPoint();
            }
            break;
        }

        case QEvent::MouseMove:
        {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            if (isHandle && m_pressedTool == toolId && mouseEvent->buttons().testFlag(Qt::LeftButton) && !m_isDragging &&
                (mouseEvent->globalPosition().toPoint() - m_pressPosition).manhattanLength() >= QApplication::startDragDistance())
            {
                startDrag(toolId);
                return true;
            }
            break;
        }

        case QEvent::MouseButtonRelease:
            if (isHandle)
            {
                m_pressedTool.clear();
            }
            break;

        case QEvent::KeyPress:
            if (handleKey(toolId, static_cast<QKeyEvent*>(event)))
            {
                return true;
            }
            break;

        default:
            break;
    }

    return BaseClass::eventFilter(watched, event);
}

void PDFAppCollectionToolList::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    const PDFStylePainterGuard guard(&painter);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // list 284:2300: the border border/default of radius 10, no fill (the card is below it)
    PDFStylePainter::strokeInside(&painter, QRectF(rect()), PDFStylePainter::Radii(RADIUS), PDFThemeColors::BorderDefault, 1.0);

    if (m_dropIndex < 0)
    {
        return;
    }

    // The drop indicator lies in the gap before the row of the index (after the last row)
    int y = PADDING + PDFAppPageWidgets::BORDER_WIDTH - DROP_INDICATOR_WIDTH / 2;
    int index = 0;
    for (const Row& row : m_rows)
    {
        if (!row.widget || row.widget->isHidden())
        {
            continue;
        }

        const QRect geometry = row.widget->geometry();
        if (index == m_dropIndex)
        {
            y = geometry.top() - (SPACING + DROP_INDICATOR_WIDTH) / 2;
            break;
        }
        y = geometry.bottom() + 1 + (SPACING - DROP_INDICATOR_WIDTH) / 2;
        ++index;
    }

    const int inset = PADDING + PDFAppPageWidgets::BORDER_WIDTH;
    const QRectF indicator(inset, qMax(0, y), qMax(0, width() - 2 * inset), DROP_INDICATOR_WIDTH);
    PDFStylePainter::fill(&painter, indicator, PDFStylePainter::Radii(DROP_INDICATOR_WIDTH / 2.0), PDFThemeColors::BorderFocus);
}

void PDFAppCollectionToolList::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    if (event->type() == QEvent::LanguageChange)
    {
        retranslate();
    }
}

void PDFAppCollectionToolList::dragEnterEvent(QDragEnterEvent* event)
{
    if (getDraggedTool(event->mimeData()).isEmpty())
    {
        event->ignore();
        return;
    }

    event->setDropAction(Qt::MoveAction);
    event->accept();
    setDropIndex(getDropIndex(event->position().toPoint()));
}

void PDFAppCollectionToolList::dragMoveEvent(QDragMoveEvent* event)
{
    if (getDraggedTool(event->mimeData()).isEmpty())
    {
        setDropIndex(-1);
        event->ignore();
        return;
    }

    event->setDropAction(Qt::MoveAction);
    event->accept();
    setDropIndex(getDropIndex(event->position().toPoint()));
}

void PDFAppCollectionToolList::dragLeaveEvent(QDragLeaveEvent* event)
{
    Q_UNUSED(event);
    setDropIndex(-1);
}

void PDFAppCollectionToolList::dropEvent(QDropEvent* event)
{
    setDropIndex(-1);

    const QString toolId = getDraggedTool(event->mimeData());
    if (toolId.isEmpty())
    {
        event->ignore();
        return;
    }

    event->setDropAction(Qt::MoveAction);
    event->accept();

    // A slot of toolsChanged may delete the list
    moveTool(toolId, getDropIndex(event->position().toPoint()));
}

PDFAppCollectionToolList::Row PDFAppCollectionToolList::createRow(const QString& toolId)
{
    Row row;
    row.toolId = toolId;

    const PDFDocumentToolCatalogEntry* tool = PDFDocumentToolCatalog::findTool(toolId);
    if (!tool)
    {
        return row;
    }

    // tool-<name> 284:2301: the handle, the Tool row without the star, the remove button
    QWidget* widget = new QWidget(this);
    widget->setObjectName(QStringLiteral("collectionTool-") + toolId);
    QHBoxLayout* layout = new QHBoxLayout(widget);
    layout->setContentsMargins(0, 0, ROW_TRAILING_PADDING, 0);
    layout->setSpacing(ROW_SPACING);

    row.handle = new PDFIconButton(QStringLiteral("more-vertical"), PDFIconButton::Size::Small, widget);
    row.handle->setObjectName(QStringLiteral("dragHandle"));
    row.handle->setFocusPolicy(Qt::NoFocus);
    row.handle->setCursor(Qt::OpenHandCursor);
    row.handle->setProperty(PDFAppToolPresentation::TOOL_PROPERTY, toolId);
    row.handle->installEventFilter(this);
    layout->addWidget(row.handle, 0, Qt::AlignVCenter);

    row.toolRow = PDFAppToolPresentation::createToolRow(*tool, false, widget);
    row.toolRow->setStarVisible(false);
    row.toolRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    row.toolRow->installEventFilter(this);
    connect(row.toolRow.data(), &QAbstractButton::clicked, this, &PDFAppCollectionToolList::onToolRowClicked);
    layout->addWidget(row.toolRow, 1);

    row.removeButton = new PDFIconButton(QStringLiteral("x"), PDFIconButton::Size::Small, widget);
    row.removeButton->setObjectName(QStringLiteral("removeTool"));
    row.removeButton->setProperty(PDFAppToolPresentation::TOOL_PROPERTY, toolId);
    row.removeButton->installEventFilter(this);
    connect(row.removeButton.data(), &QAbstractButton::clicked, this, &PDFAppCollectionToolList::onRemoveClicked);
    layout->addWidget(row.removeButton, 0, Qt::AlignVCenter);

    row.widget = widget;
    updateRowTexts(row);
    return row;
}

void PDFAppCollectionToolList::updateRowTexts(const Row& row) const
{
    const PDFDocumentToolCatalogEntry* tool = PDFDocumentToolCatalog::findTool(row.toolId);
    if (!tool)
    {
        return;
    }

    const QString title = tool->getTitle();
    if (row.toolRow)
    {
        row.toolRow->setName(title);
        row.toolRow->setDescription(tool->getDescription());
        row.toolRow->setTag(tool->isAdvanced() ? PDFAppToolPresentation::getAdvancedTagText() : QString());
        row.toolRow->setToolTip(tr("Alt+Up or Alt+Down moves the tool, Delete removes it"));
    }
    if (row.handle)
    {
        row.handle->setAccessibleName(tr("Move %1").arg(title));
        row.handle->setToolTip(tr("Drag to change the order"));
    }
    if (row.removeButton)
    {
        row.removeButton->setAccessibleName(tr("Remove %1").arg(title));
        row.removeButton->setToolTip(tr("Remove from the collection"));
    }
}

void PDFAppCollectionToolList::placeRows()
{
    if (!m_layout)
    {
        return;
    }

    // The rows in the order of the tools, then the text of the empty list
    QWidget* previous = nullptr;
    for (const Row& row : m_rows)
    {
        if (!row.widget)
        {
            continue;
        }

        m_layout->removeWidget(row.widget);
        m_layout->addWidget(row.widget);
        row.widget->show();

        // Tab goes through the rows in their order (the tool row, then its remove button)
        for (QWidget* widget : { static_cast<QWidget*>(row.toolRow.data()), static_cast<QWidget*>(row.removeButton.data()) })
        {
            if (widget)
            {
                if (previous)
                {
                    QWidget::setTabOrder(previous, widget);
                }
                previous = widget;
            }
        }
    }

    if (m_emptyLabel)
    {
        m_layout->removeWidget(m_emptyLabel);
        m_layout->addWidget(m_emptyLabel);
        m_emptyLabel->setVisible(m_tools.isEmpty());
    }

    updateGeometry();
    update();
}

void PDFAppCollectionToolList::retranslate()
{
    if (m_emptyLabel)
    {
        m_emptyLabel->setText(tr("No tools in this collection yet. Add a tool below."));
    }

    for (const Row& row : m_rows)
    {
        updateRowTexts(row);
    }
}

int PDFAppCollectionToolList::findRowIndex(const std::vector<Row>& rows, const QString& toolId)
{
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (rows[i].toolId == toolId)
        {
            return int(i);
        }
    }
    return -1;
}

const PDFAppCollectionToolList::Row* PDFAppCollectionToolList::findRowData(const QString& toolId) const
{
    for (const Row& row : m_rows)
    {
        if (row.toolId == toolId && row.widget)
        {
            return &row;
        }
    }
    return nullptr;
}

QString PDFAppCollectionToolList::getToolOfObject(const QObject* object) const
{
    const QString toolId = PDFAppToolPresentation::getToolId(object);
    const Row* row = findRowData(toolId);
    if (!row || (object != row->handle.data() && object != row->toolRow.data() && object != row->removeButton.data()))
    {
        return QString();
    }
    return toolId;
}

void PDFAppCollectionToolList::setDropIndex(int index)
{
    if (m_dropIndex != index)
    {
        m_dropIndex = index;
        update();
    }
}

void PDFAppCollectionToolList::startDrag(const QString& toolId)
{
    QWidget* rowWidget = findRow(toolId);
    if (!rowWidget)
    {
        return;
    }

    QDrag* drag = new QDrag(this);
    drag->setMimeData(createMimeData(toolId));
    drag->setPixmap(rowWidget->grab());
    drag->setHotSpot(QPoint(PADDING, rowWidget->height() / 2));

    // QDrag::exec runs its own event loop: anything may be deleted meanwhile
    QPointer<PDFAppCollectionToolList> guard(this);
    m_isDragging = true;
    drag->exec(Qt::MoveAction, Qt::MoveAction);
    if (!guard)
    {
        return;
    }

    m_isDragging = false;
    m_pressedTool.clear();
    setDropIndex(-1);

    // The drag has taken the mouse: the handle may never get the release of its
    // press (the drag loop of Windows eats it) and would stay drawn pressed (review RC6-2)
    if (PDFIconButton* handle = findHandle(toolId))
    {
        handle->setDown(false);
    }
}

bool PDFAppCollectionToolList::handleKey(const QString& toolId, QKeyEvent* event)
{
    const int index = int(m_tools.indexOf(toolId));
    if (index < 0)
    {
        return false;
    }

    const Qt::KeyboardModifiers modifiers = event->modifiers() & ~Qt::KeypadModifier;
    if (modifiers == Qt::AltModifier)
    {
        switch (event->key())
        {
            case Qt::Key_Up:
                moveTool(toolId, index - 1);
                return true;

            case Qt::Key_Down:
                moveTool(toolId, index + 2);
                return true;

            case Qt::Key_Home:
                moveTool(toolId, 0);
                return true;

            case Qt::Key_End:
                moveTool(toolId, int(m_tools.size()));
                return true;

            default:
                break;
        }
    }
    else if (modifiers == Qt::NoModifier && event->key() == Qt::Key_Delete)
    {
        removeTool(toolId);
        return true;
    }

    return false;
}

void PDFAppCollectionToolList::focusRowAfterRemoval(int removedIndex)
{
    // The next row (the previous one after the last row) takes the focus
    if (m_rows.empty())
    {
        if (QWidget* next = nextInFocusChain())
        {
            next->setFocus(Qt::TabFocusReason);
        }
        return;
    }

    const size_t index = size_t(qBound(0, removedIndex, int(m_rows.size()) - 1));
    if (m_rows[index].toolRow)
    {
        m_rows[index].toolRow->setFocus(Qt::TabFocusReason);
    }
}

void PDFAppCollectionToolList::onToolRowClicked()
{
    const QString toolId = getToolOfObject(sender());
    if (!toolId.isEmpty())
    {
        Q_EMIT toolActivated(toolId);
    }
}

void PDFAppCollectionToolList::onRemoveClicked()
{
    const QString toolId = getToolOfObject(sender());
    if (!toolId.isEmpty())
    {
        removeTool(toolId);
    }
}

}   // namespace pdf
