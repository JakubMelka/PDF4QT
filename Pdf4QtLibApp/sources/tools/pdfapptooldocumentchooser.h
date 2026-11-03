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

namespace pdf
{

class PDFAppContext;
class PDFAppDocument;

/// A row of an open document (document-Project_Report.pdf 277:13689, 784 x 49, radius 8,
/// paddings 9 / 10, gap 12): the icon file-text 20, the name Semi Bold 13, the details Regular
/// 11 text/muted, an optional Tag, "Use this document" Medium 12 - text/accent under the
/// pointer, text/tertiary otherwise - and the chevron 16. The whole row chooses the document.
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

    /// \param documentId Id of the document of the row
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

    /// Rectangle of the text "Use this document" in the coordinates of the row
    QRect getActionRect() const;

protected:
    virtual void paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const override;
    virtual QSize getContentSizeHint() const override;
    virtual void resizeEvent(QResizeEvent* event) override;

private:
    void updateTagGeometry();
    int getActionWidth() const;
    int getTextWidth() const;

    QString m_documentId;
    QString m_name;
    QString m_folder;
    QString m_numbers;
    QPointer<PDFTag> m_tag;
};

/// Choose a document of a tool (document-list 277:2040, plan of the first batch 1.2; the
/// mode One, more documents are a later mode): the card with the group of the open documents of
/// this window (F13), Open a file… and the drop zone. A locked document of a tool, which
/// cannot use it, is a disabled row with the Tag Locked (Q4); without an open document
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
    QString createNumbers(const PDFAppDocument* document) const;

    QPointer<PDFAppContext> m_context;
    QString m_toolId;
    QPointer<QVBoxLayout> m_rowsLayout;
    QPointer<PDFLabel> m_groupLabel;
    QPointer<QPushButton> m_openButton;
    QPointer<PDFAppDropZone> m_dropZone;
    std::vector<QPointer<PDFAppToolDocumentRow>> m_rows;
};

}   // namespace pdf

#endif // PDFAPPTOOLDOCUMENTCHOOSER_H
