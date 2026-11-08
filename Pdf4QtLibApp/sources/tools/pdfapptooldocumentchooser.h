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

#ifndef PDFAPPTOOLDOCUMENTCHOOSER_H
#define PDFAPPTOOLDOCUMENTCHOOSER_H

#include "pdfappglobal.h"
#include "pdfapphomewidgets.h"

#include "pdfcomponentbuttons.h"
#include "pdfcomponentrows.h"
#include "pdffluentwidgets.h"

#include <QPointer>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#include <vector>

class QMimeData;

namespace pdf
{

class PDFAppContext;
class PDFAppDocument;
class PDFAppToolInputList;

/// A row of an open document (document-Project_Report.pdf 277:13689, 784 x 49, radius 8,
/// paddings 9 / 10, gap 12): the icon file-text 20, the name Semi Bold 13, the details Regular
/// 11 text/muted, an optional Tag, "Use this document" Medium 12 - text/accent under the
/// pointer, text/tertiary otherwise - and the chevron 16. The whole row chooses the document.
///
/// The mode Multiple (pdf4qt-tool-start-multi 531:3208) adds the check box 18 at the leading
/// edge (the row is checkable, a checked row has bg/hover), the action text of the order
/// ("1st in the result", Medium 12 text/accent), the button remove 16 of a file and the drag
/// handle 16 (more-vertical) of a checked row in the place of the chevron. Alt+Up / Alt+Down
/// and a drag of the handle ask to move the row (the chooser moves the input).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolDocumentRow : public PDFRowButton
{
    Q_OBJECT

private:
    using BaseClass = PDFRowButton;

public:
    static constexpr int HEIGHT = 49;
    static constexpr int RADIUS = 8;
    static constexpr int PADDING = 10;
    static constexpr int GAP = 12;
    static constexpr int ICON = 20;
    static constexpr int CHEVRON = 16;
    static constexpr int CHECK_BOX = 18;    ///< check 531:3541
    static constexpr int REMOVE = 16;       ///< remove 531:3566

    /// What is drawn at the trailing edge
    enum class Trailing
    {
        Chevron,    ///< The row chooses (the mode One)
        Handle,     ///< The drag handle of a checked row (more-vertical)
        None        ///< Nothing (an unchecked row of the mode Multiple)
    };

    /// \param documentId Id of the document of the row (the mode Multiple: the id of an
    ///        open document or of an input of a file)
    /// \param name Name of the file (data)
    /// \param folder Folder of the file, empty = none (data)
    /// \param numbers "1,592 pages · 24.8 MB" (data), empty = none
    /// \param tagText Text of the Tag, empty = none
    explicit PDFAppToolDocumentRow(const QString& documentId, const QString& name, const QString& folder, const QString& numbers, const QString& tagText, QWidget* parent);
    virtual ~PDFAppToolDocumentRow() override;

    const QString& getDocumentId() const { return m_documentId; }

    /// The whole line of the details 277:13694: "C:\Documents · 1,592 pages · 24.8 MB"
    QString getDetails() const;

    /// The line as it is drawn: a long folder gives way (elided in the middle), the
    /// numbers stay whole
    QString getShownDetails() const;

    PDFTag* getTag() const { return m_tag.data(); }

    /// Rectangle of the action text in the coordinates of the row (empty without a text)
    QRect getActionRect() const;

    // ------------------------------------------------------------ mode Multiple

    /// The check box at the leading edge (the row becomes checkable)
    void setCheckBoxVisible(bool isVisible);
    bool isCheckBoxVisible() const { return m_isCheckBoxVisible; }

    /// Rectangle of the check box (empty without it)
    QRect getCheckBoxRect() const;

    /// Text of the action instead of "Use this document" ("1st in the result", text/accent);
    /// an empty text hides the action
    void setActionText(const QString& text);
    QString getActionText() const;

    void setTrailing(Trailing trailing);
    Trailing getTrailing() const { return m_trailing; }

    /// Rectangle of the chevron or the handle (empty for None)
    QRect getTrailingRect() const;

    /// The button remove of an input of a file (x 16)
    void setRemovable(bool isRemovable);
    PDFIconButton* getRemoveButton() const { return m_removeButton.data(); }

Q_SIGNALS:
    /// Remove (x) has been clicked (a slot may delete the row)
    void removeClicked();

    /// Alt+Up (-1) / Alt+Down (+1): the row asks to be moved (a slot may delete the row)
    void moveRequested(int delta);

    /// The handle has been dragged to the global position and released there
    void handleDropped(const QPoint& globalPosition);

protected:
    virtual void paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const override;
    virtual QSize getContentSizeHint() const override;
    virtual void resizeEvent(QResizeEvent* event) override;
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void mouseMoveEvent(QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void updateChildGeometry();
    int getActionWidth() const;
    int getTextLeft() const;
    int getTextWidth() const;

    /// Logical right edge of the parts at the trailing side: the trailing icon, remove, the
    /// action (in this order from the edge); \p part 0 = after the trailing icon, 1 = after
    /// remove, 2 = after the action (the right edge of the Tag)
    int getTrailingEdge(int part) const;

    QString m_documentId;
    QString m_name;
    QString m_folder;
    QString m_numbers;
    QString m_actionText;
    bool m_hasActionText = false;
    bool m_isCheckBoxVisible = false;
    bool m_isHandleDragged = false;
    Trailing m_trailing = Trailing::Chevron;
    QPointer<PDFTag> m_tag;
    QPointer<PDFIconButton> m_removeButton;
};

/// Choose a document of a tool (document-list 277:2040, plan of the first batch 1.2; the
/// mode One, more documents are PDFAppToolInputChooser): the card with the group of the open
/// documents of this window (F13), Open a file… and the drop zone. A locked document of a tool,
/// which cannot use it, is a disabled row with the Tag Locked (Q4); without an open document
/// the group is hidden. The rows follow the registry of the documents. The chooser only
/// says what was chosen: both signals may delete it.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolDocumentChooser : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// \param context Context of the application
    /// \param toolId Id of the tool
    /// \param parent Parent widget
    explicit PDFAppToolDocumentChooser(PDFAppContext* context, const QString& toolId, QWidget* parent);
    virtual ~PDFAppToolDocumentChooser() override;

    /// Rows in their order
    std::vector<PDFAppToolDocumentRow*> getRows() const;
    PDFLabel* getGroupLabel() const { return m_groupLabel.data(); }
    QPushButton* getOpenButton() const { return m_openButton.data(); }
    PDFAppDropZone* getDropZone() const { return m_dropZone.data(); }

    /// Reads the open documents again
    void refresh();

    /// Details of an open document: "1,592 pages · 24.8 MB"
    static QString createNumbers(const PDFAppDocument* document);

    /// Folder of a file for the details (native separators), empty for no file
    static QString getFolder(const QString& filePath);

Q_SIGNALS:
    /// The document of the row has been chosen
    void documentChosen(const QString& documentId);

    /// Open a file… has been clicked
    void openFileRequested();

protected:
    virtual void showEvent(QShowEvent* event) override;

private:
    void onRowClicked();
    void onDocumentRemoved(const QString& id);
    void onDocumentChanged(pdf::PDFAppDocument* document);

    QPointer<PDFAppContext> m_context;
    QString m_toolId;
    QPointer<QVBoxLayout> m_rowsLayout;
    QPointer<PDFLabel> m_groupLabel;
    QPointer<QPushButton> m_openButton;
    QPointer<PDFAppDropZone> m_dropZone;
    std::vector<QPointer<PDFAppToolDocumentRow>> m_rows;
};

/// Choose the documents of a tool with more inputs (the mode Multiple: pdf4qt-tool-start-multi
/// 531:3208 and its one-document state 551:149074, plan of step 2 of phase 2, 4.3, K8, K22,
/// K28, K29, R11): the card document-list with the group OPEN DOCUMENTS (the ready documents of
/// this window, each with its check box - a checked document is a snapshot of now in the list,
/// R11), the group FILES FROM DISK (the files and images added here, each with the check box,
/// remove and the handle; "FILES FROM DISK – none added yet" without them), Add files… and the
/// drop zone, the summary and the Primary "Open Merge with 3 Documents" (Disabled below two
/// documents). The order of the result is the order of PDFAppToolInputList: the checked rows
/// of each group are sorted by it and say their place ("1st in the result"); the handle and
/// Alt+Up / Alt+Down move an input in the list.
///
/// Ownership: the list belongs to the caller (the start page), the chooser holds it by a
/// QPointer; an unchecked file stays parked in the list (checked again without a second
/// reading), remove forgets it. The rows are rebuilt from the list and the registry (a row
/// may be the sender: the old rows are deleted later). The signals may delete the chooser.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolInputChooser : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// Least number of inputs of the tool (Merge: two, 551:149074)
    static constexpr int MINIMUM_INPUTS = 2;

    /// \param context Context of the application
    /// \param toolId Id of the tool
    /// \param list The inputs (the chooser does not own them)
    /// \param parent Parent widget
    explicit PDFAppToolInputChooser(PDFAppContext* context, const QString& toolId, PDFAppToolInputList* list, QWidget* parent);
    virtual ~PDFAppToolInputChooser() override;

    /// The list of the inputs (a new list after the tool took the old one)
    PDFAppToolInputList* getInputList() const;
    void setInputList(PDFAppToolInputList* list);

    /// Rows of the open documents and of the files in their order
    std::vector<PDFAppToolDocumentRow*> getDocumentRows() const;
    std::vector<PDFAppToolDocumentRow*> getFileRows() const;

    PDFLabel* getDocumentGroupLabel() const { return m_documentGroupLabel.data(); }
    PDFLabel* getFileGroupLabel() const { return m_fileGroupLabel.data(); }
    QPushButton* getAddFilesButton() const { return m_addFilesButton.data(); }
    PDFAppDropZone* getDropZone() const { return m_dropZone.data(); }
    PDFLabel* getSummaryLabel() const { return m_summaryLabel.data(); }
    QPushButton* getOpenToolButton() const { return m_openToolButton.data(); }

    /// Number of the inputs, which the tool can take (checked, not damaged, unsupported or
    /// without pages - a protected file counts, the tool asks for its password, K28)
    int getDocumentCount() const;

    /// Adds files (Add files…, a drop): a file unchecked before is checked again (no second
    /// reading), a file in the list is refused (K29: the summary says it)
    void addFiles(const QStringList& files);

    /// Takes the files of a drop (PDF files and images, K8). \returns false when the data has
    /// no local file.
    bool dropFiles(const QMimeData* mimeData);

    /// Reads the open documents and the list again
    void refresh();

    /// Ordinal text of a place in the result ("1st in the result", "2nd in the result", ...)
    static QString getOrderText(int index);

Q_SIGNALS:
    /// Open Merge with … has been clicked (the caller hands the list over to the tool)
    void openToolRequested();

protected:
    virtual void showEvent(QShowEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

private:
    void onAddFilesClicked();
    void onOpenToolClicked();
    void onRowToggled();
    void onRowRemoveClicked();
    void onRowMoveRequested(int delta);
    void onRowHandleDropped(const QPoint& globalPosition);
    void onListChanged();
    void onInputChanged(const QString& id);
    void onDocumentRemoved(const QString& id);
    void onDocumentChanged(pdf::PDFAppDocument* document);
    void scheduleRefresh();
    void updateTexts();
    QString getInputIdOfRow(const PDFAppToolDocumentRow* row) const;
    PDFAppToolDocumentRow* createInputRow(const QString& rowId, const QString& name, const QString& filePath, const QString& numbers, const QString& tagText, int order, bool isChecked, bool isFile);
    std::vector<PDFAppDocument*> getOpenDocuments() const;

    QPointer<PDFAppContext> m_context;
    QString m_toolId;
    QPointer<PDFAppToolInputList> m_list;
    QStringList m_fileIds;                  ///< Inputs of the files added here (checked or parked), in the order of their addition
    QString m_message;                      ///< A refusal of the last addition (K29), shown by the summary until the next change
    QPointer<PDFLabel> m_documentGroupLabel;
    QPointer<QVBoxLayout> m_documentRowsLayout;
    QPointer<PDFLabel> m_fileGroupLabel;
    QPointer<QVBoxLayout> m_fileRowsLayout;
    QPointer<QPushButton> m_addFilesButton;
    QPointer<PDFAppDropZone> m_dropZone;
    QPointer<PDFLabel> m_dropText;
    QPointer<PDFLabel> m_summaryLabel;
    QPointer<QPushButton> m_openToolButton;
    std::vector<QPointer<PDFAppToolDocumentRow>> m_documentRows;
    std::vector<QPointer<PDFAppToolDocumentRow>> m_fileRows;
    QString m_focusedRowId;
    bool m_isRefreshQueued = false;
};

}   // namespace pdf

#endif // PDFAPPTOOLDOCUMENTCHOOSER_H
