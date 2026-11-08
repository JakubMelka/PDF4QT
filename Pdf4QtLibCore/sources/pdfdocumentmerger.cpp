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

#include "pdfdocumentmerger.h"

#include "pdfcatalog.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentpageoperations.h"
#include "pdfdocumentreader.h"
#include "pdfdocumenttool.h"
#include "pdfencoding.h"
#include "pdfexception.h"
#include "pdfimagepagefactory.h"
#include "pdfnametreeloader.h"
#include "pdfobjectutils.h"
#include "pdfoperationcontrol.h"
#include "pdfoptimizer.h"
#include "pdfprogress.h"
#include "pdfsecurityhandler.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFileInfo>
#include <QUrl>
#include <QUuid>

#include <cmath>
#include <limits>
#include <map>
#include <set>

namespace pdf
{

// ------------------------------------------------------- Source

PDFDocumentMerger::Source PDFDocumentMerger::Source::createDocument(PDFDocumentPointer document, const QString& name)
{
    Source source;
    source.kind = Kind::Document;
    source.document = std::move(document);
    source.name = name;
    return source;
}

PDFDocumentMerger::Source PDFDocumentMerger::Source::createImage(const QByteArray& data, const QByteArray& format, double dotsPerInch, const QString& name)
{
    Source source;
    source.kind = Kind::Image;
    source.imageData = data;
    source.imageFormat = format;
    source.dotsPerInch = dotsPerInch;
    source.name = name;
    return source;
}

PDFDocumentMerger::Source PDFDocumentMerger::Source::createImage(const QImage& image, double dotsPerInch, const QString& name)
{
    Source source;
    source.kind = Kind::Image;
    source.image = image;
    source.dotsPerInch = dotsPerInch;
    source.name = name;
    return source;
}

PDFDocumentMerger::Source PDFDocumentMerger::Source::createBlank()
{
    return Source();
}

// ------------------------------------------------------- Permissions

std::vector<PDFSecurityHandler::Permission> PDFDocumentMerger::getRequiredPermissions()
{
    // ISO 32000-2, 7.6.4.2, Table 22: bit 11 inserts the pages into another document. The result
    // is not encrypted, so every other restriction would be lost (owner answer Q2, variant c);
    // bit 10 is deprecated in PDF 2.0 and restricts nothing.
    using P = PDFSecurityHandler::Permission;
    return { P::Assemble, P::PrintLowResolution, P::Modify, P::CopyContent, P::ModifyInteractiveItems, P::ModifyFormFields, P::PrintHighResolution };
}

PDFDocumentMerger::Permission PDFDocumentMerger::getPermission(const PDFDocument* document)
{
    const PDFSecurityHandler* securityHandler = document ? document->getStorage().getSecurityHandler() : nullptr;
    if (!securityHandler)
    {
        return Permission::Permitted;
    }

    // An owner password grants every bit (PDFSecurityHandler::isAllowed); a handler of revision
    // 2 is read by its table (Table 22: the assembly by bit 4, SECURITY_TODO 31)
    const std::vector<PDFSecurityHandler::Permission> required = getRequiredPermissions();
    if (!PDFSecurityHandler::isAllowedByRevision(securityHandler, required.front()))
    {
        return Permission::AssembleDenied;
    }
    for (const PDFSecurityHandler::Permission permission : required)
    {
        if (!PDFSecurityHandler::isAllowedByRevision(securityHandler, permission))
        {
            return Permission::OwnerPasswordRequired;
        }
    }
    return Permission::Permitted;
}

PDFDocumentMerger::Permission PDFDocumentMerger::getPermission(const PDFDocument* document, bool isOwnerPasswordVerified)
{
    // Owner answer Q15: the owner password, which was verified against the original file of a
    // snapshot, grants everything as the owner password of a read file does (7.6.4.2)
    return isOwnerPasswordVerified ? Permission::Permitted : getPermission(document);
}

bool PDFDocumentMerger::verifyOwnerPassword(const PDFSecurityHandler* securityHandler, const QString& ownerPassword)
{
    if (!securityHandler || securityHandler->getMode() == EncryptionMode::None)
    {
        return true;
    }

    // A copy authenticates (the handler of the document stays as it is): only the owner password
    // is tried, an empty user password does not count (7.6.4.4)
    std::unique_ptr<PDFSecurityHandler> copy(securityHandler->clone());
    bool isFirstAttempt = true;
    auto callback = [&ownerPassword, &isFirstAttempt](bool* ok) -> QString
    {
        *ok = isFirstAttempt;
        isFirstAttempt = false;
        return ownerPassword;
    };

    try
    {
        return copy->authenticate(callback, true) == PDFSecurityHandler::AuthorizationResult::OwnerAuthorized;
    }
    catch (const PDFException&)
    {
        // Revision 5 and 6 check /Perms by the key of the owner (Algorithm 2.A, step f): a file,
        // whose /OE does not give the key of its /UE, is not unlocked - the caller (a worker of
        // the application) never gets the exception
        return false;
    }
}

bool PDFDocumentMerger::verifyOwnerPassword(const QByteArray& originalData, const QString& ownerPassword)
{
    bool isFirstAttempt = true;
    auto callback = [&ownerPassword, &isFirstAttempt](bool* ok) -> QString
    {
        *ok = isFirstAttempt;
        isFirstAttempt = false;
        return ownerPassword;
    };
    PDFDocumentReader reader(nullptr, callback, true, true);
    const PDFDocument document = reader.readFromBuffer(originalData);
    if (reader.getReadingResult() != PDFDocumentReader::Result::OK)
    {
        return false;
    }
    const PDFSecurityHandler* securityHandler = document.getStorage().getSecurityHandler();
    return !securityHandler || securityHandler->getMode() == EncryptionMode::None ||
           securityHandler->getAuthorizationResult() == PDFSecurityHandler::AuthorizationResult::OwnerAuthorized;
}

QString PDFDocumentMerger::getPermissionText(Permission permission, const QString& name)
{
    const QString file = name.isEmpty() ? tr("The document") : name;
    switch (permission)
    {
        case Permission::Permitted:
            return QString();
        case Permission::AssembleDenied:
            return tr("%1 does not allow its pages to be assembled into another document. If you own it, unlock it with its owner password.").arg(file);
        case Permission::OwnerPasswordRequired:
            return tr("%1 restricts printing, copying or changes, and the merged document would not keep the restrictions. If you own it, unlock it with its owner password.").arg(file);
    }
    return QString();
}

// ------------------------------------------------------- Helpers

/// Helpers of the merge (raw objects, no state)
class PDFDocumentMergerHelper
{
public:
    PDFDocumentMergerHelper() = delete;

    /// Nesting of the direct objects of one object (the parser has at most 256 levels)
    static constexpr int MAXIMUM_OBJECT_DEPTH = 512;

    /// Depth of the trees, which are walked (outline, fields, structure)
    static constexpr int MAXIMUM_TREE_DEPTH = 256;

    /// Longest PDF name (PDF Reference 1.7, Table C.1)
    static constexpr int MAXIMUM_NAME_LENGTH = 127;

    static PDFInplaceOrMemoryString key(const char* text)
    {
        return PDFInplaceOrMemoryString(text);
    }

    /// The trailer dictionary (of a cross-reference stream too, ISO 32000-2, 7.5.8.2)
    static const PDFDictionary* getTrailer(const PDFObjectStorage* storage)
    {
        return storage->getDictionaryFromObject(storage->getTrailerDictionary());
    }

    /// The name of an object, empty for another type
    static QByteArray getName(const PDFObject& object)
    {
        return object.isName() ? object.getString() : QByteArray();
    }

    /// A text string as a QString (an empty one for another type)
    static QString getText(const PDFObject& object)
    {
        return object.isString() ? PDFEncoding::convertTextString(object.getString()) : QString();
    }

    /// Array of objects (an array or one object, which is not null)
    static std::vector<PDFObject> getItems(const PDFObjectStorage* storage, const PDFObject& object)
    {
        std::vector<PDFObject> items;
        const PDFObject& dereferenced = storage->getObject(object);
        if (dereferenced.isArray())
        {
            const PDFArray* array = dereferenced.getArray();
            items.reserve(array->getCount());
            for (size_t i = 0; i < array->getCount(); ++i)
            {
                items.push_back(array->getItem(i));
            }
        }
        else if (!dereferenced.isNull())
        {
            items.push_back(object);
        }
        return items;
    }

    /// The references of the items (the other items are left out)
    static std::set<PDFObjectReference> getReferences(const std::vector<PDFObject>& items)
    {
        std::set<PDFObjectReference> references;
        for (const PDFObject& item : items)
        {
            if (item.isReference())
            {
                references.insert(item.getReference());
            }
        }
        return references;
    }

    static PDFObject createArray(const std::vector<PDFObject>& items)
    {
        PDFArrayBuilder array;
        array.reserve(items.size());
        for (const PDFObject& item : items)
        {
            array.appendItem(item);
        }
        return PDFObject::createArray(std::move(array));
    }

    static PDFObject createRectangle(const QRectF& rectangle)
    {
        return createArray({ PDFObject::createReal(rectangle.left()), PDFObject::createReal(rectangle.top()), PDFObject::createReal(rectangle.right()), PDFObject::createReal(rectangle.bottom()) });
    }

    /// A key, which is not used yet: "key", else "key_2", "key_3", ... (the counter of each key
    /// goes on, so many collisions are linear); \p maximumLength bounds a PDF name
    static QByteArray createUniqueKey(const QByteArray& key, const std::set<QByteArray>& used, std::map<QByteArray, int>* counters, int maximumLength = 0)
    {
        if (!used.count(key))
        {
            return key;
        }

        int& counter = (*counters)[key];
        counter = qMax(counter, 1);
        while (true)
        {
            ++counter;
            const QByteArray suffix = "_" + QByteArray::number(counter);
            QByteArray base = key;
            if (maximumLength > 0 && base.size() + suffix.size() > maximumLength)
            {
                base.truncate(qMax(0, maximumLength - int(suffix.size())));
            }
            const QByteArray candidate = base + suffix;
            if (!used.count(candidate))
            {
                return candidate;
            }
        }
    }

    /// The entries of a name tree (the values are not dereferenced), read by one walk
    static std::map<QByteArray, PDFObject> readNameTree(const PDFObjectStorage* storage, const PDFObject& root)
    {
        if (storage->getObject(root).isNull())
        {
            return {};
        }
        return PDFNameTreeLoader<PDFObject>::parse(storage, root, [](const PDFObjectStorage*, const PDFObject& object) { return object; });
    }

    /// A flat name tree (ISO 32000-2, 7.9.6: the root may have /Names; keys sorted)
    static PDFObject createNameTree(const std::map<QByteArray, PDFObject>& entries)
    {
        PDFArrayBuilder names;
        names.reserve(entries.size() * 2);
        for (const auto& [name, value] : entries)
        {
            names.appendItem(PDFObject::createString(name));
            names.appendItem(value);
        }
        PDFDictionaryBuilder tree;
        tree.addEntry(key("Names"), PDFObject::createArray(std::move(names)));
        return PDFObject::createDictionary(std::move(tree));
    }

    /// A flat number tree (7.9.7)
    static PDFObject createNumberTree(const std::vector<std::pair<PDFInteger, PDFObject>>& entries)
    {
        PDFArrayBuilder numbers;
        numbers.reserve(entries.size() * 2);
        for (const auto& [number, value] : entries)
        {
            numbers.appendItem(PDFObject::createInteger(number));
            numbers.appendItem(value);
        }
        PDFDictionaryBuilder tree;
        tree.addEntry(key("Nums"), PDFObject::createArray(std::move(numbers)));
        return PDFObject::createDictionary(std::move(tree));
    }

    /// Numbering style of a page label (Table 161)
    static QByteArray getLabelStyle(PDFPageLabel::NumberingStyle style)
    {
        switch (style)
        {
            case PDFPageLabel::NumberingStyle::DecimalArabic:
                return "D";
            case PDFPageLabel::NumberingStyle::UppercaseRoman:
                return "R";
            case PDFPageLabel::NumberingStyle::LowercaseRoman:
                return "r";
            case PDFPageLabel::NumberingStyle::UppercaseLetters:
                return "A";
            case PDFPageLabel::NumberingStyle::LowercaseLetters:
                return "a";
            case PDFPageLabel::NumberingStyle::None:
                break;
        }
        return QByteArray();
    }

    /// The crop margins as the page is shown, mapped to the unrotated page (PageMaster)
    static QMarginsF mapCropMarginsToUnrotatedPage(QMarginsF cropMargins, PageRotation visualRotation)
    {
        switch (visualRotation)
        {
            case PageRotation::None:
                return cropMargins;
            case PageRotation::Rotate90:
                return QMarginsF(cropMargins.top(), cropMargins.right(), cropMargins.bottom(), cropMargins.left());
            case PageRotation::Rotate180:
                return QMarginsF(cropMargins.right(), cropMargins.bottom(), cropMargins.left(), cropMargins.top());
            case PageRotation::Rotate270:
                return QMarginsF(cropMargins.bottom(), cropMargins.left(), cropMargins.top(), cropMargins.right());
        }
        return cropMargins;
    }

    /// Each side of a page within 3 and 14 400 points (PDF Reference 1.7, Table C.1)
    static bool isValidPageSize(const QSizeF& size)
    {
        // The tolerance of a limit converted from millimeters (Blank PDF)
        static constexpr double TOLERANCE = 1e-9;
        auto isValid = [](double side) { return std::isfinite(side) && side >= PDFImagePageFactory::MINIMUM_SIDE - TOLERANCE && side <= PDFImagePageFactory::MAXIMUM_SIDE + TOLERANCE; };
        return isValid(size.width()) && isValid(size.height());
    }

    /// Angle of /Rotate
    static PDFInteger getAngle(PageRotation rotation)
    {
        switch (rotation)
        {
            case PageRotation::Rotate90:
                return 90;
            case PageRotation::Rotate180:
                return 180;
            case PageRotation::Rotate270:
                return 270;
            default:
                return 0;
        }
    }

    /// Keys of a field (Tables 226, 228-234), which a merged dictionary of a field and its widget
    /// gives to the field, when it is split (R8)
    static bool isFieldKey(const QByteArray& name)
    {
        static const std::set<QByteArray> keys = { "FT", "Kids", "T", "TU", "TM", "Ff", "V", "DV", "DA", "Q", "DS", "RV", "MaxLen", "Opt", "TI", "I", "Lock", "SV" };
        return keys.count(name) > 0;
    }

    /// Triggers of the additional actions of a field (Table 199), the rest belongs to the widget
    static bool isFieldTrigger(const QByteArray& name)
    {
        return name == "K" || name == "F" || name == "V" || name == "C";
    }

    /// Replaces the font name of the operator Tf of a default appearance string (12.7.4.3)
    static QByteArray renameDefaultAppearanceFont(const QByteArray& appearance, const std::map<QByteArray, QByteArray>& renames)
    {
        if (renames.empty())
        {
            return appearance;
        }

        // Tokens separated by white space; a name followed by a number and Tf is the font
        QList<QByteArray> tokens = appearance.simplified().split(' ');
        bool isChanged = false;
        for (qsizetype i = 0; i + 2 < tokens.size(); ++i)
        {
            if (tokens[i].startsWith('/') && tokens[i + 2] == "Tf")
            {
                auto it = renames.find(tokens[i].mid(1));
                if (it != renames.cend())
                {
                    tokens[i] = "/" + it->second;
                    isChanged = true;
                }
            }
        }
        return isChanged ? tokens.join(' ') : appearance;
    }
};

// ------------------------------------------------------- The run of a merge

/// One merge (PDFDocumentMerger::merge): the whole state is here, so the merger is reentrant
class PDFDocumentMergerRun
{
public:
    using Merger = PDFDocumentMerger;
    using Helper = PDFDocumentMergerHelper;

    explicit PDFDocumentMergerRun(const std::vector<Merger::Source>& sources, const std::vector<Merger::Item>& items, const Merger::Settings& settings, const PDFOperationControl* control, PDFProgressSink* progress) :
        m_sources(sources),
        m_items(items),
        m_settings(settings),
        m_control(control),
        m_progress(progress)
    {

    }

    Merger::Result run();

private:
    /// A PDF input of the merge
    struct DocumentInput
    {
        size_t source = 0;
        const PDFDocument* document = nullptr;      ///< The document joined: cleaned by phase 1 or the original
        std::shared_ptr<PDFDocument> cleaned;       ///< The result of phase 1
        std::vector<PDFInteger> pageMap;            ///< Page of the source -> page of \p document (-1 = not taken)
        std::vector<PDFInteger> sourcePages;        ///< Page of \p document -> page of the source
        bool isWhole = true;                        ///< Every page of the source is taken
        bool isLeftOut = false;
        QString reason;
        bool hadStructureTree = false;
        bool hadThreads = false;
        bool hadDocumentActions = false;

        // Phase 2
        std::vector<PDFObjectReference> objectMap;  ///< Object number of \p document -> its copy
        std::vector<PDFInteger> objectGenerations;  ///< Generations of the objects of \p document
        PDFInteger firstObject = 0;                 ///< The copies have the numbers firstObject to lastObject
        PDFInteger lastObject = -1;
        std::map<QByteArray, PDFObject> destinations;   ///< Named destinations of the input by their own keys
        std::vector<PDFObjectReference> pages;      ///< Copies of the pages of \p document in its order
        std::vector<bool> isPagePlaced;             ///< The page itself is in the result (a further occurrence is a clone)
        PDFObject catalog;                          ///< Its catalog (references to the copies)
        QString title;                              ///< Title of the input (a caption of a part)

        // What the join renames in the objects of the input
        std::map<QByteArray, QByteArray> destinationRenames;    ///< Key of a named destination -> the key of the result
        std::set<QByteArray> legacyDestinations;                ///< Names of /Dests: a name object refers to them
        std::map<QByteArray, QByteArray> embeddedFileRenames;
        std::map<QByteArray, QByteArray> fontRenames;           ///< /DR /Font of the AcroForm
        std::map<QByteArray, QByteArray> classRenames;          ///< /ClassMap
        std::map<QByteArray, QByteArray> idRenames;             ///< /IDTree
        std::set<QByteArray> namespaceTypes;                    ///< Types of /RoleMap of the input put into its own namespace
        PDFObjectReference roleNamespace;
        PDFInteger structParentOffset = 0;
        bool isStructureJoined = false;
        bool isStructureStripped = false;
        QString uriBase;                                        ///< Base of its relative URIs, which become absolute
    };

    /// An image input
    struct ImageInput
    {
        size_t source = 0;
        std::map<PDFInteger, PDFImagePageFactory::Frame> frames;
        bool isLeftOut = false;
        QString reason;
    };

    /// A page of the result
    struct ResultPage
    {
        PDFObjectReference page;
        DocumentInput* input = nullptr;     ///< The input of a document page
        PDFInteger documentPage = -1;       ///< Page of the cleaned document
        bool isClone = false;
        size_t item = 0;
    };

    /// A run of pages of one input (a document part, K14)
    struct Run
    {
        size_t source = 0;
        size_t first = 0;   ///< Index of the first page of the result
        size_t count = 0;
    };

    bool isCanceled() const { return PDFOperationControl::isOperationCancelled(m_control); }
    const PDFObjectStorage* getStorage() const { return m_builder.getStorage(); }
    const PDFObject& getObject(const PDFObject& object) const { return m_builder.getObject(object); }
    const PDFDictionary* getDictionary(const PDFObject& object) const { return m_builder.getDictionaryFromObject(object); }

    bool validate(QString* errorMessage);
    void cleanInputs();
    void prepareImages();
    bool hasUsableItem() const;

    // Pages
    void copyInput(DocumentInput* input);
    PDFObject remapObject(const DocumentInput* input, const PDFObject& object, int depth) const;
    PDFObjectReference mapReference(const DocumentInput* input, PDFObjectReference reference) const;
    void buildPages(PDFObjectReference pageTreeRoot, bool isSingle);
    PDFObjectReference createBlankPage(QSizeF size, bool isInherited);
    PDFObjectReference clonePage(PDFObjectReference page);
    void cloneWidget(const PDFObject& annotation, PDFObjectReference newAnnotation);
    void setPageGeometry(PDFObjectReference page, const Merger::Item& item, const DocumentInput* input, PDFInteger documentPage);
    void computeRuns();

    // Shared parts of both ways
    void removeSignatures(const std::vector<PDFObject>& fields);
    void collectTerminalFields(const PDFObject& field, int depth, PDFObjectWalk* walk, std::vector<PDFObject>* terminals) const;
    std::vector<PDFObject> getFields(const PDFObject& acroForm) const;
    PDFObject replaceSplitFields(const PDFObject& fields) const;
    PDFObject createPageLabels() const;
    PDFObject createId() const;
    Merger::Result finish(PDFVersion version, bool removeConformance);

    // One document (R3)
    Merger::Result runSingle(DocumentInput* input);

    // More documents
    Merger::Result runMulti();
    void mergeNames(PDFDictionaryBuilder* catalog);
    void mergeForm(PDFDictionaryBuilder* catalog);
    void renameFields(DocumentInput* input, const PDFObject& field, const QString& prefix, int depth, PDFObjectWalk* walk, const std::set<QString>& usedNames, const std::set<QString>& ownNames);
    void collectFieldNames(const PDFObject& field, const QString& prefix, int depth, PDFObjectWalk* walk, std::vector<QString>* names) const;
    void pushDefaultAppearance(const PDFObject& field, const QByteArray& appearance, const std::optional<PDFInteger>& quadding, int depth, PDFObjectWalk* walk, bool hasAppearance, bool hasQuadding, const QByteArray& fieldType);
    void mergeLayers(PDFDictionaryBuilder* catalog);
    std::vector<PDFObject> collectUsedLayers(const DocumentInput* input) const;
    void collectLayers(const PDFObject& object, int depth, bool isGroupList, std::vector<PDFObject>* groups, std::set<PDFObjectReference>* found) const;
    void mergeStructure(PDFDictionaryBuilder* catalog);
    void mergeOutline(PDFDictionaryBuilder* catalog);
    void mergeOtherKeys(PDFDictionaryBuilder* catalog);
    void rewriteInputObjects(DocumentInput* input);
    PDFObject rewriteObject(DocumentInput* input, const PDFObject& object, int depth, bool* isChanged) const;
    PDFObject rewriteDestination(const DocumentInput* input, const PDFObject& destination, bool* isChanged) const;

    /// The target page of an outline item or an action (an explicit destination or a named one of the input)
    PDFObjectReference getTargetPage(const DocumentInput* input, const PDFDictionary* item, const std::map<QByteArray, PDFObject>& destinations) const;

    const std::vector<Merger::Source>& m_sources;
    const std::vector<Merger::Item>& m_items;
    Merger::Settings m_settings;
    const PDFOperationControl* m_control;
    PDFProgressSink* m_progress;

    PDFDocumentBuilder m_builder;
    std::vector<std::unique_ptr<DocumentInput>> m_documents;   ///< PDF inputs in the order of the sources
    std::map<size_t, DocumentInput*> m_documentOfSource;
    std::map<size_t, ImageInput> m_images;
    std::vector<ResultPage> m_resultPages;
    std::vector<Run> m_runs;
    std::map<PDFObjectReference, PDFObjectReference> m_splitFields;    ///< A merged field and widget of /Fields -> the field split from it (R8)
    std::map<PDFObjectReference, PDFObjectReference> m_splitFieldObjects;  ///< Every merged field and widget -> the field split from it (also a kid of a field)
    std::vector<Merger::LeftOut> m_leftOut;
    Merger::Statistics m_statistics;
    bool m_hasOutline = false;
    bool m_isStructureJoined = false;
    bool m_isSingleWithImages = false;     ///< One PDF input with images: its catalog stays, the outline mode applies (Q18)
    QDateTime m_now = QDateTime::currentDateTime();
};

PDFDocumentMerger::Result PDFDocumentMerger::merge(const std::vector<Source>& sources, const std::vector<Item>& items, const Settings& settings, const PDFOperationControl* control, PDFProgressSink* progress)
{
    PDFDocumentMergerRun run(sources, items, settings, control, progress);
    return run.run();
}

PDFDocumentMerger::Result PDFDocumentMergerRun::run()
{
    Merger::Result result;
    try
    {
        QString error;
        if (!validate(&error))
        {
            result.errorMessage = error;
            return result;
        }

        if (m_progress)
        {
            m_progress->start(m_items.size(), ProgressStartupInfo());
        }

        cleanInputs();
        if (isCanceled())
        {
            result.isCanceled = true;
            return result;
        }
        prepareImages();
        if (isCanceled())
        {
            result.isCanceled = true;
            return result;
        }

        if (!hasUsableItem())
        {
            result.errorMessage = m_leftOut.empty() ? PDFDocumentMerger::tr("There is no page to merge.") : m_leftOut.front().reason;
            return result;
        }

        DocumentInput* single = nullptr;
        size_t documentCount = 0;
        for (const std::unique_ptr<DocumentInput>& input : m_documents)
        {
            if (!input->isLeftOut)
            {
                single = input.get();
                ++documentCount;
            }
        }

        result = documentCount == 1 ? runSingle(single) : runMulti();
        if (m_progress)
        {
            m_progress->finish();
        }
    }
    catch (const PDFException& exception)
    {
        result = Merger::Result();
        result.errorMessage = exception.getMessage();
    }

    if (isCanceled())
    {
        result = Merger::Result();
        result.isCanceled = true;
    }
    return result;
}

bool PDFDocumentMergerRun::validate(QString* errorMessage)
{
    if (m_items.empty())
    {
        *errorMessage = PDFDocumentMerger::tr("There is no page to merge.");
        return false;
    }
    if (qint64(m_items.size()) > Merger::MAXIMUM_PAGE_COUNT)
    {
        *errorMessage = PDFDocumentMerger::tr("The result would have more than %Ln pages.", nullptr, int(Merger::MAXIMUM_PAGE_COUNT));
        return false;
    }

    std::vector<PDFInteger> frameCounts(m_sources.size(), -1);
    for (const Merger::Item& item : m_items)
    {
        if (item.source >= m_sources.size())
        {
            *errorMessage = PDFDocumentMerger::tr("A page of the merge has no input.");
            return false;
        }

        const Merger::Source& source = m_sources[item.source];
        switch (source.kind)
        {
            case Merger::Source::Kind::Document:
            {
                if (!source.document)
                {
                    *errorMessage = PDFDocumentMerger::tr("An input document is missing.");
                    return false;
                }
                if (item.pageIndex < 0 || item.pageIndex >= PDFInteger(source.document->getCatalog()->getPageCount()))
                {
                    *errorMessage = PDFDocumentMerger::tr("%1 has no page %2.").arg(source.name).arg(item.pageIndex + 1);
                    return false;
                }
                break;
            }

            case Merger::Source::Kind::Image:
            {
                if (frameCounts[item.source] < 0)
                {
                    frameCounts[item.source] = source.imageData.isEmpty() ? (source.image.isNull() ? 0 : 1) : PDFImagePageFactory::readInfo(source.imageData, source.imageFormat).frameCount;
                }
                // An image, which cannot be read, is left out with its reason (prepareImages)
                if (frameCounts[item.source] > 0 && (item.pageIndex < 0 || item.pageIndex >= frameCounts[item.source]))
                {
                    *errorMessage = PDFDocumentMerger::tr("%1 has no page %2.").arg(source.name).arg(item.pageIndex + 1);
                    return false;
                }
                break;
            }

            case Merger::Source::Kind::Blank:
            {
                if (!Helper::isValidPageSize(item.pageSize))
                {
                    *errorMessage = PDFDocumentMerger::tr("A blank page must have each side between 3 and 14 400 points.");
                    return false;
                }
                break;
            }
        }
    }

    // The documents (R2: every caller; Diff makes no document of the user)
    std::set<size_t> checked;
    for (const Merger::Item& item : m_items)
    {
        const Merger::Source& source = m_sources[item.source];
        if (source.kind != Merger::Source::Kind::Document || !checked.insert(item.source).second)
        {
            continue;
        }

        if (m_settings.checkPermissions)
        {
            const Merger::Permission permission = Merger::getPermission(source.document.data(), source.isOwnerPasswordVerified);
            if (permission != Merger::Permission::Permitted)
            {
                *errorMessage = Merger::getPermissionText(permission, source.name);
                return false;
            }
        }
        if (source.document->getCatalog()->isXFANeedsRendering())
        {
            *errorMessage = PDFDocumentMerger::tr("%1 is a dynamic XFA form: it has no pages, which can be merged.").arg(source.name.isEmpty() ? PDFDocumentMerger::tr("The document") : source.name);
            return false;
        }
    }
    return true;
}

void PDFDocumentMergerRun::cleanInputs()
{
    // The pages of each document, which the result takes
    std::map<size_t, std::vector<bool>> usedPages;
    std::map<size_t, PDFInteger> itemCounts;
    for (const Merger::Item& item : m_items)
    {
        const Merger::Source& source = m_sources[item.source];
        ++itemCounts[item.source];
        if (source.kind == Merger::Source::Kind::Document)
        {
            std::vector<bool>& used = usedPages[item.source];
            used.resize(source.document->getCatalog()->getPageCount(), false);
            used[size_t(item.pageIndex)] = true;
        }
    }

    for (const auto& [sourceIndex, used] : usedPages)
    {
        if (isCanceled())
        {
            return;
        }

        const Merger::Source& source = m_sources[sourceIndex];
        std::unique_ptr<DocumentInput> input = std::make_unique<DocumentInput>();
        input->source = sourceIndex;
        input->document = source.document.data();

        std::vector<PDFInteger> removedPages;
        for (size_t i = 0; i < used.size(); ++i)
        {
            if (!used[i])
            {
                removedPages.push_back(PDFInteger(i));
            }
        }
        input->isWhole = removedPages.empty();

        const PDFDictionary* trailer = Helper::getTrailer(&source.document->getStorage());
        if (const PDFDictionary* catalog = trailer ? source.document->getStorage().getDictionaryFromObject(trailer->get("Root")) : nullptr)
        {
            input->hadStructureTree = !source.document->getStorage().getObject(catalog->get("StructTreeRoot")).isNull();
        }

        // Phase 1 (R1): the pages, which are not taken, go with everything pointing at them
        if (!input->isWhole)
        {
            try
            {
                PDFDocumentPageOperations::RemovalStatistics removal;
                PDFDocument cleaned = PDFDocumentPageOperations::removePages(source.document.data(), removedPages, m_control, &removal);
                if (removal.isCanceled || isCanceled())
                {
                    return;
                }
                input->cleaned = std::make_shared<PDFDocument>(std::move(cleaned));
                input->document = input->cleaned.get();
                input->hadThreads = removal.threadsRemoved;
                input->hadDocumentActions = removal.documentActionsRemoved;
                m_statistics.scriptsRemoved = m_statistics.scriptsRemoved || removal.documentActionsRemoved;
            }
            catch (const PDFException& exception)
            {
                input->isLeftOut = true;
                input->reason = exception.getMessage();
            }
        }

        input->pageMap.assign(used.size(), -1);
        for (size_t i = 0; i < used.size(); ++i)
        {
            if (used[i])
            {
                input->pageMap[i] = PDFInteger(input->sourcePages.size());
                input->sourcePages.push_back(PDFInteger(i));
            }
        }

        if (input->isLeftOut)
        {
            m_leftOut.push_back({ sourceIndex, itemCounts[sourceIndex], input->reason });
        }

        m_documentOfSource[sourceIndex] = input.get();
        m_documents.push_back(std::move(input));
    }
}

void PDFDocumentMergerRun::prepareImages()
{
    std::map<size_t, PDFInteger> itemCounts;
    for (const Merger::Item& item : m_items)
    {
        if (m_sources[item.source].kind == Merger::Source::Kind::Image)
        {
            ImageInput& image = m_images[item.source];
            image.source = item.source;
            image.frames.emplace(item.pageIndex, PDFImagePageFactory::Frame());
            ++itemCounts[item.source];
        }
    }

    for (auto& [sourceIndex, image] : m_images)
    {
        if (isCanceled())
        {
            return;
        }

        // Every frame is decoded before the pages are made (an image, which cannot be read, is
        // left out whole, owner answer Q5): a frame waits compacted, never as its pixels - a merge
        // of hundreds of photos holds their coded images, not gigabytes of decoded pixels; the
        // frames of one file are read by one reader (a TIFF of thousands of pages in one pass)
        const Merger::Source& source = m_sources[sourceIndex];
        QString error;
        if (source.imageData.isEmpty())
        {
            std::optional<PDFImagePageFactory::Frame> decoded = PDFImagePageFactory::createFrame(source.image, source.dotsPerInch, &error);
            if (decoded)
            {
                PDFImagePageFactory::compact(&decoded.value());
                for (auto& [frameIndex, frame] : image.frames)
                {
                    frame = *decoded;
                }
            }
            image.isLeftOut = !decoded;
        }
        else
        {
            std::vector<int> frameIndices;
            for (const auto& [frameIndex, frame] : image.frames)
            {
                frameIndices.push_back(frameIndex >= 0 && frameIndex <= std::numeric_limits<int>::max() ? int(frameIndex) : -1);
            }
            std::vector<PDFImagePageFactory::Frame> frames;
            image.isLeftOut = !PDFImagePageFactory::readFrames(source.imageData, source.imageFormat, frameIndices, source.dotsPerInch, m_control, &frames, &error);
            if (isCanceled())
            {
                return;
            }
            if (!image.isLeftOut)
            {
                auto decoded = frames.begin();
                for (auto& [frameIndex, frame] : image.frames)
                {
                    frame = std::move(*decoded++);
                }
            }
        }
        image.reason = error;

        if (image.isLeftOut)
        {
            image.frames.clear();
            m_leftOut.push_back({ sourceIndex, itemCounts[sourceIndex], image.reason });
        }
    }
}

bool PDFDocumentMergerRun::hasUsableItem() const
{
    for (const Merger::Item& item : m_items)
    {
        auto document = m_documentOfSource.find(item.source);
        auto image = m_images.find(item.source);
        const bool isLeftOut = (document != m_documentOfSource.cend() && document->second->isLeftOut) || (image != m_images.cend() && image->second.isLeftOut);
        if (!isLeftOut)
        {
            return true;
        }
    }
    return false;
}

PDFObjectReference PDFDocumentMergerRun::mapReference(const DocumentInput* input, PDFObjectReference reference) const
{
    // A reference, which leads to no object of the input, is null: it must never reach an
    // object of another input in the result
    if (reference.objectNumber < 0 || reference.objectNumber >= PDFInteger(input->objectMap.size()) ||
        input->objectGenerations[size_t(reference.objectNumber)] != reference.generation)
    {
        return PDFObjectReference();
    }
    return input->objectMap[size_t(reference.objectNumber)];
}

PDFObject PDFDocumentMergerRun::remapObject(const DocumentInput* input, const PDFObject& object, int depth) const
{
    if (depth > Helper::MAXIMUM_OBJECT_DEPTH)
    {
        throw PDFException(PDFDocumentMerger::tr("An object of the document is nested too deeply."));
    }

    switch (object.getType())
    {
        case PDFObject::Type::Reference:
        {
            const PDFObjectReference reference = mapReference(input, object.getReference());
            return reference.isValid() ? PDFObject::createReference(reference) : PDFObject();
        }

        case PDFObject::Type::Array:
        {
            const PDFArray* array = object.getArray();
            PDFArrayBuilder builder;
            builder.reserve(array->getCount());
            for (size_t i = 0; i < array->getCount(); ++i)
            {
                builder.appendItem(remapObject(input, array->getItem(i), depth + 1));
            }
            return PDFObject::createArray(std::move(builder));
        }

        case PDFObject::Type::Dictionary:
        {
            const PDFDictionary* dictionary = object.getDictionary();
            PDFDictionaryBuilder builder;
            builder.reserve(dictionary->getCount());
            for (size_t i = 0; i < dictionary->getCount(); ++i)
            {
                builder.addEntry(dictionary->getKey(i), remapObject(input, dictionary->getValue(i), depth + 1));
            }
            return PDFObject::createDictionary(std::move(builder));
        }

        case PDFObject::Type::Stream:
        {
            const PDFStream* stream = object.getStream();
            const PDFDictionary* dictionary = stream->getDictionary();
            PDFDictionaryBuilder builder;
            builder.reserve(dictionary->getCount());
            for (size_t i = 0; i < dictionary->getCount(); ++i)
            {
                builder.addEntry(dictionary->getKey(i), remapObject(input, dictionary->getValue(i), depth + 1));
            }
            QByteArray content = *stream->getContent();
            return PDFObject::createStream(PDFStream(std::move(builder), std::move(content)));
        }

        default:
            return object;
    }
}

void PDFDocumentMergerRun::copyInput(DocumentInput* input)
{
    // The pages get their inherited attributes (7.7.3.4); the page tree of the input is never
    // followed again - each page gets the root of the result as its /Parent
    PDFDocumentBuilder temporary(input->document);
    temporary.flattenPageTree();
    const std::vector<PDFObjectReference> pages = temporary.getPages();
    const PDFObjectStorage* storage = temporary.getStorage();
    const PDFObjectStorage::PDFObjects& objects = storage->getObjects();

    if (qint64(getStorage()->getObjects().size()) + qint64(objects.size()) > Merger::MAXIMUM_OBJECT_COUNT)
    {
        throw PDFException(PDFDocumentMerger::tr("The result would have more than %Ln objects.", nullptr, int(Merger::MAXIMUM_OBJECT_COUNT)));
    }

    input->objectMap.assign(objects.size(), PDFObjectReference());
    input->objectGenerations.assign(objects.size(), -1);
    input->firstObject = PDFInteger(getStorage()->getObjects().size());
    for (size_t i = 0; i < objects.size(); ++i)
    {
        if (!objects[i].object.isNull())
        {
            input->objectMap[i] = m_builder.addObject(PDFObject());
            input->objectGenerations[i] = objects[i].generation;
        }
    }
    input->lastObject = PDFInteger(getStorage()->getObjects().size()) - 1;
    for (size_t i = 0; i < objects.size(); ++i)
    {
        if (input->objectMap[i].isValid())
        {
            m_builder.setObject(input->objectMap[i], remapObject(input, objects[i].object, 0));
        }
    }

    input->pages.clear();
    for (const PDFObjectReference& page : pages)
    {
        const PDFObjectReference mapped = mapReference(input, page);
        if (!mapped.isValid())
        {
            throw PDFException(PDFDocumentMerger::tr("A page of the document cannot be read."));
        }
        input->pages.push_back(mapped);
    }
    if (input->pages.size() != input->sourcePages.size())
    {
        throw PDFException(PDFDocumentMerger::tr("The pages of the document cannot be read."));
    }
    input->isPagePlaced.assign(input->pages.size(), false);

    const PDFDictionary* trailer = Helper::getTrailer(storage);
    const PDFObject root = trailer ? trailer->get("Root") : PDFObject();
    input->catalog = remapObject(input, storage->getObject(root), 0);

    const Merger::Source& source = m_sources[input->source];
    input->title = input->document->getInfo()->title.trimmed();
    if (input->title.isEmpty())
    {
        input->title = QFileInfo(source.name).completeBaseName();
    }
    if (input->title.isEmpty())
    {
        input->title = PDFDocumentMerger::tr("Document %1").arg(input->source + 1);
    }
}

PDFObjectReference PDFDocumentMergerRun::createBlankPage(QSizeF size, bool isInherited)
{
    // A blank page has no /Contents (Table 31: optional); /MediaBox and /Resources are required
    // and inheritable - a document of blank pages of one size has them on the root (5.4)
    PDFDictionaryBuilder page;
    page.addEntry(Helper::key("Type"), PDFObject::createName("Page"));
    if (!isInherited)
    {
        page.addEntry(Helper::key("MediaBox"), Helper::createRectangle(QRectF(0.0, 0.0, size.width(), size.height())));
        page.addEntry(Helper::key("Resources"), PDFObject::createDictionary(PDFDictionaryBuilder()));
    }
    return m_builder.addObject(PDFObject::createDictionary(std::move(page)));
}

void PDFDocumentMergerRun::setPageGeometry(PDFObjectReference page, const Merger::Item& item, const DocumentInput* input, PDFInteger documentPage)
{
    PageRotation rotation = PageRotation::None;
    const PDFPage* sourcePage = (input && documentPage >= 0) ? input->document->getCatalog()->getPage(size_t(documentPage)) : nullptr;
    if (sourcePage)
    {
        rotation = sourcePage->getPageRotation();
    }
    rotation = getPageRotationCombined(rotation, item.turn);

    PDFDictionaryBuilder update;
    if (rotation != PageRotation::None)
    {
        update.addEntry(Helper::key("Rotate"), PDFObject::createInteger(Helper::getAngle(rotation)));
    }
    else
    {
        update.addEntry(Helper::key("Rotate"), PDFObject());
    }

    // Margins of PageMaster: from the crop box with its origin (B14)
    const QMarginsF& margins = item.cropMargins;
    if (sourcePage && (margins.left() > 0.0 || margins.top() > 0.0 || margins.right() > 0.0 || margins.bottom() > 0.0))
    {
        const QMarginsF unrotated = Helper::mapCropMarginsToUnrotatedPage(margins, sourcePage->getPageRotation());
        const QRectF cropBox = sourcePage->getCropBox();
        const QRectF box(cropBox.left() + unrotated.left(),
                         cropBox.top() + unrotated.bottom(),
                         qMax(1.0, cropBox.width() - unrotated.left() - unrotated.right()),
                         qMax(1.0, cropBox.height() - unrotated.top() - unrotated.bottom()));
        update.addEntry(Helper::key("CropBox"), Helper::createRectangle(QRectF(box.left(), box.top(), box.width(), box.height())));
    }

    const PDFDictionary* dictionary = getDictionary(PDFObject::createReference(page));
    if (!dictionary)
    {
        throw PDFException(PDFDocumentMerger::tr("A page of the result cannot be written."));
    }
    PDFDictionaryBuilder pageBuilder(*dictionary);
    for (size_t i = 0; i < update.getCount(); ++i)
    {
        if (update.getValue(i).isNull())
        {
            pageBuilder.removeEntry(update.getKey(i).getString().constData());
        }
        else
        {
            pageBuilder.setEntry(update.getKey(i), update.getValue(i));
        }
    }
    m_builder.setObject(page, PDFObject::createDictionary(std::move(pageBuilder)));
}

void PDFDocumentMergerRun::cloneWidget(const PDFObject& annotation, PDFObjectReference newAnnotation)
{
    // A clone of a page is a new widget of the same field (a field may have more widgets,
    // 12.7.4.1). A merged dictionary of a field and its widget is split into the field and two
    // widgets in its /Kids (R8).
    if (!annotation.isReference())
    {
        return;
    }

    const PDFObjectReference widgetReference = annotation.getReference();
    const PDFDictionary* widget = getDictionary(annotation);
    const PDFDictionary* newWidget = getDictionary(PDFObject::createReference(newAnnotation));
    if (!widget || !newWidget)
    {
        return;
    }

    auto appendKid = [this](PDFObjectReference field, PDFObjectReference kid, PDFObjectReference replaced)
    {
        const PDFDictionary* fieldDictionary = getDictionary(PDFObject::createReference(field));
        if (!fieldDictionary)
        {
            return;
        }

        std::vector<PDFObject> kids = Helper::getItems(getStorage(), fieldDictionary->get("Kids"));
        bool isReplaced = false;
        if (replaced.isValid())
        {
            for (PDFObject& item : kids)
            {
                if (item.isReference() && item.getReference() == replaced)
                {
                    item = PDFObject::createReference(kid);
                    isReplaced = true;
                }
            }
        }
        if (!isReplaced)
        {
            kids.push_back(PDFObject::createReference(kid));
        }

        PDFDictionaryBuilder builder(*fieldDictionary);
        builder.setEntry(Helper::key("Kids"), Helper::createArray(kids));
        m_builder.setObject(field, PDFObject::createDictionary(std::move(builder)));
    };

    bool isMerged = false;
    for (size_t i = 0; i < widget->getCount() && !isMerged; ++i)
    {
        isMerged = Helper::isFieldKey(widget->getKey(i).getString()) && widget->getKey(i).getString() != "Kids";
    }

    const PDFObject parent = widget->get("Parent");
    if (!isMerged)
    {
        if (parent.isReference())
        {
            appendKid(parent.getReference(), newAnnotation, PDFObjectReference());
        }
        return;
    }

    // Split: the field keys and the triggers of the field go to a new field
    const PDFObjectReference fieldReference = m_builder.addObject(PDFObject());
    PDFDictionaryBuilder field;
    PDFDictionaryBuilder fieldActions;
    PDFDictionaryBuilder widgetActions;
    if (const PDFDictionary* actions = getDictionary(widget->get("AA")))
    {
        for (size_t i = 0; i < actions->getCount(); ++i)
        {
            (Helper::isFieldTrigger(actions->getKey(i).getString()) ? fieldActions : widgetActions).addEntry(actions->getKey(i), actions->getValue(i));
        }
    }

    auto splitWidget = [&](const PDFDictionary* dictionary)
    {
        PDFDictionaryBuilder builder;
        for (size_t i = 0; i < dictionary->getCount(); ++i)
        {
            const QByteArray name = dictionary->getKey(i).getString();
            if (!Helper::isFieldKey(name) && name != "Parent" && name != "AA")
            {
                builder.addEntry(dictionary->getKey(i), dictionary->getValue(i));
            }
        }
        if (!widgetActions.isEmpty())
        {
            builder.addEntry(Helper::key("AA"), PDFObject::createDictionary(PDFDictionaryBuilder(widgetActions)));
        }
        builder.addEntry(Helper::key("Parent"), PDFObject::createReference(fieldReference));
        return PDFObject::createDictionary(std::move(builder));
    };

    for (size_t i = 0; i < widget->getCount(); ++i)
    {
        const QByteArray name = widget->getKey(i).getString();
        if (Helper::isFieldKey(name) && name != "Kids")
        {
            field.addEntry(widget->getKey(i), widget->getValue(i));
        }
    }
    if (!fieldActions.isEmpty())
    {
        field.addEntry(Helper::key("AA"), PDFObject::createDictionary(std::move(fieldActions)));
    }
    if (parent.isReference())
    {
        field.addEntry(Helper::key("Parent"), parent);
    }
    field.addEntry(Helper::key("Kids"), Helper::createArray({ PDFObject::createReference(widgetReference), PDFObject::createReference(newAnnotation) }));

    const PDFObject splitOriginal = splitWidget(widget);
    const PDFObject splitClone = splitWidget(newWidget);
    m_builder.setObject(fieldReference, PDFObject::createDictionary(std::move(field)));
    m_builder.setObject(widgetReference, splitOriginal);
    m_builder.setObject(newAnnotation, splitClone);

    // The new field is an object of the input: its /DA follows the renamed fonts too (rewriteInputObjects)
    m_splitFieldObjects[widgetReference] = fieldReference;
    if (parent.isReference())
    {
        appendKid(parent.getReference(), fieldReference, widgetReference);
    }
    else
    {
        // A field of /Fields: the list of the fields takes the new field instead (replaceSplitFields)
        m_splitFields[widgetReference] = fieldReference;
    }
}

PDFObjectReference PDFDocumentMergerRun::clonePage(PDFObjectReference page)
{
    // B4: only the page object and its annotations are copied, the content and the resources
    // are shared; a clone has no structure (two pages with one key of /ParentTree are an
    // error, R9) and no beads of articles
    const PDFDictionary* dictionary = getDictionary(PDFObject::createReference(page));
    if (!dictionary)
    {
        throw PDFException(PDFDocumentMerger::tr("A page of the document cannot be read."));
    }

    const PDFObjectReference clone = m_builder.addObject(PDFObject());
    PDFDictionaryBuilder pageBuilder(*dictionary);
    pageBuilder.removeEntry("StructParents");
    pageBuilder.removeEntry("B");
    pageBuilder.removeEntry("Annots");
    pageBuilder.removeEntry("DPart");

    const std::vector<PDFObject> annotations = Helper::getItems(getStorage(), dictionary->get("Annots"));
    std::map<PDFObjectReference, PDFObjectReference> annotationMap;
    for (const PDFObject& annotation : annotations)
    {
        if (annotation.isReference() && getDictionary(annotation) && !annotationMap.count(annotation.getReference()))
        {
            annotationMap[annotation.getReference()] = m_builder.addObject(PDFObject());
        }
    }

    std::vector<PDFObject> newAnnotations;
    std::set<PDFObjectReference> created;
    for (const PDFObject& annotation : annotations)
    {
        const PDFDictionary* annotationDictionary = getDictionary(annotation);
        if (!annotationDictionary)
        {
            continue;
        }

        PDFDictionaryBuilder builder(*annotationDictionary);
        builder.setEntry(Helper::key("P"), PDFObject::createReference(clone));
        builder.removeEntry("StructParent");

        const bool isPopup = Helper::getName(annotationDictionary->get("Subtype")) == "Popup";
        for (const char* linkKey : { "Popup", "IRT" })
        {
            const PDFObject& link = annotationDictionary->get(linkKey);
            if (link.isReference())
            {
                auto it = annotationMap.find(link.getReference());
                if (it != annotationMap.cend())
                {
                    builder.setEntry(Helper::key(linkKey), PDFObject::createReference(it->second));
                }
                else if (std::string_view(linkKey) == "Popup")
                {
                    // SECURITY_TODO 30: a /Popup is followed only to a popup of this page
                    builder.removeEntry("Popup");
                }
            }
        }
        if (isPopup)
        {
            const PDFObject& parent = annotationDictionary->get("Parent");
            auto it = parent.isReference() ? annotationMap.find(parent.getReference()) : annotationMap.end();
            if (it != annotationMap.end())
            {
                builder.setEntry(Helper::key("Parent"), PDFObject::createReference(it->second));
            }
            else
            {
                builder.removeEntry("Parent");
            }
        }

        if (annotation.isReference())
        {
            const PDFObjectReference newAnnotation = annotationMap.at(annotation.getReference());
            if (!created.insert(newAnnotation).second)
            {
                // The same annotation listed twice stays one annotation of the clone
                continue;
            }
            m_builder.setObject(newAnnotation, PDFObject::createDictionary(std::move(builder)));
            if (Helper::getName(annotationDictionary->get("Subtype")) == "Widget")
            {
                cloneWidget(annotation, newAnnotation);
            }
            newAnnotations.push_back(PDFObject::createReference(newAnnotation));
        }
        else
        {
            newAnnotations.push_back(PDFObject::createDictionary(std::move(builder)));
        }
    }

    if (!newAnnotations.empty())
    {
        pageBuilder.setEntry(Helper::key("Annots"), Helper::createArray(newAnnotations));
    }
    m_builder.setObject(clone, PDFObject::createDictionary(std::move(pageBuilder)));
    return clone;
}

void PDFDocumentMergerRun::buildPages(PDFObjectReference pageTreeRoot, bool isSingle)
{
    Q_UNUSED(isSingle);

    // Blank pages of one size only: /MediaBox and /Resources are inherited from the root (5.4)
    bool isInheritedBlank = true;
    for (const Merger::Item& item : m_items)
    {
        isInheritedBlank = isInheritedBlank && m_sources[item.source].kind == Merger::Source::Kind::Blank && item.pageSize == m_items.front().pageSize;
    }

    std::vector<PDFObjectReference> pages;
    pages.reserve(m_items.size());
    for (size_t itemIndex = 0; itemIndex < m_items.size(); ++itemIndex)
    {
        if (isCanceled())
        {
            return;
        }

        const Merger::Item& item = m_items[itemIndex];
        const Merger::Source& source = m_sources[item.source];
        ResultPage resultPage;
        resultPage.item = itemIndex;

        switch (source.kind)
        {
            case Merger::Source::Kind::Document:
            {
                DocumentInput* input = m_documentOfSource.at(item.source);
                if (input->isLeftOut)
                {
                    continue;
                }
                const PDFInteger documentPage = input->pageMap[size_t(item.pageIndex)];
                resultPage.input = input;
                resultPage.documentPage = documentPage;
                if (!input->isPagePlaced[size_t(documentPage)])
                {
                    input->isPagePlaced[size_t(documentPage)] = true;
                    resultPage.page = input->pages[size_t(documentPage)];
                }
                else
                {
                    resultPage.page = clonePage(input->pages[size_t(documentPage)]);
                    resultPage.isClone = true;
                }
                setPageGeometry(resultPage.page, item, input, documentPage);
                break;
            }

            case Merger::Source::Kind::Image:
            {
                const ImageInput& image = m_images.at(item.source);
                if (image.isLeftOut)
                {
                    continue;
                }
                resultPage.page = PDFImagePageFactory::createPage(&m_builder, image.frames.at(item.pageIndex), item.pageSize, item.imageClip);
                setPageGeometry(resultPage.page, item, nullptr, -1);
                break;
            }

            case Merger::Source::Kind::Blank:
                resultPage.page = createBlankPage(item.pageSize, isInheritedBlank);
                setPageGeometry(resultPage.page, item, nullptr, -1);
                break;
        }

        PDFDictionaryBuilder parent;
        parent.addEntry(Helper::key("Parent"), PDFObject::createReference(pageTreeRoot));
        m_builder.mergeTo(resultPage.page, PDFObject::createDictionary(std::move(parent)));
        pages.push_back(resultPage.page);
        m_resultPages.push_back(resultPage);

        if (m_progress)
        {
            m_progress->step();
        }
    }

    m_builder.setPages(pages);

    const PDFDictionary* root = getDictionary(PDFObject::createReference(pageTreeRoot));
    PDFDictionaryBuilder rootBuilder = root ? PDFDictionaryBuilder(*root) : PDFDictionaryBuilder();
    if (isInheritedBlank && !m_items.empty())
    {
        const QSizeF size = m_items.front().pageSize;
        rootBuilder.setEntry(Helper::key("MediaBox"), Helper::createRectangle(QRectF(0.0, 0.0, size.width(), size.height())));
        rootBuilder.setEntry(Helper::key("Resources"), PDFObject::createDictionary(PDFDictionaryBuilder()));
    }
    else
    {
        // The pages have their attributes: nothing is inherited from the root of an input
        for (const char* inheritable : { "MediaBox", "CropBox", "Resources", "Rotate" })
        {
            rootBuilder.removeEntry(inheritable);
        }
    }
    rootBuilder.removeEntry("Parent");
    m_builder.setObject(pageTreeRoot, PDFObject::createDictionary(std::move(rootBuilder)));
}

void PDFDocumentMergerRun::computeRuns()
{
    m_runs.clear();
    for (size_t i = 0; i < m_resultPages.size(); ++i)
    {
        const size_t source = m_items[m_resultPages[i].item].source;
        if (m_runs.empty() || m_runs.back().source != source)
        {
            m_runs.push_back({ source, i, 0 });
        }
        ++m_runs.back().count;
    }

    std::set<size_t> sources;
    for (const Run& run : m_runs)
    {
        if (m_sources[run.source].kind != Merger::Source::Kind::Blank)
        {
            sources.insert(run.source);
        }
    }
    m_statistics.pageCount = qint64(m_resultPages.size());
    m_statistics.partCount = qint64(m_runs.size());
    m_statistics.inputCount = qint64(sources.size());
}

std::vector<PDFObject> PDFDocumentMergerRun::getFields(const PDFObject& acroForm) const
{
    const PDFDictionary* form = getDictionary(acroForm);
    return form ? Helper::getItems(getStorage(), form->get("Fields")) : std::vector<PDFObject>();
}

PDFObject PDFDocumentMergerRun::replaceSplitFields(const PDFObject& fields) const
{
    std::vector<PDFObject> items = Helper::getItems(getStorage(), fields);
    for (PDFObject& item : items)
    {
        if (item.isReference())
        {
            auto it = m_splitFields.find(item.getReference());
            if (it != m_splitFields.cend())
            {
                item = PDFObject::createReference(it->second);
            }
        }
    }
    return Helper::createArray(items);
}

void PDFDocumentMergerRun::collectTerminalFields(const PDFObject& field, int depth, PDFObjectWalk* walk, std::vector<PDFObject>* terminals) const
{
    if (!walk->enter(field, depth))
    {
        return;
    }
    const PDFDictionary* dictionary = getDictionary(field);
    if (!dictionary)
    {
        return;
    }

    // A kid without a partial name and without field keys is a widget (12.7.4.1)
    bool hasFieldKids = false;
    const std::vector<PDFObject> kids = Helper::getItems(getStorage(), dictionary->get("Kids"));
    for (const PDFObject& kid : kids)
    {
        const PDFDictionary* kidDictionary = getDictionary(kid);
        if (kidDictionary && (kidDictionary->hasKey("T") || kidDictionary->hasKey("FT") || kidDictionary->hasKey("Kids")))
        {
            hasFieldKids = true;
            collectTerminalFields(kid, depth + 1, walk, terminals);
        }
    }
    if (!hasFieldKids)
    {
        terminals->push_back(field);
    }
}

void PDFDocumentMergerRun::removeSignatures(const std::vector<PDFObject>& fields)
{
    // Owner answer Q4: the value and the appearance of a signature go, the field stays empty
    // for a new signature; a signature of another file is never valid (12.8.1)
    PDFObjectWalk walk(Helper::MAXIMUM_TREE_DEPTH);
    std::vector<PDFObject> terminals;
    for (const PDFObject& field : fields)
    {
        collectTerminalFields(field, 0, &walk, &terminals);
    }

    for (const PDFObject& terminal : terminals)
    {
        // The field type is inheritable (Table 226)
        QByteArray fieldType;
        PDFObject current = terminal;
        std::set<PDFObjectReference> visited;
        for (int depth = 0; depth < Helper::MAXIMUM_TREE_DEPTH && fieldType.isEmpty(); ++depth)
        {
            const PDFDictionary* dictionary = getDictionary(current);
            if (!dictionary || (current.isReference() && !visited.insert(current.getReference()).second))
            {
                break;
            }
            fieldType = Helper::getName(dictionary->get("FT"));
            current = dictionary->get("Parent");
        }
        if (fieldType != "Sig" || !terminal.isReference())
        {
            continue;
        }

        const PDFDictionary* dictionary = getDictionary(terminal);
        if (!dictionary->hasKey("V"))
        {
            continue;
        }

        // The kids are read before the field is replaced: the dictionary dies with its object
        const std::vector<PDFObject> kids = Helper::getItems(getStorage(), dictionary->get("Kids"));
        PDFDictionaryBuilder field(*dictionary);
        field.removeEntry("V");
        if (Helper::getName(dictionary->get("Subtype")) == "Widget")
        {
            field.removeEntry("AP");
            field.removeEntry("AS");
        }
        dictionary = nullptr;
        m_builder.setObject(terminal.getReference(), PDFObject::createDictionary(std::move(field)));

        for (const PDFObject& kid : kids)
        {
            const PDFDictionary* widget = getDictionary(kid);
            if (widget && kid.isReference())
            {
                PDFDictionaryBuilder widgetBuilder(*widget);
                widgetBuilder.removeEntry("AP");
                widgetBuilder.removeEntry("AS");
                m_builder.setObject(kid.getReference(), PDFObject::createDictionary(std::move(widgetBuilder)));
            }
        }
        ++m_statistics.signaturesRemoved;
    }
}

PDFObject PDFDocumentMergerRun::createPageLabels() const
{
    // 12.4.2: every page keeps the label it had in its input; an input without labels and an
    // image have decimal numbers of their pages, a blank page the number of its place
    struct Label
    {
        QByteArray style;
        QString prefix;
        PDFInteger number = 1;
    };

    bool hasLabels = false;
    for (const std::unique_ptr<DocumentInput>& input : m_documents)
    {
        hasLabels = hasLabels || (!input->isLeftOut && !input->document->getCatalog()->getPageLabels().empty());
    }
    if (!hasLabels)
    {
        return PDFObject();
    }

    std::vector<Label> labels;
    labels.reserve(m_resultPages.size());
    for (size_t i = 0; i < m_resultPages.size(); ++i)
    {
        const ResultPage& page = m_resultPages[i];
        Label label;
        label.style = "D";
        if (page.input)
        {
            const std::vector<PDFPageLabel>& pageLabels = page.input->document->getCatalog()->getPageLabels();
            auto it = std::upper_bound(pageLabels.cbegin(), pageLabels.cend(), page.documentPage, [](PDFInteger pageIndex, const PDFPageLabel& pageLabel) { return pageIndex < pageLabel.getPageIndex(); });
            if (it != pageLabels.cbegin())
            {
                const PDFPageLabel& pageLabel = *std::prev(it);
                label.style = Helper::getLabelStyle(pageLabel.getNumberingStyle());
                label.prefix = pageLabel.getPrefix();
                label.number = qMax<PDFInteger>(1, pageLabel.getPageStartNumber()) + page.documentPage - pageLabel.getPageIndex();
            }
            else
            {
                label.number = page.input->sourcePages[size_t(page.documentPage)] + 1;
            }
        }
        else if (m_sources[m_items[page.item].source].kind == Merger::Source::Kind::Image)
        {
            label.number = m_items[page.item].pageIndex + 1;
        }
        else
        {
            label.number = PDFInteger(i) + 1;
        }
        labels.push_back(label);
    }

    std::vector<std::pair<PDFInteger, PDFObject>> ranges;
    for (size_t i = 0; i < labels.size(); ++i)
    {
        const Label& label = labels[i];
        const bool continues = i > 0 && labels[i - 1].style == label.style && labels[i - 1].prefix == label.prefix &&
                               (label.style.isEmpty() || labels[i - 1].number + 1 == label.number);
        if (continues)
        {
            continue;
        }

        PDFDictionaryBuilder range;
        if (!label.style.isEmpty())
        {
            range.addEntry(Helper::key("S"), PDFObject::createName(label.style));
        }
        if (!label.prefix.isEmpty())
        {
            range.addEntry(Helper::key("P"), PDFObjectFactory::createTextString(label.prefix));
        }
        if (!label.style.isEmpty() && label.number != 1)
        {
            range.addEntry(Helper::key("St"), PDFObject::createInteger(label.number));
        }
        ranges.emplace_back(PDFInteger(i), PDFObject::createDictionary(std::move(range)));
    }
    return Helper::createNumberTree(ranges);
}

PDFObject PDFDocumentMergerRun::createId() const
{
    // 14.4: a new file gets a new identifier, both strings are the same
    QCryptographicHash hash(QCryptographicHash::Md5);
    hash.addData(QUuid::createUuid().toRfc4122());
    hash.addData(m_now.toString(Qt::ISODateWithMs).toUtf8());
    hash.addData(QByteArray::number(qint64(m_resultPages.size())));
    const QByteArray identifier = hash.result();
    return Helper::createArray({ PDFObject::createString(identifier), PDFObject::createString(identifier) });
}

PDFDocumentMerger::Result PDFDocumentMergerRun::finish(PDFVersion version, bool removeConformance)
{
    Merger::Result result;
    if (isCanceled())
    {
        result.isCanceled = true;
        return result;
    }

    PDFDocument built = m_builder.build();
    PDFObjectStorage storage = built.getStorage();

    // A new document: a new identifier, not encrypted (owner answer Q2)
    PDFDictionaryBuilder trailer;
    trailer.addEntry(Helper::key("ID"), createId());
    trailer.addEntry(Helper::key("Encrypt"), PDFObject());
    storage.updateTrailerDictionary(PDFObject::createDictionary(std::move(trailer)));
    storage.setSecurityHandler(PDFSecurityHandlerPointer(new PDFNoneSecurityHandler()));

    const PDFDictionary* trailerDictionary = Helper::getTrailer(&storage);
    const PDFObject infoObject = trailerDictionary ? trailerDictionary->get("Info") : PDFObject();
    const PDFObject catalogObject = trailerDictionary ? trailerDictionary->get("Root") : PDFObject();

    // Creation and modification of a new document are now (14.3.3); PDF 2.0 has them in XMP too
    if (!m_statistics.isSingleDocument && infoObject.isReference())
    {
        if (const PDFDictionary* info = storage.getDictionaryFromObject(infoObject))
        {
            PDFDictionaryBuilder infoBuilder(*info);
            const PDFObject date = PDFObject::createString(PDFEncoding::convertDateTimeToString(m_now));
            infoBuilder.setEntry(Helper::key("CreationDate"), date);
            infoBuilder.setEntry(Helper::key("ModDate"), date);
            const QString producer = Helper::getText(info->get("Producer"));
            storage.setObject(infoObject.getReference(), PDFObject::createDictionary(std::move(infoBuilder)));

            if (version >= PDFVersion(2, 0) && catalogObject.isReference())
            {
                const QString date8601 = m_now.toOffsetFromUtc(m_now.offsetFromUtc()).toString(Qt::ISODate);
                QByteArray xmp;
                xmp += "<?xpacket begin=\"\xEF\xBB\xBF\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n";
                xmp += "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">\n<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n";
                xmp += "<rdf:Description rdf:about=\"\" xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\" xmlns:pdf=\"http://ns.adobe.com/pdf/1.3/\">\n";
                xmp += "<xmp:CreateDate>" + date8601.toUtf8() + "</xmp:CreateDate>\n";
                xmp += "<xmp:ModifyDate>" + date8601.toUtf8() + "</xmp:ModifyDate>\n";
                xmp += "<pdf:Producer>" + producer.toHtmlEscaped().toUtf8() + "</pdf:Producer>\n";
                xmp += "</rdf:Description>\n</rdf:RDF>\n</x:xmpmeta>\n<?xpacket end=\"w\"?>";

                PDFDictionaryBuilder metadataDictionary;
                metadataDictionary.addEntry(Helper::key("Type"), PDFObject::createName("Metadata"));
                metadataDictionary.addEntry(Helper::key("Subtype"), PDFObject::createName("XML"));
                metadataDictionary.addEntry(Helper::key("Length"), PDFObject::createInteger(xmp.size()));
                const PDFObjectReference metadata = storage.addObject(PDFObject::createStream(PDFStream(std::move(metadataDictionary), std::move(xmp))));
                if (const PDFDictionary* catalog = storage.getDictionaryFromObject(catalogObject))
                {
                    PDFDictionaryBuilder catalogBuilder(*catalog);
                    catalogBuilder.setEntry(Helper::key("Metadata"), PDFObject::createReference(metadata));
                    storage.setObject(catalogObject.getReference(), PDFObject::createDictionary(std::move(catalogBuilder)));
                }
            }
        }
    }

    // The objects nothing refers to go (the catalogs and the page trees of the inputs, ...)
    PDFOptimizer optimizer(PDFOptimizer::OptimizationFlags(PDFOptimizer::RemoveUnusedObjects | PDFOptimizer::ShrinkObjectStorage |
                                                           PDFOptimizer::DereferenceSimpleObjects | PDFOptimizer::MergeIdenticalObjects), nullptr);
    optimizer.setOperationControl(m_control);
    optimizer.setStorage(storage);
    optimizer.optimize();
    storage = optimizer.takeStorage();
    if (isCanceled())
    {
        result.isCanceled = true;
        return result;
    }

    // The merged identical objects can repeat a reference in an array of the layers or fields
    PDFDocument document(PDFObjectStorage(storage), version, QByteArray());
    {
        PDFDocumentBuilder finalBuilder(&document);
        const PDFDictionary* catalog = finalBuilder.getDictionaryFromObject(finalBuilder.getObjectByReference(finalBuilder.getCatalogReference()));
        if (catalog)
        {
            PDFDocumentDataLoaderDecorator loader(finalBuilder.getStorage());
            for (const char* key : { "OCProperties", "AcroForm" })
            {
                const PDFObjectReference reference = loader.readReferenceFromDictionary(catalog, key);
                if (reference.isValid())
                {
                    finalBuilder.setObject(reference, PDFObjectManipulator::removeDuplicitReferencesInArrays(finalBuilder.getObjectByReference(reference)));
                }
            }
        }

        // 8.11.2.2 (R10): every layer, which the content or an annotation uses, is in /OCGs
        if (!m_statistics.isSingleDocument && catalog)
        {
            const PDFObject ocPropertiesObject = catalog->get("OCProperties");
            if (const PDFDictionary* ocProperties = finalBuilder.getDictionaryFromObject(ocPropertiesObject))
            {
                std::vector<PDFObject> groups = Helper::getItems(finalBuilder.getStorage(), ocProperties->get("OCGs"));
                std::set<PDFObjectReference> listed;
                for (const PDFObject& group : groups)
                {
                    if (group.isReference())
                    {
                        listed.insert(group.getReference());
                    }
                }
                const PDFObjectStorage::PDFObjects& objects = finalBuilder.getStorage()->getObjects();
                bool isChanged = false;
                for (size_t i = 0; i < objects.size(); ++i)
                {
                    const PDFDictionary* dictionary = objects[i].object.isDictionary() ? objects[i].object.getDictionary() : nullptr;
                    const PDFObjectReference reference(PDFInteger(i), objects[i].generation);
                    if (dictionary && Helper::getName(dictionary->get("Type")) == "OCG" && !listed.count(reference))
                    {
                        groups.push_back(PDFObject::createReference(reference));
                        listed.insert(reference);
                        isChanged = true;
                    }
                }
                if (isChanged)
                {
                    PDFDictionaryBuilder builder(*ocProperties);
                    builder.setEntry(Helper::key("OCGs"), Helper::createArray(groups));
                    if (ocPropertiesObject.isReference())
                    {
                        finalBuilder.setObject(ocPropertiesObject.getReference(), PDFObject::createDictionary(std::move(builder)));
                    }
                    else
                    {
                        PDFDictionaryBuilder catalogBuilder(*catalog);
                        catalogBuilder.setEntry(Helper::key("OCProperties"), PDFObject::createDictionary(std::move(builder)));
                        finalBuilder.setObject(finalBuilder.getCatalogReference(), PDFObject::createDictionary(std::move(catalogBuilder)));
                    }
                }
            }
        }

        PDFDocument finalDocument = finalBuilder.build();
        PDFObjectStorage finalStorage = finalDocument.getStorage();

        // The build writes the time of now into /ModDate: a new document has the one time of its
        // creation in /Info and in XMP (14.3.3)
        const PDFDictionary* finalTrailer = Helper::getTrailer(&finalStorage);
        const PDFObject finalInfo = finalTrailer ? finalTrailer->get("Info") : PDFObject();
        const PDFDictionary* infoDictionary = finalStorage.getDictionaryFromObject(finalInfo);
        if (!m_statistics.isSingleDocument && finalInfo.isReference() && infoDictionary)
        {
            PDFDictionaryBuilder infoBuilder(*infoDictionary);
            infoBuilder.setEntry(Helper::key("ModDate"), PDFObject::createString(PDFEncoding::convertDateTimeToString(m_now)));
            infoBuilder.setEntry(Helper::key("CreationDate"), PDFObject::createString(PDFEncoding::convertDateTimeToString(m_now)));
            finalStorage.setObject(finalInfo.getReference(), PDFObject::createDictionary(std::move(infoBuilder)));
        }
        document = PDFDocument(std::move(finalStorage), version, QByteArray());
    }

    // One document keeps its metadata: it must not declare a standard it was not checked against (R7)
    if (removeConformance)
    {
        PDFObjectStorage conformanceStorage = document.getStorage();
        const PDFDocumentTool::ConformanceRemoval conformance = PDFDocumentTool::removeConformanceDeclaration(&conformanceStorage, &document);
        if (!conformance.kept.isEmpty())
        {
            result.errorMessage = PDFDocumentTool::createConformanceFailure(conformance.kept).message;
            return result;
        }
        if (!conformance.removed.isEmpty())
        {
            m_statistics.conformanceRemoved = conformance.removed;
            document = PDFDocument(std::move(conformanceStorage), version, QByteArray());
        }
    }

    if (qint64(document.getStorage().getObjects().size()) > Merger::MAXIMUM_OBJECT_COUNT)
    {
        result.errorMessage = PDFDocumentMerger::tr("The result would have more than %Ln objects.", nullptr, int(Merger::MAXIMUM_OBJECT_COUNT));
        return result;
    }

    result.isSuccess = true;
    result.document = std::move(document);
    result.statistics = m_statistics;
    result.leftOut = m_leftOut;
    return result;
}

PDFDocumentMerger::Result PDFDocumentMergerRun::runSingle(DocumentInput* input)
{
    // R3: one PDF input with all its pages keeps its catalog; only the page tree, the labels
    // and the document parts follow the order. A cloned page is a new widget of each field.
    m_statistics.isSingleDocument = true;

    // Phase 1 removed the tags of an input with omitted pages (as Remove Pages, Q1): the fact says it
    m_statistics.structureTreeRemoved = input->hadStructureTree && !input->isWhole;
    m_builder.setDocument(input->document);
    m_builder.flattenPageTree();
    input->pages = m_builder.getPages();
    if (input->pages.size() != input->sourcePages.size())
    {
        throw PDFException(PDFDocumentMerger::tr("The pages of the document cannot be read."));
    }
    input->isPagePlaced.assign(input->pages.size(), false);
    const std::vector<PDFObjectReference> originalPages = input->pages;

    const PDFObjectReference catalogReference = m_builder.getCatalogReference();
    const PDFDictionary* catalogDictionary = getDictionary(PDFObject::createReference(catalogReference));
    if (!catalogDictionary || !catalogDictionary->get("Pages").isReference())
    {
        throw PDFException(PDFDocumentMerger::tr("The document has no page tree."));
    }
    const PDFObjectReference pageTreeRoot = catalogDictionary->get("Pages").getReference();
    buildPages(pageTreeRoot, true);
    if (isCanceled())
    {
        Merger::Result result;
        result.isCanceled = true;
        return result;
    }
    computeRuns();

    catalogDictionary = getDictionary(PDFObject::createReference(catalogReference));
    PDFDictionaryBuilder catalog(*catalogDictionary);

    std::vector<PDFObjectReference> resultPages;
    for (const ResultPage& page : m_resultPages)
    {
        resultPages.push_back(page.page);
    }
    const bool isOrderChanged = resultPages != originalPages;

    // The fields: the split ones of the clones, the signatures (Q4: never valid in a new file)
    const PDFObject acroFormObject = catalog.get("AcroForm");
    if (const PDFDictionary* acroForm = getDictionary(acroFormObject))
    {
        PDFDictionaryBuilder form(*acroForm);
        form.setEntry(Helper::key("Fields"), replaceSplitFields(acroForm->get("Fields")));
        const std::vector<PDFObject> fields = Helper::getItems(getStorage(), form.get("Fields"));
        if (acroFormObject.isReference())
        {
            m_builder.setObject(acroFormObject.getReference(), PDFObject::createDictionary(PDFDictionaryBuilder(form)));
        }
        else
        {
            catalog.setEntry(Helper::key("AcroForm"), PDFObject::createDictionary(PDFDictionaryBuilder(form)));
        }
        removeSignatures(fields);

        // Bit 2 (AppendOnly) of /SigFlags goes with the signatures (Table 225)
        const PDFObject& flags = getObject(form.get("SigFlags"));
        if (m_statistics.signaturesRemoved > 0 && flags.isInt() && (flags.getInteger() & 2))
        {
            form.setEntry(Helper::key("SigFlags"), PDFObject::createInteger(flags.getInteger() & ~PDFInteger(2)));
            if (acroFormObject.isReference())
            {
                m_builder.setObject(acroFormObject.getReference(), PDFObject::createDictionary(std::move(form)));
            }
            else
            {
                catalog.setEntry(Helper::key("AcroForm"), PDFObject::createDictionary(std::move(form)));
            }
        }
    }
    if (m_statistics.signaturesRemoved > 0)
    {
        // The certification and the validation data belong to the removed signatures
        catalog.removeEntry("Perms");
        catalog.removeEntry("DSS");
    }

    if (isOrderChanged)
    {
        const PDFObject labels = createPageLabels();
        if (!labels.isNull())
        {
            catalog.setEntry(Helper::key("PageLabels"), PDFObject::createReference(m_builder.addObject(labels)));
        }

        // The document parts have ranges of pages (14.12): another order breaks them
        if (catalog.hasKey("DPartRoot"))
        {
            catalog.removeEntry("DPartRoot");
            for (const PDFObjectReference& page : resultPages)
            {
                const PDFDictionary* pageDictionary = getDictionary(PDFObject::createReference(page));
                if (pageDictionary && pageDictionary->hasKey("DPart"))
                {
                    PDFDictionaryBuilder pageBuilder(*pageDictionary);
                    pageBuilder.removeEntry("DPart");
                    m_builder.setObject(page, PDFObject::createDictionary(std::move(pageBuilder)));
                }
            }
        }
    }

    // Owner answer Q18: one PDF input with images - the catalog stays (R3), but the chosen
    // outline mode applies: a bookmark and a document part for every input (the outline of the
    // PDF below its bookmark), Join as for more inputs, or no outline at all
    bool hasImage = false;
    for (const ResultPage& page : m_resultPages)
    {
        hasImage = hasImage || m_sources[m_items[page.item].source].kind == Merger::Source::Kind::Image;
    }

    PDFVersion version = input->document->getInfo()->version;
    if (hasImage)
    {
        m_isSingleWithImages = true;
        input->catalog = PDFObject::createDictionary(PDFDictionaryBuilder(catalog));
        input->title = input->document->getInfo()->title.trimmed();
        if (input->title.isEmpty())
        {
            input->title = QFileInfo(m_sources[input->source].name).completeBaseName();
        }
        if (input->title.isEmpty())
        {
            input->title = PDFDocumentMerger::tr("Document %1").arg(input->source + 1);
        }

        // The named destinations of the document, by which its outline items may lead to pages
        if (const PDFDictionary* names = getDictionary(catalog.get("Names")))
        {
            input->destinations = Helper::readNameTree(getStorage(), names->get("Dests"));
        }
        if (const PDFDictionary* legacy = getDictionary(catalog.get("Dests")))
        {
            for (size_t i = 0; i < legacy->getCount(); ++i)
            {
                input->destinations.emplace(legacy->getKey(i).getString(), legacy->getValue(i));
            }
        }

        mergeOutline(&catalog);
        catalog.removeEntry("DPartRoot");
        if (!m_hasOutline && Helper::getName(getObject(catalog.get("PageMode"))) == "UseOutlines")
        {
            catalog.removeEntry("PageMode");
        }
    }
    m_builder.setObject(catalogReference, PDFObject::createDictionary(std::move(catalog)));

    if (hasImage && m_settings.outline != Merger::OutlineMode::NoOutline && !m_runs.empty())
    {
        // The parts of the runs replace the parts of the document (removed above, 14.12)
        for (const ResultPage& page : m_resultPages)
        {
            const PDFDictionary* pageDictionary = getDictionary(PDFObject::createReference(page.page));
            if (pageDictionary && pageDictionary->hasKey("DPart"))
            {
                PDFDictionaryBuilder pageBuilder(*pageDictionary);
                pageBuilder.removeEntry("DPart");
                m_builder.setObject(page.page, PDFObject::createDictionary(std::move(pageBuilder)));
            }
        }
        std::vector<size_t> parts;
        for (const Run& run : m_runs)
        {
            parts.push_back(run.count);
        }
        m_builder.createDocumentParts(parts);
        version = qMax(version, PDFVersion(2, 0));
    }

    return finish(version, true);
}

PDFDocumentMerger::Result PDFDocumentMergerRun::runMulti()
{
    m_builder.createDocument();

    // Copy of each input; a copy, which fails, leaves the input out (owner answer Q5)
    for (const std::unique_ptr<DocumentInput>& input : m_documents)
    {
        if (isCanceled())
        {
            Merger::Result result;
            result.isCanceled = true;
            return result;
        }
        if (input->isLeftOut)
        {
            continue;
        }

        try
        {
            copyInput(input.get());
        }
        catch (const PDFException& exception)
        {
            input->isLeftOut = true;
            input->reason = exception.getMessage();
            PDFInteger count = 0;
            for (const Merger::Item& item : m_items)
            {
                count += item.source == input->source ? 1 : 0;
            }
            m_leftOut.push_back({ input->source, count, input->reason });
        }
    }

    if (!hasUsableItem())
    {
        Merger::Result result;
        result.errorMessage = m_leftOut.empty() ? PDFDocumentMerger::tr("There is no page to merge.") : m_leftOut.front().reason;
        return result;
    }

    const PDFObjectReference catalogReference = m_builder.getCatalogReference();
    const PDFDictionary* catalogDictionary = getDictionary(PDFObject::createReference(catalogReference));
    if (!catalogDictionary || !catalogDictionary->get("Pages").isReference())
    {
        throw PDFException(PDFDocumentMerger::tr("The document has no page tree."));
    }
    const PDFObjectReference pageTreeRoot = catalogDictionary->get("Pages").getReference();
    buildPages(pageTreeRoot, false);
    if (isCanceled())
    {
        Merger::Result result;
        result.isCanceled = true;
        return result;
    }
    computeRuns();

    catalogDictionary = getDictionary(PDFObject::createReference(catalogReference));
    PDFDictionaryBuilder catalog(*catalogDictionary);
    mergeNames(&catalog);
    mergeForm(&catalog);
    mergeLayers(&catalog);
    mergeStructure(&catalog);

    // The other keys first: the bases of the relative URIs (12.6.4.8) are known before the items
    // of the outlines of the inputs are rewritten (Join)
    mergeOtherKeys(&catalog);
    mergeOutline(&catalog);

    // A page mode of the first input, which shows a panel the result has not (Table 29)
    const QByteArray pageMode = Helper::getName(getObject(catalog.get("PageMode")));
    if ((pageMode == "UseOutlines" && !m_hasOutline) || (pageMode == "UseOC" && !catalog.hasKey("OCProperties")) || (pageMode == "UseAttachments" && !catalog.hasKey("Names")))
    {
        catalog.removeEntry("PageMode");
    }

    const PDFObject labels = createPageLabels();
    if (!labels.isNull())
    {
        catalog.setEntry(Helper::key("PageLabels"), PDFObject::createReference(m_builder.addObject(labels)));
    }
    m_builder.setObject(catalogReference, PDFObject::createDictionary(std::move(catalog)));

    for (const std::unique_ptr<DocumentInput>& input : m_documents)
    {
        if (!input->isLeftOut)
        {
            rewriteInputObjects(input.get());
        }
        if (isCanceled())
        {
            Merger::Result result;
            result.isCanceled = true;
            return result;
        }
    }

    // The version (7.5.2): of the inputs, at least 1.7; the document parts and the namespaces of
    // the structure are PDF 2.0 (Q7, 14.12, 14.7.4)
    PDFVersion version(1, 7);
    for (const std::unique_ptr<DocumentInput>& input : m_documents)
    {
        if (!input->isLeftOut)
        {
            version = qMax(version, input->document->getInfo()->version);
        }
    }
    if (m_settings.outline != Merger::OutlineMode::NoOutline && !m_runs.empty() && m_statistics.inputCount > 0)
    {
        std::vector<size_t> parts;
        for (const Run& run : m_runs)
        {
            parts.push_back(run.count);
        }
        m_builder.createDocumentParts(parts);
        version = qMax(version, PDFVersion(2, 0));
    }
    if (m_isStructureJoined)
    {
        version = qMax(version, PDFVersion(2, 0));
    }

    return finish(version, false);
}

PDFObjectReference PDFDocumentMergerRun::getTargetPage(const DocumentInput* input, const PDFDictionary* item, const std::map<QByteArray, PDFObject>& destinations) const
{
    Q_UNUSED(input);

    PDFObject destination = item->get("Dest");
    if (destination.isNull())
    {
        const PDFDictionary* action = getDictionary(item->get("A"));
        if (!action || Helper::getName(action->get("S")) != "GoTo")
        {
            return PDFObjectReference();
        }
        destination = action->get("D");
    }

    // A named destination (12.3.2.3): a name of /Dests or a string of the name tree
    for (int depth = 0; depth < 4; ++depth)
    {
        const PDFObject& dereferenced = getObject(destination);
        if (dereferenced.isName() || dereferenced.isString())
        {
            auto it = destinations.find(dereferenced.getString());
            if (it == destinations.cend())
            {
                return PDFObjectReference();
            }
            destination = it->second;
            continue;
        }
        if (const PDFDictionary* dictionary = dereferenced.isDictionary() ? dereferenced.getDictionary() : nullptr)
        {
            destination = dictionary->get("D");
            continue;
        }
        if (dereferenced.isArray() && dereferenced.getArray()->getCount() > 0 && dereferenced.getArray()->getItem(0).isReference())
        {
            return dereferenced.getArray()->getItem(0).getReference();
        }
        break;
    }
    return PDFObjectReference();
}

void PDFDocumentMergerRun::mergeNames(PDFDictionaryBuilder* catalog)
{
    // Table 32: each name tree of the inputs, the collisions renamed ("name_2", R6); the
    // destinations of /Dests (PDF 1.1) join the tree of the destinations (12.3.2.3)
    static constexpr const char* trees[] = { "Dests", "AP", "JavaScript", "Pages", "Templates", "IDS", "URLS", "EmbeddedFiles", "AlternatePresentations", "Renditions" };

    std::map<QByteArray, std::map<QByteArray, PDFObject>> result;
    std::map<QByteArray, std::set<QByteArray>> usedKeys;
    std::map<QByteArray, std::map<QByteArray, int>> counters;

    for (const std::unique_ptr<DocumentInput>& input : m_documents)
    {
        if (input->isLeftOut)
        {
            continue;
        }

        const PDFDictionary* inputCatalog = getDictionary(input->catalog);
        if (!inputCatalog)
        {
            continue;
        }
        const PDFDictionary* names = getDictionary(inputCatalog->get("Names"));

        for (const char* tree : trees)
        {
            std::map<QByteArray, PDFObject> entries = names ? Helper::readNameTree(getStorage(), names->get(tree)) : std::map<QByteArray, PDFObject>();
            const bool isDestinations = std::string_view(tree) == "Dests";
            if (isDestinations)
            {
                if (const PDFDictionary* legacy = getDictionary(inputCatalog->get("Dests")))
                {
                    for (size_t i = 0; i < legacy->getCount(); ++i)
                    {
                        const QByteArray name = legacy->getKey(i).getString();
                        input->legacyDestinations.insert(name);
                        entries.emplace(name, legacy->getValue(i));
                    }
                }
                input->destinations = entries;
            }

            if (std::string_view(tree) == "JavaScript")
            {
                // Scripts of the documents are removed (Table 29, 12.6; Q8 of the first batch)
                m_statistics.scriptsRemoved = m_statistics.scriptsRemoved || !entries.empty();
                continue;
            }

            for (const auto& [name, value] : entries)
            {
                const QByteArray newName = Helper::createUniqueKey(name, usedKeys[tree], &counters[tree]);
                usedKeys[tree].insert(newName);
                result[tree].emplace(newName, value);

                if (isDestinations)
                {
                    if (newName != name)
                    {
                        ++m_statistics.destinationsRenamed;
                    }
                    if (newName != name || input->legacyDestinations.count(name))
                    {
                        input->destinationRenames[name] = newName;
                    }
                }
                else if (newName != name)
                {
                    ++m_statistics.namesRenamed;
                    if (std::string_view(tree) == "EmbeddedFiles")
                    {
                        input->embeddedFileRenames[name] = newName;
                    }
                }
            }
        }
    }

    PDFDictionaryBuilder names;
    for (const char* tree : trees)
    {
        auto it = result.find(tree);
        if (it != result.cend() && !it->second.empty())
        {
            names.addEntry(Helper::key(tree), PDFObject::createReference(m_builder.addObject(Helper::createNameTree(it->second))));
        }
    }

    catalog->removeEntry("Dests");
    if (names.isEmpty())
    {
        catalog->removeEntry("Names");
    }
    else
    {
        catalog->setEntry(Helper::key("Names"), PDFObject::createReference(m_builder.addObject(PDFObject::createDictionary(std::move(names)))));
    }
}

void PDFDocumentMergerRun::collectFieldNames(const PDFObject& field, const QString& prefix, int depth, PDFObjectWalk* walk, std::vector<QString>* names) const
{
    if (!walk->enter(field, depth))
    {
        return;
    }
    const PDFDictionary* dictionary = getDictionary(field);
    if (!dictionary)
    {
        return;
    }

    QString name = prefix;
    if (dictionary->hasKey("T"))
    {
        const QString partialName = Helper::getText(getObject(dictionary->get("T")));
        name = prefix.isEmpty() ? partialName : prefix + QLatin1Char('.') + partialName;
        names->push_back(name);
    }
    for (const PDFObject& kid : Helper::getItems(getStorage(), dictionary->get("Kids")))
    {
        collectFieldNames(kid, name, depth + 1, walk, names);
    }
}

void PDFDocumentMergerRun::renameFields(DocumentInput* input, const PDFObject& field, const QString& prefix, int depth, PDFObjectWalk* walk, const std::set<QString>& usedNames, const std::set<QString>& ownNames)
{
    Q_UNUSED(input);

    // Q3: only a field, whose fully qualified name (12.7.4.2) is a name of a field of an input
    // before, is renamed ("Name_2"); its kids follow its new name. A field without a partial
    // name gives its kids its own place in the names. The new name is neither a name of the
    // inputs before nor of this input (its own "Name_2" would join the renamed "Name" into one
    // field, 12.7.4.1); the same name of one input gets the same new name (its fields stay as
    // they are among themselves).
    if (!walk->enter(field, depth))
    {
        return;
    }
    const PDFDictionary* dictionary = getDictionary(field);
    if (!dictionary)
    {
        return;
    }

    if (!dictionary->hasKey("T"))
    {
        for (const PDFObject& kid : Helper::getItems(getStorage(), dictionary->get("Kids")))
        {
            renameFields(input, kid, prefix, depth + 1, walk, usedNames, ownNames);
        }
        return;
    }

    const QString partialName = Helper::getText(getObject(dictionary->get("T")));
    auto join = [&prefix](const QString& name) { return prefix.isEmpty() ? name : prefix + QLatin1Char('.') + name; };
    if (!usedNames.count(join(partialName)) || !field.isReference())
    {
        return;
    }

    QString newName;
    for (int counter = 2; ; ++counter)
    {
        newName = QStringLiteral("%1_%2").arg(partialName).arg(counter);
        if (!usedNames.count(join(newName)) && !ownNames.count(join(newName)))
        {
            break;
        }
    }

    PDFDictionaryBuilder builder(*dictionary);
    builder.setEntry(Helper::key("T"), PDFObjectFactory::createTextString(newName));
    m_builder.setObject(field.getReference(), PDFObject::createDictionary(std::move(builder)));
    ++m_statistics.fieldsRenamed;
}

void PDFDocumentMergerRun::pushDefaultAppearance(const PDFObject& field, const QByteArray& appearance, const std::optional<PDFInteger>& quadding, int depth, PDFObjectWalk* walk, bool hasAppearance, bool hasQuadding, const QByteArray& fieldType)
{
    // 12.7.4.3: a field of variable text without its own /DA and /Q used the ones of its
    // AcroForm; when the AcroForm of the result has others, the field gets them itself (R8)
    if (!walk->enter(field, depth))
    {
        return;
    }
    const PDFDictionary* dictionary = getDictionary(field);
    if (!dictionary)
    {
        return;
    }

    const QByteArray type = dictionary->hasKey("FT") ? Helper::getName(dictionary->get("FT")) : fieldType;
    hasAppearance = hasAppearance || dictionary->hasKey("DA");
    hasQuadding = hasQuadding || dictionary->hasKey("Q");

    bool hasFieldKids = false;
    for (const PDFObject& kid : Helper::getItems(getStorage(), dictionary->get("Kids")))
    {
        const PDFDictionary* kidDictionary = getDictionary(kid);
        if (kidDictionary && (kidDictionary->hasKey("T") || kidDictionary->hasKey("FT") || kidDictionary->hasKey("Kids")))
        {
            hasFieldKids = true;
            pushDefaultAppearance(kid, appearance, quadding, depth + 1, walk, hasAppearance, hasQuadding, type);
        }
    }

    if (hasFieldKids || (type != "Tx" && type != "Ch") || !field.isReference())
    {
        return;
    }

    PDFDictionaryBuilder builder(*dictionary);
    bool isChanged = false;
    if (!hasAppearance && !appearance.isEmpty())
    {
        builder.setEntry(Helper::key("DA"), PDFObject::createString(appearance));
        isChanged = true;
    }
    if (!hasQuadding && quadding)
    {
        builder.setEntry(Helper::key("Q"), PDFObject::createInteger(*quadding));
        isChanged = true;
    }
    if (isChanged)
    {
        m_builder.setObject(field.getReference(), PDFObject::createDictionary(std::move(builder)));
    }
}

void PDFDocumentMergerRun::mergeForm(PDFDictionaryBuilder* catalog)
{
    // 12.7.3, Table 224
    std::vector<PDFObject> fields;
    std::vector<PDFObject> calculationOrder;
    std::map<QByteArray, PDFDictionaryBuilder> resources;
    std::map<QByteArray, std::set<QByteArray>> resourceNames;
    std::map<QByteArray, std::map<QByteArray, int>> resourceCounters;
    std::set<QString> usedNames;
    bool hasForm = false;
    bool needAppearances = false;
    std::optional<QByteArray> resultAppearance;
    std::optional<PDFInteger> resultQuadding;

    for (const std::unique_ptr<DocumentInput>& input : m_documents)
    {
        const PDFDictionary* inputCatalog = input->isLeftOut ? nullptr : getDictionary(input->catalog);
        const PDFDictionary* acroForm = inputCatalog ? getDictionary(inputCatalog->get("AcroForm")) : nullptr;
        if (!acroForm)
        {
            continue;
        }
        hasForm = true;

        m_statistics.xfaRemoved = m_statistics.xfaRemoved || !getObject(acroForm->get("XFA")).isNull();
        const PDFObject& needAppearancesObject = getObject(acroForm->get("NeedAppearances"));
        needAppearances = needAppearances || (needAppearancesObject.isBool() && needAppearancesObject.getBool());

        // /DR: the same name with another object is renamed in the input (its /DA follow)
        if (const PDFDictionary* inputResources = getDictionary(acroForm->get("DR")))
        {
            for (size_t i = 0; i < inputResources->getCount(); ++i)
            {
                const QByteArray category = inputResources->getKey(i).getString();
                const PDFDictionary* entries = getDictionary(inputResources->getValue(i));
                if (!entries)
                {
                    continue;
                }
                PDFDictionaryBuilder& merged = resources[category];
                for (size_t j = 0; j < entries->getCount(); ++j)
                {
                    const QByteArray name = entries->getKey(j).getString();
                    const PDFObject& value = entries->getValue(j);
                    if (resourceNames[category].count(name) && merged.get(name) == value)
                    {
                        continue;
                    }
                    const QByteArray newName = Helper::createUniqueKey(name, resourceNames[category], &resourceCounters[category], Helper::MAXIMUM_NAME_LENGTH);
                    resourceNames[category].insert(newName);
                    merged.addEntry(PDFInplaceOrMemoryString(newName), value);
                    if (newName != name)
                    {
                        ++m_statistics.resourcesRenamed;
                        if (category == "Font")
                        {
                            input->fontRenames[name] = newName;
                        }
                    }
                }
            }
        }

        const PDFObject& appearanceObject = getObject(acroForm->get("DA"));
        const std::optional<QByteArray> appearance = appearanceObject.isString() ? std::optional<QByteArray>(Helper::renameDefaultAppearanceFont(appearanceObject.getString(), input->fontRenames)) : std::nullopt;
        const PDFObject& quaddingObject = getObject(acroForm->get("Q"));
        const PDFInteger quadding = quaddingObject.isInt() ? quaddingObject.getInteger() : 0;
        const bool isFirstForm = !resultAppearance && !resultQuadding && fields.empty();
        if (isFirstForm)
        {
            resultAppearance = appearance;
            resultQuadding = quadding;
        }
        if (!resultAppearance && appearance)
        {
            resultAppearance = appearance;
        }

        std::vector<PDFObject> inputFields = Helper::getItems(getStorage(), replaceSplitFields(acroForm->get("Fields")));

        // Names collide only with the fields of the inputs before (the fields of one input are its
        // own); a new name avoids the names of this input too
        std::vector<QString> inputNames;
        {
            std::vector<QString> originalNames;
            PDFObjectWalk originalWalk(Helper::MAXIMUM_TREE_DEPTH);
            for (const PDFObject& field : inputFields)
            {
                collectFieldNames(field, QString(), 0, &originalWalk, &originalNames);
            }
            const std::set<QString> ownNames(originalNames.cbegin(), originalNames.cend());

            PDFObjectWalk renameWalk(Helper::MAXIMUM_TREE_DEPTH);
            for (const PDFObject& field : inputFields)
            {
                renameFields(input.get(), field, QString(), 0, &renameWalk, usedNames, ownNames);
            }
            PDFObjectWalk nameWalk(Helper::MAXIMUM_TREE_DEPTH);
            for (const PDFObject& field : inputFields)
            {
                collectFieldNames(field, QString(), 0, &nameWalk, &inputNames);
            }
        }
        usedNames.insert(inputNames.cbegin(), inputNames.cend());

        const bool isAppearanceDifferent = appearance && (!resultAppearance || *resultAppearance != *appearance);
        const bool isQuaddingDifferent = quadding != resultQuadding.value_or(0);
        if (!isFirstForm && (isAppearanceDifferent || isQuaddingDifferent))
        {
            PDFObjectWalk walk(Helper::MAXIMUM_TREE_DEPTH);
            for (const PDFObject& field : inputFields)
            {
                pushDefaultAppearance(field, isAppearanceDifferent ? *appearance : QByteArray(), isQuaddingDifferent ? std::optional<PDFInteger>(quadding) : std::nullopt, 0, &walk, false, false, QByteArray());
            }
        }

        const std::vector<PDFObject> order = Helper::getItems(getStorage(), acroForm->get("CO"));
        calculationOrder.insert(calculationOrder.end(), order.cbegin(), order.cend());
        fields.insert(fields.end(), inputFields.cbegin(), inputFields.cend());
    }

    if (!hasForm)
    {
        catalog->removeEntry("AcroForm");
        return;
    }

    removeSignatures(fields);

    // /SigFlags: bit 1 while a signature field stays (also an empty one, Q4), never bit 2
    bool hasSignatureField = false;
    {
        PDFObjectWalk walk(Helper::MAXIMUM_TREE_DEPTH);
        std::vector<PDFObject> terminals;
        for (const PDFObject& field : fields)
        {
            collectTerminalFields(field, 0, &walk, &terminals);
        }
        for (const PDFObject& terminal : terminals)
        {
            PDFObject current = terminal;
            for (int depth = 0; depth < Helper::MAXIMUM_TREE_DEPTH && !hasSignatureField; ++depth)
            {
                const PDFDictionary* dictionary = getDictionary(current);
                if (!dictionary)
                {
                    break;
                }
                if (dictionary->hasKey("FT"))
                {
                    hasSignatureField = Helper::getName(dictionary->get("FT")) == "Sig";
                    break;
                }
                current = dictionary->get("Parent");
            }
        }
    }

    PDFDictionaryBuilder form;
    form.addEntry(Helper::key("Fields"), Helper::createArray(fields));
    if (needAppearances)
    {
        form.addEntry(Helper::key("NeedAppearances"), PDFObject::createBool(true));
    }
    if (hasSignatureField)
    {
        form.addEntry(Helper::key("SigFlags"), PDFObject::createInteger(1));
    }
    if (!calculationOrder.empty())
    {
        form.addEntry(Helper::key("CO"), Helper::createArray(calculationOrder));
    }
    if (!resources.empty())
    {
        PDFDictionaryBuilder resourceDictionary;
        for (auto& [category, entries] : resources)
        {
            resourceDictionary.addEntry(PDFInplaceOrMemoryString(category), PDFObject::createDictionary(std::move(entries)));
        }
        form.addEntry(Helper::key("DR"), PDFObject::createDictionary(std::move(resourceDictionary)));
    }
    if (resultAppearance)
    {
        form.addEntry(Helper::key("DA"), PDFObject::createString(*resultAppearance));
    }
    if (resultQuadding.value_or(0) != 0)
    {
        form.addEntry(Helper::key("Q"), PDFObject::createInteger(*resultQuadding));
    }
    catalog->setEntry(Helper::key("AcroForm"), PDFObject::createReference(m_builder.addObject(PDFObject::createDictionary(std::move(form)))));
}

void PDFDocumentMergerRun::mergeLayers(PDFDictionaryBuilder* catalog)
{
    // 8.11.4.2, 8.11.4.3: the groups of all inputs; the default configuration of the first, and
    // the default state of each group of the other inputs written into /ON or /OFF, so that it
    // stays the same whatever /BaseState the result has (R10)
    struct Layers
    {
        const DocumentInput* input = nullptr;
        const PDFDictionary* properties = nullptr;
        const PDFDictionary* configuration = nullptr;
        std::vector<PDFObject> groups;      ///< /OCGs of the input and the groups its content uses besides them
    };

    std::vector<Layers> inputs;
    for (const std::unique_ptr<DocumentInput>& input : m_documents)
    {
        if (input->isLeftOut)
        {
            continue;
        }
        const PDFDictionary* inputCatalog = getDictionary(input->catalog);
        const PDFDictionary* properties = inputCatalog ? getDictionary(inputCatalog->get("OCProperties")) : nullptr;

        Layers layers;
        layers.input = input.get();
        layers.properties = properties;
        layers.configuration = properties ? getDictionary(properties->get("D")) : nullptr;
        layers.groups = properties ? Helper::getItems(getStorage(), properties->get("OCGs")) : std::vector<PDFObject>();
        const std::set<PDFObjectReference> listed = Helper::getReferences(layers.groups);
        for (const PDFObject& group : collectUsedLayers(input.get()))
        {
            if (!listed.count(group.getReference()))
            {
                layers.groups.push_back(group);
            }
        }

        if (properties || !layers.groups.empty())
        {
            m_statistics.layerConfigurationsRemoved = m_statistics.layerConfigurationsRemoved || (properties && !Helper::getItems(getStorage(), properties->get("Configs")).empty());
            inputs.push_back(std::move(layers));
        }
    }

    if (inputs.empty())
    {
        catalog->removeEntry("OCProperties");
        return;
    }

    std::vector<PDFObject> groups;
    std::set<PDFObjectReference> groupSet;
    std::vector<PDFObject> on;
    std::vector<PDFObject> off;
    std::vector<PDFObject> order;
    std::vector<PDFObject> radioButtonGroups;
    std::vector<PDFObject> locked;
    std::vector<PDFObject> usage;
    bool hasOrder = false;
    for (const Layers& layers : inputs)
    {
        hasOrder = hasOrder || (layers.configuration && layers.configuration->hasKey("Order"));
    }

    for (size_t i = 0; i < inputs.size(); ++i)
    {
        const Layers& layers = inputs[i];
        const std::vector<PDFObject>& inputGroups = layers.groups;
        for (const PDFObject& group : inputGroups)
        {
            if (group.isReference() && groupSet.insert(group.getReference()).second)
            {
                groups.push_back(group);
            }
        }

        const PDFDictionary* configuration = layers.configuration;
        const std::vector<PDFObject> inputOn = configuration ? Helper::getItems(getStorage(), configuration->get("ON")) : std::vector<PDFObject>();
        const std::vector<PDFObject> inputOff = configuration ? Helper::getItems(getStorage(), configuration->get("OFF")) : std::vector<PDFObject>();
        if (i == 0)
        {
            on = inputOn;
            off = inputOff;
        }
        else
        {
            const std::set<PDFObjectReference> onSet = Helper::getReferences(inputOn);
            const std::set<PDFObjectReference> offSet = Helper::getReferences(inputOff);
            const bool isBaseOff = configuration && Helper::getName(configuration->get("BaseState")) == "OFF";
            for (const PDFObject& group : inputGroups)
            {
                if (!group.isReference())
                {
                    continue;
                }
                const bool isOff = offSet.count(group.getReference()) || (!onSet.count(group.getReference()) && isBaseOff);
                (isOff ? off : on).push_back(group);
            }
        }

        if (configuration)
        {
            const std::vector<PDFObject> rbGroups = Helper::getItems(getStorage(), configuration->get("RBGroups"));
            radioButtonGroups.insert(radioButtonGroups.end(), rbGroups.cbegin(), rbGroups.cend());
            const std::vector<PDFObject> inputLocked = Helper::getItems(getStorage(), configuration->get("Locked"));
            locked.insert(locked.end(), inputLocked.cbegin(), inputLocked.cend());
            const std::vector<PDFObject> inputUsage = Helper::getItems(getStorage(), configuration->get("AS"));
            usage.insert(usage.end(), inputUsage.cbegin(), inputUsage.cend());
        }

        if (hasOrder)
        {
            // Table 101 /Order: an array, whose first item is a text, is a labeled group; the
            // layers of each input are under its name
            std::vector<PDFObject> inputOrder = (configuration && configuration->hasKey("Order")) ? Helper::getItems(getStorage(), configuration->get("Order")) : inputGroups;
            if (inputs.size() > 1)
            {
                const QString label = m_sources[layers.input->source].name.isEmpty() ? layers.input->title : m_sources[layers.input->source].name;
                inputOrder.insert(inputOrder.begin(), PDFObjectFactory::createTextString(label));
                order.push_back(Helper::createArray(inputOrder));
            }
            else
            {
                order = inputOrder;
            }
        }
    }

    PDFDictionaryBuilder configuration = inputs.front().configuration ? PDFDictionaryBuilder(*inputs.front().configuration) : PDFDictionaryBuilder();
    configuration.setEntry(Helper::key("ON"), Helper::createArray(on));
    configuration.setEntry(Helper::key("OFF"), Helper::createArray(off));
    if (hasOrder)
    {
        configuration.setEntry(Helper::key("Order"), Helper::createArray(order));
    }
    for (const auto& [key, items] : { std::pair<const char*, const std::vector<PDFObject>*>("RBGroups", &radioButtonGroups),
                                      std::pair<const char*, const std::vector<PDFObject>*>("Locked", &locked),
                                      std::pair<const char*, const std::vector<PDFObject>*>("AS", &usage) })
    {
        if (items->empty())
        {
            configuration.removeEntry(key);
        }
        else
        {
            configuration.setEntry(Helper::key(key), Helper::createArray(*items));
        }
    }

    PDFDictionaryBuilder properties;
    properties.addEntry(Helper::key("OCGs"), Helper::createArray(groups));
    properties.addEntry(Helper::key("D"), PDFObject::createDictionary(std::move(configuration)));
    catalog->setEntry(Helper::key("OCProperties"), PDFObject::createReference(m_builder.addObject(PDFObject::createDictionary(std::move(properties)))));
}

void PDFDocumentMergerRun::collectLayers(const PDFObject& object, int depth, bool isGroupList, std::vector<PDFObject>* groups, std::set<PDFObjectReference>* found) const
{
    if (depth > Helper::MAXIMUM_TREE_DEPTH)
    {
        return;
    }

    if (object.isReference())
    {
        // A reference is a group only in a list of groups (/OCGs, /VE of an OCMD, /OC, /Properties)
        const PDFDictionary* dictionary = isGroupList ? getDictionary(object) : nullptr;
        if (dictionary && Helper::getName(dictionary->get("Type")) == "OCG" && found->insert(object.getReference()).second)
        {
            groups->push_back(object);
        }
        return;
    }

    if (object.isArray())
    {
        const PDFArray* array = object.getArray();
        for (size_t i = 0; i < array->getCount(); ++i)
        {
            collectLayers(array->getItem(i), depth + 1, isGroupList, groups, found);
        }
        return;
    }

    const PDFDictionary* dictionary = object.isDictionary() ? object.getDictionary() : (object.isStream() ? object.getStream()->getDictionary() : nullptr);
    if (!dictionary)
    {
        return;
    }

    const bool isMembership = Helper::getName(dictionary->get("Type")) == "OCMD";
    for (size_t i = 0; i < dictionary->getCount(); ++i)
    {
        const QByteArray key = dictionary->getKey(i).getString();
        const PDFObject& value = dictionary->getValue(i);
        const bool isList = key == "OC" || key == "Properties" || (isMembership && (key == "OCGs" || key == "VE"));
        if (isList && value.isReference() && !getDictionary(value))
        {
            continue;
        }
        if (isList && value.isReference() && Helper::getName(getDictionary(value)->get("Type")) == "OCG")
        {
            collectLayers(value, depth + 1, true, groups, found);
        }
        else if (isList && key == "Properties" && getDictionary(value))
        {
            // /Properties of resources: its values are groups and membership dictionaries
            const PDFDictionary* properties = getDictionary(value);
            for (size_t j = 0; j < properties->getCount(); ++j)
            {
                collectLayers(properties->getValue(j), depth + 1, true, groups, found);
            }
        }
        else
        {
            collectLayers(value, depth + 1, isList, groups, found);
        }
    }
}

std::vector<PDFObject> PDFDocumentMergerRun::collectUsedLayers(const DocumentInput* input) const
{
    // 8.11.2.2: a group, which the content or an annotation uses (by /OC, /Properties of the
    // resources, /OCGs and /VE of a membership dictionary), belongs to the input also when its
    // /OCGs does not list it (R10); each object of the input is read once
    std::vector<PDFObject> groups;
    std::set<PDFObjectReference> found;
    for (const PDFObjectReference& reference : input->objectMap)
    {
        if (reference.isValid())
        {
            collectLayers(m_builder.getObjectByReference(reference), 0, false, &groups, &found);
        }
    }
    return groups;
}

void PDFDocumentMergerRun::mergeStructure(PDFDictionaryBuilder* catalog)
{
    // Owner answer Q1, R9: joined only when every PDF input is tagged and taken whole; the result
    // has one new Document, the top elements of each input are under a DocumentFragment of the
    // namespace of PDF 2.0 (14.8.4, Annex L) with the language and the title of the input
    std::vector<DocumentInput*> inputs;
    bool isTaggedWhole = true;
    bool isAnyTagged = false;
    std::set<QString> languages;
    for (const std::unique_ptr<DocumentInput>& input : m_documents)
    {
        if (input->isLeftOut)
        {
            continue;
        }
        const PDFDictionary* inputCatalog = getDictionary(input->catalog);
        const bool isTagged = inputCatalog && getDictionary(inputCatalog->get("StructTreeRoot"));
        isAnyTagged = isAnyTagged || isTagged || input->hadStructureTree;
        isTaggedWhole = isTaggedWhole && isTagged && input->isWhole;
        languages.insert(inputCatalog ? Helper::getText(getObject(inputCatalog->get("Lang"))) : QString());
        inputs.push_back(input.get());
    }

    // 14.9.2: one language of all inputs is the language of the result
    const bool hasCommonLanguage = languages.size() == 1 && !languages.begin()->isEmpty();
    if (hasCommonLanguage)
    {
        catalog->setEntry(Helper::key("Lang"), PDFObjectFactory::createTextString(*languages.begin()));
    }

    if (!isTaggedWhole || inputs.empty())
    {
        m_statistics.structureTreeRemoved = isAnyTagged;
        for (DocumentInput* input : inputs)
        {
            input->isStructureStripped = true;
        }
        catalog->removeEntry("StructTreeRoot");
        catalog->removeEntry("MarkInfo");
        return;
    }

    PDFDictionaryBuilder pdf2Namespace;
    pdf2Namespace.addEntry(Helper::key("Type"), PDFObject::createName("Namespace"));
    pdf2Namespace.addEntry(Helper::key("NS"), PDFObject::createString("http://iso.org/pdf2/ssn"));
    const PDFObjectReference pdf2NamespaceReference = m_builder.addObject(PDFObject::createDictionary(std::move(pdf2Namespace)));
    PDFObjectReference pdf17NamespaceReference;

    const PDFObjectReference rootReference = m_builder.addObject(PDFObject());
    const PDFObjectReference documentReference = m_builder.addObject(PDFObject());

    std::vector<PDFObject> fragments;
    std::vector<PDFObject> namespaces = { PDFObject::createReference(pdf2NamespaceReference) };
    std::vector<std::pair<PDFInteger, PDFObject>> parentTree;
    PDFDictionaryBuilder roleMap;
    PDFDictionaryBuilder classMap;
    std::set<QByteArray> classNames;
    std::map<QByteArray, int> classCounters;
    std::map<QByteArray, PDFObject> idTree;
    std::set<QByteArray> ids;
    std::map<QByteArray, int> idCounters;
    PDFInteger nextKey = 0;
    bool isSuspect = false;
    bool hasUserProperties = false;

    for (DocumentInput* input : inputs)
    {
        const PDFDictionary* inputCatalog = getDictionary(input->catalog);
        // A copy of the root: a hostile kid may be the root itself, which is replaced below
        const PDFDictionaryBuilder treeRootCopy(*getDictionary(inputCatalog->get("StructTreeRoot")));
        const PDFDictionary* treeRoot = treeRootCopy.getDictionary();
        if (const PDFDictionary* markInfo = getDictionary(inputCatalog->get("MarkInfo")))
        {
            const PDFObject& suspects = getObject(markInfo->get("Suspects"));
            const PDFObject& userProperties = getObject(markInfo->get("UserProperties"));
            isSuspect = isSuspect || (suspects.isBool() && suspects.getBool());
            hasUserProperties = hasUserProperties || (userProperties.isBool() && userProperties.getBool());
        }

        const PDFObjectReference fragmentReference = m_builder.addObject(PDFObject());
        std::vector<PDFObject> kids;
        for (const PDFObject& kid : Helper::getItems(getStorage(), treeRoot->get("K")))
        {
            const PDFDictionary* element = getDictionary(kid);
            if (!element)
            {
                continue;
            }
            PDFDictionaryBuilder elementBuilder(*element);
            elementBuilder.setEntry(Helper::key("P"), PDFObject::createReference(fragmentReference));
            if (kid.isReference())
            {
                m_builder.setObject(kid.getReference(), PDFObject::createDictionary(std::move(elementBuilder)));
                kids.push_back(kid);
            }
            else
            {
                kids.push_back(PDFObject::createDictionary(std::move(elementBuilder)));
            }
        }

        PDFDictionaryBuilder fragment;
        fragment.addEntry(Helper::key("Type"), PDFObject::createName("StructElem"));
        fragment.addEntry(Helper::key("S"), PDFObject::createName("DocumentFragment"));
        fragment.addEntry(Helper::key("NS"), PDFObject::createReference(pdf2NamespaceReference));
        fragment.addEntry(Helper::key("P"), PDFObject::createReference(documentReference));
        fragment.addEntry(Helper::key("K"), Helper::createArray(kids));
        const QString language = Helper::getText(getObject(inputCatalog->get("Lang")));
        if (!hasCommonLanguage && !language.isEmpty())
        {
            fragment.addEntry(Helper::key("Lang"), PDFObjectFactory::createTextString(language));
        }
        fragment.addEntry(Helper::key("T"), PDFObjectFactory::createTextString(input->title));
        m_builder.setObject(fragmentReference, PDFObject::createDictionary(std::move(fragment)));
        fragments.push_back(PDFObject::createReference(fragmentReference));

        // /ParentTree (14.7.5.4): the keys of the input follow the keys of the inputs before
        const std::vector<std::pair<PDFInteger, PDFObject>> entries = PDFObjectUtils::readNumberTree(getStorage(), treeRoot->get("ParentTree"));
        PDFInteger inputNextKey = 0;
        const PDFObject& nextKeyObject = getObject(treeRoot->get("ParentTreeNextKey"));
        if (nextKeyObject.isInt())
        {
            inputNextKey = qMax<PDFInteger>(0, nextKeyObject.getInteger());
        }
        for (const auto& [key, value] : entries)
        {
            if (key >= 0)
            {
                inputNextKey = qMax(inputNextKey, key + 1);
                parentTree.emplace_back(key + nextKey, value);
            }
        }
        input->structParentOffset = nextKey;
        input->isStructureJoined = true;
        nextKey += inputNextKey;

        const std::vector<PDFObject> inputNamespaces = Helper::getItems(getStorage(), treeRoot->get("Namespaces"));
        namespaces.insert(namespaces.end(), inputNamespaces.cbegin(), inputNamespaces.cend());

        // /RoleMap of PDF 1.7: a type, which another input maps differently, goes into a
        // namespace of this input (14.7.4.4, /RoleMapNS)
        PDFDictionaryBuilder inputRoleMap;
        if (const PDFDictionary* inputRoles = getDictionary(treeRoot->get("RoleMap")))
        {
            for (size_t i = 0; i < inputRoles->getCount(); ++i)
            {
                const QByteArray type = inputRoles->getKey(i).getString();
                const PDFObject& value = getObject(inputRoles->getValue(i));
                if (!roleMap.hasKey(type))
                {
                    roleMap.addEntry(inputRoles->getKey(i), value);
                }
                else if (roleMap.get(type) != value)
                {
                    input->namespaceTypes.insert(type);
                    inputRoleMap.addEntry(inputRoles->getKey(i), value);
                }
            }
        }
        if (!input->namespaceTypes.empty())
        {
            if (!pdf17NamespaceReference.isValid())
            {
                PDFDictionaryBuilder pdf17Namespace;
                pdf17Namespace.addEntry(Helper::key("Type"), PDFObject::createName("Namespace"));
                pdf17Namespace.addEntry(Helper::key("NS"), PDFObject::createString("http://iso.org/pdf/ssn"));
                pdf17NamespaceReference = m_builder.addObject(PDFObject::createDictionary(std::move(pdf17Namespace)));
            }

            PDFDictionaryBuilder roleMapNamespace;
            for (size_t i = 0; i < inputRoleMap.getCount(); ++i)
            {
                const PDFObject& value = inputRoleMap.getValue(i);
                const bool isOwnType = value.isName() && input->namespaceTypes.count(value.getString());
                roleMapNamespace.addEntry(inputRoleMap.getKey(i), Helper::createArray({ value, PDFObject::createReference(isOwnType ? PDFObjectReference() : pdf17NamespaceReference) }));
            }

            PDFDictionaryBuilder inputNamespace;
            inputNamespace.addEntry(Helper::key("Type"), PDFObject::createName("Namespace"));
            inputNamespace.addEntry(Helper::key("NS"), PDFObject::createString("urn:pdf4qt:merge:input:" + QByteArray::number(qint64(input->source) + 1)));
            const PDFObjectReference inputNamespaceReference = m_builder.addObject(PDFObject());
            // A type mapped to another own type stays in the own namespace
            PDFDictionaryBuilder fixedRoleMap;
            for (size_t i = 0; i < roleMapNamespace.getCount(); ++i)
            {
                const PDFObject& mapping = roleMapNamespace.getValue(i);
                const PDFArray* array = mapping.getArray();
                const PDFObject namespaceObject = array->getItem(1).isReference() && array->getItem(1).getReference().isValid() ? array->getItem(1) : PDFObject::createReference(inputNamespaceReference);
                fixedRoleMap.addEntry(roleMapNamespace.getKey(i), Helper::createArray({ array->getItem(0), namespaceObject }));
            }
            inputNamespace.addEntry(Helper::key("RoleMapNS"), PDFObject::createDictionary(std::move(fixedRoleMap)));
            m_builder.setObject(inputNamespaceReference, PDFObject::createDictionary(std::move(inputNamespace)));
            input->roleNamespace = inputNamespaceReference;
            namespaces.push_back(PDFObject::createReference(inputNamespaceReference));
        }

        // /ClassMap and /IDTree: a collision is renamed, the elements of the input follow
        if (const PDFDictionary* inputClasses = getDictionary(treeRoot->get("ClassMap")))
        {
            for (size_t i = 0; i < inputClasses->getCount(); ++i)
            {
                const QByteArray name = inputClasses->getKey(i).getString();
                const QByteArray newName = Helper::createUniqueKey(name, classNames, &classCounters, Helper::MAXIMUM_NAME_LENGTH);
                classNames.insert(newName);
                classMap.addEntry(PDFInplaceOrMemoryString(newName), inputClasses->getValue(i));
                if (newName != name)
                {
                    input->classRenames[name] = newName;
                }
            }
        }
        for (const auto& [name, value] : Helper::readNameTree(getStorage(), treeRoot->get("IDTree")))
        {
            const QByteArray newName = Helper::createUniqueKey(name, ids, &idCounters);
            ids.insert(newName);
            idTree.emplace(newName, value);
            if (newName != name)
            {
                input->idRenames[name] = newName;
            }
        }
    }

    PDFDictionaryBuilder document;
    document.addEntry(Helper::key("Type"), PDFObject::createName("StructElem"));
    document.addEntry(Helper::key("S"), PDFObject::createName("Document"));
    document.addEntry(Helper::key("NS"), PDFObject::createReference(pdf2NamespaceReference));
    document.addEntry(Helper::key("P"), PDFObject::createReference(rootReference));
    document.addEntry(Helper::key("K"), Helper::createArray(fragments));
    m_builder.setObject(documentReference, PDFObject::createDictionary(std::move(document)));

    PDFDictionaryBuilder root;
    root.addEntry(Helper::key("Type"), PDFObject::createName("StructTreeRoot"));
    root.addEntry(Helper::key("K"), PDFObject::createReference(documentReference));
    std::sort(parentTree.begin(), parentTree.end(), [](const auto& left, const auto& right) { return left.first < right.first; });
    root.addEntry(Helper::key("ParentTree"), PDFObject::createReference(m_builder.addObject(Helper::createNumberTree(parentTree))));
    root.addEntry(Helper::key("ParentTreeNextKey"), PDFObject::createInteger(nextKey));
    root.addEntry(Helper::key("Namespaces"), Helper::createArray(namespaces));
    if (!roleMap.isEmpty())
    {
        root.addEntry(Helper::key("RoleMap"), PDFObject::createDictionary(std::move(roleMap)));
    }
    if (!classMap.isEmpty())
    {
        root.addEntry(Helper::key("ClassMap"), PDFObject::createDictionary(std::move(classMap)));
    }
    if (!idTree.empty())
    {
        root.addEntry(Helper::key("IDTree"), PDFObject::createReference(m_builder.addObject(Helper::createNameTree(idTree))));
    }
    m_builder.setObject(rootReference, PDFObject::createDictionary(std::move(root)));

    PDFDictionaryBuilder markInfo;
    markInfo.addEntry(Helper::key("Marked"), PDFObject::createBool(true));
    if (isSuspect)
    {
        markInfo.addEntry(Helper::key("Suspects"), PDFObject::createBool(true));
    }
    if (hasUserProperties)
    {
        markInfo.addEntry(Helper::key("UserProperties"), PDFObject::createBool(true));
    }
    catalog->setEntry(Helper::key("StructTreeRoot"), PDFObject::createReference(rootReference));
    catalog->setEntry(Helper::key("MarkInfo"), PDFObject::createDictionary(std::move(markInfo)));
    m_statistics.structureTreeMerged = true;
    m_isStructureJoined = true;
}

void PDFDocumentMergerRun::mergeOutline(PDFDictionaryBuilder* catalog)
{
    // 12.3.3, Table 151; owner answer Q7, K14: an item per run of pages of one input
    catalog->removeEntry("Outlines");
    if (m_settings.outline == Merger::OutlineMode::NoOutline || m_runs.empty())
    {
        return;
    }

    /// An item of the outline of an input, read once (one walk over the whole outline: a cycle
    /// or an item shared by two parents is read once, B10)
    struct Node
    {
        PDFObject dictionary;
        std::vector<size_t> children;
        PDFObjectReference target;
        bool hasTarget = false;
        bool isOpen = false;
    };

    /// A written item: its reference and its visible descendants (when open)
    struct Written
    {
        PDFObjectReference reference;
        PDFInteger count = 0;
    };

    std::map<const DocumentInput*, std::vector<Node>> outlines;
    std::map<const DocumentInput*, std::vector<size_t>> topItems;
    std::map<const DocumentInput*, size_t> firstRuns;

    // The outlines of the inputs are read for Join, and for Document parts of one PDF with images (Q18)
    const bool isInputOutlineKept = m_settings.outline == Merger::OutlineMode::Join || m_isSingleWithImages;
    if (isInputOutlineKept)
    {
        for (const std::unique_ptr<DocumentInput>& input : m_documents)
        {
            const PDFDictionary* inputCatalog = input->isLeftOut ? nullptr : getDictionary(input->catalog);
            const PDFDictionary* outline = inputCatalog ? getDictionary(inputCatalog->get("Outlines")) : nullptr;
            if (!outline)
            {
                continue;
            }

            std::vector<Node>& nodes = outlines[input.get()];
            PDFObjectWalk walk(Helper::MAXIMUM_TREE_DEPTH);
            std::function<void(const PDFObject&, int, std::vector<size_t>*)> readSiblings = [&](const PDFObject& first, int depth, std::vector<size_t>* siblings)
            {
                PDFObject current = first;
                while (current.isReference() && walk.enter(current, depth))
                {
                    const PDFDictionary* item = getDictionary(current);
                    if (!item)
                    {
                        break;
                    }
                    const size_t index = nodes.size();
                    nodes.push_back(Node());
                    nodes[index].dictionary = getObject(current);
                    nodes[index].hasTarget = item->hasKey("Dest") || item->hasKey("A");
                    nodes[index].target = getTargetPage(input.get(), item, input->destinations);
                    const PDFObject& count = getObject(item->get("Count"));
                    nodes[index].isOpen = count.isInt() && count.getInteger() > 0;
                    siblings->push_back(index);

                    std::vector<size_t> children;
                    readSiblings(item->get("First"), depth + 1, &children);
                    nodes[index].children = std::move(children);
                    current = item->get("Next");
                }
            };
            readSiblings(outline->get("First"), 0, &topItems[input.get()]);
        }
    }

    // Writes the kept items of a level; returns the written items
    std::function<std::vector<Written>(DocumentInput*, const std::vector<size_t>&, const std::set<PDFObjectReference>&, bool, int)> writeItems =
        [&](DocumentInput* input, const std::vector<size_t>& indices, const std::set<PDFObjectReference>& runPages, bool isFirstRun, int depth) -> std::vector<Written>
    {
        std::vector<Written> written;
        if (depth > Helper::MAXIMUM_TREE_DEPTH)
        {
            return written;
        }

        const std::vector<Node>& nodes = outlines[input];
        for (const size_t index : indices)
        {
            const Node& node = nodes[index];
            std::vector<Written> children = writeItems(input, node.children, runPages, isFirstRun, depth + 1);
            const bool isPageTarget = node.target.isValid();
            const bool keepsTarget = node.hasTarget && (isPageTarget ? runPages.count(node.target) > 0 : isFirstRun);
            if (!keepsTarget && children.empty())
            {
                // An item, which leads outside of the pages of the run, and has nothing below it
                continue;
            }

            bool isChanged = false;
            PDFObject rewritten = rewriteObject(input, node.dictionary, 0, &isChanged);
            PDFDictionaryBuilder item(*rewritten.getDictionary());
            for (const char* key : { "Parent", "Prev", "Next", "First", "Last", "Count" })
            {
                item.removeEntry(key);
            }
            if (!keepsTarget)
            {
                item.removeEntry("Dest");
                item.removeEntry("A");
            }

            Written result;
            result.reference = m_builder.addObject(PDFObject());
            PDFInteger descendants = 0;
            for (const Written& child : children)
            {
                descendants += 1 + qMax<PDFInteger>(0, child.count);
            }
            if (!children.empty())
            {
                item.addEntry(Helper::key("First"), PDFObject::createReference(children.front().reference));
                item.addEntry(Helper::key("Last"), PDFObject::createReference(children.back().reference));
                result.count = node.isOpen ? descendants : -descendants;
                item.addEntry(Helper::key("Count"), PDFObject::createInteger(result.count));
            }
            for (size_t i = 0; i < children.size(); ++i)
            {
                PDFDictionaryBuilder links;
                links.addEntry(Helper::key("Parent"), PDFObject::createReference(result.reference));
                if (i > 0)
                {
                    links.addEntry(Helper::key("Prev"), PDFObject::createReference(children[i - 1].reference));
                }
                if (i + 1 < children.size())
                {
                    links.addEntry(Helper::key("Next"), PDFObject::createReference(children[i + 1].reference));
                }
                m_builder.mergeTo(children[i].reference, PDFObject::createDictionary(std::move(links)));
            }
            m_builder.setObject(result.reference, PDFObject::createDictionary(std::move(item)));
            written.push_back(result);
        }
        return written;
    };

    const PDFObjectReference outlineReference = m_builder.addObject(PDFObject());
    std::vector<Written> runItems;
    std::map<size_t, PDFInteger> runsOfSource;
    for (const Run& run : m_runs)
    {
        ++runsOfSource[run.source];
    }

    for (size_t runIndex = 0; runIndex < m_runs.size(); ++runIndex)
    {
        const Run& run = m_runs[runIndex];
        const Merger::Source& source = m_sources[run.source];
        const ResultPage& firstPage = m_resultPages[run.first];

        QString title;
        DocumentInput* input = firstPage.input;
        switch (source.kind)
        {
            case Merger::Source::Kind::Document:
            {
                title = input->title;
                const size_t sourcePageCount = source.document->getCatalog()->getPageCount();
                if (run.count < sourcePageCount || runsOfSource[run.source] > 1)
                {
                    std::vector<PDFInteger> pages;
                    for (size_t i = run.first; i < run.first + run.count; ++i)
                    {
                        pages.push_back(m_items[m_resultPages[i].item].pageIndex);
                    }
                    std::sort(pages.begin(), pages.end());
                    pages.erase(std::unique(pages.begin(), pages.end()), pages.end());
                    title = PDFDocumentMerger::tr("%1, p. %2").arg(title, PDFDocumentPageOperations::formatPageNumbers(pages, 32));
                }
                break;
            }
            case Merger::Source::Kind::Image:
                title = source.name.isEmpty() ? PDFDocumentMerger::tr("%Ln images", nullptr, int(run.count)) : source.name;
                break;
            case Merger::Source::Kind::Blank:
                title = PDFDocumentMerger::tr("%Ln blank pages", nullptr, int(run.count));
                break;
        }

        std::vector<Written> children;
        if (input && isInputOutlineKept && outlines.count(input))
        {
            std::set<PDFObjectReference> runPages;
            for (size_t i = run.first; i < run.first + run.count; ++i)
            {
                if (!m_resultPages[i].isClone)
                {
                    runPages.insert(m_resultPages[i].page);
                }
            }
            const bool isFirstRun = !firstRuns.count(input);
            firstRuns.emplace(input, runIndex);
            children = writeItems(input, topItems[input], runPages, isFirstRun, 0);
        }

        PDFDictionaryBuilder item;
        item.addEntry(Helper::key("Title"), PDFObjectFactory::createTextString(title));
        item.addEntry(Helper::key("Parent"), PDFObject::createReference(outlineReference));
        item.addEntry(Helper::key("Dest"), Helper::createArray({ PDFObject::createReference(firstPage.page), PDFObject::createName("Fit") }));
        item.addEntry(Helper::key("F"), PDFObject::createInteger(2));

        Written written;
        written.reference = m_builder.addObject(PDFObject());
        if (!children.empty())
        {
            PDFInteger descendants = 0;
            for (const Written& child : children)
            {
                descendants += 1 + qMax<PDFInteger>(0, child.count);
            }
            item.addEntry(Helper::key("First"), PDFObject::createReference(children.front().reference));
            item.addEntry(Helper::key("Last"), PDFObject::createReference(children.back().reference));
            // A part of Join is closed: its outline is one level below (Table 151, a negative count);
            // the part of the one PDF of Document parts (Q18) is open, its outline as it was
            written.count = m_settings.outline == Merger::OutlineMode::Join ? -descendants : descendants;
            item.addEntry(Helper::key("Count"), PDFObject::createInteger(written.count));
            for (size_t i = 0; i < children.size(); ++i)
            {
                PDFDictionaryBuilder links;
                links.addEntry(Helper::key("Parent"), PDFObject::createReference(written.reference));
                if (i > 0)
                {
                    links.addEntry(Helper::key("Prev"), PDFObject::createReference(children[i - 1].reference));
                }
                if (i + 1 < children.size())
                {
                    links.addEntry(Helper::key("Next"), PDFObject::createReference(children[i + 1].reference));
                }
                m_builder.mergeTo(children[i].reference, PDFObject::createDictionary(std::move(links)));
            }
        }
        m_builder.setObject(written.reference, PDFObject::createDictionary(std::move(item)));
        runItems.push_back(written);
    }

    for (size_t i = 0; i < runItems.size(); ++i)
    {
        PDFDictionaryBuilder links;
        if (i > 0)
        {
            links.addEntry(Helper::key("Prev"), PDFObject::createReference(runItems[i - 1].reference));
        }
        if (i + 1 < runItems.size())
        {
            links.addEntry(Helper::key("Next"), PDFObject::createReference(runItems[i + 1].reference));
        }
        if (!links.isEmpty())
        {
            m_builder.mergeTo(runItems[i].reference, PDFObject::createDictionary(std::move(links)));
        }
    }

    PDFDictionaryBuilder outline;
    outline.addEntry(Helper::key("Type"), PDFObject::createName("Outlines"));
    outline.addEntry(Helper::key("First"), PDFObject::createReference(runItems.front().reference));
    outline.addEntry(Helper::key("Last"), PDFObject::createReference(runItems.back().reference));
    PDFInteger visible = 0;
    for (const Written& item : runItems)
    {
        visible += 1 + qMax<PDFInteger>(0, item.count);
    }
    outline.addEntry(Helper::key("Count"), PDFObject::createInteger(visible));
    m_builder.setObject(outlineReference, PDFObject::createDictionary(std::move(outline)));
    catalog->setEntry(Helper::key("Outlines"), PDFObject::createReference(outlineReference));
    m_hasOutline = true;
}

void PDFDocumentMergerRun::mergeOtherKeys(PDFDictionaryBuilder* catalog)
{
    // Table 29, the keys, which are not joined above
    std::vector<const PDFDictionary*> catalogs;
    std::vector<DocumentInput*> inputs;
    for (const std::unique_ptr<DocumentInput>& input : m_documents)
    {
        const PDFDictionary* inputCatalog = input->isLeftOut ? nullptr : getDictionary(input->catalog);
        if (inputCatalog)
        {
            catalogs.push_back(inputCatalog);
            inputs.push_back(input.get());
        }
    }
    if (catalogs.empty())
    {
        return;
    }

    // From the first PDF input: the preferences of the viewer
    for (const char* key : { "ViewerPreferences", "PageLayout", "PageMode", "SpiderInfo" })
    {
        const PDFObject& value = catalogs.front()->get(key);
        if (!value.isNull())
        {
            catalog->setEntry(Helper::key(key), value);
        }
    }
    std::vector<PDFObject> threads;
    std::vector<PDFObject> associatedFiles;
    std::vector<PDFObject> requirements;
    std::map<QByteArray, PDFObject> extensions;
    std::map<QByteArray, PDFInteger> extensionLevels;
    std::set<QString> bases;
    bool hasInputWithoutBase = false;
    std::vector<QByteArray> outputIntents;
    for (size_t i = 0; i < catalogs.size(); ++i)
    {
        const PDFDictionary* inputCatalog = catalogs[i];

        // 12.4.3: the articles of an input taken whole have all their beads on its pages
        const std::vector<PDFObject> inputThreads = Helper::getItems(getStorage(), inputCatalog->get("Threads"));
        threads.insert(threads.end(), inputThreads.cbegin(), inputThreads.cend());
        const std::vector<PDFObject> inputFiles = Helper::getItems(getStorage(), inputCatalog->get("AF"));
        associatedFiles.insert(associatedFiles.end(), inputFiles.cbegin(), inputFiles.cend());
        const std::vector<PDFObject> inputRequirements = Helper::getItems(getStorage(), inputCatalog->get("Requirements"));
        requirements.insert(requirements.end(), inputRequirements.cbegin(), inputRequirements.cend());

        m_statistics.collectionRemoved = m_statistics.collectionRemoved || !getObject(inputCatalog->get("Collection")).isNull();
        m_statistics.scriptsRemoved = m_statistics.scriptsRemoved || !getObject(inputCatalog->get("OpenAction")).isNull() || !getObject(inputCatalog->get("AA")).isNull();

        // 7.12: the extensions of all inputs, the highest level of each prefix
        if (const PDFDictionary* inputExtensions = getDictionary(inputCatalog->get("Extensions")))
        {
            for (size_t j = 0; j < inputExtensions->getCount(); ++j)
            {
                const QByteArray prefix = inputExtensions->getKey(j).getString();
                PDFInteger level = -1;
                for (const PDFObject& extension : Helper::getItems(getStorage(), inputExtensions->getValue(j)))
                {
                    if (const PDFDictionary* dictionary = getDictionary(extension))
                    {
                        const PDFObject& levelObject = getObject(dictionary->get("ExtensionLevel"));
                        level = qMax(level, levelObject.isInt() ? levelObject.getInteger() : 0);
                    }
                }
                if (!extensionLevels.count(prefix) || level > extensionLevels[prefix])
                {
                    extensionLevels[prefix] = level;
                    extensions[prefix] = inputExtensions->getValue(j);
                }
            }
        }

        // 12.6.4.8: the base of the relative URIs
        const PDFDictionary* uri = getDictionary(inputCatalog->get("URI"));
        const QString base = uri ? Helper::getText(getObject(uri->get("Base"))) : QString();
        if (base.isEmpty())
        {
            hasInputWithoutBase = true;
        }
        else
        {
            bases.insert(base);
        }

        // 14.11.5: the output intents, by their kind and profile
        QByteArray intents;
        for (const PDFObject& intent : Helper::getItems(getStorage(), inputCatalog->get("OutputIntents")))
        {
            if (const PDFDictionary* dictionary = getDictionary(intent))
            {
                intents += Helper::getName(dictionary->get("S")) + "|";
                intents += getObject(dictionary->get("OutputConditionIdentifier")).isString() ? getObject(dictionary->get("OutputConditionIdentifier")).getString() : QByteArray();
                const PDFObject& profile = getObject(dictionary->get("DestOutputProfile"));
                if (profile.isStream())
                {
                    intents += "|" + QCryptographicHash::hash(*profile.getStream()->getContent(), QCryptographicHash::Sha256);
                }
                intents += ";";
            }
        }
        outputIntents.push_back(intents);
    }

    if (!threads.empty())
    {
        catalog->setEntry(Helper::key("Threads"), Helper::createArray(threads));
    }
    if (!associatedFiles.empty())
    {
        catalog->setEntry(Helper::key("AF"), Helper::createArray(associatedFiles));
    }
    if (!requirements.empty())
    {
        catalog->setEntry(Helper::key("Requirements"), Helper::createArray(requirements));
    }
    if (!extensions.empty())
    {
        PDFDictionaryBuilder extensionDictionary;
        for (const auto& [prefix, extension] : extensions)
        {
            extensionDictionary.addEntry(PDFInplaceOrMemoryString(prefix), extension);
        }
        catalog->setEntry(Helper::key("Extensions"), PDFObject::createDictionary(std::move(extensionDictionary)));
    }

    if (bases.size() == 1 && !hasInputWithoutBase)
    {
        catalog->setEntry(Helper::key("URI"), catalogs.front()->get("URI"));
    }
    else if (!bases.empty())
    {
        for (size_t i = 0; i < catalogs.size(); ++i)
        {
            const PDFDictionary* uri = getDictionary(catalogs[i]->get("URI"));
            inputs[i]->uriBase = uri ? Helper::getText(getObject(uri->get("Base"))) : QString();
        }
    }

    const bool isOutputIntentCommon = std::all_of(outputIntents.cbegin(), outputIntents.cend(), [&outputIntents](const QByteArray& intents) { return intents == outputIntents.front(); });
    if (!outputIntents.front().isEmpty() && isOutputIntentCommon)
    {
        catalog->setEntry(Helper::key("OutputIntents"), catalogs.front()->get("OutputIntents"));
    }
    else if (std::any_of(outputIntents.cbegin(), outputIntents.cend(), [](const QByteArray& intents) { return !intents.isEmpty(); }))
    {
        m_statistics.outputIntentsRemoved = true;
    }
}

PDFObject PDFDocumentMergerRun::rewriteDestination(const DocumentInput* input, const PDFObject& destination, bool* isChanged) const
{
    // A name (of /Dests, PDF 1.1) or a string (of the name tree): the result has only the
    // tree, so a name becomes a string of its key (12.3.2.3)
    if ((destination.isName() || destination.isString()) && (destination.isString() || input->legacyDestinations.count(destination.getString())))
    {
        auto it = input->destinationRenames.find(destination.getString());
        if (it != input->destinationRenames.cend())
        {
            *isChanged = true;
            return PDFObject::createString(it->second);
        }
    }
    return destination;
}

PDFObject PDFDocumentMergerRun::rewriteObject(DocumentInput* input, const PDFObject& object, int depth, bool* isChanged) const
{
    if (depth > Helper::MAXIMUM_OBJECT_DEPTH)
    {
        return object;
    }

    switch (object.getType())
    {
        case PDFObject::Type::Array:
        {
            const PDFArray* array = object.getArray();
            bool isArrayChanged = false;
            PDFArrayBuilder builder;
            builder.reserve(array->getCount());
            for (size_t i = 0; i < array->getCount(); ++i)
            {
                builder.appendItem(rewriteObject(input, array->getItem(i), depth + 1, &isArrayChanged));
            }
            if (!isArrayChanged)
            {
                return object;
            }
            *isChanged = true;
            return PDFObject::createArray(std::move(builder));
        }

        case PDFObject::Type::Stream:
        {
            const PDFStream* stream = object.getStream();
            bool isDictionaryChanged = false;
            const PDFObject dictionary = rewriteObject(input, PDFObject::createDictionary(PDFDictionaryBuilder(*stream->getDictionary())), depth + 1, &isDictionaryChanged);
            if (!isDictionaryChanged)
            {
                return object;
            }
            *isChanged = true;
            QByteArray content = *stream->getContent();
            return PDFObject::createStream(PDFStream(PDFDictionaryBuilder(*dictionary.getDictionary()), std::move(content)));
        }

        case PDFObject::Type::Dictionary:
            break;

        default:
            return object;
    }

    const PDFDictionary* dictionary = object.getDictionary();
    bool isDictionaryChanged = false;
    PDFDictionaryBuilder builder;
    builder.reserve(dictionary->getCount());
    for (size_t i = 0; i < dictionary->getCount(); ++i)
    {
        builder.addEntry(dictionary->getKey(i), rewriteObject(input, dictionary->getValue(i), depth + 1, &isDictionaryChanged));
    }

    const QByteArray subtype = Helper::getName(dictionary->get("S"));

    // Keys of the parent tree (14.7.5.4): shifted by the keys of the inputs before, or gone
    // with the structure
    for (const char* key : { "StructParents", "StructParent" })
    {
        const PDFObject& value = dictionary->get(key);
        if (value.isInt())
        {
            if (input->isStructureStripped)
            {
                builder.removeEntry(key);
                isDictionaryChanged = true;
            }
            else if (input->isStructureJoined && input->structParentOffset != 0)
            {
                builder.setEntry(Helper::key(key), PDFObject::createInteger(value.getInteger() + input->structParentOffset));
                isDictionaryChanged = true;
            }
        }
    }

    // Named destinations: of links and outline items, of GoTo actions
    if (dictionary->hasKey("Dest"))
    {
        builder.setEntry(Helper::key("Dest"), rewriteDestination(input, dictionary->get("Dest"), &isDictionaryChanged));
    }
    if (subtype == "GoTo" && dictionary->hasKey("D"))
    {
        builder.setEntry(Helper::key("D"), rewriteDestination(input, dictionary->get("D"), &isDictionaryChanged));
    }

    // An embedded target (12.6.4.4, Table 204) names a file of /EmbeddedFiles of this document
    if (Helper::getName(dictionary->get("R")) == "C" && dictionary->get("N").isString())
    {
        auto it = input->embeddedFileRenames.find(dictionary->get("N").getString());
        if (it != input->embeddedFileRenames.cend())
        {
            builder.setEntry(Helper::key("N"), PDFObject::createString(it->second));
            isDictionaryChanged = true;
        }
    }

    // A relative URI of an input, whose base the result does not keep, becomes absolute
    if (subtype == "URI" && !input->uriBase.isEmpty() && dictionary->get("URI").isString())
    {
        const QByteArray uri = dictionary->get("URI").getString();
        const QUrl url = QUrl::fromEncoded(uri);
        if (url.isRelative())
        {
            builder.setEntry(Helper::key("URI"), PDFObject::createString(QUrl(input->uriBase).resolved(url).toEncoded()));
            isDictionaryChanged = true;
        }
    }

    // The default appearance of a field follows the renamed fonts of /DR (12.7.4.3)
    if (!input->fontRenames.empty() && dictionary->get("DA").isString())
    {
        const QByteArray appearance = dictionary->get("DA").getString();
        const QByteArray renamed = Helper::renameDefaultAppearanceFont(appearance, input->fontRenames);
        if (renamed != appearance)
        {
            builder.setEntry(Helper::key("DA"), PDFObject::createString(renamed));
            isDictionaryChanged = true;
        }
    }

    // A structure element (Table 355): its classes, its identifier, its namespace
    const bool isStructureElement = input->isStructureJoined && dictionary->get("S").isName() && dictionary->hasKey("P") &&
                                    (!dictionary->hasKey("Type") || Helper::getName(dictionary->get("Type")) == "StructElem");
    if (isStructureElement)
    {
        if (!input->classRenames.empty() && dictionary->hasKey("C"))
        {
            std::vector<PDFObject> classes = Helper::getItems(getStorage(), dictionary->get("C"));
            bool isClassChanged = false;
            for (PDFObject& item : classes)
            {
                auto it = item.isName() ? input->classRenames.find(item.getString()) : input->classRenames.end();
                if (it != input->classRenames.end())
                {
                    item = PDFObject::createName(it->second);
                    isClassChanged = true;
                }
            }
            if (isClassChanged)
            {
                builder.setEntry(Helper::key("C"), classes.size() == 1 && !getObject(dictionary->get("C")).isArray() ? classes.front() : Helper::createArray(classes));
                isDictionaryChanged = true;
            }
        }
        if (!input->idRenames.empty() && dictionary->get("ID").isString())
        {
            auto it = input->idRenames.find(dictionary->get("ID").getString());
            if (it != input->idRenames.end())
            {
                builder.setEntry(Helper::key("ID"), PDFObject::createString(it->second));
                isDictionaryChanged = true;
            }
        }
        if (input->roleNamespace.isValid() && !dictionary->hasKey("NS") && input->namespaceTypes.count(subtype))
        {
            builder.setEntry(Helper::key("NS"), PDFObject::createReference(input->roleNamespace));
            isDictionaryChanged = true;
        }
    }

    if (!isDictionaryChanged)
    {
        return object;
    }
    *isChanged = true;
    return PDFObject::createDictionary(std::move(builder));
}

void PDFDocumentMergerRun::rewriteInputObjects(DocumentInput* input)
{
    // Every object of the input once (the references are not followed: each object is in the map)
    for (const PDFObjectReference& reference : input->objectMap)
    {
        if (!reference.isValid())
        {
            continue;
        }
        bool isChanged = false;
        PDFObject rewritten = rewriteObject(input, m_builder.getObjectByReference(reference), 0, &isChanged);
        if (isChanged)
        {
            m_builder.setObject(reference, std::move(rewritten));
        }
    }

    // The fields split from a widget of a clone are new objects of the input too (of /Fields and
    // the kids of a field alike)
    for (const auto& [widget, field] : m_splitFieldObjects)
    {
        if (widget.objectNumber >= input->firstObject && widget.objectNumber <= input->lastObject)
        {
            bool isChanged = false;
            PDFObject rewritten = rewriteObject(input, m_builder.getObjectByReference(field), 0, &isChanged);
            if (isChanged)
            {
                m_builder.setObject(field, std::move(rewritten));
            }
        }
    }

    // The clones of the pages of the input are new objects: their annotations too
    for (const ResultPage& page : m_resultPages)
    {
        if (page.input == input && page.isClone)
        {
            const PDFDictionary* dictionary = getDictionary(PDFObject::createReference(page.page));
            for (const PDFObject& annotation : dictionary ? Helper::getItems(getStorage(), dictionary->get("Annots")) : std::vector<PDFObject>())
            {
                if (annotation.isReference())
                {
                    bool isChanged = false;
                    PDFObject rewritten = rewriteObject(input, m_builder.getObjectByReference(annotation.getReference()), 0, &isChanged);
                    if (isChanged)
                    {
                        m_builder.setObject(annotation.getReference(), std::move(rewritten));
                    }
                }
            }
        }
    }
}

}   // namespace pdf
