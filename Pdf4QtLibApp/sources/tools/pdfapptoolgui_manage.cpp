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

#include "pdfapptoolgui_manage.h"

#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentdialogs.h"
#include "pdfappdocumentpanels_common.h"
#include "pdfappdocumentregistry.h"
#include "pdfappdocumenttoolbar.h"
#include "pdfappfiledrop.h"
#include "pdfapplocale.h"
#include "pdfappmainwindow.h"
#include "pdfapppagelayout.h"
#include "pdfapproute.h"
#include "pdfapptoolhost.h"
#include "pdfapptoolinputrow.h"
#include "pdfapptoolmergemodel.h"
#include "pdfapptoolmergeview.h"
#include "pdfapptoolpanelbuilder.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptooltab.h"
#include "pdfapptooltoolbar.h"
#include "pdfdocumenttoolguiregistry.h"
#include "pdfrecentassemblyrepository.h"

#include "pdfcomponentbadges.h"
#include "pdfcomponentbuttons.h"
#include "pdfcomponentrows.h"
#include "pdfwidgetstyleproperties.h"
#include "pdfdocumentassemblyfile.h"
#include "pdfdocumenttool_manage.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfimagepagefactory.h"
#include "pdfoutlinebreakpoints.h"
#include "pdfstylepainter.h"
#include "pdfstylepainterguard.h"
#include "pdfthemefonts.h"

#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDir>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPushButton>
#include <QSaveFile>
#include <QTimer>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>

namespace pdf
{

/// Names, keys and texts of Manage Pages
class PDFAppManageToolGUIHelper
{
public:
    PDFAppManageToolGUIHelper() = delete;

    /// Property of a row, of a button and of an action: the name of its action
    static constexpr const char* ACTION_KEY = "pdf4qtManageAction";

    /// Key of a source row of an input, which is not a source yet
    static constexpr const char* INPUT_PREFIX = "input:";

    /// Most file names in the preview of Output files (the first and the last are shown)
    static constexpr int MAXIMUM_PREVIEW_NAMES = 2;

    /// Most recent assemblies in the menu (409:32023) and in the empty state (409:32739)
    static constexpr int MENU_RECENT_COUNT = 2;
    static constexpr int EMPTY_RECENT_COUNT = 3;

    static QString name(const char* parameter) { return QString::fromLatin1(parameter); }

    static int toInt(qint64 value)
    {
        return int(qBound(qint64(0), value, qint64(std::numeric_limits<int>::max())));
    }

    static QString pages(qint64 count)
    {
        const int value = toInt(count);
        return value == 1 ? PDFAppManageToolGUI::tr("1 page") : PDFAppManageToolGUI::tr("%Ln pages", nullptr, value);
    }

    static QString items(qint64 count)
    {
        const int value = toInt(count);
        return value == 1 ? PDFAppManageToolGUI::tr("1 item") : PDFAppManageToolGUI::tr("%Ln items", nullptr, value);
    }

    static QString files(qint64 count)
    {
        const int value = toInt(count);
        return value == 1 ? PDFAppManageToolGUI::tr("1 file") : PDFAppManageToolGUI::tr("%Ln files", nullptr, value);
    }

    /// The ids of the text of the parameter "inputs"
    static QStringList splitIds(const QString& text)
    {
        QStringList ids;
        for (const QString& id : text.split(QLatin1Char(','), Qt::SkipEmptyParts))
        {
            const QString trimmed = id.trimmed();
            if (!trimmed.isEmpty() && !ids.contains(trimmed))
            {
                ids << trimmed;
            }
        }
        return ids;
    }

    /// The pages of an assembly
    static qint64 getPageCount(const std::vector<PDFDocumentAssembly::Item>& items)
    {
        qint64 count = 0;
        for (const PDFDocumentAssembly::Item& item : items)
        {
            count += qint64(item.pages.size());
        }
        return count;
    }

    /// The item of the id, nullptr = none
    static const PDFDocumentAssembly::Item* findItem(const PDFDocumentAssembly& assembly, const QString& id)
    {
        for (const PDFDocumentAssembly::Item& item : assembly.getItems())
        {
            if (item.id == id)
            {
                return &item;
            }
        }
        return nullptr;
    }

    /// Index of the source of the id, -1 = none
    static int findSource(const PDFDocumentAssembly& assembly, const QString& id)
    {
        const std::vector<PDFDocumentAssembly::Source>& sources = assembly.getSources();
        for (size_t i = 0; i < sources.size(); ++i)
        {
            if (sources[i].id == id)
            {
                return int(i);
            }
        }
        return -1;
    }

    /// The pages of the target, which are not moved: the longest ascending run of their original
    /// numbers (n log n); the others are "moved" (1479:34632 Result › Moved)
    static int getMovedCount(const std::vector<PDFInteger>& targetPages)
    {
        std::vector<PDFInteger> tails;
        for (const PDFInteger page : targetPages)
        {
            auto it = std::lower_bound(tails.begin(), tails.end(), page);
            if (it == tails.end())
            {
                tails.push_back(page);
            }
            else
            {
                *it = page;
            }
        }
        return toInt(qint64(targetPages.size()) - qint64(tails.size()));
    }

    /// The keys of the actions (387:13288, 387:14220, 405:31588, 409:32023; KB8)
    static QKeySequence getKeys(const QString& action)
    {
        struct KeyItem
        {
            const char* action;
            QKeyCombination keys;
        };
        static const KeyItem keyItems[] = {
            { "assemblyOpen", QKeyCombination(Qt::CTRL | Qt::SHIFT, Qt::Key_O) },
            { "assemblySave", QKeyCombination(Qt::CTRL, Qt::Key_S) },
            { "assemblySaveAs", QKeyCombination(Qt::CTRL | Qt::SHIFT, Qt::Key_S) },
            { "addFiles", QKeyCombination(Qt::CTRL, Qt::Key_I) },
            { "addBlank", QKeyCombination(Qt::CTRL | Qt::SHIFT, Qt::Key_I) },
            { "selectAll", QKeyCombination(Qt::CTRL, Qt::Key_A) },
            { "selectNone", QKeyCombination(Qt::CTRL | Qt::SHIFT, Qt::Key_A) },
            { "group", QKeyCombination(Qt::CTRL, Qt::Key_G) },
            { "ungroup", QKeyCombination(Qt::CTRL | Qt::SHIFT, Qt::Key_G) },
            { "rotateLeft", QKeyCombination(Qt::SHIFT, Qt::Key_F4) },
            { "rotateRight", QKeyCombination(Qt::NoModifier, Qt::Key_F4) },
            { "remove", QKeyCombination(Qt::NoModifier, Qt::Key_Delete) },
            { "more", QKeyCombination(Qt::ALT, Qt::Key_M) },
            { "clone", QKeyCombination(Qt::CTRL, Qt::Key_L) },
            { "cut", QKeyCombination(Qt::CTRL, Qt::Key_X) },
            { "copy", QKeyCombination(Qt::CTRL, Qt::Key_C) },
            { "paste", QKeyCombination(Qt::CTRL, Qt::Key_V) },
            { "replace", QKeyCombination(Qt::CTRL | Qt::ALT, Qt::Key_R) },
            { "rename", QKeyCombination(Qt::NoModifier, Qt::Key_F2) },
            { "restoreAll", QKeyCombination(Qt::CTRL | Qt::SHIFT, Qt::Key_R) },
            { "tilesLarger", QKeyCombination(Qt::CTRL, Qt::Key_Plus) },
            { "tilesSmaller", QKeyCombination(Qt::CTRL, Qt::Key_Minus) },
            { "focusNextPart", QKeyCombination(Qt::NoModifier, Qt::Key_F6) },
            { "focusPreviousPart", QKeyCombination(Qt::SHIFT, Qt::Key_F6) },
        };
        for (const KeyItem& item : keyItems)
        {
            if (action == QLatin1String(item.action))
            {
                return QKeySequence(item.keys);
            }
        }
        return QKeySequence();
    }

    static QString getKeyText(const QString& action)
    {
        return getKeys(action).toString(QKeySequence::NativeText);
    }

    /// A row of a panel of the toolbar
    struct MenuRow
    {
        const char* action;
        QString title;
        QString description;
        QString iconName;
        QString tileText;
        bool isDividerBefore = false;
        QString captionBefore;
    };

    /// Adds the rows to the panel (the name of the action in ACTION_KEY, its key as the shortcut
    /// text); the caller connects them
    static std::vector<PDFTileRow*> addRows(PDFAppToolPanelPopup* panel, const std::vector<MenuRow>& rows)
    {
        std::vector<PDFTileRow*> result;
        for (const MenuRow& row : rows)
        {
            if (row.isDividerBefore)
            {
                panel->addGroupDivider();
            }
            if (!row.captionBefore.isEmpty())
            {
                panel->addGroupCaption(row.captionBefore);
            }
            PDFTileRow* tileRow = panel->addRow(row.title, row.description, row.iconName, PDFTileRow::Kind::MenuItem);
            const QString action = QString::fromLatin1(row.action);
            tileRow->setObjectName(QStringLiteral("manageRow-") + action);
            tileRow->setProperty(ACTION_KEY, action);
            if (!row.tileText.isEmpty())
            {
                tileRow->setTileText(row.tileText);
            }
            tileRow->setShortcutText(getKeyText(action));
            result.push_back(tileRow);
        }
        return result;
    }

    /// The selected items in the order of the assembly (one pass: 100 000 items, plan 6)
    static std::vector<const PDFDocumentAssembly::Item*> getSelectedItems(const PDFDocumentAssembly& assembly, const PDFDocumentAssembly::Selection& selection)
    {
        std::vector<const PDFDocumentAssembly::Item*> items;
        if (selection.empty())
        {
            return items;
        }
        const std::set<QString> ids(selection.cbegin(), selection.cend());
        for (const PDFDocumentAssembly::Item& item : assembly.getItems())
        {
            if (ids.count(item.id))
            {
                items.push_back(&item);
            }
        }
        return items;
    }

    /// The pages of the selected items
    static qint64 getSelectionPageCount(const PDFDocumentAssembly& assembly, const PDFDocumentAssembly::Selection& selection)
    {
        qint64 count = 0;
        for (const PDFDocumentAssembly::Item* item : getSelectedItems(assembly, selection))
        {
            count += qint64(item->pages.size());
        }
        return count;
    }

    /// The gap of the parts of a card of the empty state (409:32930: gap 10)
    static constexpr int EMPTY_CARD_GAP = 10;

    /// The padding above a caption of a card of the empty state (409:32941 "Open documents": pt 8)
    static constexpr int EMPTY_CAPTION_PADDING = 8;

    /// A card of the empty state (409:32739: 370 wide, the paddings 22 + the border, the icon tile
    /// 40, the title Semi Bold 15, the text Regular 12, the parts 10 apart; its buttons hug their
    /// texts at the leading edge) in the layout of the cards
    static QWidget* createEmptyCard(QWidget* state, QBoxLayout* cards, bool isDashed, const QString& iconName, const QString& title, const QString& text, const QString& objectName);

    /// "Rotate right (F4)" (405:31588: the name and the key of an icon button)
    static QString withKeys(const QString& text, const QString& action)
    {
        const QString keys = getKeyText(action);
        return keys.isEmpty() ? text : PDFAppManageToolGUI::tr("%1 (%2)").arg(text, keys);
    }

    /// The first widget of the part, which takes the focus by Tab (the part itself, else its first
    /// visible and enabled descendant in the focus chain), nullptr = none
    static QWidget* findFocusTarget(QWidget* part)
    {
        if (!part || !part->isVisible())
        {
            return nullptr;
        }
        if (isFocusTarget(part))
        {
            return part;
        }

        // The chain of the window is a cycle: one pass over it at most
        QWidget* widget = part->nextInFocusChain();
        const int limit = int(qMin<qsizetype>(part->window()->findChildren<QWidget*>().size() + 1, std::numeric_limits<int>::max()));
        for (int i = 0; widget && widget != part && i < limit; ++i, widget = widget->nextInFocusChain())
        {
            if (part->isAncestorOf(widget) && isFocusTarget(widget))
            {
                return widget;
            }
        }
        return nullptr;
    }

    static bool isFocusTarget(const QWidget* widget)
    {
        return widget->isVisible() && widget->isEnabled() && (widget->focusPolicy() & Qt::TabFocus);
    }

    /// The estimate of the size of the result in whole units, "11 MB", "93 kB" (387:13277; KB5:
    /// an estimate has no decimal), by the locale of the application (O17)
    static QString formatEstimatedSize(qint64 bytes)
    {
        return PDFAppLocale::formatDataSize(qMax(qint64(0), bytes), 0, QLocale::DataSizeTraditionalFormat);
    }

    /// The rotation of an item, "90°" (387:16947 Properties, the row Rotation of Selection
    /// 387:13234), the same text as the column Rotation of Details
    static QString getRotationText(PageRotation turn)
    {
        return PDFAppManageItemModel::getRotationText(turn);
    }
};

/// Registers the tool and its GUI under the id, when they are not registered yet
class PDFAppManageToolGUIRegistration
{
public:
    PDFAppManageToolGUIRegistration() = delete;

    static void registerTool()
    {
        const QString toolId = QString::fromLatin1(PDFDocumentToolManage::TOOL_ID);
        if (!PDFDocumentToolRegistry::isImplemented(toolId))
        {
            PDFDocumentToolRegistry::registerTool(std::make_shared<PDFDocumentToolManage>());
        }
        if (!PDFDocumentToolGUIRegistry::getFactory(toolId))
        {
            PDFDocumentToolGUIRegistry::registerFactory(toolId, std::make_shared<PDFDocumentToolGUIFactoryOf<PDFAppManageToolGUI>>());
        }
    }
};

/// The values of the tool follow a new version of the document of the tab (RB2): the assembly of
/// the current values and of every step of Undo and Redo is made for the version by its page
/// objects (PDFDocumentAssembly::mapToDocument)
class PDFAppManageDocumentMapper : public PDFAppToolValueMapper
{
public:
    explicit PDFAppManageDocumentMapper(PDFDocumentPointer document, quint64 version) :
        m_document(std::move(document)),
        m_version(version)
    {

    }

    virtual QVariantMap mapValues(const QVariantMap& values, bool isCurrent) const override
    {
        Q_UNUSED(isCurrent);
        const QString name = QString::fromLatin1(PDFDocumentToolManage::PARAMETER_ASSEMBLY);
        QVariantMap mapped = values;
        if (values.contains(name) && m_document)
        {
            const PDFDocumentAssembly assembly = values.value(name).value<PDFDocumentAssembly>();
            mapped.insert(name, QVariant::fromValue(PDFDocumentAssembly::mapToDocument(assembly, m_document.data(), m_version, nullptr)));
        }
        return mapped;
    }

private:
    PDFDocumentPointer m_document;
    quint64 m_version = 0;
};

/// The dashed card of the empty state (409:32739 "Start a new assembly": bg/surface, border/strong
/// dashed, radius 16; the card of Merge 378:1827)
class PDFAppManageEmptyCard : public QWidget
{
public:
    explicit PDFAppManageEmptyCard(bool isDashed, QWidget* parent) :
        QWidget(parent),
        m_isDashed(isDashed)
    {

    }

    static constexpr int RADIUS = 16;

protected:
    virtual void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        const PDFStylePainterGuard guard(&painter);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF shape = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        PDFStylePainter::fill(&painter, shape, PDFStylePainter::Radii(RADIUS), PDFThemeColors::BgSurface);
        QPen pen(PDFStylePainter::color(m_isDashed ? PDFThemeColors::BorderStrong : PDFThemeColors::BorderDefault), 1.0);
        if (m_isDashed)
        {
            pen.setDashPattern({ 4.0, 3.0 });
        }
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(PDFStylePainter::roundedPath(shape, PDFStylePainter::Radii(RADIUS)));
    }

private:
    bool m_isDashed = false;
};

QWidget* PDFAppManageToolGUIHelper::createEmptyCard(QWidget* state, QBoxLayout* cards, bool isDashed, const QString& iconName, const QString& title, const QString& text, const QString& objectName)
{
    PDFAppManageEmptyCard* card = new PDFAppManageEmptyCard(isDashed, state);
    card->setObjectName(objectName);
    card->setFixedWidth(370);
    QVBoxLayout* layout = new QVBoxLayout(card);
    layout->setContentsMargins(23, 23, 23, 21);
    layout->setSpacing(EMPTY_CARD_GAP);
    PDFIconBadge* icon = new PDFIconBadge(iconName, 40, 16, card);
    icon->setRadius(8);
    icon->setColors(PDFThemeColors::BgAccentSubtle, PDFThemeColors::IconAccent);
    layout->addWidget(icon, 0, Qt::AlignLeading);
    layout->addWidget(PDFAppPageWidgets::createLabel(title, QFont::DemiBold, 15, PDFThemeColors::TextPrimary, card));
    PDFAppParagraph* paragraph = PDFAppPageWidgets::createText(text, PDFThemeFonts::getFont(QFont::Normal, 12), 15, PDFThemeColors::TextSecondary, card);
    paragraph->setMinimumWidth(1);
    layout->addWidget(paragraph);
    cards->addWidget(card, 0, Qt::AlignTop);
    return card;
}

// ------------------------------------------------------------- PDFAppManageToolGUI

PDFAppManageToolGUI::PDFAppManageToolGUI(QObject* parent) :
    BaseClass(parent)
{
    // The output files of One PDF per group / per page (396:26763): a section of this GUI,
    // described into each right panel
    m_outputFiles = new PDFAppToolOutputFilesSection(this);
}

PDFAppManageToolGUI::~PDFAppManageToolGUI()
{
    // The key widget gets back what it had: its drops and no actions of the tool
    if (m_keyWidget)
    {
        m_keyWidget->removeEventFilter(this);
        m_keyWidget->setAcceptDrops(m_wasDropAccepted);
    }
    for (auto& [name, action] : m_keyActions)
    {
        Q_UNUSED(name);
        delete action.data();
    }
    if (m_passwordDialog)
    {
        m_passwordDialog->disconnect(this);
        m_passwordDialog->close();
    }
    if (m_dialog)
    {
        m_dialog->disconnect(this);
        m_dialog->close();
    }
    if (m_contextMenu)
    {
        m_contextMenu->disconnect(this);
        m_contextMenu->close();
    }
    if (m_sourcesDialog)
    {
        m_sourcesDialog->disconnect(this);
        m_sourcesDialog->close();
    }
}

bool PDFAppManageToolGUI::isDocumentHost() const
{
    const PDFAppToolHost* host = getHost();
    return host && !host->isOwnTab();
}

PDFAppToolInputRules PDFAppManageToolGUI::getInputRules() const
{
    PDFAppToolInputRules rules;
    rules.minimum = 1;
    rules.maximum = PDFDocumentToolCatalog::MAXIMUM_INPUT_COUNT;
    return rules;
}

bool PDFAppManageToolGUI::isTabKeptAfterResult() const
{
    return !isDocumentHost();
}

QString PDFAppManageToolGUI::getCancelText() const
{
    return isDocumentHost() ? QString() : tr("Close");
}

bool PDFAppManageToolGUI::handlesPermissionRefusal() const
{
    // Owner answer 1 of 12. 11. (1479:35631): in the tab of a document without Assemble the tool
    // opens - Rearrange Pages Disabled, the callout and the owner password (isConfirmEnabled,
    // updateRightPanel); the own tab has no document of the tab to refuse
    return isDocumentHost();
}

void PDFAppToolGUIManage::registerManageTools()
{
    PDFAppManageToolGUIRegistration::registerTool();
}

// ------------------------------------------------------------- values and the list

void PDFAppManageToolGUI::connectParts()
{
    // The list: the tab's in the own tab, an own one in the tab of a document (as Insert)
    PDFAppToolInputList* list = isDocumentHost() ? nullptr : getInputList();
    if (isDocumentHost() && !m_ownList && getHost())
    {
        m_ownList = new PDFAppToolInputList(this);
        m_ownList->setObjectName(QStringLiteral("manageSourceList"));
    }
    if (isDocumentHost())
    {
        list = m_ownList;
    }
    if (list && list != m_list)
    {
        if (m_list)
        {
            disconnect(m_list.data(), nullptr, this, nullptr);
        }
        m_list = list;
        m_list->setAcceptedInputs(PDFDocumentToolCatalog::MAXIMUM_INPUT_COUNT, true, true, QString());
        connect(m_list.data(), &PDFAppToolInputList::inputsChanged, this, &PDFAppManageToolGUI::onListInputsChanged);
        connect(m_list.data(), &PDFAppToolInputList::inputChanged, this, &PDFAppManageToolGUI::onListInputChanged);
        connect(m_list.data(), &QObject::destroyed, this, &PDFAppManageToolGUI::onListDestroyed);
    }

    if (!m_progressTimer)
    {
        m_progressTimer = new QTimer(this);
        m_progressTimer->setInterval(PROGRESS_INTERVAL);
        connect(m_progressTimer.data(), &QTimer::timeout, this, &PDFAppManageToolGUI::onProgressTimer);
    }
    if (!m_isShellConnected && getShell())
    {
        m_isShellConnected = true;
        connect(getShell(), &PDFDocumentToolShell::stateChanged, this, &PDFAppManageToolGUI::onShellStateChanged);
    }
    if (!m_isRegistryConnected && getContext() && getContext()->getDocumentRegistry())
    {
        m_isRegistryConnected = true;
        PDFAppDocumentRegistry* registry = getContext()->getDocumentRegistry();
        connect(registry, &PDFAppDocumentRegistry::documentAdded, this, &PDFAppManageToolGUI::onRegistryChanged);
        connect(registry, &PDFAppDocumentRegistry::documentChanged, this, &PDFAppManageToolGUI::onRegistryChanged);
        connect(registry, &PDFAppDocumentRegistry::documentRemoved, this, &PDFAppManageToolGUI::onRegistryChanged);
    }
    if (!m_loader && !isDocumentHost())
    {
        m_loader = new PDFAppAssemblyLoader(this);
        connect(m_loader.data(), &PDFAppAssemblyLoader::loaded, this, &PDFAppManageToolGUI::onLoaderLoaded);
    }
    initializeValues();
}

void PDFAppManageToolGUI::initializeValues()
{
    using Helper = PDFAppManageToolGUIHelper;

    PDFAppToolParameterModel* model = getModel();
    if (m_isValuesInitialized || !model || !getHost())
    {
        return;
    }
    m_isValuesInitialized = true;

    // K21: the values, which the tool came with, are no change of the user (no undo step)
    const QString output = getParameter(PDFDocumentToolManage::PARAMETER_OUTPUT);
    if (isDocumentHost())
    {
        // QB7 b: only This document; the document is the source A (createForDocument, RB2)
        if (output != QLatin1String(PDFDocumentToolManage::OUTPUT_DOCUMENT))
        {
            model->setFollowingValue(Helper::name(PDFDocumentToolManage::PARAMETER_OUTPUT), Helper::name(PDFDocumentToolManage::OUTPUT_DOCUMENT));
        }
        PDFAppDocument* document = getDocument();
        if (document && document->getDocument())
        {
            // A document over the pages of an assembly (owner 2026-11-12) gets none: the status
            // says why (getPageLimitText), the tool is not editable; updateDocumentMapping makes
            // it, when the document fits later
            m_documentVersion = document->getVersion();
            if (getPageLimitText().isEmpty())
            {
                const PDFDocumentAssembly assembly = PDFDocumentAssembly::createForDocument(document->getDocument().data(), m_documentVersion, document->getTitle());
                model->setFollowingValue(Helper::name(PDFDocumentToolManage::PARAMETER_ASSEMBLY), QVariant::fromValue(assembly));
            }
        }
    }
    else
    {
        // The own tab never changes its sources: One PDF is its default output (387:12569)
        if (output.isEmpty() || output == QLatin1String(PDFDocumentToolManage::OUTPUT_DOCUMENT))
        {
            model->setFollowingValue(Helper::name(PDFDocumentToolManage::PARAMETER_OUTPUT), Helper::name(PDFDocumentToolManage::OUTPUT_ONE_PDF));
        }

        // An assembly file of the window (a double click, the command line, PDFAppMainWindow::openAssembly)
        const QString requestedFile = PDFAppAssemblyLoader::takeRequestedFile(getInputList());
        if (!requestedFile.isEmpty() && m_loader)
        {
            m_loader->load(requestedFile);
        }

        // The inputs of the start page become the sources, when they are read
        for (int i = 0; m_list && i < m_list->getCount(); ++i)
        {
            PendingAddition pending;
            pending.inputId = m_list->getInput(i)->id;
            pending.kind = PendingAddition::Kind::AllPages;
            pending.isInitial = true;
            m_pending.push_back(pending);
        }
    }
    m_savedAssembly = model->getValue(Helper::name(PDFDocumentToolManage::PARAMETER_ASSEMBLY)).value<PDFDocumentAssembly>();
    readValues();
    queueProcessPending();
}

QStringList PDFAppManageToolGUI::getModelInputIds() const
{
    return PDFAppManageToolGUIHelper::splitIds(getParameter(PDFDocumentToolManage::PARAMETER_INPUTS));
}

QString PDFAppManageToolGUI::getParameter(const char* name) const
{
    const PDFAppToolParameterModel* model = getModel();
    return model ? model->getValue(PDFAppManageToolGUIHelper::name(name)).toString() : QString();
}

void PDFAppManageToolGUI::setParameter(const char* name, const QString& value)
{
    PDFAppToolParameterModel* model = getModel();
    if (model && isEditable())
    {
        const QString error = model->setValue(PDFAppManageToolGUIHelper::name(name), value);
        if (!error.isEmpty())
        {
            showMessage(error);
        }
    }
}

void PDFAppManageToolGUI::readValues()
{
    using Helper = PDFAppManageToolGUIHelper;

    const PDFAppToolParameterModel* model = getModel();
    if (!model)
    {
        return;
    }
    m_assembly = model->getValue(Helper::name(PDFDocumentToolManage::PARAMETER_ASSEMBLY)).value<PDFDocumentAssembly>();
    followFileState();

    // The list follows the inputs of the values (Undo / Redo park and take back the inputs)
    QPointer<PDFAppManageToolGUI> guard(this);
    syncList();
    if (!guard)
    {
        return;
    }

    if (m_canvas)
    {
        m_canvas->setContent(m_assembly, createSourceViews(), isDocumentHost(), m_newTargetPages);
        if (!guard || !m_canvas)
        {
            return;
        }
        if (m_hasPendingSelection)
        {
            m_canvas->setSelection(m_pendingSelection);
            if (!guard)
            {
                return;
            }
        }
    }
    m_hasPendingSelection = false;
    m_pendingSelection.clear();
    updateAll();
}

void PDFAppManageToolGUI::followFileState()
{
    // The file, the checkpoints and the saved state belong to the instance of the assembly: an
    // Undo of Open Assembly brings back the assembly before it, which has its own file (or none) -
    // Save Assembly never writes it into the file opened after it; Redo brings the opened file back
    const QString& instanceId = m_assembly.getInstanceId();
    if (m_fileInstanceId.isEmpty())
    {
        m_fileInstanceId = instanceId;
        return;
    }
    if (instanceId == m_fileInstanceId)
    {
        return;
    }

    m_otherFileStates[m_fileInstanceId] = FileState{ m_assemblyPath, m_savedAssembly, m_checkpoints, m_savedCheckpointCount };
    const auto it = m_otherFileStates.find(instanceId);
    if (it != m_otherFileStates.end())
    {
        m_assemblyPath = it->second.path;
        m_savedAssembly = it->second.savedAssembly;
        m_checkpoints = it->second.checkpoints;
        m_savedCheckpointCount = it->second.savedCheckpointCount;
        m_otherFileStates.erase(it);
    }
    else
    {
        // A new instance (a file being opened sets its state after the value)
        m_assemblyPath.clear();
        m_savedAssembly = PDFDocumentAssembly();
        m_checkpoints.clear();
        m_savedCheckpointCount = 0;
    }
    m_fileInstanceId = instanceId;

    // The states of the instances, which no step of the history has any more, are dropped
    std::set<QString> historyInstances;
    if (const PDFAppToolParameterModel* model = getModel())
    {
        for (const QVariant& value : model->getHistoryValues(PDFAppManageToolGUIHelper::name(PDFDocumentToolManage::PARAMETER_ASSEMBLY)))
        {
            historyInstances.insert(value.value<PDFDocumentAssembly>().getInstanceId());
        }
    }
    for (auto state = m_otherFileStates.begin(); state != m_otherFileStates.end();)
    {
        state = historyInstances.count(state->first) ? std::next(state) : m_otherFileStates.erase(state);
    }
}

void PDFAppManageToolGUI::syncList()
{
    using Helper = PDFAppManageToolGUIHelper;

    if (!m_list || !getModel())
    {
        return;
    }

    // The inputs of the sources of the values and the inputs, which wait to become sources
    QStringList ids = getModelInputIds();
    for (const PendingAddition& pending : m_pending)
    {
        if (!ids.contains(pending.inputId))
        {
            ids << pending.inputId;
        }
    }
    QPointer<PDFAppManageToolGUI> guard(this);
    if (ids != m_list->getInputIds())
    {
        m_isChangingList = true;
        m_list->setInputIds(ids);
        if (!guard)
        {
            return;
        }
        m_isChangingList = false;
    }

    // The parked inputs, which no step of the history names, are freed (R12 of step 2)
    QStringList kept = ids;
    for (const QVariant& value : getModel()->getHistoryValues(Helper::name(PDFDocumentToolManage::PARAMETER_INPUTS)))
    {
        kept << Helper::splitIds(value.toString());
    }
    if (m_list)
    {
        m_list->dropParkedInputs(kept);
    }
}

void PDFAppManageToolGUI::applyAssembly(const PDFDocumentAssembly& assembly, const PDFDocumentAssembly::Selection& selection, const QString& stepText, const QString& errorMessage)
{
    using Helper = PDFAppManageToolGUIHelper;

    QPointer<PDFAppManageToolGUI> guard(this);
    if (!errorMessage.isEmpty())
    {
        // A refused operation of the core says its reason; the value is the given one
        showMessage(errorMessage);
        if (!guard)
        {
            return;
        }
    }

    PDFAppToolParameterModel* model = getModel();
    if (!model || !isEditable())
    {
        return;
    }
    if (assembly == m_assembly)
    {
        // Nothing changed: the selection of the operation only (Select, a refusal keeps it)
        if (errorMessage.isEmpty() && m_canvas)
        {
            m_canvas->setSelection(selection);
            if (guard)
            {
                updateAll();
            }
        }
        return;
    }

    // One undo step of both values with its text (RB3 d); the model announces it (readValues),
    // the selection comes with it
    QStringList inputIds;
    for (const PDFDocumentAssembly::Source& source : assembly.getSources())
    {
        const auto it = m_sourceInputs.find(source.id);
        if (it != m_sourceInputs.cend() && !inputIds.contains(it->second))
        {
            inputIds << it->second;
        }
    }
    QVariantMap values;
    values.insert(Helper::name(PDFDocumentToolManage::PARAMETER_ASSEMBLY), QVariant::fromValue(assembly));
    values.insert(Helper::name(PDFDocumentToolManage::PARAMETER_INPUTS), inputIds.join(QLatin1Char(',')));
    m_pendingSelection = selection;
    m_hasPendingSelection = true;
    m_statusNote.clear();
    const QString error = model->setValues(values, stepText);
    if (!guard)
    {
        return;
    }
    if (!error.isEmpty())
    {
        m_hasPendingSelection = false;
        showMessage(error);
    }
}

void PDFAppManageToolGUI::applyEdit(const PDFDocumentAssembly& assembly, const PDFDocumentAssembly::Edit& edit, const QString& stepText)
{
    applyAssembly(assembly, edit.selection, stepText, edit.errorMessage);
}

// ------------------------------------------------------------- the sources

QString PDFAppManageToolGUI::getInputOfSource(const QString& sourceId) const
{
    const auto it = m_sourceInputs.find(sourceId);
    return it != m_sourceInputs.cend() ? it->second : QString();
}

QString PDFAppManageToolGUI::findSourceOfInput(const QString& inputId) const
{
    for (const auto& [sourceId, input] : m_sourceInputs)
    {
        if (input == inputId && PDFAppManageToolGUIHelper::findSource(m_assembly, sourceId) >= 0)
        {
            return sourceId;
        }
    }
    return QString();
}

QString PDFAppManageToolGUI::findSourceOfFile(const QString& filePath) const
{
    // B52: the same file is the same source (the canonical path)
    const QString canonical = QFileInfo(filePath).canonicalFilePath();
    for (const PDFDocumentAssembly::Source& source : m_assembly.getSources())
    {
        if (!source.isParked && !source.isTarget && !source.path.isEmpty() && QFileInfo(source.path).canonicalFilePath() == canonical && !canonical.isEmpty())
        {
            return source.id;
        }
    }
    return QString();
}

PDFDocumentAssembly::Source PDFAppManageToolGUI::createSource(const QString& inputId) const
{
    PDFDocumentAssembly::Source source;
    const PDFAppToolInput* input = m_list ? m_list->findInput(inputId) : nullptr;
    if (!input)
    {
        return source;
    }

    // Plan 4.1, 4.3: the pages and their sizes by the core (createDocumentSource, createImageSource);
    // the size and the time of the file here, the hash when the assembly is saved
    if (input->isImage())
    {
        QString errorMessage;
        source = PDFDocumentAssembly::createImageSource(input->imageData, input->imageFormat, input->name, input->filePath, &errorMessage);
    }
    else
    {
        source = PDFDocumentAssembly::createDocumentSource(input->document.data(), input->name, input->filePath);
    }
    source.size = input->fileSize > 0 ? input->fileSize : -1;
    if (!input->filePath.isEmpty())
    {
        source.modified = QFileInfo(input->filePath).lastModified().toUTC();
    }
    return source;
}

void PDFAppManageToolGUI::addResults(const std::vector<std::pair<QString, int>>& added, PendingAddition::Kind kind, const QStringList& refusedTexts)
{
    // \p added: the ids of the new inputs (empty with the refusal of the list) and their refusal
    QStringList refused = refusedTexts;
    const PDFDocumentAssembly::Selection selection = getSelection();
    for (const auto& [inputId, refusal] : added)
    {
        if (inputId.isEmpty())
        {
            continue;
        }
        PendingAddition pending;
        pending.inputId = inputId;
        pending.kind = kind;
        pending.selection = selection;
        m_pending.push_back(pending);
    }

    QPointer<PDFAppManageToolGUI> guard(this);
    queueProcessPending();
    updateAll();
    if (guard && !refused.isEmpty())
    {
        showMessage(refused.join(QStringLiteral(" · ")));
    }
}

void PDFAppManageToolGUI::queueProcessPending()
{
    // The readings of many files finish together: one pass after them (plan 6, 1 000 files)
    if (!m_isPendingQueued)
    {
        m_isPendingQueued = true;
        QMetaObject::invokeMethod(this, &PDFAppManageToolGUI::onProcessPending, Qt::QueuedConnection);
    }
}

void PDFAppManageToolGUI::onProcessPending()
{
    m_isPendingQueued = false;
    if (!m_list || !getModel())
    {
        return;
    }

    // The inputs read now become sources: all their pages after the selection (one undo step of
    // all of them; the inputs of the start page without a step, K21), the pages of a file of Pages
    // of a PDF… and Replace selection… are chosen in the dialog, a source of an assembly file only
    // gets its input
    PDFDocumentAssembly assembly = m_assembly;
    PDFDocumentAssembly::Selection selection;
    bool hasChange = false;
    bool isInitialOnly = true;
    bool isBound = false;
    QStringList refused;
    QString dialogInput;
    PendingAddition::Kind dialogKind = PendingAddition::Kind::ChoosePages;
    std::vector<PendingAddition> waiting;

    // The files of an addition keep the order, in which they were given (387:12569: the sources
    // A, B, C and their pages in that order), not the order, in which their readings finish: a
    // file waits while a file before it is read, and a file, which waits, follows the pages added
    // before it (not the selection of the request). A file, which needs a password, does not
    // hold the others (it comes when the password is given).
    bool isEarlierRead = false;
    std::vector<std::pair<PDFDocumentAssembly::Selection, PDFDocumentAssembly::Selection>> followedSelections;
    for (const PendingAddition& pending : m_pending)
    {
        const PDFAppToolInput* input = m_list->findInput(pending.inputId);
        if (!input || m_list->isParked(pending.inputId))
        {
            continue;
        }
        if (pending.kind == PendingAddition::Kind::AllPages && isEarlierRead)
        {
            waiting.push_back(pending);
            continue;
        }
        if (input->state == PDFAppToolInputState::Loading || input->state == PDFAppToolInputState::PasswordRequired || input->state == PDFAppToolInputState::Restricted)
        {
            // It waits for its reading or for its password (the row of Sources has the link)
            waiting.push_back(pending);
            isEarlierRead = isEarlierRead || (pending.kind == PendingAddition::Kind::AllPages && input->state == PDFAppToolInputState::Loading);
            continue;
        }
        if (!input->isUsable())
        {
            // Damaged, Unsupported, no pages: the reason, the input stays in Sources with Remove file
            refused << tr("%1 – %2").arg(input->name, input->reason.isEmpty() ? PDFAppToolInputList::getStateText(input->state) : input->reason);
            waiting.push_back(pending);
            continue;
        }

        if (pending.kind == PendingAddition::Kind::Bind)
        {
            // A source of an assembly file: its input, and its pages and sizes of the file read now
            // (updateSource: the pages over a smaller page count go to the bin, 387:16980)
            m_sourceInputs[pending.sourceId] = pending.inputId;
            const int index = PDFAppManageToolGUIHelper::findSource(assembly, pending.sourceId);
            if (index >= 0)
            {
                PDFDocumentAssembly::Source updated = createSource(pending.inputId);
                const PDFDocumentAssembly::Source& old = assembly.getSources()[size_t(index)];
                updated.id = old.id;
                updated.name = old.name;
                updated.relativePath = old.relativePath;
                updated.sha256 = old.sha256;
                PDFDocumentAssembly::Edit edit;
                assembly = PDFDocumentAssembly::updateSource(assembly, std::move(updated), &edit);
                if (!edit.errorMessage.isEmpty())
                {
                    // The file read now does not fit the assembly (more pages than an assembly
                    // has): the source keeps its pages of the assembly file, the user is told
                    refused << edit.errorMessage;
                }
            }
            isBound = true;
            continue;
        }
        if (pending.kind == PendingAddition::Kind::ChoosePages || pending.kind == PendingAddition::Kind::ReplacePages)
        {
            dialogInput = pending.inputId;
            dialogKind = pending.kind;
            continue;
        }

        QString sourceId;
        QString errorMessage;
        PDFDocumentAssembly::Source source = createSource(pending.inputId);
        const PDFInteger pageCount = source.pageCount;
        PDFDocumentAssembly withSource = PDFDocumentAssembly::addSource(assembly, std::move(source), &sourceId, &errorMessage);
        if (sourceId.isEmpty())
        {
            refused << (errorMessage.isEmpty() ? input->name : errorMessage);
            continue;
        }
        m_sourceInputs[sourceId] = pending.inputId;
        std::vector<PDFInteger> pages;
        pages.reserve(size_t(qMax<PDFInteger>(0, pageCount)));
        for (PDFInteger page = 0; page < pageCount; ++page)
        {
            pages.push_back(page);
        }
        PDFDocumentAssembly::Edit edit;
        const PDFDocumentAssembly::Selection after = hasChange ? selection : pending.selection;
        assembly = PDFDocumentAssembly::insertPages(withSource, after, sourceId, pages, &edit);
        if (!edit.errorMessage.isEmpty())
        {
            refused << edit.errorMessage;
            assembly = withSource;
            continue;
        }
        followedSelections.emplace_back(pending.selection, edit.selection);
        selection = edit.selection;
        hasChange = true;
        isInitialOnly = isInitialOnly && pending.isInitial;
    }
    for (PendingAddition& pending : waiting)
    {
        // The files after the added ones follow their pages (the last added file of the request)
        const PDFDocumentAssembly::Selection requestedSelection = pending.selection;
        for (const auto& [requested, added] : followedSelections)
        {
            if (pending.kind == PendingAddition::Kind::AllPages && requestedSelection == requested)
            {
                pending.selection = added;
            }
        }
    }
    m_pending = waiting;

    QPointer<PDFAppManageToolGUI> guard(this);
    if (hasChange)
    {
        if (isInitialOnly)
        {
            // K21: no undo step for the inputs, which the tab came with
            using Helper = PDFAppManageToolGUIHelper;
            QStringList inputIds;
            for (const PDFDocumentAssembly::Source& source : assembly.getSources())
            {
                const auto it = m_sourceInputs.find(source.id);
                if (it != m_sourceInputs.cend())
                {
                    inputIds << it->second;
                }
            }
            m_pendingSelection = PDFDocumentAssembly::Selection();
            m_hasPendingSelection = true;
            getModel()->setFollowingValue(Helper::name(PDFDocumentToolManage::PARAMETER_INPUTS), inputIds.join(QLatin1Char(',')));
            if (guard && getModel())
            {
                getModel()->setFollowingValue(Helper::name(PDFDocumentToolManage::PARAMETER_ASSEMBLY), QVariant::fromValue(assembly));
            }
        }
        else
        {
            applyAssembly(assembly, selection, tr("Add files"), QString());
        }
        if (!guard)
        {
            return;
        }
    }
    else if (isBound)
    {
        // No undo step: the sources of the read file only get what their files say (the saved
        // state follows - the file was not changed by it)
        const bool wasSaved = m_assembly.isEqual(m_savedAssembly);
        if (!(assembly == m_assembly))
        {
            if (wasSaved)
            {
                m_savedAssembly = assembly;
            }
            getModel()->setFollowingValue(PDFAppManageToolGUIHelper::name(PDFDocumentToolManage::PARAMETER_ASSEMBLY), QVariant::fromValue(assembly));
        }
        else
        {
            readValues();
        }
        if (!guard)
        {
            return;
        }
    }
    else
    {
        updateAll();
        if (!guard)
        {
            return;
        }
    }

    if (!dialogInput.isEmpty() && m_list)
    {
        // Pages of a PDF… / Replace selection…: the file is read, its pages are chosen
        const PDFAppToolInput* input = m_list->findInput(dialogInput);
        if (input)
        {
            openAddPagesDialog(QString::fromLatin1(PDFAppManageToolGUIHelper::INPUT_PREFIX) + dialogInput, input->name, input->pageCount, dialogKind == PendingAddition::Kind::ReplacePages);
            if (!guard)
            {
                return;
            }
        }
    }
    if (!refused.isEmpty())
    {
        showMessage(refused.join(QStringLiteral(" · ")));
    }
}

std::vector<PDFAppManageSourceView> PDFAppManageToolGUI::createSourceViews() const
{
    // The canvas gets values: the document of each source (the snapshot of the input, the
    // document of the tab for the target), the bytes of an image, the key of its pictures
    std::vector<PDFAppManageSourceView> views;
    const std::vector<PDFDocumentAssembly::Source>& sources = m_assembly.getSources();
    views.reserve(sources.size());
    int shownIndex = 0;
    for (size_t i = 0; i < sources.size(); ++i)
    {
        const PDFDocumentAssembly::Source& source = sources[i];
        PDFAppManageSourceView view;
        const PDFAppToolInput* input = m_list ? m_list->findInput(getInputOfSource(source.id)) : nullptr;
        view.letter = PDFAppToolInputList::getLetter(shownIndex);
        view.hue = PDFAppToolInputList::getHue(shownIndex, input);
        if (!source.isParked)
        {
            ++shownIndex;
        }
        if (source.isTarget)
        {
            const PDFAppDocument* document = getDocument();
            view.document = document ? document->getDocument() : PDFDocumentPointer();
            view.hue = PDFAppToolInputList::getHue(int(i));
            view.pictureKey = QStringLiteral("manage-document/%1/%2").arg(document ? document->getId() : QString()).arg(document ? document->getVersion() : 0);
        }
        else if (source.kind == PDFDocumentAssembly::SourceKind::Pdf)
        {
            view.document = input && input->isUsable() ? input->document : PDFDocumentPointer();
            view.pictureKey = QStringLiteral("manage-input/%1/%2/%3").arg(quintptr(m_list.data())).arg(input ? input->id : source.id).arg(input ? input->contentGeneration : 0);
        }
        else
        {
            view.imageData = !source.imageData.isEmpty() ? source.imageData : (input ? input->imageData : QByteArray());
            if (input && input->isUsable())
            {
                view.imagePageSize = PDFImagePageFactory::getPageSize(input->imageSize, input->imageDpiX, input->imageDpiY);
            }
            else if (!view.imageData.isEmpty())
            {
                // An image of an assembly file without its input: the size of its first frame
                QBuffer buffer;
                buffer.setData(view.imageData);
                QImageReader reader(&buffer, source.imageFormat);
                const QSize size = reader.size();
                if (size.isValid())
                {
                    view.imagePageSize = PDFImagePageFactory::getPageSize(size, 0.0, 0.0);
                }
            }
            view.pictureKey = QStringLiteral("manage-image/%1/%2").arg(m_assembly.getInstanceId(), source.id);
        }
        views.push_back(std::move(view));
    }
    return views;
}

QString PDFAppManageToolGUI::getSourceDisplayName(int sourceIndex) const
{
    const std::vector<PDFDocumentAssembly::Source>& sources = m_assembly.getSources();
    if (sourceIndex < 0 || size_t(sourceIndex) >= sources.size())
    {
        return QString();
    }
    const PDFDocumentAssembly::Source& source = sources[size_t(sourceIndex)];

    // Show document titles (387:14220): the title of the document instead of the file name
    if (m_areDocumentTitlesShown && source.kind == PDFDocumentAssembly::SourceKind::Pdf)
    {
        PDFDocumentPointer document;
        if (source.isTarget && getDocument())
        {
            document = getDocument()->getDocument();
        }
        else if (const PDFAppToolInput* input = m_list ? m_list->findInput(getInputOfSource(source.id)) : nullptr)
        {
            document = input->document;
        }
        const QString title = document ? document->getInfo()->title.trimmed() : QString();
        if (!title.isEmpty())
        {
            return title;
        }
    }
    return source.name;
}

void PDFAppManageToolGUI::addFiles(const QStringList& filePaths)
{
    if (!m_list || !isEditable() || filePaths.isEmpty())
    {
        return;
    }

    // B52: a file, which is a source already, adds its pages again (no second reading)
    QStringList newFiles;
    PDFDocumentAssembly assembly = m_assembly;
    PDFDocumentAssembly::Selection selection = getSelection();
    bool hasExisting = false;
    QString errorMessage;
    for (const QString& filePath : filePaths)
    {
        const QString sourceId = findSourceOfFile(filePath);
        const int index = PDFAppManageToolGUIHelper::findSource(assembly, sourceId);
        if (index < 0)
        {
            newFiles << filePath;
            continue;
        }
        std::vector<PDFInteger> pages;
        for (PDFInteger page = 0; page < assembly.getSources()[size_t(index)].pageCount; ++page)
        {
            pages.push_back(page);
        }
        PDFDocumentAssembly::Edit edit;
        assembly = PDFDocumentAssembly::insertPages(assembly, selection, sourceId, pages, &edit);
        errorMessage = edit.errorMessage;
        selection = edit.selection;
        hasExisting = hasExisting || edit.errorMessage.isEmpty();
    }

    QPointer<PDFAppManageToolGUI> guard(this);
    if (hasExisting)
    {
        applyAssembly(assembly, selection, tr("Add files"), errorMessage);
        if (!guard)
        {
            return;
        }
    }
    if (newFiles.isEmpty() || !m_list)
    {
        return;
    }

    m_isChangingList = true;
    const std::vector<PDFAppToolInputList::AddResult> results = m_list->addFiles(newFiles);
    if (!guard)
    {
        return;
    }
    m_isChangingList = false;
    std::vector<std::pair<QString, int>> added;
    QStringList refused;
    for (const PDFAppToolInputList::AddResult& result : results)
    {
        if (result.id.isEmpty())
        {
            refused << PDFAppToolInputList::getRefusalText(result.refusal, result.name);
        }
        added.emplace_back(result.id, int(result.refusal));
    }
    addResults(added, PendingAddition::Kind::AllPages, refused);
}

void PDFAppManageToolGUI::addFilesFromDialog()
{
    if (!isEditable())
    {
        return;
    }

    // The native dialog runs its own event loop: the GUI may go meanwhile
    QPointer<PDFAppManageToolGUI> guard(this);
    const QStringList files = PDFAppFileDialogs::getOpenInputFileNames(getWindow(), QString());
    if (guard && !files.isEmpty())
    {
        addFiles(files);
    }
}

void PDFAppManageToolGUI::addPagesFromDialog()
{
    if (!isEditable())
    {
        return;
    }
    QPointer<PDFAppManageToolGUI> guard(this);
    const QString file = PDFAppPanelFileDialogs::getOpenFileName(getWindow(), tr("Pages of a PDF"), QString(), tr("PDF documents (*.pdf)"));
    if (guard && !file.isEmpty())
    {
        addPagesOfFile(file);
    }
}

void PDFAppManageToolGUI::addPagesOfFile(const QString& filePath)
{
    if (!m_list || !isEditable())
    {
        return;
    }

    // A source already: its pages at once; else it is read first (a pending addition)
    const QString sourceId = findSourceOfFile(filePath);
    const int index = PDFAppManageToolGUIHelper::findSource(m_assembly, sourceId);
    if (index >= 0)
    {
        const PDFDocumentAssembly::Source& source = m_assembly.getSources()[size_t(index)];
        openAddPagesDialog(sourceId, source.name, source.pageCount, false);
        return;
    }

    QPointer<PDFAppManageToolGUI> guard(this);
    m_isChangingList = true;
    const PDFAppToolInputList::AddResult result = m_list->addFile(filePath);
    if (!guard)
    {
        return;
    }
    m_isChangingList = false;
    addResults({ { result.id, int(result.refusal) } }, PendingAddition::Kind::ChoosePages, result.id.isEmpty() ? QStringList(PDFAppToolInputList::getRefusalText(result.refusal, result.name)) : QStringList());
}

void PDFAppManageToolGUI::openAddPagesDialog(const QString& sourceKey, const QString& fileName, PDFInteger pageCount, bool isReplace)
{
    if (m_dialog)
    {
        m_dialog->raise();
        return;
    }
    PDFAppManageAddPagesDialog* dialog = new PDFAppManageAddPagesDialog(sourceKey, fileName, pageCount, isReplace, getScrimTarget(), getWindow());

    // 387:16763: the previews of the pages of the input of the source (a source, or an input read
    // now: "input:<id>")
    const char* inputPrefix = PDFAppManageToolGUIHelper::INPUT_PREFIX;
    const QString inputId = sourceKey.startsWith(QLatin1String(inputPrefix)) ? sourceKey.mid(int(qstrlen(inputPrefix))) : getInputOfSource(sourceKey);
    dialog->setPreviewSource(m_list.data(), inputId);
    connect(dialog, &PDFAppManageAddPagesDialog::pagesChosen, this, &PDFAppManageToolGUI::onPagesChosen);
    showDialog(dialog);
}

void PDFAppManageToolGUI::onPagesChosen(const QString& sourceKey, const std::vector<PDFInteger>& pages, bool isReplace)
{
    using Helper = PDFAppManageToolGUIHelper;

    if (!isEditable() || pages.empty())
    {
        return;
    }

    // The source of an input read now is added in the same step as its pages
    PDFDocumentAssembly assembly = m_assembly;
    QString sourceId = sourceKey;
    if (sourceKey.startsWith(QLatin1String(Helper::INPUT_PREFIX)))
    {
        const QString inputId = sourceKey.mid(int(qstrlen(Helper::INPUT_PREFIX)));
        sourceId = findSourceOfInput(inputId);
        if (sourceId.isEmpty())
        {
            QString errorMessage;
            assembly = PDFDocumentAssembly::addSource(assembly, createSource(inputId), &sourceId, &errorMessage);
            if (sourceId.isEmpty())
            {
                showMessage(errorMessage);
                return;
            }
            m_sourceInputs[sourceId] = inputId;
        }
    }

    PDFDocumentAssembly::Edit edit;
    if (isReplace)
    {
        // B20, B53: one operation - a refused one changes nothing
        const PDFDocumentAssembly result = PDFDocumentAssembly::replace(assembly, getSelection(), sourceId, pages, &edit);
        applyAssembly(edit.errorMessage.isEmpty() ? result : m_assembly, edit.selection, tr("Replace"), edit.errorMessage);
        return;
    }
    const PDFDocumentAssembly result = PDFDocumentAssembly::insertPages(assembly, getSelection(), sourceId, pages, &edit);
    applyAssembly(edit.errorMessage.isEmpty() ? result : m_assembly, edit.selection, tr("Add pages"), edit.errorMessage);
}

void PDFAppManageToolGUI::insertBlankPage()
{
    if (!isEditable())
    {
        return;
    }
    PDFDocumentAssembly::Edit edit;
    const PDFDocumentAssembly result = PDFDocumentAssembly::insertBlank(m_assembly, getSelection(), &edit);
    applyEdit(result, edit, tr("Blank page"));
}

void PDFAppManageToolGUI::pasteImage()
{
    if (!m_list || !isEditable())
    {
        return;
    }

    const QClipboard* clipboard = QGuiApplication::clipboard();
    const QImage image = clipboard ? clipboard->image() : QImage();
    if (image.isNull())
    {
        showMessage(tr("The clipboard has no image"));
        return;
    }

    QPointer<PDFAppManageToolGUI> guard(this);
    m_isChangingList = true;
    const PDFAppToolInputList::AddResult result = m_list->addImage(image, QString());
    if (!guard)
    {
        return;
    }
    m_isChangingList = false;
    addResults({ { result.id, int(result.refusal) } }, PendingAddition::Kind::AllPages, result.id.isEmpty() ? QStringList(PDFAppToolInputList::getRefusalText(result.refusal, result.name)) : QStringList());
}

void PDFAppManageToolGUI::addOpenDocument(const QString& documentId)
{
    const PDFAppDocumentRegistry* registry = getContext() ? getContext()->getDocumentRegistry() : nullptr;
    PDFAppDocument* document = registry ? registry->findDocument(documentId) : nullptr;
    if (!m_list || !document || !isEditable())
    {
        return;
    }

    // R11: a snapshot of now, the tab is not followed
    QPointer<PDFAppManageToolGUI> guard(this);
    m_isChangingList = true;
    const PDFAppToolInputList::AddResult result = m_list->addOpenDocument(document);
    if (!guard)
    {
        return;
    }
    m_isChangingList = false;
    addResults({ { result.id, int(result.refusal) } }, PendingAddition::Kind::AllPages, result.id.isEmpty() ? QStringList(PDFAppToolInputList::getRefusalText(result.refusal, result.name)) : QStringList());
}

bool PDFAppManageToolGUI::dropFiles(const QMimeData* mimeData)
{
    if (!PDFAppFileDrop::hasLocalFiles(mimeData) || !isEditable())
    {
        return false;
    }

    // R18: PDF files and images, in the dropped order; the refused items are said
    const PDFAppFileDrop::Result result = PDFAppFileDrop::getFiles(mimeData, PDFAppFileDrop::MAXIMUM_FILES, true);
    QStringList refused;
    for (const PDFAppFileDrop::RefusedItem& item : result.refused)
    {
        refused << tr("%1 – %2").arg(item.text, PDFAppFileDrop::getRefusalText(item.refusal));
    }
    QPointer<PDFAppManageToolGUI> guard(this);
    addFiles(result.files);
    if (guard && !refused.isEmpty())
    {
        showMessage(refused.join(QStringLiteral(" · ")));
    }
    return true;
}

void PDFAppManageToolGUI::openPasswordDialog(const QString& inputId)
{
    if (!m_list)
    {
        return;
    }
    if (m_passwordDialog)
    {
        m_passwordDialog->raise();
        return;
    }

    // A popup (new + QPointer, deleted when it is closed); Remove File asks this GUI
    m_passwordDialog = PDFAppPasswordDialog::openDialog(m_list, inputId, getWindow());
    if (m_passwordDialog)
    {
        connect(m_passwordDialog.data(), &PDFAppPasswordDialog::removeRequested, this, &PDFAppManageToolGUI::onPasswordRemoveRequested);
    }
}

void PDFAppManageToolGUI::onPasswordRemoveRequested(const QString& inputId)
{
    // An input, which is no source yet, is removed from the list (no page of it is in the assembly)
    std::vector<PendingAddition> pending;
    for (const PendingAddition& item : m_pending)
    {
        if (item.inputId != inputId)
        {
            pending.push_back(item);
        }
    }
    m_pending = std::move(pending);
    if (m_list && findSourceOfInput(inputId).isEmpty())
    {
        QPointer<PDFAppManageToolGUI> guard(this);
        m_isChangingList = true;
        m_list->removeInput(inputId);
        if (!guard)
        {
            return;
        }
        m_isChangingList = false;
    }
    updateAll();
}

// ------------------------------------------------------------- the selection

const PDFDocumentAssembly::Selection& PDFAppManageToolGUI::getSelection() const
{
    static const PDFDocumentAssembly::Selection empty;
    return m_canvas ? m_canvas->getSelection() : empty;
}

void PDFAppManageToolGUI::setSelection(const PDFDocumentAssembly::Selection& selection)
{
    if (m_canvas)
    {
        QPointer<PDFAppManageToolGUI> guard(this);
        m_canvas->setSelection(selection);
        if (guard)
        {
            updateAll();
        }
    }
}

void PDFAppManageToolGUI::select(PDFDocumentAssembly::SelectRule rule, const PDFDocumentAssembly::SelectSettings& settings)
{
    // Select is no change of the assembly (no undo step)
    setSelection(PDFDocumentAssembly::select(m_assembly, getSelection(), rule, settings));
}

void PDFAppManageToolGUI::selectAll()
{
    select(PDFDocumentAssembly::SelectRule::All, PDFDocumentAssembly::SelectSettings());
}

void PDFAppManageToolGUI::selectNone()
{
    select(PDFDocumentAssembly::SelectRule::None, PDFDocumentAssembly::SelectSettings());
}

void PDFAppManageToolGUI::selectInvert()
{
    select(PDFDocumentAssembly::SelectRule::Invert, PDFDocumentAssembly::SelectSettings());
}

void PDFAppManageToolGUI::selectOdd()
{
    select(PDFDocumentAssembly::SelectRule::Odd, PDFDocumentAssembly::SelectSettings());
}

void PDFAppManageToolGUI::selectEven()
{
    select(PDFDocumentAssembly::SelectRule::Even, PDFDocumentAssembly::SelectSettings());
}

void PDFAppManageToolGUI::selectPortrait()
{
    select(PDFDocumentAssembly::SelectRule::Portrait, PDFDocumentAssembly::SelectSettings());
}

void PDFAppManageToolGUI::selectLandscape()
{
    select(PDFDocumentAssembly::SelectRule::Landscape, PDFDocumentAssembly::SelectSettings());
}

void PDFAppManageToolGUI::selectSearchResults()
{
    PDFDocumentAssembly::SelectSettings settings;
    settings.searchText = m_canvas ? m_canvas->getSearchText() : QString();
    select(PDFDocumentAssembly::SelectRule::SearchResults, settings);
}

void PDFAppManageToolGUI::openRangeDialog()
{
    if (m_dialog || !isEditable())
    {
        return;
    }

    // Original pages of …: the PDF source of the first selected page
    QString sourceId;
    for (const QString& id : getSelection())
    {
        const PDFDocumentAssembly::Item* item = PDFAppManageToolGUIHelper::findItem(m_assembly, id);
        if (item && !item->pages.empty() && item->pages.front().plan.input >= 0 && size_t(item->pages.front().plan.input) < m_assembly.getSources().size())
        {
            const PDFDocumentAssembly::Source& source = m_assembly.getSources()[size_t(item->pages.front().plan.input)];
            if (source.kind == PDFDocumentAssembly::SourceKind::Pdf)
            {
                sourceId = source.id;
                break;
            }
        }
    }
    PDFAppManageRangeDialog* dialog = new PDFAppManageRangeDialog(m_assembly, getSelection(), sourceId, getScrimTarget(), getWindow());
    connect(dialog, &PDFAppManageRangeDialog::selectRequested, this, &PDFAppManageToolGUI::onRangeChosen);
    showDialog(dialog);
}

void PDFAppManageToolGUI::onRangeChosen(const PDFDocumentAssembly::SelectSettings& settings)
{
    select(PDFDocumentAssembly::SelectRule::PageRange, settings);
}

// ------------------------------------------------------------- operations

void PDFAppManageToolGUI::group()
{
    PDFDocumentAssembly::Edit edit;
    const PDFDocumentAssembly result = PDFDocumentAssembly::group(m_assembly, getSelection(), &edit);
    applyEdit(result, edit, tr("Group"));
}

void PDFAppManageToolGUI::ungroup()
{
    PDFDocumentAssembly::Edit edit;
    const PDFDocumentAssembly result = PDFDocumentAssembly::ungroup(m_assembly, getSelection(), &edit);
    applyEdit(result, edit, tr("Ungroup"));
}

void PDFAppManageToolGUI::regroup(PDFDocumentAssembly::RegroupMode mode, const PDFDocumentAssembly::RegroupSettings& settings)
{
    PDFDocumentAssembly::Edit edit;
    const PDFDocumentAssembly result = PDFDocumentAssembly::regroup(m_assembly, getSelection(), mode, settings, &edit);
    applyEdit(result, edit, tr("Regroup"));
}

int PDFAppManageToolGUI::getSelectedPageCount() const
{
    // Regroup and Sort work with the selection, with less than two items with everything (387:13288)
    const PDFDocumentAssembly::Selection& selection = getSelection();
    if (selection.size() < 2)
    {
        return PDFAppManageToolGUIHelper::toInt(PDFAppManageToolGUIHelper::getPageCount(m_assembly.getItems()));
    }
    return PDFAppManageToolGUIHelper::toInt(PDFAppManageToolGUIHelper::getSelectionPageCount(m_assembly, selection));
}

void PDFAppManageToolGUI::openEveryDialog()
{
    if (m_dialog || !isEditable())
    {
        return;
    }
    PDFAppManageNumberDialog* dialog = new PDFAppManageNumberDialog(PDFAppManageNumberDialog::Mode::EveryPages, getSelectedPageCount(), 6, getScrimTarget(), getWindow());
    connect(dialog, &PDFAppManageNumberDialog::numberChosen, this, &PDFAppManageToolGUI::onEveryChosen);
    showDialog(dialog);
}

void PDFAppManageToolGUI::onEveryChosen(int value)
{
    PDFDocumentAssembly::RegroupSettings settings;
    settings.pageCount = value;
    regroup(PDFDocumentAssembly::RegroupMode::EveryPages, settings);
}

void PDFAppManageToolGUI::openNumbersDialog()
{
    if (m_dialog || !isEditable())
    {
        return;
    }
    PDFAppManageTextDialog* dialog = new PDFAppManageTextDialog(PDFAppManageTextDialog::Mode::PageNumbers, tr("Regroup at page numbers"), tr("Regroup"), QString(), getSelectedPageCount(), getScrimTarget(), getWindow());
    connect(dialog, &PDFAppManageTextDialog::textChosen, this, &PDFAppManageToolGUI::onNumbersChosen);
    showDialog(dialog);
}

void PDFAppManageToolGUI::onNumbersChosen(const QString& text)
{
    QString errorMessage;
    PDFDocumentAssembly::RegroupSettings settings;
    settings.positions = PDFAppManageDialogs::parsePositions(text, getSelectedPageCount(), &errorMessage);
    if (!errorMessage.isEmpty())
    {
        showMessage(errorMessage);
        return;
    }
    regroup(PDFDocumentAssembly::RegroupMode::AtPageNumbers, settings);
}

void PDFAppManageToolGUI::openBookmarksDialog()
{
    if (m_dialog || !isEditable())
    {
        return;
    }

    // One PDF source in the selection (all items with less than two selected): its bookmarks and
    // the pages of it in the selection
    const PDFDocumentAssembly::Selection& selection = getSelection();
    const std::set<QString> selected(selection.cbegin(), selection.cend());
    int input = -2;
    std::vector<PDFInteger> pages;
    for (const PDFDocumentAssembly::Item& item : m_assembly.getItems())
    {
        if (selection.size() >= 2 && !selected.count(item.id))
        {
            continue;
        }
        for (const PDFDocumentAssembly::Page& page : item.pages)
        {
            input = input == -2 ? page.plan.input : (input == page.plan.input ? input : -1);
            pages.push_back(page.plan.pageIndex);
        }
    }
    const std::vector<PDFAppManageSourceView> views = createSourceViews();
    if (input < 0 || size_t(input) >= views.size() || !views[size_t(input)].document)
    {
        showMessage(tr("By bookmarks needs the pages of one PDF file"));
        return;
    }
    std::vector<PDFOutlineBreakPoints::Bookmark> bookmarks = PDFOutlineBreakPoints::readBookmarks(views[size_t(input)].document.data());
    if (bookmarks.empty())
    {
        showMessage(tr("%1 has no bookmarks").arg(m_assembly.getSources()[size_t(input)].name));
        return;
    }
    PDFAppManageBookmarksDialog* dialog = new PDFAppManageBookmarksDialog(m_assembly.getSources()[size_t(input)].name, std::move(bookmarks), std::move(pages), getScrimTarget(), getWindow());
    connect(dialog, &PDFAppManageBookmarksDialog::regroupRequested, this, &PDFAppManageToolGUI::onBookmarksChosen);
    showDialog(dialog);
}

void PDFAppManageToolGUI::onBookmarksChosen(const PDFDocumentAssembly::RegroupSettings& settings)
{
    regroup(PDFDocumentAssembly::RegroupMode::ByBookmarks, settings);
}

void PDFAppManageToolGUI::sort(PDFDocumentAssembly::SortKey key)
{
    PDFDocumentAssembly::Edit edit;
    const PDFDocumentAssembly result = PDFDocumentAssembly::sort(m_assembly, getSelection(), key, &edit);
    applyEdit(result, edit, tr("Sort"));
}

void PDFAppManageToolGUI::rotateLeft()
{
    PDFDocumentAssembly::Edit edit;
    const PDFDocumentAssembly result = PDFDocumentAssembly::rotate(m_assembly, getSelection(), PageRotation::Rotate270, &edit);
    applyEdit(result, edit, tr("Rotate left"));
}

void PDFAppManageToolGUI::rotateRight()
{
    PDFDocumentAssembly::Edit edit;
    const PDFDocumentAssembly result = PDFDocumentAssembly::rotate(m_assembly, getSelection(), PageRotation::Rotate90, &edit);
    applyEdit(result, edit, tr("Rotate right"));
}

void PDFAppManageToolGUI::resetRotation()
{
    PDFDocumentAssembly::Edit edit;
    const PDFDocumentAssembly result = PDFDocumentAssembly::resetRotation(m_assembly, getSelection(), &edit);
    applyEdit(result, edit, tr("Reset rotation"));
}

void PDFAppManageToolGUI::cloneSelection()
{
    PDFDocumentAssembly::Edit edit;
    const PDFDocumentAssembly result = PDFDocumentAssembly::clone(m_assembly, getSelection(), &edit);
    applyEdit(result, edit, tr("Clone"));
}

void PDFAppManageToolGUI::removeSelection()
{
    PDFDocumentAssembly::Edit edit;
    const PDFDocumentAssembly result = PDFDocumentAssembly::remove(m_assembly, getSelection(), &edit);
    applyEdit(result, edit, tr("Remove"));
}

void PDFAppManageToolGUI::moveSelection(int delta)
{
    // Alt + an arrow: one place earlier / later (the items of the assembly)
    const PDFDocumentAssembly::Selection& selection = getSelection();
    const std::vector<PDFDocumentAssembly::Item>& items = m_assembly.getItems();
    if (selection.empty() || items.empty())
    {
        return;
    }
    // One pass with a set (plan 6: Select all of 100 000 items, no work of their count squared)
    const std::set<QString> selected(selection.cbegin(), selection.cend());
    int first = -1;
    int last = -1;
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (selected.count(items[i].id))
        {
            first = first < 0 ? int(i) : first;
            last = int(i);
        }
    }
    if (delta < 0 && first > 0)
    {
        moveSelectionBefore(items[size_t(first - 1)].id);
    }
    else if (delta > 0 && last >= 0 && size_t(last + 1) < items.size())
    {
        const size_t target = size_t(last + 2);
        moveSelectionBefore(target < items.size() ? items[target].id : QString());
    }
}

void PDFAppManageToolGUI::moveSelectionBefore(const QString& beforeItemId)
{
    PDFDocumentAssembly::Edit edit;
    const PDFDocumentAssembly result = PDFDocumentAssembly::move(m_assembly, getSelection(), beforeItemId, &edit);
    applyEdit(result, edit, tr("Move"));
}

void PDFAppManageToolGUI::openMoveDialog()
{
    if (m_dialog || !isEditable() || getSelection().empty())
    {
        return;
    }
    const int count = PDFAppManageToolGUIHelper::toInt(qint64(m_assembly.getItems().size()));
    PDFAppManageNumberDialog* dialog = new PDFAppManageNumberDialog(PDFAppManageNumberDialog::Mode::MoveTo, count, 1, getScrimTarget(), getWindow());
    connect(dialog, &PDFAppManageNumberDialog::numberChosen, this, &PDFAppManageToolGUI::onMoveChosen);
    showDialog(dialog);
}

void PDFAppManageToolGUI::onMoveChosen(int value)
{
    const std::vector<PDFDocumentAssembly::Item>& items = m_assembly.getItems();
    moveSelectionBefore(value >= 1 && size_t(value) <= items.size() ? items[size_t(value - 1)].id : QString());
}

void PDFAppManageToolGUI::rename(const QString& name)
{
    const PDFDocumentAssembly::Selection& selection = getSelection();
    if (selection.size() != 1)
    {
        return;
    }

    // The name shown now (the file name of an unnamed item, owner Q5; Rename starts with it) is
    // no new name: accepted unchanged it gives the item no own name and no undo step
    const PDFDocumentAssembly::Item* item = PDFAppManageToolGUIHelper::findItem(m_assembly, selection.front());
    if (item && name == PDFAppManageItemModel::getItemName(m_assembly, *item))
    {
        return;
    }
    PDFDocumentAssembly::Edit edit;
    const PDFDocumentAssembly result = PDFDocumentAssembly::rename(m_assembly, selection.front(), name, &edit);
    applyEdit(result, edit, tr("Rename"));
}

void PDFAppManageToolGUI::openRenameDialog()
{
    const PDFDocumentAssembly::Selection& selection = getSelection();
    if (m_dialog || !isEditable() || selection.size() != 1 || isDocumentHost())
    {
        return;
    }
    const PDFDocumentAssembly::Item* item = PDFAppManageToolGUIHelper::findItem(m_assembly, selection.front());
    PDFAppManageTextDialog* dialog = new PDFAppManageTextDialog(PDFAppManageTextDialog::Mode::Name, tr("Rename"), tr("Rename"), item ? PDFAppManageItemModel::getItemName(m_assembly, *item) : QString(), 0, getScrimTarget(), getWindow());
    connect(dialog, &PDFAppManageTextDialog::textChosen, this, &PDFAppManageToolGUI::onRenameChosen);
    showDialog(dialog);
}

void PDFAppManageToolGUI::onRenameChosen(const QString& name)
{
    rename(name);
}

void PDFAppManageToolGUI::openReplaceDialog()
{
    if (!isEditable() || getSelection().empty())
    {
        return;
    }

    // Replace selection… (Ctrl+Alt+R): the file, then its pages (Add pages of a PDF in the mode Replace)
    QPointer<PDFAppManageToolGUI> guard(this);
    const QString file = PDFAppPanelFileDialogs::getOpenFileName(getWindow(), tr("Replace selection"), QString(), tr("PDF documents (*.pdf)"));
    if (!guard || file.isEmpty() || !m_list)
    {
        return;
    }
    const QString sourceId = findSourceOfFile(file);
    const int index = PDFAppManageToolGUIHelper::findSource(m_assembly, sourceId);
    if (index >= 0)
    {
        const PDFDocumentAssembly::Source& source = m_assembly.getSources()[size_t(index)];
        openAddPagesDialog(sourceId, source.name, source.pageCount, true);
        return;
    }
    m_isChangingList = true;
    const PDFAppToolInputList::AddResult result = m_list->addFile(file);
    if (!guard)
    {
        return;
    }
    m_isChangingList = false;
    addResults({ { result.id, int(result.refusal) } }, PendingAddition::Kind::ReplacePages, result.id.isEmpty() ? QStringList(PDFAppToolInputList::getRefusalText(result.refusal, result.name)) : QStringList());
}

void PDFAppManageToolGUI::crop(const QMarginsF& margins, bool isSameSource)
{
    PDFDocumentAssembly::Edit edit;
    const PDFDocumentAssembly result = PDFDocumentAssembly::crop(m_assembly, getSelection(), margins, isSameSource, &edit);
    applyEdit(result, edit, tr("Crop"));
}

void PDFAppManageToolGUI::openCropDialog()
{
    if (m_dialog || !isEditable() || getSelection().empty())
    {
        return;
    }
    QMarginsF margins;
    if (const PDFDocumentAssembly::Item* item = PDFAppManageToolGUIHelper::findItem(m_assembly, getSelection().front()); item && !item->pages.empty())
    {
        margins = item->pages.front().plan.cropMargins;
    }
    PDFAppManageCropDialog* dialog = new PDFAppManageCropDialog(margins, getScrimTarget(), getWindow());
    connect(dialog, &PDFAppManageCropDialog::cropChosen, this, &PDFAppManageToolGUI::onCropChosen);
    showDialog(dialog);
}

void PDFAppManageToolGUI::onCropChosen(const QMarginsF& margins, bool isSameSource)
{
    crop(margins, isSameSource);
}

void PDFAppManageToolGUI::openPropertiesDialog()
{
    if (m_dialog || getSelection().empty())
    {
        return;
    }

    // Properties (387:15926): Name, Type, Source (the path), Original page, Page size, Orientation, Rotation
    const PDFDocumentAssembly::Item* item = PDFAppManageToolGUIHelper::findItem(m_assembly, getSelection().front());
    if (!item || item->pages.empty())
    {
        return;
    }
    const PDFDocumentAssembly::Page& page = item->pages.front();
    const QSizeF size = PDFAppManagePageModel::getPageSize(m_assembly, page, createSourceViews());
    QString sourceText = QStringLiteral("–");
    if (page.plan.input >= 0 && size_t(page.plan.input) < m_assembly.getSources().size())
    {
        const PDFDocumentAssembly::Source& source = m_assembly.getSources()[size_t(page.plan.input)];
        sourceText = source.path.isEmpty() ? source.name : QDir::toNativeSeparators(source.path);
    }
    const std::vector<std::pair<QString, QString>> rows = {
        { tr("Name"), PDFAppManageItemModel::getItemName(m_assembly, *item) },
        { tr("Type"), PDFAppManageItemModel::getItemType(m_assembly, *item) },
        { tr("Source"), sourceText },
        { tr("Original page"), PDFAppManageItemModel::getOriginalPages(m_assembly, *item) },
        { tr("Page size"), PDFAppToolMergePageModel::getSizeText(size, false) },
        { tr("Orientation"), PDFAppToolMergePageModel::getOrientationText(size) },
        { tr("Rotation"), PDFAppManageToolGUIHelper::getRotationText(page.plan.turn) },
    };
    showDialog(new PDFAppManagePropertiesDialog(rows, getScrimTarget(), getWindow()));
}

void PDFAppManageToolGUI::restoreItem(const QString& removedItemId)
{
    PDFDocumentAssembly::Edit edit;
    const PDFDocumentAssembly result = PDFDocumentAssembly::restore(m_assembly, { removedItemId }, &edit);
    applyEdit(result, edit, tr("Restore"));
}

void PDFAppManageToolGUI::restoreAll()
{
    PDFDocumentAssembly::Edit edit;
    const PDFDocumentAssembly result = PDFDocumentAssembly::restoreAll(m_assembly, &edit);
    applyEdit(result, edit, tr("Restore all"));
}

void PDFAppManageToolGUI::copy()
{
    const PDFDocumentAssembly::Selection& selection = getSelection();
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (selection.empty() || !clipboard)
    {
        return;
    }

    // 4.6, B18: the items by value (kept by this GUI, pasted only into this assembly); the
    // clipboard holds the instance of the assembly and the token of the copy
    m_clipboardItems.clear();
    for (const PDFDocumentAssembly::Item* item : PDFAppManageToolGUIHelper::getSelectedItems(m_assembly, selection))
    {
        m_clipboardItems.push_back(*item);
    }
    m_clipboardToken = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonObject object;
    object.insert(QStringLiteral("instance"), m_assembly.getInstanceId());
    object.insert(QStringLiteral("mode"), QStringLiteral("copy"));
    object.insert(QStringLiteral("token"), m_clipboardToken);
    QJsonArray ids;
    for (const QString& id : selection)
    {
        ids.append(id);
    }
    object.insert(QStringLiteral("items"), ids);
    QMimeData* mimeData = new QMimeData();
    mimeData->setData(QString::fromLatin1(CLIPBOARD_MIME_TYPE), QJsonDocument(object).toJson(QJsonDocument::Compact));
    clipboard->setMimeData(mimeData);
}

void PDFAppManageToolGUI::cut()
{
    const PDFDocumentAssembly::Selection selection = getSelection();
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (selection.empty() || !clipboard || !isEditable())
    {
        return;
    }

    // B53: Cut removes the items into the bin at once; Paste brings them back to its place
    PDFDocumentAssembly::Edit edit;
    const PDFDocumentAssembly result = PDFDocumentAssembly::remove(m_assembly, selection, &edit);
    if (!edit.errorMessage.isEmpty())
    {
        showMessage(edit.errorMessage);
        return;
    }
    QJsonObject object;
    object.insert(QStringLiteral("instance"), m_assembly.getInstanceId());
    object.insert(QStringLiteral("mode"), QStringLiteral("cut"));
    QJsonArray ids;
    for (const QString& id : selection)
    {
        ids.append(id);
    }
    object.insert(QStringLiteral("items"), ids);
    QMimeData* mimeData = new QMimeData();
    mimeData->setData(QString::fromLatin1(CLIPBOARD_MIME_TYPE), QJsonDocument(object).toJson(QJsonDocument::Compact));
    clipboard->setMimeData(mimeData);
    applyAssembly(result, edit.selection, tr("Cut"), QString());
}

void PDFAppManageToolGUI::paste()
{
    using Helper = PDFAppManageToolGUIHelper;

    const QClipboard* clipboard = QGuiApplication::clipboard();
    const QMimeData* mimeData = clipboard ? clipboard->mimeData() : nullptr;
    if (!mimeData || !isEditable())
    {
        return;
    }
    if (!mimeData->hasFormat(QString::fromLatin1(CLIPBOARD_MIME_TYPE)))
    {
        // K7: Ctrl+V without items is Paste image
        if (mimeData->hasImage())
        {
            pasteImage();
        }
        return;
    }

    // B18: only into the assembly, which the items come from
    const QJsonObject object = QJsonDocument::fromJson(mimeData->data(QString::fromLatin1(CLIPBOARD_MIME_TYPE))).object();
    if (object.value(QStringLiteral("instance")).toString() != m_assembly.getInstanceId())
    {
        showMessage(tr("Pages can be pasted only into the assembly they come from"));
        return;
    }
    PDFDocumentAssembly::Selection ids;
    for (const QJsonValue& value : object.value(QStringLiteral("items")).toArray())
    {
        ids.push_back(value.toString());
    }

    // The place after the selection (B19): before the item after the last selected one (one pass
    // with a set, plan 6: 100 000 items)
    QString before;
    const std::vector<PDFDocumentAssembly::Item>& items = m_assembly.getItems();
    const PDFDocumentAssembly::Selection& selection = getSelection();
    const std::set<QString> selected(selection.cbegin(), selection.cend());
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (selected.count(items[i].id))
        {
            before =i + 1 < items.size() ? items[i + 1].id : QString();
        }
    }
    const bool hasPlace = !selection.empty();

    PDFDocumentAssembly::Edit edit;
    PDFDocumentAssembly result;
    if (object.value(QStringLiteral("mode")).toString() == QLatin1String("cut"))
    {
        // The cut items come back from the bin to the place (Paste after Cut moves them)
        PDFDocumentAssembly::Selection removed;
        const std::set<QString> cutIds(ids.cbegin(), ids.cend());
        for (const PDFDocumentAssembly::RemovedItem& item : m_assembly.getRemovedItems())
        {
            if (cutIds.count(item.item.id))
            {
                removed.push_back(item.item.id);
            }
        }
        if (removed.empty())
        {
            showMessage(tr("The cut pages are not in the bin any more"));
            return;
        }
        result = PDFDocumentAssembly::restore(m_assembly, removed, &edit);
    }
    else
    {
        // Copy: the copied values after the selection (insertItems); the copies are further
        // occurrences of their pages - clones (RB4: the original keeps the identity)
        if (object.value(QStringLiteral("token")).toString() != m_clipboardToken || m_clipboardItems.empty())
        {
            showMessage(tr("Pages can be pasted only into the assembly they come from"));
            return;
        }
        std::vector<PDFDocumentAssembly::Item> copies = m_clipboardItems;
        for (PDFDocumentAssembly::Item& item : copies)
        {
            for (PDFDocumentAssembly::Page& page : item.pages)
            {
                page.plan.isClone = page.plan.input != PDFDocumentAssembly::BLANK_INPUT;
            }
        }
        result = PDFDocumentAssembly::insertItems(m_assembly, selection, std::move(copies), &edit);
        applyEdit(result, edit, tr("Paste"));
        return;
    }
    if (!edit.errorMessage.isEmpty())
    {
        showMessage(edit.errorMessage);
        return;
    }
    const PDFDocumentAssembly::Selection pasted = edit.selection;
    if (hasPlace || !pasted.empty())
    {
        PDFDocumentAssembly::Edit moveEdit;
        result = PDFDocumentAssembly::move(result, pasted, hasPlace ? before : QString(), &moveEdit);
        if (!moveEdit.errorMessage.isEmpty())
        {
            showMessage(moveEdit.errorMessage);
            return;
        }
    }
    applyAssembly(result, pasted, tr("Paste"), QString());
}

void PDFAppManageToolGUI::saveCheckpoint(const QString& name)
{
    // B22, B23: a checkpoint has its id (names may repeat); saving is no undo step (RB7). The
    // count and the pages of the checkpoints by the rule of the file (owner 2026-11-12: what the
    // application saves, it always opens again)
    QString errorMessage;
    if (!PDFDocumentAssemblyFile::canCreateCheckpoint(m_assembly, m_checkpoints, &errorMessage))
    {
        showMessage(errorMessage);
        return;
    }
    m_checkpoints.push_back(PDFDocumentAssembly::createCheckpoint(m_assembly, name));
    updateAll();
}

void PDFAppManageToolGUI::openCheckpointDialog()
{
    if (m_dialog || !isEditable())
    {
        return;
    }
    PDFAppManageTextDialog* dialog = new PDFAppManageTextDialog(PDFAppManageTextDialog::Mode::Name, tr("Save checkpoint"), tr("Save"), tr("Checkpoint %1").arg(PDFAppLocale::formatNumber(qint64(m_checkpoints.size()) + 1)), 0, getScrimTarget(), getWindow());
    connect(dialog, &PDFAppManageTextDialog::textChosen, this, &PDFAppManageToolGUI::onCheckpointNameChosen);
    showDialog(dialog);
}

void PDFAppManageToolGUI::onCheckpointNameChosen(const QString& name)
{
    saveCheckpoint(name);
}

void PDFAppManageToolGUI::restoreCheckpoint(const QString& checkpointId)
{
    for (const PDFDocumentAssembly::Checkpoint& checkpoint : m_checkpoints)
    {
        if (checkpoint.id == checkpointId)
        {
            // B22: the restoration is an undo step; the bin stays
            PDFDocumentAssembly::Edit edit;
            const PDFDocumentAssembly result = PDFDocumentAssembly::restoreCheckpoint(m_assembly, checkpoint, &edit);
            applyEdit(result, edit, tr("Restore checkpoint"));
            return;
        }
    }
}

// ------------------------------------------------------------- output

void PDFAppManageToolGUI::setOutput(const QString& output)
{
    // The tab of a document has This document only (QB7 b); the own tab never This document
    if (isDocumentHost() || output == QLatin1String(PDFDocumentToolManage::OUTPUT_DOCUMENT))
    {
        return;
    }
    setParameter(PDFDocumentToolManage::PARAMETER_OUTPUT, output);
}

void PDFAppManageToolGUI::setOutline(const QString& outline)
{
    setParameter(PDFDocumentToolManage::PARAMETER_OUTLINE, outline);
}

void PDFAppManageToolGUI::setImages(const QString& images)
{
    setParameter(PDFDocumentToolManage::PARAMETER_OPTIMIZE_IMAGES, images);
}

QString PDFAppManageToolGUI::getOutput() const
{
    if (isDocumentHost())
    {
        return QString::fromLatin1(PDFDocumentToolManage::OUTPUT_DOCUMENT);
    }
    const QString output = getParameter(PDFDocumentToolManage::PARAMETER_OUTPUT);
    return output.isEmpty() || output == QLatin1String(PDFDocumentToolManage::OUTPUT_DOCUMENT) ? QString::fromLatin1(PDFDocumentToolManage::OUTPUT_ONE_PDF) : output;
}

void PDFAppManageToolGUI::setDocumentTitlesShown(bool isShown)
{
    m_areDocumentTitlesShown = isShown;
    updateAll();
}

// ------------------------------------------------------------- the assembly file

bool PDFAppManageToolGUI::isAssemblyModified() const
{
    if (isDocumentHost() || (m_assembly.isEmpty() && m_assembly.getRemovedItems().empty() && m_assemblyPath.isEmpty()))
    {
        return false;
    }
    return !m_assembly.isEqual(m_savedAssembly) || m_checkpoints.size() != m_savedCheckpointCount;
}

bool PDFAppManageToolGUI::saveAssembly()
{
    if (isDocumentHost())
    {
        return false;
    }
    if (m_assemblyPath.isEmpty())
    {
        return saveAssemblyAs();
    }
    QString errorMessage;
    if (!saveAssemblyTo(m_assemblyPath, &errorMessage))
    {
        showMessage(errorMessage);
        return false;
    }
    return true;
}

bool PDFAppManageToolGUI::saveAssemblyAs()
{
    if (isDocumentHost())
    {
        return false;
    }
    QPointer<PDFAppManageToolGUI> guard(this);
    const QString proposed = m_assemblyPath.isEmpty() ? tr("New assembly") + QStringLiteral(".") + QString::fromLatin1(PDFDocumentAssemblyFile::FILE_SUFFIX) : m_assemblyPath;
    const QString filePath = PDFAppPanelFileDialogs::getSaveFileName(getWindow(), tr("Save Assembly"), proposed, tr("PDF4QT assemblies (*.%1)").arg(QString::fromLatin1(PDFDocumentAssemblyFile::FILE_SUFFIX)));
    if (!guard || filePath.isEmpty())
    {
        return false;
    }
    QString errorMessage;
    if (!saveAssemblyTo(filePath, &errorMessage))
    {
        showMessage(errorMessage);
        return false;
    }
    return true;
}

bool PDFAppManageToolGUI::saveAssemblyTo(const QString& filePath, QString* errorMessage)
{
    using Helper = PDFAppManageToolGUIHelper;

    // Q11: a source without a file (an unsaved document, R11) cannot be found again - it is saved first
    std::vector<PDFDocumentAssembly::Source> sources = m_assembly.getSources();
    for (PDFDocumentAssembly::Source& source : sources)
    {
        if (source.kind == PDFDocumentAssembly::SourceKind::Pdf && source.path.isEmpty() && !source.isParked)
        {
            *errorMessage = tr("Save %1 as a file first – an assembly refers to the files of its sources").arg(source.name);
            return false;
        }

        // 387:16980: the size, the time and the hash of the file of the source (its bytes read by the list)
        const PDFAppToolInput* input = m_list ? m_list->findInput(getInputOfSource(source.id)) : nullptr;
        if (input && source.sha256.isEmpty() && !input->originalData.isEmpty() && !input->isModified)
        {
            source.sha256 = QCryptographicHash::hash(input->originalData, QCryptographicHash::Sha256);
        }
    }
    QString createError;
    PDFDocumentAssemblyFile::Content content;
    content.assembly = PDFDocumentAssembly::create(std::move(sources), m_assembly.getItems(), std::vector<PDFDocumentAssembly::RemovedItem>(), &createError);
    if (!createError.isEmpty())
    {
        *errorMessage = createError;
        return false;
    }
    content.checkpoints = m_checkpoints;
    if (!PDFDocumentAssemblyFile::checkPageCount(content, errorMessage))
    {
        // A file, which could not be opened again, is never written (checkpoints of a file over
        // the room of Save checkpoint, written by hand)
        return false;
    }
    if (const PDFAppToolParameterModel* model = getModel())
    {
        for (const char* name : { PDFDocumentToolManage::PARAMETER_OUTPUT, PDFDocumentToolManage::PARAMETER_OUTLINE, PDFDocumentToolManage::PARAMETER_OPTIMIZE_IMAGES,
                                  PDFDocumentToolFileOutput::PARAMETER_FOLDER, PDFDocumentToolFileOutput::PARAMETER_FILE_NAME, PDFDocumentToolFileOutput::PARAMETER_EXISTING_FILES })
        {
            const QVariant value = model->getValue(Helper::name(name));
            if (value.isValid())
            {
                content.outputParameters.insert(Helper::name(name), value);
            }
        }
    }
    const QByteArray bytes = PDFDocumentAssemblyFile::write(content, QFileInfo(filePath).absolutePath());

    // Whole or not at all (QSaveFile)
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
    {
        *errorMessage = tr("The assembly could not be saved: %1").arg(file.errorString());
        return false;
    }

    m_assemblyPath = QFileInfo(filePath).absoluteFilePath();
    m_savedAssembly = m_assembly;
    m_savedCheckpointCount = m_checkpoints.size();
    if (getContext() && getContext()->getDatabase())
    {
        PDFRecentAssemblyRepository repository(getContext()->getDatabase());
        repository.recordOpened(m_assemblyPath, Helper::toInt(Helper::getPageCount(m_assembly.getItems())));
    }

    // The title and the dot of the tab, the status (the last calls)
    QPointer<PDFAppManageToolGUI> guard(this);
    updateAll();
    if (guard)
    {
        showMessage(tr("The assembly was saved to %1").arg(QDir::toNativeSeparators(m_assemblyPath)));
    }
    return true;
}

void PDFAppManageToolGUI::openAssemblyFromDialog()
{
    if (isDocumentHost() || !isEditable())
    {
        return;
    }
    QPointer<PDFAppManageToolGUI> guard(this);
    const QString filePath = PDFAppPanelFileDialogs::getOpenFileName(getWindow(), tr("Open Assembly"), QString(), tr("PDF4QT assemblies (*.%1)").arg(QString::fromLatin1(PDFDocumentAssemblyFile::FILE_SUFFIX)));
    if (guard && !filePath.isEmpty())
    {
        openAssemblyFile(filePath);
    }
}

bool PDFAppManageToolGUI::openAssemblyFile(const QString& filePath)
{
    if (isDocumentHost() || !m_loader)
    {
        return false;
    }

    // 409:32023: another assembly asks to save this one first
    if (isAssemblyModified())
    {
        QPointer<PDFAppManageToolGUI> guard(this);
        const PDFAppToolCloseAnswer answer = askOwnTabClose();
        if (!guard || answer == PDFAppToolCloseAnswer::Cancel || !m_loader)
        {
            return false;
        }
    }
    return m_loader->load(filePath);
}

void PDFAppManageToolGUI::onLoaderLoaded(const PDFAppAssemblyLoadResult& result)
{
    // A check of a located file goes to the open dialog of the sources (387:16980)
    if (m_sourcesDialog && !result.isCanceled)
    {
        m_sourcesDialog->setResult(result);
        return;
    }
    applyLoadResult(result);
}

PDFAppToolTab* PDFAppManageToolGUI::findAssemblyTab(const PDFAppMainWindow* window, const QString& filePath)
{
    // The own tab of Manage Pages, which has the assembly file open (the canonical path): its
    // saved or opened file, the file it is loading (a second double click before the first load
    // has ended) or the file, whose sources its dialog 387:16980 resolves
    const QString canonical = QFileInfo(filePath).canonicalFilePath();
    if (!window || canonical.isEmpty())
    {
        return nullptr;
    }
    for (PDFAppToolTab* tab : window->getToolTabs())
    {
        PDFDocumentToolShell* shell = tab ? tab->getShell() : nullptr;
        const PDFAppManageToolGUI* gui = shell ? qobject_cast<const PDFAppManageToolGUI*>(shell->getGUI()) : nullptr;
        if (!gui)
        {
            continue;
        }
        const QStringList paths = { gui->getAssemblyPath(), gui->m_loader ? gui->m_loader->getLoadingFilePath() : QString(),
                                    gui->m_sourcesDialog ? gui->m_sourcesDialog->getResult().filePath : QString() };
        for (const QString& path : paths)
        {
            if (!path.isEmpty() && QFileInfo(path).canonicalFilePath() == canonical)
            {
                return tab;
            }
        }
    }
    return nullptr;
}

void PDFAppManageToolGUI::applyLoadResult(const PDFAppAssemblyLoadResult& result)
{
    if (result.isCanceled)
    {
        return;
    }
    if (!result.content || !m_list || !getModel())
    {
        showMessage(result.errorMessage.isEmpty() ? tr("The assembly could not be opened") : result.errorMessage);
        return;
    }

    // 387:16980: a source, which is not found or has changed, needs the user first
    for (const PDFAppAssemblySourceStatus& status : result.sources)
    {
        if (!status.isUsable())
        {
            openSourcesDialog(result);
            return;
        }
    }
    applyLoadedContent(result);
}

void PDFAppManageToolGUI::openSourcesDialog(const PDFAppAssemblyLoadResult& result)
{
    if (m_sourcesDialog)
    {
        m_sourcesDialog->setResult(result);
        m_sourcesDialog->raise();
        return;
    }
    m_sourcesDialog = new PDFAppManageSourcesDialog(result, getScrimTarget(), getWindow());
    connect(m_sourcesDialog.data(), &PDFAppManageSourcesDialog::locateRequested, this, &PDFAppManageToolGUI::onSourcesDialogLocate);
    connect(m_sourcesDialog.data(), &PDFAppManageSourcesDialog::openFromThereRequested, this, &PDFAppManageToolGUI::onSourcesDialogOpenFromThere);
    connect(m_sourcesDialog.data(), &PDFAppManageSourcesDialog::openRequested, this, &PDFAppManageToolGUI::applyLoadedContent);
    connect(m_sourcesDialog.data(), &QDialog::finished, this, &PDFAppManageToolGUI::onSourcesDialogFinished);

    // Last: the dialog runs from the event loop (open, never exec)
    m_sourcesDialog->open();
}

void PDFAppManageToolGUI::onSourcesDialogLocate(const QString& sourceId)
{
    // Locate…: a file chosen by the user - checked by the loader (its state comes to the dialog).
    // The loader checks one file at a time: a second Locate… during a check says so (RS3BAPP Low)
    if (m_loader && m_loader->isLoading())
    {
        showMessage(getLocateBusyText());
        return;
    }
    QPointer<PDFAppManageToolGUI> guard(this);
    const QString file = PDFAppPanelFileDialogs::getOpenFileName(getWindow(), tr("Locate"), QString(), tr("PDF documents (*.pdf)"));
    if (guard && m_loader && m_sourcesDialog && !file.isEmpty() && !m_loader->locate(m_sourcesDialog->getResult(), sourceId, file))
    {
        showMessage(getLocateBusyText());
    }
}

void PDFAppManageToolGUI::onSourcesDialogOpenFromThere(const QString& sourceId)
{
    // RB8: the file of another computer is opened only by this explicit choice of the user
    if (!m_loader || !m_sourcesDialog)
    {
        return;
    }
    for (const PDFAppAssemblySourceStatus& status : m_sourcesDialog->getResult().sources)
    {
        if (status.sourceId == sourceId)
        {
            if (!m_loader->locate(m_sourcesDialog->getResult(), sourceId, status.filePath))
            {
                showMessage(getLocateBusyText());
            }
            return;
        }
    }
}

QString PDFAppManageToolGUI::getLocateBusyText()
{
    // No frame (387:16980 shows one check): the status bar of the window below the dialog
    return tr("Another file is being checked – choose this one when the check ends");
}

void PDFAppManageToolGUI::onSourcesDialogFinished()
{
    // Cancel, Open Without Them, Open Assembly: a check of a located file, which still runs,
    // belongs to the closed dialog - its result is dropped (it would open the assembly again, or
    // open it after Cancel); the loader delivers it as canceled
    if (m_loader && m_loader->isLoading())
    {
        m_loader->cancel();
    }
}

void PDFAppManageToolGUI::applyLoadedContent(const PDFAppAssemblyLoadResult& result)
{
    using Helper = PDFAppManageToolGUIHelper;

    // B24: the content replaces the assembly only now. The usable sources take their files, the
    // others are parked with their pages in the bin (PDFAppAssemblyLoader::createContent:
    // updateSource / removeSource of the core); the PDF files are read by the list and bound
    QString contentError;
    const std::optional<PDFDocumentAssemblyFile::Content> content = PDFAppAssemblyLoader::createContent(result, &contentError);
    if (!content || !m_list || !getModel())
    {
        showMessage(contentError.isEmpty() ? tr("The assembly could not be opened") : contentError);
        return;
    }
    PDFDocumentAssembly assembly = content->assembly;
    QStringList missingNames;
    std::vector<PendingAddition> bindings;
    QStringList refused;
    for (const PDFDocumentAssembly::Source& source : assembly.getSources())
    {
        if (source.kind != PDFDocumentAssembly::SourceKind::Pdf)
        {
            continue;
        }
        const QString path = source.path;
        if (source.isParked || path.isEmpty())
        {
            missingNames << source.name;
            continue;
        }

        QPointer<PDFAppManageToolGUI> guard(this);
        m_isChangingList = true;
        PDFAppToolInputList::AddResult added = m_list->addFile(path);
        if (!guard)
        {
            return;
        }
        m_isChangingList = false;
        if (added.id.isEmpty() && added.refusal == PDFAppToolInputList::Refusal::Duplicate)
        {
            // The file is an input of this tab already: its input is bound
            for (int i = 0; i < m_list->getCount(); ++i)
            {
                if (QFileInfo(m_list->getInput(i)->filePath).canonicalFilePath() == QFileInfo(path).canonicalFilePath())
                {
                    added.id = m_list->getInput(i)->id;
                }
            }
        }
        if (added.id.isEmpty())
        {
            refused << PDFAppToolInputList::getRefusalText(added.refusal, added.name);
            continue;
        }
        PendingAddition pending;
        pending.inputId = added.id;
        pending.kind = PendingAddition::Kind::Bind;
        pending.sourceId = source.id;
        bindings.push_back(pending);
    }
    m_pending.insert(m_pending.end(), bindings.cbegin(), bindings.cend());

    // The assembly and the output of the file as one undo step, then the file state of its
    // instance (followFileState: the assembly before it keeps its own file for Undo)
    const QString assemblyPath = QFileInfo(result.filePath).absoluteFilePath();
    QVariantMap values = content->outputParameters;
    values.remove(Helper::name(PDFDocumentToolManage::PARAMETER_ASSEMBLY));
    values.remove(Helper::name(PDFDocumentToolManage::PARAMETER_INPUTS));
    values.insert(Helper::name(PDFDocumentToolManage::PARAMETER_ASSEMBLY), QVariant::fromValue(assembly));
    QStringList inputIds;
    for (const PendingAddition& pending : bindings)
    {
        inputIds << pending.inputId;
    }
    values.insert(Helper::name(PDFDocumentToolManage::PARAMETER_INPUTS), inputIds.join(QLatin1Char(',')));
    if (getContext() && getContext()->getDatabase())
    {
        PDFRecentAssemblyRepository repository(getContext()->getDatabase());
        repository.recordOpened(assemblyPath, Helper::toInt(Helper::getPageCount(assembly.getItems())));
    }

    QPointer<PDFAppManageToolGUI> guard(this);
    m_hasPendingSelection = true;
    m_pendingSelection.clear();
    const QString error = getModel()->setValues(values, tr("Open Assembly"));
    if (!guard)
    {
        return;
    }
    if (!error.isEmpty())
    {
        showMessage(error);
        return;
    }
    m_checkpoints = content->checkpoints;
    m_assemblyPath = assemblyPath;
    m_savedAssembly = assembly;
    m_savedCheckpointCount = m_checkpoints.size();
    queueProcessPending();
    updateAll();
    if (!guard)
    {
        return;
    }
    if (!missingNames.isEmpty())
    {
        refused.prepend(tr("Not found: %1 – its pages are under Removed pages").arg(missingNames.join(tr(", "))));
    }
    if (!refused.isEmpty())
    {
        showMessage(refused.join(QStringLiteral(" · ")));
    }
}

void PDFAppManageToolGUI::openAsAssembly()
{
    // 1479:34632 "Open as an Assembly": the own tab with a snapshot of the document as its source
    // A (QB7 b); the document of this tab is not changed there
    PDFAppDocument* document = getDocument();
    PDFAppMainWindow* window = m_canvas ? PDFAppPanelWidgets::findMainWindow(m_canvas) : nullptr;
    if (!document || !window)
    {
        return;
    }
    PDFAppToolInputList* list = new PDFAppToolInputList(nullptr);
    list->addOpenDocument(document);

    // The window takes the list over; the new tab may run code of the application (the last call
    // on this GUI). A list, which no tab has taken (too many tabs), is deleted here.
    QPointer<PDFAppToolInputList> listGuard(list);
    window->createToolTab(QString::fromLatin1(PDFDocumentToolManage::TOOL_ID), list);
    if (listGuard && !listGuard->parent())
    {
        delete listGuard.data();
    }
}

// ------------------------------------------------------------- document host (RB2)

void PDFAppManageToolGUI::updateDocumentMapping()
{
    PDFAppDocument* document = getDocument();
    PDFAppToolParameterModel* model = getModel();
    if (!m_isValuesInitialized || !isDocumentHost() || !document || !model || !document->getDocument() || document->getVersion() == m_documentVersion)
    {
        return;
    }

    // RB2: the assembly follows the page objects of the new version - the current value and every
    // step of Undo and Redo; new pages go to the end, the pages, which are gone, leave
    const PDFDocumentPointer pdfDocument = document->getDocument();
    const quint64 version = document->getVersion();
    if (m_assembly.getSources().empty())
    {
        // The document had more pages than an assembly at the start (initializeValues: no
        // assembly, nothing could be edited): the assembly comes, when the document fits (the
        // model announces it, last)
        m_documentVersion = version;
        if (getPageLimitText().isEmpty())
        {
            model->setFollowingValue(PDFAppManageToolGUIHelper::name(PDFDocumentToolManage::PARAMETER_ASSEMBLY),
                                     QVariant::fromValue(PDFDocumentAssembly::createForDocument(pdfDocument.data(), version, document->getTitle())));
        }
        return;
    }
    std::set<PDFObjectReference> oldPages;
    for (const PDFDocumentAssembly::Item& item : m_assembly.getItems())
    {
        for (const PDFDocumentAssembly::Page& page : item.pages)
        {
            oldPages.insert(page.targetPage);
        }
    }
    PDFInteger added = 0;
    const PDFDocumentAssembly mapped = PDFDocumentAssembly::mapToDocument(m_assembly, pdfDocument.data(), version, &added);
    m_documentVersion = version;
    m_newTargetPages.clear();
    for (const PDFDocumentAssembly::Item& item : mapped.getItems())
    {
        for (const PDFDocumentAssembly::Page& page : item.pages)
        {
            if (page.targetPage.isValid() && !oldPages.count(page.targetPage))
            {
                m_newTargetPages.insert(page.targetPage);
            }
        }
    }
    const int addedCount = PDFAppManageToolGUIHelper::toInt(added);
    m_statusNote = addedCount > 0 ? (addedCount == 1 ? tr("1 page was added to the document and placed at the end") : tr("%Ln pages were added to the document and placed at the end", nullptr, addedCount)) : QString();

    // The model announces the changed values (readValues), last
    const PDFAppManageDocumentMapper mapper(pdfDocument, version);
    model->mapValues(mapper);
}

PDFAppPermissionRequest PDFAppManageToolGUI::createPermissionRequest() const
{
    // RB3 a: This document needs Assemble (bit 11); a crop of a page needs Modify too (bit 4, Q4)
    std::vector<PDFSecurityHandler::Permission> permissions;
    for (const PDFDocumentAssembly::Item& item : m_assembly.getItems())
    {
        for (const PDFDocumentAssembly::Page& page : item.pages)
        {
            const QMarginsF& margins = page.plan.cropMargins;
            if (margins.left() > 0.0 || margins.top() > 0.0 || margins.right() > 0.0 || margins.bottom() > 0.0)
            {
                permissions.push_back(PDFSecurityHandler::Permission::Modify);
                break;
            }
        }
        if (!permissions.empty())
        {
            break;
        }
    }
    return PDFAppPermissionRequest::createTool(QString::fromLatin1(PDFDocumentToolManage::TOOL_ID), permissions, getRunParameters());
}

bool PDFAppManageToolGUI::isDocumentChangeAllowed() const
{
    const PDFAppDocument* document = getDocument();
    return document && document->getDocument() && !document->isLocked() && PDFAppDocumentPermissions::isAllowed(document->getDocument().data(), createPermissionRequest());
}

QString PDFAppManageToolGUI::getLockText() const
{
    const PDFAppDocument* document = getDocument();
    if (!isDocumentHost() || !document)
    {
        return QString();
    }
    if (document->isLocked())
    {
        // Coordinator decision (no frame): the locked document
        return tr("Document is locked – unlock it or open it as an assembly");
    }
    if (!isDocumentChangeAllowed())
    {
        // 1479:35631
        return tr("%1 does not allow inserting, rotating and removing pages · Rearrange Pages needs its owner password").arg(document->getTitle());
    }
    return QString();
}

QString PDFAppManageToolGUI::getPageLimitText() const
{
    // The document itself, not the assembly: also a document, which has grown by another tool
    // (mapToDocument adds its pages), or which got no assembly at the start
    const PDFAppDocument* document = getDocument();
    QString errorMessage;
    if (isDocumentHost() && document && document->getDocument() && !PDFDocumentAssembly::checkDocumentPageCount(document->getPageCount(), &errorMessage))
    {
        return errorMessage;
    }
    return QString();
}

void PDFAppManageToolGUI::onCalloutLinkActivated()
{
    // 1479:35631 "Enter owner password…": the owner password dialog of the shared check (owner
    // 2026-11-08); the tool continues after the unlock (onDocumentChanged)
    PDFAppDocument* document = getDocument();
    if (document)
    {
        PDFAppDocumentPermissions::request(document, createPermissionRequest(), getWindow());
    }
}

// ------------------------------------------------------------- the parts

QWidget* PDFAppManageToolGUI::createToolActions(QWidget* parent)
{
    using Helper = PDFAppManageToolGUIHelper;

    connectParts();
    m_actions.clear();
    m_panels.clear();
    m_panelRows.clear();
    m_openDocumentRows.clear();
    m_recentRows.clear();

    QWidget* widget = new QWidget(parent);
    widget->setObjectName(QStringLiteral("toolActions"));
    PDFAppToolPanelBuilder builder(this, widget, Qt::Horizontal, PDFAppToolPanelBuilder::ACTION_GAP);
    const bool isDocument = isDocumentHost();

    if (!isDocument)
    {
        // tb-save-assembly 387:12669 with the menu of the assembly 409:32023
        PDFAppToolPanelPopup* assemblyPanel = builder.addMenu(QStringLiteral("assembly"), QStringLiteral("save"), QString(), ASSEMBLY_PANEL_WIDTH);
        assemblyPanel->getButton()->setToolTip(Helper::withKeys(tr("Save assembly"), QStringLiteral("assemblySave")));
        assemblyPanel->getButton()->setAccessibleName(tr("Assembly"));
        // 387:12669 is 50 wide: the icon and the chevron of its menu (the only icon button of the
        // toolbar with a menu - the others say their name)
        assemblyPanel->getButton()->setProperty(PDFWidgetStyleProperties::MENU_INDICATOR, true);
        m_panels[QStringLiteral("assembly")] = assemblyPanel;
        m_actions[QStringLiteral("assembly")] = assemblyPanel->getButton();
        connectRows(Helper::addRows(assemblyPanel, {
            { "assemblyOpen", tr("Open Assembly…"), tr("Continue a saved arrangement"), QStringLiteral("folder-open"), QString() },
            { "assemblySave", tr("Save Assembly"), tr("Keeps sources, order, groups and checkpoints"), QStringLiteral("save"), QString() },
            { "assemblySaveAs", tr("Save Assembly As…"), tr("Saves under a new name"), QStringLiteral("save-as"), QString() },
        }));
        updateRecentRows();
        builder.addActionDivider();
    }

    // tb-Add 387:12676 with the panel of 387:13288
    PDFAppToolPanelPopup* addPanel = builder.addMenu(QStringLiteral("add"), QStringLiteral("plus"), tr("Add"), ADD_PANEL_WIDTH);
    m_panels[QStringLiteral("add")] = addPanel;
    m_actions[QStringLiteral("add")] = addPanel->getButton();
    connectRows(Helper::addRows(addPanel, {
        { "addFiles", tr("Files…"), tr("PDF documents and images"), QStringLiteral("folder-open"), QString() },
        { "addPages", tr("Pages of a PDF…"), tr("Choose a page range of one file"), QStringLiteral("page-insert"), QString() },
        { "addBlank", tr("Blank page"), tr("Same size as the page before it"), QStringLiteral("file"), QString() },
        { "addPaste", tr("Paste image"), tr("Picture from the clipboard"), QStringLiteral("clipboard"), QString() },
    }));
    updateOpenDocumentRows();

    // tb-Select 387:12681
    PDFAppToolPanelPopup* selectPanel = builder.addMenu(QStringLiteral("select"), QStringLiteral("select-all"), tr("Select"), SELECT_PANEL_WIDTH);
    m_panels[QStringLiteral("select")] = selectPanel;
    m_actions[QStringLiteral("select")] = selectPanel->getButton();
    connectRows(Helper::addRows(selectPanel, {
        { "selectAll", tr("All"), QString(), QStringLiteral("select-all"), QString() },
        { "selectNone", tr("None"), QString(), QStringLiteral("select-none"), QString() },
        { "selectInvert", tr("Invert"), QString(), QStringLiteral("select-invert"), QString() },
        { "selectRange", tr("Page range…"), QString(), QStringLiteral("range"), QString() },
        { "selectOdd", tr("Odd pages"), QString(), QString(), QStringLiteral("1 3"), true },
        { "selectEven", tr("Even pages"), QString(), QString(), QStringLiteral("2 4") },
        { "selectPortrait", tr("Portrait"), QString(), QStringLiteral("file-tall"), QString() },
        { "selectLandscape", tr("Landscape"), QString(), QStringLiteral("file-wide"), QString() },
        { "selectSearch", tr("Search results"), QString(), QStringLiteral("search"), QString() },
    }));

    if (!isDocument)
    {
        // tb-Group 387:12686 (not in the tab of a document: coordinator decision)
        PDFAppToolPanelPopup* groupPanel = builder.addMenu(QStringLiteral("group"), QStringLiteral("group"), tr("Group"), GROUP_PANEL_WIDTH);
        m_panels[QStringLiteral("group")] = groupPanel;
        m_actions[QStringLiteral("group")] = groupPanel->getButton();
        connectRows(Helper::addRows(groupPanel, {
            { "group", tr("Group"), tr("Selected pages become one item"), QStringLiteral("group"), QString() },
            { "ungroup", tr("Ungroup"), tr("One item per page"), QStringLiteral("ungroup"), QString() },
            { "regroupOddEven", tr("Odd and even pages"), tr("Two groups"), QStringLiteral("interleave"), QString(), true, tr("Regroup the selection into groups") },
            { "regroupPairs", tr("Page pairs"), tr("Groups of two pages"), QStringLiteral("page-pair"), QString() },
            { "regroupEvery", tr("Every N pages…"), tr("Groups with the same number of pages"), QString(), QStringLiteral("N") },
            { "regroupNumbers", tr("At page numbers…"), tr("A new group starts at each page you enter"), QString(), QStringLiteral("p.") },
            { "regroupBookmarks", tr("By bookmarks…"), tr("A group starts at each chosen bookmark"), QStringLiteral("bookmark"), QString() },
            { "regroupAlternate", tr("Alternate two files"), tr("Front and back sides mixed"), QStringLiteral("repeat"), QString() },
            { "regroupAlternateReversed", tr("Alternate, second file reversed"), tr("For stacks scanned from the back"), QStringLiteral("interleave-reversed"), QString() },
            { "regroupReverse", tr("Reverse order"), tr("One group, last page first"), QStringLiteral("reverse"), QString() },
        }));
    }

    // tb-Sort 387:12691
    PDFAppToolPanelPopup* sortPanel = builder.addMenu(QStringLiteral("sort"), QStringLiteral("arrow-up-down"), tr("Sort"), SORT_PANEL_WIDTH);
    m_panels[QStringLiteral("sort")] = sortPanel;
    m_actions[QStringLiteral("sort")] = sortPanel->getButton();
    connectRows(Helper::addRows(sortPanel, {
        { "sortName", tr("File name"), tr("A to Z, natural numbers"), QStringLiteral("sort-az"), QString() },
        { "sortSource", tr("Source"), tr("Keeps pages of a file together"), QStringLiteral("sort-source"), QString() },
        { "sortPage", tr("Page number"), tr("Original page numbers"), QStringLiteral("list-ordered"), QString() },
        { "sortType", tr("Type"), tr("PDF pages, images, blank pages"), QStringLiteral("images"), QString() },
        { "sortReverse", tr("Reverse order"), tr("Turns the current order around"), QStringLiteral("reverse"), QString(), true },
    }));

    builder.addActionDivider();

    // tb-rotate-left, tb-rotate-right, tb-remove 387:12698 ... 387:12713: icon buttons with the
    // name and the key in their tooltips (405:31588)
    struct IconAction
    {
        const char* action;
        QString iconName;
        QString text;
    };
    const IconAction iconActions[] = {
        { "rotateLeft", QStringLiteral("rotate-ccw"), tr("Rotate left") },
        { "rotateRight", QStringLiteral("rotate-cw"), tr("Rotate right") },
        { "remove", QStringLiteral("trash"), tr("Remove") },
    };
    for (const IconAction& iconAction : iconActions)
    {
        const QString action = QString::fromLatin1(iconAction.action);
        QToolButton* button = PDFAppDocumentToolbar::createToolbarButton(QStringLiteral("manageAction-") + action, iconAction.iconName, widget);
        button->setToolTip(Helper::withKeys(iconAction.text, action));
        button->setAccessibleName(iconAction.text);
        button->setProperty(Helper::ACTION_KEY, action);
        connect(button, &QToolButton::clicked, this, &PDFAppManageToolGUI::onKeyAction);
        builder.addWidget(button);
        m_actions[action] = button;
    }

    // tb-more 387:12718 with the panel of 387:14220 (and Crop… of Q17)
    PDFAppToolPanelPopup* morePanel = builder.addMenu(QStringLiteral("more"), QStringLiteral("more-horizontal"), QString(), MORE_PANEL_WIDTH);
    morePanel->getButton()->setToolTip(Helper::withKeys(tr("More"), QStringLiteral("more")));
    morePanel->getButton()->setAccessibleName(tr("More"));
    m_panels[QStringLiteral("more")] = morePanel;
    m_actions[QStringLiteral("more")] = morePanel->getButton();
    connectRows(Helper::addRows(morePanel, {
        { "clone", tr("Clone"), QString(), QStringLiteral("duplicate-page"), QString() },
        { "cut", tr("Cut"), QString(), QStringLiteral("clipboard-cut"), QString() },
        { "copy", tr("Copy"), QString(), QStringLiteral("copy"), QString() },
        { "paste", tr("Paste"), QString(), QStringLiteral("clipboard"), QString() },
        { "replace", tr("Replace selection…"), tr("Puts a PDF in place of the selected pages"), QStringLiteral("replace"), QString() },
        { "resetRotation", tr("Reset rotation"), QString(), QStringLiteral("rotation-reset"), QString(), true },
        { "crop", tr("Crop…"), tr("Cuts the margins of the selected pages"), QStringLiteral("crop"), QString() },
        { "titles", tr("Show document titles"), tr("Instead of file names"), QStringLiteral("type"), QString(), true },
        { "properties", tr("Properties…"), QString(), QStringLiteral("info"), QString() },
    }));

    builder.addActionDivider();
    if (!isDocument)
    {
        // tb-output 396:26549 with the panel of 396:26763
        PDFAppToolPanelPopup* outputPanel = builder.addMenu(QStringLiteral("output"), QStringLiteral("file"), QString(), OUTPUT_PANEL_WIDTH);
        m_panels[QStringLiteral("output")] = outputPanel;
        m_actions[QStringLiteral("output")] = outputPanel->getButton();
        connectRows(Helper::addRows(outputPanel, {
            { "output-one-pdf", tr("One PDF"), tr("All pages in one document"), QStringLiteral("file"), QString(), false, tr("Create") },
            { "output-per-group", tr("One PDF per group or item"), QString(), QStringLiteral("group"), QString() },
            { "output-per-page", tr("One PDF per page"), QString(), QStringLiteral("split-pages"), QString() },
            { "outline-document-parts", tr("Document parts"), tr("One bookmark per group or run of a file"), QStringLiteral("tree"), QString(), true, tr("Outline") },
            { "outline-join", tr("Join outlines"), tr("Bookmarks of the files nested under each part"), QStringLiteral("list"), QString() },
            { "outline-none", tr("No outline"), tr("The result has no bookmarks"), QStringLiteral("ban"), QString() },
            { "images-off", tr("Off"), tr("Images are stored exactly as they are"), QStringLiteral("ban"), QString(), true, tr("Images") },
            { "images-quality", tr("Prefer quality"), tr("Downsamples large images, keeps detail"), QStringLiteral("award"), QString() },
            { "images-size", tr("Minimum size"), tr("Smallest files, stronger compression"), QStringLiteral("minimize-2"), QString() },
        }));
        for (const auto& [name, row] : m_panelRows)
        {
            if (row && (name.startsWith(QStringLiteral("output-")) || name.startsWith(QStringLiteral("outline-")) || name.startsWith(QStringLiteral("images-"))))
            {
                row->setSelectionBehavior(PDFComponentButton::SelectionBehavior::Exclusive);
            }
        }
    }
    else
    {
        // 1479:34632 "Open as an Assembly" (title case, coordinator)
        QToolButton* button = builder.addAction(tr("Open as an Assembly"), QStringLiteral("grid"), this, &PDFAppManageToolGUI::openAsAssembly);
        button->setObjectName(QStringLiteral("manageOpenAsAssembly"));
        m_actions[QStringLiteral("openAsAssembly")] = button;
    }

    installKeys();
    updateToolbar();
    return widget;
}

QWidget* PDFAppManageToolGUI::createLeftPanel(QWidget* parent)
{
    connectParts();
    m_sourcesPanel = new PDFAppManageSourcesPanel(parent);
    connect(m_sourcesPanel.data(), &PDFAppManageSourcesPanel::sourceClicked, this, &PDFAppManageToolGUI::onSourceClicked);
    connect(m_sourcesPanel.data(), &PDFAppManageSourcesPanel::sourceLinkActivated, this, &PDFAppManageToolGUI::onSourceLinkActivated);
    connect(m_sourcesPanel.data(), &PDFAppManageSourcesPanel::saveCheckpointRequested, this, &PDFAppManageToolGUI::openCheckpointDialog);
    connect(m_sourcesPanel.data(), &PDFAppManageSourcesPanel::restoreCheckpointRequested, this, &PDFAppManageToolGUI::restoreCheckpoint);
    connect(m_sourcesPanel.data(), &PDFAppManageSourcesPanel::restoreAllRequested, this, &PDFAppManageToolGUI::restoreAll);
    connect(m_sourcesPanel.data(), &PDFAppManageSourcesPanel::restoreRequested, this, &PDFAppManageToolGUI::restoreItem);
    connect(m_sourcesPanel.data(), &PDFAppManageSourcesPanel::removedExpandedChanged, this, &PDFAppManageToolGUI::updateSources);
    updateSources();
    return m_sourcesPanel.data();
}

QWidget* PDFAppManageToolGUI::createRightPanel(QWidget* parent)
{
    connectParts();
    m_resultPanel = new PDFAppManageResultPanel(this, isDocumentHost() ? nullptr : m_outputFiles.data(), parent);
    connect(m_resultPanel.data(), &PDFAppManageResultPanel::renameRequested, this, &PDFAppManageToolGUI::openRenameDialog);
    connect(m_resultPanel.data(), &PDFAppManageResultPanel::calloutLinkActivated, this, &PDFAppManageToolGUI::onCalloutLinkActivated);
    updateRightPanel();
    return m_resultPanel.data();
}

PDFAppToolCalloutPlace PDFAppManageToolGUI::getCertificationCalloutPlace() const
{
    PDFAppToolCalloutPlace place;
    QBoxLayout* layout = m_resultPanel ? qobject_cast<QBoxLayout*>(m_resultPanel->layout()) : nullptr;
    PDFAppToolCallout* lockCallout = m_resultPanel ? m_resultPanel->getLockCallout() : nullptr;
    const int lockIndex = layout && lockCallout ? layout->indexOf(lockCallout) : -1;
    if (lockIndex >= 0)
    {
        place.layout = layout;
        place.index = lockIndex + 1;
    }
    return place;
}

QWidget* PDFAppManageToolGUI::createCanvasPage(QWidget* parent)
{
    connectParts();
    if (!m_isTileSizeRead)
    {
        m_isTileSizeRead = true;
        m_tileSize = PDFAppToolTileSizeMenu::readSize(getContext(), PDFAppToolPageGrid::TileSize::Small);
    }

    // The canvas of the pages (387:12602); it lives with the parts of the shell
    m_canvas = new PDFAppManageCanvas(parent);
    m_canvas->setTileSize(m_tileSize);
    m_canvas->setDetailsShown(m_isDetailsShown);
    connect(m_canvas.data(), &PDFAppManageCanvas::selectionChanged, this, &PDFAppManageToolGUI::onCanvasSelectionChanged);
    connect(m_canvas.data(), &PDFAppManageCanvas::moveRequested, this, &PDFAppManageToolGUI::onCanvasMoveRequested);
    connect(m_canvas.data(), &PDFAppManageCanvas::moveByRequested, this, &PDFAppManageToolGUI::moveSelection);
    connect(m_canvas.data(), &PDFAppManageCanvas::removeRequested, this, &PDFAppManageToolGUI::removeSelection);
    connect(m_canvas.data(), &PDFAppManageCanvas::contextMenuRequested, this, &PDFAppManageToolGUI::onCanvasContextMenu);
    connect(m_canvas.data(), &PDFAppManageCanvas::searchChanged, this, &PDFAppManageToolGUI::onCanvasSearchChanged);
    connect(m_canvas.data(), &PDFAppManageCanvas::viewSwitched, this, &PDFAppManageToolGUI::onCanvasViewSwitched);

    // Size: Small in the bar of the preview: its panel opens below the button
    QToolButton* sizeButton = m_canvas->getTileSizeButton();
    PDFAppToolPanelPopup* sizePanel = new PDFAppToolPanelPopup(nullptr, PDFAppToolTileSizeMenu::PANEL_WIDTH, sizeButton);
    sizePanel->setObjectName(QStringLiteral("toolPanel-thumbnailSize"));
    connect(sizeButton, &QToolButton::clicked, this, &PDFAppManageToolGUI::onTileSizeButtonClicked);
    connect(sizePanel, &PDFAppDocumentPanelPopup::closed, this, &PDFAppManageToolGUI::onTileSizePanelClosed);
    m_tileSizeMenu = new PDFAppToolTileSizeMenu(sizePanel, tr("Size: %1"), sizePanel);
    m_tileSizeMenu->setDefaultSize(PDFAppToolPageGrid::TileSize::Small);
    m_tileSizeMenu->setSize(m_tileSize);
    connect(m_tileSizeMenu.data(), &PDFAppToolTileSizeMenu::sizeChosen, this, &PDFAppManageToolGUI::onTileSizeChosen);

    // The content of the canvas is said to its views and observers, which may delete this GUI
    QPointer<PDFAppManageToolGUI> guard(this);
    QPointer<PDFAppManageCanvas> canvas = m_canvas;
    m_canvas->setContent(m_assembly, createSourceViews(), isDocumentHost(), m_newTargetPages);
    if (guard)
    {
        updateAll();
    }
    return canvas.data();
}

void PDFAppManageToolGUI::setContentEnabled(bool isEnabled)
{
    // The actions are disabled while the result is made; the pages and the panels stay
    if (PDFDocumentToolShell* shell = getShell())
    {
        if (QWidget* actions = shell->getToolActions())
        {
            actions->setEnabled(isEnabled);
        }
    }
    updateEditable();
}

void PDFAppManageToolGUI::installKeys()
{
    using Helper = PDFAppManageToolGUIHelper;

    // The keys of the frames on the key widget of the host: they reach every part of the tab
    // (405:31588); the destructor takes them back
    if (!getHost() || !getHost()->getKeyWidget() || !m_keyActions.empty())
    {
        return;
    }
    m_keyWidget = getHost()->getKeyWidget();
    QStringList actions = { QStringLiteral("addFiles"), QStringLiteral("addBlank"), QStringLiteral("selectAll"), QStringLiteral("selectNone"),
                            QStringLiteral("rotateLeft"), QStringLiteral("rotateRight"), QStringLiteral("more"), QStringLiteral("clone"),
                            QStringLiteral("cut"), QStringLiteral("copy"), QStringLiteral("paste"), QStringLiteral("replace"),
                            QStringLiteral("restoreAll"), QStringLiteral("tilesLarger"), QStringLiteral("tilesSmaller"),
                            QStringLiteral("focusNextPart"), QStringLiteral("focusPreviousPart") };
    if (!isDocumentHost())
    {
        // The tab of a document keeps Ctrl+S for the document (coordinator), and has no groups
        actions << QStringLiteral("assemblyOpen") << QStringLiteral("assemblySave") << QStringLiteral("assemblySaveAs")
                << QStringLiteral("group") << QStringLiteral("ungroup") << QStringLiteral("rename");
    }
    for (const QString& name : actions)
    {
        QAction* action = new QAction(m_keyWidget);
        action->setObjectName(QStringLiteral("manageKey-") + name);
        action->setShortcut(Helper::getKeys(name));
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        action->setProperty(Helper::ACTION_KEY, name);
        connect(action, &QAction::triggered, this, &PDFAppManageToolGUI::onKeyAction);
        m_keyWidget->addAction(action);
        m_keyActions[name] = action;
    }

    // A drop of files anywhere in the tab adds them (409:32739 "Drop files anywhere in this window")
    m_wasDropAccepted = m_keyWidget->acceptDrops();
    m_keyWidget->setAcceptDrops(true);
    m_keyWidget->installEventFilter(this);
}

// ------------------------------------------------------------- actions

bool PDFAppManageToolGUI::isActionEnabled(const QString& name) const
{
    using Helper = PDFAppManageToolGUIHelper;

    // F6 / Shift+F6 (405:31588) only move the focus: also while the tool runs
    if (name == QLatin1String("focusNextPart") || name == QLatin1String("focusPreviousPart"))
    {
        return true;
    }
    if (!isEditable())
    {
        return false;
    }
    const bool hasItems = !m_assembly.isEmpty();
    const bool hasSelection = !getSelection().empty();
    static const QStringList selectionActions = { QStringLiteral("rotateLeft"), QStringLiteral("rotateRight"), QStringLiteral("remove"), QStringLiteral("clone"),
                                                  QStringLiteral("cut"), QStringLiteral("copy"), QStringLiteral("replace"), QStringLiteral("resetRotation"),
                                                  QStringLiteral("crop"), QStringLiteral("properties"), QStringLiteral("moveTo"), QStringLiteral("group") };
    static const QStringList itemActions = { QStringLiteral("selectAll"), QStringLiteral("selectNone"), QStringLiteral("selectInvert"), QStringLiteral("selectRange"),
                                             QStringLiteral("selectOdd"), QStringLiteral("selectEven"), QStringLiteral("selectPortrait"), QStringLiteral("selectLandscape"),
                                             QStringLiteral("selectSearch"), QStringLiteral("select"), QStringLiteral("sort"), QStringLiteral("more") };
    if (selectionActions.contains(name))
    {
        return hasSelection;
    }
    if (itemActions.contains(name) || name.startsWith(QStringLiteral("sort")) || name.startsWith(QStringLiteral("regroup")))
    {
        return hasItems;
    }
    if (name == QLatin1String("ungroup"))
    {
        for (const PDFDocumentAssembly::Item* item : Helper::getSelectedItems(m_assembly, getSelection()))
        {
            if (item->isGroup())
            {
                return true;
            }
        }
        return false;
    }
    if (name == QLatin1String("rename"))
    {
        return getSelection().size() == 1 && !isDocumentHost();
    }
    if (name == QLatin1String("restoreAll"))
    {
        return !m_assembly.getRemovedItems().empty();
    }
    if (name == QLatin1String("paste"))
    {
        const QClipboard* clipboard = QGuiApplication::clipboard();
        const QMimeData* mimeData = clipboard ? clipboard->mimeData() : nullptr;
        return mimeData && (mimeData->hasFormat(QString::fromLatin1(CLIPBOARD_MIME_TYPE)) || mimeData->hasImage());
    }
    if (name == QLatin1String("assemblySave") || name == QLatin1String("assemblySaveAs"))
    {
        return hasItems || !m_assembly.getRemovedItems().empty();
    }
    return true;
}

void PDFAppManageToolGUI::runAction(const QString& name)
{
    using Mode = PDFDocumentAssembly::RegroupMode;
    using Key = PDFDocumentAssembly::SortKey;

    if (!isActionEnabled(name))
    {
        return;
    }

    // The slots of the actions (one of them may delete this GUI: the last call)
    struct SimpleAction
    {
        const char* name;
        void (PDFAppManageToolGUI::*slot)();
    };
    static const SimpleAction simpleActions[] = {
        { "assemblyOpen", &PDFAppManageToolGUI::openAssemblyFromDialog },
        { "addFiles", &PDFAppManageToolGUI::addFilesFromDialog },
        { "addPages", &PDFAppManageToolGUI::addPagesFromDialog },
        { "addBlank", &PDFAppManageToolGUI::insertBlankPage },
        { "addPaste", &PDFAppManageToolGUI::pasteImage },
        { "selectAll", &PDFAppManageToolGUI::selectAll },
        { "selectNone", &PDFAppManageToolGUI::selectNone },
        { "selectInvert", &PDFAppManageToolGUI::selectInvert },
        { "selectRange", &PDFAppManageToolGUI::openRangeDialog },
        { "selectOdd", &PDFAppManageToolGUI::selectOdd },
        { "selectEven", &PDFAppManageToolGUI::selectEven },
        { "selectPortrait", &PDFAppManageToolGUI::selectPortrait },
        { "selectLandscape", &PDFAppManageToolGUI::selectLandscape },
        { "selectSearch", &PDFAppManageToolGUI::selectSearchResults },
        { "group", &PDFAppManageToolGUI::group },
        { "ungroup", &PDFAppManageToolGUI::ungroup },
        { "regroupEvery", &PDFAppManageToolGUI::openEveryDialog },
        { "regroupNumbers", &PDFAppManageToolGUI::openNumbersDialog },
        { "regroupBookmarks", &PDFAppManageToolGUI::openBookmarksDialog },
        { "rotateLeft", &PDFAppManageToolGUI::rotateLeft },
        { "rotateRight", &PDFAppManageToolGUI::rotateRight },
        { "remove", &PDFAppManageToolGUI::removeSelection },
        { "clone", &PDFAppManageToolGUI::cloneSelection },
        { "cut", &PDFAppManageToolGUI::cut },
        { "copy", &PDFAppManageToolGUI::copy },
        { "paste", &PDFAppManageToolGUI::paste },
        { "replace", &PDFAppManageToolGUI::openReplaceDialog },
        { "resetRotation", &PDFAppManageToolGUI::resetRotation },
        { "crop", &PDFAppManageToolGUI::openCropDialog },
        { "properties", &PDFAppManageToolGUI::openPropertiesDialog },
        { "rename", &PDFAppManageToolGUI::openRenameDialog },
        { "moveTo", &PDFAppManageToolGUI::openMoveDialog },
        { "restoreAll", &PDFAppManageToolGUI::restoreAll },
        { "openAsAssembly", &PDFAppManageToolGUI::openAsAssembly },
        { "focusNextPart", &PDFAppManageToolGUI::focusNextPart },
        { "focusPreviousPart", &PDFAppManageToolGUI::focusPreviousPart },
    };
    for (const SimpleAction& action : simpleActions)
    {
        if (name == QLatin1String(action.name))
        {
            (this->*action.slot)();
            return;
        }
    }

    struct RegroupAction
    {
        const char* name;
        Mode mode;
    };
    static const RegroupAction regroupActions[] = {
        { "regroupOddEven", Mode::OddAndEven },
        { "regroupPairs", Mode::PagePairs },
        { "regroupAlternate", Mode::AlternateTwoFiles },
        { "regroupAlternateReversed", Mode::AlternateSecondReversed },
        { "regroupReverse", Mode::ReverseOrder },
    };
    for (const RegroupAction& action : regroupActions)
    {
        if (name == QLatin1String(action.name))
        {
            regroup(action.mode, PDFDocumentAssembly::RegroupSettings());
            return;
        }
    }

    struct SortAction
    {
        const char* name;
        Key key;
    };
    static const SortAction sortActions[] = {
        { "sortName", Key::FileName },
        { "sortSource", Key::Source },
        { "sortPage", Key::PageNumber },
        { "sortType", Key::Type },
        { "sortReverse", Key::Reverse },
    };
    for (const SortAction& action : sortActions)
    {
        if (name == QLatin1String(action.name))
        {
            sort(action.key);
            return;
        }
    }

    if (name == QLatin1String("assemblySave"))
    {
        saveAssembly();
    }
    else if (name == QLatin1String("assemblySaveAs"))
    {
        saveAssemblyAs();
    }
    else if (name == QLatin1String("titles"))
    {
        setDocumentTitlesShown(!m_areDocumentTitlesShown);
    }
    else if (name == QLatin1String("more"))
    {
        // Alt+M opens the panel More (405:31588)
        QToolButton* button = getAction(QStringLiteral("more"));
        if (button)
        {
            button->click();
        }
    }
    else if (name == QLatin1String("tilesLarger") || name == QLatin1String("tilesSmaller"))
    {
        const int size = qBound(int(PDFAppToolPageGrid::TileSize::Small), int(m_tileSize) + (name == QLatin1String("tilesLarger") ? 1 : -1), int(PDFAppToolPageGrid::TileSize::Large));
        onTileSizeChosen(PDFAppToolPageGrid::TileSize(size));
    }
    else if (name.startsWith(QStringLiteral("output-")))
    {
        setOutput(name.mid(7));
    }
    else if (name.startsWith(QStringLiteral("outline-")))
    {
        setOutline(name.mid(8));
    }
    else if (name.startsWith(QStringLiteral("images-")))
    {
        setImages(name.mid(7));
    }
}

void PDFAppManageToolGUI::connectRows(const std::vector<PDFTileRow*>& rows)
{
    for (PDFTileRow* row : rows)
    {
        connect(row, &PDFTileRow::clicked, this, &PDFAppManageToolGUI::onPanelRowClicked);
        m_panelRows[row->property(PDFAppManageToolGUIHelper::ACTION_KEY).toString()] = row;
    }
}

void PDFAppManageToolGUI::closePanels()
{
    for (const auto& [name, panel] : m_panels)
    {
        Q_UNUSED(name);
        if (panel && panel->isVisible())
        {
            panel->close();
        }
    }
}

void PDFAppManageToolGUI::onPanelRowClicked()
{
    QObject* row = sender();
    const QString name = row ? row->property(PDFAppManageToolGUIHelper::ACTION_KEY).toString() : QString();

    // The panel closes first (it may run code of the application), then the action runs
    QPointer<PDFAppManageToolGUI> guard(this);
    closePanels();
    if (guard && !name.isEmpty())
    {
        runAction(name);
    }
}

void PDFAppManageToolGUI::onKeyAction()
{
    QObject* source = sender();
    const QString name = source ? source->property(PDFAppManageToolGUIHelper::ACTION_KEY).toString() : QString();
    if (!name.isEmpty())
    {
        runAction(name);
    }
}

void PDFAppManageToolGUI::onContextAction()
{
    QObject* source = sender();
    const QString name = source ? source->property(PDFAppManageToolGUIHelper::ACTION_KEY).toString() : QString();
    if (!name.isEmpty())
    {
        runAction(name);
    }
}

void PDFAppManageToolGUI::onOpenDocumentRowClicked()
{
    QObject* row = sender();
    const QString documentId = row ? row->property(PDFAppManageToolGUIHelper::ACTION_KEY).toString() : QString();
    QPointer<PDFAppManageToolGUI> guard(this);
    closePanels();
    if (guard && !documentId.isEmpty())
    {
        addOpenDocument(documentId);
    }
}

void PDFAppManageToolGUI::onRecentRowClicked()
{
    QObject* row = sender();
    const QString filePath = row ? row->property(PDFAppManageToolGUIHelper::ACTION_KEY).toString() : QString();
    QPointer<PDFAppManageToolGUI> guard(this);
    closePanels();
    if (guard && !filePath.isEmpty())
    {
        openAssemblyFile(filePath);
    }
}

void PDFAppManageToolGUI::onCanvasContextMenu(const QPoint& globalPosition)
{
    using Helper = PDFAppManageToolGUIHelper;

    if (!isEditable() || !m_canvas)
    {
        return;
    }
    if (m_contextMenu)
    {
        m_contextMenu->close();
    }

    // 387:14220: the context menu of the pages (new + QPointer, deleted when it closes)
    QMenu* menu = new QMenu(m_canvas);
    menu->setObjectName(QStringLiteral("manageContextMenu"));
    menu->setAttribute(Qt::WA_DeleteOnClose);
    struct ContextItem
    {
        const char* action;
        QString text;
        bool isSeparatorBefore = false;
    };
    std::vector<ContextItem> items = {
        { "cut", tr("Cut") },
        { "copy", tr("Copy") },
        { "paste", tr("Paste") },
        { "rotateLeft", tr("Rotate left"), true },
        { "rotateRight", tr("Rotate right") },
        { "resetRotation", tr("Reset rotation") },
    };
    if (!isDocumentHost())
    {
        items.push_back({ "group", tr("Group"), true });
        items.push_back({ "ungroup", tr("Ungroup") });
        items.push_back({ "rename", tr("Rename…") });
    }
    items.push_back({ "clone", tr("Clone"), isDocumentHost() });
    items.push_back({ "moveTo", tr("Move to…") });
    items.push_back({ "replace", tr("Replace…") });
    items.push_back({ "crop", tr("Crop…") });
    items.push_back({ "remove", tr("Remove"), true });
    items.push_back({ "properties", tr("Properties") });
    for (const ContextItem& item : items)
    {
        if (item.isSeparatorBefore)
        {
            menu->addSeparator();
        }
        const QString name = QString::fromLatin1(item.action);
        QAction* action = menu->addAction(item.text);
        action->setObjectName(QStringLiteral("manageContext-") + name);
        action->setProperty(Helper::ACTION_KEY, name);
        action->setShortcut(Helper::getKeys(name));
        action->setShortcutVisibleInContextMenu(true);
        action->setEnabled(isActionEnabled(name));
        connect(action, &QAction::triggered, this, &PDFAppManageToolGUI::onContextAction);
    }
    m_contextMenu = menu;

    // Last: the menu runs from the event loop
    menu->popup(globalPosition);
}

void PDFAppManageToolGUI::showDialog(PDFAppManageDialog* dialog)
{
    if (m_dialog)
    {
        // One dialog at a time
        dialog->deleteLater();
        m_dialog->raise();
        return;
    }
    m_dialog = dialog;

    // Last: the dialog is shown from the event loop (open, never exec)
    dialog->open();
}

QWidget* PDFAppManageToolGUI::getWindow() const
{
    if (m_canvas)
    {
        return m_canvas->window();
    }
    return getHost() && getHost()->getKeyWidget() ? getHost()->getKeyWidget()->window() : nullptr;
}

QWidget* PDFAppManageToolGUI::getScrimTarget() const
{
    // Owner answer 6 of 12. 11. (387:15926: the scrim covers the window below its top bar): the
    // dialog host of the window, as the close question and every dialog of the window
    // (PDFAppMainWindow::prepareDialog); outside of a window the canvas area
    if (QWidget* host = PDFAppMainWindow::findDialogHost(getWindow()))
    {
        return host;
    }
    return getHost() ? getHost()->getCanvasArea() : nullptr;
}

void PDFAppManageToolGUI::focusNextPart()
{
    focusPart(1);
}

void PDFAppManageToolGUI::focusPreviousPart()
{
    focusPart(-1);
}

QWidget* PDFAppManageToolGUI::getPart(int index) const
{
    // The parts in the order of the reading (405:31588 "F6 moves between toolbar, panels and
    // pages"): the actions of the toolbar, the left panel, the pages (Details, or the canvas with
    // its empty state), the right panel
    switch (index)
    {
        case 0:
            for (const auto& [name, button] : m_actions)
            {
                Q_UNUSED(name);
                if (button)
                {
                    return button->parentWidget();
                }
            }
            return nullptr;
        case 1:
            return m_sourcesPanel.data();
        case 2:
        {
            QWidget* view = m_canvas ? (m_isDetailsShown ? static_cast<QWidget*>(m_canvas->getDetailsView()) : static_cast<QWidget*>(m_canvas->getPageView())) : nullptr;
            return view && view->isVisible() ? view : m_canvas.data();
        }
        case 3:
            return m_resultPanel.data();
        default:
            break;
    }
    return nullptr;
}

void PDFAppManageToolGUI::focusPart(int delta)
{
    // The part of the focus now; from outside of the parts F6 starts at the toolbar
    const int count = PART_COUNT;
    const QWidget* focus = QApplication::focusWidget();
    int current = delta > 0 ? -1 : count;
    for (int i = 0; i < count; ++i)
    {
        const QWidget* part = getPart(i);
        if (part && focus && (part == focus || part->isAncestorOf(focus)))
        {
            current = i;
            break;
        }
    }

    // The next part, which has a widget to focus (a hidden or empty part is skipped)
    for (int step = 1; step <= count; ++step)
    {
        const int index = ((current + step * delta) % count + count) % count;
        if (QWidget* target = PDFAppManageToolGUIHelper::findFocusTarget(getPart(index)))
        {
            target->setFocus(Qt::TabFocusReason);
            return;
        }
    }
}

void PDFAppManageToolGUI::showMessage(const QString& text)
{
    if (getHost() && !text.isEmpty())
    {
        getHost()->showTransientMessage(text);
    }
}

bool PDFAppManageToolGUI::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_keyWidget)
    {
        switch (event->type())
        {
            case QEvent::DragEnter:
            case QEvent::DragMove:
            {
                QDropEvent* dropEvent = static_cast<QDropEvent*>(event);
                if (isEditable() && PDFAppFileDrop::hasLocalFiles(dropEvent->mimeData()))
                {
                    dropEvent->acceptProposedAction();
                    return true;
                }
                break;
            }

            case QEvent::Drop:
            {
                QDropEvent* dropEvent = static_cast<QDropEvent*>(event);
                if (isEditable() && PDFAppFileDrop::hasLocalFiles(dropEvent->mimeData()))
                {
                    // The files are added from the event handler, the last thing
                    dropEvent->acceptProposedAction();
                    dropFiles(dropEvent->mimeData());
                    return true;
                }
                break;
            }

            default:
                break;
        }
    }
    return BaseClass::eventFilter(watched, event);
}

// ------------------------------------------------------------- slots

void PDFAppManageToolGUI::onListInputsChanged()
{
    if (!m_isChangingList)
    {
        queueProcessPending();
    }
}

void PDFAppManageToolGUI::onListInputChanged(const QString& inputId)
{
    Q_UNUSED(inputId);
    if (!m_isChangingList)
    {
        queueProcessPending();
    }
}

void PDFAppManageToolGUI::onListDestroyed()
{
    m_list = nullptr;
}

void PDFAppManageToolGUI::onShellStateChanged(PDFDocumentToolShell::State state)
{
    m_isDeciding = state == PDFDocumentToolShell::State::Deciding;
    updateAll();
}

void PDFAppManageToolGUI::onRunStateChanged(bool isRunning)
{
    m_isRunning = isRunning;
    updateAll();
}

void PDFAppManageToolGUI::onCanvasSelectionChanged()
{
    updateAll();
}

void PDFAppManageToolGUI::onCanvasMoveRequested(const PDFDocumentAssembly::Selection& selection, const QString& beforeItemId)
{
    PDFDocumentAssembly::Edit edit;
    const PDFDocumentAssembly result = PDFDocumentAssembly::move(m_assembly, selection, beforeItemId, &edit);
    applyEdit(result, edit, tr("Move"));
}

void PDFAppManageToolGUI::onCanvasSearchChanged(const QString& text)
{
    Q_UNUSED(text);
    updateAll();
}

void PDFAppManageToolGUI::onCanvasViewSwitched(bool isDetailsShown)
{
    m_isDetailsShown = isDetailsShown;
    updateAll();
}

void PDFAppManageToolGUI::onSourceClicked(const QString& sourceId)
{
    // The items of the source are selected (its pages come into the view)
    const int index = PDFAppManageToolGUIHelper::findSource(m_assembly, sourceId);
    if (index < 0)
    {
        return;
    }
    PDFDocumentAssembly::Selection selection;
    for (const PDFDocumentAssembly::Item& item : m_assembly.getItems())
    {
        for (const PDFDocumentAssembly::Page& page : item.pages)
        {
            if (page.plan.input == index)
            {
                selection.push_back(item.id);
                break;
            }
        }
    }
    setSelection(selection);
}

void PDFAppManageToolGUI::onSourceLinkActivated(const QString& sourceId)
{
    using Helper = PDFAppManageToolGUIHelper;

    // The row of an input, which is no source yet: Enter password… / Remove file
    QString inputId = sourceId.startsWith(QLatin1String(Helper::INPUT_PREFIX)) ? sourceId.mid(int(qstrlen(Helper::INPUT_PREFIX))) : getInputOfSource(sourceId);
    const PDFAppToolInput* input = m_list ? m_list->findInput(inputId) : nullptr;
    if (!input)
    {
        return;
    }
    if (input->state == PDFAppToolInputState::PasswordRequired || input->state == PDFAppToolInputState::Restricted)
    {
        openPasswordDialog(inputId);
    }
    else
    {
        onPasswordRemoveRequested(inputId);
    }
}

void PDFAppManageToolGUI::onProgressTimer()
{
    if (!m_list || m_list->getLoadingCount() == 0)
    {
        if (m_progressTimer)
        {
            m_progressTimer->stop();
        }
        return;
    }
    updateSources();
}

void PDFAppManageToolGUI::onTileSizeButtonClicked()
{
    QToolButton* button = m_canvas ? m_canvas->getTileSizeButton() : nullptr;
    PDFAppToolPanelPopup* panel = m_tileSizeMenu ? m_tileSizeMenu->getPanel() : nullptr;
    if (!button || !panel)
    {
        return;
    }
    if (panel->isVisible())
    {
        panel->close();
        return;
    }
    button->setChecked(true);
    panel->popup(button, button->mapToGlobal(QPoint(0, button->height())).y() + PDFAppToolToolbar::PANEL_GAP);
}

void PDFAppManageToolGUI::onTileSizePanelClosed()
{
    if (m_canvas && m_canvas->getTileSizeButton())
    {
        m_canvas->getTileSizeButton()->setChecked(false);
    }
}

void PDFAppManageToolGUI::onTileSizeChosen(PDFAppToolPageGrid::TileSize size)
{
    m_tileSize = size;
    if (m_tileSizeMenu)
    {
        m_tileSizeMenu->setSize(size);
    }
    if (m_canvas)
    {
        m_canvas->setTileSize(size);
    }

    // The store announces the change of the setting to its readers (the last call)
    PDFAppToolTileSizeMenu::storeSize(getContext(), size);
}

void PDFAppManageToolGUI::onRegistryChanged()
{
    QPointer<PDFAppManageToolGUI> guard(this);
    updateOpenDocumentRows();
    if (guard)
    {
        updateEmptyState();
    }
}

void PDFAppManageToolGUI::onEmptyStateAddFiles()
{
    addFilesFromDialog();
}

void PDFAppManageToolGUI::onEmptyStateAddDocument(const QString& documentId)
{
    // A copy: the change of the sources makes the empty state again (the row of the signal is
    // deleted later, its id would go with it)
    const QString id = documentId;
    if (!id.isEmpty())
    {
        addOpenDocument(id);
    }
}

void PDFAppManageToolGUI::onEmptyStateOpenAssembly()
{
    openAssemblyFromDialog();
}

void PDFAppManageToolGUI::onEmptyStateOpenRecent(const QString& filePath)
{
    openAssemblyFile(filePath);
}

void PDFAppManageToolGUI::onParametersChanged(const QString& name)
{
    BaseClass::onParametersChanged(name);
    if (name == QLatin1String(PDFDocumentToolManage::PARAMETER_ASSEMBLY) || name == QLatin1String(PDFDocumentToolManage::PARAMETER_INPUTS))
    {
        readValues();
        return;
    }
    updateAll();
}

void PDFAppManageToolGUI::onDocumentChanged()
{
    BaseClass::onDocumentChanged();
    QPointer<PDFAppManageToolGUI> guard(this);
    updateDocumentMapping();
    if (guard)
    {
        readValues();
    }
}

void PDFAppManageToolGUI::retranslate()
{
    BaseClass::retranslate();
}

// ------------------------------------------------------------- texts

QString PDFAppManageToolGUI::getConfirmText() const
{
    if (isDocumentHost())
    {
        // 1479:34632 "Rearrange Pages" (RB11 point 4)
        return tr("Rearrange Pages");
    }

    // 387:12569 "Create PDF", 396:26763 "Create 10 PDFs"
    const QString output = getOutput();
    if (output == QLatin1String(PDFDocumentToolManage::OUTPUT_ONE_PDF))
    {
        return tr("Create PDF");
    }
    const int count = getOutputFileCount();
    return count == 1 ? tr("Create 1 PDF") : tr("Create %Ln PDFs", nullptr, count);
}

QString PDFAppManageToolGUI::getConfirmIconName() const
{
    return isDocumentHost() ? QStringLiteral("page-moved") : QStringLiteral("file-plus");
}

bool PDFAppManageToolGUI::isConfirmEnabled() const
{
    // A page in the result, every source read, the document of the tab may be changed (1479:35631)
    if (!BaseClass::isConfirmEnabled() || !isEditable() || m_assembly.isEmpty() || !m_pending.empty())
    {
        return false;
    }
    if (isDocumentHost())
    {
        return isDocumentChangeAllowed();
    }
    return true;
}

QString PDFAppManageToolGUI::getStatusText() const
{
    using Helper = PDFAppManageToolGUIHelper;

    const PDFDocumentToolCatalogEntry* tool = getTool();
    const QString title = tool ? tool->getTitle() : tr("Manage Pages");
    const PDFDocumentAssembly::Summary summary = PDFDocumentAssembly::getSummary(m_assembly);

    // 1479:34632 the change of the document (RB2), the limit of the pages (owner 2026-11-12),
    // 1479:35631 the lock
    const QString limitText = getPageLimitText();
    if (!limitText.isEmpty())
    {
        return tr("%1 · %2").arg(title, limitText);
    }
    if (!m_statusNote.isEmpty())
    {
        return tr("%1 · %2").arg(title, m_statusNote);
    }
    const QString lockText = getLockText();
    if (!lockText.isEmpty())
    {
        return tr("%1 · %2").arg(title, lockText);
    }
    if (m_assembly.isEmpty() && !isDocumentHost())
    {
        // 409:32739
        return tr("%1 · add files to start a new assembly, or open a saved one").arg(title);
    }
    if (m_canvas && m_canvas->isFiltered())
    {
        // 387:15594
        return tr("%1 · %2 of %3 match \"%4\" · Esc clears the search").arg(title, PDFAppLocale::formatNumber(m_canvas->getShownItemCount()), Helper::items(summary.itemCount), m_canvas->getSearchText());
    }
    const QString output = getOutput();
    if (!isDocumentHost() && output != QLatin1String(PDFDocumentToolManage::OUTPUT_ONE_PDF))
    {
        // 396:26763 "one PDF per group or item · 10 files in C:\Tender\Parts"
        const QString kind = output == QLatin1String(PDFDocumentToolManage::OUTPUT_PER_PAGE) ? tr("one PDF per page") : tr("one PDF per group or item");
        const QString folder = m_outputFiles ? m_outputFiles->getFolder() : QString();
        return folder.isEmpty() ? tr("%1 · %2 · %3").arg(title, kind, Helper::files(getOutputFileCount()))
                                : tr("%1 · %2 · %3 in %4").arg(title, kind, Helper::files(getOutputFileCount()), QDir::toNativeSeparators(folder));
    }

    // 387:12569 "Manage Pages · 3 sources · 10 items · 17 pages · 2 pages selected"
    QStringList parts{ title };
    parts << (summary.sourceCount == 1 ? tr("1 source") : tr("%Ln sources", nullptr, summary.sourceCount));
    parts << Helper::items(summary.itemCount);
    parts << Helper::pages(summary.resultPageCount);
    const int selected = Helper::toInt(Helper::getSelectionPageCount(m_assembly, getSelection()));
    if (selected > 0)
    {
        parts << (selected == 1 ? tr("1 page selected") : tr("%Ln pages selected", nullptr, selected));
    }
    return parts.join(QStringLiteral(" · "));
}

QString PDFAppManageToolGUI::getRunningStatusText(const QString& stepText) const
{
    Q_UNUSED(stepText);
    if (isDocumentHost())
    {
        return tr("Rearranging the pages · Stop ends the operation, the document is not changed");
    }
    return tr("Creating the PDF · Stop ends the operation, the source files are never changed");
}

QString PDFAppManageToolGUI::getRunningCardTitle(int percentage, const QString& stepText) const
{
    Q_UNUSED(percentage);
    Q_UNUSED(stepText);
    return isDocumentHost() ? tr("Rearranging the pages") : tr("Creating the PDF");
}

void PDFAppManageToolGUI::fillResultInput(PDFAppToolResultInput* input) const
{
    if (isDocumentHost())
    {
        // R1 of Rotate with the texts of Manage (QB2: no frame)
        input->title = tr("Pages rearranged");
        return;
    }
    const QString output = getOutput();
    if (output == QLatin1String(PDFDocumentToolManage::OUTPUT_ONE_PDF))
    {
        // 387:17762
        input->title = tr("PDF created");
        return;
    }

    // 396:27563 "10 PDFs created · The source files were not changed. The assembly stays open."
    const int count = getOutputFileCount();
    input->title = count == 1 ? tr("1 PDF created") : tr("%Ln PDFs created", nullptr, count);
    input->subtitle = tr("The source files were not changed. The assembly stays open.");
}

QString PDFAppManageToolGUI::getOwnTabTitle() const
{
    // 387:12569 "Tender_2026", 409:32739 "New assembly"
    if (!m_assemblyPath.isEmpty())
    {
        return QFileInfo(m_assemblyPath).completeBaseName();
    }
    return tr("New assembly");
}

QString PDFAppManageToolGUI::getOwnTabLossText() const
{
    // The dot of the tab (387:12569); the close asks askOwnTabClose (405:30894)
    return isAssemblyModified() ? tr("The assembly has changes that are not saved.") : QString();
}

QString PDFAppManageToolGUI::getLockedGlanceText() const
{
    return tr("Can be arranged as an assembly");
}

QString PDFAppManageToolGUI::getOwnTabPageText() const
{
    // 387:12569 "17 pages", 409:32739 "No pages"
    const qint64 count = PDFAppManageToolGUIHelper::getPageCount(m_assembly.getItems());
    return count > 0 ? PDFAppManageToolGUIHelper::pages(count) : tr("No pages");
}

QString PDFAppManageToolGUI::getOwnTabZoomText() const
{
    // K12 of Merge: the tiles have no zoom of a document
    return QString();
}

PDFAppToolCloseAnswer PDFAppManageToolGUI::askOwnTabClose()
{
    if (!isAssemblyModified())
    {
        // Nothing is lost (an empty or a saved assembly)
        return PDFAppToolCloseAnswer::Close;
    }
    if (m_closeDialog)
    {
        return PDFAppToolCloseAnswer::Cancel;
    }

    // 405:30894: "Save the assembly Tender_2026?" Don't Save / Cancel / Save Assembly - the choice
    // dialog of the questions of a tab (Don't Save closes the tab: the name of the close of the
    // generic question). It belongs to the tab (the canvas area is its parent: it goes with the
    // tab or the window) and runs its own event loop - this GUI may be deleted meanwhile
    QWidget* parent = getHost() && getHost()->getCanvasArea() ? getHost()->getCanvasArea() : getWindow();
    PDFAppDocumentChoiceDialog* dialog = new PDFAppDocumentChoiceDialog(PDFThemeIcons::Badge::Warning, tr("Save the assembly %1?").arg(getOwnTabTitle().left(256)),
                                                                        tr("It has changes that are not saved. An assembly keeps the sources, the order, the groups and the checkpoints – it is not a PDF."),
                                                                        480, parent);
    dialog->setObjectName(QStringLiteral("manageCloseDialog"));
    PDFAppMainWindow::prepareDialog(dialog, parent);
    dialog->addChoice(tr("Don't Save"), PDFAppDocumentChoice::Discard, false, true, QStringLiteral("closeTabButton"));
    dialog->addChoice(tr("Cancel"), PDFAppDocumentChoice::Cancel, false, false, QStringLiteral("cancelButton"));
    dialog->addChoice(tr("Save Assembly"), PDFAppDocumentChoice::Save, true, false, QStringLiteral("manageCloseSave"));
    m_closeDialog = dialog;

    QPointer<PDFAppManageToolGUI> guard(this);
    const PDFAppDocumentChoice choice = PDFAppDocumentChoiceDialog::run(dialog);
    if (!guard)
    {
        return PDFAppToolCloseAnswer::Cancel;
    }
    m_closeDialog = nullptr;
    switch (choice)
    {
        case PDFAppDocumentChoice::Discard:
            return PDFAppToolCloseAnswer::Close;
        case PDFAppDocumentChoice::Save:
            // The save is synchronous: Close after a saved assembly, else the tab stays
            return saveAssembly() ? PDFAppToolCloseAnswer::Close : PDFAppToolCloseAnswer::Cancel;
        default:
            break;
    }
    return PDFAppToolCloseAnswer::Cancel;
}

// ------------------------------------------------------------- flow

QVariantMap PDFAppManageToolGUI::getRunParameters() const
{
    using Helper = PDFAppManageToolGUIHelper;

    // The run gets the PDF sources of the pages of the result by the ids of the sources (the
    // context documents carry them as their input ids, prepareEnvironment); the target of the tab
    // of a document is the document of the context
    QVariantMap parameters = BaseClass::getRunParameters();
    std::set<int> usedSources;
    for (const PDFDocumentAssembly::Item& item : m_assembly.getItems())
    {
        for (const PDFDocumentAssembly::Page& page : item.pages)
        {
            usedSources.insert(page.plan.input);
        }
    }
    QStringList ids;
    const std::vector<PDFDocumentAssembly::Source>& sources = m_assembly.getSources();
    for (size_t i = 0; i < sources.size(); ++i)
    {
        if (usedSources.count(int(i)) && !sources[i].isTarget)
        {
            ids << sources[i].id;
        }
    }
    parameters.insert(Helper::name(PDFDocumentToolManage::PARAMETER_INPUTS), ids.join(QLatin1Char(',')));
    parameters.insert(Helper::name(PDFDocumentToolManage::PARAMETER_OUTPUT), getOutput());
    return parameters;
}

void PDFAppManageToolGUI::prepareEnvironment(PDFAppToolRunEnvironment* environment) const
{
    using Helper = PDFAppManageToolGUIHelper;

    // The documents and the images of the sources of the run (getRunParameters), each with the id
    // of its source; the documents are immutable shared pointers, the bytes implicitly shared
    const QStringList ids = Helper::splitIds(getRunParameters().value(Helper::name(PDFDocumentToolManage::PARAMETER_INPUTS)).toString());
    for (const QString& sourceId : ids)
    {
        const PDFAppToolInput* input = m_list ? m_list->findInput(getInputOfSource(sourceId)) : nullptr;
        if (!input || !input->isUsable())
        {
            continue;
        }
        if (input->isImage())
        {
            PDFDocumentToolInputImage image = input->createToolInputImage();
            image.inputId = sourceId;
            environment->images.push_back(std::move(image));
        }
        else
        {
            PDFDocumentToolInputDocument document = input->createToolInputDocument();
            document.inputId = sourceId;
            environment->additionalDocuments.push_back(std::move(document));
        }
    }
    if (PDFDocumentToolRunner* runner = getRunner(); runner && m_list && isDocumentHost())
    {
        // K33: the list of the tab of a document is no list of the host
        runner->setInputGeneration(m_list->getGeneration());
    }
}

// ------------------------------------------------------------- update of the parts

bool PDFAppManageToolGUI::isEditable() const
{
    // A document over the pages of an assembly is not arranged (owner 2026-11-12)
    return !m_isRunning && !m_isDeciding && getPageLimitText().isEmpty();
}

void PDFAppManageToolGUI::updateAll()
{
    // The parts announce their changes, whose slots may delete this GUI: each part only by a
    // living GUI
    QPointer<PDFAppManageToolGUI> guard(this);
    updateOpenDocumentRows();
    if (guard)
    {
        updateRecentRows();
    }
    if (guard)
    {
        updateSources();
    }
    if (guard)
    {
        updateCanvas();
    }
    if (guard)
    {
        updateRightPanel();
    }
    if (guard)
    {
        updateToolbar();
    }
    if (guard)
    {
        updateEditable();
    }
    if (!guard)
    {
        return;
    }
    if (m_list && m_list->getLoadingCount() > 0 && m_progressTimer && !m_progressTimer->isActive())
    {
        m_progressTimer->start();
    }

    // The slots of the Primary button, the status and the tab may delete this GUI: the last things
    updateConfirmState();
    if (guard)
    {
        updateStatusText();
    }
    if (guard)
    {
        updateOwnTabState();
    }
}

void PDFAppManageToolGUI::updateCanvas()
{
    using Helper = PDFAppManageToolGUIHelper;

    if (!m_canvas)
    {
        return;
    }
    const PDFDocumentAssembly::Summary summary = PDFDocumentAssembly::getSummary(m_assembly);
    if (m_canvas->isFiltered())
    {
        // 387:15594 "2 of 10 items"
        m_canvas->setHeaderText(tr("%1 of %2").arg(PDFAppLocale::formatNumber(m_canvas->getShownItemCount()), Helper::items(summary.itemCount)));
    }
    else
    {
        // 387:12785 "10 items · 17 pages"
        m_canvas->setHeaderText(tr("%1 · %2").arg(Helper::items(summary.itemCount), Helper::pages(summary.resultPageCount)));
    }
    updateEmptyState();
}

void PDFAppManageToolGUI::updateSources()
{
    using Helper = PDFAppManageToolGUIHelper;

    if (!m_sourcesPanel)
    {
        return;
    }

    // Sources (387:12723): a row of each source, then the inputs being read
    const PDFDocumentAssembly::Summary summary = PDFDocumentAssembly::getSummary(m_assembly);
    std::vector<qint64> usedPages(m_assembly.getSources().size(), 0);
    std::vector<std::set<PDFInteger>> distinctPages(m_assembly.getSources().size());
    for (const PDFDocumentAssembly::Item& item : m_assembly.getItems())
    {
        for (const PDFDocumentAssembly::Page& page : item.pages)
        {
            if (page.plan.input >= 0 && size_t(page.plan.input) < usedPages.size())
            {
                ++usedPages[size_t(page.plan.input)];
                distinctPages[size_t(page.plan.input)].insert(page.plan.pageIndex);
            }
        }
    }
    std::vector<PDFAppToolInputRow::Content> rows;
    const std::vector<PDFDocumentAssembly::Source>& sources = m_assembly.getSources();
    int shownIndex = 0;
    for (size_t i = 0; i < sources.size(); ++i)
    {
        const PDFDocumentAssembly::Source& source = sources[i];
        if (source.isParked)
        {
            continue;
        }
        const PDFAppToolInput* input = m_list ? m_list->findInput(getInputOfSource(source.id)) : nullptr;
        PDFAppToolInputRow::Content content;
        content.inputId = source.id;
        content.letter = PDFAppToolInputList::getLetter(shownIndex);
        content.hue = PDFAppToolInputList::getHue(shownIndex, input);
        content.name = getSourceDisplayName(int(i));
        content.toolTip = source.path.isEmpty() ? source.name : QDir::toNativeSeparators(source.path);
        content.isSingleLine = true;
        ++shownIndex;
        if (source.isTarget)
        {
            // 1479:34632 Tag "this document"
            content.tag = tr("this document");
        }
        else if (input && input->kind == PDFAppToolInputKind::OpenTab)
        {
            content.tag = tr("open tab");
        }
        if (source.kind == PDFDocumentAssembly::SourceKind::Image)
        {
            // 387:12747 "Image · used once"
            const qint64 uses = usedPages[i];
            content.firstLine = uses == 0 ? tr("Image · not used") : (uses == 1 ? tr("Image · used once") : tr("Image · used %Ln times", nullptr, Helper::toInt(uses)));
        }
        else
        {
            // 387:12733 "6 of 6 pages used"
            content.firstLine = tr("%1 of %2 used").arg(PDFAppLocale::formatNumber(qint64(distinctPages[i].size())), Helper::pages(source.pageCount));
        }
        if (input && input->state == PDFAppToolInputState::Loading)
        {
            const int progress = m_list->getLoadingProgress(input->id);
            content.progress = progress >= 0 ? progress / 100.0 : 0.0;
        }
        rows.push_back(content);
    }
    for (const PendingAddition& pending : m_pending)
    {
        const PDFAppToolInput* input = m_list ? m_list->findInput(pending.inputId) : nullptr;
        if (!input || pending.kind == PendingAddition::Kind::Bind)
        {
            continue;
        }
        PDFAppToolInputRow::Content content;
        content.inputId = QString::fromLatin1(Helper::INPUT_PREFIX) + input->id;
        content.letter = PDFAppToolInputList::getLetter(shownIndex++);
        content.name = input->name;
        content.firstLine = PDFAppToolInputList::getStateText(input->state);
        content.isFirstLineProblem = input->needsAttention();
        content.toolTip = input->reason;
        content.isSingleLine = true;
        if (input->state == PDFAppToolInputState::Loading)
        {
            const int progress = m_list->getLoadingProgress(input->id);
            content.progress = progress >= 0 ? progress / 100.0 : 0.0;
        }
        else if (input->state == PDFAppToolInputState::PasswordRequired)
        {
            content.linkText = tr("Enter password…");
        }
        else if (input->state == PDFAppToolInputState::Restricted)
        {
            content.linkText = tr("Enter owner password…");
        }
        else
        {
            content.linkText = tr("Remove file");
        }
        rows.push_back(content);
    }

    // "3 files · 16 of 31 pages used" (387:12726), "No files yet" (409:32739)
    const QString subtitle = summary.sourceCount == 0 && rows.empty() ? tr("No files yet")
                                                                       : tr("%1 · %2 of %3 used").arg(Helper::files(summary.sourceCount), PDFAppLocale::formatNumber(summary.usedPageCount), Helper::pages(summary.sourcePageCount));
    const QString hint = rows.empty() ? tr("Files you add are listed here. Checkpoints and removed pages appear below them while you work.") : QString();
    m_sourcesPanel->setSources(tr("Sources"), subtitle, rows, hint);

    // Checkpoints, the newest first (387:12757: the newest has the accent tile)
    std::vector<PDFAppManageListRow::Content> checkpointRows;
    for (auto it = m_checkpoints.crbegin(); it != m_checkpoints.crend(); ++it)
    {
        PDFAppManageListRow::Content content;
        content.key = it->id;
        content.iconName = QStringLiteral("flag");
        content.isAccent = it == m_checkpoints.crbegin();
        content.title = it->name;
        content.text = tr("%1 · %2").arg(PDFAppLocale::formatClock(it->time.toLocalTime().time()), Helper::pages(Helper::getPageCount(it->items)));
        content.linkText = tr("Restore");
        checkpointRows.push_back(content);
    }
    const bool hasWork = !m_assembly.isEmpty() || !m_assembly.getRemovedItems().empty() || !m_checkpoints.empty();
    m_sourcesPanel->setCheckpoints(checkpointRows, hasWork);

    // Removed pages: "15 pages", "Kept until the assembly is closed" / KB4 "Kept until the tool is closed"
    PDFAppManageListRow::Content removed;
    std::vector<PDFAppManageListRow::Content> removedRows;
    if (!m_assembly.getRemovedItems().empty())
    {
        removed.iconName = QStringLiteral("trash-restore");
        removed.title = Helper::pages(summary.removedPageCount);
        removed.text = isDocumentHost() ? tr("Kept until the tool is closed") : tr("Kept until the assembly is closed");
        removed.linkText = tr("Restore all");
        // The rows of the removed items only while the bin is expanded, the newest first, at most
        // MAXIMUM_REMOVED_ROWS (Restore all brings back the rest; 100 000 items, plan 6)
        const std::vector<PDFDocumentAssembly::RemovedItem>& removedItems = m_assembly.getRemovedItems();
        const size_t rowCount = m_sourcesPanel->isRemovedExpanded() ? qMin(removedItems.size(), size_t(MAXIMUM_REMOVED_ROWS)) : 0;
        for (size_t i = 0; i < rowCount; ++i)
        {
            const PDFDocumentAssembly::RemovedItem& item = removedItems[removedItems.size() - 1 - i];
            PDFAppManageListRow::Content row;
            row.key = item.item.id;
            row.iconName = item.item.isGroup() ? QStringLiteral("group") : QStringLiteral("file");
            row.title = PDFAppManageItemModel::getItemName(m_assembly, item.item);
            row.text = tr("%1 · original %2").arg(Helper::pages(qint64(item.item.pages.size())), PDFAppManageItemModel::getOriginalPages(m_assembly, item.item));
            row.linkText = tr("Restore");
            removedRows.push_back(row);
        }
    }
    m_sourcesPanel->setRemoved(removed, removedRows);
}

std::vector<PDFDocumentToolFileOutput::File> PDFAppManageToolGUI::createOutputFiles() const
{
    // The files of One PDF per group / per page with their names: the rule of the run
    // (PDFDocumentToolManage::createFiles); empty for an invalid template or equal names
    std::vector<PDFDocumentToolFileOutput::File> files;
    QVariantMap values = getModel() ? getModel()->getValues() : QVariantMap();
    values.insert(PDFAppManageToolGUIHelper::name(PDFDocumentToolManage::PARAMETER_OUTPUT), getOutput());
    QString error;
    if (!PDFDocumentToolManage::createFiles(m_assembly, values, QDate::currentDate(), &files, &error))
    {
        files.clear();
    }
    return files;
}

int PDFAppManageToolGUI::getOutputFileCount() const
{
    // 396:26763 "10 files", "17 files"
    const QString output = getOutput();
    if (output == QLatin1String(PDFDocumentToolManage::OUTPUT_PER_PAGE))
    {
        return PDFAppManageToolGUIHelper::toInt(PDFAppManageToolGUIHelper::getPageCount(m_assembly.getItems()));
    }
    if (output == QLatin1String(PDFDocumentToolManage::OUTPUT_PER_GROUP))
    {
        return PDFAppManageToolGUIHelper::toInt(qint64(m_assembly.getItems().size()));
    }
    return 1;
}

QString PDFAppManageToolGUI::getOutputText() const
{
    const QString output = getOutput();
    if (output == QLatin1String(PDFDocumentToolManage::OUTPUT_DOCUMENT))
    {
        return tr("This document");
    }
    if (output == QLatin1String(PDFDocumentToolManage::OUTPUT_PER_GROUP))
    {
        return tr("One per group");
    }
    if (output == QLatin1String(PDFDocumentToolManage::OUTPUT_PER_PAGE))
    {
        return tr("One per page");
    }
    return tr("One PDF");
}

QString PDFAppManageToolGUI::getOutlineText() const
{
    const QString outline = getParameter(PDFDocumentToolManage::PARAMETER_OUTLINE);
    if (outline == QLatin1String(PDFDocumentToolManage::OUTLINE_JOIN))
    {
        return tr("Joined outlines");
    }
    if (outline == QLatin1String(PDFDocumentToolManage::OUTLINE_NONE))
    {
        return tr("No outline");
    }
    return tr("Document parts");
}

QString PDFAppManageToolGUI::getImagesText() const
{
    const QString images = getParameter(PDFDocumentToolManage::PARAMETER_OPTIMIZE_IMAGES);
    if (images == QLatin1String(PDFDocumentToolManage::IMAGES_QUALITY))
    {
        return tr("Prefer quality");
    }
    if (images == QLatin1String(PDFDocumentToolManage::IMAGES_SIZE))
    {
        return tr("Minimum size");
    }
    return tr("Stored as they are");
}

void PDFAppManageToolGUI::updateRightPanel()
{
    using Helper = PDFAppManageToolGUIHelper;

    if (!m_resultPanel)
    {
        return;
    }
    const PDFDocumentAssembly::Summary summary = PDFDocumentAssembly::getSummary(m_assembly);
    const bool isDocument = isDocumentHost();
    const QString output = getOutput();
    PDFAppManageResultPanel::Content content;
    content.isEmpty = m_assembly.isEmpty() && !isDocument && m_assembly.getRemovedItems().empty();

    // section-Selection 387:13234: the Tag "2 pages", Source, Original pages, Size, Orientation, Rotation
    const PDFDocumentAssembly::Selection& selection = getSelection();
    content.hasSelection = !selection.empty();
    if (content.hasSelection)
    {
        qint64 pageCount = 0;
        std::set<int> inputs;
        const std::vector<const PDFDocumentAssembly::Item*> selectedItems = Helper::getSelectedItems(m_assembly, selection);
        const PDFDocumentAssembly::Item* first = selectedItems.empty() ? nullptr : selectedItems.front();
        for (const PDFDocumentAssembly::Item* item : selectedItems)
        {
            pageCount += qint64(item->pages.size());
            for (const PDFDocumentAssembly::Page& page : item->pages)
            {
                inputs.insert(page.plan.input);
            }
        }
        content.selectionTag = Helper::pages(pageCount);
        content.canRename = selection.size() == 1 && !isDocument && isEditable();
        QString source;
        QString original = QStringLiteral("–");
        if (inputs.size() == 1)
        {
            const int input = *inputs.begin();
            source = input >= 0 && size_t(input) < m_assembly.getSources().size() ? getSourceDisplayName(input) : tr("Blank page");
            if (input >= 0 && size_t(input) < m_assembly.getSources().size() && m_assembly.getSources()[size_t(input)].kind == PDFDocumentAssembly::SourceKind::Pdf)
            {
                // The original pages of the selection (1479:34632 "7", 387:13234 "1–2")
                std::vector<PDFInteger> pages;
                for (const PDFDocumentAssembly::Item* item : selectedItems)
                {
                    for (const PDFDocumentAssembly::Page& page : item->pages)
                    {
                        pages.push_back(page.plan.pageIndex);
                    }
                }
                original = PDFAppLocale::formatPageNumbers(pages);
            }
        }
        else
        {
            source = tr("%Ln sources", nullptr, int(inputs.size()));
        }
        const QSizeF size = first && !first->pages.empty() ? PDFAppManagePageModel::getPageSize(m_assembly, first->pages.front(), createSourceViews()) : QSizeF();
        const PageRotation turn = first && !first->pages.empty() ? first->pages.front().plan.turn : PageRotation::None;
        content.selectionRows = {
            { tr("Source"), source },
            { pageCount == 1 ? tr("Original page") : tr("Original pages"), original },
            { tr("Size"), PDFAppToolMergePageModel::getSizeText(size, false) },
            { tr("Orientation"), PDFAppToolMergePageModel::getOrientationText(size) },
            { tr("Rotation"), PDFAppManageToolGUIHelper::getRotationText(turn) },
        };
    }

    if (isDocument)
    {
        // 1479:34632: Pages "10 → 12", Added, Moved, Removed, Output "This document"
        qint64 documentPages = 0;
        if (const PDFAppDocument* document = getDocument())
        {
            documentPages = document->getPageCount();
        }
        qint64 added = 0;
        std::vector<PDFInteger> targetPages;
        for (const PDFDocumentAssembly::Item& item : m_assembly.getItems())
        {
            for (const PDFDocumentAssembly::Page& page : item.pages)
            {
                const bool isTarget = page.plan.input >= 0 && size_t(page.plan.input) < m_assembly.getSources().size() && m_assembly.getSources()[size_t(page.plan.input)].isTarget;
                if (!isTarget || page.plan.isClone)
                {
                    ++added;
                }
                else
                {
                    targetPages.push_back(page.plan.pageIndex);
                }
            }
        }
        std::set<PDFInteger> kept(targetPages.cbegin(), targetPages.cend());
        const qint64 removed = qMax<qint64>(0, documentPages - qint64(kept.size()));
        content.resultRows = {
            { tr("Pages"), tr("%1 → %2").arg(PDFAppLocale::formatNumber(documentPages), PDFAppLocale::formatNumber(summary.resultPageCount)) },
            { tr("Added"), Helper::pages(added) },
            { tr("Moved"), Helper::pages(Helper::getMovedCount(targetPages)) },
            { tr("Removed"), Helper::pages(removed) },
            { tr("Output"), getOutputText() },
        };

        // 1479:34632 the info callout, 1479:35631 the owner password; the lock (coordinator)
        const PDFAppDocument* document = getDocument();
        const QString title = document ? document->getTitle() : QString();
        if (document && document->isLocked())
        {
            content.calloutHue = PDFAccentHue::Red;
            content.calloutTitle = tr("Document is locked");
            content.calloutText = tr("Unlock it or open it as an assembly. The pages can still be arranged here.");
        }
        else if (!isDocumentChangeAllowed())
        {
            content.calloutHue = PDFAccentHue::Red;
            content.calloutTitle = tr("Owner password required");
            content.calloutText = tr("The document does not allow inserting, rotating and removing pages. If you own it, unlock it with its owner password. Until then, Rearrange Pages is not available; the pages can still be arranged here.");
            content.calloutLink = tr("Enter owner password…");
        }
        else
        {
            content.calloutHue = PDFAccentHue::Blue;
            content.calloutTitle = tr("The pages are rearranged in the file");
            content.calloutText = tr("Rearrange Pages changes %1 in this tab as one step of Undo. Checkpoints and removed pages are kept until the tool is closed. For groups or new PDFs, use Open as an Assembly – %1 is not changed there.").arg(title);
        }
    }
    else
    {
        // 387:13277: Pages, Outline "5 document parts", Estimated size "about 11 MB", Output, Images;
        // 396:26763 with the files: Files "10 · per group", Pages, Outline, Images
        const QString outline = getParameter(PDFDocumentToolManage::PARAMETER_OUTLINE);
        const bool isParts = outline.isEmpty() || outline == QLatin1String(PDFDocumentToolManage::OUTLINE_DOCUMENT_PARTS);
        const QString outlineValue = isParts && output == QLatin1String(PDFDocumentToolManage::OUTPUT_ONE_PDF)
                                   ? (summary.partCount == 1 ? tr("1 document part") : tr("%Ln document parts", nullptr, summary.partCount))
                                   : getOutlineText();
        // "about 11 MB", "about 93 kB" (387:13277, RS3BFIGMA): an estimate in whole units (KB5);
        // only this row, the measured sizes of the other tools keep their decimal
        const QString size = summary.estimatedSize >= 0 ? tr("about %1").arg(PDFAppManageToolGUIHelper::formatEstimatedSize(summary.estimatedSize)) : tr("not known yet");
        if (output == QLatin1String(PDFDocumentToolManage::OUTPUT_ONE_PDF))
        {
            content.resultRows = {
                { tr("Pages"), PDFAppLocale::formatNumber(summary.resultPageCount) },
                { tr("Outline"), outlineValue },
                { tr("Estimated size"), size },
                { tr("Output"), getOutputText() },
                { tr("Images"), getImagesText() },
            };
        }
        else
        {
            const QString kind = output == QLatin1String(PDFDocumentToolManage::OUTPUT_PER_PAGE) ? tr("per page") : tr("per group");
            content.resultRows = {
                { tr("Files"), tr("%1 · %2").arg(PDFAppLocale::formatNumber(getOutputFileCount()), kind) },
                { tr("Pages"), PDFAppLocale::formatNumber(summary.resultPageCount) },
                { tr("Outline"), outlineValue },
                { tr("Images"), getImagesText() },
            };
        }
        content.note = tr("Save Assembly (%1) keeps this arrangement for later. Create PDF writes the document; the source files are never changed.").arg(Helper::getKeyText(QStringLiteral("assemblySave")));

        // QB1, owner answer 8 of 12. 11.: the link "Edit <document> in its tab" of a single open
        // document is on the start page of the tool (PDFToolStartPage), not in this panel
    }
    content.isOutputFilesShown = !isDocument && output != QLatin1String(PDFDocumentToolManage::OUTPUT_ONE_PDF);
    m_resultPanel->setContent(content);

    // The output files (396:26763): the section and the preview of the names
    if (m_outputFiles)
    {
        m_outputFiles->setVisible(content.isOutputFilesShown && !content.isEmpty);
        if (content.isOutputFilesShown)
        {
            const std::vector<PDFDocumentToolFileOutput::File> files = createOutputFiles();
            QString error;
            QStringList names;
            if (files.empty())
            {
                // The reason of the core (an invalid template, equal names, 383:9088)
                QVariantMap values = getModel() ? getModel()->getValues() : QVariantMap();
                values.insert(Helper::name(PDFDocumentToolManage::PARAMETER_OUTPUT), output);
                std::vector<PDFDocumentToolFileOutput::File> refused;
                PDFDocumentToolManage::createFiles(m_assembly, values, QDate::currentDate(), &refused, &error);
            }
            for (const PDFDocumentToolFileOutput::File& file : files)
            {
                names << file.fileName;
            }
            const QString hint = output == QLatin1String(PDFDocumentToolManage::OUTPUT_PER_PAGE) ? tr("%Ln files · @ is the page number", nullptr, int(names.size()))
                                                                                                   : tr("%Ln files · ## is the file number, {group_name} the name of the group or item", nullptr, int(names.size()));
            m_outputFiles->setPreview(names, hint, error);
        }
    }
}

void PDFAppManageToolGUI::updateToolbar()
{
    for (const auto& [name, button] : m_actions)
    {
        if (!button)
        {
            continue;
        }
        bool isEnabled = isEditable();
        if (name == QLatin1String("rotateLeft") || name == QLatin1String("rotateRight") || name == QLatin1String("remove"))
        {
            isEnabled = isActionEnabled(name);
        }
        else if (name == QLatin1String("select") || name == QLatin1String("group") || name == QLatin1String("sort") || name == QLatin1String("more") || name == QLatin1String("output"))
        {
            // 409:32739: without items only Add and the assembly
            isEnabled = isEnabled && !m_assembly.isEmpty();
        }
        button->setEnabled(isEnabled);
    }

    // "Output: One PDF" (387:12589), "Output: One per group" (396:26763)
    if (QToolButton* outputButton = getAction(QStringLiteral("output")))
    {
        outputButton->setText(tr("Output: %1").arg(getOutputText()));
        outputButton->setAccessibleName(outputButton->text());
    }
    const QString output = getOutput();
    const QString outline = getParameter(PDFDocumentToolManage::PARAMETER_OUTLINE);
    const QString images = getParameter(PDFDocumentToolManage::PARAMETER_OPTIMIZE_IMAGES);
    for (const auto& [name, row] : m_panelRows)
    {
        if (!row)
        {
            continue;
        }
        if (name.startsWith(QStringLiteral("output-")))
        {
            row->setChecked(name.mid(7) == output);
        }
        else if (name.startsWith(QStringLiteral("outline-")))
        {
            row->setChecked(name.mid(8) == (outline.isEmpty() ? QString::fromLatin1(PDFDocumentToolManage::OUTLINE_DOCUMENT_PARTS) : outline));
        }
        else if (name.startsWith(QStringLiteral("images-")))
        {
            row->setChecked(name.mid(7) == (images.isEmpty() ? QString::fromLatin1(PDFDocumentToolManage::IMAGES_OFF) : images));
        }
        else if (name == QLatin1String("titles"))
        {
            row->setChecked(m_areDocumentTitlesShown);
        }
        else
        {
            row->setEnabled(isActionEnabled(name));
        }
    }

    // 396:26763: "10 files, named after the groups", "17 files"
    if (PDFTileRow* row = getPanelRow(QStringLiteral("output-per-group")))
    {
        row->setDescription(tr("%1, named after the groups").arg(PDFAppManageToolGUIHelper::files(qint64(m_assembly.getItems().size()))));
    }
    if (PDFTileRow* row = getPanelRow(QStringLiteral("output-per-page")))
    {
        row->setDescription(PDFAppManageToolGUIHelper::files(PDFAppManageToolGUIHelper::getPageCount(m_assembly.getItems())));
    }
    for (const auto& [name, action] : m_keyActions)
    {
        if (action)
        {
            action->setEnabled(isActionEnabled(name));
        }
    }
}

void PDFAppManageToolGUI::updateEditable()
{
    const bool isEditable = this->isEditable();
    if (m_canvas)
    {
        m_canvas->setEditable(isEditable);
        if (QWidget* emptyState = m_canvas->getEmptyState())
        {
            emptyState->setEnabled(isEditable);
        }
    }
    if (m_sourcesPanel)
    {
        m_sourcesPanel->setEditable(isEditable);
    }
}

std::vector<PDFAppDocument*> PDFAppManageToolGUI::getOpenAppDocuments() const
{
    // F13: the ready documents of the tabs of this window (not the document of this tab)
    std::vector<PDFAppDocument*> documents;
    const PDFAppDocumentRegistry* registry = getContext() ? getContext()->getDocumentRegistry() : nullptr;
    const PDFAppMainWindow* window = m_canvas ? PDFAppPanelWidgets::findMainWindow(m_canvas) : nullptr;
    if (registry)
    {
        for (PDFAppDocument* document : registry->getDocuments())
        {
            if (document->isReady() && document != getDocument() && (!window || window->findDocumentTab(document->getId())))
            {
                documents.push_back(document);
            }
        }
    }
    return documents;
}

void PDFAppManageToolGUI::updateOpenDocumentRows()
{
    using Helper = PDFAppManageToolGUIHelper;

    const auto it = m_panels.find(QStringLiteral("add"));
    PDFAppToolPanelPopup* panel = it != m_panels.cend() ? it->second.data() : nullptr;
    if (!panel)
    {
        return;
    }

    // "Open documents" (387:13288): a row of each ready document of the window
    const std::vector<PDFAppDocument*> documents = getOpenAppDocuments();
    if (m_openDocumentRows.empty() && !documents.empty())
    {
        panel->addGroupDivider();
        panel->addGroupCaption(tr("Open documents"));
    }
    for (size_t i = 0; i < documents.size(); ++i)
    {
        if (i >= m_openDocumentRows.size() || !m_openDocumentRows[i])
        {
            PDFTileRow* row = panel->addRow(QString(), QString(), QStringLiteral("tab"), PDFTileRow::Kind::MenuItem);
            connect(row, &PDFTileRow::clicked, this, &PDFAppManageToolGUI::onOpenDocumentRowClicked);
            if (i < m_openDocumentRows.size())
            {
                m_openDocumentRows[i] = row;
            }
            else
            {
                m_openDocumentRows.push_back(row);
            }
        }
        PDFTileRow* row = m_openDocumentRows[i].data();
        const PDFAppDocument* document = documents[i];
        const bool isInList = m_list && m_list->containsDocument(document->getId());
        const QString pages = Helper::pages(document->getPageCount());
        row->setObjectName(QStringLiteral("manageOpenDocument-") + document->getId());
        row->setProperty(Helper::ACTION_KEY, document->getId());
        row->setTitle(document->getTitle());
        row->setTitleKind(PDFComponentPainter::TextKind::FileName);
        row->setDescription(isInList ? tr("%1 · already a source").arg(pages) : (document->isModified() ? tr("%1 · unsaved state is used").arg(pages) : pages));
        row->setEnabled(!isInList && isEditable());
        row->show();
    }
    for (size_t i = documents.size(); i < m_openDocumentRows.size(); ++i)
    {
        if (m_openDocumentRows[i])
        {
            m_openDocumentRows[i]->hide();
        }
    }
}

void PDFAppManageToolGUI::updateRecentRows()
{
    using Helper = PDFAppManageToolGUIHelper;

    const auto it = m_panels.find(QStringLiteral("assembly"));
    PDFAppToolPanelPopup* panel = it != m_panels.cend() ? it->second.data() : nullptr;
    if (!panel || !getContext() || !getContext()->getDatabase())
    {
        return;
    }

    // 409:32023 "Recent assemblies": the name, the folder · the date · the pages (not this one)
    std::vector<PDFRecentAssembly> recent;
    for (const PDFRecentAssembly& assembly : PDFRecentAssemblyRepository(getContext()->getDatabase()).getAssemblies(Helper::MENU_RECENT_COUNT + 1))
    {
        if (assembly.path != m_assemblyPath && int(recent.size()) < Helper::MENU_RECENT_COUNT)
        {
            recent.push_back(assembly);
        }
    }
    if (m_recentRows.empty() && !recent.empty())
    {
        panel->addGroupDivider();
        panel->addGroupCaption(tr("Recent assemblies"));
    }
    for (size_t i = 0; i < recent.size(); ++i)
    {
        if (i >= m_recentRows.size() || !m_recentRows[i])
        {
            PDFTileRow* row = panel->addRow(QString(), QString(), QStringLiteral("grid"), PDFTileRow::Kind::MenuItem);
            connect(row, &PDFTileRow::clicked, this, &PDFAppManageToolGUI::onRecentRowClicked);
            if (i < m_recentRows.size())
            {
                m_recentRows[i] = row;
            }
            else
            {
                m_recentRows.push_back(row);
            }
        }
        PDFTileRow* row = m_recentRows[i].data();
        const PDFRecentAssembly& assembly = recent[i];
        row->setObjectName(QStringLiteral("manageRecent-%1").arg(i));
        row->setProperty(Helper::ACTION_KEY, assembly.path);
        row->setTitle(assembly.name);
        row->setTitleKind(PDFComponentPainter::TextKind::FileName);
        QStringList parts{ QDir::toNativeSeparators(assembly.folder), PDFAppLocale::formatDate(QDateTime::fromMSecsSinceEpoch(assembly.lastOpened, QTimeZone::UTC).toLocalTime().date()) };
        if (assembly.pageCount >= 0)
        {
            parts << Helper::pages(assembly.pageCount);
        }
        row->setDescription(parts.join(QStringLiteral(" · ")));
        row->show();
    }
    for (size_t i = recent.size(); i < m_recentRows.size(); ++i)
    {
        if (m_recentRows[i])
        {
            m_recentRows[i]->hide();
        }
    }
}

void PDFAppManageToolGUI::updateEmptyState()
{
    using Helper = PDFAppManageToolGUIHelper;

    if (!m_canvas)
    {
        return;
    }

    // 409:32739: without items and sources the two cards take the stage (not in the tab of a document)
    const bool isEmpty = !isDocumentHost() && m_assembly.isEmpty() && m_assembly.getRemovedItems().empty() && m_pending.empty();
    if (!isEmpty)
    {
        if (m_canvas->getEmptyState())
        {
            m_canvas->setEmptyState(nullptr);
        }
        return;
    }

    // The open documents and the recent assemblies: the state is made again only when they change
    const std::vector<PDFAppDocument*> documents = getOpenAppDocuments();
    std::vector<PDFRecentAssembly> recent;
    if (getContext() && getContext()->getDatabase())
    {
        recent = PDFRecentAssemblyRepository(getContext()->getDatabase()).getAssemblies(Helper::EMPTY_RECENT_COUNT);
    }
    QStringList signatureParts;
    for (const PDFAppDocument* document : documents)
    {
        signatureParts << document->getId();
    }
    for (const PDFRecentAssembly& assembly : recent)
    {
        signatureParts << assembly.path;
    }
    const QString signature = signatureParts.join(QLatin1Char('|'));
    QWidget* old = m_canvas->getEmptyState();
    if (old && old->property(Helper::ACTION_KEY).toString() == signature)
    {
        return;
    }

    QWidget* state = new QWidget(m_canvas);
    state->setObjectName(QStringLiteral("manageEmptyState"));
    state->setProperty(Helper::ACTION_KEY, signature);
    QVBoxLayout* outer = new QVBoxLayout(state);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addSpacing(60);
    QHBoxLayout* cards = new QHBoxLayout();
    cards->setContentsMargins(0, 0, 0, 0);
    cards->setSpacing(16);
    cards->addStretch(1);

    // "Start a new assembly" (dashed, 370 wide) and "Open a saved assembly" (370 wide)
    QWidget* newCard = Helper::createEmptyCard(state, cards, true, QStringLiteral("plus"), tr("Start a new assembly"),
                                               tr("Add PDF documents and images, then reorder, group and combine their pages. Drop files anywhere in this window."), QStringLiteral("manageEmptyNew"));
    QPushButton* addFiles = PDFAppPageWidgets::createButton(tr("Add Files…"), QStringLiteral("plus"), true, newCard);
    addFiles->setObjectName(QStringLiteral("manageEmptyAddFiles"));
    connect(addFiles, &QPushButton::clicked, this, &PDFAppManageToolGUI::onEmptyStateAddFiles);
    newCard->layout()->addWidget(addFiles);
    newCard->layout()->setAlignment(addFiles, Qt::AlignLeading);
    if (!documents.empty())
    {
        // The caption has its padding 8 above it (409:32941)
        QVBoxLayout* layout = qobject_cast<QVBoxLayout*>(newCard->layout());
        layout->addSpacing(Helper::EMPTY_CAPTION_PADDING);
        layout->addWidget(PDFAppPageWidgets::createLabel(tr("Open documents"), QFont::DemiBold, 11, PDFThemeColors::TextTertiary, newCard));
        for (const PDFAppDocument* document : documents)
        {
            // tab-1 409:32954: the shared row of an open document with the Secondary "+ Add"
            PDFAppToolOpenDocumentRow::Document item;
            item.documentId = document->getId();
            item.name = document->getTitle();
            item.details = Helper::pages(document->getPageCount());
            PDFAppToolOpenDocumentRow* row = new PDFAppToolOpenDocumentRow(item, newCard);
            row->setObjectName(QStringLiteral("manageEmptyDocument-") + document->getId());
            row->getAddButton()->setObjectName(QStringLiteral("manageEmptyAdd"));
            connect(row, &PDFAppToolOpenDocumentRow::addRequested, this, &PDFAppManageToolGUI::onEmptyStateAddDocument);
            layout->addWidget(row);
        }
    }
    QWidget* openCard = Helper::createEmptyCard(state, cards, false, QStringLiteral("folder-open"), tr("Open a saved assembly"),
                                                tr("Continue where you stopped: sources, order, groups and checkpoints are restored."), QStringLiteral("manageEmptyOpen"));
    QPushButton* openButton = PDFAppPageWidgets::createButton(tr("Open Assembly…"), QStringLiteral("folder-open"), false, openCard);
    openButton->setObjectName(QStringLiteral("manageEmptyOpenAssembly"));
    connect(openButton, &QPushButton::clicked, this, &PDFAppManageToolGUI::onEmptyStateOpenAssembly);
    openCard->layout()->addWidget(openButton);
    openCard->layout()->setAlignment(openButton, Qt::AlignLeading);
    if (!recent.empty())
    {
        QVBoxLayout* layout = qobject_cast<QVBoxLayout*>(openCard->layout());
        layout->addSpacing(Helper::EMPTY_CAPTION_PADDING);
        layout->addWidget(PDFAppPageWidgets::createLabel(tr("Recent assemblies"), QFont::DemiBold, 11, PDFThemeColors::TextTertiary, openCard));
        for (const PDFRecentAssembly& assembly : recent)
        {
            QStringList parts{ QDir::toNativeSeparators(assembly.folder), PDFAppLocale::formatDate(QDateTime::fromMSecsSinceEpoch(assembly.lastOpened, QTimeZone::UTC).toLocalTime().date()) };
            if (assembly.pageCount >= 0)
            {
                parts << Helper::pages(assembly.pageCount);
            }
            PDFTileRow* row = new PDFTileRow(assembly.name, parts.join(QStringLiteral(" · ")), QStringLiteral("grid"), PDFTileRow::Kind::MenuItem, openCard);
            row->setObjectName(QStringLiteral("manageEmptyRecent"));
            row->setTitleKind(PDFComponentPainter::TextKind::FileName);
            row->setProperty(Helper::ACTION_KEY, assembly.path);
            connect(row, &PDFTileRow::clicked, this, &PDFAppManageToolGUI::onRecentRowClicked);
            layout->addWidget(row);
        }
    }
    cards->addStretch(1);
    outer->addLayout(cards);
    outer->addStretch(1);

    // The old state may be the sender of the change: the canvas deletes it later
    m_canvas->setEmptyState(state);
}

PDFAppToolPanelPopup* PDFAppManageToolGUI::getPanel(const QString& name) const
{
    const auto it = m_panels.find(name);
    return it != m_panels.cend() ? it->second.data() : nullptr;
}

QToolButton* PDFAppManageToolGUI::getAction(const QString& name) const
{
    const auto it = m_actions.find(name);
    return it != m_actions.cend() ? it->second.data() : nullptr;
}

PDFTileRow* PDFAppManageToolGUI::getPanelRow(const QString& name) const
{
    const auto it = m_panelRows.find(name);
    return it != m_panelRows.cend() ? it->second.data() : nullptr;
}

QAction* PDFAppManageToolGUI::getKeyAction(const QString& name) const
{
    const auto it = m_keyActions.find(name);
    return it != m_keyActions.cend() ? it->second.data() : nullptr;
}

}   // namespace pdf

#include "moc_pdfapptoolgui_manage.cpp"
