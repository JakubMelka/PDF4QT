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

#include "pdfappeditcontentpanels_pages.h"

#include "pdfappdocument.h"
#include "pdfappdocumentpanels_common.h"
#include "pdfappdocumentview.h"
#include "pdfappeditcontentcontext.h"
#include "pdfappeditcontentregistry.h"
#include "pdfapplocale.h"
#include "pdfapptoolhost.h"
#include "pdfapptoolpagegrid.h"
#include "pdfappworkspace.h"
#include "pdfdocumenttoolgui.h"

#include "pdfcomponentpainter.h"
#include "pdfthemefonts.h"

#include "pdfcatalog.h"

#include <QCoreApplication>
#include <QEvent>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>

namespace pdf
{

/// The entry of the tab Pages (zadani_gui.md 4.18)
class PDFAppEditContentPagesTabEntry : public PDFAppEditContentTabEntry
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppEditContentPagesTabEntry)

public:
    virtual QString getId() const override { return QString::fromLatin1(TAB_PAGES); }
    virtual int getOrder() const override { return 200; }
    virtual QString getTitle() const override { return tr("Pages"); }
    virtual QString getIconName() const override { return QStringLiteral("pages"); }

    virtual QWidget* createPanel(PDFAppEditContentContext* context, QObject* state, QWidget* parent) const override
    {
        Q_UNUSED(state);
        return new PDFAppEditContentPagesPanel(context, parent);
    }
};

/// Geometry of edit-content-v2-pages 1647:23580: the slot of the page of the tiles in two columns of the left panel
class PDFAppEditContentPagesHelper
{
public:
    PDFAppEditContentPagesHelper() = delete;

    static constexpr int SLOT_WIDTH = 82;
    static constexpr int SLOT_HEIGHT = 112;
};

PDFAppEditContentPagesPanel::PDFAppEditContentPagesPanel(PDFAppEditContentContext* context, QWidget* parent) :
    BaseClass(parent),
    PDFRetirable(this),
    m_context(context)
{
    using Helper = PDFAppEditContentPagesHelper;

    setObjectName(QStringLiteral("editContentPages"));

    // The header (Pages, "Page 3 of 1,592 · 2 pages edited"), the tiles on the panel, the hint below them
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(PDFAppPanelWidgets::CONTENT_PADDING_SIDE, 8, PDFAppPanelWidgets::CONTENT_PADDING_SIDE, PDFAppPanelWidgets::CONTENT_PADDING_BOTTOM);
    layout->setSpacing(8);
    QVBoxLayout* titleLayout = new QVBoxLayout();
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(0);
    m_title = new PDFAppHeaderText(PDFThemeFonts::getFont(QFont::DemiBold, 14), PDFThemeColors::TextPrimary, this);
    m_title->setObjectName(QStringLiteral("editContentPagesTitle"));
    m_subtitle = new PDFAppHeaderText(PDFThemeFonts::getFont(QFont::Normal, 11), PDFThemeColors::TextMuted, this);
    m_subtitle->setObjectName(QStringLiteral("editContentPagesSubtitle"));
    titleLayout->addWidget(m_title);
    titleLayout->addWidget(m_subtitle);
    layout->addLayout(titleLayout);

    m_grid = new PDFAppToolPageGrid(QSize(Helper::SLOT_WIDTH, Helper::SLOT_HEIGHT), this);
    m_grid->setObjectName(QStringLiteral("editContentPagesGrid"));
    m_grid->setTileSize(PDFAppToolPageGrid::TileSize::Small);
    m_grid->setCanvasPainted(false);
    m_grid->setHintPlacement(false, 12);
    m_grid->getHeaderLabel()->setVisible(false);
    layout->addWidget(m_grid, 1);
    connect(m_grid.data(), &PDFAppToolPageGrid::pageClicked, this, &PDFAppEditContentPagesPanel::onPageClicked);

    // The thumbnails of the document of the tab (the original pages: cheap and stable, Preview shows the edits)
    const PDFDocumentToolGUI* gui = context ? context->getGUI() : nullptr;
    const PDFAppDocument* document = gui ? gui->getDocument() : nullptr;
    const PDFAppToolHost* host = gui ? gui->getHost() : nullptr;
    if (document)
    {
        setDocument(document->getDocument(), document->getId(), host ? host->getView() : nullptr);
    }

    if (context)
    {
        PDFDeferredDeletion::connect(context, &PDFAppEditContentContext::pageChanged, this, &PDFAppEditContentPagesPanel::onPageChanged);
        PDFDeferredDeletion::connect(context, &PDFAppEditContentContext::sessionReplaced, this, &PDFAppEditContentPagesPanel::onSessionReplaced);
        connectSession();
    }
    updateTexts();
}

PDFAppEditContentPagesPanel::~PDFAppEditContentPagesPanel() = default;

void PDFAppEditContentPagesPanel::connectSession()
{
    if (m_session)
    {
        m_session->disconnect(this);
    }
    m_session = m_context ? m_context->getSession() : nullptr;
    if (m_session)
    {
        PDFDeferredDeletion::connect(m_session.data(), &PDFContentEditSession::modelChanged, this, &PDFAppEditContentPagesPanel::onModelChanged);
    }
}

void PDFAppEditContentPagesPanel::onSessionReplaced()
{
    // A new version of the document without work (PDFAppEditContentContext::reloadDocument): its pages with changes
    // come from the new session, the thumbnails show the new version; pageChanged follows
    connectSession();
    if (m_context && !m_documentId.isEmpty())
    {
        setDocument(m_context->getDocumentPointer(), m_documentId, m_view.data());
    }
    else
    {
        updateTiles();
        updateTexts();
    }
}

void PDFAppEditContentPagesPanel::registerParts(PDFAppEditContentRegistry* registry)
{
    registry->addTab(std::make_unique<PDFAppEditContentPagesTabEntry>());
}

PDFAppToolPageGrid* PDFAppEditContentPagesPanel::getGrid() const
{
    return m_grid.data();
}

PDFAppHeaderText* PDFAppEditContentPagesPanel::getSubtitle() const
{
    return m_subtitle.data();
}

void PDFAppEditContentPagesPanel::setDocument(PDFDocumentPointer document, const QString& documentId, PDFAppDocumentView* view)
{
    m_documentId = documentId;
    m_view = view;
    if (m_grid)
    {
        m_grid->setDocument(std::move(document), documentId, view);
        updateTiles();
        if (m_context && m_context->getPageIndex() >= 0)
        {
            m_grid->scrollToPage(int(m_context->getPageIndex()));
        }
    }
}

void PDFAppEditContentPagesPanel::retire()
{
    // The scheduler of the thumbnails is a child of the grid: it ends with the subtree
}

void PDFAppEditContentPagesPanel::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
    {
        updateTexts();
    }
}

void PDFAppEditContentPagesPanel::updateTiles()
{
    PDFAppToolPageGridModel* model = m_grid ? m_grid->getModel() : nullptr;
    const PDFContentEditSession* session = m_context ? m_context->getSession() : nullptr;
    if (!model || !session)
    {
        return;
    }

    // The current page is the selected tile, a page with changes not applied yet has the blue dot
    // (edit-content-v2-pages: pages 3 and 7); the tiles are one dataChanged of the changed range
    const PDFInteger pageCount = m_context->getPageCount();
    std::vector<PDFAppToolPageGridModel::Tile> tiles(size_t(std::max<PDFInteger>(pageCount, 0)));
    const PDFInteger currentPage = m_context->getPageIndex();
    if (currentPage >= 0 && currentPage < pageCount)
    {
        tiles[size_t(currentPage)].state = PDFComponentPainter::PageTileState::Selected;
    }
    for (PDFInteger page : session->getDirtyPages())
    {
        if (page >= 0 && page < pageCount)
        {
            tiles[size_t(page)].dotColor = PDFAccentHue::Blue;
        }
    }
    m_grid->setTiles(std::move(tiles));
}

void PDFAppEditContentPagesPanel::updateTexts()
{
    if (m_title)
    {
        m_title->setHeaderText(tr("Pages"));
    }

    QStringList parts;
    const PDFContentEditSession* session = m_context ? m_context->getSession() : nullptr;
    if (session && m_context->getPageIndex() >= 0)
    {
        parts << tr("Page %1 of %2").arg(PDFAppLocale::formatNumber(m_context->getPageIndex() + 1), PDFAppLocale::formatNumber(m_context->getPageCount()));
        const size_t edited = session->getDirtyPages().size();
        if (edited > 0)
        {
            parts << tr("%Ln pages edited", nullptr, int(edited));
        }
    }
    if (m_subtitle)
    {
        m_subtitle->setHeaderText(parts.join(PDFComponentPainter::getDetailSeparator()));
    }
    if (m_grid)
    {
        m_grid->setHintText(tr("The thumbnails show the pages as they were opened; Preview shows the edits."));
    }
}

void PDFAppEditContentPagesPanel::onPageClicked(int pageIndex, Qt::KeyboardModifiers modifiers)
{
    Q_UNUSED(modifiers);
    m_activatedPage = pageIndex;

    // Only a request: the GUI of the tool applies the rules of a switch (zadani_gui.md 4.27) and shows the page;
    // the request is the last call (a slot may close the tool)
    if (m_context)
    {
        m_context->requestPage(pageIndex);
    }
}

void PDFAppEditContentPagesPanel::onPageChanged()
{
    updateTiles();
    updateTexts();
    if (m_grid && m_context && m_context->getPageIndex() >= 0)
    {
        m_grid->scrollToPage(int(m_context->getPageIndex()));
    }
}

void PDFAppEditContentPagesPanel::onModelChanged(const PDFContentChange& change)
{
    Q_UNUSED(change);

    // A command, an undo or a redo may add or remove a page with changes
    updateTiles();
    updateTexts();
}

}   // namespace pdf

#include "moc_pdfappeditcontentpanels_pages.cpp"
