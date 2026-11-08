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

#include "pdfapptoolinputrow.h"

#include "pdfapppagelayout.h"

#include "pdfcomponentbuttons.h"
#include "pdfcomponentpainter.h"
#include "pdffluentwidgets.h"
#include "pdfstylepainter.h"
#include "pdfstylepainterguard.h"
#include "pdfthemefonts.h"

#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QVBoxLayout>

namespace pdf
{

/// Fonts of the row of an input
class PDFAppToolInputRowHelper
{
public:
    PDFAppToolInputRowHelper() = delete;

    static QFont getNameFont() { return PDFThemeFonts::getFont(QFont::DemiBold, 12); }
    static QFont getLineFont() { return PDFThemeFonts::getFont(QFont::Normal, 11); }
    static QFont getLetterFont() { return PDFThemeFonts::getFont(QFont::DemiBold, 11); }

    /// Gap of the name and its Tag (378:1209)
    static constexpr int TAG_GAP = 6;

    /// Width of the line of a drop between two rows
    static constexpr int DROP_LINE = 2;
};

// ------------------------------------------------------------ PDFAppToolInputRow

PDFAppToolInputRow::PDFAppToolInputRow(QWidget* parent) :
    BaseClass(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(getHeight(m_content));
}

PDFAppToolInputRow::~PDFAppToolInputRow() = default;

int PDFAppToolInputRow::getHeight(const Content& content)
{
    int height = PADDING + (content.tag.isEmpty() ? NAME_HEIGHT : TAG_HEIGHT) + 2 * (LINE_GAP + LINE_HEIGHT) + PADDING;
    if (!content.linkText.isEmpty())
    {
        height += LINE_GAP + LINK_HEIGHT;
    }
    else if (content.progress >= 0.0)
    {
        height += LINE_GAP + PROGRESS_HEIGHT;
    }
    return height;
}

void PDFAppToolInputRow::setContent(const Content& content)
{
    if (m_content == content)
    {
        return;
    }

    m_content = content;
    setObjectName(QStringLiteral("toolInput-") + content.inputId);
    setText(content.name);
    QStringList description{ content.firstLine, content.secondLine };
    if (!content.tag.isEmpty())
    {
        description.prepend(content.tag);
    }
    description.removeAll(QString());
    setAccessibleDescription(description.join(QStringLiteral(", ")));
    setToolTip(content.toolTip);
    setFixedHeight(getHeight(content));
    updateChildren();
    update();
}

void PDFAppToolInputRow::setCurrent(bool isCurrent)
{
    if (m_isCurrent != isCurrent)
    {
        m_isCurrent = isCurrent;
        update();
    }
}

void PDFAppToolInputRow::setDraggable(bool isDraggable)
{
    m_isDraggable = isDraggable;
}

int PDFAppToolInputRow::getTextLeft() const
{
    return PADDING + TILE + GAP;
}

int PDFAppToolInputRow::getTextWidth() const
{
    return qMax(0, width() - getTextLeft() - PADDING);
}

void PDFAppToolInputRow::updateChildren()
{
    using Helper = PDFAppToolInputRowHelper;

    // The Tag after the name (a text of the row: the row takes the mouse over it)
    if (!m_content.tag.isEmpty() && !m_tag)
    {
        m_tag = new PDFTag(this);
        m_tag->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        m_tag->setFocusPolicy(Qt::NoFocus);
        m_tag->show();
    }
    else if (m_content.tag.isEmpty() && m_tag)
    {
        delete m_tag.data();
    }
    if (m_tag)
    {
        m_tag->setText(m_content.tag);
        const QSize tagSize = m_tag->sizeHint();
        const int nameWidth = qMin(getTextWidth() - tagSize.width() - Helper::TAG_GAP, int(std::ceil(PDFStylePainter::textWidth(m_content.name, Helper::getNameFont()))) + 1);
        const QRect logical(getTextLeft() + qMax(0, nameWidth) + Helper::TAG_GAP, PADDING, tagSize.width(), TAG_HEIGHT);
        m_tag->setGeometry(QStyle::visualRect(layoutDirection(), rect(), logical));
    }

    // The link below the lines ("Enter password…", "Remove file")
    if (!m_content.linkText.isEmpty() && !m_link)
    {
        m_link = new PDFTextLink(this);
        m_link->setObjectName(QStringLiteral("toolInputLink"));
        connect(m_link.data(), &PDFTextLink::clicked, this, &PDFAppToolInputRow::onLinkClicked);
        m_link->show();
    }
    else if (m_content.linkText.isEmpty() && m_link)
    {
        // The link may be the sender of the click, which has changed the content
        m_link->hide();
        m_link->deleteLater();
        m_link = nullptr;
    }
    if (m_link)
    {
        m_link->setText(m_content.linkText);
        const QSize linkSize = m_link->sizeHint();
        const int top = PADDING + (m_content.tag.isEmpty() ? NAME_HEIGHT : TAG_HEIGHT) + 2 * (LINE_GAP + LINE_HEIGHT) + LINE_GAP;
        const QRect logical(getTextLeft(), top, qMin(linkSize.width(), getTextWidth()), LINK_HEIGHT);
        m_link->setGeometry(QStyle::visualRect(layoutDirection(), rect(), logical));
    }
}

void PDFAppToolInputRow::onLinkClicked()
{
    Q_EMIT linkActivated(m_content.inputId);
}

void PDFAppToolInputRow::resizeEvent(QResizeEvent* event)
{
    BaseClass::resizeEvent(event);
    updateChildren();
}

void PDFAppToolInputRow::keyPressEvent(QKeyEvent* event)
{
    const Qt::KeyboardModifiers modifiers = event->modifiers();
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && modifiers == Qt::NoModifier && m_isDraggable)
    {
        event->accept();
        Q_EMIT removeRequested(m_content.inputId);
        return;
    }
    if (modifiers == Qt::AltModifier && (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down) && m_isDraggable)
    {
        event->accept();
        Q_EMIT moveRequested(m_content.inputId, event->key() == Qt::Key_Up ? -1 : 1);
        return;
    }
    if (event->key() == Qt::Key_Escape && m_isDragging)
    {
        event->accept();
        m_isDragging = false;
        m_isPressed = false;
        setDown(false);
        unsetCursor();
        Q_EMIT dragFinished(m_content.inputId, QCursor::pos(), true);
        return;
    }
    BaseClass::keyPressEvent(event);
}

bool PDFAppToolInputRow::event(QEvent* event)
{
    // Delete and Alt + Up / Down belong to the row (before the shortcuts of the window)
    if (event->type() == QEvent::ShortcutOverride && m_isDraggable)
    {
        const QKeyEvent* keyEvent = static_cast<const QKeyEvent*>(event);
        const Qt::KeyboardModifiers modifiers = keyEvent->modifiers() & ~Qt::KeypadModifier;
        const bool isDelete = (keyEvent->key() == Qt::Key_Delete || keyEvent->key() == Qt::Key_Backspace) && modifiers == Qt::NoModifier;
        const bool isMove = modifiers == Qt::AltModifier && (keyEvent->key() == Qt::Key_Up || keyEvent->key() == Qt::Key_Down);
        if (isDelete || isMove)
        {
            event->accept();
            return true;
        }
    }
    return BaseClass::event(event);
}

void PDFAppToolInputRow::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        m_isPressed = true;
        m_isDragging = false;
        m_pressPosition = event->position().toPoint();
    }
    BaseClass::mousePressEvent(event);
}

void PDFAppToolInputRow::mouseMoveEvent(QMouseEvent* event)
{
    if (m_isPressed && m_isDraggable && (event->buttons() & Qt::LeftButton))
    {
        if (!m_isDragging && (event->position().toPoint() - m_pressPosition).manhattanLength() >= QApplication::startDragDistance())
        {
            // The press becomes a drag: the row is no more pressed (no click at the release)
            m_isDragging = true;
            setDown(false);
            setCursor(Qt::ClosedHandCursor);
        }
        if (m_isDragging)
        {
            event->accept();
            Q_EMIT dragMoved(m_content.inputId, event->globalPosition().toPoint());
            return;
        }
    }
    BaseClass::mouseMoveEvent(event);
}

void PDFAppToolInputRow::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_isDragging)
    {
        event->accept();
        m_isDragging = false;
        m_isPressed = false;
        unsetCursor();
        Q_EMIT dragFinished(m_content.inputId, event->globalPosition().toPoint(), false);
        return;
    }
    m_isPressed = false;
    BaseClass::mouseReleaseEvent(event);
}

QSize PDFAppToolInputRow::getContentSizeHint() const
{
    return QSize(getTextLeft() + PADDING + 40, getHeight(m_content));
}

void PDFAppToolInputRow::paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const
{
    using Color = PDFThemeColors::Color;
    using Helper = PDFAppToolInputRowHelper;

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);

    // file-A 378:1199: the current row bg/accent-subtle, radius 8
    const PDFStylePainter::Radii radii(RADIUS);
    if (m_isCurrent)
    {
        PDFStylePainter::fill(painter, QRectF(rect), radii, Color::BgAccentSubtle);
    }
    if (state.isEnabled() && state.isPressed())
    {
        PDFStylePainter::fill(painter, QRectF(rect), radii, Color::BgPressed);
    }
    else if (state.isEnabled() && state.isHovered() && !m_isCurrent)
    {
        PDFStylePainter::fill(painter, QRectF(rect), radii, Color::BgHover);
    }
    if (state.hasFocusRing())
    {
        PDFComponentPainter::paintFocusRingInside(painter, QRectF(rect), radii);
    }

    // The tile of the hue with the letter (icon-tile 378:1197)
    const Qt::LayoutDirection direction = layoutDirection();
    const QRect tileRect = QStyle::visualRect(direction, rect, QRect(rect.left() + PADDING, rect.top() + (rect.height() - TILE) / 2, TILE, TILE));
    const PDFAccentHue hue = state.isEnabled() ? m_content.hue : PDFAccentHue::Neutral;
    PDFStylePainter::fill(painter, QRectF(tileRect), radii, PDFAccentHueColors::getColor(hue, PDFAccentHueColors::Role::Background));
    PDFStylePainter::text(painter, QRectF(tileRect), Qt::AlignCenter, m_content.letter, Helper::getLetterFont(),
                          PDFStylePainter::color(state.isEnabled() ? PDFAccentHueColors::getColor(hue, PDFAccentHueColors::Role::Icon) : Color::TextDisabled));

    // The name, then the two lines
    const int left = rect.left() + getTextLeft();
    const int width = getTextWidth();
    const int nameTop = rect.top() + PADDING + (m_content.tag.isEmpty() ? 0 : (TAG_HEIGHT - NAME_HEIGHT) / 2);
    const int nameWidth = m_tag ? qMax(0, m_tag->geometry().width() > 0 ? width - m_tag->width() - Helper::TAG_GAP : width) : width;
    PDFComponentPainter::drawElidedText(painter, state, QRectF(QStyle::visualRect(direction, rect, QRect(left, nameTop, nameWidth, NAME_HEIGHT))), m_content.name, Helper::getNameFont(),
                                        !state.isEnabled() ? Color::TextDisabled : (m_content.isNameMuted ? Color::TextTertiary : Color::TextPrimary), PDFComponentPainter::TextKind::FileName);

    int top = rect.top() + PADDING + (m_content.tag.isEmpty() ? NAME_HEIGHT : TAG_HEIGHT) + LINE_GAP;
    Color firstColor = m_content.isFirstLineProblem ? Color::TextDanger : (m_content.isFirstLineNotice ? Color::TextSecondary : Color::TextTertiary);
    if (!state.isEnabled())
    {
        firstColor = Color::TextDisabled;
    }
    PDFComponentPainter::drawElidedText(painter, state, QRectF(QStyle::visualRect(direction, rect, QRect(left, top, width, LINE_HEIGHT))), m_content.firstLine, Helper::getLineFont(), firstColor);
    top += LINE_HEIGHT + LINE_GAP;
    PDFComponentPainter::drawElidedText(painter, state, QRectF(QStyle::visualRect(direction, rect, QRect(left, top, width, LINE_HEIGHT))), m_content.secondLine, Helper::getLineFont(),
                                        state.isEnabled() ? Color::TextTertiary : Color::TextDisabled);
    top += LINE_HEIGHT + LINE_GAP;

    // The reading of the file (1390:34124)
    if (m_content.linkText.isEmpty() && m_content.progress >= 0.0)
    {
        PDFComponentPainter::paintProgressBar(painter, QRectF(QStyle::visualRect(direction, rect, QRect(left, top, width, PROGRESS_HEIGHT))), m_content.progress, state);
    }
}

// ----------------------------------------------------- PDFAppToolInputFilesPanel

PDFAppToolInputFilesPanel::PDFAppToolInputFilesPanel(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("toolInputFiles"));

    // sidebar-left 378:1076: the header, the rows and the hint 10 apart
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(SIDE_MARGIN, 0, SIDE_MARGIN, 0);
    layout->setSpacing(ROW_GAP);

    QWidget* header = new QWidget(this);
    QVBoxLayout* headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(1);
    m_title = PDFAppPageWidgets::createLabel(QString(), QFont::DemiBold, 14, PDFThemeColors::TextPrimary, header);
    m_title->setObjectName(QStringLiteral("toolInputFilesTitle"));
    m_subtitle = PDFAppPageWidgets::createLabel(QString(), QFont::Normal, 11, PDFThemeColors::TextTertiary, header);
    m_subtitle->setObjectName(QStringLiteral("toolInputFilesSubtitle"));
    headerLayout->addWidget(m_title);
    headerLayout->addWidget(m_subtitle);
    layout->addWidget(header);

    QWidget* rows = new QWidget(this);
    rows->setObjectName(QStringLiteral("toolInputFilesRows"));
    m_rowsWidget = rows;
    m_rowsLayout = new QVBoxLayout(rows);
    m_rowsLayout->setContentsMargins(0, 0, 0, 0);
    m_rowsLayout->setSpacing(ROW_GAP);
    layout->addWidget(rows);

    m_hint = PDFAppPageWidgets::createText(QString(), PDFThemeFonts::getFont(QFont::Normal, 11), 13, PDFThemeColors::TextMuted, this);
    m_hint->setObjectName(QStringLiteral("toolInputFilesHint"));
    layout->addWidget(m_hint);
    layout->addStretch(1);
}

PDFAppToolInputFilesPanel::~PDFAppToolInputFilesPanel() = default;

void PDFAppToolInputFilesPanel::setHeader(const QString& title, const QString& subtitle)
{
    m_title->setText(title);
    m_subtitle->setText(subtitle);
}

void PDFAppToolInputFilesPanel::setHint(const QString& hint)
{
    m_hint->setText(hint);
}

PDFAppToolInputRow* PDFAppToolInputFilesPanel::createRow(const PDFAppToolInputRow::Content& content)
{
    PDFAppToolInputRow* row = new PDFAppToolInputRow(m_rowsLayout->parentWidget());
    row->setContent(content);
    row->setDraggable(m_isEditable);
    connect(row, &PDFAppToolInputRow::clicked, this, &PDFAppToolInputFilesPanel::onRowClicked);
    connect(row, &PDFAppToolInputRow::linkActivated, this, &PDFAppToolInputFilesPanel::linkActivated);
    connect(row, &PDFAppToolInputRow::removeRequested, this, &PDFAppToolInputFilesPanel::removeRequested);
    connect(row, &PDFAppToolInputRow::moveRequested, this, &PDFAppToolInputFilesPanel::onRowMoveRequested);
    connect(row, &PDFAppToolInputRow::dragMoved, this, &PDFAppToolInputFilesPanel::onRowDragMoved);
    connect(row, &PDFAppToolInputRow::dragFinished, this, &PDFAppToolInputFilesPanel::onRowDragFinished);
    return row;
}

void PDFAppToolInputFilesPanel::setRows(const std::vector<PDFAppToolInputRow::Content>& rows)
{
    // The same inputs in the same order: each row is updated in its place (its focus stays)
    bool isSameOrder = rows.size() == m_rows.size();
    for (size_t i = 0; isSameOrder && i < rows.size(); ++i)
    {
        isSameOrder = m_rows[i] && m_rows[i]->getInputId() == rows[i].inputId;
    }

    if (!isSameOrder)
    {
        // The rows are made again; a row, which stays, keeps its widget (and its focus), the
        // others are deleted later (one of them may be the sender of the change)
        std::vector<QPointer<PDFAppToolInputRow>> oldRows = std::move(m_rows);
        m_rows.clear();
        for (const PDFAppToolInputRow::Content& content : rows)
        {
            PDFAppToolInputRow* row = nullptr;
            for (QPointer<PDFAppToolInputRow>& oldRow : oldRows)
            {
                if (oldRow && oldRow->getInputId() == content.inputId)
                {
                    row = oldRow.data();
                    oldRow = nullptr;
                    break;
                }
            }
            if (!row)
            {
                row = createRow(content);
            }
            m_rows.push_back(row);
        }
        for (QPointer<PDFAppToolInputRow>& oldRow : oldRows)
        {
            if (oldRow)
            {
                oldRow->hide();
                m_rowsLayout->removeWidget(oldRow);
                oldRow->deleteLater();
            }
        }

        // The order of the layout and of the focus chain is the order of the rows
        QWidget* previous = nullptr;
        for (size_t i = 0; i < m_rows.size(); ++i)
        {
            m_rowsLayout->removeWidget(m_rows[i]);
            m_rowsLayout->insertWidget(int(i), m_rows[i]);
            m_rows[i]->show();
            if (previous)
            {
                QWidget::setTabOrder(previous, m_rows[i]);
            }
            previous = m_rows[i];
        }
    }

    for (size_t i = 0; i < rows.size(); ++i)
    {
        m_rows[i]->setContent(rows[i]);
        m_rows[i]->setCurrent(rows[i].inputId == m_currentInput);
    }

    // 378:1657: without rows the hint follows the header (one gap)
    m_rowsWidget->setVisible(!rows.empty());
}

void PDFAppToolInputFilesPanel::setCurrentInput(const QString& inputId)
{
    m_currentInput = inputId;
    for (const QPointer<PDFAppToolInputRow>& row : m_rows)
    {
        if (row)
        {
            row->setCurrent(row->getInputId() == inputId);
        }
    }
}

void PDFAppToolInputFilesPanel::setEditable(bool isEditable)
{
    m_isEditable = isEditable;
    for (const QPointer<PDFAppToolInputRow>& row : m_rows)
    {
        if (row)
        {
            row->setDraggable(isEditable);
        }
    }
    if (!isEditable && m_dropIndex >= 0)
    {
        m_dropIndex = -1;
        update();
    }
}

PDFAppToolInputRow* PDFAppToolInputFilesPanel::findRow(const QString& inputId) const
{
    for (const QPointer<PDFAppToolInputRow>& row : m_rows)
    {
        if (row && row->getInputId() == inputId)
        {
            return row.data();
        }
    }
    return nullptr;
}

std::vector<PDFAppToolInputRow*> PDFAppToolInputFilesPanel::getRows() const
{
    std::vector<PDFAppToolInputRow*> rows;
    for (const QPointer<PDFAppToolInputRow>& row : m_rows)
    {
        if (row)
        {
            rows.push_back(row.data());
        }
    }
    return rows;
}

int PDFAppToolInputFilesPanel::getDropIndex(const QPoint& globalPosition) const
{
    // The upper half of a row puts the dragged row in front of it
    const QPoint position = mapFromGlobal(globalPosition);
    for (size_t i = 0; i < m_rows.size(); ++i)
    {
        if (m_rows[i] && position.y() < m_rows[i]->mapTo(this, QPoint(0, 0)).y() + m_rows[i]->height() / 2)
        {
            return int(i);
        }
    }
    return int(m_rows.size());
}

void PDFAppToolInputFilesPanel::onRowClicked()
{
    PDFAppToolInputRow* row = qobject_cast<PDFAppToolInputRow*>(sender());
    if (row)
    {
        Q_EMIT rowClicked(row->getInputId());
    }
}

void PDFAppToolInputFilesPanel::onRowMoveRequested(const QString& inputId, int delta)
{
    int index = -1;
    for (size_t i = 0; i < m_rows.size(); ++i)
    {
        if (m_rows[i] && m_rows[i]->getInputId() == inputId)
        {
            index = int(i);
        }
    }

    // Up: in front of the previous row; Down: in front of the row after the next one
    const int target = delta < 0 ? index - 1 : index + 2;
    if (index >= 0 && target >= 0 && target <= int(m_rows.size()))
    {
        Q_EMIT moveRequested(inputId, target);
    }
}

void PDFAppToolInputFilesPanel::onRowDragMoved(const QString& inputId, const QPoint& globalPosition)
{
    Q_UNUSED(inputId);
    const int dropIndex = m_isEditable ? getDropIndex(globalPosition) : -1;
    if (dropIndex != m_dropIndex)
    {
        m_dropIndex = dropIndex;
        update();
    }
}

void PDFAppToolInputFilesPanel::onRowDragFinished(const QString& inputId, const QPoint& globalPosition, bool isCanceled)
{
    m_dropIndex = -1;
    update();
    if (!isCanceled && m_isEditable && rect().contains(mapFromGlobal(globalPosition)))
    {
        Q_EMIT moveRequested(inputId, getDropIndex(globalPosition));
    }
}

void PDFAppToolInputFilesPanel::paintEvent(QPaintEvent* event)
{
    BaseClass::paintEvent(event);
    if (m_dropIndex < 0 || m_rows.empty())
    {
        return;
    }

    // The line of the drop in the gap in front of the row (after the last one at its end)
    int y = 0;
    if (size_t(m_dropIndex) < m_rows.size() && m_rows[size_t(m_dropIndex)])
    {
        y = m_rows[size_t(m_dropIndex)]->mapTo(this, QPoint(0, 0)).y() - ROW_GAP / 2;
    }
    else if (m_rows.back())
    {
        y = m_rows.back()->mapTo(this, QPoint(0, m_rows.back()->height())).y() + ROW_GAP / 2;
    }

    QPainter painter(this);
    PDFStylePainter::fillRect(&painter, QRectF(SIDE_MARGIN, y - PDFAppToolInputRowHelper::DROP_LINE / 2, width() - 2 * SIDE_MARGIN, PDFAppToolInputRowHelper::DROP_LINE), PDFThemeColors::BgAccent);
}

}   // namespace pdf

#include "moc_pdfapptoolinputrow.cpp"
