#include "pdfadvancedtools.h"
#include "pdfcms.h"
#include "pdfcompiler.h"
#include "pdfdocumentbuilder.h"
#include "pdfdrawwidget.h"
#include "pdfprogress.h"
#include "pdfwidgetannotation.h"

#include <QtTest>
#include <QActionGroup>
#include <QSettings>
#include <QTemporaryDir>
#include <array>

class AnnotationToolsTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void deletionRespectsFlags_data();
    void deletionRespectsFlags();
    void activationDoesNotIndexDocument();

private:
    QTemporaryDir m_settingsDirectory;
};

void AnnotationToolsTest::initTestCase()
{
    QVERIFY(m_settingsDirectory.isValid());
    QCoreApplication::setOrganizationName("PDF4QT-Tests");
    QCoreApplication::setApplicationName("AnnotationTools");
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDirectory.path());
}

struct AnnotationFixture
{
    pdf::PDFDocument document;
    pdf::PDFCMSManager cms{nullptr};
    pdf::PDFProgress progress{nullptr};
    pdf::PDFWidget widget{&cms, pdf::RendererEngine::QPainter, nullptr};
    pdf::PDFWidgetAnnotationManager annotations{widget.getDrawWidgetProxy(), nullptr};
    std::array<QAction, 10> actions;
    pdf::PDFToolManager tools{widget.getDrawWidgetProxy(),
        { &actions[0], &actions[1], &actions[2], &actions[3], &actions[4], &actions[5],
          &actions[6], &actions[7], &actions[8], &actions[9] }, nullptr, &widget};

    explicit AnnotationFixture(pdf::PDFDocument doc) : document(std::move(doc))
    {
        widget.resize(800, 800);
        widget.getDrawWidget()->getWidget()->resize(800, 800);
        widget.setAnnotationManager(&annotations);
        widget.getDrawWidgetProxy()->setProgress(&progress);
        pdf::PDFModifiedDocument modified(&document, nullptr);
        widget.setDocument(modified, {});
        annotations.setDocument(modified);
        tools.setDocument(modified);
    }

    ~AnnotationFixture()
    {
        tools.setDocument(pdf::PDFModifiedDocument());
        annotations.setDocument(pdf::PDFModifiedDocument());
        widget.setDocument(pdf::PDFModifiedDocument(), {});
        widget.setAnnotationManager(nullptr);
    }
};

void AnnotationToolsTest::deletionRespectsFlags_data()
{
    QTest::addColumn<int>("flags");
    QTest::addColumn<bool>("deletable");
    QTest::newRow("editable") << 0 << true;
    QTest::newRow("locked") << int(pdf::PDFAnnotation::Locked) << false;
    QTest::newRow("read-only") << int(pdf::PDFAnnotation::ReadOnly) << false;
    QTest::newRow("hidden") << int(pdf::PDFAnnotation::Hidden) << false;
    QTest::newRow("no-view") << int(pdf::PDFAnnotation::NoView) << false;
}

void AnnotationToolsTest::deletionRespectsFlags()
{
    QFETCH(int, flags);
    QFETCH(bool, deletable);
    pdf::PDFDocumentBuilder builder;
    const auto page = builder.appendPage(QRectF(0, 0, 200, 200));
    const auto annotation = builder.createAnnotationHighlight(page, QRectF(50, 50, 40, 20), Qt::yellow);
    pdf::PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("F");
    factory << pdf::PDFInteger(flags);
    factory.endDictionaryItem();
    factory.endDictionary();
    builder.mergeTo(annotation, factory.takeObject());
    AnnotationFixture fixture(builder.build());

    auto* proxy = fixture.widget.getDrawWidgetProxy();
    const auto snapshot = proxy->getSnapshot();
    QVERIFY(!snapshot.items.empty());
    const QPoint position = snapshot.items.front().pageToDeviceMatrix.map(QPointF(70, 60)).toPoint();
    QCOMPARE(proxy->getPageUnderPoint(position, nullptr), pdf::PDFInteger(0));
    QSignalSpy toolChanges(&fixture.tools, &pdf::PDFToolManager::documentModified);
    QAction action;
    pdf::PDFDeleteAnnotationTool tool(proxy, &fixture.tools, &action, nullptr);
    tool.setDocument(pdf::PDFModifiedDocument(&fixture.document, nullptr));
    tool.setActive(true);
    QMouseEvent press(QEvent::MouseButtonPress, position, position, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, position, position, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    tool.mousePressEvent(&fixture.widget, &press);
    tool.mouseReleaseEvent(&fixture.widget, &release);
    tool.setActive(false);
    QCOMPARE(toolChanges.count(), deletable ? 1 : 0);

    // The Delete shortcut must enforce the same restrictions as the eraser.
    auto& pageAnnotations = fixture.annotations.getPageAnnotations(0);
    QVERIFY(!pageAnnotations.annotations.empty());
    pageAnnotations.annotations.front().isHovered = true;
    QSignalSpy keyChanges(&fixture.annotations, &pdf::PDFWidgetAnnotationManager::documentModified);
    QKeyEvent key(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
    fixture.annotations.keyPressEvent(&fixture.widget, &key);
    QCOMPARE(keyChanges.count(), deletable ? 1 : 0);
}

void AnnotationToolsTest::activationDoesNotIndexDocument()
{
    pdf::PDFDocumentBuilder builder;
    for (int i = 0; i < 20; ++i)
    {
        builder.appendPage(QRectF(0, 0, 200, 200));
    }
    AnnotationFixture fixture(builder.build());
    auto* proxy = fixture.widget.getDrawWidgetProxy();
    QSignalSpy indexing(&fixture.progress, &pdf::PDFProgress::progressStarted);
    QActionGroup group(nullptr);
    pdf::PDFCreateHighlightTextTool highlight(proxy, &fixture.tools, &group, nullptr);
    highlight.setDocument(pdf::PDFModifiedDocument(&fixture.document, nullptr));
    highlight.setActive(true);
    highlight.setActive(false);
    QCOMPARE(indexing.count(), 0);

    QAction action;
    pdf::PDFCreateRedactTextTool redact(proxy, &fixture.tools, &action, nullptr);
    redact.setDocument(pdf::PDFModifiedDocument(&fixture.document, nullptr));
    redact.setActive(true);
    redact.setActive(false);
    QCOMPARE(indexing.count(), 0);
}

QTEST_MAIN(AnnotationToolsTest)

#include "tst_annotationtoolstest.moc"
