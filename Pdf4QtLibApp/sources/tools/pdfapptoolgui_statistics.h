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

#ifndef PDFAPPTOOLGUI_STATISTICS_H
#define PDFAPPTOOLGUI_STATISTICS_H

#include "pdfappdeclarativetoolgui.h"
#include "pdfappstatisticstable.h"
#include "pdfapptoolpanelbuilder.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfappworkspace.h"
#include "pdfdocumenttooltablewriter.h"

#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"

#include <QPointer>

#include <map>
#include <optional>

class QToolButton;

namespace pdf
{

/// The GUI of Document Statistics (plan of step 7 of phase 2, 4.2; Figma 480:14219 … 480:15753): only Close (no
/// Primary button, isConfirmShown false); the toolbar View (By Function / By Object Type, 480:14833) and Export
/// (CSV / Text / HTML / XML, 497:20172; CSV the current view, K7.20); the left panel Document with the card File
/// (size, pages, version, objects), the canvas the stacked bar and the table (PDFAppStatisticsTable), the right
/// panel Selected. The statistics are the final preview of the core tool: computed at the entry and after every
/// change of the document - the runner waits PDFDocumentToolRunner::PREVIEW_DELAY after the last change and
/// cancels a running computation (R7-22 (7)); the preview does not depend on the parameters (the view only
/// chooses the table shown), so a change of the view never computes again. While the statistics are computed,
/// the canvas says "Analyzing the document…" with the card of the progress (480:15218). The export is
/// PDFDocumentToolGUI::openExport with PDFDocumentToolStatistics::writeOutput (PDFAppToolValueFormatter; the
/// data of the shown result, never a new run - plan 4.1).
///
/// Ownership: the shell owns this GUI; its widgets belong to the panels and the canvas of the shell (QPointer
/// here, rebuilt by the descriptions after a change of the language).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolGUIStatistics : public PDFAppDeclarativeToolGUI
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDeclarativeToolGUI;

public:
    /// Widths of the panels of the toolbar (480:15170 330, 497:20433 320)
    static constexpr int VIEW_MENU_WIDTH = 330;
    static constexpr int EXPORT_MENU_WIDTH = 320;

    explicit PDFAppToolGUIStatistics(QObject* parent = nullptr);
    virtual ~PDFAppToolGUIStatistics() override;

    /// Registers the core tool and this GUI (PDFAppBuiltinTools::registerAll)
    static void registerStatisticsTools();

    virtual QWidget* createCanvasPage(QWidget* parent) override;
    virtual bool isConfirmShown() const override { return false; }

    /// The session is the use (owner 2026-11-20): a view, a selected row, an export, or 10 s shown
    virtual bool isInteractive() const override { return true; }
    virtual QString getCancelText() const override;
    virtual QString getStatusText() const override;
    virtual bool hasOwnStatusItems() const override { return true; }    ///< K7.27: no page, no zoom item
    virtual QVariantMap getPreviewParameters() const override;
    virtual void fillResultInput(PDFAppToolResultInput* input) const override;
    virtual void onParametersChanged(const QString& name) override;
    virtual void onPreviewChanged(const PDFDocumentToolResult* preview) override;
    virtual void onPreviewProgress(int percentage, const QString& stepText) override;
    virtual void onDocumentChanged() override;
    virtual void onExportDecided(const PDFAppToolResultOutcome& outcome) override;

    /// The statistics are shown (a successful preview of the version of now)
    bool hasStatistics() const { return m_statistics.has_value(); }
    const std::optional<PDFDocumentToolResult>& getStatistics() const { return m_statistics; }

    /// Opens the dialog of the export of the shown statistics (the rows of the menu Export). \returns the dialog
    /// was opened; false without statistics, when the file cannot be written (the reason goes to the status bar)
    bool exportStatistics(PDFDocumentToolTableFormat format);

    /// The chosen view of the parameters ("function", "type")
    QString getView() const;

    // Parts (tests; nullptr = none)
    PDFAppStatisticsTable* getTable() const { return m_table.data(); }
    PDFAppToolCanvasMessage* getCanvasMessage() const { return m_message.data(); }
    PDFAppToolEnumMenu* getViewMenu() const { return m_viewMenu.data(); }
    QToolButton* getViewButton() const;
    QToolButton* getExportButton() const;
    PDFTileRow* getExportRow(PDFDocumentToolTableFormat format) const;
    PDFAppToolPanelHeader* getDocumentHeader() const { return m_documentHeader.data(); }
    PDFKeyValueList* getFileRows() const { return m_fileRows.data(); }
    PDFAppToolSectionHeader* getSelectedHeader() const { return m_selectedHeader.data(); }
    PDFKeyValueList* getSelectedRows() const { return m_selectedRows.data(); }
    PDFAppParagraph* getSelectedNote() const { return m_selectedNote.data(); }
    PDFAppToolProgressCard* getProgressCard() const { return m_progressCard.data(); }

    /// The texts of a format of the export ("CSV", "Text", "HTML", "XML")
    static QString getFormatTitle(PDFDocumentToolTableFormat format);

protected:
    virtual void describeToolbar(PDFAppToolPanelBuilder& builder) override;
    virtual void describeLeftPanel(PDFAppToolPanelBuilder& builder) override;
    virtual void describeRightPanel(PDFAppToolPanelBuilder& builder) override;

private:
    void updateAll();
    void updateButtons();
    bool updateCanvas();    ///< false = this GUI is gone (a slot of a signal of the parts deleted it)
    void updateFileRows();
    void updateSelectedRows();
    void onSelectedRowChanged(int row);
    void onExportRowClicked();
    QString getBaseName() const;

    std::optional<PDFDocumentToolResult> m_statistics;  ///< The shown statistics (the last successful preview)
    QString m_errorMessage;                             ///< The preview of the version of now failed
    int m_percentage = 0;                               ///< Progress of the computation
    QString m_stepText;
    PDFDocumentToolTableFormat m_exportFormat = PDFDocumentToolTableFormat::None;   ///< The format of the open export
    bool m_isUpdating = false;                          ///< updateAll runs: the chosen row of the table does not say the status

    QPointer<PDFAppToolEnumMenu> m_viewMenu;
    QPointer<PDFAppToolPanelPopup> m_exportMenu;
    std::map<PDFDocumentToolTableFormat, QPointer<PDFTileRow>> m_exportRows;
    QPointer<PDFAppToolPanelHeader> m_documentHeader;
    QPointer<PDFKeyValueList> m_fileRows;
    QPointer<PDFAppToolProgressCard> m_progressCard;
    QPointer<PDFAppToolSectionHeader> m_selectedHeader;
    QPointer<PDFKeyValueList> m_selectedRows;
    QPointer<PDFAppParagraph> m_selectedNote;
    QPointer<QWidget> m_canvasPage;
    QPointer<PDFAppStatisticsTable> m_table;
    QPointer<PDFAppToolCanvasMessage> m_message;
};

}   // namespace pdf

#endif // PDFAPPTOOLGUI_STATISTICS_H
