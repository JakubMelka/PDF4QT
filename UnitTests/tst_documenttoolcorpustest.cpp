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
#include "pdfdocmdppermissions.h"
#include "pdfdocumentreader.h"
#include "pdfdocumenttextexporter.h"
#include "pdfdocumenttextflow.h"
#include "pdfdocumentimagelist.h"
#include "pdfdocumenttool_audiobook.h"
#include "pdfdocumenttool_bitonal.h"
#include "pdfdocumenttool_extractimages.h"
#include "pdfdocumenttool_extracttext.h"
#include "pdfdocumenttool_inkcoverage.h"
#include "pdfdocumenttool_outputpreview.h"
#include "pdfdocumenttool_objectinspector.h"
#include "pdfdocumenttool_report.h"
#include "pdfdocumenttool_statistics.h"
#include "pdfobjectinspector.h"
#include "pdfdocumenttool_compress.h"
#include "pdfdocumenttool_encryption.h"
#include "pdfdocumenttool_grayscale.h"
#include "pdfdocumenttool_optimizeimages.h"
#include "pdfdocumenttool_pdftoimages.h"
#include "pdfdocumenttool_manage.h"
#include "pdfdocumenttool_merge.h"
#include "pdfdocumenttool_protect.h"
#include "pdfdocumenttool_signature.h"
#include "pdfdocumenttool_redact.h"
#include "pdfredact.h"
#include "pdfdocumentsigner.h"
#include "pdfcertificatemanager.h"
#include "pdfsignaturehandler.h"
#include "pdfdocumenttool_pages.h"
#include "pdfdocumenttool_split.h"
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
#include "pdfspeechsynthesizer.h"
#include "pdftextlayout.h"

#include <QBuffer>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
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
#include <set>
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
    PromiseAll          = 0x7F,     ///< All the above (the tools, which keep the pages)
    PromiseCertification = 0x80     ///< A change of the pages: a certified input loses its certification and the signatures (Q12, R10), see checkResultDocument
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

class CorpusMemorySink;

/// A tool of image files (PDF to Images, Extract Images; step 4 of phase 2) checks the files of the
/// file sink of its context instead of the checks of a document (Success and NothingToDo)
/// \param input The input document
/// \param result The result of the tool
/// \param parameters The parameters of the row
/// \param sink The files written by the tool, in their order
/// \param failures The failures of the row
using CorpusFilesCheck = void(*)(const pdf::PDFDocument* input, const pdf::PDFDocumentToolResult& result, const QVariantMap& parameters, const CorpusMemorySink& sink, QStringList* failures);

/// The tool refuses the input by its contract with the parameters of the row (Remove Pages of
/// the even pages of a document of one page: a document keeps at least one page)
using CorpusRefusal = bool(*)(const pdf::PDFDocument* input);

/// The tool refuses the input by the way the reader has read its file (\p isRestored: the reader
/// has restored the objects of the file of its bytes, its cross-reference sections are not valid)
using CorpusReadRefusal = bool(*)(const pdf::PDFDocument* input, bool isRestored);

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
    int inputCount = 1;     ///< The file is given this many times (Merge of a file with itself, every collision of names)
    bool isFilePerPage = false; ///< The tool writes a file per page of the page map by the file sink of the context (Split, Extract), each one is checked as a new document of that page
    std::function<QVariantMap(const pdf::PDFDocument*, const QString&)> createParameters = nullptr;    ///< Parameters made of the read file and its path in place of parameters (the assembly of Manage Pages)
    CorpusFilesCheck filesCheck = nullptr;  ///< The tool writes image files by the file sink of the context, this checks them (no document)
    std::function<void(pdf::PDFDocumentToolContext*)> prepareContext = nullptr;   ///< Completes the context (the fake speech synthesizer of Create Audio Book)
    QString password;   ///< The open password of the result, which is read back by it (the tools of the encryption, step 5 of phase 2)
    CorpusReadRefusal readRefusal = nullptr;    ///< The tool refuses the file by the way it was read (Sign of a restored signed file)

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

/// The file sink of a row of files: the files stay in the memory in the order of writing,
/// nothing is written to the disk (the folder of the files is read only)
class CorpusMemorySink : public pdf::PDFDocumentToolFileSink
{
public:
    CorpusMemorySink() : pdf::PDFDocumentToolFileSink(ExistingFiles::Overwrite) { }

    virtual bool write(const QString& fileName, const QByteArray& data, QString* errorText) override
    {
        Q_UNUSED(errorText);
        files.emplace_back(fileName, data);
        addWrittenFile(QStringLiteral("/memory/") + fileName);
        return true;
    }

    std::vector<std::pair<QString, QByteArray>> files;
};

/// The speech of the rows of Create Audio Book: never a voice of the system (the run must not
/// depend on the installed voices) - 16 bit mono at 16 kHz, 20 bytes per character of the text
class CorpusFakeSynthesizer : public pdf::PDFSpeechSynthesizer
{
public:
    virtual bool setVoice(const QString&, QString*) override { return true; }
    virtual void setRate(double) override { }
    virtual void setVolume(double) override { }
    virtual bool synthesize(const QString& text, pdf::PDFAudioSink* sink, const pdf::PDFOperationControl*, QString*) override
    {
        const pdf::PDFAudioFormat format{ 16000, 1, pdf::PDFAudioFormat::SampleFormat::Int16 };
        return sink->writeAudio(format, QByteArray(int(qMin<qsizetype>(text.size(), 100000)) * 20, '\x10'));
    }
};

/// The factory of the fake speech: one voice
class CorpusFakeSynthesizerFactory : public pdf::PDFSpeechSynthesizerFactory
{
public:
    virtual bool isAvailable() const override { return true; }
    virtual std::vector<pdf::PDFSpeechVoice> getVoices() const override { return { { QStringLiteral("Corpus"), QStringLiteral("en-US") } }; }
    virtual std::unique_ptr<pdf::PDFSpeechSynthesizer> createSynthesizer(QString*) const override { return std::make_unique<CorpusFakeSynthesizer>(); }
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

    virtual bool isOperationCanceled() const override { return m_timer.hasExpired(m_milliseconds); }

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

        if (pdf::PDFOperationControl::isOperationCanceled(control))
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

    /// The page has a widget of a signature field, signed or not (12.7.5.5; /FT inherited, Table 226)
    static bool hasSignatureWidget(const pdf::PDFDocument* document, size_t pageIndex)
    {
        const pdf::PDFPage* page = pageIndex < document->getCatalog()->getPageCount() ? document->getCatalog()->getPage(pageIndex) : nullptr;
        if (!page)
        {
            return false;
        }

        const std::vector<pdf::PDFObjectReference>& annotations = page->getAnnotations();
        bool hasWidget = false;
        try
        {
            const pdf::PDFForm form = pdf::PDFForm::parse(document, document->getCatalog()->getFormObject());
            form.apply([&annotations, &hasWidget](const pdf::PDFFormField* field)
            {
                if (!field || field->getFieldType() != pdf::PDFFormField::FieldType::Signature)
                {
                    return;
                }
                for (const pdf::PDFFormWidget& widget : field->getWidgets())
                {
                    hasWidget = hasWidget || std::find(annotations.cbegin(), annotations.cend(), widget.getWidget()) != annotations.cend();
                }
            });
        }
        catch (const pdf::PDFException&)
        {
            // A damaged form has no field, whose widget a clone could leave out
            return false;
        }
        return hasWidget;
    }

    /// Exclusions by the content of a read file (any folder): a signed input of Merge - a signature
    /// with a value (PDFForm::getSignatureCount) - is removed with its appearance, so the pages of a
    /// signed file look different by design (owner answer Q4 of step 2 of phase 2). Split and Extract
    /// write new documents by the same rule (plan of step 3 of phase 2, chapter 8: the values of the
    /// signatures are removed, a new file).
    static const CorpusExclusion* findContentExclusion(const pdf::PDFDocument* input, const QString& tool)
    {
        static const CorpusExclusion signedMerge = { "", "^pages\\.(merge|split|extract)/|^pages\\.manage/(one-pdf|per-group|per-page)-", "Owner answer Q4 of step 2 of phase 2",
                                                     "Merge, Split, Extract and the new documents of Manage Pages remove a signature with its appearance (the field stays empty): the pages of a signed file look different by design" };
        if (qEnvironmentVariableIntValue("PDF4QT_TOOL_CORPUS_NO_EXCLUSIONS") != 0)
        {
            return nullptr;
        }

        // Manage Pages: a clone in the document itself gets no widget of a signature field (RB1 of
        // step 3b: /FT /Sig, signed or not), so the clone of a signed page, or of the first page
        // with a widget of an unsigned signature field (its value null or unreadable, 7.3.7,
        // Signature/invalid/usf-2.pdf), looks different by design
        static const CorpusExclusion signedClone = { "", "^pages\\.manage/odd-even-clone-first$", "RB1 of step 3b of phase 2",
                                                     "A clone in the document itself has no widget of a signature field: the clone of a signed page looks different by design" };

        const bool isSigned = pdf::PDFForm::getSignatureCount(input) > 0;
        if ((isSigned || hasSignatureWidget(input, 0)) && QRegularExpression(QString::fromLatin1(signedClone.variantPattern)).match(tool).hasMatch())
        {
            return &signedClone;
        }
        return isSigned && QRegularExpression(QString::fromLatin1(signedMerge.variantPattern)).match(tool).hasMatch() ? &signedMerge : nullptr;
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

    /// Document Statistics (plan of step 7 of phase 2, 4.2): every object of the storage in exactly one
    /// class, the shares of the classes and of the types sum to 100 %, the CSV of the view has a row per class
    static void checkStatistics(const pdf::PDFDocument* input, const pdf::PDFDocumentToolResult& result, const QVariantMap& parameters, QStringList* failures)
    {
        Q_UNUSED(parameters);
        using Tool = pdf::PDFDocumentToolStatistics;

        qint64 objects = 0;
        for (const pdf::PDFObjectStorage::Entry& entry : input->getStorage().getObjects())
        {
            objects += entry.object.isNull() ? 0 : 1;
        }

        qint64 classified = 0;
        double share = 0.0;
        const QJsonArray classes = result.data.value(QString::fromLatin1(Tool::DATA_CLASSES)).toArray();
        for (const QJsonValue& value : classes)
        {
            classified += value.toObject().value(QString::fromLatin1(Tool::ROW_COUNT)).toInteger();
            share += value.toObject().value(QString::fromLatin1(Tool::ROW_SHARE)).toDouble();
        }
        if (classified != objects || result.data.value(QString::fromLatin1(Tool::DATA_OBJECT_COUNT)).toInteger() != objects)
        {
            *failures << QStringLiteral("%1 objects classified of %2").arg(classified).arg(objects);
        }
        if (objects > 0 && std::abs(share - 100.0) > 0.001)
        {
            *failures << QStringLiteral("The shares of the classes sum to %1 %").arg(share);
        }
        if (result.outputs.size() != 1 || QString::fromUtf8(result.outputs.front().data).split(QStringLiteral("\r\n"), Qt::SkipEmptyParts).size() != classes.size() + 1)
        {
            *failures << QStringLiteral("The CSV is not a header and a row per class");
        }
    }

    /// Object Inspector (plan of step 7 of phase 2, 4.3): the trailer as JSON; every view of the inspector
    /// is listed, the first objects of the Object List expanded and the content streams decoded within the
    /// limit of Q7.18 - a hostile file never crashes, hangs or exhausts the memory
    static void checkObjectInspector(const pdf::PDFDocument* input, const pdf::PDFDocumentToolResult& result, const QVariantMap& parameters, QStringList* failures)
    {
        Q_UNUSED(parameters);
        constexpr qint64 MAXIMUM_ITEMS = 200;

        if (result.data.value(QStringLiteral("object")).toObject().value(QStringLiteral("type")).toString() != QLatin1String("dictionary"))
        {
            *failures << QStringLiteral("The trailer is not a dictionary");
        }

        const pdf::PDFObjectInspector inspector(pdf::PDFDocumentPointer(new pdf::PDFDocument(*input)));
        for (const pdf::PDFObjectInspectorView view : pdf::PDFObjectInspector::getViews())
        {
            pdf::PDFObjectInspector::OpenedReferences opened;
            const std::vector<pdf::PDFObjectInspectorNode> roots = inspector.getRoots(view, &opened, 0, MAXIMUM_ITEMS);
            if (qint64(roots.size()) != qMin(MAXIMUM_ITEMS, inspector.getRootCount(view)))
            {
                *failures << QStringLiteral("The view %1 lists %2 of %3 roots").arg(pdf::PDFObjectInspector::getViewId(view)).arg(roots.size()).arg(inspector.getRootCount(view));
            }
            for (const pdf::PDFObjectInspectorNode& root : roots)
            {
                inspector.describe(root);
                inspector.getSyntax(root, nullptr);
                for (const pdf::PDFObjectInspectorNode& child : inspector.getChildren(root, &opened))
                {
                    inspector.getChildren(child, &opened);
                }
                if (view == pdf::PDFObjectInspectorView::ContentStreams)
                {
                    const pdf::PDFObjectInspector::DecodedStream decoded = inspector.decodeStream(root.reference, pdf::PDFObjectInspector::INSPECTOR_MAXIMUM_DECODED_SIZE, nullptr);
                    if (qint64(decoded.data.size()) > pdf::PDFObjectInspector::MAXIMUM_SHOWN_BYTES)
                    {
                        *failures << QStringLiteral("The stream %1 shows %2 bytes").arg(pdf::PDFObjectInspector::getReferenceText(root.reference)).arg(decoded.data.size());
                    }
                }
            }
        }
    }

    /// Ink Coverage (plan of step 7 of phase 2, 4.5, P3a): every page is a row or an unmeasured page with
    /// its reason (Q7.9 b); a ratio is 0 to 100 %, the area of an ink is at most the page (a tint and an
    /// opacity are at most 1); the total is the sum of the rows, each page once (B163); the process inks
    /// are the CMYK of the output (B196), /None and /All are no inks (B198)
    static void checkInkCoverage(const pdf::PDFDocument* input, const pdf::PDFDocumentToolResult& result, const QVariantMap& parameters, QStringList* failures)
    {
        Q_UNUSED(parameters);
        using Tool = pdf::PDFDocumentToolInkCoverage;
        constexpr double EPSILON = 1.0e-6;

        const QJsonArray inks = result.data.value(QString::fromLatin1(Tool::DATA_INKS)).toArray();
        const QJsonArray rows = result.data.value(QString::fromLatin1(Tool::DATA_PAGES)).toArray();
        const QJsonArray unmeasured = result.data.value(QString::fromLatin1(Tool::DATA_UNMEASURED_PAGES)).toArray();
        if (size_t(rows.size() + unmeasured.size()) != input->getCatalog()->getPageCount())
        {
            *failures << QStringLiteral("%1 rows and %2 unmeasured pages of %3 pages").arg(rows.size()).arg(unmeasured.size()).arg(input->getCatalog()->getPageCount());
        }

        const QStringList process = { QStringLiteral("Cyan"), QStringLiteral("Magenta"), QStringLiteral("Yellow"), QStringLiteral("Black") };
        for (qsizetype i = 0; i < inks.size(); ++i)
        {
            const QString name = inks.at(i).toObject().value(QStringLiteral("name")).toString();
            if ((i < process.size() && name != process.at(i)) || name == QLatin1String("None") || name == QLatin1String("All"))
            {
                *failures << QStringLiteral("The ink %1 is '%2'").arg(i + 1).arg(name);
            }
        }

        std::vector<double> sums(size_t(inks.size()), 0.0);
        for (const QJsonValue& rowValue : rows)
        {
            const QJsonObject row = rowValue.toObject();
            const QJsonArray areas = row.value(QStringLiteral("area")).toArray();
            const QJsonArray ratios = row.value(QStringLiteral("ratio")).toArray();
            const double pageArea = row.value(QStringLiteral("pageArea")).toDouble();
            for (qsizetype i = 0; i < inks.size(); ++i)
            {
                const double area = areas.at(i).toDouble();
                const double ratio = ratios.at(i).toDouble();
                if (!(area >= 0.0 && area <= pageArea * (1.0 + EPSILON)) || !(ratio >= 0.0 && ratio <= 100.0 + EPSILON))
                {
                    *failures << QStringLiteral("Page %1, ink %2: area %3 of %4 mm², ratio %5 %").arg(row.value(QStringLiteral("page")).toInteger()).arg(i + 1).arg(area).arg(pageArea).arg(ratio);
                }
                sums[size_t(i)] += area;
            }
        }

        const QJsonArray totals = result.data.value(QString::fromLatin1(Tool::DATA_TOTAL)).toObject().value(QStringLiteral("area")).toArray();
        for (qsizetype i = 0; i < inks.size(); ++i)
        {
            if (std::abs(totals.at(i).toDouble() - sums[size_t(i)]) > EPSILON * qMax(1.0, sums[size_t(i)]))
            {
                *failures << QStringLiteral("The total of the ink %1 is %2, the sum of the pages %3").arg(i + 1).arg(totals.at(i).toDouble()).arg(sums[size_t(i)]);
            }
        }
    }

    /// Output Preview (plan of step 7 of phase 2, 4.6, P3a): the measurements of page 1 - the areas are
    /// parts of the page, a ratio is 0 to 100 %, the total area coverage of a pixel is at most 100 % per ink
    static void checkOutputPreview(const pdf::PDFDocument* input, const pdf::PDFDocumentToolResult& result, const QVariantMap& parameters, QStringList* failures)
    {
        Q_UNUSED(input);
        Q_UNUSED(parameters);
        using Tool = pdf::PDFDocumentToolOutputPreview;
        constexpr double EPSILON = 1.0e-6;

        const double mediaArea = result.data.value(QStringLiteral("mediaArea")).toDouble();
        const QJsonArray inks = result.data.value(QString::fromLatin1(Tool::DATA_INKS)).toArray();
        qsizetype measuredInks = 0;
        for (const QJsonValue& inkValue : inks)
        {
            const QJsonObject ink = inkValue.toObject();
            measuredInks += ink.value(QStringLiteral("isMeasured")).toBool() ? 1 : 0;
            const double area = ink.value(QStringLiteral("area")).toDouble();
            const double ratio = ink.value(QStringLiteral("ratio")).toDouble();
            if (!(area >= 0.0 && area <= mediaArea * (1.0 + EPSILON)) || !(ratio >= 0.0 && ratio <= 100.0 + EPSILON))
            {
                *failures << QStringLiteral("The ink %1: area %2 of %3 mm², ratio %4 %").arg(ink.value(QStringLiteral("name")).toString()).arg(area).arg(mediaArea).arg(ratio);
            }
        }

        const double maximumCoverage = result.data.value(QString::fromLatin1(Tool::DATA_MAXIMUM_COVERAGE)).toDouble();
        const double overLimit = result.data.value(QString::fromLatin1(Tool::DATA_AREA_OVER_COVERAGE_LIMIT)).toDouble();
        const double richBlack = result.data.value(QString::fromLatin1(Tool::DATA_RICH_BLACK_AREA)).toDouble();
        if (!(maximumCoverage >= 0.0 && maximumCoverage <= 100.0 * double(measuredInks) + EPSILON) || !(overLimit >= 0.0 && overLimit <= mediaArea * (1.0 + EPSILON)) ||
            !(richBlack >= 0.0 && richBlack <= mediaArea * (1.0 + EPSILON)))
        {
            *failures << QStringLiteral("The coverage %1 % of %2 inks, %3 mm² over the limit, %4 mm² of rich black of %5 mm²").arg(maximumCoverage).arg(measuredInks).arg(overLimit).arg(richBlack).arg(mediaArea);
        }
    }

    /// Ink Coverage and Output Preview need a page
    static bool hasNoPage(const pdf::PDFDocument* input)
    {
        return input->getCatalog()->getPageCount() == 0;
    }

    /// Document Report (plan of step 7 of phase 2, 4.4, P1b): every section of a hostile file is read (one
    /// visited set and a depth for every walk, the limited decoding), the pages of the page boxes are each
    /// page once, the fonts are on pages of the document, the HTML is well-formed XHTML
    static void checkReport(const pdf::PDFDocument* input, const pdf::PDFDocumentToolResult& result, const QVariantMap& parameters, QStringList* failures)
    {
        Q_UNUSED(parameters);
        const qint64 pageCount = qint64(input->getCatalog()->getPageCount());
        const QJsonObject sections = result.data.value(QString::fromLatin1(pdf::PDFDocumentToolReport::DATA_SECTIONS)).toObject();
        if (sections.size() != int(pdf::PDFDocumentReport::getSections().size()))
        {
            *failures << QStringLiteral("%1 sections of %2").arg(sections.size()).arg(pdf::PDFDocumentReport::getSections().size());
        }
        if (sections.value(QStringLiteral("basicInformation")).toObject().value(QStringLiteral("pageCount")).toInteger() != pageCount)
        {
            *failures << QStringLiteral("The basic information has another page count than %1").arg(pageCount);
        }

        qint64 boxedPages = 0;
        for (const QJsonValue& group : sections.value(QStringLiteral("pageBoxes")).toObject().value(QStringLiteral("groups")).toArray())
        {
            const QString pages = group.toObject().value(QStringLiteral("pages")).toString();
            for (const QString& part : pages.split(QStringLiteral(", "), Qt::SkipEmptyParts))
            {
                const QStringList range = part.split(QLatin1Char('-'));
                boxedPages += range.size() == 2 ? range[1].toLongLong() - range[0].toLongLong() + 1 : 1;
            }
        }
        if (boxedPages != pageCount)
        {
            *failures << QStringLiteral("The page boxes have %1 pages of %2").arg(boxedPages).arg(pageCount);
        }

        if (result.outputs.size() != 1)
        {
            *failures << QStringLiteral("%1 files of a report").arg(result.outputs.size());
            return;
        }
        QXmlStreamReader reader(result.outputs.front().data);
        while (!reader.atEnd())
        {
            reader.readNext();
        }
        if (reader.hasError())
        {
            *failures << QStringLiteral("The HTML is not well-formed: %1").arg(reader.errorString());
        }
    }

    /// Structure as XML (plan of step 7 of phase 2, 4.4): streamed into its file, well-formed with every
    /// object of the storage as one element
    static void checkStructureXml(const pdf::PDFDocument* input, const pdf::PDFDocumentToolResult& result, const QVariantMap& parameters, QStringList* failures)
    {
        Q_UNUSED(parameters);
        if (result.outputs.size() != 1)
        {
            *failures << QStringLiteral("%1 files of the XML").arg(result.outputs.size());
            return;
        }

        QFile file(result.outputs.front().getFilePath());
        if (!file.open(QFile::ReadOnly))
        {
            *failures << QStringLiteral("The XML file cannot be read");
            return;
        }

        qint64 objects = 0;
        for (const pdf::PDFObjectStorage::Entry& entry : input->getStorage().getObjects())
        {
            objects += entry.object.isNull() ? 0 : 1;
        }

        qint64 elements = 0;
        QXmlStreamReader reader(&file);
        while (!reader.atEnd())
        {
            reader.readNext();
            elements += reader.isStartElement() && reader.name() == QLatin1String("pdfobject") ? 1 : 0;
        }
        if (reader.hasError())
        {
            *failures << QStringLiteral("The XML is not well-formed: %1").arg(reader.errorString());
        }
        if (elements != objects)
        {
            *failures << QStringLiteral("%1 objects in the XML of %2").arg(elements).arg(objects);
        }
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

    /// The document has attachments of the document: /AF of the catalog or the name tree
    /// /EmbeddedFiles (ISO 32000-2, 7.7.2 Table 29 AF, 7.7.4 Table 32 EmbeddedFiles, 7.11.4)
    static bool hasDocumentAttachments(const pdf::PDFDocument* document)
    {
        const pdf::PDFDictionary* catalog = document->getDictionaryFromObject(document->getTrailerDictionary()->get("Root"));
        if (!catalog)
        {
            return false;
        }
        const pdf::PDFDictionary* names = document->getDictionaryFromObject(catalog->get("Names"));
        return catalog->hasKey("AF") || (names && names->hasKey("EmbeddedFiles"));
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

    /// The pages of Manage Pages "odd, even" in the document itself: the odd pages, then the even ones
    static std::vector<CorpusPageOrigin> mapOddThenEven(const pdf::PDFDocument* input)
    {
        std::vector<CorpusPageOrigin> origins;
        for (size_t first : { size_t(0), size_t(1) })
        {
            for (size_t i = first; i < input->getCatalog()->getPageCount(); i += 2)
            {
                origins.push_back({ i, pdf::PageRotation::None });
            }
        }
        return origins;
    }

    /// The pages of Manage Pages "odd, even, 1": as mapOddThenEven, a clone of the first page at the end
    static std::vector<CorpusPageOrigin> mapOddThenEvenAndFirst(const pdf::PDFDocument* input)
    {
        std::vector<CorpusPageOrigin> origins = mapOddThenEven(input);
        if (input->getCatalog()->getPageCount() > 0)
        {
            origins.push_back({ 0, pdf::PageRotation::None });
        }
        return origins;
    }

    /// Manage Pages refuses to change a document without pages and a dynamic XFA form (plan of step
    /// 3b of phase 2, 4.5: its pages are made by the XFA form)
    static bool isArrangeRefused(const pdf::PDFDocument* input)
    {
        return input->getCatalog()->getPageCount() == 0 || input->getCatalog()->isXFANeedsRendering();
    }

    /// The pages of Merge of a file with itself: every page twice, in the order of the file
    static std::vector<CorpusPageOrigin> mapPagesTwice(const pdf::PDFDocument* input)
    {
        std::vector<CorpusPageOrigin> origins;
        for (size_t copy = 0; copy < 2; ++copy)
        {
            for (size_t i = 0; i < input->getCatalog()->getPageCount(); ++i)
            {
                origins.push_back({ i, pdf::PageRotation::None });
            }
        }
        return origins;
    }

    /// Merge refuses a file without pages, one, which restricts more than the assembly without
    /// its owner password (owner answer Q2 of step 2 of phase 2), and a dynamic XFA form (its
    /// pages are made by the XFA form, the page tree has none to merge: the merger refuses it)
    static bool isMergeRefused(const pdf::PDFDocument* input)
    {
        return input->getCatalog()->getPageCount() == 0 || pdf::PDFDocumentMerger::getPermission(input) != pdf::PDFDocumentMerger::Permission::Permitted ||
               input->getCatalog()->isXFANeedsRendering();
    }

    // ---------------------------------------------------------------- step 6 of phase 2

    /// The certificate of the rows of Sign with Certificate: one key of the test for the whole run,
    /// made at run time (never a file, never a certificate of the machine)
    static const pdf::PDFCertificateEntry& getSigningCertificate()
    {
        static const pdf::PDFCertificateEntry entry = []()
        {
            pdf::PDFCertificateManager::NewCertificateInfo info;
            info.privateKeyPasword = getSigningPassword();
            info.certCommonName = QStringLiteral("PDF4QT corpus signer");
            info.rsaKeyLength = 2048;
            pdf::PDFCertificateEntry result;
            QString error;
            result.pkcs12 = pdf::PDFSignatureFactory::createCertificate(info, nullptr, &error);
            if (const std::optional<pdf::PDFCertificateInfo> certificate = pdf::PDFCertificateManager::readCertificate(result.pkcs12, info.privateKeyPasword))
            {
                result.info = *certificate;
            }
            return result;
        }();
        return entry;
    }

    static QString getSigningPassword()
    {
        return QStringLiteral("corpus-signer");
    }

    static void addSigningCertificate(pdf::PDFDocumentToolContext* context)
    {
        context->userCertificates = { getSigningCertificate() };
    }

    static QVariantMap createSignParameters(const pdf::PDFDocument* input, const QString& path)
    {
        Q_UNUSED(input);
        Q_UNUSED(path);
        using Signature = pdf::PDFDocumentToolSignature;
        return { { QString::fromLatin1(Signature::PARAMETER_CERTIFICATE), pdf::PDFDocumentToolEncryption::getCertificateFingerprint(getSigningCertificate().info.getCertificateData()) },
                 { QString::fromLatin1(Signature::PARAMETER_CERTIFICATE_PASSWORD), getSigningPassword() },
                 { QString::fromLatin1(Signature::PARAMETER_APPEARANCE), QString::fromLatin1(Signature::APPEARANCE_INVISIBLE) } };
    }

    /// The format Compatible (PKCS#7) of owner answer Q6.1 (adbe.pkcs7.detached)
    static QVariantMap createCompatibleSignParameters(const pdf::PDFDocument* input, const QString& path)
    {
        using Signature = pdf::PDFDocumentToolSignature;
        QVariantMap parameters = createSignParameters(input, path);
        parameters.insert(QString::fromLatin1(Signature::PARAMETER_FORMAT), QString::fromLatin1(Signature::FORMAT_PKCS7));
        return parameters;
    }

    /// B135, R6-4 (ISO 32000-2 12.8.2.2.1, Table 257 /P 1): no signature of a document certified with
    /// no changes allowed
    static bool isSignatureRefused(const pdf::PDFDocument* input)
    {
        return pdf::PDFDocMDPPermissions::getLevel(input) == pdf::PDFDocMDPPermissions::Level::NoChanges;
    }

    /// Plan 4.4 (434:8913), R6-12, ASan E1 of step 6: a signed file, whose objects the reader has
    /// restored of its bytes (attack files of signature wrapping, exchanged content: the cross-reference
    /// sections do not lead to the objects), has no correct incremental update - another reader would
    /// read the update over the broken sections - and a whole write destroys its signatures: Sign refuses it
    static bool isRestoredSignedFile(const pdf::PDFDocument* input, bool isRestored)
    {
        return isRestored && pdf::PDFDocumentSigner::hasExistingSignatures(input);
    }

    /// The signed file (plan 4.4, R6-27): the written bytes are the result, they have one signature
    /// more than the input and the new one verifies over them
    static void checkSignedResult(const pdf::PDFDocument* input, const pdf::PDFDocumentToolResult& result, const QVariantMap& parameters, QStringList* failures)
    {
        Q_UNUSED(parameters);
        const QByteArray& written = result.documents.front().writtenData;
        if (written.isEmpty())
        {
            failures->append(QStringLiteral("The signed result has no written file"));
            return;
        }

        const std::optional<pdf::PDFDocument> output = pdf::PDFDocumentSigner::readDocument(written, nullptr, nullptr);
        if (!output)
        {
            failures->append(QStringLiteral("The signed file cannot be read"));
            return;
        }

        if (pdf::PDFForm::getSignatureCount(&*output) != pdf::PDFForm::getSignatureCount(input) + 1)
        {
            failures->append(QStringLiteral("The signed file has %1 signatures, the input %2").arg(pdf::PDFForm::getSignatureCount(&*output)).arg(pdf::PDFForm::getSignatureCount(input)));
        }

        std::set<QString> inputNames;
        const pdf::PDFForm inputForm = pdf::PDFForm::parse(input, input->getCatalog()->getFormObject());
        inputForm.apply([&inputNames](const pdf::PDFFormField* field) { inputNames.insert(field->getName(pdf::PDFFormField::NameType::FullyQualified)); });

        const pdf::PDFForm form = pdf::PDFForm::parse(&*output, output->getCatalog()->getFormObject());
        pdf::PDFSignatureHandler::Parameters verification;
        verification.useSystemCertificateStore = false;
        verification.verifyCertificates = false;
        int newSignatures = 0;
        for (const pdf::PDFSignatureVerificationResult& signature : pdf::PDFSignatureHandler::verifySignatures(form, written, verification))
        {
            if (!inputNames.count(signature.getSignatureFieldQualifiedName()))
            {
                ++newSignatures;
                if (!signature.isSignatureValid() || signature.getLaterRevisions() != pdf::PDFSignatureVerificationResult::LaterRevisions::None)
                {
                    failures->append(QStringLiteral("The new signature does not verify: %1").arg(signature.getErrors().join(QLatin1Char(' '))));
                }
            }
        }
        if (newSignatures != 1)
        {
            failures->append(QStringLiteral("The signed file has %1 new signatures").arg(newSignatures));
        }
    }

    /// Redact Content (P3 of step 6): the area of the middle ninth of the visible box of every page
    /// (the marks of the file are not used: their count is the file's), so every kind of content of
    /// the corpus is redacted somewhere
    static QVariantMap createRedactParameters(const pdf::PDFDocument* input, const QString& path)
    {
        Q_UNUSED(path);
        using Redact = pdf::PDFDocumentToolRedact;
        QJsonArray areas;
        for (size_t pageIndex = 0; pageIndex < input->getCatalog()->getPageCount() && areas.size() < Redact::MAXIMUM_AREA_COUNT; ++pageIndex)
        {
            const QRectF box = pdf::PDFRedact::getVisibleBox(input->getCatalog()->getPage(pageIndex));
            if (box.isValid())
            {
                QJsonObject area;
                area.insert(QStringLiteral("page"), qint64(pageIndex + 1));
                area.insert(QStringLiteral("rect"), QJsonArray({ box.left() + box.width() / 3.0, box.top() + box.height() / 3.0, box.width() / 3.0, box.height() / 3.0 }));
                areas.append(area);
            }
        }
        return { { QString::fromLatin1(Redact::PARAMETER_USE_MARKS), false },
                 { QString::fromLatin1(Redact::PARAMETER_AREAS), QString::fromUtf8(QJsonDocument(areas).toJson(QJsonDocument::Compact)) } };
    }

    /// The rule of Merge (R6-9, B139: the copy is not encrypted) and a document without a page
    static bool isRedactionRefused(const pdf::PDFDocument* input)
    {
        return pdf::PDFDocumentMerger::getPermission(input) != pdf::PDFDocumentMerger::Permission::Permitted || input->getCatalog()->getPageCount() == 0;
    }

    /// The redacted copy (6.8): verified, the written bytes are a document of the pages of the input
    /// with no annotation; a finding of the check is a Failed result, which fails the row
    static void checkRedactedResult(const pdf::PDFDocument* input, const pdf::PDFDocumentToolResult& result, const QVariantMap& parameters, QStringList* failures)
    {
        Q_UNUSED(parameters);
        if (!result.data.value(QStringLiteral("verified")).toBool())
        {
            failures->append(QStringLiteral("The redacted copy is not verified"));
        }

        const QByteArray& written = result.documents.empty() ? QByteArray() : result.documents.front().writtenData;
        pdf::PDFDocumentReader reader(nullptr, nullptr, false, false);
        const pdf::PDFDocument output = reader.readFromBuffer(written);
        if (reader.getReadingResult() != pdf::PDFDocumentReader::Result::OK)
        {
            failures->append(QStringLiteral("The redacted copy cannot be read"));
            return;
        }
        if (output.getCatalog()->getPageCount() != input->getCatalog()->getPageCount())
        {
            failures->append(QStringLiteral("The redacted copy has %1 pages, the input %2").arg(output.getCatalog()->getPageCount()).arg(input->getCatalog()->getPageCount()));
        }
        for (size_t pageIndex = 0; pageIndex < output.getCatalog()->getPageCount(); ++pageIndex)
        {
            if (!output.getCatalog()->getPage(pageIndex)->getAnnotations().empty())
            {
                failures->append(QStringLiteral("Page %1 of the redacted copy has annotations").arg(pageIndex + 1));
                break;
            }
        }
    }

    // ---------------------------------------------------------------- step 5 of phase 2

    /// The tools of the encryption refuse a document, whose security only its owner may change
    /// (R5-4) and whose owner password the folder does not give, and a document with locked
    /// attachments (Q5.9 c)
    static bool isSecurityChangeRefused(const pdf::PDFDocument* input)
    {
        const pdf::PDFSecurityHandler* handler = input->getStorage().getSecurityHandler();
        return !pdf::PDFDocumentToolEncryption::isSecurityChangeAllowed(input) || (handler && handler->isEmbeddedFilesLocked());
    }

    /// The input of Remove Password: the file encrypted by the core first (AES 256-bit) and opened
    /// by its owner password; the bytes of the original file stay the original data, so the
    /// reported size before is the size of the file of the row. A file, which cannot be encrypted,
    /// stays as it was (the row then refuses it or has nothing to remove).
    static void encryptInput(pdf::PDFDocumentToolContext* context)
    {
        pdf::PDFDocumentToolInputDocument& input = context->documents.front();
        if (!pdf::PDFDocumentToolEncryption::isSecurityChangeAllowed(input.document.data()))
        {
            return;
        }

        pdf::PDFSecurityHandlerFactory::SecuritySettings settings;
        settings.algorithm = pdf::PDFSecurityHandlerFactory::AES_256;
        settings.userPassword = QStringLiteral("corpus-open");
        settings.ownerPassword = QStringLiteral("corpus-owner");
        settings.permissions = pdf::PDFDocumentToolEncryption::getAllPermissions();
        const pdf::PDFDocumentToolEncryption::Change change = pdf::PDFDocumentToolEncryption::changeEncryption(input.document.data(), std::move(settings), nullptr, nullptr);
        if (!change.error.isEmpty())
        {
            // The run fails: "An input document is missing" names the row as failed
            input.document.reset();
            return;
        }

        bool isFirstAttempt = true;
        auto getPassword = [&isFirstAttempt](bool* ok)
        {
            *ok = isFirstAttempt;
            isFirstAttempt = false;
            return QStringLiteral("corpus-owner");
        };
        pdf::PDFDocumentReader reader(nullptr, getPassword, false, true);
        pdf::PDFDocument encrypted = reader.readFromBuffer(change.writtenData);
        input.document = reader.getReadingResult() == pdf::PDFDocumentReader::Result::OK ? pdf::PDFDocumentPointer(new pdf::PDFDocument(std::move(encrypted))) : pdf::PDFDocumentPointer();
    }

    // ---------------------------------------------------------------- step 4 of phase 2

    /// Black & White of the pages and PDF to Images refuse a document without pages
    static bool hasNoPages(const pdf::PDFDocument* input)
    {
        return input->getCatalog()->getPageCount() == 0;
    }

    /// A page, whose image at \p dpi would exceed the limits of the pixels of a page image (B65,
    /// R4-19, plan 4.4: 32 767 pixels per side, 2 GiB of 32 bit pixels), measured by its media box
    /// in inches of the paper (/UserUnit, ISO 32000-2, 14.11.2). The exports refuse such a page by
    /// their validation ("Page 1 would be 124,731 px wide – lower the resolution"), never shrink it.
    static bool hasPageTooLarge(const pdf::PDFDocument* input, double dpi)
    {
        constexpr double maximumSide = 32767.0;
        constexpr double maximumBytes = 2.0 * 1024.0 * 1024.0 * 1024.0;
        const pdf::PDFCatalog* catalog = input->getCatalog();
        for (size_t i = 0; i < catalog->getPageCount(); ++i)
        {
            const pdf::PDFPage* page = catalog->getPage(i);
            const QSizeF pixels = page->getMediaBox().normalized().size() * (page->getEffectiveUserUnit() * dpi / 72.0);
            if (pixels.width() > maximumSide || pixels.height() > maximumSide || std::ceil(pixels.width()) * std::ceil(pixels.height()) * 4.0 > maximumBytes)
            {
                return true;
            }
        }
        return false;
    }

    /// PDF to Images at 72 dpi refuses a document without pages and a page too large
    static bool isPageImagesRefused(const pdf::PDFDocument* input)
    {
        return hasNoPages(input) || hasPageTooLarge(input, 72.0);
    }

    /// Black & White of the pages at 100 dpi refuses a document without pages and a page too large
    static bool isBitonalPagesRefused(const pdf::PDFDocument* input)
    {
        return hasNoPages(input) || hasPageTooLarge(input, 100.0);
    }

    /// Create Audio Book refuses a document, whose pages have no text ("This document has no text
    /// to read"); the reference text of Extract Text is the independent reading
    static bool hasNoText(const pdf::PDFDocument* input)
    {
        for (size_t i = 0; i < input->getCatalog()->getPageCount(); ++i)
        {
            const CorpusPageBudget budget(getPageTimeLimit());
            if (!getExtractReferenceText(input, i, &budget).trimmed().isEmpty())
            {
                return false;
            }
        }
        return true;
    }

    /// The one document of a result written and read back with the empty password (what the
    /// user gets is the file; the input was opened with it): std::nullopt and a failure otherwise
    static std::optional<pdf::PDFDocument> writeAndRead(const pdf::PDFDocumentToolResult& result, QStringList* failures)
    {
        if (result.documents.size() != 1 || !result.documents.front().document)
        {
            *failures << QStringLiteral("%1 documents instead of one").arg(result.documents.size());
            return std::nullopt;
        }

        QByteArray written;
        QBuffer buffer(&written);
        buffer.open(QIODevice::WriteOnly);
        const pdf::PDFOperationResult writeResult = pdf::PDFDocumentWriter(nullptr).write(&buffer, result.documents.front().document.data());
        if (!writeResult)
        {
            *failures << QStringLiteral("The result cannot be written: %1").arg(writeResult.getErrorMessage());
            return std::nullopt;
        }

        const auto emptyPassword = [](bool* ok)
        {
            *ok = true;
            return QString();
        };
        pdf::PDFDocumentReader reader(nullptr, emptyPassword, true, false);
        pdf::PDFDocument document = reader.readFromBuffer(written);
        if (reader.getReadingResult() != pdf::PDFDocumentReader::Result::OK)
        {
            *failures << QStringLiteral("The written result cannot be read: %1").arg(reader.getErrorMessage());
            return std::nullopt;
        }
        return document;
    }

    /// The statuses of the images of the result of Optimize Images / Grayscale by their ids
    static std::map<QString, QString> getImageStatuses(const pdf::PDFDocumentToolResult& result)
    {
        std::map<QString, QString> statuses;
        for (const QJsonValue& value : result.data.value(QStringLiteral("images")).toArray())
        {
            const QJsonObject image = value.toObject();
            statuses[image.value(QStringLiteral("id")).toString()] = image.value(QStringLiteral("status")).toString();
        }
        return statuses;
    }

    /// Convert Images to Grayscale (plan of step 4, 4.3, R4-4, R4-5, R4-8, Q4.15 a): every image of
    /// every layer has its status; an inline image is kept, a gray or bitonal one by its declared
    /// color space is skipped, a color one is converted unless it cannot be read or is too large;
    /// the written result has the pages of the input, and every converted image is gray by its
    /// declaration. NothingToDo converts nothing.
    static void checkGrayscale(const pdf::PDFDocument* input, const pdf::PDFDocumentToolResult& result, const QVariantMap& parameters, QStringList* failures)
    {
        Q_UNUSED(parameters);
        using Tool = pdf::PDFDocumentToolOptimizeImages;
        using List = pdf::PDFDocumentImageList;

        const std::map<QString, QString> statuses = getImageStatuses(result);
        const List before = List::create(input, List::Options::createForChange(), nullptr, nullptr);
        int converted = 0;
        for (const List::Item& item : before.getItems())
        {
            const QString id = item.key.toString();
            const auto it = statuses.find(id);
            if (it == statuses.cend())
            {
                *failures << QStringLiteral("The image %1 has no status").arg(id);
                continue;
            }

            const QString& status = it->second;
            QString expected;
            if (item.kind == List::Kind::Inline)
            {
                expected = QString::fromLatin1(Tool::IMAGE_STATUS_INLINE);
            }
            else if (!item.isColorImage())
            {
                expected = QString::fromLatin1(Tool::IMAGE_STATUS_SKIPPED);
            }
            else if (item.isInAppearance)
            {
                // R4-4: an appearance of an annotation draws it too - never changed (checked
                // byte by byte in the written result below)
                expected = QString::fromLatin1(Tool::IMAGE_STATUS_KEPT);
            }
            if (!expected.isEmpty() ? status != expected
                                    : status != QLatin1String(Tool::IMAGE_STATUS_RECOMPRESSED) && status != QLatin1String(Tool::IMAGE_STATUS_TOO_LARGE) &&
                                      status != QLatin1String(Tool::IMAGE_STATUS_CANNOT_BE_READ))
            {
                *failures << QStringLiteral("The image %1 (%2) is %3").arg(id, item.colorSpaceName, status);
            }
            converted += status == QLatin1String(Tool::IMAGE_STATUS_RECOMPRESSED) ? 1 : 0;
        }

        if (result.status == pdf::PDFDocumentToolResultStatus::NothingToDo)
        {
            if (converted > 0)
            {
                *failures << QStringLiteral("NothingToDo, but %1 images are converted").arg(converted);
            }
            return;
        }

        const std::optional<pdf::PDFDocument> output = writeAndRead(result, failures);
        if (!output)
        {
            return;
        }
        if (output->getCatalog()->getPageCount() != input->getCatalog()->getPageCount())
        {
            *failures << QStringLiteral("%1 pages instead of %2").arg(output->getCatalog()->getPageCount()).arg(input->getCatalog()->getPageCount());
        }
        const List after = List::create(&*output, List::Options::createForChange(), nullptr, nullptr);
        for (const List::Item& item : after.getItems())
        {
            const auto it = statuses.find(item.key.toString());
            if (it != statuses.cend() && it->second == QLatin1String(Tool::IMAGE_STATUS_RECOMPRESSED) && item.isColorImage())
            {
                *failures << QStringLiteral("The converted image %1 is declared %2").arg(item.key.toString(), item.colorSpaceName);
            }
        }

        // R4-4: an image of an appearance keeps its samples and its color space
        for (const List::Item& item : before.getItems())
        {
            if (!item.isInAppearance)
            {
                continue;
            }
            const pdf::PDFObject& original = input->getObjectByReference(item.key.reference);
            const pdf::PDFObject& written = output->getObjectByReference(item.key.reference);
            if (!written.isStream() || input->getDecodedStream(original.getStream()) != output->getDecodedStream(written.getStream()) ||
                !(input->getObject(original.getStream()->getDictionary()->get("ColorSpace")) == output->getObject(written.getStream()->getDictionary()->get("ColorSpace"))))
            {
                *failures << QStringLiteral("The image %1 of an appearance has changed").arg(item.key.toString());
            }
        }
    }

    /// Convert to Black & White of the pages (plan of step 4, 4.6): a new version of the document
    /// with all its pages, which is written and read back
    static void checkBitonal(const pdf::PDFDocument* input, const pdf::PDFDocumentToolResult& result, const QVariantMap& parameters, QStringList* failures)
    {
        Q_UNUSED(parameters);
        if (result.status == pdf::PDFDocumentToolResultStatus::NothingToDo)
        {
            if (input->getCatalog()->getPageCount() > 0)
            {
                *failures << QStringLiteral("NothingToDo of %1 pages to convert").arg(input->getCatalog()->getPageCount());
            }
            return;
        }

        const std::optional<pdf::PDFDocument> output = writeAndRead(result, failures);
        if (output && output->getCatalog()->getPageCount() != input->getCatalog()->getPageCount())
        {
            *failures << QStringLiteral("%1 pages instead of %2").arg(output->getCatalog()->getPageCount()).arg(input->getCatalog()->getPageCount());
        }
    }

    /// PDF to Images (plan of step 4, 4.4, B57): a PNG file per page in the order of the pages,
    /// each one of the size of the rotated crop box at the resolution (getPageGeometry, ISO 32000-2
    /// 14.11.2, 8.3.2.3 UserUnit)
    static void checkPageImages(const pdf::PDFDocument* input, const pdf::PDFDocumentToolResult& result, const QVariantMap& parameters, const CorpusMemorySink& sink, QStringList* failures)
    {
        const size_t pageCount = input->getCatalog()->getPageCount();
        if (result.status == pdf::PDFDocumentToolResultStatus::NothingToDo)
        {
            *failures << QStringLiteral("NothingToDo of %1 pages").arg(pageCount);
            return;
        }
        if (sink.files.size() != pageCount || size_t(result.writtenFiles.size()) != pageCount)
        {
            *failures << QStringLiteral("%1 files (%2 written) of %3 pages").arg(sink.files.size()).arg(result.writtenFiles.size()).arg(pageCount);
            return;
        }

        for (size_t i = 0; i < pageCount; ++i)
        {
            const QByteArray& data = sink.files[i].second;
            const pdf::PDFDocumentToolPdfToImages::PageGeometry geometry = pdf::PDFDocumentToolPdfToImages::getPageGeometry(input->getCatalog()->getPage(i), parameters);
            const QImage image = QImage::fromData(data, "PNG");
            if (!data.startsWith(QByteArray("\x89PNG\r\n\x1a\n", 8)) || image.isNull())
            {
                *failures << QStringLiteral("The file %1 is no PNG image").arg(sink.files[i].first);
            }
            else if (image.size() != geometry.imageSize)
            {
                *failures << QStringLiteral("The file %1 is %2 x %3, the page %4 x %5").arg(sink.files[i].first).arg(image.width()).arg(image.height()).arg(geometry.imageSize.width()).arg(geometry.imageSize.height());
            }
        }
    }

    /// Extract Images (plan of step 4, 4.5, R4-11, B60): NothingToDo only without an image of the
    /// export list; else at most one file per item of the list (the same images are one file), each
    /// a PNG image, which decodes
    static void checkExtractedImages(const pdf::PDFDocument* input, const pdf::PDFDocumentToolResult& result, const QVariantMap& parameters, const CorpusMemorySink& sink, QStringList* failures)
    {
        Q_UNUSED(parameters);
        using List = pdf::PDFDocumentImageList;
        const List list = List::create(input, List::Options::createForExport(), nullptr, nullptr);
        if (result.status == pdf::PDFDocumentToolResultStatus::NothingToDo)
        {
            if (!sink.files.empty())
            {
                *failures << QStringLiteral("NothingToDo with %1 files").arg(sink.files.size());
            }
            return;
        }
        // An image, which cannot be written (larger than the format holds, R4-19; damaged, or a
        // feature the decoder does not have), is a failed file with its reason (339:7293, K4.11) -
        // the export goes on with the others
        if (sink.files.size() + result.failedFiles.size() == 0 || sink.files.size() + result.failedFiles.size() > list.getItems().size() ||
            size_t(result.writtenFiles.size()) != sink.files.size())
        {
            *failures << QStringLiteral("%1 files (%2 written, %3 failed) of %4 images").arg(sink.files.size()).arg(result.writtenFiles.size()).arg(result.failedFiles.size()).arg(list.getItems().size());
        }
        for (const pdf::PDFDocumentToolFileProblem& failed : result.failedFiles)
        {
            if (failed.fileName.isEmpty() || failed.reason.isEmpty())
            {
                *failures << QStringLiteral("A failed file without its name or reason: %1 %2").arg(failed.fileName, failed.reason);
            }
        }
        for (const auto& [name, data] : sink.files)
        {
            if (!data.startsWith(QByteArray("\x89PNG\r\n\x1a\n", 8)) || QImage::fromData(data, "PNG").isNull())
            {
                *failures << QStringLiteral("The file %1 is no PNG image").arg(name);
            }
        }
    }

    /// Reads a little-endian number of the WAV header
    static quint32 readLittleEndian(const QByteArray& data, int offset, int bytes)
    {
        quint32 value = 0;
        for (int i = bytes - 1; i >= 0; --i)
        {
            value = (value << 8) | quint8(data.at(offset + i));
        }
        return value;
    }

    /// Create Audio Book with the fake speech (plan of step 4, 4.7, Q4.2: WAV only, R4-1): one
    /// output in a temporary file - a RIFF / WAVE file of the canonical 44 bytes of the header, PCM
    /// (format 1) mono 16 bit at 16 kHz of the fake, the sizes of RIFF and of data agree with the
    /// file, at least one sample of the text
    static void checkAudioBook(const pdf::PDFDocument* input, const pdf::PDFDocumentToolResult& result, const QVariantMap& parameters, QStringList* failures)
    {
        Q_UNUSED(input);
        Q_UNUSED(parameters);
        if (result.status != pdf::PDFDocumentToolResultStatus::Success)
        {
            *failures << QStringLiteral("NothingToDo of Create Audio Book");
            return;
        }
        if (result.outputs.size() != 1 || !result.outputs.front().isTemporaryFile())
        {
            *failures << QStringLiteral("%1 outputs, the first one not in a temporary file").arg(result.outputs.size());
            return;
        }

        // Only the header is read: the file of a long document is large (5 648 pages of the OMAP
        // manual give more than 512 MiB, mostly the silence of 300 ms between the items), its size
        // comes from the file system
        const pdf::PDFDocumentToolOutputFile& output = result.outputs.front();
        QString errorMessage;
        const QByteArray data = output.readContent(44, nullptr, &errorMessage);
        const qint64 fileSize = output.getSize();
        if (data.size() < 44 || fileSize < 46)
        {
            *failures << QStringLiteral("The audio file has %1 bytes: %2").arg(fileSize).arg(errorMessage);
            return;
        }
        if (!data.startsWith("RIFF") || data.mid(8, 8) != QByteArray("WAVEfmt ") || data.mid(36, 4) != QByteArray("data"))
        {
            *failures << QStringLiteral("No canonical WAV header");
            return;
        }
        const quint32 riffSize = readLittleEndian(data, 4, 4);
        const quint32 formatTag = readLittleEndian(data, 20, 2);
        const quint32 channels = readLittleEndian(data, 22, 2);
        const quint32 sampleRate = readLittleEndian(data, 24, 4);
        const quint32 bitsPerSample = readLittleEndian(data, 34, 2);
        const quint32 dataSize = readLittleEndian(data, 40, 4);
        if (qint64(riffSize) != fileSize - 8 || qint64(dataSize) != fileSize - 44 || formatTag != 1 || channels != 1 || sampleRate != 16000 || bitsPerSample != 16)
        {
            *failures << QStringLiteral("WAV header: RIFF %1 of %2 bytes, data %3, format %4, %5 channels, %6 Hz, %7 bits").arg(riffSize).arg(fileSize).arg(dataSize).arg(formatTag).arg(channels).arg(sampleRate).arg(bitsPerSample);
        }
        if (output.suggestedFileName.isEmpty() || !output.suggestedFileName.endsWith(QStringLiteral(".wav")))
        {
            *failures << QStringLiteral("The suggested name '%1' is no WAV file").arg(output.suggestedFileName);
        }
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
        // Rotate, Remove and Manage Pages in the document itself change its pages: a certified
        // input loses the certification (Q12 of step 3, RB5 2 of step 3b)
        constexpr int PAGES_PROMISES = PromiseVersion | PromiseEncryption | PromiseText | PromiseTransparency | PromiseCertification;
        rows.push_back({ pdf::PDFDocumentToolRotatePages::TOOL_ID, "odd-right", createRotate, pages("", { QStringLiteral("odd") }, "right"), CorpusLook::Exact,
                         PAGES_PROMISES | PromiseSignatures | PromiseContent, &ToolCorpusHelper::mapOddPagesTurnedRight });
        rows.push_back({ pdf::PDFDocumentToolRotatePages::TOOL_ID, "all-half", createRotate, pages("", {}, "half"), CorpusLook::Exact,
                         PAGES_PROMISES | PromiseSignatures | PromiseContent, &ToolCorpusHelper::mapAllPagesTurnedHalf });
        rows.push_back({ pdf::PDFDocumentToolRemovePages::TOOL_ID, "even", createRemove, pages("", { QStringLiteral("even") }, nullptr), CorpusLook::Exact,
                         PAGES_PROMISES, &ToolCorpusHelper::mapOddPagesStay, nullptr, &ToolCorpusHelper::hasOnePage });

        // Merge of a file with itself (plan of step 2 of phase 2, chapter 8): every name collides
        // (fields, destinations, layers, attachments, the structure); each page looks as in the
        // file and keeps its text. A signature loses its appearance (owner answer Q4): the look of
        // a signed file is not promised, its row says it by the attribute "signed".
        const auto createMerge = []() -> std::shared_ptr<const pdf::PDFDocumentTool>
        {
            return std::make_shared<pdf::PDFDocumentToolMerge>();
        };
        const auto outline = [](const char* mode)
        {
            QVariantMap parameters;
            parameters.insert(QString::fromLatin1(pdf::PDFDocumentToolMerge::PARAMETER_OUTLINE), QString::fromLatin1(mode));
            return parameters;
        };
        CorpusToolRow mergeParts{ pdf::PDFDocumentToolMerge::TOOL_ID, "self-document-parts", createMerge, outline(pdf::PDFDocumentToolMerge::OUTLINE_DOCUMENT_PARTS), CorpusLook::Exact,
                                  PromiseText | PromiseTransparency, &ToolCorpusHelper::mapPagesTwice, nullptr, &ToolCorpusHelper::isMergeRefused };
        mergeParts.inputCount = 2;
        rows.push_back(mergeParts);
        CorpusToolRow mergeJoin{ pdf::PDFDocumentToolMerge::TOOL_ID, "self-join", createMerge, outline(pdf::PDFDocumentToolMerge::OUTLINE_JOIN), CorpusLook::Exact,
                                 PromiseText | PromiseTransparency, &ToolCorpusHelper::mapPagesTwice, nullptr, &ToolCorpusHelper::isMergeRefused };
        mergeJoin.inputCount = 2;
        rows.push_back(mergeJoin);

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

        // Document Statistics and Object Inspector (step 7 of phase 2, P1a): analyses, no document
        const auto createStatistics = []() -> std::shared_ptr<const pdf::PDFDocumentTool>
        {
            return std::make_shared<pdf::PDFDocumentToolStatistics>();
        };
        QVariantMap statisticsCsv;
        statisticsCsv.insert(QString::fromLatin1(pdf::PDFDocumentToolStatistics::PARAMETER_FORMAT), QString::fromLatin1(pdf::PDFDocumentToolTableWriter::FORMAT_CSV));
        rows.push_back({ pdf::PDFDocumentToolStatistics::TOOL_ID, "csv", createStatistics, statisticsCsv, CorpusLook::Exact, PromiseNone, nullptr, &ToolCorpusHelper::checkStatistics });
        const auto createInspector = []() -> std::shared_ptr<const pdf::PDFDocumentTool>
        {
            return std::make_shared<pdf::PDFDocumentToolObjectInspector>();
        };
        QVariantMap inspectorTrailer;
        inspectorTrailer.insert(QString::fromLatin1(pdf::PDFDocumentToolObjectInspector::PARAMETER_OBJECT), QStringLiteral("trailer"));
        inspectorTrailer.insert(QString::fromLatin1(pdf::PDFDocumentToolObjectInspector::PARAMETER_DEPTH), pdf::PDFDocumentToolObjectInspector::MAXIMUM_DEPTH);
        rows.push_back({ pdf::PDFDocumentToolObjectInspector::TOOL_ID, "trailer-views", createInspector, inspectorTrailer, CorpusLook::Exact, PromiseNone, nullptr, &ToolCorpusHelper::checkObjectInspector });

        // Ink Coverage and Output Preview (step 7 of phase 2, P3a): analyses, no document; a low resolution
        const auto createInkCoverage = []() -> std::shared_ptr<const pdf::PDFDocumentTool>
        {
            return std::make_shared<pdf::PDFDocumentToolInkCoverage>();
        };
        QVariantMap inkCoverage;
        inkCoverage.insert(QString::fromLatin1(pdf::PDFDocumentToolInkCoverage::PARAMETER_RESOLUTION), 150);
        rows.push_back({ pdf::PDFDocumentToolInkCoverage::TOOL_ID, "all-pages-150", createInkCoverage, inkCoverage, CorpusLook::Exact, PromiseNone, nullptr, &ToolCorpusHelper::checkInkCoverage, &ToolCorpusHelper::hasNoPage });
        const auto createOutputPreview = []() -> std::shared_ptr<const pdf::PDFDocumentTool>
        {
            return std::make_shared<pdf::PDFDocumentToolOutputPreview>();
        };
        QVariantMap outputPreview;
        outputPreview.insert(QString::fromLatin1(pdf::PDFDocumentToolOutputPreview::PARAMETER_RESOLUTION), 300);
        rows.push_back({ pdf::PDFDocumentToolOutputPreview::TOOL_ID, "page-1-300", createOutputPreview, outputPreview, CorpusLook::Exact, PromiseNone, nullptr, &ToolCorpusHelper::checkOutputPreview, &ToolCorpusHelper::hasNoPage });

        // Document Report (step 7 of phase 2, P1b): an analysis, no document; every section, the structure as XML
        const auto createReport = []() -> std::shared_ptr<const pdf::PDFDocumentTool>
        {
            return std::make_shared<pdf::PDFDocumentToolReport>();
        };
        QVariantMap reportAll;
        for (const pdf::PDFDocumentReportSection section : pdf::PDFDocumentReport::getSections())
        {
            reportAll.insert(pdf::PDFDocumentReport::getSectionId(section), true);
        }
        reportAll.insert(QString::fromLatin1(pdf::PDFDocumentToolReport::PARAMETER_FORMAT), QString::fromLatin1(pdf::PDFDocumentToolTableWriter::FORMAT_HTML));
        rows.push_back({ pdf::PDFDocumentToolReport::TOOL_ID, "all-sections-html", createReport, reportAll, CorpusLook::Exact, PromiseNone, nullptr, &ToolCorpusHelper::checkReport, &ToolCorpusHelper::hasNoPage });
        QVariantMap structureXml;
        structureXml.insert(QString::fromLatin1(pdf::PDFDocumentToolReport::PARAMETER_KIND), QString::fromLatin1(pdf::PDFDocumentToolReport::KIND_STRUCTURE_XML));
        rows.push_back({ pdf::PDFDocumentToolReport::TOOL_ID, "structure-xml", createReport, structureXml, CorpusLook::Exact, PromiseNone, nullptr, &ToolCorpusHelper::checkStructureXml });

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

        // The encryption (step 5 of phase 2, plan 9): the result is written whole and read back by
        // its open password, its pages look as in the file and keep their text; the version may
        // rise (B98), the signatures are lost (LostCopyOnly). Only the owner changes the security
        // (R5-4): an encrypted file opened without its owner password is refused.
        using Encryption = pdf::PDFDocumentToolEncryption;
        const auto createEncryption = [](Encryption::Mode mode)
        {
            return [mode]() -> std::shared_ptr<const pdf::PDFDocumentTool> { return std::make_shared<Encryption>(mode); };
        };
        const auto protectPassword = [](const char* algorithm, const char* scope)
        {
            QVariantMap parameters;
            parameters.insert(QString::fromLatin1(Encryption::PARAMETER_ALGORITHM), QString::fromLatin1(algorithm));
            parameters.insert(QString::fromLatin1(Encryption::PARAMETER_SCOPE), QString::fromLatin1(scope));
            parameters.insert(QString::fromLatin1(Encryption::PARAMETER_OPEN_PASSWORD), QStringLiteral("corpus-open"));
            parameters.insert(QString::fromLatin1(Encryption::PARAMETER_OWNER_SAME_AS_OPEN), false);
            parameters.insert(QString::fromLatin1(Encryption::PARAMETER_OWNER_PASSWORD), QStringLiteral("corpus-owner"));
            parameters.insert(QString::fromLatin1(Encryption::PARAMETER_KEEP_RESTRICTIONS), false);
            return parameters;
        };
        constexpr int ENCRYPTION_PROMISES = PromiseText | PromiseTransparency;
        CorpusToolRow passwordAes256{ Encryption::TOOL_ID_PASSWORD, "aes-256", createEncryption(Encryption::Mode::Password), protectPassword(Encryption::ALGORITHM_AES_256, Encryption::SCOPE_EVERYTHING),
                                      CorpusLook::Exact, ENCRYPTION_PROMISES, nullptr, nullptr, &ToolCorpusHelper::isSecurityChangeRefused };
        passwordAes256.password = QStringLiteral("corpus-open");
        rows.push_back(passwordAes256);
        CorpusToolRow passwordAes128{ Encryption::TOOL_ID_PASSWORD, "aes-128-except-metadata", createEncryption(Encryption::Mode::Password), protectPassword(Encryption::ALGORITHM_AES_128, Encryption::SCOPE_EXCEPT_METADATA),
                                      CorpusLook::Exact, ENCRYPTION_PROMISES, nullptr, nullptr, &ToolCorpusHelper::isSecurityChangeRefused };
        passwordAes128.password = QStringLiteral("corpus-open");
        rows.push_back(passwordAes128);

        // Restrict Permissions without an open password: the file opens without a password
        QVariantMap viewAndPrint = Encryption::getPresetValues(Encryption::Preset::ViewAndPrint);
        viewAndPrint.insert(QString::fromLatin1(Encryption::PARAMETER_OWNER_PASSWORD), QStringLiteral("corpus-owner"));
        rows.push_back({ Encryption::TOOL_ID_PERMISSIONS, "view-and-print", createEncryption(Encryption::Mode::Permissions), viewAndPrint,
                         CorpusLook::Exact, ENCRYPTION_PROMISES, nullptr, nullptr, &ToolCorpusHelper::isSecurityChangeRefused });

        // Remove Password of the file encrypted first by the core (AES 256-bit, opened by its owner
        // password): the result is the file again, in the clear
        CorpusToolRow unlock{ Encryption::TOOL_ID_UNLOCK, "after-aes-256", createEncryption(Encryption::Mode::Unlock), QVariantMap(),
                              CorpusLook::Exact, ENCRYPTION_PROMISES, nullptr, nullptr, &ToolCorpusHelper::isSecurityChangeRefused };
        unlock.prepareContext = &ToolCorpusHelper::encryptInput;
        rows.push_back(unlock);

        // Sign with Certificate (step 6 of phase 2, plan 4.4, 9): an invisible signature is an
        // incremental update of the exact bytes of the file - the pages look as in the file, keep
        // their text, the version and the encryption; the new signature verifies, the earlier ones
        // are never worse (the tool fails otherwise). A certification with no changes allowed refuses.
        using Signature = pdf::PDFDocumentToolSignature;
        const auto createSign = []() -> std::shared_ptr<const pdf::PDFDocumentTool>
        {
            return std::make_shared<Signature>(Signature::Mode::Sign);
        };
        CorpusToolRow signInvisible{ Signature::TOOL_ID_SIGN, "invisible", createSign, QVariantMap(), CorpusLook::Exact,
                                     PromiseVersion | PromiseEncryption | PromiseText | PromiseTransparency, nullptr, &ToolCorpusHelper::checkSignedResult, &ToolCorpusHelper::isSignatureRefused };
        signInvisible.prepareContext = &ToolCorpusHelper::addSigningCertificate;
        signInvisible.createParameters = &ToolCorpusHelper::createSignParameters;
        signInvisible.readRefusal = &ToolCorpusHelper::isRestoredSignedFile;
        rows.push_back(signInvisible);
        CorpusToolRow signCompatible = signInvisible;
        signCompatible.variant = "invisible-pkcs7";
        signCompatible.createParameters = &ToolCorpusHelper::createCompatibleSignParameters;
        rows.push_back(signCompatible);

        // Split and Extract (plan of step 3 of phase 2, chapter 8): new unencrypted documents of
        // the pages, which look as in the file and keep their text; the forms, destinations,
        // structure, annotations and attachments of the file are what the rows are run for. They
        // refuse what Merge refuses (Q4: Assemble and the owner password, a file without pages).
        const auto createSplit = []() -> std::shared_ptr<const pdf::PDFDocumentTool>
        {
            return std::make_shared<pdf::PDFDocumentToolSplit>();
        };
        const auto createExtractPages = []() -> std::shared_ptr<const pdf::PDFDocumentTool>
        {
            return std::make_shared<pdf::PDFDocumentToolExtract>();
        };
        QVariantMap everyPage;
        everyPage.insert(QString::fromLatin1(pdf::PDFDocumentToolSplit::PARAMETER_RULE), QString::fromLatin1(pdf::PDFDocumentToolSplit::RULE_EVERY_PAGE));
        const auto extractOdd = [&pages](const char* output)
        {
            QVariantMap parameters = pages("", { QStringLiteral("odd") }, nullptr);
            parameters.insert(QString::fromLatin1(pdf::PDFDocumentToolExtract::PARAMETER_OUTPUT), QString::fromLatin1(output));
            return parameters;
        };
        CorpusToolRow splitEveryPage{ pdf::PDFDocumentToolSplit::TOOL_ID, "every-page", createSplit, everyPage, CorpusLook::Exact,
                                      PromiseText | PromiseTransparency, nullptr, nullptr, &ToolCorpusHelper::isMergeRefused };
        splitEveryPage.isFilePerPage = true;
        rows.push_back(splitEveryPage);
        rows.push_back({ pdf::PDFDocumentToolExtract::TOOL_ID, "odd-one-file", createExtractPages, extractOdd(pdf::PDFDocumentToolExtract::OUTPUT_ONE_FILE), CorpusLook::Exact,
                         PromiseText | PromiseTransparency, &ToolCorpusHelper::mapOddPagesStay, nullptr, &ToolCorpusHelper::isMergeRefused });
        CorpusToolRow extractFilePerPage{ pdf::PDFDocumentToolExtract::TOOL_ID, "odd-file-per-page", createExtractPages, extractOdd(pdf::PDFDocumentToolExtract::OUTPUT_FILE_PER_PAGE), CorpusLook::Exact,
                                          PromiseText | PromiseTransparency, &ToolCorpusHelper::mapOddPagesStay, nullptr, &ToolCorpusHelper::isMergeRefused };
        extractFilePerPage.isFilePerPage = true;
        rows.push_back(extractFilePerPage);

        // Manage Pages over the document itself (plan of step 3b of phase 2, 4.5, 8, RB1, RB4): the
        // pages in another order keep their look, text, contents, annotations and the signatures,
        // the encryption and the version of the file (an incremental update); a clone of the first
        // page at the end looks as the page, it has no widget of a signature field (RB1)
        const auto createManage = []() -> std::shared_ptr<const pdf::PDFDocumentTool>
        {
            return std::make_shared<pdf::PDFDocumentToolManage>();
        };
        const auto arrangedPages = [](const char* pageList)
        {
            QVariantMap parameters;
            parameters.insert(QString::fromLatin1(pdf::PDFDocumentToolManage::PARAMETER_PAGES), QString::fromLatin1(pageList));
            return parameters;
        };
        rows.push_back({ pdf::PDFDocumentToolManage::TOOL_ID, "odd-even", createManage, arrangedPages("odd, even"), CorpusLook::Exact,
                         PAGES_PROMISES | PromiseSignatures | PromiseContent, &ToolCorpusHelper::mapOddThenEven, nullptr, &ToolCorpusHelper::isArrangeRefused });
        rows.push_back({ pdf::PDFDocumentToolManage::TOOL_ID, "odd-even-clone-first", createManage, arrangedPages("odd, even, 1"), CorpusLook::Exact,
                         PAGES_PROMISES | PromiseSignatures, &ToolCorpusHelper::mapOddThenEvenAndFirst, nullptr, &ToolCorpusHelper::isArrangeRefused });

        // Manage Pages, the new documents of an assembly (plan of step 3b of phase 2, 4.4, 8): the
        // assembly of the file is its pages "odd, even" (createAssemblyOfPages, an item per page);
        // One PDF, a file per item (per group: an ungrouped page is its own file, 396:26763) and a
        // file per page are new unencrypted documents, whose pages look as in the file and keep
        // their text. They refuse what Merge refuses (Assemble and the owner password, a file
        // without pages, a dynamic XFA form).
        const auto assemblyOfFile = [](const char* output)
        {
            return [output](const pdf::PDFDocument* input, const QString& path)
            {
                QVariantMap parameters;
                QString error;
                const std::optional<pdf::PDFDocumentAssembly> assembly = pdf::PDFDocumentToolManage::createAssemblyOfPages(input, QFileInfo(path).fileName(), QStringLiteral("odd, even"), &error);
                if (assembly)
                {
                    parameters.insert(QString::fromLatin1(pdf::PDFDocumentToolManage::PARAMETER_ASSEMBLY), QVariant::fromValue(*assembly));
                }
                parameters.insert(QString::fromLatin1(pdf::PDFDocumentToolManage::PARAMETER_OUTPUT), QString::fromLatin1(output));
                return parameters;
            };
        };
        CorpusToolRow manageOnePdf{ pdf::PDFDocumentToolManage::TOOL_ID, "one-pdf-odd-even", createManage, QVariantMap(), CorpusLook::Exact,
                                    PromiseText | PromiseTransparency, &ToolCorpusHelper::mapOddThenEven, nullptr, &ToolCorpusHelper::isMergeRefused };
        manageOnePdf.createParameters = assemblyOfFile(pdf::PDFDocumentToolManage::OUTPUT_ONE_PDF);
        rows.push_back(manageOnePdf);
        CorpusToolRow managePerGroup{ pdf::PDFDocumentToolManage::TOOL_ID, "per-group-odd-even", createManage, QVariantMap(), CorpusLook::Exact,
                                      PromiseText | PromiseTransparency, &ToolCorpusHelper::mapOddThenEven, nullptr, &ToolCorpusHelper::isMergeRefused };
        managePerGroup.isFilePerPage = true;
        managePerGroup.createParameters = assemblyOfFile(pdf::PDFDocumentToolManage::OUTPUT_PER_GROUP);
        rows.push_back(managePerGroup);
        CorpusToolRow managePerPage{ pdf::PDFDocumentToolManage::TOOL_ID, "per-page-odd-even", createManage, QVariantMap(), CorpusLook::Exact,
                                     PromiseText | PromiseTransparency, &ToolCorpusHelper::mapOddThenEven, nullptr, &ToolCorpusHelper::isMergeRefused };
        managePerPage.isFilePerPage = true;
        managePerPage.createParameters = assemblyOfFile(pdf::PDFDocumentToolManage::OUTPUT_PER_PAGE);
        rows.push_back(managePerPage);

        // Step 4 of phase 2 (plan of step 4, chapter 8, row Corpus). Optimize Images of the defaults:
        // a new encoding of the images (lossy), the pages keep their text, the images their masks,
        // the version, the encryption and the signatures are kept (as Compress of the images only)
        const auto createOptimizeImages = []() -> std::shared_ptr<const pdf::PDFDocumentTool>
        {
            return std::make_shared<pdf::PDFDocumentToolOptimizeImages>();
        };
        rows.push_back({ pdf::PDFDocumentToolOptimizeImages::TOOL_ID, "auto", createOptimizeImages, QVariantMap(), CorpusLook::Close,
                         PromiseVersion | PromiseEncryption | PromiseSignatures | PromiseText | PromiseTransparency });

        // Convert Images to Grayscale and Convert to Black & White change the colors by their
        // purpose: their own checks (checkGrayscale, checkBitonal), no look of the pages
        const auto createGrayscale = []() -> std::shared_ptr<const pdf::PDFDocumentTool>
        {
            return std::make_shared<pdf::PDFDocumentToolGrayscale>();
        };
        rows.push_back({ pdf::PDFDocumentToolGrayscale::TOOL_ID, "defaults", createGrayscale, QVariantMap(), CorpusLook::Exact, PromiseNone, nullptr, &ToolCorpusHelper::checkGrayscale });

        using Bitonal = pdf::PDFDocumentToolBitonal;
        const auto createBitonal = []() -> std::shared_ptr<const pdf::PDFDocumentTool>
        {
            return std::make_shared<Bitonal>();
        };
        QVariantMap bitonalPages;
        bitonalPages.insert(QString::fromLatin1(Bitonal::PARAMETER_SOURCE), QString::fromLatin1(Bitonal::SOURCE_PAGES));
        bitonalPages.insert(QString::fromLatin1(Bitonal::PARAMETER_DPI), 100);
        bitonalPages.insert(QString::fromLatin1(Bitonal::PARAMETER_REPLACE_BLANK_PAGES), false);
        rows.push_back({ Bitonal::TOOL_ID, "pages-100-dpi", createBitonal, bitonalPages, CorpusLook::Exact, PromiseNone, nullptr, &ToolCorpusHelper::checkBitonal, &ToolCorpusHelper::isBitonalPagesRefused });

        // PDF to Images and Extract Images: PNG files by the file sink of the context (in the memory)
        const auto createPdfToImages = []() -> std::shared_ptr<const pdf::PDFDocumentTool>
        {
            return std::make_shared<pdf::PDFDocumentToolPdfToImages>();
        };
        QVariantMap pageImages;
        pageImages.insert(QStringLiteral("folder"), QDir::tempPath());
        pageImages.insert(QStringLiteral("existingFiles"), QStringLiteral("overwrite"));
        pageImages.insert(QString::fromLatin1(pdf::PDFDocumentToolPdfToImages::PARAMETER_FORMAT), QStringLiteral("png"));
        pageImages.insert(QString::fromLatin1(pdf::PDFDocumentToolPdfToImages::PARAMETER_DPI), 72);
        CorpusToolRow pdfToImages{ pdf::PDFDocumentToolPdfToImages::TOOL_ID, "png-72-dpi", createPdfToImages, pageImages, CorpusLook::Exact, PromiseNone, nullptr, nullptr, &ToolCorpusHelper::isPageImagesRefused };
        pdfToImages.filesCheck = &ToolCorpusHelper::checkPageImages;
        rows.push_back(pdfToImages);

        const auto createExtractImages = []() -> std::shared_ptr<const pdf::PDFDocumentTool>
        {
            return std::make_shared<pdf::PDFDocumentToolExtractImages>();
        };
        QVariantMap extractedImages;
        extractedImages.insert(QStringLiteral("folder"), QDir::tempPath());
        extractedImages.insert(QStringLiteral("existingFiles"), QStringLiteral("overwrite"));
        extractedImages.insert(QString::fromLatin1(pdf::PDFDocumentToolExtractImages::PARAMETER_FORMAT), QStringLiteral("png"));
        CorpusToolRow extractImages{ pdf::PDFDocumentToolExtractImages::TOOL_ID, "png", createExtractImages, extractedImages, CorpusLook::Exact, PromiseNone };
        extractImages.filesCheck = &ToolCorpusHelper::checkExtractedImages;
        rows.push_back(extractImages);

        // Create Audio Book with the fake speech (never the voices of the system): a WAV file
        const auto createAudioBook = []() -> std::shared_ptr<const pdf::PDFDocumentTool>
        {
            return std::make_shared<pdf::PDFDocumentToolAudioBook>();
        };
        QVariantMap audioBook;
        audioBook.insert(QString::fromLatin1(pdf::PDFDocumentToolAudioBook::PARAMETER_ORDER), QString::fromLatin1(pdf::PDFDocumentToolAudioBook::ORDER_LAYOUT));
        CorpusToolRow audio{ pdf::PDFDocumentToolAudioBook::TOOL_ID, "layout-fake-speech", createAudioBook, audioBook, CorpusLook::Exact, PromiseNone, nullptr,
                             &ToolCorpusHelper::checkAudioBook, &ToolCorpusHelper::hasNoText };
        audio.prepareContext = [](pdf::PDFDocumentToolContext* context)
        {
            context->speechSynthesizerFactory = std::make_shared<CorpusFakeSynthesizerFactory>();
            context->temporaryFolder = QDir::tempPath();
        };
        rows.push_back(audio);

        // Redact Content (P3 of step 6): a new verified document of the pages, its own result check
        const auto createRedact = []() -> std::shared_ptr<const pdf::PDFDocumentTool>
        {
            return std::make_shared<pdf::PDFDocumentToolRedact>();
        };
        CorpusToolRow redact{ pdf::PDFDocumentToolRedact::TOOL_ID, "middle-areas", createRedact, QVariantMap(), CorpusLook::Close, PromiseNone, nullptr,
                              &ToolCorpusHelper::checkRedactedResult, &ToolCorpusHelper::isRedactionRefused };
        redact.createParameters = &ToolCorpusHelper::createRedactParameters;
        rows.push_back(redact);
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

    static void checkResultFiles(const CorpusToolRow& row,
                                 const QString& file,
                                 const pdf::PDFDocument* input,
                                 const CorpusMemorySink& sink,
                                 const pdf::PDFDocumentToolResult& result,
                                 CorpusSummary* summary,
                                 QStringList* failures);

    /// The pages of the result, which are drawn and compared: the first and the last page and
    /// the page of the largest image, when it is a page of the result (sorted, unique)
    static std::vector<size_t> getDrawnPages(const pdf::PDFDocument* input, const std::vector<CorpusPageOrigin>& origins);

    /// The page of the output has the boxes, the rotation and the user unit of its page of the
    /// input; false and a failure, when not
    static bool checkPageGeometry(const pdf::PDFDocument* input,
                                  const CorpusPageOrigin& origin,
                                  const pdf::PDFDocument* output,
                                  size_t outputPage,
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
        const bool isInputRestored = reader.isDocumentRestored();

        // A file the reader does not take is no failure of the tool: the row says what happened.
        // It has to be named: another refused file is a failure of the reader or a new file.
        if (reader.getReadingResult() != pdf::PDFDocumentReader::Result::OK)
        {
            const bool isCanceled = reader.getReadingResult() == pdf::PDFDocumentReader::Result::Canceled;
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

        if (const CorpusExclusion* exclusion = ToolCorpusHelper::findContentExclusion(&input, summary.tool))
        {
            summary.status = QStringLiteral("excluded");
            summary.note = QStringLiteral("%1: %2").arg(QString::fromUtf8(exclusion->reference), QString::fromUtf8(exclusion->reason));
            summary.milliseconds = timer.elapsed();
            summary.print();
            QSKIP(qPrintable(summary.note));
        }

        const std::shared_ptr<const pdf::PDFDocumentTool> tool = row.createTool();
        pdf::PDFDocumentToolContext context = pdf::PDFDocumentToolContext::create(pdf::PDFDocumentToolInputDocument::create(pdf::PDFDocumentPointer(new pdf::PDFDocument(input)), inputData, path, false));
        for (int copy = 1; copy < row.inputCount; ++copy)
        {
            context.documents.push_back(context.documents.front());
        }
        CorpusMemorySink sink;
        if (row.isFilePerPage || row.filesCheck)
        {
            context.fileSink = &sink;
        }
        if (row.prepareContext)
        {
            row.prepareContext(&context);
        }
        const QVariantMap parameters = row.createParameters ? row.createParameters(&input, path) : row.parameters;
        const pdf::PDFDocumentToolResult result = tool->run(context, parameters, nullptr, nullptr);

        summary.status = ToolCorpusHelper::getStatusName(result.status);
        summary.note = result.message;

        switch (result.status)
        {
            case pdf::PDFDocumentToolResultStatus::Success:
                break;

            case pdf::PDFDocumentToolResultStatus::NothingToDo:
                if (row.filesCheck)
                {
                    row.filesCheck(&input, result, parameters, sink, &failures);
                }
                else if (row.resultCheck)
                {
                    row.resultCheck(&input, result, parameters, &failures);
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
                if ((tool->getInfo() && !tool->getInfo()->isPermitted(&input)) || !tool->isPermittedRun(&input, parameters) || (row.refusal && row.refusal(&input)) ||
                    (row.readRefusal && row.readRefusal(&input, isInputRestored)))
                {
                    // The tool refused the file by its contract: permissions of the document, also those of
                    // the parameters of the run (Q7.2: the JavaScript and the XMP of a report need bit 5). An
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
            if (row.filesCheck)
            {
                row.filesCheck(&input, result, parameters, sink, &failures);
            }
            else if (row.resultCheck)
            {
                row.resultCheck(&input, result, parameters, &failures);
            }
            else if (row.isFilePerPage)
            {
                checkResultFiles(row, file, &input, sink, result, &summary, &failures);
            }
            else
            {
                checkResultDocument(row, file, row.password, &input, inputData, result, &summary, &failures);
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

    // Written and read back: what the user gets is the file - the bytes the tool has written and
    // verified, when it gives them (the encryption, PDFAppToolResultWriter::createCopyRequest saves them)
    QByteArray written = outputDocument.writtenData;
    if (written.isEmpty())
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

    // No level of a certification (12.8.2.2.1, Table 257: /P 1-3) allows a change of the pages: the
    // tool, which changes them, removes the certification with the values and the appearances of
    // all signatures, /Perms and /DSS - a copy only (Q12 and R10 of step 3, RB5 2 of step 3b, owner
    // answer Q4). Such a result keeps the pages of the input without its signatures, so it is
    // compared with that document; any other result keeps the certification.
    std::optional<pdf::PDFDocument> uncertifiedInput;
    const bool isCertified = !pdf::PDFDocMDPPermissions::isPageChangeAllowed(input);
    const bool isCertificationRemoved = result.data.value(QStringLiteral("certificationRemoved")).toBool();
    if (isCertificationRemoved != isCertified && (isCertificationRemoved || (row.promises & PromiseCertification) != 0))
    {
        failures->append(QStringLiteral("The input is %1, the tool reports the certification %2").arg(isCertified ? QStringLiteral("certified") : QStringLiteral("not certified"), isCertificationRemoved ? QStringLiteral("removed") : QStringLiteral("kept")));
    }
    else if (isCertificationRemoved)
    {
        const qint64 signatures = pdf::PDFForm::getSignatureCount(input);
        const qint64 reported = result.data.value(QStringLiteral("certificationSignaturesRemoved")).toInteger(-1);
        if (reported != signatures)
        {
            failures->append(QStringLiteral("The reported signatures removed with the certification %1 are not the signatures %2").arg(reported).arg(signatures));
        }
        if (result.data.value(QStringLiteral("signatureImpact")).toString() != QStringLiteral("lost-copy-only"))
        {
            failures->append(QStringLiteral("A certified input without the signature impact lost-copy-only"));
        }
        pdf::PDFInteger removed = 0;
        uncertifiedInput = pdf::PDFDocMDPPermissions::removeCertification(*input, &removed);
        input = &*uncertifiedInput;
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

    for (size_t i = 0; i < origins.size(); ++i)
    {
        if (!checkPageGeometry(input, origins[i], &output, i, failures))
        {
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

    for (const size_t page : getDrawnPages(&inputCopy, origins))
    {
        checkPage(row, file, &inputCopy, &output, &inputRenderer, &outputRenderer, page, origins[page], &notes, failures);
    }

    summary->note = notes.join(QStringLiteral("; "));
}

void DocumentToolCorpusTest::checkResultFiles(const CorpusToolRow& row,
                                              const QString& file,
                                              const pdf::PDFDocument* input,
                                              const CorpusMemorySink& sink,
                                              const pdf::PDFDocumentToolResult& result,
                                              CorpusSummary* summary,
                                              QStringList* failures)
{
    // A file per page of the map, in the order of the pages, each one reported as written
    // (plan of step 3 of phase 2, 5.6, R2, R3)
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

    if (sink.files.size() != origins.size())
    {
        failures->append(QStringLiteral("The files %1 are not the pages %2").arg(sink.files.size()).arg(origins.size()));
        return;
    }
    if (result.writtenFiles != sink.getWrittenFiles() || !result.notWrittenFiles.isEmpty())
    {
        failures->append(QStringLiteral("The reported files %1 (not written %2) are not the written files %3").arg(result.writtenFiles.size()).arg(result.notWrittenFiles.size()).arg(sink.getWrittenFiles().size()));
    }

    const bool hasAttachments = ToolCorpusHelper::hasDocumentAttachments(input);
    pdf::PDFDocument inputCopy(*input);
    CorpusRenderer inputRenderer(&inputCopy);
    const std::vector<size_t> drawnPages = getDrawnPages(&inputCopy, origins);

    QStringList notes;
    qint64 size = 0;
    for (size_t i = 0; i < sink.files.size(); ++i)
    {
        const QByteArray& data = sink.files[i].second;
        size += data.size();

        // The new document is not encrypted (R2): no password is asked
        const auto refusePassword = [](bool* ok)
        {
            *ok = false;
            return QString();
        };
        pdf::PDFDocumentReader reader(nullptr, refusePassword, false, false);
        pdf::PDFDocument output = reader.readFromBuffer(data);
        if (reader.getReadingResult() != pdf::PDFDocumentReader::Result::OK)
        {
            failures->append(QStringLiteral("The file %1 cannot be read: %2").arg(sink.files[i].first, reader.getErrorMessage()));
            return;
        }
        if (output.getCatalog()->getPageCount() != 1)
        {
            failures->append(QStringLiteral("The file %1 has %2 pages, not one").arg(sink.files[i].first).arg(output.getCatalog()->getPageCount()));
            return;
        }
        if (ToolCorpusHelper::getEncryptionMode(&output) != pdf::EncryptionMode::None)
        {
            failures->append(QStringLiteral("The file %1 is encrypted, a new document is not (R2)").arg(sink.files[i].first));
        }

        // Owner answer Q6: the attachments of the document are only in the first file
        const bool isAttachmentExpected = i == 0 && hasAttachments;
        if (ToolCorpusHelper::hasDocumentAttachments(&output) != isAttachmentExpected)
        {
            failures->append(QStringLiteral("The file %1 %2 the attachments of the document").arg(sink.files[i].first, isAttachmentExpected ? QStringLiteral("lost") : QStringLiteral("has")));
        }

        if (!checkPageGeometry(input, origins[i], &output, 0, failures))
        {
            return;
        }

        if (std::binary_search(drawnPages.cbegin(), drawnPages.cend(), i))
        {
            CorpusRenderer outputRenderer(&output);
            checkPage(row, file, &inputCopy, &output, &inputRenderer, &outputRenderer, 0, origins[i], &notes, failures);
        }
    }

    summary->sizeAfter = size;
    summary->note = notes.join(QStringLiteral("; "));
}

std::vector<size_t> DocumentToolCorpusTest::getDrawnPages(const pdf::PDFDocument* input, const std::vector<CorpusPageOrigin>& origins)
{
    std::vector<size_t> pages;
    if (origins.empty())
    {
        return pages;
    }

    pages = { 0, origins.size() - 1 };
    const CorpusPageBudget budget(ToolCorpusHelper::getPageTimeLimit());
    if (const std::optional<size_t> imagePage = ToolCorpusHelper::getLargestImagePage(input, &budget))
    {
        const auto origin = std::find_if(origins.cbegin(), origins.cend(), [imagePage](const CorpusPageOrigin& item) { return item.inputPage == *imagePage; });
        if (origin != origins.cend())
        {
            pages.push_back(size_t(std::distance(origins.cbegin(), origin)));
        }
    }
    std::sort(pages.begin(), pages.end());
    pages.erase(std::unique(pages.begin(), pages.end()), pages.end());
    return pages;
}

bool DocumentToolCorpusTest::checkPageGeometry(const pdf::PDFDocument* input,
                                               const CorpusPageOrigin& origin,
                                               const pdf::PDFDocument* output,
                                               size_t outputPage,
                                               QStringList* failures)
{
    constexpr qreal PAGE_TOLERANCE = 0.01;
    const pdf::PDFPage* before = input->getCatalog()->getPage(origin.inputPage);
    const pdf::PDFPage* after = output->getCatalog()->getPage(outputPage);
    const auto isSameBox = [](const QRectF& first, const QRectF& second)
    {
        return qAbs(first.left() - second.left()) <= PAGE_TOLERANCE && qAbs(first.top() - second.top()) <= PAGE_TOLERANCE &&
               qAbs(first.width() - second.width()) <= PAGE_TOLERANCE && qAbs(first.height() - second.height()) <= PAGE_TOLERANCE;
    };

    if (!isSameBox(before->getMediaBox(), after->getMediaBox()) || !isSameBox(before->getCropBox(), after->getCropBox()))
    {
        failures->append(QStringLiteral("The boxes of the page %1 changed").arg(outputPage + 1));
        return false;
    }
    if (pdf::getPageRotationCombined(before->getPageRotation(), origin.extraRotation) != after->getPageRotation())
    {
        failures->append(QStringLiteral("The rotation of the page %1 changed").arg(outputPage + 1));
        return false;
    }
    if (qAbs(before->getUserUnit() - after->getUserUnit()) > PAGE_TOLERANCE)
    {
        failures->append(QStringLiteral("The user unit of the page %1 changed").arg(outputPage + 1));
        return false;
    }
    return true;
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
        if (budget.isOperationCanceled())
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
        if (budgetBefore.isOperationCanceled())
        {
            notes->append(notCompared);
            return;
        }

        const CorpusPageBudget budgetAfter(ToolCorpusHelper::getPageTimeLimit() * 2);
        const QImage after = outputRenderer->render(outputPage, pdf::PageRotation::None, size, &budgetAfter, &errorsAfter);
        if (budgetAfter.isOperationCanceled())
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
        if (budget.isOperationCanceled())
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
        if (budget.isOperationCanceled())
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
