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

#ifndef PDFAPPTOOLGUI_STRUCTURE_H
#define PDFAPPTOOLGUI_STRUCTURE_H

#include "pdfappdeclarativetoolgui.h"
#include "pdfapptoolpanelparts.h"
#include "pdfdocumenttool_structure.h"
#include "pdfdocumenttoolshell.h"

#include <QPointer>
#include <QToolButton>

#include <optional>
#include <vector>

namespace pdf
{

/// The tool Optimize Structure in the shell (optimize.structure, Figma 302:4072, 303:4405,
/// 303:4560, 303:4897; package 2C of the plan of the first batch). A thin tool: the menu Steps
/// with Select all and Clear, the panel Document / Steps to run / Result, the chip Lossless over
/// the canvas; after a run the panel shows the result of the dialog (Result, Steps performed and
/// Copy report).
///
/// Ownership: the shell owns this GUI; the parts are children of the toolbar, of the right
/// panel and of the canvas area (the chip, deleted by the shell) and are held in QPointer -
/// they are made again after a change of the language. The result shown while the dialog is
/// open is a value of this GUI.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolGUIStructure : public PDFAppDeclarativeToolGUI
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDeclarativeToolGUI;

public:
    /// panel-steps 303:4560
    static constexpr int STEPS_PANEL_WIDTH = 520;

    explicit PDFAppToolGUIStructure(QObject* parent = nullptr);
    virtual ~PDFAppToolGUIStructure() override;

    /// Registers what is not registered yet: the tool in PDFDocumentToolRegistry and its
    /// GUI in PDFDocumentToolGUIRegistry (PDFAppBuiltinTools::registerAll)
    static void registerTools();

    /// Number of the steps, which are on
    int getCheckedStepCount() const;

    /// The panel shows the result of the open dialog (303:4897)
    bool isShowingResult() const { return m_result.has_value(); }

    /// Parts (nullptr = none)
    PDFAppToolFlagsMenu* getStepsMenu() const { return m_stepsMenu.data(); }
    PDFTextLink* getRestoreDefaultsLink() const { return m_restoreLink.data(); }
    QToolButton* getSelectAllButton() const { return m_selectAllButton.data(); }
    QToolButton* getClearButton() const { return m_clearButton.data(); }
    PDFAppToolSectionHeader* getDocumentHeader() const { return m_documentHeader.data(); }
    PDFKeyValueList* getDocumentFacts() const { return m_documentFacts.data(); }
    PDFAppToolSectionHeader* getStepsHeader() const { return m_stepsHeader.data(); }
    PDFAppToolStepList* getStepList() const { return m_stepList.data(); }
    PDFAppToolSectionHeader* getResultHeader() const { return m_resultHeader.data(); }
    PDFAppParagraph* getResultText() const { return m_resultText.data(); }
    PDFAppToolSectionHeader* getDoneResultHeader() const { return m_doneResultHeader.data(); }
    PDFKeyValueList* getResultRows() const { return m_resultRows.data(); }
    PDFAppToolSectionHeader* getPerformedHeader() const { return m_performedHeader.data(); }
    PDFTextLink* getCopyReportLink() const { return m_reportLink.data(); }
    PDFAppToolStepList* getPerformedList() const { return m_performedList.data(); }
    PDFAppToolCanvasChip* getChip() const { return m_chip.data(); }

    virtual QWidget* createOverlay(QWidget* canvasArea) override;
    virtual int getRightPanelSpacing() const override;
    virtual QString getInitialSidebarPanel() const override;
    virtual QString getConfirmIconName() const override;
    virtual bool isConfirmEnabled() const override;
    virtual QString getStatusDetailText() const override;
    virtual void onParametersChanged(const QString& name) override;
    virtual void onRunNotAccepted(const PDFDocumentToolResult& result) override;
    virtual void onDocumentChanged() override;
    virtual void fillResultInput(PDFAppToolResultInput* input) const override;

protected:
    virtual void describeToolbar(PDFAppToolPanelBuilder& builder) override;
    virtual void describeRightPanel(PDFAppToolPanelBuilder& builder) override;

private:
    /// Rows of the panel Steps (the texts of the parameters, 303:4560) and of the right panel (319:7288)
    std::vector<PDFAppToolFlagItem> getMenuItems() const;
    static std::vector<PDFAppToolFlagItem> getStepItems();

    void updateCounts();
    void updateParts();
    void updateDocumentFacts();

    void selectAllSteps();
    void clearSteps();
    void onCopyReportClicked();
    void onShellStateChanged(pdf::PDFDocumentToolShell::State state);

    PDFDocumentToolStructure::Counts m_counts;
    std::optional<PDFDocumentToolResult> m_result;                  ///< The result of the open dialog
    mutable std::optional<PDFDocumentToolResult> m_dialogResult;    ///< The result of the dialog, which is opening
    QString m_notReducedText;                                       ///< The last run did not make the file smaller

    QPointer<PDFAppToolFlagsMenu> m_stepsMenu;
    QPointer<PDFTextLink> m_restoreLink;
    QPointer<QToolButton> m_selectAllButton;
    QPointer<QToolButton> m_clearButton;
    QPointer<QWidget> m_toolActions;
    QPointer<PDFAppToolSectionHeader> m_documentHeader;
    QPointer<PDFKeyValueList> m_documentFacts;
    QPointer<PDFAppToolSectionHeader> m_stepsHeader;
    QPointer<PDFAppToolStepList> m_stepList;
    QPointer<PDFAppToolSectionHeader> m_resultHeader;
    QPointer<PDFAppParagraph> m_resultText;
    QPointer<PDFAppToolSectionHeader> m_doneResultHeader;
    QPointer<PDFKeyValueList> m_resultRows;
    QPointer<PDFAppToolSectionHeader> m_performedHeader;
    QPointer<PDFTextLink> m_reportLink;
    QPointer<PDFAppToolStepList> m_performedList;
    QPointer<PDFAppToolCanvasChip> m_chip;
};

}   // namespace pdf

#endif // PDFAPPTOOLGUI_STRUCTURE_H
