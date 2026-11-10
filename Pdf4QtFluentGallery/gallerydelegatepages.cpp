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

// Samples of the delegates of the views (step 5 of components.md, chapter 6.3)
// on the pages of their chapters: the matrix of the variants of the design
// drawn by PDFComponentPainter and live views with the delegates.

#include "gallerypages.h"

#include "pdfaccenthue.h"
#include "pdfcomponentdelegates.h"
#include "pdfcomponentpainter.h"
#include "pdfcomponentrows.h"
#include "pdffluentwidgets.h"
#include "pdfstylepainter.h"
#include "pdfthemecolors.h"
#include "pdfthemefonts.h"
#include "pdfwidgetstyleproperties.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QListView>
#include <QPainter>
#include <QPixmap>
#include <QStandardItemModel>
#include <QVBoxLayout>

#include <functional>

namespace pdffluentgallery
{

/// Tree view of a sample of the presets Outline and Certificate in a state,
/// which a real view shows only under the pointer or with the keyboard focus:
/// the pressed row and the focus ring of the current row
class GalleryRowStateTreeView : public pdf::PDFTreeView
{
public:
    explicit GalleryRowStateTreeView(QWidget* parent, bool isPressed, bool hasFocusRing) :
        pdf::PDFTreeView(parent),
        m_isPressed(isPressed),
        m_hasFocusRing(hasFocusRing)
    {

    }

protected:
    virtual bool isRowFocusRingVisible() const override { return m_hasFocusRing; }

    virtual void drawRow(QPainter* painter, const QStyleOptionViewItem& options, const QModelIndex& index) const override
    {
        // The current row is drawn pressed (as the view does for the pressed row)
        QStyleOptionViewItem rowOptions = options;
        const QModelIndex current = currentIndex();
        if (m_isPressed && current.isValid() && index.row() == current.row() && index.parent() == current.parent())
        {
            rowOptions.state |= QStyle::State_Sunken;
        }
        pdf::PDFTreeView::drawRow(painter, rowOptions, index);
    }

private:
    bool m_isPressed;
    bool m_hasFocusRing;
};

/// Helper functions of the samples of the delegates
class GalleryDelegatePagesHelper
{
public:
    GalleryDelegatePagesHelper() = delete;

    using Color = pdf::PDFThemeColors::Color;
    using Painter = pdf::PDFComponentPainter;
    using Roles = pdf::PDFComponentRoles;
    using PaintFunction = std::function<void(QPainter&, const QRectF&, const Painter::State&)>;

    /// A variant of a set of the design: its name, its flags and its node
    struct Variant
    {
        const char* name;
        int flags;
        const char* node;
    };

    /// Variants Selected x State of the rows of the document panels
    static QList<Variant> selectionVariants(const char* const nodes[8])
    {
        return { { "Default", GalleryPages::Default, nodes[0] },
                 { "Hover", GalleryPages::Hover, nodes[1] },
                 { "Pressed", GalleryPages::Pressed | GalleryPages::Hover, nodes[2] },
                 { "Focus", GalleryPages::Focus, nodes[3] },
                 { "Selected", GalleryPages::Selected, nodes[4] },
                 { "Selected Hover", GalleryPages::Selected | GalleryPages::Hover, nodes[5] },
                 { "Selected Pressed", GalleryPages::Selected | GalleryPages::Pressed | GalleryPages::Hover, nodes[6] },
                 { "Selected Focus", GalleryPages::Selected | GalleryPages::Focus, nodes[7] } };
    }

    /// Variants State of a row without the axis Selected
    static QList<Variant> stateVariants(const char* const nodes[4])
    {
        return { { "Default", GalleryPages::Default, nodes[0] },
                 { "Hover", GalleryPages::Hover, nodes[1] },
                 { "Pressed", GalleryPages::Pressed | GalleryPages::Hover, nodes[2] },
                 { "Focus", GalleryPages::Focus, nodes[3] } };
    }

    /// Returns the state of the painter for the flags of a sample
    static Painter::State state(int flags, Qt::LayoutDirection direction)
    {
        return Painter::State(GalleryPages::toStyleState(GalleryPages::StateFlags(flags)), direction);
    }

    /// Adds a sample drawn over bg/surface (the panels and cards of the screens)
    static void specimen(GalleryBuilder& builder, const QString& caption, QSize size, int flags, const PaintFunction& paint,
                         const QString& node = QString(), Qt::LayoutDirection direction = QApplication::layoutDirection())
    {
        builder.specimen(caption, size, [paint, flags, direction](QPainter& painter, const QRect& rect)
        {
            const qreal margin = GallerySpecimen::MARGIN;
            pdf::PDFStylePainter::fillRect(&painter, QRectF(rect).adjusted(-margin, -margin, margin, margin), Color::BgSurface);
            paint(painter, QRectF(rect), state(flags, direction));
        }, node);
    }

    /// Adds the samples of the variants, \p perRow samples in a row
    static void variants(GalleryBuilder& builder, const QString& prefix, QSize size, const QList<Variant>& list, const PaintFunction& paint, int perRow)
    {
        int count = 0;
        for (const Variant& variant : list)
        {
            const QString caption = prefix.isEmpty() ? QString::fromLatin1(variant.name) : QStringLiteral("%1 %2").arg(prefix, QString::fromLatin1(variant.name));
            specimen(builder, caption, size, variant.flags, paint, variant.node ? QString::fromLatin1(variant.node) : QString());
            if (++count % perRow == 0)
            {
                builder.row();
            }
        }
        if (count % perRow != 0)
        {
            builder.row();
        }
    }

    /// Picture of a page, which stands for a page rendered by the application
    static QPixmap createPagePicture(int variant)
    {
        QPixmap pixmap(QSize(216, 296));
        pixmap.fill(pdf::PDFStylePainter::color(Color::SampleWhite));

        QPainter painter(&pixmap);
        pdf::PDFStylePainter::fillRect(&painter, QRectF(22, 24, 90, 8), Color::SampleGrayDark);
        int top = 46;
        for (int block = 0; block < 3; ++block)
        {
            if (variant == 1 && block == 1)
            {
                pdf::PDFStylePainter::fillRect(&painter, QRectF(22, top, 172, 60), Color::SampleBlueSoft);
                pdf::PDFStylePainter::fillRect(&painter, QRectF(36, top + 12, 70, 36), Color::SampleBlue);
                top += 74;
                continue;
            }

            for (int line = 0; line < 5; ++line)
            {
                pdf::PDFStylePainter::fillRect(&painter, QRectF(22, top + line * 11, line == 4 ? 90 : 172, 4), Color::SampleGrayMid);
            }
            top += 70;
        }
        return pixmap;
    }

    /// Creates a list view with the delegate and the model, of a fixed size (a grid
    /// reserves the vertical scroll bar before it lays its cells out); the
    /// spacing of the rows is the gap of the lists of the screens (each delegate
    /// documents it: 2 in the document sidebar, 0 in the Recent list, the
    /// shortcuts and the fonts), the thumbnails are a grid (review F5 of step 5)
    static QListView* createListView(QWidget* parent, QAbstractItemDelegate* delegate, QStandardItemModel* model, QSize size, int spacing, QSize grid = QSize())
    {
        pdf::PDFListView* view = new pdf::PDFListView(parent);
        view->setModel(model);
        view->setItemDelegate(delegate);
        delegate->setParent(view);
        model->setParent(view);
        if (grid.isValid())
        {
            // Thumbnails: two columns, gaps 8 (columns) and 6 (rows), screen 193:756
            view->setViewMode(QListView::IconMode);
            view->setMovement(QListView::Static);
            view->setWrapping(true);
            view->setSpacing(0);
            view->setGridSize(grid);
        }
        else
        {
            view->setSpacing(spacing);
        }
        view->setResizeMode(QListView::Adjust);
        view->setUniformItemSizes(false);
        view->setSelectionMode(QAbstractItemView::SingleSelection);
        view->setFixedSize(size);
        return view;
    }

    /// Creates the sample of a preset of the rows of PDFTreeView in a state (Outline, Contents or Certificate)
    static pdf::PDFTreeView* createRowStateView(QWidget* parent, pdf::PDFTreeView::RowStyle rowStyle, bool isSelected, bool isPressed, bool hasFocusRing)
    {
        QStandardItemModel* model = new QStandardItemModel(parent);
        const bool isContents = rowStyle == pdf::PDFTreeView::RowStyle::Contents;
        const bool isOutline = rowStyle == pdf::PDFTreeView::RowStyle::Outline || isContents;
        if (isOutline)
        {
            QStandardItem* chapter = new QStandardItem(isContents ? QStringLiteral("Protect & Sign") : QStringLiteral("Market Analysis"));
            chapter->appendRow(new QStandardItem(isContents ? QStringLiteral("Sign with Certificate") : QStringLiteral("4.1 Regional revenue")));
            model->appendRow(chapter);
        }
        else
        {
            QList<QStandardItem*> row;
            for (const char* text : { "System", "DigiCert Global Root G2", "DigiCert Inc", "2013-08-01", "2038-01-15" })
            {
                row << new QStandardItem(QString::fromLatin1(text));
            }
            row[1]->setFont(pdf::PDFThemeFonts::getFont(pdf::PDFThemeFonts::Role::SmallSemiBold));
            model->appendRow(row);
        }

        GalleryRowStateTreeView* view = new GalleryRowStateTreeView(parent, isPressed, hasFocusRing);
        view->setModel(model);
        view->setHeaderHidden(true);
        view->setRootIsDecorated(isOutline);
        view->setSelectionBehavior(QAbstractItemView::SelectRows);
        view->setRowStyle(rowStyle);
        const QModelIndex first = model->index(0, 0);
        view->selectionModel()->setCurrentIndex(first, isSelected ? (QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows) : QItemSelectionModel::NoUpdate);
        if (isOutline)
        {
            view->setFixedSize(276 + 14, (isContents ? 28 : 30) + 14);
        }
        else
        {
            const int widths[] = { 84, 200, 240, 120, 136 };
            for (int column = 0; column < 5; ++column)
            {
                view->header()->resizeSection(column, widths[column]);
            }
            view->setFixedSize(780 + 14, 34 + 14);
        }
        return view;
    }

    /// Selects the row of the view
    static void selectRow(QAbstractItemView* view, int row)
    {
        if (view->model() && view->selectionModel())
        {
            view->selectionModel()->select(view->model()->index(row, 0), QItemSelectionModel::ClearAndSelect);
        }
    }

    // ---------------------------------------------------------------- contents

    static Painter::ThumbnailItem thumbnail(const QString& label = QStringLiteral("308"))
    {
        Painter::ThumbnailItem item;
        item.label = label;
        item.pixmap = createPagePicture(0);
        return item;
    }

    static Painter::BookmarkRow bookmark()
    {
        return Painter::BookmarkRow{ QStringLiteral("Page 17"), QStringLiteral("Executive Summary"), QStringLiteral("Auto") };
    }

    static Painter::NoteRow note()
    {
        Painter::NoteRow row;
        row.author = QStringLiteral("Jakub Melka");
        row.date = QStringLiteral("Sep 28");
        row.text = QStringLiteral("Check these numbers against the Q3 report before publishing.");
        return row;
    }

    static Painter::AttachmentRow attachment()
    {
        Painter::AttachmentRow row;
        row.name = QStringLiteral("budget-2026.xlsx");
        row.meta = QStringLiteral("Spreadsheet · 84 kB");
        return row;
    }

    static Painter::SearchResultRow searchResult()
    {
        Painter::SearchResultRow row;
        row.page = QStringLiteral("Page 308");
        row.snippet = QStringLiteral("…regional revenue grew by 18 % compared with the previous year…");
        row.matches = { Painter::TextRange{ 10, 7 } };
        return row;
    }

    static Painter::FontRow font(bool embedded)
    {
        Painter::FontRow row;
        row.name = QStringLiteral("Inter-SemiBold");
        row.family = QStringLiteral("Font family: Inter");
        row.type = QStringLiteral("TrueType");
        row.embedded = embedded;
        return row;
    }

    static Painter::RecentItemRow recentItem(Painter::RecentItemType type)
    {
        Painter::RecentItemRow row;
        row.name = QStringLiteral("Project_Report.pdf");
        row.details = QStringLiteral("C:\\Users\\Jakub\\Documents · 24 pages · 2.4 MB");
        row.progressText = QStringLiteral("Page 12 of 24");
        row.progress = type == Painter::RecentItemType::File ? 0.5 : -1.0;
        row.opened = QStringLiteral("Today 10:24");
        row.type = type;
        return row;
    }

    static Painter::ShortcutRow shortcut(const QString& keys = QStringLiteral("Ctrl+O"), bool recording = false)
    {
        Painter::ShortcutRow row;
        row.action = QStringLiteral("Open…");
        row.shortcut = keys;
        row.recording = recording;
        return row;
    }

    // ------------------------------------------------------------------ models

    static QStandardItem* item(const QString& text)
    {
        QStandardItem* item = new QStandardItem(text);
        item->setEditable(false);
        return item;
    }

    static QStandardItemModel* thumbnailModel()
    {
        QStandardItemModel* model = new QStandardItemModel();
        const char* labels[] = { "307", "308", "309" };
        for (int i = 0; i < 3; ++i)
        {
            QStandardItem* page = item(QString::fromLatin1(labels[i]));
            page->setData(createPagePicture(i % 2), Roles::PixmapRole);
            model->appendRow(page);
        }
        return model;
    }

    static QStandardItemModel* bookmarkModel()
    {
        QStandardItemModel* model = new QStandardItemModel();
        struct Bookmark { const char* title; const char* subtitle; bool isAuto; };
        static constexpr Bookmark bookmarks[] = { { "Page 1", "Title page", false }, { "Page 17", "Executive Summary", true }, { "Page 42", "Regional results", true } };
        for (const Bookmark& bookmark : bookmarks)
        {
            QStandardItem* row = item(QString::fromLatin1(bookmark.title));
            row->setData(QString::fromLatin1(bookmark.subtitle), Roles::SubtitleRole);
            if (bookmark.isAuto)
            {
                row->setData(QStringLiteral("Auto"), Roles::TagRole);
            }
            model->appendRow(row);
        }
        return model;
    }

    static QStandardItemModel* noteModel()
    {
        QStandardItemModel* model = new QStandardItemModel();
        QStandardItem* first = item(QStringLiteral("Jakub Melka"));
        first->setData(QStringLiteral("Sep 28"), Roles::DateRole);
        first->setData(QStringLiteral("Check these numbers against the Q3 report before publishing."), Roles::TextRole);
        model->appendRow(first);
        QStandardItem* second = item(QStringLiteral("Jane Novak"));
        second->setData(QStringLiteral("Sep 29"), Roles::DateRole);
        second->setData(QStringLiteral("Agreed."), Roles::TextRole);
        second->setData(QStringLiteral("message-square"), Roles::IconNameRole);
        second->setData(static_cast<int>(pdf::PDFAccentHue::Blue), Roles::HueRole);
        model->appendRow(second);

        // A reply of the note (1025:21920) and a hidden annotation (1026:21995)
        QStandardItem* reply = item(QStringLiteral("Peter Dvorak"));
        reply->setData(QStringLiteral("Today"), Roles::DateRole);
        reply->setData(QStringLiteral("Done."), Roles::TextRole);
        reply->setData(QStringLiteral("corner-down-right"), Roles::IconNameRole);
        reply->setData(static_cast<int>(pdf::PDFAccentHue::Neutral), Roles::HueRole);
        reply->setData(24, Roles::IndentRole);
        model->appendRow(reply);
        QStandardItem* hidden = item(QStringLiteral("Jane Novak"));
        hidden->setData(QStringLiteral("Sep 30"), Roles::DateRole);
        hidden->setData(QStringLiteral("Hidden on the page"), Roles::TextRole);
        hidden->setData(QStringLiteral("eye-off"), Roles::StateIconRole);
        model->appendRow(hidden);
        return model;
    }

    static QStandardItemModel* attachmentModel()
    {
        QStandardItemModel* model = new QStandardItemModel();
        QStandardItem* first = item(QStringLiteral("budget-2026.xlsx"));
        first->setData(QStringLiteral("Spreadsheet · 84 kB"), Roles::SubtitleRole);
        model->appendRow(first);
        QStandardItem* second = item(QStringLiteral("Quarterly_financial_statement_consolidated_final.pdf"));
        second->setData(QStringLiteral("PDF document · 1.2 MB"), Roles::SubtitleRole);
        second->setData(QStringLiteral("file"), Roles::IconNameRole);
        model->appendRow(second);
        return model;
    }

    static QStandardItemModel* searchModel()
    {
        QStandardItemModel* model = new QStandardItemModel();
        QStandardItem* first = item(QStringLiteral("Page 308"));
        first->setData(QStringLiteral("…regional revenue grew by 18 % compared with the previous year…"), Roles::TextRole);
        first->setData(QPoint(10, 7), Roles::MatchRangesRole);
        model->appendRow(first);
        QStandardItem* second = item(QStringLiteral("Page 312"));
        second->setData(QStringLiteral("…the revenue of the segment…"), Roles::TextRole);
        second->setData(QVariantList{ QPoint(5, 7) }, Roles::MatchRangesRole);
        model->appendRow(second);
        return model;
    }

    static QStandardItemModel* fontModel()
    {
        QStandardItemModel* model = new QStandardItemModel();
        struct Font { const char* name; const char* family; const char* type; bool embedded; };
        static constexpr Font fonts[] = { { "Inter-SemiBold", "Font family: Inter", "TrueType", true }, { "Helvetica", "Font family: Helvetica", "Type 1", false } };
        for (const Font& font : fonts)
        {
            QStandardItem* row = item(QString::fromLatin1(font.name));
            row->setData(QString::fromLatin1(font.family), Roles::SubtitleRole);
            row->setData(QString::fromLatin1(font.type), Roles::TagRole);
            row->setData(font.embedded, Roles::EmbeddedRole);
            model->appendRow(row);
        }
        return model;
    }

    static QStandardItemModel* recentModel()
    {
        QStandardItemModel* model = new QStandardItemModel();
        QStandardItem* file = item(QStringLiteral("Project_Report.pdf"));
        file->setData(QStringLiteral("C:\\Users\\Jakub\\Documents · 24 pages · 2.4 MB"), Roles::SubtitleRole);
        file->setData(QStringLiteral("Page 12 of 24"), Roles::ProgressTextRole);
        file->setData(0.5, Roles::ProgressRole);
        file->setData(QStringLiteral("Today 10:24"), Roles::DateRole);
        file->setData(createPagePicture(1), Roles::PixmapRole);
        file->setData(true, Roles::PinnedRole);
        model->appendRow(file);
        QStandardItem* folder = item(QStringLiteral("Contracts"));
        folder->setData(QStringLiteral("D:\\Work\\Contracts · 18 files"), Roles::SubtitleRole);
        folder->setData(QStringLiteral("18 files"), Roles::ProgressTextRole);
        folder->setData(QStringLiteral("Yesterday 16:15"), Roles::DateRole);
        folder->setData(static_cast<int>(pdf::PDFComponentPainter::RecentItemType::Folder), Roles::ItemTypeRole);
        model->appendRow(folder);
        QStandardItem* workspace = item(QStringLiteral("Thesis review"));
        workspace->setData(QStringLiteral("4 documents"), Roles::SubtitleRole);
        workspace->setData(QStringLiteral("4 documents"), Roles::ProgressTextRole);
        workspace->setData(QStringLiteral("Sep 12"), Roles::DateRole);
        workspace->setData(static_cast<int>(pdf::PDFComponentPainter::RecentItemType::Workspace), Roles::ItemTypeRole);
        workspace->setData(false, Roles::StarVisibleRole);
        model->appendRow(workspace);
        QStandardItem* missing = item(QStringLiteral("Building_Plan.pdf"));
        missing->setData(QStringLiteral("D:\\Projects\\Old · 12 pages"), Roles::SubtitleRole);
        missing->setData(QStringLiteral("Sep 3"), Roles::DateRole);
        missing->setData(true, Roles::MissingRole);
        model->appendRow(missing);
        QStandardItem* locked = item(QStringLiteral("Salary_Review_2026.pdf"));
        locked->setData(QStringLiteral("C:\\Users\\Jakub\\HR · 6 pages"), Roles::SubtitleRole);
        locked->setData(QStringLiteral("Page 2 of 6"), Roles::ProgressTextRole);
        locked->setData(2.0 / 6.0, Roles::ProgressRole);
        locked->setData(QStringLiteral("Aug 28"), Roles::DateRole);
        locked->setData(true, Roles::LockedRole);
        model->appendRow(locked);
        return model;
    }

    static QStandardItemModel* shortcutModel()
    {
        QStandardItemModel* model = new QStandardItemModel();
        struct Shortcut { const char* action; const char* icon; const char* keys; };
        static constexpr Shortcut shortcuts[] = { { "Open…", "folder-open", "Ctrl+O" }, { "Save", "save", "Ctrl+S" }, { "Print…", "printer", "Ctrl+P" }, { "Compare documents", "columns", "" } };
        for (const Shortcut& shortcut : shortcuts)
        {
            QStandardItem* row = new QStandardItem(QString::fromUtf8(shortcut.action));
            row->setData(QString::fromLatin1(shortcut.icon), Roles::IconNameRole);
            if (*shortcut.keys)
            {
                row->setData(QVariant::fromValue(QKeySequence::fromString(QString::fromLatin1(shortcut.keys), QKeySequence::PortableText)), Roles::ShortcutRole);
            }
            model->appendRow(row);
        }
        return model;
    }

    /// Adds the live views of a delegate: plain with a selection, disabled and right-to-left
    template<typename Delegate>
    static void liveViews(GalleryBuilder& builder, const std::function<QStandardItemModel*()>& createModel, QSize size, int selectedRow, int spacing,
                          bool isWide = false, QSize grid = QSize())
    {
        QWidget* page = builder.page();
        QListView* view = createListView(page, new Delegate(), createModel(), size, spacing, grid);
        if (selectedRow >= 0)
        {
            selectRow(view, selectedRow);
        }
        builder.live(QStringLiteral("Live view (QListView + delegate)"), view);
        if (isWide)
        {
            builder.row();
        }

        QListView* disabled = createListView(page, new Delegate(), createModel(), size, spacing, grid);
        disabled->setEnabled(false);
        if (selectedRow >= 0)
        {
            selectRow(disabled, selectedRow);
        }
        builder.live(QStringLiteral("Live disabled (O-08)"), disabled);
        if (isWide)
        {
            builder.row();
        }

        QListView* rightToLeft = createListView(page, new Delegate(), createModel(), size, spacing, grid);
        rightToLeft->setLayoutDirection(Qt::RightToLeft);
        if (selectedRow >= 0)
        {
            selectRow(rightToLeft, selectedRow);
        }
        builder.live(QStringLiteral("Live right-to-left"), rightToLeft);
        builder.row();
    }
};

void GalleryPages::addShellHomeDelegates(GalleryBuilder& builder)
{
    using Helper = GalleryDelegatePagesHelper;
    using Painter = pdf::PDFComponentPainter;
    using Type = Painter::RecentItemType;

    builder.block(QStringLiteral("Recent item row (PDFRecentItemDelegate) - Type File / Folder / Workspace x State, star and menu"), QStringLiteral("79:1026"));
    struct TypeVariants
    {
        Type type;
        const char* prefix;
        const char* nodes[4];
    };
    static constexpr TypeVariants types[] =
    {
        { Type::File, "File", { "79:802", "79:822", "79:842", "79:862" } },
        { Type::Folder, "Folder", { "79:882", "79:900", "79:918", "79:936" } },
        { Type::Workspace, "Workspace", { "79:954", "79:972", "79:990", "79:1008" } }
    };
    for (const TypeVariants& variants : types)
    {
        const Painter::RecentItemRow row = Helper::recentItem(variants.type);
        Helper::variants(builder, QString::fromLatin1(variants.prefix), Painter::getRecentItemRowSize(row), Helper::stateVariants(variants.nodes),
                         [row](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintRecentItemRow(&painter, rect, row, state); }, 2);
    }

    Painter::RecentItemRow pinned = Helper::recentItem(Type::File);
    pinned.pinned = true;
    pinned.pixmap = Helper::createPagePicture(1);
    pinned.starFlags = QStyle::State_MouseOver;
    Helper::specimen(builder, QStringLiteral("File pinned, the picture of the page, the star under the pointer"), Painter::getRecentItemRowSize(pinned), GalleryPages::Hover,
                     [pinned](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintRecentItemRow(&painter, rect, pinned, state); });
    const Painter::RecentItemRow file = Helper::recentItem(Type::File);
    Helper::specimen(builder, QStringLiteral("Disabled (O-08)"), Painter::getRecentItemRowSize(file), GalleryPages::Disabled,
                     [file](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintRecentItemRow(&painter, rect, file, state); });
    builder.row();

    // States without a frame of Figma (proposals of the integration of step 2):
    // a missing file, a document protected by a password, an item without the star
    Painter::RecentItemRow missing = Helper::recentItem(Type::File);
    missing.status = Painter::FileStatus::Missing;
    Helper::specimen(builder, QStringLiteral("Missing file (proposal): the name text/muted, the badge instead of the progress"), Painter::getRecentItemRowSize(missing), GalleryPages::Default,
                     [missing](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintRecentItemRow(&painter, rect, missing, state); });
    Helper::specimen(builder, QStringLiteral("Missing file, hover"), Painter::getRecentItemRowSize(missing), GalleryPages::Hover,
                     [missing](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintRecentItemRow(&painter, rect, missing, state); });
    builder.row();
    Painter::RecentItemRow locked = Helper::recentItem(Type::File);
    locked.locked = true;
    Helper::specimen(builder, QStringLiteral("Protected by a password (proposal): the lock instead of the thumbnail"), Painter::getRecentItemRowSize(locked), GalleryPages::Default,
                     [locked](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintRecentItemRow(&painter, rect, locked, state); });
    Painter::RecentItemRow noStar = Helper::recentItem(Type::Workspace);
    noStar.starVisible = false;
    Helper::specimen(builder, QStringLiteral("Workspace without the star (it cannot be pinned), hover"), Painter::getRecentItemRowSize(noStar), GalleryPages::Hover,
                     [noStar](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintRecentItemRow(&painter, rect, noStar, state); });
    builder.row();
    Painter::RecentItemRow longName = Helper::recentItem(Type::File);
    longName.name = QStringLiteral("Annual_Report_2026_with_all_the_appendices_and_the_corrections_of_the_board.pdf");
    Helper::specimen(builder, QStringLiteral("Right-to-left, Latin data keep their direction (the name elided in the middle)"), Painter::getRecentItemRowSize(longName), GalleryPages::Default,
                     [longName](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintRecentItemRow(&painter, rect, longName, state); },
                     QString(), Qt::RightToLeft);
    builder.row();

    Helper::liveViews<pdf::PDFRecentItemDelegate>(builder, &Helper::recentModel, QSize(830, 5 * 56 + 16), -1, 0, true);
    builder.note(QStringLiteral("The star writes PinnedRole into the model, the menu button emits actionTriggered with MenuAction (the application shows its menu). Both are also in the context menu of the row."));
}

void GalleryPages::addFormsDelegates(GalleryBuilder& builder)
{
    using Helper = GalleryDelegatePagesHelper;
    using Painter = pdf::PDFComponentPainter;

    builder.block(QStringLiteral("Shortcut row (PDFShortcutDelegate) - State Default / Hover / Pressed / Focus / Recording, Has shortcut"), QStringLiteral("145:2904"));
    static constexpr const char* nodes[] = { "145:2880", "145:2886", "145:2892", "145:2898" };
    const Painter::ShortcutRow row = Helper::shortcut();
    const QSize size = Painter::getShortcutRowSize(row);
    Helper::variants(builder, QString(), size, Helper::stateVariants(nodes),
                     [row](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintShortcutRow(&painter, rect, row, state); }, 1);

    const Painter::ShortcutRow recording = Helper::shortcut(QStringLiteral("Ctrl+O"), true);
    Helper::specimen(builder, QStringLiteral("Recording"), size, GalleryPages::Default,
                     [recording](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintShortcutRow(&painter, rect, recording, state); }, QStringLiteral("706:149339"));
    builder.row();
    const Painter::ShortcutRow empty = Helper::shortcut(QString());
    Helper::specimen(builder, QStringLiteral("Has shortcut = false (Add shortcut)"), size, GalleryPages::Default,
                     [empty](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintShortcutRow(&painter, rect, empty, state); });
    builder.row();
    const Painter::ShortcutRow firstRecording = Helper::shortcut(QString(), true);
    Helper::specimen(builder, QStringLiteral("Recording of the first shortcut"), size, GalleryPages::Default,
                     [firstRecording](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintShortcutRow(&painter, rect, firstRecording, state); });
    builder.row();
    Helper::specimen(builder, QStringLiteral("Disabled (O-08)"), size, GalleryPages::Disabled,
                     [row](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintShortcutRow(&painter, rect, row, state); });
    builder.row();
    Helper::specimen(builder, QStringLiteral("Right-to-left"), size, GalleryPages::Default,
                     [row](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintShortcutRow(&painter, rect, row, state); }, QString(), Qt::RightToLeft);
    builder.row();

    Helper::liveViews<pdf::PDFShortcutDelegate>(builder, &Helper::shortcutModel, QSize(800, 4 * 40 + 16), -1, 0, true);
    builder.note(QStringLiteral("A click on the key cap (or on Add shortcut), Enter, Space or F2 records a new shortcut, Esc cancels it. A shortcut used by another action is reported by the signal shortcutConflict."));
}

void GalleryPages::addDocumentDelegates(GalleryBuilder& builder)
{
    using Helper = GalleryDelegatePagesHelper;
    using Painter = pdf::PDFComponentPainter;

    // Thumbnail item
    builder.block(QStringLiteral("Thumbnail item (PDFThumbnailDelegate) - Selected x State, the page from the model"), QStringLiteral("191:777"));
    static constexpr const char* thumbnailNodes[] = { "191:593", "191:616", "191:639", "191:662", "191:685", "191:708", "191:731", "191:754" };
    const Painter::ThumbnailItem thumbnail = Helper::thumbnail();
    const QSize thumbnailSize = Painter::getThumbnailItemSize(thumbnail);
    Helper::variants(builder, QString(), thumbnailSize, Helper::selectionVariants(thumbnailNodes),
                     [thumbnail](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintThumbnailItem(&painter, rect, thumbnail, state); }, 4);
    Painter::ThumbnailItem placeholder = Helper::thumbnail(QStringLiteral("iv"));
    placeholder.pixmap = QPixmap();
    Helper::specimen(builder, QStringLiteral("Without the picture"), thumbnailSize, GalleryPages::Default,
                     [placeholder](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintThumbnailItem(&painter, rect, placeholder, state); });
    Painter::ThumbnailItem landscape = Helper::thumbnail(QStringLiteral("12"));
    landscape.pixmap = landscape.pixmap.transformed(QTransform().rotate(90));
    Helper::specimen(builder, QStringLiteral("Landscape page"), thumbnailSize, GalleryPages::Selected,
                     [landscape](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintThumbnailItem(&painter, rect, landscape, state); });
    Helper::specimen(builder, QStringLiteral("Disabled (O-08)"), thumbnailSize, GalleryPages::Disabled,
                     [thumbnail](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintThumbnailItem(&painter, rect, thumbnail, state); });
    Helper::specimen(builder, QStringLiteral("Selected disabled"), thumbnailSize, GalleryPages::Disabled | GalleryPages::Selected,
                     [thumbnail](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintThumbnailItem(&painter, rect, thumbnail, state); });
    builder.row();
    Helper::liveViews<pdf::PDFThumbnailDelegate>(builder, &Helper::thumbnailModel, QSize(2 * 132 + 14 + 12 + 2, 2 * 189 + 16), 1, 0, false, QSize(124 + 8, 183 + 6));
    builder.note(QStringLiteral("The view of the thumbnails is a grid of two columns (QListView::IconMode, grid of the item and the gaps 8 / 6, screen 193:756)."));

    // Bookmark row
    builder.block(QStringLiteral("Bookmark row (PDFBookmarkDelegate) - Selected x State, the filled bookmark (I-13)"), QStringLiteral("191:911"));
    static constexpr const char* bookmarkNodes[] = { "191:847", "191:855", "191:863", "191:871", "191:879", "191:887", "191:895", "191:903" };
    const Painter::BookmarkRow bookmark = Helper::bookmark();
    const QSize bookmarkSize = Painter::getBookmarkRowSize(bookmark);
    Helper::variants(builder, QString(), bookmarkSize, Helper::selectionVariants(bookmarkNodes),
                     [bookmark](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintBookmarkRow(&painter, rect, bookmark, state); }, 4);
    Painter::BookmarkRow manual = bookmark;
    manual.tag.clear();
    Helper::specimen(builder, QStringLiteral("Auto generated = false"), bookmarkSize, GalleryPages::Default,
                     [manual](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintBookmarkRow(&painter, rect, manual, state); });
    Helper::specimen(builder, QStringLiteral("Disabled (O-08)"), bookmarkSize, GalleryPages::Disabled,
                     [bookmark](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintBookmarkRow(&painter, rect, bookmark, state); });
    Helper::specimen(builder, QStringLiteral("Right-to-left"), bookmarkSize, GalleryPages::Selected,
                     [bookmark](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintBookmarkRow(&painter, rect, bookmark, state); }, QString(), Qt::RightToLeft);
    builder.row();
    Helper::liveViews<pdf::PDFBookmarkDelegate>(builder, &Helper::bookmarkModel, QSize(296, 3 * 46 + 5 * 2 + 16), 1, 1);

    // Note row
    builder.block(QStringLiteral("Note row (PDFNoteDelegate) - Selected x State, the author text/accent when selected"), QStringLiteral("191:998"));
    static constexpr const char* noteNodes[] = { "191:918", "191:928", "191:938", "191:948", "191:958", "191:968", "191:978", "191:988" };
    const Painter::NoteRow note = Helper::note();
    const QSize noteSize = Painter::getNoteRowSize(note);
    Helper::variants(builder, QString(), noteSize, Helper::selectionVariants(noteNodes),
                     [note](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintNoteRow(&painter, rect, note, state); }, 4);
    Painter::NoteRow shortNote = note;
    shortNote.text = QStringLiteral("Agreed.");
    Helper::specimen(builder, QStringLiteral("Text of one line (48 high)"), Painter::getNoteRowSize(shortNote), GalleryPages::Default,
                     [shortNote](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintNoteRow(&painter, rect, shortNote, state); });
    Painter::NoteRow longNote = note;
    longNote.text = QStringLiteral("Check these numbers against the Q3 report before publishing, the totals of the regions differ from the summary on the page 4.");
    Helper::specimen(builder, QStringLiteral("Long text (elided on the second line)"), Painter::getNoteRowSize(longNote), GalleryPages::Default,
                     [longNote](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintNoteRow(&painter, rect, longNote, state); });
    Helper::specimen(builder, QStringLiteral("Disabled (O-08)"), noteSize, GalleryPages::Disabled,
                     [note](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintNoteRow(&painter, rect, note, state); });
    Painter::NoteRow hiddenNote = note;
    hiddenNote.author = QStringLiteral("Jane Novak");
    hiddenNote.text = QStringLiteral("Hidden on the page");
    hiddenNote.stateIconName = QStringLiteral("eye-off");
    Helper::specimen(builder, QStringLiteral("Hidden on the page - eye-off (1026:21995)"), Painter::getNoteRowSize(hiddenNote), GalleryPages::Hover,
                     [hiddenNote](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintNoteRow(&painter, rect, hiddenNote, state); });
    Painter::NoteRow replyNote = note;
    replyNote.author = QStringLiteral("Jane Novak");
    replyNote.text = QStringLiteral("Agreed - keep only the first sentence.");
    replyNote.iconName = QStringLiteral("corner-down-right");
    replyNote.hue = pdf::PDFAccentHue::Neutral;
    Helper::specimen(builder, QStringLiteral("Reply - note-row-reply (1025:21921), indented by 24 in a list"), Painter::getNoteRowSize(replyNote), GalleryPages::Default,
                     [replyNote](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintNoteRow(&painter, rect, replyNote, state); });
    builder.row();
    Helper::liveViews<pdf::PDFNoteDelegate>(builder, &Helper::noteModel, QSize(296, 63 + 3 * 48 + 4 * 2 + 16), 0, 1);

    // Attachment row
    builder.block(QStringLiteral("Attachment row (PDFAttachmentDelegate) - Selected x State, the save action is an Icon button 24"), QStringLiteral("191:1045"));
    static constexpr const char* attachmentNodes[] = { "191:1005", "191:1015", "191:1025", "191:1035", "706:149934", "706:149944", "706:149954", "706:149964" };
    const Painter::AttachmentRow attachment = Helper::attachment();
    const QSize attachmentSize = Painter::getAttachmentRowSize(attachment);
    Helper::variants(builder, QString(), attachmentSize, Helper::selectionVariants(attachmentNodes),
                     [attachment](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintAttachmentRow(&painter, rect, attachment, state); }, 4);
    Painter::AttachmentRow saveHover = attachment;
    saveHover.saveFlags = QStyle::State_MouseOver;
    Helper::specimen(builder, QStringLiteral("Hover of the save button"), attachmentSize, GalleryPages::Hover,
                     [saveHover](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintAttachmentRow(&painter, rect, saveHover, state); });
    Painter::AttachmentRow savePressed = attachment;
    savePressed.saveFlags = QStyle::State_MouseOver | QStyle::State_Sunken;
    Helper::specimen(builder, QStringLiteral("Press of the save button"), attachmentSize, GalleryPages::Hover,
                     [savePressed](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintAttachmentRow(&painter, rect, savePressed, state); });
    Helper::specimen(builder, QStringLiteral("Disabled (O-08)"), attachmentSize, GalleryPages::Disabled,
                     [attachment](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintAttachmentRow(&painter, rect, attachment, state); });
    Painter::AttachmentRow longAttachment = attachment;
    longAttachment.name = QStringLiteral("Quarterly_financial_statement_consolidated_final.xlsx");
    Helper::specimen(builder, QStringLiteral("Right-to-left, the name elided in the middle"), attachmentSize, GalleryPages::Default,
                     [longAttachment](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintAttachmentRow(&painter, rect, longAttachment, state); }, QString(), Qt::RightToLeft);
    builder.row();
    Helper::liveViews<pdf::PDFAttachmentDelegate>(builder, &Helper::attachmentModel, QSize(296, 2 * 48 + 4 * 2 + 16), 0, 1);

    // Search result row
    builder.block(QStringLiteral("Search result row (PDFSearchResultDelegate) - Selected x State, the found term Semi Bold text/primary"), QStringLiteral("191:1073"));
    static constexpr const char* searchNodes[] = { "191:1049", "191:1052", "191:1055", "191:1058", "191:1061", "191:1064", "191:1067", "191:1070" };
    const Painter::SearchResultRow result = Helper::searchResult();
    const QSize resultSize = Painter::getSearchResultRowSize(result);
    Helper::variants(builder, QString(), resultSize, Helper::selectionVariants(searchNodes),
                     [result](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintSearchResultRow(&painter, rect, result, state); }, 4);
    Painter::SearchResultRow longResult = result;
    longResult.snippet = QStringLiteral("…regional revenue grew by 18 % compared with the previous year, the revenue of the services by 24 %, the revenue of the licenses stayed…");
    longResult.matches = { Painter::TextRange{ 10, 7 }, Painter::TextRange{ 66, 7 }, Painter::TextRange{ 103, 7 } };
    Helper::specimen(builder, QStringLiteral("Long snippet (elided on the second line)"), Painter::getSearchResultRowSize(longResult), GalleryPages::Default,
                     [longResult](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintSearchResultRow(&painter, rect, longResult, state); });
    Helper::specimen(builder, QStringLiteral("Disabled (O-08)"), resultSize, GalleryPages::Disabled,
                     [result](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintSearchResultRow(&painter, rect, result, state); });
    Helper::specimen(builder, QStringLiteral("Right-to-left"), resultSize, GalleryPages::Selected,
                     [result](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintSearchResultRow(&painter, rect, result, state); }, QString(), Qt::RightToLeft);
    builder.row();
    Helper::liveViews<pdf::PDFSearchResultDelegate>(builder, &Helper::searchModel, QSize(296, 62 + 47 + 4 * 2 + 16), 0, 1);

    // Outline row and Certificate row: the press tracked by PDFTreeView (review
    // I-23) and the focus ring around the whole row (review F2, F4 of step 5)
    builder.block(QStringLiteral("Outline row, Contents row and Certificate row (PDFTreeView presets) - Pressed and Focus, the ring encloses the whole row"), QStringLiteral("191:840"));
    struct RowStateSample
    {
        pdf::PDFTreeView::RowStyle rowStyle;
        const char* name;
        bool isSelected;
        bool isPressed;
        bool hasFocusRing;
        const char* node;
    };
    static constexpr RowStateSample rowStates[] =
    {
        { pdf::PDFTreeView::RowStyle::Outline, "Outline Pressed", false, true, false, "191:798" },
        { pdf::PDFTreeView::RowStyle::Outline, "Outline Focus", false, false, true, "191:805" },
        { pdf::PDFTreeView::RowStyle::Outline, "Outline Selected Pressed", true, true, false, "191:826" },
        { pdf::PDFTreeView::RowStyle::Outline, "Outline Selected Focus", true, false, true, "191:833" },
        { pdf::PDFTreeView::RowStyle::Contents, "Contents Pressed", false, true, false, "955:154338" },
        { pdf::PDFTreeView::RowStyle::Contents, "Contents Focus", false, false, true, "955:154338" },
        { pdf::PDFTreeView::RowStyle::Contents, "Contents Selected Pressed", true, true, false, "955:154366" },
        { pdf::PDFTreeView::RowStyle::Contents, "Contents Selected Focus", true, false, true, "955:154366" },
        { pdf::PDFTreeView::RowStyle::Certificate, "Certificate Pressed", false, true, false, "706:147107" },
        { pdf::PDFTreeView::RowStyle::Certificate, "Certificate Focus", false, false, true, "706:147113" },
        { pdf::PDFTreeView::RowStyle::Certificate, "Certificate Selected Pressed", true, true, false, "706:147119" },
        { pdf::PDFTreeView::RowStyle::Certificate, "Certificate Selected Focus", true, false, true, "706:147125" }
    };
    int rowStateCount = 0;
    for (const RowStateSample& sample : rowStates)
    {
        builder.live(QString::fromLatin1(sample.name), Helper::createRowStateView(builder.page(), sample.rowStyle, sample.isSelected, sample.isPressed, sample.hasFocusRing),
                     QString::fromLatin1(sample.node));
        if (++rowStateCount % 2 == 0)
        {
            builder.row();
        }
    }
    // Contents of the manual (help-contents of the page Help 955:152873): rows 28,
    // chevron 14 at 8, indentation 24, the chapter of the shown topic expanded
    QStandardItemModel* contentsModel = new QStandardItemModel(builder.page());
    for (const char* chapterName : { "Getting started", "Open and view", "Protect & Sign", "Optimize" })
    {
        QStandardItem* chapter = new QStandardItem(QString::fromLatin1(chapterName));
        for (const char* topicName : { "Protect with Password", "Sign with Certificate", "Sign by Hand" })
        {
            chapter->appendRow(new QStandardItem(QString::fromLatin1(topicName)));
        }
        contentsModel->appendRow(chapter);
    }
    contentsModel->item(2)->setFont(pdf::PDFThemeFonts::getFont(QFont::DemiBold, 13));
    pdf::PDFTreeView* contentsView = new pdf::PDFTreeView(builder.page());
    contentsView->setModel(contentsModel);
    contentsModel->setParent(contentsView);
    contentsView->setHeaderHidden(true);
    contentsView->setRowStyle(pdf::PDFTreeView::RowStyle::Contents);
    contentsView->expand(contentsModel->index(2, 0));
    contentsView->selectionModel()->setCurrentIndex(contentsModel->index(1, 0, contentsModel->index(2, 0)), QItemSelectionModel::ClearAndSelect);
    contentsView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    contentsView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    contentsView->setFixedSize(322 + 12, 7 * 28 + 12 + 2 * contentsView->frameWidth());
    builder.live(QStringLiteral("Contents of the manual (RowStyle::Contents)"), contentsView, QStringLiteral("955:154306"));
    builder.row();

    builder.note(QStringLiteral("PDFTreeView with the presets Outline, Contents and Certificate sets State_Sunken to the pressed row (bg/pressed, the selected row bg/accent-subtle-pressed) and draws the focus ring of 2 px around the whole current row. The selection stays bg/accent-subtle without the focus of the view. The delegates of the view are not replaced."));

    // A delegate of the components in a tree: the area of the branches is not
    // filled by the style, the nested item has one shape (review C7 of step 5)
    builder.block(QStringLiteral("Nested items of a delegate in a tree (PDFTreeView + PDFBookmarkDelegate)"), QStringLiteral("191:911"));
    QStandardItemModel* nestedModel = Helper::bookmarkModel();
    QStandardItem* nested = Helper::item(QStringLiteral("Page 18"));
    nested->setData(QStringLiteral("Nested bookmark"), pdf::PDFComponentRoles::SubtitleRole);
    nestedModel->item(1)->appendRow(nested);
    pdf::PDFTreeView* nestedView = new pdf::PDFTreeView(builder.page());
    nestedView->setModel(nestedModel);
    nestedModel->setParent(nestedView);
    pdf::PDFBookmarkDelegate* nestedDelegate = new pdf::PDFBookmarkDelegate(nestedView);
    nestedView->setItemDelegate(nestedDelegate);
    nestedView->setHeaderHidden(true);
    nestedView->expandAll();
    nestedView->selectionModel()->setCurrentIndex(nestedModel->index(0, 0, nestedModel->index(1, 0)), QItemSelectionModel::ClearAndSelect);
    nestedView->setFixedSize(320, 4 * 46 + 16);
    builder.live(QStringLiteral("Selected nested bookmark"), nestedView);
    builder.note(QStringLiteral("The delegates draw the rows of flat lists; in a tree the style leaves the area of the branches of their items unfilled."));

    // Configurations of the delegates, which an application may use: one
    // delegate for two views, delegates for a column and for a row (the
    // lifetime test destroys their views, models and delegates)
    builder.block(QStringLiteral("Configurations of the delegates - one delegate for two views, a delegate for a column and for a row"));

    QWidget* sharedHost = new QWidget(builder.page());
    QHBoxLayout* sharedLayout = new QHBoxLayout(sharedHost);
    sharedLayout->setContentsMargins(0, 0, 0, 0);
    pdf::PDFNoteDelegate* sharedDelegate = new pdf::PDFNoteDelegate(sharedHost);
    for (int i = 0; i < 2; ++i)
    {
        pdf::PDFListView* sharedView = new pdf::PDFListView(sharedHost);
        QStandardItemModel* sharedModel = Helper::noteModel();
        sharedView->setModel(sharedModel);
        sharedModel->setParent(sharedView);
        sharedView->setItemDelegate(sharedDelegate);
        sharedView->setSpacing(1);
        sharedView->setFixedSize(296, 63 + 48 + 4 * 2 + 16);
        sharedLayout->addWidget(sharedView);
    }
    builder.live(QStringLiteral("One PDFNoteDelegate for two views (a child of their parent)"), sharedHost);
    builder.row();

    pdf::PDFTreeView* columnView = new pdf::PDFTreeView(builder.page());
    QStandardItemModel* columnModel = Helper::searchModel();
    columnModel->setColumnCount(2);
    for (int row = 0; row < columnModel->rowCount(); ++row)
    {
        columnModel->setItem(row, 1, Helper::item(QStringLiteral("Page %1").arg(row + 1)));
    }
    columnView->setModel(columnModel);
    columnModel->setParent(columnView);
    columnView->setRootIsDecorated(false);
    columnView->setItemDelegateForColumn(0, new pdf::PDFSearchResultDelegate(columnView));
    columnView->setItemDelegateForRow(1, new pdf::PDFBookmarkDelegate(columnView));
    columnView->setColumnWidth(0, 300);
    columnView->setFixedSize(480, 62 + 47 + 2 * 46 + 16);
    builder.live(QStringLiteral("Delegate for the column 0 (search results) and for the row 1 (bookmark)"), columnView);
}

void GalleryPages::addPanelsDelegates(GalleryBuilder& builder)
{
    using Helper = GalleryDelegatePagesHelper;
    using Painter = pdf::PDFComponentPainter;

    builder.block(QStringLiteral("Font row (PDFFontDelegate) - Embedded True / False x State, the copy icon on the hovered row"), QStringLiteral("206:944"));
    static constexpr const char* embeddedNodes[] = { "206:880", "206:895", "706:147131", "706:147147" };
    static constexpr const char* notEmbeddedNodes[] = { "206:912", "206:927", "706:147161", "706:147177" };
    const Painter::FontRow embedded = Helper::font(true);
    const Painter::FontRow notEmbedded = Helper::font(false);
    const QSize size = Painter::getFontRowSize(embedded);
    Helper::variants(builder, QStringLiteral("Embedded"), size, Helper::stateVariants(embeddedNodes),
                     [embedded](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintFontRow(&painter, rect, embedded, state); }, 1);
    Helper::variants(builder, QStringLiteral("Not embedded"), size, Helper::stateVariants(notEmbeddedNodes),
                     [notEmbedded](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintFontRow(&painter, rect, notEmbedded, state); }, 1);
    Helper::specimen(builder, QStringLiteral("Disabled (O-08)"), size, GalleryPages::Disabled,
                     [embedded](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintFontRow(&painter, rect, embedded, state); });
    builder.row();
    Helper::specimen(builder, QStringLiteral("Right-to-left"), size, GalleryPages::Hover,
                     [embedded](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintFontRow(&painter, rect, embedded, state); }, QString(), Qt::RightToLeft);
    builder.row();
    Helper::liveViews<pdf::PDFFontDelegate>(builder, &Helper::fontModel, QSize(712, 2 * 48 + 16), -1, 0, true);
    builder.note(QStringLiteral("The copy icon copies the name of the font into the clipboard; Ctrl+C and the context menu of the row do the same."));
}

GalleryPages::Page GalleryPages::createToolsPage(QWidget* parent)
{
    using Helper = GalleryDelegatePagesHelper;
    using Painter = pdf::PDFComponentPainter;
    using Kind = Painter::TileRowKind;
    using Tone = Painter::TileRowTone;
    using Color = pdf::PDFThemeColors::Color;
    using Roles = pdf::PDFComponentRoles;

    GalleryBuilder builder(parent, QStringLiteral("25-tools"), QStringLiteral("Rows and page tiles of the tools"), QStringLiteral("tools"));

    const auto makeRow = [](Kind kind, const QString& title, const QString& description, const QString& icon, pdf::PDFComponentPainter::TileRowTone tone = pdf::PDFComponentPainter::TileRowTone::Accent)
    {
        Painter::TileRow row;
        row.kind = kind;
        row.title = title;
        row.description = description;
        row.iconName = icon;
        row.tone = tone;
        return row;
    };
    const auto addRow = [&builder](const QString& caption, const Painter::TileRow& row, int width, int flags, Qt::LayoutDirection direction = Qt::LeftToRight, const QString& node = QString())
    {
        Helper::specimen(builder, caption, QSize(width, Painter::getTileRowHeight(row, width)), flags,
                         [row](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintTileRow(&painter, rect, row, state); }, node, direction);
    };

    // Items of the panels of the tool bar
    builder.block(QStringLiteral("Tile row - item of a panel (PDFTileRow, MenuItem) 44 / 57"), QStringLiteral("438:15032"));
    Painter::TileRow chosen = makeRow(Kind::MenuItem, QStringLiteral("All links"), QStringLiteral("16 external links"), QStringLiteral("list"));
    chosen.chosen = true;
    addRow(QStringLiteral("Chosen"), chosen, 302, GalleryPages::Default, Qt::LeftToRight, QStringLiteral("438:15319"));
    const Painter::TileRow notChosen = makeRow(Kind::MenuItem, QStringLiteral("Web addresses"), QStringLiteral("14 links to http and https pages"), QStringLiteral("globe"));
    addRow(QStringLiteral("Not chosen"), notChosen, 302, GalleryPages::Default, Qt::LeftToRight, QStringLiteral("438:15328"));
    builder.row();
    addRow(QStringLiteral("Hover"), notChosen, 302, GalleryPages::Hover);
    addRow(QStringLiteral("Pressed"), notChosen, 302, GalleryPages::Hover | GalleryPages::Pressed);
    builder.row();
    addRow(QStringLiteral("Focus"), notChosen, 302, GalleryPages::Focus);
    addRow(QStringLiteral("Disabled (O-08)"), chosen, 302, GalleryPages::Disabled);
    builder.row();
    Painter::TileRow wrapped = makeRow(Kind::MenuItem, QStringLiteral("Auto"), QStringLiteral("Structure when the document is tagged, otherwise layout"), QStringLiteral("zap"));
    wrapped.chosen = true;
    addRow(QStringLiteral("Chosen, two lines (57)"), wrapped, 252, GalleryPages::Default, Qt::LeftToRight, QStringLiteral("339:9462"));
    addRow(QStringLiteral("Not chosen, two lines (57)"), makeRow(Kind::MenuItem, QStringLiteral("Layout"), QStringLiteral("Blocks of text as they are placed on the page"), QStringLiteral("layout")), 252, GalleryPages::Default, Qt::LeftToRight, QStringLiteral("339:9471"));
    addRow(QStringLiteral("Right-to-left"), wrapped, 252, GalleryPages::Default, Qt::RightToLeft);
    builder.row();

    pdf::PDFTileRow* liveChosen = new pdf::PDFTileRow(QStringLiteral("Standard"), QStringLiteral("Everything except the invisible text"), QStringLiteral("hidden-data"), Kind::MenuItem, builder.page());
    liveChosen->setSelectionBehavior(pdf::PDFComponentButton::SelectionBehavior::Toggle);
    liveChosen->setChecked(true);
    liveChosen->setFixedWidth(332);
    builder.live(QStringLiteral("Live, a choice of more"), liveChosen, QStringLiteral("438:13080"));
    pdf::PDFTileRow* liveFree = new pdf::PDFTileRow(QStringLiteral("Clear all"), QStringLiteral("Untick everything and choose by hand"), QStringLiteral("select-none"), Kind::MenuItem, builder.page());
    liveFree->setFixedWidth(332);
    builder.live(QStringLiteral("Live, an action"), liveFree, QStringLiteral("438:13112"));
    builder.row();

    // Steps
    builder.block(QStringLiteral("Tile row - items of the panel Steps (PanelStep) 46"), QStringLiteral("303:4560"));
    Painter::TileRow panelStep = makeRow(Kind::PanelStep, QStringLiteral("Embed (dereference) simple objects, such as int, bool, real"), QStringLiteral("Small values are written in place instead of being referenced"), QStringLiteral("embed"));
    panelStep.chosen = true;
    addRow(QStringLiteral("Chosen"), panelStep, 502, GalleryPages::Default, Qt::LeftToRight, QStringLiteral("313:7291"));
    builder.row();
    addRow(QStringLiteral("Not chosen, the place of the check mark kept"), makeRow(Kind::PanelStep, QStringLiteral("Shrink object storage (squeeze free entries)"), QStringLiteral("Objects are renumbered so no gaps remain"), QStringLiteral("hash")), 502, GalleryPages::Default, Qt::LeftToRight, QStringLiteral("313:7327"));
    builder.row();

    builder.block(QStringLiteral("Tile row - steps of the sidebar (Step) 41"), QStringLiteral("303:4405"));
    Painter::TileRow step = makeRow(Kind::Step, QStringLiteral("Embed simple objects"), QStringLiteral("Writes small values in place"), QStringLiteral("embed"));
    step.endIconName = QStringLiteral("check");
    addRow(QStringLiteral("Will run"), step, 273, GalleryPages::Default, Qt::LeftToRight, QStringLiteral("319:7289"));
    Painter::TileRow skipped = makeRow(Kind::Step, QStringLiteral("Shrink object storage"), QStringLiteral("Will be skipped"), QStringLiteral("hash"), Tone::Muted);
    skipped.endIconName = QStringLiteral("minus");
    skipped.endIconColor = Color::IconMuted;
    addRow(QStringLiteral("Skipped"), skipped, 273, GalleryPages::Default, Qt::LeftToRight, QStringLiteral("319:7380"));
    Painter::TileRow performed = makeRow(Kind::Step, QStringLiteral("Embed simple objects"), QStringLiteral("1,204 objects embedded"), QStringLiteral("embed"));
    performed.endIconName = QStringLiteral("check-circle");
    performed.endIconColor = Color::AccentGreenIcon;
    performed.descriptionColor = Color::TextSecondary;
    addRow(QStringLiteral("Performed"), performed, 273, GalleryPages::Default, Qt::LeftToRight, QStringLiteral("319:7399"));
    builder.row();
    pdf::PDFTileRow* liveStep = new pdf::PDFTileRow(QStringLiteral("Merge identical objects"), QStringLiteral("Stores duplicates once"), QStringLiteral("copy"), Kind::Step, builder.page());
    liveStep->setEndIcon(QStringLiteral("check"), Color::IconAccent);
    liveStep->setClickable(false);
    liveStep->setFixedWidth(273);
    builder.live(QStringLiteral("Live, not clickable"), liveStep);
    builder.row();

    // Pending changes
    builder.block(QStringLiteral("Tile row - pending changes (Pending) 46 with a link"), QStringLiteral("389:20177"));
    Painter::TileRow pending = makeRow(Kind::Pending, QStringLiteral("Page 9"), QStringLiteral("90\u00B0 right \u00B7 portrait \u2192 landscape"), QStringLiteral("rotate-cw"));
    pending.linkText = QStringLiteral("Undo");
    addRow(QStringLiteral("Rotation"), pending, 279, GalleryPages::Default, Qt::LeftToRight, QStringLiteral("389:20411"));
    Painter::TileRow removed = makeRow(Kind::Pending, QStringLiteral("Page 4"), QStringLiteral("A4 portrait"), QString(), Tone::Danger);
    removed.tileText = QStringLiteral("4");
    removed.linkText = QStringLiteral("Keep");
    addRow(QStringLiteral("Removal, a number in the tile"), removed, 279, GalleryPages::Default, Qt::LeftToRight, QStringLiteral("388:20825"));
    addRow(QStringLiteral("Right-to-left"), removed, 279, GalleryPages::Default, Qt::RightToLeft);
    builder.row();
    pdf::PDFTileRow* livePending = new pdf::PDFTileRow(QStringLiteral("Page 10"), QStringLiteral("90\u00B0 right \u00B7 portrait \u2192 landscape"), QStringLiteral("rotate-cw"), Kind::Pending, builder.page());
    livePending->setLinkText(QStringLiteral("Undo"));
    livePending->setClickable(false);
    livePending->setFixedWidth(279);
    builder.live(QStringLiteral("Live with the link"), livePending, QStringLiteral("389:20411"));
    pdf::PDFTileRow* liveRemoved = new pdf::PDFTileRow(QStringLiteral("Page 20"), QStringLiteral("A4 portrait"), QString(), Kind::Pending, builder.page());
    liveRemoved->setTileText(QStringLiteral("20"));
    liveRemoved->setTone(Tone::Danger);
    liveRemoved->setLinkText(QStringLiteral("Keep"));
    liveRemoved->setFixedWidth(279);
    builder.live(QStringLiteral("Live, clickable"), liveRemoved, QStringLiteral("388:20825"));
    builder.row();

    // Rows of the lists with a check box
    builder.block(QStringLiteral("Tile row - rows of a list with a check box (ListItem) 44 / 57, 49 with a tag"), QStringLiteral("438:14751"));
    Painter::TileRow link = makeRow(Kind::ListItem, QStringLiteral("Market study 2026"), QStringLiteral("https://research.example.org/\u2026"), QStringLiteral("globe"));
    link.titleKind = Painter::TextKind::Data;
    link.descriptionKind = Painter::TextKind::Data;
    link.checkBoxVisible = true;
    link.checkBoxChecked = true;
    addRow(QStringLiteral("Ticked"), link, 261, GalleryPages::Default);
    addRow(QStringLiteral("Shown on the page"), link, 261, GalleryPages::Selected, Qt::LeftToRight, QStringLiteral("438:14890"));
    Painter::TileRow kept = link;
    kept.title = QStringLiteral("statistics.example.gov");
    kept.description = QStringLiteral("https://statistics.example.go\u2026");
    kept.checkBoxChecked = false;
    addRow(QStringLiteral("Not ticked"), kept, 261, GalleryPages::Default, Qt::LeftToRight, QStringLiteral("438:14900"));
    builder.row();
    Painter::TileRow twoLines = link;
    twoLines.title = QStringLiteral("jane.novak@example.com");
    twoLines.description = QStringLiteral("mailto:jane.novak@example.com/a-longer-address");
    twoLines.iconName = QStringLiteral("mail");
    addRow(QStringLiteral("A long address (57)"), twoLines, 261, GalleryPages::Default, Qt::LeftToRight, QStringLiteral("438:14868"));
    addRow(QStringLiteral("Data text, right-to-left"), twoLines, 261, GalleryPages::Default, Qt::RightToLeft);
    addRow(QStringLiteral("Caption of a group (21)"), makeRow(Kind::Caption, QStringLiteral("Page 3"), QString(), QString()), 261, GalleryPages::Default, Qt::LeftToRight, QStringLiteral("438:14866"));
    Painter::TileRow notFound = makeRow(Kind::ListItem, QStringLiteral("Embedded search index"), QStringLiteral("Not found in this document"), QStringLiteral("search-index"));
    notFound.checkBoxVisible = true;
    notFound.checkBoxEnabled = false;
    notFound.tag = QStringLiteral("Not found");
    addRow(QStringLiteral("A kind not found (49)"), notFound, 622, GalleryPages::Default, Qt::LeftToRight, QStringLiteral("438:12782"));
    builder.row();
    addRow(QStringLiteral("Without a check box, selected"), makeRow(Kind::ListItem, QStringLiteral("Jane Novak"), QStringLiteral(".pfx file \u00B7 valid until Sep 12, 2029"), QStringLiteral("key")), 276, GalleryPages::Selected, Qt::LeftToRight, QStringLiteral("434:4466"));
    addRow(QStringLiteral("Without a check box, not selected"), makeRow(Kind::ListItem, QStringLiteral("Company ID"), QStringLiteral("System store \u00B7 valid until Mar 3, 2027"), QStringLiteral("key"), Tone::Muted), 276, GalleryPages::Default, Qt::LeftToRight, QStringLiteral("434:4476"));
    builder.row();

    pdf::PDFTileRow* liveLink = new pdf::PDFTileRow(QStringLiteral("acme-corp.example"), QStringLiteral("https://www.acme-corp.example\u2026"), QStringLiteral("globe"), Kind::ListItem, builder.page());
    liveLink->setCheckBoxVisible(true);
    liveLink->setCheckBoxChecked(true);
    liveLink->setSelectionBehavior(pdf::PDFComponentButton::SelectionBehavior::Toggle);
    liveLink->setTitleKind(Painter::TextKind::Data);
    liveLink->setDescriptionKind(Painter::TextKind::Data);
    liveLink->setFixedWidth(261);
    builder.live(QStringLiteral("Live with the check box"), liveLink, QStringLiteral("438:14868"));
    pdf::PDFTileRow* liveKind = new pdf::PDFTileRow(QStringLiteral("Embedded search index"), QStringLiteral("Not found in this document"), QStringLiteral("search-index"), Kind::ListItem, builder.page());
    liveKind->setCheckBoxVisible(true);
    liveKind->setCheckBoxEnabled(false);
    liveKind->setTag(QStringLiteral("Not found"));
    liveKind->setFixedWidth(622);
    builder.live(QStringLiteral("Live, the check box disabled alone"), liveKind, QStringLiteral("438:12782"));
    builder.row();

    // Results
    builder.block(QStringLiteral("Tile row - results (Result) 44, a tile of 26"), QStringLiteral("438:14342"));
    Painter::TileRow result = makeRow(Kind::Result, QStringLiteral("Document info"), QStringLiteral("Document info was removed"), QStringLiteral("info"), Tone::Muted);
    result.endIconName = QStringLiteral("check-circle");
    result.endIconColor = Color::AccentGreenIcon;
    addRow(QStringLiteral("Removed"), result, 273, GalleryPages::Default, Qt::LeftToRight, QStringLiteral("438:14577"));
    builder.row();

    // A long list of the rows from a model
    builder.block(QStringLiteral("PDFTileRowDelegate - lists of thousands of rows"), QStringLiteral("438:14751"));
    const auto createLinkModel = []()
    {
        QStandardItemModel* model = new QStandardItemModel();
        struct Row
        {
            const char* title;
            const char* address;
            const char* icon;
            bool checked;
        };
        static constexpr Row rows[] =
        {
            { "Page 3", nullptr, nullptr, false },
            { "acme-corp.example", "https://www.acme-corp.example/a-longer-address/of-the-link", "globe", true },
            { "jane.novak@example.com", "mailto:jane.novak@example.com", "mail", true },
            { "Page 12", nullptr, nullptr, false },
            { "Market study 2026", "https://research.example.org/\xE2\x80\xA6", "globe", true },
            { "statistics.example.gov", "https://statistics.example.go\xE2\x80\xA6", "globe", false }
        };
        for (const Row& row : rows)
        {
            QStandardItem* item = new QStandardItem(QString::fromUtf8(row.title));
            if (!row.address)
            {
                // A caption of a group: no item for the selection and the keyboard
                item->setData(int(Kind::Caption), Roles::TileRowKindRole);
                item->setFlags(Qt::NoItemFlags);
                model->appendRow(item);
                continue;
            }
            item->setData(QString::fromUtf8(row.address), Roles::SubtitleRole);
            item->setData(QString::fromLatin1(row.icon), Roles::IconNameRole);
            item->setCheckable(true);
            item->setCheckState(row.checked ? Qt::Checked : Qt::Unchecked);
            item->setEditable(false);
            model->appendRow(item);
        }
        return model;
    };
    const auto createPendingModel = []()
    {
        QStandardItemModel* model = new QStandardItemModel();
        for (const int page : { 4, 5, 6, 20 })
        {
            QStandardItem* item = new QStandardItem(QStringLiteral("Page %1").arg(page));
            item->setData(QStringLiteral("A4 portrait"), Roles::SubtitleRole);
            item->setData(QString::number(page), Roles::TileTextRole);
            item->setData(int(Tone::Danger), Roles::TileToneRole);
            item->setData(QStringLiteral("Keep"), Roles::LinkTextRole);
            item->setEditable(false);
            model->appendRow(item);
        }
        return model;
    };

    QWidget* page = builder.page();
    const auto createRowView = [page](Kind kind, QStandardItemModel* model, QSize size, Qt::LayoutDirection direction, bool isEnabled, int selectedRow)
    {
        pdf::PDFTileRowDelegate* delegate = new pdf::PDFTileRowDelegate();
        delegate->setKind(kind);
        delegate->setTitleKind(Painter::TextKind::Data);
        delegate->setDescriptionKind(Painter::TextKind::Data);
        delegate->setClickable(kind != Kind::Pending);
        QListView* view = Helper::createListView(page, delegate, model, size, 1);
        view->setLayoutDirection(direction);
        view->setEnabled(isEnabled);
        if (selectedRow >= 0)
        {
            Helper::selectRow(view, selectedRow);
        }
        return view;
    };
    builder.live(QStringLiteral("Live links (ListItem)"), createRowView(Kind::ListItem, createLinkModel(), QSize(285, 290), Qt::LeftToRight, true, 4));
    builder.live(QStringLiteral("Live right-to-left"), createRowView(Kind::ListItem, createLinkModel(), QSize(285, 290), Qt::RightToLeft, true, 4));
    builder.live(QStringLiteral("Live disabled (O-08)"), createRowView(Kind::ListItem, createLinkModel(), QSize(285, 290), Qt::LeftToRight, false, 4));
    builder.live(QStringLiteral("Live pending changes (Pending)"), createRowView(Kind::Pending, createPendingModel(), QSize(300, 216), Qt::LeftToRight, true, -1));
    builder.row();

    // Page tiles
    const auto addTile = [&builder](const QString& caption, const Painter::PageTile& tile, int flags, Qt::LayoutDirection direction = Qt::LeftToRight, const QString& node = QString())
    {
        builder.specimen(caption, Painter::getPageTileSize(tile), [tile, flags, direction](QPainter& painter, const QRect& rect)
        {
            const qreal margin = GallerySpecimen::MARGIN;
            pdf::PDFStylePainter::fillRect(&painter, QRectF(rect).adjusted(-margin, -margin, margin, margin), Color::BgTrack);
            Painter::paintPageTile(&painter, QRectF(rect), tile, Helper::state(flags, direction));
        }, node);
    };

    builder.block(QStringLiteral("Page tile (PDFPageTileDelegate) - Rotate 116 x 132, the selected one 118 x 150"), QStringLiteral("389:20177"));
    Painter::PageTile normal;
    normal.label = QStringLiteral("8");
    normal.pixmap = Helper::createPagePicture(0);
    addTile(QStringLiteral("Normal"), normal, GalleryPages::Default);
    addTile(QStringLiteral("Hover"), normal, GalleryPages::Hover);
    addTile(QStringLiteral("Pressed"), normal, GalleryPages::Hover | GalleryPages::Pressed);
    addTile(QStringLiteral("Focus"), normal, GalleryPages::Focus);
    addTile(QStringLiteral("Disabled (O-08)"), normal, GalleryPages::Disabled);
    builder.row();
    Painter::PageTile selected;
    selected.label = QStringLiteral("9");
    selected.caption = QStringLiteral("90\u00B0 right");
    selected.badgeIconName = QStringLiteral("rotate-cw");
    selected.tileState = Painter::PageTileState::Selected;
    selected.pixmap = Helper::createPagePicture(0).transformed(QTransform().rotate(90));
    addTile(QStringLiteral("Selected, turned"), selected, GalleryPages::Default, Qt::LeftToRight, QStringLiteral("389:20177"));
    addTile(QStringLiteral("Selected Hover"), selected, GalleryPages::Hover);
    addTile(QStringLiteral("Selected Pressed"), selected, GalleryPages::Hover | GalleryPages::Pressed);
    addTile(QStringLiteral("Selected Focus"), selected, GalleryPages::Focus);
    addTile(QStringLiteral("Selected right-to-left"), selected, GalleryPages::Default, Qt::RightToLeft);
    builder.row();

    builder.block(QStringLiteral("Page tile - Remove 94 x 140, the marked one 94 x 156"), QStringLiteral("388:20614"));
    Painter::PageTile removeNormal;
    removeNormal.label = QStringLiteral("3");
    removeNormal.slotSize = QSize(82, 112);
    removeNormal.pixmap = Helper::createPagePicture(1);
    addTile(QStringLiteral("Normal"), removeNormal, GalleryPages::Default);
    Painter::PageTile marked = removeNormal;
    marked.label = QStringLiteral("4");
    marked.caption = QStringLiteral("will be removed");
    marked.badgeIconName = QStringLiteral("trash");
    marked.tileState = Painter::PageTileState::Marked;
    addTile(QStringLiteral("Marked"), marked, GalleryPages::Default, Qt::LeftToRight, QStringLiteral("388:20614"));
    addTile(QStringLiteral("Marked Hover"), marked, GalleryPages::Hover);
    addTile(QStringLiteral("Marked right-to-left"), marked, GalleryPages::Default, Qt::RightToLeft);
    Painter::PageTile placeholder = removeNormal;
    placeholder.pixmap = QPixmap();
    placeholder.pageSize = QSizeF(842, 595);
    addTile(QStringLiteral("No picture yet, landscape"), placeholder, GalleryPages::Default);
    builder.row();

    // Insert Pages, Replace (388:17167): the page is dimmed as a removed one, but it is replaced -
    // the badge accent/amber, the caption text/muted (owner 2026-11-09)
    builder.block(QStringLiteral("Page tile - Replaced 85 x 144 (Insert Pages, Replace)"), QStringLiteral("388:17167"));
    Painter::PageTile replaced;
    replaced.label = QStringLiteral("13");
    replaced.slotSize = QSize(73, 100);
    replaced.pixmap = Helper::createPagePicture(1);
    replaced.caption = QStringLiteral("replaced");
    replaced.badgeIconName = QStringLiteral("replace");
    replaced.tileState = Painter::PageTileState::Replaced;
    addTile(QStringLiteral("Replaced"), replaced, GalleryPages::Default, Qt::LeftToRight, QStringLiteral("388:17167"));
    addTile(QStringLiteral("Replaced Hover"), replaced, GalleryPages::Hover);
    addTile(QStringLiteral("Replaced Disabled (O-08)"), replaced, GalleryPages::Disabled);
    addTile(QStringLiteral("Replaced right-to-left"), replaced, GalleryPages::Default, Qt::RightToLeft);
    builder.row();

    const auto createTileView = [page](const QSize& slot, const std::vector<int>& states, QSize size)
    {
        QStandardItemModel* model = new QStandardItemModel();
        int number = 1;
        for (const int state : states)
        {
            QStandardItem* item = new QStandardItem(QString::number(number++));
            item->setData(Helper::createPagePicture(number % 2), Roles::PixmapRole);
            item->setData(state, Roles::PageTileStateRole);
            if (state == int(Painter::PageTileState::Selected))
            {
                item->setData(QStringLiteral("90\u00B0 right"), Roles::SubtitleRole);
                item->setData(QStringLiteral("rotate-cw"), Roles::StateIconRole);
            }
            else if (state == int(Painter::PageTileState::Marked))
            {
                item->setData(QStringLiteral("will be removed"), Roles::SubtitleRole);
                item->setData(QStringLiteral("trash"), Roles::StateIconRole);
            }
            else if (state == int(Painter::PageTileState::Replaced))
            {
                item->setData(QStringLiteral("replaced"), Roles::SubtitleRole);
                item->setData(QStringLiteral("replace"), Roles::StateIconRole);
            }
            item->setEditable(false);
            model->appendRow(item);
        }

        pdf::PDFPageTileDelegate* delegate = new pdf::PDFPageTileDelegate();
        delegate->setSlotSize(slot);
        QListView* view = Helper::createListView(page, delegate, model, size, 6);
        view->setViewMode(QListView::IconMode);
        view->setMovement(QListView::Static);
        view->setWrapping(true);
        view->setResizeMode(QListView::Adjust);
        view->setSelectionMode(QAbstractItemView::ExtendedSelection);
        return view;
    };
    builder.live(QStringLiteral("Live, Rotate"), createTileView(QSize(104, 104), { 0, 0, 1, 0, 1, 0 }, QSize(400, 320)));
    builder.live(QStringLiteral("Live, Remove"), createTileView(QSize(82, 112), { 0, 2, 2, 0, 0, 0, 2, 0 }, QSize(420, 340)));
    builder.live(QStringLiteral("Live, Replace"), createTileView(QSize(73, 100), { 0, 3, 3, 0, 0 }, QSize(420, 180)));
    builder.note(QStringLiteral("The rendered page comes from the model, the delegate never asks for it while it computes a size. The state of a tile is a role of the model."));

    return builder.finish();
}

}   // namespace pdffluentgallery
