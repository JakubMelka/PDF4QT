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

// Edit Content: the gate of stage E0 (GUI_overhaul/edit_tool/zadani.md 9.4 point 4,
// zadani_overeni.md chapter 8). Without it the work does not go on. Over every test document,
// every page and every form and cell of a tiling pattern the pages use:
//
//   (a) nothing changed - nothing written: a snapshot of every loaded container, written with
//       the default options, leaves the document as it is (the storage of the builder is the
//       storage of the document), and the tool edit.content has nothing to do;
//   (b) a forced rewrite without edits gives for every page one Flate stream equal byte for byte
//       to the streams of its /Contents joined by one line feed (ISO 32000-2, 7.8.2), for every
//       form and pattern cell its own decoded stream, and every page renders pixel for pixel as
//       before; a read-only container is never written;
//   (c) a touch of every node keeps the rendering: (1) a transform by the identity matrix wraps
//       every leaf, which can have a wrap (R-1); (2) every node, which can be moved, is moved to
//       another place of its parent and back (R-2, R-3); (3) undo of touch 1 gives the loaded
//       containers again and redo the same written bytes. After touches 1 and 2 every page
//       renders pixel for pixel as the original, the written content builds again with the same
//       issues and the same number of objects of each kind.
//
// Where the renderer is blind (zadani_overeni.md 8, second paragraph: DefaultRGB, the widths of
// the font dictionary, Tw of a Type 0 font, the rendering mode of a Type 3 font, the space of a
// soft mask, the matrix of a pattern), a rendering proves nothing, so touch 1 compares the
// written bytes and values instead: the operations of every wrapped object are written byte for
// byte (the text operators with their widths, Tw and Tr included), an identity touch adds no
// "cm" except the X cm ... X^-1 cm of a soft mask set again in its own space (R-6), every pattern
// added by a touch is an exact copy of the pattern it replaces with the same /Matrix, and every
// entry of the original resources stays (R-4, DefaultRGB). The two soft mask documents have their
// written content by hand.
//
// A path is counted by what it does (painting, clipping): a path, which paints and clips, is
// split by a wrap into one, which paints, and one, which clips (R-1).
//
// Rows name the document; a failed check names "<document> / <container> / <check>". A check,
// which fails because of a defect of the core, is a finding of
// GUI_overhaul/phase2_briefs/pe0gate_state.md: the table getFindings marks exactly that check
// with QEXPECT_FAIL, so the other checks still run. The expected values are written by hand from
// the documents, the plan and ISO 32000-2, never taken from a run.
//
// The function corpus runs (a), (b) and (c) over the PDF files of a list given by the
// environment variable PDF4QT_ROUNDTRIP_CORPUS (one path a line, at most 20); it is skipped
// without the variable.

#include "contenttestdocuments.h"

#include "pdfcatalog.h"
#include "pdfcms.h"
#include "pdfcontentcommands.h"
#include "pdfcontentdocumentwriter.h"
#include "pdfcontenteditsession.h"
#include "pdfcontentnode.h"
#include "pdfcontentoperation.h"
#include "pdfcontentstreamwriter.h"
#include "pdfcontenttreebuilder.h"
#include "pdfcontentundostack.h"
#include "pdfdocument.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumenttool.h"
#include "pdfdocumenttool_content.h"
#include "pdfexception.h"
#include "pdffont.h"
#include "pdfobject.h"
#include "pdfoperationcontrol.h"
#include "pdfoptionalcontent.h"
#include "pdfpage.h"
#include "pdfrenderer.h"
#include "pdfsecurityhandler.h"

#include <QtTest>
#include <QByteArray>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QTransform>
#include <QVariantMap>

#include <algorithm>
#include <array>
#include <deque>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <utility>
#include <vector>

using namespace pdf;

using Documents = pdftest::ContentTestDocuments;
using Factory = PDFContentCommandFactory;
using Key = PDFContentContainerKey;
using Kind = PDFContentNodeKind;
using Writer = PDFContentDocumentWriter;
using Status = PDFContentWriteResult::Status;

/// Cancels the work on a file of the corpus after its time limit
class ContentRoundTripDeadline : public PDFOperationControl
{
public:
    explicit ContentRoundTripDeadline(qint64 milliseconds) : m_milliseconds(milliseconds) { m_timer.start(); }

    virtual bool isOperationCanceled() const override { return m_timer.elapsed() > m_milliseconds; }

    qint64 getElapsed() const { return m_timer.elapsed(); }
    qint64 getLimit() const { return m_milliseconds; }

private:
    QElapsedTimer m_timer;
    qint64 m_milliseconds = 0;
};

/// The containers of a document loaded by one session: every page, then every form and cell of
/// a tiling pattern, which a loaded container names (one visited set, the nesting limit of the
/// session)
struct ContentRoundTripLoaded
{
    PDFDocumentPointer document;
    std::unique_ptr<PDFContentEditSession> session;
    std::vector<Key> keys;                                  ///< The loaded containers in the order of loading (the pages first)
    std::map<Key, PDFContentContainerPointer> originals;    ///< The containers as they were loaded
    std::map<Key, std::vector<Key>> usedKeys;               ///< The forms and pattern cells each loaded container names
    std::map<Key, PDFInteger> pageIndices;                  ///< The page each container was loaded for (a form: the first page using it)
    QStringList failures;                                   ///< The containers, which could not be loaded, with the reason
};

/// What the content of a page consists of: the page and the forms it reaches (through forms,
/// each once). Pattern cells are not counted: a touch replaces a pattern by an exact copy (R-5),
/// which the gate checks by itself.
struct ContentRoundTripCensus
{
    std::map<Kind, int> objects;    ///< Nodes of the kinds TextObject, Image, FormUse, Shading
    int paintingPaths = 0;          ///< Paths, which stroke or fill
    int clippingPaths = 0;          ///< Paths with W or W* (a path, which paints and clips, counts here and above)
    int otherPaths = 0;             ///< Paths, which neither paint nor clip (n)
    int matrixOperations = 0;       ///< Operations cm
    int saveOperations = 0;         ///< Operations q
    std::vector<int> issues;        ///< Kinds of the issues of the builder, sorted
    int unloaded = 0;               ///< Forms, which cannot be loaded

    bool operator==(const ContentRoundTripCensus&) const = default;
};

/// A check, which fails because of a defect of the core: a finding of pe0gate_state.md
struct ContentRoundTripFinding
{
    const char* row;        ///< The document
    const char* where;      ///< The container (and node)
    const char* check;      ///< The check
    const char* comment;    ///< "finding <n> of pe0gate_state.md"
};

class ContentRoundTripTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void unchangedIsNotWritten_data();
    void unchangedIsNotWritten();
    void forcedRewriteKeepsBytes_data();
    void forcedRewriteKeepsBytes();
    void touchWrapKeepsRendering_data();
    void touchWrapKeepsRendering();
    void touchMoveKeepsRendering_data();
    void touchMoveKeepsRendering();
    void touchUndoRestoresModel_data();
    void touchUndoRestoresModel();
    void corpus_data();
    void corpus();

private:
    /// Side of a rendered page in pixels (a page of the test documents is 200 points: 2 pixels a point)
    static constexpr qreal RENDER_SIZE = 400.0;

    /// Time limit of one file of the corpus in seconds (scaled by PDF4QT_TEST_TIMEOUT_FACTOR)
    static constexpr int CORPUS_FILE_TIME_LIMIT_SECONDS = 600;

    /// Largest number of files of the list of the corpus
    static constexpr int CORPUS_MAXIMUM_FILE_COUNT = 20;

    // ---- the documents ----

    static std::vector<std::pair<QString, std::function<PDFDocument()>>> getDocuments();
    static void addDocumentRows();
    static PDFDocument createTestDocument(const QString& name);
    static bool isHandWrittenRow(const QString& row);

    /// Unknown operators, ends without beginnings, an unclosed group, an undefined font, a path
    /// without its painting operator and an unfinished string at the end
    static QByteArray hostilePageContent();

    /// A page drawing the form /Fm1, which draws itself (a cycle of forms)
    static PDFDocument createSelfDrawingFormDocument();

    // ---- the checks ----

    static std::vector<ContentRoundTripFinding> getFindings();

    /// QVERIFY2 with the place of the check; QEXPECT_FAIL first, when the check is a finding
    static void check(bool condition, const QString& row, const QString& where, const char* checkName, const QString& details);

    // ---- loading ----

    static void loadDocument(const PDFDocument& document, const PDFOperationControl* control, ContentRoundTripLoaded* loaded);
    static std::vector<Key> getUsedKeys(const PDFDocument& document, const Key& key, PDFInteger pageIndex);

    /// A container built by the tree builder alone, with the caches of the call
    static PDFContentBuildResult buildContainer(const PDFDocument& document, const Key& key, PDFInteger pageIndex);
    static QString describeKey(const Key& key);
    static QString getKindName(Kind kind);
    static std::vector<PDFContentNodePointer> getNodesInPreorder(const PDFContentContainer& container);
    static std::vector<PDFContentNodePointer> getSubtreeInPreorder(const PDFContentNodePointer& top);
    static bool isObjectKind(Kind kind);

    /// Every container reached from a page (forms and pattern cells, transitively, each once)
    static std::set<Key> getReachedKeys(const ContentRoundTripLoaded& loaded, const Key& pageKey);

    static ContentRoundTripCensus getCensus(const ContentRoundTripLoaded& loaded, PDFInteger pageIndex);

    /// Adds the objects, the operations and the issues of one container to a census
    static void addToCensus(const PDFContentContainer& container, ContentRoundTripCensus* census);
    static QString describeCensus(const ContentRoundTripCensus& census);

    // ---- reading a document ----

    /// The decoded streams of /Contents of a page joined by one line feed; \p isReadable is false,
    /// when an item is no stream
    static QByteArray getJoinedPageContent(const PDFDocument& document, PDFInteger pageIndex, bool* isReadable);

    /// The decoded data of the stream of a reference (empty for anything else)
    static QByteArray getStreamData(const PDFDocument& document, PDFObjectReference reference);

    /// The decoded data of a stream; \p isDecoded is set to false, when they cannot be decoded
    static QByteArray decodeStream(const PDFDocument& document, const PDFStream* stream, bool* isDecoded);

    /// The written content of a container: the joined page content or the stream of the object
    static QByteArray getContainerContent(const PDFDocument& document, const Key& key);

    /// The resources of a container in a document: of the page (inherited included) or of the
    /// stream of the form / pattern
    static PDFObject getContainerResources(const PDFDocument& document, const Key& key);

    /// Every entry of the original resources is in the written ones with the same value (R-4);
    /// the problems found
    static QStringList compareResources(const PDFDocument& original, const PDFObject& originalResources, const PDFDocument& written, const PDFObject& writtenResources);

    /// Every pattern, which the written resources have and the original ones have not, is an
    /// exact copy of an original pattern with the same /Matrix (R-5 with the identity); the problems found
    static QStringList comparePatternCopies(const PDFDocument& original, const PDFObject& originalResources, const PDFDocument& written, const PDFObject& writtenResources);

    static std::array<PDFReal, 6> getMatrix(const PDFDocument& document, const PDFDictionary* dictionary);
    static PDFReal getNumber(const PDFObject& object);

    /// A content as its tokens joined by one space (white space collapsed, the delimiters of a
    /// name, an array and a dictionary stand apart)
    static QByteArray getTokens(const QByteArray& content);

    // ---- rendering ----

    static QImage renderPage(const PDFDocument& document, PDFInteger pageIndex);
    static QString describeImageDifference(const QImage& expected, const QImage& actual);

    /// Every page of \p written renders as the page of \p original (the original twice first:
    /// two renderings of one document must be identical, or the comparison proves nothing)
    static void compareRenderings(const QString& row, const char* checkName, const PDFDocument& original, const PDFDocument& written);

    // ---- writing ----

    static PDFContentEditSnapshot createFullSnapshot(const ContentRoundTripLoaded& loaded);
    static PDFDocumentToolContext createToolContext(const PDFDocument& document);
    static QVariantMap createApplyParameters(const PDFContentEditSnapshotPointer& snapshot);
    static QString describeErrors(const PDFContentWriteResult& result);

    // ---- the parts of the gate (the corpus runs them too) ----

    static void runUnchanged(const QString& row, const PDFDocument& document, const PDFOperationControl* control);
    static void runForcedRewrite(const QString& row, const PDFDocument& document, const PDFOperationControl* control);
    static void runTouchWrap(const QString& row, const PDFDocument& document, const PDFOperationControl* control);
    static void runTouchMove(const QString& row, const PDFDocument& document, const PDFOperationControl* control);
    static void runTouchUndo(const QString& row, const PDFDocument& document, const PDFOperationControl* control);

    /// Touch 1: an identity transform of every node except the root, groups and marked contents
    /// (a transform of those goes to their leaves, which are touched on their own); one command
    /// per node, one macro per container
    static void applyWraps(ContentRoundTripLoaded* loaded, std::map<Key, std::vector<PDFContentNodeId>>* wrapped, std::map<QString, int>* refusals);

    /// The part (a) over the current containers of a session: nothing is written
    static void checkNothingWritten(const QString& row, const char* checkName, const ContentRoundTripLoaded& loaded, const PDFOperationControl* control);

    static void printRefusals(const QString& row, const char* part, size_t accepted, const std::map<QString, int>& refusals);

    /// Every object of the original document, which is not one of \p writtenObjects (the
    /// pages, forms and patterns written), is in the written document as it was
    static void checkOtherObjects(const QString& row, const char* checkName, const PDFDocument& original, const PDFDocument& written, const std::set<PDFObjectReference>& writtenObjects);
};

// -------------------------------------------------------------------------------------------------
//  The documents
// -------------------------------------------------------------------------------------------------

QByteArray ContentRoundTripTest::hostilePageContent()
{
    return QByteArray("1 0 0 rg 10 10 20 20 re f\n"
                      "xyz 5 Q EMC ET\n"
                      "q 0 0 1 rg 50 50 20 20 re f\n"
                      "/F9 12 Tf BT 10 100 Td (A) Tj ET\n"
                      "0 1 0 rg 100 100 m 150 150 l 100 150 l\n"
                      "% a comment before the end\n"
                      "(unfinished");
}

PDFDocument ContentRoundTripTest::createSelfDrawingFormDocument()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference form = builder.addObject(PDFObject());
    const QByteArray formContent("/Fm1 Do 0 0 1 rg 10 10 5 5 re f");

    PDFDictionaryBuilder dictionary;
    dictionary.addEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("XObject"));
    dictionary.addEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Form"));
    dictionary.addEntry(PDFInplaceOrMemoryString("BBox"), Documents::createIntegerArrayObject({ 0, 0, 100, 100 }));
    dictionary.addEntry(PDFInplaceOrMemoryString("Resources"), Documents::createDictionaryObject({ { "XObject", Documents::createDictionaryObject({ { "Fm1", PDFObject::createReference(form) } }) } }));
    dictionary.addEntry(PDFInplaceOrMemoryString(PDF_STREAM_DICT_LENGTH), PDFObject::createInteger(formContent.size()));
    builder.setObject(form, PDFObject::createStream(PDFStream(std::move(dictionary), QByteArray(formContent))));

    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, Documents::PAGE_SIZE, Documents::PAGE_SIZE));
    builder.mergeTo(page, Documents::createDictionaryObject({ { "Resources", Documents::createDictionaryObject({ { "XObject", Documents::createDictionaryObject({ { "Fm1", PDFObject::createReference(form) } }) } }) },
                                                              { "Contents", Documents::addStreamObject(&builder, Documents::DictionaryEntries(), "/Fm1 Do 1 0 0 rg 0 0 5 5 re f") } }));
    return builder.build();
}

std::vector<std::pair<QString, std::function<PDFDocument()>>> ContentRoundTripTest::getDocuments()
{
    using Variant = Documents::Type3Variant;

    std::vector<std::pair<QString, std::function<PDFDocument()>>> documents;

    // The documents of the gate (contenttestdocuments.h, "Documents of the round trip")
    documents.emplace_back(QStringLiteral("image orientations"), &Documents::createImageOrientationsDocument);
    documents.emplace_back(QStringLiteral("text orientations"), &Documents::createTextOrientationsDocument);
    documents.emplace_back(QStringLiteral("split operation"), &Documents::createSplitOperationDocument);
    documents.emplace_back(QStringLiteral("page tree"), &Documents::createPageTreeDocument);
    documents.emplace_back(QStringLiteral("two tiling patterns"), &Documents::createTwoTilingPatternsDocument);
    documents.emplace_back(QStringLiteral("clipping"), &Documents::createClippingDocument);

    // Every font of the shared documents: Tc and Tw, a code without a glyph, the Type 0 fonts
    // (Tw only for the single-byte code 32), a vertical font, Type 3 fonts, an invisible Type 3
    // text (3 Tr: the renderer is blind to it, the bytes of touch 1 are checked) and TJ
    documents.emplace_back(QStringLiteral("fonts"), []()
    {
        return Documents::createFontsDocument(QByteArray("BT /F1 12 Tf 2 Tw 1 Tc 10 180 Td (AB CD) Tj ET\n"
                                                         "BT /F2 12 Tf 10 160 Td (A B) Tj ET\n"
                                                         "BT /FC 12 Tf 10 140 Td (\\001A) Tj ET\n"
                                                         "BT /F0 12 Tf 3 Tw 10 120 Td <00410020004200640065> Tj ET\n"
                                                         "BT /FM 12 Tf 3 Tw 10 100 Td <20418020> Tj ET\n"
                                                         "BT /FN 12 Tf 3 Tw 10 80 Td <412042> Tj ET\n"
                                                         "BT /FV 12 Tf 150 180 Td <00410042> Tj ET\n"
                                                         "BT /T3 20 Tf 10 40 Td (AB C) Tj ET\n"
                                                         "BT /TN 20 Tf 3 Tr 100 40 Td (A B) Tj ET\n"
                                                         "BT /F1 12 Tf 10 20 Td [(A) -500 (B)] TJ ET\n"));
    });

    // Type 3 fonts of every variant, the hostile ones included (a procedure showing its own
    // glyph, a chain deeper than the limit, texts inside procedures, stray operands)
    const std::vector<std::pair<QString, Variant>> variants = { { QStringLiteral("type3 plain"), Variant::Plain },
                                                                { QStringLiteral("type3 self-referencing"), Variant::SelfReferencing },
                                                                { QStringLiteral("type3 chain"), Variant::Chain },
                                                                { QStringLiteral("type3 inner text"), Variant::InnerText },
                                                                { QStringLiteral("type3 unclosed text"), Variant::UnclosedText },
                                                                { QStringLiteral("type3 end text only"), Variant::EndTextOnly },
                                                                { QStringLiteral("type3 no space procedure"), Variant::NoSpaceProcedure },
                                                                { QStringLiteral("type3 stray operands"), Variant::StrayOperands } };
    for (const auto& variant : variants)
    {
        const Variant value = variant.second;
        documents.emplace_back(variant.first, [value]()
        {
            const QByteArray text = (value == Variant::Chain) ? QByteArray("(a)") : QByteArray("(AB C)");
            return Documents::createType3Document("BT /T3 50 Tf 10 10 Td " + text + " Tj ET\nBT /F1 12 Tf 10 150 Td (x) Tj ET\n", value);
        });
    }

    documents.emplace_back(QStringLiteral("form"), &Documents::createFormDocument);
    documents.emplace_back(QStringLiteral("inline image plain"), []() { return Documents::createInlineImageDocument(Documents::InlineImageVariant::Plain); });
    documents.emplace_back(QStringLiteral("inline image end marker in data"), []() { return Documents::createInlineImageDocument(Documents::InlineImageVariant::EndMarkerInData); });
    documents.emplace_back(QStringLiteral("inline image named color space"), []() { return Documents::createInlineImageDocument(Documents::InlineImageVariant::NamedColorSpace); });
    documents.emplace_back(QStringLiteral("tagged"), &Documents::createTaggedDocument);
    documents.emplace_back(QStringLiteral("soft mask"), &Documents::createSoftMaskDocument);
    documents.emplace_back(QStringLiteral("soft mask image"), &Documents::createSoftMaskImageDocument);

    // Every tiling pattern of the shared resources (the lattice, negative steps, a rotation, a
    // cell with stray operands, a pattern of a form) and a text filled by a pattern
    documents.emplace_back(QStringLiteral("tiling patterns"), []()
    {
        return Documents::createTilingPatternDocument(QByteArray("/Pattern cs /P1 scn 0 0 40 40 re f\n"
                                                                 "/P2 scn 50 0 40 40 re f\n"
                                                                 "/P3 scn 100 0 40 40 re f\n"
                                                                 "/P4 scn 150 0 40 40 re f\n"
                                                                 "/P5 scn 0 50 40 40 re f\n"
                                                                 "/P6 scn 50 50 40 40 re f\n"
                                                                 "/P7 scn 100 50 40 40 re f\n"
                                                                 "BT /F1 40 Tf /P3 scn 110 150 Td (Ab) Tj ET\n"
                                                                 "q 0.5 0 0 0.5 0 100 cm /FmP Do Q\n"));
    });

    // Broken and hostile input: a page, whose /Contents cannot be read whole (read-only, never
    // written), hostile operators, a cycle of forms
    documents.emplace_back(QStringLiteral("broken contents"), []() { return Documents::createBrokenContentsDocument("1 0 0 rg", "0 0 5 5 re f", &Documents::createNoResources); });
    documents.emplace_back(QStringLiteral("hostile content"), []() { return Documents::createDocument(hostilePageContent(), &Documents::createNoResources); });
    documents.emplace_back(QStringLiteral("self-drawing form"), &ContentRoundTripTest::createSelfDrawingFormDocument);
    return documents;
}

void ContentRoundTripTest::addDocumentRows()
{
    QTest::addColumn<QString>("document");
    for (const auto& item : getDocuments())
    {
        QTest::newRow(qPrintable(item.first)) << item.first;
    }
}

PDFDocument ContentRoundTripTest::createTestDocument(const QString& name)
{
    for (const auto& item : getDocuments())
    {
        if (item.first == name)
        {
            return item.second();
        }
    }
    return PDFDocument();
}

bool ContentRoundTripTest::isHandWrittenRow(const QString& row)
{
    for (const auto& item : getDocuments())
    {
        if (item.first == row)
        {
            return true;
        }
    }
    return false;
}

// -------------------------------------------------------------------------------------------------
//  The checks
// -------------------------------------------------------------------------------------------------

std::vector<ContentRoundTripFinding> ContentRoundTripTest::getFindings()
{
    return { };
}

void ContentRoundTripTest::check(bool condition, const QString& row, const QString& where, const char* checkName, const QString& details)
{
    for (const ContentRoundTripFinding& finding : getFindings())
    {
        if (row == QLatin1String(finding.row) && where == QLatin1String(finding.where) && qstrcmp(checkName, finding.check) == 0)
        {
            QEXPECT_FAIL("", finding.comment, Continue);
            break;
        }
    }

    QVERIFY2(condition, qPrintable(QStringLiteral("%1 / %2 / %3: %4").arg(row, where, QString::fromLatin1(checkName), details)));
}

// -------------------------------------------------------------------------------------------------
//  Loading
// -------------------------------------------------------------------------------------------------

void ContentRoundTripTest::loadDocument(const PDFDocument& document, const PDFOperationControl* control, ContentRoundTripLoaded* loaded)
{
    loaded->document = PDFDocumentPointer(new PDFDocument(document));
    loaded->session = std::make_unique<PDFContentEditSession>(loaded->document, PDFCMSPointer(), nullptr);

    struct Item
    {
        Key key;
        PDFInteger pageIndex = -1;
        int depth = 0;
    };

    std::deque<Item> queue;
    const size_t pageCount = loaded->document->getCatalog()->getPageCount();
    for (size_t i = 0; i < pageCount; ++i)
    {
        queue.push_back(Item{ loaded->session->getPageKey(PDFInteger(i)), PDFInteger(i), 0 });
    }

    // Breadth first: a form or a cell is loaded only after a container, which names it (the
    // session needs its page and its state); a cycle ends at the visited set
    std::set<Key> visited;
    while (!queue.empty())
    {
        const Item item = queue.front();
        queue.pop_front();

        if (!visited.insert(item.key).second)
        {
            continue;
        }

        QString errorText;
        if (!loaded->session->loadContainer(item.key, control, &errorText))
        {
            loaded->failures << QStringLiteral("%1: %2").arg(describeKey(item.key), errorText);
            continue;
        }

        const PDFContentContainerPointer container = loaded->session->getContainer(item.key);
        loaded->keys.push_back(item.key);
        loaded->originals[item.key] = container;
        loaded->pageIndices[item.key] = item.pageIndex;

        const std::vector<Key> used = getUsedKeys(*loaded->document, item.key, item.pageIndex);
        loaded->usedKeys[item.key] = used;
        if (item.depth < PDFContentEditSession::MAXIMUM_CONTAINER_NESTING)
        {
            for (const Key& usedKey : used)
            {
                queue.push_back(Item{ usedKey, item.pageIndex, item.depth + 1 });
            }
        }
    }
}

std::vector<Key> ContentRoundTripTest::getUsedKeys(const PDFDocument& document, const Key& key, PDFInteger pageIndex)
{
    // The session does not tell which forms and cells a container names: the builder does
    const PDFContentBuildResult result = buildContainer(document, key, pageIndex);
    std::vector<Key> keys = result.usedForms;
    keys.insert(keys.end(), result.usedPatternCells.cbegin(), result.usedPatternCells.cend());
    return keys;
}

PDFContentBuildResult ContentRoundTripTest::buildContainer(const PDFDocument& document, const Key& key, PDFInteger pageIndex)
{
    PDFCMSGeneric cms;
    PDFFontCache fontCache(32, 32);
    PDFOptionalContentActivity activity(&document, OCUsage::View, nullptr);
    fontCache.setDocument(PDFModifiedDocument(const_cast<PDFDocument*>(&document), &activity));

    PDFContentBuildContext context;
    context.fontCache = &fontCache;
    context.cms = &cms;
    context.pageIndex = pageIndex;

    PDFContentBuildOptions options;
    options.layoutText = false;

    return PDFContentTreeBuilder::build(&document, key, nullptr, context, options, nullptr, nullptr);
}

QString ContentRoundTripTest::describeKey(const Key& key)
{
    switch (key.kind)
    {
        case Key::Kind::Page:
            return QStringLiteral("page %1").arg(key.pageIndex + 1);
        case Key::Kind::Form:
            return QStringLiteral("form %1").arg(key.reference.objectNumber);
        case Key::Kind::PatternCell:
            return QStringLiteral("pattern %1").arg(key.reference.objectNumber);
    }
    return QString();
}

QString ContentRoundTripTest::getKindName(Kind kind)
{
    switch (kind)
    {
        case Kind::Root:
            return QStringLiteral("Root");
        case Kind::Group:
            return QStringLiteral("Group");
        case Kind::MarkedContent:
            return QStringLiteral("MarkedContent");
        case Kind::MarkedPoint:
            return QStringLiteral("MarkedPoint");
        case Kind::TextObject:
            return QStringLiteral("TextObject");
        case Kind::TextShow:
            return QStringLiteral("TextShow");
        case Kind::Path:
            return QStringLiteral("Path");
        case Kind::Image:
            return QStringLiteral("Image");
        case Kind::FormUse:
            return QStringLiteral("FormUse");
        case Kind::Shading:
            return QStringLiteral("Shading");
        case Kind::StateRun:
            return QStringLiteral("StateRun");
        case Kind::Opaque:
            return QStringLiteral("Opaque");
    }
    return QString();
}

std::vector<PDFContentNodePointer> ContentRoundTripTest::getNodesInPreorder(const PDFContentContainer& container)
{
    return getSubtreeInPreorder(container.getRoot());
}

std::vector<PDFContentNodePointer> ContentRoundTripTest::getSubtreeInPreorder(const PDFContentNodePointer& top)
{
    std::vector<PDFContentNodePointer> result;
    std::vector<PDFContentNodePointer> stack;
    if (top)
    {
        stack.push_back(top);
    }

    while (!stack.empty())
    {
        const PDFContentNodePointer node = stack.back();
        stack.pop_back();
        result.push_back(node);

        const std::vector<PDFContentNodePointer>& children = node->getChildren();
        stack.insert(stack.end(), children.crbegin(), children.crend());
    }
    return result;
}

bool ContentRoundTripTest::isObjectKind(Kind kind)
{
    // The objects of the facts of the writer (pe0p05_state.md): what a page paints
    switch (kind)
    {
        case Kind::TextObject:
        case Kind::Path:
        case Kind::Image:
        case Kind::FormUse:
        case Kind::Shading:
            return true;
        default:
            return false;
    }
}

std::set<Key> ContentRoundTripTest::getReachedKeys(const ContentRoundTripLoaded& loaded, const Key& pageKey)
{
    std::set<Key> visited;
    std::vector<Key> stack = { pageKey };
    while (!stack.empty())
    {
        const Key key = stack.back();
        stack.pop_back();
        if (!visited.insert(key).second)
        {
            continue;
        }

        auto it = loaded.usedKeys.find(key);
        if (it != loaded.usedKeys.end())
        {
            stack.insert(stack.end(), it->second.cbegin(), it->second.cend());
        }
    }
    return visited;
}

ContentRoundTripCensus ContentRoundTripTest::getCensus(const ContentRoundTripLoaded& loaded, PDFInteger pageIndex)
{
    ContentRoundTripCensus census;

    std::set<Key> visited;
    std::vector<Key> stack = { loaded.session->getPageKey(pageIndex) };
    while (!stack.empty())
    {
        const Key key = stack.back();
        stack.pop_back();
        if (key.kind == Key::Kind::PatternCell || !visited.insert(key).second)
        {
            continue;
        }

        auto it = loaded.originals.find(key);
        if (it == loaded.originals.end() || !it->second)
        {
            ++census.unloaded;
            continue;
        }

        addToCensus(*it->second, &census);

        auto itUsed = loaded.usedKeys.find(key);
        if (itUsed != loaded.usedKeys.end())
        {
            stack.insert(stack.end(), itUsed->second.cbegin(), itUsed->second.cend());
        }
    }
    return census;
}

void ContentRoundTripTest::addToCensus(const PDFContentContainer& container, ContentRoundTripCensus* census)
{
    // A path counts by what it does: a path, which paints and clips, is split by a wrap into a
    // painting path and a clipping path (R-1), which paint and clip the same
    for (const PDFContentNodePointer& node : getNodesInPreorder(container))
    {
        if (node->getKind() == Kind::Path)
        {
            const PDFContentPathPaint& paint = node->getData().paint;
            census->paintingPaths += (paint.stroke || paint.fill) ? 1 : 0;
            census->clippingPaths += (paint.clip != PDFContentPathPaint::Clip::None) ? 1 : 0;
            census->otherPaths += (!paint.stroke && !paint.fill && paint.clip == PDFContentPathPaint::Clip::None) ? 1 : 0;
        }
        else if (isObjectKind(node->getKind()))
        {
            ++census->objects[node->getKind()];
        }
    }

    // The operations of a container, which was just built, are its original ones
    const PDFContentOperationList& operations = container.getOperations();
    for (size_t i = 0; i < operations.getCount(); ++i)
    {
        const PDFContentOperator contentOperator = operations.getOperation(i).getOperator();
        census->matrixOperations += (contentOperator == PDFContentOperator::AdjustCurrentTransformationMatrix) ? 1 : 0;
        census->saveOperations += (contentOperator == PDFContentOperator::SaveGraphicState) ? 1 : 0;
    }

    for (const PDFContentIssue& issue : container.getIssues())
    {
        census->issues.push_back(int(issue.kind));
    }
    std::sort(census->issues.begin(), census->issues.end());
}

QString ContentRoundTripTest::describeCensus(const ContentRoundTripCensus& census)
{
    QStringList parts;
    for (const auto& item : census.objects)
    {
        parts << QStringLiteral("%1 %2").arg(item.second).arg(getKindName(item.first));
    }
    parts << QStringLiteral("paths: %1 painting, %2 clipping, %3 other").arg(census.paintingPaths).arg(census.clippingPaths).arg(census.otherPaths);
    parts << QStringLiteral("%1 cm, %2 q").arg(census.matrixOperations).arg(census.saveOperations);

    QStringList issues;
    for (const int issue : census.issues)
    {
        issues << QString::number(issue);
    }
    parts << QStringLiteral("issues [%1]").arg(issues.join(QLatin1Char(' ')));
    parts << QStringLiteral("%1 forms not loaded").arg(census.unloaded);
    return parts.join(QStringLiteral(", "));
}

// -------------------------------------------------------------------------------------------------
//  Reading a document
// -------------------------------------------------------------------------------------------------

QByteArray ContentRoundTripTest::getJoinedPageContent(const PDFDocument& document, PDFInteger pageIndex, bool* isReadable)
{
    *isReadable = true;

    const PDFPage* page = document.getCatalog()->getPage(size_t(pageIndex));
    const PDFDictionary* pageDictionary = page ? document.getDictionaryFromObject(document.getObjectByReference(page->getPageReference())) : nullptr;
    if (!pageDictionary)
    {
        *isReadable = false;
        return QByteArray();
    }

    const PDFObject& contents = document.getObject(pageDictionary->get("Contents"));
    if (contents.isNull())
    {
        return QByteArray();
    }

    if (contents.isStream())
    {
        return decodeStream(document, contents.getStream(), isReadable);
    }

    if (!contents.isArray())
    {
        *isReadable = false;
        return QByteArray();
    }

    // ISO 32000-2, 7.8.2: the streams of an array are one content, as if joined by white space
    // (the builder of the tree joins them by one line feed)
    QByteArray result;
    const PDFArray* array = contents.getArray();
    for (size_t i = 0; i < array->getCount(); ++i)
    {
        const PDFObject& item = document.getObject(array->getItem(i));
        if (!item.isStream())
        {
            *isReadable = false;
            continue;
        }

        if (i > 0)
        {
            result += '\n';
        }
        result += decodeStream(document, item.getStream(), isReadable);
    }
    return result;
}

QByteArray ContentRoundTripTest::decodeStream(const PDFDocument& document, const PDFStream* stream, bool* isDecoded)
{
    // A stream, which cannot be decoded, throws (the filters report the error so)
    try
    {
        return document.getDecodedStream(stream);
    }
    catch (const PDFException&)
    {
        *isDecoded = false;
        return QByteArray();
    }
}

QByteArray ContentRoundTripTest::getStreamData(const PDFDocument& document, PDFObjectReference reference)
{
    const PDFObject& object = document.getObjectByReference(reference);
    bool isDecoded = true;
    return object.isStream() ? decodeStream(document, object.getStream(), &isDecoded) : QByteArray();
}

QByteArray ContentRoundTripTest::getContainerContent(const PDFDocument& document, const Key& key)
{
    if (key.kind == Key::Kind::Page)
    {
        bool isReadable = false;
        return getJoinedPageContent(document, key.pageIndex, &isReadable);
    }
    return getStreamData(document, key.reference);
}

PDFObject ContentRoundTripTest::getContainerResources(const PDFDocument& document, const Key& key)
{
    if (key.kind == Key::Kind::Page)
    {
        const PDFPage* page = document.getCatalog()->getPage(size_t(key.pageIndex));
        return page ? document.getObject(page->getResources()) : PDFObject();
    }

    const PDFObject& object = document.getObjectByReference(key.reference);
    return object.isStream() ? document.getObject(object.getStream()->getDictionary()->get("Resources")) : PDFObject();
}

QStringList ContentRoundTripTest::compareResources(const PDFDocument& original, const PDFObject& originalResources, const PDFDocument& written, const PDFObject& writtenResources)
{
    QStringList problems;
    const PDFDictionary* originalDictionary = original.getDictionaryFromObject(originalResources);
    if (!originalDictionary)
    {
        return problems;
    }

    const PDFDictionary* writtenDictionary = written.getDictionaryFromObject(writtenResources);
    if (!writtenDictionary)
    {
        problems << QStringLiteral("the written container has no resources");
        return problems;
    }

    for (size_t i = 0; i < originalDictionary->getCount(); ++i)
    {
        const QByteArray key = originalDictionary->getKey(i).getString();
        const PDFObject& originalValue = original.getObject(originalDictionary->getValue(i));
        const PDFObject& writtenValue = written.getObject(writtenDictionary->get(key));

        if (!originalValue.isDictionary())
        {
            // /ProcSet and other keys: kept as they are
            if (originalValue != writtenValue)
            {
                problems << QStringLiteral("/%1 changed").arg(QString::fromLatin1(key));
            }
            continue;
        }

        // A category: every original entry stays with its value (a direct value is compared,
        // a reference must name the same object)
        const PDFDictionary* originalCategory = originalValue.getDictionary();
        const PDFDictionary* writtenCategory = writtenValue.isDictionary() ? writtenValue.getDictionary() : nullptr;
        for (size_t j = 0; j < originalCategory->getCount(); ++j)
        {
            const QByteArray name = originalCategory->getKey(j).getString();
            if (!writtenCategory || !writtenCategory->hasKey(name))
            {
                problems << QStringLiteral("/%1 /%2 is missing").arg(QString::fromLatin1(key), QString::fromLatin1(name));
            }
            else if (writtenCategory->get(name) != originalCategory->getValue(j))
            {
                problems << QStringLiteral("/%1 /%2 changed").arg(QString::fromLatin1(key), QString::fromLatin1(name));
            }
        }
    }
    return problems;
}

PDFReal ContentRoundTripTest::getNumber(const PDFObject& object)
{
    if (object.isInt())
    {
        return PDFReal(object.getInteger());
    }
    return object.isReal() ? object.getReal() : std::numeric_limits<PDFReal>::quiet_NaN();
}

std::array<PDFReal, 6> ContentRoundTripTest::getMatrix(const PDFDocument& document, const PDFDictionary* dictionary)
{
    // ISO 32000-2, Table 75: /Matrix of a pattern, the identity by default
    std::array<PDFReal, 6> matrix = { 1, 0, 0, 1, 0, 0 };
    const PDFObject& object = document.getObject(dictionary->get("Matrix"));
    if (object.isArray() && object.getArray()->getCount() == 6)
    {
        for (size_t i = 0; i < 6; ++i)
        {
            matrix[i] = getNumber(document.getObject(object.getArray()->getItem(i)));
        }
    }
    else if (!object.isNull())
    {
        matrix.fill(std::numeric_limits<PDFReal>::quiet_NaN());
    }
    return matrix;
}

QStringList ContentRoundTripTest::comparePatternCopies(const PDFDocument& original, const PDFObject& originalResources, const PDFDocument& written, const PDFObject& writtenResources)
{
    QStringList problems;

    const PDFDictionary* originalDictionary = original.getDictionaryFromObject(originalResources);
    const PDFDictionary* writtenDictionary = written.getDictionaryFromObject(writtenResources);
    const PDFDictionary* originalPatterns = originalDictionary ? original.getDictionaryFromObject(originalDictionary->get("Pattern")) : nullptr;
    const PDFDictionary* writtenPatterns = writtenDictionary ? written.getDictionaryFromObject(writtenDictionary->get("Pattern")) : nullptr;
    if (!writtenPatterns)
    {
        return problems;
    }

    // The keys of the data of a stream, which a copy may write anew
    const std::set<QByteArray> dataKeys = { "Length", "Filter", "DecodeParms", "DL", "Matrix" };

    for (size_t i = 0; i < writtenPatterns->getCount(); ++i)
    {
        const QByteArray name = writtenPatterns->getKey(i).getString();
        if (originalPatterns && originalPatterns->hasKey(name))
        {
            continue;
        }

        const PDFObject& copy = written.getObject(writtenPatterns->getValue(i));
        if (!copy.isStream())
        {
            problems << QStringLiteral("the added pattern /%1 is not a stream").arg(QString::fromLatin1(name));
            continue;
        }

        const PDFDictionary* copyDictionary = copy.getStream()->getDictionary();
        bool isDecoded = true;
        const QByteArray copyData = decodeStream(written, copy.getStream(), &isDecoded);

        // The copy of the identity touch: an original pattern with the same keys, the same cell
        // and the same /Matrix (Matrix x D, D = identity)
        bool isCopy = false;
        bool isMatrixChanged = false;
        for (size_t j = 0; originalPatterns && j < originalPatterns->getCount() && !isCopy; ++j)
        {
            const PDFObject& pattern = original.getObject(originalPatterns->getValue(j));
            if (!pattern.isStream() || decodeStream(original, pattern.getStream(), &isDecoded) != copyData)
            {
                continue;
            }

            const PDFDictionary* patternDictionary = pattern.getStream()->getDictionary();
            bool isSame = true;
            for (const PDFDictionary* dictionary : { patternDictionary, copyDictionary })
            {
                for (size_t k = 0; k < dictionary->getCount(); ++k)
                {
                    const QByteArray key = dictionary->getKey(k).getString();
                    if (!dataKeys.count(key) && patternDictionary->get(key) != copyDictionary->get(key))
                    {
                        isSame = false;
                    }
                }
            }

            if (!isSame)
            {
                continue;
            }

            if (getMatrix(original, patternDictionary) == getMatrix(written, copyDictionary))
            {
                isCopy = true;
            }
            else
            {
                isMatrixChanged = true;
            }
        }

        if (!isCopy)
        {
            problems << (isMatrixChanged ? QStringLiteral("the added pattern /%1 has another /Matrix than the pattern it copies").arg(QString::fromLatin1(name))
                                         : QStringLiteral("the added pattern /%1 is no copy of an original pattern").arg(QString::fromLatin1(name)));
        }
    }
    return problems;
}

QByteArray ContentRoundTripTest::getTokens(const QByteArray& content)
{
    QByteArray result;
    for (qsizetype i = 0; i < content.size(); ++i)
    {
        const char character = content[i];
        if ((character == '<' || character == '>') && i + 1 < content.size() && content[i + 1] == character)
        {
            result += ' ';
            result += character;
            result += character;
            result += ' ';
            ++i;
        }
        else if (character == '[' || character == ']')
        {
            result += ' ';
            result += character;
            result += ' ';
        }
        else if (character == '/')
        {
            result += ' ';
            result += character;
        }
        else
        {
            result += character;
        }
    }
    return result.simplified();
}

// -------------------------------------------------------------------------------------------------
//  Rendering
// -------------------------------------------------------------------------------------------------

QImage ContentRoundTripTest::renderPage(const PDFDocument& document, PDFInteger pageIndex)
{
    // The way of tst_contenteditortest.cpp: PDFRenderer with the default features into an image
    // filled white, the caches of the call only (the same settings for every rendering)
    const PDFPage* page = document.getCatalog()->getPage(size_t(pageIndex));
    const QSizeF pageSize = page ? page->getRotatedMediaBox().size() : QSizeF();
    const qreal longerSide = qMax(pageSize.width(), pageSize.height());
    const qreal scale = longerSide > 0 ? RENDER_SIZE / longerSide : 1.0;
    const QSize imageSize(qMax(1, qRound(pageSize.width() * scale)), qMax(1, qRound(pageSize.height() * scale)));

    PDFCMSGeneric cms;
    PDFFontCache fontCache(32, 32);
    PDFICCProfileCache iccProfileCache;
    PDFOptionalContentActivity activity(&document, OCUsage::View, nullptr);
    fontCache.setDocument(PDFModifiedDocument(const_cast<PDFDocument*>(&document), &activity));
    iccProfileCache.setDocument(PDFModifiedDocument(const_cast<PDFDocument*>(&document), &activity));

    PDFRenderer renderer(&document, &fontCache, &iccProfileCache, &cms, &activity, PDFRenderer::Features(), PDFMeshQualitySettings());

    QImage image(imageSize, QImage::Format_RGB888);
    image.fill(Qt::white);

    QPainter painter(&image);
    renderer.render(&painter, QRectF(QPointF(0, 0), QSizeF(imageSize)), size_t(pageIndex));
    painter.end();
    return image;
}

QString ContentRoundTripTest::describeImageDifference(const QImage& expected, const QImage& actual)
{
    if (expected.size() != actual.size())
    {
        return QStringLiteral("the size %1 x %2 instead of %3 x %4").arg(actual.width()).arg(actual.height()).arg(expected.width()).arg(expected.height());
    }

    int count = 0;
    int largest = 0;
    QRect box;
    for (int y = 0; y < expected.height(); ++y)
    {
        for (int x = 0; x < expected.width(); ++x)
        {
            const QRgb first = expected.pixel(x, y);
            const QRgb second = actual.pixel(x, y);
            if (first != second)
            {
                ++count;
                box = box.united(QRect(x, y, 1, 1));
                largest = qMax(largest, qMax(qAbs(qRed(first) - qRed(second)), qMax(qAbs(qGreen(first) - qGreen(second)), qAbs(qBlue(first) - qBlue(second)))));
            }
        }
    }

    // The box in points of the page (2 pixels a point, y up) helps to find the object
    return QStringLiteral("%1 pixels differ, the box of the pixels [x %2 ... %3, y %4 ... %5] of the image (y down), the largest difference of a channel %6")
            .arg(count).arg(box.left()).arg(box.right()).arg(box.top()).arg(box.bottom()).arg(largest);
}

void ContentRoundTripTest::compareRenderings(const QString& row, const char* checkName, const PDFDocument& original, const PDFDocument& written)
{
    const size_t pageCount = original.getCatalog()->getPageCount();
    check(written.getCatalog()->getPageCount() == pageCount, row, QStringLiteral("document"), checkName, QStringLiteral("the number of the pages changed"));

    for (size_t i = 0; i < pageCount && i < written.getCatalog()->getPageCount(); ++i)
    {
        const QString where = QStringLiteral("page %1").arg(i + 1);
        const QImage expected = renderPage(original, PDFInteger(i));

        // A rendering, which is not deterministic, would make every comparison meaningless
        const QImage again = renderPage(original, PDFInteger(i));
        check(expected == again, row, where, "rendering is deterministic", (expected == again) ? QString() : describeImageDifference(expected, again));

        // So would a rendering, which paints nothing: every page of the test documents with a
        // content paints something (the page 1 of the page tree has no content)
        if (isHandWrittenRow(row))
        {
            bool isReadable = false;
            const bool hasContent = !getJoinedPageContent(original, PDFInteger(i), &isReadable).trimmed().isEmpty();
            QImage blank(expected.size(), expected.format());
            blank.fill(Qt::white);
            check(!hasContent || expected != blank, row, where, "rendering paints", QStringLiteral("the original page renders blank"));
        }

        const QImage actual = renderPage(written, PDFInteger(i));
        check(expected == actual, row, where, checkName, (expected == actual) ? QString() : describeImageDifference(expected, actual));

        // For a look by a human: PDF4QT_ROUNDTRIP_PICTURES=<folder> keeps the original rendering
        // of every page, and the written one, where it differs
        const QString folder = qEnvironmentVariable("PDF4QT_ROUNDTRIP_PICTURES");
        if (!folder.isEmpty())
        {
            const QString name = QStringLiteral("%1/%2 %3 page %4").arg(folder, row, QString::fromLatin1(checkName).remove(QRegularExpression(QStringLiteral("[^a-z0-9 ]")))).arg(i + 1);
            expected.save(name + QStringLiteral(" original.png"));
            if (expected != actual)
            {
                actual.save(name + QStringLiteral(" written.png"));
            }
        }
    }
}

// -------------------------------------------------------------------------------------------------
//  Writing
// -------------------------------------------------------------------------------------------------

PDFContentEditSnapshot ContentRoundTripTest::createFullSnapshot(const ContentRoundTripLoaded& loaded)
{
    // A snapshot of a session holds only the changed containers; the gate adds the unchanged
    // ones by hand (pe0p05_state.md, PDFContentDocumentWriter::Options::forceRewrite)
    PDFContentEditSnapshot snapshot;
    snapshot.revision = 1;
    for (const Key& key : loaded.keys)
    {
        snapshot.containers[key] = loaded.session->getContainer(key);
    }
    return snapshot;
}

PDFDocumentToolContext ContentRoundTripTest::createToolContext(const PDFDocument& document)
{
    return PDFDocumentToolContext::create(PDFDocumentToolInputDocument::create(PDFDocumentPointer(new PDFDocument(document)), QByteArray(), QStringLiteral("Document.pdf"), true));
}

QVariantMap ContentRoundTripTest::createApplyParameters(const PDFContentEditSnapshotPointer& snapshot)
{
    QVariantMap parameters;
    parameters.insert(QString::fromLatin1(PDFDocumentToolContent::PARAMETER_MODE), QString::fromLatin1(PDFDocumentToolContent::MODE_APPLY));
    parameters.insert(QString::fromLatin1(PDFDocumentToolContent::PARAMETER_SESSION), QVariant::fromValue(snapshot));
    return parameters;
}

QString ContentRoundTripTest::describeErrors(const PDFContentWriteResult& result)
{
    QStringList texts;
    texts << QStringLiteral("status %1").arg(int(result.status));
    for (const auto& item : result.errors)
    {
        for (const PDFContentWriteError& error : item.second)
        {
            texts << QStringLiteral("page %1, %2, node %3: %4").arg(item.first + 1).arg(describeKey(error.container)).arg(error.nodeId.value).arg(error.text);
        }
    }
    for (const Key& key : result.changedContainers)
    {
        texts << QStringLiteral("changed in the meantime: %1").arg(describeKey(key));
    }
    return texts.join(QStringLiteral("; "));
}

void ContentRoundTripTest::checkOtherObjects(const QString& row, const char* checkName, const PDFDocument& original, const PDFDocument& written, const std::set<PDFObjectReference>& writtenObjects)
{
    // No other object of the document changed: the old content streams stay (another page may
    // share them), the writer only adds new objects. The document information dictionary is
    // the exception: every PDFDocumentBuilder::build writes /ModDate (the current time) into it.
    const PDFDictionary* trailer = original.getTrailerDictionary();
    const PDFObjectReference information = (trailer && trailer->get("Info").isReference()) ? trailer->get("Info").getReference() : PDFObjectReference();

    const PDFObjectStorage::PDFObjects& originalObjects = original.getStorage().getObjects();
    QStringList changedObjects;
    for (size_t i = 0; i < originalObjects.size(); ++i)
    {
        const PDFObjectReference reference(PDFInteger(i), originalObjects[i].generation);
        if (reference != information && !writtenObjects.count(reference) && written.getObjectByReference(reference) != originalObjects[i].object)
        {
            changedObjects << QStringLiteral("%1 %2 R").arg(reference.objectNumber).arg(reference.generation);
        }
    }
    check(changedObjects.isEmpty(), row, QStringLiteral("document"), checkName, QStringLiteral("changed: %1").arg(changedObjects.join(QStringLiteral(", "))));
}

void ContentRoundTripTest::printRefusals(const QString& row, const char* part, size_t accepted, const std::map<QString, int>& refusals)
{
    qInfo().noquote() << QStringLiteral("%1 / %2: %3 nodes touched").arg(row, QString::fromLatin1(part)).arg(accepted);
    for (const auto& item : refusals)
    {
        qInfo().noquote() << QStringLiteral("%1 / %2: refused %3x %4").arg(row, QString::fromLatin1(part)).arg(item.second).arg(item.first);
    }
}

// -------------------------------------------------------------------------------------------------
//  (a) Nothing changed - nothing written
// -------------------------------------------------------------------------------------------------

void ContentRoundTripTest::checkNothingWritten(const QString& row, const char* checkName, const ContentRoundTripLoaded& loaded, const PDFOperationControl* control)
{
    const PDFDocument* document = loaded.document.get();

    for (const Key& key : loaded.keys)
    {
        const PDFContentContainerPointer container = loaded.session->getContainer(key);
        check(container && container->isUnchanged(), row, describeKey(key), checkName, QStringLiteral("the container is not unchanged"));
    }

    // The session itself has nothing to write
    const PDFContentEditSnapshotPointer sessionSnapshot = loaded.session->createSnapshot();
    check(sessionSnapshot->isEmpty(), row, QStringLiteral("document"), checkName, QStringLiteral("the snapshot of the session is not empty (%1 containers)").arg(sessionSnapshot->containers.size()));

    // Every loaded container by hand, the default options: not one is written
    PDFDocumentBuilder builder(document);
    const PDFContentWriteResult result = Writer::write(document, createFullSnapshot(loaded), &builder, Writer::Options(), control, nullptr);
    check(result.status == Status::Success && result.errors.empty(), row, QStringLiteral("document"), checkName, describeErrors(result));
    check(result.pagesChanged.empty(), row, QStringLiteral("document"), checkName, QStringLiteral("%1 pages changed").arg(result.pagesChanged.size()));
    check(result.objectsAdded == 0 && result.objectsEdited == 0 && result.objectsRemoved == 0 && result.fontsAdded == 0 && !result.isStructureChanged, row, QStringLiteral("document"), checkName,
          QStringLiteral("facts %1 added, %2 edited, %3 removed, %4 fonts").arg(result.objectsAdded).arg(result.objectsEdited).arg(result.objectsRemoved).arg(result.fontsAdded));
    check(*builder.getStorage() == document->getStorage(), row, QStringLiteral("document"), checkName, QStringLiteral("the builder holds other objects than the document"));
}

void ContentRoundTripTest::runUnchanged(const QString& row, const PDFDocument& document, const PDFOperationControl* control)
{
    ContentRoundTripLoaded loaded;
    loadDocument(document, control, &loaded);

    // Every page is a container (also a page, which cannot be read: a read-only one)
    const size_t pageCount = document.getCatalog()->getPageCount();
    for (size_t i = 0; i < pageCount; ++i)
    {
        check(loaded.originals.count(loaded.session->getPageKey(PDFInteger(i))) > 0, row, QStringLiteral("page %1").arg(i + 1), "(a) the page is loaded", loaded.failures.join(QStringLiteral("; ")));
    }
    qInfo().noquote() << QStringLiteral("%1 / (a): %2 containers loaded, not loaded: %3").arg(row).arg(loaded.keys.size()).arg(loaded.failures.isEmpty() ? QStringLiteral("none") : loaded.failures.join(QStringLiteral("; ")));

    checkNothingWritten(row, "(a) nothing written", loaded, control);

    // The tool: the snapshot of the session (empty) and the snapshot of every unchanged
    // container are "nothing to do", no document is produced
    PDFDocumentToolContent tool;
    const PDFDocumentToolContext context = createToolContext(*loaded.document);
    const std::vector<std::pair<QString, PDFContentEditSnapshotPointer>> snapshots = { { QStringLiteral("the snapshot of the session"), loaded.session->createSnapshot() },
                                                                                       { QStringLiteral("a snapshot of every container"), std::make_shared<const PDFContentEditSnapshot>(createFullSnapshot(loaded)) } };
    for (const auto& item : snapshots)
    {
        const PDFDocumentToolResult toolResult = tool.run(context, createApplyParameters(item.second), control, nullptr);
        check(toolResult.status == PDFDocumentToolResultStatus::NothingToDo && toolResult.documents.empty(), row, QStringLiteral("document"), "(a) the tool has nothing to do",
              QStringLiteral("%1: status %2, %3 documents, %4").arg(item.first).arg(int(toolResult.status)).arg(toolResult.documents.size()).arg(toolResult.message));
    }
}

// -------------------------------------------------------------------------------------------------
//  (b) Forced rewrite - the same bytes
// -------------------------------------------------------------------------------------------------

void ContentRoundTripTest::runForcedRewrite(const QString& row, const PDFDocument& document, const PDFOperationControl* control)
{
    ContentRoundTripLoaded loaded;
    loadDocument(document, control, &loaded);
    const PDFDocument& original = *loaded.document;

    Writer::Options options;
    options.forceRewrite = true;

    PDFDocumentBuilder builder(&original);
    const PDFContentWriteResult result = Writer::write(&original, createFullSnapshot(loaded), &builder, options, control, nullptr);
    check(result.status == Status::Success && result.errors.empty(), row, QStringLiteral("document"), "(b) status", describeErrors(result));
    check(result.objectsAdded == 0 && result.objectsEdited == 0 && result.objectsRemoved == 0 && result.fontsAdded == 0 && !result.isStructureChanged, row, QStringLiteral("document"), "(b) facts",
          QStringLiteral("%1 added, %2 edited, %3 removed, %4 fonts").arg(result.objectsAdded).arg(result.objectsEdited).arg(result.objectsRemoved).arg(result.fontsAdded));

    // Every page with a container, which can be written, or reaching one, is changed
    const size_t pageCount = original.getCatalog()->getPageCount();
    std::set<PDFInteger> expectedPages;
    for (size_t i = 0; i < pageCount; ++i)
    {
        for (const Key& key : getReachedKeys(loaded, loaded.session->getPageKey(PDFInteger(i))))
        {
            auto it = loaded.originals.find(key);
            if (it != loaded.originals.end() && it->second && !it->second->isReadOnly())
            {
                expectedPages.insert(PDFInteger(i));
            }
        }
    }
    const std::set<PDFInteger> pagesChanged(result.pagesChanged.cbegin(), result.pagesChanged.cend());
    check(std::includes(pagesChanged.cbegin(), pagesChanged.cend(), expectedPages.cbegin(), expectedPages.cend()) && (pagesChanged.empty() || (*pagesChanged.begin() >= 0 && *pagesChanged.rbegin() < PDFInteger(pageCount))),
          row, QStringLiteral("document"), "(b) changed pages", QStringLiteral("%1 pages changed, %2 expected at least").arg(pagesChanged.size()).arg(expectedPages.size()));

    const PDFDocument written = builder.build();

    // The data keys of a stream, which the writer writes anew (pe0p05_state.md)
    const std::set<QByteArray> dataKeys = { "Length", "Filter", "DecodeParms", "F", "FFilter", "FDecodeParms", "DL" };

    for (const Key& key : loaded.keys)
    {
        const PDFContentContainerPointer container = loaded.originals.at(key);
        const QString where = describeKey(key);

        if (key.kind == Key::Kind::Page)
        {
            const PDFObjectReference pageReference = key.reference;
            if (container->isReadOnly())
            {
                check(written.getObjectByReference(pageReference) == original.getObjectByReference(pageReference), row, where, "(b) a read-only page is not written", QStringLiteral("the page object changed"));
                continue;
            }

            // The expected bytes from the document: the streams of /Contents joined by one line feed
            bool isReadable = false;
            const QByteArray expected = getJoinedPageContent(original, key.pageIndex, &isReadable);
            check(isReadable, row, where, "(b) the page can be read", QStringLiteral("a page, which cannot be read, is not read-only"));
            check(container->getOperations().getOriginalContent() == expected, row, where, "(b) original content", QStringLiteral("the operations of the container are not the joined streams"));

            const PDFDictionary* pageDictionary = written.getDictionaryFromObject(written.getObjectByReference(pageReference));
            const PDFObject contents = pageDictionary ? pageDictionary->get("Contents") : PDFObject();
            const PDFObject& stream = written.getObject(contents);
            const bool isOneStream = contents.isReference() && stream.isStream();
            check(isOneStream, row, where, "(b) one content stream", QStringLiteral("/Contents is not one reference to a stream"));
            if (!isOneStream)
            {
                continue;
            }

            check(stream.getStream()->getDictionary()->get("Filter") == PDFObject::createName("FlateDecode"), row, where, "(b) Flate stream", QStringLiteral("the stream is not compressed by /FlateDecode"));
            bool isDecoded = true;
            const QByteArray content = decodeStream(written, stream.getStream(), &isDecoded);
            check(content == expected, row, where, "(b) content bytes",
                  QStringLiteral("%1 bytes written, %2 expected; written: %3").arg(content.size()).arg(expected.size()).arg(QString::fromLatin1(content.left(300))));
        }
        else
        {
            const PDFObject& originalObject = original.getObjectByReference(key.reference);
            const PDFObject& writtenObject = written.getObjectByReference(key.reference);
            if (container->isReadOnly())
            {
                check(writtenObject == originalObject, row, where, "(b) a read-only container is not written", QStringLiteral("the object changed"));
                continue;
            }

            const bool isStream = writtenObject.isStream() && originalObject.isStream();
            check(isStream, row, where, "(b) the object is a stream", QString());
            if (!isStream)
            {
                continue;
            }

            bool isDecoded = true;
            const QByteArray expected = decodeStream(original, originalObject.getStream(), &isDecoded);
            const QByteArray content = decodeStream(written, writtenObject.getStream(), &isDecoded);
            check(isDecoded, row, where, "(b) the streams can be decoded", QString());
            check(container->getOperations().getOriginalContent() == expected, row, where, "(b) original content", QStringLiteral("the operations of the container are not its stream"));
            check(content == expected, row, where, "(b) content bytes",
                  QStringLiteral("%1 bytes written, %2 expected; written: %3").arg(content.size()).arg(expected.size()).arg(QString::fromLatin1(content.left(300))));

            // Every key of the dictionary of the stream, except the data keys, stays
            const PDFDictionary* originalDictionary = originalObject.getStream()->getDictionary();
            const PDFDictionary* writtenDictionary = writtenObject.getStream()->getDictionary();
            QStringList changedKeys;
            for (const PDFDictionary* dictionary : { originalDictionary, writtenDictionary })
            {
                for (size_t i = 0; i < dictionary->getCount(); ++i)
                {
                    const QByteArray dictionaryKey = dictionary->getKey(i).getString();
                    if (!dataKeys.count(dictionaryKey) && dictionaryKey != "Resources" && originalDictionary->get(dictionaryKey) != writtenDictionary->get(dictionaryKey))
                    {
                        changedKeys << QString::fromLatin1(dictionaryKey);
                    }
                }
            }
            check(changedKeys.isEmpty(), row, where, "(b) keys of the stream", changedKeys.join(QStringLiteral(", ")));
        }

        // The resources (a direct copy, R-4): every original entry stays
        const QStringList problems = compareResources(original, getContainerResources(original, key), written, getContainerResources(written, key));
        check(problems.isEmpty(), row, where, "(b) resources", problems.join(QStringLiteral("; ")));
    }

    std::set<PDFObjectReference> writtenObjects;
    for (const Key& key : loaded.keys)
    {
        if (!loaded.originals.at(key)->isReadOnly())
        {
            writtenObjects.insert(key.reference);
        }
    }
    checkOtherObjects(row, "(b) other objects unchanged", original, written, writtenObjects);

    compareRenderings(row, "(b) rendering", original, written);
}

// -------------------------------------------------------------------------------------------------
//  (c) Touch - the same rendering
// -------------------------------------------------------------------------------------------------

void ContentRoundTripTest::applyWraps(ContentRoundTripLoaded* loaded, std::map<Key, std::vector<PDFContentNodeId>>* wrapped, std::map<QString, int>* refusals)
{
    PDFContentEditSession* session = loaded->session.get();
    for (const Key& key : loaded->keys)
    {
        // One undo step per container (the limit of the undo stack counts steps)
        session->getUndoStack()->beginMacro(QStringLiteral("Touch"));
        for (const PDFContentNodePointer& node : getNodesInPreorder(*loaded->originals.at(key)))
        {
            const Kind kind = node->getKind();
            if (kind == Kind::Root || kind == Kind::Group || kind == Kind::MarkedContent)
            {
                continue;
            }

            QString errorText;
            if (session->execute(Factory::createTransform({ PDFContentNodeReference{ key, node->getId() } }, QTransform()), &errorText))
            {
                (*wrapped)[key].push_back(node->getId());
            }
            else
            {
                ++(*refusals)[getKindName(kind) + QStringLiteral(": ") + errorText];
            }
        }
        session->getUndoStack()->endMacro();
    }
}

void ContentRoundTripTest::runTouchWrap(const QString& row, const PDFDocument& document, const PDFOperationControl* control)
{
    ContentRoundTripLoaded loaded;
    loadDocument(document, control, &loaded);
    const PDFDocument& original = *loaded.document;

    std::map<Key, std::vector<PDFContentNodeId>> wrapped;
    std::map<QString, int> refusals;
    applyWraps(&loaded, &wrapped, &refusals);

    size_t accepted = 0;
    for (const auto& item : wrapped)
    {
        accepted += item.second.size();
    }
    printRefusals(row, "(c1) wrap", accepted, refusals);

    const PDFContentEditSnapshotPointer snapshot = loaded.session->createSnapshot();
    PDFDocumentBuilder builder(&original);
    const PDFContentWriteResult result = Writer::write(&original, *snapshot, &builder, Writer::Options(), control, nullptr);
    check(result.status == Status::Success && result.errors.empty(), row, QStringLiteral("document"), "(c1) status", describeErrors(result));
    const PDFDocument written = builder.build();

    std::set<PDFObjectReference> writtenObjects;
    for (const auto& item : snapshot->containers)
    {
        writtenObjects.insert(item.first.reference);
    }
    checkOtherObjects(row, "(c1) other objects unchanged", original, written, writtenObjects);

    // ---- The bytes and values, to which the renderer is blind ----
    for (const auto& item : snapshot->containers)
    {
        const Key& key = item.first;
        const QString where = describeKey(key);
        const QByteArray content = getContainerContent(written, key);

        // The document writer writes the bytes of the stream writer
        const PDFContentStreamWriteResult streamResult = PDFContentStreamWriter::write(*item.second);
        check(streamResult.isSuccess() && content == streamResult.content, row, where, "(c1) written bytes", QStringLiteral("the content of the document is not the content of the stream writer"));

        // Every wrapped object is written byte for byte inside of its wrap (the text operators
        // with the widths, Tw and Tr of their fonts; the operands of an image or a form)
        const PDFContentContainer& originalContainer = *loaded.originals.at(key);
        auto itWrapped = wrapped.find(key);
        for (const PDFContentNodeId id : (itWrapped != wrapped.end()) ? itWrapped->second : std::vector<PDFContentNodeId>())
        {
            const PDFContentNodePointer node = originalContainer.getNode(id);
            if (!node || node->hasClipEffect())
            {
                // A node with a clip is rewritten canonically in place (R-1), never wrapped
                continue;
            }

            std::vector<size_t> indices;
            for (const PDFContentNodePointer& subtreeNode : getSubtreeInPreorder(node))
            {
                for (const PDFContentOperationSpan& span : { subtreeNode->getPrefix(), subtreeNode->getSuffix() })
                {
                    for (size_t i = 0; i < span.count; ++i)
                    {
                        indices.push_back(span.first + i);
                    }
                }
            }
            std::sort(indices.begin(), indices.end());

            QByteArray bytes;
            for (const size_t index : indices)
            {
                bytes += originalContainer.getOperations().getOperation(index).getRawBytes();
            }
            check(!bytes.isEmpty() && content.contains(bytes), row, QStringLiteral("%1 node %2").arg(where).arg(id.value), "(c1) bytes of a wrapped object",
                  QStringLiteral("the bytes \"%1\" of the %2 are not in the written content").arg(QString::fromLatin1(bytes.left(200)), getKindName(node->getKind())));
        }

        // Every original resource stays (DefaultRGB, the names of opaque operations); a pattern
        // added for a wrapped object is an exact copy with the same /Matrix (R-5, D = identity)
        const PDFObject originalResources = getContainerResources(original, key);
        const PDFObject writtenResources = getContainerResources(written, key);
        const QStringList resourceProblems = compareResources(original, originalResources, written, writtenResources);
        check(resourceProblems.isEmpty(), row, where, "(c1) resources", resourceProblems.join(QStringLiteral("; ")));
        const QStringList patternProblems = comparePatternCopies(original, originalResources, written, writtenResources);
        check(patternProblems.isEmpty(), row, where, "(c1) pattern copies", patternProblems.join(QStringLiteral("; ")));

        // The written container builds again: the same objects (paths by what they do) and
        // issues, one q more for every wrap - a leaf without a clip effect, and a path, which
        // paints and clips, when its override sets a property (it is split then; a wrap with
        // nothing to set is not written at all, pe0p01_state.md) - and no cm except the two of a
        // soft mask set again in its own space, X cm ... X^-1 cm with X = CTM_e x CTM^-1 (R-6;
        // the identity touch writes no cm of its own)
        const PDFContentBuildResult rebuilt = buildContainer(written, key, loaded.pageIndices.at(key));
        check(rebuilt.container != nullptr, row, where, "(c1) the written content builds again", QString());
        if (rebuilt.container)
        {
            int expectedWraps = 0;
            int expectedMatrices = 0;
            for (const PDFContentNodeId id : (itWrapped != wrapped.end()) ? itWrapped->second : std::vector<PDFContentNodeId>())
            {
                const PDFContentNodePointer originalNode = originalContainer.getNode(id);
                const PDFContentNodePointer touchedNode = item.second->getNode(id);
                const PDFContentStateOverride* override = touchedNode ? touchedNode->getOverride() : nullptr;
                const bool paints = originalNode && (originalNode->getData().paint.stroke || originalNode->getData().paint.fill);
                const bool hasProperties = override && !override->properties.empty();
                const bool isWrapped = originalNode && (!originalNode->hasClipEffect() || (paints && hasProperties));
                expectedWraps += isWrapped ? 1 : 0;

                const PDFContentStateValue* mask = nullptr;
                if (hasProperties)
                {
                    auto itMask = override->properties.find(PDFContentStateProperty::SoftMask);
                    mask = (itMask != override->properties.end()) ? &itMask->second : nullptr;
                }

                if (isWrapped && mask && mask->kind == PDFContentStateValue::Kind::Object && !mask->object.isNull() && !(mask->object.isName() && mask->object.getString() == "None"))
                {
                    const QTransform matrix = touchedNode->getState() ? touchedNode->getState()->getCurrentTransformationMatrix() : QTransform();
                    const QTransform x = mask->matrix * (override->transform * matrix).inverted();
                    const bool isIdentity = qAbs(x.m11() - 1.0) < 1e-9 && qAbs(x.m12()) < 1e-9 && qAbs(x.m21()) < 1e-9 && qAbs(x.m22() - 1.0) < 1e-9 && qAbs(x.dx()) < 1e-9 && qAbs(x.dy()) < 1e-9;
                    expectedMatrices += isIdentity ? 0 : 2;
                }
            }

            ContentRoundTripCensus before;
            ContentRoundTripCensus after;
            addToCensus(originalContainer, &before);
            addToCensus(*rebuilt.container, &after);
            before.saveOperations += expectedWraps;
            before.matrixOperations += expectedMatrices;
            check(before == after, row, where, "(c1) objects, wraps and issues", QStringLiteral("%1 instead of %2").arg(describeCensus(after), describeCensus(before)));
        }
    }

    // The soft mask document (the renderer of the gate does not paint soft masks): the red
    // rectangle under /GS1 is wrapped with the mask set again by an ExtGState of the single key
    // /SMask (R-6). The mask was set with the identity as the CTM and the rectangle is drawn
    // under the identity, so X = identity and no cm is written. /GS1 is << /Type /ExtGState
    // /SMask ... >>: it sets the same keys to the same values as << /SMask ... >> (/Type plays
    // no role, ISO 32000-2 Table 57), so the writer names it again (pe0p01_state.md: an equal
    // ExtGState dictionary reuses its name) and adds no entry. The blue rectangle has no mask:
    // an empty wrap.
    if (row == QStringLiteral("soft mask"))
    {
        const QByteArray tokens = getTokens(getContainerContent(written, loaded.session->getPageKey(0)));
        const QByteArray expected("q /GS1 gs 1 0 0 rg q /GS1 gs 0 0 200 100 re f Q Q q /GS2 gs 0 0 1 rg 0 0 1 RG q 0 100 200 100 re B Q Q");
        check(tokens == expected, row, QStringLiteral("page 1"), "(c1) soft mask written by hand", QString::fromLatin1(tokens));

        const PDFDictionary* writtenResources = written.getDictionaryFromObject(getContainerResources(written, loaded.session->getPageKey(0)));
        const PDFDictionary* writtenStates = writtenResources ? written.getDictionaryFromObject(writtenResources->get("ExtGState")) : nullptr;
        check(writtenStates && writtenStates->getCount() == 2, row, QStringLiteral("page 1"), "(c1) soft mask written by hand", QStringLiteral("an ExtGState was added beside /GS1 and /GS2"));
    }

    // The image under a soft mask set in another space: the mask was set under the identity
    // (CTM_e = I), the image is drawn under CTM = [100 0 0 50 50 60], so the mask is set again
    // in its own space by X = CTM_e x CTM^-1 = [0.01 0 0 0.02 -0.5 -1.2] (zadani_jadro.md 4.4
    // R-6: X cm, gs, X^-1 cm), and X^-1 = CTM. /GS1 is named again (an equal ExtGState).
    if (row == QStringLiteral("soft mask image"))
    {
        const QByteArray tokens = getTokens(getContainerContent(written, loaded.session->getPageKey(0)));
        const QByteArray expected("/GS1 gs q 100 0 0 50 50 60 cm q 0.01 0 0 0.02 -0.5 -1.2 cm /GS1 gs 100 0 0 50 50 60 cm /Im1 Do Q Q");
        check(tokens == expected, row, QStringLiteral("page 1"), "(c1) soft mask written by hand", QString::fromLatin1(tokens));
    }

    // ---- The structure: the written content builds again with the same issues and objects,
    //      and an identity touch adds no cm ----
    // (per page: the page and the forms it reaches, the wraps and the soft masks aside - the
    // written containers have them counted above)
    ContentRoundTripLoaded readBack;
    loadDocument(written, control, &readBack);
    for (size_t i = 0; i < original.getCatalog()->getPageCount(); ++i)
    {
        ContentRoundTripCensus before = getCensus(loaded, PDFInteger(i));
        ContentRoundTripCensus after = getCensus(readBack, PDFInteger(i));
        before.saveOperations = after.saveOperations = 0;
        before.matrixOperations = after.matrixOperations = 0;
        check(before == after, row, QStringLiteral("page %1").arg(i + 1), "(c1) objects and issues", QStringLiteral("%1 instead of %2").arg(describeCensus(after), describeCensus(before)));
    }

    compareRenderings(row, "(c1) rendering", original, written);
}

void ContentRoundTripTest::runTouchMove(const QString& row, const PDFDocument& document, const PDFOperationControl* control)
{
    ContentRoundTripLoaded loaded;
    loadDocument(document, control, &loaded);
    const PDFDocument& original = *loaded.document;
    PDFContentEditSession* session = loaded.session.get();

    // A node is moved away by one command and back by another. A place, from which the way back
    // is refused, is left by undo and the next place is tried: in a form or a pattern cell a node
    // moved in front of the state it inherits cannot be moved behind content, which sets that
    // state, again (the inherited value is unknown - P03, a refusal by design). The refusals
    // are counted by the kind of the node and the reason.
    std::map<QString, int> refusals;
    size_t moved = 0;
    for (const Key& key : loaded.keys)
    {
        for (const PDFContentNodePointer& originalNode : getNodesInPreorder(*loaded.originals.at(key)))
        {
            const PDFContentNodeId id = originalNode->getId();
            const PDFContentContainerPointer container = session->getContainer(key);
            const PDFContentNodePointer node = container->getNode(id);
            const PDFContentNodePointer parent = container->getParent(id);
            if (!node || !parent)
            {
                // The root
                continue;
            }

            const PDFContentNodeId parentId = parent->getId();
            const PDFInteger index = container->getIndexInParent(id);
            const PDFContentNodeId previousId = index > 0 ? parent->getChildren()[size_t(index - 1)]->getId() : PDFContentNodeId();

            // Another place: the beginning of the parent, its end, or in front of the parent
            // (only out of a group: a move out of a marked content changes its sequence)
            std::vector<std::pair<PDFContentNodeId, PDFInteger>> places;
            if (index > 0)
            {
                places.emplace_back(parentId, 0);
            }
            places.emplace_back(parentId, -1);
            const PDFContentNodePointer grandparent = container->getParent(parentId);
            if (grandparent && parent->getKind() == Kind::Group)
            {
                places.emplace_back(grandparent->getId(), container->getIndexInParent(parentId));
            }

            QString awayError;
            QString backError;
            bool isMoved = false;
            for (const auto& place : places)
            {
                QString errorText;
                if (!session->execute(Factory::createReorder({ PDFContentNodeReference{ key, id } }, PDFContentNodeReference{ key, place.first }, place.second, Factory::ReorderMode::FollowParent), &errorText))
                {
                    if (awayError.isEmpty())
                    {
                        awayError = errorText;
                    }
                    continue;
                }

                // Back to its own place: behind the node, which was in front of it (the one,
                // which stands for it below the parent now - a move may give a node a new
                // group), or the first
                const PDFContentContainerPointer current = session->getContainer(key);
                PDFInteger backIndex = 0;
                if (previousId.isValid())
                {
                    PDFContentNodeId topId = previousId;
                    PDFContentNodePointer topParent = current->getParent(topId);
                    while (topParent && topParent->getId() != parentId)
                    {
                        topId = topParent->getId();
                        topParent = current->getParent(topId);
                    }
                    backIndex = topParent ? current->getIndexInParent(topId) + 1 : 0;
                }

                if (session->execute(Factory::createReorder({ PDFContentNodeReference{ key, id } }, PDFContentNodeReference{ key, parentId }, backIndex, Factory::ReorderMode::FollowParent), &errorText))
                {
                    isMoved = true;
                    break;
                }

                if (backError.isEmpty())
                {
                    backError = errorText;
                }

                // The move away is undone: the container is the version before it again
                const std::optional<PDFInteger> undone = session->undo();
                check(undone.has_value() && session->getContainer(key) == container, row, QStringLiteral("%1 node %2").arg(describeKey(key)).arg(id.value), "(c2) undo of a move away", QString());
            }

            if (isMoved)
            {
                ++moved;
            }
            else
            {
                ++refusals[getKindName(node->getKind()) + (backError.isEmpty() ? QStringLiteral(": ") + awayError : QStringLiteral(": moved away, not back: ") + backError)];
            }
        }
    }
    printRefusals(row, "(c2) move away and back", moved, refusals);

    const PDFContentEditSnapshotPointer snapshot = session->createSnapshot();
    PDFDocumentBuilder builder(&original);
    const PDFContentWriteResult result = Writer::write(&original, *snapshot, &builder, Writer::Options(), control, nullptr);
    check(result.status == Status::Success && result.errors.empty(), row, QStringLiteral("document"), "(c2) status", describeErrors(result));
    const PDFDocument written = builder.build();

    // A move inside of one parent changes no structure element (the objects of the structure
    // stay with the others)
    std::set<PDFObjectReference> writtenObjects;
    for (const auto& item : snapshot->containers)
    {
        writtenObjects.insert(item.first.reference);
    }
    check(snapshot->structure == nullptr, row, QStringLiteral("document"), "(c2) structure unchanged", QStringLiteral("the snapshot has a changed structure"));
    checkOtherObjects(row, "(c2) other objects unchanged", original, written, writtenObjects);

    // Do the bytes return to the original? They need not (residual state runs, wraps of R-2):
    // the output tells it for the report
    int sameBytes = 0;
    for (const auto& item : snapshot->containers)
    {
        if (getContainerContent(written, item.first) == item.second->getOperations().getOriginalContent())
        {
            ++sameBytes;
        }

        // The written container builds again with the same objects and issues (a bake of R-2
        // may add q and cm)
        const QString where = describeKey(item.first);
        const PDFContentBuildResult rebuilt = buildContainer(written, item.first, loaded.pageIndices.at(item.first));
        check(rebuilt.container != nullptr, row, where, "(c2) the written content builds again", QString());
        if (rebuilt.container)
        {
            ContentRoundTripCensus before;
            ContentRoundTripCensus after;
            addToCensus(*loaded.originals.at(item.first), &before);
            addToCensus(*rebuilt.container, &after);
            before.matrixOperations = after.matrixOperations = 0;
            before.saveOperations = after.saveOperations = 0;
            check(before == after, row, where, "(c2) objects and issues", QStringLiteral("%1 instead of %2").arg(describeCensus(after), describeCensus(before)));
        }
    }
    qInfo().noquote() << QStringLiteral("%1 / (c2) move away and back: %2 of %3 written containers have their original bytes").arg(row).arg(sameBytes).arg(snapshot->containers.size());

    // The written content builds again with the same issues and objects (a bake of R-2 may add cm)
    ContentRoundTripLoaded readBack;
    loadDocument(written, control, &readBack);
    for (size_t i = 0; i < original.getCatalog()->getPageCount(); ++i)
    {
        ContentRoundTripCensus before = getCensus(loaded, PDFInteger(i));
        ContentRoundTripCensus after = getCensus(readBack, PDFInteger(i));
        before.matrixOperations = after.matrixOperations = 0;
        before.saveOperations = after.saveOperations = 0;
        check(before == after, row, QStringLiteral("page %1").arg(i + 1), "(c2) objects and issues", QStringLiteral("%1 instead of %2").arg(describeCensus(after), describeCensus(before)));
    }

    compareRenderings(row, "(c2) rendering", original, written);
}

void ContentRoundTripTest::runTouchUndo(const QString& row, const PDFDocument& document, const PDFOperationControl* control)
{
    ContentRoundTripLoaded loaded;
    loadDocument(document, control, &loaded);
    PDFContentEditSession* session = loaded.session.get();

    std::map<Key, std::vector<PDFContentNodeId>> wrapped;
    std::map<QString, int> refusals;
    applyWraps(&loaded, &wrapped, &refusals);

    // The written bytes of touch 1
    const PDFContentEditSnapshotPointer touched = session->createSnapshot();
    std::map<Key, QByteArray> bytesBefore;
    for (const auto& item : touched->containers)
    {
        bytesBefore[item.first] = PDFContentStreamWriter::write(*item.second).content;
    }

    // Undo back to the start: every container is the loaded object again, (a) holds again
    size_t undoCount = 0;
    while (session->undo().has_value())
    {
        ++undoCount;
    }
    check(undoCount == session->getUndoStack()->getCount(), row, QStringLiteral("document"), "(c3) undo", QStringLiteral("%1 steps undone of %2").arg(undoCount).arg(session->getUndoStack()->getCount()));
    for (const Key& key : loaded.keys)
    {
        check(session->getContainer(key) == loaded.originals.at(key), row, describeKey(key), "(c3) undo gives the loaded container", QStringLiteral("another object"));
    }
    checkNothingWritten(row, "(c3) nothing written after undo", loaded, control);

    // Redo: the same written bytes as before the undo
    while (session->redo().has_value())
    {
    }
    const PDFContentEditSnapshotPointer redone = session->createSnapshot();
    check(redone->containers.size() == touched->containers.size(), row, QStringLiteral("document"), "(c3) redo", QStringLiteral("%1 changed containers instead of %2").arg(redone->containers.size()).arg(touched->containers.size()));
    for (const auto& item : redone->containers)
    {
        auto it = bytesBefore.find(item.first);
        check(it != bytesBefore.end() && PDFContentStreamWriter::write(*item.second).content == it->second, row, describeKey(item.first), "(c3) redo gives the same bytes", QStringLiteral("other bytes"));
    }
}

// -------------------------------------------------------------------------------------------------
//  The test functions
// -------------------------------------------------------------------------------------------------

void ContentRoundTripTest::unchangedIsNotWritten_data()
{
    addDocumentRows();
}

void ContentRoundTripTest::unchangedIsNotWritten()
{
    QFETCH(QString, document);
    runUnchanged(document, createTestDocument(document), nullptr);
}

void ContentRoundTripTest::forcedRewriteKeepsBytes_data()
{
    addDocumentRows();
}

void ContentRoundTripTest::forcedRewriteKeepsBytes()
{
    QFETCH(QString, document);
    runForcedRewrite(document, createTestDocument(document), nullptr);
}

void ContentRoundTripTest::touchWrapKeepsRendering_data()
{
    addDocumentRows();
}

void ContentRoundTripTest::touchWrapKeepsRendering()
{
    QFETCH(QString, document);
    runTouchWrap(document, createTestDocument(document), nullptr);
}

void ContentRoundTripTest::touchMoveKeepsRendering_data()
{
    addDocumentRows();
}

void ContentRoundTripTest::touchMoveKeepsRendering()
{
    QFETCH(QString, document);
    runTouchMove(document, createTestDocument(document), nullptr);
}

void ContentRoundTripTest::touchUndoRestoresModel_data()
{
    addDocumentRows();
}

void ContentRoundTripTest::touchUndoRestoresModel()
{
    QFETCH(QString, document);
    runTouchUndo(document, createTestDocument(document), nullptr);
}

void ContentRoundTripTest::corpus_data()
{
    QTest::addColumn<QString>("path");

    const QString listFile = qEnvironmentVariable("PDF4QT_ROUNDTRIP_CORPUS");
    if (listFile.isEmpty())
    {
        QSKIP("The round trip of real files is asked for by PDF4QT_ROUNDTRIP_CORPUS=<a list file with one path of a PDF file a line, at most 20>.");
    }

    QFile file(listFile);
    QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(QStringLiteral("The list of the corpus '%1' cannot be read.").arg(listFile)));

    QStringList paths;
    while (!file.atEnd())
    {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (!line.isEmpty() && !line.startsWith(QLatin1Char('#')))
        {
            paths << line;
        }
    }
    QVERIFY2(paths.size() <= CORPUS_MAXIMUM_FILE_COUNT, qPrintable(QStringLiteral("The list of the corpus has %1 files, at most %2 are allowed (the whole corpus only on the word of the owner).").arg(paths.size()).arg(CORPUS_MAXIMUM_FILE_COUNT)));

    for (int i = 0; i < paths.size(); ++i)
    {
        QTest::newRow(qPrintable(QStringLiteral("%1 %2").arg(i + 1).arg(QFileInfo(paths[i]).fileName()))) << paths[i];
    }
}

void ContentRoundTripTest::corpus()
{
    QFETCH(QString, path);

    // Only the empty password: an encrypted file is skipped
    PDFDocumentReader reader(nullptr, [](bool* ok) { *ok = false; return QString(); }, true, false);
    const PDFDocument document = reader.readFromFile(path);
    if (reader.getReadingResult() != PDFDocumentReader::Result::OK)
    {
        QSKIP(qPrintable(QStringLiteral("The file '%1' cannot be opened: %2").arg(path, reader.getErrorMessage())));
    }

    const PDFSecurityHandler* securityHandler = document.getStorage().getSecurityHandler();
    if (securityHandler && securityHandler->getMode() != EncryptionMode::None)
    {
        QSKIP(qPrintable(QStringLiteral("The file '%1' is encrypted.").arg(path)));
    }

    const int factor = qMax(1, qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR"));
    const ContentRoundTripDeadline deadline(qint64(CORPUS_FILE_TIME_LIMIT_SECONDS) * 1000 * factor);

    const QString row = QFileInfo(path).fileName();
    runUnchanged(row, document, &deadline);
    runForcedRewrite(row, document, &deadline);
    runTouchWrap(row, document, &deadline);
    runTouchMove(row, document, &deadline);
    runTouchUndo(row, document, &deadline);

    qInfo().noquote() << QStringLiteral("%1: %2 ms").arg(row).arg(deadline.getElapsed());
    QVERIFY2(deadline.getElapsed() <= deadline.getLimit(), qPrintable(QStringLiteral("%1: the time limit of %2 s was exceeded").arg(row).arg(deadline.getLimit() / 1000)));
}

QTEST_MAIN(ContentRoundTripTest)

#include "tst_contentroundtriptest.moc"
