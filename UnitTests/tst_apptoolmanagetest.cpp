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

// The GUI of Manage Pages (step 3b of phase 2, package M3; plan chapter 2, 8, RB2, RB3, QB1, QB7).
// The skeleton tests its shared hooks: the text of an undo step goes with it to Redo and back and
// makes the tooltip "Undo Group" (387:12569, RB3 d); the permissions follow the output of the run
// (RB3 a); the own tab stays after a result and its Cancel is Close (K31 396:27563, K2). Package M3
// tests the frames of chapter 2 in both hosts: the sources, the labels and captions of the tiles,
// the operations with their undo steps, the clipboard of one assembly (B18), the close question
// 405:30894, the assembly file, the document host 1479:34632 / 1479:35631 and the change of the
// document under the tool (RB2), the texts of the dialogs 387:15926, the themes, 2x and right to
// left without the leak color, and the deletion order of the new widgets.

#include "apptoolowntabfixture.h"
#include "testdocumenttool.h"

#include "pdfappcommand.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentdialogs.h"
#include "pdfappdocumentpanels_common.h"
#include "pdfappdocumentpermissions.h"
#include "pdfappdocumentstatusbar.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfapplocale.h"
#include "pdfappmainwindow.h"
#include "pdfappmanagecanvas.h"
#include "pdfappmanagedialogs.h"
#include "pdfappmanagepanels.h"
#include "pdfapppassworddialog.h"
#include "pdfapptoolgui_insert.h"
#include "pdfapptoolgui_manage.h"
#include "pdfapptoolhost.h"
#include "pdfapptoolpagegrid.h"
#include "pdfapptooltoolbar.h"
#include "pdftoolstartpage.h"
#include "pdfapptoolinputlist.h"
#include "pdfapptoolinputrow.h"
#include "pdfapptoolmergeview.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptooltab.h"
#include "pdfdocumenttoolshell.h"
#include "pdfrecentassemblyrepository.h"

#include "pdfcomponentbadges.h"
#include "pdfcomponentbuttons.h"
#include "pdfcomponentdelegates.h"
#include "pdfcomponentrows.h"
#include "pdfdocumentassemblyfile.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumenttool_manage.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfdocumentwriter.h"
#include "pdfmessagebox.h"
#include "pdfsecurityhandler.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QBuffer>
#include <QClipboard>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHeaderView>
#include <QLayout>
#include <QMimeData>
#include <QPushButton>
#include <QRegularExpression>
#include <QSemaphore>
#include <QStyle>
#include <QThreadPool>
#include <QTimer>
#include <QtTest>

using pdftest::AppDocumentSamples;
using pdftest::AppToolOwnTabFixture;

class AppToolManageTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void undoStepTexts();
    void permissionsByOutput();
    void ownTabHooks();
    void dialogTexts();
    void ownTabSourcesAndTiles();
    void operationsWithUndo();
    void clipboardOfOneAssembly();
    void cutAndPaste();
    void checkpoints();
    void outputPerGroup();
    void closeQuestion();
    void saveAndOpenAssembly();
    void openAssemblyActivatesItsTab();
    void emptyState();
    void documentHost();
    void documentHostLocked();
    void documentChangeUnderTheTool();
    void themesDirectionAndRatios();
    void deletionOrder();
    void sourcesDialog();
    void detailsOrder();
    void largeAssembly();
    void figmaFrames();
    void sourceOrderOfAddition();
    void sourcesDialogDropsLateLocate();
    void undoOfOpenAssemblyKeepsItsFile();
    void largeSelectionOperations();
    void checkpointPageLimit();
    void looseTilesByFigma();
    void itemNameIsTheFileName();
    void documentHostWithoutAssemble();
    void dialogsDimTheWindow();
    void rangeDialogsStartFromSelection();
    void addPagesDialogPreviews();
    void startPageEditInItsTab();
    void figmaTextsOfTheFixWave();
    void renameUnchangedGivesNoName();
    void secondLocateSaysBusy();
    void documentHostStatusBar();
    void detailsRotationText();
    void emptyStateOpenDocumentRow();
};

/// Helpers of the test
class ManageTestHelper
{
public:
    ManageTestHelper() = delete;

    static QString toolId()
    {
        return QString::fromLatin1(pdf::PDFDocumentToolManage::TOOL_ID);
    }

    /// A PNG of the size at 300 dpi
    static QString png(const QString& folder, const QString& name, QSize size)
    {
        QImage image(size, QImage::Format_RGB32);
        image.fill(QColor(200, 220, 240));
        image.setDotsPerMeterX(11811);
        image.setDotsPerMeterY(11811);
        const QString path = folder + QLatin1Char('/') + name;
        return image.save(path, "PNG") ? path : QString();
    }

    /// The own tab of Manage Pages with the files (the inputs of the start page)
    static pdf::PDFAppToolTab* open(AppToolOwnTabFixture& fixture, const QStringList& files)
    {
        pdf::PDFAppToolInputList* list = new pdf::PDFAppToolInputList(nullptr);
        list->addFiles(files);
        pdf::PDFAppToolTab* tab = fixture.createToolTab(toolId(), list);
        AppToolOwnTabFixture::settle();
        return tab;
    }

    static pdf::PDFAppManageToolGUI* gui(const pdf::PDFAppToolTab* tab)
    {
        pdf::PDFDocumentToolShell* shell = AppToolOwnTabFixture::getShell(tab);
        return shell ? qobject_cast<pdf::PDFAppManageToolGUI*>(shell->getGUI()) : nullptr;
    }

    static pdf::PDFAppToolParameterModel* model(const pdf::PDFAppManageToolGUI* gui)
    {
        return gui ? gui->getModel() : nullptr;
    }

    /// The assembly has the pages (the files are read and become sources)
    static bool waitPages(const pdf::PDFAppManageToolGUI* gui, int pageCount)
    {
        return AppToolOwnTabFixture::waitFor([gui, pageCount]()
        {
            qint64 count = 0;
            for (const pdf::PDFDocumentAssembly::Item& item : gui->getAssembly().getItems())
            {
                count += qint64(item.pages.size());
            }
            return count == pageCount;
        });
    }

    /// The labels and the captions of the tiles of the canvas
    static QStringList labels(const pdf::PDFAppManageToolGUI* gui)
    {
        QStringList result;
        const pdf::PDFAppManagePageModel* model = gui->getCanvas()->getPageModel();
        for (int row = 0; row < model->rowCount(); ++row)
        {
            result << model->data(model->index(row, 0), Qt::DisplayRole).toString();
        }
        return result;
    }

    static QString caption(const pdf::PDFAppManageToolGUI* gui, int row)
    {
        const pdf::PDFAppManagePageModel* model = gui->getCanvas()->getPageModel();
        return model->data(model->index(row, 0), pdf::PDFComponentRoles::SubtitleRole).toString();
    }

    /// The ids of the items at the indices
    static pdf::PDFDocumentAssembly::Selection items(const pdf::PDFAppManageToolGUI* gui, std::vector<int> indices)
    {
        pdf::PDFDocumentAssembly::Selection selection;
        for (const int index : indices)
        {
            selection.push_back(gui->getAssembly().getItems()[size_t(index)].id);
        }
        return selection;
    }

    /// The value of the row of the Result card by its key
    static QString resultValue(const pdf::PDFAppManageToolGUI* gui, const QString& key)
    {
        for (const auto& [rowKey, value] : gui->getResultPanel()->getContent().resultRows)
        {
            if (rowKey == key)
            {
                return value;
            }
        }
        return QString();
    }

    /// The source rows of the panel
    static std::vector<pdf::PDFAppToolInputRow*> sourceRows(const pdf::PDFAppManageToolGUI* gui)
    {
        return gui->getSourcesPanel()->getSourcesPanel()->getRows();
    }

    /// Writes an assembly file of one PDF source of \p pageCount pages at \p sourcePath (it need not
    /// exist), every page an item; size, time and hash unknown (the file decides nothing by them)
    static bool writeAssembly(const QString& filePath, const QString& sourcePath, pdf::PDFInteger pageCount)
    {
        pdf::PDFDocumentAssembly::Source source;
        source.id = QStringLiteral("s1");
        source.kind = pdf::PDFDocumentAssembly::SourceKind::Pdf;
        source.name = QFileInfo(sourcePath).fileName();
        source.path = QFileInfo(sourcePath).absoluteFilePath();
        source.pageCount = pageCount;
        std::vector<pdf::PDFDocumentAssembly::Item> items;
        for (pdf::PDFInteger page = 0; page < pageCount; ++page)
        {
            pdf::PDFDocumentAssembly::Item item;
            item.id = QStringLiteral("i%1").arg(page + 1);
            pdf::PDFDocumentAssembly::Page assemblyPage;
            assemblyPage.plan.input = 0;
            assemblyPage.plan.pageIndex = page;
            item.pages.push_back(assemblyPage);
            items.push_back(item);
        }
        QString error;
        pdf::PDFDocumentAssemblyFile::Content content;
        content.assembly = pdf::PDFDocumentAssembly::create({ source }, std::move(items), { }, &error);
        if (!error.isEmpty())
        {
            return false;
        }
        QFile file(filePath);
        const QByteArray data = pdf::PDFDocumentAssemblyFile::write(content, QFileInfo(filePath).absolutePath());
        return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
    }

    static QByteArray readFile(const QString& filePath)
    {
        QFile file(filePath);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }
};

using H = ManageTestHelper;

void AppToolManageTest::initTestCase()
{
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
    qRegisterMetaType<pdf::PDFDocumentToolResult>();
}

void AppToolManageTest::undoStepTexts()
{
    // RB3 d: a step with a text ("Group") - Undo names it, after Undo the Redo names it; a step
    // without a text names nothing (the title of the command alone)
    const pdftest::TestDocumentTool tool;
    pdf::PDFAppToolParameterModel model(nullptr, tool.getId(), tool.getParameters(), nullptr);
    QVERIFY(model.getUndoText().isEmpty());
    QVERIFY(model.getRedoText().isEmpty());

    QVERIFY(model.setValues(QVariantMap({ { QStringLiteral("mode"), QStringLiteral("title") } }), QStringLiteral("Group")).isEmpty());
    QCOMPARE(model.getUndoText(), QStringLiteral("Group"));
    QVERIFY(model.getRedoText().isEmpty());
    QCOMPARE(pdf::PDFAppDocumentView::getToolUndoTitle(&model, pdf::PDFAppCommands::undo()), QStringLiteral("Undo Group"));
    QVERIFY(pdf::PDFAppDocumentView::getToolUndoTitle(&model, pdf::PDFAppCommands::redo()).isEmpty());

    model.undo();
    QCOMPARE(model.getValue(QStringLiteral("mode")).toString(), QStringLiteral("count"));
    QVERIFY(model.getUndoText().isEmpty());
    QCOMPARE(model.getRedoText(), QStringLiteral("Group"));
    QCOMPARE(pdf::PDFAppDocumentView::getToolUndoTitle(&model, pdf::PDFAppCommands::redo()), QStringLiteral("Redo Group"));

    model.redo();
    QCOMPARE(model.getValue(QStringLiteral("mode")).toString(), QStringLiteral("title"));
    QCOMPARE(model.getUndoText(), QStringLiteral("Group"));
    QVERIFY(model.getRedoText().isEmpty());

    // A step of the panels without a text: the title of the command, the earlier text stays below
    QVERIFY(model.setValue(QStringLiteral("mode"), QStringLiteral("nothing")).isEmpty());
    QVERIFY(model.getUndoText().isEmpty());
    QVERIFY(pdf::PDFAppDocumentView::getToolUndoTitle(&model, pdf::PDFAppCommands::undo()).isEmpty());
    model.undo();
    QCOMPARE(model.getUndoText(), QStringLiteral("Group"));

    // Without a tool nothing is named
    QVERIFY(pdf::PDFAppDocumentView::getToolUndoTitle(nullptr, pdf::PDFAppCommands::undo()).isEmpty());
}

void AppToolManageTest::permissionsByOutput()
{
    // A document opened by the user password, which allows only bit 11 (Assemble, Table 22): This
    // document changes its pages (bit 11 is enough, RB3 a); One PDF would be a new, not encrypted
    // document of it, which would lose the other restrictions - the owner password is needed (Q2 of
    // step 2, PDFDocumentMerger::getRequiredPermissions)
    using Permission = pdf::PDFSecurityHandler::Permission;
    pdf::PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 100, 100));
    pdf::PDFSecurityHandlerFactory::SecuritySettings settings;
    settings.algorithm = pdf::PDFSecurityHandlerFactory::AES_256;
    settings.userPassword = QStringLiteral("user");
    settings.ownerPassword = QStringLiteral("owner");
    settings.permissions = uint32_t(Permission::Assemble);
    builder.setSecurityHandler(pdf::PDFSecurityHandlerFactory::createSecurityHandler(settings));
    pdf::PDFDocument document = builder.build();

    QByteArray data;
    QBuffer buffer(&data);
    QVERIFY(buffer.open(QIODevice::WriteOnly));
    QVERIFY(bool(pdf::PDFDocumentWriter(nullptr).write(&buffer, &document)));
    buffer.close();

    int calls = 0;
    const auto getPassword = [&calls](bool* ok)
    {
        *ok = calls++ == 0;
        return QStringLiteral("user");
    };
    pdf::PDFDocumentReader reader(nullptr, getPassword, false, false);
    const pdf::PDFDocument asUser = reader.readFromBuffer(data);
    QCOMPARE(reader.getReadingResult(), pdf::PDFDocumentReader::Result::OK);

    const QString toolId = QString::fromLatin1(pdf::PDFDocumentToolManage::TOOL_ID);
    const bool wasRegistered = pdf::PDFDocumentToolRegistry::isImplemented(toolId);
    if (!wasRegistered)
    {
        QCOMPARE(pdf::PDFDocumentToolRegistry::registerTool(std::make_shared<pdf::PDFDocumentToolManage>()), pdf::PDFDocumentToolRegistration::Registered);
    }
    const QString outputKey = QString::fromLatin1(pdf::PDFDocumentToolManage::PARAMETER_OUTPUT);
    const QVariantMap thisDocument({ { outputKey, QString::fromLatin1(pdf::PDFDocumentToolManage::OUTPUT_DOCUMENT) } });
    const QVariantMap onePdf({ { outputKey, QString::fromLatin1(pdf::PDFDocumentToolManage::OUTPUT_ONE_PDF) } });
    const QVariantMap perPage({ { outputKey, QString::fromLatin1(pdf::PDFDocumentToolManage::OUTPUT_PER_PAGE) } });

    const bool isThisDocumentAllowed = pdf::PDFAppDocumentPermissions::isAllowed(&asUser, pdf::PDFAppPermissionRequest::createTool(toolId, { }, thisDocument));
    const bool isOnePdfAllowed = pdf::PDFAppDocumentPermissions::isAllowed(&asUser, pdf::PDFAppPermissionRequest::createTool(toolId, { }, onePdf));
    const bool isPerPageAllowed = pdf::PDFAppDocumentPermissions::isAllowed(&asUser, pdf::PDFAppPermissionRequest::createTool(toolId, { }, perPage));
    const bool isCatalogRowAllowed = pdf::PDFAppDocumentPermissions::isAllowed(&asUser, pdf::PDFAppPermissionRequest::createTool(toolId));
    if (!wasRegistered)
    {
        QVERIFY(pdf::PDFDocumentToolRegistry::unregisterTool(toolId));
    }

    QVERIFY(isThisDocumentAllowed);
    QVERIFY(!isOnePdfAllowed);
    QVERIFY(!isPerPageAllowed);
    QVERIFY(isCatalogRowAllowed);
}

void AppToolManageTest::ownTabHooks()
{
    // Outside of the tab of a document (no host yet: the own tab of a start without a document):
    // the tab stays after a result (K31, 396:27563 "The assembly stays open"), Cancel is Close
    // (K2), 1 to 1000 inputs of PDF files and images (plan 1 point 7)
    pdf::PDFAppManageToolGUI gui;
    QVERIFY(!gui.isDocumentHost());
    QVERIFY(gui.isTabKeptAfterResult());
    QCOMPARE(gui.getCancelText(), QStringLiteral("Close"));
    const pdf::PDFAppToolInputRules rules = gui.getInputRules();
    QCOMPARE(rules.minimum, 1);
    QCOMPARE(rules.maximum, 1000);
    QVERIFY(rules.acceptsPdf);
    QVERIFY(rules.acceptsImages);

    // The base GUI keeps every other tool as before: the tab closes, Cancel stays Cancel, the
    // close asks the generic question
    // Outside of the tab of a document the shell refuses at the entry as for every tool (owner
    // answer 1 of 12. 11. only for the tab of a document)
    QVERIFY(!gui.handlesPermissionRefusal());
    pdf::PDFDocumentToolGUI base;
    QVERIFY(!base.handlesPermissionRefusal());
    QVERIFY(!base.isTabKeptAfterResult());
    QVERIFY(base.getCancelText().isEmpty());
    QCOMPARE(base.askOwnTabClose(), pdf::PDFAppToolCloseAnswer::Generic);
}

void AppToolManageTest::dialogTexts()
{
    // 387:15926: "17 selected pages → 3 groups (6, 6 and 5 pages)" and "7, 13" → "3 groups (1–6,
    // 7–12, 13–17)"; a position, which the selection does not have, is an error
    QCOMPARE(pdf::PDFAppManageDialogs::getEveryText(17, 6), QStringLiteral("17 selected pages → 3 groups (6, 6 and 5 pages)"));
    QString error;
    const std::vector<int> positions = pdf::PDFAppManageDialogs::parsePositions(QStringLiteral("7, 13"), 17, &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(positions, std::vector<int>({ 7, 13 }));
    QCOMPARE(pdf::PDFAppManageDialogs::getPositionsText(17, positions), QStringLiteral("17 selected pages → 3 groups (1–6, 7–12, 13–17)"));
    QVERIFY(pdf::PDFAppManageDialogs::parsePositions(QStringLiteral("7, 40"), 17, &error).empty());
    QVERIFY(!error.isEmpty());
    QVERIFY(pdf::PDFAppManageDialogs::parsePositions(QStringLiteral("7, x"), 17, &error).empty());
    QVERIFY(!error.isEmpty());

    // The dialog of Every N: the status follows the field, Regroup says the number
    pdf::PDFAppManageNumberDialog* dialog = new pdf::PDFAppManageNumberDialog(pdf::PDFAppManageNumberDialog::Mode::EveryPages, 17, 6, nullptr, nullptr);
    QPointer<pdf::PDFAppManageNumberDialog> guard(dialog);
    QCOMPARE(dialog->getStatusLabel()->text(), QStringLiteral("17 selected pages → 3 groups (6, 6 and 5 pages)"));
    dialog->getSpinBox()->setValue(9);
    QCOMPARE(dialog->getStatusLabel()->text(), QStringLiteral("17 selected pages → 2 groups (9 and 8 pages)"));
    int chosen = 0;
    QObject::connect(dialog, &pdf::PDFAppManageNumberDialog::numberChosen, dialog, [&chosen](int value) { chosen = value; });
    dialog->open();
    dialog->triggerAccept();
    QCOMPARE(chosen, 9);
    AppToolOwnTabFixture::settle();
    QVERIFY(!guard);
}

void AppToolManageTest::ownTabSourcesAndTiles()
{
    // 387:12569: Contract.pdf 6 pages, Annex_A.pdf 9 pages and signature_page.png - the sources
    // A, B, C, "N of M pages used", "Image · used once"; the tiles "A · 1" (the letter of the
    // source and the position in the assembly, QB5), the image tile "image"; "16 items · 16
    // pages"; the inputs of the start page are no undo step (K21)
    AppToolOwnTabFixture fixture;
    const QString contract = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Contract.pdf"), 6);
    const QString annex = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Annex_A.pdf"), 9);
    const QString image = H::png(fixture.folder(), QStringLiteral("signature_page.png"), QSize(40, 30));
    pdf::PDFAppToolTab* tab = H::open(fixture, { contract, annex, image });
    QVERIFY(tab);
    pdf::PDFAppManageToolGUI* gui = H::gui(tab);
    QVERIFY(gui);
    QVERIFY(H::waitPages(gui, 16));
    AppToolOwnTabFixture::settle();

    QCOMPARE(gui->getAssembly().getSources().size(), size_t(3));
    QCOMPARE(gui->getAssembly().getItems().size(), size_t(16));
    QVERIFY(!H::model(gui)->canUndo());
    const QStringList labels = H::labels(gui);
    QCOMPARE(labels.size(), 16);
    QCOMPARE(labels.at(0), QStringLiteral("A · 1"));
    QCOMPARE(labels.at(6), QStringLiteral("B · 7"));
    QCOMPARE(labels.at(15), QStringLiteral("C · 16"));
    QCOMPARE(H::caption(gui, 15), QStringLiteral("image"));
    QVERIFY(H::caption(gui, 0).isEmpty());
    QCOMPARE(gui->getCanvas()->getHeaderLabel()->text(), QStringLiteral("16 items · 16 pages"));

    const std::vector<pdf::PDFAppToolInputRow*> rows = H::sourceRows(gui);
    QCOMPARE(rows.size(), size_t(3));
    QCOMPARE(rows[0]->getContent().letter, QStringLiteral("A"));
    QCOMPARE(rows[0]->getContent().firstLine, QStringLiteral("6 of 6 pages used"));
    QCOMPARE(rows[1]->getContent().firstLine, QStringLiteral("9 of 9 pages used"));
    QCOMPARE(rows[2]->getContent().firstLine, QStringLiteral("Image · used once"));
    QCOMPARE(gui->getSourcesPanel()->getSourcesPanel()->getSubtitleLabel()->text(), QStringLiteral("3 files · 16 of 16 pages used"));

    // The toolbar and the texts of 387:12569
    QCOMPARE(gui->getConfirmText(), QStringLiteral("Create PDF"));
    QVERIFY(gui->isConfirmEnabled());
    QCOMPARE(gui->getStatusText(), QStringLiteral("Manage Pages · 3 sources · 16 items · 16 pages"));
    QCOMPARE(gui->getAction(QStringLiteral("output"))->text(), QStringLiteral("Output: One PDF"));
    QVERIFY(gui->getAction(QStringLiteral("group")));
    QVERIFY(!gui->getAction(QStringLiteral("openAsAssembly")));
    QCOMPARE(H::resultValue(gui, QStringLiteral("Pages")), QStringLiteral("16"));
    QCOMPARE(H::resultValue(gui, QStringLiteral("Output")), QStringLiteral("One PDF"));

    // Details (387:15072): the ten columns
    const pdf::PDFAppManageItemModel* items = gui->getCanvas()->getItemModel();
    QCOMPARE(items->columnCount(), 10);
    QCOMPARE(items->headerData(pdf::PDFAppManageItemModel::OriginalPageColumn, Qt::Horizontal, Qt::DisplayRole).toString(), QStringLiteral("Orig. page"));
    QCOMPARE(items->data(items->index(15, pdf::PDFAppManageItemModel::TypeColumn), Qt::DisplayRole).toString(), QStringLiteral("Image"));
    QCOMPARE(items->data(items->index(15, pdf::PDFAppManageItemModel::TagsColumn), Qt::DisplayRole).toString(), QStringLiteral("Image"));
    QCOMPARE(items->data(items->index(6, pdf::PDFAppManageItemModel::SourceColumn), Qt::DisplayRole).toString(), QStringLiteral("Annex_A.pdf"));
}

void AppToolManageTest::operationsWithUndo()
{
    // Every operation is one undo step with its text (RB3 d): Group gives the card "Introduction"
    // "Group · 5 pages · Contract.pdf 1–5" (387:12807), a turned page "rotated 90°", a renamed
    // single page its name (387:13050), Remove puts it into the bin ("1 page", "Kept until the
    // assembly is closed", 387:12779), Restore all brings it back
    AppToolOwnTabFixture fixture;
    const QString contract = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Contract.pdf"), 6);
    const QString annex = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Annex_A.pdf"), 4);
    pdf::PDFAppToolTab* tab = H::open(fixture, { contract, annex });
    pdf::PDFAppManageToolGUI* gui = H::gui(tab);
    QVERIFY(gui);
    QVERIFY(H::waitPages(gui, 10));
    pdf::PDFAppToolParameterModel* model = H::model(gui);

    gui->setSelection(H::items(gui, { 0, 1, 2, 3, 4 }));
    gui->group();
    AppToolOwnTabFixture::settle();
    QCOMPARE(gui->getAssembly().getItems().size(), size_t(6));
    QCOMPARE(model->getUndoText(), QStringLiteral("Group"));
    const std::vector<pdf::PDFAppToolGroupedPageModel::Group>& groups = gui->getCanvas()->getPageModel()->getGroups();
    QVERIFY(!groups.empty());
    QCOMPARE(groups.front().rowCount, 5);
    QVERIFY(!groups.front().isPlain);
    QCOMPARE(groups.front().subtitle, QStringLiteral("Group · 5 pages · Contract.pdf 1–5"));

    gui->setSelection(H::items(gui, { 0 }));
    gui->rename(QStringLiteral("Introduction"));
    AppToolOwnTabFixture::settle();
    QCOMPARE(model->getUndoText(), QStringLiteral("Rename"));
    QCOMPARE(gui->getCanvas()->getPageModel()->getGroups().front().title, QStringLiteral("Introduction"));

    // Undo / Redo of the group
    model->undo();
    model->undo();
    AppToolOwnTabFixture::settle();
    QCOMPARE(gui->getAssembly().getItems().size(), size_t(10));
    QCOMPARE(model->getRedoText(), QStringLiteral("Group"));
    model->redo();
    AppToolOwnTabFixture::settle();
    QCOMPARE(gui->getAssembly().getItems().size(), size_t(6));

    // The single page B · 7 (item 2 after the group): rotated, renamed, removed and restored
    gui->setSelection(H::items(gui, { 2 }));
    gui->rotateRight();
    AppToolOwnTabFixture::settle();
    QCOMPARE(model->getUndoText(), QStringLiteral("Rotate right"));
    const std::vector<int> rows = gui->getCanvas()->getPageModel()->getRowsOfItem(gui->getAssembly().getItems()[2].id);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(H::caption(gui, rows.front()), QStringLiteral("rotated 90°"));
    gui->setSelection(H::items(gui, { 2 }));
    gui->rename(QStringLiteral("Site plan"));
    AppToolOwnTabFixture::settle();
    QCOMPARE(H::caption(gui, rows.front()), QStringLiteral("Site plan"));

    const QString removedId = gui->getAssembly().getItems()[2].id;
    gui->setSelection({ removedId });
    gui->removeSelection();
    AppToolOwnTabFixture::settle();
    QCOMPARE(model->getUndoText(), QStringLiteral("Remove"));
    QCOMPARE(gui->getAssembly().getItems().size(), size_t(5));
    pdf::PDFAppManageListRow* bin = gui->getSourcesPanel()->getRemovedRow();
    QVERIFY(bin->isVisibleTo(gui->getSourcesPanel()));
    QCOMPARE(bin->getContent().title, QStringLiteral("1 page"));
    QCOMPARE(bin->getContent().text, QStringLiteral("Kept until the assembly is closed"));
    QCOMPARE(bin->getContent().linkText, QStringLiteral("Restore all"));
    gui->restoreAll();
    AppToolOwnTabFixture::settle();
    QCOMPARE(model->getUndoText(), QStringLiteral("Restore all"));
    QCOMPARE(gui->getAssembly().getItems().size(), size_t(6));
    QCOMPARE(gui->getAssembly().getItems()[2].id, removedId);
    QVERIFY(gui->getAssembly().getRemovedItems().empty());

    // Clone: a copy after the item (RB4: the clone carries the flag)
    gui->setSelection({ removedId });
    gui->cloneSelection();
    AppToolOwnTabFixture::settle();
    QCOMPARE(model->getUndoText(), QStringLiteral("Clone"));
    QCOMPARE(gui->getAssembly().getItems().size(), size_t(7));
    QVERIFY(gui->getAssembly().getItems()[3].pages.front().plan.isClone);
    QCOMPARE(gui->getSelection(), pdf::PDFDocumentAssembly::Selection({ gui->getAssembly().getItems()[3].id }));
}

void AppToolManageTest::clipboardOfOneAssembly()
{
    // 4.6, B18: items are pasted only into the assembly they come from; Copy + Paste clones them
    AppToolOwnTabFixture fixture;
    const QString first = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("first.pdf"), 3);
    const QString second = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("second.pdf"), 2);
    pdf::PDFAppToolTab* firstTab = H::open(fixture, { first });
    pdf::PDFAppManageToolGUI* firstGui = H::gui(firstTab);
    QVERIFY(firstGui);
    QVERIFY(H::waitPages(firstGui, 3));
    pdf::PDFAppToolTab* secondTab = H::open(fixture, { second });
    pdf::PDFAppManageToolGUI* secondGui = H::gui(secondTab);
    QVERIFY(secondGui);
    QVERIFY(H::waitPages(secondGui, 2));

    firstGui->setSelection(H::items(firstGui, { 0 }));
    firstGui->copy();
    const QMimeData* mimeData = QGuiApplication::clipboard()->mimeData();
    QVERIFY(mimeData && mimeData->hasFormat(QString::fromLatin1(pdf::PDFAppManageToolGUI::CLIPBOARD_MIME_TYPE)));

    const pdf::PDFDocumentAssembly before = secondGui->getAssembly();
    secondGui->paste();
    AppToolOwnTabFixture::settle();
    QVERIFY(secondGui->getAssembly() == before);

    firstGui->setSelection(H::items(firstGui, { 2 }));
    firstGui->paste();
    AppToolOwnTabFixture::settle();
    QCOMPARE(firstGui->getAssembly().getItems().size(), size_t(4));
    QCOMPARE(H::model(firstGui)->getUndoText(), QStringLiteral("Paste"));
    // After the selection (B19): the copy of page 1 at the end, a clone
    const pdf::PDFDocumentAssembly::Item& pasted = firstGui->getAssembly().getItems()[3];
    QCOMPARE(pasted.pages.front().plan.pageIndex, pdf::PDFInteger(0));
    QVERIFY(pasted.pages.front().plan.isClone);
}

void AppToolManageTest::cutAndPaste()
{
    // B53: Cut removes into the bin at once; Paste brings the cut item back after the selection
    AppToolOwnTabFixture fixture;
    const QString file = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("cut.pdf"), 4);
    pdf::PDFAppToolTab* tab = H::open(fixture, { file });
    pdf::PDFAppManageToolGUI* gui = H::gui(tab);
    QVERIFY(gui);
    QVERIFY(H::waitPages(gui, 4));

    const QString cutId = gui->getAssembly().getItems()[0].id;
    gui->setSelection({ cutId });
    gui->cut();
    AppToolOwnTabFixture::settle();
    QCOMPARE(gui->getAssembly().getItems().size(), size_t(3));
    QCOMPARE(gui->getAssembly().getRemovedItems().size(), size_t(1));
    QCOMPARE(H::model(gui)->getUndoText(), QStringLiteral("Cut"));

    gui->setSelection(H::items(gui, { 1 }));
    gui->paste();
    AppToolOwnTabFixture::settle();
    QCOMPARE(gui->getAssembly().getItems().size(), size_t(4));
    QVERIFY(gui->getAssembly().getRemovedItems().empty());
    QCOMPARE(gui->getAssembly().getItems()[2].id, cutId);
    QCOMPARE(gui->getAssembly().getItems()[2].pages.front().plan.pageIndex, pdf::PDFInteger(0));
    QVERIFY(!gui->getAssembly().getItems()[2].pages.front().plan.isClone);
}

void AppToolManageTest::checkpoints()
{
    // B23: two checkpoints of one name are both kept (ids), the newest first with the accent tile
    // (387:12757); B22: the restoration is an undo step; saving is none (RB7)
    AppToolOwnTabFixture fixture;
    const QString file = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("points.pdf"), 3);
    pdf::PDFAppToolTab* tab = H::open(fixture, { file });
    pdf::PDFAppManageToolGUI* gui = H::gui(tab);
    QVERIFY(gui);
    QVERIFY(H::waitPages(gui, 3));
    pdf::PDFAppToolParameterModel* model = H::model(gui);

    gui->saveCheckpoint(QStringLiteral("Before"));
    QVERIFY(!model->canUndo());
    gui->setSelection(H::items(gui, { 0 }));
    gui->removeSelection();
    AppToolOwnTabFixture::settle();
    gui->saveCheckpoint(QStringLiteral("Before"));
    QCOMPARE(gui->getCheckpoints().size(), size_t(2));
    QVERIFY(gui->getCheckpoints()[0].id != gui->getCheckpoints()[1].id);

    const std::vector<pdf::PDFAppManageListRow*> rows = gui->getSourcesPanel()->getCheckpointRows();
    QCOMPARE(rows.size(), size_t(2));
    QCOMPARE(rows[0]->getContent().key, gui->getCheckpoints()[1].id);
    QVERIFY(rows[0]->getContent().isAccent);
    QVERIFY(!rows[1]->getContent().isAccent);
    QVERIFY(rows[0]->getContent().text.endsWith(QStringLiteral("2 pages")));
    QVERIFY(rows[1]->getContent().text.endsWith(QStringLiteral("3 pages")));
    QCOMPARE(rows[0]->getContent().linkText, QStringLiteral("Restore"));

    gui->restoreCheckpoint(gui->getCheckpoints()[0].id);
    AppToolOwnTabFixture::settle();
    QCOMPARE(model->getUndoText(), QStringLiteral("Restore checkpoint"));
    QCOMPARE(gui->getAssembly().getItems().size(), size_t(3));
    model->undo();
    AppToolOwnTabFixture::settle();
    QCOMPARE(gui->getAssembly().getItems().size(), size_t(2));
}

void AppToolManageTest::outputPerGroup()
{
    // 396:26763: One PDF per group or item - "Output: One per group", "Create 3 PDFs", the files
    // named by "##_{group_name}" of the items, the Result rows Files "3 · per group"
    AppToolOwnTabFixture fixture;
    const QString file = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("parts.pdf"), 5);
    pdf::PDFAppToolTab* tab = H::open(fixture, { file });
    pdf::PDFAppManageToolGUI* gui = H::gui(tab);
    QVERIFY(gui);
    QVERIFY(H::waitPages(gui, 5));
    gui->setSelection(H::items(gui, { 0, 1, 2 }));
    gui->group();
    AppToolOwnTabFixture::settle();
    gui->setSelection(H::items(gui, { 0 }));
    gui->rename(QStringLiteral("Introduction"));
    AppToolOwnTabFixture::settle();

    gui->setOutput(QString::fromLatin1(pdf::PDFDocumentToolManage::OUTPUT_PER_GROUP));
    AppToolOwnTabFixture::settle();
    QCOMPARE(gui->getAction(QStringLiteral("output"))->text(), QStringLiteral("Output: One per group"));
    QCOMPARE(gui->getConfirmText(), QStringLiteral("Create 3 PDFs"));
    QCOMPARE(H::resultValue(gui, QStringLiteral("Files")), QStringLiteral("3 · per group"));
    QVERIFY(gui->getOutputFiles()->getContainer()->isVisibleTo(gui->getResultPanel()));
    QVERIFY(gui->getOutputFiles()->getPreview()->text().contains(QStringLiteral("01_Introduction.pdf")));

    // This document is never an output of the own tab (QB7 b)
    gui->setOutput(QString::fromLatin1(pdf::PDFDocumentToolManage::OUTPUT_DOCUMENT));
    QCOMPARE(gui->getOutput(), QString::fromLatin1(pdf::PDFDocumentToolManage::OUTPUT_PER_GROUP));
}

void AppToolManageTest::closeQuestion()
{
    // 405:30894: a changed assembly asks "Save the assembly …?" - Don't Save closes, Cancel keeps
    // the tab; a saved assembly closes without a question
    AppToolOwnTabFixture fixture;
    const QString file = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("close.pdf"), 2);
    pdf::PDFAppToolTab* tab = H::open(fixture, { file });
    pdf::PDFAppManageToolGUI* gui = H::gui(tab);
    QVERIFY(gui);
    QVERIFY(H::waitPages(gui, 2));
    QVERIFY(gui->isAssemblyModified());
    QCOMPARE(gui->getOwnTabLossText(), QStringLiteral("The assembly has changes that are not saved."));

    QStringList buttonTexts;
    const auto answer = [gui, &buttonTexts](const QString& buttonName)
    {
        QTimer::singleShot(0, gui, [gui, &buttonTexts, buttonName]()
        {
            pdf::PDFAppDocumentChoiceDialog* dialog = gui->getCloseDialog();
            QVERIFY(dialog);
            buttonTexts.clear();
            for (const pdf::PDFAppDocumentChoice choice : { pdf::PDFAppDocumentChoice::Discard, pdf::PDFAppDocumentChoice::Cancel, pdf::PDFAppDocumentChoice::Save })
            {
                buttonTexts << (dialog->getChoiceButton(choice) ? dialog->getChoiceButton(choice)->text() : QString());
            }
            QPushButton* button = dialog->findChild<QPushButton*>(buttonName);
            QVERIFY(button);
            button->click();
        });
    };
    answer(QStringLiteral("cancelButton"));
    QCOMPARE(gui->askOwnTabClose(), pdf::PDFAppToolCloseAnswer::Cancel);
    QCOMPARE(buttonTexts, QStringList({ QStringLiteral("Don't Save"), QStringLiteral("Cancel"), QStringLiteral("Save Assembly") }));
    answer(QStringLiteral("closeTabButton"));
    QCOMPARE(gui->askOwnTabClose(), pdf::PDFAppToolCloseAnswer::Close);

    // Save Assembly: the save dialog of the system (the handler of the tests) writes the file
    const QString savedPath = fixture.folder() + QStringLiteral("/Saved_on_close.pdf4qtassembly");
    pdf::PDFAppPanelFileDialogs::setSaveHandler([savedPath](QWidget*, const QString&, const QString&, const QString&) { return savedPath; });
    answer(QStringLiteral("manageCloseSave"));
    QCOMPARE(gui->askOwnTabClose(), pdf::PDFAppToolCloseAnswer::Close);
    pdf::PDFAppPanelFileDialogs::setSaveHandler(pdf::PDFAppPanelFileDialogs::FileHandler());
    QVERIFY(QFileInfo::exists(savedPath));
    QVERIFY(!gui->isAssemblyModified());
    gui->setSelection(H::items(gui, { 0 }));
    gui->rotateRight();
    AppToolOwnTabFixture::settle();
    QVERIFY(gui->isAssemblyModified());

    // Saved: nothing is lost
    QString error;
    QVERIFY(gui->saveAssemblyTo(fixture.folder() + QStringLiteral("/Tender_2026.pdf4qtassembly"), &error));
    QVERIFY(!gui->isAssemblyModified());
    QVERIFY(gui->getOwnTabLossText().isEmpty());
    QCOMPARE(gui->askOwnTabClose(), pdf::PDFAppToolCloseAnswer::Close);
    QCOMPARE(gui->getOwnTabTitle(), QStringLiteral("Tender_2026"));
}

void AppToolManageTest::saveAndOpenAssembly()
{
    // 409:32023: Save Assembly writes the file and records it among the recent assemblies; Open
    // Assembly reads it into another tab (B24): the same items and pages, the PDF files read again
    AppToolOwnTabFixture fixture;
    const QString contract = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Contract.pdf"), 3);
    const QString image = H::png(fixture.folder(), QStringLiteral("signature_page.png"), QSize(40, 30));
    pdf::PDFAppToolTab* tab = H::open(fixture, { contract, image });
    pdf::PDFAppManageToolGUI* gui = H::gui(tab);
    QVERIFY(gui);
    QVERIFY(H::waitPages(gui, 4));
    gui->setSelection(H::items(gui, { 0, 1 }));
    gui->group();
    AppToolOwnTabFixture::settle();
    gui->saveCheckpoint(QStringLiteral("Grouped"));

    const QString path = fixture.folder() + QStringLiteral("/Tender_2026.pdf4qtassembly");
    QString error;
    QVERIFY2(gui->saveAssemblyTo(path, &error), qPrintable(error));
    QVERIFY(QFileInfo::exists(path));
    const std::vector<pdf::PDFRecentAssembly> recent = pdf::PDFRecentAssemblyRepository(fixture.context->getDatabase()).getAssemblies();
    QVERIFY(!recent.empty());
    QCOMPARE(QFileInfo(recent.front().path).fileName(), QStringLiteral("Tender_2026.pdf4qtassembly"));

    pdf::PDFAppToolTab* secondTab = H::open(fixture, { });
    pdf::PDFAppManageToolGUI* second = H::gui(secondTab);
    QVERIFY(second);
    QVERIFY(second->openAssemblyFile(path));
    QVERIFY(H::waitPages(second, 4));
    QVERIFY(AppToolOwnTabFixture::waitFor([second]() { return !second->getInputOfSource(second->getAssembly().getSources().front().id).isEmpty(); }));
    QCOMPARE(second->getAssembly().getItems().size(), gui->getAssembly().getItems().size());
    for (size_t i = 0; i < gui->getAssembly().getItems().size(); ++i)
    {
        QCOMPARE(second->getAssembly().getItems()[i].pages.size(), gui->getAssembly().getItems()[i].pages.size());
        QCOMPARE(second->getAssembly().getItems()[i].pages.front().plan.pageIndex, gui->getAssembly().getItems()[i].pages.front().plan.pageIndex);
    }
    QCOMPARE(second->getCheckpoints().size(), size_t(1));
    QCOMPARE(second->getOwnTabTitle(), QStringLiteral("Tender_2026"));
    QVERIFY(!second->isAssemblyModified());
}

void AppToolManageTest::openAssemblyActivatesItsTab()
{
    // RB3 e: an assembly file, which a tab of Manage Pages has open, is not opened a second time
    // (a double click, the command line, the Open dialog, a drop: PDFAppMainWindow::openAssembly) -
    // its tab becomes current. So is a file, which a tab is still loading (two quick double clicks).
    AppToolOwnTabFixture fixture;
    const QString contract = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Contract.pdf"), 3);
    pdf::PDFAppToolTab* tab = H::open(fixture, { contract });
    pdf::PDFAppManageToolGUI* gui = H::gui(tab);
    QVERIFY(gui);
    QVERIFY(H::waitPages(gui, 3));
    const QString otherPath = fixture.folder() + QStringLiteral("/Other.pdf4qtassembly");
    const QString path = fixture.folder() + QStringLiteral("/Tender_2026.pdf4qtassembly");
    QString error;
    QVERIFY2(gui->saveAssemblyTo(otherPath, &error), qPrintable(error));
    QVERIFY2(gui->saveAssemblyTo(path, &error), qPrintable(error));
    QCOMPARE(pdf::PDFAppManageToolGUI::findAssemblyTab(fixture.window.data(), path), tab);
    QVERIFY(pdf::PDFAppManageToolGUI::findAssemblyTab(fixture.window.data(), otherPath) == nullptr);

    // Another tab is current: the open file brings its tab back, no new tab
    pdf::PDFAppToolTab* emptyTab = H::open(fixture, { });
    QCOMPARE(fixture.window->getCurrentTab(), static_cast<pdf::PDFAppTab*>(emptyTab));
    const qsizetype tabCount = fixture.window->getToolTabs().size();
    QVERIFY(fixture.window->openAssembly(path, &error));
    AppToolOwnTabFixture::settle();
    QCOMPARE(fixture.window->getToolTabs().size(), tabCount);
    QCOMPARE(fixture.window->getCurrentTab(), static_cast<pdf::PDFAppTab*>(tab));

    // A file opened twice before its first load has ended: one new tab, current
    QVERIFY(fixture.window->openAssembly(otherPath, &error));
    pdf::PDFAppToolTab* otherTab = qobject_cast<pdf::PDFAppToolTab*>(fixture.window->getCurrentTab());
    QVERIFY(otherTab && otherTab != tab && otherTab != emptyTab);
    QVERIFY(fixture.window->openAssembly(otherPath, &error));
    QCOMPARE(fixture.window->getToolTabs().size(), tabCount + 1);
    QCOMPARE(fixture.window->getCurrentTab(), static_cast<pdf::PDFAppTab*>(otherTab));
    pdf::PDFAppManageToolGUI* other = H::gui(otherTab);
    QVERIFY(other);
    QVERIFY(H::waitPages(other, 3));
    QCOMPARE(QFileInfo(other->getAssemblyPath()), QFileInfo(otherPath));

    // ... and later from another tab
    fixture.window->activateTab(emptyTab);
    QVERIFY(fixture.window->openAssembly(otherPath, &error));
    AppToolOwnTabFixture::settle();
    QCOMPARE(fixture.window->getToolTabs().size(), tabCount + 1);
    QCOMPARE(fixture.window->getCurrentTab(), static_cast<pdf::PDFAppTab*>(otherTab));
}

void AppToolManageTest::emptyState()
{
    // 409:32739: without files the two cards "Start a new assembly" / "Open a saved assembly",
    // "Sources · No files yet", "How Manage Pages works", Create PDF disabled
    AppToolOwnTabFixture fixture;
    pdf::PDFAppToolTab* tab = H::open(fixture, { });
    pdf::PDFAppManageToolGUI* gui = H::gui(tab);
    QVERIFY(gui);
    AppToolOwnTabFixture::settle();
    QVERIFY(gui->getCanvas()->getEmptyState());
    QVERIFY(gui->getCanvas()->getEmptyState()->findChild<QPushButton*>(QStringLiteral("manageEmptyAddFiles")));
    QVERIFY(gui->getCanvas()->getEmptyState()->findChild<QPushButton*>(QStringLiteral("manageEmptyOpenAssembly")));
    QCOMPARE(gui->getSourcesPanel()->getSourcesPanel()->getSubtitleLabel()->text(), QStringLiteral("No files yet"));
    QVERIFY(gui->getResultPanel()->getHowSection()->isVisibleTo(gui->getResultPanel()));
    QVERIFY(!gui->isConfirmEnabled());
    QCOMPARE(gui->getStatusText(), QStringLiteral("Manage Pages · add files to start a new assembly, or open a saved one"));
    QCOMPARE(gui->getOwnTabTitle(), QStringLiteral("New assembly"));
    QCOMPARE(gui->getOwnTabPageText(), QStringLiteral("No pages"));
    QVERIFY(!gui->getAction(QStringLiteral("select"))->isEnabled());
    QVERIFY(gui->getAction(QStringLiteral("add"))->isEnabled());
}

void AppToolManageTest::documentHost()
{
    // QB1 B, 1479:34632: started over a document, the tool runs in its tab - the source A "this
    // document", the tiles "1 · p. 1", only This document ("Rearrange Pages"), no groups, "Open
    // as an Assembly", Cancel; Result Pages "3 → 3", Moved by the longest kept order
    AppToolOwnTabFixture fixture;
    pdf::PDFAppDocumentTab* documentTab = fixture.open(AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Invoice.pdf"), 3));
    QVERIFY(documentTab);
    pdf::PDFDocumentToolShell* shell = fixture.openTool(documentTab, H::toolId());
    QVERIFY(shell);
    pdf::PDFAppManageToolGUI* gui = qobject_cast<pdf::PDFAppManageToolGUI*>(shell->getGUI());
    QVERIFY(gui);
    AppToolOwnTabFixture::settle();
    QVERIFY(gui->isDocumentHost());
    QVERIFY(!gui->isTabKeptAfterResult());
    QVERIFY(gui->getCancelText().isEmpty());
    QCOMPARE(gui->getAssembly().getItems().size(), size_t(3));
    QCOMPARE(H::labels(gui), QStringList({ QStringLiteral("1 · p. 1"), QStringLiteral("2 · p. 2"), QStringLiteral("3 · p. 3") }));
    QCOMPARE(H::sourceRows(gui).front()->getContent().tag, QStringLiteral("this document"));
    QCOMPARE(gui->getConfirmText(), QStringLiteral("Rearrange Pages"));
    QVERIFY(gui->isConfirmEnabled());
    QVERIFY(!gui->getAction(QStringLiteral("group")));
    QVERIFY(!gui->getAction(QStringLiteral("output")));
    QVERIFY(!gui->getAction(QStringLiteral("assembly")));
    QVERIFY(gui->getAction(QStringLiteral("openAsAssembly")));
    QCOMPARE(gui->getOutput(), QString::fromLatin1(pdf::PDFDocumentToolManage::OUTPUT_DOCUMENT));
    QCOMPARE(H::resultValue(gui, QStringLiteral("Pages")), QStringLiteral("3 → 3"));
    QCOMPARE(H::resultValue(gui, QStringLiteral("Output")), QStringLiteral("This document"));
    QVERIFY(gui->getResultPanel()->getInfoCallout()->isVisibleTo(gui->getResultPanel()));
    QCOMPARE(gui->getResultPanel()->getContent().calloutTitle, QStringLiteral("The pages are rearranged in the file"));

    // Page 3 before page 1: one page moved (the others keep their order), labels by the position
    gui->setSelection(H::items(gui, { 2 }));
    gui->moveSelectionBefore(gui->getAssembly().getItems()[0].id);
    AppToolOwnTabFixture::settle();
    QCOMPARE(H::labels(gui).front(), QStringLiteral("1 · p. 3"));
    QCOMPARE(H::resultValue(gui, QStringLiteral("Moved")), QStringLiteral("1 page"));
    QCOMPARE(H::resultValue(gui, QStringLiteral("Removed")), QStringLiteral("0 pages"));

    // Removed into the bin: "Kept until the tool is closed" (KB4)
    gui->setSelection(H::items(gui, { 1 }));
    gui->removeSelection();
    AppToolOwnTabFixture::settle();
    QCOMPARE(gui->getSourcesPanel()->getRemovedRow()->getContent().text, QStringLiteral("Kept until the tool is closed"));
    QCOMPARE(H::resultValue(gui, QStringLiteral("Removed")), QStringLiteral("1 page"));
    QCOMPARE(H::resultValue(gui, QStringLiteral("Pages")), QStringLiteral("3 → 2"));

    // Rearrange Pages: R1 "Pages rearranged"; Discard result ends the tool, the document stays
    const quint64 version = documentTab->getDocument()->getVersion();
    shell->confirm();
    QVERIFY(AppToolOwnTabFixture::waitForState(shell, pdf::PDFDocumentToolShell::State::Deciding));
    QVERIFY(shell->getResultDialog());
    shell->getResultDialog()->reject();
    QVERIFY(AppToolOwnTabFixture::waitFor([documentTab]() { return !AppToolOwnTabFixture::getShell(documentTab); }));
    QCOMPARE(documentTab->getDocument()->getVersion(), version);
}

void AppToolManageTest::documentHostLocked()
{
    // Coordinator decision (1479:35631 for the lock): a locked document keeps the tool, Rearrange
    // Pages is Disabled with "unlock it or open it as an assembly"
    AppToolOwnTabFixture fixture;
    pdf::PDFAppDocumentTab* documentTab = fixture.open(AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Invoice.pdf"), 2));
    QVERIFY(documentTab);
    pdf::PDFDocumentToolShell* shell = fixture.openTool(documentTab, H::toolId());
    QVERIFY(shell);
    pdf::PDFAppManageToolGUI* gui = qobject_cast<pdf::PDFAppManageToolGUI*>(shell->getGUI());
    QVERIFY(gui);
    QVERIFY(gui->isConfirmEnabled());
    documentTab->getDocument()->setLocked(true);
    AppToolOwnTabFixture::settle();
    gui->onDocumentChanged();
    AppToolOwnTabFixture::settle();
    QVERIFY(!gui->isConfirmEnabled());
    QCOMPARE(gui->getResultPanel()->getContent().calloutTitle, QStringLiteral("Document is locked"));
    QVERIFY(gui->getResultPanel()->getLockCallout()->isVisibleTo(gui->getResultPanel()));
    QCOMPARE(gui->getStatusText(), QStringLiteral("Manage Pages · Document is locked – unlock it or open it as an assembly"));
}

void AppToolManageTest::documentChangeUnderTheTool()
{
    // RB2: an assistant adds a page below the open tool - the assembly follows the page objects,
    // the new page is placed at the end with "new in the document", the status says it; the undo
    // steps are made for the new version (Undo keeps the new page)
    AppToolOwnTabFixture fixture;
    pdf::PDFAppDocumentTab* documentTab = fixture.open(AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Invoice.pdf"), 3));
    QVERIFY(documentTab);
    pdf::PDFDocumentToolShell* shell = fixture.openTool(documentTab, H::toolId());
    pdf::PDFAppManageToolGUI* gui = shell ? qobject_cast<pdf::PDFAppManageToolGUI*>(shell->getGUI()) : nullptr;
    QVERIFY(gui);
    AppToolOwnTabFixture::settle();
    gui->setSelection(H::items(gui, { 2 }));
    gui->moveSelectionBefore(gui->getAssembly().getItems()[0].id);
    AppToolOwnTabFixture::settle();
    QCOMPARE(H::labels(gui), QStringList({ QStringLiteral("1 · p. 3"), QStringLiteral("2 · p. 1"), QStringLiteral("3 · p. 2") }));

    pdf::PDFAppDocument* document = documentTab->getDocument();
    pdf::PDFDocumentBuilder builder(document->getDocument().data());
    builder.appendPage(QRectF(0, 0, 200, 200));
    const pdf::PDFDocumentPointer changed(new pdf::PDFDocument(builder.build()));
    QVERIFY(document->commit(pdf::PDFModifiedDocument(changed, nullptr, pdf::PDFModifiedDocument::Reset), QStringLiteral("Assistant"), document->getVersion(), pdf::PDFAppChangeSource::Mcp));
    AppToolOwnTabFixture::settle();
    QVERIFY(AppToolOwnTabFixture::waitFor([gui]() { return gui->getAssembly().getItems().size() == 4; }));

    QCOMPARE(H::labels(gui), QStringList({ QStringLiteral("1 · p. 3"), QStringLiteral("2 · p. 1"), QStringLiteral("3 · p. 2"), QStringLiteral("4 · p. 4") }));
    QCOMPARE(H::caption(gui, 3), QStringLiteral("new in the document"));
    QCOMPARE(gui->getStatusNote(), QStringLiteral("1 page was added to the document and placed at the end"));
    QCOMPARE(gui->getStatusText(), QStringLiteral("Manage Pages · 1 page was added to the document and placed at the end"));
    QCOMPARE(H::resultValue(gui, QStringLiteral("Pages")), QStringLiteral("4 → 4"));

    // Undo of the move: the new page stays (the step was made for the new version)
    gui->getModel()->undo();
    AppToolOwnTabFixture::settle();
    QCOMPARE(H::labels(gui), QStringList({ QStringLiteral("1 · p. 1"), QStringLiteral("2 · p. 2"), QStringLiteral("3 · p. 3"), QStringLiteral("4 · p. 4") }));
}

void AppToolManageTest::themesDirectionAndRatios()
{
    // Both themes without the leak color at 1x and 2x, right to left (the sources at the right
    // edge): the own tab of 387:12569 and the tab of a document of 1479:34632
    for (const pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
    {
        for (const Qt::LayoutDirection direction : { Qt::LeftToRight, Qt::RightToLeft })
        {
            QApplication::setLayoutDirection(direction);
            AppToolOwnTabFixture fixture;
            pdf::PDFThemeManager::getInstance()->setScheme(scheme);
            AppToolOwnTabFixture::settle();
            const QString contract = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Contract.pdf"), 6);
            const QString image = H::png(fixture.folder(), QStringLiteral("signature_page.png"), QSize(40, 30));
            pdf::PDFAppToolTab* tab = H::open(fixture, { contract, image });
            pdf::PDFAppManageToolGUI* gui = H::gui(tab);
            QVERIFY(gui);
            QVERIFY(H::waitPages(gui, 7));
            gui->setSelection(H::items(gui, { 0, 1, 2 }));
            gui->group();
            gui->setSelection(H::items(gui, { 1 }));
            AppToolOwnTabFixture::settle();
            pdf::PDFAppToolTabView* view = tab->getToolPage()->getTabView();
            for (const qreal ratio : { 1.0, 2.0 })
            {
                QCOMPARE(AppToolOwnTabFixture::countLeakPixels(AppToolOwnTabFixture::render(view, ratio)), 0);
            }
            const int sourcesX = gui->getSourcesPanel()->mapTo(view, QPoint(0, 0)).x();
            QCOMPARE(sourcesX < 720, direction == Qt::LeftToRight);

            pdf::PDFAppDocumentTab* documentTab = fixture.open(AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Invoice.pdf"), 3));
            QVERIFY(documentTab);
            pdf::PDFDocumentToolShell* shell = fixture.openTool(documentTab, H::toolId());
            QVERIFY(shell);
            AppToolOwnTabFixture::settle();
            QCOMPARE(AppToolOwnTabFixture::countLeakPixels(AppToolOwnTabFixture::render(documentTab, 1.0)), 0);
        }
    }
    QApplication::setLayoutDirection(Qt::LeftToRight);
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
}

void AppToolManageTest::deletionOrder()
{
    // Every new widget: the parent deleted first (the children go with it) and the children
    // deleted before their parent (no dangling pointer, the QPointer members of the parts)
    {
        QWidget parent;
        pdf::PDFAppManageCanvas* canvas = new pdf::PDFAppManageCanvas(&parent);
        pdf::PDFAppManageSourcesPanel* sources = new pdf::PDFAppManageSourcesPanel(&parent);
        pdf::PDFAppManageResultPanel* result = new pdf::PDFAppManageResultPanel(nullptr, nullptr, &parent);
        pdf::PDFAppManageListRow* row = new pdf::PDFAppManageListRow(&parent);
        sources->setCheckpoints({ { QStringLiteral("c1"), QStringLiteral("flag"), true, QStringLiteral("A"), QStringLiteral("10:42 · 1 page"), QStringLiteral("Restore") } }, true);
        Q_UNUSED(canvas);
        Q_UNUSED(result);
        Q_UNUSED(row);
    }
    {
        QWidget parent;
        QPointer<pdf::PDFAppManageCanvas> canvas = new pdf::PDFAppManageCanvas(&parent);
        QPointer<pdf::PDFAppManageSourcesPanel> sources = new pdf::PDFAppManageSourcesPanel(&parent);
        QPointer<pdf::PDFAppManageResultPanel> result = new pdf::PDFAppManageResultPanel(nullptr, nullptr, &parent);
        canvas->setEmptyState(new QWidget());
        delete canvas->getPageView();
        delete canvas->getDetailsView();
        delete canvas.data();
        delete sources->getSourcesPanel();
        delete sources.data();
        delete result->getSelectionCard();
        delete result.data();
        QVERIFY(!canvas && !sources && !result);
    }
    {
        // A dialog deleted by its parent while it is open, and a dialog deleted before its parent
        QWidget parent;
        QPointer<pdf::PDFAppManageTextDialog> text = new pdf::PDFAppManageTextDialog(pdf::PDFAppManageTextDialog::Mode::PageNumbers, QStringLiteral("Regroup"), QStringLiteral("Regroup"), QStringLiteral("7, 13"), 17, nullptr, &parent);
        text->open();
        QPointer<pdf::PDFAppManageCropDialog> crop = new pdf::PDFAppManageCropDialog(QMarginsF(), nullptr, &parent);
        crop->open();
        delete crop.data();
        QVERIFY(text);
    }
}

void AppToolManageTest::sourcesDialog()
{
    // 387:16980: Annex_A.pdf "Not found in C:\Tender" (Locate…), Contract.pdf "Changed on 2 Oct 2026 –
    // now 5 pages instead of 6" · "Page 6 is no longer available" (Use Changed File); Open Assembly is
    // Enabled only when every source can be used, Open Without Them always
    std::vector<pdf::PDFDocumentAssembly::Source> sources(2);
    sources[0].id = QStringLiteral("s1");
    sources[0].name = QStringLiteral("Annex_A.pdf");
    sources[0].pageCount = 9;
    sources[1].id = QStringLiteral("s2");
    sources[1].name = QStringLiteral("Contract.pdf");
    sources[1].pageCount = 6;
    std::vector<pdf::PDFDocumentAssembly::Item> items;
    for (int input = 0; input < 2; ++input)
    {
        for (pdf::PDFInteger page = 0; page < sources[size_t(input)].pageCount; ++page)
        {
            pdf::PDFDocumentAssembly::Item item;
            item.id = QStringLiteral("i%1-%2").arg(input).arg(page);
            pdf::PDFDocumentAssembly::Page assemblyPage;
            assemblyPage.plan.input = input;
            assemblyPage.plan.pageIndex = page;
            item.pages.push_back(assemblyPage);
            items.push_back(item);
        }
    }
    QString error;
    pdf::PDFAppAssemblyLoadResult result;
    result.filePath = QDir::temp().filePath(QStringLiteral("Tender_2026.pdf4qtassembly"));
    result.content = pdf::PDFDocumentAssemblyFile::Content();
    result.content->assembly = pdf::PDFDocumentAssembly::create(sources, items, { }, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    pdf::PDFAppAssemblySourceStatus missing;
    missing.sourceId = QStringLiteral("s1");
    missing.state = pdf::PDFAppAssemblySourceState::Missing;
    const QString missingFolder = QDir::rootPath() + QStringLiteral("Tender");     // absolute on every system
    missing.filePath = missingFolder + QStringLiteral("/Tender_2026.pdf4qtassembly");
    pdf::PDFAppAssemblySourceStatus changed;
    changed.sourceId = QStringLiteral("s2");
    changed.state = pdf::PDFAppAssemblySourceState::Changed;
    changed.pageCount = 5;
    changed.modified = QDateTime(QDate(2026, 10, 2), QTime(12, 0), QTimeZone::UTC);
    result.sources = { missing, changed };

    const pdf::PDFDocumentAssembly& assembly = result.content->assembly;
    QCOMPARE(pdf::PDFAppManageSourcesDialog::getStateText(missing, assembly), QStringLiteral("Not found in %1").arg(QDir::toNativeSeparators(missingFolder)));
    QCOMPARE(pdf::PDFAppManageSourcesDialog::getMeaningText(missing, assembly), QStringLiteral("9 pages of the assembly use it"));
    QCOMPARE(pdf::PDFAppManageSourcesDialog::getStateText(changed, assembly), QStringLiteral("Changed on %1 – now 5 pages instead of 6").arg(pdf::PDFAppLocale::formatDate(changed.modified.toLocalTime().date())));
    QCOMPARE(pdf::PDFAppManageSourcesDialog::getMeaningText(changed, assembly), QStringLiteral("Page 6 is no longer available"));

    pdf::PDFAppManageSourcesDialog* dialog = new pdf::PDFAppManageSourcesDialog(result, nullptr, nullptr);
    QPointer<pdf::PDFAppManageSourcesDialog> guard(dialog);
    QVERIFY(!dialog->getOpenButton()->isEnabled());
    QVERIFY(dialog->getWithoutButton()->isEnabled());
    QVERIFY(dialog->getSourceButton(QStringLiteral("s1"), QStringLiteral("locate")));
    QVERIFY(!dialog->getSourceButton(QStringLiteral("s1"), QStringLiteral("openFromThere")));
    QPushButton* useChanged = dialog->getSourceButton(QStringLiteral("s2"), QStringLiteral("useChanged"));
    QVERIFY(useChanged);
    useChanged->click();
    QVERIFY(dialog->getResult().sources[1].isAccepted);
    QVERIFY(!dialog->getOpenButton()->isEnabled());

    // The located file of the missing source makes it usable: Open Assembly is Enabled
    pdf::PDFAppAssemblyLoadResult located = dialog->getResult();
    located.sources[0].state = pdf::PDFAppAssemblySourceState::Found;
    located.sources[1].isAccepted = false;
    dialog->setResult(located);
    QVERIFY(dialog->getResult().sources[1].isAccepted);
    QVERIFY(dialog->getOpenButton()->isEnabled());

    // A source of another computer offers Open From There (RB8)
    located.sources[0].state = pdf::PDFAppAssemblySourceState::OnAnotherComputer;
    located.sources[0].filePath = QStringLiteral("//server/share/Annex_A.pdf");
    dialog->setResult(located);
    QVERIFY(dialog->getSourceButton(QStringLiteral("s1"), QStringLiteral("openFromThere")));
    delete dialog;
    QVERIFY(!guard);
}

void AppToolManageTest::detailsOrder()
{
    // 387:15072: Details shows the order of the assembly until a heading is clicked; a heading
    // sorts only this view (the assembly is not changed)
    AppToolOwnTabFixture fixture;
    const QString b = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("b.pdf"), 2);
    const QString a = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 1);
    pdf::PDFAppToolTab* tab = H::open(fixture, { b, a });
    pdf::PDFAppManageToolGUI* gui = H::gui(tab);
    QVERIFY(gui);
    QVERIFY(H::waitPages(gui, 3));
    pdf::PDFAppManageItemModel* items = gui->getCanvas()->getItemModel();
    for (int row = 0; row < 3; ++row)
    {
        QCOMPARE(items->getItemId(row), gui->getAssembly().getItems()[size_t(row)].id);
    }
    const pdf::PDFDocumentAssembly before = gui->getAssembly();
    items->sort(pdf::PDFAppManageItemModel::SourceColumn, Qt::AscendingOrder);
    QCOMPARE(items->data(items->index(0, pdf::PDFAppManageItemModel::SourceColumn), Qt::DisplayRole).toString(), QStringLiteral("a.pdf"));
    QVERIFY(gui->getAssembly() == before);
    items->sort(-1, Qt::AscendingOrder);
    QCOMPARE(items->getItemId(0), gui->getAssembly().getItems()[0].id);
}

void AppToolManageTest::largeAssembly()
{
    // Plan 6: 10 000 pages - Select all, the panels and the views follow in one pass of each
    // (no work of the selected count times the item count), Remove and its Undo, under the limit
    AppToolOwnTabFixture fixture;
    const QString file = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("large.pdf"), 10000);
    pdf::PDFAppToolTab* tab = H::open(fixture, { file });
    pdf::PDFAppManageToolGUI* gui = H::gui(tab);
    QVERIFY(gui);
    QVERIFY(H::waitPages(gui, 10000));
    AppToolOwnTabFixture::settle();

    QElapsedTimer timer;
    timer.start();
    gui->selectAll();
    QCOMPARE(gui->getSelection().size(), size_t(10000));
    QCOMPARE(gui->getResultPanel()->getContent().selectionTag, QStringLiteral("10,000 pages"));
    gui->removeSelection();
    AppToolOwnTabFixture::settle();
    QVERIFY(gui->getAssembly().isEmpty());

    // The bin of 10 000 items: its rows only when it is expanded, at most 100 of them
    QVERIFY(gui->getSourcesPanel()->getRemovedItemRows().empty());
    gui->getSourcesPanel()->getRemovedRow()->setFocus();
    QTest::mouseClick(gui->getSourcesPanel()->getRemovedRow(), Qt::LeftButton, Qt::NoModifier, QPoint(4, 4));
    AppToolOwnTabFixture::settle();
    QCOMPARE(gui->getSourcesPanel()->getRemovedItemRows().size(), size_t(pdf::PDFAppManageToolGUI::MAXIMUM_REMOVED_ROWS));
    QCOMPARE(gui->getSourcesPanel()->getRemovedRow()->getContent().title, QStringLiteral("10,000 pages"));
    gui->getModel()->undo();
    AppToolOwnTabFixture::settle();
    QCOMPARE(gui->getAssembly().getItems().size(), size_t(10000));
    QVERIFY2(timer.elapsed() < 10000 * qint64(AppToolOwnTabFixture::getTimeoutFactor()), qPrintable(QString::number(timer.elapsed())));
}

void AppToolManageTest::figmaFrames()
{
    // RS3BFIGMA: the measures of the frames of Manage Pages (both themes, right to left, 1x / 2x;
    // PDF4QT_APP_SCREENSHOTS writes the screens under the names of the frames):
    //  - 387:12729: a source is one line, 275 x 46, the rows 10 apart (56); with the Tag "this
    //    document" 51 (1479:34681);
    //  - 387:12669: the assembly button is 50 wide - the icon and the chevron of its menu;
    //  - 387:13288 / 387:14220 / 396:26763 / 409:32023: the panels 270, 250, 290, 250, 270, 320, 320
    //    wide; the icons of Group (icon/interleave Odd and even, icon/page-pair Page pairs,
    //    icon/repeat Alternate two files), Add (icon/page-insert Pages of a PDF…, icon/tab of an
    //    open document) and the assembly (icon/grid of a recent assembly);
    //  - 387:15072: every column of Details in the card, no heading elided;
    //  - 409:32930: the parts of a card 10 apart, the title Semi Bold 15, the buttons hug their texts
    //    at the leading edge, the caption 8 below the button gap, an open document icon/tab
    for (const pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
    {
        for (const Qt::LayoutDirection direction : { Qt::LeftToRight, Qt::RightToLeft })
        {
            const QString suffix = QStringLiteral("%1-%2").arg(scheme == pdf::PDFThemeColors::Scheme::Light ? QStringLiteral("light") : QStringLiteral("dark"),
                                                               direction == Qt::LeftToRight ? QStringLiteral("ltr") : QStringLiteral("rtl"));
            const bool isMain = scheme == pdf::PDFThemeColors::Scheme::Light && direction == Qt::LeftToRight;
            QApplication::setLayoutDirection(direction);
            AppToolOwnTabFixture fixture;
            pdf::PDFThemeManager::getInstance()->setScheme(scheme);
            AppToolOwnTabFixture::settle();
            const QString contract = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Contract.pdf"), 6);
            const QString annex = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Annex_A.pdf"), 9);
            const QString image = H::png(fixture.folder(), QStringLiteral("signature_page.png"), QSize(400, 300));
            pdf::PDFAppToolTab* tab = H::open(fixture, { contract, annex, image });
            pdf::PDFAppManageToolGUI* gui = H::gui(tab);
            QVERIFY(gui);
            QVERIFY(H::waitPages(gui, 16));
            gui->setSelection(H::items(gui, { 0, 1, 2, 3, 4 }));
            gui->group();
            gui->rename(QStringLiteral("Introduction"));
            gui->setSelection(H::items(gui, { 8, 9, 10, 11 }));
            gui->group();
            gui->rename(QStringLiteral("Appendix"));
            gui->setSelection(H::items(gui, { 4 }));
            gui->rotateRight();
            gui->setSelection(H::items(gui, { 5 }));
            gui->removeSelection();
            gui->saveCheckpoint(QStringLiteral("Before grouping"));
            gui->setSelection(H::items(gui, { 2, 3 }));
            AppToolOwnTabFixture::settle();
            QTest::qWait(500);
            AppToolOwnTabFixture::settle();
            fixture.screenshot(QStringLiteral("pages-manage-") + suffix);
            pdf::PDFAppToolTabView* view = tab->getToolPage()->getTabView();
            for (const qreal ratio : { 1.0, 2.0 })
            {
                QCOMPARE(AppToolOwnTabFixture::countLeakPixels(AppToolOwnTabFixture::render(view, ratio)), 0);
            }

            // 387:12729: the sources A, B, C - one line, 46 high, 56 apart
            const std::vector<pdf::PDFAppToolInputRow*> rows = H::sourceRows(gui);
            QCOMPARE(rows.size(), size_t(3));
            for (size_t i = 0; i < rows.size(); ++i)
            {
                QCOMPARE(rows[i]->height(), 46);
                if (i > 0)
                {
                    QCOMPARE(rows[i]->y() - rows[i - 1]->y(), 56);
                }
            }

            // 387:12748: the header of Checkpoints 10 below the last source
            {
                pdf::PDFAppManageSourcesPanel* panel = gui->getSourcesPanel();
                const int sourcesBottom = rows.back()->mapTo(panel, QPoint(0, rows.back()->height())).y();
                const int headerTop = panel->getCheckpointsTitle()->parentWidget()->mapTo(panel, QPoint(0, 0)).y();
                QCOMPARE(headerTop - sourcesBottom, 10);
            }

            // 387:12669: the icon and the chevron of the menu of the assembly
            QCOMPARE(gui->getAction(QStringLiteral("assembly"))->width(), 50);

            if (!isMain)
            {
                pdf::PDFAppDocumentTab* documentTab = fixture.open(AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Invoice.pdf"), 10));
                pdf::PDFDocumentToolShell* documentShell = fixture.openTool(documentTab, H::toolId());
                QVERIFY(documentShell);
                AppToolOwnTabFixture::settle();
                QTest::qWait(300);
                fixture.screenshot(QStringLiteral("pages-manage-document-") + suffix);

                // 1479:34681: the source with the Tag "this document" is 51 high
                pdf::PDFAppManageToolGUI* documentGui = qobject_cast<pdf::PDFAppManageToolGUI*>(documentShell->getGUI());
                QVERIFY(documentGui);
                QCOMPARE(H::sourceRows(documentGui).front()->height(), 51);
                QCOMPARE(AppToolOwnTabFixture::countLeakPixels(AppToolOwnTabFixture::render(documentTab, 2.0)), 0);
                continue;
            }

            // The icons of the panels (387:14117, 387:14017)
            QCOMPARE(gui->getPanelRow(QStringLiteral("regroupOddEven"))->getIconName(), QStringLiteral("interleave"));
            QCOMPARE(gui->getPanelRow(QStringLiteral("regroupPairs"))->getIconName(), QStringLiteral("page-pair"));
            QCOMPARE(gui->getPanelRow(QStringLiteral("regroupAlternate"))->getIconName(), QStringLiteral("repeat"));
            QCOMPARE(gui->getPanelRow(QStringLiteral("regroupAlternateReversed"))->getIconName(), QStringLiteral("interleave-reversed"));
            QCOMPARE(gui->getPanelRow(QStringLiteral("addPages"))->getIconName(), QStringLiteral("page-insert"));

            const std::pair<const char*, int> panelWidths[] = {
                { "assembly", 320 }, { "add", 270 }, { "select", 250 }, { "group", 290 }, { "sort", 250 }, { "more", 270 }, { "output", 320 },
            };
            for (const auto& [panelName, panelWidth] : panelWidths)
            {
                const QString panel = QString::fromLatin1(panelName);
                gui->getAction(panel)->click();
                AppToolOwnTabFixture::settle();
                QVERIFY2(gui->getPanel(panel)->isVisible(), panelName);
                QCOMPARE(gui->getPanel(panel)->getPanelRect().width(), panelWidth);
                fixture.screenshot(QStringLiteral("pages-manage-menu-") + panel);
                if (QWidget* popup = QApplication::activePopupWidget())
                {
                    popup->close();
                }
                AppToolOwnTabFixture::settle();
            }

            using DialogOpener = void (pdf::PDFAppManageToolGUI::*)();
            const std::pair<const char*, DialogOpener> dialogs[] = {
                { "range", &pdf::PDFAppManageToolGUI::openRangeDialog },
                { "every", &pdf::PDFAppManageToolGUI::openEveryDialog },
                { "numbers", &pdf::PDFAppManageToolGUI::openNumbersDialog },
                { "properties", &pdf::PDFAppManageToolGUI::openPropertiesDialog },
                { "rename", &pdf::PDFAppManageToolGUI::openRenameDialog },
                { "move", &pdf::PDFAppManageToolGUI::openMoveDialog },
                { "checkpoint", &pdf::PDFAppManageToolGUI::openCheckpointDialog },
                { "crop", &pdf::PDFAppManageToolGUI::openCropDialog },
            };
            for (const auto& [name, opener] : dialogs)
            {
                (gui->*opener)();
                AppToolOwnTabFixture::settle();
                if (gui->getDialog())
                {
                    AppToolOwnTabFixture::screenshotDialog(gui->getDialog(), QStringLiteral("pages-manage-dialog-") + QString::fromLatin1(name));
                    fixture.screenshot(QStringLiteral("pages-manage-dialogwin-") + QString::fromLatin1(name));
                    gui->getDialog()->reject();
                }
                AppToolOwnTabFixture::settle();
            }

            // 387:15072: the ten columns in the card, Tags too, no heading elided (the cells of the
            // Library row have the paddings 10), also after a change of the assembly
            gui->getCanvas()->setDetailsShown(true);
            gui->setSelection(H::items(gui, { 1 }));
            gui->rotateLeft();
            gui->setSelection(H::items(gui, { 2, 3 }));
            AppToolOwnTabFixture::settle();
            fixture.screenshot(QStringLiteral("pages-manage-details"));
            {
                pdf::PDFAppToolMergeDetailsView* details = gui->getCanvas()->getDetailsView();
                QHeaderView* header = details->header();
                const pdf::PDFAppManageItemModel* itemModel = gui->getCanvas()->getItemModel();
                const QFontMetrics metrics(header->font());
                for (int column = 0; column < itemModel->columnCount(); ++column)
                {
                    const QString heading = itemModel->headerData(column, Qt::Horizontal, Qt::DisplayRole).toString();
                    QVERIFY2(header->sectionSize(column) >= metrics.horizontalAdvance(heading) + 20, qPrintable(heading));
                }
                const int last = pdf::PDFAppManageItemModel::TagsColumn;
                QVERIFY(header->sectionViewportPosition(last) >= 0);
                QVERIFY2(header->sectionViewportPosition(last) + header->sectionSize(last) <= details->viewport()->width(),
                         qPrintable(QStringLiteral("%1 + %2 > %3 stretch %4 resize %5 header %6 visible %7").arg(header->sectionViewportPosition(last)).arg(header->sectionSize(last)).arg(details->viewport()->width())
                                    .arg(header->stretchLastSection()).arg(int(header->sectionResizeMode(last))).arg(header->width()).arg(details->isVisible())));
            }
            gui->getCanvas()->setDetailsShown(false);

            // 387:15594: the search typed into the field filters the items, the status says it
            gui->getCanvas()->getSearchField()->setFocus();
            QTest::keyClicks(gui->getCanvas()->getSearchField(), QStringLiteral("landscape"));
            QVERIFY(AppToolOwnTabFixture::waitFor([gui]() { return gui->getCanvas()->isFiltered(); }));
            AppToolOwnTabFixture::settle();
            fixture.screenshot(QStringLiteral("pages-manage-search"));
            gui->getCanvas()->setSearchText(QString());
            AppToolOwnTabFixture::settle();

            QTimer::singleShot(200, gui, [gui, &fixture]()
            {
                fixture.screenshot(QStringLiteral("pages-manage-close-prompt"));
                if (gui->getCloseDialog())
                {
                    AppToolOwnTabFixture::screenshotDialog(gui->getCloseDialog(), QStringLiteral("pages-manage-dialog-close"));
                    gui->getCloseDialog()->findChild<QPushButton*>(QStringLiteral("cancelButton"))->click();
                }
            });
            gui->askOwnTabClose();
            AppToolOwnTabFixture::settle();

            // 409:32739 with an open document and a recent assembly
            pdf::PDFAppDocumentTab* documentTab = fixture.open(AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Invoice.pdf"), 10));
            QVERIFY(documentTab);
            QVERIFY(!pdf::PDFRecentAssemblyRepository(fixture.context->getDatabase()).recordOpened(QDir(fixture.folder()).filePath(QStringLiteral("Tender_2026.pdf4qtassembly")), 17).isError());
            pdf::PDFAppToolTab* emptyTab = H::open(fixture, { });
            QVERIFY(emptyTab);
            pdf::PDFAppManageToolGUI* emptyGui = H::gui(emptyTab);
            QVERIFY(emptyGui);
            AppToolOwnTabFixture::settle();
            fixture.screenshot(QStringLiteral("pages-manage-empty"));
            QWidget* emptyState = emptyGui->getCanvas()->getEmptyState();
            QVERIFY(emptyState);
            for (const QString& cardName : { QStringLiteral("manageEmptyNew"), QStringLiteral("manageEmptyOpen") })
            {
                QWidget* card = emptyState->findChild<QWidget*>(cardName);
                QVERIFY(card);
                QLayout* layout = card->layout();
                QWidget* icon = layout->itemAt(0)->widget();
                QWidget* title = layout->itemAt(1)->widget();
                QWidget* text = layout->itemAt(2)->widget();
                QWidget* button = layout->itemAt(3)->widget();
                QVERIFY(icon && title && text && qobject_cast<QPushButton*>(button));
                QCOMPARE(title->y() - (icon->y() + icon->height()), 10);
                QCOMPARE(text->y() - (title->y() + title->height()), 10);
                QCOMPARE(button->y() - (text->y() + text->height()), 10);
                QCOMPARE(title->font().pixelSize(), 15);
                QCOMPARE(button->width(), button->sizeHint().width());
                QCOMPARE(QStyle::visualRect(card->layoutDirection(), card->rect(), button->geometry()).left(), 23);
            }
            // The recent assembly is a row of icon/grid, the open document the shared row of
            // 409:32954 with icon/tab and the Secondary Add
            const QList<pdf::PDFTileRow*> emptyRows = emptyState->findChildren<pdf::PDFTileRow*>();
            QCOMPARE(emptyRows.size(), 1);
            QCOMPARE(emptyRows.front()->objectName(), QStringLiteral("manageEmptyRecent"));
            QCOMPARE(emptyRows.front()->getIconName(), QStringLiteral("grid"));
            const QList<pdf::PDFAppToolOpenDocumentRow*> documentRows = emptyState->findChildren<pdf::PDFAppToolOpenDocumentRow*>();
            QCOMPARE(documentRows.size(), 1);
            QCOMPARE(documentRows.front()->findChild<pdf::PDFIconBadge*>()->getIconName(), QStringLiteral("tab"));

            // The same rows in the panels Add (387:14055 icon/tab) and of the assembly (icon/grid)
            int panelRowCount = 0;
            for (const QString& panel : { QStringLiteral("add"), QStringLiteral("assembly") })
            {
                for (const pdf::PDFTileRow* row : emptyGui->getPanel(panel)->findChildren<pdf::PDFTileRow*>())
                {
                    if (row->objectName().startsWith(QStringLiteral("manageOpenDocument-")) || row->objectName().startsWith(QStringLiteral("manageRecent-")))
                    {
                        QCOMPARE(row->getIconName(), panel == QLatin1String("add") ? QStringLiteral("tab") : QStringLiteral("grid"));
                        ++panelRowCount;
                    }
                }
            }
            QCOMPARE(panelRowCount, 2);

            pdf::PDFDocumentToolShell* shell = fixture.openTool(documentTab, H::toolId());
            QVERIFY(shell);
            pdf::PDFAppManageToolGUI* documentGui = qobject_cast<pdf::PDFAppManageToolGUI*>(shell->getGUI());
            documentGui->setSelection(H::items(documentGui, { 6 }));
            documentGui->moveSelectionBefore(documentGui->getAssembly().getItems()[2].id);
            documentGui->setSelection(H::items(documentGui, { 2 }));
            AppToolOwnTabFixture::settle();
            QTest::qWait(300);
            fixture.screenshot(QStringLiteral("pages-manage-document-") + suffix);
            documentTab->getDocument()->setLocked(true);
            documentGui->onDocumentChanged();
            AppToolOwnTabFixture::settle();
            fixture.screenshot(QStringLiteral("pages-manage-document-locked"));
            documentTab->getDocument()->setLocked(false);
            documentGui->onDocumentChanged();
            AppToolOwnTabFixture::settle();
            shell->confirm();
            QVERIFY(AppToolOwnTabFixture::waitForState(shell, pdf::PDFDocumentToolShell::State::Deciding));
            AppToolOwnTabFixture::settle();
            fixture.screenshot(QStringLiteral("pages-manage-document-done"));
            if (shell->getResultDialog())
            {
                AppToolOwnTabFixture::screenshotDialog(shell->getResultDialog(), QStringLiteral("pages-manage-dialog-document-done"));
                shell->getResultDialog()->reject();
            }
            AppToolOwnTabFixture::settle();
        }
    }
    QApplication::setLayoutDirection(Qt::LeftToRight);
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
}

void AppToolManageTest::sourceOrderOfAddition()
{
    // RS3BFIGMA, 387:12569: the files of one addition become the sources B, C, D in the order, in
    // which they were given, with their pages after the selection in that order - also when the
    // file before the last one (a PDF of 3000 pages) is read long after the others (a page of a PDF,
    // an image): the first file comes at once, the other two follow its pages
    AppToolOwnTabFixture fixture;
    const QString contract = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Contract.pdf"), 2);
    const QString cover = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Cover.pdf"), 1);
    const QString annex = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Annex_A.pdf"), 3000);
    const QString image = H::png(fixture.folder(), QStringLiteral("signature_page.png"), QSize(40, 30));
    pdf::PDFAppToolTab* tab = H::open(fixture, { contract });
    pdf::PDFAppManageToolGUI* gui = H::gui(tab);
    QVERIFY(gui);
    QVERIFY(H::waitPages(gui, 2));

    gui->setSelection(H::items(gui, { 0 }));
    gui->addFiles({ cover, annex, image });
    QVERIFY(H::waitPages(gui, 2 + 1 + 3000 + 1));
    AppToolOwnTabFixture::settle();

    const std::vector<pdf::PDFDocumentAssembly::Source>& sources = gui->getAssembly().getSources();
    QCOMPARE(sources.size(), size_t(4));
    QCOMPARE(sources[0].name, QStringLiteral("Contract.pdf"));
    QCOMPARE(sources[1].name, QStringLiteral("Cover.pdf"));
    QCOMPARE(sources[2].name, QStringLiteral("Annex_A.pdf"));
    QCOMPARE(sources[3].name, QStringLiteral("signature_page.png"));

    // After the selected page 1: the cover B, the 3000 pages of C, the image D, then page 2 of A
    // (the positions are numbers of the application locale, O17)
    const QStringList labels = H::labels(gui);
    QCOMPARE(labels.size(), 3004);
    QCOMPARE(labels.at(0), QStringLiteral("A · 1"));
    QCOMPARE(labels.at(1), QStringLiteral("B · 2"));
    QCOMPARE(labels.at(2), QStringLiteral("C · 3"));
    QCOMPARE(labels.at(3001), QStringLiteral("C · ") + pdf::PDFAppLocale::formatNumber(qint64(3002)));
    QCOMPARE(labels.at(3002), QStringLiteral("D · ") + pdf::PDFAppLocale::formatNumber(qint64(3003)));
    QCOMPARE(labels.at(3003), QStringLiteral("A · ") + pdf::PDFAppLocale::formatNumber(qint64(3004)));
}

void AppToolManageTest::sourcesDialogDropsLateLocate()
{
    // 387:16980 (RS3BAPP): Locate… checks the chosen file in the job of the loader; Cancel of the
    // dialog before the check ends closes the question - the late result opens nothing (no
    // assembly, no second dialog; the tab stays the empty new assembly)
    AppToolOwnTabFixture fixture;
    const QString located = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Annex_found.pdf"), 2);
    const QString assemblyPath = fixture.folder() + QStringLiteral("/Missing.pdf4qtassembly");
    QVERIFY(H::writeAssembly(assemblyPath, fixture.folder() + QStringLiteral("/Annex_A.pdf"), 2));

    pdf::PDFAppToolTab* tab = H::open(fixture, { });
    pdf::PDFAppManageToolGUI* gui = H::gui(tab);
    QVERIFY(gui);
    QVERIFY(gui->openAssemblyFile(assemblyPath));
    QVERIFY(AppToolOwnTabFixture::waitFor([gui]() { return gui->getSourcesDialog() != nullptr; }));
    QPointer<pdf::PDFAppManageSourcesDialog> dialog = gui->getSourcesDialog();
    QCOMPARE(dialog->getResult().sources.size(), size_t(1));
    QCOMPARE(dialog->getResult().sources.front().state, pdf::PDFAppAssemblySourceState::Missing);
    pdf::PDFAppAssemblyLoader* loader = gui->findChild<pdf::PDFAppAssemblyLoader*>();
    QVERIFY(loader);

    // The job of the check waits in the pool until the dialog is gone (a slow disk)
    QThreadPool* pool = QThreadPool::globalInstance();
    const int maximumThreads = pool->maxThreadCount();
    QSemaphore release;
    QSemaphore started;
    pool->setMaxThreadCount(1);
    pool->start([&release, &started]() { started.release(); release.acquire(); });
    QVERIFY(started.tryAcquire(1, 30000 * AppToolOwnTabFixture::getTimeoutFactor()));

    // Locate… of the GUI after its file dialog: the check of the chosen file starts
    QVERIFY(loader->locate(dialog->getResult(), QStringLiteral("s1"), located));
    dialog->getCancelButton()->click();
    QVERIFY(AppToolOwnTabFixture::waitFor([&dialog]() { return dialog.isNull(); }));

    release.release();
    QVERIFY(AppToolOwnTabFixture::waitFor([loader]() { return !loader->isLoading(); }));
    AppToolOwnTabFixture::settle();
    pool->waitForDone();
    pool->setMaxThreadCount(maximumThreads);

    QVERIFY(!gui->getSourcesDialog());
    QVERIFY(gui->getAssembly().isEmpty());
    QVERIFY(gui->getAssemblyPath().isEmpty());
    QCOMPARE(gui->getOwnTabTitle(), QStringLiteral("New assembly"));
}

void AppToolManageTest::undoOfOpenAssemblyKeepsItsFile()
{
    // 409:32023 Open Assembly is an undo step (B24). The assembly before it has its own file:
    // after Undo the tab is that assembly again ("First", its checkpoint) and Save Assembly writes
    // it into its own file - never into the file opened after it; Redo brings the opened file back
    AppToolOwnTabFixture fixture;
    const QString contract = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Contract.pdf"), 3);
    const QString secondPath = fixture.folder() + QStringLiteral("/Second.pdf4qtassembly");
    QVERIFY(H::writeAssembly(secondPath, contract, 3));
    const QByteArray secondBytes = H::readFile(secondPath);

    pdf::PDFAppToolTab* tab = H::open(fixture, { contract });
    pdf::PDFAppManageToolGUI* gui = H::gui(tab);
    QVERIFY(gui);
    QVERIFY(H::waitPages(gui, 3));
    gui->setSelection(H::items(gui, { 0 }));
    gui->removeSelection();
    AppToolOwnTabFixture::settle();
    gui->saveCheckpoint(QStringLiteral("Two pages"));
    const QString firstPath = fixture.folder() + QStringLiteral("/First.pdf4qtassembly");
    QString error;
    QVERIFY2(gui->saveAssemblyTo(firstPath, &error), qPrintable(error));
    QVERIFY(!gui->isAssemblyModified());

    QVERIFY(gui->openAssemblyFile(secondPath));
    QVERIFY(AppToolOwnTabFixture::waitFor([gui, &secondPath]() { return QFileInfo(gui->getAssemblyPath()) == QFileInfo(secondPath); }));
    QVERIFY(H::waitPages(gui, 3));
    QCOMPARE(gui->getOwnTabTitle(), QStringLiteral("Second"));
    QVERIFY(gui->getCheckpoints().empty());
    QCOMPARE(gui->getModel()->getUndoText(), QStringLiteral("Open Assembly"));

    // Undo: the first assembly with its file, its checkpoint and its saved state
    gui->getModel()->undo();
    AppToolOwnTabFixture::settle();
    QVERIFY(H::waitPages(gui, 2));
    QCOMPARE(QFileInfo(gui->getAssemblyPath()), QFileInfo(firstPath));
    QCOMPARE(gui->getOwnTabTitle(), QStringLiteral("First"));
    QCOMPARE(gui->getCheckpoints().size(), size_t(1));
    QCOMPARE(gui->getCheckpoints().front().name, QStringLiteral("Two pages"));
    QVERIFY(!gui->isAssemblyModified());

    // Redo of the opening: the opened file is back; Undo again
    gui->getModel()->redo();
    AppToolOwnTabFixture::settle();
    QVERIFY(H::waitPages(gui, 3));
    QCOMPARE(QFileInfo(gui->getAssemblyPath()), QFileInfo(secondPath));
    QCOMPARE(gui->getOwnTabTitle(), QStringLiteral("Second"));
    QVERIFY(gui->getCheckpoints().empty());
    gui->getModel()->undo();
    AppToolOwnTabFixture::settle();
    QVERIFY(H::waitPages(gui, 2));
    QCOMPARE(QFileInfo(gui->getAssemblyPath()), QFileInfo(firstPath));

    // A change and Save Assembly: written to First, Second stays as it was
    gui->setSelection(H::items(gui, { 0 }));
    gui->rotateRight();
    AppToolOwnTabFixture::settle();
    QVERIFY(gui->isAssemblyModified());
    QVERIFY(gui->saveAssembly());
    QCOMPARE(H::readFile(secondPath), secondBytes);
    QVERIFY(H::readFile(firstPath).contains("Two pages"));
}

void AppToolManageTest::largeSelectionOperations()
{
    // Plan 6 (every operation O(n log n)): 32 768 selected items - Alt+arrow at the start (nothing
    // moves), By bookmarks of blank pages (refused), Copy and Paste after the selection (65 536
    // pages, the limit of an assembly by the owner 2026-11-12) - each in one pass, no work of the
    // selected count times the item count (32 768 x 32 768 string comparisons would take minutes)
    constexpr int ITEM_COUNT = 32768;
    AppToolOwnTabFixture fixture;
    pdf::PDFAppToolTab* tab = H::open(fixture, { });
    pdf::PDFAppManageToolGUI* gui = H::gui(tab);
    QVERIFY(gui);
    std::vector<pdf::PDFDocumentAssembly::Item> items;
    items.reserve(ITEM_COUNT);
    for (int i = 0; i < ITEM_COUNT; ++i)
    {
        pdf::PDFDocumentAssembly::Item item;
        item.id = QStringLiteral("i%1").arg(i + 1);
        pdf::PDFDocumentAssembly::Page page;
        page.plan.input = pdf::PDFDocumentAssembly::BLANK_INPUT;
        page.plan.pageSize = QSizeF(595, 842);
        item.pages.push_back(page);
        items.push_back(std::move(item));
    }
    QString error;
    const pdf::PDFDocumentAssembly assembly = pdf::PDFDocumentAssembly::create({ }, std::move(items), { }, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(gui->getModel()->setValues(QVariantMap({ { QString::fromLatin1(pdf::PDFDocumentToolManage::PARAMETER_ASSEMBLY), QVariant::fromValue(assembly) } }), QStringLiteral("Fill")).isEmpty());
    AppToolOwnTabFixture::settle();
    QCOMPARE(gui->getAssembly().getItems().size(), size_t(ITEM_COUNT));
    gui->selectAll();
    QCOMPARE(gui->getSelection().size(), size_t(ITEM_COUNT));
    const pdf::PDFDocumentAssembly before = gui->getAssembly();

    // Nothing changes: one pass each (milliseconds; a pass per selected item takes seconds)
    QElapsedTimer timer;
    timer.start();
    gui->moveSelection(-1);
    gui->openBookmarksDialog();
    const qint64 unchangedElapsed = timer.elapsed();
    QVERIFY(!gui->getDialog());
    QVERIFY(gui->getAssembly() == before);
    QVERIFY2(unchangedElapsed < 2000 * qint64(AppToolOwnTabFixture::getTimeoutFactor()), qPrintable(QString::number(unchangedElapsed)));

    // Copy and Paste: the copies after the selection, the views follow
    timer.restart();
    gui->copy();
    gui->paste();
    AppToolOwnTabFixture::settle();
    const qint64 pasteElapsed = timer.elapsed();
    QCOMPARE(gui->getAssembly().getItems().size(), size_t(2 * ITEM_COUNT));
    QCOMPARE(gui->getAssembly().getItems()[ITEM_COUNT].pages.front().plan.input, pdf::PDFDocumentAssembly::BLANK_INPUT);
    QVERIFY2(pasteElapsed < 5000 * qint64(AppToolOwnTabFixture::getTimeoutFactor()), qPrintable(QString::number(pasteElapsed)));
}

void AppToolManageTest::checkpointPageLimit()
{
    // Owner 2026-11-12 (plan of step 3b, 13 point 3): an assembly file has at most 16 x 65 536 =
    // 1 048 576 pages and Save checkpoint keeps the room of a whole assembly in it (what the
    // application saves, it always opens again) - an assembly of 65 536 pages has room for 15
    // checkpoints of itself (15 x 65 536 = 983 040), a sixteenth is refused with the reason in the
    // status bar and the checkpoints stay 15
    constexpr int PAGE_COUNT = 65536;
    AppToolOwnTabFixture fixture;
    pdf::PDFAppToolTab* tab = H::open(fixture, { });
    pdf::PDFAppManageToolGUI* gui = H::gui(tab);
    QVERIFY(gui);
    pdf::PDFDocumentAssembly::Item group;
    group.id = QStringLiteral("g1");
    group.isCreatedGroup = true;
    pdf::PDFDocumentAssembly::Page page;
    page.plan.input = pdf::PDFDocumentAssembly::BLANK_INPUT;
    page.plan.pageSize = QSizeF(595, 842);
    group.pages.assign(PAGE_COUNT, page);
    QString error;
    const pdf::PDFDocumentAssembly assembly = pdf::PDFDocumentAssembly::create({ }, { group }, { }, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(gui->getModel()->setValues(QVariantMap({ { QString::fromLatin1(pdf::PDFDocumentToolManage::PARAMETER_ASSEMBLY), QVariant::fromValue(assembly) } }), QStringLiteral("Fill")).isEmpty());
    AppToolOwnTabFixture::settle();
    QCOMPARE(gui->getAssembly().getItems().front().pages.size(), size_t(PAGE_COUNT));

    for (int i = 1; i <= 15; ++i)
    {
        gui->saveCheckpoint(QStringLiteral("Checkpoint %1").arg(i));
        QCOMPARE(gui->getCheckpoints().size(), size_t(i));
    }
    AppToolOwnTabFixture::settle();
    gui->saveCheckpoint(QStringLiteral("Checkpoint 16"));
    AppToolOwnTabFixture::settle();
    QCOMPARE(gui->getCheckpoints().size(), size_t(15));
    const QString message = tab->getToolPage()->getTabView()->getStatusBar()->getMessage();
    QVERIFY2(message.startsWith(QStringLiteral("The checkpoints of an assembly have at most ")), qPrintable(message));
}

void AppToolManageTest::looseTilesByFigma()
{
    // Owner Q4 of 12. 11. (plan of step 3b, 13 point 4), 387:12937 and 1479:34787: a page outside
    // of a group is the box 100 x 100 in the tile 112 x 128 - 112 x 144 with the line of its
    // caption (387:13010) - with the page fitted into the box: a portrait page of 3 : 4 is 75 x 100
    // centered (12.5 from the edges of the box), a landscape page of 4 : 3 is 100 x 75 (387:13011
    // "rotated 90°" 100 wide); the box 6 from the top and the sides of the tile. The loose tiles are
    // 6 apart in both directions (387:12938 x 0 / 118, 387:13076 y 150) and start at the edge of the
    // stage (the leading edge: the right one in right to left). The tiles of a card keep 85 x 128
    // with the box 73 x 100, 8 apart (387:12814, 387:12837: x 0 / 93), a page of 3 : 4 is 73 x 97.
    // The size Medium scales the slot by 1.5: the box 150 x 150, the tile 162 x 194. Both themes,
    // right to left, 1x / 2x without the leak color.
    for (const pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
    {
        for (const Qt::LayoutDirection direction : { Qt::LeftToRight, Qt::RightToLeft })
        {
            QApplication::setLayoutDirection(direction);
            AppToolOwnTabFixture fixture;
            pdf::PDFThemeManager::getInstance()->setScheme(scheme);
            AppToolOwnTabFixture::settle();
            const QString contract = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Contract.pdf"), 12);
            const QString image = H::png(fixture.folder(), QStringLiteral("signature_page.png"), QSize(400, 300));
            pdf::PDFAppToolTab* tab = H::open(fixture, { contract, image });
            pdf::PDFAppManageToolGUI* gui = H::gui(tab);
            QVERIFY(gui);
            QVERIFY(H::waitPages(gui, 13));
            gui->setSelection(H::items(gui, { 0, 1, 2 }));
            gui->group();
            gui->setSelection({ });
            pdf::PDFAppManageCanvas* canvas = gui->getCanvas();
            canvas->setTileSize(pdf::PDFAppToolPageGrid::TileSize::Small);
            AppToolOwnTabFixture::settle();

            const pdf::PDFAppManagePageModel* model = canvas->getPageModel();
            pdf::PDFAppToolMergePageView* view = canvas->getPageView();
            const pdf::PDFAppToolPageTileDelegate* delegate = canvas->getDelegate();
            QVERIFY(delegate);
            QCOMPARE(model->rowCount(), 13);
            QCOMPARE(model->getGroups().size(), size_t(2));
            QVERIFY(!model->getGroups()[0].isPlain);
            QVERIFY(model->getGroups()[1].isPlain);

            const auto tile = [view, model](int row) { return view->visualRect(model->index(row, 0)); };
            const auto page = [view, model, delegate, direction](int row)
            {
                QStyleOptionViewItem option;
                option.direction = direction;
                option.rect = view->visualRect(model->index(row, 0));
                return delegate->getMarkRects(option, model->index(row, 0)).page;
            };
            const int sign = direction == Qt::LeftToRight ? 1 : -1;

            // The card: 85 x 128, 8 apart, the page 73 x 97 at the top of its box
            QCOMPARE(tile(0).size(), QSize(85, 128));
            QCOMPARE(sign * (tile(1).x() - tile(0).x()), 85 + 8);
            QCOMPARE(page(0).size(), QSizeF(73, 97));
            QCOMPARE(page(0).left() - tile(0).left(), 6.0);

            // The loose tiles (rows 3 - 12: nine pages and the image): 112 x 144, 118 apart, at the
            // leading edge of the stage
            QCOMPARE(tile(3).size(), QSize(112, 144));
            QCOMPARE(sign * (tile(4).x() - tile(3).x()), 112 + 6);
            if (direction == Qt::LeftToRight)
            {
                QCOMPARE(tile(3).left(), 0);
            }
            else
            {
                QCOMPARE(tile(3).right(), view->viewport()->width() - 1);
            }
            int nextLine = -1;
            for (int row = 4; row < 13 && nextLine < 0; ++row)
            {
                nextLine = tile(row).y() != tile(3).y() ? row : -1;
            }
            QVERIFY(nextLine > 4);
            QCOMPARE(tile(nextLine).y() - tile(3).y(), 144 + 6);
            QCOMPARE(tile(nextLine).x(), tile(3).x());

            // A portrait page 75 x 100 in the middle of the box, the landscape image 100 x 75
            QCOMPARE(page(3).size(), QSizeF(75, 100));
            QCOMPARE(page(3).left() - tile(3).left(), 6.0 + 12.0);
            QCOMPARE(page(3).top() - tile(3).top(), 6.0);
            QCOMPARE(page(12).size(), QSizeF(100, 75));
            QCOMPARE(page(12).left() - tile(12).left(), 6.0);
            QCOMPARE(page(12).top() - tile(12).top(), 6.0 + 12.0);

            pdf::PDFAppToolTabView* tabView = tab->getToolPage()->getTabView();
            for (const qreal ratio : { 1.0, 2.0 })
            {
                QCOMPARE(AppToolOwnTabFixture::countLeakPixels(AppToolOwnTabFixture::render(tabView, ratio)), 0);
            }

            // Medium: the slot 110 x 150, the box of a loose page 150 x 150
            canvas->setTileSize(pdf::PDFAppToolPageGrid::TileSize::Medium);
            AppToolOwnTabFixture::settle();
            QCOMPARE(tile(0).size(), QSize(110 + 12, 150 + 28));
            QCOMPARE(tile(3).size(), QSize(150 + 12, 150 + 28 + 16));
            QCOMPARE(sign * (tile(4).x() - tile(3).x()), 162 + 6);
            QCOMPARE(page(12).size(), QSizeF(150, 113));
        }
    }
    QApplication::setLayoutDirection(Qt::LeftToRight);
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
}

void AppToolManageTest::itemNameIsTheFileName()
{
    // Owner Q5 of 12. 11. (plan of step 3b, 13 point 5), 387:15072: the name of an item is the file
    // name ("Contract.pdf", "signature_page.png", not "Contract.pdf, p. 2") in Details, the tooltip
    // of a tile, the prefill of Rename and the rows of the bin; a name the user gave stays ("Site
    // plan"), a blank page is "Blank page"; an unnamed group is titled by its file and its subtitle
    // says the pages (387:12811 "Group · 2 pages · Contract.pdf 3–4"). The caption "p. N" is not a
    // part of the name: the tile says the position ("A · 2").
    AppToolOwnTabFixture fixture;
    const QString contract = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Contract.pdf"), 5);
    const QString image = H::png(fixture.folder(), QStringLiteral("signature_page.png"), QSize(40, 30));
    pdf::PDFAppToolTab* tab = H::open(fixture, { contract, image });
    pdf::PDFAppManageToolGUI* gui = H::gui(tab);
    QVERIFY(gui);
    QVERIFY(H::waitPages(gui, 6));

    gui->setSelection(H::items(gui, { 2, 3 }));
    gui->group();
    gui->setSelection(H::items(gui, { 1 }));
    gui->rename(QStringLiteral("Site plan"));
    gui->setSelection(H::items(gui, { 4 }));
    gui->insertBlankPage();
    AppToolOwnTabFixture::settle();

    // Items: Contract p. 1, "Site plan" (p. 2), the group of p. 3 - 4, p. 5, the image, the blank page
    const std::vector<pdf::PDFDocumentAssembly::Item>& items = gui->getAssembly().getItems();
    QCOMPARE(items.size(), size_t(6));
    const pdf::PDFAppManageItemModel* itemModel = gui->getCanvas()->getItemModel();
    QStringList names;
    for (int row = 0; row < itemModel->rowCount(); ++row)
    {
        names << itemModel->data(itemModel->index(row, pdf::PDFAppManageItemModel::NameColumn), Qt::DisplayRole).toString();
    }
    QCOMPARE(names, QStringList({ QStringLiteral("Contract.pdf"), QStringLiteral("Site plan"), QStringLiteral("Contract.pdf"), QStringLiteral("Contract.pdf"),
                                  QStringLiteral("signature_page.png"), QStringLiteral("Blank page") }));

    const pdf::PDFAppManagePageModel* pageModel = gui->getCanvas()->getPageModel();
    QCOMPARE(pageModel->getGroups()[1].title, QStringLiteral("Contract.pdf"));
    QCOMPARE(pageModel->getGroups()[1].subtitle, QStringLiteral("Group · 2 pages · Contract.pdf 3–4"));
    QCOMPARE(pageModel->data(pageModel->index(0, 0), Qt::DisplayRole).toString(), QStringLiteral("A · 1"));
    QCOMPARE(pageModel->data(pageModel->index(0, 0), Qt::ToolTipRole).toString(), QStringLiteral("Page 1 of the result · Contract.pdf"));
    QCOMPARE(pageModel->data(pageModel->index(4, 0), Qt::ToolTipRole).toString(), QStringLiteral("Page 5 of the result · Contract.pdf"));

    // Rename starts with the name of the item
    gui->setSelection(H::items(gui, { 3 }));
    gui->openRenameDialog();
    AppToolOwnTabFixture::settle();
    pdf::PDFAppManageTextDialog* rename = qobject_cast<pdf::PDFAppManageTextDialog*>(gui->getDialog());
    QVERIFY(rename);
    QCOMPARE(rename->getField()->text(), QStringLiteral("Contract.pdf"));
    rename->reject();
    AppToolOwnTabFixture::settle();

    // The bin lists the removed item by its name
    gui->removeSelection();
    AppToolOwnTabFixture::settle();
    QTest::mouseClick(gui->getSourcesPanel()->getRemovedRow(), Qt::LeftButton, Qt::NoModifier, QPoint(4, 4));
    AppToolOwnTabFixture::settle();
    const std::vector<pdf::PDFAppManageListRow*> removedRows = gui->getSourcesPanel()->getRemovedItemRows();
    QCOMPARE(removedRows.size(), size_t(1));
    QCOMPARE(removedRows.front()->getContent().title, QStringLiteral("Contract.pdf"));
}

/// Helpers of the tests of the fix wave of the GUI (P2S3BFIXGUI)
class ManageFixWaveTestHelper
{
public:
    ManageFixWaveTestHelper() = delete;

    /// 3 pages encrypted by AES-256 with the owner password only (it opens without a password):
    /// every permission of Table 22 except bit 11 Assemble (inserting, rotating, removing pages)
    static QString writeWithoutAssemble(const QString& folder)
    {
        pdf::PDFDocumentBuilder builder;
        for (int i = 0; i < 3; ++i)
        {
            builder.appendPage(QRectF(0, 0, 300, 400));
        }
        pdf::PDFSecurityHandlerFactory::SecuritySettings settings;
        settings.algorithm = pdf::PDFSecurityHandlerFactory::AES_256;
        settings.ownerPassword = AppDocumentSamples::getOwnerPassword();
        settings.permissions = 0xFFFFFFFCu & ~uint32_t(pdf::PDFSecurityHandler::Permission::Assemble);
        builder.setSecurityHandler(pdf::PDFSecurityHandlerFactory::createSecurityHandler(settings));
        pdf::PDFDocument document = builder.build();
        const QString path = folder + QStringLiteral("/no-assemble.pdf");
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || !bool(pdf::PDFDocumentWriter(nullptr).write(&file, &document)))
        {
            return QString();
        }
        return path;
    }

    /// The visible owner password dialog of the window, nullptr = none
    static pdf::PDFAppPasswordDialog* passwordDialog(const QWidget* window)
    {
        for (pdf::PDFAppPasswordDialog* dialog : window->findChildren<pdf::PDFAppPasswordDialog*>())
        {
            if (dialog->isVisible())
            {
                return dialog;
            }
        }
        return nullptr;
    }

    /// The color of the pixel in the middle of the leading edge of the widget (inside of its radius)
    static QColor edgeColor(QWidget* widget)
    {
        const QImage image = widget->grab().toImage();
        return image.pixelColor(2, image.height() / 2);
    }
};

void AppToolManageTest::documentHostWithoutAssemble()
{
    // Owner answer 1 of 12. 11., 1479:35631: a document without Assemble (Table 22 bit 11) opens
    // Manage Pages in its tab - Rearrange Pages is Disabled, the red callout "Owner password
    // required" with "Enter owner password…", the status of the frame; Rearrange does not run. Any
    // other tool is refused at the entry as before (339:10328). The owner password enables it.
    using F = ManageFixWaveTestHelper;
    AppToolOwnTabFixture fixture;
    const QString path = F::writeWithoutAssemble(fixture.folder());
    QVERIFY(!path.isEmpty());
    pdf::PDFAppDocumentTab* documentTab = fixture.open(path);
    QVERIFY(documentTab);
    QVERIFY(AppToolOwnTabFixture::waitFor([documentTab]() { return documentTab->getDocument()->isReady(); }));
    QVERIFY(pdf::PDFAppDocumentPermissions::canUnlock(documentTab->getDocument()->getDocument().data()));

    // Rotate Pages (Assemble too) is refused at the entry: the owner password dialog of the shell
    pdf::PDFDocumentToolShell* rotateShell = fixture.openTool(documentTab, QStringLiteral("pages.rotate"));
    QVERIFY(rotateShell);
    QVERIFY(rotateShell->isPermissionRefused());
    QVERIFY(!rotateShell->getGUI()->handlesPermissionRefusal());
    AppToolOwnTabFixture::settle();
    pdf::PDFAppPasswordDialog* rotateDialog = F::passwordDialog(fixture.window);
    QVERIFY(rotateDialog);
    rotateDialog->reject();
    QVERIFY(AppToolOwnTabFixture::waitFor([documentTab]() { return !AppToolOwnTabFixture::getShell(documentTab); }));

    // Manage Pages opens: no refusal at the entry, no dialog
    pdf::PDFDocumentToolShell* shell = fixture.openTool(documentTab, H::toolId());
    QVERIFY(shell);
    AppToolOwnTabFixture::settle();
    pdf::PDFAppManageToolGUI* gui = qobject_cast<pdf::PDFAppManageToolGUI*>(shell->getGUI());
    QVERIFY(gui);
    QVERIFY(gui->isDocumentHost());
    QVERIFY(gui->handlesPermissionRefusal());
    QVERIFY(!shell->isPermissionRefused());
    QCOMPARE(shell->getState(), pdf::PDFDocumentToolShell::State::Editing);
    QVERIFY(!F::passwordDialog(fixture.window));
    QCOMPARE(gui->getAssembly().getItems().size(), size_t(3));

    // 1479:35631: Rearrange Pages Disabled, the callout, the status
    QCOMPARE(gui->getConfirmText(), QStringLiteral("Rearrange Pages"));
    QVERIFY(!gui->isConfirmEnabled());
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
    pdf::PDFAppManageResultPanel* panel = gui->getResultPanel();
    QVERIFY(panel->getLockCallout()->isVisibleTo(panel));
    QVERIFY(!panel->getInfoCallout()->isVisibleTo(panel));
    QCOMPARE(panel->getContent().calloutTitle, QStringLiteral("Owner password required"));
    QCOMPARE(panel->getContent().calloutText, QStringLiteral("The document does not allow inserting, rotating and removing pages. If you own it, unlock it with its owner password. Until then, Rearrange Pages is not available; the pages can still be arranged here."));
    QCOMPARE(panel->getCalloutLink()->text(), QStringLiteral("Enter owner password…"));
    QVERIFY(panel->getCalloutLink()->isVisibleTo(panel));
    const QString title = documentTab->getDocument()->getTitle();
    QCOMPARE(gui->getStatusText(), QStringLiteral("Manage Pages · %1 does not allow inserting, rotating and removing pages · Rearrange Pages needs its owner password").arg(title));

    // The pages can still be arranged; the run is refused (the document stays as it is)
    gui->setSelection(H::items(gui, { 2 }));
    gui->moveSelectionBefore(gui->getAssembly().getItems()[0].id);
    AppToolOwnTabFixture::settle();
    QCOMPARE(H::labels(gui).front(), QStringLiteral("1 · p. 3"));
    QVERIFY(!gui->isConfirmEnabled());
    const quint64 version = documentTab->getDocument()->getVersion();
    shell->confirm();
    AppToolOwnTabFixture::settle();
    QCOMPARE(shell->getState(), pdf::PDFDocumentToolShell::State::Editing);
    QVERIFY(!shell->getResultDialog());
    QCOMPARE(documentTab->getDocument()->getVersion(), version);

    // Enter owner password… (the shared owner password dialog): the tool goes on, enabled; the
    // arrangement stays
    panel->getCalloutLink()->click();
    AppToolOwnTabFixture::settle();
    pdf::PDFAppPasswordDialog* dialog = F::passwordDialog(fixture.window);
    QVERIFY(dialog);
    QVERIFY(dialog->isDocumentMode());
    QTest::keyClicks(dialog->getPasswordField(), AppDocumentSamples::getOwnerPassword());
    QTest::mouseClick(dialog->getUnlockButton(), Qt::LeftButton);
    QVERIFY(AppToolOwnTabFixture::waitFor([gui]() { return gui->isConfirmEnabled(); }));
    QCOMPARE(AppToolOwnTabFixture::getShell(documentTab), shell);
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());
    QVERIFY(!panel->getLockCallout()->isVisibleTo(panel));
    QCOMPARE(panel->getContent().calloutTitle, QStringLiteral("The pages are rearranged in the file"));
    QCOMPARE(H::labels(gui).front(), QStringLiteral("1 · p. 3"));
    QVERIFY(!gui->getStatusText().contains(QStringLiteral("owner password")));
}

void AppToolManageTest::dialogsDimTheWindow()
{
    // Owner answer 6 of 12. 11., 387:15926 (the scrim 1440 x 922 below the top bar): every dialog
    // of Manage Pages dims the window body, the dialog host of the window - not only the canvas
    AppToolOwnTabFixture fixture;
    const QString contract = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Contract.pdf"), 4);
    pdf::PDFAppToolTab* tab = H::open(fixture, { contract });
    pdf::PDFAppManageToolGUI* gui = H::gui(tab);
    QVERIFY(gui);
    QVERIFY(H::waitPages(gui, 4));
    gui->setSelection(H::items(gui, { 1 }));
    QWidget* body = fixture.window->getDialogHost();
    QVERIFY(body);
    QWidget* canvasArea = gui->getHost() ? gui->getHost()->getCanvasArea() : nullptr;
    QVERIFY(canvasArea);
    QVERIFY(body->height() > canvasArea->height());

    using DialogOpener = void (pdf::PDFAppManageToolGUI::*)();
    const DialogOpener openers[] = { &pdf::PDFAppManageToolGUI::openRangeDialog, &pdf::PDFAppManageToolGUI::openPropertiesDialog,
                                     &pdf::PDFAppManageToolGUI::openEveryDialog, &pdf::PDFAppManageToolGUI::openCropDialog };
    for (const DialogOpener opener : openers)
    {
        (gui->*opener)();
        AppToolOwnTabFixture::settle();
        QPointer<pdf::PDFAppManageDialog> dialog = gui->getDialog();
        QVERIFY(dialog);
        QCOMPARE(dialog->getScrimTarget(), body);
        dialog->reject();
        AppToolOwnTabFixture::settle();
    }

    // The close question 405:30894 dims the same body
    gui->rotateRight();
    AppToolOwnTabFixture::settle();
    QWidget* closeScrim = nullptr;
    QTimer::singleShot(0, gui, [gui, &closeScrim]()
    {
        if (pdf::PDFAppDocumentChoiceDialog* dialog = gui->getCloseDialog())
        {
            closeScrim = dialog->getScrimTarget();
            dialog->findChild<QPushButton*>(QStringLiteral("cancelButton"))->click();
        }
    });
    QCOMPARE(gui->askOwnTabClose(), pdf::PDFAppToolCloseAnswer::Cancel);
    QCOMPARE(closeScrim, body);
}

void AppToolManageTest::rangeDialogsStartFromSelection()
{
    // Owner answer 7 of 12. 11., 387:15926: Select page range starts with the positions of the
    // selection in the syntax of the field ("1-3, 8, 10-": the runs, the run up to the last page
    // open), never in the error state; without a selection the field is empty, not invalid, and
    // Select waits. Regroup at page numbers starts without an error too.
    AppToolOwnTabFixture fixture;
    const QString contract = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Contract.pdf"), 6);
    const QString annex = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Annex_A.pdf"), 9);
    pdf::PDFAppToolTab* tab = H::open(fixture, { contract, annex });
    pdf::PDFAppManageToolGUI* gui = H::gui(tab);
    QVERIFY(gui);
    QVERIFY(H::waitPages(gui, 15));

    // Positions 1, 2, 3, 8, 14, 15 of 15
    gui->setSelection(H::items(gui, { 0, 1, 2, 7, 13, 14 }));
    gui->openRangeDialog();
    AppToolOwnTabFixture::settle();
    pdf::PDFAppManageRangeDialog* range = qobject_cast<pdf::PDFAppManageRangeDialog*>(gui->getDialog());
    QVERIFY(range);
    QCOMPARE(range->getRangeField()->getText(), QStringLiteral("1-3, 8, 14-"));
    QVERIFY(range->getRangeField()->getError().isEmpty());
    QVERIFY(!range->getRangeField()->getField()->isInvalid());
    QCOMPARE(range->getStatusLabel()->text(), QStringLiteral("6 items will be selected"));
    QVERIFY(range->getAcceptButton()->isEnabled());
    QVERIFY(range->getPositionRadio()->isChecked());
    QVERIFY(range->getReplaceRadio()->isChecked());

    // The status follows each key at once: "1-3, 8, 14" (no open end) is 5 items, then the
    // removal of "4" leaves "1-3, 8, 1" (4 items, page 1 twice); "1-3, 8, 1x" is an error
    range->getRangeField()->getField()->end(false);
    QTest::keyClick(range->getRangeField()->getField(), Qt::Key_Backspace);
    QCOMPARE(range->getStatusLabel()->text(), QStringLiteral("5 items will be selected"));
    QTest::keyClick(range->getRangeField()->getField(), Qt::Key_Backspace);
    QCOMPARE(range->getStatusLabel()->text(), QStringLiteral("4 items will be selected"));
    QTest::keyClicks(range->getRangeField()->getField(), QStringLiteral("x"));
    QCOMPARE(range->getStatusLabel()->getTextColor(), pdf::PDFThemeColors::TextDanger);
    QVERIFY(!range->getAcceptButton()->isEnabled());
    range->getRangeField()->getField()->selectAll();
    QTest::keyClicks(range->getRangeField()->getField(), QStringLiteral("1-3, 8, 14-"));
    QCOMPARE(range->getStatusLabel()->text(), QStringLiteral("6 items will be selected"));

    // Select gives the same selection back
    range->triggerAccept();
    AppToolOwnTabFixture::settle();
    QCOMPARE(gui->getSelection(), H::items(gui, { 0, 1, 2, 7, 13, 14 }));

    // A group is selected with all its pages; the last page alone is no open range
    gui->setSelection(H::items(gui, { 3, 4, 5 }));
    gui->group();
    AppToolOwnTabFixture::settle();
    QCOMPARE(gui->getAssembly().getItems().size(), size_t(13));
    QCOMPARE(pdf::PDFAppManageDialogs::getSelectionRangeText(gui->getAssembly(), H::items(gui, { 3, 12 })), QStringLiteral("4-6, 15"));
    QCOMPARE(pdf::PDFAppManageDialogs::getSelectionRangeText(gui->getAssembly(), H::items(gui, { 11, 12 })), QStringLiteral("14-"));
    QVERIFY(pdf::PDFAppManageDialogs::getSelectionRangeText(gui->getAssembly(), { QStringLiteral("no-such-item") }).isEmpty());

    // Without a selection: empty, no error, Select Disabled
    gui->setSelection({ });
    gui->openRangeDialog();
    AppToolOwnTabFixture::settle();
    range = qobject_cast<pdf::PDFAppManageRangeDialog*>(gui->getDialog());
    QVERIFY(range);
    QVERIFY(range->getRangeField()->getText().isEmpty());
    QVERIFY(range->getRangeField()->getError().isEmpty());
    QVERIFY(!range->getRangeField()->getField()->isInvalid());
    QVERIFY(range->getStatusLabel()->text().isEmpty());
    QVERIFY(!range->getAcceptButton()->isEnabled());
    range->reject();
    AppToolOwnTabFixture::settle();

    // Regroup at page numbers: the selection as one group, no error, Regroup waits for positions
    gui->setSelection(H::items(gui, { 0, 1, 2, 3 }));
    gui->openNumbersDialog();
    AppToolOwnTabFixture::settle();
    pdf::PDFAppManageTextDialog* numbers = qobject_cast<pdf::PDFAppManageTextDialog*>(gui->getDialog());
    QVERIFY(numbers);
    QCOMPARE(numbers->getStatusLabel()->text(), QStringLiteral("6 selected pages → 1 group (1–6)"));
    QCOMPARE(numbers->getStatusLabel()->getTextColor(), pdf::PDFThemeColors::TextAccent);
    QVERIFY(!numbers->getAcceptButton()->isEnabled());
    numbers->getField()->setText(QStringLiteral("4"));
    QCOMPARE(numbers->getStatusLabel()->text(), QStringLiteral("6 selected pages → 2 groups (1–3, 4–6)"));
    QVERIFY(numbers->getAcceptButton()->isEnabled());
    numbers->reject();
    AppToolOwnTabFixture::settle();
}

void AppToolManageTest::addPagesDialogPreviews()
{
    // RS3BFIGMA Low, 387:16742: Add pages of a PDF shows the file on bg/subtle (387:16744) and the
    // previews of the chosen pages in the order of the range - the tiles 53 x 84 with the page
    // number (387:16764); Properties has its facts on bg/subtle (387:16947)
    using F = ManageFixWaveTestHelper;
    AppToolOwnTabFixture fixture;
    const QString contract = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Contract.pdf"), 6);
    pdf::PDFAppToolTab* tab = H::open(fixture, { contract });
    pdf::PDFAppManageToolGUI* gui = H::gui(tab);
    QVERIFY(gui);
    QVERIFY(H::waitPages(gui, 6));
    const QColor subtle = pdf::PDFThemeManager::getInstance()->getColor(pdf::PDFThemeColors::BgSubtle);
    const QColor surface = pdf::PDFThemeManager::getInstance()->getColor(pdf::PDFThemeColors::BgSurface);
    QVERIFY(subtle != surface);

    gui->setSelection(H::items(gui, { 5 }));
    gui->addPagesOfFile(contract);
    AppToolOwnTabFixture::settle();
    pdf::PDFAppManageAddPagesDialog* dialog = qobject_cast<pdf::PDFAppManageAddPagesDialog*>(gui->getDialog());
    QVERIFY(dialog);
    QCOMPARE(F::edgeColor(dialog->getFacts()), subtle);
    QVERIFY(!dialog->getRangeField()->getStatusLabel()->isVisibleTo(dialog));

    // "1-": every page, labels 1 - 6
    const pdf::PDFAppInsertSourceModel* model = dialog->getPreviewModel();
    QCOMPARE(model->rowCount(), 6);
    QCOMPARE(model->data(model->index(0, 0), Qt::DisplayRole).toString(), QStringLiteral("1"));
    QCOMPARE(model->data(model->index(5, 0), Qt::DisplayRole).toString(), QStringLiteral("6"));

    // The order of the range typed, a page twice is two tiles
    dialog->getRangeField()->getField()->selectAll();
    QTest::keyClicks(dialog->getRangeField()->getField(), QStringLiteral("5, 2-3, 2"));
    QStringList labels;
    for (int row = 0; row < model->rowCount(); ++row)
    {
        labels << model->data(model->index(row, 0), Qt::DisplayRole).toString();
    }
    QCOMPARE(labels, QStringList({ QStringLiteral("5"), QStringLiteral("2"), QStringLiteral("3"), QStringLiteral("2") }));
    QCOMPARE(dialog->getStatusLabel()->text(), QStringLiteral("4 of 6 pages · added after the selection"));
    QCOMPARE(dialog->getAcceptButton()->text(), QStringLiteral("Add 4 Pages"));

    // 387:16764: the tile 53 x 84 (the box 41 x 56); the picture of the page comes
    QStyleOptionViewItem option;
    option.initFrom(dialog->getPreviewView());
    QCOMPARE(dialog->getPreviewView()->itemDelegate()->sizeHint(option, model->index(0, 0)), QSize(53, 84));
    QVERIFY(AppToolOwnTabFixture::waitFor([model]() { return model->data(model->index(0, 0), pdf::PDFComponentRoles::PixmapRole).isValid(); }));

    // An invalid range: no tiles, the error in the status of the dialog at the key, which made it
    dialog->getRangeField()->getField()->selectAll();
    QTest::keyClicks(dialog->getRangeField()->getField(), QStringLiteral("9"));
    QCOMPARE(model->rowCount(), 0);
    QCOMPARE(dialog->getStatusLabel()->getTextColor(), pdf::PDFThemeColors::TextDanger);
    QVERIFY(!dialog->getAcceptButton()->isEnabled());
    dialog->reject();
    AppToolOwnTabFixture::settle();

    // Properties: the facts on bg/subtle
    gui->openPropertiesDialog();
    AppToolOwnTabFixture::settle();
    pdf::PDFAppManagePropertiesDialog* properties = qobject_cast<pdf::PDFAppManagePropertiesDialog*>(gui->getDialog());
    QVERIFY(properties);
    QCOMPARE(F::edgeColor(properties->getValues()->parentWidget()), subtle);
    properties->reject();
    AppToolOwnTabFixture::settle();

    // Deletion order: the dialog deleted while its pictures are being made (the view, the model,
    // the delegate and the scheduler are its children; the workers hold no part of it)
    gui->addPagesOfFile(contract);
    AppToolOwnTabFixture::settle();
    QPointer<pdf::PDFAppManageAddPagesDialog> second = qobject_cast<pdf::PDFAppManageAddPagesDialog*>(gui->getDialog());
    QVERIFY(second);
    delete second.data();
    AppToolOwnTabFixture::settle();
    QTest::qWait(100);
    QVERIFY(!gui->getDialog());
    QCOMPARE(gui->getAssembly().getItems().size(), size_t(6));
}

void AppToolManageTest::startPageEditInItsTab()
{
    // QB1, owner answer 8 of 12. 11.: the start page of Manage Pages with exactly one open document
    // in its list offers "Edit <document> in its tab", which opens the tool in the tab of that
    // document (it changes the document); another input, a file, or no input: no link. Merge has
    // no host in the tab of a document: no link. The own tab has no such callout any more.
    AppToolOwnTabFixture fixture;
    pdf::PDFAppDocumentTab* documentTab = fixture.open(AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Invoice.pdf"), 3));
    QVERIFY(documentTab);
    QVERIFY(AppToolOwnTabFixture::waitFor([documentTab]() { return documentTab->getDocument()->isReady(); }));
    const QString other = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Other.pdf"), 2);

    fixture.navigate(QStringLiteral("/tools/pages.merge"));
    AppToolOwnTabFixture::settle();
    pdf::PDFToolStartPage* mergePage = fixture.page<pdf::PDFToolStartPage>();
    QVERIFY(mergePage);
    QVERIFY(!mergePage->getEditInTabLink());

    fixture.navigate(QStringLiteral("/tools/pages.manage"));
    AppToolOwnTabFixture::settle();
    pdf::PDFToolStartPage* page = fixture.page<pdf::PDFToolStartPage>();
    QVERIFY(page && page->getEditInTabLink() && page->getInputList());
    QVERIFY(!page->getEditInTabLink()->isVisibleTo(page));

    const pdf::PDFAppToolInputList::AddResult added = page->getInputList()->addOpenDocument(documentTab->getDocument());
    QVERIFY(!added.id.isEmpty());
    AppToolOwnTabFixture::settle();
    QVERIFY(page->getEditInTabLink()->isVisibleTo(page));
    QCOMPARE(page->getEditInTabLink()->text(), QStringLiteral("Edit %1 in its tab").arg(documentTab->getDocument()->getTitle()));
    QCOMPARE(page->getEditInTabDocumentId(), documentTab->getDocumentId());

    // A second input: the own tab works with both, no link
    const pdf::PDFAppToolInputList::AddResult file = page->getInputList()->addFile(other);
    QVERIFY(!file.id.isEmpty());
    AppToolOwnTabFixture::settle();
    QVERIFY(!page->getEditInTabLink()->isVisibleTo(page));
    page->getInputList()->removeInput(file.id);
    AppToolOwnTabFixture::settle();
    QVERIFY(page->getEditInTabLink()->isVisibleTo(page));

    // The link opens the tool in the tab of the document
    QTest::mouseClick(page->getEditInTabLink(), Qt::LeftButton);
    QVERIFY(AppToolOwnTabFixture::waitFor([documentTab]() { return AppToolOwnTabFixture::getShell(documentTab) != nullptr; }));
    pdf::PDFAppManageToolGUI* gui = qobject_cast<pdf::PDFAppManageToolGUI*>(AppToolOwnTabFixture::getShell(documentTab)->getGUI());
    QVERIFY(gui);
    QVERIFY(gui->isDocumentHost());

    // The own tab of one open document: no callout "Edit … in its tab" in the right panel
    pdf::PDFAppToolInputList* list = new pdf::PDFAppToolInputList(nullptr);
    list->addOpenDocument(documentTab->getDocument());
    pdf::PDFAppToolTab* tab = fixture.createToolTab(H::toolId(), list);
    AppToolOwnTabFixture::settle();
    pdf::PDFAppManageToolGUI* ownGui = H::gui(tab);
    QVERIFY(ownGui);
    QVERIFY(H::waitPages(ownGui, 3));
    QVERIFY(ownGui->getResultPanel()->getContent().calloutTitle.isEmpty());
}

void AppToolManageTest::figmaTextsOfTheFixWave()
{
    // RS3BFIGMA Lows: the rotation "90°" (387:16947 Properties, the row Rotation of Selection), the
    // estimate "about 93 kB" in whole units (387:13277 "about 11 MB")
    AppToolOwnTabFixture fixture;
    const QString contract = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Contract.pdf"), 3);
    pdf::PDFAppToolTab* tab = H::open(fixture, { contract });
    pdf::PDFAppManageToolGUI* gui = H::gui(tab);
    QVERIFY(gui);
    QVERIFY(H::waitPages(gui, 3));

    const QRegularExpression estimate(QStringLiteral("^about \\d+ (bytes|kB|MB|GB)$"));
    const QString size = H::resultValue(gui, QStringLiteral("Estimated size"));
    QVERIFY2(estimate.match(size).hasMatch(), qPrintable(size));

    gui->setSelection(H::items(gui, { 1 }));
    gui->rotateRight();
    AppToolOwnTabFixture::settle();
    const std::vector<std::pair<QString, QString>>& rows = gui->getResultPanel()->getContent().selectionRows;
    QVERIFY(std::find(rows.cbegin(), rows.cend(), std::make_pair(QStringLiteral("Rotation"), QStringLiteral("90°"))) != rows.cend());
    gui->openPropertiesDialog();
    AppToolOwnTabFixture::settle();
    pdf::PDFAppManagePropertiesDialog* properties = qobject_cast<pdf::PDFAppManagePropertiesDialog*>(gui->getDialog());
    QVERIFY(properties);
    const pdf::PDFKeyValueList* values = properties->getValues();
    QCOMPARE(values->getKey(values->getRowCount() - 1), QStringLiteral("Rotation"));
    QCOMPARE(values->getValue(values->getRowCount() - 1), QStringLiteral("90°"));
    properties->reject();
    AppToolOwnTabFixture::settle();

    gui->rotateRight();
    gui->rotateRight();
    AppToolOwnTabFixture::settle();
    const std::vector<std::pair<QString, QString>>& turned = gui->getResultPanel()->getContent().selectionRows;
    QVERIFY(std::find(turned.cbegin(), turned.cend(), std::make_pair(QStringLiteral("Rotation"), QStringLiteral("270°"))) != turned.cend());

    // 405:31588 "F6 moves between toolbar, panels and pages": from the pages to the right panel,
    // the toolbar (it wraps), the left panel; Shift+F6 goes back
    fixture.window->activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(fixture.window));
    QWidget* pages = gui->getCanvas()->getPageView();
    pages->setFocus(Qt::OtherFocusReason);
    AppToolOwnTabFixture::settle();
    QCOMPARE(QApplication::focusWidget(), pages);
    QCOMPARE(gui->getKeyAction(QStringLiteral("focusNextPart"))->shortcut(), QKeySequence(Qt::Key_F6));
    QCOMPARE(gui->getKeyAction(QStringLiteral("focusPreviousPart"))->shortcut(), QKeySequence(Qt::SHIFT | Qt::Key_F6));
    const auto isIn = [](const QWidget* part)
    {
        const QWidget* focus = QApplication::focusWidget();
        return focus && (focus == part || part->isAncestorOf(focus));
    };
    QWidget* toolActions = gui->getAction(QStringLiteral("add"))->parentWidget();
    gui->getKeyAction(QStringLiteral("focusNextPart"))->trigger();
    QVERIFY(isIn(gui->getResultPanel()));
    gui->getKeyAction(QStringLiteral("focusNextPart"))->trigger();
    QVERIFY(isIn(toolActions));
    gui->getKeyAction(QStringLiteral("focusNextPart"))->trigger();
    QVERIFY(isIn(gui->getSourcesPanel()));
    gui->getKeyAction(QStringLiteral("focusPreviousPart"))->trigger();
    QVERIFY(isIn(toolActions));
    gui->getKeyAction(QStringLiteral("focusPreviousPart"))->trigger();
    QVERIFY(isIn(gui->getResultPanel()));
    gui->getKeyAction(QStringLiteral("focusPreviousPart"))->trigger();
    QCOMPARE(QApplication::focusWidget(), pages);
}

void AppToolManageTest::renameUnchangedGivesNoName()
{
    // Owner Q5 / F3 of the fix wave: Rename starts with the name shown ("Contract.pdf", the file
    // name of an unnamed item); accepted unchanged it is no new name - the item keeps no own name
    // (its caption and the parts of the outline stay derived) and there is no undo step. A changed
    // name is the own name with its step "Rename".
    AppToolOwnTabFixture fixture;
    const QString contract = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Contract.pdf"), 3);
    pdf::PDFAppToolTab* tab = H::open(fixture, { contract });
    pdf::PDFAppManageToolGUI* gui = H::gui(tab);
    QVERIFY(gui);
    QVERIFY(H::waitPages(gui, 3));
    QVERIFY(gui->getModel()->getUndoText().isEmpty());

    gui->setSelection(H::items(gui, { 1 }));
    gui->openRenameDialog();
    AppToolOwnTabFixture::settle();
    pdf::PDFAppManageTextDialog* rename = qobject_cast<pdf::PDFAppManageTextDialog*>(gui->getDialog());
    QVERIFY(rename);
    QCOMPARE(rename->getField()->text(), QStringLiteral("Contract.pdf"));
    rename->triggerAccept();
    AppToolOwnTabFixture::settle();
    QVERIFY(gui->getAssembly().getItems()[1].name.isEmpty());
    QVERIFY(gui->getModel()->getUndoText().isEmpty());

    gui->rename(QStringLiteral("Site plan"));
    AppToolOwnTabFixture::settle();
    QCOMPARE(gui->getAssembly().getItems()[1].name, QStringLiteral("Site plan"));
    QCOMPARE(gui->getModel()->getUndoText(), QStringLiteral("Rename"));

    // An own name accepted unchanged: no second step
    gui->rename(QStringLiteral("Site plan"));
    AppToolOwnTabFixture::settle();
    gui->getModel()->undo();
    AppToolOwnTabFixture::settle();
    QVERIFY(gui->getAssembly().getItems()[1].name.isEmpty());
}

void AppToolManageTest::secondLocateSaysBusy()
{
    // RS3BAPP Low, 387:16980: the loader checks one located file at a time; Locate… during a check
    // says so in the status bar instead of doing nothing (no file dialog, the check goes on)
    AppToolOwnTabFixture fixture;
    const QString located = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Annex_found.pdf"), 2);
    const QString assemblyPath = fixture.folder() + QStringLiteral("/Missing.pdf4qtassembly");
    QVERIFY(H::writeAssembly(assemblyPath, fixture.folder() + QStringLiteral("/Annex_A.pdf"), 2));

    pdf::PDFAppToolTab* tab = H::open(fixture, { });
    pdf::PDFAppManageToolGUI* gui = H::gui(tab);
    QVERIFY(gui);
    QVERIFY(gui->openAssemblyFile(assemblyPath));
    QVERIFY(AppToolOwnTabFixture::waitFor([gui]() { return gui->getSourcesDialog() != nullptr; }));
    QPointer<pdf::PDFAppManageSourcesDialog> dialog = gui->getSourcesDialog();
    pdf::PDFAppAssemblyLoader* loader = gui->findChild<pdf::PDFAppAssemblyLoader*>();
    QVERIFY(loader);

    // The check of the first located file waits in the pool
    QThreadPool* pool = QThreadPool::globalInstance();
    const int maximumThreads = pool->maxThreadCount();
    QSemaphore release;
    QSemaphore started;
    pool->setMaxThreadCount(1);
    pool->start([&release, &started]() { started.release(); release.acquire(); });
    QVERIFY(started.tryAcquire(1, 30000 * AppToolOwnTabFixture::getTimeoutFactor()));
    QVERIFY(loader->locate(dialog->getResult(), QStringLiteral("s1"), located));

    // The second Locate… of the dialog
    Q_EMIT dialog->locateRequested(QStringLiteral("s1"));
    AppToolOwnTabFixture::settle();
    QCOMPARE(tab->getToolPage()->getTabView()->getStatusBar()->getMessage(), pdf::PDFAppManageToolGUI::getLocateBusyText());
    QCOMPARE(pdf::PDFAppManageToolGUI::getLocateBusyText(), QStringLiteral("Another file is being checked – choose this one when the check ends"));
    QVERIFY(loader->isLoading());

    release.release();
    QVERIFY(AppToolOwnTabFixture::waitFor([loader]() { return !loader->isLoading(); }));
    AppToolOwnTabFixture::settle();
    pool->waitForDone();
    pool->setMaxThreadCount(maximumThreads);
    QVERIFY(dialog);
    QVERIFY(dialog->getOpenButton()->isEnabled());
    dialog->getCancelButton()->click();
    AppToolOwnTabFixture::settle();
}

void AppToolManageTest::documentHostStatusBar()
{
    // 1479:34632 (fix wave 3b, F4 item 1): in the document host the page item of the status bar
    // says the pages of the assembly ("12 pages") instead of "Page 1 / 10"; the zoom item stays
    // the zoom of the view ("151 %"). The text follows the assembly and ends with the tool. A
    // tool without its own page item (Rotate Pages, 293:1407 "Page 308 / 1592") keeps the page
    // item of the view.
    AppToolOwnTabFixture fixture;
    pdf::PDFAppDocumentTab* documentTab = fixture.open(AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Invoice.pdf"), 10));
    QVERIFY(documentTab);
    QVERIFY(AppToolOwnTabFixture::waitFor([documentTab]() { return documentTab->getDocument()->isReady(); }));
    pdf::PDFAppDocumentView* view = documentTab->getView();
    pdf::PDFAppDocumentStatusBar* statusBar = view->getStatusBar();
    QVERIFY(statusBar);
    const QString viewPageText = statusBar->getPageText();

    pdf::PDFDocumentToolShell* rotateShell = fixture.openTool(documentTab, QStringLiteral("pages.rotate"));
    QVERIFY(rotateShell);
    AppToolOwnTabFixture::settle();
    QCOMPARE(statusBar->getPageText(), QStringLiteral("Page 1 / 10"));
    rotateShell->cancel();
    QVERIFY(AppToolOwnTabFixture::waitFor([documentTab]() { return !AppToolOwnTabFixture::getShell(documentTab); }));
    QCOMPARE(statusBar->getPageText(), viewPageText);

    pdf::PDFDocumentToolShell* shell = fixture.openTool(documentTab, H::toolId());
    QVERIFY(shell);
    pdf::PDFAppManageToolGUI* gui = qobject_cast<pdf::PDFAppManageToolGUI*>(shell->getGUI());
    QVERIFY(gui);
    AppToolOwnTabFixture::settle();
    QVERIFY(gui->isDocumentHost());
    QCOMPARE(statusBar->getPageText(), QStringLiteral("10 pages"));
    QVERIFY(statusBar->getPageItem()->isVisibleTo(statusBar));
    QCOMPARE(statusBar->getZoomText(), QStringLiteral("%1 %").arg(pdf::PDFAppLocale::formatNumber(qRound(view->getZoom() * 100.0))));

    // Two pages into the bin: the item follows the assembly
    gui->setSelection(H::items(gui, { 0, 1 }));
    gui->removeSelection();
    AppToolOwnTabFixture::settle();
    QCOMPARE(statusBar->getPageText(), QStringLiteral("8 pages"));

    // The end of the tool gives the page item back to the view; Rotate Pages shows its own again
    shell->cancel();
    QVERIFY(AppToolOwnTabFixture::waitFor([documentTab]() { return !AppToolOwnTabFixture::getShell(documentTab); }));
    AppToolOwnTabFixture::settle();
    QVERIFY(!view->isToolModeActive());
    QCOMPARE(statusBar->getPageText(), viewPageText);
    QVERIFY(view->getToolPageText().isEmpty());
    rotateShell = fixture.openTool(documentTab, QStringLiteral("pages.rotate"));
    QVERIFY(rotateShell);
    AppToolOwnTabFixture::settle();
    QCOMPARE(statusBar->getPageText(), QStringLiteral("Page 1 / 10"));
    rotateShell->cancel();
    AppToolOwnTabFixture::settle();
}

void AppToolManageTest::detailsRotationText()
{
    // 387:15072, the column Rotation: "None" and "90°" (the turn clockwise in degrees, not the
    // "90° right" of Merge); the same text as Selection and Properties (387:13234, 387:16947)
    AppToolOwnTabFixture fixture;
    const QString file = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Annex_A.pdf"), 3);
    pdf::PDFAppToolTab* tab = H::open(fixture, { file });
    pdf::PDFAppManageToolGUI* gui = H::gui(tab);
    QVERIFY(gui);
    QVERIFY(H::waitPages(gui, 3));
    gui->setSelection(H::items(gui, { 1 }));
    gui->rotateRight();
    AppToolOwnTabFixture::settle();

    const pdf::PDFAppManageItemModel* items = gui->getCanvas()->getItemModel();
    const auto rotation = [items](int row) { return items->data(items->index(row, pdf::PDFAppManageItemModel::RotationColumn), Qt::DisplayRole).toString(); };
    QCOMPARE(rotation(0), QStringLiteral("None"));
    QCOMPARE(rotation(1), QStringLiteral("90°"));
    QCOMPARE(rotation(2), QStringLiteral("None"));
    gui->rotateRight();
    AppToolOwnTabFixture::settle();
    QCOMPARE(rotation(1), QStringLiteral("180°"));
    QCOMPARE(pdf::PDFAppManageItemModel::getRotationText(pdf::PageRotation::Rotate270), QStringLiteral("270°"));
    QCOMPARE(pdf::PDFAppManageItemModel::getRotationText(pdf::PageRotation::None), QStringLiteral("None"));
}

void AppToolManageTest::emptyStateOpenDocumentRow()
{
    // 409:32954 (fix wave 3b, F4 item 4): the row of an open document in "Start a new assembly" -
    // 324 x 52, the tile tab 30 at 8, 11 on bg/accent-subtle, the name at 48, the line 16 below
    // it, the Secondary "+ Add" 86 x 36 at 8 from the trailing edge; the row of Insert and Merge.
    // A long name is elided in the middle (".pdf" stays) with the whole name in its tooltip;
    // Add adds the document (not a click into the row).
    AppToolOwnTabFixture fixture;
    const QString longName = QStringLiteral("Tender_2026_annex_with_a_very_long_name_of_the_final_signed_version.pdf");
    pdf::PDFAppDocumentTab* invoiceTab = fixture.open(AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Invoice.pdf"), 3));
    pdf::PDFAppDocumentTab* longTab = fixture.open(AppDocumentSamples::simple(fixture.folder(), longName, 2));
    QVERIFY(invoiceTab && longTab);
    QVERIFY(AppToolOwnTabFixture::waitFor([invoiceTab, longTab]() { return invoiceTab->getDocument()->isReady() && longTab->getDocument()->isReady(); }));
    pdf::PDFAppToolTab* tab = H::open(fixture, { });
    pdf::PDFAppManageToolGUI* gui = H::gui(tab);
    QVERIFY(gui);
    AppToolOwnTabFixture::settle();
    QWidget* emptyState = gui->getCanvas()->getEmptyState();
    QVERIFY(emptyState);

    for (const bool isRightToLeft : { false, true })
    {
        QGuiApplication::setLayoutDirection(isRightToLeft ? Qt::RightToLeft : Qt::LeftToRight);
        AppToolOwnTabFixture::settle();
        pdf::PDFAppToolOpenDocumentRow* row = emptyState->findChild<pdf::PDFAppToolOpenDocumentRow*>(QStringLiteral("manageEmptyDocument-") + invoiceTab->getDocument()->getId());
        QVERIFY(row);
        QCOMPARE(row->size(), QSize(324, 52));
        const auto place = [row](const QWidget* widget) { return QStyle::visualRect(row->layoutDirection(), row->rect(), widget->geometry()); };
        const pdf::PDFIconBadge* tile = row->findChild<pdf::PDFIconBadge*>();
        QVERIFY(tile);
        QCOMPARE(tile->getIconName(), QStringLiteral("tab"));
        QCOMPARE(place(tile), QRect(8, 11, 30, 30));
        QCOMPARE(place(row->getNameLabel()).left(), 48);
        QCOMPARE(row->getNameLabel()->text(), QStringLiteral("Invoice.pdf"));
        QCOMPARE(row->getDetailsLabel()->text(), QStringLiteral("3 pages"));
        QCOMPARE(row->getDetailsLabel()->y() - row->getNameLabel()->y(), 16);
        QCOMPARE(place(row->getAddButton()), QRect(230, 8, 86, 36));
        QCOMPARE(row->getAddButton()->text(), QStringLiteral("Add"));
        QVERIFY(!row->getTag());
    }
    QGuiApplication::setLayoutDirection(Qt::LeftToRight);
    AppToolOwnTabFixture::settle();

    // The long name keeps its end and its extension; the whole name is the tooltip
    pdf::PDFAppToolOpenDocumentRow* longRow = emptyState->findChild<pdf::PDFAppToolOpenDocumentRow*>(QStringLiteral("manageEmptyDocument-") + longTab->getDocument()->getId());
    QVERIFY(longRow);
    const QString shown = longRow->getNameLabel()->text();
    QVERIFY2(shown != longName && shown.contains(QChar(0x2026)) && shown.endsWith(QStringLiteral("version.pdf")), qPrintable(shown));
    QCOMPARE(longRow->getNameLabel()->toolTip(), longName);
    QVERIFY(longRow->getNameLabel()->geometry().right() < longRow->getAddButton()->geometry().left());

    // A click into the row adds nothing, Add adds Invoice.pdf
    pdf::PDFAppToolOpenDocumentRow* row = emptyState->findChild<pdf::PDFAppToolOpenDocumentRow*>(QStringLiteral("manageEmptyDocument-") + invoiceTab->getDocument()->getId());
    QTest::mouseClick(row, Qt::LeftButton, Qt::NoModifier, QPoint(150, 26));
    AppToolOwnTabFixture::settle();
    QVERIFY(gui->getAssembly().isEmpty());
    row->getAddButton()->click();
    QVERIFY(H::waitPages(gui, 3));
    AppToolOwnTabFixture::settle();
    QVERIFY(!gui->getCanvas()->getEmptyState());
}

QTEST_MAIN(AppToolManageTest)

#include "tst_apptoolmanagetest.moc"
