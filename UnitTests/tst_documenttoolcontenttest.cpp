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

// Edit Content, stage E0: the tool edit.content of the core, the writer of a snapshot into a
// document and the JSON of the tool (package P05; GUI_overhaul/edit_tool/zadani_jadro.md 4.4, 4.6,
// zadani_overeni.md 6.2, 7.1, chapter 8).
//
// Placeholder of the skeleton: the single function checks only what the skeleton guarantees -
// the tool exists, is in the catalog, is not registered and its run fails, so nobody takes the
// skeleton for a working tool; the names of the parameters and the limits of the script - and
// never calls a function of the package (they are stubs ending the process). Package P05 replaces
// it by its tests. The expected values are written by hand from the plan, never taken from a run.

#include "pdfcontentjson.h"
#include "pdfdocumenttool.h"
#include "pdfdocumenttool_content.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfsecurityhandler.h"

#include <QtTest>
#include <QVariantMap>

#include <vector>

using namespace pdf;

class DocumentToolContentTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void skeletonTool();
};

void DocumentToolContentTest::skeletonTool()
{
    using Tool = PDFDocumentToolContent;
    using Permissions = std::vector<PDFSecurityHandler::Permission>;

    const QString toolId = QStringLiteral("edit.content");

    // ---- The tool and its row of the catalog (zadani.md KE.6, QE.3, QE.4) ----
    Tool tool;
    QCOMPARE(tool.getId(), toolId);
    QCOMPARE(QString::fromLatin1(Tool::TOOL_ID), toolId);

    const PDFDocumentToolCatalogEntry* entry = PDFDocumentToolCatalog::findTool(toolId);
    QVERIFY(entry != nullptr);
    QCOMPARE(tool.getInfo(), entry);
    QCOMPARE(entry->getRequiredPermissions(), Permissions({ PDFSecurityHandler::Permission::Modify }));
    QVERIFY(entry->isPageChange());
    QVERIFY(entry->isAlwaysConfirmedByMcp());

    // The values, which the tool gives for a run, are the values of its row until the package
    // tells the modes apart: a change of the document, which changes the pages and needs the
    // permission Modify (bit 4 of ISO 32000-2, Table 22)
    const QVariantMap noParameters;
    QVERIFY(tool.getResultKind(noParameters) == PDFDocumentToolResultKind::ChangeDocument);
    QVERIFY(tool.isPageChange(noParameters));
    QCOMPARE(tool.getRequiredPermissions(noParameters), Permissions({ PDFSecurityHandler::Permission::Modify }));

    // ---- The tool is not registered in stage E0: its page stays Page Not Found and MCP does
    //      not offer it (the core registers nothing by itself) ----
    QVERIFY(!PDFDocumentToolRegistry::isImplemented(toolId));
    QVERIFY(PDFDocumentToolRegistry::getTool(toolId) == nullptr);
    QVERIFY(!PDFDocumentToolRegistry::getImplementedTools().contains(toolId));

    // ---- A run of the skeleton fails and says why: never a success, never a crash ----
    const PDFDocumentToolResult result = tool.execute(PDFDocumentToolContext(), noParameters, nullptr, nullptr);
    QVERIFY(result.status == PDFDocumentToolResultStatus::Failed);
    QVERIFY(!result.isSuccess());
    QVERIFY(!result.isUse());
    QCOMPARE(result.message, QStringLiteral("Edit Content is not implemented yet."));
    QVERIFY(result.documents.empty());
    QVERIFY(result.outputs.empty());

    // ---- The names of the parameters (the table of zadani_jadro.md 4.6) and the modes ----
    QCOMPARE(QByteArray(Tool::PARAMETER_MODE), QByteArray("mode"));
    QCOMPARE(QByteArray(Tool::PARAMETER_SESSION), QByteArray("session"));
    QCOMPARE(QByteArray(Tool::PARAMETER_OPERATIONS), QByteArray("operations"));
    QCOMPARE(QByteArray(Tool::PARAMETER_IMAGE_PATHS), QByteArray("imagePaths"));
    QCOMPARE(QByteArray(Tool::PARAMETER_PAGES), QByteArray("pages"));
    QCOMPARE(QByteArray(Tool::PARAMETER_DEPTH), QByteArray("depth"));
    QCOMPARE(QByteArray(Tool::PARAMETER_OFFSET), QByteArray("offset"));
    QCOMPARE(QByteArray(Tool::PARAMETER_MAXIMUM_OBJECTS), QByteArray("maximumObjects"));
    QCOMPARE(QByteArray(Tool::PARAMETER_SKIP_PAGES), QByteArray("skipPages"));
    QCOMPARE(QByteArray(Tool::MODE_APPLY), QByteArray("apply"));
    QCOMPARE(QByteArray(Tool::MODE_LIST), QByteArray("list"));

    // The facts of the result (zadani_jadro.md 4.6)
    QCOMPARE(QByteArray(Tool::DATA_PAGES_CHANGED), QByteArray("pagesChanged"));
    QCOMPARE(QByteArray(Tool::DATA_OBJECTS_ADDED), QByteArray("objectsAdded"));
    QCOMPARE(QByteArray(Tool::DATA_OBJECTS_EDITED), QByteArray("objectsEdited"));
    QCOMPARE(QByteArray(Tool::DATA_OBJECTS_REMOVED), QByteArray("objectsRemoved"));
    QCOMPARE(QByteArray(Tool::DATA_LAYERS), QByteArray("layers"));
    QCOMPARE(QByteArray(Tool::DATA_STRUCTURE), QByteArray("structure"));
    QCOMPARE(QByteArray(Tool::DATA_FONTS), QByteArray("fonts"));
    QCOMPARE(QByteArray(Tool::FACT_CONFORMANCE), QByteArray("conformance"));
    QCOMPARE(QByteArray(Tool::FACT_NOT_TAGGED), QByteArray("notTagged"));

    // ---- The limits of a script and of a listing (zadani_overeni.md 6.2, 7.1): 500 operations
    //      and 20 changed pages a call, a script of 4 MiB, texts of 65 536 characters; a listing
    //      of 500 objects by default and 2 000 at most a call, of the depth 4 by default and 16
    //      at most, of 20 pages at most ----
    QCOMPARE(PDFContentJson::MAXIMUM_OPERATION_COUNT, 500);
    QCOMPARE(PDFContentJson::MAXIMUM_CHANGED_PAGE_COUNT, 20);
    QCOMPARE(PDFContentJson::MAXIMUM_TEXT_LENGTH, 65536);
    QCOMPARE(Tool::MAXIMUM_OPERATIONS_LENGTH, 4 * 1024 * 1024);
    QCOMPARE(PDFContentJson::DEFAULT_LIST_OBJECT_COUNT, 500);
    QCOMPARE(PDFContentJson::MAXIMUM_LIST_OBJECT_COUNT, 2000);
    QCOMPARE(PDFContentJson::DEFAULT_LIST_DEPTH, 4);
    QCOMPARE(PDFContentJson::MAXIMUM_LIST_DEPTH, 16);
    QCOMPARE(PDFContentJson::MAXIMUM_LIST_PAGE_COUNT, 20);

    PDFContentJson::ListOptions listOptions;
    QCOMPARE(listOptions.depth, 4);
    QCOMPARE(listOptions.offset, PDFInteger(0));
    QCOMPARE(listOptions.maximumObjects, 500);

    // The keys of a script { "revisions": { ... }, "items": [ { "type": ..., "id": ..., "as": ... } ] }
    QCOMPARE(QByteArray(PDFContentJson::KEY_REVISIONS), QByteArray("revisions"));
    QCOMPARE(QByteArray(PDFContentJson::KEY_ITEMS), QByteArray("items"));
    QCOMPARE(QByteArray(PDFContentJson::KEY_TYPE), QByteArray("type"));
    QCOMPARE(QByteArray(PDFContentJson::KEY_ID), QByteArray("id"));
    QCOMPARE(QByteArray(PDFContentJson::KEY_AS), QByteArray("as"));

    PDFContentScript script;
    QVERIFY(script.revisions.empty());
    QVERIFY(script.items.empty());
}

QTEST_MAIN(DocumentToolContentTest)

#include "tst_documenttoolcontenttest.moc"
