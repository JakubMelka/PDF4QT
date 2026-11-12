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

#include "pdfappmcpmanageaction.h"
#include "pdfappassemblyloader.h"
#include "pdfappdocument.h"
#include "pdfappdocumentpermissions.h"
#include "pdfappmcparguments.h"
#include "pdfappmcpchange.h"
#include "pdfappmcpdocumentresolver.h"
#include "pdfappmcphost.h"
#include "pdfappmcppermissions.h"
#include "pdfappmcptoolhandler.h"
#include "pdfapptoolpathrules.h"
#include "pdfapptoolresultwriter.h"
#include "pdfrepositoryhelper.h"

#include "pdfdocumentassemblyfile.h"
#include "pdfdocumenttool_manage.h"
#include "pdfdocumenttool_split.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumenttoolregistry.h"

#include "pdfmcpcall.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace pdf
{

/// Helpers of the action pages.manage
class PDFAppMcpManageActionHelper
{
public:
    PDFAppMcpManageActionHelper() = delete;

    /// Most entries of a list of an error text (missing pages, paths): the rest is "…"
    static constexpr int MAXIMUM_LISTED = 50;

    static QString name(const char* parameter) { return QString::fromLatin1(parameter); }

    /// A list of paths of an error text, at most MAXIMUM_LISTED
    static QString formatPaths(const QStringList& paths)
    {
        QStringList listed = paths.mid(0, MAXIMUM_LISTED);
        for (QString& path : listed)
        {
            path = QDir::toNativeSeparators(path);
        }
        if (paths.size() > MAXIMUM_LISTED)
        {
            listed << QStringLiteral("…");
        }
        return listed.join(QStringLiteral("; "));
    }

    /// The value of "result" of the call, empty = not given (the default of the catalog row)
    static QString getResult(const PDFAppMcpToolInvocation& invocation)
    {
        return invocation.arguments.value(QLatin1StringView("result")).toString();
    }

    static bool refuse(const QString& text, PDFMcpActivityStatus refusalStatus, QString* errorText, PDFMcpActivityStatus* status)
    {
        if (errorText)
        {
            *errorText = text;
        }
        if (status)
        {
            *status = refusalStatus;
        }
        return false;
    }

    /// The open document of a path of a source of an assembly (RB8): the candidates are its
    /// absolute path and its relative path resolved against the folder of the assembly file; a
    /// candidate on another computer or a device is never touched (\p isOnAnotherComputer), a
    /// local one is compared with the files of the open documents (also by its canonical path)
    static PDFAppDocument* findOpenDocument(const PDFDocumentAssembly::Source& source, const QString& folder, const std::vector<PDFAppDocument*>& documents,
                                           QString* displayPath, bool* isOnAnotherComputer)
    {
        QStringList candidates;
        if (!source.path.isEmpty())
        {
            candidates << source.path;
        }
        if (!source.relativePath.isEmpty())
        {
            candidates << QDir(folder).filePath(QDir::fromNativeSeparators(source.relativePath));
        }

        *isOnAnotherComputer = false;
        *displayPath = candidates.isEmpty() ? source.name : candidates.front();
        for (const QString& candidate : std::as_const(candidates))
        {
            if (candidate.size() > PDFRepositoryHelper::MAXIMUM_PATH_LENGTH || candidate.contains(QChar(0)))
            {
                continue;
            }
            const QString cleanPath = QDir::cleanPath(QDir::fromNativeSeparators(candidate));
            if (!QDir::isAbsolutePath(cleanPath))
            {
                continue;
            }
            if (PDFAppToolPathRules::isDeviceOrNetworkPath(candidate) || PDFAppToolPathRules::isDeviceOrNetworkPath(cleanPath))
            {
                *isOnAnotherComputer = true;
                *displayPath = cleanPath;
                continue;
            }

            const QString canonicalPath = QFileInfo(cleanPath).canonicalFilePath();
            for (PDFAppDocument* document : documents)
            {
                const QString documentPath = document ? document->getFilePath() : QString();
                if (documentPath.isEmpty())
                {
                    continue;
                }
                if (PDFRepositoryHelper::isSamePath(QDir::cleanPath(QDir::fromNativeSeparators(documentPath)), cleanPath) ||
                    (!canonicalPath.isEmpty() && PDFRepositoryHelper::isSamePath(QFileInfo(documentPath).canonicalFilePath(), canonicalPath)))
                {
                    return document;
                }
            }
        }
        return nullptr;
    }

    /// The assembly mode (Q9 b): see PDFAppMcpManageAction
    static bool prepareAssembly(PDFAppMcpHost* host, const PDFAppMcpToolInvocation& invocation, QVariantMap* parameters,
                                std::vector<QPointer<PDFAppDocument>>* inputDocuments, QString* errorText, PDFMcpActivityStatus* status)
    {
        const QString toolId = QString::fromLatin1(PDFDocumentToolManage::TOOL_ID);
        if (!invocation.documentId.isEmpty())
        {
            return refuse(QStringLiteral("documentId: with assemblyPath the documents are the PDF sources of the assembly; give documentId only with pages."), PDFMcpActivityStatus::Failed, errorText, status);
        }

        // The output makes new documents; its result is given explicitly (the default of the
        // catalog row, keep, is the result of This document)
        const QString output = PDFDocumentToolManage::getOutput(*parameters);
        if (output != QLatin1String(PDFDocumentToolManage::OUTPUT_ONE_PDF) && output != QLatin1String(PDFDocumentToolManage::OUTPUT_PER_GROUP) &&
            output != QLatin1String(PDFDocumentToolManage::OUTPUT_PER_PAGE))
        {
            return refuse(QStringLiteral("output: an assembly makes new documents - give one-pdf, per-group or per-page (output document is the mode documentId with pages)."), PDFMcpActivityStatus::Failed, errorText, status);
        }
        const QString result = getResult(invocation);
        if (result != QLatin1String("open_new_tab") && result != QLatin1String("save_file") && result != QLatin1String("save_files"))
        {
            return refuse(QStringLiteral("result: an assembly makes new documents - give the result open_new_tab or save_file with a path (output one-pdf), or save_files (output per-group or per-page)."),
                          PDFMcpActivityStatus::Failed, errorText, status);
        }

        // The file is read with the permission Open and save files (Deny refuses it here; the
        // result asks by its own permission, as the images of imagePaths)
        const QString assistantKey = invocation.call ? invocation.call->getRequest().client.assistantKey : QString();
        if (host->getPolicy().evaluate(PDFAppMcpPermissionKind::Files, assistantKey, true) == PDFAppMcpPolicyResult::Denied)
        {
            return refuse(QStringLiteral("The user does not allow assistants to open files (Open and save files is set to Deny in PDF4QT), so the assembly file cannot be read."),
                          PDFMcpActivityStatus::DeniedPermission, errorText, status);
        }

        // The rules of the paths of an assistant (absolute, local, no device), an existing
        // assembly file under the limits of the file (RB8)
        QString pathError;
        const QString assemblyPath = PDFAppMcpArguments::checkPath(parameters->value(name(PDFDocumentToolManage::PARAMETER_ASSEMBLY_PATH)).toString(), false, host->getContext(), &pathError);
        if (assemblyPath.isEmpty())
        {
            return refuse(QStringLiteral("assemblyPath: %1").arg(pathError.isEmpty() ? QStringLiteral("an absolute path of a .pdf4qtassembly file is expected") : pathError), PDFMcpActivityStatus::Failed, errorText, status);
        }
        if (!PDFAppAssemblyLoader::isAssemblyFile(assemblyPath))
        {
            return refuse(QStringLiteral("assemblyPath: the file must have the extension .pdf4qtassembly (an assembly saved by Manage Pages)."), PDFMcpActivityStatus::Failed, errorText, status);
        }

        const QFileInfo info(assemblyPath);
        const PDFDocumentAssemblyFile::Limits limits;
        if (!info.isFile())
        {
            return refuse(QStringLiteral("assemblyPath: the file does not exist."), PDFMcpActivityStatus::Failed, errorText, status);
        }
        if (info.size() > limits.maximumFileSize)
        {
            return refuse(QStringLiteral("assemblyPath: the file is larger than %1 MB.").arg(limits.maximumFileSize / (1024 * 1024)), PDFMcpActivityStatus::Failed, errorText, status);
        }

        QFile file(assemblyPath);
        if (!file.open(QIODevice::ReadOnly))
        {
            return refuse(QStringLiteral("assemblyPath: the file cannot be read."), PDFMcpActivityStatus::Failed, errorText, status);
        }
        const QByteArray data = file.read(limits.maximumFileSize + 1);
        file.close();
        if (data.size() > limits.maximumFileSize)
        {
            return refuse(QStringLiteral("assemblyPath: the file is larger than %1 MB.").arg(limits.maximumFileSize / (1024 * 1024)), PDFMcpActivityStatus::Failed, errorText, status);
        }

        PDFDocumentAssemblyFile::ReadResult read = PDFDocumentAssemblyFile::read(data, limits);
        if (!read.content)
        {
            return refuse(QStringLiteral("assemblyPath: %1").arg(read.errorMessage.isEmpty() ? QStringLiteral("This is not an assembly file.") : read.errorMessage), PDFMcpActivityStatus::Failed, errorText, status);
        }

        // A problem of the file would move pages to the bin silently (the dialog of the GUI shows
        // it): an assistant gets it as an error, nothing runs
        if (!read.problems.empty())
        {
            QStringList problems;
            for (const PDFDocumentAssemblyFile::Problem& problem : read.problems)
            {
                if (problems.size() >= MAXIMUM_LISTED)
                {
                    problems << QStringLiteral("…");
                    break;
                }
                problems << problem.text;
            }
            return refuse(QStringLiteral("assemblyPath: the assembly file has problems, which would leave pages out: %1 Open it in Manage Pages of PDF4QT to resolve them.").arg(problems.join(QLatin1Char(' '))),
                          PDFMcpActivityStatus::Failed, errorText, status);
        }

        const PDFDocumentAssembly assembly = read.content->assembly;
        if (assembly.isEmpty())
        {
            return refuse(QStringLiteral("assemblyPath: the assembly has no pages."), PDFMcpActivityStatus::Failed, errorText, status);
        }

        // Each PDF source is an open document (Q9 b: no PDF file is read from the disk by an
        // assistant, Q11 of step 2); an image is in the file. The documents go to the context in
        // the order of the PDF sources of the assembly.
        const std::shared_ptr<const PDFDocumentTool> tool = PDFDocumentToolRegistry::getTool(toolId);
        const PDFDocumentToolOutputTraits outputTraits = tool ? tool->getOutputTraits(*parameters) : PDFDocumentToolOutputTraits(PDFDocumentToolCatalog::findTool(toolId));
        const std::vector<PDFAppDocument*> openDocuments = host->getResolver().getDocuments();
        const std::vector<PDFDocumentAssembly::Source>& sources = assembly.getSources();
        const QString folder = info.absolutePath();
        QStringList missingPaths;
        QStringList remotePaths;
        std::vector<QPointer<PDFAppDocument>> documents;
        std::vector<PDFInteger> documentPageCounts;
        std::vector<int> documentOfSource(sources.size(), -1);
        for (size_t i = 0; i < sources.size(); ++i)
        {
            const PDFDocumentAssembly::Source& source = sources[i];
            if (source.kind == PDFDocumentAssembly::SourceKind::Image)
            {
                if (source.imageData.isEmpty())
                {
                    return refuse(QStringLiteral("assemblyPath: the image source %1 has no data in the file.").arg(i + 1), PDFMcpActivityStatus::Failed, errorText, status);
                }
                continue;
            }

            QString displayPath;
            bool isOnAnotherComputer = false;
            PDFAppDocument* document = findOpenDocument(source, folder, openDocuments, &displayPath, &isOnAnotherComputer);
            if (!document)
            {
                (isOnAnotherComputer ? remotePaths : missingPaths) << displayPath;
                continue;
            }
            if (!document->isReady() || !document->getDocument())
            {
                return refuse(QStringLiteral("assemblyPath: the document of the source %1 is not read yet; try again when it is open.").arg(QDir::toNativeSeparators(displayPath)), PDFMcpActivityStatus::Failed, errorText, status);
            }

            switch (PDFAppToolResultWriter::getDocumentRefusal(outputTraits, document))
            {
                case PDFAppToolResultWriter::DocumentRefusal::Locked:
                    return refuse(PDFAppMcpChangeJob::getLockedText(), PDFMcpActivityStatus::DeniedLocked, errorText, status);
                case PDFAppToolResultWriter::DocumentRefusal::DisplayVersion:
                    return refuse(PDFAppMcpChangeJob::getPagedXFAText(), PDFMcpActivityStatus::Failed, errorText, status);
                case PDFAppToolResultWriter::DocumentRefusal::None:
                    break;
            }

            documentOfSource[i] = int(documents.size());
            documents.emplace_back(document);
            documentPageCounts.push_back(document->getPageCount());
        }

        if (!remotePaths.isEmpty())
        {
            // RB8: a file of another computer is never contacted for an assistant
            return refuse(QStringLiteral("assemblyPath: these sources are files of another computer or a device, which PDF4QT never opens for an assistant: %1").arg(formatPaths(remotePaths)),
                          PDFMcpActivityStatus::Failed, errorText, status);
        }
        if (!missingPaths.isEmpty())
        {
            return refuse(QStringLiteral("assemblyPath: these PDF sources of the assembly are not open in PDF4QT - open them with files.open first: %1").arg(formatPaths(missingPaths)),
                          PDFMcpActivityStatus::Failed, errorText, status);
        }

        // The documents of the run are the PDF sources; without one the bridge would take the
        // document of the active tab as the first input (its resolution of documentId)
        if (documents.empty())
        {
            return refuse(QStringLiteral("assemblyPath: the assembly has no PDF source; images alone become a PDF by convert.images-to-pdf."), PDFMcpActivityStatus::Failed, errorText, status);
        }

        // The pages of the items must exist in the open documents (they may differ from the
        // files, when the assembly was saved)
        for (const PDFDocumentAssembly::Item& item : assembly.getItems())
        {
            for (const PDFDocumentAssembly::Page& page : item.pages)
            {
                const int input = page.plan.input;
                if (input < 0 || size_t(input) >= sources.size() || documentOfSource[input] < 0)
                {
                    continue;
                }
                const PDFInteger pageCount = documentPageCounts[documentOfSource[input]];
                if (page.plan.pageIndex < 0 || page.plan.pageIndex >= pageCount)
                {
                    return refuse(QStringLiteral("assemblyPath: the assembly uses page %1 of %2, but the open document has %3 pages.")
                                  .arg(page.plan.pageIndex + 1).arg(QDir::toNativeSeparators(sources[input].path)).arg(pageCount), PDFMcpActivityStatus::Failed, errorText, status);
                }
            }
        }

        // The parameters of the run: the assembly read here (assemblyPath stays: the tool names
        // it in its texts); the output values of the file fill those the assistant has not given
        // (never the folder or the existing files: the bridge has checked those of the call)
        parameters->insert(name(PDFDocumentToolManage::PARAMETER_ASSEMBLY), QVariant::fromValue(assembly));
        for (const char* key : { PDFDocumentToolManage::PARAMETER_OUTLINE, PDFDocumentToolManage::PARAMETER_OPTIMIZE_IMAGES, PDFDocumentToolFileOutput::PARAMETER_FILE_NAME })
        {
            const QString keyName = name(key);
            if (!parameters->contains(keyName) && read.content->outputParameters.contains(keyName))
            {
                parameters->insert(keyName, read.content->outputParameters.value(keyName));
            }
        }

        // The permissions of every source by the output of these parameters (a new document of
        // the inputs keeps none of their restrictions, so it needs every permission - the rule
        // of the core); the bridge checks the catalog row (This document) after this
        for (size_t i = 0; i < documents.size(); ++i)
        {
            const PDFDocument* pdfDocument = documents[i] ? documents[i]->getDocument().data() : nullptr;
            const PDFAppPermissionRequest request = PDFAppPermissionRequest::createTool(toolId, { }, *parameters);
            if (pdfDocument && !PDFAppDocumentPermissions::isAllowed(pdfDocument, request))
            {
                return refuse(QStringLiteral("assemblyPath: PDF source %1: %2").arg(i + 1).arg(PDFAppDocumentPermissions::getMcpRefusalText(pdfDocument, request)), PDFMcpActivityStatus::Failed, errorText, status);
            }
        }

        *inputDocuments = std::move(documents);
        return true;
    }

    /// The document mode (QB4 with RB9): see PDFAppMcpManageAction
    static bool preparePages(PDFAppMcpHost* host, const PDFAppMcpToolInvocation& invocation, QVariantMap* parameters, QString* errorText, PDFMcpActivityStatus* status)
    {
        const QString output = PDFDocumentToolManage::getOutput(*parameters);
        if (output != QLatin1String(PDFDocumentToolManage::OUTPUT_DOCUMENT))
        {
            return refuse(QStringLiteral("output: pages rearranges the document of documentId (output document); new documents are made of an assembly file (assemblyPath)."), PDFMcpActivityStatus::Failed, errorText, status);
        }
        const QString result = getResult(invocation);
        if (!result.isEmpty() && result != QLatin1String("keep") && result != QLatin1String("save_copy") && result != QLatin1String("overwrite"))
        {
            return refuse(QStringLiteral("result: rearranged pages change the document - give keep, save_copy with a path, or overwrite."), PDFMcpActivityStatus::Failed, errorText, status);
        }

        const PDFAppMcpResolvedDocument resolved = host->getResolver().resolve(invocation.documentId, true);
        if (!resolved.isResolved())
        {
            return refuse(resolved.errorText, PDFMcpActivityStatus::Failed, errorText, status);
        }
        PDFAppDocument* document = resolved.document;

        // This document is not available for a locked document (plan 4.5: "Document is locked -
        // unlock it or create a new PDF"), whatever the result
        if (document->isLocked())
        {
            return refuse(PDFAppMcpChangeJob::getLockedText(), PDFMcpActivityStatus::DeniedLocked, errorText, status);
        }

        // RB9: every page named at least once, else the missing pages - checked now, before a
        // question of the user (the one rule of the core); the tool makes the assembly of "pages"
        // itself over the version it runs on (completeGivenParameters), so a run again after a
        // change of the document uses the pages of that version
        const PDFDocumentPointer pdfDocument = document->getDocument();
        QString pagesError;
        if (!pdfDocument || !PDFDocumentToolManage::createAssemblyOfPages(pdfDocument.data(), QString(), parameters->value(name(PDFDocumentToolManage::PARAMETER_PAGES)).toString(), &pagesError))
        {
            return refuse(QStringLiteral("pages: %1").arg(pagesError.isEmpty() ? QStringLiteral("the document has no pages.") : pagesError), PDFMcpActivityStatus::Failed, errorText, status);
        }

        // The document is resolved again by the common flow (its busy state, the permissions of
        // This document, the copy-only rule of a certified document)
        return true;
    }
};

bool PDFAppMcpManageAction::isManageTool(const QString& toolId)
{
    return toolId == QLatin1String(PDFDocumentToolManage::TOOL_ID);
}

bool PDFAppMcpManageAction::prepare(PDFAppMcpHost* host, const PDFAppMcpToolInvocation& invocation, QVariantMap* parameters,
                                    std::vector<QPointer<PDFAppDocument>>* inputDocuments, QString* errorText, PDFMcpActivityStatus* status)
{
    using Helper = PDFAppMcpManageActionHelper;

    if (!host || !parameters || !inputDocuments)
    {
        return Helper::refuse(QStringLiteral("PDF4QT is closing."), PDFMcpActivityStatus::Failed, errorText, status);
    }

    // The panel parameters never come from an assistant (the schema has none of them)
    if (parameters->contains(Helper::name(PDFDocumentToolManage::PARAMETER_ASSEMBLY)) || parameters->contains(Helper::name(PDFDocumentToolManage::PARAMETER_INPUTS)))
    {
        return Helper::refuse(QStringLiteral("assembly and inputs belong to the panels of PDF4QT; give assemblyPath, or documentId with pages."), PDFMcpActivityStatus::Failed, errorText, status);
    }
    if (invocation.arguments.contains(QLatin1StringView("documentIds")))
    {
        return Helper::refuse(QStringLiteral("documentIds: Manage Pages takes the documents of an assembly file by assemblyPath, or one document by documentId with pages."), PDFMcpActivityStatus::Failed, errorText, status);
    }

    const bool hasAssemblyPath = parameters->contains(Helper::name(PDFDocumentToolManage::PARAMETER_ASSEMBLY_PATH));
    const bool hasPages = parameters->contains(Helper::name(PDFDocumentToolManage::PARAMETER_PAGES));
    if (hasAssemblyPath == hasPages)
    {
        return Helper::refuse(QStringLiteral("Give either assemblyPath (new documents of an assembly file of Manage Pages, whose PDF sources are open) or pages with documentId (the new order of the pages of one document)."),
                              PDFMcpActivityStatus::Failed, errorText, status);
    }

    return hasAssemblyPath ? Helper::prepareAssembly(host, invocation, parameters, inputDocuments, errorText, status)
                           : Helper::preparePages(host, invocation, parameters, errorText, status);
}

}   // namespace pdf
