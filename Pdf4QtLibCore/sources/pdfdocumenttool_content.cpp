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

// Edit Content, stage E0, package P05 (document writer, tool, JSON): the tool edit.content of the
// core (GUI_overhaul/edit_tool/zadani_jadro.md 4.6, zadani_overeni.md 7.1). The tool is not
// registered in stage E0: only a direct call runs it.
//
// A run of the mode apply works with a snapshot: the one of the panels (the parameter session),
// or the one of a session of its own, which lives only inside of the run, loads the pages named
// by the revisions of the script in this thread and replays the operations. The snapshot is
// written by PDFContentDocumentWriter - the tool never reads a session, when it writes.

#include "pdfdocumenttool_content.h"

#include "pdfcatalog.h"
#include "pdfcontentcommands.h"
#include "pdfcontentdocumentwriter.h"
#include "pdfcontenteditsession.h"
#include "pdfcontentjson.h"
#include "pdfdocmdppermissions.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentpageoperations.h"
#include "pdfexception.h"
#include "pdfform.h"
#include "pdfoperationcontrol.h"
#include "pdfpage.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>

#include <set>
#include <utility>

namespace pdf
{

/// Helpers of Edit Content.
///
/// Threads and ownership: static functions only; a session of a run is a local object of the
/// function, which created it, and of its thread.
class PDFDocumentToolContentHelper
{
public:
    PDFDocumentToolContentHelper() = delete;

    using Tool = PDFDocumentToolContent;

    /// Most containers of forms loaded for one page of a run (the forms of a page and the forms
    /// they use, each once): the forms above it are not loaded - they are listed without their
    /// objects and an operation cannot name an object inside of them
    static constexpr int MAXIMUM_FORM_COUNT = 1000;

    static bool isList(const QVariantMap& parameters)
    {
        return parameters.value(QString::fromLatin1(Tool::PARAMETER_MODE)).toString() == QLatin1String(Tool::MODE_LIST);
    }

    /// The snapshot of the parameter session, nullptr = none
    static PDFContentEditSnapshotPointer getSnapshot(const QVariantMap& parameters)
    {
        const QVariant value = parameters.value(QString::fromLatin1(Tool::PARAMETER_SESSION));
        return value.canConvert<PDFContentEditSnapshotPointer>() ? value.value<PDFContentEditSnapshotPointer>() : PDFContentEditSnapshotPointer();
    }

    static QString getOperations(const QVariantMap& parameters)
    {
        return parameters.value(QString::fromLatin1(Tool::PARAMETER_OPERATIONS)).toString();
    }

    /// The mode apply has exactly one of the snapshot and the operations (KE.8); the reason otherwise
    static QString checkSource(bool hasSnapshot, bool hasOperations)
    {
        if (hasSnapshot && hasOperations)
        {
            return Tool::tr("Give either the operations or the changes of the editor, not both.");
        }
        if (!hasSnapshot && !hasOperations)
        {
            return Tool::tr("Give the operations.");
        }
        return QString();
    }

    /// The pages of the mode list: at most PDFContentJson::MAXIMUM_LIST_PAGE_COUNT of them
    static std::optional<std::vector<PDFInteger>> getListPages(const PDFDocument* document, const QVariantMap& parameters, QString* errorText)
    {
        const QVariant value = parameters.value(QString::fromLatin1(Tool::PARAMETER_PAGES));
        const std::optional<PDFDocumentToolPageSelection> selection = PDFDocumentToolPageSelection::fromVariant(value.isValid() ? value : PDFDocumentToolPageSelection().toVariant(), errorText);
        if (!selection)
        {
            return std::nullopt;
        }

        std::vector<PDFInteger> pages = selection->getPageIndices(document, errorText);
        if (pages.empty())
        {
            return std::nullopt;
        }

        if (pages.size() > size_t(PDFContentJson::MAXIMUM_LIST_PAGE_COUNT))
        {
            *errorText = Tool::tr("One call lists at most %1 pages; give the pages to list.").arg(PDFContentJson::MAXIMUM_LIST_PAGE_COUNT);
            return std::nullopt;
        }

        return pages;
    }

    /// The pages left out of the writing (the parameter skipPages); no value with the reason
    static std::optional<std::set<PDFInteger>> getSkippedPages(const PDFDocument* document, const QVariantMap& parameters, QString* errorText)
    {
        const QVariant value = parameters.value(QString::fromLatin1(Tool::PARAMETER_SKIP_PAGES));
        if (!value.isValid())
        {
            return std::set<PDFInteger>();
        }

        const std::optional<PDFDocumentToolPageSelection> selection = PDFDocumentToolPageSelection::fromVariant(value, errorText);
        if (!selection)
        {
            return std::nullopt;
        }

        if (selection->isNone)
        {
            return std::set<PDFInteger>();
        }

        const std::vector<PDFInteger> pages = selection->getPageIndices(document, errorText);
        if (pages.empty())
        {
            return std::nullopt;
        }

        return std::set<PDFInteger>(pages.cbegin(), pages.cend());
    }

    /// Loads the container of a page into a session and the containers of the forms it uses (each
    /// once, also a form using itself; not deeper than the session nests them, at most
    /// MAXIMUM_FORM_COUNT of them). A form, which cannot be loaded, is left out.
    /// \returns false with \p errorText, when the page cannot be loaded or the run was canceled
    static bool loadPage(PDFContentEditSession* session, PDFInteger pageIndex, const PDFOperationControl* control, QString* errorText)
    {
        const PDFContentContainerKey pageKey = session->getPageKey(pageIndex);
        if (!pageKey.isValid())
        {
            *errorText = Tool::tr("The page %1 does not exist.").arg(pageIndex + 1);
            return false;
        }

        if (!session->loadContainer(pageKey, control, errorText))
        {
            return false;
        }

        // One visited set for the whole walk over the forms; the queue holds a key once
        std::set<PDFContentContainerKey> visited;
        std::vector<std::pair<PDFContentContainerKey, int>> queue;
        queue.emplace_back(pageKey, 0);
        visited.insert(pageKey);

        std::vector<const PDFContentNode*> stack;
        for (size_t i = 0; i < queue.size(); ++i)
        {
            if (PDFOperationControl::isOperationCanceled(control))
            {
                errorText->clear();
                return false;
            }

            // Copies: the queue grows
            const PDFContentContainerKey key = queue[i].first;
            const int nesting = queue[i].second;

            const PDFContentContainerPointer container = session->getContainer(key);
            if (!container || !container->getRoot() || nesting >= PDFContentEditSession::MAXIMUM_CONTAINER_NESTING)
            {
                continue;
            }

            // The tree is walked with a stack of its own (its depth is given by the document)
            stack.clear();
            stack.push_back(container->getRoot().get());
            while (!stack.empty())
            {
                const PDFContentNode* node = stack.back();
                stack.pop_back();

                if (node->getKind() == PDFContentNodeKind::FormUse && node->getData().xobject.isValid())
                {
                    const PDFContentContainerKey formKey = PDFContentContainerKey::createForm(node->getData().xobject);
                    if (int(visited.size()) <= MAXIMUM_FORM_COUNT && visited.insert(formKey).second)
                    {
                        QString formError;
                        if (session->loadContainer(formKey, control, &formError))
                        {
                            queue.emplace_back(formKey, nesting + 1);
                        }
                    }
                }

                for (const PDFContentNodePointer& child : node->getChildren())
                {
                    if (child)
                    {
                        stack.push_back(child.get());
                    }
                }
            }
        }

        if (PDFOperationControl::isOperationCanceled(control))
        {
            errorText->clear();
            return false;
        }

        return true;
    }

    /// Number of the objects of a listing, which were listed whole
    static int countListedObjects(const QJsonArray& objects)
    {
        int count = 0;
        std::vector<QJsonArray> stack;
        stack.push_back(objects);
        while (!stack.empty())
        {
            const QJsonArray current = std::move(stack.back());
            stack.pop_back();

            for (const QJsonValueConstRef value : current)
            {
                const QJsonObject object = value.toObject();
                if (!object.value(QStringLiteral("partial")).toBool())
                {
                    ++count;
                }

                const QJsonValue children = object.value(QStringLiteral("children"));
                if (children.isArray())
                {
                    stack.push_back(children.toArray());
                }
            }
        }
        return count;
    }

    /// The node of a container of a page, which draws a form; not valid, when there is none
    static PDFContentNodeId findFormUse(const PDFContentContainer& container, PDFObjectReference form)
    {
        std::vector<const PDFContentNode*> stack;
        if (container.getRoot())
        {
            stack.push_back(container.getRoot().get());
        }

        while (!stack.empty())
        {
            const PDFContentNode* node = stack.back();
            stack.pop_back();

            if (node->getKind() == PDFContentNodeKind::FormUse && node->getData().xobject == form)
            {
                return node->getId();
            }

            for (const PDFContentNodePointer& child : node->getChildren())
            {
                if (child)
                {
                    stack.push_back(child.get());
                }
            }
        }

        return PDFContentNodeId();
    }

    /// The errors of the pages of a failed write as the data of the result:
    /// [ { page, errors: [ { id, text } ] } ]; the page 0 holds the errors, which belong to no page
    static QJsonArray createPageErrors(const PDFContentWriteResult& writeResult, const PDFContentEditSnapshot& snapshot)
    {
        QJsonArray pages;
        for (const auto& pageErrors : writeResult.errors)
        {
            const PDFInteger pageIndex = pageErrors.first;

            QJsonArray errors;
            for (const PDFContentWriteError& error : pageErrors.second)
            {
                QJsonObject errorObject;
                if (error.nodeId.isValid() && pageIndex >= 0)
                {
                    if (error.container.kind == PDFContentContainerKey::Kind::Page)
                    {
                        errorObject.insert(QStringLiteral("id"), PDFContentJson::writeNodeId(pageIndex, { error.nodeId }));
                    }
                    else
                    {
                        // A node of a form: named through the node of the page, which draws the form
                        for (const auto& item : snapshot.containers)
                        {
                            if (item.first.kind == PDFContentContainerKey::Kind::Page && item.first.pageIndex == pageIndex && item.second)
                            {
                                const PDFContentNodeId use = findFormUse(*item.second, error.container.reference);
                                if (use.isValid())
                                {
                                    errorObject.insert(QStringLiteral("id"), PDFContentJson::writeNodeId(pageIndex, { use, error.nodeId }));
                                }
                            }
                        }
                    }
                }

                if (error.container.kind != PDFContentContainerKey::Kind::Page && error.container.reference.isValid())
                {
                    errorObject.insert(QStringLiteral("form"), qint64(error.container.reference.objectNumber));
                }
                errorObject.insert(QStringLiteral("text"), error.text);
                errors.append(errorObject);
            }

            QJsonObject page;
            page.insert(QStringLiteral("page"), qint64(pageIndex + 1));
            page.insert(QStringLiteral("errors"), errors);
            pages.append(page);
        }
        return pages;
    }

    /// The node is content of the structure or an artifact by itself (a marked content sequence
    /// with an identifier or of an artifact, ISO 32000-2, 14.7.5 and 14.8.2.2.2)
    static bool isTagged(const PDFContentNode& node)
    {
        return node.getKind() == PDFContentNodeKind::MarkedContent && (node.getData().mcid.has_value() || node.getData().isArtifact);
    }

    /// Number of the objects of a container, which are neither inside of a marked content sequence
    /// of the structure or of an artifact, nor hold one (a text object with its sequences inside)
    static PDFInteger countNotTagged(const PDFContentContainer& container)
    {
        PDFInteger count = 0;
        std::vector<const PDFContentNode*> stack;
        std::vector<const PDFContentNode*> textStack;
        if (container.getRoot())
        {
            stack.push_back(container.getRoot().get());
        }

        while (!stack.empty())
        {
            const PDFContentNode* node = stack.back();
            stack.pop_back();

            if (isTagged(*node))
            {
                // Everything inside is tagged
                continue;
            }

            switch (node->getKind())
            {
                case PDFContentNodeKind::Path:
                case PDFContentNodeKind::Image:
                case PDFContentNodeKind::FormUse:
                case PDFContentNodeKind::Shading:
                    ++count;
                    continue;

                case PDFContentNodeKind::TextObject:
                {
                    bool hasTag = false;
                    textStack.clear();
                    textStack.push_back(node);
                    while (!textStack.empty() && !hasTag)
                    {
                        const PDFContentNode* part = textStack.back();
                        textStack.pop_back();
                        hasTag = isTagged(*part);
                        for (const PDFContentNodePointer& child : part->getChildren())
                        {
                            if (child)
                            {
                                textStack.push_back(child.get());
                            }
                        }
                    }

                    if (!hasTag)
                    {
                        ++count;
                    }
                    continue;
                }

                default:
                    break;
            }

            for (const PDFContentNodePointer& child : node->getChildren())
            {
                if (child)
                {
                    stack.push_back(child.get());
                }
            }
        }

        return count;
    }

    /// The document has a structure tree (it is a tagged document)
    static bool isTaggedDocument(const PDFDocument* document)
    {
        const PDFDictionary* trailer = document->getTrailerDictionary();
        const PDFDictionary* catalog = trailer ? document->getDictionaryFromObject(trailer->get("Root")) : nullptr;
        return catalog && document->getDictionaryFromObject(catalog->get("StructTreeRoot")) != nullptr;
    }

    /// The result of a run, which a script has stopped: the errors with their paths, one a line
    static PDFDocumentToolResult createScriptFailure(const std::vector<QString>& errors, const QString& fallback)
    {
        QStringList lines;
        for (const QString& error : errors)
        {
            lines << error;
        }
        return PDFDocumentToolResult::createFailed(lines.isEmpty() ? fallback : lines.join(QLatin1Char('\n')));
    }

    /// Number of the characters of the texts of the document in the objects of a listing
    static qint64 countListedText(const QJsonArray& objects)
    {
        qint64 count = 0;
        std::vector<QJsonArray> stack;
        stack.push_back(objects);
        while (!stack.empty())
        {
            const QJsonArray current = std::move(stack.back());
            stack.pop_back();

            for (const QJsonValueConstRef value : current)
            {
                const QJsonObject object = value.toObject();
                count += qint64(object.value(QStringLiteral("text")).toObject().value(QStringLiteral("value")).toString().size());
                count += qint64(object.value(QStringLiteral("tag")).toObject().value(QStringLiteral("actualText")).toString().size());

                const QJsonValue children = object.value(QStringLiteral("children"));
                if (children.isArray())
                {
                    stack.push_back(children.toArray());
                }
            }
        }
        return count;
    }

    /// The changed forms and patterns, which were left as they are because of a skipped page, as
    /// the data of the result: [ { object, kind, skippedPages } ]
    static QJsonArray createFormsNotWritten(const PDFContentWriteResult& writeResult)
    {
        QJsonArray forms;
        for (const auto& item : writeResult.skippedContainers)
        {
            QJsonArray pages;
            for (const PDFInteger pageIndex : item.second)
            {
                pages.append(qint64(pageIndex + 1));
            }

            QJsonObject form;
            form.insert(QStringLiteral("object"), qint64(item.first.reference.objectNumber));
            form.insert(QStringLiteral("kind"), item.first.kind == PDFContentContainerKey::Kind::PatternCell ? QStringLiteral("pattern") : QStringLiteral("form"));
            form.insert(QStringLiteral("skippedPages"), pages);
            forms.append(form);
        }
        return forms;
    }

    /// The result of a run of the mode apply, which has nothing to write. It still says, what
    /// was left as it is: the operations of the script, which changed nothing, and the forms a
    /// skipped page keeps from being written.
    /// \param operationsWithoutEffect The operations of the script, which changed nothing
    /// \param writeResult The result of the write, nullptr when nothing was given to the writer
    static PDFDocumentToolResult createNothingToDo(const QJsonArray& operationsWithoutEffect, const PDFContentWriteResult* writeResult)
    {
        PDFDocumentToolResult result = PDFDocumentToolResult::createNothingToDo(Tool::tr("There is no change of the content to apply."));
        if (!operationsWithoutEffect.isEmpty())
        {
            result.data.insert(QString::fromLatin1(Tool::DATA_OPERATIONS_WITHOUT_EFFECT), operationsWithoutEffect);
        }
        if (writeResult && !writeResult->skippedContainers.empty())
        {
            result.data.insert(QString::fromLatin1(Tool::DATA_FORMS_NOT_WRITTEN), createFormsNotWritten(*writeResult));
        }
        return result;
    }

    /// The result of a page of a script, which loadPage has not loaded: Stopped, when the run was
    /// canceled, otherwise Failed with the reason under the path of the page in the script
    static PDFDocumentToolResult createLoadFailure(const PDFOperationControl* control, const QString& path, const QString& errorText)
    {
        if (PDFOperationControl::isOperationCanceled(control))
        {
            return PDFDocumentToolResult::createStopped();
        }
        return PDFDocumentToolResult::createFailed(QStringLiteral("%1: %2").arg(path, errorText));
    }

    /// Replays a script in a session of its own (a local object of this function and of this
    /// thread; only its synchronous functions are used) and gives the snapshot of its work.
    /// \param withoutEffect Gets the operations, which changed nothing: [ { index, type } ]
    /// \returns the result of a run, which cannot go on (Failed, Stopped); no value on success
    static std::optional<PDFDocumentToolResult> replayScript(const PDFDocumentToolContext& context,
                                                              const QString& operations,
                                                              const PDFOperationControl* control,
                                                              PDFContentEditSnapshotPointer* snapshot,
                                                              QJsonArray* withoutEffect)
    {
        const PDFDocumentToolInputDocument& input = context.documents.front();
        const PDFInteger pageCount = PDFInteger(input.document->getCatalog()->getPageCount());

        std::vector<QString> errors;
        const std::optional<PDFContentScript> script = PDFContentJson::readScript(operations, &errors);
        if (!script)
        {
            return createScriptFailure(errors, Tool::tr("The operations cannot be read."));
        }

        PDFContentEditSession session(input.document, context.cms, nullptr);

        // The pages the script names by their revisions are the pages it can touch: each one is
        // loaded with its forms, as the mode list loads it, and its revision is compared (RE-15).
        // The revision covers the page and every form an identifier of the page can name: a form
        // rewritten since the listing makes the identifiers of its nodes name other nodes, though
        // nothing of the page itself has changed. An identifier of a node of any other page finds
        // no container and the operation is refused.
        //
        // The revision is computed over the containers of a session, which holds the page alone,
        // exactly as the mode list computes it (a session of one page): which forms the walk of
        // loadPage loads depends on what the session holds already. The limits, which cut that
        // walk, are MAXIMUM_FORM_COUNT forms of the page (the first ones in the order of the walk)
        // and the nesting of the session (a session remembers the first content, which names a
        // form, and its nesting decides, whether the form can be loaded). A page loaded after
        // another one would see the forms the other page has loaded beyond these limits, or miss
        // a form the other page reached too deep - its revision would differ from the listed one
        // on an unchanged document. When the walk is cut, the revision covers the page and the
        // forms the walk has loaded, on both sides the same. Each page is loaded into the
        // session of the script and compared there - the same revision unless such a limit cut
        // the walk (the first page always: the session holds nothing yet). Only a page, whose
        // revision differs there, is loaded once more in a session of its own, which is released
        // after the comparison: equal hashes mean the same containers with the same content, so
        // the session of the script never accepts a page the session of the page alone refuses.
        bool isSessionEmpty = true;
        for (const auto& revision : script->revisions)
        {
            const QString path = QStringLiteral("operations.%1.%2").arg(QString::fromLatin1(PDFContentJson::KEY_REVISIONS)).arg(revision.first);
            if (revision.first < 1 || revision.first > pageCount)
            {
                return PDFDocumentToolResult::createFailed(QStringLiteral("%1: %2").arg(path, Tool::tr("The page %1 does not exist.").arg(revision.first)));
            }

            const bool isComparedInEmptySession = isSessionEmpty;
            isSessionEmpty = false;

            QString errorText;
            if (!loadPage(&session, revision.first - 1, control, &errorText))
            {
                return createLoadFailure(control, path, errorText);
            }
            QByteArray currentRevision = PDFContentJson::getPageRevision(&session, revision.first - 1);

            if (!isComparedInEmptySession && currentRevision != revision.second)
            {
                PDFContentEditSession pageSession(input.document, context.cms, nullptr);
                if (!loadPage(&pageSession, revision.first - 1, control, &errorText))
                {
                    return createLoadFailure(control, path, errorText);
                }
                currentRevision = PDFContentJson::getPageRevision(&pageSession, revision.first - 1);
            }

            if (currentRevision.isEmpty() || currentRevision != revision.second)
            {
                PDFDocumentToolResult result = PDFDocumentToolResult::createFailed(Tool::tr("The document changed in the meantime."));
                result.data.insert(QStringLiteral("changedPage"), qint64(revision.first));
                return result;
            }
        }

        session.clearScriptNames();
        for (size_t i = 0; i < script->items.size(); ++i)
        {
            if (PDFOperationControl::isOperationCanceled(control))
            {
                return PDFDocumentToolResult::createStopped();
            }

            const QJsonObject& item = script->items[i];
            const QString path = QStringLiteral("operations.%1[%2]").arg(QString::fromLatin1(PDFContentJson::KEY_ITEMS)).arg(i);

            errors.clear();
            const PDFContentCommandPointer command = PDFContentCommandFactory::fromJson(item, &session, path, &errors);
            if (!command)
            {
                return createScriptFailure(errors, QStringLiteral("%1: %2").arg(path, Tool::tr("The operation cannot be read.")));
            }

            QString errorText;
            if (!session.execute(command, &errorText))
            {
                if (!command->isRefusedAsUnchanged())
                {
                    return PDFDocumentToolResult::createFailed(QStringLiteral("%1: %2").arg(path, errorText));
                }

                // The model is what the operation asks for already (owner 2026-11-26): a success
                // without an effect. Nothing was changed and nothing was added, so a name of
                // "as" names nothing; the script goes on and the result lists the operation.
                QJsonObject operation;
                operation.insert(QStringLiteral("index"), qint64(i));
                operation.insert(QString::fromLatin1(PDFContentJson::KEY_TYPE), item.value(QString::fromLatin1(PDFContentJson::KEY_TYPE)).toString());
                withoutEffect->append(operation);
                continue;
            }

            const QJsonValue name = item.value(QString::fromLatin1(PDFContentJson::KEY_AS));
            if (name.isString())
            {
                const std::vector<PDFContentNodeReference>& addedNodes = command->getChange().addedNodes;
                if (addedNodes.empty())
                {
                    return PDFDocumentToolResult::createFailed(QStringLiteral("%1.%2: %3").arg(path, QString::fromLatin1(PDFContentJson::KEY_AS), Tool::tr("The operation adds no object, which could get a name.")));
                }
                session.setScriptName(name.toString(), addedNodes.front());
            }
        }

        *snapshot = session.createSnapshot();
        session.clearScriptNames();
        return std::nullopt;
    }
};

QString PDFDocumentToolContent::getId() const
{
    return QString::fromLatin1(TOOL_ID);
}

PDFDocumentToolParameterDefinitions PDFDocumentToolContent::getParameters() const
{
    using Definition = PDFDocumentToolParameterDefinition;

    PDFDocumentToolParameterDefinitions definitions;

    std::vector<PDFDocumentToolParameterOption> modes;
    modes.push_back({ QString::fromLatin1(MODE_APPLY), QT_TR_NOOP("Apply"), QT_TR_NOOP("The operations change the content of the pages") });
    modes.push_back({ QString::fromLatin1(MODE_LIST), QT_TR_NOOP("List"), QT_TR_NOOP("The objects of the content of the pages as data; nothing is changed") });
    definitions.push_back(Definition::createEnum(tr, QString::fromLatin1(PARAMETER_MODE), QT_TR_NOOP("Mode"), QT_TR_NOOP("What is done with the content of the pages"), std::move(modes), QString::fromLatin1(MODE_APPLY)));

    // The work of the panels: a value, never stored and never in the schema of MCP
    definitions.push_back(Definition::createValue(tr, QString::fromLatin1(PARAMETER_SESSION), QT_TR_NOOP("Changes"), QT_TR_NOOP("The changes made in the editor"),
                                                  QVariant::fromValue(PDFContentEditSnapshotPointer())));

    // The script of an assistant (KE.8). It stays out of MCP until the actions of the assistant
    // and their documentation are written (stage E6, zadani_overeni.md 7.1): until then an
    // assistant may only read (the mode list).
    definitions.push_back(Definition::createString(tr, QString::fromLatin1(PARAMETER_OPERATIONS), QT_TR_NOOP("Operations"),
                                                   QT_TR_NOOP("JSON object of the operations: {\"revisions\": {\"<page>\": \"<revision of the page from the mode list>\"}, \"items\": [{\"type\": \"delete\", \"ids\": [\"p1:5\"]}]}; 1 to 500 operations, which change at most 20 pages"),
                                                   QString()).setStructuredData(true).setMaximumLength(MAXIMUM_OPERATIONS_LENGTH).setStored(false).setMcpExcluded(true));

    // imagePaths: declared with the first operation, which places an image (stage E3)

    definitions.push_back(Definition::createPageSelection(tr, QString::fromLatin1(PARAMETER_PAGES), QT_TR_NOOP("Pages"), QT_TR_NOOP("Pages, whose content is listed; at most 20 pages a call")).setStored(false));
    definitions.push_back(Definition::createInteger(tr, QString::fromLatin1(PARAMETER_DEPTH), QT_TR_NOOP("Depth"), QT_TR_NOOP("How deep the objects inside of groups, texts and forms are listed"),
                                                    PDFContentJson::DEFAULT_LIST_DEPTH, 1, PDFContentJson::MAXIMUM_LIST_DEPTH).setStored(false));
    definitions.push_back(Definition::createInteger(tr, QString::fromLatin1(PARAMETER_OFFSET), QT_TR_NOOP("Offset"), QT_TR_NOOP("Number of the objects of the first listed page to skip: the value nextOffset of the previous call"),
                                                    0, 0, Definition::MAXIMUM_SAFE_INTEGER).setStored(false));
    definitions.push_back(Definition::createInteger(tr, QString::fromLatin1(PARAMETER_MAXIMUM_OBJECTS), QT_TR_NOOP("Maximum objects"), QT_TR_NOOP("Most objects listed by the call"),
                                                    PDFContentJson::DEFAULT_LIST_OBJECT_COUNT, 1, PDFContentJson::MAXIMUM_LIST_OBJECT_COUNT).setStored(false));

    // The pages left out after an error of writing: no page by default
    PDFDocumentToolPageSelection noPage;
    noPage.isNone = true;
    definitions.push_back(Definition::createPageSelection(tr, QString::fromLatin1(PARAMETER_SKIP_PAGES), QT_TR_NOOP("Skipped pages"), QT_TR_NOOP("Pages, which are not written"), noPage).setPanelOnly(true).setStored(false));

    // The options of the display of the editor (zadani_gui.md 4.26: the menu View, the cards
    // Grid and units and Preview, the left panel): the last values of the tool, given only by
    // the panels and never in MCP; apply and list do not read them
    PDFDocumentToolParameterDefinitions options;
    options.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_SHOW_RULERS), QT_TR_NOOP("Rulers"), QT_TR_NOOP("The rulers along the canvas are shown"), false));
    options.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_SHOW_GRID), QT_TR_NOOP("Grid"), QT_TR_NOOP("The grid is shown on the canvas"), false));
    options.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_SHOW_GUIDES), QT_TR_NOOP("Guides"), QT_TR_NOOP("The guides are shown on the canvas"), true));
    options.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_SHOW_CLIPPING_PATHS), QT_TR_NOOP("Clipping paths"), QT_TR_NOOP("The outlines of the clipping paths are shown on the canvas"), false));
    options.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_SHOW_INVISIBLE_TEXT), QT_TR_NOOP("Invisible text"), QT_TR_NOOP("The baselines of invisible text are shown on the canvas"), false));
    options.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_SHOW_ANNOTATIONS), QT_TR_NOOP("Annotations and fields"), QT_TR_NOOP("The annotations and form fields are shown dimmed under the canvas"), true));
    options.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_SHOW_READING_ORDER), QT_TR_NOOP("Reading order"), QT_TR_NOOP("The reading order of the tagged content is shown on the canvas"), false));
    options.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_SNAP_TO_OBJECTS), QT_TR_NOOP("Snap to objects"), QT_TR_NOOP("A moved object snaps to the edges and centers of other objects"), true));
    options.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_SNAP_TO_GRID), QT_TR_NOOP("Snap to grid and guides"), QT_TR_NOOP("A moved object snaps to the grid and to the guides"), true));

    std::vector<PDFDocumentToolParameterOption> units;
    units.push_back({ QString::fromLatin1(UNITS_POINTS), QT_TR_NOOP("Points") });
    units.push_back({ QString::fromLatin1(UNITS_MILLIMETERS), QT_TR_NOOP("Millimeters") });
    units.push_back({ QString::fromLatin1(UNITS_INCHES), QT_TR_NOOP("Inches") });
    options.push_back(Definition::createEnum(tr, QString::fromLatin1(PARAMETER_UNITS), QT_TR_NOOP("Units"), QT_TR_NOOP("The units of the rulers and of the fields of the cards"), std::move(units), QString::fromLatin1(UNITS_POINTS)));
    options.push_back(Definition::createReal(tr, QString::fromLatin1(PARAMETER_GRID_SPACING), QT_TR_NOOP("Grid spacing"), QT_TR_NOOP("The distance of the lines of the grid in points"), 36.0, MINIMUM_GRID_SPACING, MAXIMUM_GRID_SPACING));

    std::vector<PDFDocumentToolParameterOption> origins;
    origins.push_back({ QString::fromLatin1(GRID_ORIGIN_BOTTOM_LEFT), QT_TR_NOOP("Bottom left") });
    origins.push_back({ QString::fromLatin1(GRID_ORIGIN_TOP_LEFT), QT_TR_NOOP("Top left") });
    options.push_back(Definition::createEnum(tr, QString::fromLatin1(PARAMETER_GRID_ORIGIN), QT_TR_NOOP("Origin"), QT_TR_NOOP("The corner of the page, from which the rulers and the grid are measured"), std::move(origins), QString::fromLatin1(GRID_ORIGIN_BOTTOM_LEFT)));

    options.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_PREVIEW_OVERPRINT), QT_TR_NOOP("Simulate overprint"), QT_TR_NOOP("The preview simulates overprint"), true));
    options.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_PREVIEW_ANNOTATIONS), QT_TR_NOOP("Show annotations and fields"), QT_TR_NOOP("The preview shows the annotations and form fields"), true));
    options.push_back(Definition::createString(tr, QString::fromLatin1(PARAMETER_PREVIEW_PAPER_COLOR), QT_TR_NOOP("Paper color"), QT_TR_NOOP("The color of the paper of the preview as #rrggbb"), QStringLiteral("#ffffff")).setPanelOnly(true).setMaximumLength(9));
    options.push_back(Definition::createString(tr, QString::fromLatin1(PARAMETER_LEFT_TAB), QT_TR_NOOP("Left panel tab"), QT_TR_NOOP("The tab of the left panel shown when the tool opens"), QStringLiteral("objects")).setPanelOnly(true).setMaximumLength(64));

    for (Definition& option : options)
    {
        definitions.push_back(std::move(option.setStored(true).setPanelOnly(true).setMcpExcluded(true)));
    }

    return definitions;
}

PDFDocumentToolResultKind PDFDocumentToolContent::getResultKind(const QVariantMap& parameters) const
{
    // list only reads; apply changes the document
    return PDFDocumentToolContentHelper::isList(parameters) ? PDFDocumentToolResultKind::Analysis : PDFDocumentToolResultKind::ChangeDocument;
}

PDFDocumentToolPreselectedResult PDFDocumentToolContent::getPreselectedResult(const QVariantMap& parameters) const
{
    // list is an analysis (no document); apply keeps the value of the row of the catalog (Keep)
    return PDFDocumentToolExtendedData::getDefaultPreselectedResult(getResultKind(parameters));
}

PDFDocumentToolSignatureImpact PDFDocumentToolContent::getSignatureImpact(const QVariantMap& parameters) const
{
    // list writes nothing; apply is an incremental update of a signed file (QE.3, the row of the catalog)
    return PDFDocumentToolExtendedData::getDefaultSignatureImpact(getResultKind(parameters));
}

bool PDFDocumentToolContent::isPageChange(const QVariantMap& parameters) const
{
    return !PDFDocumentToolContentHelper::isList(parameters) && PDFDocumentTool::isPageChange(parameters);
}

std::vector<PDFSecurityHandler::Permission> PDFDocumentToolContent::getRequiredPermissions(const QVariantMap& parameters) const
{
    if (PDFDocumentToolContentHelper::isList(parameters))
    {
        // The listing gives the text of the pages: ISO 32000-2, Table 22, bit 5 "copy or otherwise
        // extract text and graphics from the document" (as Extract Text does)
        return { PDFSecurityHandler::Permission::CopyContent };
    }

    // Bit 4 "modify the contents of the document": the row of the catalog
    return PDFDocumentTool::getRequiredPermissions(parameters);
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolContent::checkGivenParameters(const QVariantMap& given) const
{
    using Helper = PDFDocumentToolContentHelper;

    std::vector<PDFDocumentToolParameterError> errors = PDFDocumentTool::checkGivenParameters(given);

    const QString sessionParameter = QString::fromLatin1(PARAMETER_SESSION);
    const QString operationsParameter = QString::fromLatin1(PARAMETER_OPERATIONS);
    const bool hasSnapshot = Helper::getSnapshot(given) != nullptr;
    const bool hasOperations = !Helper::getOperations(given).isEmpty();

    if (Helper::isList(given))
    {
        // A value of the other mode would be silently ignored
        QStringList ignored;
        if (hasSnapshot)
        {
            ignored << sessionParameter;
        }
        if (hasOperations)
        {
            ignored << operationsParameter;
        }
        if (given.contains(QString::fromLatin1(PARAMETER_SKIP_PAGES)))
        {
            ignored << QString::fromLatin1(PARAMETER_SKIP_PAGES);
        }
        for (const QString& parameter : std::as_const(ignored))
        {
            errors.push_back({ parameter, tr("The parameter “%1” is used only by the mode apply.").arg(parameter) });
        }
        return errors;
    }

    for (const char* name : { PARAMETER_PAGES, PARAMETER_DEPTH, PARAMETER_OFFSET, PARAMETER_MAXIMUM_OBJECTS })
    {
        const QString parameter = QString::fromLatin1(name);
        if (given.contains(parameter))
        {
            errors.push_back({ parameter, tr("The parameter “%1” is used only by the mode list.").arg(parameter) });
        }
    }

    const QString sourceError = Helper::checkSource(hasSnapshot, hasOperations);
    if (!sourceError.isEmpty())
    {
        errors.push_back({ operationsParameter, sourceError });
    }
    else if (hasOperations)
    {
        std::vector<QString> scriptErrors;
        if (!PDFContentJson::readScript(Helper::getOperations(given), &scriptErrors))
        {
            for (const QString& scriptError : scriptErrors)
            {
                errors.push_back({ operationsParameter, scriptError });
            }
        }
    }

    return errors;
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolContent::validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const
{
    using Helper = PDFDocumentToolContentHelper;

    std::vector<PDFDocumentToolParameterError> errors = PDFDocumentTool::validate(context, parameters);
    const PDFDocument* document = context.getDocument();
    if (!errors.empty() || !document)
    {
        return errors;
    }

    QString errorText;
    if (Helper::isList(parameters))
    {
        if (!Helper::getListPages(document, parameters, &errorText))
        {
            errors.push_back({ QString::fromLatin1(PARAMETER_PAGES), errorText });
        }
        return errors;
    }

    const QString operationsParameter = QString::fromLatin1(PARAMETER_OPERATIONS);
    const QString operations = Helper::getOperations(parameters);
    const QString sourceError = Helper::checkSource(Helper::getSnapshot(parameters) != nullptr, !operations.isEmpty());
    if (!sourceError.isEmpty())
    {
        errors.push_back({ operationsParameter, sourceError });
        return errors;
    }

    if (!operations.isEmpty())
    {
        std::vector<QString> scriptErrors;
        const std::optional<PDFContentScript> script = PDFContentJson::readScript(operations, &scriptErrors);
        for (const QString& scriptError : scriptErrors)
        {
            errors.push_back({ operationsParameter, scriptError });
        }

        const PDFInteger pageCount = PDFInteger(document->getCatalog()->getPageCount());
        if (script && !script->revisions.empty() && script->revisions.rbegin()->first > pageCount)
        {
            errors.push_back({ operationsParameter, QStringLiteral("operations.%1.%2: %3").arg(QString::fromLatin1(PDFContentJson::KEY_REVISIONS)).arg(script->revisions.rbegin()->first)
                                                        .arg(tr("The page %1 does not exist.").arg(script->revisions.rbegin()->first)) });
        }
    }

    if (!Helper::getSkippedPages(document, parameters, &errorText))
    {
        errors.push_back({ QString::fromLatin1(PARAMETER_SKIP_PAGES), errorText });
    }

    return errors;
}

PDFDocumentToolResult PDFDocumentToolContent::execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    using Helper = PDFDocumentToolContentHelper;

    if (context.documents.empty() || !context.documents.front().document)
    {
        return PDFDocumentToolResult::createFailed(tr("There is no document."));
    }

    const PDFDocumentToolInputDocument& input = context.documents.front();
    const PDFDocument* document = input.document.data();
    QString errorText;

    if (Helper::isList(parameters))
    {
        // ---- list: the objects of the pages as data; nothing is changed ----
        const std::optional<std::vector<PDFInteger>> pages = Helper::getListPages(document, parameters, &errorText);
        if (!pages)
        {
            return PDFDocumentToolResult::createFailed(errorText);
        }

        PDFContentJson::ListOptions options;
        options.depth = qBound(1, parameters.value(QString::fromLatin1(PARAMETER_DEPTH), PDFContentJson::DEFAULT_LIST_DEPTH).toInt(), PDFContentJson::MAXIMUM_LIST_DEPTH);
        options.offset = qMax(qint64(0), parameters.value(QString::fromLatin1(PARAMETER_OFFSET), 0).toLongLong());

        // The limit of the objects is the limit of the call: the pages share it. A page, which
        // it does not reach, is not listed and the result names it.
        int remainingObjects = qBound(1, parameters.value(QString::fromLatin1(PARAMETER_MAXIMUM_OBJECTS), PDFContentJson::DEFAULT_LIST_OBJECT_COUNT).toInt(), PDFContentJson::MAXIMUM_LIST_OBJECT_COUNT);

        // The same for the texts of the document: the call returns at most
        // MAXIMUM_LIST_TEXT_LENGTH characters of them. A page is started only when the longest
        // texts of one object still fit (so its first object is never cut off by the limit).
        qint64 remainingText = PDFContentJson::MAXIMUM_LIST_TEXT_LENGTH;
        const qint64 longestObjectText = 2 * qint64(PDFContentJson::MAXIMUM_TEXT_LENGTH);

        QJsonArray listedPages;
        QJsonArray notListedPages;
        for (const PDFInteger pageIndex : *pages)
        {
            if (PDFOperationControl::isOperationCanceled(control))
            {
                return PDFDocumentToolResult::createStopped();
            }

            if (remainingObjects <= 0 || remainingText < longestObjectText)
            {
                notListedPages.append(qint64(pageIndex + 1));
                continue;
            }

            // A session of one page: the containers of a page are released before the next one
            PDFContentEditSession session(input.document, context.cms, nullptr);
            if (!Helper::loadPage(&session, pageIndex, control, &errorText))
            {
                if (PDFOperationControl::isOperationCanceled(control))
                {
                    return PDFDocumentToolResult::createStopped();
                }
                return PDFDocumentToolResult::createFailed(tr("Page %1: %2").arg(pageIndex + 1).arg(errorText));
            }

            options.maximumObjects = remainingObjects;
            options.maximumTextLength = remainingText;
            const QJsonObject page = PDFContentJson::writePage(&session, pageIndex, options);
            const QJsonArray objects = page.value(QStringLiteral("objects")).toArray();
            remainingObjects -= Helper::countListedObjects(objects);
            remainingText -= qMin(remainingText, Helper::countListedText(objects));
            if (page.contains(QStringLiteral("nextOffset")))
            {
                // The page is not listed whole: the call ends here
                remainingObjects = 0;
            }
            listedPages.append(page);

            // The offset belongs to the first page only
            options.offset = 0;
        }

        PDFDocumentToolResult result;
        result.data.insert(QString::fromLatin1(DATA_PAGES), listedPages);
        if (!notListedPages.isEmpty())
        {
            result.data.insert(QString::fromLatin1(DATA_PAGES_NOT_LISTED), notListedPages);
        }
        return result;
    }

    // ---- apply: the snapshot of the panels, or the snapshot of a replayed script ----
    PDFContentEditSnapshotPointer snapshot = Helper::getSnapshot(parameters);
    const QString operations = Helper::getOperations(parameters);
    const QString sourceError = Helper::checkSource(snapshot != nullptr, !operations.isEmpty());
    if (!sourceError.isEmpty())
    {
        return PDFDocumentToolResult::createFailed(sourceError);
    }

    const bool isScript = !operations.isEmpty();
    QJsonArray operationsWithoutEffect;
    if (isScript)
    {
        std::optional<PDFDocumentToolResult> failure = Helper::replayScript(context, operations, control, &snapshot, &operationsWithoutEffect);
        if (failure)
        {
            return std::move(*failure);
        }
    }

    if (!snapshot || snapshot->isEmpty())
    {
        // Also a script, whose operations all changed nothing: the result lists them
        return Helper::createNothingToDo(operationsWithoutEffect, nullptr);
    }

    const std::optional<std::set<PDFInteger>> skippedPages = Helper::getSkippedPages(document, parameters, &errorText);
    if (!skippedPages)
    {
        return PDFDocumentToolResult::createFailed(errorText);
    }

    PDFContentDocumentWriter::Options options;
    options.skippedPages = *skippedPages;

    PDFDocumentBuilder builder(document);
    const PDFContentWriteResult writeResult = PDFContentDocumentWriter::write(document, *snapshot, &builder, options, control, progress);
    switch (writeResult.status)
    {
        case PDFContentWriteResult::Status::Success:
            break;

        case PDFContentWriteResult::Status::Canceled:
            return PDFDocumentToolResult::createStopped();

        case PDFContentWriteResult::Status::DocumentChanged:
            // RE-15: the document is not the one the changes were made over
            return PDFDocumentToolResult::createFailed(tr("The document changed in the meantime."));

        case PDFContentWriteResult::Status::PageErrors:
        {
            // The errors of the pages go to the caller as data: the editor shows them and offers
            // to skip the page (a new run with skipPages)
            PDFDocumentToolResult result = PDFDocumentToolResult::createFailed(tr("%Ln page(s) cannot be written.", "", int(writeResult.errors.size())));
            result.data.insert(QString::fromLatin1(DATA_PAGE_ERRORS), Helper::createPageErrors(writeResult, *snapshot));
            return result;
        }
    }

    if (writeResult.pagesChanged.empty() && !writeResult.isStructureChanged)
    {
        // Nothing was written - also when every change belongs to a skipped page or to a form,
        // which a skipped page uses: the result says, what was left as it is
        return Helper::createNothingToDo(operationsWithoutEffect, &writeResult);
    }

    if (isScript && writeResult.pagesChanged.size() > size_t(PDFContentJson::MAXIMUM_CHANGED_PAGE_COUNT))
    {
        // zadani_overeni.md 7.1: the user confirms a call of an assistant over the changed pages
        return PDFDocumentToolResult::createFailed(tr("The operations change %Ln page(s); one call changes at most %1.", "", int(writeResult.pagesChanged.size())).arg(PDFContentJson::MAXIMUM_CHANGED_PAGE_COUNT));
    }

    PDFDocument output;
    ConformanceRemoval conformance;
    try
    {
        output = builder.build();

        // QE.2 (a): the content is not checked against a standard the document declares
        PDFObjectStorage storage = output.getStorage();
        conformance = removeConformanceDeclaration(&storage, &output);
        if (!conformance.kept.isEmpty())
        {
            return createConformanceFailure(conformance.kept);
        }
        if (!conformance.removed.isEmpty())
        {
            output = PDFDocument(std::move(storage), document->getInfo()->version, QByteArray());
        }
    }
    catch (const PDFException& exception)
    {
        return PDFDocumentToolResult::createFailed(exception.getMessage());
    }

    // ISO 32000-2, 12.8.2.2, Table 257: no level of a certification allows a change of the content
    // of a page - the certification is removed and the result is only a copy (QE.3)
    const PDFDocumentToolOutputTraits traits = getOutputTraits(parameters);
    const bool isCertificationRemoved = PDFDocMDPPermissions::isCertificationRemovedBy(document, traits);
    PDFInteger certificationSignaturesRemoved = 0;
    if (isCertificationRemoved)
    {
        output = PDFDocMDPPermissions::removeCertification(output, &certificationSignaturesRemoved);
    }

    PDFDocumentToolResult result;
    PDFDocumentToolOutputDocument outputDocument;
    outputDocument.kind = PDFDocumentToolOutputDocument::Kind::Changed;
    outputDocument.inputIndex = 0;
    outputDocument.document = PDFDocumentPointer(new PDFDocument(std::move(output)));
    outputDocument.flags = PDFModifiedDocument::ModificationFlags(PDFModifiedDocument::Reset);   // KE.9
    const QString baseName = QFileInfo(input.fileName).completeBaseName();
    outputDocument.suggestedFileName = (baseName.isEmpty() ? QStringLiteral("document") : baseName) + QStringLiteral("_edited.pdf");
    result.documents.push_back(std::move(outputDocument));

    // ---- The facts of the result and the data of MCP ----
    QJsonArray pagesChanged;
    for (const PDFInteger pageIndex : writeResult.pagesChanged)
    {
        pagesChanged.append(qint64(pageIndex + 1));
    }
    result.data.insert(QString::fromLatin1(DATA_PAGES_CHANGED), pagesChanged);
    result.data.insert(QString::fromLatin1(DATA_OBJECTS_ADDED), qint64(writeResult.objectsAdded));
    result.data.insert(QString::fromLatin1(DATA_OBJECTS_EDITED), qint64(writeResult.objectsEdited));
    result.data.insert(QString::fromLatin1(DATA_OBJECTS_REMOVED), qint64(writeResult.objectsRemoved));
    result.data.insert(QString::fromLatin1(DATA_LAYERS), 0);    // The layers are changed from stage E4
    result.data.insert(QString::fromLatin1(DATA_STRUCTURE), writeResult.isStructureChanged);
    result.data.insert(QString::fromLatin1(DATA_FONTS), qint64(writeResult.fontsAdded));

    if (writeResult.isPagesChangedIncomplete)
    {
        result.data.insert(QString::fromLatin1(DATA_PAGES_CHANGED_INCOMPLETE), true);
    }

    if (!writeResult.pagesChanged.empty())
    {
        const QString pages = tr("%Ln page(s): %1", "", int(writeResult.pagesChanged.size())).arg(PDFDocumentPageOperations::formatPageNumbers(writeResult.pagesChanged));
        result.facts.push_back({ QString::fromLatin1(FACT_PAGES_CHANGED), tr("Pages changed"),
                                 writeResult.isPagesChangedIncomplete ? tr("%1 and possibly other pages, which use a changed form").arg(pages) : pages });
    }

    const QStringList objects = { tr("%Ln added", "objects", int(writeResult.objectsAdded)),
                                  tr("%Ln edited", "objects", int(writeResult.objectsEdited)),
                                  tr("%Ln removed", "objects", int(writeResult.objectsRemoved)) };
    result.facts.push_back({ QString::fromLatin1(FACT_OBJECTS), tr("Objects"), objects.join(QStringLiteral(", ")) });

    if (writeResult.isStructureChanged)
    {
        result.facts.push_back({ QString::fromLatin1(FACT_STRUCTURE), tr("Structure"), tr("The tags of the changed content were updated") });
    }

    if (writeResult.fontsAdded > 0)
    {
        result.facts.push_back({ QString::fromLatin1(FACT_FONTS), tr("Fonts"), tr("%Ln font(s) added", "", int(writeResult.fontsAdded)) });
    }

    // A skipped page stays as it is (owner 2026-11-26): a changed form, which it shares with a
    // written page, was not written. Not an error - the rest was written - but the user must
    // know, that a change of a written page is missing.
    if (!writeResult.skippedContainers.empty())
    {
        result.data.insert(QString::fromLatin1(DATA_FORMS_NOT_WRITTEN), Helper::createFormsNotWritten(writeResult));
        result.facts.push_back({ QString::fromLatin1(FACT_FORMS_NOT_WRITTEN), tr("Shared content"),
                                 tr("%Ln changed form(s) were left as they are, because a skipped page uses them too", "", int(writeResult.skippedContainers.size())) });
    }

    if (!operationsWithoutEffect.isEmpty())
    {
        result.data.insert(QString::fromLatin1(DATA_OPERATIONS_WITHOUT_EFFECT), operationsWithoutEffect);
    }

    addConformanceFact(&result, conformance.removed);

    // QE.2: a tagged document says, how many objects of its written pages have no tag
    if (Helper::isTaggedDocument(document))
    {
        PDFInteger notTagged = 0;
        for (const auto& item : snapshot->containers)
        {
            const bool isWrittenPage = item.first.kind == PDFContentContainerKey::Kind::Page && item.second && !item.second->isReadOnly() &&
                                       !item.second->isUnchanged() && !skippedPages->count(item.first.pageIndex);
            if (isWrittenPage)
            {
                notTagged += Helper::countNotTagged(*item.second);
            }
        }

        result.data.insert(QString::fromLatin1(FACT_NOT_TAGGED), qint64(notTagged));
        if (notTagged > 0)
        {
            result.facts.push_back({ QString::fromLatin1(FACT_NOT_TAGGED), tr("Not tagged"), tr("%Ln object(s) of the changed pages have no tag", "", int(notTagged)) });
        }
    }

    result.data.insert(QStringLiteral("signatureCount"), PDFForm::getSignatureCount(document));
    result.data.insert(QStringLiteral("signatureImpact"), QStringLiteral("incremental-save"));
    result.data.insert(QStringLiteral("certified"), isCertificationRemoved);
    if (isCertificationRemoved)
    {
        PDFDocMDPPermissions::addCertificationRemoved(&result, certificationSignaturesRemoved);
    }

    return result;
}

}   // namespace pdf
