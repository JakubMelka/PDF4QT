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
#include "pdfnewdocumentfinisher.h"
#include "pdfobjectutils.h"
#include "pdfoperationcontrol.h"
#include "pdfprogress.h"
#include "pdfsecurityhandler.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFileInfo>
#include <QUrl>

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

    /// The insertion mode (PDFDocumentMerger::insert): source 0 is the target, which the items
    /// never name; \p position is the index of the page of the target, before which they go
    void setInsertion(PDFInteger position) { m_isInsert = true; m_position = position; }

    /// The statistics of the removal of the replaced pages of the target (insert)
    void setReplacement(const PDFDocumentPageOperations::RemovalStatistics& statistics, qint64 count) { m_statistics.replacement = statistics; m_statistics.replacedPageCount = count; }

    /// The arrangement mode (PDFDocumentMerger::arrange): source 0 is the target as in the
    /// insertion mode, but the items name its pages in any order; every page of the target is
    /// named (the unused ones were removed before, \p statistics and \p count tell it)
    void setArrangement(const PDFDocumentPageOperations::RemovalStatistics& statistics, qint64 count) { m_isInsert = true; m_isArrange = true; setReplacement(statistics, count); }

    /// PDFDocumentMerger::removeSignatures
    PDFDocument runRemoveSignatures(const PDFDocument& document, PDFInteger* signaturesRemoved);

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
        bool hadDocumentPartsRemoved = false;       ///< Phase 1 removed the document parts (damaged or without a page)

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
    bool createResultPage(size_t itemIndex, bool isInheritedBlank, ResultPage* resultPage);
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
    Merger::Result finish(PDFVersion version, bool removeConformance);

    // One document (R3)
    Merger::Result runSingle(DocumentInput* input);

    // The insertion into a target (insert)

    /// A page of the page tree of the target: its node, the nodes above it (the root first) and
    /// its index in /Kids of its parent
    struct PageTreeLeaf
    {
        PDFObjectReference page;
        std::vector<PDFObjectReference> ancestors;
        size_t indexInParent = 0;
    };

    Merger::Result runInsert();
    void createTargetInput();
    bool copySourceInputs();
    void collectPageTreeLeaves(PDFObjectReference node, std::vector<PDFObjectReference>* ancestors, int depth, PDFObjectWalk* walk,
                               std::vector<PageTreeLeaf>* leaves, std::map<PDFObjectReference, PDFInteger>* leafCounts) const;
    bool readTargetPageTree(PDFObjectReference pageTreeRoot, std::vector<PageTreeLeaf>* leaves, std::map<PDFObjectReference, PDFInteger>* leafCounts) const;
    void insertIntoPageTree(PDFObjectReference pageTreeRoot, const std::vector<PDFObjectReference>& newPages);
    void makeInheritedValuesExplicit(PDFObjectReference page, const std::vector<PDFObjectReference>& ancestors);
    void insertIntoDocumentParts(const std::vector<PDFObjectReference>& newPages);
    PDFVersion getInsertionVersion(const PDFDocument* target) const;
    bool rewriteSourceInputs();
    Merger::Result createInsertionResult(const PDFDocument* target, PDFVersion version);
    PDFObjectStorage compactNewObjects(PDFObjectStorage storage) const;
    bool isTargetInput(const DocumentInput* input) const { return m_isInsert && input == m_targetInput; }

    // The arrangement of the target (arrange)
    Merger::Result runArrange();
    bool createArrangedPage(size_t itemIndex, ResultPage* resultPage);
    PDFObjectReference cloneTargetPage(PDFInteger documentPage);
    void setTargetPageGeometry(PDFObjectReference page, const Merger::Item& item, PDFInteger documentPage);
    void insertIntoPageTreePlaces(const std::vector<PageTreeLeaf>& leaves, const std::map<PDFObjectReference, PDFInteger>& leafCounts,
                                  const std::vector<std::vector<PDFObjectReference>>& places);
    void flattenArrangedPageTree(PDFObjectReference pageTreeRoot, const std::vector<PDFObjectReference>& newPages);
    void removeDocumentParts(PDFDictionaryBuilder* catalog);
    void removeUnreachableTargetObjects(const std::set<PDFObjectReference>& reachableBefore);
    bool isSignatureWidget(const PDFObject& annotation) const;

    /// The lock of a signed signature field (12.7.5.5, the signature field lock dictionary; 12.8.2.4,
    /// the FieldMDP transform parameters of its signature)
    struct FieldLock
    {
        QByteArray action;          ///< All, Include, Exclude
        std::set<QString> fields;   ///< Fully qualified names of /Fields
    };
    void collectFieldLocks(const PDFObject& field, const QByteArray& inheritedType, const PDFDictionary* inheritedSignature, int depth, PDFObjectWalk* walk);
    bool isLockedFieldWidget(const PDFObject& annotation) const;

    // The document parts of the settings (Settings::parts, QB3 of step 3b) or the runs
    struct Section
    {
        size_t first = 0;   ///< Index of the first page of the result
        size_t count = 0;
        QString title;      ///< Title of its bookmark
    };
    std::vector<Section> getSections() const;
    std::vector<size_t> getSectionSizes() const;
    QString getRunTitle(const Run& run, const std::map<size_t, PDFInteger>& runsOfSource) const;

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

    // The insertion mode: m_sources[0] is the target, its input is the first of m_documents
    bool m_isInsert = false;
    PDFInteger m_position = 0;
    DocumentInput* m_targetInput = nullptr;     ///< Owned by m_documents (its first input)
    size_t m_targetObjectCount = 0;             ///< Objects of the target: the new ones follow

    // The arrangement mode (m_isInsert is set too): no clone gets a widget of a signature field (RB1)
    bool m_isArrange = false;
    std::vector<FieldLock> m_fieldLocks;    ///< The locks of the signed signature fields of the target (arrange): no clone gets a widget of a locked field
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

        if (m_isArrange)
        {
            result = runArrange();
        }
        else if (m_isInsert)
        {
            result = runInsert();
        }
        else
        {
            result = documentCount == 1 ? runSingle(single) : runMulti();
        }
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

    // The document parts (QB3 of step 3b): the first one at the first item, ascending, each with
    // an item - every page is in exactly one part and the parts are contiguous (14.12)
    for (size_t i = 0; i < m_settings.parts.size(); ++i)
    {
        const size_t firstItem = m_settings.parts[i].firstItem;
        const bool isValid = firstItem < m_items.size() && (i == 0 ? firstItem == 0 : firstItem > m_settings.parts[i - 1].firstItem);
        if (!isValid)
        {
            *errorMessage = PDFDocumentMerger::tr("The document parts of the result are not valid.");
            return false;
        }
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
                input->hadDocumentPartsRemoved = removal.documentPartsRemoved;
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

    // Margins of PageMaster and of Manage Pages: from the visible area with its origin (B14, B28;
    // 14.11.2: the effective crop box is the crop box within the media box - a crop box larger than
    // the media box shows the media box, and the margins are measured on what the page shows)
    const QMarginsF& margins = item.cropMargins;
    if (sourcePage && (margins.left() > 0.0 || margins.top() > 0.0 || margins.right() > 0.0 || margins.bottom() > 0.0))
    {
        const QMarginsF unrotated = Helper::mapCropMarginsToUnrotatedPage(margins, sourcePage->getPageRotation());
        const QRectF cropBox = sourcePage->getEffectiveCropBox();
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

    std::vector<PDFObject> annotations = Helper::getItems(getStorage(), dictionary->get("Annots"));
    if (m_isArrange)
    {
        // RB1 of step 3b: a clone in the document itself gets no widget of a signature field - a
        // new widget would change the signed field (its /Kids, or the split of a merged field and
        // widget), which an incremental update must keep as it was signed. The same holds for a field
        // locked by a signed signature field (12.7.5.5, owner 2026-11-11); the facts count them
        auto isLeftOut = [this](const PDFObject& annotation)
        {
            if (isSignatureWidget(annotation))
            {
                ++m_statistics.signatureWidgetsNotCloned;
                return true;
            }
            if (isLockedFieldWidget(annotation))
            {
                ++m_statistics.lockedWidgetsNotCloned;
                return true;
            }
            return false;
        };
        annotations.erase(std::remove_if(annotations.begin(), annotations.end(), isLeftOut), annotations.end());
    }
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

bool PDFDocumentMergerRun::createResultPage(size_t itemIndex, bool isInheritedBlank, ResultPage* resultPage)
{
    // The page object of an item (the page of a document, its clone, an image page, a blank
    // page) with its geometry; false = its input is left out
    const Merger::Item& item = m_items[itemIndex];
    const Merger::Source& source = m_sources[item.source];
    resultPage->item = itemIndex;

    switch (source.kind)
    {
        case Merger::Source::Kind::Document:
        {
            DocumentInput* input = m_documentOfSource.at(item.source);
            if (input->isLeftOut)
            {
                return false;
            }
            const PDFInteger documentPage = input->pageMap[size_t(item.pageIndex)];
            resultPage->input = input;
            resultPage->documentPage = documentPage;
            if (!input->isPagePlaced[size_t(documentPage)])
            {
                input->isPagePlaced[size_t(documentPage)] = true;
                resultPage->page = input->pages[size_t(documentPage)];
            }
            else
            {
                resultPage->page = clonePage(input->pages[size_t(documentPage)]);
                resultPage->isClone = true;
            }
            setPageGeometry(resultPage->page, item, input, documentPage);
            return true;
        }

        case Merger::Source::Kind::Image:
        {
            const ImageInput& image = m_images.at(item.source);
            if (image.isLeftOut)
            {
                return false;
            }
            const PDFImagePageLayout layout = item.imageLayout ? *item.imageLayout : PDFImagePageLayout::createStretched(item.pageSize, item.imageClip);
            resultPage->page = PDFImagePageFactory::createPage(&m_builder, image.frames.at(item.pageIndex), layout);
            setPageGeometry(resultPage->page, item, nullptr, -1);
            return true;
        }

        case Merger::Source::Kind::Blank:
            resultPage->page = createBlankPage(item.pageSize, isInheritedBlank);
            setPageGeometry(resultPage->page, item, nullptr, -1);
            return true;
    }
    return false;
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

        ResultPage resultPage;
        if (!createResultPage(itemIndex, isInheritedBlank, &resultPage))
        {
            continue;
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
        // A page of the target (insert) has no item: its input is the source 0
        const size_t source = m_resultPages[i].input ? m_resultPages[i].input->source : m_items[m_resultPages[i].item].source;
        if (m_runs.empty() || m_runs.back().source != source)
        {
            m_runs.push_back({ source, i, 0 });
        }
        ++m_runs.back().count;
    }

    std::set<size_t> sources;
    for (const Run& run : m_runs)
    {
        if (m_sources[run.source].kind != Merger::Source::Kind::Blank && !(m_isInsert && run.source == 0))
        {
            sources.insert(run.source);
        }
    }
    m_statistics.pageCount = qint64(m_resultPages.size());
    m_statistics.partCount = qint64(m_runs.size());
    m_statistics.inputCount = qint64(sources.size());
    if (!m_settings.parts.empty() && !m_isInsert)
    {
        m_statistics.partCount = qint64(getSectionSizes().size());
    }
}

std::vector<PDFDocumentMergerRun::Section> PDFDocumentMergerRun::getSections() const
{
    // The parts of the outline and of /DPartRoot: the runs of the pages of one input (K14), or
    // the parts of the settings (QB3 of step 3b) - a part has the pages of its items, which are in
    // the result (the pages of the result are in the order of the items); a part without such a
    // page has no bookmark
    std::vector<Section> sections;
    if (m_settings.parts.empty() || m_isInsert)
    {
        std::map<size_t, PDFInteger> runsOfSource;
        for (const Run& run : m_runs)
        {
            ++runsOfSource[run.source];
        }
        for (const Run& run : m_runs)
        {
            sections.push_back({ run.first, run.count, getRunTitle(run, runsOfSource) });
        }
        return sections;
    }

    size_t page = 0;
    for (size_t i = 0; i < m_settings.parts.size(); ++i)
    {
        const size_t endItem = i + 1 < m_settings.parts.size() ? m_settings.parts[i + 1].firstItem : m_items.size();
        Section section;
        section.first = page;
        section.title = m_settings.parts[i].title;
        while (page < m_resultPages.size() && m_resultPages[page].item < endItem)
        {
            ++page;
        }
        section.count = page - section.first;
        if (section.count > 0)
        {
            sections.push_back(std::move(section));
        }
    }
    return sections;
}

std::vector<size_t> PDFDocumentMergerRun::getSectionSizes() const
{
    std::vector<size_t> sizes;
    for (const Section& section : getSections())
    {
        sizes.push_back(section.count);
    }
    return sizes;
}

QString PDFDocumentMergerRun::getRunTitle(const Run& run, const std::map<size_t, PDFInteger>& runsOfSource) const
{
    // The caption of a run (K14): the title of the document with its pages, when the run has not
    // all of them or the document has more runs ("Contract.pdf, p. 6"); the name of an image; the
    // number of blank pages
    const Merger::Source& source = m_sources[run.source];
    switch (source.kind)
    {
        case Merger::Source::Kind::Document:
        {
            const DocumentInput* input = m_resultPages[run.first].input;
            QString title = input ? input->title : source.name;
            const size_t sourcePageCount = source.document->getCatalog()->getPageCount();
            auto runCount = runsOfSource.find(run.source);
            if (run.count < sourcePageCount || (runCount != runsOfSource.cend() && runCount->second > 1))
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
            return title;
        }
        case Merger::Source::Kind::Image:
            return source.name.isEmpty() ? PDFDocumentMerger::tr("%Ln images", nullptr, int(run.count)) : source.name;
        case Merger::Source::Kind::Blank:
            return PDFDocumentMerger::tr("%Ln blank pages", nullptr, int(run.count));
    }
    return QString();
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

    // Insert (5.7): only a target with labels gets them - its pages keep theirs
    bool hasLabels = false;
    for (const std::unique_ptr<DocumentInput>& input : m_documents)
    {
        const bool isConsidered = !m_isInsert || isTargetInput(input.get());
        hasLabels = hasLabels || (isConsidered && !input->isLeftOut && !input->document->getCatalog()->getPageLabels().empty());
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

PDFDocumentMerger::Result PDFDocumentMergerRun::finish(PDFVersion version, bool removeConformance)
{
    Merger::Result result;
    if (isCanceled())
    {
        result.isCanceled = true;
        return result;
    }

    // R2 of step 3: the keys of a new document (/ID, no /Encrypt, the dates in /Info and XMP,
    // the signatures, the layers, the declarations), the compacted storage and the limit of the
    // objects - one implementation for the merger and the splitter
    PDFNewDocumentFinisher::Settings settings;
    settings.version = version;
    settings.creationTime = m_now;
    settings.isWholeSingleDocument = m_statistics.isSingleDocument;
    settings.isNewFile = m_settings.isNewFile;
    settings.removeConformance = removeConformance;
    PDFNewDocumentFinisher::Result finished = PDFNewDocumentFinisher::finish(m_builder.build(), settings, m_control);
    if (finished.isCanceled || isCanceled())
    {
        result.isCanceled = true;
        return result;
    }
    if (!finished.isSuccess)
    {
        result.errorMessage = finished.errorMessage;
        return result;
    }

    m_statistics.conformanceRemoved = finished.conformanceRemoved;
    m_statistics.signaturesRemoved += finished.signaturesRemoved;
    result.isSuccess = true;
    result.document = std::move(finished.document);
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
    m_statistics.documentPartsRemoved = input->hadDocumentPartsRemoved;
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
            m_statistics.documentPartsRemoved = true;
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

    // The parts of the settings (QB3 of step 3b: the groups of Manage Pages) apply the outline mode
    // as the images do - a bookmark and a document part per part, the outline of the document
    // below them (Document parts open, Join closed), or no outline
    const bool isOutlineModeApplied = hasImage || !m_settings.parts.empty();

    PDFVersion version = input->document->getInfo()->version;
    if (isOutlineModeApplied)
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

    if (isOutlineModeApplied && m_settings.outline != Merger::OutlineMode::NoOutline && !m_runs.empty())
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
        m_builder.createDocumentParts(getSectionSizes());
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
        m_builder.createDocumentParts(getSectionSizes());
        version = qMax(version, PDFVersion(2, 0));
    }
    if (m_isStructureJoined)
    {
        version = qMax(version, PDFVersion(2, 0));
    }

    return finish(version, false);
}

// ------------------------------------------------------- The insertion mode (PDFDocumentMerger::insert)

void PDFDocumentMergerRun::createTargetInput()
{
    // The target is an input copied as itself: every object keeps its number (the identity map),
    // so the joins of the catalogs read it as the first input and the rules of merge apply
    const PDFDocument* document = m_sources.front().document.data();
    const PDFObjectStorage& storage = document->getStorage();
    const PDFObjectStorage::PDFObjects& objects = storage.getObjects();
    const PDFCatalog* catalog = document->getCatalog();

    std::unique_ptr<DocumentInput> input = std::make_unique<DocumentInput>();
    input->source = 0;
    input->document = document;
    input->isWhole = true;
    for (size_t i = 0; i < catalog->getPageCount(); ++i)
    {
        input->pageMap.push_back(PDFInteger(i));
        input->sourcePages.push_back(PDFInteger(i));
        input->pages.push_back(catalog->getPage(i)->getPageReference());
    }
    input->isPagePlaced.assign(input->pages.size(), true);

    input->objectMap.assign(objects.size(), PDFObjectReference());
    input->objectGenerations.assign(objects.size(), -1);
    for (size_t i = 0; i < objects.size(); ++i)
    {
        if (!objects[i].object.isNull())
        {
            input->objectMap[i] = PDFObjectReference(PDFInteger(i), objects[i].generation);
            input->objectGenerations[i] = objects[i].generation;
        }
    }
    input->firstObject = 0;
    input->lastObject = PDFInteger(objects.size()) - 1;

    const PDFDictionary* trailer = Helper::getTrailer(&storage);
    input->catalog = storage.getObject(trailer ? trailer->get("Root") : PDFObject());
    input->title = document->getInfo()->title.trimmed();

    m_targetInput = input.get();
    m_documentOfSource[0] = input.get();
    m_documents.insert(m_documents.begin(), std::move(input));
}

void PDFDocumentMergerRun::collectPageTreeLeaves(PDFObjectReference node, std::vector<PDFObjectReference>* ancestors, int depth, PDFObjectWalk* walk,
                                                 std::vector<PageTreeLeaf>* leaves, std::map<PDFObjectReference, PDFInteger>* leafCounts) const
{
    // 7.7.3.2: an intermediate node has /Kids; a leaf is a page. One walk for the whole tree: a
    // node reached twice or a cycle is read once - the leaves then differ from the pages of the
    // catalog and the caller flattens the tree
    if (!walk->enterReference(node, depth))
    {
        return;
    }
    const PDFDictionary* dictionary = getDictionary(PDFObject::createReference(node));
    if (!dictionary)
    {
        return;
    }

    if (Helper::getName(dictionary->get("Type")) == "Pages" || (!dictionary->hasKey("Type") && dictionary->hasKey("Kids")))
    {
        ancestors->push_back(node);
        const PDFInteger countBefore = PDFInteger(leaves->size());
        const std::vector<PDFObject> kids = Helper::getItems(getStorage(), dictionary->get("Kids"));
        for (size_t i = 0; i < kids.size(); ++i)
        {
            if (!kids[i].isReference())
            {
                continue;
            }
            const size_t leafCount = leaves->size();
            collectPageTreeLeaves(kids[i].getReference(), ancestors, depth + 1, walk, leaves, leafCounts);
            if (leaves->size() == leafCount + 1 && leaves->back().page == kids[i].getReference())
            {
                leaves->back().indexInParent = i;
            }
        }
        (*leafCounts)[node] = PDFInteger(leaves->size()) - countBefore;
        ancestors->pop_back();
        return;
    }

    PageTreeLeaf leaf;
    leaf.page = node;
    leaf.ancestors = *ancestors;
    leaves->push_back(std::move(leaf));
}

void PDFDocumentMergerRun::makeInheritedValuesExplicit(PDFObjectReference page, const std::vector<PDFObjectReference>& ancestors)
{
    // Table 31, 7.7.3.4: an inserted page must not inherit a value of the tree of the target -
    // it has its own (the flattened source page, the image or blank page) or the default
    const PDFDictionary* dictionary = getDictionary(PDFObject::createReference(page));
    if (!dictionary)
    {
        return;
    }

    PDFDictionaryBuilder builder(*dictionary);
    bool isChanged = false;
    for (const char* key : { "Resources", "MediaBox", "CropBox", "Rotate" })
    {
        if (dictionary->hasKey(key))
        {
            continue;
        }

        bool isInherited = false;
        for (const PDFObjectReference& ancestor : ancestors)
        {
            const PDFDictionary* node = getDictionary(PDFObject::createReference(ancestor));
            isInherited = isInherited || (node && node->hasKey(key));
        }
        if (!isInherited)
        {
            continue;
        }

        const std::string_view name(key);
        if (name == "Resources")
        {
            builder.setEntry(Helper::key(key), PDFObject::createDictionary(PDFDictionaryBuilder()));
            isChanged = true;
        }
        else if (name == "CropBox" && dictionary->hasKey("MediaBox"))
        {
            // The default of the crop box is the media box of the page (14.11.2)
            builder.setEntry(Helper::key(key), dictionary->get("MediaBox"));
            isChanged = true;
        }
        else if (name == "Rotate")
        {
            builder.setEntry(Helper::key(key), PDFObject::createInteger(0));
            isChanged = true;
        }
    }

    if (isChanged)
    {
        m_builder.setObject(page, PDFObject::createDictionary(std::move(builder)));
    }
}

bool PDFDocumentMergerRun::readTargetPageTree(PDFObjectReference pageTreeRoot, std::vector<PageTreeLeaf>* leaves, std::map<PDFObjectReference, PDFInteger>* leafCounts) const
{
    // The leaves of the tree of the target; false = they are not the pages of the catalog in its
    // order (a damaged tree: a cycle, a node shared by two parents, a page without a parent node)
    std::vector<PDFObjectReference> ancestors;
    PDFObjectWalk walk(Helper::MAXIMUM_TREE_DEPTH);
    collectPageTreeLeaves(pageTreeRoot, &ancestors, 0, &walk, leaves, leafCounts);

    bool isTreeValid = leaves->size() == m_targetInput->pages.size() && !leafCounts->empty();
    for (size_t i = 0; isTreeValid && i < leaves->size(); ++i)
    {
        isTreeValid = (*leaves)[i].page == m_targetInput->pages[i] && !(*leaves)[i].ancestors.empty();
    }
    return isTreeValid;
}

void PDFDocumentMergerRun::insertIntoPageTree(PDFObjectReference pageTreeRoot, const std::vector<PDFObjectReference>& newPages)
{
    // R8: the new pages go into /Kids of the node, which holds the page at the position (the last
    // page at the end), and /Count of every node above them grows; nothing else of the tree
    // changes, so every inherited value of the pages of the target stays. A tree, whose pages
    // are not the pages of the catalog (a damaged tree), is flattened first (its inherited
    // values are written into its pages).
    std::vector<PageTreeLeaf> leaves;
    std::map<PDFObjectReference, PDFInteger> leafCounts;
    if (!readTargetPageTree(pageTreeRoot, &leaves, &leafCounts))
    {
        m_builder.flattenPageTree();
        m_statistics.isPageTreeFlattened = true;
        leaves.clear();
        leafCounts.clear();
        for (size_t i = 0; i < m_targetInput->pages.size(); ++i)
        {
            PageTreeLeaf leaf;
            leaf.page = m_targetInput->pages[i];
            leaf.ancestors = { pageTreeRoot };
            leaf.indexInParent = i;
            leaves.push_back(std::move(leaf));
        }
        leafCounts[pageTreeRoot] = PDFInteger(leaves.size());
    }

    std::vector<PDFObjectReference> ancestors = { pageTreeRoot };
    size_t index = 0;
    if (!leaves.empty())
    {
        const size_t position = size_t(qBound<PDFInteger>(0, m_position, PDFInteger(leaves.size())));
        const PageTreeLeaf& leaf = leaves[qMin(position, leaves.size() - 1)];
        ancestors = leaf.ancestors;
        index = leaf.indexInParent + (position < leaves.size() ? 0 : 1);
    }
    const PDFObjectReference parent = ancestors.back();

    const PDFDictionary* parentDictionary = getDictionary(PDFObject::createReference(parent));
    std::vector<PDFObject> kids = parentDictionary ? Helper::getItems(getStorage(), parentDictionary->get("Kids")) : std::vector<PDFObject>();
    index = qMin(index, kids.size());
    std::vector<PDFObject> newKids;
    for (const PDFObjectReference& page : newPages)
    {
        newKids.push_back(PDFObject::createReference(page));
    }
    kids.insert(kids.begin() + qint64(index), newKids.cbegin(), newKids.cend());

    for (const PDFObjectReference& node : ancestors)
    {
        const PDFDictionary* nodeDictionary = getDictionary(PDFObject::createReference(node));
        PDFDictionaryBuilder nodeBuilder = nodeDictionary ? PDFDictionaryBuilder(*nodeDictionary) : PDFDictionaryBuilder();
        if (node == parent)
        {
            nodeBuilder.setEntry(Helper::key("Kids"), Helper::createArray(kids));
        }
        nodeBuilder.setEntry(Helper::key("Count"), PDFObject::createInteger(leafCounts[node] + PDFInteger(newPages.size())));
        m_builder.setObject(node, PDFObject::createDictionary(std::move(nodeBuilder)));
    }

    for (const PDFObjectReference& page : newPages)
    {
        PDFDictionaryBuilder parentEntry;
        parentEntry.addEntry(Helper::key("Parent"), PDFObject::createReference(parent));
        m_builder.mergeTo(page, PDFObject::createDictionary(std::move(parentEntry)));
        makeInheritedValuesExplicit(page, ancestors);
    }
}

void PDFDocumentMergerRun::insertIntoDocumentParts(const std::vector<PDFObjectReference>& newPages)
{
    // 14.12: the pages of a document part are a contiguous range from /Start to /End. The inserted
    // pages join the part, inside of which they are inserted; at the border of two parts the part
    // before them (its /End moves), at the beginning the first part (its /Start moves)
    const PDFDictionary* catalog = getDictionary(m_targetInput->catalog);
    if (!catalog || getObject(catalog->get("DPartRoot")).isNull() || newPages.empty() || m_targetInput->pages.empty())
    {
        return;
    }

    const size_t position = size_t(qBound<PDFInteger>(0, m_position, PDFInteger(m_targetInput->pages.size())));
    auto getPart = [this](PDFObjectReference page) -> PDFObject
    {
        const PDFDictionary* dictionary = getDictionary(PDFObject::createReference(page));
        return dictionary ? dictionary->get("DPart") : PDFObject();
    };

    const PDFObject before = position > 0 ? getPart(m_targetInput->pages[position - 1]) : PDFObject();
    const PDFObject after = position < m_targetInput->pages.size() ? getPart(m_targetInput->pages[position]) : PDFObject();
    const PDFObject part = before.isReference() ? before : after;
    const PDFDictionary* partDictionary = part.isReference() ? getDictionary(part) : nullptr;
    if (!partDictionary)
    {
        return;
    }

    PDFDictionaryBuilder partBuilder(*partDictionary);
    bool isPartChanged = false;
    if (before.isReference() && before != after)
    {
        // The pages follow the last page of the part before them
        const PDFObject& end = partDictionary->get("End");
        const PDFObjectReference lastPage = m_targetInput->pages[position - 1];
        if ((end.isReference() && end.getReference() == lastPage) || (!end.isReference() && partDictionary->get("Start").isReference() && partDictionary->get("Start").getReference() == lastPage))
        {
            partBuilder.setEntry(Helper::key("End"), PDFObject::createReference(newPages.back()));
            isPartChanged = true;
        }
    }
    else if (!before.isReference())
    {
        // The pages are before the first page of the part after them
        const PDFObject& start = partDictionary->get("Start");
        if (start.isReference() && start.getReference() == m_targetInput->pages[position])
        {
            partBuilder.setEntry(Helper::key("Start"), PDFObject::createReference(newPages.front()));
            if (!partDictionary->hasKey("End"))
            {
                partBuilder.setEntry(Helper::key("End"), PDFObject::createReference(m_targetInput->pages[position]));
            }
            isPartChanged = true;
        }
    }
    if (isPartChanged)
    {
        m_builder.setObject(part.getReference(), PDFObject::createDictionary(std::move(partBuilder)));
    }

    for (const PDFObjectReference& page : newPages)
    {
        PDFDictionaryBuilder entry;
        entry.addEntry(Helper::key("DPart"), part);
        m_builder.mergeTo(page, PDFObject::createDictionary(std::move(entry)));
    }
}

PDFObjectStorage PDFDocumentMergerRun::compactNewObjects(PDFObjectStorage storage) const
{
    // An incremental update adds only what the result reaches: the objects of the sources, which
    // nothing refers to (their catalogs, page trees, outlines, encryption dictionaries), are
    // left out, the others get the numbers from the end of the target without a gap. The objects
    // of the target keep their numbers; a reference to a new object is rewritten only in the new
    // objects and in the changed objects of the target.
    const PDFObjectStorage::PDFObjects& objects = storage.getObjects();
    const PDFObjectStorage::PDFObjects& targetObjects = m_targetInput->document->getStorage().getObjects();
    const size_t targetCount = qMin(m_targetObjectCount, objects.size());
    const std::set<PDFObjectReference> reachable = PDFObjectUtils::getReferences({ storage.getTrailerDictionary() }, storage);

    PDFObjectStorage::PDFObjects compacted(objects.cbegin(), objects.cbegin() + qint64(targetCount));
    std::map<PDFObjectReference, PDFObjectReference> mapping;
    for (size_t i = targetCount; i < objects.size(); ++i)
    {
        const PDFObjectReference reference(PDFInteger(i), objects[i].generation);
        if (objects[i].object.isNull() || !reachable.count(reference))
        {
            continue;
        }
        const PDFObjectReference newReference(PDFInteger(compacted.size()), 0);
        if (newReference != reference)
        {
            mapping[reference] = newReference;
        }
        compacted.emplace_back(0, objects[i].object);
    }

    if (!mapping.empty())
    {
        for (size_t i = 0; i < compacted.size(); ++i)
        {
            const bool isTargetObject = i < targetCount && i < targetObjects.size();
            if (isTargetObject && compacted[i] == targetObjects[i])
            {
                continue;
            }
            compacted[i].object = PDFObjectUtils::replaceReferences(compacted[i].object, mapping);
        }
    }

    // The trailer refers to a new object too (/Info of a target, which had none)
    const size_t size = compacted.size();
    storage.setObjects(std::move(compacted));
    PDFDictionaryBuilder trailer;
    if (const PDFDictionary* trailerDictionary = Helper::getTrailer(&storage))
    {
        for (size_t i = 0; i < trailerDictionary->getCount(); ++i)
        {
            const PDFObject& value = trailerDictionary->getValue(i);
            auto it = value.isReference() ? mapping.find(value.getReference()) : mapping.end();
            if (it != mapping.end())
            {
                trailer.addEntry(trailerDictionary->getKey(i), PDFObject::createReference(it->second));
            }
        }
    }
    trailer.setEntry(Helper::key("Size"), PDFObject::createInteger(PDFInteger(size)));
    storage.updateTrailerDictionary(PDFObject::createDictionary(std::move(trailer)));
    return storage;
}

PDFDocumentMerger::Result PDFDocumentMergerRun::runInsert()
{
    const PDFDocument* target = m_sources.front().document.data();
    m_targetObjectCount = target->getStorage().getObjects().size();
    m_builder.setDocument(target);
    createTargetInput();
    if (!copySourceInputs())
    {
        Merger::Result result;
        result.isCanceled = true;
        return result;
    }

    const PDFObjectReference catalogReference = m_builder.getCatalogReference();
    const PDFDictionary* catalogDictionary = getDictionary(PDFObject::createReference(catalogReference));
    if (!catalogDictionary || !catalogDictionary->get("Pages").isReference())
    {
        throw PDFException(PDFDocumentMerger::tr("The document has no page tree."));
    }
    const PDFObjectReference pageTreeRoot = catalogDictionary->get("Pages").getReference();

    // The pages of the items
    std::vector<ResultPage> newPages;
    for (size_t itemIndex = 0; itemIndex < m_items.size(); ++itemIndex)
    {
        if (isCanceled())
        {
            Merger::Result result;
            result.isCanceled = true;
            return result;
        }

        ResultPage resultPage;
        if (createResultPage(itemIndex, false, &resultPage))
        {
            newPages.push_back(resultPage);
        }
        if (m_progress)
        {
            m_progress->step();
        }
    }
    if (newPages.empty())
    {
        Merger::Result result;
        result.errorMessage = m_leftOut.empty() ? PDFDocumentMerger::tr("There is no page to insert.") : m_leftOut.front().reason;
        return result;
    }

    std::vector<PDFObjectReference> newPageReferences;
    for (const ResultPage& page : newPages)
    {
        newPageReferences.push_back(page.page);
    }
    insertIntoPageTree(pageTreeRoot, newPageReferences);

    // The pages of the result in their order: the pages of the target with the new ones at the position
    const size_t position = size_t(qBound<PDFInteger>(0, m_position, PDFInteger(m_targetInput->pages.size())));
    for (size_t i = 0; i < m_targetInput->pages.size(); ++i)
    {
        if (i == position)
        {
            m_resultPages.insert(m_resultPages.end(), newPages.cbegin(), newPages.cend());
        }
        ResultPage page;
        page.page = m_targetInput->pages[i];
        page.input = m_targetInput;
        page.documentPage = PDFInteger(i);
        m_resultPages.push_back(page);
    }
    if (position >= m_targetInput->pages.size())
    {
        m_resultPages.insert(m_resultPages.end(), newPages.cbegin(), newPages.cend());
    }
    computeRuns();
    m_statistics.insertedPageCount = qint64(newPages.size());
    m_statistics.isPageTreeFlattened = m_statistics.isPageTreeFlattened || m_statistics.replacedPageCount > 0;

    // The catalog of the target is the catalog of the result: the sources are joined to it
    catalogDictionary = getDictionary(PDFObject::createReference(catalogReference));
    PDFDictionaryBuilder catalog(*catalogDictionary);
    mergeNames(&catalog);
    mergeForm(&catalog);
    mergeLayers(&catalog);
    mergeStructure(&catalog);
    mergeOtherKeys(&catalog);

    if (!m_targetInput->document->getCatalog()->getPageLabels().empty())
    {
        const PDFObject labels = createPageLabels();
        if (!labels.isNull())
        {
            catalog.setEntry(Helper::key("PageLabels"), PDFObject::createReference(m_builder.addObject(labels)));
        }
    }
    insertIntoDocumentParts(newPageReferences);

    const PDFVersion version = getInsertionVersion(target);
    if (target->getInfo()->version < version)
    {
        catalog.setEntry(Helper::key("Version"), PDFObject::createName(QByteArray::number(version.major) + "." + QByteArray::number(version.minor)));
    }
    m_builder.setObject(catalogReference, PDFObject::createDictionary(std::move(catalog)));

    if (!rewriteSourceInputs())
    {
        Merger::Result result;
        result.isCanceled = true;
        return result;
    }
    return createInsertionResult(target, version);
}

bool PDFDocumentMergerRun::copySourceInputs()
{
    // The sources get the numbers after the objects of the target; a copy, which fails, leaves
    // the source out (owner answer Q5). False = canceled.
    for (const std::unique_ptr<DocumentInput>& input : m_documents)
    {
        if (isCanceled())
        {
            return false;
        }
        if (input->isLeftOut || isTargetInput(input.get()))
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
    return true;
}

PDFVersion PDFDocumentMergerRun::getInsertionVersion(const PDFDocument* target) const
{
    // The version (7.2.2, Table 29 /Version): of the sources too; an incremental update cannot
    // change the header, the catalog says it
    PDFVersion version = target->getInfo()->version;
    for (const std::unique_ptr<DocumentInput>& input : m_documents)
    {
        if (!input->isLeftOut)
        {
            version = qMax(version, input->document->getInfo()->version);
        }
    }
    if (m_isStructureJoined)
    {
        version = qMax(version, PDFVersion(2, 0));
    }
    return version;
}

bool PDFDocumentMergerRun::rewriteSourceInputs()
{
    // False = canceled
    for (const std::unique_ptr<DocumentInput>& input : m_documents)
    {
        // The target is rewritten only for its destinations of /Dests, which the tree took
        const bool isRewritten = !input->isLeftOut && (!isTargetInput(input.get()) || !input->destinationRenames.empty());
        if (isRewritten)
        {
            rewriteInputObjects(input.get());
        }
        if (isCanceled())
        {
            return false;
        }
    }
    return true;
}

PDFDocumentMerger::Result PDFDocumentMergerRun::createInsertionResult(const PDFDocument* target, PDFVersion version)
{
    PDFDocument built = m_builder.build();
    PDFObjectStorage storage = compactNewObjects(built.getStorage());
    if (qint64(storage.getObjects().size()) > Merger::MAXIMUM_OBJECT_COUNT)
    {
        Merger::Result result;
        result.errorMessage = PDFDocumentMerger::tr("The result would have more than %Ln objects.", nullptr, int(Merger::MAXIMUM_OBJECT_COUNT));
        return result;
    }

    Merger::Result result;
    result.isSuccess = true;
    result.document = PDFDocument(std::move(storage), version, target->getSourceDataHash());
    result.statistics = m_statistics;
    result.statistics.pageCount = qint64(m_resultPages.size());
    result.leftOut = m_leftOut;
    return result;
}

// ------------------------------------------------------- The arrangement mode (PDFDocumentMerger::arrange)

PDFDocumentMerger::Result PDFDocumentMergerRun::runArrange()
{
    // Plan of step 3b of phase 2, 4.5: the generalization of the insertion. The target is the input
    // 0 with the identity of its object numbers (createTargetInput); the items name its pages in any
    // order, each page at least once (PDFDocumentMerger::arrange removed the unused ones before).
    const PDFDocument* target = m_sources.front().document.data();
    m_targetObjectCount = target->getStorage().getObjects().size();
    m_builder.setDocument(target);

    // cleanInputs made an input of the target as of any source (every page of it is used, so it
    // was not cleaned): the input with the identity of the numbers replaces it
    m_documents.erase(std::remove_if(m_documents.begin(), m_documents.end(), [](const std::unique_ptr<DocumentInput>& input) { return input->source == 0; }), m_documents.end());
    m_documentOfSource.erase(0);
    createTargetInput();
    m_targetInput->isPagePlaced.assign(m_targetInput->pages.size(), false);
    if (!copySourceInputs())
    {
        Merger::Result result;
        result.isCanceled = true;
        return result;
    }

    const PDFObjectReference catalogReference = m_builder.getCatalogReference();
    const PDFDictionary* catalogDictionary = getDictionary(PDFObject::createReference(catalogReference));
    if (!catalogDictionary || !catalogDictionary->get("Pages").isReference())
    {
        throw PDFException(PDFDocumentMerger::tr("The document has no page tree."));
    }
    const PDFObjectReference pageTreeRoot = catalogDictionary->get("Pages").getReference();
    const bool isTargetTagged = getDictionary(catalogDictionary->get("StructTreeRoot")) != nullptr;

    // The locks of the signed signature fields of the target, read before a clone changes a field
    // (one walk of the whole field tree)
    {
        PDFObjectWalk walk(Helper::MAXIMUM_TREE_DEPTH);
        for (const PDFObject& field : getFields(catalogDictionary->get("AcroForm")))
        {
            collectFieldLocks(field, QByteArray(), nullptr, 0, &walk);
        }
    }

    // RB4: the items without the clone flag first, then the clones - an item, which is not a clone,
    // keeps the page itself (its object number, the outline, the links, the structure, the values
    // of its fields) wherever it stands; a page named by clones only is kept by its first clone
    std::vector<std::optional<ResultPage>> pagesOfItems(m_items.size());
    for (const bool isClonePass : { false, true })
    {
        for (size_t itemIndex = 0; itemIndex < m_items.size(); ++itemIndex)
        {
            if (m_items[itemIndex].isClone != isClonePass)
            {
                continue;
            }
            if (isCanceled())
            {
                Merger::Result result;
                result.isCanceled = true;
                return result;
            }

            ResultPage resultPage;
            if (createArrangedPage(itemIndex, &resultPage))
            {
                pagesOfItems[itemIndex] = resultPage;
            }
            if (m_progress)
            {
                m_progress->step();
            }
        }
    }

    // The pages of the result in the order of the items; the pages of the target, which stay
    // themselves, in their new order, and the new pages (clones, pages of the other sources, images,
    // blank pages) by the place among them
    const size_t targetPageCount = m_targetInput->pages.size();
    std::vector<std::vector<PDFObjectReference>> places(targetPageCount + 1);
    std::vector<PDFObjectReference> newPages;
    std::vector<PDFInteger> targetOrder;
    bool hasClone = false;
    for (const std::optional<ResultPage>& page : pagesOfItems)
    {
        if (!page)
        {
            continue;
        }
        m_resultPages.push_back(*page);
        hasClone = hasClone || page->isClone;
        if (page->input == m_targetInput && !page->isClone)
        {
            targetOrder.push_back(page->documentPage);
        }
        else
        {
            places[targetOrder.size()].push_back(page->page);
            newPages.push_back(page->page);
        }
    }

    // 7.7.3.2, R8: the relative order of the pages of the target kept - the new pages go into the
    // tree, which stays with its inherited values (Table 31); another order flattens it (as a tree,
    // whose leaves are not the pages of the catalog)
    bool isOrderKept = targetOrder.size() == targetPageCount;
    for (size_t i = 0; i < targetOrder.size() && isOrderKept; ++i)
    {
        isOrderKept = targetOrder[i] == PDFInteger(i);
    }
    std::vector<PageTreeLeaf> leaves;
    std::map<PDFObjectReference, PDFInteger> leafCounts;
    const bool isTreeKept = isOrderKept && readTargetPageTree(pageTreeRoot, &leaves, &leafCounts);
    if (isTreeKept)
    {
        insertIntoPageTreePlaces(leaves, leafCounts, places);
    }
    else
    {
        flattenArrangedPageTree(pageTreeRoot, newPages);
    }

    computeRuns();
    m_statistics.insertedPageCount = qint64(newPages.size());
    m_statistics.isPageTreeFlattened = m_statistics.isPageTreeFlattened || m_statistics.replacedPageCount > 0;

    // The catalog of the target is the catalog of the result. The other documents join it by the
    // rules of insert; without them nothing of the catalog is joined (a pure arrangement changes
    // no name tree, form, layer or structure of the target)
    bool hasOtherDocument = false;
    for (const std::unique_ptr<DocumentInput>& input : m_documents)
    {
        hasOtherDocument = hasOtherDocument || (!input->isLeftOut && !isTargetInput(input.get()));
    }

    catalogDictionary = getDictionary(PDFObject::createReference(catalogReference));
    PDFDictionaryBuilder catalog(*catalogDictionary);
    if (hasOtherDocument)
    {
        mergeNames(&catalog);
        mergeForm(&catalog);
        mergeLayers(&catalog);
        mergeStructure(&catalog);
        mergeOtherKeys(&catalog);
    }
    else if (!m_splitFields.empty())
    {
        // A field of /Fields split by a clone of its widget (R8): /Fields lists the new field
        const PDFObject acroFormObject = catalog.get("AcroForm");
        if (const PDFDictionary* acroForm = getDictionary(acroFormObject))
        {
            PDFDictionaryBuilder form(*acroForm);
            form.setEntry(Helper::key("Fields"), replaceSplitFields(acroForm->get("Fields")));
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

    // RB13: a clone has no /StructParents and a new page of no joined structure has no tags - in a
    // tagged target their content is not tagged (14.7.5.4)
    if (isTargetTagged && (hasClone || (!hasOtherDocument && !newPages.empty())))
    {
        m_statistics.insertedPagesUntagged = true;
    }

    // 12.4.2: every page keeps its label, a clone the label of its original (only a target with
    // labels gets them, as insert)
    std::vector<PDFObjectReference> resultPageReferences;
    resultPageReferences.reserve(m_resultPages.size());
    for (const ResultPage& page : m_resultPages)
    {
        resultPageReferences.push_back(page.page);
    }
    if (!m_targetInput->document->getCatalog()->getPageLabels().empty() && resultPageReferences != m_targetInput->pages)
    {
        const PDFObject labels = createPageLabels();
        if (!labels.isNull())
        {
            catalog.setEntry(Helper::key("PageLabels"), PDFObject::createReference(m_builder.addObject(labels)));
        }
    }

    // 14.12: the document parts are ranges of pages from /Start to /End - another order breaks them.
    // The pages removed by arrange went by removePages, which has made each range the pages of it,
    // which stay (the order kept); the new pages of a kept order join the part, inside of which (or
    // at the end of which) they are. The pages replaced by insert are a part of their own, which
    // the new pages would not take: their parts are removed.
    const bool arePartsKept = isOrderKept && (m_isArrange || m_statistics.replacedPageCount == 0);
    if (arePartsKept)
    {
        for (size_t place = 0; place < places.size(); ++place)
        {
            if (!places[place].empty())
            {
                m_position = PDFInteger(place);
                insertIntoDocumentParts(places[place]);
            }
        }
    }
    else
    {
        removeDocumentParts(&catalog);
    }

    const PDFVersion version = getInsertionVersion(target);
    if (target->getInfo()->version < version)
    {
        catalog.setEntry(Helper::key("Version"), PDFObject::createName(QByteArray::number(version.major) + "." + QByteArray::number(version.minor)));
    }
    m_builder.setObject(catalogReference, PDFObject::createDictionary(std::move(catalog)));

    if (!rewriteSourceInputs())
    {
        Merger::Result result;
        result.isCanceled = true;
        return result;
    }

    if (!isTreeKept || !arePartsKept)
    {
        // The nodes of the flattened tree and the document parts, which nothing reaches any more
        removeUnreachableTargetObjects(PDFObjectUtils::getReferences({ target->getStorage().getTrailerDictionary() }, target->getStorage()));
    }
    return createInsertionResult(target, version);
}

bool PDFDocumentMergerRun::createArrangedPage(size_t itemIndex, ResultPage* resultPage)
{
    // A page of the target: the page itself (its first item without the clone flag), or a clone;
    // a page of another source, an image or a blank page as by the insertion. False = its input
    // is left out.
    const Merger::Item& item = m_items[itemIndex];
    if (item.source != 0)
    {
        return createResultPage(itemIndex, false, resultPage);
    }

    const size_t index = size_t(item.pageIndex);
    resultPage->item = itemIndex;
    resultPage->input = m_targetInput;
    resultPage->documentPage = item.pageIndex;
    if (!m_targetInput->isPagePlaced[index])
    {
        m_targetInput->isPagePlaced[index] = true;
        resultPage->page = m_targetInput->pages[index];

        // A page without a turn and a crop stays unchanged (an incremental update does not write it)
        const QMarginsF& margins = item.cropMargins;
        const bool isCropped = margins.left() > 0.0 || margins.top() > 0.0 || margins.right() > 0.0 || margins.bottom() > 0.0;
        if (item.turn != PageRotation::None || isCropped)
        {
            setTargetPageGeometry(resultPage->page, item, item.pageIndex);
        }
    }
    else
    {
        resultPage->page = cloneTargetPage(item.pageIndex);
        resultPage->isClone = true;
        setTargetPageGeometry(resultPage->page, item, item.pageIndex);
    }
    return true;
}

PDFObjectReference PDFDocumentMergerRun::cloneTargetPage(PDFInteger documentPage)
{
    // A clone of a page of the target: a new page object and new annotations, the content shared,
    // no /StructParents, /B, /DPart and no widget of a signature field (clonePage, RB1, RB13). The
    // page may inherit its attributes from a node of the tree (7.7.3.4, Table 31): the clone gets
    // them itself, so it looks the same wherever it is placed
    const PDFPage* page = m_targetInput->document->getCatalog()->getPage(size_t(documentPage));
    if (!page)
    {
        throw PDFException(PDFDocumentMerger::tr("A page of the document cannot be read."));
    }

    const PDFObjectReference clone = clonePage(m_targetInput->pages[size_t(documentPage)]);
    const PDFDictionary* dictionary = getDictionary(PDFObject::createReference(clone));
    if (!dictionary)
    {
        throw PDFException(PDFDocumentMerger::tr("A page of the document cannot be read."));
    }

    PDFDictionaryBuilder inherited;
    if (!dictionary->hasKey("MediaBox"))
    {
        inherited.addEntry(Helper::key("MediaBox"), Helper::createRectangle(page->getMediaBox()));
    }
    if (!dictionary->hasKey("CropBox") && page->getCropBox().isValid() && page->getCropBox() != page->getMediaBox())
    {
        inherited.addEntry(Helper::key("CropBox"), Helper::createRectangle(page->getCropBox()));
    }
    if (!dictionary->hasKey("Resources") && !page->getResources().isNull())
    {
        inherited.addEntry(Helper::key("Resources"), page->getResources());
    }
    if (!inherited.isEmpty())
    {
        m_builder.mergeTo(clone, PDFObject::createDictionary(std::move(inherited)));
    }
    return clone;
}

void PDFDocumentMergerRun::setTargetPageGeometry(PDFObjectReference page, const Merger::Item& item, PDFInteger documentPage)
{
    // The turn is composed with the rotation of the page, also an inherited one (Table 31: 0 to
    // 270); the crop from its crop box (B14, B28). The value of /Rotate is written into the page
    // always - also 0, so that no node of the tree gives the page another one by inheritance
    setPageGeometry(page, item, m_targetInput, documentPage);
    const PDFDictionary* dictionary = getDictionary(PDFObject::createReference(page));
    if (dictionary && !dictionary->hasKey("Rotate"))
    {
        PDFDictionaryBuilder rotation;
        rotation.addEntry(Helper::key("Rotate"), PDFObject::createInteger(0));
        m_builder.mergeTo(page, PDFObject::createDictionary(std::move(rotation)));
    }
}

void PDFDocumentMergerRun::insertIntoPageTreePlaces(const std::vector<PageTreeLeaf>& leaves, const std::map<PDFObjectReference, PDFInteger>& leafCounts,
                                                    const std::vector<std::vector<PDFObjectReference>>& places)
{
    // R8 for more places: the new pages of the place before a page of the target go into /Kids of
    // the node of that page before it (the pages after the last page into the node of the last
    // page after it), /Count of every node above them grows. Nothing else of the tree changes, so
    // every inherited value of the pages of the target stays; a new page gets the inherited values
    // it must not take over (makeInheritedValuesExplicit).
    struct Insertion
    {
        size_t index = 0;   ///< Index in /Kids of the node
        size_t place = 0;   ///< The place among the pages of the target
    };

    std::map<PDFObjectReference, std::vector<Insertion>> insertions;
    std::map<PDFObjectReference, PDFInteger> added;
    const size_t pageCount = leaves.size();
    for (size_t place = 0; place < places.size() && pageCount > 0; ++place)
    {
        if (places[place].empty())
        {
            continue;
        }

        const PageTreeLeaf& leaf = leaves[qMin(place, pageCount - 1)];
        const PDFObjectReference parent = leaf.ancestors.back();
        insertions[parent].push_back({ leaf.indexInParent + (place < pageCount ? 0 : 1), place });
        for (const PDFObjectReference& node : leaf.ancestors)
        {
            added[node] += PDFInteger(places[place].size());
        }
        for (const PDFObjectReference& page : places[place])
        {
            PDFDictionaryBuilder parentEntry;
            parentEntry.addEntry(Helper::key("Parent"), PDFObject::createReference(parent));
            m_builder.mergeTo(page, PDFObject::createDictionary(std::move(parentEntry)));
            makeInheritedValuesExplicit(page, leaf.ancestors);
        }
    }

    std::map<PDFObjectReference, std::vector<PDFObject>> newKids;
    for (auto& [parent, parentInsertions] : insertions)
    {
        const PDFDictionary* parentDictionary = getDictionary(PDFObject::createReference(parent));
        std::vector<PDFObject> kids = parentDictionary ? Helper::getItems(getStorage(), parentDictionary->get("Kids")) : std::vector<PDFObject>();

        // From the last place: an insertion does not move the indices of the ones before it; the
        // pages of the earlier place of one index go before the later ones
        std::sort(parentInsertions.begin(), parentInsertions.end(), [](const Insertion& left, const Insertion& right)
        {
            return left.index != right.index ? left.index > right.index : left.place > right.place;
        });
        for (const Insertion& insertion : parentInsertions)
        {
            std::vector<PDFObject> pages;
            for (const PDFObjectReference& page : places[insertion.place])
            {
                pages.push_back(PDFObject::createReference(page));
            }
            const size_t index = qMin(insertion.index, kids.size());
            kids.insert(kids.begin() + qint64(index), pages.cbegin(), pages.cend());
        }
        newKids[parent] = std::move(kids);
    }

    for (const auto& [node, count] : added)
    {
        const PDFDictionary* nodeDictionary = getDictionary(PDFObject::createReference(node));
        PDFDictionaryBuilder nodeBuilder = nodeDictionary ? PDFDictionaryBuilder(*nodeDictionary) : PDFDictionaryBuilder();
        auto kids = newKids.find(node);
        if (kids != newKids.end())
        {
            nodeBuilder.setEntry(Helper::key("Kids"), Helper::createArray(kids->second));
        }
        auto leafCount = leafCounts.find(node);
        nodeBuilder.setEntry(Helper::key("Count"), PDFObject::createInteger((leafCount != leafCounts.cend() ? leafCount->second : 0) + count));
        m_builder.setObject(node, PDFObject::createDictionary(std::move(nodeBuilder)));
    }
}

void PDFDocumentMergerRun::flattenArrangedPageTree(PDFObjectReference pageTreeRoot, const std::vector<PDFObjectReference>& newPages)
{
    // The pages of the target in another order (or a damaged tree): the inherited values of the
    // tree are written into the pages of the target (flattenPageTree, Table 31) and the root lists
    // the pages of the result in their order (7.7.3.2); a new page gets the root as its parent and
    // its own values of what the root would give it
    m_builder.flattenPageTree();
    std::vector<PDFObjectReference> pages;
    pages.reserve(m_resultPages.size());
    for (const ResultPage& page : m_resultPages)
    {
        pages.push_back(page.page);
    }
    m_builder.setPages(pages);

    for (const PDFObjectReference& page : newPages)
    {
        PDFDictionaryBuilder parentEntry;
        parentEntry.addEntry(Helper::key("Parent"), PDFObject::createReference(pageTreeRoot));
        m_builder.mergeTo(page, PDFObject::createDictionary(std::move(parentEntry)));
        makeInheritedValuesExplicit(page, { pageTreeRoot });
    }
    m_statistics.isPageTreeFlattened = true;
}

void PDFDocumentMergerRun::removeDocumentParts(PDFDictionaryBuilder* catalog)
{
    // 14.12: the pages of a document part are a contiguous range from /Start to /End - another order
    // of the pages breaks the parts, they are removed with /DPart of the pages (as runSingle does)
    if (!catalog->hasKey("DPartRoot"))
    {
        return;
    }

    catalog->removeEntry("DPartRoot");
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
}

void PDFDocumentMergerRun::removeUnreachableTargetObjects(const std::set<PDFObjectReference>& reachableBefore)
{
    // An object of the target, which the target reached and the result does not (the nodes of a
    // flattened page tree, the document parts), is null - as removePages leaves the removed
    // objects; an incremental update writes it as null (RB12). Objects, which the target did not
    // reach, are left as they are (the business of Optimize Structure).
    const std::set<PDFObjectReference> reachableAfter = PDFObjectUtils::getReferences({ getStorage()->getTrailerDictionary() }, *getStorage());
    for (const PDFObjectReference& reference : reachableBefore)
    {
        if (reference.objectNumber >= 0 && size_t(reference.objectNumber) < m_targetObjectCount && !reachableAfter.count(reference))
        {
            m_builder.setObject(reference, PDFObject());
        }
    }
}

bool PDFDocumentMergerRun::isSignatureWidget(const PDFObject& annotation) const
{
    // A widget of a field of the type Sig: /FT of the widget (a merged field and widget) or of its
    // parent fields (inheritable, Table 226); one walk with a depth limit (a cycle of /Parent)
    const PDFDictionary* dictionary = getDictionary(annotation);
    if (!dictionary || Helper::getName(getObject(dictionary->get("Subtype"))) != "Widget")
    {
        return false;
    }

    PDFObjectWalk walk(Helper::MAXIMUM_TREE_DEPTH);
    PDFObject current = annotation;
    for (int depth = 0; walk.enter(current, depth); ++depth)
    {
        const PDFDictionary* node = getDictionary(current);
        if (!node)
        {
            return false;
        }
        if (node->hasKey("FT"))
        {
            return Helper::getName(getObject(node->get("FT"))) == "Sig";
        }
        current = node->get("Parent");
    }
    return false;
}

void PDFDocumentMergerRun::collectFieldLocks(const PDFObject& field, const QByteArray& inheritedType, const PDFDictionary* inheritedSignature, int depth, PDFObjectWalk* walk)
{
    // 12.7.5.5, Table 236: the lock dictionary (/Lock) of a signature field names the fields, which
    // are locked, when the field is signed (/V, the signature dictionary; /FT and /V inheritable,
    // Table 226). 12.8.2.4, Table 259: the signature itself carries the lock as the FieldMDP
    // transform parameters of its signature reference (Table 255 /Reference) - a validator checks
    // them, also without /Lock in the field. One walk of the whole tree: a cycle or a field shared
    // by two parents is read once.
    if (!walk->enter(field, depth))
    {
        return;
    }
    const PDFDictionary* dictionary = getDictionary(field);
    if (!dictionary)
    {
        return;
    }

    const QByteArray type = dictionary->hasKey("FT") ? Helper::getName(getObject(dictionary->get("FT"))) : inheritedType;
    const PDFDictionary* signature = dictionary->hasKey("V") ? getDictionary(dictionary->get("V")) : inheritedSignature;
    if (type == "Sig" && signature)
    {
        // An unknown action locks every field (the signature must stay valid, nothing is guessed)
        auto addLock = [this](const PDFDictionary* lock)
        {
            FieldLock fieldLock;
            fieldLock.action = Helper::getName(getObject(lock->get("Action")));
            if (fieldLock.action != "Include" && fieldLock.action != "Exclude")
            {
                fieldLock.action = "All";
            }
            for (const PDFObject& name : Helper::getItems(getStorage(), lock->get("Fields")))
            {
                fieldLock.fields.insert(Helper::getText(getObject(name)));
            }
            m_fieldLocks.push_back(std::move(fieldLock));
        };

        if (const PDFDictionary* lock = getDictionary(dictionary->get("Lock")))
        {
            addLock(lock);
        }
        for (const PDFObject& referenceObject : Helper::getItems(getStorage(), signature->get("Reference")))
        {
            const PDFDictionary* reference = getDictionary(referenceObject);
            if (reference && Helper::getName(getObject(reference->get("TransformMethod"))) == "FieldMDP")
            {
                const PDFDictionary* parameters = getDictionary(reference->get("TransformParams"));
                if (parameters)
                {
                    addLock(parameters);
                }
            }
        }
    }

    for (const PDFObject& kid : Helper::getItems(getStorage(), dictionary->get("Kids")))
    {
        collectFieldLocks(kid, type, signature, depth + 1, walk);
    }
}

bool PDFDocumentMergerRun::isLockedFieldWidget(const PDFObject& annotation) const
{
    // A widget of a field locked by a signed signature field (12.7.5.5): All locks every field,
    // Include the named ones, Exclude all but the named ones. A name is the fully qualified name of
    // the field or of a field above it (12.7.4.2, the partial names joined by periods).
    if (m_fieldLocks.empty())
    {
        return false;
    }
    const PDFDictionary* dictionary = getDictionary(annotation);
    if (!dictionary || Helper::getName(getObject(dictionary->get("Subtype"))) != "Widget")
    {
        return false;
    }

    // The partial names from the widget (a merged field and widget has its own) to the root
    std::vector<QString> partialNames;
    PDFObjectWalk walk(Helper::MAXIMUM_TREE_DEPTH);
    PDFObject current = annotation;
    for (int depth = 0; walk.enter(current, depth); ++depth)
    {
        const PDFDictionary* node = getDictionary(current);
        if (!node)
        {
            break;
        }
        if (node->hasKey("T"))
        {
            partialNames.push_back(Helper::getText(getObject(node->get("T"))));
        }
        current = node->get("Parent");
    }

    // The fully qualified names of the field and of the fields above it
    std::vector<QString> names;
    QString name;
    for (auto it = partialNames.crbegin(); it != partialNames.crend(); ++it)
    {
        name = name.isEmpty() ? *it : name + QLatin1Char('.') + *it;
        names.push_back(name);
    }
    if (names.empty())
    {
        // A widget of no field is no form field
        return false;
    }

    for (const FieldLock& lock : m_fieldLocks)
    {
        const bool isNamed = std::any_of(names.cbegin(), names.cend(), [&lock](const QString& fieldName) { return lock.fields.count(fieldName) > 0; });
        const bool isLocked = lock.action == "All" || (lock.action == "Include" && isNamed) || (lock.action == "Exclude" && !isNamed);
        if (isLocked)
        {
            return true;
        }
    }
    return false;
}

PDFDocument PDFDocumentMergerRun::runRemoveSignatures(const PDFDocument& document, PDFInteger* signaturesRemoved)
{
    m_builder.setDocument(&document);
    const PDFObjectReference catalogReference = m_builder.getCatalogReference();
    const PDFDictionary* catalogDictionary = getDictionary(PDFObject::createReference(catalogReference));
    if (!catalogDictionary)
    {
        return document;
    }

    const PDFObject acroFormObject = catalogDictionary->get("AcroForm");
    removeSignatures(getFields(acroFormObject));

    catalogDictionary = getDictionary(PDFObject::createReference(catalogReference));
    PDFDictionaryBuilder catalog(*catalogDictionary);
    const PDFDictionary* permissions = getDictionary(catalog.get("Perms"));
    const bool isCertified = permissions && !getObject(permissions->get("DocMDP")).isNull();
    if (m_statistics.signaturesRemoved == 0 && !isCertified)
    {
        if (signaturesRemoved)
        {
            *signaturesRemoved = 0;
        }
        return document;
    }

    // Bit 2 (AppendOnly) of /SigFlags goes with the signatures (Table 225)
    if (const PDFDictionary* acroForm = getDictionary(acroFormObject))
    {
        const PDFObject& flags = getObject(acroForm->get("SigFlags"));
        if (flags.isInt() && (flags.getInteger() & 2))
        {
            PDFDictionaryBuilder form(*acroForm);
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

    // The certification, the usage rights and the validation data belong to the signatures
    catalog.removeEntry("Perms");
    catalog.removeEntry("DSS");
    m_builder.setObject(catalogReference, PDFObject::createDictionary(std::move(catalog)));

    if (signaturesRemoved)
    {
        *signaturesRemoved = m_statistics.signaturesRemoved;
    }
    PDFDocument built = m_builder.build();
    return PDFDocument(PDFObjectStorage(built.getStorage()), document.getInfo()->version, document.getSourceDataHash());
}

PDFDocumentMerger::Result PDFDocumentMerger::insert(const PDFDocument& target, PDFInteger position, std::vector<PDFInteger> replacedPages, const std::vector<Source>& sources,
                                                    const std::vector<Item>& items, const Settings& settings, const PDFOperationControl* control, PDFProgressSink* progress)
{
    Result result;
    const PDFInteger pageCount = PDFInteger(target.getCatalog()->getPageCount());
    if (position < 0 || position > pageCount)
    {
        result.errorMessage = tr("The place of the insertion is outside of the document of %Ln pages.", nullptr, int(pageCount));
        return result;
    }
    if (target.getCatalog()->isXFANeedsRendering())
    {
        result.errorMessage = tr("The document is a dynamic XFA form: pages cannot be inserted into it.");
        return result;
    }

    std::sort(replacedPages.begin(), replacedPages.end());
    replacedPages.erase(std::unique(replacedPages.begin(), replacedPages.end()), replacedPages.end());
    for (const PDFInteger page : replacedPages)
    {
        if (page < 0 || page >= pageCount)
        {
            result.errorMessage = tr("The document has no page %1.").arg(page + 1);
            return result;
        }
    }
    if (!replacedPages.empty() && PDFInteger(replacedPages.size()) >= pageCount)
    {
        result.errorMessage = tr("Every page of the document would be replaced. Insert the pages at the beginning and remove the old pages with Remove Pages.");
        return result;
    }

    // B20: the replaced pages go with everything pointing at them; the result of the removal is the
    // target of the insertion - a failure of either leaves the document as it was
    PDFDocumentPointer targetDocument(new PDFDocument(target));
    PDFDocumentPageOperations::RemovalStatistics removal;
    if (!replacedPages.empty())
    {
        try
        {
            PDFDocument cleaned = PDFDocumentPageOperations::removePages(&target, replacedPages, control, &removal);
            if (removal.isCanceled || PDFOperationControl::isOperationCancelled(control))
            {
                result.isCanceled = true;
                return result;
            }
            targetDocument.reset(new PDFDocument(std::move(cleaned)));
        }
        catch (const PDFException& exception)
        {
            result.errorMessage = exception.getMessage();
            return result;
        }
        position -= PDFInteger(std::lower_bound(replacedPages.cbegin(), replacedPages.cend(), position) - replacedPages.cbegin());
    }

    // The target is the source 0 (never named by an item), the sources follow it
    std::vector<Source> allSources;
    allSources.reserve(sources.size() + 1);
    allSources.push_back(Source::createDocument(targetDocument, QString()));
    allSources.insert(allSources.end(), sources.cbegin(), sources.cend());
    std::vector<Item> allItems = items;
    for (Item& item : allItems)
    {
        ++item.source;
    }

    PDFDocumentMergerRun run(allSources, allItems, settings, control, progress);
    run.setInsertion(position);
    run.setReplacement(removal, qint64(replacedPages.size()));
    result = run.run();
    for (LeftOut& leftOut : result.leftOut)
    {
        leftOut.source = leftOut.source > 0 ? leftOut.source - 1 : 0;
    }
    return result;
}

PDFDocumentMerger::Result PDFDocumentMerger::arrange(const PDFDocument& target, const std::vector<Source>& sources, const std::vector<Item>& items,
                                                     const Settings& settings, const PDFOperationControl* control, PDFProgressSink* progress)
{
    Result result;
    const PDFCatalog* catalog = target.getCatalog();
    const size_t pageCount = catalog->getPageCount();
    if (sources.empty() || sources.front().kind != Source::Kind::Document)
    {
        result.errorMessage = tr("The first input of an arrangement must be the document itself.");
        return result;
    }
    if (catalog->isXFANeedsRendering())
    {
        result.errorMessage = tr("The document is a dynamic XFA form: its pages cannot be arranged.");
        return result;
    }

    // RB2: the plan was made for these page objects in this order. Another version of the document
    // (a page inserted, removed or moved meanwhile by another change) is refused - a page, which
    // the plan does not know, is never removed
    std::vector<PDFObjectReference> pages;
    pages.reserve(pageCount);
    for (size_t i = 0; i < pageCount; ++i)
    {
        pages.push_back(catalog->getPage(i)->getPageReference());
    }
    if (pages != settings.targetPages)
    {
        result.errorMessage = tr("The document has changed. Its pages were arranged for another version of it, arrange them again.");
        return result;
    }

    // The pages of the target, which the items use (the source 0); the others are removed
    std::vector<bool> isUsed(pageCount, false);
    bool isCropped = false;
    for (const Item& item : items)
    {
        if (item.source >= sources.size())
        {
            result.errorMessage = tr("A page of the merge has no input.");
            return result;
        }
        if (item.source != 0)
        {
            continue;
        }
        if (item.pageIndex < 0 || item.pageIndex >= PDFInteger(pageCount))
        {
            result.errorMessage = tr("The document has no page %1.").arg(item.pageIndex + 1);
            return result;
        }
        isUsed[size_t(item.pageIndex)] = true;
        const QMarginsF& margins = item.cropMargins;
        isCropped = isCropped || margins.left() > 0.0 || margins.top() > 0.0 || margins.right() > 0.0 || margins.bottom() > 0.0;
    }

    // ISO 32000-2, Table 22: bit 11 (assemble: insert, rotate, remove pages), bit 4 for a crop (Q4
    // of step 3). The result stays encrypted with the permissions of the target, so no other bit is
    // needed (unlike a merge into a new document). The owner password verified by the caller grants
    // every bit (Q15 of step 2).
    const PDFSecurityHandler* securityHandler = target.getStorage().getSecurityHandler();
    if (!sources.front().isOwnerPasswordVerified && securityHandler &&
        (!PDFSecurityHandler::isAllowedByRevision(securityHandler, PDFSecurityHandler::Permission::Assemble) ||
         (isCropped && !PDFSecurityHandler::isAllowedByRevision(securityHandler, PDFSecurityHandler::Permission::Modify))))
    {
        const QString name = sources.front().name.isEmpty() ? tr("The document") : sources.front().name;
        result.errorMessage = tr("%1 does not allow its pages to be arranged. If you own it, unlock it with its owner password.").arg(name);
        return result;
    }

    std::vector<PDFInteger> removedPages;
    std::vector<PDFInteger> pageMap(pageCount, -1);
    for (size_t i = 0; i < pageCount; ++i)
    {
        if (isUsed[i])
        {
            pageMap[i] = PDFInteger(i - removedPages.size());
        }
        else
        {
            removedPages.push_back(PDFInteger(i));
        }
    }
    if (removedPages.size() == pageCount)
    {
        result.errorMessage = tr("The document needs at least one page.");
        return result;
    }

    // The unused pages go with everything pointing at them (the rules of Remove Pages: the outline,
    // the destinations, the links, the fields, the structure tree, the articles; the object numbers
    // stay); the result of the removal is the target of the arrangement - a failure of either leaves
    // the document as it was
    PDFDocumentPointer targetDocument(new PDFDocument(target));
    PDFDocumentPageOperations::RemovalStatistics removal;
    if (!removedPages.empty())
    {
        try
        {
            PDFDocument cleaned = PDFDocumentPageOperations::removePages(&target, removedPages, control, &removal);
            if (removal.isCanceled || PDFOperationControl::isOperationCancelled(control))
            {
                result.isCanceled = true;
                return result;
            }
            targetDocument.reset(new PDFDocument(std::move(cleaned)));
        }
        catch (const PDFException& exception)
        {
            result.errorMessage = exception.getMessage();
            return result;
        }
    }

    // The target is the source 0 of the run (its pages renumbered after the removal). It stays
    // encrypted with its permissions - the rule of getPermission for an input of a new document
    // (every restriction would be lost) does not apply to it; its permission was checked above.
    std::vector<Source> allSources = sources;
    allSources.front() = Source::createDocument(targetDocument, sources.front().name);
    allSources.front().isOwnerPasswordVerified = true;
    std::vector<Item> allItems = items;
    for (Item& item : allItems)
    {
        if (item.source == 0)
        {
            item.pageIndex = pageMap[size_t(item.pageIndex)];
        }
    }

    PDFDocumentMergerRun run(allSources, allItems, settings, control, progress);
    run.setArrangement(removal, qint64(removedPages.size()));
    return run.run();
}

PDFDocument PDFDocumentMerger::removeSignatures(const PDFDocument& document, PDFInteger* signaturesRemoved)
{
    const std::vector<Source> sources;
    const std::vector<Item> items;
    PDFDocumentMergerRun run(sources, items, Settings(), nullptr, nullptr);
    return run.runRemoveSignatures(document, signaturesRemoved);
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

            if (std::string_view(tree) == "JavaScript" && !isTargetInput(input.get()))
            {
                // Scripts of the documents are removed (Table 29, 12.6; Q8 of the first batch);
                // the target of an insertion keeps its own
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
    std::vector<PDFObject> sourceFields;                ///< Insert: the fields of the sources
    std::optional<PDFDictionaryBuilder> targetForm;     ///< Insert: a copy of the form of the target
    PDFObject targetFormObject;                         ///< Insert: the entry /AcroForm of the target

    for (const std::unique_ptr<DocumentInput>& input : m_documents)
    {
        const PDFDictionary* inputCatalog = input->isLeftOut ? nullptr : getDictionary(input->catalog);
        const PDFDictionary* acroForm = inputCatalog ? getDictionary(inputCatalog->get("AcroForm")) : nullptr;
        if (!acroForm)
        {
            continue;
        }
        hasForm = true;

        // The target of an insertion keeps its form as it is (only the sources are joined to it)
        const bool isTarget = isTargetInput(input.get());
        if (isTarget)
        {
            targetForm = PDFDictionaryBuilder(*acroForm);
            targetFormObject = inputCatalog->get("AcroForm");
        }
        m_statistics.xfaRemoved = m_statistics.xfaRemoved || (!isTarget && !getObject(acroForm->get("XFA")).isNull());
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
        if (!isTarget)
        {
            sourceFields.insert(sourceFields.end(), inputFields.cbegin(), inputFields.cend());
        }
    }

    if (!hasForm)
    {
        catalog->removeEntry("AcroForm");
        return;
    }

    // The signatures of the target of an insertion stay (they are saved incrementally, 5.7)
    removeSignatures(m_isInsert ? sourceFields : fields);

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

    // Insert: the form of the target is the base - its other keys (/XFA, /SigFlags with bit 2 of
    // its signatures, /DA, /Q) stay, it keeps its object number
    const bool isTargetFormKept = m_isInsert && targetForm.has_value();
    PDFDictionaryBuilder form = isTargetFormKept ? *targetForm : PDFDictionaryBuilder();
    form.setEntry(Helper::key("Fields"), Helper::createArray(fields));
    if (needAppearances)
    {
        form.setEntry(Helper::key("NeedAppearances"), PDFObject::createBool(true));
    }
    if (isTargetFormKept)
    {
        const PDFObject& flags = getObject(form.get("SigFlags"));
        const PDFInteger targetFlags = flags.isInt() ? flags.getInteger() : 0;
        if (hasSignatureField && !(targetFlags & 1))
        {
            form.setEntry(Helper::key("SigFlags"), PDFObject::createInteger(targetFlags | 1));
        }
    }
    else if (hasSignatureField)
    {
        form.setEntry(Helper::key("SigFlags"), PDFObject::createInteger(1));
    }
    if (!calculationOrder.empty())
    {
        form.setEntry(Helper::key("CO"), Helper::createArray(calculationOrder));
    }
    if (!resources.empty())
    {
        PDFDictionaryBuilder resourceDictionary;
        for (auto& [category, entries] : resources)
        {
            resourceDictionary.addEntry(PDFInplaceOrMemoryString(category), PDFObject::createDictionary(std::move(entries)));
        }
        form.setEntry(Helper::key("DR"), PDFObject::createDictionary(std::move(resourceDictionary)));
    }
    if (resultAppearance && !(isTargetFormKept && form.hasKey("DA")))
    {
        form.setEntry(Helper::key("DA"), PDFObject::createString(*resultAppearance));
    }
    if (resultQuadding.value_or(0) != 0 && !(isTargetFormKept && form.hasKey("Q")))
    {
        form.setEntry(Helper::key("Q"), PDFObject::createInteger(*resultQuadding));
    }

    if (isTargetFormKept && targetFormObject.isReference())
    {
        m_builder.setObject(targetFormObject.getReference(), PDFObject::createDictionary(std::move(form)));
        catalog->setEntry(Helper::key("AcroForm"), targetFormObject);
        return;
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
            // The target of an insertion keeps its alternate configurations
            m_statistics.layerConfigurationsRemoved = m_statistics.layerConfigurationsRemoved ||
                                                      (!isTargetInput(input.get()) && properties && !Helper::getItems(getStorage(), properties->get("Configs")).empty());
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
            if (isTargetInput(layers.input))
            {
                // The order of the target of an insertion stays as it is, the sources follow it
                order.insert(order.end(), inputOrder.cbegin(), inputOrder.cend());
            }
            else if (inputs.size() > 1)
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

    // Insert: the properties of the target are the base (its alternate configurations stay)
    const bool isTargetKept = m_isInsert && inputs.front().input == m_targetInput && inputs.front().properties;
    PDFDictionaryBuilder properties = isTargetKept ? PDFDictionaryBuilder(*inputs.front().properties) : PDFDictionaryBuilder();
    properties.setEntry(Helper::key("OCGs"), Helper::createArray(groups));
    properties.setEntry(Helper::key("D"), PDFObject::createDictionary(std::move(configuration)));
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

    // Insert (5.7): the structure of the target stays; the sources join it as document fragments
    // only when the target is tagged and every source is tagged and taken whole, else the inserted
    // pages are outside of the structure (their tags go, the target is not touched)
    QString targetLanguage;
    if (m_isInsert)
    {
        bool isTargetTagged = false;
        bool areSourcesJoinable = true;
        for (DocumentInput* input : inputs)
        {
            const PDFDictionary* inputCatalog = getDictionary(input->catalog);
            const bool isTagged = inputCatalog && getDictionary(inputCatalog->get("StructTreeRoot"));
            if (isTargetInput(input))
            {
                isTargetTagged = isTagged && inputCatalog->get("StructTreeRoot").isReference();
                targetLanguage = Helper::getText(getObject(inputCatalog->get("Lang")));
            }
            else
            {
                areSourcesJoinable = areSourcesJoinable && isTagged && input->isWhole;
            }
        }

        bool hasUntaggedItem = false;
        for (const Merger::Item& item : m_items)
        {
            hasUntaggedItem = hasUntaggedItem || m_sources[item.source].kind != Merger::Source::Kind::Document;
        }

        if (!isTargetTagged || !areSourcesJoinable || inputs.size() < 2)
        {
            for (DocumentInput* input : inputs)
            {
                input->isStructureStripped = !isTargetInput(input);
            }
            m_statistics.insertedPagesUntagged = isTargetTagged;
            return;
        }
        m_statistics.insertedPagesUntagged = hasUntaggedItem;
    }

    // 14.9.2: one language of all inputs is the language of the result (the target of an
    // insertion keeps its own; a fragment of another language says it)
    const bool hasCommonLanguage = !m_isInsert && languages.size() == 1 && !languages.begin()->isEmpty();
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

    // Insert: the root of the target keeps its number; the fragments go below its Document
    // element (the one top element of the type Document), else below the root itself
    PDFObjectReference rootReference;
    PDFObjectReference documentReference;
    if (m_isInsert)
    {
        const PDFDictionary* targetCatalog = getDictionary(m_targetInput->catalog);
        rootReference = targetCatalog->get("StructTreeRoot").getReference();
        documentReference = rootReference;
        const std::vector<PDFObject> topElements = Helper::getItems(getStorage(), getDictionary(PDFObject::createReference(rootReference))->get("K"));
        const PDFDictionary* topElement = topElements.size() == 1 ? getDictionary(topElements.front()) : nullptr;
        if (topElement && topElements.front().isReference() && Helper::getName(topElement->get("S")) == "Document")
        {
            documentReference = topElements.front().getReference();
        }
    }
    else
    {
        rootReference = m_builder.addObject(PDFObject());
        documentReference = m_builder.addObject(PDFObject());
    }

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

        // The elements of the target of an insertion stay where they are
        const bool isTarget = isTargetInput(input);
        const PDFObjectReference fragmentReference = isTarget ? PDFObjectReference() : m_builder.addObject(PDFObject());
        std::vector<PDFObject> kids;
        for (const PDFObject& kid : isTarget ? std::vector<PDFObject>() : Helper::getItems(getStorage(), treeRoot->get("K")))
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

        if (!isTarget)
        {
            PDFDictionaryBuilder fragment;
            fragment.addEntry(Helper::key("Type"), PDFObject::createName("StructElem"));
            fragment.addEntry(Helper::key("S"), PDFObject::createName("DocumentFragment"));
            fragment.addEntry(Helper::key("NS"), PDFObject::createReference(pdf2NamespaceReference));
            fragment.addEntry(Helper::key("P"), PDFObject::createReference(documentReference));
            fragment.addEntry(Helper::key("K"), Helper::createArray(kids));
            const QString language = Helper::getText(getObject(inputCatalog->get("Lang")));
            if (!hasCommonLanguage && !language.isEmpty() && language != targetLanguage)
            {
                fragment.addEntry(Helper::key("Lang"), PDFObjectFactory::createTextString(language));
            }
            fragment.addEntry(Helper::key("T"), PDFObjectFactory::createTextString(input->title));
            m_builder.setObject(fragmentReference, PDFObject::createDictionary(std::move(fragment)));
            fragments.push_back(PDFObject::createReference(fragmentReference));
        }

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

    // Insert: the fragments follow the elements of the Document element of the target (or of
    // its root); the root keeps its other keys
    const PDFDictionary* targetRoot = m_isInsert ? getDictionary(PDFObject::createReference(rootReference)) : nullptr;
    PDFDictionaryBuilder root = targetRoot ? PDFDictionaryBuilder(*targetRoot) : PDFDictionaryBuilder();
    if (m_isInsert)
    {
        if (documentReference != rootReference)
        {
            const PDFDictionary* documentElement = getDictionary(PDFObject::createReference(documentReference));
            std::vector<PDFObject> documentKids = Helper::getItems(getStorage(), documentElement->get("K"));
            documentKids.insert(documentKids.end(), fragments.cbegin(), fragments.cend());
            PDFDictionaryBuilder documentBuilder(*documentElement);
            documentBuilder.setEntry(Helper::key("K"), Helper::createArray(documentKids));
            m_builder.setObject(documentReference, PDFObject::createDictionary(std::move(documentBuilder)));
        }
        else
        {
            std::vector<PDFObject> rootKids = Helper::getItems(getStorage(), root.get("K"));
            rootKids.insert(rootKids.end(), fragments.cbegin(), fragments.cend());
            root.setEntry(Helper::key("K"), Helper::createArray(rootKids));
        }
    }
    else
    {
        PDFDictionaryBuilder document;
        document.addEntry(Helper::key("Type"), PDFObject::createName("StructElem"));
        document.addEntry(Helper::key("S"), PDFObject::createName("Document"));
        document.addEntry(Helper::key("NS"), PDFObject::createReference(pdf2NamespaceReference));
        document.addEntry(Helper::key("P"), PDFObject::createReference(rootReference));
        document.addEntry(Helper::key("K"), Helper::createArray(fragments));
        m_builder.setObject(documentReference, PDFObject::createDictionary(std::move(document)));

        root.addEntry(Helper::key("Type"), PDFObject::createName("StructTreeRoot"));
        root.addEntry(Helper::key("K"), PDFObject::createReference(documentReference));
    }

    std::sort(parentTree.begin(), parentTree.end(), [](const auto& left, const auto& right) { return left.first < right.first; });
    root.setEntry(Helper::key("ParentTree"), PDFObject::createReference(m_builder.addObject(Helper::createNumberTree(parentTree))));
    root.setEntry(Helper::key("ParentTreeNextKey"), PDFObject::createInteger(nextKey));
    root.setEntry(Helper::key("Namespaces"), Helper::createArray(namespaces));
    if (!roleMap.isEmpty())
    {
        root.setEntry(Helper::key("RoleMap"), PDFObject::createDictionary(std::move(roleMap)));
    }
    if (!classMap.isEmpty())
    {
        root.setEntry(Helper::key("ClassMap"), PDFObject::createDictionary(std::move(classMap)));
    }
    if (!idTree.empty())
    {
        root.setEntry(Helper::key("IDTree"), PDFObject::createReference(m_builder.addObject(Helper::createNameTree(idTree))));
    }
    m_builder.setObject(rootReference, PDFObject::createDictionary(std::move(root)));

    // The mark information of the target stays, a suspect source makes it suspect
    const PDFDictionary* targetMarkInfo = m_isInsert ? getDictionary(catalog->get("MarkInfo")) : nullptr;
    PDFDictionaryBuilder markInfo = targetMarkInfo ? PDFDictionaryBuilder(*targetMarkInfo) : PDFDictionaryBuilder();
    markInfo.setEntry(Helper::key("Marked"), PDFObject::createBool(true));
    if (isSuspect)
    {
        markInfo.setEntry(Helper::key("Suspects"), PDFObject::createBool(true));
    }
    if (hasUserProperties)
    {
        markInfo.setEntry(Helper::key("UserProperties"), PDFObject::createBool(true));
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

    const std::vector<Section> sections = getSections();
    if (sections.empty())
    {
        return;
    }
    const PDFObjectReference outlineReference = m_builder.addObject(PDFObject());
    std::vector<Written> runItems;

    for (size_t sectionIndex = 0; sectionIndex < sections.size(); ++sectionIndex)
    {
        // A run (or a part of the settings, QB3 of step 3b: Join puts the outlines of the inputs of
        // its pages below it, in the order of their first pages; a run has one input)
        const Section& section = sections[sectionIndex];
        const ResultPage& firstPage = m_resultPages[section.first];

        std::vector<Written> children;
        if (isInputOutlineKept)
        {
            std::vector<DocumentInput*> sectionInputs;
            std::set<const DocumentInput*> seenInputs;
            for (size_t i = section.first; i < section.first + section.count; ++i)
            {
                DocumentInput* input = m_resultPages[i].input;
                if (input && outlines.count(input) && seenInputs.insert(input).second)
                {
                    sectionInputs.push_back(input);
                }
            }

            for (DocumentInput* input : sectionInputs)
            {
                std::set<PDFObjectReference> runPages;
                for (size_t i = section.first; i < section.first + section.count; ++i)
                {
                    if (m_resultPages[i].input == input && !m_resultPages[i].isClone)
                    {
                        runPages.insert(m_resultPages[i].page);
                    }
                }
                const bool isFirstRun = !firstRuns.count(input);
                firstRuns.emplace(input, sectionIndex);
                const std::vector<Written> written = writeItems(input, topItems[input], runPages, isFirstRun, 0);
                children.insert(children.end(), written.cbegin(), written.cend());
            }
        }

        PDFDictionaryBuilder item;
        item.addEntry(Helper::key("Title"), PDFObjectFactory::createTextString(section.title));
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

        // The catalog of the target of an insertion is the catalog of the result: its own keys stay
        if (!isTargetInput(inputs[i]))
        {
            m_statistics.collectionRemoved = m_statistics.collectionRemoved || !getObject(inputCatalog->get("Collection")).isNull();
            m_statistics.scriptsRemoved = m_statistics.scriptsRemoved || !getObject(inputCatalog->get("OpenAction")).isNull() || !getObject(inputCatalog->get("AA")).isNull();
        }

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
        // The target of an insertion keeps its base in its catalog, its URIs stay relative
        for (size_t i = 0; i < catalogs.size(); ++i)
        {
            const PDFDictionary* uri = getDictionary(catalogs[i]->get("URI"));
            inputs[i]->uriBase = (uri && !isTargetInput(inputs[i])) ? Helper::getText(getObject(uri->get("Base"))) : QString();
        }
    }

    // R8: the output intents of the target of an insertion always stay (the catalog is its own)
    if (m_isInsert)
    {
        return;
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
