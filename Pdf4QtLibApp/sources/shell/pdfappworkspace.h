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

#ifndef PDFAPPWORKSPACE_H
#define PDFAPPWORKSPACE_H

#include "pdfappglobal.h"
#include "pdfapppage.h"
#include "pdfapproutetable.h"
#include "pdfappcontext.h"
#include "pdfdatabase.h"

#include "pdfcomponentbuttons.h"
#include "pdfcomponentrows.h"
#include "pdffluentwidgets.h"

#include <QFrame>
#include <QPointer>
#include <QPushButton>
#include <QUrl>
#include <QWidget>

namespace pdf
{

/// Tile of the application in the sidebar (logo-icon-bg of pdf4qt-home 28:4):
/// bg/accent 36 x 36 with the radius 8 and the icon file-text 20 in icon/on-accent
class PDF4QTLIBAPPSHARED_EXPORT PDFAppLogoTile : public QWidget
{
    Q_OBJECT

public:
    explicit PDFAppLogoTile(QWidget* parent = nullptr);

    virtual QSize sizeHint() const override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;
};

/// Sidebar of the Home tab (pdf4qt-home 28:4, 210 px): the tile and the name of
/// the application, the navigation in three groups and the switch of the color
/// scheme Light / Dark / Auto. The switch is the setting appearance.colorScheme
/// (the same as Settings › General › Color scheme): a choice writes the database,
/// PDFAppContext applies it, and a change by another instance (notifier) moves
/// the switch. A click on an item of the navigation asks for its route.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSidebar : public QFrame
{
    Q_OBJECT

private:
    using BaseClass = QFrame;

public:
    static constexpr int WIDTH = 210;

    explicit PDFAppSidebar(PDFAppContext* context, QWidget* parent = nullptr);
    virtual ~PDFAppSidebar() override;

    /// Highlighted item (None = no item, e.g. Page Not Found)
    PDFAppNavItem getCurrentItem() const;
    void setCurrentItem(PDFAppNavItem item);

    /// Route of the item of the navigation (an invalid route for None)
    static PDFAppRoute getRoute(PDFAppNavItem item);

    PDFNavList* getNavigation() const { return m_navigation; }
    PDFSegmentedControl* getThemeSwitch() const { return m_themeSwitch; }

    /// Index of the segment of the color scheme (Light 0, Dark 1, Auto 2)
    static int getSchemeIndex(const QString& colorScheme);
    static QString getSchemeValue(int index);

Q_SIGNALS:
    void navigationRequested(const pdf::PDFAppRoute& route, pdf::PDFAppNavigationMode mode);

private:
    void onNavigationIndexChanged(int index);
    void onThemeIndexChanged(int index);
    void onSettingChanged(const QString& topic, const QString& key);
    void onEverythingChanged();
    void updateThemeSwitch();

    QPointer<PDFAppContext> m_context;
    QPointer<PDFNavList> m_navigation;
    QPointer<PDFSegmentedControl> m_themeSwitch;
    bool m_isUpdating = false;
};

/// Header of the workspace (workspace-header): a title Bold 28 and a subtitle
/// Regular 14 (both optional) and the global search field 320 px "Search tools,
/// documents, or help..." (Ctrl+K). Enter asks for the search of the text.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppWorkspaceHeader : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAppWorkspaceHeader(QWidget* parent = nullptr);
    virtual ~PDFAppWorkspaceHeader() override;

    /// Texts of the page (empty = the line is hidden)
    void setTexts(const QString& title, const QString& subtitle);

    PDFLineEdit* getSearchField() const { return m_searchField; }
    PDFLabel* getTitleLabel() const { return m_title; }
    PDFLabel* getSubtitleLabel() const { return m_subtitle; }

Q_SIGNALS:
    /// Enter in the search field with a non-empty text
    void searchRequested(const QString& text);

private:
    void onReturnPressed();

    QPointer<PDFLabel> m_title;
    QPointer<PDFLabel> m_subtitle;
    QPointer<PDFLineEdit> m_searchField;
};

/// Banner of the state of the storage at the top of the content of every page of
/// the Home tab (pdf4qt-settings-general-portable 941:138200, pdf4qt-settings-
/// storage-banners 958:28036): portable mode (blue), the database could not be
/// opened (amber, Try Again, Show in Folder), the database of a newer version
/// (amber, Check for Updates) and the damaged database, which was replaced
/// (amber, Show in Folder, Dismiss - hides it for the rest of the run). Nothing
/// is shown, when the database works.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppStorageBanner : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    enum class Kind
    {
        None,       ///< The database works, no banner
        Portable,   ///< Portable mode
        Fallback,   ///< The database could not be opened (memory database)
        ReadOnly,   ///< The database has a newer schema (read only copy in memory)
        Replaced    ///< The damaged database was renamed and a new one created
    };
    Q_ENUM(Kind)

    explicit PDFAppStorageBanner(QWidget* parent = nullptr);
    virtual ~PDFAppStorageBanner() override;

    /// Kind of the banner of the state of the storage
    static Kind getKind(const PDFDatabaseState& state);

    /// Shows the banner of the state (hidden for Kind::None and a dismissed banner)
    void setState(const PDFDatabaseState& state);

    /// Kind of the shown banner (None, when it is hidden)
    Kind getKind() const;

    /// Texts of the banner
    QString getTitle() const;
    QString getText() const;

    /// Buttons of the banner (nullptr, if the kind has no such button)
    QPushButton* getTryAgainButton() const { return m_tryAgainButton; }
    QPushButton* getShowInFolderButton() const { return m_showInFolderButton; }
    QPushButton* getCheckForUpdatesButton() const { return m_checkForUpdatesButton; }
    QPushButton* getDismissButton() const { return m_dismissButton; }

Q_SIGNALS:
    void tryAgainRequested();
    void showInFolderRequested();
    void checkForUpdatesRequested();

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

private:
    void onDismissClicked();
    void updateContent();
    QPushButton* createButton(const QString& text);

    PDFDatabaseState m_state;
    Kind m_kind = Kind::None;
    QString m_dismissedFile;
    QPointer<PDFLabel> m_titleLabel;
    QPointer<PDFLabel> m_textLabel;
    QPointer<QPushButton> m_tryAgainButton;
    QPointer<QPushButton> m_showInFolderButton;
    QPointer<QPushButton> m_checkForUpdatesButton;
    QPointer<QPushButton> m_dismissButton;
};

/// Footer of the Home tab (app-footer): "Free • Open Source • MIT License •
/// Version x" with the tag of the storage (Portable, Not saved, Read only) and
/// the targets Windows / Linux / macOS, divided from the page by a line.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppFooter : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAppFooter(QWidget* parent = nullptr);
    virtual ~PDFAppFooter() override;

    /// Address of the license (MIT License link)
    static QUrl getLicenseUrl();

    /// Shows the tag of the state of the storage
    void setState(const PDFDatabaseState& state);

    /// Text of the tag (empty when it is hidden)
    QString getTagText() const;

    PDFTextLink* getLicenseLink() const { return m_licenseLink; }
    PDFLabel* getVersionLabel() const { return m_versionLabel; }

Q_SIGNALS:
    void externalUrlRequested(const QUrl& url);

protected:
    virtual void paintEvent(QPaintEvent* event) override;

private:
    void onLicenseClicked();

    QPointer<PDFTextLink> m_licenseLink;
    QPointer<PDFLabel> m_versionLabel;
    QPointer<PDFTag> m_tag;
};

}   // namespace pdf

#endif // PDFAPPWORKSPACE_H
