// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
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

#include "pdfwidgetutils.h"
#include "pdfcolorconvertor.h"

#include <map>
#include <set>
#include <optional>

#include <QMenu>
#include <QAction>
#include <QDialog>
#include <QLayout>
#include <QPainter>
#include <QPixmap>
#include <QGroupBox>
#include <QMessageBox>
#include <QApplication>
#include <QStyleHints>
#include <QStyleFactory>

#include "pdfdbgheap.h"

#ifdef Q_OS_MAC
int qt_default_dpi_x() { return 72; }
int qt_default_dpi_y() { return 72; }
#else
int qt_default_dpi_x() { return 96; }
int qt_default_dpi_y() { return 96; }
#endif

namespace pdf
{

static constexpr bool isScalingNeeded()
{
    return QT_VERSION_MAJOR < 6;
}

int PDFWidgetUtils::getPixelSize(const QPaintDevice* device, pdf::PDFReal sizeMM)
{
    const int width = device->width();
    const int height = device->height();

    if (width > height)
    {
        return PDFReal(width) * sizeMM / PDFReal(device->widthMM());
    }
    else
    {
        return PDFReal(height) * sizeMM / PDFReal(device->heightMM());
    }
}

int PDFWidgetUtils::scaleDPI_x(const QPaintDevice* device, int unscaledSize)
{
    if constexpr (isScalingNeeded())
    {
        const double logicalDPI_x = device->logicalDpiX();
        const double defaultDPI_x = qt_default_dpi_x();
        return (logicalDPI_x / defaultDPI_x) * unscaledSize;
    }
    else
    {
        return unscaledSize;
    }
}

int PDFWidgetUtils::scaleDPI_y(const QPaintDevice* device, int unscaledSize)
{
    if constexpr (isScalingNeeded())
    {
        const double logicalDPI_y = device->logicalDpiY();
        const double defaultDPI_y = qt_default_dpi_y();
        return (logicalDPI_y / defaultDPI_y) * unscaledSize;
    }
    else
    {
        return unscaledSize;
    }
}

PDFReal PDFWidgetUtils::scaleDPI_x(const QPaintDevice* device, PDFReal unscaledSize)
{
    if constexpr (isScalingNeeded())
    {
        const double logicalDPI_x = device->logicalDpiX();
        const double defaultDPI_x = qt_default_dpi_x();
        return (logicalDPI_x / defaultDPI_x) * unscaledSize;
    }
    else
    {
        return unscaledSize;
    }
}

void PDFWidgetUtils::scaleWidget(QWidget* widget, QSize unscaledSize)
{
    if constexpr (!isScalingNeeded())
    {
        return;
    }

    const double logicalDPI_x = widget->logicalDpiX();
    const double logicalDPI_y = widget->logicalDpiY();
    const double defaultDPI_x = qt_default_dpi_x();
    const double defaultDPI_y = qt_default_dpi_y();

    const int width = (logicalDPI_x / defaultDPI_x) * unscaledSize.width();
    const int height = (logicalDPI_y / defaultDPI_y) * unscaledSize.height();

    widget->resize(width, height);
}

QSize PDFWidgetUtils::scaleDPI(const QPaintDevice* widget, QSize unscaledSize)
{
    if constexpr (isScalingNeeded())
    {
        const double logicalDPI_x = widget->logicalDpiX();
        const double logicalDPI_y = widget->logicalDpiY();
        const double defaultDPI_x = qt_default_dpi_x();
        const double defaultDPI_y = qt_default_dpi_y();

        const int width = (logicalDPI_x / defaultDPI_x) * unscaledSize.width();
        const int height = (logicalDPI_y / defaultDPI_y) * unscaledSize.height();

        return QSize(width, height);
    }
    else
    {
        return unscaledSize;
    }
}

void PDFWidgetUtils::style(QWidget* widget)
{
    const int dialogMarginX = scaleDPI_x(widget, 12);
    const int dialogMarginY = scaleDPI_y(widget, 12);
    const int dialogSpacing = scaleDPI_y(widget, 12);

    const int groupBoxMarginX = scaleDPI_x(widget, 20);
    const int groupBoxMarginY = scaleDPI_y(widget, 20);
    const int groupBoxSpacing = scaleDPI_y(widget, 12);

    QList<QWidget*> childWidgets = widget->findChildren<QWidget*>();
    childWidgets.append(widget);

    for (QWidget* childWidget : childWidgets)
    {
        if (qobject_cast<QGroupBox*>(childWidget))
        {
            childWidget->layout()->setContentsMargins(groupBoxMarginX, groupBoxMarginY, groupBoxMarginX, groupBoxMarginY);
            childWidget->layout()->setSpacing(groupBoxSpacing);
        }
        else if (qobject_cast<QDialog*>(childWidget))
        {
            childWidget->layout()->setContentsMargins(dialogMarginX, dialogMarginY, dialogMarginX, dialogMarginY);
            childWidget->layout()->setSpacing(dialogSpacing);
        }
    }
}

void PDFWidgetUtils::setDarkTheme(bool isLightTheme, bool isDarkTheme)
{
    if (isLightTheme)
    {
        QApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light);
    }

    if (isDarkTheme)
    {
        QApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);
    }

    if (PDFWidgetUtils::isDarkTheme())
    {
        QPalette darkPalette = QApplication::palette();

#ifdef Q_OS_WIN
        QApplication::setStyle(QStyleFactory::create("Fusion"));

        // Basic colors
        darkPalette.setColor(QPalette::WindowText, QColor(220, 220, 220));
        darkPalette.setColor(QPalette::Button, QColor(53, 53, 53));
        darkPalette.setColor(QPalette::Light, QColor(70, 70, 70));
        darkPalette.setColor(QPalette::Midlight, QColor(60, 60, 60));
        darkPalette.setColor(QPalette::Dark, QColor(35, 35, 35));
        darkPalette.setColor(QPalette::Mid, QColor(40, 40, 40));

        // Texts
        darkPalette.setColor(QPalette::Text, QColor(220, 220, 220));
        darkPalette.setColor(QPalette::BrightText, Qt::red);
        darkPalette.setColor(QPalette::ButtonText, QColor(220, 220, 220));

        // Background
        darkPalette.setColor(QPalette::Base, QColor(42, 42, 42));
        darkPalette.setColor(QPalette::Window, QColor(53, 53, 53));
        darkPalette.setColor(QPalette::Shadow, QColor(20, 20, 20));

        // Highlight
        darkPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));    // Barva výběru
        darkPalette.setColor(QPalette::HighlightedText, Qt::black);         // Text ve výběru

        // Links
        darkPalette.setColor(QPalette::Link, QColor(42, 130, 218));         // Odkazy
        darkPalette.setColor(QPalette::LinkVisited, QColor(100, 100, 150)); // Navštívené odkazy

        // Alternative background
        darkPalette.setColor(QPalette::AlternateBase, QColor(66, 66, 66));  // Např. střídavé řádky v tabulce

        // Special roles
        darkPalette.setColor(QPalette::NoRole, QColor(0, 0, 0, 0));

        // Help
        darkPalette.setColor(QPalette::ToolTipBase, QColor(53, 53, 53));
        darkPalette.setColor(QPalette::ToolTipText, QColor(220, 220, 220));

        for (int i = 0; i < QPalette::NColorRoles; ++i)
        {
            QColor disabledColor = darkPalette.color(QPalette::Disabled, static_cast<QPalette::ColorRole>(i));
            disabledColor = disabledColor.darker(200);
            darkPalette.setColor(QPalette::Disabled, static_cast<QPalette::ColorRole>(i), disabledColor);

            QColor currentColor = darkPalette.color(QPalette::Current, static_cast<QPalette::ColorRole>(i));
            currentColor = currentColor.lighter(150);
            darkPalette.setColor(QPalette::Current, static_cast<QPalette::ColorRole>(i), currentColor);
        }
#endif

        // Placeholder text (Qt 5.12+)
        darkPalette.setColor(QPalette::PlaceholderText, QColor(150, 150, 150));

        // Accents (Qt 6.5+)
        darkPalette.setColor(QPalette::Accent, QColor(42, 130, 218));

        QApplication::setPalette(darkPalette);
    }
}

bool PDFWidgetUtils::isDarkTheme()
{
    Qt::ColorScheme colorScheme = QApplication::styleHints()->colorScheme();
    return colorScheme == Qt::ColorScheme::Dark;
}

void PDFWidgetUtils::convertActionForDarkTheme(QAction* action, QSize iconSize, qreal devicePixelRatioF)
{
    if (!action)
    {
        return;
    }

    QIcon icon = action->icon();
    if (!icon.isNull())
    {
        icon = pdf::PDFWidgetUtils::convertIconForDarkTheme(icon, iconSize, devicePixelRatioF);
        action->setIcon(icon);
    }
}

QIcon PDFWidgetUtils::convertIconForDarkTheme(QIcon icon, QSize iconSize, qreal devicePixelRatioF)
{
    QPixmap pixmap(iconSize * devicePixelRatioF);
    pixmap.setDevicePixelRatio(devicePixelRatioF);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    icon.paint(&painter, QRect(0, 0, iconSize.width(), iconSize.height()));
    painter.end();

    PDFColorConvertor convertor;
    convertor.setMode(PDFColorConvertor::Mode::InvertedColors);

    QImage image = pixmap.toImage();
    image = convertor.convert(image);
    pixmap = QPixmap::fromImage(image);

    return QIcon(pixmap);
}

void PDFWidgetUtils::checkMenuAccessibility(QWidget* widget)
{
    QList<QMenu*> menus = widget->findChildren<QMenu*>();

    for (QMenu* menu : menus)
    {
        checkMenuAccessibility(menu);
    }
}

void PDFWidgetUtils::checkMenuAccessibility(QMenu* menu)
{
    QStringList actionsDuplicities;
    QStringList actionsWithNoAmpersands;
    std::map<QChar, std::set<QAction*>> actions;

    for (QAction* action : menu->actions())
    {
        if (action->isSeparator())
        {
            continue;
        }

        if (QMenu* submenu = action->menu())
        {
            checkMenuAccessibility(submenu);
        }

        QString text = action->text();
        int i = text.indexOf(QChar('&'));

        if (text.isEmpty())
        {
            continue;
        }

        if (i == -1)
        {
            actionsWithNoAmpersands << text;
        }
        else
        {
            QString accesibilityChar = text.mid(i + 1, 1);
            if (accesibilityChar.size() == 1)
            {
                actions[accesibilityChar.front().toUpper()].insert(action);
            }
        }
    }

    for (const auto& actionItem : actions)
    {
        if (actionItem.second.size() > 1)
        {
            QStringList actionTexts;
            for (QAction* action : actionItem.second)
            {
                actionTexts << action->text();
            }

            actionsDuplicities << QString("'%1': Duplicities = '%2'").arg(QString(actionItem.first), actionTexts.join("', '"));
        }
    }

    QStringList errors;
    errors.append(std::move(actionsDuplicities));
    errors.append(std::move(actionsWithNoAmpersands));

    if (!errors.isEmpty())
    {
        QMessageBox::warning(nullptr, "Accesibility Check", errors.join("<br>"));
    }
}

} // namespace pdf
