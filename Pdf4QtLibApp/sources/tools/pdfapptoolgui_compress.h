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

#ifndef PDFAPPTOOLGUI_COMPRESS_H
#define PDFAPPTOOLGUI_COMPRESS_H

#include "pdfappdeclarativetoolgui.h"
#include "pdfappdocumentmenus.h"
#include "pdfappsettingcontrol.h"
#include "pdfapptoolbeforeafteroverlay.h"
#include "pdfdocumenttool.h"
#include "pdfdocumenttoolshell.h"

#include <QPointer>
#include <QPushButton>
#include <QToolButton>

#include <optional>
#include <vector>

namespace pdf
{

/// Panel Custom… of Compress (panel-custom 294:1787: 360 wide, paddings 16 + the border,
/// gap 14, radius 12). Its controls are bound to the parameters by PDFAppSettingControl and
/// the stores of the parameter model. It lies by K13: x 176 from the leading edge of the
/// segments, 4 px below the toolbar, inside of the canvas in a narrower window. The
/// panel is a child of the actions of the toolbar and goes with them.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolCompressCustomPanel : public PDFAppDocumentPanelPopup
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDocumentPanelPopup;

public:
    static constexpr int PANEL_WIDTH = 360;
    static constexpr int SEGMENTS_OFFSET = 176;     ///< x 509 of the frame from the segments at 333

    /// \param gui GUI of the tool: its model, host and context
    /// \param parent Parent widget
    explicit PDFAppToolCompressCustomPanel(PDFDocumentToolGUI* gui, QWidget* parent);
    virtual ~PDFAppToolCompressCustomPanel() override;

    /// Binder of the parameter, nullptr = none
    PDFAppSettingControl* getControl(const QString& parameter) const;

    virtual int getPanelLeft(const QRect& anchorRect, int panelWidth) const override;

private:
    QString getParameterTitle(const char* parameter) const;
    void addCaption(const QString& text);
    void addRow(const QString& labelText, QWidget* control, int height);
    PDFAppSettingControl* addControl(PDFAppSettingControl* control);
    PDFAppSettingControl* createCheckBox(const QString& parameter);

    QPointer<PDFDocumentToolGUI> m_gui;
    std::vector<QPointer<PDFAppSettingControl>> m_controls;
};

/// The tool Compress in the shell (optimize.compress, Figma 291:994, 294:1340, 295:1711,
/// 295:2182, 296:2761, 316:7293). What it has of its own: the panel of the measured result,
/// the panel Custom… and the overlay before / after over the canvas.
///
/// The measuring (owner answer Q1 in the wording of F1) is the preview of the runner: it
/// starts after the opening and after a change of a preset, a change cancels it, its
/// result is the result of the tool. The state of the measuring is one value; the
/// parts are built again after a change of the language, each of them shows it by
/// \p updateParts.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolGUICompress : public PDFAppDeclarativeToolGUI
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDeclarativeToolGUI;

public:
    /// State of the measuring of the current parameters
    enum class MeasureState
    {
        Measuring,      ///< The preview computes or waits for its delay (no frame, Q1)
        Stopped,        ///< Stop of a run: no result, the next confirmation computes again
        Measured,       ///< The result of the parameters is known (291:994)
        NotReduced,     ///< The file does not get smaller (295:2182)
        Failed          ///< The tool cannot measure the document (no frame, chapter 3: an error in the card)
    };

    explicit PDFAppToolGUICompress(QObject* parent = nullptr);
    virtual ~PDFAppToolGUICompress() override;

    /// Registers what is not registered yet: the tool in PDFDocumentToolRegistry and its
    /// GUI in PDFDocumentToolGUIRegistry (PDFAppBuiltinTools::registerAll)
    static void registerTools();

    MeasureState getMeasureState() const { return m_measureState; }

    /// Parts (nullptr = none)
    PDFAppToolCompressCustomPanel* getCustomPanel() const { return m_customPanel.data(); }
    PDFAppToolBeforeAfterOverlay* getBeforeAfterOverlay() const { return m_overlay.data(); }
    PDFAppSettingControl* getPresetControl() const { return m_presetControl.data(); }
    PDFAppToolSectionHeader* getResultHeader() const { return m_resultHeader.data(); }
    PDFAppToolMeasuredCard* getMeasuredCard() const { return m_measuredCard.data(); }
    PDFAppToolProgressCard* getMeasuringCard() const { return m_measuringCard.data(); }
    PDFAppToolCallout* getNotReducedCallout() const { return m_notReducedCallout.data(); }
    QPushButton* getTrySmallerButton() const { return m_trySmallerButton.data(); }
    PDFAppToolSectionHeader* getChangesHeader() const { return m_changesHeader.data(); }
    PDFKeyValueList* getChangeRows() const { return m_changeRows.data(); }
    PDFAppParagraph* getPresetNote() const { return m_presetNote.data(); }

    /// Opens the panel Custom… below the toolbar (from a click on the segment)
    void openCustomPanel();

    virtual QWidget* createOverlay(QWidget* canvasArea) override;
    virtual int getRightPanelSpacing() const override;
    virtual QString getInitialSidebarPanel() const override;
    virtual PDFAppToolProgressPlace getProgressPlace() const override;
    virtual void setContentEnabled(bool isEnabled) override;
    virtual bool isConfirmEnabled() const override;
    virtual QString getRunningStatusText(const QString& stepText) const override;
    virtual QString getRunningTitle(int percentage) const override;
    virtual void onParametersChanged(const QString& name) override;
    virtual void onPreviewChanged(const PDFDocumentToolResult* preview) override;
    virtual void onPreviewProgress(int percentage, const QString& stepText) override;
    virtual void onRunStateChanged(bool isRunning) override;
    virtual void onRunNotAccepted(const PDFDocumentToolResult& result) override;
    virtual void onDocumentChanged() override;
    virtual void fillResultInput(PDFAppToolResultInput* input) const override;

protected:
    virtual void describeToolbar(PDFAppToolPanelBuilder& builder) override;
    virtual void describeRightPanel(PDFAppToolPanelBuilder& builder) override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void updateParts();
    void updateOverlay();
    void updateChangeRows();
    void setMeasureState(MeasureState state);
    void updateSegmentsEnabled();

    /// Size of the file, which the document has now, -1 = no file
    qint64 getCurrentFileSize() const;

    void openCustomPanelByMouse();
    void onTrySmallerClicked();
    void onCustomPanelClosed();
    void onShellStateChanged(pdf::PDFDocumentToolShell::State state);

    MeasureState m_measureState = MeasureState::Measuring;
    std::optional<PDFDocumentToolResult> m_result;  ///< The measured result of the current parameters
    mutable std::optional<PDFDocumentToolResult> m_dialogResult;    ///< The result of the dialog, which is opening (a run, which has joined the preview)
    QString m_errorText;
    int m_percentage = 0;
    QString m_stepText;
    bool m_isRunning = false;
    bool m_isContentEnabled = true;

    QPointer<PDFAppSettingControl> m_presetControl;
    QPointer<QToolButton> m_customSegment;
    QPointer<PDFAppToolCompressCustomPanel> m_customPanel;
    QPointer<PDFAppToolBeforeAfterOverlay> m_overlay;
    QPointer<PDFAppToolSectionHeader> m_resultHeader;
    QPointer<PDFAppToolMeasuredCard> m_measuredCard;
    QPointer<PDFAppToolProgressCard> m_measuringCard;
    QPointer<PDFAppToolCallout> m_notReducedCallout;
    QPointer<QPushButton> m_trySmallerButton;
    QPointer<PDFAppToolSectionHeader> m_changesHeader;
    QPointer<PDFKeyValueList> m_changeRows;
    QPointer<PDFAppParagraph> m_presetNote;
    QPointer<QWidget> m_changesCard;
    QBoxLayout* m_resultSectionLayout = nullptr;    ///< Layout of the section Result (owned by the content of the panel)
    QPointer<QWidget> m_rightContent;
    bool m_isCustomPanelOpen = false;               ///< Open, `closed` has not come yet
    bool m_isCustomPressClosing = false;            ///< The press on the segment has closed the panel: its release does not open it
};

}   // namespace pdf

#endif // PDFAPPTOOLGUI_COMPRESS_H
