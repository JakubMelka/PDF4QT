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

#ifndef PDFAPPMCPPREVIEW_H
#define PDFAPPMCPPREVIEW_H

#include "pdfappglobal.h"
#include "pdfappfloatingbar.h"
#include "pdfappmcpchange.h"

#include "pdfdocumentdrawinterface.h"

#include <QPointer>

#include <vector>

class QPushButton;
class QTimer;

namespace pdf
{

class PDFIconButton;
class PDFLabel;

/// Ghosts of the proposed annotations of a confirmation (955:151877): the
/// annotation color at 30 % multiplied with the page, a dashed border 1.5 px of
/// accent/amber/border-strong (radius 2; the note marker 20 with its icon, radius 4).
/// A draw interface of the proxy of the document view.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpGhostOverlay : public IDocumentDrawInterface
{
public:
    PDFAppMcpGhostOverlay() = default;

    void setGhosts(std::vector<PDFAppMcpGhost> ghosts) { m_ghosts = std::move(ghosts); }
    const std::vector<PDFAppMcpGhost>& getGhosts() const { return m_ghosts; }

    virtual void drawPage(QPainter* painter,
                          PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QTransform& pagePointToDevicePointMatrix,
                          const PDFColorConvertor& convertor,
                          QList<PDFRenderError>& errors) const override;

    /// Draws the ghosts of the page (tests draw into an image)
    void drawGhosts(QPainter* painter, PDFInteger pageIndex, const QTransform& matrix) const;

    /// Extent of the note marker in pixels
    static constexpr int NOTE_EXTENT = 20;

private:
    std::vector<PDFAppMcpGhost> m_ghosts;
};

/// Floating bar of the assistants over the canvas (preview 555 x 48, notification
/// 591 x 48 - Figma map 5.10): radius 14, padding 12 / 10 / 5 inside of the
/// border, gap 8, buttons 36, the monogram 24 of the assistant and its message
/// Semi Bold 13 text/primary; centered 12 px below the top of the canvas
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpFloatingBar : public PDFAppFloatingBar
{
    Q_OBJECT

private:
    using BaseClass = PDFAppFloatingBar;

public:
    static constexpr int CARD_HEIGHT = 48;
    static constexpr int GAP = 8;

    PDFAppMcpFloatingBar(const QString& clientNameRaw, QWidget* parent);
    virtual ~PDFAppMcpFloatingBar() override;

    void setMessage(const QString& text);
    QString getMessage() const;

protected:
    virtual bool event(QEvent* event) override;

    /// A Secondary or Primary button of the bar (36 high)
    QPushButton* addButton(const QString& objectName, bool isPrimary);

    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void applyMargins();

    /// The message elided to the width of the canvas (a narrow window), its whole
    /// text the tooltip
    void fitMessage();
    void invalidateLayouts();

    QPointer<PDFLabel> m_messageLabel;
    QPointer<QWidget> m_watchedParent;
    QString m_message;
    bool m_isFitting = false;
};

/// Bar of the preview of a confirmation (955:152311): "Claude Code wants to add
/// these · 3 highlights, 1 note", Deny / Allow
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpPreviewBar : public PDFAppMcpFloatingBar
{
    Q_OBJECT

private:
    using BaseClass = PDFAppMcpFloatingBar;

public:
    PDFAppMcpPreviewBar(quint64 confirmationId, const QString& clientName, const QString& clientNameRaw, const QString& summary, QWidget* parent);
    virtual ~PDFAppMcpPreviewBar() override;

    quint64 getConfirmationId() const { return m_confirmationId; }
    QPushButton* getAllowButton() const;
    QPushButton* getDenyButton() const;

Q_SIGNALS:
    void allowed(quint64 confirmationId);
    void denied(quint64 confirmationId);

protected:
    virtual void changeEvent(QEvent* event) override;

private:
    void updateTexts();
    void onAllowClicked();
    void onDenyClicked();

    quint64 m_confirmationId = 0;
    QString m_clientName;
    QString m_summary;
    QPointer<QPushButton> m_allowButton;
    QPointer<QPushButton> m_denyButton;
};

/// Notification bar after a change of an assistant (940:33875 / 955:151457, also
/// with Edit documents = Allow - owner Q3): "Claude Code added 3 highlights and 1
/// note" · Show Changes · Undo · close. Undo undoes only while the change is the
/// last step of the document (else it is disabled with its tooltip). The bar
/// hides itself after 30 s or at the next change of the document.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpNotificationBar : public PDFAppMcpFloatingBar
{
    Q_OBJECT

private:
    using BaseClass = PDFAppMcpFloatingBar;

public:
    PDFAppMcpNotificationBar(const PDFAppMcpCommitInfo& info, QWidget* parent);
    virtual ~PDFAppMcpNotificationBar() override;

    const PDFAppMcpCommitInfo& getInfo() const { return m_info; }

    QPushButton* getShowChangesButton() const;
    QPushButton* getUndoButton() const;
    PDFIconButton* getCloseButton() const;

    /// The change is still the last step of the document (Undo is enabled)
    bool isChangeOnTop() const;

    /// Updates the state of Undo (the document changed)
    void updateUndoState();

    /// The bar asks to be closed after \p milliseconds (30 s - owner Q3)
    void startAutoHide(int milliseconds);

Q_SIGNALS:
    void showChangesRequested();
    void undoRequested();
    void closeRequested();

protected:
    virtual void changeEvent(QEvent* event) override;

private:
    void updateTexts();
    void onShowChangesClicked();
    void onUndoClicked();
    void onCloseClicked();
    void onDocumentChanged();

    PDFAppMcpCommitInfo m_info;
    QPointer<QTimer> m_autoHideTimer;
    QPointer<QPushButton> m_showChangesButton;
    QPointer<QPushButton> m_undoButton;
    QPointer<PDFIconButton> m_closeButton;
};

}   // namespace pdf

#endif // PDFAPPMCPPREVIEW_H
