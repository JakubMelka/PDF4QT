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

#include "pdfapplifetimetokens.h"
#include "pdfappdocumentstatusbar.h"

#include "pdfdocumentdrawinterface.h"
#include "pdfdrawspacecontroller.h"
#include "pdfdrawwidget.h"

namespace pdf
{

// ---------------------------------------------------------------- canvas input

PDFAppCanvasInputToken::PDFAppCanvasInputToken(QObject* owner, PDFWidget* widget, IDrawWidgetInputInterface* inputInterface) :
    BaseClass(owner),
    m_widget(inputInterface ? widget : nullptr),
    m_inputInterface(widget ? inputInterface : nullptr)
{
    setObjectName(QStringLiteral("canvasInputToken"));
    if (m_widget)
    {
        m_widget->addInputInterface(m_inputInterface);
    }
}

PDFAppCanvasInputToken::~PDFAppCanvasInputToken()
{
    releaseToken();
}

PDFWidget* PDFAppCanvasInputToken::getWidget() const
{
    return m_widget.data();
}

void PDFAppCanvasInputToken::release()
{
    // The canvas only forgets the pointer (the interface is not called)
    if (PDFWidget* widget = m_widget.data())
    {
        m_widget = nullptr;
        widget->removeInputInterface(m_inputInterface);
    }
}

// ---------------------------------------------------------------- canvas draw

PDFAppCanvasDrawToken::PDFAppCanvasDrawToken(QObject* owner, PDFDrawWidgetProxy* proxy, IDocumentDrawInterface* drawInterface) :
    BaseClass(owner),
    m_proxy(drawInterface ? proxy : nullptr),
    m_drawInterface(proxy ? drawInterface : nullptr)
{
    setObjectName(QStringLiteral("canvasDrawToken"));
    if (m_proxy)
    {
        m_proxy->registerDrawInterface(m_drawInterface);
    }
}

PDFAppCanvasDrawToken::~PDFAppCanvasDrawToken()
{
    releaseToken();
}

PDFDrawWidgetProxy* PDFAppCanvasDrawToken::getProxy() const
{
    return m_proxy.data();
}

void PDFAppCanvasDrawToken::release()
{
    if (PDFDrawWidgetProxy* proxy = m_proxy.data())
    {
        m_proxy = nullptr;
        proxy->unregisterDrawInterface(m_drawInterface);

        // The canvas repaints without the interface (the last call: its slots run code of the view)
        Q_EMIT proxy->repaintNeeded();
    }
}

// ---------------------------------------------------------------- status message

PDFAppStatusMessageToken::PDFAppStatusMessageToken(QObject* owner, PDFAppDocumentStatusBar* statusBar, const QString& text) :
    BaseClass(owner),
    m_statusBar(statusBar),
    m_text(text)
{
    setObjectName(QStringLiteral("statusMessageToken"));
    if (m_statusBar)
    {
        m_previousText = m_statusBar->getMessage();
        m_statusBar->showMessage(m_text);
    }
}

PDFAppStatusMessageToken::~PDFAppStatusMessageToken()
{
    releaseToken();
}

void PDFAppStatusMessageToken::setText(const QString& text)
{
    if (isReleased())
    {
        return;
    }

    m_text = text;
    if (m_statusBar)
    {
        m_statusBar->showMessage(m_text);
    }
}

void PDFAppStatusMessageToken::release()
{
    PDFAppDocumentStatusBar* statusBar = m_statusBar.data();
    m_statusBar = nullptr;
    if (statusBar && statusBar->getMessage() == m_text)
    {
        statusBar->showMessage(m_previousText);
    }
}

// ---------------------------------------------------------------- cursor

PDFAppCursorToken::PDFAppCursorToken(QObject* owner, QWidget* widget, const QCursor& cursor) :
    BaseClass(owner),
    m_widget(widget)
{
    setObjectName(QStringLiteral("cursorToken"));
    if (m_widget)
    {
        m_hadCursor = m_widget->testAttribute(Qt::WA_SetCursor);
        m_previousCursor = m_widget->cursor();
        m_widget->setCursor(cursor);
    }
}

PDFAppCursorToken::~PDFAppCursorToken()
{
    releaseToken();
}

void PDFAppCursorToken::setCursor(const QCursor& cursor)
{
    if (!isReleased() && m_widget)
    {
        m_widget->setCursor(cursor);
    }
}

void PDFAppCursorToken::release()
{
    QWidget* widget = m_widget.data();
    m_widget = nullptr;
    if (!widget)
    {
        return;
    }

    if (m_hadCursor)
    {
        widget->setCursor(m_previousCursor);
    }
    else
    {
        widget->unsetCursor();
    }
}

}   // namespace pdf
