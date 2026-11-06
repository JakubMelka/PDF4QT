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

#include "pdfdocumentsanitizer.h"
#include "pdfvisitor.h"
#include "pdfexecutionpolicy.h"
#include "pdfoptimizer.h"
#include "pdfdocumentbuilder.h"
#include "pdfparser.h"
#include "pdfstreamfilters.h"
#include "pdffont.h"
#include "pdfannotation.h"
#include "pdfform.h"
#include "pdfobjectutils.h"
#include "pdfocrtextlayerwriter.h"
#include "pdfprogress.h"

#include <QStringDecoder>
#include <QStringEncoder>
#include <QXmlStreamReader>

#include <algorithm>
#include <optional>
#include <set>

#include "pdfdbgheap.h"

namespace pdf
{

struct SanitizedObjectResult
{
    PDFObject object;
    bool changed = false;
};

/// The walks of the sanitization of the invisible text: each object is rewritten once (a
/// stream or a form shared by more pages, a form, which draws itself), the forms are followed
/// to the default depth of PDFObjectWalk (form levels, more than the scan looks at)
struct InvisibleTextSanitizationContext
{
    PDFObjectWalk contentWalk;  ///< The content streams of the pages
    PDFObjectWalk formWalk;     ///< The resources and their forms
    PDFInteger modifiedStreamCount = 0;
};

/// Helper implementing low-level sanitization of invisible OCR text.
///
/// The goal is intentionally narrow: remove only text-showing operators executed
/// with text rendering mode Tr = 3 (invisible text), while keeping all other
/// operators and stream structure unchanged whenever possible. This is safer than
/// rebuilding the page from a higher-level model because the original content may
/// contain inline images or uncommon operator sequences that should survive the
/// sanitization unchanged.
class PDFInvisibleTextSanitizerHelper
{
public:
    using SanitizedObjectResult = ::pdf::SanitizedObjectResult;
    using InvisibleTextSanitizationContext = ::pdf::InvisibleTextSanitizationContext;

    static bool isTextShowingOperator(const QByteArray& command);
    static bool isInlineImageOperator(const QByteArray& command);
    static PDFInteger readDirectIntegerFromDictionary(const PDFDictionary* dictionary, const char* key, PDFInteger defaultValue);
    static QByteArray readDirectNameFromDictionary(const PDFDictionary* dictionary, const char* key);
    static QByteArray sanitizeInvisibleTextInContent(const QByteArray& content, PDFDocumentSanitizer::InvisibleTextState* state, bool* changed);
    static PDFObject createCompressedContentStream(const PDFStream* originalStream, QByteArray decodedContent, PDFDictionaryBuilder dictionary);
    static bool isFormXObject(const PDFObject& object, PDFDocumentBuilder& builder);
    static SanitizedObjectResult sanitizeResourcesObject(PDFDocumentBuilder& builder, const PDFObject& resourcesObject, InvisibleTextSanitizationContext& context, int depth);
    static SanitizedObjectResult sanitizeFormXObject(PDFDocumentBuilder& builder, const PDFObject& object, InvisibleTextSanitizationContext& context, int depth);
    static SanitizedObjectResult sanitizeContentsObject(PDFDocumentBuilder& builder, const PDFObject& contentsObject, InvisibleTextSanitizationContext& context, PDFDocumentSanitizer::InvisibleTextState* state, int depth);
};

bool PDFInvisibleTextSanitizerHelper::isTextShowingOperator(const QByteArray& command)
{
    return command == "Tj" || command == "TJ" || command == "'" || command == "\"";
}

bool PDFInvisibleTextSanitizerHelper::isInlineImageOperator(const QByteArray& command)
{
    return command == "BI";
}

PDFInteger PDFInvisibleTextSanitizerHelper::readDirectIntegerFromDictionary(const PDFDictionary* dictionary, const char* key, PDFInteger defaultValue)
{
    if (!dictionary || !dictionary->hasKey(key))
    {
        return defaultValue;
    }

    const PDFObject& object = dictionary->get(key);
    if (object.isInt())
    {
        return object.getInteger();
    }

    if (object.isReal())
    {
        return PDFInteger(object.getReal());
    }

    return defaultValue;
}

QByteArray PDFInvisibleTextSanitizerHelper::readDirectNameFromDictionary(const PDFDictionary* dictionary, const char* key)
{
    if (!dictionary || !dictionary->hasKey(key))
    {
        return QByteArray();
    }

    const PDFObject& object = dictionary->get(key);
    return object.isName() ? object.getString() : QByteArray();
}

QByteArray PDFInvisibleTextSanitizerHelper::sanitizeInvisibleTextInContent(const QByteArray& content, PDFDocumentSanitizer::InvisibleTextState* state, bool* changed)
{
    Q_ASSERT(changed);
    Q_ASSERT(state);
    *changed = false;

    struct GraphicState
    {
        TextRenderingMode textRenderingMode = TextRenderingMode::Fill;
    };

    // Work directly on lexical tokens so the sanitizer can remove only the
    // affected text-showing instructions and preserve the rest of the stream.
    PDFLexicalAnalyzer parser(content.constBegin(), content.constEnd());
    QByteArray output;
    std::vector<PDFLexicalAnalyzer::Token> operands;
    std::vector<std::pair<PDFInteger, PDFInteger>> operandRanges;
    std::vector<GraphicState> graphicStateStack;
    for (int mode : state->stack)
    {
        graphicStateStack.push_back({ static_cast<TextRenderingMode>(mode) });
    }

    GraphicState currentGraphicState;
    currentGraphicState.textRenderingMode = static_cast<TextRenderingMode>(state->renderingMode);
    bool inTextObject = state->isInTextObject;
    PDFInteger copyPosition = 0;
    PDFInteger operandStartPosition = -1;

    while (!parser.isAtEnd())
    {
        const PDFInteger tokenStartPosition = parser.pos();
        PDFLexicalAnalyzer::Token token = parser.fetch();
        const PDFInteger tokenEndPosition = parser.pos();

        if (token.type == PDFLexicalAnalyzer::TokenType::EndOfFile)
        {
            break;
        }

        if (token.type != PDFLexicalAnalyzer::TokenType::Command)
        {
            if (operandStartPosition == -1)
            {
                operandStartPosition = tokenStartPosition;
            }

            operands.push_back(std::move(token));
            operandRanges.emplace_back(tokenStartPosition, tokenEndPosition);
            continue;
        }

        const QByteArray command = token.data.toByteArray();

        if (isInlineImageOperator(command))
        {
            // Inline images are copied through unchanged. They are parsed only to
            // determine where the image data ends so the tokenizer stays aligned.
            const PDFInteger operatorBIPosition = parser.pos();
            PDFInteger operatorIDPosition = parser.findSubstring("ID", operatorBIPosition);
            PDFInteger operatorEIPosition = parser.findSubstring("EI", operatorIDPosition);
            const PDFInteger startDataPosition = operatorIDPosition + 3;

            if (operatorIDPosition != -1 && operatorEIPosition != -1)
            {
                PDFLexicalAnalyzer inlineImageLexicalAnalyzer(content.constBegin() + operatorBIPosition, content.constBegin() + operatorIDPosition);
                PDFParser inlineImageParser([&inlineImageLexicalAnalyzer] { return inlineImageLexicalAnalyzer.fetch(); });

                constexpr std::pair<const char*, const char*> replacements[] =
                {
                    { "BPC", "BitsPerComponent" },
                    { "CS", "ColorSpace" },
                    { "D", "Decode" },
                    { "DP", "DecodeParms" },
                    { "F", "Filter" },
                    { "H", "Height" },
                    { "IM", "ImageMask" },
                    { "I", "Interpolate" },
                    { "W", "Width" },
                    { "L", "Length" },
                    { "G", "DeviceGray" },
                    { "RGB", "DeviceRGB" },
                    { "CMYK", "DeviceCMYK" }
                };

                PDFDictionaryBuilder inlineImageDictionary;
                PDFDictionaryBuilder* dictionary = &inlineImageDictionary;

                while (inlineImageParser.lookahead().type != PDFLexicalAnalyzer::TokenType::EndOfFile)
                {
                    PDFObject nameObject = inlineImageParser.getObject();
                    PDFObject valueObject = inlineImageParser.getObject();

                    if (!nameObject.isName())
                    {
                        break;
                    }

                    QByteArray name = nameObject.getString();
                    for (auto [string, replacement] : replacements)
                    {
                        if (name == string)
                        {
                            name = replacement;
                            break;
                        }
                    }

                    dictionary->addEntry(PDFInplaceOrMemoryString(qMove(name)), qMove(valueObject));
                }

                PDFInteger dataLength = 0;

                if (dictionary->hasKey("Length"))
                {
                    dataLength = readDirectIntegerFromDictionary(dictionary->getDictionary(), "Length", 0);
                }
                else if (dictionary->hasKey("Filter"))
                {
                    dataLength = -1;
                    const QByteArray filterName = readDirectNameFromDictionary(dictionary->getDictionary(), "Filter");
                    if (!filterName.isEmpty())
                    {
                        dataLength = PDFStreamFilterStorage::getStreamDataLength(content, filterName, startDataPosition);
                    }

                    if (dataLength == -1)
                    {
                        dataLength = operatorEIPosition - startDataPosition;
                    }
                }
                else
                {
                    const PDFInteger width = readDirectIntegerFromDictionary(dictionary->getDictionary(), "Width", 0);
                    const PDFInteger height = readDirectIntegerFromDictionary(dictionary->getDictionary(), "Height", 0);
                    const PDFInteger bpc = readDirectIntegerFromDictionary(dictionary->getDictionary(), "BitsPerComponent", 8);

                    if (width > 0 && height > 0 && bpc > 0)
                    {
                        const PDFInteger stride = (width * bpc + 7) / 8;
                        dataLength = stride * height;
                    }
                }

                if (dataLength >= 0)
                {
                    operatorEIPosition = parser.findSubstring("EI", startDataPosition + dataLength);
                    if (operatorEIPosition != -1)
                    {
                        parser.seek(operatorEIPosition + 2);
                    }
                }
            }

            operands.clear();
            operandRanges.clear();
            operandStartPosition = -1;
            continue;
        }

        // Only remove text-showing operators from text objects currently using
        // invisible text rendering mode. Other rendering modes may affect page
        // appearance or clipping and are intentionally left untouched.
        const bool isInvisibleTextToRemove = inTextObject &&
                                             currentGraphicState.textRenderingMode == TextRenderingMode::Invisible &&
                                             isTextShowingOperator(command) &&
                                             operandStartPosition != -1;

        if (isInvisibleTextToRemove)
        {
            output.append(content.constData() + copyPosition, operandStartPosition - copyPosition);
            copyPosition = tokenEndPosition;
            *changed = true;

            // Only the text goes: ' moves to the next line before it shows the text, " sets the
            // word and the character spacing too (ISO 32000-2, Table 106). The text, which follows,
            // keeps its line and its spacing. The written operators start with a space: the removed
            // operand may have been glued to the token in front of it ("Tr(text)'").
            if (command == "\"" && operandRanges.size() >= 3)
            {
                const auto& [wordSpacingStart, wordSpacingEnd] = operandRanges[operandRanges.size() - 3];
                const auto& [characterSpacingStart, characterSpacingEnd] = operandRanges[operandRanges.size() - 2];
                output.append(' ');
                output.append(content.constData() + wordSpacingStart, wordSpacingEnd - wordSpacingStart);
                output.append(" Tw ");
                output.append(content.constData() + characterSpacingStart, characterSpacingEnd - characterSpacingStart);
                output.append(" Tc T*");
            }
            else if (command == "'" || command == "\"")
            {
                output.append(" T*");
            }
        }

        if (command == "q")
        {
            graphicStateStack.push_back(currentGraphicState);
        }
        else if (command == "Q")
        {
            if (!graphicStateStack.empty())
            {
                currentGraphicState = graphicStateStack.back();
                graphicStateStack.pop_back();
            }
        }
        else if (command == "BT")
        {
            inTextObject = true;
        }
        else if (command == "ET")
        {
            inTextObject = false;
        }
        else if (command == "Tr" && !operands.empty())
        {
            const PDFLexicalAnalyzer::Token& modeToken = operands.back();
            int mode = -1;
            if (modeToken.type == PDFLexicalAnalyzer::TokenType::Integer)
            {
                mode = int(modeToken.data.toLongLong());
            }
            else if (modeToken.type == PDFLexicalAnalyzer::TokenType::Real)
            {
                mode = int(modeToken.data.toDouble());
            }

            if (mode >= 0 && mode <= 7)
            {
                currentGraphicState.textRenderingMode = static_cast<TextRenderingMode>(mode);
            }
        }

        operands.clear();
        operandRanges.clear();
        operandStartPosition = -1;
    }

    // The next stream of the page goes on with this state
    state->renderingMode = static_cast<int>(currentGraphicState.textRenderingMode);
    state->isInTextObject = inTextObject;
    state->stack.clear();
    for (const GraphicState& graphicState : graphicStateStack)
    {
        state->stack.push_back(static_cast<int>(graphicState.textRenderingMode));
    }

    if (*changed)
    {
        output.append(content.constData() + copyPosition, content.size() - copyPosition);
        return output;
    }

    return content;
}

PDFObject PDFInvisibleTextSanitizerHelper::createCompressedContentStream(const PDFStream* originalStream,
                                                                         QByteArray decodedContent,
                                                                         PDFDictionaryBuilder dictionary)
{
    // Re-encode the rewritten content into a regular compressed stream so the
    // updated object can be written back through the document builder.
    PDFArrayBuilder filters;
    filters.appendItem(PDFObject::createName("FlateDecode"));

    dictionary.removeEntry("DecodeParms");

    QByteArray compressedData = PDFFlateDecodeFilter::compress(decodedContent);
    dictionary.setEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(compressedData.size()));
    dictionary.setEntry(PDFInplaceOrMemoryString("Filter"), PDFObject::createArray(qMove(filters)));

    Q_UNUSED(originalStream);
    return PDFObject::createStream(PDFStream(qMove(dictionary), qMove(compressedData)));
}

bool PDFInvisibleTextSanitizerHelper::isFormXObject(const PDFObject& object, PDFDocumentBuilder& builder)
{
    const PDFObject& dereferencedObject = builder.getObject(object);
    if (!dereferencedObject.isStream())
    {
        return false;
    }

    const PDFDictionary* dictionary = dereferencedObject.getStream()->getDictionary();
    return dictionary &&
           dictionary->hasKey("Subtype") &&
           builder.getObject(dictionary->get("Subtype")).isName() &&
           builder.getObject(dictionary->get("Subtype")).getString() == "Form";
}

SanitizedObjectResult PDFInvisibleTextSanitizerHelper::sanitizeFormXObject(PDFDocumentBuilder& builder,
                                                                           const PDFObject& object,
                                                                           InvisibleTextSanitizationContext& context,
                                                                           int depth)
{
    if (object.isReference())
    {
        const PDFObjectReference reference = object.getReference();
        if (!context.formWalk.enter(object, depth))
        {
            return { object, false };
        }

        const PDFObject referencedObject = builder.getObjectByReference(reference);
        SanitizedObjectResult result = sanitizeFormXObject(builder, referencedObject, context, depth);
        if (result.changed)
        {
            builder.setObject(reference, qMove(result.object));
        }
        return { object, false };
    }

    if (!isFormXObject(object, builder))
    {
        return { object, false };
    }

    const PDFStream* stream = object.getStream();
    PDFDictionaryBuilder dictionary(*stream->getDictionary());
    QByteArray decodedContent = builder.getDecodedStream(stream);
    bool contentChanged = false;
    PDFDocumentSanitizer::InvisibleTextState formState;
    QByteArray sanitizedContent = sanitizeInvisibleTextInContent(decodedContent, &formState, &contentChanged);

    bool resourcesChanged = false;
    if (dictionary.hasKey("Resources"))
    {
        // OCR text may also be nested inside Form XObjects referenced from this
        // form, so sanitize the local resource tree recursively.
        SanitizedObjectResult resourcesResult = sanitizeResourcesObject(builder, dictionary.get("Resources"), context, depth + 1);
        if (resourcesResult.changed)
        {
            dictionary.setEntry(PDFInplaceOrMemoryString("Resources"), qMove(resourcesResult.object));
            resourcesChanged = true;
        }
    }

    if (!contentChanged && !resourcesChanged)
    {
        return { object, false };
    }

    if (contentChanged)
    {
        ++context.modifiedStreamCount;
    }

    return { createCompressedContentStream(stream, qMove(sanitizedContent), qMove(dictionary)), true };
}

SanitizedObjectResult PDFInvisibleTextSanitizerHelper::sanitizeResourcesObject(PDFDocumentBuilder& builder,
                                                                               const PDFObject& resourcesObject,
                                                                               InvisibleTextSanitizationContext& context,
                                                                               int depth)
{
    if (resourcesObject.isReference())
    {
        const PDFObjectReference reference = resourcesObject.getReference();
        if (!context.formWalk.enter(resourcesObject, depth))
        {
            return { resourcesObject, false };
        }

        const PDFObject referencedObject = builder.getObjectByReference(reference);
        SanitizedObjectResult result = sanitizeResourcesObject(builder, referencedObject, context, depth);
        if (result.changed)
        {
            builder.setObject(reference, qMove(result.object));
        }
        return { resourcesObject, false };
    }

    const PDFDictionary* resourcesDictionary = builder.getDictionaryFromObject(resourcesObject);
    if (!resourcesDictionary || !resourcesDictionary->hasKey("XObject"))
    {
        return { resourcesObject, false };
    }

    PDFObject xobjectObject = builder.getObject(resourcesDictionary->get("XObject"));
    const PDFDictionary* xobjectDictionary = builder.getDictionaryFromObject(xobjectObject);
    if (!xobjectDictionary)
    {
        return { resourcesObject, false };
    }

    PDFDictionaryBuilder updatedResourcesDictionary(*resourcesDictionary);
    PDFDictionaryBuilder updatedXObjectDictionary(*xobjectDictionary);
    bool changed = false;

    // Walk XObjects recursively because invisible OCR text is often stored in
    // nested Form XObjects instead of the page content stream itself.
    for (size_t i = 0, count = xobjectDictionary->getCount(); i < count; ++i)
    {
        const PDFInplaceOrMemoryString key = xobjectDictionary->getKey(i);
        const PDFObject currentObject = xobjectDictionary->getValue(i);
        SanitizedObjectResult result = sanitizeFormXObject(builder, currentObject, context, depth);
        if (result.changed)
        {
            updatedXObjectDictionary.setEntry(key, qMove(result.object));
            changed = true;
        }
    }

    if (!changed)
    {
        return { resourcesObject, false };
    }

    updatedResourcesDictionary.setEntry(PDFInplaceOrMemoryString("XObject"),
                                        PDFObject::createDictionary(qMove(updatedXObjectDictionary)));
    return { PDFObject::createDictionary(qMove(updatedResourcesDictionary)), true };
}

SanitizedObjectResult PDFInvisibleTextSanitizerHelper::sanitizeContentsObject(PDFDocumentBuilder& builder,
                                                                              const PDFObject& contentsObject,
                                                                              InvisibleTextSanitizationContext& context,
                                                                              PDFDocumentSanitizer::InvisibleTextState* state,
                                                                              int depth)
{
    if (contentsObject.isReference())
    {
        const PDFObjectReference reference = contentsObject.getReference();
        if (!context.contentWalk.enter(contentsObject, depth))
        {
            return { contentsObject, false };
        }

        const PDFObject referencedObject = builder.getObjectByReference(reference);
        SanitizedObjectResult result = sanitizeContentsObject(builder, referencedObject, context, state, depth + 1);
        if (result.changed)
        {
            builder.setObject(reference, qMove(result.object));
        }
        return { contentsObject, false };
    }

    if (contentsObject.isArray())
    {
        PDFArrayBuilder updatedArray(*contentsObject.getArray());
        bool changed = false;

        for (size_t i = 0, count = updatedArray.getCount(); i < count; ++i)
        {
            SanitizedObjectResult result = sanitizeContentsObject(builder, updatedArray.getItem(i), context, state, depth + 1);
            if (result.changed)
            {
                updatedArray.setItem(qMove(result.object), i);
                changed = true;
            }
        }

        return { changed ? PDFObject::createArray(qMove(updatedArray)) : contentsObject, changed };
    }

    if (!contentsObject.isStream())
    {
        return { contentsObject, false };
    }

    const PDFStream* stream = contentsObject.getStream();
    const QByteArray decodedContent = builder.getDecodedStream(stream);
    bool changed = false;
    QByteArray sanitizedContent = sanitizeInvisibleTextInContent(decodedContent, state, &changed);

    if (!changed)
    {
        return { contentsObject, false };
    }

    ++context.modifiedStreamCount;
    return { createCompressedContentStream(stream, qMove(sanitizedContent), PDFDictionaryBuilder(*stream->getDictionary())), true };
}

/// Removes the entries of the given keys from every dictionary of an object, also from the
/// dictionaries of streams (the keys of the XMP metadata, of the embedded files)
class PDFRemoveEntriesVisitor : public PDFUpdateObjectVisitor
{
public:
    explicit PDFRemoveEntriesVisitor(const PDFObjectStorage* storage, std::atomic<PDFInteger>* counter, const std::vector<const char*>* keys) :
        PDFUpdateObjectVisitor(storage),
        m_counter(counter),
        m_keys(keys)
    {

    }

    virtual void visitDictionary(const PDFDictionary* dictionary) override;

private:
    bool isRemovedKey(const PDFInplaceOrMemoryString& key) const;

    std::atomic<PDFInteger>* m_counter;
    const std::vector<const char*>* m_keys;
};

bool PDFRemoveEntriesVisitor::isRemovedKey(const PDFInplaceOrMemoryString& key) const
{
    for (const char* removedKey : *m_keys)
    {
        if (key == removedKey)
        {
            return true;
        }
    }
    return false;
}

void PDFRemoveEntriesVisitor::visitDictionary(const PDFDictionary* dictionary)
{
    Q_ASSERT(dictionary);

    std::vector<PDFDictionary::DictionaryEntry> entries;
    entries.reserve(dictionary->getCount());

    for (size_t i = 0, count = dictionary->getCount(); i < count; ++i)
    {
        dictionary->getValue(i).accept(this);
        Q_ASSERT(!m_objectStack.empty());
        if (!isRemovedKey(dictionary->getKey(i)))
        {
            entries.emplace_back(dictionary->getKey(i), m_objectStack.back());
        }
        else
        {
            ++*m_counter;
        }
        m_objectStack.pop_back();
    }

    m_objectStack.push_back(PDFObject::createDictionary(qMove(entries)));
}

/// Helpers of the steps of the sanitizer, which change the items of the arrays /Annots
class PDFDocumentSanitizerHelper
{
public:
    PDFDocumentSanitizerHelper() = delete;

    /// Removes the entries of \p keys from every object of the storage
    static PDFInteger removeEntries(PDFObjectStorage* storage, const std::vector<const char*>& keys, const PDFDocumentSanitizer* sanitizer)
    {
        std::atomic<PDFInteger> counter = 0;
        PDFObjectStorage::PDFObjects objects = storage->getObjects();
        const PDFObjectStorage* readStorage = storage;
        auto processEntry = [sanitizer, readStorage, &counter, &keys](PDFObjectStorage::Entry& entry)
        {
            if (sanitizer->isCanceled())
            {
                return;
            }

            PDFRemoveEntriesVisitor visitor(readStorage, &counter, &keys);
            entry.object.accept(&visitor);
            entry.object = visitor.getObject();
        };

        PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Unknown, objects.begin(), objects.end(), processEntry);
        storage->setObjects(qMove(objects));
        return counter.load();
    }

    /// The references of the pages: an annotation, which is a page of a hostile document, is
    /// never replaced by a null object
    static std::set<PDFObjectReference> getPageSet(const std::vector<PDFObjectReference>& pages)
    {
        return std::set<PDFObjectReference>(pages.cbegin(), pages.cend());
    }

    /// The annotation of an item of /Annots: an object, which says it is no annotation (a
    /// /Type other than /Annot), is not one of them
    static PDFAnnotationPtr parseAnnotation(const PDFObjectStorage* storage, const PDFObject& item)
    {
        const PDFDictionary* dictionary = storage->getDictionaryFromObject(item);
        if (!dictionary)
        {
            return PDFAnnotationPtr();
        }
        const PDFObject& type = storage->getObject(dictionary->get("Type"));
        if (type.isName() && type.getString() != "Annot")
        {
            return PDFAnnotationPtr();
        }
        return PDFAnnotation::parseItem(storage, item);
    }

    /// Removes the items of the array /Annots of a page, for which \p isRemoved is true, and the
    /// popup annotations of the removed annotations (only a /Popup, which is a popup annotation);
    /// the removed indirect annotations are collected in \p nulledAnnotations
    /// \returns number of the removed annotations, which matched (the popups are not counted)
    template<typename Predicate>
    static int removePageAnnotations(PDFDocumentBuilder* builder,
                                     PDFObjectReference pageReference,
                                     const std::set<PDFObjectReference>& pages,
                                     Predicate isRemoved,
                                     std::set<PDFObjectReference>* nulledAnnotations)
    {
        const PDFObjectStorage* storage = builder->getStorage();
        const PDFDictionary* pageDictionary = storage->getDictionaryFromObject(storage->getObjectByReference(pageReference));
        if (!pageDictionary)
        {
            return 0;
        }

        const PDFObject& annotationsObject = storage->getObject(pageDictionary->get("Annots"));
        if (!annotationsObject.isArray())
        {
            return 0;
        }

        const PDFArray* annotations = annotationsObject.getArray();
        std::vector<bool> isItemRemoved(annotations->getCount(), false);
        std::set<PDFObjectReference> removedReferences;
        int removedCount = 0;

        for (size_t i = 0, count = annotations->getCount(); i < count; ++i)
        {
            const PDFObject& item = annotations->getItem(i);
            if (item.isReference() && pages.count(item.getReference()))
            {
                continue;
            }

            PDFAnnotationPtr annotation = parseAnnotation(storage, item);
            if (!annotation || !isRemoved(annotation.get(), storage->getDictionaryFromObject(item)))
            {
                continue;
            }

            isItemRemoved[i] = true;
            ++removedCount;
            if (item.isReference())
            {
                removedReferences.insert(item.getReference());
            }

            // The popup window of the removed annotation holds its text too
            const PDFDictionary* dictionary = storage->getDictionaryFromObject(item);
            const PDFObject popup = dictionary ? dictionary->get("Popup") : PDFObject();
            if (popup.isReference() && !pages.count(popup.getReference()))
            {
                const PDFDictionary* popupDictionary = storage->getDictionaryFromObject(storage->getObjectByReference(popup.getReference()));
                const PDFObject subtype = popupDictionary ? storage->getObject(popupDictionary->get("Subtype")) : PDFObject();
                if (subtype.isName() && subtype.getString() == "Popup")
                {
                    removedReferences.insert(popup.getReference());
                }
            }
        }

        if (removedCount == 0)
        {
            return 0;
        }

        PDFArrayBuilder keptItems;
        for (size_t i = 0, count = annotations->getCount(); i < count; ++i)
        {
            const PDFObject& item = annotations->getItem(i);
            if (isItemRemoved[i] || (item.isReference() && removedReferences.count(item.getReference())))
            {
                continue;
            }
            keptItems.appendItem(item);
        }

        // The page gets its own array: an array shared by more pages stays for the others
        PDFDictionaryBuilder updatedPage(*pageDictionary);
        if (keptItems.getCount() > 0)
        {
            updatedPage.setEntry(PDFInplaceOrMemoryString("Annots"), PDFObject::createArray(std::move(keptItems)));
        }
        else
        {
            updatedPage.removeEntry("Annots");
        }
        builder->setObject(pageReference, PDFObject::createDictionary(std::move(updatedPage)));

        nulledAnnotations->insert(removedReferences.cbegin(), removedReferences.cend());
        return removedCount;
    }

    /// Removes the signature fields of an array of fields (/Fields, /Kids), the fields which
    /// stay are written again, when their kids have changed
    /// \returns the new array, std::nullopt = unchanged
    static std::optional<PDFObject> removeSignatureFields(PDFDocumentBuilder* builder,
                                                          const PDFObject& fieldsObject,
                                                          int depth,
                                                          PDFObjectWalk* walk,
                                                          std::set<PDFObjectReference>* nulledFields)
    {
        const PDFObjectStorage* storage = builder->getStorage();
        const PDFObject& dereferenced = storage->getObject(fieldsObject);
        if (!dereferenced.isArray() || depth > PDFFormField::MAX_FIELD_TREE_DEPTH)
        {
            return std::nullopt;
        }

        const PDFArray* fields = dereferenced.getArray();
        PDFArrayBuilder keptFields;
        bool isChanged = false;
        for (size_t i = 0, count = fields->getCount(); i < count; ++i)
        {
            PDFObject item = fields->getItem(i);
            if (item.isReference() && !walk->enter(item, depth))
            {
                // A cycle or a field shared by more parents: it was decided once
                if (!nulledFields->count(item.getReference()))
                {
                    keptFields.appendItem(item);
                }
                else
                {
                    isChanged = true;
                }
                continue;
            }

            const PDFDictionary* field = storage->getDictionaryFromObject(item);
            if (field && PDFDocumentSanitizer::isSignatureField(storage, field))
            {
                isChanged = true;
                if (item.isReference())
                {
                    nulledFields->insert(item.getReference());
                }
                continue;
            }

            if (field && field->hasKey("Kids"))
            {
                std::optional<PDFObject> kids = removeSignatureFields(builder, field->get("Kids"), depth + 1, walk, nulledFields);
                if (kids)
                {
                    PDFDictionaryBuilder updatedField(*field);
                    updatedField.setEntry(PDFInplaceOrMemoryString("Kids"), std::move(*kids));
                    PDFObject updatedFieldObject = PDFObject::createDictionary(std::move(updatedField));
                    if (item.isReference())
                    {
                        builder->setObject(item.getReference(), std::move(updatedFieldObject));
                    }
                    else
                    {
                        item = std::move(updatedFieldObject);
                        isChanged = true;
                    }
                }
            }
            keptFields.appendItem(item);
        }

        if (!isChanged)
        {
            return std::nullopt;
        }
        return PDFObject::createArray(std::move(keptFields));
    }
};

/// The properties of the XMP metadata, which are the entries of the document information
/// dictionary (ISO 32000-2, 14.3.3, Table 349; ISO 19005-1, Table 1)
class PDFDocumentInfoXmpHelper
{
public:
    PDFDocumentInfoXmpHelper() = delete;

    static constexpr const char* DC_NAMESPACE = "http://purl.org/dc/elements/1.1/";
    static constexpr const char* XMP_NAMESPACE = "http://ns.adobe.com/xap/1.0/";
    static constexpr const char* PDF_NAMESPACE = "http://ns.adobe.com/pdf/1.3/";
    static constexpr const char* RDF_NAMESPACE = "http://www.w3.org/1999/02/22-rdf-syntax-ns#";

    struct Property
    {
        const char* namespaceUri;
        const char* name;
        const char* qualifiedName;  ///< The name with the usual prefix (the documents may use another one)
    };

    // The entries Title, Author, Subject, Keywords, Creator, Producer, CreationDate, ModDate, Trapped
    static constexpr Property PROPERTIES[] = {
        { DC_NAMESPACE, "title", "dc:title" },
        { DC_NAMESPACE, "creator", "dc:creator" },
        { DC_NAMESPACE, "description", "dc:description" },
        { PDF_NAMESPACE, "Keywords", "pdf:Keywords" },
        { XMP_NAMESPACE, "CreatorTool", "xmp:CreatorTool" },
        { PDF_NAMESPACE, "Producer", "pdf:Producer" },
        { XMP_NAMESPACE, "CreateDate", "xmp:CreateDate" },
        { XMP_NAMESPACE, "ModifyDate", "xmp:ModifyDate" },
        { PDF_NAMESPACE, "Trapped", "pdf:Trapped" },
    };

    static constexpr size_t PROPERTY_COUNT = std::size(PROPERTIES);

    /// Index of the property in PROPERTIES, -1 for another one
    static int find(QStringView namespaceUri, QStringView name)
    {
        for (size_t i = 0; i < PROPERTY_COUNT; ++i)
        {
            if (namespaceUri == QLatin1StringView(PROPERTIES[i].namespaceUri) && name == QLatin1StringView(PROPERTIES[i].name))
            {
                return int(i);
            }
        }
        return -1;
    }

    static bool isSpace(QChar character)
    {
        return character == QLatin1Char(' ') || character == QLatin1Char('\t') || character == QLatin1Char('\r') || character == QLatin1Char('\n');
    }

    /// The start of a removed element, which is alone on its line: its indentation and the line
    /// break in front of it go with it (no empty line stays)
    static qsizetype extendOverIndentation(QStringView text, qsizetype start)
    {
        qsizetype position = start;
        while (position > 0 && (text[position - 1] == QLatin1Char(' ') || text[position - 1] == QLatin1Char('\t')))
        {
            --position;
        }
        if (position > 0 && text[position - 1] == QLatin1Char('\n'))
        {
            --position;
            if (position > 0 && text[position - 1] == QLatin1Char('\r'))
            {
                --position;
            }
            return position;
        }
        if (position > 0 && text[position - 1] == QLatin1Char('\r'))
        {
            return position - 1;
        }
        return start;
    }

    /// The parts of the attributes \p qualifiedNames of a start tag: the white space in front of
    /// each attribute, its name, its equal sign and its quoted value (positions in \p text)
    static void findAttributes(QStringView text, qsizetype tagStart, qsizetype tagEnd, const QStringList& qualifiedNames, std::vector<std::pair<qsizetype, qsizetype>>* ranges)
    {
        // The name of the element
        qsizetype position = tagStart + 1;
        while (position < tagEnd && !isSpace(text[position]) && text[position] != QLatin1Char('>') && text[position] != QLatin1Char('/'))
        {
            ++position;
        }

        while (position < tagEnd)
        {
            const qsizetype spaceStart = position;
            while (position < tagEnd && isSpace(text[position]))
            {
                ++position;
            }
            const qsizetype nameStart = position;
            while (position < tagEnd && !isSpace(text[position]) && text[position] != QLatin1Char('=') && text[position] != QLatin1Char('>') && text[position] != QLatin1Char('/'))
            {
                ++position;
            }
            if (position == nameStart)
            {
                return;
            }
            const QStringView name = text.mid(nameStart, position - nameStart);
            while (position < tagEnd && isSpace(text[position]))
            {
                ++position;
            }
            if (position >= tagEnd || text[position] != QLatin1Char('='))
            {
                return;
            }
            ++position;
            while (position < tagEnd && isSpace(text[position]))
            {
                ++position;
            }
            if (position >= tagEnd || (text[position] != QLatin1Char('"') && text[position] != QLatin1Char('\'')))
            {
                return;
            }
            const qsizetype valueEnd = text.indexOf(text[position], position + 1);
            if (valueEnd < 0 || valueEnd >= tagEnd)
            {
                return;
            }
            position = valueEnd + 1;
            if (qualifiedNames.contains(name))
            {
                ranges->emplace_back(spaceStart, position);
            }
        }
    }

    /// The text of the packet names a namespace of the properties
    static bool hasNamespaceText(const QByteArray& metadata)
    {
        return metadata.contains(DC_NAMESPACE) || metadata.contains(XMP_NAMESPACE) || metadata.contains(PDF_NAMESPACE);
    }

    /// Finds the properties: their parts in \p text and their indices
    /// \returns false, when the text cannot be parsed up to the end of its root element
    static bool scan(QStringView text, std::vector<std::pair<qsizetype, qsizetype>>* ranges, std::vector<bool>* found)
    {
        QXmlStreamReader reader(text);
        std::vector<bool> isDescriptionStack;
        bool isRootClosed = false;
        while (!reader.atEnd())
        {
            const QXmlStreamReader::TokenType token = reader.readNext();
            if (token == QXmlStreamReader::StartElement)
            {
                // The start tag: from its "<" to its ">" (no "<" in a value of an attribute)
                const qsizetype tagEnd = text.lastIndexOf(QLatin1Char('>'), reader.characterOffset() - 1) + 1;
                const qsizetype tagStart = tagEnd > 0 ? text.lastIndexOf(QLatin1Char('<'), tagEnd - 1) : -1;
                if (tagStart < 0)
                {
                    return false;
                }

                const bool isParentDescription = !isDescriptionStack.empty() && isDescriptionStack.back();
                const int index = isParentDescription ? find(reader.namespaceUri(), reader.name()) : -1;
                if (index >= 0)
                {
                    // The element form: the element with its value goes
                    reader.skipCurrentElement();
                    if (reader.hasError())
                    {
                        break;
                    }
                    const qsizetype elementEnd = text.lastIndexOf(QLatin1Char('>'), reader.characterOffset() - 1) + 1;
                    ranges->emplace_back(extendOverIndentation(text, tagStart), qMax(elementEnd, tagEnd));
                    (*found)[size_t(index)] = true;
                    continue;
                }

                const bool isDescription = reader.namespaceUri() == QLatin1StringView(RDF_NAMESPACE) && reader.name() == QLatin1StringView("Description");
                if (isDescription)
                {
                    // The attribute form of the properties of rdf:Description
                    QStringList qualifiedNames;
                    for (const QXmlStreamAttribute& attribute : reader.attributes())
                    {
                        const int attributeIndex = find(attribute.namespaceUri(), attribute.name());
                        if (attributeIndex >= 0)
                        {
                            qualifiedNames << attribute.qualifiedName().toString();
                            (*found)[size_t(attributeIndex)] = true;
                        }
                    }
                    if (!qualifiedNames.isEmpty())
                    {
                        findAttributes(text, tagStart, tagEnd, qualifiedNames, ranges);
                    }
                }
                isDescriptionStack.push_back(isDescription);
            }
            else if (token == QXmlStreamReader::EndElement && !isDescriptionStack.empty())
            {
                isDescriptionStack.pop_back();
                isRootClosed = isDescriptionStack.empty();
            }
        }

        // Bytes after the root element (some producers pad the packet by zeros) do not matter
        return !reader.hasError() || isRootClosed;
    }
};

QStringList PDFDocumentSanitizer::getDocumentInfoPropertyNames()
{
    QStringList names;
    for (const PDFDocumentInfoXmpHelper::Property& property : PDFDocumentInfoXmpHelper::PROPERTIES)
    {
        names << QString::fromLatin1(property.qualifiedName);
    }
    return names;
}

PDFDocumentSanitizer::DocumentInfoProperties PDFDocumentSanitizer::removeDocumentInfoProperties(const QByteArray& metadata)
{
    using Helper = PDFDocumentInfoXmpHelper;

    DocumentInfoProperties result;
    result.metadata = metadata;
    if (metadata.isEmpty())
    {
        return result;
    }

    // XMP is UTF-8, UTF-16 or UTF-32 (the packet starts by "<"); a byte order mark stays where it is
    const QStringConverter::Encoding encoding = QStringConverter::encodingForData(metadata, u'<').value_or(QStringConverter::Utf8);
    QStringDecoder decoder(encoding, QStringConverter::Flag::ConvertInitialBom);
    const QString text = decoder.decode(metadata);
    const qsizetype bodyStart = text.startsWith(QChar(0xFEFF)) ? 1 : 0;
    const QStringView body = QStringView(text).mid(bodyStart);

    std::vector<std::pair<qsizetype, qsizetype>> ranges;
    std::vector<bool> found(Helper::PROPERTY_COUNT, false);
    if (decoder.hasError() || !Helper::scan(body, &ranges, &found))
    {
        // Nothing can be removed from it: it may have them, when it names their namespaces
        result.isReadable = !Helper::hasNamespaceText(metadata);
        return result;
    }

    for (size_t i = 0; i < Helper::PROPERTY_COUNT; ++i)
    {
        if (found[i])
        {
            result.names << QString::fromLatin1(Helper::PROPERTIES[i].qualifiedName);
        }
    }
    if (ranges.empty())
    {
        return result;
    }

    std::sort(ranges.begin(), ranges.end());
    QString cleaned = text.left(bodyStart);
    qsizetype position = 0;
    for (const auto& [start, end] : ranges)
    {
        if (start > position)
        {
            cleaned += body.mid(position, start - position);
        }
        position = qMax(position, end);
    }
    cleaned += body.mid(position);

    QStringEncoder encoder(encoding);
    const QByteArray cleanedMetadata = encoder.encode(cleaned);

    // The removal is verified: the packet is still read and has none of them
    std::vector<std::pair<qsizetype, qsizetype>> verificationRanges;
    std::vector<bool> verificationFound(Helper::PROPERTY_COUNT, false);
    const qsizetype cleanedBodyStart = cleaned.startsWith(QChar(0xFEFF)) ? 1 : 0;
    if (encoder.hasError() || !Helper::scan(QStringView(cleaned).mid(cleanedBodyStart), &verificationRanges, &verificationFound) || !verificationRanges.empty())
    {
        result.names.clear();
        result.isReadable = false;
        return result;
    }

    result.metadata = cleanedMetadata;
    return result;
}

QByteArray PDFDocumentSanitizer::getCatalogMetadata(const PDFObjectStorage* storage, PDFObjectReference* reference)
{
    if (reference)
    {
        *reference = PDFObjectReference();
    }

    const PDFDictionary* trailer = storage->getDictionaryFromObject(storage->getTrailerDictionary());
    const PDFDictionary* catalog = trailer ? storage->getDictionaryFromObject(trailer->get("Root")) : nullptr;
    if (!catalog)
    {
        return QByteArray();
    }

    const PDFObject& metadataItem = catalog->get("Metadata");
    const PDFObject& metadataObject = storage->getObject(metadataItem);
    if (!metadataObject.isStream())
    {
        return QByteArray();
    }
    if (reference && metadataItem.isReference())
    {
        *reference = metadataItem.getReference();
    }
    return storage->getDecodedStream(metadataObject.getStream());
}

PDFDocumentSanitizer::PDFDocumentSanitizer(SanitizationFlag flags, QObject* parent) :
    QObject(parent),
    m_flags(flags)
{

}

QByteArray PDFDocumentSanitizer::removeInvisibleText(const QByteArray& content, InvisibleTextState* state, bool* changed)
{
    return PDFInvisibleTextSanitizerHelper::sanitizeInvisibleTextInContent(content, state, changed);
}

bool PDFDocumentSanitizer::isCommentAnnotation(const PDFAnnotation* annotation)
{
    return annotation && (annotation->asMarkupAnnotation() != nullptr || annotation->getType() == AnnotationType::Popup);
}

bool PDFDocumentSanitizer::isSignatureField(const PDFObjectStorage* storage, const PDFDictionary* dictionary)
{
    // The inherited type of the field (ISO 32000-2, Table 226): the chain of /Parent, each parent once
    PDFObjectWalk walk(PDFFormField::MAX_FIELD_TREE_DEPTH);
    const PDFDictionary* current = dictionary;
    for (int depth = 1; current; ++depth)
    {
        const PDFObject& fieldType = storage->getObject(current->get("FT"));
        if (fieldType.isName())
        {
            return fieldType.getString() == "Sig";
        }

        const PDFObject& parent = current->get("Parent");
        current = walk.enter(parent, depth) ? storage->getDictionaryFromObject(parent) : nullptr;
    }
    return false;
}

std::vector<PDFDocumentSanitizer::SanitizationFlag> PDFDocumentSanitizer::getSteps(SanitizationFlags flags)
{
    // The order of the rows of Remove Hidden Data (438:12588)
    std::vector<SanitizationFlag> steps;
    for (SanitizationFlag flag : { DocumentInfo, Metadata, MarkupAnnotations, FileAttachments, Outline, PageLabels, EmbeddedSearchIndex, PageThumbnails, InvisibleText, SignatureFields })
    {
        if (flags.testFlag(flag))
        {
            steps.push_back(flag);
        }
    }
    return steps;
}

void PDFDocumentSanitizer::sanitize()
{
    m_removedDocumentInfoProperties.clear();
    Q_EMIT sanitizationStarted();

    for (SanitizationFlag step : getSteps(m_flags))
    {
        if (isCanceled())
        {
            break;
        }

        const auto phase = m_progress ? m_phaseOfStep.find(int(step)) : m_phaseOfStep.cend();
        if (phase != m_phaseOfStep.cend())
        {
            m_progress->beginPhase(phase->second);
        }

        switch (step)
        {
            case DocumentInfo:
                performSanitizeDocumentInfo();
                break;
            case Metadata:
                performSanitizeMetadata();
                break;
            case MarkupAnnotations:
                performSanitizeMarkupAnnotations();
                break;
            case FileAttachments:
                performSanitizeFileAttachments();
                break;
            case Outline:
                performSanitizeOutline();
                break;
            case PageLabels:
                performSanitizePageLabels();
                break;
            case EmbeddedSearchIndex:
                performSanitizeEmbeddedSearchIndex();
                break;
            case PageThumbnails:
                performSanitizePageThumbnails();
                break;
            case InvisibleText:
                performSanitizeInvisibleText();
                break;
            case SignatureFields:
                performSanitizeSignatureFields();
                break;
            default:
                break;
        }
    }

    // The builders of the steps have written /Producer and /ModDate into the information dictionary
    if (m_flags.testFlag(DocumentInfo) && !isCanceled())
    {
        removeDocumentInfoEntry();
    }

    // Optimize - remove unused objects
    PDFOptimizer optimizer(PDFOptimizer::OptimizationFlags(PDFOptimizer::RemoveUnusedObjects | PDFOptimizer::ShrinkObjectStorage | PDFOptimizer::RemoveNullObjects), nullptr);
    optimizer.setOperationControl(m_operationControl);
    optimizer.setStorage(m_storage);
    optimizer.optimize();
    m_storage = optimizer.takeStorage();

    Q_EMIT sanitizationFinished();
}

PDFDocumentSanitizer::SanitizationFlags PDFDocumentSanitizer::getFlags() const
{
    return m_flags;
}

void PDFDocumentSanitizer::setFlags(SanitizationFlags flags)
{
    m_flags = flags;
}

void PDFDocumentSanitizer::performSanitizeDocumentInfo()
{
    // The same data in the XMP metadata of the document (ISO 32000-2, 14.3.3, Table 349) go
    // too, unless every metadata stream goes in the next step; the other properties stay
    if (!m_flags.testFlag(Metadata))
    {
        const DocumentInfoProperties properties = removeDocumentInfoProperties(getCatalogMetadata(&m_storage, nullptr));
        if (!properties.names.isEmpty())
        {
            PDFDocumentBuilder metadataBuilder(m_storage, PDFVersion(2, 0));
            metadataBuilder.setCatalogMetadata(properties.metadata);
            m_storage = metadataBuilder.build().getStorage();
            m_removedDocumentInfoProperties = properties.names;
        }
    }

    // The entry /Info of the trailer is removed after the last step (removeDocumentInfoEntry)
    PDFDocumentBuilder builder(m_storage, PDFVersion(2, 0));
    if (builder.getDocumentInfo().isValid())
    {
        Q_EMIT sanitizationProgress(tr("Document info was removed."));
    }
}

void PDFDocumentSanitizer::removeDocumentInfoEntry()
{
    PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("Info");
    factory << PDFObject();
    factory.endDictionaryItem();
    factory.endDictionary();
    m_storage.updateTrailerDictionary(factory.takeObject());
}

void PDFDocumentSanitizer::performSanitizeMetadata()
{
    const PDFInteger count = PDFDocumentSanitizerHelper::removeEntries(&m_storage, { "Metadata" }, this);
    Q_EMIT sanitizationProgress(tr("Metadata streams removed: %1").arg(count));
}

void PDFDocumentSanitizer::performSanitizeOutline()
{
    PDFDocumentBuilder builder(m_storage, PDFVersion(2, 0));
    PDFObject catalogObject = builder.getObjectByReference(builder.getCatalogReference());
    const PDFDictionary* catalogDictionary = builder.getDictionaryFromObject(catalogObject);
    const bool hasOutline = catalogDictionary && catalogDictionary->hasKey("Outlines");

    if (hasOutline)
    {
        builder.removeOutline();
        PDFDocument document = builder.build();
        m_storage = document.getStorage();
        Q_EMIT sanitizationProgress(tr("Outline was removed."));
    }
}

void PDFDocumentSanitizer::performSanitizeFileAttachments()
{
    auto filter = [](const PDFAnnotation* annotation, const PDFDictionary*)
    {
        return annotation->getType() == AnnotationType::FileAttachment;
    };
    removeAnnotations(filter, tr("File attachments removed: %1."));
    if (isCanceled())
    {
        return;
    }

    // The embedded files of every file specification, the associated files (/AF, PDF 2.0)
    // and the related files: an embedded file is not reached by the name tree only
    PDFDocumentSanitizerHelper::removeEntries(&m_storage, { "EF", "RF", "AF" }, this);

    // The name tree of the embedded files and the portable collection of the catalog
    PDFDocumentBuilder builder(m_storage, PDFVersion(2, 0));
    const PDFObjectReference catalogReference = builder.getCatalogReference();
    const PDFDictionary* catalogDictionary = builder.getDictionaryFromObject(builder.getObjectByReference(catalogReference));
    if (!catalogDictionary)
    {
        return;
    }

    PDFDictionaryBuilder updatedCatalog(*catalogDictionary);
    bool isCatalogChanged = false;
    if (catalogDictionary->hasKey("Collection"))
    {
        updatedCatalog.removeEntry("Collection");
        isCatalogChanged = true;
    }

    const PDFObject& namesObject = catalogDictionary->get("Names");
    const PDFDictionary* namesDictionary = builder.getDictionaryFromObject(namesObject);
    if (namesDictionary && namesDictionary->hasKey("EmbeddedFiles"))
    {
        PDFDictionaryBuilder updatedNames(*namesDictionary);
        updatedNames.removeEntry("EmbeddedFiles");
        PDFObject updatedNamesObject = PDFObject::createDictionary(std::move(updatedNames));
        if (namesObject.isReference())
        {
            builder.setObject(namesObject.getReference(), std::move(updatedNamesObject));
        }
        else
        {
            updatedCatalog.setEntry(PDFInplaceOrMemoryString("Names"), std::move(updatedNamesObject));
            isCatalogChanged = true;
        }
        Q_EMIT sanitizationProgress(tr("Embedded files were removed."));
    }

    if (isCatalogChanged)
    {
        builder.setObject(catalogReference, PDFObject::createDictionary(std::move(updatedCatalog)));
    }
    m_storage = *builder.getStorage();
}

void PDFDocumentSanitizer::performSanitizeEmbeddedSearchIndex()
{
    PDFDocumentBuilder builder(m_storage, PDFVersion(2, 0));
    PDFObject catalogObject = builder.getObjectByReference(builder.getCatalogReference());
    const PDFDictionary* catalogDictionary = builder.getDictionaryFromObject(catalogObject);
    const bool hasPieceInfo = catalogDictionary && catalogDictionary->hasKey("PieceInfo");

    if (hasPieceInfo)
    {
        PDFObject pieceInfoObject = builder.getObject(catalogDictionary->get("PieceInfo"));
        const PDFDictionary* pieceInfoDictionary = builder.getDictionaryFromObject(pieceInfoObject);
        if (pieceInfoDictionary && pieceInfoDictionary->hasKey("SearchIndex"))
        {
            PDFDictionaryBuilder dictionaryCopy(*pieceInfoDictionary);
            dictionaryCopy.setEntry(PDFInplaceOrMemoryString("SearchIndex"), PDFObject());
            pieceInfoObject = PDFObject::createDictionary(qMove(dictionaryCopy));

            PDFObjectFactory factory;
            factory.beginDictionary();
            factory.beginDictionaryItem("PieceInfo");
            factory << pieceInfoObject;
            factory.endDictionaryItem();
            factory.endDictionary();
            PDFObject newCatalog = factory.takeObject();
            builder.mergeTo(builder.getCatalogReference(), std::move(newCatalog));
            PDFDocument document = builder.build();
            m_storage = document.getStorage();
            Q_EMIT sanitizationProgress(tr("Search index was removed."));
        }
    }
}

void PDFDocumentSanitizer::performSanitizeMarkupAnnotations()
{
    // A popup annotation holds the text of its comment
    auto filter = [](const PDFAnnotation* annotation, const PDFDictionary*)
    {
        return isCommentAnnotation(annotation);
    };
    removeAnnotations(filter, tr("Markup annotations removed: %1."));
}

void PDFDocumentSanitizer::performSanitizePageThumbnails()
{
    PDFDocumentBuilder builder(m_storage, PDFVersion(2, 0));
    builder.flattenPageTree();
    std::vector<PDFObjectReference> pageReferences = builder.getPages();
    std::vector<PDFObjectReference> pagesWithThumbnail;

    for (const PDFObjectReference& pageReference : pageReferences)
    {
        const PDFDictionary* pageDictionary = builder.getDictionaryFromObject(builder.getObjectByReference(pageReference));
        if (pageDictionary && pageDictionary->hasKey("Thumb"))
        {
            pagesWithThumbnail.push_back(pageReference);
        }
    }

    if (!pagesWithThumbnail.empty())
    {
        for (const auto& pageReference : pagesWithThumbnail)
        {
            builder.removePageThumbnail(pageReference);
        }

        PDFDocument document = builder.build();
        m_storage = document.getStorage();
        Q_EMIT sanitizationProgress(tr("Page thumbnails removed: %1.").arg(pagesWithThumbnail.size()));
    }
}

void PDFDocumentSanitizer::performSanitizePageLabels()
{
    PDFDocumentBuilder builder(m_storage, PDFVersion(2, 0));
    PDFObject catalogObject = builder.getObjectByReference(builder.getCatalogReference());
    const PDFDictionary* catalogDictionary = builder.getDictionaryFromObject(catalogObject);
    const bool hasPageLabels = catalogDictionary && catalogDictionary->hasKey("PageLabels");

    if (hasPageLabels)
    {
        PDFObjectFactory objectBuilder;
        objectBuilder.beginDictionary();
        objectBuilder.beginDictionaryItem("PageLabels");
        objectBuilder << PDFObject();
        objectBuilder.endDictionaryItem();
        objectBuilder.endDictionary();

        builder.mergeTo(builder.getCatalogReference(), objectBuilder.takeObject());
        PDFDocument document = builder.build();
        m_storage = document.getStorage();
        Q_EMIT sanitizationProgress(tr("Page labels were removed."));
    }
}

void PDFDocumentSanitizer::performSanitizeInvisibleText()
{
    // This sanitizer intentionally performs a targeted low-level rewrite of page
    // and form content streams. It removes only invisible text operators used by
    // OCR layers and leaves the rest of the page description intact.
    PDFDocumentBuilder builder(m_storage, PDFVersion(2, 0));
    builder.flattenPageTree();
    const std::vector<PDFObjectReference> pageReferences = builder.getPages();

    PDFInvisibleTextSanitizerHelper::InvisibleTextSanitizationContext context;
    bool changed = false;

    for (const PDFObjectReference& pageReference : pageReferences)
    {
        if (isCanceled())
        {
            return;
        }

        const PDFObject pageObject = builder.getObjectByReference(pageReference);
        const PDFDictionary* pageDictionary = builder.getDictionaryFromObject(pageObject);
        if (!pageDictionary)
        {
            continue;
        }

        PDFDictionaryBuilder updatedPageDictionary(*pageDictionary);
        bool pageChanged = false;

        if (pageDictionary->hasKey("Contents"))
        {
            // One state for the streams of the page (they are one content stream)
            PDFDocumentSanitizer::InvisibleTextState state;
            SanitizedObjectResult result = PDFInvisibleTextSanitizerHelper::sanitizeContentsObject(builder, pageDictionary->get("Contents"), context, &state, 0);
            if (result.changed)
            {
                updatedPageDictionary.setEntry(PDFInplaceOrMemoryString("Contents"), qMove(result.object));
                pageChanged = true;
            }
        }

        if (pageDictionary->hasKey("Resources"))
        {
            SanitizedObjectResult result = PDFInvisibleTextSanitizerHelper::sanitizeResourcesObject(builder, pageDictionary->get("Resources"), context, 0);
            if (result.changed)
            {
                updatedPageDictionary.setEntry(PDFInplaceOrMemoryString("Resources"), qMove(result.object));
                pageChanged = true;
            }
        }

        // The private data of an own OCR layer of PDF4QT hold the recognized words
        // (PDFOCRTextLayerWriter, /PieceInfo /PDF4QT_OCR): they go with the invisible text
        const PDFDictionary* pieceInfo = builder.getDictionaryFromObject(pageDictionary->get("PieceInfo"));
        if (pieceInfo && pieceInfo->hasKey(PDFOCRTextLayerWriter::PIECE_INFO_KEY))
        {
            PDFDictionaryBuilder updatedPieceInfo(*pieceInfo);
            updatedPieceInfo.removeEntry(PDFOCRTextLayerWriter::PIECE_INFO_KEY);
            if (updatedPieceInfo.getCount() > 0)
            {
                updatedPageDictionary.setEntry(PDFInplaceOrMemoryString("PieceInfo"), PDFObject::createDictionary(std::move(updatedPieceInfo)));
            }
            else
            {
                updatedPageDictionary.removeEntry("PieceInfo");
            }
            pageChanged = true;
        }

        if (pageChanged)
        {
            builder.setObject(pageReference, PDFObject::createDictionary(qMove(updatedPageDictionary)));
            changed = true;
        }
    }

    if (changed || context.modifiedStreamCount > 0)
    {
        PDFDocument document = builder.build();
        m_storage = document.getStorage();
        Q_EMIT sanitizationProgress(tr("Invisible text content streams sanitized: %1.").arg(context.modifiedStreamCount));
    }
}

void PDFDocumentSanitizer::performSanitizeSignatureFields()
{
    using Helper = PDFDocumentSanitizerHelper;

    PDFDocumentBuilder builder(m_storage, PDFVersion(2, 0));
    builder.flattenPageTree();
    const std::vector<PDFObjectReference> pageReferences = builder.getPages();
    const std::set<PDFObjectReference> pages = Helper::getPageSet(pageReferences);
    std::set<PDFObjectReference> nulledObjects;

    // The widgets of the signature fields on the pages (also a widget, which is not in the field tree)
    int removedWidgets = 0;
    const PDFObjectStorage* storage = builder.getStorage();
    for (const PDFObjectReference& pageReference : pageReferences)
    {
        if (isCanceled())
        {
            return;
        }

        auto filter = [storage](const PDFAnnotation* annotation, const PDFDictionary* dictionary)
        {
            return annotation->getType() == AnnotationType::Widget && dictionary && isSignatureField(storage, dictionary);
        };
        removedWidgets += Helper::removePageAnnotations(&builder, pageReference, pages, filter, &nulledObjects);
    }

    // The field tree, the signature flags, the permissions (DocMDP, UR3 refer to the
    // signatures) and the security store of the validation (certificates, OCSP, CRL)
    const PDFObjectReference catalogReference = builder.getCatalogReference();
    const PDFDictionary* catalogDictionary = builder.getDictionaryFromObject(builder.getObjectByReference(catalogReference));
    if (!catalogDictionary)
    {
        return;
    }

    PDFDictionaryBuilder updatedCatalog(*catalogDictionary);
    bool isCatalogChanged = false;
    for (const char* key : { "Perms", "DSS" })
    {
        if (catalogDictionary->hasKey(key))
        {
            updatedCatalog.removeEntry(key);
            isCatalogChanged = true;
        }
    }

    int removedFields = 0;
    const PDFObject& formObject = catalogDictionary->get("AcroForm");
    if (const PDFDictionary* formDictionary = builder.getDictionaryFromObject(formObject))
    {
        PDFObjectWalk walk(PDFFormField::MAX_FIELD_TREE_DEPTH);
        std::set<PDFObjectReference> nulledFields;
        std::optional<PDFObject> fields = Helper::removeSignatureFields(&builder, formDictionary->get("Fields"), 0, &walk, &nulledFields);

        // The dictionary of the form is read again: removeSignatureFields may have written it
        formDictionary = builder.getDictionaryFromObject(builder.getObject(formObject));
        if (formDictionary && (fields || formDictionary->hasKey("SigFlags")))
        {
            PDFDictionaryBuilder updatedForm(*formDictionary);
            if (fields)
            {
                updatedForm.setEntry(PDFInplaceOrMemoryString("Fields"), std::move(*fields));
            }
            updatedForm.removeEntry("SigFlags");

            PDFObject updatedFormObject = PDFObject::createDictionary(std::move(updatedForm));
            if (formObject.isReference())
            {
                builder.setObject(formObject.getReference(), std::move(updatedFormObject));
            }
            else
            {
                updatedCatalog.setEntry(PDFInplaceOrMemoryString("AcroForm"), std::move(updatedFormObject));
                isCatalogChanged = true;
            }
        }

        for (const PDFObjectReference& reference : nulledFields)
        {
            if (!pages.count(reference) && reference != catalogReference)
            {
                nulledObjects.insert(reference);
                ++removedFields;
            }
        }
    }

    if (isCatalogChanged)
    {
        builder.setObject(catalogReference, PDFObject::createDictionary(std::move(updatedCatalog)));
    }

    for (const PDFObjectReference& reference : nulledObjects)
    {
        if (reference != catalogReference)
        {
            builder.setObject(reference, PDFObject());
        }
    }

    m_storage = *builder.getStorage();
    if (removedWidgets > 0 || removedFields > 0 || isCatalogChanged)
    {
        Q_EMIT sanitizationProgress(tr("Signature fields removed: %1.").arg(qMax(removedWidgets, removedFields)));
    }
}

void PDFDocumentSanitizer::removeAnnotations(const std::function<bool(const PDFAnnotation*, const PDFDictionary*)>& filter, QString message)
{
    using Helper = PDFDocumentSanitizerHelper;

    PDFDocumentBuilder builder(m_storage, PDFVersion(2, 0));
    builder.flattenPageTree();
    const std::vector<PDFObjectReference> pageReferences = builder.getPages();
    const std::set<PDFObjectReference> pages = Helper::getPageSet(pageReferences);
    std::set<PDFObjectReference> nulledAnnotations;
    int removedCount = 0;

    for (const PDFObjectReference& pageReference : pageReferences)
    {
        if (isCanceled())
        {
            return;
        }
        removedCount += Helper::removePageAnnotations(&builder, pageReference, pages, filter, &nulledAnnotations);
    }

    if (removedCount == 0)
    {
        return;
    }

    // The removed annotations become null objects: an object, which still refers to them (the
    // structure tree, a reply, a link of a page shared by more pages), does not keep their text
    for (const PDFObjectReference& reference : nulledAnnotations)
    {
        builder.setObject(reference, PDFObject());
    }

    m_storage = *builder.getStorage();
    Q_EMIT sanitizationProgress(message.arg(removedCount));
}

}   // namespace pdf

#include "moc_pdfdocumentsanitizer.cpp"
