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

#include "pdfappannotationmenus.h"
#include "pdfappannotationcontroller.h"
#include "pdfappannotationinfo.h"
#include "pdfappannotationoperations.h"
#include "pdfappcommand.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentview.h"
#include "pdfappmainwindow.h"
#include "pdfapppage.h"

#include "pdfthemeiconengine.h"
#include "pdfwidgetstyle.h"
#include "pdfwidgetstyleproperties.h"

#include <QAction>
#include <QMenu>

namespace pdf
{

/// Properties of the actions of the menus
class PDFAppAnnotationMenusHelper
{
public:
    PDFAppAnnotationMenusHelper() = delete;

    /// Command (or operation) of the action, annotation of the action (an item
    /// of a row of the chooser acts on the annotation of its row)
    static constexpr const char* ID_PROPERTY = "annotationMenuId";
    static constexpr const char* ANNOTATION_PROPERTY = "annotationMenuReference";
    static constexpr const char* OPERATION_PROPERTY = "annotationMenuOperation";

    static QVariant toVariant(PDFObjectReference reference)
    {
        return PDFAppAnnotationMenus::toVariant(reference);
    }

    static PDFObjectReference fromVariant(const QVariant& value)
    {
        return PDFAppAnnotationMenus::fromVariant(value);
    }

    /// A text of the document (a name, an author, a page label) in the text of a
    /// menu item: an ampersand is shown, it never becomes a mnemonic
    static QString escapeMnemonic(QString text)
    {
        return text.replace(QLatin1Char('&'), QStringLiteral("&&"));
    }
};

QVariant PDFAppAnnotationMenus::toVariant(PDFObjectReference reference)
{
    return QVariantList({ QVariant(qlonglong(reference.objectNumber)), QVariant(qlonglong(reference.generation)) });
}

PDFObjectReference PDFAppAnnotationMenus::fromVariant(const QVariant& value)
{
    const QVariantList list = value.toList();
    if (list.size() != 2)
    {
        return PDFObjectReference();
    }
    return PDFObjectReference(PDFInteger(list[0].toLongLong()), PDFInteger(list[1].toLongLong()));
}

PDFAppAnnotationMenus::PDFAppAnnotationMenus(PDFAppDocumentView* view, QObject* parent) :
    BaseClass(parent),
    m_view(view)
{
    setObjectName(QStringLiteral("annotationMenus"));
}

PDFAppAnnotationMenus::~PDFAppAnnotationMenus()
{
    delete m_menu.data();
    setHighlightedAnnotation(PDFObjectReference());
}

void PDFAppAnnotationMenus::onChooserHovered(QAction* action)
{
    setHighlightedAnnotation(action ? fromVariant(action->property(PDFAppAnnotationMenusHelper::ANNOTATION_PROPERTY)) : PDFObjectReference());
}

void PDFAppAnnotationMenus::onChooserHidden()
{
    setHighlightedAnnotation(PDFObjectReference());
}

void PDFAppAnnotationMenus::setHighlightedAnnotation(PDFObjectReference annotation)
{
    // The view is asked only while a row is hovered (the view is alive then); the
    // outline is removed through the guarded manager - also from the destructor,
    // when the view can be destroyed already
    if (annotation.isValid())
    {
        PDFAppAnnotationController* controller = m_view ? m_view->getAnnotationController() : nullptr;
        PDFWidgetAnnotationManager* manager = controller ? controller->getAnnotationManager() : nullptr;
        if (m_highlightManager && m_highlightManager != manager)
        {
            m_highlightManager->setHighlightedAnnotation(PDFObjectReference());
        }
        m_highlightManager = manager;
    }

    if (m_highlightManager)
    {
        m_highlightManager->setHighlightedAnnotation(annotation);
    }
}

QKeySequence PDFAppAnnotationMenus::getShortcut(const QString& commandId) const
{
    // The effective shortcut of the user (the action of the window has it)
    const PDFAppMainWindow* window = m_view ? qobject_cast<const PDFAppMainWindow*>(m_view->window()) : nullptr;
    const QAction* canvasAction = m_view ? m_view->getCanvasAction(commandId) : nullptr;
    const QAction* action = canvasAction ? canvasAction : (window ? window->getCommandAction(commandId) : nullptr);
    if (action && !action->shortcut().isEmpty())
    {
        return action->shortcut();
    }
    const PDFAppContext* context = m_view ? m_view->getContext() : nullptr;
    const PDFAppCommand* command = context ? context->getCommandRegistry()->findCommand(commandId) : nullptr;
    if (command)
    {
        return command->defaultShortcut;
    }

    // The canvas actions of View (Copy, Select All) are known by their keys
    if (commandId == PDFAppCommands::copy())
    {
        return QKeySequence::Copy;
    }
    return QKeySequence();
}

void PDFAppAnnotationMenus::addCommand(QMenu* menu, const QString& commandId, const QString& text, const QString& iconName, PDFObjectReference annotation, bool isEnabled)
{
    using H = PDFAppAnnotationMenusHelper;

    // An item without an icon keeps the column of the icon (Menu item, Show icon off)
    QAction* action = menu->addAction(iconName.isEmpty() ? QIcon() : PDFThemeIcons::getIcon(PDFAppPage::getDirectionalIconName(iconName, menu->layoutDirection())), text);
    action->setObjectName(commandId);
    action->setProperty(H::ID_PROPERTY, commandId);
    action->setProperty(H::ANNOTATION_PROPERTY, H::toVariant(annotation));
    action->setEnabled(isEnabled);

    // The shortcut is shown only (the commands have their actions in the window);
    // Select All on Displayed Pages is Ctrl+A of Select All, which acts on the
    // annotations, when some are selected
    const QKeySequence shortcut = getShortcut(commandId == PDFAppCommands::annotationSelectAll() ? PDFAppCommands::selectAll() : commandId);
    if (!shortcut.isEmpty())
    {
        action->setShortcut(shortcut);
        action->setShortcutContext(Qt::WidgetShortcut);
    }
    if (commandId == PDFAppCommands::deleteSelection())
    {
        action->setProperty(PDFWidgetStyleProperties::DESTRUCTIVE, true);
    }
    connect(action, &QAction::triggered, this, &PDFAppAnnotationMenus::onActionTriggered);
}

QAction* PDFAppAnnotationMenus::addOperation(QMenu* menu, const QString& id, const QString& text, const QString& iconName, QAction* before)
{
    using H = PDFAppAnnotationMenusHelper;

    QAction* action = new QAction(PDFThemeIcons::getIcon(iconName), text, menu);
    action->setObjectName(id);
    action->setProperty(H::ID_PROPERTY, id);
    action->setProperty(H::OPERATION_PROPERTY, true);
    menu->insertAction(before, action);
    connect(action, &QAction::triggered, this, &PDFAppAnnotationMenus::onActionTriggered);
    return action;
}

void PDFAppAnnotationMenus::setPanelWidth(QMenu* menu, int width)
{
    if (menu)
    {
        const QMargins margins = PDFWidgetStyle::getPopoverMargins(menu);
        menu->setMinimumWidth(width + margins.left() + margins.right());
    }
}

void PDFAppAnnotationMenus::addSection(QMenu* menu, const QString& text)
{
    QAction* section = menu->addSection(text);
    section->setObjectName(QStringLiteral("section"));
}

QAction* PDFAppAnnotationMenus::findAction(const QMenu* menu, const QString& id)
{
    if (!menu)
    {
        return nullptr;
    }

    for (QAction* action : menu->actions())
    {
        if (action->property(PDFAppAnnotationMenusHelper::ID_PROPERTY).toString() == id)
        {
            return action;
        }
        if (QAction* found = findAction(action->menu(), id))
        {
            return found;
        }
    }
    return nullptr;
}

QMenu* PDFAppAnnotationMenus::showMenu(const PDFAnnotationMenuRequest& request)
{
    // One menu at a time
    if (m_menu)
    {
        m_menu->close();
        delete m_menu.data();
    }

    QWidget* parent = m_view ? m_view->window() : nullptr;
    QMenu* menu = createMenu(request, parent);
    if (!menu)
    {
        return nullptr;
    }

    m_menu = menu;
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->popup(request.globalPosition);
    return menu;
}

QMenu* PDFAppAnnotationMenus::createMenu(const PDFAnnotationMenuRequest& request, QWidget* parent)
{
    const PDFAppAnnotationController* controller = m_view ? m_view->getAnnotationController() : nullptr;
    if (!controller || request.kind == PDFAnnotationMenuRequest::Kind::None)
    {
        return nullptr;
    }

    m_request = request;
    QMenu* menu = new QMenu(parent);
    menu->setObjectName(QStringLiteral("annotationMenu"));
    if (m_view)
    {
        menu->setLayoutDirection(m_view->layoutDirection());
    }

    switch (request.kind)
    {
        case PDFAnnotationMenuRequest::Kind::Selection:
            fillSelectionMenu(menu, request, request.annotation, false);
            fillEngineItems(menu, request);
            break;
        case PDFAnnotationMenuRequest::Kind::Unselectable:
            fillUnselectableMenu(menu, request);
            break;
        case PDFAnnotationMenuRequest::Kind::Overlap:
            fillChooserMenu(menu, request);
            break;
        case PDFAnnotationMenuRequest::Kind::None:
            break;
    }

    if (menu->actions().isEmpty())
    {
        delete menu;
        return nullptr;
    }
    return menu;
}

void PDFAppAnnotationMenus::fillSelectionMenu(QMenu* menu, const PDFAnnotationMenuRequest& request, PDFObjectReference annotation, bool isChooser)
{
    using C = PDFAppCommands;

    const PDFAppAnnotationController* controller = m_view->getAnnotationController();
    const PDFAppDocument* document = controller->getDocument();
    const PDFDocumentPointer pdfDocument = document ? document->getDocument() : PDFDocumentPointer();
    const bool isEditingAllowed = controller->isAnnotationEditingAllowed();
    const std::vector<PDFObjectReference> selection = isChooser ? std::vector<PDFObjectReference>({ annotation }) : request.selection;
    const bool isSingle = selection.size() == 1;
    if (selection.empty())
    {
        // A request without a selection (the selection has gone, before the queued
        // request came) has no menu of a selection
        return;
    }

    // A locked annotation (or all of them locked) keeps its geometry and its properties
    bool isAnyModifiable = false;
    for (const PDFObjectReference& reference : selection)
    {
        isAnyModifiable = isAnyModifiable || PDFAppAnnotationOperations::canModifyAnnotation(pdfDocument, reference);
    }
    const bool canChange = isEditingAllowed && isAnyModifiable;
    const PDFObjectReference target = isChooser ? annotation : PDFObjectReference();

    if (!isSingle && !isChooser)
    {
        // section 1019:19986: "3 annotations on page 308"
        const PDFAppAnnotationInfo info = PDFAppAnnotationInfo::read(pdfDocument.data(), selection.front());
        addSection(menu, tr("%n annotations on page %1", nullptr, int(selection.size())).arg(PDFAppAnnotationMenusHelper::escapeMnemonic(m_view->getPageLabel(info.pageIndex))));
    }

    if (isSingle)
    {
        // open-comment 1019:19319, reply 1019:19331, properties 1019:19342, geometry 1019:19353
        addCommand(menu, C::annotationComment(), tr("Open Comment"), QStringLiteral("message-square"), target);
        if (isEditingAllowed)
        {
            addCommand(menu, C::annotationReply(), tr("Reply"), QStringLiteral("corner-down-right"), target);
        }
        addCommand(menu, C::annotationProperties(), tr("Properties…"), QStringLiteral("info"), target);
        if (canChange)
        {
            addCommand(menu, C::annotationGeometry(), tr("Geometry…"), QStringLiteral("transform"), target);
        }
        menu->addSeparator();
    }

    // cut 1019:19366, copy 1019:19377, paste 1019:19388, paste-at-original-position 1019:19399, copy-to-pages 1019:19409
    if (canChange)
    {
        addCommand(menu, C::cut(), tr("Cut"), QStringLiteral("clipboard-cut"), target);
    }
    addCommand(menu, C::copy(), tr("Copy"), QStringLiteral("copy"), target);
    if (isEditingAllowed && !isChooser)
    {
        addCommand(menu, C::paste(), tr("Paste"), QStringLiteral("clipboard"), PDFObjectReference(), request.canPaste);
        if (isSingle)
        {
            // paste-at-original-position 1019:19399 has no icon
            addCommand(menu, C::pasteInPlace(), tr("Paste at Original Position"), QString(), PDFObjectReference(), request.canPaste);
        }
    }
    if (isEditingAllowed)
    {
        addCommand(menu, C::annotationCopyToPages(), tr("Copy to Pages…"), QStringLiteral("copy-to-pages"), target);
    }

    // rotate-and-flip 1019:19424 (submenu); align 1019:20044 (several annotations)
    if (canChange)
    {
        menu->addSeparator();
        createRotateMenu(menu, target, canChange);
        if (!isSingle)
        {
            createAlignMenu(menu, canChange);
        }

        // hide-on-page 1019:19434
        menu->addSeparator();
        addCommand(menu, C::annotationHide(), tr("Hide on Page"), QStringLiteral("eye-off"), target);
    }

    // select-all-on-displayed-pages 1019:19447 (a single annotation only: not in the menu
    // of several annotations 1019:19985, nor in the rows of the chooser), delete 1019:19460
    menu->addSeparator();
    if (isSingle && !isChooser)
    {
        addCommand(menu, C::annotationSelectAll(), tr("Select All on Displayed Pages"), QStringLiteral("select-all"), PDFObjectReference());
    }
    if (canChange)
    {
        addCommand(menu, C::deleteSelection(), tr("Delete"), QStringLiteral("trash"), target);
    }

    if (isChooser)
    {
        setPanelWidth(menu, SUBMENU_WIDTH);
    }
    else
    {
        setPanelWidth(menu, isSingle ? SINGLE_WIDTH : MULTI_WIDTH);
    }
}

QMenu* PDFAppAnnotationMenus::createRotateMenu(QMenu* parent, PDFObjectReference annotation, bool isEnabled)
{
    using C = PDFAppCommands;

    // rotate-and-flip 1019:19424 has no icon
    QMenu* submenu = parent->addMenu(tr("Rotate and Flip"));
    setPanelWidth(submenu, SUBMENU_WIDTH);
    submenu->menuAction()->setObjectName(QStringLiteral("rotateAndFlip"));
    submenu->setEnabled(isEnabled);
    addCommand(submenu, C::annotationRotateClockwise(), tr("Rotate Clockwise"), QStringLiteral("rotate-cw"), annotation);
    addCommand(submenu, C::annotationRotateCounterclockwise(), tr("Rotate Counterclockwise"), QStringLiteral("rotate-ccw"), annotation);
    addCommand(submenu, C::annotationRotate180(), tr("Rotate 180°"), QStringLiteral("rotate-180"), annotation);
    submenu->addSeparator();
    addCommand(submenu, C::annotationFlipHorizontal(), tr("Flip Horizontally"), QStringLiteral("swap"), annotation);
    addCommand(submenu, C::annotationFlipVertical(), tr("Flip Vertically"), QStringLiteral("arrow-up-down"), annotation);
    return submenu;
}

QMenu* PDFAppAnnotationMenus::createAlignMenu(QMenu* parent, bool isEnabled)
{
    using C = PDFAppCommands;

    // align 1019:20044 has no icon, submenu-align 1019:20080
    QMenu* submenu = parent->addMenu(tr("Align"));
    setPanelWidth(submenu, SUBMENU_WIDTH);
    submenu->menuAction()->setObjectName(QStringLiteral("align"));
    submenu->setEnabled(isEnabled);
    addCommand(submenu, C::annotationAlignLeft(), tr("Left"), QStringLiteral("obj-align-left"), PDFObjectReference());
    addCommand(submenu, C::annotationAlignCenter(), tr("Center Horizontally"), QStringLiteral("obj-align-hcenter"), PDFObjectReference());
    addCommand(submenu, C::annotationAlignRight(), tr("Right"), QStringLiteral("obj-align-right"), PDFObjectReference());
    addCommand(submenu, C::annotationAlignTop(), tr("Top"), QStringLiteral("obj-align-top"), PDFObjectReference());
    addCommand(submenu, C::annotationAlignMiddle(), tr("Center Vertically"), QStringLiteral("obj-align-vcenter"), PDFObjectReference());
    addCommand(submenu, C::annotationAlignBottom(), tr("Bottom"), QStringLiteral("obj-align-bottom"), PDFObjectReference());
    submenu->addSeparator();
    addCommand(submenu, C::annotationDistributeHorizontally(), tr("Distribute Horizontally"), QStringLiteral("distribute-h"), PDFObjectReference(),
               m_request.selection.size() >= 3);
    addCommand(submenu, C::annotationDistributeVertically(), tr("Distribute Vertically"), QStringLiteral("distribute-v"), PDFObjectReference(),
               m_request.selection.size() >= 3);
    return submenu;
}

void PDFAppAnnotationMenus::fillUnselectableMenu(QMenu* menu, const PDFAnnotationMenuRequest& request)
{
    // A hidden annotation, a reply (1026:21995): Open Comment, Show on Page, Properties, Delete
    const PDFAppAnnotationController* controller = m_view->getAnnotationController();
    const PDFAppDocument* document = controller->getDocument();
    const PDFDocumentPointer pdfDocument = document ? document->getDocument() : PDFDocumentPointer();
    const PDFAppAnnotationInfo info = PDFAppAnnotationInfo::read(pdfDocument.data(), request.annotation);
    if (!info.isValid())
    {
        return;
    }

    const bool canChange = controller->isAnnotationEditingAllowed() && PDFAppAnnotationOperations::canModifyAnnotation(pdfDocument, request.annotation);
    addOperation(menu, QStringLiteral("annotation.openComment"), tr("Open Comment"), QStringLiteral("message-square"));
    if (info.isHidden() && canChange)
    {
        addOperation(menu, QStringLiteral("annotation.show"), tr("Show on Page"), QStringLiteral("eye"));
    }
    addOperation(menu, QStringLiteral("annotation.openProperties"), tr("Properties…"), QStringLiteral("info"));
    if (canChange)
    {
        menu->addSeparator();
        QAction* action = addOperation(menu, QStringLiteral("annotation.remove"), tr("Delete"), QStringLiteral("trash"));
        action->setProperty(PDFWidgetStyleProperties::DESTRUCTIVE, true);
    }
    setPanelWidth(menu, SINGLE_WIDTH);
}

void PDFAppAnnotationMenus::fillChooserMenu(QMenu* menu, const PDFAnnotationMenuRequest& request)
{
    // chooser-menu 1019:20673: "2 annotations at this place", a row per annotation
    // with the author as the shortcut text and the single menu as a submenu
    const PDFAppAnnotationController* controller = m_view->getAnnotationController();
    const PDFAppDocument* document = controller->getDocument();
    const PDFDocumentPointer pdfDocument = document ? document->getDocument() : PDFDocumentPointer();
    addSection(menu, tr("%n annotations at this place", nullptr, int(request.annotationsUnderCursor.size())));

    int index = 0;
    for (const PDFObjectReference& reference : request.annotationsUnderCursor)
    {
        const PDFAppAnnotationInfo info = PDFAppAnnotationInfo::read(pdfDocument.data(), reference);
        if (!info.isValid() || index >= 50)
        {
            continue;
        }

        QString iconName;
        PDFAccentHue hue = PDFAccentHue::Neutral;
        PDFAppAnnotationInfo::getBadge(info.type, info.isReply, &iconName, &hue);
        using H = PDFAppAnnotationMenusHelper;
        const QString author = info.isMarkup ? H::escapeMnemonic(PDFAppAnnotationInfo::getAuthorText(info.author)) : QString();
        const QString name = H::escapeMnemonic(PDFAppAnnotationInfo::getDisplayName(info));
        QMenu* submenu = menu->addMenu(PDFThemeIcons::getIcon(iconName), author.isEmpty() ? name : tr("%1\t%2").arg(name, author));
        submenu->menuAction()->setObjectName(QStringLiteral("chooser:%1").arg(index));
        submenu->menuAction()->setProperty(PDFAppAnnotationMenusHelper::ANNOTATION_PROPERTY, PDFAppAnnotationMenusHelper::toVariant(reference));
        fillSelectionMenu(submenu, request, reference, true);
        ++index;
    }

    // section hint 1019:20700 (a section at the end is shown only, when the
    // separators of the menu are not collapsed)
    menu->setSeparatorsCollapsible(false);
    menu->addSeparator();
    addSection(menu, tr("Alt+click on the page selects the next one"));
    setPanelWidth(menu, CHOOSER_WIDTH);

    // hover-outline 1019:20672: the annotation of the hovered row is outlined on the page
    connect(menu, &QMenu::hovered, this, &PDFAppAnnotationMenus::onChooserHovered);
    connect(menu, &QMenu::aboutToHide, this, &PDFAppAnnotationMenus::onChooserHidden);
}

void PDFAppAnnotationMenus::fillEngineItems(QMenu* menu, const PDFAnnotationMenuRequest& request)
{
    // Items of the place of the menu (a point, a part, a callout line) - the engine
    // tells, what is possible there; none of them for a locked document
    if (!request.isModificationAllowed || !request.widgetPosition)
    {
        return;
    }

    std::vector<std::pair<QString, QString>> items;
    if (request.pointAnnotation.isValid() && request.removablePoint >= 0)
    {
        items.emplace_back(QStringLiteral("point.remove"), tr("Delete Point"));
    }
    if (request.pointAnnotation.isValid() && request.insertSegment >= 0)
    {
        items.emplace_back(QStringLiteral("point.insert"), tr("Insert Point"));
    }
    if (request.partAnnotation.isValid() && request.partIndex >= 0)
    {
        items.emplace_back(QStringLiteral("part.remove"), tr("Remove This Part"));
    }
    if (request.canAddStroke)
    {
        items.emplace_back(QStringLiteral("part.addStroke"), tr("Add Stroke"));
    }
    if (request.canAddMarkedAreas)
    {
        items.emplace_back(QStringLiteral("part.addMarkedAreas"), tr("Mark More Text"));
    }
    if (request.canEraseInk)
    {
        items.emplace_back(QStringLiteral("part.eraseInk"), tr("Erase Here"));
    }
    if (request.canAddCalloutLine)
    {
        items.emplace_back(QStringLiteral("callout.add"), tr("Add Callout Line"));
    }
    if (request.canRemoveCalloutLine)
    {
        items.emplace_back(QStringLiteral("callout.remove"), tr("Remove Callout Line"));
    }
    if (items.empty())
    {
        return;
    }

    // The items of the place come first, as the menu of the old editor had them
    QAction* first = menu->actions().isEmpty() ? nullptr : menu->actions().front();
    for (const auto& item : items)
    {
        addOperation(menu, item.first, item.second, QStringLiteral("anchor-point"), first);
    }
    menu->insertSeparator(first);
}

void PDFAppAnnotationMenus::onActionTriggered()
{
    using H = PDFAppAnnotationMenusHelper;

    // The actions do not identify themselves in the signal
    const QAction* action = qobject_cast<const QAction*>(sender());
    PDFAppDocumentView* view = m_view.data();
    PDFAppAnnotationController* controller = view ? view->getAnnotationController() : nullptr;
    if (!action || !controller)
    {
        return;
    }

    const QString id = action->property(H::ID_PROPERTY).toString();
    if (action->property(H::OPERATION_PROPERTY).toBool())
    {
        runOperation(id);
        return;
    }

    // An item of a row of the chooser acts on its annotation: it is selected first
    const PDFObjectReference annotation = H::fromVariant(action->property(H::ANNOTATION_PROPERTY));
    if (annotation.isValid())
    {
        controller->selectAnnotations({ annotation });
    }

    // Paste at the place of the menu (the clipboard of the annotations)
    if (id == PDFAppCommands::paste() && m_request.widgetPosition && controller->canPaste())
    {
        QPointer<PDFAppAnnotationController> controllerGuard(controller);
        controller->beginEngineChange(tr("Paste annotations"));
        controller->getAnnotationManager()->pasteAnnotations(m_request.widgetPosition, false);
        if (controllerGuard)
        {
            controllerGuard->endEngineChange();
        }
        return;
    }

    // The command runs from the event loop (a dialog, the deletion of the tab)
    view->requestCommand(id);
}

void PDFAppAnnotationMenus::runOperation(const QString& id)
{
    PDFAppDocumentView* view = m_view.data();
    PDFAppAnnotationController* controller = view ? view->getAnnotationController() : nullptr;
    PDFWidgetAnnotationManager* manager = controller ? controller->getAnnotationManager() : nullptr;
    PDFAppDocument* document = controller ? controller->getDocument() : nullptr;
    if (!manager || !document)
    {
        return;
    }

    // The request is copied (an item may show another menu); a change of the engine
    // is announced by beginEngineChange and finished at the end
    const PDFAnnotationMenuRequest request = m_request;
    QPointer<PDFAppAnnotationController> controllerGuard(controller);
    if (id == QStringLiteral("annotation.openComment"))
    {
        controller->requestComment(request.annotation, false);
    }
    else if (id == QStringLiteral("annotation.openProperties"))
    {
        controller->requestProperties(request.annotation);
    }
    else if (id == QStringLiteral("annotation.show"))
    {
        if (const PDFAppAnnotationResult result = PDFAppAnnotationOperations::setAnnotationsHidden(document->getDocument(), { request.annotation }, false))
        {
            controller->commit(*result, PDFAppAnnotationChangeKind::Annotation, tr("Show annotation"));
        }
    }
    else if (id == QStringLiteral("annotation.remove"))
    {
        if (const PDFAppAnnotationResult result = PDFAppAnnotationOperations::deleteAnnotations(document->getDocument(), { request.annotation }))
        {
            controller->commit(*result, PDFAppAnnotationChangeKind::Annotation, tr("Delete annotation"));
        }
    }
    else if (id == QStringLiteral("point.remove"))
    {
        controller->beginEngineChange(tr("Delete point"));
        manager->removeAnnotationPoint(request.pointAnnotation, size_t(request.removablePoint));
    }
    else if (id == QStringLiteral("point.insert"))
    {
        controller->beginEngineChange(tr("Insert point"));
        manager->insertAnnotationPoint(request.pointAnnotation, size_t(request.insertSegment), request.insertPosition);
    }
    else if (id == QStringLiteral("part.remove"))
    {
        controller->beginEngineChange(tr("Remove part"));
        manager->removeAnnotationPart(request.partAnnotation, size_t(request.partIndex));
    }
    else if (id == QStringLiteral("part.addStroke"))
    {
        manager->beginPartEdit(PDFWidgetAnnotationManager::PartEdit::AddStroke);
    }
    else if (id == QStringLiteral("part.addMarkedAreas"))
    {
        manager->beginPartEdit(PDFWidgetAnnotationManager::PartEdit::AddMarkedAreas);
    }
    else if (id == QStringLiteral("part.eraseInk"))
    {
        controller->beginEngineChange(tr("Erase"));
        manager->eraseAnnotationInk(request.annotation, request.eraseCenter, request.eraseRadius);
    }
    else if (id == QStringLiteral("callout.add"))
    {
        controller->beginEngineChange(tr("Add callout line"));
        manager->addAnnotationCalloutLine(request.annotation, request.calloutTip);
    }
    else if (id == QStringLiteral("callout.remove"))
    {
        controller->beginEngineChange(tr("Remove callout line"));
        manager->removeAnnotationCalloutLine(request.annotation);
    }

    // A description, which no commit has taken (nothing has changed), does not
    // name the next change
    if (controllerGuard)
    {
        controllerGuard->endEngineChange();
    }
}

}   // namespace pdf
