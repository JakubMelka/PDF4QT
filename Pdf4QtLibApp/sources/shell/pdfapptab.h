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

#ifndef PDFAPPTAB_H
#define PDFAPPTAB_H

#include "pdfappglobal.h"
#include "pdfapphistory.h"
#include "pdfapppage.h"
#include "pdfapproutetable.h"
#include "pdfappworkspace.h"

#include <QPointer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

namespace pdf
{

/// Content of a tab of the main window: its kind, its history and its current
/// page. The navigator creates the pages and gives them to the tab (setPage);
/// the tab owns the page and destroys the previous one (deleteLater, the page
/// may be the sender of the navigation).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppTab : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAppTab(PDFAppTabKind kind, PDFAppContext* context, QWidget* parent = nullptr);
    virtual ~PDFAppTab() override;

    PDFAppTabKind getKind() const { return m_kind; }

    /// Identifier of the tab in its window (never reused)
    quint64 getId() const { return m_id; }

    PDFAppHistory& getHistory() { return m_history; }
    const PDFAppHistory& getHistory() const { return m_history; }

    /// Current page (nullptr before the first navigation)
    PDFAppPage* getPage() const { return m_page; }

    /// Shows the page (the tab takes it over); the previous page is hidden and
    /// deleted later
    void setPage(PDFAppPage* page);

    /// Highlighted item of the navigation (only the Home tab has the navigation)
    virtual void setNavItem(PDFAppNavItem item);
    virtual PDFAppNavItem getNavItem() const;

    /// Title and icon of the tab in the tab bar
    virtual QString getTabTitle() const = 0;
    virtual QString getTabIconName() const = 0;

    /// The tab can be closed (the Home tab cannot)
    virtual bool isClosable() const;

    /// Field of the global search of the tab (nullptr, if the tab has none or the
    /// page hides the header)
    virtual PDFLineEdit* getSearchField() const;

    PDFAppContext* getContext() const { return m_context; }

Q_SIGNALS:
    /// A part of the tab, which is not a page (sidebar, search, banner, footer), asks for a route
    void navigationRequested(const pdf::PDFAppRoute& route, pdf::PDFAppNavigationMode mode);

    /// A part of the tab asks to open an external address
    void externalUrlRequested(const QUrl& url);

    /// The title of the page has changed
    void pageTitleChanged();

protected:
    /// Widget, which holds the page, and its layout
    virtual QWidget* getPageArea() const = 0;
    virtual QVBoxLayout* getPageLayout() const = 0;

    /// The page has been changed (header texts)
    virtual void onPageChanged();

private:
    void onPageTitleChanged();

    static quint64 s_nextId;

    PDFAppTabKind m_kind;
    quint64 m_id = 0;
    QPointer<PDFAppContext> m_context;
    PDFAppHistory m_history;
    QPointer<PDFAppPage> m_page;
};

/// Tab Home (pdf4qt-home 28:4: the sidebar, the workspace with the header, the
/// banner of the storage, the page and the footer) and tab New tab
/// (pdf4qt-new-tab 955:152339: the workspace with the header and the page only)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppWorkspaceTab : public PDFAppTab
{
    Q_OBJECT

private:
    using BaseClass = PDFAppTab;

public:
    /// \p kind is Home or NewTab (anything else is NewTab)
    explicit PDFAppWorkspaceTab(PDFAppTabKind kind, PDFAppContext* context, QWidget* parent = nullptr);
    virtual ~PDFAppWorkspaceTab() override;

    virtual void setNavItem(PDFAppNavItem item) override;
    virtual PDFAppNavItem getNavItem() const override;
    virtual QString getTabTitle() const override;
    virtual QString getTabIconName() const override;
    virtual bool isClosable() const override;
    virtual PDFLineEdit* getSearchField() const override;

    /// Parts of the tab (nullptr, if the tab has no such part)
    PDFAppSidebar* getSidebar() const { return m_sidebar; }
    PDFAppWorkspaceHeader* getHeader() const { return m_header; }
    PDFAppStorageBanner* getBanner() const { return m_banner; }
    PDFAppFooter* getFooter() const { return m_footer; }

protected:
    virtual QWidget* getPageArea() const override;
    virtual QVBoxLayout* getPageLayout() const override;
    virtual void onPageChanged() override;

private:
    void onStorageStateChanged();
    void onSearchRequested(const QString& text);
    void onTryAgainRequested();
    void onShowInFolderRequested();
    void onCheckForUpdatesRequested();

    QPointer<PDFAppSidebar> m_sidebar;
    QPointer<PDFAppWorkspaceHeader> m_header;
    QPointer<PDFAppStorageBanner> m_banner;
    QPointer<PDFAppFooter> m_footer;
    QPointer<QWidget> m_pageArea;
    QPointer<QVBoxLayout> m_pageLayout;
};

}   // namespace pdf

#endif // PDFAPPTAB_H
