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

// Edit Content, stage E1: the real code of the skeleton (GUI_overhaul/phase2_briefs/pe1_skeleton_notes.md) - the
// registry of the parts, the table of the keys, the plain state of the selection, the options, the context over a
// hand-made document, the registration behind the developer switch and the opening of the tool over the stubs.
// Expected values from the headers (pdfappeditcontent*.h) and the plan (zadani_gui.md 4.14 - 4.20, zadani.md 11.2),
// never from a run.

#include "appdocumentfixture.h"
#include "apptoolshellfixture.h"
#include "contenttestdocuments.h"

#include "pdfappcommand.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfappeditcontentcontext.h"
#include "pdfappeditcontentpanels_objects.h"
#include "pdfappeditcontentpreview.h"
#include "pdfappeditcontentregistry.h"
#include "pdfappeditcontentscene.h"
#include "pdfappeditcontentselection.h"
#include "pdfappeditcontenttools.h"
#include "pdfappeditcontentundo.h"
#include "pdfappeditcontentview.h"
#include "pdfapphelpcatalog.h"
#include "pdfapptoolgui_content.h"
#include "pdfapptooldocumentpage.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptooltoolbar.h"
#include "pdfdeferreddeletion.h"
#include "pdfdocumenttoolguiregistry.h"
#include "pdfdocumenttoolshell.h"
#include "pdfdatabase.h"
#include "pdfdatabasenotifier.h"
#include "pdfsettingdefinitions.h"
#include "pdftoolparameterrepository.h"

#include "pdfcontentdisplay.h"
#include "pdfcontentundostack.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumenttool_content.h"
#include "pdfdocumenttoolregistry.h"

#include <QAbstractButton>
#include <QAction>
#include <QLabel>
#include <QPushButton>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>

#include <map>
#include <memory>
#include <optional>

using Registry = pdf::PDFAppEditContentRegistry;
using ToolEntry = pdf::PDFAppEditContentToolEntry;
using ActionEntry = pdf::PDFAppEditContentActionEntry;
using KeyScope = pdf::PDFAppEditContentKeyScope;
using KeyTable = pdf::PDFAppEditContentKeyTable;
using Context = pdf::PDFAppEditContentContext;

/// The translator of the parameter definitions of the test
class ContentSkeletonText
{
    Q_DECLARE_TR_FUNCTIONS(ContentSkeletonText)
};

/// A tool of a test registry: it only exists
class ContentSkeletonTool : public pdf::PDFAppEditContentTool
{
public:
    using pdf::PDFAppEditContentTool::PDFAppEditContentTool;
};

/// A tool entry of a test registry
class ContentSkeletonToolEntry : public ToolEntry
{
public:
    ContentSkeletonToolEntry(QString id, Place place, int order, QKeySequence key = QKeySequence(), QString title = QString(),
                             std::optional<pdf::PDFContentNodeKind> editorKind = std::nullopt, bool isEditorOfAll = false) :
        m_id(std::move(id)), m_place(place), m_order(order), m_key(std::move(key)), m_title(std::move(title)), m_editorKind(editorKind), m_isEditorOfAll(isEditorOfAll)
    {

    }

    virtual QString getId() const override { return m_id; }
    virtual Place getPlace() const override { return m_place; }
    virtual int getOrder() const override { return m_order; }
    virtual QString getTitle() const override { return m_title.isEmpty() ? m_id : m_title; }
    virtual QKeySequence getKey() const override { return m_key; }

    virtual bool isEditorOf(const pdf::PDFContentNode& node, const pdf::PDFContentContainer& container) const override
    {
        Q_UNUSED(container);
        return m_isEditorOfAll || (m_editorKind && node.getKind() == *m_editorKind);
    }

    virtual std::unique_ptr<pdf::PDFAppEditContentTool> createTool(pdf::PDFAppEditContentContext* context) const override
    {
        return std::make_unique<ContentSkeletonTool>(context);
    }

private:
    QString m_id;
    Place m_place;
    int m_order;
    QKeySequence m_key;
    QString m_title;
    std::optional<pdf::PDFContentNodeKind> m_editorKind;
    bool m_isEditorOfAll;
};

/// An item entry of a test registry
class ContentSkeletonItemEntry : public pdf::PDFAppEditContentItemEntry
{
public:
    ContentSkeletonItemEntry(QString id, int priority, bool isAccepted) : m_id(std::move(id)), m_priority(priority), m_isAccepted(isAccepted) { }

    virtual QString getId() const override { return m_id; }
    virtual int getPriority() const override { return m_priority; }
    virtual bool isItemOf(const pdf::PDFContentNode&, const pdf::PDFContentContainer&) const override { return m_isAccepted; }
    virtual pdf::PDFAppEditContentItem* createItem(const pdf::PDFAppEditContentItemArguments&) const override { return nullptr; }

private:
    QString m_id;
    int m_priority;
    bool m_isAccepted;
};

/// A tab entry of a test registry; its state object is named by the identifier of the tab
class ContentSkeletonTabEntry : public pdf::PDFAppEditContentTabEntry
{
public:
    ContentSkeletonTabEntry(QString id, int order, bool hasState) : m_id(std::move(id)), m_order(order), m_hasState(hasState) { }

    virtual QString getId() const override { return m_id; }
    virtual int getOrder() const override { return m_order; }
    virtual QString getTitle() const override { return m_id; }
    virtual QString getIconName() const override { return QString(); }

    virtual QObject* createState(pdf::PDFAppEditContentContext* context, QObject* parent) const override
    {
        Q_UNUSED(context);
        if (!m_hasState)
        {
            return nullptr;
        }
        QObject* state = new QObject(parent);
        state->setObjectName(m_id);
        return state;
    }

    virtual QWidget* createPanel(pdf::PDFAppEditContentContext*, QObject*, QWidget* parent) const override { return new QWidget(parent); }

private:
    QString m_id;
    int m_order;
    bool m_hasState;
};

/// A card entry of a test registry (never shown); \p tag tells two entries of one identifier apart
class ContentSkeletonCardEntry : public pdf::PDFAppEditContentCardEntry
{
public:
    explicit ContentSkeletonCardEntry(QString id, QString tag = QString()) : m_id(std::move(id)), tag(std::move(tag)) { }

    virtual QString getId() const override { return m_id; }
    virtual bool isShown(const pdf::PDFAppEditContentContext*) const override { return false; }
    virtual pdf::PDFAppEditContentCard* createCard(pdf::PDFAppEditContentContext*, QWidget*) const override { return nullptr; }

private:
    QString m_id;

public:
    QString tag;
};

/// What the action entries of a test registry did
class ContentSkeletonActionRecorder
{
public:
    QStringList log;            ///< "<id>" of trigger, "<id>:<choice>" of triggerChoice, in their order
    bool isEnabled = true;
    bool isVisible = true;
};

/// An action entry of a test registry
class ContentSkeletonActionEntry : public ActionEntry
{
public:
    ContentSkeletonActionEntry(QString id, std::vector<Key> keys = std::vector<Key>(), std::vector<Placement> placements = std::vector<Placement>(),
                               ContentSkeletonActionRecorder* recorder = nullptr, QString title = QString()) :
        m_id(std::move(id)), m_keys(std::move(keys)), m_placements(std::move(placements)), m_recorder(recorder), m_title(std::move(title))
    {

    }

    virtual QString getId() const override { return m_id; }
    virtual QString getTitle(const pdf::PDFAppEditContentContext*) const override { return m_title.isEmpty() ? m_id : m_title; }
    virtual std::vector<Key> getKeys() const override { return m_keys; }
    virtual std::vector<Placement> getPlacements() const override { return m_placements; }
    virtual bool isVisible(const pdf::PDFAppEditContentContext*) const override { return !m_recorder || m_recorder->isVisible; }
    virtual bool isEnabled(const pdf::PDFAppEditContentContext*) const override { return !m_recorder || m_recorder->isEnabled; }

    virtual void trigger(pdf::PDFAppEditContentContext*) const override
    {
        if (m_recorder)
        {
            m_recorder->log << m_id;
        }
    }

    virtual void triggerChoice(pdf::PDFAppEditContentContext*, const QString& choiceId) const override
    {
        if (m_recorder)
        {
            m_recorder->log << m_id + QLatin1Char(':') + choiceId;
        }
    }

private:
    QString m_id;
    std::vector<Key> m_keys;
    std::vector<Placement> m_placements;
    ContentSkeletonActionRecorder* m_recorder;
    QString m_title;
};

/// What the entries of the routed application commands of a test registry did
class ContentSkeletonCommandRecorder
{
public:
    QStringList log;            ///< "<id>" of each trigger
    bool isVisible = true;
};

/// An action entry, which answers commands of the application (pe1_skeleton_notes.md 11, R3-1); its isEnabled is the
/// default of the registry (PDFAppEditContentContext::isEditingAllowed)
class ContentSkeletonCommandEntry : public ActionEntry
{
public:
    ContentSkeletonCommandEntry(QString id, QStringList commandIds, ContentSkeletonCommandRecorder* recorder = nullptr, std::vector<Key> keys = std::vector<Key>()) :
        m_id(std::move(id)), m_commandIds(std::move(commandIds)), m_recorder(recorder), m_keys(std::move(keys))
    {

    }

    virtual QString getId() const override { return m_id; }
    virtual QString getTitle(const pdf::PDFAppEditContentContext*) const override { return m_id; }
    virtual std::vector<Key> getKeys() const override { return m_keys; }
    virtual QStringList getApplicationCommandIds() const override { return m_commandIds; }
    virtual bool isVisible(const pdf::PDFAppEditContentContext*) const override { return !m_recorder || m_recorder->isVisible; }

    virtual void trigger(pdf::PDFAppEditContentContext*) const override
    {
        if (m_recorder)
        {
            m_recorder->log << m_id;
        }
    }

private:
    QString m_id;
    QStringList m_commandIds;
    ContentSkeletonCommandRecorder* m_recorder;
    std::vector<Key> m_keys;
};

/// What the probes of the order of the creation and of the deletion of the children of a context saw
/// (R3-3, R3-8)
class ContentSkeletonLifeRecorder
{
public:
    QStringList log;
};

/// A tool, which records what of the context it finds in its constructor and in its destructor
class ContentSkeletonProbeTool : public pdf::PDFAppEditContentTool
{
public:
    ContentSkeletonProbeTool(Context* context, ContentSkeletonLifeRecorder* recorder) :
        pdf::PDFAppEditContentTool(context),
        m_recorder(recorder),
        m_session(context->getSession()),
        m_options(context->getOptions()),
        m_selection(context->getSelection())
    {
        // Everything documented "never nullptr" exists in the constructor of a tool
        recorder->log << QStringLiteral("tool: controller %1, own controller %2, preview %3, tab state %4")
                         .arg(yes(context->getToolController()), yes(getController() && getController() == context->getToolController()),
                              yes(context->getPreview()), yes(context->getTabState(QStringLiteral("probe"))));
    }

    virtual ~ContentSkeletonProbeTool() override
    {
        m_recorder->log << QStringLiteral("~tool: session %1, options %2, selection %3").arg(yes(m_session.data()), yes(m_options.data()), yes(m_selection.data()));
    }

    static QString yes(const void* pointer) { return pointer ? QStringLiteral("yes") : QStringLiteral("no"); }
    static QString yes(bool value) { return value ? QStringLiteral("yes") : QStringLiteral("no"); }

private:
    ContentSkeletonLifeRecorder* m_recorder;
    QPointer<pdf::PDFContentEditSession> m_session;
    QPointer<pdf::PDFAppEditContentOptions> m_options;
    QPointer<pdf::PDFAppEditContentSelection> m_selection;
};

/// The entry of the probe tool
class ContentSkeletonProbeToolEntry : public ToolEntry
{
public:
    explicit ContentSkeletonProbeToolEntry(ContentSkeletonLifeRecorder* recorder) : m_recorder(recorder) { }

    virtual QString getId() const override { return QStringLiteral("probe"); }
    virtual Place getPlace() const override { return Place::Mode; }
    virtual int getOrder() const override { return 100; }
    virtual QString getTitle() const override { return QStringLiteral("Probe"); }
    virtual std::unique_ptr<pdf::PDFAppEditContentTool> createTool(pdf::PDFAppEditContentContext* context) const override
    {
        return std::make_unique<ContentSkeletonProbeTool>(context, m_recorder);
    }

private:
    ContentSkeletonLifeRecorder* m_recorder;
};

/// The state of a tab, which records what of the context it still finds in its destructor
class ContentSkeletonProbeState : public QObject
{
public:
    ContentSkeletonProbeState(Context* context, ContentSkeletonLifeRecorder* recorder, QObject* parent) :
        QObject(parent),
        m_recorder(recorder),
        m_session(context->getSession()),
        m_selection(context->getSelection()),
        m_controller(context->getToolController()),
        m_preview(context->getPreview())
    {

    }

    virtual ~ContentSkeletonProbeState() override
    {
        using T = ContentSkeletonProbeTool;
        m_recorder->log << QStringLiteral("~state: session %1, selection %2, controller %3, preview %4")
                           .arg(T::yes(m_session.data()), T::yes(m_selection.data()), T::yes(m_controller.data()), T::yes(m_preview.data()));
    }

private:
    ContentSkeletonLifeRecorder* m_recorder;
    QPointer<pdf::PDFContentEditSession> m_session;
    QPointer<pdf::PDFAppEditContentSelection> m_selection;
    QPointer<pdf::PDFAppEditContentToolController> m_controller;
    QPointer<pdf::PDFAppEditContentPreview> m_preview;
};

/// The tab entry of the probe state
class ContentSkeletonProbeTabEntry : public pdf::PDFAppEditContentTabEntry
{
public:
    explicit ContentSkeletonProbeTabEntry(ContentSkeletonLifeRecorder* recorder) : m_recorder(recorder) { }

    virtual QString getId() const override { return QStringLiteral("probe"); }
    virtual int getOrder() const override { return 100; }
    virtual QString getTitle() const override { return QStringLiteral("Probe"); }
    virtual QString getIconName() const override { return QString(); }
    virtual QObject* createState(pdf::PDFAppEditContentContext* context, QObject* parent) const override { return new ContentSkeletonProbeState(context, m_recorder, parent); }
    virtual QWidget* createPanel(pdf::PDFAppEditContentContext*, QObject*, QWidget* parent) const override { return new QWidget(parent); }

private:
    ContentSkeletonLifeRecorder* m_recorder;
};

/// A command of a test, which records its applications (the order of requestExecute)
class ContentSkeletonRecordingCommand : public pdf::PDFContentCommand
{
public:
    ContentSkeletonRecordingCommand(QString name, QStringList* log) : m_name(std::move(name)), m_log(log) { }

    virtual bool apply(pdf::PDFContentEditSession*, QString*) override
    {
        *m_log << m_name;
        return true;
    }

    virtual void revert(pdf::PDFContentEditSession*) override { }
    virtual QString getText() const override { return m_name; }
    virtual pdf::PDFInteger getPageIndex() const override { return 0; }
    virtual QByteArray getType() const override { return QByteArrayLiteral("test"); }
    virtual QJsonObject toJson() const override { return QJsonObject(); }

private:
    QString m_name;
    QStringList* m_log;
};

/// Helpers of the test
class ContentSkeletonHelper
{
public:
    ContentSkeletonHelper() = delete;

    static constexpr const char* TOOL_ID = "edit.content";

    static ActionEntry::Key key(const QKeySequence& sequence, KeyScope scope)
    {
        ActionEntry::Key result;
        result.sequence = sequence;
        result.scope = scope;
        return result;
    }

    static ActionEntry::Placement placement(const char* menuId, int group, int order)
    {
        ActionEntry::Placement result;
        result.menuId = QString::fromLatin1(menuId);
        result.group = group;
        result.order = order;
        return result;
    }

    template<typename Entry>
    static QStringList ids(const std::vector<const Entry*>& entries)
    {
        QStringList result;
        for (const Entry* entry : entries)
        {
            result << entry->getId();
        }
        return result;
    }

    static pdf::PDFContentNodeReference node(const pdf::PDFContentContainerKey& container, quint64 id)
    {
        pdf::PDFContentNodeReference reference;
        reference.container = container;
        reference.node.value = id;
        return reference;
    }

    static std::shared_ptr<const pdf::PDFContentNode> createNode(pdf::PDFContentNodeKind kind)
    {
        pdf::PDFContentNode::Data data;
        data.kind = kind;
        data.id.value = 7;
        return std::make_shared<const pdf::PDFContentNode>(std::move(data), std::vector<pdf::PDFContentNodePointer>());
    }

    /// A document of \p pageCount pages [0 0 200 200], each with two filled rectangles (two Path nodes)
    static pdf::PDFDocumentPointer createDocument(int pageCount)
    {
        using D = pdftest::ContentTestDocuments;
        pdf::PDFDocumentBuilder builder;
        for (int i = 0; i < pageCount; ++i)
        {
            const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 200, 200));
            const pdf::PDFObject contents = D::addStreamObject(&builder, D::DictionaryEntries(), QByteArrayLiteral("10 10 50 50 re f 100 100 20 20 re f"));
            builder.mergeTo(page, D::createDictionaryObject({ { "Resources", D::createDictionaryObject({ }) }, { "Contents", contents } }));
        }
        return pdf::PDFDocumentPointer(new pdf::PDFDocument(builder.build()));
    }

    /// A page [0 0 200 200] for the geometry: a path in a group with a matrix, a path, a group of two paths, an
    /// inline image (1 x 1, gray) under a matrix, a use of the form /Fm1 (/BBox [0 0 40 40], /Matrix [2 0 0 2 0 0],
    /// one path "0 0 10 10 re f") under a matrix
    static pdf::PDFDocumentPointer createGeometryDocument()
    {
        using D = pdftest::ContentTestDocuments;
        const QByteArray content = QByteArrayLiteral("q 2 0 0 2 10 20 cm 0 0 10 5 re f Q\n"
                                                     "100 100 20 20 re f\n"
                                                     "q 30 30 10 10 re f 50 60 5 5 re f Q\n"
                                                     "q 20 0 0 10 150 150 cm BI /W 1 /H 1 /BPC 8 /CS /G ID x EI Q\n"
                                                     "q 1 0 0 1 50 0 cm /Fm1 Do Q\n");
        return pdf::PDFDocumentPointer(new pdf::PDFDocument(D::createDocument(content, [](pdf::PDFDocumentBuilder* builder)
        {
            const pdf::PDFObject form = D::addStreamObject(builder, { { "Type", pdf::PDFObject::createName("XObject") }, { "Subtype", pdf::PDFObject::createName("Form") },
                                                                      { "BBox", D::createNumberArrayObject({ 0, 0, 40, 40 }) }, { "Matrix", D::createNumberArrayObject({ 2, 0, 0, 2, 0, 0 }) },
                                                                      { "Resources", D::createDictionaryObject({ }) } },
                                                           QByteArrayLiteral("0 0 10 10 re f"));
            pdf::PDFDictionaryBuilder resources;
            resources.addEntry(pdf::PDFInplaceOrMemoryString("XObject"), D::createDictionaryObject({ { "Fm1", form } }));
            return resources;
        })));
    }

    /// The nodes of the kind in the container, in the order of the content (a walk of a tree of a test)
    static std::vector<pdf::PDFContentNodeReference> findNodes(const pdf::PDFContentContainerPointer& container, pdf::PDFContentNodeKind kind)
    {
        std::vector<pdf::PDFContentNodeReference> result;
        std::vector<const pdf::PDFContentNode*> stack;
        if (container && container->getRoot())
        {
            stack.push_back(container->getRoot().get());
        }
        while (!stack.empty())
        {
            const pdf::PDFContentNode* current = stack.back();
            stack.pop_back();
            if (current->getKind() == kind)
            {
                result.push_back(node(container->getKey(), current->getId().value));
            }
            const std::vector<pdf::PDFContentNodePointer>& children = current->getChildren();
            for (auto it = children.crbegin(); it != children.crend(); ++it)
            {
                stack.push_back(it->get());
            }
        }
        return result;
    }

    /// The rectangles are equal up to the rounding of the matrices
    static bool isSame(const QRectF& actual, const QRectF& expected)
    {
        const bool isSame = qAbs(actual.left() - expected.left()) < 1e-6 && qAbs(actual.top() - expected.top()) < 1e-6 &&
                            qAbs(actual.width() - expected.width()) < 1e-6 && qAbs(actual.height() - expected.height()) < 1e-6 && !actual.isNull();
        if (!isSame)
        {
            qWarning() << "rectangle" << actual << "expected" << expected;
        }
        return isSame;
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

    static int timeout(int milliseconds)
    {
        return milliseconds * pdftest::AppDocumentFixture::getTimeoutFactor();
    }

    /// Shows the page and waits for its container
    static bool showPage(Context* context, pdf::PDFInteger pageIndex)
    {
        QSignalSpy loaded(context, &Context::containerChanged);
        context->setPage(pageIndex);
        return (context->getContainer() || loaded.wait(timeout(15000))) && context->getContainer();
    }

    /// The deliberate end of a context of a test (retire, then the deferred deletion now)
    static void finish(QPointer<Context>& context)
    {
        if (context)
        {
            pdf::PDFDeferredDeletion::retire(context.data());
            pdf::PDFDeferredDeletion::flush();
        }
    }

    static bool setDeveloperMode(pdf::PDFAppContext* context, bool isOn)
    {
        if (context->getSettings().setValue(pdf::PDFSettingKeys::advancedDeveloperMode(), isOn).isError())
        {
            return false;
        }
        return pdftest::AppToolShellFixture::waitFor([isOn]() { return pdf::PDFAppToolGUIContent::isContentToolRegistered() == isOn; });
    }

    static bool isToolRegistered()
    {
        const QString toolId = QString::fromLatin1(TOOL_ID);
        return pdf::PDFDocumentToolRegistry::isImplemented(toolId) && pdf::PDFDocumentToolGUIRegistry::getFactory(toolId) != nullptr;
    }

    static bool isToolUnregistered()
    {
        const QString toolId = QString::fromLatin1(TOOL_ID);
        return !pdf::PDFDocumentToolRegistry::isImplemented(toolId) && !pdf::PDFDocumentToolGUIRegistry::getFactory(toolId);
    }

    static pdf::PDFAppDocumentTab* openReady(pdftest::AppToolShellFixture& fixture)
    {
        pdf::PDFAppDocumentTab* tab = fixture.openDocument(3);
        return tab && pdftest::AppToolShellFixture::waitFor([tab]() { return tab->getDocument() && tab->getDocument()->isReady(); }) ? tab : nullptr;
    }
};

class AppToolContentSkeletonTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    // registry
    void registryToolsByPlaceAndOrder();
    void registryRefusesOrReplacesIdentifier();
    void registryTabsByOrder();
    void registryCardsInTableOrder();
    void registryItemsByPriorityThenRegistration();
    void registryMenuActionsByGroupAndOrder();
    void registryActionByKeyAndScope();
    void registryEditorTool();

    // key table
    void keyTableRows();
    void keyTableProblem_data();
    void keyTableProblem();
    void keyTableKeyTwiceInScope();
    void keyTableAgainstApplication();
    void keyTableApplicationCommands();
    void keyTableCanvasKeys();

    // selection, options, geometry
    void selectionState();
    void optionsDefaultsAndSessionValues();
    void optionsModelParameterWins();
    void geometryBoundingBoxes();

    // context
    void contextPages();
    void contextEditingAllowed();
    void contextExecute();
    void contextRequestExecute();
    void contextMacroIsOneStep();
    void contextTriggerAction();
    void contextApplicationCommands();
    void contextCardsAndRequests();
    void contextChildren();
    void contextChildrenCreatedAndDeletedInOrder();
    void contextRetireDropsQueues();
    void contextChangeableSelection();
    void contextComplexPagePolicy();
    void contextReloadDocument();

    // developer switch, the tool over the stubs
    void developerSwitch();
    void developerSwitchFollowsStorage();
    void openingEndToEnd();
    void routedApplicationCommandsEndToEnd();
    void helpWithoutTopic();
    void statesFollowTheUndoStack();
    void optionsAreStoredParameters();
};

void AppToolContentSkeletonTest::initTestCase()
{
    qRegisterMetaType<pdf::PDFAppEditContentNoticeRequest>();
    qRegisterMetaType<pdf::PDFContentNodeReference>();
}

void AppToolContentSkeletonTest::registryToolsByPlaceAndOrder()
{
    // pdfappeditcontentregistry.h: tools sorted by their place (Toolbar, ShapeMenu, Mode) and their order inside of it
    using E = ContentSkeletonToolEntry;
    using H = ContentSkeletonHelper;
    Registry registry;
    QVERIFY(registry.addTool(std::make_unique<E>(QStringLiteral("nodes"), ToolEntry::Place::Mode, 100)));
    QVERIFY(registry.addTool(std::make_unique<E>(QStringLiteral("image"), ToolEntry::Place::Toolbar, 300, QKeySequence(Qt::Key_I))));
    QVERIFY(registry.addTool(std::make_unique<E>(QStringLiteral("shape.ellipse"), ToolEntry::Place::ShapeMenu, 200, QKeySequence(Qt::Key_E))));
    QVERIFY(registry.addTool(std::make_unique<E>(QStringLiteral("select"), ToolEntry::Place::Toolbar, 100, QKeySequence(Qt::Key_V))));
    QVERIFY(registry.addTool(std::make_unique<E>(QStringLiteral("shape.rectangle"), ToolEntry::Place::ShapeMenu, 100, QKeySequence(Qt::Key_R))));
    QVERIFY(registry.addTool(std::make_unique<E>(QStringLiteral("text"), ToolEntry::Place::Toolbar, 200, QKeySequence(Qt::Key_T))));

    QCOMPARE(H::ids(registry.getTools()), QStringList({ "select", "text", "image", "shape.rectangle", "shape.ellipse", "nodes" }));
    QCOMPARE(H::ids(registry.getTools(ToolEntry::Place::ShapeMenu)), QStringList({ "shape.rectangle", "shape.ellipse" }));
    QCOMPARE(H::ids(registry.getTools(ToolEntry::Place::Mode)), QStringList({ "nodes" }));
    QVERIFY(registry.findTool(QStringLiteral("text")));
    QCOMPARE(registry.findTool(QStringLiteral("text"))->getOrder(), 200);
    QVERIFY(!registry.findTool(QStringLiteral("pen")));

    // The tool of a canvas key; no tool of an empty key or of a key nobody has
    QVERIFY(registry.findToolByKey(QKeySequence(Qt::Key_E)));
    QCOMPARE(registry.findToolByKey(QKeySequence(Qt::Key_E))->getId(), QStringLiteral("shape.ellipse"));
    QVERIFY(!registry.findToolByKey(QKeySequence()));
    QVERIFY(!registry.findToolByKey(QKeySequence(Qt::Key_P)));
}

void AppToolContentSkeletonTest::registryRefusesOrReplacesIdentifier()
{
    // An identifier registered already is refused with a warning, or replaced in its place with isReplacing
    using H = ContentSkeletonHelper;
    Registry registry;
    QVERIFY(registry.addTool(std::make_unique<ContentSkeletonToolEntry>(QStringLiteral("select"), ToolEntry::Place::Toolbar, 100, QKeySequence(Qt::Key_V), QStringLiteral("Select"))));
    QTest::ignoreMessage(QtWarningMsg, "PDFAppEditContentRegistry: the tool 'select' is registered already");
    QVERIFY(!registry.addTool(std::make_unique<ContentSkeletonToolEntry>(QStringLiteral("select"), ToolEntry::Place::Toolbar, 100, QKeySequence(Qt::Key_V), QStringLiteral("Other"))));
    QCOMPARE(registry.getTools().size(), size_t(1));
    QCOMPARE(registry.findTool(QStringLiteral("select"))->getTitle(), QStringLiteral("Select"));
    QVERIFY(registry.addTool(std::make_unique<ContentSkeletonToolEntry>(QStringLiteral("select"), ToolEntry::Place::Toolbar, 100, QKeySequence(Qt::Key_V), QStringLiteral("Replaced")), true));
    QCOMPARE(registry.getTools().size(), size_t(1));
    QCOMPARE(registry.findTool(QStringLiteral("select"))->getTitle(), QStringLiteral("Replaced"));

    // The replaced action keeps its place in the order of the registration (the order of the keys)
    QVERIFY(registry.addAction(std::make_unique<ContentSkeletonActionEntry>(QStringLiteral("a"))));
    QVERIFY(registry.addAction(std::make_unique<ContentSkeletonActionEntry>(QStringLiteral("b"))));
    QVERIFY(registry.addAction(std::make_unique<ContentSkeletonActionEntry>(QStringLiteral("c"))));
    QVERIFY(registry.addAction(std::make_unique<ContentSkeletonActionEntry>(QStringLiteral("b"), std::vector<ActionEntry::Key>(), std::vector<ActionEntry::Placement>(), nullptr, QStringLiteral("B2")), true));
    QCOMPARE(H::ids(registry.getActions()), QStringList({ "a", "b", "c" }));
    QCOMPARE(registry.findAction(QStringLiteral("b"))->getTitle(nullptr), QStringLiteral("B2"));

    // A later stage takes a card over (Fill and stroke of P41)
    QVERIFY(registry.addCard(std::make_unique<ContentSkeletonCardEntry>(QString::fromLatin1(pdf::PDFAppEditContentCardIds::FILL_AND_STROKE), QStringLiteral("E1"))));
    QTest::ignoreMessage(QtWarningMsg, "PDFAppEditContentRegistry: the card 'fill-and-stroke' is registered already");
    QVERIFY(!registry.addCard(std::make_unique<ContentSkeletonCardEntry>(QString::fromLatin1(pdf::PDFAppEditContentCardIds::FILL_AND_STROKE), QStringLiteral("E4"))));
    QCOMPARE(static_cast<const ContentSkeletonCardEntry*>(registry.findCard(QString::fromLatin1(pdf::PDFAppEditContentCardIds::FILL_AND_STROKE)))->tag, QStringLiteral("E1"));
    QVERIFY(registry.addCard(std::make_unique<ContentSkeletonCardEntry>(QString::fromLatin1(pdf::PDFAppEditContentCardIds::FILL_AND_STROKE), QStringLiteral("E4")), true));
    QCOMPARE(registry.getCards().size(), size_t(1));
    QCOMPARE(static_cast<const ContentSkeletonCardEntry*>(registry.findCard(QString::fromLatin1(pdf::PDFAppEditContentCardIds::FILL_AND_STROKE)))->tag, QStringLiteral("E4"));

    // An entry without an identifier and no entry at all are refused
    QTest::ignoreMessage(QtWarningMsg, "PDFAppEditContentRegistry: an entry of the kind tab without an identifier was refused");
    QVERIFY(!registry.addTab(std::make_unique<ContentSkeletonTabEntry>(QString(), 100, false)));
    QTest::ignoreMessage(QtWarningMsg, "PDFAppEditContentRegistry: an entry of the kind item without an identifier was refused");
    QVERIFY(!registry.addItem(nullptr));
    QVERIFY(registry.getTabs().empty());
    QVERIFY(registry.getItems().empty());
}

void AppToolContentSkeletonTest::registryTabsByOrder()
{
    // Objects 100, Pages 200, Layers 300, Structure 400 (pdfappeditcontentregistry.h), registered in any order
    using H = ContentSkeletonHelper;
    using T = pdf::PDFAppEditContentTabEntry;
    Registry registry;
    QVERIFY(registry.addTab(std::make_unique<ContentSkeletonTabEntry>(QString::fromLatin1(T::TAB_STRUCTURE), 400, false)));
    QVERIFY(registry.addTab(std::make_unique<ContentSkeletonTabEntry>(QString::fromLatin1(T::TAB_OBJECTS), 100, true)));
    QVERIFY(registry.addTab(std::make_unique<ContentSkeletonTabEntry>(QString::fromLatin1(T::TAB_LAYERS), 300, true)));
    QVERIFY(registry.addTab(std::make_unique<ContentSkeletonTabEntry>(QString::fromLatin1(T::TAB_PAGES), 200, false)));
    QCOMPARE(H::ids(registry.getTabs()), QStringList({ "objects", "pages", "layers", "structure" }));
    QVERIFY(registry.findTab(QStringLiteral("layers")));
    QVERIFY(!registry.findTab(QStringLiteral("find")));

    // The tabs of the application in stage E1: Objects with its model, Pages without a state
    const Registry& application = Registry::getInstance();
    QCOMPARE(H::ids(application.getTabs()), QStringList({ "objects", "pages" }));
    QCOMPARE(application.findTab(QStringLiteral("objects"))->getOrder(), 100);
    QCOMPARE(application.findTab(QStringLiteral("pages"))->getOrder(), 200);
    QCOMPARE(application.findTab(QStringLiteral("objects"))->getTitle(), QStringLiteral("Objects"));
    QCOMPARE(application.findTab(QStringLiteral("pages"))->getTitle(), QStringLiteral("Pages"));
    QCOMPARE(application.findTab(QStringLiteral("objects"))->getIconName(), QStringLiteral("object-tree"));
    QCOMPARE(application.findTab(QStringLiteral("pages"))->getIconName(), QStringLiteral("pages"));
}

void AppToolContentSkeletonTest::registryCardsInTableOrder()
{
    // zadani_gui.md 4.19, the table of the cards from the top (Selection always the first, Arrange the last card of
    // the selection; the cards without a selection or on request after it). The frames place two cards: Locked object
    // right below Selection (-locked-click: Selection, Locked object, Arrange), Annotations and fields between
    // Selection and Arrange (-annotations-underlay, -annotations-hidden)
    using C = pdf::PDFAppEditContentCardIds;
    using H = ContentSkeletonHelper;
    const QStringList table = { C::SELECTION, C::LOCKED_OBJECT, C::ANNOTATIONS, C::GLYPH, C::NODE, C::CLIPPING, C::IMAGE, C::CROP, C::SHARED_CONTENT,
                                C::SHADING, C::TILING_PATTERN, C::TEXT, C::FILL_AND_STROKE, C::TRANSPARENCY, C::PRINT_PRODUCTION, C::POSITION_AND_SIZE,
                                C::LAYER_AND_TAG, C::ARRANGE, C::LAYER, C::TAG, C::ARTIFACT, C::GRID_AND_UNITS, C::PREVIEW, C::KEYBOARD };
    int previous = 0;
    for (const QString& cardId : table)
    {
        const int order = C::getOrder(cardId);
        QVERIFY2(order > previous, qPrintable(cardId));
        previous = order;
    }
    QCOMPARE(C::getOrder(QStringLiteral("unknown")), -1);
    QCOMPARE(C::getOrder(QString()), -1);

    // The registry sorts the cards registered in the reverse order of the table
    Registry registry;
    for (auto it = table.crbegin(); it != table.crend(); ++it)
    {
        QVERIFY(registry.addCard(std::make_unique<ContentSkeletonCardEntry>(*it)));
    }
    QCOMPARE(H::ids(registry.getCards()), table);
}

void AppToolContentSkeletonTest::registryItemsByPriorityThenRegistration()
{
    // Entries of a higher priority are asked first, entries of one priority in the order of their registration
    using H = ContentSkeletonHelper;
    using E = ContentSkeletonItemEntry;
    const pdf::PDFContentContainer container{ pdf::PDFContentContainer::Data() };
    const std::shared_ptr<const pdf::PDFContentNode> node = H::createNode(pdf::PDFContentNodeKind::Path);

    Registry registry;
    QVERIFY(registry.addItem(std::make_unique<E>(QStringLiteral("a"), 0, false)));
    QVERIFY(registry.addItem(std::make_unique<E>(QStringLiteral("b"), 10, false)));
    QVERIFY(registry.addItem(std::make_unique<E>(QStringLiteral("c"), 0, true)));
    QVERIFY(registry.addItem(std::make_unique<E>(QStringLiteral("d"), 10, true)));
    QVERIFY(registry.addItem(std::make_unique<E>(QStringLiteral("e"), 0, true)));
    QCOMPARE(H::ids(registry.getItems()), QStringList({ "b", "d", "a", "c", "e" }));
    QVERIFY(registry.findItem(*node, container));
    QCOMPARE(registry.findItem(*node, container)->getId(), QStringLiteral("d"));

    Registry samePriority;
    QVERIFY(samePriority.addItem(std::make_unique<E>(QStringLiteral("a"), 0, false)));
    QVERIFY(samePriority.addItem(std::make_unique<E>(QStringLiteral("c"), 0, true)));
    QVERIFY(samePriority.addItem(std::make_unique<E>(QStringLiteral("e"), 0, true)));
    QCOMPARE(samePriority.findItem(*node, container)->getId(), QStringLiteral("c"));

    Registry none;
    QVERIFY(none.addItem(std::make_unique<E>(QStringLiteral("a"), 0, false)));
    QVERIFY(!none.findItem(*node, container));
}

void AppToolContentSkeletonTest::registryMenuActionsByGroupAndOrder()
{
    // The rows of a menu by the group and the order of their placement in that menu; equal ones by registration
    using H = ContentSkeletonHelper;
    using A = ContentSkeletonActionEntry;
    using M = pdf::PDFAppEditContentMenuIds;
    using Placements = std::vector<ActionEntry::Placement>;
    using Keys = std::vector<ActionEntry::Key>;
    Registry registry;
    QVERIFY(registry.addAction(std::make_unique<A>(QStringLiteral("a"), Keys(), Placements({ H::placement(M::CONTEXT, 200, 20) }))));
    QVERIFY(registry.addAction(std::make_unique<A>(QStringLiteral("b"), Keys(), Placements({ H::placement(M::CONTEXT, 100, 50), H::placement(M::VIEW, 100, 10) }))));
    QVERIFY(registry.addAction(std::make_unique<A>(QStringLiteral("c"), Keys(), Placements({ H::placement(M::CONTEXT, 200, 10) }))));
    QVERIFY(registry.addAction(std::make_unique<A>(QStringLiteral("d"), Keys(), Placements({ H::placement(M::VIEW, 300, 10) }))));
    QVERIFY(registry.addAction(std::make_unique<A>(QStringLiteral("e"), Keys(), Placements({ H::placement(M::CONTEXT, 100, 50) }))));
    QVERIFY(registry.addAction(std::make_unique<A>(QStringLiteral("f"))));

    QCOMPARE(H::ids(registry.getMenuActions(QString::fromLatin1(M::CONTEXT))), QStringList({ "b", "e", "c", "a" }));
    QCOMPARE(H::ids(registry.getMenuActions(QString::fromLatin1(M::VIEW))), QStringList({ "b", "d" }));
    QVERIFY(registry.getMenuActions(QString::fromLatin1(M::LAYER_ROW)).empty());
    QCOMPARE(H::ids(registry.getActions()), QStringList({ "a", "b", "c", "d", "e", "f" }));

    // A value (R8-12): taken straight from the temporary vector of getPlacements, it stays valid
    const std::optional<ActionEntry::Placement> view = Registry::findPlacement(registry.findAction(QStringLiteral("b"))->getPlacements(), QString::fromLatin1(M::VIEW));
    QVERIFY(view.has_value());
    QCOMPARE(view->menuId, QString::fromLatin1(M::VIEW));
    QCOMPARE(view->group, 100);
    QCOMPARE(view->order, 10);
    QVERIFY(!Registry::findPlacement(registry.findAction(QStringLiteral("b"))->getPlacements(), QString::fromLatin1(M::TAG_ROW)).has_value());
}

void AppToolContentSkeletonTest::registryActionByKeyAndScope()
{
    // A key belongs to an action in its scope only (zadani_gui.md 4.20: the canvas keys and the shortcuts of the tool)
    using H = ContentSkeletonHelper;
    using A = ContentSkeletonActionEntry;
    using Keys = std::vector<ActionEntry::Key>;
    const QKeySequence ctrlAltH(Qt::CTRL | Qt::ALT | Qt::Key_H);
    const QKeySequence ctrlAltX(Qt::CTRL | Qt::ALT | Qt::Key_X);
    Registry registry;
    QVERIFY(registry.addAction(std::make_unique<A>(QStringLiteral("canvas-h"), Keys({ H::key(ctrlAltH, KeyScope::Canvas) }))));
    QVERIFY(registry.addAction(std::make_unique<A>(QStringLiteral("hide"), Keys({ H::key(ctrlAltH, KeyScope::Tool) }))));
    QVERIFY(registry.addAction(std::make_unique<A>(QStringLiteral("preview"), Keys({ H::key(QKeySequence(Qt::Key_P), KeyScope::Canvas) }))));
    QVERIFY(registry.addAction(std::make_unique<A>(QStringLiteral("both"), Keys({ H::key(ctrlAltX, KeyScope::Tool), H::key(QKeySequence(Qt::Key_X), KeyScope::Canvas) }))));

    QCOMPARE(registry.findActionByKey(ctrlAltH, KeyScope::Tool)->getId(), QStringLiteral("hide"));
    QCOMPARE(registry.findActionByKey(ctrlAltH, KeyScope::Canvas)->getId(), QStringLiteral("canvas-h"));
    QCOMPARE(registry.findActionByKey(QKeySequence(Qt::Key_P), KeyScope::Canvas)->getId(), QStringLiteral("preview"));
    QVERIFY(!registry.findActionByKey(QKeySequence(Qt::Key_P), KeyScope::Tool));
    QCOMPARE(registry.findActionByKey(ctrlAltX, KeyScope::Tool)->getId(), QStringLiteral("both"));
    QCOMPARE(registry.findActionByKey(QKeySequence(Qt::Key_X), KeyScope::Canvas)->getId(), QStringLiteral("both"));
    QVERIFY(!registry.findActionByKey(QKeySequence(), KeyScope::Canvas));
}

void AppToolContentSkeletonTest::registryEditorTool()
{
    // The editor of a node: the first tool in the order of the registry (place, order), which says isEditorOf
    using H = ContentSkeletonHelper;
    using E = ContentSkeletonToolEntry;
    const pdf::PDFContentContainer container{ pdf::PDFContentContainer::Data() };
    Registry registry;
    QVERIFY(registry.addTool(std::make_unique<E>(QStringLiteral("catch-all"), ToolEntry::Place::Mode, 300, QKeySequence(), QString(), std::nullopt, true)));
    QVERIFY(registry.addTool(std::make_unique<E>(QStringLiteral("image.crop"), ToolEntry::Place::Mode, 200, QKeySequence(), QString(), pdf::PDFContentNodeKind::Image)));
    QVERIFY(registry.addTool(std::make_unique<E>(QStringLiteral("nodes"), ToolEntry::Place::Mode, 100, QKeySequence(), QString(), pdf::PDFContentNodeKind::Path)));
    QVERIFY(registry.addTool(std::make_unique<E>(QStringLiteral("select"), ToolEntry::Place::Toolbar, 100, QKeySequence(Qt::Key_V))));

    QCOMPARE(registry.findEditorTool(*H::createNode(pdf::PDFContentNodeKind::Path), container)->getId(), QStringLiteral("nodes"));
    QCOMPARE(registry.findEditorTool(*H::createNode(pdf::PDFContentNodeKind::Image), container)->getId(), QStringLiteral("image.crop"));
    QCOMPARE(registry.findEditorTool(*H::createNode(pdf::PDFContentNodeKind::Shading), container)->getId(), QStringLiteral("catch-all"));

    Registry withoutEditor;
    QVERIFY(withoutEditor.addTool(std::make_unique<E>(QStringLiteral("select"), ToolEntry::Place::Toolbar, 100, QKeySequence(Qt::Key_V))));
    QVERIFY(!withoutEditor.findEditorTool(*H::createNode(pdf::PDFContentNodeKind::Path), container));
}

void AppToolContentSkeletonTest::keyTableRows()
{
    // The rows: the canvas keys of the tools in their order, then the keys of the actions in the order of registration
    using H = ContentSkeletonHelper;
    using Keys = std::vector<ActionEntry::Key>;
    Registry registry;
    QVERIFY(registry.addTool(std::make_unique<ContentSkeletonToolEntry>(QStringLiteral("text"), ToolEntry::Place::Toolbar, 200, QKeySequence(Qt::Key_T), QStringLiteral("Text"))));
    QVERIFY(registry.addTool(std::make_unique<ContentSkeletonToolEntry>(QStringLiteral("nodes"), ToolEntry::Place::Mode, 100)));
    QVERIFY(registry.addTool(std::make_unique<ContentSkeletonToolEntry>(QStringLiteral("select"), ToolEntry::Place::Toolbar, 100, QKeySequence(Qt::Key_V), QStringLiteral("Select"))));
    QVERIFY(registry.addAction(std::make_unique<ContentSkeletonActionEntry>(QStringLiteral("hide"), Keys({ H::key(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_H), KeyScope::Tool) }),
                                                                            std::vector<ActionEntry::Placement>(), nullptr, QStringLiteral("Hide"))));
    QVERIFY(registry.addAction(std::make_unique<ContentSkeletonActionEntry>(QStringLiteral("none"))));
    QVERIFY(registry.addAction(std::make_unique<ContentSkeletonActionEntry>(QStringLiteral("preview"), Keys({ H::key(QKeySequence(Qt::Key_P), KeyScope::Canvas) }))));

    const std::vector<pdf::PDFAppEditContentKeyRow> rows = KeyTable::getRows(registry);
    QCOMPARE(rows.size(), size_t(4));
    QCOMPARE(rows[0].toolId, QStringLiteral("select"));
    QCOMPARE(rows[0].sequence, QKeySequence(Qt::Key_V));
    QVERIFY(rows[0].scope == KeyScope::Canvas);
    QCOMPARE(rows[0].title, QStringLiteral("Select"));
    QVERIFY(rows[0].actionId.isEmpty());
    QCOMPARE(rows[1].toolId, QStringLiteral("text"));
    QCOMPARE(rows[2].actionId, QStringLiteral("hide"));
    QVERIFY(rows[2].toolId.isEmpty());
    QVERIFY(rows[2].scope == KeyScope::Tool);
    QCOMPARE(rows[2].title, QStringLiteral("Hide"));
    QCOMPARE(rows[3].actionId, QStringLiteral("preview"));
    QVERIFY(rows[3].scope == KeyScope::Canvas);
    QCOMPARE(KeyTable::check(rows, std::vector<QKeySequence>()), QStringList());
}

void AppToolContentSkeletonTest::keyTableProblem_data()
{
    // pdfappeditcontentregistry.h, PDFAppEditContentKeyTable::check: one hand-made row per kind of problem
    QTest::addColumn<QKeySequence>("sequence");
    QTest::addColumn<int>("scope");
    QTest::addColumn<QString>("problem");

    const int canvas = int(KeyScope::Canvas);
    const int tool = int(KeyScope::Tool);
    QTest::newRow("empty") << QKeySequence() << tool << QStringLiteral("has an empty key");
    QTest::newRow("chord") << QKeySequence(Qt::CTRL | Qt::Key_K, Qt::CTRL | Qt::Key_L) << tool << QStringLiteral("chord of more combinations");
    QTest::newRow("canvas with Ctrl") << QKeySequence(Qt::CTRL | Qt::Key_Q) << canvas << QStringLiteral("modifier other than Shift");
    QTest::newRow("canvas with Alt") << QKeySequence(Qt::ALT | Qt::Key_Q) << canvas << QStringLiteral("modifier other than Shift");
    QTest::newRow("canvas reserved Delete") << QKeySequence(Qt::Key_Delete) << canvas << QStringLiteral("belongs to the tools of the canvas");
    QTest::newRow("canvas reserved Shift+Left") << QKeySequence(Qt::SHIFT | Qt::Key_Left) << canvas << QStringLiteral("belongs to the tools of the canvas");
    QTest::newRow("tool without Ctrl or Alt") << QKeySequence(Qt::Key_Q) << tool << QStringLiteral("neither Ctrl nor Alt");
    QTest::newRow("tool with Shift only") << QKeySequence(Qt::SHIFT | Qt::Key_Q) << tool << QStringLiteral("neither Ctrl nor Alt");
    QTest::newRow("shell key") << QKeySequence(Qt::CTRL | Qt::Key_Return) << tool << QStringLiteral("key of the tool shell");
    QTest::newRow("application shortcut") << QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Q) << tool << QStringLiteral("shortcut of a command of the application");
    QTest::newRow("canvas key valid") << QKeySequence(Qt::Key_Q) << canvas << QString();
    QTest::newRow("canvas Shift valid") << QKeySequence(Qt::SHIFT | Qt::Key_Q) << canvas << QString();
    QTest::newRow("tool function key valid") << QKeySequence(Qt::Key_F7) << tool << QString();
    QTest::newRow("tool Ctrl+Alt valid") << QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_W) << tool << QString();
}

void AppToolContentSkeletonTest::keyTableProblem()
{
    QFETCH(QKeySequence, sequence);
    QFETCH(int, scope);
    QFETCH(QString, problem);

    pdf::PDFAppEditContentKeyRow row;
    row.sequence = sequence;
    row.scope = KeyScope(scope);
    row.actionId = QStringLiteral("probe");
    const QStringList problems = KeyTable::check({ row }, { QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Q) });
    if (problem.isEmpty())
    {
        QCOMPARE(problems, QStringList());
    }
    else
    {
        QCOMPARE(problems.size(), 1);
        QVERIFY2(problems.front().contains(problem), qPrintable(problems.front()));
        QVERIFY2(problems.front().contains(QStringLiteral("action 'probe'")), qPrintable(problems.front()));
    }
}

void AppToolContentSkeletonTest::keyTableKeyTwiceInScope()
{
    // A key twice in one scope is a problem of the second row; the same key in the other scope is none
    pdf::PDFAppEditContentKeyRow first;
    first.sequence = QKeySequence(Qt::Key_F8);
    first.scope = KeyScope::Canvas;
    first.toolId = QStringLiteral("first");
    pdf::PDFAppEditContentKeyRow other = first;
    other.scope = KeyScope::Tool;
    other.toolId = QStringLiteral("other");
    QCOMPARE(KeyTable::check({ first, other }, { }), QStringList());

    pdf::PDFAppEditContentKeyRow second = first;
    second.toolId = QStringLiteral("second");
    const QStringList problems = KeyTable::check({ first, other, second }, { });
    QCOMPARE(problems.size(), 1);
    QVERIFY2(problems.front().contains(QStringLiteral("tool 'second'")) && problems.front().contains(QStringLiteral("used twice in the scope Canvas")), qPrintable(problems.front()));
}

void AppToolContentSkeletonTest::keyTableAgainstApplication()
{
    // The table of the application registry has no problem against the default shortcuts of the commands of the
    // application, which act in the whole window (scopes Window and Document; zadani_gui.md 4.20). The test keeps
    // guarding the table when the packages add their keys.
    pdf::PDFAppCommandRegistry commands;
    pdf::PDFAppCommands::registerBuiltinCommands(&commands);
    std::vector<QKeySequence> shortcuts;
    for (const pdf::PDFAppCommand* command : commands.getCommands())
    {
        if (command->scope != pdf::PDFAppCommandScope::Window && command->scope != pdf::PDFAppCommandScope::Document)
        {
            continue;
        }
        if (!command->defaultShortcut.isEmpty())
        {
            shortcuts.push_back(command->defaultShortcut);
        }
        for (const QKeySequence& alternate : command->alternateShortcuts)
        {
            shortcuts.push_back(alternate);
        }
    }

    // Go to page is Ctrl+G of the application (pe1_skeleton_notes.md 8.2): the list holds the window shortcuts
    QVERIFY(std::find(shortcuts.cbegin(), shortcuts.cend(), QKeySequence(Qt::CTRL | Qt::Key_G)) != shortcuts.cend());

    const QStringList problems = KeyTable::check(KeyTable::getRows(Registry::getInstance()), shortcuts);
    QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));

    // Every one of them is refused, when a row of the tool takes it
    for (const QKeySequence& shortcut : shortcuts)
    {
        pdf::PDFAppEditContentKeyRow row;
        row.sequence = shortcut;
        row.scope = KeyScope::Tool;
        row.actionId = QStringLiteral("probe");
        QVERIFY2(KeyTable::check({ row }, shortcuts).join(QLatin1Char('\n')).contains(QStringLiteral("shortcut of a command of the application")), qPrintable(shortcut.toString()));
    }

    // The application commands answered by the entries of the registry of the application (R3-1): known commands,
    // one entry each, none of the shell, no key of a command an entry answers (every scope)
    std::map<QString, std::vector<QKeySequence>> commandShortcuts;
    for (const pdf::PDFAppCommand* command : commands.getCommands())
    {
        std::vector<QKeySequence>& keys = commandShortcuts[command->id];
        if (!command->defaultShortcut.isEmpty())
        {
            keys.push_back(command->defaultShortcut);
        }
        keys.insert(keys.end(), command->alternateShortcuts.cbegin(), command->alternateShortcuts.cend());
    }
    const QStringList commandProblems = KeyTable::checkApplicationCommands(Registry::getInstance(), commandShortcuts);
    QVERIFY2(commandProblems.isEmpty(), qPrintable(commandProblems.join(QLatin1Char('\n'))));

    // The commands of the shell exist in the application
    for (const QString& commandId : KeyTable::getShellCommandIds())
    {
        QVERIFY2(commandShortcuts.count(commandId), qPrintable(commandId));
    }
}

void AppToolContentSkeletonTest::keyTableApplicationCommands()
{
    // pdfappeditcontentregistry.h, PDFAppEditContentActionEntry::getApplicationCommandIds (R3-1): a command answered by
    // one entry at most, never a command of the shell, never an unknown one, and the entry never takes a shortcut of
    // a command it answers as its own key - the Window / Document ones come from the application, the Canvas ones
    // are turned into the command by the view of the tool (pe1_skeleton_notes.md 8.2, 11)
    using E = ContentSkeletonCommandEntry;
    using H = ContentSkeletonHelper;
    const QKeySequence ctrlA(Qt::CTRL | Qt::Key_A);
    const QKeySequence ctrlC(Qt::CTRL | Qt::Key_C);
    const QKeySequence ctrlF(Qt::CTRL | Qt::Key_F);
    const QKeySequence ctrlS(Qt::CTRL | Qt::Key_S);
    const QKeySequence remove(Qt::Key_Delete);
    const std::map<QString, std::vector<QKeySequence>> shortcuts = {
        { QStringLiteral("edit.selectAll"), { ctrlA } }, { QStringLiteral("edit.copy"), { ctrlC } }, { QStringLiteral("search.find"), { ctrlF } },
        { QStringLiteral("file.save"), { ctrlS } }, { QStringLiteral("edit.delete"), { remove } }, { QStringLiteral("view.zoomIn"), { } }
    };

    // Valid: commands without a key of their own, a key of another command (Ctrl+Alt+... is no shortcut here)
    {
        Registry registry;
        QVERIFY(registry.addAction(std::make_unique<E>(QStringLiteral("select-all"), QStringList({ "edit.selectAll" }))));
        QVERIFY(registry.addAction(std::make_unique<E>(QStringLiteral("zoom"), QStringList({ "view.zoomIn" }), nullptr,
                                                        std::vector<ActionEntry::Key>({ H::key(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Z), KeyScope::Tool) }))));
        const QStringList problems = KeyTable::checkApplicationCommands(registry, shortcuts);
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
        QCOMPARE(registry.findActionByApplicationCommand(QStringLiteral("edit.selectAll"))->getId(), QStringLiteral("select-all"));
        QCOMPARE(registry.findActionByApplicationCommand(QStringLiteral("view.zoomIn"))->getId(), QStringLiteral("zoom"));
        QVERIFY(!registry.findActionByApplicationCommand(QStringLiteral("edit.copy")));
        QVERIFY(!registry.findActionByApplicationCommand(QString()));
    }

    // Each fault once
    Registry registry;
    QVERIFY(registry.addAction(std::make_unique<E>(QStringLiteral("find"), QStringList({ "search.find" }), nullptr, std::vector<ActionEntry::Key>({ H::key(ctrlF, KeyScope::Tool) }))));
    QVERIFY(registry.addAction(std::make_unique<E>(QStringLiteral("copy"), QStringList({ "edit.copy" }), nullptr, std::vector<ActionEntry::Key>({ H::key(ctrlC, KeyScope::Tool) }))));
    QVERIFY(registry.addAction(std::make_unique<E>(QStringLiteral("copy-again"), QStringList({ "edit.copy" }))));
    QVERIFY(registry.addAction(std::make_unique<E>(QStringLiteral("save"), QStringList({ "file.save" }))));
    QVERIFY(registry.addAction(std::make_unique<E>(QStringLiteral("ghost"), QStringList({ "edit.nothing" }))));
    QVERIFY(registry.addAction(std::make_unique<E>(QStringLiteral("delete"), QStringList({ "edit.delete" }), nullptr, std::vector<ActionEntry::Key>({ H::key(remove, KeyScope::Canvas) }))));
    const QStringList problems = KeyTable::checkApplicationCommands(registry, shortcuts);
    const QStringList expected = {
        QStringLiteral("action 'find' takes the key 'Ctrl+F' of the command 'search.find', which it answers"),
        QStringLiteral("action 'copy' takes the key 'Ctrl+C' of the command 'edit.copy', which it answers"),
        QStringLiteral("action 'copy-again' answers the command 'edit.copy', which the action 'copy' answers already"),
        QStringLiteral("action 'save' answers the command 'file.save', which the shell of the tool answers"),
        QStringLiteral("action 'ghost' answers the command 'edit.nothing', which the application does not have"),
        QStringLiteral("action 'delete' takes the key 'Del' of the command 'edit.delete', which it answers")
    };
    QCOMPARE(problems, expected);

    // The first registered entry answers a command answered twice; a replaced entry takes the command over
    QCOMPARE(registry.findActionByApplicationCommand(QStringLiteral("edit.copy"))->getId(), QStringLiteral("copy"));
    QVERIFY(registry.addAction(std::make_unique<E>(QStringLiteral("copy"), QStringList({ "edit.cut" })), true));
    QCOMPARE(registry.findActionByApplicationCommand(QStringLiteral("edit.copy"))->getId(), QStringLiteral("copy-again"));
    QCOMPARE(registry.findActionByApplicationCommand(QStringLiteral("edit.cut"))->getId(), QStringLiteral("copy"));
    QCOMPARE(KeyTable::getShellCommandIds(), QStringList({ "edit.undo", "edit.redo", "file.save", "file.saveAs" }));
}

void AppToolContentSkeletonTest::keyTableCanvasKeys()
{
    // R6-6: a canvas key of the registry (of a tool entry, or of the scope Canvas of an action entry), which is the
    // Canvas-scope shortcut of a command of the application an entry answers, is reported (on the canvas of the tool the
    // key of the registry wins, the command is reached by the menu and the status bar); a command no entry answers is
    // no conflict (its shortcut never reaches the canvas of the tool), a key of the scope Tool neither
    using A = ContentSkeletonActionEntry;
    using C = ContentSkeletonCommandEntry;
    using E = ContentSkeletonToolEntry;
    using H = ContentSkeletonHelper;
    const std::map<QString, std::vector<QKeySequence>> canvasShortcuts = {
        { QStringLiteral("view.fitPage"), { QKeySequence(Qt::Key_P) } }, { QStringLiteral("view.fitWidth"), { QKeySequence(Qt::Key_W) } }
    };

    {
        Registry registry;
        QVERIFY(registry.addAction(std::make_unique<A>(QStringLiteral("preview"), std::vector<ActionEntry::Key>({ H::key(QKeySequence(Qt::Key_P), KeyScope::Canvas) }))));
        QVERIFY(registry.addAction(std::make_unique<A>(QStringLiteral("tool-key"), std::vector<ActionEntry::Key>({ H::key(QKeySequence(Qt::CTRL | Qt::Key_W), KeyScope::Tool) }))));
        QVERIFY(registry.addTool(std::make_unique<E>(QStringLiteral("wide"), ToolEntry::Place::Toolbar, 100, QKeySequence(Qt::Key_W))));
        QVERIFY(KeyTable::checkCanvasKeys(registry, canvasShortcuts).isEmpty());

        QVERIFY(registry.addAction(std::make_unique<C>(QStringLiteral("fit-page"), QStringList({ "view.fitPage" }))));
        QCOMPARE(KeyTable::checkCanvasKeys(registry, canvasShortcuts),
                 QStringList({ QStringLiteral("action 'preview': the canvas key 'P' is the Canvas shortcut of the command 'view.fitPage', which the action 'fit-page' answers") }));

        QVERIFY(registry.addAction(std::make_unique<C>(QStringLiteral("fit-width"), QStringList({ "view.fitWidth" }))));
        QCOMPARE(KeyTable::checkCanvasKeys(registry, canvasShortcuts),
                 QStringList({ QStringLiteral("tool 'wide': the canvas key 'W' is the Canvas shortcut of the command 'view.fitWidth', which the action 'fit-width' answers"),
                               QStringLiteral("action 'preview': the canvas key 'P' is the Canvas shortcut of the command 'view.fitPage', which the action 'fit-page' answers") }));
    }

    // The registry of the application against the default shortcuts of the scope Canvas: exactly the one the plan
    // decided (zadani_gui.md 4.15 / 4.20: P is Preview on the canvas of the tool, view.fitPage stays in the menu and the
    // status bar) - a new conflict of a package fails here
    pdf::PDFAppCommandRegistry commands;
    pdf::PDFAppCommands::registerBuiltinCommands(&commands);
    std::map<QString, std::vector<QKeySequence>> applicationCanvasShortcuts;
    for (const pdf::PDFAppCommand* command : commands.getCommands())
    {
        if (command->scope != pdf::PDFAppCommandScope::Canvas)
        {
            continue;
        }
        std::vector<QKeySequence>& keys = applicationCanvasShortcuts[command->id];
        if (!command->defaultShortcut.isEmpty())
        {
            keys.push_back(command->defaultShortcut);
        }
        keys.insert(keys.end(), command->alternateShortcuts.cbegin(), command->alternateShortcuts.cend());
    }
    const std::vector<QKeySequence>& fitPageKeys = applicationCanvasShortcuts[QStringLiteral("view.fitPage")];
    QVERIFY(std::find(fitPageKeys.cbegin(), fitPageKeys.cend(), QKeySequence(Qt::Key_P)) != fitPageKeys.cend());
    const QStringList problems = KeyTable::checkCanvasKeys(Registry::getInstance(), applicationCanvasShortcuts);
    QVERIFY2(problems.size() == 1, qPrintable(problems.join(QLatin1Char('\n'))));
    QVERIFY2(problems.front().startsWith(QStringLiteral("action 'preview': the canvas key 'P' is the Canvas shortcut of the command 'view.fitPage', which the action ")), qPrintable(problems.front()));
}

void AppToolContentSkeletonTest::selectionState()
{
    // pdfappeditcontentselection.h: the order of the selection, the key object (the node selected last), no
    // duplicates, one container, changed exactly once per change and last, none without a change
    using H = ContentSkeletonHelper;
    const pdf::PDFContentContainerKey pageA = pdf::PDFContentContainerKey::createPage(0, pdf::PDFObjectReference(10, 0));
    const pdf::PDFContentContainerKey pageB = pdf::PDFContentContainerKey::createPage(1, pdf::PDFObjectReference(11, 0));
    const pdf::PDFContentNodeReference n1 = H::node(pageA, 1);
    const pdf::PDFContentNodeReference n2 = H::node(pageA, 2);
    const pdf::PDFContentNodeReference n3 = H::node(pageA, 3);
    const pdf::PDFContentNodeReference n4 = H::node(pageA, 4);
    const pdf::PDFContentNodeReference m1 = H::node(pageB, 1);
    using Nodes = std::vector<pdf::PDFContentNodeReference>;

    pdf::PDFAppEditContentSelection selection(nullptr, nullptr);
    int changes = 0;
    Nodes nodesAtSignal;
    pdf::PDFContentNodeReference keyAtSignal;
    QObject::connect(&selection, &pdf::PDFAppEditContentSelection::changed, &selection, [&]()
    {
        ++changes;
        nodesAtSignal = selection.getNodes();
        keyAtSignal = selection.getKeyNode();
    });

    selection.setNodes({ n2, n1, n2, pdf::PDFContentNodeReference(), m1, n3 });
    QVERIFY(selection.getNodes() == Nodes({ n2, n1, n3 }));
    QVERIFY(selection.getKeyNode() == n3);
    QCOMPARE(changes, 1);
    QVERIFY(nodesAtSignal == Nodes({ n2, n1, n3 }) && keyAtSignal == n3);

    selection.setNodes({ n2, n1, n3 });
    QCOMPARE(changes, 1);

    // The key object: a selected node only
    selection.setKeyNode(n1);
    QVERIFY(selection.getKeyNode() == n1);
    QCOMPARE(changes, 2);
    QVERIFY(keyAtSignal == n1);
    selection.setKeyNode(n1);
    selection.setKeyNode(m1);
    QCOMPARE(changes, 2);
    QVERIFY(selection.getKeyNode() == n1);

    // Adding a selected node makes it the key object; adding a new one appends it
    selection.addNode(n2);
    QVERIFY(selection.getNodes() == Nodes({ n2, n1, n3 }));
    QVERIFY(selection.getKeyNode() == n2);
    QCOMPARE(changes, 3);
    selection.addNode(n4);
    QVERIFY(selection.getNodes() == Nodes({ n2, n1, n3, n4 }));
    QVERIFY(selection.getKeyNode() == n4);
    QCOMPARE(changes, 4);

    // Removing the key object passes the key to the node selected last
    selection.removeNode(n4);
    QVERIFY(selection.getNodes() == Nodes({ n2, n1, n3 }));
    QVERIFY(selection.getKeyNode() == n3);
    QCOMPARE(changes, 5);
    QVERIFY(keyAtSignal == n3);
    selection.removeNode(m1);
    QCOMPARE(changes, 5);

    // Toggle: out and back in (at the end, the key object)
    selection.toggleNode(n2);
    QVERIFY(selection.getNodes() == Nodes({ n1, n3 }));
    QVERIFY(selection.getKeyNode() == n3);
    QCOMPARE(changes, 6);
    selection.toggleNode(n2);
    QVERIFY(selection.getNodes() == Nodes({ n1, n3, n2 }));
    QVERIFY(selection.getKeyNode() == n2);
    QCOMPARE(changes, 7);

    // A node of another container replaces the selection
    selection.addNode(m1);
    QVERIFY(selection.getNodes() == Nodes({ m1 }));
    QVERIFY(selection.getKeyNode() == m1);
    QCOMPARE(changes, 8);
    QVERIFY(selection.contains(m1));
    QVERIFY(!selection.contains(n1));
    QCOMPARE(selection.getCount(), 1);
    selection.setNode(m1);
    QCOMPARE(changes, 8);

    // Nothing selected; an invalid node is never added
    selection.clear();
    QVERIFY(selection.isEmpty());
    QVERIFY(!selection.getKeyNode().isValid());
    QCOMPARE(changes, 9);
    selection.clear();
    selection.addNode(pdf::PDFContentNodeReference());
    selection.setNode(pdf::PDFContentNodeReference());
    QCOMPARE(changes, 9);
    QVERIFY(selection.isEmpty());
}

void AppToolContentSkeletonTest::optionsDefaultsAndSessionValues()
{
    // pdfappeditcontentcontext.h, PDFAppEditContentOptions: the defaults of the header; without a model a value is
    // kept for the session; changed once per change, none without one
    using O = pdf::PDFAppEditContentOptions;
    QObject owner;
    O* options = new O(&owner);
    const std::vector<std::pair<const char*, QVariant>> defaults = {
        { O::SHOW_RULERS, false }, { O::SHOW_GRID, false }, { O::SHOW_GUIDES, true }, { O::SHOW_CLIPPING_PATHS, false },
        { O::SHOW_INVISIBLE_TEXT, false }, { O::SHOW_ANNOTATIONS, true }, { O::SHOW_READING_ORDER, false }, { O::SNAP_TO_OBJECTS, true },
        { O::SNAP_TO_GRID, true }, { O::UNITS, QStringLiteral("pt") }, { O::GRID_SPACING, 36.0 }, { O::GRID_ORIGIN, QStringLiteral("bottom-left") },
        { O::PREVIEW_OVERPRINT, true }, { O::PREVIEW_ANNOTATIONS, true }, { O::PREVIEW_PAPER_COLOR, QStringLiteral("#ffffff") },
        { O::LEFT_TAB, QStringLiteral("objects") }
    };
    for (const auto& [name, value] : defaults)
    {
        QCOMPARE(O::getDefaultValue(QString::fromLatin1(name)), value);
        QCOMPARE(options->getValue(QString::fromLatin1(name)), value);
    }
    QVERIFY(!O::getDefaultValue(QStringLiteral("unknown")).isValid());
    QVERIFY(!options->getValue(QStringLiteral("unknown")).isValid());

    QSignalSpy changed(options, &O::changed);
    options->setValue(QString::fromLatin1(O::SHOW_GRID), true);
    QVERIFY(options->getBool(QString::fromLatin1(O::SHOW_GRID)));
    QCOMPARE(changed.size(), 1);
    QCOMPARE(changed.at(0).at(0).toString(), QString::fromLatin1(O::SHOW_GRID));
    options->setValue(QString::fromLatin1(O::SHOW_GRID), true);
    QCOMPARE(changed.size(), 1);

    // A name of a later stage works the same way
    options->setValue(QStringLiteral("nextShape.fill"), QStringLiteral("#ff0000"));
    QCOMPARE(options->getValue(QStringLiteral("nextShape.fill")), QVariant(QStringLiteral("#ff0000")));
    QCOMPARE(changed.size(), 2);
    options->setValue(QString(), 1);
    QCOMPARE(changed.size(), 2);
}

void AppToolContentSkeletonTest::optionsModelParameterWins()
{
    // A stored parameter of the tool of the same name wins over the default and is written without an undo step
    using O = pdf::PDFAppEditContentOptions;
    const QString rulers = QString::fromLatin1(O::SHOW_RULERS);
    pdf::PDFDocumentToolParameterDefinitions definitions;
    definitions.push_back(pdf::PDFDocumentToolParameterDefinition::createBool(&ContentSkeletonText::tr, rulers, QT_TR_NOOP("Rulers"), nullptr, true));
    pdf::PDFAppToolParameterModel model(nullptr, QString::fromLatin1(ContentSkeletonHelper::TOOL_ID), definitions, nullptr);
    QObject owner;
    O* options = new O(&owner);
    options->setValue(QString::fromLatin1(O::SHOW_GRID), true);
    options->setModel(&model);
    QSignalSpy changed(options, &O::changed);

    QCOMPARE(options->getValue(rulers), QVariant(true));
    options->setValue(rulers, false);
    QCOMPARE(model.getValue(rulers), QVariant(false));
    QVERIFY(!options->getBool(rulers));
    QVERIFY(!model.canUndo());
    QCOMPARE(changed.size(), 1);
    QCOMPARE(changed.at(0).at(0).toString(), rulers);

    // A change of the model is told by the options
    QVERIFY(model.setValue(rulers, true).isEmpty());
    QVERIFY(options->getBool(rulers));
    QCOMPARE(changed.size(), 2);

    // A name the model does not have is still the value of the session
    QVERIFY(options->getBool(QString::fromLatin1(O::SHOW_GRID)));

    // Without the model the default comes back
    options->setModel(nullptr);
    QCOMPARE(options->getValue(rulers), QVariant(false));
}

void AppToolContentSkeletonTest::geometryBoundingBoxes()
{
    // PDFAppEditContentGeometryHelper, PDFAppEditContentSelection::getBoundingBox (R3-2): the box of a node as the
    // content draws it now, in the default space of the page. The boxes by hand from the content (ISO 32000-2: re,
    // cm, the unit square of an image 8.9.5, the /BBox of a form under its /Matrix 8.10.2) and from the matrices of
    // the commands (a Transform changes the override, never the box of the builder).
    using H = ContentSkeletonHelper;
    using G = pdf::PDFAppEditContentGeometryHelper;
    using F = pdf::PDFContentCommandFactory;
    using Kind = pdf::PDFContentNodeKind;
    Registry registry;
    QPointer<Context> context = new Context(H::createGeometryDocument(), pdf::PDFCMSPointer(), &registry, nullptr);
    auto cleanup = qScopeGuard([&context]() { ContentSkeletonHelper::finish(context); });
    QVERIFY(H::showPage(context, 0));
    const pdf::PDFContentContainerPointer page = context->getContainer();
    const pdf::PDFContentContainerKey pageKey = page->getKey();

    const std::vector<pdf::PDFContentNodeReference> paths = H::findNodes(page, Kind::Path);
    const std::vector<pdf::PDFContentNodeReference> groups = H::findNodes(page, Kind::Group);
    const std::vector<pdf::PDFContentNodeReference> images = H::findNodes(page, Kind::Image);
    const std::vector<pdf::PDFContentNodeReference> forms = H::findNodes(page, Kind::FormUse);
    QCOMPARE(paths.size(), size_t(4));
    QCOMPARE(groups.size(), size_t(4));
    QCOMPARE(images.size(), size_t(1));
    QCOMPARE(forms.size(), size_t(1));
    const pdf::PDFContentNodeReference a = paths[0];
    const pdf::PDFContentNodeReference b = paths[1];
    const pdf::PDFContentNodeReference c = paths[2];
    const pdf::PDFContentNodeReference d = paths[3];
    const pdf::PDFContentNodeReference image = images[0];
    const pdf::PDFContentNodeReference formUse = forms[0];

    // As read: "q 2 0 0 2 10 20 cm 0 0 10 5 re f Q" maps (0 0)-(10 5) to (10 20)-(30 30); the group of C and D is
    // their union; the image is the unit square under "20 0 0 10 150 150 cm"; the form use is /BBox [0 0 40 40] under
    // /Matrix [2 0 0 2 0 0] and "1 0 0 1 50 0 cm"; the root is the union of everything
    QVERIFY(H::isSame(G::getPageBoundingBox(context, a), QRectF(10, 20, 20, 10)));
    QVERIFY(H::isSame(G::getPageBoundingBox(context, groups[0]), QRectF(10, 20, 20, 10)));
    QVERIFY(H::isSame(G::getPageBoundingBox(context, b), QRectF(100, 100, 20, 20)));
    QVERIFY(H::isSame(G::getPageBoundingBox(context, c), QRectF(30, 30, 10, 10)));
    QVERIFY(H::isSame(G::getPageBoundingBox(context, groups[1]), QRectF(30, 30, 25, 35)));
    QVERIFY(H::isSame(G::getPageBoundingBox(context, image), QRectF(150, 150, 20, 10)));
    QVERIFY(H::isSame(G::getPageBoundingBox(context, formUse), QRectF(50, 0, 80, 80)));
    QVERIFY(H::isSame(G::getNodeBoundingBox(*page, page->getRoot()->getId()), QRectF(10, 0, 160, 160)));
    QVERIFY(G::getPageBoundingBox(context, H::node(pageKey, 999999)).isNull());
    QVERIFY(G::getPageBoundingBox(nullptr, a).isNull());

    // A transformed path (exactly: its points under the matrix of its override), a transformed image (its box under
    // D: a rotation by 90 degrees maps (x, y) to (-y, x)), a transformed group (its leaves), a moved form use
    QVERIFY(context->execute(F::createTransform({ a }, QTransform(2, 0, 0, 2, 0, 0))));
    QVERIFY(context->execute(F::createTransform({ image }, QTransform().rotate(90))));
    QVERIFY(context->execute(F::createTransform({ groups[1] }, QTransform::fromTranslate(10, 0))));
    QVERIFY(context->execute(F::createTransform({ formUse }, QTransform::fromTranslate(0, 10))));
    const pdf::PDFContentContainerPointer moved = context->getContainer();
    QVERIFY(moved && moved != page);
    QVERIFY(H::isSame(moved->getNode(a.node)->getBoundingBox(), QRectF(10, 20, 20, 10)));     // the box of the builder stays
    QVERIFY(H::isSame(G::getPageBoundingBox(context, a), QRectF(20, 40, 40, 20)));
    QVERIFY(H::isSame(G::getPageBoundingBox(context, groups[0]), QRectF(20, 40, 40, 20)));
    QVERIFY(H::isSame(G::getPageBoundingBox(context, image), QRectF(-160, 150, 10, 20)));
    QVERIFY(H::isSame(G::getPageBoundingBox(context, c), QRectF(40, 30, 10, 10)));
    QVERIFY(H::isSame(G::getPageBoundingBox(context, groups[1]), QRectF(40, 30, 25, 35)));
    QVERIFY(H::isSame(G::getPageBoundingBox(context, formUse), QRectF(50, 10, 80, 80)));
    QVERIFY(H::isSame(G::getPageBoundingBox(context, b), QRectF(100, 100, 20, 20)));

    // The selection of two nodes: the union of their boxes; nothing selected: none
    pdf::PDFAppEditContentSelection* selection = context->getSelection();
    QVERIFY(selection->getBoundingBox().isNull());
    selection->setNodes({ b, image });
    QVERIFY(H::isSame(selection->getBoundingBox(), QRectF(-160, 100, 280, 70)));
    selection->setNodes({ a, d });
    QVERIFY(H::isSame(selection->getBoundingBox(), QRectF(20, 40, 45, 25)));     // (20 40)-(60 60) and (60 60)-(65 65)

    // A node of the form inside of the isolation: "0 0 10 10 re f" of the form under /Matrix [2 0 0 2 0 0], the use
    // "1 0 0 1 50 0 cm" and the move by (0 10)
    const pdf::PDFContentContainerKey formKey = pdf::PDFContentContainerKey::createForm(moved->getNode(formUse.node)->getData().xobject);
    QVERIFY(!G::getContainerToPageMatrix(*context->getSession(), formKey, context->getIsolationPath()));
    QVERIFY(G::getContainerToPageMatrix(*context->getSession(), pageKey, context->getIsolationPath())->isIdentity());
    pdf::PDFAppEditContentIsolationLevel level;
    level.root.container = formKey;
    level.entry = formUse;
    QSignalSpy loaded(context.data(), &Context::containerChanged);
    context->setIsolationPath({ level });
    QVERIFY(context->getContainer() || loaded.wait(H::timeout(15000)));
    const pdf::PDFContentContainerPointer form = context->getContainer();
    QVERIFY(form && form->getKey() == formKey);
    const std::vector<pdf::PDFContentNodeReference> formPaths = H::findNodes(form, Kind::Path);
    QCOMPARE(formPaths.size(), size_t(1));
    QVERIFY(H::isSame(G::getNodeBoundingBox(*form, formPaths[0].node), QRectF(0, 0, 10, 10)));
    QVERIFY(H::isSame(G::getPageBoundingBox(context, formPaths[0]), QRectF(50, 10, 20, 20)));
    selection->setNode(formPaths[0]);
    QVERIFY(H::isSame(selection->getBoundingBox(), QRectF(50, 10, 20, 20)));
}

void AppToolContentSkeletonTest::contextPages()
{
    // requestPage only asks; setPage shows the page (range, no repetition), the container loads in a worker and the
    // edited root is its root afterwards (pdfappeditcontentcontext.h)
    using H = ContentSkeletonHelper;
    Registry registry;
    QPointer<Context> context = new Context(H::createDocument(2), pdf::PDFCMSPointer(), &registry, nullptr);
    auto cleanup = qScopeGuard([&context]() { ContentSkeletonHelper::finish(context); });

    QCOMPARE(context->getPageCount(), pdf::PDFInteger(2));
    QCOMPARE(context->getPageIndex(), pdf::PDFInteger(-1));
    QVERIFY(!context->getPageKey().isValid());
    QVERIFY(!context->getEditedRoot().container.isValid());
    QVERIFY(!context->getContainer());
    QVERIFY(!context->isEditingAllowed());

    QStringList emitted;
    QSignalSpy requested(context.data(), &Context::pageRequested);
    QObject::connect(context.data(), &Context::pageChanged, context.data(), [&emitted](pdf::PDFInteger pageIndex) { emitted <<QStringLiteral("pageChanged %1").arg(pageIndex); });
    QObject::connect(context.data(), &Context::editingAllowedChanged, context.data(), [&emitted]() { emitted <<QStringLiteral("editingAllowedChanged"); });
    QObject::connect(context.data(), &Context::containerChanged, context.data(), [&emitted]() { emitted <<QStringLiteral("containerChanged"); });

    context->requestPage(2);
    context->requestPage(-1);
    QCOMPARE(requested.size(), 0);
    context->requestPage(1);
    QCOMPARE(requested.size(), 1);
    QCOMPARE(requested.at(0).at(0).value<pdf::PDFInteger>(), pdf::PDFInteger(1));
    QCOMPARE(context->getPageIndex(), pdf::PDFInteger(-1));
    QVERIFY(emitted.isEmpty());

    context->setPage(2);
    QVERIFY(emitted.isEmpty());
    context->setPage(1);
    QCOMPARE(emitted,QStringList({ "pageChanged 1", "editingAllowedChanged" }));
    QCOMPARE(context->getPageIndex(), pdf::PDFInteger(1));
    QVERIFY(context->getPageKey().isValid());
    QCOMPARE(context->getPageKey().pageIndex, pdf::PDFInteger(1));
    QVERIFY(context->getPageKey() == context->getSession()->getPageKey(1));
    QVERIFY(context->getEditedRoot().container == context->getPageKey());

    // Loading: no container, no node of the root, no editing
    QVERIFY(!context->getContainer());
    QVERIFY(!context->getEditedRoot().node.isValid());
    QVERIFY(!context->isEditingAllowed());

    QSignalSpy loaded(context.data(), &Context::containerChanged);
    QVERIFY(loaded.wait(H::timeout(15000)));
    QVERIFY(context->getContainer());
    QVERIFY(context->getEditedRoot().node == context->getContainer()->getRoot()->getId());
    QVERIFY(context->getEditedRoot().node.isValid());
    QVERIFY(context->isEditingAllowed());
    QCOMPARE(emitted,QStringList({ "pageChanged 1", "editingAllowedChanged", "containerChanged", "editingAllowedChanged" }));
    QCOMPARE(H::getPaths(context).size(), size_t(2));
    QVERIFY(context->getIsolationPath().empty());

    // The page shown already, an index out of range: nothing
    context->setPage(1);
    context->setPage(5);
    QCOMPARE(emitted.size(), 4);
}

void AppToolContentSkeletonTest::contextEditingAllowed()
{
    // isEditingAllowed: a page with a loaded container, Preview off, no run (pdfappeditcontentcontext.h)
    using H = ContentSkeletonHelper;
    Registry registry;
    QPointer<Context> context = new Context(H::createDocument(1), pdf::PDFCMSPointer(), &registry, nullptr);
    auto cleanup = qScopeGuard([&context]() { ContentSkeletonHelper::finish(context); });
    QVERIFY(H::showPage(context, 0));
    QVERIFY(context->isEditingAllowed());

    QSignalSpy allowed(context.data(), &Context::editingAllowedChanged);
    QSignalSpy preview(context.data(), &Context::previewActiveChanged);
    context->setPreviewActive(true);
    QVERIFY(context->isPreviewActive());
    QVERIFY(!context->isEditingAllowed());
    QCOMPARE(preview.size(), 1);
    QCOMPARE(preview.at(0).at(0).toBool(), true);
    QCOMPARE(allowed.size(), 1);
    context->setPreviewActive(true);
    QCOMPARE(preview.size(), 1);
    QCOMPARE(allowed.size(), 1);
    context->setPreviewActive(false);
    QVERIFY(context->isEditingAllowed());
    QCOMPARE(allowed.size(), 2);

    context->setRunning(true);
    QVERIFY(context->isRunning());
    QVERIFY(!context->isEditingAllowed());
    QCOMPARE(allowed.size(), 3);
    context->setRunning(true);
    QCOMPARE(allowed.size(), 3);
    context->setRunning(false);
    QVERIFY(context->isEditingAllowed());
    QCOMPARE(allowed.size(), 4);
}

void AppToolContentSkeletonTest::contextExecute()
{
    // execute: a real command of the core is applied (one undo step); a refused one leaves the model and tells its
    // text; a null command and a command while the editing is not allowed are refused without the session
    using H = ContentSkeletonHelper;
    using F = pdf::PDFContentCommandFactory;
    Registry registry;
    QPointer<Context> context = new Context(H::createDocument(1), pdf::PDFCMSPointer(), &registry, nullptr);
    auto cleanup = qScopeGuard([&context]() { ContentSkeletonHelper::finish(context); });
    QVERIFY(H::showPage(context, 0));
    const std::vector<pdf::PDFContentNodeReference> paths = H::getPaths(context);
    QCOMPARE(paths.size(), size_t(2));
    QSignalSpy refused(context.data(), &Context::commandRefused);
    const pdf::PDFContentUndoStack* undoStack = context->getSession()->getUndoStack();

    const pdf::PDFContentCommandPointer deleteFirst = F::createDelete({ paths[0] });
    QVERIFY(context->execute(deleteFirst));
    QVERIFY(context->getLastRefusalText().isEmpty());
    QVERIFY(H::getPaths(context) == std::vector<pdf::PDFContentNodeReference>({ paths[1] }));
    QVERIFY(undoStack->canUndo());
    QCOMPARE(undoStack->getUndoText(), deleteFirst->getText());
    QCOMPARE(refused.size(), 0);

    // A node, which does not exist: refused with its text, the model and the undo stack unchanged
    QVERIFY(!context->execute(F::createDelete({ H::node(context->getPageKey(), 999999) })));
    QCOMPARE(refused.size(), 1);
    const QString text = refused.at(0).at(0).toString();
    QVERIFY(!text.isEmpty());
    QCOMPARE(context->getLastRefusalText(), text);
    QCOMPARE(H::getPaths(context).size(), size_t(1));
    QCOMPARE(undoStack->getUndoText(), deleteFirst->getText());

    // Refused without the session: no signal, the text of the last refusal stays
    QVERIFY(!context->execute(pdf::PDFContentCommandPointer()));
    context->setPreviewActive(true);
    QVERIFY(!context->execute(F::createDelete({ paths[1] })));
    context->setPreviewActive(false);
    QCOMPARE(refused.size(), 1);
    QCOMPARE(H::getPaths(context).size(), size_t(1));
    QCOMPARE(context->getLastRefusalText(), text);

    // An applied command clears the text of the refusal
    QVERIFY(context->execute(F::createDelete({ paths[1] })));
    QVERIFY(context->getLastRefusalText().isEmpty());
    QVERIFY(H::getPaths(context).empty());
}

void AppToolContentSkeletonTest::contextRequestExecute()
{
    // requestExecute runs the commands from the event loop, never inside of the call, in the order of the requests
    using H = ContentSkeletonHelper;
    Registry registry;
    QPointer<Context> context = new Context(H::createDocument(1), pdf::PDFCMSPointer(), &registry, nullptr);
    auto cleanup = qScopeGuard([&context]() { ContentSkeletonHelper::finish(context); });
    QVERIFY(H::showPage(context, 0));

    QStringList log;
    context->requestExecute(std::make_shared<ContentSkeletonRecordingCommand>(QStringLiteral("first"), &log));
    context->requestExecute(pdf::PDFContentCommandPointer());
    context->requestExecute(std::make_shared<ContentSkeletonRecordingCommand>(QStringLiteral("second"), &log));
    context->requestExecute(std::make_shared<ContentSkeletonRecordingCommand>(QStringLiteral("third"), &log));
    QVERIFY(log.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(log, QStringList({ "first", "second", "third" }), H::timeout(5000));
    QCOMPARE(context->getSession()->getUndoStack()->getUndoText(), QStringLiteral("third"));
}

void AppToolContentSkeletonTest::contextMacroIsOneStep()
{
    // The commands between beginMacro and endMacro are one undo step of the text; nested macros count
    using H = ContentSkeletonHelper;
    using F = pdf::PDFContentCommandFactory;
    Registry registry;
    QPointer<Context> context = new Context(H::createDocument(1), pdf::PDFCMSPointer(), &registry, nullptr);
    auto cleanup = qScopeGuard([&context]() { ContentSkeletonHelper::finish(context); });
    QVERIFY(H::showPage(context, 0));
    const std::vector<pdf::PDFContentNodeReference> paths = H::getPaths(context);
    QCOMPARE(paths.size(), size_t(2));
    pdf::PDFContentUndoStack* undoStack = context->getSession()->getUndoStack();

    context->beginMacro(QStringLiteral("Delete Two Objects"));
    context->beginMacro(QStringLiteral("Inner"));
    QVERIFY(context->execute(F::createDelete({ paths[0] })));
    context->endMacro();
    QVERIFY(undoStack->isMacroOpen());
    QVERIFY(context->execute(F::createDelete({ paths[1] })));
    context->endMacro();
    QVERIFY(!undoStack->isMacroOpen());
    context->endMacro();
    QVERIFY(!undoStack->isMacroOpen());
    QVERIFY(H::getPaths(context).empty());
    QCOMPARE(undoStack->getUndoText(), QStringLiteral("Delete Two Objects"));

    // One undo brings both objects back and empties the stack
    QVERIFY(context->getSession()->undo().has_value());
    QCOMPARE(H::getPaths(context).size(), size_t(2));
    QVERIFY(!undoStack->canUndo());
}

void AppToolContentSkeletonTest::contextTriggerAction()
{
    // triggerAction queues the action: it runs from the event loop in the order of the requests, only when it is
    // visible and enabled then; an unknown action is refused at once
    using H = ContentSkeletonHelper;
    ContentSkeletonActionRecorder recorder;
    Registry registry;
    QVERIFY(registry.addAction(std::make_unique<ContentSkeletonActionEntry>(QStringLiteral("act"), std::vector<ActionEntry::Key>(), std::vector<ActionEntry::Placement>(), &recorder)));
    QVERIFY(registry.addAction(std::make_unique<ContentSkeletonActionEntry>(QStringLiteral("second"), std::vector<ActionEntry::Key>(), std::vector<ActionEntry::Placement>(), &recorder)));
    QPointer<Context> context = new Context(H::createDocument(1), pdf::PDFCMSPointer(), &registry, nullptr);
    auto cleanup = qScopeGuard([&context]() { ContentSkeletonHelper::finish(context); });

    QVERIFY(!context->triggerAction(QStringLiteral("unknown")));
    QVERIFY(context->triggerAction(QStringLiteral("second")));
    QVERIFY(context->triggerAction(QStringLiteral("act"), QStringLiteral("c1")));
    QVERIFY(context->triggerAction(QStringLiteral("act")));
    QVERIFY(recorder.log.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(recorder.log, QStringList({ "second", "act:c1", "act" }), H::timeout(5000));

    // Disabled or hidden, when the queue runs: nothing
    recorder.log.clear();
    QVERIFY(context->triggerAction(QStringLiteral("act")));
    recorder.isEnabled = false;
    QCoreApplication::processEvents();
    QVERIFY(context->triggerAction(QStringLiteral("act")));
    recorder.isEnabled = true;
    recorder.isVisible = false;
    QCoreApplication::processEvents();
    QVERIFY(recorder.log.isEmpty());
    recorder.isVisible = true;
    QVERIFY(context->triggerAction(QStringLiteral("act")));
    QTRY_COMPARE_WITH_TIMEOUT(recorder.log, QStringList({ "act" }), H::timeout(5000));
}

void AppToolContentSkeletonTest::contextApplicationCommands()
{
    // pdfappeditcontentcontext.h (R3-1, zadani_gui.md 4.20 way 2): a command of the application is the tool's, when an
    // action entry answers it - Enabled while the entry is visible and enabled (by default: the editing is allowed),
    // Disabled otherwise; NotHandled without an entry ("hidden, not disabled") and for the commands of the shell. The
    // handling is queued and runs the entry from the event loop after the state was asked again.
    using E = ContentSkeletonCommandEntry;
    using H = ContentSkeletonHelper;
    using State = pdf::PDFAppToolCommandState;
    ContentSkeletonCommandRecorder recorder;
    ContentSkeletonCommandRecorder hidden;
    hidden.isVisible = false;
    Registry registry;
    QVERIFY(registry.addAction(std::make_unique<E>(QStringLiteral("select-all"), QStringList({ "edit.selectAll", "edit.delete" }), &recorder)));
    QVERIFY(registry.addAction(std::make_unique<E>(QStringLiteral("find"), QStringList({ "search.find" }), &hidden)));
    QVERIFY(registry.addAction(std::make_unique<E>(QStringLiteral("undo"), QStringList({ "edit.undo", "file.save" }), &recorder)));
    QPointer<Context> context = new Context(H::createDocument(1), pdf::PDFCMSPointer(), &registry, nullptr);
    auto cleanup = qScopeGuard([&context]() { ContentSkeletonHelper::finish(context); });
    const QString selectAll = QStringLiteral("edit.selectAll");

    // No page yet: the editing is not allowed - the command is the tool's, disabled; nothing is queued
    QCOMPARE(context->getApplicationCommandState(selectAll), State::Disabled);
    QVERIFY(!context->handleApplicationCommand(selectAll));

    QVERIFY(H::showPage(context, 0));
    QCOMPARE(context->getApplicationCommandState(selectAll), State::Enabled);
    QCOMPARE(context->getApplicationCommandState(QStringLiteral("edit.delete")), State::Enabled);
    QCOMPARE(context->getApplicationCommandState(QStringLiteral("search.find")), State::Disabled);
    QCOMPARE(context->getApplicationCommandState(QStringLiteral("edit.copy")), State::NotHandled);
    QCOMPARE(context->getApplicationCommandState(QStringLiteral("edit.undo")), State::NotHandled);
    QCOMPARE(context->getApplicationCommandState(QStringLiteral("file.save")), State::NotHandled);
    QCOMPARE(context->getApplicationCommandState(QString()), State::NotHandled);

    // Handled from the event loop, in the order of the requests
    QVERIFY(context->handleApplicationCommand(selectAll));
    QVERIFY(context->handleApplicationCommand(QStringLiteral("edit.delete")));
    QVERIFY(!context->handleApplicationCommand(QStringLiteral("search.find")));
    QVERIFY(!context->handleApplicationCommand(QStringLiteral("edit.copy")));
    QVERIFY(!context->handleApplicationCommand(QStringLiteral("edit.undo")));
    QVERIFY(recorder.log.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(recorder.log, QStringList({ "select-all", "select-all" }), H::timeout(5000));
    QVERIFY(hidden.log.isEmpty());

    // The state is asked again when the queue runs: Preview on meanwhile - nothing
    recorder.log.clear();
    QVERIFY(context->handleApplicationCommand(selectAll));
    context->setPreviewActive(true);
    QCOMPARE(context->getApplicationCommandState(selectAll), State::Disabled);
    QCoreApplication::processEvents();
    QVERIFY(recorder.log.isEmpty());
    context->setPreviewActive(false);

    // Retired: disabled, never queued
    QVERIFY(context->handleApplicationCommand(selectAll));
    pdf::PDFDeferredDeletion::retire(context.data());
    QCOMPARE(context->getApplicationCommandState(selectAll), State::Disabled);
    QVERIFY(!context->handleApplicationCommand(selectAll));
    QCoreApplication::processEvents();
    QVERIFY(recorder.log.isEmpty());
}

void AppToolContentSkeletonTest::contextCardsAndRequests()
{
    // The requests answered by another package are signals of the context (pe1_skeleton_notes.md section 3)
    using H = ContentSkeletonHelper;
    using C = pdf::PDFAppEditContentCardIds;
    Registry registry;
    QPointer<Context> context = new Context(H::createDocument(1), pdf::PDFCMSPointer(), &registry, nullptr);
    auto cleanup = qScopeGuard([&context]() { ContentSkeletonHelper::finish(context); });
    const pdf::PDFContentNodeReference node = H::node(context->getSession()->getPageKey(0), 5);

    // Requested cards and the node of Locked object
    QSignalSpy cards(context.data(), &Context::cardsChanged);
    const QString keyboard = QString::fromLatin1(C::KEYBOARD);
    context->setCardRequested(keyboard, true);
    QVERIFY(context->isCardRequested(keyboard));
    QCOMPARE(cards.size(), 1);
    context->setCardRequested(keyboard, true);
    context->setCardRequested(QString(), true);
    QCOMPARE(cards.size(), 1);
    context->setCardRequested(keyboard, false);
    QVERIFY(!context->isCardRequested(keyboard));
    QCOMPARE(cards.size(), 2);
    context->setLockedClickNode(node);
    QVERIFY(context->getLockedClickNode() == node);
    QCOMPARE(cards.size(), 3);
    context->setLockedClickNode(node);
    QCOMPARE(cards.size(), 3);

    // The expanded state of a section: no signal
    context->setSectionExpanded(QStringLiteral("arrange/actions"), true);
    QVERIFY(context->isSectionExpanded(QStringLiteral("arrange/actions")));
    QVERIFY(!context->isSectionExpanded(QStringLiteral("arrange")));
    context->setSectionExpanded(QStringLiteral("arrange/actions"), false);
    QVERIFY(!context->isSectionExpanded(QStringLiteral("arrange/actions")));
    QCOMPARE(cards.size(), 3);

    // Notices
    QSignalSpy notice(context.data(), &Context::noticeRequested);
    QSignalSpy noticeHide(context.data(), &Context::noticeHideRequested);
    pdf::PDFAppEditContentNoticeRequest request;
    request.id = QStringLiteral("preview");
    request.kind = pdf::PDFAppEditContentNoticeRequest::Kind::State;
    request.text = QStringLiteral("Preview is on.");
    context->showNotice(request);
    QCOMPARE(notice.size(), 1);
    QVERIFY(notice.at(0).at(0).value<pdf::PDFAppEditContentNoticeRequest>() == request);
    pdf::PDFAppEditContentNoticeRequest withoutText = request;
    withoutText.text.clear();
    context->showNotice(withoutText);
    QCOMPARE(notice.size(), 1);
    context->hideNotice(QStringLiteral("preview"));
    context->hideNotice(QString());
    QCOMPARE(noticeHide.size(), 1);
    QCOMPARE(noticeHide.at(0).at(0).toString(), QStringLiteral("preview"));

    // Context menu, pending edits, isolation
    // The menu of the request is named (R3-10): the context menu, a row of Layers / Structure of later stages
    QSignalSpy menu(context.data(), &Context::contextMenuRequested);
    context->requestContextMenu(QString::fromLatin1(pdf::PDFAppEditContentMenuIds::CONTEXT), QPoint(10, 20));
    context->requestContextMenu(QString(), QPoint(1, 1));
    context->requestContextMenu(QString::fromLatin1(pdf::PDFAppEditContentMenuIds::LAYER_ROW), QPoint(30, 40));
    QCOMPARE(menu.size(), 2);
    QCOMPARE(menu.at(0).at(0).toString(), QStringLiteral("context"));
    QCOMPARE(menu.at(0).at(1).toPoint(), QPoint(10, 20));
    QCOMPARE(menu.at(1).at(0).toString(), QStringLiteral("layer-row"));
    QCOMPARE(menu.at(1).at(1).toPoint(), QPoint(30, 40));
    QSignalSpy commit(context.data(), &Context::pendingEditsCommitRequested);
    QSignalSpy discard(context.data(), &Context::pendingEditsDiscardRequested);
    context->commitPendingEdits();
    QCOMPARE(commit.size(), 1);
    QCOMPARE(discard.size(), 0);
    context->discardPendingEdits();
    QCOMPARE(discard.size(), 1);
    QSignalSpy isolation(context.data(), &Context::isolationRequested);
    QSignalSpy leave(context.data(), &Context::leaveIsolationRequested);
    context->requestIsolation(pdf::PDFContentNodeReference());
    context->requestIsolation(node);
    QCOMPARE(isolation.size(), 1);
    context->requestLeaveIsolation(1);
    QCOMPARE(leave.size(), 0);
}

void AppToolContentSkeletonTest::contextChildren()
{
    // The children of the context (pdfappeditcontentcontext.h): the session, the options, the selection, the
    // controller with every tool of the registry, the preview, the state of each tab entry, which has one
    using H = ContentSkeletonHelper;
    Registry registry;
    QVERIFY(registry.addTool(std::make_unique<ContentSkeletonToolEntry>(QStringLiteral("select"), ToolEntry::Place::Toolbar, 100, QKeySequence(Qt::Key_V))));
    QVERIFY(registry.addTool(std::make_unique<ContentSkeletonToolEntry>(QStringLiteral("nodes"), ToolEntry::Place::Mode, 100)));
    QVERIFY(registry.addTab(std::make_unique<ContentSkeletonTabEntry>(QStringLiteral("objects"), 100, true)));
    QVERIFY(registry.addTab(std::make_unique<ContentSkeletonTabEntry>(QStringLiteral("pages"), 200, false)));
    QPointer<Context> context = new Context(H::createDocument(1), pdf::PDFCMSPointer(), &registry, nullptr);
    auto cleanup = qScopeGuard([&context]() { ContentSkeletonHelper::finish(context); });

    QCOMPARE(context->getRegistry(), &registry);
    const std::vector<QObject*> children = { static_cast<QObject*>(context->getSession()), static_cast<QObject*>(context->getOptions()), static_cast<QObject*>(context->getSelection()),
                                             static_cast<QObject*>(context->getToolController()), static_cast<QObject*>(context->getPreview()) };
    for (QObject* child : children)
    {
        QVERIFY(child);
        QCOMPARE(child->parent(), static_cast<QObject*>(context.data()));
    }

    QObject* objects = context->getTabState(QStringLiteral("objects"));
    QVERIFY(objects);
    QCOMPARE(objects->objectName(), QStringLiteral("objects"));
    QCOMPARE(objects->parent(), static_cast<QObject*>(context.data()));
    QVERIFY(!context->getTabState(QStringLiteral("pages")));
    QVERIFY(!context->getTabState(QStringLiteral("layers")));

    pdf::PDFAppEditContentToolController* controller = context->getToolController();
    for (const char* toolId : { "select", "nodes" })
    {
        pdf::PDFAppEditContentTool* tool = controller->getTool(QString::fromLatin1(toolId));
        QVERIFY2(tool, toolId);
        QCOMPARE(tool->getController(), controller);
        QCOMPARE(tool->getContext(), context.data());
    }
    QVERIFY(!controller->getTool(QStringLiteral("text")));
    QVERIFY(controller->getActiveToolId().isEmpty());
    QVERIFY(!controller->getActiveTool());
    QVERIFY(!controller->getActiveToolEntry());
    QVERIFY(!controller->isMultiple());

    // The canvas and the GUI are not owned: none in a test
    QVERIFY(!context->getGUI());
    QVERIFY(!context->getCanvas());
    QVERIFY(!context->getScene());
    QVERIFY(!context->getView());
    QVERIFY(!context->getDialogParent());

    // The registry of the application is the default
    QPointer<Context> applicationContext = new Context(H::createDocument(1), pdf::PDFCMSPointer(), nullptr, nullptr);
    auto applicationCleanup = qScopeGuard([&applicationContext]() { ContentSkeletonHelper::finish(applicationContext); });
    QCOMPARE(applicationContext->getRegistry(), &Registry::getInstance());
    QVERIFY(qobject_cast<pdf::PDFAppEditContentObjectsModel*>(applicationContext->getTabState(QStringLiteral("objects"))));
    QVERIFY(!applicationContext->getTabState(QStringLiteral("pages")));
}

void AppToolContentSkeletonTest::contextChildrenCreatedAndDeletedInOrder()
{
    // pdfappeditcontentcontext.h (R3-8, R3-3): the tools are created last, so a tool finds the controller, its own
    // controller, the preview and the tab states in its constructor; the destructor deletes the children in the
    // reverse order, so a tab state still finds the session, the selection, the controller and the preview, a tool the
    // session, the options and the selection - also when the context is deleted synchronously (order (2) of
    // zadani_gui.md 4.14: the tab goes, no retire hook ran)
    using H = ContentSkeletonHelper;
    for (const bool isRetiredFirst : { false, true })
    {
        ContentSkeletonLifeRecorder recorder;
        Registry registry;
        QVERIFY(registry.addTool(std::make_unique<ContentSkeletonProbeToolEntry>(&recorder)));
        QVERIFY(registry.addTab(std::make_unique<ContentSkeletonProbeTabEntry>(&recorder)));
        QPointer<Context> context = new Context(H::createDocument(1), pdf::PDFCMSPointer(), &registry, nullptr);
        QCOMPARE(recorder.log, QStringList({ "tool: controller yes, own controller yes, preview yes, tab state yes" }));
        QVERIFY(H::showPage(context, 0));

        if (isRetiredFirst)
        {
            pdf::PDFDeferredDeletion::retire(context.data());
            pdf::PDFDeferredDeletion::flush();
        }
        else
        {
            delete context.data();
        }
        QVERIFY(!context);
        QCOMPARE(recorder.log, QStringList({ "tool: controller yes, own controller yes, preview yes, tab state yes",
                                             "~state: session yes, selection yes, controller yes, preview yes",
                                             "~tool: session yes, options yes, selection yes" }));
    }
}

void AppToolContentSkeletonTest::contextRetireDropsQueues()
{
    // The hook retire: the queued commands and actions never run, an open macro is closed, nothing is emitted, no new
    // request is taken
    using H = ContentSkeletonHelper;
    ContentSkeletonActionRecorder recorder;
    Registry registry;
    QVERIFY(registry.addAction(std::make_unique<ContentSkeletonActionEntry>(QStringLiteral("act"), std::vector<ActionEntry::Key>(), std::vector<ActionEntry::Placement>(), &recorder)));
    QPointer<Context> context = new Context(H::createDocument(1), pdf::PDFCMSPointer(), &registry, nullptr);
    auto cleanup = qScopeGuard([&context]() { ContentSkeletonHelper::finish(context); });
    QVERIFY(H::showPage(context, 0));

    QStringList log;
    context->requestExecute(std::make_shared<ContentSkeletonRecordingCommand>(QStringLiteral("queued"), &log));
    QVERIFY(context->triggerAction(QStringLiteral("act")));
    context->beginMacro(QStringLiteral("Open"));
    const pdf::PDFContentUndoStack* undoStack = context->getSession()->getUndoStack();
    QVERIFY(undoStack->isMacroOpen());

    QSignalSpy notice(context.data(), &Context::noticeRequested);
    pdf::PDFDeferredDeletion::retire(context.data());
    QVERIFY(context);
    QVERIFY(!undoStack->isMacroOpen());
    QVERIFY(!context->triggerAction(QStringLiteral("act")));
    context->requestExecute(std::make_shared<ContentSkeletonRecordingCommand>(QStringLiteral("late"), &log));
    pdf::PDFAppEditContentNoticeRequest request;
    request.id = QStringLiteral("preview");
    request.text = QStringLiteral("Preview is on.");
    context->showNotice(request);
    QVERIFY(!context->isEditingAllowed());

    QCoreApplication::processEvents();
    QVERIFY(log.isEmpty());
    QVERIFY(recorder.log.isEmpty());
    QCOMPARE(notice.size(), 0);
    pdf::PDFDeferredDeletion::flush();
    QVERIFY(!context);
}

void AppToolContentSkeletonTest::contextChangeableSelection()
{
    // R7-4 (QE.8, RE-16): the one rule of the lock - the selected nodes a command may change are the nodes of the edited
    // container inside of the edited root, not the root, without a locked node on the way up (a locked group locks what
    // it holds), in the order of the selection. The geometry page: a in a group, b, c and d in the group G.
    using H = ContentSkeletonHelper;
    using Kind = pdf::PDFContentNodeKind;
    Registry registry;
    QPointer<Context> context = new Context(H::createGeometryDocument(), pdf::PDFCMSPointer(), &registry, nullptr);
    auto cleanup = qScopeGuard([&context]() { ContentSkeletonHelper::finish(context); });
    QVERIFY(H::showPage(context, 0));
    const pdf::PDFContentContainerPointer page = context->getContainer();
    const std::vector<pdf::PDFContentNodeReference> paths = H::findNodes(page, Kind::Path);
    const std::vector<pdf::PDFContentNodeReference> groups = H::findNodes(page, Kind::Group);
    QCOMPARE(paths.size(), size_t(4));
    const pdf::PDFContentNodeReference a = paths[0];
    const pdf::PDFContentNodeReference b = paths[1];
    const pdf::PDFContentNodeReference c = paths[2];
    const pdf::PDFContentNodeReference d = paths[3];
    const pdf::PDFContentNodeReference g = groups[1];
    const pdf::PDFContentNodeReference root = H::node(page->getKey(), page->getRoot()->getId().value);

    pdf::PDFAppEditContentSelection* selection = context->getSelection();
    selection->setNodes({ c, a, b, root });
    QCOMPARE(context->getChangeableSelection(), std::vector<pdf::PDFContentNodeReference>({ c, a, b }));
    QVERIFY(!context->isNodeChangeable(root));

    pdf::PDFContentEditSession* session = context->getSession();
    session->setNodeLocked(b, true);
    session->setNodeLocked(g, true);
    QVERIFY(!context->isNodeChangeable(b));
    QVERIFY(!context->isNodeChangeable(c));
    QVERIFY(!context->isNodeChangeable(d));
    QVERIFY(!context->isNodeChangeable(g));
    QVERIFY(context->isNodeChangeable(a));
    QCOMPARE(context->getChangeableSelection(), std::vector<pdf::PDFContentNodeReference>({ a }));

    session->setNodeLocked(g, false);
    QCOMPARE(context->getChangeableSelection(), std::vector<pdf::PDFContentNodeReference>({ c, a }));
    QVERIFY(!context->isNodeChangeable(H::node(page->getKey(), 999999)));
    QVERIFY(!context->isNodeChangeable(pdf::PDFContentNodeReference()));
}

void AppToolContentSkeletonTest::contextComplexPagePolicy()
{
    // Integration row 27, zadani.md QE.10: one policy of the complex page for the canvas and the notice - more than
    // COMPLEX_PAGE_OBJECTS nodes or more than COMPLEX_PAGE_PATH_SEGMENTS segments
    using Scene = pdf::PDFAppEditContentScene;
    pdf::PDFContentDisplayCounts counts;
    QVERIFY(!Context::isComplexPage(counts));
    counts.nodeCount = Scene::COMPLEX_PAGE_OBJECTS;
    QVERIFY(!Context::isComplexPage(counts));
    counts.nodeCount = Scene::COMPLEX_PAGE_OBJECTS + 1;
    QVERIFY(Context::isComplexPage(counts));
    counts.nodeCount = 2;
    counts.pathSegmentCount = Scene::COMPLEX_PAGE_PATH_SEGMENTS;
    QVERIFY(!Context::isComplexPage(counts));
    counts.pathSegmentCount = Scene::COMPLEX_PAGE_PATH_SEGMENTS + 1;
    QVERIFY(Context::isComplexPage(counts));
}

void AppToolContentSkeletonTest::contextReloadDocument()
{
    // zadani_gui.md 4.21 (integration row 26): reloadDocument - the queued work of the old version dropped, the selection
    // and the node of Locked object cleared, a new session over the new version (no locks of the old one, a clean
    // stack), the page kept (clamped), sessionReplaced before pageChanged, the old session deleted from the event loop;
    // nothing for the same document, a null document or a retired context
    using H = ContentSkeletonHelper;
    using Kind = pdf::PDFContentNodeKind;
    ContentSkeletonActionRecorder recorder;
    Registry registry;
    QVERIFY(registry.addAction(std::make_unique<ContentSkeletonActionEntry>(QStringLiteral("act"), std::vector<ActionEntry::Key>(), std::vector<ActionEntry::Placement>(), &recorder)));
    QPointer<Context> context = new Context(H::createDocument(2), pdf::PDFCMSPointer(), &registry, nullptr);
    auto cleanup = qScopeGuard([&context]() { ContentSkeletonHelper::finish(context); });
    QVERIFY(H::showPage(context, 1));
    const std::vector<pdf::PDFContentNodeReference> paths = H::findNodes(context->getContainer(), Kind::Path);
    QCOMPARE(paths.size(), size_t(2));
    context->getSelection()->setNodes(paths);
    context->setLockedClickNode(paths.front());
    context->getSession()->setNodeLocked(paths.front(), true);
    QVERIFY(context->execute(pdf::PDFContentCommandFactory::createDelete({ paths.back() })));
    QStringList log;
    context->requestExecute(std::make_shared<ContentSkeletonRecordingCommand>(QStringLiteral("queued"), &log));
    QVERIFY(context->triggerAction(QStringLiteral("act")));

    QPointer<pdf::PDFContentEditSession> oldSession = context->getSession();
    QStringList order;
    QObject receiver;
    QObject::connect(context.data(), &Context::sessionReplaced, &receiver, [&]() { order << (context->getSession() != oldSession.data() ? QStringLiteral("session") : QStringLiteral("same")); });
    QObject::connect(context.data(), &Context::pageChanged, &receiver, [&](pdf::PDFInteger pageIndex) { order << QStringLiteral("page %1").arg(pageIndex); });

    const pdf::PDFDocumentPointer newDocument = H::createDocument(3);
    context->reloadDocument(newDocument);
    QCOMPARE(order, QStringList({ "session", "page 1" }));
    QVERIFY(context->getDocumentPointer() == newDocument);
    QCOMPARE(context->getSession()->getDocument(), newDocument.data());
    QCOMPARE(context->getPageIndex(), pdf::PDFInteger(1));
    QVERIFY(context->getSelection()->isEmpty());
    QVERIFY(!context->getLockedClickNode().isValid());
    QVERIFY(!context->getSession()->isNodeLocked(paths.front()));
    QCOMPARE(context->getSession()->getUndoStack()->getCount(), size_t(0));
    QVERIFY(oldSession);
    QTRY_VERIFY_WITH_TIMEOUT(!oldSession, H::timeout(5000));
    QTRY_VERIFY_WITH_TIMEOUT(context->getContainer() != nullptr, H::timeout(15000));
    QCOMPARE(H::findNodes(context->getContainer(), Kind::Path).size(), size_t(2));
    QVERIFY(log.isEmpty());
    QVERIFY(recorder.log.isEmpty());

    // Nothing for the same document and for a null one
    order.clear();
    context->reloadDocument(newDocument);
    context->reloadDocument(pdf::PDFDocumentPointer());
    QVERIFY(order.isEmpty());

    // A version of one page: the page is clamped
    context->reloadDocument(H::createDocument(1));
    QCOMPARE(order, QStringList({ "session", "page 0" }));
    QCOMPARE(context->getPageIndex(), pdf::PDFInteger(0));
}

void AppToolContentSkeletonTest::developerSwitch()
{
    // zadani.md 11.2 (Uvolnění), QE.13: the tool exists only with advanced.developerMode on. Off: as at 52deeff1 (the
    // catalog says not implemented, the routes are Page Not Found); on: registered; a tool open when the switch goes
    // off stays until it is closed, then its route is Page Not Found again.
    using H = ContentSkeletonHelper;
    pdftest::AppToolShellFixture fixture;
    QVERIFY(fixture.window);
    const QString toolId = QString::fromLatin1(H::TOOL_ID);
    QVERIFY(!pdf::PDFAppToolGUIContent::isContentToolRegistered());
    QVERIFY(H::isToolUnregistered());
    fixture.navigate(QStringLiteral("/tools/edit.content"));
    QVERIFY(fixture.notFoundPage());
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    QVERIFY(!fixture.openTool(tab, toolId));
    QVERIFY(fixture.notFoundPage());

    // On
    QVERIFY(H::setDeveloperMode(fixture.context.get(), true));
    QVERIFY(H::isToolRegistered());
    QPointer<pdf::PDFDocumentToolShell> shell = fixture.openTool(tab, toolId);
    QVERIFY(shell);
    QPointer<pdf::PDFDocumentToolGUI> gui = shell->getGUI();
    QVERIFY(qobject_cast<pdf::PDFAppToolGUIContent*>(gui.data()));

    // Off while the tool is open: the registration goes, the open tool stays
    QVERIFY(H::setDeveloperMode(fixture.context.get(), false));
    QVERIFY(H::isToolUnregistered());
    pdftest::AppToolShellFixture::settle();
    QVERIFY(shell && gui);
    QCOMPARE(shell->getState(), pdf::PDFDocumentToolShell::State::Editing);
    QCOMPARE(pdftest::AppToolShellFixture::getShell(tab), shell.data());

    // Closed, the route of the tool is Page Not Found again
    shell->cancel();
    QVERIFY(pdftest::AppToolShellFixture::waitFor([&shell]() { return !shell; }));
    QVERIFY(!fixture.openTool(tab, toolId));
    QVERIFY(fixture.notFoundPage());

    // On again: the tool opens again
    QVERIFY(H::setDeveloperMode(fixture.context.get(), true));
    fixture.navigate(QStringLiteral("/home"));
    QVERIFY(fixture.openTool(tab, toolId));
}

void AppToolContentSkeletonTest::developerSwitchFollowsStorage()
{
    // PDFAppContext: open applies the stored setting, close removes the tool (pdfapptoolgui_content.h)
    using H = ContentSkeletonHelper;
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    pdf::PDFDatabaseOptions options;
    options.filePath = directory.filePath(QStringLiteral("settings.db"));
    {
        pdf::PDFAppContext context(pdf::PDFAppCommandLine::parse({ QStringLiteral("PDF4QT") }, QDir::currentPath()));
        context.open(options);
        QVERIFY(context.getNotifier());
        context.getNotifier()->setPollInterval(20);
        QVERIFY(!pdf::PDFAppToolGUIContent::isContentToolRegistered());
        QVERIFY(H::setDeveloperMode(&context, true));
        QVERIFY(H::isToolRegistered());

        context.close();
        QVERIFY(!pdf::PDFAppToolGUIContent::isContentToolRegistered());
        QVERIFY(H::isToolUnregistered());

        // The stored value registers the tool at the opening
        context.open(options);
        QVERIFY(pdf::PDFAppToolGUIContent::isContentToolRegistered());
        QVERIFY(H::isToolRegistered());
        context.getNotifier()->setPollInterval(20);
        QVERIFY(H::setDeveloperMode(&context, false));
        QVERIFY(H::isToolUnregistered());
        QVERIFY(H::setDeveloperMode(&context, true));
    }
    QVERIFY(!pdf::PDFAppToolGUIContent::isContentToolRegistered());
    QVERIFY(H::isToolUnregistered());
}

void AppToolContentSkeletonTest::openingEndToEnd()
{
    // pe1skel.md S4, deliverable 7: with the switch on the tool opens over the stubs on a document - the three stub
    // parts are shown, Apply Changes is disabled, Undo is disabled, Cancel closes it without a question and the
    // view of the document is back
    using H = ContentSkeletonHelper;
    pdftest::AppToolShellFixture fixture;
    QVERIFY(fixture.window);
    QVERIFY(H::setDeveloperMode(fixture.context.get(), true));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    QPointer<pdf::PDFDocumentToolShell> shell = fixture.openTool(tab, QString::fromLatin1(H::TOOL_ID));
    QVERIFY(shell);
    QCOMPARE(shell->getState(), pdf::PDFDocumentToolShell::State::Editing);
    pdf::PDFAppToolGUIContent* gui = qobject_cast<pdf::PDFAppToolGUIContent*>(shell->getGUI());
    QVERIFY(gui);

    // The children: the context with the session of the document, the own undo target, notices, menus, keyboard
    QPointer<Context> context = gui->getEditContext();
    QVERIFY(context);
    QCOMPARE(context->parent(), static_cast<QObject*>(gui));
    QCOMPARE(context->getGUI(), static_cast<pdf::PDFDocumentToolGUI*>(gui));
    QCOMPARE(context->getRegistry(), &Registry::getInstance());
    QVERIFY(context->getSession()->getDocument() == tab->getDocument()->getDocument().data());
    QVERIFY(gui->getNotices() && gui->getMenus() && gui->getKeyboard());
    QVERIFY(qobject_cast<pdf::PDFAppEditContentUndoTarget*>(shell->getUndoTarget()));

    // The parts: the real ones of P11 - P14 (stage E1), no stub label of the skeleton is left
    for (const char* name : { "editContentCanvasStub", "editContentObjectsStub", "editContentPagesStub", "editContentCardsStub", "editContentActionsStub" })
    {
        QVERIFY2(!fixture.window->findChild<QLabel*>(QString::fromLatin1(name)), name);
    }
    QVERIFY(gui->getCanvas());
    QVERIFY(gui->getCanvas()->isVisible());
    QCOMPARE(context->getCanvas(), gui->getCanvas());
    QVERIFY(context->getView());
    QVERIFY(context->getScene());
    QVERIFY(gui->getCardStack() && gui->getLeftPanelWidget() && gui->getToolActionsWidget());
    QVERIFY(gui->getCardStack()->isVisible());
    QVERIFY(gui->getLeftPanelWidget()->isVisible());

    // The page of the view is shown; its container is built by the core
    QCOMPARE(context->getPageIndex(), pdf::PDFInteger(0));
    QVERIFY(pdftest::AppToolShellFixture::waitFor([&context]() { return !context || context->getContainer(); }));
    QVERIFY(context && context->getContainer());

    // Apply Changes disabled, Undo disabled, nothing to lose
    pdf::PDFAppToolToolbar* toolbar = shell->getToolbar();
    QVERIFY(toolbar);
    QCOMPARE(toolbar->getConfirmButton()->text(), QStringLiteral("Apply Changes"));
    QVERIFY(!toolbar->getConfirmButton()->isEnabled());
    QVERIFY(!toolbar->getUndoButton()->isEnabled());
    QVERIFY(!toolbar->getRedoButton()->isEnabled());
    QVERIFY(!gui->hasUnsavedWork());

    // Cancel closes the tool without a question; the view of the document is back
    QTest::mouseClick(toolbar->getCancelButton(), Qt::LeftButton);
    QVERIFY(pdftest::AppToolShellFixture::waitFor([&shell]() { return !shell; }));
    QVERIFY(!context);
    QVERIFY(!pdftest::AppToolShellFixture::getShell(tab));
    QVERIFY(!qobject_cast<pdf::PDFAppToolDocumentPage*>(tab->getPage()));
    QVERIFY(tab->getView() && tab->getView()->isVisible());
}

void AppToolContentSkeletonTest::routedApplicationCommandsEndToEnd()
{
    // R3-1: the real tool with a registry of the test - a command of the application answered by an action entry is
    // the tool's through the tab, the page, the shell and the GUI (getDocumentCommandState / handleDocumentCommand of
    // the GUI only call the context); its action of the window follows; the work runs from the event loop. A command
    // nobody answers stays the application's; Undo / Save stay the shell's.
    using H = ContentSkeletonHelper;
    using State = pdf::PDFAppToolCommandState;
    ContentSkeletonCommandRecorder recorder;
    Registry registry;
    QVERIFY(registry.addAction(std::make_unique<ContentSkeletonCommandEntry>(QStringLiteral("select-all"), QStringList({ "edit.selectAll" }), &recorder)));
    pdf::PDFAppToolGUIContent::setRegistry(&registry);
    auto resetRegistry = qScopeGuard([]() { pdf::PDFAppToolGUIContent::setRegistry(nullptr); });

    pdftest::AppToolShellFixture fixture;
    QVERIFY(fixture.window);
    QVERIFY(H::setDeveloperMode(fixture.context.get(), true));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    QPointer<pdf::PDFDocumentToolShell> shell = fixture.openTool(tab, QString::fromLatin1(H::TOOL_ID));
    QVERIFY(shell);
    pdf::PDFAppToolGUIContent* gui = qobject_cast<pdf::PDFAppToolGUIContent*>(shell->getGUI());
    QVERIFY(gui && gui->getEditContext());
    QCOMPARE(gui->getEditContext()->getRegistry(), &registry);
    QVERIFY(pdftest::AppToolShellFixture::waitFor([gui]() { return gui->getEditContext()->isEditingAllowed(); }));
    pdftest::AppToolShellFixture::settle();

    const QString selectAll = pdf::PDFAppCommands::selectAll();
    QCOMPARE(gui->getDocumentCommandState(selectAll), State::Enabled);
    QCOMPARE(shell->getDocumentCommandState(selectAll), State::Enabled);
    QVERIFY(tab->isCommandEnabled(selectAll));
    const QAction* action = fixture.window->getCommandAction(selectAll);
    QVERIFY(action && action->isEnabled());
    QCOMPARE(gui->getDocumentCommandState(pdf::PDFAppCommands::copy()), State::NotHandled);
    QCOMPARE(gui->getDocumentCommandState(pdf::PDFAppCommands::save()), State::NotHandled);

    QVERIFY(tab->executeCommand(selectAll));
    QVERIFY(recorder.log.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(recorder.log, QStringList({ "select-all" }), H::timeout(5000));

    // Preview pauses the editing: the command stays the tool's, disabled - also in the window
    gui->getEditContext()->setPreviewActive(true);
    QCOMPARE(shell->getDocumentCommandState(selectAll), State::Disabled);
    QVERIFY(!tab->isCommandEnabled(selectAll));
    pdftest::AppToolShellFixture::settle();
    QVERIFY(!action->isEnabled());
    QVERIFY(!tab->executeCommand(selectAll));
    gui->getEditContext()->setPreviewActive(false);
    pdftest::AppToolShellFixture::settle();
    QVERIFY(action->isEnabled());

    shell->cancel();
    QVERIFY(pdftest::AppToolShellFixture::waitFor([&shell]() { return !shell; }));
    pdftest::AppToolShellFixture::settle();
    QCOMPARE(recorder.log, QStringList({ "select-all" }));
}

void AppToolContentSkeletonTest::helpWithoutTopic()
{
    // R3-5, zadani.md 11.2 (Uvolnění): the manual has no topic of Edit Content before stage E6 - the help page, its
    // search and the related links do not show the tool to a user with the switch off, as at 52deeff1. With the
    // switch on, F1 in the tool does what it does for every tool without a topic: the page Tools of the manual.
    using H = ContentSkeletonHelper;
    const std::shared_ptr<const pdf::PDFAppHelpCatalog> catalog = pdf::PDFAppHelpCatalog::getBuiltin();
    QVERIFY(catalog);
    QVERIFY(!catalog->findTopic(QStringLiteral("tool-edit-content")));
    for (const QString& categoryId : { QStringLiteral("tools"), QStringLiteral("getting-started") })
    {
        for (const pdf::PDFAppHelpTopic* topic : catalog->getCategoryTopics(categoryId))
        {
            QVERIFY2(!topic->related.contains(QStringLiteral("/tools/edit.content")), qPrintable(topic->id));
        }
    }

    pdftest::AppToolShellFixture fixture;
    QVERIFY(fixture.window);
    QVERIFY(H::setDeveloperMode(fixture.context.get(), true));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    QVERIFY(fixture.openTool(tab, QString::fromLatin1(H::TOOL_ID)));
    pdf::PDFAppToolDocumentPage* page = qobject_cast<pdf::PDFAppToolDocumentPage*>(tab->getPage());
    QVERIFY(page);
    QCOMPARE(page->getHelpTopic(), QStringLiteral("tools"));
    fixture.window->getCommandAction(pdf::PDFAppCommands::help())->trigger();
    pdftest::AppToolShellFixture::settle();
    QCOMPARE(fixture.getShownHelpTopic(), QStringLiteral("tools"));
}

void AppToolContentSkeletonTest::statesFollowTheUndoStack()
{
    // R3-11, R3-6: the states the shell asks follow the undo stack of the session until P14 - a command executed
    // through the context (a package of E1 before P14) is work: Apply Changes and Undo are enabled, the tool has
    // unsaved work and defers the automatic refresh of the document; back at the clean point nothing of it
    using H = ContentSkeletonHelper;
    using F = pdf::PDFContentCommandFactory;
    pdftest::AppToolShellFixture fixture;
    QVERIFY(fixture.window);
    QVERIFY(H::setDeveloperMode(fixture.context.get(), true));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    QPointer<pdf::PDFDocumentToolShell> shell = fixture.openTool(tab, QString::fromLatin1(H::TOOL_ID));
    QVERIFY(shell);
    pdf::PDFAppToolGUIContent* gui = qobject_cast<pdf::PDFAppToolGUIContent*>(shell->getGUI());
    QVERIFY(gui);
    Context* context = gui->getEditContext();
    QVERIFY(pdftest::AppToolShellFixture::waitFor([context]() { return context->isEditingAllowed(); }));
    pdf::PDFAppToolUndoTarget* target = shell->getUndoTarget();
    QVERIFY(qobject_cast<pdf::PDFAppEditContentUndoTarget*>(target));

    QVERIFY(!gui->isConfirmEnabled() && !gui->hasUnsavedWork() && !gui->isDocumentReloadDeferred());
    QVERIFY(!target->canUndo() && !target->canRedo());
    QVERIFY(!tab->getPage()->isDocumentReloadDeferred());

    std::vector<pdf::PDFContentNodeReference> nodes = H::findNodes(context->getContainer(), pdf::PDFContentNodeKind::Path);
    if (nodes.empty())
    {
        nodes = H::findNodes(context->getContainer(), pdf::PDFContentNodeKind::TextObject);
    }
    QVERIFY(!nodes.empty());
    QVERIFY(context->execute(F::createDelete({ nodes.front() })));
    QVERIFY(gui->isConfirmEnabled() && gui->hasUnsavedWork() && gui->isDocumentReloadDeferred());
    QVERIFY(target->canUndo() && !target->canRedo());
    QVERIFY(shell->hasUnsavedWork());
    QVERIFY(shell->isDocumentReloadDeferred());
    QVERIFY(tab->getPage()->isDocumentReloadDeferred());

    // Back at the clean point by the undo of the session (the undo of the target is P14's)
    QVERIFY(context->getSession()->undo().has_value());
    QVERIFY(!gui->isConfirmEnabled() && !gui->hasUnsavedWork() && !gui->isDocumentReloadDeferred());
    QVERIFY(!target->canUndo() && target->canRedo());
    QVERIFY(!tab->getPage()->isDocumentReloadDeferred());

    shell->cancel();
    QVERIFY(pdftest::AppToolShellFixture::waitFor([&shell]() { return !shell; }));
}

void AppToolContentSkeletonTest::optionsAreStoredParameters()
{
    // pe1_skeleton_notes.md decision 7, pb0s7_state.md item 2: the display options of the header are the stored
    // parameters of the core tool - a value stored for the tool is read at the opening, a value set by the options is
    // written into the parameter model without an undo step. The GUI of the tool is no generic panel of parameters:
    // none of them is a row of its panels.
    using H = ContentSkeletonHelper;
    using O = pdf::PDFAppEditContentOptions;
    pdftest::AppToolShellFixture fixture;
    QVERIFY(fixture.window);
    QVERIFY(H::setDeveloperMode(fixture.context.get(), true));
    const pdf::PDFDocumentToolContent core;
    const pdf::PDFDocumentToolParameterDefinitions definitions = core.getParameters();
    const QString toolId = QString::fromLatin1(H::TOOL_ID);
    pdf::PDFToolParameterRepository repository(fixture.context->getDatabase());
    for (const pdf::PDFDocumentToolParameterDefinition& definition : definitions)
    {
        if (definition.getName() == QLatin1String(O::SHOW_GRID))
        {
            QVERIFY(!repository.setValue(toolId, definition, true).isError());
        }
        if (definition.getName() == QLatin1String(O::GRID_SPACING))
        {
            QVERIFY(!repository.setValue(toolId, definition, 12.5).isError());
        }
    }

    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    QPointer<pdf::PDFDocumentToolShell> shell = fixture.openTool(tab, toolId);
    QVERIFY(shell);
    pdf::PDFAppToolGUIContent* gui = qobject_cast<pdf::PDFAppToolGUIContent*>(shell->getGUI());
    QVERIFY(gui && gui->getEditContext());
    O* options = gui->getEditContext()->getOptions();
    pdf::PDFAppToolParameterModel* model = gui->getModel();
    QVERIFY(model);

    // The stored values, the defaults of the others
    QVERIFY(options->getBool(QString::fromLatin1(O::SHOW_GRID)));
    QCOMPARE(options->getValue(QString::fromLatin1(O::GRID_SPACING)).toDouble(), 12.5);
    QCOMPARE(options->getValue(QString::fromLatin1(O::UNITS)), QVariant(QStringLiteral("pt")));
    for (const char* name : { O::SHOW_RULERS, O::SHOW_GRID, O::SHOW_GUIDES, O::SHOW_CLIPPING_PATHS, O::SHOW_INVISIBLE_TEXT, O::SHOW_ANNOTATIONS,
                              O::SHOW_READING_ORDER, O::SNAP_TO_OBJECTS, O::SNAP_TO_GRID, O::UNITS, O::GRID_SPACING, O::GRID_ORIGIN, O::PREVIEW_OVERPRINT,
                              O::PREVIEW_ANNOTATIONS, O::PREVIEW_PAPER_COLOR, O::LEFT_TAB })
    {
        const QString parameter = QString::fromLatin1(name);
        bool isStored = false;
        for (const pdf::PDFDocumentToolParameterDefinition& definition : model->getDefinitions())
        {
            isStored = isStored || (definition.getName() == parameter && definition.isStored());
        }
        QVERIFY2(isStored, name);
        QVERIFY2(model->getValue(parameter).isValid(), name);
        QCOMPARE(options->getValue(parameter), model->getValue(parameter));
    }

    // Written without an undo step
    options->setValue(QString::fromLatin1(O::SHOW_RULERS), true);
    QCOMPARE(model->getValue(QString::fromLatin1(O::SHOW_RULERS)), QVariant(true));
    QVERIFY(!model->canUndo());

    // No generic row of a parameter in the parts of the tool. The titles of the two tabs of the left panel are no
    // rows: "Pages" of the tab Pages reads as the title of the parameter "pages" of the core tool.
    QVERIFY(!dynamic_cast<pdf::PDFAppDeclarativeToolGUI*>(gui));
    const std::vector<QWidget*> parts = { gui->getLeftPanelWidget(), gui->getCardStack(), gui->getCanvas(), gui->getToolActionsWidget() };
    for (QWidget* part : parts)
    {
        QVERIFY(part);
        for (const pdf::PDFDocumentToolParameterDefinition& definition : model->getDefinitions())
        {
            for (const QLabel* label : part->findChildren<QLabel*>())
            {
                if (label->objectName() == QLatin1String("editContentPagesTitle") || label->objectName() == QLatin1String("editContentObjectsTitle"))
                {
                    continue;
                }
                QVERIFY2(label->text() != definition.getTitle(), qPrintable(definition.getName()));
            }
            for (const QAbstractButton* button : part->findChildren<QAbstractButton*>())
            {
                QVERIFY2(button->text() != definition.getTitle(), qPrintable(definition.getName()));
            }
        }
    }
}

QTEST_MAIN(AppToolContentSkeletonTest)

#include "tst_apptoolcontentskeletontest.moc"
