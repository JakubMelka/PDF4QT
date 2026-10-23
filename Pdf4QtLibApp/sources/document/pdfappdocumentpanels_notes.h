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

#ifndef PDFAPPDOCUMENTPANELS_NOTES_H
#define PDFAPPDOCUMENTPANELS_NOTES_H

#include "pdfappglobal.h"
#include "pdfappdocumentpanel.h"
#include "pdfappdocumentpanels_common.h"

#include "pdfaccenthue.h"
#include "pdfannotation.h"
#include "pdfcomponentdelegates.h"
#include "pdfdocument.h"

#include <QAbstractListModel>
#include <QDateTime>
#include <QPointer>

#include <memory>
#include <vector>

#include <QListView>


namespace pdf
{

class PDFLineEdit;

/// Annotation of the Notes panel
class PDF4QTLIBAPPSHARED_EXPORT PDFAppNote
{
public:
    PDFInteger page = 0;                ///< Page index from 0
    PDFObjectReference reference;       ///< Annotation (selection on the page - step 4)
    AnnotationType type = AnnotationType::Invalid;
    bool isReply = false;               ///< A reply to another annotation (/IRT)
    QString author;                     ///< /T
    QString text;                       ///< /Contents (one paragraph)
    QDateTime date;                     ///< /M, else /CreationDate
};

/// Annotations of the Notes panel (step 3 read only, O16: the editing, the
/// synchronization of the selection with the page and the replies are step 4)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppNotes
{
public:
    PDFAppNotes() = delete;

    /// Most annotations listed
    static constexpr size_t MAXIMUM_NOTES = 10000;

    /// Longest text and author kept of an annotation
    static constexpr int MAXIMUM_TEXT_LENGTH = 2048;

    /// The type is a markup annotation listed by the panel (Text, Free Text,
    /// Caret, Stamp, the text markups, the shapes, Ink, File Attachment, Sound,
    /// Redact, Projection; not Link, Widget, Popup, ...)
    static bool isListedType(AnnotationType type);

    /// Icon and hue of the badge of the type (222:8664; a reply: corner-down-right Neutral)
    static void getBadge(AnnotationType type, bool isReply, QString* iconName, PDFAccentHue* hue);

    /// Annotations of the document in the order of the pages (at most \p maximumCount)
    static std::vector<PDFAppNote> collect(const PDFDocument* document, size_t maximumCount = MAXIMUM_NOTES);

    /// The document has an annotation, which is listed
    static bool hasNotes(const PDFDocument* document);

    /// Notes of the current version of \p document: collected once per version
    /// and shared by the panel and the tab of the sidebar
    /// (PDFAppDocument::getDerivedData); empty for a document, which is not shown
    static std::shared_ptr<const std::vector<PDFAppNote>> getNotes(const PDFAppDocument* document);

    /// Number of the calls of collect since the start (tests)
    static int getCollectCount();

    /// "Sep 28" (this year), "Sep 28, 2025" (another year); empty for an invalid date
    static QString formatDate(const QDateTime& dateTime, const QDate& today);
};

/// Rows of the Notes panel: a caption of a page ("Page 12", group-caption
/// 195:4315) before the notes of the page (Note row)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppNotesModel : public QAbstractListModel
{
    Q_OBJECT

private:
    using BaseClass = QAbstractListModel;

public:
    /// Role of a caption row (bool)
    static constexpr int CaptionRole = Qt::UserRole + 1;

    /// Role of the page of a row (index from 0)
    static constexpr int PageRole = Qt::UserRole + 2;

    explicit PDFAppNotesModel(QObject* parent);
    virtual ~PDFAppNotesModel() override;

    /// Sets the notes and the filter text (author or text containing it, without case)
    void setNotes(std::vector<PDFAppNote> notes, PDFDocumentPointer document);
    void setFilterText(const QString& text);

    /// Number of all notes (without the filter) and of their pages
    int getNoteCount() const { return int(m_notes.size()); }
    int getPageCount() const;

    /// Note of the row (nullptr for a caption)
    const PDFAppNote* getNote(int row) const;

    virtual int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    virtual QVariant data(const QModelIndex& index, int role) const override;
    virtual Qt::ItemFlags flags(const QModelIndex& index) const override;

private:
    struct Row
    {
        PDFInteger page = 0;
        int note = -1;      ///< Index of the note, -1 = caption of the page
    };

    void rebuildRows();

    std::vector<PDFAppNote> m_notes;
    std::vector<Row> m_rows;
    PDFDocumentPointer m_document;
    QString m_filterText;
    QDate m_today;
};

/// Delegate of the Notes panel: Note rows of PDFNoteDelegate and the captions of
/// the pages (Medium 11 text/muted, paddings 12 / 10 / 4)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppNotesDelegate : public PDFNoteDelegate
{
    Q_OBJECT

private:
    using BaseClass = PDFNoteDelegate;

public:
    explicit PDFAppNotesDelegate(QObject* parent);
    virtual ~PDFAppNotesDelegate() override;

protected:
    virtual void paintItem(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index, const PDFComponentPainter::State& state) const override;
    virtual QSize getItemSize(const QStyleOptionViewItem& option, const QModelIndex& index, int width) const override;
};

/// Notes panel (pdf4qt-document-notes 195:3910), read only with navigation in
/// step 3 (O16): "Notes / 5 notes on 3 pages", the field "Search notes…", the
/// notes grouped by their pages. A click (Enter) goes to the page of the note.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppNotesPanel : public PDFAppDocumentPanel
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDocumentPanel;

public:
    explicit PDFAppNotesPanel(QWidget* parent);
    virtual ~PDFAppNotesPanel() override;

    virtual QString getId() const override;
    virtual QString getTitle() const override;
    virtual QString getSubtitle() const override;
    virtual QString getIconName() const override;
    virtual void setDocument(PDFAppDocument* document, PDFAppDocumentView* view) override;

    /// Goes to the page of the row
    void activateRow(int row);

    QListView* getListView() const { return m_view; }
    PDFAppNotesModel* getModel() const { return m_model; }
    PDFLineEdit* getSearchField() const { return m_searchField; }

protected:
    virtual void changeEvent(QEvent* event) override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void onDocumentChanged(const PDFModifiedDocument& modified);
    void onDocumentDestroyed();
    void onSearchTextChanged(const QString& text);
    void onClicked(const QModelIndex& index);
    void rebuild();

    QPointer<PDFLineEdit> m_searchField;
    QPointer<PDFAppPanelListView> m_view;
    QPointer<PDFAppNotesModel> m_model;
    QPointer<PDFAppNotesDelegate> m_delegate;
    QPointer<PDFAppDocument> m_connectedDocument;
    PDFDocumentPointer m_document;
};

/// Factory of the notes panel (the tab only for a document with a listed annotation)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppNotesPanelFactory : public PDFAppDocumentPanelFactory
{
public:
    virtual QString getId() const override;
    virtual int getOrder() const override;
    virtual QString getTitle() const override;
    virtual QString getIconName() const override;
    virtual bool isAvailable(const PDFAppDocument* document) const override;
    virtual PDFAppDocumentPanel* createPanel(QWidget* parent) const override;
};

}   // namespace pdf

#endif // PDFAPPDOCUMENTPANELS_NOTES_H
