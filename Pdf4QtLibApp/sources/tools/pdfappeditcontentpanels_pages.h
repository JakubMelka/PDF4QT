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

#ifndef PDFAPPEDITCONTENTPANELS_PAGES_H
#define PDFAPPEDITCONTENTPANELS_PAGES_H

#include "pdfappglobal.h"
#include "pdfdeferreddeletion.h"
#include "pdfdocument.h"
#include "pdfglobal.h"

#include <QPointer>
#include <QWidget>

namespace pdf
{

class PDFAppDocumentView;
class PDFAppEditContentContext;
class PDFAppEditContentRegistry;
class PDFAppHeaderText;
class PDFAppToolPageGrid;
struct PDFContentChange;

// Edit Content, stage E1, package P12 (tabs Objects and Pages). Plan: zadani_gui.md 4.18 (Pages).

/// The panel of the tab Pages (zadani_gui.md 4.18; the frame edit-content-v2-pages): the grid of
/// the page thumbnails (PDFAppToolPageGrid with PDFPageTileDelegate), a blue dot at the pages
/// with changes, which are not applied yet (PDFContentEditSession::getDirtyPages), the current
/// page marked; a click asks for the page (PDFAppEditContentContext::requestPage - the tab
/// never switches the page itself). The thumbnails show the original document also after edits;
/// the text below them says so.
///
/// The tab has no state object: the pages with changes are read from the session, the current
/// page from the context.
///
/// Threads and ownership: a part (see PDFAppEditContentObjectsPanel). GUI thread; the thumbnails
/// are rendered by the scheduler of the application (its own workers). The hook retire() ends
/// what the panel has outside of its subtree.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentPagesPanel : public QWidget, public PDFRetirable
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAppEditContentPagesPanel(PDFAppEditContentContext* context, QWidget* parent);
    virtual ~PDFAppEditContentPagesPanel() override;

    /// The page of the tile the user has activated last (tests), -1 = none
    PDFInteger getActivatedPage() const { return m_activatedPage; }

    /// The entries of this file for the registry (the tab Pages)
    static void registerParts(PDFAppEditContentRegistry* registry);

    // ---- P12's own functions (tests)

    /// The document of the thumbnails (the panel takes it from the GUI of the tool; a test of a
    /// context without a GUI gives it here)
    void setDocument(PDFDocumentPointer document, const QString& documentId, PDFAppDocumentView* view);

    PDFAppToolPageGrid* getGrid() const;
    PDFAppHeaderText* getSubtitle() const;

protected:
    virtual void retire() override;
    virtual void changeEvent(QEvent* event) override;

private:
    void updateTiles();
    void updateTexts();

    void onPageClicked(int pageIndex, Qt::KeyboardModifiers modifiers);
    void onPageChanged();
    void onModelChanged(const pdf::PDFContentChange& change);

    QPointer<PDFAppEditContentContext> m_context;
    QPointer<PDFAppToolPageGrid> m_grid;
    QPointer<PDFAppHeaderText> m_title;
    QPointer<PDFAppHeaderText> m_subtitle;
    PDFInteger m_activatedPage = -1;
};

}   // namespace pdf

#endif // PDFAPPEDITCONTENTPANELS_PAGES_H
