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

#ifndef PDFAPPSETTINGSPAGE_H
#define PDFAPPSETTINGSPAGE_H

#include "pdfappglobal.h"
#include "pdfapppagelayout.h"
#include "pdfapprestoredefaultsdialog.h"
#include "pdfappsettingssection.h"

#include "pdfcomponentbadges.h"
#include "pdfcomponentrows.h"
#include "pdffluentwidgets.h"

#include <QList>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>
#include <QTimer>

#include <map>

namespace pdf
{

class PDFAppParagraph;
class PDFAppRestoreDefaultsDialog;
class PDFAppSettingRow;
class PDFAppSettingsSection;
class PDFIconBadge;
class PDFLineEdit;
class PDFNavItem;
class PDFNavList;

/// Header of a section of Settings (146:3329 section-header): the icon circle 44
/// (accent/blue/bg, the icon 22 accent/blue/icon), the title Bold 22 with the
/// subtitle Regular 13 text/tertiary (gap 2) and the button Restore defaults
/// (Secondary, icon rotate-ccw) at the trailing side; gap 14
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSettingsSectionHeader : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAppSettingsSectionHeader(const QString& iconName, const QString& title, const QString& subtitle, bool hasRestoreButton, QWidget* parent = nullptr);
    virtual ~PDFAppSettingsSectionHeader() override;

    PDFIconBadge* getIconBadge() const { return m_iconBadge; }
    PDFAppParagraph* getTitleLabel() const { return m_title; }
    PDFAppParagraph* getSubtitleLabel() const { return m_subtitle; }

    /// Restore defaults (nullptr for a header without it)
    QPushButton* getRestoreButton() const { return m_restoreButton; }

private:
    QPointer<PDFIconBadge> m_iconBadge;
    QPointer<PDFAppParagraph> m_title;
    QPointer<PDFAppParagraph> m_subtitle;
    QPointer<QPushButton> m_restoreButton;
};

/// Page Settings (step 5, plan 2.1; frames 146:2843 … 147:4565, 941:138200):
///  - /settings/{section}#anchor: the settings menu (title, Search settings…, the
///    card of the Settings nav items of PDFAppSettingsSectionRegistry) and the
///    content column (the storage banner, the section header with Restore
///    defaults, the section, the status of a refused write, the footer note);
///    the anchor scrolls to its row and highlights it for 3 s, an unknown anchor
///    shows the top of the section,
///  - /settings?q=: every section with the rows matching the query below its own
///    header (no Restore defaults), the navigation shows only the sections with a
///    match and their counts; no match = the empty state with Clear search.
/// Typing into the search field navigates once to /settings?q= (Push) after
/// SEARCH_DELAY ms, further typing replaces the route; Esc or an emptied field goes
/// back. A nav item navigates to its section (Push). The page has no header of the
/// workspace (hasSearchHeader). Values are written by the binders of the rows;
/// pending values are written, when the page is hidden or destroyed.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSettingsPage : public PDFAppContentPage
{
    Q_OBJECT

private:
    using BaseClass = PDFAppContentPage;

public:
    /// Width of the settings menu (146:3066)
    static constexpr int MENU_WIDTH = 300;

    /// Narrowest content column beside the menu (narrower pages stack the columns)
    static constexpr int MINIMUM_CONTENT_WIDTH = 480;

    /// Rest of the search field before the filter is applied
    static constexpr int SEARCH_DELAY = 200;

    /// Time of a status message (a refused write)
    static constexpr int STATUS_DURATION = 6000;

    /// Longest text of a filter (the length of a query of the search)
    static constexpr int MAXIMUM_QUERY_LENGTH = 256;

    explicit PDFAppSettingsPage(PDFAppContext* context, QWidget* parent = nullptr);
    virtual ~PDFAppSettingsPage() override;

    virtual bool setRoute(const PDFAppRoute& route) override;
    virtual QString getTitle() const override;
    virtual bool hasSearchHeader() const override;
    virtual QBoxLayout* getBannerLayout() const override;

    /// Shown section (empty in the filtered page)
    const QString& getSectionId() const { return m_sectionId; }

    /// Query of the filtered page (empty for a section)
    const QString& getQuery() const { return m_query; }
    bool isFiltering() const { return !m_query.isEmpty(); }

    /// Parts of the page (tests, packages)
    PDFAppColumns* getColumns() const { return m_columns; }
    PDFLineEdit* getSearchField() const { return m_searchField; }
    PDFNavList* getNavigation() const { return m_navigation; }
    PDFNavItem* getNavigationItem(const QString& sectionId) const;
    PDFAppSettingsSectionHeader* getSectionHeader() const { return m_sectionHeader; }
    QPushButton* getRestoreButton() const;
    PDFAppParagraph* getFooterNote() const { return m_footerNote; }
    PDFAppParagraph* getStatusLabel() const { return m_statusLabel; }
    QWidget* getEmptyState() const { return m_emptyState; }
    QPushButton* getClearSearchButton() const { return m_clearSearchButton; }
    PDFAppRestoreDefaultsDialog* getRestoreDialog() const { return m_restoreDialog; }

    /// Shown section (nullptr in the filtered page)
    PDFAppSettingsSection* getSection() const;

    /// Sections of the page: the shown section, or all sections of the filter
    /// (also those without a match - hidden)
    QList<PDFAppSettingsSection*> getSections() const;

    /// Row of the anchor in the shown sections
    PDFAppSettingRow* findRow(const QString& anchor) const;

    /// Widget of an anchor in the shown sections (a row or a custom part of a
    /// section), nullptr for an unknown anchor or a part hidden by the filter;
    /// the anchor of a route and a scroll to an anchor use it
    QWidget* findAnchorTarget(const QString& anchor) const;

    /// Shows a message below the content (a refused write) for STATUS_DURATION ms
    void showStatusMessage(const QString& message);

    /// Opens the dialog Restore defaults of the shown section (false = nothing to restore)
    bool openRestoreDefaultsDialog();

    /// Writes the pending values of the rows
    void commitPendingValues();

    /// Route of the filter of the text (sanitized and cut)
    static PDFAppRoute createFilterRoute(const QString& text);

    /// Text of a filter without control characters and lone surrogates, at most
    /// MAXIMUM_QUERY_LENGTH characters and short enough for a valid route
    static QString sanitizeQuery(const QString& text);

protected:
    virtual bool scrollToAnchor(const QString& anchor) override;
    virtual void changeEvent(QEvent* event) override;
    virtual void showEvent(QShowEvent* event) override;
    virtual void hideEvent(QHideEvent* event) override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;
    virtual bool event(QEvent* event) override;

private:
    /// What the next page of a navigation of this page focuses (the search field
    /// of a typed filter, the navigation of the arrows)
    enum class PendingFocus
    {
        None,
        Search,
        Navigation
    };

    void buildMenu();
    void buildContent();
    void rebuild();
    void showSection(const QString& sectionId, const QString& anchor);
    void showFilter(const QString& query);
    void clearContent();
    void updateNavigation();
    void updateRestoreButton();
    void updateFilter();
    void applyPendingFocus();
    bool navigateTo(const PDFAppRoute& route, PDFAppNavigationMode mode, PendingFocus focus);
    bool clearFilter();
    void connectSection(PDFAppSettingsSection* section);
    void highlightAnchor(QWidget* widget);

    void onNavigationIndexChanged(int index);
    void onSearchTextEdited(const QString& text);
    void onSearchTimeout();
    void onClearSearchClicked();
    void onRestoreClicked();
    void onRestoreDialogFinished(int result);
    void onDatabaseChanged(const QString& topic, const QString& key);
    void onEverythingChanged();
    void onStatusMessageRequested(const QString& message);
    void onSectionNavigationRequested(const PDFAppRoute& route);
    void onStatusTimeout();

    static PendingFocus s_pendingFocus;

    QString m_sectionId;
    QString m_query;
    QString m_anchor;
    PendingFocus m_focus = PendingFocus::None;
    bool m_isUpdatingNavigation = false;

    QPointer<PDFAppColumns> m_columns;
    QPointer<QWidget> m_menuColumn;
    QPointer<QWidget> m_contentColumn;
    QPointer<QVBoxLayout> m_contentLayout;
    QPointer<QWidget> m_menuBody;
    QPointer<QWidget> m_contentBody;
    QPointer<QVBoxLayout> m_bodyLayout;
    QPointer<PDFLineEdit> m_searchField;
    QPointer<PDFNavList> m_navigation;
    QPointer<PDFAppSettingsSectionHeader> m_sectionHeader;
    QList<QPointer<PDFAppSettingsSection>> m_sections;
    std::map<QString, QPointer<PDFAppSettingsSectionHeader>> m_filterHeaders;
    QPointer<QWidget> m_emptyState;
    QPointer<PDFAppParagraph> m_emptyText;
    QPointer<QPushButton> m_clearSearchButton;
    QPointer<PDFAppParagraph> m_statusLabel;
    QPointer<PDFAppParagraph> m_footerNote;
    QPointer<PDFAppRestoreDefaultsDialog> m_restoreDialog;
    QTimer m_searchTimer;
    QTimer m_statusTimer;
};

/// Factory of the page Settings (/settings, /settings/{section})
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSettingsPageFactory : public PDFAppPageFactory
{
public:
    virtual PDFAppPage* createPage(PDFAppContext* context, QWidget* parent) const override;
};

}   // namespace pdf

#endif // PDFAPPSETTINGSPAGE_H
