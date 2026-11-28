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

#include "pdfappeditcontentregistry.h"

#include "pdfappcommand.h"
#include "pdfappeditcontentcards.h"
#include "pdfappeditcontentcards_appearance.h"
#include "pdfappeditcontentcards_geometry.h"
#include "pdfappeditcontentcontext.h"
#include "pdfappeditcontentitems.h"
#include "pdfappeditcontentkeyboard.h"
#include "pdfappeditcontentmenus.h"
#include "pdfappeditcontentpanels_objects.h"
#include "pdfappeditcontentpanels_pages.h"
#include "pdfappeditcontenttools.h"

#include <QtGlobal>

#include <algorithm>
#include <array>

namespace pdf
{

// ---------------------------------------------------------------- entries: defaults

PDFAppEditContentToolEntry::~PDFAppEditContentToolEntry() = default;

QString PDFAppEditContentToolEntry::getDescription() const
{
    return QString();
}

QString PDFAppEditContentToolEntry::getIconName() const
{
    return QString();
}

QKeySequence PDFAppEditContentToolEntry::getKey() const
{
    return QKeySequence();
}

bool PDFAppEditContentToolEntry::isEditorOf(const PDFContentNode& node, const PDFContentContainer& container) const
{
    Q_UNUSED(node);
    Q_UNUSED(container);
    return false;
}

PDFAppEditContentItemEntry::~PDFAppEditContentItemEntry() = default;

int PDFAppEditContentItemEntry::getPriority() const
{
    return 0;
}

PDFAppEditContentOverlayEntry::~PDFAppEditContentOverlayEntry() = default;

PDFAppEditContentCanvasWidgetEntry::~PDFAppEditContentCanvasWidgetEntry() = default;

PDFAppEditContentDropEntry::~PDFAppEditContentDropEntry() = default;

PDFAppEditContentTabEntry::~PDFAppEditContentTabEntry() = default;

QObject* PDFAppEditContentTabEntry::createState(PDFAppEditContentContext* context, QObject* parent) const
{
    Q_UNUSED(context);
    Q_UNUSED(parent);
    return nullptr;
}

PDFAppEditContentCardEntry::~PDFAppEditContentCardEntry() = default;

int PDFAppEditContentCardEntry::getOrder() const
{
    return PDFAppEditContentCardIds::getOrder(getId());
}

bool PDFAppEditContentCardEntry::isChangingModel() const
{
    return true;
}

PDFAppEditContentActionEntry::~PDFAppEditContentActionEntry() = default;

QString PDFAppEditContentActionEntry::getIconName() const
{
    return QString();
}

QString PDFAppEditContentActionEntry::getDescription(const PDFAppEditContentContext* context) const
{
    Q_UNUSED(context);
    return QString();
}

std::vector<PDFAppEditContentActionEntry::Key> PDFAppEditContentActionEntry::getKeys() const
{
    return std::vector<Key>();
}

std::vector<PDFAppEditContentActionEntry::Placement> PDFAppEditContentActionEntry::getPlacements() const
{
    return std::vector<Placement>();
}

QStringList PDFAppEditContentActionEntry::getApplicationCommandIds() const
{
    return QStringList();
}

bool PDFAppEditContentActionEntry::isVisible(const PDFAppEditContentContext* context) const
{
    Q_UNUSED(context);
    return true;
}

bool PDFAppEditContentActionEntry::isEnabled(const PDFAppEditContentContext* context) const
{
    return context && context->isEditingAllowed();
}

bool PDFAppEditContentActionEntry::isCheckable() const
{
    return false;
}

bool PDFAppEditContentActionEntry::isChecked(const PDFAppEditContentContext* context) const
{
    Q_UNUSED(context);
    return false;
}

std::vector<PDFAppEditContentActionChoice> PDFAppEditContentActionEntry::getChoices(const PDFAppEditContentContext* context) const
{
    Q_UNUSED(context);
    return std::vector<PDFAppEditContentActionChoice>();
}

void PDFAppEditContentActionEntry::triggerChoice(PDFAppEditContentContext* context, const QString& choiceId) const
{
    Q_UNUSED(context);
    Q_UNUSED(choiceId);
}

// ---------------------------------------------------------------- order of the cards

int PDFAppEditContentCardIds::getOrder(const QString& cardId)
{
    // zadani_gui.md 4.19: the table from the top. Selection is always the first, Arrange the last card of the
    // selection; the cards of a layer, of a tag and of the artifacts take the place of the cards of an object;
    // the cards shown without a selection or on request follow. The frames decide the place of two cards (stage
    // E1): Locked object, requested by a click on a locked object, is right below Selection (-locked-click),
    // Annotations and fields is between Selection and Arrange (-annotations-underlay, -annotations-hidden).
    static constexpr std::array<const char*, 24> ORDER = {
        SELECTION, LOCKED_OBJECT, ANNOTATIONS, GLYPH, NODE, CLIPPING, IMAGE, CROP, SHARED_CONTENT, SHADING, TILING_PATTERN, TEXT,
        FILL_AND_STROKE, TRANSPARENCY, PRINT_PRODUCTION, POSITION_AND_SIZE, LAYER_AND_TAG, ARRANGE, LAYER, TAG, ARTIFACT,
        GRID_AND_UNITS, PREVIEW, KEYBOARD
    };

    for (size_t i = 0; i < ORDER.size(); ++i)
    {
        if (cardId == QLatin1StringView(ORDER[i]))
        {
            return int(i + 1) * 100;
        }
    }
    return -1;
}

// ---------------------------------------------------------------- registry

/// Helpers of the registry
class PDFAppEditContentRegistryHelper
{
public:
    PDFAppEditContentRegistryHelper() = delete;

    /// Adds the entry to the list: an entry of the same identifier is replaced in its place, when \p isReplacing,
    /// and the new one is refused with a warning otherwise
    template<typename Entry>
    static bool add(std::vector<std::unique_ptr<const Entry>>* entries, std::unique_ptr<const Entry> entry, bool isReplacing, const char* kind)
    {
        if (!entry || entry->getId().isEmpty())
        {
            qWarning("PDFAppEditContentRegistry: an entry of the kind %s without an identifier was refused", kind);
            return false;
        }

        const QString id = entry->getId();
        for (std::unique_ptr<const Entry>& existing : *entries)
        {
            if (existing->getId() == id)
            {
                if (!isReplacing)
                {
                    qWarning("PDFAppEditContentRegistry: the %s '%s' is registered already", kind, qPrintable(id));
                    return false;
                }
                existing = std::move(entry);
                return true;
            }
        }

        entries->push_back(std::move(entry));
        return true;
    }

    template<typename Entry>
    static std::vector<const Entry*> getPointers(const std::vector<std::unique_ptr<const Entry>>& entries)
    {
        std::vector<const Entry*> result;
        result.reserve(entries.size());
        for (const std::unique_ptr<const Entry>& entry : entries)
        {
            result.push_back(entry.get());
        }
        return result;
    }

    template<typename Entry>
    static const Entry* find(const std::vector<std::unique_ptr<const Entry>>& entries, const QString& id)
    {
        for (const std::unique_ptr<const Entry>& entry : entries)
        {
            if (entry->getId() == id)
            {
                return entry.get();
            }
        }
        return nullptr;
    }

    static bool isToolBefore(const PDFAppEditContentToolEntry* left, const PDFAppEditContentToolEntry* right)
    {
        if (left->getPlace() != right->getPlace())
        {
            return int(left->getPlace()) < int(right->getPlace());
        }
        return left->getOrder() < right->getOrder();
    }

    static bool isTabBefore(const PDFAppEditContentTabEntry* left, const PDFAppEditContentTabEntry* right)
    {
        return left->getOrder() < right->getOrder();
    }

    static bool isCardBefore(const PDFAppEditContentCardEntry* left, const PDFAppEditContentCardEntry* right)
    {
        return left->getOrder() < right->getOrder();
    }

    static bool isItemBefore(const PDFAppEditContentItemEntry* left, const PDFAppEditContentItemEntry* right)
    {
        return left->getPriority() > right->getPriority();
    }

    /// The text of a key for a log
    static QString getKeyText(const QKeySequence& sequence)
    {
        return sequence.toString(QKeySequence::PortableText);
    }

    static QString getScopeText(PDFAppEditContentKeyScope scope)
    {
        return scope == PDFAppEditContentKeyScope::Canvas ? QStringLiteral("Canvas") : QStringLiteral("Tool");
    }

    static bool containsKey(const std::vector<QKeySequence>& keys, const QKeySequence& key)
    {
        return std::find(keys.cbegin(), keys.cend(), key) != keys.cend();
    }
};

/// Builds the registry of the application (PDFAppEditContentRegistry::getInstance)
class PDFAppEditContentRegistryInstance
{
public:
    PDFAppEditContentRegistryInstance() = delete;

    static std::unique_ptr<const PDFAppEditContentRegistry> create();
};

class PDFAppEditContentRegistry::Impl
{
public:
    std::vector<std::unique_ptr<const PDFAppEditContentToolEntry>> tools;
    std::vector<std::unique_ptr<const PDFAppEditContentItemEntry>> items;
    std::vector<std::unique_ptr<const PDFAppEditContentOverlayEntry>> overlays;
    std::vector<std::unique_ptr<const PDFAppEditContentCanvasWidgetEntry>> canvasWidgets;
    std::vector<std::unique_ptr<const PDFAppEditContentDropEntry>> drops;
    std::vector<std::unique_ptr<const PDFAppEditContentTabEntry>> tabs;
    std::vector<std::unique_ptr<const PDFAppEditContentCardEntry>> cards;
    std::vector<std::unique_ptr<const PDFAppEditContentActionEntry>> actions;

    /// The action of each application command (findActionByApplicationCommand): built again after
    /// every addAction - the entries are immutable, so their commands never change afterwards
    std::map<QString, const PDFAppEditContentActionEntry*> applicationCommands;

    void updateApplicationCommands()
    {
        applicationCommands.clear();
        for (const std::unique_ptr<const PDFAppEditContentActionEntry>& entry : actions)
        {
            for (const QString& commandId : entry->getApplicationCommandIds())
            {
                // The first registered entry wins (checkApplicationCommands reports the second one)
                applicationCommands.emplace(commandId, entry.get());
            }
        }
    }
};

PDFAppEditContentRegistry::PDFAppEditContentRegistry() :
    m_impl(std::make_unique<Impl>())
{

}

PDFAppEditContentRegistry::~PDFAppEditContentRegistry() = default;

const PDFAppEditContentRegistry& PDFAppEditContentRegistry::getInstance()
{
    // Built at the first call (GUI thread), never changed afterwards; no static object of another translation unit
    // depends on its construction
    static const std::unique_ptr<const PDFAppEditContentRegistry> instance = PDFAppEditContentRegistryInstance::create();
    return *instance;
}

bool PDFAppEditContentRegistry::addTool(std::unique_ptr<const PDFAppEditContentToolEntry> entry, bool isReplacing)
{
    return PDFAppEditContentRegistryHelper::add(&m_impl->tools, std::move(entry), isReplacing, "tool");
}

bool PDFAppEditContentRegistry::addItem(std::unique_ptr<const PDFAppEditContentItemEntry> entry, bool isReplacing)
{
    return PDFAppEditContentRegistryHelper::add(&m_impl->items, std::move(entry), isReplacing, "item");
}

bool PDFAppEditContentRegistry::addOverlay(std::unique_ptr<const PDFAppEditContentOverlayEntry> entry, bool isReplacing)
{
    return PDFAppEditContentRegistryHelper::add(&m_impl->overlays, std::move(entry), isReplacing, "overlay");
}

bool PDFAppEditContentRegistry::addCanvasWidget(std::unique_ptr<const PDFAppEditContentCanvasWidgetEntry> entry, bool isReplacing)
{
    return PDFAppEditContentRegistryHelper::add(&m_impl->canvasWidgets, std::move(entry), isReplacing, "canvas widget");
}

bool PDFAppEditContentRegistry::addDrop(std::unique_ptr<const PDFAppEditContentDropEntry> entry, bool isReplacing)
{
    return PDFAppEditContentRegistryHelper::add(&m_impl->drops, std::move(entry), isReplacing, "drop");
}

bool PDFAppEditContentRegistry::addTab(std::unique_ptr<const PDFAppEditContentTabEntry> entry, bool isReplacing)
{
    return PDFAppEditContentRegistryHelper::add(&m_impl->tabs, std::move(entry), isReplacing, "tab");
}

bool PDFAppEditContentRegistry::addCard(std::unique_ptr<const PDFAppEditContentCardEntry> entry, bool isReplacing)
{
    return PDFAppEditContentRegistryHelper::add(&m_impl->cards, std::move(entry), isReplacing, "card");
}

bool PDFAppEditContentRegistry::addAction(std::unique_ptr<const PDFAppEditContentActionEntry> entry, bool isReplacing)
{
    const bool isAdded = PDFAppEditContentRegistryHelper::add(&m_impl->actions, std::move(entry), isReplacing, "action");
    if (isAdded)
    {
        m_impl->updateApplicationCommands();
    }
    return isAdded;
}

std::vector<const PDFAppEditContentToolEntry*> PDFAppEditContentRegistry::getTools() const
{
    std::vector<const PDFAppEditContentToolEntry*> result = PDFAppEditContentRegistryHelper::getPointers(m_impl->tools);
    std::stable_sort(result.begin(), result.end(), &PDFAppEditContentRegistryHelper::isToolBefore);
    return result;
}

std::vector<const PDFAppEditContentToolEntry*> PDFAppEditContentRegistry::getTools(PDFAppEditContentToolEntry::Place place) const
{
    std::vector<const PDFAppEditContentToolEntry*> result;
    for (const PDFAppEditContentToolEntry* entry : getTools())
    {
        if (entry->getPlace() == place)
        {
            result.push_back(entry);
        }
    }
    return result;
}

const PDFAppEditContentToolEntry* PDFAppEditContentRegistry::findTool(const QString& id) const
{
    return PDFAppEditContentRegistryHelper::find(m_impl->tools, id);
}

const PDFAppEditContentToolEntry* PDFAppEditContentRegistry::findToolByKey(const QKeySequence& key) const
{
    if (key.isEmpty())
    {
        return nullptr;
    }
    for (const PDFAppEditContentToolEntry* entry : getTools())
    {
        if (entry->getKey() == key)
        {
            return entry;
        }
    }
    return nullptr;
}

const PDFAppEditContentToolEntry* PDFAppEditContentRegistry::findEditorTool(const PDFContentNode& node, const PDFContentContainer& container) const
{
    for (const PDFAppEditContentToolEntry* entry : getTools())
    {
        if (entry->isEditorOf(node, container))
        {
            return entry;
        }
    }
    return nullptr;
}

std::vector<const PDFAppEditContentItemEntry*> PDFAppEditContentRegistry::getItems() const
{
    std::vector<const PDFAppEditContentItemEntry*> result = PDFAppEditContentRegistryHelper::getPointers(m_impl->items);
    std::stable_sort(result.begin(), result.end(), &PDFAppEditContentRegistryHelper::isItemBefore);
    return result;
}

const PDFAppEditContentItemEntry* PDFAppEditContentRegistry::findItem(const PDFContentNode& node, const PDFContentContainer& container) const
{
    for (const PDFAppEditContentItemEntry* entry : getItems())
    {
        if (entry->isItemOf(node, container))
        {
            return entry;
        }
    }
    return nullptr;
}

std::vector<const PDFAppEditContentOverlayEntry*> PDFAppEditContentRegistry::getOverlays() const
{
    return PDFAppEditContentRegistryHelper::getPointers(m_impl->overlays);
}

std::vector<const PDFAppEditContentCanvasWidgetEntry*> PDFAppEditContentRegistry::getCanvasWidgets() const
{
    return PDFAppEditContentRegistryHelper::getPointers(m_impl->canvasWidgets);
}

std::vector<const PDFAppEditContentDropEntry*> PDFAppEditContentRegistry::getDrops() const
{
    return PDFAppEditContentRegistryHelper::getPointers(m_impl->drops);
}

std::vector<const PDFAppEditContentTabEntry*> PDFAppEditContentRegistry::getTabs() const
{
    std::vector<const PDFAppEditContentTabEntry*> result = PDFAppEditContentRegistryHelper::getPointers(m_impl->tabs);
    std::stable_sort(result.begin(), result.end(), &PDFAppEditContentRegistryHelper::isTabBefore);
    return result;
}

const PDFAppEditContentTabEntry* PDFAppEditContentRegistry::findTab(const QString& id) const
{
    return PDFAppEditContentRegistryHelper::find(m_impl->tabs, id);
}

std::vector<const PDFAppEditContentCardEntry*> PDFAppEditContentRegistry::getCards() const
{
    std::vector<const PDFAppEditContentCardEntry*> result = PDFAppEditContentRegistryHelper::getPointers(m_impl->cards);
    std::stable_sort(result.begin(), result.end(), &PDFAppEditContentRegistryHelper::isCardBefore);
    return result;
}

const PDFAppEditContentCardEntry* PDFAppEditContentRegistry::findCard(const QString& id) const
{
    return PDFAppEditContentRegistryHelper::find(m_impl->cards, id);
}

std::vector<const PDFAppEditContentActionEntry*> PDFAppEditContentRegistry::getActions() const
{
    return PDFAppEditContentRegistryHelper::getPointers(m_impl->actions);
}

std::vector<const PDFAppEditContentActionEntry*> PDFAppEditContentRegistry::getMenuActions(const QString& menuId) const
{
    // (group, order, registration) of the placement in the menu
    struct Row
    {
        int group = 0;
        int order = 0;
        const PDFAppEditContentActionEntry* entry = nullptr;
    };

    std::vector<Row> rows;
    for (const std::unique_ptr<const PDFAppEditContentActionEntry>& entry : m_impl->actions)
    {
        if (const std::optional<PDFAppEditContentActionEntry::Placement> placement = findPlacement(entry->getPlacements(), menuId))
        {
            rows.push_back(Row{ placement->group, placement->order, entry.get() });
        }
    }

    // Insertion sort, stable: the menus have tens of rows
    for (size_t i = 1; i < rows.size(); ++i)
    {
        size_t j = i;
        while (j > 0 && (rows[j - 1].group > rows[j].group || (rows[j - 1].group == rows[j].group && rows[j - 1].order > rows[j].order)))
        {
            std::swap(rows[j - 1], rows[j]);
            --j;
        }
    }

    std::vector<const PDFAppEditContentActionEntry*> result;
    result.reserve(rows.size());
    for (const Row& row : rows)
    {
        result.push_back(row.entry);
    }
    return result;
}

const PDFAppEditContentActionEntry* PDFAppEditContentRegistry::findAction(const QString& id) const
{
    return PDFAppEditContentRegistryHelper::find(m_impl->actions, id);
}

const PDFAppEditContentActionEntry* PDFAppEditContentRegistry::findActionByKey(const QKeySequence& key, PDFAppEditContentKeyScope scope) const
{
    if (key.isEmpty())
    {
        return nullptr;
    }
    for (const std::unique_ptr<const PDFAppEditContentActionEntry>& entry : m_impl->actions)
    {
        for (const PDFAppEditContentActionEntry::Key& entryKey : entry->getKeys())
        {
            if (entryKey.scope == scope && entryKey.sequence == key)
            {
                return entry.get();
            }
        }
    }
    return nullptr;
}

const PDFAppEditContentActionEntry* PDFAppEditContentRegistry::findActionByApplicationCommand(const QString& commandId) const
{
    const auto it = m_impl->applicationCommands.find(commandId);
    return it != m_impl->applicationCommands.cend() ? it->second : nullptr;
}

std::optional<PDFAppEditContentActionEntry::Placement> PDFAppEditContentRegistry::findPlacement(const std::vector<PDFAppEditContentActionEntry::Placement>& placements, const QString& menuId)
{
    for (const PDFAppEditContentActionEntry::Placement& placement : placements)
    {
        if (placement.menuId == menuId)
        {
            return placement;
        }
    }
    return std::nullopt;
}

// ---------------------------------------------------------------- key table

std::vector<PDFAppEditContentKeyRow> PDFAppEditContentKeyTable::getRows(const PDFAppEditContentRegistry& registry)
{
    std::vector<PDFAppEditContentKeyRow> rows;

    for (const PDFAppEditContentToolEntry* tool : registry.getTools())
    {
        const QKeySequence key = tool->getKey();
        if (!key.isEmpty())
        {
            PDFAppEditContentKeyRow row;
            row.sequence = key;
            row.scope = PDFAppEditContentKeyScope::Canvas;
            row.title = tool->getTitle();
            row.toolId = tool->getId();
            rows.push_back(std::move(row));
        }
    }

    for (const PDFAppEditContentActionEntry* action : registry.getActions())
    {
        for (const PDFAppEditContentActionEntry::Key& key : action->getKeys())
        {
            PDFAppEditContentKeyRow row;
            row.sequence = key.sequence;
            row.scope = key.scope;
            row.title = action->getTitle(nullptr);
            row.actionId = action->getId();
            rows.push_back(std::move(row));
        }
    }

    return rows;
}

std::vector<QKeySequence> PDFAppEditContentKeyTable::getReservedCanvasKeys()
{
    return { QKeySequence(Qt::Key_Left), QKeySequence(Qt::Key_Right), QKeySequence(Qt::Key_Up), QKeySequence(Qt::Key_Down),
             QKeySequence(Qt::SHIFT | Qt::Key_Left), QKeySequence(Qt::SHIFT | Qt::Key_Right), QKeySequence(Qt::SHIFT | Qt::Key_Up), QKeySequence(Qt::SHIFT | Qt::Key_Down),
             QKeySequence(Qt::Key_Return), QKeySequence(Qt::Key_Enter), QKeySequence(Qt::Key_Delete), QKeySequence(Qt::Key_Backspace),
             QKeySequence(Qt::Key_Tab), QKeySequence(Qt::SHIFT | Qt::Key_Tab), QKeySequence(Qt::Key_Backtab), QKeySequence(Qt::SHIFT | Qt::Key_Backtab),
             QKeySequence(Qt::Key_Escape), QKeySequence(Qt::Key_Space) };
}

std::vector<QKeySequence> PDFAppEditContentKeyTable::getShellKeys()
{
    // PDFDocumentToolShell::enter: toolEscape, toolConfirm, toolConfirmKeypad
    return { QKeySequence(Qt::Key_Escape), QKeySequence(Qt::CTRL | Qt::Key_Return), QKeySequence(Qt::CTRL | Qt::Key_Enter) };
}

QStringList PDFAppEditContentKeyTable::check(const std::vector<PDFAppEditContentKeyRow>& rows, const std::vector<QKeySequence>& applicationShortcuts)
{
    using H = PDFAppEditContentRegistryHelper;

    QStringList problems;
    const std::vector<QKeySequence> reservedCanvasKeys = getReservedCanvasKeys();
    const std::vector<QKeySequence> shellKeys = getShellKeys();

    for (size_t i = 0; i < rows.size(); ++i)
    {
        const PDFAppEditContentKeyRow& row = rows[i];
        const QString name = row.toolId.isEmpty() ? QStringLiteral("action '%1'").arg(row.actionId) : QStringLiteral("tool '%1'").arg(row.toolId);
        const QString keyText = H::getKeyText(row.sequence);

        if (row.sequence.isEmpty())
        {
            problems << QStringLiteral("%1 has an empty key").arg(name);
            continue;
        }
        if (row.sequence.count() != 1)
        {
            problems << QStringLiteral("%1: the key '%2' is a chord of more combinations").arg(name, keyText);
            continue;
        }

        const QKeyCombination combination = row.sequence[0];
        const Qt::KeyboardModifiers modifiers = combination.keyboardModifiers();
        const Qt::Key key = combination.key();
        if (row.scope == PDFAppEditContentKeyScope::Canvas)
        {
            if (modifiers & ~Qt::KeyboardModifiers(Qt::ShiftModifier))
            {
                problems << QStringLiteral("%1: the canvas key '%2' has a modifier other than Shift").arg(name, keyText);
            }
            if (H::containsKey(reservedCanvasKeys, row.sequence))
            {
                problems << QStringLiteral("%1: the canvas key '%2' belongs to the tools of the canvas").arg(name, keyText);
            }
        }
        else
        {
            const bool isFunctionKey = key >= Qt::Key_F1 && key <= Qt::Key_F35;
            if (!isFunctionKey && !(modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)))
            {
                problems << QStringLiteral("%1: the shortcut '%2' of the whole tool has neither Ctrl nor Alt").arg(name, keyText);
            }
        }

        if (H::containsKey(shellKeys, row.sequence))
        {
            problems << QStringLiteral("%1: the key '%2' is a key of the tool shell").arg(name, keyText);
        }
        if (H::containsKey(applicationShortcuts, row.sequence))
        {
            problems << QStringLiteral("%1: the key '%2' is a shortcut of a command of the application").arg(name, keyText);
        }

        for (size_t j = 0; j < i; ++j)
        {
            if (rows[j].scope == row.scope && rows[j].sequence == row.sequence)
            {
                problems << QStringLiteral("%1: the key '%2' is used twice in the scope %3").arg(name, keyText, H::getScopeText(row.scope));
                break;
            }
        }
    }

    return problems;
}

QStringList PDFAppEditContentKeyTable::getShellCommandIds()
{
    return { PDFAppCommands::undo(), PDFAppCommands::redo(), PDFAppCommands::save(), PDFAppCommands::saveAs() };
}

QStringList PDFAppEditContentKeyTable::checkApplicationCommands(const PDFAppEditContentRegistry& registry, const std::map<QString, std::vector<QKeySequence>>& commandShortcuts)
{
    using H = PDFAppEditContentRegistryHelper;

    QStringList problems;
    const QStringList shellCommandIds = getShellCommandIds();
    std::map<QString, QString> owners;     // command -> the first action, which answers it

    for (const PDFAppEditContentActionEntry* action : registry.getActions())
    {
        const QString actionId = action->getId();
        const std::vector<PDFAppEditContentActionEntry::Key> keys = action->getKeys();
        for (const QString& commandId : action->getApplicationCommandIds())
        {
            const auto shortcuts = commandShortcuts.find(commandId);
            if (shortcuts == commandShortcuts.cend())
            {
                problems << QStringLiteral("action '%1' answers the command '%2', which the application does not have").arg(actionId, commandId);
                continue;
            }
            if (shellCommandIds.contains(commandId))
            {
                problems << QStringLiteral("action '%1' answers the command '%2', which the shell of the tool answers").arg(actionId, commandId);
            }

            const auto [owner, isFirst] = owners.emplace(commandId, actionId);
            if (!isFirst)
            {
                problems << QStringLiteral("action '%1' answers the command '%2', which the action '%3' answers already").arg(actionId, commandId, owner->second);
            }

            // The key of the command comes from the application (Window / Document) or from the view of the tool
            // (Canvas): the entry never takes it as a key of its own
            for (const PDFAppEditContentActionEntry::Key& key : keys)
            {
                if (H::containsKey(shortcuts->second, key.sequence))
                {
                    problems << QStringLiteral("action '%1' takes the key '%2' of the command '%3', which it answers").arg(actionId, H::getKeyText(key.sequence), commandId);
                }
            }
        }
    }

    return problems;
}

QStringList PDFAppEditContentKeyTable::checkCanvasKeys(const PDFAppEditContentRegistry& registry, const std::map<QString, std::vector<QKeySequence>>& canvasCommandShortcuts)
{
    using H = PDFAppEditContentRegistryHelper;

    QStringList problems;
    for (const PDFAppEditContentKeyRow& row : getRows(registry))
    {
        if (row.scope != PDFAppEditContentKeyScope::Canvas || row.sequence.isEmpty())
        {
            continue;
        }

        const QString name = row.toolId.isEmpty() ? QStringLiteral("action '%1'").arg(row.actionId) : QStringLiteral("tool '%1'").arg(row.toolId);
        for (const auto& [commandId, shortcuts] : canvasCommandShortcuts)
        {
            const PDFAppEditContentActionEntry* owner = registry.findActionByApplicationCommand(commandId);
            if (owner && H::containsKey(shortcuts, row.sequence))
            {
                problems << QStringLiteral("%1: the canvas key '%2' is the Canvas shortcut of the command '%3', which the action '%4' answers")
                                .arg(name, H::getKeyText(row.sequence), commandId, owner->getId());
            }
        }
    }
    return problems;
}

// ---------------------------------------------------------------- built-in entries

std::unique_ptr<const PDFAppEditContentRegistry> PDFAppEditContentRegistryInstance::create()
{
    std::unique_ptr<PDFAppEditContentRegistry> registry = std::make_unique<PDFAppEditContentRegistry>();
    PDFAppEditContentBuiltins::registerAll(registry.get());
    return registry;
}

void PDFAppEditContentBuiltins::registerAll(PDFAppEditContentRegistry* registry)
{
    // One line per package file, in the order of the packages (the order of the registration is the order of
    // the entries of the same order value, of the same item priority and of the actions in the list of the keys).
    // A new file of a later stage: the coordinator adds its line here and its two lines to tools.cmake.

    // Stage E1, P11 - canvas
    PDFAppEditContentTools::registerParts(registry);            // pdfappeditcontenttools.cpp: the tool Select
    PDFAppEditContentItems::registerParts(registry);            // pdfappeditcontentitems.cpp: the items of stage E1

    // Stage E1, P12 - tabs Objects and Pages, menus
    PDFAppEditContentObjectsPanel::registerParts(registry);     // pdfappeditcontentpanels_objects.cpp: the tab Objects
    PDFAppEditContentPagesPanel::registerParts(registry);       // pdfappeditcontentpanels_pages.cpp: the tab Pages
    PDFAppEditContentMenus::registerParts(registry);            // pdfappeditcontentmenus.cpp: actions of the context menu and of View

    // Stage E1, P13 - cards
    PDFAppEditContentCardStack::registerParts(registry);        // pdfappeditcontentcards.cpp: Selection, Locked object, Annotations and fields, Preview
    PDFAppEditContentAppearanceCards::registerParts(registry);  // pdfappeditcontentcards_appearance.cpp: Fill and stroke, Transparency
    PDFAppEditContentGeometryCards::registerParts(registry);    // pdfappeditcontentcards_geometry.cpp: Position and size, Arrange

    // Stage E1, P14 - GUI of the tool, keys
    PDFAppEditContentKeyboard::registerParts(registry);         // pdfappeditcontentkeyboard.cpp: Preview, Keyboard Shortcuts, Clear Page, the card Keyboard

    // Stage E2: P22 pdfappeditcontenttextedit.cpp, pdfappeditcontentcards_text.cpp, pdfappeditcontenttools_text.cpp;
    //           P23 pdfappeditcontentfind.cpp, pdfappeditcontentclipboard.cpp
    // Stage E3: P31 pdfappeditcontenttools_shapes.cpp, _nodes.cpp, _clip.cpp, pdfappeditcontentcards_path.cpp;
    //           P32 pdfappeditcontentrulers.cpp, pdfappeditcontentgrid.cpp, pdfappeditcontentcards_grid.cpp;
    //           P33 pdfappeditcontenttools_image.cpp, pdfappeditcontentcards_image.cpp; P34 pdfappeditcontenttools_eyedropper.cpp
    // Stage E4: P41 pdfappeditcontentcards_color.cpp, _production.cpp; P42 pdfappeditcontentpanels_layers.cpp,
    //           pdfappeditcontentcards_layer.cpp; P43 pdfappeditcontentisolation.cpp, pdfappeditcontentcards_groups.cpp
    // Stage E5: P52 pdfappeditcontentpanels_structure.cpp, pdfappeditcontentcards_tag.cpp
}

}   // namespace pdf
