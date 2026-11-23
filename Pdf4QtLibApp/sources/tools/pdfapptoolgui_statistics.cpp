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

#include "pdfapptoolgui_statistics.h"
#include "pdfappdocument.h"
#include "pdfapplocale.h"
#include "pdfappstatisticstable.h"
#include "pdfapptoolhost.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptoolvalueformatter.h"
#include "pdfappworkspace.h"
#include "pdfdocumenttoolguiregistry.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfdocumenttoolshell.h"

#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdfdocumenttool_statistics.h"
#include "pdfthemeiconengine.h"

#include <QFileInfo>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

namespace pdf
{

/// The names of the parameters and the texts of Document Statistics
class PDFAppToolGUIStatisticsHelper
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppToolGUIStatistics)

public:
    PDFAppToolGUIStatisticsHelper() = delete;

    using Tool = PDFDocumentToolStatistics;

    /// Property of a row of the menu Export: the format (int of PDFDocumentToolTableFormat)
    static constexpr const char* FORMAT_PROPERTY = "pdf4qtStatisticsFormat";

    static QString name(const char* parameter) { return QString::fromLatin1(parameter); }

    /// "61.42 %"
    static QString formatPercent(double value)
    {
        return tr("%1 %").arg(PDFAppLocale::getUiLocale().toString(value, 'f', 2));
    }

    /// "2,960,412 bytes"
    static QString formatBytes(qint64 bytes)
    {
        return tr("%1 bytes").arg(PDFAppLocale::formatNumber(bytes));
    }
};

PDFAppToolGUIStatistics::PDFAppToolGUIStatistics(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppToolGUIStatistics::~PDFAppToolGUIStatistics() = default;

void PDFAppToolGUIStatistics::registerStatisticsTools()
{
    const QString toolId = QString::fromLatin1(PDFDocumentToolStatistics::TOOL_ID);
    if (!PDFDocumentToolRegistry::isImplemented(toolId))
    {
        PDFDocumentToolRegistry::registerTool(std::make_shared<PDFDocumentToolStatistics>());
    }
    if (!PDFDocumentToolGUIRegistry::getFactory(toolId))
    {
        PDFDocumentToolGUIRegistry::registerFactory(toolId, std::make_shared<PDFDocumentToolGUIFactoryOf<PDFAppToolGUIStatistics>>());
    }
}

QString PDFAppToolGUIStatistics::getView() const
{
    using Helper = PDFAppToolGUIStatisticsHelper;
    const PDFAppToolParameterModel* model = getModel();
    const QString view = model ? model->getValue(Helper::name(Helper::Tool::PARAMETER_VIEW)).toString() : QString();
    return view.isEmpty() ? Helper::name(Helper::Tool::VIEW_FUNCTION) : view;
}

QString PDFAppToolGUIStatistics::getFormatTitle(PDFDocumentToolTableFormat format)
{
    using Helper = PDFAppToolGUIStatisticsHelper;
    switch (format)
    {
        case PDFDocumentToolTableFormat::Csv:
            return Helper::tr("CSV");
        case PDFDocumentToolTableFormat::Text:
            return Helper::tr("Text");
        case PDFDocumentToolTableFormat::Html:
            return Helper::tr("HTML");
        case PDFDocumentToolTableFormat::Xml:
            return Helper::tr("XML");
        case PDFDocumentToolTableFormat::None:
            break;
    }
    return QString();
}

QString PDFAppToolGUIStatistics::getCancelText() const
{
    // 480:14243: the tool has only Close (no run to cancel)
    return tr("Close");
}

QVariantMap PDFAppToolGUIStatistics::getPreviewParameters() const
{
    // The statistics do not depend on the view or the format (both tables are computed): one preview of the
    // version, a change of the view takes the cached one
    return QVariantMap();
}

QToolButton* PDFAppToolGUIStatistics::getViewButton() const
{
    return m_viewMenu ? m_viewMenu->getButton() : nullptr;
}

QToolButton* PDFAppToolGUIStatistics::getExportButton() const
{
    return m_exportMenu ? m_exportMenu->getButton() : nullptr;
}

PDFTileRow* PDFAppToolGUIStatistics::getExportRow(PDFDocumentToolTableFormat format) const
{
    auto it = m_exportRows.find(format);
    return it != m_exportRows.cend() ? it->second.data() : nullptr;
}

void PDFAppToolGUIStatistics::describeToolbar(PDFAppToolPanelBuilder& builder)
{
    using Helper = PDFAppToolGUIStatisticsHelper;
    using Tool = Helper::Tool;

    // tool-actions 480:14239: View (480:15170, 330 wide) and Export (497:20433, 320 wide)
    m_viewMenu = builder.addEnumMenu(Helper::name(Tool::PARAMETER_VIEW), QStringLiteral("categories"), QStringLiteral("%1"), VIEW_MENU_WIDTH);
    if (m_viewMenu)
    {
        m_viewMenu->getButton()->setObjectName(QStringLiteral("statisticsView"));
        m_viewMenu->getButton()->setToolTip(tr("What the table shows"));
        if (PDFTileRow* row = m_viewMenu->getRow(Helper::name(Tool::VIEW_FUNCTION)))
        {
            row->setIconName(QStringLiteral("categories"));
            row->setDescription(tr("Pages, images, fonts… with the space they take"));
        }
        if (PDFTileRow* row = m_viewMenu->getRow(Helper::name(Tool::VIEW_TYPE)))
        {
            row->setIconName(QStringLiteral("object-type"));
            row->setDescription(tr("Dictionaries, streams, numbers… by their count"));
        }
    }

    m_exportRows.clear();
    m_exportMenu = builder.addMenu(QStringLiteral("statisticsExport"), QStringLiteral("download"), tr("Export"), EXPORT_MENU_WIDTH);
    if (m_exportMenu)
    {
        m_exportMenu->getButton()->setObjectName(QStringLiteral("statisticsExport"));
        m_exportMenu->addCaption(tr("Text, HTML and XML hold both tables"));

        struct Row
        {
            PDFDocumentToolTableFormat format;
            const char* title;
            const char* description;
            const char* icon;
        };
        const Row rows[] = {
            { PDFDocumentToolTableFormat::Csv, QT_TR_NOOP("CSV Table…"), QT_TR_NOOP("The table shown, for spreadsheets"), "csv" },
            { PDFDocumentToolTableFormat::Text, QT_TR_NOOP("Text…"), QT_TR_NOOP("Plain text tables"), "txt" },
            { PDFDocumentToolTableFormat::Html, QT_TR_NOOP("HTML…"), QT_TR_NOOP("Tables for a web browser"), "html" },
            { PDFDocumentToolTableFormat::Xml, QT_TR_NOOP("XML…"), QT_TR_NOOP("Machine-readable values"), "xml" },
        };
        for (const Row& item : rows)
        {
            PDFTileRow* row = m_exportMenu->addRow(tr(item.title), tr(item.description), QString::fromLatin1(item.icon), PDFTileRow::Kind::MenuItem);
            row->setObjectName(QStringLiteral("statisticsExport-") + PDFDocumentToolTableWriter::getFormatId(item.format));
            row->setProperty(Helper::FORMAT_PROPERTY, int(item.format));
            connect(row, &PDFTileRow::clicked, this, &PDFAppToolGUIStatistics::onExportRowClicked);
            m_exportRows[item.format] = row;
        }
    }

    updateButtons();
}

void PDFAppToolGUIStatistics::describeLeftPanel(PDFAppToolPanelBuilder& builder)
{
    // sidebar-left 480:14246: Document / the name of the file, the card File, the note
    const PDFAppDocument* document = getDocument();
    m_documentHeader = builder.addPanelHeader(tr("Document"), document ? document->getTitle() : QString());
    m_documentHeader->setObjectName(QStringLiteral("statisticsDocumentHeader"));

    builder.addSection(tr("File"));
    m_fileRows = builder.addKeyValues();
    m_fileRows->setObjectName(QStringLiteral("statisticsFileRows"));
    builder.endSection();

    builder.addHint(tr("The statistics describe the document as it is open now, including unsaved changes."));
    updateFileRows();
}

void PDFAppToolGUIStatistics::describeRightPanel(PDFAppToolPanelBuilder& builder)
{
    // sidebar-right 480:14253: the card of the progress of 480:15218, Selected: <row>, the note
    m_progressCard = new PDFAppToolProgressCard(builder.getWidget());
    m_progressCard->setObjectName(QStringLiteral("statisticsProgress"));
    builder.addWidget(m_progressCard);

    m_selectedHeader = builder.addSection(tr("Selected"));
    m_selectedHeader->setObjectName(QStringLiteral("statisticsSelectedHeader"));
    m_selectedRows = builder.addKeyValues();
    m_selectedRows->setObjectName(QStringLiteral("statisticsSelectedRows"));
    builder.endSection();

    m_selectedNote = builder.addHint(QString());
    m_selectedNote->setObjectName(QStringLiteral("statisticsSelectedNote"));
    updateSelectedRows();
}

QWidget* PDFAppToolGUIStatistics::createCanvasPage(QWidget* parent)
{
    // document-canvas 480:14252: bg/track, padding 20; the message of 480:15218 over the same background
    m_message = new PDFAppToolCanvasMessage(parent);
    m_message->setObjectName(QStringLiteral("statisticsCanvas"));
    QVBoxLayout* pageLayout = new QVBoxLayout(m_message);
    pageLayout->setContentsMargins(0, 0, 0, 0);

    QScrollArea* area = new QScrollArea(m_message);
    area->setFrameShape(QFrame::NoFrame);
    area->setProperty("overlayScrollBars", true);
    area->viewport()->setAutoFillBackground(false);
    area->setWidgetResizable(true);
    pageLayout->addWidget(area);

    QWidget* content = new QWidget(area);
    content->setAutoFillBackground(false);
    QVBoxLayout* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(20, 20, 20, 20);
    m_table = new PDFAppStatisticsTable(content);
    contentLayout->addWidget(m_table);
    contentLayout->addStretch(1);
    area->setWidget(content);
    m_canvasPage = area;

    connect(m_table.data(), &PDFAppStatisticsTable::selectedRowChanged, this, &PDFAppToolGUIStatistics::onSelectedRowChanged);
    updateCanvas();
    return m_message.data();
}

void PDFAppToolGUIStatistics::updateButtons()
{
    // 480:15218: View and Export are Disabled until the statistics are there; the view button shows the icon of
    // the chosen view (480:14324 categories, 480:14555 object-type)
    const bool isTypeView = getView() == QLatin1String(PDFDocumentToolStatistics::VIEW_TYPE);
    if (QToolButton* button = getViewButton())
    {
        button->setIcon(PDFThemeIcons::getIcon(isTypeView ? QStringLiteral("object-type") : QStringLiteral("categories")));
        button->setEnabled(hasStatistics());
    }
    if (QToolButton* button = getExportButton())
    {
        button->setEnabled(hasStatistics());
    }
}

void PDFAppToolGUIStatistics::updateFileRows()
{
    // section-File 480:14338: File size, Pages, PDF version, Objects ("Counting…" while analyzing, 480:15218),
    // Repaired when opened of a damaged file (plan 4.2);
    // the size of the file is of the saved file only (FD-2: unsaved changes have no file yet)
    if (!m_fileRows)
    {
        return;
    }

    const PDFAppDocument* appDocument = getDocument();
    const PDFDocument* document = appDocument && appDocument->getDocument() ? appDocument->getDocument().get() : nullptr;
    QString fileSize = QStringLiteral("–");
    if (appDocument)
    {
        if (appDocument->isModified() || appDocument->getFilePath().isEmpty())
        {
            fileSize = tr("Save the document first");
        }
        else
        {
            const qint64 size = !appDocument->getOriginalData().isEmpty() ? appDocument->getOriginalData().size() : QFileInfo(appDocument->getFilePath()).size();
            fileSize = PDFAppLocale::formatDataSize(size, 2, QLocale::DataSizeTraditionalFormat);
        }
    }

    QString objects = tr("Counting…");
    if (m_statistics)
    {
        objects = PDFAppLocale::formatNumber(m_statistics->data.value(QLatin1String(PDFDocumentToolStatistics::DATA_OBJECT_COUNT)).toInteger());
    }
    else if (!m_errorMessage.isEmpty())
    {
        objects = QStringLiteral("–");
    }

    m_fileRows->clear();
    m_fileRows->addRow(tr("File size"), fileSize);
    m_fileRows->addRow(tr("Pages"), document ? PDFAppLocale::formatNumber(document->getCatalog()->getPageCount()) : QStringLiteral("–"));
    m_fileRows->addRow(tr("PDF version"), document ? QString::fromLatin1(document->getVersion()) : QStringLiteral("–"));
    m_fileRows->addRow(tr("Objects"), objects);

    // Plan 4.2, FD-7: the reader restored the document of a damaged file (the data of the core, from the reading
    // of the tab - one source with the report and Properties); an intact file has no such row
    if (m_statistics && m_statistics->data.value(QLatin1String(PDFDocumentToolStatistics::DATA_REPAIRED)).toBool())
    {
        m_fileRows->addRow(tr("Repaired when opened"), tr("Yes"));
    }
}

bool PDFAppToolGUIStatistics::updateCanvas()
{
    // The parts without a signal first; the progress bar and the table emit (a slot of any signal may retire
    // this GUI): each is followed by the check of the retired state
    if (m_canvasPage)
    {
        m_canvasPage->setVisible(m_statistics.has_value());
    }
    if (m_message)
    {
        if (m_statistics)
        {
            m_message->setMessage(QString(), QString(), QString());
        }
        else if (!m_errorMessage.isEmpty())
        {
            m_message->setMessage(QStringLiteral("alert-circle"), tr("The statistics could not be computed"), m_errorMessage);
        }
        else
        {
            m_message->setMessage(QStringLiteral("refresh-cw"), tr("Analyzing the document…"), QString());
        }
    }

    if (m_progressCard)
    {
        // 480:15218: "Analyzing the document" with the step "Objects are classified by what they are used for"
        const QString step = m_stepText.isEmpty() ? tr("Objects are classified by what they are used for") : m_stepText;
        m_progressCard->setVisible(!m_statistics && m_errorMessage.isEmpty());
        m_progressCard->setProgress(m_percentage, tr("Analyzing the document"), step, QString());
        if (PDFDeferredDeletion::isRetired(this))
        {
            return false;
        }
    }

    if (m_table)
    {
        if (m_statistics)
        {
            m_table->setData(m_statistics->data, getView());
        }
        else
        {
            m_table->clear();
        }
    }
    return !PDFDeferredDeletion::isRetired(this);
}

void PDFAppToolGUIStatistics::updateSelectedRows()
{
    using Helper = PDFAppToolGUIStatisticsHelper;

    // section-Selected 480:14355: by function Share of size, Count, Space usage, Average size; by type Share of
    // values, Count (480:14555)
    const bool isTypeView = getView() == QLatin1String(PDFDocumentToolStatistics::VIEW_TYPE);
    const PDFAppStatisticsRow* row = m_table ? m_table->getSelectedRowData() : nullptr;
    const bool isShown = hasStatistics() && row;
    if (m_selectedHeader)
    {
        m_selectedHeader->getTitleLabel()->setText(row ? tr("Selected: %1").arg(row->title) : tr("Selected"));
        m_selectedHeader->setVisible(isShown);
    }
    if (m_selectedRows)
    {
        m_selectedRows->clear();
        if (row)
        {
            if (isTypeView)
            {
                m_selectedRows->addRow(tr("Share of values"), Helper::formatPercent(row->share));
                m_selectedRows->addRow(tr("Count"), PDFAppLocale::formatNumber(row->count));
            }
            else
            {
                m_selectedRows->addRow(tr("Share of size"), Helper::formatPercent(row->share));
                m_selectedRows->addRow(tr("Count"), PDFAppLocale::formatNumber(row->count));
                m_selectedRows->addRow(tr("Space usage"), Helper::formatBytes(row->bytes));
                m_selectedRows->addRow(tr("Average size"), Helper::formatBytes(row->count > 0 ? qRound64(double(row->bytes) / double(row->count)) : 0));
            }
        }
        // The card of the rows goes with them: an empty card is no part of 480:15218
        m_selectedRows->setVisible(isShown);
        if (QWidget* card = m_selectedRows->parentWidget())
        {
            card->setVisible(isShown);
        }
    }
    if (m_selectedNote)
    {
        m_selectedNote->setText(isTypeView ? tr("Every value in the document is counted by its PDF type. The share is the part of the number of all values.")
                                           : tr("Every object is assigned to what it is used for. The share is the part of the size of all objects in bytes."));
        m_selectedNote->setVisible(hasStatistics());
    }
}

QString PDFAppToolGUIStatistics::getStatusText() const
{
    // 480:14255 "Images take 61.42 % of the document.", 480:14555 "Values counted by their PDF type.",
    // 480:15218 "Analyzing the document…"
    const PDFDocumentToolShell* shell = getShell();
    if (shell && shell->isPermissionRefused())
    {
        return BaseClass::getStatusText();
    }
    if (!m_errorMessage.isEmpty())
    {
        return tr("The statistics could not be computed: %1").arg(m_errorMessage);
    }
    if (!m_statistics)
    {
        return tr("Analyzing the document…");
    }
    if (getView() == QLatin1String(PDFDocumentToolStatistics::VIEW_TYPE))
    {
        return tr("Values counted by their PDF type.");
    }

    const PDFAppStatisticsRow* row = m_table ? m_table->getSelectedRowData() : nullptr;
    if (!row)
    {
        return tr("The document has no objects.");
    }
    return tr("%1 take %2 of the document.").arg(PDFAppStatisticsTable::getClassPluralTitle(row->id), PDFAppToolGUIStatisticsHelper::formatPercent(row->share));
}

void PDFAppToolGUIStatistics::updateAll()
{
    // The table emits its chosen row in updateCanvas: onSelectedRowChanged says nothing to the status meanwhile
    // (its slots may delete this GUI) - the status is said once, as the last thing
    m_isUpdating = true;
    updateButtons();
    updateFileRows();
    if (!updateCanvas())
    {
        return;
    }
    updateSelectedRows();
    m_isUpdating = false;

    // The slots of the status may delete this GUI: the status is the last thing
    updateStatusText();
}

void PDFAppToolGUIStatistics::onParametersChanged(const QString& name)
{
    Q_UNUSED(name);
    updateAll();
}

void PDFAppToolGUIStatistics::onPreviewChanged(const PDFDocumentToolResult* preview)
{
    if (preview && preview->isSuccess())
    {
        m_statistics = *preview;
        m_errorMessage.clear();
    }
    else
    {
        // A dropped preview (a new version is analyzed) or a failed one
        m_statistics.reset();
        m_errorMessage = preview ? (preview->message.isEmpty() ? tr("The document could not be analyzed.") : preview->message) : QString();
    }
    m_percentage = 0;
    m_stepText.clear();
    updateAll();
}

void PDFAppToolGUIStatistics::onPreviewProgress(int percentage, const QString& stepText)
{
    m_percentage = percentage;
    m_stepText = stepText;
    updateCanvas();
}

void PDFAppToolGUIStatistics::onDocumentChanged()
{
    // A new version: the runner drops the preview and computes it again after a short rest (R7-22 (7))
    if (m_documentHeader)
    {
        const PDFAppDocument* document = getDocument();
        m_documentHeader->setTexts(tr("Document"), document ? document->getTitle() : QString());
    }
    updateFileRows();
}

void PDFAppToolGUIStatistics::onSelectedRowChanged(int row)
{
    Q_UNUSED(row);
    if (!m_isUpdating)
    {
        // A row chosen by the user (owner 2026-11-20, isInteractive)
        markUsed();
    }
    updateSelectedRows();

    // The slots of the status may delete this GUI: the status is the last thing (an update of the GUI says it
    // at its end)
    if (!m_isUpdating)
    {
        updateStatusText();
    }
}

QString PDFAppToolGUIStatistics::getBaseName() const
{
    // "Project_Report - statistics" (480:15368)
    const PDFAppDocument* document = getDocument();
    const QString title = document ? QFileInfo(document->getTitle()).completeBaseName() : QString();
    return tr("%1 - statistics").arg(title.isEmpty() ? tr("Document") : title);
}

bool PDFAppToolGUIStatistics::exportStatistics(PDFDocumentToolTableFormat format)
{
    using Tool = PDFDocumentToolStatistics;

    const PDFDocumentToolShell* shell = getShell();
    if (!m_statistics || format == PDFDocumentToolTableFormat::None || !shell || shell->getState() != PDFDocumentToolShell::State::Editing)
    {
        return false;
    }

    // The data of the shown statistics, never a new run (plan 4.1); CSV is the table shown (K7.20)
    QVariantMap parameters;
    parameters.insert(QString::fromLatin1(Tool::PARAMETER_VIEW), getView());
    parameters.insert(QString::fromLatin1(Tool::PARAMETER_FORMAT), PDFDocumentToolTableWriter::getFormatId(format));
    QString errorMessage;
    PDFDocumentToolOutputFile output = Tool::writeOutput(m_statistics->data, parameters, PDFAppToolValueFormatter::createOptions(PDFDocumentToolDateFormat::Short), getBaseName(), &errorMessage);
    if (output.suggestedFileName.isEmpty())
    {
        if (PDFAppToolHost* host = getHost())
        {
            host->showTransientMessage(errorMessage.isEmpty() ? tr("The statistics could not be exported.") : errorMessage);
        }
        return false;
    }

    PDFDocumentToolResult exportResult;
    exportResult.outputs.push_back(std::move(output));
    exportResult.data = m_statistics->data;
    m_exportFormat = format;

    // The dialog is the last thing: its slots may run anything
    return openExport(exportResult);
}

void PDFAppToolGUIStatistics::fillResultInput(PDFAppToolResultInput* input) const
{
    using Tool = PDFDocumentToolStatistics;

    // 480:15368: the title, the facts of the tables and of the format, the status message
    input->title = tr("Statistics are ready to export");
    input->statusText = tr("The statistics are ready to be exported.");

    const QJsonObject data = m_statistics ? m_statistics->data : QJsonObject();
    const std::vector<PDFAppStatisticsRow> classes = PDFAppStatisticsTable::createRows(data, QString::fromLatin1(Tool::VIEW_FUNCTION));
    const std::vector<PDFAppStatisticsRow> types = PDFAppStatisticsTable::createRows(data, QString::fromLatin1(Tool::VIEW_TYPE));
    const bool isCsv = m_exportFormat == PDFDocumentToolTableFormat::Csv;
    const bool isTypeView = getView() == QLatin1String(Tool::VIEW_TYPE);

    std::vector<PDFDocumentToolFact> facts;
    if (!isCsv || !isTypeView)
    {
        facts.push_back({ QStringLiteral("byFunction"), tr("By function"), tr("%Ln classes with size and count", nullptr, int(classes.size())) });
    }
    if (!isCsv || isTypeView)
    {
        facts.push_back({ QStringLiteral("byType"), tr("By object type"), tr("%Ln types with count", nullptr, int(types.size())) });
    }
    facts.push_back({ QStringLiteral("format"), tr("Format"), getFormatTitle(m_exportFormat) });
    input->result.facts = std::move(facts);
}

void PDFAppToolGUIStatistics::onExportDecided(const PDFAppToolResultOutcome& outcome)
{
    Q_UNUSED(outcome);
    m_exportFormat = PDFDocumentToolTableFormat::None;
}

void PDFAppToolGUIStatistics::onExportRowClicked()
{
    QObject* row = sender();
    if (!row)
    {
        return;
    }

    const PDFDocumentToolTableFormat format = static_cast<PDFDocumentToolTableFormat>(row->property(PDFAppToolGUIStatisticsHelper::FORMAT_PROPERTY).toInt());
    if (m_exportMenu)
    {
        m_exportMenu->close();
    }

    // The dialog runs foreign code: the last statement
    exportStatistics(format);
}

}   // namespace pdf

#include "moc_pdfapptoolgui_statistics.cpp"
