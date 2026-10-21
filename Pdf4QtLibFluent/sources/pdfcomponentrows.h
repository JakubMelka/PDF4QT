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

#ifndef PDFCOMPONENTROWS_H
#define PDFCOMPONENTROWS_H

#include "pdffluentglobal.h"
#include "pdfcomponentbuttons.h"
#include "pdfcomponentcards.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QFont>
#include <QPointer>
#include <QTimer>

class QContextMenuEvent;
class QLineEdit;
class QVBoxLayout;

namespace pdf
{

/// Common base of the rows of the lists of the design (Link row, Recent file
/// row, Tool row, ...). A row is a button: the whole row activates its item,
/// Space and Enter activate it from the keyboard, its title is the text of the
/// button (the accessible name) and its subtitle the accessible description.
/// The focus ring of 2 px lies inside of the row (rows lie next to each other
/// and a list clips them). Nested widgets (star, menu button, action buttons)
/// are placed at the trailing side by \p addTrailingWidget, by default as a
/// row 12 px from the trailing edge with gaps of 12 px.
///
/// A row has no surface of its own: the hover and the press are the overlays
/// bg/hover and bg/pressed, the row lies on a bg/surface card (components.md, 2.4).
///
/// The nested widgets of the rows are owned by the row. The getters return them
/// for the tests, the styling and the accessibility; a nested widget deleted or
/// moved to another parent by the application is forgotten (the getter returns
/// nullptr, the row draws and behaves without it).
class PDF4QTLIBFLUENTSHARED_EXPORT PDFRowButton : public PDFComponentButton
{
    Q_OBJECT

private:
    using BaseClass = PDFComponentButton;

public:
    explicit PDFRowButton(QWidget* parent = nullptr);
};

/// Row of a list card (component Link row 49:190): an icon, a title with a
/// subtitle and a chevron. The whole row activates the link. The texts are not
/// elided in the design, the minimal width of the row holds them.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFLinkRow : public PDFRowButton
{
    Q_OBJECT

private:
    using BaseClass = PDFRowButton;

public:
    explicit PDFLinkRow(QWidget* parent = nullptr);
    explicit PDFLinkRow(const QString& title, const QString& subtitle, const QString& iconName, QWidget* parent = nullptr);

    /// Title of the row (the text of the button and its accessible name)
    QString getTitle() const { return text(); }
    void setTitle(const QString& title);

    /// Subtitle (also the accessible description)
    const QString& getSubtitle() const { return m_row.subtitle; }
    void setSubtitle(const QString& subtitle);

    const QString& getIconName() const { return m_row.iconName; }
    void setIconName(const QString& iconName);

    /// Color of the icon of the item (icon/default by default; an invalid value
    /// of the enumeration resets it)
    std::optional<PDFThemeColors::Color> getIconColor() const { return m_row.iconColor; }
    void setIconColor(PDFThemeColors::Color color);
    void resetIconColor();

    /// Returns the content drawn by PDFComponentPainter::paintLinkRow
    PDFComponentPainter::LinkRow getContent() const;

protected:
    virtual void paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const override;
    virtual QSize getContentSizeHint() const override;
    virtual QSize getContentMinimumSizeHint() const override;

private:
    PDFComponentPainter::LinkRow m_row;
};

/// Row of the list of the recent files (component Recent file row 49:280). The
/// name of the file is elided in the middle and keeps its extension; a missing
/// file has a dimmed name and icon and the badge "No longer exists" instead of
/// the time. The star (24, pins the file) and the menu button are nested buttons:
/// a click on them does not open the file. A right click shows the menu as well,
/// the context menu key shows it below the menu button. The accessible
/// description is the folder and the time ("No longer exists" and the folder of
/// a missing file).
class PDF4QTLIBFLUENTSHARED_EXPORT PDFRecentFileRow : public PDFRowButton
{
    Q_OBJECT
    Q_PROPERTY(bool pinned READ isPinned WRITE setPinned NOTIFY pinnedChanged)

private:
    using BaseClass = PDFRowButton;

public:
    using Status = PDFComponentPainter::FileStatus;

    explicit PDFRecentFileRow(QWidget* parent = nullptr);
    explicit PDFRecentFileRow(const QString& fileName, const QString& path, const QString& time, QWidget* parent = nullptr);

    /// Name of the file (the text of the button and its accessible name)
    QString getFileName() const { return text(); }
    void setFileName(const QString& fileName);

    /// Folder of the file (also a part of the accessible description)
    const QString& getPath() const { return m_row.path; }
    void setPath(const QString& path);

    /// Time of the last opening (also a part of the accessible description)
    const QString& getTime() const { return m_row.time; }
    void setTime(const QString& time);

    Status getStatus() const { return m_row.status; }
    void setStatus(Status status);

    /// The star is shown (also, when the application hides the star button itself)
    bool isStarVisible() const;
    void setStarVisible(bool isVisible);

    bool isPinned() const;
    void setPinned(bool isPinned);

    /// Menu of the row (not owned): shown by the menu button and by a right click
    QMenu* getMenu() const;
    void setMenu(QMenu* menu);

    PDFStarButton* getStarButton() const { return m_star.data(); }
    PDFMoreButton* getMenuButton() const { return m_moreButton.data(); }

    /// Returns the content drawn by PDFComponentPainter::paintRecentFileRow
    PDFComponentPainter::RecentFileRow getContent() const;

Q_SIGNALS:
    /// The file has been pinned or unpinned by the star or by \p setPinned (only on a real change)
    void pinnedChanged(bool isPinned);

    /// The menu has been requested (menu button, right click or the context menu
    /// key), before the menu is shown
    void menuRequested();

protected:
    virtual void paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const override;
    virtual QSize getContentSizeHint() const override;
    virtual QSize getContentMinimumSizeHint() const override;
    virtual void contextMenuEvent(QContextMenuEvent* event) override;
    virtual void changeEvent(QEvent* event) override;
    virtual void requestMoreButtonMenu() override;

private:
    void updateDescription();

    PDFComponentPainter::RecentFileRow m_row;
    QPointer<PDFStarButton> m_star;
    QPointer<PDFMoreButton> m_moreButton;
    QString m_description;
};

/// Recently used tool on the Home screen (component Recent tool row 50:279):
/// the badge of the tool, its name and the time of its last use, a chevron.
/// The default hue is the one of the default variant of the design (Amber).
class PDF4QTLIBFLUENTSHARED_EXPORT PDFRecentToolRow : public PDFRowButton
{
    Q_OBJECT

private:
    using BaseClass = PDFRowButton;

public:
    explicit PDFRecentToolRow(QWidget* parent = nullptr);
    explicit PDFRecentToolRow(const QString& name, const QString& lastUsed, const QString& iconName, PDFAccentHue hue, QWidget* parent = nullptr);

    QString getName() const { return text(); }
    void setName(const QString& name);

    /// Time of the last use (also the accessible description)
    const QString& getLastUsed() const { return m_row.lastUsed; }
    void setLastUsed(const QString& lastUsed);

    const QString& getIconName() const { return m_row.iconName; }
    void setIconName(const QString& iconName);

    /// Hue of the category of the tool (an invalid value is Neutral)
    PDFAccentHue getHue() const { return m_row.hue; }
    void setHue(PDFAccentHue hue);

    PDFComponentPainter::RecentToolRow getContent() const;

protected:
    virtual void paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const override;
    virtual QSize getContentSizeHint() const override;
    virtual QSize getContentMinimumSizeHint() const override;

private:
    PDFComponentPainter::RecentToolRow m_row;
};

/// Saved workspace (component Workspace row 64:544): the badge (Violet with the
/// icon layers, the fixed look of a workspace in the design, changed by
/// \p setIconName and \p setHue), the name, the meta text and the menu button.
/// A click opens the workspace, the menu button and a right click show its menu.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFWorkspaceRow : public PDFRowButton
{
    Q_OBJECT

private:
    using BaseClass = PDFRowButton;

public:
    explicit PDFWorkspaceRow(QWidget* parent = nullptr);
    explicit PDFWorkspaceRow(const QString& name, const QString& meta, QWidget* parent = nullptr);

    QString getName() const { return text(); }
    void setName(const QString& name);

    /// Meta text ("4 documents · Opened Yesterday 16:15", also the accessible description)
    const QString& getMeta() const { return m_row.meta; }
    void setMeta(const QString& meta);

    const QString& getIconName() const { return m_row.iconName; }
    void setIconName(const QString& iconName);

    PDFAccentHue getHue() const { return m_row.hue; }
    void setHue(PDFAccentHue hue);

    QMenu* getMenu() const;
    void setMenu(QMenu* menu);

    PDFMoreButton* getMenuButton() const { return m_moreButton.data(); }

    PDFComponentPainter::WorkspaceRow getContent() const;

Q_SIGNALS:
    /// The menu has been requested (menu button, right click or the context menu
    /// key), before the menu is shown
    void menuRequested();

protected:
    virtual void paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const override;
    virtual QSize getContentSizeHint() const override;
    virtual QSize getContentMinimumSizeHint() const override;
    virtual void contextMenuEvent(QContextMenuEvent* event) override;
    virtual void requestMoreButtonMenu() override;

private:
    PDFComponentPainter::WorkspaceRow m_row;
    QPointer<PDFMoreButton> m_moreButton;
};

/// Tool of the catalog (component Tool row 89:1330): the badge, the name and the
/// description, an optional tag ("Advanced", a label, the mouse passes through
/// it) and the star (28), which pins the tool. The whole row launches the tool.
/// A row narrower than its minimal width drops the tag (as the painter does).
class PDF4QTLIBFLUENTSHARED_EXPORT PDFToolRow : public PDFRowButton
{
    Q_OBJECT
    Q_PROPERTY(bool pinned READ isPinned WRITE setPinned NOTIFY pinnedChanged)

private:
    using BaseClass = PDFRowButton;

public:
    explicit PDFToolRow(QWidget* parent = nullptr);
    explicit PDFToolRow(const QString& name, const QString& description, const QString& iconName, PDFAccentHue hue, QWidget* parent = nullptr);

    QString getName() const { return text(); }
    void setName(const QString& name);

    /// Description of the tool (also the accessible description)
    const QString& getDescription() const { return m_row.description; }
    void setDescription(const QString& description);

    const QString& getIconName() const { return m_row.iconName; }
    void setIconName(const QString& iconName);

    PDFAccentHue getHue() const { return m_row.hue; }
    void setHue(PDFAccentHue hue);

    /// Text of the tag; an empty text hides the tag
    const QString& getTag() const { return m_row.tag; }
    void setTag(const QString& tag);

    bool isPinned() const;
    void setPinned(bool isPinned);

    PDFStarButton* getStarButton() const { return m_star.data(); }
    PDFTag* getTagWidget() const { return m_tag.data(); }

    PDFComponentPainter::ToolRow getContent() const;

Q_SIGNALS:
    /// The tool has been pinned or unpinned by the star or by \p setPinned (only on a real change)
    void pinnedChanged(bool isPinned);

protected:
    virtual void paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const override;
    virtual QSize getContentSizeHint() const override;
    virtual QSize getContentMinimumSizeHint() const override;
    virtual void layoutTrailingWidgets() override;

private:
    PDFComponentPainter::ToolRow m_row;
    QPointer<PDFTag> m_tag;
    QPointer<PDFStarButton> m_star;
};

/// Collection of tools (component Collection row 89:1367): the icon in a circle
/// of the fixed colors bg/accent-subtle and icon/accent (review F-32, the icon
/// layers by default as in the design, changed by \p setIconName), the name,
/// the list of the tools and a chevron
class PDF4QTLIBFLUENTSHARED_EXPORT PDFCollectionRow : public PDFRowButton
{
    Q_OBJECT

private:
    using BaseClass = PDFRowButton;

public:
    explicit PDFCollectionRow(QWidget* parent = nullptr);
    explicit PDFCollectionRow(const QString& name, const QString& meta, QWidget* parent = nullptr);

    QString getName() const { return text(); }
    void setName(const QString& name);

    /// Tools of the collection ("3 tools · Compress, Sanitize, Sign", also the accessible description)
    const QString& getMeta() const { return m_row.meta; }
    void setMeta(const QString& meta);

    const QString& getIconName() const { return m_row.iconName; }
    void setIconName(const QString& iconName);

    PDFComponentPainter::CollectionRow getContent() const;

protected:
    virtual void paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const override;
    virtual QSize getContentSizeHint() const override;
    virtual QSize getContentMinimumSizeHint() const override;

private:
    PDFComponentPainter::CollectionRow m_row;
};

/// Pinned tool of the Tools panel of the document toolbar (component Tool
/// shortcut 192:836): a bordered tile 180 x 48 with the badge and the name.
/// It is a free-standing tile (a card): its focus ring lies outside (radius 10,
/// drawn by the focus frame of the style) and replaces the border. The default
/// hue is the one of the default variant of the design (Blue).
class PDF4QTLIBFLUENTSHARED_EXPORT PDFToolShortcut : public PDFCardButton
{
    Q_OBJECT

private:
    using BaseClass = PDFCardButton;

public:
    explicit PDFToolShortcut(QWidget* parent = nullptr);
    explicit PDFToolShortcut(const QString& name, const QString& iconName, PDFAccentHue hue, QWidget* parent = nullptr);

    QString getName() const { return text(); }
    void setName(const QString& name);

    const QString& getIconName() const { return m_shortcut.iconName; }
    void setIconName(const QString& iconName);

    PDFAccentHue getHue() const { return m_shortcut.hue; }
    void setHue(PDFAccentHue hue);

    PDFComponentPainter::ToolShortcut getContent() const;

protected:
    virtual void paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const override;
    virtual QSize getContentSizeHint() const override;
    virtual QSize getContentMinimumSizeHint() const override;

private:
    PDFComponentPainter::ToolShortcut m_shortcut;
};

/// Fixed issue of a release on the What's New page (component Issue row
/// 103:1931): the green status circle, the number (a column of 44 px, a longer
/// number wraps), the title, the description and the date. A click opens the
/// issue. The accessible description is the description and the date.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFIssueRow : public PDFRowButton
{
    Q_OBJECT

private:
    using BaseClass = PDFRowButton;

public:
    explicit PDFIssueRow(QWidget* parent = nullptr);
    explicit PDFIssueRow(const QString& number, const QString& title, const QString& description, const QString& date, QWidget* parent = nullptr);

    /// Number of the issue ("#612"); the accessible name is the number and the title
    const QString& getNumber() const { return m_row.number; }
    void setNumber(const QString& number);

    QString getTitle() const { return text(); }
    void setTitle(const QString& title);

    /// Description (also a part of the accessible description)
    const QString& getDescription() const { return m_row.description; }
    void setDescription(const QString& description);

    /// Date of the fix (also a part of the accessible description)
    const QString& getDate() const { return m_row.date; }
    void setDate(const QString& date);

    /// Icon of the status circle (check by default)
    const QString& getStatusIconName() const { return m_row.statusIconName; }
    void setStatusIconName(const QString& iconName);

    PDFComponentPainter::IssueRow getContent() const;

protected:
    virtual void paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const override;
    virtual QSize getContentSizeHint() const override;
    virtual QSize getContentMinimumSizeHint() const override;
    virtual QString getDefaultAccessibleName() const override;
    virtual void changeEvent(QEvent* event) override;

private:
    void updateDescription();

    PDFComponentPainter::IssueRow m_row;
    QString m_description;
};

/// Release of the version history (component Version row 103:1950): the rail
/// with the dot and the line, the version, the date and the summary, which
/// wraps (the height of the row follows its width). The row is a link to its
/// release (signal clicked): hover, press, the focus ring inside, Enter and
/// Space; the selected row is the version shown by the page (bg/accent-subtle,
/// the version text/accent), the current row the installed version (the dot
/// bg/accent). A row, which is not clickable, is a text: no focus, no hover,
/// announced as a text. The accessible description says "Current version" of
/// the installed one, "Shown" of the selected one, the date and the summary.
/// The last row of a history hides the line by \p setLineVisible.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFVersionRow : public PDFRowButton
{
    Q_OBJECT
    Q_PROPERTY(bool current READ isCurrent WRITE setCurrent)
    Q_PROPERTY(bool selected READ isSelected WRITE setSelected)
    Q_PROPERTY(bool clickable READ isClickable WRITE setClickable)

private:
    using BaseClass = PDFRowButton;

public:
    explicit PDFVersionRow(QWidget* parent = nullptr);
    explicit PDFVersionRow(const QString& version, const QString& date, const QString& summary, QWidget* parent = nullptr);

    /// Version ("v1.5.3", the text and the accessible name)
    QString getVersion() const { return text(); }
    void setVersion(const QString& version);

    const QString& getDate() const { return m_row.date; }
    void setDate(const QString& date);

    /// Summary of the release (wraps; also a part of the accessible description)
    const QString& getSummary() const { return m_row.summary; }
    void setSummary(const QString& summary);

    /// The installed version: the dot bg/accent
    bool isCurrent() const { return m_row.current; }
    void setCurrent(bool isCurrent);

    /// The version shown by the page: bg/accent-subtle and the version text/accent
    bool isSelected() const { return m_row.selected; }
    void setSelected(bool isSelected);

    /// A clickable row (the default) is a link, which takes the focus; a row,
    /// which stops being clickable, gives its focus away and is a text
    bool isClickable() const { return m_isClickable; }
    void setClickable(bool isClickable);

    /// The line of the rail below the dot (hidden on the last row)
    bool isLineVisible() const { return m_row.lineVisible; }
    void setLineVisible(bool isVisible);

    virtual bool isInteractive() const override { return m_isClickable; }

    PDFComponentPainter::VersionRow getContent() const;

protected:
    virtual void paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const override;
    virtual QSize getContentSizeHint() const override;
    virtual QSize getContentMinimumSizeHint() const override;
    virtual int computeHeightForWidth(int width) const override;
    virtual void changeEvent(QEvent* event) override;

private:
    void updateDescription();

    PDFComponentPainter::VersionRow m_row;
    QString m_description;
    bool m_isClickable = false;
};

/// Row of the Document properties dialog (component Property row 206:873): the
/// key, the value and the actions, which are Icon buttons 24. The row itself is
/// a text (no focus, no activation); the pointer shows the copy button, an
/// editable row shows the edit button always. The copy puts the value into the
/// clipboard and shows a green check with the tooltip "Copied" for a moment
/// (\p setCopiedDuration), nothing moves. Editing replaces the value by a field:
/// Enter (in the field or on the confirm button), the confirm button and the
/// focus leaving the row confirm it (as the editors of the item views commit
/// their data), Esc (in the field or on the actions) and the cancel button
/// cancel it; hiding and disabling the row cancel it as well. While editing,
/// Tab goes from the field to the confirm and the cancel button, the arrows
/// stay in the field. The copy button is reached by Tab (it is shown, when it
/// has the focus). The keyboard focus stays in the row, when the editing ends.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFPropertyRow : public PDFRowButton
{
    Q_OBJECT
    Q_PROPERTY(bool editable READ isEditable WRITE setEditable)

private:
    using BaseClass = PDFRowButton;

public:
    /// The row can be edited (it has the edit button)
    enum class Editability
    {
        ReadOnly,
        Editable
    };
    Q_ENUM(Editability)

    explicit PDFPropertyRow(QWidget* parent = nullptr);
    explicit PDFPropertyRow(const QString& key, const QString& value, QWidget* parent = nullptr);
    explicit PDFPropertyRow(const QString& key, const QString& value, Editability editability, QWidget* parent = nullptr);
    virtual ~PDFPropertyRow() override;

    /// Name of the property ("Author", the text and the accessible name)
    QString getKey() const { return text(); }
    void setKey(const QString& key);

    /// Value of the property (also the accessible description). Setting it
    /// while the row is edited does not change the text in the field.
    const QString& getValue() const { return m_value; }
    void setValue(const QString& value);

    /// An editable row has the edit button. A row, which stops being editable
    /// while it is edited, cancels the editing (without the signal edited).
    bool isEditable() const { return m_isEditable; }
    void setEditable(bool isEditable);

    /// Starts the editing (only of an enabled editable row): the field gets the
    /// value and the focus
    /// \returns true, if the row is edited after the call (a slot of the signal
    ///          editingChanged may have ended the editing again)
    bool startEditing();

    /// Confirms the editing: the value of the field becomes the value of the
    /// row; the signal edited is emitted, if the value has changed
    void confirmEditing();

    /// Cancels the editing, the value is kept
    void cancelEditing();

    bool isEditing() const { return m_isEditing; }

    /// Copies the value into the clipboard and shows the state Copied (only
    /// when the row is shown; a hidden row copies without the state)
    void copy();

    /// The state Copied is shown (for the time of \p getCopiedDuration after the copy)
    bool isCopied() const { return m_isCopied; }

    /// Time of the state Copied in milliseconds (1500 by default, at least 0)
    int getCopiedDuration() const { return m_copiedDuration; }
    void setCopiedDuration(int milliseconds);

    /// Returns the state of the component drawn now
    PDFComponentPainter::PropertyState getPropertyState() const;

    PDFIconButton* getCopyButton() const { return m_copyButton.data(); }
    PDFIconButton* getEditButton() const { return m_editButton.data(); }
    PDFIconButton* getConfirmButton() const { return m_confirmButton.data(); }
    PDFIconButton* getCancelButton() const { return m_cancelButton.data(); }
    QLineEdit* getEditor() const { return m_editor.data(); }

    virtual bool isInteractive() const override { return false; }

    PDFComponentPainter::PropertyRow getContent() const;

Q_SIGNALS:
    /// The value has been copied into the clipboard
    void copied(const QString& value);

    /// The value has been changed by the editing (emitted after the change, only on a real change)
    void edited(const QString& value);

    /// The editing has started or ended
    void editingChanged(bool isEditing);

protected:
    virtual void paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const override;
    virtual QSize getContentSizeHint() const override;
    virtual QSize getContentMinimumSizeHint() const override;
    virtual bool event(QEvent* event) override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual void resizeEvent(QResizeEvent* event) override;
    virtual void changeEvent(QEvent* event) override;
    virtual void hideEvent(QHideEvent* event) override;

private:
    /// Ends the editing; the keyboard focus in the row goes to the edit button
    /// (or to the copy button), not somewhere else in the window
    void finishEditing();
    void onCopiedTimeout();

    /// Shows the actions and the field of the state; the focus of a widget,
    /// which is hidden, goes to the widget of the state first
    void updateActions();
    void updateTexts();
    void placeEditor();
    bool isHoveredRow() const;
    bool hasFocusInside() const;

    QString m_value;
    bool m_isEditable = false;
    bool m_isEditing = false;
    bool m_isCopied = false;
    int m_copiedDuration = 1500;

    // The field is created first: it precedes the actions in the focus chain,
    // so Tab goes from the field to the confirm and the cancel button
    QPointer<QLineEdit> m_editor;
    QPointer<PDFIconButton> m_editButton;
    QPointer<PDFIconButton> m_copyButton;
    QPointer<PDFIconButton> m_confirmButton;
    QPointer<PDFIconButton> m_cancelButton;
    /// A member, not a child: the application cannot reach it by children() and delete it
    QTimer m_copiedTimer;
};

/// Item of a navigation (components Nav item and Settings nav item). The item
/// is a row with the selection behavior ExclusiveStatic: the checked item is
/// the current page, it does not react to the pointer and a click on it does
/// nothing. The items of a navigation are in the exclusive group of PDFNavList.
/// The focus ring of 2 px lies inside of the item.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFNavItem : public PDFRowButton
{
    Q_OBJECT

private:
    using BaseClass = PDFRowButton;

public:
    /// Look of the item
    enum class Kind
    {
        Home,       ///< Nav item: 190 x 38, icon 20, label Regular 13 (Semi Bold, when selected)
        Settings    ///< Settings nav item: 300 x 52, icon 20, name Semi Bold 13 and a subtitle Regular 11
    };

    explicit PDFNavItem(QWidget* parent = nullptr);
    explicit PDFNavItem(const QString& text, const QString& iconName, Kind kind = Kind::Home, QWidget* parent = nullptr);

    Kind getKind() const { return m_kind; }
    void setKind(Kind kind);

    const QString& getIconName() const { return m_iconName; }
    void setIconName(const QString& iconName);

    /// Subtitle of the item of the settings (one line summary of the section).
    /// It is also the description of the item for assistive technologies.
    const QString& getSubtitle() const { return m_subtitle; }
    void setSubtitle(const QString& subtitle);

protected:
    virtual void paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const override;
    virtual QSize getContentSizeHint() const override;
    virtual QSize getContentMinimumSizeHint() const override;

private:
    void init();

    QString m_iconName;
    QString m_subtitle;
    Kind m_kind = Kind::Home;
};

/// Column of navigation items with a single selection. The items can be split
/// into groups divided by a line (the navigation of the Home screen has three
/// groups). The arrows up and down move the selection over the borders of the groups.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFNavList : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFNavList(QWidget* parent = nullptr);
    explicit PDFNavList(PDFNavItem::Kind kind, QWidget* parent = nullptr);

    /// Adds an item to the last group. The first item becomes the current one.
    /// \returns Index of the item
    int addItem(const QString& text, const QString& iconName, const QString& subtitle = QString());

    /// Starts a new group of items, it is divided from the previous one by a line
    void addGroup();

    int getCount() const;
    PDFNavItem* getItem(int index) const;

    /// Index of the current item, -1 when no item is current
    int getCurrentIndex() const;

    /// Makes the item current. A negative index leaves the navigation without
    /// a current item (a page, which is not in the navigation, as Page Not
    /// Found); an index beyond the items does nothing.
    void setCurrentIndex(int index);

Q_SIGNALS:
    /// The current item has changed (-1: no item is current)
    void currentIndexChanged(int index);

protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void onGroupIdToggled(int id, bool isChecked);

    /// The layout is reachable by QWidget::layout(), the application may delete it
    QPointer<QVBoxLayout> m_layout;
    /// A member, not a child: the application cannot reach it by children() and delete it
    QButtonGroup m_group;
    PDFNavItem::Kind m_kind = PDFNavItem::Kind::Home;
};

}   // namespace pdf

#endif // PDFCOMPONENTROWS_H
