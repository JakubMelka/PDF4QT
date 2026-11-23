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

#ifndef PDFAPPLIFETIMETOKENS_H
#define PDFAPPLIFETIMETOKENS_H

#include "pdfappglobal.h"
#include "pdfdeferreddeletion.h"

#include <QCursor>
#include <QPointer>
#include <QString>
#include <QWidget>

namespace pdf
{
class IDocumentDrawInterface;
class IDrawWidgetInputInterface;
class PDFAppDocumentStatusBar;
class PDFDrawWidgetProxy;
class PDFWidget;

/// Registrations of the application outside the subtree of their owner as tokens (step A, newapp_robustness.md 14.4,
/// 15.7): thin App wrappers over the calls of Pdf4QtLibWidgets (Widgets is not changed). A token is a child of the
/// registering object (the owner); PDFDeferredDeletion::retire of the owner (or of an ancestor, or of its logical
/// owner) ends the registration at once (step 4), otherwise the destruction of the token does, or releaseToken().
/// The registration starts in the constructor. The interface / the registered object is not owned and must live as
/// long as the token (usually the owner itself or its member). GUI thread only.

/// The input interface of an overlay on a canvas (PDFWidget::addInputInterface / removeInputInterface)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppCanvasInputToken : public PDFLifetimeToken
{
private:
    using BaseClass = PDFLifetimeToken;

public:
    /// \param owner The registering object (the parent of the token)
    /// \param widget The canvas (nullptr = nothing is registered)
    /// \param inputInterface The interface (nullptr = nothing is registered)
    explicit PDFAppCanvasInputToken(QObject* owner, PDFWidget* widget, IDrawWidgetInputInterface* inputInterface);
    virtual ~PDFAppCanvasInputToken() override;

    /// The canvas, nullptr after it has gone
    PDFWidget* getWidget() const;

protected:
    virtual void release() override;

private:
    QPointer<PDFWidget> m_widget;
    IDrawWidgetInputInterface* m_inputInterface = nullptr;
};

/// The draw interface of an overlay or a highlight on a canvas (PDFDrawWidgetProxy::registerDrawInterface /
/// unregisterDrawInterface); the release asks the canvas to repaint (the overlay disappears)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppCanvasDrawToken : public PDFLifetimeToken
{
private:
    using BaseClass = PDFLifetimeToken;

public:
    /// \param owner The registering object (the parent of the token)
    /// \param proxy The proxy of the canvas (nullptr = nothing is registered)
    /// \param drawInterface The interface (nullptr = nothing is registered)
    explicit PDFAppCanvasDrawToken(QObject* owner, PDFDrawWidgetProxy* proxy, IDocumentDrawInterface* drawInterface);
    virtual ~PDFAppCanvasDrawToken() override;

    /// The proxy, nullptr after it has gone
    PDFDrawWidgetProxy* getProxy() const;

protected:
    virtual void release() override;

private:
    QPointer<PDFDrawWidgetProxy> m_proxy;
    IDocumentDrawInterface* m_drawInterface = nullptr;
};

/// A message of the owner in the status bar of a document view (PDFAppDocumentStatusBar::showMessage): the release
/// gives back the message shown before the token, when the bar still shows the owner's message (a newer message of
/// somebody else stays)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppStatusMessageToken : public PDFLifetimeToken
{
private:
    using BaseClass = PDFLifetimeToken;

public:
    /// \param owner The registering object (the parent of the token)
    /// \param statusBar The status bar (nullptr = nothing is shown)
    /// \param text The message (shown at once, permanent)
    explicit PDFAppStatusMessageToken(QObject* owner, PDFAppDocumentStatusBar* statusBar, const QString& text);
    virtual ~PDFAppStatusMessageToken() override;

    /// Shows another message of the owner (nothing after the release)
    void setText(const QString& text);

    const QString& getText() const { return m_text; }

protected:
    virtual void release() override;

private:
    QPointer<PDFAppDocumentStatusBar> m_statusBar;
    QString m_text;
    QString m_previousText;
};

/// A cursor set by the owner on a foreign widget (QWidget::setCursor): the release gives back the cursor of the
/// widget before the token (its own set cursor, or unsetCursor)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppCursorToken : public PDFLifetimeToken
{
private:
    using BaseClass = PDFLifetimeToken;

public:
    /// \param owner The registering object (the parent of the token)
    /// \param widget The foreign widget (nullptr = nothing is set)
    /// \param cursor The cursor (set at once)
    explicit PDFAppCursorToken(QObject* owner, QWidget* widget, const QCursor& cursor);
    virtual ~PDFAppCursorToken() override;

    /// Sets another cursor of the owner (nothing after the release)
    void setCursor(const QCursor& cursor);

protected:
    virtual void release() override;

private:
    QPointer<QWidget> m_widget;
    bool m_hadCursor = false;
    QCursor m_previousCursor;
};

}   // namespace pdf

#endif // PDFAPPLIFETIMETOKENS_H
