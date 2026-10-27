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

#include "pdfappmcppreview.h"
#include "pdfappmcpclientnames.h"
#include "pdfappmcpconfirmdialog.h"
#include "pdfappdocument.h"

#include "pdfcomponentassistants.h"
#include "pdfcomponentbadges.h"
#include "pdfcomponentbuttons.h"
#include "pdffluentwidgets.h"
#include "pdfpainterutils.h"
#include "pdfstylepainter.h"
#include "pdfthemeiconengine.h"
#include "pdfwidgetstyleproperties.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QTimer>

namespace pdf
{

/// Helpers of the ghosts and the bars
class PDFAppMcpPreviewHelper
{
public:
    PDFAppMcpPreviewHelper() = delete;

    /// Opacity of the fill of a ghost (955:152306: the annotation color at 30 %)
    static constexpr qreal FILL_OPACITY = 0.3;

    /// Width of the dashed border of a ghost
    static constexpr qreal BORDER_WIDTH = 1.5;

    static QPainterPath createPath(const PDFAppMcpGhost& ghost, const QTransform& matrix, bool* isClosed)
    {
        QPainterPath path;
        *isClosed = true;
        switch (ghost.shape)
        {
            case PDFAppMcpGhost::Shape::Quads:
            {
                // QuadPoints order: top-left, top-right, bottom-left, bottom-right
                for (qsizetype i = 0; i + 3 < ghost.points.size(); i += 4)
                {
                    QPolygonF polygon;
                    polygon << ghost.points[i] << ghost.points[i + 1] << ghost.points[i + 3] << ghost.points[i + 2];
                    path.addPolygon(matrix.map(polygon));
                    path.closeSubpath();
                }
                break;
            }

            case PDFAppMcpGhost::Shape::Rectangle:
            case PDFAppMcpGhost::Shape::Ellipse:
            {
                if (ghost.points.size() < 2)
                {
                    break;
                }

                const QRectF rect = QRectF(ghost.points[0], ghost.points[1]).normalized();
                QPainterPath pagePath;
                if (ghost.shape == PDFAppMcpGhost::Shape::Ellipse)
                {
                    pagePath.addEllipse(rect);
                }
                else
                {
                    pagePath.addRect(rect);
                }
                path = matrix.map(pagePath);
                break;
            }

            case PDFAppMcpGhost::Shape::Polygon:
            case PDFAppMcpGhost::Shape::Polyline:
            {
                *isClosed = ghost.shape == PDFAppMcpGhost::Shape::Polygon;
                QPolygonF polygon = matrix.map(ghost.points);
                if (*isClosed)
                {
                    path.addPolygon(polygon);
                    path.closeSubpath();
                }
                else if (!polygon.isEmpty())
                {
                    path.moveTo(polygon.front());
                    for (qsizetype i = 1; i < polygon.size(); ++i)
                    {
                        path.lineTo(polygon[i]);
                    }
                }
                break;
            }

            case PDFAppMcpGhost::Shape::Ink:
            {
                *isClosed = false;
                for (const QPolygonF& stroke : ghost.strokes)
                {
                    const QPolygonF polygon = matrix.map(stroke);
                    if (polygon.isEmpty())
                    {
                        continue;
                    }
                    path.moveTo(polygon.front());
                    for (qsizetype i = 1; i < polygon.size(); ++i)
                    {
                        path.lineTo(polygon[i]);
                    }
                }
                break;
            }

            case PDFAppMcpGhost::Shape::Note:
                break;
        }
        return path;
    }

    static QPen createBorderPen()
    {
        QPen pen(PDFStylePainter::color(PDFThemeColors::AccentAmberBorderStrong), BORDER_WIDTH);
        pen.setStyle(Qt::DashLine);
        pen.setCapStyle(Qt::FlatCap);
        pen.setJoinStyle(Qt::MiterJoin);
        return pen;
    }

    static QColor getFillColor(const PDFAppMcpGhost& ghost)
    {
        QColor color = ghost.color.isValid() ? ghost.color : PDFStylePainter::color(PDFThemeColors::AnnotationYellow);
        color.setAlphaF(float(FILL_OPACITY));
        return color;
    }
};

void PDFAppMcpGhostOverlay::drawPage(QPainter* painter,
                                     PDFInteger pageIndex,
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

    drawGhosts(painter, pageIndex, pagePointToDevicePointMatrix);
}

void PDFAppMcpGhostOverlay::drawGhosts(QPainter* painter, PDFInteger pageIndex, const QTransform& matrix) const
{
    using Helper = PDFAppMcpPreviewHelper;

    if (!painter)
    {
        return;
    }

    for (const PDFAppMcpGhost& ghost : m_ghosts)
    {
        if (ghost.pageIndex != pageIndex)
        {
            continue;
        }

        PDFPainterStateGuard guard(painter);
        painter->setRenderHint(QPainter::Antialiasing, true);

        if (ghost.shape == PDFAppMcpGhost::Shape::Note)
        {
            // The note marker 20 px at the top-left point of the icon, radius 4,
            // with the icon message-square 14 (955:152309)
            if (ghost.points.isEmpty())
            {
                continue;
            }

            const QPointF topLeft = matrix.map(ghost.points.front());
            const QRectF marker(topLeft, QSizeF(NOTE_EXTENT, NOTE_EXTENT));
            painter->setCompositionMode(QPainter::CompositionMode_Multiply);
            PDFStylePainter::fill(painter, marker, PDFStylePainter::Radii(4.0), Helper::getFillColor(ghost));
            painter->setCompositionMode(QPainter::CompositionMode_SourceOver);
            painter->setPen(Helper::createBorderPen());
            painter->setBrush(Qt::NoBrush);
            painter->drawRoundedRect(marker.adjusted(0.75, 0.75, -0.75, -0.75), 4.0, 4.0);
            const QRectF iconRect = PDFStylePainter::centered(marker, QSizeF(14.0, 14.0));
            const QPixmap icon = PDFThemeIcons::getPixmap(QStringLiteral("message-square"), iconRect.size(), painter->device() ? painter->device()->devicePixelRatioF() : 1.0,
                                                          PDFStylePainter::color(PDFThemeColors::IconDefault));
            painter->drawPixmap(iconRect.topLeft(), icon);
            continue;
        }

        bool isClosed = true;
        const QPainterPath path = Helper::createPath(ghost, matrix, &isClosed);
        if (path.isEmpty())
        {
            continue;
        }

        if (isClosed)
        {
            painter->setCompositionMode(QPainter::CompositionMode_Multiply);
            painter->fillPath(path, Helper::getFillColor(ghost));
            painter->setCompositionMode(QPainter::CompositionMode_SourceOver);
        }
        painter->setPen(Helper::createBorderPen());
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(path);
    }
}

PDFAppMcpFloatingBar::PDFAppMcpFloatingBar(const QString& clientNameRaw, QWidget* parent) :
    BaseClass(Size::Normal, parent)
{
    setCardHeight(CARD_HEIGHT);
    if (QHBoxLayout* layout = getContentLayout())
    {
        layout->setSpacing(GAP);
    }
    applyMargins();

    // The monogram 24 of the assistant (Bold 10) and the message 10 px after it
    QWidget* message = new QWidget(this);
    message->setObjectName(QStringLiteral("message"));
    QHBoxLayout* messageLayout = new QHBoxLayout(message);
    messageLayout->setContentsMargins(0, 0, 0, 0);
    messageLayout->setSpacing(10);
    const PDFAppMcpClientInfo client = PDFAppMcpClientNames::describe(clientNameRaw);
    // The monogram 24 (955:152313: accent/<hue>/bg without a border, Bold 10; the
    // mcp icon in teal of an unknown client)
    PDFComponentPainter::AssistantAvatar avatarData;
    avatarData.monogram = client.monogram;
    avatarData.hue = client.monogram.isEmpty() ? PDFAccentHue::Teal : client.hue;
    avatarData.extent = 24;
    avatarData.fontPixelSize = 10;
    PDFAssistantAvatar* avatar = new PDFAssistantAvatar(avatarData, message);
    avatar->setObjectName(QStringLiteral("monogram"));
    messageLayout->addWidget(avatar, 0, Qt::AlignVCenter);
    m_messageLabel = new PDFLabel(QString(), PDFThemeFonts::Role::BodySemiBold, PDFThemeColors::TextPrimary, message);
    m_messageLabel->setTextFormat(Qt::PlainText);
    m_messageLabel->setObjectName(QStringLiteral("messageText"));
    messageLayout->addWidget(m_messageLabel, 1, Qt::AlignVCenter);
    addWidget(message);

    // The message follows the width of the canvas
    m_watchedParent = parent;
    if (parent)
    {
        parent->installEventFilter(this);
    }
}

PDFAppMcpFloatingBar::~PDFAppMcpFloatingBar()
{
    if (m_watchedParent)
    {
        m_watchedParent->removeEventFilter(this);
    }
}

void PDFAppMcpFloatingBar::setMessage(const QString& text)
{
    m_message = text;
    setAccessibleName(text);
    fitMessage();
}

QString PDFAppMcpFloatingBar::getMessage() const
{
    return m_message;
}

void PDFAppMcpFloatingBar::fitMessage()
{
    if (!m_messageLabel || m_isFitting)
    {
        return;
    }

    m_isFitting = true;
    m_messageLabel->setText(m_message);
    m_messageLabel->setToolTip(QString());
    invalidateLayouts();

    QWidget* parent = parentWidget();
    const QMargins shadow = getShadowMargins();
    if (parent && parent->width() > 0)
    {
        // The card stays inside of the parent with its margin
        // A few steps: the label and the layouts measure the elided text again
        const int available = parent->width() - 2 * PARENT_MARGIN;
        int textWidth = m_messageLabel->fontMetrics().horizontalAdvance(m_message);
        for (int step = 0; step < 4; ++step)
        {
            const int cardWidth = getContentLayout() ? getContentLayout()->sizeHint().width() - shadow.left() - shadow.right() : 0;
            if (cardWidth <= available || textWidth <= 40)
            {
                break;
            }
            textWidth = qMax(40, textWidth - (cardWidth - available));
            m_messageLabel->setText(m_messageLabel->fontMetrics().elidedText(m_message, Qt::ElideRight, textWidth));
            m_messageLabel->setToolTip(m_message);
            invalidateLayouts();
        }
    }
    updateGeometry();
    updatePlacement();
    m_isFitting = false;
}

void PDFAppMcpFloatingBar::invalidateLayouts()
{
    // The size hints of the label and of the layouts are measured again now
    m_messageLabel->updateGeometry();
    if (QWidget* message = m_messageLabel->parentWidget(); message && message->layout())
    {
        message->layout()->invalidate();
        message->updateGeometry();
    }
    if (QHBoxLayout* layout = getContentLayout())
    {
        layout->invalidate();
    }
}

bool PDFAppMcpFloatingBar::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_watchedParent && event->type() == QEvent::Resize)
    {
        fitMessage();
    }
    return BaseClass::eventFilter(watched, event);
}

void PDFAppMcpFloatingBar::applyMargins()
{
    QHBoxLayout* layout = getContentLayout();
    if (!layout)
    {
        return;
    }

    // Border 1 + the padding of the design 12 / 10 / 5 (Figma map 5.10)
    QMargins margins = getShadowMargins() + QMargins(13, 6, 11, 6);
    if (isRightToLeft())
    {
        margins = QMargins(margins.right(), margins.top(), margins.left(), margins.bottom());
    }
    layout->setContentsMargins(margins);
}

bool PDFAppMcpFloatingBar::event(QEvent* event)
{
    const bool result = BaseClass::event(event);
    if (event->type() == QEvent::LayoutDirectionChange)
    {
        // The base gives the margins of its size back
        applyMargins();
        updatePlacement();
    }
    else if (event->type() == QEvent::Polish || event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange)
    {
        // The fonts of the style are known now
        fitMessage();
    }
    return result;
}

QPushButton* PDFAppMcpFloatingBar::addButton(const QString& objectName, bool isPrimary)
{
    QPushButton* button = new QPushButton(this);
    button->setObjectName(objectName);
    button->setAutoDefault(false);
    if (isPrimary)
    {
        PDFWidgetStyleProperties::set(button, PDFWidgetStyleProperties::PRIMARY);
    }
    addWidget(button);
    return button;
}

PDFAppMcpPreviewBar::PDFAppMcpPreviewBar(quint64 confirmationId, const QString& clientName, const QString& clientNameRaw, const QString& summary, QWidget* parent) :
    BaseClass(clientNameRaw, parent),
    m_confirmationId(confirmationId),
    m_clientName(PDFAppMcpConfirmDialog::sanitize(clientName, 64)),
    m_summary(PDFAppMcpConfirmDialog::sanitize(summary))
{
    setObjectName(QStringLiteral("mcpPreviewBar"));
    addDivider();
    m_denyButton = addButton(QStringLiteral("denyButton"), false);
    m_allowButton = addButton(QStringLiteral("allowButton"), true);
    connect(m_denyButton, &QPushButton::clicked, this, &PDFAppMcpPreviewBar::onDenyClicked);
    connect(m_allowButton, &QPushButton::clicked, this, &PDFAppMcpPreviewBar::onAllowClicked);
    updateTexts();
}

PDFAppMcpPreviewBar::~PDFAppMcpPreviewBar() = default;

QPushButton* PDFAppMcpPreviewBar::getAllowButton() const
{
    return m_allowButton.data();
}

QPushButton* PDFAppMcpPreviewBar::getDenyButton() const
{
    return m_denyButton.data();
}

void PDFAppMcpPreviewBar::updateTexts()
{
    // The buttons first - the message is elided into the rest of the width
    if (m_denyButton)
    {
        m_denyButton->setText(tr("Deny"));
    }
    if (m_allowButton)
    {
        m_allowButton->setText(tr("Allow"));
    }
    setMessage(m_summary.isEmpty() ? tr("%1 wants to add these").arg(m_clientName) : tr("%1 wants to add these · %2").arg(m_clientName, m_summary));
}

void PDFAppMcpPreviewBar::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
    {
        updateTexts();
    }
}

void PDFAppMcpPreviewBar::onAllowClicked()
{
    Q_EMIT allowed(m_confirmationId);
}

void PDFAppMcpPreviewBar::onDenyClicked()
{
    Q_EMIT denied(m_confirmationId);
}

PDFAppMcpNotificationBar::PDFAppMcpNotificationBar(const PDFAppMcpCommitInfo& info, QWidget* parent) :
    BaseClass(info.clientNameRaw, parent),
    m_info(info)
{
    setObjectName(QStringLiteral("mcpNotificationBar"));
    addDivider();
    m_showChangesButton = addButton(QStringLiteral("showChangesButton"), false);
    m_undoButton = addButton(QStringLiteral("undoButton"), false);
    addDivider();
    m_closeButton = new PDFIconButton(QStringLiteral("x"), PDFIconButton::Size::TabClose, this);
    m_closeButton->setObjectName(QStringLiteral("closeButton"));
    addWidget(m_closeButton);

    // Show Changes only with something to show (new annotations, the outline)
    m_showChangesButton->setVisible(!m_info.annotations.empty() || !m_info.panelId.isEmpty());

    connect(m_showChangesButton, &QPushButton::clicked, this, &PDFAppMcpNotificationBar::onShowChangesClicked);
    connect(m_undoButton, &QPushButton::clicked, this, &PDFAppMcpNotificationBar::onUndoClicked);
    connect(m_closeButton, &QAbstractButton::clicked, this, &PDFAppMcpNotificationBar::onCloseClicked);

    // The next change of the document (also Undo) closes the bar
    if (m_info.document)
    {
        connect(m_info.document, &PDFAppDocument::documentChanged, this, &PDFAppMcpNotificationBar::onDocumentChanged);
    }
    updateTexts();
    updateUndoState();
}

PDFAppMcpNotificationBar::~PDFAppMcpNotificationBar() = default;

QPushButton* PDFAppMcpNotificationBar::getShowChangesButton() const
{
    return m_showChangesButton.data();
}

QPushButton* PDFAppMcpNotificationBar::getUndoButton() const
{
    return m_undoButton.data();
}

PDFIconButton* PDFAppMcpNotificationBar::getCloseButton() const
{
    return m_closeButton.data();
}

bool PDFAppMcpNotificationBar::isChangeOnTop() const
{
    return m_info.document && m_info.document->getVersion() == m_info.version;
}

void PDFAppMcpNotificationBar::updateUndoState()
{
    if (!m_undoButton)
    {
        return;
    }

    const bool isOnTop = isChangeOnTop();
    m_undoButton->setEnabled(isOnTop);
    m_undoButton->setToolTip(isOnTop ? QString() : tr("The document has changed since then - use Undo of the document"));
}

void PDFAppMcpNotificationBar::startAutoHide(int milliseconds)
{
    if (!m_autoHideTimer)
    {
        m_autoHideTimer = new QTimer(this);
        m_autoHideTimer->setSingleShot(true);
        connect(m_autoHideTimer, &QTimer::timeout, this, &PDFAppMcpNotificationBar::onCloseClicked);
    }
    m_autoHideTimer->start(qMax(0, milliseconds));
}

void PDFAppMcpNotificationBar::onDocumentChanged()
{
    updateUndoState();
    if (!isChangeOnTop())
    {
        Q_EMIT closeRequested();
    }
}

void PDFAppMcpNotificationBar::updateTexts()
{
    // The buttons first - the message is elided into the rest of the width
    if (m_showChangesButton)
    {
        m_showChangesButton->setText(tr("Show Changes"));
    }
    if (m_undoButton)
    {
        m_undoButton->setText(tr("Undo"));
    }
    setMessage(PDFAppMcpConfirmDialog::sanitize(m_info.text, 120));
    if (m_closeButton)
    {
        m_closeButton->setToolTip(tr("Close"));
        m_closeButton->setAccessibleName(tr("Close"));
    }
    updateUndoState();
}

void PDFAppMcpNotificationBar::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
    {
        updateTexts();
    }
}

void PDFAppMcpNotificationBar::onShowChangesClicked()
{
    Q_EMIT showChangesRequested();
}

void PDFAppMcpNotificationBar::onUndoClicked()
{
    if (isChangeOnTop())
    {
        Q_EMIT undoRequested();
    }
}

void PDFAppMcpNotificationBar::onCloseClicked()
{
    Q_EMIT closeRequested();
}

}   // namespace pdf
