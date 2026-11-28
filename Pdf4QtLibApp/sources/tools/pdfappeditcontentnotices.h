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

#ifndef PDFAPPEDITCONTENTNOTICES_H
#define PDFAPPEDITCONTENTNOTICES_H

#include "pdfappglobal.h"
#include "pdfappeditcontentcontext.h"
#include "pdfdeferreddeletion.h"

#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QWidget>

#include <optional>
#include <vector>

class QAbstractButton;

namespace pdf
{

class PDFAppToolCallout;

// Edit Content, stage E1, package P14 (GUI of the tool). Plan: zadani_gui.md 4.15 ("Notices"),
// zadani.md KE.28 (an own widget; PDFAppInfoBar and the shared layout of the info bars of the
// shell are neither extended nor used), 12.2 (QE.2: the notice of PDF/A and PDF/UA at the first
// opening); the text "row-note-Edit Content v2 - notices and modes" of Figma.

/// Identifiers of the notices, which more packages name
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentNoticeIds
{
public:
    PDFAppEditContentNoticeIds() = delete;

    static constexpr const char* CONFORMANCE = "conformance";       ///< QE.2: the document declares PDF/A or PDF/UA (state, once per session; P14)
    static constexpr const char* PREVIEW = "preview";               ///< Preview is on (state; P14)
    static constexpr const char* READ_ONLY_PAGE = "read-only-page"; ///< The page cannot be edited (state; P14)
    static constexpr const char* SCANNED_PAGE = "scanned-page";     ///< A scan: one image (state; P14)
    static constexpr const char* COMPLEX_PAGE = "complex-page";     ///< QE.10 (state; P62 measures, P14 shows)
    static constexpr const char* USER_UNIT = "user-unit";           ///< KE.20 (state; P14)
    static constexpr const char* STALE_DOCUMENT = "stale-document"; ///< The document changed under the tool with work (state; P14)
    static constexpr const char* COMMAND_REFUSED = "command-refused";   ///< A refused command (event; P14 from commandRefused)
    static constexpr const char* LOCKED_OBJECT = "locked-object";   ///< A click on a locked object (event; P11)
};

/// The notices of the tool: what is asked to be shown and what is shown (zadani_gui.md 4.15).
///
/// - It listens to PDFAppEditContentContext::noticeRequested / noticeHideRequested.
/// - One notice is shown at a time: an Event notice covers the State notice below it and leaves
///   with the next change of the selection; the State notices wait in the order of their
///   requests (the newest first) until hideNotice of their id.
/// - A notice closed by the user: an Event notice is gone; a State notice stays hidden until
///   its state is entered again (hide, then show); a notice with isOncePerSession is not shown
///   again until the tool closes.
/// - The button of a notice triggers its action (PDFAppEditContentContext::triggerAction).
///
/// Threads and ownership: a child of the GUI of the tool (it lives as long as the tool: the
/// state of the notices survives the rebuild of the parts), GUI thread. The widget is a part and
/// only shows getCurrent. currentChanged is emitted last.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentNotices : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFAppEditContentNotices(PDFAppEditContentContext* context, QObject* parent);
    virtual ~PDFAppEditContentNotices() override;

    /// The notice shown now; no value = none
    std::optional<PDFAppEditContentNoticeRequest> getCurrent() const;

    /// The user has closed the notice shown (the close button of the widget)
    void closeCurrent();

    /// The user has pressed the button of the notice shown
    void triggerCurrentAction();

    /// The context (nullptr after it has gone); the widget reads the titles of the actions
    PDFAppEditContentContext* getContext() const { return m_context.data(); }

Q_SIGNALS:
    /// getCurrent changed
    void currentChanged();

private:
    void onNoticeRequested(const pdf::PDFAppEditContentNoticeRequest& request);
    void onNoticeHideRequested(const QString& noticeId);
    void onSelectionChanged();

    /// Emits currentChanged (last), when getCurrent differs from \p before
    void emitIfChanged(const std::optional<PDFAppEditContentNoticeRequest>& before);

    QPointer<PDFAppEditContentContext> m_context;
    std::vector<PDFAppEditContentNoticeRequest> m_stateNotices;     ///< Requested State notices, the newest last
    std::optional<PDFAppEditContentNoticeRequest> m_eventNotice;
    QSet<QString> m_closedNotices;          ///< State notices closed by the user and not left yet
    QSet<QString> m_sessionClosedNotices;   ///< isOncePerSession notices closed by the user
};

/// The widget of the notice at the bottom edge of the canvas (zadani_gui.md 4.15, KE.28): the
/// existing component PDFAppToolCallout with a close button and an optional action button, the
/// text wrapped; hidden without a notice. The GUI of the tool creates it and gives it to
/// PDFAppEditContentCanvas::setNoticeWidget, which places it.
///
/// Threads and ownership: a part - a child of the canvas; it keeps no state (it shows
/// PDFAppEditContentNotices::getCurrent, held by a QPointer). GUI thread.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentNotice : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAppEditContentNotice(PDFAppEditContentNotices* notices, QWidget* parent);
    virtual ~PDFAppEditContentNotice() override;

    /// The texts shown (tests); empty without a notice
    QString getTitle() const;
    QString getText() const;

protected:
    /// A change of the language: the tooltip and the accessible name of the close button (R8-13; the texts of the
    /// notice come with the notices requested again by the GUI)
    virtual void changeEvent(QEvent* event) override;

private:
    void onCurrentChanged();

    /// The translated texts of the close button
    void updateTexts();
    void onCloseClicked();
    void onActionClicked();

    QPointer<PDFAppEditContentNotices> m_notices;
    QPointer<PDFAppToolCallout> m_callout;      ///< The callout of the notice shown (rebuilt when the notice changes: its hue and icon are fixed)
    QPointer<QWidget> m_closeHolder;            ///< The close button over the head of the callout
    QPointer<QAbstractButton> m_closeButton;
    QString m_title;
    QString m_text;
};

}   // namespace pdf

#endif // PDFAPPEDITCONTENTNOTICES_H
