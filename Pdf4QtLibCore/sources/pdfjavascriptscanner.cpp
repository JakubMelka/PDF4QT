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

#include "pdfjavascriptscanner.h"
#include "pdfencoding.h"
#include "pdfnametreeloader.h"
#include "pdfobjectutils.h"
#include "pdfoperationcontrol.h"
#include "pdfstreamfilters.h"

#include <QXmlStreamReader>

#include <algorithm>
#include <map>
#include <numeric>
#include <set>

#include "pdfdbgheap.h"

namespace pdf
{

/// One scan over the raw objects of a document (see PDFJavaScriptScanner): the walks and the result
class PDFJavaScriptScannerHelper
{
public:
    using Type = PDFJavaScriptEntry::Type;

    explicit PDFJavaScriptScannerHelper(const PDFDocument* document, PDFJavaScriptScanner::Options options, const PDFOperationControl* control) :
        m_document(document),
        m_storage(&document->getStorage()),
        m_options(options),
        m_control(control),
        m_actionWalk(PDFJavaScriptScanner::MAXIMUM_ACTION_DEPTH)
    {

    }

    /// The owner of the actions of a container
    class Owner
    {
    public:
        Type type = Type::Invalid;
        PDFInteger pageIndex = -1;
        QString name;
    };

    /// No more is scanned (the first script of FindFirstOnly, the limit of the entries, a cancellation)
    bool isDone() const
    {
        return m_result.isCanceled || m_result.isLimitReached || (m_options.testFlag(PDFJavaScriptScanner::FindFirstOnly) && !m_result.entries.empty());
    }

    /// The catalog dictionary (nullptr = none)
    const PDFDictionary* getCatalog() const
    {
        const PDFDictionary* trailer = m_document->getTrailerDictionary();
        return trailer ? m_storage->getDictionaryFromObject(trailer->get("Root")) : nullptr;
    }

    /// /OpenAction (an action, not a destination) and /AA of the catalog (12.3.3, Table 200)
    void scanCatalog(const PDFDictionary* catalog)
    {
        Owner owner;
        owner.type = Type::Document;
        const PDFObject& openAction = catalog->get("OpenAction");
        if (m_storage->getObject(openAction).isDictionary())
        {
            scanAction(openAction, owner, QStringLiteral("OpenAction"));
        }
        scanAdditionalActions(catalog->get("AA"), owner);
    }

    /// The name tree /Names /JavaScript (12.6.4.17)
    void scanNamed(const PDFDictionary* catalog)
    {
        const PDFDictionary* names = m_storage->getDictionaryFromObject(catalog->get("Names"));
        if (!names)
        {
            return;
        }

        const std::map<QByteArray, PDFObject> scripts = PDFNameTreeLoader<PDFObject>::parse(m_storage, names->get("JavaScript"), [](const PDFObjectStorage*, const PDFObject& object) { return object; });
        for (const auto& script : scripts)
        {
            if (isDone())
            {
                break;
            }

            Owner owner;
            owner.type = Type::Named;
            owner.name = sanitize(PDFEncoding::convertTextString(script.first));
            scanAction(script.second, owner, QString());
        }
    }

    /// The items of the outline (12.3.3, Table 151): depth first in the order of the outline
    void scanOutline(const PDFDictionary* catalog)
    {
        const PDFDictionary* outlines = m_storage->getDictionaryFromObject(catalog->get("Outlines"));
        if (!outlines)
        {
            return;
        }

        PDFObjectWalk walk;
        std::vector<std::pair<PDFObject, int>> stack;
        stack.emplace_back(outlines->get("First"), 1);
        while (!stack.empty() && !isDone())
        {
            const std::pair<PDFObject, int> item = stack.back();
            stack.pop_back();
            if (isCanceled() || item.first.isNull() || !walk.enter(item.first, item.second))
            {
                m_result.isLimitReached = m_result.isLimitReached || walk.isLimitReached();
                continue;
            }

            const PDFDictionary* dictionary = m_storage->getDictionaryFromObject(item.first);
            if (!dictionary)
            {
                continue;
            }

            Owner owner;
            owner.type = Type::Outline;
            owner.name = getText(dictionary->get("Title"));
            scanAction(dictionary->get("A"), owner, QStringLiteral("A"));

            // The next sibling after the children of the item
            stack.emplace_back(dictionary->get("Next"), item.second);
            stack.emplace_back(dictionary->get("First"), item.second + 1);
        }
    }

    /// The fields of the interactive form (12.7.4): /A of a widget merged with its field, /AA (Table 199)
    void scanForm(const PDFDictionary* catalog)
    {
        const PDFDictionary* form = m_storage->getDictionaryFromObject(catalog->get("AcroForm"));
        const PDFObject& fields = form ? m_storage->getObject(form->get("Fields")) : PDFObject();
        if (!fields.isArray())
        {
            return;
        }

        class Field
        {
        public:
            PDFObject object;
            int depth = 0;
            QString parentName;
        };

        PDFObjectWalk walk;
        std::vector<Field> stack;
        pushFields(fields.getArray(), 0, QString(), stack);
        while (!stack.empty() && !isDone())
        {
            const Field field = stack.back();
            stack.pop_back();
            if (isCanceled() || !walk.enter(field.object, field.depth))
            {
                m_result.isLimitReached = m_result.isLimitReached || walk.isLimitReached();
                continue;
            }

            const PDFDictionary* dictionary = m_storage->getDictionaryFromObject(field.object);
            if (!dictionary)
            {
                continue;
            }

            // The fully qualified name (12.7.4.2): the partial names joined by periods
            const QString partialName = getText(dictionary->get("T"));
            Owner owner;
            owner.type = Type::Form;
            owner.name = field.parentName.isEmpty() ? partialName : (partialName.isEmpty() ? field.parentName : sanitize(field.parentName + QLatin1Char('.') + partialName));
            scanAction(dictionary->get("A"), owner, QStringLiteral("A"));
            scanAdditionalActions(dictionary->get("AA"), owner);

            const PDFObject& kids = m_storage->getObject(dictionary->get("Kids"));
            if (kids.isArray())
            {
                pushFields(kids.getArray(), field.depth + 1, owner.name, stack);
            }
        }
    }

    /// /AA of the pages (Table 198) and /A, /AA of their annotations (Table 166, Table 197)
    void scanPages(const std::vector<PDFInteger>& pageIndices)
    {
        const PDFCatalog* catalog = m_document->getCatalog();
        PDFObjectWalk annotationWalk;
        for (const PDFInteger pageIndex : pageIndices)
        {
            if (isDone() || isCanceled())
            {
                break;
            }
            if (pageIndex < 0 || pageIndex >= PDFInteger(catalog->getPageCount()))
            {
                continue;
            }

            const PDFPage* page = catalog->getPage(pageIndex);
            const PDFDictionary* pageDictionary = page->getPageReference().isValid() ? m_storage->getDictionaryFromObject(PDFObject::createReference(page->getPageReference())) : nullptr;
            if (!pageDictionary)
            {
                continue;
            }

            Owner pageOwner;
            pageOwner.type = Type::Page;
            pageOwner.pageIndex = pageIndex;
            scanAdditionalActions(pageDictionary->get("AA"), pageOwner);

            const PDFObject& annotations = m_storage->getObject(pageDictionary->get("Annots"));
            if (!annotations.isArray())
            {
                continue;
            }

            const PDFArray* array = annotations.getArray();
            for (size_t i = 0; i < array->getCount() && !isDone(); ++i)
            {
                // An annotation listed on two pages is read once (on the first of them)
                const PDFObject& annotation = array->getItem(i);
                if (isCanceled() || !annotationWalk.enter(annotation, 1))
                {
                    m_result.isLimitReached = m_result.isLimitReached || annotationWalk.isLimitReached();
                    continue;
                }

                const PDFDictionary* dictionary = m_storage->getDictionaryFromObject(annotation);
                if (!dictionary)
                {
                    continue;
                }

                Owner owner;
                owner.type = Type::Annotation;
                owner.pageIndex = pageIndex;
                owner.name = getText(dictionary->get("Subtype"));
                scanAction(dictionary->get("A"), owner, QStringLiteral("A"));
                scanAdditionalActions(dictionary->get("AA"), owner);
            }
        }
    }

    /// The scripts of the XFA form (<script> of the template, 12.7.9): read as a text only
    void scanXfa(const PDFDictionary* catalog)
    {
        const PDFDictionary* form = m_storage->getDictionaryFromObject(catalog->get("AcroForm"));
        const PDFObject& xfa = form ? m_storage->getObject(form->get("XFA")) : PDFObject();
        if (!xfa.isStream() && !xfa.isArray())
        {
            return;
        }
        m_result.hasXfa = true;

        // The packets in their order: one stream, or an array of names and streams
        std::vector<const PDFStream*> streams;
        if (xfa.isStream())
        {
            streams.push_back(xfa.getStream());
        }
        else
        {
            const PDFArray* array = xfa.getArray();
            for (size_t i = 1; i < array->getCount(); i += 2)
            {
                const PDFObject& packet = m_storage->getObject(array->getItem(i));
                if (packet.isStream())
                {
                    streams.push_back(packet.getStream());
                }
            }
        }

        QByteArray data;
        for (const PDFStream* stream : streams)
        {
            QString errorMessage;
            const QByteArray packet = decode(stream, MAXIMUM_XFA_SIZE - data.size(), &errorMessage);
            if (!errorMessage.isEmpty())
            {
                // A packet, which cannot be read: its scripts are not known
                m_result.isLimitReached = true;
                continue;
            }
            data += packet;
        }

        class Element
        {
        public:
            QString name;
            QString activity;
        };

        std::vector<Element> elements;
        QXmlStreamReader reader(data);
        while (!reader.atEnd() && !isDone())
        {
            switch (reader.readNext())
            {
                case QXmlStreamReader::DTD:
                    // No entities of a document type definition are expanded
                    return;

                case QXmlStreamReader::StartElement:
                {
                    if (reader.name() == QLatin1String("script"))
                    {
                        Element owner;
                        for (auto it = elements.crbegin(); it != elements.crend(); ++it)
                        {
                            owner.activity = owner.activity.isEmpty() ? it->activity : owner.activity;
                            if (owner.name.isEmpty() && !it->name.isEmpty())
                            {
                                owner.name = it->name;
                            }
                        }

                        PDFJavaScriptEntry entry(Type::Xfa, -1, reader.readElementText(QXmlStreamReader::IncludeChildElements));
                        entry.name = owner.name;
                        entry.trigger = owner.activity;
                        addEntry(std::move(entry));
                        break;
                    }

                    Element element;
                    element.name = sanitize(reader.attributes().value(QLatin1String("name")).toString());
                    element.activity = sanitize(reader.attributes().value(QLatin1String("activity")).toString());
                    elements.push_back(std::move(element));
                    break;
                }

                case QXmlStreamReader::EndElement:
                    if (!elements.empty())
                    {
                        elements.pop_back();
                    }
                    break;

                default:
                    break;
            }
        }
    }

    PDFJavaScriptScanner::Result takeResult()
    {
        return std::move(m_result);
    }

private:
    /// Most bytes of the packets of the XFA form, which are read
    static constexpr qint64 MAXIMUM_XFA_SIZE = PDFJavaScriptScanner::MAXIMUM_STREAM_SIZE;

    /// Longest text of a name of an owner
    static constexpr int MAXIMUM_NAME_LENGTH = 256;

    /// Most characters of the texts of all scripts of a scan (16 scripts of the longest length)
    static constexpr qint64 MAXIMUM_TOTAL_LENGTH = qint64(16) * PDFJavaScriptScanner::MAXIMUM_SCRIPT_LENGTH;

    /// The text of a script stream, decoded once
    class DecodedScript
    {
    public:
        QString text;
        bool isTruncated = false;
    };

    template<typename Stack>
    static void pushFields(const PDFArray* fields, int depth, const QString& parentName, Stack& stack)
    {
        // Reversed: the first field is taken first from the stack
        for (size_t i = fields->getCount(); i > 0; --i)
        {
            typename Stack::value_type field;
            field.object = fields->getItem(i - 1);
            field.depth = depth;
            field.parentName = parentName;
            stack.push_back(std::move(field));
        }
    }

    bool isCanceled()
    {
        if (!m_result.isCanceled && (++m_steps % 256) == 0)
        {
            m_result.isCanceled = PDFOperationControl::isOperationCanceled(m_control);
        }
        return m_result.isCanceled;
    }

    /// Every action of an additional-actions dictionary (Table 197 of an annotation, 198 of a page, 199 of a field, 200 of the catalog)
    void scanAdditionalActions(const PDFObject& additionalActions, const Owner& owner)
    {
        const PDFDictionary* dictionary = m_storage->getDictionaryFromObject(additionalActions);
        if (!dictionary)
        {
            return;
        }

        for (size_t i = 0; i < dictionary->getCount() && !isDone(); ++i)
        {
            scanAction(dictionary->getValue(i), owner, sanitize(QString::fromLatin1(dictionary->getKey(i).getString())));
        }
    }

    /// The action and its chain /Next (12.6.2, Table 196): iterative, one visited set of the whole scan
    void scanAction(const PDFObject& action, const Owner& owner, const QString& trigger)
    {
        std::vector<std::pair<PDFObject, int>> stack;
        stack.emplace_back(action, 0);
        while (!stack.empty() && !isDone())
        {
            const std::pair<PDFObject, int> item = stack.back();
            stack.pop_back();
            if (isCanceled() || item.first.isNull() || !m_actionWalk.enter(item.first, item.second))
            {
                m_result.isLimitReached = m_result.isLimitReached || m_actionWalk.isLimitReached();
                continue;
            }

            const PDFDictionary* dictionary = m_storage->getDictionaryFromObject(item.first);
            if (!dictionary)
            {
                continue;
            }

            // A JavaScript action (12.6.4.17, Table 221) and the script of a Rendition action (Table 218)
            const PDFObject& subtype = m_storage->getObject(dictionary->get("S"));
            if (subtype.isName() && (subtype.getString() == "JavaScript" || subtype.getString() == "Rendition"))
            {
                const PDFObject& script = m_storage->getObject(dictionary->get("JS"));
                if (script.isString() || script.isStream())
                {
                    PDFJavaScriptEntry entry(owner.type, owner.pageIndex, QString());
                    entry.name = owner.name;
                    entry.trigger = trigger;
                    entry.actionReference = item.first.isReference() ? item.first.getReference() : PDFObjectReference();
                    if (script.isString())
                    {
                        entry.javaScript = PDFEncoding::convertTextString(script.getString());
                    }
                    else
                    {
                        // A stream of more actions is decoded once (its text is shared)
                        const PDFObject& scriptObject = dictionary->get("JS");
                        const PDFObjectReference scriptReference = scriptObject.isReference() ? scriptObject.getReference() : PDFObjectReference();
                        auto it = scriptReference.isValid() ? m_decodedScripts.find(scriptReference) : m_decodedScripts.end();
                        if (it == m_decodedScripts.end())
                        {
                            DecodedScript decoded;
                            QString errorMessage;
                            if (m_totalLength < MAXIMUM_TOTAL_LENGTH)
                            {
                                decoded.text = PDFEncoding::convertTextString(decode(script.getStream(), PDFJavaScriptScanner::MAXIMUM_STREAM_SIZE, &errorMessage));
                                if (decoded.text.size() > PDFJavaScriptScanner::MAXIMUM_SCRIPT_LENGTH)
                                {
                                    // A new string of the kept length: a truncated one keeps its whole allocation
                                    decoded.text = decoded.text.left(PDFJavaScriptScanner::MAXIMUM_SCRIPT_LENGTH);
                                    decoded.isTruncated = true;
                                }
                            }

                            // The script exists, but it cannot be read (or the scripts read before have their
                            // limit): it is reported without its text
                            decoded.isTruncated = decoded.isTruncated || !errorMessage.isEmpty() || m_totalLength >= MAXIMUM_TOTAL_LENGTH;
                            if (scriptReference.isValid())
                            {
                                m_decodedScripts.emplace(scriptReference, decoded);
                            }
                            entry.javaScript = decoded.text;
                            entry.isTruncated = decoded.isTruncated;
                        }
                        else
                        {
                            entry.javaScript = it->second.text;
                            entry.isTruncated = it->second.isTruncated;
                        }
                    }
                    addEntry(std::move(entry));
                }
            }

            // /Next: a dictionary or an array of them, performed in their order
            const PDFObject& next = dictionary->get("Next");
            const PDFObject& nextObject = m_storage->getObject(next);
            if (nextObject.isArray())
            {
                const PDFArray* array = nextObject.getArray();
                for (size_t i = array->getCount(); i > 0; --i)
                {
                    stack.emplace_back(array->getItem(i - 1), item.second + 1);
                }
            }
            else if (nextObject.isDictionary())
            {
                stack.emplace_back(next, item.second + 1);
            }
        }
    }

    void addEntry(PDFJavaScriptEntry entry)
    {
        if (int(m_result.entries.size()) >= PDFJavaScriptScanner::MAXIMUM_ENTRIES)
        {
            m_result.isLimitReached = true;
            return;
        }

        // Review RS7CORE: the texts of all scripts have one budget (100 000 actions of one script of
        // 1 Mi characters were 200 GB); a script over it keeps the rest of the budget, the scripts after
        // it are still listed (a script is never hidden), truncated to no text. A new string of the kept
        // length (truncate keeps the allocation).
        const qint64 length = qMin(qint64(PDFJavaScriptScanner::MAXIMUM_SCRIPT_LENGTH), MAXIMUM_TOTAL_LENGTH - m_totalLength);
        if (entry.javaScript.size() > length)
        {
            entry.javaScript = entry.javaScript.left(qsizetype(qMax(qint64(0), length)));
            entry.isTruncated = true;
        }
        m_totalLength += entry.javaScript.size();
        m_result.entries.push_back(std::move(entry));
    }

    /// The decoded data of a stream with the limit (an empty array and the reason on a failure)
    QByteArray decode(const PDFStream* stream, qint64 maximumSize, QString* errorMessage) const
    {
        if (maximumSize <= 0)
        {
            *errorMessage = PDFTranslationContext::tr("The data are too large.");
            return QByteArray();
        }

        const PDFLimitedStreamDecoder::Result result = PDFLimitedStreamDecoder::decode(stream, [this](const PDFObject& object) -> const PDFObject& { return m_storage->getObject(object); },
                                                                                        m_storage->getSecurityHandler(), maximumSize, m_control);
        if (result.status != PDFLimitedStreamDecoder::Status::Decoded)
        {
            *errorMessage = result.errorMessage.isEmpty() ? PDFTranslationContext::tr("The stream cannot be decoded.") : result.errorMessage;
            return QByteArray();
        }
        return result.data;
    }

    /// A text of a name of an owner (a text string or a name), bounded and without control characters
    QString getText(const PDFObject& object) const
    {
        const PDFObject& value = m_storage->getObject(object);
        if (value.isString())
        {
            return sanitize(PDFEncoding::convertTextString(value.getString()));
        }
        if (value.isName())
        {
            return sanitize(QString::fromLatin1(value.getString()));
        }
        return QString();
    }

    static QString sanitize(const QString& text)
    {
        QString result = text.left(MAXIMUM_NAME_LENGTH);
        for (QChar& character : result)
        {
            if (character.category() == QChar::Other_Control)
            {
                character = QLatin1Char(' ');
            }
        }
        return result;
    }

    const PDFDocument* m_document;
    const PDFObjectStorage* m_storage;
    PDFJavaScriptScanner::Options m_options;
    const PDFOperationControl* m_control;
    PDFObjectWalk m_actionWalk;
    PDFJavaScriptScanner::Result m_result;
    qint64 m_steps = 0;
    qint64 m_totalLength = 0;                                       ///< Characters of the texts of the entries
    std::map<PDFObjectReference, DecodedScript> m_decodedScripts;   ///< The script streams decoded already
};

PDFJavaScriptScanner::PDFJavaScriptScanner(const PDFDocument* document) :
    m_document(document)
{

}

PDFJavaScriptScanner::Entries PDFJavaScriptScanner::scan(const std::vector<PDFInteger>& pages, Options options) const
{
    return scanDocument(pages, options, nullptr).entries;
}

PDFJavaScriptScanner::Result PDFJavaScriptScanner::scanDocument(const std::vector<PDFInteger>& pages, Options options, const PDFOperationControl* control) const
{
    if (!m_document)
    {
        return Result();
    }

    PDFJavaScriptScannerHelper helper(m_document, options, control);
    if (const PDFDictionary* catalog = helper.getCatalog())
    {
        if (options.testFlag(ScanDocument) && !helper.isDone())
        {
            helper.scanCatalog(catalog);
        }
        if (options.testFlag(ScanNamed) && !helper.isDone())
        {
            helper.scanNamed(catalog);
        }
        if (options.testFlag(ScanOutline) && !helper.isDone())
        {
            helper.scanOutline(catalog);
        }
        if (options.testFlag(ScanPage) && !helper.isDone())
        {
            std::vector<PDFInteger> scannedPages = pages;
            if (options.testFlag(AllPages))
            {
                scannedPages.resize(m_document->getCatalog()->getPageCount(), 0);
                std::iota(scannedPages.begin(), scannedPages.end(), 0);
            }
            helper.scanPages(scannedPages);
        }
        if (options.testFlag(ScanForm) && !helper.isDone())
        {
            helper.scanForm(catalog);
        }
        if (options.testFlag(ScanXfa) && !helper.isDone())
        {
            helper.scanXfa(catalog);
        }
    }

    Result result = helper.takeResult();

    // By the type and the page; the scripts of one owner stay in the order of the document (a chain /Next)
    std::stable_sort(result.entries.begin(), result.entries.end(), [](const PDFJavaScriptEntry& left, const PDFJavaScriptEntry& right) { return std::tie(left.type, left.pageIndex) < std::tie(right.type, right.pageIndex); });
    if (options.testFlag(Optimize))
    {
        std::set<PDFJavaScriptEntry> seen;
        Entries unique;
        for (PDFJavaScriptEntry& entry : result.entries)
        {
            if (seen.insert(entry).second)
            {
                unique.push_back(std::move(entry));
            }
        }
        result.entries = std::move(unique);
    }
    return result;
}

bool PDFJavaScriptScanner::hasJavaScript() const
{
    return !scan({ }, Options(AllPages | FindFirstOnly | ScanMask)).empty();
}

}   // namespace pdf
