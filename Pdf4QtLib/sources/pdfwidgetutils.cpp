//    Copyright (C) 2019-2022 Jakub Melka
//
//    This file is part of PDF4QT.
//
//    PDF4QT is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    PDF4QT is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDF4QT.  If not, see <https://www.gnu.org/licenses/>.

#include "pdfwidgetutils.h"
#include "pdfdbgheap.h"

#include <QDialog>
#include <QLayout>
#include <QGroupBox>

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

} // namespace pdf
