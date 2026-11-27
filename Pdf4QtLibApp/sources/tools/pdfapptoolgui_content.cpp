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

#include "pdfapptoolgui_content.h"

#include "pdfappdocument.h"
#include "pdfappdocumenttoolbar.h"
#include "pdfappeditcontentcards.h"
#include "pdfappeditcontentcontext.h"
#include "pdfappeditcontentkeyboard.h"
#include "pdfappeditcontentmenus.h"
#include "pdfappeditcontentnotices.h"
#include "pdfappeditcontentregistry.h"
#include "pdfappeditcontentselection.h"
#include "pdfappeditcontenttools.h"
#include "pdfappeditcontentundo.h"
#include "pdfappeditcontentview.h"
#include "pdfapplocale.h"
#include "pdfappmainwindow.h"
#include "pdfapptoolhost.h"
#include "pdfapptoolpanelbuilder.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfapptooltoolbar.h"
#include "pdfdocumenttoolguiregistry.h"
#include "pdfdocumenttoolrunner.h"
#include "pdfdocumenttoolshell.h"

#include "pdfcontentjson.h"
#include "pdfcontentundostack.h"
#include "pdfdocumenttool_content.h"
#include "pdfdocumenttoolregistry.h"

#include "pdfcomponentrows.h"
#include "pdffluentwidgets.h"
#include "pdfmessagebox.h"
#include "pdfthemeiconengine.h"
#include "pdfwidgetstyleproperties.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <limits>
#include <memory>
#include <utility>

namespace pdf
{

/// The state of the registration of the tool (PDFAppToolGUIContent::registerContentTools); GUI thread
class PDFAppToolGUIContentRegistration
{
public:
    PDFAppToolGUIContentRegistration() = delete;

    static bool s_isRegistered;
    static const PDFAppEditContentRegistry* s_registry;     ///< setRegistry; nullptr = the registry of the application
};

bool PDFAppToolGUIContentRegistration::s_isRegistered = false;
const PDFAppEditContentRegistry* PDFAppToolGUIContentRegistration::s_registry = nullptr;

/// What the GUI computes from the model and the texts of the frames (pure functions)
class PDFAppToolGUIContentHelper
{
public:
    PDFAppToolGUIContentHelper() = delete;

    /// The property of a button of the toolbar, which names its tool or its action entry
    static constexpr const char* ID_PROPERTY = "pdf4qtEditContentId";

    /// The deepest groups, through which a scanned page is recognized (q cm /Im0 Do Q and its marked content)
    static constexpr int MAXIMUM_SCAN_DEPTH = 8;

    /// The tab strip of the left panel (Sidebar tab 190:619: 32 x 32, the icon 18, 2 apart, 6 below the strip)
    static constexpr int TAB_SIZE = 32;
    static constexpr int TAB_ICON_SIZE = 18;
    static constexpr int TAB_GAP = 2;
    static constexpr int TAB_STRIP_BOTTOM = 6;

    /// Width of the panels View and Shape (edit-content-v2-menus-view, -menus-shape)
    static constexpr int VIEW_PANEL_WIDTH = 260;

    /// The place of Shape among the tool buttons (the order of the tool entries of the toolbar)
    static constexpr int SHAPE_ORDER = 400;

    /// The objects of the content (as the facts of the writer count them): a text object, a path, an image, a use of
    /// a form, a shading, an opaque part. Groups and marked content hold objects, state runs and marked points are no
    /// objects. Bounded by the nodes of the container (one visit each, an explicit stack: the tree is no graph).
    struct ObjectCounts
    {
        PDFInteger original = 0;    ///< Objects read from the document
        PDFInteger added = 0;       ///< Objects created in this session (PDFContentEditSession::FIRST_SESSION_NODE_ID)
    };

    static ObjectCounts countObjects(const PDFContentContainer& container)
    {
        ObjectCounts counts;
        std::vector<const PDFContentNode*> stack;
        if (container.getRoot())
        {
            stack.push_back(container.getRoot().get());
        }

        while (!stack.empty())
        {
            const PDFContentNode* node = stack.back();
            stack.pop_back();
            switch (node->getKind())
            {
                case PDFContentNodeKind::TextObject:
                case PDFContentNodeKind::Path:
                case PDFContentNodeKind::Image:
                case PDFContentNodeKind::FormUse:
                case PDFContentNodeKind::Shading:
                case PDFContentNodeKind::Opaque:
                    if (node->getId().value >= PDFContentEditSession::FIRST_SESSION_NODE_ID)
                    {
                        ++counts.added;
                    }
                    else
                    {
                        ++counts.original;
                    }
                    continue;

                default:
                    break;
            }

            for (const PDFContentNodePointer& child : node->getChildren())
            {
                if (child)
                {
                    stack.push_back(child.get());
                }
            }
        }

        return counts;
    }

    /// The page is a scan (edit-content-v2-scanned): its only object is an image, also inside of groups and marked
    /// content of a single object. Bounded: MAXIMUM_SCAN_DEPTH levels, the children of a level until a second object.
    static bool isScannedPage(const PDFContentContainer& container)
    {
        const PDFContentNode* node = container.getRoot().get();
        for (int depth = 0; node && depth < MAXIMUM_SCAN_DEPTH; ++depth)
        {
            const PDFContentNode* single = nullptr;
            for (const PDFContentNodePointer& child : node->getChildren())
            {
                if (!child || child->getKind() == PDFContentNodeKind::StateRun || child->getKind() == PDFContentNodeKind::MarkedPoint)
                {
                    continue;
                }
                if (single)
                {
                    return false;
                }
                single = child.get();
            }

            if (!single)
            {
                return false;
            }
            if (single->getKind() == PDFContentNodeKind::Image)
            {
                return true;
            }
            if (single->getKind() != PDFContentNodeKind::Group && single->getKind() != PDFContentNodeKind::MarkedContent)
            {
                return false;
            }
            node = single;
        }
        return false;
    }

    /// A notice of a state (pdfappeditcontentcontext.h)
    static PDFAppEditContentNoticeRequest createState(const char* id, PDFAppEditContentNoticeRequest::Severity severity, const QString& title, const QString& text)
    {
        PDFAppEditContentNoticeRequest request;
        request.id = QString::fromLatin1(id);
        request.kind = PDFAppEditContentNoticeRequest::Kind::State;
        request.severity = severity;
        request.title = title;
        request.text = text;
        return request;
    }

    /// A notice of an event (it leaves with the next change of the selection)
    static PDFAppEditContentNoticeRequest createEvent(const char* id, PDFAppEditContentNoticeRequest::Severity severity, const QString& title, const QString& text)
    {
        PDFAppEditContentNoticeRequest request = createState(id, severity, title, text);
        request.kind = PDFAppEditContentNoticeRequest::Kind::Event;
        return request;
    }

    /// The page numbers from 1 of page indices, as the application writes them ("3, 7", "3–5")
    static QString formatPages(const std::vector<PDFInteger>& pageIndices)
    {
        return PDFAppLocale::formatPageNumbers(pageIndices);
    }

    /// Makes a dialog of the tool: owned by the GUI (retire closes it unanswered), deleted when closed
    static PDFMessageBox* createDialog(QObject* owner, PDFThemeIcons::Badge badge, const QString& title, const QString& text, const QString& objectName, QWidget* parent)
    {
        PDFMessageBox* dialog = new PDFMessageBox(badge, title, text, QDialogButtonBox::NoButton, parent);
        PDFDeferredDeletion::setOwner(dialog, owner);
        dialog->setObjectName(objectName);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        return dialog;
    }

    /// Shows a dialog of the tool (open, never exec)
    static void showDialog(PDFMessageBox* dialog, QWidget* parent)
    {
        PDFAppMainWindow::prepareDialog(dialog, parent);
        dialog->open();
    }
};

PDFAppToolGUIContent::PDFAppToolGUIContent(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppToolGUIContent::~PDFAppToolGUIContent()
{
    // The children in the reverse order of their creation (as the context does with its own, R3-3): the keyboard, the
    // menus, the notices and the undo target read the context in their destructors, Qt alone would delete the context
    // first
    delete m_keyboard.data();
    delete m_menus.data();
    delete m_notices.data();
    delete m_undoTarget.data();
    delete m_context.data();
}

void PDFAppToolGUIContent::setRegistry(const PDFAppEditContentRegistry* registry)
{
    PDFAppToolGUIContentRegistration::s_registry = registry;
}

void PDFAppToolGUIContent::registerContentTools(bool isRegistered)
{
    if (PDFAppToolGUIContentRegistration::s_isRegistered == isRegistered)
    {
        return;
    }

    const QString toolId = QString::fromLatin1(PDFDocumentToolContent::TOOL_ID);
    PDFAppToolGUIContentRegistration::s_isRegistered = isRegistered;
    if (isRegistered)
    {
        // The core tool first, then the GUI: the GUI registry registers the page of the tool only for a tool, which
        // has an implementation
        if (!PDFDocumentToolRegistry::isImplemented(toolId))
        {
            PDFDocumentToolRegistry::registerTool(std::make_shared<PDFDocumentToolContent>());
        }
        if (!PDFDocumentToolGUIRegistry::getFactory(toolId))
        {
            PDFDocumentToolGUIRegistry::registerFactory(toolId, std::make_shared<PDFDocumentToolGUIFactoryOf<PDFAppToolGUIContent>>());
        }
    }
    else
    {
        // An open tool keeps its GUI and its shell until it is closed (the shell holds them); only a new entry of the
        // route finds Page Not Found
        PDFDocumentToolGUIRegistry::unregisterFactory(toolId);
        PDFDocumentToolRegistry::unregisterTool(toolId);
    }
}

bool PDFAppToolGUIContent::isContentToolRegistered()
{
    return PDFAppToolGUIContentRegistration::s_isRegistered;
}

bool PDFAppToolGUIContent::ensureContext()
{
    if (m_context)
    {
        return true;
    }

    PDFAppDocument* document = getShell() ? getDocument() : nullptr;
    PDFDocumentPointer pdfDocument = document ? document->getDocument() : PDFDocumentPointer();
    if (!pdfDocument)
    {
        return false;
    }

    // Color management of the application, as the host gives it to a run (nullptr = the generic one)
    PDFAppToolHost* host = getHost();
    PDFCMSPointer cms = host ? host->createEnvironment().cms : PDFCMSPointer();

    // The children live as long as the GUI (pdfapptoolgui_content.h): the context first, the others read it
    PDFAppEditContentContext* context = new PDFAppEditContentContext(std::move(pdfDocument), std::move(cms), PDFAppToolGUIContentRegistration::s_registry, this);
    m_context = context;
    context->setGUI(this);
    m_undoTarget = new PDFAppEditContentUndoTarget(context, this);
    m_notices = new PDFAppEditContentNotices(context, this);
    m_menus = new PDFAppEditContentMenus(context, this);
    m_keyboard = new PDFAppEditContentKeyboard(context, this);

    // The children of the context live as long as it does (and this GUI): plain connections
    connect(context, &PDFAppEditContentContext::editingAllowedChanged, this, &PDFAppToolGUIContent::onEditingAllowedChanged);
    connect(context, &PDFAppEditContentContext::pageRequested, this, &PDFAppToolGUIContent::onPageRequested);
    connect(context, &PDFAppEditContentContext::pageChanged, this, &PDFAppToolGUIContent::onPageChanged);
    connect(context, &PDFAppEditContentContext::containerChanged, this, &PDFAppToolGUIContent::onContainerChanged);
    connect(context, &PDFAppEditContentContext::previewActiveChanged, this, &PDFAppToolGUIContent::onPreviewActiveChanged);
    connect(context, &PDFAppEditContentContext::commandRefused, this, &PDFAppToolGUIContent::onCommandRefused);
    connect(context->getSession(), &PDFContentEditSession::modelChanged, this, &PDFAppToolGUIContent::onModelChanged);
    connect(context->getOptions(), &PDFAppEditContentOptions::changed, this, &PDFAppToolGUIContent::onOptionsChanged);
    connect(context->getToolController(), &PDFAppEditContentToolController::activeToolChanged, this, &PDFAppToolGUIContent::onToolStateChanged);
    connect(context->getToolController(), &PDFAppEditContentToolController::stateChanged, this, &PDFAppToolGUIContent::onToolStateChanged);
    connect(context->getSelection(), &PDFAppEditContentSelection::changed, this, &PDFAppToolGUIContent::onSelectionChanged);

    m_isClean = context->getSession()->getUndoStack()->isClean();

    // QE.2: the notice of a declared conformance comes with the first opening (no signal of a part is connected yet)
    updateStateNotices(false);
    return true;
}

QWidget* PDFAppToolGUIContent::getDialogParent() const
{
    const PDFAppToolHost* host = getHost();
    return host ? host->getKeyWidget() : nullptr;
}

// ------------------------------------------------------------------ parts

QWidget* PDFAppToolGUIContent::createToolActions(QWidget* parent)
{
    if (!ensureContext())
    {
        return nullptr;
    }

    using H = PDFAppToolGUIContentHelper;
    const PDFAppEditContentRegistry* registry = m_context->getRegistry();

    // edit-content-v2 1647:21313: Select ... Clipping Path, Multiple | Find, View, Preview | Clear Page. A button
    // exists only for what is registered (zadani_gui.md 4.16: "hidden, not disabled").
    QWidget* widget = new QWidget(parent);
    widget->setObjectName(QStringLiteral("editContentActions"));
    PDFAppToolPanelBuilder builder(this, widget, Qt::Horizontal, PDFAppToolPanelBuilder::ACTION_GAP);
    m_toolButtons.clear();
    m_actionButtons.clear();
    m_shapePanel = nullptr;

    // The tool buttons: an exclusive group of icon check buttons (the group is a part: a child of the widget)
    QButtonGroup* group = new QButtonGroup(widget);
    group->setExclusive(true);
    m_toolGroup = group;
    bool hasCreationTool = false;
    bool hasShapeMenu = false;
    const std::vector<const PDFAppEditContentToolEntry*> shapes = registry->getTools(PDFAppEditContentToolEntry::Place::ShapeMenu);
    for (const PDFAppEditContentToolEntry* tool : registry->getTools(PDFAppEditContentToolEntry::Place::Toolbar))
    {
        // Shape lies at the order 400 among the tool buttons (Select 100, Text 200, Image 300, Shape 400, Pen 500)
        if (!hasShapeMenu && !shapes.empty() && tool->getOrder() > H::SHAPE_ORDER)
        {
            addShapeMenu(&builder, shapes);
            hasShapeMenu = true;
        }

        // The tooltip: the title, the key and the description (row-note "toolbar", the frame -tooltip)
        QString toolTip = tool->getKey().isEmpty() ? tool->getTitle() : tr("%1 (%2)").arg(tool->getTitle(), tool->getKey().toString(QKeySequence::NativeText));
        if (!tool->getDescription().isEmpty())
        {
            toolTip += QLatin1Char('\n') + tool->getDescription();
        }

        QToolButton* button = builder.addToolButton(QStringLiteral("editContentTool-") + tool->getId(), tool->getIconName(), toolTip, group);
        button->setAccessibleName(tool->getTitle());
        button->setProperty(H::ID_PROPERTY, tool->getId());
        m_toolButtons[tool->getId()] = button;
        hasCreationTool = hasCreationTool || tool->getId() != QLatin1String(PDFAppEditContentToolIds::SELECT);
    }
    if (!hasShapeMenu && !shapes.empty())
    {
        addShapeMenu(&builder, shapes);
    }
    hasCreationTool = hasCreationTool || !shapes.empty();
    connect(group, &QButtonGroup::buttonClicked, this, &PDFAppToolGUIContent::onToolButtonClicked);

    // Multiple keeps a creation tool active (an icon toggle; there is nothing to keep without a creation tool)
    m_multipleButton = nullptr;
    if (hasCreationTool)
    {
        QToolButton* multiple = builder.addToolButton(QStringLiteral("editContentMultiple"), QStringLiteral("multi-create"),
                                                      tr("Multiple\nKeeps the creation tool active to place several objects."), nullptr);
        multiple->setAccessibleName(tr("Multiple"));
        connect(multiple, &QAbstractButton::toggled, this, &PDFAppToolGUIContent::onMultipleToggled);
        m_multipleButton = multiple;
    }

    // The buttons of the entries placed in TOOLBAR by their groups (a divider before a group, which has a button), View
    // of the GUI first in its group
    const std::vector<const PDFAppEditContentActionEntry*> entries = registry->getMenuActions(QString::fromLatin1(PDFAppEditContentMenuIds::TOOLBAR));
    const bool hasViewMenu = !registry->getMenuActions(QString::fromLatin1(PDFAppEditContentMenuIds::VIEW)).empty();
    bool hasButtons = !m_toolButtons.empty() || m_multipleButton;
    int lastGroup = -1;
    m_viewPanel = nullptr;
    for (int toolbarGroup : { PDFAppEditContentMenuIds::GROUP_TOOLBAR_MODES, PDFAppEditContentMenuIds::GROUP_TOOLBAR_VIEW, PDFAppEditContentMenuIds::GROUP_TOOLBAR_PAGE })
    {
        if (toolbarGroup == PDFAppEditContentMenuIds::GROUP_TOOLBAR_VIEW && hasViewMenu)
        {
            if (hasButtons && lastGroup != PDFAppEditContentMenuIds::GROUP_TOOLBAR_MODES)
            {
                builder.addActionDivider();
            }
            hasButtons = true;
            lastGroup = PDFAppEditContentMenuIds::GROUP_TOOLBAR_MODES;

            PDFAppToolPanelPopup* panel = builder.addMenu(QStringLiteral("editContentView"), QStringLiteral("eye"), tr("View"), H::VIEW_PANEL_WIDTH);
            if (QToolButton* button = panel->getButton())
            {
                button->setObjectName(QStringLiteral("editContentView"));
                button->setProperty(PDFAppToolToolbar::NARROW_FIRST_PROPERTY, true);
                connect(button, &QAbstractButton::toggled, this, &PDFAppToolGUIContent::onViewButtonToggled);
            }
            m_viewPanel = panel;
        }

        for (const PDFAppEditContentActionEntry* entry : entries)
        {
            // The placements are a value of the entry: findPlacement points into them, they must outlive the pointer
            const std::vector<PDFAppEditContentActionEntry::Placement> placements = entry->getPlacements();
            const PDFAppEditContentActionEntry::Placement* placement = PDFAppEditContentRegistry::findPlacement(placements, QString::fromLatin1(PDFAppEditContentMenuIds::TOOLBAR));
            if (!placement || placement->group != toolbarGroup)
            {
                continue;
            }

            // The groups MODES and VIEW are one segment of the toolbar after the divider (Multiple | Find, View, Preview)
            const int segment = toolbarGroup == PDFAppEditContentMenuIds::GROUP_TOOLBAR_VIEW ? PDFAppEditContentMenuIds::GROUP_TOOLBAR_MODES : toolbarGroup;
            if (hasButtons && lastGroup != segment)
            {
                builder.addActionDivider();
            }
            hasButtons = true;
            lastGroup = segment;

            const QString objectName = QStringLiteral("editContentAction-") + entry->getId();
            QToolButton* button = entry->isCheckable() ? builder.addToggleButton(objectName, entry->getTitle(m_context), entry->getIconName())
                                                       : builder.addAction(entry->getTitle(m_context), entry->getIconName(), this, &PDFAppToolGUIContent::onActionButtonClicked);
            button->setObjectName(objectName);
            button->setProperty(H::ID_PROPERTY, entry->getId());
            button->setProperty(PDFAppToolToolbar::NARROW_FIRST_PROPERTY, true);
            if (entry->isCheckable())
            {
                connect(button, &QAbstractButton::clicked, this, &PDFAppToolGUIContent::onActionButtonClicked);
            }
            m_actionButtons[entry->getId()] = button;
        }
    }

    m_toolActionsWidget = widget;
    updateToolbarStates();
    return widget;
}

void PDFAppToolGUIContent::addShapeMenu(PDFAppToolPanelBuilder* builder, const std::vector<const PDFAppEditContentToolEntry*>& shapes)
{
    using H = PDFAppToolGUIContentHelper;

    // edit-content-v2-menus-shape: a row per shape tool; a row starts its tool from the event loop (the panel closes
    // first, its row is the sender)
    PDFAppToolPanelPopup* panel = builder->addMenu(QStringLiteral("editContentShape"), QStringLiteral("shapes"), tr("Shape"), H::VIEW_PANEL_WIDTH);
    if (QToolButton* button = panel->getButton())
    {
        button->setObjectName(QStringLiteral("editContentShape"));
    }
    for (const PDFAppEditContentToolEntry* shape : shapes)
    {
        const QString description = shape->getKey().isEmpty() ? shape->getDescription()
                                                               : tr("%1 · %2").arg(shape->getKey().toString(QKeySequence::NativeText), shape->getDescription());
        PDFTileRow* row = panel->addRow(shape->getTitle(), description, shape->getIconName(), PDFTileRow::Kind::MenuItem);
        row->setObjectName(QStringLiteral("editContentShape-") + shape->getId());
        row->setProperty(H::ID_PROPERTY, shape->getId());
        connect(row, &QAbstractButton::clicked, this, &PDFAppToolGUIContent::onShapeRowClicked);
    }
    m_shapePanel = panel;
}

void PDFAppToolGUIContent::onShapeRowClicked()
{
    const QObject* row = sender();
    if (!row)
    {
        return;
    }

    m_pendingShapeToolId = row->property(PDFAppToolGUIContentHelper::ID_PROPERTY).toString();
    if (PDFAppToolPanelPopup* panel = m_shapePanel.data())
    {
        panel->close();
    }
    QMetaObject::invokeMethod(this, &PDFAppToolGUIContent::onShapeToolQueued, Qt::QueuedConnection);
}

void PDFAppToolGUIContent::onShapeToolQueued()
{
    const QString toolId = std::exchange(m_pendingShapeToolId, QString());
    if (m_context && !toolId.isEmpty() && !PDFDeferredDeletion::isRetired(this))
    {
        // The last call: the slots of the switch of the tool run
        m_context->getToolController()->activateTool(toolId);
    }
}

QWidget* PDFAppToolGUIContent::createLeftPanel(QWidget* parent)
{
    if (!ensureContext())
    {
        return nullptr;
    }

    using H = PDFAppToolGUIContentHelper;

    // The tab strip (Sidebar tab: icon buttons, the selected one accented) and the panels of the tab entries; a panel
    // keeps no state - the models are the states of the tabs, children of the context
    QWidget* widget = new QWidget(parent);
    widget->setObjectName(QStringLiteral("editContentLeftPanel"));
    QVBoxLayout* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    QWidget* strip = new QWidget(widget);
    strip->setObjectName(QStringLiteral("editContentTabs"));
    QHBoxLayout* stripLayout = new QHBoxLayout(strip);
    stripLayout->setContentsMargins(0, 0, 0, H::TAB_STRIP_BOTTOM);
    stripLayout->setSpacing(H::TAB_GAP);
    layout->addWidget(strip);

    QStackedWidget* stack = new QStackedWidget(widget);
    stack->setObjectName(QStringLiteral("editContentTabPanels"));
    layout->addWidget(stack, 1);

    QButtonGroup* group = new QButtonGroup(widget);
    group->setExclusive(true);
    m_tabIds.clear();
    for (const PDFAppEditContentTabEntry* tab : m_context->getRegistry()->getTabs())
    {
        QWidget* panel = tab->createPanel(m_context, m_context->getTabState(tab->getId()), stack);
        if (!panel)
        {
            continue;
        }

        QToolButton* button = new QToolButton(strip);
        button->setObjectName(QStringLiteral("editContentTab-") + tab->getId());
        PDFWidgetStyleProperties::set(button, PDFWidgetStyleProperties::SIDEBAR_TAB);
        button->setCheckable(true);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::TabFocus);
        button->setFixedSize(H::TAB_SIZE, H::TAB_SIZE);
        button->setIconSize(QSize(H::TAB_ICON_SIZE, H::TAB_ICON_SIZE));
        button->setIcon(PDFThemeIcons::getIcon(tab->getIconName()));
        button->setToolTip(tab->getTitle());
        button->setAccessibleName(tab->getTitle());
        stripLayout->addWidget(button);

        group->addButton(button, int(m_tabIds.size()));
        stack->addWidget(panel);
        m_tabIds.push_back(tab->getId());
    }
    stripLayout->addStretch(1);
    connect(group, &QButtonGroup::idClicked, this, &PDFAppToolGUIContent::onTabClicked);

    m_tabGroup = group;
    m_tabStack = stack;
    m_leftPanelWidget = widget;
    updateLeftTab();
    return widget;
}

QWidget* PDFAppToolGUIContent::createRightPanel(QWidget* parent)
{
    if (!ensureContext())
    {
        return nullptr;
    }

    PDFAppEditContentCardStack* stack = new PDFAppEditContentCardStack(m_context, parent);
    m_cardStack = stack;
    return stack;
}

QWidget* PDFAppToolGUIContent::createCanvasPage(QWidget* parent)
{
    if (!ensureContext())
    {
        return nullptr;
    }

    PDFAppEditContentCanvas* canvas = new PDFAppEditContentCanvas(m_context, parent);
    m_canvas = canvas;

    // One place for the notices at the bottom edge of the canvas (KE.28); the canvas places it
    PDFAppEditContentNotice* notice = new PDFAppEditContentNotice(m_notices, canvas);
    canvas->setNoticeWidget(notice);
    m_noticeWidget = notice;

    // The zoom item of the status bar follows the view (a part: a foreign sender for this GUI)
    if (PDFAppEditContentView* view = canvas->getView())
    {
        PDFDeferredDeletion::connect(view, &PDFAppEditContentView::viewChanged, this, &PDFAppToolGUIContent::onViewChanged);
    }

    // The shortcuts of the tool on the key widget of the host (way 3 of zadani_gui.md 4.20)
    PDFAppToolHost* host = getHost();
    if (m_keyboard && host)
    {
        m_keyboard->install(host->getKeyWidget());
    }

    // The page of the view of the host is shown (the first one without it); the core starts the asynchronous build of
    // its container. Nothing is selected yet: no rule of a switch applies.
    const PDFInteger currentPage = host ? host->getCurrentPage() : -1;
    m_context->setPage(currentPage >= 0 ? currentPage : 0);
    return canvas;
}

void PDFAppToolGUIContent::setContentEnabled(bool isEnabled)
{
    if (m_context)
    {
        m_context->setRunning(!isEnabled);
    }
    BaseClass::setContentEnabled(isEnabled);
}

QAbstractButton* PDFAppToolGUIContent::getToolButton(const QString& toolId) const
{
    const auto it = m_toolButtons.find(toolId);
    return it != m_toolButtons.cend() ? it->second.data() : nullptr;
}

QAbstractButton* PDFAppToolGUIContent::getActionButton(const QString& actionId) const
{
    const auto it = m_actionButtons.find(actionId);
    return it != m_actionButtons.cend() ? it->second.data() : nullptr;
}

QAbstractButton* PDFAppToolGUIContent::getViewButton() const
{
    return m_viewPanel ? m_viewPanel->getButton() : nullptr;
}

QAbstractButton* PDFAppToolGUIContent::getShapeButton() const
{
    return m_shapePanel ? m_shapePanel->getButton() : nullptr;
}

QAbstractButton* PDFAppToolGUIContent::getTabButton(const QString& tabId) const
{
    for (size_t i = 0; i < m_tabIds.size(); ++i)
    {
        if (m_tabIds[i] == tabId)
        {
            return m_tabGroup ? m_tabGroup->button(int(i)) : nullptr;
        }
    }
    return nullptr;
}

void PDFAppToolGUIContent::updateToolbarStates()
{
    if (!m_context)
    {
        return;
    }

    // The tool buttons: the active tool is checked; the tools act only while the editing is allowed (Preview and a
    // run pause them, edit-content-v2-preview)
    const PDFAppEditContentToolController* controller = m_context->getToolController();
    const bool isEditingAllowed = m_context->isEditingAllowed();

    // A tool of the menu Shape or a mode has no button: the exclusive group must allow no checked button then
    if (QButtonGroup* group = m_toolGroup.data())
    {
        group->setExclusive(false);
    }
    for (const auto& item : m_toolButtons)
    {
        if (QAbstractButton* button = item.second.data())
        {
            button->setEnabled(isEditingAllowed);
            button->setChecked(item.first == controller->getActiveToolId());
        }
    }
    if (QButtonGroup* group = m_toolGroup.data())
    {
        group->setExclusive(true);
    }

    if (QAbstractButton* multiple = m_multipleButton.data())
    {
        const QSignalBlocker blocker(multiple);
        multiple->setEnabled(isEditingAllowed);
        multiple->setChecked(controller->isMultiple());
    }

    // The buttons of the entries: hidden, disabled and checked by their entries
    const PDFAppEditContentRegistry* registry = m_context->getRegistry();
    for (const auto& item : m_actionButtons)
    {
        QAbstractButton* button = item.second.data();
        const PDFAppEditContentActionEntry* entry = registry->findAction(item.first);
        if (!button || !entry)
        {
            continue;
        }

        button->setVisible(entry->isVisible(m_context));
        button->setEnabled(entry->isEnabled(m_context));
        if (entry->isCheckable())
        {
            button->setChecked(entry->isChecked(m_context));
        }
    }
}

void PDFAppToolGUIContent::updateLeftTab()
{
    if (!m_context || !m_tabGroup || !m_tabStack || m_tabIds.empty())
    {
        return;
    }

    // The tab of the option; an unknown one (a tab of a later stage stored, then the developer switch) is the first
    const QString tabId = m_context->getOptions()->getValue(QString::fromLatin1(PDFAppEditContentOptions::LEFT_TAB)).toString();
    int index = 0;
    for (size_t i = 0; i < m_tabIds.size(); ++i)
    {
        if (m_tabIds[i] == tabId)
        {
            index = int(i);
        }
    }

    m_tabStack->setCurrentIndex(index);
    if (QAbstractButton* button = m_tabGroup->button(index))
    {
        button->setChecked(true);
    }
}

// ------------------------------------------------------------------ texts and states of the shell

QString PDFAppToolGUIContent::getConfirmText() const
{
    return tr("Apply Changes");
}

QString PDFAppToolGUIContent::getConfirmIconName() const
{
    return QStringLiteral("check");
}

bool PDFAppToolGUIContent::isConfirmEnabled() const
{
    // RE-16: the work is the undo stack; locks and hidden objects are no steps
    return m_context && !m_context->getSession()->getUndoStack()->isClean();
}

QString PDFAppToolGUIContent::getStatusText() const
{
    if (!m_context)
    {
        return BaseClass::getStatusText();
    }

    // edit-content-v2-preview
    if (m_context->isPreviewActive())
    {
        return tr("Preview is on · editing is paused · press P or click Preview to continue editing");
    }

    // The active tool says what it does (the selection, a drag, a mode)
    const PDFAppEditContentToolController* controller = m_context->getToolController();
    if (controller->getActiveTool())
    {
        const QString text = controller->getStatusText();
        if (!text.isEmpty())
        {
            return text;
        }
    }

    // The state of the page (edit-content-v2-scanned, -complex-page; measured when the container changes)
    const PDFContentContainerPointer container = m_context->getContainer();
    const QString pageNumber = PDFAppLocale::formatNumber(m_context->getPageIndex() + 1);
    if (container && container->isReadOnly())
    {
        return tr("Page %1 cannot be read completely · it is shown as it is and cannot be changed").arg(pageNumber);
    }
    if (container && m_isScannedPage)
    {
        return tr("Page %1 is a scanned image · add new content on top of it").arg(pageNumber);
    }
    if (container && m_isComplexPage)
    {
        return tr("Complex page · snapping looks only at the objects near the pointer");
    }

    return BaseClass::getStatusText();
}

QString PDFAppToolGUIContent::getOwnTabPageText() const
{
    if (!m_context || m_context->getPageIndex() < 0)
    {
        return QString();
    }

    // "Page 3 of 1,592" (edit-content-v2)
    return tr("Page %1 of %2").arg(PDFAppLocale::formatNumber(m_context->getPageIndex() + 1), PDFAppLocale::formatNumber(m_context->getPageCount()));
}

QString PDFAppToolGUIContent::getOwnTabZoomText() const
{
    // The zoom of the view of the tool ("151 %"); no view, no item
    const PDFAppEditContentView* view = m_context ? m_context->getView() : nullptr;
    return view ? view->getZoomText() : QString();
}

bool PDFAppToolGUIContent::hasOwnStatusItems() const
{
    return true;
}

bool PDFAppToolGUIContent::hasUnsavedWork() const
{
    // The work is the undo stack (RE-16): the same rule as Apply Changes
    return m_context && !m_context->getSession()->getUndoStack()->isClean();
}

PDFAppToolCloseQuestion PDFAppToolGUIContent::getCloseQuestion() const
{
    PDFAppToolCloseQuestion question;
    if (!hasUnsavedWork())
    {
        return question;
    }

    // edit-content-v2-discard: Keep Editing (Primary) | Discard Changes (trash). The frame counts the objects of the
    // facts of a write, which only the writer computes - the question names the pages with unapplied changes.
    const std::vector<PDFInteger> pages = m_context->getSession()->getDirtyPages();
    question.title = tr("Discard the changes to the page content?");
    if (pages.empty())
    {
        question.text = tr("Nothing has been written to the document yet.");
    }
    else if (pages.size() == 1)
    {
        question.text = tr("The content of page %1 was changed. Nothing has been written to the document yet.").arg(PDFAppToolGUIContentHelper::formatPages(pages));
    }
    else
    {
        question.text = tr("The content of pages %1 was changed. Nothing has been written to the document yet.").arg(PDFAppToolGUIContentHelper::formatPages(pages));
    }
    question.keepText = tr("Keep Editing");
    question.discardText = tr("Discard Changes");
    return question;
}

bool PDFAppToolGUIContent::handleEscape()
{
    // KE.16: (the menu, the panel and a drag of a widget are the shell's) the drag of the active tool and its mode,
    // the group, the selection; then the shell leaves the tool with its question
    if (!m_context)
    {
        return false;
    }

    PDFAppEditContentToolController* controller = m_context->getToolController();
    if (controller->getActiveTool() && controller->cancel())
    {
        return true;
    }

    if (!m_context->getIsolationPath().empty())
    {
        m_context->requestLeaveIsolation(1);
        return true;
    }

    PDFAppEditContentSelection* selection = m_context->getSelection();
    if (!selection->isEmpty())
    {
        selection->clear();
        return true;
    }

    return false;
}

QString PDFAppToolGUIContent::getEscapeModeName() const
{
    const PDFAppEditContentToolController* controller = m_context ? m_context->getToolController() : nullptr;
    return controller && controller->getActiveTool() ? controller->getModeLabel() : QString();
}

QString PDFAppToolGUIContent::getEscapeModeHint() const
{
    // The same order as handleEscape
    if (!m_context)
    {
        return QString();
    }

    const PDFAppEditContentToolController* controller = m_context->getToolController();
    if (controller->getActiveTool() && controller->isDragActive())
    {
        return tr("Esc cancels the drag");
    }
    if (!getEscapeModeName().isEmpty())
    {
        return BaseClass::getEscapeModeHint();
    }
    if (!m_context->getIsolationPath().empty())
    {
        return tr("Esc leaves the group");
    }
    if (!m_context->getSelection()->isEmpty())
    {
        return tr("Esc deselects");
    }
    return QString();
}

PDFAppToolUndoTarget* PDFAppToolGUIContent::getOwnUndoTarget()
{
    // The shell asks in enter() after attach, before the mode is entered: the context and the target exist from now on
    ensureContext();
    return m_undoTarget.data();
}

PDFAppToolCommandState PDFAppToolGUIContent::getDocumentCommandState(const QString& commandId) const
{
    // R3-1: the routing of the commands of the application is the context's (the active tool, then the action entry)
    return m_context ? m_context->getApplicationCommandState(commandId) : PDFAppToolCommandState::NotHandled;
}

bool PDFAppToolGUIContent::handleDocumentCommand(const QString& commandId)
{
    // The work runs from the event loop (the context queues it): nothing of the GUI is used after the call
    return m_context && m_context->handleApplicationCommand(commandId);
}

PDFAppToolSaveQuestion PDFAppToolGUIContent::getSaveQuestion() const
{
    PDFAppToolSaveQuestion question;
    if (!m_context)
    {
        return question;
    }

    // edit-content-v2-save-prompt: Cancel | Save Without the Changes | Apply and Save (Primary). The frame says
    // "Ctrl+S was pressed"; Save and Save As of the menu ask the same question.
    const std::vector<PDFInteger> pages = m_context->getSession()->getDirtyPages();
    question.title = tr("Apply the content changes before saving?");
    if (pages.empty())
    {
        question.text = tr("The document is being saved while Edit Content is open. The changes are not part of the document until they are applied.");
    }
    else if (pages.size() == 1)
    {
        question.text = tr("The document is being saved while Edit Content is open. The changes on page %1 are not part of the document until they are applied.")
                            .arg(PDFAppToolGUIContentHelper::formatPages(pages));
    }
    else
    {
        question.text = tr("The document is being saved while Edit Content is open. The changes on pages %1 are not part of the document until they are applied.")
                            .arg(PDFAppToolGUIContentHelper::formatPages(pages));
    }
    question.cancelText = tr("Cancel");
    question.saveText = tr("Save Without the Changes");
    question.applyText = tr("Apply and Save");
    question.applyIconName = QStringLiteral("check");
    return question;
}

bool PDFAppToolGUIContent::isDocumentReloadDeferred() const
{
    // The edits of the session belong to the version of the document they were made on (a reload would turn Apply
    // into the error of the fingerprint)
    return hasUnsavedWork();
}

// ------------------------------------------------------------------ flow

QVariantMap PDFAppToolGUIContent::getRunParameters() const
{
    QVariantMap parameters = BaseClass::getRunParameters();
    if (!m_context)
    {
        return parameters;
    }

    // apply with the snapshot of the session (zadani_gui.md 4.21); the snapshot co-owns its data and outlives the GUI
    parameters.insert(QString::fromLatin1(PDFDocumentToolContent::PARAMETER_MODE), QString::fromLatin1(PDFDocumentToolContent::MODE_APPLY));
    parameters.insert(QString::fromLatin1(PDFDocumentToolContent::PARAMETER_SESSION), QVariant::fromValue(m_context->getSession()->createSnapshot()));
    parameters.insert(QString::fromLatin1(PDFDocumentToolContent::PARAMETER_OPERATIONS), QString());

    // The pages left out after an error of writing (Skip Page of -apply-error)
    PDFDocumentToolPageSelection skippedPages;
    if (m_skippedPages.empty())
    {
        skippedPages.isNone = true;
    }
    else
    {
        QStringList numbers;
        for (const PDFInteger pageIndex : m_skippedPages)
        {
            numbers << QString::number(pageIndex + 1);
        }
        skippedPages.pages = numbers.join(QLatin1Char(','));
    }
    parameters.insert(QString::fromLatin1(PDFDocumentToolContent::PARAMETER_SKIP_PAGES), skippedPages.toVariant());
    return parameters;
}

bool PDFAppToolGUIContent::confirmWithoutRun()
{
    // A value typed into a card and not confirmed yet belongs to the run (zadani_gui.md 4.19): its command first. The
    // shell asks isConfirmEnabled again after this call.
    if (m_context)
    {
        m_context->commitPendingEdits();
    }
    return false;
}

void PDFAppToolGUIContent::onRunNotAccepted(const PDFDocumentToolResult& result)
{
    using H = PDFAppToolGUIContentHelper;
    using Severity = PDFAppEditContentNoticeRequest::Severity;
    if (!m_context)
    {
        return;
    }

    // RE-19: the errors of the pages come by Failed with their data - the dialog -apply-error, never the result dialog
    if (result.status == PDFDocumentToolResultStatus::Failed && result.data.value(QString::fromLatin1(PDFDocumentToolContent::DATA_PAGE_ERRORS)).toArray().size() > 0)
    {
        openApplyErrorDialog(result);
        return;
    }

    // RE-15: the document changed under the tool - the edits cannot be written any more (the error of the fingerprint)
    if (result.status == PDFDocumentToolResultStatus::Failed && result.message == PDFDocumentToolContent::tr("The document changed in the meantime."))
    {
        m_isDocumentStale = true;
        updateStateNotices(false);
        return;
    }

    // Everything else is a notice of the tool (KE.28: the info bars of the shell are not used)
    if (result.status == PDFDocumentToolResultStatus::NothingToDo)
    {
        m_context->showNotice(H::createEvent(PDFAppEditContentNoticeIds::COMMAND_REFUSED, Severity::Information, tr("Nothing to apply"), result.message));
    }
    else
    {
        m_context->showNotice(H::createEvent(PDFAppEditContentNoticeIds::COMMAND_REFUSED, Severity::Error, tr("The changes could not be applied"), result.message));
    }
}

void PDFAppToolGUIContent::fillResultInput(PDFAppToolResultInput* input) const
{
    BaseClass::fillResultInput(input);

    // edit-content-v2-done: "Content of 2 pages changed", "Nothing has been written yet – choose what to do with the
    // result."
    const QJsonArray pages = input->result.data.value(QString::fromLatin1(PDFDocumentToolContent::DATA_PAGES_CHANGED)).toArray();
    if (!pages.isEmpty())
    {
        input->title = pages.size() == 1 ? tr("Content of %Ln page changed", nullptr, int(pages.size())) : tr("Content of %Ln pages changed", nullptr, int(pages.size()));
    }
    input->subtitle = tr("Nothing has been written yet – choose what to do with the result.");
    if (m_canvas)
    {
        input->scrimTarget = m_canvas.data();
    }

    // The frame has a row for each count of the objects (Objects added, edited, removed): the one fact of the core is
    // split by its data, in its place
    std::vector<PDFDocumentToolFact> facts;
    for (const PDFDocumentToolFact& fact : input->result.facts)
    {
        if (fact.id != QLatin1String(PDFDocumentToolContent::FACT_OBJECTS))
        {
            facts.push_back(fact);
            continue;
        }

        const QJsonObject& data = input->result.data;
        facts.push_back({ fact.id + QStringLiteral(".added"), tr("Objects added"), PDFAppLocale::formatNumber(data.value(QString::fromLatin1(PDFDocumentToolContent::DATA_OBJECTS_ADDED)).toInteger()) });
        facts.push_back({ fact.id + QStringLiteral(".edited"), tr("Objects edited"), PDFAppLocale::formatNumber(data.value(QString::fromLatin1(PDFDocumentToolContent::DATA_OBJECTS_EDITED)).toInteger()) });
        facts.push_back({ fact.id + QStringLiteral(".removed"), tr("Objects removed"), PDFAppLocale::formatNumber(data.value(QString::fromLatin1(PDFDocumentToolContent::DATA_OBJECTS_REMOVED)).toInteger()) });
    }
    input->result.facts = std::move(facts);
}

void PDFAppToolGUIContent::onDocumentChanged()
{
    // zadani_gui.md 4.21: a new version of the document under the tool - the edits of the session belong to the
    // version they were made on (Apply would end by the error of the fingerprint). The context has no way to load the
    // session again (a request of P14 to the coordinator): the notice says what to do.
    const PDFAppDocument* document = getDocument();
    if (!m_context || !document || document->getDocument().data() == m_context->getSession()->getDocument())
    {
        return;
    }

    m_isDocumentStale = true;
    updateStateNotices(false);
}

void PDFAppToolGUIContent::retranslate()
{
    // The shell builds the actions and the panels again right after this call: a value typed into a card is committed
    // as its command first (R3-9); then the notices of the states get their new texts (the notices object stays)
    if (!m_context)
    {
        return;
    }

    // The notices of the states get the texts of the new language from the event loop: the shell builds the parts
    // right after this call and expects no foreign code here besides the commit (a request emits into the notices)
    QPointer<PDFAppToolGUIContent> guard(this);
    m_context->commitPendingEdits();
    if (guard && !PDFDeferredDeletion::isRetired(this))
    {
        QMetaObject::invokeMethod(this, &PDFAppToolGUIContent::onRetranslateQueued, Qt::QueuedConnection);
    }
}

void PDFAppToolGUIContent::onRetranslateQueued()
{
    if (!PDFDeferredDeletion::isRetired(this))
    {
        updateStateNotices(false);
    }
}

void PDFAppToolGUIContent::retire()
{
    // Nothing is emitted (R3-9): the cards are parts retired in the same retire, their connections to the context are
    // cut before the hooks run, and each card forgets its pending value in its own hook; the retired context refuses
    // every command (isEditingAllowed). The dialogs of the tool are owned by this GUI (setOwner): they close unanswered.
    BaseClass::retire();
}

// ------------------------------------------------------------------ the notices of the states

void PDFAppToolGUIContent::updateStateNotices(bool isPageEntered)
{
    using H = PDFAppToolGUIContentHelper;
    using Ids = PDFAppEditContentNoticeIds;
    using Severity = PDFAppEditContentNoticeRequest::Severity;

    QPointer<PDFAppEditContentContext> context = m_context;
    if (!context)
    {
        return;
    }

    // The requests only emit to the notices (a child of this GUI): nothing here closes the tool. A state is shown
    // while it holds and hidden when it is left; a new page enters its states again (a closed notice comes back).
    std::vector<std::pair<QString, std::optional<PDFAppEditContentNoticeRequest>>> states;

    // QE.2 (notes 8.3): the declaration of PDF/A and PDF/UA, once per session
    const QStringList declarations = PDFDocumentTool::getConformanceDeclarations(context->getSession()->getDocument());
    const bool isPdfA = declarations.contains(QStringLiteral("PDF/A"));
    const bool isPdfUA = declarations.contains(QStringLiteral("PDF/UA"));
    std::optional<PDFAppEditContentNoticeRequest> conformance;
    if (isPdfA || isPdfUA)
    {
        const QString title = isPdfA && isPdfUA ? tr("This document declares PDF/A and PDF/UA") : (isPdfA ? tr("This document declares PDF/A") : tr("This document declares PDF/UA"));
        const QString text = isPdfA && isPdfUA ? tr("Apply Changes removes both declarations, because edited content is not checked against the standards.")
                                               : (isPdfA ? tr("Apply Changes removes the PDF/A declaration, because edited content is not checked against the standard.")
                                                         : tr("Apply Changes removes the PDF/UA declaration, because edited content is not checked against the standard."));
        conformance = H::createState(Ids::CONFORMANCE, Severity::Information, title, text);
        conformance->isOncePerSession = true;
    }
    states.emplace_back(QString::fromLatin1(Ids::CONFORMANCE), conformance);

    // edit-content-v2-preview
    std::optional<PDFAppEditContentNoticeRequest> preview;
    if (context->isPreviewActive())
    {
        preview = H::createState(Ids::PREVIEW, Severity::Information, tr("Preview – exact rendering"),
                                 tr("The page is drawn by the PDF renderer with your edits applied, so overprint, knockout, blend modes, soft masks and spot colors look as they will be written. Editing is paused."));
    }
    states.emplace_back(QString::fromLatin1(Ids::PREVIEW), preview);

    // The states of the page shown
    const PDFContentContainerPointer container = context->getContainer();
    std::optional<PDFAppEditContentNoticeRequest> readOnly;
    std::optional<PDFAppEditContentNoticeRequest> scanned;
    std::optional<PDFAppEditContentNoticeRequest> complex;
    if (container && container->isReadOnly())
    {
        // zadani_gui.md 4.27 (no frame): shown, the commands refused
        readOnly = H::createState(Ids::READ_ONLY_PAGE, Severity::Warning, tr("This page cannot be edited"),
                                  tr("A content stream of the page cannot be decoded. The page is shown as far as it can be read, and its content stays in the document as it is."));
    }
    else if (container && H::isScannedPage(*container))
    {
        scanned = H::createState(Ids::SCANNED_PAGE, Severity::Information, tr("Scanned page"),
                                 tr("This page is a single image. New content can be added on top of it, but the scanned text cannot be edited. Recognize Text (OCR) adds searchable text without changing the image."));
    }
    m_isScannedPage = scanned.has_value();
    m_isComplexPage = false;
    if (container)
    {
        // QE.10 (edit-content-v2-complex-page): the segments of the paths are no query of the core yet - the objects
        const H::ObjectCounts counts = H::countObjects(*container);
        const PDFInteger objects = counts.original + counts.added;
        m_isComplexPage = objects >= COMPLEX_PAGE_OBJECT_COUNT;
        if (m_isComplexPage)
        {
            complex = H::createState(Ids::COMPLEX_PAGE, Severity::Warning, tr("This page is complex"),
                                     tr("%1 objects. Everything can be edited as usual; moving many objects at once and snapping may respond more slowly.").arg(PDFAppLocale::formatNumber(objects)));
        }
    }
    states.emplace_back(QString::fromLatin1(Ids::READ_ONLY_PAGE), readOnly);
    states.emplace_back(QString::fromLatin1(Ids::SCANNED_PAGE), scanned);
    states.emplace_back(QString::fromLatin1(Ids::COMPLEX_PAGE), complex);

    // KE.20: a page with /UserUnit other than 1
    std::optional<PDFAppEditContentNoticeRequest> userUnit;
    const PDFDocument* document = context->getSession()->getDocument();
    const PDFInteger pageIndex = context->getPageIndex();
    const PDFPage* page = document && pageIndex >= 0 && pageIndex < context->getPageCount() ? document->getCatalog()->getPage(size_t(pageIndex)) : nullptr;
    if (page && !qFuzzyCompare(page->getUserUnit(), 1.0))
    {
        userUnit = H::createState(Ids::USER_UNIT, Severity::Information, tr("The page has its own unit"),
                                  tr("The page sets one unit to %1 points (UserUnit). The editor and its rulers work in the points of the page without it, as this application shows the page.")
                                      .arg(QLocale::c().toString(page->getUserUnit(), 'g', 6)));
    }
    states.emplace_back(QString::fromLatin1(Ids::USER_UNIT), userUnit);

    // RE-15, zadani_gui.md 4.21: the document changed under the tool
    std::optional<PDFAppEditContentNoticeRequest> stale;
    if (m_isDocumentStale)
    {
        stale = H::createState(Ids::STALE_DOCUMENT, Severity::Error, tr("The document has changed"),
                               hasUnsavedWork() ? tr("The edits belong to the previous version of the document and cannot be applied to it. Close the tool and open it again to edit the document as it is now.")
                                                : tr("The tool shows the previous version of the document. Close the tool and open it again to edit the document as it is now."));
    }
    states.emplace_back(QString::fromLatin1(Ids::STALE_DOCUMENT), stale);

    for (const auto& state : states)
    {
        const bool isPageState = state.first == QLatin1String(Ids::READ_ONLY_PAGE) || state.first == QLatin1String(Ids::SCANNED_PAGE) ||
                                 state.first == QLatin1String(Ids::COMPLEX_PAGE) || state.first == QLatin1String(Ids::USER_UNIT);
        if (!context)
        {
            return;
        }
        if (!state.second || (isPageEntered && isPageState))
        {
            context->hideNotice(state.first);
        }
        if (state.second && context)
        {
            context->showNotice(*state.second);
        }
    }
}

// ------------------------------------------------------------------ slots of the children

void PDFAppToolGUIContent::onEditingAllowedChanged()
{
    QPointer<PDFAppToolGUIContent> guard(this);
    updateToolbarStates();

    // Only when an entry answers a command of the application at all. The last call: the shell emits the change of the
    // states into the tab (its slots may close the tool).
    bool hasApplicationCommands = false;
    for (const PDFAppEditContentActionEntry* entry : m_context ? m_context->getRegistry()->getActions() : std::vector<const PDFAppEditContentActionEntry*>())
    {
        hasApplicationCommands = hasApplicationCommands || !entry->getApplicationCommandIds().isEmpty();
    }
    if (guard && hasApplicationCommands)
    {
        updateDocumentCommands();
    }
}

void PDFAppToolGUIContent::onPageRequested(PDFInteger pageIndex)
{
    // zadani_gui.md 4.27: typing ends, the pending values are committed, the group is left, the selection is cleared,
    // then the page is shown (its zoom and position are the canvas's). Each step runs foreign slots: guarded.
    if (!m_context || pageIndex < 0 || pageIndex >= m_context->getPageCount() || pageIndex == m_context->getPageIndex())
    {
        return;
    }

    QPointer<PDFAppToolGUIContent> guard(this);
    QPointer<PDFAppEditContentContext> context = m_context;
    context->commitPendingEdits();
    if (!guard || !context || PDFDeferredDeletion::isRetired(context.data()))
    {
        return;
    }

    // A mode (typing, nodes, crop) ends by the return to Select
    PDFAppEditContentToolController* controller = context->getToolController();
    if (controller->getActiveTool() && controller->getActiveToolId() != QLatin1String(PDFAppEditContentToolIds::SELECT) &&
        context->getRegistry()->findTool(QString::fromLatin1(PDFAppEditContentToolIds::SELECT)))
    {
        controller->activateSelectTool();
        if (!guard || !context || PDFDeferredDeletion::isRetired(context.data()))
        {
            return;
        }
    }

    // The group is left by setPage itself (it clears the isolation path)
    context->getSelection()->clear();
    if (!guard || !context || PDFDeferredDeletion::isRetired(context.data()))
    {
        return;
    }

    context->setPage(pageIndex);
}

void PDFAppToolGUIContent::onPageChanged()
{
    QPointer<PDFAppToolGUIContent> guard(this);
    m_pendingErrorNodeId.clear();
    updateStateNotices(true);
    updateToolbarStates();
    if (!guard)
    {
        return;
    }
    updateStatusText();

    // The answers of the commands of the pages changed (the first and the last page). The last call.
    if (guard)
    {
        updateDocumentCommands();
    }
}

void PDFAppToolGUIContent::onContainerChanged()
{
    QPointer<PDFAppToolGUIContent> guard(this);
    updateStateNotices(false);
    updateToolbarStates();
    selectPendingErrorNode();
    if (guard)
    {
        updateStatusText();
    }
}

void PDFAppToolGUIContent::onPreviewActiveChanged()
{
    QPointer<PDFAppToolGUIContent> guard(this);
    updateStateNotices(false);
    updateToolbarStates();
    if (guard)
    {
        updateStatusText();
    }
}

void PDFAppToolGUIContent::onCommandRefused(const QString& text)
{
    // The refusal of a command is an event: it leaves with the next change of the selection
    if (m_context && !text.isEmpty())
    {
        m_context->showNotice(PDFAppToolGUIContentHelper::createEvent(PDFAppEditContentNoticeIds::COMMAND_REFUSED, PDFAppEditContentNoticeRequest::Severity::Warning,
                                                                      tr("The change was not made"), text));
    }
}

void PDFAppToolGUIContent::onModelChanged()
{
    if (!m_context)
    {
        return;
    }

    // A change of the model after an error of writing: the skipped pages are asked again by the next error
    m_skippedPages.clear();

    QPointer<PDFAppToolGUIContent> guard(this);
    updateToolbarStates();
    updateStatusText();
    if (!guard || !m_context)
    {
        return;
    }

    // Apply Changes, the work, the save question and the deferred reload follow the clean point of the undo stack
    // (also by Undo and Redo): confirmStateChanged at every crossing (the shell refreshes the actions of the window)
    const bool isClean = m_context->getSession()->getUndoStack()->isClean();
    if (isClean != m_isClean)
    {
        m_isClean = isClean;
        updateConfirmState();
    }
}

void PDFAppToolGUIContent::onOptionsChanged(const QString& name)
{
    if (name == QLatin1String(PDFAppEditContentOptions::LEFT_TAB))
    {
        updateLeftTab();
    }
    updateToolbarStates();
}

void PDFAppToolGUIContent::onToolStateChanged()
{
    updateToolbarStates();
    updateStatusText();
}

void PDFAppToolGUIContent::onSelectionChanged()
{
    // The status text tells the selection and what Esc does
    updateStatusText();
}

// ------------------------------------------------------------------ slots of the parts

void PDFAppToolGUIContent::onToolButtonClicked(QAbstractButton* button)
{
    if (!m_context || !button)
    {
        return;
    }

    // The controller commits the pending values and switches the tool; a refusal (Preview, a run) keeps the old one
    const QString toolId = button->property(PDFAppToolGUIContentHelper::ID_PROPERTY).toString();
    QPointer<PDFAppToolGUIContent> guard(this);
    if (!m_context->getToolController()->activateTool(toolId) && guard)
    {
        updateToolbarStates();
    }
}

void PDFAppToolGUIContent::onMultipleToggled(bool isChecked)
{
    if (m_context)
    {
        m_context->getToolController()->setMultiple(isChecked);
    }
}

void PDFAppToolGUIContent::onActionButtonClicked()
{
    const QAbstractButton* button = qobject_cast<const QAbstractButton*>(sender());
    if (!m_context || !button)
    {
        return;
    }

    // The action runs from the event loop (the context queues it); the check of a toggle follows its entry, not the
    // click (the action may be refused)
    const QString actionId = button->property(PDFAppToolGUIContentHelper::ID_PROPERTY).toString();
    m_context->triggerAction(actionId);
    updateToolbarStates();
}

void PDFAppToolGUIContent::onViewButtonToggled(bool isChecked)
{
    // The toolbar checks the button right before it opens the panel (a click, the keys, the menu bar of the
    // panels): the rows follow the state of now
    if (isChecked && m_menus && m_viewPanel)
    {
        m_menus->fillPanel(m_viewPanel.data(), QString::fromLatin1(PDFAppEditContentMenuIds::VIEW));
    }
}

void PDFAppToolGUIContent::onTabClicked(int index)
{
    // The tab is an option of the tool (stored, zadani_gui.md 4.26); the stack follows its change
    if (m_context && index >= 0 && index < int(m_tabIds.size()))
    {
        m_context->getOptions()->setValue(QString::fromLatin1(PDFAppEditContentOptions::LEFT_TAB), m_tabIds[size_t(index)]);
    }
}

void PDFAppToolGUIContent::onViewChanged()
{
    updateStatusText();
}

// ------------------------------------------------------------------ Clear Page, Go to page

PDFMessageBox* PDFAppToolGUIContent::openClearPageQuestion()
{
    using H = PDFAppToolGUIContentHelper;
    const PDFContentContainerPointer container = m_context ? m_context->getContainer() : nullptr;
    if (!container || !m_context->isEditingAllowed())
    {
        return nullptr;
    }
    if (m_clearQuestion)
    {
        m_clearQuestion->raise();
        return m_clearQuestion.data();
    }

    const H::ObjectCounts counts = H::countObjects(*container);
    if (counts.original + counts.added == 0)
    {
        return nullptr;
    }

    // edit-content-v2-clear-confirm: Cancel (Primary, leading) | Clear Page (eraser)
    const int added = int(counts.added);
    const int original = int(counts.original);
    const QString addedPart = added == 1 ? tr("the %Ln new object", nullptr, added) : tr("the %Ln new objects", nullptr, added);
    const QString originalPart = original == 1 ? tr("the %Ln object of the original content", nullptr, original) : tr("the %Ln objects of the original content", nullptr, original);
    QString removed;
    if (added > 0 && original > 0)
    {
        removed = tr("Every object of the page is removed – %1 and %2.").arg(addedPart, originalPart);
    }
    else
    {
        removed = tr("Every object of the page is removed – %1.").arg(added > 0 ? addedPart : originalPart);
    }
    const QString text = removed + QLatin1Char(' ') + tr("Layers and tags stay in the document. Undo brings the content back while the tool is open.");

    QWidget* parent = getDialogParent();
    PDFMessageBox* dialog = H::createDialog(this, PDFThemeIcons::Badge::Critical, tr("Clear page %1?").arg(PDFAppLocale::formatNumber(m_context->getPageIndex() + 1)), text,
                                            QStringLiteral("editContentClearQuestion"), parent);
    QPushButton* cancel = dialog->addActionButton(tr("Cancel"), PDFMessageBox::ActionPosition::Leading, true);
    cancel->setObjectName(QStringLiteral("editContentClearCancel"));
    connect(cancel, &QPushButton::clicked, dialog, &QDialog::reject);
    QPushButton* clear = dialog->addActionButton(tr("Clear Page"), PDFMessageBox::ActionPosition::Trailing, false);
    clear->setObjectName(QStringLiteral("editContentClearConfirm"));
    clear->setIcon(PDFThemeIcons::getIcon(QStringLiteral("eraser")));
    connect(clear, &QPushButton::clicked, this, &PDFAppToolGUIContent::onClearPageConfirmed);
    m_clearQuestion = dialog;

    H::showDialog(dialog, parent);
    return PDFDeferredDeletion::isRetired(this) ? nullptr : m_clearQuestion.data();
}

void PDFAppToolGUIContent::onClearPageConfirmed()
{
    if (PDFMessageBox* dialog = m_clearQuestion.data())
    {
        dialog->accept();
    }
    if (PDFDeferredDeletion::isRetired(this) || !m_context || !m_context->isEditingAllowed())
    {
        return;
    }

    // One undo step; a refusal is told by commandRefused (the notice). The last call: the slots of the model run.
    m_context->execute(PDFContentCommandFactory::createClearPage(m_context->getPageKey()));
}

PDFMessageBox* PDFAppToolGUIContent::openGoToPage()
{
    using H = PDFAppToolGUIContentHelper;
    if (!m_context || m_context->getPageCount() < 2)
    {
        return nullptr;
    }
    if (m_goToPageQuestion)
    {
        m_goToPageQuestion->raise();
        return m_goToPageQuestion.data();
    }

    // No frame: the Go to panel of the document lives in the toolbar of the document, hidden while the tool is open -
    // a question of the page number in the dialog of the tool
    const PDFInteger pageCount = m_context->getPageCount();
    QWidget* parent = getDialogParent();
    PDFMessageBox* dialog = H::createDialog(this, PDFThemeIcons::Badge::Question, tr("Go to page"),
                                            tr("Pages 1 to %1. The changes of the page shown stay in the tool.").arg(PDFAppLocale::formatNumber(pageCount)),
                                            QStringLiteral("editContentGoToPage"), parent);
    PDFSpinBox* field = new PDFSpinBox(dialog);
    field->setObjectName(QStringLiteral("editContentGoToPageField"));
    field->setRange(1, int(qMin(pageCount, PDFInteger(std::numeric_limits<int>::max()))));
    field->setValue(int(m_context->getPageIndex() + 1));
    field->setAccessibleName(tr("Page number"));
    dialog->addContent(field);
    QPushButton* cancel = dialog->addActionButton(tr("Cancel"), PDFMessageBox::ActionPosition::Trailing, false);
    cancel->setObjectName(QStringLiteral("editContentGoToPageCancel"));
    connect(cancel, &QPushButton::clicked, dialog, &QDialog::reject);
    QPushButton* go = dialog->addActionButton(tr("Go to Page"), PDFMessageBox::ActionPosition::Trailing, true);
    go->setObjectName(QStringLiteral("editContentGoToPageConfirm"));
    connect(go, &QPushButton::clicked, this, &PDFAppToolGUIContent::onGoToPageConfirmed);
    m_goToPageQuestion = dialog;
    m_goToPageField = field;

    H::showDialog(dialog, parent);
    if (m_goToPageField)
    {
        m_goToPageField->selectAll();
        m_goToPageField->setFocus();
    }
    return PDFDeferredDeletion::isRetired(this) ? nullptr : m_goToPageQuestion.data();
}

void PDFAppToolGUIContent::onGoToPageConfirmed()
{
    // The number typed and not confirmed by Enter counts (interpretText)
    PDFInteger pageIndex = -1;
    if (QSpinBox* field = m_goToPageField.data())
    {
        field->interpretText();
        pageIndex = PDFInteger(field->value()) - 1;
    }
    if (PDFMessageBox* dialog = m_goToPageQuestion.data())
    {
        dialog->accept();
    }

    // The rules of a switch (onPageRequested); the last call
    if (!PDFDeferredDeletion::isRetired(this) && m_context && pageIndex >= 0)
    {
        m_context->requestPage(pageIndex);
    }
}

// ------------------------------------------------------------------ errors of writing (-apply-error)

void PDFAppToolGUIContent::openApplyErrorDialog(const PDFDocumentToolResult& result)
{
    using H = PDFAppToolGUIContentHelper;

    // A dialog of an older run goes (its pages are not this run's)
    if (PDFMessageBox* previous = m_applyErrorDialog.data())
    {
        m_applyErrorDialog = nullptr;
        previous->close();
    }

    // DATA_PAGE_ERRORS: [ { page (from 1; 0 = no page), errors: [ { id, form, text } ] } ]. Hostile in size (one
    // error per node): every error is counted, MAXIMUM_LISTED_ERRORS are listed
    const QJsonArray pages = result.data.value(QString::fromLatin1(PDFDocumentToolContent::DATA_PAGE_ERRORS)).toArray();
    QStringList rows;
    int errorCount = 0;
    m_errorPages.clear();
    m_errorPage = -1;
    m_errorNodeId.clear();
    for (const QJsonValue& pageValue : pages)
    {
        const QJsonObject page = pageValue.toObject();
        const PDFInteger pageNumber = page.value(QStringLiteral("page")).toInteger();
        const QJsonArray errors = page.value(QStringLiteral("errors")).toArray();
        if (pageNumber > 0)
        {
            m_errorPages.push_back(pageNumber - 1);
        }

        for (const QJsonValue& errorValue : errors)
        {
            const QJsonObject error = errorValue.toObject();
            ++errorCount;
            if (m_errorNodeId.isEmpty() && pageNumber > 0 && !error.value(QStringLiteral("id")).toString().isEmpty())
            {
                m_errorPage = pageNumber - 1;
                m_errorNodeId = error.value(QStringLiteral("id")).toString();
            }
            if (rows.size() < MAXIMUM_LISTED_ERRORS)
            {
                const QString text = error.value(QStringLiteral("text")).toString();
                rows << (pageNumber > 0 ? tr("Page %1: %2").arg(PDFAppLocale::formatNumber(pageNumber), text) : text);
            }
        }
    }
    if (m_errorPage < 0 && !m_errorPages.empty())
    {
        m_errorPage = m_errorPages.front();
    }

    // edit-content-v2-apply-error: "2 errors on page 7. Pages without errors are ready."
    const QString errors = errorCount == 1 ? tr("%Ln error", nullptr, errorCount) : tr("%Ln errors", nullptr, errorCount);
    QString text;
    if (m_errorPages.empty())
    {
        text = tr("%1 of the document. Nothing was written.").arg(errors);
    }
    else if (m_errorPages.size() == 1)
    {
        text = tr("%1 on page %2. Pages without errors are ready.").arg(errors, PDFAppLocale::formatNumber(m_errorPages.front() + 1));
    }
    else
    {
        text = tr("%1 on pages %2. Pages without errors are ready.").arg(errors, H::formatPages(m_errorPages));
    }

    QWidget* parent = getDialogParent();
    PDFMessageBox* dialog = H::createDialog(this, PDFThemeIcons::Badge::Critical, tr("Some content could not be written"), text, QStringLiteral("editContentApplyError"), parent);

    QWidget* list = new QWidget(dialog);
    list->setObjectName(QStringLiteral("editContentApplyErrorList"));
    QVBoxLayout* listLayout = new QVBoxLayout(list);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(4);
    for (const QString& row : std::as_const(rows))
    {
        PDFLabel* label = new PDFLabel(row, list);
        label->setWordWrap(true);
        label->setDataText(true);
        listLayout->addWidget(label);
    }
    if (errorCount > rows.size())
    {
        const int moreCount = errorCount - int(rows.size());
        PDFLabel* more = new PDFLabel(moreCount == 1 ? tr("%Ln more error", nullptr, moreCount) : tr("%Ln more errors", nullptr, moreCount), list);
        listLayout->addWidget(more);
    }

    // The explanation of the two choices
    QString explanation;
    if (m_errorPages.size() == 1)
    {
        const QString number = PDFAppLocale::formatNumber(m_errorPages.front() + 1);
        explanation = tr("Skip Page %1 writes the other pages; page %1 stays as it is in the document and its edits are not written. "
                         "Back to Editing keeps the tool open and selects the first object with an error.").arg(number);
    }
    else if (m_errorPages.size() > 1)
    {
        explanation = tr("Skip These Pages writes the other pages; pages %1 stay as they are in the document and their edits are not written. "
                         "Back to Editing keeps the tool open and selects the first object with an error.").arg(H::formatPages(m_errorPages));
    }
    else
    {
        explanation = tr("Back to Editing keeps the tool open and selects the first object with an error.");
    }
    PDFLabel* explanationLabel = new PDFLabel(explanation, list);
    explanationLabel->setObjectName(QStringLiteral("editContentApplyErrorExplanation"));
    explanationLabel->setWordWrap(true);
    listLayout->addWidget(explanationLabel);
    dialog->addContent(list);

    // Back to Editing (leading) | Skip Page 7 (Primary, skip); errors outside of pages cannot be skipped
    QPushButton* back = dialog->addActionButton(tr("Back to Editing"), PDFMessageBox::ActionPosition::Leading, m_errorPages.empty());
    back->setObjectName(QStringLiteral("editContentApplyErrorBack"));
    connect(back, &QPushButton::clicked, this, &PDFAppToolGUIContent::onApplyErrorBack);
    if (!m_errorPages.empty())
    {
        const QString skipText = m_errorPages.size() == 1 ? tr("Skip Page %1").arg(PDFAppLocale::formatNumber(m_errorPages.front() + 1)) : tr("Skip These Pages");
        QPushButton* skip = dialog->addActionButton(skipText, PDFMessageBox::ActionPosition::Trailing, true);
        skip->setObjectName(QStringLiteral("editContentApplyErrorSkip"));
        skip->setIcon(PDFThemeIcons::getIcon(QStringLiteral("skip")));
        connect(skip, &QPushButton::clicked, this, &PDFAppToolGUIContent::onApplyErrorSkip);
    }
    m_applyErrorDialog = dialog;

    H::showDialog(dialog, parent);
}

void PDFAppToolGUIContent::onApplyErrorBack()
{
    if (PDFMessageBox* dialog = m_applyErrorDialog.data())
    {
        dialog->reject();
    }
    if (PDFDeferredDeletion::isRetired(this) || !m_context)
    {
        return;
    }

    // The page of the first error, then its object (selected when the container of the page is loaded)
    QPointer<PDFAppToolGUIContent> guard(this);
    const PDFInteger pageIndex = m_errorPage;
    const QString nodeId = m_errorNodeId;
    if (pageIndex >= 0 && pageIndex != m_context->getPageIndex())
    {
        m_context->requestPage(pageIndex);
        if (!guard || PDFDeferredDeletion::isRetired(this) || !m_context || m_context->getPageIndex() != pageIndex)
        {
            return;
        }
    }

    m_pendingErrorNodeId = nodeId;
    selectPendingErrorNode();
}

void PDFAppToolGUIContent::selectPendingErrorNode()
{
    if (m_pendingErrorNodeId.isEmpty() || !m_context || !m_context->getContainer())
    {
        return;
    }

    // The identifier names a node of the page, or of a form drawn by the page (the selection of a node of a form is
    // the use of the form until the group is entered, stage E4)
    QString errorText;
    const PDFContentNodeReference node = PDFContentJson::readNodeId(m_pendingErrorNodeId, m_context->getSession(), &errorText);
    m_pendingErrorNodeId.clear();
    if (!node.isValid())
    {
        return;
    }

    PDFContentNodeReference selected = node;
    if (node.container != m_context->getPageKey())
    {
        const QString pagePart = m_errorNodeId.section(QLatin1Char('/'), 0, 0);
        selected = PDFContentJson::readNodeId(pagePart, m_context->getSession(), &errorText);
    }
    if (!selected.isValid())
    {
        return;
    }

    // The last call: the slots of the selection run (the tree, the canvas, the cards)
    m_context->getSelection()->setNodes({ selected });
}

void PDFAppToolGUIContent::onApplyErrorSkip()
{
    if (PDFMessageBox* dialog = m_applyErrorDialog.data())
    {
        dialog->accept();
    }
    if (PDFDeferredDeletion::isRetired(this))
    {
        return;
    }

    // The run again without the pages of the errors (zadani_gui.md 4.21) - from the event loop: the shell is not
    // confirmed inside of the click of its dialog
    m_skippedPages.insert(m_errorPages.cbegin(), m_errorPages.cend());
    QMetaObject::invokeMethod(this, &PDFAppToolGUIContent::onSkipRunQueued, Qt::QueuedConnection);
}

void PDFAppToolGUIContent::onSkipRunQueued()
{
    // The last call: the shell runs the tool (its slots may close it)
    PDFDocumentToolShell* shell = getShell();
    if (!PDFDeferredDeletion::isRetired(this) && shell && !m_skippedPages.empty())
    {
        shell->confirm();
    }
}

}   // namespace pdf

#include "moc_pdfapptoolgui_content.cpp"
