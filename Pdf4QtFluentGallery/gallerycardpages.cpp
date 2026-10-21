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

// Samples of the tiles, cards and pictures of the components (step 3 of
// components.md, chapter 6.3) on the pages of their chapters, and the sample of
// the Home screen of the Figma page Main page composed of the widgets of the library.

#include "gallerypages.h"

#include "pdfaccenthue.h"
#include "pdfcomponentbadges.h"
#include "pdfcomponentbuttons.h"
#include "pdfcomponentcards.h"
#include "pdfcomponentpainter.h"
#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdfcomponentwindow.h"
#include "pdffluentwidgets.h"
#include "pdflockeddocumentpicture.h"
#include "pdfpreviewpainter.h"
#include "pdfstylepainter.h"
#include "pdfthemecolors.h"
#include "pdfthemefonts.h"
#include "pdfthemeiconengine.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyleproperties.h"

#include <QAbstractButton>
#include <QApplication>
#include <QButtonGroup>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLinearGradient>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include <QtMath>

#include <functional>

namespace pdffluentgallery
{

/// Widget of the sample of the Home screen, which draws a part of the frame,
/// which is not a component (the tile of the logo, the drop zone, a line, texts
/// of more styles). It draws by its function with the colors and fonts of the
/// active theme and it is repainted after a change of the theme.
class GalleryCardPaintedWidget : public QWidget
{
public:
    using PaintFunction = std::function<void(QPainter&, const QRectF&)>;

    explicit GalleryCardPaintedWidget(QSize sizeHint, PaintFunction paint, QWidget* parent) :
        QWidget(parent),
        m_sizeHint(sizeHint),
        m_paint(std::move(paint))
    {
        connect(pdf::PDFThemeManager::getInstance(), &pdf::PDFThemeManager::themeChanged, this, qOverload<>(&QWidget::update));
    }

    virtual QSize sizeHint() const override { return m_sizeHint; }
    virtual QSize minimumSizeHint() const override { return m_sizeHint; }

protected:
    virtual void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        if (m_paint)
        {
            m_paint(painter, QRectF(rect()));
        }
    }

private:
    QSize m_sizeHint;
    PaintFunction m_paint;
};

/// Helper functions of the samples of the tiles and cards
class GalleryCardPagesHelper
{
public:
    GalleryCardPagesHelper() = delete;

    using Color = pdf::PDFThemeColors::Color;
    using Font = pdf::PDFThemeFonts::Role;
    using Painter = pdf::PDFComponentPainter;
    using Preview = pdf::PDFPreviewPainter::Preview;
    using Properties = pdf::PDFWidgetStyleProperties;

    struct NamedState
    {
        const char* name;
        int flags;
    };

    /// States of the cards: the four variants of the design and the disabled state (O-08)
    static constexpr NamedState STATES[] =
    {
        { "Default", GalleryPages::Default },
        { "Hover", GalleryPages::Hover },
        { "Pressed", GalleryPages::Pressed | GalleryPages::Hover },
        { "Focus", GalleryPages::Focus },
        { "Disabled (O-08)", GalleryPages::Disabled }
    };

    /// Nodes of the variants of a hue (Default, Hover, Pressed, Focus)
    struct HueNodes
    {
        const char* name;
        pdf::PDFAccentHue hue;
        const char* nodes[4];
    };

    /// Returns the state of the painter for the flags of a sample
    static Painter::State state(int flags, Qt::LayoutDirection direction = QApplication::layoutDirection())
    {
        return Painter::State(GalleryPages::toStyleState(GalleryPages::StateFlags(flags)), direction);
    }

    /// Forces the hover, the press and the disabled state of a live button
    static QAbstractButton* setState(QAbstractButton* button, int flags)
    {
        button->setAttribute(Qt::WA_UnderMouse, (flags & GalleryPages::Hover) != 0);
        button->setDown((flags & GalleryPages::Pressed) != 0);
        button->setEnabled((flags & GalleryPages::Disabled) == 0);
        return button;
    }

    /// Fills the sample (with its margin) by bg/surface: the cards, which lie on
    /// bg/surface cards or panels on the screens
    static void fillSurface(QPainter& painter, const QRectF& rect)
    {
        const qreal margin = GallerySpecimen::MARGIN;
        pdf::PDFStylePainter::fillRect(&painter, rect.adjusted(-margin, -margin, margin, margin), Color::BgSurface);
    }

    /// Adds the samples of the five states of a card drawn by \p paint(painter, rect, state);
    /// \p nodes are the nodes of the four variants of the design
    static void stateSpecimens(GalleryBuilder& builder, const QString& prefix, QSize size, const char* const nodes[4], bool onSurface, int extraFlags,
                               const std::function<void(QPainter&, const QRectF&, const Painter::State&)>& paint)
    {
        for (int i = 0; i < 5; ++i)
        {
            const int flags = STATES[i].flags | extraFlags;
            const QString caption = prefix.isEmpty() ? QString::fromLatin1(STATES[i].name) : QStringLiteral("%1 %2").arg(prefix, QString::fromLatin1(STATES[i].name));
            const QString node = (i < 4 && nodes && nodes[i]) ? QString::fromLatin1(nodes[i]) : QString();
            builder.specimen(caption, size, [paint, flags, onSurface](QPainter& painter, const QRect& rect)
            {
                if (onSurface)
                {
                    fillSurface(painter, QRectF(rect));
                }
                paint(painter, QRectF(rect), state(flags));
            }, node);
        }
        builder.row();
    }

    /// Adds a single sample of a card
    static void specimen(GalleryBuilder& builder, const QString& caption, QSize size, bool onSurface, int flags,
                         const std::function<void(QPainter&, const QRectF&, const Painter::State&)>& paint, const QString& node = QString(),
                         Qt::LayoutDirection direction = QApplication::layoutDirection())
    {
        builder.specimen(caption, size, [paint, flags, onSurface, direction](QPainter& painter, const QRect& rect)
        {
            if (onSurface)
            {
                fillSurface(painter, QRectF(rect));
            }
            paint(painter, QRectF(rect), state(flags, direction));
        }, node);
    }

    /// Returns a card (bg/surface with the border of the style)
    static QWidget* createCard(QWidget* parent, int margin, int spacing, Qt::Orientation orientation = Qt::Vertical)
    {
        QWidget* card = new QWidget(parent);
        Properties::set(card, Properties::CARD);
        QBoxLayout* layout = orientation == Qt::Vertical ? static_cast<QBoxLayout*>(new QVBoxLayout(card)) : static_cast<QBoxLayout*>(new QHBoxLayout(card));
        layout->setContentsMargins(margin, margin, margin, margin);
        layout->setSpacing(spacing);
        return card;
    }

    /// Adds live cards in the states hover, pressed and disabled and in the
    /// right-to-left layout; \p create returns a new card. Cards lying on a
    /// surface on the screens are put on a card.
    static void liveStates(GalleryBuilder& builder, bool onSurface, const std::function<QAbstractButton*()>& create)
    {
        struct LiveState
        {
            const char* name;
            int flags;
            bool rightToLeft;
        };
        static constexpr LiveState liveStatesList[] =
        {
            { "Live", GalleryPages::Default, false },
            { "Live hover", GalleryPages::Hover, false },
            { "Live pressed", GalleryPages::Pressed | GalleryPages::Hover, false },
            { "Live disabled", GalleryPages::Disabled, false },
            { "Live right-to-left", GalleryPages::Default, true }
        };

        for (const LiveState& liveState : liveStatesList)
        {
            QAbstractButton* button = setState(create(), liveState.flags);
            if (liveState.rightToLeft)
            {
                button->setLayoutDirection(Qt::RightToLeft);
            }

            if (onSurface)
            {
                QWidget* card = createCard(builder.page(), 6, 0);
                card->setLayoutDirection(button->layoutDirection());
                button->setParent(card);
                card->layout()->addWidget(button);
                builder.live(QString::fromLatin1(liveState.name), card);
            }
            else
            {
                builder.live(QString::fromLatin1(liveState.name), button);
            }
        }
        builder.row();
    }

    /// Picture of a page, which stands for a page rendered by the application
    static QPixmap createPagePicture()
    {
        QPixmap pixmap(QSize(120, 158));
        pixmap.fill(pdf::PDFStylePainter::color(Color::SampleWhite));

        QPainter painter(&pixmap);
        pdf::PDFStylePainter::fillRect(&painter, QRectF(12, 14, 70, 8), Color::SampleGrayDark);
        pdf::PDFStylePainter::fillRect(&painter, QRectF(12, 30, 96, 40), Color::SampleBlueSoft);
        pdf::PDFStylePainter::fillRect(&painter, QRectF(20, 38, 40, 24), Color::SampleBlue);
        for (int line = 0; line < 8; ++line)
        {
            pdf::PDFStylePainter::fillRect(&painter, QRectF(12, 80 + line * 8, line == 7 ? 54 : 96, 3), Color::SampleGrayMid);
        }
        return pixmap;
    }

    /// Picture of a video, which stands for the cached picture of the application
    static QPixmap createVideoPicture()
    {
        QPixmap pixmap(QSize(320, 180));
        QPainter painter(&pixmap);
        QLinearGradient gradient(0.0, 0.0, 320.0, 180.0);
        gradient.setColorAt(0.0, pdf::PDFStylePainter::color(Color::SampleBlueSoft));
        gradient.setColorAt(1.0, pdf::PDFStylePainter::color(Color::SampleOrangeSoft));
        painter.fillRect(QRect(0, 0, 320, 180), gradient);
        pdf::PDFStylePainter::fillRect(&painter, QRectF(40, 30, 120, 150), Color::SampleWhite);
        pdf::PDFStylePainter::fillRect(&painter, QRectF(56, 46, 70, 8), Color::SampleGrayDark);
        for (int line = 0; line < 6; ++line)
        {
            pdf::PDFStylePainter::fillRect(&painter, QRectF(56, 66 + line * 10, 88, 4), Color::SampleGrayMid);
        }
        return pixmap;
    }

    /// Returns a label of a fixed role and color
    static pdf::PDFLabel* label(const QString& text, Font font, Color color, QWidget* parent)
    {
        return new pdf::PDFLabel(text, font, color, parent);
    }

    /// Run of a text of one style
    struct TextRun
    {
        QString text;
        Font font;
        Color color;
    };

    /// Returns a line of texts of more styles ("open-source **PDF** editor", "MIT License" in the footer)
    static QWidget* textRuns(const QList<TextRun>& runs, int height, QWidget* parent)
    {
        qreal width = 0.0;
        for (const TextRun& run : runs)
        {
            width += pdf::PDFStylePainter::textWidth(run.text, pdf::PDFThemeFonts::getFont(run.font));
        }

        return new GalleryCardPaintedWidget(QSize(qCeil(width) + 1, height), [runs](QPainter& painter, const QRectF& rect)
        {
            qreal left = rect.left();
            for (const TextRun& run : runs)
            {
                const QFont font = pdf::PDFThemeFonts::getFont(run.font);
                const qreal runWidth = pdf::PDFStylePainter::textWidth(run.text, font);
                pdf::PDFStylePainter::text(&painter, QRectF(left, rect.top(), runWidth + 1.0, rect.height()), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                                           run.text, font, pdf::PDFStylePainter::color(run.color));
                left += runWidth;
            }
        }, parent);
    }

    /// Returns a horizontal line of 1 px of the color
    static QWidget* line(Color color, QWidget* parent)
    {
        QWidget* widget = new GalleryCardPaintedWidget(QSize(1, 1), [color](QPainter& painter, const QRectF& rect)
        {
            pdf::PDFStylePainter::fillRect(&painter, rect, color);
        }, parent);
        widget->setFixedHeight(1);
        widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        return widget;
    }
};

void GalleryPages::addShellHomeCards(GalleryBuilder& builder)
{
    using Helper = GalleryCardPagesHelper;
    using Painter = pdf::PDFComponentPainter;

    QWidget* page = builder.page();

    // Quick action tile: Hue x State, in the order of the design
    builder.block(QStringLiteral("Quick action tile (PDFQuickActionTile) - Hue x State, on bg/canvas"), QStringLiteral("46:431"));
    static constexpr Helper::HueNodes tileHues[] =
    {
        { "Blue", pdf::PDFAccentHue::Blue, { "46:268", "46:270", "46:277", "46:284" } },
        { "Violet", pdf::PDFAccentHue::Violet, { "46:291", "46:298", "46:305", "46:312" } },
        { "Amber", pdf::PDFAccentHue::Amber, { "46:319", "46:326", "46:333", "46:340" } },
        { "Red", pdf::PDFAccentHue::Red, { "46:347", "46:354", "46:361", "46:368" } },
        { "Teal", pdf::PDFAccentHue::Teal, { "46:375", "46:382", "46:389", "46:396" } },
        { "Neutral", pdf::PDFAccentHue::Neutral, { "46:403", "46:410", "46:417", "46:424" } },
        { "Green", pdf::PDFAccentHue::Green, { "93:1387", "93:1395", "93:1403", "93:1411" } }
    };
    for (const Helper::HueNodes& hue : tileHues)
    {
        Painter::QuickActionTile tile;
        tile.title = QStringLiteral("Open PDF");
        tile.description = QStringLiteral("Browse and open a document");
        tile.hue = hue.hue;
        Helper::stateSpecimens(builder, QString::fromLatin1(hue.name), Painter::getQuickActionTileSize(tile), hue.nodes, false, 0,
                               [tile](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintQuickActionTile(&painter, rect, tile, state); });
    }

    Painter::QuickActionTile starTile;
    starTile.title = QStringLiteral("Compress");
    starTile.description = QStringLiteral("Reduce the file size in one step");
    starTile.iconName = QStringLiteral("minimize-2");
    starTile.hue = pdf::PDFAccentHue::Violet;
    starTile.starVisible = true;
    Helper::specimen(builder, QStringLiteral("Show star"), Painter::getQuickActionTileSize(starTile), false, GalleryPages::Default,
                     [starTile](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintQuickActionTile(&painter, rect, starTile, state); });
    Painter::QuickActionTile pinnedTile = starTile;
    pinnedTile.pinned = true;
    Helper::specimen(builder, QStringLiteral("Show star, pinned, hover"), Painter::getQuickActionTileSize(pinnedTile), false, GalleryPages::Hover,
                     [pinnedTile](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintQuickActionTile(&painter, rect, pinnedTile, state); });
    Painter::QuickActionTile longTile;
    longTile.title = QStringLiteral("Recognize Text (OCR)");
    longTile.description = QStringLiteral("Make scanned pages searchable");
    longTile.iconName = QStringLiteral("ocr");
    Helper::specimen(builder, QStringLiteral("Title of two lines"), Painter::getQuickActionTileSize(longTile), false, GalleryPages::Default,
                     [longTile](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintQuickActionTile(&painter, rect, longTile, state); });
    Helper::specimen(builder, QStringLiteral("Right-to-left"), Painter::getQuickActionTileSize(starTile), false, GalleryPages::Default,
                     [starTile](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintQuickActionTile(&painter, rect, starTile, state); }, QString(), Qt::RightToLeft);
    builder.row();
    Helper::liveStates(builder, false, [page]() { return new pdf::PDFQuickActionTile(QStringLiteral("Merge"), QStringLiteral("Combine several files into one PDF"), QStringLiteral("layers"), pdf::PDFAccentHue::Amber, page); });
    pdf::PDFQuickActionTile* liveStarTile = builder.live(QStringLiteral("Live with star, pinned"), new pdf::PDFQuickActionTile(QStringLiteral("Protect with Password"), QStringLiteral("Require a password to open the PDF"), QStringLiteral("lock"), pdf::PDFAccentHue::Red, page));
    liveStarTile->setStarVisible(true);
    liveStarTile->setPinned(true);
    liveStarTile->setFixedWidth(259);
    builder.row();

    // Folder card: Hue x State; the standard folders and the others, on bg/canvas and on bg/surface
    builder.block(QStringLiteral("Folder card (PDFFolderCard) - Hue x State"), QStringLiteral("63:597"));
    static constexpr Helper::HueNodes folderHues[] =
    {
        { "Blue", pdf::PDFAccentHue::Blue, { "63:517", "63:527", "63:537", "63:547" } },
        { "Neutral", pdf::PDFAccentHue::Neutral, { "63:557", "63:567", "63:577", "63:587" } },
        { "Green", pdf::PDFAccentHue::Green, { "72:878", "72:888", "72:898", "72:908" } },
        { "Violet", pdf::PDFAccentHue::Violet, { "72:918", "72:928", "72:938", "72:948" } }
    };
    for (const Helper::HueNodes& hue : folderHues)
    {
        Painter::FolderCard card;
        card.name = QStringLiteral("Documents");
        card.path = QStringLiteral("C:\\Users\\Jakub\\Documents");
        card.meta = QStringLiteral("Last used Today 10:24");
        card.hue = hue.hue;
        Helper::stateSpecimens(builder, QString::fromLatin1(hue.name), Painter::getFolderCardSize(card), hue.nodes, false, 0,
                               [card](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintFolderCard(&painter, rect, card, state); });
    }
    Painter::FolderCard noMeta;
    noMeta.name = QStringLiteral("Projects");
    noMeta.path = QStringLiteral("D:\\Projects\\PDF4QT\\Specifications\\Components\\Archive");
    noMeta.metaVisible = false;
    Helper::specimen(builder, QStringLiteral("Show meta false, long path"), Painter::getFolderCardSize(noMeta), false, GalleryPages::Default,
                     [noMeta](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintFolderCard(&painter, rect, noMeta, state); });
    Painter::FolderCard surfaceFolder;
    surfaceFolder.name = QStringLiteral("Team share");
    surfaceFolder.path = QStringLiteral("\\\\server\\documents");
    surfaceFolder.meta = QStringLiteral("Network");
    surfaceFolder.iconName = QStringLiteral("server");
    Helper::specimen(builder, QStringLiteral("On bg/surface (pinned locations)"), QSize(342, 84), true, GalleryPages::Default,
                     [surfaceFolder](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintFolderCard(&painter, rect, surfaceFolder, state); });
    Helper::specimen(builder, QStringLiteral("Right-to-left"), Painter::getFolderCardSize(surfaceFolder), false, GalleryPages::Default,
                     [surfaceFolder](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintFolderCard(&painter, rect, surfaceFolder, state); }, QString(), Qt::RightToLeft);
    builder.row();
    Helper::liveStates(builder, false, [page]() { return new pdf::PDFFolderCard(QStringLiteral("Downloads"), QStringLiteral("C:\\Users\\Jakub\\Downloads"), QStringLiteral("Last used Yesterday 18:02"), pdf::PDFAccentHue::Blue, page); });

    // Pinned card: State; the thumbnail is painted by the card
    builder.block(QStringLiteral("Pinned card (PDFPinnedCard) - State, the thumbnail and the bar painted by the card"), QStringLiteral("79:1115"));
    static constexpr const char* pinnedNodes[4] = { "79:1027", "79:1049", "79:1071", "79:1093" };
    Painter::PinnedCard pinned;
    pinned.name = QStringLiteral("Project_Report.pdf");
    pinned.path = QStringLiteral("C:\\Users\\Jakub\\Documents");
    pinned.progressText = QStringLiteral("Page 12 of 24 · Today 10:24");
    pinned.progress = 0.5;
    Helper::stateSpecimens(builder, QString(), Painter::getPinnedCardSize(pinned), pinnedNodes, false, 0,
                           [pinned](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintPinnedCard(&painter, rect, pinned, state); });
    Painter::PinnedCard finished = pinned;
    finished.pixmap = Helper::createPagePicture();
    finished.progress = 1.0;
    finished.progressText = QStringLiteral("Page 24 of 24 · Yesterday");
    Helper::specimen(builder, QStringLiteral("Picture of the page, finished (green)"), Painter::getPinnedCardSize(finished), false, GalleryPages::Default,
                     [finished](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintPinnedCard(&painter, rect, finished, state); });
    Painter::PinnedCard unread = pinned;
    unread.progress = 0.0;
    unread.pinned = false;
    unread.progressText = QStringLiteral("Not opened yet");
    Helper::specimen(builder, QStringLiteral("Progress 0, unpinned"), Painter::getPinnedCardSize(unread), false, GalleryPages::Default,
                     [unread](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintPinnedCard(&painter, rect, unread, state); });
    Helper::specimen(builder, QStringLiteral("Right-to-left"), Painter::getPinnedCardSize(pinned), false, GalleryPages::Default,
                     [pinned](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintPinnedCard(&painter, rect, pinned, state); }, QString(), Qt::RightToLeft);
    builder.row();

    // States without a frame of Figma (proposals of the integration of step 2)
    Painter::PinnedCard missingCard = pinned;
    missingCard.status = Painter::FileStatus::Missing;
    Helper::specimen(builder, QStringLiteral("Missing file (proposal)"), Painter::getPinnedCardSize(missingCard), false, GalleryPages::Default,
                     [missingCard](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintPinnedCard(&painter, rect, missingCard, state); });
    Painter::PinnedCard lockedCard = pinned;
    lockedCard.locked = true;
    Helper::specimen(builder, QStringLiteral("Protected by a password (proposal)"), Painter::getPinnedCardSize(lockedCard), false, GalleryPages::Default,
                     [lockedCard](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintPinnedCard(&painter, rect, lockedCard, state); });
    Helper::specimen(builder, QStringLiteral("Protected by a password, right-to-left"), Painter::getPinnedCardSize(lockedCard), false, GalleryPages::Hover,
                     [lockedCard](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintPinnedCard(&painter, rect, lockedCard, state); }, QString(), Qt::RightToLeft);
    builder.row();
    pdf::PDFPinnedCard* liveMissing = builder.live(QStringLiteral("Live missing file"), new pdf::PDFPinnedCard(QStringLiteral("Building_Plan.pdf"), QStringLiteral("D:\\Projects\\Old"), QStringLiteral("Page 1 of 12"), 1.0 / 12.0, page));
    liveMissing->setStatus(pdf::PDFPinnedCard::Status::Missing);
    pdf::PDFPinnedCard* liveLocked = builder.live(QStringLiteral("Live protected by a password"), new pdf::PDFPinnedCard(QStringLiteral("Salary_Review_2026.pdf"), QStringLiteral("C:\\Users\\Jakub\\HR"), QStringLiteral("Page 2 of 6 · Aug 28"), 2.0 / 6.0, page));
    liveLocked->setLocked(true);
    builder.row();
    const QPixmap pagePicture = Helper::createPagePicture();
    Helper::liveStates(builder, false, [page, pagePicture]()
    {
        pdf::PDFPinnedCard* card = new pdf::PDFPinnedCard(QStringLiteral("Annual_Report_2026.pdf"), QStringLiteral("C:\\Users\\Jakub\\Documents\\Reports"), QStringLiteral("Page 3 of 48 · Today 08:17"), 3.0 / 48.0, page);
        card->setPixmap(pagePicture);
        return card;
    });

    // Elements of the pages of step 2 (frames of Main page, not components of
    // Figma): icon badges, status badges, fact pills, avatars, status rows and
    // tinted cards (the integration of step 2 moved them from the application)
    addPageElements(builder);

    // The sample of the Home screen (Main page, frame pdf4qt-home 28:4)
    builder.block(QStringLiteral("Sample of the Home screen (Main page, pdf4qt-home) - composed of the widgets of the library"), QStringLiteral("28:4"));
    builder.wide(createHomeSample(page), QStringLiteral("Home screen"));
}

void GalleryPages::addPageElements(GalleryBuilder& builder)
{
    using Helper = GalleryCardPagesHelper;
    using Color = Helper::Color;
    using Painter = pdf::PDFComponentPainter;
    QWidget* page = builder.page();

    // Icon badge: the circle of a card, the rounded box of a logo, the circle of an empty state, a plain icon
    builder.block(QStringLiteral("Icon badge (PDFIconBadge) - icon-circle of the cards, logo-box of About, empty states"), QStringLiteral("103:1951"));
    struct BadgeSample
    {
        const char* name;
        int extent;
        int iconExtent;
        qreal radius;
        bool hasFill;
        Color fill;
        bool hasBorder;
        Color border;
        Color icon;
        const char* iconName;
        bool logo;
    };
    static constexpr BadgeSample badgeSamples[] =
    {
        { "Circle 40, hue Blue", 40, 20, -1.0, true, Color::AccentBlueBg, false, Color::BorderDefault, Color::AccentBlueIcon, "download", false },
        { "Circle 36 on a tinted card", 36, 18, -1.0, true, Color::BgSurface, false, Color::BorderDefault, Color::AccentRedIcon, "heart", false },
        { "Box 56, radius 14, accent", 56, 28, 14.0, true, Color::BgAccent, false, Color::BorderDefault, Color::IconOnAccent, "file-text", false },
        { "Logo box 88, radius 16, border", 88, 64, 16.0, true, Color::BgSurface, true, Color::AccentBlueBorder, Color::IconDefault, "", true },
        { "Empty state 72", 72, 32, -1.0, true, Color::AccentNeutralBg, false, Color::BorderDefault, Color::AccentNeutralIcon, "clock", false },
        { "Icon only 14 (no fill)", 14, 14, 0.0, false, Color::BgSurface, false, Color::BorderDefault, Color::IconDanger, "alert-circle", false }
    };
    for (const BadgeSample& sample : badgeSamples)
    {
        Painter::IconBadge badge;
        badge.iconName = QString::fromLatin1(sample.iconName);
        badge.extent = sample.extent;
        badge.iconExtent = sample.iconExtent;
        badge.radius = sample.radius;
        badge.fill = sample.hasFill ? std::optional<Color>(sample.fill) : std::nullopt;
        badge.border = sample.hasBorder ? std::optional<Color>(sample.border) : std::nullopt;
        badge.iconColor = sample.icon;
        badge.logo = sample.logo;
        Helper::specimen(builder, QString::fromLatin1(sample.name), Painter::getIconBadgeSize(badge), false, GalleryPages::Default,
                         [badge](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintIconBadge(&painter, rect, badge, state); });
    }
    builder.row();
    pdf::PDFIconBadge* liveBadge = builder.live(QStringLiteral("Live, hue Green"), new pdf::PDFIconBadge(QStringLiteral("check"), 40, 20, page));
    liveBadge->setHue(pdf::PDFAccentHue::Green);
    pdf::PDFIconBadge* liveLogo = builder.live(QStringLiteral("Live logo"), new pdf::PDFIconBadge(QString(), 56, 40, page));
    liveLogo->setRadius(12.0);
    liveLogo->setColors(Color::BgSurface, Color::IconDefault, Color::BorderDefault);
    liveLogo->setLogo(true);
    pdf::PDFIconBadge* disabledBadge = builder.live(QStringLiteral("Live disabled"), new pdf::PDFIconBadge(QStringLiteral("search"), 40, 20, page));
    disabledBadge->setEnabled(false);
    builder.row();

    // Status badge
    builder.block(QStringLiteral("Status badge (PDFStatusBadge) - badge of the release card of What's New"), QStringLiteral("103:1951"));
    const Painter::StatusBadge upToDate{ QStringLiteral("Up to date"), QStringLiteral("check"), pdf::PDFAccentHue::Green };
    const Painter::StatusBadge available{ QStringLiteral("Update available"), QStringLiteral("download"), pdf::PDFAccentHue::Amber };
    const Painter::StatusBadge previous{ QStringLiteral("Previous release"), QStringLiteral("clock"), pdf::PDFAccentHue::Neutral };
    for (const Painter::StatusBadge& badge : { upToDate, available, previous })
    {
        Helper::specimen(builder, badge.text, Painter::getStatusBadgeSize(badge), false, GalleryPages::Default,
                         [badge](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintStatusBadge(&painter, rect, badge, state); });
    }
    Helper::specimen(builder, QStringLiteral("Right-to-left"), Painter::getStatusBadgeSize(upToDate), false, GalleryPages::Default,
                     [upToDate](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintStatusBadge(&painter, rect, upToDate, state); }, QString(), Qt::RightToLeft);
    Helper::specimen(builder, QStringLiteral("Disabled"), Painter::getStatusBadgeSize(upToDate), false, GalleryPages::Disabled,
                     [upToDate](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintStatusBadge(&painter, rect, upToDate, state); });
    builder.row();
    builder.live(QStringLiteral("Live"), new pdf::PDFStatusBadge(QStringLiteral("Up to date"), QStringLiteral("check"), pdf::PDFAccentHue::Green, page));
    builder.row();

    // Fact pill and avatar
    builder.block(QStringLiteral("Fact pill (PDFFactPill) and Avatar (PDFAvatar) - hero cards of About and Sponsor"), QStringLiteral("111:2226"));
    const Painter::FactPill pill{ QStringLiteral("shield"), QStringLiteral("MIT License") };
    Helper::specimen(builder, QStringLiteral("Fact pill"), Painter::getFactPillSize(pill), false, GalleryPages::Default,
                     [pill](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintFactPill(&painter, rect, pill, state); });
    Helper::specimen(builder, QStringLiteral("Fact pill right-to-left"), Painter::getFactPillSize(pill), false, GalleryPages::Default,
                     [pill](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintFactPill(&painter, rect, pill, state); }, QString(), Qt::RightToLeft);
    const Painter::Avatar avatar{ QStringLiteral("JM"), 56, 18, pdf::PDFAccentHue::Blue };
    const Painter::Avatar smallAvatar{ QStringLiteral("JM"), 40, 14, pdf::PDFAccentHue::Blue };
    Helper::specimen(builder, QStringLiteral("Avatar 56"), Painter::getAvatarSize(avatar), false, GalleryPages::Default,
                     [avatar](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintAvatar(&painter, rect, avatar, state); });
    Helper::specimen(builder, QStringLiteral("Avatar 40"), Painter::getAvatarSize(smallAvatar), false, GalleryPages::Default,
                     [smallAvatar](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintAvatar(&painter, rect, smallAvatar, state); });
    builder.row();
    builder.live(QStringLiteral("Live fact pill"), new pdf::PDFFactPill(QStringLiteral("github"), QStringLiteral("Open source"), page));
    pdf::PDFAvatar* liveAvatar = builder.live(QStringLiteral("Live avatar"), new pdf::PDFAvatar(QStringLiteral("JM"), 40, 14, page));
    liveAvatar->setAccessibleName(QStringLiteral("Jakub Melka"));
    builder.row();

    // Status row
    builder.block(QStringLiteral("Status row (PDFStatusRow) - priorities of Sponsor"), QStringLiteral("123:2869"));
    const Painter::StatusRow active{ QStringLiteral("The new application"), QStringLiteral("One window for every tool"), QStringLiteral("In progress"), true };
    const Painter::StatusRow planned{ QStringLiteral("Accessibility of documents"), QStringLiteral("Tagged PDF and PDF/UA"), QStringLiteral("Planned"), false };
    Helper::specimen(builder, QStringLiteral("Active"), QSize(400, Painter::getStatusRowSize(active).height()), true, GalleryPages::Default,
                     [active](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintStatusRow(&painter, rect, active, state); });
    Helper::specimen(builder, QStringLiteral("Planned"), QSize(400, Painter::getStatusRowSize(planned).height()), true, GalleryPages::Default,
                     [planned](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintStatusRow(&painter, rect, planned, state); });
    builder.row();
    Helper::specimen(builder, QStringLiteral("Right-to-left"), QSize(400, Painter::getStatusRowSize(active).height()), true, GalleryPages::Default,
                     [active](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintStatusRow(&painter, rect, active, state); }, QString(), Qt::RightToLeft);
    Helper::specimen(builder, QStringLiteral("Narrow (elided texts)"), Painter::getStatusRowMinimumSize(active), true, GalleryPages::Default,
                     [active](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintStatusRow(&painter, rect, active, state); });
    builder.row();
    QWidget* statusCard = Helper::createCard(page, 9, 0);
    statusCard->setFixedWidth(400);
    statusCard->layout()->addWidget(new pdf::PDFStatusRow(QStringLiteral("The new application"), QStringLiteral("One window for every tool"), QStringLiteral("In progress"), true, statusCard));
    statusCard->layout()->addWidget(new pdf::PDFStatusRow(QStringLiteral("Accessibility of documents"), QStringLiteral("Tagged PDF and PDF/UA"), QStringLiteral("Planned"), false, statusCard));
    builder.live(QStringLiteral("Live on a card"), statusCard);
    builder.row();

    // Tinted card
    builder.block(QStringLiteral("Tinted card (PDFTintedCard) - release card, hero cards, Thank you"), QStringLiteral("103:1951"));
    struct TintSample
    {
        const char* name;
        bool hasHue;
        pdf::PDFAccentHue hue;
    };
    static constexpr TintSample tints[] =
    {
        { "Blue", true, pdf::PDFAccentHue::Blue },
        { "Green", true, pdf::PDFAccentHue::Green },
        { "No hue (bg/surface)", false, pdf::PDFAccentHue::Blue }
    };
    for (const TintSample& tint : tints)
    {
        const std::optional<pdf::PDFAccentHue> hue = tint.hasHue ? std::optional<pdf::PDFAccentHue>(tint.hue) : std::nullopt;
        Helper::specimen(builder, QString::fromLatin1(tint.name), QSize(200, 80), false, GalleryPages::Default,
                         [hue](QPainter& painter, const QRectF& rect, const Painter::State& state) { Q_UNUSED(state); Painter::paintTintedCard(&painter, rect, hue); });
    }
    builder.row();
    pdf::PDFTintedCard* liveCard = builder.live(QStringLiteral("Live with content"), new pdf::PDFTintedCard(pdf::PDFAccentHue::Green, page));
    QHBoxLayout* liveCardLayout = new QHBoxLayout(liveCard);
    liveCardLayout->setContentsMargins(17, 17, 17, 17);
    liveCardLayout->setSpacing(12);
    pdf::PDFIconBadge* heart = new pdf::PDFIconBadge(QStringLiteral("heart"), 36, 18, liveCard);
    heart->setColors(Color::BgSurface, Color::AccentRedIcon);
    liveCardLayout->addWidget(heart);
    liveCardLayout->addWidget(Helper::label(QStringLiteral("Thank you for using PDF4QT"), Helper::Font::BodySemiBold, Color::TextPrimary, liveCard));
    builder.row();
}

QWidget* GalleryPages::createHomeSample(QWidget* parent)
{
    using Helper = GalleryCardPagesHelper;
    using Color = Helper::Color;
    using Font = Helper::Font;
    using Properties = Helper::Properties;

    QWidget* application = new QWidget(parent);
    application->setFixedWidth(1440);
    QVBoxLayout* applicationLayout = new QVBoxLayout(application);
    applicationLayout->setContentsMargins(0, 0, 0, 0);
    applicationLayout->setSpacing(0);

    // app-top-bar: the Home tab, the tabs of the documents, the new tab button.
    // The tab bar has the 6 px above the tabs of 32 px (its height is 38, the
    // focus ring of a tab needs the room), and each tab has the gap of 8 px
    // to the next one at its end (also the last one before the new tab button).
    QWidget* topBar = new QWidget(application);
    topBar->setProperty(Properties::DOCUMENT_TABS, true);
    topBar->setFixedHeight(38);
    QHBoxLayout* topBarLayout = new QHBoxLayout(topBar);
    topBarLayout->setContentsMargins(16, 0, 0, 0);
    topBarLayout->setSpacing(0);
    pdf::PDFDocumentTabBar* tabs = new pdf::PDFDocumentTabBar(topBar);
    tabs->setAccessibleName(QStringLiteral("Documents"));
    tabs->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    tabs->addTab(pdf::PDFThemeIcons::getIcon(QStringLiteral("home")), QStringLiteral("Home"));
    tabs->addTab(pdf::PDFThemeIcons::getIcon(QStringLiteral("file")), QStringLiteral("Project_Report.pdf"));
    tabs->addTab(pdf::PDFThemeIcons::getIcon(QStringLiteral("file")), QStringLiteral("Invoice.pdf"));
    tabs->setTabClosable(1, true);
    tabs->setTabClosable(2, true);
    tabs->setCurrentIndex(0);
    topBarLayout->addWidget(tabs, 0, Qt::AlignBottom);
    QToolButton* newTab = new QToolButton(topBar);
    newTab->setProperty(Properties::NEW_TAB, true);
    newTab->setIcon(pdf::PDFThemeIcons::getIcon(QStringLiteral("plus")));
    newTab->setAccessibleName(QStringLiteral("New tab"));
    newTab->setToolTip(QStringLiteral("New tab"));
    topBarLayout->addWidget(newTab, 0, Qt::AlignBottom);

    // The free part of the bar drags the window: the bar keeps the right
    // padding of 200 (16 + 46 free + the controls of 138), the window controls
    // touch the right edge of the window
    topBarLayout->addStretch(1);
    topBarLayout->addSpacing(16 + 46);
    pdf::PDFWindowControls* windowControls = new pdf::PDFWindowControls(pdf::PDFWindowControls::Platform::Windows, topBar);
    windowControls->setActivity(pdf::PDFWindowControls::Activity::Active);
    topBarLayout->addWidget(windowControls, 0, Qt::AlignTop);
    applicationLayout->addWidget(topBar);

    QWidget* body = new QWidget(application);
    QHBoxLayout* bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    applicationLayout->addWidget(body);

    // Sidebar: the tile of the application and its name, the navigation in three
    // groups, the theme toggle (padding 16 of the sidebar and 8 of its bottom area)
    QFrame* sidebar = new QFrame(body);
    sidebar->setProperty(Properties::PANEL, true);
    sidebar->setFixedWidth(210);
    QVBoxLayout* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(10, 20, 10, 24);
    sidebarLayout->setSpacing(0);
    QHBoxLayout* logoLayout = new QHBoxLayout();
    logoLayout->setContentsMargins(6, 8, 6, 16);
    logoLayout->setSpacing(10);
    logoLayout->addWidget(new GalleryCardPaintedWidget(QSize(36, 36), [](QPainter& painter, const QRectF& rect)
    {
        // logo-icon-bg: bg/accent, radius 8, icon/file-text 20 on the accent
        pdf::PDFStylePainter::fill(&painter, rect, pdf::PDFStylePainter::Radii(8.0), Color::BgAccent);
        pdf::PDFStylePainter::icon(&painter, pdf::PDFStylePainter::centered(rect, QSizeF(20.0, 20.0)), QStringLiteral("file-text"), Color::IconOnAccent);
    }, sidebar), 0, Qt::AlignVCenter);
    QVBoxLayout* logoTexts = new QVBoxLayout();
    logoTexts->setSpacing(2);
    logoTexts->addWidget(Helper::label(QStringLiteral("PDF4QT"), Font::TileTitle, Color::TextPrimary, sidebar));
    logoTexts->addWidget(Helper::label(QStringLiteral("Open. Edit. Do more."), Font::Tiny, Color::TextMuted, sidebar));
    logoLayout->addLayout(logoTexts, 1);
    sidebarLayout->addLayout(logoLayout);

    pdf::PDFNavList* navigation = new pdf::PDFNavList(pdf::PDFNavItem::Kind::Home, sidebar);
    navigation->setAccessibleName(QStringLiteral("Navigation"));
    navigation->addItem(QStringLiteral("Home"), QStringLiteral("home"));
    navigation->addItem(QStringLiteral("Open"), QStringLiteral("folder"));
    navigation->addItem(QStringLiteral("Recent"), QStringLiteral("clock"));
    navigation->addItem(QStringLiteral("Tools"), QStringLiteral("tools"));
    navigation->addItem(QStringLiteral("AI"), QStringLiteral("ai"));
    navigation->addGroup();
    navigation->addItem(QStringLiteral("What's New"), QStringLiteral("gift"));
    navigation->addItem(QStringLiteral("About"), QStringLiteral("info"));
    navigation->addItem(QStringLiteral("Support"), QStringLiteral("headphones"));
    navigation->addItem(QStringLiteral("Sponsor"), QStringLiteral("heart"));
    navigation->addGroup();
    navigation->addItem(QStringLiteral("Settings"), QStringLiteral("settings"));
    sidebarLayout->addWidget(navigation);
    sidebarLayout->addStretch(1);
    pdf::PDFSegmentedControl* themeToggle = new pdf::PDFSegmentedControl(sidebar);
    themeToggle->setAccessibleName(QStringLiteral("Theme"));
    const int lightSegment = themeToggle->addSegment(QStringLiteral("Light"), QStringLiteral("sun"));
    themeToggle->addSegment(QStringLiteral("Dark"), QStringLiteral("moon"));
    const int autoSegment = themeToggle->addSegment(QStringLiteral("Auto"), QStringLiteral("monitor"));
    themeToggle->setSegmentIconColor(lightSegment, Color::IconSun);
    themeToggle->setCurrentIndex(autoSegment);
    themeToggle->setFixedWidth(182);
    sidebarLayout->addWidget(themeToggle, 0, Qt::AlignHCenter);
    bodyLayout->addWidget(sidebar);

    // Workspace: header, quick actions, recent files, learn & support, recent tools, footer
    QWidget* workspace = new QWidget(body);
    workspace->setProperty(Properties::CANVAS, true);
    QVBoxLayout* workspaceLayout = new QVBoxLayout(workspace);
    workspaceLayout->setContentsMargins(32, 32, 32, 32);
    workspaceLayout->setSpacing(24);
    bodyLayout->addWidget(workspace, 1);

    // workspace-header 55 px: the title (Extra Bold 28 in the design, the
    // library has Bold 22, review I-35) in its box of 34 px, the subtitle
    // text/tertiary with "PDF" Semi Bold text/accent (14 px in the design, 13 here)
    QWidget* header = new QWidget(workspace);
    header->setFixedHeight(55);
    QHBoxLayout* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    QVBoxLayout* welcomeLayout = new QVBoxLayout();
    welcomeLayout->setContentsMargins(0, 0, 0, 0);
    welcomeLayout->setSpacing(4);
    pdf::PDFLabel* welcome = Helper::label(QStringLiteral("Welcome to PDF4QT"), Font::Display, Color::TextPrimary, header);
    welcome->setFixedHeight(34);
    welcomeLayout->addWidget(welcome);
    welcomeLayout->addWidget(Helper::textRuns({ { QStringLiteral("A fast, modern and open-source "), Font::Body, Color::TextTertiary },
                                                { QStringLiteral("PDF"), Font::BodySemiBold, Color::TextAccent },
                                                { QStringLiteral(" editor for Windows, Linux and macOS."), Font::Body, Color::TextTertiary } }, 17, header));
    welcomeLayout->addStretch(1);
    headerLayout->addLayout(welcomeLayout, 1);
    pdf::PDFLineEdit* search = new pdf::PDFLineEdit(header);
    search->setSearchField();
    search->setPlaceholderText(QStringLiteral("Search tools, documents, or help..."));
    search->setFixedWidth(320);
    headerLayout->addWidget(search, 0, Qt::AlignVCenter);
    workspaceLayout->addWidget(header);

    // Quick actions: a grid of five columns of tiles 88 px high, gaps of 12
    // (the title is Bold 16 in the design, the library has Bold 15, review I-35)
    QVBoxLayout* actionsLayout = new QVBoxLayout();
    actionsLayout->setSpacing(12);
    actionsLayout->addWidget(Helper::label(QStringLiteral("Quick Actions"), Font::Title, Color::TextPrimary, workspace));
    QGridLayout* tiles = new QGridLayout();
    tiles->setHorizontalSpacing(12);
    tiles->setVerticalSpacing(12);
    struct Tile
    {
        const char* title;
        const char* description;
        const char* icon;
        pdf::PDFAccentHue hue;
    };
    static constexpr Tile homeTiles[] =
    {
        { "Open PDF", "Browse and open a document", "folder-open", pdf::PDFAccentHue::Blue },
        { "Images to PDF", "Turn photos and scans into a PDF", "image", pdf::PDFAccentHue::Blue },
        { "PDF to Images", "Save pages as PNG, JPEG or TIFF", "download", pdf::PDFAccentHue::Blue },
        { "Recognize Text (OCR)", "Make scanned pages searchable", "ocr", pdf::PDFAccentHue::Blue },
        { "Edit Content", "Edit or add text, images and graphics", "edit", pdf::PDFAccentHue::Green },
        { "Merge", "Combine several files into one PDF", "layers", pdf::PDFAccentHue::Amber },
        { "Manage Pages", "Reorder, group and arrange pages", "grid", pdf::PDFAccentHue::Amber },
        { "Compress", "Reduce the file size in one step", "minimize-2", pdf::PDFAccentHue::Violet },
        { "Protect with Password", "Require a password to open the PDF", "lock", pdf::PDFAccentHue::Red },
        { "Sign with Certificate", "Add a digital signature", "sign-certificate", pdf::PDFAccentHue::Red },
        { "Remove Hidden Data", "Strip metadata", "hidden-data", pdf::PDFAccentHue::Red },
        { "Measure", "Distances, areas, perimeters, angles", "ruler", pdf::PDFAccentHue::Teal },
        { "All Tools", "View complete catalog", "tools", pdf::PDFAccentHue::Neutral }
    };
    int tileIndex = 0;
    for (const Tile& tile : homeTiles)
    {
        tiles->addWidget(new pdf::PDFQuickActionTile(QString::fromLatin1(tile.title), QString::fromLatin1(tile.description), QString::fromLatin1(tile.icon), tile.hue, workspace), tileIndex / 5, tileIndex % 5);
        ++tileIndex;
    }
    actionsLayout->addLayout(tiles);
    workspaceLayout->addLayout(actionsLayout);

    // Three columns: Recent Files (397), Learn & Resources with Support (360), Recent Tools (361)
    QHBoxLayout* columns = new QHBoxLayout();
    columns->setSpacing(24);
    workspaceLayout->addLayout(columns);

    auto sectionHeader = [workspace](const QString& title, const QString& link)
    {
        QHBoxLayout* layout = new QHBoxLayout();
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(Helper::label(title, Font::Title, Color::TextPrimary, workspace));
        layout->addStretch(1);
        if (!link.isEmpty())
        {
            layout->addWidget(new pdf::PDFTextLink(link, workspace));
        }
        return layout;
    };

    QVBoxLayout* recentFiles = new QVBoxLayout();
    recentFiles->setSpacing(12);
    recentFiles->addLayout(sectionHeader(QStringLiteral("Recent Files"), QStringLiteral("View all")));
    QWidget* filesCard = Helper::createCard(workspace, 8, 0);
    struct File
    {
        const char* name;
        const char* path;
        const char* time;
    };
    static constexpr File homeFiles[] =
    {
        { "Project_Report.pdf", "C:\\Users\\Jakub\\Documents", "Today 10:24" },
        { "Invoice_2025.pdf", "C:\\Users\\Jakub\\Downloads", "Today 08:17" },
        { "Technical_Documentation.pdf", "D:\\Projects\\PDF4QT\\Docs", "Yesterday 18:02" },
        { "Building_Plan.pdf", "C:\\Users\\Jakub\\Desktop", "Sep 18, 2026" },
        { "Presentation.pdf", "C:\\Users\\Jakub\\Documents", "Sep 15, 2026" }
    };
    for (const File& file : homeFiles)
    {
        pdf::PDFRecentFileRow* row = new pdf::PDFRecentFileRow(QString::fromLatin1(file.name), QString::fromLatin1(file.path), QString::fromLatin1(file.time), filesCard);
        row->setStarVisible(false);
        filesCard->layout()->addWidget(row);
    }
    // drop-zone (not a component): bg/subtle, border/default 1 px dashed 4 / 3,
    // radius 8, icon/download 14, gap 8, Medium 12 text/tertiary, 43 px high
    const QString dropText = QStringLiteral("Drag and drop a PDF file here to open it");
    QWidget* dropZone = new GalleryCardPaintedWidget(QSize(381, 43), [dropText](QPainter& painter, const QRectF& rect)
    {
        const pdf::PDFStylePainter::Radii radii(8.0);
        pdf::PDFStylePainter::fill(&painter, rect, radii, Color::BgSubtle);
        QPen pen(pdf::PDFStylePainter::color(Color::BorderDefault), 1.0, Qt::CustomDashLine);
        pen.setDashPattern({ 4.0, 3.0 });
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(pdf::PDFStylePainter::roundedPath(rect.adjusted(0.5, 0.5, -0.5, -0.5), pdf::PDFStylePainter::Radii(7.5)));

        const QFont font = pdf::PDFThemeFonts::getFont(Font::SmallMedium);
        const qreal textWidth = pdf::PDFStylePainter::textWidth(dropText, font);
        const qreal left = rect.left() + qFloor((rect.width() - 14.0 - 8.0 - textWidth) / 2.0);
        pdf::PDFStylePainter::icon(&painter, QRectF(left, rect.top() + qFloor((rect.height() - 14.0) / 2.0), 14.0, 14.0), QStringLiteral("download"), Color::IconDefault);
        pdf::PDFStylePainter::text(&painter, QRectF(left + 22.0, rect.top(), textWidth + 1.0, rect.height()), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                                   dropText, font, pdf::PDFStylePainter::color(Color::TextTertiary));
    }, filesCard);
    dropZone->setFixedHeight(43);
    filesCard->layout()->addWidget(dropZone);
    static_cast<QVBoxLayout*>(filesCard->layout())->addStretch(1);
    filesCard->setFixedWidth(397);
    recentFiles->addWidget(filesCard, 1);
    columns->addLayout(recentFiles);

    QVBoxLayout* learnAndSupport = new QVBoxLayout();
    learnAndSupport->setContentsMargins(0, 0, 0, 0);
    learnAndSupport->setSpacing(12);
    learnAndSupport->addLayout(sectionHeader(QStringLiteral("Learn & Resources"), QStringLiteral("View all resources")));
    QHBoxLayout* resources = new QHBoxLayout();
    resources->setSpacing(12);
    resources->addWidget(new pdf::PDFResourceCard(QStringLiteral("Video Tutorials"), QStringLiteral("Watch step-by-step guides"), QStringLiteral("play-circle"), pdf::PDFAccentHue::Violet, workspace));
    resources->addWidget(new pdf::PDFResourceCard(QStringLiteral("User Manual"), QStringLiteral("Detailed documentation"), QStringLiteral("book-open"), pdf::PDFAccentHue::Green, workspace));
    resources->addWidget(new pdf::PDFResourceCard(QStringLiteral("Getting Started"), QStringLiteral("New to PDF4QT? Start here"), QStringLiteral("help-circle"), pdf::PDFAccentHue::Amber, workspace));
    learnAndSupport->addLayout(resources);
    learnAndSupport->addSpacing(8);
    learnAndSupport->addLayout(sectionHeader(QStringLiteral("Support"), QString()));
    QWidget* supportCard = Helper::createCard(workspace, 6, 0);
    pdf::PDFLinkRow* reportIssue = new pdf::PDFLinkRow(QStringLiteral("Report Issue"), QStringLiteral("Tell us about a problem"), QStringLiteral("alert-circle"), supportCard);
    reportIssue->setIconColor(Color::IconDanger);
    supportCard->layout()->addWidget(reportIssue);

    // The rows of the support list are separated by lines border/divider
    auto divider = [supportCard]()
    {
        QFrame* frame = new QFrame(supportCard);
        frame->setFrameShape(QFrame::HLine);
        frame->setFixedHeight(1);
        return frame;
    };
    supportCard->layout()->addWidget(divider());
    supportCard->layout()->addWidget(new pdf::PDFLinkRow(QStringLiteral("Discuss on GitHub"), QStringLiteral("Join the community"), QStringLiteral("github"), supportCard));
    supportCard->layout()->addWidget(divider());
    pdf::PDFLinkRow* sponsor = new pdf::PDFLinkRow(QStringLiteral("Become a Sponsor"), QStringLiteral("Support the development of PDF4QT"), QStringLiteral("heart"), supportCard);
    sponsor->setIconColor(Color::IconDanger);
    supportCard->layout()->addWidget(sponsor);
    learnAndSupport->addWidget(supportCard);
    learnAndSupport->addStretch(1);
    QWidget* learnColumn = new QWidget(workspace);
    learnColumn->setLayout(learnAndSupport);
    learnColumn->setFixedWidth(360);
    columns->addWidget(learnColumn);

    QVBoxLayout* recentTools = new QVBoxLayout();
    recentTools->setSpacing(12);
    recentTools->addLayout(sectionHeader(QStringLiteral("Recent Tools"), QString()));
    QWidget* toolsCard = Helper::createCard(workspace, 8, 2);
    struct RecentTool
    {
        const char* name;
        const char* time;
        const char* icon;
        pdf::PDFAccentHue hue;
    };
    static constexpr RecentTool homeTools[] =
    {
        { "Compress", "Today 11:02", "minimize-2", pdf::PDFAccentHue::Violet },
        { "Sign with Certificate", "Today 09:40", "sign-certificate", pdf::PDFAccentHue::Red },
        { "Manage Pages", "Yesterday 16:15", "grid", pdf::PDFAccentHue::Amber },
        { "Recognize Text (OCR)", "Sep 18, 2026", "ocr", pdf::PDFAccentHue::Blue },
        { "PDF to Images", "Sep 15, 2026", "download", pdf::PDFAccentHue::Blue }
    };
    for (const RecentTool& tool : homeTools)
    {
        toolsCard->layout()->addWidget(new pdf::PDFRecentToolRow(QString::fromLatin1(tool.name), QString::fromLatin1(tool.time), QString::fromLatin1(tool.icon), tool.hue, toolsCard));
    }
    static_cast<QVBoxLayout*>(toolsCard->layout())->addStretch(1);
    recentTools->addWidget(toolsCard, 1);
    columns->addLayout(recentTools, 1);

    // app-footer: the top border border/default and the padding 16; one line
    // Regular 12 text/tertiary with "MIT License" Semi Bold 12 text/accent (a
    // text of the line, not a Text link), the targets Semi Bold 11 text/tertiary
    // with gaps of 16 (the logos of Windows and macOS are drawings of the frame,
    // not icons of the library, review I-35; they are left out with the one of Linux)
    QVBoxLayout* footerLayout = new QVBoxLayout();
    footerLayout->setContentsMargins(0, 0, 0, 0);
    footerLayout->setSpacing(16);
    footerLayout->addWidget(Helper::line(Color::BorderDefault, workspace));
    QHBoxLayout* footer = new QHBoxLayout();
    footer->setContentsMargins(0, 0, 0, 0);
    footer->setSpacing(16);
    footer->addWidget(Helper::textRuns({ { QStringLiteral("Free • Open Source • "), Font::Small, Color::TextTertiary },
                                         { QStringLiteral("MIT License"), Font::SmallSemiBold, Color::TextAccent },
                                         { QStringLiteral(" • Version 1.5.3"), Font::Small, Color::TextTertiary } }, 15, workspace));
    footer->addStretch(1);
    for (const char* target : { "Windows", "Linux", "macOS" })
    {
        footer->addWidget(Helper::label(QString::fromLatin1(target), Font::CaptionSemiBold, Color::TextTertiary, workspace));
    }
    footerLayout->addLayout(footer);
    workspaceLayout->addLayout(footerLayout);

    return application;
}

void GalleryPages::addCatalogCards(GalleryBuilder& builder)
{
    using Helper = GalleryCardPagesHelper;
    using Painter = pdf::PDFComponentPainter;

    QWidget* page = builder.page();

    // Tool category header: Hue (no states), the header of a card of the Tools screen
    builder.block(QStringLiteral("Tool category header (PDFToolCategoryHeader) - Hue, header of a bg/surface card"), QStringLiteral("89:1424"));
    static constexpr Helper::HueNodes headerHues[] =
    {
        { "Blue", pdf::PDFAccentHue::Blue, { "89:1368", nullptr, nullptr, nullptr } },
        { "Violet", pdf::PDFAccentHue::Violet, { "89:1376", nullptr, nullptr, nullptr } },
        { "Amber", pdf::PDFAccentHue::Amber, { "89:1384", nullptr, nullptr, nullptr } },
        { "Red", pdf::PDFAccentHue::Red, { "89:1392", nullptr, nullptr, nullptr } },
        { "Teal", pdf::PDFAccentHue::Teal, { "89:1400", nullptr, nullptr, nullptr } },
        { "Green", pdf::PDFAccentHue::Green, { "89:1408", nullptr, nullptr, nullptr } },
        { "Neutral", pdf::PDFAccentHue::Neutral, { "89:1416", nullptr, nullptr, nullptr } }
    };
    int headerCount = 0;
    for (const Helper::HueNodes& hue : headerHues)
    {
        const Painter::ToolCategoryHeader header{ QStringLiteral("Pages"), QStringLiteral("Merge, split, reorder and rotate pages"), QStringLiteral("6 tools"), QStringLiteral("grid"), hue.hue };
        Helper::specimen(builder, QString::fromLatin1(hue.name), Painter::getToolCategoryHeaderSize(header), true, GalleryPages::Default,
                         [header](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintToolCategoryHeader(&painter, rect, header, state); }, QString::fromLatin1(hue.nodes[0]));
        if (++headerCount % 3 == 0)
        {
            builder.row();
        }
    }
    const Painter::ToolCategoryHeader disabledHeader{ QStringLiteral("Security"), QStringLiteral("Passwords, signatures and redaction"), QStringLiteral("8 tools"), QStringLiteral("lock"), pdf::PDFAccentHue::Red };
    Helper::specimen(builder, QStringLiteral("Disabled (O-08)"), Painter::getToolCategoryHeaderSize(disabledHeader), true, GalleryPages::Disabled,
                     [disabledHeader](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintToolCategoryHeader(&painter, rect, disabledHeader, state); });
    Helper::specimen(builder, QStringLiteral("Right-to-left"), Painter::getToolCategoryHeaderSize(disabledHeader), true, GalleryPages::Default,
                     [disabledHeader](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintToolCategoryHeader(&painter, rect, disabledHeader, state); }, QString(), Qt::RightToLeft);
    builder.row();

    // A category card: the header and the tool rows below it
    QWidget* category = Helper::createCard(page, 1, 0);
    category->setFixedWidth(393);
    pdf::PDFToolCategoryHeader* categoryHeader = new pdf::PDFToolCategoryHeader(QStringLiteral("Edit & Forms"), QStringLiteral("Edit content, fill and create forms"), QStringLiteral("2 tools"), QStringLiteral("edit"), pdf::PDFAccentHue::Green, category);
    categoryHeader->setTopRadius(11.0);
    category->layout()->addWidget(categoryHeader);
    QWidget* categoryRows = new QWidget(category);
    QVBoxLayout* categoryRowsLayout = new QVBoxLayout(categoryRows);
    categoryRowsLayout->setContentsMargins(6, 6, 6, 6);
    categoryRowsLayout->setSpacing(0);
    categoryRowsLayout->addWidget(new pdf::PDFToolRow(QStringLiteral("Edit Content"), QStringLiteral("Edit or add text, images and graphics"), QStringLiteral("edit"), pdf::PDFAccentHue::Green, categoryRows));
    categoryRowsLayout->addWidget(new pdf::PDFToolRow(QStringLiteral("Add Page Numbers"), QStringLiteral("Number the pages of the document"), QStringLiteral("grid"), pdf::PDFAccentHue::Green, categoryRows));
    category->layout()->addWidget(categoryRows);
    builder.live(QStringLiteral("Live category card"), category);
    pdf::PDFToolCategoryHeader* rightToLeftHeader = new pdf::PDFToolCategoryHeader(QStringLiteral("Optimize"), QStringLiteral("Compress and clean up documents"), QStringLiteral("4 tools"), QStringLiteral("compress-auto"), pdf::PDFAccentHue::Violet, page);
    rightToLeftHeader->setLayoutDirection(Qt::RightToLeft);
    rightToLeftHeader->setFixedWidth(393);
    builder.live(QStringLiteral("Live right-to-left"), rightToLeftHeader);
    builder.row();

    // Tool choice: Selected x State, in a dialog (bg/surface)
    builder.block(QStringLiteral("Tool choice (PDFToolChoice) - Selected x State, deselectable exclusive group"), QStringLiteral("417:1006"));
    static constexpr const char* unselectedChoiceNodes[4] = { "417:994", "706:146750", "706:146755", "706:146760" };
    static constexpr const char* selectedChoiceNodes[4] = { "417:999", "706:146766", "706:146773", "706:146780" };
    const Painter::ToolChoice choice{ QStringLiteral("Add Page Numbers"), QStringLiteral("grid"), pdf::PDFAccentHue::Blue };
    auto paintChoice = [choice](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintToolChoice(&painter, rect, choice, state); };
    Helper::stateSpecimens(builder, QStringLiteral("Unselected"), Painter::getToolChoiceSize(choice), unselectedChoiceNodes, true, 0, paintChoice);
    Helper::stateSpecimens(builder, QStringLiteral("Selected"), Painter::getToolChoiceSize(choice), selectedChoiceNodes, true, GalleryPages::On, paintChoice);
    const Painter::ToolChoice shortChoice{ QStringLiteral("Compress"), QStringLiteral("compress-auto"), pdf::PDFAccentHue::Violet };
    Helper::specimen(builder, QStringLiteral("One line"), Painter::getToolChoiceSize(shortChoice), true, GalleryPages::Default,
                     [shortChoice](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintToolChoice(&painter, rect, shortChoice, state); });
    const Painter::ToolChoice longChoice{ QStringLiteral("Recognize Text in Scanned Pages (OCR)"), QStringLiteral("ocr"), pdf::PDFAccentHue::Blue };
    Helper::specimen(builder, QStringLiteral("Long name, two lines elided"), Painter::getToolChoiceSize(longChoice), true, GalleryPages::On,
                     [longChoice](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintToolChoice(&painter, rect, longChoice, state); });
    Helper::specimen(builder, QStringLiteral("Selected, right-to-left"), Painter::getToolChoiceSize(choice), true, GalleryPages::On, paintChoice, QString(), Qt::RightToLeft);
    builder.row();

    // Live: three suggested tools in a deselectable exclusive group (as in Continue with)
    for (const bool enabled : { true, false })
    {
        QWidget* dialog = Helper::createCard(page, 12, 8, Qt::Horizontal);
        QButtonGroup* group = new QButtonGroup(dialog);
        pdf::PDFToolChoice* numbers = new pdf::PDFToolChoice(QStringLiteral("Add Page Numbers"), QStringLiteral("grid"), pdf::PDFAccentHue::Green, dialog);
        pdf::PDFToolChoice* compress = new pdf::PDFToolChoice(QStringLiteral("Compress"), QStringLiteral("compress-auto"), pdf::PDFAccentHue::Violet, dialog);
        pdf::PDFToolChoice* sign = new pdf::PDFToolChoice(QStringLiteral("Sign with Certificate"), QStringLiteral("sign-certificate"), pdf::PDFAccentHue::Red, dialog);
        for (pdf::PDFToolChoice* toolChoice : { numbers, compress, sign })
        {
            group->addButton(toolChoice);
            dialog->layout()->addWidget(toolChoice);
        }
        compress->setSelected(true);
        dialog->setEnabled(enabled);
        builder.live(enabled ? QStringLiteral("Live group, Compress selected") : QStringLiteral("Live group, disabled"), dialog);
    }
    QWidget* hoverDialog = Helper::createCard(page, 12, 8, Qt::Horizontal);
    pdf::PDFToolChoice* hoveredChoice = new pdf::PDFToolChoice(QStringLiteral("Add Page Numbers"), QStringLiteral("grid"), pdf::PDFAccentHue::Green, hoverDialog);
    Helper::setState(hoveredChoice, GalleryPages::Hover);
    pdf::PDFToolChoice* pressedChoice = new pdf::PDFToolChoice(QStringLiteral("Compress"), QStringLiteral("compress-auto"), pdf::PDFAccentHue::Violet, hoverDialog);
    pressedChoice->setSelected(true);
    Helper::setState(pressedChoice, GalleryPages::Hover);
    hoverDialog->layout()->addWidget(hoveredChoice);
    hoverDialog->layout()->addWidget(pressedChoice);
    builder.live(QStringLiteral("Live hover, selected hover"), hoverDialog);
    builder.row();

    // Resource card: Hue x State, the cards of a row share the width and the height
    builder.block(QStringLiteral("Resource card (PDFResourceCard) - Hue x State, on bg/canvas"), QStringLiteral("50:242"));
    static constexpr Helper::HueNodes resourceHues[] =
    {
        { "Neutral", pdf::PDFAccentHue::Neutral, { "50:163", "50:165", "50:172", "50:179" } },
        { "Green", pdf::PDFAccentHue::Green, { "50:186", "50:193", "50:200", "50:207" } },
        { "Amber", pdf::PDFAccentHue::Amber, { "50:214", "50:221", "50:228", "50:235" } },
        { "Violet", pdf::PDFAccentHue::Violet, { "773:147242", "773:147249", "773:147256", "773:147263" } }
    };
    for (const Helper::HueNodes& hue : resourceHues)
    {
        const Painter::ResourceCard card{ QStringLiteral("User Manual"), QStringLiteral("Detailed documentation"), QStringLiteral("book-open"), hue.hue };
        Helper::stateSpecimens(builder, QString::fromLatin1(hue.name), Painter::getResourceCardSize(card), hue.nodes, false, 0,
                               [card](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintResourceCard(&painter, rect, card, state); });
    }
    const Painter::ResourceCard videoResource{ QStringLiteral("Video Tutorials"), QStringLiteral("Watch step-by-step guides"), QStringLiteral("play-circle"), pdf::PDFAccentHue::Violet };
    Helper::specimen(builder, QStringLiteral("112 wide (Home)"), QSize(112, Painter::getResourceCardHeight(videoResource, 112)), false, GalleryPages::Default,
                     [videoResource](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintResourceCard(&painter, rect, videoResource, state); });
    Helper::specimen(builder, QStringLiteral("Right-to-left"), Painter::getResourceCardSize(videoResource), false, GalleryPages::Default,
                     [videoResource](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintResourceCard(&painter, rect, videoResource, state); }, QString(), Qt::RightToLeft);
    builder.row();
    QWidget* resourceRow = new QWidget(page);
    QHBoxLayout* resourceRowLayout = new QHBoxLayout(resourceRow);
    resourceRowLayout->setContentsMargins(0, 0, 0, 0);
    resourceRowLayout->setSpacing(12);
    resourceRowLayout->addWidget(new pdf::PDFResourceCard(QStringLiteral("Video Tutorials"), QStringLiteral("Watch step-by-step guides"), QStringLiteral("play-circle"), pdf::PDFAccentHue::Violet, resourceRow));
    resourceRowLayout->addWidget(new pdf::PDFResourceCard(QStringLiteral("User Manual"), QStringLiteral("Detailed documentation"), QStringLiteral("book-open"), pdf::PDFAccentHue::Green, resourceRow));
    resourceRowLayout->addWidget(new pdf::PDFResourceCard(QStringLiteral("Getting Started"), QStringLiteral("New to PDF4QT? Start here"), QStringLiteral("help-circle"), pdf::PDFAccentHue::Amber, resourceRow));
    resourceRow->setFixedWidth(360);
    builder.live(QStringLiteral("Live row of three (equal height)"), resourceRow);
    builder.row();
    Helper::liveStates(builder, false, [page]()
    {
        pdf::PDFResourceCard* card = new pdf::PDFResourceCard(QStringLiteral("User Manual"), QStringLiteral("Detailed documentation"), QStringLiteral("book-open"), pdf::PDFAccentHue::Neutral, page);
        card->setFixedWidth(116);
        return card;
    });

    // Highlight card: Hue (no states)
    builder.block(QStringLiteral("Highlight card (PDFHighlightCard) - Hue, not interactive, on bg/canvas"), QStringLiteral("103:1894"));
    static constexpr Helper::HueNodes highlightHues[] =
    {
        { "Blue", pdf::PDFAccentHue::Blue, { "103:1845", nullptr, nullptr, nullptr } },
        { "Violet", pdf::PDFAccentHue::Violet, { "103:1852", nullptr, nullptr, nullptr } },
        { "Amber", pdf::PDFAccentHue::Amber, { "103:1859", nullptr, nullptr, nullptr } },
        { "Red", pdf::PDFAccentHue::Red, { "103:1866", nullptr, nullptr, nullptr } },
        { "Teal", pdf::PDFAccentHue::Teal, { "103:1873", nullptr, nullptr, nullptr } },
        { "Green", pdf::PDFAccentHue::Green, { "103:1880", nullptr, nullptr, nullptr } },
        { "Neutral", pdf::PDFAccentHue::Neutral, { "103:1887", nullptr, nullptr, nullptr } }
    };
    int highlightCount = 0;
    for (const Helper::HueNodes& hue : highlightHues)
    {
        const Painter::HighlightCard card{ QStringLiteral("One app for everything"), QStringLiteral("Editor, Viewer, PageMaster and Diff are now a single application."), QStringLiteral("grid"), hue.hue };
        Helper::specimen(builder, QString::fromLatin1(hue.name), Painter::getHighlightCardSize(card), false, GalleryPages::Default,
                         [card](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintHighlightCard(&painter, rect, card, state); }, QString::fromLatin1(hue.nodes[0]));
        if (++highlightCount % 4 == 0)
        {
            builder.row();
        }
    }
    const Painter::HighlightCard disabledHighlight{ QStringLiteral("Timestamped signatures"), QStringLiteral("Signatures and documents get a trusted timestamp of an RFC 3161 authority."), QStringLiteral("clock"), pdf::PDFAccentHue::Red };
    Helper::specimen(builder, QStringLiteral("Disabled (O-08)"), Painter::getHighlightCardSize(disabledHighlight), false, GalleryPages::Disabled,
                     [disabledHighlight](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintHighlightCard(&painter, rect, disabledHighlight, state); });
    builder.row();
    for (const bool rightToLeft : { false, true })
    {
        pdf::PDFHighlightCard* highlight = new pdf::PDFHighlightCard(QStringLiteral("Faster loading"), QStringLiteral("Large documents open up to three times faster thanks to the parallel parsing of the objects of the document."),
                                                                     QStringLiteral("zap"), pdf::PDFAccentHue::Amber, page);
        highlight->setFixedWidth(259);
        highlight->setLayoutDirection(rightToLeft ? Qt::RightToLeft : Qt::LeftToRight);
        builder.live(rightToLeft ? QStringLiteral("Live right-to-left") : QStringLiteral("Live, three lines"), highlight);
    }
    builder.row();

    // Video card: State, on the bg/surface panel of the Support screen
    builder.block(QStringLiteral("Video card (PDFVideoCard) - State, on bg/surface"), QStringLiteral("116:2418"));
    static constexpr const char* videoNodes[4] = { "116:2354", "116:2370", "116:2386", "116:2402" };
    Painter::VideoCard video;
    video.title = QStringLiteral("Getting started with PDF4QT");
    video.description = QStringLiteral("A quick tour of the interface and the main features.");
    video.duration = QStringLiteral("2:45");
    video.tags = QStringList{ QStringLiteral("OCR"), QStringLiteral("Basics") };
    Helper::stateSpecimens(builder, QString(), Painter::getVideoCardSize(video), videoNodes, true, 0,
                           [video](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintVideoCard(&painter, rect, video, state); });
    Painter::VideoCard pictureVideo = video;
    pictureVideo.pixmap = Helper::createVideoPicture();
    pictureVideo.title = QStringLiteral("Signing documents with a certificate and a timestamp");
    pictureVideo.description = QStringLiteral("How to sign a document, add a timestamp of an authority and check the signatures of other people.");
    pictureVideo.duration = QStringLiteral("12:08");
    pictureVideo.tags = QStringList{ QStringLiteral("Signatures"), QString(), QStringLiteral("Security"), QStringLiteral("Certificates"), QStringLiteral("Advanced") };
    Helper::specimen(builder, QStringLiteral("Picture, long texts, empty and hidden tags"), Painter::getVideoCardSize(pictureVideo), true, GalleryPages::Default,
                     [pictureVideo](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintVideoCard(&painter, rect, pictureVideo, state); });
    Painter::VideoCard oneTagVideo = video;
    oneTagVideo.tags = QStringList{ QStringLiteral("OCR") };
    Helper::specimen(builder, QStringLiteral("Show tag 2 false (one tag)"), Painter::getVideoCardSize(oneTagVideo), true, GalleryPages::Default,
                     [oneTagVideo](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintVideoCard(&painter, rect, oneTagVideo, state); });
    Painter::VideoCard shortVideo = video;
    shortVideo.description = QStringLiteral("Open a document.");
    shortVideo.tags.clear();
    Helper::specimen(builder, QStringLiteral("One line, no tags"), Painter::getVideoCardSize(shortVideo), true, GalleryPages::Default,
                     [shortVideo](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintVideoCard(&painter, rect, shortVideo, state); });
    Helper::specimen(builder, QStringLiteral("Right-to-left"), Painter::getVideoCardSize(video), true, GalleryPages::Default,
                     [video](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintVideoCard(&painter, rect, video, state); }, QString(), Qt::RightToLeft);
    builder.row();
    const QPixmap videoPicture = Helper::createVideoPicture();
    Helper::liveStates(builder, true, [page, videoPicture]()
    {
        pdf::PDFVideoCard* card = new pdf::PDFVideoCard(QStringLiteral("Comparing two documents"), QStringLiteral("Find the differences of two versions of a contract, page by page."),
                                                        QStringLiteral("4:12"), QStringList{ QStringLiteral("Compare"), QStringLiteral("Review") }, page);
        card->setPixmap(videoPicture);
        card->setFixedWidth(267);
        return card;
    });

    // Logo
    builder.block(QStringLiteral("Logo / PDF4QT (SVG in the resources of the library, PDFPreviewPainter::paintLogo)"), QStringLiteral("110:2154"));
    for (const int extent : { 130, 64, 32 })
    {
        builder.specimen(QStringLiteral("%1 px").arg(extent), QSize(extent, extent), [](QPainter& painter, const QRect& rect)
        {
            pdf::PDFPreviewPainter::paintLogo(&painter, QRectF(rect));
        }, QStringLiteral("110:2154"));
    }
    builder.row();
}

void GalleryPages::addPanelsCards(GalleryBuilder& builder)
{
    using Helper = GalleryCardPagesHelper;
    using Painter = pdf::PDFComponentPainter;
    using Color = Helper::Color;
    using Preview = Helper::Preview;

    QWidget* page = builder.page();

    // Reading theme card: Selected x State, on the bg/surface card of the settings
    builder.block(QStringLiteral("Reading theme card (PDFReadingThemeCard) - Selected x State, on bg/surface"), QStringLiteral("156:4218"));
    static constexpr const char* unselectedReadingNodes[4] = { "156:4146", "156:4154", "156:4162", "156:4170" };
    static constexpr const char* selectedReadingNodes[4] = { "156:4178", "156:4188", "156:4198", "156:4208" };
    Painter::ReadingThemeCard paper;
    paper.name = QStringLiteral("Paper");
    auto paintReading = [paper](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintReadingThemeCard(&painter, rect, paper, state); };
    Helper::stateSpecimens(builder, QStringLiteral("Unselected"), Painter::getReadingThemeCardSize(paper), unselectedReadingNodes, true, 0, paintReading);
    Helper::stateSpecimens(builder, QStringLiteral("Selected"), Painter::getReadingThemeCardSize(paper), selectedReadingNodes, true, GalleryPages::On, paintReading);
    Painter::ReadingThemeCard custom;
    custom.name = QStringLiteral("My colors");
    custom.customPageColor = QColor(QStringLiteral("#2b1f3a"));
    custom.customTextColor = QColor(QStringLiteral("#f4d6ff"));
    Helper::specimen(builder, QStringLiteral("Custom colors of the user"), Painter::getReadingThemeCardSize(custom), true, GalleryPages::Default,
                     [custom](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintReadingThemeCard(&painter, rect, custom, state); });
    Helper::specimen(builder, QStringLiteral("Selected, right-to-left"), Painter::getReadingThemeCardSize(paper), true, GalleryPages::On, paintReading, QString(), Qt::RightToLeft);
    builder.row();

    struct ReadingTheme
    {
        const char* name;
        Color page;
        Color text;
    };
    static constexpr ReadingTheme readingThemes[] =
    {
        { "Paper", Color::ReadingPaperPage, Color::ReadingPaperText }, { "Sepia", Color::ReadingSepiaPage, Color::ReadingSepiaText },
        { "Soft gray", Color::ReadingSoftGrayPage, Color::ReadingSoftGrayText }, { "Mint", Color::ReadingMintPage, Color::ReadingMintText },
        { "Night", Color::ReadingNightPage, Color::ReadingNightText }, { "Black", Color::ReadingBlackPage, Color::ReadingBlackText },
        { "Terminal", Color::ReadingTerminalPage, Color::ReadingTerminalText }, { "Amber", Color::ReadingAmberPage, Color::ReadingAmberText },
        { "High contrast", Color::ReadingHighContrastPage, Color::ReadingHighContrastText }, { "Custom", Color::ReadingCustomPage, Color::ReadingCustomText }
    };
    for (const bool enabled : { true, false })
    {
        QWidget* panel = Helper::createCard(page, 12, 8);
        QButtonGroup* group = new QButtonGroup(panel);
        QGridLayout* grid = new QGridLayout();
        grid->setHorizontalSpacing(8);
        grid->setVerticalSpacing(8);
        int index = 0;
        for (const ReadingTheme& theme : readingThemes)
        {
            pdf::PDFReadingThemeCard* card = new pdf::PDFReadingThemeCard(QString::fromLatin1(theme.name), theme.page, theme.text, panel);
            group->addButton(card);
            grid->addWidget(card, index / 5, index % 5);
            card->setSelected(index == 1);
            ++index;
        }
        static_cast<QVBoxLayout*>(panel->layout())->addLayout(grid);
        panel->setEnabled(enabled);
        builder.live(enabled ? QStringLiteral("Live exclusive group, Sepia selected") : QStringLiteral("Live group, disabled"), panel);
        builder.row();
    }
    QWidget* hoverPanel = Helper::createCard(page, 12, 8, Qt::Horizontal);
    pdf::PDFReadingThemeCard* hoveredTheme = new pdf::PDFReadingThemeCard(QStringLiteral("Mint"), Color::ReadingMintPage, Color::ReadingMintText, hoverPanel);
    Helper::setState(hoveredTheme, GalleryPages::Hover);
    pdf::PDFReadingThemeCard* pressedTheme = new pdf::PDFReadingThemeCard(QStringLiteral("Night"), Color::ReadingNightPage, Color::ReadingNightText, hoverPanel);
    Helper::setState(pressedTheme, GalleryPages::Pressed | GalleryPages::Hover);
    pdf::PDFReadingThemeCard* rightToLeftTheme = new pdf::PDFReadingThemeCard(QStringLiteral("Sepia"), Color::ReadingSepiaPage, Color::ReadingSepiaText, hoverPanel);
    rightToLeftTheme->setSelected(true);
    rightToLeftTheme->setLayoutDirection(Qt::RightToLeft);
    hoverPanel->layout()->addWidget(hoveredTheme);
    hoverPanel->layout()->addWidget(pressedTheme);
    hoverPanel->layout()->addWidget(rightToLeftTheme);
    builder.live(QStringLiteral("Live hover, pressed, selected right-to-left"), hoverPanel);
    builder.row();

    // Color mode card: Selected x State; the card serves the color modes and the page layouts (O-05)
    builder.block(QStringLiteral("Color mode card (PDFPreviewCard) - Selected x State, on bg/surface"), QStringLiteral("171:4545"));
    static constexpr const char* unselectedModeNodes[4] = { "171:4413", "171:4429", "171:4445", "171:4461" };
    static constexpr const char* selectedModeNodes[4] = { "171:4477", "171:4494", "171:4511", "171:4528" };
    const Painter::PreviewCard modeCard{ QStringLiteral("Normal"), Preview::ModeNormal };
    auto paintMode = [modeCard](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintPreviewCard(&painter, rect, modeCard, state); };
    Helper::stateSpecimens(builder, QStringLiteral("Unselected"), Painter::getPreviewCardSize(modeCard), unselectedModeNodes, true, 0, paintMode);
    Helper::stateSpecimens(builder, QStringLiteral("Selected"), Painter::getPreviewCardSize(modeCard), selectedModeNodes, true, GalleryPages::On, paintMode);
    Helper::specimen(builder, QStringLiteral("130 wide (Main page)"), QSize(130, 122), true, GalleryPages::On, paintMode);
    Helper::specimen(builder, QStringLiteral("Selected, right-to-left"), Painter::getPreviewCardSize(modeCard), true, GalleryPages::On, paintMode, QString(), Qt::RightToLeft);
    builder.row();

    for (const bool isLayout : { false, true })
    {
        QWidget* panel = Helper::createCard(page, 12, 8, Qt::Horizontal);
        QButtonGroup* group = new QButtonGroup(panel);
        const QList<Preview> previews = isLayout ? QList<Preview>{ Preview::LayoutSinglePage, Preview::LayoutContinuous, Preview::LayoutTwoPages, Preview::LayoutTwoColumns }
                                                 : QList<Preview>{ Preview::ModeNormal, Preview::ModeInverted, Preview::ModeGrayscale, Preview::ModeHighContrast, Preview::ModeBitonal, Preview::ModeCustomColors };
        for (const Preview preview : previews)
        {
            pdf::PDFPreviewCard* card = new pdf::PDFPreviewCard(preview, panel);
            card->setFixedWidth(130);
            group->addButton(card);
            panel->layout()->addWidget(card);
            card->setSelected(preview == previews.front());
        }
        builder.live(isLayout ? QStringLiteral("Live page layouts") : QStringLiteral("Live color modes"), panel);
        builder.row();
    }
    QWidget* disabledModes = Helper::createCard(page, 12, 8, Qt::Horizontal);
    pdf::PDFPreviewCard* disabledSelected = new pdf::PDFPreviewCard(Preview::ModeBitonal, disabledModes);
    disabledSelected->setSelected(true);
    disabledModes->layout()->addWidget(disabledSelected);
    disabledModes->layout()->addWidget(new pdf::PDFPreviewCard(Preview::ModeGrayscale, disabledModes));
    disabledModes->setEnabled(false);
    builder.live(QStringLiteral("Live disabled"), disabledModes);
    QWidget* hoveredModes = Helper::createCard(page, 12, 8, Qt::Horizontal);
    pdf::PDFPreviewCard* hoveredMode = new pdf::PDFPreviewCard(Preview::ModeInverted, hoveredModes);
    Helper::setState(hoveredMode, GalleryPages::Hover);
    pdf::PDFPreviewCard* rightToLeftMode = new pdf::PDFPreviewCard(Preview::LayoutTwoPages, hoveredModes);
    rightToLeftMode->setSelected(true);
    rightToLeftMode->setLayoutDirection(Qt::RightToLeft);
    hoveredModes->layout()->addWidget(hoveredMode);
    hoveredModes->layout()->addWidget(rightToLeftMode);
    builder.live(QStringLiteral("Live hover, selected right-to-left"), hoveredModes);
    builder.row();

    // The pictures: Mode preview and Layout preview
    builder.block(QStringLiteral("Mode preview and Layout preview (PDFPreviewPainter)"), QStringLiteral("171:4367"));
    struct NamedPreview
    {
        Preview preview;
        const char* node;
    };
    static constexpr NamedPreview previews[] =
    {
        { Preview::ModeNormal, "171:4367" }, { Preview::ModeInverted, "171:4376" }, { Preview::ModeGrayscale, "171:4385" },
        { Preview::ModeHighContrast, "171:4394" }, { Preview::ModeBitonal, "171:4403" }, { Preview::ModeCustomColors, "171:4412" },
        { Preview::LayoutSinglePage, "192:735" }, { Preview::LayoutContinuous, "192:743" }, { Preview::LayoutTwoPages, "192:758" }, { Preview::LayoutTwoColumns, "192:773" }
    };
    for (const NamedPreview& preview : previews)
    {
        const Preview kind = preview.preview;
        builder.specimen(pdf::PDFPreviewPainter::getPreviewName(kind), pdf::PDFPreviewPainter::getPreviewSize(), [kind](QPainter& painter, const QRect& rect)
        {
            pdf::PDFPreviewPainter::paintPreview(&painter, QRectF(rect), kind);
        }, QString::fromLatin1(preview.node));
        if (kind == Preview::ModeCustomColors)
        {
            builder.row();
        }
    }
    builder.row();
    for (const NamedPreview& preview : { previews[0], previews[4], previews[6], previews[9] })
    {
        const Preview kind = preview.preview;
        builder.specimen(QStringLiteral("%1, 60 x 46").arg(pdf::PDFPreviewPainter::getPreviewName(kind)), QSize(60, 46), [kind](QPainter& painter, const QRect& rect)
        {
            pdf::PDFPreviewPainter::paintPreview(&painter, QRectF(rect), kind);
        }, QString::fromLatin1(preview.node));
        builder.specimen(QStringLiteral("%1, 150 x 89").arg(pdf::PDFPreviewPainter::getPreviewName(kind)), QSize(150, 89), [kind](QPainter& painter, const QRect& rect)
        {
            pdf::PDFPreviewPainter::paintPreview(&painter, QRectF(rect), kind);
        }, QString::fromLatin1(preview.node));
    }
    builder.row();

    // Locked document
    builder.block(QStringLiteral("Locked document (PDFLockedDocumentPicture) - State Locked / Wrong"), QStringLiteral("225:906"));
    builder.specimen(QStringLiteral("Locked"), pdf::PDFPreviewPainter::getLockedDocumentSize(), [](QPainter& painter, const QRect& rect)
    {
        pdf::PDFPreviewPainter::paintLockedDocument(&painter, QRectF(rect), pdf::PDFPreviewPainter::LockedState::Locked);
    }, QStringLiteral("225:874"));
    builder.specimen(QStringLiteral("Wrong"), pdf::PDFPreviewPainter::getLockedDocumentSize(), [](QPainter& painter, const QRect& rect)
    {
        pdf::PDFPreviewPainter::paintLockedDocument(&painter, QRectF(rect), pdf::PDFPreviewPainter::LockedState::Wrong);
    }, QStringLiteral("225:890"));
    builder.specimen(QStringLiteral("Locked, 80 x 80"), QSize(80, 80), [](QPainter& painter, const QRect& rect)
    {
        pdf::PDFPreviewPainter::paintLockedDocument(&painter, QRectF(rect), pdf::PDFPreviewPainter::LockedState::Locked);
    });
    // The live picture draws the part of the shadow outside of it into the page (the component does not clip it)
    builder.live(QStringLiteral("Live Locked"), new pdf::PDFLockedDocumentPicture(pdf::PDFLockedDocumentPicture::State::Locked, page), QStringLiteral("225:874"));
    builder.live(QStringLiteral("Live Wrong"), new pdf::PDFLockedDocumentPicture(pdf::PDFLockedDocumentPicture::State::Wrong, page), QStringLiteral("225:890"));
    builder.row();
}

}   // namespace pdffluentgallery
