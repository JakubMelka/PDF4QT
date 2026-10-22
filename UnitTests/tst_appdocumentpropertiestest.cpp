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

#include "pdfappcommand.h"
#include "pdfappdeveloperoverlay.h"
#include "pdfappdocument.h"
#include "pdfappdocumentactions.h"
#include "pdfappdocumentcommands.h"
#include "pdfappdocumentproperties.h"
#include "pdfappdocumentpropertiesdata.h"
#include "pdfappdocumentpropertiespage.h"
#include "pdfappdocumentstatusbar.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfappmainwindow.h"
#include "pdfappnavigator.h"
#include "pdfapppagelayout.h"
#include "pdfapppagenotfound.h"
#include "pdfapprenderingissues.h"
#include "pdfapproute.h"
#include "pdfappundostack.h"
#include "pdfsettingdefinition.h"
#include "pdfsettingdefinitions.h"
#include "pdfsettingsrepository.h"

#include "pdfannotation.h"
#include "pdfcomponentbuttons.h"
#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdfdocumentbuilder.h"
#include "pdfdrawspacecontroller.h"
#include "pdfdocumentreader.h"
#include "pdfdrawwidget.h"
#include "pdfform.h"
#include "pdffluentwidgets.h"
#include "pdfmessagebox.h"
#include "pdfstylepainter.h"
#include "pdfthemecolors.h"
#include "pdfthemefonts.h"
#include "pdfthememanager.h"
#include "pdftooltip.h"
#include "pdfwidgetannotation.h"
#include "pdfwidgetstyle.h"
#include "pdfwidgettool.h"

#include <QClipboard>
#include <QListView>
#include <QMenu>
#include <QToolButton>
#include <QtTest>

#include <functional>

/// Helpers of the test
class PropertiesTestHelper
{
public:
    /// Command handler of package F of the tab
    static pdf::PDFAppDocumentPropertiesCommandHandler* handler(pdf::PDFAppDocumentTab* tab)
    {
        return tab ? qobject_cast<pdf::PDFAppDocumentPropertiesCommandHandler*>(tab->findHandler(pdf::PDFAppCommands::properties())) : nullptr;
    }

    /// Shows the section of the properties of the tab (the page is returned)
    static pdf::PDFAppDocumentPropertiesPage* showProperties(pdftest::AppDocumentFixture& fixture, pdf::PDFAppDocumentTab* tab, const QString& section)
    {
        fixture.window->getNavigator()->navigate(pdf::PDFAppRoutes::documentProperties(tab->getDocumentId(), section));
        fixture.settle();
        pdf::PDFAppDocumentPropertiesPage* page = qobject_cast<pdf::PDFAppDocumentPropertiesPage*>(tab->getPage());
        if (page)
        {
            page->rebuildNow();
            fixture.settle();
        }
        return page;
    }

    /// Clicks the button of a message box, as soon as the message box is shown (a
    /// timer of the nested event loop of exec). \returns the clicker (it deletes itself).
    static QTimer* clickWhenShown(const QString& dialogName, const QString& buttonName, QStringList* seenTexts = nullptr)
    {
        QTimer* timer = new QTimer();
        timer->setInterval(5);
        QObject::connect(timer, &QTimer::timeout, timer, [timer, dialogName, buttonName, seenTexts]()
        {
            for (QWidget* widget : QApplication::topLevelWidgets())
            {
                pdf::PDFMessageBox* box = qobject_cast<pdf::PDFMessageBox*>(widget);
                if (!box)
                {
                    for (pdf::PDFMessageBox* child : widget->findChildren<pdf::PDFMessageBox*>())
                    {
                        if (child->isVisible() && child->objectName() == dialogName)
                        {
                            box = child;
                        }
                    }
                }
                if (box && box->isVisible() && box->objectName() == dialogName)
                {
                    if (seenTexts)
                    {
                        for (QLabel* label : box->findChildren<QLabel*>())
                        {
                            seenTexts->append(label->text());
                        }
                        for (pdf::PDFKeyValueList* list : box->findChildren<pdf::PDFKeyValueList*>())
                        {
                            for (int i = 0; i < list->getRowCount(); ++i)
                            {
                                seenTexts->append(list->getValue(i));
                            }
                        }
                    }
                    if (buttonName == QStringLiteral("escape"))
                    {
                        QTest::keyClick(box, Qt::Key_Escape);
                    }
                    else if (buttonName == QStringLiteral("enter"))
                    {
                        QTest::keyClick(box, Qt::Key_Return);
                    }
                    else if (QPushButton* button = box->findChild<QPushButton*>(buttonName))
                    {
                        button->click();
                    }
                    timer->stop();
                    timer->deleteLater();
                    return;
                }
            }
        });
        timer->start();
        return timer;
    }

    /// Actions of the link annotations of the first page (the annotations are kept alive)
    static std::vector<const pdf::PDFAction*> linkActions(const pdf::PDFDocument* document, std::vector<pdf::PDFAnnotationPtr>& annotations)
    {
        std::vector<const pdf::PDFAction*> actions;
        for (const pdf::PDFObjectReference& reference : document->getCatalog()->getPage(0)->getAnnotations())
        {
            pdf::PDFAnnotationPtr annotation = pdf::PDFAnnotation::parse(&document->getStorage(), reference);
            const pdf::PDFLinkAnnotation* link = dynamic_cast<const pdf::PDFLinkAnnotation*>(annotation.get());
            actions.push_back(link ? link->getAction() : nullptr);
            annotations.push_back(annotation);
        }
        return actions;
    }

    /// Action parsed from a dictionary
    static pdf::PDFActionPtr parseAction(const pdf::PDFDocument* document, const pdf::PDFObject& object)
    {
        return pdf::PDFAction::parse(&document->getStorage(), object);
    }

    static pdf::PDFObject name(const char* text)
    {
        return pdf::PDFObject::createName(QByteArray(text));
    }

    static pdf::PDFObject dictionary(std::initializer_list<std::pair<const char*, pdf::PDFObject>> entries)
    {
        pdf::PDFDictionaryBuilder builder;
        for (const auto& entry : entries)
        {
            builder.addEntry(pdf::PDFInplaceOrMemoryString(entry.first), entry.second);
        }
        return pdf::PDFObject::createDictionary(std::move(builder));
    }

    /// Picture of the widget at the device pixel ratio
    static QImage render(QWidget* widget, qreal devicePixelRatio = 1.0)
    {
        QImage image(QSize(qCeil(widget->width() * devicePixelRatio), qCeil(widget->height() * devicePixelRatio)), QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(devicePixelRatio);
        image.fill(pdf::PDFStylePainter::color(pdf::PDFThemeColors::BgCanvas));
        widget->render(&image, QPoint(), QRegion(), widget->isWindow() ? QWidget::RenderFlags(QWidget::DrawChildren | QWidget::DrawWindowBackground) : QWidget::RenderFlags(QWidget::DrawChildren));
        return image;
    }

    /// Number of the pixels of the leak color (a color of the palette)
    static int countLeakPixels(const QImage& image)
    {
        const QRgb leak = pdf::PDFThemeColors::getLeakColor().rgb() & 0xFFFFFF;
        int count = 0;
        const QImage converted = image.convertToFormat(QImage::Format_RGB32);
        for (int y = 0; y < converted.height(); ++y)
        {
            const QRgb* line = reinterpret_cast<const QRgb*>(converted.constScanLine(y));
            for (int x = 0; x < converted.width(); ++x)
            {
                count += (line[x] & 0xFFFFFF) == leak ? 1 : 0;
            }
        }
        return count;
    }

    /// Document with an Info dictionary, custom properties and XMP (or without it)
    static QString infoDocument(const QString& folder, const QString& name, bool withMetadata)
    {
        pdf::PDFDocumentBuilder builder;
        builder.createDocument();
        builder.appendPage(QRectF(0, 0, 595, 842));
        builder.appendPage(QRectF(0, 0, 595, 842));
        pdf::PDFObjectFactory factory;
        factory.beginDictionary();
        factory.beginDictionaryItem("Title");
        factory << pdf::PDFObjectFactory::createTextString(QStringLiteral("Project Report 2026"));
        factory.endDictionaryItem();
        factory.beginDictionaryItem("Author");
        factory << pdf::PDFObjectFactory::createTextString(QStringLiteral("Jane Novak"));
        factory.endDictionaryItem();
        factory.beginDictionaryItem("Department");
        factory << pdf::PDFObjectFactory::createTextString(QStringLiteral("Finance"));
        factory.endDictionaryItem();
        factory.beginDictionaryItem("Trapped");
        factory << pdf::WrapName("False");
        factory.endDictionaryItem();
        factory.endDictionary();
        builder.updateDocumentInfo(factory.takeObject());
        if (withMetadata)
        {
            builder.setCatalogMetadata(QByteArray("<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\"/></x:xmpmeta>"));
        }
        pdf::PDFDocument document = builder.build();
        const QString path = QDir(folder).absoluteFilePath(name);
        return pdftest::AppDocumentSamples::write(document, path) ? path : QString();
    }

    /// Point of the widget, which draws the pages, at the point of the first page
    static QPoint pagePoint(pdf::PDFAppDocumentView* view, const QPointF& pagePoint)
    {
        const pdf::PDFWidgetSnapshot snapshot = view->getProxy()->getSnapshot();
        for (const pdf::PDFWidgetSnapshot::SnapshotItem& item : snapshot.items)
        {
            if (item.pageIndex == 0)
            {
                return item.pageToDeviceMatrix.map(pagePoint).toPoint();
            }
        }
        return QPoint(-1, -1);
    }

    /// Opens the file and waits until its pages are shown by the view
    static pdf::PDFAppDocumentTab* openReady(pdftest::AppDocumentFixture& fixture, const QString& filePath)
    {
        QPointer<pdf::PDFAppDocumentTab> tab = fixture.open(filePath);
        if (!tab)
        {
            return nullptr;
        }
        const bool isReady = QTest::qWaitFor([&tab]()
        {
            return tab && tab->getDocument() && tab->getDocument()->isReady() && tab->getView() && tab->getView()->getProxy()->getDocument();
        }, 20000);
        fixture.settle();
        return isReady ? tab.data() : nullptr;
    }

    /// Closes the tab of the window: a changed document is marked as saved first (the
    /// question of the unsaved changes would wait for an answer)
    static bool closeTab(pdftest::AppDocumentFixture& fixture, pdf::PDFAppDocumentTab* tab)
    {
        if (!tab || !fixture.window)
        {
            return false;
        }
        if (tab->getDocument())
        {
            tab->getDocument()->setSaved();
        }
        return fixture.window->closeTab(tab);
    }

    static void sendMouse(QWidget* widget, QEvent::Type type, const QPoint& position, Qt::MouseButton button, Qt::MouseButtons buttons)
    {
        QMouseEvent event(type, QPointF(position), widget->mapToGlobal(QPointF(position)), button, buttons, Qt::NoModifier);
        QApplication::sendEvent(widget, &event);
    }
};

/// Package F of step 3: the page Document properties (5 sections, editing of the
/// Info dictionary and of the XMP packet with undo), links and actions (hover,
/// confirmations, blocked links, Launch, GoToR, JavaScript, SubmitForm), the
/// rendering issues (status item and popover) and the developer view.
class AppDocumentPropertiesTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();

    // Data of the properties
    void dataGeneral();
    void dataSecurity();
    void dataDisplay();
    void dataInfoChanges();
    void dataMetadata();
    void dataFonts();
    void dataHostile();

    // Page
    void propertiesRoute();
    void propertiesCommands();
    void sectionGeneral();
    void editInfoWithUndo();
    void lockedDocument();
    void sectionSecurity();
    void sectionFonts();
    void sectionDisplay();
    void sectionXmp();
    void copyAll();
    void pageDeletion();
    void minimalWindowAndDirection();

    // Links and actions
    void urlRules();
    void linkDialogs();
    void webLinks();
    void webLinksBlocked();
    void launchActions();
    void otherActions();
    void linkHover();
    void linkClick();
    void actionLifetime();

    // Rendering issues, developer view
    void renderingIssues();
    void renderingIssuesLifetime();
    void developerOverlay();
    void settings();

    // Pictures of the screens (PDF4QT_APP_SCREENSHOTS)
    void screenshots();
};

void AppDocumentPropertiesTest::initTestCase()
{
    // The text of the samples needs fonts (offscreen has none)
    QVERIFY(pdf::PDFThemeFonts::loadFonts());

    // The style of the application (the real application sets it in main)
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    QVERIFY(pdf::PDFWidgetStyle::isActive());
}

void AppDocumentPropertiesTest::cleanup()
{
    pdf::PDFAppDocumentActionHandler::setLaunchHandler(pdf::PDFAppDocumentActionHandler::LaunchHandler());
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
    QApplication::setLayoutDirection(Qt::LeftToRight);
}

void AppDocumentPropertiesTest::dataGeneral()
{
    QTemporaryDir directory;
    const QString path = PropertiesTestHelper::infoDocument(directory.path(), QStringLiteral("Info.pdf"), false);
    QVERIFY(!path.isEmpty());
    pdf::PDFDocumentReader reader(nullptr, [](bool* ok) { *ok = false; return QString(); }, false, false);
    const pdf::PDFDocument document = reader.readFromFile(path);
    QCOMPARE(reader.getReadingResult(), pdf::PDFDocumentReader::Result::OK);

    const std::vector<pdf::PDFAppPropertyGroup> groups = pdf::PDFAppDocumentPropertiesData::createGeneralGroups(&document, path, QStringLiteral("Info.pdf"), 1700000000000);
    QCOMPARE(groups.size(), size_t(4));
    QCOMPARE(groups[0].id, QStringLiteral("description"));
    QCOMPARE(groups[0].items[0].value, QStringLiteral("Project Report 2026"));
    QCOMPARE(groups[0].items[0].infoKey, QByteArray("Title"));
    QCOMPARE(groups[0].items[2].value, QStringLiteral("Jane Novak"));
    QVERIFY(groups[0].items[4].infoKey.isEmpty());
    QCOMPARE(groups[1].id, QStringLiteral("content"));
    QCOMPARE(groups[1].items[0].value, QStringLiteral("2"));
    QCOMPARE(groups[1].items[1].value, QStringLiteral("A4"));
    QVERIFY(groups[1].items[2].value.contains(QStringLiteral("210")));
    QCOMPARE(groups[1].items[3].value, QStringLiteral("No"));
    QCOMPARE(groups[2].id, QStringLiteral("file"));
    QCOMPARE(groups[2].items[0].value, QStringLiteral("Info.pdf"));
    QVERIFY(!groups[2].items.back().value.isEmpty());
    QCOMPARE(groups[3].id, QStringLiteral("custom"));
    QCOMPARE(groups[3].items.size(), size_t(1));
    QCOMPARE(groups[3].items[0].key, QStringLiteral("Department"));
    QCOMPARE(groups[3].items[0].infoKey, QByteArray("Department"));

    // A document without a file, no document
    const std::vector<pdf::PDFAppPropertyGroup> copyGroups = pdf::PDFAppDocumentPropertiesData::createGeneralGroups(&document, QString(), QStringLiteral("Untitled"), 0);
    QCOMPARE(copyGroups[2].items.size(), size_t(2));
    QCOMPARE(copyGroups[2].items[0].value, QStringLiteral("Untitled"));
    QVERIFY(pdf::PDFAppDocumentPropertiesData::createGeneralGroups(nullptr, path, QString(), 0).empty());

    // Various sizes of the pages
    pdf::PDFDocumentBuilder builder;
    builder.createDocument();
    builder.appendPage(QRectF(0, 0, 595, 842));
    builder.appendPage(QRectF(0, 0, 842, 595));
    builder.appendPage(QRectF(0, 0, 300, 300));
    const pdf::PDFDocument mixed = builder.build();
    const std::vector<pdf::PDFAppPropertyGroup> mixedGroups = pdf::PDFAppDocumentPropertiesData::createGeneralGroups(&mixed, QString(), QStringLiteral("Mixed"), 0);
    QCOMPARE(mixedGroups[1].items[1].value, QStringLiteral("Various sizes"));
    QVERIFY(mixedGroups[1].items[2].value.endsWith(QStringLiteral("(page 1)")));
}

void AppDocumentPropertiesTest::dataSecurity()
{
    QTemporaryDir directory;
    const QString ownerPath = pdftest::AppDocumentSamples::ownerPassword(directory.path());
    pdf::PDFDocumentReader reader(nullptr, [](bool* ok) { *ok = false; return QString(); }, false, false);
    const pdf::PDFDocument owner = reader.readFromFile(ownerPath);
    QCOMPARE(reader.getReadingResult(), pdf::PDFDocumentReader::Result::OK);

    const pdf::PDFAppPropertyGroup encryption = pdf::PDFAppDocumentPropertiesData::createEncryptionGroup(&owner);
    QCOMPARE(encryption.items.size(), size_t(4));
    QCOMPARE(encryption.items[0].value, QStringLiteral("Standard (password)"));
    QCOMPARE(encryption.items[1].value, QStringLiteral("User"));
    QCOMPARE(encryption.items[3].value, QStringLiteral("5 (AES, 256 bit)"));

    const std::vector<pdf::PDFAppPermissionItem> permissions = pdf::PDFAppDocumentPropertiesData::createPermissions(&owner);
    QCOMPARE(permissions.size(), size_t(8));
    QVERIFY(!permissions[0].isAllowed);
    QVERIFY(!permissions[2].isAllowed);
    QVERIFY(!pdf::PDFAppDocumentPropertiesData::getSecurityNote(&owner).isEmpty());

    // Not encrypted: everything is allowed, no note
    const QString simplePath = pdftest::AppDocumentSamples::simple(directory.path(), QStringLiteral("Simple.pdf"), 1);
    const pdf::PDFDocument simple = reader.readFromFile(simplePath);
    QCOMPARE(pdf::PDFAppDocumentPropertiesData::createEncryptionGroup(&simple).items.size(), size_t(1));
    QCOMPARE(pdf::PDFAppDocumentPropertiesData::createEncryptionGroup(&simple).items[0].value, QStringLiteral("Not encrypted"));
    for (const pdf::PDFAppPermissionItem& permission : pdf::PDFAppDocumentPropertiesData::createPermissions(&simple))
    {
        QVERIFY(permission.isAllowed);
    }
    QVERIFY(pdf::PDFAppDocumentPropertiesData::getSecurityNote(&simple).isEmpty());
    QVERIFY(pdf::PDFAppDocumentPropertiesData::getSecurityNote(nullptr).isEmpty());
    QCOMPARE(pdf::PDFAppDocumentPropertiesData::createPermissions(nullptr).size(), size_t(8));
}

void AppDocumentPropertiesTest::dataDisplay()
{
    pdf::PDFDocumentBuilder builder;
    builder.createDocument();
    builder.appendPage(QRectF(0, 0, 595, 842));
    const pdf::PDFDocument document = builder.build();
    const std::vector<pdf::PDFAppPropertyGroup> groups = pdf::PDFAppDocumentPropertiesData::createDisplayGroups(&document);
    QCOMPARE(groups.size(), size_t(2));
    QCOMPARE(groups[0].items[2].value, QStringLiteral("Left to right"));
    QCOMPARE(groups[1].items[0].value, QStringLiteral("Application default"));
    QCOMPARE(groups[1].items[1].value, QStringLiteral("Not set"));
    QCOMPARE(groups[1].items[3].value, QStringLiteral("All pages"));
    QCOMPARE(groups[1].items[4].value, QStringLiteral("1"));
    QVERIFY(pdf::PDFAppDocumentPropertiesData::createDisplayGroups(nullptr).empty());

    // Every page layout and page mode, viewer preferences of printing
    const char* layouts[] = { "SinglePage", "OneColumn", "TwoColumnLeft", "TwoColumnRight", "TwoPageLeft", "TwoPageRight" };
    const char* modes[] = { "UseNone", "UseOutlines", "UseThumbs", "FullScreen", "UseOC", "UseAttachments" };
    const char* duplexes[] = { "Simplex", "DuplexFlipShortEdge", "DuplexFlipLongEdge", "Simplex", "Simplex", "Simplex" };
    QStringList layoutTexts;
    QStringList modeTexts;
    QStringList duplexTexts;
    for (int i = 0; i < 6; ++i)
    {
        pdf::PDFDocumentBuilder preferencesBuilder;
        preferencesBuilder.createDocument();
        preferencesBuilder.appendPage(QRectF(0, 0, 595, 842));
        pdf::PDFArrayBuilder ranges;
        for (int range = 0; range < 104; ++range)
        {
            ranges.appendItem(pdf::PDFObject::createInteger(range * 2 + 1));
            ranges.appendItem(pdf::PDFObject::createInteger(range == 0 ? 1 : range * 2 + 2));
        }
        const pdf::PDFObject preferences = PropertiesTestHelper::dictionary({ { "Direction", PropertiesTestHelper::name("R2L") },
                                                                              { "PrintScaling", PropertiesTestHelper::name("None") },
                                                                              { "Duplex", PropertiesTestHelper::name(duplexes[i]) },
                                                                              { "PickTrayByPDFSize", pdf::PDFObject::createBool(true) },
                                                                              { "NumCopies", pdf::PDFObject::createInteger(3) },
                                                                              { "PrintPageRange", pdf::PDFObject::createArray(ranges) } });
        preferencesBuilder.mergeTo(preferencesBuilder.getCatalogReference(), PropertiesTestHelper::dictionary({ { "PageLayout", PropertiesTestHelper::name(layouts[i]) },
                                                                                                                { "PageMode", PropertiesTestHelper::name(modes[i]) },
                                                                                                                { "ViewerPreferences", preferences } }));
        // A print page range used to make the document unreadable (Core read the duplex as the array)
        const pdf::PDFDocument preferencesDocument = preferencesBuilder.build();
        const std::vector<pdf::PDFAppPropertyGroup> preferenceGroups = pdf::PDFAppDocumentPropertiesData::createDisplayGroups(&preferencesDocument);
        layoutTexts << preferenceGroups[0].items[0].value;
        modeTexts << preferenceGroups[0].items[1].value;
        duplexTexts << preferenceGroups[1].items[1].value;
        QCOMPARE(preferenceGroups[0].items[2].value, QStringLiteral("Right to left"));
        QCOMPARE(preferenceGroups[1].items[0].value, QStringLiteral("None"));
        QCOMPARE(preferenceGroups[1].items[2].value, QStringLiteral("Yes"));
        QVERIFY(preferenceGroups[1].items[3].value.startsWith(QStringLiteral("1, 3–4")));
        QVERIFY(preferenceGroups[1].items[3].value.endsWith(QStringLiteral("…")));
        QCOMPARE(preferenceGroups[1].items[4].value, QStringLiteral("3"));
    }
    QCOMPARE(layoutTexts.removeDuplicates(), 0);
    QCOMPARE(modeTexts.removeDuplicates(), 0);
    QVERIFY(duplexTexts.contains(QStringLiteral("Duplex (flip long edge)")));
    QVERIFY(duplexTexts.contains(QStringLiteral("Duplex (flip short edge)")));
    QVERIFY(duplexTexts.contains(QStringLiteral("Simplex")));
}

void AppDocumentPropertiesTest::dataInfoChanges()
{
    QTemporaryDir directory;
    const QString path = PropertiesTestHelper::infoDocument(directory.path(), QStringLiteral("Info.pdf"), false);
    pdf::PDFDocumentReader reader(nullptr, [](bool* ok) { *ok = false; return QString(); }, false, false);
    const pdf::PDFDocument document = reader.readFromFile(path);

    // A new value, the same value (nothing), a removed value, a new custom key
    pdf::PDFDocumentPointer changed = pdf::PDFAppDocumentPropertiesData::setInfoValue(&document, QByteArrayLiteral("Title"), QStringLiteral("Příliš žluťoučký kůň"));
    QVERIFY(changed);
    QCOMPARE(changed->getInfo()->title, QStringLiteral("Příliš žluťoučký kůň"));
    QCOMPARE(changed->getInfo()->author, QStringLiteral("Jane Novak"));
    QVERIFY(!pdf::PDFAppDocumentPropertiesData::setInfoValue(changed.data(), QByteArrayLiteral("Title"), QStringLiteral("Příliš žluťoučký kůň")));
    pdf::PDFDocumentPointer removed = pdf::PDFAppDocumentPropertiesData::setInfoValue(changed.data(), QByteArrayLiteral("Author"), QString());
    QVERIFY(removed);
    QVERIFY(removed->getInfo()->author.isEmpty());
    QVERIFY(!pdf::PDFAppDocumentPropertiesData::setInfoValue(removed.data(), QByteArrayLiteral("Author"), QString()));
    pdf::PDFDocumentPointer custom = pdf::PDFAppDocumentPropertiesData::setInfoValue(removed.data(), QByteArrayLiteral("Status"), QStringLiteral("Final"));
    QVERIFY(custom);
    QCOMPARE(custom->getInfo()->extra.at(QByteArrayLiteral("Status")).toString(), QStringLiteral("Final"));

    // A document without the Info dictionary gets one
    pdf::PDFDocumentBuilder builder;
    builder.createDocument();
    builder.appendPage(QRectF(0, 0, 100, 100));
    const pdf::PDFDocument empty = builder.build();
    pdf::PDFDocumentPointer withInfo = pdf::PDFAppDocumentPropertiesData::setInfoValue(&empty, QByteArrayLiteral("Subject"), QStringLiteral("Subject"));
    QVERIFY(withInfo);
    QCOMPARE(withInfo->getInfo()->subject, QStringLiteral("Subject"));

    // Hostile keys are refused
    QVERIFY(!pdf::PDFAppDocumentPropertiesData::setInfoValue(&document, QByteArray(), QStringLiteral("x")));
    QVERIFY(!pdf::PDFAppDocumentPropertiesData::setInfoValue(&document, QByteArray(200, 'K'), QStringLiteral("x")));
    QVERIFY(!pdf::PDFAppDocumentPropertiesData::setInfoValue(&document, QByteArray("A\0B", 3), QStringLiteral("x")));
    QVERIFY(!pdf::PDFAppDocumentPropertiesData::setInfoValue(nullptr, QByteArrayLiteral("Title"), QStringLiteral("x")));
}

void AppDocumentPropertiesTest::dataMetadata()
{
    QTemporaryDir directory;
    const QString path = PropertiesTestHelper::infoDocument(directory.path(), QStringLiteral("Info.pdf"), false);
    pdf::PDFDocumentReader reader(nullptr, [](bool* ok) { *ok = false; return QString(); }, false, false);
    const pdf::PDFDocument document = reader.readFromFile(path);

    bool hasMetadata = true;
    QString error;
    QVERIFY(pdf::PDFAppDocumentPropertiesData::getMetadata(&document, &hasMetadata, &error).isEmpty());
    QVERIFY(!hasMetadata);

    // The default packet from the Info dictionary: well-formed, with the title and the author escaped
    const QByteArray xml = pdf::PDFAppDocumentPropertiesData::createDefaultMetadata(&document, QDateTime(QDate(2026, 10, 22), QTime(12, 0), QTimeZone::UTC));
    QVERIFY(pdf::PDFAppDocumentPropertiesData::validateMetadata(xml, &error));
    QVERIFY(xml.contains("W5M0MpCehiHzreSzNTczkc9d"));
    QVERIFY(xml.contains("Project Report 2026"));
    QVERIFY(xml.contains("Jane Novak"));
    QVERIFY(xml.contains("xmp:MetadataDate"));

    pdf::PDFDocumentPointer withMetadata = pdf::PDFAppDocumentPropertiesData::setMetadata(&document, xml);
    QVERIFY(withMetadata);
    const QString text = pdf::PDFAppDocumentPropertiesData::getMetadata(withMetadata.data(), &hasMetadata, &error);
    QVERIFY(hasMetadata);
    QVERIFY(error.isEmpty());
    QVERIFY(text.contains(QStringLiteral("Project Report 2026")));
    QVERIFY(!pdf::PDFAppDocumentPropertiesData::setMetadata(withMetadata.data(), text.toUtf8()));

    // Refused packets: empty, malformed, a DTD (entities), too large
    QVERIFY(!pdf::PDFAppDocumentPropertiesData::validateMetadata(QByteArray("   "), &error));
    QVERIFY(!pdf::PDFAppDocumentPropertiesData::validateMetadata(QByteArray("<a><b></a>"), &error));
    QVERIFY(error.contains(QStringLiteral("line 1")));
    QVERIFY(!pdf::PDFAppDocumentPropertiesData::validateMetadata(QByteArray("<?xml version=\"1.0\"?><!DOCTYPE a [<!ENTITY x \"xxxxxxxxxx\"><!ENTITY y \"&x;&x;&x;&x;\">]><a>&y;</a>"), &error));
    QVERIFY(error.contains(QStringLiteral("Document type")));
    QVERIFY(!pdf::PDFAppDocumentPropertiesData::validateMetadata(QByteArray("plain text"), &error));
    QByteArray huge("<a>");
    huge.append(QByteArray(pdf::PDFAppDocumentPropertiesData::MAXIMUM_METADATA_SIZE, 'x'));
    huge.append("</a>");
    QVERIFY(!pdf::PDFAppDocumentPropertiesData::validateMetadata(huge, &error));
    QVERIFY(!pdf::PDFAppDocumentPropertiesData::setMetadata(&document, QByteArray("<a>")));
    QVERIFY(!pdf::PDFAppDocumentPropertiesData::setMetadata(nullptr, xml));

    // UTF-16 packet with a byte order mark
    QByteArray utf16;
    utf16.append(QByteArray::fromHex("FFFE"));
    const QString sample = QStringLiteral("<a>ž</a>");
    utf16.append(reinterpret_cast<const char*>(sample.utf16()), sample.size() * 2);
    pdf::PDFDocumentBuilder builder;
    builder.createDocument();
    builder.appendPage(QRectF(0, 0, 100, 100));
    builder.setCatalogMetadata(utf16);
    const pdf::PDFDocument utf16Document = builder.build();
    QCOMPARE(pdf::PDFAppDocumentPropertiesData::getMetadata(&utf16Document, &hasMetadata, &error), sample);
}

void AppDocumentPropertiesTest::dataFonts()
{
    QTemporaryDir directory;
    const QString path = pdftest::AppDocumentSamples::simple(directory.path(), QStringLiteral("Fonts.pdf"), 3);
    pdf::PDFDocumentReader reader(nullptr, [](bool* ok) { *ok = false; return QString(); }, false, false);
    const pdf::PDFDocument document = reader.readFromFile(path);
    QCOMPARE(reader.getReadingResult(), pdf::PDFDocumentReader::Result::OK);

    const std::vector<pdf::PDFAppFontInfo> fonts = pdf::PDFAppDocumentFontScanTask::scan(&document, nullptr);
    QVERIFY(!fonts.empty());
    for (const pdf::PDFAppFontInfo& font : fonts)
    {
        QVERIFY(!font.name.isEmpty());
        QVERIFY(!font.type.isEmpty());
        QVERIFY(!font.getStatusText().isEmpty());
        QVERIFY(!font.name.contains(QLatin1Char('+')));
    }

    // Canceled before the first page, no document
    std::atomic_bool cancel(true);
    QVERIFY(pdf::PDFAppDocumentFontScanTask::scan(&document, &cancel).empty());
    QVERIFY(pdf::PDFAppDocumentFontScanTask::scan(nullptr, nullptr).empty());

    // The functor of the worker
    auto flag = std::make_shared<std::atomic_bool>(false);
    pdf::PDFAppDocumentFontScanTask task(pdf::PDFDocumentPointer(new pdf::PDFDocument(document)), flag);
    QCOMPARE(task().size(), fonts.size());

    // Statuses and the types
    pdf::PDFAppFontInfo info;
    info.isEmbedded = true;
    info.isSubset = true;
    QCOMPARE(info.getStatusText(), QStringLiteral("Embedded subset"));
    info.isSubset = false;
    QCOMPARE(info.getStatusText(), QStringLiteral("Embedded"));
    info.isEmbedded = false;
    QCOMPARE(info.getStatusText(), QStringLiteral("Not embedded"));
    QCOMPARE(pdf::PDFAppDocumentPropertiesData::getFontTypeText(int(pdf::FontType::TrueType)), QStringLiteral("TrueType"));
    QCOMPARE(pdf::PDFAppDocumentPropertiesData::getFontTypeText(999), QStringLiteral("Unknown"));
}

void AppDocumentPropertiesTest::dataHostile()
{
    // Control characters, very long texts
    const QString control = QStringLiteral("a\nb\tc\x01" "d");
    QVERIFY(!pdf::PDFAppDocumentPropertiesData::sanitize(control).contains(QLatin1Char('\n')));
    const QString longText(100000, QLatin1Char('x'));
    QVERIFY(pdf::PDFAppDocumentPropertiesData::sanitize(longText).size() <= pdf::PDFAppDocumentPropertiesData::MAXIMUM_VALUE_LENGTH + 1);
    QVERIFY(pdf::PDFAppDocumentPropertiesData::formatDateTime(QDateTime()).isEmpty());

    // Copy all
    pdf::PDFAppPropertyGroup group;
    group.title = QStringLiteral("Group");
    group.items.push_back(pdf::PDFAppPropertyItem{ QStringLiteral("Key"), QString(), QByteArray(), QString() });
    const QString text = pdf::PDFAppDocumentPropertiesData::createCopyText(QStringLiteral("Heading"), { group });
    QVERIFY(text.startsWith(QStringLiteral("Heading\n\nGroup\nKey: ")));
    QVERIFY(text.endsWith(pdf::PDFAppDocumentPropertiesData::getEmptyValueText()));

    // A damaged document: an Info dictionary of a wrong type, fonts of a wrong type
    pdf::PDFDocumentBuilder builder;
    builder.createDocument();
    const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 100, 100));
    const pdf::PDFObject resources = PropertiesTestHelper::dictionary({ { "Font", PropertiesTestHelper::dictionary({ { "F1", pdf::PDFObject::createInteger(5) },
                                                                                                                   { "F2", PropertiesTestHelper::dictionary({ { "Type", PropertiesTestHelper::name("Font") } }) } }) },
                                                                        { "XObject", PropertiesTestHelper::dictionary({ { "X", pdf::PDFObject::createReference(page) } }) } });
    builder.mergeTo(page, PropertiesTestHelper::dictionary({ { "Resources", resources } }));
    const pdf::PDFDocument document = builder.build();
    QVERIFY(pdf::PDFAppDocumentFontScanTask::scan(&document, nullptr).size() <= 1);
}

void AppDocumentPropertiesTest::propertiesRoute()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Properties.pdf"), 1));
    QVERIFY(tab);

    for (const QString& section : pdf::PDFAppDocumentPropertiesPage::getSectionIds())
    {
        pdf::PDFAppDocumentPropertiesPage* page = PropertiesTestHelper::showProperties(fixture, tab, section);
        QVERIFY2(page, qPrintable(section));
        QCOMPARE(page->getSectionId(), section);
        QCOMPARE(fixture.currentDocumentTab(), tab);
        QVERIFY(!tab->isViewShown());
        QVERIFY(page->getSectionWidget());
        QVERIFY(page->getTitle().contains(QStringLiteral("Properties.pdf")));
    }

    // The navigation replaces the route (Back returns to the document)
    pdf::PDFAppDocumentPropertiesPage* page = PropertiesTestHelper::showProperties(fixture, tab, QStringLiteral("general"));
    QVERIFY(page);
    pdf::PDFNavList* navigation = page->findChild<pdf::PDFNavList*>(QStringLiteral("propertiesNavigationList"));
    QVERIFY(navigation);
    QCOMPARE(navigation->getCount(), 5);
    QCOMPARE(navigation->getCurrentIndex(), 0);
    navigation->getItem(2)->click();
    fixture.settle();
    QCOMPARE(page->getSectionId(), QStringLiteral("fonts"));
    QCOMPARE(page->getRoute(), pdf::PDFAppRoutes::documentProperties(tab->getDocumentId(), QStringLiteral("fonts")));

    QPushButton* back = page->findChild<QPushButton*>(QStringLiteral("backButton"));
    QVERIFY(back);
    back->click();
    fixture.settle();
    QVERIFY(tab->isViewShown());

    // Invalid section and a route of another document: Page Not Found
    fixture.window->getNavigator()->navigate(pdf::PDFAppRoute::create({ QStringLiteral("document"), tab->getDocumentId(), QStringLiteral("properties"), QStringLiteral("bogus") }));
    fixture.settle();
    QVERIFY(!qobject_cast<pdf::PDFAppDocumentPropertiesPage*>(tab->getPage()));

    // Back without a history goes to the document as well
    pdf::PDFAppDocumentPropertiesPage page2(fixture.context.get(), tab);
    QVERIFY(!page2.setRoute(pdf::PDFAppRoutes::documentProperties(QStringLiteral("AAAAAAAAAAAAAAAAAAAAAAAAAA"), QStringLiteral("general"))));
    QVERIFY(!page2.setRoute(pdf::PDFAppRoutes::document(tab->getDocumentId())));
    pdf::PDFAppDocumentPropertiesPage orphan(fixture.context.get(), nullptr);
    QVERIFY(!orphan.setRoute(pdf::PDFAppRoutes::documentProperties(tab->getDocumentId(), QStringLiteral("general"))));
    QVERIFY(!orphan.getDocumentTab());
    QVERIFY(orphan.getCopyText().isEmpty());
}

void AppDocumentPropertiesTest::propertiesCommands()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Commands.pdf"), 1));
    QVERIFY(tab);

    for (const QString& id : pdf::PDFAppDocumentCommandTable::getCommands(pdf::PDFAppDocumentPackage::Properties))
    {
        QVERIFY2(tab->findHandler(id), qPrintable(id));
    }

    QVERIFY(tab->isCommandEnabled(pdf::PDFAppCommands::properties()));
    QVERIFY(!tab->isCommandEnabled(pdf::PDFAppCommands::renderingIssues()));
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::properties()));
    fixture.settle();
    pdf::PDFAppDocumentPropertiesPage* page = qobject_cast<pdf::PDFAppDocumentPropertiesPage*>(tab->getPage());
    QVERIFY(page);
    QCOMPARE(page->getSectionId(), QStringLiteral("general"));

    // An unknown section does nothing
    PropertiesTestHelper::handler(tab)->showProperties(QStringLiteral("bogus"));
    fixture.settle();
    QCOMPARE(tab->getPage(), page);
}

void AppDocumentPropertiesTest::sectionGeneral()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(PropertiesTestHelper::infoDocument(fixture.folder(), QStringLiteral("General.pdf"), false));
    QVERIFY(tab);
    pdf::PDFAppDocumentPropertiesPage* page = PropertiesTestHelper::showProperties(fixture, tab, QStringLiteral("general"));
    QVERIFY(page);

    pdf::PDFPropertyRow* title = page->findChild<pdf::PDFPropertyRow*>(QStringLiteral("property-title"));
    QVERIFY(title);
    QCOMPARE(title->getValue(), QStringLiteral("Project Report 2026"));
    QVERIFY(title->isEditable());
    pdf::PDFPropertyRow* producer = page->findChild<pdf::PDFPropertyRow*>(QStringLiteral("property-producer"));
    QVERIFY(producer);
    QVERIFY(!producer->isEditable());
    pdf::PDFPropertyRow* custom = page->findChild<pdf::PDFPropertyRow*>(QStringLiteral("property-custom"));
    QVERIFY(custom);
    QCOMPARE(custom->getKey(), QStringLiteral("Department"));
    QVERIFY(custom->isEditable());
    QVERIFY(page->findChild<QWidget*>(QStringLiteral("group-custom")));
    QCOMPARE(page->findChild<pdf::PDFLabel*>(QStringLiteral("summaryName"))->text(), QStringLiteral("General.pdf"));

    // Colors of the design (236:8882): the bar bg/subtle, the navigation bg/sidebar, the content bg/canvas, cards bg/surface with border/default
    {
        const QImage image = PropertiesTestHelper::render(fixture.window);
        auto colorAt = [&](const QWidget* widget, QPoint point) { return image.pixelColor(widget->mapTo(fixture.window, point)); };
        auto token = [](pdf::PDFThemeColors::Color color) { return pdf::PDFStylePainter::color(color); };
        const QWidget* bar = page->findChild<QWidget*>(QStringLiteral("propertiesBar"));
        const QWidget* navigation = page->findChild<QWidget*>(QStringLiteral("propertiesNavigation"));
        const QWidget* content = page->findChild<QWidget*>(QStringLiteral("propertiesContent"));
        const QWidget* card = page->findChild<QWidget*>(QStringLiteral("group-description"))->findChild<QWidget*>(QStringLiteral("card"));
        QCOMPARE(colorAt(bar, QPoint(bar->width() / 2, 4)), token(pdf::PDFThemeColors::BgSubtle));
        QCOMPARE(colorAt(navigation, QPoint(150, navigation->height() - 20)), token(pdf::PDFThemeColors::BgSidebar));
        QCOMPARE(colorAt(content, QPoint(10, 10)), token(pdf::PDFThemeColors::BgCanvas));
        QCOMPARE(colorAt(card, QPoint(card->width() / 2, card->height() - 3)), token(pdf::PDFThemeColors::BgSurface));
        QCOMPARE(card->height(), 9 * 30 + 2 * 4 + 2);
        const QPushButton* back = page->findChild<QPushButton*>(QStringLiteral("backButton"));
        QCOMPARE(back->height(), 36);
    }
    QVERIFY(page->findChild<pdf::PDFLabel*>(QStringLiteral("summaryMeta"))->text().contains(QStringLiteral("2")));
    QCOMPARE(PropertiesTestHelper::countLeakPixels(PropertiesTestHelper::render(fixture.window)), 0);
}

void AppDocumentPropertiesTest::editInfoWithUndo()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(PropertiesTestHelper::infoDocument(fixture.folder(), QStringLiteral("Edit.pdf"), false));
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppDocumentPropertiesPage* page = PropertiesTestHelper::showProperties(fixture, tab, QStringLiteral("general"));
    QVERIFY(page);
    const quint64 version = document->getVersion();

    // Title by the inline editor: one undo step
    pdf::PDFPropertyRow* title = page->findChild<pdf::PDFPropertyRow*>(QStringLiteral("property-title"));
    QVERIFY(title->startEditing());
    title->getEditor()->setText(QStringLiteral("New title"));
    title->confirmEditing();
    fixture.settle();
    QCOMPARE(document->getDocument()->getInfo()->title, QStringLiteral("New title"));
    QCOMPARE(document->getVersion(), version + 1);
    QVERIFY(document->isModified());
    QVERIFY(document->getUndoStack()->canUndo());

    // The rows follow the document (also the new row after the rebuild)
    page->rebuildNow();
    title = page->findChild<pdf::PDFPropertyRow*>(QStringLiteral("property-title"));
    QCOMPARE(title->getValue(), QStringLiteral("New title"));

    // Undo returns the old value, the page shows it
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::undo()));
    fixture.settle();
    QCOMPARE(document->getDocument()->getInfo()->title, QStringLiteral("Project Report 2026"));
    page->rebuildNow();
    QCOMPARE(page->findChild<pdf::PDFPropertyRow*>(QStringLiteral("property-title"))->getValue(), QStringLiteral("Project Report 2026"));

    // An empty value removes the entry; the custom property is edited the same way
    pdf::PDFPropertyRow* author = page->findChild<pdf::PDFPropertyRow*>(QStringLiteral("property-author"));
    QVERIFY(author->startEditing());
    author->getEditor()->clear();
    author->confirmEditing();
    fixture.settle();
    QVERIFY(document->getDocument()->getInfo()->author.isEmpty());
    page->rebuildNow();
    pdf::PDFPropertyRow* custom = page->findChild<pdf::PDFPropertyRow*>(QStringLiteral("property-custom"));
    QVERIFY(custom->startEditing());
    custom->getEditor()->setText(QStringLiteral("Legal"));
    custom->confirmEditing();
    fixture.settle();
    QCOMPARE(document->getDocument()->getInfo()->extra.at(QByteArrayLiteral("Department")).toString(), QStringLiteral("Legal"));

    // A change while a row is edited waits for the end of the editing
    page->rebuildNow();
    pdf::PDFPropertyRow* subject = page->findChild<pdf::PDFPropertyRow*>(QStringLiteral("property-subject"));
    QVERIFY(subject->startEditing());
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::undo()));
    fixture.settle();
    QVERIFY(subject->isEditing());
    QCOMPARE(page->findChild<pdf::PDFPropertyRow*>(QStringLiteral("property-subject")), subject);
    subject->cancelEditing();
    fixture.settle();
    page->rebuildNow();
    QCOMPARE(page->findChild<pdf::PDFPropertyRow*>(QStringLiteral("property-custom"))->getValue(), QStringLiteral("Finance"));
}

void AppDocumentPropertiesTest::lockedDocument()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(PropertiesTestHelper::infoDocument(fixture.folder(), QStringLiteral("Locked.pdf"), false));
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    document->setLocked(true);
    pdf::PDFAppDocumentPropertiesPage* page = PropertiesTestHelper::showProperties(fixture, tab, QStringLiteral("general"));
    QVERIFY(page);
    QVERIFY(!page->isEditable());
    QVERIFY(!page->findChild<pdf::PDFPropertyRow*>(QStringLiteral("property-title"))->isEditable());

    page = PropertiesTestHelper::showProperties(fixture, tab, QStringLiteral("xmp"));
    QVERIFY(page->getMetadataEditor()->isReadOnly());
    QVERIFY(!page->findChild<QPushButton*>(QStringLiteral("createMetadataButton"))->isEnabled());

    // Unlocked: the page follows (after the events)
    document->setLocked(false);
    fixture.settle();
    page->rebuildNow();
    QVERIFY(page->isEditable());
    QVERIFY(!page->getMetadataEditor()->isReadOnly());
    QVERIFY(page->findChild<QPushButton*>(QStringLiteral("createMetadataButton"))->isEnabled());

    // Locked between the start and the end of the editing: the change is refused
    page = PropertiesTestHelper::showProperties(fixture, tab, QStringLiteral("general"));
    pdf::PDFPropertyRow* title = page->findChild<pdf::PDFPropertyRow*>(QStringLiteral("property-title"));
    QVERIFY(title->startEditing());
    title->getEditor()->setText(QStringLiteral("Refused"));
    const quint64 version = document->getVersion();
    document->setLocked(true);
    title->confirmEditing();
    fixture.settle();
    QCOMPARE(document->getVersion(), version);
    QCOMPARE(document->getDocument()->getInfo()->title, QStringLiteral("Project Report 2026"));
    pdf::PDFAppDocumentStatusBar* statusBar = page->findChild<pdf::PDFAppDocumentStatusBar*>(QStringLiteral("propertiesStatusBar"));
    QVERIFY(statusBar->getMessage().contains(QStringLiteral("locked")));
}

void AppDocumentPropertiesTest::sectionSecurity()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::ownerPassword(fixture.folder()));
    QVERIFY(tab);
    QVERIFY(tab->getDocument()->isReady());
    pdf::PDFAppDocumentPropertiesPage* page = PropertiesTestHelper::showProperties(fixture, tab, QStringLiteral("security"));
    QVERIFY(page);
    QCOMPARE(page->findChild<pdf::PDFPropertyRow*>(QStringLiteral("property-authorized-as"))->getValue(), QStringLiteral("User"));
    QCOMPARE(page->findChildren<QWidget*>(QStringLiteral("permission")).size(), 8);
    QVERIFY(!page->findChildren<pdf::PDFLabel*>(QStringLiteral("notAllowed")).isEmpty());
    QVERIFY(page->findChild<QWidget*>(QStringLiteral("securityNote")));
    QCOMPARE(PropertiesTestHelper::countLeakPixels(PropertiesTestHelper::render(fixture.window)), 0);
}

void AppDocumentPropertiesTest::sectionFonts()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Fonts.pdf"), 3));
    QVERIFY(tab);
    pdf::PDFAppDocumentPropertiesPage* page = PropertiesTestHelper::showProperties(fixture, tab, QStringLiteral("fonts"));
    QVERIFY(page);

    // The scan runs in a worker thread, the list comes after it
    QTRY_VERIFY_WITH_TIMEOUT(!page->isFontScanRunning(), 20000);
    fixture.settle();
    page->rebuildNow();
    QVERIFY(!page->getFonts().empty());
    QListView* list = page->findChild<QListView*>(QStringLiteral("fontList"));
    QVERIFY(list);
    QCOMPARE(list->model()->rowCount(), int(page->getFonts().size()));
    QVERIFY(page->findChild<pdf::PDFLabel*>(QStringLiteral("sectionSubtitle"))->text().contains(QString::number(page->getFonts().size())));

    // A new version of the document: a new scan
    const pdf::PDFDocumentPointer changed = pdf::PDFAppDocumentPropertiesData::setInfoValue(tab->getDocument()->getDocument().data(), QByteArrayLiteral("Title"), QStringLiteral("x"));
    QVERIFY(tab->getDocument()->commit(pdf::PDFModifiedDocument(changed, tab->getDocument()->getOptionalContentActivity()), QStringLiteral("x"), tab->getDocument()->getVersion(), pdf::PDFAppChangeSource::User));
    fixture.settle();
    page->rebuildNow();
    QTRY_VERIFY_WITH_TIMEOUT(!page->isFontScanRunning(), 20000);
    fixture.settle();
    QVERIFY(!page->getFonts().empty());

    // The page deleted while the scan runs (no wait, no crash)
    fixture.window->getNavigator()->navigate(pdf::PDFAppRoutes::document(tab->getDocumentId()));
    fixture.settle();
    const pdf::PDFDocumentPointer changed2 = pdf::PDFAppDocumentPropertiesData::setInfoValue(tab->getDocument()->getDocument().data(), QByteArrayLiteral("Title"), QStringLiteral("y"));
    QVERIFY(tab->getDocument()->commit(pdf::PDFModifiedDocument(changed2, tab->getDocument()->getOptionalContentActivity()), QStringLiteral("y"), tab->getDocument()->getVersion(), pdf::PDFAppChangeSource::User));
    page = PropertiesTestHelper::showProperties(fixture, tab, QStringLiteral("fonts"));
    QVERIFY(page);
    QPointer<pdf::PDFAppDocumentPropertiesPage> guard(page);
    PropertiesTestHelper::closeTab(fixture, tab);
    fixture.settle();
    QVERIFY(!guard);
    QTest::qWait(200);
}

void AppDocumentPropertiesTest::sectionDisplay()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Display.pdf"), 1));
    QVERIFY(tab);
    pdf::PDFAppDocumentPropertiesPage* page = PropertiesTestHelper::showProperties(fixture, tab, QStringLiteral("display"));
    QVERIFY(page);
    QVERIFY(page->findChild<QWidget*>(QStringLiteral("group-viewer")));
    QVERIFY(page->findChild<QWidget*>(QStringLiteral("group-print")));
    QCOMPARE(page->findChild<pdf::PDFPropertyRow*>(QStringLiteral("property-number-of-copies"))->getValue(), QStringLiteral("1"));
}

void AppDocumentPropertiesTest::sectionXmp()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(PropertiesTestHelper::infoDocument(fixture.folder(), QStringLiteral("Xmp.pdf"), false));
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppDocumentPropertiesPage* page = PropertiesTestHelper::showProperties(fixture, tab, QStringLiteral("xmp"));
    QVERIFY(page);
    QVERIFY(page->getMetadataEditor()->toPlainText().isEmpty());

    // Create default metadata: one undo step, the packet is shown
    QPushButton* create = page->findChild<QPushButton*>(QStringLiteral("createMetadataButton"));
    QVERIFY(create->isEnabled());
    create->click();
    fixture.settle();
    page->rebuildNow();
    QVERIFY(document->getDocument()->getCatalog()->getMetadata().isReference());
    QVERIFY(page->getMetadataEditor()->toPlainText().contains(QStringLiteral("Project Report 2026")));
    QVERIFY(!page->findChild<QPushButton*>(QStringLiteral("createMetadataButton"))->isEnabled());

    // An edit shows Apply and Discard; a malformed packet is refused with its line
    QPushButton* apply = page->findChild<QPushButton*>(QStringLiteral("applyMetadataButton"));
    QPushButton* discard = page->findChild<QPushButton*>(QStringLiteral("discardMetadataButton"));
    QVERIFY(apply->isHidden());
    page->getMetadataEditor()->setPlainText(QStringLiteral("<a><b></a>"));
    QVERIFY(!apply->isHidden());
    apply->click();
    fixture.settle();
    pdf::PDFLabel* error = page->findChild<pdf::PDFLabel*>(QStringLiteral("metadataError"));
    QVERIFY(!error->isHidden());
    QVERIFY(error->text().contains(QStringLiteral("line")));

    // A change of the document during the editing does not throw the text away
    const quint64 version = document->getVersion();
    page->rebuildNow();
    QCOMPARE(page->getMetadataEditor()->toPlainText(), QStringLiteral("<a><b></a>"));

    // Discard returns the packet; a valid edit is committed
    discard->click();
    fixture.settle();
    page->rebuildNow();
    QVERIFY(page->getMetadataEditor()->toPlainText().contains(QStringLiteral("Project Report 2026")));
    page->getMetadataEditor()->setPlainText(QStringLiteral("<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"/>"));
    page->findChild<QPushButton*>(QStringLiteral("applyMetadataButton"))->click();
    fixture.settle();
    QCOMPARE(document->getVersion(), version + 1);
    bool hasMetadata = false;
    QCOMPARE(pdf::PDFAppDocumentPropertiesData::getMetadata(document->getDocument().data(), &hasMetadata, nullptr), QStringLiteral("<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"/>"));

    // Undo twice: no metadata again
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::undo()));
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::undo()));
    fixture.settle();
    page->rebuildNow();
    QVERIFY(page->getMetadataEditor()->toPlainText().isEmpty());
    QVERIFY(page->findChild<QPushButton*>(QStringLiteral("createMetadataButton"))->isEnabled());
    QCOMPARE(PropertiesTestHelper::countLeakPixels(PropertiesTestHelper::render(fixture.window)), 0);
}

void AppDocumentPropertiesTest::copyAll()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(PropertiesTestHelper::infoDocument(fixture.folder(), QStringLiteral("Copy.pdf"), false));
    QVERIFY(tab);
    pdf::PDFAppDocumentPropertiesPage* page = PropertiesTestHelper::showProperties(fixture, tab, QStringLiteral("general"));
    QVERIFY(page);
    QApplication::clipboard()->clear();
    page->findChild<QPushButton*>(QStringLiteral("copyAllButton"))->click();
    const QString text = QApplication::clipboard()->text();
    QVERIFY(text.contains(QStringLiteral("Title: Project Report 2026")));
    QVERIFY(text.contains(QStringLiteral("Permissions")));
    QVERIFY(text.contains(QStringLiteral("Viewer settings")));
}

void AppDocumentPropertiesTest::pageDeletion()
{
    // The window, the tab, the page deleted with an edited row, a scheduled rebuild
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(PropertiesTestHelper::infoDocument(fixture.folder(), QStringLiteral("Delete.pdf"), false));
    QVERIFY(tab);
    pdf::PDFAppDocumentPropertiesPage* page = PropertiesTestHelper::showProperties(fixture, tab, QStringLiteral("general"));
    pdf::PDFPropertyRow* title = page->findChild<pdf::PDFPropertyRow*>(QStringLiteral("property-title"));
    QVERIFY(title->startEditing());
    tab->getDocument()->setLocked(true);
    QPointer<pdf::PDFAppDocumentPropertiesPage> guard(page);
    PropertiesTestHelper::closeTab(fixture, tab);
    fixture.settle();
    QVERIFY(!guard);

    // A page deleted from the signal of its own row
    tab = fixture.open(PropertiesTestHelper::infoDocument(fixture.folder(), QStringLiteral("Delete2.pdf"), false));
    QVERIFY(tab);
    page = PropertiesTestHelper::showProperties(fixture, tab, QStringLiteral("general"));
    title = page->findChild<pdf::PDFPropertyRow*>(QStringLiteral("property-title"));
    QPointer<pdf::PDFAppDocumentTab> tabGuard(tab);
    connect(title, &pdf::PDFPropertyRow::edited, tab, [&fixture, tabGuard]() { PropertiesTestHelper::closeTab(fixture, tabGuard); });
    QVERIFY(title->startEditing());
    title->getEditor()->setText(QStringLiteral("Closed"));
    title->confirmEditing();
    fixture.settle();
    QVERIFY(!tabGuard);

    // The window deleted with the properties shown
    tab = fixture.open(PropertiesTestHelper::infoDocument(fixture.folder(), QStringLiteral("Delete3.pdf"), false));
    QVERIFY(PropertiesTestHelper::showProperties(fixture, tab, QStringLiteral("fonts")));
    delete fixture.window.data();
    fixture.settle();
    QTest::qWait(100);
}

void AppDocumentPropertiesTest::minimalWindowAndDirection()
{
    pdftest::AppDocumentFixture fixture(QSize(720, 480));
    pdf::PDFAppDocumentTab* tab = fixture.open(PropertiesTestHelper::infoDocument(fixture.folder(), QStringLiteral("Small.pdf"), true));
    QVERIFY(tab);
    for (const QString& section : pdf::PDFAppDocumentPropertiesPage::getSectionIds())
    {
        pdf::PDFAppDocumentPropertiesPage* page = PropertiesTestHelper::showProperties(fixture, tab, section);
        QVERIFY(page);
        fixture.settle();
        QScrollArea* area = page->findChild<QScrollArea*>(QStringLiteral("propertiesScrollArea"));
        QVERIFY(area);
        QVERIFY(!area->horizontalScrollBar()->isVisible());
        if (section == QStringLiteral("general"))
        {
            pdf::PDFAppColumns* columns = page->findChild<pdf::PDFAppColumns*>(QStringLiteral("columns"));
            QVERIFY(columns);
            QVERIFY(columns->isStacked());
        }
    }

    QApplication::setLayoutDirection(Qt::RightToLeft);
    fixture.settle();
    pdf::PDFAppDocumentPropertiesPage* page = PropertiesTestHelper::showProperties(fixture, tab, QStringLiteral("general"));
    QVERIFY(page);
    QWidget* navigation = page->findChild<QWidget*>(QStringLiteral("propertiesNavigation"));
    QVERIFY(navigation->mapTo(page, QPoint()).x() > page->width() / 2);
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Dark);
    fixture.settle();
    QCOMPARE(PropertiesTestHelper::countLeakPixels(PropertiesTestHelper::render(fixture.window)), 0);
}

void AppDocumentPropertiesTest::urlRules()
{
    using Handler = pdf::PDFAppDocumentActionHandler;

    QCOMPARE(Handler::resolveUri(QByteArray(), QByteArray("https://example.com/a")), QUrl(QStringLiteral("https://example.com/a")));
    QCOMPARE(Handler::resolveUri(QByteArray("https://example.com/docs/"), QByteArray("page.html")), QUrl(QStringLiteral("https://example.com/docs/page.html")));
    QCOMPARE(Handler::resolveUri(QByteArray("https://example.com/docs/"), QByteArray("https://other.org/")), QUrl(QStringLiteral("https://other.org/")));
    QVERIFY(!Handler::resolveUri(QByteArray(), QByteArray()).isValid());
    QVERIFY(!Handler::resolveUri(QByteArray(), QByteArray(Handler::MAXIMUM_URL_LENGTH + 1, 'a')).isValid());

    QVERIFY(Handler::isAllowedUrl(QUrl(QStringLiteral("https://example.com/"))));
    QVERIFY(Handler::isAllowedUrl(QUrl(QStringLiteral("HTTP://example.com/"))));
    QVERIFY(Handler::isAllowedUrl(QUrl(QStringLiteral("mailto:jane@example.com"))));
    QVERIFY(!Handler::isAllowedUrl(QUrl(QStringLiteral("mailto:"))));
    QVERIFY(!Handler::isAllowedUrl(QUrl(QStringLiteral("https://"))));
    QVERIFY(!Handler::isAllowedUrl(QUrl(QStringLiteral("file:///C:/Windows/notepad.exe"))));
    QVERIFY(!Handler::isAllowedUrl(QUrl(QStringLiteral("javascript:alert(1)"))));
    QVERIFY(!Handler::isAllowedUrl(QUrl(QStringLiteral("ftp://example.com/"))));
    QVERIFY(!Handler::isAllowedUrl(QUrl(QStringLiteral("smb://server/share"))));
    QVERIFY(!Handler::isAllowedUrl(QUrl()));

    QVERIFY(Handler::isExecutable(QStringLiteral("C:/a/tool.exe")));
    QVERIFY(Handler::isExecutable(QStringLiteral("/a/run.SH")));
    QVERIFY(Handler::isExecutable(QStringLiteral("x.bat")));
    QVERIFY(!Handler::isExecutable(QStringLiteral("x.xlsx")));
    QVERIFY(!Handler::isExecutable(QStringLiteral("x.pdf")));

    QCOMPARE(pdf::PDFAppDocumentActionDialogs::shorten(QStringLiteral("abcdefghij"), 5), QStringLiteral("ab…ij"));
    QCOMPARE(pdf::PDFAppDocumentActionDialogs::shorten(QStringLiteral("abc"), 5), QStringLiteral("abc"));
}

void AppDocumentPropertiesTest::linkDialogs()
{
    QWidget host;
    host.resize(1000, 700);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));

    // Open the web link? 440: Copy Link / Cancel / Open Link (primary), the address panel
    pdf::PDFMessageBox* link = pdf::PDFAppDocumentActionDialogs::createLinkDialog(&host, QStringLiteral("Report.pdf"), QUrl(QStringLiteral("https://market-insight.example/reports/2026?ref=pdf")));
    QCOMPARE(link->getCardWidth(), 440);
    QCOMPARE(link->getBadgeIconName(), QStringLiteral("external-link"));
    QList<QPushButton*> buttons = link->getActionButtons();
    QCOMPARE(buttons.size(), 3);
    QCOMPARE(buttons[0]->objectName(), QStringLiteral("copyLinkButton"));
    QCOMPARE(buttons[2]->objectName(), QStringLiteral("openLinkButton"));
    QVERIFY(buttons[2]->isDefault());
    QCOMPARE(link->findChild<QLabel*>(QStringLiteral("addressTitle"))->text(), QStringLiteral("market-insight.example"));
    delete link;

    pdf::PDFMessageBox* mail = pdf::PDFAppDocumentActionDialogs::createLinkDialog(&host, QStringLiteral("Report.pdf"), QUrl(QStringLiteral("mailto:jane@example.com")));
    QCOMPARE(mail->getBadgeIconName(), QStringLiteral("mail"));
    QCOMPARE(mail->findChild<QLabel*>(QStringLiteral("addressTitle"))->text(), QStringLiteral("jane@example.com"));
    delete mail;

    // Start a program / Open a file 520: the safe button is primary
    pdf::PDFMessageBox* program = pdf::PDFAppDocumentActionDialogs::createProgramDialog(&host, QStringLiteral("Report.pdf"), QStringLiteral("C:/Tools/Viewer.exe"), QStringLiteral("/open x"), QStringLiteral("C:/Tools"));
    QCOMPARE(program->getCardWidth(), 520);
    buttons = program->getActionButtons();
    QCOMPARE(buttons.size(), 2);
    QCOMPARE(buttons[1]->objectName(), QStringLiteral("dontStartButton"));
    QVERIFY(buttons[1]->isDefault());
    pdf::PDFKeyValueList* facts = program->findChild<pdf::PDFKeyValueList*>(QStringLiteral("factList"));
    QCOMPARE(facts->getRowCount(), 3);
    QCOMPARE(facts->getValue(1), QStringLiteral("/open x"));
    delete program;

    pdf::PDFMessageBox* file = pdf::PDFAppDocumentActionDialogs::createFileDialog(&host, QStringLiteral("Report.pdf"), QStringLiteral("C:/Data/Budget.xlsx"));
    QCOMPARE(file->getActionButtons()[1]->objectName(), QStringLiteral("dontOpenButton"));
    QCOMPARE(file->findChild<pdf::PDFKeyValueList*>(QStringLiteral("factList"))->getValue(0), QStringLiteral("Budget.xlsx"));
    delete file;

    // run: Enter = the primary button, Esc = rejected, a deleted parent = rejected
    PropertiesTestHelper::clickWhenShown(QStringLiteral("linkConfirmDialog"), QStringLiteral("enter"));
    QCOMPARE(pdf::PDFAppDocumentActionDialogs::confirmLink(&host, QStringLiteral("R.pdf"), QUrl(QStringLiteral("https://example.com/"))), pdf::PDFAppDocumentActionDialogs::Result::Accepted);
    PropertiesTestHelper::clickWhenShown(QStringLiteral("linkConfirmDialog"), QStringLiteral("escape"));
    QCOMPARE(pdf::PDFAppDocumentActionDialogs::confirmLink(&host, QStringLiteral("R.pdf"), QUrl(QStringLiteral("https://example.com/"))), pdf::PDFAppDocumentActionDialogs::Result::Rejected);
    PropertiesTestHelper::clickWhenShown(QStringLiteral("linkConfirmDialog"), QStringLiteral("copyLinkButton"));
    QCOMPARE(pdf::PDFAppDocumentActionDialogs::confirmLink(&host, QStringLiteral("R.pdf"), QUrl(QStringLiteral("https://example.com/"))), pdf::PDFAppDocumentActionDialogs::Result::CopyLink);
    PropertiesTestHelper::clickWhenShown(QStringLiteral("launchConfirmDialog"), QStringLiteral("enter"));
    QCOMPARE(pdf::PDFAppDocumentActionDialogs::confirmProgram(&host, QStringLiteral("R.pdf"), QStringLiteral("a.exe"), QString(), QString()), pdf::PDFAppDocumentActionDialogs::Result::Rejected);
    PropertiesTestHelper::clickWhenShown(QStringLiteral("launchFileConfirmDialog"), QStringLiteral("openFileButton"));
    QCOMPARE(pdf::PDFAppDocumentActionDialogs::confirmFile(&host, QStringLiteral("R.pdf"), QStringLiteral("a.xlsx")), pdf::PDFAppDocumentActionDialogs::Result::Accepted);

    QWidget* parent = new QWidget(&host);
    parent->show();
    QTimer::singleShot(50, parent, [parent]() { parent->deleteLater(); });
    QCOMPARE(pdf::PDFAppDocumentActionDialogs::confirmFile(parent, QStringLiteral("R.pdf"), QStringLiteral("a.xlsx")), pdf::PDFAppDocumentActionDialogs::Result::Rejected);
    QCOMPARE(pdf::PDFAppDocumentActionDialogs::run(nullptr), pdf::PDFAppDocumentActionDialogs::Result::Rejected);
}

void AppDocumentPropertiesTest::webLinks()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = PropertiesTestHelper::openReady(fixture, pdftest::AppDocumentSamples::links(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppDocumentActionHandler* handler = PropertiesTestHelper::handler(tab)->getActionHandler();
    QVERIFY(handler);
    std::vector<pdf::PDFAnnotationPtr> annotations;
    const std::vector<const pdf::PDFAction*> actions = PropertiesTestHelper::linkActions(tab->getDocument()->getDocument().data(), annotations);
    QCOMPARE(actions.size(), size_t(6));

    QList<pdf::PDFAppExternalLaunch> launched;
    pdf::PDFAppDocumentActionHandler::setLaunchHandler([&launched](const pdf::PDFAppExternalLaunch& request) { launched << request; return true; });

    // Open Link: the address goes to the browser
    QStringList texts;
    PropertiesTestHelper::clickWhenShown(QStringLiteral("linkConfirmDialog"), QStringLiteral("openLinkButton"), &texts);
    handler->performAction(actions[2]);
    fixture.settle();
    QCOMPARE(launched.size(), 1);
    QCOMPARE(launched[0].kind, pdf::PDFAppExternalLaunch::Kind::Url);
    QCOMPARE(launched[0].url, QUrl(QStringLiteral("https://example.com/")));
    QVERIFY(texts.contains(QStringLiteral("example.com")));

    // Cancel: nothing; Copy Link: the clipboard
    PropertiesTestHelper::clickWhenShown(QStringLiteral("linkConfirmDialog"), QStringLiteral("cancelButton"));
    handler->performAction(actions[2]);
    QCOMPARE(launched.size(), 1);
    QApplication::clipboard()->clear();
    PropertiesTestHelper::clickWhenShown(QStringLiteral("linkConfirmDialog"), QStringLiteral("copyLinkButton"));
    handler->performAction(actions[2]);
    QCOMPARE(launched.size(), 1);
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("https://example.com/"));

    // A failing browser: a status message
    pdf::PDFAppDocumentActionHandler::setLaunchHandler([](const pdf::PDFAppExternalLaunch&) { return false; });
    PropertiesTestHelper::clickWhenShown(QStringLiteral("linkConfirmDialog"), QStringLiteral("openLinkButton"));
    handler->performAction(actions[2]);
    QVERIFY(tab->getView()->getStatusBar()->getMessage().contains(QStringLiteral("could not be opened")));

    // Other schemes are never opened (no dialog)
    const pdf::PDFDocument* document = tab->getDocument()->getDocument().data();
    const pdf::PDFActionPtr fileUri = PropertiesTestHelper::parseAction(document, PropertiesTestHelper::dictionary({ { "S", PropertiesTestHelper::name("URI") },
                                                                                                                    { "URI", pdf::PDFObject::createString(QByteArray("file:///etc/passwd")) } }));
    QVERIFY(fileUri);
    handler->performAction(fileUri.data());
    fixture.settle();
    const pdf::PDFAppInfoBarRequest* bar = tab->getView()->findInfoBar(pdf::PDFAppDocumentActionHandler::getLinkBlockedBarId());
    QVERIFY(bar);
    QVERIFY(bar->title.contains(QStringLiteral("only web and e-mail")));
}

void AppDocumentPropertiesTest::webLinksBlocked()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = PropertiesTestHelper::openReady(fixture, pdftest::AppDocumentSamples::links(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppDocumentActionHandler* handler = PropertiesTestHelper::handler(tab)->getActionHandler();
    std::vector<pdf::PDFAnnotationPtr> annotations;
    const std::vector<const pdf::PDFAction*> actions = PropertiesTestHelper::linkActions(tab->getDocument()->getDocument().data(), annotations);

    int launched = 0;
    pdf::PDFAppDocumentActionHandler::setLaunchHandler([&launched](const pdf::PDFAppExternalLaunch&) { ++launched; return true; });
    QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::securityOpenLinks(), false).isError());

    // No dialog: the info bar link-blocked with Copy Link and Security Settings
    handler->performAction(actions[2]);
    fixture.settle();
    QCOMPARE(launched, 0);
    const pdf::PDFAppInfoBarRequest* bar = tab->getView()->findInfoBar(pdf::PDFAppDocumentActionHandler::getLinkBlockedBarId());
    QVERIFY(bar);
    QCOMPARE(bar->title, QStringLiteral("The web link was not opened – web links are turned off"));
    QCOMPARE(bar->actions.size(), size_t(2));

    QApplication::clipboard()->clear();
    Q_EMIT tab->getView()->infoBarActionTriggered(pdf::PDFAppDocumentActionHandler::getLinkBlockedBarId(), pdf::PDFAppDocumentActionHandler::getCopyLinkActionId());
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("https://example.com/"));

    // Security Settings: the settings of the security in the Home tab
    Q_EMIT tab->getView()->infoBarActionTriggered(pdf::PDFAppDocumentActionHandler::getLinkBlockedBarId(), pdf::PDFAppDocumentActionHandler::getSecuritySettingsActionId());
    fixture.settle();
    pdf::PDFAppTab* current = fixture.window->getCurrentTab();
    QVERIFY(current);
    QVERIFY(current != tab);
    QCOMPARE(current->getHistory().getCurrentEntry()->route.getSegments().value(0), QStringLiteral("settings"));

    // Bars of other ids are not handled
    Q_EMIT tab->getView()->infoBarActionTriggered(QStringLiteral("printing"), pdf::PDFAppDocumentActionHandler::getCopyLinkActionId());
}

void AppDocumentPropertiesTest::launchActions()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = PropertiesTestHelper::openReady(fixture, pdftest::AppDocumentSamples::links(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppDocumentActionHandler* handler = PropertiesTestHelper::handler(tab)->getActionHandler();
    std::vector<pdf::PDFAnnotationPtr> annotations;
    const std::vector<const pdf::PDFAction*> actions = PropertiesTestHelper::linkActions(tab->getDocument()->getDocument().data(), annotations);
    QList<pdf::PDFAppExternalLaunch> launched;
    pdf::PDFAppDocumentActionHandler::setLaunchHandler([&launched](const pdf::PDFAppExternalLaunch& request) { launched << request; return true; });

    // Off by default: the info bar, nothing starts
    handler->performAction(actions[3]);
    fixture.settle();
    QVERIFY(tab->getView()->findInfoBar(pdf::PDFAppDocumentActionHandler::getLaunchBlockedBarId()));
    QVERIFY(launched.isEmpty());

    // On: a missing program is reported
    QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::securityLaunchFiles(), true).isError());
    handler->performAction(actions[3]);
    fixture.settle();
    QVERIFY(tab->getView()->findInfoBar(pdf::PDFAppDocumentActionHandler::getLinkFailedBarId()));
    QVERIFY(launched.isEmpty());

    // A program of the folder of the document: Start a program (Don't Start / Start Program)
    QFile program(fixture.path(QStringLiteral("notepad.exe")));
    QVERIFY(program.open(QIODevice::WriteOnly));
    program.write("MZ");
    program.close();
    QStringList texts;
    PropertiesTestHelper::clickWhenShown(QStringLiteral("launchConfirmDialog"), QStringLiteral("dontStartButton"), &texts);
    handler->performAction(actions[3]);
    QVERIFY(launched.isEmpty());
    PropertiesTestHelper::clickWhenShown(QStringLiteral("launchConfirmDialog"), QStringLiteral("startProgramButton"));
    handler->performAction(actions[3]);
    QCOMPARE(launched.size(), 1);
    QCOMPARE(launched[0].kind, pdf::PDFAppExternalLaunch::Kind::Program);
    QCOMPARE(QFileInfo(launched[0].filePath).fileName(), QStringLiteral("notepad.exe"));

    // A file (not a program): Open a file
    const pdf::PDFDocument* document = tab->getDocument()->getDocument().data();
    QFile data(fixture.path(QStringLiteral("Budget.xlsx")));
    QVERIFY(data.open(QIODevice::WriteOnly));
    data.write("data");
    data.close();
    const pdf::PDFActionPtr fileLaunch = PropertiesTestHelper::parseAction(document, PropertiesTestHelper::dictionary({ { "S", PropertiesTestHelper::name("Launch") },
                                                                                                                       { "F", pdf::PDFObject::createString(QByteArray("Budget.xlsx")) } }));
    QVERIFY(fileLaunch);
    PropertiesTestHelper::clickWhenShown(QStringLiteral("launchFileConfirmDialog"), QStringLiteral("openFileButton"), &texts);
    handler->performAction(fileLaunch.data());
    QCOMPARE(launched.size(), 2);
    QCOMPARE(launched[1].kind, pdf::PDFAppExternalLaunch::Kind::File);
    QVERIFY(texts.contains(QStringLiteral("Budget.xlsx")));

    // A PDF file opens in a new tab (as GoToR)
    const QString other = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Second.pdf"), 2);
    const pdf::PDFActionPtr pdfLaunch = PropertiesTestHelper::parseAction(document, PropertiesTestHelper::dictionary({ { "S", PropertiesTestHelper::name("Launch") },
                                                                                                                      { "F", pdf::PDFObject::createString(QByteArray("Second.pdf")) } }));
    handler->performAction(pdfLaunch.data());
    fixture.settle();
    QTRY_VERIFY(fixture.currentDocumentTab() && fixture.currentDocumentTab()->getDocument()->getFilePath() == QFileInfo(other).canonicalFilePath());
    QCOMPARE(launched.size(), 2);

    // A Launch without a file
    const pdf::PDFActionPtr empty = PropertiesTestHelper::parseAction(document, PropertiesTestHelper::dictionary({ { "S", PropertiesTestHelper::name("Launch") } }));
    if (empty)
    {
        handler->performAction(empty.data());
    }
    QCOMPARE(launched.size(), 2);
}

void AppDocumentPropertiesTest::otherActions()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = PropertiesTestHelper::openReady(fixture, pdftest::AppDocumentSamples::links(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppDocumentActionHandler* handler = PropertiesTestHelper::handler(tab)->getActionHandler();
    std::vector<pdf::PDFAnnotationPtr> annotations;
    const std::vector<const pdf::PDFAction*> actions = PropertiesTestHelper::linkActions(tab->getDocument()->getDocument().data(), annotations);
    const pdf::PDFDocument* document = tab->getDocument()->getDocument().data();

    // GoTo page 3, the named destination (page 4)
    handler->performAction(actions[0]);
    fixture.settle();
    QTRY_COMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(2));
    handler->performAction(actions[1]);
    fixture.settle();
    QTRY_COMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(3));

    // A missing named destination: a status message, the page stays
    const pdf::PDFActionPtr missing = PropertiesTestHelper::parseAction(document, PropertiesTestHelper::dictionary({ { "S", PropertiesTestHelper::name("GoTo") },
                                                                                                                    { "D", pdf::PDFObject::createString(QByteArray("nowhere")) } }));
    handler->performAction(missing.data());
    QVERIFY(view->getStatusBar()->getMessage().contains(QStringLiteral("nowhere")));
    QCOMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(3));

    // Named actions
    const pdf::PDFActionPtr first = PropertiesTestHelper::parseAction(document, PropertiesTestHelper::dictionary({ { "S", PropertiesTestHelper::name("Named") }, { "N", PropertiesTestHelper::name("FirstPage") } }));
    handler->performAction(first.data());
    fixture.settle();
    QTRY_COMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(0));
    const pdf::PDFActionPtr next = PropertiesTestHelper::parseAction(document, PropertiesTestHelper::dictionary({ { "S", PropertiesTestHelper::name("Named") }, { "N", PropertiesTestHelper::name("NextPage") } }));
    handler->performAction(next.data());
    fixture.settle();
    QTRY_COMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(1));
    const pdf::PDFActionPtr last = PropertiesTestHelper::parseAction(document, PropertiesTestHelper::dictionary({ { "S", PropertiesTestHelper::name("Named") }, { "N", PropertiesTestHelper::name("LastPage") } }));
    handler->performAction(last.data());
    fixture.settle();
    QTRY_COMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(3));
    const pdf::PDFActionPtr previous = PropertiesTestHelper::parseAction(document, PropertiesTestHelper::dictionary({ { "S", PropertiesTestHelper::name("Named") }, { "N", PropertiesTestHelper::name("PrevPage") } }));
    handler->performAction(previous.data());
    fixture.settle();
    QTRY_COMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(2));
    const pdf::PDFActionPtr custom = PropertiesTestHelper::parseAction(document, PropertiesTestHelper::dictionary({ { "S", PropertiesTestHelper::name("Named") }, { "N", PropertiesTestHelper::name("Print") } }));
    handler->performAction(custom.data());
    QVERIFY(view->getStatusBar()->getMessage().contains(QStringLiteral("Print")));

    // JavaScript is not run: the info bar
    QVERIFY(!view->findInfoBar(pdf::PDFAppDocumentActionHandler::getScriptsBarId()));
    handler->performAction(actions[5]);
    QVERIFY(view->findInfoBar(pdf::PDFAppDocumentActionHandler::getScriptsBarId()));

    // SubmitForm is blocked
    const pdf::PDFActionPtr submit = PropertiesTestHelper::parseAction(document, PropertiesTestHelper::dictionary({ { "S", PropertiesTestHelper::name("SubmitForm") },
                                                                                                                   { "F", pdf::PDFObject::createString(QByteArray("https://example.com/submit")) } }));
    QVERIFY(submit);
    handler->performAction(submit.data());
    QVERIFY(view->findInfoBar(pdf::PDFAppDocumentActionHandler::getSubmitBlockedBarId()));

    // An unsupported action, a null action
    const pdf::PDFActionPtr sound = PropertiesTestHelper::parseAction(document, PropertiesTestHelper::dictionary({ { "S", PropertiesTestHelper::name("Hide") }, { "T", pdf::PDFObject::createString(QByteArray("x")) } }));
    if (sound)
    {
        handler->performAction(sound.data());
        QVERIFY(view->getStatusBar()->getMessage().contains(QStringLiteral("not supported")));
    }
    handler->performAction(nullptr);

    // GoToR: a missing document (info bar), an existing one in a new tab at its page
    handler->performAction(actions[4]);
    fixture.settle();
    QVERIFY(view->findInfoBar(pdf::PDFAppDocumentActionHandler::getLinkFailedBarId()));
    const QString other = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("other.pdf"), 3);
    const pdf::PDFActionPtr remote = PropertiesTestHelper::parseAction(document, PropertiesTestHelper::dictionary({ { "S", PropertiesTestHelper::name("GoToR") },
                                                                                                                   { "F", pdf::PDFObject::createString(QByteArray("other.pdf")) },
                                                                                                                   { "D", pdf::PDFObject::createArray([]() { pdf::PDFArrayBuilder b; b.appendItem(pdf::PDFObject::createInteger(2)); b.appendItem(PropertiesTestHelper::name("Fit")); return b; }()) } }));
    QVERIFY(remote);
    handler->performAction(remote.data());
    fixture.settle();
    QTRY_VERIFY(fixture.currentDocumentTab() && fixture.currentDocumentTab() != tab);
    QCOMPARE(fixture.currentDocumentTab()->getDocument()->getFilePath(), QFileInfo(other).canonicalFilePath());

    // Set the state of layers: the activity of the document changes (a document without layers keeps it)
    const pdf::PDFActionPtr layers = PropertiesTestHelper::parseAction(document, PropertiesTestHelper::dictionary({ { "S", PropertiesTestHelper::name("SetOCGState") },
                                                                                                                   { "State", pdf::PDFObject::createArray([]() { pdf::PDFArrayBuilder b; b.appendItem(PropertiesTestHelper::name("Toggle")); return b; }()) } }));
    if (layers)
    {
        handler->performAction(layers.data());
    }
}

void AppDocumentPropertiesTest::linkHover()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = PropertiesTestHelper::openReady(fixture, pdftest::AppDocumentSamples::links(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppDocumentActionHandler* handler = PropertiesTestHelper::handler(tab)->getActionHandler();
    QWidget* drawWidget = view->getPdfWidget()->getDrawWidget()->getWidget();
    view->getProxy()->performOperation(pdf::PDFDrawWidgetProxy::ZoomFit);
    fixture.settle();
    QTest::qWait(50);
    const QString permanent = view->getStatusBar()->getMessage();

    // The web link (page rectangle 30, 140, 200, 16 in the coordinates of PDF)
    const QPoint webLink = PropertiesTestHelper::pagePoint(view, QPointF(100, 148));
    QVERIFY(drawWidget->rect().contains(webLink));
    PropertiesTestHelper::sendMouse(drawWidget, QEvent::MouseMove, webLink, Qt::NoButton, Qt::NoButton);
    QTRY_COMPARE(handler->getHoverTitle(), QStringLiteral("https://example.com/"));
    QCOMPARE(handler->getHoverDescription(), QStringLiteral("Web link · opens in your web browser"));
    QVERIFY(view->getStatusBar()->getMessage().startsWith(QStringLiteral("Web link: https://example.com/")));

    // The tooltip of the design
    QHelpEvent help(QEvent::ToolTip, webLink, drawWidget->mapToGlobal(webLink));
    QApplication::sendEvent(drawWidget, &help);
    QVERIFY(pdf::PDFToolTip::isVisible());
    QCOMPARE(pdf::PDFToolTip::getTitle(), QStringLiteral("https://example.com/"));

    // The link to a page of the document
    const QPoint goTo = PropertiesTestHelper::pagePoint(view, QPointF(100, 108));
    PropertiesTestHelper::sendMouse(drawWidget, QEvent::MouseMove, goTo, Qt::NoButton, Qt::NoButton);
    QTRY_COMPARE(handler->getHoverTitle(), QStringLiteral("Page 3"));
    QVERIFY(!pdf::PDFToolTip::isVisible() || pdf::PDFToolTip::getTitle() != QStringLiteral("https://example.com/"));

    // Launch, GoToR, JavaScript
    PropertiesTestHelper::sendMouse(drawWidget, QEvent::MouseMove, PropertiesTestHelper::pagePoint(view, QPointF(100, 168)), Qt::NoButton, Qt::NoButton);
    QTRY_COMPARE(handler->getHoverTitle(), QStringLiteral("notepad.exe"));
    PropertiesTestHelper::sendMouse(drawWidget, QEvent::MouseMove, PropertiesTestHelper::pagePoint(view, QPointF(100, 188)), Qt::NoButton, Qt::NoButton);
    QTRY_COMPARE(handler->getHoverTitle(), QStringLiteral("other.pdf"));
    PropertiesTestHelper::sendMouse(drawWidget, QEvent::MouseMove, PropertiesTestHelper::pagePoint(view, QPointF(100, 208)), Qt::NoButton, Qt::NoButton);
    QTRY_COMPARE(handler->getHoverTitle(), QStringLiteral("Script"));

    // Outside of a link: the hover and the message end
    PropertiesTestHelper::sendMouse(drawWidget, QEvent::MouseMove, PropertiesTestHelper::pagePoint(view, QPointF(280, 380)), Qt::NoButton, Qt::NoButton);
    QTRY_VERIFY(handler->getHoverTitle().isEmpty());
    QTRY_COMPARE(view->getStatusBar()->getMessage(), permanent);

    // Leave ends it as well
    PropertiesTestHelper::sendMouse(drawWidget, QEvent::MouseMove, webLink, Qt::NoButton, Qt::NoButton);
    QTRY_VERIFY(!handler->getHoverTitle().isEmpty());
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(drawWidget, &leave);
    QVERIFY(handler->getHoverTitle().isEmpty());
    pdf::PDFToolTip::hideText();
}

void AppDocumentPropertiesTest::linkClick()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = PropertiesTestHelper::openReady(fixture, pdftest::AppDocumentSamples::links(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    QWidget* drawWidget = view->getPdfWidget()->getDrawWidget()->getWidget();
    view->getProxy()->performOperation(pdf::PDFDrawWidgetProxy::ZoomFit);
    fixture.settle();

    // A click on the link to page 3 goes there (the annotation manager triggers the action)
    const QPoint goTo = PropertiesTestHelper::pagePoint(view, QPointF(100, 108));
    PropertiesTestHelper::sendMouse(drawWidget, QEvent::MouseMove, goTo, Qt::NoButton, Qt::NoButton);
    QSignalSpy triggered(view->getAnnotationManager(), &pdf::PDFWidgetAnnotationManager::actionTriggered);
    PropertiesTestHelper::sendMouse(drawWidget, QEvent::MouseButtonPress, goTo, Qt::LeftButton, Qt::LeftButton);
    PropertiesTestHelper::sendMouse(drawWidget, QEvent::MouseButtonRelease, goTo, Qt::LeftButton, Qt::NoButton);
    fixture.settle();
    QTRY_COMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(2));
}

void AppDocumentPropertiesTest::actionLifetime()
{
    // The tab closed while the confirmation of a link is shown
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = PropertiesTestHelper::openReady(fixture, pdftest::AppDocumentSamples::links(fixture.folder()));
    QVERIFY(tab);
    QPointer<pdf::PDFAppDocumentActionHandler> handler = PropertiesTestHelper::handler(tab)->getActionHandler();
    std::vector<pdf::PDFAnnotationPtr> annotations;
    const std::vector<const pdf::PDFAction*> actions = PropertiesTestHelper::linkActions(tab->getDocument()->getDocument().data(), annotations);
    int launched = 0;
    pdf::PDFAppDocumentActionHandler::setLaunchHandler([&launched](const pdf::PDFAppExternalLaunch&) { ++launched; return true; });

    QPointer<pdf::PDFAppDocumentTab> tabGuard(tab);
    QTimer::singleShot(100, fixture.window.data(), [&fixture, tabGuard]()
    {
        // The tab goes away with its dialog (a request of another part of the application)
        if (tabGuard)
        {
            delete tabGuard.data();
        }
        Q_UNUSED(fixture);
    });
    handler->performAction(actions[2]);
    fixture.settle();
    QVERIFY(!tabGuard);
    QVERIFY(!handler);
    QCOMPARE(launched, 0);

    // The document changed while the dialog is shown: the chain stops
    tab = PropertiesTestHelper::openReady(fixture, pdftest::AppDocumentSamples::links(fixture.folder()));
    if (!tab)
    {
        tab = fixture.currentDocumentTab();
    }
    QVERIFY(tab);
    handler = PropertiesTestHelper::handler(tab)->getActionHandler();
    annotations.clear();
    const std::vector<const pdf::PDFAction*> actions2 = PropertiesTestHelper::linkActions(tab->getDocument()->getDocument().data(), annotations);
    pdf::PDFAppDocument* document = tab->getDocument();
    const pdf::PDFActionPtr chain = PropertiesTestHelper::parseAction(document->getDocument().data(),
                                                                      PropertiesTestHelper::dictionary({ { "S", PropertiesTestHelper::name("URI") }, { "URI", pdf::PDFObject::createString(QByteArray("https://example.com/")) },
                                                                                                         { "Next", PropertiesTestHelper::dictionary({ { "S", PropertiesTestHelper::name("Named") }, { "N", PropertiesTestHelper::name("LastPage") } }) } }));
    QTimer::singleShot(100, document, [document]()
    {
        const pdf::PDFDocumentPointer changed = pdf::PDFAppDocumentPropertiesData::setInfoValue(document->getDocument().data(), QByteArrayLiteral("Title"), QStringLiteral("changed"));
        document->commit(pdf::PDFModifiedDocument(changed, document->getOptionalContentActivity()), QStringLiteral("x"), document->getVersion(), pdf::PDFAppChangeSource::User);
    });
    PropertiesTestHelper::clickWhenShown(QStringLiteral("linkConfirmDialog"), QStringLiteral("cancelButton"));
    QTimer::singleShot(250, fixture.window.data(), [&fixture]() { fixture.closeMessageBoxes(); });
    handler->performAction(chain.data());
    fixture.settle();
    QCOMPARE(tab->getView()->getCurrentPageIndex(), pdf::PDFInteger(0));
    Q_UNUSED(actions2);
}

void AppDocumentPropertiesTest::renderingIssues()
{
    // The window fits on the screen of the offscreen platform (800 x 800, the window at 40, 40), the popover is not moved by the screen
    pdftest::AppDocumentFixture fixture(QSize(720, 560));
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Issues.pdf"), 5));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppRenderingIssues* issues = PropertiesTestHelper::handler(tab)->getRenderingIssues();
    QVERIFY(issues);
    QToolButton* item = issues->getStatusItem();
    QVERIFY(item);
    QVERIFY(item->isHidden());
    QVERIFY(!issues->showPopover());

    // Errors of the compiled pages: the item and the command
    QSignalSpy changed(issues, &pdf::PDFAppRenderingIssues::issuesChanged);
    Q_EMIT view->getProxy()->renderingError(4, { pdf::PDFRenderError(pdf::RenderErrorType::Error, QStringLiteral("Invalid arguments of the operator Tj.")) });
    Q_EMIT view->getProxy()->renderingError(1, { pdf::PDFRenderError(pdf::RenderErrorType::Warning, QStringLiteral("Font ArialMT is not embedded.")),
                                                 pdf::PDFRenderError(pdf::RenderErrorType::NotSupported, QStringLiteral("Shading of type 7.")) });
    QTRY_COMPARE(issues->getIssueCount(), 3);
    QTRY_VERIFY(!item->isHidden());
    QCOMPARE(item->text(), QStringLiteral("3 rendering issues"));
    QVERIFY(changed.count() >= 1);
    QVERIFY(tab->isCommandEnabled(pdf::PDFAppCommands::renderingIssues()));
    const std::vector<pdf::PDFAppRenderingIssue> list = issues->getIssues();
    QCOMPARE(list[0].pageIndex, pdf::PDFInteger(1));
    QCOMPARE(list[2].severity, pdf::PDFComponentPainter::IssueSeverity::Error);

    // The popover (command and click on the item): rows, the count, a click shows the page
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::renderingIssues()));
    fixture.settle();
    QVERIFY(issues->isPopoverVisible());
    pdf::PDFAppRenderingIssuesPanel* panel = issues->getPanel();
    QVERIFY(panel);
    QCOMPARE(panel->getRows().size(), size_t(3));
    QCOMPARE(panel->findChild<pdf::PDFTag*>(QStringLiteral("issueCount"))->text(), QStringLiteral("3"));
    QVERIFY(panel->getRows().front()->hasFocus() || !QApplication::focusWidget());

    // The popover lies above the item, aligned with its trailing edge
    const QRect itemRect(item->mapToGlobal(QPoint()), item->size());
    const QRect panelRect = issues->getPopover()->geometry().marginsRemoved(issues->getPopover()->contentsMargins());
    QCOMPARE(panelRect.width(), 440);
    QVERIFY(qAbs(panelRect.bottom() + 1 + 6 - itemRect.top()) <= 1);
    QVERIFY(qAbs(panelRect.right() - itemRect.right()) <= 2);
    const QRect innerRect(panel->mapToGlobal(QPoint()), panel->size());
    QVERIFY(qAbs(innerRect.left() + panel->layout()->contentsMargins().left() - panelRect.left() - 16) <= 1);

    // Arrows move between the rows, Enter shows the page
    QWidget* firstRow = panel->getRows()[0];
    firstRow->setFocus();
    QTest::keyClick(firstRow, Qt::Key_Down);
    QVERIFY(panel->getRows()[1]->hasFocus());
    QTest::keyClick(panel->getRows()[1], Qt::Key_Up);
    QVERIFY(firstRow->hasFocus());
    static_cast<QAbstractButton*>(panel->getRows()[2])->click();
    fixture.settle();
    QVERIFY(!issues->isPopoverVisible());
    QTRY_COMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(4));

    // The close button, the click on the item twice
    item->click();
    fixture.settle();
    QVERIFY(issues->isPopoverVisible());
    issues->getPanel()->getCloseButton()->click();
    fixture.settle();
    QVERIFY(!issues->isPopoverVisible());
    item->click();
    fixture.settle();
    QVERIFY(issues->isPopoverVisible());
    QCOMPARE(PropertiesTestHelper::countLeakPixels(PropertiesTestHelper::render(issues->getPopover())), 0);
    item->click();
    fixture.settle();
    QVERIFY(!issues->isPopoverVisible());

    // The status item of the page Document properties follows the number; its click shows the popover over the document
    pdf::PDFAppDocumentPropertiesPage* page = PropertiesTestHelper::showProperties(fixture, tab, QStringLiteral("general"));
    QToolButton* pageItem = page->findChild<QToolButton*>(QStringLiteral("statusIssues"));
    QVERIFY(pageItem);
    QCOMPARE(pageItem->text(), QStringLiteral("3 rendering issues"));
    QVERIFY(!pageItem->isHidden());
    pageItem->click();
    fixture.settle();
    QTRY_VERIFY(tab->isViewShown());
    QTRY_VERIFY(issues->isPopoverVisible());
    issues->hidePopover();
    fixture.settle();

    // A new version of the document clears the issues
    const pdf::PDFDocumentPointer modified = pdf::PDFAppDocumentPropertiesData::setInfoValue(tab->getDocument()->getDocument().data(), QByteArrayLiteral("Title"), QStringLiteral("x"));
    QVERIFY(tab->getDocument()->commit(pdf::PDFModifiedDocument(modified, tab->getDocument()->getOptionalContentActivity()), QStringLiteral("x"), tab->getDocument()->getVersion(), pdf::PDFAppChangeSource::User));
    QTRY_COMPARE(issues->getIssueCount(), 0);
    QTRY_VERIFY(item->isHidden());
    QVERIFY(!tab->isCommandEnabled(pdf::PDFAppCommands::renderingIssues()));

    // Many issues: the rows are bounded, the rest is summed up
    QList<pdf::PDFRenderError> many;
    for (int i = 0; i < pdf::PDFAppRenderingIssuesPanel::MAXIMUM_ROWS + 20; ++i)
    {
        many << pdf::PDFRenderError(pdf::RenderErrorType::Information, QStringLiteral("Issue %1").arg(i));
    }
    Q_EMIT view->getProxy()->renderingError(0, many);
    QTRY_VERIFY(!item->isHidden());
    QVERIFY(issues->showPopover());
    QCOMPARE(issues->getPanel()->getRows().size(), size_t(pdf::PDFAppRenderingIssuesPanel::MAXIMUM_ROWS));
    QVERIFY(!issues->getPanel()->findChild<pdf::PDFLabel*>(QStringLiteral("moreIssues"))->isHidden());

    // New errors while the popover is open are shown
    Q_EMIT view->getProxy()->renderingError(3, { pdf::PDFRenderError(pdf::RenderErrorType::Error, QStringLiteral("Another")) });
    QTRY_COMPARE(issues->getIssueCount(), pdf::PDFAppRenderingIssuesPanel::MAXIMUM_ROWS + 21);
    issues->hidePopover();

    QCOMPARE(pdf::PDFAppRenderingIssues::getSeverity(int(pdf::RenderErrorType::Information)), pdf::PDFComponentPainter::IssueSeverity::Information);
    QCOMPARE(pdf::PDFAppRenderingIssues::getSeverity(999), pdf::PDFComponentPainter::IssueSeverity::Warning);
    QCOMPARE(pdf::PDFAppRenderingIssues::getStatusText(1), QStringLiteral("1 rendering issue"));
}

void AppDocumentPropertiesTest::renderingIssuesLifetime()
{
    // The tab closed with the popover open; the popover deleted by the application;
    // the window deleted with the popover open
    pdftest::AppDocumentFixture fixture;
    for (int scenario = 0; scenario < 3; ++scenario)
    {
        pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Life%1.pdf").arg(scenario), 2));
        QVERIFY(tab);
        pdf::PDFAppRenderingIssues* issues = PropertiesTestHelper::handler(tab)->getRenderingIssues();
        Q_EMIT tab->getView()->getProxy()->renderingError(0, { pdf::PDFRenderError(pdf::RenderErrorType::Error, QStringLiteral("x")) });
        QTRY_VERIFY(!issues->getStatusItem()->isHidden());
        QVERIFY(issues->showPopover());
        QPointer<QMenu> popover = issues->getPopover();

        switch (scenario)
        {
            case 0:
                PropertiesTestHelper::closeTab(fixture, tab);
                break;
            case 1:
                delete popover.data();
                QVERIFY(!issues->isPopoverVisible());
                QVERIFY(issues->showPopover());
                issues->hidePopover();
                break;
            case 2:
                delete fixture.window.data();
                break;
        }
        fixture.settle();
        QVERIFY(!popover);
        if (!fixture.window)
        {
            break;
        }
    }
}

void AppDocumentPropertiesTest::developerOverlay()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Developer.pdf"), 1));
    QVERIFY(tab);
    pdf::PDFDrawWidgetProxy* proxy = tab->getView()->getProxy();
    pdf::PDFAppDeveloperOverlay* overlay = PropertiesTestHelper::handler(tab)->getDeveloperOverlay();
    QVERIFY(overlay);
    QCOMPARE(proxy->getFeatures() & pdf::PDFAppDeveloperOverlay::getOverlayFeatures(), pdf::PDFRenderer::Features());

    // The overlays only with the developer mode
    pdf::PDFSettingsRepository settings = fixture.context->getSettings();
    QVERIFY(!settings.setValue(pdf::PDFSettingKeys::advancedShowTextBlocks(), true).isError());
    fixture.settle();
    QVERIFY(!proxy->getFeatures().testFlag(pdf::PDFRenderer::DebugTextBlocks));
    QVERIFY(!settings.setValue(pdf::PDFSettingKeys::advancedDeveloperMode(), true).isError());
    QTRY_VERIFY(proxy->getFeatures().testFlag(pdf::PDFRenderer::DebugTextBlocks));
    QVERIFY(!proxy->getFeatures().testFlag(pdf::PDFRenderer::DebugTextLines));
    QVERIFY(!settings.setValue(pdf::PDFSettingKeys::advancedShowTextLines(), true).isError());
    QVERIFY(!settings.setValue(pdf::PDFSettingKeys::advancedShowRenderTimes(), true).isError());
    QTRY_VERIFY(proxy->getFeatures().testFlag(pdf::PDFRenderer::DebugTextLines));
    QTRY_VERIFY(proxy->getFeatures().testFlag(pdf::PDFRenderer::DisplayTimes));

    // The view sets its features as a whole: the overlays come back; the other features are kept
    const pdf::PDFRenderer::Features other = proxy->getFeatures() & ~pdf::PDFAppDeveloperOverlay::getOverlayFeatures();
    proxy->setFeatures(other);
    QTRY_VERIFY(proxy->getFeatures().testFlag(pdf::PDFRenderer::DebugTextBlocks));
    QCOMPARE(proxy->getFeatures() & ~pdf::PDFAppDeveloperOverlay::getOverlayFeatures(), other);

    // The canvas draws the overlays (no crash, pages compiled)
    tab->getView()->getPdfWidget()->repaint();
    QTest::qWait(100);

    QVERIFY(!settings.setValue(pdf::PDFSettingKeys::advancedDeveloperMode(), false).isError());
    QTRY_COMPARE(proxy->getFeatures() & pdf::PDFAppDeveloperOverlay::getOverlayFeatures(), pdf::PDFRenderer::Features());
    QCOMPARE(overlay->getRequestedFeatures(), pdf::PDFRenderer::Features());

    // An overlay without a view or a context does nothing
    pdf::PDFAppDeveloperOverlay orphan(nullptr, nullptr, nullptr);
    orphan.apply();
    QCOMPARE(orphan.getRequestedFeatures(), pdf::PDFRenderer::Features());
}

void AppDocumentPropertiesTest::settings()
{
    // The settings of step 3 are registered with valid defaults
    pdftest::AppDocumentFixture fixture;
    const pdf::PDFSettingRegistry* registry = fixture.context->getSettingRegistry();
    for (const QString& key : { pdf::PDFSettingKeys::securityOpenLinks(), pdf::PDFSettingKeys::securityLaunchFiles(), pdf::PDFSettingKeys::signaturesVerify(),
                                pdf::PDFSettingKeys::viewingPageLayout(), pdf::PDFSettingKeys::viewingZoom(), pdf::PDFSettingKeys::viewingColorMode(),
                                pdf::PDFSettingKeys::documentAutoRefresh(), pdf::PDFSettingKeys::editingUndoLimit(), pdf::PDFSettingKeys::advancedDeveloperMode(),
                                pdf::PDFSettingKeys::advancedShowTextBlocks(), pdf::PDFSettingKeys::advancedShowRenderTimes(), pdf::PDFSettingKeys::speechVoice() })
    {
        const pdf::PDFSettingDefinition* definition = registry->findDefinition(key);
        QVERIFY2(definition, qPrintable(key));
        QVERIFY2(definition->isValid(), qPrintable(key));
    }
    QCOMPARE(fixture.context->getSettings().getBool(pdf::PDFSettingKeys::securityOpenLinks()), true);
    QCOMPARE(fixture.context->getSettings().getBool(pdf::PDFSettingKeys::securityLaunchFiles()), false);
}

void AppDocumentPropertiesTest::screenshots()
{
    // PDF4QT_APP_SCREENSHOTS=<folder>: the screens of package F in both themes at
    // 1x and 2x, the minimal window and right-to-left
    const QString folder = qEnvironmentVariable("PDF4QT_APP_SCREENSHOTS");
    if (folder.isEmpty())
    {
        QSKIP("PDF4QT_APP_SCREENSHOTS is not set");
    }
    QDir().mkpath(folder);

    struct Variant
    {
        const char* name;
        QSize size;
        Qt::LayoutDirection direction;
    };
    const Variant variants[] = { { "", QSize(1440, 960), Qt::LeftToRight }, { "-720", QSize(720, 480), Qt::LeftToRight }, { "-rtl", QSize(1440, 960), Qt::RightToLeft } };

    for (const Variant& variant : variants)
    {
        QApplication::setLayoutDirection(variant.direction);
        pdftest::AppDocumentFixture fixture(variant.size);
        pdf::PDFAppDocumentTab* tab = fixture.open(PropertiesTestHelper::infoDocument(fixture.folder(), QStringLiteral("Project_Report.pdf"), true));
        QVERIFY(tab);
        pdf::PDFAppDocumentTab* links = PropertiesTestHelper::openReady(fixture, pdftest::AppDocumentSamples::links(fixture.folder()));
        QVERIFY(links);
        pdf::PDFAppDocumentTab* text = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Report_Text.pdf"), 3));
        QVERIFY(text);

        for (const char* theme : { "light", "dark" })
        {
            pdf::PDFThemeManager::getInstance()->setScheme(QByteArray(theme) == "light" ? pdf::PDFThemeColors::Scheme::Light : pdf::PDFThemeColors::Scheme::Dark);
            fixture.settle();

            auto shoot = [&](const QString& name)
            {
                fixture.settle();
                QTest::qWait(50);
                for (const qreal ratio : { 1.0, 2.0 })
                {
                    if (variant.size.width() < 1000 && ratio > 1.0)
                    {
                        continue;
                    }
                    const QImage image = PropertiesTestHelper::render(fixture.window, ratio);
                    image.save(QDir(folder).absoluteFilePath(QStringLiteral("%1-%2%3@%4x.png").arg(name, QString::fromLatin1(theme), QString::fromLatin1(variant.name)).arg(ratio)));
                }
            };

            fixture.window->activateTab(tab);
            for (const QString& section : pdf::PDFAppDocumentPropertiesPage::getSectionIds())
            {
                // The fonts of a document with a text
                pdf::PDFAppDocumentTab* sectionTab = section == QStringLiteral("fonts") ? text : tab;
                fixture.window->activateTab(sectionTab);
                pdf::PDFAppDocumentPropertiesPage* page = PropertiesTestHelper::showProperties(fixture, sectionTab, section);
                if (section == QStringLiteral("fonts"))
                {
                    QTRY_VERIFY_WITH_TIMEOUT(!page->isFontScanRunning(), 20000);
                    fixture.settle();
                    page->rebuildNow();
                }
                shoot(QStringLiteral("properties-%1").arg(section));
            }
            fixture.window->getNavigator()->navigate(pdf::PDFAppRoutes::document(tab->getDocumentId()));

            // The info bar of a blocked link, the rendering issues
            fixture.window->activateTab(links);
            fixture.settle();
            pdf::PDFAppDocumentActionHandler* handler = PropertiesTestHelper::handler(links)->getActionHandler();
            std::vector<pdf::PDFAnnotationPtr> annotations;
            const std::vector<const pdf::PDFAction*> actions = PropertiesTestHelper::linkActions(links->getDocument()->getDocument().data(), annotations);
            fixture.context->getSettings().setValue(pdf::PDFSettingKeys::securityOpenLinks(), false);
            handler->performAction(actions[2]);
            fixture.context->getSettings().setValue(pdf::PDFSettingKeys::securityOpenLinks(), true);
            Q_EMIT links->getView()->getProxy()->renderingError(0, { pdf::PDFRenderError(pdf::RenderErrorType::Warning, QStringLiteral("Font “ArialMT” is not embedded. A substitute font is used.")),
                                                                    pdf::PDFRenderError(pdf::RenderErrorType::Error, QStringLiteral("Invalid arguments of the operator “Tj”. The operator was skipped.")) });
            shoot(QStringLiteral("link-blocked"));
            links->getView()->hideInfoBar(pdf::PDFAppDocumentActionHandler::getLinkBlockedBarId());
            PropertiesTestHelper::handler(links)->getRenderingIssues()->showPopover();
            fixture.settle();
            if (QMenu* popover = PropertiesTestHelper::handler(links)->getRenderingIssues()->getPopover())
            {
                PropertiesTestHelper::render(popover).save(QDir(folder).absoluteFilePath(QStringLiteral("rendering-issues-popover-%1%2.png").arg(QString::fromLatin1(theme), QString::fromLatin1(variant.name))));
            }
            shoot(QStringLiteral("rendering-issues"));
            PropertiesTestHelper::handler(links)->getRenderingIssues()->hidePopover();

            // The dialogs of the links
            const QString docName = QStringLiteral("Project_Report.pdf");
            for (pdf::PDFMessageBox* box : { pdf::PDFAppDocumentActionDialogs::createLinkDialog(links, docName, QUrl(QStringLiteral("https://market-insight.example/reports/2026?ref=pdf"))),
                                             pdf::PDFAppDocumentActionDialogs::createProgramDialog(links, docName, QStringLiteral("C:/Tools/ReportViewer.exe"), QStringLiteral("/open quarterly.xlsx"), QStringLiteral("C:/Tools")),
                                             pdf::PDFAppDocumentActionDialogs::createFileDialog(links, docName, QStringLiteral("C:/Users/Jakub/Documents/Reports/Budget_2026.xlsx")) })
            {
                box->show();
                fixture.settle();
                QTest::qWait(50);
                PropertiesTestHelper::render(box).save(QDir(folder).absoluteFilePath(QStringLiteral("dialog-%1-%2%3.png").arg(box->objectName(), QString::fromLatin1(theme), QString::fromLatin1(variant.name))));
                shoot(QStringLiteral("dialog-%1-window").arg(box->objectName()));
                box->hide();
                delete box;
            }
        }
    }
}

QTEST_MAIN(AppDocumentPropertiesTest)
#include "tst_appdocumentpropertiestest.moc"
