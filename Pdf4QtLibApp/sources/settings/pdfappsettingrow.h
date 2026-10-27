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

#ifndef PDFAPPSETTINGROW_H
#define PDFAPPSETTINGROW_H

#include "pdfappglobal.h"
#include "pdfappcontext.h"
#include "pdfsettingdefinition.h"
#include "pdfapppagelayout.h"
#include "pdfappsettingcontrol.h"

#include "pdfcomponentbuttons.h"
#include "pdffluentwidgets.h"

#include <QBoxLayout>
#include <QList>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QWidget>

#include <optional>

namespace pdf
{

class PDFAppCard;
class PDFAppContext;
class PDFAppParagraph;
class PDFAppSectionHeader;
class PDFAppSettingControl;
class PDFLabel;
class PDFSearchQuery;
class PDFTag;

/// Row of a setting (146:2843 row-*): the title Semi Bold 13 with an optional tag,
/// the description Regular 11 text/muted below it and the control in the control
/// wrap of 260 px at the trailing side; paddings 16 / 11, gap 16, at least 56 high,
/// a divider border/divider above every row of a card except the first visible one.
/// A row narrower than STACK_THRESHOLD has the control below the texts (the
/// minimal window). The height follows the width (wrapped descriptions).
///
/// A row of a definition creates its control by PDFAppSettingControl (the binder);
/// a custom row of a package (Tool history, Play sample) has a title, a description
/// and an anchor and gets its control by setControlWidget. The anchor of a route
/// (/settings/viewing#magnifier-zoom) highlights the row: bg/accent-subtle with the
/// ring border/focus of 2 px for HIGHLIGHT_DURATION ms.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSettingRow : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    static constexpr int MINIMUM_HEIGHT = 56;
    static constexpr int PADDING_HORIZONTAL = 16;
    static constexpr int PADDING_VERTICAL = 11;
    static constexpr int SPACING = 16;
    static constexpr int CONTROL_WRAP_WIDTH = 260;
    static constexpr int STACK_THRESHOLD = 520;
    static constexpr int HIGHLIGHT_DURATION = 3000;
    static constexpr int HIGHLIGHT_RING = 2;
    static constexpr int DIVIDER_WIDTH = 1;

    /// Row of the definition with its control (the binder)
    explicit PDFAppSettingRow(PDFAppContext* context, const PDFSettingDefinition& definition, QWidget* parent = nullptr);

    /// Custom row: the texts and the anchor; the control comes by setControlWidget
    explicit PDFAppSettingRow(const QString& title, const QString& description, const QString& anchor, QWidget* parent = nullptr);

    virtual ~PDFAppSettingRow() override;

    /// Definition of the row (nullopt for a custom row)
    const std::optional<PDFSettingDefinition>& getDefinition() const { return m_definition; }

    /// Key of the setting (empty for a custom row)
    QString getKey() const;

    /// Binder of the control (nullptr for a custom row)
    PDFAppSettingControl* getControl() const { return m_control; }

    /// Control wrap (260 px) and the widget placed into it
    QWidget* getControlWrap() const { return m_controlWrap; }
    QWidget* getControlWidget() const { return m_controlWidget; }

    /// Width of the control wrap (CONTROL_WRAP_WIDTH; the rows of the page AI
    /// have the control column 320 of 653:5475)
    int getControlWrapWidth() const { return m_controlWrapWidth; }
    void setControlWrapWidth(int width);

    /// Places the widget into the control wrap (the row takes it over, a previous
    /// widget of a custom row is deleted)
    void setControlWidget(QWidget* widget);

    /// Block row of a package (the cards of the color modes, the presets, the
    /// preview of 160:4203): the widget takes the whole row with the paddings
    /// \p margins instead of the texts and the control wrap (they are hidden; the
    /// title, the description and the keywords stay for the search and the
    /// anchor). The row takes the widget over. The paddings are kept, when the
    /// divider is shown or hidden (the divider is above the padding).
    void setBlockWidget(QWidget* widget, const QMargins& margins);
    QWidget* getBlockWidget() const { return m_blockWidget; }

    /// Paddings of the row without the divider (16 / 11, or the paddings of a block)
    const QMargins& getPadding() const { return m_padding; }

    const QString& getAnchor() const { return m_anchor; }

    QString getTitle() const;
    void setTitle(const QString& title);
    QString getDescription() const;
    void setDescription(const QString& description);

    /// Tag beside the title (empty = none; a tag of the definition by default)
    QString getTagText() const;
    void setTagText(const QString& text);

    PDFAppParagraph* getTitleLabel() const { return m_titleLabel; }
    PDFAppParagraph* getDescriptionLabel() const { return m_descriptionLabel; }
    PDFTag* getTag() const { return m_tag; }

    /// Words of the search of a custom row (English, the definition of a row has
    /// its keywords and option titles)
    const QStringList& getSearchKeywords() const { return m_searchKeywords; }
    void setSearchKeywords(const QStringList& keywords);

    /// The query matches the row: its title, keywords (also the group, the option
    /// titles), description - translated and English source texts
    bool matches(const PDFSearchQuery& query, const QStringList& groupTexts) const;

    /// Updates the row after a change of the setting \p key (empty = all): the
    /// control shows the stored value, the dependency (setEnabledWhen) is evaluated
    void updateFromSettings(const QString& key);

    /// The dependency of the definition is fulfilled (true without a dependency)
    bool isDependencyEnabled() const;

    /// Highlight of an anchor: on for HIGHLIGHT_DURATION ms
    void highlight();
    bool isHighlighted() const { return m_isHighlighted; }
    void setHighlighted(bool highlighted);

    /// Divider above the row (the first visible row of a card has none)
    bool isDividerVisible() const { return m_isDividerVisible; }
    void setDividerVisible(bool visible);

    /// The row never has the divider (the flat warning row of the page AI
    /// 653:6658 directly below the row above it)
    bool isDividerSuppressed() const { return m_isDividerSuppressed; }
    void setDividerSuppressed(bool suppressed);

    /// The control is below the texts (a narrow row)
    bool isStacked() const { return m_isStacked; }

    /// Writes the pending value of the control
    void commitPendingValue();

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void resizeEvent(QResizeEvent* event) override;

private:
    void init(const QString& title, const QString& description, const QString& tag);
    void updateArrangement(int width);
    void updateMargins();
    void onHighlightTimeout();

    QPointer<PDFAppContext> m_context;
    std::optional<PDFSettingDefinition> m_definition;
    QString m_anchor;
    QStringList m_searchKeywords;
    QPointer<QBoxLayout> m_layout;
    QPointer<QWidget> m_textColumn;
    QPointer<PDFAppParagraph> m_titleLabel;
    QPointer<PDFTag> m_tag;
    QPointer<PDFAppParagraph> m_descriptionLabel;
    QPointer<QWidget> m_controlWrap;
    QPointer<QHBoxLayout> m_controlLayout;
    QPointer<QWidget> m_controlWidget;
    QPointer<PDFAppSettingControl> m_control;
    QPointer<QWidget> m_blockWidget;
    QMargins m_padding = QMargins(PADDING_HORIZONTAL, PADDING_VERTICAL, PADDING_HORIZONTAL, PADDING_VERTICAL);
    QTimer m_highlightTimer;
    int m_controlWrapWidth = CONTROL_WRAP_WIDTH;
    bool m_isHighlighted = false;
    bool m_isDividerVisible = false;
    bool m_isDividerSuppressed = false;
    bool m_isStacked = false;
};

/// Group of a section (146:3344 group-*): the header (the title Bold 15 and an
/// optional hint Regular 12 at the trailing side, 12 above the content) and the
/// card (bg/surface, border/default, radius 12) with the rows. A package adds its
/// own content (cards of the color modes, the table of the certificates) by
/// insertWidget: such a widget is shown, when the section is not filtered.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSettingGroup : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// Gap between the header and the content
    static constexpr int SPACING = 12;

    /// \param key Key of the group (the source text of the group of the definitions)
    explicit PDFAppSettingGroup(const QString& key, const QString& title, const QString& hint, QWidget* parent = nullptr);
    virtual ~PDFAppSettingGroup() override;

    const QString& getKey() const { return m_key; }
    QString getTitle() const;

    /// The title and the hint (a language change)
    void setTexts(const QString& title, const QString& hint);

    PDFAppSectionHeader* getHeader() const { return m_header; }
    PDFAppCard* getCard() const { return m_card; }

    /// Layout of the group: the header (index 0), the card (index 1) and the widgets
    /// of insertWidget
    QVBoxLayout* getLayout() const { return m_layout; }

    /// Adds the row to the card (at \p index of the card, -1 = at the end)
    void addRow(PDFAppSettingRow* row, int index = -1);

    /// Inserts a widget of the group (outside of the card) at the index of the
    /// layout of the group (-1 = at the end)
    void insertWidget(QWidget* widget, int index = -1);

    /// Rows of the card in their order
    QList<PDFAppSettingRow*> getRows() const;

    /// Widgets of insertWidget
    QList<QWidget*> getExtraWidgets() const;

    /// Shows the rows and the widgets (no filter) or the rows matching the
    /// query (the widgets of insertWidget are hidden). \returns visible rows.
    int applyFilter(const PDFSearchQuery& query, bool showAll);

    /// The first visible row has no divider, the card is hidden without visible rows
    void updateRows();

    /// A widget of a group (a row, a table, a card of a package), whose size has
    /// changed after it was shown: the layouts of all its ancestors up to the window
    /// are laid out again in the next pass of the event loop. Qt propagates a new
    /// size hint one level per pass (a level takes the new hint of its child, when
    /// it is activated), the settings page has about ten levels - the parts below
    /// the widget would cover it for those passes.
    static void updateAncestorLayouts(QWidget* widget);

private:
    QString m_key;
    QPointer<QVBoxLayout> m_layout;
    QPointer<PDFAppSectionHeader> m_header;
    QPointer<PDFAppParagraph> m_hint;
    QPointer<PDFAppCard> m_card;
    QList<QPointer<QWidget>> m_extraWidgets;
};

}   // namespace pdf

#endif // PDFAPPSETTINGROW_H
