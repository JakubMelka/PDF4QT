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

#ifndef PDFCOMPONENTPANELS_H
#define PDFCOMPONENTPANELS_H

#include "pdffluentglobal.h"
#include "pdfcomponentbuttons.h"
#include "pdfcomponentpainter.h"
#include "pdffluentwidgets.h"

#include <QCoreApplication>
#include <QList>
#include <QPointer>
#include <QSizeF>
#include <QTabBar>

#include <vector>

class QCheckBox;

namespace pdf
{

class PDFToolChoice;

/// Editable value in a toolbar (component Toolbar field): the current page with
/// the page count, or the zoom. The value is Semi Bold 13, the muted suffix
/// ("/ 1592") follows it 4 px after. The field is 32 px high, at least 64 px
/// wide, and it is as wide as its value and its suffix (10 px from the border).
/// The focus ring of 2 px lies inside of the field, the disabled field is
/// filled with bg/muted and its texts are text/disabled.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFToolBarField : public PDFLineEdit
{
    Q_OBJECT

private:
    using BaseClass = PDFLineEdit;

public:
    explicit PDFToolBarField(QWidget* parent = nullptr);
    explicit PDFToolBarField(const QString& text, const QString& suffix = QString(), QWidget* parent = nullptr);

    /// Suffix drawn after the value ("/ 1592", "%"), it is not a part of the edited text.
    /// It is also the description of the field for assistive technologies.
    const QString& getSuffix() const { return m_suffix; }
    void setSuffix(const QString& suffix);

    bool isSuffixVisible() const { return m_suffixVisible; }
    void setSuffixVisible(bool visible);

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void resizeEvent(QResizeEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

private:
    void init();
    void onTextChanged();
    void updateTextInsets();
    qreal getValueWidth() const;
    qreal getSuffixWidth() const;

    QString m_suffix;
    bool m_suffixVisible = true;
};

/// Rows of labels and values of a card (component Key value row): build
/// information, facts of a tool, properties of a document. One widget draws all
/// rows of the card. A row is 28 px high, the label (Regular 12 text/tertiary)
/// has the width 120 and it wraps, the value (Medium 12 text/primary) is aligned
/// to the right and elided, its whole text is the tooltip of an elided value.
/// The context menu of a row copies its value.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFKeyValueList : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFKeyValueList(QWidget* parent = nullptr);

    /// Adds a row
    /// \returns Index of the row
    int addRow(const QString& key, const QString& value);

    /// Changes the value of the row
    void setValue(int index, const QString& value);

    /// Removes all rows
    void clear();

    int getRowCount() const { return static_cast<int>(m_rows.size()); }
    QString getKey(int index) const;
    QString getValue(int index) const;

    /// Returns the rectangle of the row in the coordinates of the widget
    QRect getRowRect(int index) const;

    /// Returns the index of the row at the position, -1 if there is no row
    int getRowAt(const QPoint& position) const;

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;
    virtual bool hasHeightForWidth() const override { return true; }
    virtual int heightForWidth(int width) const override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual bool event(QEvent* event) override;
    virtual void contextMenuEvent(QContextMenuEvent* event) override;

private:
    struct Row
    {
        QString key;
        QString value;
    };

    int getRowHeight(int index) const;
    void updateDescription();

    std::vector<Row> m_rows;
};

class PDFDocumentTabCloseButton;

/// Tabs of the documents (component Tab): a tab shows the lock of a locked
/// document (amber in every state), the dot of unsaved changes (in the color of
/// the label) and the progress of a running tool (line of 2 px at the bottom).
/// Every tab is 32 px high, also without the close button.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFDocumentTabBar : public QTabBar
{
    Q_OBJECT

private:
    using BaseClass = QTabBar;

public:
    explicit PDFDocumentTabBar(QWidget* parent = nullptr);
    virtual ~PDFDocumentTabBar() override;

    bool isTabLocked(int index) const;
    void setTabLocked(int index, bool locked);

    bool isTabUnsaved(int index) const;
    void setTabUnsaved(int index, bool unsaved);

    /// Progress of the tab in the range 0 to 1, a negative value hides the line
    qreal getTabProgress(int index) const;
    void setTabProgress(int index, qreal progress);

    /// Shows or hides the close button of the tab (the Home tab has none)
    void setTabClosable(int index, bool closable);
    bool isTabClosable(int index) const;

protected:
    virtual QSize tabSizeHint(int index) const override;
    virtual QSize minimumTabSizeHint(int index) const override;
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void tabInserted(int index) override;
    virtual void tabRemoved(int index) override;

private:
    struct TabState
    {
        bool locked = false;
        bool unsaved = false;
        qreal progress = -1.0;
    };

    const TabState* getTabState(int index) const;
    TabState* getTabState(int index);
    int getExtraWidth(int index) const;
    void updateAccessibleName(int index);
    void changeTabState(int index);
    void onTabMoved(int from, int to);
    void onCloseButtonClicked();

    friend class PDFDocumentTabCloseButton;

    /// Takes the close button out of its tab (called by the destructor of the button)
    void forgetCloseButton(PDFDocumentTabCloseButton* button);

    /// State of the tabs by their position. QTabBar keeps the data of a tab
    /// (tabData) for the application, so the state is held here.
    std::vector<TabState> m_tabs;

    /// The close buttons created by the tab bar, which are alive (each one takes
    /// itself out from its destructor, the destructor of the tab bar detaches them)
    QList<PDFDocumentTabCloseButton*> m_closeButtons;
};


/// Range of pages written by the user ("1-3, 8, 10-12"). The syntax is the
/// syntax of PDFClosedIntervalSet::parse of the core library (this library does
/// not depend on it): parts separated by commas, each part is a page or a range
/// "a-b" with optional bounds ("-5" is 1 to 5, "8-" is 8 to the last page, "-"
/// is the whole document), white space around the numbers and empty parts are
/// allowed. Moreover the separators of other scripts and keyboards are accepted
/// (parts: the Arabic comma U+060C, the fullwidth comma U+FF0C, the ideographic
/// comma U+3001; ranges: the en dash U+2013, the minus sign U+2212, the hyphen
/// U+2010, the non-breaking hyphen U+2011, the fullwidth hyphen-minus U+FF0D)
/// and the decimal digits of any script of the Basic Multilingual Plane are
/// numbers (Arabic-Indic digits of a right-to-left keyboard, fullwidth digits);
/// digits outside of it (mathematical digits, Adlam) are not numbers.
/// Pages are numbered from 1 (the physical order of the pages, not the labels).
/// The parser is robust: it never overflows, a number of any length is
/// handled, the result has at most the count of the pages of the document.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFPageRange
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFPageRange)

public:
    PDFPageRange() = delete;

    /// Largest count of pages of a document (a larger count is bounded)
    static constexpr int MAX_PAGE_COUNT = 10'000'000;

    /// Closed interval of pages (first <= last)
    struct Interval
    {
        int first = 1;
        int last = 1;

        bool operator==(const Interval&) const = default;
    };

    /// Result of the parsing: sorted and merged intervals, or an error. The
    /// functions assume normalized intervals (as \p parse and \p normalized give
    /// them); other intervals are tolerated: an interval with first > last
    /// counts no page, \p getPages gives at most MAX_PAGE_COUNT pages.
    struct PDF4QTLIBFLUENTSHARED_EXPORT Result
    {
        QList<Interval> intervals;  ///< Sorted, disjoint, not adjacent; empty for an error
        QString errorMessage;       ///< The first error of the text, empty for a valid text

        bool isValid() const { return errorMessage.isEmpty(); }

        /// Returns the count of the pages of the intervals
        qint64 getCount() const;

        /// Returns true, if the page (numbered from 1) is in the range
        bool contains(int page) const;

        /// Returns the pages in the ascending order (at most MAX_PAGE_COUNT
        /// pages; a list of all pages of a huge document is large, prefer the
        /// intervals)
        QList<int> getPages() const;
    };

    /// Parses the text of a range of pages of a document of the page count (a
    /// count above MAX_PAGE_COUNT is bounded, the messages say the bounded count).
    /// Errors (the first one is reported, the result has no interval then):
    /// a part, which is not a number ("Can't convert '%1' to a number."; the
    /// part is isolated by U+2068 and U+2069, so a text of another direction
    /// does not reorder the message), a page, which the document does not have
    /// (\p getMissingPageText), a reversed range ("Closed interval [%1, %2] is
    /// invalid."). The numbers typed by the user are repeated as they are, the
    /// count of the pages is formatted by the default QLocale. An empty text is
    /// a valid empty range.
    static Result parse(const QString& text, int pageCount);

    /// Returns the intervals sorted and merged (overlapping and adjacent ones are
    /// joined); invalid intervals (first > last, first < 1) are left out
    static QList<Interval> normalized(QList<Interval> intervals);

    /// Returns the intervals as a text of the range ("1-3, 8, 10-12")
    static QString format(const QList<Interval>& intervals);

    /// Returns the text of the error of a page, which does not exist ("Page 60
    /// does not exist – the document has 48 pages"; the plural form of the
    /// language, the count formatted by the default QLocale)
    static QString getMissingPageText(qint64 page, int pageCount);

    /// Returns true, if the character separates two parts of a range (comma, Arabic, fullwidth and ideographic comma)
    static bool isListSeparator(QChar character);

    /// Returns true, if the character separates the bounds of a range (hyphen-minus, en dash, minus sign, hyphens)
    static bool isRangeSeparator(QChar character);
};

/// Common page selection of the tools (component Page selection 265:985): the
/// header with the count of the pages of the document, the modes All / Current
/// / Selected / Range as a segmented control dividing the width equally, the
/// range field (mode Range), the filters Even pages / Odd pages / Portrait /
/// Landscape (check boxes in two columns), the status with the count of the
/// selected pages and the error row.
///
/// The result is the list of the pages (numbered from 1) of the mode narrowed
/// by the filters. The filters of one kind are joined: Even and Odd together
/// (as no parity filter) keep all pages, the same for Portrait and Landscape;
/// the two kinds narrow each other. The orientation of a page comes from the
/// sizes given by \p setPageSizes (a page wider than high is landscape); a page
/// of an unknown size passes no orientation filter. Ranges are in the physical
/// order of the pages, the labels of the pages (\p setPageLabels) are shown next
/// to the numbers in the status ("7 pages selected · 3 (iii) – 12 (xii)").
///
/// An invalid range is the state Error of the design: the error row with the
/// message is shown (the widget is 24 px higher), the range field has the red
/// border and the message as its description, assistive technologies announce
/// it (assertively, when the selection becomes invalid, politely, when only the
/// message changes). An error of the application (\p setCustomError) is shown
/// in any mode. A page selection, which cannot be used now (a running tool), is
/// disabled by QWidget::setEnabled (decision 15): its texts are text/disabled.
///
/// The counts and the page numbers of the status and of the header are
/// formatted by the default QLocale with the plural forms of the language.
///
/// The result of a huge document is not materialized: the widget keeps the
/// intervals of the mode and the filters, the count and the first and the last
/// page are computed arithmetically (a parity filter) or over the known sizes
/// of the pages (an orientation filter). An input, which the current mode does
/// not use (the current page outside of the mode Current, the selected pages
/// outside of the mode Selected, the sizes without an orientation filter), only
/// is stored.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFPageSelection : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(int pageCount READ getPageCount WRITE setPageCount)
    Q_PROPERTY(pdf::PDFPageSelection::Mode mode READ getMode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(pdf::PDFPageSelection::Filters filters READ getFilters WRITE setFilters NOTIFY filtersChanged)
    Q_PROPERTY(QString range READ getRange WRITE setRange NOTIFY rangeChanged)

private:
    using BaseClass = QWidget;

public:
    /// Longest text of the range (the limit of QLineEdit; a longer text is cut)
    static constexpr int MAX_RANGE_LENGTH = 32767;

    /// Mode of the selection (the order of the segments)
    enum class Mode
    {
        All,        ///< All pages of the document
        Current,    ///< The current page
        Selected,   ///< The pages selected in the Thumbnails panel
        Range       ///< The range written into the field
    };
    Q_ENUM(Mode)

    /// Filter of the pages
    enum class Filter
    {
        None = 0x0,
        Even = 0x1,
        Odd = 0x2,
        Portrait = 0x4,
        Landscape = 0x8
    };
    Q_DECLARE_FLAGS(Filters, Filter)
    Q_FLAG(Filters)

    explicit PDFPageSelection(QWidget* parent = nullptr);
    virtual ~PDFPageSelection() override;

    /// Count of the pages of the document (bounded to 0 to PDFPageRange::MAX_PAGE_COUNT)
    int getPageCount() const { return m_pageCount; }
    void setPageCount(int pageCount);

    /// Current page (numbered from 1; 0 for none, a page out of the document selects nothing)
    int getCurrentPage() const { return m_currentPage; }
    void setCurrentPage(int page);

    /// Pages selected in the Thumbnails panel (numbered from 1; sorted, without
    /// duplicates; pages, which the document does not have, are left out of the result)
    const QList<int>& getSelectedPages() const { return m_selectedPages; }
    void setSelectedPages(const QList<int>& pages);

    /// Labels of the pages ("iii"); the label of the page n is the item n - 1
    const QStringList& getPageLabels() const { return m_pageLabels; }
    void setPageLabels(const QStringList& labels);

    /// Sizes of the pages as they are shown (after the rotation of the page), for
    /// the orientation filters; the size of the page n is the item n - 1
    const QList<QSizeF>& getPageSizes() const { return m_pageSizes; }
    void setPageSizes(const QList<QSizeF>& sizes);

    Mode getMode() const { return m_mode; }
    void setMode(Mode mode);

    /// Text of the range field (also when another mode is selected); a text
    /// longer than MAX_RANGE_LENGTH is cut to it
    const QString& getRange() const { return m_range; }
    void setRange(const QString& range);

    Filters getFilters() const { return m_filters; }
    void setFilters(Filters filters);

    /// Label of the header ("Pages" by default)
    const QString& getLabel() const { return m_label; }
    void setLabel(const QString& label);

    /// Error of the application shown in any mode (empty for none). The error
    /// of the range is shown before it in the mode Range.
    const QString& getCustomError() const { return m_customError; }
    void setCustomError(const QString& message);

    /// Returns the shown error (the error of the range or of the application), empty without an error
    QString getError() const;

    /// Returns the error of the text of the range (also when another mode is selected)
    const QString& getRangeError() const { return m_rangeError; }

    /// Returns true, if no error is shown
    bool isValid() const { return getError().isEmpty(); }

    /// Returns the result: the pages of the mode narrowed by the filters
    /// (numbered from 1, ascending). The list has one item for each page, up to
    /// PDFPageRange::MAX_PAGE_COUNT items (40 MB) for a huge document; prefer
    /// \p getResultIntervals or \p getResultCount.
    QList<int> getResultPages() const;

    /// Returns the result as sorted disjoint intervals. They are built at the
    /// first call after a change of the result and kept until the next change;
    /// a parity filter gives one interval for each page.
    const QList<PDFPageRange::Interval>& getResultIntervals() const;

    /// Returns the count of the pages of the result
    qint64 getResultCount() const { return m_summary.count; }

    /// Returns the text of the status ("7 pages selected · 3 (iii) – 12 (xii)")
    const QString& getStatusText() const { return m_statusText; }

    /// Nested widgets (for the tests, the styling and the accessibility)
    PDFSegmentedControl* getModeControl() const { return m_modes.data(); }
    PDFLineEdit* getRangeField() const { return m_rangeField.data(); }
    QCheckBox* getFilterCheckBox(Filter filter) const;

    /// Returns the painted parts (header, status, error) for PDFComponentPainter::paintPageSelection
    PDFComponentPainter::PageSelection getContent() const;

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;
    virtual bool hasHeightForWidth() const override { return true; }
    virtual int heightForWidth(int width) const override;

Q_SIGNALS:
    /// The mode has been changed (by the user or by \p setMode)
    void modeChanged(pdf::PDFPageSelection::Mode mode);

    /// The filters have been changed
    void filtersChanged(pdf::PDFPageSelection::Filters filters);

    /// The text of the range has been changed
    void rangeChanged(const QString& range);

    /// The result (the pages, their count), the text of the status (also by new
    /// labels of the pages or by a new language) or the error has been changed.
    /// It is emitted after the state of the widget (the layout of its children
    /// included) has been updated. A slot may delete the widget, when the change
    /// comes from a setter called by the application; for a change made by the
    /// user (typing into the range field, a click on a segment or on a filter)
    /// or by a change of the language, use deleteLater (the event or the signal
    /// of Qt is still being delivered to the widget or to its child).
    void resultChanged();

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void resizeEvent(QResizeEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

private:
    /// Parts of the state recomputed by \p updateResult
    enum UpdateFlag
    {
        UpdateTexts = 0x0,      ///< The error and the texts for assistive technologies (always)
        UpdateRange = 0x1,      ///< Parse the text of the range again
        UpdateResult = 0x2,     ///< The intervals of the mode and the summary of the result
        UpdateStatus = 0x4,     ///< The text of the status
        UpdateSizes = 0x8,      ///< The sizes of the pages have changed (the result of an orientation filter changes)
        UpdateAll = UpdateRange | UpdateResult | UpdateStatus
    };

    /// Summary of the result: the count and the first and the last page
    struct Summary
    {
        qint64 count = 0;
        int first = 0;
        int last = 0;

        bool operator==(const Summary&) const = default;
    };

    void retranslate();
    void updateResult(int flags);
    void layoutChildren();
    void onModeIndexChanged(int index);
    void onRangeTextChanged(const QString& text);
    void onFilterToggled();
    void onThemeChanged();
    static void addToSummary(Summary& summary, qint64 first, qint64 last, qint64 count);
    QString getPageText(int page) const;
    bool passesFilters(int page) const;
    bool isParityFilter() const;
    bool isOrientationFilter() const;
    QList<PDFPageRange::Interval> computeBase() const;
    Summary computeSummary(const QList<PDFPageRange::Interval>& base) const;
    QString computeStatus() const;
    static QStringList getModeTexts();
    static QStringList getFilterTexts();

    QPointer<PDFSegmentedControl> m_modes;
    QPointer<PDFLineEdit> m_rangeField;
    QPointer<QCheckBox> m_filterBoxes[4];
    QString m_modesAccessibleName;
    QString m_rangeAccessibleName;

    int m_pageCount = 0;
    int m_currentPage = 0;
    QList<int> m_selectedPages;
    QStringList m_pageLabels;
    QList<QSizeF> m_pageSizes;
    Mode m_mode = Mode::All;
    Filters m_filters = Filter::None;
    QString m_label;
    bool m_isLabelDefault = true;
    QString m_range;
    QString m_customError;
    QString m_rangeError;
    QString m_shownError;
    QString m_accessibleName;
    QString m_accessibleDescription;
    QList<PDFPageRange::Interval> m_rangeIntervals;     ///< Parsed text of the range
    QList<PDFPageRange::Interval> m_base;               ///< Intervals of the mode before the filters
    Filters m_resultFilters = Filter::None;             ///< Filters of the summary
    Summary m_summary;
    mutable QList<PDFPageRange::Interval> m_result;     ///< Built by getResultIntervals
    mutable bool m_isResultBuilt = true;
    QString m_statusText;
};

/// Choice of the next tool of a result dialog (component Continue with
/// 417:1007): the header (title "Then continue with", note "optional", link
/// "Another tool…"), zero to three suggested tools (Tool choice) dividing the
/// width equally and the caption, which says what happens. Nothing is
/// preselected; a click on a tool selects it, a second click unselects it (an
/// exclusive group created by the widget, which can be left empty). The caption
/// is the text of no choice ("Nothing is chosen – the tool just finishes."), or
/// the pattern of the selected tool ("Add Page Numbers starts on the new
/// document after Merge finishes successfully.", tools.md 9.3 item 12; without
/// the name of the current tool "… after this tool finishes successfully.").
/// The row of the choices is as high as the tallest choice at its width (46 or
/// 48), the minimal width keeps every word of the names whole.
///
/// Keyboard: the choices are one stop of Tab (the last focused one; the selected
/// one or the first one, before a choice gets the focus), the arrows, Home and
/// End move the focus among them, Space and Enter select or unselect the
/// focused one.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFContinueWith : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    using Choice = PDFComponentPainter::ToolChoice;

    explicit PDFContinueWith(QWidget* parent = nullptr);
    virtual ~PDFContinueWith() override;

    const QString& getTitle() const { return m_title; }
    void setTitle(const QString& title);

    const QString& getTitleNote() const { return m_titleNote; }
    void setTitleNote(const QString& note);

    QString getLinkText() const;
    void setLinkText(const QString& text);

    /// Caption shown, when nothing is chosen
    const QString& getCaption() const { return m_caption; }
    void setCaption(const QString& caption);

    /// Caption of a selected tool: %1 is the name of the selected tool, %2 the
    /// name of the current tool (\p setCurrentToolName); a pattern without them is
    /// taken as it is
    const QString& getSelectionCaption() const { return m_selectionCaption; }
    void setSelectionCaption(const QString& pattern);

    /// Name of the tool, which has produced the result ("Merge"); the default
    /// caption of a selected tool says it. Empty by default ("this tool").
    const QString& getCurrentToolName() const { return m_currentToolName; }
    void setCurrentToolName(const QString& name);

    /// Returns the shown caption
    QString getCurrentCaption() const;

    /// Suggested tools; at most three are shown (the others are left out). A
    /// change of the choices clears the selection (nothing is preselected): the
    /// signal selectedChoiceChanged(-1) is emitted after the new choices are set.
    const QList<Choice>& getChoices() const { return m_choices; }
    void setChoices(const QList<Choice>& choices);

    /// Selected tool (an index into the choices), -1 for none
    int getSelectedChoice() const;
    void setSelectedChoice(int index);

    /// Nested widgets (nullptr for an index out of the shown choices)
    PDFToolChoice* getChoiceWidget(int index) const;
    PDFTextLink* getLinkWidget() const { return m_link.data(); }

    /// Returns the content drawn by PDFComponentPainter::paintContinueWith
    PDFComponentPainter::ContinueWith getContent() const;

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;
    virtual bool hasHeightForWidth() const override { return true; }
    virtual int heightForWidth(int width) const override;

Q_SIGNALS:
    /// The selected tool has been changed (-1: nothing is chosen)
    void selectedChoiceChanged(int index);

    /// The link "Another tool…" has been activated (the application opens the search of the catalog)
    void anotherToolRequested();

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void resizeEvent(QResizeEvent* event) override;
    virtual void changeEvent(QEvent* event) override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void retranslate();
    void layoutChildren();
    void updateDescription();
    void updateTabStop();
    void onChoiceToggled();
    void onThemeChanged();
    QString getDefaultSelectionCaption() const;

    QPointer<PDFTextLink> m_link;
    /// A member, not a child: the application cannot reach it by children() and delete it
    QButtonGroup m_group;
    QList<QPointer<PDFToolChoice>> m_choiceWidgets;
    QList<Choice> m_choices;
    QString m_title;
    QString m_titleNote;
    QString m_caption;
    QString m_selectionCaption;
    QString m_currentToolName;
    QPointer<PDFToolChoice> m_tabStop;
    int m_selectedChoice = -1;
    bool m_isTitleDefault = true;
    bool m_isNoteDefault = true;
    bool m_isLinkDefault = true;
    bool m_isCaptionDefault = true;
    bool m_isSelectionCaptionDefault = true;
    QString m_accessibleName;
    QString m_accessibleDescription;
};

/// Palette of the colors of a color button (component Color palette popover
/// 156:4240): the suggested colors and the recently used ones as round swatches
/// six in a row, and the link "More colors…". The current color is the selected
/// suggested swatch. A click on a swatch (or Space, Enter) selects its color:
/// it becomes the current color and the first recently used one (at most six,
/// without duplicates), the popup closes and \p colorSelected is emitted.
///
/// The palette is a popover window (created with the window flag Qt::Popup, as
/// \p PDFColorButton creates it), whose panel has the shadow of the popups of
/// the style; it closes by Esc, by a click outside of it and when the window,
/// which owns it, is hidden or closed. Without the flag it is a panel inside of
/// another widget. The design keeps the colors inline in a panel, where there is
/// room for them (a row of PDFColorSwatch); the popover is for the other places.
///
/// Keyboard: the suggested and the recent swatches are a grid, each grid is one
/// stop of Tab; the arrows move the focus in the grid (Up and Down by a row,
/// also between the grids), Home and End to its first and last swatch.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFColorPalette : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// Color of the palette with its name for assistive technologies (an invalid color is no color)
    struct Entry
    {
        QColor color;
        QString name;

        bool operator==(const Entry&) const = default;
    };

    /// Largest count of the suggested colors and of the recent colors
    static constexpr int MAX_SUGGESTED_COLORS = 48;
    static constexpr int MAX_RECENT_COLORS = 6;

    /// \param parent Parent widget (the color button, which opens it)
    /// \param flags Qt::Popup for a popover window
    explicit PDFColorPalette(QWidget* parent = nullptr, Qt::WindowFlags flags = Qt::WindowFlags());
    virtual ~PDFColorPalette() override;

    const QList<Entry>& getSuggestedColors() const { return m_suggested; }
    void setSuggestedColors(const QList<Entry>& colors);

    const QList<Entry>& getRecentColors() const { return m_recent; }
    void setRecentColors(const QList<Entry>& colors);

    /// Puts the color to the front of the recent colors
    void addRecentColor(const Entry& entry);

    /// Current color: the suggested swatch of the same color is selected (none, if no suggested color equals it)
    const QColor& getCurrentColor() const { return m_currentColor; }
    void setCurrentColor(const QColor& color);

    /// Returns the name of the color: the name of a suggested or recent color of the same value, or its hexadecimal value
    QString getColorName(const QColor& color) const;

    /// Returns true, if the palette is a popover window
    bool isPopup() const;

    /// Shows the popover below the widget (above it, when there is no room
    /// below), aligned with its leading edge, and gives the focus to the
    /// selected swatch. Nothing is done for a palette, which is not a popover,
    /// and for an invalid or hidden anchor.
    void popup(QWidget* anchor);

    /// Nested widgets (nullptr for an index out of the range)
    PDFColorSwatch* getSuggestedSwatch(int index) const;
    PDFColorSwatch* getRecentSwatch(int index) const;
    PDFTextLink* getMoreColorsLink() const { return m_more.data(); }

    /// Returns the content drawn by PDFComponentPainter::paintColorPalette
    PDFComponentPainter::ColorPalette getContent() const;

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

Q_SIGNALS:
    /// A color has been selected by the user (emitted after the popup is closed)
    void colorSelected(const QColor& color);

    /// "More colors…" has been activated (the application opens the full color picker)
    void moreColorsRequested();

    /// The popover has been closed (by any hide of the window). The signal is
    /// queued: it comes after Qt has hidden the window, so a slot may delete
    /// the palette; a palette opened again before it comes does not emit it.
    void closed();

    /// The suggested or the recent colors (or their names) have been changed
    void colorsChanged();

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void changeEvent(QEvent* event) override;
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void resizeEvent(QResizeEvent* event) override;
    virtual void hideEvent(QHideEvent* event) override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QMargins getPanelMargins() const;
    /// Creates or updates the swatches
    /// \returns False, if the palette has been deleted meanwhile (by a slot of toggled of a swatch)
    bool rebuild();
    /// Creates the missing swatches of the colors and shows the needed ones (the others are hidden)
    void fillSwatches(const QList<Entry>& entries, QList<QPointer<PDFColorSwatch>>& swatches, bool isSuggested);
    void layoutChildren();
    /// Selects the swatch of the current color
    /// \returns False, if the palette has been deleted meanwhile (a slot of
    ///          the signal toggled of a swatch may delete it)
    bool updateSelection();
    void updateTabStops(PDFColorSwatch* focused);
    void onSwatchClicked(PDFColorSwatch* swatch);
    /// Slot of the signal clicked of the swatches (the swatch is the sender)
    void onSwatchButtonClicked();
    void onMoreClicked();
    void onAnchorDestroyed();
    /// Queued after a hide: emits closed, unless the palette has been opened again meanwhile
    void onHiddenQueued(quint64 openCount);
    QList<PDFColorSwatch*> getVisibleSwatches(bool recent) const;
    bool moveFocus(PDFColorSwatch* swatch, int key);
    void retranslate();
    void watchAnchor(bool watch);

    QList<Entry> m_suggested;
    QList<Entry> m_recent;
    QColor m_currentColor;
    QList<QPointer<PDFColorSwatch>> m_suggestedSwatches;
    QList<QPointer<PDFColorSwatch>> m_recentSwatches;
    /// A member, not a child: the application cannot reach it by children() and delete it
    QButtonGroup m_group;
    QPointer<PDFTextLink> m_more;
    QPointer<QWidget> m_anchor;
    QPointer<QWidget> m_anchorWindow;
    QMetaObject::Connection m_anchorDestroyed;
    bool m_isOpen = false;
    quint64 m_openCount = 0;
};

/// Button of a color (component Color button 156:4239): the swatch of the
/// current color, its name and the chevron. A click (Space, Enter, Alt+Down, F4)
/// opens the palette of the colors (\p PDFColorPalette, owned by the button). The
/// focus ring of 2 px lies inside, the open palette draws the button pressed. A
/// disabled button is filled with bg/muted, its name is text/disabled, the swatch
/// keeps its color. The name of the button for assistive technologies is the
/// name of the color, unless the application sets its own (then the color is
/// the description); the button is expandable, expanded while its palette is
/// shown (as a combo box).
class PDF4QTLIBFLUENTSHARED_EXPORT PDFColorButton : public PDFComponentButton
{
    Q_OBJECT
    Q_PROPERTY(QColor color READ getColor WRITE setColor NOTIFY colorChanged)

private:
    using BaseClass = PDFComponentButton;

public:
    explicit PDFColorButton(QWidget* parent = nullptr);
    explicit PDFColorButton(const QColor& color, const QString& colorName = QString(), QWidget* parent = nullptr);
    virtual ~PDFColorButton() override;

    /// Current color (an invalid color is no color). A new color drops the name
    /// of the previous color set by \p setColorName (set the name after the color).
    const QColor& getColor() const { return m_color; }
    void setColor(const QColor& color);

    /// Name of the current color ("Black"); without it the name from the palette
    /// or the hexadecimal value is shown. A color picked in the palette has the
    /// name of the palette.
    const QString& getColorName() const { return m_colorName; }
    void setColorName(const QString& colorName);

    /// Returns the shown name of the color
    QString getShownName() const;

    /// Returns the palette of the button (created at the first call)
    PDFColorPalette* getPalette();

    /// Opens the palette below the button; false, if the button is disabled or hidden
    bool showPalette();

    /// Closes the palette
    void hidePalette();

    /// Returns true, if the palette is shown
    bool isPaletteVisible() const;

    virtual bool hasPopup() const override { return true; }
    virtual bool hasExpandablePopup() const override { return true; }
    virtual bool isPopupShown() const override { return isPaletteVisible(); }

    /// Returns the content drawn by PDFComponentPainter::paintColorButton
    PDFComponentPainter::ColorButton getContent() const;

Q_SIGNALS:
    /// The color has been changed (by the palette or by \p setColor)
    void colorChanged(const QColor& color);

    /// A color has been selected in the palette by the user
    void colorSelected(const QColor& color);

    /// "More colors…" of the palette has been activated
    void moreColorsRequested();

protected:
    virtual void paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const override;
    virtual QSize getContentSizeHint() const override;
    virtual QSize getContentMinimumSizeHint() const override;
    virtual QString getDefaultAccessibleName() const override;
    virtual QString getDefaultAccessibleDescription() const override;
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual void changeEvent(QEvent* event) override;
    virtual void hideEvent(QHideEvent* event) override;

private:
    void init();
    void onPaletteClosed();
    void onPaletteDestroyed();
    void onPaletteColorSelected(const QColor& color);
    void onPaletteColorsChanged();
    void updateText();
    void notifyExpanded();

    QColor m_color;
    QString m_colorName;
    QPointer<PDFColorPalette> m_palette;
};

/// Signature or timestamp of the Signatures tab (component Signature card
/// 192:1032): the status badge, the signer, the meta line, the expand button
/// (Icon button 24, chevron down or up), the status tag ("Valid", "Needs
/// attention", "Invalid" by the status), the summary and, when expanded, the
/// details and the certificate chain from the signer to the root. All texts
/// except the tag are data of the application (review F-38, F-54). A link of a
/// certificate ("Add to trusted certificates") is a Text link, which emits
/// \p certificateLinkActivated. The height follows the width (texts wrap).
class PDF4QTLIBFLUENTSHARED_EXPORT PDFSignatureCard : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(bool expanded READ isExpanded WRITE setExpanded NOTIFY expandedChanged)

private:
    using BaseClass = QWidget;

public:
    using Status = PDFComponentPainter::SignatureStatus;
    using Detail = PDFComponentPainter::SignatureDetail;
    using Certificate = PDFComponentPainter::SignatureCertificate;

    explicit PDFSignatureCard(QWidget* parent = nullptr);
    virtual ~PDFSignatureCard() override;

    const QString& getSigner() const { return m_card.signer; }
    void setSigner(const QString& signer);

    const QString& getMeta() const { return m_card.meta; }
    void setMeta(const QString& meta);

    const QString& getSummary() const { return m_card.summary; }
    void setSummary(const QString& summary);

    /// Status of the signature (an invalid value is Valid)
    Status getStatus() const { return m_card.status; }
    void setStatus(Status status);

    bool isExpanded() const { return m_card.expanded; }
    void setExpanded(bool expanded);

    const QList<Detail>& getDetails() const { return m_card.details; }
    void setDetails(const QList<Detail>& details);

    const QList<Certificate>& getCertificates() const { return m_card.certificates; }
    void setCertificates(const QList<Certificate>& certificates);

    /// Caption of the chain ("Certificate chain" by default)
    const QString& getChainCaption() const { return m_card.chainCaption; }
    void setChainCaption(const QString& caption);

    /// Nested widgets (nullptr for a certificate without a link or out of the range)
    PDFIconButton* getToggleButton() const { return m_toggle.data(); }
    PDFTextLink* getCertificateLink(int index) const;

    /// Returns the content drawn by PDFComponentPainter::paintSignatureCard
    PDFComponentPainter::SignatureCard getContent() const;

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;
    virtual bool hasHeightForWidth() const override { return true; }
    virtual int heightForWidth(int width) const override;

Q_SIGNALS:
    /// The card has been expanded or collapsed (by the button or by \p setExpanded)
    void expandedChanged(bool expanded);

    /// The link of the certificate has been activated
    void certificateLinkActivated(int index);

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void resizeEvent(QResizeEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

private:
    void retranslate();
    void changeContent();
    void layoutChildren();
    void updateToggle();
    void updateLinks();
    void updateDescription();
    bool isExpandable() const;
    void onToggleClicked();
    /// Slot of the signal clicked of the links of the certificates (the link is the sender)
    void onCertificateLinkClicked();
    void onThemeChanged();

    PDFComponentPainter::SignatureCard m_card;
    bool m_isCaptionDefault = true;
    QString m_accessibleName;
    QString m_accessibleDescription;
    QPointer<PDFIconButton> m_toggle;
    QList<QPointer<PDFTextLink>> m_links;
};

/// Strength of a password or of an encryption algorithm (component Strength
/// meter 455:1270): the level in words (red, amber, green), the detail on the
/// right (the measured entropy), a bar of 20 cells filled in steps of 5 % and,
/// for passwords, the criteria, which make the password stronger (satisfied
/// ones green with a check). The level, the cells and the satisfied criteria are
/// data of the application. Without interaction; its name is the level, its
/// description the detail and the criteria, a new level (except Empty) of a
/// visible meter is announced politely to assistive technologies.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFStrengthMeter : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(pdf::PDFComponentPainter::StrengthLevel level READ getLevel WRITE setLevel NOTIFY levelChanged)

private:
    using BaseClass = QWidget;

public:
    using Level = PDFComponentPainter::StrengthLevel;
    using Criterion = PDFComponentPainter::StrengthCriterion;
    using Criteria = PDFComponentPainter::StrengthCriteria;

    explicit PDFStrengthMeter(QWidget* parent = nullptr);

    /// Level (an invalid value is Empty)
    Level getLevel() const { return m_meter.level; }
    void setLevel(Level level);

    /// Text of the level Empty ("Password strength" by default)
    const QString& getTitle() const { return m_meter.title; }
    void setTitle(const QString& title);

    /// Detail on the right ("118 of 128 bits")
    const QString& getDetail() const { return m_meter.detail; }
    void setDetail(const QString& detail);

    bool isDetailVisible() const { return m_meter.detailVisible; }
    void setDetailVisible(bool visible);

    bool isCriteriaVisible() const { return m_meter.criteriaVisible; }
    void setCriteriaVisible(bool visible);

    /// Satisfied criteria
    Criteria getSatisfiedCriteria() const { return m_meter.satisfied; }
    void setSatisfiedCriteria(Criteria criteria);

    /// Filled cells of 20; a negative value fills the cells of the level (the
    /// default); the level Empty has no filled cell
    int getFilledCells() const { return PDFComponentPainter::getStrengthMeterFilledCells(m_meter); }
    void setFilledCells(int cells);

    /// Own text of a criterion (an empty text is the default text)
    void setCriterionText(Criterion criterion, const QString& text);

    /// Returns the text of the level ("Weak") or the title
    QString getLevelText() const;

    /// Returns the content drawn by PDFComponentPainter::paintStrengthMeter
    const PDFComponentPainter::StrengthMeter& getContent() const { return m_meter; }

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;
    virtual bool hasHeightForWidth() const override { return true; }
    virtual int heightForWidth(int width) const override;

Q_SIGNALS:
    /// The level has been changed
    void levelChanged(pdf::PDFComponentPainter::StrengthLevel level);

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

private:
    void retranslate();
    void changeContent(bool isGeometryChanged);
    void updateAccessibleTexts();
    void onThemeChanged();

    PDFComponentPainter::StrengthMeter m_meter;
    bool m_isTitleDefault = true;
    QString m_accessibleName;
    QString m_accessibleDescription;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(PDFPageSelection::Filters)

}   // namespace pdf

#endif // PDFCOMPONENTPANELS_H
