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

// The hooks and getters of PDFPageContentProcessor, which the content tree of Edit Content is
// built on (stage E0): the byte range of the current operation (also across the streams of
// /Contents, ISO 32000-2, 7.8.2), the decoded stream data, the inline image as one instruction
// (8.9.7), performOperatorFailed, the hooks around a glyph procedure of a Type 3 font and the
// limit of their nesting (9.6.4), the character code of the items of a text sequence (9.4.3),
// and the two corrections of the text displacement: the word spacing belongs to the single-byte
// character code 32 only (9.3.3) and a Type 3 font gets the character and the word spacing (9.4.4).
// The test documents are those of contenttestdocuments.h, which this program also checks.
//
// Three more rules of the processor (2026-11-25): a glyph procedure of a Type 3 font is a
// content stream of its own - its text objects are not those of the page (9.6.4); a character
// code with a width and without a glyph moves the text as any other code (9.4.4); the content
// of a cell of a tiling pattern starts with an empty current path and leaves the path of the
// page alone (8.7.3.1). And three more: the cells of a tiling pattern are on the lattice of the
// pattern space (8.7.3.1); a negative font size turns the advance of a glyph of every font type,
// never the character and the word spacing (9.4.4); every nested content has an operand stack
// of its own. And the overprint of a graphics state parameter dictionary (8.4.5): /OP sets the
// one for the other painting operations only when /op is absent, a dictionary with neither
// changes neither. Three more entries of that dictionary (PDF Reference 1.7, Table 4.8): /Font
// is [font size] with an indirect reference to the font dictionary, /BM as an array takes its
// first recognized name, /SMask of a wrong type leaves the soft mask alone. And: an ET without
// an open text object fails alone (5.3 of the Reference), a path construction operator without
// a current point is reported and still builds its segment (4.4.1), and a descendant processes
// the cell of an uncolored tiling pattern inside of PDFUncoloredTilingPatternGuard (4.6.2).
//
// The expected values are written by hand from the hand-written content and from the standard;
// the byte offsets are counted in the tests from the literal strings (offsetAfter), never taken
// from the processor.

#include "contenttestdocuments.h"

#include "pdfcatalog.h"
#include "pdfcms.h"
#include "pdfdocument.h"
#include "pdfexception.h"
#include "pdffont.h"
#include "pdfmeshqualitysettings.h"
#include "pdfoperationcontrol.h"
#include "pdfoptionalcontent.h"
#include "pdfpagecontenteditorprocessor.h"
#include "pdfpagecontentprocessor.h"
#include "pdfrenderer.h"
#include "pdfstructuretree.h"

#include <QtTest>
#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QStringList>
#include <QTransform>

#include <cmath>
#include <initializer_list>
#include <limits>
#include <memory>
#include <set>
#include <utility>
#include <vector>

using namespace pdf;
using pdftest::ContentTestDocuments;

/// Two numbers of a hand computation are the same (the sums of a few products of the test)
#define COMPARE_NEAR(actual, expected) \
    QVERIFY2(qAbs(qreal(actual) - qreal(expected)) <= 1e-9, \
             qPrintable(QStringLiteral("%1: actual %2, expected %3").arg(QLatin1String(#actual)).arg(qreal(actual), 0, 'g', 15).arg(qreal(expected), 0, 'g', 15)))

/// Records, what the processor tells to its descendant
class ContentRecordingProcessor : public PDFPageContentProcessor
{
public:
    using PDFPageContentProcessor::PDFPageContentProcessor;

    using Range = ContentOperationRange;
    using ImageRange = InlineImageRange;

    /// The limit of the nesting of the glyph procedures of the processor
    static constexpr size_t GLYPH_NESTING_LIMIT = MAXIMUM_TYPE3_GLYPH_NESTING;

    enum class EventKind
    {
        Before,         ///< performInterceptInstruction, before the operation
        After,          ///< performInterceptInstruction, after the operation
        Failed,         ///< performOperatorFailed
        GlyphBegin,     ///< performType3GlyphBegin
        GlyphEnd        ///< performType3GlyphEnd
    };

    struct Event
    {
        EventKind kind = EventKind::Before;
        Operator op = Operator::Invalid;
        QByteArray text;                    ///< Operator as text; the glyph procedure of GlyphBegin
        QString message;                    ///< Error of Failed
        unsigned int code = 0;              ///< Character code of GlyphBegin
        bool isFontOfState = false;         ///< GlyphBegin: the font is the Type 3 font of the graphic state
        PDFInteger level = 0;               ///< getContentNestingLevel
        Range range;                        ///< getOperationRange
        ImageRange imageRange;              ///< getInlineImageRange
        std::vector<QByteArray> streamData; ///< getContentStreamData of 0 ... getContentStreamCount - 1
        bool isDataBeforeNull = false;      ///< getContentStreamData(-1) is null
        bool isDataAfterNull = false;       ///< getContentStreamData(getContentStreamCount()) is null
        QTransform textMatrix;
        QTransform matrix;                  ///< Current transformation matrix
        PDFReal lineWidth = 0.0;
        PDFReal alphaFilling = 0.0;
        PDFReal alphaStroking = 0.0;
        bool overprintStroking = false;     ///< getOverprintMode: the overprint for stroking
        bool overprintFilling = false;      ///< getOverprintMode: the overprint for the other painting
        BlendMode blendMode = BlendMode::Normal;
        const PDFFont* font = nullptr;      ///< getTextFont: the address is compared, never used
        PDFReal fontSize = 0.0;             ///< getTextFontSize
        bool isColorIgnored = false;        ///< isColorOperatorsIgnored
        bool isTextObjectOpen = false;      ///< isTextProcessing
        size_t operandCount = 0;            ///< getOperands().size()
        bool hasSoftMask = false;
        bool isSoftMaskLuminosity = false;
        QByteArray softMaskContent;         ///< Decoded content of the group of the soft mask
    };

    struct PaintedPath
    {
        QPainterPath path;
        QRectF bounds;
        bool stroke = false;
        bool fill = false;
        bool text = false;
        Qt::FillRule fillRule = Qt::OddEvenFill;
        PDFInteger level = 0;
        QTransform matrix;
        PDFReal lineWidth = 0.0;
        QColor strokeColor;
        QColor fillColor;
    };

    struct TextItem
    {
        unsigned int code = 0;
        unsigned int codeLength = 0;
        CID cid = 0;
        bool isCharacter = false;
        bool isContentStream = false;
        bool isWordSpace = false;
        PDFReal advance = 0.0;
    };

    struct MarkedContent
    {
        QByteArray tag;
        PDFInteger identifier = -1;         ///< /MCID of the properties, -1 = none
        PDFInteger structuralParentKey = -1;
    };

    /// isContentDataRetained
    bool retainData = false;

    /// isTextIgnored
    bool ignoreText = false;

    /// Thrown by performPathPainting, when throwInPathPainting is set: an exception, which the
    /// processor does not catch, so it leaves processForm
    struct ProbeException
    {
    };
    bool throwInPathPainting = false;

    std::vector<Event> events;
    std::vector<PaintedPath> paths;
    std::vector<std::vector<TextItem>> textSequences;
    std::vector<MarkedContent> markedContents;
    std::vector<QByteArray> imageData;
    int markedContentEndCount = 0;

    struct OutputCharacter
    {
        QChar character;
        QTransform matrix;                  ///< The glyph space of the character to the device space
        PDFInteger level = 0;
    };

    struct Clipping
    {
        QRectF bounds;                      ///< Bounding box of the clipping path
        PDFInteger level = 0;
        size_t eventCount = 0;              ///< Number of the events before the call
    };

    std::vector<OutputCharacter> outputCharacters;
    std::vector<Clipping> clippings;
    int textBeginCount = 0;                 ///< performTextBegin before the operation
    int textEndCount = 0;                   ///< performTextEnd before the operation

    /// isTilingPatternProcessingAllowed: a pattern of more cells is refused, -1 = no limit
    PDFInteger tilingCellLimit = -1;

    /// The numbers of cells, which isTilingPatternProcessingAllowed was asked about
    mutable std::vector<PDFInteger> tilingCellCounts;

    // The getters of the processor after processContents
    Range currentRange() const { return getOperationRange(); }
    PDFInteger nestingLevel() const { return getContentNestingLevel(); }
    PDFInteger streamCount() const { return getContentStreamCount(); }
    bool isStreamDecoded(PDFInteger index) const { return isContentStreamDecoded(index); }
    QByteArray streamData(PDFInteger index) const { return getContentStreamData(index); }
    QList<PDFRenderError> renderErrors() const { return getRenderErrors(); }
    bool colorOperatorsIgnored() const { return isColorOperatorsIgnored(); }

    /// Processes the content by the public processForm, the way a descendant processes the cell
    /// of a tiling pattern by itself: inside of PDFUncoloredTilingPatternGuard(this, uncolored)
    void processCell(const QByteArray& content, bool uncolored)
    {
        PDFUncoloredTilingPatternGuard guard(this, uncolored);
        processForm(QTransform(), QRectF(), PDFObject(), PDFObject(), content, 0);
    }

    /// isColorOperatorsIgnored before the guards, inside of an uncolored one, of a colored one
    /// in it, of an uncolored one in both, and after the end of each of the three
    std::vector<bool> colorOperatorsIgnoredInNestedGuards()
    {
        std::vector<bool> result;
        result.push_back(isColorOperatorsIgnored());
        {
            PDFUncoloredTilingPatternGuard outerGuard(this);
            result.push_back(isColorOperatorsIgnored());
            {
                PDFUncoloredTilingPatternGuard coloredGuard(this, false);
                result.push_back(isColorOperatorsIgnored());
                {
                    PDFUncoloredTilingPatternGuard innerGuard(this, true);
                    result.push_back(isColorOperatorsIgnored());
                }
                result.push_back(isColorOperatorsIgnored());
            }
            result.push_back(isColorOperatorsIgnored());
        }
        result.push_back(isColorOperatorsIgnored());
        return result;
    }

    static Range makeRange(PDFInteger beginStream, PDFInteger beginOffset, PDFInteger endStream, PDFInteger endOffset)
    {
        Range range;
        range.beginStream = beginStream;
        range.beginOffset = beginOffset;
        range.endStream = endStream;
        range.endOffset = endOffset;
        return range;
    }

    static ImageRange makeImageRange(PDFInteger dictionaryOffset, PDFInteger dataOperatorOffset, PDFInteger dataOffset, PDFInteger dataLength, PDFInteger endOperatorOffset)
    {
        ImageRange range;
        range.dictionaryOffset = dictionaryOffset;
        range.dataOperatorOffset = dataOperatorOffset;
        range.dataOffset = dataOffset;
        range.dataLength = dataLength;
        range.endOperatorOffset = endOperatorOffset;
        return range;
    }

    static QString toString(const Range& range)
    {
        return QStringLiteral("[%1:%2 - %3:%4]").arg(range.beginStream).arg(range.beginOffset).arg(range.endStream).arg(range.endOffset);
    }

    static QString toString(const ImageRange& range)
    {
        return QStringLiteral("[dictionary %1, ID %2, data %3 + %4, EI %5]").arg(range.dictionaryOffset).arg(range.dataOperatorOffset).arg(range.dataOffset).arg(range.dataLength).arg(range.endOperatorOffset);
    }

    /// The events as texts "level:kind:text": "0:B:re" before and "0:A:re" after the operator re
    /// of the page, "1:F:gs" a failed gs of a nested content, "0:G+65" the begin of the glyph
    /// procedure of the code 65 shown by the page, "0:G-" its end
    /// \param withFailures false = without the failures of the operators
    QStringList trace(bool withFailures = true) const
    {
        QStringList result;
        for (const Event& event : events)
        {
            const QString level = QString::number(event.level);
            switch (event.kind)
            {
                case EventKind::Before:
                    result << level + QStringLiteral(":B:") + QString::fromLatin1(event.text);
                    break;
                case EventKind::After:
                    result << level + QStringLiteral(":A:") + QString::fromLatin1(event.text);
                    break;
                case EventKind::Failed:
                    if (withFailures)
                    {
                        result << level + QStringLiteral(":F:") + QString::fromLatin1(event.text);
                    }
                    break;
                case EventKind::GlyphBegin:
                    result << level + QStringLiteral(":G+") + QString::number(event.code);
                    break;
                case EventKind::GlyphEnd:
                    result << level + QStringLiteral(":G-");
                    break;
            }
        }
        return result;
    }

    /// The events of the kind
    std::vector<Event> eventsOfKind(EventKind kind) const
    {
        std::vector<Event> result;
        for (const Event& event : events)
        {
            if (event.kind == kind)
            {
                result.push_back(event);
            }
        }
        return result;
    }

    /// The last event of the kind with the operator text, nullptr = none
    const Event* lastEvent(EventKind kind, const QByteArray& text) const
    {
        for (auto it = events.crbegin(); it != events.crend(); ++it)
        {
            if (it->kind == kind && it->text == text)
            {
                return &*it;
            }
        }
        return nullptr;
    }

protected:
    virtual bool isContentDataRetained() const override { return retainData; }
    virtual bool isTextIgnored() const override { return ignoreText; }

    virtual bool isTilingPatternProcessingAllowed(PDFInteger tileCount) const override
    {
        tilingCellCounts.push_back(tileCount);
        return tilingCellLimit < 0 || tileCount <= tilingCellLimit;
    }

    virtual void performInterceptInstruction(Operator currentOperator, ProcessOrder processOrder, const QByteArray& operatorAsText) override
    {
        PDFPageContentProcessor::performInterceptInstruction(currentOperator, processOrder, operatorAsText);

        Event event = createEvent(processOrder == ProcessOrder::BeforeOperation ? EventKind::Before : EventKind::After);
        event.op = currentOperator;
        event.text = operatorAsText;
        m_lastOperatorText = operatorAsText;
        events.push_back(std::move(event));
    }

    virtual void performOperatorFailed(Operator currentOperator, const PDFRenderError& error) override
    {
        PDFPageContentProcessor::performOperatorFailed(currentOperator, error);

        Event event = createEvent(EventKind::Failed);
        event.op = currentOperator;
        event.text = m_lastOperatorText;
        event.message = error.message;
        events.push_back(std::move(event));
    }

    virtual void performType3GlyphBegin(const PDFType3Font* font, unsigned int code, const QByteArray& content) override
    {
        PDFPageContentProcessor::performType3GlyphBegin(font, code, content);

        Event event = createEvent(EventKind::GlyphBegin);
        event.text = content;
        event.code = code;
        event.isFontOfState = font && font->getFontType() == FontType::Type3 && getGraphicState()->getTextFont().get() == font;
        events.push_back(std::move(event));
    }

    virtual void performType3GlyphEnd() override
    {
        PDFPageContentProcessor::performType3GlyphEnd();
        events.push_back(createEvent(EventKind::GlyphEnd));
    }

    virtual void performPathPainting(const QPainterPath& path, bool stroke, bool fill, bool text, Qt::FillRule fillRule) override
    {
        PaintedPath paintedPath;
        paintedPath.path = path;
        paintedPath.bounds = path.boundingRect();
        paintedPath.stroke = stroke;
        paintedPath.fill = fill;
        paintedPath.text = text;
        paintedPath.fillRule = fillRule;
        paintedPath.level = getContentNestingLevel();
        paintedPath.matrix = getGraphicState()->getCurrentTransformationMatrix();
        paintedPath.lineWidth = getGraphicState()->getLineWidth();
        paintedPath.strokeColor = getGraphicState()->getStrokeColor();
        paintedPath.fillColor = getGraphicState()->getFillColor();
        paths.push_back(std::move(paintedPath));

        if (throwInPathPainting)
        {
            throw ProbeException();
        }
    }

    virtual bool performOriginalImagePainting(const PDFImage& image, const PDFStream* stream, PDFObjectReference reference) override
    {
        Q_UNUSED(image);
        Q_UNUSED(reference);

        // The data, which the processor took for the image; the image is not painted
        imageData.push_back(*stream->getContent());
        return true;
    }

    virtual void performProcessTextSequence(const TextSequence& textSequence, ProcessOrder order) override
    {
        if (order != ProcessOrder::BeforeOperation)
        {
            return;
        }

        std::vector<TextItem> items;
        for (const TextSequenceItem& item : textSequence.items)
        {
            TextItem textItem;
            textItem.code = item.code;
            textItem.codeLength = item.codeLength;
            textItem.cid = item.cid;
            textItem.isCharacter = item.isCharacter();
            textItem.isContentStream = item.isContentStream();
            textItem.isWordSpace = item.isWordSpace();
            textItem.advance = item.advance;
            items.push_back(textItem);
        }
        textSequences.push_back(std::move(items));
    }

    virtual void performMarkedContentBegin(const QByteArray& tag, const PDFObject& properties) override
    {
        MarkedContent markedContent;
        markedContent.tag = tag;
        markedContent.structuralParentKey = getStructuralParentKey();
        if (const PDFDictionary* dictionary = getDocument()->getDictionaryFromObject(properties))
        {
            const PDFObject& identifier = getDocument()->getObject(dictionary->get("MCID"));
            if (identifier.isInt())
            {
                markedContent.identifier = identifier.getInteger();
            }
        }
        markedContents.push_back(std::move(markedContent));
    }

    virtual void performMarkedContentEnd() override
    {
        ++markedContentEndCount;
    }

    virtual void performOutputCharacter(const PDFTextCharacterInfo& info) override
    {
        OutputCharacter outputCharacter;
        outputCharacter.character = info.character;
        outputCharacter.matrix = info.matrix;
        outputCharacter.level = getContentNestingLevel();
        outputCharacters.push_back(outputCharacter);
    }

    virtual void performClipping(const QPainterPath& path, Qt::FillRule fillRule) override
    {
        Q_UNUSED(fillRule);

        Clipping clipping;
        clipping.bounds = path.boundingRect();
        clipping.level = getContentNestingLevel();
        clipping.eventCount = events.size();
        clippings.push_back(clipping);
    }

    virtual void performTextBegin(ProcessOrder order) override
    {
        if (order == ProcessOrder::BeforeOperation)
        {
            ++textBeginCount;
        }
    }

    virtual void performTextEnd(ProcessOrder order) override
    {
        if (order == ProcessOrder::BeforeOperation)
        {
            ++textEndCount;
        }
    }

private:
    Event createEvent(EventKind kind)
    {
        const PDFPageContentProcessorState* state = getGraphicState();

        Event event;
        event.kind = kind;
        event.level = getContentNestingLevel();
        event.range = getOperationRange();
        event.imageRange = getInlineImageRange();
        for (PDFInteger i = 0, count = getContentStreamCount(); i < count; ++i)
        {
            event.streamData.push_back(getContentStreamData(i));
        }
        event.isDataBeforeNull = getContentStreamData(-1).isNull();
        event.isDataAfterNull = getContentStreamData(getContentStreamCount()).isNull();
        event.textMatrix = state->getTextMatrix();
        event.matrix = state->getCurrentTransformationMatrix();
        event.lineWidth = state->getLineWidth();
        event.alphaFilling = state->getAlphaFilling();
        event.alphaStroking = state->getAlphaStroking();
        event.overprintStroking = state->getOverprintMode().overprintStroking;
        event.overprintFilling = state->getOverprintMode().overprintFilling;
        event.blendMode = state->getBlendMode();
        event.font = state->getTextFont().get();
        event.fontSize = state->getTextFontSize();
        event.isColorIgnored = isColorOperatorsIgnored();
        event.isTextObjectOpen = isTextProcessing();
        event.operandCount = getOperands().size();
        event.hasSoftMask = state->getSoftMask() != nullptr;
        if (event.hasSoftMask)
        {
            // The soft mask dictionary of the graphic state (ISO 32000-2, 11.6.5.1, Table 142)
            const PDFDictionary* softMask = state->getSoftMask();
            const PDFObject& subtype = getDocument()->getObject(softMask->get("S"));
            const PDFObject& group = getDocument()->getObject(softMask->get("G"));
            event.isSoftMaskLuminosity = subtype.isName() && subtype.getString() == "Luminosity";
            if (group.isStream())
            {
                event.softMaskContent = getDocument()->getDecodedStream(group.getStream());
            }
        }
        return event;
    }

    QByteArray m_lastOperatorText;
};

using Recorder = ContentRecordingProcessor;

/// A descendant with the default isTilingPatternProcessingAllowed (every pattern is allowed),
/// which only counts the painted paths. It cancels the processing after CANCEL_AFTER_PATHS paths:
/// a lattice without a bound ends so (a renderer would paint its cells until the page is canceled).
class TilingCellCountingProcessor : public PDFPageContentProcessor, public PDFOperationControl
{
public:
    using PDFPageContentProcessor::PDFPageContentProcessor;

    static constexpr int CANCEL_AFTER_PATHS = 10000;

    int paintedPaths = 0;
    QList<QRectF> paintedBounds;    ///< Bounds of the paths painted with a nesting level 0

    virtual bool isOperationCanceled() const override { return paintedPaths >= CANCEL_AFTER_PATHS; }

protected:
    virtual void performPathPainting(const QPainterPath& path, bool stroke, bool fill, bool text, Qt::FillRule fillRule) override
    {
        Q_UNUSED(stroke);
        Q_UNUSED(fill);
        Q_UNUSED(text);
        Q_UNUSED(fillRule);

        ++paintedPaths;
        if (getContentNestingLevel() == 0)
        {
            paintedBounds.push_back(path.boundingRect());
        }
    }
};

/// A document, the objects its processing needs, and the processor after processContents
class ContentProcessorRun
{
public:
    /// \param processedDocument The document, its first page is processed
    /// \param retainData The processor keeps the data of all streams (isContentDataRetained)
    /// \param processPage false = the processor is only created, the test calls it
    explicit ContentProcessorRun(PDFDocument processedDocument, bool retainData = false, bool processPage = true) :
        document(std::move(processedDocument)),
        fontCache(32, 32),
        activity(&document, OCUsage::View, nullptr)
    {
        fontCache.setDocument(PDFModifiedDocument(&document, &activity));
        iccProfileCache.setDocument(PDFModifiedDocument(&document, &activity));

        processor = std::make_unique<Recorder>(document.getCatalog()->getPage(0), &document, &fontCache, &iccProfileCache, &cms, &activity,
                                               QTransform(), PDFMeshQualitySettings());
        processor->retainData = retainData;
        if (processPage)
        {
            errors = processor->processContents();
        }
    }

    /// The messages of the errors, for the message of a failed check
    QString errorText() const
    {
        QStringList messages;
        for (const PDFRenderError& error : errors)
        {
            messages << error.message;
        }
        return messages.join(QStringLiteral(" | "));
    }

    PDFDocument document;
    PDFCMSGeneric cms;
    PDFFontCache fontCache;
    PDFICCProfileCache iccProfileCache;
    PDFOptionalContentActivity activity;
    std::unique_ptr<Recorder> processor;
    QList<PDFRenderError> errors;
};

class ContentProcessorTest : public QObject
{
    Q_OBJECT

private slots:
    // Ranges of the operations and the stream data
    void rangesOfOneStream();
    void rangesAcrossTwoStreams();
    void streamData_data();
    void streamData();
    void brokenContentsArray();
    void nestingLevelOfForm();
    void nestingLevelOfTile();
    void nestingLevelOfGlyph();
    void contentGivenByTheCaller();

    // Inline image as one instruction
    void inlineImageWithEndMarkerInData();
    void inlineImageWithNamedColorSpace();
    void inlineImagePlain();
    void inlineImageWithoutData();

    // Failing operators
    void failingOperators();
    void failingOperatorOfForm();

    // Glyph procedures of a Type 3 font
    void type3GlyphProcedure();
    void type3GlyphWithTextObject();
    void type3GlyphWithUnclosedTextObject();
    void type3GlyphWithEndOfTextOnly();
    void type3GlyphKeepsTextLineMatrix();
    void type3GlyphKeepsTextClip();
    void type3GlyphShowingItself();
    void type3GlyphChainOverTheLimit();

    // Character codes of the text items
    void textItemCodes();

    // Word and character spacing (QE.15)
    void wordSpacing_data();
    void wordSpacing();
    void type3Spacing_data();
    void type3Spacing();

    // A character code with a width and without a glyph
    void codeWithoutGlyph_data();
    void codeWithoutGlyph();

    // Tiling patterns: the cells are content streams of their own
    void tilingPatternFillAndStroke();
    void tilingPatternStroke();
    void tilingPatternFillsText();
    void tilingPatternRendering();
    void tilingPatternLattice_data();
    void tilingPatternLattice();
    void tilingPatternLatticeRendering();
    void tilingPatternCellLimit();
    void tilingPatternTooFineToCount();

    // A negative font size
    void negativeFontSize_data();
    void negativeFontSize();
    void negativeFontSizeSpacing_data();
    void negativeFontSizeSpacing();

    // The operand stack of a nested content
    void nestedContentOperandsOfCell();
    void nestedContentOperandsOfGlyph();
    void nestedContentOperandsOfForm();

    // The same output as before the change
    void interceptedOperators();
    void pathsAndGraphicState();

    // The overprint parameters of a graphics state parameter dictionary (OP, op)
    void overprintOfGraphicState_data();
    void overprintOfGraphicState();

    // The entries Font, BM and SMask of a graphics state parameter dictionary
    void fontOfGraphicState_data();
    void fontOfGraphicState();
    void fontOfGraphicStateOfIgnoredText();
    void blendModeOfGraphicState_data();
    void blendModeOfGraphicState();
    void softMaskOfGraphicState_data();
    void softMaskOfGraphicState();

    // ET without an open text object
    void textEndWithoutBegin_data();
    void textEndWithoutBegin();
    void textEndWithoutBeginInForm();
    void textEndWithoutBeginInGlyph();

    // Path construction operators without a current point
    void pathWithoutCurrentPoint_data();
    void pathWithoutCurrentPoint();

    // The cell of an uncolored tiling pattern
    void uncoloredPatternIgnoresColorOperators();
    void coloredPatternInsideUncoloredCell();
    void uncoloredPatternInsideColoredCell();
    void uncoloredCellByDescendant_data();
    void uncoloredCellByDescendant();
    void uncoloredCellGuardsNest();
    void uncoloredCellGuardEndsWithException();

    // The documents of contenttestdocuments.h
    void documentSimpleFonts();
    void documentType0Font();
    void documentType3Fonts();
    void documentTaggedPage();
    void documentSoftMask();

    // The old content editor and the glyph procedures
    void oldEditorKeepsType3Text_data();
    void oldEditorKeepsType3Text();

private:
    /// Offset right after the only occurrence of \p token in \p content: the end of an operator
    /// is counted from the hand-written content by its unique text (the operator with a part of
    /// its operands or of the white space before it). -1, when the token does not occur exactly
    /// once - a wrong test, its comparison fails.
    static PDFInteger offsetAfter(const QByteArray& content, const QByteArray& token);

    /// The texts "level:B:op", "level:A:op" of the operators of one content without a nested content
    static QStringList pairs(int level, std::initializer_list<const char*> operators);

    /// The font of the resources of the page of the document
    static PDFFontPointer getFont(const PDFDocument& document, const char* name);

    /// The content "BT /font 10 Tf Tz Tc Tw 20 100 Td show ET" of the tests of the spacing
    static QByteArray spacingContent(const QByteArray& font, PDFReal horizontalScaling, PDFReal characterSpacing, PDFReal wordSpacing, const QByteArray& show);

    /// Horizontal position of the text matrix after the last text showing operator of the page
    /// (read after the operator, before ET discards the matrix); the run has no error and no warning
    static bool getTextPositionAfterShow(const ContentProcessorRun& run, const QByteArray& showOperator, PDFReal* position);

    static bool isSameColor(const QColor& color, int red, int green, int blue);

    /// The references to the fonts of createFontStateResources
    struct FontStateFonts
    {
        PDFObject fontA;    ///< /FA: addType3FontObject, Plain
        PDFObject fontB;    ///< /FB: addType3FontObject, NoSpaceProcedure
        PDFObject fontS;    ///< /FS: addSimpleFontObject
        PDFObject fontU;    ///< A font, which the font resources do not have: addType3FontObject, Plain
    };

    /// The resources of the tests of the entry Font of a graphics state parameter dictionary:
    /// /Font << /FA /FB /FS >> (FontStateFonts) and the dictionaries of /ExtGState, each << /LW 3
    /// /Font (the value of its row) >>. The code 65 has the width 0.6 of the text space in the
    /// three Type 3 fonts.
    /// \param fonts Gets the references to the fonts
    static PDFDictionaryBuilder createFontStateResources(PDFDocumentBuilder* builder, FontStateFonts* fonts);

    /// The elements of the path as a text: "M x y" (move), "L x y" (line), "C x1 y1 x2 y2 x3 y3"
    /// (curve), separated by a space, e.g. "M 0 0 L 10 20"
    static QString pathText(const QPainterPath& path);

    /// Adds a Type 3 font with one glyph and returns the reference to it: /FontBBox [0 0 100 100],
    /// /FontMatrix [0.01 0 0 0.01 0 0], /FirstChar 65, /LastChar 65, /Widths [60], /Encoding with
    /// /Differences [65 /square], /CharProcs << /square (a stream with \p procedure) >>,
    /// /Resources << /ProcSet [/PDF] >>
    static PDFObject addGlyphFontObject(PDFDocumentBuilder* builder, const QByteArray& procedure);

    /// The content of the cell of the uncolored tiling pattern /PU of createUncoloredPatternResources:
    /// each of the twelve color operators once, then the square [0 0 5 5] filled and stroked
    static QByteArray uncoloredCellContent();

    /// The resources of the tests of the uncolored tiling patterns: /ColorSpace << /Cs [/Pattern
    /// /DeviceRGB] >> and /Pattern with four tiling patterns (/PatternType 1, /TilingType 1,
    /// /BBox [0 0 size size], both steps = size):
    ///   /PU   uncolored (/PaintType 2), size 10, the content uncoloredCellContent
    ///   /PC   colored (/PaintType 1), size 10, the content "0 0 1 rg 0 0 3 3 re f"
    ///   /PUN  uncolored, size 10, /Resources << /Pattern << /PC >> >>, the content
    ///         "/Pattern cs /PC scn 0 0 5 5 re f" - it selects the colored pattern /PC
    ///   /PCN  colored, size 40, /Resources << /ColorSpace << /Cs >> /Pattern << /PU >> >>, the
    ///         content "/Cs cs 0 1 0 /PU scn 2 2 6 6 re f 0 0 1 rg 20 20 5 5 re f" - it fills by
    ///         the uncolored pattern /PU in green and sets a color after it
    static PDFDictionaryBuilder createUncoloredPatternResources(PDFDocumentBuilder* builder);
};

PDFInteger ContentProcessorTest::offsetAfter(const QByteArray& content, const QByteArray& token)
{
    const qsizetype index = content.indexOf(token);
    if (index < 0 || content.indexOf(token, index + 1) >= 0)
    {
        qWarning("The token '%s' does not occur exactly once in the content.", token.constData());
        return -1;
    }

    return index + token.size();
}

QStringList ContentProcessorTest::pairs(int level, std::initializer_list<const char*> operators)
{
    QStringList result;
    for (const char* operatorText : operators)
    {
        result << QStringLiteral("%1:B:%2").arg(level).arg(QLatin1String(operatorText));
        result << QStringLiteral("%1:A:%2").arg(level).arg(QLatin1String(operatorText));
    }
    return result;
}

PDFFontPointer ContentProcessorTest::getFont(const PDFDocument& document, const char* name)
{
    const PDFDictionary* resources = document.getDictionaryFromObject(document.getCatalog()->getPage(0)->getResources());
    const PDFDictionary* fonts = resources ? document.getDictionaryFromObject(resources->get("Font")) : nullptr;
    if (!fonts || !fonts->hasKey(name))
    {
        return PDFFontPointer();
    }

    return PDFFont::createFont(fonts->get(name), name, &document);
}

QByteArray ContentProcessorTest::spacingContent(const QByteArray& font, PDFReal horizontalScaling, PDFReal characterSpacing, PDFReal wordSpacing, const QByteArray& show)
{
    return "BT /" + font + " 10 Tf " + QByteArray::number(horizontalScaling) + " Tz " + QByteArray::number(characterSpacing) + " Tc " +
           QByteArray::number(wordSpacing) + " Tw 20 100 Td " + show + " ET";
}

bool ContentProcessorTest::getTextPositionAfterShow(const ContentProcessorRun& run, const QByteArray& showOperator, PDFReal* position)
{
    const Recorder::Event* event = run.processor->lastEvent(Recorder::EventKind::After, showOperator);
    if (!run.errors.isEmpty() || !event)
    {
        qWarning("The text was not shown without errors: %s", qPrintable(run.errorText()));
        return false;
    }

    // The text matrix is a translation here (Td only): its position is its last row
    const QTransform& matrix = event->textMatrix;
    if (matrix.m11() != 1.0 || matrix.m12() != 0.0 || matrix.m21() != 0.0 || matrix.m22() != 1.0 || matrix.dy() != 100.0)
    {
        qWarning("The text matrix is not a horizontal move of the text line.");
        return false;
    }

    *position = matrix.dx();
    return true;
}

bool ContentProcessorTest::isSameColor(const QColor& color, int red, int green, int blue)
{
    return color.red() == red && color.green() == green && color.blue() == blue;
}

QString ContentProcessorTest::pathText(const QPainterPath& path)
{
    QStringList parts;
    for (int i = 0; i < path.elementCount(); ++i)
    {
        const QPainterPath::Element element = path.elementAt(i);
        const QString point = QString::number(element.x) + QLatin1Char(' ') + QString::number(element.y);
        switch (element.type)
        {
            case QPainterPath::MoveToElement:
                parts << QStringLiteral("M") << point;
                break;
            case QPainterPath::LineToElement:
                parts << QStringLiteral("L") << point;
                break;
            case QPainterPath::CurveToElement:
                parts << QStringLiteral("C") << point;
                break;
            case QPainterPath::CurveToDataElement:
                parts << point;
                break;
        }
    }
    return parts.join(QLatin1Char(' '));
}

PDFObject ContentProcessorTest::addGlyphFontObject(PDFDocumentBuilder* builder, const QByteArray& procedure)
{
    using Documents = ContentTestDocuments;

    PDFObject glyph = Documents::addStreamObject(builder, Documents::DictionaryEntries(), procedure);
    return Documents::addObject(builder, Documents::createDictionaryObject({
        { "Type", PDFObject::createName("Font") },
        { "Subtype", PDFObject::createName("Type3") },
        { "FontBBox", Documents::createIntegerArrayObject({ 0, 0, 100, 100 }) },
        { "FontMatrix", Documents::createNumberArrayObject({ 0.01, 0, 0, 0.01, 0, 0 }) },
        { "FirstChar", PDFObject::createInteger(65) },
        { "LastChar", PDFObject::createInteger(65) },
        { "Widths", Documents::createIntegerArrayObject({ 60 }) },
        { "Encoding", Documents::createDictionaryObject({ { "Type", PDFObject::createName("Encoding") },
                                                          { "Differences", Documents::createArrayObject({ PDFObject::createInteger(65), PDFObject::createName("square") }) } }) },
        { "CharProcs", Documents::createDictionaryObject({ { "square", std::move(glyph) } }) },
        { "Resources", Documents::createDictionaryObject({ { "ProcSet", Documents::createArrayObject({ PDFObject::createName("PDF") }) } }) } }));
}

QByteArray ContentProcessorTest::uncoloredCellContent()
{
    return QByteArray("/DeviceRGB cs /DeviceRGB CS 0 1 0 sc 0 1 0 SC 0 1 0 scn 0 1 0 SCN "
                      "0.5 g 0.5 G 0 0 1 rg 0 0 1 RG 0 0 0 1 k 0 0 0 1 K "
                      "0 0 5 5 re B");
}

PDFDictionaryBuilder ContentProcessorTest::createUncoloredPatternResources(PDFDocumentBuilder* builder)
{
    using Documents = ContentTestDocuments;

    const auto addPattern = [builder](PDFInteger paintType, PDFInteger size, PDFObject patternResources, const QByteArray& content)
    {
        return Documents::addStreamObject(builder, { { "Type", PDFObject::createName("Pattern") },
                                                     { "PatternType", PDFObject::createInteger(1) },
                                                     { "PaintType", PDFObject::createInteger(paintType) },
                                                     { "TilingType", PDFObject::createInteger(1) },
                                                     { "BBox", Documents::createIntegerArrayObject({ 0, 0, size, size }) },
                                                     { "XStep", PDFObject::createInteger(size) },
                                                     { "YStep", PDFObject::createInteger(size) },
                                                     { "Resources", std::move(patternResources) } }, content);
    };
    const auto noResources = []()
    {
        return Documents::createDictionaryObject(Documents::DictionaryEntries());
    };

    const PDFObject colorSpace = Documents::createArrayObject({ PDFObject::createName("Pattern"), PDFObject::createName("DeviceRGB") });
    const PDFObject colorSpaces = Documents::createDictionaryObject({ { "Cs", colorSpace } });

    const PDFObject uncolored = addPattern(2, 10, noResources(), uncoloredCellContent());
    const PDFObject colored = addPattern(1, 10, noResources(), "0 0 1 rg 0 0 3 3 re f");
    const PDFObject uncoloredNesting = addPattern(2, 10, Documents::createDictionaryObject({ { "Pattern", Documents::createDictionaryObject({ { "PC", colored } }) } }),
                                                  "/Pattern cs /PC scn 0 0 5 5 re f");
    const PDFObject coloredNesting = addPattern(1, 40, Documents::createDictionaryObject({ { "ColorSpace", colorSpaces },
                                                                                           { "Pattern", Documents::createDictionaryObject({ { "PU", uncolored } }) } }),
                                                "/Cs cs 0 1 0 /PU scn 2 2 6 6 re f 0 0 1 rg 20 20 5 5 re f");

    PDFDictionaryBuilder resources;
    resources.addEntry(PDFInplaceOrMemoryString("ColorSpace"), colorSpaces);
    resources.addEntry(PDFInplaceOrMemoryString("Pattern"), Documents::createDictionaryObject({ { "PU", uncolored },
                                                                                                { "PC", colored },
                                                                                                { "PUN", uncoloredNesting },
                                                                                                { "PCN", coloredNesting } }));
    return resources;
}

PDFDictionaryBuilder ContentProcessorTest::createFontStateResources(PDFDocumentBuilder* builder, FontStateFonts* fonts)
{
    using Documents = ContentTestDocuments;

    fonts->fontA = Documents::addType3FontObject(builder, Documents::Type3Variant::Plain);
    fonts->fontB = Documents::addType3FontObject(builder, Documents::Type3Variant::NoSpaceProcedure);
    fonts->fontS = Documents::addSimpleFontObject(builder);
    fonts->fontU = Documents::addType3FontObject(builder, Documents::Type3Variant::Plain);

    const PDFObject font = fonts->fontA;
    const PDFObject size = PDFObject::createInteger(12);
    const PDFObject indirectNumber = Documents::addObject(builder, PDFObject::createInteger(12));
    const PDFObject noFont = Documents::addObject(builder, Documents::createDictionaryObject({ { "Type", PDFObject::createName("ExtGState") } }));
    const PDFObject directFont = Documents::createDictionaryObject({ { "Type", PDFObject::createName("Font") },
                                                                     { "Subtype", PDFObject::createName("Type1") },
                                                                     { "BaseFont", PDFObject::createName("Helvetica") } });
    const PDFObject nothing = PDFObject::createReference(PDFObjectReference(100000, 0));
    const PDFObject indirectArray = Documents::addObject(builder, Documents::createArrayObject({ font, size }));

    // << /LW 3 /Font value >>, the value mostly an array
    const auto state = [](PDFObject value)
    {
        return Documents::createDictionaryObject({ { "LW", PDFObject::createInteger(3) }, { "Font", std::move(value) } });
    };
    const auto array = [](std::vector<PDFObject> items)
    {
        return Documents::createArrayObject(std::move(items));
    };

    PDFDictionaryBuilder resources;
    resources.addEntry(PDFInplaceOrMemoryString("Font"), Documents::createDictionaryObject({ { "FA", fonts->fontA }, { "FB", fonts->fontB }, { "FS", fonts->fontS } }));
    resources.addEntry(PDFInplaceOrMemoryString("ExtGState"), Documents::createDictionaryObject({
        { "GReference", state(array({ font, size })) },
        { "GSimpleFont", state(array({ fonts->fontS, size })) },
        { "GUnnamedFont", state(array({ fonts->fontU, size })) },
        { "GRealSize", state(array({ font, PDFObject::createReal(7.5) })) },
        { "GIndirectSize", state(array({ font, indirectNumber })) },
        { "GIndirectArray", state(indirectArray) },
        { "GName", state(array({ PDFObject::createName("FA"), size })) },
        { "GNoArray", state(font) },
        { "GEmpty", state(array({ })) },
        { "GOneItem", state(array({ font })) },
        { "GThreeItems", state(array({ font, size, size })) },
        { "GDirectFont", state(array({ directFont, size })) },
        { "GNumberFont", state(array({ size, size })) },
        { "GSizeName", state(array({ font, PDFObject::createName("x") })) },
        { "GSizeNull", state(array({ font, PDFObject() })) },
        { "GSizeInfinite", state(array({ font, PDFObject::createReal(std::numeric_limits<PDFReal>::infinity()) })) },
        { "GSizeNaN", state(array({ font, PDFObject::createReal(std::numeric_limits<PDFReal>::quiet_NaN()) })) },
        { "GReferenceToNumber", state(array({ indirectNumber, size })) },
        { "GReferenceToNoFont", state(array({ noFont, size })) },
        { "GReferenceToNothing", state(array({ nothing, size })) },
        { "GNameMissing", state(array({ PDFObject::createName("Missing"), size })) },
        { "GNameSizeName", state(array({ PDFObject::createName("FA"), PDFObject::createName("x") })) } }));
    return resources;
}

void ContentProcessorTest::rangesOfOneStream()
{
    // Comments, more white space of every kind and an operand, which cannot be read (1.2.3).
    // The comment of the third line holds the texts of two operators: they are not operations.
    const QByteArray content = "% first comment\n"
                               "  q\t1 0 0 1 10 20 cm\r\n"
                               "0 0 1 rg % blue (not operators: re f)\n"
                               "20 1.2.3 30 60 40 re\n"
                               "f\n"
                               "5 5 10 10 re f*\n"
                               "Q   \n"
                               "% last comment\n";

    // The end of each operator, counted from the content
    const std::vector<std::pair<QByteArray, PDFInteger>> expectedEnds = { { "q", offsetAfter(content, "  q") },
                                                                          { "cm", offsetAfter(content, "20 cm") },
                                                                          { "rg", offsetAfter(content, "0 0 1 rg") },
                                                                          { "re", offsetAfter(content, "60 40 re") },
                                                                          { "f", offsetAfter(content, "re\nf") },
                                                                          { "re", offsetAfter(content, "10 10 re") },
                                                                          { "f*", offsetAfter(content, "f*") },
                                                                          { "Q", offsetAfter(content, "Q") } };

    ContentProcessorRun run(ContentTestDocuments::createDocument(content, &ContentTestDocuments::createNoResources));
    const Recorder& recorder = *run.processor;

    // The token 1.2.3 and the operator, which reads it
    QCOMPARE(run.errors.size(), 2);

    const std::vector<Recorder::Event> before = recorder.eventsOfKind(Recorder::EventKind::Before);
    const std::vector<Recorder::Event> after = recorder.eventsOfKind(Recorder::EventKind::After);
    QCOMPARE(before.size(), expectedEnds.size());
    QCOMPARE(after.size(), expectedEnds.size());

    QByteArray joined;
    PDFInteger expectedBegin = 0;
    for (size_t i = 0; i < expectedEnds.size(); ++i)
    {
        // The operation begins, where the previous one ended (the first one at the start of the
        // content), and ends right after its operator
        const Recorder::Range expectedRange = Recorder::makeRange(0, expectedBegin, 0, expectedEnds[i].second);
        QCOMPARE(before[i].text, expectedEnds[i].first);
        QVERIFY2(before[i].range == expectedRange, qPrintable(Recorder::toString(before[i].range) + QStringLiteral(" != ") + Recorder::toString(expectedRange)));
        QVERIFY2(after[i].range == expectedRange, qPrintable(Recorder::toString(after[i].range) + QStringLiteral(" != ") + Recorder::toString(expectedRange)));
        QVERIFY(before[i].range.isValid());
        QVERIFY(!before[i].imageRange.isValid());
        QCOMPARE(before[i].level, PDFInteger(0));

        joined += content.mid(before[i].range.beginOffset, before[i].range.endOffset - before[i].range.beginOffset);
        expectedBegin = expectedEnds[i].second;
    }

    // The ranges and the rest after the last one are the whole content, each byte once
    const QByteArray rest = content.mid(expectedBegin);
    QCOMPARE(rest, QByteArray("   \n% last comment\n"));
    QCOMPARE(joined + rest, content);

    // The bytes of an operation are its white space, comments and operands, also those, which
    // cannot be read
    QCOMPARE(content.mid(before[0].range.beginOffset, before[0].range.endOffset - before[0].range.beginOffset), QByteArray("% first comment\n  q"));
    QCOMPARE(content.mid(before[3].range.beginOffset, before[3].range.endOffset - before[3].range.beginOffset), QByteArray(" % blue (not operators: re f)\n20 1.2.3 30 60 40 re"));

    // The operator re, which got the operand 1.2.3, has failed with the last error of the list
    const std::vector<Recorder::Event> failed = recorder.eventsOfKind(Recorder::EventKind::Failed);
    QCOMPARE(failed.size(), size_t(1));
    QCOMPARE(failed[0].op, PDFPageContentProcessor::Operator::Rectangle);
    QCOMPARE(failed[0].message, run.errors.back().message);
    QVERIFY(failed[0].range == before[3].range);

    // The second rectangle is painted, the first one is not
    QCOMPARE(recorder.paths.size(), size_t(1));
    QCOMPARE(recorder.paths[0].bounds, QRectF(5, 5, 10, 10));

    // After the processing: the range of the last operation of the page
    QVERIFY(recorder.currentRange() == before.back().range);
    QCOMPARE(recorder.nestingLevel(), PDFInteger(0));
    QCOMPARE(recorder.streamCount(), PDFInteger(1));
}

void ContentProcessorTest::rangesAcrossTwoStreams()
{
    // ISO 32000-2, 7.8.2: the streams of /Contents are one content, an operator can have its
    // operands in the previous stream. The operands of re and the white space after them end the
    // stream 0, the operator is the first token of the stream 1.
    const QByteArray stream0 = "0 0 1 rg\n20 30 60 40 \n";
    const QByteArray stream1 = "re f\n1 0 0 rg % a comment\n";

    ContentProcessorRun run(ContentTestDocuments::createDocument(QList<QByteArray>{ stream0, stream1 }, &ContentTestDocuments::createNoResources));
    const Recorder& recorder = *run.processor;
    QVERIFY2(run.errors.isEmpty(), qPrintable(run.errorText()));

    const PDFInteger endOfFirstColor = offsetAfter(stream0, "0 0 1 rg");
    const PDFInteger endOfRectangle = offsetAfter(stream1, "re");
    const PDFInteger endOfFill = offsetAfter(stream1, "re f");
    const PDFInteger endOfSecondColor = offsetAfter(stream1, "1 0 0 rg");
    QCOMPARE(endOfFirstColor, PDFInteger(8));
    QCOMPARE(endOfRectangle, PDFInteger(2));

    const std::vector<Recorder::Event> before = recorder.eventsOfKind(Recorder::EventKind::Before);
    QCOMPARE(recorder.trace(), pairs(0, { "rg", "re", "f", "rg" }));
    QCOMPARE(before.size(), size_t(4));

    const std::vector<Recorder::Range> expectedRanges = { Recorder::makeRange(0, 0, 0, endOfFirstColor),
                                                          Recorder::makeRange(0, endOfFirstColor, 1, endOfRectangle),
                                                          Recorder::makeRange(1, endOfRectangle, 1, endOfFill),
                                                          Recorder::makeRange(1, endOfFill, 1, endOfSecondColor) };
    for (size_t i = 0; i < expectedRanges.size(); ++i)
    {
        QVERIFY2(before[i].range == expectedRanges[i], qPrintable(Recorder::toString(before[i].range) + QStringLiteral(" != ") + Recorder::toString(expectedRanges[i])));
        QVERIFY(before[i].range.isValid());
    }

    // The bytes of the split operation: the rest of the stream 0 (with its trailing white space)
    // and the start of the stream 1
    QCOMPARE(stream0.mid(expectedRanges[1].beginOffset) + stream1.left(expectedRanges[1].endOffset), QByteArray("\n20 30 60 40 \nre"));

    // The operator got its operands of the previous stream: the rectangle is painted, in blue
    QCOMPARE(recorder.paths.size(), size_t(1));
    QCOMPARE(recorder.paths[0].bounds, QRectF(20, 30, 60, 40));
    QVERIFY(recorder.paths[0].fill);
    QVERIFY(!recorder.paths[0].stroke);
    QVERIFY(isSameColor(recorder.paths[0].fillColor, 0, 0, 255));

    QVERIFY(recorder.currentRange() == expectedRanges.back());
    QCOMPARE(recorder.streamCount(), PDFInteger(2));
}

void ContentProcessorTest::streamData_data()
{
    QTest::addColumn<bool>("retained");

    QTest::newRow("default") << false;
    QTest::newRow("retained") << true;
}

void ContentProcessorTest::streamData()
{
    QFETCH(bool, retained);

    const QByteArray stream0 = "0 0 1 rg\n20 30 60 40 \n";
    const QByteArray stream1 = "re f\n";

    ContentProcessorRun run(ContentTestDocuments::createDocument(QList<QByteArray>{ stream0, stream1 }, &ContentTestDocuments::createNoResources), retained);
    const Recorder& recorder = *run.processor;
    QVERIFY2(run.errors.isEmpty(), qPrintable(run.errorText()));

    // rg is an operator of the stream 0, re and f of the stream 1
    const std::vector<Recorder::Event> before = recorder.eventsOfKind(Recorder::EventKind::Before);
    QCOMPARE(before.size(), size_t(3));

    // Inside of the stream 0: one stream reached, its data are there
    QCOMPARE(before[0].streamData.size(), size_t(1));
    QCOMPARE(before[0].streamData[0], stream0);
    QVERIFY(before[0].isDataBeforeNull);
    QVERIFY(before[0].isDataAfterNull);

    // Inside of the stream 1: two streams reached; the data of the stream 0 are kept only by a
    // processor, which asked for it
    for (size_t i = 1; i < before.size(); ++i)
    {
        QCOMPARE(before[i].streamData.size(), size_t(2));
        QCOMPARE(before[i].streamData[1], stream1);
        if (retained)
        {
            QCOMPARE(before[i].streamData[0], stream0);
        }
        else
        {
            QVERIFY(before[i].streamData[0].isNull());
        }
        QVERIFY(before[i].isDataBeforeNull);
        QVERIFY(before[i].isDataAfterNull);
    }

    // After the processing
    QCOMPARE(recorder.streamCount(), PDFInteger(2));
    if (retained)
    {
        QCOMPARE(recorder.streamData(0), stream0);
        QCOMPARE(recorder.streamData(1), stream1);
    }
    else
    {
        QVERIFY(recorder.streamData(0).isNull());
        QVERIFY(recorder.streamData(1).isNull());
    }
    QVERIFY(recorder.isStreamDecoded(0));
    QVERIFY(recorder.isStreamDecoded(1));

    // An index out of range
    QVERIFY(recorder.streamData(-1).isNull());
    QVERIFY(recorder.streamData(2).isNull());
    QVERIFY(!recorder.isStreamDecoded(-1));
    QVERIFY(!recorder.isStreamDecoded(2));
}

void ContentProcessorTest::brokenContentsArray()
{
    // /Contents [ stream, a dictionary, a stream, which cannot be decoded, stream ]
    const QByteArray firstContent = "0 0 1 rg 10 10 20 20 re f\n";
    const QByteArray lastContent = "1 0 0 rg 50 50 20 20 re f\n";

    ContentProcessorRun run(ContentTestDocuments::createBrokenContentsDocument(firstContent, lastContent, &ContentTestDocuments::createNoResources), true);
    const Recorder& recorder = *run.processor;

    // One error for each bad item, the first one is the item, which is not a stream
    QCOMPARE(run.errors.size(), 2);
    QCOMPARE(run.errors[0].type, RenderErrorType::Error);
    QCOMPARE(run.errors[1].type, RenderErrorType::Error);
    QVERIFY(!run.errors[1].message.isEmpty());

    // The streams keep the indices of the array
    QCOMPARE(recorder.streamCount(), PDFInteger(ContentTestDocuments::BROKEN_CONTENTS_ITEM_COUNT));
    QVERIFY(recorder.isStreamDecoded(0));
    QVERIFY(!recorder.isStreamDecoded(ContentTestDocuments::BROKEN_CONTENTS_NOT_STREAM_INDEX));
    QVERIFY(!recorder.isStreamDecoded(ContentTestDocuments::BROKEN_CONTENTS_NOT_DECODABLE_INDEX));
    QVERIFY(recorder.isStreamDecoded(3));
    QCOMPARE(recorder.streamData(0), firstContent);
    QVERIFY(recorder.streamData(ContentTestDocuments::BROKEN_CONTENTS_NOT_STREAM_INDEX).isNull());
    QVERIFY(recorder.streamData(ContentTestDocuments::BROKEN_CONTENTS_NOT_DECODABLE_INDEX).isNull());
    QCOMPARE(recorder.streamData(3), lastContent);

    // The page is processed without the bad items
    QCOMPARE(recorder.trace(), pairs(0, { "rg", "re", "f", "rg", "re", "f" }));
    QCOMPARE(recorder.paths.size(), size_t(2));
    QCOMPARE(recorder.paths[0].bounds, QRectF(10, 10, 20, 20));
    QVERIFY(isSameColor(recorder.paths[0].fillColor, 0, 0, 255));
    QCOMPARE(recorder.paths[1].bounds, QRectF(50, 50, 20, 20));
    QVERIFY(isSameColor(recorder.paths[1].fillColor, 255, 0, 0));

    // The first operation of the last stream begins, where the last operation of the stream 0
    // ended (the trailing new line of the stream 0 is its byte), and ends in the stream 3
    const std::vector<Recorder::Event> before = recorder.eventsOfKind(Recorder::EventKind::Before);
    QCOMPARE(before.size(), size_t(6));
    const Recorder::Range expectedFirst = Recorder::makeRange(0, offsetAfter(firstContent, "re f"), 3, offsetAfter(lastContent, "1 0 0 rg"));
    const Recorder::Range expectedLast = Recorder::makeRange(3, offsetAfter(lastContent, "20 20 re"), 3, offsetAfter(lastContent, "re f"));
    QVERIFY2(before[3].range == expectedFirst, qPrintable(Recorder::toString(before[3].range) + QStringLiteral(" != ") + Recorder::toString(expectedFirst)));
    QVERIFY2(before[5].range == expectedLast, qPrintable(Recorder::toString(before[5].range) + QStringLiteral(" != ") + Recorder::toString(expectedLast)));

    // Inside of the last stream the four items are counted
    QCOMPARE(before[5].streamData.size(), size_t(4));
    QCOMPARE(before[0].streamData.size(), size_t(1));
}

void ContentProcessorTest::nestingLevelOfForm()
{
    ContentProcessorRun run(ContentTestDocuments::createFormDocument());
    const Recorder& recorder = *run.processor;
    QVERIFY2(run.errors.isEmpty(), qPrintable(run.errorText()));

    // The operators of the page have the level 0, those of a form drawn by Do the level 1 - the
    // same list as before the change of the processor
    const QStringList form = pairs(1, { "gs", "rg", "re", "f" });
    QStringList expected;
    expected << pairs(0, { "q" }) << QStringLiteral("0:B:Do") << form << QStringLiteral("0:A:Do") << pairs(0, { "Q" });
    expected << pairs(0, { "q", "cm" }) << QStringLiteral("0:B:Do") << form << QStringLiteral("0:A:Do") << pairs(0, { "Q" });
    expected << QStringLiteral("0:B:Do") << form << QStringLiteral("0:A:Do");
    QCOMPARE(recorder.trace(), expected);

    const QByteArray pageContent = ContentTestDocuments::formPageContent();
    const QByteArray formContent = ContentTestDocuments::formContent();
    const QByteArray formWithoutResourcesContent = ContentTestDocuments::formWithoutResourcesContent();

    // The first Do of the page: its range is of the page before and after the form; the ranges
    // inside of the form are of the content of the form (the stream 0, offsets of its data)
    // (the first q is the first byte of the page: it ends before " /Fm1" of the only "q /Fm1")
    const PDFInteger endOfFirstSave = offsetAfter(pageContent, "q /Fm1") - PDFInteger(qstrlen(" /Fm1"));
    QCOMPARE(endOfFirstSave, PDFInteger(1));
    const Recorder::Range expectedDo = Recorder::makeRange(0, endOfFirstSave, 0, offsetAfter(pageContent, "q /Fm1 Do"));
    const Recorder::Range expectedFormState = Recorder::makeRange(0, 0, 0, offsetAfter(formContent, "/GF gs"));
    const Recorder::Range expectedFormFill = Recorder::makeRange(0, offsetAfter(formContent, "20 10 re"), 0, offsetAfter(formContent, "re f"));
    QCOMPARE(recorder.events[2].text, QByteArray("Do"));
    QVERIFY2(recorder.events[2].range == expectedDo, qPrintable(Recorder::toString(recorder.events[2].range)));
    QVERIFY2(recorder.events[3].range == expectedFormState, qPrintable(Recorder::toString(recorder.events[3].range)));
    QVERIFY2(recorder.events[9].range == expectedFormFill, qPrintable(Recorder::toString(recorder.events[9].range)));
    QCOMPARE(recorder.events[11].kind, Recorder::EventKind::After);
    QCOMPARE(recorder.events[11].text, QByteArray("Do"));
    QVERIFY2(recorder.events[11].range == expectedDo, qPrintable(Recorder::toString(recorder.events[11].range)));

    // Inside of the form the data are those of the form, one stream
    QCOMPARE(recorder.events[3].streamData.size(), size_t(1));
    QCOMPARE(recorder.events[3].streamData[0], formContent);
    QCOMPARE(recorder.events.back().kind, Recorder::EventKind::After);
    QCOMPARE(recorder.events[recorder.events.size() - 2].streamData[0], formWithoutResourcesContent);

    // The rectangles of the forms: /Fm1 twice with its matrix [2 0 0 2 10 20] (the second one
    // moved by 100) and the line width 4 of its resource, /Fm2 with the line width 6 of the
    // resource of the page
    QCOMPARE(recorder.paths.size(), size_t(3));
    QCOMPARE(recorder.paths[0].bounds, QRectF(5, 5, 20, 10));
    QCOMPARE(recorder.paths[0].matrix, QTransform(2, 0, 0, 2, 10, 20));
    QCOMPARE(recorder.paths[0].lineWidth, 4.0);
    QCOMPARE(recorder.paths[0].level, PDFInteger(1));
    QVERIFY(isSameColor(recorder.paths[0].fillColor, 0, 255, 0));
    QCOMPARE(recorder.paths[1].bounds, QRectF(5, 5, 20, 10));
    QCOMPARE(recorder.paths[1].matrix, QTransform(2, 0, 0, 2, 110, 20));
    QCOMPARE(recorder.paths[2].bounds, QRectF(0, 0, 10, 10));
    QCOMPARE(recorder.paths[2].matrix, QTransform());
    QCOMPARE(recorder.paths[2].lineWidth, 6.0);
    QVERIFY(isSameColor(recorder.paths[2].fillColor, 255, 0, 0));

    QCOMPARE(recorder.nestingLevel(), PDFInteger(0));
    // After the processing: the last operation of the page, from the end of the second Q
    QVERIFY(recorder.currentRange() == Recorder::makeRange(0, offsetAfter(pageContent, "cm /Fm1 Do Q"), 0, offsetAfter(pageContent, "/Fm2 Do")));
}

void ContentProcessorTest::nestingLevelOfTile()
{
    ContentProcessorRun run(ContentTestDocuments::createTilingPatternDocument());
    const Recorder& recorder = *run.processor;
    QVERIFY2(run.errors.isEmpty(), qPrintable(run.errorText()));

    // The filled square shows four cells of the pattern (see createTilingPatternDocument): the
    // operators of each tile have the level 1, between the two calls of the operator f of the page
    const QStringList tile = pairs(1, { "gs", "rg", "re", "f" });
    QStringList expected;
    expected << pairs(0, { "cs", "scn", "re" }) << QStringLiteral("0:B:f") << tile << tile << tile << tile << QStringLiteral("0:A:f");
    QCOMPARE(recorder.trace(), expected);

    // The range inside of a tile is of the content of the pattern
    const QByteArray patternContent = ContentTestDocuments::tilingPatternContent();
    const QByteArray pageContent = ContentTestDocuments::tilingPatternPageContent();
    const Recorder::Range expectedFill = Recorder::makeRange(0, offsetAfter(pageContent, "40 40 re"), 0, offsetAfter(pageContent, "re f"));
    QVERIFY(recorder.events[7].range == Recorder::makeRange(0, 0, 0, offsetAfter(patternContent, "/GT gs")));
    QCOMPARE(recorder.events[7].streamData.size(), size_t(1));
    QCOMPARE(recorder.events[7].streamData[0], patternContent);
    QVERIFY(recorder.events[6].range == expectedFill);
    QVERIFY(recorder.events.back().range == expectedFill);

    // Four red cells 10 x 10 with the lower left corners (25, 25), (45, 25), (25, 45), (45, 45)
    // of the page and the line width 2 of the resource of the pattern
    QCOMPARE(recorder.paths.size(), size_t(4));
    std::set<std::pair<int, int>> origins;
    for (size_t i = 0; i < recorder.paths.size(); ++i)
    {
        const Recorder::PaintedPath& path = recorder.paths[i];
        QCOMPARE(path.level, PDFInteger(1));

        // The path of a cell is its own rectangle, also the one of the first cell: the content of
        // a cell starts with an empty current path, not with the square being filled (8.7.3.1)
        QCOMPARE(path.bounds, QRectF(0, 0, 10, 10));
        QCOMPARE(path.path.elementCount(), 5);
        QCOMPARE(path.lineWidth, 2.0);
        QVERIFY(isSameColor(path.fillColor, 255, 0, 0));
        QCOMPARE(path.matrix.m11(), 1.0);
        QCOMPARE(path.matrix.m22(), 1.0);
        origins.insert(std::make_pair(qRound(path.matrix.dx()), qRound(path.matrix.dy())));
        COMPARE_NEAR(path.matrix.dx(), qRound(path.matrix.dx()));
        COMPARE_NEAR(path.matrix.dy(), qRound(path.matrix.dy()));
    }
    const std::set<std::pair<int, int>> expectedOrigins = { { 25, 25 }, { 45, 25 }, { 25, 45 }, { 45, 45 } };
    QVERIFY(origins == expectedOrigins);
}

void ContentProcessorTest::nestingLevelOfGlyph()
{
    ContentProcessorRun run(ContentTestDocuments::createType3Document("BT /T3 10 Tf 20 100 Td (A) Tj ET", ContentTestDocuments::Type3Variant::Plain));
    const Recorder& recorder = *run.processor;
    QVERIFY2(run.errors.isEmpty(), qPrintable(run.errorText()));

    QStringList expected;
    expected << pairs(0, { "BT", "Tf", "Td" }) << QStringLiteral("0:B:Tj") << QStringLiteral("0:G+65") << pairs(1, { "d0", "re", "f" })
             << QStringLiteral("0:G-") << QStringLiteral("0:A:Tj") << pairs(0, { "ET" });
    QCOMPARE(recorder.trace(), expected);

    // The range inside of the glyph procedure is of the procedure
    const QByteArray procedure = ContentTestDocuments::type3SquareProcedure();
    QCOMPARE(recorder.events[8].text, QByteArray("d0"));
    QVERIFY(recorder.events[8].range == Recorder::makeRange(0, 0, 0, offsetAfter(procedure, "60 0 d0")));
    QCOMPARE(recorder.events[8].streamData.size(), size_t(1));
    QCOMPARE(recorder.events[8].streamData[0], procedure);
}

void ContentProcessorTest::contentGivenByTheCaller()
{
    // The public processForm with a content of the caller (the way a form or a cell of a pattern
    // is processed on its own): the content has the level 0 and its ranges are of the given
    // bytes; a form drawn by its Do has the level 1
    const QByteArray content = "1 0 0 rg 0 0 10 10 re f\n/Fm2 Do\n";

    ContentProcessorRun run(ContentTestDocuments::createFormDocument(), false, false);
    Recorder& recorder = *run.processor;
    recorder.initializeProcessor();
    recorder.processForm(QTransform(1, 0, 0, 1, 30, 40), QRectF(), PDFObject(), PDFObject(), content, 0);
    QVERIFY(recorder.renderErrors().isEmpty());

    QStringList expected;
    expected << pairs(0, { "rg", "re", "f" }) << QStringLiteral("0:B:Do") << pairs(1, { "gs", "rg", "re", "f" }) << QStringLiteral("0:A:Do");
    QCOMPARE(recorder.trace(), expected);

    QVERIFY(recorder.events[0].range == Recorder::makeRange(0, 0, 0, offsetAfter(content, "1 0 0 rg")));
    QVERIFY(recorder.events[4].range == Recorder::makeRange(0, offsetAfter(content, "10 10 re"), 0, offsetAfter(content, "re f")));
    QVERIFY(recorder.events[6].range == Recorder::makeRange(0, offsetAfter(content, "re f"), 0, offsetAfter(content, "/Fm2 Do")));
    QCOMPARE(recorder.events[0].streamData.size(), size_t(1));
    QCOMPARE(recorder.events[0].streamData[0], content);

    // The matrix of the caller is the matrix of the content
    QCOMPARE(recorder.paths.size(), size_t(2));
    QCOMPARE(recorder.paths[0].bounds, QRectF(0, 0, 10, 10));
    QCOMPARE(recorder.paths[0].matrix, QTransform(1, 0, 0, 1, 30, 40));
    QCOMPARE(recorder.paths[0].level, PDFInteger(0));
    QCOMPARE(recorder.paths[1].level, PDFInteger(1));
    QCOMPARE(recorder.nestingLevel(), PDFInteger(0));
}

void ContentProcessorTest::inlineImageWithEndMarkerInData()
{
    const ContentTestDocuments::InlineImageVariant variant = ContentTestDocuments::InlineImageVariant::EndMarkerInData;
    const QByteArray content = ContentTestDocuments::inlineImagePageContent(variant);
    QCOMPARE(content, QByteArray("q 20 0 0 20 10 10 cm\nBI /W 2 /H 2 /BPC 8 /CS /G ID aEIb\nEI\nQ\n0 0 5 5 re f\n"));

    ContentProcessorRun run(ContentTestDocuments::createInlineImageDocument(variant));
    const Recorder& recorder = *run.processor;
    QVERIFY2(run.errors.isEmpty(), qPrintable(run.errorText()));

    // The inline image is one instruction with the text BI; the operators BI, ID and EI of the
    // table of the operators are never told
    QCOMPARE(recorder.trace(), pairs(0, { "q", "cm", "BI", "Q", "re", "f" }));
    for (const Recorder::Event& event : recorder.events)
    {
        QVERIFY(event.op != PDFPageContentProcessor::Operator::InlineImageBegin);
        QVERIFY(event.op != PDFPageContentProcessor::Operator::InlineImageData);
        QVERIFY(event.op != PDFPageContentProcessor::Operator::InlineImageEnd);
        QCOMPARE(event.op == PDFPageContentProcessor::Operator::InlineImage, event.text == "BI");
    }

    // The offsets counted from the content: the data are the four bytes after "ID " (8 bits,
    // one component, 2 x 2 samples), the real EI is the one after the new line
    const PDFInteger endOfMatrix = offsetAfter(content, "cm");
    const PDFInteger endOfBegin = offsetAfter(content, "BI");
    const PDFInteger dataOperator = offsetAfter(content, "ID") - 2;
    const PDFInteger endOperator = offsetAfter(content, "\nEI") - 2;
    QCOMPARE(content.mid(dataOperator + 3, 4), ContentTestDocuments::inlineImageData(variant));
    QCOMPARE(endOperator, dataOperator + 3 + 4 + 1);

    const Recorder::Range expectedRange = Recorder::makeRange(0, endOfMatrix, 0, endOperator + 2);
    const Recorder::ImageRange expectedImageRange = Recorder::makeImageRange(endOfBegin, dataOperator, dataOperator + 3, 4, endOperator);
    for (const size_t index : { size_t(4), size_t(5) })
    {
        const Recorder::Event& event = recorder.events[index];
        QCOMPARE(event.text, QByteArray("BI"));
        QVERIFY2(event.range == expectedRange, qPrintable(Recorder::toString(event.range) + QStringLiteral(" != ") + Recorder::toString(expectedRange)));
    }
    QVERIFY(recorder.events[4].imageRange.isValid());
    QVERIFY2(recorder.events[4].imageRange == expectedImageRange, qPrintable(Recorder::toString(recorder.events[4].imageRange) + QStringLiteral(" != ") + Recorder::toString(expectedImageRange)));

    // The bytes of the operation: from the end of cm to the end of the real EI
    QCOMPARE(content.mid(expectedRange.beginOffset, expectedRange.endOffset - expectedRange.beginOffset), QByteArray("\nBI /W 2 /H 2 /BPC 8 /CS /G ID aEIb\nEI"));

    // The next operator begins after the real EI
    QCOMPARE(recorder.events[6].text, QByteArray("Q"));
    QVERIFY(recorder.events[6].range == Recorder::makeRange(0, endOperator + 2, 0, offsetAfter(content, "EI\nQ")));
    QVERIFY(!recorder.events[6].imageRange.isValid());

    // The image got its four bytes, with the EI among them, and the matrix of cm
    QCOMPARE(recorder.imageData.size(), size_t(1));
    QCOMPARE(recorder.imageData[0], QByteArray("aEIb"));
    QCOMPARE(recorder.events[4].matrix, QTransform(20, 0, 0, 20, 10, 10));

    // The rectangle after the image is painted
    QCOMPARE(recorder.paths.size(), size_t(1));
    QCOMPARE(recorder.paths[0].bounds, QRectF(0, 0, 5, 5));
}

void ContentProcessorTest::inlineImageWithNamedColorSpace()
{
    // The color space /Cs1 of the resources has three components: the data are 1 x 2 x 3 = 6
    // bytes with an EI in the middle (a reader, which takes one component, ends the image there)
    const ContentTestDocuments::InlineImageVariant variant = ContentTestDocuments::InlineImageVariant::NamedColorSpace;
    const QByteArray content = ContentTestDocuments::inlineImagePageContent(variant);
    QCOMPARE(content, QByteArray("q 20 0 0 20 10 10 cm\nBI /W 1 /H 2 /BPC 8 /CS /Cs1 ID abEIcd\nEI\nQ\n0 0 5 5 re f\n"));

    ContentProcessorRun run(ContentTestDocuments::createInlineImageDocument(variant));
    const Recorder& recorder = *run.processor;
    QVERIFY2(run.errors.isEmpty(), qPrintable(run.errorText()));
    QCOMPARE(recorder.trace(), pairs(0, { "q", "cm", "BI", "Q", "re", "f" }));

    const PDFInteger dataOperator = offsetAfter(content, "ID") - 2;
    const PDFInteger endOperator = offsetAfter(content, "\nEI") - 2;
    const Recorder::ImageRange expectedImageRange = Recorder::makeImageRange(offsetAfter(content, "BI"), dataOperator, dataOperator + 3, 6, endOperator);
    QVERIFY2(recorder.events[4].imageRange == expectedImageRange, qPrintable(Recorder::toString(recorder.events[4].imageRange) + QStringLiteral(" != ") + Recorder::toString(expectedImageRange)));
    QVERIFY(recorder.events[4].range == Recorder::makeRange(0, offsetAfter(content, "cm"), 0, endOperator + 2));

    QCOMPARE(recorder.imageData.size(), size_t(1));
    QCOMPARE(recorder.imageData[0], QByteArray("abEIcd"));
    QCOMPARE(recorder.paths.size(), size_t(1));
}

void ContentProcessorTest::inlineImagePlain()
{
    const ContentTestDocuments::InlineImageVariant variant = ContentTestDocuments::InlineImageVariant::Plain;
    const QByteArray content = ContentTestDocuments::inlineImagePageContent(variant);

    ContentProcessorRun run(ContentTestDocuments::createInlineImageDocument(variant));
    const Recorder& recorder = *run.processor;
    QVERIFY2(run.errors.isEmpty(), qPrintable(run.errorText()));
    QCOMPARE(recorder.trace(), pairs(0, { "q", "cm", "BI", "Q", "re", "f" }));

    const PDFInteger dataOperator = offsetAfter(content, "ID") - 2;
    const PDFInteger endOperator = offsetAfter(content, "EI") - 2;
    QVERIFY(recorder.events[4].imageRange == Recorder::makeImageRange(offsetAfter(content, "BI"), dataOperator, dataOperator + 3, 4, endOperator));
    QCOMPARE(recorder.imageData.size(), size_t(1));
    QCOMPARE(recorder.imageData[0], QByteArray("abcd"));
}

void ContentProcessorTest::inlineImageWithoutData()
{
    // No operator ID: the dictionary of the image cannot be read. The operands, which stay after
    // the failure, are consumed by the operator q.
    const QByteArray content = "0 0 1 rg\n"
                               "BI /W 2 /H 2 /BPC 8 /CS /G\n"
                               "q\n"
                               "1 0 0 rg 10 10 50 50 re f\n"
                               "Q\n";

    ContentProcessorRun run(ContentTestDocuments::createDocument(content, &ContentTestDocuments::createNoResources));
    const Recorder& recorder = *run.processor;

    // The call before the instruction, its failure, the call after it; then the rest of the content
    QStringList expected;
    expected << pairs(0, { "rg" }) << QStringLiteral("0:B:BI") << QStringLiteral("0:F:BI") << QStringLiteral("0:A:BI") << pairs(0, { "q", "rg", "re", "f", "Q" });
    QCOMPARE(recorder.trace(), expected);

    // The range ends right after BI, the parts of the image are not known
    const Recorder::Range expectedRange = Recorder::makeRange(0, offsetAfter(content, "0 0 1 rg"), 0, offsetAfter(content, "BI"));
    for (const size_t index : { size_t(2), size_t(3), size_t(4) })
    {
        const Recorder::Event& event = recorder.events[index];
        QCOMPARE(event.op, PDFPageContentProcessor::Operator::InlineImage);
        QVERIFY2(event.range == expectedRange, qPrintable(Recorder::toString(event.range) + QStringLiteral(" != ") + Recorder::toString(expectedRange)));
        QVERIFY(!event.imageRange.isValid());
    }

    // The error is in the error list, as before the change, and it is the one of the failure
    QCOMPARE(run.errors.size(), 1);
    QCOMPARE(run.errors[0].type, RenderErrorType::Error);
    QVERIFY(!run.errors[0].message.isEmpty());
    QCOMPARE(recorder.events[3].message, run.errors[0].message);

    // The next operation begins after BI; no image, the red rectangle is painted
    QVERIFY(recorder.events[5].range == Recorder::makeRange(0, offsetAfter(content, "BI"), 0, offsetAfter(content, "/G\nq")));
    QVERIFY(recorder.imageData.empty());
    QCOMPARE(recorder.paths.size(), size_t(1));
    QCOMPARE(recorder.paths[0].bounds, QRectF(10, 10, 50, 50));
    QVERIFY(isSameColor(recorder.paths[0].fillColor, 255, 0, 0));
}

void ContentProcessorTest::failingOperators()
{
    // A successful operator, a missing resource, an operand of a wrong type, a restore without a
    // save, an unknown operator and a successful operator again
    const QByteArray content = "1 w\n"
                               "/Missing gs\n"
                               "/Name w\n"
                               "Q\n"
                               "xyz\n"
                               "2 w\n";

    ContentProcessorRun run(ContentTestDocuments::createDocument(content, [](PDFDocumentBuilder* builder)
    {
        Q_UNUSED(builder);

        // The resources have graphic states, but not the one of the content
        PDFDictionaryBuilder resources;
        resources.addEntry(PDFInplaceOrMemoryString("ExtGState"), ContentTestDocuments::createDictionaryObject({ { "Other", ContentTestDocuments::createDictionaryObject({ { "LW", PDFObject::createInteger(9) } }) } }));
        return resources;
    }));
    const Recorder& recorder = *run.processor;

    // The failure is told between the two calls of the operator; an unknown operator is the
    // operator Invalid and not a failure
    QStringList expected;
    expected << pairs(0, { "w" })
             << QStringLiteral("0:B:gs") << QStringLiteral("0:F:gs") << QStringLiteral("0:A:gs")
             << QStringLiteral("0:B:w") << QStringLiteral("0:F:w") << QStringLiteral("0:A:w")
             << QStringLiteral("0:B:Q") << QStringLiteral("0:F:Q") << QStringLiteral("0:A:Q")
             << pairs(0, { "xyz", "w" });
    QCOMPARE(recorder.trace(), expected);

    // The errors of the list: the three failures and the unknown operator, in the order of the content
    QCOMPARE(run.errors.size(), 4);
    const std::vector<Recorder::Event> failed = recorder.eventsOfKind(Recorder::EventKind::Failed);
    QCOMPARE(failed.size(), size_t(3));
    QCOMPARE(failed[0].op, PDFPageContentProcessor::Operator::SetGraphicState);
    QCOMPARE(failed[1].op, PDFPageContentProcessor::Operator::SetLineWidth);
    QCOMPARE(failed[2].op, PDFPageContentProcessor::Operator::RestoreGraphicState);
    for (size_t i = 0; i < failed.size(); ++i)
    {
        QVERIFY(!failed[i].message.isEmpty());
        QCOMPARE(failed[i].message, run.errors[qsizetype(i)].message);
    }
    QVERIFY(failed[0].message.contains(QStringLiteral("Missing")));
    QVERIFY(run.errors[3].message.contains(QStringLiteral("xyz")));

    // The range of a failure is the range of its operator
    QVERIFY(failed[0].range == Recorder::makeRange(0, offsetAfter(content, "1 w"), 0, offsetAfter(content, "/Missing gs")));
    QVERIFY(failed[2].range == Recorder::makeRange(0, offsetAfter(content, "/Name w"), 0, offsetAfter(content, "Q")));

    // The unknown operator
    QCOMPARE(recorder.events[11].text, QByteArray("xyz"));
    QCOMPARE(recorder.events[11].op, PDFPageContentProcessor::Operator::Invalid);
    QCOMPARE(recorder.events[12].op, PDFPageContentProcessor::Operator::Invalid);

    // The refused operators changed nothing: the line width is 1 until the last operator sets 2
    QCOMPARE(recorder.events[13].lineWidth, 1.0);
    QCOMPARE(recorder.events[14].lineWidth, 2.0);
}

void ContentProcessorTest::failingOperatorOfForm()
{
    // The failure of an operator of a nested content is told too, with the level and the range
    // of that content: the form has no graphic state /Missing
    const QByteArray formContent = "/Missing gs 1 0 0 rg 0 0 10 10 re f";
    const QByteArray pageContent = "/Fm1 Do 0 0 5 5 re f";

    ContentProcessorRun run(ContentTestDocuments::createDocument(pageContent, [&formContent](PDFDocumentBuilder* builder)
    {
        PDFObject form = ContentTestDocuments::addStreamObject(builder, { { "Type", PDFObject::createName("XObject") },
                                                                          { "Subtype", PDFObject::createName("Form") },
                                                                          { "BBox", ContentTestDocuments::createIntegerArrayObject({ 0, 0, 10, 10 }) },
                                                                          { "Resources", ContentTestDocuments::createDictionaryObject({ { "ExtGState", ContentTestDocuments::createDictionaryObject({ { "Other", ContentTestDocuments::createDictionaryObject({ { "LW", PDFObject::createInteger(9) } }) } }) } }) } }, formContent);

        PDFDictionaryBuilder resources;
        resources.addEntry(PDFInplaceOrMemoryString("XObject"), ContentTestDocuments::createDictionaryObject({ { "Fm1", form } }));
        return resources;
    }));
    const Recorder& recorder = *run.processor;

    QStringList expected;
    expected << QStringLiteral("0:B:Do") << QStringLiteral("1:B:gs") << QStringLiteral("1:F:gs") << QStringLiteral("1:A:gs") << pairs(1, { "rg", "re", "f" })
             << QStringLiteral("0:A:Do") << pairs(0, { "re", "f" });
    QCOMPARE(recorder.trace(), expected);

    QCOMPARE(run.errors.size(), 1);
    const Recorder::Event& failure = recorder.events[2];
    QCOMPARE(failure.op, PDFPageContentProcessor::Operator::SetGraphicState);
    QCOMPARE(failure.message, run.errors[0].message);
    QCOMPARE(failure.level, PDFInteger(1));
    QVERIFY(failure.range == Recorder::makeRange(0, 0, 0, offsetAfter(formContent, "/Missing gs")));

    // The rest of the form and of the page is processed
    QCOMPARE(recorder.paths.size(), size_t(2));
    QCOMPARE(recorder.paths[0].bounds, QRectF(0, 0, 10, 10));
    QCOMPARE(recorder.paths[0].level, PDFInteger(1));
    QCOMPARE(recorder.paths[1].bounds, QRectF(0, 0, 5, 5));
    QCOMPARE(recorder.paths[1].level, PDFInteger(0));
}

void ContentProcessorTest::type3GlyphProcedure()
{
    // Two visible glyphs, an invisible text (3 Tr) and a code without a glyph procedure (C = 67)
    const QByteArray content = "BT /T3 10 Tf 20 100 Td (AB) Tj 3 Tr (A) Tj 0 Tr (C) Tj ET";

    ContentProcessorRun run(ContentTestDocuments::createType3Document(content, ContentTestDocuments::Type3Variant::Plain));
    const Recorder& recorder = *run.processor;

    // The glyph without a procedure is a warning, nothing else
    QCOMPARE(run.errors.size(), 1);
    QCOMPARE(run.errors[0].type, RenderErrorType::Warning);

    // The calls are paired around the instructions of each processed procedure, which have the
    // level 1; the invisible text and the code without a procedure have none
    QStringList expected;
    expected << pairs(0, { "BT", "Tf", "Td" }) << QStringLiteral("0:B:Tj")
             << QStringLiteral("0:G+65") << pairs(1, { "d0", "re", "f" }) << QStringLiteral("0:G-")
             << QStringLiteral("0:G+66") << pairs(1, { "d1", "re", "f" }) << QStringLiteral("0:G-")
             << QStringLiteral("0:A:Tj") << pairs(0, { "Tr", "Tj", "Tr", "Tj", "ET" });
    QCOMPARE(recorder.trace(), expected);

    // The code, the bytes of the procedure and the font of each call
    const std::vector<Recorder::Event> begins = recorder.eventsOfKind(Recorder::EventKind::GlyphBegin);
    QCOMPARE(begins.size(), size_t(2));
    QCOMPARE(begins[0].code, 65u);
    QCOMPARE(begins[0].text, ContentTestDocuments::type3SquareProcedure());
    QVERIFY(begins[0].isFontOfState);
    QCOMPARE(begins[1].code, 66u);
    QCOMPARE(begins[1].text, ContentTestDocuments::type3BarProcedure());
    QVERIFY(begins[1].isFontOfState);
    QCOMPARE(recorder.eventsOfKind(Recorder::EventKind::GlyphEnd).size(), size_t(2));

    // The marks of the glyphs: the glyph space is 0.01 * 10 of the text space (the font matrix
    // and the font size), the second glyph is moved by the width of the first one, 0.6 * 10
    QCOMPARE(recorder.paths.size(), size_t(2));
    QCOMPARE(recorder.paths[0].bounds, QRectF(0, 0, 50, 50));
    QCOMPARE(recorder.paths[0].level, PDFInteger(1));
    COMPARE_NEAR(recorder.paths[0].matrix.m11(), 0.1);
    COMPARE_NEAR(recorder.paths[0].matrix.m22(), 0.1);
    COMPARE_NEAR(recorder.paths[0].matrix.dx(), 20.0);
    COMPARE_NEAR(recorder.paths[0].matrix.dy(), 100.0);
    QCOMPARE(recorder.paths[1].bounds, QRectF(0, 0, 20, 100));
    COMPARE_NEAR(recorder.paths[1].matrix.dx(), 26.0);
    COMPARE_NEAR(recorder.paths[1].matrix.dy(), 100.0);

    // The text went on by the width of every code (9.4.4), whether its glyph was painted or
    // not: 20 + 6 + 8 after (AB), + 6 after the invisible (A), + 0.7 * 10 after the code 67
    // without a procedure
    std::vector<PDFReal> positions;
    for (const Recorder::Event& event : recorder.events)
    {
        if (event.kind == Recorder::EventKind::After && event.text == "Tj")
        {
            positions.push_back(event.textMatrix.dx());
        }
    }
    QCOMPARE(positions.size(), size_t(3));
    COMPARE_NEAR(positions[0], 34.0);
    COMPARE_NEAR(positions[1], 40.0);
    COMPARE_NEAR(positions[2], 47.0);
}

void ContentProcessorTest::type3GlyphWithTextObject()
{
    // The procedure of the glyph A paints its square and has a text object of its own, which
    // shows the glyph B of the same font at (30, 40) of its glyph space (ISO 32000-2, 9.6.4: a
    // glyph procedure is a content stream; the text object of the page is not its text object)
    ContentProcessorRun run(ContentTestDocuments::createType3Document("BT /T3 10 Tf 20 100 Td (AB) Tj ET", ContentTestDocuments::Type3Variant::InnerText));
    const Recorder& recorder = *run.processor;

    // No error and no failed operator: BT of the procedure is not a second BT of the page
    QVERIFY2(run.errors.isEmpty(), qPrintable(run.errorText()));

    QStringList expected;
    expected << pairs(0, { "BT", "Tf", "Td" }) << QStringLiteral("0:B:Tj")
             << QStringLiteral("0:G+65") << pairs(1, { "d0", "re", "f", "BT", "Tf", "Td" }) << QStringLiteral("1:B:Tj")
             << QStringLiteral("1:G+66") << pairs(2, { "d1", "re", "f" }) << QStringLiteral("1:G-")
             << QStringLiteral("1:A:Tj") << pairs(1, { "ET" }) << QStringLiteral("0:G-")
             << QStringLiteral("0:G+66") << pairs(1, { "d1", "re", "f" }) << QStringLiteral("0:G-")
             << QStringLiteral("0:A:Tj") << pairs(0, { "ET" });
    QCOMPARE(recorder.trace(), expected);

    // The text object of the page is open around the glyph (the calls of its begin and of its
    // end are told in the state of the page); the procedure starts without a text object and
    // has one only between its BT and its ET
    QCOMPARE(recorder.events[6].text, QByteArray("Tj"));
    QVERIFY(recorder.events[6].isTextObjectOpen);
    QCOMPARE(recorder.events[7].kind, Recorder::EventKind::GlyphBegin);
    QVERIFY(recorder.events[7].isTextObjectOpen);
    for (size_t index = 8; index <= 14; ++index)
    {
        // d0, re, f and the call before BT
        QCOMPARE(recorder.events[index].level, PDFInteger(1));
        QVERIFY(!recorder.events[index].isTextObjectOpen);
    }
    QCOMPARE(recorder.events[15].text, QByteArray("BT"));
    QCOMPARE(recorder.events[15].kind, Recorder::EventKind::After);
    QVERIFY(recorder.events[15].isTextObjectOpen);
    const Recorder::Event* innerEnd = nullptr;
    const Recorder::Event* outerGlyphEnd = nullptr;
    for (const Recorder::Event& event : recorder.events)
    {
        if (!innerEnd && event.kind == Recorder::EventKind::After && event.text == "ET" && event.level == 1)
        {
            innerEnd = &event;
        }
        if (!outerGlyphEnd && event.kind == Recorder::EventKind::GlyphEnd && event.level == 0)
        {
            outerGlyphEnd = &event;
        }
    }
    QVERIFY(innerEnd && outerGlyphEnd);
    QVERIFY(!innerEnd->isTextObjectOpen);
    QVERIFY(outerGlyphEnd->isTextObjectOpen);
    QVERIFY(recorder.lastEvent(Recorder::EventKind::After, "Tj")->isTextObjectOpen);
    QVERIFY(!recorder.events.back().isTextObjectOpen);

    // Two text objects were begun and two were ended
    QCOMPARE(recorder.textBeginCount, 2);
    QCOMPARE(recorder.textEndCount, 2);

    // The text of the procedure is shown: the glyph space of the outer glyph is 0.1 of the
    // page at (20, 100); the inner text begins at its (30, 40), which is (23, 104) of the page,
    // and the glyph space of the inner glyph is 0.01 * 1 of the outer one
    QCOMPARE(recorder.paths.size(), size_t(3));
    QCOMPARE(recorder.paths[0].bounds, QRectF(0, 0, 50, 50));
    QCOMPARE(recorder.paths[0].level, PDFInteger(1));
    COMPARE_NEAR(recorder.paths[0].matrix.dx(), 20.0);
    QCOMPARE(recorder.paths[1].bounds, QRectF(0, 0, 20, 100));
    QCOMPARE(recorder.paths[1].level, PDFInteger(2));
    COMPARE_NEAR(recorder.paths[1].matrix.m11(), 0.001);
    COMPARE_NEAR(recorder.paths[1].matrix.m22(), 0.001);
    COMPARE_NEAR(recorder.paths[1].matrix.dx(), 23.0);
    COMPARE_NEAR(recorder.paths[1].matrix.dy(), 104.0);
    QCOMPARE(recorder.paths[2].bounds, QRectF(0, 0, 20, 100));
    QCOMPARE(recorder.paths[2].level, PDFInteger(1));
    COMPARE_NEAR(recorder.paths[2].matrix.m11(), 0.1);
    COMPARE_NEAR(recorder.paths[2].matrix.dx(), 26.0);
    COMPARE_NEAR(recorder.paths[2].matrix.dy(), 100.0);

    // The text of the page went on by the widths of its glyphs: 20 + 6 + 8
    COMPARE_NEAR(recorder.lastEvent(Recorder::EventKind::After, "Tj")->textMatrix.dx(), 34.0);
    COMPARE_NEAR(recorder.lastEvent(Recorder::EventKind::After, "Tj")->textMatrix.dy(), 100.0);
}

void ContentProcessorTest::type3GlyphWithUnclosedTextObject()
{
    // The procedure of the glyph A does not end its text object. The text object ends with the
    // procedure; the text object of the page goes on and a later text object is as any other.
    const QByteArray content = "BT /T3 10 Tf 20 100 Td (AAB) Tj ET\n"
                               "BT /T3 10 Tf 20 50 Td (B) Tj ET\n";

    ContentProcessorRun run(ContentTestDocuments::createType3Document(content, ContentTestDocuments::Type3Variant::UnclosedText));
    const Recorder& recorder = *run.processor;

    // Exactly one error - of the procedure, though the glyph is shown twice - and it is not a
    // failure of an operator: neither BT of the procedure nor ET of the page fails
    QCOMPARE(run.errors.size(), 1);
    QCOMPARE(run.errors[0].type, RenderErrorType::Error);
    QVERIFY(!run.errors[0].message.isEmpty());
    QCOMPARE(recorder.eventsOfKind(Recorder::EventKind::Failed).size(), size_t(0));

    // The procedure has no call of ET, the page has its two
    const QStringList glyph = QStringList() << QStringLiteral("0:G+65") << pairs(1, { "d0", "re", "f", "BT", "Tf", "Td" }) << QStringLiteral("1:B:Tj")
                                            << QStringLiteral("1:G+66") << pairs(2, { "d1", "re", "f" }) << QStringLiteral("1:G-")
                                            << QStringLiteral("1:A:Tj") << QStringLiteral("0:G-");
    const QStringList bar = QStringList() << QStringLiteral("0:G+66") << pairs(1, { "d1", "re", "f" }) << QStringLiteral("0:G-");
    QStringList expected;
    expected << pairs(0, { "BT", "Tf", "Td" }) << QStringLiteral("0:B:Tj") << glyph << glyph << bar << QStringLiteral("0:A:Tj") << pairs(0, { "ET" })
             << pairs(0, { "BT", "Tf", "Td" }) << QStringLiteral("0:B:Tj") << bar << QStringLiteral("0:A:Tj") << pairs(0, { "ET" });
    QCOMPARE(recorder.trace(), expected);

    // Every text object, which was begun, was ended: two of the page and one of each of the
    // two processed procedures
    QCOMPARE(recorder.textBeginCount, 4);
    QCOMPARE(recorder.textEndCount, 4);

    // The text object of the page is open after each glyph and closed after its ET
    std::vector<Recorder::Event> pageGlyphEnds;
    for (const Recorder::Event& event : recorder.events)
    {
        if (event.kind == Recorder::EventKind::GlyphEnd && event.level == 0)
        {
            pageGlyphEnds.push_back(event);
        }
    }
    QCOMPARE(pageGlyphEnds.size(), size_t(4));
    for (const Recorder::Event& event : pageGlyphEnds)
    {
        QVERIFY(event.isTextObjectOpen);
    }
    QVERIFY(!recorder.events.back().isTextObjectOpen);

    // The glyphs of the page are at their positions: A at 20, A at 26, B at 32 (the widths 6,
    // 6), the text ends at 40; the B of the second text object is at (20, 50)
    std::vector<Recorder::Event> pageGlyphBegins;
    for (const Recorder::Event& event : recorder.events)
    {
        if (event.kind == Recorder::EventKind::GlyphBegin && event.level == 0)
        {
            pageGlyphBegins.push_back(event);
        }
    }
    QCOMPARE(pageGlyphBegins.size(), size_t(4));
    const std::vector<std::pair<unsigned int, QPointF>> expectedGlyphs = { { 65u, QPointF(20, 100) }, { 65u, QPointF(26, 100) }, { 66u, QPointF(32, 100) }, { 66u, QPointF(20, 50) } };
    for (size_t i = 0; i < expectedGlyphs.size(); ++i)
    {
        QCOMPARE(pageGlyphBegins[i].code, expectedGlyphs[i].first);
        COMPARE_NEAR(pageGlyphBegins[i].matrix.dx(), expectedGlyphs[i].second.x());
        COMPARE_NEAR(pageGlyphBegins[i].matrix.dy(), expectedGlyphs[i].second.y());
        COMPARE_NEAR(pageGlyphBegins[i].matrix.m11(), 0.1);
    }

    // The inner text of both procedures is shown
    QCOMPARE(recorder.paths.size(), size_t(6));
}

void ContentProcessorTest::type3GlyphWithEndOfTextOnly()
{
    // The procedure of the glyph A has an ET without a BT: it is an error of the procedure
    // and it does not end the text object of the page
    ContentProcessorRun run(ContentTestDocuments::createType3Document("BT /T3 10 Tf 20 100 Td (AB) Tj ET", ContentTestDocuments::Type3Variant::EndTextOnly));
    const Recorder& recorder = *run.processor;

    QStringList expected;
    expected << pairs(0, { "BT", "Tf", "Td" }) << QStringLiteral("0:B:Tj")
             << QStringLiteral("0:G+65") << pairs(1, { "d0", "re", "f" }) << QStringLiteral("1:B:ET") << QStringLiteral("1:F:ET") << QStringLiteral("1:A:ET") << QStringLiteral("0:G-")
             << QStringLiteral("0:G+66") << pairs(1, { "d1", "re", "f" }) << QStringLiteral("0:G-")
             << QStringLiteral("0:A:Tj") << pairs(0, { "ET" });
    QCOMPARE(recorder.trace(), expected);

    // The only error is the failure of the ET of the procedure; the ET of the page succeeds
    const std::vector<Recorder::Event> failed = recorder.eventsOfKind(Recorder::EventKind::Failed);
    QCOMPARE(failed.size(), size_t(1));
    QCOMPARE(failed[0].op, PDFPageContentProcessor::Operator::TextEnd);
    QCOMPARE(failed[0].level, PDFInteger(1));
    QCOMPARE(run.errors.size(), 1);
    QCOMPARE(run.errors[0].message, failed[0].message);

    // The text object of the page was begun once and ended once, by its own ET: it is open
    // after the glyph, the next glyph is at its position (20 + 6) and the text ends at 34
    QCOMPARE(recorder.textBeginCount, 1);
    QCOMPARE(recorder.textEndCount, 1);
    const std::vector<Recorder::Event> glyphEnds = recorder.eventsOfKind(Recorder::EventKind::GlyphEnd);
    QCOMPARE(glyphEnds.size(), size_t(2));
    QVERIFY(glyphEnds[0].isTextObjectOpen);
    QVERIFY(glyphEnds[1].isTextObjectOpen);
    const std::vector<Recorder::Event> glyphBegins = recorder.eventsOfKind(Recorder::EventKind::GlyphBegin);
    QCOMPARE(glyphBegins.size(), size_t(2));
    COMPARE_NEAR(glyphBegins[1].matrix.dx(), 26.0);
    const Recorder::Event* show = recorder.lastEvent(Recorder::EventKind::After, "Tj");
    QVERIFY(show && show->isTextObjectOpen);
    COMPARE_NEAR(show->textMatrix.dx(), 34.0);
    QVERIFY(!recorder.events.back().isTextObjectOpen);
}

void ContentProcessorTest::type3GlyphKeepsTextLineMatrix()
{
    // The text object of a procedure sets its own text matrices; those of the page are the
    // same after the glyph. T* moves to the next line from the origin of the line of the page
    // (9.4.2: the text line matrix (20, 100), the leading 12), not from the origin of the text
    // object of the procedure.
    ContentProcessorRun run(ContentTestDocuments::createType3Document("BT /T3 10 Tf 12 TL 20 100 Td (A) Tj T* (A) Tj ET", ContentTestDocuments::Type3Variant::InnerText));
    const Recorder& recorder = *run.processor;
    QVERIFY2(run.errors.isEmpty(), qPrintable(run.errorText()));

    std::vector<Recorder::Event> pageGlyphBegins;
    for (const Recorder::Event& event : recorder.events)
    {
        if (event.kind == Recorder::EventKind::GlyphBegin && event.level == 0)
        {
            pageGlyphBegins.push_back(event);
        }
    }
    QCOMPARE(pageGlyphBegins.size(), size_t(2));
    COMPARE_NEAR(pageGlyphBegins[0].matrix.dx(), 20.0);
    COMPARE_NEAR(pageGlyphBegins[0].matrix.dy(), 100.0);
    COMPARE_NEAR(pageGlyphBegins[1].matrix.dx(), 20.0);
    COMPARE_NEAR(pageGlyphBegins[1].matrix.dy(), 88.0);

    // The text matrix after each glyph of the page: the origin of its line moved by the width 6
    const Recorder::Event* show = recorder.lastEvent(Recorder::EventKind::After, "Tj");
    QVERIFY(show);
    COMPARE_NEAR(show->textMatrix.dx(), 26.0);
    COMPARE_NEAR(show->textMatrix.dy(), 88.0);
}

void ContentProcessorTest::type3GlyphKeepsTextClip()
{
    // The text of the page is filled and added to the clipping path (the text rendering mode 4):
    // a glyph of an ordinary font, then a glyph of the Type 3 font, whose procedure has a text
    // object. The clipping path of the page is applied once, at the ET of the page (9.3.6), with
    // the glyph of the ordinary font in it; the ET of the procedure neither applies nor
    // clears it.
    ContentProcessorRun run(ContentTestDocuments::createType3Document("BT 4 Tr /F1 10 Tf 20 100 Td (A) Tj /T3 10 Tf (A) Tj ET", ContentTestDocuments::Type3Variant::InnerText));
    const Recorder& recorder = *run.processor;
    QVERIFY2(run.errors.isEmpty(), qPrintable(run.errorText()));

    QCOMPARE(recorder.clippings.size(), size_t(1));
    const Recorder::Clipping& clipping = recorder.clippings[0];
    QCOMPARE(clipping.level, PDFInteger(0));

    // It comes inside of the ET of the page: the last event before it is the call before ET
    QVERIFY(clipping.eventCount > 0 && clipping.eventCount < recorder.events.size());
    QCOMPARE(recorder.events[clipping.eventCount - 1].text, QByteArray("ET"));
    QCOMPARE(recorder.events[clipping.eventCount - 1].kind, Recorder::EventKind::Before);
    QCOMPARE(recorder.events[clipping.eventCount - 1].level, PDFInteger(0));

    // The outline of the letter A of the size 10 at (20, 100): inside of its em square
    QVERIFY(!clipping.bounds.isEmpty());
    QVERIFY2(clipping.bounds.left() >= 19.0 && clipping.bounds.right() <= 30.0 && clipping.bounds.top() >= 98.0 && clipping.bounds.bottom() <= 110.0,
             qPrintable(QStringLiteral("%1 %2 %3 %4").arg(clipping.bounds.left()).arg(clipping.bounds.top()).arg(clipping.bounds.right()).arg(clipping.bounds.bottom())));
    QVERIFY(clipping.bounds.width() > 3.0 && clipping.bounds.height() > 3.0);
}

void ContentProcessorTest::type3GlyphShowingItself()
{
    // The procedure of the glyph A paints its square and shows the glyph A of its own font
    const QByteArray content = "BT /T3 10 Tf 20 100 Td (AA) Tj ET";

    ContentProcessorRun run(ContentTestDocuments::createType3Document(content, ContentTestDocuments::Type3Variant::SelfReferencing));
    const Recorder& recorder = *run.processor;

    // The processing ends: each of the two glyphs of the page is processed once, the glyph
    // inside of it is refused; inside of the procedure the text operators have the level 1
    const QStringList glyph = QStringList() << QStringLiteral("0:G+65") << pairs(1, { "d0", "re", "f", "BT", "Tf", "Tj", "ET" }) << QStringLiteral("0:G-");
    QStringList expected;
    expected << pairs(0, { "BT", "Tf", "Td" }) << QStringLiteral("0:B:Tj") << glyph << glyph << QStringLiteral("0:A:Tj") << pairs(0, { "ET" });
    QCOMPARE(recorder.trace(), expected);

    // No operator of the content fails: a glyph procedure is a content stream of its own and may
    // have a text object (ISO 32000-2, 9.6.4), though the text object of the page is open
    const std::vector<Recorder::Event> failed = recorder.eventsOfKind(Recorder::EventKind::Failed);
    QCOMPARE(failed.size(), size_t(0));

    for (const Recorder::Event& begin : recorder.eventsOfKind(Recorder::EventKind::GlyphBegin))
    {
        QCOMPARE(begin.text, ContentTestDocuments::type3SelfReferencingProcedure());
    }

    // The own marks of the glyph are painted once for each glyph shown by the page
    QCOMPARE(recorder.paths.size(), size_t(2));
    QCOMPARE(recorder.paths[0].bounds, QRectF(0, 0, 50, 50));
    QCOMPARE(recorder.paths[1].bounds, QRectF(0, 0, 50, 50));
    COMPARE_NEAR(recorder.paths[0].matrix.dx(), 20.0);
    COMPARE_NEAR(recorder.paths[1].matrix.dx(), 26.0);

    // The refusal is one error, though the glyph was refused twice, and the only error
    QCOMPARE(run.errors.size(), 1);
    QCOMPARE(run.errors[0].type, RenderErrorType::Error);

    // The text of the page went on by the widths of its two glyphs: 2 * 0.6 * 10
    const Recorder::Event* show = recorder.lastEvent(Recorder::EventKind::After, "Tj");
    QVERIFY(show);
    QCOMPARE(show->level, PDFInteger(0));
    COMPARE_NEAR(show->textMatrix.dx(), 32.0);
}

void ContentProcessorTest::type3GlyphChainOverTheLimit()
{
    // Ten distinct glyphs, each one shows the next one: more than the limit of the nesting
    const int limit = int(Recorder::GLYPH_NESTING_LIMIT);
    QCOMPARE(limit, 8);
    QVERIFY(ContentTestDocuments::TYPE3_CHAIN_LENGTH > limit);

    ContentProcessorRun run(ContentTestDocuments::createType3Document("BT /T3 10 Tf 20 100 Td (a) Tj ET", ContentTestDocuments::Type3Variant::Chain));
    const Recorder& recorder = *run.processor;

    // Exactly the first eight procedures are processed, each one level deeper; the ninth glyph is
    // not processed (so the tenth one is never reached)
    const std::vector<Recorder::Event> begins = recorder.eventsOfKind(Recorder::EventKind::GlyphBegin);
    QCOMPARE(begins.size(), size_t(limit));
    for (int i = 0; i < limit; ++i)
    {
        QCOMPARE(begins[size_t(i)].code, unsigned(ContentTestDocuments::TYPE3_CHAIN_FIRST_CHAR + i));
        QCOMPARE(begins[size_t(i)].text, ContentTestDocuments::type3ChainProcedure(i));
        QCOMPARE(begins[size_t(i)].level, PDFInteger(i));
    }
    QCOMPARE(recorder.eventsOfKind(Recorder::EventKind::GlyphEnd).size(), size_t(limit));

    // The squares of the processed glyphs: the glyph i paints [i 0 i+1 1] at the level i + 1
    QCOMPARE(recorder.paths.size(), size_t(limit));
    for (int i = 0; i < limit; ++i)
    {
        QCOMPARE(recorder.paths[size_t(i)].bounds, QRectF(i, 0, 1, 1));
        QCOMPARE(recorder.paths[size_t(i)].level, PDFInteger(i + 1));
    }

    // No operator fails: the text objects of the glyph procedures are their own (9.6.4)
    QCOMPARE(recorder.eventsOfKind(Recorder::EventKind::Failed).size(), size_t(0));

    // The refusal of the ninth glyph is one error and the only one. The page is processed to
    // its end.
    QCOMPARE(run.errors.size(), 1);
    QCOMPARE(run.errors[0].type, RenderErrorType::Error);
    QCOMPARE(recorder.trace().last(), QStringLiteral("0:A:ET"));
    QCOMPARE(recorder.nestingLevel(), PDFInteger(0));
}

void ContentProcessorTest::textItemCodes()
{
    // A simple font (Tj and TJ with a number), a Type 0 font with codes of two bytes and a Type 3 font
    const QByteArray content = "BT /F1 10 Tf 20 100 Td (A B) Tj [(A) -250 (B)] TJ /F0 10 Tf <00410020> Tj /T3 10 Tf (AB) Tj /FM 10 Tf <20418020> Tj ET";

    ContentProcessorRun run(ContentTestDocuments::createFontsDocument(content));
    const Recorder& recorder = *run.processor;
    QVERIFY2(run.errors.isEmpty(), qPrintable(run.errorText()));
    QCOMPARE(recorder.textSequences.size(), size_t(5));

    // <20418020> in the CMap with codes of one and of two bytes: 0x20 and 0x41 of one byte,
    // 0x8020 of two bytes; only the first one is the single-byte code 32
    const std::vector<Recorder::TextItem>& mixed = recorder.textSequences[4];
    QCOMPARE(mixed.size(), size_t(3));
    QCOMPARE(mixed[0].code, 0x20u);
    QCOMPARE(mixed[0].codeLength, 1u);
    QVERIFY(mixed[0].isWordSpace);
    QCOMPARE(mixed[1].code, 0x41u);
    QCOMPARE(mixed[1].codeLength, 1u);
    QVERIFY(!mixed[1].isWordSpace);
    QCOMPARE(mixed[2].code, 0x8020u);
    QCOMPARE(mixed[2].codeLength, 2u);
    QCOMPARE(mixed[2].cid, CID(0x8020));
    QVERIFY(!mixed[2].isWordSpace);

    // (A B): the bytes are the codes, the middle one is the single-byte code 32
    const std::vector<Recorder::TextItem>& simple = recorder.textSequences[0];
    QCOMPARE(simple.size(), size_t(3));
    QCOMPARE(simple[0].code, 65u);
    QCOMPARE(simple[1].code, 32u);
    QCOMPARE(simple[2].code, 66u);
    for (const Recorder::TextItem& item : simple)
    {
        QCOMPARE(item.codeLength, 1u);
        QVERIFY(item.isCharacter);
    }
    QVERIFY(!simple[0].isWordSpace);
    QVERIFY(simple[1].isWordSpace);
    QVERIFY(!simple[2].isWordSpace);

    // [(A) -250 (B)]: the number comes from no character code
    const std::vector<Recorder::TextItem>& array = recorder.textSequences[1];
    QCOMPARE(array.size(), size_t(3));
    QCOMPARE(array[0].code, 65u);
    QCOMPARE(array[0].codeLength, 1u);
    QVERIFY(!array[1].isCharacter);
    QCOMPARE(array[1].advance, -250.0);
    QCOMPARE(array[1].codeLength, 0u);
    QCOMPARE(array[1].code, 0u);
    QVERIFY(!array[1].isWordSpace);
    QCOMPARE(array[2].code, 66u);
    QCOMPARE(array[2].codeLength, 1u);

    // <00410020> in /Identity-H: two codes of two bytes, 0x0041 and 0x0020; the code is the CID.
    // The second one has the value 32 and the Unicode U+0020, but it is not the single-byte code 32.
    const std::vector<Recorder::TextItem>& composite = recorder.textSequences[2];
    QCOMPARE(composite.size(), size_t(2));
    QCOMPARE(composite[0].code, 0x41u);
    QCOMPARE(composite[0].codeLength, 2u);
    QCOMPARE(composite[0].cid, CID(0x41));
    QCOMPARE(composite[1].code, 0x20u);
    QCOMPARE(composite[1].codeLength, 2u);
    QCOMPARE(composite[1].cid, CID(0x20));
    QVERIFY(!composite[0].isWordSpace);
    QVERIFY(!composite[1].isWordSpace);

    // (AB) in the Type 3 font
    const std::vector<Recorder::TextItem>& type3 = recorder.textSequences[3];
    QCOMPARE(type3.size(), size_t(2));
    QCOMPARE(type3[0].code, 65u);
    QCOMPARE(type3[1].code, 66u);
    for (const Recorder::TextItem& item : type3)
    {
        QCOMPARE(item.codeLength, 1u);
        QVERIFY(item.isContentStream);
    }
}

void ContentProcessorTest::wordSpacing_data()
{
    QTest::addColumn<QByteArray>("font");
    QTest::addColumn<QByteArray>("show");
    QTest::addColumn<double>("horizontalScaling");
    QTest::addColumn<double>("characterSpacing");
    QTest::addColumn<double>("wordSpacing");
    QTest::addColumn<double>("expectedDifference");

    // ISO 32000-2, 9.4.4: tx = ((w0 - Tj / 1000) * Tfs + Tc + Tw) * Th for each glyph, with Tw only for
    // the single-byte character code 32 (9.3.3). The widths w0 of a font, which is not embedded,
    // come from a font of the system here, so each row compares the position after the text with
    // the position of the same text with Tc = Tw = 0: the difference is (glyphs * Tc + codes 32 * Tw) * Th.

    // /F1: 32 is the space. (A B A) has two codes 32: 2 * 7 * 1
    QTest::newRow("simple font, two codes 32") << QByteArray("F1") << QByteArray("(A B A) Tj") << 100.0 << 0.0 << 7.0 << 14.0;

    // /F1: the code 68 (D) shows the glyph space by /Differences, it is not the code 32: (ADBDA)
    // has no code 32
    QTest::newRow("simple font, space under another code") << QByteArray("F1") << QByteArray("(ADBDA) Tj") << 100.0 << 0.0 << 7.0 << 0.0;

    // /F2: the code 32 shows the glyph A, the code 65 shows the glyph space. <2041204141> has two
    // codes 32 (2 * 7) and three codes 65, which are spaces by their Unicode value only
    QTest::newRow("swapped font, code 32 is not a space") << QByteArray("F2") << QByteArray("<2041204141> Tj") << 100.0 << 0.0 << 7.0 << 14.0;

    // /F0: codes of two bytes; <0020> is U+0020 by /ToUnicode, but not a single-byte code
    QTest::newRow("Type 0 font, code of two bytes") << QByteArray("F0") << QByteArray("<004100200042> Tj") << 100.0 << 0.0 << 7.0 << 0.0;

    // /FM: a composite font, which defines the code 32 as a single-byte code. <20418020> + <20>:
    // the two codes <20> get the word spacing (2 * 7), the code <8020> of two bytes does not,
    // though it is U+0020 by /ToUnicode and its second byte is 32
    QTest::newRow("Type 0 font, single-byte code 32") << QByteArray("FM") << QByteArray("<2041802020> Tj") << 100.0 << 0.0 << 7.0 << 14.0;

    // The horizontal scaling scales the word spacing: 2 * 7 * 0.5
    QTest::newRow("simple font, horizontal scaling") << QByteArray("F1") << QByteArray("(A B A) Tj") << 50.0 << 0.0 << 7.0 << 7.0;

    // The array of TJ: the codes 32 of all its strings, 3 * 7; its number is the same in both runs
    QTest::newRow("simple font, TJ") << QByteArray("F1") << QByteArray("[(A ) -500 ( B )] TJ") << 100.0 << 0.0 << 7.0 << 21.0;

    // The character spacing is as before: every glyph gets it, 5 * 3; scaled, 5 * 3 * 0.5;
    // also the glyphs of a Type 0 font, 3 * 3
    QTest::newRow("simple font, character spacing") << QByteArray("F1") << QByteArray("(A B A) Tj") << 100.0 << 3.0 << 0.0 << 15.0;
    QTest::newRow("simple font, character spacing scaled") << QByteArray("F1") << QByteArray("(A B A) Tj") << 50.0 << 3.0 << 0.0 << 7.5;
    QTest::newRow("Type 0 font, character spacing") << QByteArray("F0") << QByteArray("<004100200042> Tj") << 100.0 << 3.0 << 0.0 << 9.0;

    // Both: (5 * 3 + 2 * 7) * 0.5; Type 0: only the character spacing, 3 * 3
    QTest::newRow("simple font, both spacings scaled") << QByteArray("F1") << QByteArray("(A B A) Tj") << 50.0 << 3.0 << 7.0 << 14.5;
    QTest::newRow("Type 0 font, both spacings") << QByteArray("F0") << QByteArray("<004100200042> Tj") << 100.0 << 3.0 << 7.0 << 9.0;
}

void ContentProcessorTest::wordSpacing()
{
    QFETCH(QByteArray, font);
    QFETCH(QByteArray, show);
    QFETCH(double, horizontalScaling);
    QFETCH(double, characterSpacing);
    QFETCH(double, wordSpacing);
    QFETCH(double, expectedDifference);

    const QByteArray showOperator = show.endsWith("TJ") ? QByteArray("TJ") : QByteArray("Tj");

    ContentProcessorRun plainRun(ContentTestDocuments::createFontsDocument(spacingContent(font, horizontalScaling, 0.0, 0.0, show)));
    ContentProcessorRun spacedRun(ContentTestDocuments::createFontsDocument(spacingContent(font, horizontalScaling, characterSpacing, wordSpacing, show)));

    PDFReal plainPosition = 0.0;
    PDFReal spacedPosition = 0.0;
    QVERIFY(getTextPositionAfterShow(plainRun, showOperator, &plainPosition));
    QVERIFY(getTextPositionAfterShow(spacedRun, showOperator, &spacedPosition));

    // The text was shown at all: it begins at 20 and its glyphs have widths
    QVERIFY(plainPosition > 20.0);
    COMPARE_NEAR(spacedPosition - plainPosition, expectedDifference);
}

void ContentProcessorTest::type3Spacing_data()
{
    QTest::addColumn<QByteArray>("show");
    QTest::addColumn<double>("horizontalScaling");
    QTest::addColumn<double>("characterSpacing");
    QTest::addColumn<double>("wordSpacing");
    QTest::addColumn<QList<double>>("expectedOrigins");
    QTest::addColumn<double>("expectedEnd");

    // ISO 32000-2, 9.4.4: tx = ((w0 - Tj / 1000) * Tfs + Tc + Tw) * Th. The font size Tfs is 10, the text
    // begins at 20. The widths w0 of the Type 3 font are its /Widths * the font matrix 0.01:
    // A = 0.6, the code 32 = 0.5, B = 0.8, so w0 * Tfs is 6, 5 and 8.

    // No spacing, the positions of before the change: 20, 20 + 6, 26 + 5; the end 31 + 8
    QTest::newRow("no spacing") << QByteArray("(A B) Tj") << 100.0 << 0.0 << 0.0 << QList<double>{ 20.0, 26.0, 31.0 } << 39.0;

    // Tc 1, Tw 3: A (6 + 1) -> 27; the code 32 (5 + 1 + 3) -> 36; B (8 + 1) -> 45
    QTest::newRow("character and word spacing") << QByteArray("(A B) Tj") << 100.0 << 1.0 << 3.0 << QList<double>{ 20.0, 27.0, 36.0 } << 45.0;

    // The same with Th = 0.5: 3.5 -> 23.5; 4.5 -> 28; 4.5 -> 32.5
    QTest::newRow("horizontal scaling") << QByteArray("(A B) Tj") << 50.0 << 1.0 << 3.0 << QList<double>{ 20.0, 23.5, 28.0 } << 32.5;

    // A number of TJ, Th = 0.5: A 3.5 -> 23.5; -200: (200 / 1000) * 10 * 0.5 = 1 -> 24.5; the code 32
    // 4.5 -> 29; B 4.5 -> 33.5
    QTest::newRow("TJ with a number") << QByteArray("[(A) -200 ( B)] TJ") << 50.0 << 1.0 << 3.0 << QList<double>{ 20.0, 24.5, 29.0 } << 33.5;

    // The word spacing alone and no code 32: nothing changes, 20, 26; the end 34
    QTest::newRow("word spacing without the code 32") << QByteArray("(AB) Tj") << 100.0 << 0.0 << 3.0 << QList<double>{ 20.0, 26.0 } << 34.0;

    // The word spacing alone: only the code 32 gets it, 20, 26, 26 + 5 + 3; the end 34 + 8
    QTest::newRow("word spacing alone") << QByteArray("(A B) Tj") << 100.0 << 0.0 << 3.0 << QList<double>{ 20.0, 26.0, 34.0 } << 42.0;

    // The character spacing alone: 20, 27, 33; the end 33 + 9
    QTest::newRow("character spacing alone") << QByteArray("(A B) Tj") << 100.0 << 1.0 << 0.0 << QList<double>{ 20.0, 27.0, 33.0 } << 42.0;
}

void ContentProcessorTest::type3Spacing()
{
    QFETCH(QByteArray, show);
    QFETCH(double, horizontalScaling);
    QFETCH(double, characterSpacing);
    QFETCH(double, wordSpacing);
    QFETCH(QList<double>, expectedOrigins);
    QFETCH(double, expectedEnd);

    const QByteArray showOperator = show.endsWith("TJ") ? QByteArray("TJ") : QByteArray("Tj");

    ContentProcessorRun run(ContentTestDocuments::createFontsDocument(spacingContent("T3", horizontalScaling, characterSpacing, wordSpacing, show)));
    const Recorder& recorder = *run.processor;

    // The origin of each glyph: the matrix of its procedure is the font matrix, the font size and
    // the horizontal scaling (0.01 * 10 * Th, 0.01 * 10) at the position of the text
    const std::vector<Recorder::Event> begins = recorder.eventsOfKind(Recorder::EventKind::GlyphBegin);
    QCOMPARE(begins.size(), size_t(expectedOrigins.size()));
    for (size_t i = 0; i < begins.size(); ++i)
    {
        COMPARE_NEAR(begins[i].matrix.dx(), expectedOrigins[qsizetype(i)]);
        COMPARE_NEAR(begins[i].matrix.dy(), 100.0);
        COMPARE_NEAR(begins[i].matrix.m11(), 0.1 * horizontalScaling / 100.0);
        COMPARE_NEAR(begins[i].matrix.m22(), 0.1);
    }

    PDFReal end = 0.0;
    QVERIFY(getTextPositionAfterShow(run, showOperator, &end));
    COMPARE_NEAR(end, expectedEnd);
}

void ContentProcessorTest::codeWithoutGlyph_data()
{
    QTest::addColumn<QByteArray>("font");
    QTest::addColumn<QByteArray>("show");
    QTest::addColumn<double>("horizontalScaling");
    QTest::addColumn<double>("characterSpacing");
    QTest::addColumn<double>("wordSpacing");
    QTest::addColumn<double>("expectedOrigin");
    QTest::addColumn<double>("expectedEnd");
    QTest::addColumn<int>("expectedWarnings");

    // ISO 32000-2, 9.4.4: a character code moves the text by tx = (w0 * Tfs + Tc + Tw) * Th, whether
    // the font has a glyph for it or not; Tw only for the single-byte code 32 (9.3.3). The font
    // size is 10 and the text begins at 20. Each string has one or two codes with a width and
    // without a glyph and then the code of the letter B: expectedOrigin is the origin of that
    // B, expectedEnd the position after it (-1, when the width of B is the one of a font of the
    // system). A reader, which has no glyph for a code, warns (expectedWarnings).

    // /FC, a simple font: the code 1 has the width 450 (w0 * Tfs = 4.5) and no glyph.
    // 20 + 4.5; 20 + (4.5 + 1) - the code is not 32, no word spacing; 20 + 5.5 * 0.5
    QTest::newRow("simple font") << QByteArray("FC") << QByteArray("<0142> Tj") << 100.0 << 0.0 << 0.0 << 24.5 << -1.0 << 1;
    QTest::newRow("simple font, spacing") << QByteArray("FC") << QByteArray("<0142> Tj") << 100.0 << 1.0 << 3.0 << 25.5 << -1.0 << 1;
    QTest::newRow("simple font, spacing scaled") << QByteArray("FC") << QByteArray("<0142> Tj") << 50.0 << 1.0 << 3.0 << 22.75 << -1.0 << 1;

    // Two codes without a glyph, 1 and 2: 20 + 2 * (4.5 + 1)
    QTest::newRow("simple font, two codes") << QByteArray("FC") << QByteArray("<010242> Tj") << 100.0 << 1.0 << 0.0 << 31.0 << -1.0 << 2;

    // /T3: the code 67 (C) has the width 70 of the glyph space (w0 * Tfs = 0.7 * 10 = 7) and no
    // glyph procedure; B has the width 8. 20 + 7, the end + 8; 20 + (7 + 1), the end + (8 + 1);
    // 20 + 8 * 0.5, the end + 9 * 0.5
    QTest::newRow("Type 3 font") << QByteArray("T3") << QByteArray("(CB) Tj") << 100.0 << 0.0 << 0.0 << 27.0 << 35.0 << 1;
    QTest::newRow("Type 3 font, spacing") << QByteArray("T3") << QByteArray("(CB) Tj") << 100.0 << 1.0 << 3.0 << 28.0 << 37.0 << 1;
    QTest::newRow("Type 3 font, spacing scaled") << QByteArray("T3") << QByteArray("(CB) Tj") << 50.0 << 1.0 << 3.0 << 24.0 << 28.5 << 1;

    // A number of TJ after the code stays a number: C (7 + 1), -200 gives 0.2 * 10 = 2, so B is
    // at 20 + 8 + 2, the end + 9
    QTest::newRow("Type 3 font, TJ") << QByteArray("T3") << QByteArray("[(C) -200 (B)] TJ") << 100.0 << 1.0 << 3.0 << 30.0 << 39.0 << 1;

    // /TN: the code 32 has the width 50 (w0 * Tfs = 5) and no glyph procedure; it is the
    // single-byte code 32, so it gets the word spacing. 20 + 5, the end + 8; 20 + (5 + 1 + 3),
    // the end + 9; 20 + 9 * 0.5, the end + 9 * 0.5
    QTest::newRow("Type 3 font, code 32") << QByteArray("TN") << QByteArray("( B) Tj") << 100.0 << 0.0 << 0.0 << 25.0 << 33.0 << 1;
    QTest::newRow("Type 3 font, code 32, spacing") << QByteArray("TN") << QByteArray("( B) Tj") << 100.0 << 1.0 << 3.0 << 29.0 << 38.0 << 1;
    QTest::newRow("Type 3 font, code 32, spacing scaled") << QByteArray("TN") << QByteArray("( B) Tj") << 50.0 << 1.0 << 3.0 << 24.5 << 29.0 << 1;

    // /F0, a Type 0 font: the code <0000> of two bytes is the CID 0 with the default width 900
    // (w0 * Tfs = 9) and no glyph; B has the width 6.5. 20 + 9, the end + 6.5; 20 + (9 + 1) - two
    // bytes, no word spacing -, the end + 7.5; 20 + 10 * 0.5, the end + 7.5 * 0.5
    QTest::newRow("Type 0 font") << QByteArray("F0") << QByteArray("<00000042> Tj") << 100.0 << 0.0 << 0.0 << 29.0 << 35.5 << 0;
    QTest::newRow("Type 0 font, spacing") << QByteArray("F0") << QByteArray("<00000042> Tj") << 100.0 << 1.0 << 3.0 << 30.0 << 37.5 << 0;
    QTest::newRow("Type 0 font, spacing scaled") << QByteArray("F0") << QByteArray("<00000042> Tj") << 50.0 << 1.0 << 3.0 << 25.0 << 28.75 << 0;

    // /FN, a Type 0 font: the single-byte code <20> is the CID 0 (the width 9, no glyph) and it
    // gets the word spacing. 20 + 9, the end + 6.5; 20 + (9 + 1 + 3), the end + 7.5; 20 + 13 * 0.5,
    // the end + 7.5 * 0.5
    QTest::newRow("Type 0 font, code 32") << QByteArray("FN") << QByteArray("<2042> Tj") << 100.0 << 0.0 << 0.0 << 29.0 << 35.5 << 0;
    QTest::newRow("Type 0 font, code 32, spacing") << QByteArray("FN") << QByteArray("<2042> Tj") << 100.0 << 1.0 << 3.0 << 33.0 << 40.5 << 0;
    QTest::newRow("Type 0 font, code 32, spacing scaled") << QByteArray("FN") << QByteArray("<2042> Tj") << 50.0 << 1.0 << 3.0 << 26.5 << 30.25 << 0;
}

void ContentProcessorTest::codeWithoutGlyph()
{
    QFETCH(QByteArray, font);
    QFETCH(QByteArray, show);
    QFETCH(double, horizontalScaling);
    QFETCH(double, characterSpacing);
    QFETCH(double, wordSpacing);
    QFETCH(double, expectedOrigin);
    QFETCH(double, expectedEnd);
    QFETCH(int, expectedWarnings);

    const QByteArray showOperator = show.endsWith("TJ") ? QByteArray("TJ") : QByteArray("Tj");
    const bool isType3 = font.startsWith('T');

    ContentProcessorRun run(ContentTestDocuments::createFontsDocument(spacingContent(font, horizontalScaling, characterSpacing, wordSpacing, show)));
    const Recorder& recorder = *run.processor;

    // The reader warns about the code without a glyph, there is no error
    QCOMPARE(run.errors.size(), expectedWarnings);
    for (const PDFRenderError& error : run.errors)
    {
        QCOMPARE(error.type, RenderErrorType::Warning);
    }

    // Only B is painted: the code without a glyph paints nothing
    PDFReal origin = 0.0;
    if (isType3)
    {
        const std::vector<Recorder::Event> begins = recorder.eventsOfKind(Recorder::EventKind::GlyphBegin);
        QCOMPARE(begins.size(), size_t(1));
        QCOMPARE(begins[0].code, 66u);
        origin = begins[0].matrix.dx();
        COMPARE_NEAR(begins[0].matrix.dy(), 100.0);
    }
    else
    {
        QCOMPARE(recorder.outputCharacters.size(), size_t(1));
        QCOMPARE(recorder.outputCharacters[0].character, QChar(u'B'));
        origin = recorder.outputCharacters[0].matrix.dx();
        COMPARE_NEAR(recorder.outputCharacters[0].matrix.dy(), 100.0);
        QCOMPARE(recorder.paths.size(), size_t(1));
    }
    COMPARE_NEAR(origin, expectedOrigin);

    const Recorder::Event* shown = recorder.lastEvent(Recorder::EventKind::After, showOperator);
    QVERIFY(shown);
    if (expectedEnd >= 0.0)
    {
        COMPARE_NEAR(shown->textMatrix.dx(), expectedEnd);
    }
    else
    {
        // The width of B comes from a font of the system: it is after its origin
        QVERIFY(shown->textMatrix.dx() > expectedOrigin);
    }
    COMPARE_NEAR(shown->textMatrix.dy(), 100.0);
}

void ContentProcessorTest::tilingPatternFillAndStroke()
{
    // An operator, which fills by a tiling pattern and strokes: the cells are processed in the
    // middle of it. Every cell has its own path, and the path of the page is stroked afterwards.
    // The cells of /P3 are at (10 + 40 i, 10 + 40 j): the square [10 10 90 90] has four of them.
    const QByteArray content = "/Pattern cs /P3 scn 0 0 1 RG 4 w 10 10 80 80 re B\n"
                               "0 g 100 100 20 20 re f\n";

    ContentProcessorRun run(ContentTestDocuments::createTilingPatternDocument(content));
    const Recorder& recorder = *run.processor;
    QVERIFY2(run.errors.isEmpty(), qPrintable(run.errorText()));

    const QStringList cell = pairs(1, { "rg", "re", "f" });
    QStringList expected;
    expected << pairs(0, { "cs", "scn", "RG", "w", "re" }) << QStringLiteral("0:B:B") << cell << cell << cell << cell << QStringLiteral("0:A:B")
             << pairs(0, { "g", "re", "f" });
    QCOMPARE(recorder.trace(), expected);

    // The four cells: each one paints its own red mark [0 0 10 10] and nothing of the page
    QCOMPARE(recorder.paths.size(), size_t(6));
    std::set<std::pair<int, int>> origins;
    for (size_t i = 0; i < 4; ++i)
    {
        const Recorder::PaintedPath& path = recorder.paths[i];
        QCOMPARE(path.level, PDFInteger(1));
        QCOMPARE(path.bounds, QRectF(0, 0, 10, 10));
        QCOMPARE(path.path.elementCount(), 5);
        QVERIFY(path.fill);
        QVERIFY(!path.stroke);
        QVERIFY(isSameColor(path.fillColor, 255, 0, 0));
        COMPARE_NEAR(path.matrix.dx(), qRound(path.matrix.dx()));
        COMPARE_NEAR(path.matrix.dy(), qRound(path.matrix.dy()));
        origins.insert(std::make_pair(qRound(path.matrix.dx()), qRound(path.matrix.dy())));
    }
    const std::set<std::pair<int, int>> expectedOrigins = { { 10, 10 }, { 50, 10 }, { 10, 50 }, { 50, 50 } };
    QVERIFY(origins == expectedOrigins);

    // The stroke of the same operator: the whole square of the page, in the state of the page
    const Recorder::PaintedPath& stroke = recorder.paths[4];
    QCOMPARE(stroke.level, PDFInteger(0));
    QCOMPARE(stroke.bounds, QRectF(10, 10, 80, 80));
    QCOMPARE(stroke.path.elementCount(), 5);
    QVERIFY(stroke.stroke);
    QVERIFY(!stroke.fill);
    QCOMPARE(stroke.matrix, QTransform());
    QCOMPARE(stroke.lineWidth, 4.0);
    QVERIFY(isSameColor(stroke.strokeColor, 0, 0, 255));

    // The next path of the page has nothing of a cell and nothing of the square
    const Recorder::PaintedPath& next = recorder.paths[5];
    QCOMPARE(next.level, PDFInteger(0));
    QCOMPARE(next.bounds, QRectF(100, 100, 20, 20));
    QCOMPARE(next.path.elementCount(), 5);
    QVERIFY(next.fill);
    QVERIFY(isSameColor(next.fillColor, 0, 0, 0));
}

void ContentProcessorTest::tilingPatternStroke()
{
    // A path stroked by a pattern color: the cells are clipped by the outline of the stroke,
    // which is not the current path - that is still the square of the page, and no cell gets it
    ContentProcessorRun run(ContentTestDocuments::createTilingPatternDocument("/Pattern CS /P3 SCN 4 w 10 10 80 80 re S\n0 g 100 100 20 20 re f\n"));
    const Recorder& recorder = *run.processor;
    QVERIFY2(run.errors.isEmpty(), qPrintable(run.errorText()));

    QVERIFY(recorder.paths.size() >= 2);
    for (size_t i = 0; i + 1 < recorder.paths.size(); ++i)
    {
        const Recorder::PaintedPath& path = recorder.paths[i];
        QCOMPARE(path.level, PDFInteger(1));
        QCOMPARE(path.bounds, QRectF(0, 0, 10, 10));
        QCOMPARE(path.path.elementCount(), 5);
        QVERIFY(isSameColor(path.fillColor, 255, 0, 0));
    }

    // The stroke itself is painted by the cells only; the next path of the page is its own
    const Recorder::PaintedPath& next = recorder.paths.back();
    QCOMPARE(next.level, PDFInteger(0));
    QCOMPARE(next.bounds, QRectF(100, 100, 20, 20));
    QCOMPARE(next.path.elementCount(), 5);
}

void ContentProcessorTest::tilingPatternFillsText()
{
    // A text filled by a pattern: the painted path is the outline of a glyph, the cells are
    // processed inside of the text showing operator
    ContentProcessorRun run(ContentTestDocuments::createTilingPatternDocument("BT /Pattern cs /P3 scn /F1 40 Tf 20 100 Td (A) Tj ET"));
    const Recorder& recorder = *run.processor;

    // The cell is a content stream of its own, also for the operand stack: the string of the
    // text showing operator is not an operand of the first operator of the cell (rg), which
    // gets its own three numbers and succeeds
    QVERIFY2(run.errors.isEmpty(), qPrintable(run.errorText()));
    QCOMPARE(recorder.eventsOfKind(Recorder::EventKind::Failed).size(), size_t(0));

    // The glyph is painted by the cells only, and each cell paints its own red mark
    QVERIFY(!recorder.paths.empty());
    for (const Recorder::PaintedPath& path : recorder.paths)
    {
        QCOMPARE(path.level, PDFInteger(1));
        QVERIFY(!path.text);
        QCOMPARE(path.bounds, QRectF(0, 0, 10, 10));
        QCOMPARE(path.path.elementCount(), 5);
        QVERIFY(isSameColor(path.fillColor, 255, 0, 0));
    }

    // The operands seen by the calls: the string of Tj before the text and - it is intact -
    // after the cells; the three numbers of rg inside of the cell
    const Recorder::Event* cellColor = recorder.lastEvent(Recorder::EventKind::Before, "rg");
    const Recorder::Event* shown = recorder.lastEvent(Recorder::EventKind::After, "Tj");
    QVERIFY(cellColor && shown);
    QCOMPARE(cellColor->level, PDFInteger(1));
    QCOMPARE(cellColor->operandCount, size_t(3));
    QCOMPARE(shown->operandCount, size_t(1));
    QCOMPARE(recorder.lastEvent(Recorder::EventKind::Before, "Tj")->operandCount, size_t(1));

    // The text object of the page goes on
    QCOMPARE(recorder.trace().last(), QStringLiteral("0:A:ET"));
    QCOMPARE(recorder.textBeginCount, 1);
    QCOMPARE(recorder.textEndCount, 1);
}

void ContentProcessorTest::tilingPatternRendering()
{
    // The page of tilingPatternFillAndStroke rendered: the square [10 10 90 90] filled by /P3
    // and stroked by a blue line 4 wide. The cells of /P3 are at (10 + 40 i, 10 + 40 j) and
    // each one paints only its red mark 10 x 10 in its lower left corner, so the square shows
    // the four red marks [10 20] x [10 20], [50 60] x [10 20], [10 20] x [50 60] and [50 60] x
    // [50 60]; the rest of it stays white (the page is white). The blue stroke covers 2 on each
    // side of the edges of the square and it is painted over the fill. The page 200 x 200 is
    // rendered into 400 x 400 pixels; the points are well inside of the areas.
    const PDFDocument document = ContentTestDocuments::createTilingPatternDocument("/Pattern cs /P3 scn 0 0 1 RG 4 w 10 10 80 80 re B\n");

    PDFCMSGeneric cms;
    PDFFontCache fontCache(32, 32);
    PDFICCProfileCache iccProfileCache;
    PDFOptionalContentActivity activity(&document, OCUsage::View, nullptr);
    fontCache.setDocument(PDFModifiedDocument(const_cast<PDFDocument*>(&document), &activity));
    iccProfileCache.setDocument(PDFModifiedDocument(const_cast<PDFDocument*>(&document), &activity));

    PDFRenderer renderer(&document, &fontCache, &iccProfileCache, &cms, &activity, PDFRenderer::Features(), PDFMeshQualitySettings());

    QImage image(400, 400, QImage::Format_RGB888);
    image.fill(Qt::white);
    {
        QPainter painter(&image);
        renderer.render(&painter, QRectF(0, 0, image.width(), image.height()), 0);
    }

    // The color of a point of the page: the vertical axis of the page points up
    auto pageColor = [&image](qreal x, qreal y)
    {
        return QColor(image.pixel(int(2.0 * x), int(image.height() - 2.0 * y)));
    };
    auto isRed = [](const QColor& color) { return color.red() > 200 && color.green() < 60 && color.blue() < 60; };
    auto isBlue = [](const QColor& color) { return color.blue() > 200 && color.red() < 60 && color.green() < 60; };
    auto isWhite = [](const QColor& color) { return color.red() > 200 && color.green() > 200 && color.blue() > 200; };

    // The four marks
    for (const QPointF& point : { QPointF(16, 16), QPointF(55, 16), QPointF(16, 55), QPointF(55, 55) })
    {
        QVERIFY2(isRed(pageColor(point.x(), point.y())), qPrintable(QStringLiteral("(%1, %2) is %3").arg(point.x()).arg(point.y()).arg(pageColor(point.x(), point.y()).name())));
    }

    // The rest of the cells is not painted, also of the first cell [10 50] x [10 50]
    for (const QPointF& point : { QPointF(35, 35), QPointF(30, 16), QPointF(16, 30), QPointF(75, 75), QPointF(35, 75), QPointF(75, 35), QPointF(70, 16), QPointF(55, 30) })
    {
        QVERIFY2(isWhite(pageColor(point.x(), point.y())), qPrintable(QStringLiteral("(%1, %2) is %3").arg(point.x()).arg(point.y()).arg(pageColor(point.x(), point.y()).name())));
    }

    // The stroke of the square, on its four edges
    for (const QPointF& point : { QPointF(10, 35), QPointF(90, 35), QPointF(35, 10), QPointF(35, 90), QPointF(90, 75), QPointF(75, 90) })
    {
        QVERIFY2(isBlue(pageColor(point.x(), point.y())), qPrintable(QStringLiteral("(%1, %2) is %3").arg(point.x()).arg(point.y()).arg(pageColor(point.x(), point.y()).name())));
    }

    // Nothing outside of the stroke
    for (const QPointF& point : { QPointF(4, 35), QPointF(96, 35), QPointF(35, 4), QPointF(35, 96), QPointF(150, 150) })
    {
        QVERIFY2(isWhite(pageColor(point.x(), point.y())), qPrintable(QStringLiteral("(%1, %2) is %3").arg(point.x()).arg(point.y()).arg(pageColor(point.x(), point.y()).name())));
    }
}

void ContentProcessorTest::tilingPatternLattice_data()
{
    QTest::addColumn<QByteArray>("content");
    QTest::addColumn<QRectF>("area");
    QTest::addColumn<int>("level");
    QTest::addColumn<double>("markSize");
    QTest::addColumn<QList<QPointF>>("expectedMarks");

    // ISO 32000-2, 8.7.3.1: the cells of a tiling pattern are at the multiples of /XStep and /YStep
    // from the origin of the pattern space, wherever the painted area begins and whatever the
    // current transformation matrix is. The patterns have the cell 20 x 20 and a blue mark
    // [0 0 5 5] in the lower left corner of each cell (createTilingPatternResources). Each row
    // paints a square 40 x 40, which does not begin on the lattice: three cells of each
    // direction reach into it. expectedMarks are the lower left corners on the page of the
    // marks, which reach into the painted area.

    // /P2, no /Matrix: the marks are at (20 i, 20 j). The square [30 30 70 70] shows those at 40
    // and 60 of both axes (the marks [20 25] are outside of it).
    QTest::newRow("no matrix") << QByteArray("/Pattern cs /P2 scn 30 30 40 40 re f\n") << QRectF(30, 30, 40, 40) << 1 << 5.0
                               << QList<QPointF>{ QPointF(40, 40), QPointF(60, 40), QPointF(40, 60), QPointF(60, 60) };

    // The same square painted through a current transformation matrix: 15 15 20 20 re scaled by
    // 2 is [30 30 70 70] again. The matrix of the painting has no effect on the pattern.
    QTest::newRow("painted through a matrix") << QByteArray("q 2 0 0 2 0 0 cm /Pattern cs /P2 scn 15 15 20 20 re f Q\n") << QRectF(30, 30, 40, 40) << 1 << 5.0
                                              << QList<QPointF>{ QPointF(40, 40), QPointF(60, 40), QPointF(40, 60), QPointF(60, 60) };

    // /P4, /Matrix [1 0 0 1 7 3]: the marks are at (7 + 20 i, 3 + 20 j). In x the marks [27 32],
    // [47 52] and [67 72] reach into [30 70]; in y the marks [43 48] and [63 68] ([23 28] is below).
    QTest::newRow("translated") << QByteArray("/Pattern cs /P4 scn 30 30 40 40 re f\n") << QRectF(30, 30, 40, 40) << 1 << 5.0
                                << QList<QPointF>{ QPointF(27, 43), QPointF(47, 43), QPointF(67, 43), QPointF(27, 63), QPointF(47, 63), QPointF(67, 63) };

    // /P5, the steps -20: the same lattice and the same marks as /P4
    QTest::newRow("negative steps") << QByteArray("/Pattern cs /P5 scn 30 30 40 40 re f\n") << QRectF(30, 30, 40, 40) << 1 << 5.0
                                    << QList<QPointF>{ QPointF(27, 43), QPointF(47, 43), QPointF(67, 43), QPointF(27, 63), QPointF(47, 63), QPointF(67, 63) };

    // /P6, /Matrix [0 1 -1 0 100 0]: the mark of the cell (i, j) is [95 - 20 j, 100 - 20 j] x
    // [20 i, 20 i + 5]. In x the marks [35 40] (j = 3) and [55 60] (j = 2) are in [30 70]; in y
    // the marks [40 45] and [60 65].
    QTest::newRow("rotated") << QByteArray("/Pattern cs /P6 scn 30 30 40 40 re f\n") << QRectF(30, 30, 40, 40) << 1 << 5.0
                             << QList<QPointF>{ QPointF(35, 40), QPointF(55, 40), QPointF(35, 60), QPointF(55, 60) };

    // /P2 as a resource of the form /FmP with /Matrix [2 0 0 2 10 20]: the pattern space is the
    // space of the form, so the marks are 10 x 10 at (10 + 40 i, 20 + 40 j) of the page. The form
    // fills its [12 12 42 42], which is [34 44 94 104] of the page: the marks [50 60] and [90 100]
    // in x, [60 70] and [100 110] in y.
    QTest::newRow("in a form") << QByteArray("/FmP Do\n") << QRectF(34, 44, 60, 60) << 2 << 10.0
                               << QList<QPointF>{ QPointF(50, 60), QPointF(90, 60), QPointF(50, 100), QPointF(90, 100) };
}

void ContentProcessorTest::tilingPatternLattice()
{
    QFETCH(QByteArray, content);
    QFETCH(QRectF, area);
    QFETCH(int, level);
    QFETCH(double, markSize);
    QFETCH(QList<QPointF>, expectedMarks);

    ContentProcessorRun run(ContentTestDocuments::createTilingPatternDocument(content));
    const Recorder& recorder = *run.processor;
    QVERIFY2(run.errors.isEmpty(), qPrintable(run.errorText()));

    // Three cells of each direction reach into the painted square: nine cells are processed,
    // and the processor has asked about that number
    QCOMPARE(recorder.paths.size(), size_t(9));
    QCOMPARE(recorder.tilingCellCounts.size(), size_t(1));
    QCOMPARE(recorder.tilingCellCounts[0], PDFInteger(9));

    // The marks, which reach into the painted area, by their lower left corners on the page
    std::set<std::pair<int, int>> visibleMarks;
    for (const Recorder::PaintedPath& path : recorder.paths)
    {
        QCOMPARE(path.level, PDFInteger(level));
        QCOMPARE(path.bounds, QRectF(0, 0, 5, 5));
        QVERIFY(isSameColor(path.fillColor, 0, 0, 255));

        const QRectF mark = path.matrix.mapRect(path.bounds);
        COMPARE_NEAR(mark.width(), markSize);
        COMPARE_NEAR(mark.height(), markSize);
        COMPARE_NEAR(mark.left(), qRound(mark.left()));
        COMPARE_NEAR(mark.top(), qRound(mark.top()));
        if (mark.intersects(area))
        {
            visibleMarks.insert(std::make_pair(qRound(mark.left()), qRound(mark.top())));
        }
    }

    std::set<std::pair<int, int>> expected;
    for (const QPointF& point : expectedMarks)
    {
        expected.insert(std::make_pair(qRound(point.x()), qRound(point.y())));
    }

    QStringList actualText;
    for (const auto& mark : visibleMarks)
    {
        actualText << QStringLiteral("(%1, %2)").arg(mark.first).arg(mark.second);
    }
    QVERIFY2(visibleMarks == expected, qPrintable(actualText.join(QLatin1Char(' '))));
}

void ContentProcessorTest::tilingPatternLatticeRendering()
{
    // The first row of tilingPatternLattice rendered: the square [30 30 70 70] filled by /P2,
    // whose blue marks 5 x 5 are at the multiples of 20. The page 200 x 200 is rendered into
    // 400 x 400 pixels. Blue: inside of the marks [40 45] and [60 65] of both axes. White: the
    // rest of the square - also its corner (30, 30) and the points (50, 50), where the cells
    // would be, if they started at the corner of the square - and the mark at (20, 20), which
    // is outside of the square.
    const PDFDocument document = ContentTestDocuments::createTilingPatternDocument("/Pattern cs /P2 scn 30 30 40 40 re f\n");

    PDFCMSGeneric cms;
    PDFFontCache fontCache(32, 32);
    PDFICCProfileCache iccProfileCache;
    PDFOptionalContentActivity activity(&document, OCUsage::View, nullptr);
    fontCache.setDocument(PDFModifiedDocument(const_cast<PDFDocument*>(&document), &activity));
    iccProfileCache.setDocument(PDFModifiedDocument(const_cast<PDFDocument*>(&document), &activity));

    PDFRenderer renderer(&document, &fontCache, &iccProfileCache, &cms, &activity, PDFRenderer::Features(), PDFMeshQualitySettings());

    QImage image(400, 400, QImage::Format_RGB888);
    image.fill(Qt::white);
    {
        QPainter painter(&image);
        renderer.render(&painter, QRectF(0, 0, image.width(), image.height()), 0);
    }

    auto pageColor = [&image](qreal x, qreal y)
    {
        return QColor(image.pixel(int(2.0 * x), int(image.height() - 2.0 * y)));
    };
    auto isBlue = [](const QColor& color) { return color.blue() > 200 && color.red() < 60 && color.green() < 60; };
    auto isWhite = [](const QColor& color) { return color.red() > 200 && color.green() > 200 && color.blue() > 200; };

    for (const QPointF& point : { QPointF(42, 42), QPointF(62, 42), QPointF(42, 62), QPointF(62, 62) })
    {
        QVERIFY2(isBlue(pageColor(point.x(), point.y())), qPrintable(QStringLiteral("(%1, %2) is %3").arg(point.x()).arg(point.y()).arg(pageColor(point.x(), point.y()).name())));
    }

    for (const QPointF& point : { QPointF(32, 32), QPointF(52, 52), QPointF(52, 32), QPointF(32, 52), QPointF(48, 48), QPointF(68, 68), QPointF(22, 22), QPointF(42, 22), QPointF(82, 82) })
    {
        QVERIFY2(isWhite(pageColor(point.x(), point.y())), qPrintable(QStringLiteral("(%1, %2) is %3").arg(point.x()).arg(point.y()).arg(pageColor(point.x(), point.y()).name())));
    }
}

void ContentProcessorTest::tilingPatternCellLimit()
{
    // A descendant can refuse a pattern by the number of its cells
    // (isTilingPatternProcessingAllowed): it is asked about the cells, which are going to be
    // processed - nine for the square of tilingPatternLattice -, and a refused pattern paints
    // nothing and gives a warning
    for (const PDFInteger limit : { PDFInteger(9), PDFInteger(8) })
    {
        ContentProcessorRun run(ContentTestDocuments::createTilingPatternDocument("/Pattern cs /P2 scn 30 30 40 40 re f\n0 g 100 100 20 20 re f\n"), false, false);
        Recorder& recorder = *run.processor;
        recorder.tilingCellLimit = limit;
        const QList<PDFRenderError> errors = recorder.processContents();

        QCOMPARE(recorder.tilingCellCounts.size(), size_t(1));
        QCOMPARE(recorder.tilingCellCounts[0], PDFInteger(9));

        if (limit >= 9)
        {
            QVERIFY(errors.isEmpty());
            QCOMPARE(recorder.paths.size(), size_t(10));
        }
        else
        {
            QCOMPARE(errors.size(), 1);
            QCOMPARE(errors[0].type, RenderErrorType::Warning);

            // Only the black square of the page after the pattern
            QCOMPARE(recorder.paths.size(), size_t(1));
            QCOMPARE(recorder.paths[0].level, PDFInteger(0));
            QCOMPARE(recorder.paths[0].bounds, QRectF(100, 100, 20, 20));
        }
    }
}

void ContentProcessorTest::tilingPatternTooFineToCount()
{
    // A5L1-5. The pattern /PF: /BBox [0 0 0.00001 0.00001], /XStep and /YStep 0.00001 (more than the
    // smallest step a pattern may have, 0.000001) and /Matrix [0.001 0 0 0.001 0 0]. The square
    // [50 50 150 150] of the page is [50000 150000] of the pattern space in each direction: 10^10
    // cells of each direction (ISO 32000-2, 8.7.3.1: the lattice of /XStep and /YStep), more than
    // a count of cells can hold (2^31 - 1). Such a lattice is not painted, also by a descendant,
    // which allows every pattern (the default isTilingPatternProcessingAllowed): one warning, and
    // the processing goes on with the black square after the pattern.
    using D = ContentTestDocuments;

    auto createResources = [](PDFDocumentBuilder* builder)
    {
        const PDFObject pattern = D::addStreamObject(builder, { { "Type", PDFObject::createName("Pattern") },
                                                                { "PatternType", PDFObject::createInteger(1) },
                                                                { "PaintType", PDFObject::createInteger(1) },
                                                                { "TilingType", PDFObject::createInteger(1) },
                                                                { "BBox", D::createNumberArrayObject({ 0.0, 0.0, 0.00001, 0.00001 }) },
                                                                { "XStep", PDFObject::createReal(0.00001) },
                                                                { "YStep", PDFObject::createReal(0.00001) },
                                                                { "Matrix", D::createNumberArrayObject({ 0.001, 0.0, 0.0, 0.001, 0.0, 0.0 }) },
                                                                { "Resources", D::createDictionaryObject(D::DictionaryEntries()) } },
                                                     QByteArray("0 0 1 rg 0 0 0.00001 0.00001 re f"));

        PDFDictionaryBuilder resources;
        resources.addEntry(PDFInplaceOrMemoryString("Pattern"), D::createDictionaryObject({ { "PF", pattern } }));
        return resources;
    };

    PDFDocument document = D::createDocument(QByteArray("/Pattern cs /PF scn 50 50 100 100 re f\n0 g 160 160 20 20 re f\n"), createResources);

    PDFCMSGeneric cms;
    PDFFontCache fontCache(32, 32);
    PDFICCProfileCache iccProfileCache;
    PDFOptionalContentActivity activity(&document, OCUsage::View, nullptr);
    fontCache.setDocument(PDFModifiedDocument(&document, &activity));
    iccProfileCache.setDocument(PDFModifiedDocument(&document, &activity));

    TilingCellCountingProcessor processor(document.getCatalog()->getPage(0), &document, &fontCache, &iccProfileCache, &cms, &activity,
                                          QTransform(), PDFMeshQualitySettings());
    processor.setOperationControl(&processor);

    QElapsedTimer timer;
    timer.start();
    const QList<PDFRenderError> errors = processor.processContents();
    const qint64 elapsed = timer.elapsed();

    QStringList messages;
    for (const PDFRenderError& error : errors)
    {
        messages << error.message;
    }

    // No cell is painted, only the black square
    QVERIFY2(processor.paintedPaths == 1, qPrintable(QStringLiteral("%1 paths painted").arg(processor.paintedPaths)));
    QCOMPARE(processor.paintedBounds.size(), 1);
    QCOMPARE(processor.paintedBounds[0], QRectF(160, 160, 20, 20));

    QVERIFY2(errors.size() == 1, qPrintable(messages.join(QStringLiteral(" | "))));
    QCOMPARE(errors[0].type, RenderErrorType::Warning);

    const qint64 limit = qint64(5000) * qMax(1, qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR"));
    QVERIFY2(elapsed < limit, qPrintable(QStringLiteral("%1 ms").arg(elapsed)));
}

void ContentProcessorTest::negativeFontSize_data()
{
    QTest::addColumn<QByteArray>("font");
    QTest::addColumn<double>("fontSize");
    QTest::addColumn<QByteArray>("show");
    QTest::addColumn<double>("horizontalScaling");
    QTest::addColumn<double>("characterSpacing");
    QTest::addColumn<double>("wordSpacing");
    QTest::addColumn<QList<QPointF>>("expectedOrigins");
    QTest::addColumn<QPointF>("expectedEnd");

    // ISO 32000-2, 9.4.4: tx = ((w0 - Tj / 1000) * Tfs + Tc + Tw) * Th, in the vertical writing mode
    // ty = (w1 - Tj / 1000) * Tfs + Tc + Tw. A negative font size Tfs turns the advance of the
    // glyph; the character and the word spacing are added as they are (9.3.2, 9.3.3). The text
    // rendering matrix scales by Tfs for every font type, so the glyphs are mirrored in both
    // axes. The text begins at (100, 100); expectedOrigins are the origins of the painted glyphs,
    // expectedEnd the position after the text (x is nan, when the last width is the one of a
    // font of the system).
    const double unknown = std::numeric_limits<double>::quiet_NaN();

    // /F0, a Type 0 font, A = 0.7, B = 0.65. Size -10, Tc 1: A (-7 + 1) -> 94, B (-6.5 + 1) -> 88.5
    QTest::newRow("Type 0 font") << QByteArray("F0") << -10.0 << QByteArray("<00410042> Tj") << 100.0 << 1.0 << 3.0
                                 << QList<QPointF>{ QPointF(100, 100), QPointF(94, 100) } << QPointF(88.5, 100);

    // The same with Th = 0.5: A (-7 + 1) * 0.5 -> 97, B (-6.5 + 1) * 0.5 -> 94.25
    QTest::newRow("Type 0 font, scaled") << QByteArray("F0") << -10.0 << QByteArray("<00410042> Tj") << 50.0 << 1.0 << 3.0
                                         << QList<QPointF>{ QPointF(100, 100), QPointF(97, 100) } << QPointF(94.25, 100);

    // /FM: the single-byte code 32 (0.25) gets the word spacing: (-2.5 + 1 + 3) -> 101.5, to the
    // right; A (-7 + 1) -> 95.5
    QTest::newRow("Type 0 font, code 32") << QByteArray("FM") << -10.0 << QByteArray("<2041> Tj") << 100.0 << 1.0 << 3.0
                                          << QList<QPointF>{ QPointF(100, 100), QPointF(101.5, 100) } << QPointF(95.5, 100);

    // /FN: the code 32 without a glyph (0.9): (-9 + 1 + 3) -> 95; B (-6.5 + 1) -> 89.5
    QTest::newRow("Type 0 font, code without a glyph") << QByteArray("FN") << -10.0 << QByteArray("<2042> Tj") << 100.0 << 1.0 << 3.0
                                                       << QList<QPointF>{ QPointF(95, 100) } << QPointF(89.5, 100);

    // /FC, a simple font: the code 1 without a glyph (0.45): (-4.5 + 1) -> 96.5
    QTest::newRow("simple font, code without a glyph") << QByteArray("FC") << -10.0 << QByteArray("<0142> Tj") << 100.0 << 1.0 << 3.0
                                                       << QList<QPointF>{ QPointF(96.5, 100) } << QPointF(unknown, 100);

    // /T3, a Type 3 font: A = 0.6, the code 32 = 0.5, B = 0.8. Size -10, Tc 1, Tw 3: A (-6 + 1) -> 95,
    // the code 32 (-5 + 1 + 3) -> 94, B (-8 + 1) -> 87
    QTest::newRow("Type 3 font") << QByteArray("T3") << -10.0 << QByteArray("(A B) Tj") << 100.0 << 1.0 << 3.0
                                 << QList<QPointF>{ QPointF(100, 100), QPointF(95, 100), QPointF(94, 100) } << QPointF(87, 100);

    // The same with Th = 0.5: -2.5 -> 97.5, -0.5 -> 97, -3.5 -> 93.5
    QTest::newRow("Type 3 font, scaled") << QByteArray("T3") << -10.0 << QByteArray("(A B) Tj") << 50.0 << 1.0 << 3.0
                                         << QList<QPointF>{ QPointF(100, 100), QPointF(97.5, 100), QPointF(97, 100) } << QPointF(93.5, 100);

    // A number of TJ: A (-6 + 1) -> 95, -200 gives (200 / 1000) * (-10) = -2 -> 93, B (-8 + 1) -> 86
    QTest::newRow("Type 3 font, TJ") << QByteArray("T3") << -10.0 << QByteArray("[(A) -200 (B)] TJ") << 100.0 << 1.0 << 3.0
                                     << QList<QPointF>{ QPointF(100, 100), QPointF(93, 100) } << QPointF(86, 100);

    // The code 67 without a glyph procedure (0.7): (-7 + 1) -> 94, B (-8 + 1) -> 87
    QTest::newRow("Type 3 font, code without a procedure") << QByteArray("T3") << -10.0 << QByteArray("(CB) Tj") << 100.0 << 1.0 << 3.0
                                                           << QList<QPointF>{ QPointF(94, 100) } << QPointF(87, 100);

    // /FV, the vertical writing mode: w1 of A is -0.7, of B -0.9. Size 10, Tc 1: A (-7 + 1) -> y 94,
    // B (-9 + 1) -> y 86; the horizontal scaling has no part in it
    QTest::newRow("vertical font, positive size") << QByteArray("FV") << 10.0 << QByteArray("<00410042> Tj") << 50.0 << 1.0 << 3.0
                                                  << QList<QPointF>{ QPointF(100, 100), QPointF(100, 94) } << QPointF(100, 86);

    // Size -10: A (7 + 1) -> y 108, B (9 + 1) -> y 118
    QTest::newRow("vertical font, negative size") << QByteArray("FV") << -10.0 << QByteArray("<00410042> Tj") << 100.0 << 1.0 << 3.0
                                                  << QList<QPointF>{ QPointF(100, 100), QPointF(100, 108) } << QPointF(100, 118);
}

void ContentProcessorTest::negativeFontSize()
{
    QFETCH(QByteArray, font);
    QFETCH(double, fontSize);
    QFETCH(QByteArray, show);
    QFETCH(double, horizontalScaling);
    QFETCH(double, characterSpacing);
    QFETCH(double, wordSpacing);
    QFETCH(QList<QPointF>, expectedOrigins);
    QFETCH(QPointF, expectedEnd);

    const QByteArray showOperator = show.endsWith("TJ") ? QByteArray("TJ") : QByteArray("Tj");
    const bool isType3 = font.startsWith('T');
    const QByteArray content = "BT /" + font + " " + QByteArray::number(fontSize) + " Tf " + QByteArray::number(horizontalScaling) + " Tz " +
                               QByteArray::number(characterSpacing) + " Tc " + QByteArray::number(wordSpacing) + " Tw 100 100 Td " + show + " ET";

    ContentProcessorRun run(ContentTestDocuments::createFontsDocument(content));
    const Recorder& recorder = *run.processor;
    for (const PDFRenderError& error : run.errors)
    {
        // Only the warnings about the codes without a glyph
        QCOMPARE(error.type, RenderErrorType::Warning);
    }

    // The origins of the painted glyphs
    std::vector<QTransform> matrices;
    if (isType3)
    {
        for (const Recorder::Event& event : recorder.eventsOfKind(Recorder::EventKind::GlyphBegin))
        {
            matrices.push_back(event.matrix);
        }
    }
    else
    {
        for (const Recorder::OutputCharacter& character : recorder.outputCharacters)
        {
            matrices.push_back(character.matrix);
        }
    }

    QCOMPARE(matrices.size(), size_t(expectedOrigins.size()));
    for (size_t i = 0; i < matrices.size(); ++i)
    {
        COMPARE_NEAR(matrices[i].dx(), expectedOrigins[qsizetype(i)].x());
        COMPARE_NEAR(matrices[i].dy(), expectedOrigins[qsizetype(i)].y());

        if (isType3)
        {
            // The glyph space: the font matrix 0.01, the font size with its sign and the
            // horizontal scaling - a negative size mirrors the glyph in both axes
            COMPARE_NEAR(matrices[i].m11(), 0.01 * fontSize * horizontalScaling / 100.0);
            COMPARE_NEAR(matrices[i].m22(), 0.01 * fontSize);
            COMPARE_NEAR(matrices[i].m12(), 0.0);
            COMPARE_NEAR(matrices[i].m21(), 0.0);
        }
    }

    const Recorder::Event* shown = recorder.lastEvent(Recorder::EventKind::After, showOperator);
    QVERIFY(shown);
    if (!std::isnan(expectedEnd.x()))
    {
        COMPARE_NEAR(shown->textMatrix.dx(), expectedEnd.x());
    }
    COMPARE_NEAR(shown->textMatrix.dy(), expectedEnd.y());

    // The outline of a glyph of the horizontal Type 0 font of a negative size is mirrored in
    // both axes: it is to the left of its origin and below it (the letter A of the size 10 is
    // within 10 of its origin)
    if (font == "F0" && fontSize < 0.0)
    {
        QVERIFY(!recorder.paths.empty());
        const QRectF bounds = recorder.paths[0].bounds;
        QVERIFY2(bounds.right() <= 100.1 && bounds.left() >= 89.9 && bounds.bottom() <= 100.1 && bounds.top() >= 89.9,
                 qPrintable(QStringLiteral("%1 %2 %3 %4").arg(bounds.left()).arg(bounds.top()).arg(bounds.right()).arg(bounds.bottom())));
    }
}

void ContentProcessorTest::negativeFontSizeSpacing_data()
{
    QTest::addColumn<QByteArray>("font");
    QTest::addColumn<QByteArray>("show");
    QTest::addColumn<double>("horizontalScaling");
    QTest::addColumn<double>("characterSpacing");
    QTest::addColumn<double>("wordSpacing");
    QTest::addColumn<double>("expectedDifference");

    // A simple font, which is not embedded, has the advances of a font of the system, so each
    // row compares the position after the text of the size -10 with the position of the same
    // text with Tc = Tw = 0: the difference is (glyphs * Tc + codes 32 * Tw) * Th and it is
    // positive - the spacings are added as they are, though the glyphs advance to the left.

    // (A B A): five glyphs, two codes 32
    QTest::newRow("character spacing") << QByteArray("F1") << QByteArray("(A B A) Tj") << 100.0 << 3.0 << 0.0 << 15.0;
    QTest::newRow("word spacing") << QByteArray("F1") << QByteArray("(A B A) Tj") << 100.0 << 0.0 << 7.0 << 14.0;
    QTest::newRow("both, scaled") << QByteArray("F1") << QByteArray("(A B A) Tj") << 50.0 << 3.0 << 7.0 << 14.5;

    // /F2: the code 32 shows the glyph A; two codes 32 among five codes
    QTest::newRow("code 32 is not a space") << QByteArray("F2") << QByteArray("<2041204141> Tj") << 100.0 << 0.0 << 7.0 << 14.0;
}

void ContentProcessorTest::negativeFontSizeSpacing()
{
    QFETCH(QByteArray, font);
    QFETCH(QByteArray, show);
    QFETCH(double, horizontalScaling);
    QFETCH(double, characterSpacing);
    QFETCH(double, wordSpacing);
    QFETCH(double, expectedDifference);

    auto createContent = [&font, &show, horizontalScaling](double characterSpacingOfRun, double wordSpacingOfRun)
    {
        return "BT /" + font + " -10 Tf " + QByteArray::number(horizontalScaling) + " Tz " + QByteArray::number(characterSpacingOfRun) + " Tc " +
               QByteArray::number(wordSpacingOfRun) + " Tw 100 100 Td " + show + " ET";
    };

    ContentProcessorRun plainRun(ContentTestDocuments::createFontsDocument(createContent(0.0, 0.0)));
    ContentProcessorRun spacedRun(ContentTestDocuments::createFontsDocument(createContent(characterSpacing, wordSpacing)));
    QVERIFY2(plainRun.errors.isEmpty(), qPrintable(plainRun.errorText()));
    QVERIFY2(spacedRun.errors.isEmpty(), qPrintable(spacedRun.errorText()));

    const Recorder::Event* plainShown = plainRun.processor->lastEvent(Recorder::EventKind::After, "Tj");
    const Recorder::Event* spacedShown = spacedRun.processor->lastEvent(Recorder::EventKind::After, "Tj");
    QVERIFY(plainShown && spacedShown);

    // The text of a negative size goes to the left of its beginning
    QVERIFY(plainShown->textMatrix.dx() < 100.0);
    COMPARE_NEAR(plainShown->textMatrix.dy(), 100.0);
    COMPARE_NEAR(spacedShown->textMatrix.dx() - plainShown->textMatrix.dx(), expectedDifference);
}

void ContentProcessorTest::nestedContentOperandsOfCell()
{
    // The cell of /P7 is a wrong content: its rg has two operands, and three numbers are left at
    // its end. The page has a stray operand before its painting operator. A cell is a content
    // stream of its own (8.7.3.1), also for the operand stack: its rg fails in every cell with
    // its own two operands - the stray 1 of the page would make it a red, the 7 8 9 of the
    // previous cell another color - and the cell is painted by its initial black.
    // The cells of /P7 are 10 x 10 at (20 i, 20 j): the square [20 20 60 60] has four of them.
    const QByteArray content = "/Pattern cs /P7 scn 20 20 40 40 re 1 f\n"
                               "0 0 1 rg 100 100 20 20 re f\n";

    ContentProcessorRun run(ContentTestDocuments::createTilingPatternDocument(content));
    const Recorder& recorder = *run.processor;

    const QStringList cell = QStringList() << QStringLiteral("1:B:rg") << QStringLiteral("1:F:rg") << QStringLiteral("1:A:rg") << pairs(1, { "re", "f" });
    QStringList expected;
    expected << pairs(0, { "cs", "scn", "re" }) << QStringLiteral("0:B:f") << cell << cell << cell << cell << QStringLiteral("0:A:f")
             << pairs(0, { "rg", "re", "f" });
    QCOMPARE(recorder.trace(), expected);

    // The errors are those of the four cells: an operator with two operands
    const std::vector<Recorder::Event> failed = recorder.eventsOfKind(Recorder::EventKind::Failed);
    QCOMPARE(failed.size(), size_t(4));
    QCOMPARE(run.errors.size(), 4);
    for (size_t i = 0; i < failed.size(); ++i)
    {
        QCOMPARE(failed[i].op, PDFPageContentProcessor::Operator::ColorSetDeviceRGBFilling);
        QCOMPARE(failed[i].level, PDFInteger(1));
        QCOMPARE(failed[i].operandCount, size_t(2));
        QCOMPARE(failed[i].message, run.errors[qsizetype(i)].message);
        QVERIFY2(failed[i].message.contains(QStringLiteral("2 operands")), qPrintable(failed[i].message));
    }

    // The operands of the first painting operator of the page: its stray one, before the cells
    // and - it is intact - after them
    const Recorder::Event* fillBefore = nullptr;
    const Recorder::Event* fillAfter = nullptr;
    for (const Recorder::Event& event : recorder.events)
    {
        if (event.level == 0 && event.text == "f")
        {
            if (event.kind == Recorder::EventKind::Before && !fillBefore)
            {
                fillBefore = &event;
            }
            if (event.kind == Recorder::EventKind::After && !fillAfter)
            {
                fillAfter = &event;
            }
        }
    }
    QVERIFY(fillBefore && fillAfter);
    QCOMPARE(fillBefore->operandCount, size_t(1));
    QCOMPARE(fillAfter->operandCount, size_t(1));

    // The four cells in black, then the blue square of the page
    QCOMPARE(recorder.paths.size(), size_t(5));
    for (size_t i = 0; i < 4; ++i)
    {
        QCOMPARE(recorder.paths[i].level, PDFInteger(1));
        QCOMPARE(recorder.paths[i].bounds, QRectF(0, 0, 10, 10));
        QVERIFY2(isSameColor(recorder.paths[i].fillColor, 0, 0, 0), qPrintable(recorder.paths[i].fillColor.name()));
    }
    QCOMPARE(recorder.paths[4].level, PDFInteger(0));
    QCOMPARE(recorder.paths[4].bounds, QRectF(100, 100, 20, 20));
    QVERIFY(isSameColor(recorder.paths[4].fillColor, 0, 0, 255));
}

void ContentProcessorTest::nestedContentOperandsOfGlyph()
{
    // The same wrong content as a glyph procedure of a Type 3 font: after d0 its rg has two
    // operands, and three numbers are left at its end. Each of the two glyphs fails the same
    // way with its own operands and is painted by the color of the text (black).
    ContentProcessorRun run(ContentTestDocuments::createType3Document("BT /T3 10 Tf 20 100 Td (AA) Tj ET", ContentTestDocuments::Type3Variant::StrayOperands));
    const Recorder& recorder = *run.processor;

    const QStringList glyph = QStringList() << QStringLiteral("0:G+65") << pairs(1, { "d0" }) << QStringLiteral("1:B:rg") << QStringLiteral("1:F:rg") << QStringLiteral("1:A:rg")
                                            << pairs(1, { "re", "f" }) << QStringLiteral("0:G-");
    QStringList expected;
    expected << pairs(0, { "BT", "Tf", "Td" }) << QStringLiteral("0:B:Tj") << glyph << glyph << QStringLiteral("0:A:Tj") << pairs(0, { "ET" });
    QCOMPARE(recorder.trace(), expected);

    const std::vector<Recorder::Event> failed = recorder.eventsOfKind(Recorder::EventKind::Failed);
    QCOMPARE(failed.size(), size_t(2));
    QCOMPARE(run.errors.size(), 2);
    for (size_t i = 0; i < failed.size(); ++i)
    {
        QCOMPARE(failed[i].level, PDFInteger(1));
        QCOMPARE(failed[i].operandCount, size_t(2));
        QCOMPARE(failed[i].message, run.errors[qsizetype(i)].message);
        QVERIFY2(failed[i].message.contains(QStringLiteral("2 operands")), qPrintable(failed[i].message));
    }

    // The first operator of each procedure has its own two operands
    for (const Recorder::Event& event : recorder.events)
    {
        if (event.kind == Recorder::EventKind::Before && event.text == "d0")
        {
            QCOMPARE(event.operandCount, size_t(2));
        }
    }

    QCOMPARE(recorder.paths.size(), size_t(2));
    for (const Recorder::PaintedPath& path : recorder.paths)
    {
        QCOMPARE(path.bounds, QRectF(0, 0, 50, 50));
        QVERIFY(isSameColor(path.fillColor, 0, 0, 0));
    }
    COMPARE_NEAR(recorder.lastEvent(Recorder::EventKind::After, "Tj")->textMatrix.dx(), 32.0);
}

void ContentProcessorTest::nestedContentOperandsOfForm()
{
    // The same wrong content as a form drawn twice: its rg fails with its own two operands
    // (the name of Do is not an operand of the form), the square of the form is painted by
    // the color of the page, and the numbers left by the first form do not reach the second
    // one nor the page
    const QByteArray formContent = "0 0 rg 0 0 10 10 re f 7 8 9";
    const QByteArray pageContent = "1 0 0 rg /FmS Do /FmS Do 0 0 5 5 re f";

    ContentProcessorRun run(ContentTestDocuments::createDocument(pageContent, [&formContent](PDFDocumentBuilder* builder)
    {
        PDFObject form = ContentTestDocuments::addStreamObject(builder, { { "Type", PDFObject::createName("XObject") },
                                                                          { "Subtype", PDFObject::createName("Form") },
                                                                          { "BBox", ContentTestDocuments::createIntegerArrayObject({ 0, 0, 10, 10 }) } }, formContent);

        PDFDictionaryBuilder resources;
        resources.addEntry(PDFInplaceOrMemoryString("XObject"), ContentTestDocuments::createDictionaryObject({ { "FmS", form } }));
        return resources;
    }));
    const Recorder& recorder = *run.processor;

    const QStringList form = QStringList() << QStringLiteral("0:B:Do") << QStringLiteral("1:B:rg") << QStringLiteral("1:F:rg") << QStringLiteral("1:A:rg")
                                           << pairs(1, { "re", "f" }) << QStringLiteral("0:A:Do");
    QStringList expected;
    expected << pairs(0, { "rg" }) << form << form << pairs(0, { "re", "f" });
    QCOMPARE(recorder.trace(), expected);

    const std::vector<Recorder::Event> failed = recorder.eventsOfKind(Recorder::EventKind::Failed);
    QCOMPARE(failed.size(), size_t(2));
    QCOMPARE(run.errors.size(), 2);
    for (size_t i = 0; i < failed.size(); ++i)
    {
        QCOMPARE(failed[i].level, PDFInteger(1));
        QCOMPARE(failed[i].operandCount, size_t(2));
        QCOMPARE(failed[i].message, run.errors[qsizetype(i)].message);
        QVERIFY2(failed[i].message.contains(QStringLiteral("2 operands")), qPrintable(failed[i].message));
    }

    // The operator re of the page after the forms has its own four operands
    const Recorder::Event* rectangle = recorder.lastEvent(Recorder::EventKind::Before, "re");
    QVERIFY(rectangle);
    QCOMPARE(rectangle->level, PDFInteger(0));
    QCOMPARE(rectangle->operandCount, size_t(4));

    QCOMPARE(recorder.paths.size(), size_t(3));
    QCOMPARE(recorder.paths[0].bounds, QRectF(0, 0, 10, 10));
    QCOMPARE(recorder.paths[1].bounds, QRectF(0, 0, 10, 10));
    QCOMPARE(recorder.paths[2].bounds, QRectF(0, 0, 5, 5));
    for (const Recorder::PaintedPath& path : recorder.paths)
    {
        QVERIFY(isSameColor(path.fillColor, 255, 0, 0));
    }
}

void ContentProcessorTest::interceptedOperators()
{
    // A recording descendant gets the same pairs of calls as before the change of the processor;
    // the only addition is the pair of the inline image (it had none)
    const QByteArray content = "q\n"
                               "2 0 0 2 10 20 cm\n"
                               "0.5 w 1 0 0 RG 0 0 1 rg\n"
                               "5 5 30 40 re B\n"
                               "Q\n"
                               "BI /W 1 /H 1 /BPC 8 /CS /G ID a EI\n"
                               "10 10 m 20 10 l 20 20 l h S\n"
                               "0 1 0 rg 0 0 4 4 re f*\n"
                               "BT /F1 12 Tf 20 150 Td (A) Tj ET\n"
                               "/Tag BMC EMC\n";

    using Operator = PDFPageContentProcessor::Operator;
    const std::vector<std::pair<Operator, QByteArray>> expectedOperators = { { Operator::SaveGraphicState, "q" },
                                                                             { Operator::AdjustCurrentTransformationMatrix, "cm" },
                                                                             { Operator::SetLineWidth, "w" },
                                                                             { Operator::ColorSetDeviceRGBStroking, "RG" },
                                                                             { Operator::ColorSetDeviceRGBFilling, "rg" },
                                                                             { Operator::Rectangle, "re" },
                                                                             { Operator::PathFillStrokeWinding, "B" },
                                                                             { Operator::RestoreGraphicState, "Q" },
                                                                             { Operator::InlineImage, "BI" },
                                                                             { Operator::MoveCurrentPoint, "m" },
                                                                             { Operator::LineTo, "l" },
                                                                             { Operator::LineTo, "l" },
                                                                             { Operator::EndSubpath, "h" },
                                                                             { Operator::PathStroke, "S" },
                                                                             { Operator::ColorSetDeviceRGBFilling, "rg" },
                                                                             { Operator::Rectangle, "re" },
                                                                             { Operator::PathFillEvenOdd, "f*" },
                                                                             { Operator::TextBegin, "BT" },
                                                                             { Operator::TextSetFontAndFontSize, "Tf" },
                                                                             { Operator::TextMoveByOffset, "Td" },
                                                                             { Operator::TextShowTextString, "Tj" },
                                                                             { Operator::TextEnd, "ET" },
                                                                             { Operator::MarkedContentBegin, "BMC" },
                                                                             { Operator::MarkedContentEnd, "EMC" } };

    ContentProcessorRun run(ContentTestDocuments::createDocument(content, [](PDFDocumentBuilder* builder)
    {
        PDFDictionaryBuilder resources;
        resources.addEntry(PDFInplaceOrMemoryString("Font"), ContentTestDocuments::createDictionaryObject({ { "F1", ContentTestDocuments::addStandardFontObject(builder, "Helvetica") } }));
        return resources;
    }));
    const Recorder& recorder = *run.processor;
    QVERIFY2(run.errors.isEmpty(), qPrintable(run.errorText()));

    // Each operator once before and once after, in the order of the content, nothing else
    QCOMPARE(recorder.events.size(), expectedOperators.size() * 2);
    for (size_t i = 0; i < expectedOperators.size(); ++i)
    {
        const Recorder::Event& before = recorder.events[2 * i];
        const Recorder::Event& after = recorder.events[2 * i + 1];
        QCOMPARE(before.kind, Recorder::EventKind::Before);
        QCOMPARE(after.kind, Recorder::EventKind::After);
        QCOMPARE(before.op, expectedOperators[i].first);
        QCOMPARE(after.op, expectedOperators[i].first);
        QCOMPARE(before.text, expectedOperators[i].second);
        QCOMPARE(after.text, expectedOperators[i].second);
    }

    // The value of the new operator is after all the old ones
    QVERIFY(int(Operator::InlineImage) > int(Operator::Invalid));

    QCOMPARE(recorder.markedContents.size(), size_t(1));
    QCOMPARE(recorder.markedContents[0].tag, QByteArray("Tag"));
    QCOMPARE(recorder.markedContentEndCount, 1);
    QCOMPARE(recorder.textSequences.size(), size_t(1));
    QCOMPARE(recorder.imageData.size(), size_t(1));
    QCOMPARE(recorder.imageData[0], QByteArray("a"));
}

void ContentProcessorTest::pathsAndGraphicState()
{
    // The paths and the graphic state, which the base class reports for a content without a text
    const QByteArray content = "q\n"
                               "2 0 0 2 10 20 cm\n"
                               "0.5 w 1 0 0 RG 0 0 1 rg\n"
                               "5 5 30 40 re B\n"
                               "Q\n"
                               "10 10 m 20 10 l 20 20 l h S\n"
                               "0 1 0 rg 0 0 4 4 re f*\n";

    ContentProcessorRun run(ContentTestDocuments::createDocument(content, &ContentTestDocuments::createNoResources));
    const Recorder& recorder = *run.processor;
    QVERIFY2(run.errors.isEmpty(), qPrintable(run.errorText()));
    QCOMPARE(recorder.paths.size(), size_t(3));

    // 5 5 30 40 re B inside of q ... Q: filled by the nonzero winding number rule and stroked,
    // the matrix of cm, the line width 0.5, a red stroke and a blue fill
    const Recorder::PaintedPath& rectangle = recorder.paths[0];
    QCOMPARE(rectangle.bounds, QRectF(5, 5, 30, 40));
    QVERIFY(rectangle.stroke);
    QVERIFY(rectangle.fill);
    QVERIFY(!rectangle.text);
    QCOMPARE(rectangle.fillRule, Qt::WindingFill);
    QCOMPARE(rectangle.matrix, QTransform(2, 0, 0, 2, 10, 20));
    QCOMPARE(rectangle.lineWidth, 0.5);
    QVERIFY(isSameColor(rectangle.strokeColor, 255, 0, 0));
    QVERIFY(isSameColor(rectangle.fillColor, 0, 0, 255));

    // The triangle after Q: stroked only, with the state of before q - the identity, the line
    // width 1 and the black of the initial graphic state (ISO 32000-2, 8.4.1, Table 51)
    const Recorder::PaintedPath& triangle = recorder.paths[1];
    QCOMPARE(triangle.bounds, QRectF(10, 10, 10, 10));
    QVERIFY(triangle.stroke);
    QVERIFY(!triangle.fill);
    QCOMPARE(triangle.matrix, QTransform());
    QCOMPARE(triangle.lineWidth, 1.0);
    QVERIFY(isSameColor(triangle.strokeColor, 0, 0, 0));
    QVERIFY(triangle.path.elementCount() >= 3);
    QCOMPARE(QPointF(triangle.path.elementAt(0)), QPointF(10, 10));
    QCOMPARE(QPointF(triangle.path.elementAt(1)), QPointF(20, 10));
    QCOMPARE(QPointF(triangle.path.elementAt(2)), QPointF(20, 20));
    QVERIFY(triangle.path.elementAt(0).isMoveTo());
    QVERIFY(triangle.path.elementAt(1).isLineTo());
    QVERIFY(triangle.path.elementAt(2).isLineTo());

    // 0 0 4 4 re f*: filled by the even-odd rule, green; the red and the blue of the first path
    // were restored by Q, the green is set after it
    const Recorder::PaintedPath& square = recorder.paths[2];
    QCOMPARE(square.bounds, QRectF(0, 0, 4, 4));
    QVERIFY(!square.stroke);
    QVERIFY(square.fill);
    QCOMPARE(square.fillRule, Qt::OddEvenFill);
    QCOMPARE(square.matrix, QTransform());
    QVERIFY(isSameColor(square.fillColor, 0, 255, 0));
    QVERIFY(isSameColor(square.strokeColor, 0, 0, 0));

    // The graphic state seen by the intercept: the matrix and the line width after cm and w,
    // the initial values again after Q
    const Recorder::Event* widthSet = recorder.lastEvent(Recorder::EventKind::After, "w");
    const Recorder::Event* restored = recorder.lastEvent(Recorder::EventKind::After, "Q");
    QVERIFY(widthSet && restored);
    QCOMPARE(widthSet->matrix, QTransform(2, 0, 0, 2, 10, 20));
    QCOMPARE(widthSet->lineWidth, 0.5);
    QCOMPARE(restored->matrix, QTransform());
    QCOMPARE(restored->lineWidth, 1.0);
    QCOMPARE(restored->alphaFilling, 1.0);
    QCOMPARE(restored->alphaStroking, 1.0);
    QVERIFY(!restored->hasSoftMask);
}

void ContentProcessorTest::overprintOfGraphicState_data()
{
    // ISO 32000-2, 8.4.5 (the entries of a graphics state parameter dictionary; PDF 1.7 reference,
    // Table 4.8): OP sets the overprint for stroking - and the one for the other painting
    // operations too, unless the same dictionary has op; op sets the overprint for the other
    // painting operations; the results of gs are cumulative, so a dictionary with neither of
    // them changes neither. An entry, which is not a boolean, is handled as an absent one.
    //
    // start: the dictionary of the gs before q, which sets the start state by both keys (empty =
    // the initial state, both false: 8.4.1); dictionary: the dictionary of the gs after q.
    QTest::addColumn<QByteArray>("start");
    QTest::addColumn<bool>("startStroking");
    QTest::addColumn<bool>("startFilling");
    QTest::addColumn<QByteArray>("dictionary");
    QTest::addColumn<bool>("stroking");
    QTest::addColumn<bool>("filling");

    // << /ca 0.5 >>: neither of the keys, nothing changes
    QTest::newRow("initial, /ca") << QByteArray() << false << false << QByteArray("GAlpha") << false << false;
    QTest::newRow("both on, /ca") << QByteArray("GOnFillOn") << true << true << QByteArray("GAlpha") << true << true;
    QTest::newRow("stroking only, /ca") << QByteArray("GOnFillOff") << true << false << QByteArray("GAlpha") << true << false;
    QTest::newRow("filling only, /ca") << QByteArray("GOffFillOn") << false << true << QByteArray("GAlpha") << false << true;

    // << /OP true >>: both parameters
    QTest::newRow("initial, /OP true") << QByteArray() << false << false << QByteArray("GOn") << true << true;
    QTest::newRow("both on, /OP true") << QByteArray("GOnFillOn") << true << true << QByteArray("GOn") << true << true;
    QTest::newRow("stroking only, /OP true") << QByteArray("GOnFillOff") << true << false << QByteArray("GOn") << true << true;
    QTest::newRow("filling only, /OP true") << QByteArray("GOffFillOn") << false << true << QByteArray("GOn") << true << true;

    // << /OP false >>: both parameters
    QTest::newRow("initial, /OP false") << QByteArray() << false << false << QByteArray("GOff") << false << false;
    QTest::newRow("both on, /OP false") << QByteArray("GOnFillOn") << true << true << QByteArray("GOff") << false << false;
    QTest::newRow("stroking only, /OP false") << QByteArray("GOnFillOff") << true << false << QByteArray("GOff") << false << false;
    QTest::newRow("filling only, /OP false") << QByteArray("GOffFillOn") << false << true << QByteArray("GOff") << false << false;

    // << /op true >>: the other painting operations only, the stroking stays
    QTest::newRow("initial, /op true") << QByteArray() << false << false << QByteArray("GFillOn") << false << true;
    QTest::newRow("both on, /op true") << QByteArray("GOnFillOn") << true << true << QByteArray("GFillOn") << true << true;
    QTest::newRow("stroking only, /op true") << QByteArray("GOnFillOff") << true << false << QByteArray("GFillOn") << true << true;
    QTest::newRow("filling only, /op true") << QByteArray("GOffFillOn") << false << true << QByteArray("GFillOn") << false << true;

    // << /op false >>: the other painting operations only, the stroking stays
    QTest::newRow("initial, /op false") << QByteArray() << false << false << QByteArray("GFillOff") << false << false;
    QTest::newRow("both on, /op false") << QByteArray("GOnFillOn") << true << true << QByteArray("GFillOff") << true << false;
    QTest::newRow("stroking only, /op false") << QByteArray("GOnFillOff") << true << false << QByteArray("GFillOff") << true << false;
    QTest::newRow("filling only, /op false") << QByteArray("GOffFillOn") << false << true << QByteArray("GFillOff") << false << false;

    // << /OP true /op false >>: each key sets its parameter
    QTest::newRow("initial, /OP true /op false") << QByteArray() << false << false << QByteArray("GOnFillOff") << true << false;
    QTest::newRow("both on, /OP true /op false") << QByteArray("GOnFillOn") << true << true << QByteArray("GOnFillOff") << true << false;
    QTest::newRow("stroking only, /OP true /op false") << QByteArray("GOnFillOff") << true << false << QByteArray("GOnFillOff") << true << false;
    QTest::newRow("filling only, /OP true /op false") << QByteArray("GOffFillOn") << false << true << QByteArray("GOnFillOff") << true << false;

    // << /OP false /op true >>: each key sets its parameter
    QTest::newRow("initial, /OP false /op true") << QByteArray() << false << false << QByteArray("GOffFillOn") << false << true;
    QTest::newRow("both on, /OP false /op true") << QByteArray("GOnFillOn") << true << true << QByteArray("GOffFillOn") << false << true;
    QTest::newRow("stroking only, /OP false /op true") << QByteArray("GOnFillOff") << true << false << QByteArray("GOffFillOn") << false << true;
    QTest::newRow("filling only, /OP false /op true") << QByteArray("GOffFillOn") << false << true << QByteArray("GOffFillOn") << false << true;

    // << /OP /true /op 1 >>: a name and an integer are not booleans, both entries are as if
    // absent and nothing changes
    QTest::newRow("initial, both of a wrong type") << QByteArray() << false << false << QByteArray("GBad") << false << false;
    QTest::newRow("both on, both of a wrong type") << QByteArray("GOnFillOn") << true << true << QByteArray("GBad") << true << true;
    QTest::newRow("stroking only, both of a wrong type") << QByteArray("GOnFillOff") << true << false << QByteArray("GBad") << true << false;
    QTest::newRow("filling only, both of a wrong type") << QByteArray("GOffFillOn") << false << true << QByteArray("GBad") << false << true;

    // << /OP true /op 1 >>: op is as if absent, OP sets both parameters
    QTest::newRow("initial, /OP true, op of a wrong type") << QByteArray() << false << false << QByteArray("GOnBadFill") << true << true;
    QTest::newRow("both on, /OP true, op of a wrong type") << QByteArray("GOnFillOn") << true << true << QByteArray("GOnBadFill") << true << true;
    QTest::newRow("stroking only, /OP true, op of a wrong type") << QByteArray("GOnFillOff") << true << false << QByteArray("GOnBadFill") << true << true;
    QTest::newRow("filling only, /OP true, op of a wrong type") << QByteArray("GOffFillOn") << false << true << QByteArray("GOnBadFill") << true << true;

    // << /OP 1 /op true >>: OP is as if absent, the stroking stays, op sets its parameter
    QTest::newRow("initial, OP of a wrong type, /op true") << QByteArray() << false << false << QByteArray("GBadFillOn") << false << true;
    QTest::newRow("both on, OP of a wrong type, /op true") << QByteArray("GOnFillOn") << true << true << QByteArray("GBadFillOn") << true << true;
    QTest::newRow("stroking only, OP of a wrong type, /op true") << QByteArray("GOnFillOff") << true << false << QByteArray("GBadFillOn") << true << true;
    QTest::newRow("filling only, OP of a wrong type, /op true") << QByteArray("GOffFillOn") << false << true << QByteArray("GBadFillOn") << false << true;
}

void ContentProcessorTest::overprintOfGraphicState()
{
    QFETCH(QByteArray, start);
    QFETCH(bool, startStroking);
    QFETCH(bool, startFilling);
    QFETCH(QByteArray, dictionary);
    QFETCH(bool, stroking);
    QFETCH(bool, filling);

    // The start state by a gs with both keys (none = the initial state), then the tested gs
    // inside of q ... Q
    QByteArray content;
    if (!start.isEmpty())
    {
        content += "/" + start + " gs\n";
    }
    content += "q\n/" + dictionary + " gs\nQ\n";

    ContentProcessorRun run(ContentTestDocuments::createDocument(content, [](PDFDocumentBuilder* builder)
    {
        Q_UNUSED(builder);

        // The graphics state parameter dictionaries of the rows, each with its entries only
        using Documents = ContentTestDocuments;
        PDFDictionaryBuilder resources;
        resources.addEntry(PDFInplaceOrMemoryString("ExtGState"), Documents::createDictionaryObject({
            { "GAlpha", Documents::createDictionaryObject({ { "ca", PDFObject::createReal(0.5) } }) },
            { "GOn", Documents::createDictionaryObject({ { "OP", PDFObject::createBool(true) } }) },
            { "GOff", Documents::createDictionaryObject({ { "OP", PDFObject::createBool(false) } }) },
            { "GFillOn", Documents::createDictionaryObject({ { "op", PDFObject::createBool(true) } }) },
            { "GFillOff", Documents::createDictionaryObject({ { "op", PDFObject::createBool(false) } }) },
            { "GOnFillOn", Documents::createDictionaryObject({ { "OP", PDFObject::createBool(true) }, { "op", PDFObject::createBool(true) } }) },
            { "GOnFillOff", Documents::createDictionaryObject({ { "OP", PDFObject::createBool(true) }, { "op", PDFObject::createBool(false) } }) },
            { "GOffFillOn", Documents::createDictionaryObject({ { "OP", PDFObject::createBool(false) }, { "op", PDFObject::createBool(true) } }) },
            { "GBad", Documents::createDictionaryObject({ { "OP", PDFObject::createName("true") }, { "op", PDFObject::createInteger(1) } }) },
            { "GOnBadFill", Documents::createDictionaryObject({ { "OP", PDFObject::createBool(true) }, { "op", PDFObject::createInteger(1) } }) },
            { "GBadFillOn", Documents::createDictionaryObject({ { "OP", PDFObject::createInteger(1) }, { "op", PDFObject::createBool(true) } }) } }));
        return resources;
    }));
    const Recorder& recorder = *run.processor;
    QVERIFY2(run.errors.isEmpty(), qPrintable(run.errorText()));

    QStringList expectedTrace = pairs(0, { "q", "gs", "Q" });
    if (!start.isEmpty())
    {
        expectedTrace = pairs(0, { "gs" }) + expectedTrace;
    }
    QCOMPARE(recorder.trace(), expectedTrace);

    // The start state, as q saves it
    const Recorder::Event* saved = recorder.lastEvent(Recorder::EventKind::After, "q");
    QVERIFY(saved);
    QCOMPARE(saved->overprintStroking, startStroking);
    QCOMPARE(saved->overprintFilling, startFilling);

    // The state after the tested gs (the last one of the content)
    const Recorder::Event* set = recorder.lastEvent(Recorder::EventKind::After, "gs");
    QVERIFY(set);
    QCOMPARE(set->overprintStroking, stroking);
    QCOMPARE(set->overprintFilling, filling);

    // The dictionary was applied: only /GAlpha has /ca, the alpha of the initial state is 1
    QCOMPARE(set->alphaFilling, dictionary == "GAlpha" ? 0.5 : 1.0);

    // Q restores both parameters of the start state
    const Recorder::Event* restored = recorder.lastEvent(Recorder::EventKind::After, "Q");
    QVERIFY(restored);
    QCOMPARE(restored->overprintStroking, startStroking);
    QCOMPARE(restored->overprintFilling, startFilling);
    QCOMPARE(restored->alphaFilling, 1.0);
}

void ContentProcessorTest::fontOfGraphicState_data()
{
    // PDF Reference 1.7, Table 4.8 (the entries of a graphics state parameter dictionary; the
    // corresponding section of ISO 32000-2 is 8.4.5), Font: "An array of the form [font size],
    // where font is an indirect reference to a font dictionary and size is a number expressed in
    // text space units. These two objects correspond to the operands of the Tf operator;
    // however, the first operand is an indirect object reference instead of a resource name."
    //
    // The page sets the font /FB of the size 10 by Tf; the fonts and the dictionaries are those
    // of createFontStateResources, each dictionary with /LW 3 beside its /Font. font, size: the
    // font and its size after the gs; error: the only error of the page (empty = none). A wrong
    // entry leaves the font and its size alone, the operator does not fail and the rest of the
    // dictionary is applied.
    QTest::addColumn<QByteArray>("dictionary");
    QTest::addColumn<QByteArray>("font");
    QTest::addColumn<double>("size");
    QTest::addColumn<QString>("error");

    const QString invalid = QStringLiteral("Font of the graphic state is invalid.");

    // The conforming entry
    QTest::newRow("reference, integer size") << QByteArray("GReference") << QByteArray("FA") << 12.0 << QString();
    QTest::newRow("reference, real size") << QByteArray("GRealSize") << QByteArray("FA") << 7.5 << QString();
    QTest::newRow("reference, indirect size") << QByteArray("GIndirectSize") << QByteArray("FA") << 12.0 << QString();
    QTest::newRow("indirect array") << QByteArray("GIndirectArray") << QByteArray("FA") << 12.0 << QString();
    QTest::newRow("reference to a simple font") << QByteArray("GSimpleFont") << QByteArray("FS") << 12.0 << QString();
    QTest::newRow("reference to a font without a resource name") << QByteArray("GUnnamedFont") << QByteArray("FU") << 12.0 << QString();

    // A name of the font resources in place of the reference: accepted (NOTES.txt, 8.4.5)
    QTest::newRow("name of a resource") << QByteArray("GName") << QByteArray("FA") << 12.0 << QString();

    // Not an array of two items
    QTest::newRow("no array") << QByteArray("GNoArray") << QByteArray("FB") << 10.0 << invalid;
    QTest::newRow("empty array") << QByteArray("GEmpty") << QByteArray("FB") << 10.0 << invalid;
    QTest::newRow("one item") << QByteArray("GOneItem") << QByteArray("FB") << 10.0 << invalid;
    QTest::newRow("three items") << QByteArray("GThreeItems") << QByteArray("FB") << 10.0 << invalid;

    // The font is neither a reference nor a name
    QTest::newRow("direct font dictionary") << QByteArray("GDirectFont") << QByteArray("FB") << 10.0 << invalid;
    QTest::newRow("number in place of the font") << QByteArray("GNumberFont") << QByteArray("FB") << 10.0 << invalid;

    // The size is not a finite number
    QTest::newRow("size is a name") << QByteArray("GSizeName") << QByteArray("FB") << 10.0 << invalid;
    QTest::newRow("size is null") << QByteArray("GSizeNull") << QByteArray("FB") << 10.0 << invalid;
    QTest::newRow("size is infinite") << QByteArray("GSizeInfinite") << QByteArray("FB") << 10.0 << invalid;
    QTest::newRow("size is not a number") << QByteArray("GSizeNaN") << QByteArray("FB") << 10.0 << invalid;
    QTest::newRow("name, size is a name") << QByteArray("GNameSizeName") << QByteArray("FB") << 10.0 << invalid;

    // The reference does not lead to a font: the error of the font, as Tf reports it
    QTest::newRow("reference to a number") << QByteArray("GReferenceToNumber") << QByteArray("FB") << 10.0 << QStringLiteral("Font object must be a dictionary.");
    QTest::newRow("reference to a dictionary, which is no font") << QByteArray("GReferenceToNoFont") << QByteArray("FB") << 10.0 << QStringLiteral("Invalid font type.");
    QTest::newRow("reference to no object") << QByteArray("GReferenceToNothing") << QByteArray("FB") << 10.0 << QStringLiteral("Font object must be a dictionary.");

    // The name is not in the font resources: the error of Tf
    QTest::newRow("name without a resource") << QByteArray("GNameMissing") << QByteArray("FB") << 10.0 << QStringLiteral("Font 'Missing' not found in font dictionary.");
}

void ContentProcessorTest::fontOfGraphicState()
{
    QFETCH(QByteArray, dictionary);
    QFETCH(QByteArray, font);
    QFETCH(double, size);
    QFETCH(QString, error);

    // The font of Tf; the tested gs and a text inside of q ... Q, the same text after it
    const QByteArray content = "/FB 10 Tf\n"
                               "q\n"
                               "/" + dictionary + " gs\n"
                               "BT 20 100 Td (A) Tj ET\n"
                               "Q\n"
                               "BT 20 100 Td (A) Tj ET\n";

    FontStateFonts fonts;
    ContentProcessorRun run(ContentTestDocuments::createDocument(content, [&fonts](PDFDocumentBuilder* builder)
    {
        return createFontStateResources(builder, &fonts);
    }));
    const Recorder& recorder = *run.processor;

    // A wrong entry is one error of the list; no operator fails, also not the gs
    if (error.isEmpty())
    {
        QVERIFY2(run.errors.isEmpty(), qPrintable(run.errorText()));
    }
    else
    {
        QCOMPARE(run.errors.size(), 1);
        QCOMPARE(run.errors[0].type, RenderErrorType::Error);
        QCOMPARE(run.errors[0].message, error);
    }
    QVERIFY(recorder.eventsOfKind(Recorder::EventKind::Failed).empty());

    // The fonts of the font dictionaries, as the font cache of the processor holds them
    const PDFObject& expectedFontObject = font == "FA" ? fonts.fontA : (font == "FS" ? fonts.fontS : (font == "FU" ? fonts.fontU : fonts.fontB));
    const PDFFont* expectedFontB = run.fontCache.getFont(fonts.fontB, QByteArray()).get();
    const PDFFont* expectedFont = run.fontCache.getFont(expectedFontObject, QByteArray()).get();
    QVERIFY(expectedFont && expectedFontB);
    QCOMPARE(expectedFont == expectedFontB, font == "FB");

    // The identifier of the font is its name in the font resources, also when the gs created
    // the font by its reference; a font, which the resources do not have, has none
    QCOMPARE(expectedFont->getFontId(), font == "FU" ? QByteArray() : font);

    // The state after the gs: the font and its size, and the line width of the same dictionary
    const Recorder::Event* set = recorder.lastEvent(Recorder::EventKind::After, "gs");
    QVERIFY(set);
    QCOMPARE(set->font, expectedFont);
    QCOMPARE(set->fontSize, size);
    QCOMPARE(set->lineWidth, 3.0);

    // The two texts of the page (a glyph procedure of the fonts shows no text)
    std::vector<Recorder::Event> shows;
    for (const Recorder::Event& event : recorder.eventsOfKind(Recorder::EventKind::After))
    {
        if (event.text == "Tj")
        {
            shows.push_back(event);
        }
    }
    QCOMPARE(shows.size(), size_t(2));

    // The font and the size reach the text. The code 65 has the width 0.6 of the text space in
    // both Type 3 fonts, the text moves by tx = 0.6 * Tfs (PDF Reference 1.7, 5.3.3); the
    // advance of the simple font comes from a font of the system, a test cannot know it
    QCOMPARE(shows[0].font, expectedFont);
    QCOMPARE(shows[0].fontSize, size);
    if (font != "FS")
    {
        COMPARE_NEAR(shows[0].textMatrix.dx(), 20.0 + 0.6 * size);
    }
    else
    {
        QVERIFY(shows[0].textMatrix.dx() > 20.0);
    }

    // Q restores the font, the size and the line width of before q
    const Recorder::Event* restored = recorder.lastEvent(Recorder::EventKind::After, "Q");
    QVERIFY(restored);
    QCOMPARE(restored->font, expectedFontB);
    QCOMPARE(restored->fontSize, 10.0);
    QCOMPARE(restored->lineWidth, 1.0);
    QCOMPARE(shows[1].font, expectedFontB);
    QCOMPARE(shows[1].fontSize, 10.0);
    COMPARE_NEAR(shows[1].textMatrix.dx(), 26.0);
}

void ContentProcessorTest::fontOfGraphicStateOfIgnoredText()
{
    // A processor, which ignores the text (isTextIgnored), loads no font: Tf does nothing there,
    // and the entry Font of a graphics state parameter dictionary is not read either - neither
    // the conforming one nor a wrong one (no error). The rest of the dictionary is applied.
    const QByteArray content = "/GReference gs\n"
                               "/GOneItem gs\n";

    FontStateFonts fonts;
    ContentProcessorRun run(ContentTestDocuments::createDocument(content, [&fonts](PDFDocumentBuilder* builder)
    {
        return createFontStateResources(builder, &fonts);
    }), false, false);
    run.processor->ignoreText = true;
    run.errors = run.processor->processContents();
    const Recorder& recorder = *run.processor;

    QVERIFY2(run.errors.isEmpty(), qPrintable(run.errorText()));
    QCOMPARE(recorder.trace(), pairs(0, { "gs", "gs" }));

    const std::vector<Recorder::Event> after = recorder.eventsOfKind(Recorder::EventKind::After);
    QCOMPARE(after.size(), size_t(2));
    for (const Recorder::Event& event : after)
    {
        QCOMPARE(event.font, static_cast<const PDFFont*>(nullptr));
        QCOMPARE(event.fontSize, 0.0);
        QCOMPARE(event.lineWidth, 3.0);
    }
}

void ContentProcessorTest::blendModeOfGraphicState_data()
{
    // PDF Reference 1.7, Table 4.8 (BM: name or array; the corresponding section of ISO 32000-2
    // is 8.4.5) and 7.5.2: the value "is either a name object, designating one of the standard
    // blend modes [...], or an array of such names. In the latter case, the application should
    // use the first blend mode in the array that it recognizes (or Normal if it recognizes none
    // of them)."
    //
    // The page sets the blend mode Screen by a gs with a single name; blendMode: the mode after
    // the tested gs; error: the only error of the page (empty = none). An item of the array,
    // which is not a name, is skipped; an array without a name changes nothing, as an entry of a
    // wrong type; an unknown name alone is the mode Normal and an error.
    QTest::addColumn<QByteArray>("dictionary");
    QTest::addColumn<int>("blendMode");
    QTest::addColumn<QString>("error");

    const QString invalidFoo = QStringLiteral("Blend mode 'Foo' is invalid.");

    // A single name, and a value, which is neither a name nor an array
    QTest::newRow("name") << QByteArray("GName") << int(BlendMode::Multiply) << QString();
    QTest::newRow("unknown name") << QByteArray("GUnknown") << int(BlendMode::Normal) << invalidFoo;
    QTest::newRow("number") << QByteArray("GNumber") << int(BlendMode::Screen) << QString();

    // Arrays of names
    QTest::newRow("[/Multiply /Darken]") << QByteArray("GSupportedFirst") << int(BlendMode::Multiply) << QString();
    QTest::newRow("[/Foo /Multiply]") << QByteArray("GUnknownFirst") << int(BlendMode::Multiply) << QString();
    QTest::newRow("[/Foo /Bar /Darken /Multiply]") << QByteArray("GUnknownTwice") << int(BlendMode::Darken) << QString();
    QTest::newRow("[/Foo /Bar]") << QByteArray("GAllUnknown") << int(BlendMode::Normal) << invalidFoo;
    QTest::newRow("[]") << QByteArray("GEmpty") << int(BlendMode::Screen) << QString();

    // Items, which are not names
    QTest::newRow("[1 /Multiply]") << QByteArray("GNumberFirst") << int(BlendMode::Multiply) << QString();
    QTest::newRow("[1 2.5 true]") << QByteArray("GNoNames") << int(BlendMode::Screen) << QString();
    QTest::newRow("[1 /Foo]") << QByteArray("GNumberUnknown") << int(BlendMode::Normal) << invalidFoo;
    QTest::newRow("[reference to /Multiply]") << QByteArray("GIndirectName") << int(BlendMode::Multiply) << QString();
}

void ContentProcessorTest::blendModeOfGraphicState()
{
    QFETCH(QByteArray, dictionary);
    QFETCH(int, blendMode);
    QFETCH(QString, error);

    const QByteArray content = "/GStart gs\n"
                               "q\n"
                               "/" + dictionary + " gs\n"
                               "Q\n";

    ContentProcessorRun run(ContentTestDocuments::createDocument(content, [](PDFDocumentBuilder* builder)
    {
        using Documents = ContentTestDocuments;

        const auto name = [](const char* text)
        {
            return PDFObject::createName(text);
        };
        const auto state = [](PDFObject value)
        {
            return Documents::createDictionaryObject({ { "BM", std::move(value) } });
        };
        const PDFObject number = PDFObject::createInteger(1);
        const PDFObject indirectName = Documents::addObject(builder, name("Multiply"));

        PDFDictionaryBuilder resources;
        resources.addEntry(PDFInplaceOrMemoryString("ExtGState"), Documents::createDictionaryObject({
            { "GStart", state(name("Screen")) },
            { "GName", state(name("Multiply")) },
            { "GUnknown", state(name("Foo")) },
            { "GNumber", state(number) },
            { "GSupportedFirst", state(Documents::createArrayObject({ name("Multiply"), name("Darken") })) },
            { "GUnknownFirst", state(Documents::createArrayObject({ name("Foo"), name("Multiply") })) },
            { "GUnknownTwice", state(Documents::createArrayObject({ name("Foo"), name("Bar"), name("Darken"), name("Multiply") })) },
            { "GAllUnknown", state(Documents::createArrayObject({ name("Foo"), name("Bar") })) },
            { "GEmpty", state(Documents::createArrayObject({ })) },
            { "GNumberFirst", state(Documents::createArrayObject({ number, name("Multiply") })) },
            { "GNoNames", state(Documents::createArrayObject({ number, PDFObject::createReal(2.5), PDFObject::createBool(true) })) },
            { "GNumberUnknown", state(Documents::createArrayObject({ number, name("Foo") })) },
            { "GIndirectName", state(Documents::createArrayObject({ indirectName })) } }));
        return resources;
    }));
    const Recorder& recorder = *run.processor;

    // An unknown blend mode is one error of the list; the operator gs does not fail
    if (error.isEmpty())
    {
        QVERIFY2(run.errors.isEmpty(), qPrintable(run.errorText()));
    }
    else
    {
        QCOMPARE(run.errors.size(), 1);
        QCOMPARE(run.errors[0].type, RenderErrorType::NotImplemented);
        QCOMPARE(run.errors[0].message, error);
    }
    QCOMPARE(recorder.trace(), pairs(0, { "gs", "q", "gs", "Q" }));

    // The start state, as q saves it; the state after the tested gs; Q restores the start state
    const Recorder::Event* saved = recorder.lastEvent(Recorder::EventKind::After, "q");
    const Recorder::Event* set = recorder.lastEvent(Recorder::EventKind::After, "gs");
    const Recorder::Event* restored = recorder.lastEvent(Recorder::EventKind::After, "Q");
    QVERIFY(saved && set && restored);
    QCOMPARE(int(saved->blendMode), int(BlendMode::Screen));
    QCOMPARE(int(set->blendMode), blendMode);
    QCOMPARE(int(restored->blendMode), int(BlendMode::Screen));
}

void ContentProcessorTest::softMaskOfGraphicState_data()
{
    // PDF Reference 1.7, Table 4.8 (SMask: dictionary or name; the corresponding section of
    // ISO 32000-2 is 8.4.5): "altering it with the gs operator completely replaces the old value
    // with the new one"; and 7.5.3, Mask Shape and Opacity: the current soft mask "contains a
    // soft-mask dictionary [...]. The name None may be specified in place of a soft-mask
    // dictionary, denoting the absence of a soft mask." A value, which is neither a dictionary
    // nor the name None, is handled as an absent entry: the current soft mask stays, nothing is
    // reported.
    //
    // withMask: the page sets the mask "start" (/S /Alpha) by a gs before q; mask: the mask
    // after the tested gs - "none", "start" or "new" (/S /Luminosity, the mask of the tested
    // dictionaries).
    QTest::addColumn<bool>("withMask");
    QTest::addColumn<QByteArray>("dictionary");
    QTest::addColumn<QByteArray>("mask");

    // No entry
    QTest::newRow("no mask, no entry") << false << QByteArray("GAbsent") << QByteArray("none");
    QTest::newRow("mask, no entry") << true << QByteArray("GAbsent") << QByteArray("start");

    // A soft-mask dictionary, direct and by a reference
    QTest::newRow("no mask, dictionary") << false << QByteArray("GMask") << QByteArray("new");
    QTest::newRow("mask, dictionary") << true << QByteArray("GMask") << QByteArray("new");
    QTest::newRow("no mask, reference to a dictionary") << false << QByteArray("GMaskReference") << QByteArray("new");
    QTest::newRow("mask, reference to a dictionary") << true << QByteArray("GMaskReference") << QByteArray("new");

    // The name None, direct and by a reference
    QTest::newRow("no mask, /None") << false << QByteArray("GNone") << QByteArray("none");
    QTest::newRow("mask, /None") << true << QByteArray("GNone") << QByteArray("none");
    QTest::newRow("mask, reference to /None") << true << QByteArray("GNoneReference") << QByteArray("none");

    // Values of a wrong type and an unknown name
    QTest::newRow("no mask, number") << false << QByteArray("GNumber") << QByteArray("none");
    QTest::newRow("mask, number") << true << QByteArray("GNumber") << QByteArray("start");
    QTest::newRow("no mask, array") << false << QByteArray("GArray") << QByteArray("none");
    QTest::newRow("mask, array") << true << QByteArray("GArray") << QByteArray("start");
    QTest::newRow("no mask, unknown name") << false << QByteArray("GName") << QByteArray("none");
    QTest::newRow("mask, unknown name") << true << QByteArray("GName") << QByteArray("start");
    QTest::newRow("no mask, reference to a number") << false << QByteArray("GReferenceToNumber") << QByteArray("none");
    QTest::newRow("mask, reference to a number") << true << QByteArray("GReferenceToNumber") << QByteArray("start");
    QTest::newRow("no mask, reference to a stream") << false << QByteArray("GReferenceToStream") << QByteArray("none");
    QTest::newRow("mask, reference to a stream") << true << QByteArray("GReferenceToStream") << QByteArray("start");
}

void ContentProcessorTest::softMaskOfGraphicState()
{
    QFETCH(bool, withMask);
    QFETCH(QByteArray, dictionary);
    QFETCH(QByteArray, mask);

    // The contents of the transparency groups of the two masks: a test sees, which mask the
    // state holds
    const QByteArray startGroupContent = "0 g 0 0 10 10 re f";
    const QByteArray newGroupContent = "1 g 0 0 20 20 re f";

    QByteArray content;
    if (withMask)
    {
        content += "/GStart gs\n";
    }
    content += "q\n/" + dictionary + " gs\nQ\n";

    ContentProcessorRun run(ContentTestDocuments::createDocument(content, [&startGroupContent, &newGroupContent](PDFDocumentBuilder* builder)
    {
        using Documents = ContentTestDocuments;

        const auto addGroup = [builder](const QByteArray& groupContent)
        {
            return Documents::addStreamObject(builder, { { "Type", PDFObject::createName("XObject") },
                                                         { "Subtype", PDFObject::createName("Form") },
                                                         { "BBox", Documents::createIntegerArrayObject({ 0, 0, 200, 200 }) },
                                                         { "Group", Documents::createDictionaryObject({ { "S", PDFObject::createName("Transparency") },
                                                                                                        { "CS", PDFObject::createName("DeviceGray") } }) } }, groupContent);
        };
        const auto state = [](PDFObject value)
        {
            return Documents::createDictionaryObject({ { "SMask", std::move(value) } });
        };

        const PDFObject newGroup = addGroup(newGroupContent);
        const PDFObject startMask = Documents::createDictionaryObject({ { "Type", PDFObject::createName("Mask") },
                                                                        { "S", PDFObject::createName("Alpha") },
                                                                        { "G", addGroup(startGroupContent) } });
        const PDFObject newMask = Documents::createDictionaryObject({ { "Type", PDFObject::createName("Mask") },
                                                                      { "S", PDFObject::createName("Luminosity") },
                                                                      { "G", newGroup } });

        PDFDictionaryBuilder resources;
        resources.addEntry(PDFInplaceOrMemoryString("ExtGState"), Documents::createDictionaryObject({
            { "GStart", state(startMask) },
            { "GAbsent", Documents::createDictionaryObject({ { "ca", PDFObject::createReal(0.5) } }) },
            { "GMask", state(newMask) },
            { "GMaskReference", state(Documents::addObject(builder, newMask)) },
            { "GNone", state(PDFObject::createName("None")) },
            { "GNoneReference", state(Documents::addObject(builder, PDFObject::createName("None"))) },
            { "GNumber", state(PDFObject::createInteger(1)) },
            { "GArray", state(Documents::createArrayObject({ PDFObject::createName("None") })) },
            { "GName", state(PDFObject::createName("Foo")) },
            { "GReferenceToNumber", state(Documents::addObject(builder, PDFObject::createInteger(1))) },
            { "GReferenceToStream", state(newGroup) } }));
        return resources;
    }));
    const Recorder& recorder = *run.processor;

    // Nothing is reported, also not a value of a wrong type
    QVERIFY2(run.errors.isEmpty(), qPrintable(run.errorText()));

    QStringList expectedTrace = pairs(0, { "q", "gs", "Q" });
    if (withMask)
    {
        expectedTrace = pairs(0, { "gs" }) + expectedTrace;
    }
    QCOMPARE(recorder.trace(), expectedTrace);

    // The start state, as q saves it; the state after the tested gs; Q restores the start state
    const Recorder::Event* saved = recorder.lastEvent(Recorder::EventKind::After, "q");
    const Recorder::Event* set = recorder.lastEvent(Recorder::EventKind::After, "gs");
    const Recorder::Event* restored = recorder.lastEvent(Recorder::EventKind::After, "Q");
    QVERIFY(saved && set && restored);

    QCOMPARE(saved->hasSoftMask, withMask);
    QCOMPARE(saved->isSoftMaskLuminosity, false);
    QCOMPARE(saved->softMaskContent, withMask ? startGroupContent : QByteArray());

    QCOMPARE(set->hasSoftMask, mask != "none");
    QCOMPARE(set->isSoftMaskLuminosity, mask == "new");
    QCOMPARE(set->softMaskContent, mask == "new" ? newGroupContent : (mask == "start" ? startGroupContent : QByteArray()));

    QCOMPARE(restored->hasSoftMask, withMask);
    QCOMPARE(restored->isSoftMaskLuminosity, false);
    QCOMPARE(restored->softMaskContent, withMask ? startGroupContent : QByteArray());
}

void ContentProcessorTest::textEndWithoutBegin_data()
{
    // PDF Reference 1.7, 5.3: "A text object begins with the BT operator and ends with the ET
    // operator"; Table 5.4, ET: "End a text object, discarding the text matrix." An ET without
    // an open text object ends nothing: it is an error of that operator only, and the text
    // objects after it are whole.
    //
    // errors: the failed ET operators of the page; begins, ends: the text objects, which were
    // begun and ended (performTextBegin, performTextEnd); shows: the texts shown, each one
    // inside of an open text object.
    QTest::addColumn<QByteArray>("content");
    QTest::addColumn<int>("errors");
    QTest::addColumn<int>("begins");
    QTest::addColumn<int>("ends");
    QTest::addColumn<int>("shows");

    QTest::newRow("BT text ET") << QByteArray("BT /F1 10 Tf 20 100 Td (A) Tj ET") << 0 << 1 << 1 << 1;
    QTest::newRow("ET") << QByteArray("ET") << 1 << 0 << 0 << 0;
    QTest::newRow("ET ET BT text ET") << QByteArray("ET ET BT /F1 10 Tf 20 100 Td (A) Tj ET") << 2 << 1 << 1 << 1;
    QTest::newRow("BT ET ET BT text ET") << QByteArray("BT ET ET BT /F1 10 Tf 20 100 Td (A) Tj ET") << 1 << 2 << 2 << 1;
    QTest::newRow("ET BT text ET ET BT text ET") << QByteArray("ET BT /F1 10 Tf 20 100 Td (A) Tj ET ET BT 20 50 Td (B) Tj ET") << 2 << 2 << 2 << 2;
}

void ContentProcessorTest::textEndWithoutBegin()
{
    QFETCH(QByteArray, content);
    QFETCH(int, errors);
    QFETCH(int, begins);
    QFETCH(int, ends);
    QFETCH(int, shows);

    ContentProcessorRun run(ContentTestDocuments::createFontsDocument(content));
    const Recorder& recorder = *run.processor;

    // Each ET without a text object fails, and nothing else
    const std::vector<Recorder::Event> failed = recorder.eventsOfKind(Recorder::EventKind::Failed);
    QCOMPARE(run.errors.size(), errors);
    QCOMPARE(failed.size(), size_t(errors));
    for (size_t i = 0; i < failed.size(); ++i)
    {
        QCOMPARE(failed[i].op, PDFPageContentProcessor::Operator::TextEnd);
        QCOMPARE(run.errors[qsizetype(i)].type, RenderErrorType::Error);
        QCOMPARE(run.errors[qsizetype(i)].message, QStringLiteral("Text object ended more than once."));
        QVERIFY(!failed[i].isTextObjectOpen);
    }

    QCOMPARE(recorder.textBeginCount, begins);
    QCOMPARE(recorder.textEndCount, ends);

    // Each text is shown inside of an open text object; no text object is open at the end
    int shown = 0;
    for (const Recorder::Event& event : recorder.eventsOfKind(Recorder::EventKind::After))
    {
        if (event.text == "Tj")
        {
            ++shown;
            QVERIFY(event.isTextObjectOpen);
        }
    }
    QCOMPARE(shown, shows);
    QCOMPARE(recorder.textSequences.size(), size_t(shows));
    QVERIFY(!recorder.events.back().isTextObjectOpen);
}

void ContentProcessorTest::textEndWithoutBeginInForm()
{
    // The form has two ET without a text object and a whole text object after them; the page
    // shows a text after the form. The two ET fail, both texts are shown in a text object.
    const QByteArray formContent = "ET ET BT /F1 10 Tf 5 5 Td (A) Tj ET";
    const QByteArray content = "/Fm1 Do\n"
                               "BT /F1 10 Tf 20 100 Td (A) Tj ET\n";

    ContentProcessorRun run(ContentTestDocuments::createDocument(content, [&formContent](PDFDocumentBuilder* builder)
    {
        using Documents = ContentTestDocuments;

        const PDFObject fonts = Documents::createDictionaryObject({ { "F1", Documents::addSimpleFontObject(builder) } });
        PDFObject form = Documents::addStreamObject(builder, { { "Type", PDFObject::createName("XObject") },
                                                               { "Subtype", PDFObject::createName("Form") },
                                                               { "BBox", Documents::createIntegerArrayObject({ 0, 0, 100, 100 }) },
                                                               { "Resources", Documents::createDictionaryObject({ { "Font", fonts } }) } }, formContent);

        PDFDictionaryBuilder resources;
        resources.addEntry(PDFInplaceOrMemoryString("Font"), fonts);
        resources.addEntry(PDFInplaceOrMemoryString("XObject"), Documents::createDictionaryObject({ { "Fm1", std::move(form) } }));
        return resources;
    }));
    const Recorder& recorder = *run.processor;

    QStringList expected;
    expected << QStringLiteral("0:B:Do")
             << QStringLiteral("1:B:ET") << QStringLiteral("1:F:ET") << QStringLiteral("1:A:ET")
             << QStringLiteral("1:B:ET") << QStringLiteral("1:F:ET") << QStringLiteral("1:A:ET")
             << pairs(1, { "BT", "Tf", "Td", "Tj", "ET" })
             << QStringLiteral("0:A:Do")
             << pairs(0, { "BT", "Tf", "Td", "Tj", "ET" });
    QCOMPARE(recorder.trace(), expected);

    QCOMPARE(run.errors.size(), 2);
    QCOMPARE(run.errors[0].message, QStringLiteral("Text object ended more than once."));
    QCOMPARE(run.errors[1].message, QStringLiteral("Text object ended more than once."));

    QCOMPARE(recorder.textBeginCount, 2);
    QCOMPARE(recorder.textEndCount, 2);
    QCOMPARE(recorder.textSequences.size(), size_t(2));
    for (const Recorder::Event& event : recorder.eventsOfKind(Recorder::EventKind::After))
    {
        // Open after BT, Tf, Td and Tj of both contents, closed after everything else
        const bool isInTextObject = event.text == "BT" || event.text == "Tf" || event.text == "Td" || event.text == "Tj";
        QCOMPARE(event.isTextObjectOpen, isInTextObject);
    }
}

void ContentProcessorTest::textEndWithoutBeginInGlyph()
{
    // The glyph procedure has two ET without a text object, then a whole (empty) text object,
    // then its square. A glyph procedure is a content stream of its own (PDF Reference 1.7,
    // 5.5.4): its two ET fail, its text object is begun and ended, and the text object of the
    // page, which shows the glyph, is open after the glyph and ended by its own ET.
    const QByteArray procedure = "60 0 d0 ET ET BT ET 0 0 50 50 re f";

    ContentProcessorRun run(ContentTestDocuments::createDocument("BT /T3 10 Tf 20 100 Td (A) Tj ET", [&procedure](PDFDocumentBuilder* builder)
    {
        PDFDictionaryBuilder resources;
        resources.addEntry(PDFInplaceOrMemoryString("Font"), ContentTestDocuments::createDictionaryObject({ { "T3", addGlyphFontObject(builder, procedure) } }));
        return resources;
    }));
    const Recorder& recorder = *run.processor;

    QStringList expected;
    expected << pairs(0, { "BT", "Tf", "Td" }) << QStringLiteral("0:B:Tj")
             << QStringLiteral("0:G+65") << pairs(1, { "d0" })
             << QStringLiteral("1:B:ET") << QStringLiteral("1:F:ET") << QStringLiteral("1:A:ET")
             << QStringLiteral("1:B:ET") << QStringLiteral("1:F:ET") << QStringLiteral("1:A:ET")
             << pairs(1, { "BT", "ET", "re", "f" }) << QStringLiteral("0:G-")
             << QStringLiteral("0:A:Tj") << pairs(0, { "ET" });
    QCOMPARE(recorder.trace(), expected);

    QCOMPARE(run.errors.size(), 2);
    QCOMPARE(run.errors[0].message, QStringLiteral("Text object ended more than once."));
    QCOMPARE(run.errors[1].message, QStringLiteral("Text object ended more than once."));

    // The text object of the page and the one of the procedure
    QCOMPARE(recorder.textBeginCount, 2);
    QCOMPARE(recorder.textEndCount, 2);

    const Recorder::Event* innerBegin = recorder.lastEvent(Recorder::EventKind::After, "BT");
    QVERIFY(innerBegin);
    QCOMPARE(innerBegin->level, PDFInteger(1));
    QVERIFY(innerBegin->isTextObjectOpen);

    const std::vector<Recorder::Event> glyphEnds = recorder.eventsOfKind(Recorder::EventKind::GlyphEnd);
    QCOMPARE(glyphEnds.size(), size_t(1));
    QVERIFY(glyphEnds[0].isTextObjectOpen);
    const Recorder::Event* show = recorder.lastEvent(Recorder::EventKind::After, "Tj");
    QVERIFY(show && show->isTextObjectOpen);
    COMPARE_NEAR(show->textMatrix.dx(), 26.0);
    QVERIFY(!recorder.events.back().isTextObjectOpen);
}

void ContentProcessorTest::pathWithoutCurrentPoint_data()
{
    // PDF Reference 1.7, 4.4.1 (the corresponding section of ISO 32000-2 is 8.5.2.1): "If the
    // current path is empty, the current point is undefined. Most operators that add a segment
    // to the current path start at the current point; if the current point is undefined, an
    // error is generated." Table 4.9: l, c, v, y and h append "from the current point"; m begins
    // a new subpath and re appends a complete subpath - they need no current point. "Once a
    // path has been painted, it is no longer defined".
    //
    // The error is reported (once per page), but the operator still does, what it did before
    // (owner 2026-11-26, NOTES.txt 8.5.2.1). paths: the painted paths as pathText - for the
    // rows without a current point this is the KEPT behavior, not the standard: the segment
    // of l, c and y starts at the origin (0, 0), h does nothing, and v fails as it did (it is
    // the only one, which throws: failed = 1, no segment).
    QTest::addColumn<QByteArray>("content");
    QTest::addColumn<QStringList>("paths");
    QTest::addColumn<int>("errors");
    QTest::addColumn<int>("failed");

    // Without a current point
    QTest::newRow("l") << QByteArray("10 20 l S") << QStringList{ QStringLiteral("M 0 0 L 10 20") } << 1 << 0;
    QTest::newRow("c") << QByteArray("1 2 3 4 5 6 c S") << QStringList{ QStringLiteral("M 0 0 C 1 2 3 4 5 6") } << 1 << 0;
    QTest::newRow("y") << QByteArray("1 2 5 6 y S") << QStringList{ QStringLiteral("M 0 0 C 1 2 5 6 5 6") } << 1 << 0;
    QTest::newRow("h") << QByteArray("h 10 10 m 20 20 l S") << QStringList{ QStringLiteral("M 10 10 L 20 20") } << 1 << 0;
    QTest::newRow("v") << QByteArray("3 4 5 6 v 10 10 m 20 20 l S") << QStringList{ QStringLiteral("M 10 10 L 20 20") } << 1 << 1;

    // With a current point: after m and after re (the current point after re is its first
    // corner, as after h)
    QTest::newRow("after m") << QByteArray("10 10 m 20 20 l 1 2 3 4 5 6 c 3 4 5 6 v 1 2 5 6 y h S")
                             << QStringList{ QStringLiteral("M 10 10 L 20 20 C 1 2 3 4 5 6 C 5 6 3 4 5 6 C 1 2 5 6 5 6 L 10 10") } << 0 << 0;
    QTest::newRow("after re") << QByteArray("0 0 10 10 re 20 20 l S")
                              << QStringList{ QStringLiteral("M 0 0 L 10 0 L 10 10 L 0 10 L 0 0 M 0 0 L 20 20") } << 0 << 0;
    QTest::newRow("h after m") << QByteArray("10 10 m h 20 20 l S") << QStringList{ QStringLiteral("M 10 10 L 20 20") } << 0 << 0;

    // The current point is gone after a painting operator and after n
    QTest::newRow("after a painting operator") << QByteArray("10 10 m 20 20 l S 30 30 l S")
                                               << QStringList{ QStringLiteral("M 10 10 L 20 20"), QStringLiteral("M 0 0 L 30 30") } << 1 << 0;
    QTest::newRow("after n") << QByteArray("10 10 m 20 20 l n 30 30 l S") << QStringList{ QStringLiteral("M 0 0 L 30 30") } << 1 << 0;

    // KEPT behavior, not the standard: a painting operator paints nothing of a lone m and
    // leaves its point in the path, so the segment after it starts there and is not reported
    QTest::newRow("after a painting operator of a lone m") << QByteArray("10 10 m S 20 20 l S") << QStringList{ QStringLiteral("M 10 10 L 20 20") } << 0 << 0;

    // The second segment of a path has the current point of the first one; a page with more
    // such paths reports once
    QTest::newRow("reported once") << QByteArray("10 20 l 30 40 l S 50 60 l S 1 2 3 4 5 6 c S")
                                   << QStringList{ QStringLiteral("M 0 0 L 10 20 L 30 40"), QStringLiteral("M 0 0 L 50 60"), QStringLiteral("M 0 0 C 1 2 3 4 5 6") } << 1 << 0;
}

void ContentProcessorTest::pathWithoutCurrentPoint()
{
    QFETCH(QByteArray, content);
    QFETCH(QStringList, paths);
    QFETCH(int, errors);
    QFETCH(int, failed);

    ContentProcessorRun run(ContentTestDocuments::createDocument(content, &ContentTestDocuments::createNoResources));
    const Recorder& recorder = *run.processor;

    // The error of the missing current point, reported once per page; only v fails
    QCOMPARE(run.errors.size(), errors);
    for (const PDFRenderError& error : run.errors)
    {
        QCOMPARE(error.type, RenderErrorType::Error);
        QCOMPARE(error.message, QStringLiteral("Current point of path is not set. Path is empty."));
    }
    const std::vector<Recorder::Event> failedEvents = recorder.eventsOfKind(Recorder::EventKind::Failed);
    QCOMPARE(failedEvents.size(), size_t(failed));
    for (const Recorder::Event& event : failedEvents)
    {
        QCOMPARE(event.text, QByteArray("v"));
    }

    // The painted paths
    QStringList painted;
    for (const Recorder::PaintedPath& path : recorder.paths)
    {
        QVERIFY(path.stroke);
        QVERIFY(!path.fill);
        painted << pathText(path.path);
    }
    QCOMPARE(painted, paths);
}

void ContentProcessorTest::uncoloredPatternIgnoresColorOperators()
{
    // PDF Reference 1.7, 4.6.2 (PaintType 2; the corresponding section of ISO 32000-2 is
    // 8.7.3.3): "The content stream must not invoke operators that specify colors or other
    // color-related parameters in the graphics state; otherwise, an error occurs (see Section
    // 4.5.7)"; 4.5.7 names CS, cs, SC, SCN, sc, scn, G, g, RG, rg, K, k, ri, sh, the entries TR,
    // TR2, BG, BG2, UCR, UCR2, HT of gs, and the painting of an image. The processor ignores
    // the twelve operators, which set a color or a color space, with one warning: the cell is
    // painted with the color of the pattern color space (here red), filled and stroked. After
    // the pattern the color operators of the page work.
    const QByteArray content = "/Cs cs 1 0 0 /PU scn 2 2 6 6 re f\n"
                               "0 0 1 rg 20 20 5 5 re f\n";

    ContentProcessorRun run(ContentTestDocuments::createDocument(content, &ContentProcessorTest::createUncoloredPatternResources));
    const Recorder& recorder = *run.processor;

    QCOMPARE(run.errors.size(), 1);
    QCOMPARE(run.errors[0].type, RenderErrorType::Warning);
    QCOMPARE(run.errors[0].message, QStringLiteral("Color operators are not allowed in uncolored tilling pattern."));

    // The painted square [2 2 8 8] is inside of one cell [0 0 10 10]; no operator fails
    QStringList expected;
    expected << pairs(0, { "cs", "scn", "re" }) << QStringLiteral("0:B:f")
             << pairs(1, { "cs", "CS", "sc", "SC", "scn", "SCN", "g", "G", "rg", "RG", "k", "K", "re", "B" })
             << QStringLiteral("0:A:f") << pairs(0, { "rg", "re", "f" });
    QCOMPARE(recorder.trace(), expected);

    QCOMPARE(recorder.paths.size(), size_t(2));
    const Recorder::PaintedPath& cell = recorder.paths[0];
    QCOMPARE(cell.level, PDFInteger(1));
    QCOMPARE(cell.bounds, QRectF(0, 0, 5, 5));
    QVERIFY(cell.fill && cell.stroke);
    QVERIFY(isSameColor(cell.fillColor, 255, 0, 0));
    QVERIFY(isSameColor(cell.strokeColor, 255, 0, 0));

    const Recorder::PaintedPath& next = recorder.paths[1];
    QCOMPARE(next.level, PDFInteger(0));
    QCOMPARE(next.bounds, QRectF(20, 20, 5, 5));
    QVERIFY(isSameColor(next.fillColor, 0, 0, 255));
}

void ContentProcessorTest::coloredPatternInsideUncoloredCell()
{
    // The cell of the uncolored pattern /PUN selects the colored pattern /PC by cs and scn. The
    // standard does not allow it: selecting a pattern is setting a color, which the cell of an
    // uncolored pattern must not do (PDF Reference 1.7, 4.5.7 and 4.6.2). The KEPT behavior of
    // the processor: both operators are ignored as every color operator there, so the cell of
    // /PC is never processed and the square of /PUN is filled with the color of the pattern
    // color space (red).
    const QByteArray content = "/Cs cs 1 0 0 /PUN scn 2 2 6 6 re f\n";

    ContentProcessorRun run(ContentTestDocuments::createDocument(content, &ContentProcessorTest::createUncoloredPatternResources));
    const Recorder& recorder = *run.processor;

    QCOMPARE(run.errors.size(), 1);
    QCOMPARE(run.errors[0].type, RenderErrorType::Warning);

    QStringList expected;
    expected << pairs(0, { "cs", "scn", "re" }) << QStringLiteral("0:B:f")
             << pairs(1, { "cs", "scn", "re", "f" })
             << QStringLiteral("0:A:f");
    QCOMPARE(recorder.trace(), expected);

    QCOMPARE(recorder.paths.size(), size_t(1));
    QCOMPARE(recorder.paths[0].level, PDFInteger(1));
    QCOMPARE(recorder.paths[0].bounds, QRectF(0, 0, 5, 5));
    QVERIFY(isSameColor(recorder.paths[0].fillColor, 255, 0, 0));
}

void ContentProcessorTest::uncoloredPatternInsideColoredCell()
{
    // The cell of the colored pattern /PCN fills a square by the uncolored pattern /PU in green
    // and sets the blue color after it: the color operators are ignored only inside of the
    // cell of /PU, they work again in the cell of /PCN, when the cell of /PU has ended.
    const QByteArray content = "/Pattern cs /PCN scn 2 2 6 6 re f\n";

    ContentProcessorRun run(ContentTestDocuments::createDocument(content, &ContentProcessorTest::createUncoloredPatternResources));
    const Recorder& recorder = *run.processor;

    QCOMPARE(run.errors.size(), 1);
    QCOMPARE(run.errors[0].type, RenderErrorType::Warning);

    QStringList expected;
    expected << pairs(0, { "cs", "scn", "re" }) << QStringLiteral("0:B:f")
             << pairs(1, { "cs", "scn", "re" }) << QStringLiteral("1:B:f")
             << pairs(2, { "cs", "CS", "sc", "SC", "scn", "SCN", "g", "G", "rg", "RG", "k", "K", "re", "B" })
             << QStringLiteral("1:A:f") << pairs(1, { "rg", "re", "f" })
             << QStringLiteral("0:A:f");
    QCOMPARE(recorder.trace(), expected);

    QCOMPARE(recorder.paths.size(), size_t(2));
    QCOMPARE(recorder.paths[0].level, PDFInteger(2));
    QCOMPARE(recorder.paths[0].bounds, QRectF(0, 0, 5, 5));
    QVERIFY(isSameColor(recorder.paths[0].fillColor, 0, 255, 0));
    QVERIFY(isSameColor(recorder.paths[0].strokeColor, 0, 255, 0));
    QCOMPARE(recorder.paths[1].level, PDFInteger(1));
    QCOMPARE(recorder.paths[1].bounds, QRectF(20, 20, 5, 5));
    QVERIFY(isSameColor(recorder.paths[1].fillColor, 0, 0, 255));
}

void ContentProcessorTest::uncoloredCellByDescendant_data()
{
    // A descendant processes the content of the cell of a tiling pattern by itself, through the
    // public processForm (Recorder::processCell). Inside of PDFUncoloredTilingPatternGuard the
    // content is the cell of an uncolored pattern: its color operators are ignored with a
    // warning, the fill color stays the one of before (the black of the initial state), and
    // isColorOperatorsIgnored says so at every operation. Without the guard (and with the
    // guard of a colored pattern) the content sets its color. The guard ends also, when an
    // operator of the cell fails.
    QTest::addColumn<bool>("uncolored");
    QTest::addColumn<QByteArray>("content");
    QTest::addColumn<int>("failed");
    QTest::addColumn<bool>("isColorSet");

    QTest::newRow("no guard of an uncolored cell") << false << QByteArray("1 0 0 rg 0 0 10 10 re f") << 0 << true;
    QTest::newRow("guard") << true << QByteArray("1 0 0 rg 0 0 10 10 re f") << 0 << false;
    QTest::newRow("guard, an operator fails") << true << QByteArray("/Missing gs 1 0 0 rg 0 0 10 10 re f") << 1 << false;
}

void ContentProcessorTest::uncoloredCellByDescendant()
{
    QFETCH(bool, uncolored);
    QFETCH(QByteArray, content);
    QFETCH(int, failed);
    QFETCH(bool, isColorSet);

    ContentProcessorRun run(ContentTestDocuments::createDocument(QByteArray(), &ContentTestDocuments::createNoResources), false, false);
    Recorder& recorder = *run.processor;
    recorder.initializeProcessor();
    QVERIFY(!recorder.colorOperatorsIgnored());

    recorder.processCell(content, uncolored);

    // The guard has ended
    QVERIFY(!recorder.colorOperatorsIgnored());

    // The descendant can ask at every operation of the cell
    QVERIFY(!recorder.events.empty());
    for (const Recorder::Event& event : recorder.events)
    {
        QCOMPARE(event.isColorIgnored, uncolored);
    }
    QCOMPARE(recorder.eventsOfKind(Recorder::EventKind::Failed).size(), size_t(failed));

    // The color of the fill: the red of the cell, or the black of before the cell
    QCOMPARE(recorder.paths.size(), size_t(1));
    QCOMPARE(recorder.paths[0].bounds, QRectF(0, 0, 10, 10));
    if (isColorSet)
    {
        QVERIFY(isSameColor(recorder.paths[0].fillColor, 255, 0, 0));
    }
    else
    {
        QVERIFY(isSameColor(recorder.paths[0].fillColor, 0, 0, 0));
    }

    // The ignored color operator is one warning; the failed operator is one error
    const QList<PDFRenderError> errors = recorder.renderErrors();
    QCOMPARE(errors.size(), failed + (uncolored ? 1 : 0));
    int warnings = 0;
    for (const PDFRenderError& error : errors)
    {
        if (error.type == RenderErrorType::Warning)
        {
            ++warnings;
            QCOMPARE(error.message, QStringLiteral("Color operators are not allowed in uncolored tilling pattern."));
        }
    }
    QCOMPARE(warnings, uncolored ? 1 : 0);
}

void ContentProcessorTest::uncoloredCellGuardsNest()
{
    // The guards nest: inside of the cell of an uncolored pattern every content is without a
    // color, also the cell of a colored pattern (its guard does nothing) and the cell of
    // another uncolored one; the end of an inner guard does not end the outer one
    ContentProcessorRun run(ContentTestDocuments::createDocument(QByteArray(), &ContentTestDocuments::createNoResources), false, false);
    Recorder& recorder = *run.processor;
    recorder.initializeProcessor();

    const std::vector<bool> expected = { false, true, true, true, true, true, false };
    QVERIFY(recorder.colorOperatorsIgnoredInNestedGuards() == expected);
    QVERIFY(!recorder.colorOperatorsIgnored());
}

void ContentProcessorTest::uncoloredCellGuardEndsWithException()
{
    // An exception, which the processor does not catch (thrown by a hook of the descendant),
    // leaves processForm through the guard: the color operators are not ignored afterwards
    ContentProcessorRun run(ContentTestDocuments::createDocument(QByteArray(), &ContentTestDocuments::createNoResources), false, false);
    Recorder& recorder = *run.processor;
    recorder.initializeProcessor();
    recorder.throwInPathPainting = true;

    bool isThrown = false;
    try
    {
        recorder.processCell("1 0 0 rg 0 0 10 10 re f", true);
    }
    catch (const Recorder::ProbeException&)
    {
        isThrown = true;
    }
    QVERIFY(isThrown);

    // The guard was in effect, when the path was painted, and it has ended
    const Recorder::Event* fill = recorder.lastEvent(Recorder::EventKind::Before, "f");
    QVERIFY(fill && fill->isColorIgnored);
    QCOMPARE(recorder.paths.size(), size_t(1));
    QVERIFY(!recorder.colorOperatorsIgnored());
}

void ContentProcessorTest::documentSimpleFonts()
{
    const PDFDocument document = ContentTestDocuments::createFontsDocument("BT /F1 10 Tf (A) Tj ET");
    QCOMPARE(document.getCatalog()->getPageCount(), size_t(1));
    QCOMPARE(document.getCatalog()->getPage(0)->getMediaBox(), QRectF(0, 0, ContentTestDocuments::PAGE_SIZE, ContentTestDocuments::PAGE_SIZE));

    // /F1: the hand-written widths of /Widths and /MissingWidth, the encoding with /Differences
    const PDFFontPointer font = getFont(document, "F1");
    QVERIFY(font);
    QCOMPARE(font->getFontType(), FontType::Type1);
    const PDFSimpleFont* simpleFont = dynamic_cast<const PDFSimpleFont*>(font.get());
    QVERIFY(simpleFont);
    QCOMPARE(simpleFont->getGlyphAdvance(32), PDFInteger(300));
    QCOMPARE(simpleFont->getGlyphAdvance(40), PDFInteger(500));
    QCOMPARE(simpleFont->getGlyphAdvance(65), PDFInteger(700));
    QCOMPARE(simpleFont->getGlyphAdvance(66), PDFInteger(650));
    QCOMPARE(simpleFont->getGlyphAdvance(67), PDFInteger(600));
    QCOMPARE(simpleFont->getGlyphAdvance(68), PDFInteger(550));
    for (int code = ContentTestDocuments::SIMPLE_FONT_FIRST_CHAR; code <= ContentTestDocuments::SIMPLE_FONT_LAST_CHAR; ++code)
    {
        QCOMPARE(simpleFont->getGlyphAdvance(size_t(code)), PDFInteger(ContentTestDocuments::simpleFontWidth(code)));
    }
    QCOMPARE(font->getFontDescriptor()->missingWidth, PDFReal(ContentTestDocuments::SIMPLE_FONT_MISSING_WIDTH));
    QCOMPARE(ContentTestDocuments::simpleFontWidth(31), 400);
    QCOMPARE(ContentTestDocuments::simpleFontWidth(69), 400);
    QVERIFY(!font->getFontDescriptor()->isEmbedded());
    QCOMPARE(simpleFont->getUnicode(32), QChar(u' '));
    QCOMPARE(simpleFont->getUnicode(65), QChar(u'A'));
    QCOMPARE(simpleFont->getUnicode(66), QChar(u'B'));
    QCOMPARE(simpleFont->getUnicode(67), QChar(char16_t(0x20AC)));
    QCOMPARE(simpleFont->getUnicode(68), QChar(u' '));

    // /F2: the code 32 is the glyph A and the code 65 is the glyph space; the same widths
    const PDFFontPointer swappedFont = getFont(document, "F2");
    QVERIFY(swappedFont);
    const PDFSimpleFont* swappedSimpleFont = dynamic_cast<const PDFSimpleFont*>(swappedFont.get());
    QVERIFY(swappedSimpleFont);
    QCOMPARE(swappedSimpleFont->getUnicode(32), QChar(u'A'));
    QCOMPARE(swappedSimpleFont->getUnicode(65), QChar(u' '));
    QCOMPARE(swappedSimpleFont->getUnicode(66), QChar(u'B'));
    QCOMPARE(swappedSimpleFont->getGlyphAdvance(32), PDFInteger(300));
    QCOMPARE(swappedSimpleFont->getGlyphAdvance(65), PDFInteger(700));

    // The page with the font is processed without an error; the glyph of the text is painted
    ContentProcessorRun run(ContentTestDocuments::createFontsDocument("BT /F1 10 Tf 20 100 Td (A) Tj /F2 10 Tf ( ) Tj ET"));
    QVERIFY2(run.errors.isEmpty(), qPrintable(run.errorText()));
    QCOMPARE(run.processor->textSequences.size(), size_t(2));
    QCOMPARE(run.processor->paths.size(), size_t(2));
    QVERIFY(run.processor->paths[0].text);
    QVERIFY(run.processor->paths[1].text);
}

void ContentProcessorTest::documentType0Font()
{
    const PDFDocument document = ContentTestDocuments::createFontsDocument("BT /F0 10 Tf <0041> Tj ET");

    const PDFFontPointer font = getFont(document, "F0");
    QVERIFY(font);
    QCOMPARE(font->getFontType(), FontType::Type0);
    const PDFType0Font* type0Font = dynamic_cast<const PDFType0Font*>(font.get());
    QVERIFY(type0Font);
    QVERIFY(!font->getFontDescriptor()->isEmbedded());

    // /W [ 32 [250] 65 [700 650] 100 102 480 ], /DW 900
    QCOMPARE(type0Font->getGlyphAdvance(32), 250.0);
    QCOMPARE(type0Font->getGlyphAdvance(65), 700.0);
    QCOMPARE(type0Font->getGlyphAdvance(66), 650.0);
    QCOMPARE(type0Font->getGlyphAdvance(67), 900.0);
    QCOMPARE(type0Font->getGlyphAdvance(99), 900.0);
    QCOMPARE(type0Font->getGlyphAdvance(100), 480.0);
    QCOMPARE(type0Font->getGlyphAdvance(101), 480.0);
    QCOMPARE(type0Font->getGlyphAdvance(102), 480.0);
    QCOMPARE(type0Font->getGlyphAdvance(103), 900.0);
    for (const int cid : { 0, 32, 33, 65, 66, 100, 102, 5000 })
    {
        QCOMPARE(type0Font->getGlyphAdvance(CID(cid)), PDFReal(ContentTestDocuments::type0FontWidth(cid)));
    }

    // /ToUnicode: codes of two bytes; one code is two code units
    QCOMPARE(font->getToUnicodeText(0x0020, 2), QStringLiteral(" "));
    QCOMPARE(font->getToUnicodeText(0x0041, 2), QStringLiteral("A"));
    QCOMPARE(font->getToUnicodeText(0x0042, 2), QStringLiteral("B"));
    QCOMPARE(font->getToUnicodeText(0x0064, 2), QStringLiteral("ff"));
    QCOMPARE(font->getToUnicodeText(0x0065, 2), QString());
    QCOMPARE(font->getToUnicodeText(0x0041, 1), QString());

    // The widths of the dictionary are the advances of the font, which is not embedded: by
    // 9.4.4 the text <00410042> of the size 10 at 20 ends at 20 + (0.7 + 0.65) * 10
    ContentProcessorRun run(ContentTestDocuments::createFontsDocument("BT /F0 10 Tf 20 100 Td <00410042> Tj ET"));
    PDFReal position = 0.0;
    QVERIFY(getTextPositionAfterShow(run, "Tj", &position));
    COMPARE_NEAR(position, 33.5);
    QCOMPARE(run.processor->paths.size(), size_t(2));

    // /FM: the same descendant font under a CMap with codes of one and of two bytes; the code
    // <8020> is the CID 32800 (the default width)
    const PDFFontPointer mixedFont = getFont(document, "FM");
    QVERIFY(mixedFont);
    QCOMPARE(mixedFont->getFontType(), FontType::Type0);
    const PDFType0Font* mixedType0Font = dynamic_cast<const PDFType0Font*>(mixedFont.get());
    QVERIFY(mixedType0Font);
    QCOMPARE(mixedType0Font->getGlyphAdvance(32), 250.0);
    QCOMPARE(mixedType0Font->getGlyphAdvance(65), 700.0);
    QCOMPARE(mixedType0Font->getGlyphAdvance(0x8020), 900.0);
    QCOMPARE(mixedFont->getToUnicodeText(0x20, 1), QStringLiteral(" "));
    QCOMPARE(mixedFont->getToUnicodeText(0x41, 1), QStringLiteral("A"));
    QCOMPARE(mixedFont->getToUnicodeText(0x8020, 2), QStringLiteral(" "));
    QCOMPARE(mixedFont->getToUnicodeText(0x8020, 1), QString());

    // 9.4.4: <20418020> of the size 10 at 20 ends at 20 + (0.25 + 0.7 + 0.9) * 10
    ContentProcessorRun mixedRun(ContentTestDocuments::createFontsDocument("BT /FM 10 Tf 20 100 Td <20418020> Tj ET"));
    QVERIFY(getTextPositionAfterShow(mixedRun, "Tj", &position));
    COMPARE_NEAR(position, 38.5);
}

void ContentProcessorTest::documentType3Fonts()
{
    // Plain
    {
        const PDFDocument document = ContentTestDocuments::createType3Document("BT /T3 10 Tf (A) Tj ET", ContentTestDocuments::Type3Variant::Plain);
        const PDFFontPointer font = getFont(document, "T3");
        QVERIFY(font);
        QCOMPARE(font->getFontType(), FontType::Type3);
        const PDFType3Font* type3Font = dynamic_cast<const PDFType3Font*>(font.get());
        QVERIFY(type3Font);

        QCOMPARE(type3Font->getFontMatrix(), QTransform(0.01, 0, 0, 0.01, 0, 0));
        QCOMPARE(type3Font->getFontMatrix().m11(), ContentTestDocuments::TYPE3_FONT_MATRIX_SCALE);
        QCOMPARE(type3Font->getWidth(32), 50.0);
        QCOMPARE(type3Font->getWidth(33), 0.0);
        QCOMPARE(type3Font->getWidth(65), 60.0);
        QCOMPARE(type3Font->getWidth(66), 80.0);
        QCOMPARE(type3Font->getWidth(67), 70.0);
        for (int code = 32; code <= 67; ++code)
        {
            QCOMPARE(type3Font->getWidth(code), double(ContentTestDocuments::type3FontWidth(code)));
        }

        // Three procedures; the code 67 has a width, but no procedure
        QCOMPARE(type3Font->getContentStreams().size(), size_t(3));
        QVERIFY(type3Font->getContentStream(32));
        QVERIFY(type3Font->getContentStream(65));
        QVERIFY(type3Font->getContentStream(66));
        QVERIFY(!type3Font->getContentStream(67));
        QCOMPARE(*type3Font->getContentStream(32), ContentTestDocuments::type3SpaceProcedure());
        QCOMPARE(*type3Font->getContentStream(65), ContentTestDocuments::type3SquareProcedure());
        QCOMPARE(*type3Font->getContentStream(66), ContentTestDocuments::type3BarProcedure());

        QCOMPARE(type3Font->getUnicode(32), QChar(u' '));
        QCOMPARE(type3Font->getUnicode(65), QChar(u'A'));
        QCOMPARE(type3Font->getUnicode(66), QChar(u'B'));
        QVERIFY(type3Font->getUnicode(67).isNull());

        // /Resources << /ProcSet [/PDF] >>: no font
        const PDFDictionary* resources = document.getDictionaryFromObject(type3Font->getResources());
        QVERIFY(resources);
        QVERIFY(resources->hasKey("ProcSet"));
        QVERIFY(!resources->hasKey("Font"));
    }

    // SelfReferencing: the resource /T3 of the font is the font itself
    {
        const PDFDocument document = ContentTestDocuments::createType3Document("BT /T3 10 Tf (A) Tj ET", ContentTestDocuments::Type3Variant::SelfReferencing);
        const PDFDictionary* pageResources = document.getDictionaryFromObject(document.getCatalog()->getPage(0)->getResources());
        QVERIFY(pageResources);
        const PDFDictionary* pageFonts = document.getDictionaryFromObject(pageResources->get("Font"));
        QVERIFY(pageFonts);
        const PDFObject fontObject = pageFonts->get("T3");
        QVERIFY(fontObject.isReference());

        const PDFFontPointer font = getFont(document, "T3");
        QVERIFY(font);
        const PDFType3Font* type3Font = dynamic_cast<const PDFType3Font*>(font.get());
        QVERIFY(type3Font);
        QVERIFY(type3Font->getContentStream(65));
        QCOMPARE(*type3Font->getContentStream(65), ContentTestDocuments::type3SelfReferencingProcedure());
        QCOMPARE(*type3Font->getContentStream(66), ContentTestDocuments::type3BarProcedure());

        const PDFDictionary* resources = document.getDictionaryFromObject(type3Font->getResources());
        QVERIFY(resources);
        const PDFDictionary* fonts = document.getDictionaryFromObject(resources->get("Font"));
        QVERIFY(fonts);
        QVERIFY(fonts->get("T3").isReference());
        QCOMPARE(fonts->get("T3").getReference(), fontObject.getReference());
    }

    // Chain: ten glyphs, each one shows the next one
    {
        const PDFDocument document = ContentTestDocuments::createType3Document("BT /T3 10 Tf (a) Tj ET", ContentTestDocuments::Type3Variant::Chain);
        const PDFFontPointer font = getFont(document, "T3");
        QVERIFY(font);
        const PDFType3Font* type3Font = dynamic_cast<const PDFType3Font*>(font.get());
        QVERIFY(type3Font);
        QCOMPARE(ContentTestDocuments::TYPE3_CHAIN_LENGTH, 10);
        QCOMPARE(ContentTestDocuments::TYPE3_CHAIN_FIRST_CHAR, int('a'));
        QCOMPARE(type3Font->getContentStreams().size(), size_t(10));
        QCOMPARE(ContentTestDocuments::type3ChainProcedure(3), QByteArray("10 0 d0 3 0 1 1 re f BT /T3 1 Tf (e) Tj ET"));
        QCOMPARE(ContentTestDocuments::type3ChainProcedure(9), QByteArray("10 0 d0 9 0 1 1 re f"));
        for (int i = 0; i < ContentTestDocuments::TYPE3_CHAIN_LENGTH; ++i)
        {
            const int code = ContentTestDocuments::TYPE3_CHAIN_FIRST_CHAR + i;
            QVERIFY(type3Font->getContentStream(code));
            QCOMPARE(*type3Font->getContentStream(code), ContentTestDocuments::type3ChainProcedure(i));
            QCOMPARE(type3Font->getWidth(code), 10.0);
        }
    }
}

void ContentProcessorTest::documentTaggedPage()
{
    ContentProcessorRun run(ContentTestDocuments::createTaggedDocument());
    const Recorder& recorder = *run.processor;
    const PDFDocument& document = run.document;
    QVERIFY2(run.errors.isEmpty(), qPrintable(run.errorText()));

    const PDFCatalog* catalog = document.getCatalog();
    QCOMPARE(catalog->getPageCount(), size_t(1));
    QVERIFY(catalog->isLogicalStructureMarked());
    const PDFPage* page = catalog->getPage(0);
    QCOMPARE(page->getStructureParentKey(), PDFInteger(0));

    // The marked content sequences of the content: three with an identifier, one artifact
    QCOMPARE(recorder.markedContents.size(), size_t(4));
    QCOMPARE(recorder.markedContentEndCount, 4);
    const std::vector<std::pair<QByteArray, PDFInteger>> expectedSequences = { { "P", 0 }, { "Span", 1 }, { "Artifact", -1 }, { "Figure", 3 } };
    std::vector<PDFInteger> identifiers;
    for (size_t i = 0; i < expectedSequences.size(); ++i)
    {
        QCOMPARE(recorder.markedContents[i].tag, expectedSequences[i].first);
        QCOMPARE(recorder.markedContents[i].identifier, expectedSequences[i].second);
        QCOMPARE(recorder.markedContents[i].structuralParentKey, PDFInteger(0));
        if (recorder.markedContents[i].identifier >= 0)
        {
            identifiers.push_back(recorder.markedContents[i].identifier);
        }
    }
    QVERIFY(identifiers == ContentTestDocuments::taggedPageMarkedContentIdentifiers());
    QCOMPARE(recorder.textSequences.size(), size_t(2));

    // The structure tree: Document with the four elements; the kids of P, Span and Figure are
    // the marked content of the identifiers 0, 1 and 3 (an integer, a marked content reference,
    // an integer in an array), the kid of Link is the annotation
    const PDFStructureTree tree = PDFStructureTree::parse(&document.getStorage(), catalog->getStructureTreeRoot());
    QVERIFY(tree.isValid());
    QCOMPARE(tree.getChildCount(), size_t(1));
    const PDFStructureElement* documentElement = tree.getChild(0)->asStructureElement();
    QVERIFY(documentElement);
    QCOMPARE(documentElement->getTypeName(), QByteArray("Document"));
    QCOMPARE(documentElement->getChildCount(), size_t(4));

    const std::vector<QByteArray> expectedElements = { "P", "Span", "Figure", "Link" };
    std::vector<const PDFDictionary*> elementDictionaries;
    for (size_t i = 0; i < expectedElements.size(); ++i)
    {
        const PDFStructureElement* element = documentElement->getChild(i)->asStructureElement();
        QVERIFY(element);
        QCOMPARE(element->getTypeName(), expectedElements[i]);
        QCOMPARE(element->getPageReference(), page->getPageReference());
        QVERIFY(element->getSelfReference().isValid());

        const PDFDictionary* elementDictionary = document.getDictionaryFromObject(document.getObjectByReference(element->getSelfReference()));
        QVERIFY(elementDictionary);
        elementDictionaries.push_back(elementDictionary);
    }
    QCOMPARE(documentElement->getChild(2)->asStructureElement()->getText(PDFStructureElement::AlternativeDescription), QStringLiteral("Square"));

    // P: /K is the integer identifier 0; Figure: /K is the array [3]. They are read from the
    // objects (the structure tree of the project keeps only the kids, which are dictionaries).
    const PDFObject& paragraphKids = document.getObject(elementDictionaries[0]->get("K"));
    QVERIFY(paragraphKids.isInt());
    QCOMPARE(paragraphKids.getInteger(), PDFInteger(0));
    const PDFObject& figureKids = document.getObject(elementDictionaries[2]->get("K"));
    QVERIFY(figureKids.isArray());
    QCOMPARE(figureKids.getArray()->getCount(), size_t(1));
    QVERIFY(figureKids.getArray()->getItem(0).isInt());
    QCOMPARE(figureKids.getArray()->getItem(0).getInteger(), PDFInteger(3));

    // Span: /K is a marked content reference of the identifier 1 on the page
    const PDFStructureElement* spanElement = documentElement->getChild(1)->asStructureElement();
    QCOMPARE(spanElement->getChildCount(), size_t(1));
    const PDFStructureMarkedContentReference* markedContentReference = spanElement->getChild(0)->asStructureMarkedContentReference();
    QVERIFY(markedContentReference);
    QCOMPARE(markedContentReference->getMarkedContentIdentifier(), PDFInteger(1));
    QCOMPARE(markedContentReference->getPageReference(), page->getPageReference());

    const PDFDictionary* pageDictionary = document.getDictionaryFromObject(document.getObjectByReference(page->getPageReference()));
    QVERIFY(pageDictionary);
    const PDFObject& annotations = document.getObject(pageDictionary->get("Annots"));
    QVERIFY(annotations.isArray());
    QCOMPARE(annotations.getArray()->getCount(), size_t(1));
    const PDFObject annotation = annotations.getArray()->getItem(0);
    QVERIFY(annotation.isReference());

    const PDFStructureElement* linkElement = documentElement->getChild(3)->asStructureElement();
    QVERIFY(linkElement);
    QCOMPARE(linkElement->getTypeName(), QByteArray("Link"));
    QCOMPARE(linkElement->getChildCount(), size_t(1));
    const PDFStructureObjectReference* objectReference = linkElement->getChild(0)->asStructureObjectReference();
    QVERIFY(objectReference);
    QCOMPARE(objectReference->getObjectReference(), annotation.getReference());

    // The parent tree as written: /Nums [ 0 [P Span null Figure] 1 Link ]
    const PDFDictionary* root = document.getDictionaryFromObject(catalog->getStructureTreeRoot());
    QVERIFY(root);
    const PDFDictionary* parentTree = document.getDictionaryFromObject(root->get("ParentTree"));
    QVERIFY(parentTree);
    const PDFObject& numbers = document.getObject(parentTree->get("Nums"));
    QVERIFY(numbers.isArray());
    QCOMPARE(numbers.getArray()->getCount(), size_t(4));
    QCOMPARE(numbers.getArray()->getItem(0).getInteger(), PDFInteger(0));
    const PDFObject& pageArray = document.getObject(numbers.getArray()->getItem(1));
    QVERIFY(pageArray.isArray());
    QCOMPARE(pageArray.getArray()->getCount(), size_t(4));
    QCOMPARE(pageArray.getArray()->getItem(0).getReference(), documentElement->getChild(0)->getSelfReference());
    QCOMPARE(pageArray.getArray()->getItem(1).getReference(), documentElement->getChild(1)->getSelfReference());
    QVERIFY(pageArray.getArray()->getItem(2).isNull());
    QCOMPARE(pageArray.getArray()->getItem(3).getReference(), documentElement->getChild(2)->getSelfReference());
    QCOMPARE(numbers.getArray()->getItem(2).getInteger(), PDFInteger(1));
    QCOMPARE(numbers.getArray()->getItem(3).getReference(), linkElement->getSelfReference());
}

void ContentProcessorTest::documentSoftMask()
{
    ContentProcessorRun run(ContentTestDocuments::createSoftMaskDocument());
    const Recorder& recorder = *run.processor;
    QVERIFY2(run.errors.isEmpty(), qPrintable(run.errorText()));

    QStringList expected;
    expected << pairs(0, { "q", "gs", "rg", "re", "f", "Q", "q", "gs", "rg", "RG", "re", "B", "Q" });
    QCOMPARE(recorder.trace(), expected);

    // /GS1: a luminosity soft mask with the transparency group of the document, the alphas stay 1
    const Recorder::Event& maskSet = recorder.events[3];
    QCOMPARE(maskSet.text, QByteArray("gs"));
    QCOMPARE(maskSet.kind, Recorder::EventKind::After);
    QVERIFY(maskSet.hasSoftMask);
    QVERIFY(maskSet.isSoftMaskLuminosity);
    QCOMPARE(maskSet.softMaskContent, ContentTestDocuments::softMaskGroupContent());
    QCOMPARE(maskSet.alphaFilling, 1.0);
    QCOMPARE(maskSet.alphaStroking, 1.0);

    // Q restores the state without a mask
    const Recorder::Event& maskRestored = recorder.events[11];
    QCOMPARE(maskRestored.text, QByteArray("Q"));
    QVERIFY(!maskRestored.hasSoftMask);

    // /GS2: only the constant alphas
    const Recorder::Event& alphaSet = recorder.events[15];
    QCOMPARE(alphaSet.text, QByteArray("gs"));
    QCOMPARE(alphaSet.kind, Recorder::EventKind::After);
    QVERIFY(!alphaSet.hasSoftMask);
    QCOMPARE(alphaSet.alphaFilling, 0.5);
    QCOMPARE(alphaSet.alphaStroking, 0.25);

    QCOMPARE(recorder.paths.size(), size_t(2));
    QCOMPARE(recorder.paths[0].bounds, QRectF(0, 0, 200, 100));
    QVERIFY(recorder.paths[0].fill);
    QVERIFY(!recorder.paths[0].stroke);
    QVERIFY(isSameColor(recorder.paths[0].fillColor, 255, 0, 0));
    QCOMPARE(recorder.paths[1].bounds, QRectF(0, 100, 200, 100));
    QVERIFY(recorder.paths[1].fill);
    QVERIFY(recorder.paths[1].stroke);
    QVERIFY(isSameColor(recorder.paths[1].fillColor, 0, 0, 255));
    QVERIFY(isSameColor(recorder.paths[1].strokeColor, 0, 0, 255));
}

void ContentProcessorTest::oldEditorKeepsType3Text_data()
{
    QTest::addColumn<int>("variant");
    QTest::addColumn<QList<QRectF>>("expectedPathBoxes");
    QTest::addColumn<int>("expectedTextSequences");

    // The old content editor (PDFPageContentEditorProcessor) makes one text element of a text
    // object of the page and one path element of each path painted by a glyph procedure of a
    // Type 3 font, before the text element. The page is "BT /T3 10 Tf 20 100 Td (AB) Tj ET": the
    // glyph space is 0.1 of the page, so the square [0 0 50 50] of A is 5 x 5 at (20, 100) and
    // the bar [0 0 20 100] of B is 2 x 10 at (26, 100).
    QTest::newRow("glyphs without a text object") << int(ContentTestDocuments::Type3Variant::Plain)
                                                  << QList<QRectF>{ QRectF(20, 100, 5, 5), QRectF(26, 100, 2, 10) } << 1;

    // The procedure of A has a text object, which shows a small B at (30, 40) of its glyph
    // space: 0.02 x 0.1 at (23, 104) of the page. The text object of the procedure neither begins
    // a text element nor ends the one of the page; its text sequence is an item of that element
    // (as before the procedures became content streams of their own for the processor).
    QTest::newRow("glyph with a text object") << int(ContentTestDocuments::Type3Variant::InnerText)
                                              << QList<QRectF>{ QRectF(20, 100, 5, 5), QRectF(23, 104, 0.02, 0.1), QRectF(26, 100, 2, 10) } << 2;
}

void ContentProcessorTest::oldEditorKeepsType3Text()
{
    QFETCH(int, variant);
    QFETCH(QList<QRectF>, expectedPathBoxes);
    QFETCH(int, expectedTextSequences);

    PDFDocument document = ContentTestDocuments::createType3Document("BT /T3 10 Tf 20 100 Td (AB) Tj ET", ContentTestDocuments::Type3Variant(variant));

    PDFCMSGeneric cms;
    PDFFontCache fontCache(32, 32);
    PDFICCProfileCache iccProfileCache;
    PDFOptionalContentActivity activity(&document, OCUsage::View, nullptr);
    fontCache.setDocument(PDFModifiedDocument(&document, &activity));
    iccProfileCache.setDocument(PDFModifiedDocument(&document, &activity));

    PDFPageContentEditorProcessor processor(document.getCatalog()->getPage(0), &document, &fontCache, &iccProfileCache, &cms, &activity, QTransform(), PDFMeshQualitySettings());
    const QList<PDFRenderError> errors = processor.processContents();
    const PDFEditedPageContent content = processor.takeEditedPageContent();
    QVERIFY(errors.isEmpty());

    // The path elements of the glyphs, then the one text element of the page
    QCOMPARE(content.getElementCount(), size_t(expectedPathBoxes.size()) + 1);
    for (qsizetype i = 0; i < expectedPathBoxes.size(); ++i)
    {
        const PDFEditedPageContentElement* element = content.getElement(size_t(i));
        QVERIFY(element->asPath());
        const QRectF box = element->getBoundingBox();
        COMPARE_NEAR(box.left(), expectedPathBoxes[i].left());
        COMPARE_NEAR(box.top(), expectedPathBoxes[i].top());
        COMPARE_NEAR(box.width(), expectedPathBoxes[i].width());
        COMPARE_NEAR(box.height(), expectedPathBoxes[i].height());
    }

    const PDFEditedPageContentElementText* textElement = content.getElement(content.getElementCount() - 1)->asText();
    QVERIFY(textElement);

    // Its first text sequence is the string of the page, the codes 65 and 66
    std::vector<const TextSequence*> textSequences;
    for (const PDFEditedPageContentElementText::Item& item : textElement->getItems())
    {
        if (item.isText)
        {
            textSequences.push_back(&item.textSequence);
        }
    }
    QCOMPARE(textSequences.size(), size_t(expectedTextSequences));
    QCOMPARE(textSequences[0]->items.size(), size_t(2));
    QCOMPARE(textSequences[0]->items[0].code, 65u);
    QCOMPARE(textSequences[0]->items[1].code, 66u);
}

QTEST_MAIN(ContentProcessorTest)

#include "tst_contentprocessortest.moc"
