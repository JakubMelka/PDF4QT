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

#include "pdfadvancedtools.h"
#include "pdfannotationmanipulator.h"
#include "pdfannotationproperties.h"
#include "pdfannotationstyle.h"
#include "pdfcms.h"
#include "pdfcompiler.h"
#include "pdfdocumentbuilder.h"
#include "pdfdrawspacecontroller.h"
#include "pdfdrawwidget.h"
#include "pdfprogress.h"
#include "pdfsnapper.h"
#include "pdfwidgetannotation.h"
#include "pdfwidgettool.h"

#include <QtTest>
#include <QActionGroup>
#include <QClipboard>
#include <QDialog>
#include <QElapsedTimer>
#include <QMimeData>
#include <QPainter>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>

#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <map>

using namespace pdf;

/// Tests of the annotation tools and of the opt-in switches of the annotation
/// manager for the new application: tools without actions, the setters of the
/// variants, the storage of the styles, the delegated user interface of the tools
/// and of the manager, the read only mode, the new operations, the colors of the
/// view, the lock badge and the annotations selectable by the text selection tool.
class AnnotationToolsTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void deletionRespectsFlags_data();
    void deletionRespectsFlags();
    void activationDoesNotIndexDocument();
    void toolsWithoutActions();
    void variantSetters();
    void styleStorageInterface();
    void styleWindowDisabled();
    void delegatedCreation();
    void delegatedManagerUi();
    void menuRequestContext();
    void readOnlyManager();
    void fillAndHidden();
    void deleteAnnotationsOneStep();
    void viewColorsSelection();
    void lockBadge();
    void designSelectionLook();
    void selectTextPassThrough();
    void actionGroupsOfOldApplications();
    void persistentCreationTools();
    void toolDestroyedWhileActive();
    void reviewReadOnlyMultiSelection();
    void reviewHiddenLeavesSelection();
    void reviewAnnotationsOutsideOfView();
    void noteIconIsMemo();
    void reviewLinksSelectable();
    void reviewLinksMovable();
    void reviewOwnerDeletedFromEmissions();
    void reviewHighlightedAnnotation();
    void reviewPickTargetToolDeleted();
    void reviewSpecificationShapeTypes();
    void reviewPickToolViewColors();
    void reviewInteractionHitTestCost();
    void reviewInkStrokeSmooth();

private:
    QTemporaryDir m_settingsDirectory;
};

void AnnotationToolsTest::initTestCase()
{
    QVERIFY(m_settingsDirectory.isValid());
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName("PDF4QT-Tests");
    QCoreApplication::setApplicationName("AnnotationTools");
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDirectory.path());
    qRegisterMetaType<PDFAnnotationCreationRequest>();
    qRegisterMetaType<PDFAnnotationMenuRequest>();
}

/// Fixture of the old tests - a document without the modifications applied
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

/// Widget with the annotation manager and the tool manager of the new application
/// (without actions). Modified documents of both managers are applied as the
/// application does it. Input goes through the draw widget (tools, annotations).
struct ToolsFixture
{
    PDFDocumentPointer document;
    PDFCMSManager cms{ nullptr };
    PDFProgress progress{ nullptr };
    PDFWidget widget{ &cms, RendererEngine::QPainter, nullptr };
    PDFWidgetAnnotationManager annotations{ widget.getDrawWidgetProxy(), nullptr };
    PDFToolManager tools{ widget.getDrawWidgetProxy(), nullptr };
    int modificationCount = 0;

    explicit ToolsFixture(PDFDocument initialDocument) :
        document(new PDFDocument(std::move(initialDocument)))
    {
        widget.resize(800, 800);
        widget.getDrawWidget()->getWidget()->resize(800, 800);
        widget.setAnnotationManager(&annotations);
        widget.setToolManager(&tools);
        widget.getDrawWidgetProxy()->setProgress(&progress);
        apply(PDFModifiedDocument(document, nullptr));

        QObject::connect(&annotations, &PDFWidgetAnnotationManager::documentModified, &annotations, [this](PDFModifiedDocument modified) { onModified(modified); });
        QObject::connect(&tools, &PDFToolManager::documentModified, &tools, [this](PDFModifiedDocument modified) { onModified(modified); });
    }

    ~ToolsFixture()
    {
        tools.setActiveTool(nullptr);
        tools.setDocument(PDFModifiedDocument());
        annotations.setDocument(PDFModifiedDocument());
        widget.setDocument(PDFModifiedDocument(), {});
        widget.setAnnotationManager(nullptr);
        widget.setToolManager(nullptr);
    }

    void onModified(PDFModifiedDocument modified)
    {
        ++modificationCount;
        PDFDocumentPointer oldDocument = document;
        document = modified;
        apply(PDFModifiedDocument(document, nullptr, modified.getFlags()));
    }

    void apply(PDFModifiedDocument modified)
    {
        widget.setDocument(modified, {});
        annotations.setDocument(modified);
        tools.setDocument(modified);
    }

    template<typename Tool>
    Tool* addTool(Tool* tool)
    {
        tools.addTool(tool);
        tool->setDocument(PDFModifiedDocument(document, nullptr));
        return tool;
    }

    PDFDrawWidgetProxy* proxy() const { return widget.getDrawWidgetProxy(); }
    QWidget* target() const { return widget.getDrawWidget()->getWidget(); }

    QTransform pageToDevice() const
    {
        const PDFWidgetSnapshot snapshot = proxy()->getSnapshot();
        return snapshot.items.empty() ? QTransform() : snapshot.items.front().pageToDeviceMatrix;
    }

    QPoint device(const QPointF& pagePoint) const
    {
        return pageToDevice().map(pagePoint).toPoint();
    }

    void mouse(QEvent::Type type, QPoint position, Qt::MouseButton button, Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QMouseEvent event(type, position, target()->mapToGlobal(position), button, buttons, modifiers);
        QCoreApplication::sendEvent(target(), &event);
    }

    void moveTo(QPoint position, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        mouse(QEvent::MouseMove, position, Qt::NoButton, Qt::NoButton, modifiers);
    }

    void click(QPoint position, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        moveTo(position, modifiers);
        mouse(QEvent::MouseButtonPress, position, Qt::LeftButton, Qt::LeftButton, modifiers);
        mouse(QEvent::MouseButtonRelease, position, Qt::LeftButton, Qt::NoButton, modifiers);
    }

    void rightClick(QPoint position)
    {
        moveTo(position);
        mouse(QEvent::MouseButtonPress, position, Qt::RightButton, Qt::RightButton);
        mouse(QEvent::MouseButtonRelease, position, Qt::RightButton, Qt::NoButton);
    }

    void doubleClick(QPoint position)
    {
        click(position);
        mouse(QEvent::MouseButtonDblClick, position, Qt::LeftButton, Qt::LeftButton);
        mouse(QEvent::MouseButtonRelease, position, Qt::LeftButton, Qt::NoButton);
    }

    /// Key press through the draw widget (tools, forms, annotations)
    void key(int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QKeyEvent event(QEvent::KeyPress, key, modifiers);
        QCoreApplication::sendEvent(target(), &event);
    }

    /// Key press directly to the annotation manager
    bool annotationKey(int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QKeyEvent event(QEvent::KeyPress, key, modifiers);
        event.ignore();
        annotations.keyPressEvent(&widget, &event);
        return event.isAccepted();
    }

    PDFObject entry(PDFObjectReference annotation, const char* key) const
    {
        const PDFDictionary* dictionary = document->getDictionaryFromObject(document->getObjectByReference(annotation));
        return dictionary ? document->getObject(dictionary->get(key)) : PDFObject();
    }

    PDFAnnotation::Flags flags(PDFObjectReference annotation) const
    {
        const PDFAnnotationPtr parsedAnnotation = PDFAnnotation::parse(&document->getStorage(), annotation);
        return parsedAnnotation ? parsedAnnotation->getFlags() : PDFAnnotation::Flags();
    }

    std::vector<PDFObjectReference> pageAnnotations() const
    {
        return document->getCatalog()->getPage(0)->getAnnotations();
    }

    /// Draws the markers of the annotation manager (selection, handles, rubber band)
    QImage drawMarkers() const
    {
        QImage image(800, 800, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        annotations.drawPostRendering(&painter, image.rect());
        painter.end();
        return image;
    }

    /// Returns the number of pixels of the color (opaque enough, tolerance for the antialiasing)
    static int countColor(const QImage& image, const QColor& color, int tolerance = 12)
    {
        int count = 0;
        for (int y = 0; y < image.height(); ++y)
        {
            for (int x = 0; x < image.width(); ++x)
            {
                const QColor pixel = image.pixelColor(x, y);
                if (pixel.alpha() >= 100 &&
                    std::abs(pixel.red() - color.red()) <= tolerance &&
                    std::abs(pixel.green() - color.green()) <= tolerance &&
                    std::abs(pixel.blue() - color.blue()) <= tolerance)
                {
                    ++count;
                }
            }
        }
        return count;
    }

    static int countDrawn(const QImage& image)
    {
        int count = 0;
        for (int y = 0; y < image.height(); ++y)
        {
            for (int x = 0; x < image.width(); ++x)
            {
                count += qAlpha(image.pixel(x, y)) > 0 ? 1 : 0;
            }
        }
        return count;
    }
};

class AnnotationToolsTestHelper
{
public:
    AnnotationToolsTestHelper() = delete;

    static void setFlags(PDFDocumentBuilder& builder, PDFObjectReference annotation, PDFAnnotation::Flags flags)
    {
        PDFObjectFactory factory;
        factory.beginDictionary();
        factory.beginDictionaryItem("F");
        factory << PDFInteger(flags.toInt());
        factory.endDictionaryItem();
        factory.endDictionary();
        builder.mergeTo(annotation, factory.takeObject());
    }

    static bool fuzzyCompare(const QPointF& left, const QPointF& right, qreal tolerance = 1.5)
    {
        return std::abs(left.x() - right.x()) <= tolerance && std::abs(left.y() - right.y()) <= tolerance;
    }

    static bool fuzzyCompare(const QRectF& left, const QRectF& right, qreal tolerance = 1.5)
    {
        const bool result = fuzzyCompare(left.topLeft(), right.topLeft(), tolerance) && fuzzyCompare(left.bottomRight(), right.bottomRight(), tolerance);
        if (!result)
        {
            qWarning() << "Rectangles differ:" << left << right;
        }
        return result;
    }

    static int countVisibleStyleWindows(QWidget* parent)
    {
        int count = 0;
        for (const PDFAnnotationStyleWidget* window : parent->findChildren<PDFAnnotationStyleWidget*>())
        {
            count += window->isVisible() ? 1 : 0;
        }
        return count;
    }
};

/// Page 300 x 300: square A (50, 50, 60, 40), square B (150, 150, 40, 40)
struct TwoSquares
{
    PDFObjectReference page;
    PDFObjectReference first;
    PDFObjectReference second;
    PDFDocument document;

    TwoSquares()
    {
        PDFDocumentBuilder builder;
        page = builder.appendPage(QRectF(0, 0, 300, 300));
        first = builder.createAnnotationSquare(page, QRectF(50, 50, 60, 40), 1.0, Qt::yellow, Qt::black, "First", "Subject", "First contents");
        second = builder.createAnnotationSquare(page, QRectF(150, 150, 40, 40), 1.0, Qt::green, Qt::black, "Second", "Subject", "Second contents");
        document = builder.build();
    }
};

/// Fake storage of the styles (the database of the application)
class FakeStyleStorage : public PDFAnnotationStyleStorage
{
public:
    virtual PDFAnnotationStyle getStyle(const QString& styleId, const PDFAnnotationStyle& defaultStyle) const override
    {
        ++readCount;
        auto it = styles.find(styleId);
        return it != styles.cend() ? it->second : defaultStyle;
    }

    virtual void setStyle(const QString& styleId, const PDFAnnotationStyle& style) override
    {
        ++writeCount;
        styles[styleId] = style;
    }

    std::map<QString, PDFAnnotationStyle> styles;
    mutable int readCount = 0;
    int writeCount = 0;
};

/// Installs the storage of the styles for the lifetime of the guard
struct StyleStorageGuard
{
    explicit StyleStorageGuard(PDFAnnotationStyleStorage* storage) { PDFAnnotationStyleSettings::setStorage(storage); }
    ~StyleStorageGuard() { PDFAnnotationStyleSettings::setStorage(nullptr); }
};

/// Turns the style window off for the lifetime of the guard
struct StyleWindowGuard
{
    explicit StyleWindowGuard(bool enabled) { PDFAnnotationStyleManager::setStyleWindowEnabled(enabled); }
    ~StyleWindowGuard() { PDFAnnotationStyleManager::setStyleWindowEnabled(true); }
};

/// Rejects every modal dialog and closes every popup, which appears (they run their
/// own event loops), and counts them
struct DialogWatcher
{
    QTimer timer;
    int dialogs = 0;
    int popups = 0;

    DialogWatcher()
    {
        QObject::connect(&timer, &QTimer::timeout, &timer, [this]()
        {
            if (QDialog* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()))
            {
                ++dialogs;
                dialog->reject();
            }
            if (QWidget* popup = QApplication::activePopupWidget())
            {
                ++popups;
                popup->close();
            }
        });
        timer.start(5);
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

void AnnotationToolsTest::toolsWithoutActions()
{
    // Every tool accepts a missing action / action group - no connection to a null
    // object (Qt would warn), the application activates them by the tool manager
    QTest::failOnWarning(QRegularExpression("QObject::connect"));
    StyleWindowGuard windowGuard(false);

    TwoSquares squares;
    ToolsFixture fixture(squares.document);
    PDFDrawWidgetProxy* proxy = fixture.proxy();
    PDFToolManager* tools = &fixture.tools;

    std::vector<PDFWidgetTool*> createdTools;
    createdTools.push_back(fixture.addTool(new PDFCreateStickyNoteTool(proxy, tools, nullptr, tools)));
    createdTools.push_back(fixture.addTool(new PDFCreateHyperlinkTool(proxy, tools, nullptr, tools)));
    createdTools.push_back(fixture.addTool(new PDFCreateInDocumentHyperlinkTool(proxy, tools, nullptr, tools)));
    createdTools.push_back(fixture.addTool(new PDFCreateFreeTextTool(proxy, tools, nullptr, tools)));
    createdTools.push_back(fixture.addTool(new PDFCreateLineTypeTool(proxy, tools, PDFCreateLineTypeTool::Type::Line, nullptr, tools)));
    createdTools.push_back(fixture.addTool(new PDFCreateLineTypeTool(proxy, tools, PDFCreateLineTypeTool::Type::PolyLine, nullptr, tools)));
    createdTools.push_back(fixture.addTool(new PDFCreateLineTypeTool(proxy, tools, PDFCreateLineTypeTool::Type::Polygon, nullptr, tools)));
    createdTools.push_back(fixture.addTool(new PDFCreateLineTypeTool(proxy, tools, PDFCreateLineTypeTool::Type::Rectangle, nullptr, tools)));
    createdTools.push_back(fixture.addTool(new PDFCreateEllipseTool(proxy, tools, nullptr, tools)));
    createdTools.push_back(fixture.addTool(new PDFCreateFreehandCurveTool(proxy, tools, nullptr, tools)));
    createdTools.push_back(fixture.addTool(new PDFCreateStampTool(proxy, tools, nullptr, tools)));
    createdTools.push_back(fixture.addTool(new PDFCreateHighlightTextTool(proxy, tools, nullptr, tools)));
    createdTools.push_back(fixture.addTool(new PDFDeleteAnnotationTool(proxy, tools, nullptr, tools)));
    createdTools.push_back(fixture.addTool(new PDFCreateRedactRectangleTool(proxy, tools, nullptr, tools)));
    createdTools.push_back(fixture.addTool(new PDFCreateRedactTextTool(proxy, tools, nullptr, tools)));

    QSignalSpy activeToolChanges(tools, &PDFToolManager::activeToolChanged);
    for (PDFWidgetTool* tool : createdTools)
    {
        QVERIFY(!tool->getAction());
        tools->setActiveTool(tool);
        QCOMPARE(tools->getActiveTool(), tool);
        QVERIFY(tool->isActive());

        // A switch to the next tool and Escape turn the tool off
        fixture.key(Qt::Key_Escape);
        QVERIFY(!tool->isActive());
        QCOMPARE(tools->getActiveTool(), nullptr);
    }
    QCOMPARE(activeToolChanges.count(), int(createdTools.size()) * 2);
    QCOMPARE(fixture.modificationCount, 0);
}

void AnnotationToolsTest::variantSetters()
{
    FakeStyleStorage storage;
    StyleStorageGuard storageGuard(&storage);
    StyleWindowGuard windowGuard(false);

    TwoSquares squares;
    ToolsFixture fixture(squares.document);
    PDFDrawWidgetProxy* proxy = fixture.proxy();
    PDFToolManager* tools = &fixture.tools;

    // Sticky note
    PDFCreateStickyNoteTool* sticky = fixture.addTool(new PDFCreateStickyNoteTool(proxy, tools, nullptr, tools));
    QCOMPARE(sticky->getIcon(), TextAnnotationIcon::Comment);
    sticky->setIcon(TextAnnotationIcon::Key);
    QCOMPARE(sticky->getIcon(), TextAnnotationIcon::Key);
    sticky->setIcon(static_cast<TextAnnotationIcon>(99));
    QCOMPARE(sticky->getIcon(), TextAnnotationIcon::Key);
    sticky->setIcon(static_cast<TextAnnotationIcon>(-1));
    QCOMPARE(sticky->getIcon(), TextAnnotationIcon::Key);
    QVERIFY(!sticky->getStyleManager());

    // Stamp
    PDFCreateStampTool* stamp = fixture.addTool(new PDFCreateStampTool(proxy, tools, nullptr, tools));
    stamp->setStamp(Stamp::Draft);
    QCOMPARE(stamp->getStamp(), Stamp::Draft);
    stamp->setStamp(Stamp::TopSecret);
    QCOMPARE(stamp->getStamp(), Stamp::TopSecret);
    stamp->setStamp(static_cast<Stamp>(100));
    QCOMPARE(stamp->getStamp(), Stamp::TopSecret);
    stamp->setStamp(static_cast<Stamp>(-3));
    QCOMPARE(stamp->getStamp(), Stamp::TopSecret);
    QVERIFY(!stamp->getStyleManager());

    // Text markup - each type has its own style
    PDFCreateHighlightTextTool* highlight = fixture.addTool(new PDFCreateHighlightTextTool(proxy, tools, nullptr, tools));
    QCOMPARE(highlight->getAnnotationType(), AnnotationType::Highlight);
    QVERIFY(highlight->getStyleManager());
    QCOMPARE(highlight->getStyleManager()->getStyle().strokeColor, QColor(Qt::yellow));
    highlight->setAnnotationType(AnnotationType::Underline);
    QCOMPARE(highlight->getAnnotationType(), AnnotationType::Underline);
    QCOMPARE(highlight->getStyleManager()->getStyle().strokeColor, QColor(Qt::black));

    PDFAnnotationStyle underline;
    underline.strokeColor = Qt::blue;
    storage.styles[PDFAnnotationStyleSettings::STYLE_UNDERLINE] = underline;
    highlight->setAnnotationType(AnnotationType::StrikeOut);
    QCOMPARE(highlight->getStyleManager()->getStyle().strokeColor, QColor(Qt::red));
    highlight->setAnnotationType(AnnotationType::Underline);
    QCOMPARE(highlight->getStyleManager()->getStyle().strokeColor, QColor(Qt::blue));
    highlight->setAnnotationType(AnnotationType::Square);
    QCOMPARE(highlight->getAnnotationType(), AnnotationType::Underline);
    highlight->setAnnotationType(static_cast<AnnotationType>(1000));
    QCOMPARE(highlight->getAnnotationType(), AnnotationType::Underline);
    QVERIFY(PDFCreateHighlightTextTool::isTextMarkupType(AnnotationType::Squiggly));
    QVERIFY(!PDFCreateHighlightTextTool::isTextMarkupType(AnnotationType::Ink));

    // Link to a page
    PDFCreateInDocumentHyperlinkTool* pageLink = fixture.addTool(new PDFCreateInDocumentHyperlinkTool(proxy, tools, nullptr, tools));
    QCOMPARE(pageLink->getDestinationType(), DestinationType::Fit);
    pageLink->setDestinationType(DestinationType::FitH);
    QCOMPARE(pageLink->getDestinationType(), DestinationType::FitH);
    pageLink->setDestinationType(DestinationType::Invalid);
    QCOMPARE(pageLink->getDestinationType(), DestinationType::FitH);
    pageLink->setDestinationType(DestinationType::Named);
    QCOMPARE(pageLink->getDestinationType(), DestinationType::FitH);
    pageLink->setDestinationType(static_cast<DestinationType>(77));
    QCOMPARE(pageLink->getDestinationType(), DestinationType::FitH);
    pageLink->setDestinationType(DestinationType::XYZ);
    QCOMPARE(pageLink->getDestinationType(), DestinationType::XYZ);
    QVERIFY(!pageLink->isInheritZoom());
    pageLink->setInheritZoom(true);
    QVERIFY(pageLink->isInheritZoom());
    QVERIFY(!pageLink->getStyleManager());

    // Free text - hostile style is bounded
    PDFCreateFreeTextTool* freeText = fixture.addTool(new PDFCreateFreeTextTool(proxy, tools, nullptr, tools));
    PDFFreeTextStyle style;
    style.fontFamily = QString();
    style.fontSize = std::numeric_limits<PDFReal>::quiet_NaN();
    style.textColor = QColor();
    style.textAlignment = TextAlignment(Qt::AlignBottom);
    freeText->setFreeTextStyle(style);
    QCOMPARE(freeText->getFreeTextStyle().fontFamily, QString("Helvetica"));
    QCOMPARE(freeText->getFreeTextStyle().fontSize, 10.0);
    QCOMPARE(freeText->getFreeTextStyle().textColor, QColor(Qt::black));
    QCOMPARE(freeText->getFreeTextStyle().textAlignment, TextAlignment(Qt::AlignTop | Qt::AlignLeft));

    style.fontFamily = QString(300, QChar('A'));
    style.fontSize = 1000.0;
    style.textColor = Qt::darkGreen;
    style.textAlignment = TextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    freeText->setFreeTextStyle(style);
    QCOMPARE(freeText->getFreeTextStyle().fontFamily, QString("Helvetica"));
    QCOMPARE(freeText->getFreeTextStyle().fontSize, 512.0);
    QCOMPARE(freeText->getFreeTextStyle().textColor, QColor(Qt::darkGreen));
    QCOMPARE(freeText->getFreeTextStyle().textAlignment, TextAlignment(Qt::AlignTop | Qt::AlignRight));

    style.fontFamily = QStringLiteral("  Arial  ");
    style.fontSize = 0.01;
    style.textAlignment = TextAlignment(Qt::AlignLeft | Qt::AlignRight);
    freeText->setFreeTextStyle(style);
    QCOMPARE(freeText->getFreeTextStyle().fontFamily, QString("Arial"));
    QCOMPARE(freeText->getFreeTextStyle().fontSize, 1.0);
    QCOMPARE(freeText->getFreeTextStyle().textAlignment, TextAlignment(Qt::AlignTop | Qt::AlignLeft));
    QVERIFY(freeText->isAutoResizeToContents());
    freeText->setAutoResizeToContents(false);
    QVERIFY(!freeText->isAutoResizeToContents());
    QVERIFY(!freeText->getStyleManager());

    // One rule of the text box style for the tool, its storage and the properties of an
    // annotation: a family longer than the limit of a PDF name (127) is Helvetica, not
    // a cut family; more horizontal alignments (left and right) are left
    std::vector<PDFFreeTextStyle> styles(8);
    styles[0].fontFamily = QString(200, QChar('A'));
    styles[1].fontFamily = QString(128, QChar('B'));
    styles[2].fontFamily = QString(127, QChar('C'));
    styles[3].fontFamily = QStringLiteral(" Ari") + QChar(u'\0') + QStringLiteral("al ");
    styles[4].textAlignment = TextAlignment(Qt::AlignLeft | Qt::AlignRight);
    styles[5].textAlignment = TextAlignment(Qt::AlignHCenter | Qt::AlignRight | Qt::AlignBottom);
    styles[6].textAlignment = TextAlignment(Qt::AlignJustify);
    styles[7].fontSize = -std::numeric_limits<PDFReal>::infinity();
    styles[7].textColor = QColor::fromCmykF(0.0, 1.0, 0.0, 0.0);
    for (const PDFFreeTextStyle& hostile : styles)
    {
        PDFAnnotationProperties properties;
        properties.freeTextStyle = hostile;
        const PDFFreeTextStyle tool = PDFCreateFreeTextTool::boundFreeTextStyle(hostile);
        const PDFFreeTextStyle core = PDFAnnotationPropertyEditor::bound(properties).freeTextStyle;
        QCOMPARE(tool.fontFamily, core.fontFamily);
        QCOMPARE(tool.fontSize, core.fontSize);
        QCOMPARE(tool.textColor, core.textColor);
        QCOMPARE(tool.textAlignment, core.textAlignment);
        QCOMPARE(PDFCreateFreeTextTool::boundFreeTextStyle(tool).fontFamily, tool.fontFamily);
    }
    QCOMPARE(PDFCreateFreeTextTool::boundFreeTextStyle(styles[0]).fontFamily, QStringLiteral("Helvetica"));
    QCOMPARE(PDFCreateFreeTextTool::boundFreeTextStyle(styles[1]).fontFamily, QStringLiteral("Helvetica"));
    QCOMPARE(PDFCreateFreeTextTool::boundFreeTextStyle(styles[2]).fontFamily, QString(127, QChar('C')));
    QCOMPARE(PDFCreateFreeTextTool::boundFreeTextStyle(styles[3]).fontFamily, QStringLiteral("Arial"));
    QCOMPARE(PDFCreateFreeTextTool::boundFreeTextStyle(styles[4]).textAlignment, TextAlignment(Qt::AlignTop | Qt::AlignLeft));
    QCOMPARE(PDFCreateFreeTextTool::boundFreeTextStyle(styles[5]).textAlignment, TextAlignment(Qt::AlignTop | Qt::AlignLeft));
    QCOMPARE(PDFCreateFreeTextTool::boundFreeTextStyle(styles[6]).textAlignment, TextAlignment(Qt::AlignTop | Qt::AlignLeft));
    QCOMPARE(PDFCreateFreeTextTool::boundFreeTextStyle(styles[7]).fontSize, 10.0);
    QCOMPARE(PDFCreateFreeTextTool::boundFreeTextStyle(styles[7]).textColor.spec(), QColor::Rgb);

    // Tools with a style
    PDFCreateLineTypeTool* polygon = fixture.addTool(new PDFCreateLineTypeTool(proxy, tools, PDFCreateLineTypeTool::Type::Polygon, nullptr, tools));
    QCOMPARE(polygon->getType(), PDFCreateLineTypeTool::Type::Polygon);
    QVERIFY(polygon->getStyleManager());
    QVERIFY(fixture.addTool(new PDFCreateEllipseTool(proxy, tools, nullptr, tools))->getStyleManager());
    QVERIFY(fixture.addTool(new PDFCreateFreehandCurveTool(proxy, tools, nullptr, tools))->getStyleManager());
    QVERIFY(fixture.addTool(new PDFCreateRedactRectangleTool(proxy, tools, nullptr, tools))->getStyleManager());
    QVERIFY(fixture.addTool(new PDFCreateRedactTextTool(proxy, tools, nullptr, tools))->getStyleManager());
    QVERIFY(!fixture.addTool(new PDFCreateHyperlinkTool(proxy, tools, nullptr, tools))->getStyleManager());
    QVERIFY(!fixture.addTool(new PDFDeleteAnnotationTool(proxy, tools, nullptr, tools))->getStyleManager());

    // The stamp and the icon are used by the created annotations
    tools->setActiveTool(stamp);
    fixture.click(fixture.device(QPointF(240, 60)));
    QCOMPARE(fixture.modificationCount, 1);
    const PDFAnnotationPtr createdStamp = PDFAnnotation::parse(&fixture.document->getStorage(), fixture.pageAnnotations().back());
    QVERIFY(createdStamp);
    QCOMPARE(createdStamp->getType(), AnnotationType::Stamp);
    QCOMPARE(static_cast<const PDFStampAnnotation*>(createdStamp.data())->getStamp(), Stamp::TopSecret);
}

void AnnotationToolsTest::styleStorageInterface()
{
    StyleWindowGuard windowGuard(false);

    PDFAnnotationStyle style;
    style.strokeColor = Qt::blue;
    style.fillColor = Qt::green;
    style.penWidth = 3.0;

    PDFAnnotationStyle defaultStyle;
    defaultStyle.strokeColor = Qt::red;
    defaultStyle.penWidth = 2.0;

    // The legacy redaction color of the old applications in QSettings
    {
        QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
        settings.beginGroup("RedactTool");
        settings.setValue("redactColor", QColor(Qt::green));
        settings.endGroup();
    }

    FakeStyleStorage storage;
    {
        StyleStorageGuard storageGuard(&storage);
        QVERIFY(!PDFAnnotationStyleSettings::isDefaultStorage());
        QCOMPARE(PDFAnnotationStyleSettings::getStorage(), static_cast<PDFAnnotationStyleStorage*>(&storage));

        PDFAnnotationStyleSettings::setStyle(PDFAnnotationStyleSettings::STYLE_SHAPE, style);
        QCOMPARE(storage.writeCount, 1);
        QVERIFY(storage.styles[PDFAnnotationStyleSettings::STYLE_SHAPE] == style);
        QVERIFY(PDFAnnotationStyleSettings::getStyle(PDFAnnotationStyleSettings::STYLE_SHAPE, defaultStyle) == style);
        QVERIFY(PDFAnnotationStyleSettings::getStyle("Missing", defaultStyle) == defaultStyle);

        // Values of the storage are untrusted - they are bounded
        PDFAnnotationStyle hostile;
        hostile.strokeColor = QColor();
        hostile.fillColor = QColor(1, 2, 3, 4);
        hostile.penWidth = std::numeric_limits<PDFReal>::quiet_NaN();
        storage.styles["Hostile"] = hostile;
        PDFAnnotationStyle bounded = PDFAnnotationStyleSettings::getStyle("Hostile", defaultStyle);
        QCOMPARE(bounded.strokeColor, QColor(Qt::red));
        QCOMPARE(bounded.fillColor, QColor(1, 2, 3));
        QCOMPARE(bounded.penWidth, 2.0);

        storage.styles["Hostile"].penWidth = 1e9;
        storage.styles["Hostile"].strokeColor = QColor(10, 20, 30, 0);
        bounded = PDFAnnotationStyleSettings::getStyle("Hostile", defaultStyle);
        QCOMPARE(bounded.penWidth, PDFAnnotationStyleSettings::MAXIMUM_PEN_WIDTH);
        QCOMPARE(bounded.strokeColor, QColor(10, 20, 30));
        storage.styles["Hostile"].penWidth = -5.0;
        QCOMPARE(PDFAnnotationStyleSettings::getStyle("Hostile", defaultStyle).penWidth, 0.0);
        storage.styles["Hostile"].penWidth = std::numeric_limits<PDFReal>::infinity();
        QCOMPARE(PDFAnnotationStyleSettings::getStyle("Hostile", defaultStyle).penWidth, 2.0);
        storage.styles["Hostile"].fillColor = QColor();
        QVERIFY(!PDFAnnotationStyleSettings::getStyle("Hostile", defaultStyle).fillColor.isValid());

        // A tool reads and writes the installed storage
        TwoSquares squares;
        ToolsFixture fixture(squares.document);
        PDFCreateLineTypeTool* line = fixture.addTool(new PDFCreateLineTypeTool(fixture.proxy(), &fixture.tools, PDFCreateLineTypeTool::Type::Line, nullptr, &fixture.tools));
        QCOMPARE(line->getStrokeColor(), QColor(Qt::blue));
        QCOMPARE(line->getPenWidth(), 3.0);
        line->setStrokeColor(Qt::magenta);
        QCOMPARE(storage.styles[PDFAnnotationStyleSettings::STYLE_SHAPE].strokeColor, QColor(Qt::magenta));

        // A change of another instance - the application reloads the style
        storage.styles[PDFAnnotationStyleSettings::STYLE_SHAPE].strokeColor = Qt::cyan;
        QSignalSpy styleChanges(line->getStyleManager(), &PDFAnnotationStyleManager::styleChanged);
        line->getStyleManager()->reloadStyle();
        QCOMPARE(styleChanges.count(), 1);
        QCOMPARE(line->getStrokeColor(), QColor(Qt::cyan));

        // A style of the application is bounded too
        line->getStyleManager()->setStyle(hostile);
        QCOMPARE(line->getStrokeColor(), QColor(Qt::red));
        QCOMPARE(line->getPenWidth(), 1.0);

        // The tool shares the style with another shape tool
        PDFCreateEllipseTool* ellipse = fixture.addTool(new PDFCreateEllipseTool(fixture.proxy(), &fixture.tools, nullptr, &fixture.tools));
        fixture.tools.setActiveTool(ellipse);
        QCOMPARE(ellipse->getStrokeColor(), QColor(Qt::red));
        fixture.tools.setActiveTool(nullptr);

        // The storage of the application has no legacy redaction color
        QCOMPARE(PDFCreateRedactRectangleTool::getRedactColor(), QColor(Qt::black));
    }

    // Without a storage of the application the styles are in QSettings again
    QVERIFY(PDFAnnotationStyleSettings::isDefaultStorage());
    QVERIFY(PDFAnnotationStyleSettings::getStorage() != static_cast<PDFAnnotationStyleStorage*>(&storage));
    QCOMPARE(PDFCreateRedactRectangleTool::getRedactColor(), QColor(Qt::green));

    PDFAnnotationStyleSettings::setStyle("StorageTest", style);
    {
        QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
        QCOMPARE(settings.value("AnnotationStyles/StorageTest/penWidth").toDouble(), 3.0);
        QCOMPARE(settings.value("AnnotationStyles/StorageTest/strokeColor").value<QColor>(), QColor(Qt::blue));
    }
    QVERIFY(PDFAnnotationStyleSettings::getStyle("StorageTest", defaultStyle) == style);
    QCOMPARE(storage.writeCount, 3);
}

void AnnotationToolsTest::styleWindowDisabled()
{
    TwoSquares squares;
    ToolsFixture fixture(squares.document);
    PDFCreateLineTypeTool* line = fixture.addTool(new PDFCreateLineTypeTool(fixture.proxy(), &fixture.tools, PDFCreateLineTypeTool::Type::Rectangle, nullptr, &fixture.tools));
    PDFCreateHighlightTextTool* highlight = fixture.addTool(new PDFCreateHighlightTextTool(fixture.proxy(), &fixture.tools, nullptr, &fixture.tools));

    {
        StyleWindowGuard windowGuard(false);
        QVERIFY(!PDFAnnotationStyleManager::isStyleWindowEnabled());

        fixture.tools.setActiveTool(line);
        QVERIFY(line->isActive());
        QVERIFY(!line->getStyleManager()->isStyleWindowShown());
        QCOMPARE(AnnotationToolsTestHelper::countVisibleStyleWindows(&fixture.widget), 0);

        fixture.tools.setActiveTool(highlight);
        QVERIFY(!highlight->getStyleManager()->isStyleWindowShown());
        QCOMPARE(AnnotationToolsTestHelper::countVisibleStyleWindows(&fixture.widget), 0);
        fixture.tools.setActiveTool(nullptr);
    }

    // The old applications show the window
    QVERIFY(PDFAnnotationStyleManager::isStyleWindowEnabled());
    fixture.tools.setActiveTool(line);
    QVERIFY(line->getStyleManager()->isStyleWindowShown());
    QCOMPARE(AnnotationToolsTestHelper::countVisibleStyleWindows(&fixture.widget), 1);
    fixture.tools.setActiveTool(nullptr);
    QVERIFY(!line->getStyleManager()->isStyleWindowShown());
    QCOMPARE(AnnotationToolsTestHelper::countVisibleStyleWindows(&fixture.widget), 0);
}

void AnnotationToolsTest::delegatedCreation()
{
    using Request = PDFAnnotationCreationRequest;

    StyleWindowGuard windowGuard(false);
    DialogWatcher watcher;

    TwoSquares squares;
    ToolsFixture fixture(squares.document);
    PDFDrawWidgetProxy* proxy = fixture.proxy();
    PDFToolManager* tools = &fixture.tools;
    QVERIFY(!tools->isUserInterfaceDelegated());
    tools->setUserInterfaceDelegated(true);
    QVERIFY(tools->isUserInterfaceDelegated());
    QSignalSpy requests(tools, &PDFToolManager::annotationCreationRequested);

    // Sticky note - the point and the icon
    PDFCreateStickyNoteTool* sticky = fixture.addTool(new PDFCreateStickyNoteTool(proxy, tools, nullptr, tools));
    sticky->setIcon(TextAnnotationIcon::Help);
    tools->setActiveTool(sticky);
    fixture.click(fixture.device(QPointF(230, 80)));
    QCOMPARE(requests.count(), 1);
    Request request = requests.at(0).at(0).value<Request>();
    QCOMPARE(request.kind, Request::Kind::StickyNote);
    QCOMPARE(request.pageIndex, PDFInteger(0));
    QCOMPARE(request.pageReference, squares.page);
    QVERIFY(AnnotationToolsTestHelper::fuzzyCompare(request.point, QPointF(230, 80)));
    QCOMPARE(request.icon, TextAnnotationIcon::Help);
    QVERIFY(!sticky->isActive());

    // Free text - the rectangle and the style
    PDFCreateFreeTextTool* freeText = fixture.addTool(new PDFCreateFreeTextTool(proxy, tools, nullptr, tools));
    PDFFreeTextStyle style;
    style.fontSize = 14.0;
    style.textColor = Qt::blue;
    freeText->setFreeTextStyle(style);
    freeText->setAutoResizeToContents(false);
    tools->setActiveTool(freeText);
    fixture.click(fixture.device(QPointF(200, 20)));
    fixture.click(fixture.device(QPointF(280, 120)));
    QCOMPARE(requests.count(), 2);
    request = requests.at(1).at(0).value<Request>();
    QCOMPARE(request.kind, Request::Kind::FreeText);
    QCOMPARE(request.pageReference, squares.page);
    QVERIFY(AnnotationToolsTestHelper::fuzzyCompare(request.rectangle, QRectF(QPointF(200, 20), QPointF(280, 120))));
    QCOMPARE(request.freeTextStyle.fontSize, 14.0);
    QCOMPARE(request.freeTextStyle.textColor, QColor(Qt::blue));
    QVERIFY(!request.autoResizeToContents);
    QVERIFY(!freeText->isActive());

    // Hyperlink - the rectangle and the highlight mode
    PDFCreateHyperlinkTool* hyperlink = fixture.addTool(new PDFCreateHyperlinkTool(proxy, tools, nullptr, tools));
    hyperlink->setHighlightMode(LinkHighlightMode::Invert);
    tools->setActiveTool(hyperlink);
    fixture.click(fixture.device(QPointF(20, 200)));
    fixture.click(fixture.device(QPointF(120, 260)));
    QCOMPARE(requests.count(), 3);
    request = requests.at(2).at(0).value<Request>();
    QCOMPARE(request.kind, Request::Kind::Hyperlink);
    QVERIFY(AnnotationToolsTestHelper::fuzzyCompare(request.rectangle, QRectF(QPointF(20, 200), QPointF(120, 260))));
    QCOMPARE(request.highlightMode, LinkHighlightMode::Invert);
    QVERIFY(!hyperlink->isActive());

    // An empty rectangle asks for nothing, the tool stays active
    tools->setActiveTool(hyperlink);
    fixture.click(fixture.device(QPointF(60, 220)));
    fixture.click(fixture.device(QPointF(60, 220)));
    QCOMPARE(requests.count(), 3);
    QVERIFY(hyperlink->isActive());
    tools->setActiveTool(nullptr);

    // No dialog has appeared and nothing has been created - the application creates the annotations
    QCOMPARE(watcher.dialogs, 0);
    QCOMPARE(watcher.popups, 0);
    QCOMPARE(fixture.modificationCount, 0);

    // The tool is deleted later by the receiver of its request
    QObject::connect(tools, &PDFToolManager::annotationCreationRequested, sticky, &QObject::deleteLater);
    QPointer<PDFCreateStickyNoteTool> stickyGuard(sticky);
    tools->setActiveTool(sticky);
    fixture.click(fixture.device(QPointF(230, 120)));
    QCOMPARE(requests.count(), 4);
    QTRY_VERIFY(stickyGuard.isNull());

    // Without the delegation the tool shows its own dialog (the old applications)
    tools->setUserInterfaceDelegated(false);
    PDFCreateStickyNoteTool* oldSticky = fixture.addTool(new PDFCreateStickyNoteTool(proxy, tools, nullptr, tools));
    tools->setActiveTool(oldSticky);
    fixture.click(fixture.device(QPointF(230, 80)));
    QCOMPARE(watcher.dialogs, 1);
    QCOMPARE(requests.count(), 4);
    QCOMPARE(fixture.modificationCount, 0);
    QVERIFY(oldSticky->isActive());
    tools->setActiveTool(nullptr);
}

void AnnotationToolsTest::delegatedManagerUi()
{
    DialogWatcher watcher;

    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference first = builder.createAnnotationSquare(page, QRectF(50, 50, 60, 40), 1.0, Qt::yellow, Qt::black, "First", "Subject", "First contents");
    const PDFObjectReference second = builder.createAnnotationSquare(page, QRectF(150, 150, 40, 40), 1.0, Qt::green, Qt::black, "Second", "Subject", "Second contents");
    const PDFObjectReference hidden = builder.createAnnotationSquare(page, QRectF(220, 40, 30, 30), 1.0, Qt::green, Qt::black, "Hidden", "Subject", "Hidden contents");
    AnnotationToolsTestHelper::setFlags(builder, hidden, PDFAnnotation::Hidden);
    ToolsFixture fixture(builder.build());
    PDFWidgetAnnotationManager& annotations = fixture.annotations;

    QVERIFY(!annotations.isUserInterfaceDelegated());
    annotations.setUserInterfaceDelegated(true);
    QVERIFY(annotations.isUserInterfaceDelegated());
    QSignalSpy menus(&annotations, &PDFWidgetAnnotationManager::menuRequested);
    QSignalSpy popups(&annotations, &PDFWidgetAnnotationManager::popupRequested);
    QSignalSpy properties(&annotations, &PDFWidgetAnnotationManager::propertiesRequested);

    // Right click - the annotation is selected and the application shows the menu
    const QPoint firstPosition = fixture.device(QPointF(80, 70));
    fixture.rightClick(firstPosition);
    QCOMPARE(menus.count(), 1);
    PDFAnnotationMenuRequest request = menus.at(0).at(0).value<PDFAnnotationMenuRequest>();
    QCOMPARE(request.kind, PDFAnnotationMenuRequest::Kind::Selection);
    QCOMPARE(request.annotation, first);
    QCOMPARE(request.pageReference, page);
    QCOMPARE(request.selection, std::vector<PDFObjectReference>({ first }));
    QVERIFY(request.widgetPosition.has_value());
    QCOMPARE(*request.widgetPosition, firstPosition);
    QCOMPARE(request.globalPosition, fixture.widget.mapToGlobal(firstPosition));
    QCOMPARE(request.pageIndex, PDFInteger(0));
    QVERIFY(AnnotationToolsTestHelper::fuzzyCompare(request.pagePosition, QPointF(80, 70)));
    QCOMPARE(request.annotationsUnderCursor, std::vector<PDFObjectReference>({ first }));
    QVERIFY(request.isModificationAllowed);
    QVERIFY(annotations.isAnnotationSelected(first));

    // F2 / Enter - the properties (also of a read only manager)
    QVERIFY(fixture.annotationKey(Qt::Key_F2));
    QCOMPARE(properties.count(), 1);
    QCOMPARE(properties.at(0).at(0).value<PDFObjectReference>(), first);
    annotations.setReadOnly(true);
    QVERIFY(fixture.annotationKey(Qt::Key_Return));
    QCOMPARE(properties.count(), 2);
    annotations.setReadOnly(false);

    // Menu key - the menu without a position in the widget
    QVERIFY(fixture.annotationKey(Qt::Key_Menu));
    QCOMPARE(menus.count(), 2);
    request = menus.at(1).at(0).value<PDFAnnotationMenuRequest>();
    QCOMPARE(request.kind, PDFAnnotationMenuRequest::Kind::Selection);
    QVERIFY(!request.widgetPosition.has_value());
    QCOMPARE(request.annotation, first);

    // Double click - the popup window of the markup annotation
    fixture.doubleClick(fixture.device(QPointF(170, 170)));
    QCOMPARE(popups.count(), 1);
    QCOMPARE(popups.at(0).at(0).value<PDFObjectReference>(), second);
    QCOMPARE(popups.at(0).at(1).value<PDFObjectReference>(), page);

    // Menu of a list (Notes): a hidden annotation cannot be selected
    annotations.showAnnotationMenu(hidden, page, QPoint(10, 20));
    QCOMPARE(menus.count(), 3);
    request = menus.at(2).at(0).value<PDFAnnotationMenuRequest>();
    QCOMPARE(request.kind, PDFAnnotationMenuRequest::Kind::Unselectable);
    QCOMPARE(request.annotation, hidden);
    QCOMPARE(request.pageReference, page);
    QCOMPARE(request.globalPosition, QPoint(10, 20));
    QVERIFY(!annotations.hasSelection());

    annotations.showAnnotationMenu(second, page, QPoint(30, 40));
    QCOMPARE(menus.count(), 4);
    request = menus.at(3).at(0).value<PDFAnnotationMenuRequest>();
    QCOMPARE(request.kind, PDFAnnotationMenuRequest::Kind::Selection);
    QCOMPARE(request.annotation, second);
    QVERIFY(annotations.isAnnotationSelected(second));

    // No menu and no dialog of the manager has appeared
    QCOMPARE(watcher.popups, 0);
    QCOMPARE(watcher.dialogs, 0);
    QCOMPARE(fixture.modificationCount, 0);

    // An empty place of a page - the menu of the selection, while something can be selected
    QApplication::clipboard()->clear();
    annotations.clearSelection();
    QCOMPARE(annotations.getMenuRequest(fixture.device(QPointF(280, 280))).kind, PDFAnnotationMenuRequest::Kind::Selection);

    // Without the delegation the manager shows its own menu (the old applications)
    annotations.setUserInterfaceDelegated(false);
    fixture.rightClick(firstPosition);
    QCOMPARE(watcher.popups, 1);
    QCOMPARE(menus.count(), 4);
}

void AnnotationToolsTest::menuRequestContext()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference polygon = builder.createAnnotationPolygon(page, { QPointF(40, 40), QPointF(140, 40), QPointF(140, 140), QPointF(40, 140) }, 1.0, Qt::yellow, Qt::black, "Polygon", "Subject", "Contents");
    const PDFObjectReference lower = builder.createAnnotationSquare(page, QRectF(180, 180, 60, 60), 1.0, Qt::yellow, Qt::black, "Lower", "Subject", "Contents");
    const PDFObjectReference upper = builder.createAnnotationSquare(page, QRectF(200, 200, 60, 60), 1.0, Qt::green, Qt::black, "Upper", "Subject", "Contents");
    const PDFObjectReference freeText = builder.createAnnotationFreeText(page, QRectF(180, 20, 100, 40), "Free text", "Subject", "Contents", PDFFreeTextStyle(), false);
    ToolsFixture fixture(builder.build());
    PDFWidgetAnnotationManager& annotations = fixture.annotations;
    annotations.setUserInterfaceDelegated(true);
    QSignalSpy menus(&annotations, &PDFWidgetAnnotationManager::menuRequested);
    QApplication::clipboard()->clear();

    // Overlapping annotations, the topmost is not selected - a choice, the selection is not changed
    const QPoint overlap = fixture.device(QPointF(220, 220));
    fixture.rightClick(overlap);
    QCOMPARE(menus.count(), 1);
    PDFAnnotationMenuRequest request = menus.at(0).at(0).value<PDFAnnotationMenuRequest>();
    QCOMPARE(request.kind, PDFAnnotationMenuRequest::Kind::Overlap);
    QCOMPARE(request.annotationsUnderCursor, std::vector<PDFObjectReference>({ upper, lower }));
    QCOMPARE(request.annotation, upper);
    QCOMPARE(request.pageReference, page);
    QVERIFY(!annotations.hasSelection());
    QCOMPARE(annotations.getMenuRequest(overlap).kind, PDFAnnotationMenuRequest::Kind::Overlap);
    QVERIFY(!annotations.hasSelection());

    // The topmost one selected - the menu of the selection
    annotations.setSelectedAnnotations({ upper });
    fixture.rightClick(overlap);
    QCOMPARE(menus.count(), 2);
    request = menus.at(1).at(0).value<PDFAnnotationMenuRequest>();
    QCOMPARE(request.kind, PDFAnnotationMenuRequest::Kind::Selection);
    QCOMPARE(request.annotation, upper);

    // Several selected annotations - no single annotation
    annotations.setSelectedAnnotations({ upper, lower });
    request = annotations.getMenuRequest(overlap);
    QCOMPARE(request.kind, PDFAnnotationMenuRequest::Kind::Selection);
    QVERIFY(!request.annotation.isValid());
    QCOMPARE(request.selection.size(), size_t(2));

    // A vertex of the selected polygon (it can be removed, the polygon has four of them)
    annotations.setSelectedAnnotations({ polygon });
    fixture.rightClick(fixture.device(QPointF(140, 140)));
    request = menus.at(menus.count() - 1).at(0).value<PDFAnnotationMenuRequest>();
    QCOMPARE(request.kind, PDFAnnotationMenuRequest::Kind::Selection);
    QCOMPARE(request.pointAnnotation, polygon);
    QCOMPARE(request.removablePoint, 2);
    QCOMPARE(request.insertSegment, -1);

    // A segment of the polygon - a point can be inserted
    request = annotations.getMenuRequest(fixture.device(QPointF(90, 140)));
    QCOMPARE(request.pointAnnotation, polygon);
    QCOMPARE(request.removablePoint, -1);
    QCOMPARE(request.insertSegment, 2);
    QVERIFY(AnnotationToolsTestHelper::fuzzyCompare(request.insertPosition, QPointF(90, 140)));
    QVERIFY(!request.canPaste);

    // The clipboard with annotations
    annotations.copySelectedAnnotations();
    QVERIFY(annotations.getMenuRequest(fixture.device(QPointF(90, 90))).canPaste);

    // Callout line of a free text can be added
    annotations.setSelectedAnnotations({ freeText });
    request = annotations.getMenuRequest(fixture.device(QPointF(150, 200)));
    QCOMPARE(request.annotation, freeText);
    QVERIFY(request.canAddCalloutLine);
    QVERIFY(!request.canRemoveCalloutLine);
    QVERIFY(AnnotationToolsTestHelper::fuzzyCompare(request.calloutTip, QPointF(150, 200)));
    QVERIFY(annotations.addAnnotationCalloutLine(freeText, request.calloutTip));
    annotations.setSelectedAnnotations({ freeText });
    request = annotations.getMenuRequest(fixture.device(QPointF(150, 200)));
    QVERIFY(request.canRemoveCalloutLine);
    QVERIFY(!request.canAddCalloutLine);

    // A read only manager says, that the document does not allow changes
    annotations.setReadOnly(true);
    request = annotations.getMenuRequest(fixture.device(QPointF(150, 200)));
    QVERIFY(!request.isModificationAllowed);
    QVERIFY(!request.canPaste);
    QVERIFY(!request.canRemoveCalloutLine);
}

void AnnotationToolsTest::readOnlyManager()
{
    TwoSquares squares;
    ToolsFixture fixture(squares.document);
    PDFWidgetAnnotationManager& annotations = fixture.annotations;
    QApplication::clipboard()->clear();

    // Handles and markers of an editable selection
    annotations.setSelectedAnnotations({ squares.first });
    const int editablePixels = ToolsFixture::countDrawn(fixture.drawMarkers());
    annotations.clearSelection();

    QVERIFY(!annotations.isReadOnly());
    QSignalSpy selectionChanges(&annotations, &PDFWidgetAnnotationManager::selectionChanged);
    annotations.setReadOnly(true);
    QVERIFY(annotations.isReadOnly());
    QVERIFY(!annotations.isModificationAllowed());

    // The selection works
    fixture.click(fixture.device(QPointF(80, 70)));
    QVERIFY(annotations.isAnnotationSelected(squares.first));
    QVERIFY(selectionChanges.count() > 0);

    // Every change is refused
    fixture.annotationKey(Qt::Key_Delete);
    fixture.annotationKey(Qt::Key_Right);
    annotations.deleteSelectedAnnotations();
    annotations.cutSelectedAnnotations();
    annotations.setSelectedAnnotationsColor(Qt::blue);
    annotations.setSelectedAnnotationsFillColor(Qt::blue);
    annotations.setSelectedAnnotationsOpacity(0.5);
    annotations.rotateSelectedAnnotations(90.0);
    QVERIFY(!annotations.setAnnotationsHidden({ squares.first }, true));
    QVERIFY(!annotations.deleteAnnotations({ squares.first }));
    QVERIFY(!annotations.addAnnotationReply(squares.first, "Reply"));
    QVERIFY(!annotations.setAnnotationRectangle(squares.first, QRectF(10, 10, 20, 20)));
    annotations.copySelectedAnnotationsToPages({ 0 });
    QCOMPARE(fixture.modificationCount, 0);

    // The copy works, the paste does not
    annotations.copySelectedAnnotations();
    const QMimeData* mimeData = QApplication::clipboard()->mimeData();
    QVERIFY(mimeData && mimeData->hasFormat(PDFAnnotationManipulator::getMimeType()));
    QVERIFY(!annotations.canPasteAnnotations());
    annotations.pasteAnnotations(std::nullopt);
    QCOMPARE(fixture.modificationCount, 0);

    // The handles are not shown
    const int readOnlyPixels = ToolsFixture::countDrawn(fixture.drawMarkers());
    QVERIFY(readOnlyPixels > 0);
    QVERIFY(readOnlyPixels < editablePixels);

    // Editable again
    annotations.setReadOnly(false);
    QVERIFY(annotations.isModificationAllowed());
    QVERIFY(ToolsFixture::countDrawn(fixture.drawMarkers()) > readOnlyPixels);
    QVERIFY(fixture.annotationKey(Qt::Key_Delete));
    QCOMPARE(fixture.modificationCount, 1);
}

void AnnotationToolsTest::fillAndHidden()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference square = builder.createAnnotationSquare(page, QRectF(50, 50, 60, 40), 1.0, Qt::yellow, Qt::black, "Square", "Subject", "Contents");
    const PDFObjectReference note = builder.createAnnotationText(page, QRectF(200, 200, 20, 20), TextAnnotationIcon::Note, "Note", "Subject", "Contents", false);
    const PDFObjectReference locked = builder.createAnnotationSquare(page, QRectF(150, 50, 60, 40), 1.0, Qt::yellow, Qt::black, "Locked", "Subject", "Contents");
    AnnotationToolsTestHelper::setFlags(builder, locked, PDFAnnotation::Locked);
    ToolsFixture fixture(builder.build());
    PDFWidgetAnnotationManager& annotations = fixture.annotations;
    const PDFObject lockedFill = fixture.entry(locked, "IC");

    // Fill color - only the annotations with a fill, one step
    annotations.setSelectedAnnotations({ square, note, locked });
    annotations.setSelectedAnnotationsFillColor(QColor(Qt::blue));
    QCOMPARE(fixture.modificationCount, 1);
    PDFDocumentDataLoaderDecorator loader(fixture.document.data());
    QCOMPARE(loader.readNumberArray(fixture.entry(square, "IC")), std::vector<PDFReal>({ 0.0, 0.0, 1.0 }));
    QVERIFY(fixture.entry(note, "IC").isNull());
    QCOMPARE(fixture.entry(locked, "IC"), lockedFill);

    // Invalid color - no fill
    annotations.setSelectedAnnotationsFillColor(QColor());
    QCOMPARE(fixture.modificationCount, 2);
    QVERIFY(fixture.entry(square, "IC").isNull());

    // Hidden - one step, locked and duplicate annotations skipped
    QVERIFY(annotations.setAnnotationsHidden({ square, locked, square }, true));
    QCOMPARE(fixture.modificationCount, 3);
    QVERIFY(fixture.flags(square).testFlag(PDFAnnotation::Hidden));
    QVERIFY(!fixture.flags(locked).testFlag(PDFAnnotation::Hidden));
    QVERIFY(fixture.flags(locked).testFlag(PDFAnnotation::Locked));

    // Nothing to change - no step
    QVERIFY(!annotations.setAnnotationsHidden({ square }, true));
    QVERIFY(!annotations.setAnnotationsHidden({ PDFObjectReference(9999, 0) }, true));
    QVERIFY(!annotations.setAnnotationsHidden({}, true));
    QCOMPARE(fixture.modificationCount, 3);

    // A hidden annotation cannot be selected on the page, it is shown again
    annotations.setSelectedAnnotations({ square });
    QVERIFY(!annotations.hasSelection());
    QVERIFY(annotations.setAnnotationsHidden({ square }, false));
    QCOMPARE(fixture.modificationCount, 4);
    QVERIFY(!fixture.flags(square).testFlag(PDFAnnotation::Hidden));
    annotations.setSelectedAnnotations({ square });
    QVERIFY(annotations.isAnnotationSelected(square));
}

void AnnotationToolsTest::deleteAnnotationsOneStep()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference first = builder.createAnnotationSquare(page, QRectF(10, 10, 30, 30), 1.0, Qt::yellow, Qt::black, "1", "Subject", "Contents");
    const PDFObjectReference second = builder.createAnnotationSquare(page, QRectF(60, 10, 30, 30), 1.0, Qt::yellow, Qt::black, "2", "Subject", "Contents");
    const PDFObjectReference hidden = builder.createAnnotationSquare(page, QRectF(110, 10, 30, 30), 1.0, Qt::yellow, Qt::black, "3", "Subject", "Contents");
    const PDFObjectReference locked = builder.createAnnotationSquare(page, QRectF(160, 10, 30, 30), 1.0, Qt::yellow, Qt::black, "4", "Subject", "Contents");
    AnnotationToolsTestHelper::setFlags(builder, hidden, PDFAnnotation::Hidden);
    AnnotationToolsTestHelper::setFlags(builder, locked, PDFAnnotation::Locked);
    ToolsFixture fixture(builder.build());

    QVERIFY(!fixture.annotations.deleteAnnotations({}));
    QVERIFY(!fixture.annotations.deleteAnnotations({ locked }));
    QCOMPARE(fixture.modificationCount, 0);

    // A hidden annotation (from the list of the Notes) is deleted too
    QVERIFY(fixture.annotations.deleteAnnotations({ first, second, hidden, locked, first, PDFObjectReference(9999, 0) }));
    QCOMPARE(fixture.modificationCount, 1);

    const std::vector<PDFObjectReference> remaining = fixture.pageAnnotations();
    for (const PDFObjectReference& deleted : { first, second, hidden })
    {
        QVERIFY(std::find(remaining.cbegin(), remaining.cend(), deleted) == remaining.cend());
    }
    QVERIFY(std::find(remaining.cbegin(), remaining.cend(), locked) != remaining.cend());
}

void AnnotationToolsTest::viewColorsSelection()
{
    // Defaults are the colors of the old applications, also of a view, which sets only the colors of step 3
    const PDFViewColors defaults = PDFViewColors::getDefaultColors();
    const PDFViewColors constructed;
    for (const PDFViewColors& colors : { defaults, constructed })
    {
        QCOMPARE(colors.annotationSelection, QColor(0, 120, 215));
        QCOMPARE(colors.annotationHandle, QColor(Qt::white));
        QCOMPARE(colors.annotationPoint, QColor(220, 0, 0));
        QCOMPARE(colors.annotationPointActive, QColor(Qt::white));
        QCOMPARE(colors.rubberBand, QColor(0, 120, 215));
        QCOMPARE(colors.snapMarker, QColor(0, 160, 0));
        QVERIFY(!colors.lockBadge.isValid());
    }

    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference polygon = builder.createAnnotationPolygon(page, { QPointF(50, 50), QPointF(150, 50), QPointF(100, 150) }, 1.0, Qt::yellow, Qt::black, "Polygon", "Subject", "Contents");
    ToolsFixture fixture(builder.build());
    PDFDrawWidgetProxy* proxy = fixture.proxy();
    fixture.annotations.setSelectedAnnotations({ polygon });

    // Old colors
    QVERIFY(!proxy->hasViewColors());
    QImage image = fixture.drawMarkers();
    QVERIFY(ToolsFixture::countColor(image, QColor(0, 120, 215)) > 0);
    QVERIFY(ToolsFixture::countColor(image, QColor(220, 0, 0)) > 0);

    // Colors of the theme
    PDFViewColors colors = PDFViewColors::getDefaultColors();
    colors.annotationSelection = QColor(200, 30, 90);
    colors.annotationPoint = QColor(10, 180, 40);
    colors.annotationHandle = QColor(250, 240, 20);
    colors.rubberBand = QColor(40, 40, 250);
    proxy->setViewColors(colors);
    QVERIFY(proxy->hasViewColors());
    image = fixture.drawMarkers();
    QVERIFY(ToolsFixture::countColor(image, QColor(200, 30, 90)) > 0);
    QVERIFY(ToolsFixture::countColor(image, QColor(10, 180, 40)) > 0);
    QVERIFY(ToolsFixture::countColor(image, QColor(250, 240, 20)) > 0);
    QCOMPARE(ToolsFixture::countColor(image, QColor(0, 120, 215)), 0);
    QCOMPARE(ToolsFixture::countColor(image, QColor(220, 0, 0)), 0);
    QCOMPARE(ToolsFixture::countColor(image, QColor(40, 40, 250)), 0);

    // Rubber band
    fixture.annotations.clearSelection();
    fixture.moveTo(fixture.device(QPointF(200, 200)), Qt::ControlModifier);
    fixture.mouse(QEvent::MouseButtonPress, fixture.device(QPointF(200, 200)), Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
    fixture.mouse(QEvent::MouseMove, fixture.device(QPointF(280, 280)), Qt::NoButton, Qt::LeftButton, Qt::ControlModifier);
    image = fixture.drawMarkers();
    QVERIFY(ToolsFixture::countColor(image, QColor(40, 40, 250)) > 0);
    QVERIFY(fixture.annotationKey(Qt::Key_Escape));

    // Back to the old colors
    proxy->resetViewColors();
    fixture.annotations.setSelectedAnnotations({ polygon });
    image = fixture.drawMarkers();
    QVERIFY(ToolsFixture::countColor(image, QColor(0, 120, 215)) > 0);
    QCOMPARE(ToolsFixture::countColor(image, QColor(200, 30, 90)), 0);
}

void AnnotationToolsTest::lockBadge()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference square = builder.createAnnotationSquare(page, QRectF(50, 150, 60, 40), 1.0, Qt::yellow, Qt::black, "Square", "Subject", "Contents");
    const PDFObjectReference locked = builder.createAnnotationSquare(page, QRectF(150, 150, 60, 40), 1.0, Qt::yellow, Qt::black, "Locked", "Subject", "Contents");
    const PDFObjectReference readOnly = builder.createAnnotationSquare(page, QRectF(150, 50, 60, 40), 1.0, Qt::yellow, Qt::black, "Read only", "Subject", "Contents");
    AnnotationToolsTestHelper::setFlags(builder, locked, PDFAnnotation::Locked);
    AnnotationToolsTestHelper::setFlags(builder, readOnly, PDFAnnotation::ReadOnly);
    ToolsFixture fixture(builder.build());
    const QColor badge(255, 0, 255);

    // No badge by default (the old applications)
    fixture.annotations.setSelectedAnnotations({ locked });
    QCOMPARE(ToolsFixture::countColor(fixture.drawMarkers(), badge), 0);

    // The color of the badge turns it on
    PDFViewColors colors = PDFViewColors::getDefaultColors();
    colors.lockBadge = badge;
    fixture.proxy()->setViewColors(colors);
    QVERIFY(ToolsFixture::countColor(fixture.drawMarkers(), badge) > 20);

    fixture.annotations.setSelectedAnnotations({ readOnly });
    QVERIFY(ToolsFixture::countColor(fixture.drawMarkers(), badge) > 20);

    // An annotation, which is not locked, has no badge - not even in the read only mode
    fixture.annotations.setSelectedAnnotations({ square });
    QCOMPARE(ToolsFixture::countColor(fixture.drawMarkers(), badge), 0);
    fixture.annotations.setReadOnly(true);
    QCOMPARE(ToolsFixture::countColor(fixture.drawMarkers(), badge), 0);

    // Hovered locked annotation
    fixture.annotations.clearSelection();
    fixture.moveTo(fixture.device(QPointF(180, 170)));
    QVERIFY(ToolsFixture::countColor(fixture.drawMarkers(), badge) > 20);
}

void AnnotationToolsTest::designSelectionLook()
{
    // The look of the design of the new application (Figma 1015:17319, 1017:17699, 1018:18079,
    // 1018:18584, 1026:21979) against the look of the old applications
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference square = builder.createAnnotationSquare(page, QRectF(50, 150, 100, 60), 1.0, Qt::yellow, Qt::black, "Square", "Subject", "Contents");
    const PDFObjectReference polygon = builder.createAnnotationPolygon(page, { QPointF(30, 30), QPointF(130, 30), QPointF(80, 110) }, 1.0, Qt::yellow, Qt::black, "Polygon", "Subject", "Contents");
    const PDFObjectReference locked = builder.createAnnotationSquare(page, QRectF(190, 60, 70, 40), 1.0, Qt::yellow, Qt::black, "Locked", "Subject", "Contents");
    AnnotationToolsTestHelper::setFlags(builder, locked, PDFAnnotation::Locked);
    ToolsFixture fixture(builder.build());
    PDFWidgetAnnotationManager& annotations = fixture.annotations;

    const QColor selection(200, 30, 90);
    const QColor handle(250, 240, 20);
    const QColor point(10, 180, 40);
    const QColor rubberBand(40, 40, 250);
    const QColor badge(255, 0, 255);
    const QColor badgeBorder(0, 200, 200);
    PDFViewColors colors = PDFViewColors::getDefaultColors();
    colors.annotationSelection = selection;
    colors.annotationHandle = handle;
    colors.annotationPoint = point;
    colors.annotationPointActive = handle;
    colors.rubberBand = rubberBand;
    colors.lockBadge = badge;
    colors.lockBadgeBorder = badgeBorder;
    const QColor infoBackground(120, 250, 250);
    const QColor infoBorder(90, 90, 90);
    colors.infoText = QColor(0, 0, 180);
    colors.infoBackground = infoBackground;
    colors.infoBorder = infoBorder;
    const QColor infoShadow(255, 0, 0);
    colors.infoShadow = infoShadow;
    fixture.proxy()->setViewColors(colors);
    QVERIFY(!annotations.isDesignSelectionLook());

    const QRectF frame = fixture.pageToDevice().mapRect(QRectF(50, 150, 100, 60)).normalized();
    const QPoint center = frame.center().toPoint();
    const QPoint rotationNear(qRound(frame.center().x()), qRound(frame.top() - 15.0));
    const QPoint rotationOld(qRound(frame.center().x()), qRound(frame.top() - 26.0));
    const QPoint rotationFar(qRound(frame.center().x()), qRound(frame.top() - 32.0));
    // The design: the frame 4 px outside of the annotation (1015:17767), the rotation handle 22 above it
    const QRectF designFrame = frame.adjusted(-4.0, -4.0, 4.0, 4.0);
    const QPoint rotationDesign(qRound(frame.center().x()), qRound(designFrame.top() - 22.0));
    const QPoint rotationDesignFar(qRound(frame.center().x()), qRound(designFrame.top() - 33.0));

    // Rows of the stem of the rotation handle without a pixel (a dashed stem has gaps)
    auto stemGaps = [&](const QImage& image)
    {
        int gaps = 0;
        for (int distance = 8; distance <= 15; ++distance)
        {
            const int y = qRound(frame.top() - distance);
            int maximumAlpha = 0;
            for (int x = qRound(frame.center().x()) - 1; x <= qRound(frame.center().x()) + 1; ++x)
            {
                maximumAlpha = qMax(maximumAlpha, image.pixelColor(x, y).alpha());
            }
            gaps += maximumAlpha < 60 ? 1 : 0;
        }
        return gaps;
    };

    // The old look: the fill 12 %, the rotation handle 5 at the distance 24, a point filled by its color
    annotations.setSelectedAnnotations({ square });
    QImage image = fixture.drawMarkers();
    QVERIFY(stemGaps(image) > 0);
    QVERIFY(qAbs(image.pixelColor(center).alpha() - 31) <= 4);
    QVERIFY(!annotations.isInteractionAt(rotationNear, Qt::NoModifier));
    QVERIFY(annotations.isInteractionAt(rotationOld, Qt::NoModifier));

    annotations.setSelectedAnnotations({ polygon });
    image = fixture.drawMarkers();
    QCOMPARE(image.pixelColor(fixture.device(QPointF(30, 30))).rgb(), point.rgb());

    // The design: the fill 6 %, the rotation handle 12 at the distance 22, a point filled by the
    // handle color with the outline of the point color
    annotations.setDesignSelectionLook(true);
    QVERIFY(annotations.isDesignSelectionLook());
    annotations.setSelectedAnnotations({ square });
    image = fixture.drawMarkers();
    QCOMPARE(stemGaps(image), 0);
    QVERIFY(qAbs(image.pixelColor(center).alpha() - 15) <= 4);
    QVERIFY(annotations.isInteractionAt(rotationDesign, Qt::NoModifier));
    QVERIFY(!annotations.isInteractionAt(rotationNear, Qt::NoModifier));
    QVERIFY(!annotations.isInteractionAt(rotationDesignFar, Qt::NoModifier));

    // The frame 4 px outside of the annotation, the edge of the annotation inside of the fill,
    // the handles on the frame
    const int middleY = qRound(frame.top() + frame.height() * 0.25);
    QColor frameLine(0, 0, 0, 0);
    for (int x = qRound(designFrame.left()) - 1; x <= qRound(designFrame.left()) + 1; ++x)
    {
        frameLine = image.pixelColor(x, middleY).alpha() > frameLine.alpha() ? image.pixelColor(x, middleY) : frameLine;
    }
    QVERIFY(frameLine.alpha() > 150);
    QCOMPARE(frameLine.rgb(), selection.rgb());
    QVERIFY(qAbs(image.pixelColor(qRound(frame.left()) + 1, middleY).alpha() - 15) <= 4);
    QCOMPARE(image.pixelColor(qRound(frame.center().x()), qRound(designFrame.bottom())).rgb(), handle.rgb());
    QVERIFY(qAbs(image.pixelColor(qRound(frame.center().x()), qRound(frame.bottom()) - 6).alpha() - 15) <= 4);

    annotations.setSelectedAnnotations({ polygon });
    image = fixture.drawMarkers();
    const QPoint vertex = fixture.device(QPointF(30, 30));
    QCOMPARE(image.pixelColor(vertex).rgb(), handle.rgb());
    QVERIFY(ToolsFixture::countColor(image, point) > 0);

    // A multiple selection: the members have thin outlines without a fill, the frame holds the fill once
    annotations.setSelectedAnnotations({ square, polygon });
    image = fixture.drawMarkers();
    QVERIFY(qAbs(image.pixelColor(center).alpha() - 15) <= 4);

    // A locked annotation: the dashed frame and the round badge in the color of the outline
    annotations.setSelectedAnnotations({ locked });
    image = fixture.drawMarkers();
    QVERIFY(ToolsFixture::countColor(image, badgeBorder) > 20);
    QVERIFY(ToolsFixture::countColor(image, badge) > 10);
    const QRectF lockedFrame = fixture.pageToDevice().mapRect(QRectF(190, 60, 70, 40)).normalized();
    QCOMPARE(image.pixelColor(QPoint(qRound(lockedFrame.right() - 7.0), qRound(lockedFrame.top()))).rgb(), handle.rgb());

    // The box of the numbers of a drag (1017:18217): the fill and the outline of the view colors,
    // the shadow 0 / 4 / 16; the handle on the frame 4 px outside moves the edge of the annotation
    annotations.setSelectedAnnotations({ square });
    const QPoint corner = designFrame.bottomRight().toPoint();
    fixture.moveTo(corner);
    fixture.mouse(QEvent::MouseButtonPress, corner, Qt::LeftButton, Qt::LeftButton);
    fixture.mouse(QEvent::MouseMove, corner + QPoint(30, 30), Qt::NoButton, Qt::LeftButton);
    image = fixture.drawMarkers();
    QVERIFY(ToolsFixture::countColor(image, infoBackground, 2) > 100);
    QVERIFY(ToolsFixture::countColor(image, infoBorder, 2) > 20);
    QRect infoBox;
    for (int y = 0; y < image.height(); ++y)
    {
        for (int x = 0; x < image.width(); ++x)
        {
            const QColor pixel = image.pixelColor(x, y);
            if (pixel.alpha() == 255 && pixel.rgb() == infoBackground.rgb())
            {
                infoBox = infoBox.united(QRect(x, y, 1, 1));
            }
        }
    }
    QVERIFY(infoBox.isValid());
    const QColor shadowBelow = image.pixelColor(infoBox.center().x(), infoBox.bottom() + 4);
    QVERIFY(shadowBelow.alpha() > 40);
    QVERIFY(shadowBelow.red() > 200 && shadowBelow.green() < 60 && shadowBelow.blue() < 60);
    QVERIFY(image.pixelColor(infoBox.center().x(), infoBox.bottom() + 30).alpha() < 8);
    QVERIFY(image.pixelColor(infoBox.center().x(), infoBox.top() - 3).alpha() < image.pixelColor(infoBox.center().x(), infoBox.bottom() + 4).alpha());
    if (!qEnvironmentVariable("PDF4QT_APP_SCREENSHOTS").isEmpty())
    {
        image.save(qEnvironmentVariable("PDF4QT_APP_SCREENSHOTS") + QStringLiteral("/design-selection-drag.png"));
    }
    fixture.mouse(QEvent::MouseButtonRelease, corner + QPoint(30, 30), Qt::LeftButton, Qt::NoButton);
    {
        PDFDocumentDataLoaderDecorator loader(fixture.document.data());
        const QRectF resized = fixture.pageToDevice().mapRect(loader.readRectangle(fixture.entry(square, "Rect"), QRectF())).normalized();
        QVERIFY(AnnotationToolsTestHelper::fuzzyCompare(resized, frame.adjusted(0.0, 0.0, 30.0, 30.0)));
    }
    fixture.annotationKey(Qt::Key_Escape);

    // The rubber band: the fill 8 %
    annotations.clearSelection();
    fixture.moveTo(fixture.device(QPointF(200, 280)), Qt::ControlModifier);
    fixture.mouse(QEvent::MouseButtonPress, fixture.device(QPointF(200, 280)), Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
    fixture.mouse(QEvent::MouseMove, fixture.device(QPointF(290, 230)), Qt::NoButton, Qt::LeftButton, Qt::ControlModifier);
    image = fixture.drawMarkers();
    QVERIFY(qAbs(image.pixelColor(fixture.device(QPointF(245, 255))).alpha() - 20) <= 4);
    QVERIFY(ToolsFixture::countColor(image, rubberBand) > 0);
    QVERIFY(fixture.annotationKey(Qt::Key_Escape));
}

void AnnotationToolsTest::selectTextPassThrough()
{
    TwoSquares squares;
    ToolsFixture fixture(squares.document);
    PDFWidgetAnnotationManager& annotations = fixture.annotations;
    PDFSelectTextTool* textTool = fixture.tools.getSelectTextTool();
    QVERIFY(textTool);
    fixture.tools.setActiveTool(PDFToolManager::SelectTextTool);
    QVERIFY(textTool->isActive());
    QVERIFY(textTool->getTextSelection().isEmpty());

    const QPoint onFirst = fixture.device(QPointF(80, 70));
    const QPoint emptyPlace = fixture.device(QPointF(250, 30));

    // Off (the old applications): the tool takes the press, nothing is selected
    QVERIFY(!textTool->isAnnotationsSelectable());
    fixture.click(onFirst);
    QVERIFY(!annotations.hasSelection());

    // On: the annotation manager gets the press over an annotation
    textTool->setAnnotationsSelectable(true);
    QVERIFY(annotations.isInteractionAt(onFirst, Qt::NoModifier));
    QVERIFY(!annotations.isInteractionAt(emptyPlace, Qt::NoModifier));
    QVERIFY(annotations.isInteractionAt(emptyPlace, Qt::ControlModifier));
    QVERIFY(annotations.isInteractionAt(emptyPlace, Qt::ShiftModifier));
    QCOMPARE(annotations.getSelectableAnnotationsAt(onFirst), std::vector<PDFObjectReference>({ squares.first }));
    QVERIFY(annotations.getSelectableAnnotationsAt(emptyPlace).empty());

    fixture.click(onFirst);
    QVERIFY(annotations.isAnnotationSelected(squares.first));
    QVERIFY(textTool->isActive());

    // A handle of the selection belongs to the manager
    const QRectF frame = fixture.pageToDevice().mapRect(QRectF(50, 50, 60, 40)).normalized();
    QVERIFY(annotations.isInteractionAt(frame.bottomRight().toPoint(), Qt::NoModifier));

    // The keys of the annotation manager work while the text tool is active
    fixture.key(Qt::Key_Right);
    QCOMPARE(fixture.modificationCount, 1);
    QVERIFY(textTool->isActive());

    // Escape clears the annotation selection, the text tool stays active
    fixture.key(Qt::Key_Escape);
    QVERIFY(!annotations.hasSelection());
    QVERIFY(textTool->isActive());

    // A press elsewhere starts a text selection and deselects the annotations
    annotations.setSelectedAnnotations({ squares.second });
    fixture.click(emptyPlace);
    QVERIFY(!annotations.hasSelection());
    QVERIFY(textTool->isActive());

    // Rubber band with Ctrl
    const QPoint bandStart = fixture.device(QPointF(140, 140));
    const QPoint bandEnd = fixture.device(QPointF(200, 200));
    fixture.moveTo(bandStart, Qt::ControlModifier);
    fixture.mouse(QEvent::MouseButtonPress, bandStart, Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
    fixture.mouse(QEvent::MouseMove, bandEnd, Qt::NoButton, Qt::LeftButton, Qt::ControlModifier);
    fixture.mouse(QEvent::MouseButtonRelease, bandEnd, Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
    QVERIFY(annotations.isAnnotationSelected(squares.second));
    QVERIFY(!annotations.isAnnotationSelected(squares.first));

    // Delete through the widget
    fixture.key(Qt::Key_Delete);
    QCOMPARE(fixture.modificationCount, 2);
    const std::vector<PDFObjectReference> remaining = fixture.pageAnnotations();
    QVERIFY(std::find(remaining.cbegin(), remaining.cend(), squares.second) == remaining.cend());
    QVERIFY(textTool->isActive());

    // Off again: Escape turns the tool off (the old applications)
    textTool->setAnnotationsSelectable(false);
    fixture.key(Qt::Key_Escape);
    QVERIFY(!textTool->isActive());
}

void AnnotationToolsTest::actionGroupsOfOldApplications()
{
    // The old applications choose the variants by the actions of the groups - unchanged
    StyleWindowGuard windowGuard(false);

    // The groups outlive the tools (the action manager of the old applications)
    QActionGroup stickyGroup(nullptr);
    QActionGroup stampGroup(nullptr);
    QActionGroup highlightGroup(nullptr);
    QActionGroup linkGroup(nullptr);

    TwoSquares squares;
    ToolsFixture fixture(squares.document);
    PDFDrawWidgetProxy* proxy = fixture.proxy();
    PDFToolManager* tools = &fixture.tools;

    auto addAction = [](QActionGroup* group, int data)
    {
        QAction* action = new QAction(group);
        action->setCheckable(true);
        action->setData(data);
        group->addAction(action);
        return action;
    };

    addAction(&stickyGroup, int(TextAnnotationIcon::Comment));
    QAction* keyAction = addAction(&stickyGroup, int(TextAnnotationIcon::Key));
    PDFCreateStickyNoteTool* sticky = fixture.addTool(new PDFCreateStickyNoteTool(proxy, tools, &stickyGroup, tools));
    keyAction->trigger();
    QVERIFY(sticky->isActive());
    QCOMPARE(sticky->getIcon(), TextAnnotationIcon::Key);
    tools->setActiveTool(nullptr);
    QVERIFY(!keyAction->isChecked());

    QAction* draftAction = addAction(&stampGroup, int(Stamp::Draft));
    QAction* invalidAction = addAction(&stampGroup, 1000);
    PDFCreateStampTool* stamp = fixture.addTool(new PDFCreateStampTool(proxy, tools, &stampGroup, tools));
    draftAction->trigger();
    QVERIFY(stamp->isActive());
    QCOMPARE(stamp->getStamp(), Stamp::Draft);
    invalidAction->trigger();
    QCOMPARE(stamp->getStamp(), Stamp::Draft);
    tools->setActiveTool(nullptr);
    QVERIFY(!draftAction->isChecked() && !invalidAction->isChecked());

    QAction* squigglyAction = addAction(&highlightGroup, int(AnnotationType::Squiggly));
    PDFCreateHighlightTextTool* highlight = fixture.addTool(new PDFCreateHighlightTextTool(proxy, tools, &highlightGroup, tools));
    squigglyAction->trigger();
    QVERIFY(highlight->isActive());
    QCOMPARE(highlight->getAnnotationType(), AnnotationType::Squiggly);
    tools->setActiveTool(nullptr);

    QAction* fitHAction = addAction(&linkGroup, int(DestinationType::FitH));
    fitHAction->setProperty("inheritZoom", true);
    PDFCreateInDocumentHyperlinkTool* pageLink = fixture.addTool(new PDFCreateInDocumentHyperlinkTool(proxy, tools, &linkGroup, tools));
    fitHAction->trigger();
    QVERIFY(pageLink->isActive());
    QCOMPARE(pageLink->getDestinationType(), DestinationType::FitH);
    QVERIFY(pageLink->isInheritZoom());
    tools->setActiveTool(nullptr);
    QCOMPARE(fixture.modificationCount, 0);
}

void AnnotationToolsTest::persistentCreationTools()
{
    using Request = PDFAnnotationCreationRequest;

    StyleWindowGuard windowGuard(false);
    DialogWatcher watcher;

    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    ToolsFixture fixture(builder.build());
    PDFDrawWidgetProxy* proxy = fixture.proxy();
    PDFToolManager* tools = &fixture.tools;
    QSignalSpy activeToolChanges(tools, &PDFToolManager::activeToolChanged);

    // Off (the old applications): the tool is turned off after one annotation
    QVERIFY(!tools->isCreationToolsPersistent());
    PDFCreateEllipseTool* ellipse = fixture.addTool(new PDFCreateEllipseTool(proxy, tools, nullptr, tools));
    tools->setActiveTool(ellipse);
    fixture.click(fixture.device(QPointF(20, 20)));
    fixture.click(fixture.device(QPointF(60, 50)));
    QCOMPARE(fixture.modificationCount, 1);
    QVERIFY(!ellipse->isActive());

    // On: the ellipse stays active, a second ellipse is created, Escape turns it off
    tools->setCreationToolsPersistent(true);
    QVERIFY(tools->isCreationToolsPersistent());
    tools->setActiveTool(ellipse);
    activeToolChanges.clear();
    fixture.click(fixture.device(QPointF(20, 80)));
    fixture.click(fixture.device(QPointF(60, 110)));
    QCOMPARE(fixture.modificationCount, 2);
    QVERIFY(ellipse->isActive());
    fixture.click(fixture.device(QPointF(20, 140)));
    fixture.click(fixture.device(QPointF(60, 170)));
    QCOMPARE(fixture.modificationCount, 3);
    QVERIFY(ellipse->isActive());
    QCOMPARE(activeToolChanges.count(), 0);
    fixture.key(Qt::Key_Escape);
    QVERIFY(!ellipse->isActive());
    QCOMPARE(fixture.pageAnnotations().size(), size_t(3));

    // Line - the pick tool starts again (the second line has its own two points)
    PDFCreateLineTypeTool* line = fixture.addTool(new PDFCreateLineTypeTool(proxy, tools, PDFCreateLineTypeTool::Type::Line, nullptr, tools));
    tools->setActiveTool(line);
    fixture.click(fixture.device(QPointF(100, 20)));
    fixture.click(fixture.device(QPointF(140, 20)));
    QCOMPARE(fixture.modificationCount, 4);
    QVERIFY(line->isActive());
    fixture.click(fixture.device(QPointF(100, 60)));
    QCOMPARE(fixture.modificationCount, 4);
    fixture.click(fixture.device(QPointF(140, 60)));
    QCOMPARE(fixture.modificationCount, 5);
    QVERIFY(line->isActive());

    // Rectangle
    PDFCreateLineTypeTool* rectangle = fixture.addTool(new PDFCreateLineTypeTool(proxy, tools, PDFCreateLineTypeTool::Type::Rectangle, nullptr, tools));
    tools->setActiveTool(rectangle);
    fixture.click(fixture.device(QPointF(100, 100)));
    fixture.click(fixture.device(QPointF(130, 120)));
    QCOMPARE(fixture.modificationCount, 6);
    QVERIFY(rectangle->isActive());

    // Freehand curve - two strokes
    PDFCreateFreehandCurveTool* freehand = fixture.addTool(new PDFCreateFreehandCurveTool(proxy, tools, nullptr, tools));
    tools->setActiveTool(freehand);
    for (const qreal y : { 200.0, 240.0 })
    {
        fixture.moveTo(fixture.device(QPointF(160, y)));
        fixture.mouse(QEvent::MouseButtonPress, fixture.device(QPointF(160, y)), Qt::LeftButton, Qt::LeftButton);
        fixture.mouse(QEvent::MouseMove, fixture.device(QPointF(180, y + 5)), Qt::NoButton, Qt::LeftButton);
        fixture.mouse(QEvent::MouseMove, fixture.device(QPointF(200, y + 10)), Qt::NoButton, Qt::LeftButton);
        fixture.mouse(QEvent::MouseButtonRelease, fixture.device(QPointF(220, y + 15)), Qt::LeftButton, Qt::NoButton);
    }
    QCOMPARE(fixture.modificationCount, 8);
    QVERIFY(freehand->isActive());

    // Stamp
    PDFCreateStampTool* stamp = fixture.addTool(new PDFCreateStampTool(proxy, tools, nullptr, tools));
    stamp->setStamp(Stamp::Draft);
    tools->setActiveTool(stamp);
    fixture.click(fixture.device(QPointF(250, 30)));
    fixture.click(fixture.device(QPointF(250, 130)));
    QCOMPARE(fixture.modificationCount, 10);
    QVERIFY(stamp->isActive());
    tools->setActiveTool(nullptr);

    // Link to a page: the link rectangle, the target page, the tool is active again for the next link
    PDFCreateInDocumentHyperlinkTool* pageLink = fixture.addTool(new PDFCreateInDocumentHyperlinkTool(proxy, tools, nullptr, tools));
    pageLink->setDestinationType(DestinationType::Fit);
    tools->setActiveTool(pageLink);
    for (int i = 0; i < 2; ++i)
    {
        const qreal x = 20.0 + i * 50.0;
        fixture.click(fixture.device(QPointF(x, 260)));
        fixture.click(fixture.device(QPointF(x + 30, 280)));
        QCOMPARE(tools->getActiveTool(), tools->getPredefinedTool(PDFToolManager::PickPageTool));

        // The pick tool updates its point only after the mouse has moved (another place each time)
        fixture.click(fixture.device(QPointF(150 + i * 20, 150)));
        QCOMPARE(fixture.modificationCount, 11 + i);
        QCOMPARE(tools->getActiveTool(), pageLink);
    }

    // An empty target rectangle (FitR) creates nothing, the tool stays for the next link
    pageLink->setDestinationType(DestinationType::FitR);
    fixture.click(fixture.device(QPointF(200, 260)));
    fixture.click(fixture.device(QPointF(230, 280)));
    QCOMPARE(tools->getActiveTool(), tools->getPredefinedTool(PDFToolManager::PickRectangleTool));
    fixture.click(fixture.device(QPointF(150, 150)));
    fixture.click(fixture.device(QPointF(150, 150)));
    QCOMPARE(fixture.modificationCount, 12);
    QCOMPARE(tools->getActiveTool(), pageLink);
    fixture.key(Qt::Key_Escape);
    QVERIFY(!tools->getActiveTool());

    // Off again: the link to a page ends after its target (the old applications)
    tools->setCreationToolsPersistent(false);
    pageLink->setDestinationType(DestinationType::Fit);
    tools->setActiveTool(pageLink);
    fixture.click(fixture.device(QPointF(20, 230)));
    fixture.click(fixture.device(QPointF(50, 250)));
    fixture.click(fixture.device(QPointF(150, 150)));
    QCOMPARE(fixture.modificationCount, 13);
    QVERIFY(!tools->getActiveTool());
    tools->setCreationToolsPersistent(true);

    // Delegated creation: each place is one request, the tool stays active
    tools->setUserInterfaceDelegated(true);
    QSignalSpy requests(tools, &PDFToolManager::annotationCreationRequested);

    PDFCreateStickyNoteTool* sticky = fixture.addTool(new PDFCreateStickyNoteTool(proxy, tools, nullptr, tools));
    tools->setActiveTool(sticky);
    fixture.click(fixture.device(QPointF(280, 200)));
    fixture.click(fixture.device(QPointF(280, 260)));
    QCOMPARE(requests.count(), 2);
    QVERIFY(sticky->isActive());
    QCOMPARE(requests.at(1).at(0).value<Request>().kind, Request::Kind::StickyNote);
    QCOMPARE(requests.at(1).at(0).value<Request>().pageReference, page);
    QVERIFY(AnnotationToolsTestHelper::fuzzyCompare(requests.at(1).at(0).value<Request>().point, QPointF(280, 260)));

    PDFCreateFreeTextTool* freeText = fixture.addTool(new PDFCreateFreeTextTool(proxy, tools, nullptr, tools));
    tools->setActiveTool(freeText);
    fixture.click(fixture.device(QPointF(10, 180)));
    fixture.click(fixture.device(QPointF(40, 195)));
    fixture.click(fixture.device(QPointF(10, 200)));
    fixture.click(fixture.device(QPointF(40, 215)));
    QCOMPARE(requests.count(), 4);
    QVERIFY(freeText->isActive());
    QVERIFY(AnnotationToolsTestHelper::fuzzyCompare(requests.at(3).at(0).value<Request>().rectangle, QRectF(QPointF(10, 200), QPointF(40, 215))));

    PDFCreateHyperlinkTool* hyperlink = fixture.addTool(new PDFCreateHyperlinkTool(proxy, tools, nullptr, tools));
    tools->setActiveTool(hyperlink);
    fixture.click(fixture.device(QPointF(60, 180)));
    fixture.click(fixture.device(QPointF(90, 195)));
    fixture.click(fixture.device(QPointF(60, 200)));
    fixture.click(fixture.device(QPointF(90, 215)));
    QCOMPARE(requests.count(), 6);
    QVERIFY(hyperlink->isActive());

    // A receiver deleting the tool from its request - the tool is not touched any more
    QObject::connect(tools, &PDFToolManager::annotationCreationRequested, hyperlink, &QObject::deleteLater);
    QPointer<PDFCreateHyperlinkTool> hyperlinkGuard(hyperlink);
    fixture.click(fixture.device(QPointF(60, 230)));
    fixture.click(fixture.device(QPointF(90, 245)));
    QCOMPARE(requests.count(), 7);
    QTRY_VERIFY(hyperlinkGuard.isNull());
    QVERIFY(!tools->getActiveTool());

    // A receiver deleting the tool later from the modification of the document (no delegation)
    tools->setUserInterfaceDelegated(false);
    QObject::connect(tools, &PDFToolManager::documentModified, stamp, &QObject::deleteLater);
    QPointer<PDFCreateStampTool> stampGuard(stamp);
    tools->setActiveTool(stamp);
    fixture.click(fixture.device(QPointF(250, 200)));
    QCOMPARE(fixture.modificationCount, 14);
    QTRY_VERIFY(stampGuard.isNull());
    QVERIFY(!tools->getActiveTool());
    fixture.target()->grab();

    QCOMPARE(watcher.dialogs, 0);
    QCOMPARE(watcher.popups, 0);
}

void AnnotationToolsTest::toolDestroyedWhileActive()
{
    StyleWindowGuard windowGuard(false);
    TwoSquares squares;
    ToolsFixture fixture(squares.document);
    PDFToolManager* tools = &fixture.tools;
    QSignalSpy activeToolChanges(tools, &PDFToolManager::activeToolChanged);

    PDFCreateEllipseTool* ellipse = fixture.addTool(new PDFCreateEllipseTool(fixture.proxy(), tools, nullptr, tools));
    tools->setActiveTool(ellipse);
    QCOMPARE(activeToolChanges.count(), 1);
    fixture.click(fixture.device(QPointF(220, 20)));

    // The application deletes the active tool - the listeners learn, that no tool is
    // active, the proxy does not draw it any more (valgrind), the input goes on
    delete ellipse;
    QVERIFY(!tools->getActiveTool());
    QCOMPARE(activeToolChanges.count(), 2);
    QVERIFY(!activeToolChanges.last().at(0).value<PDFWidgetTool*>());
    fixture.target()->grab();
    fixture.key(Qt::Key_Escape);
    fixture.click(fixture.device(QPointF(80, 70)));
    QVERIFY(fixture.annotations.isAnnotationSelected(squares.first));

    // An inactive tool deleted - no notification
    PDFCreateStampTool* stamp = fixture.addTool(new PDFCreateStampTool(fixture.proxy(), tools, nullptr, tools));
    delete stamp;
    QCOMPARE(activeToolChanges.count(), 2);
    tools->setActiveTool(PDFToolManager::SelectTextTool);
    QCOMPARE(activeToolChanges.count(), 3);
}

void AnnotationToolsTest::reviewReadOnlyMultiSelection()
{
    // Several selected annotations of a read only manager have no handles (the layout of
    // the handles takes only the transformable annotations - none of a read only manager)
    TwoSquares squares;
    ToolsFixture fixture(squares.document);
    PDFWidgetAnnotationManager& annotations = fixture.annotations;
    annotations.setSelectedAnnotations({ squares.first, squares.second });
    const int editablePixels = ToolsFixture::countDrawn(fixture.drawMarkers());

    annotations.setReadOnly(true);
    QCOMPARE(annotations.getSelectedAnnotations().size(), size_t(2));
    const int readOnlyPixels = ToolsFixture::countDrawn(fixture.drawMarkers());
    QVERIFY(readOnlyPixels > 0);
    QVERIFY(readOnlyPixels < editablePixels);

    // The handle at the corner of the frame outside of the annotations is no interaction
    const QPoint rotationArea = fixture.device(QPointF(50, 190));
    annotations.setReadOnly(false);
    const bool editableInteraction = annotations.isInteractionAt(rotationArea, Qt::NoModifier);
    annotations.setReadOnly(true);
    QVERIFY(!annotations.isInteractionAt(rotationArea, Qt::NoModifier));
    QVERIFY(editableInteraction);
}

void AnnotationToolsTest::reviewHiddenLeavesSelection()
{
    TwoSquares squares;
    ToolsFixture fixture(squares.document);
    PDFWidgetAnnotationManager& annotations = fixture.annotations;
    annotations.setSelectedAnnotations({ squares.first, squares.second });
    QSignalSpy selectionChanges(&annotations, &PDFWidgetAnnotationManager::selectionChanged);

    // A hidden annotation leaves the selection, the other one stays
    QVERIFY(annotations.setAnnotationsHidden({ squares.first }, true));
    QCOMPARE(annotations.getSelectedAnnotations(), std::vector<PDFObjectReference>({ squares.second }));
    QVERIFY(selectionChanges.count() > 0);

    // The keys do not move the hidden annotation
    annotations.setSelectedAnnotations({ squares.second });
    QVERIFY(annotations.setAnnotationsHidden({ squares.second }, true));
    QVERIFY(!annotations.hasSelection());
    fixture.annotationKey(Qt::Key_Right);
    QCOMPARE(fixture.modificationCount, 2);
}

void AnnotationToolsTest::reviewAnnotationsOutsideOfView()
{
    // An annotation without the entry P on a page, which is not displayed (a list of all
    // annotations - the Notes), is hidden and deleted too
    PDFDocumentBuilder builder;
    std::vector<PDFObjectReference> pages;
    for (int i = 0; i < 6; ++i)
    {
        pages.push_back(builder.appendPage(QRectF(0, 0, 300, 3000)));
    }
    const PDFObjectReference first = builder.createAnnotationSquare(pages.back(), QRectF(50, 50, 60, 40), 1.0, Qt::yellow, Qt::black, "First", "Subject", "Contents");
    const PDFObjectReference second = builder.createAnnotationSquare(pages.back(), QRectF(150, 50, 60, 40), 1.0, Qt::yellow, Qt::black, "Second", "Subject", "Contents");
    for (const PDFObjectReference& annotation : { first, second })
    {
        PDFObjectFactory factory;
        factory.beginDictionary();
        factory.beginDictionaryItem("P");
        factory << PDFObject();
        factory.endDictionaryItem();
        factory.endDictionary();
        builder.mergeTo(annotation, factory.takeObject());
    }
    ToolsFixture fixture(builder.build());
    const std::vector<PDFInteger> currentPages = fixture.widget.getDrawWidget()->getCurrentPages();
    QVERIFY(std::find(currentPages.cbegin(), currentPages.cend(), PDFInteger(5)) == currentPages.cend());

    QVERIFY(fixture.annotations.setAnnotationsHidden({ first }, true));
    QCOMPARE(fixture.modificationCount, 1);
    QVERIFY(fixture.flags(first).testFlag(PDFAnnotation::Hidden));

    QVERIFY(fixture.annotations.deleteAnnotations({ first, second }));
    QCOMPARE(fixture.modificationCount, 2);
    QVERIFY(fixture.document->getCatalog()->getPage(5)->getAnnotations().empty());
}

void AnnotationToolsTest::noteIconIsMemo()
{
    // The icon Note of a text annotation without an appearance stream is a memo (U+1F4DD),
    // not a musical note (owner decision O20); the other icons are unchanged
    const QString memo = QString::fromUtf16(u"\U0001F4DD");
    QCOMPARE(PDFTextAnnotation::getTextForIcon("Note"), memo);
    QVERIFY(PDFTextAnnotation::getTextForIcon("Note") != QString::fromUtf16(u"♪"));
    QCOMPARE(PDFTextAnnotation::getTextForIcon("Comment"), QString::fromUtf16(u"\U0001F4AC"));
    QCOMPARE(PDFTextAnnotation::getTextForIcon("Key"), QString::fromUtf16(u"\U0001F511"));
    QCOMPARE(PDFTextAnnotation::getTextForIcon("Paragraph"), QString::fromUtf16(u"¶"));
    QCOMPARE(PDFTextAnnotation::getTextForIcon("Unknown"), QString("?"));

    // A /Note annotation without an appearance stream is drawn: the circle and the symbol
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 200, 200));
    const PDFObjectReference note = builder.createAnnotationText(page, QRectF(50, 50, 0, 0), TextAnnotationIcon::Note, "Note", "Subject", "Contents", false);
    PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("AP");
    factory << PDFObject();
    factory.endDictionaryItem();
    factory.endDictionary();
    builder.mergeTo(note, factory.takeObject());
    const PDFDocument document = builder.build();

    PDFAnnotationPtr annotation = PDFAnnotation::parse(&document.getStorage(), note);
    QVERIFY(annotation);
    QCOMPARE(annotation->getType(), AnnotationType::Text);
    QCOMPARE(static_cast<const PDFTextAnnotation*>(annotation.data())->getIconName(), QByteArray("Note"));

    QImage image(200, 200, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QFont font;
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        AnnotationDrawParameters parameters;
        parameters.painter = &painter;
        parameters.annotation = annotation.data();
        parameters.key.first = PDFAppeareanceStreams::Appearance::Normal;
        annotation->draw(parameters);
        font = painter.font();
        QVERIFY(parameters.boundingRectangle.isValid());
    }

    const QRectF rectangle = annotation->getRectangle();
    const QPointF center = QRectF(rectangle.topLeft(), QSizeF(32, 32)).center();
    QVERIFY(ToolsFixture::countColor(image, QColor(Qt::yellow), 40) > 100);

    // The symbol inside of the circle, when a font of the platform has the memo
    font.setPixelSize(16);
    if (QFontMetricsF(font).inFontUcs4(0x1F4DD))
    {
        int symbolPixels = 0;
        for (int y = 0; y < image.height(); ++y)
        {
            for (int x = 0; x < image.width(); ++x)
            {
                const QColor pixel = image.pixelColor(x, y);
                if (QLineF(center, QPointF(x + 0.5, y + 0.5)).length() < 10.0 && pixel.alpha() > 100 && pixel.red() < 80 && pixel.green() < 80)
                {
                    ++symbolPixels;
                }
            }
        }
        QVERIFY(symbolPixels > 0);
    }
    else
    {
        qInfo("No font of the platform has U+1F4DD - the symbol is not checked");
    }
}

void AnnotationToolsTest::reviewLinksSelectable()
{
    // Page 300 x 300: a web link (50, 50, 60, 40), a square (150, 150, 40, 40)
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference link = builder.createAnnotationLink(page, QRectF(50, 50, 60, 40), QStringLiteral("https://example.com/"), LinkHighlightMode::Invert);
    const PDFObjectReference square = builder.createAnnotationSquare(page, QRectF(150, 150, 40, 40), 1.0, Qt::yellow, Qt::black, "Square", "Subject", "Contents");
    ToolsFixture fixture(builder.build());
    PDFWidgetAnnotationManager& annotations = fixture.annotations;
    annotations.setUserInterfaceDelegated(true);
    PDFSelectTextTool* textTool = fixture.tools.getSelectTextTool();
    QVERIFY(textTool);
    textTool->setLinksClickable(true);
    textTool->setAnnotationsSelectable(true);
    fixture.tools.setActiveTool(PDFToolManager::SelectTextTool);

    QSignalSpy followed(&annotations, &PDFWidgetAnnotationManager::actionTriggered);
    QSignalSpy menus(&annotations, &PDFWidgetAnnotationManager::menuRequested);
    QSignalSpy properties(&annotations, &PDFWidgetAnnotationManager::propertiesRequested);
    const QPoint onLink = fixture.device(QPointF(80, 70));

    // Off (the old applications): a link is never selected, a click (also with Ctrl) follows it
    QVERIFY(!annotations.isLinksSelectable());
    QVERIFY(annotations.getSelectableAnnotationsAt(onLink).empty());
    fixture.click(onLink, Qt::ControlModifier);
    QVERIFY(!annotations.hasSelection());
    fixture.click(onLink);
    QCOMPARE(followed.count(), 2);
    followed.clear();

    // On: the link is selectable, a plain click still follows it and does not select it
    annotations.setLinksSelectable(true);
    QVERIFY(annotations.isLinksSelectable());
    QCOMPARE(annotations.getSelectableAnnotationsAt(onLink), std::vector<PDFObjectReference>({ link }));

    // A plain press on the link is not an interaction of the selection: the text tool
    // decides by setLinksClickable - off (step 3), it takes the press, nothing is followed
    QVERIFY(!annotations.isInteractionAt(onLink, Qt::NoModifier));
    QVERIFY(annotations.isInteractionAt(onLink, Qt::ControlModifier));
    QVERIFY(annotations.isInteractionAt(onLink, Qt::AltModifier));
    textTool->setLinksClickable(false);
    fixture.click(onLink);
    QCOMPARE(followed.count(), 0);
    QVERIFY(!annotations.hasSelection());
    textTool->setLinksClickable(true);

    fixture.click(onLink);
    QCOMPARE(followed.count(), 1);
    QVERIFY(!annotations.hasSelection());
    QVERIFY(textTool->isActive());

    // The link under the cursor is not outlined (it is a link to follow), with Ctrl it is
    annotations.clearSelection();
    fixture.moveTo(onLink);
    QCOMPARE(ToolsFixture::countDrawn(fixture.drawMarkers()), 0);
    fixture.moveTo(onLink, Qt::ControlModifier);
    QVERIFY(ToolsFixture::countDrawn(fixture.drawMarkers()) > 0);

    // Ctrl + click selects the link; a click on the selected link keeps it (no follow)
    fixture.click(onLink, Qt::ControlModifier);
    QCOMPARE(annotations.getSelectedAnnotations(), std::vector<PDFObjectReference>({ link }));
    fixture.click(onLink);
    QVERIFY(annotations.isAnnotationSelected(link));
    QCOMPARE(followed.count(), 1);
    QCOMPARE(fixture.modificationCount, 0);

    // Its properties by the keyboard (the link properties of the application)
    QVERIFY(fixture.annotationKey(Qt::Key_Return));
    QCOMPARE(properties.count(), 1);
    QCOMPARE(properties.last().at(0).value<PDFObjectReference>(), link);

    // A right click on a link, which is not selected, selects it and asks for its menu
    annotations.clearSelection();
    fixture.rightClick(onLink);
    QVERIFY(annotations.isAnnotationSelected(link));
    QVERIFY(!menus.isEmpty());
    const PDFAnnotationMenuRequest request = menus.last().at(0).value<PDFAnnotationMenuRequest>();
    QCOMPARE(request.kind, PDFAnnotationMenuRequest::Kind::Selection);
    QCOMPARE(request.annotation, link);
    QCOMPARE(followed.count(), 1);

    // Alt + click and the rubber band take links too; the link is deleted as one change
    annotations.clearSelection();
    fixture.click(onLink, Qt::AltModifier);
    QVERIFY(annotations.isAnnotationSelected(link));
    QVERIFY(annotations.deleteAnnotations({ link }));
    QCOMPARE(fixture.modificationCount, 1);
    const std::vector<PDFObjectReference> remaining = fixture.pageAnnotations();
    QVERIFY(std::find(remaining.cbegin(), remaining.cend(), link) == remaining.cend());

    // Off again: a selected link leaves the selection, the square stays
    PDFDocumentBuilder secondBuilder;
    const PDFObjectReference secondPage = secondBuilder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference secondLink = secondBuilder.createAnnotationLink(secondPage, QRectF(50, 50, 60, 40), QStringLiteral("https://example.com/"), LinkHighlightMode::Invert);
    const PDFObjectReference secondSquare = secondBuilder.createAnnotationSquare(secondPage, QRectF(150, 150, 40, 40), 1.0, Qt::yellow, Qt::black, "Square", "Subject", "Contents");
    ToolsFixture secondFixture(secondBuilder.build());
    secondFixture.annotations.setLinksSelectable(true);
    secondFixture.annotations.setSelectedAnnotations({ secondLink, secondSquare });
    QCOMPARE(secondFixture.annotations.getSelectedAnnotations().size(), size_t(2));
    QSignalSpy selectionChanges(&secondFixture.annotations, &PDFWidgetAnnotationManager::selectionChanged);
    secondFixture.annotations.setLinksSelectable(false);
    QCOMPARE(secondFixture.annotations.getSelectedAnnotations(), std::vector<PDFObjectReference>({ secondSquare }));
    QCOMPARE(selectionChanges.count(), 1);
    Q_UNUSED(square);
}

void AnnotationToolsTest::reviewLinksMovable()
{
    // Owner (O20): a selected link is moved and resized by the mouse (its Rect and its
    // QuadPoints); a Locked link is not (ISO 32000-2, Table 167) - no handles, the lock badge
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference link = builder.createAnnotationLink(page, QRectF(50, 50, 60, 40), QStringLiteral("https://example.com/"), LinkHighlightMode::Invert);
    {
        PDFObjectFactory factory;
        factory.beginDictionary();
        factory.beginDictionaryItem("QuadPoints");
        factory << std::vector<PDFReal>{ 55, 55, 105, 55, 105, 85, 55, 85 };
        factory.endDictionaryItem();
        factory.endDictionary();
        builder.mergeTo(link, factory.takeObject());
    }
    const PDFObjectReference locked = builder.createAnnotationLink(page, QRectF(150, 150, 60, 40), QStringLiteral("https://example.com/"), LinkHighlightMode::Invert);
    AnnotationToolsTestHelper::setFlags(builder, locked, PDFAnnotation::Locked);
    ToolsFixture fixture(builder.build());
    PDFWidgetAnnotationManager& annotations = fixture.annotations;
    annotations.setUserInterfaceDelegated(true);
    PDFSelectTextTool* textTool = fixture.tools.getSelectTextTool();
    QVERIFY(textTool);
    textTool->setLinksClickable(true);
    textTool->setAnnotationsSelectable(true);
    fixture.tools.setActiveTool(PDFToolManager::SelectTextTool);
    PDFViewColors colors = PDFViewColors::getDefaultColors();
    colors.annotationHandle = QColor(250, 240, 20);
    colors.lockBadge = QColor(255, 0, 255);
    fixture.proxy()->setViewColors(colors);
    QSignalSpy followed(&annotations, &PDFWidgetAnnotationManager::actionTriggered);

    auto readRectangle = [&fixture](PDFObjectReference annotation)
    {
        PDFDocumentDataLoaderDecorator loader(fixture.document.data());
        return loader.readRectangle(fixture.entry(annotation, "Rect"), QRectF()).normalized();
    };
    auto readQuadPoints = [&fixture](PDFObjectReference annotation)
    {
        PDFDocumentDataLoaderDecorator loader(fixture.document.data());
        return loader.readNumberArray(fixture.entry(annotation, "QuadPoints"));
    };
    auto drag = [&fixture](QPoint from, QPoint to)
    {
        fixture.moveTo(from);
        fixture.mouse(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
        fixture.mouse(QEvent::MouseMove, (from + to) / 2, Qt::NoButton, Qt::LeftButton);
        fixture.mouse(QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
        fixture.mouse(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
    };

    // Off: a drag on a link, which is not selectable, changes nothing
    const QPoint onLink = fixture.device(QPointF(80, 70));
    drag(onLink, onLink + QPoint(40, 0));
    QCOMPARE(fixture.modificationCount, 0);
    annotations.setLinksSelectable(true);

    // Selected (Ctrl + click), it has the handles of a box and it is moved by a drag
    // (the drag and drop of the annotations) - not followed - with its areas
    fixture.click(onLink, Qt::ControlModifier);
    QVERIFY(annotations.isAnnotationSelected(link));
    const int handlePixels = ToolsFixture::countColor(fixture.drawMarkers(), colors.annotationHandle);
    QVERIFY(handlePixels > 100);
    fixture.mouse(QEvent::MouseButtonPress, onLink, Qt::LeftButton, Qt::LeftButton);
    std::unique_ptr<QMimeData> dragData(annotations.createAnnotationDragData());
    QVERIFY(dragData);
    QVERIFY(annotations.handleAnnotationDrop(dragData.get(), onLink + QPoint(40, -20), Qt::MoveAction, false));
    fixture.mouse(QEvent::MouseButtonRelease, onLink + QPoint(40, -20), Qt::LeftButton, Qt::NoButton);
    QCOMPARE(fixture.modificationCount, 1);
    QCOMPARE(followed.count(), 0);
    QRectF moved = readRectangle(link);
    QVERIFY(qAbs(moved.width() - 60.0) < 0.01 && qAbs(moved.height() - 40.0) < 0.01);
    QVERIFY(moved.left() > 55.0 && moved.top() > 52.0);
    std::vector<PDFReal> quadPoints = readQuadPoints(link);
    QCOMPARE(quadPoints.size(), size_t(8));
    QVERIFY(qAbs(quadPoints[0] - (moved.left() + 5.0)) < 0.01);
    QVERIFY(qAbs(quadPoints[1] - (moved.top() + 5.0)) < 0.01);
    QVERIFY(annotations.isAnnotationSelected(link));

    // Resized by the bottom right handle: the areas are scaled with the rectangle and stay inside of it
    const QRectF frame = fixture.pageToDevice().mapRect(moved).normalized();
    const QPoint corner = frame.bottomRight().toPoint();
    QVERIFY(annotations.isInteractionAt(corner, Qt::NoModifier));
    drag(corner, corner + QPoint(30, 20));
    QCOMPARE(fixture.modificationCount, 2);
    const QRectF resized = readRectangle(link);
    QVERIFY2(resized.width() > 65.0 && resized.height() > 42.0, qPrintable(QString("%1 x %2").arg(resized.width()).arg(resized.height())));
    quadPoints = readQuadPoints(link);
    QCOMPARE(quadPoints.size(), size_t(8));
    for (size_t i = 0; i < quadPoints.size(); i += 2)
    {
        QVERIFY(resized.adjusted(-0.01, -0.01, 0.01, 0.01).contains(QPointF(quadPoints[i], quadPoints[i + 1])));
    }
    QVERIFY(quadPoints[2] - quadPoints[0] > 50.0 + 0.5);

    // Locked: selected with the lock badge and without handles, a drag does not move it
    annotations.clearSelection();
    const QPoint onLocked = fixture.device(QPointF(180, 170));
    fixture.click(onLocked, Qt::ControlModifier);
    QVERIFY(annotations.isAnnotationSelected(locked));
    const QImage lockedImage = fixture.drawMarkers();
    QVERIFY(ToolsFixture::countColor(lockedImage, colors.lockBadge) > 0);
    QVERIFY(ToolsFixture::countColor(lockedImage, colors.annotationHandle) < handlePixels / 2); // the padlock glyph only
    const QRectF lockedRectangle = readRectangle(locked);
    fixture.mouse(QEvent::MouseButtonPress, onLocked, Qt::LeftButton, Qt::LeftButton);
    std::unique_ptr<QMimeData> lockedData(annotations.createAnnotationDragData());
    QVERIFY(!lockedData || !annotations.handleAnnotationDrop(lockedData.get(), onLocked + QPoint(40, 20), Qt::MoveAction, false));
    fixture.mouse(QEvent::MouseButtonRelease, onLocked, Qt::LeftButton, Qt::NoButton);
    drag(onLocked, onLocked + QPoint(40, 20));
    const QPoint lockedCorner = fixture.pageToDevice().mapRect(lockedRectangle).normalized().bottomRight().toPoint();
    drag(lockedCorner, lockedCorner + QPoint(30, 20));
    QCOMPARE(fixture.modificationCount, 2);
    QCOMPARE(readRectangle(locked), lockedRectangle);
    QCOMPARE(followed.count(), 0);
}

/// A view, which owns the widget and its managers on the heap (as the document view of the
/// application), so a receiver of their signals can delete it synchronously
class DeletableToolsView : public QWidget
{
public:
    explicit DeletableToolsView(PDFDocument initialDocument) :
        QWidget(nullptr),
        document(new PDFDocument(std::move(initialDocument)))
    {
        widget = new PDFWidget(&cms, RendererEngine::QPainter, this);
        widget->resize(800, 800);
        widget->getDrawWidget()->getWidget()->resize(800, 800);
        annotations = new PDFWidgetAnnotationManager(widget->getDrawWidgetProxy(), this);
        tools = new PDFToolManager(widget->getDrawWidgetProxy(), this);
        widget->setAnnotationManager(annotations);
        widget->setToolManager(tools);
        widget->getDrawWidgetProxy()->setProgress(&progress);
        const PDFModifiedDocument modified(document, nullptr);
        widget->setDocument(modified, {});
        annotations->setDocument(modified);
        tools->setDocument(modified);
    }

    virtual ~DeletableToolsView() override
    {
        // As the view of the application: nothing is emitted any more, the managers are
        // detached from the widget and the document, then they go
        tools->disconnect();
        annotations->disconnect();
        tools->blockSignals(true);
        annotations->blockSignals(true);
        tools->setActiveTool(nullptr);
        tools->setDocument(PDFModifiedDocument());
        annotations->setDocument(PDFModifiedDocument());
        widget->setDocument(PDFModifiedDocument(), {});
        widget->setAnnotationManager(nullptr);
        widget->setToolManager(nullptr);
        delete tools;
        delete annotations;
        delete widget;
    }

    QPoint device(const QPointF& pagePoint) const
    {
        const PDFWidgetSnapshot snapshot = widget->getDrawWidgetProxy()->getSnapshot();
        return snapshot.items.empty() ? QPoint() : snapshot.items.front().pageToDeviceMatrix.map(pagePoint).toPoint();
    }

    PDFDocumentPointer document;
    PDFCMSManager cms{ nullptr };
    PDFProgress progress{ nullptr };
    PDFWidget* widget = nullptr;
    PDFWidgetAnnotationManager* annotations = nullptr;
    PDFToolManager* tools = nullptr;
};

void AnnotationToolsTest::reviewOwnerDeletedFromEmissions()
{
    // The application commits a change from the signals and the callbacks of the engine
    // and that can delete the tab (with the view, the widget and the managers) at once:
    // the engine uses nothing after such an emission (review RT4: the pick callback in
    // PDFPickTool::mousePressEvent, activeToolChanged, pasteAnnotations)
    auto createDocument = []()
    {
        PDFDocumentBuilder builder;
        const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
        builder.createAnnotationSquare(page, QRectF(50, 50, 60, 40), 1.0, Qt::yellow, Qt::black, "Square", "Subject", "Contents");
        return builder.build();
    };
    auto click = [](QPointer<DeletableToolsView> view, QPoint position)
    {
        QPointer<QWidget> target = view->widget->getDrawWidget()->getWidget();
        QMouseEvent move(QEvent::MouseMove, position, target->mapToGlobal(position), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(target, &move);
        QMouseEvent press(QEvent::MouseButtonPress, position, target->mapToGlobal(position), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(target, &press);
        if (target)
        {
            QMouseEvent release(QEvent::MouseButtonRelease, position, target->mapToGlobal(position), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(target, &release);
        }
    };

    // The callback of the pick of a rectangle deletes the view
    {
        QPointer<DeletableToolsView> view = new DeletableToolsView(createDocument());
        const QPoint first = view->device(QPointF(150, 150));
        const QPoint second = view->device(QPointF(200, 200));
        bool isCalled = false;
        view->tools->pickRectangle([view, &isCalled](PDFInteger, QRectF) { isCalled = true; delete view.data(); });
        click(view, first);
        QVERIFY(view);
        click(view, second);
        QVERIFY(isCalled);
        QVERIFY(!view);
    }

    // The receiver of activeToolChanged deletes the view, when the rectangle tool ends
    // after its annotation (the old one-shot behavior)
    {
        QPointer<DeletableToolsView> view = new DeletableToolsView(createDocument());
        PDFCreateLineTypeTool* rectangle = new PDFCreateLineTypeTool(view->widget->getDrawWidgetProxy(), view->tools, PDFCreateLineTypeTool::Type::Rectangle, nullptr, view->tools);
        view->tools->addTool(rectangle);
        rectangle->setDocument(PDFModifiedDocument(view->document, nullptr));
        view->tools->setActiveTool(rectangle);
        int modifications = 0;
        QObject::connect(view->tools, &PDFToolManager::documentModified, view->tools, [&modifications](const PDFModifiedDocument&) { ++modifications; });
        QObject::connect(view->tools, &PDFToolManager::activeToolChanged, view->tools, [view](PDFWidgetTool* tool) { if (!tool) { delete view.data(); } });
        click(view, view->device(QPointF(150, 150)));
        QVERIFY(view);
        click(view, view->device(QPointF(200, 200)));
        QCOMPARE(modifications, 1);
        QVERIFY(!view);
    }

    // The receiver of documentModified deletes the view during pasteAnnotations
    {
        QPointer<DeletableToolsView> view = new DeletableToolsView(createDocument());
        const PDFObjectReference square = view->document->getCatalog()->getPage(0)->getAnnotations().front();
        view->annotations->setSelectedAnnotations({ square });
        view->annotations->copySelectedAnnotations();
        QVERIFY(view->annotations->canPasteAnnotations());
        QObject::connect(view->annotations, &PDFWidgetAnnotationManager::documentModified, view->annotations, [view](const PDFModifiedDocument&) { delete view.data(); });
        view->annotations->pasteAnnotations(view->device(QPointF(200, 200)));
        QVERIFY(!view);
    }
}

void AnnotationToolsTest::reviewHighlightedAnnotation()
{
    // The application outlines an annotation of its chooser of overlapping annotations
    TwoSquares squares;
    ToolsFixture fixture(squares.document);
    PDFWidgetAnnotationManager& annotations = fixture.annotations;
    PDFViewColors colors = PDFViewColors::getDefaultColors();
    colors.annotationSelection = QColor(10, 200, 30);
    fixture.proxy()->setViewColors(colors);

    QCOMPARE(ToolsFixture::countDrawn(fixture.drawMarkers()), 0);
    annotations.setHighlightedAnnotation(squares.first);
    QCOMPARE(annotations.getHighlightedAnnotation(), squares.first);
    const QImage image = fixture.drawMarkers();
    QVERIFY(ToolsFixture::countColor(image, colors.annotationSelection) > 100);

    // Only the outline: the inside of the annotation is not filled
    const QPoint inside = fixture.device(QPointF(80, 70));
    QCOMPARE(qAlpha(image.pixel(inside)), 0);

    // A selected annotation is not outlined twice, an invalid reference removes it
    annotations.setSelectedAnnotations({ squares.first });
    const int selectedPixels = ToolsFixture::countDrawn(fixture.drawMarkers());
    annotations.setHighlightedAnnotation(PDFObjectReference());
    QCOMPARE(ToolsFixture::countDrawn(fixture.drawMarkers()), selectedPixels);
    annotations.clearSelection();
    QCOMPARE(ToolsFixture::countDrawn(fixture.drawMarkers()), 0);

    // A reference of no annotation of the document draws nothing
    annotations.setHighlightedAnnotation(PDFObjectReference(100000, 0));
    QCOMPARE(ToolsFixture::countDrawn(fixture.drawMarkers()), 0);
}

void AnnotationToolsTest::reviewPickTargetToolDeleted()
{
    // The link to a page picks its target with the pick tool of the tool manager; the
    // application deletes the link tool meanwhile - the picked target creates nothing
    StyleWindowGuard windowGuard(false);
    TwoSquares squares;
    ToolsFixture fixture(squares.document);
    PDFToolManager* tools = &fixture.tools;
    tools->setCreationToolsPersistent(true);

    for (const DestinationType type : { DestinationType::Fit, DestinationType::FitR })
    {
        QPointer<PDFCreateInDocumentHyperlinkTool> pageLink = fixture.addTool(new PDFCreateInDocumentHyperlinkTool(fixture.proxy(), tools, nullptr, tools));
        pageLink->setDestinationType(type);
        tools->setActiveTool(pageLink.data());
        fixture.click(fixture.device(QPointF(20, 230)));
        fixture.click(fixture.device(QPointF(50, 250)));
        const PDFToolManager::PredefinedTools pickTool = type == DestinationType::Fit ? PDFToolManager::PickPageTool : PDFToolManager::PickRectangleTool;
        QCOMPARE(tools->getActiveTool(), tools->getPredefinedTool(pickTool));

        delete pageLink.data();
        QVERIFY(!pageLink);
        fixture.click(fixture.device(QPointF(150, 30)));
        fixture.click(fixture.device(QPointF(200, 80)));
        QCOMPARE(fixture.modificationCount, 0);
        tools->setActiveTool(nullptr);
    }
}

void AnnotationToolsTest::reviewSpecificationShapeTypes()
{
    // Owner (O20): the new application creates a Square by the rectangle tool and an Ink
    // by the freehand tool; the old applications keep a Polygon and a PolyLine
    StyleWindowGuard windowGuard(false);
    for (const bool isSpecification : { false, true })
    {
        PDFDocumentBuilder builder;
        builder.appendPage(QRectF(0, 0, 300, 300));
        ToolsFixture fixture(builder.build());
        PDFDrawWidgetProxy* proxy = fixture.proxy();
        PDFToolManager* tools = &fixture.tools;
        QVERIFY(!tools->isSpecificationShapeTypes());
        tools->setSpecificationShapeTypes(isSpecification);
        QCOMPARE(tools->isSpecificationShapeTypes(), isSpecification);

        PDFCreateLineTypeTool* rectangle = fixture.addTool(new PDFCreateLineTypeTool(proxy, tools, PDFCreateLineTypeTool::Type::Rectangle, nullptr, tools));
        rectangle->setPenWidth(4.0);
        rectangle->setStrokeColor(QColor(Qt::blue));
        rectangle->setFillColor(QColor(Qt::yellow));
        tools->setActiveTool(rectangle);
        fixture.click(fixture.device(QPointF(100, 100)));
        fixture.click(fixture.device(QPointF(160, 140)));
        QCOMPARE(fixture.modificationCount, 1);

        PDFCreateFreehandCurveTool* freehand = fixture.addTool(new PDFCreateFreehandCurveTool(proxy, tools, nullptr, tools));
        tools->setActiveTool(freehand);
        fixture.moveTo(fixture.device(QPointF(160, 200)));
        fixture.mouse(QEvent::MouseButtonPress, fixture.device(QPointF(160, 200)), Qt::LeftButton, Qt::LeftButton);
        fixture.mouse(QEvent::MouseMove, fixture.device(QPointF(180, 205)), Qt::NoButton, Qt::LeftButton);
        fixture.mouse(QEvent::MouseMove, fixture.device(QPointF(200, 210)), Qt::NoButton, Qt::LeftButton);
        fixture.mouse(QEvent::MouseButtonRelease, fixture.device(QPointF(220, 215)), Qt::LeftButton, Qt::NoButton);
        QCOMPARE(fixture.modificationCount, 2);
        tools->setActiveTool(nullptr);

        const std::vector<PDFObjectReference> annotations = fixture.pageAnnotations();
        QCOMPARE(annotations.size(), size_t(2));
        const PDFAnnotationPtr shape = PDFAnnotation::parse(&fixture.document->getStorage(), annotations[0]);
        const PDFAnnotationPtr curve = PDFAnnotation::parse(&fixture.document->getStorage(), annotations[1]);
        QVERIFY(shape && curve);
        QCOMPARE(shape->getType(), isSpecification ? AnnotationType::Square : AnnotationType::Polygon);
        QCOMPARE(curve->getType(), isSpecification ? AnnotationType::Ink : AnnotationType::Polyline);
        QVERIFY(fixture.entry(annotations[0], "AP").isDictionary());
        QVERIFY(fixture.entry(annotations[1], "AP").isDictionary());

        if (isSpecification)
        {
            // The Square is the drawn rectangle; resized and moved by the handles (Box)
            const QRectF expected(100, 100, 60, 40);
            QVERIFY(AnnotationToolsTestHelper::fuzzyCompare(shape->getRectangle().normalized(), expected));
            const PDFAnnotationManipulator::Capabilities capabilities = PDFAnnotationManipulator::getCapabilities(shape.data());
            QVERIFY(capabilities.testFlag(PDFAnnotationManipulator::Move));
            QVERIFY(capabilities.testFlag(PDFAnnotationManipulator::Resize));
            QCOMPARE(fixture.entry(annotations[0], "IC").isArray(), true);

            // Its border of the width 4 is drawn whole inside of the rectangle (the
            // appearance stream is not cut by its bounding box)
            QImage image(300, 300, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::white);
            AnnotationDrawParameters parameters;
            {
                QPainter painter(&image);
                parameters.painter = &painter;
                parameters.annotation = shape.data();
                painter.setWorldTransform(QTransform(1, 0, 0, -1, 0, 300));
                shape->draw(parameters);
            }
            QVERIFY(AnnotationToolsTestHelper::fuzzyCompare(parameters.boundingRectangle, expected));
            const QColor border = image.pixelColor(QPoint(101, 300 - 120));
            const QColor outside = image.pixelColor(QPoint(98, 300 - 120));
            const QColor innerEdge = image.pixelColor(QPoint(103, 300 - 120));
            QVERIFY2(border.blue() > 200 && border.red() < 60, qPrintable(border.name()));
            QVERIFY2(innerEdge.blue() > 200 && innerEdge.red() < 60, qPrintable(innerEdge.name()));
            QCOMPARE(outside, QColor(Qt::white));

            // The Ink is one stroke through the points, moved and resized by its points
            const PDFInkAnnotation* ink = dynamic_cast<const PDFInkAnnotation*>(curve.data());
            QVERIFY(ink);
            QVERIFY(ink->getInkPath().elementCount() >= 3);
            QVERIFY(PDFAnnotationManipulator::getCapabilities(curve.data()).testFlag(PDFAnnotationManipulator::Move));
        }
    }
}

void AnnotationToolsTest::reviewInkStrokeSmooth()
{
    // An ink drawn by the freehand tool is a stroke through many close points. Every
    // segment was drawn as a quadratic curve bulging to the same side by an eighth of its
    // length, so the stroke was scalloped at the joins of the segments. The stroke goes
    // through the points smoothly now: points on a straight line give a straight band
    // of the width of the pen, points of a zigzag are all on the stroke, the ends are round.
    constexpr qreal SCALE = 4.0;
    constexpr qreal WIDTH = 2.0;
    QPolygonF straight;
    for (qreal x = 20.0; x <= 280.0; x += 10.0)
    {
        straight << QPointF(x, 150.0);
    }
    QPolygonF zigzag;
    for (int i = 0; i <= 26; ++i)
    {
        zigzag << QPointF(20.0 + i * 10.0, 60.0 + (i % 2 ? 6.0 : -6.0));
    }

    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference straightInk = builder.createAnnotationInk(page, Polygons{ straight }, WIDTH, Qt::black, QString(), QString(), QString());
    const PDFObjectReference zigzagInk = builder.createAnnotationInk(page, Polygons{ zigzag }, WIDTH, Qt::black, QString(), QString(), QString());
    const PDFDocument document = builder.build();

    auto render = [&document](PDFObjectReference reference, QRectF* boundingRectangle)
    {
        QImage image(int(300 * SCALE), int(300 * SCALE), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        const PDFAnnotationPtr annotation = PDFAnnotation::parse(&document.getStorage(), reference);
        if (annotation)
        {
            QPainter painter(&image);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setWorldTransform(QTransform(SCALE, 0, 0, -SCALE, 0, 300 * SCALE));
            AnnotationDrawParameters parameters;
            parameters.painter = &painter;
            parameters.annotation = annotation.data();
            annotation->draw(parameters);
            *boundingRectangle = parameters.boundingRectangle;
        }
        return image;
    };
    auto isDark = [](const QImage& image, int x, int y)
    {
        return x >= 0 && y >= 0 && x < image.width() && y < image.height() && qGray(image.pixel(x, y)) < 128;
    };

    // Straight: every column between the points has a band of the width of the pen at
    // the line, nothing above or below it (the curves bulged 1.25 pt = 5 px)
    QRectF boundingRectangle;
    const QImage straightImage = render(straightInk, &boundingRectangle);
    const int lineY = int(150 * SCALE);
    for (int x = int(25 * SCALE); x < int(275 * SCALE); ++x)
    {
        int painted = 0;
        for (int y = lineY - 20; y <= lineY + 20; ++y)
        {
            if (isDark(straightImage, x, y))
            {
                ++painted;
                QVERIFY2(std::abs(y - lineY) <= int(WIDTH * SCALE / 2.0) + 1, qPrintable(QString("x %1, y %2").arg(x).arg(y)));
            }
        }
        QVERIFY2(painted >= int(WIDTH * SCALE) - 2, qPrintable(QString("x %1").arg(x)));
    }

    // The round end is beyond the last point by a half of the width (a flat cap is not)
    QVERIFY(isDark(straightImage, int((280.0 + WIDTH * 0.25) * SCALE), lineY));
    QVERIFY(boundingRectangle.contains(QRectF(20, 150 - WIDTH / 2.0, 260, WIDTH)));

    // Zigzag: the stroke goes through every point
    const QImage zigzagImage = render(zigzagInk, &boundingRectangle);
    for (const QPointF& point : zigzag)
    {
        QVERIFY2(isDark(zigzagImage, int(point.x() * SCALE), int((300.0 - point.y()) * SCALE)), qPrintable(QString("%1 %2").arg(point.x()).arg(point.y())));
    }
}

void AnnotationToolsTest::reviewInteractionHitTestCost()
{
    // A page with 5000 annotations: every mouse move of the text tool with selectable
    // annotations asks PDFWidgetAnnotationManager::isInteractionAt, then the manager
    // updates its hover - one hit test of the annotations under the cursor is shared by
    // both in the dispatch of the move, and a rectangle is tested by its bounding box
    // before its path is made. The time of a move is printed (measured by the review).
    constexpr int COLUMNS = 70;
    constexpr int ROWS = 72;
    constexpr qreal CELL = 8.0;
    constexpr qreal SIZE = 6.0;
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, COLUMNS * CELL, ROWS * CELL));
    std::vector<PDFObjectReference> squares;
    for (int row = 0; row < ROWS && squares.size() < 5000; ++row)
    {
        for (int column = 0; column < COLUMNS && squares.size() < 5000; ++column)
        {
            squares.push_back(builder.createAnnotationSquare(page, QRectF(column * CELL + 1.0, row * CELL + 1.0, SIZE, SIZE), 0.5, Qt::yellow, Qt::black, QString(), QString(), QString()));
        }
    }
    ToolsFixture fixture(builder.build());
    PDFWidgetAnnotationManager& annotations = fixture.annotations;
    PDFSelectTextTool* textTool = fixture.tools.getSelectTextTool();
    QVERIFY(textTool);
    fixture.tools.setActiveTool(PDFToolManager::SelectTextTool);
    textTool->setAnnotationsSelectable(true);

    QElapsedTimer timer;
    timer.start();
    constexpr int MOVES = 400;
    int moves = 0;
    int onSquares = 0;
    int hovered = 0;
    int gaps = 0;
    for (int i = 0; i < MOVES; ++i)
    {
        const qreal x = (i % COLUMNS) * CELL + (i % 2 ? 4.0 : 0.4);
        const qreal y = ((i * 7) % ROWS) * CELL + (i % 2 ? 4.0 : 0.4);
        const QPoint position = fixture.device(QPointF(x, y));
        if (!fixture.target()->rect().adjusted(2, 2, -2, -2).contains(position))
        {
            continue;
        }
        ++moves;
        onSquares += (i % 2 && size_t(((i * 7) % ROWS) * COLUMNS + i % COLUMNS) < squares.size()) ? 1 : 0;
        fixture.moveTo(position);
        hovered += fixture.target()->cursor().shape() == Qt::SizeAllCursor ? 1 : 0;
        gaps += fixture.target()->cursor().shape() == Qt::ArrowCursor ? 1 : 0;
    }
    const qint64 elapsed = timer.nsecsElapsed();
    QVERIFY(moves > MOVES / 2);
    qInfo("%d annotations: %.3f ms per mouse move of the text tool with selectable annotations", int(squares.size()), double(elapsed) / 1.0e6 / moves);

    // The cursor of every move is that of its own position (the hover of the manager is
    // not one move late): a move to a gap after an annotation shows the arrow of the tool
    QCOMPARE(hovered, onSquares);
    QCOMPARE(gaps, moves - onSquares);

    // The cursor and the hit test follow the annotation under the cursor
    const QPoint onSquare = fixture.device(QPointF(CELL * 3 + 4.0, CELL * 5 + 4.0));
    const QPoint inGap = fixture.device(QPointF(CELL * 3 + 0.4, CELL * 5 + 0.4));
    QCOMPARE(annotations.getSelectableAnnotationsAt(onSquare), std::vector<PDFObjectReference>({ squares[5 * COLUMNS + 3] }));
    QVERIFY(annotations.getSelectableAnnotationsAt(inGap).empty());
    fixture.moveTo(onSquare);
    QCOMPARE(fixture.target()->cursor().shape(), Qt::SizeAllCursor);
    fixture.moveTo(inGap);
    QCOMPARE(fixture.target()->cursor().shape(), Qt::ArrowCursor);
    fixture.moveTo(onSquare);
    QCOMPARE(fixture.target()->cursor().shape(), Qt::SizeAllCursor);

    // A selected annotation is hit by its rectangle - the hit test of the move after the
    // selection is not the one of the move before it
    fixture.click(onSquare);
    QVERIFY(annotations.isAnnotationSelected(squares[5 * COLUMNS + 3]));
    fixture.moveTo(onSquare);
    QVERIFY(fixture.target()->cursor().shape() != Qt::ArrowCursor);
    QVERIFY(annotations.isInteractionAt(onSquare, Qt::NoModifier));
    fixture.key(Qt::Key_Escape);
    fixture.moveTo(inGap);
    QCOMPARE(fixture.target()->cursor().shape(), Qt::ArrowCursor);
    QVERIFY(!annotations.hasSelection());

}

void AnnotationToolsTest::reviewPickToolViewColors()
{
    // The cross and the snap points of the pick tools: the colors of the old applications
    // (a difference cross, a color per type of the point) without view colors, the colors
    // of the theme of the application with them
    TwoSquares squares;
    ToolsFixture fixture(squares.document);
    PDFToolManager* tools = &fixture.tools;
    tools->pickRectangle([](PDFInteger, QRectF) { });
    PDFPickTool* pickTool = qobject_cast<PDFPickTool*>(tools->getPredefinedTool(PDFToolManager::PickRectangleTool));
    QVERIFY(pickTool);
    QVERIFY(pickTool->isActive());
    fixture.moveTo(fixture.device(QPointF(150, 100)));

    auto draw = [&fixture, pickTool]()
    {
        QImage image(800, 800, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        pickTool->drawPostRendering(&painter, image.rect());
        painter.end();
        return image;
    };

    const QColor cross(10, 200, 30);
    const QColor point(200, 10, 130);
    const QColor snapped(250, 140, 0);

    const QImage oldImage = draw();
    QVERIFY(ToolsFixture::countDrawn(oldImage) > 0);
    QCOMPARE(ToolsFixture::countColor(oldImage, cross), 0);

    PDFViewColors colors = PDFViewColors::getDefaultColors();
    colors.pickCross = cross;
    colors.annotationPoint = point;
    colors.snapMarker = snapped;
    fixture.proxy()->setViewColors(colors);
    const QImage themeImage = draw();
    QVERIFY(ToolsFixture::countColor(themeImage, cross) > 10);
    tools->setActiveTool(nullptr);

    // The snap points (the corners of a page): blue of the old applications, the
    // color of the theme for every point by the overload of the application
    PDFSnapInfo info;
    info.addPageMediaBox(QRectF(100, 100, 200, 200));
    PDFSnapper snapper;
    snapper.setSnapPointPixelSize(10);
    snapper.addSnapInfo(0, QTransform(), info);
    auto drawPoints = [&snapper](const QColor& pointColor, const QColor& snappedColor)
    {
        QImage image(400, 400, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        if (pointColor.isValid())
        {
            snapper.drawSnapPoints(&painter, pointColor, snappedColor);
        }
        else
        {
            snapper.drawSnapPoints(&painter);
        }
        painter.end();
        return image;
    };
    const QImage oldPoints = drawPoints(QColor(), QColor());
    QVERIFY(ToolsFixture::countColor(oldPoints, QColor(Qt::blue), 40) > 0);
    QCOMPARE(ToolsFixture::countColor(oldPoints, point), 0);
    const QImage themePoints = drawPoints(point, snapped);
    QVERIFY(ToolsFixture::countColor(themePoints, point, 40) > 0);
    QCOMPARE(ToolsFixture::countColor(themePoints, QColor(Qt::blue), 40), 0);
}

QTEST_MAIN(AnnotationToolsTest)

#include "tst_annotationtoolstest.moc"
