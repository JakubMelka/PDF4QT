//    Copyright (C) 2018 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.


#ifndef PDFDRAWWIDGET_H
#define PDFDRAWWIDGET_H

#include "pdfglobal.h"

#include <QWidget>
#include <QScrollBar>

namespace pdf
{
class PDFDocument;
class PDFDrawWidget;
class PDFDrawWidgetProxy;

class PDFFORQTLIBSHARED_EXPORT PDFWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PDFWidget(QWidget* parent);
    virtual ~PDFWidget() override;

    /// Sets the document to be viewed in this widget. Document can be nullptr,
    /// in that case, widget contents are cleared.
    /// \param document Document
    void setDocument(const PDFDocument* document);

    PDFDrawWidget* getDrawWidget() const { return m_drawWidget; }
    QScrollBar* getHorizontalScrollbar() const { return m_horizontalScrollBar; }
    QScrollBar* getVerticalScrollbar() const { return m_verticalScrollBar; }
    PDFDrawWidgetProxy* getDrawWidgetProxy() const { return m_proxy; }

private:
    PDFDrawWidget* m_drawWidget;
    QScrollBar* m_horizontalScrollBar;
    QScrollBar* m_verticalScrollBar;
    PDFDrawWidgetProxy* m_proxy;
};

class PDFFORQTLIBSHARED_EXPORT PDFDrawWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PDFDrawWidget(PDFWidget* widget, QWidget* parent);
    virtual ~PDFDrawWidget() override;

    virtual QSize minimumSizeHint() const override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void resizeEvent(QResizeEvent* event) override;
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QMouseEvent* event) override;
    virtual void mouseMoveEvent(QMouseEvent* event) override;
    virtual void wheelEvent(QWheelEvent* event) override;

private:
    enum class MouseOperation
    {
        None,
        Translate
    };

    /// Performs the mouse operation (under the current mouse position)
    /// \param currentMousePosition Current position of the mouse
    void performMouseOperation(QPoint currentMousePosition);

    PDFWidget* m_widget;
    QPoint m_lastMousePosition;
    MouseOperation m_mouseOperation;
};

}   // namespace pdf

#endif // PDFDRAWWIDGET_H
