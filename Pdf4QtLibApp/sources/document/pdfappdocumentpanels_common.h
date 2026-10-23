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

#ifndef PDFAPPDOCUMENTPANELS_COMMON_H
#define PDFAPPDOCUMENTPANELS_COMMON_H

#include "pdfappglobal.h"

#include "pdfglobal.h"
#include "pdffluentwidgets.h"

#include <QString>

#include <functional>

class QAbstractItemView;
class QListView;
class QToolButton;
class QWidget;

namespace pdf
{

class PDFAppDocumentTab;
class PDFAppMainWindow;
class PDFAppRoute;
class PDFAction;
class PDFCatalog;
class PDFDestination;
class PDFLineEdit;
class PDFMessageBox;

/// Native file dialogs of the navigation panels (attachments, export / import of
/// the bookmarks) for any type of file. Tests replace them by handlers.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppPanelFileDialogs
{
public:
    PDFAppPanelFileDialogs() = delete;

    /// (parent, title, proposed file or folder, filter) -> chosen file, empty = canceled
    using FileHandler = std::function<QString(QWidget*, const QString&, const QString&, const QString&)>;

    /// Save dialog: \p filePath is the proposed file; an empty text = canceled
    static QString getSaveFileName(QWidget* parent, const QString& title, const QString& filePath, const QString& filter);

    /// Open dialog of one file in the folder; an empty text = canceled
    static QString getOpenFileName(QWidget* parent, const QString& title, const QString& folder, const QString& filter);

    /// Handlers replacing the dialogs (tests); an empty handler restores them
    static void setSaveHandler(FileHandler handler);
    static void setOpenHandler(FileHandler handler);

private:
    static FileHandler& getSaveHandler();
    static FileHandler& getOpenHandler();
};

/// List of a navigation panel: PDFListView, whose paddings of the content are
/// set by the panel (the viewport margins are protected in Qt)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppPanelListView : public PDFListView
{
    Q_OBJECT

private:
    using BaseClass = PDFListView;

public:
    explicit PDFAppPanelListView(QWidget* parent);
    virtual ~PDFAppPanelListView() override;

    /// Paddings of the content around the rows
    void setContentPaddings(const QMargins& margins);
};

/// Common parts of the navigation panels (package C)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppPanelWidgets
{
public:
    PDFAppPanelWidgets() = delete;

    /// Paddings of the content of the sidebar (sidebar-content 195:1489: 2 / 12 / 16)
    static constexpr int CONTENT_PADDING_TOP = 2;
    static constexpr int CONTENT_PADDING_SIDE = 12;
    static constexpr int CONTENT_PADDING_BOTTOM = 16;

    /// Gap of the rows of the lists of the sidebar (195:2470: 2)
    static constexpr int LIST_SPACING = 2;

    /// Height of the search fields of the panels (outline-search 195:1490: 36)
    static constexpr int SEARCH_FIELD_HEIGHT = 36;

    /// Prepares a list or tree of the sidebar: no frame, scroll bars over the
    /// content, scrolled per pixel, the background of the sidebar
    static void prepareView(QAbstractItemView* view);

    /// Prepares a list of the sidebar (prepareView) with the paddings of the
    /// content of the sidebar and the gap 2 of its rows (QListView::setSpacing
    /// puts the spacing on every side of each row, so two neighbors are 2 x the
    /// spacing apart: the spacing is half of the gap and the margins are smaller
    /// by it; a PDFTreeView keeps its own margins of 6)
    static void prepareList(PDFAppPanelListView* view);

    /// Search field of a panel (component Search field 49:143, pill, 36 px)
    static PDFLineEdit* createSearchField(QWidget* parent, const QString& placeholder, const QString& objectName);

    /// Icon button of the header of a panel (32 x 32, the look of a Sidebar tab
    /// without the selection: bookmark-add 195:3845, bookmark-more 195:3849)
    static QToolButton* createHeaderButton(QWidget* parent, const QString& iconName, const QString& toolTip, const QString& objectName);

    /// Document tab of the widget (nullptr outside of a document tab)
    static PDFAppDocumentTab* findDocumentTab(const QWidget* widget);

    /// Main window of the widget (nullptr outside of a main window)
    static PDFAppMainWindow* findMainWindow(const QWidget* widget);

    /// Navigates to the route in the tab of the widget (nothing outside of a window)
    static void navigate(const QWidget* widget, const PDFAppRoute& route);

    /// "Page 12" by the page label (or the number of the page)
    static QString getPageTitle(const PDFCatalog* catalog, PDFInteger pageIndex);

    /// Label of the page ("xii", "12"); the number of the page without a label
    static QString getPageLabel(const PDFCatalog* catalog, PDFInteger pageIndex);

    /// Page of the destination (a named destination is looked up once), -1 for a
    /// destination outside of the document or an invalid one
    static PDFInteger getDestinationPage(const PDFCatalog* catalog, const PDFDestination& destination);

    /// Page of the first GoTo action of the action and of its next actions, -1 if none
    static PDFInteger getActionPage(const PDFCatalog* catalog, const PDFAction* action);

    /// Prepares a message box of a panel: deleted on close, the scrim over the
    /// body of the window
    static void prepareMessageBox(PDFMessageBox* box, const QWidget* widget, const QString& objectName);
};

}   // namespace pdf

#endif // PDFAPPDOCUMENTPANELS_COMMON_H
