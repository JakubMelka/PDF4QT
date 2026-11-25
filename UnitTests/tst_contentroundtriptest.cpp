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

// Edit Content: the gate of stage E0 (the coordinator's; GUI_overhaul/edit_tool/zadani.md 9.4
// point 4, zadani_overeni.md chapter 8). Without it the work does not go on. The gate will check,
// on documents built by hand and on a selection of the corpus by a filter:
//   (a) a page, which was not changed, is not written at all (the document keeps its objects);
//   (b) a forced rewrite without edits gives a stream equal byte for byte to the original
//       streams of the page joined by one line feed;
//   (c) a "touch" of every node (a wrap with the identity matrix for every node, which can have
//       one, a "move" of every node to its own place, a canonical writing of every path) gives
//       the same rendering.
//
// Placeholder of the skeleton: the single function checks only the plain types the gate will
// use - the options and the result of the document writer, the snapshot of a session - and never
// calls a function of a package (they are stubs ending the process). The coordinator replaces it
// by the gate. The expected values are written by hand from the plan, never taken from a run.

#include "pdfcontentdocumentwriter.h"
#include "pdfcontenteditsession.h"
#include "pdfcontentstreamwriter.h"

#include <QtTest>

#include <memory>

using namespace pdf;

class ContentRoundTripTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void skeletonPlainTypes();
};

void ContentRoundTripTest::skeletonPlainTypes()
{
    using Status = PDFContentWriteResult::Status;

    // The options of a write: by default no page is skipped and an unchanged container is not
    // written (part (a) of the gate); the forced rewrite of part (b) is asked for explicitly
    PDFContentDocumentWriter::Options options;
    QVERIFY(options.skippedPages.empty());
    QVERIFY(!options.forceRewrite);

    // A default result is a success, which wrote nothing: no error of a page, no changed
    // container, no page and no object in its facts
    PDFContentWriteResult result;
    QVERIFY(result.status == Status::Success);
    QVERIFY(result.isSuccess());
    QVERIFY(result.errors.empty());
    QVERIFY(result.changedContainers.empty());
    QVERIFY(result.pagesChanged.empty());
    QCOMPARE(result.objectsAdded, PDFInteger(0));
    QCOMPARE(result.objectsEdited, PDFInteger(0));
    QCOMPARE(result.objectsRemoved, PDFInteger(0));
    QVERIFY(!result.isStructureChanged);
    QCOMPARE(result.fontsAdded, PDFInteger(0));

    // Every other status is no success: errors of pages, a document changed in the meantime
    // (a fingerprint does not match), a cancellation
    for (const Status status : { Status::PageErrors, Status::DocumentChanged, Status::Canceled })
    {
        PDFContentWriteResult failed;
        failed.status = status;
        QVERIFY(!failed.isSuccess());
    }

    // The result of writing one container: a success without errors; an error of a node makes
    // it a failure (the container is not written at all then)
    PDFContentStreamWriteResult streamResult;
    QVERIFY(streamResult.isSuccess());
    QVERIFY(streamResult.content.isEmpty());
    QVERIFY(PDFContentStreamWriteResult{}.isSuccess());     // An aggregate, which can be written {}
    streamResult.errors.push_back(PDFContentWriteError{ PDFContentContainerKey(), PDFContentNodeId{ 4 }, QStringLiteral("error") });
    QVERIFY(!streamResult.isSuccess());

    // An error of the stream writer names no container: the document writer fills it
    QVERIFY(!PDFContentWriteError{}.container.isValid());
    QVERIFY(!PDFContentWriteError{}.nodeId.isValid());

    // A snapshot without a changed container and without a structure model is empty: there is
    // nothing to write
    PDFContentEditSnapshot snapshot;
    QCOMPARE(snapshot.revision, quint64(0));
    QVERIFY(snapshot.containers.empty());
    QVERIFY(snapshot.structure == nullptr);
    QVERIFY(snapshot.dirtyPages.empty());
    QVERIFY(snapshot.newObjects.empty());
    QVERIFY(snapshot.isEmpty());

    // A changed container makes it not empty; so does a changed structure alone
    PDFContentContainer::Data containerData;
    containerData.key = PDFContentContainerKey::createPage(0, PDFObjectReference(3, 0));
    PDFContentEditSnapshot contentSnapshot;
    contentSnapshot.containers[containerData.key] = std::make_shared<const PDFContentContainer>(containerData);
    QVERIFY(!contentSnapshot.isEmpty());

    PDFContentEditSnapshot structureSnapshot;
    structureSnapshot.structure = std::make_shared<const PDFContentStructureModel>();
    QVERIFY(!structureSnapshot.isEmpty());
}

QTEST_MAIN(ContentRoundTripTest)

#include "tst_contentroundtriptest.moc"
