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

#include "pdftooltip.h"
#include "pdfcomponentdelegates.h"
#include "pdfstylepainter.h"
#include "pdfwidgetstyleimpl.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QFontMetricsF>
#include <QPainter>
#include <QPointer>
#include <QScreen>
#include <QTextLayout>
#include <QTimer>
#include <QToolButton>
#include <QWidget>
#include <QtMath>

#include <limits>

namespace pdf
{

/// Window of the tooltip
class PDFToolTipWindow : public QWidget
{
    Q_OBJECT

public:
    explicit PDFToolTipWindow() :
        QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus)
    {
        setAttribute(Qt::WA_ShowWithoutActivating, true);
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setFocusPolicy(Qt::NoFocus);
        setObjectName(QStringLiteral("pdf4qt_tooltip"));

        m_hideTimer.setSingleShot(true);
        connect(&m_hideTimer, &QTimer::timeout, this, &PDFToolTipWindow::hideTip);
    }

    static constexpr int PADDING_H = 10;
    static constexpr int PADDING_V = 6;
    static constexpr int BORDER = 1;
    static constexpr int SHORTCUT_GAP = 8;
    static constexpr int DESCRIPTION_GAP = 2;
    static constexpr int DESCRIPTION_WIDTH = 260;
    static constexpr int DESCRIPTION_LINE = 15;
    static constexpr int COMPACT_LINE = 13;
    static constexpr int COMPACT_MAX_WIDTH = 480;
    static constexpr int COMPACT_MAX_LINES = 8;

    const QString& getTitle() const { return m_title; }
    PDFToolTip::Style getStyle() const { return m_style; }

    void showTip(const QPoint& globalPosition, const QString& title, const QString& shortcut, const QString& description, QWidget* widget, PDFToolTip::Style style)
    {
        m_style = style;
        m_title = title;
        m_shortcut = shortcut;
        m_description = description;
        m_widget = widget;

        // The window is prepared by the style before its size is known
        ensurePolished();

        const QMargins margins = PDFWidgetStyleImpl::getPopupMargins(this);
        const QSize panelSize = getPanelSize();
        resize(panelSize.grownBy(margins));

        // The panel is shown below the pointer
        QPoint position = globalPosition + QPoint(2, 18);

        const QScreen* screen = QGuiApplication::screenAt(globalPosition);
        if (!screen)
        {
            screen = QGuiApplication::primaryScreen();
        }

        if (screen)
        {
            const QRect available = screen->availableGeometry();
            if (position.x() + panelSize.width() > available.right())
            {
                position.setX(available.right() - panelSize.width());
            }
            if (position.y() + panelSize.height() > available.bottom())
            {
                position.setY(globalPosition.y() - 6 - panelSize.height());
            }
            position.setX(qMax(position.x(), available.left()));
            position.setY(qMax(position.y(), available.top()));
        }

        move(position - QPoint(margins.left(), margins.top()));

        if (!isVisible())
        {
            qApp->installEventFilter(this);
            show();
        }

        update();

        // Longer texts stay longer
        const qsizetype extraCharacters = qBound(qsizetype(0), m_title.size() + m_description.size() - 100, qsizetype((std::numeric_limits<int>::max() - 10000) / 40));
        m_hideTimer.start(10000 + 40 * static_cast<int>(extraCharacters));
    }

    void hideTip()
    {
        m_hideTimer.stop();

        // The window can be hidden by another way (the window system), the
        // filter of the application is removed anyway
        if (qApp)
        {
            qApp->removeEventFilter(this);
        }

        if (isVisible())
        {
            hide();
        }

        m_widget = nullptr;
    }

    virtual bool eventFilter(QObject* watched, QEvent* event) override
    {
        switch (event->type())
        {
            case QEvent::Leave:
                if (!m_widget || watched == m_widget)
                {
                    hideTip();
                }
                break;

            case QEvent::MouseButtonPress:
            case QEvent::MouseButtonRelease:
            case QEvent::MouseButtonDblClick:
            case QEvent::Wheel:
            case QEvent::KeyPress:
            case QEvent::FocusIn:
            case QEvent::FocusOut:
            case QEvent::WindowActivate:
            case QEvent::WindowDeactivate:
            case QEvent::Close:
                if (watched != this)
                {
                    hideTip();
                }
                break;

            default:
                break;
        }

        return false;
    }

protected:
    virtual void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);

        if (m_style == PDFToolTip::Style::Compact)
        {
            paintCompact(&painter);
            return;
        }

        PDFWidgetStyleImpl::drawPopupPanel(&painter, rect(), this, PDFWidgetStyleImpl::PopupKind::ToolTip, 8.0, PDFThemeColors::BorderStrong);

        const QMargins margins = PDFWidgetStyleImpl::getPopupMargins(this);
        const QRectF content = QRectF(rect()).marginsRemoved(QMarginsF(margins)).adjusted(BORDER + PADDING_H, BORDER + PADDING_V, -BORDER - PADDING_H, -BORDER - PADDING_V);
        const bool isRightToLeft = layoutDirection() == Qt::RightToLeft;

        const QFont titleFont = PDFThemeFonts::getFont(PDFThemeFonts::Role::SmallMedium);
        const QFont shortcutFont = PDFThemeFonts::getFont(PDFThemeFonts::Role::CaptionRegular);
        const qreal titleHeight = 15.0;
        const qreal titleWidth = PDFStylePainter::textWidth(m_title, titleFont);

        QRectF titleRect(content.left(), content.top(), titleWidth, titleHeight);
        QRectF shortcutRect(titleRect.right() + SHORTCUT_GAP, content.top(), PDFStylePainter::textWidth(m_shortcut, shortcutFont), titleHeight);

        if (isRightToLeft)
        {
            titleRect = PDFWidgetStyleImpl::visualRect(true, content, titleRect);
            shortcutRect = PDFWidgetStyleImpl::visualRect(true, content, shortcutRect);
        }

        PDFStylePainter::text(&painter, titleRect, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine, m_title, titleFont, PDFStylePainter::color(PDFThemeColors::TextPrimary));

        if (!m_shortcut.isEmpty())
        {
            PDFStylePainter::text(&painter, shortcutRect, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine, m_shortcut, shortcutFont, PDFStylePainter::color(PDFThemeColors::TextTertiary));
        }

        if (!m_description.isEmpty())
        {
            // Description: Regular 11 with lines of 15 px
            painter.setPen(PDFStylePainter::color(PDFThemeColors::TextMuted));
            const QPointF origin(content.left(), content.top() + titleHeight + DESCRIPTION_GAP);
            layoutDescription(content.width(), &painter, origin);
        }
    }

private:
    /// Returns the lines of the description of the compact tooltip (each line of
    /// the text is a line of the tooltip, at most COMPACT_MAX_LINES lines)
    QStringList getCompactLines() const
    {
        QStringList lines = m_description.isEmpty() ? QStringList() : m_description.split(QLatin1Char('\n'));
        if (lines.size() > COMPACT_MAX_LINES)
        {
            lines = lines.mid(0, COMPACT_MAX_LINES);
        }
        return lines;
    }

    /// Draws the compact tooltip (link of a document 1028:22472)
    void paintCompact(QPainter* painter)
    {
        PDFWidgetStyleImpl::drawPopupPanel(painter, rect(), this, PDFWidgetStyleImpl::PopupKind::ToolTip, 6.0, PDFThemeColors::BorderDefault);

        const QMargins margins = PDFWidgetStyleImpl::getPopupMargins(this);
        const QRectF content = QRectF(rect()).marginsRemoved(QMarginsF(margins)).adjusted(BORDER + PADDING_H, BORDER + PADDING_V, -BORDER - PADDING_H, -BORDER - PADDING_V);
        const bool isRightToLeft = layoutDirection() == Qt::RightToLeft;
        const int alignment = Qt::AlignVCenter | Qt::TextSingleLine | (isRightToLeft ? Qt::AlignRight : Qt::AlignLeft);
        const QFont font = PDFThemeFonts::getFont(PDFThemeFonts::Role::CaptionRegular);
        const QFontMetricsF metrics(font);

        // The name (an address) is elided in the middle, the shortcut follows it
        qreal titleWidth = content.width();
        if (!m_shortcut.isEmpty())
        {
            titleWidth = qMax(0.0, titleWidth - SHORTCUT_GAP - PDFStylePainter::textWidth(m_shortcut, font));
        }
        const QString title = metrics.elidedText(m_title, Qt::ElideMiddle, titleWidth);
        QRectF titleRect(content.left(), content.top(), titleWidth, COMPACT_LINE);
        QRectF shortcutRect(content.left() + qMin(titleWidth, PDFStylePainter::textWidth(title, font)) + SHORTCUT_GAP, content.top(), PDFStylePainter::textWidth(m_shortcut, font), COMPACT_LINE);
        if (isRightToLeft)
        {
            titleRect = PDFWidgetStyleImpl::visualRect(true, content, titleRect);
            shortcutRect = PDFWidgetStyleImpl::visualRect(true, content, shortcutRect);
        }
        PDFStylePainter::text(painter, titleRect, alignment, title, font, PDFStylePainter::color(PDFThemeColors::TextPrimary));
        if (!m_shortcut.isEmpty())
        {
            PDFStylePainter::text(painter, shortcutRect, alignment, m_shortcut, font, PDFStylePainter::color(PDFThemeColors::TextTertiary));
        }

        qreal y = content.top() + COMPACT_LINE + DESCRIPTION_GAP;
        for (const QString& line : getCompactLines())
        {
            const QString shownLine = metrics.elidedText(line, Qt::ElideRight, content.width());
            PDFStylePainter::text(painter, QRectF(content.left(), y, content.width(), COMPACT_LINE), alignment, shownLine, font, PDFStylePainter::color(PDFThemeColors::TextMuted));
            y += COMPACT_LINE;
        }
    }

    QSize getCompactPanelSize() const
    {
        const QFont font = PDFThemeFonts::getFont(PDFThemeFonts::Role::CaptionRegular);

        qreal width = PDFStylePainter::textWidth(m_title, font);
        if (!m_shortcut.isEmpty())
        {
            width += SHORTCUT_GAP + PDFStylePainter::textWidth(m_shortcut, font);
        }

        const QStringList lines = getCompactLines();
        for (const QString& line : lines)
        {
            width = qMax(width, PDFStylePainter::textWidth(line, font));
        }
        width = qMin<qreal>(qCeil(width), COMPACT_MAX_WIDTH);

        int height = COMPACT_LINE;
        if (!lines.isEmpty())
        {
            height += DESCRIPTION_GAP + static_cast<int>(lines.size()) * COMPACT_LINE;
        }

        return QSize(qCeil(width) + 2 * (BORDER + PADDING_H), height + 2 * (BORDER + PADDING_V));
    }

    /// Lays the description out into lines of the fixed height
    /// \param width Width of the lines
    /// \param painter Painter, which draws the lines, nullptr, if the lines are only measured
    /// \param origin Position of the first line
    /// \returns Height of the description
    qreal layoutDescription(qreal width, QPainter* painter, const QPointF& origin) const
    {
        const QFont font = PDFThemeFonts::getFont(PDFThemeFonts::Role::CaptionRegular);
        const QFontMetricsF metrics(font);
        const qreal lineOffset = (DESCRIPTION_LINE - metrics.height()) / 2.0;

        QTextOption textOption;
        textOption.setWrapMode(QTextOption::WordWrap);
        textOption.setTextDirection(layoutDirection());

        qreal y = 0.0;
        const QStringList paragraphs = m_description.split(QLatin1Char('\n'));
        for (const QString& paragraph : paragraphs)
        {
            QTextLayout layout(paragraph, font);
            layout.setTextOption(textOption);
            layout.beginLayout();

            while (true)
            {
                QTextLine line = layout.createLine();
                if (!line.isValid())
                {
                    break;
                }

                line.setLineWidth(width);
                line.setPosition(QPointF(0.0, 0.0));
            }
            layout.endLayout();

            for (int i = 0; i < layout.lineCount(); ++i)
            {
                if (painter)
                {
                    layout.lineAt(i).draw(painter, origin + QPointF(0.0, y + lineOffset));
                }
                y += DESCRIPTION_LINE;
            }

            if (layout.lineCount() == 0)
            {
                y += DESCRIPTION_LINE;
            }
        }

        return y;
    }

    QSize getPanelSize() const
    {
        if (m_style == PDFToolTip::Style::Compact)
        {
            return getCompactPanelSize();
        }

        const QFont titleFont = PDFThemeFonts::getFont(PDFThemeFonts::Role::SmallMedium);
        const QFont shortcutFont = PDFThemeFonts::getFont(PDFThemeFonts::Role::CaptionRegular);

        qreal width = qCeil(PDFStylePainter::textWidth(m_title, titleFont));
        if (!m_shortcut.isEmpty())
        {
            width += SHORTCUT_GAP + qCeil(PDFStylePainter::textWidth(m_shortcut, shortcutFont));
        }

        qreal height = 15.0;
        if (!m_description.isEmpty())
        {
            width = qMax<qreal>(width, DESCRIPTION_WIDTH);
            height += DESCRIPTION_GAP + layoutDescription(width, nullptr, QPointF());
        }

        return QSize(qCeil(width) + 2 * (BORDER + PADDING_H), qCeil(height) + 2 * (BORDER + PADDING_V));
    }

    PDFToolTip::Style m_style = PDFToolTip::Style::Default;
    QString m_title;
    QString m_shortcut;
    QString m_description;
    QPointer<QWidget> m_widget;
    QTimer m_hideTimer;
};

class PDFToolTipHelper
{
public:
    PDFToolTipHelper() = delete;

    static QPointer<PDFToolTipWindow> s_window;

    static PDFToolTipWindow* getWindow()
    {
        if (!s_window)
        {
            s_window = new PDFToolTipWindow();

            // The window must not outlive the application
            QObject::connect(qApp, &QCoreApplication::aboutToQuit, s_window.data(), &QObject::deleteLater);
        }

        return s_window;
    }
};

QPointer<PDFToolTipWindow> PDFToolTipHelper::s_window;

void PDFToolTip::showText(const QPoint& globalPosition, const QString& title, const QString& shortcut, const QString& description, QWidget* widget)
{
    showText(globalPosition, title, shortcut, description, widget, Style::Default);
}

void PDFToolTip::showText(const QPoint& globalPosition, const QString& title, const QString& shortcut, const QString& description, QWidget* widget, Style style)
{
    if (title.isEmpty() || !qobject_cast<QApplication*>(QCoreApplication::instance()))
    {
        hideText();
        return;
    }

    if (style != Style::Default && style != Style::Compact)
    {
        style = Style::Default;
    }

    PDFToolTipHelper::getWindow()->showTip(globalPosition, title, shortcut, description, widget, style);
}

PDFToolTip::Style PDFToolTip::getStyle()
{
    return isVisible() ? PDFToolTipHelper::s_window->getStyle() : Style::Default;
}

bool PDFToolTip::showForWidget(QWidget* widget, const QPoint& globalPosition, const QPoint& position)
{
    if (!widget)
    {
        return false;
    }

    QString text = widget->toolTip();
    QString shortcut;

    if (text.isEmpty())
    {
        // Tooltip of an item of a view
        const QAbstractItemView* view = qobject_cast<const QAbstractItemView*>(widget->parentWidget());
        if (view && view->viewport() == widget)
        {
            // A delegate of the components shows the tooltips of its items and
            // of the actions inside of them itself (PDFRowDelegate::helpEvent)
            const QModelIndex index = view->indexAt(position);
            if (index.isValid() && qobject_cast<const PDFRowDelegate*>(view->itemDelegateForIndex(index)))
            {
                return false;
            }

            text = index.data(Qt::ToolTipRole).toString();
        }
    }

    if (text.isEmpty() || Qt::mightBeRichText(text))
    {
        // A rich text is left to the tooltip of Qt
        return false;
    }

    if (const QAbstractButton* button = qobject_cast<const QAbstractButton*>(widget))
    {
        QKeySequence sequence = button->shortcut();

        if (const QToolButton* toolButton = qobject_cast<const QToolButton*>(widget))
        {
            if (sequence.isEmpty() && toolButton->defaultAction())
            {
                sequence = toolButton->defaultAction()->shortcut();
            }
        }

        shortcut = sequence.toString(QKeySequence::NativeText);
    }

    const qsizetype newLine = text.indexOf(QLatin1Char('\n'));
    const QString title = newLine >= 0 ? text.left(newLine).trimmed() : text;
    const QString description = newLine >= 0 ? text.mid(newLine + 1).trimmed() : QString();

    showText(globalPosition, title, shortcut, description, widget);
    return true;
}

void PDFToolTip::hideText()
{
    if (PDFToolTipHelper::s_window)
    {
        PDFToolTipHelper::s_window->hideTip();
    }
}

bool PDFToolTip::isVisible()
{
    return PDFToolTipHelper::s_window && PDFToolTipHelper::s_window->isVisible();
}

QString PDFToolTip::getTitle()
{
    return isVisible() ? PDFToolTipHelper::s_window->getTitle() : QString();
}

QWidget* PDFToolTip::getWindow()
{
    return PDFToolTipHelper::s_window;
}

}   // namespace pdf

#include "pdftooltip.moc"
