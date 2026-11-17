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

#ifndef PDFAPPTOOLGUI_INKCOVERAGE_H
#define PDFAPPTOOLGUI_INKCOVERAGE_H

#include "pdfappdeclarativetoolgui.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfappworkspace.h"

#include "pdfcomponentpanels.h"
#include "pdfdocumenttool_inkcoverage.h"
#include "pdffluentwidgets.h"

#include <QAbstractScrollArea>
#include <QColor>
#include <QJsonObject>
#include <QMutex>
#include <QPointer>
#include <QWidget>

#include <memory>
#include <optional>
#include <vector>

class QStackedWidget;
class QTimer;

namespace pdf
{

class PDFAppParagraph;
class PDFAppToolCallout;
class PDFAppToolPanelPopup;
class PDFIconBadge;
class PDFKeyValueList;
class PDFLabel;

/// Ink Coverage of one run of the GUI: the core tool, which gives the finished pages of its run to the collector
/// of that run, which the GUI polls (plan of step 7 of phase 2, 4.5: the rows appear during the run 478:11954,
/// the finished pages stay after Stop 478:14194; PDFDocumentToolInkCoverage::measure). The GUI creates one for
/// each run and gives it by the environment of the run (PDFAppToolRunEnvironment::runTool, RS7APP L3): the sink
/// belongs to the run, no other run (MCP over the same document, the catalog) can reach it. The catalog has the
/// core tool. Immutable: execute runs in the worker; the collector is thread-safe.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppInkCoverageTool : public PDFDocumentToolInkCoverage
{
public:
    /// \param collector The collector of the run, nullptr = none (PDFDocumentToolInkCoverage::execute exactly)
    explicit PDFAppInkCoverageTool(std::shared_ptr<PDFInkCoverageCollector> collector);

    virtual PDFDocumentToolResult execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const override;

    const std::shared_ptr<PDFInkCoverageCollector>& getCollector() const { return m_collector; }

private:
    std::shared_ptr<PDFInkCoverageCollector> m_collector;
};

/// What the table of Ink Coverage shows (a value built from the data of the core, DATA_* of
/// PDFDocumentToolInkCoverage, of a finished run or of the finished pages of a running / stopped one)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppInkCoverageTableData
{
public:
    class Ink
    {
    public:
        QString name;       ///< Shown name
        QColor color;       ///< Color of the swatch (invalid = none)
        bool isSpot = false;
    };

    enum class RowState
    {
        Pending,    ///< Not measured yet ("…", 478:11954)
        Measured,   ///< Measured
        Issue,      ///< Measured with errors of the rendering (the warning icon, Q7.9 b)
        Unmeasured  ///< Cannot be rendered at all: not in the totals
    };

    class Row
    {
    public:
        PDFInteger page = 0;            ///< From 1
        RowState state = RowState::Pending;
        std::vector<double> areas;      ///< mm² per ink
        std::vector<double> ratios;     ///< % per ink
        double boxArea = 0.0;           ///< mm² of the box of the ratios
        double maximumCoverage = 0.0;   ///< % of the darkest point
        QString message;                ///< The error of the page
    };

    std::vector<Ink> inks;
    std::vector<Row> rows;              ///< The pages to measure in their order
    std::vector<double> totalAreas;     ///< The row Total: the sum of the measured pages
    std::vector<double> totalRatios;    ///< The row Total: the ink inside of the boxes over their area
    PDFInteger peakPage = -1;           ///< Page of the peak coverage (from 1), -1 = none
    double peakCoverage = 0.0;          ///< % of the peak coverage
    int measuredCount = 0;              ///< Measured pages (also those with issues)

    bool isEmpty() const { return inks.empty() && rows.empty(); }
    int getProcessCount() const;
    int getSpotCount() const;

    /// The data of a finished run (DATA_* of the core), the totals of the core
    static PDFAppInkCoverageTableData fromResult(const QJsonObject& data);

    /// The finished pages of a run (\p inks, \p pages of PDFInkCoverageCollector) over \p pageIndices (the
    /// pages to measure, from 0): the others Pending; the totals summed from the finished pages (each page
    /// once; the ratio is the ink inside of the boxes over their area, as the core does it)
    static PDFAppInkCoverageTableData fromPages(const QJsonArray& inks, const QJsonArray& pages, const std::vector<PDFInteger>& pageIndices);

    /// The data of the core of these pages (DATA_INKS, DATA_PAGES, DATA_TOTAL, DATA_PEAK) for an export of a
    /// stopped run (writeOutput reads it)
    QJsonObject toData() const;

private:
    static Row readRow(const QJsonObject& object, size_t inkCount);
};

/// The table of Ink Coverage (table 478:12696): a card bg/surface, border/default, radius 10; the head of two
/// lines - the swatch and the name of each ink (Medium 11) over its columns, the units "mm²" and "%" (Medium 11
/// text/tertiary) -, the rows of 26 (Regular 12; "…" of a pending page, the warning icon of a page with errors),
/// the row Total of 30 (Semi Bold 12). The head and the row Total stay, the pages scroll (10,000 pages: only the
/// shown rows are drawn). Drawn by the tokens of the theme; the columns are mirrored right-to-left.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppInkCoverageTable : public QAbstractScrollArea
{
    Q_OBJECT

private:
    using BaseClass = QAbstractScrollArea;

public:
    static constexpr int PADDING = 13;          ///< Padding 12 + the border
    static constexpr int PAGE_WIDTH = 46;
    static constexpr int AREA_WIDTH = 72;
    static constexpr int RATIO_WIDTH = 38;
    static constexpr int INK_LINE = 22;         ///< The line of the names of the inks
    static constexpr int UNIT_LINE = 24;        ///< The line of the units
    static constexpr int ROW_HEIGHT = 26;
    static constexpr int TOTAL_HEIGHT = 30;
    static constexpr int RADIUS = 10;

    explicit PDFAppInkCoverageTable(QWidget* parent);
    virtual ~PDFAppInkCoverageTable() override;

    void setData(const PDFAppInkCoverageTableData& data);
    const PDFAppInkCoverageTableData& getData() const { return m_data; }

    /// Texts of a cell as drawn (tests): \p row -1 = the row Total; \p column 0 = Page, then mm² and % of
    /// each ink
    QString getCellText(int row, int column) const;

    /// Rows of the pages, which getAccessibleText names (the others are counted)
    static constexpr int MAXIMUM_ACCESSIBLE_ROWS = 50;

    /// What a screen reader says of the painted table (RS7APP L8; the accessible description): the row Total and
    /// the first MAXIMUM_ACCESSIBLE_ROWS pages with the texts of their cells ("Page 1: Cyan 12.40 mm², 3 %; …"),
    /// a pending page "not measured yet", a page, which cannot be rendered, "cannot be measured"
    QString getAccessibleText() const;

    /// Height of the head and of the row Total
    int getHeadHeight() const { return INK_LINE + UNIT_LINE; }

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void resizeEvent(QResizeEvent* event) override;
    virtual bool viewportEvent(QEvent* event) override;
    virtual void scrollContentsBy(int dx, int dy) override;

private:
    void updateScrollBars();
    int getContentWidth() const;
    QString getValueText(double value, int decimals) const;

    PDFAppInkCoverageTableData m_data;
};

/// Total per ink (section-Total per ink 478:12696): a card of rows - the swatch, the name (Regular 12), the
/// total area (Medium 12) and the bar of 4 px in the color of the ink, its length the share of the largest
/// total. Drawn by the tokens of the theme.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppInkTotalsList : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    static constexpr int ROW_HEIGHT = 35;
    static constexpr int PADDING = 13;

    explicit PDFAppInkTotalsList(QWidget* parent);
    virtual ~PDFAppInkTotalsList() override;

    void setData(const PDFAppInkCoverageTableData& data);

    /// Text of the area of the ink (tests)
    QString getAreaText(int index) const;

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;

private:
    PDFAppInkCoverageTableData m_data;
};

/// The canvas of Ink Coverage (document-canvas 478:11760 … 478:17237): bg/track, padding 20; the empty state
/// (the badge of the tool, Semi Bold 18 title, Regular 13 text) before the first result or while the page
/// selection has an error (478:17237), else the table and its note below it.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppInkCoverageCanvasPage : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    static constexpr int PADDING = 20;
    static constexpr int GAP = 12;

    explicit PDFAppInkCoverageCanvasPage(QWidget* parent);
    virtual ~PDFAppInkCoverageCanvasPage() override;

    /// The empty state with its texts
    void showEmptyState(const QString& title, const QString& text);

    /// The table
    void showTable(const PDFAppInkCoverageTableData& data);

    bool isTableShown() const;
    PDFAppInkCoverageTable* getTable() const { return m_table.data(); }
    PDFLabel* getEmptyTitle() const { return m_emptyTitle.data(); }
    PDFAppParagraph* getEmptyText() const { return m_emptyText.data(); }

protected:
    virtual void paintEvent(QPaintEvent* event) override;

private:
    QPointer<QStackedWidget> m_stack;
    QPointer<QWidget> m_emptyPage;
    QPointer<QWidget> m_tablePage;
    QPointer<PDFLabel> m_emptyTitle;
    QPointer<PDFAppParagraph> m_emptyText;
    QPointer<PDFAppInkCoverageTable> m_table;
};

/// The GUI of Ink Coverage (plan of step 7 of phase 2, 4.5; Figma 478:11760 … 478:17237): the Page selection on
/// the left, the table page × ink (mm², %) with the row Total, the Summary and Total per ink on the right;
/// Calculate Coverage → Stop → Calculate Again. The result is kept in the tool (isResultKeptInTool, R7-2: after
/// it the Primary button Disabled, stale after a change of the document or of the pages); the rows appear during
/// the run (PDFAppInkCoverageTool + a timer of the GUI polling the collector - no signal from the thread); after
/// Stop the finished pages stay, the others "…" (478:14194). Export (CSV / Text / HTML / XML) is
/// PDFDocumentToolGUI::openExport with PDFDocumentToolInkCoverage::writeOutput (478:15674) - the data of the
/// shown table, never a new run. A page with an error of the rendering is measured with a warning (Q7.9 b).
///
/// Ownership: the shell owns this GUI; the timer is its child, the collector is shared with the worker of the
/// run (std::shared_ptr, the worker drops its copy, when measure returns); the widgets belong to the panels.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolGUIInkCoverage : public PDFAppDeclarativeToolGUI
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDeclarativeToolGUI;

public:
    explicit PDFAppToolGUIInkCoverage(QObject* parent = nullptr);
    virtual ~PDFAppToolGUIInkCoverage() override;

    /// Registers the core tool (as PDFAppInkCoverageTool) and this GUI (PDFAppBuiltinTools::registerAll)
    static void registerInkCoverageTools();

    /// Interval of the poll of the finished pages during a run (ms)
    static constexpr int POLL_INTERVAL = 150;

    /// What the table shows
    enum class Content
    {
        None,       ///< Nothing measured yet (the empty state)
        Running,    ///< The finished pages of the run
        Stopped,    ///< The finished pages of a stopped run (478:14194)
        Result      ///< The result of a finished run
    };

    virtual QWidget* createCanvasPage(QWidget* parent) override;
    virtual QString getStatusText() const override;
    virtual QString getCancelText() const override;
    virtual QString getConfirmText() const override;
    virtual QString getConfirmIconName() const override;
    virtual bool isConfirmEnabled() const override;
    virtual QString getRunningStatusText(const QString& stepText) const override;
    virtual QString getRunningCardTitle(int percentage, const QString& stepText) const override;
    virtual QString getRunningCardStepText(const QString& stepText) const override;
    virtual QString getRunningTimeText(int seconds) const override;
    virtual void fillResultInput(PDFAppToolResultInput* input) const override;
    virtual bool isResultKeptInTool() const override;
    virtual void onResultKept(const PDFDocumentToolResult& result) override;
    virtual void prepareEnvironment(PDFAppToolRunEnvironment* environment) const override;
    virtual void onRunStateChanged(bool isRunning) override;
    virtual void onRunNotAccepted(const PDFDocumentToolResult& result) override;
    virtual void onParametersChanged(const QString& name) override;
    virtual void onDocumentChanged() override;
    virtual void onExportDecided(const PDFAppToolResultOutcome& outcome) override;

    Content getContent() const { return m_content; }
    const PDFAppInkCoverageTableData& getTableData() const { return m_tableData; }

    /// The pages to measure (from 0) of the selection of now; empty with an error (\p errorMessage)
    std::vector<PDFInteger> getSelectedPages(QString* errorMessage = nullptr) const;

    /// Opens the dialog of the export of the shown table (\p format of PDFDocumentToolTableFormat)
    bool exportTable(PDFDocumentToolTableFormat format);

    /// Parts (tests; nullptr = none)
    PDFAppInkCoverageCanvasPage* getCanvasPage() const { return m_canvasPage.data(); }
    PDFKeyValueList* getSummaryRows() const { return m_summaryRows.data(); }
    PDFAppInkTotalsList* getTotalsList() const { return m_totals.data(); }
    PDFAppToolCallout* getCallout() const { return m_callout.data(); }
    PDFAppToolPanelPopup* getExportPanel() const { return m_exportPanel.data(); }

protected:
    virtual void describeToolbar(PDFAppToolPanelBuilder& builder) override;
    virtual void describeRightPanel(PDFAppToolPanelBuilder& builder) override;
    virtual void describeLeftPanel(PDFAppToolPanelBuilder& builder) override;

private:
    void onPollTimeout();
    void onExportRowClicked();
    void pollCollector();
    void updateParts();
    void announceState();
    void setContent(Content content, const PDFAppInkCoverageTableData& data, const QJsonObject& resultData);
    QString getPageSizeText(const std::vector<PDFInteger>& pageIndices) const;
    QString getBaseName() const;

    Content m_content = Content::None;
    PDFAppInkCoverageTableData m_tableData;
    QJsonObject m_resultData;                       ///< The data of the shown result (Content::Result)
    mutable std::shared_ptr<PDFInkCoverageCollector> m_collector;   ///< The collector of the run (prepareEnvironment)
    mutable std::vector<PDFInteger> m_runPages;     ///< The pages of the run
    Content m_contentBeforeRun = Content::None;
    PDFAppInkCoverageTableData m_dataBeforeRun;
    QJsonObject m_resultDataBeforeRun;
    PDFDocumentToolTableFormat m_exportFormat = PDFDocumentToolTableFormat::Csv;
    bool m_isRunning = false;

    QPointer<QTimer> m_pollTimer;
    QPointer<PDFAppInkCoverageCanvasPage> m_canvasPage;
    QPointer<PDFKeyValueList> m_summaryRows;
    QPointer<QWidget> m_totalsSection;
    QPointer<PDFAppInkTotalsList> m_totals;
    QPointer<PDFAppToolCallout> m_callout;
    QPointer<PDFAppToolPanelPopup> m_exportPanel;
};

}   // namespace pdf

#endif // PDFAPPTOOLGUI_INKCOVERAGE_H
