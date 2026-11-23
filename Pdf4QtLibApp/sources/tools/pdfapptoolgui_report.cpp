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

#include "pdfapptoolgui_report.h"
#include "pdfappdocument.h"
#include "pdfappdocumentsearchindex.h"
#include "pdfapplocale.h"
#include "pdfapppagelayout.h"
#include "pdfappreportpreview.h"
#include "pdfappstatisticstable.h"
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
#include "pdfdocumenttool_report.h"
#include "pdfdocumentxmlexport.h"
#include "pdfjavascriptscanner.h"
#include "pdfthemefonts.h"
#include "pdfthemeiconengine.h"

#include <QCheckBox>
#include <QDesktopServices>
#include <QFileInfo>
#include <QIODevice>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <limits>

namespace pdf
{

/// The cancellation of the export of the XML preview: the device is full, or the job of the preview is canceled
/// (the preview is closed or its options change, RS7APP L5 - the export stops at once, not at the end of the file)
class PDFAppReportXmlPreviewControl : public PDFOperationControl
{
public:
    explicit PDFAppReportXmlPreviewControl(const PDFOperationControl* job) :
        m_job(job)
    {

    }

    virtual bool isOperationCanceled() const override
    {
        return m_isFull.isOperationCanceled() || PDFOperationControl::isOperationCanceled(m_job);
    }

    /// The device has the whole preview
    void setFull() { m_isFull.cancel(); }

private:
    const PDFOperationControl* m_job;
    PDFAppCancelFlag m_isFull;
};

/// A device of the XML preview: it keeps the first XML_PREVIEW_BYTES and then cancels the export (a huge
/// document is never written whole into the memory, plan 4.4)
class PDFAppReportXmlPreviewDevice : public QIODevice
{
public:
    explicit PDFAppReportXmlPreviewDevice(PDFAppReportXmlPreviewControl* cancel) :
        m_cancel(cancel)
    {

    }

    const QByteArray& getData() const { return m_data; }
    bool isFull() const { return m_isFull; }

protected:
    virtual qint64 readData(char* data, qint64 maxSize) override
    {
        Q_UNUSED(data);
        Q_UNUSED(maxSize);
        return -1;
    }

    virtual qint64 writeData(const char* data, qint64 maxSize) override
    {
        // The rest is accepted and dropped: the writer sees no error, the cancellation stops it
        const qint64 room = PDFAppToolGUIReport::XML_PREVIEW_BYTES - m_data.size();
        if (room > 0)
        {
            m_data.append(data, qMin(room, maxSize));
        }
        if (m_data.size() >= PDFAppToolGUIReport::XML_PREVIEW_BYTES && !m_isFull)
        {
            m_isFull = true;
            m_cancel->setFull();
        }
        return maxSize;
    }

private:
    PDFAppReportXmlPreviewControl* m_cancel;
    QByteArray m_data;
    bool m_isFull = false;
};

/// The names of the parameters and the texts of Document Report
class PDFAppToolGUIReportHelper
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppToolGUIReport)

public:
    PDFAppToolGUIReportHelper() = delete;

    using Tool = PDFDocumentToolReport;

    /// Property of a row of a section: the section (int)
    static constexpr const char* SECTION_PROPERTY = "pdf4qtReportSection";

    /// Property of a check box of the XML options: its id
    static constexpr const char* OPTION_PROPERTY = "pdf4qtReportOption";

    /// The ids of the XML options (484:18196)
    static constexpr const char* OPTION_HEX = "streamsHex";
    static constexpr const char* OPTION_TEXT = "streamsText";
    static constexpr const char* OPTION_INDENT = "indent";
    static constexpr const char* OPTION_BINARY = "alwaysBinaryStrings";

    static QString name(const char* parameter) { return QString::fromLatin1(parameter); }

    /// The title of the format of the values (484:16401 "Text", "HTML", "XML")
    static QString getFormatTitle(const QVariantMap& values)
    {
        switch (PDFDocumentToolTableWriter::getFormat(values))
        {
            case PDFDocumentToolTableFormat::Text:
                return tr("Text");
            case PDFDocumentToolTableFormat::Html:
                return tr("HTML");
            case PDFDocumentToolTableFormat::Xml:
                return tr("XML");
            default:
                break;
        }
        return QString();
    }

    /// The title of the kind of the values (484:15916, the options of the core)
    static QString getKindTitle(const PDFAppToolParameterModel* model)
    {
        const PDFDocumentToolParameterDefinition* definition = model ? PDFDocumentToolParameters::find(model->getDefinitions(), name(Tool::PARAMETER_KIND)) : nullptr;
        const QString value = model ? model->getValue(name(Tool::PARAMETER_KIND)).toString() : QString();
        if (definition)
        {
            for (const PDFDocumentToolParameterOption& option : definition->getOptions())
            {
                if (option.id == value)
                {
                    return option.getTitle();
                }
            }
        }
        return value;
    }

    /// The parameter of a section
    static QString sectionName(PDFDocumentReportSection section) { return PDFDocumentReport::getSectionId(section); }
};

PDFAppToolGUIReport::FileOpener PDFAppToolGUIReport::s_fileOpener = nullptr;

PDFAppToolGUIReport::PDFAppToolGUIReport(QObject* parent) :
    BaseClass(parent)
{
    m_previewTimer = new QTimer(this);
    m_previewTimer->setSingleShot(true);
    m_previewTimer->setInterval(PREVIEW_DELAY);
    connect(m_previewTimer.data(), &QTimer::timeout, this, &PDFAppToolGUIReport::onPreviewTimeout);
}

PDFAppToolGUIReport::~PDFAppToolGUIReport()
{
    // The job goes on with its own copies; its cancellation stops it early
    cancelPreview();
}

void PDFAppToolGUIReport::registerReportTools()
{
    const QString toolId = QString::fromLatin1(PDFDocumentToolReport::TOOL_ID);
    if (!PDFDocumentToolRegistry::isImplemented(toolId))
    {
        PDFDocumentToolRegistry::registerTool(std::make_shared<PDFDocumentToolReport>());
    }
    if (!PDFDocumentToolGUIRegistry::getFactory(toolId))
    {
        PDFDocumentToolGUIRegistry::registerFactory(toolId, std::make_shared<PDFDocumentToolGUIFactoryOf<PDFAppToolGUIReport>>());
    }
}

void PDFAppToolGUIReport::setFileOpener(FileOpener opener)
{
    s_fileOpener = opener;
}

QString PDFAppToolGUIReport::getSectionTitle(PDFDocumentReportSection section)
{
    using Helper = PDFAppToolGUIReportHelper;
    switch (section)
    {
        case PDFDocumentReportSection::BasicInformation:
            return Helper::tr("Basic information");
        case PDFDocumentReportSection::FileHashes:
            return Helper::tr("File hashes");
        case PDFDocumentReportSection::Metadata:
            return Helper::tr("Metadata (XMP)");
        case PDFDocumentReportSection::JavaScript:
            return Helper::tr("JavaScript");
        case PDFDocumentReportSection::NamedDestinations:
            return Helper::tr("Named destinations");
        case PDFDocumentReportSection::PageBoxes:
            return Helper::tr("Page boxes");
        case PDFDocumentReportSection::StructureTree:
            return Helper::tr("Structure tree");
        case PDFDocumentReportSection::Inks:
            return Helper::tr("Inks");
        case PDFDocumentReportSection::Fonts:
            return Helper::tr("Fonts");
    }
    return QString();
}

QString PDFAppToolGUIReport::getSectionIconName(PDFDocumentReportSection section)
{
    // The tiles of 484:15472
    switch (section)
    {
        case PDFDocumentReportSection::BasicInformation:
            return QStringLiteral("info");
        case PDFDocumentReportSection::FileHashes:
            return QStringLiteral("hash");
        case PDFDocumentReportSection::Metadata:
            return QStringLiteral("metadata");
        case PDFDocumentReportSection::JavaScript:
            return QStringLiteral("script");
        case PDFDocumentReportSection::NamedDestinations:
            return QStringLiteral("destination");
        case PDFDocumentReportSection::PageBoxes:
            return QStringLiteral("boxes-show");
        case PDFDocumentReportSection::StructureTree:
            return QStringLiteral("tree");
        case PDFDocumentReportSection::Inks:
            return QStringLiteral("droplet");
        case PDFDocumentReportSection::Fonts:
            return QStringLiteral("font");
    }
    return QString();
}

QVariantMap PDFAppToolGUIReport::getStreamValues(bool isHex, bool isText)
{
    using Tool = PDFDocumentToolReport;

    // One choice of two check boxes: the streams are written (as text when possible, else hexadecimal) or not
    QVariantMap values;
    values.insert(QString::fromLatin1(Tool::PARAMETER_EXPORT_STREAMS), isHex || isText);
    values.insert(QString::fromLatin1(Tool::PARAMETER_STREAMS_AS_TEXT), isText);
    return values;
}

bool PDFAppToolGUIReport::isStructureXml() const
{
    using Helper = PDFAppToolGUIReportHelper;
    const PDFAppToolParameterModel* model = getModel();
    return model && model->getValue(Helper::name(Helper::Tool::PARAMETER_KIND)).toString() == QLatin1String(Helper::Tool::KIND_STRUCTURE_XML);
}

bool PDFAppToolGUIReport::isSectionAvailable(PDFDocumentReportSection section) const
{
    if (!m_facts)
    {
        return true;
    }
    auto it = m_facts->emptyReasons.find(section);
    return it == m_facts->emptyReasons.cend() || it->second.isEmpty();
}

std::vector<PDFDocumentReportSection> PDFAppToolGUIReport::getIncludedSections() const
{
    std::vector<PDFDocumentReportSection> sections;
    const PDFAppToolParameterModel* model = getModel();
    if (!model)
    {
        return sections;
    }
    for (const PDFDocumentReportSection section : PDFDocumentReport::getSections())
    {
        if (model->getValue(PDFAppToolGUIReportHelper::sectionName(section)).toBool() && isSectionAvailable(section))
        {
            sections.push_back(section);
        }
    }
    return sections;
}

PDFTileRow* PDFAppToolGUIReport::getSectionRow(PDFDocumentReportSection section) const
{
    auto it = m_sectionRows.find(section);
    return it != m_sectionRows.cend() ? it->second.data() : nullptr;
}

QCheckBox* PDFAppToolGUIReport::getXmlOption(const char* parameter) const
{
    auto it = m_xmlOptions.find(QString::fromLatin1(parameter));
    return it != m_xmlOptions.cend() ? it->second.data() : nullptr;
}

// ------------------------------------------------------------------ the parts

void PDFAppToolGUIReport::describeToolbar(PDFAppToolPanelBuilder& builder)
{
    using Helper = PDFAppToolGUIReportHelper;
    using Tool = Helper::Tool;

    // tool-actions of 484:15472: Kind | Format, Dates (the panels 484:15916, 484:16401, 497:20544)
    m_kindMenu = builder.addEnumMenu(Helper::name(Tool::PARAMETER_KIND), QStringLiteral("report"), QStringLiteral("%1"), KIND_MENU_WIDTH);
    if (m_kindMenu)
    {
        m_kindMenu->getButton()->setObjectName(QStringLiteral("reportKind"));
        if (PDFTileRow* row = m_kindMenu->getRow(Helper::name(Tool::KIND_REPORT)))
        {
            row->setIconName(QStringLiteral("report"));
            row->setDescription(tr("Chosen sections as readable text, HTML or XML"));
        }
        if (PDFTileRow* row = m_kindMenu->getRow(Helper::name(Tool::KIND_STRUCTURE_XML)))
        {
            row->setIconName(QStringLiteral("braces"));
            row->setDescription(tr("Every internal object of the document as XML"));
        }
    }
    builder.addActionDivider();

    m_formatMenu = builder.addEnumMenu(Helper::name(Tool::PARAMETER_FORMAT), QStringLiteral("html"), QStringLiteral("%1"), FORMAT_MENU_WIDTH);
    if (m_formatMenu)
    {
        m_formatMenu->getButton()->setObjectName(QStringLiteral("reportFormat"));
        struct Row { const char* id; const char* title; const char* description; const char* icon; };
        const Row rows[] = {
            { PDFDocumentToolTableWriter::FORMAT_TEXT, QT_TR_NOOP("Text"), QT_TR_NOOP("Plain text with aligned tables"), "txt" },
            { PDFDocumentToolTableWriter::FORMAT_HTML, QT_TR_NOOP("HTML"), QT_TR_NOOP("For reading in a web browser"), "html" },
            { PDFDocumentToolTableWriter::FORMAT_XML, QT_TR_NOOP("XML"), QT_TR_NOOP("For processing by other programs"), "xml" },
        };
        for (const Row& item : rows)
        {
            if (PDFTileRow* row = m_formatMenu->getRow(QString::fromLatin1(item.id)))
            {
                row->setTitle(tr(item.title));
                row->setDescription(tr(item.description));
                row->setIconName(QString::fromLatin1(item.icon));
                m_formatMenu->setButtonTitle(QString::fromLatin1(item.id), tr(item.title));
            }
        }

        // The format "none" (the data only) is MCP's: the GUI always writes a file
        if (PDFTileRow* row = m_formatMenu->getRow(QString::fromLatin1(PDFDocumentToolTableWriter::FORMAT_NONE)))
        {
            row->setVisible(false);
        }
    }

    m_dateMenu = builder.addEnumMenu(Helper::name(Tool::PARAMETER_DATE_FORMAT), QStringLiteral("calendar"), QStringLiteral("%1"), DATE_MENU_WIDTH);
    if (m_dateMenu)
    {
        m_dateMenu->getButton()->setObjectName(QStringLiteral("reportDates"));
        struct Row { const char* id; const char* title; const char* button; PDFDocumentToolDateFormat format; };
        const Row rows[] = {
            { PDFDocumentToolTableWriter::DATE_FORMAT_SHORT, QT_TR_NOOP("Short"), QT_TR_NOOP("Short dates"), PDFDocumentToolDateFormat::Short },
            { PDFDocumentToolTableWriter::DATE_FORMAT_LONG, QT_TR_NOOP("Long"), QT_TR_NOOP("Long dates"), PDFDocumentToolDateFormat::Long },
            { PDFDocumentToolTableWriter::DATE_FORMAT_ISO, QT_TR_NOOP("ISO"), QT_TR_NOOP("ISO dates"), PDFDocumentToolDateFormat::Iso },
            { PDFDocumentToolTableWriter::DATE_FORMAT_RFC2822, QT_TR_NOOP("RFC 2822"), QT_TR_NOOP("RFC 2822 dates"), PDFDocumentToolDateFormat::Rfc2822 },
        };

        // 497:20544: each row shows the date of now in its format (US dates, ISO with the offset, K7.21)
        const PDFAppToolValueFormatter formatter;
        const QDateTime now = QDateTime::fromMSecsSinceEpoch(PDFAppLocale::getCurrentTime());
        for (const Row& item : rows)
        {
            if (PDFTileRow* row = m_dateMenu->getRow(QString::fromLatin1(item.id)))
            {
                row->setTitle(tr(item.title));
                row->setDescription(formatter.formatDateTime(now, item.format));
                row->setIconName(QStringLiteral("calendar"));
            }
            m_dateMenu->setButtonTitle(QString::fromLatin1(item.id), tr(item.button));
        }
    }

    updateMenus();
}

void PDFAppToolGUIReport::describeLeftPanel(PDFAppToolPanelBuilder& builder)
{
    using Helper = PDFAppToolGUIReportHelper;
    using Tool = Helper::Tool;

    // sidebar-left 484:15472: Sections / "6 of 9 included", the nine rows (a check box and a tile), Pages
    m_sectionsHeader = builder.addPanelHeader(tr("Sections"), QString());
    m_sectionsHeader->setObjectName(QStringLiteral("reportSectionsHeader"));

    m_sectionRows.clear();
    m_sectionsList = new QWidget(builder.getWidget());
    m_sectionsList->setObjectName(QStringLiteral("reportSections"));
    QVBoxLayout* listLayout = new QVBoxLayout(m_sectionsList);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(0);
    for (const PDFDocumentReportSection section : PDFDocumentReport::getSections())
    {
        PDFTileRow* row = new PDFTileRow(getSectionTitle(section), QString(), getSectionIconName(section), PDFTileRow::Kind::ListItem, m_sectionsList);
        row->setObjectName(QStringLiteral("reportSection-") + Helper::sectionName(section));
        row->setProperty(Helper::SECTION_PROPERTY, int(section));
        row->setCheckBoxVisible(true);
        row->setSelectionBehavior(PDFComponentButton::SelectionBehavior::Exclusive);
        connect(row, &PDFTileRow::checkBoxToggled, this, &PDFAppToolGUIReport::onSectionCheckBoxToggled);
        connect(row, &PDFTileRow::clicked, this, &PDFAppToolGUIReport::onSectionRowClicked);
        listLayout->addWidget(row);
        m_sectionRows[section] = row;
    }
    builder.addWidget(m_sectionsList);

    m_pageSelection = builder.addPageSelection(Helper::name(Tool::PARAMETER_PAGES), tr("Pages"));
    if (m_pageSelection)
    {
        m_pageSelection->setObjectName(QStringLiteral("reportPages"));
        connect(m_pageSelection.data(), &PDFPageSelection::rangeChanged, this, &PDFAppToolGUIReport::updateRightPanel);

        // 484:20021 "The page selection is not valid.": a range being written never reaches the model (its binder
        // keeps only the input error), so the status is said again here - the last connection of the signal
        connect(m_pageSelection.data(), &PDFPageSelection::rangeChanged, this, &PDFAppToolGUIReport::updateStatusText);
    }

    // 484:18196: Structure as XML / The whole internal structure, the text, the callout "The file can be large"
    m_xmlHeader = builder.addPanelHeader(tr("Structure as XML"), tr("The whole internal structure"));
    m_xmlHeader->setObjectName(QStringLiteral("reportXmlHeader"));
    m_xmlText = builder.addNote(QString());
    m_xmlText->setObjectName(QStringLiteral("reportXmlText"));
    m_xmlCallout = builder.addCallout(PDFAccentHue::Blue, QStringLiteral("info"), tr("The file can be large"), tr("With stream data included the XML is several times bigger than the PDF."));
    m_xmlCallout->setObjectName(QStringLiteral("reportXmlCallout"));

    updateLeftPanel();
}

void PDFAppToolGUIReport::describeRightPanel(PDFAppToolPanelBuilder& builder)
{
    using Helper = PDFAppToolGUIReportHelper;

    // sidebar-right 484:15472: Output (Kind, Format, Dates, Sections, Pages), the character maps of the fonts
    // (497:22345), the note; for XML (484:18196): Output (Kind, Objects), XML options with its note
    m_outputHeader = builder.addSection(tr("Output"));
    m_outputRows = builder.addKeyValues();
    m_outputRows->setObjectName(QStringLiteral("reportOutput"));
    builder.endSection();

    m_characterMapsCheckBox = new QCheckBox(tr("Include character maps of the fonts"), builder.getWidget());
    m_characterMapsCheckBox->setObjectName(QStringLiteral("reportCharacterMaps"));
    connect(m_characterMapsCheckBox.data(), &QCheckBox::toggled, this, &PDFAppToolGUIReport::onCharacterMapsToggled);
    builder.addWidget(m_characterMapsCheckBox);

    m_reportHint = builder.addHint(tr("The page selection applies to JavaScript, named destinations and page boxes. The other sections describe the whole document."));
    m_reportHint->setObjectName(QStringLiteral("reportPagesHint"));

    m_xmlOptionsHeader = builder.addSection(tr("XML options"));
    builder.endSection();
    m_xmlOptionsCard = new PDFAppToolCard(builder.getWidget());
    m_xmlOptionsCard->setObjectName(QStringLiteral("reportXmlOptions"));
    m_xmlOptionsCard->getBodyLayout()->setContentsMargins(13, 9, 13, 9);
    m_xmlOptionsCard->getBodyLayout()->setSpacing(4);
    m_xmlOptions.clear();
    struct Option { const char* id; const char* title; };
    const Option options[] = {
        { Helper::OPTION_HEX, QT_TR_NOOP("Export streams as hexadecimal data") },
        { Helper::OPTION_TEXT, QT_TR_NOOP("Export streams as text, if possible") },
        { Helper::OPTION_INDENT, QT_TR_NOOP("Indent the XML") },
        { Helper::OPTION_BINARY, QT_TR_NOOP("Never convert strings to text") },
    };
    for (const Option& option : options)
    {
        QCheckBox* checkBox = new QCheckBox(tr(option.title), m_xmlOptionsCard);
        checkBox->setObjectName(QStringLiteral("reportXmlOption-") + QString::fromLatin1(option.id));
        checkBox->setProperty(Helper::OPTION_PROPERTY, QString::fromLatin1(option.id));
        connect(checkBox, &QCheckBox::toggled, this, &PDFAppToolGUIReport::onXmlOptionToggled);
        m_xmlOptionsCard->getBodyLayout()->addWidget(checkBox);
        m_xmlOptions[QString::fromLatin1(option.id)] = checkBox;
    }
    PDFAppParagraph* optionsNote = PDFAppPageWidgets::createText(tr("Stream data are left out unless one of the first two options is on."), PDFThemeFonts::getFont(QFont::Normal, 11), 13,
                                                                 PDFThemeColors::TextMuted, m_xmlOptionsCard);
    optionsNote->setMinimumWidth(1);
    m_xmlOptionsCard->getBodyLayout()->addWidget(optionsNote);
    builder.addWidget(m_xmlOptionsCard);
    updateRightPanel();
}

QWidget* PDFAppToolGUIReport::createCanvasPage(QWidget* parent)
{
    // document-canvas 484:15472: bg/track, padding 20, the white card of the preview; 484:20937 the message
    m_message = new PDFAppToolCanvasMessage(parent);
    m_message->setObjectName(QStringLiteral("reportCanvas"));
    QVBoxLayout* layout = new QVBoxLayout(m_message);
    layout->setContentsMargins(0, 0, 0, 0);

    m_previewArea = new QScrollArea(m_message);
    m_previewArea->setFrameShape(QFrame::NoFrame);
    m_previewArea->setProperty("overlayScrollBars", true);
    m_previewArea->viewport()->setAutoFillBackground(false);
    m_previewArea->setWidgetResizable(true);
    layout->addWidget(m_previewArea);

    QWidget* content = new QWidget(m_previewArea);
    content->setAutoFillBackground(false);
    QVBoxLayout* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(20, 20, 20, 20);
    m_preview = new PDFAppReportPreview(content);
    contentLayout->addWidget(m_preview);
    contentLayout->addStretch(1);
    m_previewArea->setWidget(content);

    updatePreviewModel();
    updateCanvas();
    schedulePreview();
    return m_message.data();
}

// ------------------------------------------------------------------ the texts of the shell

QString PDFAppToolGUIReport::getCancelText() const
{
    return tr("Close");
}

QString PDFAppToolGUIReport::getConfirmText() const
{
    // 484:15472 Create Report, 484:18196 Export XML
    return isStructureXml() ? tr("Export XML") : tr("Create Report");
}

QString PDFAppToolGUIReport::getConfirmIconName() const
{
    return isStructureXml() ? QStringLiteral("code") : QStringLiteral("report");
}

bool PDFAppToolGUIReport::isConfirmEnabled() const
{
    // 484:20937: nothing checked = Disabled; 484:20021: an invalid range = Disabled (the input error)
    if (!BaseClass::isConfirmEnabled())
    {
        return false;
    }
    if (isStructureXml())
    {
        return true;
    }
    return !getIncludedSections().empty() && (!m_pageSelection || m_pageSelection->isValid());
}

QString PDFAppToolGUIReport::getStatusText() const
{
    const PDFDocumentToolShell* shell = getShell();
    if (shell && shell->isPermissionRefused())
    {
        return BaseClass::getStatusText();
    }

    // 484:18196, 484:20937, 484:20021, 484:20505, 484:15472
    if (isStructureXml())
    {
        return tr("The whole internal structure will be exported as XML.");
    }
    const std::vector<PDFDocumentReportSection> sections = getIncludedSections();
    if (sections.empty())
    {
        return tr("No section is checked.");
    }
    if (m_pageSelection && !m_pageSelection->isValid())
    {
        return tr("The page selection is not valid.");
    }
    if (!isSectionAvailable(m_currentSection) && m_currentSection == PDFDocumentReportSection::StructureTree)
    {
        return tr("This document has no structure tree; the section is left out.");
    }
    if (!isSectionAvailable(m_currentSection))
    {
        return tr("%1: %2").arg(getSectionTitle(m_currentSection), m_facts->emptyReasons.at(m_currentSection));
    }
    return tr("The report will have %Ln sections. Create Report puts it together and asks for the file.", nullptr, int(sections.size()));
}

QString PDFAppToolGUIReport::getRunningStatusText(const QString& stepText) const
{
    // 484:18372 "Creating the report… Stop ends it, nothing is written."
    Q_UNUSED(stepText);
    return isStructureXml() ? tr("Exporting the XML… Stop ends it, nothing is written.") : tr("Creating the report… Stop ends it, nothing is written.");
}

QString PDFAppToolGUIReport::getRunningTitle(int percentage) const
{
    Q_UNUSED(percentage);
    return isStructureXml() ? tr("Exporting the XML") : tr("Creating the report");
}

std::vector<PDFSecurityHandler::Permission> PDFAppToolGUIReport::getRunPermissions() const
{
    // Q7.2: JavaScript, XMP and the data of the streams are content of the document (bit 5); the shell asks it
    // with the owner password offered (the validation of the core refuses the run anyway)
    const PDFDocumentToolReport tool;
    return tool.getRequiredPermissions(getRunParameters());
}

QVariantMap PDFAppToolGUIReport::getRunParameters() const
{
    // A section the document has nothing of is left out (484:20505 "the section is left out")
    QVariantMap parameters = BaseClass::getRunParameters();
    for (const PDFDocumentReportSection section : PDFDocumentReport::getSections())
    {
        if (!isSectionAvailable(section))
        {
            parameters.insert(PDFAppToolGUIReportHelper::sectionName(section), false);
        }
    }
    return parameters;
}

void PDFAppToolGUIReport::fillResultInput(PDFAppToolResultInput* input) const
{
    using Tool = PDFDocumentToolReport;

    // 484:18811 "The report is ready" (Sections, Format, Size; "Save Report", "Open the report when it is saved");
    // 484:19793 "The XML export is ready" (Objects, Streams, Size)
    const PDFAppToolParameterModel* model = getModel();
    const QVariantMap values = model ? model->getValues() : QVariantMap();
    const qint64 size = input->result.outputs.size() == 1 ? input->result.outputs.front().getSize() : -1;
    const QString sizeText = size >= 0 ? tr("about %1").arg(PDFAppLocale::formatDataSize(size, 0, QLocale::DataSizeTraditionalFormat)) : QStringLiteral("–");

    std::vector<PDFDocumentToolFact> facts;
    if (isStructureXml())
    {
        input->title = tr("The XML export is ready");
        input->statusText = tr("The XML export is ready to be saved.");
        const qint64 objects = m_facts ? m_facts->objectCount : -1;
        facts.push_back({ QStringLiteral("objects"), tr("Objects"), objects >= 0 ? tr("%1 and the trailer").arg(PDFAppLocale::formatNumber(objects)) : QStringLiteral("–") });
        const bool isExported = values.value(QLatin1String(Tool::PARAMETER_EXPORT_STREAMS)).toBool();
        const bool isText = values.value(QLatin1String(Tool::PARAMETER_STREAMS_AS_TEXT)).toBool();
        facts.push_back({ QStringLiteral("streams"), tr("Streams"), !isExported ? tr("Not included") : (isText ? tr("As text, if possible") : tr("As hexadecimal data")) });
        facts.push_back({ QStringLiteral("size"), tr("Size"), sizeText });
    }
    else
    {
        input->title = tr("The report is ready");
        input->statusText = tr("The report is ready to be saved.");
        input->fileConfirmText = tr("Save Report");
        input->fileOpenText = tr("Open the report when it is saved");
        facts.push_back({ QStringLiteral("sections"), tr("Sections"), tr("%1 of %2").arg(getIncludedSections().size()).arg(PDFDocumentReport::getSections().size()) });
        facts.push_back({ QStringLiteral("format"), tr("Format"), PDFAppToolGUIReportHelper::getFormatTitle(values) });
        facts.push_back({ QStringLiteral("size"), tr("Size"), sizeText });
    }
    input->result.facts = std::move(facts);
}

void PDFAppToolGUIReport::onResultKept(const PDFDocumentToolResult& result)
{
    m_result = result;
}

bool PDFAppToolGUIReport::isResultStaleBy(const QString& parameterName) const
{
    // Every setting is a part of the file of the report (also its format and its dates)
    Q_UNUSED(parameterName);
    return true;
}

QString PDFAppToolGUIReport::getStaleTitle() const
{
    return tr("The report is out of date");
}

QString PDFAppToolGUIReport::getStaleText() const
{
    return tr("The document or the settings changed since the report was created. Press %1 to create it again.").arg(getConfirmText());
}

void PDFAppToolGUIReport::onExportDecided(const PDFAppToolResultOutcome& outcome)
{
    // 484:18811 "Open the report when it is saved": the program of the system for the file (the last statement)
    if (outcome.action == PDFAppToolResultOutcome::Action::FileSaved && outcome.isFileOpenRequested && !outcome.filePath.isEmpty())
    {
        if (s_fileOpener)
        {
            s_fileOpener(outcome.filePath);
        }
        else
        {
            QDesktopServices::openUrl(QUrl::fromLocalFile(outcome.filePath));
        }
    }
}

// ------------------------------------------------------------------ the preview

QString PDFAppToolGUIReport::createXmlPreview(const PDFDocument* document, const PDFDocumentXmlExport::Options& options, const PDFOperationControl* job)
{
    // The first lines of the XML (a device, which stops the export after XML_PREVIEW_BYTES; the cancellation of
    // the job stops it too)
    PDFAppReportXmlPreviewControl control(job);
    PDFAppReportXmlPreviewDevice device(&control);
    device.open(QIODevice::WriteOnly);
    PDFDocumentXmlExport::write(document, options, &device, &control, nullptr, nullptr);
    QStringList lines = QString::fromUtf8(device.getData()).split(QLatin1Char('\n'));
    const bool isCut = device.isFull() || lines.size() > XML_PREVIEW_LINES;
    lines = lines.mid(0, XML_PREVIEW_LINES);
    if (isCut)
    {
        lines << QStringLiteral("…");
    }
    return lines.join(QLatin1Char('\n'));
}

PDFAppToolGUIReport::PreviewResult PDFAppToolGUIReport::previewJob(PreviewRequest request, std::shared_ptr<PDFAppCancelFlag> cancel)
{
    // A thread of the pool: the shared immutable document and copies of the options (no object of the GUI)
    PreviewResult result;
    result.generation = request.generation;
    const PDFDocument* document = request.document.data();
    if (!document)
    {
        return result;
    }

    if (request.isFactsRequested)
    {
        Facts facts;
        std::vector<PDFInteger> allPages(document->getCatalog()->getPageCount());
        for (size_t i = 0; i < allPages.size(); ++i)
        {
            allPages[i] = PDFInteger(i);
        }
        for (const PDFDocumentReportSection section : PDFDocumentReport::getSections())
        {
            facts.emptyReasons[section] = PDFDocumentReport::getEmptySectionReason(document, section, !request.options.originalData.isEmpty());
        }
        const PDFJavaScriptScanner scanner(document);
        const PDFJavaScriptScanner::Result scripts = scanner.scanDocument(allPages, PDFJavaScriptScanner::Options(PDFJavaScriptScanner::ScanMask | PDFJavaScriptScanner::AllPages), cancel.get());
        facts.scriptCount = int(scripts.entries.size());
        facts.destinationCount = int(PDFDocumentReport::getNamedDestinations(document, allPages).size());
        facts.processInkCount = 0;
        facts.spotInkCount = 0;
        for (const PDFDocumentReportInk& ink : PDFDocumentReport::getInks(document, cancel.get()))
        {
            ++(ink.isSpot ? facts.spotInkCount : facts.processInkCount);
        }
        facts.objectCount = 0;
        for (const PDFObjectStorage::Entry& entry : document->getStorage().getObjects())
        {
            if (!entry.object.isNull())
            {
                ++facts.objectCount;
            }
        }
        // The request was made before the facts were known (the first preview of a version): a section the
        // document has nothing of is left out of this preview as well (484:20505 "the section is left out"),
        // as getIncludedSections leaves it out of the run
        std::vector<PDFDocumentReportSection> availableSections;
        for (const PDFDocumentReportSection section : request.options.sections)
        {
            if (facts.emptyReasons[section].isEmpty())
            {
                availableSections.push_back(section);
            }
        }
        request.options.sections = std::move(availableSections);

        if (!PDFOperationControl::isOperationCanceled(cancel.get()))
        {
            result.facts = std::move(facts);
        }
    }

    if (request.isStructureXml)
    {
        PDFDocumentXmlExport::Options options;
        options.isStreamExported = request.isStreamExported;
        options.isStreamWrittenAsText = request.isStreamWrittenAsText;
        options.isIndented = request.isIndented;
        options.isStringAlwaysBinary = request.isStringAlwaysBinary;
        result.xmlText = createXmlPreview(document, options, cancel.get());
    }
    else if (!request.options.sections.empty())
    {
        result.data = PDFDocumentReport::createData(document, request.options, cancel.get(), nullptr);
    }

    result.isCanceled = PDFOperationControl::isOperationCanceled(cancel.get());
    return result;
}

bool PDFAppToolGUIReport::isPreviewPending() const
{
    return (m_previewTimer && m_previewTimer->isActive()) || m_previewWatcher != nullptr;
}

void PDFAppToolGUIReport::cancelPreview()
{
    if (m_previewTimer)
    {
        m_previewTimer->stop();
    }
    if (m_previewCancel)
    {
        m_previewCancel->cancel();
        m_previewCancel.reset();
    }
    if (m_previewWatcher)
    {
        // The late result is dropped: the watcher goes, the job ends by itself
        m_previewWatcher->disconnect(this);
        m_previewWatcher->deleteLater();
        m_previewWatcher = nullptr;
    }
}

void PDFAppToolGUIReport::schedulePreview()
{
    // A short rest after the last change; a running job of older values is canceled
    cancelPreview();
    if (m_previewTimer)
    {
        m_previewTimer->start();
    }
}

void PDFAppToolGUIReport::onPreviewTimeout()
{
    startPreview();
}

void PDFAppToolGUIReport::startPreview()
{
    using Helper = PDFAppToolGUIReportHelper;
    using Tool = Helper::Tool;

    cancelPreview();
    const PDFAppDocument* document = getDocument();
    const PDFAppToolParameterModel* model = getModel();
    if (!document || !document->getDocument() || !model || !m_preview)
    {
        return;
    }

    PreviewRequest request;
    request.generation = ++m_previewGeneration;
    request.document = document->getDocument();
    request.isFactsRequested = m_factsVersion != document->getVersion() + 1;
    request.isStructureXml = isStructureXml();
    request.isStreamExported = model->getValue(Helper::name(Tool::PARAMETER_EXPORT_STREAMS)).toBool();
    request.isStreamWrittenAsText = model->getValue(Helper::name(Tool::PARAMETER_STREAMS_AS_TEXT)).toBool();
    request.isIndented = model->getValue(Helper::name(Tool::PARAMETER_INDENT)).toBool();
    request.isStringAlwaysBinary = model->getValue(Helper::name(Tool::PARAMETER_ALWAYS_BINARY_STRINGS)).toBool();
    request.options.sections = getIncludedSections();
    bool isValid = true;
    request.options.pageIndices = getPageIndices(&isValid);
    request.options.isCharacterMapIncluded = model->getValue(Helper::name(Tool::PARAMETER_CHARACTER_MAPS)).toBool();
    request.options.fileName = document->getTitle();
    if (!document->isModified() && !document->getFilePath().isEmpty())
    {
        // FD-2: the hashes are of the file; unsaved changes have no file yet
        request.options.originalData = document->getOriginalData();
    }

    // One asynchronous job (CLAUDE.md: QtConcurrent for one job); a new request replaces it
    m_previewCancel = std::make_shared<PDFAppCancelFlag>();
    m_previewWatcher = new QFutureWatcher<PreviewResult>(this);
    connect(m_previewWatcher.data(), &QFutureWatcherBase::finished, this, &PDFAppToolGUIReport::onPreviewFinished);
    m_previewWatcher->setFuture(QtConcurrent::run(&PDFAppToolGUIReport::previewJob, std::move(request), m_previewCancel));
}

void PDFAppToolGUIReport::onPreviewFinished()
{
    QFutureWatcher<PreviewResult>* watcher = m_previewWatcher.data();
    if (!watcher || sender() != watcher)
    {
        return;
    }

    const PreviewResult result = watcher->result();
    m_previewWatcher = nullptr;
    watcher->deleteLater();
    m_previewCancel.reset();
    if (result.generation != m_previewGeneration || result.isCanceled)
    {
        return;
    }

    if (result.facts)
    {
        const PDFAppDocument* document = getDocument();
        m_facts = result.facts;
        m_factsVersion = document ? document->getVersion() + 1 : 0;
    }
    m_previewData = result.data;
    m_xmlPreview = result.xmlText;
    updatePreviewModel();
    updateAll();
}

void PDFAppToolGUIReport::updatePreviewModel()
{
    using Tool = PDFDocumentToolReport;

    if (!m_preview)
    {
        return;
    }

    // The model of the data in the GUI thread: the numbers and the dates by PDFAppLocale (O17)
    const PDFAppToolParameterModel* model = getModel();
    const PDFDocumentToolDateFormat dateFormat = PDFDocumentToolTableWriter::getDateFormat(model ? model->getValue(QString::fromLatin1(Tool::PARAMETER_DATE_FORMAT)).toString() : QString());
    if (isStructureXml())
    {
        PDFDocumentToolTableSection section;
        section.addParagraph(tr("Preview of the first lines"));
        if (!m_xmlPreview.isEmpty())
        {
            section.addCode(m_xmlPreview);
        }
        m_preview->setReport(section, dateFormat);
        return;
    }

    const PDFAppToolValueFormatter formatter;
    PDFDocumentToolTableSection report = PDFDocumentReport::createModel(m_previewData, &formatter, dateFormat);
    if (const PDFAppDocument* document = getDocument())
    {
        // 484:15472 "Report on Project_Report.pdf"
        report.title = tr("Report on %1").arg(document->getTitle());
    }
    m_preview->setReport(report, dateFormat);
}

std::vector<PDFInteger> PDFAppToolGUIReport::getPageIndices(bool* isValid) const
{
    using Tool = PDFDocumentToolReport;

    *isValid = true;
    std::vector<PDFInteger> pages;
    const PDFAppDocument* appDocument = getDocument();
    const PDFDocument* document = appDocument && appDocument->getDocument() ? appDocument->getDocument().data() : nullptr;
    const PDFAppToolParameterModel* model = getModel();
    if (!document || !model)
    {
        return pages;
    }

    QString errorMessage;
    const std::optional<PDFDocumentToolPageSelection> selection = PDFDocumentToolPageSelection::fromVariant(model->getValue(QString::fromLatin1(Tool::PARAMETER_PAGES)), &errorMessage);
    if (selection)
    {
        pages = selection->getPageIndices(document, &errorMessage);
    }
    if (!selection || !errorMessage.isEmpty())
    {
        // An invalid range: the preview shows all pages (Create Report is Disabled)
        *isValid = false;
        pages.resize(document->getCatalog()->getPageCount());
        for (size_t i = 0; i < pages.size(); ++i)
        {
            pages[i] = PDFInteger(i);
        }
    }
    return pages;
}

// ------------------------------------------------------------------ the updates

void PDFAppToolGUIReport::updateAll()
{
    updateMenus();
    updateRightPanel();
    updateCanvas();
    if (!updateLeftPanel())
    {
        return;
    }

    // The slots of the status and of the Primary button may retire this GUI: the last things
    updateConfirmState();
    if (!PDFDeferredDeletion::isRetired(this))
    {
        updateStatusText();
    }
}

void PDFAppToolGUIReport::updateMenus()
{
    // 484:18196: Format and Dates are Disabled for Structure as XML; the kind button with its icon
    const bool isXml = isStructureXml();
    if (m_kindMenu && m_kindMenu->getButton())
    {
        m_kindMenu->getButton()->setIcon(PDFThemeIcons::getIcon(isXml ? QStringLiteral("braces") : QStringLiteral("report")));
    }
    if (m_formatMenu && m_formatMenu->getButton())
    {
        m_formatMenu->getButton()->setEnabled(!isXml && !m_isRunning);
    }
    if (m_dateMenu && m_dateMenu->getButton())
    {
        m_dateMenu->getButton()->setEnabled(!isXml && !m_isRunning);
    }
}

QString PDFAppToolGUIReport::getSectionDescription(PDFDocumentReportSection section) const
{
    // 484:15472: the counts of the document where they are known; a section the document has nothing of says why
    if (m_facts)
    {
        auto it = m_facts->emptyReasons.find(section);
        if (it != m_facts->emptyReasons.cend() && !it->second.isEmpty())
        {
            return it->second;
        }
    }

    switch (section)
    {
        case PDFDocumentReportSection::BasicInformation:
            return tr("Title, author, dates, version, security");
        case PDFDocumentReportSection::FileHashes:
            return tr("MD5, SHA1, SHA256, SHA384, SHA512");
        case PDFDocumentReportSection::Metadata:
            return tr("The embedded XML metadata");
        case PDFDocumentReportSection::JavaScript:
            return m_facts && m_facts->scriptCount >= 0 ? tr("%Ln scripts in the document", nullptr, m_facts->scriptCount) : tr("The scripts of the document, as a text");
        case PDFDocumentReportSection::NamedDestinations:
            return m_facts && m_facts->destinationCount >= 0 ? tr("%Ln destinations", nullptr, m_facts->destinationCount) : tr("The named targets of links");
        case PDFDocumentReportSection::PageBoxes:
            return tr("Media, crop, bleed, trim and art box");
        case PDFDocumentReportSection::StructureTree:
            return tr("Tags of the tagged document");
        case PDFDocumentReportSection::Inks:
            return m_facts && m_facts->processInkCount >= 0 ? tr("%1 process, %2 spot").arg(m_facts->processInkCount).arg(m_facts->spotInkCount) : tr("Process and spot inks");
        case PDFDocumentReportSection::Fonts:
            return tr("Used fonts and how they are embedded");
    }
    return QString();
}

bool PDFAppToolGUIReport::updateLeftPanel()
{
    const bool isXml = isStructureXml();
    const PDFAppToolParameterModel* model = getModel();
    if (m_sectionsHeader)
    {
        // "6 of 9 included" (484:15472), "5 of 9" with a Disabled section (484:20505), "0 of 9" (484:20937)
        m_sectionsHeader->setTexts(tr("Sections"), tr("%1 of %2 included").arg(getIncludedSections().size()).arg(PDFDocumentReport::getSections().size()));
        m_sectionsHeader->setVisible(!isXml);
    }
    if (m_sectionsList)
    {
        m_sectionsList->setVisible(!isXml);
    }
    if (m_pageSelection)
    {
        m_pageSelection->setVisible(!isXml);
    }

    if (m_xmlHeader)
    {
        m_xmlHeader->setVisible(isXml);
    }
    if (m_xmlText)
    {
        // 484:18196 "Every object of the document is written as an XML element: the trailer, then all 10,275 objects …"
        const qint64 objects = m_facts ? m_facts->objectCount : -1;
        m_xmlText->setText(objects >= 0 ? tr("Every object of the document is written as an XML element: the trailer, then all %1 objects with their dictionaries, arrays and values. Use it to inspect or compare documents with other tools.").arg(PDFAppLocale::formatNumber(objects))
                                        : tr("Every object of the document is written as an XML element: the trailer, then all objects with their dictionaries, arrays and values. Use it to inspect or compare documents with other tools."));
        m_xmlText->setVisible(isXml);
    }
    if (m_xmlCallout)
    {
        m_xmlCallout->setVisible(isXml);
    }

    // The rows last: the check box of a row emits its toggle (the signals of the row are blocked, not those of its
    // check box) - a slot of any signal may delete this GUI, so the rows are copies and the guard is checked
    const std::map<PDFDocumentReportSection, QPointer<PDFTileRow>> rows = m_sectionRows;
    const PDFDocumentReportSection currentSection = m_currentSection;
    const bool isRunning = m_isRunning;
    for (const auto& item : rows)
    {
        PDFTileRow* row = item.second.data();
        if (!row)
        {
            continue;
        }

        const bool isAvailable = isSectionAvailable(item.first);
        const QSignalBlocker blocker(row);
        row->setDescription(getSectionDescription(item.first));
        row->setChecked(item.first == currentSection);
        row->setCheckBoxEnabled(isAvailable && !isRunning);
        row->setCheckBoxChecked(isAvailable && model && model->getValue(PDFAppToolGUIReportHelper::sectionName(item.first)).toBool());
        if (PDFDeferredDeletion::isRetired(this))
        {
            return false;
        }
    }
    return true;
}

QString PDFAppToolGUIReport::getPagesText() const
{
    // 484:15472 "All 24", 484:20021 "Invalid"
    if (!m_pageSelection)
    {
        return QStringLiteral("–");
    }
    if (!m_pageSelection->isValid())
    {
        return tr("Invalid");
    }
    const qint64 count = m_pageSelection->getResultCount();
    if (count == m_pageSelection->getPageCount())
    {
        return tr("All %1").arg(PDFAppLocale::formatNumber(count));
    }
    return tr("%Ln pages", nullptr, int(qMin<qint64>(count, (std::numeric_limits<int>::max)())));
}

void PDFAppToolGUIReport::updateRightPanel()
{
    using Helper = PDFAppToolGUIReportHelper;
    using Tool = Helper::Tool;

    const bool isXml = isStructureXml();
    const PDFAppToolParameterModel* model = getModel();
    if (m_outputRows)
    {
        m_outputRows->clear();
        m_outputRows->addRow(tr("Kind"), Helper::getKindTitle(model));
        if (isXml)
        {
            const qint64 objects = m_facts ? m_facts->objectCount : -1;
            m_outputRows->addRow(tr("Objects"), objects >= 0 ? PDFAppLocale::formatNumber(objects) : QStringLiteral("…"));
        }
        else
        {
            const size_t count = getIncludedSections().size();
            m_outputRows->addRow(tr("Format"), Helper::getFormatTitle(model ? model->getValues() : QVariantMap()));
            const QString dates = model ? model->getValue(Helper::name(Tool::PARAMETER_DATE_FORMAT)).toString() : QString();
            const PDFTileRow* dateRow = m_dateMenu ? m_dateMenu->getRow(dates) : nullptr;
            m_outputRows->addRow(tr("Dates"), dateRow ? dateRow->getTitle() : dates);
            m_outputRows->addRow(tr("Sections"), count == 0 ? tr("None") : tr("%1 of %2").arg(count).arg(PDFDocumentReport::getSections().size()));
            m_outputRows->addRow(tr("Pages"), getPagesText());
        }
    }

    // 497:22345: the character maps only with the section Fonts
    const bool hasFonts = model && model->getValue(Helper::sectionName(PDFDocumentReportSection::Fonts)).toBool();
    if (m_characterMapsCheckBox)
    {
        const QSignalBlocker blocker(m_characterMapsCheckBox.data());
        m_characterMapsCheckBox->setChecked(model && model->getValue(Helper::name(Tool::PARAMETER_CHARACTER_MAPS)).toBool());
        m_characterMapsCheckBox->setVisible(!isXml && hasFonts);
    }
    if (m_reportHint)
    {
        m_reportHint->setVisible(!isXml);
    }

    if (m_xmlOptionsHeader)
    {
        m_xmlOptionsHeader->setVisible(isXml);
    }
    if (m_xmlOptionsCard)
    {
        m_xmlOptionsCard->setVisible(isXml);
    }
    const bool isExported = model && model->getValue(Helper::name(Tool::PARAMETER_EXPORT_STREAMS)).toBool();
    const bool isText = model && model->getValue(Helper::name(Tool::PARAMETER_STREAMS_AS_TEXT)).toBool();
    const std::pair<const char*, bool> states[] = {
        { Helper::OPTION_HEX, isExported && !isText },
        { Helper::OPTION_TEXT, isExported && isText },
        { Helper::OPTION_INDENT, model && model->getValue(Helper::name(Tool::PARAMETER_INDENT)).toBool() },
        { Helper::OPTION_BINARY, model && model->getValue(Helper::name(Tool::PARAMETER_ALWAYS_BINARY_STRINGS)).toBool() },
    };
    for (const auto& state : states)
    {
        if (QCheckBox* checkBox = getXmlOption(state.first))
        {
            const QSignalBlocker blocker(checkBox);
            checkBox->setChecked(state.second);
            checkBox->setEnabled(!m_isRunning);
        }
    }
}

void PDFAppToolGUIReport::updateCanvas()
{
    // 484:20937: nothing checked - "Check at least one section"; 484:18372: dimmed while the report is created
    const bool isEmpty = !isStructureXml() && getIncludedSections().empty();
    if (m_previewArea)
    {
        m_previewArea->setVisible(!isEmpty);
    }
    if (m_message)
    {
        if (isEmpty)
        {
            m_message->setMessage(QStringLiteral("report"), tr("Check at least one section"), tr("The report is put together from the sections checked on the left."));
        }
        else
        {
            m_message->setMessage(QString(), QString(), QString());
        }
    }
    if (m_preview)
    {
        m_preview->setDimmed(m_isRunning);
    }
}

void PDFAppToolGUIReport::setCurrentSection(PDFDocumentReportSection section)
{
    m_currentSection = section;

    // The preview shows the section (484:16862 … 497:22345)
    if (m_preview && m_previewArea)
    {
        const int top = m_preview->getSectionTop(PDFDocumentReport::getSectionId(section));
        if (top >= 0)
        {
            // The scroll bar emits its value (a slot may retire this GUI)
            m_previewArea->verticalScrollBar()->setValue(m_preview->mapTo(m_previewArea->widget(), QPoint(0, top)).y() - 20);
            if (PDFDeferredDeletion::isRetired(this))
            {
                return;
            }
        }
    }
    updateAll();
}

// ------------------------------------------------------------------ the events

void PDFAppToolGUIReport::onParametersChanged(const QString& name)
{
    using Tool = PDFDocumentToolReport;

    // The format and the dates only change the model of the shown data; the rest is computed again
    const bool isModelOnly = name == QLatin1String(Tool::PARAMETER_FORMAT) || name == QLatin1String(Tool::PARAMETER_DATE_FORMAT);
    if (isModelOnly)
    {
        updatePreviewModel();
    }
    else
    {
        schedulePreview();
    }
    updateAll();
}

void PDFAppToolGUIReport::onRunStateChanged(bool isRunning)
{
    m_isRunning = isRunning;
    updateAll();
}

void PDFAppToolGUIReport::onDocumentChanged()
{
    // A new version: the facts and the preview are of the old one (the shell marks a kept result stale)
    schedulePreview();
    updateAll();
}

void PDFAppToolGUIReport::onSectionRowClicked()
{
    QObject* row = sender();
    if (row)
    {
        setCurrentSection(static_cast<PDFDocumentReportSection>(row->property(PDFAppToolGUIReportHelper::SECTION_PROPERTY).toInt()));
    }
}

void PDFAppToolGUIReport::onSectionCheckBoxToggled(bool isChecked)
{
    QObject* row = sender();
    PDFAppToolParameterModel* model = getModel();
    if (!row || !model)
    {
        return;
    }

    // The slots of the model run: the last statement
    const PDFDocumentReportSection section = static_cast<PDFDocumentReportSection>(row->property(PDFAppToolGUIReportHelper::SECTION_PROPERTY).toInt());
    m_currentSection = section;
    model->setValue(PDFAppToolGUIReportHelper::sectionName(section), isChecked);
}

void PDFAppToolGUIReport::onCharacterMapsToggled(bool isChecked)
{
    if (PDFAppToolParameterModel* model = getModel())
    {
        model->setValue(QString::fromLatin1(PDFDocumentToolReport::PARAMETER_CHARACTER_MAPS), isChecked);
    }
}

void PDFAppToolGUIReport::onXmlOptionToggled(bool isChecked)
{
    using Helper = PDFAppToolGUIReportHelper;
    using Tool = Helper::Tool;

    QObject* checkBox = sender();
    PDFAppToolParameterModel* model = getModel();
    if (!checkBox || !model)
    {
        return;
    }

    // The two check boxes of the streams are one choice (484:18196); the others are their parameters. The slots
    // of the model run: the last statements
    const QString option = checkBox->property(Helper::OPTION_PROPERTY).toString();
    if (option == QLatin1String(Helper::OPTION_HEX))
    {
        model->setValues(getStreamValues(isChecked, false));
    }
    else if (option == QLatin1String(Helper::OPTION_TEXT))
    {
        model->setValues(getStreamValues(false, isChecked));
    }
    else if (option == QLatin1String(Helper::OPTION_INDENT))
    {
        model->setValue(Helper::name(Tool::PARAMETER_INDENT), isChecked);
    }
    else if (option == QLatin1String(Helper::OPTION_BINARY))
    {
        model->setValue(Helper::name(Tool::PARAMETER_ALWAYS_BINARY_STRINGS), isChecked);
    }
}

}   // namespace pdf

#include "moc_pdfapptoolgui_report.cpp"
