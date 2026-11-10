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

#include "pdfapptoolgui_split.h"

#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentregistry.h"
#include "pdfapplocale.h"
#include "pdfappsettingcontrol.h"
#include "pdfapptooloutputfiles.h"
#include "pdfapptoolpanelbuilder.h"
#include "pdfapptoolparametermodel.h"
#include "pdfdocumenttoolguiregistry.h"

#include "pdfcatalog.h"
#include "pdfdocumentpageoperations.h"
#include "pdfdocumenttool_merge.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfpagerangelist.h"

#include <QFileInfo>

#include <algorithm>

namespace pdf
{

/// Helpers of Split and Extract Pages
class PDFAppToolGUISplitHelper
{
public:
    PDFAppToolGUISplitHelper() = delete;

    static QString name(const char* parameter)
    {
        return QString::fromLatin1(parameter);
    }

    /// The tiles of the files (383:3315: 85 x 128; 385:9405: 121 x 194 - the page of the tile)
    static constexpr QSize SPLIT_SLOT = QSize(85, 128);
    static constexpr QSize EXTRACT_SLOT = QSize(121, 194);

    /// Widths of the panels of the menus of the toolbar (383:4447)
    static constexpr int RULE_PANEL_WIDTH = 300;
    static constexpr int OPTIMIZE_PANEL_WIDTH = 270;
    static constexpr int OUTPUT_PANEL_WIDTH = 270;

    /// The rows of the cards Summary are 36 apart (383:3315, 385:9405): 28 + the gap 8
    static constexpr int SUMMARY_ROW_GAP = 8;

    /// The dots of the files (383:5715: blue, green, violet, teal, ...)
    static PDFAccentHue getFileHue(size_t fileIndex)
    {
        static const PDFAccentHue hues[] = { PDFAccentHue::Blue, PDFAccentHue::Green, PDFAccentHue::Violet, PDFAccentHue::Teal };
        return hues[fileIndex % std::size(hues)];
    }

    /// "Pages 1–12", "Page 7", "Pages 1, 3, 5"
    static QString getPagesText(const std::vector<PDFInteger>& pages)
    {
        if (pages.size() == 1)
        {
            return PDFAppToolGUISplit::tr("Page %1").arg(PDFAppLocale::formatNumber(pages.front() + 1));
        }
        return PDFAppToolGUISplit::tr("Pages %1").arg(PDFDocumentPageOperations::formatPageNumbers(pages, 3));
    }

    /// A container of a part of a panel with its own builder (a part, which is hidden as a whole)
    static QWidget* createPart(PDFAppToolPanelBuilder& builder, const QString& objectName)
    {
        QWidget* part = new QWidget();
        part->setObjectName(objectName);
        builder.addWidget(part);
        return part;
    }
};

// -------------------------------------------------------- PDFAppToolGUISplit

/// Registers the tool and its GUI under the id, when they are not registered yet
template<typename Tool, typename GUI>
class PDFAppToolGUISplitRegistration
{
public:
    static void registerTool()
    {
        const QString toolId = QString::fromLatin1(Tool::TOOL_ID);
        if (!PDFDocumentToolRegistry::isImplemented(toolId))
        {
            PDFDocumentToolRegistry::registerTool(std::make_shared<Tool>());
        }
        if (!PDFDocumentToolGUIRegistry::getFactory(toolId))
        {
            PDFDocumentToolGUIRegistry::registerFactory(toolId, std::make_shared<PDFDocumentToolGUIFactoryOf<GUI>>());
        }
    }
};

void PDFAppToolGUISplit::registerSplitTools()
{
    PDFAppToolGUISplitRegistration<PDFDocumentToolSplit, PDFAppSplitToolGUI>::registerTool();
    PDFAppToolGUISplitRegistration<PDFDocumentToolExtract, PDFAppExtractToolGUI>::registerTool();
}

QStringList PDFAppToolGUISplit::getOpenDocumentPaths(const PDFDocumentToolGUI* gui)
{
    QStringList paths;
    const PDFAppContext* context = gui ? gui->getContext() : nullptr;
    const PDFAppDocumentRegistry* registry = context ? context->getDocumentRegistry() : nullptr;
    if (!registry)
    {
        return paths;
    }
    for (const PDFAppDocument* document : registry->getDocuments())
    {
        const QString canonicalPath = document && !document->getFilePath().isEmpty() ? QFileInfo(document->getFilePath()).canonicalFilePath() : QString();
        if (!canonicalPath.isEmpty())
        {
            paths << canonicalPath;
        }
    }
    return paths;
}

// -------------------------------------------------------- PDFAppSplitToolGUI

PDFAppSplitToolGUI::PDFAppSplitToolGUI(QObject* parent) :
    BaseClass(parent)
{
    m_outputFiles = new PDFAppToolOutputFilesSection(this);
}

PDFAppSplitToolGUI::~PDFAppSplitToolGUI() = default;

QString PDFAppSplitToolGUI::getRule() const
{
    const PDFAppToolParameterModel* model = getModel();
    return model ? model->getValue(PDFAppToolGUISplitHelper::name(PDFDocumentToolSplit::PARAMETER_RULE)).toString() : QString();
}

QSize PDFAppSplitToolGUI::getSlotSize() const
{
    return PDFAppToolGUISplitHelper::SPLIT_SLOT;
}

QString PDFAppSplitToolGUI::getSelectionLabel() const
{
    return tr("Pages to split");
}

QString PDFAppSplitToolGUI::getPendingTitle() const
{
    // 383:3315 "Files to create · 4"
    return tr("Files to create");
}

QString PDFAppSplitToolGUI::getGridHeaderText() const
{
    // 383:3315 "4 files will be created"
    return m_files.empty() ? tr("No file will be created") : tr("%Ln files will be created", nullptr, int(m_files.size()));
}

QString PDFAppSplitToolGUI::getGridHintText() const
{
    if (getRule() == QLatin1String(PDFDocumentToolSplit::RULE_AT_PAGES))
    {
        // 383:5715
        return tr("The dot shows the file of each page. Click a page to start a new file there, click it again to join it to the file before.");
    }
    return tr("Each page shows the file it goes to.");
}

void PDFAppSplitToolGUI::onTargetsUpdated()
{
    // The files of the values now: the same functions as the run (PDFDocumentToolSplit)
    m_files.clear();
    m_filesError.clear();
    m_fileOfPage.assign(size_t(getPageCount()), -1);

    const PDFDocumentPointer document = getShownDocument();
    const PDFAppToolParameterModel* model = getModel();
    if (!document || !model)
    {
        return;
    }

    const QVariantMap values = model->getValues();
    std::optional<std::vector<PDFDocumentToolFileOutput::File>> files = PDFDocumentToolSplit::createFiles(document.data(), values, &m_filesError);
    if (!files)
    {
        return;
    }

    const PDFAppDocument* appDocument = getDocument();
    const QString sourceName = appDocument ? QFileInfo(appDocument->getFilePath()).fileName() : QString();
    QString nameError;
    PDFDocumentToolFileOutput::createFileNames(&*files, sourceName, values, QDate::currentDate(), &nameError);
    if (!nameError.isEmpty())
    {
        m_filesError = nameError;
    }

    m_files = std::move(*files);
    for (size_t i = 0; i < m_files.size(); ++i)
    {
        for (const PDFInteger page : m_files[i].pages)
        {
            if (size_t(page) < m_fileOfPage.size())
            {
                m_fileOfPage[size_t(page)] = int(i);
            }
        }
    }
}

std::vector<PDFInteger> PDFAppSplitToolGUI::getPendingPages() const
{
    // A row per file, keyed by its first page
    std::vector<PDFInteger> pages;
    pages.reserve(m_files.size());
    for (const PDFDocumentToolFileOutput::File& file : m_files)
    {
        pages.push_back(file.pages.front());
    }
    return pages;
}

PDFAppToolPendingPagesModel::Row PDFAppSplitToolGUI::createPendingRow(PDFInteger page) const
{
    // 383:3315: the tile with the number, "Handbook_1.pdf", "Pages 1–12" (383:7775 "· about 9.8 MB")
    PDFAppToolPendingPagesModel::Row row;
    row.page = page;
    const int fileIndex = size_t(page) < m_fileOfPage.size() ? m_fileOfPage[size_t(page)] : -1;
    if (fileIndex < 0)
    {
        return row;
    }

    const PDFDocumentToolFileOutput::File& file = m_files[size_t(fileIndex)];
    row.tileText = PDFAppLocale::formatNumber(fileIndex + 1);
    row.title = file.fileName;
    row.description = PDFAppToolGUISplitHelper::getPagesText(file.pages);
    if (!file.title.isEmpty() && getRule() == QLatin1String(PDFDocumentToolSplit::RULE_AT_BOOKMARKS))
    {
        // 383:6771 "Introduction · pages 1–6"
        row.description = tr("%1 · %2").arg(file.title, row.description);
    }
    if (file.estimatedSize >= 0)
    {
        row.description = tr("%1 · about %2").arg(row.description, PDFAppLocale::formatDataSize(file.estimatedSize, 1, QLocale::DataSizeTraditionalFormat));
    }
    row.tone = PDFComponentPainter::TileRowTone::Accent;
    return row;
}

PDFAppToolPageGridModel::Tile PDFAppSplitToolGUI::createTile(PDFInteger page) const
{
    PDFAppToolPageGridModel::Tile tile;
    const int fileIndex = size_t(page) < m_fileOfPage.size() ? m_fileOfPage[size_t(page)] : -1;
    if (fileIndex < 0)
    {
        // A page, which is not split
        tile.isDimmed = true;
        return tile;
    }

    // 383:5715: the dot of the file, the mark of a cut before the first page of a file
    const PDFDocumentToolFileOutput::File& file = m_files[size_t(fileIndex)];
    tile.caption = file.fileName;
    tile.dotColor = PDFAppToolGUISplitHelper::getFileHue(size_t(fileIndex));
    tile.markerBefore = fileIndex > 0 && file.pages.front() == page;
    return tile;
}

bool PDFAppSplitToolGUI::isPendingListShown(const std::vector<PDFInteger>& pendingPages) const
{
    return !pendingPages.empty();
}

void PDFAppSplitToolGUI::onPendingLinkActivated(PDFInteger page)
{
    // The rows of the files have no link
    Q_UNUSED(page);
}

PDFPageSelection::Mode PDFAppSplitToolGUI::getEntryModeWithoutSelection() const
{
    // 383:3315: Pages to split "All"
    return PDFPageSelection::Mode::All;
}

void PDFAppSplitToolGUI::clickPage(int page, Qt::KeyboardModifiers modifiers)
{
    PDFAppToolParameterModel* model = getModel();
    if (!model || getRule() != QLatin1String(PDFDocumentToolSplit::RULE_AT_PAGES))
    {
        BaseClass::clickPage(page, modifiers);
        return;
    }

    // 383:5715: a click sets or removes the cut before the page (the page 1 starts the first
    // file always); one undo step, the model announces it last
    const PDFInteger pageCount = getPageCount();
    if (page <= 0 || page >= pageCount)
    {
        return;
    }

    const QString key = PDFAppToolGUISplitHelper::name(PDFDocumentToolSplit::PARAMETER_CUTS);
    QString error;
    const std::optional<PDFPageRangeList> cuts = PDFPageRangeList::parse(model->getValue(key).toString(), pageCount, &error);
    std::vector<PDFInteger> pages = cuts ? cuts->getPageIndices() : std::vector<PDFInteger>();
    std::sort(pages.begin(), pages.end());
    pages.erase(std::unique(pages.begin(), pages.end()), pages.end());
    const auto it = std::find(pages.begin(), pages.end(), PDFInteger(page));
    if (it != pages.end())
    {
        pages.erase(it);
    }
    else
    {
        pages.insert(std::upper_bound(pages.begin(), pages.end(), PDFInteger(page)), PDFInteger(page));
    }

    QStringList numbers;
    for (const PDFInteger cut : pages)
    {
        numbers << QString::number(cut + 1);
    }
    model->setValue(key, numbers.join(QStringLiteral(", ")));
}

void PDFAppSplitToolGUI::describeToolbar(PDFAppToolPanelBuilder& builder)
{
    using Helper = PDFAppToolGUISplitHelper;

    // 383:3315: Split: Every N pages, the field "12 pages per file", Optimize: Off, Thumbnails
    builder.addEnumMenu(Helper::name(PDFDocumentToolSplit::PARAMETER_RULE), QStringLiteral("scissors"), tr("Split: %1"), Helper::RULE_PANEL_WIDTH);
    m_pagesPerFile = builder.addValueControl(Helper::name(PDFDocumentToolSplit::PARAMETER_PAGES_PER_FILE));
    if (m_pagesPerFile)
    {
        m_pagesPerFile->getWidget()->setObjectName(QStringLiteral("splitPagesPerFile"));
        m_pagesPerFile->getWidget()->setToolTip(tr("pages per file"));
    }
    builder.addActionDivider();
    builder.addEnumMenu(Helper::name(PDFDocumentToolSplit::PARAMETER_OPTIMIZE_IMAGES), QStringLiteral("minimize-2"), tr("Optimize: %1"), Helper::OPTIMIZE_PANEL_WIDTH);
    addTileSizeMenu(builder);
    updatePanels();
}

void PDFAppSplitToolGUI::describeRightPanel(PDFAppToolPanelBuilder& builder)
{
    using Helper = PDFAppToolGUISplitHelper;

    // 383:5715 Cuts "New file starts at pages"
    QWidget* cuts = Helper::createPart(builder, QStringLiteral("splitCutsSection"));
    PDFAppToolPanelBuilder cutsBuilder(this, cuts, Qt::Vertical, PDFAppToolPanelBuilder::SECTION_GAP);
    cutsBuilder.addSection(tr("Cuts"), tr("New file starts at pages"));
    cutsBuilder.addValueControl(Helper::name(PDFDocumentToolSplit::PARAMETER_CUTS));
    cutsBuilder.endSection();
    m_cutsSection = cuts;

    // 383:6771 the bookmarks ("top-level" or their numbers - no list of the frame here)
    QWidget* bookmarks = Helper::createPart(builder, QStringLiteral("splitBookmarksSection"));
    PDFAppToolPanelBuilder bookmarksBuilder(this, bookmarks, Qt::Vertical, PDFAppToolPanelBuilder::SECTION_GAP);
    bookmarksBuilder.addSection(tr("Bookmarks"), tr("\"top-level\" or the numbers of the bookmarks"));
    bookmarksBuilder.addValueControl(Helper::name(PDFDocumentToolSplit::PARAMETER_BOOKMARKS));
    bookmarksBuilder.addHint(tr("Pages before the first bookmark join the first file."));
    bookmarksBuilder.endSection();
    m_bookmarksSection = bookmarks;

    // 383:7775 the size and the callout "The size is approximate"
    QWidget* size = Helper::createPart(builder, QStringLiteral("splitSizeSection"));
    PDFAppToolPanelBuilder sizeBuilder(this, size, Qt::Vertical, PDFAppToolPanelBuilder::SECTION_GAP);
    sizeBuilder.addSection(tr("File size"), tr("MB"));
    sizeBuilder.addValueControl(Helper::name(PDFDocumentToolSplit::PARAMETER_SIZE_MB));
    m_sizeCallout = sizeBuilder.addCallout(PDFAccentHue::Blue, QStringLiteral("info"), tr("The size is approximate"),
                                           tr("It is estimated from the size of the source pages; the written files can be a little smaller or larger."));
    sizeBuilder.endSection();
    m_sizeSection = size;

    m_outputFiles->describe(builder);

    // 383:3315 Summary: Source, Rule, Files, Images
    builder.addSection(tr("Summary"));
    m_summary = builder.addKeyValues();
    m_summary->setRowGap(Helper::SUMMARY_ROW_GAP);
    m_summary->addRow(tr("Source"), QString());
    m_summary->addRow(tr("Rule"), QString());
    m_summary->addRow(tr("Files"), QString());
    m_summary->addRow(tr("Images"), QString());
    builder.endSection();
    updateAll();
}

void PDFAppSplitToolGUI::updatePanels()
{
    using Helper = PDFAppToolGUISplitHelper;

    const QString rule = getRule();
    if (m_pagesPerFile)
    {
        m_pagesPerFile->getWidget()->setVisible(rule == QLatin1String(PDFDocumentToolSplit::RULE_EVERY_N_PAGES));
    }
    if (m_cutsSection)
    {
        m_cutsSection->setVisible(rule == QLatin1String(PDFDocumentToolSplit::RULE_AT_PAGES));
    }
    if (m_bookmarksSection)
    {
        m_bookmarksSection->setVisible(rule == QLatin1String(PDFDocumentToolSplit::RULE_AT_BOOKMARKS));
    }
    if (m_sizeSection)
    {
        m_sizeSection->setVisible(rule == QLatin1String(PDFDocumentToolSplit::RULE_BY_SIZE));
    }

    QStringList names;
    for (const PDFDocumentToolFileOutput::File& file : m_files)
    {
        names << file.fileName;
    }
    if (m_outputFiles)
    {
        // 383:3315 "4 files · # is replaced by the file number"; 383:9088 the error
        const QString hint = m_files.empty() ? QString() : tr("%Ln files · # is replaced by the file number", nullptr, int(m_files.size()));
        m_outputFiles->setPreview(names, hint, m_filesError, isEveryNameEqual() ? tr("Add # to the file name so that each file gets its own number, for example %1_#.").arg(getSourceBaseName()) : QString());
    }

    if (m_summary && m_summary->getRowCount() == 4)
    {
        const PDFAppToolParameterModel* model = getModel();
        const int pageCount = int(getTargetPages().size());
        m_summary->setValue(0, tr("%Ln pages", nullptr, pageCount));
        QString ruleText;
        if (const PDFDocumentToolParameterDefinition* definition = model ? PDFDocumentToolParameters::find(model->getDefinitions(), Helper::name(PDFDocumentToolSplit::PARAMETER_RULE)) : nullptr)
        {
            for (const PDFDocumentToolParameterOption& option : definition->getOptions())
            {
                if (option.id == rule)
                {
                    ruleText = option.getTitle();
                }
            }
        }
        if (rule == QLatin1String(PDFDocumentToolSplit::RULE_EVERY_N_PAGES) && model)
        {
            // 383:3315 "Every N pages · 12"
            ruleText = tr("%1 · %2").arg(ruleText, PDFAppLocale::formatNumber(model->getValue(Helper::name(PDFDocumentToolSplit::PARAMETER_PAGES_PER_FILE)).toLongLong()));
        }
        m_summary->setValue(1, ruleText);
        m_summary->setValue(2, PDFAppLocale::formatNumber(qint64(m_files.size())));
        const bool isOptimized = model && model->getValue(Helper::name(PDFDocumentToolSplit::PARAMETER_OPTIMIZE_IMAGES)).toString() != QLatin1String(PDFDocumentToolMerge::OPTIMIZE_OFF);
        m_summary->setValue(3, isOptimized ? tr("Recompressed") : tr("Stored as they are"));
    }
}

QString PDFAppSplitToolGUI::getConfirmText() const
{
    // 383:3315 "Split into 4 Files"
    return m_files.empty() || !m_filesError.isEmpty() ? tr("Split") : tr("Split into %Ln Files", nullptr, int(m_files.size()));
}

bool PDFAppSplitToolGUI::isConfirmEnabled() const
{
    // 383:9088: equal names disable the Primary button
    return BaseClass::isConfirmEnabled() && !m_files.empty() && m_filesError.isEmpty();
}

QString PDFAppSplitToolGUI::getStatusText() const
{
    // 383:3315 "Split · Handbook.pdf · 48 pages → 4 files · the open document is not changed"
    const PDFDocumentToolCatalogEntry* tool = getTool();
    const PDFAppDocument* document = getDocument();
    const QString title = tool ? tool->getTitle() : QString();
    const QString fileName = document ? QFileInfo(document->getFilePath()).fileName() : QString();
    if (isEveryNameEqual())
    {
        // 383:9088
        return tr("%1 · the file name template gives the same name to every file").arg(title);
    }
    if (!m_filesError.isEmpty())
    {
        return tr("%1 · %2").arg(title, m_filesError);
    }
    return tr("%1 · %2 · %Ln pages → %3 · the open document is not changed", nullptr, int(getTargetPages().size()))
        .arg(title, fileName, tr("%Ln files", nullptr, int(m_files.size())));
}

bool PDFAppSplitToolGUI::isEveryNameEqual() const
{
    // 383:9088: the template gives one name to more files (no # in it)
    if (m_filesError.isEmpty() || m_files.size() < 2)
    {
        return false;
    }
    for (const PDFDocumentToolFileOutput::File& file : m_files)
    {
        if (file.fileName != m_files.front().fileName)
        {
            return false;
        }
    }
    return true;
}

QString PDFAppSplitToolGUI::getSourceBaseName() const
{
    const PDFAppDocument* document = getDocument();
    return document ? QFileInfo(document->getFilePath()).completeBaseName() : QString();
}

void PDFAppSplitToolGUI::fillResultInput(PDFAppToolResultInput* input) const
{
    // 383:11374 "Split finished", 403:29978 "Split stopped" (the defaults of the dialog)
    Q_UNUSED(input);
}

void PDFAppSplitToolGUI::prepareEnvironment(PDFAppToolRunEnvironment* environment) const
{
    BaseClass::prepareEnvironment(environment);
    environment->protectedFilePaths = PDFAppToolGUISplit::getOpenDocumentPaths(this);
}

// -------------------------------------------------------- PDFAppExtractToolGUI

PDFAppExtractToolGUI::PDFAppExtractToolGUI(QObject* parent) :
    BaseClass(parent)
{
    m_outputFiles = new PDFAppToolOutputFilesSection(this);
}

PDFAppExtractToolGUI::~PDFAppExtractToolGUI() = default;

bool PDFAppExtractToolGUI::isFilePerPage() const
{
    const PDFAppToolParameterModel* model = getModel();
    return model && model->getValue(PDFAppToolGUISplitHelper::name(PDFDocumentToolExtract::PARAMETER_OUTPUT)).toString() == QLatin1String(PDFDocumentToolExtract::OUTPUT_FILE_PER_PAGE);
}

QSize PDFAppExtractToolGUI::getSlotSize() const
{
    return PDFAppToolGUISplitHelper::EXTRACT_SLOT;
}

QString PDFAppExtractToolGUI::getSelectionLabel() const
{
    return tr("Pages to extract");
}

QString PDFAppExtractToolGUI::getPendingTitle() const
{
    return QString();
}

QString PDFAppExtractToolGUI::getGridHeaderText() const
{
    // 385:9405 "New document · 7 pages", 385:11029 "7 files", 385:11855 "Nothing to extract yet"
    const int count = int(getTargetPages().size());
    if (count == 0)
    {
        return tr("Nothing to extract yet");
    }
    return isFilePerPage() ? tr("%Ln files", nullptr, count) : tr("New document · %Ln pages", nullptr, count);
}

QString PDFAppExtractToolGUI::getGridHintText() const
{
    // 385:9405
    return tr("The pages are copied in the order of the document. Click a page to add it or to leave it out.");
}

std::vector<PDFInteger> PDFAppExtractToolGUI::getPendingPages() const
{
    return {};
}

PDFAppToolPendingPagesModel::Row PDFAppExtractToolGUI::createPendingRow(PDFInteger page) const
{
    PDFAppToolPendingPagesModel::Row row;
    row.page = page;
    return row;
}

PDFAppToolPageGridModel::Tile PDFAppExtractToolGUI::createTile(PDFInteger page) const
{
    PDFAppToolPageGridModel::Tile tile;
    if (isTarget(page))
    {
        tile.state = PDFComponentPainter::PageTileState::Marked;
        tile.badgeIconName = QStringLiteral("extract");
        tile.caption = isFilePerPage() ? tr("own file") : tr("extracted");
    }
    else
    {
        tile.isDimmed = true;
    }
    return tile;
}

bool PDFAppExtractToolGUI::isPendingListShown(const std::vector<PDFInteger>& pendingPages) const
{
    Q_UNUSED(pendingPages);
    return false;
}

void PDFAppExtractToolGUI::onPendingLinkActivated(PDFInteger page)
{
    Q_UNUSED(page);
}

void PDFAppExtractToolGUI::describeToolbar(PDFAppToolPanelBuilder& builder)
{
    using Helper = PDFAppToolGUISplitHelper;

    // 385:9405: Output: One file, Thumbnails
    builder.addEnumMenu(Helper::name(PDFDocumentToolExtract::PARAMETER_OUTPUT), QStringLiteral("files"), tr("Output: %1"), Helper::OUTPUT_PANEL_WIDTH);
    addTileSizeMenu(builder);
    updatePanels();
}

void PDFAppExtractToolGUI::describeRightPanel(PDFAppToolPanelBuilder& builder)
{
    // 385:11029 Output files (One file per page), 385:9405 Summary and the note
    m_outputFiles->describe(builder);
    builder.addSection(tr("Summary"));
    m_summary = builder.addKeyValues();
    m_summary->setRowGap(PDFAppToolGUISplitHelper::SUMMARY_ROW_GAP);
    m_summary->addRow(tr("Source"), QString());
    m_summary->addRow(tr("Pages"), QString());
    m_summary->addRow(tr("Output"), QString());
    builder.endSection();
    builder.addHint(tr("Extract Pages never changes the open document, so it also works when the document is locked. To delete pages, use Remove Pages."));
    updateAll();
}

void PDFAppExtractToolGUI::updatePanels()
{
    const std::vector<PDFInteger>& pages = getTargetPages();
    m_namesError.clear();
    if (m_outputFiles)
    {
        m_outputFiles->setVisible(isFilePerPage());
        if (isFilePerPage() && getModel())
        {
            // 385:11029 "Handbook_p1.pdf … Handbook_p12.pdf", "@ is replaced by the page number"
            std::vector<PDFDocumentToolFileOutput::File> files;
            for (const PDFInteger page : pages)
            {
                files.push_back({ { page }, QString(), -1, QString() });
            }
            const PDFAppDocument* document = getDocument();
            PDFDocumentToolFileOutput::createFileNames(&files, document ? QFileInfo(document->getFilePath()).fileName() : QString(), getModel()->getValues(), QDate::currentDate(), &m_namesError);
            QStringList names;
            for (const PDFDocumentToolFileOutput::File& file : files)
            {
                names << file.fileName;
            }
            m_outputFiles->setPreview(m_namesError.isEmpty() ? names : QStringList(), files.empty() ? QString() : tr("%Ln files · @ is replaced by the page number", nullptr, int(files.size())), m_namesError);
        }
    }

    if (m_summary && m_summary->getRowCount() == 3)
    {
        // 385:9405 Source "Handbook.pdf", Pages "7 of 48", Output "New document"; 385:11029
        // Output "7 files"; 385:11855 Pages "–"
        const PDFAppDocument* document = getDocument();
        const int count = int(pages.size());
        m_summary->setValue(0, document ? QFileInfo(document->getFilePath()).fileName() : QString());
        m_summary->setValue(1, count == 0 ? QStringLiteral("–") : tr("%1 of %2").arg(PDFAppLocale::formatNumber(count), PDFAppLocale::formatNumber(getPageCount())));
        m_summary->setValue(2, isFilePerPage() ? tr("%Ln files", nullptr, count) : tr("New document"));
    }
}

QString PDFAppExtractToolGUI::getConfirmText() const
{
    // 385:9405 "Extract 7 Pages", 385:11029 "Extract 7 Files"
    const int count = int(getTargetPages().size());
    if (count == 0)
    {
        return tr("Extract Pages");
    }
    return isFilePerPage() ? tr("Extract %Ln Files", nullptr, count) : tr("Extract %Ln Pages", nullptr, count);
}

bool PDFAppExtractToolGUI::isConfirmEnabled() const
{
    return BaseClass::isConfirmEnabled() && !getTargetPages().empty() && m_namesError.isEmpty();
}

QString PDFAppExtractToolGUI::getStatusText() const
{
    const PDFDocumentToolCatalogEntry* tool = getTool();
    const QString title = tool ? tool->getTitle() : QString();
    const int count = int(getTargetPages().size());
    if (count == 0)
    {
        return BaseClass::getStatusText();
    }

    // 385:9405 "Extract Pages · 7 of 48 pages → a new document · Handbook.pdf is not changed",
    // 385:11029 "… → 7 files · …"
    const PDFAppDocument* document = getDocument();
    const QString fileName = document ? QFileInfo(document->getFilePath()).fileName() : QString();
    const QString pagesText = tr("%1 of %Ln pages", nullptr, int(getPageCount())).arg(PDFAppLocale::formatNumber(count));
    const QString resultText = isFilePerPage() ? tr("%Ln files", nullptr, count) : tr("a new document");
    return tr("%1 · %2 → %3 · %4 is not changed").arg(title, pagesText, resultText, fileName);
}

void PDFAppExtractToolGUI::fillResultInput(PDFAppToolResultInput* input) const
{
    // 385:12395, 403:31676 "Pages extracted"
    input->title = tr("Pages extracted");
}

void PDFAppExtractToolGUI::prepareEnvironment(PDFAppToolRunEnvironment* environment) const
{
    BaseClass::prepareEnvironment(environment);
    environment->protectedFilePaths = PDFAppToolGUISplit::getOpenDocumentPaths(this);
}

}   // namespace pdf

#include "moc_pdfapptoolgui_split.cpp"
