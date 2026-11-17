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

#ifndef PDFAPPTOOLGUI_SOFTPROOF_H
#define PDFAPPTOOLGUI_SOFTPROOF_H

#include "pdfappdeclarativetoolgui.h"
#include "pdfappdocumentsoftproof.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfappworkspace.h"

#include "pdfcms.h"
#include "pdfcomponentpanels.h"
#include "pdffluentwidgets.h"

#include <QColor>
#include <QPointer>
#include <QWidget>

class QToolButton;

namespace pdf
{

class PDFAppDocumentView;
class PDFAppParagraph;
class PDFAppThumbnailsPanel;
class PDFAppToolCallout;
class PDFAppToolPagePicture;
class PDFAppToolPanelPopup;
class PDFColorSwatchRow;
class PDFKeyValueList;
class PDFLabel;
class PDFTileRow;

/// A color of a card of a right panel (color-Alarm color 497:19687 of Soft Proofing, the alarm colors of the
/// warnings of Output Preview 474:9818, 474:10178): the label Regular 12 text/secondary with the name of the
/// color Regular 11 text/tertiary at the trailing edge, 6 px below them the inline swatches (black, white,
/// red, orange, yellow, green, blue and "+" for a custom color, 1567:19183). A choice of the user emits
/// colorSelected; setColor never emits.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppProofColorField : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    static constexpr int GAP = 6;

    explicit PDFAppProofColorField(const QString& label, QWidget* parent);
    virtual ~PDFAppProofColorField() override;

    /// The shown color (no signal)
    void setColor(const QColor& color);

    PDFColorSwatchRow* getSwatchRow() const { return m_row.data(); }
    PDFLabel* getNameLabel() const { return m_name.data(); }

Q_SIGNALS:
    /// The user has chosen a color (emitted last)
    void colorSelected(const QColor& color);

private:
    void onColorSelected(const QColor& color);

    QPointer<PDFLabel> m_name;
    QPointer<PDFColorSwatchRow> m_row;
};

/// The canvas of Soft Proofing (document-canvas 479:11760): bg/track, padding 20, the bar of 36 ("Page 3 ·
/// proof of Coated FOGRA39" Semi Bold 13, "Gamut checking on" Regular 12 text/tertiary at the trailing edge),
/// gap 12, the page as the document view draws it - its compiled page, so the proof of the CMS of the view is
/// what is shown (PDFAppToolPagePicture, fitted). The texts are set by the GUI.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSoftProofCanvasPage : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    static constexpr int PADDING = 20;
    static constexpr int GAP = 12;
    static constexpr int BAR_HEIGHT = 36;

    explicit PDFAppSoftProofCanvasPage(QWidget* parent);
    virtual ~PDFAppSoftProofCanvasPage() override;

    void setTexts(const QString& title, const QString& hint);

    PDFLabel* getTitleLabel() const { return m_title.data(); }
    PDFLabel* getHintLabel() const { return m_hint.data(); }
    PDFAppToolPagePicture* getPicture() const;

protected:
    virtual void paintEvent(QPaintEvent* event) override;

private:
    QPointer<PDFLabel> m_title;
    QPointer<PDFLabel> m_hint;
    QPointer<PDFAppToolPagePicture> m_picture;
};

/// The GUI of Soft Proofing (plan of step 7 of phase 2, 4.7; Figma 479:11760 … 485:17529): the switches Soft
/// Proof and Gamut Check (labeled Toolbar menu buttons, Open = on, as the toggles of Redact - owner 17. 11.),
/// the menus of the profile (the output intents of the document and the CMYK profiles with the reasons of the
/// unavailable ones, FP-6, no built-in profiles - Q7.11) and of the intent (497:19614), Close and Keep Proofing
/// On (Disabled, when the proof is off - K7.9, designer 17. 11.); the thumbnails on the left; the canvas is the
/// current page as the view of the tab draws it with its proof; the right panel Proofing and Out of gamut. Keep
/// Proofing On keeps the state after the tool closes with the item of the status bar "Soft proof: <profile>"
/// (485:17529, a click opens the tool); Close without it gives the state back. Nothing of the proof reaches the
/// printing, the exports or the previews (Q7.10). No CMYK profile: 479:14751; the Generic engine: 479:15335.
///
/// The profile, the intent and the alarm color are the parameters of the tool (their last values are
/// remembered, plan 4.7); the switches are the state of the tab only (never saved).
///
/// Keep Proofing On is the Primary button and it never runs the tool (the GUI shows the proof itself; the run
/// of the core tool is the measurement of MCP and PdfTool): the shell gives the press to confirmWithoutRun
/// (RS7APP L4), which keeps the proof and closes the tool from the event loop - no run, no environment.
///
/// Ownership: the shell owns this GUI and its parts; the state of the proof belongs to the view of the tab
/// (PDFAppDocumentSoftProof::getSoftProof), the GUI holds it by QPointer.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolGUISoftProof : public PDFAppDeclarativeToolGUI
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDeclarativeToolGUI;

public:
    explicit PDFAppToolGUISoftProof(QObject* parent = nullptr);
    virtual ~PDFAppToolGUISoftProof() override;

    /// Registers the core tool and this GUI (PDFAppBuiltinTools::registerAll)
    static void registerSoftProofTools();

    /// Why the proof cannot be shown
    enum class Availability
    {
        Available,      ///< A CMYK profile and Little CMS 2
        NoProfile,      ///< No usable CMYK profile (479:14751)
        GenericEngine   ///< The Generic color engine of Settings (479:15335)
    };

    virtual QWidget* createCanvasPage(QWidget* parent) override;
    virtual QString getStatusText() const override;
    virtual QString getCancelText() const override;
    virtual QString getConfirmText() const override;
    virtual QString getConfirmIconName() const override;
    virtual bool isConfirmEnabled() const override;
    virtual bool confirmWithoutRun() override;
    virtual void onParametersChanged(const QString& name) override;
    virtual void onDocumentChanged() override;

    /// The state of the proof of the tab (nullptr before the entry or after the view)
    PDFAppDocumentSoftProof* getSoftProof() const { return m_softProof.data(); }

    /// What can be shown with the settings and the profiles of now
    Availability getAvailability() const;

    /// The profiles of the menu (the output intents of the document first)
    PDFSoftProofingProfiles getProfiles() const;

    /// The name of the profile of the proof for the user (the description or the file name), empty = none
    QString getProfileTitle() const;

    /// Parts (tests; nullptr = none)
    QToolButton* getProofButton() const { return m_proofButton.data(); }
    QToolButton* getGamutButton() const { return m_gamutButton.data(); }
    PDFAppToolPanelPopup* getProfilePanel() const { return m_profilePanel.data(); }
    PDFAppToolPanelPopup* getIntentPanel() const { return m_intentPanel.data(); }
    PDFAppSoftProofCanvasPage* getCanvasPage() const { return m_canvasPage.data(); }
    PDFAppToolCallout* getCallout() const { return m_callout.data(); }
    PDFKeyValueList* getProofingRows() const { return m_proofingRows.data(); }
    PDFAppProofColorField* getAlarmField() const { return m_alarmField.data(); }
    PDFAppParagraph* getGamutText() const { return m_gamutText.data(); }
    PDFAppThumbnailsPanel* getThumbnailsPanel() const;

    /// Short and full names of an intent (the card "Relative", the menu "Relative colorimetric")
    static QString getIntentShortName(RenderingIntent intent);
    static QString getIntentName(RenderingIntent intent);

    /// The intent of an identifier of the parameter "intent" and back
    static RenderingIntent getIntent(const QString& id);
    static QString getIntentId(RenderingIntent intent);

protected:
    virtual void describeToolbar(PDFAppToolPanelBuilder& builder) override;
    virtual void describeRightPanel(PDFAppToolPanelBuilder& builder) override;
    virtual void describeLeftPanel(PDFAppToolPanelBuilder& builder) override;

private:
    void ensureSoftProof();
    void applyParameters();
    void updateParts();
    void onProofClicked();
    void onGamutClicked();
    void onProfileRowClicked();
    void onIntentRowClicked();
    void onAlarmColorSelected(const QColor& color);
    void onSoftProofChanged();
    void onDisplayedPageChanged(pdf::PDFInteger pageIndex);
    void onKeepQueued();

    PDFAppDocumentView* getView() const;
    QString getEffectiveProfileId() const;
    RenderingIntent getModelIntent() const;
    QColor getModelAlarmColor() const;

    QPointer<PDFAppDocumentSoftProof> m_softProof;
    QPointer<PDFAppDocumentView> m_proofView;       ///< The view of the state (null in its destruction: nothing is given back then)
    PDFAppDocumentSoftProof::State m_entryState;    ///< The state at the entry: Close without Keep Proofing On gives it back
    bool m_isEntered = false;
    bool m_isKept = false;                          ///< Keep Proofing On was pressed: the state stays at the close
    bool m_isKeepRequested = false;                 ///< The Primary button was pressed (confirmWithoutRun), the close is queued

    QPointer<QToolButton> m_proofButton;
    QPointer<QToolButton> m_gamutButton;
    QPointer<PDFAppToolPanelPopup> m_profilePanel;
    QPointer<PDFAppToolPanelPopup> m_intentPanel;
    QPointer<PDFAppSoftProofCanvasPage> m_canvasPage;
    QPointer<PDFAppToolCallout> m_callout;
    QPointer<PDFKeyValueList> m_proofingRows;
    QPointer<PDFAppProofColorField> m_alarmField;
    QPointer<PDFAppParagraph> m_gamutText;
    QPointer<PDFAppThumbnailsPanel> m_thumbnails;
    QPointer<PDFAppDocumentView> m_connectedView;
};

}   // namespace pdf

#endif // PDFAPPTOOLGUI_SOFTPROOF_H
