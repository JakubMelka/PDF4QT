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

#ifndef PDFAPPMANAGEPANELS_H
#define PDFAPPMANAGEPANELS_H

#include "pdfappglobal.h"
#include "pdfapptoolgui_images.h"
#include "pdfapptoolinputrow.h"
#include "pdfapptoolpanelparts.h"

#include "pdfaccenthue.h"

#include <QPointer>
#include <QString>
#include <QWidget>

#include <utility>
#include <vector>

namespace pdf
{

class PDFAppParagraph;
class PDFAppToolCallout;
class PDFAppToolCard;
class PDFAppToolOutputFilesSection;
class PDFAppToolSelectedItemCard;
class PDFDocumentToolGUI;
class PDFIconBadge;
class PDFKeyValueList;
class PDFLabel;
class PDFTextLink;

/// A row of the left panel of Manage Pages (checkpoint-2 387:12757, removed 387:12779): padding 8,
/// the icon tile 30 (radius 8, the icon 16; bg/accent-subtle for the newest checkpoint, bg/muted for
/// the others), 10 apart the title Semi Bold 12 and its line Regular 11 text/tertiary (1 apart), the
/// link Semi Bold 13 at the trailing edge ("Restore", "Restore all"). A click of the row says
/// clicked (the removed pages expand), the link linkActivated; both last.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppManageListRow : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    static constexpr int PADDING = 8;
    static constexpr int TILE = 30;
    static constexpr int ICON = 16;
    static constexpr int GAP = 10;
    static constexpr int RADIUS = 8;

    /// What the row shows
    struct Content
    {
        QString key;            ///< Id of the checkpoint, of the removed item, empty = the bin
        QString iconName;       ///< "flag", "trash-restore"
        bool isAccent = false;  ///< The tile is bg/accent-subtle (the newest checkpoint)
        QString title;          ///< "Before grouping", "15 pages"
        QString text;           ///< "10:42 · 19 pages", "Kept until the assembly is closed"
        QString linkText;       ///< "Restore", "Restore all"

        bool operator==(const Content&) const = default;
    };

    explicit PDFAppManageListRow(QWidget* parent);
    virtual ~PDFAppManageListRow() override;

    const Content& getContent() const { return m_content; }
    void setContent(const Content& content);

    PDFTextLink* getLink() const { return m_link.data(); }
    PDFLabel* getTitleLabel() const { return m_title.data(); }
    PDFLabel* getTextLabel() const { return m_text.data(); }
    PDFIconBadge* getTile() const { return m_tile.data(); }

Q_SIGNALS:
    void clicked(const QString& key);
    void linkActivated(const QString& key);

protected:
    virtual void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void onLinkClicked();

    Content m_content;
    QPointer<PDFIconBadge> m_tile;
    QPointer<PDFLabel> m_title;
    QPointer<PDFLabel> m_text;
    QPointer<PDFTextLink> m_link;
};

/// The left panel of Manage Pages (387:12569 sidebar-left; plan of step 3b of phase 2, chapter 2;
/// package M3):
///  - Sources (the panel Files of Merge: "Sources" Semi Bold 14, "3 files · 16 of 31 pages used",
///    a row of each source with its letter and hue, "6 of 6 pages used", "Image · used once"; in
///    the tab of a document the Tag "this document" of the source A, 1479:34632);
///  - Checkpoints (387:12748: the header Semi Bold 13 with the link "Save checkpoint", the rows of
///    the checkpoints, the newest first);
///  - Removed pages (387:12772: "15 pages", "Kept until the assembly is closed" / in the tab of a
///    document "Kept until the tool is closed" KB4, the link "Restore all"); a click of the row
///    expands the removed items, each with its "Restore" (no frame, plan 2).
/// Without an item only the Sources with the hint of 409:32739. The panel only says what was
/// chosen; the GUI changes the assembly. Rows are made again only when their number changes.
/// Signals: emitted last (a slot may rebuild or delete the panel).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppManageSourcesPanel : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// Gap of the rows and of the sections (387:12596: rows 10 apart, a header 10 below a row)
    static constexpr int ROW_GAP = 10;
    static constexpr int HEADER_HEIGHT = 24;

    explicit PDFAppManageSourcesPanel(QWidget* parent);
    virtual ~PDFAppManageSourcesPanel() override;

    /// The sources: the header, the rows and the hint (409:32739 without files)
    void setSources(const QString& title, const QString& subtitle, const std::vector<PDFAppToolInputRow::Content>& rows, const QString& hint);

    /// The checkpoints (newest first) and whether the sections below the sources are shown
    void setCheckpoints(const std::vector<PDFAppManageListRow::Content>& rows, bool isShown);

    /// The bin: its row (an empty title hides the section) and the rows of its items, when it
    /// is expanded
    void setRemoved(const PDFAppManageListRow::Content& row, const std::vector<PDFAppManageListRow::Content>& itemRows);
    bool isRemovedExpanded() const { return m_isRemovedExpanded; }
    void setRemovedExpanded(bool isExpanded);

    /// The current source (the source of the current page)
    void setCurrentSource(const QString& sourceId);

    /// The links and the rows can be used (false while the tool runs)
    void setEditable(bool isEditable);

    PDFAppToolInputFilesPanel* getSourcesPanel() const { return m_sources.data(); }
    PDFTextLink* getSaveCheckpointLink() const { return m_saveCheckpoint.data(); }
    std::vector<PDFAppManageListRow*> getCheckpointRows() const;
    PDFAppManageListRow* getRemovedRow() const { return m_removedRow.data(); }
    std::vector<PDFAppManageListRow*> getRemovedItemRows() const;
    PDFLabel* getCheckpointsTitle() const { return m_checkpointsTitle.data(); }
    PDFLabel* getRemovedTitle() const { return m_removedTitle.data(); }

Q_SIGNALS:
    void sourceClicked(const QString& sourceId);
    void sourceLinkActivated(const QString& sourceId);
    void saveCheckpointRequested();
    void restoreCheckpointRequested(const QString& checkpointId);
    void restoreAllRequested();
    void restoreRequested(const QString& removedItemId);
    void removedExpandedChanged(bool isExpanded);

private:
    void onSaveCheckpointClicked();
    void onCheckpointLinkActivated(const QString& key);
    void onRemovedLinkActivated(const QString& key);
    void onRemovedRowClicked(const QString& key);
    void onRemovedItemLinkActivated(const QString& key);
    void updateRows(QWidget* container, std::vector<QPointer<PDFAppManageListRow>>* rows, const std::vector<PDFAppManageListRow::Content>& contents, bool isCheckpoint);
    void retranslate();

    QPointer<PDFAppToolInputFilesPanel> m_sources;
    QPointer<QWidget> m_checkpointsSection;
    QPointer<PDFLabel> m_checkpointsTitle;
    QPointer<PDFTextLink> m_saveCheckpoint;
    QPointer<QWidget> m_checkpointRowsContainer;
    std::vector<QPointer<PDFAppManageListRow>> m_checkpointRows;
    QPointer<QWidget> m_removedSection;
    QPointer<PDFLabel> m_removedTitle;
    QPointer<PDFAppManageListRow> m_removedRow;
    QPointer<QWidget> m_removedRowsContainer;
    std::vector<QPointer<PDFAppManageListRow>> m_removedItemRows;
    bool m_isRemovedExpanded = false;

protected:
    virtual void changeEvent(QEvent* event) override;
};

/// The right panel of Manage Pages (387:12569 sidebar-right; 396:26763 with the Output files of
/// the outputs of more files; 1479:34632 / 1479:35631 in the tab of a document; 409:32739 "How
/// Manage Pages works"; package M3): Selection (PDFAppToolSelectedItemCard: the Tag "2 pages", the
/// link Rename, Source, Original pages, Size, Orientation, Rotation), Result (the rows given by the
/// GUI), the callout of the tab of a document ("The pages are rearranged in the file", "Owner
/// password required" with "Enter owner password…") and the note below. A value of Content is
/// shown; the panel says only the links. Signals emitted last.
///
/// Ownership: the parts are children; the output files section belongs to the GUI (it lives over
/// the parts made again after a change of the language), the panel describes it into its layout.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppManageResultPanel : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// Gap of the rows of the cards (387:13242: rows of 28, a pitch of 36)
    static constexpr int KEY_VALUE_ROW_GAP = 8;

    /// What the panel shows
    struct Content
    {
        bool isEmpty = false;                                       ///< No item: "How Manage Pages works" only (409:32739)
        bool hasSelection = false;                                  ///< The section Selection
        QString selectionTag;                                       ///< "2 pages"
        bool canRename = false;                                     ///< The link Rename
        std::vector<std::pair<QString, QString>> selectionRows;     ///< Source, Original pages, Size, Orientation, Rotation
        std::vector<std::pair<QString, QString>> resultRows;        ///< Pages, Outline, Estimated size, Output, Images (387:13277)
        bool isOutputFilesShown = false;                            ///< Output files (396:26763: One PDF per group / per page)
        PDFAccentHue calloutHue = PDFAccentHue::Blue;               ///< Blue: the info callout (icon info), Red: the lock callout (icon lock)
        QString calloutTitle;                                       ///< Empty = no callout
        QString calloutText;
        QString calloutLink;                                        ///< "Enter owner password…", empty = none
        QString note;                                               ///< "Save Assembly (Ctrl+S) keeps …", empty = none
    };

    explicit PDFAppManageResultPanel(PDFDocumentToolGUI* gui, PDFAppToolOutputFilesSection* outputFiles, QWidget* parent);
    virtual ~PDFAppManageResultPanel() override;

    void setContent(const Content& content);
    const Content& getContent() const { return m_content; }

    PDFAppToolSelectedItemCard* getSelectionCard() const { return m_selection.data(); }
    QWidget* getResultSection() const { return m_resultSection.data(); }
    PDFKeyValueList* getResultValues() const { return m_resultValues.data(); }
    PDFAppToolCallout* getInfoCallout() const { return m_infoCallout.data(); }
    PDFAppToolCallout* getLockCallout() const { return m_lockCallout.data(); }
    PDFTextLink* getCalloutLink() const { return m_calloutLink.data(); }
    PDFAppParagraph* getNote() const { return m_note.data(); }
    QWidget* getHowSection() const { return m_howSection.data(); }

Q_SIGNALS:
    void renameRequested();
    void calloutLinkActivated();

protected:
    virtual void changeEvent(QEvent* event) override;

private:
    void onRenameActivated();
    void onCalloutLinkClicked();
    void retranslate();

    Content m_content;
    QPointer<PDFAppToolSelectedItemCard> m_selection;
    QPointer<QWidget> m_resultSection;
    QPointer<PDFLabel> m_resultTitle;
    QPointer<PDFKeyValueList> m_resultValues;
    QPointer<PDFAppToolCallout> m_infoCallout;
    QPointer<PDFAppToolCallout> m_lockCallout;
    QPointer<PDFTextLink> m_calloutLink;
    QPointer<PDFAppParagraph> m_note;
    QPointer<QWidget> m_howSection;
    QPointer<PDFLabel> m_howTitle;
    QPointer<PDFAppParagraph> m_howText;
};

}   // namespace pdf

#endif // PDFAPPMANAGEPANELS_H
