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

#include "pdfannotation.h"
#include "pdfannotationmanipulator.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentwriter.h"
#include "pdfdrawwidget.h"
#include "pdfdrawspacecontroller.h"
#include "pdfwidgetannotation.h"
#include "pdfwidgettool.h"
#include "pdfcms.h"
#include "pdfprogress.h"

#include "pdfsidebarwidget.h"
#include "pdfundoredomanager.h"
#include "pdftexttospeech.h"
#include "pdfbookmarkmanager.h"
#include "pdfviewersettings.h"

#include <QtTest>
#include <QAction>
#include <QBuffer>
#include <QTreeView>
#include <QItemSelectionModel>

#include <array>
#include <vector>

using namespace pdf;

/// Tests of the annotation editing, which need the parts of the application shell - the history
/// of the document (undo/redo), the sidebar with the list of the notes, and saving and reopening
/// of the edited document.
class AnnotationIntegrationTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void undoRedoOfThread();
    void undoRedoOfGeometry();
    void sidebarSelectionIsSynchronized();
    void editedDocumentIsSavedAndReopened();

private:
    QTemporaryDir m_settingsDirectory;
};

/// The widget, the annotation manager, the history of the document and the sidebar are connected
/// the same way, as the application connects them - a modified document creates an undo step and
/// it is set to all components, undo/redo requests the change of the document.
struct IntegrationFixture
{
    PDFDocumentPointer document;
    PDFCMSManager cms{ nullptr };
    PDFProgress progress{ nullptr };
    PDFWidget widget{ &cms, RendererEngine::QPainter, nullptr };
    PDFWidgetAnnotationManager annotations{ widget.getDrawWidgetProxy(), nullptr };
    std::array<QAction, 10> actions;
    PDFToolManager tools{ widget.getDrawWidgetProxy(),
        { &actions[0], &actions[1], &actions[2], &actions[3], &actions[4], &actions[5],
          &actions[6], &actions[7], &actions[8], &actions[9] }, nullptr, &widget };
    pdfviewer::PDFUndoRedoManager undoRedo{ nullptr };
    pdfviewer::PDFViewerSettings settings{ nullptr };
    pdfviewer::PDFTextToSpeech textToSpeech{ nullptr };
    pdfviewer::PDFBookmarkManager bookmarks{ nullptr };
    std::unique_ptr<pdfviewer::PDFSidebarWidget> sidebar;

    explicit IntegrationFixture(PDFDocument initialDocument) :
        document(new PDFDocument(std::move(initialDocument)))
    {
        widget.resize(800, 800);
        widget.getDrawWidget()->getWidget()->resize(800, 800);
        widget.setAnnotationManager(&annotations);
        widget.setToolManager(&tools);
        widget.getDrawWidgetProxy()->setProgress(&progress);

        // The application sets the limits of the history from its settings (there is no history by default)
        undoRedo.setMaximumSteps(10, 10);
        sidebar.reset(new pdfviewer::PDFSidebarWidget(widget.getDrawWidgetProxy(), &textToSpeech, nullptr, &bookmarks, &settings, false, nullptr));
        apply(PDFModifiedDocument(document, nullptr));

        QObject::connect(&annotations, &PDFWidgetAnnotationManager::documentModified, &annotations, [this](PDFModifiedDocument modified)
        {
            PDFDocumentPointer oldDocument = document;
            document = modified;
            undoRedo.createUndo(modified, oldDocument);
            apply(PDFModifiedDocument(document, nullptr, modified.getFlags()));
        });

        QObject::connect(&undoRedo, &pdfviewer::PDFUndoRedoManager::documentChangeRequest, &annotations, [this](PDFModifiedDocument modified)
        {
            document = modified;
            apply(modified);
        });
    }

    ~IntegrationFixture()
    {
        sidebar.reset();
        tools.setDocument(PDFModifiedDocument());
        annotations.setDocument(PDFModifiedDocument());
        widget.setDocument(PDFModifiedDocument(), {});
        widget.setAnnotationManager(nullptr);
        widget.setToolManager(nullptr);
    }

    void apply(PDFModifiedDocument modified)
    {
        widget.setDocument(modified, {});
        annotations.setDocument(modified);
        tools.setDocument(modified);
        sidebar->setDocument(modified, {});
    }

    std::vector<PDFObjectReference> pageAnnotations() const
    {
        return document->getCatalog()->getPage(0)->getAnnotations();
    }

    QRectF rectangle(PDFObjectReference annotation) const
    {
        const PDFAnnotationPtr parsed = PDFAnnotation::parse(&document->getStorage(), annotation);
        return parsed ? parsed->getRectangle().normalized() : QRectF();
    }

    bool key(int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QKeyEvent event(QEvent::KeyPress, key, modifiers);
        event.ignore();
        annotations.keyPressEvent(&widget, &event);
        return event.isAccepted();
    }
};

void AnnotationIntegrationTest::initTestCase()
{
    QVERIFY(m_settingsDirectory.isValid());
    QCoreApplication::setOrganizationName("PDF4QT-Tests");
    QCoreApplication::setApplicationName("AnnotationIntegration");
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDirectory.path());
}

void AnnotationIntegrationTest::undoRedoOfThread()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference square = builder.createAnnotationSquare(page, QRectF(50, 50, 60, 40), 1.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    IntegrationFixture fixture(builder.build());

    // A reply is written, the thread is cut and pasted. Each operation is a single step of the history.
    const size_t baseCount = fixture.pageAnnotations().size();
    QVERIFY(!fixture.undoRedo.canUndo());
    QVERIFY(fixture.annotations.addAnnotationReply(square, "I do not agree."));
    QVERIFY(fixture.undoRedo.canUndo());
    const size_t countWithReply = fixture.pageAnnotations().size();
    QCOMPARE(PDFAnnotationManipulator::getReplies(&fixture.document->getStorage(), page, square).size(), size_t(1));

    fixture.annotations.setSelectedAnnotations({ square });
    fixture.annotations.cutSelectedAnnotations();
    QVERIFY(fixture.pageAnnotations().empty());

    // Undo returns the whole thread with its references
    fixture.undoRedo.doUndo();
    QCOMPARE(fixture.pageAnnotations().size(), countWithReply);
    const std::vector<PDFObjectReference> replies = PDFAnnotationManipulator::getReplies(&fixture.document->getStorage(), page, square);
    QCOMPARE(replies.size(), size_t(1));
    QCOMPARE(PDFAnnotation::parse(&fixture.document->getStorage(), replies.front())->getContents(), QString("I do not agree."));
    QVERIFY(fixture.undoRedo.canRedo());

    // Redo cuts it again, the second undo removes the reply
    fixture.undoRedo.doRedo();
    QVERIFY(fixture.pageAnnotations().empty());
    fixture.undoRedo.doUndo();
    fixture.undoRedo.doUndo();
    QVERIFY(!fixture.undoRedo.canUndo());
    QVERIFY(PDFAnnotationManipulator::getReplies(&fixture.document->getStorage(), page, square).empty());
    QVERIFY(fixture.pageAnnotations().size() < countWithReply);

    // The clipboard still has the thread - it is pasted as a single step
    QCOMPARE(fixture.pageAnnotations().size(), baseCount);
    fixture.annotations.pasteAnnotations(std::nullopt, true);
    QCOMPARE(fixture.pageAnnotations().size(), baseCount + countWithReply);
    fixture.undoRedo.doUndo();
    QCOMPARE(fixture.pageAnnotations().size(), baseCount);
    QVERIFY(!fixture.undoRedo.canUndo());
}

void AnnotationIntegrationTest::undoRedoOfGeometry()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference polygon = builder.createAnnotationPolygon(page, { QPointF(50, 50), QPointF(150, 50), QPointF(100, 150) }, 1.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference square = builder.createAnnotationSquare(page, QRectF(200, 200, 60, 40), 1.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    IntegrationFixture fixture(builder.build());

    const QRectF polygonRectangle = fixture.rectangle(polygon);
    const QRectF squareRectangle = fixture.rectangle(square);

    // Several annotations are moved by the keyboard - it is a single step of the history
    fixture.annotations.setSelectedAnnotations({ polygon, square });
    QVERIFY(fixture.key(Qt::Key_Right, Qt::ShiftModifier));
    QVERIFY(fixture.rectangle(polygon) != polygonRectangle);
    QVERIFY(fixture.rectangle(square) != squareRectangle);

    fixture.undoRedo.doUndo();
    QCOMPARE(fixture.rectangle(polygon), polygonRectangle);
    QCOMPARE(fixture.rectangle(square), squareRectangle);
    QVERIFY(!fixture.undoRedo.canUndo());

    // The selection survives the undo (the annotations still exist)
    QVERIFY(fixture.annotations.isAnnotationSelected(polygon));
    QVERIFY(fixture.annotations.isAnnotationSelected(square));

    fixture.undoRedo.doRedo();
    QVERIFY(fixture.rectangle(polygon) != polygonRectangle);

    // The numerical rectangle and the rotation
    QVERIFY(fixture.annotations.setAnnotationRectangle(square, QRectF(100, 100, 120, 80)));
    fixture.annotations.setSelectedAnnotations({ square });
    fixture.annotations.rotateSelectedAnnotations(90.0);
    QVERIFY(qAbs(fixture.rectangle(square).width() - 80.0) < 0.01);
    fixture.undoRedo.doUndo();
    QVERIFY(qAbs(fixture.rectangle(square).width() - 120.0) < 0.01);
    fixture.undoRedo.doUndo();
    QCOMPARE(fixture.rectangle(square).size(), squareRectangle.size());

    // An annotation, which is removed by the undo, is removed from the selection
    fixture.annotations.setSelectedAnnotations({ square });
    fixture.annotations.copySelectedAnnotations();
    fixture.annotations.pasteAnnotations(std::nullopt, true);
    const std::vector<PDFObjectReference> pasted = fixture.annotations.getSelectedAnnotations();
    QCOMPARE(pasted.size(), size_t(1));
    QVERIFY(pasted.front() != square);
    fixture.undoRedo.doUndo();
    QVERIFY(!fixture.annotations.isAnnotationSelected(pasted.front()));
}

void AnnotationIntegrationTest::sidebarSelectionIsSynchronized()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference first = builder.createAnnotationSquare(page, QRectF(50, 50, 60, 40), 1.0, Qt::yellow, Qt::black, "Alice", "Subject", "First contents");
    const PDFObjectReference second = builder.createAnnotationSquare(page, QRectF(150, 150, 40, 40), 1.0, Qt::green, Qt::black, "Bob", "Subject", "Second contents");
    const PDFObjectReference third = builder.createAnnotationCircle(page, QRectF(200, 50, 40, 40), 1.0, Qt::green, Qt::black, "Bob", "Subject", "Third contents");
    IntegrationFixture fixture(builder.build());

    QTreeView* notesView = fixture.sidebar->findChild<QTreeView*>("notesTreeView");
    QVERIFY(notesView);
    QAbstractItemModel* model = notesView->model();
    QItemSelectionModel* selectionModel = notesView->selectionModel();

    // Leaves of the tree (page / author / note) and their texts
    auto getLeaves = [model]()
    {
        std::vector<QModelIndex> leaves;
        std::vector<QModelIndex> stack = { QModelIndex() };
        while (!stack.empty())
        {
            const QModelIndex parent = stack.back();
            stack.pop_back();
            for (int row = 0; row < model->rowCount(parent); ++row)
            {
                const QModelIndex index = model->index(row, 0, parent);
                if (model->rowCount(index) == 0)
                {
                    leaves.push_back(index);
                }
                else
                {
                    stack.push_back(index);
                }
            }
        }
        return leaves;
    };

    auto findLeaf = [&](const QString& text)
    {
        for (const QModelIndex& index : getLeaves())
        {
            if (index.data(Qt::DisplayRole).toString().contains(text))
            {
                return index;
            }
        }
        return QModelIndex();
    };

    QCOMPARE(getLeaves().size(), size_t(3));
    QVERIFY(findLeaf("First contents").isValid());
    QVERIFY(selectionModel->selectedIndexes().isEmpty());

    // Selection on the page selects the notes...
    fixture.annotations.setSelectedAnnotations({ first, third });
    QCOMPARE(selectionModel->selectedIndexes().size(), 2);
    QVERIFY(selectionModel->isSelected(findLeaf("First contents")));
    QVERIFY(selectionModel->isSelected(findLeaf("Third contents")));
    QVERIFY(!selectionModel->isSelected(findLeaf("Second contents")));

    fixture.annotations.clearSelection();
    QVERIFY(selectionModel->selectedIndexes().isEmpty());

    // ...and selection of the notes selects the annotations on the page
    selectionModel->select(findLeaf("Second contents"), QItemSelectionModel::ClearAndSelect);
    QVERIFY(fixture.annotations.isAnnotationSelected(second));
    QCOMPARE(fixture.annotations.getSelectedAnnotations().size(), size_t(1));

    selectionModel->select(findLeaf("Third contents"), QItemSelectionModel::Select);
    QCOMPARE(fixture.annotations.getSelectedAnnotations().size(), size_t(2));
    QVERIFY(fixture.annotations.isAnnotationSelected(third));

    // The list is rebuilt, when the document is modified - the selection stays on both sides
    QVERIFY(fixture.key(Qt::Key_Right));
    QVERIFY(fixture.undoRedo.canUndo());
    QCOMPARE(fixture.annotations.getSelectedAnnotations().size(), size_t(2));
    QCOMPARE(selectionModel->selectedIndexes().size(), 2);
    QVERIFY(selectionModel->isSelected(findLeaf("Second contents")));
    QVERIFY(selectionModel->isSelected(findLeaf("Third contents")));

    // Deleted annotations disappear from the list
    QVERIFY(fixture.key(Qt::Key_Delete));
    QCOMPARE(getLeaves().size(), size_t(1));
    QVERIFY(selectionModel->selectedIndexes().isEmpty());
    QVERIFY(!fixture.annotations.hasSelection());

    fixture.undoRedo.doUndo();
    QCOMPARE(getLeaves().size(), size_t(3));
}

void AnnotationIntegrationTest::editedDocumentIsSavedAndReopened()
{
    // Annotations, which are hard to edit - the annotation does not refer to its page, the media box
    // of the page is inherited, and the polygon is defined by a path without the vertices
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference polygon = builder.createAnnotationPolygon(page, { QPointF(50, 50), QPointF(150, 50), QPointF(100, 150) }, 1.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference line = builder.createAnnotationLine(page, QRectF(0, 0, 300, 300), QPointF(50, 200), QPointF(150, 200), 1.0, Qt::red, Qt::blue,
                                                                 "Title", "Subject", "100 pt", AnnotationLineEnding::None, AnnotationLineEnding::None, 0.0, 0.0, 0.0, true, true);

    auto setEntry = [&builder](PDFObjectReference reference, const char* key, PDFObject value)
    {
        PDFObjectFactory factory;
        factory.beginDictionary();
        factory.beginDictionaryItem(key);
        factory << value;
        factory.endDictionaryItem();
        factory.endDictionary();
        builder.mergeTo(reference, factory.takeObject());
    };

    PDFObjectFactory pathFactory;
    pathFactory.beginArray();
    pathFactory << std::vector<PDFReal>{ 50, 50 } << std::vector<PDFReal>{ 150, 50 } << std::vector<PDFReal>{ 100, 150 };
    pathFactory.endArray();
    setEntry(polygon, "Path", pathFactory.takeObject());
    setEntry(polygon, "Vertices", PDFObject());
    setEntry(polygon, "P", PDFObject());
    setEntry(line, "P", PDFObject());
    setEntry(line, "IT", PDFObject::createName("LineDimension"));

    // The media box is moved to the node of the page tree
    const PDFObject pageObject = builder.getObjectByReference(page);
    const PDFDictionary* pageDictionary = builder.getDictionaryFromObject(pageObject);
    const PDFObject parentObject = pageDictionary->get("Parent");
    QVERIFY(parentObject.isReference());
    PDFObjectFactory mediaBoxFactory;
    mediaBoxFactory.beginDictionary();
    mediaBoxFactory.beginDictionaryItem("MediaBox");
    mediaBoxFactory << QRectF(0, 0, 300, 300);
    mediaBoxFactory.endDictionaryItem();
    mediaBoxFactory.endDictionary();
    builder.mergeTo(parentObject.getReference(), mediaBoxFactory.takeObject());
    setEntry(page, "MediaBox", PDFObject());

    IntegrationFixture fixture(builder.build());

    // The annotations are edited by the manager
    fixture.annotations.setSelectedAnnotations({ polygon });
    QVERIFY(fixture.key(Qt::Key_Left, Qt::AltModifier));
    QVERIFY(fixture.key(Qt::Key_Up, Qt::ShiftModifier));
    fixture.annotations.setSelectedAnnotations({ line });
    fixture.annotations.rotateSelectedAnnotations(90.0);
    QVERIFY(fixture.annotations.setAnnotationRectangle(polygon, QRectF(40, 40, 150, 140)));

    // The document is written and read again
    QBuffer buffer;
    QVERIFY(buffer.open(QBuffer::ReadWrite));
    PDFDocumentWriter writer(nullptr);
    QVERIFY(writer.write(&buffer, fixture.document.data()));

    PDFDocumentReader reader(nullptr, [](bool*) { return QString(); }, true, false);
    const PDFDocument reopened = reader.readFromBuffer(buffer.data());
    QCOMPARE(reader.getReadingResult(), PDFDocumentReader::Result::OK);

    // The annotations of the reopened document are the edited annotations - the geometry, the
    // text and also the appearance streams (they are, what is displayed) are the same
    bool hasAppearance = false;
    const std::vector<PDFObjectReference> oldAnnotations = fixture.pageAnnotations();
    const std::vector<PDFObjectReference> newAnnotations = reopened.getCatalog()->getPage(0)->getAnnotations();
    QCOMPARE(newAnnotations.size(), oldAnnotations.size());
    QVERIFY(!oldAnnotations.empty());

    // Content of the normal appearance stream of the annotation
    auto getAppearance = [](const PDFDocument& currentDocument, PDFObjectReference annotation)
    {
        const PDFDictionary* dictionary = currentDocument.getDictionaryFromObject(currentDocument.getObjectByReference(annotation));
        const PDFDictionary* appearanceDictionary = dictionary ? currentDocument.getDictionaryFromObject(dictionary->get("AP")) : nullptr;
        const PDFObject appearance = appearanceDictionary ? currentDocument.getObject(appearanceDictionary->get("N")) : PDFObject();
        return appearance.isStream() ? currentDocument.getDecodedStream(appearance.getStream()) : QByteArray();
    };

    for (size_t i = 0; i < oldAnnotations.size(); ++i)
    {
        const PDFAnnotationPtr oldAnnotation = PDFAnnotation::parse(&fixture.document->getStorage(), oldAnnotations[i]);
        const PDFAnnotationPtr newAnnotation = PDFAnnotation::parse(&reopened.getStorage(), newAnnotations[i]);
        QVERIFY(oldAnnotation && newAnnotation);
        QCOMPARE(newAnnotation->getType(), oldAnnotation->getType());
        QCOMPARE(newAnnotation->getContents(), oldAnnotation->getContents());

        const QRectF oldRectangle = oldAnnotation->getRectangle();
        const QRectF newRectangle = newAnnotation->getRectangle();
        QVERIFY(QLineF(oldRectangle.topLeft(), newRectangle.topLeft()).length() < 0.01);
        QVERIFY(QLineF(oldRectangle.bottomRight(), newRectangle.bottomRight()).length() < 0.01);

        const std::vector<QPointF> oldPoints = PDFAnnotationManipulator::getEditablePoints(oldAnnotation.data()).points;
        const std::vector<QPointF> newPoints = PDFAnnotationManipulator::getEditablePoints(newAnnotation.data()).points;
        QCOMPARE(newPoints.size(), oldPoints.size());
        for (size_t j = 0; j < oldPoints.size(); ++j)
        {
            QVERIFY(QLineF(oldPoints[j], newPoints[j]).length() < 0.01);
        }

        QCOMPARE(getAppearance(reopened, newAnnotations[i]), getAppearance(*fixture.document, oldAnnotations[i]));
        hasAppearance = hasAppearance || !getAppearance(reopened, newAnnotations[i]).isEmpty();
    }
    QVERIFY(hasAppearance);

    // The edited annotations are there
    const PDFAnnotationPtr reopenedPolygon = PDFAnnotation::parse(&reopened.getStorage(), newAnnotations.front());
    QVERIFY(QLineF(reopenedPolygon->getRectangle().normalized().topLeft(), QPointF(40, 40)).length() < 0.01);
    QVERIFY(QLineF(reopenedPolygon->getRectangle().normalized().bottomRight(), QPointF(190, 180)).length() < 0.01);
}

QTEST_MAIN(AnnotationIntegrationTest)

#include "tst_annotationintegrationtest.moc"
