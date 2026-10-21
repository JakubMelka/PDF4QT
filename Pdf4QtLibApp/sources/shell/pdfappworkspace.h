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

#include <QBoxLayout>
#include <QFrame>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSpacerItem>
#include <QTextLayout>
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

/// Wrapped plain text with the fixed line height of the design (QLabel takes the
/// line spacing of the font, 17 px for Inter 14): the text of Page Not Found
/// 14 / 21, the text of the storage banners 12 / 16. Each line is centered
/// vertically in its line box, as Figma does. The height follows the width.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppParagraph : public PDFLabel
{
    Q_OBJECT

private:
    using BaseClass = PDFLabel;

public:
    explicit PDFAppParagraph(const QFont& font, int lineHeight, PDFThemeColors::Color color, QWidget* parent = nullptr);

    int getLineHeight() const { return m_lineHeight; }

    /// Lines of the text wrapped to the width
    QStringList getLines(int width) const;

    virtual bool hasHeightForWidth() const override;
    virtual int heightForWidth(int width) const override;
    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;

private:
    int m_lineHeight = 0;
};

/// Scroll area of the shell (the page area of the workspace, the navigation of the
/// sidebar): no frame, the scroll bars of the style, the viewport and the content
/// are transparent (they never fill the palette), the content follows the size of
/// the viewport and scrolls, when it needs more space (the window has its minimal
/// size 720 x 480, newapplication.md 3)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppScrollArea : public QScrollArea
{
    Q_OBJECT

private:
    using BaseClass = QScrollArea;

public:
    /// The area takes \p content over (it may be nullptr, setContent later)
    explicit PDFAppScrollArea(QWidget* content, QWidget* parent = nullptr);
    virtual ~PDFAppScrollArea() override;

    /// Sets the content (the previous one is deleted by QScrollArea)
    void setContent(QWidget* content);

private:
    /// A child focused by the keyboard is scrolled into the view (QScrollArea
    /// does it only for Tab; the arrows of a group move the focus by setFocus)
    void onFocusChanged(QWidget* old, QWidget* now);
};

/// Sidebar of the Home tab (pdf4qt-home 28:4, 210 px): the tile and the name of
/// the application, the navigation in three groups and the switch of the color
/// scheme Light / Dark / Auto. The switch is the setting appearance.colorScheme
/// (the same as Settings › General › Color scheme): a choice writes the database,
/// PDFAppContext applies it, and a change by another instance (notifier) moves
/// the switch. A click on an item of the navigation asks for its route. The
/// navigation scrolls in a low window, the switch stays at the bottom (8 px from
/// the edge of the window, owner decision O11).
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
    PDFAppScrollArea* getNavigationArea() const { return m_navigationArea; }
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
    QPointer<PDFAppScrollArea> m_navigationArea;
    QPointer<PDFSegmentedControl> m_themeSwitch;
    bool m_isUpdating = false;
};

/// Link of a breadcrumb (crumb of pdf4qt-help 955:152873): Medium 12 text/accent,
/// underlined under the pointer, the focus ring outside (radius 4)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppCrumbLink : public PDFComponentButton
{
    Q_OBJECT

private:
    using BaseClass = PDFComponentButton;

public:
    explicit PDFAppCrumbLink(const QString& text, QWidget* parent = nullptr);

    static QFont getCrumbFont(bool isUnderlined = false);

protected:
    virtual void paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const override;
    virtual QSize getContentSizeHint() const override;
};

/// Breadcrumb of a page (Help > Protect & Sign > Sign with Certificate): links of
/// the parents, separators text/tertiary and the current page text/tertiary,
/// all Medium 12, gaps 6. A click on a link emits linkActivated with its route.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppBreadcrumb : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAppBreadcrumb(QWidget* parent = nullptr);

    /// Shows the items (a link for an item with a valid route, the current page
    /// otherwise); the previous parts are deleted later (a part may be the sender)
    void setItems(const QList<PDFAppBreadcrumbItem>& items);

    /// Adds a link to the route (a route of the application)
    void addLink(const QString& text, const QString& route);

    /// Adds the current page (the last part, not a link)
    void addCurrent(const QString& text);

    /// Links of the breadcrumb and their routes
    QList<PDFAppCrumbLink*> getLinks() const;
    QString getRoute(const PDFAppCrumbLink* link) const;

Q_SIGNALS:
    void linkActivated(const QString& route);

private:
    void addSeparator();
    void onLinkClicked();
    void clear();

    QPointer<QHBoxLayout> m_layout;
    QList<QPointer<PDFAppCrumbLink>> m_links;
    QStringList m_routes;
};

/// Text of the header of the workspace (the title, the subtitle): one line, elided
/// at its end to the width of the header (PDFComponentPainter::drawElidedText, the
/// header has the fixed height of the frames, a narrow window never wraps it); the
/// whole text is the tooltip of an elided text. A part of the text may be stressed
/// (the subtitle of Home 28:35: "PDF" Semi Bold in text/accent). The colors are
/// tokens read at paint time and the text is never a rich text; an elided text is
/// drawn without the stress.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppHeaderText : public PDFLabel
{
    Q_OBJECT

private:
    using BaseClass = PDFLabel;

public:
    explicit PDFAppHeaderText(const QFont& font, PDFThemeColors::Color color, QWidget* parent = nullptr);
    virtual ~PDFAppHeaderText() override;

    /// Sets the text and its stressed part (the first occurrence of \p accent in
    /// the text; empty or not found = none)
    void setHeaderText(const QString& text, const QString& accent = QString());

    /// Index of the stressed part in the text (-1 = none) and its length
    qsizetype getAccentIndex() const;
    qsizetype getAccentLength() const { return getAccentIndex() >= 0 ? m_accent.size() : 0; }

    /// Width of the whole text with its stressed part
    qreal getTextWidth() const;

    /// The text is elided at the current width
    bool isElided() const;

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual bool event(QEvent* event) override;

private:
    QList<QTextLayout::FormatRange> getFormats() const;

    QString m_accent;
};

/// Header of the workspace (workspace-header): the breadcrumbs (optional), a title Bold 28 and a subtitle
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

    /// Texts of the page (empty = the line is hidden); \p subtitleAccent is the
    /// stressed part of the subtitle (PDFAppPage::getHeaderSubtitleAccent)
    void setTexts(const QString& title, const QString& subtitle, const QString& subtitleAccent = QString());

    PDFLineEdit* getSearchField() const { return m_searchField; }
    PDFAppHeaderText* getTitleLabel() const { return m_title; }
    PDFAppHeaderText* getSubtitleLabel() const { return m_subtitle; }
    PDFAppBreadcrumb* getBreadcrumb() const { return m_breadcrumb; }

    /// Breadcrumbs above the title (empty = hidden)
    void setBreadcrumbs(const QList<PDFAppBreadcrumbItem>& items);

Q_SIGNALS:
    /// Enter in the search field with a non-empty text
    void searchRequested(const QString& text);

    /// A link of the breadcrumbs has been clicked
    void breadcrumbActivated(const pdf::PDFAppRoute& route);

private:
    void onReturnPressed();
    void onBreadcrumbActivated(const QString& route);

    QPointer<PDFAppBreadcrumb> m_breadcrumb;
    QPointer<PDFAppHeaderText> m_title;
    QPointer<PDFAppHeaderText> m_subtitle;
    QPointer<PDFLineEdit> m_searchField;
};

/// Banner of the state of the storage at the top of the content of every page of
/// the Home tab (pdf4qt-settings-general-portable 941:138200, pdf4qt-settings-
/// storage-banners 958:28036): portable mode (blue), the database could not be
/// opened (amber, Try Again, Show in Folder), the database of a newer version
/// (amber, Check for Updates) and the damaged database, which was replaced
/// (amber, Show in Folder, Dismiss - hides it for the rest of the run). Nothing
/// is shown, when the database works. The buttons stand beside the text; when
/// they would leave the text narrower than MIN_TEXT_WIDTH (a narrow window), they
/// move below the text, so the text never becomes a column of single words.
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

    /// Narrowest text beside the buttons, a narrower banner has them below the text
    static constexpr int MIN_TEXT_WIDTH = 320;

    /// Kind of the banner of the state of the storage
    static Kind getKind(const PDFDatabaseState& state);

    /// The buttons are below the text (the banner is narrow)
    bool isActionsBelowText() const { return m_isActionsBelowText; }

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
    virtual void resizeEvent(QResizeEvent* event) override;

private:
    void onDismissClicked();
    void updateContent();
    void updateActionsPlacement();
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

    /// The buttons (beside the text in the main layout, or below it in the text layout)
    QPointer<QWidget> m_actions;
    QPointer<QHBoxLayout> m_mainLayout;
    QPointer<QVBoxLayout> m_textLayout;
    bool m_isActionsBelowText = false;
};

/// Footer of the Home tab (app-footer): "Free • Open Source • MIT License •
/// Version x" with the tag of the storage (Portable, Not saved, Read only) and
/// the targets Windows / Linux / macOS, divided from the page by a line. The
/// footer holds the bottom padding of the workspace (16 px, owner decision O11),
/// so the row of the texts is centered 24 px above the bottom edge - on the axis
/// of the switch of the sidebar (8 px + 32 / 2). The tag (21 px) is centered on
/// the text and reaches into the paddings, the height of the footer does not
/// change with it. The targets are hidden, when the row is too narrow for them
/// (minimal window).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppFooter : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAppFooter(QWidget* parent = nullptr);
    virtual ~PDFAppFooter() override;

    /// Padding above the texts (the line lies inside of it) and below the row (the
    /// bottom padding of the workspace)
    static constexpr int PADDING = 16;

    /// The MIT License link (Text link, 16 px) is 1 px higher than the texts (15 px,
    /// one text of 15 px in app-footer 28:217): it reaches 1 px into the padding
    /// above the texts, so the footer is 16 + 15 + 16 = 47 px as the frames
    static constexpr int LINK_OVERHANG = 1;

    /// Gap between the version and the tag
    static constexpr int TAG_GAP = 8;

    /// Address of the license (MIT License link)
    static QUrl getLicenseUrl();

    /// Version as it is shown to the user: three parts (1.6.0 of the project
    /// version 1.6.0.0, newapp_whatsnew.md 2), missing parts are zeros; a text,
    /// which is not a version, is returned trimmed to 32 characters
    static QString getDisplayVersion(const QString& version);
    static QString getDisplayVersion();

    /// Shows the tag of the state of the storage
    void setState(const PDFDatabaseState& state);

    /// Text of the tag (empty when it is hidden)
    QString getTagText() const;

    PDFTextLink* getLicenseLink() const { return m_licenseLink; }
    PDFLabel* getVersionLabel() const { return m_versionLabel; }
    PDFTag* getTag() const { return m_tag; }

    /// Widget of the targets Windows / Linux / macOS (its labels are hidden, when
    /// it is narrower than they need)
    QWidget* getTargets() const { return m_targets; }

Q_SIGNALS:
    void externalUrlRequested(const QUrl& url);

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual bool event(QEvent* event) override;

private:
    void onLicenseClicked();
    void updateTagSpace();
    void updateTexts();
    void updateTag();
    PDFLabel* createSeparator();
    void placeTag();

    QPointer<PDFLabel> m_freeLabel;
    QPointer<PDFTextLink> m_licenseLink;
    QPointer<PDFLabel> m_versionLabel;
    QPointer<PDFTag> m_tag;
    QPointer<QWidget> m_targets;

    /// Place of the tag in the layout (owned by the layout, checked by indexOf)
    QSpacerItem* m_tagSpacer = nullptr;

    /// Mode of the storage shown by the tag (its text is translated again)
    PDFDatabaseStorageMode m_storageMode = PDFDatabaseStorageMode::Disk;
};

}   // namespace pdf

#endif // PDFAPPWORKSPACE_H
