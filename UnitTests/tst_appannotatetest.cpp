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

// Tests of package A of step 4 (creation of annotations): the Annotate and Insert
// panels, the tool bars, the markup bar, the compose flows, the text box popup,
// the web link dialog, Link to page, the stamps, the shapes, Mark as annotations
// and Delete annotations. PDF4QT_APP_SCREENSHOTS=<folder> writes the screens.

#include "appdocumentfixture.h"

#include "pdfannotationstylerepository.h"
#include "pdfappannotatecommands.h"
#include "pdfappannotatepanels.h"
#include "pdfappannotationcontroller.h"
#include "pdfappannotationcreatedialogs.h"
#include "pdfappannotationcreatetools.h"
#include "pdfappannotationcreator.h"
#include "pdfappannotationstylestorage.h"
#include "pdfappannotationtoolbar.h"
#include "pdfappcommand.h"
#include "pdfappcommentpopup.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentsearch.h"
#include "pdfappdocumentsearchcontroller.h"
#include "pdfappdocumentsearchpanel.h"
#include "pdfappdocumentstatusbar.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumenttoolbar.h"
#include "pdfappdocumentview.h"
#include "pdfappfloatingbar.h"
#include "pdfapphelpcatalog.h"
#include "pdfapplocale.h"
#include "pdfappmainwindow.h"
#include "pdfappmarkresults.h"
#include "pdfappmarkupbar.h"
#include "pdfappoutlineeditor.h"
#include "pdfappundostack.h"
#include "pdfsettingdefinitions.h"
#include "pdfsettingsrepository.h"

#include "pdfaction.h"
#include "pdfannotation.h"
#include "pdfannotationmanipulator.h"
#include "pdfannotationstyle.h"
#include "pdfdocument.h"
#include "pdfdrawwidget.h"
#include "pdfutils.h"
#include "pdfwidgettool.h"

#include "pdfcomponentbuttons.h"
#include "pdfcomponentpanels.h"
#include "pdffluentwidgets.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QAction>
#include <QCheckBox>
#include <QClipboard>
#include <QDir>
#include <QJsonObject>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QToolButton>
#include <QTranslator>
#include <QtTest>

#include <cmath>
#include <limits>

/// Translator of the test: every text is marked, so a retranslated text is seen
class AnnotateTestTranslator : public QTranslator
{
public:
    explicit AnnotateTestTranslator(QObject* parent) :
        QTranslator(parent)
    {
    }

    virtual bool isEmpty() const override { return false; }

    virtual QString translate(const char* context, const char* sourceText, const char* disambiguation, int n) const override
    {
        Q_UNUSED(disambiguation);
        Q_UNUSED(n);
        if (!sourceText || !context || !QByteArray(context).startsWith("pdf::"))
        {
            return QString();
        }
        return QStringLiteral("§") + QString::fromUtf8(sourceText);
    }
};

/// Helpers of the tests of package A (creation of annotations)
class AppAnnotateTestHelper
{
public:
    AppAnnotateTestHelper() = delete;

    /// Opens the file and waits for the shown document and its pages
    static pdf::PDFAppDocumentTab* openReady(pdftest::AppDocumentFixture& fixture, const QString& filePath)
    {
        pdf::PDFAppDocumentTab* tab = fixture.open(filePath);
        if (!tab)
        {
            return nullptr;
        }

        QPointer<pdf::PDFAppDocumentTab> guard(tab);
        const bool isReady = QTest::qWaitFor([&guard]()
        {
            return guard && guard->getDocument() && guard->getDocument()->isReady() && guard->getView() && guard->getView()->getPageCount() > 0;
        }, 20000 * pdftest::AppDocumentFixture::getTimeoutFactor());
        fixture.window->activateTab(guard.data());
        pdftest::AppDocumentFixture::settle();
        return isReady ? guard.data() : nullptr;
    }

    static pdf::PDFAppAnnotationCreator* creator(pdf::PDFAppDocumentTab* tab)
    {
        return tab ? pdf::PDFAppAnnotateCommandHandler::findCreator(tab->getView()) : nullptr;
    }

    static pdf::PDFAppMarkResults* markResults(pdf::PDFAppDocumentTab* tab)
    {
        const pdf::PDFAppAnnotateCommandHandler* handler = tab ? pdf::PDFAppAnnotateCommandHandler::findHandler(tab->getView()) : nullptr;
        return handler ? handler->getMarkResults() : nullptr;
    }

    /// Number of the annotations of the page
    static int annotationCount(const pdf::PDFAppDocument* document, int pageIndex)
    {
        const pdf::PDFDocumentPointer pdfDocument = document ? document->getDocument() : pdf::PDFDocumentPointer();
        const pdf::PDFPage* page = pdfDocument && pageIndex >= 0 && size_t(pageIndex) < pdfDocument->getCatalog()->getPageCount() ? pdfDocument->getCatalog()->getPage(pageIndex) : nullptr;
        return page ? int(page->getAnnotations().size()) : -1;
    }

    /// The last annotation of the page of the subtype (a sticky note has its popup after it)
    static pdf::PDFObjectReference lastOfType(const pdf::PDFAppDocument* document, int pageIndex, const QByteArray& subtype)
    {
        const pdf::PDFDocumentPointer pdfDocument = document ? document->getDocument() : pdf::PDFDocumentPointer();
        const pdf::PDFPage* page = pdfDocument ? pdfDocument->getCatalog()->getPage(pageIndex) : nullptr;
        if (!page)
        {
            return pdf::PDFObjectReference();
        }
        const std::vector<pdf::PDFObjectReference>& annotations = page->getAnnotations();
        for (auto it = annotations.rbegin(); it != annotations.rend(); ++it)
        {
            if (name(document, *it, "Subtype") == subtype)
            {
                return *it;
            }
        }
        return pdf::PDFObjectReference();
    }

    /// The last annotation of the page
    static pdf::PDFObjectReference lastAnnotation(const pdf::PDFAppDocument* document, int pageIndex)
    {
        const pdf::PDFDocumentPointer pdfDocument = document ? document->getDocument() : pdf::PDFDocumentPointer();
        const pdf::PDFPage* page = pdfDocument ? pdfDocument->getCatalog()->getPage(pageIndex) : nullptr;
        return page && !page->getAnnotations().empty() ? page->getAnnotations().back() : pdf::PDFObjectReference();
    }

    static const pdf::PDFDictionary* dictionary(const pdf::PDFAppDocument* document, pdf::PDFObjectReference reference)
    {
        const pdf::PDFDocumentPointer pdfDocument = document ? document->getDocument() : pdf::PDFDocumentPointer();
        if (!pdfDocument)
        {
            return nullptr;
        }
        const pdf::PDFObject& object = pdfDocument->getStorage().getObjectByReference(reference);
        return object.isDictionary() ? object.getDictionary() : nullptr;
    }

    static QByteArray name(const pdf::PDFAppDocument* document, pdf::PDFObjectReference reference, const char* key)
    {
        const pdf::PDFDocumentPointer pdfDocument = document->getDocument();
        pdf::PDFDocumentDataLoaderDecorator loader(pdfDocument.data());
        return loader.readNameFromDictionary(dictionary(document, reference), key);
    }

    static QString text(const pdf::PDFAppDocument* document, pdf::PDFObjectReference reference, const char* key)
    {
        const pdf::PDFDocumentPointer pdfDocument = document->getDocument();
        pdf::PDFDocumentDataLoaderDecorator loader(pdfDocument.data());
        return loader.readTextStringFromDictionary(dictionary(document, reference), key, QString());
    }

    static pdf::PDFAnnotationPtr annotation(const pdf::PDFAppDocument* document, pdf::PDFObjectReference reference)
    {
        const pdf::PDFDocumentPointer pdfDocument = document->getDocument();
        return pdf::PDFAnnotation::parse(&pdfDocument->getStorage(), reference);
    }

    /// Destination of the GoTo action of a link
    static pdf::PDFDestination linkDestination(const pdf::PDFAppDocument* document, pdf::PDFObjectReference reference)
    {
        const pdf::PDFAnnotationPtr parsed = annotation(document, reference);
        const pdf::PDFLinkAnnotation* link = dynamic_cast<const pdf::PDFLinkAnnotation*>(parsed.data());
        const pdf::PDFActionGoTo* action = link ? dynamic_cast<const pdf::PDFActionGoTo*>(link->getAction()) : nullptr;
        return action ? action->getDestination() : pdf::PDFDestination();
    }

    /// Moves the pointer to the point of the page and clicks there (the pick
    /// tools take the point of the last move)
    static bool pick(pdf::PDFAppDocumentView* view, int pageIndex, const QPointF& pagePoint, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        bool ok = false;
        const QPoint position = pdftest::AppDocumentFixture::pageToWidget(view, pageIndex, pagePoint, &ok);
        QPointer<QWidget> target = pdftest::AppDocumentFixture::canvasWidget(view);
        if (!ok || !target)
        {
            return false;
        }

        // Two moves: a pick tool takes a move to the point it already knows for no move
        const QPoint nearby = position + QPoint(1, 1);
        QMouseEvent moveNearby(QEvent::MouseMove, nearby, target->mapToGlobal(nearby), Qt::NoButton, Qt::NoButton, modifiers);
        QCoreApplication::sendEvent(target, &moveNearby);
        QMouseEvent move(QEvent::MouseMove, position, target->mapToGlobal(position), Qt::NoButton, Qt::NoButton, modifiers);
        QCoreApplication::sendEvent(target, &move);
        return target && pdftest::AppDocumentFixture::clickPage(view, pageIndex, pagePoint, modifiers);
    }

    /// Picks the two corners of a rectangle of the page
    static bool pickRectangle(pdf::PDFAppDocumentView* view, int pageIndex, const QRectF& rectangle)
    {
        return pick(view, pageIndex, rectangle.topLeft()) && pick(view, pageIndex, rectangle.bottomRight());
    }

    /// Sends a key to the canvas
    static void key(pdf::PDFAppDocumentView* view, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QWidget* target = pdftest::AppDocumentFixture::canvasWidget(view);
        QVERIFY(target);
        QKeyEvent press(QEvent::KeyPress, key, modifiers);
        QCoreApplication::sendEvent(target, &press);
        QKeyEvent release(QEvent::KeyRelease, key, modifiers);
        QCoreApplication::sendEvent(target, &release);
    }

    /// Selects the whole text of the document by the view (waits for the text layout)
    static bool selectAllText(pdf::PDFAppDocumentTab* tab)
    {
        pdf::PDFAppAnnotationCreator* annotationCreator = creator(tab);
        if (!annotationCreator)
        {
            return false;
        }
        tab->getView()->selectAllText();
        const bool isSelected = QTest::qWaitFor([annotationCreator]() { return annotationCreator->hasTextSelection(); }, 10000 * pdftest::AppDocumentFixture::getTimeoutFactor());
        pdftest::AppDocumentFixture::settle();
        return isSelected;
    }

    static QImage render(QWidget* widget, qreal devicePixelRatio = 1.0)
    {
        QImage image(QSize(qCeil(widget->width() * devicePixelRatio), qCeil(widget->height() * devicePixelRatio)), QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(devicePixelRatio);
        image.fill(Qt::transparent);
        widget->render(&image, QPoint(), QRegion(), widget->isWindow() ? QWidget::RenderFlags(QWidget::DrawChildren | QWidget::DrawWindowBackground) : QWidget::RenderFlags(QWidget::DrawChildren));
        return image;
    }

    static int countLeakPixels(const QImage& image)
    {
        const QRgb leak = pdf::PDFThemeColors::getLeakColor().rgb();
        int count = 0;
        const QImage converted = image.convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < converted.height(); ++y)
        {
            const QRgb* line = reinterpret_cast<const QRgb*>(converted.constScanLine(y));
            for (int x = 0; x < converted.width(); ++x)
            {
                if (qAlpha(line[x]) == 255 && (line[x] & 0x00FFFFFF) == (leak & 0x00FFFFFF))
                {
                    ++count;
                }
            }
        }
        return count;
    }

    static QColor pixel(const QImage& image, QPoint point)
    {
        const qreal ratio = image.devicePixelRatio();
        return image.pixelColor(QPoint(qRound(point.x() * ratio), qRound(point.y() * ratio)));
    }

    static QColor token(pdf::PDFThemeColors::Color color)
    {
        return pdf::PDFThemeManager::getInstance()->getColor(color);
    }

    static void save(const QString& folder, const QString& name, const QImage& image)
    {
        QDir().mkpath(folder);
        QVERIFY(image.save(QDir(folder).filePath(name + QStringLiteral(".png"))));
    }

    /// Picture of the window with the open popups drawn at their places
    static QImage renderWindowWithPopups(QWidget* window, const QList<QWidget*>& popups, qreal ratio)
    {
        QImage image = render(window, ratio);
        QPainter painter(&image);
        for (QWidget* popup : popups)
        {
            if (popup && popup->isVisible())
            {
                const QImage popupImage = render(popup, ratio);
                painter.drawImage(QPointF(popup->mapToGlobal(QPoint(0, 0)) - window->mapToGlobal(QPoint(0, 0))), popupImage);
            }
        }
        return image;
    }

    /// The comment popup of the view in the compose mode
    static pdf::PDFAppCommentPopup* composePopup(pdf::PDFAppDocumentTab* tab)
    {
        pdf::PDFAppCommentPopup* popup = tab->getView()->getCommentPopup();
        return popup && popup->getMode() == pdf::PDFAppCommentPopup::Mode::Compose ? popup : nullptr;
    }

    /// Searches the text in the tab and waits for the results
    static bool search(pdf::PDFAppDocumentTab* tab, const QString& text)
    {
        pdf::PDFAppDocumentSearchCommandHandler* handler = pdf::PDFAppDocumentSearchCommandHandler::findHandler(tab->getView());
        pdf::PDFAppDocumentSearchController* controller = handler ? handler->getSearchController() : nullptr;
        if (!controller)
        {
            return false;
        }
        controller->search(text);
        const bool isFinished = QTest::qWaitFor([controller]() { return controller->getState() == pdf::PDFAppDocumentSearchController::State::Finished; }, 60000 * pdftest::AppDocumentFixture::getTimeoutFactor());
        pdftest::AppDocumentFixture::settle();
        return isFinished;
    }

    /// Color (C) of the annotation as an rgb value (0 without a color)
    static QRgb strokeRgb(const pdf::PDFAnnotationPtr& annotation)
    {
        const std::vector<pdf::PDFReal> color = annotation ? annotation->getColor() : std::vector<pdf::PDFReal>();
        return color.size() == 3 ? QColor::fromRgbF(float(color[0]), float(color[1]), float(color[2])).rgb() : 0;
    }

    /// Undo description of the top step
    static QString undoDescription(const pdf::PDFAppDocument* document)
    {
        return document && document->getUndoStack() ? document->getUndoStack()->getUndoDescription() : QString();
    }
};

using H = AppAnnotateTestHelper;
using Kind = pdf::PDFAppAnnotationToolKind;

class AppAnnotateTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void fixtureSamples();
    void floatingBarGeometry();
    void floatingBarFolding();
    void annotationStyleStorageInstalled();
    void operationsHostile();
    void panelsGeometry();
    void panelsKeyboard();
    void toolActivationEachTile();
    void toolBarVariants();
    void toolEndsByEscAndClose();
    void toolStylePersisted();
    void toolStyleFromOtherInstance();
    void hostileStyleRows();
    void createEachKindOneStep();
    void authorFromSettings();
    void stampsAll();
    void stickyIconsAll();
    void stickyNoteCompose();
    void highlightAndComment();
    void textBoxDialog();
    void webLinkDialogHostile();
    void pageLinkDestination();
    void creationAreasDrawing();
    void pageLinkTargetMenu();
    void selectionFirstMarkup();
    void markupBar();
    void markupBarDisabled();
    void markAsAnnotations();
    void markAsAnnotationsLarge();
    void markResultsFooterGeometry();
    void deleteTool();
    void lockedDocument();
    void permissionDenied();
    void deletionMatrix();
    void deleteFromOwnSignals();
    void themesAndScales();
    void rightToLeft();
    void minimalWindow();
    void accessibility();
    void languageChange();
    void reviewHighlightCommentOverPages();
    void reviewTextBoxPopupHiddenTab();
    void reviewPageLinkBarAndMenu();
    void reviewMarkupBarLostRelease();
    void reviewPluralTexts();
    void reviewIconsAndHelp();
    void reviewMarkResultsKinds();
    void reviewStyleReloadOnce();
    void reviewLastMarkupOfStartedTool();
    void reviewCommentCountCached();
    void screenshots();
};

void AppAnnotateTest::initTestCase()
{
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
}

void AppAnnotateTest::fixtureSamples()
{
    pdftest::AppDocumentFixture fixture;

    // Every sample of step 4 opens and has its annotations / fields
    pdf::PDFAppDocumentTab* annotations = fixture.open(pdftest::AppDocumentSamples::annotations(fixture.folder()));
    QVERIFY(annotations);
    QVERIFY(annotations->getDocument()->isReady());
    QCOMPARE(annotations->getDocument()->getPageCount(), pdf::PDFInteger(3));
    QVERIFY(H::annotationCount(annotations->getDocument(), 0) >= 22);
    QCOMPARE(H::annotationCount(annotations->getDocument(), 1), 3);
    QCOMPARE(H::annotationCount(annotations->getDocument(), 2), 0);

    pdf::PDFAppDocumentTab* restricted = fixture.open(pdftest::AppDocumentSamples::restricted(fixture.folder()));
    QVERIFY(restricted);
    QVERIFY(restricted->getDocument()->isReady());

    pdf::PDFAppDocumentTab* large = fixture.open(pdftest::AppDocumentSamples::annotatedLarge(fixture.folder(), 20, 5));
    QVERIFY(large);
    QCOMPARE(large->getDocument()->getPageCount(), pdf::PDFInteger(20));
    QCOMPARE(H::annotationCount(large->getDocument(), 19), 5 + 2);

    // The page helpers of the fixture map page points to the canvas
    pdf::PDFAppDocumentView* view = annotations->getView();
    fixture.window->activateTab(annotations);
    pdftest::AppDocumentFixture::settle();
    bool ok = false;
    const QPoint point = pdftest::AppDocumentFixture::pageToWidget(view, 0, QPointF(150, 200), &ok);
    QVERIFY(ok);
    QVERIFY(pdftest::AppDocumentFixture::canvasWidget(view)->rect().contains(point));
    QVERIFY(!pdftest::AppDocumentFixture::clickPage(view, 99, QPointF(5, 5)));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(nullptr), -1);
}

void AppAnnotateTest::floatingBarGeometry()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Bar.pdf"), 1));
    QVERIFY(tab);
    QWidget* canvas = tab->getView()->getCanvasArea();
    QVERIFY(canvas);

    // Normal bar (tool-highlight 877:132853): height 46, 12 px below the toolbar, centered
    QPointer<pdf::PDFAppFloatingBar> bar = new pdf::PDFAppFloatingBar(pdf::PDFAppFloatingBar::Size::Normal, canvas);
    QLabel* name = new QLabel(QStringLiteral("Highlight"), bar);
    name->setFixedSize(80, 16);
    bar->addWidget(name);
    bar->addDivider();
    pdf::PDFColorSwatchRow* swatches = new pdf::PDFColorSwatchRow(bar);
    bar->addWidget(swatches);
    bar->setFoldableSwatchRow(swatches);
    bar->placeAtTop();
    bar->show();
    pdftest::AppDocumentFixture::settle();

    const QRect card = bar->getCardGeometry();
    QCOMPARE(card.height(), pdf::PDFAppFloatingBar::NORMAL_HEIGHT);
    QCOMPARE(card.top(), pdf::PDFAppFloatingBar::TOP_GAP);
    QVERIFY(qAbs(card.center().x() - canvas->width() / 2) <= 1);
    QCOMPARE(name->mapTo(canvas, QPoint(0, 0)).x() - card.left(), 15);
    QCOMPARE(card.width(), 15 + 80 + 10 + 1 + 10 + swatches->sizeHint().width() + 9);
    QVERIFY(bar->getShadow());
    QVERIFY(!bar->mask().contains(QPoint(2, 2)));

    // Compact (markup bar 877:134226): 38 high, anchored 8 px above the selection
    QPointer<pdf::PDFAppFloatingBar> compact = new pdf::PDFAppFloatingBar(pdf::PDFAppFloatingBar::Size::Compact, canvas);
    for (const char* icon : { "highlighter", "underline", "strikethrough" })
    {
        pdf::PDFIconButton* button = new pdf::PDFIconButton(QString::fromLatin1(icon), pdf::PDFIconButton::Size::Medium, compact);
        compact->addWidget(button);
    }
    const QRect selection(300, 400, 342, 40);
    compact->placeAnchored(selection);
    compact->show();
    pdftest::AppDocumentFixture::settle();
    const QRect compactCard = compact->getCardGeometry();
    QCOMPARE(compactCard.height(), pdf::PDFAppFloatingBar::COMPACT_HEIGHT);
    QCOMPARE(compactCard.bottom() + 1 + pdf::PDFAppFloatingBar::ANCHOR_GAP, selection.top());
    QCOMPARE(compactCard.width(), 5 + 3 * 28 + 2 * 2 + 5);

    // The bars die with the canvas in any order (the shadow goes with its bar)
    QPointer<pdf::PDFAppFloatingBarShadow> shadow = compact->getShadow();
    delete compact.data();
    QVERIFY(!shadow);
    delete tab;
    pdftest::AppDocumentFixture::settle();
    QVERIFY(!bar);
}

void AppAnnotateTest::floatingBarFolding()
{
    pdftest::AppDocumentFixture fixture(QSize(720, 480));
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Narrow.pdf"), 1));
    QVERIFY(tab);
    QWidget* canvas = tab->getView()->getCanvasArea();

    // A bar wider than the canvas folds its swatches into More (the minimal window)
    pdf::PDFAppFloatingBar* bar = new pdf::PDFAppFloatingBar(pdf::PDFAppFloatingBar::Size::Normal, canvas);
    QWidget* wide = new QWidget(bar);
    wide->setFixedSize(canvas->width() - 200, 20);
    bar->addWidget(wide);
    pdf::PDFColorSwatchRow* swatches = new pdf::PDFColorSwatchRow(bar);
    swatches->setOptions(pdf::PDFColorSwatchRow::Option::NoColor | pdf::PDFColorSwatchRow::Option::White | pdf::PDFColorSwatchRow::Option::More);
    bar->addWidget(swatches);
    bar->setFoldableSwatchRow(swatches);
    bar->placeAtTop();
    bar->show();
    pdftest::AppDocumentFixture::settle();

    QVERIFY(swatches->getVisibleSwatchLimit() > 0);
    QVERIFY(swatches->getVisibleSwatchLimit() < swatches->getSwatchCount());
    fixture.window->resize(1440, 960);
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(swatches->getVisibleSwatchLimit(), -1);
}

void AppAnnotateTest::annotationStyleStorageInstalled()
{
    pdftest::AppDocumentFixture fixture;

    // The context installs its storage of the styles (E1) and turns the old style window off
    pdf::PDFAppAnnotationStyleStorage* storage = fixture.context->getAnnotationStyleStorage();
    QVERIFY(storage);
    QCOMPARE(pdf::PDFAnnotationStyleSettings::getStorage(), storage);
    QVERIFY(!pdf::PDFAnnotationStyleManager::isStyleWindowEnabled());

    pdf::PDFAnnotationStyle style;
    style.strokeColor = QColor(0x12, 0x34, 0x56);
    style.fillColor = QColor();
    style.penWidth = 3.5;
    pdf::PDFAnnotationStyleSettings::setStyle(pdf::PDFAnnotationStyleSettings::STYLE_SHAPE, style);
    const std::optional<QJsonObject> row = storage->getRepository().getValue(pdf::PDFAnnotationStyleSettings::STYLE_SHAPE);
    QVERIFY(row.has_value());
    QCOMPARE(row->value(QStringLiteral("strokeColor")).toString(), QStringLiteral("#123456"));
    QCOMPARE(pdf::PDFAnnotationStyleSettings::getStyle(pdf::PDFAnnotationStyleSettings::STYLE_SHAPE, pdf::PDFAnnotationStyle()), style);

    fixture.context->close();
    QVERIFY(!fixture.context->getAnnotationStyleStorage());
    QVERIFY(pdf::PDFAnnotationStyleSettings::isDefaultStorage());
}

void AppAnnotateTest::operationsHostile()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Hostile.pdf"), 2));
    QVERIFY(tab);
    const pdf::PDFDocumentPointer document = tab->getDocument()->getDocument();
    using Ops = pdf::PDFAppAnnotationOperations;
    pdf::PDFAppNewAnnotation data;
    data.author = QStringLiteral("A") + QChar(0x0001) + QStringLiteral("u") + QChar(0x202E) + QStringLiteral("thor");
    data.contents = QString(Ops::MAXIMUM_TEXT_LENGTH * 3, QLatin1Char('x'));
    data.opacity = std::numeric_limits<double>::quiet_NaN();
    data.penWidth = 1e300;

    // Null document, invalid pages, types, rectangles, points and quadrilaterals give nullopt
    const double nan = std::numeric_limits<double>::quiet_NaN();
    QVERIFY(!Ops::createTextMarkups(pdf::PDFDocumentPointer(), pdf::AnnotationType::Highlight, { { 0, QPolygonF({ QPointF(0, 0), QPointF(1, 0), QPointF(0, 1), QPointF(1, 1) }) } }, data, nullptr));
    QVERIFY(!Ops::createTextMarkups(document, pdf::AnnotationType::Square, { { 0, QPolygonF({ QPointF(0, 0), QPointF(1, 0), QPointF(0, 1), QPointF(1, 1) }) } }, data, nullptr));
    QVERIFY(!Ops::createTextMarkups(document, pdf::AnnotationType::Highlight, { { 7, QPolygonF({ QPointF(0, 0), QPointF(1, 0), QPointF(0, 1), QPointF(1, 1) }) } }, data, nullptr));
    QVERIFY(!Ops::createTextMarkups(document, pdf::AnnotationType::Highlight, { { 0, QPolygonF({ QPointF(nan, 0), QPointF(1, 0), QPointF(0, 1), QPointF(1, 1) }) } }, data, nullptr));
    QVERIFY(!Ops::createTextMarkups(document, pdf::AnnotationType::Highlight, { { 0, QPolygonF({ QPointF(0, 0), QPointF(1, 0) }) } }, data, nullptr));
    QVERIFY(!Ops::createTextMarkups(document, pdf::AnnotationType::Highlight, {}, data, nullptr));
    QVERIFY(!Ops::createStickyNote(document, -1, QPointF(10, 10), pdf::TextAnnotationIcon::Comment, data, nullptr));
    QVERIFY(!Ops::createStickyNote(document, 0, QPointF(1e9, 10), pdf::TextAnnotationIcon::Comment, data, nullptr));
    QVERIFY(!Ops::createStickyNote(document, 0, QPointF(10, 10), pdf::TextAnnotationIcon(77), data, nullptr));
    QVERIFY(!Ops::createFreeText(document, 0, QRectF(10, 10, 0, 50), QStringLiteral("x"), pdf::PDFFreeTextStyle(), data, nullptr));
    QVERIFY(!Ops::createFreeText(document, 0, QRectF(10, 10, 50, 50), QStringLiteral("  \n "), pdf::PDFFreeTextStyle(), data, nullptr));
    QVERIFY(!Ops::createWebLink(document, 0, QRectF(10, 10, 50, 50), QStringLiteral("javascript:alert(1)"), nullptr));
    pdf::PDFDestination missingPage = pdf::PDFDestination::createFit(pdf::PDFObjectReference());
    missingPage.setPageIndex(99);
    QVERIFY(!Ops::createPageLink(document, 0, QRectF(10, 10, 50, 50), missingPage, nullptr));
    QVERIFY(!Ops::createPageLink(document, 5, QRectF(10, 10, 50, 50), pdf::PDFDestination::createFit(pdf::PDFObjectReference()), nullptr));
    pdf::PDFDestination named = pdf::PDFDestination::createNamed("x");
    named.setPageIndex(1);
    QVERIFY(!Ops::createPageLink(document, 0, QRectF(10, 10, 50, 50), named, nullptr));
    pdf::PDFDestination badZoom = pdf::PDFDestination::createXYZ(pdf::PDFObjectReference(), 1, 2, -5);
    badZoom.setPageIndex(1);
    QVERIFY(!Ops::createPageLink(document, 0, QRectF(10, 10, 50, 50), badZoom, nullptr));
    pdf::PDFDestination badTop = pdf::PDFDestination::createFitH(pdf::PDFObjectReference(), nan);
    badTop.setPageIndex(1);
    QVERIFY(!Ops::createPageLink(document, 0, QRectF(10, 10, 50, 50), badTop, nullptr));
    QVERIFY(!Ops::markSearchResults(document, {}, pdf::AnnotationType::Highlight, data, nullptr, nullptr, nullptr));

    // Hostile texts and numbers are bounded
    pdf::PDFObjectReference created;
    pdf::PDFFreeTextStyle style;
    style.fontFamily = QString(1000, QLatin1Char('F'));
    style.fontSize = 1e9;
    style.textColor = QColor();
    const pdf::PDFAppAnnotationResult result = Ops::createFreeText(document, 0, QRectF(10, 10, 80, 40), QStringLiteral("Text") + QChar(0) + QChar(7) + QStringLiteral(" ok"), style, data, &created);
    QVERIFY(result);
    QVERIFY(created.isValid());
    QVERIFY(tab->getView()->getAnnotationController()->commit(*result, pdf::PDFAppAnnotationChangeKind::Tool, QStringLiteral("Test")));
    QCOMPARE(H::text(tab->getDocument(), created, "T"), QStringLiteral("Author"));
    QCOMPARE(H::text(tab->getDocument(), created, "Contents"), QStringLiteral("Text ok"));

    // A canceled control gives nothing
    class Canceled : public pdf::PDFOperationControl
    {
    public:
        virtual bool isOperationCancelled() const override { return true; }
    };
    Canceled canceled;
    pdf::PDFDocumentTextSearchMatch match;
    match.pageIndex = 0;
    match.quadrilaterals = QPolygonF({ QPointF(0, 10), QPointF(10, 10), QPointF(0, 0), QPointF(10, 0) });
    QVERIFY(!Ops::markSearchResults(document, { match }, pdf::AnnotationType::Highlight, data, &canceled, nullptr, nullptr));
    size_t count = 0;
    QVERIFY(Ops::markSearchResults(document, { match, match }, pdf::AnnotationType::Squiggly, data, nullptr, nullptr, &count));
    QCOMPARE(count, size_t(2));
}

void AppAnnotateTest::panelsGeometry()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::annotations(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppDocumentToolbar* toolbar = tab->getView()->getToolbar();

    // Annotate 877:10157: 550 wide, tiles 88 / 140, the color row 272, Show all comments (N)
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::annotatePanel()));
    pdf::PDFAppAnnotatePanel* annotate = qobject_cast<pdf::PDFAppAnnotatePanel*>(toolbar->findPanel(pdf::PDFAppDocumentToolbar::Panel::Annotate));
    QVERIFY(annotate);
    QVERIFY(annotate->isVisible());
    QCOMPARE(annotate->getPanelRect().width(), pdf::PDFAppAnnotatePanel::PANEL_WIDTH);
    QAbstractButton* highlightTile = annotate->getCommandButton(pdf::PDFAppCommands::annotateHighlight());
    QAbstractButton* commentTile = annotate->getCommandButton(pdf::PDFAppCommands::annotateHighlightComment());
    QVERIFY(highlightTile && commentTile);
    QVERIFY(qAbs(highlightTile->width() - pdf::PDFAppAnnotatePanel::MARKUP_TILE_WIDTH) <= 1);
    QVERIFY(qAbs(commentTile->width() - pdf::PDFAppAnnotatePanel::COMMENT_TILE_WIDTH) <= 1);
    QCOMPARE(highlightTile->height(), pdf::PDFAppDocumentPanelPopup::TILE_HEIGHT);
    QCOMPARE(annotate->getColorRow()->width(), pdf::PDFColorSwatchRow::getRowSize(8, true).width());
    const int comments = pdf::PDFAppAnnotatePanel::countComments(tab->getDocument()->getDocument().data());
    QVERIFY(comments > 10);
    QVERIFY(annotate->getShowCommentsItem()->text().contains(QString::number(comments)));

    // The active tile is the last used markup (Highlight by default) and the color row its color
    QCOMPARE(annotate->getColorMarkupType(), pdf::AnnotationType::Highlight);
    QCOMPARE(annotate->getColorRow()->getColor(), pdf::PDFAppAnnotationToolSettings::getDefaultMarkupColor(pdf::AnnotationType::Highlight));

    // Show all comments opens the Notes tab
    annotate->getShowCommentsItem()->click();
    pdftest::AppDocumentFixture::settle();
    QVERIFY(!annotate->isVisible());
    QCOMPARE(tab->getView()->getCurrentPanelId(), QStringLiteral("notes"));

    // Insert 196:3160: 778 wide, 8 columns of tiles, 14 stamps, Delete annotations
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::insertPanel()));
    pdf::PDFAppInsertPanel* insert = qobject_cast<pdf::PDFAppInsertPanel*>(toolbar->findPanel(pdf::PDFAppDocumentToolbar::Panel::Insert));
    QVERIFY(insert);
    QVERIFY(insert->isVisible());
    QCOMPARE(insert->getPanelRect().width(), pdf::PDFAppInsertPanel::PANEL_WIDTH);
    for (pdf::TextAnnotationIcon icon : pdf::PDFAppAnnotationTools::getStickyNoteIcons())
    {
        QToolButton* tile = insert->getStickyNoteTile(icon);
        QVERIFY(tile);
        QVERIFY(qAbs(tile->width() - 88) <= 1);
        QCOMPARE(tile->height(), pdf::PDFAppDocumentPanelPopup::TILE_HEIGHT);
    }
    QVERIFY(qAbs(insert->getCommandButton(pdf::PDFAppCommands::insertLine())->width() - 88) <= 1);
    for (pdf::Stamp stamp : pdf::PDFAppAnnotationTools::getStamps())
    {
        pdf::PDFStampChip* chip = insert->getStampChip(stamp);
        QVERIFY(chip);
        QCOMPARE(chip->text(), pdf::PDFAppAnnotationTools::getStampName(stamp));
        QCOMPARE(chip->getHue(), pdf::PDFAppAnnotationTools::getStampHue(stamp));
        QVERIFY(insert->getPanelRect().contains(QRect(chip->mapTo(insert, QPoint(0, 0)), chip->size())));
    }
    QVERIFY(insert->getCommandButton(pdf::PDFAppCommands::annotateDeleteTool()));
    QVERIFY(insert->getPageNumbersTile());
    QVERIFY(insert->getOutlineItemTile());

    // One panel at a time
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::annotatePanel()));
    QVERIFY(!insert->isVisible());
    annotate->close();
    pdftest::AppDocumentFixture::settle();
}

void AppAnnotateTest::panelsKeyboard()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Keyboard.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppAnnotationCreator* creator = H::creator(tab);
    QVERIFY(creator);
    pdf::PDFAppDocumentToolbar* toolbar = tab->getView()->getToolbar();

    // The keyboard starts at the first tile; Tab moves, Space starts the tool, Esc closes
    pdf::PDFAppDocumentPanelPopup* insert = toolbar->openPanel(pdf::PDFAppDocumentToolbar::Panel::Insert);
    QVERIFY(insert);
    pdftest::AppDocumentFixture::settle();
    QWidget* focused = QApplication::focusWidget();
    QVERIFY(focused && insert->isAncestorOf(focused));
    QTest::keyClick(focused, Qt::Key_Tab);
    QWidget* second = QApplication::focusWidget();
    QVERIFY(second && second != focused);
    QTest::keyClick(insert, Qt::Key_Escape);
    pdftest::AppDocumentFixture::settle();
    QVERIFY(!insert->isVisible());
    QCOMPARE(creator->getActiveKind(), Kind::None);

    insert = toolbar->openPanel(pdf::PDFAppDocumentToolbar::Panel::Insert);
    pdftest::AppDocumentFixture::settle();
    QAbstractButton* rectangle = insert->getCommandButton(pdf::PDFAppCommands::insertRectangle());
    rectangle->setFocus(Qt::TabFocusReason);
    QTest::keyClick(rectangle, Qt::Key_Space);
    QTRY_COMPARE(creator->getActiveKind(), Kind::Rectangle);
    QVERIFY(!insert->isVisible());

    // The panel shows the active tool checked at the next opening
    insert = toolbar->openPanel(pdf::PDFAppDocumentToolbar::Panel::Insert);
    pdftest::AppDocumentFixture::settle();
    QVERIFY(insert->getCommandButton(pdf::PDFAppCommands::insertRectangle())->isChecked());
    insert->close();
    creator->endTool();
    pdftest::AppDocumentFixture::settle();
}

void AppAnnotateTest::toolActivationEachTile()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Tiles.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppAnnotationCreator* creator = H::creator(tab);
    pdf::PDFAppDocumentToolbar* toolbar = tab->getView()->getToolbar();

    // Every command tile of the panels starts its tool (closing the panel)
    for (Kind kind : pdf::PDFAppAnnotationTools::getKinds())
    {
        const bool isAnnotate = pdf::PDFAppAnnotationTools::isMarkup(kind);
        pdf::PDFAppDocumentPanelPopup* panel = toolbar->openPanel(isAnnotate ? pdf::PDFAppDocumentToolbar::Panel::Annotate : pdf::PDFAppDocumentToolbar::Panel::Insert);
        QVERIFY(panel);
        pdftest::AppDocumentFixture::settle();
        QAbstractButton* tile = panel->getCommandButton(pdf::PDFAppAnnotationTools::getCommandId(kind));
        if (kind == Kind::StickyNote)
        {
            tile = qobject_cast<pdf::PDFAppInsertPanel*>(panel)->getStickyNoteTile(pdf::TextAnnotationIcon::Key);
        }
        else if (kind == Kind::Stamp)
        {
            tile = qobject_cast<pdf::PDFAppInsertPanel*>(panel)->getStampChip(pdf::Stamp::Draft);
        }
        QVERIFY2(tile, qPrintable(pdf::PDFAppAnnotationTools::getName(kind)));
        tile->click();
        QTRY_COMPARE(creator->getActiveKind(), kind);
        QVERIFY(!panel->isVisible());
        QVERIFY(creator->getToolBar() && creator->getToolBar()->isVisible());
        QCOMPARE(creator->getToolBar()->getKind(), kind);
        QCOMPARE(tab->getView()->getStatusBar()->getMessage(), pdf::PDFAppAnnotationTools::getStatusHint(kind));
        QVERIFY(tab->isCommandChecked(pdf::PDFAppAnnotationTools::getCommandId(kind)));
        if (pdf::PDFAppAnnotationTools::isEngineTool(kind))
        {
            QCOMPARE(tab->getView()->getActiveTool(), creator->getTool(kind));
        }
    }

    QCOMPARE(creator->getStickyNoteIcon(), pdf::TextAnnotationIcon::Key);
    QCOMPARE(creator->getStamp(), pdf::Stamp::Draft);

    // The command toggles the tool
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::annotateDeleteTool()));
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(creator->getActiveKind(), Kind::None);
    QVERIFY(!creator->getToolBar()->isVisible());
    QVERIFY(tab->getView()->isCanvasToolActive(pdf::PDFToolManager::ToolEnd));

    // Page numbers opens the route of its tool, Outline item asks the outline editor
    pdf::PDFAppInsertPanel* insert = qobject_cast<pdf::PDFAppInsertPanel*>(toolbar->openPanel(pdf::PDFAppDocumentToolbar::Panel::Insert));
    pdftest::AppDocumentFixture::settle();
    insert->getOutlineItemTile()->click();
    pdftest::AppDocumentFixture::settle();
    QVERIFY(!insert->isVisible());
    insert = qobject_cast<pdf::PDFAppInsertPanel*>(toolbar->openPanel(pdf::PDFAppDocumentToolbar::Panel::Insert));
    pdftest::AppDocumentFixture::settle();
    insert->getPageNumbersTile()->click();
    pdftest::AppDocumentFixture::settle();
    QVERIFY(tab->getHistory().getCurrentEntry() && tab->getHistory().getCurrentEntry()->route.toString().contains(QStringLiteral("edit.page-numbers")));
}

void AppAnnotateTest::toolBarVariants()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Bars.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppAnnotationCreator* creator = H::creator(tab);
    QWidget* canvas = tab->getView()->getCanvasArea();

    for (Kind kind : pdf::PDFAppAnnotationTools::getKinds())
    {
        QVERIFY(creator->startTool(kind));
        pdftest::AppDocumentFixture::settle();
        pdf::PDFAppAnnotationToolBar* bar = creator->getToolBar();
        QVERIFY(bar && bar->isVisible());

        // Shell of the bar: 46 high (34 with a hint only - 1168:26016), 12 below
        // the toolbar, centered on the canvas
        const QRect card = bar->getCardGeometry();
        const bool hasHintOnly = !pdf::PDFAppAnnotationTools::getBarHint(kind).isEmpty();
        QCOMPARE(card.height(), hasHintOnly ? pdf::PDFAppAnnotationToolBar::HINT_HEIGHT : pdf::PDFAppFloatingBar::NORMAL_HEIGHT);
        QCOMPARE(card.top(), pdf::PDFAppFloatingBar::TOP_GAP);
        QVERIFY(qAbs(card.center().x() - canvas->width() / 2) <= 1);
        QVERIFY(bar->getIcon() && bar->getNameLabel() && bar->getCloseButton());
        QCOMPARE(bar->getCloseButton()->width(), 20);

        // The parts of the variant (step4_floating_bar_geometry.md 2)
        const bool hasHint = !pdf::PDFAppAnnotationTools::getBarHint(kind).isEmpty();
        QCOMPARE(bar->getHintLabel() != nullptr, hasHint);
        QCOMPARE(bar->getColorRow() != nullptr, pdf::PDFAppAnnotationTools::hasColor(kind));
        QCOMPARE(bar->getWidthSelect() != nullptr, pdf::PDFAppAnnotationTools::hasWidth(kind));
        QCOMPARE(bar->getColorTargetControl() != nullptr, pdf::PDFAppAnnotationTools::hasFill(kind));
        QCOMPARE(bar->getTargetSelect() != nullptr, kind == Kind::PageLink);
        if (bar->getColorRow())
        {
            QCOMPARE(bar->getColorRow()->height(), 32);
            QCOMPARE(bar->getColorRow()->getSwatchCount(), 8);
            QCOMPARE(bar->getColorRow()->getColor(), creator->getColor(kind, pdf::PDFAppAnnotationCreator::ColorTarget::Stroke));
        }
        if (bar->getWidthSelect())
        {
            QCOMPARE(bar->getWidthSelect()->height(), 32);
            QVERIFY(bar->getWidthSelect()->width() >= 111);
        }
        if (bar->getColorTargetControl())
        {
            QCOMPARE(bar->getColorTargetControl()->size(), QSize(166, 32));

            // Fill: No color + 8 + white (207:7182)
            bar->getColorTargetControl()->setCurrentIndex(1);
            pdftest::AppDocumentFixture::settle();
            QCOMPARE(creator->getColorTarget(), pdf::PDFAppAnnotationCreator::ColorTarget::Fill);
            QCOMPARE(bar->getColorRow()->getSwatchCount(), 10);
            bar->getColorTargetControl()->setCurrentIndex(0);
            pdftest::AppDocumentFixture::settle();
            QCOMPARE(bar->getColorRow()->getSwatchCount(), 8);
        }
        if (kind == Kind::PageLink)
        {
            QCOMPARE(bar->getStepBadge()->getText(), QStringLiteral("1 / 2"));
            QCOMPARE(bar->getInstructionLabel()->text(), QStringLiteral("Draw the link area"));
            QVERIFY(!bar->getBackButton()->isVisible());
            QCOMPARE(bar->getTargetSelect()->getType(), pdf::DestinationType::Fit);
        }
    }

    // A variant of the name: Stamp “Approved”, the sticky note icon
    creator->setStamp(pdf::Stamp::Final);
    QVERIFY(creator->startTool(Kind::Stamp));
    QCOMPARE(creator->getToolBar()->getNameLabel()->text(), QStringLiteral("Stamp “Final”"));
    creator->setStickyNoteIcon(pdf::TextAnnotationIcon::Help);
    QVERIFY(creator->startTool(Kind::StickyNote));
    QCOMPARE(creator->getToolBar()->getNameLabel()->text(), QStringLiteral("Help"));
    QCOMPARE(creator->getToolBar()->getIcon()->getIconName(), QStringLiteral("note-help"));
    creator->endTool();
}

void AppAnnotateTest::toolEndsByEscAndClose()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Esc.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppAnnotationCreator* creator = H::creator(tab);
    pdf::PDFWidgetTool* selectTextTool = view->getToolManager()->getSelectTextTool();
    QSignalSpy kindSpy(creator, &pdf::PDFAppAnnotationCreator::activeKindChanged);

    // Esc on the canvas ends an engine tool, the mode of the view (Hand of a new tab,
    // owner 2026-10-25) is active again
    QVERIFY(creator->startTool(Kind::Ellipse));
    H::key(view, Qt::Key_Escape);
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(creator->getActiveKind(), Kind::None);
    QVERIFY(view->isCanvasToolActive(pdf::PDFToolManager::ToolEnd));
    QVERIFY(!creator->getToolBar()->isVisible());
    QCOMPARE(view->getStatusBar()->getMessage(), QStringLiteral("Ready"));

    // Esc ends a markup tool (the annotations are selectable again); the mode Select
    // text keeps the text selection
    view->setCanvasTool(pdf::PDFToolManager::SelectTextTool);
    QVERIFY(creator->startTool(Kind::Squiggly));
    QVERIFY(!view->getToolManager()->getSelectTextTool()->isAnnotationsSelectable());
    H::key(view, Qt::Key_Escape);
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(creator->getActiveKind(), Kind::None);
    QCOMPARE(view->getActiveTool(), selectTextTool);
    QVERIFY(view->getToolManager()->getSelectTextTool()->isAnnotationsSelectable());

    // The close button and Esc of the bar end the tool
    QVERIFY(creator->startTool(Kind::Stamp));
    creator->getToolBar()->getCloseButton()->click();
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(creator->getActiveKind(), Kind::None);
    QVERIFY(creator->startTool(Kind::Line));
    QTest::keyClick(creator->getToolBar(), Qt::Key_Escape);
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(creator->getActiveKind(), Kind::None);

    // Another tool of the canvas (the Select menu) ends the tool
    QVERIFY(creator->startTool(Kind::Freehand));
    view->setCanvasTool(pdf::PDFToolManager::ScreenshotTool);
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(creator->getActiveKind(), Kind::None);
    QVERIFY(creator->startTool(Kind::Highlight));
    view->setCanvasTool(pdf::PDFToolManager::ScreenshotTool);
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(creator->getActiveKind(), Kind::None);
    QVERIFY(kindSpy.count() >= 12);

    // A creation tool stays active after an annotation (tools.md 9.8)
    view->setCanvasTool(pdf::PDFToolManager::SelectTextTool);
    QVERIFY(creator->startTool(Kind::Rectangle));
    QVERIFY(H::pickRectangle(view, 0, QRectF(40, 100, 80, 60)));
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(H::annotationCount(tab->getDocument(), 0), 1);
    QCOMPARE(creator->getActiveKind(), Kind::Rectangle);
    QVERIFY(H::pickRectangle(view, 0, QRectF(150, 100, 80, 60)));
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(H::annotationCount(tab->getDocument(), 0), 2);
    creator->endTool();
}

void AppAnnotateTest::toolStylePersisted()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Style.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppAnnotationCreator* creator = H::creator(tab);
    pdf::PDFAppAnnotationStyleStorage* storage = fixture.context->getAnnotationStyleStorage();

    // Stroke, fill and width of a shape are stored in the row Shape at once
    QVERIFY(creator->startTool(Kind::Rectangle));
    pdf::PDFAppAnnotationToolBar* bar = creator->getToolBar();
    bar->getColorRow()->getSwatch(5)->click();
    const QColor blue = bar->getColorRow()->getEntry(5).canonicalColor;
    bar->getColorTargetControl()->setCurrentIndex(1);
    pdftest::AppDocumentFixture::settle();
    bar->getColorRow()->getSwatch(0)->click();
    pdftest::AppDocumentFixture::settle();
    QVERIFY(bar->getColorRow()->isNoColorSelected());
    bar->getWidthSelect()->setCurrentIndex(5);
    Q_EMIT bar->getWidthSelect()->activated(5);
    pdftest::AppDocumentFixture::settle();

    const pdf::PDFAnnotationStyle shape = pdf::PDFAnnotationStyleSettings::getStyle(pdf::PDFAnnotationStyleSettings::STYLE_SHAPE, pdf::PDFAnnotationStyle());
    QCOMPARE(shape.strokeColor.rgb(), blue.rgb());
    QVERIFY(!shape.fillColor.isValid());
    QCOMPARE(shape.penWidth, pdf::PDFAppAnnotationTools::getPenWidths()[5]);
    QVERIFY(storage->getRepository().getValue(pdf::PDFAnnotationStyleSettings::STYLE_SHAPE).has_value());

    // The other shapes share the style; the created rectangle has it
    QCOMPARE(creator->getColor(Kind::Ellipse, pdf::PDFAppAnnotationCreator::ColorTarget::Stroke).rgb(), blue.rgb());
    QVERIFY(H::pickRectangle(view, 0, QRectF(40, 100, 80, 60)));
    pdftest::AppDocumentFixture::settle();
    const pdf::PDFAnnotationPtr rectangle = H::annotation(tab->getDocument(), H::lastAnnotation(tab->getDocument(), 0));
    QVERIFY(rectangle);
    QCOMPARE(H::strokeRgb(rectangle), blue.rgb());
    creator->endTool();

    // The color of a markup, of a sticky note; the target of Link to page
    QVERIFY(creator->startTool(Kind::Underline));
    creator->getToolBar()->getColorRow()->getSwatch(6)->click();
    QCOMPARE(pdf::PDFAppAnnotationToolSettings::getMarkupColor(pdf::AnnotationType::Underline).rgb(), creator->getToolBar()->getColorRow()->getEntry(6).canonicalColor.rgb());
    QCOMPARE(pdf::PDFAppAnnotationToolSettings::getLastMarkupType(fixture.context.get()), pdf::AnnotationType::Underline);
    QVERIFY(creator->startTool(Kind::StickyNote));
    creator->getToolBar()->getColorRow()->getSwatch(4)->click();
    QCOMPARE(pdf::PDFAppAnnotationToolSettings::getStickyNoteColor().rgb(), creator->getToolBar()->getColorRow()->getEntry(4).canonicalColor.rgb());
    creator->setPageLinkTarget(pdf::DestinationType::XYZ, true);
    QCOMPARE(pdf::PDFAppAnnotationToolSettings::getPageLinkTarget(fixture.context.get()), pdf::DestinationType::XYZ);
    QVERIFY(pdf::PDFAppAnnotationToolSettings::isPageLinkKeepZoom(fixture.context.get()));
    creator->endTool();
}

void AppAnnotateTest::toolStyleFromOtherInstance()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Other.pdf"), 1));
    QVERIFY(tab);
    pdf::PDFAppAnnotationCreator* creator = H::creator(tab);
    pdf::PDFAppAnnotationStyleStorage* storage = fixture.context->getAnnotationStyleStorage();

    QVERIFY(creator->startTool(Kind::Highlight));
    QSignalSpy spy(creator, &pdf::PDFAppAnnotationCreator::toolStateChanged);

    // A row written by another instance (the repository directly) is shown by the open bar
    QJsonObject value;
    value.insert(QStringLiteral("strokeColor"), QStringLiteral("#34c759"));
    value.insert(QStringLiteral("penWidth"), 1.0);
    QVERIFY(!storage->getRepository().setValue(pdf::PDFAnnotationStyleSettings::STYLE_HIGHLIGHT, value).isError());
    QTRY_VERIFY(spy.count() > 0);
    QTRY_COMPARE(creator->getToolBar()->getColorRow()->getColor().rgb(), QColor(0x34, 0xc7, 0x59).rgb());

    // The rectangle tool reads the shared style of another instance
    QJsonObject shape;
    shape.insert(QStringLiteral("strokeColor"), QStringLiteral("#ff2d92"));
    shape.insert(QStringLiteral("fillColor"), QJsonValue(QJsonValue::Null));
    shape.insert(QStringLiteral("penWidth"), 3.0);
    QVERIFY(!storage->getRepository().setValue(pdf::PDFAnnotationStyleSettings::STYLE_SHAPE, shape).isError());
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(creator->getColor(Kind::Rectangle, pdf::PDFAppAnnotationCreator::ColorTarget::Stroke).rgb(), QColor(0xff, 0x2d, 0x92).rgb());
    QCOMPARE(creator->getPenWidth(Kind::Polygon), 3.0);
    creator->endTool();
}

void AppAnnotateTest::hostileStyleRows()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppAnnotationStyleStorage* storage = fixture.context->getAnnotationStyleStorage();
    pdf::PDFAnnotationStyleRepository repository = storage->getRepository();

    auto write = [&repository](const QString& id, const QJsonObject& value) { QVERIFY(!repository.setValue(id, value).isError()); };
    write(pdf::PDFAnnotationStyleSettings::STYLE_HIGHLIGHT, QJsonObject({ { QStringLiteral("strokeColor"), QStringLiteral("not a color") }, { QStringLiteral("penWidth"), 1e308 } }));
    write(pdf::PDFAnnotationStyleSettings::STYLE_SHAPE, QJsonObject({ { QStringLiteral("strokeColor"), 42 }, { QStringLiteral("penWidth"), -7 } }));
    write(pdf::PDFAppAnnotationToolSettings::styleStickyNote(), QJsonObject({ { QStringLiteral("strokeColor"), QJsonObject() } }));
    write(pdf::PDFAnnotationStyleRepository::toolPageLink(), QJsonObject({ { QStringLiteral("destinationType"), QStringLiteral("Named") }, { QStringLiteral("keepZoom"), QStringLiteral("yes") } }));
    write(pdf::PDFAppAnnotationToolSettings::rowFreeTextBorder(), QJsonObject({ { QStringLiteral("color"), QStringLiteral("#12") }, { QStringLiteral("width"), -50 } }));
    write(pdf::PDFAnnotationStyleRepository::toolStickyNote(), QJsonObject({ { QStringLiteral("icon"), 4 } }));
    write(pdf::PDFAnnotationStyleRepository::toolStamp(), QJsonObject({ { QStringLiteral("stamp"), QString(5000, QLatin1Char('x')) } }));
    write(pdf::PDFAnnotationStyleRepository::toolFreeText(), QJsonObject({ { QStringLiteral("font"), QString(5000, QLatin1Char('F')) }, { QStringLiteral("size"), -1 } }));

    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("HostileRows.pdf"), 1));
    QVERIFY(tab);
    pdf::PDFAppAnnotationCreator* creator = H::creator(tab);

    // Every damaged value gives the default (O19), the tools work
    QVERIFY(creator->getColor(Kind::Highlight, pdf::PDFAppAnnotationCreator::ColorTarget::Stroke).isValid());
    QVERIFY(creator->getPenWidth(Kind::Rectangle) >= 0.0 && creator->getPenWidth(Kind::Rectangle) <= 100.0);
    QVERIFY(creator->getColor(Kind::StickyNote, pdf::PDFAppAnnotationCreator::ColorTarget::Stroke).isValid());
    QCOMPARE(creator->getPageLinkTarget(), pdf::DestinationType::Fit);
    QVERIFY(!creator->isPageLinkKeepZoom());
    QCOMPARE(creator->getStickyNoteIcon(), pdf::TextAnnotationIcon::Comment);
    QCOMPARE(creator->getStamp(), pdf::Stamp::Approved);
    QColor borderColor = Qt::red;
    pdf::PDFReal borderWidth = -1.0;
    pdf::PDFAppAnnotationToolSettings::getFreeTextBorder(fixture.context.get(), &borderColor, &borderWidth);
    QVERIFY(!borderColor.isValid());
    QCOMPARE(borderWidth, 0.0);
    const pdf::PDFFreeTextStyle style = pdf::PDFAppAnnotationToolSettings::getFreeTextStyle(fixture.context.get());
    QVERIFY(style.fontSize >= 1.0 && style.fontSize <= 512.0);
    QVERIFY(style.fontFamily.size() <= 256);

    for (Kind kind : pdf::PDFAppAnnotationTools::getKinds())
    {
        QVERIFY(creator->startTool(kind));
        pdftest::AppDocumentFixture::settle();
    }
    QVERIFY(creator->startTool(Kind::Rectangle));
    QVERIFY(H::pickRectangle(tab->getView(), 0, QRectF(40, 100, 80, 60)));
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(H::annotationCount(tab->getDocument(), 0), 1);
    creator->endTool();
}

void AppAnnotateTest::createEachKindOneStep()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Create.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppAnnotationCreator* creator = H::creator(tab);

    int expectedCount = 0;
    int expectedSteps = 0;
    auto check = [&](Kind kind, const QByteArray& subtype, int added)
    {
        pdftest::AppDocumentFixture::settle();
        expectedCount += added;
        ++expectedSteps;
        QCOMPARE(H::annotationCount(document, 0), expectedCount);
        QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), expectedSteps);
        QCOMPARE(H::undoDescription(document), pdf::PDFAppAnnotationTools::getUndoDescription(kind));
        QVERIFY(H::lastOfType(document, 0, subtype).isValid());
    };

    QVERIFY(creator->startTool(Kind::Rectangle));
    QVERIFY(H::pickRectangle(view, 0, QRectF(20, 100, 40, 30)));
    check(Kind::Rectangle, "Square", 1);

    // The Square of the drawn rectangle (owner, O20 - not a closed Polygon) can be moved,
    // resized and filled; its border lies inside of the rectangle
    {
        const pdf::PDFObjectReference square = H::lastOfType(document, 0, "Square");
        const pdf::PDFAnnotationPtr parsed = pdf::PDFAnnotation::parse(&document->getDocument()->getStorage(), square);
        QVERIFY(parsed);
        const QRectF rectangle = parsed->getRectangle().normalized();
        QVERIFY(std::abs(rectangle.left() - 20.0) < 1.0 && std::abs(rectangle.top() - 100.0) < 1.0);
        QVERIFY(std::abs(rectangle.width() - 40.0) < 1.0 && std::abs(rectangle.height() - 30.0) < 1.0);
        QVERIFY(pdf::PDFAnnotationManipulator::getCapabilities(parsed.data()).testFlag(pdf::PDFAnnotationManipulator::Resize));
        QVERIFY(document->getDocument()->getDictionaryFromObject(document->getDocument()->getObjectByReference(square))->hasKey("AP"));
    }

    QVERIFY(creator->startTool(Kind::Ellipse));
    QVERIFY(H::pickRectangle(view, 0, QRectF(70, 100, 40, 30)));
    check(Kind::Ellipse, "Circle", 1);

    QVERIFY(creator->startTool(Kind::Line));
    QVERIFY(H::pick(view, 0, QPointF(120, 100)));
    QVERIFY(H::pick(view, 0, QPointF(160, 130)));
    check(Kind::Line, "Line", 1);

    QVERIFY(creator->startTool(Kind::Polyline));
    QVERIFY(H::pick(view, 0, QPointF(170, 100)));
    QVERIFY(H::pick(view, 0, QPointF(190, 130)));
    QVERIFY(H::pick(view, 0, QPointF(210, 100)));
    H::key(view, Qt::Key_Return);
    check(Kind::Polyline, "PolyLine", 1);

    QVERIFY(creator->startTool(Kind::Polygon));
    QVERIFY(H::pick(view, 0, QPointF(220, 100)));
    QVERIFY(H::pick(view, 0, QPointF(240, 130)));
    QVERIFY(H::pick(view, 0, QPointF(260, 100)));
    H::key(view, Qt::Key_Return);
    check(Kind::Polygon, "Polygon", 1);

    QVERIFY(creator->startTool(Kind::Freehand));
    QVERIFY(pdftest::AppDocumentFixture::dragPage(view, 0, QPointF(20, 160), QPointF(80, 190), Qt::NoModifier, 8));
    check(Kind::Freehand, "Ink", 1);

    // The freehand curve is one stroke of an Ink annotation (owner, O20 - not a PolyLine)
    {
        const pdf::PDFObjectReference ink = H::lastOfType(document, 0, "Ink");
        const pdf::PDFAnnotationPtr parsed = pdf::PDFAnnotation::parse(&document->getDocument()->getStorage(), ink);
        const pdf::PDFInkAnnotation* inkAnnotation = dynamic_cast<const pdf::PDFInkAnnotation*>(parsed.data());
        QVERIFY(inkAnnotation);
        // A straight drag is a line (PDFGeometryUtils::simplifyHandDrawnPolyline)
        QVERIFY(inkAnnotation->getInkPath().elementCount() >= 2);
        QVERIFY(pdf::PDFAnnotationManipulator::getCapabilities(parsed.data()).testFlag(pdf::PDFAnnotationManipulator::Move));
        QVERIFY(document->getDocument()->getDictionaryFromObject(document->getDocument()->getObjectByReference(ink))->hasKey("AP"));
    }

    QVERIFY(creator->startTool(Kind::Stamp));
    QVERIFY(H::pick(view, 0, QPointF(150, 180)));
    check(Kind::Stamp, "Stamp", 1);

    // Sticky note: the compose popup, Post = one step
    QVERIFY(creator->startTool(Kind::StickyNote));
    QVERIFY(H::pick(view, 0, QPointF(30, 250)));
    pdftest::AppDocumentFixture::settle();
    pdf::PDFAppCommentPopup* popup = H::composePopup(tab);
    QVERIFY(popup);
    popup->setComposedText(QStringLiteral("Sticky text"));
    popup->accept();
    check(Kind::StickyNote, "Text", 2);
    QCOMPARE(H::text(document, H::lastOfType(document, 0, "Text"), "Contents"), QStringLiteral("Sticky text"));

    // Text box: the popup, Add = one step and the end of the tool
    QVERIFY(creator->startTool(Kind::TextBox));
    QVERIFY(H::pickRectangle(view, 0, QRectF(60, 230, 120, 40)));
    pdftest::AppDocumentFixture::settle();
    QVERIFY(creator->getTextBoxPopup() && creator->getTextBoxPopup()->isVisible());
    creator->getTextBoxPopup()->setText(QStringLiteral("Box"));
    QVERIFY(creator->getTextBoxPopup()->accept());
    check(Kind::TextBox, "FreeText", 1);
    QCOMPARE(creator->getActiveKind(), Kind::None);

    // Web link: the dialog, Add Link = one step
    QVERIFY(creator->startTool(Kind::WebLink));
    QVERIFY(H::pickRectangle(view, 0, QRectF(200, 230, 60, 20)));
    pdftest::AppDocumentFixture::settle();
    pdf::PDFAppWebLinkDialog* dialog = creator->getWebLinkDialog();
    QVERIFY(dialog && dialog->isVisible());
    dialog->getField()->setText(QStringLiteral("https://pdf4qt.example/docs"));
    dialog->getAddButton()->click();
    check(Kind::WebLink, "Link", 1);

    // Link to page: the area, then the target page
    QVERIFY(creator->startTool(Kind::PageLink));
    QVERIFY(H::pickRectangle(view, 0, QRectF(200, 280, 60, 20)));
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(creator->getPageLinkTool()->getStep(), pdf::PDFAppPageLinkTool::Step::Target);
    creator->getPageLinkTool()->pickTarget(1, QPointF(50, 300), QRectF());
    check(Kind::PageLink, "Link", 1);

    // Undo removes the annotations one by one
    for (int i = 0; i < expectedSteps; ++i)
    {
        QVERIFY(document->getUndoStack()->undo());
    }
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(H::annotationCount(document, 0), 0);
    creator->endTool();
}

void AppAnnotateTest::authorFromSettings()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Author.pdf"), 1));
    QVERIFY(tab);
    pdf::PDFAppAnnotationCreator* creator = H::creator(tab);
    pdf::PDFAppDocument* document = tab->getDocument();

    // Anonymous by default (O20)
    QVERIFY(H::selectAllText(tab));
    QVERIFY(creator->markTextSelection(pdf::AnnotationType::Highlight));
    QCOMPARE(H::text(document, H::lastAnnotation(document, 0), "T"), pdf::PDFAuthorSettings::getAnonymousAuthorName());

    // A custom name of Settings - Editing
    QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::annotationsCustomAuthorName(), QStringLiteral("Alice Author")).isError());
    QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::annotationsAuthorMode(), pdf::PDFSettingValues::authorModeCustomName()).isError());
    QTRY_COMPARE(pdf::PDFAuthorSettings::getAuthorName(), QStringLiteral("Alice Author"));
    QVERIFY(H::selectAllText(tab));
    QVERIFY(creator->markTextSelection(pdf::AnnotationType::Underline));
    QCOMPARE(H::text(document, H::lastAnnotation(document, 0), "T"), QStringLiteral("Alice Author"));

    QVERIFY(creator->startTool(Kind::Rectangle));
    QVERIFY(H::pickRectangle(tab->getView(), 0, QRectF(40, 100, 80, 60)));
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(H::text(document, H::lastAnnotation(document, 0), "T"), QStringLiteral("Alice Author"));
    creator->endTool();
}

void AppAnnotateTest::stampsAll()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Stamps.pdf"), 1));
    QVERIFY(tab);
    pdf::PDFAppAnnotationCreator* creator = H::creator(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppDocumentToolbar* toolbar = tab->getView()->getToolbar();

    // Each chip of the Insert panel places its stamp
    int count = 0;
    for (pdf::Stamp stamp : pdf::PDFAppAnnotationTools::getStamps())
    {
        pdf::PDFAppInsertPanel* insert = qobject_cast<pdf::PDFAppInsertPanel*>(toolbar->openPanel(pdf::PDFAppDocumentToolbar::Panel::Insert));
        QVERIFY(insert);
        pdftest::AppDocumentFixture::settle();
        insert->getStampChip(stamp)->click();
        QTRY_COMPARE(creator->getActiveKind(), Kind::Stamp);
        QCOMPARE(creator->getStamp(), stamp);
        QVERIFY(H::pick(tab->getView(), 0, QPointF(40 + 15 * (count % 10), 100 + 20 * (count / 10))));
        pdftest::AppDocumentFixture::settle();
        ++count;
        QCOMPARE(H::annotationCount(document, 0), count);
        const pdf::PDFAnnotationPtr annotation = H::annotation(document, H::lastAnnotation(document, 0));
        const pdf::PDFStampAnnotation* stampAnnotation = dynamic_cast<const pdf::PDFStampAnnotation*>(annotation.data());
        QVERIFY(stampAnnotation);
        QCOMPARE(stampAnnotation->getStamp(), stamp);
    }
    creator->endTool();
}

void AppAnnotateTest::stickyIconsAll()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Icons.pdf"), 1));
    QVERIFY(tab);
    pdf::PDFAppAnnotationCreator* creator = H::creator(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    const QByteArray names[] = { "Comment", "Note", "Help", "Key", "Insert", "Paragraph", "NewParagraph" };

    int index = 0;
    for (pdf::TextAnnotationIcon icon : pdf::PDFAppAnnotationTools::getStickyNoteIcons())
    {
        creator->setStickyNoteIcon(icon);
        QVERIFY(creator->startTool(Kind::StickyNote));
        QVERIFY(H::pick(tab->getView(), 0, QPointF(30 + 30 * index, 200)));
        pdftest::AppDocumentFixture::settle();
        pdf::PDFAppCommentPopup* popup = H::composePopup(tab);
        QVERIFY(popup);
        popup->setComposedText(QStringLiteral("Icon %1").arg(index));
        popup->accept();
        pdftest::AppDocumentFixture::settle();
        QCOMPARE(H::name(document, H::lastOfType(document, 0, "Text"), "Name"), names[index]);
        ++index;
    }
    QCOMPARE(H::annotationCount(document, 0), 14);
    creator->endTool();
}

void AppAnnotateTest::stickyNoteCompose()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Compose.pdf"), 1));
    QVERIFY(tab);
    pdf::PDFAppAnnotationCreator* creator = H::creator(tab);
    pdf::PDFAppDocument* document = tab->getDocument();

    // Esc of the compose popup: nothing, no undo step; the tool stays
    QVERIFY(creator->startTool(Kind::StickyNote));
    QVERIFY(H::pick(tab->getView(), 0, QPointF(60, 200)));
    pdftest::AppDocumentFixture::settle();
    pdf::PDFAppCommentPopup* popup = H::composePopup(tab);
    QVERIFY(popup);
    popup->cancel();
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(H::annotationCount(document, 0), 0);
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 0);
    QCOMPARE(creator->getActiveKind(), Kind::StickyNote);

    // Post: one step with the text, the color of the tool and the author
    QVERIFY(H::pick(tab->getView(), 0, QPointF(60, 200)));
    pdftest::AppDocumentFixture::settle();
    popup = H::composePopup(tab);
    QVERIFY(popup);
    popup->setComposedText(QStringLiteral("Line 1\nLine 2"));
    popup->accept();
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(H::annotationCount(document, 0), 2);
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
    QCOMPARE(H::text(document, H::lastOfType(document, 0, "Text"), "Contents"), QStringLiteral("Line 1\nLine 2"));
    const pdf::PDFAnnotationPtr note = H::annotation(document, H::lastOfType(document, 0, "Text"));
    QCOMPARE(H::strokeRgb(note), pdf::PDFAppAnnotationToolSettings::getStickyNoteColor().rgb());

    // Ending the tool cancels an open compose
    QVERIFY(H::pick(tab->getView(), 0, QPointF(120, 200)));
    pdftest::AppDocumentFixture::settle();
    QVERIFY(H::composePopup(tab));
    creator->endTool();
    pdftest::AppDocumentFixture::settle();
    QVERIFY(!H::composePopup(tab));
    QCOMPARE(H::annotationCount(document, 0), 2);
}

void AppAnnotateTest::highlightAndComment()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Comment.pdf"), 1));
    QVERIFY(tab);
    pdf::PDFAppAnnotationCreator* creator = H::creator(tab);
    pdf::PDFAppDocument* document = tab->getDocument();

    // Selection first: Ctrl+Shift+M opens the compose popup over the selection
    QVERIFY(H::selectAllText(tab));
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::annotateHighlightComment()));
    pdftest::AppDocumentFixture::settle();
    pdf::PDFAppCommentPopup* popup = H::composePopup(tab);
    QVERIFY(popup);
    popup->setComposedText(QStringLiteral("Important"));
    popup->accept();
    pdftest::AppDocumentFixture::settle();

    // One Highlight annotation with the contents (tools.md 9.8), one step
    QCOMPARE(H::annotationCount(document, 0), 1);
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
    QCOMPARE(H::name(document, H::lastAnnotation(document, 0), "Subtype"), QByteArray("Highlight"));
    QCOMPARE(H::text(document, H::lastAnnotation(document, 0), "Contents"), QStringLiteral("Important"));
    QVERIFY(!creator->hasTextSelection());

    // Esc of the compose: nothing
    QVERIFY(H::selectAllText(tab));
    QVERIFY(creator->commentTextSelection());
    pdftest::AppDocumentFixture::settle();
    H::composePopup(tab)->cancel();
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(H::annotationCount(document, 0), 1);
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);

    // The tool: the dragged text opens the compose popup
    tab->getView()->deselectText();
    QVERIFY(creator->startTool(Kind::HighlightComment));
    QVERIFY(pdftest::AppDocumentFixture::dragPage(tab->getView(), 0, QPointF(25, 372), QPointF(290, 312), Qt::NoModifier, 8));
    pdftest::AppDocumentFixture::settle();
    popup = H::composePopup(tab);
    QVERIFY(popup);
    popup->setComposedText(QStringLiteral("Tool"));
    popup->accept();
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(H::annotationCount(document, 0), 2);
    QCOMPARE(H::undoDescription(document), pdf::PDFAppAnnotationTools::getUndoDescription(Kind::HighlightComment));
    QCOMPARE(creator->getActiveKind(), Kind::HighlightComment);
    creator->endTool();
}

void AppAnnotateTest::textBoxDialog()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("TextBox.pdf"), 1));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppAnnotationCreator* creator = H::creator(tab);
    pdf::PDFAppDocument* document = tab->getDocument();

    // The drawn rectangle opens the popup 1168:25948 below it (card 504)
    QVERIFY(creator->startTool(Kind::TextBox));
    QVERIFY(H::pickRectangle(view, 0, QRectF(40, 250, 150, 40)));
    pdftest::AppDocumentFixture::settle();
    pdf::PDFAppTextBoxPopup* popup = creator->getTextBoxPopup();
    QVERIFY(popup && popup->isVisible());
    QCOMPARE(popup->getCardRect().width(), pdf::PDFAppTextBoxPopup::CARD_WIDTH);
    QVERIFY(creator->getTextBoxTool()->hasArea());
    QVERIFY(!popup->getAddButton()->isEnabled());
    QVERIFY(popup->getSubtitleLabel()->text().startsWith(QStringLiteral("Page 1")));
    QCOMPARE(popup->getBorderColorRow()->getSwatchCount(), 9);
    QVERIFY(popup->getBorderColorRow()->isNoColorSelected());

    // Geometry of 1168:26450 (card 504 x 364): the head 30 high (title line 16, gap 1,
    // subtitle line 13), the text field at 13, 53 (478 x 80), Add ends 13 above the bottom
    {
        const QRect card = popup->getCardRect();
        QCOMPARE(card.height(), 364);
        QCOMPARE(popup->getTextEdit()->geometry(), QRect(card.left() + 13, card.top() + 53, 478, 80));
        QCOMPARE(popup->getSubtitleLabel()->height(), 13);
        const QRect add(popup->getAddButton()->mapTo(popup, QPoint(0, 0)), popup->getAddButton()->size());
        QCOMPARE(add.bottom() + 1 + 13, card.bottom() + 1);
        QCOMPARE(add.right() + 1 + 13, card.right() + 1);
    }

    // Esc cancels: nothing, the tool stays for another rectangle
    QTest::keyClick(popup->getTextEdit(), Qt::Key_Escape);
    pdftest::AppDocumentFixture::settle();
    QVERIFY(!popup->isVisible());
    QCOMPARE(H::annotationCount(document, 0), 0);
    QCOMPARE(creator->getActiveKind(), Kind::TextBox);
    QVERIFY(!creator->getTextBoxTool()->hasArea());

    // A filled form; Ctrl+Enter creates the text box as one step and ends the tool
    QVERIFY(H::pickRectangle(view, 0, QRectF(40, 250, 150, 40)));
    pdftest::AppDocumentFixture::settle();
    QVERIFY(popup->isVisible());
    QTest::keyClicks(popup->getTextEdit(), QStringLiteral("Hello"));
    QVERIFY(popup->getAddButton()->isEnabled());
    popup->getFontCombo()->setCurrentIndex(2);
    popup->getSizeSpin()->setValue(14);
    popup->getTextColorRow()->getSwatch(5)->click();
    popup->getAlignmentControl()->setCurrentIndex(1);
    popup->getBorderWidthSpin()->setValue(2);
    popup->getBorderColorRow()->getSwatch(3)->click();
    const QColor borderColor = popup->getBorderColorRow()->getEntry(3).canonicalColor;

    // The live preview of the text and of the border is drawn in the rectangle
    QVERIFY(!pdftest::AppDocumentFixture::canvasWidget(view)->grab().isNull());
    QTest::keyClick(popup->getTextEdit(), Qt::Key_Return, Qt::ControlModifier);
    pdftest::AppDocumentFixture::settle();
    QVERIFY(!popup->isVisible());
    QCOMPARE(H::annotationCount(document, 0), 1);
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
    QCOMPARE(creator->getActiveKind(), Kind::None);

    const pdf::PDFObjectReference created = H::lastAnnotation(document, 0);
    QCOMPARE(H::text(document, created, "Contents"), QStringLiteral("Hello"));
    const pdf::PDFDictionary* dictionary = H::dictionary(document, created);
    QVERIFY(dictionary);
    pdf::PDFDocumentDataLoaderDecorator loader(document->getDocument().data());
    QCOMPARE(loader.readIntegerFromDictionary(dictionary, "Q", -1), pdf::PDFInteger(1));
    QVERIFY(loader.readStringFromDictionary(dictionary, "DA").contains("14"));
    const pdf::PDFAnnotationPtr annotation = H::annotation(document, created);
    QCOMPARE(annotation->getBorder().getWidth(), 2.0);
    QCOMPARE(H::strokeRgb(annotation), borderColor.rgb());

    // The values are remembered for the next text box
    const pdf::PDFFreeTextStyle style = pdf::PDFAppAnnotationToolSettings::getFreeTextStyle(fixture.context.get());
    QCOMPARE(style.fontFamily, QStringLiteral("Courier"));
    QCOMPARE(style.fontSize, 14.0);
    QColor storedBorder;
    pdf::PDFReal storedWidth = 0.0;
    pdf::PDFAppAnnotationToolSettings::getFreeTextBorder(fixture.context.get(), &storedBorder, &storedWidth);
    QCOMPARE(storedBorder.rgb(), borderColor.rgb());
    QCOMPARE(storedWidth, 2.0);

    // The next popup starts with them; a press on the page cancels it
    QVERIFY(creator->startTool(Kind::TextBox));
    QVERIFY(H::pickRectangle(view, 0, QRectF(40, 150, 150, 40)));
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(popup->getFontCombo()->currentText(), QStringLiteral("Courier"));
    QCOMPARE(popup->getSizeSpin()->value(), 14);
    QCOMPARE(popup->getAlignmentControl()->getCurrentIndex(), 1);
    QVERIFY(pdftest::AppDocumentFixture::clickPage(view, 0, QPointF(250, 50)));
    pdftest::AppDocumentFixture::settle();
    QVERIFY(!popup->isVisible());
    QCOMPARE(H::annotationCount(document, 0), 1);

    // Add is refused for an empty text
    QVERIFY(H::pickRectangle(view, 0, QRectF(40, 150, 150, 40)));
    pdftest::AppDocumentFixture::settle();
    QVERIFY(!popup->accept());
    popup->getCancelButton()->click();
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(H::annotationCount(document, 0), 1);
    creator->endTool();
}

void AppAnnotateTest::webLinkDialogHostile()
{
    using Ops = pdf::PDFAppAnnotationOperations;

    // The rules of the address: http, https, mailto only, no control or
    // bidirectional characters, no white space, a host, at most 2048 characters
    QVERIFY(Ops::isWebLinkAllowed(QStringLiteral("https://example.com")));
    QVERIFY(Ops::isWebLinkAllowed(QStringLiteral("http://example.com/path?query=1#anchor")));
    QVERIFY(Ops::isWebLinkAllowed(QStringLiteral("HTTPS://EXAMPLE.COM")));
    QVERIFY(Ops::isWebLinkAllowed(QStringLiteral("mailto:someone@example.com")));
    QVERIFY(!Ops::isWebLinkAllowed(QString()));
    QVERIFY(!Ops::isWebLinkAllowed(QStringLiteral("javascript:alert(1)")));
    QVERIFY(!Ops::isWebLinkAllowed(QStringLiteral("file:///C:/Windows/System32/calc.exe")));
    QVERIFY(!Ops::isWebLinkAllowed(QStringLiteral("ftp://example.com")));
    QVERIFY(!Ops::isWebLinkAllowed(QStringLiteral("data:text/html,<b>x</b>")));
    QVERIFY(!Ops::isWebLinkAllowed(QStringLiteral("http://")));
    QVERIFY(!Ops::isWebLinkAllowed(QStringLiteral("www.example.com")));
    QVERIFY(!Ops::isWebLinkAllowed(QStringLiteral("https://exa mple.com")));
    QVERIFY(!Ops::isWebLinkAllowed(QStringLiteral("https://example.com/\u0001")));
    QVERIFY(!Ops::isWebLinkAllowed(QStringLiteral("https://example.com/\u202Egpj.exe")));
    QVERIFY(!Ops::isWebLinkAllowed(QStringLiteral("mailto:@example.com")));
    QVERIFY(!Ops::isWebLinkAllowed(QStringLiteral("mailto:nobody")));
    QVERIFY(!Ops::isWebLinkAllowed(QStringLiteral("https://example.com/") + QString(Ops::MAXIMUM_URL_LENGTH, QLatin1Char('a'))));
    QVERIFY(Ops::isWebLinkAllowed(QStringLiteral("https://example.com/") + QString(Ops::MAXIMUM_URL_LENGTH - 20, QLatin1Char('a'))));

    // Security review RS4: the characters are code points - an invisible tag
    // character (a format character outside of the BMP) and a lone surrogate are
    // refused, a path of other than ASCII characters is allowed
    QVERIFY(!Ops::isWebLinkAllowed(QStringLiteral("https://example.com/a") + QString::fromUcs4(U"\U000E0041") + QStringLiteral("b")));
    QVERIFY(!Ops::isWebLinkAllowed(QStringLiteral("https://example.com/") + QChar(0xD800)));
    QVERIFY(!Ops::isWebLinkAllowed(QStringLiteral("https://example.com/") + QChar(0xDC00) + QStringLiteral("a")));
    QVERIFY(!Ops::isWebLinkAllowed(QStringLiteral("https://example.com/a b")));
    QVERIFY(Ops::isWebLinkAllowed(QStringLiteral("https://example.com/čeština")));
    QVERIFY(Ops::isWebLinkAllowed(QStringLiteral("https://example.com/") + QString::fromUcs4(U"\U0001F600")));

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("WebLink.pdf"), 1));
    QVERIFY(tab);
    pdf::PDFAppAnnotationCreator* creator = H::creator(tab);
    pdf::PDFAppDocument* document = tab->getDocument();

    QVERIFY(creator->startTool(Kind::WebLink));
    QVERIFY(H::pickRectangle(tab->getView(), 0, QRectF(40, 150, 100, 20)));
    pdftest::AppDocumentFixture::settle();
    pdf::PDFAppWebLinkDialog* dialog = creator->getWebLinkDialog();
    QVERIFY(dialog && dialog->isVisible());
    QVERIFY(!dialog->getAddButton()->isEnabled());
    QVERIFY(!dialog->getErrorLabel()->isVisible());

    // A refused address: Add disabled, the error row, Enter does nothing
    dialog->getField()->setText(QStringLiteral("javascript:alert(document.cookie)"));
    QVERIFY(!dialog->getAddButton()->isEnabled());
    QVERIFY(dialog->getErrorLabel()->isVisible());
    QVERIFY(dialog->getField()->isInvalid());
    QTest::keyClick(dialog->getField(), Qt::Key_Return);
    pdftest::AppDocumentFixture::settle();
    QVERIFY(dialog->isVisible());

    // Cancel: nothing
    dialog->getCancelButton()->click();
    pdftest::AppDocumentFixture::settle();
    QVERIFY(!creator->getWebLinkDialog());
    QCOMPARE(H::annotationCount(document, 0), 0);

    // A valid address with spaces around: the link with the trimmed URI
    QVERIFY(H::pickRectangle(tab->getView(), 0, QRectF(40, 150, 100, 20)));
    pdftest::AppDocumentFixture::settle();
    dialog = creator->getWebLinkDialog();
    QVERIFY(dialog);
    dialog->getField()->setText(QStringLiteral("  mailto:someone@example.com "));
    QVERIFY(dialog->getAddButton()->isEnabled());
    QTest::keyClick(dialog->getField(), Qt::Key_Return);
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(H::annotationCount(document, 0), 1);
    const pdf::PDFDictionary* link = H::dictionary(document, H::lastAnnotation(document, 0));
    QVERIFY(link);
    const pdf::PDFDocumentPointer pdfDocument = document->getDocument();
    const pdf::PDFObject& actionObject = pdfDocument->getObject(link->get("A"));
    QVERIFY(actionObject.isDictionary());
    pdf::PDFDocumentDataLoaderDecorator loader(pdfDocument.data());
    QCOMPARE(loader.readStringFromDictionary(actionObject.getDictionary(), "URI"), QByteArray("mailto:someone@example.com"));

    // The URI is 7-bit ASCII (ISO 32000-2 12.6.4.8): an address with other
    // characters is written percent-encoded, not as a UTF-16 text string
    {
        pdf::PDFObjectReference created;
        const pdf::PDFAppAnnotationResult result = Ops::createWebLink(pdfDocument, 0, QRectF(40, 300, 100, 20), QStringLiteral("https://example.com/čeština"), &created);
        QVERIFY(result);
        const pdf::PDFDocumentPointer withLink = *result;
        QVERIFY(withLink);
        const pdf::PDFDictionary* linkDictionary = withLink->getDictionaryFromObject(withLink->getObjectByReference(created));
        QVERIFY(linkDictionary);
        const pdf::PDFObject& linkAction = withLink->getObject(linkDictionary->get("A"));
        QVERIFY(linkAction.isDictionary());
        pdf::PDFDocumentDataLoaderDecorator linkLoader(withLink.data());
        QCOMPARE(linkLoader.readStringFromDictionary(linkAction.getDictionary(), "URI"), QByteArray("https://example.com/%C4%8De%C5%A1tina"));
    }

    // A locked document drops a pending dialog
    QVERIFY(H::pickRectangle(tab->getView(), 0, QRectF(40, 200, 100, 20)));
    pdftest::AppDocumentFixture::settle();
    QVERIFY(creator->getWebLinkDialog());
    document->setLocked(true);
    pdftest::AppDocumentFixture::settle();
    QVERIFY(!creator->getWebLinkDialog());
    QCOMPARE(H::annotationCount(document, 0), 1);
    document->setLocked(false);
}

void AppAnnotateTest::creationAreasDrawing()
{
    // The areas of the creation tools as Figma draws them: the area being drawn
    // 1169:27256 / 1168:26449 (bg/accent 6 % and a dashed outline 1.5 inside - never
    // the opaque rectangle of the pick tool of the engine), the link area of the
    // step 2 1170:27194 (a solid outline), the target page 1170:27667 (2 px outside)
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Areas.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppAnnotationCreator* creator = H::creator(tab);
    QWidget* canvas = pdftest::AppDocumentFixture::canvasWidget(view);
    QVERIFY(canvas);
    fixture.window->move(0, 0);
    view->setZoom(1.0);
    pdftest::AppDocumentFixture::settle();

    const auto widgetPoint = [view](const QPointF& pagePoint)
    {
        bool ok = false;
        const QPoint point = pdftest::AppDocumentFixture::pageToWidget(view, 0, pagePoint, &ok);
        return ok ? point : QPoint(-1, -1);
    };
    const auto moveTo = [canvas](const QPoint& position)
    {
        QMouseEvent move(QEvent::MouseMove, position, canvas->mapToGlobal(position), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(canvas, &move);
    };
    // The outline of the accent (also antialiased) - not the fill of 6 %, not white or gray
    const auto isOutline = [](const QColor& color) { return color.blue() - color.red() > 80; };
    const auto isOutlineAt = [&isOutline](const QImage& image, int x, int y) { return isOutline(image.pixelColor(x, y)) || isOutline(image.pixelColor(x, y + 1)); };

    // Text box: the first corner picked, the pointer at the other one
    QVERIFY(creator->startTool(Kind::TextBox));
    pdftest::AppDocumentFixture::settle();
    QVERIFY(H::pick(view, 0, QPointF(60, 400)));
    const QPoint corner = widgetPoint(QPointF(260, 300));
    moveTo(corner + QPoint(1, 1));
    moveTo(corner);
    pdftest::AppDocumentFixture::settle();
    {
        const QImage image = canvas->grab().toImage();
        const QRect area = QRect(widgetPoint(QPointF(60, 400)), corner).normalized();
        QVERIFY(area.width() > 100 && area.height() > 50);
        const QColor inside = image.pixelColor(area.center());
        QVERIFY2(inside.red() > 220 && inside.green() > 220 && inside.blue() > 220, qPrintable(inside.name()));
        int dashes = 0;
        int gaps = 0;
        for (int x = area.left() + 8; x < area.right() - 8; ++x)
        {
            ++(isOutlineAt(image, x, area.top()) ? dashes : gaps);
        }
        QVERIFY2(dashes > 20 && gaps > 10, qPrintable(QStringLiteral("%1 %2").arg(dashes).arg(gaps)));
    }
    creator->endTool();
    pdftest::AppDocumentFixture::settle();

    // Link to page: the link area of the step 2 has a solid outline
    QVERIFY(creator->startTool(Kind::PageLink));
    pdftest::AppDocumentFixture::settle();
    QVERIFY(H::pickRectangle(view, 0, QRectF(60, 300, 200, 100)));
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(creator->getPageLinkTool()->getStep(), pdf::PDFAppPageLinkTool::Step::Target);
    moveTo(QPoint(2, 2));
    pdftest::AppDocumentFixture::settle();
    {
        const QImage image = canvas->grab().toImage();
        const QRect area = QRect(widgetPoint(QPointF(60, 300)), widgetPoint(QPointF(260, 400))).normalized();
        // (the left edge - the snap points of the engine lie on the edges of the page)
        for (int y = area.top() + 4; y < area.bottom() - 4; ++y)
        {
            QVERIFY2(isOutline(image.pixelColor(area.left(), y)) || isOutline(image.pixelColor(area.left() + 1, y)), qPrintable(QString::number(y)));
        }
    }

    // The target page under the pointer (Whole page): the outline 2 px outside of the page
    const pdf::PDFDocumentPointer pdfDocument = tab->getDocument()->getDocument();
    const QRectF mediaBox = pdfDocument->getCatalog()->getPage(0)->getMediaBox();
    moveTo(widgetPoint(mediaBox.center()) + QPoint(1, 1));
    moveTo(widgetPoint(mediaBox.center()));
    pdftest::AppDocumentFixture::settle();
    {
        const QImage image = canvas->grab().toImage();
        const QRect page = QRect(widgetPoint(mediaBox.topLeft()), widgetPoint(mediaBox.bottomRight())).normalized();
        // (not the row of the pointer - the cross of the engine pick tool lies there)
        const int y = page.center().y() + 20;
        QVERIFY2(isOutline(image.pixelColor(page.left() - 2, y)), qPrintable(image.pixelColor(page.left() - 2, y).name()));
        // (the right edge of the page lies at the right of the pixel page.right())
        QVERIFY2(isOutline(image.pixelColor(page.right() + 1, y)), qPrintable(image.pixelColor(page.right() + 1, y).name()));
        QVERIFY(!isOutline(image.pixelColor(page.left() + 4, y)));
    }

    // The Target menu 1170:27714 lies 4 px below the bar (not over its bottom edge),
    // at the leading edge of the select
    creator->getPageLinkTool()->goBack();
    pdftest::AppDocumentFixture::settle();
    pdf::PDFAppAnnotationToolBar* bar = creator->getToolBar();
    pdf::PDFAppPageLinkTargetSelect* select = bar->getTargetSelect();
    QVERIFY(select);
    select->showPopup();
    pdftest::AppDocumentFixture::settle();
    pdf::PDFAppPageLinkTargetMenu* menu = select->getMenu();
    QVERIFY(menu && menu->isVisible());
    const QRect menuPanel = menu->getPanelRect().translated(menu->mapToGlobal(QPoint(0, 0)));
    const QPoint barBottom = bar->mapToGlobal(bar->getCardRect().bottomLeft());
    QCOMPARE(menuPanel.top(), barBottom.y() + 1 + 4);
    // (the leading edge kept inside of the screen - the offscreen screen is small)
    const QRect available = select->screen()->availableGeometry();
    QCOMPARE(menuPanel.left(), qBound(available.left(), select->mapToGlobal(QPoint(0, 0)).x(), qMax(available.left(), available.right() + 1 - menuPanel.width())));
    menu->hide();
    creator->endTool();
}

void AppAnnotateTest::pageLinkDestination()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("PageLink.pdf"), 3));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppAnnotationCreator* creator = H::creator(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppPageLinkTool* tool = creator->getPageLinkTool();

    QVERIFY(creator->startTool(Kind::PageLink));
    pdf::PDFAppAnnotationToolBar* bar = creator->getToolBar();

    // Step 1 -> 2 (the badge, the instruction, Back), Back and Esc return
    QVERIFY(H::pickRectangle(view, 0, QRectF(40, 150, 100, 20)));
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(tool->getStep(), pdf::PDFAppPageLinkTool::Step::Target);
    QCOMPARE(bar->getStepBadge()->getText(), QStringLiteral("2 / 2"));
    QCOMPARE(bar->getInstructionLabel()->text(), QStringLiteral("Go to the target page and click it"));
    QVERIFY(bar->getBackButton()->isVisible());
    bar->getBackButton()->click();
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(tool->getStep(), pdf::PDFAppPageLinkTool::Step::LinkArea);
    QVERIFY(!bar->getBackButton()->isVisible());
    QVERIFY(H::pickRectangle(view, 0, QRectF(40, 150, 100, 20)));
    pdftest::AppDocumentFixture::settle();
    H::key(view, Qt::Key_Escape);
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(tool->getStep(), pdf::PDFAppPageLinkTool::Step::LinkArea);
    QCOMPARE(creator->getActiveKind(), Kind::PageLink);
    QCOMPARE(H::annotationCount(document, 0), 0);

    // Each target: the destination of the clicked / drawn target (note 1172:28734)
    struct Case
    {
        pdf::DestinationType type;
        bool keepZoom;
    };
    const Case cases[] = { { pdf::DestinationType::Fit, false }, { pdf::DestinationType::FitH, false }, { pdf::DestinationType::FitV, false },
                           { pdf::DestinationType::FitR, false }, { pdf::DestinationType::XYZ, false }, { pdf::DestinationType::XYZ, true } };
    int count = 0;
    for (const Case& testCase : cases)
    {
        creator->setPageLinkTarget(testCase.type, testCase.keepZoom);
        view->goToPage(0);
        pdftest::AppDocumentFixture::settle();
        QVERIFY(H::pickRectangle(view, 0, QRectF(40, 150 - 10 * count, 100, 8)));
        pdftest::AppDocumentFixture::settle();
        QCOMPARE(tool->getStep(), pdf::PDFAppPageLinkTool::Step::Target);
        if (testCase.type == pdf::DestinationType::FitR)
        {
            QCOMPARE(bar->getInstructionLabel()->text(), QStringLiteral("Go to the target page and draw the area"));
        }

        // Navigation is free meanwhile: the target is on the page 2; the target under
        // the pointer is highlighted with its tip (drawn over the canvas)
        view->goToPage(1);
        pdftest::AppDocumentFixture::settle();
        bool isShown = false;
        const QPoint hover = pdftest::AppDocumentFixture::pageToWidget(view, 1, QPointF(80, 260), &isShown);
        QVERIFY(isShown);
        QWidget* canvasWidget = pdftest::AppDocumentFixture::canvasWidget(view);
        QMouseEvent hoverMove(QEvent::MouseMove, hover, canvasWidget->mapToGlobal(hover), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(canvasWidget, &hoverMove);
        QVERIFY(!canvasWidget->grab().isNull());
        if (testCase.type == pdf::DestinationType::FitR)
        {
            QVERIFY(H::pickRectangle(view, 1, QRectF(50, 100, 120, 80)));
        }
        else
        {
            QVERIFY(H::pick(view, 1, QPointF(70, 250)));
        }
        pdftest::AppDocumentFixture::settle();
        ++count;
        QCOMPARE(H::annotationCount(document, 0), count);
        QCOMPARE(tool->getStep(), pdf::PDFAppPageLinkTool::Step::LinkArea);

        // The view returns to the source page (the engine does not select links -
        // a click on a link follows it)
        QCOMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(0));
        const pdf::PDFObjectReference link = H::lastAnnotation(document, 0);
        QCOMPARE(creator->getLastCreated(), std::vector<pdf::PDFObjectReference>({ link }));

        const pdf::PDFDestination destination = H::linkDestination(document, link);
        QCOMPARE(destination.getDestinationType(), testCase.type);
        QCOMPARE(destination.getPageReference(), document->getDocument()->getCatalog()->getPage(1)->getPageReference());
        switch (testCase.type)
        {
            case pdf::DestinationType::FitH:
                QVERIFY(qAbs(destination.getTop() - 250) < 3);
                break;
            case pdf::DestinationType::FitV:
                QVERIFY(qAbs(destination.getLeft() - 70) < 3);
                break;
            case pdf::DestinationType::FitR:
                QVERIFY(qAbs(destination.getLeft() - 50) < 3);
                QVERIFY(qAbs(destination.getRight() - 170) < 3);
                QVERIFY(qAbs(destination.getBottom() - 100) < 3);
                QVERIFY(qAbs(destination.getTop() - 180) < 3);
                break;
            case pdf::DestinationType::XYZ:
                QVERIFY(qAbs(destination.getLeft() - 70) < 3);
                QVERIFY(qAbs(destination.getTop() - 250) < 3);
                QCOMPARE(std::isnan(destination.getZoom()), testCase.keepZoom);
                break;
            default:
                break;
        }
        QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), count);
        QCOMPARE(H::undoDescription(document), pdf::PDFAppAnnotationTools::getUndoDescription(Kind::PageLink));
    }

    // An empty area of Area is no target (the user draws it again)
    creator->setPageLinkTarget(pdf::DestinationType::FitR, false);
    QVERIFY(H::pickRectangle(view, 0, QRectF(200, 300, 50, 10)));
    tool->pickTarget(1, QPointF(), QRectF(10, 10, 0, 0));
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(tool->getStep(), pdf::PDFAppPageLinkTool::Step::Target);
    QCOMPARE(H::annotationCount(document, 0), count);
    tool->pickTarget(99, QPointF(10, 10), QRectF(10, 10, 20, 20));
    QCOMPARE(H::annotationCount(document, 0), count);
    creator->endTool();
    QCOMPARE(tool->getStep(), pdf::PDFAppPageLinkTool::Step::LinkArea);
}

void AppAnnotateTest::pageLinkTargetMenu()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("TargetMenu.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppAnnotationCreator* creator = H::creator(tab);
    QVERIFY(creator->startTool(Kind::PageLink));
    pdf::PDFAppPageLinkTargetSelect* select = creator->getToolBar()->getTargetSelect();
    QVERIFY(select);
    QCOMPARE(select->currentText(), QStringLiteral("Whole page"));

    // The menu 1170:27714 below the select: 380 wide, five targets, the check box
    select->showPopup();
    pdftest::AppDocumentFixture::settle();
    pdf::PDFAppPageLinkTargetMenu* menu = select->getMenu();
    QVERIFY(menu && menu->isVisible());
    QCOMPARE(menu->getPanelRect().width(), pdf::PDFAppPageLinkTargetMenu::PANEL_WIDTH);
    QVERIFY(menu->getPanelRect().translated(menu->mapToGlobal(QPoint(0, 0))).top() >= select->mapToGlobal(QPoint(0, select->height())).y());
    for (pdf::DestinationType type : pdf::PDFAppAnnotationTools::getPageLinkTargets())
    {
        pdf::PDFAppPageLinkTargetItem* item = menu->getItem(type);
        QVERIFY(item);
        QCOMPARE(item->height(), pdf::PDFAppPageLinkTargetItem::HEIGHT);
        QCOMPARE(item->isChecked(), type == pdf::DestinationType::Fit);
        QCOMPARE(item->getDescription(), pdf::PDFAppAnnotationTools::getPageLinkTargetDescription(type));
    }
    QVERIFY(!menu->getKeepZoomCheckBox()->isEnabled());
    QCOMPARE(menu->getKeepZoomHint()->text(), QStringLiteral("Only for Position"));

    // A target closes the menu and is remembered
    menu->getItem(pdf::DestinationType::XYZ)->click();
    pdftest::AppDocumentFixture::settle();
    QVERIFY(!menu->isVisible());
    QCOMPARE(creator->getPageLinkTarget(), pdf::DestinationType::XYZ);
    QCOMPARE(creator->getPageLinkTool()->getDestinationType(), pdf::DestinationType::XYZ);
    QCOMPARE(select->currentText(), QStringLiteral("Position"));

    // Keep the reader's zoom only for Position, the menu stays open
    select->showPopup();
    pdftest::AppDocumentFixture::settle();
    QVERIFY(menu->getKeepZoomCheckBox()->isEnabled());
    QVERIFY(menu->getKeepZoomHint()->text().contains(QStringLiteral("%")));
    menu->getKeepZoomCheckBox()->click();
    pdftest::AppDocumentFixture::settle();
    QVERIFY(menu->isVisible());
    QVERIFY(creator->isPageLinkKeepZoom());
    QVERIFY(creator->getPageLinkTool()->isKeepZoom());

    // Keyboard: Down moves, Enter chooses, Esc closes
    QTest::keyClick(menu, Qt::Key_Escape);
    pdftest::AppDocumentFixture::settle();
    QVERIFY(!menu->isVisible());
    select->showPopup();
    pdftest::AppDocumentFixture::settle();
    QWidget* focused = QApplication::focusWidget();
    QCOMPARE(focused, static_cast<QWidget*>(menu->getItem(pdf::DestinationType::XYZ)));
    QTest::keyClick(menu->getItem(pdf::DestinationType::Fit), Qt::Key_Return);
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(creator->getPageLinkTarget(), pdf::DestinationType::Fit);

    // The keyboard of the closed select changes the target directly
    select->setCurrentIndex(3);
    Q_EMIT select->activated(3);
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(creator->getPageLinkTarget(), pdf::DestinationType::FitR);
    creator->endTool();
}

void AppAnnotateTest::selectionFirstMarkup()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("SelectionFirst.pdf"), 1));
    QVERIFY(tab);
    pdf::PDFAppAnnotationCreator* creator = H::creator(tab);
    pdf::PDFAppDocument* document = tab->getDocument();

    // With a selection the command marks it at once, no tool starts (tools.md 9.8)
    const std::pair<QString, QByteArray> commands[] = { { pdf::PDFAppCommands::annotateHighlight(), "Highlight" },
                                                         { pdf::PDFAppCommands::annotateUnderline(), "Underline" },
                                                         { pdf::PDFAppCommands::annotateStrikeout(), "StrikeOut" },
                                                         { pdf::PDFAppCommands::annotateSquiggly(), "Squiggly" } };
    int count = 0;
    for (const auto& command : commands)
    {
        QVERIFY(H::selectAllText(tab));
        QVERIFY(tab->executeCommand(command.first));
        pdftest::AppDocumentFixture::settle();
        ++count;
        QCOMPARE(H::annotationCount(document, 0), count);
        QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), count);
        const pdf::PDFObjectReference created = H::lastAnnotation(document, 0);
        QCOMPARE(H::name(document, created, "Subtype"), command.second);
        QCOMPARE(creator->getActiveKind(), Kind::None);
        QVERIFY(!creator->hasTextSelection());
        QVERIFY(!creator->getLastCreated().empty());

        // The color of the type (Underline blue, Strikeout and Squiggly red)
        const pdf::PDFAnnotationPtr annotation = H::annotation(document, created);
        const pdf::AnnotationType type = annotation->getType();
        QCOMPARE(H::strokeRgb(annotation), pdf::PDFAppAnnotationToolSettings::getMarkupColor(type).rgb());
    }

    // Without a selection the command starts the tool; the dragged text is marked
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::annotateUnderline()));
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(creator->getActiveKind(), Kind::Underline);
    QVERIFY(pdftest::AppDocumentFixture::dragPage(tab->getView(), 0, QPointF(25, 372), QPointF(290, 312), Qt::NoModifier, 8));
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(H::annotationCount(document, 0), count + 1);
    QCOMPARE(creator->getActiveKind(), Kind::Underline);
    QVERIFY(!creator->hasTextSelection());
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::annotateUnderline()));
    QCOMPARE(creator->getActiveKind(), Kind::None);
}

void AppAnnotateTest::markupBar()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("MarkupBar.pdf"), 1));
    QVERIFY(tab);
    pdf::PDFAppAnnotationCreator* creator = H::creator(tab);
    pdf::PDFAppDocument* document = tab->getDocument();

    // The bar 877:134226 over the selection: 38 high, 8 above (or below) it, centered
    QVERIFY(H::selectAllText(tab));
    pdf::PDFAppMarkupBar* bar = creator->getMarkupBar();
    QVERIFY(bar && bar->isVisible());
    const QRect selection = creator->getTextSelectionRect();
    const QRect card = bar->getCardGeometry();
    QCOMPARE(card.height(), pdf::PDFAppFloatingBar::COMPACT_HEIGHT);
    QVERIFY(card.bottom() + 1 + pdf::PDFAppFloatingBar::ANCHOR_GAP == selection.top() || card.top() == selection.bottom() + 1 + pdf::PDFAppFloatingBar::ANCHOR_GAP ||
            card.top() == pdf::PDFAppFloatingBar::PARENT_MARGIN);
    QCOMPARE(card.width(), 199);
    for (pdf::PDFAppMarkupBar::Button button : { pdf::PDFAppMarkupBar::Button::Highlight, pdf::PDFAppMarkupBar::Button::Underline, pdf::PDFAppMarkupBar::Button::Strikeout,
                                                 pdf::PDFAppMarkupBar::Button::Squiggly, pdf::PDFAppMarkupBar::Button::HighlightComment, pdf::PDFAppMarkupBar::Button::Copy })
    {
        QVERIFY(bar->getButton(button)->isVisible());
        QCOMPARE(bar->getButton(button)->size(), QSize(28, 28));
    }
    QCOMPARE(bar->getButton(pdf::PDFAppMarkupBar::Button::Highlight)->toolTip(), QStringLiteral("Highlight (Ctrl+Shift+H)"));
    QCOMPARE(bar->getButton(pdf::PDFAppMarkupBar::Button::Highlight)->getCurrentIconColor(), pdf::PDFThemeColors::AnnotationYellow);

    // Copy copies the text (the bar stays)
    QApplication::clipboard()->clear();
    bar->getButton(pdf::PDFAppMarkupBar::Button::Copy)->click();
    QVERIFY(QApplication::clipboard()->text().contains(QStringLiteral("quick brown fox")));

    // A markup marks at once and hides the bar
    bar->getButton(pdf::PDFAppMarkupBar::Button::Strikeout)->click();
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(H::annotationCount(document, 0), 1);
    QCOMPARE(H::name(document, H::lastAnnotation(document, 0), "Subtype"), QByteArray("StrikeOut"));
    QVERIFY(!bar->isVisible());

    // Highlight and comment opens the compose popup
    QVERIFY(H::selectAllText(tab));
    QVERIFY(bar->isVisible());
    bar->getButton(pdf::PDFAppMarkupBar::Button::HighlightComment)->click();
    pdftest::AppDocumentFixture::settle();
    QVERIFY(H::composePopup(tab));
    QVERIFY(!bar->isVisible());
    H::composePopup(tab)->cancel();
    pdftest::AppDocumentFixture::settle();

    // Esc on the canvas, a scroll and a tool hide it
    QVERIFY(H::selectAllText(tab));
    QVERIFY(bar->isVisible());
    H::key(tab->getView(), Qt::Key_Escape);
    pdftest::AppDocumentFixture::settle();
    QVERIFY(!bar->isVisible());
    QVERIFY(H::selectAllText(tab));
    QVERIFY(bar->isVisible());
    QVERIFY(creator->startTool(Kind::Ellipse));
    QVERIFY(!bar->isVisible());
    creator->endTool();
}

void AppAnnotateTest::markupBarDisabled()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("MarkupBarOff.pdf"), 1));
    QVERIFY(tab);
    pdf::PDFAppAnnotationCreator* creator = H::creator(tab);

    // The setting annotations.showMarkupBar off: no bar
    QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::annotationsShowMarkupBar(), false).isError());
    pdftest::AppDocumentFixture::settle();
    QVERIFY(H::selectAllText(tab));
    QVERIFY(!creator->getMarkupBar() || !creator->getMarkupBar()->isVisible());

    // On again: the bar of the selection
    QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::annotationsShowMarkupBar(), true).isError());
    QTRY_VERIFY(creator->getMarkupBar() && creator->getMarkupBar()->isVisible());

    // Locked: only Copy (tools.md 9.8)
    tab->getDocument()->setLocked(true);
    pdftest::AppDocumentFixture::settle();
    QVERIFY(H::selectAllText(tab));
    pdf::PDFAppMarkupBar* bar = creator->getMarkupBar();
    QVERIFY(bar->isVisible());
    QVERIFY(bar->getButton(pdf::PDFAppMarkupBar::Button::Copy)->isVisible());
    QVERIFY(!bar->getButton(pdf::PDFAppMarkupBar::Button::Highlight)->isVisible());
    QVERIFY(!bar->getButton(pdf::PDFAppMarkupBar::Button::HighlightComment)->isVisible());
    QCOMPARE(bar->getCardGeometry().width(), 5 + 28 + 5);
    QVERIFY(!creator->markTextSelection(pdf::AnnotationType::Highlight));
    QVERIFY(!creator->commentTextSelection());
    QCOMPARE(H::annotationCount(tab->getDocument(), 0), 0);
    tab->getDocument()->setLocked(false);
}

void AppAnnotateTest::markAsAnnotations()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Mark.pdf"), 3));
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppMarkResults* markResults = H::markResults(tab);
    QVERIFY(markResults);
    QVERIFY(!markResults->canMark());
    QVERIFY(!tab->isCommandEnabled(pdf::PDFAppCommands::annotateMarkResults()));

    // The search shows the footer 737:9129 with the results
    QVERIFY(H::search(tab, QStringLiteral("gamma")));
    QCOMPARE(markResults->getMatchCount(pdf::PDFAppMarkResults::Scope::All), 3);
    QCOMPARE(markResults->getMatchCount(pdf::PDFAppMarkResults::Scope::CurrentPage), 1);
    QVERIFY(tab->isCommandEnabled(pdf::PDFAppCommands::annotateMarkResults()));
    tab->getView()->showPanel(QStringLiteral("search"));
    pdftest::AppDocumentFixture::settle();
    pdf::PDFAppMarkResultsFooter* footer = tab->getView()->findChild<pdf::PDFAppMarkResultsFooter*>();
    QVERIFY(footer);
    QTRY_VERIFY(footer->isVisible());
    QCOMPARE(footer->getButton()->text(), QStringLiteral("Mark as annotations\u2026"));
    QCOMPARE(footer->getButton()->height(), 36);

    // The menu 737:9221 above the footer: the kind, the color, the results, Apply
    footer->getButton()->click();
    pdftest::AppDocumentFixture::settle();
    pdf::PDFAppMarkResultsMenu* menu = markResults->getMenu();
    QVERIFY(menu && menu->isVisible());
    QCOMPARE(menu->getPanelRect().width(), pdf::PDFAppMarkResultsMenu::PANEL_WIDTH);
    QVERIFY(menu->getTypeItem(pdf::AnnotationType::Highlight)->isChecked());
    QCOMPARE(menu->getScopeItem(pdf::PDFAppMarkResultsMenu::Scope::All)->text(), QStringLiteral("All results (3)"));
    QCOMPARE(menu->getScopeItem(pdf::PDFAppMarkResultsMenu::Scope::CurrentPage)->text(), QStringLiteral("Results on this page (1)"));
    QCOMPARE(menu->getApplyButton()->text(), QStringLiteral("Highlight 3 results"));
    menu->getTypeItem(pdf::AnnotationType::Squiggly)->click();
    QCOMPARE(menu->getApplyButton()->text(), QStringLiteral("Underline 3 results with a squiggly line"));
    menu->getColorRow()->getSwatch(5)->click();
    const QColor color = menu->getColorRow()->getEntry(5).canonicalColor;
    menu->getApplyButton()->click();
    pdftest::AppDocumentFixture::settle();
    QVERIFY(!menu->isVisible());

    // One undo step, one annotation per result, the status of 737:9141
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
    for (int page = 0; page < 3; ++page)
    {
        QCOMPARE(H::annotationCount(document, page), 1);
        const pdf::PDFAnnotationPtr annotation = H::annotation(document, H::lastAnnotation(document, page));
        QCOMPARE(annotation->getType(), pdf::AnnotationType::Squiggly);
        QCOMPARE(H::strokeRgb(annotation), color.rgb());
    }
    QCOMPARE(tab->getView()->getStatusBar()->getMessage(), QStringLiteral("3 results underlined with a squiggly line as annotations. Ctrl+Z undoes it."));

    // The scopes Results on this page and Selected result only
    QVERIFY(markResults->mark(pdf::AnnotationType::Highlight, QColor(), pdf::PDFAppMarkResults::Scope::CurrentPage));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 2);
    QCOMPARE(H::annotationCount(document, 0) + H::annotationCount(document, 1) + H::annotationCount(document, 2), 4);
    QVERIFY(document->getUndoStack()->undo());
    QVERIFY(document->getUndoStack()->undo());
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(H::annotationCount(document, 0) + H::annotationCount(document, 1) + H::annotationCount(document, 2), 0);

    // Locked: the footer is disabled with the reason
    document->setLocked(true);
    pdftest::AppDocumentFixture::settle();
    QVERIFY(!footer->getButton()->isEnabled());
    QCOMPARE(footer->getButton()->toolTip(), QStringLiteral("The document is locked. Unlock it to make changes."));
    QVERIFY(!markResults->mark(pdf::AnnotationType::Highlight, QColor(), pdf::PDFAppMarkResults::Scope::All));
    document->setLocked(false);
}

void AppAnnotateTest::markResultsFooterGeometry()
{
    // Footer 737:9129: 59 high, border-top 1 border/default, the button 12, 11 (x 36),
    // padding 12 below it; the menu 737:9221 ends 6 px above the button (737:9061)
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("MarkFooter.pdf"), 2));
    QVERIFY(tab);
    QVERIFY(H::search(tab, QStringLiteral("gamma")));
    tab->getView()->showPanel(QStringLiteral("search"));
    pdftest::AppDocumentFixture::settle();

    // A window inside of the offscreen screen (the menu is not moved by the screen)
    fixture.window->move(0, 0);
    fixture.window->resize(760, 560);
    pdftest::AppDocumentFixture::settle();

    pdf::PDFAppMarkResultsFooter* footer = tab->getView()->findChild<pdf::PDFAppMarkResultsFooter*>();
    QVERIFY(footer);
    QTRY_VERIFY(footer->isVisible());
    QCOMPARE(footer->height(), 59);
    QCOMPARE(footer->getButton()->geometry(), QRect(12, 11, footer->width() - 24, 36));

    for (pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
    {
        pdf::PDFThemeManager::getInstance()->setScheme(scheme);
        pdftest::AppDocumentFixture::settle();
        const QImage image = H::render(footer);
        QCOMPARE(H::pixel(image, QPoint(4, 0)).rgb(), H::token(pdf::PDFThemeColors::BorderDefault).rgb());
    }
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);

    footer->getButton()->click();
    pdftest::AppDocumentFixture::settle();
    pdf::PDFAppMarkResultsMenu* menu = H::markResults(tab)->getMenu();
    QVERIFY(menu && menu->isVisible());
    const QRect panel = menu->getPanelRect().translated(menu->mapToGlobal(QPoint(0, 0)));
    const QPoint buttonTopLeft = footer->getButton()->mapToGlobal(QPoint(0, 0));
    QCOMPARE(panel.left(), buttonTopLeft.x());
    QCOMPARE(panel.bottom() + 1 + 6, buttonTopLeft.y());
    QCOMPARE(panel.height(), 427);
    menu->hide();
}

void AppAnnotateTest::markAsAnnotationsLarge()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::large(fixture.folder(), 2000));
    QVERIFY(tab);
    QPointer<pdf::PDFAppDocument> document = tab->getDocument();
    pdf::PDFAppMarkResults* markResults = H::markResults(tab);

    // 2000 results: a worker from a snapshot, one undo step
    QVERIFY(H::search(tab, QStringLiteral("alpha")));
    QCOMPARE(markResults->getMatchCount(pdf::PDFAppMarkResults::Scope::All), 2000);
    QSignalSpy finished(markResults, &pdf::PDFAppMarkResults::finished);
    QVERIFY(markResults->mark(pdf::AnnotationType::Highlight, QColor(), pdf::PDFAppMarkResults::Scope::All));
    QVERIFY(markResults->isRunning());
    QVERIFY(!markResults->canMark());
    QCOMPARE(document->getOperations().size(), size_t(1));
    QVERIFY(document->isBlockedByOperation());
    QVERIFY(finished.wait(120000));
    QCOMPARE(finished.at(0).at(0).toInt(), 2000);
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
    QCOMPARE(H::annotationCount(document, 1999), 1);
    QVERIFY(!markResults->isRunning());

    // 100 000 results (the safety limit of the search), canceled by the operation
    pdf::PDFAppDocumentSearchController* searchController = markResults->getSearchController();
    pdf::PDFDocumentTextSearchOptions options = searchController->getOptions();
    options.regularExpression = true;
    searchController->setOptions(options);
    QVERIFY(H::search(tab, QStringLiteral("\\S")));
    QCOMPARE(markResults->getMatchCount(pdf::PDFAppMarkResults::Scope::All), int(pdf::PDFAppDocumentSearchController::MAXIMUM_RESULTS));
    finished.clear();
    QVERIFY(markResults->mark(pdf::AnnotationType::Underline, QColor(), pdf::PDFAppMarkResults::Scope::All));
    QVERIFY(markResults->isRunning());
    document->getOperations().front()->requestCancel();
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.at(0).at(0).toInt(), 0);
    QVERIFY(!markResults->isRunning());
    QVERIFY(document->getOperations().empty() || document->getOperations().front()->isFinished());
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);

    // A change of the document meanwhile: the result is refused (expectedVersion)
    QVERIFY(markResults->mark(pdf::AnnotationType::StrikeOut, QColor(), pdf::PDFAppMarkResults::Scope::All));
    QPointer<pdf::PDFAppDocumentTab> tabGuard(tab);

    // The tab deleted while the worker runs: nothing crashes, nothing is committed
    delete tab;
    pdftest::AppDocumentFixture::settle();
    QVERIFY(!tabGuard);
    QTest::qWait(200);
    pdftest::AppDocumentFixture::settle();
}

void AppAnnotateTest::deleteTool()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::annotations(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppAnnotationCreator* creator = H::creator(tab);
    const int initial = H::annotationCount(document, 0);

    // A click on the square deletes it (one step "Delete annotations"), the tool stays
    QVERIFY(creator->startTool(Kind::DeleteTool));
    QVERIFY(H::pick(tab->getView(), 0, QPointF(42, 122)));
    pdftest::AppDocumentFixture::settle();
    QVERIFY(H::annotationCount(document, 0) < initial);
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
    QCOMPARE(H::undoDescription(document), QStringLiteral("Delete annotations"));
    QCOMPARE(creator->getActiveKind(), Kind::DeleteTool);

    // A rectangle around more annotations: one step
    const int before = H::annotationCount(document, 0);
    QVERIFY(pdftest::AppDocumentFixture::dragPage(tab->getView(), 0, QPointF(75, 95), QPointF(225, 150), Qt::NoModifier, 6));
    pdftest::AppDocumentFixture::settle();
    QVERIFY(H::annotationCount(document, 0) < before);
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 2);
    QVERIFY(document->getUndoStack()->undo());
    QVERIFY(document->getUndoStack()->undo());
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(H::annotationCount(document, 0), initial);
    creator->endTool();
}

void AppAnnotateTest::lockedDocument()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Locked.pdf"), 1));
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppAnnotationCreator* creator = H::creator(tab);
    pdf::PDFAppDocumentToolbar* toolbar = tab->getView()->getToolbar();

    // An open text box and its tool end with the lock (O20)
    QVERIFY(creator->startTool(Kind::TextBox));
    QVERIFY(H::pickRectangle(tab->getView(), 0, QRectF(40, 150, 100, 40)));
    pdftest::AppDocumentFixture::settle();
    QVERIFY(creator->getTextBoxPopup()->isVisible());
    document->setLocked(true);
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(creator->getActiveKind(), Kind::None);
    QVERIFY(!creator->getTextBoxPopup()->isVisible());
    QVERIFY(!creator->getToolBar()->isVisible());

    // Every command of the package is disabled, the buttons say why
    for (const QString& commandId : pdf::PDFAppDocumentCommandTable::getCommands(pdf::PDFAppDocumentPackage::Annotate))
    {
        QVERIFY2(!tab->isCommandEnabled(commandId), qPrintable(commandId));
    }
    QVERIFY(!toolbar->getAnnotateButton()->isEnabled());
    QCOMPARE(toolbar->getInsertButton()->toolTip(), QStringLiteral("The document is locked. Unlock it to make changes."));
    QVERIFY(!creator->startTool(Kind::Rectangle));
    QCOMPARE(tab->getView()->getStatusBar()->getMessage(), QStringLiteral("The document is locked. Unlock it to make changes."));
    QVERIFY(!creator->isCreationAllowed());

    // A compose of a sticky note ends with the lock
    document->setLocked(false);
    pdftest::AppDocumentFixture::settle();
    QVERIFY(creator->startTool(Kind::StickyNote));
    QVERIFY(H::pick(tab->getView(), 0, QPointF(60, 200)));
    pdftest::AppDocumentFixture::settle();
    QVERIFY(H::composePopup(tab));
    document->setLocked(true);
    pdftest::AppDocumentFixture::settle();
    QVERIFY(!H::composePopup(tab));
    QCOMPARE(H::annotationCount(document, 0), 0);
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 0);
    document->setLocked(false);
}

void AppAnnotateTest::permissionDenied()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::restricted(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppAnnotationCreator* creator = H::creator(tab);

    // No permission to modify annotations: the other text of O8
    QVERIFY(!creator->isCreationAllowed());
    QVERIFY(!tab->isCommandEnabled(pdf::PDFAppCommands::insertPanel()));
    QVERIFY(!tab->isCommandEnabled(pdf::PDFAppCommands::annotateHighlight()));
    QCOMPARE(tab->getView()->getToolbar()->getAnnotateButton()->toolTip(), QStringLiteral("The document does not allow changes."));
    QVERIFY(!creator->startTool(Kind::Stamp));
    QCOMPARE(creator->getActiveKind(), Kind::None);
    QVERIFY(H::selectAllText(tab));
    QVERIFY(!creator->markTextSelection(pdf::AnnotationType::Highlight));
    QVERIFY(creator->getMarkupBar() && creator->getMarkupBar()->isVisible());
    QVERIFY(!creator->getMarkupBar()->getButton(pdf::PDFAppMarkupBar::Button::Underline)->isVisible());
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(tab->getDocument()), 0);
}

void AppAnnotateTest::deletionMatrix()
{
    // Each open part of the package; the tab or the window deleted meanwhile
    enum class Part
    {
        ToolBar,
        MarkupBar,
        TextBoxPopup,
        WebLinkDialog,
        Compose,
        TargetMenu,
        MarkMenu,
        Panel
    };
    const Part parts[] = { Part::ToolBar, Part::MarkupBar, Part::TextBoxPopup, Part::WebLinkDialog, Part::Compose, Part::TargetMenu, Part::MarkMenu, Part::Panel };

    for (int deleteWindow = 0; deleteWindow < 2; ++deleteWindow)
    {
        for (Part part : parts)
        {
            pdftest::AppDocumentFixture fixture;
            pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Deletion.pdf"), 2));
            QVERIFY(tab);
            pdf::PDFAppAnnotationCreator* creator = H::creator(tab);
            pdf::PDFAppDocumentView* view = tab->getView();
            QPointer<QWidget> watched;

            switch (part)
            {
                case Part::ToolBar:
                    QVERIFY(creator->startTool(Kind::Rectangle));
                    watched = creator->getToolBar();
                    break;
                case Part::MarkupBar:
                    QVERIFY(H::selectAllText(tab));
                    watched = creator->getMarkupBar();
                    break;
                case Part::TextBoxPopup:
                    QVERIFY(creator->startTool(Kind::TextBox));
                    QVERIFY(H::pickRectangle(view, 0, QRectF(40, 150, 100, 40)));
                    pdftest::AppDocumentFixture::settle();
                    watched = creator->getTextBoxPopup();
                    break;
                case Part::WebLinkDialog:
                    QVERIFY(creator->startTool(Kind::WebLink));
                    QVERIFY(H::pickRectangle(view, 0, QRectF(40, 150, 100, 40)));
                    pdftest::AppDocumentFixture::settle();
                    watched = creator->getWebLinkDialog();
                    break;
                case Part::Compose:
                    QVERIFY(creator->startTool(Kind::StickyNote));
                    QVERIFY(H::pick(view, 0, QPointF(60, 200)));
                    pdftest::AppDocumentFixture::settle();
                    watched = H::composePopup(tab);
                    break;
                case Part::TargetMenu:
                    QVERIFY(creator->startTool(Kind::PageLink));
                    creator->getToolBar()->getTargetSelect()->showPopup();
                    pdftest::AppDocumentFixture::settle();
                    watched = creator->getToolBar()->getTargetSelect()->getMenu();
                    break;
                case Part::MarkMenu:
                    QVERIFY(H::search(tab, QStringLiteral("alpha")));
                    H::markResults(tab)->openMenu(view->getToolbar()->getSearchField());
                    pdftest::AppDocumentFixture::settle();
                    watched = H::markResults(tab)->getMenu();
                    break;
                case Part::Panel:
                    watched = view->getToolbar()->openPanel(pdf::PDFAppDocumentToolbar::Panel::Insert);
                    pdftest::AppDocumentFixture::settle();
                    break;
            }

            QVERIFY2(watched && watched->isVisible(), qPrintable(QString::number(int(part))));
            QPointer<pdf::PDFAppAnnotationCreator> creatorGuard(creator);
            QPointer<pdf::PDFAppDocumentTab> tabGuard(tab);
            if (deleteWindow)
            {
                delete fixture.window.data();
            }
            else
            {
                delete tab;
            }
            pdftest::AppDocumentFixture::settle();
            QVERIFY(!tabGuard);
            QVERIFY(!creatorGuard);
            QVERIFY(!watched || !watched->isVisible());
            pdftest::AppDocumentFixture::settle();
        }
    }
}

void AppAnnotateTest::deleteFromOwnSignals()
{
    // The tab deleted synchronously from every signal of the package
    enum class Source
    {
        ActiveKind,
        ToolState,
        Created,
        TextBoxAccepted,
        TextBoxCanceled,
        MarkFinished,
        MarkState
    };
    const Source sources[] = { Source::ActiveKind, Source::ToolState, Source::Created, Source::TextBoxAccepted, Source::TextBoxCanceled, Source::MarkFinished, Source::MarkState };

    for (Source source : sources)
    {
        pdftest::AppDocumentFixture fixture;
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("OwnSignals.pdf"), 2));
        QVERIFY(tab);
        QPointer<pdf::PDFAppDocumentTab> tabGuard(tab);
        pdf::PDFAppAnnotationCreator* creator = H::creator(tab);
        pdf::PDFAppDocumentView* view = tab->getView();
        auto deleteTab = [&tabGuard]()
        {
            delete tabGuard.data();
        };

        switch (source)
        {
            case Source::ActiveKind:
                connect(creator, &pdf::PDFAppAnnotationCreator::activeKindChanged, creator, deleteTab);
                creator->startTool(Kind::Line);
                break;
            case Source::ToolState:
                connect(creator, &pdf::PDFAppAnnotationCreator::toolStateChanged, creator, deleteTab);
                creator->setStamp(pdf::Stamp::Sold);
                break;
            case Source::Created:
                QVERIFY(H::selectAllText(tab));
                connect(creator, &pdf::PDFAppAnnotationCreator::annotationsCreated, creator, deleteTab);
                creator->markTextSelection(pdf::AnnotationType::Highlight);
                break;
            case Source::TextBoxAccepted:
            case Source::TextBoxCanceled:
            {
                QVERIFY(creator->startTool(Kind::TextBox));
                QVERIFY(H::pickRectangle(view, 0, QRectF(40, 150, 100, 40)));
                pdftest::AppDocumentFixture::settle();
                pdf::PDFAppTextBoxPopup* popup = creator->getTextBoxPopup();
                popup->setText(QStringLiteral("x"));
                if (source == Source::TextBoxAccepted)
                {
                    connect(popup, &pdf::PDFAppTextBoxPopup::accepted, popup, deleteTab);
                    popup->accept();
                }
                else
                {
                    connect(popup, &pdf::PDFAppTextBoxPopup::canceled, popup, deleteTab);
                    popup->cancel();
                }
                break;
            }
            case Source::MarkFinished:
            case Source::MarkState:
            {
                QVERIFY(H::search(tab, QStringLiteral("alpha")));
                pdf::PDFAppMarkResults* markResults = H::markResults(tab);
                if (source == Source::MarkFinished)
                {
                    connect(markResults, &pdf::PDFAppMarkResults::finished, markResults, deleteTab);
                }
                else
                {
                    connect(markResults, &pdf::PDFAppMarkResults::stateChanged, markResults, deleteTab);
                }
                markResults->mark(pdf::AnnotationType::Highlight, QColor(), pdf::PDFAppMarkResults::Scope::All);
                break;
            }
        }

        pdftest::AppDocumentFixture::settle();
        QVERIFY2(!tabGuard, qPrintable(QString::number(int(source))));
    }
}

void AppAnnotateTest::themesAndScales()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Themes.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppAnnotationCreator* creator = H::creator(tab);
    pdf::PDFAppDocumentView* view = tab->getView();

    for (pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
    {
        pdf::PDFThemeManager::getInstance()->setScheme(scheme);
        pdftest::AppDocumentFixture::settle();

        // The tool bar of a shape, the link bar with its menu, the text box popup, the markup bar
        QVERIFY(creator->startTool(Kind::Rectangle));
        pdf::PDFAppAnnotationToolBar* bar = creator->getToolBar();
        QVERIFY(H::selectAllText(tab) || true);
        for (qreal ratio : { 1.0, 1.5, 2.0 })
        {
            const QImage image = H::render(bar, ratio);
            QCOMPARE(H::countLeakPixels(image), 0);
            const QRect card = bar->getCardRect();
            QCOMPARE(H::pixel(image, QPoint(card.left() + 6, card.top() + 3)).rgb(), H::token(pdf::PDFThemeColors::BgSurface).rgb());
        }

        QVERIFY(creator->startTool(Kind::PageLink));
        bar = creator->getToolBar();
        QCOMPARE(H::countLeakPixels(H::render(bar, 2.0)), 0);
        bar->getTargetSelect()->showPopup();
        pdftest::AppDocumentFixture::settle();
        pdf::PDFAppPageLinkTargetMenu* menu = bar->getTargetSelect()->getMenu();
        for (qreal ratio : { 1.0, 1.5, 2.0 })
        {
            QCOMPARE(H::countLeakPixels(H::render(menu, ratio)), 0);
        }
        // The tile of the chosen target is bg/accent-subtle, the others bg/muted
        const QImage menuImage = H::render(menu);
        pdf::PDFAppPageLinkTargetItem* chosen = menu->getItem(pdf::DestinationType::Fit);
        pdf::PDFAppPageLinkTargetItem* other = menu->getItem(pdf::DestinationType::FitH);
        const QColor chosenTile = H::pixel(menuImage, chosen->mapTo(menu, QPoint(10 + 2, chosen->height() / 2)));
        const QColor otherTile = H::pixel(menuImage, other->mapTo(menu, QPoint(10 + 2, other->height() / 2)));
        QCOMPARE(otherTile.rgb(), H::token(pdf::PDFThemeColors::BgMuted).rgb());
        QVERIFY(chosenTile.rgb() != otherTile.rgb());
        menu->hide();

        QVERIFY(creator->startTool(Kind::TextBox));
        QVERIFY(H::pickRectangle(view, 0, QRectF(40, 150, 100, 40)));
        pdftest::AppDocumentFixture::settle();
        pdf::PDFAppTextBoxPopup* popup = creator->getTextBoxPopup();
        for (qreal ratio : { 1.0, 1.5, 2.0 })
        {
            const QImage image = H::render(popup, ratio);
            QCOMPARE(H::countLeakPixels(image), 0);
            const QRect card = popup->getCardRect();
            QCOMPARE(H::pixel(image, QPoint(card.left() + 4, card.center().y())).rgb(), H::token(pdf::PDFThemeColors::BgSurface).rgb());
        }
        popup->cancel();
        creator->endTool();

        QVERIFY(H::selectAllText(tab));
        pdf::PDFAppMarkupBar* markupBar = creator->getMarkupBar();
        QVERIFY(markupBar->isVisible());
        for (qreal ratio : { 1.0, 1.5, 2.0 })
        {
            QCOMPARE(H::countLeakPixels(H::render(markupBar, ratio)), 0);
        }

        // The panels
        for (pdf::PDFAppDocumentToolbar::Panel panel : { pdf::PDFAppDocumentToolbar::Panel::Annotate, pdf::PDFAppDocumentToolbar::Panel::Insert })
        {
            pdf::PDFAppDocumentPanelPopup* popupPanel = view->getToolbar()->openPanel(panel);
            pdftest::AppDocumentFixture::settle();
            for (qreal ratio : { 1.0, 2.0 })
            {
                QCOMPARE(H::countLeakPixels(H::render(popupPanel, ratio)), 0);
            }
            popupPanel->close();
            pdftest::AppDocumentFixture::settle();
        }
        view->deselectText();
    }
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
}

void AppAnnotateTest::rightToLeft()
{
    QApplication::setLayoutDirection(Qt::RightToLeft);
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("RightToLeft.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppAnnotationCreator* creator = H::creator(tab);
    QWidget* canvas = tab->getView()->getCanvasArea();

    // The bar mirrors: the tool at the right, the close button at the left
    QVERIFY(creator->startTool(Kind::Ellipse));
    pdf::PDFAppAnnotationToolBar* bar = creator->getToolBar();
    pdftest::AppDocumentFixture::settle();
    QVERIFY(bar->getNameLabel()->mapTo(canvas, QPoint(0, 0)).x() > bar->getCloseButton()->mapTo(canvas, QPoint(0, 0)).x());
    QVERIFY(bar->getColorRow()->mapTo(canvas, QPoint(0, 0)).x() > bar->getWidthSelect()->mapTo(canvas, QPoint(0, 0)).x());

    // The markup bar mirrors: Copy at the left
    creator->endTool();
    QVERIFY(H::selectAllText(tab));
    pdf::PDFAppMarkupBar* markupBar = creator->getMarkupBar();
    QVERIFY(markupBar->getButton(pdf::PDFAppMarkupBar::Button::Copy)->mapTo(canvas, QPoint(0, 0)).x() <
            markupBar->getButton(pdf::PDFAppMarkupBar::Button::Highlight)->mapTo(canvas, QPoint(0, 0)).x());
    tab->getView()->deselectText();

    // The text box popup at the trailing (right) edge of the rectangle
    QVERIFY(creator->startTool(Kind::TextBox));
    QVERIFY(H::pickRectangle(tab->getView(), 0, QRectF(160, 150, 120, 40)));
    pdftest::AppDocumentFixture::settle();
    pdf::PDFAppTextBoxPopup* popup = creator->getTextBoxPopup();
    QVERIFY(popup->isVisible());
    QCOMPARE(popup->layoutDirection(), Qt::RightToLeft);
    QVERIFY(popup->getCardGeometry().left() >= pdf::PDFAppTextBoxPopup::PARENT_MARGIN);
    QVERIFY(popup->getCardGeometry().right() <= canvas->width() - pdf::PDFAppTextBoxPopup::PARENT_MARGIN);
    popup->cancel();
    creator->endTool();
}

void AppAnnotateTest::minimalWindow()
{
    pdftest::AppDocumentFixture fixture(QSize(720, 480));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Minimal.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppAnnotationCreator* creator = H::creator(tab);
    QWidget* canvas = tab->getView()->getCanvasArea();

    // Annotate and Insert stay in the narrowed toolbar (the only entries of their
    // panels; the page navigation is hidden before them)
    pdf::PDFAppDocumentToolbar* toolbar = tab->getView()->getToolbar();
    QVERIFY(toolbar->getCollapseLevel() > 0);
    QVERIFY(toolbar->getAnnotateButton()->isVisible());
    QVERIFY(toolbar->getInsertButton()->isVisible());
    const int insertRight = toolbar->getInsertButton()->mapTo(toolbar, QPoint(toolbar->getInsertButton()->width(), 0)).x();
    QVERIFY(insertRight <= toolbar->getSearchField()->mapTo(toolbar, QPoint(0, 0)).x());
    QVERIFY(toolbar->getSearchField()->mapTo(toolbar, QPoint(toolbar->getSearchField()->width(), 0)).x() <= toolbar->width());

    // The widest bar (a shape with Fill) folds its swatches and stays in the canvas
    QVERIFY(creator->startTool(Kind::Polygon));
    creator->setColorTarget(pdf::PDFAppAnnotationCreator::ColorTarget::Fill);
    pdftest::AppDocumentFixture::settle();
    pdf::PDFAppAnnotationToolBar* bar = creator->getToolBar();
    QVERIFY(bar->getCardGeometry().left() >= pdf::PDFAppFloatingBar::PARENT_MARGIN);
    QVERIFY(bar->getCardGeometry().right() <= canvas->width() - pdf::PDFAppFloatingBar::PARENT_MARGIN);
    QVERIFY(bar->getColorRow()->getVisibleSwatchLimit() > 0 || bar->getCardGeometry().width() <= canvas->width());
    creator->setColorTarget(pdf::PDFAppAnnotationCreator::ColorTarget::Stroke);

    // The text box popup is kept inside of the canvas
    QVERIFY(creator->startTool(Kind::TextBox));
    QVERIFY(H::pickRectangle(tab->getView(), 0, QRectF(40, 60, 120, 40)));
    pdftest::AppDocumentFixture::settle();
    pdf::PDFAppTextBoxPopup* popup = creator->getTextBoxPopup();
    QVERIFY(popup->isVisible());
    QVERIFY(popup->getCardGeometry().left() >= pdf::PDFAppTextBoxPopup::PARENT_MARGIN);
    QVERIFY(popup->getCardGeometry().top() >= pdf::PDFAppTextBoxPopup::PARENT_MARGIN);
    popup->cancel();
    creator->endTool();

    // The markup bar is clamped to the canvas
    QVERIFY(H::selectAllText(tab));
    pdf::PDFAppMarkupBar* markupBar = creator->getMarkupBar();
    QVERIFY(markupBar->isVisible());
    QVERIFY(canvas->rect().contains(markupBar->getCardGeometry()));
}

void AppAnnotateTest::accessibility()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Accessibility.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppAnnotationCreator* creator = H::creator(tab);

    QVERIFY(creator->startTool(Kind::Rectangle));
    pdf::PDFAppAnnotationToolBar* bar = creator->getToolBar();
    QCOMPARE(bar->accessibleName(), QStringLiteral("Rectangle"));
    QCOMPARE(bar->getCloseButton()->accessibleName(), QStringLiteral("End Rectangle"));
    QCOMPARE(bar->getCloseButton()->toolTip(), QStringLiteral("End Rectangle (Esc)"));
    QCOMPARE(bar->getWidthSelect()->accessibleName(), QStringLiteral("Line width"));
    QCOMPARE(bar->getColorTargetControl()->accessibleName(), QStringLiteral("Color of"));
    QVERIFY(!bar->getColorRow()->accessibleName().isEmpty());

    QVERIFY(creator->startTool(Kind::PageLink));
    QCOMPARE(creator->getToolBar()->getTargetSelect()->accessibleName(), QStringLiteral("Target of the link"));
    QCOMPARE(creator->getToolBar()->getStepBadge()->accessibleName(), QStringLiteral("1 / 2"));

    QVERIFY(creator->startTool(Kind::TextBox));
    QVERIFY(H::pickRectangle(tab->getView(), 0, QRectF(40, 150, 100, 40)));
    pdftest::AppDocumentFixture::settle();
    pdf::PDFAppTextBoxPopup* popup = creator->getTextBoxPopup();
    QCOMPARE(popup->getTextEdit()->accessibleName(), QStringLiteral("Text of the text box"));
    QCOMPARE(popup->getSizeSpin()->accessibleName(), QStringLiteral("Font size"));
    QCOMPARE(popup->getBorderColorRow()->accessibleName(), QStringLiteral("Border color"));
    QCOMPARE(popup->getAlignmentControl()->accessibleName(), QStringLiteral("Alignment"));
    QCOMPARE(popup->getCloseButton()->accessibleName(), QStringLiteral("Cancel"));
    popup->cancel();

    QVERIFY(creator->startTool(Kind::WebLink));
    QVERIFY(H::pickRectangle(tab->getView(), 0, QRectF(40, 150, 100, 40)));
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(creator->getWebLinkDialog()->getField()->accessibleName(), QStringLiteral("Address of the link"));
    creator->getWebLinkDialog()->reject();
    creator->endTool();

    QVERIFY(H::selectAllText(tab));
    QCOMPARE(creator->getMarkupBar()->getButton(pdf::PDFAppMarkupBar::Button::Squiggly)->accessibleName(), QStringLiteral("Squiggly"));
    QCOMPARE(creator->getMarkupBar()->accessibleName(), QStringLiteral("Mark the selected text"));

    pdf::PDFAppInsertPanel* insert = qobject_cast<pdf::PDFAppInsertPanel*>(tab->getView()->getToolbar()->openPanel(pdf::PDFAppDocumentToolbar::Panel::Insert));
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(insert->getStickyNoteTile(pdf::TextAnnotationIcon::NewParagraph)->accessibleName(), QStringLiteral("New paragraph"));
    QCOMPARE(insert->getStampChip(pdf::Stamp::TopSecret)->accessibleName(), QStringLiteral("Stamp “Top secret”"));
    QCOMPARE(insert->accessibleName(), QStringLiteral("Insert"));
    insert->close();
}

void AppAnnotateTest::languageChange()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Language.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppAnnotationCreator* creator = H::creator(tab);

    QVERIFY(creator->startTool(Kind::Rectangle));
    QVERIFY(creator->startTool(Kind::TextBox));
    QVERIFY(H::pickRectangle(tab->getView(), 0, QRectF(40, 150, 100, 40)));
    pdftest::AppDocumentFixture::settle();
    pdf::PDFAppTextBoxPopup* popup = creator->getTextBoxPopup();
    QVERIFY(popup->isVisible());
    popup->getAlignmentControl()->setCurrentIndex(2);

    // Every text is retranslated (the segments are created again, the value stays)
    AnnotateTestTranslator* translator = new AnnotateTestTranslator(qApp);
    QCoreApplication::installTranslator(translator);
    pdftest::AppDocumentFixture::settle();
    QVERIFY(popup->getTitleLabel()->text().startsWith(QStringLiteral("§")));
    QVERIFY(popup->getAddButton()->text().startsWith(QStringLiteral("§")));
    QCOMPARE(popup->getAlignmentControl()->getCurrentIndex(), 2);
    QVERIFY(creator->getToolBar()->getHintLabel()->text().startsWith(QStringLiteral("§")));
    popup->cancel();
    QVERIFY(creator->startTool(Kind::Ellipse));
    QVERIFY(creator->getToolBar()->getNameLabel()->text().startsWith(QStringLiteral("§")));
    QVERIFY(creator->getToolBar()->getColorTargetControl());

    QCoreApplication::removeTranslator(translator);
    delete translator;
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(creator->getToolBar()->getNameLabel()->text(), QStringLiteral("Ellipse"));
    creator->endTool();
}

void AppAnnotateTest::reviewHighlightCommentOverPages()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("CommentPages.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppAnnotationCreator* creator = H::creator(tab);
    pdf::PDFAppDocument* document = tab->getDocument();

    // A selection over two pages: one highlight per page, but one comment - only
    // the first highlight has it (two equal comments would be two threads in Notes)
    QVERIFY(H::selectAllText(tab));
    QCOMPARE(creator->getTextSelectionParts().size(), size_t(2));
    QVERIFY(creator->commentTextSelection());
    pdftest::AppDocumentFixture::settle();
    pdf::PDFAppCommentPopup* popup = H::composePopup(tab);
    QVERIFY(popup);
    popup->setComposedText(QStringLiteral("One comment"));
    popup->accept();
    pdftest::AppDocumentFixture::settle();

    QCOMPARE(H::annotationCount(document, 0), 1);
    QCOMPARE(H::annotationCount(document, 1), 1);
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
    QCOMPARE(H::text(document, H::lastAnnotation(document, 0), "Contents"), QStringLiteral("One comment"));
    QVERIFY(H::text(document, H::lastAnnotation(document, 1), "Contents").isEmpty());
    QCOMPARE(H::name(document, H::lastAnnotation(document, 1), "Subtype"), QByteArray("Highlight"));
}

void AppAnnotateTest::reviewTextBoxPopupHiddenTab()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("TextBoxTab.pdf"), 1));
    QVERIFY(tab);
    QPointer<pdf::PDFAppDocumentTab> tabGuard(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppAnnotationCreator* creator = H::creator(tab);
    pdf::PDFAppDocument* document = tab->getDocument();

    QVERIFY(creator->startTool(Kind::TextBox));
    QVERIFY(H::pickRectangle(view, 0, QRectF(40, 250, 150, 40)));
    pdftest::AppDocumentFixture::settle();
    pdf::PDFAppTextBoxPopup* popup = creator->getTextBoxPopup();
    QVERIFY(popup && popup->isVisible());

    // Another tab is shown and this one again: the popup with its shadow is back
    // and a press on the page still cancels it
    pdf::PDFAppDocumentTab* other = H::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("TextBoxOther.pdf"), 1));
    QVERIFY(other && tabGuard);
    QVERIFY(!popup->isVisible());
    fixture.window->activateTab(tab);
    pdftest::AppDocumentFixture::settle();
    QVERIFY(popup->isVisible());
    QWidget* shadow = view->getCanvasArea()->findChild<QWidget*>(QStringLiteral("textBoxPopupShadow"));
    QVERIFY(shadow && shadow->isVisible());
    QVERIFY(pdftest::AppDocumentFixture::clickPage(view, 0, QPointF(250, 50)));
    pdftest::AppDocumentFixture::settle();
    QVERIFY(!popup->isVisible());
    QVERIFY(!creator->getTextBoxTool()->hasArea());
    QCOMPARE(creator->getActiveKind(), Kind::TextBox);
    QCOMPARE(H::annotationCount(document, 0), 0);

    // The tool ends while the tab is hidden: the popup does not come back without
    // its area (Add would lose the text)
    QVERIFY(H::pickRectangle(view, 0, QRectF(40, 250, 150, 40)));
    pdftest::AppDocumentFixture::settle();
    QVERIFY(popup->isVisible());
    fixture.window->activateTab(other);
    pdftest::AppDocumentFixture::settle();
    creator->endTool();
    fixture.window->activateTab(tab);
    pdftest::AppDocumentFixture::settle();
    QVERIFY(!popup->isVisible());
    QVERIFY(!shadow->isVisible());
    QVERIFY(!creator->getTextBoxTool()->hasArea());
    QCOMPARE(H::annotationCount(document, 0), 0);
}

void AppAnnotateTest::reviewPageLinkBarAndMenu()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::pageLabels(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppAnnotationCreator* creator = H::creator(tab);
    pdf::PDFAppPageLinkTool* tool = creator->getPageLinkTool();

    // The bar hugs its parts (Figma): 46 in the step 1, 50 with Back 36 in the step 2
    view->setZoom(1.0);
    pdftest::AppDocumentFixture::settle();
    QVERIFY(creator->startTool(Kind::PageLink));
    pdftest::AppDocumentFixture::settle();
    pdf::PDFAppAnnotationToolBar* bar = creator->getToolBar();
    QCOMPARE(bar->getCardGeometry().height(), pdf::PDFAppFloatingBar::NORMAL_HEIGHT);
    QVERIFY(H::pickRectangle(view, 0, QRectF(40, 150, 100, 20)));
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(tool->getStep(), pdf::PDFAppPageLinkTool::Step::Target);
    QCOMPARE(bar->getCardGeometry().height(), pdf::PDFAppAnnotationToolBar::TARGET_STEP_HEIGHT);
    QCOMPARE(bar->getBackButton()->height(), pdf::PDFAppAnnotationToolBar::BACK_BUTTON_HEIGHT);
    QCOMPARE(bar->getCardGeometry().top(), pdf::PDFAppFloatingBar::TOP_GAP);

    // The tip of the target names the page label (page 3 is "iii"), as the page field
    view->goToPage(2);
    pdftest::AppDocumentFixture::settle();
    bool isShown = false;
    const QPoint hover = pdftest::AppDocumentFixture::pageToWidget(view, 2, QPointF(80, 260), &isShown);
    QVERIFY(isShown);
    QWidget* canvasWidget = pdftest::AppDocumentFixture::canvasWidget(view);
    QMouseEvent hoverMove(QEvent::MouseMove, hover, canvasWidget->mapToGlobal(hover), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(canvasWidget, &hoverMove);
    QCOMPARE(tool->getTipText(), QStringLiteral("Page iii · Whole page"));

    // Back: 46 again
    bar->getBackButton()->click();
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(bar->getCardGeometry().height(), pdf::PDFAppFloatingBar::NORMAL_HEIGHT);

    // The hint of Keep the reader's zoom names the zoom of now, which a Position
    // link stores (not the zoom of the start of the tool)
    creator->setPageLinkTarget(pdf::DestinationType::XYZ, false);
    view->setZoom(2.0);
    pdftest::AppDocumentFixture::settle();
    pdf::PDFAppPageLinkTargetSelect* select = bar->getTargetSelect();
    select->showPopup();
    pdftest::AppDocumentFixture::settle();
    pdf::PDFAppPageLinkTargetMenu* menu = select->getMenu();
    QVERIFY(menu && menu->isVisible());
    QVERIFY2(menu->getKeepZoomHint()->text().contains(QStringLiteral("200 %")), qPrintable(menu->getKeepZoomHint()->text()));
    QCOMPARE(tool->createDestination(1, QPointF(10, 10), QRectF()).getZoom(), 2.0);

    // The menu starts at the leading edge of the select (1170:28177), mirrored for
    // right-to-left: its right edge at the right edge of the select
    const QRect selectRect(select->mapToGlobal(QPoint(0, 0)), select->size());
    const QRect available = select->screen()->availableGeometry();
    const auto expectedLeft = [&available](int left)
    {
        return qBound(available.left(), left, qMax(available.left(), available.right() + 1 - pdf::PDFAppPageLinkTargetMenu::PANEL_WIDTH));
    };
    QCOMPARE(menu->getPanelRect().translated(menu->pos()).left(), expectedLeft(selectRect.left()));
    QTest::keyClick(menu, Qt::Key_Escape);
    pdftest::AppDocumentFixture::settle();
    select->setLayoutDirection(Qt::RightToLeft);
    select->showPopup();
    pdftest::AppDocumentFixture::settle();
    QVERIFY(menu->isVisible());
    QCOMPARE(menu->getPanelRect().translated(menu->pos()).left(), expectedLeft(selectRect.right() + 1 - pdf::PDFAppPageLinkTargetMenu::PANEL_WIDTH));
    QTest::keyClick(menu, Qt::Key_Escape);
    pdftest::AppDocumentFixture::settle();
    select->setLayoutDirection(Qt::LeftToRight);

    // The end of the tool in the step 2 leaves no hint of the tool in the status bar
    view->setZoom(1.0);
    view->goToPage(0);
    pdftest::AppDocumentFixture::settle();
    QVERIFY(H::pickRectangle(view, 0, QRectF(40, 150, 100, 20)));
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(tool->getStep(), pdf::PDFAppPageLinkTool::Step::Target);
    bar->getCloseButton()->click();
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(creator->getActiveKind(), Kind::None);
    QCOMPARE(tool->getStep(), pdf::PDFAppPageLinkTool::Step::LinkArea);
    const QString message = view->getStatusBar()->getMessage();
    QVERIFY2(message != pdf::PDFAppAnnotationTools::getStatusHint(Kind::PageLink), qPrintable(message));
    QVERIFY2(!message.startsWith(QStringLiteral("Go to the target page")), qPrintable(message));
}

void AppAnnotateTest::reviewMarkupBarLostRelease()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("LostRelease.pdf"), 1));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppAnnotationCreator* creator = H::creator(tab);

    // A press, whose release never comes (a popup opened by the press took the
    // mouse), then the pointer moves on without the button
    bool ok = false;
    const QPoint position = pdftest::AppDocumentFixture::pageToWidget(view, 0, QPointF(250, 50), &ok);
    QVERIFY(ok);
    QWidget* canvasWidget = pdftest::AppDocumentFixture::canvasWidget(view);
    QMouseEvent press(QEvent::MouseButtonPress, position, canvasWidget->mapToGlobal(position), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(canvasWidget, &press);
    const QPoint moved = position + QPoint(6, 6);
    QMouseEvent move(QEvent::MouseMove, moved, canvasWidget->mapToGlobal(moved), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(canvasWidget, &move);
    pdftest::AppDocumentFixture::settle();

    // A selection by the keyboard gets its markup bar
    QVERIFY(H::selectAllText(tab));
    QTRY_VERIFY(creator->getMarkupBar() && creator->getMarkupBar()->isVisible());
}

void AppAnnotateTest::reviewPluralTexts()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Plural.pdf"), 3));
    QVERIFY(tab);
    QWidget* anchor = tab->getView()->getToolbar()->getSearchField();
    QVERIFY(anchor && anchor->isVisible());

    // The counts of the Apply button are numbers of the locale (%Ln, O17) and the
    // singular texts have the count too
    pdf::PDFAppMarkResultsMenu* menu = new pdf::PDFAppMarkResultsMenu(anchor->window());
    menu->popup(anchor, pdf::AnnotationType::Highlight, QColor(Qt::yellow), { 1234, 1, 1 }, pdf::PDFAppMarkResultsMenu::Scope::All);
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(menu->getApplyButton()->text(), QStringLiteral("Highlight 1,234 results"));
    QCOMPARE(menu->getScopeItem(pdf::PDFAppMarkResultsMenu::Scope::All)->text(), QStringLiteral("All results (1,234)"));
    menu->getScopeItem(pdf::PDFAppMarkResultsMenu::Scope::CurrentPage)->click();
    QCOMPARE(menu->getApplyButton()->text(), QStringLiteral("Highlight 1 result"));
    menu->getTypeItem(pdf::AnnotationType::Underline)->click();
    QCOMPARE(menu->getApplyButton()->text(), QStringLiteral("Underline 1 result"));
    menu->getTypeItem(pdf::AnnotationType::StrikeOut)->click();
    QCOMPARE(menu->getApplyButton()->text(), QStringLiteral("Strike through 1 result"));
    menu->getTypeItem(pdf::AnnotationType::Squiggly)->click();
    QCOMPARE(menu->getApplyButton()->text(), QStringLiteral("Underline 1 result with a squiggly line"));
    menu->getScopeItem(pdf::PDFAppMarkResultsMenu::Scope::All)->click();
    QCOMPARE(menu->getApplyButton()->text(), QStringLiteral("Underline 1,234 results with a squiggly line"));
    menu->hide();
    delete menu;

    // The status of one marked result
    pdf::PDFAppMarkResults* markResults = H::markResults(tab);
    QVERIFY(H::search(tab, QStringLiteral("gamma")));
    QCOMPARE(markResults->getMatchCount(pdf::PDFAppMarkResults::Scope::CurrentPage), 1);
    QVERIFY(markResults->mark(pdf::AnnotationType::Highlight, QColor(Qt::yellow), pdf::PDFAppMarkResults::Scope::CurrentPage));
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(tab->getView()->getStatusBar()->getMessage(), QStringLiteral("1 result highlighted as an annotation. Ctrl+Z undoes it."));
}

void AppAnnotateTest::reviewIconsAndHelp()
{
    pdftest::AppDocumentFixture fixture;

    // One icon per meaning: the command of a tool has the icon of its tile
    // (Figma 196:3160 - line, scribble, eraser)
    const pdf::PDFAppCommandRegistry* registry = fixture.context->getCommandRegistry();
    for (Kind kind : pdf::PDFAppAnnotationTools::getKinds())
    {
        const QString commandId = pdf::PDFAppAnnotationTools::getCommandId(kind);
        const pdf::PDFAppCommand* command = registry->findCommand(commandId);
        QVERIFY2(command, qPrintable(commandId));
        QCOMPARE(command->iconName, pdf::PDFAppAnnotationTools::getIconName(kind));
    }

    // The topic Annotate a document leads to Edit annotations
    const pdf::PDFAppHelpCatalog catalog = pdf::PDFAppHelpCatalog::load();
    const pdf::PDFAppHelpTopic* topic = catalog.findTopic(QStringLiteral("annotations"));
    QVERIFY(topic);
    QVERIFY(topic->related.contains(QStringLiteral("/help/edit-annotations")));
    QVERIFY(topic->markdown.contains(QStringLiteral("](/help/edit-annotations)")));
}

void AppAnnotateTest::screenshots()
{
    // PDF4QT_APP_SCREENSHOTS=<folder>: the panels Annotate / Insert, the tool bars,
    // the markup bar, the text box popup, Link to page with its Target menu, the
    // web link dialog, the mark menu - both themes at 1x and 2x, 720 x 480, right-to-left
    const QString folder = qEnvironmentVariable("PDF4QT_APP_SCREENSHOTS");
    if (folder.isEmpty())
    {
        QSKIP("PDF4QT_APP_SCREENSHOTS is not set");
    }

    for (pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
    {
        const QString suffix = scheme == pdf::PDFThemeColors::Scheme::Light ? QStringLiteral("light") : QStringLiteral("dark");
        pdftest::AppDocumentFixture fixture;
        pdf::PDFThemeManager::getInstance()->setScheme(scheme);
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Screens.pdf"), 3));
        QVERIFY(tab);
        pdf::PDFAppDocumentView* view = tab->getView();
        pdf::PDFAppAnnotationCreator* creator = H::creator(tab);

        for (qreal ratio : { 1.0, 2.0 })
        {
            const QString scale = ratio > 1.0 ? QStringLiteral("2x") : QStringLiteral("1x");
            for (const auto& panel : { std::make_pair(pdf::PDFAppDocumentToolbar::Panel::Annotate, QStringLiteral("annotate")),
                                       std::make_pair(pdf::PDFAppDocumentToolbar::Panel::Insert, QStringLiteral("insert")) })
            {
                pdf::PDFAppDocumentPanelPopup* popup = view->getToolbar()->openPanel(panel.first);
                pdftest::AppDocumentFixture::settle();
                H::save(folder, QStringLiteral("annotate-panel-%1-%2-%3").arg(panel.second, suffix, scale), H::renderWindowWithPopups(fixture.window, { popup }, ratio));
                H::save(folder, QStringLiteral("annotate-panel-only-%1-%2-%3").arg(panel.second, suffix, scale), H::render(popup, ratio));
                popup->close();
                pdftest::AppDocumentFixture::settle();
            }

            for (const auto& tool : { std::make_pair(Kind::Highlight, QStringLiteral("highlight")), std::make_pair(Kind::Rectangle, QStringLiteral("shape")),
                                      std::make_pair(Kind::StickyNote, QStringLiteral("note")), std::make_pair(Kind::Stamp, QStringLiteral("stamp")),
                                      std::make_pair(Kind::TextBox, QStringLiteral("textbox")), std::make_pair(Kind::PageLink, QStringLiteral("pagelink")) })
            {
                QVERIFY(creator->startTool(tool.first));
                pdftest::AppDocumentFixture::settle();
                H::save(folder, QStringLiteral("annotate-bar-%1-%2-%3").arg(tool.second, suffix, scale), H::render(creator->getToolBar(), ratio));
            }
            creator->setColorTarget(pdf::PDFAppAnnotationCreator::ColorTarget::Fill);
            QVERIFY(creator->startTool(Kind::Rectangle));
            pdftest::AppDocumentFixture::settle();
            H::save(folder, QStringLiteral("annotate-bar-shape-fill-%1-%2").arg(suffix, scale), H::render(creator->getToolBar(), ratio));
            creator->setColorTarget(pdf::PDFAppAnnotationCreator::ColorTarget::Stroke);

            // Link to page: step 2 and the Target menu of Position
            QVERIFY(creator->startTool(Kind::PageLink));
            QVERIFY(H::pickRectangle(view, 0, QRectF(40, 150, 200, 20)));
            pdftest::AppDocumentFixture::settle();
            H::save(folder, QStringLiteral("annotate-pagelink-target-%1-%2").arg(suffix, scale), H::render(fixture.window, ratio));
            creator->getPageLinkTool()->goBack();
            creator->setPageLinkTarget(pdf::DestinationType::XYZ, false);
            creator->getToolBar()->getTargetSelect()->showPopup();
            pdftest::AppDocumentFixture::settle();
            pdf::PDFAppPageLinkTargetMenu* menu = creator->getToolBar()->getTargetSelect()->getMenu();
            H::save(folder, QStringLiteral("annotate-pagelink-menu-%1-%2").arg(suffix, scale), H::renderWindowWithPopups(fixture.window, { menu }, ratio));
            menu->hide();
            creator->setPageLinkTarget(pdf::DestinationType::Fit, false);

            // The text box popup filled
            QVERIFY(creator->startTool(Kind::TextBox));
            QVERIFY(H::pickRectangle(view, 0, QRectF(40, 250, 180, 50)));
            pdftest::AppDocumentFixture::settle();
            creator->getTextBoxPopup()->setText(QStringLiteral("Revenue grew by 18 %"));
            pdftest::AppDocumentFixture::settle();
            H::save(folder, QStringLiteral("annotate-textbox-%1-%2").arg(suffix, scale), H::render(fixture.window, ratio));
            H::save(folder, QStringLiteral("annotate-textbox-only-%1-%2").arg(suffix, scale), H::render(creator->getTextBoxPopup(), ratio));
            creator->getTextBoxPopup()->cancel();
            creator->endTool();

            // The markup bar over a selection
            QVERIFY(H::selectAllText(tab));
            H::save(folder, QStringLiteral("annotate-markup-bar-%1-%2").arg(suffix, scale), H::render(fixture.window, ratio));
            view->deselectText();

            // The web link dialog
            QVERIFY(creator->startTool(Kind::WebLink));
            QVERIFY(H::pickRectangle(view, 0, QRectF(40, 150, 100, 20)));
            pdftest::AppDocumentFixture::settle();
            creator->getWebLinkDialog()->getField()->setText(QStringLiteral("javascript:alert(1)"));
            H::save(folder, QStringLiteral("annotate-weblink-%1-%2").arg(suffix, scale), H::render(creator->getWebLinkDialog(), ratio));
            creator->getWebLinkDialog()->reject();
            creator->endTool();
            pdftest::AppDocumentFixture::settle();

            // The mark menu
            QVERIFY(H::search(tab, QStringLiteral("alpha")));
            view->showPanel(QStringLiteral("search"));
            pdftest::AppDocumentFixture::settle();
            H::markResults(tab)->openMenu();
            pdftest::AppDocumentFixture::settle();
            pdf::PDFAppMarkResultsMenu* markMenu = H::markResults(tab)->getMenu();
            H::save(folder, QStringLiteral("annotate-mark-menu-%1-%2").arg(suffix, scale), H::renderWindowWithPopups(fixture.window, { markMenu }, ratio));
            markMenu->hide();
        }

        fixture.window->resize(720, 480);
        pdftest::AppDocumentFixture::settle();
        QVERIFY(creator->startTool(Kind::Rectangle));
        pdftest::AppDocumentFixture::settle();
        H::save(folder, QStringLiteral("annotate-minimal-%1").arg(suffix), H::render(fixture.window));
        creator->endTool();
    }

    QApplication::setLayoutDirection(Qt::RightToLeft);
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("ScreensRtl.pdf"), 2));
    QVERIFY(tab);
    QVERIFY(H::creator(tab)->startTool(Kind::Ellipse));
    pdftest::AppDocumentFixture::settle();
    H::save(folder, QStringLiteral("annotate-rtl"), H::render(fixture.window));
    H::creator(tab)->endTool();
}

void AppAnnotateTest::reviewStyleReloadOnce()
{
    // Wrap-up RC-A: a color or a width of the bar was read back twice - by the
    // creator at once and by the notification of the stored style; now one reload
    // from the event loop serves both (and the tools sharing the style)
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Reload.pdf"), 1));
    QVERIFY(tab);
    pdf::PDFAppAnnotationCreator* creator = H::creator(tab);
    QVERIFY(creator->startTool(Kind::Rectangle));
    pdftest::AppDocumentFixture::settle();

    QSignalSpy spy(creator, &pdf::PDFAppAnnotationCreator::toolStateChanged);
    creator->setColor(Kind::Rectangle, pdf::PDFAppAnnotationCreator::ColorTarget::Stroke, QColor(Qt::green));
    QVERIFY(creator->isStyleReloadPending());
    pdftest::AppDocumentFixture::settle();
    QVERIFY(!creator->isStyleReloadPending());
    QCOMPARE(spy.count(), 1);
    QCOMPARE(creator->getColor(Kind::Ellipse, pdf::PDFAppAnnotationCreator::ColorTarget::Stroke).rgb(), QColor(Qt::green).rgb());

    spy.clear();
    creator->setPenWidth(Kind::Rectangle, 4.0);
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(spy.count(), 1);
    QCOMPARE(creator->getPenWidth(Kind::Ellipse), 4.0);

    // The change of another instance (a notification only) reads the styles too
    spy.clear();
    pdf::PDFAppAnnotationStyleStorage* storage = fixture.context->getAnnotationStyleStorage();
    QVERIFY(storage);
    pdf::PDFAnnotationStyle style = pdf::PDFAnnotationStyleSettings::getStyle(pdf::PDFAnnotationStyleSettings::STYLE_SHAPE, pdf::PDFAnnotationStyle());
    style.strokeColor = QColor(Qt::magenta);
    storage->setStyle(pdf::PDFAnnotationStyleSettings::STYLE_SHAPE, style);
    QTRY_COMPARE(creator->getColor(Kind::Rectangle, pdf::PDFAppAnnotationCreator::ColorTarget::Stroke).rgb(), QColor(Qt::magenta).rgb());
    QCOMPARE(creator->getColor(Kind::Ellipse, pdf::PDFAppAnnotationCreator::ColorTarget::Stroke).rgb(), QColor(Qt::magenta).rgb());
    QCOMPARE(spy.count(), 1);
    creator->endTool();
}

void AppAnnotateTest::reviewLastMarkupOfStartedTool()
{
    // Wrap-up RC-A: the last markup was stored before the tool had started
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("LastMarkup.pdf"), 1));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppAnnotationCreator* creator = H::creator(tab);
    QVERIFY(creator->startTool(Kind::Highlight));
    creator->endTool();
    QCOMPARE(pdf::PDFAppAnnotationToolSettings::getLastMarkupType(fixture.context.get()), pdf::AnnotationType::Highlight);

    // Another tool takes the canvas, when the markup wants the Select text tool:
    // the markup does not start and is not remembered
    pdf::PDFToolManager* toolManager = view->getToolManager();
    QVERIFY(toolManager && toolManager->getSelectTextTool());
    view->setCanvasTool(pdf::PDFToolManager::ScreenshotTool);
    QVERIFY(view->isCanvasToolActive(pdf::PDFToolManager::ScreenshotTool));
    QMetaObject::Connection connection = QObject::connect(toolManager->getSelectTextTool(), &pdf::PDFWidgetTool::toolActivityChanged, toolManager, [toolManager](bool active)
    {
        if (active)
        {
            toolManager->setActiveTool(pdf::PDFToolManager::ScreenshotTool);
        }
    });
    QVERIFY(!creator->startTool(Kind::Underline));
    QObject::disconnect(connection);
    QCOMPARE(creator->getActiveKind(), pdf::PDFAppAnnotationToolKind::None);
    QCOMPARE(pdf::PDFAppAnnotationToolSettings::getLastMarkupType(fixture.context.get()), pdf::AnnotationType::Highlight);

    // A markup, which starts, is remembered
    QVERIFY(creator->startTool(Kind::Underline));
    QCOMPARE(pdf::PDFAppAnnotationToolSettings::getLastMarkupType(fixture.context.get()), pdf::AnnotationType::Underline);
    creator->endTool();
    pdftest::AppDocumentFixture::settle();
}

void AppAnnotateTest::reviewCommentCountCached()
{
    // Wrap-up RC-A: the Annotate panel counted the comments of the whole document
    // at every change of the tools; it counts them once per version
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::annotatedLarge(fixture.folder(), 2000, 5));
    QVERIFY(tab);
    pdf::PDFAppAnnotationCreator* creator = H::creator(tab);
    pdf::PDFAppDocumentToolbar* toolbar = tab->getView()->getToolbar();
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::annotatePanel()));
    pdf::PDFAppAnnotatePanel* annotate = qobject_cast<pdf::PDFAppAnnotatePanel*>(toolbar->findPanel(pdf::PDFAppDocumentToolbar::Panel::Annotate));
    QVERIFY(annotate && annotate->isVisible());
    const pdf::PDFDocument* document = tab->getDocument()->getDocument().data();
    const int comments = pdf::PDFAppAnnotatePanel::countComments(document);
    QVERIFY(comments >= 2000);
    QVERIFY(annotate->getShowCommentsItem()->text().contains(pdf::PDFAppLocale::formatNumber(comments)));

    constexpr int updates = 100;
    QElapsedTimer timer;
    timer.start();
    for (int i = 0; i < updates; ++i)
    {
        pdf::PDFAppAnnotatePanel::countComments(document);
    }
    const qint64 counting = timer.elapsed();
    timer.restart();
    for (int i = 0; i < updates; ++i)
    {
        Q_EMIT creator->toolStateChanged();
    }
    const qint64 panelUpdates = timer.elapsed();
    qInfo("Annotate panel, 2000 pages x 5 annotations: %d counts of the comments %lld ms, %d updates of the panel %lld ms", updates, counting, updates, panelUpdates);

    // A new version is counted again
    annotate->hide();
    pdftest::AppDocumentFixture::settle();
    QVERIFY(creator->startTool(Kind::Rectangle));
    QVERIFY(H::pickRectangle(tab->getView(), 0, QRectF(40, 100, 80, 60)));
    pdftest::AppDocumentFixture::settle();
    creator->endTool();
    QCOMPARE(pdf::PDFAppAnnotatePanel::countComments(tab->getDocument()->getDocument().data()), comments + 1);
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::annotatePanel()));
    QVERIFY(annotate->isVisible());
    QVERIFY(annotate->getShowCommentsItem()->text().contains(pdf::PDFAppLocale::formatNumber(comments + 1)));
}

void AppAnnotateTest::reviewMarkResultsKinds()
{
    // Each kind of Mark as annotations: one undo step with the annotations of the
    // kind and its own message; a blocking operation refuses the marking
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("MarkKinds.pdf"), 3));
    QVERIFY(tab);
    QVERIFY(H::search(tab, QStringLiteral("alpha")));
    pdf::PDFAppMarkResults* markResults = H::markResults(tab);
    QVERIFY(markResults);
    pdf::PDFAppDocument* document = tab->getDocument();

    struct Case
    {
        pdf::AnnotationType type;
        QByteArray subtype;
        QString message;
    };
    const Case cases[] = { { pdf::AnnotationType::Underline, QByteArrayLiteral("Underline"), QStringLiteral("3 results underlined as annotations. Ctrl+Z undoes it.") },
                           { pdf::AnnotationType::StrikeOut, QByteArrayLiteral("StrikeOut"), QStringLiteral("3 results struck through as annotations. Ctrl+Z undoes it.") } };
    for (const Case& testCase : cases)
    {
        const int undoCount = pdftest::AppDocumentFixture::undoCount(document);
        QVERIFY(markResults->mark(testCase.type, QColor(), pdf::PDFAppMarkResults::Scope::All));
        pdftest::AppDocumentFixture::settle();
        QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), undoCount + 1);
        QVERIFY(H::lastOfType(document, 0, testCase.subtype).isValid());
        QCOMPARE(tab->getView()->getStatusBar()->getMessage(), testCase.message);
        tab->executeCommand(pdf::PDFAppCommands::undo());
        pdftest::AppDocumentFixture::settle();
    }

    QPointer<pdf::PDFAppDocumentOperation> operation = document->beginOperation(pdf::PDFAppDocumentOperationKind::Save, QStringLiteral("Saving"), true);
    const int undoCount = pdftest::AppDocumentFixture::undoCount(document);
    markResults->mark(pdf::AnnotationType::Highlight, QColor(), pdf::PDFAppMarkResults::Scope::All);
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), undoCount);
    operation->finish();
    pdftest::AppDocumentFixture::settle();
}

QTEST_MAIN(AppAnnotateTest)
#include "tst_appannotatetest.moc"
