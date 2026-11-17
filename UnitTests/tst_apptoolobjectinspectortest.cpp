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

// The GUI of Object Inspector (step 7 of phase 2, plan 2.7, 4.3, 8; package P2): the frames 482:14847 …
// 482:16403 - the lazy tree with the one set of the opened references of a view (B158), the views with their
// counts (482:15054), the decoding in a job (B165), Pin / Unpin (482:15778, 482:15989), the copy of the data of
// a stream (R7-22 (6)), the selection restored after a change of the document. The inspector of the core is
// PDFObjectInspector (package P1a): the tests need it implemented.

#include "apptoolshellfixture.h"

#include "pdfappdocument.h"
#include "pdfappdocumenttab.h"
#include "pdfapplocale.h"
#include "pdfappobjecttreemodel.h"
#include "pdfapppassworddialog.h"
#include "pdfapptoolgui_objectinspector.h"
#include "pdfapptooltoolbar.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumenttool_objectinspector.h"
#include "pdfdocumenttoolguiregistry.h"
#include "pdfdocumenttoolshell.h"
#include "pdfdocumentwriter.h"
#include "pdfsecurityhandler.h"

#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QApplication>
#include <QAbstractTextDocumentLayout>
#include <QClipboard>
#include <QMimeData>
#include <QTextBlock>
#include <QPainter>
#include <QScopeGuard>
#include <QToolButton>
#include <QtTest>

using pdftest::AppToolShellFixture;
using View = pdf::PDFObjectInspectorView;

/// Helpers of the test
class AppToolObjectInspectorTestHelper
{
public:
    AppToolObjectInspectorTestHelper() = delete;

    using GUI = pdf::PDFAppToolGUIObjectInspector;

    static pdf::PDFDocumentToolShell* openTool(AppToolShellFixture& fixture, pdf::PDFAppDocumentTab** tab = nullptr)
    {
        const QString id = QString::fromLatin1(pdf::PDFDocumentToolObjectInspector::TOOL_ID);
        pdf::PDFDocumentToolGUIRegistry::unregisterFactory(id);
        pdf::PDFDocumentToolGUIRegistry::registerFactory(id, std::make_shared<pdf::PDFDocumentToolGUIFactoryOf<GUI>>());
        pdf::PDFAppDocumentTab* documentTab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Project_Report.pdf"), 2));
        if (!documentTab || !AppToolShellFixture::waitFor([documentTab]() { return documentTab->getDocument()->isReady(); }))
        {
            return nullptr;
        }
        if (tab)
        {
            *tab = documentTab;
        }
        return fixture.openTool(documentTab, id);
    }

    static GUI* gui(pdf::PDFDocumentToolShell* shell)
    {
        return shell ? qobject_cast<GUI*>(shell->getGUI()) : nullptr;
    }

    static bool waitReady(GUI* gui)
    {
        return AppToolShellFixture::waitFor([gui]() { return gui->isReady() && !gui->isPreparing(); });
    }

    static bool waitContents(GUI* gui)
    {
        return AppToolShellFixture::waitFor([gui]() { return !gui->isLoadingContents(); });
    }

    /// The child of the row by the start of its text ("/Root"), fetched first
    static QModelIndex child(pdf::PDFAppObjectTreeModel* model, const QModelIndex& parent, const QString& prefix)
    {
        if (model->canFetchMore(parent))
        {
            model->fetchMore(parent);
        }
        for (int row = 0; row < model->rowCount(parent); ++row)
        {
            const QModelIndex index = model->index(row, 0, parent);
            if (model->data(index, Qt::DisplayRole).toString().startsWith(prefix))
            {
                return index;
            }
        }
        return QModelIndex();
    }

    static QStringList values(const pdf::PDFKeyValueList* list)
    {
        QStringList result;
        for (int i = 0; list && i < list->getRowCount(); ++i)
        {
            result << list->getKey(i) + QStringLiteral("=") + list->getValue(i);
        }
        return result;
    }
};

/// The contents view with its data of a drag (createMimeDataFromSelection is protected)
class AppToolObjectInspectorContentsProbe : public pdf::PDFAppObjectContentsView
{
public:
    using pdf::PDFAppObjectContentsView::PDFAppObjectContentsView;

    QString getDragText() const
    {
        const std::unique_ptr<QMimeData> mimeData(createMimeDataFromSelection());
        return mimeData ? mimeData->text() : QString();
    }
};

class AppToolObjectInspectorTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void documentViewIsLazy();
    void referenceOpenedOnce();
    void viewsWithCounts();
    void streamDecodedInJob();
    void copyOfStreamAsksPermission();
    void guardedContentsGiveNoDragData();
    void contentsPaddingAndLines();
    void largeNodeFetchedInParts();
    void pinAndUnpin();
    void selectionRestoredAfterChange();
    void pinnedTabKeepsItsVersion();
    void screenshots();

private:
    QLocale m_locale;
};

void AppToolObjectInspectorTest::initTestCase()
{
    // The numbers of the frames are of the US English user interface (O17); the geometry of the frames is the
    // geometry of the Fluent style (review RS7FIGMA)
    m_locale = pdf::PDFAppLocale::getUiLocale();
    pdf::PDFAppLocale::setUiLocale(QLocale(QLocale::English, QLocale::UnitedStates));
    QApplication::setStyle(new pdf::PDFWidgetStyle());
}

void AppToolObjectInspectorTest::cleanupTestCase()
{
    pdf::PDFAppLocale::setUiLocale(m_locale);
}

void AppToolObjectInspectorTest::documentViewIsLazy()
{
    // 482:14847: only Close; the view Document from the trailer; a row has its children only when it is opened
    // (B158: the tree is never built whole in the GUI thread); the contents are the PDF syntax of the object
    using H = AppToolObjectInspectorTestHelper;
    AppToolShellFixture fixture;
    pdf::PDFDocumentToolShell* shell = H::openTool(fixture);
    QVERIFY(shell);
    H::GUI* gui = H::gui(shell);
    QVERIFY(gui);
    QVERIFY(H::waitReady(gui));
    AppToolShellFixture::settle();

    QVERIFY(!shell->getToolbar()->getConfirmButton()->isVisible());
    QCOMPARE(shell->getToolbar()->getCancelButton()->text(), QStringLiteral("Close"));
    QCOMPARE(gui->getViewButton()->text(), QStringLiteral("Document"));
    QCOMPARE(gui->getObjectsHeader()->getSubtitleLabel()->text(), QStringLiteral("From the trailer of the document"));

    pdf::PDFAppObjectTreeModel* model = gui->getTreeModel();
    QCOMPARE(model->rowCount(QModelIndex()), 1);
    const QModelIndex trailer = model->index(0, 0, QModelIndex());
    QVERIFY(model->getNode(trailer)->isTrailer);
    QVERIFY(model->data(trailer, Qt::DisplayRole).toString().startsWith(QStringLiteral("Trailer  Dictionary")));
    QVERIFY(model->hasChildren(trailer));
    QCOMPARE(model->rowCount(trailer), 0);
    QVERIFY(model->canFetchMore(trailer));

    const QModelIndex root = H::child(model, trailer, QStringLiteral("/Root "));
    QVERIFY(root.isValid());
    QVERIFY(model->getNode(root)->reference.isValid());
    QCOMPARE(model->rowCount(root), 0);
    QVERIFY(!model->canFetchMore(trailer));

    // The trailer is chosen at the entry: Information, Description, the syntax of the dictionary
    QCOMPARE(H::values(gui->getInformationRows()), QStringList({ "Reference=Trailer", "Type=Dictionary" }));
    // 482:14847: no section Image for a dictionary - neither its header nor an empty card (RS7FIGMA)
    QVERIFY(!gui->getImageHeader()->isVisible());
    QVERIFY(!gui->getImageRows()->parentWidget()->isVisible());
    // object-tree 482:14976: the rows lie on bg/sidebar - no white card or frame around the tree (RS7FIGMA)
    {
        QWidget* tree = gui->getTreeView();
        QWidget* window = tree->window();
        const QImage image = window->grab().toImage();
        const QColor sidebar = pdf::PDFThemeManager::getInstance()->getColor(pdf::PDFThemeColors::BgSidebar);
        QCOMPARE(QColor(image.pixel(tree->mapTo(window, QPoint(tree->width() - 4, tree->height() - 4)))), sidebar);
        QCOMPARE(QColor(image.pixel(tree->mapTo(window, QPoint(0, tree->height() / 2)))), sidebar);
    }
    QCOMPARE(gui->getContentsCaption()->text(), QStringLiteral("PDF syntax of the object"));
    QVERIFY(gui->getContentsView()->toPlainText().startsWith(QStringLiteral("<<")));
    QVERIFY(gui->getContentsView()->toPlainText().contains(QStringLiteral("/Root")));
    QVERIFY(!gui->getContentsView()->isCopyGuarded());

    // The catalog: its reference and the status "1 0 R · Dictionary [n items]"
    gui->selectIndex(root);
    AppToolShellFixture::settle();
    const QString reference = pdf::PDFObjectInspector::getReferenceText(model->getNode(root)->reference);
    QCOMPARE(H::values(gui->getInformationRows()).first(), QStringLiteral("Reference=") + reference);
    QVERIFY(gui->getStatusText().startsWith(reference + QStringLiteral(" · Dictionary")));
}

void AppToolObjectInspectorTest::referenceOpenedOnce()
{
    // 482:14847 "A reference is opened only once in the tree": the /Parent of a page is the page tree, which is
    // opened already - a repeated row, shown, never expanded
    using H = AppToolObjectInspectorTestHelper;
    AppToolShellFixture fixture;
    pdf::PDFDocumentToolShell* shell = H::openTool(fixture);
    QVERIFY(shell);
    H::GUI* gui = H::gui(shell);
    QVERIFY(H::waitReady(gui));

    pdf::PDFAppObjectTreeModel* model = gui->getTreeModel();
    const QModelIndex trailer = model->index(0, 0, QModelIndex());
    const QModelIndex root = H::child(model, trailer, QStringLiteral("/Root "));
    const QModelIndex pages = H::child(model, root, QStringLiteral("/Pages "));
    QVERIFY(pages.isValid());
    const QModelIndex kids = H::child(model, pages, QStringLiteral("/Kids"));
    QVERIFY(kids.isValid());
    const QModelIndex page = H::child(model, kids, QStringLiteral("[0]"));
    QVERIFY(page.isValid());
    const QModelIndex parent = H::child(model, page, QStringLiteral("/Parent "));
    QVERIFY(parent.isValid());

    const pdf::PDFObjectInspectorNode* node = model->getNode(parent);
    QVERIFY(node->isRepeated);
    QCOMPARE(node->reference, model->getNode(pages)->reference);
    QVERIFY(!model->hasChildren(parent));
    QVERIFY(!model->canFetchMore(parent));
    QCOMPARE(model->data(parent, Qt::DisplayRole).toString(), QStringLiteral("/Parent ") + pdf::PDFObjectInspector::getReferenceText(node->reference));
}

void AppToolObjectInspectorTest::viewsWithCounts()
{
    // 482:15054: every view with its count; a kind the document does not contain is not listed (no shading in
    // the sample), Document and Object List always; the choice of a view rebuilds the tree with its subtitle
    using H = AppToolObjectInspectorTestHelper;
    AppToolShellFixture fixture;
    pdf::PDFDocumentToolShell* shell = H::openTool(fixture);
    QVERIFY(shell);
    H::GUI* gui = H::gui(shell);
    QVERIFY(H::waitReady(gui));

    QVERIFY(!gui->getViewRow(View::Document)->isHidden());
    QVERIFY(!gui->getViewRow(View::Objects)->isHidden());
    QVERIFY(gui->getViewRow(View::Shadings)->isHidden());
    QVERIFY(!gui->getViewRow(View::Pages)->isHidden());
    QCOMPARE(gui->getViewRow(View::Pages)->getDescription(), QStringLiteral("2 objects"));
    QCOMPARE(gui->getViewRow(View::Pages)->getIconName(), QStringLiteral("pages"));
    QCOMPARE(gui->getViewRow(View::Objects)->getIconName(), QStringLiteral("list-ordered"));
    QVERIFY(gui->getViewRow(View::Document)->isChecked());

    gui->getViewRow(View::Pages)->click();
    AppToolShellFixture::settle();
    QCOMPARE(gui->getView(), View::Pages);
    QCOMPARE(gui->getViewButton()->text(), QStringLiteral("Pages"));
    QCOMPARE(gui->getTreeModel()->rowCount(QModelIndex()), 2);
    QCOMPARE(gui->getObjectsHeader()->getSubtitleLabel()->text(), QStringLiteral("2 page objects"));

    gui->setView(View::Objects);
    AppToolShellFixture::settle();
    QVERIFY(gui->getTreeNote()->text().startsWith(QStringLiteral("References are not followed in the object list")));
}

void AppToolObjectInspectorTest::streamDecodedInJob()
{
    // B165: the data of a stream are decoded in a job, never in the GUI thread; "Decoded stream data" (482:15358);
    // the copy of the data is guarded (R7-22 (6)) - a document, which allows copying, copies at once
    using H = AppToolObjectInspectorTestHelper;
    AppToolShellFixture fixture;
    pdf::PDFDocumentToolShell* shell = H::openTool(fixture);
    QVERIFY(shell);
    H::GUI* gui = H::gui(shell);
    QVERIFY(H::waitReady(gui));
    gui->setView(View::ContentStreams);
    AppToolShellFixture::settle();
    QVERIFY(H::waitContents(gui));

    pdf::PDFAppObjectTreeModel* model = gui->getTreeModel();
    QVERIFY(model->rowCount(QModelIndex()) >= 2);
    gui->selectIndex(model->index(1, 0, QModelIndex()));
    QVERIFY(gui->isLoadingContents());
    QCOMPARE(gui->getTabs().front().kind, H::GUI::ContentKind::Loading);
    QVERIFY(H::waitContents(gui));
    AppToolShellFixture::settle();

    QCOMPARE(gui->getTabs().front().kind, H::GUI::ContentKind::Text);
    QCOMPARE(gui->getContentsCaption()->text(), QStringLiteral("Decoded stream data"));
    QVERIFY(gui->getContentsView()->toPlainText().contains(QStringLiteral("Tj")));
    QVERIFY(gui->getContentsView()->isCopyGuarded());
    QCOMPARE(H::values(gui->getInformationRows()).last(), QStringLiteral("Type=Stream"));

    QApplication::clipboard()->clear();
    gui->getContentsView()->selectAll();
    gui->copyContents();
    AppToolShellFixture::settle();
    QCOMPARE(QApplication::clipboard()->text(), gui->getContentsView()->toPlainText());
}

void AppToolObjectInspectorTest::copyOfStreamAsksPermission()
{
    // R7-22 (6), Q7.2: viewing is reading the document, the copy of the data of a stream is extraction - a document
    // without bit 5 (CopyContent of Table 22) offers the owner password and copies nothing; the syntax of a
    // dictionary is no data of a stream and copies at once
    using H = AppToolObjectInspectorTestHelper;
    AppToolShellFixture fixture;
    const QString id = QString::fromLatin1(pdf::PDFDocumentToolObjectInspector::TOOL_ID);
    pdf::PDFDocumentToolGUIRegistry::unregisterFactory(id);
    pdf::PDFDocumentToolGUIRegistry::registerFactory(id, std::make_shared<pdf::PDFDocumentToolGUIFactoryOf<H::GUI>>());

    pdf::PDFDocumentBuilder builder;
    const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 595, 842));
    pdf::PDFPageContentStreamBuilder content(&builder);
    QPainter* painter = content.begin(page);
    painter->drawText(QPointF(60, 80), QStringLiteral("secret text"));
    content.end(painter);
    pdf::PDFDocument document = builder.build();
    pdf::PDFDocumentBuilder encrypted(&document);
    pdf::PDFSecurityHandlerFactory::SecuritySettings settings;
    settings.algorithm = pdf::PDFSecurityHandlerFactory::AES_256;
    settings.ownerPassword = QStringLiteral("owner-secret");
    settings.permissions = 0xFFFFFFFCu & ~uint32_t(pdf::PDFSecurityHandler::Permission::CopyContent);
    encrypted.setSecurityHandler(pdf::PDFSecurityHandlerFactory::createSecurityHandler(settings));
    document = encrypted.build();
    const QString filePath = fixture.path(QStringLiteral("restricted.pdf"));
    pdf::PDFDocumentWriter writer(nullptr);
    QVERIFY(writer.write(filePath, &document, false));

    pdf::PDFAppDocumentTab* tab = fixture.open(filePath);
    QVERIFY(tab && AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));
    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, id);
    QVERIFY(shell && !shell->isPermissionRefused());
    H::GUI* gui = H::gui(shell);
    QVERIFY(H::waitReady(gui));

    // The syntax of the trailer copies at once
    QApplication::clipboard()->clear();
    gui->copyContents();
    AppToolShellFixture::settle();
    QVERIFY(QApplication::clipboard()->text().startsWith(QStringLiteral("<<")));

    gui->setView(View::ContentStreams);
    AppToolShellFixture::settle();
    gui->selectIndex(gui->getTreeModel()->index(0, 0, QModelIndex()));
    QVERIFY(H::waitContents(gui));
    AppToolShellFixture::settle();
    QVERIFY(gui->getContentsView()->isCopyGuarded());
    QVERIFY(gui->getContentsView()->toPlainText().contains(QStringLiteral("Tj")));

    QApplication::clipboard()->setText(QStringLiteral("before"));
    gui->copyContents();
    pdf::PDFAppPasswordDialog* dialog = AppToolShellFixture::waitForWidget<pdf::PDFAppPasswordDialog>();
    QVERIFY(dialog);
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("before"));
    dialog->reject();
    AppToolShellFixture::settle();
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("before"));
}

void AppToolObjectInspectorTest::guardedContentsGiveNoDragData()
{
    // R7-22 (6), Q7.2: the data of a stream leave the view only by the guarded copy (bit 5 of Table 22) - a drag of
    // the selection and the selection clipboard of X11 (both createMimeDataFromSelection of Qt) carry no text of a
    // guarded view; the syntax of an object (not guarded) is dragged as it is
    AppToolObjectInspectorContentsProbe view;
    view.setPlainText(QStringLiteral("BT /F1 12 Tf (secret text) Tj ET"));
    view.selectAll();
    QCOMPARE(view.getDragText(), QStringLiteral("BT /F1 12 Tf (secret text) Tj ET"));

    view.setCopyGuarded(true);
    QVERIFY(view.textCursor().hasSelection());
    QVERIFY(view.getDragText().isEmpty());

    view.setCopyGuarded(false);
    QCOMPARE(view.getDragText(), QStringLiteral("BT /F1 12 Tf (secret text) Tj ET"));
}

void AppToolObjectInspectorTest::contentsPaddingAndLines()
{
    // RS7FIGMA, 482:14847: the contents of a tab have the padding 16 on every side and lines of 19 (the plain text
    // editor of the design has 10 and 18)
    pdf::PDFAppObjectContentsView view;
    view.resize(400, 300);
    view.setPlainText(QStringLiteral("<<\n/Type /Catalog\n>>"));
    QCOMPARE(view.getLineHeight(), 19.0);
    QCOMPARE(view.document()->documentMargin(), 16.0);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QCOMPARE(view.viewport()->geometry().left(), view.contentsRect().left());

    const QTextBlock first = view.document()->firstBlock();
    const QTextBlock second = first.next();
    QVERIFY(first.isValid() && second.isValid());
    QAbstractTextDocumentLayout* layout = view.document()->documentLayout();
    QCOMPARE(layout->blockBoundingRect(first).height(), 19.0);
    QCOMPARE(view.cursorRect(QTextCursor(second)).top() - view.cursorRect(QTextCursor(first)).top(), 19);
    QVERIFY(view.cursorRect(QTextCursor(first)).top() >= 16);

    // The editor of the design keeps its lines
    pdf::PDFPlainTextEdit editor;
    QCOMPARE(editor.getLineHeight(), 18.0);
}

void AppToolObjectInspectorTest::largeNodeFetchedInParts()
{
    // RS7APP: an untrusted file shown raw never hangs nor exhausts the memory - an array of many items (a hostile
    // file can have millions) is opened in parts of CHILD_BATCH, never whole in the GUI thread; the next part comes,
    // when the last loaded item is shown, the items keep their indexes [n] of the array
    using Model = pdf::PDFAppObjectTreeModel;
    constexpr int ITEM_COUNT = 2 * Model::CHILD_BATCH + Model::CHILD_BATCH / 2;

    pdf::PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 595, 842));
    pdf::PDFObjectFactory factory;
    factory.beginArray();
    for (int i = 0; i < ITEM_COUNT; ++i)
    {
        factory << pdf::PDFInteger(i);
    }
    factory.endArray();
    const pdf::PDFObjectReference arrayReference = builder.addObject(factory.takeObject());
    const pdf::PDFDocumentPointer document(new pdf::PDFDocument(builder.build()));
    std::shared_ptr<pdf::PDFObjectInspector> inspector = std::make_shared<pdf::PDFObjectInspector>(document);
    QVERIFY(inspector->prepare(nullptr));

    // The Object List: the array is a root of its own
    Model model;
    model.setInspector(inspector, View::Objects);
    const QModelIndex array = model.findReference(arrayReference);
    QVERIFY(array.isValid());
    QVERIFY(model.hasChildren(array));
    QCOMPARE(model.rowCount(array), 0);
    QVERIFY(model.canFetchMore(array));

    // The first part only
    model.fetchMore(array);
    QCOMPARE(model.rowCount(array), Model::CHILD_BATCH);
    QVERIFY(model.canFetchMore(array));
    QVERIFY(model.hasChildren(array));

    // The last loaded item shown: the next part from the event loop
    const QModelIndex lastOfFirst = model.index(Model::CHILD_BATCH - 1, 0, array);
    QVERIFY(model.data(lastOfFirst, Qt::DisplayRole).toString().startsWith(QStringLiteral("[%1]").arg(Model::CHILD_BATCH - 1)));
    QCOMPARE(model.rowCount(array), Model::CHILD_BATCH);
    QTRY_COMPARE(model.rowCount(array), 2 * Model::CHILD_BATCH);
    const pdf::PDFObjectInspectorNode* firstOfSecond = model.getNode(model.index(Model::CHILD_BATCH, 0, array));
    QVERIFY(firstOfSecond);
    QCOMPARE(firstOfSecond->arrayIndex, pdf::PDFInteger(Model::CHILD_BATCH));
    QCOMPARE(firstOfSecond->object.getInteger(), pdf::PDFInteger(Model::CHILD_BATCH));

    // The rest by the view (fetchMore): the last part is shorter, then nothing more
    model.fetchMore(array);
    QCOMPARE(model.rowCount(array), ITEM_COUNT);
    QVERIFY(!model.canFetchMore(array));
    QVERIFY(model.hasChildren(array));
    const pdf::PDFObjectInspectorNode* last = model.getNode(model.index(ITEM_COUNT - 1, 0, array));
    QVERIFY(last);
    QCOMPARE(last->arrayIndex, pdf::PDFInteger(ITEM_COUNT - 1));

    // A reset drops a queued part (its node is gone)
    QVERIFY(model.data(model.index(ITEM_COUNT - 1, 0, array), Qt::DisplayRole).isValid());
    model.setInspector(inspector, View::Objects);
    QCoreApplication::processEvents();
    QCOMPARE(model.rowCount(model.findReference(arrayReference)), 0);
}

void AppToolObjectInspectorTest::pinAndUnpin()
{
    // 482:15778: Pin keeps the object in its own tab, Current Object stays active; 482:15989: on a pinned tab Pin is
    // Disabled, Unpin closes it; the pinned tab is a snapshot - it does not follow the tree
    using H = AppToolObjectInspectorTestHelper;
    AppToolShellFixture fixture;
    pdf::PDFDocumentToolShell* shell = H::openTool(fixture);
    QVERIFY(shell);
    H::GUI* gui = H::gui(shell);
    QVERIFY(H::waitReady(gui));
    gui->setView(View::Pages);
    AppToolShellFixture::settle();
    pdf::PDFAppObjectTreeModel* model = gui->getTreeModel();
    gui->selectIndex(model->index(0, 0, QModelIndex()));
    AppToolShellFixture::settle();

    QVERIFY(gui->getPinButton()->isEnabled());
    QVERIFY(!gui->getUnpinButton()->isEnabled());
    const pdf::PDFObjectReference first = gui->getTabs().front().node.reference;
    QVERIFY(gui->pin());
    AppToolShellFixture::settle();
    QCOMPARE(gui->getTabs().size(), size_t(2));
    QCOMPARE(gui->getActiveTab(), 0);
    const std::vector<QToolButton*> buttons = gui->getTabButtons();
    QCOMPARE(buttons.size(), size_t(2));
    QCOMPARE(buttons[0]->text(), QStringLiteral("Current Object"));
    QCOMPARE(buttons[1]->text(), pdf::PDFObjectInspector::getReferenceText(first));
    QCOMPARE(gui->getStatusText(), pdf::PDFObjectInspector::getReferenceText(first) + QStringLiteral(" · 1 pinned"));

    // The tree moves on, the pinned tab stays
    gui->selectIndex(model->index(1, 0, QModelIndex()));
    AppToolShellFixture::settle();
    QVERIFY(gui->getTabs().front().node.reference != first);
    QCOMPARE(gui->getTabs()[1].node.reference, first);

    gui->setActiveTab(1);
    AppToolShellFixture::settle();
    QVERIFY(!gui->getPinButton()->isEnabled());
    QVERIFY(gui->getUnpinButton()->isEnabled());
    QCOMPARE(gui->getTabNote()->text(), QStringLiteral("A pinned tab is a snapshot and does not follow the tree. Unpin closes it."));
    QCOMPARE(gui->getStatusText(), QStringLiteral("Pinned: object %1.").arg(pdf::PDFObjectInspector::getReferenceText(first)));

    gui->getUnpinButton()->click();
    AppToolShellFixture::settle();
    QCOMPARE(gui->getTabs().size(), size_t(1));
    QCOMPARE(gui->getActiveTab(), 0);
    QVERIFY(!gui->getUnpinButton()->isEnabled());
}

void AppToolObjectInspectorTest::selectionRestoredAfterChange()
{
    // Plan 4.3: after a change of the document the tree is built again and the selection restored by its
    // reference; a pinned tab stays
    using H = AppToolObjectInspectorTestHelper;
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    pdf::PDFDocumentToolShell* shell = H::openTool(fixture, &tab);
    QVERIFY(shell);
    H::GUI* gui = H::gui(shell);
    QVERIFY(H::waitReady(gui));
    gui->setView(View::Pages);
    AppToolShellFixture::settle();
    gui->selectIndex(gui->getTreeModel()->index(1, 0, QModelIndex()));
    AppToolShellFixture::settle();
    const pdf::PDFObjectReference selected = gui->getTabs().front().node.reference;
    QVERIFY(selected.isValid());
    QVERIFY(gui->pin());

    pdf::PDFAppDocument* document = tab->getDocument();
    QVERIFY(document->commit(pdf::PDFModifiedDocument(document->getDocument(), nullptr, pdf::PDFModifiedDocument::Annotation), QStringLiteral("Assistant"),
                             document->getVersion(), pdf::PDFAppChangeSource::Mcp));
    QVERIFY(!gui->isReady());
    QVERIFY(H::waitReady(gui));
    AppToolShellFixture::settle();

    QCOMPARE(gui->getTabs().front().node.reference, selected);
    QCOMPARE(gui->getTreeView()->currentIndex().row(), 1);
    QCOMPARE(gui->getTabs().size(), size_t(2));
}

void AppToolObjectInspectorTest::pinnedTabKeepsItsVersion()
{
    // 482:15989 "A pinned tab is a snapshot": a pinned tab, whose stream is not decoded yet (pinned while it was
    // loading), decodes the stream of its own version after a change of the document - also while the inspector of
    // the new version is prepared (RS7APP L6: it stayed "Loading", then it showed the data of the new version)
    using H = AppToolObjectInspectorTestHelper;
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    pdf::PDFDocumentToolShell* shell = H::openTool(fixture, &tab);
    QVERIFY(shell);
    H::GUI* gui = H::gui(shell);
    QVERIFY(H::waitReady(gui));
    gui->setView(View::ContentStreams);
    AppToolShellFixture::settle();
    QVERIFY(H::waitContents(gui));

    // The stream chosen and pinned at once: the pinned copy is still loading, the job decodes Current Object
    gui->selectIndex(gui->getTreeModel()->index(1, 0, QModelIndex()));
    QCOMPARE(gui->getTabs().front().kind, H::GUI::ContentKind::Loading);
    QVERIFY(gui->pin());
    QCOMPARE(gui->getTabs().size(), size_t(2));
    QCOMPARE(gui->getTabs()[1].kind, H::GUI::ContentKind::Loading);
    QVERIFY(H::waitContents(gui));
    AppToolShellFixture::settle();
    const QString oldText = gui->getTabs().front().text;
    QVERIFY(oldText.contains(QStringLiteral("Tj")));
    QCOMPARE(gui->getTabs()[1].kind, H::GUI::ContentKind::Loading);
    const pdf::PDFObjectReference reference = gui->getTabs()[1].node.reference;
    QVERIFY(reference.isValid());

    // A new version: the stream has another content
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFDocumentBuilder builder(document->getDocument().data());
    const QByteArray newContent("0 0 m 100 100 l S");
    pdf::PDFDictionaryBuilder dictionary;
    dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Length"), pdf::PDFObject::createInteger(newContent.size()));
    builder.setObject(reference, pdf::PDFObject::createStream(pdf::PDFStream(std::move(dictionary), QByteArray(newContent))));
    const pdf::PDFDocumentPointer changed(new pdf::PDFDocument(builder.build()));
    QVERIFY(document->commit(pdf::PDFModifiedDocument(changed, nullptr, pdf::PDFModifiedDocument::Reset), QStringLiteral("Assistant"),
                             document->getVersion(), pdf::PDFAppChangeSource::Mcp));
    QVERIFY(gui->isPreparing() || !gui->isReady());

    // The pinned tab shown at once decodes its own version (the new inspector is not needed)
    gui->setActiveTab(1);
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getTabs().size() == 2 && gui->getTabs()[1].kind != H::GUI::ContentKind::Loading; }));
    QCOMPARE(gui->getTabs()[1].kind, H::GUI::ContentKind::Text);
    QCOMPARE(gui->getTabs()[1].text, oldText);
    QVERIFY(H::waitReady(gui));
    AppToolShellFixture::settle();
    QCOMPARE(gui->getTabs()[1].text, oldText);
    QVERIFY(!gui->getTabs()[1].text.contains(QString::fromLatin1(newContent)));
}

void AppToolObjectInspectorTest::screenshots()
{
    // PDF4QT_APP_SCREENSHOTS=<folder> (review RS7FIGMA): the states of Object Inspector under the ids of their
    // frames (482:14847 … 482:16200) in both themes, at 1x / 2x and right-to-left, for the comparison with Figma;
    // no pixel of the leak color in any of them (Fluent)
    if (qEnvironmentVariableIsEmpty("PDF4QT_APP_SCREENSHOTS"))
    {
        QSKIP("PDF4QT_APP_SCREENSHOTS is not set");
    }

    using H = AppToolObjectInspectorTestHelper;
    for (const char* variant : { "light", "dark", "rtl" })
    {
        const QString variantName = QString::fromLatin1(variant);
        const QString suffix = variantName == QStringLiteral("light") ? QString() : QStringLiteral("-") + variantName;
        QApplication::setLayoutDirection(variantName == QStringLiteral("rtl") ? Qt::RightToLeft : Qt::LeftToRight);
        const QScopeGuard guard([]() { QApplication::setLayoutDirection(Qt::LeftToRight); pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light); });

        AppToolShellFixture fixture(QSize(1440, 960));
        pdf::PDFThemeManager::getInstance()->setScheme(variantName == QStringLiteral("dark") ? pdf::PDFThemeColors::Scheme::Dark : pdf::PDFThemeColors::Scheme::Light);
        AppToolShellFixture::settle();
        QStringList leaks;
        const auto shot = [&fixture, &suffix, &leaks](const QString& frame)
        {
            AppToolShellFixture::settle();
            if (AppToolShellFixture::countLeakPixels(fixture.screenshot(frame + suffix)) > 0)
            {
                leaks << frame + suffix;
            }
        };

        pdf::PDFDocumentToolShell* shell = H::openTool(fixture);
        QVERIFY(shell);
        H::GUI* gui = H::gui(shell);
        QVERIFY(H::waitReady(gui));
        pdf::PDFAppObjectTreeModel* model = gui->getTreeModel();
        const QModelIndex trailer = model->index(0, 0, QModelIndex());
        gui->getTreeView()->expand(trailer);
        const QModelIndex root = H::child(model, trailer, QStringLiteral("/Root "));
        gui->getTreeView()->expand(root);
        gui->selectIndex(root);
        shot(QStringLiteral("482-14847"));

        QTest::mouseClick(gui->getViewButton(), Qt::LeftButton);
        shot(QStringLiteral("482-15054"));
        if (QWidget* popup = QApplication::activePopupWidget())
        {
            popup->close();
        }
        AppToolShellFixture::settle();

        gui->setView(View::ContentStreams);
        AppToolShellFixture::settle();
        gui->selectIndex(gui->getTreeModel()->index(0, 0, QModelIndex()));
        QVERIFY(H::waitContents(gui));
        shot(QStringLiteral("482-15358"));

        QVERIFY(gui->pin());
        shot(QStringLiteral("482-15778"));
        gui->setActiveTab(1);
        shot(QStringLiteral("482-15989"));
        gui->setActiveTab(0);

        gui->setView(View::Objects);
        AppToolShellFixture::settle();
        gui->selectIndex(gui->getTreeModel()->index(0, 0, QModelIndex()));
        QVERIFY(H::waitContents(gui));
        shot(QStringLiteral("482-16200"));

        fixture.closeMessageBoxes();
        QVERIFY2(leaks.isEmpty(), qPrintable(leaks.join(QStringLiteral(", "))));
    }
}

QTEST_MAIN(AppToolObjectInspectorTest)

#include "tst_apptoolobjectinspectortest.moc"
