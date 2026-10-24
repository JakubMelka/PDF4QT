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

#include "pdfappannotationoperations.h"

#include "pdfannotationmanipulator.h"
#include "pdfannotationproperties.h"
#include "pdfdocumentbuilder.h"

#include <set>

namespace pdf
{

/// Checks of the editing operations (every argument is untrusted)
class PDFAppAnnotationOperationsEditHelper
{
public:
    PDFAppAnnotationOperationsEditHelper() = delete;

    /// Most annotations of one operation and most target pages of a copy
    static constexpr size_t MAXIMUM_ANNOTATIONS = 100000;
    static constexpr size_t MAXIMUM_PAGES = 100000;

    /// Dictionary of the annotation (nullptr, when the reference is not an
    /// annotation of the document - a type, which is not an annotation, is refused)
    static const PDFDictionary* getAnnotationDictionary(const PDFDocument* document, PDFObjectReference annotation)
    {
        if (!document || !annotation.isValid())
        {
            return nullptr;
        }

        const PDFObjectStorage& storage = document->getStorage();
        const PDFDictionary* dictionary = storage.getDictionaryFromObject(storage.getObjectByReference(annotation));
        if (!dictionary || !storage.getObject(dictionary->get("Subtype")).isName())
        {
            return nullptr;
        }
        return dictionary;
    }

    /// Flags (F) of the annotation
    static PDFAnnotation::Flags getFlags(const PDFDocument* document, const PDFDictionary* dictionary)
    {
        PDFDocumentDataLoaderDecorator loader(document);
        return PDFAnnotation::Flags(static_cast<uint>(loader.readIntegerFromDictionary(dictionary, "F", 0) & 0x3FF));
    }

    /// The annotation can be changed (it exists, it lies on a page and it is not
    /// Locked / ReadOnly); \p page gets its page
    static bool canModify(const PDFDocument* document, PDFObjectReference annotation, PDFObjectReference* page)
    {
        const PDFDictionary* dictionary = getAnnotationDictionary(document, annotation);
        if (!dictionary)
        {
            return false;
        }

        const PDFAnnotation::Flags flags = getFlags(document, dictionary);
        if (flags.testFlag(PDFAnnotation::Locked) || flags.testFlag(PDFAnnotation::ReadOnly))
        {
            return false;
        }

        const PDFObjectReference annotationPage = PDFAnnotationManipulator::findAnnotationPage(&document->getStorage(), annotation);
        if (page)
        {
            *page = annotationPage;
        }
        return annotationPage.isValid();
    }

    /// Text written into an annotation: bounded, control characters other than
    /// the line breaks and the tabulator removed
    static QString sanitizeText(const QString& text)
    {
        QString result;
        const QString bounded = text.left(PDFAppAnnotationOperations::MAXIMUM_TEXT_LENGTH);
        result.reserve(bounded.size());
        for (const QChar character : bounded)
        {
            if (character == QChar(u'\n') || character == QChar(u'\t'))
            {
                result.append(character);
            }
            else if (character == QChar(u'\r'))
            {
                continue;
            }
            else if (character.category() != QChar::Other_Control)
            {
                result.append(character);
            }
        }
        return result;
    }

    /// Finishes a modification: the changed document, or nullopt
    static PDFAppAnnotationResult finish(PDFDocumentModifier& modifier, bool isModified)
    {
        if (!isModified || !modifier.finalize() || !modifier.getDocument())
        {
            return std::nullopt;
        }
        return PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags());
    }

    /// Unique references in their order (at most MAXIMUM_ANNOTATIONS)
    static std::vector<PDFObjectReference> unique(const std::vector<PDFObjectReference>& references)
    {
        std::vector<PDFObjectReference> result;
        std::set<PDFObjectReference> seen;
        for (const PDFObjectReference& reference : references)
        {
            if (result.size() >= MAXIMUM_ANNOTATIONS)
            {
                break;
            }
            if (reference.isValid() && seen.insert(reference).second)
            {
                result.push_back(reference);
            }
        }
        return result;
    }
};

bool PDFAppAnnotationOperations::canModifyAnnotation(const PDFDocumentPointer& document, PDFObjectReference annotation)
{
    return PDFAppAnnotationOperationsEditHelper::canModify(document.data(), annotation, nullptr);
}

bool PDFAppAnnotationOperations::canModifyContents(const PDFDocumentPointer& document, PDFObjectReference annotation)
{
    using H = PDFAppAnnotationOperationsEditHelper;

    const PDFDictionary* dictionary = H::getAnnotationDictionary(document.data(), annotation);
    if (!dictionary)
    {
        return false;
    }

    // Locked contents forbid the change of the text, not of the geometry
    const PDFAnnotation::Flags flags = H::getFlags(document.data(), dictionary);
    return !flags.testFlag(PDFAnnotation::LockedContents) && !flags.testFlag(PDFAnnotation::ReadOnly) &&
           PDFAnnotationManipulator::findAnnotationPage(&document->getStorage(), annotation).isValid();
}

PDFAppAnnotationResult PDFAppAnnotationOperations::deleteAnnotations(const PDFDocumentPointer& document, const std::vector<PDFObjectReference>& annotations)
{
    using H = PDFAppAnnotationOperationsEditHelper;

    if (!document)
    {
        return std::nullopt;
    }

    PDFDocumentModifier modifier(document.data());
    modifier.markAnnotationsChanged();

    // The popups and the replies of an annotation go with it (removeAnnotation)
    bool isModified = false;
    for (const PDFObjectReference& annotation : H::unique(annotations))
    {
        PDFObjectReference page;
        if (!H::canModify(document.data(), annotation, &page))
        {
            continue;
        }

        // An annotation removed with its owner earlier in the list
        if (!modifier.getBuilder()->getStorage()->getObjectByReference(annotation).isDictionary() ||
            !PDFAnnotationManipulator::findAnnotationPage(modifier.getBuilder()->getStorage(), annotation).isValid())
        {
            continue;
        }

        modifier.getBuilder()->removeAnnotation(page, annotation);
        isModified = true;
    }

    return H::finish(modifier, isModified);
}

PDFAppAnnotationResult PDFAppAnnotationOperations::setAnnotationsHidden(const PDFDocumentPointer& document, const std::vector<PDFObjectReference>& annotations, bool hidden)
{
    using H = PDFAppAnnotationOperationsEditHelper;

    if (!document)
    {
        return std::nullopt;
    }

    PDFDocumentModifier modifier(document.data());
    modifier.markAnnotationsChanged();

    bool isModified = false;
    for (const PDFObjectReference& annotation : H::unique(annotations))
    {
        // Hidden is a property of the annotation: a Locked (ISO 32000-2 Table 167)
        // or a read-only annotation stays as it is (as PDFWidgetAnnotationManager)
        const PDFDictionary* dictionary = H::getAnnotationDictionary(document.data(), annotation);
        if (!dictionary || !H::canModify(document.data(), annotation, nullptr))
        {
            continue;
        }

        PDFAnnotation::Flags flags = H::getFlags(document.data(), dictionary);
        if (flags.testFlag(PDFAnnotation::Hidden) == hidden)
        {
            continue;
        }

        flags.setFlag(PDFAnnotation::Hidden, hidden);
        PDFObjectFactory factory;
        factory.beginDictionary();
        factory.beginDictionaryItem("F");
        factory << PDFInteger(flags.toInt());
        factory.endDictionaryItem();
        factory.endDictionary();
        modifier.getBuilder()->mergeTo(annotation, factory.takeObject());
        isModified = true;
    }

    return H::finish(modifier, isModified);
}

PDFAppAnnotationResult PDFAppAnnotationOperations::setContents(const PDFDocumentPointer& document, PDFObjectReference annotation, const QString& contents)
{
    using H = PDFAppAnnotationOperationsEditHelper;

    if (!canModifyContents(document, annotation))
    {
        return std::nullopt;
    }

    const QString text = H::sanitizeText(contents);
    PDFDocumentDataLoaderDecorator loader(document.data());
    const PDFDictionary* dictionary = H::getAnnotationDictionary(document.data(), annotation);
    if (loader.readTextStringFromDictionary(dictionary, "Contents", QString()) == text)
    {
        return std::nullopt;
    }

    PDFDocumentModifier modifier(document.data());
    modifier.markAnnotationsChanged();

    PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("Contents");
    factory << text;
    factory.endDictionaryItem();
    factory.beginDictionaryItem("M");
    factory << WrapCurrentDateTime();
    factory.endDictionaryItem();
    factory.endDictionary();
    modifier.getBuilder()->mergeTo(annotation, factory.takeObject());

    // A text box draws its contents
    modifier.getBuilder()->updateAnnotationAppearanceStreams(annotation);
    return H::finish(modifier, true);
}

PDFAppAnnotationResult PDFAppAnnotationOperations::addReply(const PDFDocumentPointer& document, PDFObjectReference annotation, const QString& contents, const QString& author, PDFObjectReference* created)
{
    using H = PDFAppAnnotationOperationsEditHelper;

    if (created)
    {
        *created = PDFObjectReference();
    }

    const QString text = H::sanitizeText(contents);
    const PDFDictionary* dictionary = H::getAnnotationDictionary(document.data(), annotation);
    if (!dictionary || text.trimmed().isEmpty() || H::getFlags(document.data(), dictionary).testFlag(PDFAnnotation::ReadOnly))
    {
        return std::nullopt;
    }

    PDFDocumentModifier modifier(document.data());
    modifier.markAnnotationsChanged();

    QString authorName = H::sanitizeText(author).left(256);
    authorName.remove(QChar(u'\n'));
    authorName.remove(QChar(u'\t'));
    const PDFObjectReference reply = PDFAnnotationManipulator::addReply(modifier.getBuilder(), annotation, authorName, text);
    if (!reply.isValid())
    {
        return std::nullopt;
    }

    PDFAppAnnotationResult result = H::finish(modifier, true);
    if (result && created)
    {
        *created = reply;
    }
    return result;
}

size_t PDFAppAnnotationOperations::getCopyToPagesCount(const PDFDocumentPointer& document, const std::vector<PDFObjectReference>& annotations, const std::vector<PDFInteger>& pageIndices)
{
    using H = PDFAppAnnotationOperationsEditHelper;

    if (!document || pageIndices.empty() || pageIndices.size() > H::MAXIMUM_PAGES)
    {
        return 0;
    }

    const PDFCatalog* catalog = document->getCatalog();
    const PDFInteger pageCount = PDFInteger(catalog->getPageCount());
    std::set<PDFInteger> pages;
    for (const PDFInteger pageIndex : pageIndices)
    {
        if (pageIndex >= 0 && pageIndex < pageCount)
        {
            pages.insert(pageIndex);
        }
    }

    // Each annotation to each page except its own one
    size_t count = 0;
    for (const PDFObjectReference& annotation : H::unique(annotations))
    {
        if (!H::getAnnotationDictionary(document.data(), annotation))
        {
            continue;
        }
        const PDFObjectReference sourcePage = PDFAnnotationManipulator::findAnnotationPage(&document->getStorage(), annotation);
        if (!sourcePage.isValid())
        {
            continue;
        }
        const size_t sourceIndex = catalog->getPageIndexFromPageReference(sourcePage);
        const bool isSourceTarget = sourceIndex < size_t(pageCount) && pages.count(PDFInteger(sourceIndex)) > 0;
        count += pages.size() - (isSourceTarget ? 1 : 0);
    }
    return count;
}

PDFAppAnnotationResult PDFAppAnnotationOperations::copyToPages(const PDFDocumentPointer& document, const std::vector<PDFObjectReference>& annotations, const std::vector<PDFInteger>& pageIndices)
{
    using H = PDFAppAnnotationOperationsEditHelper;

    if (!document || pageIndices.empty() || pageIndices.size() > H::MAXIMUM_PAGES)
    {
        return std::nullopt;
    }

    // Annotations x pages is bounded (a selection of all annotations copied to
    // thousands of pages would freeze the application and inflate the document)
    if (getCopyToPagesCount(document, annotations, pageIndices) > MAXIMUM_COPIES)
    {
        return std::nullopt;
    }

    const PDFCatalog* catalog = document->getCatalog();
    const PDFInteger pageCount = PDFInteger(catalog->getPageCount());
    std::set<PDFInteger> pages;
    for (const PDFInteger pageIndex : pageIndices)
    {
        if (pageIndex >= 0 && pageIndex < pageCount)
        {
            pages.insert(pageIndex);
        }
    }

    PDFDocumentModifier modifier(document.data());
    modifier.markAnnotationsChanged();

    // The copies keep the position, the size, the popup and the replies
    // (PDFAnnotationManipulator::copyAnnotation); the source page is skipped
    bool isModified = false;
    for (const PDFObjectReference& annotation : H::unique(annotations))
    {
        if (!H::getAnnotationDictionary(document.data(), annotation))
        {
            continue;
        }

        const PDFObjectReference sourcePage = PDFAnnotationManipulator::findAnnotationPage(&document->getStorage(), annotation);
        if (!sourcePage.isValid())
        {
            continue;
        }

        for (const PDFInteger pageIndex : pages)
        {
            const PDFPage* page = catalog->getPage(size_t(pageIndex));
            if (!page || page->getPageReference() == sourcePage || !page->getPageReference().isValid())
            {
                continue;
            }
            isModified = PDFAnnotationManipulator::copyAnnotation(modifier.getBuilder(), annotation, page->getPageReference()).isValid() || isModified;
        }
    }

    return H::finish(modifier, isModified);
}

PDFAppAnnotationResult PDFAppAnnotationOperations::setProperties(const PDFDocumentPointer& document,
                                                                 PDFObjectReference annotation,
                                                                 const PDFAnnotationProperties& properties,
                                                                 PDFAnnotationProperties::Fields fields)
{
    using H = PDFAppAnnotationOperationsEditHelper;
    using F = PDFAnnotationProperties;

    const PDFDictionary* dictionary = H::getAnnotationDictionary(document.data(), annotation);
    if (!dictionary || !PDFAnnotationManipulator::findAnnotationPage(&document->getStorage(), annotation).isValid())
    {
        return std::nullopt;
    }

    // ISO 32000-2 Table 167, as the other operations and the properties dialog:
    // a Locked (or ReadOnly) annotation changes only its text and its lock flags
    // (the other flags are kept), LockedContents (or ReadOnly) keeps the text
    const PDFAnnotation::Flags flags = H::getFlags(document.data(), dictionary);
    PDFAnnotationProperties allowedProperties = properties;
    if (flags.testFlag(PDFAnnotation::Locked) || flags.testFlag(PDFAnnotation::ReadOnly))
    {
        fields &= F::Fields(F::Contents | F::Flags);
        PDFAnnotation::Flags allowedFlags = flags;
        allowedFlags.setFlag(PDFAnnotation::Locked, properties.flags.testFlag(PDFAnnotation::Locked));
        allowedFlags.setFlag(PDFAnnotation::LockedContents, properties.flags.testFlag(PDFAnnotation::LockedContents));
        allowedProperties.flags = allowedFlags;
    }
    if (flags.testFlag(PDFAnnotation::LockedContents) || flags.testFlag(PDFAnnotation::ReadOnly))
    {
        fields &= ~F::Fields(F::Contents);
    }
    if (fields == F::None)
    {
        return std::nullopt;
    }

    PDFDocumentModifier modifier(document.data());
    modifier.markAnnotationsChanged();
    const bool isModified = PDFAnnotationPropertyEditor::write(modifier.getBuilder(), annotation, allowedProperties, fields);
    return PDFAppAnnotationOperationsEditHelper::finish(modifier, isModified);
}

PDFAppAnnotationResult PDFAppAnnotationOperations::setRectangle(const PDFDocumentPointer& document, PDFObjectReference annotation, const QRectF& rectangle)
{
    const QRectF normalized = rectangle.normalized();
    if (!PDFAppAnnotationOperationsEditHelper::canModify(document.data(), annotation, nullptr) || !normalized.isValid() ||
        !qIsFinite(normalized.left()) || !qIsFinite(normalized.top()) || !qIsFinite(normalized.width()) || !qIsFinite(normalized.height()) ||
        qMax(qAbs(normalized.left()), qAbs(normalized.top())) > MAXIMUM_COORDINATE || qMax(normalized.width(), normalized.height()) > MAXIMUM_COORDINATE)
    {
        return std::nullopt;
    }

    PDFDocumentModifier modifier(document.data());
    modifier.markAnnotationsChanged();
    const bool isModified = PDFAnnotationManipulator::setRectangle(modifier.getBuilder(), annotation, normalized);
    return PDFAppAnnotationOperationsEditHelper::finish(modifier, isModified);
}

PDFAppAnnotationResult PDFAppAnnotationOperations::transformAnnotation(const PDFDocumentPointer& document, PDFObjectReference annotation, const QTransform& transform)
{
    if (!PDFAppAnnotationOperationsEditHelper::canModify(document.data(), annotation, nullptr) || transform.isIdentity() || !transform.isInvertible() ||
        !qIsFinite(transform.m11()) || !qIsFinite(transform.m12()) || !qIsFinite(transform.m21()) || !qIsFinite(transform.m22()) ||
        !qIsFinite(transform.dx()) || !qIsFinite(transform.dy()))
    {
        return std::nullopt;
    }

    PDFDocumentModifier modifier(document.data());
    modifier.markAnnotationsChanged();
    const bool isModified = PDFAnnotationManipulator::transformAnnotation(modifier.getBuilder(), annotation, transform);
    return PDFAppAnnotationOperationsEditHelper::finish(modifier, isModified);
}

PDFAppAnnotationResult PDFAppAnnotationOperations::setEditablePoints(const PDFDocumentPointer& document, PDFObjectReference annotation, const std::vector<QPointF>& points)
{
    if (!PDFAppAnnotationOperationsEditHelper::canModify(document.data(), annotation, nullptr) || points.empty() || points.size() > 100000)
    {
        return std::nullopt;
    }

    for (const QPointF& point : points)
    {
        if (!qIsFinite(point.x()) || !qIsFinite(point.y()) || qMax(qAbs(point.x()), qAbs(point.y())) > MAXIMUM_COORDINATE)
        {
            return std::nullopt;
        }
    }

    PDFDocumentModifier modifier(document.data());
    modifier.markAnnotationsChanged();
    const bool isModified = PDFAnnotationManipulator::setEditablePoints(modifier.getBuilder(), annotation, points);
    return PDFAppAnnotationOperationsEditHelper::finish(modifier, isModified);
}

}   // namespace pdf
