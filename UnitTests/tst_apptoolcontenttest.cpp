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

// Edit Content, stage E1, package P14: the GUI of the tool, the shell, the keys, the notices and Undo
// (zadani_gui.md 4.14, 4.20, 4.21, 4.27; zadani.md KE.2, KE.3, KE.16; zadani_overeni.md 8, the row of
// UnitTestsAppToolContent). The expected texts are the texts of the frames edit-content-v2* (Figma) and of the notes
// of the skeleton (8.3); the expected values come from hand-written documents. A row, which needs the real part of
// another package of stage E1 (P11 canvas, P12 tabs and menus, P13 cards), is written whole and skipped while that
// part is a stub (QSKIP "needs P1x").

#include "appdocumentfixture.h"
#include "apptoolshellfixture.h"
#include "contenttestdocuments.h"

#include "pdfappcommand.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumenttab.h"
#include "pdfappeditcontentcards.h"
#include "pdfappeditcontentcontext.h"
#include "pdfappeditcontentkeyboard.h"
#include "pdfappeditcontentnotices.h"
#include "pdfappeditcontentregistry.h"
#include "pdfappeditcontentselection.h"
#include "pdfappeditcontenttools.h"
#include "pdfappeditcontentundo.h"
#include "pdfappeditcontentview.h"
#include "pdfappmainwindow.h"
#include "pdfapptoolgui_content.h"
#include "pdfapptoolhost.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptoolresultdialog.h"
#include "pdfapptooltoolbar.h"
#include "pdfdeferreddeletion.h"
#include "pdfdocumenttoolshell.h"
#include "pdfmessagebox.h"
#include "pdfsettingdefinitions.h"
#include "pdfthemecolors.h"
#include "pdfthememanager.h"

#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdfcontentjson.h"
#include "pdfcontentundostack.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumenttool_content.h"

#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStackedWidget>
#include <QToolButton>
#include <QTranslator>
#include <QtTest>

#include <memory>

using Context = pdf::PDFAppEditContentContext;
using GUI = pdf::PDFAppToolGUIContent;
using Registry = pdf::PDFAppEditContentRegistry;
using Notice = pdf::PDFAppEditContentNoticeRequest;
using Notices = pdf::PDFAppEditContentNotices;
using NoticeIds = pdf::PDFAppEditContentNoticeIds;
using State = pdf::PDFAppToolCommandState;

/// A translator, which is never empty: its installation and its removal send the change of the language
class ContentToolTranslator : public QTranslator
{
public:
    virtual bool isEmpty() const override { return false; }
};

/// The tool opened on a document of a test
class ContentToolOpened
{
public:
    pdf::PDFAppDocumentTab* tab = nullptr;
    QPointer<pdf::PDFDocumentToolShell> shell;
    QPointer<GUI> gui;
    QPointer<Context> context;

    bool isValid() const { return tab && shell && gui && context; }
};

/// A tool entry of a test registry (the toolbar, the menu Shape)
class ContentToolTestToolEntry : public pdf::PDFAppEditContentToolEntry
{
public:
    ContentToolTestToolEntry(QString id, Place place, int order, QString title, QKeySequence key) :
        m_id(std::move(id)), m_place(place), m_order(order), m_title(std::move(title)), m_key(std::move(key))
    {
    }

    virtual QString getId() const override { return m_id; }
    virtual Place getPlace() const override { return m_place; }
    virtual int getOrder() const override { return m_order; }
    virtual QString getTitle() const override { return m_title; }
    virtual QString getDescription() const override { return QStringLiteral("Description of ") + m_title; }
    virtual QString getIconName() const override { return QStringLiteral("shapes"); }
    virtual QKeySequence getKey() const override { return m_key; }
    virtual std::unique_ptr<pdf::PDFAppEditContentTool> createTool(pdf::PDFAppEditContentContext* context) const override { return std::make_unique<pdf::PDFAppEditContentTool>(context); }

private:
    QString m_id;
    Place m_place;
    int m_order;
    QString m_title;
    QKeySequence m_key;
};

/// Helpers of the test
class ContentToolHelper
{
public:
    ContentToolHelper() = delete;

    static constexpr const char* TOOL_ID = "edit.content";

    static int timeout(int milliseconds)
    {
        return milliseconds * pdftest::AppDocumentFixture::getTimeoutFactor();
    }

    /// \p pageCount pages [0 0 200 200], each with two filled rectangles (two Path objects); \p userUnit of the
    /// pages after the first one when it is not 1
    static pdf::PDFDocument createDocument(int pageCount, pdf::PDFReal userUnit = 1.0)
    {
        using D = pdftest::ContentTestDocuments;
        pdf::PDFDocumentBuilder builder;
        for (int i = 0; i < pageCount; ++i)
        {
            const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 200, 200));
            const pdf::PDFObject contents = D::addStreamObject(&builder, D::DictionaryEntries(), QByteArrayLiteral("10 10 50 50 re f 100 100 20 20 re f"));
            D::DictionaryEntries entries = { { "Resources", D::createDictionaryObject({ }) }, { "Contents", contents } };
            if (i >= 1 && userUnit != 1.0)
            {
                entries.push_back({ "UserUnit", pdf::PDFObject::createReal(userUnit) });
            }
            builder.mergeTo(page, D::createDictionaryObject(entries));
        }
        return builder.build();
    }

    /// One page with one image XObject drawn over the page (a scan)
    static pdf::PDFDocument createScannedDocument()
    {
        using D = pdftest::ContentTestDocuments;
        return D::createDocument(QByteArrayLiteral("q 200 0 0 200 0 0 cm /Im1 Do Q"), [](pdf::PDFDocumentBuilder* builder)
        {
            const pdf::PDFObject image = D::addStreamObject(builder, { { "Type", pdf::PDFObject::createName("XObject") }, { "Subtype", pdf::PDFObject::createName("Image") },
                                                                       { "Width", pdf::PDFObject::createInteger(1) }, { "Height", pdf::PDFObject::createInteger(1) },
                                                                       { "ColorSpace", pdf::PDFObject::createName("DeviceGray") }, { "BitsPerComponent", pdf::PDFObject::createInteger(8) } },
                                                            QByteArray(1, char(0x7F)));
            pdf::PDFDictionaryBuilder resources;
            resources.addEntry(pdf::PDFInplaceOrMemoryString("XObject"), D::createDictionaryObject({ { "Im1", image } }));
            return resources;
        });
    }

    /// One page with \p count filled rectangles of 1 x 1
    static pdf::PDFDocument createManyObjectsDocument(int count)
    {
        QByteArray content;
        content.reserve(count * 14);
        for (int i = 0; i < count; ++i)
        {
            content += QByteArrayLiteral("0 0 1 1 re f\n");
        }
        return pdftest::ContentTestDocuments::createDocument(content, pdftest::ContentTestDocuments::createNoResources);
    }

    /// One page, whose /Contents has a stream, which cannot be decoded (a page only for reading)
    static pdf::PDFDocument createBrokenDocument()
    {
        using D = pdftest::ContentTestDocuments;
        return D::createBrokenContentsDocument(QByteArrayLiteral("10 10 50 50 re f"), QByteArrayLiteral("100 100 20 20 re f"), D::createNoResources);
    }

    /// The document of createDocument with the XMP metadata, which declare \p declarations ("pdfaid", "pdfuaid")
    static pdf::PDFDocument createConformanceDocument(bool isPdfA, bool isPdfUA)
    {
        using D = pdftest::ContentTestDocuments;
        pdf::PDFDocument document = createDocument(1);
        pdf::PDFDocumentBuilder builder(&document);
        QByteArray xmp = QByteArrayLiteral("<?xpacket begin=\"\xEF\xBB\xBF\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>"
                                           "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">");
        if (isPdfA)
        {
            xmp += QByteArrayLiteral("<rdf:Description rdf:about=\"\" xmlns:pdfaid=\"http://www.aiim.org/pdfa/ns/id/\"><pdfaid:part>2</pdfaid:part><pdfaid:conformance>B</pdfaid:conformance></rdf:Description>");
        }
        if (isPdfUA)
        {
            xmp += QByteArrayLiteral("<rdf:Description rdf:about=\"\" xmlns:pdfuaid=\"http://www.aiim.org/pdfua/ns/id/\"><pdfuaid:part>1</pdfuaid:part></rdf:Description>");
        }
        xmp += QByteArrayLiteral("</rdf:RDF></x:xmpmeta><?xpacket end=\"w\"?>");
        const pdf::PDFObject metadata = D::addStreamObject(&builder, { { "Type", pdf::PDFObject::createName("Metadata") }, { "Subtype", pdf::PDFObject::createName("XML") } }, xmp);
        builder.mergeTo(builder.getCatalogReference(), D::createDictionaryObject({ { "Metadata", metadata } }));
        return builder.build();
    }

    static bool setDeveloperMode(pdf::PDFAppContext* context, bool isOn)
    {
        if (context->getSettings().setValue(pdf::PDFSettingKeys::advancedDeveloperMode(), isOn).isError())
        {
            return false;
        }
        return pdftest::AppToolShellFixture::waitFor([isOn]() { return GUI::isContentToolRegistered() == isOn; });
    }

    /// Writes the document into the folder of the fixture, opens it and the tool, waits for the container of the
    /// shown page
    static ContentToolOpened open(pdftest::AppToolShellFixture& fixture, pdf::PDFDocument document, const QString& name)
    {
        ContentToolOpened opened;
        const QString path = QDir(fixture.folder()).absoluteFilePath(name);
        if (!setDeveloperMode(fixture.context.get(), true) || !pdftest::AppDocumentSamples::write(document, path))
        {
            return opened;
        }

        opened.tab = fixture.open(path);
        if (!opened.tab || !pdftest::AppToolShellFixture::waitFor([&opened]() { return opened.tab->getDocument() && opened.tab->getDocument()->isReady(); }))
        {
            return opened;
        }

        opened.shell = fixture.openTool(opened.tab, QString::fromLatin1(TOOL_ID));
        opened.gui = opened.shell ? qobject_cast<GUI*>(opened.shell->getGUI()) : nullptr;
        opened.context = opened.gui ? opened.gui->getEditContext() : nullptr;
        if (opened.context)
        {
            pdftest::AppToolShellFixture::waitFor([&opened]() { return !opened.context || opened.context->getContainer(); });
            pdftest::AppToolShellFixture::settle();
        }
        return opened;
    }

    /// Shows the page through the rules of the GUI and waits for its container
    static bool showPage(Context* context, pdf::PDFInteger pageIndex)
    {
        QPointer<Context> guard(context);
        context->requestPage(pageIndex);
        return pdftest::AppToolShellFixture::waitFor([&guard, pageIndex]() { return !guard || (guard->getPageIndex() == pageIndex && guard->getContainer()); }) && guard;
    }

    static pdf::PDFContentNodeReference node(const pdf::PDFContentContainerKey& container, quint64 id)
    {
        pdf::PDFContentNodeReference reference;
        reference.container = container;
        reference.node.value = id;
        return reference;
    }

    /// The Path nodes among the children of the root of the container of the context
    static std::vector<pdf::PDFContentNodeReference> getPaths(const Context* context)
    {
        std::vector<pdf::PDFContentNodeReference> paths;
        const pdf::PDFContentContainerPointer container = context->getContainer();
        if (container && container->getRoot())
        {
            for (const pdf::PDFContentNodePointer& child : container->getRoot()->getChildren())
            {
                if (child->getKind() == pdf::PDFContentNodeKind::Path)
                {
                    paths.push_back(node(container->getKey(), child->getId().value));
                }
            }
        }
        return paths;
    }

    /// The Path objects among the children of the root of a page of a document (read by a session of its own)
    static int countPaths(const pdf::PDFDocumentPointer& document, pdf::PDFInteger pageIndex)
    {
        pdf::PDFContentEditSession session(document, nullptr, nullptr);
        QString errorText;
        if (!session.loadContainer(session.getPageKey(pageIndex), nullptr, &errorText))
        {
            return -1;
        }
        int paths = 0;
        for (const pdf::PDFContentNodePointer& child : session.getContainer(session.getPageKey(pageIndex))->getRoot()->getChildren())
        {
            paths += child->getKind() == pdf::PDFContentNodeKind::Path ? 1 : 0;
        }
        return paths;
    }

    /// Deletes the first path of the page shown (one undo step of work)
    static bool deleteFirstPath(Context* context)
    {
        const std::vector<pdf::PDFContentNodeReference> paths = getPaths(context);
        return !paths.empty() && context->execute(pdf::PDFContentCommandFactory::createDelete({ paths.front() }));
    }

    /// The texts of the labels of a widget
    static QStringList getLabelTexts(const QWidget* widget)
    {
        QStringList texts;
        for (const QLabel* label : widget->findChildren<QLabel*>())
        {
            texts << label->text();
        }
        return texts;
    }

    static bool hasLabel(const QWidget* widget, const QString& text)
    {
        const bool isFound = widget && getLabelTexts(widget).contains(text);
        if (!isFound && widget)
        {
            qWarning() << "label" << text << "not in" << getLabelTexts(widget);
        }
        return isFound;
    }

    static std::optional<Notice> currentNotice(const GUI* gui)
    {
        return gui && gui->getNotices() ? gui->getNotices()->getCurrent() : std::nullopt;
    }

    /// A notice of a test
    static Notice notice(const QString& id, Notice::Kind kind, const QString& text, bool isOncePerSession = false)
    {
        Notice request;
        request.id = id;
        request.kind = kind;
        request.title = QStringLiteral("Title ") + id;
        request.text = text;
        request.isOncePerSession = isOncePerSession;
        return request;
    }

    static QString currentId(const Notices& notices)
    {
        const std::optional<Notice> current = notices.getCurrent();
        return current ? current->id : QString();
    }

    /// The real parts of the other packages of stage E1 (their entries are registered then)
    static bool isCanvasReal(const GUI* gui) { return gui && gui->getCanvas() && gui->getCanvas()->getView(); }
    static bool isSelectRegistered() { return Registry::getInstance().findTool(QString::fromLatin1(pdf::PDFAppEditContentToolIds::SELECT)) != nullptr; }
    static bool isMenusReal() { return Registry::getInstance().findAction(QString::fromLatin1(pdf::PDFAppEditContentActionIds::GROUP)) != nullptr; }
    static bool isCardsReal() { return Registry::getInstance().findCard(QString::fromLatin1(pdf::PDFAppEditContentCardIds::SELECTION)) != nullptr; }

    /// Closes the tool without a question (the end of a test)
    static void close(ContentToolOpened& opened)
    {
        if (opened.shell)
        {
            opened.shell->close();
            pdftest::AppToolShellFixture::waitFor([&opened]() { return !opened.shell || opened.shell->getState() == pdf::PDFDocumentToolShell::State::Closed; });
        }
        pdftest::AppToolShellFixture::settle();
    }

    /// A failed result of the tool with the errors of the pages (PDFDocumentToolContent::DATA_PAGE_ERRORS)
    static pdf::PDFDocumentToolResult createPageErrors(const QJsonArray& pages)
    {
        pdf::PDFDocumentToolResult result = pdf::PDFDocumentToolResult::createFailed(QStringLiteral("1 page(s) cannot be written."));
        result.data.insert(QString::fromLatin1(pdf::PDFDocumentToolContent::DATA_PAGE_ERRORS), pages);
        return result;
    }
};

class AppToolContentTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanup();

    // the notices (zadani_gui.md 4.15, KE.28)
    void noticesStateAndEvent();
    void noticesClosedAndOncePerSession();
    void noticeWidgetShowsCurrent();

    // the keys (zadani_gui.md 4.20, 4.23)
    void keyRowsOfRegistry();
    void keyboardShortcutsAndRegions();

    // the tool in the window
    void toolbarOfTheTool();
    void shapeMenuAndMultiple();
    void previewToggle();
    void leftPanelTabs();
    void statusItems();
    void pageSwitchRules();
    void routedPageCommands();
    void goToPage();
    void undoAcrossPages();
    void confirmStateAtCleanPoint();
    void lockIsNoWork();
    void escapeOrder();
    void applyChangesResult();
    void applyErrorBackToEditing();
    void applyErrorSkipPage();
    void applyErrorWithoutPage();
    void staleDocument();
    void cancelAsksWithWork();
    void saveQuestionAnswers();
    void clearPage();
    void pageNotices();
    void conformanceNotice();
    void commandRefusedNotice();
    void languageChange();
    void leakPixels();

    // rows over the real parts of the other packages
    void toolButtonsFromRegistry();
    void keyboardCard();
    void viewPanel();
};

void AppToolContentTest::initTestCase()
{
    qRegisterMetaType<pdf::PDFAppEditContentNoticeRequest>();
    qRegisterMetaType<pdf::PDFContentNodeReference>();
}

void AppToolContentTest::cleanup()
{
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
}

// ------------------------------------------------------------------ notices

void AppToolContentTest::noticesStateAndEvent()
{
    // zadani_gui.md 4.15 "Notices": one notice at a time; an event covers the state below it and leaves with the
    // next change of the selection; the states wait, the newest first, until they are left (hideNotice)
    using H = ContentToolHelper;
    Registry registry;
    QObject owner;
    Context* context = new Context(pdf::PDFDocumentPointer(new pdf::PDFDocument(H::createDocument(1))), nullptr, &registry, &owner);
    Notices notices(context, &owner);
    QSignalSpy changed(&notices, &Notices::currentChanged);
    QVERIFY(!notices.getCurrent());

    context->showNotice(H::notice(QStringLiteral("a"), Notice::Kind::State, QStringLiteral("A")));
    QCOMPARE(H::currentId(notices), QStringLiteral("a"));
    context->showNotice(H::notice(QStringLiteral("b"), Notice::Kind::State, QStringLiteral("B")));
    QCOMPARE(H::currentId(notices), QStringLiteral("b"));
    QCOMPARE(changed.count(), 2);

    // The same state with the same text again: no change; with a new text: replaced in place (the order stays)
    context->showNotice(H::notice(QStringLiteral("b"), Notice::Kind::State, QStringLiteral("B")));
    QCOMPARE(changed.count(), 2);
    context->showNotice(H::notice(QStringLiteral("a"), Notice::Kind::State, QStringLiteral("A2")));
    QCOMPARE(H::currentId(notices), QStringLiteral("b"));
    QCOMPARE(changed.count(), 2);

    // An event covers the states
    context->showNotice(H::notice(QStringLiteral("e"), Notice::Kind::Event, QStringLiteral("E")));
    QCOMPARE(H::currentId(notices), QStringLiteral("e"));
    QCOMPARE(changed.count(), 3);

    // ... and leaves with the next change of the selection
    context->getSelection()->setNodes({ H::node(context->getSession()->getPageKey(0), 1) });
    QCOMPARE(H::currentId(notices), QStringLiteral("b"));
    QCOMPARE(changed.count(), 4);

    // A state left: the older one again, with its new text
    context->hideNotice(QStringLiteral("b"));
    QCOMPARE(H::currentId(notices), QStringLiteral("a"));
    QCOMPARE(notices.getCurrent()->text, QStringLiteral("A2"));
    context->hideNotice(QStringLiteral("a"));
    QVERIFY(!notices.getCurrent());
    QCOMPARE(changed.count(), 6);

    // An invalid request (no text) is nothing
    context->showNotice(H::notice(QStringLiteral("x"), Notice::Kind::State, QString()));
    QVERIFY(!notices.getCurrent());
    QCOMPARE(changed.count(), 6);
}

void AppToolContentTest::noticesClosedAndOncePerSession()
{
    // A state closed by the user stays hidden while it holds and comes back when it is entered again; a notice of
    // isOncePerSession closed by the user does not come back (QE.2); a closed event is gone
    using H = ContentToolHelper;
    Registry registry;
    QObject owner;
    Context* context = new Context(pdf::PDFDocumentPointer(new pdf::PDFDocument(H::createDocument(1))), nullptr, &registry, &owner);
    Notices notices(context, &owner);

    context->showNotice(H::notice(QStringLiteral("s"), Notice::Kind::State, QStringLiteral("S")));
    notices.closeCurrent();
    QVERIFY(!notices.getCurrent());
    context->showNotice(H::notice(QStringLiteral("s"), Notice::Kind::State, QStringLiteral("S new text")));
    QVERIFY(!notices.getCurrent());
    context->hideNotice(QStringLiteral("s"));
    context->showNotice(H::notice(QStringLiteral("s"), Notice::Kind::State, QStringLiteral("S")));
    QCOMPARE(H::currentId(notices), QStringLiteral("s"));
    context->hideNotice(QStringLiteral("s"));

    context->showNotice(H::notice(QStringLiteral("once"), Notice::Kind::State, QStringLiteral("O"), true));
    QCOMPARE(H::currentId(notices), QStringLiteral("once"));
    notices.closeCurrent();
    context->hideNotice(QStringLiteral("once"));
    context->showNotice(H::notice(QStringLiteral("once"), Notice::Kind::State, QStringLiteral("O"), true));
    QVERIFY(!notices.getCurrent());

    context->showNotice(H::notice(QStringLiteral("e"), Notice::Kind::Event, QStringLiteral("E")));
    notices.closeCurrent();
    QVERIFY(!notices.getCurrent());
}

void AppToolContentTest::noticeWidgetShowsCurrent()
{
    // The widget of the notice (KE.28): hidden without a notice, the title and the text of the current one, the
    // close button closes it
    using H = ContentToolHelper;
    Registry registry;
    QObject owner;
    Context* context = new Context(pdf::PDFDocumentPointer(new pdf::PDFDocument(H::createDocument(1))), nullptr, &registry, &owner);
    Notices notices(context, &owner);
    QWidget parent;
    parent.resize(600, 200);
    pdf::PDFAppEditContentNotice* widget = new pdf::PDFAppEditContentNotice(&notices, &parent);
    parent.show();
    QVERIFY(QTest::qWaitForWindowExposed(&parent));
    QVERIFY(!widget->isVisible());
    QVERIFY(widget->getTitle().isEmpty() && widget->getText().isEmpty());

    Notice request = H::notice(QStringLiteral("w"), Notice::Kind::State, QStringLiteral("The text of the notice"));
    request.severity = Notice::Severity::Warning;
    context->showNotice(request);
    QVERIFY(widget->isVisible());
    QCOMPARE(widget->getTitle(), QStringLiteral("Title w"));
    QCOMPARE(widget->getText(), QStringLiteral("The text of the notice"));
    QVERIFY(H::hasLabel(widget, QStringLiteral("The text of the notice")));

    QAbstractButton* close = widget->findChild<QAbstractButton*>(QStringLiteral("editContentNoticeClose"));
    QVERIFY(close && close->isVisible());
    QTest::mouseClick(close, Qt::LeftButton);
    QVERIFY(!notices.getCurrent());
    QVERIFY(!widget->isVisible());
    QVERIFY(widget->getTitle().isEmpty());
}

// ------------------------------------------------------------------ keys

void AppToolContentTest::keyRowsOfRegistry()
{
    // The rows of the card Keyboard (edit-content-v2-keyboard): the shortcuts of the action entries, Preview, F6;
    // the keys of the tool Select only when it is registered ("hidden, not disabled")
    using K = pdf::PDFAppEditContentKeyboard;
    Registry registry;
    K::registerParts(&registry);
    const std::vector<std::pair<QString, QString>> rows = K::getKeyRows(registry);
    const QString slash = QKeySequence(Qt::CTRL | Qt::Key_Slash).toString(QKeySequence::NativeText);
    QVERIFY(rows.size() == 3);
    QCOMPARE(rows[0], std::make_pair(slash, QStringLiteral("Keyboard Shortcuts")));
    QCOMPARE(rows[1], std::make_pair(QStringLiteral("P"), QStringLiteral("Preview")));
    QCOMPARE(rows[2], std::make_pair(QStringLiteral("F6"), QStringLiteral("Next pane")));

    // The entries of P14 in the registry of the application and the keys pass the rules of the key table
    const Registry& application = Registry::getInstance();
    for (const char* id : { pdf::PDFAppEditContentActionIds::PREVIEW, pdf::PDFAppEditContentActionIds::VIEW_KEYBOARD, pdf::PDFAppEditContentActionIds::CLEAR_PAGE })
    {
        QVERIFY2(application.findAction(QString::fromLatin1(id)), id);
    }
    QVERIFY(application.findCard(QString::fromLatin1(pdf::PDFAppEditContentCardIds::KEYBOARD)));
    for (const QString& command : { pdf::PDFAppCommands::zoomIn(), pdf::PDFAppCommands::fitPage(), pdf::PDFAppCommands::nextPage(), pdf::PDFAppCommands::goToPage() })
    {
        const pdf::PDFAppEditContentActionEntry* entry = application.findActionByApplicationCommand(command);
        QVERIFY2(entry, qPrintable(command));
        QCOMPARE(entry->getId(), K::getCommandActionId(command));
    }
    QVERIFY(pdf::PDFAppEditContentKeyTable::check(pdf::PDFAppEditContentKeyTable::getRows(registry), { }).isEmpty());
}

void AppToolContentTest::keyboardShortcutsAndRegions()
{
    // Way 3 of zadani_gui.md 4.20: the shortcuts of the scope Tool are actions on the key widget of the host, which
    // trigger their entry from the event loop; F6 / Shift+F6 walk the regions (4.23); the actions leave the key widget
    // with the tool
    using H = ContentToolHelper;
    pdftest::AppToolShellFixture fixture;
    QVERIFY(fixture.window);
    ContentToolOpened opened = H::open(fixture, H::createDocument(1), QStringLiteral("keys.pdf"));
    QVERIFY(opened.isValid());

    pdf::PDFAppEditContentKeyboard* keyboard = opened.gui->getKeyboard();
    QVERIFY(keyboard);
    QWidget* keyWidget = opened.shell->getHost()->getKeyWidget();
    QAction* cardAction = nullptr;
    QAction* nextRegion = nullptr;
    for (QAction* action : keyboard->getActions())
    {
        QVERIFY(keyWidget->actions().contains(action));
        QCOMPARE(action->shortcutContext(), Qt::WidgetWithChildrenShortcut);
        if (action->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_Slash))
        {
            cardAction = action;
        }
        if (action->shortcut() == QKeySequence(Qt::Key_F6))
        {
            nextRegion = action;
        }
    }
    QVERIFY(cardAction && nextRegion);

    // Ctrl+/ requests the card Keyboard - from the event loop
    const QString cardId = QString::fromLatin1(pdf::PDFAppEditContentCardIds::KEYBOARD);
    cardAction->trigger();
    QVERIFY(!opened.context->isCardRequested(cardId));
    QTRY_VERIFY_WITH_TIMEOUT(opened.context->isCardRequested(cardId), H::timeout(5000));
    cardAction->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(!opened.context->isCardRequested(cardId), H::timeout(5000));

    // F6: toolbar -> left panel -> canvas -> right panel -> toolbar
    const std::vector<QWidget*> regions = keyboard->getRegions();
    QCOMPARE(regions.size(), size_t(4));
    QCOMPARE(regions[0], static_cast<QWidget*>(opened.shell->getToolbar()));
    QCOMPARE(regions[1], opened.gui->getLeftPanelWidget());
    QCOMPARE(regions[2], static_cast<QWidget*>(opened.gui->getCanvas()));
    QCOMPARE(regions[3], static_cast<QWidget*>(opened.gui->getCardStack()));
    fixture.window->activateWindow();
    QAbstractButton* tab = opened.gui->getTabButton(QStringLiteral("objects"));
    QVERIFY(tab);
    tab->setFocus(Qt::OtherFocusReason);
    QTRY_VERIFY(regions[1]->isAncestorOf(QApplication::focusWidget()));
    nextRegion->trigger();
    const QWidget* focus = QApplication::focusWidget();
    QVERIFY(focus && !regions[1]->isAncestorOf(focus) && focus != regions[1]);

    // The actions leave the key widget with the tool
    std::vector<QPointer<QAction>> actions;
    for (QAction* action : keyboard->getActions())
    {
        actions.emplace_back(action);
    }
    QPointer<GUI> gui = opened.gui;
    QTest::mouseClick(opened.shell->getToolbar()->getCancelButton(), Qt::LeftButton);
    QVERIFY(pdftest::AppToolShellFixture::waitFor([&gui]() { return !gui; }));
    for (const QPointer<QAction>& action : actions)
    {
        QVERIFY(!action || !keyWidget->actions().contains(action.data()));
    }
}

// ------------------------------------------------------------------ the tool in the window

void AppToolContentTest::toolbarOfTheTool()
{
    // edit-content-v2 1647:21313: after the tool buttons View, Preview | Clear Page (the entries of P14 placed in
    // TOOLBAR, View of the GUI); their labels go first in a narrow window; Multiple only with a creation tool; Apply
    // Changes with the icon check, disabled without work
    using H = ContentToolHelper;
    pdftest::AppToolShellFixture fixture;
    QVERIFY(fixture.window);
    ContentToolOpened opened = H::open(fixture, H::createDocument(1), QStringLiteral("toolbar.pdf"));
    QVERIFY(opened.isValid());
    QVERIFY(opened.context->isEditingAllowed());

    QAbstractButton* preview = opened.gui->getActionButton(QString::fromLatin1(pdf::PDFAppEditContentActionIds::PREVIEW));
    QAbstractButton* clear = opened.gui->getActionButton(QString::fromLatin1(pdf::PDFAppEditContentActionIds::CLEAR_PAGE));
    QAbstractButton* view = opened.gui->getViewButton();
    QVERIFY(preview && clear && view);
    QCOMPARE(preview->text(), QStringLiteral("Preview"));
    QCOMPARE(clear->text(), QStringLiteral("Clear Page"));
    QCOMPARE(view->text(), QStringLiteral("View"));
    QVERIFY(preview->isCheckable() && !clear->isCheckable());
    for (QAbstractButton* button : { preview, clear, view })
    {
        QVERIFY(button->property(pdf::PDFAppToolToolbar::NARROW_FIRST_PROPERTY).toBool());
        QVERIFY(button->isVisible() && button->isEnabled());
    }

    // View, Preview | Clear Page in this order, a divider between them
    QVERIFY(view->mapTo(opened.gui->getToolActionsWidget(), QPoint()).x() < preview->mapTo(opened.gui->getToolActionsWidget(), QPoint()).x());
    QVERIFY(preview->mapTo(opened.gui->getToolActionsWidget(), QPoint()).x() < clear->mapTo(opened.gui->getToolActionsWidget(), QPoint()).x());

    // No creation tool in stage E1: no Multiple
    const bool hasCreationTool = Registry::getInstance().getTools(pdf::PDFAppEditContentToolEntry::Place::Toolbar).size() > (H::isSelectRegistered() ? 1u : 0u);
    QCOMPARE(opened.gui->getMultipleButton() != nullptr, hasCreationTool);

    pdf::PDFAppToolToolbar* toolbar = opened.shell->getToolbar();
    QCOMPARE(toolbar->getConfirmButton()->text(), QStringLiteral("Apply Changes"));
    QCOMPARE(opened.gui->getConfirmIconName(), QStringLiteral("check"));
    QVERIFY(!toolbar->getConfirmButton()->isEnabled());

    // Clear Page follows its entry: disabled on a page without content
    QVERIFY(opened.context->execute(pdf::PDFContentCommandFactory::createClearPage(opened.context->getPageKey())));
    QVERIFY(!clear->isEnabled());
    QVERIFY(toolbar->getConfirmButton()->isEnabled());
    H::close(opened);
}

void AppToolContentTest::shapeMenuAndMultiple()
{
    // zadani_gui.md 4.20 (edit-content-v2 1647:21313, -menus-shape): the tool buttons in their order with Shape at
    // the order 400; Shape opens the panel of the shape tools, a row starts its tool (from the event loop); Multiple
    // exists with a creation tool and is the toggle of the controller
    using H = ContentToolHelper;
    using Place = pdf::PDFAppEditContentToolEntry::Place;
    Registry registry;
    QVERIFY(registry.addTool(std::make_unique<ContentToolTestToolEntry>(QStringLiteral("t-select"), Place::Toolbar, 100, QStringLiteral("Pick"), QKeySequence(Qt::Key_V))));
    QVERIFY(registry.addTool(std::make_unique<ContentToolTestToolEntry>(QStringLiteral("t-pen"), Place::Toolbar, 500, QStringLiteral("Draw"), QKeySequence(Qt::Key_N))));
    QVERIFY(registry.addTool(std::make_unique<ContentToolTestToolEntry>(QStringLiteral("s-rectangle"), Place::ShapeMenu, 100, QStringLiteral("Box"), QKeySequence(Qt::Key_R))));
    QVERIFY(registry.addTool(std::make_unique<ContentToolTestToolEntry>(QStringLiteral("s-ellipse"), Place::ShapeMenu, 200, QStringLiteral("Oval"), QKeySequence(Qt::Key_E))));
    GUI::setRegistry(&registry);
    auto resetRegistry = qScopeGuard([]() { GUI::setRegistry(nullptr); });

    pdftest::AppToolShellFixture fixture;
    QVERIFY(fixture.window);
    ContentToolOpened opened = H::open(fixture, H::createDocument(1), QStringLiteral("shape.pdf"));
    QVERIFY(opened.isValid());
    QWidget* actions = opened.gui->getToolActionsWidget();
    QAbstractButton* pick = opened.gui->getToolButton(QStringLiteral("t-select"));
    QAbstractButton* draw = opened.gui->getToolButton(QStringLiteral("t-pen"));
    QAbstractButton* shape = opened.gui->getShapeButton();
    QAbstractButton* multiple = opened.gui->getMultipleButton();
    QVERIFY(pick && draw && shape && multiple);
    QCOMPARE(pick->toolTip(), QStringLiteral("Pick (V)\nDescription of Pick"));
    QCOMPARE(pick->accessibleName(), QStringLiteral("Pick"));
    QCOMPARE(shape->text(), QStringLiteral("Shape"));
    QVERIFY(pick->mapTo(actions, QPoint()).x() < shape->mapTo(actions, QPoint()).x());
    QVERIFY(shape->mapTo(actions, QPoint()).x() < draw->mapTo(actions, QPoint()).x());
    QVERIFY(draw->mapTo(actions, QPoint()).x() < multiple->mapTo(actions, QPoint()).x());

    // A tool button switches the tool; the check follows the controller
    QTest::mouseClick(draw, Qt::LeftButton);
    QCOMPARE(opened.context->getToolController()->getActiveToolId(), QStringLiteral("t-pen"));
    QVERIFY(draw->isChecked() && !pick->isChecked());

    // Shape: the rows of the shape tools, a row starts its tool from the event loop
    QTest::mouseClick(shape, Qt::LeftButton);
    pdftest::AppToolShellFixture::settle();
    pdf::PDFAppToolPanelPopup* panel = opened.shell->getToolbar()->getOpenPanel();
    QVERIFY(panel);
    QStringList titles;
    for (const QPointer<pdf::PDFTileRow>& row : panel->getRows())
    {
        titles << (row ? row->getTitle() : QString());
    }
    QCOMPARE(titles, QStringList({ QStringLiteral("Box"), QStringLiteral("Oval") }));
    QTest::mouseClick(panel->findChild<QAbstractButton*>(QStringLiteral("editContentShape-s-ellipse")), Qt::LeftButton);
    QCOMPARE(opened.context->getToolController()->getActiveToolId(), QStringLiteral("t-pen"));
    QTRY_COMPARE_WITH_TIMEOUT(opened.context->getToolController()->getActiveToolId(), QStringLiteral("s-ellipse"), H::timeout(5000));
    QVERIFY(!pick->isChecked() && !draw->isChecked());

    QTest::mouseClick(multiple, Qt::LeftButton);
    QVERIFY(opened.context->getToolController()->isMultiple());
    QTest::mouseClick(multiple, Qt::LeftButton);
    QVERIFY(!opened.context->getToolController()->isMultiple());
    H::close(opened);
}

void AppToolContentTest::previewToggle()
{
    // zadani_gui.md 4.15: Preview pauses the editing - the toggle is checked, Clear Page and the tools are disabled,
    // the notice of the frame -preview and its status text; Undo is paused (canUndo false)
    using H = ContentToolHelper;
    pdftest::AppToolShellFixture fixture;
    QVERIFY(fixture.window);
    ContentToolOpened opened = H::open(fixture, H::createDocument(1), QStringLiteral("preview.pdf"));
    QVERIFY(opened.isValid());
    QVERIFY(H::deleteFirstPath(opened.context));
    pdf::PDFAppToolUndoTarget* target = opened.shell->getUndoTarget();
    QVERIFY(target->canUndo());

    QAbstractButton* preview = opened.gui->getActionButton(QString::fromLatin1(pdf::PDFAppEditContentActionIds::PREVIEW));
    QAbstractButton* clear = opened.gui->getActionButton(QString::fromLatin1(pdf::PDFAppEditContentActionIds::CLEAR_PAGE));
    QTest::mouseClick(preview, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(opened.context->isPreviewActive(), H::timeout(5000));
    QVERIFY(preview->isChecked() && preview->isEnabled());
    QVERIFY(!clear->isEnabled());
    QVERIFY(!target->canUndo());
    const std::optional<Notice> notice = H::currentNotice(opened.gui);
    QVERIFY(notice);
    QCOMPARE(notice->id, QString::fromLatin1(NoticeIds::PREVIEW));
    QCOMPARE(notice->title, QStringLiteral("Preview – exact rendering"));
    QCOMPARE(notice->text, QStringLiteral("The page is drawn by the PDF renderer with your edits applied, so overprint, knockout, blend modes, soft masks and spot colors look as they will be written. Editing is paused."));
    QCOMPARE(opened.gui->getStatusText(), QStringLiteral("Preview is on · editing is paused · press P or click Preview to continue editing"));

    QTest::mouseClick(preview, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(!opened.context->isPreviewActive(), H::timeout(5000));
    QVERIFY(!preview->isChecked() && clear->isEnabled());
    QVERIFY(target->canUndo());
    QVERIFY(!H::currentNotice(opened.gui) || H::currentNotice(opened.gui)->id != QLatin1String(NoticeIds::PREVIEW));
    H::close(opened);
}

void AppToolContentTest::leftPanelTabs()
{
    // zadani_gui.md 4.18, the frames edit-content-v2 and -pages: a strip of icon tabs (Sidebar tab) over the panels of
    // the tab entries; the tab shown is the stored option leftTab (4.26), it survives the rebuild of the panels
    using H = ContentToolHelper;
    using O = pdf::PDFAppEditContentOptions;
    pdftest::AppToolShellFixture fixture;
    QVERIFY(fixture.window);
    ContentToolOpened opened = H::open(fixture, H::createDocument(1), QStringLiteral("tabs.pdf"));
    QVERIFY(opened.isValid());

    QAbstractButton* objects = opened.gui->getTabButton(QStringLiteral("objects"));
    QAbstractButton* pages = opened.gui->getTabButton(QStringLiteral("pages"));
    QVERIFY(objects && pages);
    QVERIFY(objects->text().isEmpty() && !objects->icon().isNull());
    QCOMPARE(objects->size(), QSize(32, 32));
    QVERIFY(objects->isChecked() && !pages->isChecked());
    QStackedWidget* stack = opened.gui->getLeftPanelWidget()->findChild<QStackedWidget*>(QStringLiteral("editContentTabPanels"));
    QVERIFY(stack);
    QCOMPARE(stack->currentIndex(), 0);

    QTest::mouseClick(pages, Qt::LeftButton);
    QCOMPARE(opened.context->getOptions()->getValue(QString::fromLatin1(O::LEFT_TAB)).toString(), QStringLiteral("pages"));
    QCOMPARE(opened.gui->getModel()->getValue(QString::fromLatin1(O::LEFT_TAB)).toString(), QStringLiteral("pages"));
    QCOMPARE(stack->currentIndex(), 1);
    QVERIFY(pages->isChecked() && !objects->isChecked());

    // A change of the language builds the panel again on the same tab
    ContentToolTranslator translator;
    QCoreApplication::installTranslator(&translator);
    pdftest::AppToolShellFixture::settle();
    QCoreApplication::removeTranslator(&translator);
    pdftest::AppToolShellFixture::settle();
    QAbstractButton* pagesAgain = opened.gui->getTabButton(QStringLiteral("pages"));
    QVERIFY(pagesAgain && pagesAgain->isChecked());
    H::close(opened);
}

void AppToolContentTest::statusItems()
{
    // The status bar (zadani_gui.md 4.21; edit-content-v2): the own page item "Page 3 of 1,592" and the zoom of the
    // view of the tool; the numbers by PDFAppLocale
    using H = ContentToolHelper;
    pdftest::AppToolShellFixture fixture;
    QVERIFY(fixture.window);
    ContentToolOpened opened = H::open(fixture, H::createDocument(3), QStringLiteral("status.pdf"));
    QVERIFY(opened.isValid());
    QVERIFY(opened.gui->hasOwnStatusItems());
    QCOMPARE(opened.gui->getOwnTabPageText(), QStringLiteral("Page 1 of 3"));
    QVERIFY(H::showPage(opened.context, 2));
    QCOMPARE(opened.gui->getOwnTabPageText(), QStringLiteral("Page 3 of 3"));
    if (H::isCanvasReal(opened.gui))
    {
        QCOMPARE(opened.gui->getOwnTabZoomText(), opened.context->getView()->getZoomText());
        QVERIFY(!opened.gui->getOwnTabZoomText().isEmpty());
    }
    else
    {
        // The stub canvas has no view: no zoom item
        QVERIFY(opened.gui->getOwnTabZoomText().isEmpty());
    }
    H::close(opened);
}

void AppToolContentTest::pageSwitchRules()
{
    // zadani_gui.md 4.27: a switch of the page commits the pending values first, clears the selection, then shows the
    // page; a page out of range and the page shown are no switch
    using H = ContentToolHelper;
    pdftest::AppToolShellFixture fixture;
    QVERIFY(fixture.window);
    ContentToolOpened opened = H::open(fixture, H::createDocument(3), QStringLiteral("switch.pdf"));
    QVERIFY(opened.isValid());

    const std::vector<pdf::PDFContentNodeReference> paths = H::getPaths(opened.context);
    QCOMPARE(paths.size(), size_t(2));
    opened.context->getSelection()->setNodes(paths);

    // The order: the commit of the cards before the page changes, the selection empty when it does
    QStringList log;
    QPointer<Context> context = opened.context;
    QObject receiver;
    QObject::connect(opened.context, &Context::pendingEditsCommitRequested, &receiver, [&log, context]() { log << QStringLiteral("commit:%1:%2").arg(context->getPageIndex()).arg(context->getSelection()->getNodes().size()); });
    QObject::connect(opened.context, &Context::pageChanged, &receiver, [&log, context](pdf::PDFInteger page) { log << QStringLiteral("page:%1:%2").arg(page).arg(context->getSelection()->getNodes().size()); });

    opened.context->requestPage(1);
    QCOMPARE(log, QStringList({ QStringLiteral("commit:0:2"), QStringLiteral("page:1:0") }));
    QCOMPARE(opened.context->getPageIndex(), pdf::PDFInteger(1));

    log.clear();
    opened.context->requestPage(1);
    opened.context->requestPage(3);
    opened.context->requestPage(-1);
    QVERIFY(log.isEmpty());
    QCOMPARE(opened.context->getPageIndex(), pdf::PDFInteger(1));
    H::close(opened);
}

void AppToolContentTest::routedPageCommands()
{
    // zadani_gui.md 4.20, way 2: the commands of the pages of the application act on the page of the tool (from the
    // event loop); a command, whose page does not exist, is the tool's and disabled; the zoom is the tool's only with
    // the view of the tool
    using H = ContentToolHelper;
    using C = pdf::PDFAppCommands;
    pdftest::AppToolShellFixture fixture;
    QVERIFY(fixture.window);
    ContentToolOpened opened = H::open(fixture, H::createDocument(3), QStringLiteral("pages.pdf"));
    QVERIFY(opened.isValid());

    QCOMPARE(opened.gui->getDocumentCommandState(C::previousPage()), State::Disabled);
    QCOMPARE(opened.gui->getDocumentCommandState(C::firstPage()), State::Disabled);
    QCOMPARE(opened.gui->getDocumentCommandState(C::nextPage()), State::Enabled);
    QVERIFY(opened.tab->isCommandEnabled(C::nextPage()));

    QVERIFY(opened.tab->executeCommand(C::nextPage()));
    QCOMPARE(opened.context->getPageIndex(), pdf::PDFInteger(0));
    QTRY_COMPARE_WITH_TIMEOUT(opened.context->getPageIndex(), pdf::PDFInteger(1), H::timeout(5000));

    QVERIFY(opened.tab->executeCommand(C::lastPage()));
    QTRY_COMPARE_WITH_TIMEOUT(opened.context->getPageIndex(), pdf::PDFInteger(2), H::timeout(5000));
    QCOMPARE(opened.gui->getDocumentCommandState(C::nextPage()), State::Disabled);
    pdftest::AppToolShellFixture::settle();
    QVERIFY(!fixture.window->getCommandAction(C::nextPage())->isEnabled());

    QVERIFY(opened.tab->executeCommand(C::previousPage()));
    QTRY_COMPARE_WITH_TIMEOUT(opened.context->getPageIndex(), pdf::PDFInteger(1), H::timeout(5000));
    QVERIFY(opened.tab->executeCommand(C::firstPage()));
    QTRY_COMPARE_WITH_TIMEOUT(opened.context->getPageIndex(), pdf::PDFInteger(0), H::timeout(5000));

    // The zoom: the view of the tool takes it; without the view (the stub of P11) the application keeps it
    const State zoomState = opened.gui->getDocumentCommandState(C::zoomIn());
    QCOMPARE(zoomState, H::isCanvasReal(opened.gui) ? State::Enabled : State::NotHandled);
    QCOMPARE(opened.gui->getDocumentCommandState(C::undo()), State::NotHandled);
    H::close(opened);
}

void AppToolContentTest::goToPage()
{
    // Go to page… of the application (Ctrl+G): the question of the page number of the tool, its Go to Page switches
    // the page by the rules of 4.27
    using H = ContentToolHelper;
    using C = pdf::PDFAppCommands;
    pdftest::AppToolShellFixture fixture;
    QVERIFY(fixture.window);
    ContentToolOpened opened = H::open(fixture, H::createDocument(3), QStringLiteral("goto.pdf"));
    QVERIFY(opened.isValid());

    QCOMPARE(opened.gui->getDocumentCommandState(C::goToPage()), State::Enabled);
    QVERIFY(opened.tab->executeCommand(C::goToPage()));
    QTRY_VERIFY_WITH_TIMEOUT(opened.gui->getGoToPageQuestion() != nullptr, H::timeout(5000));
    pdf::PDFMessageBox* question = opened.gui->getGoToPageQuestion();
    QVERIFY(H::hasLabel(question, QStringLiteral("Go to page")));
    QSpinBox* field = question->findChild<QSpinBox*>(QStringLiteral("editContentGoToPageField"));
    QVERIFY(field);
    QCOMPARE(field->minimum(), 1);
    QCOMPARE(field->maximum(), 3);
    QCOMPARE(field->value(), 1);
    field->setValue(3);
    QTest::mouseClick(question->findChild<QPushButton*>(QStringLiteral("editContentGoToPageConfirm")), Qt::LeftButton);
    QCOMPARE(opened.context->getPageIndex(), pdf::PDFInteger(2));
    QTRY_VERIFY_WITH_TIMEOUT(!opened.gui->getGoToPageQuestion(), H::timeout(5000));

    // Cancel changes nothing
    QVERIFY(opened.gui->openGoToPage());
    opened.gui->getGoToPageQuestion()->findChild<QSpinBox*>()->setValue(1);
    QTest::mouseClick(opened.gui->getGoToPageQuestion()->findChild<QPushButton*>(QStringLiteral("editContentGoToPageCancel")), Qt::LeftButton);
    QCOMPARE(opened.context->getPageIndex(), pdf::PDFInteger(2));
    H::close(opened);
}

void AppToolContentTest::undoAcrossPages()
{
    // KE.2, zadani_gui.md 4.27: one undo stack over the pages - Undo of a step of another page shows that page first
    // (the rules of the switch), then reverts the step; Redo the same
    using H = ContentToolHelper;
    pdftest::AppToolShellFixture fixture;
    QVERIFY(fixture.window);
    ContentToolOpened opened = H::open(fixture, H::createDocument(3), QStringLiteral("undo.pdf"));
    QVERIFY(opened.isValid());
    pdf::PDFAppToolUndoTarget* target = opened.shell->getUndoTarget();
    QVERIFY(qobject_cast<pdf::PDFAppEditContentUndoTarget*>(target));

    QVERIFY(H::deleteFirstPath(opened.context));
    QCOMPARE(target->getUndoText(), opened.context->getSession()->getUndoStack()->getUndoText());
    QVERIFY(!target->getUndoText().isEmpty());
    QVERIFY(H::showPage(opened.context, 2));
    QSignalSpy stateChanged(target, &pdf::PDFAppToolUndoTarget::undoStateChanged);

    target->undo();
    QCOMPARE(opened.context->getPageIndex(), pdf::PDFInteger(0));
    QVERIFY(opened.context->getSession()->getUndoStack()->isClean());
    QVERIFY(!target->canUndo() && target->canRedo());
    QVERIFY(stateChanged.count() >= 1);
    QTRY_VERIFY_WITH_TIMEOUT(opened.context->getContainer() && H::getPaths(opened.context).size() == 2, H::timeout(15000));

    QVERIFY(H::showPage(opened.context, 1));
    target->redo();
    QCOMPARE(opened.context->getPageIndex(), pdf::PDFInteger(0));
    QVERIFY(!opened.context->getSession()->getUndoStack()->isClean());
    QTRY_VERIFY_WITH_TIMEOUT(opened.context->getContainer() && H::getPaths(opened.context).size() == 1, H::timeout(15000));

    // The window: Undo of the application acts on the target
    QVERIFY(opened.tab->executeCommand(pdf::PDFAppCommands::undo()));
    QTRY_VERIFY_WITH_TIMEOUT(opened.context->getSession()->getUndoStack()->isClean(), H::timeout(5000));
    H::close(opened);
}

void AppToolContentTest::confirmStateAtCleanPoint()
{
    // The reviewer's note for P14 (notes 11.4): confirmStateChanged whenever the undo stack crosses its clean point,
    // also by Undo and Redo - the save question and the Save action follow it
    using H = ContentToolHelper;
    pdftest::AppToolShellFixture fixture;
    QVERIFY(fixture.window);
    ContentToolOpened opened = H::open(fixture, H::createDocument(1), QStringLiteral("clean.pdf"));
    QVERIFY(opened.isValid());
    QSignalSpy confirmChanged(opened.gui.data(), &pdf::PDFDocumentToolGUI::confirmStateChanged);
    QVERIFY(!opened.shell->hasSaveQuestion());

    QVERIFY(H::deleteFirstPath(opened.context));
    QCOMPARE(confirmChanged.count(), 1);
    QVERIFY(opened.shell->hasSaveQuestion());
    QVERIFY(H::deleteFirstPath(opened.context));
    QCOMPARE(confirmChanged.count(), 1);

    pdf::PDFAppToolUndoTarget* target = opened.shell->getUndoTarget();
    target->undo();
    QCOMPARE(confirmChanged.count(), 1);
    target->undo();
    QCOMPARE(confirmChanged.count(), 2);
    QVERIFY(!opened.shell->hasSaveQuestion());
    QVERIFY(!opened.shell->getToolbar()->getConfirmButton()->isEnabled());
    target->redo();
    QCOMPARE(confirmChanged.count(), 3);
    QVERIFY(opened.shell->getToolbar()->getConfirmButton()->isEnabled());
    H::close(opened);
}

void AppToolContentTest::lockIsNoWork()
{
    // RE-16, QE.8: a lock and "hidden in the editor" are no work - Apply Changes stays disabled, nothing to lose
    using H = ContentToolHelper;
    pdftest::AppToolShellFixture fixture;
    QVERIFY(fixture.window);
    ContentToolOpened opened = H::open(fixture, H::createDocument(1), QStringLiteral("lock.pdf"));
    QVERIFY(opened.isValid());
    const std::vector<pdf::PDFContentNodeReference> paths = H::getPaths(opened.context);
    QVERIFY(!paths.empty());
    opened.context->getSession()->setNodeLocked(paths.front(), true);
    opened.context->getSession()->setNodeHidden(paths.back(), true);
    pdftest::AppToolShellFixture::settle();
    QVERIFY(!opened.gui->isConfirmEnabled());
    QVERIFY(!opened.gui->hasUnsavedWork());
    QVERIFY(!opened.shell->getToolbar()->getConfirmButton()->isEnabled());
    QVERIFY(!opened.shell->getUndoTarget()->canUndo());
    H::close(opened);
}

void AppToolContentTest::escapeOrder()
{
    // KE.16: Esc ends the mode of the tool, then the selection, then the shell leaves the tool; the status says what
    // Esc does now
    using H = ContentToolHelper;
    pdftest::AppToolShellFixture fixture;
    QVERIFY(fixture.window);
    ContentToolOpened opened = H::open(fixture, H::createDocument(1), QStringLiteral("escape.pdf"));
    QVERIFY(opened.isValid());

    opened.context->getSelection()->setNodes(H::getPaths(opened.context));
    QCOMPARE(opened.gui->getEscapeModeHint(), QStringLiteral("Esc deselects"));
    QCOMPARE(opened.gui->getEscapeHint(), QStringLiteral("Esc deselects"));
    QVERIFY(opened.gui->handleEscape());
    QVERIFY(opened.context->getSelection()->isEmpty());
    QVERIFY(opened.gui->getEscapeModeHint().isEmpty());
    QVERIFY(!opened.gui->handleEscape());

    // Esc of the window: the selection first, the tool stays
    opened.context->getSelection()->setNodes(H::getPaths(opened.context));
    QTest::keyClick(opened.shell->getHost()->getKeyWidget(), Qt::Key_Escape);
    pdftest::AppToolShellFixture::settle();
    QVERIFY(opened.shell && opened.context->getSelection()->isEmpty());
    QCOMPARE(opened.shell->getState(), pdf::PDFDocumentToolShell::State::Editing);

    // Without a selection and without work Esc leaves the tool
    QTest::keyClick(opened.shell->getHost()->getKeyWidget(), Qt::Key_Escape);
    QVERIFY(pdftest::AppToolShellFixture::waitFor([&opened]() { return !opened.shell || opened.shell->getState() == pdf::PDFDocumentToolShell::State::Closed; }));
}

void AppToolContentTest::applyChangesResult()
{
    // zadani_gui.md 4.21, edit-content-v2-done: Apply Changes runs the tool with the snapshot of the session; the
    // result dialog "Content of 1 page changed" with the rows Objects added / edited / removed; Keep in This Tab
    // writes the content into the document of the tab
    using H = ContentToolHelper;
    pdftest::AppToolShellFixture fixture;
    QVERIFY(fixture.window);
    ContentToolOpened opened = H::open(fixture, H::createDocument(2), QStringLiteral("apply.pdf"));
    QVERIFY(opened.isValid());
    QVERIFY(H::deleteFirstPath(opened.context));

    const QVariantMap parameters = opened.gui->getRunParameters();
    QCOMPARE(parameters.value(QString::fromLatin1(pdf::PDFDocumentToolContent::PARAMETER_MODE)).toString(), QStringLiteral("apply"));
    const pdf::PDFContentEditSnapshotPointer snapshot = parameters.value(QString::fromLatin1(pdf::PDFDocumentToolContent::PARAMETER_SESSION)).value<pdf::PDFContentEditSnapshotPointer>();
    QVERIFY(snapshot && snapshot->containers.size() == 1);
    QVERIFY(pdf::PDFDocumentToolPageSelection::fromVariant(parameters.value(QString::fromLatin1(pdf::PDFDocumentToolContent::PARAMETER_SKIP_PAGES)), nullptr)->isNone);

    QPointer<pdf::PDFDocumentToolShell> shell = opened.shell;
    QTest::mouseClick(shell->getToolbar()->getConfirmButton(), Qt::LeftButton);
    QVERIFY(pdftest::AppToolShellFixture::waitFor([&shell]() { return !shell || shell->getResultDialog(); }, H::timeout(30000)));
    pdf::PDFAppToolResultDialog* dialog = shell->getResultDialog();
    QVERIFY(dialog);
    QVERIFY(H::hasLabel(dialog, QStringLiteral("Content of 1 page changed")));
    pdf::PDFKeyValueList* facts = dialog->getFacts();
    QVERIFY(facts);
    QStringList rows;
    for (int i = 0; i < facts->getRowCount(); ++i)
    {
        rows << facts->getKey(i) + QStringLiteral("=") + facts->getValue(i);
    }
    QVERIFY2(rows.contains(QStringLiteral("Objects added=0")), qPrintable(rows.join(QStringLiteral("; "))));
    QVERIFY2(rows.contains(QStringLiteral("Objects edited=0")), qPrintable(rows.join(QStringLiteral("; "))));
    QVERIFY2(rows.contains(QStringLiteral("Objects removed=1")), qPrintable(rows.join(QStringLiteral("; "))));
    QVERIFY(!rows.join(QLatin1Char(';')).contains(QStringLiteral("Objects=")));

    // Keep in This Tab: the tool closes, the document of the tab has one rectangle less on page 1
    QTest::mouseClick(dialog->getConfirmButton(), Qt::LeftButton);
    QVERIFY(pdftest::AppToolShellFixture::waitFor([&shell]() { return !shell; }, H::timeout(30000)));
    const pdf::PDFAppDocument* document = opened.tab->getDocument();
    QVERIFY(document->isModified());
    QCOMPARE(H::countPaths(document->getDocument(), 0), 1);
    QCOMPARE(H::countPaths(document->getDocument(), 1), 2);
}

void AppToolContentTest::applyErrorBackToEditing()
{
    // RE-19, edit-content-v2-apply-error: the errors of the pages come by Failed (onRunNotAccepted) - the dialog of
    // the errors, never the result dialog; Back to Editing closes it, shows the page of the first error and selects
    // its object
    using H = ContentToolHelper;
    pdftest::AppToolShellFixture fixture;
    QVERIFY(fixture.window);
    ContentToolOpened opened = H::open(fixture, H::createDocument(3), QStringLiteral("error.pdf"));
    QVERIFY(opened.isValid());
    QVERIFY(H::showPage(opened.context, 1));
    const std::vector<pdf::PDFContentNodeReference> paths = H::getPaths(opened.context);
    QCOMPARE(paths.size(), size_t(2));
    QVERIFY(H::showPage(opened.context, 0));

    QJsonArray errors;
    errors.append(QJsonObject({ { QStringLiteral("id"), pdf::PDFContentJson::writeNodeId(1, { paths.back().node }) }, { QStringLiteral("text"), QStringLiteral("A fallback font cannot be made.") } }));
    errors.append(QJsonObject({ { QStringLiteral("text"), QStringLiteral("The image file is no longer available.") } }));
    QJsonArray pages;
    pages.append(QJsonObject({ { QStringLiteral("page"), 2 }, { QStringLiteral("errors"), errors } }));
    opened.gui->onRunNotAccepted(H::createPageErrors(pages));

    pdf::PDFMessageBox* dialog = opened.gui->getApplyErrorDialog();
    QVERIFY(dialog && dialog->isVisible());
    QVERIFY(!opened.shell->getResultDialog());
    QVERIFY(H::hasLabel(dialog, QStringLiteral("Some content could not be written")));
    QVERIFY(H::hasLabel(dialog, QStringLiteral("2 errors on page 2. Pages without errors are ready.")));
    QVERIFY(H::hasLabel(dialog, QStringLiteral("Page 2: A fallback font cannot be made.")));
    QVERIFY(H::hasLabel(dialog, QStringLiteral("Page 2: The image file is no longer available.")));
    QVERIFY(H::hasLabel(dialog, QStringLiteral("Skip Page 2 writes the other pages; page 2 stays as it is in the document and its edits are not written. "
                                               "Back to Editing keeps the tool open and selects the first object with an error.")));
    QPushButton* skip = dialog->findChild<QPushButton*>(QStringLiteral("editContentApplyErrorSkip"));
    QPushButton* back = dialog->findChild<QPushButton*>(QStringLiteral("editContentApplyErrorBack"));
    QVERIFY(skip && back);
    QCOMPARE(skip->text(), QStringLiteral("Skip Page 2"));
    QCOMPARE(back->text(), QStringLiteral("Back to Editing"));

    QTest::mouseClick(back, Qt::LeftButton);
    QCOMPARE(opened.context->getPageIndex(), pdf::PDFInteger(1));
    QTRY_COMPARE_WITH_TIMEOUT(opened.context->getSelection()->getNodes(), std::vector<pdf::PDFContentNodeReference>({ paths.back() }), H::timeout(15000));
    QTRY_VERIFY_WITH_TIMEOUT(!opened.gui->getApplyErrorDialog(), H::timeout(5000));
    QVERIFY(opened.gui->getSkippedPages().empty());
    QVERIFY(opened.shell && opened.shell->getState() == pdf::PDFDocumentToolShell::State::Editing);
    H::close(opened);
}

void AppToolContentTest::applyErrorSkipPage()
{
    // zadani_gui.md 4.21: Skip Page runs the tool again with skipPages - the page stays as it is; a run, whose only
    // changes were on the skipped page, has nothing to write (the notice says so)
    using H = ContentToolHelper;
    pdftest::AppToolShellFixture fixture;
    QVERIFY(fixture.window);
    ContentToolOpened opened = H::open(fixture, H::createDocument(2), QStringLiteral("skip.pdf"));
    QVERIFY(opened.isValid());
    QVERIFY(H::deleteFirstPath(opened.context));

    QJsonArray errors;
    errors.append(QJsonObject({ { QStringLiteral("text"), QStringLiteral("The page cannot be written.") } }));
    QJsonArray pages;
    pages.append(QJsonObject({ { QStringLiteral("page"), 1 }, { QStringLiteral("errors"), errors } }));
    opened.gui->onRunNotAccepted(H::createPageErrors(pages));
    pdf::PDFMessageBox* dialog = opened.gui->getApplyErrorDialog();
    QVERIFY(dialog);
    QVERIFY(H::hasLabel(dialog, QStringLiteral("1 error on page 1. Pages without errors are ready.")));

    QPointer<pdf::PDFDocumentToolShell> shell = opened.shell;
    QTest::mouseClick(dialog->findChild<QPushButton*>(QStringLiteral("editContentApplyErrorSkip")), Qt::LeftButton);
    QCOMPARE(opened.gui->getSkippedPages(), std::set<pdf::PDFInteger>({ 0 }));
    const QVariantMap parameters = opened.gui->getRunParameters();
    QCOMPARE(pdf::PDFDocumentToolPageSelection::fromVariant(parameters.value(QString::fromLatin1(pdf::PDFDocumentToolContent::PARAMETER_SKIP_PAGES)), nullptr)->pages, QStringLiteral("1"));

    // The run (queued) writes nothing: NothingToDo, the tool stays with its work
    QVERIFY(pdftest::AppToolShellFixture::waitFor([&opened]() { const std::optional<Notice> notice = H::currentNotice(opened.gui); return notice && notice->title == QStringLiteral("Nothing to apply"); }, H::timeout(30000)));
    QVERIFY(shell && shell->getState() == pdf::PDFDocumentToolShell::State::Editing);
    QVERIFY(!shell->getResultDialog());
    QVERIFY(opened.gui->hasUnsavedWork());
    QVERIFY(!opened.tab->getDocument()->isModified());

    // A new change of the model asks the pages again
    QVERIFY(H::deleteFirstPath(opened.context));
    QVERIFY(opened.gui->getSkippedPages().empty());
    H::close(opened);
}

void AppToolContentTest::applyErrorWithoutPage()
{
    // An error, which belongs to no page (page 0 of the data: the structure), cannot be skipped: Back to Editing only
    using H = ContentToolHelper;
    pdftest::AppToolShellFixture fixture;
    QVERIFY(fixture.window);
    ContentToolOpened opened = H::open(fixture, H::createDocument(1), QStringLiteral("nopage.pdf"));
    QVERIFY(opened.isValid());

    QJsonArray errors;
    errors.append(QJsonObject({ { QStringLiteral("text"), QStringLiteral("The structure cannot be written.") } }));
    QJsonArray pages;
    pages.append(QJsonObject({ { QStringLiteral("page"), 0 }, { QStringLiteral("errors"), errors } }));
    opened.gui->onRunNotAccepted(H::createPageErrors(pages));
    pdf::PDFMessageBox* dialog = opened.gui->getApplyErrorDialog();
    QVERIFY(dialog);
    QVERIFY(H::hasLabel(dialog, QStringLiteral("1 error of the document. Nothing was written.")));
    QVERIFY(H::hasLabel(dialog, QStringLiteral("The structure cannot be written.")));
    QVERIFY(!dialog->findChild<QPushButton*>(QStringLiteral("editContentApplyErrorSkip")));
    QVERIFY(dialog->findChild<QPushButton*>(QStringLiteral("editContentApplyErrorBack")));

    // At most MAXIMUM_LISTED_ERRORS rows, the rest counted
    QJsonArray many;
    for (int i = 0; i < GUI::MAXIMUM_LISTED_ERRORS + 5; ++i)
    {
        many.append(QJsonObject({ { QStringLiteral("text"), QStringLiteral("Error %1").arg(i) } }));
    }
    QJsonArray manyPages;
    manyPages.append(QJsonObject({ { QStringLiteral("page"), 1 }, { QStringLiteral("errors"), many } }));
    opened.gui->onRunNotAccepted(H::createPageErrors(manyPages));
    dialog = opened.gui->getApplyErrorDialog();
    QVERIFY(dialog);
    QVERIFY(H::hasLabel(dialog, QStringLiteral("Page 1: Error %1").arg(GUI::MAXIMUM_LISTED_ERRORS - 1)));
    QVERIFY(!H::getLabelTexts(dialog).contains(QStringLiteral("Page 1: Error %1").arg(GUI::MAXIMUM_LISTED_ERRORS)));
    QVERIFY(H::hasLabel(dialog, QStringLiteral("5 more errors")));
    H::close(opened);
}

void AppToolContentTest::staleDocument()
{
    // RE-15, zadani_gui.md 4.21: the error of the fingerprint ("The document changed in the meantime.") is the notice
    // of the stale document with what to do - no info bar of the shell
    using H = ContentToolHelper;
    pdftest::AppToolShellFixture fixture;
    QVERIFY(fixture.window);
    ContentToolOpened opened = H::open(fixture, H::createDocument(1), QStringLiteral("stale.pdf"));
    QVERIFY(opened.isValid());
    QVERIFY(H::deleteFirstPath(opened.context));

    opened.gui->onRunNotAccepted(pdf::PDFDocumentToolResult::createFailed(pdf::PDFDocumentToolContent::tr("The document changed in the meantime.")));
    const std::optional<Notice> notice = H::currentNotice(opened.gui);
    QVERIFY(notice);
    QCOMPARE(notice->id, QString::fromLatin1(NoticeIds::STALE_DOCUMENT));
    QCOMPARE(notice->severity, Notice::Severity::Error);
    QCOMPARE(notice->title, QStringLiteral("The document has changed"));
    QCOMPARE(notice->text, QStringLiteral("The edits belong to the previous version of the document and cannot be applied to it. Close the tool and open it again to edit the document as it is now."));

    // Another failure is an event notice with the message of the run
    opened.gui->onRunNotAccepted(pdf::PDFDocumentToolResult::createFailed(QStringLiteral("Something failed.")));
    QCOMPARE(H::currentNotice(opened.gui)->title, QStringLiteral("The changes could not be applied"));
    QCOMPARE(H::currentNotice(opened.gui)->text, QStringLiteral("Something failed."));
    H::close(opened);
}

void AppToolContentTest::cancelAsksWithWork()
{
    // zadani_gui.md 4.21, edit-content-v2-discard: Cancel asks only when there is work; Keep Editing keeps the tool,
    // Discard Changes closes it and the document stays as it is
    using H = ContentToolHelper;
    pdftest::AppToolShellFixture fixture;
    QVERIFY(fixture.window);
    ContentToolOpened opened = H::open(fixture, H::createDocument(2), QStringLiteral("cancel.pdf"));
    QVERIFY(opened.isValid());
    QVERIFY(!opened.gui->getCloseQuestion().isValid());

    QVERIFY(H::deleteFirstPath(opened.context));
    const pdf::PDFAppToolCloseQuestion question = opened.gui->getCloseQuestion();
    QCOMPARE(question.title, QStringLiteral("Discard the changes to the page content?"));
    QCOMPARE(question.text, QStringLiteral("The content of page 1 was changed. Nothing has been written to the document yet."));
    QCOMPARE(question.keepText, QStringLiteral("Keep Editing"));
    QCOMPARE(question.discardText, QStringLiteral("Discard Changes"));

    QPointer<pdf::PDFDocumentToolShell> shell = opened.shell;
    QTest::mouseClick(shell->getToolbar()->getCancelButton(), Qt::LeftButton);
    pdf::PDFMessageBox* dialog = opened.gui->getGenericCloseQuestion();
    QVERIFY(dialog && dialog->isVisible());
    QTest::mouseClick(dialog->findChild<QPushButton*>(QStringLiteral("toolCloseKeep")), Qt::LeftButton);
    pdftest::AppToolShellFixture::settle();
    QVERIFY(shell && shell->getState() == pdf::PDFDocumentToolShell::State::Editing);

    QTest::mouseClick(shell->getToolbar()->getCancelButton(), Qt::LeftButton);
    dialog = opened.gui->getGenericCloseQuestion();
    QVERIFY(dialog);
    QTest::mouseClick(dialog->findChild<QPushButton*>(QStringLiteral("toolCloseDiscard")), Qt::LeftButton);
    QVERIFY(pdftest::AppToolShellFixture::waitFor([&shell]() { return !shell; }));
    QVERIFY(!opened.tab->getDocument()->isModified());
}

void AppToolContentTest::saveQuestionAnswers()
{
    // KE.3, edit-content-v2-save-prompt: Save with work asks; Cancel saves nothing; Save Without the Changes saves the
    // document as it is (nothing to save here) and the tool stays; Apply and Save runs the tool with the result dialog
    // and saves the kept result
    using H = ContentToolHelper;
    pdftest::AppToolShellFixture fixture;
    QVERIFY(fixture.window);
    ContentToolOpened opened = H::open(fixture, H::createDocument(1), QStringLiteral("save.pdf"));
    QVERIFY(opened.isValid());
    QVERIFY(!opened.gui->getSaveQuestion().title.isEmpty());
    QVERIFY(H::deleteFirstPath(opened.context));

    const pdf::PDFAppToolSaveQuestion question = opened.gui->getSaveQuestion();
    QCOMPARE(question.title, QStringLiteral("Apply the content changes before saving?"));
    QCOMPARE(question.text, QStringLiteral("The document is being saved while Edit Content is open. The changes on page 1 are not part of the document until they are applied."));
    QCOMPARE(question.cancelText, QStringLiteral("Cancel"));
    QCOMPARE(question.saveText, QStringLiteral("Save Without the Changes"));
    QCOMPARE(question.applyText, QStringLiteral("Apply and Save"));

    QPointer<pdf::PDFDocumentToolShell> shell = opened.shell;
    const QString path = opened.tab->getDocument()->getFilePath();
    const QDateTime modified = QFileInfo(path).lastModified();
    for (const char* answer : { "toolSaveCancel", "toolSaveWithoutChanges" })
    {
        QVERIFY(opened.tab->executeCommand(pdf::PDFAppCommands::save()));
        pdftest::AppToolShellFixture::settle();
        pdf::PDFMessageBox* dialog = shell->getSaveQuestion();
        QVERIFY2(dialog && dialog->isVisible(), answer);
        QTest::mouseClick(dialog->findChild<QPushButton*>(QString::fromLatin1(answer)), Qt::LeftButton);
        pdftest::AppToolShellFixture::settle();
        QVERIFY2(shell && shell->getState() == pdf::PDFDocumentToolShell::State::Editing, answer);
        QVERIFY2(opened.gui->hasUnsavedWork(), answer);
        QCOMPARE(QFileInfo(path).lastModified(), modified);
    }

    QVERIFY(opened.tab->executeCommand(pdf::PDFAppCommands::save()));
    pdftest::AppToolShellFixture::settle();
    QTest::mouseClick(shell->getSaveQuestion()->findChild<QPushButton*>(QStringLiteral("toolSaveApply")), Qt::LeftButton);
    QVERIFY(pdftest::AppToolShellFixture::waitFor([&shell]() { return !shell || shell->getResultDialog(); }, H::timeout(30000)));
    QVERIFY(shell && shell->getResultDialog());
    QTest::mouseClick(shell->getResultDialog()->getConfirmButton(), Qt::LeftButton);
    QVERIFY(pdftest::AppToolShellFixture::waitFor([&opened]() { return opened.tab->getDocument() && !opened.tab->getDocument()->isModified() && !opened.shell; }, H::timeout(30000)));

    // The saved file has the changed page: one rectangle
    pdf::PDFDocumentReader reader(nullptr, [](bool* ok) { *ok = false; return QString(); }, true, false);
    pdf::PDFDocument saved = reader.readFromFile(path);
    QCOMPARE(reader.getReadingResult(), pdf::PDFDocumentReader::Result::OK);
    QCOMPARE(H::countPaths(pdf::PDFDocumentPointer(new pdf::PDFDocument(std::move(saved))), 0), 1);
}

void AppToolContentTest::clearPage()
{
    // zadani_gui.md 4.21, edit-content-v2-clear-confirm: Clear Page asks "Clear page 1?" with the objects it removes;
    // Cancel changes nothing, Clear Page removes every object (one undo step)
    using H = ContentToolHelper;
    pdftest::AppToolShellFixture fixture;
    QVERIFY(fixture.window);
    ContentToolOpened opened = H::open(fixture, H::createDocument(1), QStringLiteral("clear.pdf"));
    QVERIFY(opened.isValid());

    QAbstractButton* button = opened.gui->getActionButton(QString::fromLatin1(pdf::PDFAppEditContentActionIds::CLEAR_PAGE));
    QVERIFY(button && button->isEnabled());
    QTest::mouseClick(button, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(opened.gui->getClearPageQuestion() != nullptr, H::timeout(5000));
    pdf::PDFMessageBox* question = opened.gui->getClearPageQuestion();
    QVERIFY(H::hasLabel(question, QStringLiteral("Clear page 1?")));
    QVERIFY(H::hasLabel(question, QStringLiteral("Every object of the page is removed – the 2 objects of the original content. Layers and tags stay in the document. Undo brings the content back while the tool is open.")));
    QPushButton* cancel = question->findChild<QPushButton*>(QStringLiteral("editContentClearCancel"));
    QPushButton* confirm = question->findChild<QPushButton*>(QStringLiteral("editContentClearConfirm"));
    QVERIFY(cancel && confirm);
    QCOMPARE(confirm->text(), QStringLiteral("Clear Page"));

    QTest::mouseClick(cancel, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(!opened.gui->getClearPageQuestion(), H::timeout(5000));
    QCOMPARE(H::getPaths(opened.context).size(), size_t(2));
    QVERIFY(!opened.gui->hasUnsavedWork());

    // A copy of one rectangle is a new object: the text names both kinds
    const std::vector<pdf::PDFContentNodeReference> paths = H::getPaths(opened.context);
    QVERIFY(opened.context->execute(pdf::PDFContentCommandFactory::createCopy({ paths.front() }, H::node(opened.context->getPageKey(), opened.context->getContainer()->getRoot()->getId().value), -1, QTransform::fromTranslate(5, 5))));
    QVERIFY(opened.gui->openClearPageQuestion());
    QVERIFY(H::hasLabel(opened.gui->getClearPageQuestion(), QStringLiteral("Every object of the page is removed – the 1 new object and the 2 objects of the original content. Layers and tags stay in the document. Undo brings the content back while the tool is open.")));
    QTest::mouseClick(opened.gui->getClearPageQuestion()->findChild<QPushButton*>(QStringLiteral("editContentClearConfirm")), Qt::LeftButton);
    QVERIFY(H::getPaths(opened.context).empty());
    QCOMPARE(opened.context->getSession()->getUndoStack()->getCount(), size_t(2));
    QVERIFY(!button->isEnabled());
    opened.shell->getUndoTarget()->undo();
    QCOMPARE(H::getPaths(opened.context).size(), size_t(3));
    H::close(opened);
}

void AppToolContentTest::pageNotices()
{
    // The notices of the states of a page (zadani_gui.md 4.15, 4.27; the frames -scanned, -complex-page; KE.20): a
    // scan, a complex page (QE.10: 20 000 objects), a page only for reading, a page with /UserUnit; the status text
    using H = ContentToolHelper;
    pdftest::AppToolShellFixture fixture;
    QVERIFY(fixture.window);

    {
        ContentToolOpened opened = H::open(fixture, H::createScannedDocument(), QStringLiteral("scan.pdf"));
        QVERIFY(opened.isValid());
        const std::optional<Notice> notice = H::currentNotice(opened.gui);
        QVERIFY(notice);
        QCOMPARE(notice->id, QString::fromLatin1(NoticeIds::SCANNED_PAGE));
        QCOMPARE(notice->severity, Notice::Severity::Information);
        QCOMPARE(notice->title, QStringLiteral("Scanned page"));
        QCOMPARE(notice->text, QStringLiteral("This page is a single image. New content can be added on top of it, but the scanned text cannot be edited. Recognize Text (OCR) adds searchable text without changing the image."));
        if (!opened.context->getToolController()->getActiveTool())
        {
            QCOMPARE(opened.gui->getStatusText(), QStringLiteral("Page 1 is a scanned image · add new content on top of it"));
        }
        QVERIFY(opened.gui->getNoticeWidget() && opened.gui->getNoticeWidget()->getTitle() == QStringLiteral("Scanned page"));
        H::close(opened);
    }

    {
        ContentToolOpened opened = H::open(fixture, H::createManyObjectsDocument(int(GUI::COMPLEX_PAGE_OBJECT_COUNT)), QStringLiteral("complex.pdf"));
        QVERIFY(opened.isValid());
        const std::optional<Notice> notice = H::currentNotice(opened.gui);
        QVERIFY(notice);
        QCOMPARE(notice->id, QString::fromLatin1(NoticeIds::COMPLEX_PAGE));
        QCOMPARE(notice->severity, Notice::Severity::Warning);
        QCOMPARE(notice->title, QStringLiteral("This page is complex"));
        QCOMPARE(notice->text, QStringLiteral("20,000 objects. Everything can be edited as usual; moving many objects at once and snapping may respond more slowly."));
        H::close(opened);
    }

    {
        // One object less is no complex page
        ContentToolOpened opened = H::open(fixture, H::createManyObjectsDocument(int(GUI::COMPLEX_PAGE_OBJECT_COUNT) - 1), QStringLiteral("almost.pdf"));
        QVERIFY(opened.isValid());
        QVERIFY(!H::currentNotice(opened.gui));
        H::close(opened);
    }

    {
        ContentToolOpened opened = H::open(fixture, H::createBrokenDocument(), QStringLiteral("broken.pdf"));
        QVERIFY(opened.isValid());
        QVERIFY(opened.context->getContainer()->isReadOnly());
        const std::optional<Notice> notice = H::currentNotice(opened.gui);
        QVERIFY(notice);
        QCOMPARE(notice->id, QString::fromLatin1(NoticeIds::READ_ONLY_PAGE));
        QCOMPARE(notice->title, QStringLiteral("This page cannot be edited"));
        QVERIFY(!opened.gui->getActionButton(QString::fromLatin1(pdf::PDFAppEditContentActionIds::CLEAR_PAGE))->isEnabled());
        H::close(opened);
    }

    {
        // KE.20: the pages 2 and 3 have /UserUnit 2, the first page none - the notice comes and goes with the page
        ContentToolOpened opened = H::open(fixture, H::createDocument(3, 2.0), QStringLiteral("unit.pdf"));
        QVERIFY(opened.isValid());
        QVERIFY(!H::currentNotice(opened.gui));
        QVERIFY(H::showPage(opened.context, 1));
        const std::optional<Notice> notice = H::currentNotice(opened.gui);
        QVERIFY(notice);
        QCOMPARE(notice->id, QString::fromLatin1(NoticeIds::USER_UNIT));
        QCOMPARE(notice->text, QStringLiteral("The page sets one unit to 2 points (UserUnit). The editor and its rulers work in the points of the page without it, as this application shows the page."));

        // Closed by the user, it stays closed while its page is shown; the next page with the state enters it again
        opened.gui->getNotices()->closeCurrent();
        QVERIFY(!H::currentNotice(opened.gui));
        opened.context->showNotice(*notice);
        QVERIFY(!H::currentNotice(opened.gui));
        QVERIFY(H::showPage(opened.context, 2));
        QVERIFY(H::currentNotice(opened.gui) && H::currentNotice(opened.gui)->id == QLatin1String(NoticeIds::USER_UNIT));
        QVERIFY(H::showPage(opened.context, 0));
        QVERIFY(!H::currentNotice(opened.gui));
        H::close(opened);
    }
}

void AppToolContentTest::conformanceNotice()
{
    // QE.2, notes 8.3: a document, which declares PDF/A or PDF/UA, says at the opening that Apply Changes removes the
    // declaration; closed, it does not come back in the session (also not after a change of the language)
    using H = ContentToolHelper;
    pdftest::AppToolShellFixture fixture;
    QVERIFY(fixture.window);

    struct Case
    {
        bool isPdfA;
        bool isPdfUA;
        QString title;
        QString text;
    };
    const std::vector<Case> cases = {
        { true, false, QStringLiteral("This document declares PDF/A"), QStringLiteral("Apply Changes removes the PDF/A declaration, because edited content is not checked against the standard.") },
        { false, true, QStringLiteral("This document declares PDF/UA"), QStringLiteral("Apply Changes removes the PDF/UA declaration, because edited content is not checked against the standard.") },
        { true, true, QStringLiteral("This document declares PDF/A and PDF/UA"), QStringLiteral("Apply Changes removes both declarations, because edited content is not checked against the standards.") },
    };
    int index = 0;
    for (const Case& item : cases)
    {
        ContentToolOpened opened = H::open(fixture, H::createConformanceDocument(item.isPdfA, item.isPdfUA), QStringLiteral("conformance%1.pdf").arg(++index));
        QVERIFY(opened.isValid());
        const std::optional<Notice> notice = H::currentNotice(opened.gui);
        QVERIFY(notice);
        QCOMPARE(notice->id, QString::fromLatin1(NoticeIds::CONFORMANCE));
        QVERIFY(notice->isOncePerSession);
        QCOMPARE(notice->title, item.title);
        QCOMPARE(notice->text, item.text);

        opened.gui->getNotices()->closeCurrent();
        ContentToolTranslator translator;
        QCoreApplication::installTranslator(&translator);
        pdftest::AppToolShellFixture::settle();
        QCoreApplication::removeTranslator(&translator);
        pdftest::AppToolShellFixture::settle();
        QVERIFY(!H::currentNotice(opened.gui));
        H::close(opened);
    }

    // A document without a declaration: no notice
    ContentToolOpened plain = H::open(fixture, H::createDocument(1), QStringLiteral("plain.pdf"));
    QVERIFY(plain.isValid());
    QVERIFY(!H::currentNotice(plain.gui));
    H::close(plain);
}

void AppToolContentTest::commandRefusedNotice()
{
    // A refused command is an event notice with its reason; it leaves with the next change of the selection
    using H = ContentToolHelper;
    pdftest::AppToolShellFixture fixture;
    QVERIFY(fixture.window);
    ContentToolOpened opened = H::open(fixture, H::createDocument(1), QStringLiteral("refused.pdf"));
    QVERIFY(opened.isValid());

    QVERIFY(!opened.context->execute(pdf::PDFContentCommandFactory::createDelete({ H::node(opened.context->getPageKey(), 999999) })));
    const std::optional<Notice> notice = H::currentNotice(opened.gui);
    QVERIFY(notice);
    QCOMPARE(notice->id, QString::fromLatin1(NoticeIds::COMMAND_REFUSED));
    QCOMPARE(notice->kind, Notice::Kind::Event);
    QCOMPARE(notice->title, QStringLiteral("The change was not made"));
    QCOMPARE(notice->text, opened.context->getLastRefusalText());
    QVERIFY(!notice->text.isEmpty());

    opened.context->getSelection()->setNodes(H::getPaths(opened.context));
    QVERIFY(!H::currentNotice(opened.gui));
    H::close(opened);
}

void AppToolContentTest::languageChange()
{
    // Order (3) of zadani_gui.md 4.14, R3-9: retranslate commits the pending values of the cards before the shell
    // builds the actions and the panels again; the notices survive with the texts of the new language
    using H = ContentToolHelper;
    pdftest::AppToolShellFixture fixture;
    QVERIFY(fixture.window);
    ContentToolOpened opened = H::open(fixture, H::createScannedDocument(), QStringLiteral("language.pdf"));
    QVERIFY(opened.isValid());
    QVERIFY(H::currentNotice(opened.gui));

    QSignalSpy commit(opened.context.data(), &Context::pendingEditsCommitRequested);
    QPointer<QWidget> actions = opened.gui->getToolActionsWidget();
    QPointer<QWidget> notice = opened.gui->getNoticeWidget();
    ContentToolTranslator translator;
    QCoreApplication::installTranslator(&translator);
    pdftest::AppToolShellFixture::settle();
    QCoreApplication::removeTranslator(&translator);
    pdftest::AppToolShellFixture::settle();
    QVERIFY(commit.count() >= 1);
    QVERIFY(opened.gui->getToolActionsWidget() && opened.gui->getToolActionsWidget() != actions);
    QVERIFY(notice && notice == opened.gui->getNoticeWidget());
    QCOMPARE(H::currentNotice(opened.gui)->id, QString::fromLatin1(NoticeIds::SCANNED_PAGE));
    QCOMPARE(notice->property("visible").toBool(), true);
    H::close(opened);
}

void AppToolContentTest::leakPixels()
{
    // zadani_overeni.md 8 (UnitTestsAppToolContent): no pixel of the leak color in the toolbar of the tool, the tab
    // strip and the notice, in both schemes
    using H = ContentToolHelper;
    pdftest::AppToolShellFixture fixture;
    QVERIFY(fixture.window);
    ContentToolOpened opened = H::open(fixture, H::createScannedDocument(), QStringLiteral("leak.pdf"));
    QVERIFY(opened.isValid());
    QVERIFY(opened.gui->getNoticeWidget() && opened.gui->getNoticeWidget()->isVisible());
    for (pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
    {
        pdf::PDFThemeManager::getInstance()->setScheme(scheme);
        pdftest::AppToolShellFixture::settle();
        for (QWidget* widget : { opened.gui->getToolActionsWidget(), opened.gui->getLeftPanelWidget(), static_cast<QWidget*>(opened.gui->getNoticeWidget()) })
        {
            QVERIFY(widget);
            for (qreal ratio : { 1.0, 2.0 })
            {
                const QImage image = pdftest::AppToolShellFixture::render(widget, ratio);
                QVERIFY2(pdftest::AppToolShellFixture::countLeakPixels(image) == 0, qPrintable(widget->objectName()));
            }
        }
    }
    H::close(opened);
}

// ------------------------------------------------------------------ rows over the real parts of the other packages

void AppToolContentTest::toolButtonsFromRegistry()
{
    // zadani_gui.md 4.20: a tool button per tool entry of the toolbar; the active tool is checked; the tooltip has the
    // title, the key and the description; the tools are disabled in Preview
    using H = ContentToolHelper;
    if (!H::isSelectRegistered())
    {
        QSKIP("needs P11 (the tool Select of the registry)");
    }
    pdftest::AppToolShellFixture fixture;
    QVERIFY(fixture.window);
    ContentToolOpened opened = H::open(fixture, H::createDocument(1), QStringLiteral("tools.pdf"));
    QVERIFY(opened.isValid());
    const QString select = QString::fromLatin1(pdf::PDFAppEditContentToolIds::SELECT);
    QAbstractButton* button = opened.gui->getToolButton(select);
    QVERIFY(button);
    QVERIFY(button->isChecked() && button->isEnabled());
    QVERIFY(button->toolTip().startsWith(QStringLiteral("Select (V)")));
    QCOMPARE(button->accessibleName(), QStringLiteral("Select"));
    QCOMPARE(opened.context->getToolController()->getActiveToolId(), select);

    opened.context->setPreviewActive(true);
    QVERIFY(!button->isEnabled());
    opened.context->setPreviewActive(false);
    QVERIFY(button->isEnabled());
    H::close(opened);
}

void AppToolContentTest::keyboardCard()
{
    // KE.22, edit-content-v2-keyboard: Ctrl+/ shows the card Keyboard with the rows of the keys; Close hides it
    using H = ContentToolHelper;
    if (!H::isCardsReal())
    {
        QSKIP("needs P13 (the card stack and the card base)");
    }
    pdftest::AppToolShellFixture fixture;
    QVERIFY(fixture.window);
    ContentToolOpened opened = H::open(fixture, H::createDocument(1), QStringLiteral("card.pdf"));
    QVERIFY(opened.isValid());
    const QString cardId = QString::fromLatin1(pdf::PDFAppEditContentCardIds::KEYBOARD);
    QVERIFY(opened.context->triggerAction(QString::fromLatin1(pdf::PDFAppEditContentActionIds::VIEW_KEYBOARD)));
    QTRY_VERIFY_WITH_TIMEOUT(opened.gui->getCardStack()->getCard(cardId) != nullptr, H::timeout(5000));
    pdf::PDFAppEditContentKeyboardCard* card = qobject_cast<pdf::PDFAppEditContentKeyboardCard*>(opened.gui->getCardStack()->getCard(cardId));
    QVERIFY(card && card->getList());
    const std::vector<std::pair<QString, QString>> rows = pdf::PDFAppEditContentKeyboard::getKeyRows(Registry::getInstance());
    QCOMPARE(card->getList()->getRowCount(), int(rows.size()));
    for (int i = 0; i < int(rows.size()); ++i)
    {
        QCOMPARE(card->getList()->getKey(i), rows[size_t(i)].first);
        QCOMPARE(card->getList()->getValue(i), rows[size_t(i)].second);
    }

    QTest::mouseClick(card->findChild<QAbstractButton*>(QStringLiteral("editContentKeyboardClose")), Qt::LeftButton);
    QVERIFY(!opened.context->isCardRequested(cardId));
    QTRY_VERIFY_WITH_TIMEOUT(!opened.gui->getCardStack()->getCard(cardId), H::timeout(5000));
    H::close(opened);
}

void AppToolContentTest::viewPanel()
{
    // zadani_gui.md 4.20, edit-content-v2-menus-view: View opens the panel filled by the menus of P12 with the rows of
    // the menu View, among them Keyboard Shortcuts of P14
    using H = ContentToolHelper;
    if (!H::isMenusReal())
    {
        QSKIP("needs P12 (PDFAppEditContentMenus::fillPanel)");
    }
    pdftest::AppToolShellFixture fixture;
    QVERIFY(fixture.window);
    ContentToolOpened opened = H::open(fixture, H::createDocument(1), QStringLiteral("view.pdf"));
    QVERIFY(opened.isValid());
    QAbstractButton* view = opened.gui->getViewButton();
    QVERIFY(view);
    QTest::mouseClick(view, Qt::LeftButton);
    pdftest::AppToolShellFixture::settle();
    pdf::PDFAppToolPanelPopup* panel = opened.shell->getToolbar()->getOpenPanel();
    QVERIFY(panel);
    bool hasKeyboard = false;
    for (const QPointer<pdf::PDFTileRow>& row : panel->getRows())
    {
        hasKeyboard = hasKeyboard || (row && row->getTitle() == QStringLiteral("Keyboard Shortcuts"));
    }
    QVERIFY(hasKeyboard);
    panel->close();
    H::close(opened);
}

QTEST_MAIN(AppToolContentTest)

#include "tst_apptoolcontenttest.moc"
