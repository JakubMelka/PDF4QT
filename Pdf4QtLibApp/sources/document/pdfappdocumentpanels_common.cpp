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

#include "pdfappdocumentpanels_common.h"
#include "pdfappdocumenttab.h"
#include "pdfappmainwindow.h"
#include "pdfappnavigator.h"
#include "pdfapproute.h"

#include "pdfaction.h"
#include "pdfcatalog.h"
#include "pdffluentwidgets.h"
#include "pdfmessagebox.h"
#include "pdfthemeiconengine.h"
#include "pdfwidgetstyleproperties.h"

#include <QAbstractItemView>
#include <QCoreApplication>
#include <QItemSelectionModel>
#include <QListView>
#include <QToolButton>

#include <utility>

namespace pdf
{

PDFAppFileDialogRequest* PDFAppPanelFileDialogs::begin(QObject* requester, QWidget* parent, PDFAppFileDialogRequest::Mode mode, const QString& title, const QString& path, const QString& filter)
{
    PDFAppFileDialogRequest::Settings settings;
    settings.mode = mode;
    settings.title = title;
    settings.path = path;
    settings.filter = filter;
    PDFAppFileDialogRequest* request = PDFAppFileDialogRequest::create(requester, parent, settings);
    if (!request)
    {
        return nullptr;
    }

    // The handler of the tests (the one set now) answers as the user of a real dialog, later: the request calls it
    // from the event loop with the four arguments of the settings. This call runs no foreign code.
    const FileHandler& handler = mode == PDFAppFileDialogRequest::Mode::SaveFile ? getSaveHandler() : getOpenHandler();
    if (handler)
    {
        request->answerByHandler(handler);
        return request;
    }

    request->show();
    return request;
}

void PDFAppPanelFileDialogs::setSaveHandler(FileHandler handler)
{
    getSaveHandler() = std::move(handler);
}

void PDFAppPanelFileDialogs::setOpenHandler(FileHandler handler)
{
    getOpenHandler() = std::move(handler);
}

PDFAppPanelFileDialogs::FileHandler& PDFAppPanelFileDialogs::getSaveHandler()
{
    static FileHandler handler;
    return handler;
}

PDFAppPanelFileDialogs::FileHandler& PDFAppPanelFileDialogs::getOpenHandler()
{
    static FileHandler handler;
    return handler;
}

PDFAppPanelListView::PDFAppPanelListView(QWidget* parent) :
    BaseClass(parent)
{

}

PDFAppPanelListView::~PDFAppPanelListView() = default;

void PDFAppPanelListView::setContentPaddings(const QMargins& margins)
{
    setViewportMargins(margins);
}

PDFAppItemViewSignalRelay::PDFAppItemViewSignalRelay(QAbstractItemView* view, QObject* parent) :
    BaseClass(parent),
    m_view(view)
{
    if (!view)
    {
        return;
    }

    connect(view, &QAbstractItemView::clicked, this, &PDFAppItemViewSignalRelay::onClicked);
    connect(view, &QAbstractItemView::activated, this, &PDFAppItemViewSignalRelay::onActivated);
    if (QItemSelectionModel* selectionModel = view->selectionModel())
    {
        connect(selectionModel, &QItemSelectionModel::currentChanged, this, &PDFAppItemViewSignalRelay::onCurrentChanged);
        connect(selectionModel, &QItemSelectionModel::selectionChanged, this, &PDFAppItemViewSignalRelay::onSelectionChanged);
    }
}

PDFAppItemViewSignalRelay::~PDFAppItemViewSignalRelay() = default;

void PDFAppItemViewSignalRelay::onClicked(const QModelIndex& index)
{
    add(Kind::Clicked, index);
}

void PDFAppItemViewSignalRelay::onActivated(const QModelIndex& index)
{
    add(Kind::Activated, index);
}

void PDFAppItemViewSignalRelay::onCurrentChanged()
{
    add(Kind::CurrentChanged, QModelIndex());
}

void PDFAppItemViewSignalRelay::onSelectionChanged()
{
    add(Kind::SelectionChanged, QModelIndex());
}

void PDFAppItemViewSignalRelay::add(Kind kind, const QModelIndex& index)
{
    // A change of the state is said once (the slot reads the state of then); every click and activation is said
    if (kind == Kind::CurrentChanged || kind == Kind::SelectionChanged)
    {
        for (const Pending& pending : m_pending)
        {
            if (pending.kind == kind)
            {
                return;
            }
        }
    }

    const bool isQueued = !m_pending.empty();
    m_pending.push_back(Pending{ kind, QPersistentModelIndex(index) });
    if (!isQueued)
    {
        QMetaObject::invokeMethod(this, &PDFAppItemViewSignalRelay::onQueued, Qt::QueuedConnection);
    }
}

void PDFAppItemViewSignalRelay::onQueued()
{
    // The signals in the order of Qt; each of them may delete the relay with its receiver, or the view
    const std::vector<Pending> pending = std::exchange(m_pending, std::vector<Pending>());
    QPointer<PDFAppItemViewSignalRelay> guard(this);
    for (const Pending& item : pending)
    {
        if (!guard || !m_view)
        {
            return;
        }

        switch (item.kind)
        {
            case Kind::Clicked:
                if (item.index.isValid())
                {
                    Q_EMIT clicked(item.index);
                }
                break;

            case Kind::Activated:
                if (item.index.isValid())
                {
                    Q_EMIT activated(item.index);
                }
                break;

            case Kind::CurrentChanged:
                Q_EMIT currentChanged();
                break;

            case Kind::SelectionChanged:
                Q_EMIT selectionChanged();
                break;
        }
    }
}

void PDFAppPanelWidgets::prepareList(PDFAppPanelListView* view)
{
    prepareView(view);
    if (view)
    {
        // The spacing of QListView is on every side of a row: half of the gap
        constexpr int spacing = LIST_SPACING / 2;
        view->setSpacing(spacing);
        view->setContentPaddings(QMargins(CONTENT_PADDING_SIDE - spacing, CONTENT_PADDING_TOP - spacing, CONTENT_PADDING_SIDE - spacing, CONTENT_PADDING_BOTTOM - spacing));
    }
}

void PDFAppPanelWidgets::prepareView(QAbstractItemView* view)
{
    if (!view)
    {
        return;
    }

    view->setFrameShape(QFrame::NoFrame);
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view->setAttribute(Qt::WA_MacShowFocusRect, false);
    PDFWidgetStyleProperties::set(view, PDFWidgetStyleProperties::OVERLAY_SCROLL_BARS);

    // The rows lie on the background of the sidebar (sidebar-content 195:2470)
    view->viewport()->setAutoFillBackground(false);
    view->setAutoFillBackground(false);
}

PDFLineEdit* PDFAppPanelWidgets::createSearchField(QWidget* parent, const QString& placeholder, const QString& objectName)
{
    PDFLineEdit* field = new PDFLineEdit(parent);
    field->setObjectName(objectName);
    field->setSearchField();
    field->setPlaceholderText(placeholder);
    field->setAccessibleName(placeholder);
    field->setClearButtonEnabled(true);
    field->setMaxLength(1024);
    PDFWidgetStyleProperties::set(field, PDFWidgetStyleProperties::FIELD_HEIGHT, SEARCH_FIELD_HEIGHT);
    return field;
}

QToolButton* PDFAppPanelWidgets::createHeaderButton(QWidget* parent, const QString& iconName, const QString& toolTip, const QString& objectName)
{
    QToolButton* button = new QToolButton(parent);
    button->setObjectName(objectName);
    PDFWidgetStyleProperties::set(button, PDFWidgetStyleProperties::SIDEBAR_TAB);
    button->setAutoRaise(true);
    button->setIcon(PDFThemeIcons::getIcon(iconName));
    button->setIconSize(QSize(18, 18));
    button->setFixedSize(32, 32);
    button->setToolTip(toolTip);
    button->setAccessibleName(toolTip);
    button->setFocusPolicy(Qt::TabFocus);
    return button;
}

PDFAppDocumentTab* PDFAppPanelWidgets::findDocumentTab(const QWidget* widget)
{
    for (const QWidget* current = widget; current; current = current->parentWidget())
    {
        if (PDFAppDocumentTab* tab = qobject_cast<PDFAppDocumentTab*>(const_cast<QWidget*>(current)))
        {
            return tab;
        }
    }
    return nullptr;
}

PDFAppMainWindow* PDFAppPanelWidgets::findMainWindow(const QWidget* widget)
{
    for (const QWidget* current = widget; current; current = current->parentWidget())
    {
        if (PDFAppMainWindow* window = qobject_cast<PDFAppMainWindow*>(const_cast<QWidget*>(current)))
        {
            return window;
        }
    }
    return nullptr;
}

void PDFAppPanelWidgets::navigate(const QWidget* widget, const PDFAppRoute& route)
{
    PDFAppMainWindow* window = findMainWindow(widget);
    PDFAppNavigator* navigator = window ? window->getNavigator() : nullptr;
    if (navigator)
    {
        navigator->navigate(route, PDFAppNavigationMode::Push, findDocumentTab(widget));
    }
}

QString PDFAppPanelWidgets::getPageLabel(const PDFCatalog* catalog, PDFInteger pageIndex)
{
    const QString label = catalog ? catalog->getPageLabel(pageIndex) : QString();
    return label.isEmpty() ? QString::number(pageIndex + 1) : label;
}

QString PDFAppPanelWidgets::getPageTitle(const PDFCatalog* catalog, PDFInteger pageIndex)
{
    return QCoreApplication::translate("pdf::PDFAppPanelWidgets", "Page %1").arg(getPageLabel(catalog, pageIndex));
}

PDFInteger PDFAppPanelWidgets::getDestinationPage(const PDFCatalog* catalog, const PDFDestination& destination)
{
    if (!catalog)
    {
        return -1;
    }

    const PDFDestination* target = &destination;
    if (destination.getDestinationType() == DestinationType::Named)
    {
        target = catalog->getNamedDestination(destination.getName());
        if (!target || target->getDestinationType() == DestinationType::Named)
        {
            return -1;
        }
    }

    if (target->getDestinationType() == DestinationType::Invalid)
    {
        return -1;
    }

    const PDFInteger pageCount = PDFInteger(catalog->getPageCount());
    if (target->getPageReference().isValid())
    {
        const size_t pageIndex = catalog->getPageIndexFromPageReference(target->getPageReference());
        return (pageIndex != PDFCatalog::INVALID_PAGE_INDEX && PDFInteger(pageIndex) < pageCount) ? PDFInteger(pageIndex) : -1;
    }

    // A destination by the page number (index from 0)
    const PDFInteger pageIndex = target->getPageIndex();
    return (pageIndex >= 0 && pageIndex < pageCount) ? pageIndex : -1;
}

PDFInteger PDFAppPanelWidgets::getActionPage(const PDFCatalog* catalog, const PDFAction* action)
{
    if (!action)
    {
        return -1;
    }

    for (const PDFAction* current : action->getActionList())
    {
        if (current && current->getType() == ActionType::GoTo)
        {
            const PDFActionGoTo* goTo = static_cast<const PDFActionGoTo*>(current);
            const PDFInteger page = getDestinationPage(catalog, goTo->getDestination());
            if (page >= 0)
            {
                return page;
            }
        }
    }
    return -1;
}

void PDFAppPanelWidgets::prepareMessageBox(PDFMessageBox* box, const QWidget* widget, const QString& objectName)
{
    if (!box)
    {
        return;
    }

    box->setObjectName(objectName);
    box->setAttribute(Qt::WA_DeleteOnClose);
    PDFAppMainWindow::prepareDialog(box, widget);
}

}   // namespace pdf

#include "moc_pdfappdocumentpanels_common.cpp"
