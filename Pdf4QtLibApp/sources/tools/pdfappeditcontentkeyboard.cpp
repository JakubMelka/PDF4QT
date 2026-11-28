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

#include "pdfappeditcontentkeyboard.h"

#include "pdfappcommand.h"
#include "pdfappeditcontentcontext.h"
#include "pdfappeditcontentregistry.h"
#include "pdfappeditcontentview.h"
#include "pdfapptoolgui_content.h"
#include "pdfapptooltoolbar.h"

#include "pdfcomponentbuttons.h"
#include "pdfcomponentpanels.h"
#include "pdfcomponentsections.h"
#include "pdffluentwidgets.h"

#include <QAction>
#include <QApplication>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace pdf
{

// ---------------------------------------------------------------- the entries of this file (registerParts)

/// The toggle Preview (zadani_gui.md 4.15; the key P of the canvas; the toolbar after View): the exact picture of
/// the page, the editing paused. It acts also while Preview is on (to end it) - not during a run.
class PDFAppEditContentPreviewEntry : public PDFAppEditContentActionEntry
{
public:
    virtual QString getId() const override { return QString::fromLatin1(PDFAppEditContentActionIds::PREVIEW); }
    virtual QString getTitle(const PDFAppEditContentContext* context) const override { Q_UNUSED(context); return PDFAppEditContentKeyboard::tr("Preview"); }
    virtual QString getIconName() const override { return QStringLiteral("preview-exact"); }

    virtual std::vector<Key> getKeys() const override
    {
        Key key;
        key.sequence = QKeySequence(Qt::Key_P);
        key.scope = PDFAppEditContentKeyScope::Canvas;
        return { key };
    }

    virtual std::vector<Placement> getPlacements() const override
    {
        Placement placement;
        placement.menuId = QString::fromLatin1(PDFAppEditContentMenuIds::TOOLBAR);
        placement.group = PDFAppEditContentMenuIds::GROUP_TOOLBAR_VIEW;
        placement.order = 200;
        return { placement };
    }

    virtual bool isEnabled(const PDFAppEditContentContext* context) const override
    {
        // A page with its content: the preview writes the snapshot of the loaded page
        return context && !context->isRunning() && (context->isPreviewActive() || context->getContainer() != nullptr);
    }

    virtual bool isCheckable() const override { return true; }
    virtual bool isChecked(const PDFAppEditContentContext* context) const override { return context && context->isPreviewActive(); }

    virtual void trigger(PDFAppEditContentContext* context) const override
    {
        // A value typed into a card belongs to the picture (zadani_gui.md 4.19); its command may close the tool
        QPointer<PDFAppEditContentContext> guard(context);
        context->commitPendingEdits();
        if (guard && !PDFDeferredDeletion::isRetired(guard.data()))
        {
            guard->setPreviewActive(!guard->isPreviewActive());
        }
    }
};

/// View > Keyboard Shortcuts and Ctrl+/ (zadani.md KE.22): the card Keyboard of the right panel
class PDFAppEditContentKeyboardShortcutsEntry : public PDFAppEditContentActionEntry
{
public:
    virtual QString getId() const override { return QString::fromLatin1(PDFAppEditContentActionIds::VIEW_KEYBOARD); }
    virtual QString getTitle(const PDFAppEditContentContext* context) const override { Q_UNUSED(context); return PDFAppEditContentKeyboard::tr("Keyboard Shortcuts"); }

    virtual std::vector<Key> getKeys() const override
    {
        Key key;
        key.sequence = QKeySequence(Qt::CTRL | Qt::Key_Slash);
        key.scope = PDFAppEditContentKeyScope::Tool;
        return { key };
    }

    virtual std::vector<Placement> getPlacements() const override
    {
        Placement placement;
        placement.menuId = QString::fromLatin1(PDFAppEditContentMenuIds::VIEW);
        placement.group = PDFAppEditContentMenuIds::GROUP_SETTINGS;
        placement.order = 200;
        return { placement };
    }

    /// The list of the keys is no editing: also in Preview and during a run
    virtual bool isEnabled(const PDFAppEditContentContext* context) const override { return context != nullptr; }

    virtual void trigger(PDFAppEditContentContext* context) const override
    {
        // The key and the row open the card; the key closes it again (the link Close of the card too)
        const QString cardId = QString::fromLatin1(PDFAppEditContentCardIds::KEYBOARD);
        context->setCardRequested(cardId, !context->isCardRequested(cardId));
    }
};

/// Clear Page of the toolbar (zadani_gui.md 4.21): the confirmation edit-content-v2-clear-confirm, then ClearPage
class PDFAppEditContentClearPageEntry : public PDFAppEditContentActionEntry
{
public:
    virtual QString getId() const override { return QString::fromLatin1(PDFAppEditContentActionIds::CLEAR_PAGE); }
    virtual QString getTitle(const PDFAppEditContentContext* context) const override { Q_UNUSED(context); return PDFAppEditContentKeyboard::tr("Clear Page"); }
    virtual QString getIconName() const override { return QStringLiteral("eraser"); }

    virtual std::vector<Placement> getPlacements() const override
    {
        Placement placement;
        placement.menuId = QString::fromLatin1(PDFAppEditContentMenuIds::TOOLBAR);
        placement.group = PDFAppEditContentMenuIds::GROUP_TOOLBAR_PAGE;
        placement.order = 100;
        return { placement };
    }

    /// The page can be edited and has an object (a ClearPage of a page without objects changes nothing, R8-10): the
    /// container of the page, also while a group is entered (R8-4: the page is cleared, not the edited root)
    virtual bool isEnabled(const PDFAppEditContentContext* context) const override
    {
        if (!context || !context->isEditingAllowed())
        {
            return false;
        }
        const PDFContentContainerKey pageKey = context->getPageKey();
        const PDFContentContainerPointer container = pageKey.isValid() ? context->getSession()->getContainer(pageKey) : PDFContentContainerPointer();
        return container && PDFAppToolGUIContent::hasObjects(*container);
    }

    virtual void trigger(PDFAppEditContentContext* context) const override
    {
        // The question belongs to the GUI (its dialogs are owned by it)
        if (PDFAppToolGUIContent* gui = qobject_cast<PDFAppToolGUIContent*>(context->getGUI()))
        {
            gui->openClearPageQuestion();
        }
    }
};

/// A command of the application, which acts on the canvas of the tool (zadani_gui.md 4.20, way 2): the zoom and the
/// fits of the paper. Visible only with a view (the canvas of the package P11); it acts also in Preview (4.15: a
/// change of the zoom renders the preview again).
class PDFAppEditContentViewCommandEntry : public PDFAppEditContentActionEntry
{
public:
    explicit PDFAppEditContentViewCommandEntry(QString commandId) : m_commandId(std::move(commandId)) { }

    virtual QString getId() const override { return PDFAppEditContentKeyboard::getCommandActionId(m_commandId); }
    virtual QString getTitle(const PDFAppEditContentContext* context) const override { Q_UNUSED(context); return m_commandId; }
    virtual QStringList getApplicationCommandIds() const override { return { m_commandId }; }
    virtual bool isVisible(const PDFAppEditContentContext* context) const override { return context && context->getView() != nullptr; }
    virtual bool isEnabled(const PDFAppEditContentContext* context) const override { return context && context->getView() != nullptr && !context->isRunning(); }

    virtual void trigger(PDFAppEditContentContext* context) const override
    {
        using C = PDFAppCommands;
        PDFAppEditContentView* view = context->getView();
        if (!view)
        {
            return;
        }

        if (m_commandId == C::zoomIn())
        {
            view->zoomIn();
        }
        else if (m_commandId == C::zoomOut())
        {
            view->zoomOut();
        }
        else if (m_commandId == C::actualSize())
        {
            view->zoomActualSize();
        }
        else if (m_commandId == C::fitPage())
        {
            view->fitPage();
        }
        else if (m_commandId == C::fitWidth())
        {
            view->fitWidth();
        }
        else if (m_commandId == C::fitHeight())
        {
            view->fitHeight();
        }
    }

private:
    QString m_commandId;
};

/// A command of the application, which switches the page of the tool (zadani_gui.md 4.20, way 2; 4.27): the first,
/// the previous, the next, the last page. The switch is a request: the GUI applies the rules of a switch.
class PDFAppEditContentPageCommandEntry : public PDFAppEditContentActionEntry
{
public:
    explicit PDFAppEditContentPageCommandEntry(QString commandId) : m_commandId(std::move(commandId)) { }

    virtual QString getId() const override { return PDFAppEditContentKeyboard::getCommandActionId(m_commandId); }
    virtual QString getTitle(const PDFAppEditContentContext* context) const override { Q_UNUSED(context); return m_commandId; }
    virtual QStringList getApplicationCommandIds() const override { return { m_commandId }; }

    /// The target page exists and is another page; never during a run
    virtual bool isEnabled(const PDFAppEditContentContext* context) const override
    {
        if (!context || context->isRunning())
        {
            return false;
        }
        const PDFInteger target = getTargetPage(context);
        return target >= 0 && target < context->getPageCount() && target != context->getPageIndex();
    }

    virtual void trigger(PDFAppEditContentContext* context) const override
    {
        context->requestPage(getTargetPage(context));
    }

private:
    PDFInteger getTargetPage(const PDFAppEditContentContext* context) const
    {
        using C = PDFAppCommands;
        const PDFInteger current = context->getPageIndex();
        const PDFInteger count = context->getPageCount();
        if (current < 0 || count <= 0)
        {
            return -1;
        }
        if (m_commandId == C::firstPage())
        {
            return 0;
        }
        if (m_commandId == C::lastPage())
        {
            return count - 1;
        }
        if (m_commandId == C::nextPage())
        {
            return current + 1;
        }
        if (m_commandId == C::previousPage())
        {
            return current - 1;
        }
        return -1;
    }

    QString m_commandId;
};

/// Go to page… of the application (Ctrl+G; zadani_gui.md 4.20): the question of the page number of the GUI
class PDFAppEditContentGoToPageEntry : public PDFAppEditContentActionEntry
{
public:
    virtual QString getId() const override { return PDFAppEditContentKeyboard::getCommandActionId(PDFAppCommands::goToPage()); }
    virtual QString getTitle(const PDFAppEditContentContext* context) const override { Q_UNUSED(context); return PDFAppEditContentKeyboard::tr("Go to Page"); }
    virtual QStringList getApplicationCommandIds() const override { return { PDFAppCommands::goToPage() }; }
    virtual bool isEnabled(const PDFAppEditContentContext* context) const override { return context && !context->isRunning() && context->getPageCount() > 1; }

    virtual void trigger(PDFAppEditContentContext* context) const override
    {
        if (PDFAppToolGUIContent* gui = qobject_cast<PDFAppToolGUIContent*>(context->getGUI()))
        {
            gui->openGoToPage();
        }
    }
};

/// The card Keyboard: shown while it is requested
class PDFAppEditContentKeyboardCardEntry : public PDFAppEditContentCardEntry
{
public:
    virtual QString getId() const override { return QString::fromLatin1(PDFAppEditContentCardIds::KEYBOARD); }
    virtual bool isShown(const PDFAppEditContentContext* context) const override { return context && context->isCardRequested(getId()); }
    virtual PDFAppEditContentCard* createCard(PDFAppEditContentContext* context, QWidget* parent) const override { return new PDFAppEditContentKeyboardCard(context, parent); }

    /// The list of the keys and its Close link work in Preview, during a run and on a read-only page (R7-8)
    virtual bool isChangingModel() const override { return false; }
};

/// What the keyboard needs besides the entries
class PDFAppEditContentKeyboardHelper
{
public:
    PDFAppEditContentKeyboardHelper() = delete;

    /// The property of an installed action, which names its action entry
    static constexpr const char* ACTION_ID_PROPERTY = "pdf4qtEditContentActionId";

    /// The commands of the application of the zoom and of the fits (the view of the tool)
    static QStringList getViewCommands()
    {
        using C = PDFAppCommands;
        return { C::zoomIn(), C::zoomOut(), C::actualSize(), C::fitPage(), C::fitWidth(), C::fitHeight() };
    }

    /// The commands of the application of the pages
    static QStringList getPageCommands()
    {
        using C = PDFAppCommands;
        return { C::firstPage(), C::previousPage(), C::nextPage(), C::lastPage() };
    }

    /// The widget of a region gets the focus: its first child, which takes the focus by Tab (the widget itself
    /// otherwise). \returns the focus was moved.
    static bool focusRegion(QWidget* region)
    {
        if (!region || !region->isVisible() || !region->isEnabled())
        {
            return false;
        }

        for (QWidget* child : region->findChildren<QWidget*>())
        {
            if (child->isVisible() && child->isEnabled() && (child->focusPolicy() & Qt::TabFocus) == Qt::TabFocus)
            {
                child->setFocus(Qt::TabFocusReason);
                return true;
            }
        }

        if ((region->focusPolicy() & Qt::TabFocus) == Qt::TabFocus)
        {
            region->setFocus(Qt::TabFocusReason);
            return true;
        }
        return false;
    }
};

// ---------------------------------------------------------------- PDFAppEditContentKeyboard

PDFAppEditContentKeyboard::PDFAppEditContentKeyboard(PDFAppEditContentContext* context, QObject* parent) :
    BaseClass(parent),
    PDFRetirable(this),
    m_context(context)
{

}

PDFAppEditContentKeyboard::~PDFAppEditContentKeyboard()
{
    uninstall();
}

void PDFAppEditContentKeyboard::install(QWidget* keyWidget)
{
    uninstall();
    if (!keyWidget || !m_context || PDFDeferredDeletion::isRetired(this))
    {
        return;
    }

    // Way 3 of zadani_gui.md 4.20: the shortcuts of the scope Tool of the action entries, on the key widget of the
    // host (the whole tool, also the panels)
    m_keyWidget = keyWidget;
    for (const PDFAppEditContentKeyRow& row : PDFAppEditContentKeyTable::getRows(*m_context->getRegistry()))
    {
        if (row.scope != PDFAppEditContentKeyScope::Tool || row.actionId.isEmpty() || row.sequence.isEmpty())
        {
            continue;
        }

        QAction* action = new QAction(this);
        action->setObjectName(QStringLiteral("editContentKey-") + row.actionId);
        action->setShortcut(row.sequence);
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        action->setProperty(PDFAppEditContentKeyboardHelper::ACTION_ID_PROPERTY, row.actionId);
        connect(action, &QAction::triggered, this, &PDFAppEditContentKeyboard::onActionTriggered);
        keyWidget->addAction(action);
        m_actions.push_back(action);
    }

    // F6 / Shift+F6: the regions of the window (zadani_gui.md 4.23); no command of the application uses them
    QAction* next = new QAction(this);
    next->setObjectName(QStringLiteral("editContentNextRegion"));
    next->setShortcut(QKeySequence(Qt::Key_F6));
    next->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(next, &QAction::triggered, this, &PDFAppEditContentKeyboard::onNextRegion);
    keyWidget->addAction(next);
    m_actions.push_back(next);

    QAction* previous = new QAction(this);
    previous->setObjectName(QStringLiteral("editContentPreviousRegion"));
    previous->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F6));
    previous->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(previous, &QAction::triggered, this, &PDFAppEditContentKeyboard::onPreviousRegion);
    keyWidget->addAction(previous);
    m_actions.push_back(previous);
}

void PDFAppEditContentKeyboard::uninstall()
{
    // The key widget is foreign (the view of the document): the actions leave it, they are deleted with this object
    QWidget* keyWidget = m_keyWidget.data();
    for (const QPointer<QAction>& action : m_actions)
    {
        if (action && keyWidget)
        {
            keyWidget->removeAction(action.data());
        }
    }
    for (const QPointer<QAction>& action : m_actions)
    {
        delete action.data();
    }
    m_actions.clear();
    m_keyWidget = nullptr;
}

std::vector<QAction*> PDFAppEditContentKeyboard::getActions() const
{
    std::vector<QAction*> actions;
    for (const QPointer<QAction>& action : m_actions)
    {
        if (action)
        {
            actions.push_back(action.data());
        }
    }
    return actions;
}

QString PDFAppEditContentKeyboard::getCommandActionId(const QString& commandId)
{
    return QStringLiteral("command.") + commandId;
}

void PDFAppEditContentKeyboard::registerParts(PDFAppEditContentRegistry* registry)
{
    registry->addAction(std::make_unique<PDFAppEditContentPreviewEntry>());
    registry->addAction(std::make_unique<PDFAppEditContentKeyboardShortcutsEntry>());
    registry->addAction(std::make_unique<PDFAppEditContentClearPageEntry>());

    // The commands of the application routed to the tool (zadani_gui.md 4.20, way 2; pe1_packages.md P14)
    for (const QString& commandId : PDFAppEditContentKeyboardHelper::getViewCommands())
    {
        registry->addAction(std::make_unique<PDFAppEditContentViewCommandEntry>(commandId));
    }
    for (const QString& commandId : PDFAppEditContentKeyboardHelper::getPageCommands())
    {
        registry->addAction(std::make_unique<PDFAppEditContentPageCommandEntry>(commandId));
    }
    registry->addAction(std::make_unique<PDFAppEditContentGoToPageEntry>());

    registry->addCard(std::make_unique<PDFAppEditContentKeyboardCardEntry>());
}

std::vector<std::pair<QString, QString>> PDFAppEditContentKeyboard::getKeyRows(const PDFAppEditContentRegistry& registry)
{
    std::vector<std::pair<QString, QString>> rows;

    // The keys of the canvas, which the tool Select handles (edit-content-v2-keyboard); hidden without the tool
    if (registry.findTool(QString::fromLatin1(PDFAppEditContentToolIds::SELECT)))
    {
        rows.emplace_back(tr("Tab"), tr("Next object"));
        rows.emplace_back(tr("Shift+Tab"), tr("Previous object"));
        rows.emplace_back(tr("Arrows"), tr("Move by 1 pt"));
        rows.emplace_back(tr("Shift+Arrows"), tr("Move by 10 pt"));
        rows.emplace_back(tr("Enter"), tr("Edit the object"));
        rows.emplace_back(tr("Esc"), tr("Finish · deselect"));
        if (registry.findAction(QString::fromLatin1(PDFAppEditContentActionIds::DELETE_SELECTION)))
        {
            rows.emplace_back(tr("Delete"), tr("Delete"));
        }
    }

    // The shortcuts of the actions (the scope Tool), then the letters of the tools and the keys of the actions of the
    // canvas (P of Preview)
    const std::vector<PDFAppEditContentKeyRow> keyRows = PDFAppEditContentKeyTable::getRows(registry);
    for (const PDFAppEditContentKeyRow& row : keyRows)
    {
        if (row.scope == PDFAppEditContentKeyScope::Tool && !row.actionId.isEmpty())
        {
            rows.emplace_back(row.sequence.toString(QKeySequence::NativeText), row.title);
        }
    }

    if (registry.findAction(QString::fromLatin1(PDFAppEditContentActionIds::FIND)))
    {
        // The command of the application (Ctrl+F), which the entry of Find answers
        rows.emplace_back(QKeySequence(QKeySequence::Find).toString(QKeySequence::NativeText), tr("Find and Replace"));
    }

    QStringList toolKeys;
    for (const PDFAppEditContentKeyRow& row : keyRows)
    {
        if (!row.toolId.isEmpty() && !row.sequence.isEmpty())
        {
            toolKeys << row.sequence.toString(QKeySequence::NativeText);
        }
    }
    if (!toolKeys.isEmpty())
    {
        rows.emplace_back(toolKeys.join(QLatin1Char(' ')), tr("Tools"));
    }

    for (const PDFAppEditContentKeyRow& row : keyRows)
    {
        if (row.scope == PDFAppEditContentKeyScope::Canvas && !row.actionId.isEmpty())
        {
            rows.emplace_back(row.sequence.toString(QKeySequence::NativeText), row.title);
        }
    }

    rows.emplace_back(QKeySequence(Qt::Key_F6).toString(QKeySequence::NativeText), tr("Next pane"));
    return rows;
}

std::vector<QWidget*> PDFAppEditContentKeyboard::getRegions() const
{
    std::vector<QWidget*> regions;
    const PDFAppToolGUIContent* gui = m_context ? qobject_cast<const PDFAppToolGUIContent*>(m_context->getGUI()) : nullptr;
    if (!gui)
    {
        return regions;
    }

    // The toolbar of the tool, the left panel, the canvas, the right panel (zadani_gui.md 4.23)
    QWidget* candidates[] = { gui->getToolbar(), gui->getLeftPanelWidget(), gui->getCanvas(), gui->getCardStack() };
    for (QWidget* region : candidates)
    {
        if (region)
        {
            regions.push_back(region);
        }
    }
    return regions;
}

void PDFAppEditContentKeyboard::retire()
{
    // The actions leave the key widget at once (the view of the document outlives the tool)
    uninstall();
}

void PDFAppEditContentKeyboard::onActionTriggered()
{
    const QAction* action = qobject_cast<const QAction*>(sender());
    if (action && m_context && !PDFDeferredDeletion::isRetired(this))
    {
        // The work runs from the event loop (the context queues it): nothing is touched after the call
        m_context->triggerAction(action->property(PDFAppEditContentKeyboardHelper::ACTION_ID_PROPERTY).toString());
    }
}

void PDFAppEditContentKeyboard::onNextRegion()
{
    moveToRegion(1);
}

void PDFAppEditContentKeyboard::onPreviousRegion()
{
    moveToRegion(-1);
}

void PDFAppEditContentKeyboard::moveToRegion(int step)
{
    const std::vector<QWidget*> regions = getRegions();
    if (regions.empty())
    {
        return;
    }

    // The region of the focus (none: the first one for F6, the last one for Shift+F6)
    const int count = int(regions.size());
    int current = step > 0 ? -1 : count;
    const QWidget* focus = QApplication::focusWidget();
    for (int i = 0; i < count; ++i)
    {
        if (focus && (focus == regions[i] || regions[i]->isAncestorOf(focus)))
        {
            current = i;
            break;
        }
    }

    // The next region, which can take the focus (a hidden or a disabled one is skipped); each region once
    for (int i = 1; i <= count; ++i)
    {
        const int index = ((current + step * i) % count + count) % count;
        if (PDFAppEditContentKeyboardHelper::focusRegion(regions[index]))
        {
            return;
        }
    }
}

// ---------------------------------------------------------------- PDFAppEditContentKeyboardCard

PDFAppEditContentKeyboardCard::PDFAppEditContentKeyboardCard(PDFAppEditContentContext* context, QWidget* parent) :
    BaseClass(context, QString::fromLatin1(PDFAppEditContentCardIds::KEYBOARD), parent)
{
    setObjectName(QStringLiteral("editContentKeyboardCard"));

    // edit-content-v2-keyboard: the section Keyboard with the link Close, the sentence, the rows of the keys
    PDFExpanderCard* section = addSection(tr("Keyboard"));

    QWidget* content = new QWidget(section);
    QVBoxLayout* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    QHBoxLayout* headLayout = new QHBoxLayout();
    headLayout->setContentsMargins(0, 0, 0, 0);
    PDFLabel* description = new PDFLabel(tr("Opened by Ctrl+/ or by View › Keyboard Shortcuts. Everything on the page can be reached without a mouse. "
                                            "The cards of the selection come back when this list is closed."), content);
    description->setObjectName(QStringLiteral("editContentKeyboardDescription"));
    description->setWordWrap(true);
    headLayout->addWidget(description, 1);
    PDFTextLink* close = new PDFTextLink(tr("Close"), content);
    close->setObjectName(QStringLiteral("editContentKeyboardClose"));
    connect(close, &QAbstractButton::clicked, this, &PDFAppEditContentKeyboardCard::onCloseClicked);
    headLayout->addWidget(close, 0, Qt::AlignTop);
    layout->addLayout(headLayout);

    PDFKeyValueList* list = new PDFKeyValueList(content);
    list->setObjectName(QStringLiteral("editContentKeyboardList"));
    layout->addWidget(list);
    m_list = list;

    if (section)
    {
        section->setContentWidget(content);
    }
    updateCard();
}

PDFAppEditContentKeyboardCard::~PDFAppEditContentKeyboardCard() = default;

void PDFAppEditContentKeyboardCard::updateCard()
{
    PDFKeyValueList* list = m_list.data();
    const PDFAppEditContentContext* context = getContext();
    if (!list || !context)
    {
        return;
    }

    list->clear();
    for (const std::pair<QString, QString>& row : PDFAppEditContentKeyboard::getKeyRows(*context->getRegistry()))
    {
        list->addRow(row.first, row.second);
    }
}

void PDFAppEditContentKeyboardCard::onCloseClicked()
{
    // The stack retires this card from the event loop (cardsChanged): the last call
    if (PDFAppEditContentContext* context = getContext())
    {
        context->setCardRequested(QString::fromLatin1(PDFAppEditContentCardIds::KEYBOARD), false);
    }
}

}   // namespace pdf

#include "moc_pdfappeditcontentkeyboard.cpp"
