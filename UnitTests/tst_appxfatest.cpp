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

#include "appdocumentfixture.h"
#include "appmcpfixture.h"
#include "testdocumenttool.h"
#include "xfatestsamples.h"

#include "pdfappannotationcontroller.h"
#include "pdfappautorefresh.h"
#include "pdfappcommand.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentdialogs.h"
#include "pdfappdocumentinfobar.h"
#include "pdfappdocumentpanels_thumbnails.h"
#include "pdfappdocumentpropertiespage.h"
#include "pdfappdocumentsave.h"
#include "pdfappdocumentsidebar.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumenttoolbar.h"
#include "pdfappdocumentview.h"
#include "pdfappfiledrop.h"
#include "pdfappformcontroller.h"
#include "pdfappmainwindow.h"
#include "pdfappmcphost.h"
#include "pdfappmcppermissions.h"
#include "pdfappnavigator.h"
#include "pdfappoutlineeditor.h"
#include "pdfappprint.h"
#include "pdfappprintdialog.h"
#include "pdfapproute.h"
#include "pdfappundostack.h"
#include "pdfmcprepository.h"

#include "pdfcatalog.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumenttool.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfform.h"
#include "pdfthemecolors.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalSpy>
#include <QThreadPool>
#include <QtTest>

/// Samples and helpers of the tests of step 8 (paging of XFA forms)
class XFATestHelper
{
public:
    XFATestHelper() = delete;

    /// Writes a document with the PDF pages \p pdfPages and the XFA template \p xml
    static QString writeSample(const QString& folder, const QString& name, const std::vector<QSizeF>& pdfPages, const QByteArray& xml)
    {
        pdf::PDFDocument document = pdftest::XFATestSamples::createDocument(pdfPages, xml);
        const QString filePath = QDir(folder).absoluteFilePath(name);
        return pdftest::AppDocumentSamples::write(document, filePath) ? filePath : QString();
    }

    /// One letter page in the file, three pages of the layout
    static QString dynamicForm(const QString& folder, const QString& name = QStringLiteral("dynamic-xfa.pdf"))
    {
        return writeSample(folder, name, { pdftest::XFATestSamples::letter() }, pdftest::XFATestSamples::pagedTemplate(3));
    }

    /// Two letter pages in the file and in the layout
    static QString staticForm(const QString& folder)
    {
        return writeSample(folder, QStringLiteral("static-xfa.pdf"), { pdftest::XFATestSamples::letter(), pdftest::XFATestSamples::letter() },
                           pdftest::XFATestSamples::pagedTemplate(2));
    }

    static QByteArray readFile(const QString& filePath)
    {
        QFile file(filePath);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

    static bool replaceFile(const QString& filePath, const QByteArray& data)
    {
        for (int attempt = 0; attempt < 20; ++attempt)
        {
            QSaveFile file(filePath);
            if (file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit())
            {
                return true;
            }
            QTest::qWait(100);
        }
        return false;
    }

    static int getPageCount(const QString& filePath)
    {
        pdf::PDFDocumentReader reader(nullptr, nullptr, false, false);
        const pdf::PDFDocument document = reader.readFromFile(filePath);
        return reader.getReadingResult() == pdf::PDFDocumentReader::Result::OK ? int(document.getCatalog()->getPageCount()) : -1;
    }

    static pdf::PDFAppDocumentTab* openReady(pdftest::AppDocumentFixture& fixture, const QString& filePath, int page = 0)
    {
        pdf::PDFAppDocumentTab* tab = fixture.open(filePath, page);
        if (!tab)
        {
            return nullptr;
        }
        QPointer<pdf::PDFAppDocumentTab> guard(tab);
        if (!QTest::qWaitFor([&guard]() { return !guard || (guard->getDocument() && guard->getDocument()->isReady() && guard->getView()->getProxy()->getDocument()); },
                             20000 * pdftest::AppDocumentFixture::getTimeoutFactor()))
        {
            return nullptr;
        }
        pdftest::AppDocumentFixture::settle();
        return guard.data();
    }

    static const pdf::PDFAppInfoBarRequest* xfaBar(pdf::PDFAppDocumentTab* tab)
    {
        return tab->getView()->findInfoBar(pdf::PDFAppFormController::getXFABarId());
    }

    static int countLeakPixels(const QImage& image)
    {
        const QRgb leak = pdf::PDFThemeColors::getLeakColor().rgb() & 0xFFFFFF;
        int count = 0;
        const QImage converted = image.convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < converted.height(); ++y)
        {
            const QRgb* line = reinterpret_cast<const QRgb*>(converted.constScanLine(y));
            for (int x = 0; x < converted.width(); ++x)
            {
                if ((line[x] & 0xFFFFFF) == leak && qAlpha(line[x]) > 0)
                {
                    ++count;
                }
            }
        }
        return count;
    }

    /// Number of the dark pixels (a text) in the rectangle of the picture
    static int countDarkPixels(const QImage& image, const QRect& rectangle)
    {
        const QImage converted = image.convertToFormat(QImage::Format_ARGB32);
        const QRect area = rectangle.intersected(QRect(QPoint(0, 0), QSize(qRound(converted.width() / converted.devicePixelRatio()), qRound(converted.height() / converted.devicePixelRatio()))));
        int count = 0;
        const qreal ratio = converted.devicePixelRatio();
        for (int y = qFloor(area.top() * ratio); y < qCeil((area.bottom() + 1) * ratio) && y < converted.height(); ++y)
        {
            for (int x = qFloor(area.left() * ratio); x < qCeil((area.right() + 1) * ratio) && x < converted.width(); ++x)
            {
                if (qGray(converted.pixel(x, y)) < 100)
                {
                    ++count;
                }
            }
        }
        return count;
    }

    /// A change of the title of the document (refused for a display version)
    static bool commitTitle(pdf::PDFAppDocument* document, const QString& title)
    {
        pdf::PDFDocumentBuilder builder(document->getDocument().data());
        builder.setDocumentTitle(title);
        pdf::PDFDocumentPointer pointer(new pdf::PDFDocument(builder.build()));
        return document->commit(pdf::PDFModifiedDocument(pointer, nullptr, pdf::PDFModifiedDocument::None), title, document->getVersion(), pdf::PDFAppChangeSource::User);
    }

    static QJsonObject json(const char* text)
    {
        return QJsonDocument::fromJson(QByteArray(text)).object();
    }

    static QJsonObject structured(const std::optional<pdf::PDFMcpCallResult>& result)
    {
        return result ? result->toQJson().value(QStringLiteral("structuredContent")).toObject() : QJsonObject();
    }

    static bool isError(const std::optional<pdf::PDFMcpCallResult>& result)
    {
        return !result || result->kind != pdf::PDFMcpCallResult::Kind::Complete || result->isError;
    }

    static QString text(const std::optional<pdf::PDFMcpCallResult>& result)
    {
        return result ? (result->kind == pdf::PDFMcpCallResult::Kind::Error ? result->errorMessage : result->getFirstText()) : QString();
    }

    static QString pagedText()
    {
        return QStringLiteral("The pages of this XFA form are shown as laid out. The document cannot be changed.");
    }
};

class AppXfaTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();

    void openShowsLayoutPages();
    void staticFormStaysEditable();
    void editPathsRefused();
    void saveAsWritesOriginal();
    void copyWritesOriginal();
    void printPageCount();
    void autoRefreshPagesAgain();
    void hostileForms();
    void deletedWhileReading_data();
    void deletedWhileReading();
    void infoBarThemesAndDirection();
    void mcpReadsAndRefusals();
    void reviewLockPopoverWithoutCopy();
    void reviewMcpCatalogToolRefused();
};

void AppXfaTest::initTestCase()
{
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
}

void AppXfaTest::cleanup()
{
    QApplication::setLayoutDirection(Qt::LeftToRight);
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
    pdf::PDFAppFileDialogs::setSaveFileHandler(pdf::PDFAppFileDialogs::SaveFileHandler());
    pdf::PDFAppPrinters::setProvider(pdf::PDFAppPrinters::Provider());
    QThreadPool::globalInstance()->waitForDone(30000);
}

void AppXfaTest::openShowsLayoutPages()
{
    using H = XFATestHelper;
    pdftest::AppDocumentFixture fixture;
    const QString file = H::dynamicForm(fixture.folder());
    QCOMPARE(H::getPageCount(file), 1);
    const QByteArray original = H::readFile(file);

    // The page of the route is a page of the layout (beyond the pages of the file)
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, file, 3);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    QVERIFY(document->isPagedXFA());
    QVERIFY(!document->isEditable());
    QCOMPARE(document->getPageCount(), pdf::PDFInteger(3));
    QTRY_COMPARE(tab->getView()->getCurrentPageIndex(), pdf::PDFInteger(2));

    // Opening does not change the document
    QVERIFY(!document->isModified());
    QCOMPARE(document->getUnsavedChangeCount(), 0);
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 0);
    QCOMPARE(document->getOriginalData(), original);
    QVERIFY(document->getSnapshot().isPagedXFA);
    QCOMPARE(H::readFile(file), original);

    // The pages of the layout: media boxes, the page field, the thumbnails
    for (pdf::PDFInteger i = 0; i < 3; ++i)
    {
        QCOMPARE(document->getDocument()->getCatalog()->getPage(i)->getMediaBox().size(), pdftest::XFATestSamples::letter());
    }
    QCOMPARE(tab->getView()->getToolbar()->getPageField()->getSuffix(), QStringLiteral("/ 3"));
    pdf::PDFAppDocumentSidebar* sidebar = tab->getView()->getSidebar();
    QVERIFY(sidebar && sidebar->showPanel(QStringLiteral("thumbnails")));
    pdf::PDFAppThumbnailsPanel* thumbnails = qobject_cast<pdf::PDFAppThumbnailsPanel*>(sidebar->findPanel(QStringLiteral("thumbnails")));
    QVERIFY(thumbnails);
    QTRY_COMPARE(thumbnails->getModel()->rowCount(), 3);

    // The form manager draws the layout of the shown document: the text of the
    // third layout page on the third page (a page, which the file does not have)
    QWidget* canvas = pdftest::AppDocumentFixture::canvasWidget(tab->getView());
    QVERIFY(canvas);
    bool okTopLeft = false;
    bool okBottomRight = false;
    const QPoint topLeft = pdftest::AppDocumentFixture::pageToWidget(tab->getView(), 2, QPointF(18.0, 792.0 - 18.0), &okTopLeft);
    const QPoint bottomRight = pdftest::AppDocumentFixture::pageToWidget(tab->getView(), 2, QPointF(234.0, 792.0 - 60.0), &okBottomRight);
    QVERIFY(okTopLeft && okBottomRight);
    QTRY_VERIFY_WITH_TIMEOUT(H::countDarkPixels(canvas->grab().toImage(), QRect(topLeft, bottomRight).normalized()) > 20, 10000);

    // The XFA bar says, that the document is a display version
    QTRY_VERIFY(H::xfaBar(tab));
    QCOMPARE(H::xfaBar(tab)->title, QStringLiteral("This form cannot be filled in"));
    QCOMPARE(H::xfaBar(tab)->text, QStringLiteral("PDF4QT shows the pages of this XFA form as laid out. The document cannot be edited; Save As keeps the original file."));

    // The view state of Recent keeps a page of the layout: opened again on page 3
    QVERIFY(fixture.window->closeTab(tab));
    fixture.settle();
    pdf::PDFAppDocumentTab* again = H::openReady(fixture, file);
    QVERIFY(again);
    QVERIFY(again->getDocument()->isPagedXFA());
    QTRY_COMPARE(again->getView()->getCurrentPageIndex(), pdf::PDFInteger(2));
}

void AppXfaTest::staticFormStaysEditable()
{
    using H = XFATestHelper;
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::staticForm(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    QVERIFY(!document->isPagedXFA());
    QVERIFY(document->isEditable());
    QCOMPARE(document->getPageCount(), pdf::PDFInteger(2));
    QVERIFY(tab->getView()->getAnnotationController()->isAnnotationEditingAllowed());
    QVERIFY(tab->isCommandEnabled(pdf::PDFAppCommands::annotateHighlight()));
    QVERIFY(tab->isCommandEnabled(pdf::PDFAppCommands::editCopy()));
    QTRY_VERIFY(H::xfaBar(tab));
    QCOMPARE(H::xfaBar(tab)->text, QStringLiteral("PDF4QT shows XFA forms, but it does not fill them in."));

    // Not filled, but changed as any document
    QVERIFY(!tab->getView()->getAnnotationController()->isFormFillingAllowed());
    QVERIFY(H::commitTitle(document, QStringLiteral("Changed")));
    QVERIFY(document->isModified());
}

void AppXfaTest::editPathsRefused()
{
    using H = XFATestHelper;
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::dynamicForm(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppDocumentView* view = tab->getView();
    QVERIFY(document->isPagedXFA());
    QCOMPARE(pdf::PDFAppDocument::getPagedXFARefusalText(), H::pagedText());

    // Annotations, their tools, Insert and Notes (the annotation controller)
    pdf::PDFAppAnnotationController* controller = view->getAnnotationController();
    QVERIFY(!controller->isAnnotationEditingAllowed());
    QVERIFY(!controller->isFormFillingAllowed());
    QCOMPARE(controller->getEditingRefusalReason(), H::pagedText());
    for (const QString& command : { pdf::PDFAppCommands::annotatePanel(), pdf::PDFAppCommands::insertPanel(), pdf::PDFAppCommands::annotateHighlight(),
                                    pdf::PDFAppCommands::annotateDeleteTool(), pdf::PDFAppCommands::rotatePagesLeft(), pdf::PDFAppCommands::rotatePagesRight(),
                                    pdf::PDFAppCommands::editCopy(), pdf::PDFAppCommands::undo(), pdf::PDFAppCommands::redo(), pdf::PDFAppCommands::save() })
    {
        QVERIFY2(!tab->isCommandEnabled(command), qPrintable(command));
    }
    QVERIFY(tab->isCommandEnabled(pdf::PDFAppCommands::saveAs()));
    QVERIFY(tab->isCommandEnabled(pdf::PDFAppCommands::print()));

    // The refusal of the controller itself (an engine change) names the reason
    QSignalSpy refusedSpy(controller, &pdf::PDFAppAnnotationController::changeRefused);
    pdf::PDFDocumentBuilder builder(document->getDocument().data());
    builder.setDocumentTitle(QStringLiteral("x"));
    pdf::PDFDocumentPointer changed(new pdf::PDFDocument(builder.build()));
    QVERIFY(!controller->commit(pdf::PDFModifiedDocument(changed, nullptr, pdf::PDFModifiedDocument::Annotation), pdf::PDFAppAnnotationChangeKind::Annotation, QStringLiteral("x")));
    QCOMPARE(refusedSpy.count(), 1);
    QCOMPARE(refusedSpy.at(0).at(0).toString(), H::pagedText());

    // Outline
    QVERIFY(!view->getOutlineEditor()->isEditingAllowed());
    QCOMPARE(view->getOutlineEditor()->getEditingRefusalReason(), H::pagedText());

    // Document properties
    fixture.window->getNavigator()->navigate(pdf::PDFAppRoutes::documentProperties(tab->getDocumentId(), QStringLiteral("general")));
    fixture.settle();
    pdf::PDFAppDocumentPropertiesPage* properties = qobject_cast<pdf::PDFAppDocumentPropertiesPage*>(tab->getPage());
    QVERIFY(properties);
    QVERIFY(!properties->isEditable());

    // The model refuses every change, also with the lock off
    const quint64 version = document->getVersion();
    QVERIFY(!document->isLocked());
    QVERIFY(!H::commitTitle(document, QStringLiteral("Changed")));
    QCOMPARE(document->getVersion(), version);
    QVERIFY(!document->isModified());

    // The lock keeps the refusal of the display version
    document->setLocked(true);
    QCOMPARE(controller->getEditingRefusalReason(), H::pagedText());
    document->setLocked(false);
    QVERIFY(!H::commitTitle(document, QStringLiteral("Changed")));
}

void AppXfaTest::saveAsWritesOriginal()
{
    using H = XFATestHelper;
    pdftest::AppDocumentFixture fixture;
    const QString file = H::dynamicForm(fixture.folder());
    const QByteArray original = H::readFile(file);
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, file);
    QVERIFY(tab);
    pdf::PDFAppDocumentFileCommandHandler* handler = pdf::PDFAppDocumentFileCommandHandler::find(tab);
    QVERIFY(handler);

    // Save As: the bytes of the original file (never the paged document)
    const QString target = fixture.path(QStringLiteral("saved-as.pdf"));
    pdf::PDFAppFileDialogs::setSaveFileHandler([target](QWidget*, const QString&) { return target; });
    QSignalSpy finishedSpy(handler, &pdf::PDFAppDocumentFileCommandHandler::saveFinished);
    QVERIFY(handler->saveAs());
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 20000);
    QVERIFY(finishedSpy.at(0).at(0).toBool());
    QCOMPARE(H::readFile(target), original);
    QCOMPARE(H::getPageCount(target), 1);

    // The tab follows the new file and stays a display version
    pdf::PDFAppDocument* document = tab->getDocument();
    QCOMPARE(document->getFilePath(), QFileInfo(target).canonicalFilePath());
    QVERIFY(document->isPagedXFA());
    QVERIFY(!document->isModified());
    QCOMPARE(document->getPageCount(), pdf::PDFInteger(3));
    QCOMPARE(document->getOriginalData(), original);

    // The writer itself: the original bytes of a request of a display version
    pdf::PDFAppDocumentFileWriter::Request request;
    request.document = document->getDocument();
    request.originalData = original;
    request.isOriginalData = true;
    request.isIncremental = true;
    request.filePath = fixture.path(QStringLiteral("written.pdf"));
    const pdf::PDFAppDocumentFileWriter::Result result = pdf::PDFAppDocumentFileWriter::write(request);
    QVERIFY(result.isSaved);
    QVERIFY(!result.isIncremental);
    QCOMPARE(result.data, original);
    QCOMPARE(H::readFile(request.filePath), original);

    // Without the original bytes nothing is written
    request.originalData.clear();
    request.filePath = fixture.path(QStringLiteral("empty.pdf"));
    QVERIFY(!pdf::PDFAppDocumentFileWriter::write(request).isSaved);
    QVERIFY(!QFileInfo::exists(request.filePath));
}

void AppXfaTest::copyWritesOriginal()
{
    using H = XFATestHelper;
    pdftest::AppDocumentFixture fixture;
    const QString file = H::dynamicForm(fixture.folder());
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, file);
    QVERIFY(tab);

    // A copy (the lock popover of a locked document) is a display version too
    pdf::PDFAppDocument copy(fixture.context.get(), nullptr);
    QVERIFY(copy.setCopyOf(tab->getDocument()));
    QVERIFY(copy.isPagedXFA());
    QVERIFY(copy.getSnapshot().isPagedXFA);
    QCOMPARE(copy.getPageCount(), pdf::PDFInteger(3));
    QVERIFY(!H::commitTitle(&copy, QStringLiteral("x")));
    QCOMPARE(copy.getOriginalData(), H::readFile(file));
}

void AppXfaTest::printPageCount()
{
    using H = XFATestHelper;
    pdftest::AppDocumentFixture fixture;
    const QString folder = fixture.folder();
    pdf::PDFAppPrinters::setProvider([folder]()
    {
        pdf::PDFAppPrinterInfo info;
        info.name = QStringLiteral("Test Printer");
        info.isDefault = true;
        info.stateText = QStringLiteral("Ready");
        info.outputFile = QDir(folder).absoluteFilePath(QStringLiteral("printed.pdf"));
        return std::vector<pdf::PDFAppPrinterInfo>{ info };
    });

    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::dynamicForm(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppDocumentFileCommandHandler* handler = pdf::PDFAppDocumentFileCommandHandler::find(tab);
    QVERIFY(handler->isEnabled(pdf::PDFAppCommands::print()));
    handler->execute(pdf::PDFAppCommands::print());
    pdf::PDFAppPrintController* controller = handler->getPrintController();
    pdf::PDFAppPrintDialog* dialog = controller->getDialog();
    QVERIFY(dialog);
    QCOMPARE(dialog->getSettings().pages.size(), size_t(3));

    QSignalSpy finishedSpy(controller, &pdf::PDFAppPrintController::printFinished);
    dialog->getPrintButton()->click();
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 60000);
    QVERIFY(controller->getLastResult().isCompleted);
    QCOMPARE(controller->getLastResult().printedCount, 3);
    QCOMPARE(H::getPageCount(fixture.path(QStringLiteral("printed.pdf"))), 3);
    QVERIFY(!tab->getDocument()->isModified());
}

void AppXfaTest::autoRefreshPagesAgain()
{
    using H = XFATestHelper;
    pdftest::AppDocumentFixture fixture;
    const QString file = H::dynamicForm(fixture.folder());
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, file);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppAutoRefresh* refresh = pdf::PDFAppDocumentFileCommandHandler::find(tab)->getAutoRefresh();
    QVERIFY(refresh->isEnabled());

    // Another program writes a form of 5 layout pages: paged again
    const QString five = H::writeSample(fixture.folder(), QStringLiteral("five.pdf"), { pdftest::XFATestSamples::a4() }, pdftest::XFATestSamples::pagedTemplate(5));
    QSignalSpy reloadedSpy(refresh, &pdf::PDFAppAutoRefresh::reloaded);
    QVERIFY(H::replaceFile(file, H::readFile(five)));
    if (!reloadedSpy.wait(5000))
    {
        refresh->checkNow();
    }
    QTRY_VERIFY_WITH_TIMEOUT(reloadedSpy.count() >= 1, 20000);
    QTRY_COMPARE(document->getPageCount(), pdf::PDFInteger(5));
    QVERIFY(document->isPagedXFA());
    QVERIFY(!document->isModified());
    QCOMPARE(document->getOriginalData(), H::readFile(file));
    QCOMPARE(document->getDocument()->getCatalog()->getPage(0)->getMediaBox().size(), pdftest::XFATestSamples::letter());

    // A static form: the document is editable again
    const int reloads = reloadedSpy.count();
    QVERIFY(H::replaceFile(file, H::readFile(H::staticForm(fixture.folder()))));
    if (!QTest::qWaitFor([&reloadedSpy, reloads]() { return reloadedSpy.count() > reloads; }, 5000))
    {
        refresh->checkNow();
    }
    QTRY_VERIFY_WITH_TIMEOUT(reloadedSpy.count() > reloads, 20000);
    QTRY_VERIFY(!document->isPagedXFA());
    QCOMPARE(document->getPageCount(), pdf::PDFInteger(2));
    QVERIFY(document->isEditable());
    QTRY_VERIFY(tab->getView()->getAnnotationController()->isAnnotationEditingAllowed());
    QTRY_COMPARE(H::xfaBar(tab)->text, QStringLiteral("PDF4QT shows XFA forms, but it does not fill them in."));
}

void AppXfaTest::hostileForms()
{
    using H = XFATestHelper;
    using S = pdftest::XFATestSamples;
    pdftest::AppDocumentFixture fixture;

    // A template, which is not XML: shown as the PDF file, editable
    pdf::PDFAppDocumentTab* garbage = H::openReady(fixture, H::writeSample(fixture.folder(), QStringLiteral("garbage.pdf"), { S::a4() }, S::garbageTemplate()));
    QVERIFY(garbage);
    QVERIFY(!garbage->getDocument()->isPagedXFA());
    QCOMPARE(garbage->getDocument()->getPageCount(), pdf::PDFInteger(1));
    QVERIFY(garbage->getDocument()->isEditable());

    // A template breaking to thousands of pages: at most the limit of the layout
    pdf::PDFAppDocumentTab* many = H::openReady(fixture, H::writeSample(fixture.folder(), QStringLiteral("many.pdf"), { S::letter() }, S::manyPagesTemplate(2000)));
    QVERIFY(many);
    QVERIFY(many->getDocument()->isPagedXFA());
    QVERIFY(many->getDocument()->getPageCount() > 100);
    QVERIFY(many->getDocument()->getPageCount() <= pdf::PDFInteger(pdf::PDFXFAPaging::MAXIMUM_PAGE_COUNT));
    QCOMPARE(many->getView()->getToolbar()->getPageField()->getSuffix(), QStringLiteral("/ %1").arg(many->getDocument()->getPageCount()));

    // A medium of a hostile size: not paged
    pdf::PDFAppDocumentTab* huge = H::openReady(fixture, H::writeSample(fixture.folder(), QStringLiteral("huge.pdf"), { S::letter() }, S::pagedTemplate(2, QStringLiteral("99999in"), QStringLiteral("11in"))));
    QVERIFY(huge);
    QVERIFY(!huge->getDocument()->isPagedXFA());
    QCOMPARE(huge->getDocument()->getPageCount(), pdf::PDFInteger(1));
}

void AppXfaTest::deletedWhileReading_data()
{
    QTest::addColumn<bool>("deleteWindow");
    QTest::newRow("tab") << false;
    QTest::newRow("window") << true;
}

void AppXfaTest::deletedWhileReading()
{
    using H = XFATestHelper;
    QFETCH(bool, deleteWindow);
    pdftest::AppDocumentFixture fixture;
    const QString file = H::writeSample(fixture.folder(), QStringLiteral("many.pdf"), { pdftest::XFATestSamples::letter() }, pdftest::XFATestSamples::manyPagesTemplate(3000));

    // The paging runs in the worker of the reading: the tab or the window is gone,
    // before the worker ends
    for (int i = 0; i < 3; ++i)
    {
        QVERIFY(fixture.window);
        fixture.window->openDocuments(QStringList({ file }), 0);
        QList<pdf::PDFAppDocumentTab*> tabs = fixture.window->getDocumentTabs();
        QVERIFY(!tabs.isEmpty());
        QPointer<pdf::PDFAppDocumentTab> tab(tabs.back());
        QCOMPARE(tab->getDocument()->getState(), pdf::PDFAppDocumentState::Loading);
        if (deleteWindow)
        {
            delete fixture.window.data();
            QVERIFY(!fixture.window);
            QVERIFY(!tab);
            break;
        }
        delete tab.data();
        QTest::qWait(i * 20);
        fixture.settle();
    }
    QThreadPool::globalInstance()->waitForDone(60000);
    fixture.settle();

    // The window still opens the form
    if (fixture.window)
    {
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, file);
        QVERIFY(tab);
        QVERIFY(tab->getDocument()->isPagedXFA());
    }
}

void AppXfaTest::infoBarThemesAndDirection()
{
    using H = XFATestHelper;
    for (const Qt::LayoutDirection direction : { Qt::LeftToRight, Qt::RightToLeft })
    {
        QApplication::setLayoutDirection(direction);
        pdftest::AppDocumentFixture fixture;
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::dynamicForm(fixture.folder()));
        QVERIFY(tab);
        QTRY_VERIFY(H::xfaBar(tab));
        for (const pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Dark, pdf::PDFThemeColors::Scheme::Light })
        {
            pdf::PDFThemeManager::getInstance()->setScheme(scheme);
            fixture.settle();
            pdf::PDFAppInfoBar* bar = tab->getView()->findInfoBarWidget(pdf::PDFAppFormController::getXFABarId());
            QVERIFY(bar);
            QCOMPARE(bar->isRightToLeft(), direction == Qt::RightToLeft);
            QVERIFY(bar->geometry().intersects(tab->getView()->getCanvasArea()->rect()));
            for (const qreal ratio : { 1.0, 2.0 })
            {
                QImage image(QSize(qCeil(bar->width() * ratio), qCeil(bar->height() * ratio)), QImage::Format_ARGB32_Premultiplied);
                image.setDevicePixelRatio(ratio);
                image.fill(Qt::transparent);
                bar->render(&image, QPoint(), QRegion(), QWidget::DrawChildren);
                QCOMPARE(H::countLeakPixels(image), 0);
            }
        }
        QApplication::setLayoutDirection(Qt::LeftToRight);
        pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
    }
}

void AppXfaTest::mcpReadsAndRefusals()
{
    using H = XFATestHelper;
    pdftest::AppMcpFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    const QString file = H::dynamicForm(fixture.folder());
    const QByteArray original = H::readFile(file);
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, file);
    QVERIFY(tab);
    const QString id = tab->getDocument()->getId();

    // Reads use the shown document: the pages of the layout
    QJsonObject arguments = H::json(R"({"action":"get_info"})");
    arguments.insert(QStringLiteral("documentId"), id);
    std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("document"), arguments);
    QVERIFY2(!H::isError(result), qPrintable(H::text(result)));
    const QJsonObject info = H::structured(result).value(QStringLiteral("get_info")).toObject();
    QCOMPARE(info.value(QStringLiteral("pageCount")).toInteger(), qint64(3));
    QCOMPARE(info.value(QStringLiteral("form")).toString(), QStringLiteral("xfa"));

    arguments = H::json(R"({"action":"get_pages"})");
    arguments.insert(QStringLiteral("documentId"), id);
    result = fixture.callTool(QStringLiteral("document"), arguments);
    QVERIFY2(!H::isError(result), qPrintable(H::text(result)));
    const QJsonArray pages = H::structured(result).value(QStringLiteral("get_pages")).toObject().value(QStringLiteral("pages")).toArray();
    QCOMPARE(pages.size(), qsizetype(3));
    QCOMPARE(pages.at(2).toObject().value(QStringLiteral("widthPt")).toDouble(), 612.0);

    arguments = H::json(R"({"action":"get_form_fields"})");
    arguments.insert(QStringLiteral("documentId"), id);
    result = fixture.callTool(QStringLiteral("document"), arguments);
    QVERIFY2(!H::isError(result), qPrintable(H::text(result)));
    const QJsonObject fields = H::structured(result).value(QStringLiteral("get_form_fields")).toObject();
    QVERIFY(fields.value(QStringLiteral("xfa")).toBool());
    QVERIFY(fields.value(QStringLiteral("note")).toString().contains(QStringLiteral("pages of its layout")));

    // annotations, forms and outline are refused with the reason, nothing is asked
    const QString reason = QStringLiteral("The document is an XFA form, which PDF4QT shows with the pages of its layout; it cannot be changed. files.save_copy writes the original PDF.");
    for (const char* call : { R"({"action":"add","add":{"items":[{"type":"note","page":1,"point":{"x":100,"y":100},"contents":"x"}]}})",
                              R"({"action":"delete","delete":{"ids":["1:0"]}})" })
    {
        arguments = H::json(call);
        arguments.insert(QStringLiteral("documentId"), id);
        result = fixture.callTool(QStringLiteral("annotations"), arguments);
        QVERIFY(H::isError(result));
        QCOMPARE(H::text(result), reason);
    }
    arguments = H::json(R"({"action":"reset"})");
    arguments.insert(QStringLiteral("documentId"), id);
    result = fixture.callTool(QStringLiteral("forms"), arguments);
    QVERIFY(H::isError(result));
    QCOMPARE(H::text(result), reason);
    arguments = H::json(R"({"action":"add","add":{"title":"x","page":1}})");
    arguments.insert(QStringLiteral("documentId"), id);
    result = fixture.callTool(QStringLiteral("outline"), arguments);
    QVERIFY(H::isError(result));
    QCOMPARE(H::text(result), reason);
    QVERIFY(fixture.confirmations()->shown.empty());
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(tab->getDocument()), 0);
    QVERIFY(!tab->getDocument()->isModified());

    // files.save_copy writes the original bytes
    const QString copyPath = fixture.path(QStringLiteral("xfa-copy.pdf"));
    QJsonObject copy;
    copy.insert(QStringLiteral("action"), QStringLiteral("save_copy"));
    copy.insert(QStringLiteral("documentId"), id);
    copy.insert(QStringLiteral("save_copy"), QJsonObject({ { QStringLiteral("path"), copyPath } }));
    result = fixture.callTool(QStringLiteral("files"), copy);
    QVERIFY2(!H::isError(result), qPrintable(H::text(result)));
    QCOMPARE(H::readFile(copyPath), original);

    // files.save keeps the file (the original bytes; the overwrite of the own file
    // is always confirmed - allowed here)
    fixture.confirmations()->setAutoAnswer(pdf::PDFAppMcpDecision::Allowed);
    QJsonObject save;
    save.insert(QStringLiteral("action"), QStringLiteral("save"));
    save.insert(QStringLiteral("documentId"), id);
    result = fixture.callTool(QStringLiteral("files"), save);
    QVERIFY2(!H::isError(result), qPrintable(H::text(result)));
    QThreadPool::globalInstance()->waitForDone(30000);
    fixture.settle();
    QCOMPARE(H::readFile(file), original);
    QVERIFY(tab->getDocument()->isPagedXFA());
}

void AppXfaTest::reviewLockPopoverWithoutCopy()
{
    // Review RS8: the lock popover of a locked display version offers Unlock only -
    // a copy of it could not be edited either (the command Edit a Copy is off)
    using H = XFATestHelper;
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::dynamicForm(fixture.folder()));
    QVERIFY(tab);
    QVERIFY(tab->getDocument()->isPagedXFA());
    tab->getDocument()->setLocked(true);
    fixture.settle();
    QVERIFY(!tab->isCommandEnabled(pdf::PDFAppCommands::editCopy()));

    QSignalSpy copySpy(tab, &pdf::PDFAppDocumentTab::editCopyRequested);
    tab->showLockPopover(QRect(100, 100, 10, 10));
    QTRY_VERIFY(tab->getLockPopover() && tab->getLockPopover()->isVisible());
    QPointer<pdf::PDFAppLockPopover> popover(tab->getLockPopover());
    QVERIFY(popover->getEditCopyButton()->isHidden());
    QVERIFY(!popover->getUnlockButton()->isHidden());
    pdf::PDFLabel* text = popover->findChild<pdf::PDFLabel*>(QStringLiteral("lockText"));
    QVERIFY(text);
    QVERIFY(text->text().contains(QStringLiteral("XFA form")));
    QVERIFY(text->height() >= text->heightForWidth(text->width()));

    // A request of a copy (also an old signal of the popover) opens nothing
    Q_EMIT popover->editCopyRequested();
    fixture.settle();
    QCOMPARE(copySpy.count(), 0);
    QCOMPARE(fixture.window->getDocumentTabs().size(), qsizetype(1));

    // Unlock still works (the document stays a display version)
    popover->getUnlockButton()->click();
    fixture.settle();
    QVERIFY(!tab->getDocument()->isLocked());
    QVERIFY(!tab->getDocument()->isEditable());

    // A document, which is not paged, still offers its copy
    pdf::PDFAppDocumentTab* plain = H::openReady(fixture, H::staticForm(fixture.folder()));
    QVERIFY(plain);
    plain->getDocument()->setLocked(true);
    fixture.settle();
    plain->showLockPopover(QRect(100, 100, 10, 10));
    QTRY_VERIFY(plain->getLockPopover() && plain->getLockPopover()->isVisible());
    QVERIFY(!plain->getLockPopover()->getEditCopyButton()->isHidden());
    plain->getLockPopover()->close();
    fixture.settle();
}

void AppXfaTest::reviewMcpCatalogToolRefused()
{
    // Review RS8: a tool of the catalog (the bridge of plan 3.10) may analyze the
    // shown pages, but never commits its changed document to a display version or
    // writes it as a changed copy
    using H = XFATestHelper;
    pdftest::AppMcpFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    const QString file = H::dynamicForm(fixture.folder());
    const QByteArray original = H::readFile(file);
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, file);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    QVERIFY(document->isPagedXFA());

    std::shared_ptr<pdftest::TestDocumentTool> tool = std::make_shared<pdftest::TestDocumentTool>();
    pdf::PDFDocumentToolRegistry::registerTool(tool);
    fixture.host()->checkToolRegistry();
    QTRY_VERIFY_WITH_TIMEOUT(fixture.host()->getCatalog()->findTool(u"review"), 5000);

    // An analysis of the shown document: the pages of the layout
    QJsonObject arguments = H::json(R"({"action":"statistics","statistics":{"mode":"count"}})");
    arguments.insert(QStringLiteral("documentId"), document->getId());
    std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("review"), arguments);
    QVERIFY2(!H::isError(result), qPrintable(H::text(result)));
    QCOMPARE(H::structured(result).value(QStringLiteral("statistics")).toObject().value(QStringLiteral("data")).toObject().value(QStringLiteral("counted")).toInt(), 3);

    // A change kept in the tab and a changed copy are refused with the reason
    const QString reason = QStringLiteral("The document is an XFA form, which PDF4QT shows with the pages of its layout; it cannot be changed. files.save_copy writes the original PDF.");
    arguments = H::json(R"({"action":"statistics","statistics":{"mode":"title","title":"Changed"}})");
    arguments.insert(QStringLiteral("documentId"), document->getId());
    result = fixture.callTool(QStringLiteral("review"), arguments);
    QVERIFY(H::isError(result));
    QCOMPARE(H::text(result), reason);

    const QString copyPath = fixture.path(QStringLiteral("changed-copy.pdf"));
    arguments = H::json(R"({"action":"statistics","result":"save_copy","statistics":{"mode":"title","title":"Copy"}})");
    arguments.insert(QStringLiteral("documentId"), document->getId());
    arguments.insert(QStringLiteral("path"), copyPath);
    result = fixture.callTool(QStringLiteral("review"), arguments);
    QVERIFY(H::isError(result));
    QCOMPARE(H::text(result), reason);
    QVERIFY(!QFile::exists(copyPath));

    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 0);
    QVERIFY(!document->isModified());
    QCOMPARE(document->getDocument()->getInfo()->title, QString());
    QCOMPARE(H::readFile(file), original);

    pdf::PDFDocumentToolRegistry::unregisterTool(QString::fromLatin1(pdftest::TestDocumentTool::TOOL_ID));
}

QTEST_MAIN(AppXfaTest)

#include "tst_appxfatest.moc"
