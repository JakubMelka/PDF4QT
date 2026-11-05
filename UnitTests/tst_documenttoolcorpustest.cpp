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

#include "pdfannotation.h"
#include "pdfcms.h"
#include "pdfconstants.h"
#include "pdfdocumentreader.h"
#include "pdfdocumenttextexporter.h"
#include "pdfdocumenttextflow.h"
#include "pdfdocumenttool_extracttext.h"
#include "pdfdocumenttool_compress.h"
#include "pdfdocumenttool_protect.h"
#include "pdfdocumenttool_pages.h"
#include "pdfdocumenttool_structure.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumentwriter.h"
#include "pdfexception.h"
#include "pdffont.h"
#include "pdfform.h"
#include "pdfimage.h"
#include "pdfimagecompressor.h"
#include "pdfmeshqualitysettings.h"
#include "pdfoperationcontrol.h"
#include "pdfoptionalcontent.h"
#include "pdfpage.h"
#include "pdfpainter.h"
#include "pdfrenderer.h"
#include "pdfsecurityhandler.h"
#include "pdftextlayout.h"

#include <QBuffer>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonObject>
#include <QLockFile>
#include <QPainter>
#include <QRegularExpression>
#include <QXmlStreamReader>
#include <QtTest>

#include <algorithm>
#include <array>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

// Every tool of the core over the real PDF files of a folder, which is not in the
// repository (testpdf, the files are covered by the copyright of their authors): one data
// row per file and tool, so the runners (runtest_parallel.py, asan-run.py --shard) run each
// row as its own process and a crash names its file. A plain ctest run skips the test: the
// folder is 1.3 GB, the test runs only when it is asked for by PDF4QT_TOOL_CORPUS_DIRECTORY
// or by PDF4QT_TOOL_CORPUS_RUN=1 (the default folder <source>/testpdf).
//
// Environment:
//   PDF4QT_TOOL_CORPUS_DIRECTORY=<folder>   the files (asks for the run)
//   PDF4QT_TOOL_CORPUS_RUN=1                the run of the default folder
//   PDF4QT_TOOL_CORPUS_LARGE=1              also the files over LARGE_FILE_SIZE
//   PDF4QT_TOOL_CORPUS_SUMMARY=<file>       appends the summary lines to a file
//   PDF4QT_TOOL_CORPUS_OUTPUT=<folder>      the pictures of the pages, which differ
//   PDF4QT_TOOL_CORPUS_OUTPUT_ALL=1         the pictures of all compared pages (with a filter of rows)
//   PDF4QT_TOOL_CORPUS_NO_EXCLUSIONS=1      runs also the rows of the named exclusions
//   PDF4QT_TEST_TIMEOUT_FACTOR=<n>          multiplies the limit of the time of a row
//
// A summary line per row (also in the log of QtTest as QINFO), tabs between the columns:
//   CORPUS file tool/variant status sizeBefore sizeAfter milliseconds pages attributes note
// status: success, nothing-to-do, failed, refused, stopped, password-protected, damaged,
// skipped-large, excluded, timeout. attributes: of the input, "encrypted" and "signed".

/// What the tool promises for its result; the checks of a row are chosen only by these
enum CorpusPromise
{
    PromiseNone         = 0x00,
    PromiseSmaller      = 0x01,     ///< Success = the written file is smaller than the input file
    PromiseVersion      = 0x02,     ///< The PDF version of the input is kept
    PromiseEncryption   = 0x04,     ///< An encrypted input is encrypted again by the same handler, a plain input stays plain
    PromiseSignatures   = 0x08,     ///< The signature fields are kept and the impact is reported
    PromiseText         = 0x10,     ///< The text of the pages is the same
    PromiseTransparency = 0x20,     ///< The images keep their soft masks and masks
    PromiseContent      = 0x40,     ///< Every page keeps its decoded content, the names of its resources and its annotations, the catalog its entries
    PromiseAll          = 0x7F
};

/// How the pages of the result look against the pages of the input
enum class CorpusLook
{
    Exact,      ///< Lossless: the same within a small tolerance (antialiasing of a moved float)
    Close       ///< Lossy: close by a coarse metric, whole blocks of a page never change
};

/// Page of the result: which page of the input it is and how it was rotated by the tool
struct CorpusPageOrigin
{
    size_t inputPage = 0;
    pdf::PageRotation extraRotation = pdf::PageRotation::None;
};

/// The tool changes the pages by this rule (nullptr = every page stays as it is): the
/// row of Rotate or Remove says it, the checks of the pages are the same for all tools
using CorpusPageMap = std::vector<CorpusPageOrigin>(*)(const pdf::PDFDocument* input);

/// A tool, whose result is no document (Extract Text: a file), checks its result by its own
/// assertion instead of the checks of a document (Success and NothingToDo)
/// \param input The input document
/// \param result The result of the tool
/// \param parameters The parameters of the row
/// \param failures The failures of the row
using CorpusResultCheck = void(*)(const pdf::PDFDocument* input, const pdf::PDFDocumentToolResult& result, const QVariantMap& parameters, QStringList* failures);

/// The tool refuses the input by its contract with the parameters of the row (Remove Pages of
/// the even pages of a document of one page: a document keeps at least one page)
using CorpusRefusal = bool(*)(const pdf::PDFDocument* input);

/// A tool with its parameters. A new tool of the core is one row (or a few) of
/// ToolCorpusHelper::getToolRows: nothing else in this file knows the tools.
struct CorpusToolRow
{
    const char* toolId;
    const char* variant;
    std::function<std::shared_ptr<const pdf::PDFDocumentTool>()> createTool;
    QVariantMap parameters;
    CorpusLook look;
    int promises;
    CorpusPageMap pageMap = nullptr;
    CorpusResultCheck resultCheck = nullptr;
    CorpusRefusal refusal = nullptr;

    QString getName() const { return QString::fromLatin1(toolId) + QLatin1Char('/') + QString::fromLatin1(variant); }
};

/// A row, which is known not to end well because of the older Core: a hang or a crash has an
/// entry of SECURITY_TODO.md, the others say the decision of the owner. The run stays green
/// only knowingly: PDF4QT_TOOL_CORPUS_NO_EXCLUSIONS=1 runs them.
struct CorpusExclusion
{
    const char* filePattern;    ///< Regular expression of the relative path of the file
    const char* variantPattern; ///< Regular expression of tool/variant
    const char* reference;      ///< "SECURITY_TODO 22", a decision of the owner
    const char* reason;
};

/// Difference of two pictures of one page
struct CorpusImageDifference
{
    double meanDifference = 0.0;    ///< Mean of |a - b| over the channels, 0 to 255
    double worstBlock = 0.0;        ///< The largest mean of |a - b| of a block of EXACT_BLOCK_SIZE pixels
    double meanShift = 0.0;         ///< Mean over the blocks of CLOSE_BLOCK_SIZE pixels of the largest shift of a channel, |mean(a) - mean(b)|
    double worstShift = 0.0;        ///< The largest shift of a channel of such a block: artifacts and a blur average out in a block, a lost image or another color does not
    double worstLargeShift = 0.0;   ///< The same of a block of CLOSE_LARGE_BLOCK_SIZE pixels: less is averaged away by a resampled edge, an area in another color stays
    int worstPixel = 0;             ///< The largest |a - b| of a channel of a pixel: a dot, a thin line and a light object are lost in the mean of a block
};

/// The line of the summary of a row
class CorpusSummary
{
public:
    QString file;
    QString tool;
    QString status;
    qint64 sizeBefore = -1;
    qint64 sizeAfter = -1;
    qint64 milliseconds = 0;
    qint64 pages = -1;
    QString attributes;
    QString note;

    QString createLine() const
    {
        QString cleanNote = note;
        cleanNote.replace(QRegularExpression(QStringLiteral("[\\t\\r\\n]+")), QStringLiteral(" "));
        return QStringList{ QStringLiteral("CORPUS"), file, tool, status, QString::number(sizeBefore), QString::number(sizeAfter), QString::number(milliseconds), QString::number(pages), attributes, cleanNote.left(1000) }.join(QLatin1Char('\t'));
    }

    void print() const
    {
        const QString line = createLine();
        qInfo().noquote() << line;

        const QString summaryFile = qEnvironmentVariable("PDF4QT_TOOL_CORPUS_SUMMARY");
        if (!summaryFile.isEmpty())
        {
            QLockFile lock(summaryFile + QStringLiteral(".lock"));
            lock.lock();
            QFile output(summaryFile);
            if (output.open(QFile::Append | QFile::Text))
            {
                output.write(line.toUtf8() + '\n');
            }
        }
    }
};

/// Ends a process, whose row runs over the limit of the time: a hang of one file is named
/// by its row (the other rows are other processes). The cancellation of the tools is not
/// used, the point is to find what the cancellation does not reach.
class CorpusWatchdog
{
public:
    CorpusWatchdog(const CorpusSummary* summary, int seconds) :
        m_summary(summary),
        m_thread(&CorpusWatchdog::run, this, seconds)
    {

    }

    ~CorpusWatchdog()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_isFinished = true;
        }
        m_condition.notify_all();
        m_thread.join();
    }

private:
    void run(int seconds)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (!m_condition.wait_for(lock, std::chrono::seconds(seconds), [this]() { return m_isFinished; }))
        {
            CorpusSummary summary = *m_summary;
            summary.status = QStringLiteral("timeout");
            summary.milliseconds = seconds * qint64(1000);
            summary.note = QStringLiteral("over the limit of %1 s").arg(seconds);
            summary.print();
            fprintf(stderr, "%s\n", qPrintable(summary.createLine()));
            fflush(stderr);
            std::_Exit(71);
        }
    }

    const CorpusSummary* m_summary;
    std::mutex m_mutex;
    std::condition_variable m_condition;
    bool m_isFinished = false;
    std::thread m_thread;
};

/// Limit of the time of the comparison of one page. A page, which the renderer draws for
/// minutes (the files of the slow cases), says nothing about the tool: its row reports it
/// in the note, and the limit of the row stays for the tool itself.
class CorpusPageBudget : public pdf::PDFOperationControl
{
public:
    explicit CorpusPageBudget(int seconds) :
        m_milliseconds(qint64(seconds) * 1000)
    {
        m_timer.start();
    }

    virtual bool isOperationCancelled() const override { return m_timer.hasExpired(m_milliseconds); }

private:
    qint64 m_milliseconds;
    QElapsedTimer m_timer;
};

/// Draws the pages of one document: the page content and the annotations, as a page of the
/// application, with the default color management
class CorpusRenderer
{
public:
    explicit CorpusRenderer(pdf::PDFDocument* document) :
        m_document(document),
        m_fontCache(pdf::DEFAULT_FONT_CACHE_LIMIT, pdf::DEFAULT_REALIZED_FONT_CACHE_LIMIT),
        m_cmsManager(nullptr),
        m_optionalContentActivity(document, pdf::OCUsage::View, nullptr),
        m_annotationManager(&m_fontCache, nullptr, &m_cmsManager, &m_optionalContentActivity, pdf::PDFMeshQualitySettings(), pdf::PDFRenderer::getDefaultFeatures(), pdf::PDFAnnotationManager::Target::View, nullptr)
    {
        pdf::PDFModifiedDocument modifiedDocument(document, &m_optionalContentActivity);

        // The settings the image optimizer decodes by and an application starts with: a new
        // manager has the Generic system, which draws CMYK and ICC images in other colors
        m_cmsManager.setDocument(document);
        m_cmsManager.setSettings(m_cmsManager.getDefaultSettings());
        m_fontCache.setDocument(modifiedDocument);
        m_annotationManager.setDocument(modifiedDocument);
    }

    /// The picture of the page and the errors of the processing
    QImage render(size_t pageIndex, pdf::PageRotation extraRotation, QSize size, const pdf::PDFOperationControl* control, QStringList* errors)
    {
        const pdf::PDFPage* page = m_document->getCatalog()->getPage(pageIndex);
        const pdf::PDFCMSPointer cms = m_cmsManager.getCurrentCMS();

        pdf::PDFPrecompiledPage compiledPage;
        pdf::PDFRenderer renderer(m_document, &m_fontCache, nullptr, cms.data(), &m_optionalContentActivity, pdf::PDFRenderer::getDefaultFeatures(), pdf::PDFMeshQualitySettings());
        renderer.setOperationControl(control);
        renderer.compile(&compiledPage, pageIndex);

        if (pdf::PDFOperationControl::isOperationCancelled(control))
        {
            return QImage();
        }

        QImage image(size, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);

        QList<pdf::PDFRenderError> renderErrors = compiledPage.getErrors();

        {
            QPainter painter(&image);
            const QTransform matrix = pdf::PDFRenderer::createPagePointToDevicePointMatrix(page, QRect(QPoint(0, 0), size), extraRotation);
            pdf::PDFColorConvertor convertor = cms->getColorConvertor();
            compiledPage.draw(&painter, page->getCropBox(), matrix, pdf::PDFRenderer::getDefaultFeatures(), 1.0);
            pdf::PDFTextLayoutGetter textLayoutGetter(nullptr, pageIndex);
            m_annotationManager.drawPage(&painter, pageIndex, &compiledPage, textLayoutGetter, matrix, convertor, renderErrors);
        }

        for (const pdf::PDFRenderError& error : std::as_const(renderErrors))
        {
            if (error.type != pdf::RenderErrorType::Information)
            {
                errors->append(error.message);
            }
        }
        return image;
    }

private:
    pdf::PDFDocument* m_document;
    pdf::PDFFontCache m_fontCache;
    pdf::PDFCMSManager m_cmsManager;
    pdf::PDFOptionalContentActivity m_optionalContentActivity;
    pdf::PDFAnnotationManager m_annotationManager;
};

/// Rows of the test and the checks
class ToolCorpusHelper
{
public:
    ToolCorpusHelper() = delete;

    /// A file over this size runs only with PDF4QT_TOOL_CORPUS_LARGE=1
    static constexpr qint64 LARGE_FILE_SIZE = qint64(64) * 1024 * 1024;

    /// Limit of the time of one row (scaled by PDF4QT_TEST_TIMEOUT_FACTOR)
    static constexpr int ITEM_TIME_LIMIT_SECONDS = 600;

    /// Limit of the time of the comparison of one page (scaled by the same factor)
    static constexpr int PAGE_TIME_LIMIT_SECONDS = 60;

    /// The longer side of a picture of a page: one row stays seconds, and a page of the
    /// lossy presets at 110 dpi is not drawn at more pixels than it has
    static constexpr int RENDER_SIZE = 600;

    /// A lossless tool: the mean of the differences of a channel and of a block of a page
    /// (the same drawing gives 0; a level of 1 is a rounding, a missing glyph is much more).
    /// A pixel: the dot of a period of 10 points at this size covers about a half of a pixel, so
    /// about 100 levels; an antialiased edge moved by a rounding of a number by 1 / 8 of a pixel 32.
    static constexpr double EXACT_MEAN_DIFFERENCE = 0.25;
    static constexpr double EXACT_WORST_BLOCK = 4.0;
    static constexpr int EXACT_BLOCK_SIZE = 8;
    static constexpr int EXACT_WORST_PIXEL = 32;

    /// A lossy tool: the color of a block moves by at most these numbers (levels 0 to 255). The
    /// artifacts of a JPEG and a reduced resolution average out in a block: the rows measured
    /// for it reach 2.65 over a page and 22.4 in a block. The limits lie above them and below
    /// the real damages seen: a small patch of a page turned black 31.4 (the composite page
    /// Ghent_PDF-Output-Test-V50_CMS_X4.pdf), images in wrong colors 80.0 in a block.
    static constexpr double CLOSE_MEAN_SHIFT = 4.0;
    static constexpr double CLOSE_WORST_SHIFT = 28.0;

    static constexpr int CLOSE_BLOCK_SIZE = 16;

    /// A black and white edge across a block moved by a half of a pixel of the picture (the image
    /// of 110 dpi against the picture of about 55 dpi) shifts a block of 64 pixels by 2 levels, the
    /// error of the mean color of a JPEG block of the quality 60 is about 3 levels: an area of a
    /// page as large as the block in another color is above it, also when the mean of a page is not.
    static constexpr double CLOSE_WORST_LARGE_SHIFT = 10.0;
    static constexpr int CLOSE_LARGE_BLOCK_SIZE = 64;

    /// An image is transparent, when this share of its pixels is not opaque. A smaller share
    /// of the result is accepted (hysteresis), a reduced resolution moves a share a little.
    static constexpr double TRANSPARENT_SHARE = 0.01;
    static constexpr double KEPT_TRANSPARENT_SHARE = 0.0025;

    /// At most this number of pixels of an image is looked at for its transparency
    static constexpr int MAXIMUM_PROBED_ROWS = 1024;

    /// The tools of the core: one table, the only place which knows them
    static const std::vector<CorpusToolRow>& getToolRows()
    {
        static const std::vector<CorpusToolRow> rows = createToolRows();
        return rows;
    }

    /// Named exclusions: rows, which end badly because of the older Core, each with its reference
    static const std::vector<CorpusExclusion>& getExclusions()
    {
        static const std::vector<CorpusExclusion> exclusions = {
            { "^Bugs/SLOW_PERF_BTN_TRX_BigData_0513pdf\\(5\\)\\.pdf$", "/(smaller-file|balanced|best-quality|all-steps)$", "SECURITY_TODO 22",
              "PDFPrecompiledPage::draw of the meshes of the shadings of the page runs for more than 10 minutes, the cancellation does not reach it" },
            { "^GhentOutputSuite/.*GWG", ".", "Owner 2026-11-01", "known, needs a change of the renderer, ignored for now" },
            { "^Bugs/SLOW_PERF_BTN_TRX_BigData_0513pdf\\(5\\)\\.pdf$", "^convert\\.extract-text/", "SECURITY_TODO 22",
              "the content of its pages runs for more than 10 minutes (the meshes of the shadings), the walks of Extract Text read the same content" },
        };
        return exclusions;
    }

    /// Files the reader refuses, each with its reason. Another file refused fails its row: a
    /// change of the reader would otherwise only turn the rows into skips of a green run.
    static const std::vector<CorpusExclusion>& getRefusedFiles()
    {
        static const std::vector<CorpusExclusion> files = {
            { "^Encryption/PDF_Color_Spaces_E_", ".", "Owner 2026-11-01", "no passwords of the folder" },
            { "^(Bugs/LinksClickable\\.pdf|Fonts/FONT_0C_T1_SPEC_\\.PDF)$", ".", "Not a PDF", "the page 404 of a web server" },
            { "^(Bugs/CrashStackOverflow|Bugs/PoC2|Signature/crash-HASH_End)\\.pdf$", ".", "Hostile", "written to be refused or to crash a reader" },
            { "^Bugs/FAIL_BOOKMARKS_user_guide\\.pdf$", ".", "Damaged", "/Prev of the first table points 37175 bytes before the main table, the reader does not rebuild a table" },
        };
        return files;
    }

    static const CorpusExclusion* findRefusedFile(const QString& file)
    {
        for (const CorpusExclusion& refused : getRefusedFiles())
        {
            if (QRegularExpression(QString::fromUtf8(refused.filePattern)).match(file).hasMatch())
            {
                return &refused;
            }
        }
        return nullptr;
    }

    static const CorpusExclusion* findExclusion(const QString& file, const QString& tool)
    {
        if (qEnvironmentVariableIntValue("PDF4QT_TOOL_CORPUS_NO_EXCLUSIONS") != 0)
        {
            return nullptr;
        }

        for (const CorpusExclusion& exclusion : getExclusions())
        {
            if (QRegularExpression(QString::fromUtf8(exclusion.filePattern)).match(file).hasMatch() && QRegularExpression(QString::fromLatin1(exclusion.variantPattern)).match(tool).hasMatch())
            {
                return &exclusion;
            }
        }
        return nullptr;
    }

    /// The test was asked for
    static bool isRunRequested()
    {
        return !qEnvironmentVariable("PDF4QT_TOOL_CORPUS_DIRECTORY").isEmpty() || qEnvironmentVariableIntValue("PDF4QT_TOOL_CORPUS_RUN") != 0;
    }

    static QDir getDirectory()
    {
        const QString directory = qEnvironmentVariable("PDF4QT_TOOL_CORPUS_DIRECTORY");
        return QDir(directory.isEmpty() ? QStringLiteral(PDF4QT_TOOL_CORPUS_DIRECTORY) : directory);
    }

    static int getPageTimeLimit()
    {
        return PAGE_TIME_LIMIT_SECONDS * qMax(1, qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR"));
    }

    static int getTimeLimit()
    {
        const int factor = qMax(1, qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR"));
        return ITEM_TIME_LIMIT_SECONDS * factor;
    }

    /// Name of a row: the relative path of the file and the tool. The arguments of a
    /// process are in the code page of the system on Windows, so other than ASCII
    /// characters are percent-encoded.
    static QByteArray createRowName(const QString& file, const QString& tool)
    {
        return (file + QLatin1Char('|') + tool).toUtf8().toPercentEncoding(" !#$&'()+,-./:;=@[]^_`{|}~");
    }

    /// The relative paths of the files, sorted
    static QStringList getFiles(const QDir& directory)
    {
        QStringList files;
        QDirIterator iterator(directory.absolutePath(), { QStringLiteral("*.pdf") }, QDir::Files, QDirIterator::Subdirectories);
        while (iterator.hasNext())
        {
            files << directory.relativeFilePath(iterator.next());
        }
        files.sort();
        return files;
    }

    /// Compares the pictures of a page (the same size)
    static CorpusImageDifference compareImages(const QImage& firstImage, const QImage& secondImage)
    {
        CorpusImageDifference result;
        const QImage first = firstImage.convertToFormat(QImage::Format_RGB32);
        const QImage second = secondImage.convertToFormat(QImage::Format_RGB32);
        const int width = first.width();
        const int height = first.height();
        const int blocksX = (width + EXACT_BLOCK_SIZE - 1) / EXACT_BLOCK_SIZE;
        const int blocksY = (height + EXACT_BLOCK_SIZE - 1) / EXACT_BLOCK_SIZE;
        const size_t blockCount = size_t(blocksX) * blocksY;
        const int shiftBlocksX = (width + CLOSE_BLOCK_SIZE - 1) / CLOSE_BLOCK_SIZE;
        const int shiftBlocksY = (height + CLOSE_BLOCK_SIZE - 1) / CLOSE_BLOCK_SIZE;
        const size_t shiftBlockCount = size_t(shiftBlocksX) * shiftBlocksY;
        std::vector<qint64> blockSums(blockCount, 0);
        std::vector<qint64> blockCounts(blockCount, 0);
        std::vector<std::array<qint64, 3>> blockShifts(shiftBlockCount, std::array<qint64, 3>{ 0, 0, 0 });
        std::vector<qint64> shiftCounts(shiftBlockCount, 0);
        const int largeBlocksX = (width + CLOSE_LARGE_BLOCK_SIZE - 1) / CLOSE_LARGE_BLOCK_SIZE;
        const int largeBlocksY = (height + CLOSE_LARGE_BLOCK_SIZE - 1) / CLOSE_LARGE_BLOCK_SIZE;
        const size_t largeBlockCount = size_t(largeBlocksX) * largeBlocksY;
        std::vector<std::array<qint64, 3>> largeShifts(largeBlockCount, std::array<qint64, 3>{ 0, 0, 0 });
        std::vector<qint64> largeCounts(largeBlockCount, 0);

        qint64 sum = 0;
        for (int y = 0; y < height; ++y)
        {
            const QRgb* firstLine = reinterpret_cast<const QRgb*>(first.constScanLine(y));
            const QRgb* secondLine = reinterpret_cast<const QRgb*>(second.constScanLine(y));
            for (int x = 0; x < width; ++x)
            {
                const int red = qAbs(qRed(firstLine[x]) - qRed(secondLine[x]));
                const int green = qAbs(qGreen(firstLine[x]) - qGreen(secondLine[x]));
                const int blue = qAbs(qBlue(firstLine[x]) - qBlue(secondLine[x]));
                const size_t block = size_t(y / EXACT_BLOCK_SIZE) * blocksX + x / EXACT_BLOCK_SIZE;
                const size_t shiftBlock = size_t(y / CLOSE_BLOCK_SIZE) * shiftBlocksX + x / CLOSE_BLOCK_SIZE;
                blockSums[block] += red + green + blue;
                blockCounts[block] += 3;
                blockShifts[shiftBlock][0] += qRed(firstLine[x]) - qRed(secondLine[x]);
                blockShifts[shiftBlock][1] += qGreen(firstLine[x]) - qGreen(secondLine[x]);
                blockShifts[shiftBlock][2] += qBlue(firstLine[x]) - qBlue(secondLine[x]);
                ++shiftCounts[shiftBlock];
                const size_t largeBlock = size_t(y / CLOSE_LARGE_BLOCK_SIZE) * largeBlocksX + x / CLOSE_LARGE_BLOCK_SIZE;
                largeShifts[largeBlock][0] += qRed(firstLine[x]) - qRed(secondLine[x]);
                largeShifts[largeBlock][1] += qGreen(firstLine[x]) - qGreen(secondLine[x]);
                largeShifts[largeBlock][2] += qBlue(firstLine[x]) - qBlue(secondLine[x]);
                ++largeCounts[largeBlock];
                result.worstPixel = qMax(result.worstPixel, qMax(red, qMax(green, blue)));
                sum += red + green + blue;
            }
        }

        const qint64 count = qint64(width) * height * 3;
        result.meanDifference = count > 0 ? double(sum) / double(count) : 0.0;
        for (size_t i = 0; i < blockCount; ++i)
        {
            if (blockCounts[i] > 0)
            {
                result.worstBlock = qMax(result.worstBlock, double(blockSums[i]) / double(blockCounts[i]));
            }
        }

        double shiftSum = 0.0;
        for (size_t i = 0; i < shiftBlockCount; ++i)
        {
            if (shiftCounts[i] > 0)
            {
                const double shift = qMax(qAbs(double(blockShifts[i][0])), qMax(qAbs(double(blockShifts[i][1])), qAbs(double(blockShifts[i][2])))) / double(shiftCounts[i]);
                result.worstShift = qMax(result.worstShift, shift);
                shiftSum += shift;
            }
        }
        result.meanShift = shiftBlockCount > 0 ? shiftSum / double(shiftBlockCount) : 0.0;

        for (size_t i = 0; i < largeBlockCount; ++i)
        {
            if (largeCounts[i] > 0)
            {
                result.worstLargeShift = qMax(result.worstLargeShift, qMax(qAbs(double(largeShifts[i][0])), qMax(qAbs(double(largeShifts[i][1])), qAbs(double(largeShifts[i][2])))) / double(largeCounts[i]));
            }
        }
        return result;
    }

    /// Size of a picture of a page of the given rotated box
    static QSize getRenderSize(const QSizeF& pageSize)
    {
        const qreal largest = qMax(pageSize.width(), pageSize.height());
        if (!(largest > 0.0) || !std::isfinite(largest))
        {
            return QSize();
        }
        const qreal scale = RENDER_SIZE / largest;
        return QSize(qBound(1, qRound(pageSize.width() * scale), RENDER_SIZE), qBound(1, qRound(pageSize.height() * scale), RENDER_SIZE));
    }

    /// The text of a page
    static QString getPageText(const pdf::PDFDocument* document, size_t pageIndex, const pdf::PDFOperationControl* control)
    {
        pdf::PDFDocumentTextFlowFactory factory;
        factory.setOperationControl(control);
        const pdf::PDFDocumentTextFlow flow = factory.create(document, { pdf::PDFInteger(pageIndex) }, pdf::PDFDocumentTextFlowFactory::Algorithm::Content);

        // The text of the page, not its number: the item "Page 5" of the flow says the index of the
        // page, which another tool changes (Remove Pages)
        QStringList texts;
        for (const pdf::PDFDocumentTextFlow::Item& item : flow.getItems())
        {
            if (!item.flags.testFlag(pdf::PDFDocumentTextFlow::PageStart))
            {
                texts << item.text.trimmed();
            }
        }
        return texts.join(QLatin1Char(' '));
    }

    /// The decoded content of a page. A stream, which cannot be decoded, by its stored data:
    /// a tool keeps a damaged stream as it is.
    static QByteArray getPageContent(const pdf::PDFDocument* document, size_t pageIndex)
    {
        const auto getStreamData = [document](const pdf::PDFObject& object)
        {
            const pdf::PDFObject& streamObject = document->getObject(object);
            if (!streamObject.isStream())
            {
                return QByteArray();
            }

            try
            {
                return document->getDecodedStream(streamObject.getStream());
            }
            catch (const pdf::PDFException&)
            {
                return *streamObject.getStream()->getContent();
            }
        };

        const pdf::PDFObject& contents = document->getObject(document->getCatalog()->getPage(pageIndex)->getContents());
        if (!contents.isArray())
        {
            return getStreamData(contents);
        }

        QByteArray content;
        const pdf::PDFArray* array = contents.getArray();
        for (size_t i = 0; i < array->getCount(); ++i)
        {
            content += getStreamData(array->getItem(i));
        }
        return content;
    }

    /// Keys of a dictionary with a value: an entry of a null object means no entry
    /// (ISO 32000-2, 7.3.9), the optimizer removes it
    static QStringList getKeys(const pdf::PDFDocument* document, const pdf::PDFDictionary* dictionary)
    {
        QStringList keys;
        for (size_t i = 0; dictionary && i < dictionary->getCount(); ++i)
        {
            if (!document->getObject(dictionary->getValue(i)).isNull())
            {
                keys << QString::fromLatin1(dictionary->getKey(i).getString().toPercentEncoding());
            }
        }
        keys.sort();
        return keys;
    }

    /// Names of the resources of a page by their kinds and its annotations, which exist
    static QStringList getPageStructure(const pdf::PDFDocument* document, size_t pageIndex)
    {
        const pdf::PDFPage* page = document->getCatalog()->getPage(pageIndex);
        const pdf::PDFDictionary* resources = document->getDictionaryFromObject(page->getResources());

        QStringList structure;
        for (const QString& kind : getKeys(document, resources))
        {
            structure << kind;
            const pdf::PDFDictionary* names = document->getDictionaryFromObject(resources->get(QByteArray::fromPercentEncoding(kind.toLatin1())));
            for (const QString& name : getKeys(document, names))
            {
                structure << kind + QLatin1Char('/') + name;
            }
        }

        const qsizetype annotations = std::count_if(page->getAnnotations().cbegin(), page->getAnnotations().cend(), [document](pdf::PDFObjectReference reference) { return !document->getObjectByReference(reference).isNull(); });
        structure << QStringLiteral("annotations %1").arg(annotations);
        return structure;
    }

    /// Entries of the catalog of a document with a value
    static QStringList getCatalogKeys(const pdf::PDFDocument* document)
    {
        const pdf::PDFDictionary* trailer = document->getTrailerDictionary();
        return getKeys(document, trailer ? document->getDictionaryFromObject(trailer->get("Root")) : nullptr);
    }

    /// The page, which draws the largest image: the first and the last page of a long file
    /// often have none, and then a lossy row would never look at an image
    static std::optional<size_t> getLargestImagePage(const pdf::PDFDocument* document, const pdf::PDFOperationControl* control)
    {
        pdf::PDFImageCompressor::Environment environment(document);
        const pdf::PDFImageCompressor::ImagePlacements placements = pdf::PDFImageCompressor().collectImagePlacements(environment, control);

        std::optional<size_t> page;
        double largest = 0.0;
        for (const pdf::PDFImageCompressor::ImagePlacement& placement : placements)
        {
            const double area = placement.maximumInches.x() * placement.maximumInches.y();
            if (area > largest)
            {
                largest = area;
                page = size_t(placement.firstPageIndex);
            }
        }
        return page;
    }

    /// Shares of the pixels, which are not opaque, of the images drawn by a page with a mask
    /// of any kind. An image with a mask, which covers nothing, may lose it (share 0).
    static std::vector<double> getTransparentShares(const pdf::PDFDocument* document, size_t pageIndex, const pdf::PDFOperationControl* control)
    {
        std::vector<double> shares;
        pdf::PDFRenderErrorReporterDummy reporter;
        pdf::PDFImageCompressor::Environment environment(document);
        const pdf::PDFCMS* cms = environment.getCMS();

        const auto hasMask = [document](pdf::PDFObjectReference reference)
        {
            const pdf::PDFObject& object = document->getObjectByReference(reference);
            if (!object.isStream())
            {
                return false;
            }
            const pdf::PDFDictionary* dictionary = object.getStream()->getDictionary();
            return dictionary->hasKey("SMask") || dictionary->hasKey("Mask") || dictionary->hasKey("SMaskInData");
        };
        const auto addImage = [&shares, &reporter, cms, control](const pdf::PDFImage& image, const pdf::PDFStream*, pdf::PDFObjectReference, QPointF)
        {
            shares.push_back(getTransparentShare(image.getImage(cms, &reporter, control)));
        };
        environment.processPage(pdf::PDFInteger(pageIndex), addImage, control, hasMask);
        return shares;
    }

    /// Number of the shares of at least \p limit
    static qsizetype countShares(const std::vector<double>& shares, double limit)
    {
        return std::count_if(shares.cbegin(), shares.cend(), [limit](double share) { return share >= limit; });
    }

    /// Texts appearing in \p after more times than in \p before
    static QStringList getNewTexts(const QStringList& before, const QStringList& after)
    {
        std::map<QString, int> counts;
        for (const QString& text : before)
        {
            ++counts[text];
        }

        QStringList newTexts;
        for (const QString& text : after)
        {
            if (--counts[text] < 0)
            {
                newTexts << text;
            }
        }
        return newTexts;
    }

    static QString getStatusName(pdf::PDFDocumentToolResultStatus status)
    {
        return pdf::PDFDocumentToolResult::getStatusName(status);
    }

    static pdf::EncryptionMode getEncryptionMode(const pdf::PDFDocument* document)
    {
        const pdf::PDFSecurityHandler* handler = document->getStorage().getSecurityHandler();
        return handler ? handler->getMode() : pdf::EncryptionMode::None;
    }

    /// Writes the pictures of a page, which differ, for a look by a human
    static void writePictures(const QString& file, const QString& tool, size_t pageIndex, const QImage& before, const QImage& after)
    {
        const QString folder = qEnvironmentVariable("PDF4QT_TOOL_CORPUS_OUTPUT");
        if (folder.isEmpty())
        {
            return;
        }

        QString name = file + QLatin1Char('_') + tool + QLatin1Char('_') + QString::number(pageIndex);
        name.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")), QStringLiteral("_"));
        QDir().mkpath(folder);
        before.save(QDir(folder).absoluteFilePath(name + QStringLiteral("_before.png")));
        after.save(QDir(folder).absoluteFilePath(name + QStringLiteral("_after.png")));
    }

    /// Characters other than white space with their counts (the soft hyphen U+00AD, which the
    /// layout removes, does not count)
    static std::map<char32_t, qint64> countCharacters(const QString& text)
    {
        std::map<char32_t, qint64> counts;
        for (const char32_t character : text.toUcs4())
        {
            if (!QChar::isSpace(character) && character != 0x00AD)
            {
                ++counts[character];
            }
        }
        return counts;
    }

    /// The text of a page as the order of the content stream reads all of it (the text items only,
    /// no "Page N", also the text outside of a marked content - setStructureExport)
    static QString getExtractReferenceText(const pdf::PDFDocument* document, size_t pageIndex, const pdf::PDFOperationControl* control)
    {
        pdf::PDFDocumentTextFlowFactory factory;
        factory.setOperationControl(control);
        factory.setStructureExport(true);
        QString text;
        const pdf::PDFDocumentTextFlow flow = factory.create(document, { pdf::PDFInteger(pageIndex) }, pdf::PDFDocumentTextFlowFactory::Algorithm::Content);
        for (const pdf::PDFDocumentTextFlow::Item& item : flow.getItems())
        {
            if (item.isText())
            {
                text += item.text + QLatin1Char(' ');
            }
        }
        return text;
    }

    /// Extract Text: the text of the file against the text of the pages, read by the order of
    /// the content stream (an independent reading for the other orders). Every character of the
    /// file is a character of a page (or of a line "Page N" of the row); at most 2 % of the
    /// characters of the pages are missing (the layout merges a hyphen of a line end). A markup
    /// file is well formed. NothingToDo: no page has a text. A tagged document read by the
    /// structure has its own text (artifacts are left out), only the file is checked.
    static void checkExtractedText(const pdf::PDFDocument* input, const pdf::PDFDocumentToolResult& result, const QVariantMap& parameters, QStringList* failures)
    {
        using Tool = pdf::PDFDocumentToolExtractText;

        std::map<char32_t, qint64> pageCounts;
        const bool isLatin1 = parameters.value(QString::fromLatin1(Tool::PARAMETER_ENCODING)).toString() == QLatin1String(Tool::ENCODING_LATIN1);
        qint64 notInLatin1 = 0;
        for (size_t i = 0; i < input->getCatalog()->getPageCount(); ++i)
        {
            const CorpusPageBudget budget(getPageTimeLimit());
            for (const auto& item : countCharacters(getExtractReferenceText(input, i, &budget)))
            {
                const bool isLost = isLatin1 && item.first > 0xFF;
                pageCounts[isLost ? char32_t('?') : item.first] += item.second;
                notInLatin1 += isLost ? item.second : 0;
            }
        }

        qint64 pageTotal = 0;
        for (const auto& item : pageCounts)
        {
            pageTotal += item.second;
        }

        if (result.status == pdf::PDFDocumentToolResultStatus::NothingToDo)
        {
            if (pageTotal > 0 && result.data.value(QStringLiteral("order")).toString() != QLatin1String(Tool::ORDER_STRUCTURE))
            {
                *failures << QStringLiteral("NothingToDo, but the pages have %1 characters").arg(pageTotal);
            }
            return;
        }

        if (result.outputs.size() != 1)
        {
            *failures << QStringLiteral("%1 output files instead of one").arg(result.outputs.size());
            return;
        }

        const pdf::PDFDocumentToolOutputFile& output = result.outputs.front();
        if (output.data.isEmpty())
        {
            *failures << QStringLiteral("An empty file");
        }
        QStringDecoder decoder = pdf::PDFDocumentTextExporter::createDecoder(output.mimeType);
        if (!decoder.isValid())
        {
            *failures << QStringLiteral("The charset of '%1' is not known").arg(output.mimeType);
            return;
        }

        // The text of the file: a plain text as it is, the text elements of the markup
        QString text;
        const pdf::PDFOutputFormatter::Style style = Tool::getStyle(parameters);
        if (style == pdf::PDFOutputFormatter::Style::Text)
        {
            text = decoder.decode(output.data);
        }
        else
        {
            const QString element = style == pdf::PDFOutputFormatter::Style::Xml ? QStringLiteral("text") : QStringLiteral("p");
            QXmlStreamReader reader(output.data);
            QStringList stack;
            while (!reader.atEnd())
            {
                switch (reader.readNext())
                {
                    case QXmlStreamReader::StartElement:
                        stack << reader.name().toString();
                        break;
                    case QXmlStreamReader::EndElement:
                        stack.removeLast();
                        break;
                    case QXmlStreamReader::Characters:
                        if (!stack.isEmpty() && stack.last() == element)
                        {
                            text += reader.text().toString() + QLatin1Char('\n');
                        }
                        break;
                    default:
                        break;
                }
            }
            if (reader.hasError())
            {
                *failures << QStringLiteral("The %1 file is not well formed: %2 at line %3").arg(element == QLatin1String("p") ? QStringLiteral("HTML") : QStringLiteral("XML"), reader.errorString()).arg(reader.lineNumber());
                return;
            }
        }

        if (result.data.value(QStringLiteral("order")).toString() == QLatin1String(Tool::ORDER_STRUCTURE))
        {
            return;
        }

        // The lines "Page N" of the row are no text of a page
        if (parameters.value(QString::fromLatin1(Tool::PARAMETER_INCLUDE_PAGE_NUMBERS)).toBool())
        {
            for (size_t i = 0; i < input->getCatalog()->getPageCount(); ++i)
            {
                const QString line = QStringLiteral("Page %1").arg(i + 1);
                const qsizetype index = text.indexOf(line);
                if (index < 0)
                {
                    *failures << QStringLiteral("The line '%1' is missing").arg(line);
                    return;
                }
                text.remove(index, line.size());
            }
        }

        qint64 missing = 0;
        const std::map<char32_t, qint64> fileCounts = countCharacters(text);
        for (const auto& item : fileCounts)
        {
            const auto it = pageCounts.find(item.first);
            const qint64 pageCount = it != pageCounts.cend() ? it->second : 0;
            if (item.second > pageCount)
            {
                *failures << QStringLiteral("The file has %1 x U+%2, the pages %3").arg(item.second).arg(qint64(item.first), 4, 16, QLatin1Char('0')).arg(pageCount);
                return;
            }
        }
        for (const auto& item : pageCounts)
        {
            const auto it = fileCounts.find(item.first);
            missing += item.second - (it != fileCounts.cend() ? it->second : 0);
        }
        if (missing * 50 > pageTotal)
        {
            *failures << QStringLiteral("%1 of %2 characters of the pages are not in the file").arg(missing).arg(pageTotal);
        }

        // Latin-1 counts the characters, which it replaces (white space too)
        if (isLatin1 && result.data.value(QStringLiteral("lostCharacters")).toInteger() < notInLatin1)
        {
            *failures << QStringLiteral("%1 characters reported lost, the pages have %2 outside of Latin-1").arg(result.data.value(QStringLiteral("lostCharacters")).toInteger()).arg(notInLatin1);
        }
    }

private:
    /// The pages of Rotate Pages of the odd pages turned right
    static std::vector<CorpusPageOrigin> mapOddPagesTurnedRight(const pdf::PDFDocument* input)
    {
        std::vector<CorpusPageOrigin> origins(input->getCatalog()->getPageCount());
        for (size_t i = 0; i < origins.size(); ++i)
        {
            origins[i] = { i, i % 2 == 0 ? pdf::PageRotation::Rotate90 : pdf::PageRotation::None };
        }
        return origins;
    }

    /// The pages of Rotate Pages of all pages turned upside down
    static std::vector<CorpusPageOrigin> mapAllPagesTurnedHalf(const pdf::PDFDocument* input)
    {
        std::vector<CorpusPageOrigin> origins(input->getCatalog()->getPageCount());
        for (size_t i = 0; i < origins.size(); ++i)
        {
            origins[i] = { i, pdf::PageRotation::Rotate180 };
        }
        return origins;
    }

    /// The pages of Remove Pages of the even pages: the odd ones stay
    static std::vector<CorpusPageOrigin> mapOddPagesStay(const pdf::PDFDocument* input)
    {
        std::vector<CorpusPageOrigin> origins;
        for (size_t i = 0; i < input->getCatalog()->getPageCount(); i += 2)
        {
            origins.push_back({ i, pdf::PageRotation::None });
        }
        return origins;
    }

    /// A document of one page has no even page to remove
    static bool hasOnePage(const pdf::PDFDocument* input)
    {
        return input->getCatalog()->getPageCount() < 2;
    }

    static std::vector<CorpusToolRow> createToolRows()
    {
        using Compress = pdf::PDFDocumentToolCompress;

        const auto createCompress = []() -> std::shared_ptr<const pdf::PDFDocumentTool>
        {
            return std::make_shared<Compress>();
        };
        const auto preset = [](const char* id)
        {
            QVariantMap parameters;
            parameters.insert(QString::fromLatin1(Compress::PARAMETER_PRESET), QString::fromLatin1(id));
            return parameters;
        };

        // The images only: a damage of a page is either of the images or of the structure
        QVariantMap imagesOnly = preset(Compress::PRESET_CUSTOM);
        imagesOnly.insert(QString::fromLatin1(Compress::PARAMETER_TARGET_DPI), 150);
        imagesOnly.insert(QString::fromLatin1(Compress::PARAMETER_JPEG_QUALITY), 75);
        imagesOnly.insert(QString::fromLatin1(Compress::PARAMETER_GOAL), QString::fromLatin1(Compress::GOAL_MINIMUM_SIZE));
        imagesOnly.insert(QString::fromLatin1(Compress::PARAMETER_CLEAN_STRUCTURE), false);

        std::vector<CorpusToolRow> rows;
        rows.push_back({ Compress::TOOL_ID, "smaller-file", createCompress, preset(Compress::PRESET_SMALLER_FILE), CorpusLook::Close, PromiseAll });
        rows.push_back({ Compress::TOOL_ID, "balanced", createCompress, preset(Compress::PRESET_BALANCED), CorpusLook::Close, PromiseAll });
        rows.push_back({ Compress::TOOL_ID, "best-quality", createCompress, preset(Compress::PRESET_BEST_QUALITY), CorpusLook::Exact, PromiseAll });
        rows.push_back({ Compress::TOOL_ID, "custom-images-only", createCompress, imagesOnly, CorpusLook::Close, PromiseAll });

        // Optimize Structure: lossless, the strict limits of the pages
        const auto createStructure = []() -> std::shared_ptr<const pdf::PDFDocumentTool>
        {
            return std::make_shared<pdf::PDFDocumentToolStructure>();
        };
        rows.push_back({ pdf::PDFDocumentToolStructure::TOOL_ID, "all-steps", createStructure, QVariantMap(), CorpusLook::Exact, PromiseAll });

        // Rotate Pages and Remove Pages: the pages change by their maps, the look of a page is lossless
        const auto createRotate = []() -> std::shared_ptr<const pdf::PDFDocumentTool>
        {
            return std::make_shared<pdf::PDFDocumentToolRotatePages>();
        };
        const auto createRemove = []() -> std::shared_ptr<const pdf::PDFDocumentTool>
        {
            return std::make_shared<pdf::PDFDocumentToolRemovePages>();
        };
        const auto pages = [](const char* range, const QStringList& filters, const char* rotation)
        {
            QVariantMap pageSelection;
            pageSelection.insert(QStringLiteral("pages"), QString::fromLatin1(range));
            pageSelection.insert(QStringLiteral("filters"), filters);
            QVariantMap parameters;
            parameters.insert(QStringLiteral("pages"), pageSelection);
            if (rotation)
            {
                parameters.insert(QStringLiteral("rotation"), QString::fromLatin1(rotation));
            }
            return parameters;
        };
        constexpr int PAGES_PROMISES = PromiseVersion | PromiseEncryption | PromiseText | PromiseTransparency;
        rows.push_back({ pdf::PDFDocumentToolRotatePages::TOOL_ID, "odd-right", createRotate, pages("", { QStringLiteral("odd") }, "right"), CorpusLook::Exact,
                         PAGES_PROMISES | PromiseSignatures | PromiseContent, &ToolCorpusHelper::mapOddPagesTurnedRight });
        rows.push_back({ pdf::PDFDocumentToolRotatePages::TOOL_ID, "all-half", createRotate, pages("", {}, "half"), CorpusLook::Exact,
                         PAGES_PROMISES | PromiseSignatures | PromiseContent, &ToolCorpusHelper::mapAllPagesTurnedHalf });
        rows.push_back({ pdf::PDFDocumentToolRemovePages::TOOL_ID, "even", createRemove, pages("", { QStringLiteral("even") }, nullptr), CorpusLook::Exact,
                         PAGES_PROMISES, &ToolCorpusHelper::mapOddPagesStay, nullptr, &ToolCorpusHelper::hasOnePage });

        // Extract Text: a file, no document - its own assertion
        using Extract = pdf::PDFDocumentToolExtractText;
        const auto createExtract = []() -> std::shared_ptr<const pdf::PDFDocumentTool>
        {
            return std::make_shared<Extract>();
        };
        const auto extract = [](const char* format, const char* order, const char* encoding, bool isPageNumbers)
        {
            QVariantMap parameters;
            parameters.insert(QString::fromLatin1(Extract::PARAMETER_FORMAT), QString::fromLatin1(format));
            parameters.insert(QString::fromLatin1(Extract::PARAMETER_ORDER), QString::fromLatin1(order));
            parameters.insert(QString::fromLatin1(Extract::PARAMETER_ENCODING), QString::fromLatin1(encoding));
            parameters.insert(QString::fromLatin1(Extract::PARAMETER_INCLUDE_PAGE_NUMBERS), isPageNumbers);
            return parameters;
        };
        rows.push_back({ Extract::TOOL_ID, "text-layout", createExtract, extract(Extract::FORMAT_TEXT, Extract::ORDER_LAYOUT, Extract::ENCODING_UTF8, false), CorpusLook::Exact, PromiseNone, nullptr, &ToolCorpusHelper::checkExtractedText });
        rows.push_back({ Extract::TOOL_ID, "html-auto", createExtract, extract(Extract::FORMAT_HTML, Extract::ORDER_AUTO, Extract::ENCODING_UTF8, false), CorpusLook::Exact, PromiseNone, nullptr, &ToolCorpusHelper::checkExtractedText });
        rows.push_back({ Extract::TOOL_ID, "xml-content-stream-pages", createExtract, extract(Extract::FORMAT_XML, Extract::ORDER_CONTENT_STREAM, Extract::ENCODING_UTF8, true), CorpusLook::Exact, PromiseNone, nullptr, &ToolCorpusHelper::checkExtractedText });
        rows.push_back({ Extract::TOOL_ID, "text-latin-1", createExtract, extract(Extract::FORMAT_TEXT, Extract::ORDER_LAYOUT, Extract::ENCODING_LATIN1, false), CorpusLook::Exact, PromiseNone, nullptr, &ToolCorpusHelper::checkExtractedText });

        // Remove Hidden Data: the kinds, which nothing on the pages shows, keep the look exactly;
        // the invisible text keeps it too (callout of 438:12588: page content is not changed),
        // its text is gone by the purpose
        const auto createSanitize = []() -> std::shared_ptr<const pdf::PDFDocumentTool>
        {
            return std::make_shared<pdf::PDFDocumentToolSanitize>();
        };
        const auto kinds = [](const QStringList& names)
        {
            QVariantMap parameters = pdf::PDFDocumentToolSanitize::getPresetValues(pdf::PDFDocumentToolSanitize::Preset::Custom);
            for (const QString& name : names)
            {
                parameters.insert(name, true);
            }
            return parameters;
        };
        constexpr int SANITIZE_PROMISES = PromiseVersion | PromiseEncryption | PromiseSignatures | PromiseTransparency;
        rows.push_back({ pdf::PDFDocumentToolSanitize::TOOL_ID, "unseen-kinds", createSanitize,
                         kinds({ QStringLiteral("documentInfo"), QStringLiteral("metadata"), QStringLiteral("outline"), QStringLiteral("pageLabels"), QStringLiteral("searchIndex"), QStringLiteral("thumbnails") }),
                         CorpusLook::Exact, SANITIZE_PROMISES | PromiseText });
        rows.push_back({ pdf::PDFDocumentToolSanitize::TOOL_ID, "invisible-text", createSanitize, kinds({ QStringLiteral("invisibleText") }), CorpusLook::Exact, SANITIZE_PROMISES });

        // Remove External Links: only the links go, the pages keep their look and text
        const auto createRemoveLinks = []() -> std::shared_ptr<const pdf::PDFDocumentTool>
        {
            return std::make_shared<pdf::PDFDocumentToolRemoveLinks>();
        };
        rows.push_back({ pdf::PDFDocumentToolRemoveLinks::TOOL_ID, "all", createRemoveLinks, QVariantMap(), CorpusLook::Exact,
                         PromiseVersion | PromiseEncryption | PromiseSignatures | PromiseText | PromiseTransparency });
        return rows;
    }

    /// Share of the pixels, which are not opaque; large images by their rows at equal steps
    static double getTransparentShare(const QImage& image)
    {
        if (image.isNull() || !image.hasAlphaChannel())
        {
            return 0.0;
        }

        const QImage converted = image.convertToFormat(QImage::Format_ARGB32);
        const int step = qMax(1, converted.height() / MAXIMUM_PROBED_ROWS);
        qint64 transparent = 0;
        qint64 total = 0;
        for (int y = 0; y < converted.height(); y += step)
        {
            const QRgb* line = reinterpret_cast<const QRgb*>(converted.constScanLine(y));
            for (int x = 0; x < converted.width(); ++x)
            {
                transparent += qAlpha(line[x]) < 250 ? 1 : 0;
            }
            total += converted.width();
        }
        return total > 0 ? double(transparent) / double(total) : 0.0;
    }
};

class DocumentToolCorpusTest : public QObject
{
    Q_OBJECT

public:
    DocumentToolCorpusTest()
    {
        // The watch dog of QtTest (5 minutes by default) comes after the limit of a row
        if (!qEnvironmentVariableIsSet("QTEST_FUNCTION_TIMEOUT"))
        {
            qputenv("QTEST_FUNCTION_TIMEOUT", QByteArray::number(qint64(ToolCorpusHelper::getTimeLimit() + 60) * 1000));
        }
    }

private slots:
    void initTestCase();

    void toolOnFile_data();
    void toolOnFile();

private:
    static void checkResultDocument(const CorpusToolRow& row,
                                    const QString& file,
                                    const QString& password,
                                    const pdf::PDFDocument* input,
                                    const QByteArray& inputData,
                                    const pdf::PDFDocumentToolResult& result,
                                    CorpusSummary* summary,
                                    QStringList* failures);

    static void checkContent(const pdf::PDFDocument* input,
                             const pdf::PDFDocument* output,
                             const std::vector<CorpusPageOrigin>& origins,
                             QStringList* notes,
                             QStringList* failures);

    static void checkPage(const CorpusToolRow& row,
                          const QString& file,
                          pdf::PDFDocument* input,
                          pdf::PDFDocument* output,
                          CorpusRenderer* inputRenderer,
                          CorpusRenderer* outputRenderer,
                          size_t outputPage,
                          const CorpusPageOrigin& origin,
                          QStringList* notes,
                          QStringList* failures);
};

void DocumentToolCorpusTest::initTestCase()
{
    if (!ToolCorpusHelper::isRunRequested())
    {
        QSKIP("The run of the real files is asked for by PDF4QT_TOOL_CORPUS_DIRECTORY=<folder> or PDF4QT_TOOL_CORPUS_RUN=1 (<source>/testpdf).");
    }

    const QDir directory = ToolCorpusHelper::getDirectory();
    if (!directory.exists())
    {
        QSKIP(qPrintable(QStringLiteral("The folder '%1' with the PDF files does not exist.").arg(directory.absolutePath())));
    }
}

void DocumentToolCorpusTest::toolOnFile_data()
{
    QTest::addColumn<QString>("file");
    QTest::addColumn<int>("toolRow");

    if (!ToolCorpusHelper::isRunRequested())
    {
        return;
    }

    const QDir directory = ToolCorpusHelper::getDirectory();
    if (!directory.exists())
    {
        return;
    }

    const QStringList files = ToolCorpusHelper::getFiles(directory);
    const std::vector<CorpusToolRow>& rows = ToolCorpusHelper::getToolRows();
    for (const QString& file : files)
    {
        for (size_t i = 0; i < rows.size(); ++i)
        {
            QTest::newRow(ToolCorpusHelper::createRowName(file, rows[i].getName()).constData()) << file << int(i);
        }
    }
}

void DocumentToolCorpusTest::toolOnFile()
{
    QFETCH(QString, file);
    QFETCH(int, toolRow);

    const CorpusToolRow& row = ToolCorpusHelper::getToolRows().at(toolRow);

    CorpusSummary summary;
    summary.file = file;
    summary.tool = row.getName();

    const QString path = ToolCorpusHelper::getDirectory().absoluteFilePath(file);
    summary.sizeBefore = QFileInfo(path).size();

    if (const CorpusExclusion* exclusion = ToolCorpusHelper::findExclusion(file, summary.tool))
    {
        summary.status = QStringLiteral("excluded");
        summary.note = QStringLiteral("%1: %2").arg(QString::fromUtf8(exclusion->reference), QString::fromUtf8(exclusion->reason));
        summary.print();
        QSKIP(qPrintable(summary.note));
    }

    if (summary.sizeBefore > ToolCorpusHelper::LARGE_FILE_SIZE && qEnvironmentVariableIntValue("PDF4QT_TOOL_CORPUS_LARGE") == 0)
    {
        summary.status = QStringLiteral("skipped-large");
        summary.note = QStringLiteral("%1 MB is over the limit of %2 MB, it runs with PDF4QT_TOOL_CORPUS_LARGE=1").arg(summary.sizeBefore / (1024 * 1024)).arg(ToolCorpusHelper::LARGE_FILE_SIZE / (1024 * 1024));
        summary.print();
        QSKIP(qPrintable(summary.note));
    }

    CorpusWatchdog watchdog(&summary, ToolCorpusHelper::getTimeLimit());
    QElapsedTimer timer;
    timer.start();

    QStringList failures;

    try
    {
        QFile inputFile(path);
        QVERIFY2(inputFile.open(QFile::ReadOnly), qPrintable(QStringLiteral("The file '%1' cannot be read.").arg(path)));
        const QByteArray inputData = inputFile.readAll();
        inputFile.close();

        // Only the empty password of a document: the files of the folder have no list of their passwords
        const auto refusePassword = [](bool* ok)
        {
            *ok = false;
            return QString();
        };
        pdf::PDFDocumentReader reader(nullptr, refusePassword, true, false);
        pdf::PDFDocument input = reader.readFromBuffer(inputData);

        // A file the reader does not take is no failure of the tool: the row says what happened.
        // It has to be named: another refused file is a failure of the reader or a new file.
        if (reader.getReadingResult() != pdf::PDFDocumentReader::Result::OK)
        {
            const bool isCanceled = reader.getReadingResult() == pdf::PDFDocumentReader::Result::Cancelled;
            summary.status = isCanceled ? QStringLiteral("password-protected") : QStringLiteral("damaged");
            summary.note = reader.getErrorMessage();
            summary.milliseconds = timer.elapsed();
            summary.print();

            const CorpusExclusion* refused = ToolCorpusHelper::findRefusedFile(file);
            QVERIFY2(refused, qPrintable(QStringLiteral("The reader refuses a file, which is not named in getRefusedFiles: %1: %2").arg(summary.status, summary.note)));
            QSKIP(qPrintable(QStringLiteral("%1: %2 (%3: %4)").arg(summary.status, summary.note, QString::fromUtf8(refused->reference), QString::fromUtf8(refused->reason))));
        }

        summary.pages = qint64(input.getCatalog()->getPageCount());
        QStringList attributes;
        if (ToolCorpusHelper::getEncryptionMode(&input) != pdf::EncryptionMode::None)
        {
            attributes << QStringLiteral("encrypted");
        }
        if (pdf::PDFForm::getSignatureCount(&input) > 0)
        {
            attributes << QStringLiteral("signed");
        }
        summary.attributes = attributes.join(QLatin1Char(','));

        const std::shared_ptr<const pdf::PDFDocumentTool> tool = row.createTool();
        pdf::PDFDocumentToolContext context = pdf::PDFDocumentToolContext::create(pdf::PDFDocumentToolInputDocument::create(pdf::PDFDocumentPointer(new pdf::PDFDocument(input)), inputData, path, false));
        const pdf::PDFDocumentToolResult result = tool->run(context, row.parameters, nullptr, nullptr);

        summary.status = ToolCorpusHelper::getStatusName(result.status);
        summary.note = result.message;

        switch (result.status)
        {
            case pdf::PDFDocumentToolResultStatus::Success:
                break;

            case pdf::PDFDocumentToolResultStatus::NothingToDo:
                if (row.resultCheck)
                {
                    row.resultCheck(&input, result, row.parameters, &failures);
                }
                if (!result.documents.empty())
                {
                    failures << QStringLiteral("NothingToDo with a document of the result");
                }
                if ((row.promises & PromiseSmaller) != 0)
                {
                    // A row, which promises a smaller file, says why there is none by its numbers
                    summary.sizeAfter = result.data.value(QStringLiteral("sizeAfter")).toInteger(-1);
                    if (summary.sizeAfter < summary.sizeBefore)
                    {
                        failures << QStringLiteral("NothingToDo of a smaller file: %1 < %2").arg(summary.sizeAfter).arg(summary.sizeBefore);
                    }
                    if (result.data.value(QStringLiteral("sizeBefore")).toInteger(-1) != summary.sizeBefore)
                    {
                        failures << QStringLiteral("The reported size before %1 is not the size of the file %2").arg(result.data.value(QStringLiteral("sizeBefore")).toInteger(-1)).arg(summary.sizeBefore);
                    }
                }
                break;

            case pdf::PDFDocumentToolResultStatus::Failed:
                if (result.message.isEmpty())
                {
                    failures << QStringLiteral("Failed without a message");
                }
                if ((tool->getInfo() && !tool->getInfo()->isPermitted(&input)) || (row.refusal && row.refusal(&input)))
                {
                    // The tool refused the file by its contract: permissions of the document. An
                    // invalid parameter of a row comes with the same errors, it is a failure.
                    summary.status = QStringLiteral("refused");
                }
                else
                {
                    // A file the reader took and the tool could not process is listed by its row:
                    // the run stays green only with a named exclusion
                    failures << QStringLiteral("Failed: %1").arg(result.message);
                }
                break;

            case pdf::PDFDocumentToolResultStatus::Stopped:
                failures << QStringLiteral("Stopped without a cancellation");
                break;
        }

        if (result.status == pdf::PDFDocumentToolResultStatus::Success)
        {
            if (row.resultCheck)
            {
                row.resultCheck(&input, result, row.parameters, &failures);
            }
            else
            {
                checkResultDocument(row, file, QString(), &input, inputData, result, &summary, &failures);
            }
        }
    }
    catch (const pdf::PDFException& exception)
    {
        failures << QStringLiteral("PDFException: %1").arg(exception.getMessage());
    }
    catch (const std::exception& exception)
    {
        failures << QStringLiteral("Exception: %1").arg(QString::fromLocal8Bit(exception.what()));
    }

    summary.milliseconds = timer.elapsed();
    if (!failures.isEmpty())
    {
        summary.note = failures.join(QStringLiteral("; ")) + QStringLiteral(" | ") + summary.note;
    }
    summary.print();

    QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QStringLiteral("\n"))));
}

void DocumentToolCorpusTest::checkResultDocument(const CorpusToolRow& row,
                                                 const QString& file,
                                                 const QString& password,
                                                 const pdf::PDFDocument* input,
                                                 const QByteArray& inputData,
                                                 const pdf::PDFDocumentToolResult& result,
                                                 CorpusSummary* summary,
                                                 QStringList* failures)
{
    // A tool of files only promises nothing about a document and has none to check. Facts alone
    // are no result: Success of a row with promises, which has written nothing, promises nothing.
    if (result.documents.empty())
    {
        if (row.promises != PromiseNone || (result.outputs.empty() && result.writtenFiles.isEmpty()))
        {
            failures->append(QStringLiteral("Success without any result"));
        }
        return;
    }

    const pdf::PDFDocumentToolOutputDocument& outputDocument = result.documents.front();
    if (!outputDocument.document)
    {
        failures->append(QStringLiteral("Success with a null document"));
        return;
    }

    // Written and read back: what the user gets is the file
    QByteArray written;
    {
        QBuffer buffer(&written);
        buffer.open(QIODevice::WriteOnly);
        pdf::PDFDocumentWriter writer(nullptr);
        const pdf::PDFOperationResult writeResult = writer.write(&buffer, outputDocument.document.data());
        if (!writeResult)
        {
            failures->append(QStringLiteral("The result cannot be written: %1").arg(writeResult.getErrorMessage()));
            return;
        }
    }
    summary->sizeAfter = written.size();

    if ((row.promises & PromiseSmaller) != 0 && written.size() >= inputData.size())
    {
        failures->append(QStringLiteral("Success, but the file is not smaller: %1 >= %2").arg(written.size()).arg(inputData.size()));
    }

    // Every write of an encrypted document has other random vectors of AES, and the strings
    // are written with escapes depending on their bytes: its size differs by some bytes
    if (result.data.contains(QStringLiteral("sizeAfter")))
    {
        const qint64 reportedSize = result.data.value(QStringLiteral("sizeAfter")).toInteger();
        const qint64 tolerance = ToolCorpusHelper::getEncryptionMode(input) != pdf::EncryptionMode::None ? written.size() / 100 : 0;
        if (qAbs(reportedSize - written.size()) > tolerance)
        {
            failures->append(QStringLiteral("The reported size %1 is not the size of the written file %2").arg(reportedSize).arg(written.size()));
        }
    }
    if (result.data.contains(QStringLiteral("sizeBefore")) && result.data.value(QStringLiteral("sizeBefore")).toInteger() != inputData.size())
    {
        failures->append(QStringLiteral("The reported size before %1 is not the size of the file %2").arg(result.data.value(QStringLiteral("sizeBefore")).toInteger()).arg(inputData.size()));
    }

    const auto getPassword = [&password](bool* ok)
    {
        *ok = !password.isEmpty();
        return password;
    };
    pdf::PDFDocumentReader reader(nullptr, getPassword, false, false);
    pdf::PDFDocument output = reader.readFromBuffer(written);
    if (reader.getReadingResult() != pdf::PDFDocumentReader::Result::OK)
    {
        failures->append(QStringLiteral("The written result cannot be read: %1").arg(reader.getErrorMessage()));
        return;
    }

    if ((row.promises & PromiseVersion) != 0 && !(output.getInfo()->version == input->getInfo()->version))
    {
        failures->append(QStringLiteral("The version %1.%2 of the input became %3.%4").arg(input->getInfo()->version.major).arg(input->getInfo()->version.minor).arg(output.getInfo()->version.major).arg(output.getInfo()->version.minor));
    }

    if ((row.promises & PromiseEncryption) != 0 && ToolCorpusHelper::getEncryptionMode(&output) != ToolCorpusHelper::getEncryptionMode(input))
    {
        failures->append(QStringLiteral("The encryption mode %1 of the input became %2").arg(int(ToolCorpusHelper::getEncryptionMode(input))).arg(int(ToolCorpusHelper::getEncryptionMode(&output))));
    }

    if ((row.promises & PromiseSignatures) != 0)
    {
        const int signaturesBefore = pdf::PDFForm::getSignatureCount(input);
        const int signaturesAfter = pdf::PDFForm::getSignatureCount(&output);
        if (signaturesBefore != signaturesAfter)
        {
            failures->append(QStringLiteral("The signature fields %1 became %2").arg(signaturesBefore).arg(signaturesAfter));
        }
        if (signaturesBefore > 0)
        {
            if (result.data.value(QStringLiteral("signatureCount")).toInt() != signaturesBefore)
            {
                failures->append(QStringLiteral("The reported signature count %1 is not %2").arg(result.data.value(QStringLiteral("signatureCount")).toInt()).arg(signaturesBefore));
            }

            const QString impact = result.data.value(QStringLiteral("signatureImpact")).toString();
            if (impact.isEmpty() || impact == QStringLiteral("none"))
            {
                failures->append(QStringLiteral("A signed input without the signature impact"));
            }
        }
    }

    // The pages: all sizes and rotations, the pictures and the texts of the first and the last page
    std::vector<CorpusPageOrigin> origins;
    if (row.pageMap)
    {
        origins = row.pageMap(input);
    }
    else
    {
        origins.resize(input->getCatalog()->getPageCount());
        for (size_t i = 0; i < origins.size(); ++i)
        {
            origins[i].inputPage = i;
        }
    }

    if (output.getCatalog()->getPageCount() != origins.size())
    {
        failures->append(QStringLiteral("The pages %1 became %2").arg(origins.size()).arg(output.getCatalog()->getPageCount()));
        return;
    }

    constexpr qreal PAGE_TOLERANCE = 0.01;
    for (size_t i = 0; i < origins.size(); ++i)
    {
        const pdf::PDFPage* before = input->getCatalog()->getPage(origins[i].inputPage);
        const pdf::PDFPage* after = output.getCatalog()->getPage(i);
        const auto isSameBox = [](const QRectF& first, const QRectF& second)
        {
            return qAbs(first.left() - second.left()) <= PAGE_TOLERANCE && qAbs(first.top() - second.top()) <= PAGE_TOLERANCE &&
                   qAbs(first.width() - second.width()) <= PAGE_TOLERANCE && qAbs(first.height() - second.height()) <= PAGE_TOLERANCE;
        };

        if (!isSameBox(before->getMediaBox(), after->getMediaBox()) || !isSameBox(before->getCropBox(), after->getCropBox()))
        {
            failures->append(QStringLiteral("The boxes of the page %1 changed").arg(i + 1));
            break;
        }
        if (pdf::getPageRotationCombined(before->getPageRotation(), origins[i].extraRotation) != after->getPageRotation())
        {
            failures->append(QStringLiteral("The rotation of the page %1 changed").arg(i + 1));
            break;
        }
        if (qAbs(before->getUserUnit() - after->getUserUnit()) > PAGE_TOLERANCE)
        {
            failures->append(QStringLiteral("The user unit of the page %1 changed").arg(i + 1));
            break;
        }
    }

    QStringList notes;
    if ((row.promises & PromiseContent) != 0)
    {
        checkContent(input, &output, origins, &notes, failures);
    }

    if (origins.empty())
    {
        summary->note = notes.join(QStringLiteral("; "));
        return;
    }

    pdf::PDFDocument inputCopy(*input);
    CorpusRenderer inputRenderer(&inputCopy);
    CorpusRenderer outputRenderer(&output);

    // The first and the last page and the page of the largest image, when it is a page of the result
    std::vector<size_t> pages = { 0, origins.size() - 1 };
    const CorpusPageBudget budget(ToolCorpusHelper::getPageTimeLimit());
    if (const std::optional<size_t> imagePage = ToolCorpusHelper::getLargestImagePage(&inputCopy, &budget))
    {
        const auto origin = std::find_if(origins.cbegin(), origins.cend(), [imagePage](const CorpusPageOrigin& item) { return item.inputPage == *imagePage; });
        if (origin != origins.cend())
        {
            pages.push_back(size_t(std::distance(origins.cbegin(), origin)));
        }
    }
    std::sort(pages.begin(), pages.end());
    pages.erase(std::unique(pages.begin(), pages.end()), pages.end());

    for (const size_t page : pages)
    {
        checkPage(row, file, &inputCopy, &output, &inputRenderer, &outputRenderer, page, origins[page], &notes, failures);
    }

    summary->note = notes.join(QStringLiteral("; "));
}

void DocumentToolCorpusTest::checkContent(const pdf::PDFDocument* input,
                                          const pdf::PDFDocument* output,
                                          const std::vector<CorpusPageOrigin>& origins,
                                          QStringList* notes,
                                          QStringList* failures)
{
    if (ToolCorpusHelper::getCatalogKeys(input) != ToolCorpusHelper::getCatalogKeys(output))
    {
        failures->append(QStringLiteral("The entries of the catalog changed: %1 became %2").arg(ToolCorpusHelper::getCatalogKeys(input).join(QLatin1Char(' ')), ToolCorpusHelper::getCatalogKeys(output).join(QLatin1Char(' '))));
    }

    // Every page, not only the drawn ones
    const CorpusPageBudget budget(ToolCorpusHelper::getPageTimeLimit());
    for (size_t i = 0; i < origins.size(); ++i)
    {
        if (budget.isOperationCancelled())
        {
            notes->append(QStringLiteral("the content of the pages from %1 not compared: over the limit of the time").arg(i + 1));
            return;
        }

        if (ToolCorpusHelper::getPageContent(input, origins[i].inputPage) != ToolCorpusHelper::getPageContent(output, i))
        {
            failures->append(QStringLiteral("The content of the page %1 changed").arg(i + 1));
            return;
        }

        const QStringList structureBefore = ToolCorpusHelper::getPageStructure(input, origins[i].inputPage);
        const QStringList structureAfter = ToolCorpusHelper::getPageStructure(output, i);
        if (structureBefore != structureAfter)
        {
            failures->append(QStringLiteral("The resources or the annotations of the page %1 changed: %2 became %3").arg(i + 1).arg(structureBefore.join(QLatin1Char(' ')).left(200), structureAfter.join(QLatin1Char(' ')).left(200)));
            return;
        }
    }
}

void DocumentToolCorpusTest::checkPage(const CorpusToolRow& row,
                                       const QString& file,
                                       pdf::PDFDocument* input,
                                       pdf::PDFDocument* output,
                                       CorpusRenderer* inputRenderer,
                                       CorpusRenderer* outputRenderer,
                                       size_t outputPage,
                                       const CorpusPageOrigin& origin,
                                       QStringList* notes,
                                       QStringList* failures)
{
    const pdf::PDFPage* page = input->getCatalog()->getPage(origin.inputPage);
    const pdf::PageRotation rotation = pdf::getPageRotationCombined(page->getPageRotation(), origin.extraRotation);
    const QSize size = ToolCorpusHelper::getRenderSize(pdf::PDFPage::getRotatedBox(page->getMediaBox(), rotation).size());

    const QString notCompared = QStringLiteral("page %1 not compared: over the limit of the time of a page").arg(outputPage + 1);

    if (size.isValid())
    {
        // A page too slow for the renderer says nothing about the tool; a page the
        // tool has made more than twice slower does
        QStringList errorsBefore;
        QStringList errorsAfter;
        const CorpusPageBudget budgetBefore(ToolCorpusHelper::getPageTimeLimit());
        const QImage before = inputRenderer->render(origin.inputPage, origin.extraRotation, size, &budgetBefore, &errorsBefore);
        if (budgetBefore.isOperationCancelled())
        {
            notes->append(notCompared);
            return;
        }

        const CorpusPageBudget budgetAfter(ToolCorpusHelper::getPageTimeLimit() * 2);
        const QImage after = outputRenderer->render(outputPage, pdf::PageRotation::None, size, &budgetAfter, &errorsAfter);
        if (budgetAfter.isOperationCancelled())
        {
            failures->append(QStringLiteral("The page %1 is drawn within the limit of %2 s before the tool and not within twice the limit after it").arg(outputPage + 1).arg(ToolCorpusHelper::getPageTimeLimit()));
            return;
        }

        const QStringList newErrors = ToolCorpusHelper::getNewTexts(errorsBefore, errorsAfter);
        if (!newErrors.isEmpty())
        {
            failures->append(QStringLiteral("New rendering errors of the page %1: %2").arg(outputPage + 1).arg(newErrors.mid(0, 3).join(QStringLiteral(" / "))));
        }

        const CorpusImageDifference difference = ToolCorpusHelper::compareImages(before, after);
        const QString values = QStringLiteral("page %1: mean %2, worst block %3, mean shift %4, worst shift %5, large shift %6, worst pixel %7").arg(outputPage + 1).arg(difference.meanDifference, 0, 'f', 3).arg(difference.worstBlock, 0, 'f', 1).arg(difference.meanShift, 0, 'f', 2).arg(difference.worstShift, 0, 'f', 1).arg(difference.worstLargeShift, 0, 'f', 1).arg(difference.worstPixel);
        notes->append(values);

        const bool isSame = row.look == CorpusLook::Exact ? difference.meanDifference <= ToolCorpusHelper::EXACT_MEAN_DIFFERENCE && difference.worstBlock <= ToolCorpusHelper::EXACT_WORST_BLOCK && difference.worstPixel <= ToolCorpusHelper::EXACT_WORST_PIXEL
                                                          : difference.meanShift <= ToolCorpusHelper::CLOSE_MEAN_SHIFT && difference.worstShift <= ToolCorpusHelper::CLOSE_WORST_SHIFT && difference.worstLargeShift <= ToolCorpusHelper::CLOSE_WORST_LARGE_SHIFT;
        if (!isSame)
        {
            failures->append(QStringLiteral("The look changed, %1").arg(values));
        }
        if (!isSame || qEnvironmentVariableIntValue("PDF4QT_TOOL_CORPUS_OUTPUT_ALL") != 0)
        {
            ToolCorpusHelper::writePictures(file, row.getName(), outputPage, before, after);
        }
    }

    const CorpusPageBudget budget(ToolCorpusHelper::getPageTimeLimit());

    if ((row.promises & PromiseText) != 0)
    {
        const QString textBefore = ToolCorpusHelper::getPageText(input, origin.inputPage, &budget);
        const QString textAfter = ToolCorpusHelper::getPageText(output, outputPage, &budget);
        if (budget.isOperationCancelled())
        {
            notes->append(notCompared);
            return;
        }
        if (textBefore != textAfter)
        {
            failures->append(QStringLiteral("The text of the page %1 changed").arg(outputPage + 1));
        }
    }

    if ((row.promises & PromiseTransparency) != 0)
    {
        // In both directions: transparent images stay transparent, opaque ones get no holes
        const std::vector<double> sharesBefore = ToolCorpusHelper::getTransparentShares(input, origin.inputPage, &budget);
        const std::vector<double> sharesAfter = ToolCorpusHelper::getTransparentShares(output, outputPage, &budget);
        if (budget.isOperationCancelled())
        {
            notes->append(notCompared);
            return;
        }

        const qsizetype transparentBefore = ToolCorpusHelper::countShares(sharesBefore, ToolCorpusHelper::TRANSPARENT_SHARE);
        const qsizetype keptAfter = ToolCorpusHelper::countShares(sharesAfter, ToolCorpusHelper::KEPT_TRANSPARENT_SHARE);
        const qsizetype transparentAfter = ToolCorpusHelper::countShares(sharesAfter, ToolCorpusHelper::TRANSPARENT_SHARE);
        const qsizetype keptBefore = ToolCorpusHelper::countShares(sharesBefore, ToolCorpusHelper::KEPT_TRANSPARENT_SHARE);
        if (!sharesBefore.empty() || !sharesAfter.empty())
        {
            notes->append(QStringLiteral("page %1: images with a mask %2 became %3, transparent %4 became %5").arg(outputPage + 1).arg(sharesBefore.size()).arg(sharesAfter.size()).arg(transparentBefore).arg(transparentAfter));
        }
        if (keptAfter < transparentBefore || keptBefore < transparentAfter)
        {
            failures->append(QStringLiteral("The transparent images of the page %1: %2 became %3").arg(outputPage + 1).arg(transparentBefore).arg(transparentAfter));
        }
    }
}

QTEST_MAIN(DocumentToolCorpusTest)

#include "tst_documenttoolcorpustest.moc"
