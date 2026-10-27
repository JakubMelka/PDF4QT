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

#ifndef PDFAPPMCPCONFIRMDIALOG_H
#define PDFAPPMCPCONFIRMDIALOG_H

#include "pdfappglobal.h"
#include "pdfappmcpconfirmation.h"

#include "pdfmessagebox.h"

#include <QFrame>
#include <QPointer>
#include <QStringList>

#include <memory>
#include <vector>

class QPushButton;

namespace pdf
{

class PDFAppMcpDocumentResolver;
class PDFLabel;
class PDFTag;
class PDFTextLink;

/// Box of the body of a confirmation (bg/subtle, radius 10, no border): the
/// change rows, the open documents, the file of an open request, the facts of a save
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpBodyBox : public QFrame
{
    Q_OBJECT

private:
    using BaseClass = QFrame;

public:
    explicit PDFAppMcpBodyBox(QWidget* parent);
    virtual ~PDFAppMcpBodyBox() override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;
};

/// Scrim of a confirmation over its tab (958:28030: "Modal for the tab only: the
/// scrim covers the tab, the tab strip and the window buttons stay live"): a child
/// of the tab, which covers the whole tab, takes the mouse and draws bg/scrim and
/// the shadow 0 / 8 / 24 of the card of its dialog, which it centers
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpConfirmScrim : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// \param host The tab (the scrim follows its size)
    explicit PDFAppMcpConfirmScrim(QWidget* host);
    virtual ~PDFAppMcpConfirmScrim() override;

    /// The dialog over the scrim (a child of the scrim, centered)
    void setDialog(QWidget* dialog);
    QWidget* getDialog() const { return m_dialog.data(); }

    /// Places the dialog again (its size changed)
    void placeDialog();

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QMouseEvent* event) override;
    virtual void mouseDoubleClickEvent(QMouseEvent* event) override;
    virtual void wheelEvent(QWheelEvent* event) override;
    virtual void contextMenuEvent(QContextMenuEvent* event) override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QPointer<QWidget> m_host;
    QPointer<QWidget> m_dialog;
};

/// Confirmation of a request of an assistant (plan 5.4, one template for all
/// dialogs 958:28030, card 520): the monogram of the assistant, "<Client> wants
/// to …", the origin and the time ("This computer · asked at 16:44", the Remote
/// tag and the address of a remote assistant), the position in the queue of the
/// tab ("1 of 3"), the body of the variant (the change rows with Show on Page
/// 940:133693, the open documents 966:150610, the facts of a save 940:134248 /
/// 958:27539, the file of an open request 940:136305), the notes (undo,
/// overwrite, the signatures 958:27048), the reason row and the countdown at the
/// leading side of the buttons. No default button (Enter does nothing, Escape is
/// Deny), the buttons ignore input for 1 s after the dialog appears. The dialog
/// is a child widget of its scrim (modal for its tab only). Texts written by the
/// model are plain texts, sanitized and elided (at most 80 characters).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpConfirmDialog : public PDFMessageBox
{
    Q_OBJECT

private:
    using BaseClass = PDFMessageBox;

public:
    /// Width of the card (958:28030)
    static constexpr int CARD_WIDTH = 520;

    /// Longest text of the model shown in a row (958:28030)
    static constexpr int MAXIMUM_QUOTE_LENGTH = 80;

    /// Most change rows and document rows of the body (the rest is counted)
    static constexpr int MAXIMUM_ROWS = 6;

    /// \param resolver The open documents of a read approval (may be nullptr)
    explicit PDFAppMcpConfirmDialog(std::shared_ptr<PDFAppMcpConfirmation> confirmation, const PDFAppMcpDocumentResolver* resolver, QWidget* parent);
    virtual ~PDFAppMcpConfirmDialog() override;

    quint64 getConfirmationId() const;
    const std::shared_ptr<PDFAppMcpConfirmation>& getConfirmation() const { return m_confirmation; }

    /// Position in the queue of the tab ("1 of 3"; hidden for one request)
    void setQueuePosition(int index, int count);
    QString getQueueText() const;

    /// Remaining time of the countdown ("Denied automatically in 1:52")
    void setRemainingTime(qint64 milliseconds);
    QString getCountdownText() const;

    /// The button of the verb (Allow, Open, Save, Save Copy, Close) and Deny
    QPushButton* getAllowButton() const;
    QPushButton* getDenyButton() const;

    /// Show on Page of a change of annotations (nullptr for other variants)
    PDFTextLink* getShowOnPageLink() const;

    /// The texts of the dialog (tests)
    QString getTitleText() const;
    QString getOriginText() const;
    QString getReasonText() const;
    QStringList getNoteTexts() const;
    QStringList getRowTexts() const;

    /// "1:52" of the remaining milliseconds (rounded up to whole seconds)
    static QString formatRemainingTime(qint64 milliseconds);

    /// Title of the confirmation ("Claude Code wants to change Invoice.pdf")
    static QString createTitle(const PDFAppMcpConfirmation& confirmation);

    /// Reason row (958:28030): the permissions set to Ask, or the rule
    static QString createReason(const PDFAppMcpConfirmation& confirmation);

    /// A text of the model or of a document for a row: controls and bidirectional
    /// overrides removed, one line, at most \p maximumLength characters (elided)
    static QString sanitize(const QString& text, int maximumLength = MAXIMUM_QUOTE_LENGTH);

    virtual void reject() override;

Q_SIGNALS:
    /// The verb button was clicked
    void allowed(quint64 confirmationId);

    /// Deny or Escape
    void denied(quint64 confirmationId);

    /// Show on Page of a change of annotations
    void showOnPageRequested(quint64 confirmationId);

protected:
    virtual bool focusNextPrevChild(bool next) override;
    virtual void changeEvent(QEvent* event) override;

private:
    void build();
    void updateTexts();
    void onAllowClicked();
    void onDenyClicked();
    void onShowOnPageClicked();

    QWidget* createHead();
    QWidget* createInfoRow(const QString& iconName, PDFLabel** label);
    void addChangeBody();
    void addReadBody();
    void addSaveBody();
    void addOpenBody();
    void addCloseBody();

    std::shared_ptr<PDFAppMcpConfirmation> m_confirmation;
    const PDFAppMcpDocumentResolver* m_resolver = nullptr;
    QPointer<PDFLabel> m_titleLabel;
    QPointer<PDFLabel> m_originLabel;
    QPointer<PDFTag> m_remoteTag;
    QPointer<PDFTag> m_queueTag;
    QPointer<PDFLabel> m_reasonLabel;
    QPointer<PDFLabel> m_countdownLabel;
    QPointer<PDFTextLink> m_showOnPageLink;
    QPointer<QPushButton> m_allowButton;
    QPointer<QPushButton> m_denyButton;
    std::vector<QPointer<PDFLabel>> m_noteLabels;
    std::vector<QPointer<PDFLabel>> m_rowLabels;
    QPointer<PDFLabel> m_scopeLabel;
    QPointer<PDFLabel> m_bodyLabel;
    qint64 m_remainingTime = 0;
    int m_queueIndex = 0;
    int m_queueCount = 1;
};

}   // namespace pdf

#endif // PDFAPPMCPCONFIRMDIALOG_H
