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
#include "applocaleguard.h"

#include "pdfappcommand.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentloader.h"
#include "pdfappdocumentregistry.h"
#include "pdfappmainwindow.h"
#include "pdfappdocumentpanel.h"
#include "pdfappdocumentpanelcommands.h"
#include "pdfappdocumentpanels_attachments.h"
#include "pdfappdocumentpanels_bookmarks.h"
#include "pdfappdocumentpanels_common.h"
#include "pdfappdocumentpanels_layers.h"
#include "pdfappdocumentpanels_notes.h"
#include "pdfappdocumentpanels_outline.h"
#include "pdfappdocumentpanels_thumbnails.h"
#include "pdfappdocumentsidebar.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfappnavigator.h"
#include "pdfappundostack.h"
#include "pdfbookmarkrepository.h"
#include "pdfdatabasestatement.h"
#include "pdfsettingdefinitions.h"
#include "pdfsettingsrepository.h"

#include "pdfcatalog.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdrawspacecontroller.h"
#include "pdfdrawwidget.h"
#include "pdfoptionalcontent.h"
#include "pdfrecentrepository.h"
#include "pdfsecurityhandler.h"
#include "pdfmessagebox.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QMenu>
#include <QPushButton>
#include <QScrollBar>
#include <QSlider>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QThread>
#include <QToolButton>
#include <QtTest>

#include <limits>

/// Package C of step 3: the sidebar, the navigation panels, the personal
/// bookmarks and their repository, the commands of the package
class AppDocumentPanelsTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();

    void panelRegistry();
    void sidebarFrame();
    void sidebarHeaderApi();
    void sidebarIconSize();
    void sidebarRequestedBeforeReady();
    void sidebarThemeAndDirection();
    void sidebarMinimalWindow();
    void thumbnails();
    void thumbnailKeysPerTab();
    void thumbnailsSelectionAndSync();
    void thumbnailsSizeSlider();
    void thumbnailsContextMenu();
    void thumbnailsRotateUndoLocked();
    void thumbnailsLargeDocument();
    void outline();
    void outlineHostile();
    void layers();
    void layersKeptOverReset();
    void outlineSamePage();
    void annotationListsShared();
    void listGeometry();
    void attachmentsList();
    void attachmentsSave();
    void attachmentsOpen();
    void attachmentsOpenEncrypted();
    void attachmentsMarkOfTheWeb();
    void attachmentsRefused();
    void attachmentsFileNames();
    void attachmentsDeletedDuringWrite();
    void attachmentsDocumentChanged();
    void bookmarkRepository();
    void bookmarkRepositoryHostileRows();
    void bookmarkJson();
    void bookmarksPanel();
    void bookmarksCommands();
    void bookmarksAutoGenerate();
    void bookmarksExportImport();
    void bookmarksOtherConnection();
    void bookmarksRenameMoveRemove();
    void bookmarksMenuAfterReload();
    void bookmarksEncryptedTitle();
    void notes();
    void notesFilterAndHostile();
    void deletionMatrix();
    void deletionFromOwnSignals_data();
    void deletionFromOwnSignals();
    void screenshots();
};

/// Samples and helpers of the test
class PanelsTestHelper
{
public:
    PanelsTestHelper() = delete;

    static pdf::PDFObject name(const char* value) { return pdf::PDFObject::createName(QByteArray(value)); }
    static pdf::PDFObject text(const QString& value) { return pdf::PDFObjectFactory::createTextString(value); }
    static pdf::PDFObject reference(pdf::PDFObjectReference value) { return pdf::PDFObject::createReference(value); }

    static pdf::PDFObject array(std::initializer_list<pdf::PDFObject> items)
    {
        pdf::PDFArrayBuilder builder;
        for (const pdf::PDFObject& item : items)
        {
            builder.appendItem(item);
        }
        return pdf::PDFObject::createArray(std::move(builder));
    }

    static pdf::PDFObject dictionary(std::initializer_list<std::pair<const char*, pdf::PDFObject>> entries)
    {
        pdf::PDFDictionaryBuilder builder;
        for (const auto& entry : entries)
        {
            builder.setEntry(pdf::PDFInplaceOrMemoryString(entry.first), entry.second);
        }
        return pdf::PDFObject::createDictionary(std::move(builder));
    }

    static pdf::PDFObject stream(QByteArray data)
    {
        pdf::PDFDictionaryBuilder builder;
        builder.setEntry(pdf::PDFInplaceOrMemoryString("Type"), name("EmbeddedFile"));
        builder.setEntry(pdf::PDFInplaceOrMemoryString("Length"), pdf::PDFObject::createInteger(data.size()));
        return pdf::PDFObject::createStream(pdf::PDFStream(std::move(builder), std::move(data)));
    }

    static QString write(pdf::PDFDocumentBuilder& builder, const QString& folder, const QString& fileName)
    {
        pdf::PDFDocument document = builder.build();
        const QString path = QDir(folder).absoluteFilePath(fileName);
        return pdftest::AppDocumentSamples::write(document, path) ? path : QString();
    }

    /// Attachments with the given names (each "data of <name>")
    static QString attachments(const QString& folder, const QString& fileName, const QStringList& names)
    {
        pdf::PDFDocumentBuilder builder;
        builder.appendPage(QRectF(0, 0, 300, 400));
        pdf::PDFArrayBuilder namesArray;
        for (const QString& attachmentName : names)
        {
            const pdf::PDFObjectReference data = builder.addObject(stream(QByteArray("data of ") + attachmentName.toUtf8()));
            const pdf::PDFObjectReference file = builder.addObject(dictionary({ { "Type", name("Filespec") }, { "F", text(attachmentName) }, { "UF", text(attachmentName) },
                                                                                { "EF", dictionary({ { "F", reference(data) } }) } }));
            namesArray.appendItem(text(attachmentName));
            namesArray.appendItem(reference(file));
        }
        const pdf::PDFObject embeddedFiles = dictionary({ { "Names", pdf::PDFObject::createArray(std::move(namesArray)) } });
        builder.setCatalogNames(builder.addObject(dictionary({ { "EmbeddedFiles", embeddedFiles } })));
        return write(builder, folder, fileName);
    }

    /// Attachments with the given names and data
    static QString attachmentFiles(const QString& folder, const QString& fileName, const std::vector<std::pair<QString, QByteArray>>& files)
    {
        pdf::PDFDocumentBuilder builder;
        builder.appendPage(QRectF(0, 0, 300, 400));
        pdf::PDFArrayBuilder namesArray;
        for (const auto& file : files)
        {
            const pdf::PDFObjectReference data = builder.addObject(stream(file.second));
            const pdf::PDFObjectReference specification = builder.addObject(dictionary({ { "Type", name("Filespec") }, { "F", text(file.first) }, { "UF", text(file.first) },
                                                                                         { "EF", dictionary({ { "F", reference(data) } }) } }));
            namesArray.appendItem(text(file.first));
            namesArray.appendItem(reference(specification));
        }
        const pdf::PDFObject embeddedFiles = dictionary({ { "Names", pdf::PDFObject::createArray(std::move(namesArray)) } });
        builder.setCatalogNames(builder.addObject(dictionary({ { "EmbeddedFiles", embeddedFiles } })));
        return write(builder, folder, fileName);
    }

    static QByteArray readFile(const QString& path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

    /// Document tab of the window, whose document has no file and the title
    static pdf::PDFAppDocumentTab* findMemoryTab(pdftest::AppDocumentFixture& fixture, const QString& title)
    {
        for (pdf::PDFAppDocumentTab* tab : fixture.window->getDocumentTabs())
        {
            if (tab->getDocument() && tab->getDocument()->getFilePath().isEmpty() && tab->getDocument()->getTitle() == title)
            {
                return tab;
            }
        }
        return nullptr;
    }

    static pdf::PDFObject xyz(pdf::PDFObjectReference page, double top)
    {
        return array({ reference(page), name("XYZ"), pdf::PDFObject::createNull(), pdf::PDFObject::createReal(top), pdf::PDFObject::createNull() });
    }

    /// Outline: two items of page 1 (tops 390, 150), three items of page 2 (tops
    /// 380, 200, 50) and one of page 3 (Fit)
    static QString sharedPageOutline(const QString& folder, bool isEncrypted)
    {
        pdf::PDFDocumentBuilder builder;
        std::vector<pdf::PDFObjectReference> pages;
        for (int i = 0; i < 3; ++i)
        {
            pages.push_back(builder.appendPage(QRectF(0, 0, 300, 400)));
        }

        struct Entry
        {
            const char* title;
            int page;
            double top;
        };
        const std::vector<Entry> entries =
        {
            { "Cover top", 0, 390.0 }, { "Cover middle", 0, 150.0 },
            { "First of page 2", 1, 380.0 }, { "Second of page 2", 1, 200.0 }, { "Third of page 2", 1, 50.0 },
            { "Chapter of page 3", 2, -1.0 }
        };

        const pdf::PDFObjectReference root = builder.addObject(pdf::PDFObject());
        std::vector<pdf::PDFObjectReference> items;
        for (size_t i = 0; i < entries.size(); ++i)
        {
            items.push_back(builder.addObject(pdf::PDFObject()));
        }
        for (size_t i = 0; i < entries.size(); ++i)
        {
            pdf::PDFDictionaryBuilder item;
            item.setEntry(pdf::PDFInplaceOrMemoryString("Title"), text(QString::fromLatin1(entries[i].title)));
            item.setEntry(pdf::PDFInplaceOrMemoryString("Parent"), reference(root));
            const pdf::PDFObjectReference page = pages[size_t(entries[i].page)];
            item.setEntry(pdf::PDFInplaceOrMemoryString("Dest"), entries[i].top < 0.0 ? array({ reference(page), name("Fit") }) : xyz(page, entries[i].top));
            if (i > 0)
            {
                item.setEntry(pdf::PDFInplaceOrMemoryString("Prev"), reference(items[i - 1]));
            }
            if (i + 1 < entries.size())
            {
                item.setEntry(pdf::PDFInplaceOrMemoryString("Next"), reference(items[i + 1]));
            }
            builder.setObject(items[i], pdf::PDFObject::createDictionary(std::move(item)));
        }
        builder.setObject(root, dictionary({ { "Type", name("Outlines") }, { "First", reference(items.front()) }, { "Last", reference(items.back()) },
                                             { "Count", pdf::PDFObject::createInteger(pdf::PDFInteger(entries.size())) } }));
        builder.setOutline(root);

        if (isEncrypted)
        {
            pdf::PDFSecurityHandlerFactory::SecuritySettings settings;
            settings.algorithm = pdf::PDFSecurityHandlerFactory::AES_256;
            settings.userPassword = pdftest::AppDocumentSamples::getUserPassword();
            settings.ownerPassword = pdftest::AppDocumentSamples::getOwnerPassword();
            settings.permissions = 0xFFFFFFFCu;
            builder.setSecurityHandler(pdf::PDFSecurityHandlerFactory::createSecurityHandler(settings));
        }
        return write(builder, folder, isEncrypted ? QStringLiteral("shared-page-outline-encrypted.pdf") : QStringLiteral("shared-page-outline.pdf"));
    }

    /// Top of the view in the coordinates of the page (as the outline panel)
    static double viewTop(pdf::PDFAppDocumentView* view, pdf::PDFInteger page)
    {
        QPointF point;
        QWidget* widget = view->getPdfWidget()->getDrawWidget()->getWidget();
        return view->getProxy()->getPageUnderPoint(QPoint(widget->width() / 2, 0), &point) == page ? point.y() : std::numeric_limits<double>::infinity();
    }

    /// 4 pages: text notes on pages 1 (two) and 3, a highlight on page 3, a link (not listed) on page 2
    static QString notes(const QString& folder)
    {
        pdf::PDFDocumentBuilder builder;
        std::vector<pdf::PDFObjectReference> pages;
        for (int i = 0; i < 4; ++i)
        {
            pages.push_back(builder.appendPage(QRectF(0, 0, 300, 400)));
        }
        builder.createAnnotationText(pages[0], QRectF(10, 10, 20, 20), pdf::TextAnnotationIcon::Comment, QStringLiteral("Jakub Melka"), QString(), QStringLiteral("Add the source of this statistic."), false);
        builder.createAnnotationText(pages[0], QRectF(40, 10, 20, 20), pdf::TextAnnotationIcon::Note, QStringLiteral("Jane Novak"), QString(), QStringLiteral("Check these numbers\nagainst the report."), false);
        builder.createAnnotationLink(pages[1], QRectF(10, 10, 50, 20), QStringLiteral("https://example.com/"), pdf::LinkHighlightMode::Invert);
        builder.createAnnotationText(pages[2], QRectF(10, 10, 20, 20), pdf::TextAnnotationIcon::Comment, QStringLiteral("Peter Dvorak"), QString(), QStringLiteral("The colors of the chart."), false);
        builder.createAnnotationHighlight(pages[2], QRectF(50, 50, 100, 20), QColor(Qt::yellow), QStringLiteral("Jane Novak"), QString(), QStringLiteral("Important"));
        return write(builder, folder, QStringLiteral("notes.pdf"));
    }

    /// Outline with a cycle-free but hostile structure: an item without an action,
    /// an item to a page of another document, a deep chain, control characters
    static QString hostileOutline(const QString& folder)
    {
        pdf::PDFDocumentBuilder builder;
        std::vector<pdf::PDFObjectReference> pages;
        for (int i = 0; i < 3; ++i)
        {
            pages.push_back(builder.appendPage(QRectF(0, 0, 300, 400)));
        }

        pdf::PDFObjectReference root = builder.addObject(dictionary({ { "Type", name("Outlines") } }));
        const int depth = 300;
        std::vector<pdf::PDFObjectReference> items;
        for (int i = 0; i < depth; ++i)
        {
            items.push_back(builder.addObject(dictionary({ })));
        }
        for (int i = 0; i < depth; ++i)
        {
            pdf::PDFDictionaryBuilder item;
            item.setEntry(pdf::PDFInplaceOrMemoryString("Title"), text(i == 0 ? QStringLiteral("Bad\x01\x02 title\nwith lines") : QStringLiteral("Level %1").arg(i)));
            item.setEntry(pdf::PDFInplaceOrMemoryString("Parent"), reference(i == 0 ? root : items[size_t(i - 1)]));
            if (i % 2 == 0)
            {
                item.setEntry(pdf::PDFInplaceOrMemoryString("Dest"), array({ reference(pages[size_t(i % 3)]), name("Fit") }));
            }
            else if (i == 1)
            {
                item.setEntry(pdf::PDFInplaceOrMemoryString("Dest"), array({ pdf::PDFObject::createInteger(99999), name("Fit") }));
            }
            if (i + 1 < depth)
            {
                item.setEntry(pdf::PDFInplaceOrMemoryString("First"), reference(items[size_t(i + 1)]));
                item.setEntry(pdf::PDFInplaceOrMemoryString("Last"), reference(items[size_t(i + 1)]));
                item.setEntry(pdf::PDFInplaceOrMemoryString("Count"), pdf::PDFObject::createInteger(1));
            }
            builder.setObject(items[size_t(i)], pdf::PDFObject::createDictionary(std::move(item)));
        }
        builder.setObject(root, dictionary({ { "Type", name("Outlines") }, { "First", reference(items.front()) }, { "Last", reference(items.front()) },
                                             { "Count", pdf::PDFObject::createInteger(1) } }));
        builder.setOutline(root);
        return write(builder, folder, QStringLiteral("hostile-outline.pdf"));
    }

    /// Reads the file into the document (a document of a standalone view)
    static bool load(pdf::PDFAppDocument* document, const QString& path)
    {
        QFile file(path);
        if (!document || !file.open(QIODevice::ReadOnly))
        {
            return false;
        }
        const QByteArray data = file.readAll();
        pdf::PDFDocumentReader reader(nullptr, [](bool* ok) { *ok = false; return QString(); }, true, false);
        pdf::PDFDocument pdfDocument = reader.readFromBuffer(data);
        if (reader.getReadingResult() != pdf::PDFDocumentReader::Result::OK)
        {
            return false;
        }
        document->setLoaded(pdf::PDFDocumentPointer(new pdf::PDFDocument(std::move(pdfDocument))), data, QFileInfo(path).canonicalFilePath(), false);
        return document->isReady();
    }

    /// Replaces the document of the tab by the content of the file (one undo step,
    /// as a change of another package)
    static bool replaceDocument(pdf::PDFAppDocumentTab* tab, const QString& path)
    {
        pdf::PDFAppDocument* document = tab ? tab->getDocument() : nullptr;
        QFile file(path);
        if (!document || !file.open(QIODevice::ReadOnly))
        {
            return false;
        }
        pdf::PDFDocumentReader reader(nullptr, [](bool* ok) { *ok = false; return QString(); }, true, false);
        pdf::PDFDocument pdfDocument = reader.readFromBuffer(file.readAll());
        if (reader.getReadingResult() != pdf::PDFDocumentReader::Result::OK)
        {
            return false;
        }
        const pdf::PDFDocumentPointer pointer(new pdf::PDFDocument(std::move(pdfDocument)));
        const pdf::PDFModifiedDocument modified(pointer, document->getOptionalContentActivity(), pdf::PDFModifiedDocument::Reset);
        return document->commit(modified, QStringLiteral("Replace"), document->getVersion(), pdf::PDFAppChangeSource::User);
    }

    static pdf::PDFAppDocumentSidebar* sidebar(pdf::PDFAppDocumentTab* tab)
    {
        return tab && tab->getView() ? tab->getView()->getSidebar() : nullptr;
    }

    template<typename T>
    static T* panel(pdf::PDFAppDocumentTab* tab, const QString& id)
    {
        pdf::PDFAppDocumentSidebar* bar = sidebar(tab);
        if (!bar || !bar->showPanel(id))
        {
            return nullptr;
        }
        return qobject_cast<T*>(bar->findPanel(id));
    }

    static int countVisiblePictures(pdf::PDFAppThumbnailsPanel* thumbnails)
    {
        int count = 0;
        QListView* view = thumbnails->getListView();
        for (int row = 0; row < thumbnails->getModel()->rowCount(); ++row)
        {
            const QRect rect = view->visualRect(thumbnails->getModel()->index(row));
            if (rect.isValid() && rect.intersects(view->viewport()->rect()) && thumbnails->getModel()->hasPicture(row))
            {
                ++count;
            }
        }
        return count;
    }

    static int countVisibleRows(pdf::PDFAppThumbnailsPanel* thumbnails)
    {
        int count = 0;
        QListView* view = thumbnails->getListView();
        for (int row = 0; row < thumbnails->getModel()->rowCount(); ++row)
        {
            const QRect rect = view->visualRect(thumbnails->getModel()->index(row));
            if (rect.isValid() && rect.intersects(view->viewport()->rect()))
            {
                ++count;
            }
        }
        return count;
    }

    static QAction* findAction(QMenu* menu, const QString& objectName)
    {
        for (QAction* action : menu->actions())
        {
            if (action->objectName() == objectName)
            {
                return action;
            }
        }
        return nullptr;
    }

    static pdf::PDFMessageBox* findMessageBox(const QString& objectName)
    {
        for (QWidget* widget : QApplication::topLevelWidgets())
        {
            if (pdf::PDFMessageBox* box = qobject_cast<pdf::PDFMessageBox*>(widget))
            {
                if (box->isVisible() && (objectName.isEmpty() || box->objectName() == objectName))
                {
                    return box;
                }
            }
        }
        return nullptr;
    }

    static void restoreHandlers()
    {
        pdf::PDFAppPanelFileDialogs::setSaveHandler(pdf::PDFAppPanelFileDialogs::FileHandler());
        pdf::PDFAppPanelFileDialogs::setOpenHandler(pdf::PDFAppPanelFileDialogs::FileHandler());
    }
};

/// Panel of the test, optionally with its own header and a header widget
class TestPanel : public pdf::PDFAppDocumentPanel
{
public:
    explicit TestPanel(QString id, bool ownHeader, QWidget* parent) : pdf::PDFAppDocumentPanel(parent), m_id(std::move(id)), m_ownHeader(ownHeader) { }

    virtual QString getId() const override { return m_id; }
    virtual QString getTitle() const override { return QStringLiteral("Test %1").arg(m_id); }
    virtual QString getSubtitle() const override { return QStringLiteral("Subtitle %1").arg(m_id); }
    virtual QString getIconName() const override { return QStringLiteral("info"); }
    virtual bool hasOwnHeader() const override { return m_ownHeader; }
    virtual QWidget* createHeaderWidget(QWidget* parent) override
    {
        QToolButton* button = new QToolButton(parent);
        button->setObjectName(QStringLiteral("headerOf_%1").arg(m_id));
        return button;
    }

private:
    QString m_id;
    bool m_ownHeader = false;
};

/// Factory of the panel of the test
class TestPanelFactory : public pdf::PDFAppDocumentPanelFactory
{
public:
    explicit TestPanelFactory(QString id, int order, bool ownHeader = false) : m_id(std::move(id)), m_order(order), m_ownHeader(ownHeader) { }

    virtual QString getId() const override { return m_id; }
    virtual int getOrder() const override { return m_order; }
    virtual pdf::PDFAppDocumentPanel* createPanel(QWidget* parent) const override { return new TestPanel(m_id, m_ownHeader, parent); }

private:
    QString m_id;
    int m_order;
    bool m_ownHeader;
};

void AppDocumentPanelsTest::initTestCase()
{
    // The style of the application (the scroll bars over the content, the sizes of the design)
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
}

void AppDocumentPanelsTest::cleanup()
{
    PanelsTestHelper::restoreHandlers();
}

void AppDocumentPanelsTest::panelRegistry()
{
    pdf::PDFAppDocumentPanelRegistry registry;
    QVERIFY(registry.registerFactory(std::make_shared<TestPanelFactory>(QStringLiteral("b"), 20)));
    QVERIFY(registry.registerFactory(std::make_shared<TestPanelFactory>(QStringLiteral("a"), 10)));
    QVERIFY(!registry.registerFactory(std::make_shared<TestPanelFactory>(QStringLiteral("a"), 30)));
    QVERIFY(!registry.registerFactory(nullptr));
    QVERIFY(!registry.registerFactory(std::make_shared<TestPanelFactory>(QString(), 30)));

    const std::vector<const pdf::PDFAppDocumentPanelFactory*> factories = registry.getFactories();
    QCOMPARE(factories.size(), size_t(2));
    QCOMPARE(factories[0]->getId(), QStringLiteral("a"));
    QVERIFY(registry.findFactory(QStringLiteral("b")));
    QVERIFY(!registry.findFactory(QStringLiteral("c")));

    // The defaults of the base: available, a tab, no title / icon (the panel tells them)
    QVERIFY(factories[0]->hasSidebarTab());
    QVERIFY(factories[0]->isAvailable(nullptr));
    QVERIFY(factories[0]->getTitle().isEmpty());
    QVERIFY(factories[0]->getIconName().isEmpty());

    // The navigation panels of package C in the order of the design (193:884)
    const pdf::PDFAppDocumentPanelRegistry defaults = pdf::PDFAppDocumentPanelRegistry::createDefault();
    const QStringList expected = { QStringLiteral("outline"), QStringLiteral("thumbnails"), QStringLiteral("layers"), QStringLiteral("attachments"),
                                   QStringLiteral("bookmarks"), QStringLiteral("notes") };
    QStringList found;
    for (const pdf::PDFAppDocumentPanelFactory* factory : defaults.getFactories())
    {
        QVERIFY(!factory->getId().isEmpty());
        if (expected.contains(factory->getId()))
        {
            found << factory->getId();
            QVERIFY(!factory->getTitle().isEmpty());
            QVERIFY(!factory->getIconName().isEmpty());
            QVERIFY(factory->hasSidebarTab());
        }
    }
    QCOMPARE(found, expected);
    QCOMPARE(defaults.findFactory(QStringLiteral("thumbnails"))->getIconName(), QStringLiteral("pages"));
    QCOMPARE(defaults.findFactory(QStringLiteral("outline"))->getIconName(), QStringLiteral("list"));
    QCOMPARE(defaults.findFactory(QStringLiteral("notes"))->getIconName(), QStringLiteral("message-square"));
}

void AppDocumentPanelsTest::sidebarFrame()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("simple.pdf"), 3));
    QVERIFY(tab);
    pdf::PDFAppDocumentSidebar* sidebar = PanelsTestHelper::sidebar(tab);
    QVERIFY(sidebar);
    fixture.settle();

    // The default panel: thumbnails (193:756)
    QCOMPARE(sidebar->getCurrentPanelId(), QStringLiteral("thumbnails"));
    QCOMPARE(sidebar->getHeaderTitle(), QStringLiteral("Thumbnails"));
    QCOMPARE(sidebar->getHeaderSubtitle(), QStringLiteral("3 pages"));
    QVERIFY(sidebar->getTabButton(QStringLiteral("thumbnails"))->isChecked());

    // Tabs without content are hidden (Sidebar tab 190:619)
    QVERIFY(sidebar->isTabVisible(QStringLiteral("thumbnails")));
    QVERIFY(sidebar->isTabVisible(QStringLiteral("bookmarks")));
    QVERIFY(!sidebar->isTabVisible(QStringLiteral("outline")));
    QVERIFY(!sidebar->isTabVisible(QStringLiteral("layers")));
    QVERIFY(!sidebar->isTabVisible(QStringLiteral("attachments")));
    QVERIFY(!sidebar->isTabVisible(QStringLiteral("notes")));
    QVERIFY(!sidebar->showPanel(QStringLiteral("outline")));
    QVERIFY(!sidebar->showPanel(QStringLiteral("unknown")));
    QCOMPARE(sidebar->getCurrentPanelId(), QStringLiteral("thumbnails"));

    // Geometry of 193:884 / 193:917: tabs 32 at x 12, y 10; header at least 44
    QToolButton* thumbnailsTab = sidebar->getTabButton(QStringLiteral("thumbnails"));
    QCOMPARE(thumbnailsTab->size(), QSize(32, 32));
    QCOMPARE(thumbnailsTab->mapTo(sidebar, QPoint(0, 0)).y(), 10);
    QVERIFY(sidebar->getHeader()->height() >= 44);
    QCOMPARE(sidebar->getHeader()->mapTo(sidebar, QPoint(0, 0)).y(), 48);
    QCOMPARE(thumbnailsTab->toolTip(), QStringLiteral("Thumbnails"));
    QCOMPARE(thumbnailsTab->accessibleName(), QStringLiteral("Thumbnails"));

    // A click on a tab shows the panel and changes the route (?panel=)
    QSignalSpy spy(sidebar, &pdf::PDFAppDocumentSidebar::currentPanelChanged);
    sidebar->getTabButton(QStringLiteral("bookmarks"))->click();
    QCOMPARE(sidebar->getCurrentPanelId(), QStringLiteral("bookmarks"));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(sidebar->getHeaderTitle(), QStringLiteral("Bookmarks"));
    QVERIFY(sidebar->getHeaderWidget(QStringLiteral("bookmarks"))->isVisible());
    QVERIFY(!sidebar->getHeaderWidget(QStringLiteral("thumbnails"))->isVisible());
    QCOMPARE(tab->getView()->getCurrentPanelId(), QStringLiteral("bookmarks"));

    // Showing the shown panel again does not notify
    QVERIFY(sidebar->showPanel(QStringLiteral("bookmarks")));
    QCOMPARE(spy.count(), 1);

    // The other samples show their tabs
    pdf::PDFAppDocumentTab* outlineTab = fixture.open(pdftest::AppDocumentSamples::outline(fixture.folder()));
    QVERIFY(PanelsTestHelper::sidebar(outlineTab)->isTabVisible(QStringLiteral("outline")));
    pdf::PDFAppDocumentTab* layersTab = fixture.open(pdftest::AppDocumentSamples::layers(fixture.folder()));
    QVERIFY(PanelsTestHelper::sidebar(layersTab)->isTabVisible(QStringLiteral("layers")));
    pdf::PDFAppDocumentTab* attachmentsTab = fixture.open(pdftest::AppDocumentSamples::attachments(fixture.folder()));
    QVERIFY(PanelsTestHelper::sidebar(attachmentsTab)->isTabVisible(QStringLiteral("attachments")));
    // The file attachment annotation is a note too
    QVERIFY(PanelsTestHelper::sidebar(attachmentsTab)->isTabVisible(QStringLiteral("notes")));
}

void AppDocumentPanelsTest::sidebarHeaderApi()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentView view(fixture.context.get());
    view.resize(1000, 700);
    view.show();
    pdf::PDFAppDocumentSidebar* sidebar = view.getSidebar();

    // The registry of the sidebar is the default one; a panel of another package
    // (the search results) is tested by its own test. Here the frame: a panel with
    // its own header hides the common header, the header widget follows its panel.
    QVERIFY(sidebar->getHeader());
    QVERIFY(sidebar->getRegistry().findFactory(QStringLiteral("thumbnails")));

    const pdf::PDFAppDocumentPanelFactory* searchFactory = sidebar->getRegistry().findFactory(QStringLiteral("search"));
    if (searchFactory && !searchFactory->hasSidebarTab())
    {
        QVERIFY(!sidebar->getTabButton(QStringLiteral("search")));
    }

    // A panel without a document is shown at once (requested) and its header
    QVERIFY(sidebar->showPanel(QStringLiteral("bookmarks")));
    QCOMPARE(sidebar->getHeaderTitle(), QStringLiteral("Bookmarks"));
    QPointer<QWidget> header = sidebar->getHeaderWidget(QStringLiteral("bookmarks"));
    QVERIFY(header);

    // A deleted panel deletes its header widget
    delete sidebar->findPanel(QStringLiteral("bookmarks"));
    pdftest::AppDocumentFixture::settle();
    QVERIFY(!header);
    QVERIFY(!sidebar->findPanel(QStringLiteral("bookmarks")));

    // It is created again
    QVERIFY(sidebar->showPanel(QStringLiteral("bookmarks")));
    QVERIFY(sidebar->findPanel(QStringLiteral("bookmarks")));
    QVERIFY(sidebar->getHeaderWidget(QStringLiteral("bookmarks")));
}

void AppDocumentPanelsTest::sidebarIconSize()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("simple.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppDocumentSidebar* sidebar = PanelsTestHelper::sidebar(tab);
    QToolButton* button = sidebar->getTabButton(QStringLiteral("thumbnails"));
    QCOMPARE(button->size(), QSize(32, 32));

    const QList<std::pair<QString, int>> sizes = { { QStringLiteral("small"), 28 }, { QStringLiteral("large"), 36 }, { QStringLiteral("veryLarge"), 40 }, { QStringLiteral("medium"), 32 } };
    for (const auto& size : sizes)
    {
        QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::viewingSidebarIconSize(), size.first).isError());
        QTRY_COMPARE(button->size(), QSize(size.second, size.second));
    }
}

void AppDocumentPanelsTest::sidebarRequestedBeforeReady()
{
    pdftest::AppDocumentFixture fixture;
    const QString path = pdftest::AppDocumentSamples::outline(fixture.folder());

    pdf::PDFAppDocumentView view(fixture.context.get());
    pdf::PDFAppDocument* document = new pdf::PDFAppDocument(fixture.context.get(), &view);
    view.setDocument(document);
    view.resize(1000, 700);
    view.show();

    // Before the document is ready, every panel may be requested (?panel= of the route)
    QVERIFY(view.showPanel(QStringLiteral("outline")));
    QVERIFY(!view.getSidebar()->isTabVisible(QStringLiteral("outline")));

    QVERIFY(PanelsTestHelper::load(document, path));
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(view.getCurrentPanelId(), QStringLiteral("outline"));
    QVERIFY(view.getSidebar()->isTabVisible(QStringLiteral("outline")));

    // A requested panel without content falls back to the thumbnails
    pdf::PDFAppDocumentView view2(fixture.context.get());
    pdf::PDFAppDocument* document2 = new pdf::PDFAppDocument(fixture.context.get(), &view2);
    view2.setDocument(document2);
    QVERIFY(view2.showPanel(QStringLiteral("layers")));
    QVERIFY(PanelsTestHelper::load(document2, path));
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(view2.getCurrentPanelId(), QStringLiteral("thumbnails"));

    // The document destroyed before the view
    delete document2;
    pdftest::AppDocumentFixture::settle();
    QVERIFY(!view2.getSidebar()->isTabVisible(QStringLiteral("thumbnails")));
}

void AppDocumentPanelsTest::sidebarThemeAndDirection()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::attachments(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppDocumentSidebar* sidebar = PanelsTestHelper::sidebar(tab);
    for (const QString& id : { QStringLiteral("thumbnails"), QStringLiteral("attachments"), QStringLiteral("bookmarks"), QStringLiteral("notes") })
    {
        QVERIFY(sidebar->showPanel(id));
        fixture.settle();
    }

    // The background is bg/sidebar and the line border/default at the trailing edge
    for (const pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Dark, pdf::PDFThemeColors::Scheme::Light })
    {
        pdf::PDFThemeManager::getInstance()->setScheme(scheme);
        fixture.settle();
        const QImage image = sidebar->grab().toImage();
        const pdf::PDFThemeManager* manager = pdf::PDFThemeManager::getInstance();
        const QPoint probe(image.width() / 2 * image.devicePixelRatio(), (image.height() - 4) * image.devicePixelRatio());
        QCOMPARE(QColor(image.pixel(probe)).name(), manager->getColor(pdf::PDFThemeColors::BgSidebar).name());
        QCOMPARE(QColor(image.pixel(QPoint((image.width() - 1) * image.devicePixelRatio(), 100))).name(), manager->getColor(pdf::PDFThemeColors::BorderDefault).name());
    }

    // Right-to-left: the line at the left edge, the tabs from the right
    QApplication::setLayoutDirection(Qt::RightToLeft);
    fixture.settle();
    const QImage image = sidebar->grab().toImage();
    QCOMPARE(QColor(image.pixel(QPoint(0, 100))).name(), pdf::PDFThemeManager::getInstance()->getColor(pdf::PDFThemeColors::BorderDefault).name());
    QToolButton* first = sidebar->getTabButton(QStringLiteral("thumbnails"));
    QVERIFY(first->mapTo(sidebar, QPoint(0, 0)).x() > sidebar->width() / 2);
    QApplication::setLayoutDirection(Qt::LeftToRight);
}

void AppDocumentPanelsTest::sidebarMinimalWindow()
{
    pdftest::AppDocumentFixture fixture(QSize(720, 480));
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::outline(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppDocumentSidebar* sidebar = PanelsTestHelper::sidebar(tab);
    for (const QString& id : { QStringLiteral("outline"), QStringLiteral("thumbnails"), QStringLiteral("bookmarks") })
    {
        QVERIFY(sidebar->showPanel(id));
        fixture.settle();
        // The header and the tabs keep their geometry, the content scrolls
        QVERIFY(sidebar->getHeader()->height() >= 44);
        QVERIFY(sidebar->findPanel(id)->height() > 0);
    }
}

void AppDocumentPanelsTest::thumbnails()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("simple.pdf"), 10));
    QVERIFY(tab);
    pdf::PDFAppThumbnailsPanel* thumbnails = PanelsTestHelper::panel<pdf::PDFAppThumbnailsPanel>(tab, QStringLiteral("thumbnails"));
    QVERIFY(thumbnails);
    QCOMPARE(thumbnails->getModel()->rowCount(), 10);
    QCOMPARE(thumbnails->getModel()->data(thumbnails->getModel()->index(2), Qt::DisplayRole).toString(), QStringLiteral("3"));
    QCOMPARE(thumbnails->getModel()->data(thumbnails->getModel()->index(2), Qt::ToolTipRole).toString(), QStringLiteral("Page 3"));
    QCOMPARE(thumbnails->getSubtitle(), QStringLiteral("10 pages"));
    QVERIFY(!thumbnails->getModel()->data(QModelIndex(), Qt::DisplayRole).isValid());
    QVERIFY(!thumbnails->getModel()->data(thumbnails->getModel()->index(99), Qt::DisplayRole).isValid());

    // The pictures of the visible rows are drawn (placeholder until then)
    QTRY_VERIFY_WITH_TIMEOUT(PanelsTestHelper::countVisiblePictures(thumbnails) == PanelsTestHelper::countVisibleRows(thumbnails), 20000);
    QVERIFY(PanelsTestHelper::countVisibleRows(thumbnails) >= 2);
    QVERIFY(thumbnails->getScheduler()->getDrawnCount() >= 2);
    QVERIFY(thumbnails->getScheduler()->getMaximumPendingCount() <= pdf::PDFAppThumbnailScheduler::MAXIMUM_PENDING_COMPILES);

    // Two columns centered in the sidebar (193:929: x 21.5 and 153.5 of 299)
    QListView* view = thumbnails->getListView();
    const QRect first = view->visualRect(thumbnails->getModel()->index(0));
    const QRect second = view->visualRect(thumbnails->getModel()->index(1));
    QVERIFY2(first.top() == second.top(), qPrintable(QStringLiteral("view %1, viewport %2, grid %3 x %4, sidebar %5").arg(view->width()).arg(view->viewport()->width())
                                                     .arg(view->gridSize().width()).arg(view->gridSize().height()).arg(PanelsTestHelper::sidebar(tab)->width())
                                                     + QStringLiteral(" sb %1 %2 overlap %3 margins %4 %5 frame %6").arg(view->verticalScrollBar()->width()).arg(view->verticalScrollBar()->isVisible())
                                                     .arg(view->style()->pixelMetric(QStyle::PM_ScrollView_ScrollBarOverlap, nullptr, view->verticalScrollBar()))
                                                     .arg(view->viewport()->geometry().left()).arg(view->viewport()->geometry().right()).arg(view->frameWidth())));
    QCOMPARE(first.size(), QSize(124, 183));
    const int left = view->viewport()->mapTo(view, first.topLeft()).x();
    const int right = view->width() - view->viewport()->mapTo(view, second.topRight()).x() - 1;
    QVERIFY2(qAbs(left - right) <= 2, qPrintable(QStringLiteral("%1 %2").arg(left).arg(right)));

    // A page label is the text of the item
    pdf::PDFAppDocumentTab* labels = fixture.open(pdftest::AppDocumentSamples::pageLabels(fixture.folder()));
    pdf::PDFAppThumbnailsPanel* labelThumbnails = PanelsTestHelper::panel<pdf::PDFAppThumbnailsPanel>(labels, QStringLiteral("thumbnails"));
    QCOMPARE(labelThumbnails->getModel()->data(labelThumbnails->getModel()->index(1), Qt::DisplayRole).toString(), QStringLiteral("ii"));
    QCOMPARE(labelThumbnails->getModel()->data(labelThumbnails->getModel()->index(8), Qt::ToolTipRole).toString(), QStringLiteral("Page A-1"));
}

void AppDocumentPanelsTest::thumbnailKeysPerTab()
{
    pdftest::AppDocumentFixture fixture;
    const QString first = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("first.pdf"), 3);
    const QString second = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("second.pdf"), 3);
    pdf::PDFAppDocumentTab* firstTab = fixture.open(first);
    pdf::PDFAppDocumentTab* secondTab = fixture.open(second);
    QVERIFY(firstTab && secondTab);
    pdf::PDFAppThumbnailsPanel* firstPanel = PanelsTestHelper::panel<pdf::PDFAppThumbnailsPanel>(firstTab, QStringLiteral("thumbnails"));
    pdf::PDFAppThumbnailsPanel* secondPanel = PanelsTestHelper::panel<pdf::PDFAppThumbnailsPanel>(secondTab, QStringLiteral("thumbnails"));

    // Two tabs never share a picture of QPixmapCache (old Editor: "PDF_THUMBNAIL_%1")
    QVERIFY(firstPanel->getModel()->getKey(0) != secondPanel->getModel()->getKey(0));
    QVERIFY(firstPanel->getModel()->getKey(0).contains(firstTab->getDocumentId()));

    // A new version of the document has new keys
    const QString before = secondPanel->getModel()->getKey(1);
    secondPanel->getModel()->invalidatePages({ 1 });
    QVERIFY(secondPanel->getModel()->getKey(1) != before);
    const QString beforeAll = secondPanel->getModel()->getKey(0);
    secondPanel->getModel()->invalidateAll();
    QVERIFY(secondPanel->getModel()->getKey(0) != beforeAll);

    // Hostile values of the model
    secondPanel->getModel()->invalidatePages({ -1, 1000, std::numeric_limits<pdf::PDFInteger>::max() });
    secondPanel->getModel()->setPicture(-5, QPixmap(10, 10));
    secondPanel->getModel()->setPicture(1000, QPixmap(10, 10));
    secondPanel->getModel()->setPicture(0, QPixmap());
    QVERIFY(!secondPanel->getModel()->hasPicture(1000));
    secondPanel->getModel()->setPictureSize(QSize(-1, -1), std::numeric_limits<qreal>::quiet_NaN());
    QCOMPARE(secondPanel->getModel()->getPictureSize(), QSize(108, 148));
}

void AppDocumentPanelsTest::thumbnailsSelectionAndSync()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("simple.pdf"), 12));
    pdf::PDFAppThumbnailsPanel* thumbnails = PanelsTestHelper::panel<pdf::PDFAppThumbnailsPanel>(tab, QStringLiteral("thumbnails"));
    QVERIFY(thumbnails);
    pdf::PDFAppDocumentView* view = tab->getView();

    // The canvas page is selected (Selected = the page shown)
    view->goToPage(6);
    fixture.settle();
    QTRY_COMPARE(thumbnails->getSelectedPages(), std::vector<pdf::PDFInteger>({ 6 }));
    QVERIFY(view->getSelectedPages() == std::vector<pdf::PDFInteger>({ 6 }));

    // More pages (266:12502): the subtitle and the selection of the view
    thumbnails->selectPages({ 2, 3, 4 });
    QCOMPARE(thumbnails->getSubtitle(), QStringLiteral("3 of 12 pages selected"));
    QCOMPARE(PanelsTestHelper::sidebar(tab)->getHeaderSubtitle(), QStringLiteral("3 of 12 pages selected"));
    QVERIFY(view->getSelectedPages() == std::vector<pdf::PDFInteger>({ 2, 3, 4 }));
    QVERIFY(PanelsTestHelper::sidebar(tab)->getSelectedPages() == std::vector<pdf::PDFInteger>({ 2, 3, 4 }));

    // A selection of more pages is kept, when the canvas moves
    view->goToPage(9);
    fixture.settle();
    QVERIFY(thumbnails->getSelectedPages() == std::vector<pdf::PDFInteger>({ 2, 3, 4 }));

    // One selected page goes to the canvas (a click, the keyboard)
    thumbnails->getListView()->selectionModel()->setCurrentIndex(thumbnails->getModel()->index(1), QItemSelectionModel::ClearAndSelect);
    fixture.settle();
    QTRY_COMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(1));
    QVERIFY(view->getSelectedPages() == std::vector<pdf::PDFInteger>({ 1 }));

    // Click on a page by the mouse
    QListView* list = thumbnails->getListView();
    const QModelIndex index = thumbnails->getModel()->index(3);
    list->scrollTo(index);
    fixture.settle();
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, list->visualRect(index).center());
    QTRY_COMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(3));

    // Ctrl+click adds a page without moving the canvas
    const QModelIndex other = thumbnails->getModel()->index(5);
    list->scrollTo(other);
    fixture.settle();
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::ControlModifier, list->visualRect(other).center());
    QVERIFY(thumbnails->getSelectedPages() == std::vector<pdf::PDFInteger>({ 3, 5 }));
    QCOMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(3));

    // Sync off: the thumbnails stay
    thumbnails->selectPages({ 0 });
    thumbnails->setSyncEnabled(false);
    QVERIFY(!thumbnails->getSyncButton()->isChecked());
    view->goToPage(10);
    fixture.settle();
    QVERIFY(thumbnails->getSelectedPages() == std::vector<pdf::PDFInteger>({ 0 }) || thumbnails->getSelectedPages() == std::vector<pdf::PDFInteger>({ 10 }));
    thumbnails->getSyncButton()->click();
    QVERIFY(thumbnails->isSyncEnabled());
    QTRY_COMPARE(thumbnails->getSelectedPages(), std::vector<pdf::PDFInteger>({ 10 }));

    // Invalid pages are ignored
    thumbnails->selectPages({ -1, 100 });
    QVERIFY(thumbnails->getSelectedPages().empty());
}

void AppDocumentPanelsTest::thumbnailsSizeSlider()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("simple.pdf"), 4));
    pdf::PDFAppThumbnailsPanel* thumbnails = PanelsTestHelper::panel<pdf::PDFAppThumbnailsPanel>(tab, QStringLiteral("thumbnails"));
    QVERIFY(thumbnails);
    QSlider* slider = thumbnails->getSizeSlider();
    QVERIFY(slider);
    QCOMPARE(slider->size(), QSize(84, 20));
    QCOMPARE(slider->value(), pdf::PDFAppThumbnailsPanel::DEFAULT_SLOT_WIDTH);
    QCOMPARE(thumbnails->getListView()->getItemSize(), QSize(124, 183));

    const QString key = thumbnails->getModel()->getKey(0);
    slider->setValue(pdf::PDFAppThumbnailsPanel::MAXIMUM_SLOT_WIDTH);
    QCOMPARE(thumbnails->getSlotWidth(), pdf::PDFAppThumbnailsPanel::MAXIMUM_SLOT_WIDTH);
    QVERIFY(thumbnails->getListView()->getItemSize().width() > 124);
    QVERIFY(thumbnails->getModel()->getKey(0) != key);
    fixture.settle();

    // One column for large pictures, two for the default
    const QRect first = thumbnails->getListView()->visualRect(thumbnails->getModel()->index(0));
    const QRect second = thumbnails->getListView()->visualRect(thumbnails->getModel()->index(1));
    QVERIFY(second.top() > first.top());

    thumbnails->setSlotWidth(1);
    QCOMPARE(thumbnails->getSlotWidth(), pdf::PDFAppThumbnailsPanel::MINIMUM_SLOT_WIDTH);
    QCOMPARE(slider->value(), pdf::PDFAppThumbnailsPanel::MINIMUM_SLOT_WIDTH);
    thumbnails->setSlotWidth(100000);
    QCOMPARE(thumbnails->getSlotWidth(), pdf::PDFAppThumbnailsPanel::MAXIMUM_SLOT_WIDTH);
    QTRY_VERIFY_WITH_TIMEOUT(PanelsTestHelper::countVisiblePictures(thumbnails) == PanelsTestHelper::countVisibleRows(thumbnails), 20000);
}

void AppDocumentPanelsTest::thumbnailsContextMenu()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("simple.pdf"), 4));
    pdf::PDFAppThumbnailsPanel* thumbnails = PanelsTestHelper::panel<pdf::PDFAppThumbnailsPanel>(tab, QStringLiteral("thumbnails"));
    QVERIFY(thumbnails);
    thumbnails->selectPages({ 1, 2 });

    // 266:12848: Rotate Left / Right, Insert Pages…, Move to…, Extract to New Document, Remove Pages (danger)
    QPointer<QMenu> menu = thumbnails->createContextMenu();
    QStringList texts;
    for (QAction* action : menu->actions())
    {
        if (!action->isSeparator())
        {
            texts << action->text();
        }
    }
    QCOMPARE(texts, QStringList({ QStringLiteral("Rotate Left"), QStringLiteral("Rotate Right"), QStringLiteral("Insert Pages…"), QStringLiteral("Move to…"),
                                  QStringLiteral("Extract to New Document"), QStringLiteral("Remove Pages") }));
    QVERIFY(PanelsTestHelper::findAction(menu, QStringLiteral("removePages"))->property("destructive").toBool());
    for (QAction* action : menu->actions())
    {
        QVERIFY(action->isSeparator() || action->isEnabled());
    }

    // A page tool opens its route: Page Not Found in phase 1, Back returns
    PanelsTestHelper::findAction(menu, QStringLiteral("extractPages"))->trigger();
    fixture.settle();
    QVERIFY(!tab->isViewShown());
    QVERIFY(fixture.window->getNavigator()->goBack(tab));
    fixture.settle();
    QTRY_VERIFY(tab->isViewShown());
    delete menu;

    for (const QString& name : { QStringLiteral("insertPages"), QStringLiteral("movePages"), QStringLiteral("removePages") })
    {
        QPointer<QMenu> toolMenu = thumbnails->createContextMenu();
        PanelsTestHelper::findAction(toolMenu, name)->trigger();
        fixture.settle();
        QVERIFY(!tab->isViewShown());
        fixture.window->getNavigator()->goBack(tab);
        fixture.settle();
        QTRY_VERIFY(tab->isViewShown());
        delete toolMenu;
    }

    // The context menu by the keyboard opens at the current page and closes
    thumbnails->getListView()->setFocus();
    QContextMenuEvent event(QContextMenuEvent::Keyboard, QPoint(0, 0), thumbnails->getListView()->mapToGlobal(QPoint(-50, -50)));
    QApplication::sendEvent(thumbnails->getListView(), &event);
    fixture.settle();
    QMenu* shown = thumbnails->findChild<QMenu*>(QStringLiteral("thumbnailsContextMenu"));
    QVERIFY(shown);
    shown->close();
    fixture.settle();

    // A right click on a page outside of the selection selects it alone
    QListView* list = thumbnails->getListView();
    const QModelIndex index = thumbnails->getModel()->index(0);
    thumbnails->selectPages({ 2, 3 });
    list->scrollTo(index);
    fixture.settle();
    QVERIFY2(list->viewport()->rect().contains(list->visualRect(index).center()), "the first page is visible");
    QContextMenuEvent mouseEvent(QContextMenuEvent::Mouse, list->visualRect(index).center(), list->viewport()->mapToGlobal(list->visualRect(index).center()));
    QApplication::sendEvent(list->viewport(), &mouseEvent);
    fixture.settle();
    QVERIFY(thumbnails->getSelectedPages() == std::vector<pdf::PDFInteger>({ 0 }));
    if (QMenu* opened = thumbnails->findChild<QMenu*>(QStringLiteral("thumbnailsContextMenu")))
    {
        opened->close();
    }
    fixture.settle();
}

void AppDocumentPanelsTest::thumbnailsRotateUndoLocked()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("simple.pdf"), 4));
    pdf::PDFAppThumbnailsPanel* thumbnails = PanelsTestHelper::panel<pdf::PDFAppThumbnailsPanel>(tab, QStringLiteral("thumbnails"));
    pdf::PDFAppDocument* document = tab->getDocument();
    QVERIFY(thumbnails && document);

    thumbnails->selectPages({ 1, 3 });
    const quint64 version = document->getVersion();
    QVERIFY(tab->isCommandEnabled(pdf::PDFAppCommands::rotatePagesRight()));
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::rotatePagesRight()));
    fixture.settle();
    QCOMPARE(document->getVersion(), version + 1);
    const pdf::PDFCatalog* catalog = document->getDocument()->getCatalog();
    QCOMPARE(catalog->getPage(1)->getPageRotation(), pdf::PageRotation::Rotate90);
    QCOMPARE(catalog->getPage(3)->getPageRotation(), pdf::PageRotation::Rotate90);
    QCOMPARE(catalog->getPage(0)->getPageRotation(), pdf::PageRotation::None);

    // The selection of more pages survives the new version
    QVERIFY(thumbnails->getSelectedPages() == std::vector<pdf::PDFInteger>({ 1, 3 }));

    // Left twice from 90 = 270; the menu runs the same command
    QPointer<QMenu> menu = thumbnails->createContextMenu();
    PanelsTestHelper::findAction(menu, QStringLiteral("rotateLeft"))->trigger();
    delete menu;
    menu = thumbnails->createContextMenu();
    PanelsTestHelper::findAction(menu, QStringLiteral("rotateLeft"))->trigger();
    delete menu;
    fixture.settle();
    QCOMPARE(document->getDocument()->getCatalog()->getPage(1)->getPageRotation(), pdf::PageRotation::Rotate270);

    // One undo step each
    QVERIFY(document->getUndoStack()->canUndo());
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::undo()));
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::undo()));
    fixture.settle();
    QCOMPARE(document->getDocument()->getCatalog()->getPage(1)->getPageRotation(), pdf::PageRotation::Rotate90);

    // Locked: refused, the menu items are off
    document->setLocked(true);
    fixture.settle();
    QVERIFY(!tab->isCommandEnabled(pdf::PDFAppCommands::rotatePagesLeft()));
    menu = thumbnails->createContextMenu();
    QVERIFY(!PanelsTestHelper::findAction(menu, QStringLiteral("rotateLeft"))->isEnabled());
    QVERIFY(!PanelsTestHelper::findAction(menu, QStringLiteral("removePages"))->isEnabled());
    QVERIFY(PanelsTestHelper::findAction(menu, QStringLiteral("extractPages"))->isEnabled());
    delete menu;
    pdf::PDFAppDocumentPanelCommandHandler* handler = qobject_cast<pdf::PDFAppDocumentPanelCommandHandler*>(tab->findHandler(pdf::PDFAppCommands::rotatePagesLeft()));
    QVERIFY(handler);
    QVERIFY(!handler->rotatePages({ 0 }, true));
    document->setLocked(false);
    QVERIFY(!handler->rotatePages({ }, true));
    QVERIFY(!handler->rotatePages({ -1, 99 }, true));
    QVERIFY(handler->rotatePages({ 0, 0, 0 }, false));
    QCOMPARE(document->getDocument()->getCatalog()->getPage(0)->getPageRotation(), pdf::PageRotation::Rotate270);
}

void AppDocumentPanelsTest::thumbnailsLargeDocument()
{
    pdftest::AppDocumentFixture fixture;
    QElapsedTimer timer;
    timer.start();
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::large(fixture.folder(), 2000));
    QVERIFY(tab);
    const qint64 openTime = timer.restart();
    pdf::PDFAppThumbnailsPanel* thumbnails = PanelsTestHelper::panel<pdf::PDFAppThumbnailsPanel>(tab, QStringLiteral("thumbnails"));
    QVERIFY(thumbnails);
    QCOMPARE(thumbnails->getModel()->rowCount(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(PanelsTestHelper::countVisiblePictures(thumbnails) == PanelsTestHelper::countVisibleRows(thumbnails), 60000);
    const qint64 firstPictures = timer.restart();

    // Scrolling over all pages queues no more than a few compilations
    QScrollBar* scrollBar = thumbnails->getListView()->verticalScrollBar();
    QVERIFY(scrollBar->maximum() > 0);
    for (int step = 0; step <= 100; ++step)
    {
        scrollBar->setValue(scrollBar->maximum() * step / 100);
        QApplication::processEvents();
        QVERIFY(thumbnails->getScheduler()->getPendingCount() <= pdf::PDFAppThumbnailScheduler::MAXIMUM_PENDING_COMPILES);
    }
    QVERIFY(thumbnails->getScheduler()->getMaximumPendingCount() <= pdf::PDFAppThumbnailScheduler::MAXIMUM_PENDING_COMPILES);
    QTRY_VERIFY_WITH_TIMEOUT(PanelsTestHelper::countVisiblePictures(thumbnails) == PanelsTestHelper::countVisibleRows(thumbnails), 60000);
    const qint64 scrollPictures = timer.elapsed();

    // The pages drawn are only a small part of the document
    QVERIFY(thumbnails->getScheduler()->getDrawnCount() < 400);
    qInfo("2000 pages: open %lld ms, first thumbnails %lld ms, scroll to the end and its thumbnails %lld ms, %d pictures drawn",
          openTime, firstPictures, scrollPictures, thumbnails->getScheduler()->getDrawnCount());

    // Closing the tab while compilations wait never blocks
    timer.restart();
    fixture.window->closeTab(tab);
    fixture.settle();
    QVERIFY(timer.elapsed() < 10000);
}

void AppDocumentPanelsTest::outline()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::outline(fixture.folder()));
    pdf::PDFAppOutlinePanel* outline = PanelsTestHelper::panel<pdf::PDFAppOutlinePanel>(tab, QStringLiteral("outline"));
    QVERIFY(outline);
    QCOMPARE(outline->getItemCount(), 4);
    QCOMPARE(outline->getSubtitle(), QStringLiteral("4 items"));
    QCOMPARE(outline->getTreeView()->getRowStyle(), pdf::PDFTreeView::RowStyle::Outline);

    QAbstractItemModel* model = outline->getViewModel();
    QCOMPARE(model->rowCount(), 3);
    QCOMPARE(model->index(0, 0).data().toString(), QStringLiteral("Introduction"));
    const QModelIndex chapter = model->index(1, 0);
    QCOMPARE(model->rowCount(chapter), 1);
    const QModelIndex section = model->index(0, 0, chapter);
    QCOMPARE(section.data().toString(), QStringLiteral("Section 1.1"));
    QCOMPARE(section.data(pdf::PDFAppOutlinePanel::PageRole).toLongLong(), 2);

    // A click goes to the page
    outline->activateItem(section);
    fixture.settle();
    QTRY_COMPARE(tab->getView()->getCurrentPageIndex(), pdf::PDFInteger(2));

    // The item of the current page is selected (the greatest page not after it)
    tab->getView()->goToPage(3);
    fixture.settle();
    QTRY_COMPARE(outline->getCurrentItem().data().toString(), QStringLiteral("Section 1.1"));
    tab->getView()->goToPage(4);
    fixture.settle();
    QTRY_COMPARE(outline->getCurrentItem().data().toString(), QStringLiteral("Appendix"));

    // Search outline…
    outline->getSearchField()->setText(QStringLiteral("section"));
    QCOMPARE(model->rowCount(), 1);
    QCOMPARE(model->index(0, 0).data().toString(), QStringLiteral("Chapter 1"));
    outline->getSearchField()->setText(QStringLiteral("nothing at all"));
    QCOMPARE(model->rowCount(), 0);
    outline->getSearchField()->clear();
    QCOMPARE(model->rowCount(), 3);

    // Invalid indices are ignored
    outline->activateItem(QModelIndex());
    QStandardItemModel other;
    other.appendRow(new QStandardItem(QStringLiteral("x")));
    outline->activateItem(other.index(0, 0));
}

void AppDocumentPanelsTest::outlineHostile()
{
    pdftest::AppDocumentFixture fixture;
    const QString path = PanelsTestHelper::hostileOutline(fixture.folder());
    QVERIFY(!path.isEmpty());
    pdf::PDFAppDocumentTab* tab = fixture.open(path);
    QVERIFY(tab);
    pdf::PDFAppOutlinePanel* outline = PanelsTestHelper::panel<pdf::PDFAppOutlinePanel>(tab, QStringLiteral("outline"));
    QVERIFY(outline);
    QVERIFY(outline->getItemCount() > 0);

    // Control characters are spaces, an item without a page shows a message
    const QModelIndex first = outline->getViewModel()->index(0, 0);
    QVERIFY(!first.data().toString().contains(QChar(1)));
    QVERIFY(!first.data().toString().contains(QChar(u'\n')));
    const QModelIndex second = outline->getViewModel()->index(0, 0, first);
    if (second.isValid())
    {
        QCOMPARE(second.data(pdf::PDFAppOutlinePanel::PageRole).toLongLong(), -1);
        outline->activateItem(second);
    }
    outline->getSearchField()->setText(QStringLiteral("Level 2"));
    outline->getSearchField()->setText(QString(2000, QChar(u'x')));
    outline->getSearchField()->clear();
}

void AppDocumentPanelsTest::layers()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::layers(fixture.folder()));
    pdf::PDFAppLayersPanel* layers = PanelsTestHelper::panel<pdf::PDFAppLayersPanel>(tab, QStringLiteral("layers"));
    QVERIFY(layers);
    QCOMPARE(layers->getLayerCount(), 2);
    QCOMPARE(layers->getSubtitle(), QStringLiteral("2 layers"));

    pdf::PDFAppLayersModel* model = layers->getModel();
    QCOMPARE(model->rowCount(QModelIndex()), 2);
    const QModelIndex layerA = model->index(0, 0, QModelIndex());
    const QModelIndex layerB = model->index(1, 0, QModelIndex());
    QCOMPARE(layerA.data().toString(), QStringLiteral("Layer A"));
    QCOMPARE(layerA.data(Qt::CheckStateRole).toInt(), int(Qt::Checked));
    QCOMPARE(layerB.data(Qt::CheckStateRole).toInt(), int(Qt::Unchecked));
    QCOMPARE(layerB.data(Qt::ToolTipRole).toString(), QStringLiteral("Layer B"));

    // A switch changes the activity of the document (the view), not the document
    const quint64 version = tab->getDocument()->getVersion();
    QSignalSpy spy(model, &QAbstractItemModel::dataChanged);
    QVERIFY(model->setData(layerB, Qt::Checked, Qt::CheckStateRole));
    QCOMPARE(layerB.data(Qt::CheckStateRole).toInt(), int(Qt::Checked));
    QVERIFY(spy.count() > 0);
    QCOMPARE(tab->getDocument()->getVersion(), version);

    // The activity changed elsewhere repaints the rows
    spy.clear();
    pdf::PDFOptionalContentActivity* activity = tab->getDocument()->getOptionalContentActivity();
    const auto ocgs = tab->getDocument()->getDocument()->getCatalog()->getOptionalContentProperties()->getAllOptionalContentGroups();
    activity->setState(ocgs.front(), pdf::OCState::OFF);
    QVERIFY(spy.count() > 0);
    QCOMPARE(layerA.data(Qt::CheckStateRole).toInt(), int(Qt::Unchecked));
}

void AppDocumentPanelsTest::layersKeptOverReset()
{
    // A change, which replaces the activity of the optional content (Rotate is a
    // Reset change, undo / redo of it too), keeps the layers switched by the user
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::layers(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    const auto ocgs = document->getDocument()->getCatalog()->getOptionalContentProperties()->getAllOptionalContentGroups();
    QCOMPARE(ocgs.size(), size_t(2));
    QCOMPARE(document->getOptionalContentActivity()->getState(ocgs[0]), pdf::OCState::ON);
    QCOMPARE(document->getOptionalContentActivity()->getState(ocgs[1]), pdf::OCState::OFF);

    // The user hides layer A and shows layer B
    document->getOptionalContentActivity()->setState(ocgs[0], pdf::OCState::OFF);
    document->getOptionalContentActivity()->setState(ocgs[1], pdf::OCState::ON);

    QPointer<pdf::PDFOptionalContentActivity> oldActivity = document->getOptionalContentActivity();
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::rotatePagesRight()));
    fixture.settle();
    QVERIFY(document->getOptionalContentActivity() != oldActivity.data());
    QCOMPARE(document->getOptionalContentActivity()->getState(ocgs[0]), pdf::OCState::OFF);
    QCOMPARE(document->getOptionalContentActivity()->getState(ocgs[1]), pdf::OCState::ON);

    // The view and the layers panel have the new activity with the states
    pdf::PDFAppLayersPanel* layers = PanelsTestHelper::panel<pdf::PDFAppLayersPanel>(tab, QStringLiteral("layers"));
    QVERIFY(layers);
    QCOMPARE(layers->getModel()->index(0, 0, QModelIndex()).data(Qt::CheckStateRole).toInt(), int(Qt::Unchecked));
    QCOMPARE(layers->getModel()->index(1, 0, QModelIndex()).data(Qt::CheckStateRole).toInt(), int(Qt::Checked));

    // Undo and redo
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::undo()));
    fixture.settle();
    QCOMPARE(document->getOptionalContentActivity()->getState(ocgs[0]), pdf::OCState::OFF);
    QCOMPARE(document->getOptionalContentActivity()->getState(ocgs[1]), pdf::OCState::ON);
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::redo()));
    fixture.settle();
    QCOMPARE(document->getOptionalContentActivity()->getState(ocgs[0]), pdf::OCState::OFF);
    QCOMPARE(document->getOptionalContentActivity()->getState(ocgs[1]), pdf::OCState::ON);

    // A new version with other defaults of the layers (a file changed by another
    // program and reloaded): only the layer switched by the user keeps its state,
    // the other one takes the default of the new version (B is shown by default now)
    document->getOptionalContentActivity()->setState(ocgs[1], pdf::OCState::OFF);
    {
        pdf::PDFDocumentBuilder builder(document->getDocument().data());
        pdf::PDFArrayBuilder groups;
        groups.appendItem(pdf::PDFObject::createReference(ocgs[0]));
        groups.appendItem(pdf::PDFObject::createReference(ocgs[1]));
        pdf::PDFArrayBuilder order;
        order.appendItem(pdf::PDFObject::createReference(ocgs[0]));
        order.appendItem(pdf::PDFObject::createReference(ocgs[1]));
        pdf::PDFDictionaryBuilder defaultConfiguration;
        defaultConfiguration.setEntry(pdf::PDFInplaceOrMemoryString("Order"), pdf::PDFObject::createArray(std::move(order)));
        pdf::PDFDictionaryBuilder properties;
        properties.setEntry(pdf::PDFInplaceOrMemoryString("OCGs"), pdf::PDFObject::createArray(std::move(groups)));
        properties.setEntry(pdf::PDFInplaceOrMemoryString("D"), pdf::PDFObject::createDictionary(std::move(defaultConfiguration)));
        builder.setCatalogOptionalContentProperties(builder.addObject(pdf::PDFObject::createDictionary(std::move(properties))));
        const pdf::PDFDocumentPointer pointer(new pdf::PDFDocument(builder.build()));
        QCOMPARE(pdf::PDFOptionalContentActivity(pointer.data(), pdf::OCUsage::View, nullptr).getState(ocgs[1]), pdf::OCState::ON);
        const pdf::PDFModifiedDocument modified(pointer, document->getOptionalContentActivity(), pdf::PDFModifiedDocument::Reset);
        QVERIFY(document->commit(modified, QStringLiteral("Other defaults"), document->getVersion(), pdf::PDFAppChangeSource::User));
    }
    fixture.settle();
    QCOMPARE(document->getOptionalContentActivity()->getState(ocgs[0]), pdf::OCState::OFF);
    QCOMPARE(document->getOptionalContentActivity()->getState(ocgs[1]), pdf::OCState::ON);

    // A document without the layers of the old one: nothing is copied (no crash)
    QVERIFY(PanelsTestHelper::replaceDocument(tab, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("no-layers.pdf"), 1)));
    fixture.settle();
    QCOMPARE(document->getOptionalContentActivity()->getState(ocgs[0]), pdf::OCState::Unknown);
}

void AppDocumentPanelsTest::outlineSamePage()
{
    // Owner report: an item of a page with another item earlier on it - the clicked
    // item stays selected, when the view reaches its page (also with three items
    // of a page); the sync by the canvas runs, when the view reaches another page,
    // and takes the item closest above the top of the view
    using Position = pdf::PDFAppOutlinePanel::ItemPosition;
    constexpr double pageTop = std::numeric_limits<double>::infinity();
    const std::vector<Position> positions = { { 0, 390.0 }, { 0, 150.0 }, { 1, 380.0 }, { 1, 200.0 }, { 1, 50.0 }, { 2, pageTop }, { -1, pageTop } };
    QCOMPARE(pdf::PDFAppOutlinePanel::findCurrentItemIndex(positions, 1, pageTop), 2);  // top of the page not reached: the topmost
    QCOMPARE(pdf::PDFAppOutlinePanel::findCurrentItemIndex(positions, 1, 400.0), 2);
    QCOMPARE(pdf::PDFAppOutlinePanel::findCurrentItemIndex(positions, 1, 380.0), 2);
    QCOMPARE(pdf::PDFAppOutlinePanel::findCurrentItemIndex(positions, 1, 250.0), 2);
    QCOMPARE(pdf::PDFAppOutlinePanel::findCurrentItemIndex(positions, 1, 199.0), 3);
    QCOMPARE(pdf::PDFAppOutlinePanel::findCurrentItemIndex(positions, 1, 10.0), 4);
    QCOMPARE(pdf::PDFAppOutlinePanel::findCurrentItemIndex(positions, 0, 100.0), 1);
    QCOMPARE(pdf::PDFAppOutlinePanel::findCurrentItemIndex(positions, 2, 0.0), 5);    // Fit: the top of the page
    QCOMPARE(pdf::PDFAppOutlinePanel::findCurrentItemIndex(positions, 7, 0.0), 5);    // a page without items: the last page with one
    QCOMPARE(pdf::PDFAppOutlinePanel::findCurrentItemIndex({ { 3, pageTop }, { 3, pageTop } }, 3, pageTop), 1); // equal: the later
    QCOMPARE(pdf::PDFAppOutlinePanel::findCurrentItemIndex({ { 3, 100.0 }, { 3, 300.0 } }, 5, 0.0), 0);         // an earlier page: its lowest
    QCOMPARE(pdf::PDFAppOutlinePanel::findCurrentItemIndex(positions, -1, 0.0), -1);
    QCOMPARE(pdf::PDFAppOutlinePanel::findCurrentItemIndex({ }, 1, 0.0), -1);

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(PanelsTestHelper::sharedPageOutline(fixture.folder(), false));
    pdf::PDFAppOutlinePanel* outline = PanelsTestHelper::panel<pdf::PDFAppOutlinePanel>(tab, QStringLiteral("outline"));
    QVERIFY(outline);
    fixture.settle();
    QAbstractItemModel* model = outline->getViewModel();
    QCOMPARE(model->rowCount(), 6);
    pdf::PDFTreeView* tree = outline->getTreeView();
    pdf::PDFAppDocumentView* view = tab->getView();

    // A click on each item of a page with two and with three items: the clicked one stays
    for (const int row : { 3, 2, 4, 3, 1, 0, 1 })
    {
        const QModelIndex index = model->index(row, 0);
        const QString title = index.data().toString();
        tree->scrollTo(index);
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, tree->visualRect(index).center());
        fixture.settle();
        QTRY_COMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(row < 2 ? 0 : 1));
        QTest::qWait(50);
        fixture.settle();
        QCOMPARE(outline->getCurrentItem().data().toString(), title);
    }

    // Another page and back by the view (the top of the page): the topmost item
    view->goToPage(2);
    fixture.settle();
    QTRY_COMPARE(outline->getCurrentItem().data().toString(), QStringLiteral("Chapter of page 3"));
    view->goToPage(1);
    fixture.settle();
    QTRY_COMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(1));
    QTRY_COMPARE(outline->getCurrentItem().data().toString(), QStringLiteral("First of page 2"));

    // Scrolled down inside of page 2: the item selected by the view (not activated by
    // the user) follows the view - the second item, when its top is passed (the
    // selection is updated by a change of the filter here, as by a change of the page)
    for (int step = 0; step < 400 && view->getCurrentPageIndex() == 1 && PanelsTestHelper::viewTop(view, 1) > 190.0; ++step)
    {
        view->getProxy()->scrollByPixels(QPoint(0, -5));
        fixture.settle();
    }
    QCOMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(1));
    QVERIFY(PanelsTestHelper::viewTop(view, 1) <= 190.0);
    outline->getSearchField()->setText(QStringLiteral("page"));
    fixture.settle();
    QCOMPARE(outline->getCurrentItem().data().toString(), QStringLiteral("Second of page 2"));
    outline->getSearchField()->clear();
    fixture.settle();
    QCOMPARE(outline->getCurrentItem().data().toString(), QStringLiteral("Second of page 2"));

    // The first item activated by the user stays selected on its page (also after
    // the filter has changed)
    {
        const QModelIndex first = model->index(2, 0);
        QCOMPARE(first.data().toString(), QStringLiteral("First of page 2"));
        tree->scrollTo(first);
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, tree->visualRect(first).center());
        fixture.settle();
        QCOMPARE(outline->getCurrentItem().data().toString(), QStringLiteral("First of page 2"));
        outline->getSearchField()->setText(QStringLiteral("page"));
        fixture.settle();
        QCOMPARE(outline->getCurrentItem().data().toString(), QStringLiteral("First of page 2"));
        outline->getSearchField()->clear();
        fixture.settle();
    }

    // Scrolled up from page 3 into the bottom of page 2: the item closest above the
    // top of the view (the third one, its destination is above it)
    view->goToPage(2);
    fixture.settle();
    QTRY_COMPARE(outline->getCurrentItem().data().toString(), QStringLiteral("Chapter of page 3"));
    for (int step = 0; step < 400 && view->getCurrentPageIndex() != 1; ++step)
    {
        view->getProxy()->scrollByPixels(QPoint(0, 5));
        fixture.settle();
    }
    QCOMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(1));
    fixture.settle();
    const double top = PanelsTestHelper::viewTop(view, 1);
    const std::vector<Position> documentPositions = { { 0, 390.0 }, { 0, 150.0 }, { 1, 380.0 }, { 1, 200.0 }, { 1, 50.0 }, { 2, pageTop } };
    const int expected = pdf::PDFAppOutlinePanel::findCurrentItemIndex(documentPositions, 1, top);
    QVERIFY(expected >= 2 && expected <= 4);
    QTRY_COMPARE(outline->getCurrentItem().data().toString(), model->index(expected, 0).data().toString());
    if (top < 50.0)
    {
        QCOMPARE(outline->getCurrentItem().data().toString(), QStringLiteral("Third of page 2"));
    }
}

void AppDocumentPanelsTest::annotationListsShared()
{
    // The attachments and the notes of a version are collected once and shared by
    // the tabs of the sidebar and the panels (also after a change)
    pdftest::AppDocumentFixture fixture;

    // The tabs of the sidebar do not collect the lists (they are asked at every
    // change of the document): only a shown panel collects its list
    {
        const int attachmentsHidden = pdf::PDFAppAttachments::getCollectCount();
        const int notesHidden = pdf::PDFAppNotes::getCollectCount();
        const QString hiddenPath = fixture.path(QStringLiteral("attachments-hidden.pdf"));
        QVERIFY(QFile::copy(pdftest::AppDocumentSamples::attachments(fixture.folder()), hiddenPath));
        pdf::PDFAppDocumentTab* hiddenTab = fixture.open(hiddenPath);
        QVERIFY(hiddenTab);
        fixture.settle();
        pdf::PDFAppDocumentSidebar* sidebar = hiddenTab->getView()->getSidebar();
        QVERIFY(sidebar->isTabVisible(QStringLiteral("attachments")));
        QVERIFY(sidebar->isTabVisible(QStringLiteral("notes")));
        QVERIFY(hiddenTab->executeCommand(pdf::PDFAppCommands::rotatePagesRight()));
        fixture.settle();
        QVERIFY(sidebar->isTabVisible(QStringLiteral("attachments")));
        QVERIFY(sidebar->isTabVisible(QStringLiteral("notes")));
        QCOMPARE(pdf::PDFAppAttachments::getCollectCount(), attachmentsHidden);
        QCOMPARE(pdf::PDFAppNotes::getCollectCount(), notesHidden);
    }

    const int attachmentsBefore = pdf::PDFAppAttachments::getCollectCount();
    const int notesBefore = pdf::PDFAppNotes::getCollectCount();
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::attachments(fixture.folder()));
    QVERIFY(tab);
    QVERIFY(PanelsTestHelper::panel<pdf::PDFAppAttachmentsPanel>(tab, QStringLiteral("attachments")));
    QVERIFY(PanelsTestHelper::panel<pdf::PDFAppNotesPanel>(tab, QStringLiteral("notes")));
    fixture.settle();
    QCOMPARE(pdf::PDFAppAttachments::getCollectCount() - attachmentsBefore, 1);
    QCOMPARE(pdf::PDFAppNotes::getCollectCount() - notesBefore, 1);
    const std::shared_ptr<const std::vector<pdf::PDFAppAttachment>> list = pdf::PDFAppAttachments::getAttachments(tab->getDocument());
    QCOMPARE(list.get(), pdf::PDFAppAttachments::getAttachments(tab->getDocument()).get());
    QCOMPARE(list->size(), size_t(3));

    // A change (Rotate, a Reset change): once more each
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::rotatePagesRight()));
    fixture.settle();
    QCOMPARE(pdf::PDFAppAttachments::getCollectCount() - attachmentsBefore, 2);
    QCOMPARE(pdf::PDFAppNotes::getCollectCount() - notesBefore, 2);
    QVERIFY(list.get() != pdf::PDFAppAttachments::getAttachments(tab->getDocument()).get());

    // No document: an empty list, nothing stored
    QVERIFY(pdf::PDFAppAttachments::getAttachments(nullptr)->empty());
    QVERIFY(pdf::PDFAppNotes::getNotes(nullptr)->empty());
    pdf::PDFAppDocument loading(fixture.context.get(), nullptr);
    QVERIFY(pdf::PDFAppNotes::getNotes(&loading)->empty());
    QVERIFY(!loading.getDerivedData(QStringLiteral("notes")));
}

void AppDocumentPanelsTest::listGeometry()
{
    // The lists of the sidebar (195:2070 attachments, 195:3910 notes, 195:3445
    // bookmarks): rows 275 wide at x 12 of the sidebar, 2 px between two rows
    // (QListView::setSpacing puts its spacing on both rows - not 4 px)
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::attachments(fixture.folder()));
    pdf::PDFAppAttachmentsPanel* attachments = PanelsTestHelper::panel<pdf::PDFAppAttachmentsPanel>(tab, QStringLiteral("attachments"));
    QVERIFY(attachments);
    fixture.settle();

    pdf::PDFAppDocumentSidebar* bar = PanelsTestHelper::sidebar(tab);
    auto checkRows = [&bar](QListView* list, int firstRow)
    {
        QVERIFY(list);
        const QRect first = list->visualRect(list->model()->index(firstRow, 0));
        const QRect second = list->visualRect(list->model()->index(firstRow + 1, 0));
        QCOMPARE(second.top() - first.bottom() - 1, 2);
        QCOMPARE(list->viewport()->mapTo(bar, first.topLeft()).x(), 12);
        QCOMPARE(first.width(), bar->width() - 1 - 24);
    };
    checkRows(attachments->getListView(), 0);

    const QString notesPath = PanelsTestHelper::notes(fixture.folder());
    QVERIFY(!notesPath.isEmpty());
    pdf::PDFAppDocumentTab* notesTab = fixture.open(notesPath);
    pdf::PDFAppNotesPanel* notes = PanelsTestHelper::panel<pdf::PDFAppNotesPanel>(notesTab, QStringLiteral("notes"));
    QVERIFY(notes);
    fixture.settle();
    bar = PanelsTestHelper::sidebar(notesTab);
    checkRows(notes->getListView(), 1);
}

void AppDocumentPanelsTest::attachmentsList()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::attachments(fixture.folder()));
    pdf::PDFAppAttachmentsPanel* attachments = PanelsTestHelper::panel<pdf::PDFAppAttachmentsPanel>(tab, QStringLiteral("attachments"));
    QVERIFY(attachments);
    pdf::PDFAppAttachmentsModel* model = attachments->getModel();
    QCOMPARE(model->rowCount(), 3);
    QCOMPARE(attachments->getSubtitle(), QStringLiteral("3 files"));
    QCOMPARE(model->index(0).data().toString(), QStringLiteral("data.csv"));
    QCOMPARE(model->index(1).data().toString(), QStringLiteral("notes.txt"));
    QCOMPARE(model->getAttachment(2)->page, pdf::PDFInteger(0));
    QVERIFY(model->index(1).data(pdf::PDFComponentRoles::SubtitleRole).toString().startsWith(QStringLiteral("Text file")));
    QCOMPARE(model->index(1).data(pdf::PDFComponentRoles::IconNameRole).toString(), QStringLiteral("file-text"));
    QVERIFY(!model->getAttachment(-1));
    QVERIFY(!model->getAttachment(3));

    // The delegate: the save action in the row, Open in the context menu of a PDF only (O18)
    pdf::PDFAppAttachmentDelegate delegate(nullptr);
    QStyleOptionViewItem option;
    option.rect = QRect(0, 0, 276, 48);
    const QList<pdf::PDFRowDelegate::Action> actions = delegate.getActions(option, model->index(0));
    QCOMPARE(actions.size(), 1);
    QCOMPARE(actions[0].id, int(pdf::PDFAttachmentDelegate::SaveAction));

    // Kinds and sizes
    QString kind;
    QString icon;
    pdf::PDFAppAttachments::getKind(QStringLiteral("photo.JPG"), &kind, &icon);
    QCOMPARE(kind, QStringLiteral("Image"));
    QCOMPARE(icon, QStringLiteral("image"));
    pdf::PDFAppAttachments::getKind(QStringLiteral("budget.xlsx"), &kind, &icon);
    QCOMPARE(kind, QStringLiteral("Spreadsheet"));
    pdf::PDFAppAttachments::getKind(QStringLiteral("noextension"), &kind, &icon);
    QCOMPARE(kind, QStringLiteral("File"));
    QVERIFY(pdf::PDFAppAttachments::formatSize(-1).isEmpty());
    QVERIFY(!pdf::PDFAppAttachments::formatSize(84000).isEmpty());
    QVERIFY(!pdf::PDFAppAttachments::formatSize(1200000).isEmpty());
    {
        // "kind · size" in the language of the user interface also on a Czech
        // system (O17)
        pdftest::AppSystemLocaleGuard guard(pdftest::AppSystemLocaleGuard::czech());
        QCOMPARE(pdf::PDFAppAttachments::formatSize(84000), QStringLiteral("84 kB"));
        QCOMPARE(pdf::PDFAppAttachments::formatSize(1234567), QStringLiteral("1.2 MB"));
    }
    QVERIFY(pdf::PDFAppAttachments::collect(nullptr).empty());
}

void AppDocumentPanelsTest::attachmentsSave()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::attachments(fixture.folder()));
    pdf::PDFAppAttachmentsPanel* attachments = PanelsTestHelper::panel<pdf::PDFAppAttachmentsPanel>(tab, QStringLiteral("attachments"));
    QVERIFY(attachments);

    const QString target = fixture.path(QStringLiteral("saved.txt"));
    QString proposed;
    pdf::PDFAppPanelFileDialogs::setSaveHandler([&](QWidget*, const QString&, const QString& file, const QString&) { proposed = file; return target; });
    attachments->saveAttachment(1);
    QTRY_COMPARE(attachments->getRunningCount(), 0);
    QVERIFY(proposed.endsWith(QStringLiteral("notes.txt")));
    QFile file(target);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("Notes of the sample\n"));
    file.close();

    // The save action of the delegate saves too; a canceled dialog writes nothing
    pdf::PDFAppPanelFileDialogs::setSaveHandler([](QWidget*, const QString&, const QString&, const QString&) { return QString(); });
    attachments->saveAttachment(0);
    QCOMPARE(attachments->getRunningCount(), 0);

    // A folder, which cannot be written: an error box
    pdf::PDFAppPanelFileDialogs::setSaveHandler([&](QWidget*, const QString&, const QString&, const QString&) { return fixture.path(QStringLiteral("missing/folder/file.txt")); });
    attachments->saveAttachment(0);
    QTRY_VERIFY(PanelsTestHelper::findMessageBox(QStringLiteral("attachmentErrorBox")));
    fixture.closeMessageBoxes();
    attachments->saveAttachment(-1);
    attachments->saveAttachment(100);
}

void AppDocumentPanelsTest::attachmentsOpen()
{
    // O18: a PDF attachment opens in a new tab directly from the memory (no
    // temporary file, a copy without a file, not in Recent); another file is only
    // saved - a click on its row saves it
    pdftest::AppDocumentFixture fixture;
    const QByteArray inner = PanelsTestHelper::readFile(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("inner-source.pdf"), 3));
    QVERIFY(!inner.isEmpty());
    const QString path = PanelsTestHelper::attachmentFiles(fixture.folder(), QStringLiteral("open.pdf"), { { QStringLiteral("readme.txt"), QByteArray("data of readme.txt") },
                                                                                                         { QStringLiteral("inner.pdf"), inner } });
    QVERIFY(!path.isEmpty());
    pdf::PDFAppDocumentTab* tab = fixture.open(path);
    pdf::PDFAppAttachmentsPanel* attachments = PanelsTestHelper::panel<pdf::PDFAppAttachmentsPanel>(tab, QStringLiteral("attachments"));
    QVERIFY(attachments);
    const qsizetype recentCount = fixture.context->getRecent().getFiles().size();
    const int tabCount = fixture.window->getDocumentTabs().size();

    // The context menu offers Open only for the PDF
    pdf::PDFAppAttachmentDelegate delegate(nullptr);
    QStyleOptionViewItem option;
    option.rect = QRect(0, 0, 276, 48);
    QCOMPARE(delegate.getActions(option, attachments->getModel()->index(0)).size(), 2);
    QCOMPARE(delegate.getActions(option, attachments->getModel()->index(1)).size(), 1);

    // Another file: never opened, a click saves it
    QVERIFY(!attachments->openAttachment(1));
    QCOMPARE(attachments->getRunningCount(), 0);
    const QString target = fixture.path(QStringLiteral("readme-saved.txt"));
    QString proposed;
    pdf::PDFAppPanelFileDialogs::setSaveHandler([&](QWidget*, const QString&, const QString& file, const QString&) { proposed = file; return target; });
    attachments->activateAttachment(1);
    QTRY_COMPARE(attachments->getRunningCount(), 0);
    QVERIFY(proposed.endsWith(QStringLiteral("readme.txt")));
    QCOMPARE(PanelsTestHelper::readFile(target), QByteArray("data of readme.txt"));
    QCOMPARE(fixture.window->getDocumentTabs().size(), tabCount);

    // The PDF: a new tab, read from the memory
    pdf::PDFAppPanelFileDialogs::setSaveHandler([](QWidget*, const QString&, const QString&, const QString&) { return QString(); });
    attachments->activateAttachment(0);
    QTRY_VERIFY(PanelsTestHelper::findMemoryTab(fixture, QStringLiteral("inner.pdf")));
    pdf::PDFAppDocumentTab* innerTab = PanelsTestHelper::findMemoryTab(fixture, QStringLiteral("inner.pdf"));
    QTRY_VERIFY(innerTab->getDocument()->isReady());
    pdf::PDFAppDocument* document = innerTab->getDocument();
    QCOMPARE(document->getPageCount(), pdf::PDFInteger(3));
    QVERIFY(document->getFilePath().isEmpty());
    QVERIFY(document->isCopy());
    QVERIFY(document->isModified());
    QCOMPARE(document->getUnsavedChangeCount(), -1);
    QCOMPARE(document->getOriginalData(), inner);
    QCOMPARE(document->getOriginFilePath(), QFileInfo(path).canonicalFilePath());
    QCOMPARE(innerTab->getTabTitle(), QStringLiteral("inner.pdf"));
    QCOMPARE(fixture.window->getDocumentTabs().size(), tabCount + 1);

    // Not recorded in Recent, the registry does not find it by a path
    fixture.settle();
    QCOMPARE(fixture.context->getRecent().getFiles().size(), recentCount);
    QVERIFY(!fixture.context->getDocumentRegistry()->findByCanonicalPath(QString()));

    // The same attachment again: another tab (a copy, no file to find)
    attachments->openAttachment(0);
    QTRY_COMPARE(fixture.window->getDocumentTabs().size(), tabCount + 2);

    // A PDF attachment, which is damaged: the open-failed dialog names the attachment
    const QString damaged = PanelsTestHelper::attachmentFiles(fixture.folder(), QStringLiteral("damaged-attachment.pdf"), { { QStringLiteral("broken.pdf"), QByteArray("not a PDF at all") } });
    pdf::PDFAppDocumentTab* damagedTab = fixture.open(damaged);
    pdf::PDFAppAttachmentsPanel* damagedAttachments = PanelsTestHelper::panel<pdf::PDFAppAttachmentsPanel>(damagedTab, QStringLiteral("attachments"));
    QVERIFY(damagedAttachments);
    const int countBefore = fixture.window->getDocumentTabs().size();
    QVERIFY(damagedAttachments->openAttachment(0));
    QTRY_VERIFY(fixture.window->getOpenFailedDialog());
    QCOMPARE(fixture.window->getDocumentTabs().size(), countBefore);
    fixture.closeMessageBoxes();
}

void AppDocumentPanelsTest::attachmentsOpenEncrypted()
{
    // The password screen of an encrypted attachment works from the memory too
    pdftest::AppDocumentFixture fixture;
    const QByteArray secret = PanelsTestHelper::readFile(pdftest::AppDocumentSamples::userPassword(fixture.folder()));
    QVERIFY(!secret.isEmpty());
    const QString path = PanelsTestHelper::attachmentFiles(fixture.folder(), QStringLiteral("with-secret.pdf"), { { QStringLiteral("secret.pdf"), secret } });
    pdf::PDFAppDocumentTab* tab = fixture.open(path);
    pdf::PDFAppAttachmentsPanel* attachments = PanelsTestHelper::panel<pdf::PDFAppAttachmentsPanel>(tab, QStringLiteral("attachments"));
    QVERIFY(attachments);
    QVERIFY(attachments->openAttachment(0));
    QTRY_VERIFY(PanelsTestHelper::findMemoryTab(fixture, QStringLiteral("secret.pdf")));
    QPointer<pdf::PDFAppDocumentTab> secretTab = PanelsTestHelper::findMemoryTab(fixture, QStringLiteral("secret.pdf"));
    QTRY_COMPARE(secretTab->getDocument()->getState(), pdf::PDFAppDocumentState::PasswordRequired);
    QVERIFY(secretTab->isStateScreenShown());

    // A wrong password, then the right one
    QVERIFY(secretTab->getLoader()->setPassword(QStringLiteral("wrong")));
    QTRY_VERIFY(secretTab->getDocument()->isPasswordWrong());
    QVERIFY(secretTab->getLoader()->setPassword(pdftest::AppDocumentSamples::getUserPassword()));
    QTRY_VERIFY(secretTab->getDocument()->isReady());
    QCOMPARE(secretTab->getDocument()->getPageCount(), pdf::PDFInteger(2));
    QVERIFY(secretTab->getDocument()->isCopy());
    QVERIFY(secretTab->getDocument()->getFilePath().isEmpty());
    QCOMPARE(secretTab->getTabTitle(), QStringLiteral("secret.pdf"));
    QVERIFY(secretTab->isTabPasswordProtected());

    // An empty document from the memory fails (the loader refuses a second start)
    pdf::PDFAppDocumentTab emptyTab(fixture.context.get(), nullptr);
    QVERIFY(emptyTab.openFromMemory(QByteArray(), QStringLiteral("empty.pdf"), QString()));
    QVERIFY(!emptyTab.openFromMemory(QByteArray("x"), QStringLiteral("again.pdf"), QString()));
    QTRY_COMPARE(emptyTab.getDocument()->getState(), pdf::PDFAppDocumentState::Failed);
    QCOMPARE(emptyTab.getDocument()->getTitle(), QStringLiteral("empty.pdf"));
}

void AppDocumentPanelsTest::attachmentsMarkOfTheWeb()
{
#ifdef Q_OS_WIN
    // O18: a saved attachment takes over the Mark of the Web of its PDF (NTFS)
    pdftest::AppDocumentFixture fixture;
    const QByteArray inner = PanelsTestHelper::readFile(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("motw-inner.pdf"), 1));
    const QString path = PanelsTestHelper::attachmentFiles(fixture.folder(), QStringLiteral("downloaded.pdf"), { { QStringLiteral("notes.txt"), QByteArray("data of notes.txt") },
                                                                                                               { QStringLiteral("inner.pdf"), inner } });
    const QByteArray zone("[ZoneTransfer]\r\nZoneId=3\r\nHostUrl=https://example.com/downloaded.pdf\r\n");
    {
        QFile stream(QDir::toNativeSeparators(path) + QStringLiteral(":Zone.Identifier"));
        if (!stream.open(QIODevice::WriteOnly) || stream.write(zone) != zone.size())
        {
            QSKIP("The folder of the test has no alternate data streams (not NTFS)");
        }
    }

    pdf::PDFAppDocumentTab* tab = fixture.open(path);
    pdf::PDFAppAttachmentsPanel* attachments = PanelsTestHelper::panel<pdf::PDFAppAttachmentsPanel>(tab, QStringLiteral("attachments"));
    QVERIFY(attachments);
    const QString target = fixture.path(QStringLiteral("notes-saved.txt"));
    pdf::PDFAppPanelFileDialogs::setSaveHandler([&](QWidget*, const QString&, const QString&, const QString&) { return target; });
    attachments->saveAttachment(1);
    QTRY_COMPARE(attachments->getRunningCount(), 0);
    QCOMPARE(PanelsTestHelper::readFile(target), QByteArray("data of notes.txt"));
    QCOMPARE(PanelsTestHelper::readFile(QDir::toNativeSeparators(target) + QStringLiteral(":Zone.Identifier")), zone);

    // A PDF opened from the memory keeps the file of its origin for its own attachments
    attachments->openAttachment(0);
    QTRY_VERIFY(PanelsTestHelper::findMemoryTab(fixture, QStringLiteral("inner.pdf")));
    QCOMPARE(PanelsTestHelper::findMemoryTab(fixture, QStringLiteral("inner.pdf"))->getDocument()->getOriginFilePath(), QFileInfo(path).canonicalFilePath());

    // Without a mark nothing is written, and nothing fails
    const QString plain = fixture.path(QStringLiteral("plain.txt"));
    const QString copy = fixture.path(QStringLiteral("copy.txt"));
    QFile plainFile(plain);
    QVERIFY(plainFile.open(QIODevice::WriteOnly));
    plainFile.write("x");
    plainFile.close();
    QFile copyFile(copy);
    QVERIFY(copyFile.open(QIODevice::WriteOnly));
    copyFile.write("y");
    copyFile.close();
    QVERIFY(!pdf::PDFAppAttachments::copyMarkOfTheWeb(plain, copy));
    QVERIFY(!QFile::exists(QDir::toNativeSeparators(copy) + QStringLiteral(":Zone.Identifier")));
    QVERIFY(!pdf::PDFAppAttachments::copyMarkOfTheWeb(QString(), copy));
    QVERIFY(!pdf::PDFAppAttachments::copyMarkOfTheWeb(path, QString()));
    QVERIFY(pdf::PDFAppAttachments::copyMarkOfTheWeb(path, copy));
    QCOMPARE(PanelsTestHelper::readFile(QDir::toNativeSeparators(copy) + QStringLiteral(":Zone.Identifier")), zone);
#else
    QVERIFY(!pdf::PDFAppAttachments::copyMarkOfTheWeb(QStringLiteral("/tmp/a.pdf"), QStringLiteral("/tmp/b.txt")));
    QSKIP("The Mark of the Web is a stream of NTFS (Windows only)");
#endif
}

void AppDocumentPanelsTest::attachmentsRefused()
{
    // Programs and scripts are never opened: a click saves them as any other file
    pdftest::AppDocumentFixture fixture;
    const QString path = PanelsTestHelper::attachments(fixture.folder(), QStringLiteral("refused.pdf"),
                                                       { QStringLiteral("setup.exe"), QStringLiteral("run.BAT"), QStringLiteral("invoice.pdf.exe"),
                                                         QStringLiteral("tool.exe."), QStringLiteral("script.js"), QStringLiteral("x.ps1") });
    pdf::PDFAppDocumentTab* tab = fixture.open(path);
    pdf::PDFAppAttachmentsPanel* attachments = PanelsTestHelper::panel<pdf::PDFAppAttachmentsPanel>(tab, QStringLiteral("attachments"));
    QVERIFY(attachments);
    QCOMPARE(attachments->getModel()->rowCount(), 6);
    const int tabCount = fixture.window->getDocumentTabs().size();

    int dialogs = 0;
    pdf::PDFAppPanelFileDialogs::setSaveHandler([&](QWidget*, const QString&, const QString&, const QString&) { ++dialogs; return QString(); });
    for (int row = 0; row < 6; ++row)
    {
        QVERIFY(!attachments->openAttachment(row));
        attachments->activateAttachment(row);
        fixture.settle();
        QCOMPARE(attachments->getRunningCount(), 0);
        QVERIFY(!PanelsTestHelper::findMessageBox(QString()));
    }
    QCOMPARE(dialogs, 6);
    QCOMPARE(fixture.window->getDocumentTabs().size(), tabCount);

    // A program can be saved (D8)
    const QString target = fixture.path(QStringLiteral("setup-saved.bin"));
    pdf::PDFAppPanelFileDialogs::setSaveHandler([&](QWidget*, const QString&, const QString&, const QString&) { return target; });
    attachments->saveAttachment(0);
    QTRY_VERIFY(QFile::exists(target));
    QVERIFY(!(QFileInfo(target).permissions() & (QFileDevice::ExeOwner | QFileDevice::ExeOther | QFileDevice::ExeGroup)));
}

void AppDocumentPanelsTest::attachmentsFileNames()
{
    using pdf::PDFAppAttachments;
    QCOMPARE(PDFAppAttachments::sanitizeFileName(QStringLiteral("../../etc/passwd")), QStringLiteral("passwd"));
    QCOMPARE(PDFAppAttachments::sanitizeFileName(QStringLiteral("C:\\Windows\\evil.txt")), QStringLiteral("evil.txt"));
    QCOMPARE(PDFAppAttachments::sanitizeFileName(QStringLiteral("..")), QStringLiteral("attachment"));
    QCOMPARE(PDFAppAttachments::sanitizeFileName(QString()), QStringLiteral("attachment"));
    QCOMPARE(PDFAppAttachments::sanitizeFileName(QStringLiteral("a:b*c?.txt")), QStringLiteral("a_b_c_.txt"));
    QCOMPARE(PDFAppAttachments::sanitizeFileName(QStringLiteral("file.txt:evil.exe")), QStringLiteral("file.txt_evil.exe"));
    QCOMPARE(PDFAppAttachments::sanitizeFileName(QStringLiteral("tool.exe. . ")), QStringLiteral("tool.exe"));
    QCOMPARE(PDFAppAttachments::sanitizeFileName(QStringLiteral("CON.txt")), QStringLiteral("_CON.txt"));
    QCOMPARE(PDFAppAttachments::sanitizeFileName(QStringLiteral("nul")), QStringLiteral("_nul"));
    QCOMPARE(PDFAppAttachments::sanitizeFileName(QStringLiteral(".hidden")), QStringLiteral("hidden"));
    QCOMPARE(PDFAppAttachments::sanitizeFileName(QStringLiteral("a\x01\x7f" "b.txt")), QStringLiteral("a__b.txt"));
    QCOMPARE(PDFAppAttachments::sanitizeFileName(QStringLiteral("x\u202Etxt.exe")), QStringLiteral("x_txt.exe"));

    const QString longName = QString(500, QChar(u'a')) + QStringLiteral(".docx");
    const QString shortened = PDFAppAttachments::sanitizeFileName(longName);
    QCOMPARE(shortened.size(), PDFAppAttachments::MAXIMUM_FILE_NAME_LENGTH);
    QVERIFY(shortened.endsWith(QStringLiteral(".docx")));

    QVERIFY(PDFAppAttachments::isExecutable(QStringLiteral("SETUP.EXE")));
    QVERIFY(PDFAppAttachments::isExecutable(QStringLiteral("invoice.pdf.exe")));
    QVERIFY(PDFAppAttachments::isExecutable(QStringLiteral("tool.exe.")));
    QVERIFY(PDFAppAttachments::isExecutable(QStringLiteral("x/y/run.sh")));
    QVERIFY(PDFAppAttachments::isExecutable(QStringLiteral("app.AppImage")));
    QVERIFY(PDFAppAttachments::isExecutable(QStringLiteral("macro.vbs")));
    QVERIFY(!PDFAppAttachments::isExecutable(QStringLiteral("report.pdf")));
    QVERIFY(!PDFAppAttachments::isExecutable(QStringLiteral("readme")));
    QVERIFY(!PDFAppAttachments::isExecutable(QStringLiteral("data.csv")));
    QVERIFY(PDFAppAttachments::isPdf(QStringLiteral("Inner.PDF")));
    QVERIFY(!PDFAppAttachments::isPdf(QStringLiteral("inner.pdf.exe")));

    // Random names never crash and never give a path or an executable outside of the list
    QRandomGenerator random(7);
    for (int i = 0; i < 2000; ++i)
    {
        QString name;
        const int length = random.bounded(0, 300);
        for (int j = 0; j < length; ++j)
        {
            name.append(QChar(char16_t(random.bounded(1, 0xFFFF))));
        }
        const QString result = PDFAppAttachments::sanitizeFileName(name);
        QVERIFY(!result.isEmpty());
        QVERIFY(result.size() <= PDFAppAttachments::MAXIMUM_FILE_NAME_LENGTH + 1);
        QVERIFY(!result.contains(QChar(u'/')) && !result.contains(QChar(u'\\')) && !result.contains(QChar(u':')));
        QVERIFY(!result.startsWith(QChar(u'.')));
    }
}

void AppDocumentPanelsTest::attachmentsDeletedDuringWrite()
{
    pdftest::AppDocumentFixture fixture;
    const QByteArray inner = PanelsTestHelper::readFile(pdftest::AppDocumentSamples::large(fixture.folder(), 300));
    const QString path = PanelsTestHelper::attachmentFiles(fixture.folder(), QStringLiteral("deleted.pdf"), { { QStringLiteral("a.txt"), QByteArray("data of a.txt") },
                                                                                                            { QStringLiteral("inner.pdf"), inner } });
    pdf::PDFAppDocumentTab* tab = fixture.open(path);
    pdf::PDFAppAttachmentsPanel* attachments = PanelsTestHelper::panel<pdf::PDFAppAttachmentsPanel>(tab, QStringLiteral("attachments"));
    QVERIFY(attachments);
    const int tabCount = fixture.window->getDocumentTabs().size();

    // The tab closed while the writes and the decoding of a PDF run: the results
    // are dropped (no new tab), nothing crashes
    const QString target = fixture.path(QStringLiteral("late.txt"));
    pdf::PDFAppPanelFileDialogs::setSaveHandler([&](QWidget*, const QString&, const QString&, const QString&) { return target; });
    for (int i = 0; i < 5; ++i)
    {
        attachments->saveAttachment(i % 2);
    }
    QVERIFY(attachments->openAttachment(1));
    fixture.window->closeTab(tab);
    fixture.settle();
    QThreadPool::globalInstance()->waitForDone(10000);
    fixture.settle();
    QCOMPARE(fixture.window->getDocumentTabs().size(), tabCount - 1);

    // The new tab closed, while it reads the document from the memory; the window
    // deleted, while another one reads
    tab = fixture.open(path);
    attachments = PanelsTestHelper::panel<pdf::PDFAppAttachmentsPanel>(tab, QStringLiteral("attachments"));
    QVERIFY(attachments);
    QVERIFY(attachments->openAttachment(1));
    QTRY_VERIFY(PanelsTestHelper::findMemoryTab(fixture, QStringLiteral("inner.pdf")));
    QPointer<pdf::PDFAppDocumentTab> innerTab = PanelsTestHelper::findMemoryTab(fixture, QStringLiteral("inner.pdf"));
    if (innerTab->getDocument()->getState() == pdf::PDFAppDocumentState::Loading)
    {
        fixture.window->closeTab(innerTab);
    }
    else
    {
        // Already read (a fast machine): the copy asks about its changes - Don't Save
        delete innerTab.data();
    }
    fixture.settle();
    QVERIFY(attachments->openAttachment(1));
    QTRY_VERIFY(PanelsTestHelper::findMemoryTab(fixture, QStringLiteral("inner.pdf")));
    delete fixture.window.data();
    QThreadPool::globalInstance()->waitForDone(10000);
    fixture.settle();
}

void AppDocumentPanelsTest::attachmentsDocumentChanged()
{
    // The document changes while the save dialog is open: the rows change with it,
    // but the chosen attachment is written - never the attachment, which has its
    // row now
    pdftest::AppDocumentFixture fixture;
    const QString first = PanelsTestHelper::attachments(fixture.folder(), QStringLiteral("first.pdf"), { QStringLiteral("a-readme.txt"), QStringLiteral("b-data.txt") });
    const QString second = PanelsTestHelper::attachments(fixture.folder(), QStringLiteral("second.pdf"), { QStringLiteral("a-readme.txt"), QStringLiteral("b-evil.exe") });
    QVERIFY(!first.isEmpty() && !second.isEmpty());
    pdf::PDFAppDocumentTab* tab = fixture.open(first);
    pdf::PDFAppAttachmentsPanel* attachments = PanelsTestHelper::panel<pdf::PDFAppAttachmentsPanel>(tab, QStringLiteral("attachments"));
    QVERIFY(attachments);
    QCOMPARE(attachments->getModel()->getAttachment(1)->name, QStringLiteral("b-data.txt"));

    const QString chosen = fixture.path(QStringLiteral("chosen-first.bin"));
    pdf::PDFAppPanelFileDialogs::setSaveHandler([&](QWidget*, const QString&, const QString& proposed, const QString&)
    {
        if (!proposed.endsWith(QStringLiteral("b-data.txt")))
        {
            return QString();
        }
        PanelsTestHelper::replaceDocument(tab, second);
        return chosen;
    });
    attachments->saveAttachment(1);
    QTRY_COMPARE(attachments->getRunningCount(), 0);
    fixture.settle();
    QCOMPARE(attachments->getModel()->getAttachment(1)->name, QStringLiteral("b-evil.exe"));
    QCOMPARE(PanelsTestHelper::readFile(chosen), QByteArray("data of b-data.txt"));

    // The save dialog: the document changes back while it runs
    const QString target = fixture.path(QStringLiteral("chosen.bin"));
    pdf::PDFAppPanelFileDialogs::setSaveHandler([&](QWidget*, const QString&, const QString& proposed, const QString&)
    {
        if (!proposed.endsWith(QStringLiteral("b-evil.exe")))
        {
            return QString();
        }
        PanelsTestHelper::replaceDocument(tab, first);
        return target;
    });
    attachments->saveAttachment(1);
    QTRY_COMPARE(attachments->getRunningCount(), 0);
    fixture.settle();
    QCOMPARE(attachments->getModel()->getAttachment(1)->name, QStringLiteral("b-data.txt"));
    QFile savedFile(target);
    QVERIFY(savedFile.open(QIODevice::ReadOnly));
    QCOMPARE(savedFile.readAll(), QByteArray("data of b-evil.exe"));
}

void AppDocumentPanelsTest::bookmarkRepository()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFBookmarkRepository repository(fixture.context->getDatabase());
    const QString file = QDir(fixture.folder()).absoluteFilePath(QStringLiteral("book.pdf"));

    qint64 id5 = 0;
    qint64 id2 = 0;
    qint64 id9 = 0;
    QVERIFY(!repository.addBookmark(file, 5, QStringLiteral("Five"), &id5).isError());
    QVERIFY(!repository.addBookmark(file, 9, QStringLiteral("Nine"), &id9).isError());
    QVERIFY(!repository.addBookmark(file, 2, QStringLiteral("  Two\twith\ntabs  "), &id2).isError());
    QVERIFY(id5 > 0 && id2 > 0 && id9 > 0);

    // In the order of the pages
    std::vector<pdf::PDFBookmark> bookmarks = repository.getBookmarks(file);
    QCOMPARE(bookmarks.size(), size_t(3));
    QCOMPARE(bookmarks[0].page, qint64(2));
    QCOMPARE(bookmarks[0].title, QStringLiteral("Two with tabs"));
    QCOMPARE(bookmarks[2].page, qint64(9));

    // Reorder (unknown and duplicate ids are ignored), rename, remove
    QVERIFY(!repository.setOrder(file, { id9, id9, 12345, id2 }).isError());
    bookmarks = repository.getBookmarks(file);
    QCOMPARE(bookmarks[0].id, id9);
    QCOMPARE(bookmarks[1].id, id2);
    QCOMPARE(bookmarks[2].id, id5);
    QVERIFY(!repository.renameBookmark(id5, QString(1000, QChar(u'x'))).isError());
    QCOMPARE(repository.getBookmarks(file)[2].title.size(), pdf::PDFBookmarkRepository::MAXIMUM_TITLE_LENGTH);
    QVERIFY(!repository.removeBookmark(id9).isError());
    QCOMPARE(repository.getBookmarks(file).size(), size_t(2));

    // Another file is separate; invalid input is refused
    QVERIFY(repository.getBookmarks(QDir(fixture.folder()).absoluteFilePath(QStringLiteral("other.pdf"))).empty());
    QVERIFY(repository.addBookmark(QString(), 1, QString()).isError());
    QVERIFY(repository.addBookmark(QStringLiteral("relative.pdf"), 1, QString()).isError());
    QVERIFY(repository.addBookmark(file, -1, QString()).isError());
    QVERIFY(repository.addBookmark(file, pdf::PDFBookmarkRepository::MAXIMUM_PAGE + 1, QString()).isError());
    QVERIFY(repository.setOrder(QString(), { }).isError());
    QVERIFY(repository.importBookmarks(QString(), { }).isError());
    QVERIFY(repository.getBookmarks(QStringLiteral("\\\\.\\PhysicalDrive0")).empty());

    // Import: duplicates skipped, sorted by the pages
    int added = 0;
    QVERIFY(!repository.importBookmarks(file, { { 2, QStringLiteral("Two with tabs") }, { 0, QStringLiteral("Zero") }, { 7, QString() }, { -3, QString() } }, &added).isError());
    QCOMPARE(added, 2);
    bookmarks = repository.getBookmarks(file);
    QCOMPARE(bookmarks.size(), size_t(4));
    QCOMPARE(bookmarks.front().page, qint64(0));

    // A database without a connection
    pdf::PDFBookmarkRepository empty(nullptr);
    QVERIFY(empty.getBookmarks(file).empty());
    QVERIFY(empty.addBookmark(file, 1, QString()).isError());
}

void AppDocumentPanelsTest::bookmarkRepositoryHostileRows()
{
    pdftest::AppDocumentFixture fixture;
    const QString file = QDir(fixture.folder()).absoluteFilePath(QStringLiteral("hostile.pdf"));
    pdf::PDFDatabaseConnection* connection = fixture.context->getDatabase()->getConnection();
    {
        pdf::PDFDatabaseTransaction transaction(connection);
        QVERIFY(transaction.isActive());
        QVERIFY(!connection->execute("INSERT INTO bookmark(file_path, page, title, auto, position) VALUES "
                                     "('x', 1, 'not my file', 0, 1)").isError());
        pdf::PDFDatabaseStatement statement(connection, "INSERT INTO bookmark(file_path, page, title, auto, position) VALUES (?1, ?2, ?3, ?4, ?5)");
        const QList<QVariantList> rows =
        {
            { 1, QStringLiteral("good"), 0, 1 },
            { QStringLiteral("text page"), QStringLiteral("bad page"), 0, 2 },
            { -5, QStringLiteral("negative"), 0, 3 },
            { qint64(1) << 60, QStringLiteral("huge"), 0, 4 },
            { 3, QByteArray("blob title"), 0, 5 },
            { 4, QStringLiteral("auto of another version"), 1, 6 },
            { 5, QString(5000, QChar(u'y')), 0, QStringLiteral("text position") },
            { 6, QStringLiteral(""), 0, 2.5 }
        };
        for (const QVariantList& row : rows)
        {
            statement.bindText(1, file);
            for (int i = 0; i < row.size(); ++i)
            {
                statement.bindVariant(i + 2, row[i]);
            }
            QVERIFY2(statement.execute(), qPrintable(statement.getError().toString()));
        }
        QVERIFY(transaction.commit());
    }

    const std::vector<pdf::PDFBookmark> bookmarks = pdf::PDFBookmarkRepository(fixture.context->getDatabase()).getBookmarks(file);
    QStringList titles;
    for (const pdf::PDFBookmark& bookmark : bookmarks)
    {
        titles << bookmark.title;
        QVERIFY(bookmark.page >= 0 && bookmark.page <= pdf::PDFBookmarkRepository::MAXIMUM_PAGE);
        QVERIFY(bookmark.title.size() <= pdf::PDFBookmarkRepository::MAXIMUM_TITLE_LENGTH);
    }
    QVERIFY(titles.contains(QStringLiteral("good")));
    QVERIFY(!titles.contains(QStringLiteral("bad page")));
    QVERIFY(!titles.contains(QStringLiteral("negative")));
    QVERIFY(!titles.contains(QStringLiteral("huge")));
    QVERIFY(!titles.contains(QStringLiteral("auto of another version")));
    QVERIFY(titles.contains(QString()));

    // A document with these rows shows the valid ones only
    QVERIFY(QFile::copy(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("source.pdf"), 7), file));
    pdf::PDFAppDocumentTab* tab = fixture.open(file);
    pdf::PDFAppBookmarksPanel* panel = PanelsTestHelper::panel<pdf::PDFAppBookmarksPanel>(tab, QStringLiteral("bookmarks"));
    QVERIFY(panel);
    QVERIFY(panel->getModel()->rowCount() >= 1);
}

void AppDocumentPanelsTest::bookmarkJson()
{
    std::vector<pdf::PDFBookmark> bookmarks(2);
    bookmarks[0].page = 0;
    bookmarks[0].title = QStringLiteral("First");
    bookmarks[1].page = 4;
    bookmarks[1].title = QStringLiteral("Fünfte – \"quoted\"");
    const QByteArray json = pdf::PDFBookmarkRepository::toJson(bookmarks);

    std::vector<pdf::PDFBookmarkEntry> entries;
    QString error;
    QVERIFY(pdf::PDFBookmarkRepository::fromJson(json, 10, &entries, &error));
    QCOMPARE(entries.size(), size_t(2));
    QCOMPARE(entries[1].page, qint64(4));
    QCOMPARE(entries[1].title, bookmarks[1].title);

    // Pages out of the document are skipped
    QVERIFY(pdf::PDFBookmarkRepository::fromJson(json, 3, &entries, &error));
    QCOMPARE(entries.size(), size_t(1));

    // Not bookmarks, a newer version, a huge file, hostile entries
    QVERIFY(!pdf::PDFBookmarkRepository::fromJson(QByteArray("not json"), 10, &entries, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!pdf::PDFBookmarkRepository::fromJson(QByteArray("[1,2,3]"), 10, &entries, &error));
    QVERIFY(!pdf::PDFBookmarkRepository::fromJson(QByteArray("{\"format\":\"other\",\"version\":1,\"bookmarks\":[]}"), 10, &entries, &error));
    QVERIFY(!pdf::PDFBookmarkRepository::fromJson(QByteArray("{\"format\":\"pdf4qt-bookmarks\",\"version\":99,\"bookmarks\":[]}"), 10, &entries, &error));
    QVERIFY(!pdf::PDFBookmarkRepository::fromJson(QByteArray(pdf::PDFBookmarkRepository::MAXIMUM_IMPORT_SIZE + 1, ' '), 10, &entries, &error));
    QVERIFY(pdf::PDFBookmarkRepository::fromJson(QByteArray("{\"format\":\"pdf4qt-bookmarks\",\"version\":1,\"bookmarks\":["
                                                            "{\"page\":\"3\"},{\"page\":1.5},{\"page\":-1},{\"page\":1e300},{\"page\":0},null,7,"
                                                            "{\"page\":2,\"title\":{\"x\":1}},{\"page\":3,\"title\":\"ok\\u0001\"}]}"), 10, &entries, &error));
    QCOMPARE(entries.size(), size_t(2));
    QCOMPARE(entries[0].page, qint64(1));
    QVERIFY(entries[0].title.isEmpty());
    QCOMPARE(entries[1].title, QStringLiteral("ok"));

    QCOMPARE(pdf::PDFBookmarkRepository::normalizeTitle(QString(300, QChar(u'a'))).size(), pdf::PDFBookmarkRepository::MAXIMUM_TITLE_LENGTH);
    QString surrogates;
    for (int i = 0; i < 200; ++i)
    {
        surrogates += QString::fromUcs4(U"\U0001F600");
    }
    const QString cut = pdf::PDFBookmarkRepository::normalizeTitle(surrogates);
    QVERIFY(!cut.back().isHighSurrogate());
}

void AppDocumentPanelsTest::bookmarksPanel()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::outline(fixture.folder()));
    pdf::PDFAppBookmarksPanel* panel = PanelsTestHelper::panel<pdf::PDFAppBookmarksPanel>(tab, QStringLiteral("bookmarks"));
    QVERIFY(panel);
    QCOMPARE(panel->getModel()->rowCount(), 0);
    QCOMPARE(panel->getSubtitle(), QStringLiteral("No bookmarks"));
    QVERIFY(panel->getAddButton()->isEnabled());
    QCOMPARE(panel->getAddButton()->size(), QSize(32, 32));

    // Add (the header button) of the current page: the title of its chapter
    tab->getView()->goToPage(2);
    fixture.settle();
    panel->getAddButton()->click();
    QTRY_COMPARE(panel->getModel()->rowCount(), 1);
    QCOMPARE(panel->getModel()->index(0).data().toString(), QStringLiteral("Page 3"));
    QCOMPARE(panel->getModel()->index(0).data(pdf::PDFComponentRoles::SubtitleRole).toString(), QStringLiteral("Section 1.1"));
    QVERIFY(panel->getModel()->index(0).data(pdf::PDFComponentRoles::TagRole).toString().isEmpty());
    QCOMPARE(panel->getSubtitle(), QStringLiteral("1 bookmark"));

    // The bookmark of the current page is selected
    QTRY_VERIFY(panel->getListView()->selectionModel()->isSelected(panel->getModel()->index(0)));

    // The same page is not added twice
    panel->getAddButton()->click();
    fixture.settle();
    QCOMPARE(panel->getModel()->rowCount(), 1);

    // A click goes to the page
    tab->getView()->goToPage(0);
    fixture.settle();
    panel->goToBookmark(0);
    QTRY_COMPARE(tab->getView()->getCurrentPageIndex(), pdf::PDFInteger(2));

    // The More menu
    QPointer<QMenu> menu = panel->createMoreMenu();
    QVERIFY(PanelsTestHelper::findAction(menu, QStringLiteral("exportBookmarks"))->isEnabled());
    QVERIFY(PanelsTestHelper::findAction(menu, QStringLiteral("importBookmarks"))->isEnabled());
    QVERIFY(PanelsTestHelper::findAction(menu, QStringLiteral("generateBookmarks"))->isCheckable());
    QVERIFY(!PanelsTestHelper::findAction(menu, QStringLiteral("generateBookmarks"))->isChecked());
    delete menu;
    panel->getMoreButton()->click();
    fixture.settle();
    QMenu* shown = panel->findChild<QMenu*>(QStringLiteral("bookmarkMoreMenu"));
    QVERIFY(shown);
    shown->close();
    fixture.settle();

    // Keyboard: Enter goes to the page
    tab->getView()->goToPage(4);
    fixture.settle();
    panel->getListView()->setCurrentIndex(panel->getModel()->index(0));
    QTest::keyClick(panel->getListView(), Qt::Key_Return);
    QTRY_COMPARE(tab->getView()->getCurrentPageIndex(), pdf::PDFInteger(2));

    // Invalid rows
    panel->goToBookmark(-1);
    panel->renameBookmark(100);
    panel->moveBookmark(100, 1);
    panel->removeBookmark(-3);
    QVERIFY(!panel->createContextMenu(100));
}

void AppDocumentPanelsTest::bookmarksCommands()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("simple.pdf"), 10));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();

    // The commands work without the panel being shown
    QVERIFY(!PanelsTestHelper::sidebar(tab)->findPanel(QStringLiteral("bookmarks")));
    for (const QString& command : pdf::PDFAppDocumentCommandTable::getCommands(pdf::PDFAppDocumentPackage::Panels))
    {
        QVERIFY2(tab->isCommandEnabled(command), qPrintable(command));
    }

    for (const int page : { 7, 2, 5 })
    {
        view->goToPage(page);
        fixture.settle();
        QVERIFY(tab->executeCommand(pdf::PDFAppCommands::addBookmark()));
    }
    const std::vector<pdf::PDFAppBookmarkItem> items = pdf::PDFAppBookmarks::getItems(fixture.context.get(), tab->getDocument());
    QCOMPARE(items.size(), size_t(3));
    QCOMPARE(items[0].page, pdf::PDFInteger(2));
    QCOMPARE(items[2].page, pdf::PDFInteger(7));

    // Next / previous by the pages
    view->goToPage(0);
    fixture.settle();
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::nextBookmark()));
    QTRY_COMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(2));
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::nextBookmark()));
    QTRY_COMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(5));
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::nextBookmark()));
    QTRY_COMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(7));
    QVERIFY(!pdf::PDFAppBookmarks::goToNext(view, true));
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::previousBookmark()));
    QTRY_COMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(5));
    view->goToPage(1);
    fixture.settle();
    QVERIFY(!pdf::PDFAppBookmarks::goToNext(view, false));

    // The panel shows them
    pdf::PDFAppBookmarksPanel* panel = PanelsTestHelper::panel<pdf::PDFAppBookmarksPanel>(tab, QStringLiteral("bookmarks"));
    QCOMPARE(panel->getModel()->rowCount(), 3);

    // Defensive: no view, no document
    QVERIFY(!pdf::PDFAppBookmarks::goToNext(nullptr, true));
    QCOMPARE(pdf::PDFAppBookmarks::addCurrentPage(nullptr, nullptr), qint64(0));
    pdf::PDFAppBookmarks::exportBookmarks(nullptr, nullptr);
    pdf::PDFAppBookmarks::importBookmarks(nullptr, nullptr);
    QVERIFY(pdf::PDFAppBookmarks::getItems(nullptr, nullptr).empty());
    QVERIFY(pdf::PDFAppBookmarks::getDefaultTitle(nullptr, 0).isEmpty());
}

void AppDocumentPanelsTest::bookmarksAutoGenerate()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::outline(fixture.folder()));
    pdf::PDFAppBookmarksPanel* panel = PanelsTestHelper::panel<pdf::PDFAppBookmarksPanel>(tab, QStringLiteral("bookmarks"));
    QVERIFY(panel);

    // O10: off by default
    QVERIFY(!fixture.context->getSettings().getBool(pdf::PDFSettingKeys::bookmarksAutoGenerate()));
    tab->getView()->goToPage(3);
    fixture.settle();
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::addBookmark()));
    QTRY_COMPARE(panel->getModel()->rowCount(), 1);

    // On: the main chapters (Introduction p1, Chapter 1 p2, Appendix p5) merged by the pages
    QPointer<QMenu> menu = panel->createMoreMenu();
    PanelsTestHelper::findAction(menu, QStringLiteral("generateBookmarks"))->setChecked(true);
    delete menu;
    QTRY_COMPARE(panel->getModel()->rowCount(), 4);

    // A personal bookmark of a chapter page replaces its generated bookmark
    tab->getView()->goToPage(4);
    fixture.settle();
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::addBookmark()));
    QTRY_VERIFY(panel->getModel()->index(3).data(pdf::PDFComponentRoles::TagRole).toString().isEmpty());
    QCOMPARE(panel->getModel()->rowCount(), 4);
    panel->removeBookmark(3);
    QTRY_COMPARE(panel->getModel()->index(3).data(pdf::PDFComponentRoles::TagRole).toString(), QStringLiteral("Auto"));
    QCOMPARE(panel->getModel()->index(0).data(pdf::PDFComponentRoles::TagRole).toString(), QStringLiteral("Auto"));
    QCOMPARE(panel->getModel()->index(0).data(pdf::PDFComponentRoles::SubtitleRole).toString(), QStringLiteral("Introduction"));
    QCOMPARE(panel->getModel()->index(2).data().toString(), QStringLiteral("Page 4"));
    QVERIFY(panel->getModel()->index(2).data(pdf::PDFComponentRoles::TagRole).toString().isEmpty());
    QCOMPARE(panel->getModel()->index(3).data(pdf::PDFComponentRoles::SubtitleRole).toString(), QStringLiteral("Appendix"));

    // A generated bookmark has only Go to Page
    menu = panel->createContextMenu(0);
    QCOMPARE(menu->actions().size(), 1);
    delete menu;
    panel->removeBookmark(0);
    panel->renameBookmark(0);
    panel->moveBookmark(0, 1);
    fixture.settle();
    QCOMPARE(panel->getModel()->rowCount(), 4);

    QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::bookmarksAutoGenerate(), false).isError());
    QTRY_COMPARE(panel->getModel()->rowCount(), 1);
}

void AppDocumentPanelsTest::bookmarksExportImport()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("simple.pdf"), 6));
    pdf::PDFAppDocumentView* view = tab->getView();
    for (const int page : { 1, 4 })
    {
        view->goToPage(page);
        fixture.settle();
        QVERIFY(tab->executeCommand(pdf::PDFAppCommands::addBookmark()));
    }

    const QString exported = fixture.path(QStringLiteral("bookmarks.json"));
    QString proposed;
    pdf::PDFAppPanelFileDialogs::setSaveHandler([&](QWidget*, const QString&, const QString& file, const QString&) { proposed = file; return exported; });
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::exportBookmarks()));
    QVERIFY(proposed.endsWith(QStringLiteral("simple-bookmarks.json")));
    QVERIFY(QFile::exists(exported));

    // Import into another document
    pdf::PDFAppDocumentTab* other = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("other.pdf"), 3));
    pdf::PDFAppPanelFileDialogs::setOpenHandler([&](QWidget*, const QString&, const QString&, const QString&) { return exported; });
    QVERIFY(other->executeCommand(pdf::PDFAppCommands::importBookmarks()));
    const std::vector<pdf::PDFAppBookmarkItem> items = pdf::PDFAppBookmarks::getItems(fixture.context.get(), other->getDocument());
    // Page 5 (index 4) is outside of the 3 pages
    QCOMPARE(items.size(), size_t(1));
    QCOMPARE(items[0].page, pdf::PDFInteger(1));

    // Hostile files: not JSON, too large, missing
    const QString broken = fixture.path(QStringLiteral("broken.json"));
    QFile brokenFile(broken);
    QVERIFY(brokenFile.open(QIODevice::WriteOnly));
    brokenFile.write("{ not json");
    brokenFile.close();
    pdf::PDFAppPanelFileDialogs::setOpenHandler([&](QWidget*, const QString&, const QString&, const QString&) { return broken; });
    other->executeCommand(pdf::PDFAppCommands::importBookmarks());
    QTRY_VERIFY(PanelsTestHelper::findMessageBox(QStringLiteral("bookmarkErrorBox")));
    fixture.closeMessageBoxes();

    const QString huge = fixture.path(QStringLiteral("huge.json"));
    QFile hugeFile(huge);
    QVERIFY(hugeFile.open(QIODevice::WriteOnly));
    hugeFile.write(QByteArray(pdf::PDFBookmarkRepository::MAXIMUM_IMPORT_SIZE + 10, ' '));
    hugeFile.close();
    pdf::PDFAppPanelFileDialogs::setOpenHandler([&](QWidget*, const QString&, const QString&, const QString&) { return huge; });
    other->executeCommand(pdf::PDFAppCommands::importBookmarks());
    QTRY_VERIFY(PanelsTestHelper::findMessageBox(QStringLiteral("bookmarkErrorBox")));
    fixture.closeMessageBoxes();

    pdf::PDFAppPanelFileDialogs::setOpenHandler([&](QWidget*, const QString&, const QString&, const QString&) { return fixture.path(QStringLiteral("missing.json")); });
    other->executeCommand(pdf::PDFAppCommands::importBookmarks());
    QTRY_VERIFY(PanelsTestHelper::findMessageBox(QStringLiteral("bookmarkErrorBox")));
    fixture.closeMessageBoxes();

    // A target, which cannot be written
    pdf::PDFAppPanelFileDialogs::setSaveHandler([&](QWidget*, const QString&, const QString&, const QString&) { return fixture.path(QStringLiteral("no/such/folder/x.json")); });
    tab->executeCommand(pdf::PDFAppCommands::exportBookmarks());
    QTRY_VERIFY(PanelsTestHelper::findMessageBox(QStringLiteral("bookmarkErrorBox")));
    fixture.closeMessageBoxes();

    // A document without bookmarks has nothing to export (no dialog)
    bool isAsked = false;
    pdf::PDFAppPanelFileDialogs::setSaveHandler([&](QWidget*, const QString&, const QString&, const QString&) { isAsked = true; return QString(); });
    pdf::PDFAppDocumentTab* empty = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("empty.pdf"), 1));
    empty->executeCommand(pdf::PDFAppCommands::exportBookmarks());
    QVERIFY(!isAsked);
}

/// Thread adding a bookmark by its own connection
class BookmarkWriterThread : public QThread
{
public:
    BookmarkWriterThread(pdf::PDFDatabase* database, QString file) : m_database(database), m_file(std::move(file)) { }

protected:
    virtual void run() override
    {
        pdf::PDFBookmarkRepository(m_database).addBookmark(m_file, 1, QStringLiteral("From another connection"));
    }

private:
    pdf::PDFDatabase* m_database;
    QString m_file;
};

void AppDocumentPanelsTest::bookmarksOtherConnection()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("simple.pdf"), 3));
    pdf::PDFAppBookmarksPanel* panel = PanelsTestHelper::panel<pdf::PDFAppBookmarksPanel>(tab, QStringLiteral("bookmarks"));
    QVERIFY(panel);

    // Another window (connection) adds a bookmark of this file: the panel follows
    BookmarkWriterThread thread(fixture.context->getDatabase(), tab->getDocument()->getFilePath());
    thread.start();
    QVERIFY(thread.wait(10000));
    QTRY_COMPARE(panel->getModel()->rowCount(), 1);
    QCOMPARE(panel->getModel()->index(0).data(pdf::PDFComponentRoles::SubtitleRole).toString(), QStringLiteral("From another connection"));

    // A bookmark of another file is not shown
    BookmarkWriterThread otherThread(fixture.context->getDatabase(), QDir(fixture.folder()).absoluteFilePath(QStringLiteral("unrelated.pdf")));
    otherThread.start();
    QVERIFY(otherThread.wait(10000));
    QTest::qWait(200);
    QCOMPARE(panel->getModel()->rowCount(), 1);
}

void AppDocumentPanelsTest::bookmarksRenameMoveRemove()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("simple.pdf"), 6));
    pdf::PDFAppBookmarksPanel* panel = PanelsTestHelper::panel<pdf::PDFAppBookmarksPanel>(tab, QStringLiteral("bookmarks"));
    for (const int page : { 1, 3, 5 })
    {
        tab->getView()->goToPage(page);
        fixture.settle();
        tab->executeCommand(pdf::PDFAppCommands::addBookmark());
    }
    QTRY_COMPARE(panel->getModel()->rowCount(), 3);

    // Context menu of a personal bookmark: Go to Page, Rename…, Move Up, Move Down, Remove
    QPointer<QMenu> menu = panel->createContextMenu(0);
    QVERIFY(PanelsTestHelper::findAction(menu, QStringLiteral("renameBookmark")));
    QVERIFY(!PanelsTestHelper::findAction(menu, QStringLiteral("moveBookmarkUp"))->isEnabled());
    QVERIFY(PanelsTestHelper::findAction(menu, QStringLiteral("moveBookmarkDown"))->isEnabled());
    QVERIFY(PanelsTestHelper::findAction(menu, QStringLiteral("removeBookmark"))->property("destructive").toBool());
    PanelsTestHelper::findAction(menu, QStringLiteral("moveBookmarkDown"))->trigger();
    delete menu;
    QTRY_COMPARE(panel->getModel()->getItem(0)->page, pdf::PDFInteger(3));
    QCOMPARE(panel->getModel()->getItem(1)->page, pdf::PDFInteger(1));

    menu = panel->createContextMenu(1);
    PanelsTestHelper::findAction(menu, QStringLiteral("moveBookmarkUp"))->trigger();
    delete menu;
    QTRY_COMPARE(panel->getModel()->getItem(0)->page, pdf::PDFInteger(1));

    // Rename (F2): the dialog writes the title
    panel->getListView()->setCurrentIndex(panel->getModel()->index(2));
    QTest::keyClick(panel->getListView(), Qt::Key_F2);
    fixture.settle();
    pdf::PDFAppRenameBookmarkDialog* dialog = qobject_cast<pdf::PDFAppRenameBookmarkDialog*>(PanelsTestHelper::findMessageBox(QStringLiteral("renameBookmarkDialog")));
    QVERIFY(dialog);
    dialog->getNameField()->setText(QStringLiteral("My page"));
    dialog->getActionButton()->click();
    QTRY_COMPARE(panel->getModel()->index(2).data(pdf::PDFComponentRoles::SubtitleRole).toString(), QStringLiteral("My page"));

    // Remove (Delete)
    panel->getListView()->setCurrentIndex(panel->getModel()->index(0));
    QTest::keyClick(panel->getListView(), Qt::Key_Delete);
    QTRY_COMPARE(panel->getModel()->rowCount(), 2);

    // The menu triggers after the panel has changed are safe
    menu = panel->createContextMenu(1);
    QAction* remove = PanelsTestHelper::findAction(menu, QStringLiteral("removeBookmark"));
    panel->reload();
    remove->trigger();
    delete menu;
    QTRY_COMPARE(panel->getModel()->rowCount(), 1);
}

void AppDocumentPanelsTest::bookmarksMenuAfterReload()
{
    // The context menu does not block: another instance adds a bookmark while it is
    // open, the rows are reloaded, the action works on the bookmark of the menu
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("menu.pdf"), 6));
    pdf::PDFAppBookmarksPanel* panel = PanelsTestHelper::panel<pdf::PDFAppBookmarksPanel>(tab, QStringLiteral("bookmarks"));
    QVERIFY(panel);
    for (const int page : { 1, 3, 5 })
    {
        tab->getView()->goToPage(page);
        fixture.settle();
        tab->executeCommand(pdf::PDFAppCommands::addBookmark());
    }
    QTRY_COMPARE(panel->getModel()->rowCount(), 3);

    const QString filePath = tab->getDocument()->getFilePath();
    pdf::PDFBookmarkRepository repository(fixture.context->getDatabase());

    QPointer<QMenu> menu = panel->createContextMenu(1);
    QVERIFY(menu);
    QVERIFY(!repository.addBookmark(filePath, 0, QStringLiteral("Other instance")).isError());
    panel->reload();
    QCOMPARE(panel->getModel()->rowCount(), 4);
    QCOMPARE(panel->getModel()->getItem(1)->page, pdf::PDFInteger(1));
    PanelsTestHelper::findAction(menu, QStringLiteral("removeBookmark"))->trigger();
    delete menu;
    QTRY_COMPARE(panel->getModel()->rowCount(), 3);
    std::vector<pdf::PDFInteger> pages;
    for (const pdf::PDFAppBookmarkItem& item : panel->getModel()->getItems())
    {
        pages.push_back(item.page);
    }
    QCOMPARE(pages, std::vector<pdf::PDFInteger>({ 0, 1, 5 }));

    // Go to Page of the menu goes to the page of its bookmark
    menu = panel->createContextMenu(2);
    QVERIFY(!repository.addBookmark(filePath, 2, QStringLiteral("Another")).isError());
    panel->reload();
    tab->getView()->goToPage(0);
    fixture.settle();
    QCOMPARE(tab->getView()->getCurrentPageIndex(), pdf::PDFInteger(0));
    PanelsTestHelper::findAction(menu, QStringLiteral("goToBookmark"))->trigger();
    delete menu;
    QTRY_COMPARE(tab->getView()->getCurrentPageIndex(), pdf::PDFInteger(5));

    // The bookmark of the menu removed elsewhere: the action does nothing
    menu = panel->createContextMenu(0);
    const qint64 removedId = panel->getModel()->getItem(0)->id;
    QVERIFY(!repository.removeBookmark(removedId).isError());
    panel->reload();
    const int rows = panel->getModel()->rowCount();
    PanelsTestHelper::findAction(menu, QStringLiteral("removeBookmark"))->trigger();
    delete menu;
    fixture.settle();
    QCOMPARE(panel->getModel()->rowCount(), rows);
}

void AppDocumentPanelsTest::bookmarksEncryptedTitle()
{
    // O18: nothing generated from the content of an encrypted document is written
    // to the database - its bookmark has no title of its outline; the bookmarks
    // generated from the outline are never stored; a title of the user is stored
    pdftest::AppDocumentFixture fixture;
    QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::bookmarksAutoGenerate(), true).isError());

    // The control: a plain document gets the title of its outline
    pdf::PDFAppDocumentTab* plainTab = fixture.open(PanelsTestHelper::sharedPageOutline(fixture.folder(), false));
    QVERIFY(plainTab);
    plainTab->getView()->goToPage(2);
    fixture.settle();
    QVERIFY(plainTab->executeCommand(pdf::PDFAppCommands::addBookmark()));
    const std::vector<pdf::PDFBookmark> plainRows = pdf::PDFBookmarkRepository(fixture.context->getDatabase()).getBookmarks(plainTab->getDocument()->getFilePath());
    QCOMPARE(plainRows.size(), size_t(1));
    QCOMPARE(plainRows.front().title, QStringLiteral("Chapter of page 3"));

    const QString path = PanelsTestHelper::sharedPageOutline(fixture.folder(), true);
    pdf::PDFAppDocumentTab* tab = fixture.open(path);
    QVERIFY(tab);
    QCOMPARE(tab->getDocument()->getState(), pdf::PDFAppDocumentState::PasswordRequired);
    QVERIFY(tab->getLoader()->setPassword(pdftest::AppDocumentSamples::getUserPassword()));
    QTRY_VERIFY(tab->getDocument()->isReady());
    QVERIFY(tab->getDocument()->isPasswordProtected());

    // The generated bookmarks are shown, but not stored
    QVERIFY(!pdf::PDFAppBookmarks::getItems(fixture.context.get(), tab->getDocument()).empty());
    QVERIFY(pdf::PDFBookmarkRepository(fixture.context->getDatabase()).getBookmarks(tab->getDocument()->getFilePath()).empty());

    tab->getView()->goToPage(2);
    fixture.settle();
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::addBookmark()));
    std::vector<pdf::PDFBookmark> rows = pdf::PDFBookmarkRepository(fixture.context->getDatabase()).getBookmarks(tab->getDocument()->getFilePath());
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows.front().page, qint64(2));
    QVERIFY(rows.front().title.isEmpty());

    // The whole database has no title of the encrypted outline
    pdf::PDFDatabaseStatement statement(fixture.context->getDatabase()->getConnection(), "SELECT COUNT(*) FROM bookmark WHERE title LIKE '%of page%'");
    QVERIFY(statement.next());
    QCOMPARE(statement.getInt64(0), qint64(1));

    // A title the user types is stored
    QVERIFY(!pdf::PDFBookmarkRepository(fixture.context->getDatabase()).renameBookmark(rows.front().id, QStringLiteral("My own title")).isError());
    rows = pdf::PDFBookmarkRepository(fixture.context->getDatabase()).getBookmarks(tab->getDocument()->getFilePath());
    QCOMPARE(rows.front().title, QStringLiteral("My own title"));
}

void AppDocumentPanelsTest::notes()
{
    pdftest::AppDocumentFixture fixture;
    const QString path = PanelsTestHelper::notes(fixture.folder());
    QVERIFY(!path.isEmpty());
    pdf::PDFAppDocumentTab* tab = fixture.open(path);
    QVERIFY(PanelsTestHelper::sidebar(tab)->isTabVisible(QStringLiteral("notes")));
    pdf::PDFAppNotesPanel* notes = PanelsTestHelper::panel<pdf::PDFAppNotesPanel>(tab, QStringLiteral("notes"));
    QVERIFY(notes);

    // 195:3910: captions of the pages and the notes of each page
    pdf::PDFAppNotesModel* model = notes->getModel();
    QCOMPARE(model->getNoteCount(), 4);
    QCOMPARE(notes->getSubtitle(), QStringLiteral("4 notes on 2 pages"));
    QCOMPARE(model->rowCount(), 6);
    QVERIFY(model->index(0).data(pdf::PDFAppNotesModel::CaptionRole).toBool());
    QCOMPARE(model->index(0).data().toString(), QStringLiteral("Page 1"));
    QCOMPARE(model->index(1).data().toString(), QStringLiteral("Jakub Melka"));
    QCOMPARE(model->index(1).data(pdf::PDFComponentRoles::TextRole).toString(), QStringLiteral("Add the source of this statistic."));
    QCOMPARE(model->index(1).data(pdf::PDFComponentRoles::IconNameRole).toString(), QStringLiteral("message-square"));
    QCOMPARE(model->index(2).data(pdf::PDFComponentRoles::TextRole).toString(), QStringLiteral("Check these numbers against the report."));
    QCOMPARE(model->index(3).data().toString(), QStringLiteral("Page 3"));
    QCOMPARE(model->index(5).data(pdf::PDFComponentRoles::IconNameRole).toString(), QStringLiteral("highlighter"));
    QCOMPARE(model->index(5).data(pdf::PDFComponentRoles::HueRole).toInt(), int(pdf::PDFAccentHue::Amber));
    QVERIFY(!(model->flags(model->index(0)) & Qt::ItemIsSelectable));
    QVERIFY(model->flags(model->index(1)) & Qt::ItemIsSelectable);

    // A click goes to the page of the note; a caption does nothing
    notes->activateRow(4);
    QTRY_COMPARE(tab->getView()->getCurrentPageIndex(), pdf::PDFInteger(2));
    notes->activateRow(0);
    notes->activateRow(-1);
    notes->activateRow(100);

    // Mouse and keyboard
    QListView* list = notes->getListView();
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, list->visualRect(model->index(1)).center());
    QTRY_COMPARE(tab->getView()->getCurrentPageIndex(), pdf::PDFInteger(0));
    list->setCurrentIndex(model->index(5));
    QTest::keyClick(list, Qt::Key_Enter);
    QTRY_COMPARE(tab->getView()->getCurrentPageIndex(), pdf::PDFInteger(2));

    // The caption row has its own height
    QStyleOptionViewItem option;
    option.rect = QRect(0, 0, 276, 30);
    pdf::PDFAppNotesDelegate delegate(nullptr);
    QVERIFY(delegate.sizeHint(option, model->index(0)).height() < delegate.sizeHint(option, model->index(1)).height());

    // Badges of every type
    for (int type = int(pdf::AnnotationType::Invalid); type <= int(pdf::AnnotationType::Unknown); ++type)
    {
        QString icon;
        pdf::PDFAccentHue hue = pdf::PDFAccentHue::Neutral;
        pdf::PDFAppNotes::getBadge(pdf::AnnotationType(type), false, &icon, &hue);
        QVERIFY(!icon.isEmpty());
    }
    QString icon;
    pdf::PDFAccentHue hue = pdf::PDFAccentHue::Blue;
    pdf::PDFAppNotes::getBadge(pdf::AnnotationType::Text, true, &icon, &hue);
    QCOMPARE(icon, QStringLiteral("corner-down-right"));
    QCOMPARE(hue, pdf::PDFAccentHue::Neutral);
}

void AppDocumentPanelsTest::notesFilterAndHostile()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(PanelsTestHelper::notes(fixture.folder()));
    pdf::PDFAppNotesPanel* notes = PanelsTestHelper::panel<pdf::PDFAppNotesPanel>(tab, QStringLiteral("notes"));
    QVERIFY(notes);
    pdf::PDFAppNotesModel* model = notes->getModel();

    notes->getSearchField()->setText(QStringLiteral("jane"));
    QCOMPARE(model->rowCount(), 4);
    notes->getSearchField()->setText(QStringLiteral("CHART"));
    QCOMPARE(model->rowCount(), 2);
    notes->getSearchField()->setText(QStringLiteral("nothing at all"));
    QCOMPARE(model->rowCount(), 0);
    notes->getSearchField()->setText(QString(5000, QChar(u'x')));
    notes->getSearchField()->clear();
    QCOMPARE(model->rowCount(), 6);

    // Dates
    const QDate today(2026, 10, 22);
    QVERIFY(pdf::PDFAppNotes::formatDate(QDateTime(), today).isEmpty());
    QVERIFY(!pdf::PDFAppNotes::formatDate(QDateTime(QDate(2026, 9, 28), QTime(10, 0)), today).contains(QStringLiteral("2026")));
    QVERIFY(pdf::PDFAppNotes::formatDate(QDateTime(QDate(2024, 9, 28), QTime(10, 0)), today).contains(QStringLiteral("2024")));
    {
        pdftest::AppSystemLocaleGuard guard(pdftest::AppSystemLocaleGuard::czech());
        QCOMPARE(pdf::PDFAppNotes::formatDate(QDateTime(QDate(2026, 9, 28), QTime(10, 0)), today), QStringLiteral("Sep 28"));
        QCOMPARE(pdf::PDFAppNotes::formatDate(QDateTime(QDate(2024, 9, 28), QTime(10, 0)), today), QStringLiteral("Sep 28, 2024"));
    }
    QVERIFY(pdf::PDFAppNotes::collect(nullptr).empty());
    QVERIFY(!pdf::PDFAppNotes::hasNotes(nullptr));
    QVERIFY(!model->getNote(0));
    QVERIFY(model->getNote(1));
    QVERIFY(!model->data(model->index(100), Qt::DisplayRole).isValid());

    // A limit of the notes
    QCOMPARE(pdf::PDFAppNotes::collect(tab->getDocument()->getDocument().data(), 2).size(), size_t(2));
}

void AppDocumentPanelsTest::deletionFromOwnSignals_data()
{
    QTest::addColumn<QString>("signal");
    QTest::addColumn<bool>("deleteWindow");
    // A thumbnail clicked by the mouse is not a row: the view moves from the slot of
    // the selection model inside of QAbstractItemView::mousePressEvent, which Qt
    // itself does not survive, when a slot deletes the view there
    for (const char* name : { "panelChanged", "thumbnailsScrolled", "rotateFromMenu", "toolFromMenu" })
    {
        QTest::newRow(QStringLiteral("%1-tab").arg(QLatin1String(name)).toLatin1().constData()) << QString::fromLatin1(name) << false;
        QTest::newRow(QStringLiteral("%1-window").arg(QLatin1String(name)).toLatin1().constData()) << QString::fromLatin1(name) << true;
    }
}

void AppDocumentPanelsTest::deletionFromOwnSignals()
{
    // The application deletes the tab (or the window) synchronously from a signal
    // of the sidebar, of the view (a click on a thumbnail moves the view) or of the
    // document (Rotate of the context menu of the thumbnails), from the navigation
    // of a page tool of the menu, and while the thumbnails of a scrolled list are
    // being compiled (valgrind finds the reads)
    QFETCH(QString, signal);
    QFETCH(bool, deleteWindow);

    pdftest::AppDocumentFixture fixture;
    QPointer<pdf::PDFAppMainWindow> window = fixture.window;
    const bool isLarge = signal == QStringLiteral("thumbnailsScrolled");
    // 400 pages are enough for a list scrolled far from its start; 2000 pages took
    // almost the whole time limit of QtTest under valgrind (the sample and its opening)
    QPointer<pdf::PDFAppDocumentTab> tab = fixture.open(isLarge ? pdftest::AppDocumentSamples::large(fixture.folder(), 400) : pdftest::AppDocumentSamples::outline(fixture.folder()));
    QVERIFY(tab);
    QObject receiver;
    bool isDeleted = false;
    auto deleteNow = [&isDeleted, &tab, &window, deleteWindow]()
    {
        if (isDeleted)
        {
            return;
        }
        isDeleted = true;
        if (deleteWindow)
        {
            delete window.data();
        }
        else
        {
            delete tab.data();
        }
    };

    pdf::PDFAppDocumentSidebar* bar = PanelsTestHelper::sidebar(tab);
    QVERIFY(bar);
    pdf::PDFAppThumbnailsPanel* thumbnails = PanelsTestHelper::panel<pdf::PDFAppThumbnailsPanel>(tab, QStringLiteral("thumbnails"));
    QVERIFY(thumbnails);
    fixture.settle();

    if (signal == QStringLiteral("panelChanged"))
    {
        connect(bar, &pdf::PDFAppDocumentSidebar::currentPanelChanged, &receiver, deleteNow);
        bar->showPanel(QStringLiteral("outline"));
    }
    else if (signal == QStringLiteral("thumbnailsScrolled"))
    {
        // Pages of the middle of the document are compiled for the visible rows
        pdf::PDFAppThumbnailListView* list = thumbnails->getListView();
        list->scrollTo(thumbnails->getModel()->index(200, 0));
        QTest::qWait(20);
        list->scrollTo(thumbnails->getModel()->index(300, 0));
        deleteNow();
    }
    else if (signal == QStringLiteral("rotateFromMenu") || signal == QStringLiteral("toolFromMenu"))
    {
        thumbnails->selectPages({ 0, 1 });
        QMenu* menu = thumbnails->createContextMenu();
        QVERIFY(menu);
        QAction* found = nullptr;
        for (QAction* action : menu->actions())
        {
            const QString text = action->text();
            if ((signal == QStringLiteral("rotateFromMenu") && text.contains(QStringLiteral("Rotate"))) ||
                (signal == QStringLiteral("toolFromMenu") && text.contains(QStringLiteral("Extract"))))
            {
                found = action;
                break;
            }
        }
        QVERIFY(found);
        if (signal == QStringLiteral("rotateFromMenu"))
        {
            connect(tab->getDocument(), &pdf::PDFAppDocument::documentChanged, &receiver, deleteNow);
        }
        else
        {
            connect(fixture.window->getNavigator(), &pdf::PDFAppNavigator::navigated, &receiver, deleteNow);
        }
        found->trigger();
    }

    QTRY_VERIFY_WITH_TIMEOUT(isDeleted, 30000);
    fixture.settle();
    // The compilations of the deleted tab end (valgrind runs the threads one after
    // another, so they may take long): nothing of them may be left running
    QVERIFY(QThreadPool::globalInstance()->waitForDone(600000));
    QTest::qWait(50);
    fixture.settle();
    QVERIFY(deleteWindow ? !window : !tab);
    if (!deleteWindow)
    {
        QVERIFY(window);
        QVERIFY(fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("After.pdf"), 1)));
    }
}

void AppDocumentPanelsTest::deletionMatrix()
{
    // Each panel shown, then the view, the sidebar or the tab / window deleted
    // (regression: a destroyed panel notified the half-destroyed sidebar)
    const QStringList ids = { QStringLiteral("thumbnails"), QStringLiteral("outline"), QStringLiteral("layers"), QStringLiteral("attachments"),
                              QStringLiteral("bookmarks"), QStringLiteral("notes") };
    for (const QString& id : ids)
    {
        for (int order = 0; order < 4; ++order)
        {
            pdftest::AppDocumentFixture fixture;
            const QString path = id == QStringLiteral("outline") ? pdftest::AppDocumentSamples::outline(fixture.folder())
                                                                 : (id == QStringLiteral("layers") ? pdftest::AppDocumentSamples::layers(fixture.folder())
                                                                                                   : pdftest::AppDocumentSamples::attachments(fixture.folder()));
            if (order < 2)
            {
                QPointer<pdf::PDFAppDocumentView> view = new pdf::PDFAppDocumentView(fixture.context.get());
                QPointer<pdf::PDFAppDocument> document = new pdf::PDFAppDocument(fixture.context.get(), nullptr);
                QVERIFY(PanelsTestHelper::load(document, path));
                view->setDocument(document);
                view->show();
                QVERIFY2(view->getSidebar()->showPanel(id), qPrintable(id));
                fixture.settle();
                if (order == 0)
                {
                    delete view.data();
                    delete document.data();
                }
                else
                {
                    delete view->getSidebar();
                    fixture.settle();
                    delete document.data();
                    delete view.data();
                }
            }
            else
            {
                pdf::PDFAppDocumentTab* tab = fixture.open(path);
                QVERIFY(tab);
                QVERIFY2(PanelsTestHelper::sidebar(tab)->showPanel(id), qPrintable(id));
                fixture.settle();
                if (order == 2)
                {
                    fixture.window->closeTab(tab);
                }
                else
                {
                    delete fixture.window.data();
                }
            }
            fixture.settle();
        }
    }

    // The window deleted with a panel menu, a rename dialog and a running write
    {
        pdftest::AppDocumentFixture fixture;
        pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::attachments(fixture.folder()));
        pdf::PDFAppBookmarksPanel* bookmarks = PanelsTestHelper::panel<pdf::PDFAppBookmarksPanel>(tab, QStringLiteral("bookmarks"));
        tab->executeCommand(pdf::PDFAppCommands::addBookmark());
        QTRY_COMPARE(bookmarks->getModel()->rowCount(), 1);
        QMenu* menu = bookmarks->createContextMenu(0);
        menu->popup(QPoint(100, 100));
        bookmarks->renameBookmark(0);
        pdf::PDFAppAttachmentsPanel* attachments = PanelsTestHelper::panel<pdf::PDFAppAttachmentsPanel>(tab, QStringLiteral("attachments"));
        pdf::PDFAppPanelFileDialogs::setSaveHandler([&](QWidget*, const QString&, const QString&, const QString&) { return fixture.path(QStringLiteral("x.txt")); });
        attachments->saveAttachment(0);
        pdf::PDFAppThumbnailsPanel* thumbnails = PanelsTestHelper::panel<pdf::PDFAppThumbnailsPanel>(tab, QStringLiteral("thumbnails"));
        thumbnails->createContextMenu()->popup(QPoint(100, 100));
        delete fixture.window.data();
        fixture.settle();
    }

    // Panels deleted by the application one by one, the sidebar survives
    {
        pdftest::AppDocumentFixture fixture;
        pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::attachments(fixture.folder()));
        pdf::PDFAppDocumentSidebar* sidebar = PanelsTestHelper::sidebar(tab);
        for (const QString& id : { QStringLiteral("thumbnails"), QStringLiteral("attachments"), QStringLiteral("bookmarks"), QStringLiteral("notes") })
        {
            QVERIFY(sidebar->showPanel(id));
            fixture.settle();
            delete sidebar->findPanel(id);
            fixture.settle();
            QVERIFY(!sidebar->findPanel(id));
            QVERIFY(sidebar->showPanel(id));
            fixture.settle();
        }

        // The document changed under every panel (rotate = Reset), the undo
        tab->executeCommand(pdf::PDFAppCommands::rotatePagesRight());
        fixture.settle();
        tab->executeCommand(pdf::PDFAppCommands::undo());
        fixture.settle();

        // The tab closed with every panel created
        fixture.window->closeTab(tab);
        fixture.settle();
    }

    // The document deleted under the panels of a standalone view
    {
        pdftest::AppDocumentFixture fixture;
        pdf::PDFAppDocumentView view(fixture.context.get());
        pdf::PDFAppDocument* document = new pdf::PDFAppDocument(fixture.context.get(), nullptr);
        QVERIFY(PanelsTestHelper::load(document, pdftest::AppDocumentSamples::attachments(fixture.folder())));
        view.setDocument(document);
        view.show();
        for (const QString& id : { QStringLiteral("thumbnails"), QStringLiteral("attachments"), QStringLiteral("bookmarks"), QStringLiteral("notes"), QStringLiteral("outline"),
                                   QStringLiteral("layers") })
        {
            view.getSidebar()->showPanel(id);
            fixture.settle();
        }
        delete document;
        fixture.settle();
        for (const QString& id : { QStringLiteral("thumbnails"), QStringLiteral("attachments"), QStringLiteral("bookmarks"), QStringLiteral("notes") })
        {
            QVERIFY(view.getSidebar()->findPanel(id));
        }
        QCOMPARE(qobject_cast<pdf::PDFAppThumbnailsPanel*>(view.getSidebar()->findPanel(QStringLiteral("thumbnails")))->getModel()->rowCount(), 0);
    }

    // Layers and outline with the document deleted
    {
        pdftest::AppDocumentFixture fixture;
        pdf::PDFAppDocumentView view(fixture.context.get());
        pdf::PDFAppDocument* document = new pdf::PDFAppDocument(fixture.context.get(), nullptr);
        QVERIFY(PanelsTestHelper::load(document, pdftest::AppDocumentSamples::layers(fixture.folder())));
        view.setDocument(document);
        QVERIFY(view.getSidebar()->showPanel(QStringLiteral("layers")));
        pdf::PDFAppLayersPanel* layers = qobject_cast<pdf::PDFAppLayersPanel*>(view.getSidebar()->findPanel(QStringLiteral("layers")));
        QCOMPARE(layers->getLayerCount(), 2);
        delete document;
        fixture.settle();
        QCOMPARE(layers->getLayerCount(), 0);
        QCOMPARE(layers->getModel()->rowCount(QModelIndex()), 0);
    }
}

void AppDocumentPanelsTest::screenshots()
{
    const QString folder = qEnvironmentVariable("PDF4QT_APP_SCREENSHOTS");
    if (folder.isEmpty())
    {
        QSKIP("PDF4QT_APP_SCREENSHOTS is not set");
    }
    QDir().mkpath(folder);

    struct Shot
    {
        QString name;
        QSize size;
        Qt::LayoutDirection direction;
    };
    const std::vector<Shot> shots = { { QStringLiteral(""), QSize(1440, 960), Qt::LeftToRight }, { QStringLiteral("-720"), QSize(720, 480), Qt::LeftToRight },
                                      { QStringLiteral("-rtl"), QSize(1440, 960), Qt::RightToLeft } };
    for (const Shot& shot : shots)
    {
        for (const pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
        {
            pdftest::AppDocumentFixture fixture(shot.size);
            QApplication::setLayoutDirection(shot.direction);
            pdf::PDFThemeManager::getInstance()->setScheme(scheme);
            const QString notesPath = PanelsTestHelper::notes(fixture.folder());
            const QString attachmentsPath = PanelsTestHelper::attachments(fixture.folder(), QStringLiteral("files.pdf"),
                                                                          { QStringLiteral("budget-2026.xlsx"), QStringLiteral("survey-raw-data.csv"), QStringLiteral("site-photo.jpg") });
            const QString theme = scheme == pdf::PDFThemeColors::Scheme::Dark ? QStringLiteral("dark") : QStringLiteral("light");

            pdf::PDFAppDocumentTab* outlineTab = fixture.open(pdftest::AppDocumentSamples::outline(fixture.folder()));
            outlineTab->getView()->goToPage(2);
            outlineTab->executeCommand(pdf::PDFAppCommands::addBookmark());
            outlineTab->getView()->goToPage(4);
            outlineTab->executeCommand(pdf::PDFAppCommands::addBookmark());
            fixture.context->getSettings().setValue(pdf::PDFSettingKeys::bookmarksAutoGenerate(), true);
            fixture.settle();

            const std::vector<std::pair<pdf::PDFAppDocumentTab*, QString>> panels =
            {
                { outlineTab, QStringLiteral("thumbnails") }, { outlineTab, QStringLiteral("outline") }, { outlineTab, QStringLiteral("bookmarks") },
                { fixture.open(pdftest::AppDocumentSamples::layers(fixture.folder())), QStringLiteral("layers") },
                { fixture.open(attachmentsPath), QStringLiteral("attachments") },
                { fixture.open(notesPath), QStringLiteral("notes") }
            };
            for (const auto& item : panels)
            {
                fixture.window->getNavigator()->navigate(pdf::PDFAppRoutes::document(item.first->getDocumentId()));
                fixture.settle();
                PanelsTestHelper::sidebar(item.first)->showPanel(item.second);
                fixture.settle();
                QTest::qWait(500);
                fixture.settle();
                for (const qreal ratio : { 1.0, 2.0 })
                {
                    pdf::PDFAppDocumentSidebar* sidebar = PanelsTestHelper::sidebar(item.first);
                    QPixmap pixmap(sidebar->size() * ratio);
                    pixmap.setDevicePixelRatio(ratio);
                    sidebar->render(&pixmap);
                    pixmap.save(QDir(folder).filePath(QStringLiteral("sidebar-%1-%2%3@%4x.png").arg(item.second, theme, shot.name).arg(int(ratio))));
                }
                fixture.window->grab().save(QDir(folder).filePath(QStringLiteral("window-%1-%2%3.png").arg(item.second, theme, shot.name)));
            }

            // The context menu of the thumbnails (266:12502)
            pdf::PDFAppThumbnailsPanel* thumbnails = PanelsTestHelper::panel<pdf::PDFAppThumbnailsPanel>(outlineTab, QStringLiteral("thumbnails"));
            fixture.window->getNavigator()->navigate(pdf::PDFAppRoutes::document(outlineTab->getDocumentId()));
            fixture.settle();
            thumbnails->selectPages({ 1, 2, 3 });
            QMenu* menu = thumbnails->createContextMenu();
            menu->popup(thumbnails->mapToGlobal(QPoint(100, 200)));
            fixture.settle();
            menu->grab().save(QDir(folder).filePath(QStringLiteral("thumbnails-menu-%1%2.png").arg(theme, shot.name)));
            fixture.window->grab().save(QDir(folder).filePath(QStringLiteral("window-thumbnails-selection-%1%2.png").arg(theme, shot.name)));
            menu->close();
            fixture.settle();
        }
    }
    QApplication::setLayoutDirection(Qt::LeftToRight);
}

QTEST_MAIN(AppDocumentPanelsTest)
#include "tst_appdocumentpanelstest.moc"
