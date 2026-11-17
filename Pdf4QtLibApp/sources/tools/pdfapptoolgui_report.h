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

#ifndef PDFAPPTOOLGUI_REPORT_H
#define PDFAPPTOOLGUI_REPORT_H

#include "pdfappdeclarativetoolgui.h"
#include "pdfappreportpreview.h"
#include "pdfappstatisticstable.h"
#include "pdfapptoolpanelbuilder.h"
#include "pdfapptoolpanelparts.h"
#include "pdfappworkspace.h"
#include "pdfdocumentreport.h"
#include "pdfdocumentxmlexport.h"

#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"

#include <QCheckBox>
#include <QFutureWatcher>
#include <QJsonObject>
#include <QPointer>
#include <QScrollArea>
#include <QTimer>

#include <map>
#include <memory>
#include <optional>

class QToolButton;

namespace pdf
{

class PDFAppCancelFlag;

/// The GUI of Document Report (plan of step 7 of phase 2, 4.4; Figma 484:15472 … 484:20937): the toolbar Kind
/// (Report / Structure as XML, 484:15916), Format (Text / HTML / XML, 484:16401), Dates (Short / Long / ISO / RFC
/// 2822, 497:20544), Close and Create Report / Export XML; the left panel the nine sections (a check box writes
/// the parameter, a click on the row shows its section in the preview; a section the document has nothing of is
/// Disabled with its reason - PDFDocumentReport::getEmptySectionReason, FD-8) and the page selection; for XML the
/// description and the callout "The file can be large" (484:18196); the canvas the preview (PDFAppReportPreview)
/// of the data of the chosen sections - computed by one job (QtConcurrent::run, PDFDocumentReport::createData with
/// a PDFAppCancelFlag; the model is built in the GUI thread by createModel with PDFAppToolValueFormatter), a new
/// request after a short rest cancels the old job; the XML preview the first lines of PDFDocumentXmlExport; the
/// right panel Output (and the four XML options). Create Report runs the core tool: its file opens the dialog of
/// the export (484:18811, R7-13 FileOnly with "Open the report when it is saved"); the tool keeps its result
/// (Create Report Disabled until the document or a setting changes, R7-2). JavaScript, XMP and the data of the
/// streams are an extraction (Q7.2): the run asks bit 5 by getRunPermissions (the owner password offered).
///
/// Ownership: the shell owns this GUI; the watcher and the timer are its children, the widgets belong to the
/// panels and the canvas of the shell (QPointer here). A job owns only copies (the shared immutable document).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolGUIReport : public PDFAppDeclarativeToolGUI
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDeclarativeToolGUI;

public:
    /// Widths of the panels of the toolbar (484:15916, 484:16401, 497:20544)
    static constexpr int KIND_MENU_WIDTH = 340;
    static constexpr int FORMAT_MENU_WIDTH = 320;
    static constexpr int DATE_MENU_WIDTH = 330;

    /// Rest after a change before the preview is computed again (ms)
    static constexpr int PREVIEW_DELAY = 300;

    /// The first bytes of the XML shown in the preview (484:18196 "Preview of the first lines")
    static constexpr qint64 XML_PREVIEW_BYTES = 16 * 1024;
    static constexpr int XML_PREVIEW_LINES = 40;

    /// What the job learns about the sections of the document (the descriptions and the Disabled rows of the
    /// left panel, 484:15472, 484:20505)
    class Facts
    {
    public:
        int scriptCount = -1;           ///< "4 scripts in the document"
        int destinationCount = -1;      ///< "18 destinations"
        int processInkCount = -1;       ///< "4 process, 2 spot"
        int spotInkCount = -1;
        qint64 objectCount = -1;        ///< The objects of the storage (Structure as XML, 484:18196)
        std::map<PDFDocumentReportSection, QString> emptyReasons;   ///< A section the document has nothing of
    };

    /// Opens a saved report (the check box "Open the report when it is saved"); tests replace it
    using FileOpener = bool (*)(const QString& filePath);

    explicit PDFAppToolGUIReport(QObject* parent = nullptr);
    virtual ~PDFAppToolGUIReport() override;

    /// Registers the core tool and this GUI (PDFAppBuiltinTools::registerAll)
    static void registerReportTools();

    /// The opener of the saved reports (nullptr = the program of the system)
    static void setFileOpener(FileOpener opener);

    virtual QWidget* createCanvasPage(QWidget* parent) override;
    virtual QString getCancelText() const override;
    virtual QString getConfirmText() const override;
    virtual QString getConfirmIconName() const override;
    virtual bool isConfirmEnabled() const override;
    virtual QString getStatusText() const override;
    virtual bool hasOwnStatusItems() const override { return true; }    ///< K7.27: no page, no zoom item
    virtual QString getRunningStatusText(const QString& stepText) const override;
    virtual QString getRunningTitle(int percentage) const override;
    virtual std::vector<PDFSecurityHandler::Permission> getRunPermissions() const override;
    virtual QVariantMap getRunParameters() const override;
    virtual void fillResultInput(PDFAppToolResultInput* input) const override;
    virtual bool isResultKeptInTool() const override { return true; }
    virtual void onResultKept(const PDFDocumentToolResult& result) override;
    virtual bool isResultStaleBy(const QString& parameterName) const override;
    virtual QString getStaleTitle() const override;
    virtual QString getStaleText() const override;
    virtual void onExportDecided(const PDFAppToolResultOutcome& outcome) override;
    virtual void onParametersChanged(const QString& name) override;
    virtual void onRunStateChanged(bool isRunning) override;
    virtual void onDocumentChanged() override;

    /// The kind of the parameters is Structure as XML
    bool isStructureXml() const;

    /// The sections, which the report has: checked and available
    std::vector<PDFDocumentReportSection> getIncludedSections() const;

    /// The section is available in the document (no reason of an empty section)
    bool isSectionAvailable(PDFDocumentReportSection section) const;

    /// The row of the section, which the preview shows (a click on a row)
    PDFDocumentReportSection getCurrentSection() const { return m_currentSection; }
    void setCurrentSection(PDFDocumentReportSection section);

    /// The preview of the parameters of now is being computed
    bool isPreviewPending() const;

    /// The facts of the document of now (std::nullopt until the first job has ended)
    const std::optional<Facts>& getFacts() const { return m_facts; }

    /// The data of the shown preview
    const QJsonObject& getPreviewData() const { return m_previewData; }

    // Parts (tests; nullptr = none)
    PDFAppToolEnumMenu* getKindMenu() const { return m_kindMenu.data(); }
    PDFAppToolEnumMenu* getFormatMenu() const { return m_formatMenu.data(); }
    PDFAppToolEnumMenu* getDateMenu() const { return m_dateMenu.data(); }
    PDFAppToolPanelHeader* getSectionsHeader() const { return m_sectionsHeader.data(); }
    PDFTileRow* getSectionRow(PDFDocumentReportSection section) const;
    PDFPageSelection* getPageSelection() const { return m_pageSelection.data(); }
    PDFAppToolPanelHeader* getXmlHeader() const { return m_xmlHeader.data(); }
    PDFAppToolCallout* getXmlCallout() const { return m_xmlCallout.data(); }
    PDFKeyValueList* getOutputRows() const { return m_outputRows.data(); }
    QCheckBox* getCharacterMapsCheckBox() const { return m_characterMapsCheckBox.data(); }
    QCheckBox* getXmlOption(const char* parameter) const;
    PDFAppReportPreview* getPreview() const { return m_preview.data(); }
    PDFAppToolCanvasMessage* getCanvasMessage() const { return m_message.data(); }

    /// The title of a section ("Basic information"), its icon (484:15472) and its description
    static QString getSectionTitle(PDFDocumentReportSection section);
    static QString getSectionIconName(PDFDocumentReportSection section);

    /// The parameters of the XML options for a choice of the streams of 484:18196: the two check boxes "Export
    /// streams as hexadecimal data" and "Export streams as text, if possible" are one choice - none, hex, text
    static QVariantMap getStreamValues(bool isHex, bool isText);

    /// The first lines of the XML of \p document for the preview (484:18196): at most XML_PREVIEW_BYTES and
    /// XML_PREVIEW_LINES, "…" after a cut. The export stops when the preview is full or \p job is canceled
    /// (RS7APP L5: the preview is closed or its options change - the export never runs on to the end of a huge
    /// file). Any thread; \p job may be null.
    static QString createXmlPreview(const PDFDocument* document, const PDFDocumentXmlExport::Options& options, const PDFOperationControl* job);

protected:
    virtual void describeToolbar(PDFAppToolPanelBuilder& builder) override;
    virtual void describeLeftPanel(PDFAppToolPanelBuilder& builder) override;
    virtual void describeRightPanel(PDFAppToolPanelBuilder& builder) override;

private:
    /// The request of a preview (plain values for the job)
    class PreviewRequest
    {
    public:
        quint64 generation = 0;
        PDFDocumentPointer document;
        PDFDocumentReport::Options options;
        bool isStructureXml = false;
        bool isFactsRequested = false;
        bool isStreamExported = false;
        bool isStreamWrittenAsText = false;
        bool isIndented = true;
        bool isStringAlwaysBinary = false;
    };

    /// The result of the job of the preview
    class PreviewResult
    {
    public:
        quint64 generation = 0;
        QJsonObject data;
        QString xmlText;
        std::optional<Facts> facts;
        bool isCanceled = false;
    };

    static PreviewResult previewJob(PreviewRequest request, std::shared_ptr<PDFAppCancelFlag> cancel);

    void schedulePreview();
    void startPreview();
    void cancelPreview();
    void onPreviewTimeout();
    void onPreviewFinished();
    void updateAll();
    void updateMenus();
    bool updateLeftPanel(); ///< false = this GUI is gone (a slot of a signal of a row deleted it)
    void updateRightPanel();
    void updateCanvas();
    void updatePreviewModel();
    std::vector<PDFInteger> getPageIndices(bool* isValid) const;
    QString getPagesText() const;
    QString getSectionDescription(PDFDocumentReportSection section) const;
    void onSectionRowClicked();
    void onSectionCheckBoxToggled(bool isChecked);
    void onCharacterMapsToggled(bool isChecked);
    void onXmlOptionToggled(bool isChecked);

    static FileOpener s_fileOpener;

    PDFDocumentReportSection m_currentSection = PDFDocumentReportSection::BasicInformation;
    std::optional<Facts> m_facts;
    quint64 m_factsVersion = 0;                 ///< The version of the document of m_facts (+1, 0 = none)
    QJsonObject m_previewData;
    QString m_xmlPreview;
    std::optional<PDFDocumentToolResult> m_result;  ///< The kept result of the last run
    quint64 m_previewGeneration = 0;
    std::shared_ptr<PDFAppCancelFlag> m_previewCancel;
    QPointer<QFutureWatcher<PreviewResult>> m_previewWatcher;
    QPointer<QTimer> m_previewTimer;
    bool m_isRunning = false;

    QPointer<PDFAppToolEnumMenu> m_kindMenu;
    QPointer<PDFAppToolEnumMenu> m_formatMenu;
    QPointer<PDFAppToolEnumMenu> m_dateMenu;
    QPointer<PDFAppToolPanelHeader> m_sectionsHeader;
    QPointer<QWidget> m_sectionsList;
    std::map<PDFDocumentReportSection, QPointer<PDFTileRow>> m_sectionRows;
    QPointer<PDFPageSelection> m_pageSelection;
    QPointer<PDFAppToolPanelHeader> m_xmlHeader;
    QPointer<PDFAppParagraph> m_xmlText;
    QPointer<PDFAppToolCallout> m_xmlCallout;
    QPointer<PDFAppToolSectionHeader> m_outputHeader;
    QPointer<PDFKeyValueList> m_outputRows;
    QPointer<QCheckBox> m_characterMapsCheckBox;
    QPointer<PDFAppParagraph> m_reportHint;
    QPointer<PDFAppToolSectionHeader> m_xmlOptionsHeader;
    QPointer<PDFAppToolCard> m_xmlOptionsCard;
    std::map<QString, QPointer<QCheckBox>> m_xmlOptions;
    QPointer<PDFAppToolCanvasMessage> m_message;
    QPointer<QScrollArea> m_previewArea;
    QPointer<PDFAppReportPreview> m_preview;
};

}   // namespace pdf

#endif // PDFAPPTOOLGUI_REPORT_H
