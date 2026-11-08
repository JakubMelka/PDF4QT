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

#include "pdfdocumenttool_protect.h"

#include "pdfdocumentsanitizer.h"
#include "pdfocrpagepreparer.h"
#include "pdfoperationcontrol.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QLocale>

#include <algorithm>
#include <limits>
#include <map>

#include "pdfdbgheap.h"

namespace pdf
{

/// Helpers of the tools Remove Hidden Data and Remove External Links
class PDFDocumentToolProtectHelper
{
public:
    PDFDocumentToolProtectHelper() = delete;

    using Kind = PDFDocumentHiddenDataScan::Kind;

    // The rows of 438:12588 (the signature fields are the tenth kind of owner answer Q10)
    static constexpr const char* TITLES[PDFDocumentHiddenDataScan::KIND_COUNT] = {
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolSanitize", "Document info"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolSanitize", "All metadata"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolSanitize", "Comments and markup"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolSanitize", "File attachments"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolSanitize", "Outline"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolSanitize", "Page labels"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolSanitize", "Embedded search index"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolSanitize", "Page thumbnails"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolSanitize", "Invisible text (OCR layer)"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolSanitize", "Signature fields")
    };

    static constexpr const char* DESCRIPTIONS[PDFDocumentHiddenDataScan::KIND_COUNT] = {
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolSanitize", "Author, title, subject, producer and dates"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolSanitize", "XMP metadata streams of the document and of its objects"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolSanitize", "Comments, highlights and other markup annotations"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolSanitize", "Files embedded in the document and in its annotations"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolSanitize", "The bookmarks of the document"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolSanitize", "Custom numbering of the pages"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolSanitize", "A search index stored in the file"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolSanitize", "Small pictures of the pages stored in the file"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolSanitize", "The text layer of scanned pages: search and copy stop working on them"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolSanitize", "Signature fields with the names, reasons and certificates of the signatures; the signatures are lost")
    };

    static constexpr const char* ICONS[PDFDocumentHiddenDataScan::KIND_COUNT] = {
        "info", "metadata", "message-square", "paperclip", "bookmark", "hash", "search-index", "images", "text-hidden", "signature-field"
    };

    /// Weight of a step in the bar (the decoding of the content streams takes the longest)
    static int getWeight(Kind kind)
    {
        switch (kind)
        {
            case Kind::InvisibleText:
                return 40;
            case Kind::Comments:
            case Kind::Attachments:
            case Kind::SignatureFields:
                return 10;
            default:
                return 5;
        }
    }

    /// The text of a count, the singular right in English also without a translation
    static QString plural(int count, const char* singular, const char* pluralText)
    {
        return count == 1 ? PDFDocumentToolSanitize::tr(singular, nullptr, count) : PDFDocumentToolSanitize::tr(pluralText, nullptr, count);
    }

    static QString pluralLinks(int count, const char* singular, const char* pluralText)
    {
        return count == 1 ? PDFDocumentToolRemoveLinks::tr(singular, nullptr, count) : PDFDocumentToolRemoveLinks::tr(pluralText, nullptr, count);
    }

    /// Suggested name of a changed document
    static QString createFileName(const QString& fileName, const QString& suffix)
    {
        const QString baseName = QFileInfo(fileName).completeBaseName();
        return (baseName.isEmpty() ? QStringLiteral("document") : baseName) + suffix + QStringLiteral(".pdf");
    }

    static int toInt(qint64 value)
    {
        return int(qBound<qint64>(0, value, qint64(std::numeric_limits<int>::max())));
    }
};

// ------------------------------------------------------------------ Remove Hidden Data

QString PDFDocumentToolSanitize::getKindTitle(Kind kind)
{
    return tr(PDFDocumentToolProtectHelper::TITLES[size_t(kind)]);
}

QString PDFDocumentToolSanitize::getKindDescription(Kind kind)
{
    return tr(PDFDocumentToolProtectHelper::DESCRIPTIONS[size_t(kind)]);
}

QString PDFDocumentToolSanitize::getKindIconName(Kind kind)
{
    return QString::fromLatin1(PDFDocumentToolProtectHelper::ICONS[size_t(kind)]);
}

QString PDFDocumentToolSanitize::formatKindCount(int count, bool isOfHiddenData)
{
    using Helper = PDFDocumentToolProtectHelper;
    if (isOfHiddenData)
    {
        return Helper::plural(count, QT_TR_NOOP("%Ln kind of hidden data"), QT_TR_NOOP("%Ln kinds of hidden data"));
    }
    return Helper::plural(count, QT_TR_NOOP("%Ln kind"), QT_TR_NOOP("%Ln kinds"));
}

QString PDFDocumentToolSanitize::getStepText(Kind kind, const PDFDocumentHiddenDataScan::KindResult& found)
{
    using Helper = PDFDocumentToolProtectHelper;

    const int count = Helper::toInt(found.count);
    switch (kind)
    {
        case Kind::DocumentInfo:
            return tr("Document info was removed");
        case Kind::Metadata:
            return Helper::plural(count, QT_TR_NOOP("%Ln metadata stream removed"), QT_TR_NOOP("%Ln metadata streams removed"));
        case Kind::Comments:
            return Helper::plural(count, QT_TR_NOOP("%Ln markup annotation removed"), QT_TR_NOOP("%Ln markup annotations removed"));
        case Kind::Attachments:
            return Helper::plural(count, QT_TR_NOOP("%Ln file attachment removed"), QT_TR_NOOP("%Ln file attachments removed"));
        case Kind::Outline:
            return tr("Outline was removed");
        case Kind::PageLabels:
            return tr("Page labels were removed");
        case Kind::SearchIndex:
            return tr("Search index was removed");
        case Kind::Thumbnails:
            return Helper::plural(count, QT_TR_NOOP("%Ln page thumbnail removed"), QT_TR_NOOP("%Ln page thumbnails removed"));
        case Kind::InvisibleText:
            return Helper::plural(count, QT_TR_NOOP("Invisible text removed on %Ln page"), QT_TR_NOOP("Invisible text removed on %Ln pages"));
        case Kind::SignatureFields:
            return Helper::plural(count, QT_TR_NOOP("%Ln signature field removed"), QT_TR_NOOP("%Ln signature fields removed"));
    }
    return QString();
}

QVariantMap PDFDocumentToolSanitize::getPresetValues(Preset preset)
{
    std::set<Kind> chosen;
    switch (preset)
    {
        case Preset::Standard:
            chosen = { Kind::DocumentInfo, Kind::Metadata, Kind::Comments, Kind::Attachments, Kind::Outline, Kind::PageLabels, Kind::SearchIndex, Kind::Thumbnails };
            break;
        case Preset::BeforeSending:
            chosen = { Kind::DocumentInfo, Kind::Metadata, Kind::Comments, Kind::SearchIndex, Kind::Thumbnails };
            break;
        case Preset::BeforePublishing:
            chosen = { Kind::DocumentInfo, Kind::Metadata, Kind::Comments, Kind::Attachments, Kind::SearchIndex, Kind::Thumbnails };
            break;
        case Preset::Everything:
        {
            const std::vector<Kind> kinds = PDFDocumentHiddenDataScan::getKinds();
            chosen = std::set<Kind>(kinds.cbegin(), kinds.cend());
            break;
        }
        case Preset::Custom:
            break;
    }

    QVariantMap values;
    for (Kind kind : PDFDocumentHiddenDataScan::getKinds())
    {
        values.insert(PDFDocumentHiddenDataScan::getName(kind), chosen.count(kind) > 0);
    }
    return values;
}

PDFDocumentToolSanitize::Preset PDFDocumentToolSanitize::findPreset(const QVariantMap& values, const std::vector<Kind>& comparedKinds)
{
    for (Preset preset : { Preset::Standard, Preset::BeforeSending, Preset::BeforePublishing, Preset::Everything })
    {
        const QVariantMap presetValues = getPresetValues(preset);
        bool isMatch = true;
        for (Kind kind : comparedKinds)
        {
            const QString name = PDFDocumentHiddenDataScan::getName(kind);
            isMatch = isMatch && values.value(name).toBool() == presetValues.value(name).toBool();
        }
        if (isMatch)
        {
            return preset;
        }
    }
    return Preset::Custom;
}

std::vector<PDFDocumentToolSanitize::Kind> PDFDocumentToolSanitize::getChosenKinds(const QVariantMap& values)
{
    std::vector<Kind> kinds;
    for (Kind kind : PDFDocumentHiddenDataScan::getKinds())
    {
        if (values.value(PDFDocumentHiddenDataScan::getName(kind)).toBool())
        {
            kinds.push_back(kind);
        }
    }
    return kinds;
}

std::vector<PDFDocumentToolSanitize::Kind> PDFDocumentToolSanitize::getInteractiveKinds(const PDFDocumentHiddenDataScan::Result& scan, const std::vector<Kind>& removedKinds)
{
    // ISO 32000-2, 7.6.4.2, Table 22: bit 4 "Modify the contents of the document by operations
    // other than those controlled by bits 6, 9, and 11"; bit 6 "Add or modify text annotations,
    // fill in interactive form fields, and, if bit 4 is also set, create or modify interactive
    // form fields (including signature fields)". Every kind rewrites the document: bit 4 of the
    // catalog. A kind, which removes annotations or fields, needs bit 6 too: the comments (markup
    // and popup annotations), the attachments of file attachment annotations, the signature
    // fields (also /Perms and /DSS alone are bit 4 only).
    std::vector<Kind> kinds;
    for (const Kind kind : removedKinds)
    {
        const PDFDocumentHiddenDataScan::KindResult& found = scan.get(kind);
        bool isInteractive = false;
        switch (kind)
        {
            case Kind::Comments:
                isInteractive = found.isPresent;
                break;
            case Kind::Attachments:
                isInteractive = scan.attachmentAnnotations > 0 || !found.isComplete;
                break;
            case Kind::SignatureFields:
                isInteractive = found.count > 0 || !found.isComplete;
                break;
            default:
                break;
        }
        if (isInteractive)
        {
            kinds.push_back(kind);
        }
    }
    return kinds;
}

QString PDFDocumentToolSanitize::getPermissionRefusal(const PDFDocument* document, const PDFDocumentHiddenDataScan::Result& scan, const std::vector<Kind>& removedKinds)
{
    const PDFSecurityHandler* securityHandler = document ? document->getStorage().getSecurityHandler() : nullptr;
    if (!securityHandler || securityHandler->isAllowed(PDFSecurityHandler::Permission::ModifyInteractiveItems))
    {
        return QString();
    }

    const std::vector<Kind> kinds = getInteractiveKinds(scan, removedKinds);
    if (kinds.empty())
    {
        return QString();
    }
    return tr("The permissions of the document do not allow changing its annotations and form fields: untick %1 to remove the other kinds.").arg(joinKindTitles(kinds));
}

QString PDFDocumentToolSanitize::joinKindTitles(const std::vector<Kind>& kinds)
{
    QStringList titles;
    for (const Kind kind : kinds)
    {
        titles << getKindTitle(kind);
    }
    return titles.join(QStringLiteral(", "));
}

QString PDFDocumentToolSanitize::getId() const
{
    return QString::fromLatin1(TOOL_ID);
}

PDFDocumentToolParameterDefinitions PDFDocumentToolSanitize::getParameters() const
{
    using Definition = PDFDocumentToolParameterDefinition;
    using Helper = PDFDocumentToolProtectHelper;

    // The defaults are the preset Standard (438:12588); the texts are constants of the program (PDFDocumentToolText)
    const QVariantMap defaults = getPresetValues(Preset::Standard);
    auto name = [](Kind kind) { return PDFDocumentHiddenDataScan::getName(kind); };
    PDFDocumentToolParameterDefinitions definitions;
    definitions.push_back(Definition::createBool(tr, name(Kind::DocumentInfo), Helper::TITLES[0], Helper::DESCRIPTIONS[0], defaults.value(name(Kind::DocumentInfo)).toBool()));
    definitions.push_back(Definition::createBool(tr, name(Kind::Metadata), Helper::TITLES[1], Helper::DESCRIPTIONS[1], defaults.value(name(Kind::Metadata)).toBool()));
    definitions.push_back(Definition::createBool(tr, name(Kind::Comments), Helper::TITLES[2], Helper::DESCRIPTIONS[2], defaults.value(name(Kind::Comments)).toBool()));
    definitions.push_back(Definition::createBool(tr, name(Kind::Attachments), Helper::TITLES[3], Helper::DESCRIPTIONS[3], defaults.value(name(Kind::Attachments)).toBool()));
    definitions.push_back(Definition::createBool(tr, name(Kind::Outline), Helper::TITLES[4], Helper::DESCRIPTIONS[4], defaults.value(name(Kind::Outline)).toBool()));
    definitions.push_back(Definition::createBool(tr, name(Kind::PageLabels), Helper::TITLES[5], Helper::DESCRIPTIONS[5], defaults.value(name(Kind::PageLabels)).toBool()));
    definitions.push_back(Definition::createBool(tr, name(Kind::SearchIndex), Helper::TITLES[6], Helper::DESCRIPTIONS[6], defaults.value(name(Kind::SearchIndex)).toBool()));
    definitions.push_back(Definition::createBool(tr, name(Kind::Thumbnails), Helper::TITLES[7], Helper::DESCRIPTIONS[7], defaults.value(name(Kind::Thumbnails)).toBool()));
    definitions.push_back(Definition::createBool(tr, name(Kind::InvisibleText), Helper::TITLES[8], Helper::DESCRIPTIONS[8], defaults.value(name(Kind::InvisibleText)).toBool()));
    definitions.push_back(Definition::createBool(tr, name(Kind::SignatureFields), Helper::TITLES[9], Helper::DESCRIPTIONS[9], defaults.value(name(Kind::SignatureFields)).toBool()));
    return definitions;
}

PDFDocumentToolResult PDFDocumentToolSanitize::computePreview(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    Q_UNUSED(parameters);

    const PDFDocument* document = context.getDocument();
    const PDFDocumentHiddenDataScan::Result scan = PDFDocumentHiddenDataScan::scan(document, true, control, progress);
    if (scan.isCanceled || PDFOperationControl::isOperationCancelled(control))
    {
        return PDFDocumentToolResult::createStopped();
    }

    PDFDocumentToolResult result;
    result.data.insert(QStringLiteral("scan"), scan.toJson());
    result.data.insert(QStringLiteral("pageCount"), qint64(document->getCatalog()->getPageCount()));
    return result;
}

PDFDocumentToolResult PDFDocumentToolSanitize::execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    using Helper = PDFDocumentToolProtectHelper;

    const PDFDocumentToolInputDocument& input = context.documents.front();
    const PDFDocument* document = input.document.data();
    const std::vector<Kind> chosenKinds = getChosenKinds(parameters);

    // The kinds, which the document has: the steps of the result and what is kept (438:14342 says
    // a kept invisible text, so the content is looked at also when it is not chosen)
    const PDFDocumentHiddenDataScan::Result scan = PDFDocumentHiddenDataScan::scan(document, true, control, nullptr);
    if (scan.isCanceled || PDFOperationControl::isOperationCancelled(control))
    {
        return PDFDocumentToolResult::createStopped();
    }

    std::vector<Kind> removedKinds;
    std::vector<Kind> keptKinds;
    PDFDocumentSanitizer::SanitizationFlags flags = PDFDocumentSanitizer::None;
    for (Kind kind : PDFDocumentHiddenDataScan::getKinds())
    {
        const bool isChosen = std::find(chosenKinds.cbegin(), chosenKinds.cend(), kind) != chosenKinds.cend();
        if (isChosen)
        {
            // An empty information dictionary goes too (the builders would write their producer into it)
            flags.setFlag(PDFDocumentHiddenDataScan::getFlag(kind), scan.get(kind).isPresent || kind == Kind::DocumentInfo);
        }
        if (scan.get(kind).isPresent)
        {
            (isChosen ? removedKinds : keptKinds).push_back(kind);
        }
    }

    if (removedKinds.empty())
    {
        return PDFDocumentToolResult::createNothingToDo(tr("The chosen kinds of hidden data are not in this document: there is nothing to remove."));
    }

    // Before anything is changed: what the chosen kinds change must be permitted (the same
    // check and text as the panel of the tool), and the document info must be removable from
    // the XMP metadata, which stay
    const QString permissionRefusal = getPermissionRefusal(document, scan, removedKinds);
    if (!permissionRefusal.isEmpty())
    {
        return PDFDocumentToolResult::createFailed(permissionRefusal);
    }
    const bool isDocumentInfoRemoved = std::find(removedKinds.cbegin(), removedKinds.cend(), Kind::DocumentInfo) != removedKinds.cend();
    if (isDocumentInfoRemoved && !flags.testFlag(PDFDocumentSanitizer::Metadata) && !scan.isDocumentInfoXmpReadable)
    {
        return PDFDocumentToolResult::createFailed(tr("The XMP metadata of the document cannot be read, so the document info in them cannot be removed. Tick All metadata too: it removes the metadata whole."));
    }

    const qint64 sizeBefore = getWholeFileInputSize(input);
    if (sizeBefore < 0)
    {
        return PDFDocumentToolResult::createFailed(tr("The size of the document cannot be measured."));
    }

    // One bar over the steps: "Comments and markup · step 3 of 8" (438:14093)
    const int stepCount = int(removedKinds.size()) + 1;
    PDFProgressPhases phases(progress);
    std::map<int, int> phaseOfFlag;
    for (size_t i = 0; i < removedKinds.size(); ++i)
    {
        const Kind kind = removedKinds[i];
        const QString text = tr("%1 · step %2 of %3").arg(getKindTitle(kind)).arg(i + 1).arg(stepCount);
        phaseOfFlag[int(PDFDocumentHiddenDataScan::getFlag(kind))] = phases.addPhase(Helper::getWeight(kind), text, false);
    }
    const int measurePhase = phases.addPhase(10, tr("Measuring the result · step %1 of %2").arg(stepCount).arg(stepCount), false);
    phases.start();

    // The declaration of PDF/A or PDF/UA: the result is not checked against the standard.
    // Metadata, which go, take it with them; otherwise the whole write removes it as for
    // Compress and Optimize Structure (createWholeFileDocument below).
    ConformanceRemoval conformance;
    const bool isMetadataRemoved = flags.testFlag(PDFDocumentSanitizer::Metadata);
    if (isMetadataRemoved)
    {
        PDFOCRPagePreparer::hasConformanceDeclaration(document, &conformance.removed);
    }

    // A local sanitizer: it has a state, one instance of the tool serves more threads
    PDFDocumentSanitizer sanitizer(PDFDocumentSanitizer::None, nullptr);
    sanitizer.setFlags(flags);
    sanitizer.setOperationControl(control);
    sanitizer.setStorage(document->getStorage());
    sanitizer.setProgress(progress, std::move(phaseOfFlag));
    sanitizer.sanitize();
    if (sanitizer.isCanceled())
    {
        return PDFDocumentToolResult::createStopped();
    }

    // Last: the dead containers of the organization of the file go, the version stays, a
    // declaration of a standard goes with the kept metadata
    PDFDocument output = createWholeFileDocument(sanitizer.takeStorage(), document, isMetadataRemoved ? nullptr : &conformance);
    if (!conformance.kept.isEmpty())
    {
        return createConformanceFailure(conformance.kept);
    }

    phases.beginPhase(measurePhase);
    const WholeFileSize outputSize = getWholeFileOutputSize(&output);
    if (!outputSize.error.isEmpty())
    {
        return PDFDocumentToolResult::createFailed(outputSize.error);
    }
    phases.finish();

    PDFDocumentToolResult result;
    QStringList keptTitles;
    QJsonArray removedNames;
    QJsonArray keptNames;
    for (Kind kind : removedKinds)
    {
        result.steps.push_back({ getKindIconName(kind), getStepText(kind, scan.get(kind)), PDFDocumentToolStep::State::Done });
        removedNames.append(PDFDocumentHiddenDataScan::getName(kind));
    }
    for (Kind kind : keptKinds)
    {
        keptTitles << getKindTitle(kind);
        keptNames.append(PDFDocumentHiddenDataScan::getName(kind));
    }

    // 438:14342: Removed "7 kinds of hidden data", File size, Kept "Invisible text (OCR layer)"
    result.facts.push_back({ QStringLiteral("removed"), tr("Removed"), formatKindCount(int(removedKinds.size()), true) });
    result.facts.push_back({ QStringLiteral("size"), tr("File size"), PDFDocumentToolFormat::formatSizeChange(sizeBefore, outputSize.size) });
    const QString keptText = keptTitles.isEmpty() ? tr("Nothing") : (keptTitles.size() <= 2 ? keptTitles.join(QStringLiteral(", ")) : formatKindCount(int(keptTitles.size()), false));
    result.facts.push_back({ QStringLiteral("kept"), tr("Kept"), keptText });

    // The document info went also from the XMP metadata, which stay (ISO 32000-2, Table 349)
    const QStringList documentInfoXmp = sanitizer.getRemovedDocumentInfoProperties();
    if (!documentInfoXmp.isEmpty())
    {
        result.facts.push_back({ QStringLiteral("documentInfoXmp"), tr("Document info"),
                                 Helper::plural(int(documentInfoXmp.size()), QT_TR_NOOP("Removed also from the XMP metadata: %Ln property"), QT_TR_NOOP("Removed also from the XMP metadata: %Ln properties")) });
    }
    addConformanceFact(&result, conformance.removed);

    result.data.insert(QStringLiteral("removedKinds"), removedNames);
    result.data.insert(QStringLiteral("keptKinds"), keptNames);
    result.data.insert(QStringLiteral("scan"), scan.toJson());
    result.data.insert(QStringLiteral("documentInfoXmp"), QJsonArray::fromStringList(documentInfoXmp));

    // The removal is the purpose: also a larger file has its document (review RP2FIX1-CORE)
    finishWholeFileResult(&result, input, std::move(output), sizeBefore, outputSize.size, QStringLiteral("_clean"), WholeFileDocument::Always);
    return result;
}

// -------------------------------------------------------------- Remove External Links

std::vector<PDFDocumentLinkOperations::ExternalLink> PDFDocumentToolRemoveLinks::getPreviewLinks(const PDFDocumentToolResult& preview)
{
    return PDFDocumentLinkOperations::fromJson(preview.data.value(QStringLiteral("links")).toArray());
}

QString PDFDocumentToolRemoveLinks::createExcludeText(const std::set<QString>& ids)
{
    QStringList list(ids.cbegin(), ids.cend());
    return list.join(QLatin1Char(','));
}

QString PDFDocumentToolRemoveLinks::getId() const
{
    return QString::fromLatin1(TOOL_ID);
}

PDFDocumentToolParameterDefinitions PDFDocumentToolRemoveLinks::getParameters() const
{
    using Definition = PDFDocumentToolParameterDefinition;

    PDFDocumentToolParameterDefinitions definitions;
    Definition exclude = Definition::createString(tr, QString::fromLatin1(PARAMETER_EXCLUDE), QT_TR_NOOP("Links to keep"),
                                                  QT_TR_NOOP("Ids of the external links, which stay, separated by commas: the id of document.get_annotations (\"12:0\"); empty = every external link is removed"),
                                                  QString(), Definition::MAXIMUM_TEXT_LENGTH);

    // The kept links of one document mean nothing for the next one
    exclude.setStored(false);
    definitions.push_back(std::move(exclude));
    return definitions;
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolRemoveLinks::validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const
{
    std::vector<PDFDocumentToolParameterError> errors = PDFDocumentTool::validate(context, parameters);
    QString invalidItem;
    if (!PDFDocumentLinkOperations::parseIds(parameters.value(QString::fromLatin1(PARAMETER_EXCLUDE)).toString(), nullptr, &invalidItem))
    {
        errors.push_back({ QString::fromLatin1(PARAMETER_EXCLUDE), tr("“%1” is not an id of an annotation; write ids such as 12:0, 15:0.").arg(invalidItem) });
    }
    return errors;
}

PDFDocumentToolResult PDFDocumentToolRemoveLinks::computePreview(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    Q_UNUSED(parameters);
    Q_UNUSED(progress);

    const PDFDocumentLinkOperations::LinkList list = PDFDocumentLinkOperations::listExternalLinks(context.getDocument(), control);
    if (PDFOperationControl::isOperationCancelled(control))
    {
        return PDFDocumentToolResult::createStopped();
    }

    PDFDocumentToolResult result;
    result.data.insert(QStringLiteral("links"), PDFDocumentLinkOperations::toJson(list.links));
    result.data.insert(QStringLiteral("internalLinks"), qint64(list.internalLinkCount));
    result.data.insert(QStringLiteral("truncated"), list.isTruncated);
    return result;
}

PDFDocumentToolResult PDFDocumentToolRemoveLinks::execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    using Helper = PDFDocumentToolProtectHelper;
    Q_UNUSED(progress);

    const PDFDocumentToolInputDocument& input = context.documents.front();
    const PDFDocument* document = input.document.data();

    std::set<QString> excludedIds;
    PDFDocumentLinkOperations::parseIds(parameters.value(QString::fromLatin1(PARAMETER_EXCLUDE)).toString(), &excludedIds, nullptr);

    // Every link of the document, also behind the bound of the listing of the preview: none of
    // them stays, unless the caller keeps it
    const PDFDocumentLinkOperations::LinkList list = PDFDocumentLinkOperations::listExternalLinks(document, control, std::numeric_limits<size_t>::max());
    if (PDFOperationControl::isOperationCancelled(control))
    {
        return PDFDocumentToolResult::createStopped();
    }

    // A kept id, which is no external link, would be a link the caller thinks it keeps
    std::set<QString> unknownIds = excludedIds;
    for (const PDFDocumentLinkOperations::ExternalLink& link : list.links)
    {
        unknownIds.erase(link.id);
    }
    if (!unknownIds.empty())
    {
        QStringList shown;
        for (const QString& id : unknownIds)
        {
            if (shown.size() == 3)
            {
                shown << QStringLiteral("…");
                break;
            }
            shown << id;
        }
        return PDFDocumentToolResult::createFailed(tr("These ids are not external links of the document: %1. Read the links again by document.get_annotations.").arg(shown.join(QStringLiteral(", "))));
    }

    std::vector<PDFDocumentLinkOperations::ExternalLink> removedLinks;
    std::set<PDFInteger> pages;
    for (const PDFDocumentLinkOperations::ExternalLink& link : list.links)
    {
        if (!excludedIds.count(link.id))
        {
            removedLinks.push_back(link);
            pages.insert(link.pageIndex);
        }
    }

    if (list.links.empty())
    {
        return PDFDocumentToolResult::createNothingToDo(tr("This document has no external links."));
    }
    if (removedLinks.empty())
    {
        return PDFDocumentToolResult::createNothingToDo(tr("Every external link is kept: there is nothing to remove."));
    }

    PDFDocumentLinkOperations::RemovalResult removal = PDFDocumentLinkOperations::removeLinks(document, removedLinks);
    if (!removal.document)
    {
        return PDFDocumentToolResult::createFailed(tr("The links could not be removed."));
    }

    const int removedCount = removal.removed;
    const int keptCount = int(list.links.size()) - int(removedLinks.size());
    const int internalCount = Helper::toInt(list.internalLinkCount);

    PDFDocumentToolResult result;

    // 438:16073: "14 external links on 9 pages", "2 external links", "48 · not touched"
    const QString removedText = tr("%1 on %2").arg(Helper::pluralLinks(removedCount, QT_TR_NOOP("%Ln external link"), QT_TR_NOOP("%Ln external links")),
                                                    Helper::pluralLinks(int(pages.size()), QT_TR_NOOP("%Ln page"), QT_TR_NOOP("%Ln pages")));
    result.facts.push_back({ QStringLiteral("removed"), tr("Removed"), removedText });
    result.facts.push_back({ QStringLiteral("kept"), tr("Kept"), Helper::pluralLinks(keptCount, QT_TR_NOOP("%Ln external link"), QT_TR_NOOP("%Ln external links")) });
    result.facts.push_back({ QStringLiteral("internal"), tr("Internal links"), tr("%1 · not touched").arg(QLocale().toString(internalCount)) });

    result.data.insert(QStringLiteral("removed"), removedCount);
    result.data.insert(QStringLiteral("kept"), keptCount);
    result.data.insert(QStringLiteral("internalLinks"), internalCount);
    result.data.insert(QStringLiteral("pages"), int(pages.size()));
    result.data.insert(QStringLiteral("truncated"), list.isTruncated);

    PDFDocumentToolOutputDocument output;
    output.kind = PDFDocumentToolOutputDocument::Kind::Changed;
    output.inputIndex = 0;
    output.document = removal.document;
    output.flags = removal.flags;
    output.suggestedFileName = Helper::createFileName(input.fileName, QStringLiteral("_no_links"));
    result.documents.push_back(std::move(output));
    return result;
}

}   // namespace pdf
