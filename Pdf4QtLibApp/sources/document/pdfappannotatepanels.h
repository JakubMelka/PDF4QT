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

#ifndef PDFAPPANNOTATEPANELS_H
#define PDFAPPANNOTATEPANELS_H

#include "pdfappglobal.h"
#include "pdfappannotationcreator.h"
#include "pdfappdocumentmenus.h"

#include <QPointer>

#include <map>
#include <vector>

class QTimer;
class QToolButton;

namespace pdf
{

class PDFAppDocument;
class PDFColorSwatchRow;
class PDFLabel;
class PDFStampChip;

/// Annotate panel (Figma 877:10157, 550 wide, tools.md 9.8): the tiles of the
/// text markups (Highlight, Underline, Strikeout, Squiggly - 88 wide, Highlight
/// and comment - 140 wide; the active tile is the active or the last used markup),
/// the color row and the row Show all comments (N), which opens the Notes tab of
/// the sidebar. A tile with a text selection marks it at once, without one it
/// starts the tool (the commands annotate.*).
///
/// Every markup tool remembers its own color (owner 2026-10-27, as Acrobat and
/// Foxit), the panel makes it visible: each tile shows the color of its tool as
/// the color bar of the Action tile (190:740) and the color row edits the color of
/// one tile, the target, named by its caption ("Underline color"). The target is
/// the checked tile (the active or the last used markup), until the pointer rests
/// on another tile for HOVER_INTENT_DELAY (crossing a tile on the way to the color
/// row does not switch it) or the keyboard moves the focus to another tile; then
/// it stays (sticky) until the panel closes. A target, which is not the checked
/// tile, shows the outline of the property editTarget (1295:31357). A swatch
/// changes only the color of the tool of the target (it keeps the panel open, the
/// bars follow at once); Highlight and comment shares the color of Highlight.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppAnnotatePanel : public PDFAppDocumentPanelPopup
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDocumentPanelPopup;

public:
    /// Width of the panel 877:10157
    static constexpr int PANEL_WIDTH = 550;

    /// Widths of the tiles 877:10160 (four markups and Highlight and comment)
    static constexpr int MARKUP_TILE_WIDTH = 88;
    static constexpr int COMMENT_TILE_WIDTH = 140;

    explicit PDFAppAnnotatePanel(PDFAppDocumentView* view, QWidget* parent);
    virtual ~PDFAppAnnotatePanel() override;

    virtual void updateState() override;

    PDFColorSwatchRow* getColorRow() const;
    PDFAppPanelMenuItem* getShowCommentsItem() const;

    /// Markup type, whose color the color row shows (the type of the target tile)
    AnnotationType getColorMarkupType() const;

    /// Markup tool of the target tile of the color row (the tile hovered or
    /// focused last, else the active markup tool, else the last used markup)
    PDFAppAnnotationToolKind getColorTargetKind() const;

    /// Tile of a markup tool (nullptr for another kind)
    QToolButton* getMarkupTile(PDFAppAnnotationToolKind kind) const;

    /// Caption of the color row ("Highlight color")
    PDFLabel* getColorCaption() const;

    /// The pointer rests on a tile, which becomes the target after HOVER_INTENT_DELAY
    bool isHoverIntentPending() const;

    /// The pointer rests on a tile for this time, before the tile becomes the target
    static constexpr int HOVER_INTENT_DELAY = 150;

    /// Number of the comments of the document (the markup annotations without the
    /// popups and the replies), at most MAXIMUM_COUNTED_COMMENTS
    static int countComments(const PDFDocument* document);

    static constexpr int MAXIMUM_COUNTED_COMMENTS = 1000000;

protected:
    virtual void retranslate() override;
    virtual void hideEvent(QHideEvent* event) override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void onColorSelected(const QColor& color);
    void onShowCommentsClicked();
    void onCreatorStateChanged();
    void onHoverIntentTimeout();

    /// The markup tool of the tile (None, if the object is no tile of a markup)
    PDFAppAnnotationToolKind getTileKind(const QObject* object) const;

    /// The target is the checked tile again (the panel closes)
    void resetColorTarget();

    void setColorTarget(PDFAppAnnotationToolKind kind);

    /// Updates the color bars, the outline of the target, the caption and the
    /// color row from the remembered colors of the tools
    void updateColorTarget();

    /// Name of the color for the user (the name of an annotation color, otherwise #RRGGBB)
    static QString getColorName(const QColor& color);

    QPointer<PDFAppAnnotationCreator> m_creator;
    QPointer<PDFLabel> m_colorCaption;
    QPointer<PDFColorSwatchRow> m_colorRow;
    std::vector<std::pair<PDFAppAnnotationToolKind, QPointer<QToolButton>>> m_markupTiles;
    QTimer* m_hoverIntentTimer = nullptr;
    PDFAppAnnotationToolKind m_targetKind = PDFAppAnnotationToolKind::None;          ///< Target chosen by the user (None = the checked tile)
    PDFAppAnnotationToolKind m_pendingTargetKind = PDFAppAnnotationToolKind::None;   ///< Hovered tile waiting for the hover intent
    bool m_isKeyboardUsed = false;      ///< A key has been pressed in the opened panel (its first focus is no choice of the user)
    QPointer<PDFAppPanelMenuItem> m_showComments;
    int m_commentCount = 0;
    QPointer<const PDFAppDocument> m_countedDocument;   ///< Document model of the counted comments (read once per version)
    quint64 m_countedVersion = 0;
    int m_countedComments = 0;
};

/// Insert panel (Figma 196:3160, 778 wide): Text and notes (Text box, the seven
/// sticky notes), Shapes (Line, Polyline, Rectangle, Polygon, Ellipse, Freehand),
/// Links and pages (Web link, Link to page, Page numbers - the tool route,
/// Outline item - the outline editor of package C), the fourteen stamps and the
/// row Delete annotations. A tile starts its tool (the panel closes, the tool
/// starts from the event loop); the tile of the active tool is checked.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppInsertPanel : public PDFAppDocumentPanelPopup
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDocumentPanelPopup;

public:
    /// Width of the panel 196:3160
    static constexpr int PANEL_WIDTH = 778;

    /// Columns of a row of tiles (196:3191)
    static constexpr int TILE_COLUMNS = 8;

    /// Gap of the stamp chips (196:3285)
    static constexpr int STAMP_GAP = 6;

    explicit PDFAppInsertPanel(PDFAppDocumentView* view, QWidget* parent);
    virtual ~PDFAppInsertPanel() override;

    virtual void updateState() override;

    /// Tile of a sticky note icon, of Page numbers, of Outline item (nullptr otherwise)
    QToolButton* getStickyNoteTile(TextAnnotationIcon icon) const;
    QToolButton* getPageNumbersTile() const;
    QToolButton* getOutlineItemTile() const;

    /// Chip of a stamp
    PDFStampChip* getStampChip(Stamp stamp) const;

protected:
    virtual void retranslate() override;

private:
    void addSection(const char* caption, QWidget* content);
    QToolButton* createVariantTile(const QString& objectName, const QString& iconName);
    void onStickyNoteClicked();
    void onStampClicked();
    void onPageNumbersClicked();
    void onOutlineItemClicked();
    void onOutlineItemQueued();
    void onCreatorStateChanged();

    QPointer<PDFAppAnnotationCreator> m_creator;
    std::map<TextAnnotationIcon, QPointer<QToolButton>> m_stickyNoteTiles;
    std::map<Stamp, QPointer<PDFStampChip>> m_stampChips;
    QPointer<QToolButton> m_pageNumbersTile;
    QPointer<QToolButton> m_outlineItemTile;
};

}   // namespace pdf

#endif // PDFAPPANNOTATEPANELS_H
