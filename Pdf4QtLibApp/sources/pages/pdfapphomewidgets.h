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

#ifndef PDFAPPHOMEWIDGETS_H
#define PDFAPPHOMEWIDGETS_H

#include "pdfappglobal.h"
#include "pdfapplocale.h"
#include "pdfrecentrepository.h"
#include "pdfworkspacerepository.h"

#include "pdfaccenthue.h"
#include "pdfcomponentpainter.h"
#include "pdfthemecolors.h"

#include "pdffluentwidgets.h"

#include <QPixmap>
#include <QPushButton>
#include <QPointer>
#include <QWidget>

class QBoxLayout;

namespace pdf
{

class PDFAppParagraph;

/// Texts of the pages Home, Open and Recent (Figma 28:4, 61:1201, 80:910): times
/// ("Today 10:24", "Yesterday 16:15", "Sep 18, 2026"), sizes, page counts, the
/// reading state and the groups of the Recent list. Every text of the database
/// is untrusted: control characters are replaced and long texts are shortened
/// before they reach a component (toDisplayText).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppHomeFormat
{
public:
    PDFAppHomeFormat() = delete;

    /// Longest text of the database, which is shown (the components elide it further)
    static constexpr int MAXIMUM_DISPLAY_LENGTH = 512;

    /// Group of the Recent list by the time of opening
    enum class DateGroup
    {
        Today,
        Yesterday,
        ThisWeek,   ///< The last 7 days before yesterday
        Earlier     ///< Older, or unknown
    };

    /// Current time in milliseconds since the epoch (tests fix it by setCurrentTimeOverride)
    static qint64 getCurrentTime();

    /// Fixed current time (tests); 0 = the clock
    static void setCurrentTimeOverride(qint64 time);

    /// Time of recent activity (owner decision O25, PDFAppLocale::formatRecentTime):
    /// "Just now", "5 minutes ago", "Today 10:24", "Yesterday 16:15", "Sep 18, 2026";
    /// empty for an unknown time. Inside of a sentence (\p use InSentence: "Opened
    /// just now") "just now" has a small letter.
    static QString formatTime(qint64 time, qint64 now, PDFAppLocale::RecentTimeUse use = PDFAppLocale::RecentTimeUse::Standalone);

    /// "Last used Today 10:24" / "Not used yet" (folder cards)
    static QString formatLastUsed(qint64 time, qint64 now);

    /// "Last opened Today 10:24" (Continue where you left off)
    static QString formatLastOpened(qint64 time, qint64 now);

    /// "2.4 MB"
    static QString formatSize(qint64 bytes);

    /// "24 pages"
    static QString formatPageCount(int pageCount);

    /// "4 documents"
    static QString formatDocumentCount(int documentCount);

    /// "6 PDF files" ("10000+ PDF files" for a limited count)
    static QString formatPdfCount(int count, bool isLimited);

    /// "Page 12 of 24", "Page 12", "Finished", "Not started"
    static QString formatReading(const PDFRecentFile& file);

    /// Progress of a pinned card: "Finished · 36 pages" for a finished document, else formatReading
    static QString formatPinnedProgress(const PDFRecentFile& file);

    /// Progress of the bar of the Recent list (-1 = no bar: not started, unknown page count)
    static qreal getBarProgress(const PDFRecentFile& file);

    /// "50 %"
    static QString formatPercent(qreal progress);

    /// Details of a file of the Recent list: "C:\Users\Jakub\Documents · 24 pages · 2.4 MB"
    static QString formatFileDetails(const PDFRecentFile& file);

    /// Meta of a workspace row: "4 documents · Opened Yesterday 16:15" ("4 documents", when it was not opened)
    static QString formatWorkspaceMeta(const PDFWorkspace& workspace, qint64 now);

    /// Details of a workspace of the Recent list: "4 documents · Saved Sep 10, 2026"
    static QString formatWorkspaceDetails(const PDFWorkspace& workspace, qint64 now);

    /// Meta of Restore last session: "3 documents · closed Today 10:24"
    static QString formatSessionMeta(const PDFWorkspace& session, qint64 now);

    /// Time text of a widget, which follows the clock (owner decision O25): the
    /// minute ticker formats it again by updateTimeTexts
    enum class TimeText
    {
        Time,               ///< formatTime: a recent file row, a recent tool row, a label
        LastUsed,           ///< formatLastUsed: the meta of a folder card
        LastOpened,         ///< formatLastOpened: Continue where you left off
        WorkspaceMeta,      ///< formatWorkspaceMeta: the meta of a workspace row (opened)
        WorkspaceDetails,   ///< formatWorkspaceDetails: the details of a workspace (saved)
        SessionMeta         ///< formatSessionMeta: the meta of Restore last session
    };

    /// Names of the dynamic properties of a widget with a time text
    static constexpr const char* TIME_KIND_PROPERTY = "pdf4qtTimeKind";
    static constexpr const char* TIME_PROPERTY = "pdf4qtTime";
    static constexpr const char* TIME_COUNT_PROPERTY = "pdf4qtTimeCount";

    /// Text of the kind of a time text (\p count = the documents of a workspace)
    static QString formatTimeText(TimeText kind, qint64 time, qint64 now, int count);

    /// Sets the time text of the widget (PDFRecentFileRow, PDFRecentToolRow,
    /// PDFWorkspaceRow, PDFFolderCard, PDFAppSessionRow or a label) and remembers its
    /// time in dynamic properties, so updateTimeTexts formats it again
    static void setTimeText(QWidget* widget, TimeText kind, qint64 time, qint64 now, int count = 0);

    /// Formats the time texts of \p root and of its descendants set by setTimeText
    /// against \p now again: nothing is read, no widget is created or deleted
    static void updateTimeTexts(QWidget* root, qint64 now);

    /// Group of the time of opening
    static DateGroup getDateGroup(qint64 time, qint64 now);
    static QString getDateGroupText(DateGroup group);

    /// Hue and icon of the folder card of a recent folder (Figma 61:1201)
    static PDFAccentHue getFolderHue(PDFRecentFolderKind kind);
    static QString getFolderIconName(PDFRecentFolderKind kind);

    /// Icon of a pinned location (briefcase, cloud, server)
    static QString getLocationIconName(PDFPinnedLocationKind kind);

    /// Text of the database shown to the user: control characters and separators
    /// of lines replaced by spaces, lone surrogates removed, at most \p maximumLength characters
    static QString toDisplayText(const QString& text, int maximumLength = MAXIMUM_DISPLAY_LENGTH);

    /// Path for the user: native separators, toDisplayText
    static QString toDisplayPath(const QString& path);

    /// Search text of a route or a field: as toDisplayText, at most \p maximumLength
    static QString toSearchText(const QString& text, int maximumLength);
};

/// Single line of a text (a name, a path, a meta text) elided to the width of
/// the label by PDFComponentPainter::drawElidedText: a text of data keeps its own
/// direction, a name of a file is elided in the middle. The whole text is the
/// tooltip, when it is elided. The label is never wider than its layout gives it.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppElidedLabel : public PDFLabel
{
    Q_OBJECT

private:
    using BaseClass = PDFLabel;

public:
    explicit PDFAppElidedLabel(const QFont& font, PDFThemeColors::Color color, PDFComponentPainter::TextKind kind, QWidget* parent = nullptr);
    virtual ~PDFAppElidedLabel() override;

    PDFComponentPainter::TextKind getTextKind() const { return m_kind; }

    /// The text is elided at the current width
    bool isElided() const;

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual bool event(QEvent* event) override;

private:
    PDFComponentPainter::TextKind m_kind;
};

/// Area for a drop of PDF files (drop-zone of Open 61:1201 and of Home 28:4; a
/// frame of Main page, not a component): bg/subtle with a dashed border. The
/// large variant has the icon, the title, the text, Browse Files… and the hint,
/// the compact one (below the recent files of Home) the icon and a line. The
/// files are dropped onto the window (PDFAppMainWindow takes the drop), the
/// area only shows where. While local files are dragged over the window, the
/// window makes its areas active: bg/accent-subtle with the dashed border in the
/// accent color (bg/accent), the drop target is seen at once.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDropZone : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    enum class Variant
    {
        Large,      ///< Open: 246 px, border/strong dashed 6 / 4, radius 12
        Compact     ///< Home: 43 px, border/default dashed 4 / 3, radius 8
    };

    explicit PDFAppDropZone(Variant variant, QWidget* parent = nullptr);
    virtual ~PDFAppDropZone() override;

    Variant getVariant() const { return m_variant; }

    /// Browse Files… of the large variant (nullptr for the compact one)
    QPushButton* getBrowseButton() const { return m_browseButton; }

    /// Text of the shortcut of the hint ("Ctrl+O"), empty = the hint shows the filter only
    void setShortcutText(const QString& text);

    /// Files are dragged over the window (the area is highlighted)
    bool isDropActive() const { return m_isDropActive; }
    void setDropActive(bool isActive);

Q_SIGNALS:
    void browseRequested();

protected:
    virtual void paintEvent(QPaintEvent* event) override;

private:
    void updateHint();

    Variant m_variant;
    bool m_isDropActive = false;
    QString m_shortcutText;
    QPointer<QPushButton> m_browseButton;
    QPointer<PDFLabel> m_hint;
};

/// Restore last session (restore-last-session of Open 61:1201 and Recent 80:910;
/// a frame, not a component): the icon rotate-ccw in a circle, the name, the meta
/// ("3 documents · closed Today 10:24") and the button Restore. Open has it in
/// accent/blue, Recent in bg/subtle.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSessionRow : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAppSessionRow(bool isAccent, QWidget* parent = nullptr);
    virtual ~PDFAppSessionRow() override;

    void setMeta(const QString& meta);
    QString getMeta() const;

    QPushButton* getRestoreButton() const { return m_restoreButton; }

    bool isAccent() const { return m_isAccent; }

Q_SIGNALS:
    void restoreRequested();

protected:
    virtual void paintEvent(QPaintEvent* event) override;

private:
    bool m_isAccent = false;
    QPointer<PDFLabel> m_meta;
    QPointer<QPushButton> m_restoreButton;
};

/// Continue where you left off (card of Recent 80:910; a frame, not a component):
/// the thumbnail of the first page, the name, the folder, the time of opening,
/// the reading progress with its percent and bar, Continue reading.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppContinueCard : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAppContinueCard(QWidget* parent = nullptr);
    virtual ~PDFAppContinueCard() override;

    /// Space between the reading bar and Continue reading (gap 3, spacer 8, gap 3)
    static constexpr int BUTTON_SPACING = 14;

    /// Shows the file (\p now for the time of opening)
    void setFile(const PDFRecentFile& file, qint64 now);
    const QString& getPath() const { return m_path; }

    void setPixmap(const QPixmap& pixmap);
    const QPixmap& getPixmap() const;

    QString getName() const;
    QString getProgressText() const;
    qreal getProgress() const { return m_progress; }

    /// "Last opened 5 minutes ago" (follows the clock, O25)
    QString getOpenedText() const;

    QPushButton* getContinueButton() const { return m_continueButton; }

    /// Rectangle of the progress bar
    QRect getBarRect() const;

Q_SIGNALS:
    void continueRequested();

protected:
    virtual void paintEvent(QPaintEvent* event) override;

private:
    QString m_path;
    qreal m_progress = 0.0;
    QPointer<QWidget> m_thumbnail;
    QPointer<PDFLabel> m_name;
    QPointer<PDFLabel> m_folder;
    QPointer<PDFLabel> m_opened;
    QPointer<PDFLabel> m_progressText;
    QPointer<PDFLabel> m_percent;
    QPointer<QWidget> m_bar;
    QPointer<QPushButton> m_continueButton;
};

/// Empty state of the Recent list (pdf4qt-recent-empty 955:142865; a frame, not
/// a component): the icon clock in a circle, the title, the text and Browse Files…
///
/// The small variant is the empty state of a list card of Home (Recent Files,
/// Recent Tools; no frame in Figma, derived from 955:142865 by the scale of the
/// card): the icon circle 40 (bg/subtle, icon 20 icon/muted - PDFIconBadge
/// Neutral), gap 12, the title Semi Bold 14 text/primary, gap 4, the hint
/// Regular 12 (line 17) text/tertiary at most 280 wide, centered, the padding 20
/// above and below, no button.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEmptyState : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    enum class Size
    {
        Large,  ///< Recent (955:142865): circle 72, title Bold 22, text 14, Browse Files…
        Small   ///< List card of Home: circle 40, title Semi Bold 14, hint 12, no button
    };

    /// Extent of the icon circle and of its icon of the small variant
    static constexpr int SMALL_CIRCLE = 40;
    static constexpr int SMALL_ICON = 20;

    explicit PDFAppEmptyState(QWidget* parent = nullptr);
    PDFAppEmptyState(Size size, const QString& iconName, QWidget* parent = nullptr);
    virtual ~PDFAppEmptyState() override;

    Size getSize() const { return m_size; }
    QWidget* getIcon() const { return m_icon; }

    void setTexts(const QString& title, const QString& text);
    QString getTitle() const;
    QString getText() const;

    QPushButton* getBrowseButton() const { return m_browseButton; }

Q_SIGNALS:
    void browseRequested();

private:
    Size m_size = Size::Large;
    QPointer<QWidget> m_icon;
    QPointer<PDFLabel> m_title;
    QPointer<PDFAppParagraph> m_text;
    QPointer<QPushButton> m_browseButton;
};

}   // namespace pdf

#endif // PDFAPPHOMEWIDGETS_H
