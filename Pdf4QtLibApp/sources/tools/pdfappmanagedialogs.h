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

#ifndef PDFAPPMANAGEDIALOGS_H
#define PDFAPPMANAGEDIALOGS_H

#include "pdfappglobal.h"
#include "pdfappassemblyloader.h"
#include "pdfapptoolgui_insert.h"
#include "pdfapptoolgui_split.h"

#include "pdfcomponentbuttons.h"
#include "pdfcomponentpanels.h"
#include "pdfdocumentassembly.h"
#include "pdffluentwidgets.h"
#include "pdfmessagebox.h"
#include "pdfoutlinebreakpoints.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QListView>
#include <QMarginsF>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QString>
#include <QStringList>

#include <utility>
#include <vector>

namespace pdf
{

class PDFAppParagraph;
class PDFAppSplitBookmarkModel;
class PDFAppThumbnailScheduler;
class PDFAppToolInputList;
class PDFAppToolPageGridView;
class PDFAppToolPageRangeField;
class PDFAppToolPageTileDelegate;
class PDFDoubleSpinBox;
class PDFKeyValueList;
class PDFLabel;
class PDFLineEdit;
class PDFSpinBox;
class PDFTextLink;

/// The dialogs of Manage Pages (plan of step 3b of phase 2, chapter 2; package M3): the six of
/// 387:15926 - Select page range, Regroup by bookmarks, Add pages of a PDF, Properties, Regroup
/// every N pages, Regroup at page numbers - and the dialogs without a frame (Q17 of step 2, built
/// from them): Crop… (dialog width 520), Rename… / the name of a checkpoint (the field of Regroup at
/// page numbers), Move to… (the field of Regroup every N pages). The close question 405:30894 is
/// PDFAppManageToolGUI::askOwnTabClose.
///
/// Every dialog is a PDFMessageBox of the design without its badge (a form): the title Semi Bold
/// 16, the parts, Cancel and the Primary button at the trailing edge. It is created by new with
/// the window as its parent and WA_DeleteOnClose, held by a QPointer and opened by open() (never
/// on the stack, CLAUDE.md); it holds only values (the assembly is an immutable value, RB7) - no
/// pointer to the GUI, the canvas or a document. Its decision is said by its signal, emitted last,
/// before the dialog closes itself (the slot may delete the dialog: a QPointer guards the close).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppManageDialogs
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppManageDialogs)

public:
    PDFAppManageDialogs() = delete;

    /// Width of the dialogs without a frame (the dialog Crop… of Q17, as the dialog of a result)
    static constexpr int DIALOG_WIDTH = 520;

    /// Width of the dialogs of 387:15926 (Select page range 400, Add pages of a PDF 420)
    static constexpr int FORM_WIDTH = 400;
    static constexpr int ADD_PAGES_WIDTH = 420;

    /// "17 selected pages → 3 groups (6, 6 and 5 pages)" of Every N pages (387:15926)
    static QString getEveryText(int pageCount, int pagesPerGroup);

    /// The positions of "7, 13" (from 1, each in 2 to \p pageCount, ascending, unique); an empty
    /// result and \p errorMessage for an invalid text
    static std::vector<int> parsePositions(const QString& text, int pageCount, QString* errorMessage);

    /// "17 selected pages → 3 groups (1–6, 7–12, 13–17)" of At page numbers (387:15926)
    static QString getPositionsText(int pageCount, const std::vector<int>& positions);

    /// The positions in the assembly (from 1) of the pages of the selected items as the text of
    /// the field of Select page range (387:15926 "1-3, 8, 10-", owner answer 7 of 12. 11.): the
    /// ascending runs, a run of two or more pages "a-b", a run of them up to the last page of the
    /// assembly "a-"; the ASCII hyphen, which PDFPageRangeList reads. Empty without a selected
    /// page, or when the text would be longer than PDFPageRangeList::MAXIMUM_TEXT_LENGTH.
    static QString getSelectionRangeText(const PDFDocumentAssembly& assembly, const PDFDocumentAssembly::Selection& selection);
};

/// The base of the dialogs of Manage Pages: a form of the design (no badge, the card width, the
/// scrim over \p scrimTarget), Cancel and the Primary button. The subclass adds its parts by
/// addContent and says its decision by its own signal in \p acceptDialog.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppManageDialog : public PDFMessageBox
{
    Q_OBJECT

private:
    using BaseClass = PDFMessageBox;

public:
    explicit PDFAppManageDialog(const QString& title, const QString& acceptText, int width, QWidget* scrimTarget, QWidget* parent);
    virtual ~PDFAppManageDialog() override;

    QPushButton* getAcceptButton() const { return m_acceptButton.data(); }
    QPushButton* getCancelButton() const { return m_cancelButton.data(); }

    /// The Primary button (Enter) - the decision of the dialog, when it may be accepted
    void triggerAccept();

protected:
    /// Says the decision (its signal, last); the dialog closes after it, when it still lives
    virtual void acceptDialog() = 0;

    /// The Primary button may be used now
    void setAcceptEnabled(bool isEnabled);

    /// A label of a group of the form ("Numbers mean", Semi Bold 12)
    QWidget* createCaption(const QString& text);

    /// A line of the status below the parts ("5 items will be selected", Regular 11 text/accent;
    /// an error text/danger)
    PDFLabel* createStatus();
    static void setStatus(PDFLabel* label, const QString& text, bool isError);

private:
    void onAcceptClicked();
    void onCancelClicked();

    QPointer<QPushButton> m_acceptButton;
    QPointer<QPushButton> m_cancelButton;
};

/// Select page range (387:15926): the range ("1-3, 8, 10-", the examples), "Numbers mean" Position
/// in the assembly / Original pages of <source> (a PDF source of the selection or of the current
/// item), "Selection" Replace / Add, the count of the items "5 items will be selected" (the rule of
/// the core, PDFDocumentAssembly::select).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppManageRangeDialog : public PDFAppManageDialog
{
    Q_OBJECT

private:
    using BaseClass = PDFAppManageDialog;

public:
    explicit PDFAppManageRangeDialog(PDFDocumentAssembly assembly, PDFDocumentAssembly::Selection selection, const QString& sourceId, QWidget* scrimTarget, QWidget* parent);
    virtual ~PDFAppManageRangeDialog() override;

    PDFAppToolPageRangeField* getRangeField() const { return m_range.data(); }
    QRadioButton* getPositionRadio() const { return m_positionRadio.data(); }
    QRadioButton* getOriginalRadio() const { return m_originalRadio.data(); }
    QRadioButton* getReplaceRadio() const { return m_replaceRadio.data(); }
    QRadioButton* getAddRadio() const { return m_addRadio.data(); }
    PDFLabel* getStatusLabel() const { return m_status.data(); }

    /// The values of the form now
    PDFDocumentAssembly::SelectSettings getSettings() const;

Q_SIGNALS:
    void selectRequested(const pdf::PDFDocumentAssembly::SelectSettings& settings);

protected:
    virtual void acceptDialog() override;

private:
    void updateStatus();

    PDFDocumentAssembly m_assembly;
    PDFDocumentAssembly::Selection m_selection;
    QString m_sourceId;
    QPointer<PDFAppToolPageRangeField> m_range;
    QPointer<QRadioButton> m_positionRadio;
    QPointer<QRadioButton> m_originalRadio;
    QPointer<QRadioButton> m_replaceRadio;
    QPointer<QRadioButton> m_addRadio;
    QPointer<PDFLabel> m_status;
};

/// Regroup by bookmarks (387:15926): "A new group starts at each checked bookmark of Annex_A.pdf.",
/// the checklist of the bookmarks of the file (the model and the rows of Split At bookmarks), the
/// links Select all, Level 1, Level 2, Invert. Regroup says the pages of the source (from 0), where
/// a group begins, and their titles (PDFOutlineBreakPoints::createGroups).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppManageBookmarksDialog : public PDFAppManageDialog
{
    Q_OBJECT

private:
    using BaseClass = PDFAppManageDialog;

public:
    static constexpr int LIST_HEIGHT = 136;

    explicit PDFAppManageBookmarksDialog(const QString& fileName, std::vector<PDFOutlineBreakPoints::Bookmark> bookmarks, std::vector<PDFInteger> pages, QWidget* scrimTarget, QWidget* parent);
    virtual ~PDFAppManageBookmarksDialog() override;

    PDFAppSplitBookmarkModel* getModel() const { return m_model.data(); }
    QListView* getView() const { return m_view.data(); }

    /// Toggles the bookmark of the row; the links
    void toggleRow(int row);
    void selectAll();
    void selectLevel(int level);
    void invert();

    /// The groups of the checked bookmarks
    PDFDocumentAssembly::RegroupSettings getSettings() const;

Q_SIGNALS:
    void regroupRequested(const pdf::PDFDocumentAssembly::RegroupSettings& settings);

protected:
    virtual void acceptDialog() override;

private:
    void onViewClicked(const QModelIndex& index);
    void onSelectAllClicked();
    void onLevel1Clicked();
    void onLevel2Clicked();
    void onInvertClicked();
    void setCheckedRows(const std::vector<int>& rows);

    std::vector<PDFInteger> m_pages;
    QPointer<PDFAppSplitBookmarkModel> m_model;
    QPointer<QListView> m_view;
};

/// Add pages of a PDF (387:15926), also Replace selection… (Ctrl+Alt+R): the file (name, pages) on
/// bg/subtle, All pages / Page range (the ordered range of K23, the examples), the previews of the
/// chosen pages in the order of the range (387:16763: tiles 53 x 84 with the box 41 x 56 and the
/// page number, 4 apart, two rows high; the rest scrolls), "33 of 36 pages · added after the
/// selection", "Add 33 Pages" (Replace: "Replace with 33 Pages").
///
/// The previews (setPreviewSource): the pictures of the pages of the input of the list (the model
/// of the tiles of Insert, PDFAppInsertSourceModel, and the thumbnail scheduler); the list is held
/// by a QPointer (it belongs to the tab or to the GUI and may go first - then no pictures). The
/// model, the view, the delegate and the scheduler are children of the dialog.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppManageAddPagesDialog : public PDFAppManageDialog
{
    Q_OBJECT

private:
    using BaseClass = PDFAppManageDialog;

public:
    /// The previews of 387:16763: the box of a page, the gap of the tiles, two rows of 84
    static constexpr QSize PREVIEW_SLOT = QSize(41, 56);
    static constexpr int PREVIEW_GAP = 4;
    static constexpr int PREVIEW_HEIGHT = 172;

    /// Most tiles of the previews (a range of a huge file shows its first pages; the count of the
    /// status and of the button stays exact)
    static constexpr int MAXIMUM_PREVIEW_PAGES = 1000;

    explicit PDFAppManageAddPagesDialog(const QString& sourceId, const QString& fileName, PDFInteger pageCount, bool isReplace, QWidget* scrimTarget, QWidget* parent);
    virtual ~PDFAppManageAddPagesDialog() override;

    /// The input of the list, whose pages the previews show (nullptr / empty = no previews)
    void setPreviewSource(PDFAppToolInputList* list, const QString& inputId);

    QRadioButton* getAllRadio() const { return m_allRadio.data(); }
    QRadioButton* getRangeRadio() const { return m_rangeRadio.data(); }
    PDFAppToolPageRangeField* getRangeField() const { return m_range.data(); }
    PDFLabel* getStatusLabel() const { return m_status.data(); }
    QWidget* getFacts() const { return m_facts.data(); }
    PDFAppToolPageGridView* getPreviewView() const { return m_previewView.data(); }
    PDFAppInsertSourceModel* getPreviewModel() const { return m_previewModel.data(); }

    /// The chosen pages (from 0, in the order of the range), empty for an invalid range
    std::vector<PDFInteger> getPages() const;

Q_SIGNALS:
    void pagesChosen(const QString& sourceId, const std::vector<PDFInteger>& pages, bool isReplace);

protected:
    virtual void acceptDialog() override;
    virtual void changeEvent(QEvent* event) override;

private:
    void updateState();
    void updatePreviews(const std::vector<PDFInteger>& pages);

    QString m_sourceId;
    QString m_previewInputId;
    PDFInteger m_pageCount = 0;
    bool m_isReplace = false;
    QPointer<QWidget> m_facts;
    QPointer<QRadioButton> m_allRadio;
    QPointer<QRadioButton> m_rangeRadio;
    QPointer<PDFAppToolPageRangeField> m_range;
    QPointer<PDFAppToolPageGridView> m_previewView;
    QPointer<PDFAppInsertSourceModel> m_previewModel;
    QPointer<PDFAppToolPageTileDelegate> m_previewDelegate;
    QPointer<PDFAppThumbnailScheduler> m_previewScheduler;
    QPointer<PDFAppToolInputList> m_previewList;
    QPointer<PDFLabel> m_status;
};

/// Properties (387:15926): the rows of the item (Name, Type, Source, Original page, Page size,
/// Orientation, Rotation "90°") in the facts on bg/subtle (387:16947), Close. Says nothing.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppManagePropertiesDialog : public PDFAppManageDialog
{
    Q_OBJECT

private:
    using BaseClass = PDFAppManageDialog;

public:
    explicit PDFAppManagePropertiesDialog(const std::vector<std::pair<QString, QString>>& rows, QWidget* scrimTarget, QWidget* parent);
    virtual ~PDFAppManagePropertiesDialog() override;

    PDFKeyValueList* getValues() const { return m_values.data(); }

protected:
    virtual void acceptDialog() override;

private:
    QPointer<PDFKeyValueList> m_values;
};

/// Regroup every N pages (387:15926): "Pages per group" and "17 selected pages → 3 groups (6, 6 and
/// 5 pages)"; also Move to… ("Before item", the item from 1 - the end is the count + 1)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppManageNumberDialog : public PDFAppManageDialog
{
    Q_OBJECT

private:
    using BaseClass = PDFAppManageDialog;

public:
    /// What the number means
    enum class Mode
    {
        EveryPages,     ///< Regroup every N pages: pages of the selection \p count
        MoveTo          ///< Move to…: items \p count, the place before the item (count + 1 = the end)
    };

    explicit PDFAppManageNumberDialog(Mode mode, int count, int value, QWidget* scrimTarget, QWidget* parent);
    virtual ~PDFAppManageNumberDialog() override;

    PDFSpinBox* getSpinBox() const { return m_spinBox.data(); }
    PDFLabel* getStatusLabel() const { return m_status.data(); }

Q_SIGNALS:
    void numberChosen(int value);

protected:
    virtual void acceptDialog() override;

private:
    void updateStatus();

    Mode m_mode;
    int m_count = 0;
    QPointer<PDFSpinBox> m_spinBox;
    QPointer<PDFLabel> m_status;
};

/// Regroup at page numbers (387:15926): "A new group starts at pages", the field "7, 13", "Positions in
/// the selection, separated by commas.", "17 selected pages → 3 groups (1–6, 7–12, 13–17)"; also the
/// name of Rename… and of Save checkpoint (the field and its limit, no status)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppManageTextDialog : public PDFAppManageDialog
{
    Q_OBJECT

private:
    using BaseClass = PDFAppManageDialog;

public:
    enum class Mode
    {
        PageNumbers,    ///< Regroup at page numbers of the \p pageCount pages of the selection
        Name            ///< A name (Rename…, Save checkpoint)
    };

    explicit PDFAppManageTextDialog(Mode mode, const QString& title, const QString& acceptText, const QString& text, int pageCount, QWidget* scrimTarget, QWidget* parent);
    virtual ~PDFAppManageTextDialog() override;

    PDFLineEdit* getField() const { return m_field.data(); }
    PDFLabel* getStatusLabel() const { return m_status.data(); }

Q_SIGNALS:
    void textChosen(const QString& text);

protected:
    virtual void acceptDialog() override;

private:
    void updateStatus();

    Mode m_mode;
    int m_pageCount = 0;
    QPointer<PDFLineEdit> m_field;
    QPointer<PDFLabel> m_status;
};

/// Crop… (Q17 of step 3, no frame; built from the panel Crop 398:28158 in a dialog of the width 520):
/// the margins of the visible area in millimeters (left, top, right, bottom - never mirrored, they
/// belong to the page), "Apply to all pages of the same source", Crop.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppManageCropDialog : public PDFAppManageDialog
{
    Q_OBJECT

private:
    using BaseClass = PDFAppManageDialog;

public:
    /// Largest margin in millimeters (a page is at most 14 400 points, PDFImagePageFactory)
    static constexpr double MAXIMUM_MARGIN = 2000.0;

    explicit PDFAppManageCropDialog(const QMarginsF& marginsInPoints, QWidget* scrimTarget, QWidget* parent);
    virtual ~PDFAppManageCropDialog() override;

    PDFDoubleSpinBox* getField(int index) const;
    QCheckBox* getSameSourceBox() const { return m_sameSource.data(); }

    /// The margins in points
    QMarginsF getMargins() const;

Q_SIGNALS:
    void cropChosen(const QMarginsF& marginsInPoints, bool isSameSource);

protected:
    virtual void acceptDialog() override;

private:
    QPointer<PDFDoubleSpinBox> m_fields[4];
    QPointer<QCheckBox> m_sameSource;
};

/// The sources of an assembly file, which need attention (387:16980): "2 source files need
/// attention · The assembly Tender_2026 was saved on 28 Sep 2026."; a row of each source, which
/// is not found or has changed - its letter, its name, the state ("Not found in C:\Tender" text/
/// danger, "Changed on 2 Oct 2026 – now 5 pages instead of 6"), what it means ("9 pages of the
/// assembly use it", "Page 6 is no longer available") and its button (Locate…, Use Changed File;
/// a file of another computer: Locate… and Open From There, RB8 - never touched without this
/// choice); "Pages of files that stay missing are left out and listed under Removed pages.";
/// Cancel, Open Without Them, Open Assembly (Enabled when every source can be used).
///
/// The dialog holds the result of the loader as a value (PDFAppAssemblyLoader); Use Changed File
/// accepts the changed file in it, Locate… and Open From There are asked from the GUI (the loader
/// checks the file, setResult shows its state). Its decisions are said last.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppManageSourcesDialog : public PDFMessageBox
{
    Q_OBJECT

private:
    using BaseClass = PDFMessageBox;

public:
    static constexpr int WIDTH = 520;

    explicit PDFAppManageSourcesDialog(PDFAppAssemblyLoadResult result, QWidget* scrimTarget, QWidget* parent);
    virtual ~PDFAppManageSourcesDialog() override;

    /// The result of a check of a located file (the rows are made again)
    void setResult(PDFAppAssemblyLoadResult result);
    const PDFAppAssemblyLoadResult& getResult() const { return m_result; }

    /// Use Changed File of the source
    void acceptChangedFile(const QString& sourceId);

    QPushButton* getOpenButton() const { return m_openButton.data(); }
    QPushButton* getWithoutButton() const { return m_withoutButton.data(); }
    QPushButton* getCancelButton() const { return m_cancelButton.data(); }
    QPushButton* getSourceButton(const QString& sourceId, const QString& action) const;

    /// The texts of a source of the dialog (tests): its state and what it means
    static QString getStateText(const PDFAppAssemblySourceStatus& status, const PDFDocumentAssembly& assembly);
    static QString getMeaningText(const PDFAppAssemblySourceStatus& status, const PDFDocumentAssembly& assembly);

Q_SIGNALS:
    void locateRequested(const QString& sourceId);
    void openFromThereRequested(const QString& sourceId);
    void openRequested(const pdf::PDFAppAssemblyLoadResult& result);

private:
    void updateRows();
    void onSourceButtonClicked();
    void onOpenClicked();
    void onCancelClicked();

    PDFAppAssemblyLoadResult m_result;
    QPointer<QWidget> m_rows;
    QPointer<QPushButton> m_openButton;
    QPointer<QPushButton> m_withoutButton;
    QPointer<QPushButton> m_cancelButton;
    std::vector<QPointer<QPushButton>> m_sourceButtons;
};

}   // namespace pdf

#endif // PDFAPPMANAGEDIALOGS_H
