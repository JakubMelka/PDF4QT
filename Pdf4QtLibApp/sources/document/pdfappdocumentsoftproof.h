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

#ifndef PDFAPPDOCUMENTSOFTPROOF_H
#define PDFAPPDOCUMENTSOFTPROOF_H

#include "pdfappglobal.h"
#include "pdfcms.h"

#include <QColor>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QToolButton>

class QWidget;

namespace pdf
{

class PDFAppDocumentView;
class PDFDocument;

/// The item of the status bar of a tab with a kept soft proof (485:17529 "Soft proof: Coated FOGRA39",
/// status-soft-proof 485:17944): a pill accent/teal/bg with the border accent/teal/border, radius 11, the icon
/// soft-proof 14 at 8 px, 6 px gap, the text Medium 11 text/primary, 22 high. A click opens Soft Proofing (the
/// owner of the item connects clicked). Drawn by the tokens of the theme (no QPalette).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSoftProofStatusItem : public QToolButton
{
    Q_OBJECT

private:
    using BaseClass = QToolButton;

public:
    static constexpr int HEIGHT = 22;
    static constexpr int RADIUS = 11;
    static constexpr int PADDING = 8;
    static constexpr int ICON = 14;
    static constexpr int GAP = 6;

    explicit PDFAppSoftProofStatusItem(QWidget* parent);
    virtual ~PDFAppSoftProofStatusItem() override;

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;
};

/// The soft proof of one document tab (plan of step 7 of phase 2, 1 point 6, 4.7, 9.3; owner answer Q7.10):
/// on, the gamut check, the profile, the intent and the alarm color. A state of the view of the tab only:
/// PDFAppDocumentView applies it over the CMS settings of the application (applyTo, after readCMSSettings -
/// a change of the settings keeps the proof); every copy of the settings of the tab for something else gets
/// PDFCMSSettings::withoutProofing (the thumbnails, the printing, the tools - the six places of plan 4.7). It
/// is never saved between sessions; the gamut check is kept only with the proof (Q7.10).
///
/// Keep Proofing On of Soft Proofing (isKept): the proof stays after the tool closes, the item of the status
/// bar "Soft proof: <profile>" shows it (485:17529) and a click on it opens the tool again. The item is shown
/// only for a kept proof (the tool itself shows the state, while it is open).
///
/// Ownership and threads: a child of the view of the tab (getSoftProof creates it at the first use), it dies
/// with the view; the status item belongs to the status bar of the view (QPointer here). The GUI of Soft
/// Proofing holds the state by QPointer. GUI thread only; changed is emitted last, after the state has changed,
/// by a setter that changed something - its slot of the view only schedules an application of the settings.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentSoftProof : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFAppDocumentSoftProof(QObject* parent = nullptr);
    virtual ~PDFAppDocumentSoftProof() override;

    /// The object name of the state of a view (findSoftProof)
    static constexpr const char* OBJECT_NAME = "documentSoftProof";

    /// The tool, which a click on the status item opens
    static constexpr const char* TOOL_ID = "review.soft-proof";

    /// The values of the state at once (the entry of the tool remembers them, Close without Keep Proofing On
    /// gives them back)
    class State
    {
    public:
        bool operator==(const State&) const = default;

        bool isProofing = false;
        bool isGamutChecking = false;
        bool isKept = false;
        QString profile;
        QString profileTitle;
        RenderingIntent intent = RenderingIntent::RelativeColorimetric;
        QColor alarmColor = Qt::red;
    };

    /// Soft Proof (the switch of the toolbar)
    bool isProofing() const { return m_state.isProofing; }
    void setProofing(bool isProofing);

    /// Gamut Check (the switch of the toolbar)
    bool isGamutChecking() const { return m_state.isGamutChecking; }
    void setGamutChecking(bool isGamutChecking);

    /// The identifier of the CMYK profile (PDFCMSSettings::softProofingProfile; empty = the output intent of
    /// the document)
    const QString& getProfile() const { return m_state.profile; }
    void setProfile(const QString& profile);

    /// The name of the profile for the user (the description of the profile or its file name, Q7.11; the
    /// status item "Soft proof: Coated FOGRA39")
    const QString& getProfileTitle() const { return m_state.profileTitle; }
    void setProfileTitle(const QString& title);

    /// The rendering intent of the proof
    RenderingIntent getIntent() const { return m_state.intent; }
    void setIntent(RenderingIntent intent);

    /// The color of the colors out of gamut
    const QColor& getAlarmColor() const { return m_state.alarmColor; }
    void setAlarmColor(const QColor& color);

    /// Keep Proofing On: the proof stays after the tool closes (only with the proof on, Q7.10 - a request
    /// without the proof is ignored); the status item shows it
    bool isKept() const { return m_state.isKept; }
    void setKept(bool isKept);

    /// All values at once (one changed signal, when anything differs)
    const State& getState() const { return m_state; }
    void setState(const State& state);

    /// The proof is shown (the proof or the gamut check is on)
    bool isActive() const { return m_state.isProofing || m_state.isGamutChecking; }

    /// The settings of the view with this proof: the flags, the profile, the intent and the alarm color over
    /// \p settings (the proof off: withoutProofing of \p settings)
    PDFCMSSettings applyTo(const PDFCMSSettings& settings) const;

    /// The item of the status bar of the tab (485:17529 "Soft proof: Coated FOGRA39", Status bar item; a click
    /// opens Soft Proofing). The caller adds it to the status bar (PDFAppDocumentStatusBar::addItem, Trailing),
    /// which owns it; this object follows it by QPointer and shows it only for a kept proof.
    QToolButton* createStatusItem(QWidget* parent);

    /// The created status item (nullptr = none or deleted)
    QToolButton* getStatusItem() const { return m_statusItem.data(); }

    /// Text of the status item ("Soft proof: Coated FOGRA39")
    QString getStatusText() const;

    /// The state of the view (nullptr = none yet)
    static PDFAppDocumentSoftProof* findSoftProof(const PDFAppDocumentView* view);

    /// The state of the view, created at the first call (a child of the view, its item in the status bar of
    /// the view; the view applies a change from the event loop). nullptr for a null view.
    static PDFAppDocumentSoftProof* getSoftProof(PDFAppDocumentView* view);

    /// The color management of \p manager without the soft proof (a shared immutable copy for a run of a tool,
    /// F12; Q7.10): the current one of the manager, when it has no proof, else a new one of its settings
    /// withoutProofing with the output intents of \p document. GUI thread only (it may read the profiles).
    static PDFCMSPointer createCMSWithoutProofing(const PDFCMSManager* manager, const PDFDocument* document);

Q_SIGNALS:
    /// The state has changed (GUI thread; emitted last)
    void changed();

private:
    void updateStatusItem();
    void onChanged();
    void onStatusItemClicked();
    void onStatusItemDestroyed();
    void setStateImpl(const State& state);

    State m_state;
    QPointer<QToolButton> m_statusItem;
    QPointer<PDFAppDocumentView> m_view;    ///< The view of getSoftProof (the parent), null in its destruction
};

}   // namespace pdf

#endif // PDFAPPDOCUMENTSOFTPROOF_H
