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

#ifndef PDFCOMPONENTCARDS_H
#define PDFCOMPONENTCARDS_H

#include "pdffluentglobal.h"
#include "pdfcomponentbuttons.h"
#include "pdfpreviewpainter.h"

#include <QList>
#include <QPointer>
#include <QStringList>

class QContextMenuEvent;

namespace pdf
{

/// Common base of the tiles and cards of the design (Quick action tile, Folder
/// card, Pinned card, Tool choice, Reading theme card, ...). A card is a button:
/// the whole card activates it, Space and Enter activate it from the keyboard,
/// its title is the text of the button (the accessible name) and its subtitle
/// the accessible description. The focus ring of 2 px lies outside of the card
/// (drawn by the focus frame of the style around the shape of the radius set by
/// \p setFocusRingRadius, 12 by default), so the card keeps its size and its
/// content in each state. Cards of a choice set \p setSelectionBehavior.
/// Nested widgets (star, menu button) are placed at the trailing side by
/// \p addTrailingWidget, by default as a row 12 px from the trailing edge with
/// gaps of 8 px. A card, whose height follows its width, implements
/// \p computeHeightForWidth (PDFComponentButton caches it) and its setters call
/// \p invalidateHeight. A card, which is not interactive (a header, a card of
/// the information), has no focus and no Qt::WA_Hover.
///
/// The nested widgets of the cards are owned by the card. The getters return
/// them for the tests, the styling and the accessibility; a nested widget
/// deleted or moved to another parent by the application is forgotten (the
/// getter returns nullptr, the card draws and behaves without it).
class PDF4QTLIBFLUENTSHARED_EXPORT PDFCardButton : public PDFComponentButton
{
    Q_OBJECT

private:
    using BaseClass = PDFComponentButton;

public:
    explicit PDFCardButton(QWidget* parent = nullptr);

protected:
    /// Returns the state, in which a card with a border draws itself: the flag
    /// State_HasFocus is set, when the focus frame of the style (PDFWidgetStyle
    /// is the style of the application) shows the ring around the card, so the
    /// card leaves its border out (the ring replaces it). Draw the card then
    /// with the focus ring of the painter turned off. Under another style the
    /// card keeps its border and PDFComponentButton draws the ring inside.
    PDFComponentPainter::State getBorderState(const PDFComponentPainter::State& state) const;

    /// Updates the accessible description composed of the texts of the card;
    /// called also after a change of the language (the pattern is translated)
    virtual void updateDescription() { }

    virtual void changeEvent(QEvent* event) override;
};

/// Tile of the Home screen and of the Tools screen (component Quick action tile
/// 46:431): a hued tile 88 px high with the icon in a white circle, the title
/// and the description, which wrap, and the optional star (28), which pins the
/// tool ("Pin tool", described by the title of the tile). The width is given by the grid. The hue is the category of the tool
/// (decision 13). A click on the star does not activate the tile.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFQuickActionTile : public PDFCardButton
{
    Q_OBJECT
    Q_PROPERTY(bool pinned READ isPinned WRITE setPinned NOTIFY pinnedChanged)

private:
    using BaseClass = PDFCardButton;

public:
    explicit PDFQuickActionTile(QWidget* parent = nullptr);
    explicit PDFQuickActionTile(const QString& title, const QString& description, const QString& iconName, PDFAccentHue hue, QWidget* parent = nullptr);

    /// Title (the text of the button and its accessible name)
    QString getTitle() const { return text(); }
    void setTitle(const QString& title);

    /// Description (also the accessible description)
    const QString& getDescription() const { return m_tile.description; }
    void setDescription(const QString& description);

    const QString& getIconName() const { return m_tile.iconName; }
    void setIconName(const QString& iconName);

    /// Hue of the category of the tool (an invalid value is Neutral)
    PDFAccentHue getHue() const { return m_tile.hue; }
    void setHue(PDFAccentHue hue);

    /// The star is shown (hidden by default, property Show star of the design)
    bool isStarVisible() const;
    void setStarVisible(bool isVisible);

    bool isPinned() const;
    void setPinned(bool isPinned);

    PDFStarButton* getStarButton() const { return m_star.data(); }

    /// Returns the content drawn by PDFComponentPainter::paintQuickActionTile
    PDFComponentPainter::QuickActionTile getContent() const;

Q_SIGNALS:
    /// The tool has been pinned or unpinned by the star or by \p setPinned (only on a real change)
    void pinnedChanged(bool isPinned);

protected:
    virtual void paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const override;
    virtual QSize getContentSizeHint() const override;
    virtual QSize getContentMinimumSizeHint() const override;

private:
    PDFComponentPainter::QuickActionTile m_tile;
    QPointer<PDFStarButton> m_star;
};

/// Folder of the Open screen (component Folder card 63:597): the icon in a white
/// circle, the name, the path and the meta line ("Last used Today 10:24", or the
/// kind of a pinned location), the menu button. Hue Green, Blue and Violet are
/// the standard folders (Documents, Downloads, Desktop), Neutral the others. A
/// click opens the folder, the menu button and a right click show its menu
/// (the context menu key shows it below the menu button). The accessible
/// description is the path and the meta line.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFFolderCard : public PDFCardButton
{
    Q_OBJECT

private:
    using BaseClass = PDFCardButton;

public:
    explicit PDFFolderCard(QWidget* parent = nullptr);
    explicit PDFFolderCard(const QString& name, const QString& path, const QString& meta, PDFAccentHue hue = PDFAccentHue::Neutral, QWidget* parent = nullptr);

    QString getName() const { return text(); }
    void setName(const QString& name);

    const QString& getPath() const { return m_card.path; }
    void setPath(const QString& path);

    const QString& getMeta() const { return m_card.meta; }
    void setMeta(const QString& meta);

    /// The meta line is shown (property Show meta of the design)
    bool isMetaVisible() const { return m_card.metaVisible; }
    void setMetaVisible(bool isVisible);

    const QString& getIconName() const { return m_card.iconName; }
    void setIconName(const QString& iconName);

    /// Hue: Blue, Neutral, Green or Violet (any other value is Neutral)
    PDFAccentHue getHue() const { return m_card.hue; }
    void setHue(PDFAccentHue hue);

    /// Menu of the card (not owned): shown by the menu button and by a right click
    QMenu* getMenu() const;
    void setMenu(QMenu* menu);

    PDFMoreButton* getMenuButton() const { return m_moreButton.data(); }

    PDFComponentPainter::FolderCard getContent() const;

Q_SIGNALS:
    /// The menu has been requested (menu button, right click or the context menu
    /// key), before the menu is shown
    void menuRequested();

protected:
    virtual void paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const override;
    virtual QSize getContentSizeHint() const override;
    virtual QSize getContentMinimumSizeHint() const override;
    virtual void contextMenuEvent(QContextMenuEvent* event) override;
    virtual void updateDescription() override;
    virtual void requestMoreButtonMenu() override;

private:

    PDFComponentPainter::FolderCard m_card;
    QPointer<PDFMoreButton> m_moreButton;
    QString m_description;
};

/// Pinned document of the Recent screen (component Pinned card 79:1115): the
/// thumbnail of the first page (the picture of the application, painted by the
/// card with its shadow), the name, the path, the reading progress (text and
/// bar), the star (pinned by default; a click unpins the document) and the menu
/// button. A click opens the document. The accessible description is the path
/// and the progress text. A missing file (\p setStatus) has the name text/muted
/// and the badge "No longer exists" instead of the progress, a document
/// protected by a password (\p setLocked) the lock instead of the thumbnail
/// (both proposals consistent with the tokens, Figma has no frame of them).
class PDF4QTLIBFLUENTSHARED_EXPORT PDFPinnedCard : public PDFCardButton
{
    Q_OBJECT
    Q_PROPERTY(bool pinned READ isPinned WRITE setPinned NOTIFY pinnedChanged)
    Q_PROPERTY(bool locked READ isLocked WRITE setLocked)

private:
    using BaseClass = PDFCardButton;

public:
    using Status = PDFComponentPainter::FileStatus;

    explicit PDFPinnedCard(QWidget* parent = nullptr);
    explicit PDFPinnedCard(const QString& name, const QString& path, const QString& progressText, qreal progress, QWidget* parent = nullptr);

    QString getName() const { return text(); }
    void setName(const QString& name);

    const QString& getPath() const { return m_card.path; }
    void setPath(const QString& path);

    /// Text of the reading progress ("Page 12 of 24 · Today 10:24")
    const QString& getProgressText() const { return m_card.progressText; }
    void setProgressText(const QString& progressText);

    /// Reading progress of 0 to 1 (a value out of the range is bounded, NaN is 0)
    qreal getProgress() const { return m_card.progress; }
    void setProgress(qreal progress);

    /// Picture of the first page (the placeholder lines without it)
    const QPixmap& getPixmap() const { return m_card.pixmap; }
    void setPixmap(const QPixmap& pixmap);

    bool isPinned() const;
    void setPinned(bool isPinned);

    /// Status of the file (an invalid value is Available)
    Status getStatus() const { return m_card.status; }
    void setStatus(Status status);

    /// The document is protected by a password: the thumbnail shows the lock
    bool isLocked() const { return m_card.locked; }
    void setLocked(bool isLocked);

    QMenu* getMenu() const;
    void setMenu(QMenu* menu);

    PDFStarButton* getStarButton() const { return m_star.data(); }
    PDFMoreButton* getMenuButton() const { return m_moreButton.data(); }

    PDFComponentPainter::PinnedCard getContent() const;

Q_SIGNALS:
    /// The document has been pinned or unpinned by the star or by \p setPinned (only on a real change)
    void pinnedChanged(bool isPinned);

    /// The menu has been requested (menu button, right click or the context menu key)
    void menuRequested();

protected:
    virtual void paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const override;
    virtual QSize getContentSizeHint() const override;
    virtual QSize getContentMinimumSizeHint() const override;
    virtual void contextMenuEvent(QContextMenuEvent* event) override;
    virtual void updateDescription() override;
    virtual void requestMoreButtonMenu() override;

private:

    PDFComponentPainter::PinnedCard m_card;
    QPointer<PDFStarButton> m_star;
    QPointer<PDFMoreButton> m_moreButton;
    QString m_description;
};

/// Tinted header of a category card of the Tools screen (component Tool category
/// header 89:1424): the icon in a white circle, the title, the description and
/// the count of the tools. Without states: it is a header, not a button (no
/// focus, no hover, announced as a text with the description and the count).
class PDF4QTLIBFLUENTSHARED_EXPORT PDFToolCategoryHeader : public PDFCardButton
{
    Q_OBJECT

private:
    using BaseClass = PDFCardButton;

public:
    explicit PDFToolCategoryHeader(QWidget* parent = nullptr);
    explicit PDFToolCategoryHeader(const QString& title, const QString& description, const QString& count, const QString& iconName, PDFAccentHue hue, QWidget* parent = nullptr);

    QString getTitle() const { return text(); }
    void setTitle(const QString& title);

    const QString& getDescription() const { return m_header.description; }
    void setDescription(const QString& description);

    /// Count of the tools ("6 tools"), never elided
    const QString& getCount() const { return m_header.count; }
    void setCount(const QString& count);

    const QString& getIconName() const { return m_header.iconName; }
    void setIconName(const QString& iconName);

    PDFAccentHue getHue() const { return m_header.hue; }
    void setHue(PDFAccentHue hue);

    /// Radius of the top corners (0 by default): the radius of the inside of the
    /// card, at whose top the header lies (a negative or invalid value is 0)
    qreal getTopRadius() const { return m_header.topRadius; }
    void setTopRadius(qreal radius);

    virtual bool isInteractive() const override { return false; }

    PDFComponentPainter::ToolCategoryHeader getContent() const;

protected:
    virtual void paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const override;
    virtual QSize getContentSizeHint() const override;
    virtual QSize getContentMinimumSizeHint() const override;
    virtual void updateDescription() override;

private:
    PDFComponentPainter::ToolCategoryHeader m_header;
    QString m_description;
};

/// Suggested tool of "Then continue with" in a result dialog (component Tool
/// choice 417:1006): the badge 28 and the name, which wraps by words; 154 wide,
/// the height hugs the name (46 for one line, 48 for two lines) and it is the
/// same in each state; the minimal width keeps the longest word of the name
/// whole. It is a choice of an exclusive group, which can be left empty
/// (SelectionBehavior::Deselectable: nothing is preselected, a click on the
/// selected tool unselects it). The selection is a ring inside, the focus ring
/// lies outside (radius 10), the selected tool with the focus shows both.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFToolChoice : public PDFCardButton
{
    Q_OBJECT
    Q_PROPERTY(bool selected READ isSelected WRITE setSelected NOTIFY toggled)

private:
    using BaseClass = PDFCardButton;

public:
    explicit PDFToolChoice(QWidget* parent = nullptr);
    explicit PDFToolChoice(const QString& name, const QString& iconName, PDFAccentHue hue, QWidget* parent = nullptr);

    QString getName() const { return text(); }
    void setName(const QString& name);

    const QString& getIconName() const { return m_choice.iconName; }
    void setIconName(const QString& iconName);

    PDFAccentHue getHue() const { return m_choice.hue; }
    void setHue(PDFAccentHue hue);

    /// Selects or unselects the tool (also the selected tool of an exclusive
    /// group, which is then left empty); the signal toggled is emitted on a change
    bool isSelected() const { return isChecked(); }
    void setSelected(bool isSelected);

    PDFComponentPainter::ToolChoice getContent() const;

protected:
    virtual void paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const override;
    virtual QSize getContentSizeHint() const override;
    virtual QSize getContentMinimumSizeHint() const override;
    virtual int computeHeightForWidth(int width) const override;

private:
    PDFComponentPainter::ToolChoice m_choice;
};

/// Card of Learn & Resources (component Resource card 50:242): the icon, the
/// title and the description, which wrap; the height follows the width (the
/// cards of a row in a layout share their height, the content lies at the top).
/// Hue Violet is video tutorials, Green documentation, Amber getting started,
/// Neutral anything else.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFResourceCard : public PDFCardButton
{
    Q_OBJECT

private:
    using BaseClass = PDFCardButton;

public:
    explicit PDFResourceCard(QWidget* parent = nullptr);
    explicit PDFResourceCard(const QString& title, const QString& description, const QString& iconName, PDFAccentHue hue, QWidget* parent = nullptr);

    QString getTitle() const { return text(); }
    void setTitle(const QString& title);

    /// Description (also the accessible description)
    const QString& getDescription() const { return m_card.description; }
    void setDescription(const QString& description);

    const QString& getIconName() const { return m_card.iconName; }
    void setIconName(const QString& iconName);

    /// Hue: Neutral, Green, Amber or Violet (any other value is Neutral)
    PDFAccentHue getHue() const { return m_card.hue; }
    void setHue(PDFAccentHue hue);

    PDFComponentPainter::ResourceCard getContent() const;

protected:
    virtual void paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const override;
    virtual QSize getContentSizeHint() const override;
    virtual QSize getContentMinimumSizeHint() const override;
    virtual int computeHeightForWidth(int width) const override;

private:
    PDFComponentPainter::ResourceCard m_card;
};

/// Highlight of a release on the What's New page (component Highlight card
/// 103:1894): a white card with the icon in a circle of the hue, the title and
/// the description, which wrap. Not interactive (the design has no states): it
/// is a card of the information, not a button - no focus, no hover, announced
/// as a text. It derives from PDFCardButton, as the Version row derives from
/// PDFRowButton, to share the drawing through PDFComponentPainter, the change of
/// the theme, the right-to-left layout and the accessibility of the components.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFHighlightCard : public PDFCardButton
{
    Q_OBJECT

private:
    using BaseClass = PDFCardButton;

public:
    explicit PDFHighlightCard(QWidget* parent = nullptr);
    explicit PDFHighlightCard(const QString& title, const QString& description, const QString& iconName, PDFAccentHue hue, QWidget* parent = nullptr);

    QString getTitle() const { return text(); }
    void setTitle(const QString& title);

    const QString& getDescription() const { return m_card.description; }
    void setDescription(const QString& description);

    const QString& getIconName() const { return m_card.iconName; }
    void setIconName(const QString& iconName);

    PDFAccentHue getHue() const { return m_card.hue; }
    void setHue(PDFAccentHue hue);

    virtual bool isInteractive() const override { return false; }

    PDFComponentPainter::HighlightCard getContent() const;

protected:
    virtual void paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const override;
    virtual QSize getContentSizeHint() const override;
    virtual QSize getContentMinimumSizeHint() const override;
    virtual int computeHeightForWidth(int width) const override;

private:
    PDFComponentPainter::HighlightCard m_card;
};

/// Video tutorial of the Support screen (component Video card 116:2418): the
/// preview (the cached picture of the video given by the application; the
/// placeholder without it) with the play button and the duration, the title,
/// the description of two lines at most and the tags. The whole card opens the
/// video; the tags are clickable (they filter the list of the videos, signal
/// \p tagClicked) and they do not activate the card. Tags, which do not fit the
/// width, are hidden by the card. The tags follow the card in the order of the
/// keyboard focus (also the tags of a later \p setTags). A tag hidden by the
/// application stays hidden and takes no place, a tag deleted by the application
/// is forgotten (the other tags take its place). The height follows the width
/// (the description wraps).
class PDF4QTLIBFLUENTSHARED_EXPORT PDFVideoCard : public PDFCardButton
{
    Q_OBJECT

private:
    using BaseClass = PDFCardButton;

public:
    explicit PDFVideoCard(QWidget* parent = nullptr);
    explicit PDFVideoCard(const QString& title, const QString& description, const QString& duration, const QStringList& tags, QWidget* parent = nullptr);

    QString getTitle() const { return text(); }
    void setTitle(const QString& title);

    /// Description (also a part of the accessible description)
    const QString& getDescription() const { return m_card.description; }
    void setDescription(const QString& description);

    /// Duration ("2:45", also a part of the accessible description)
    const QString& getDuration() const { return m_card.duration; }
    void setDuration(const QString& duration);

    /// Tags; empty and blank strings are left out (the stored list is without
    /// them, so its indices are the indices of \p getTagWidgets, until the
    /// application deletes a tag). Tags equal to the current ones only recreate
    /// the tags deleted by the application.
    const QStringList& getTags() const { return m_card.tags; }
    void setTags(const QStringList& tags);

    /// Picture of the video (the placeholder bg/muted without it)
    const QPixmap& getPixmap() const { return m_card.pixmap; }
    void setPixmap(const QPixmap& pixmap);

    /// Returns the nested tags (one for each tag of \p getTags, in its order,
    /// also the hidden ones; a tag deleted by the application is left out)
    QList<PDFTag*> getTagWidgets() const;

    /// Returns the content drawn by the card: its tags are the texts of the
    /// nested tags, which take their places (not deleted, not hidden by the application)
    PDFComponentPainter::VideoCard getContent() const;

Q_SIGNALS:
    /// A tag has been clicked (the card is not activated)
    void tagClicked(const QString& tag);

protected:
    virtual void paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const override;
    virtual QSize getContentSizeHint() const override;
    virtual QSize getContentMinimumSizeHint() const override;
    virtual int computeHeightForWidth(int width) const override;
    virtual void layoutTrailingWidgets() override;
    virtual void updateDescription() override;
    virtual bool event(QEvent* event) override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;
    virtual void paintEvent(QPaintEvent* event) override;

private:
    void rebuildTags();

    /// A nested tag has been clicked: the signal tagClicked with its text
    void onTagClicked();

    /// Returns true, if the tag is a nested tag of the card (not deleted, not moved away)
    bool isOwnTag(const PDFTag* tag) const;

    /// Returns true, if the tag has been hidden by the application (not by the card)
    static bool isHiddenByApplication(const PDFTag* tag);

    PDFComponentPainter::VideoCard m_card;
    QList<QPointer<PDFTag>> m_tags;
    QStringList m_tagTexts;         ///< Texts of the tags of \p m_tags (the same index), reported by tagClicked
    QString m_description;
    QString m_tagLayoutTitle;       ///< Title, with which the tags were placed (QAbstractButton::setText bypasses setTitle)
    bool m_isPlacingTags = false;   ///< The card itself shows and hides its tags
};

/// Preset of the page and the text colors of the Custom colors mode (component
/// Reading theme card 156:4218): the sample "Aa" in the colors of the theme and
/// the name, 159 x 86 in each state. It is a choice of an exclusive group
/// (SelectionBehavior::ExclusiveStatic: the selected card does not react to the
/// pointer). The selection is a ring inside, the focus ring lies outside (review
/// N-09: in the light theme both rings have the same color). The colors are the
/// tokens reading/* (\p setColors) or colors of the user (\p setCustomColors).
class PDF4QTLIBFLUENTSHARED_EXPORT PDFReadingThemeCard : public PDFCardButton
{
    Q_OBJECT
    Q_PROPERTY(bool selected READ isSelected WRITE setSelected NOTIFY toggled)

private:
    using BaseClass = PDFCardButton;

public:
    explicit PDFReadingThemeCard(QWidget* parent = nullptr);
    explicit PDFReadingThemeCard(const QString& name, PDFThemeColors::Color pageColor, PDFThemeColors::Color textColor, QWidget* parent = nullptr);

    QString getName() const { return text(); }
    void setName(const QString& name);

    /// Colors of the sample as tokens (reading/paper/* by default; an invalid
    /// token is replaced by the token of the paper)
    PDFThemeColors::Color getPageColor() const { return m_card.pageColor; }
    PDFThemeColors::Color getTextColor() const { return m_card.textColor; }
    void setColors(PDFThemeColors::Color pageColor, PDFThemeColors::Color textColor);

    /// Colors of the user, which replace the tokens (the theme Custom); an invalid
    /// color leaves the token of its role
    const QColor& getCustomPageColor() const { return m_card.customPageColor; }
    const QColor& getCustomTextColor() const { return m_card.customTextColor; }
    void setCustomColors(const QColor& pageColor, const QColor& textColor);

    bool isSelected() const { return isChecked(); }
    void setSelected(bool isSelected);

    PDFComponentPainter::ReadingThemeCard getContent() const;

protected:
    virtual void paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const override;
    virtual QSize getContentSizeHint() const override;
    virtual QSize getContentMinimumSizeHint() const override;

private:
    PDFComponentPainter::ReadingThemeCard m_card;
};

/// Card of a choice with a picture (component Color mode card 171:4545, review
/// O-05): the picture of PDFPreviewPainter - one of the six color modes or of the
/// four page layouts - and the name, 125 x 122 in each state (the width follows
/// the layout). Selection and focus as PDFReadingThemeCard. Without a name the
/// card shows the name of its picture ("High contrast").
class PDF4QTLIBFLUENTSHARED_EXPORT PDFPreviewCard : public PDFCardButton
{
    Q_OBJECT
    Q_PROPERTY(bool selected READ isSelected WRITE setSelected NOTIFY toggled)

private:
    using BaseClass = PDFCardButton;

public:
    using Preview = PDFPreviewPainter::Preview;

    explicit PDFPreviewCard(QWidget* parent = nullptr);
    explicit PDFPreviewCard(Preview preview, QWidget* parent = nullptr);
    explicit PDFPreviewCard(const QString& name, Preview preview, QWidget* parent = nullptr);

    /// Name of the card; an empty name shows the name of the picture
    QString getName() const { return text(); }
    void setName(const QString& name);

    /// Picture of the card (an invalid value is ModeNormal)
    Preview getPreview() const { return m_card.preview; }
    void setPreview(Preview preview);

    bool isSelected() const { return isChecked(); }
    void setSelected(bool isSelected);

    PDFComponentPainter::PreviewCard getContent() const;

protected:
    virtual void paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const override;
    virtual QSize getContentSizeHint() const override;
    virtual QSize getContentMinimumSizeHint() const override;
    virtual QString getDefaultAccessibleName() const override;
    virtual void changeEvent(QEvent* event) override;

private:
    /// Returns the name shown by the card
    QString getShownName() const;

    PDFComponentPainter::PreviewCard m_card;
};

}   // namespace pdf

#endif // PDFCOMPONENTCARDS_H
