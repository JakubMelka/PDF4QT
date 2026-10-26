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

#include "gallerypages.h"

#include "pdfaccenthue.h"
#include "pdfcomponentbuttons.h"
#include "pdfcomponentcards.h"
#include "pdfcomponentpanels.h"
#include "pdfreferencepointpicker.h"
#include "pdfcomponentpainter.h"
#include "pdfcomponentrows.h"
#include "pdfcomponentwindow.h"
#include "pdfstylepainter.h"
#include "pdfthemecolors.h"
#include "pdfthemeiconengine.h"
#include "pdfwidgetstyleproperties.h"

#include <QAbstractButton>
#include <QApplication>
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QVBoxLayout>
#include <QWidget>

#include <optional>

namespace pdffluentgallery
{

/// Helper functions of the pages of the components
class GalleryComponentPagesHelper
{
public:
    GalleryComponentPagesHelper() = delete;

    using Color = pdf::PDFThemeColors::Color;
    using Painter = pdf::PDFComponentPainter;

    struct NamedState
    {
        const char* name;
        int flags;
    };

    /// States of a component with hover, press, focus and the disabled state
    static constexpr NamedState STATES[] =
    {
        { "Default", GalleryPages::Default },
        { "Hover", GalleryPages::Hover },
        { "Pressed", GalleryPages::Pressed | GalleryPages::Hover },
        { "Focus", GalleryPages::Focus },
        { "Disabled", GalleryPages::Disabled }
    };

    /// Hues in the order of the variants of the design
    struct NamedHue
    {
        const char* name;
        pdf::PDFAccentHue hue;
    };

    static constexpr NamedHue HUES[] =
    {
        { "Blue", pdf::PDFAccentHue::Blue },
        { "Violet", pdf::PDFAccentHue::Violet },
        { "Amber", pdf::PDFAccentHue::Amber },
        { "Red", pdf::PDFAccentHue::Red },
        { "Teal", pdf::PDFAccentHue::Teal },
        { "Green", pdf::PDFAccentHue::Green },
        { "Neutral", pdf::PDFAccentHue::Neutral }
    };

    /// Returns the state of the painter for the flags of a sample
    static Painter::State state(int flags)
    {
        return Painter::State(GalleryPages::toStyleState(GalleryPages::StateFlags(flags)), QApplication::layoutDirection());
    }

    /// Forces the hover, the press and the disabled state of a live button
    static void setState(QAbstractButton* button, int flags)
    {
        button->setAttribute(Qt::WA_UnderMouse, (flags & GalleryPages::Hover) != 0);
        button->setDown((flags & GalleryPages::Pressed) != 0);
        button->setEnabled((flags & GalleryPages::Disabled) == 0);
    }

    /// Returns the node of the design or an empty string, if the state has no variant
    static QString node(const char* const* nodes, int index)
    {
        return (nodes && nodes[index]) ? QString::fromLatin1(nodes[index]) : QString();
    }

    /// Fills the sample (with its margin) by bg/surface: rows have no surface
    /// of their own, on the screens they lie on bg/surface cards
    static void fillSurface(QPainter& painter, const QRectF& rect)
    {
        const qreal margin = GallerySpecimen::MARGIN;
        pdf::PDFStylePainter::fillRect(&painter, rect.adjusted(-margin, -margin, margin, margin), Color::BgSurface);
    }

    /// Fills the sample (with its margin) by bg/titlebar: the buttons of the
    /// window lie in the top bar of the application
    static void fillTitleBar(QPainter& painter, const QRectF& rect)
    {
        const qreal margin = GallerySpecimen::MARGIN;
        pdf::PDFStylePainter::fillRect(&painter, rect.adjusted(-margin, -margin, margin, margin), Color::BgTitlebar);
    }

    /// Returns a strip of the top bar (bg/titlebar) holding the widget, as in app-top-bar
    static QWidget* createTitleBar(QWidget* parent, QWidget* widget)
    {
        QWidget* titleBar = new QWidget(parent);
        pdf::PDFWidgetStyleProperties::set(titleBar, pdf::PDFWidgetStyleProperties::DOCUMENT_TABS);
        titleBar->setLayoutDirection(widget->layoutDirection());
        QHBoxLayout* layout = new QHBoxLayout(titleBar);
        layout->setContentsMargins(8, 0, 8, 0);
        layout->setSpacing(0);
        widget->setParent(titleBar);
        layout->addWidget(widget);
        return titleBar;
    }

    /// Adds the window buttons and the window controls (components Window
    /// button 947:1475 and Window controls 947:1504) to the page
    static void addWindowControls(GalleryBuilder& builder);

    /// Adds a sample of a row drawn over bg/surface
    template<typename Paint>
    static void rowSpecimen(GalleryBuilder& builder, const QString& caption, QSize size, Paint paint, const QString& figmaNode = QString())
    {
        builder.specimen(caption, size, [paint](QPainter& painter, const QRect& rect)
        {
            fillSurface(painter, QRectF(rect));
            paint(painter, QRectF(rect));
        }, figmaNode);
    }

    /// Adds a row of samples of the five states (Default, Hover, Pressed, Focus,
    /// Disabled) of a row drawn by \p paint(painter, rect, state) over bg/surface
    template<typename Paint>
    static void stateSpecimens(GalleryBuilder& builder, const QString& prefix, QSize size, const char* const* nodes, Paint paint)
    {
        for (int i = 0; i < 5; ++i)
        {
            const int flags = STATES[i].flags;
            const QString caption = prefix.isEmpty() ? QString::fromLatin1(STATES[i].name) : QStringLiteral("%1 %2").arg(prefix, QString::fromLatin1(STATES[i].name));
            builder.specimen(caption, size, [paint, flags](QPainter& painter, const QRect& rect)
            {
                fillSurface(painter, QRectF(rect));
                paint(painter, QRectF(rect), state(flags));
            }, node(nodes, i));
        }
        builder.row();
    }

    /// Returns a card (bg/surface with the border of the style) for live rows
    static QWidget* createCard(QWidget* parent, int spacing = 0)
    {
        QWidget* card = new QWidget(parent);
        pdf::PDFWidgetStyleProperties::set(card, pdf::PDFWidgetStyleProperties::CARD);
        QVBoxLayout* layout = new QVBoxLayout(card);
        layout->setContentsMargins(4, 4, 4, 4);
        layout->setSpacing(spacing);
        return card;
    }

    /// Adds a live row lying on a card, as on the screens
    template<typename Row>
    static Row* liveOnCard(GalleryBuilder& builder, const QString& caption, Row* row)
    {
        QWidget* card = createCard(builder.page());
        card->setLayoutDirection(row->layoutDirection());
        row->setParent(card);
        card->layout()->addWidget(row);
        builder.live(caption, card);
        return row;
    }

    /// Adds live rows in the states hover, pressed and disabled and in the
    /// right-to-left layout, each on a card; \p create returns a new row
    template<typename Create>
    static void liveStates(GalleryBuilder& builder, Create create)
    {
        liveOnCard(builder, QStringLiteral("Live"), create());
        builder.row();
        liveOnCard(builder, QStringLiteral("Live hover"), setStateAndReturn(create(), GalleryPages::Hover));
        builder.row();
        liveOnCard(builder, QStringLiteral("Live pressed"), setStateAndReturn(create(), int(GalleryPages::Pressed | GalleryPages::Hover)));
        builder.row();
        liveOnCard(builder, QStringLiteral("Live disabled"), setStateAndReturn(create(), GalleryPages::Disabled));
        builder.row();
        QAbstractButton* rightToLeft = create();
        rightToLeft->setLayoutDirection(Qt::RightToLeft);
        liveOnCard(builder, QStringLiteral("Live right-to-left"), rightToLeft);
        builder.row();
    }

    static QAbstractButton* setStateAndReturn(QAbstractButton* button, int flags)
    {
        setState(button, flags);
        return button;
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
        pdf::PDFStylePainter::fillRect(&painter, QRectF(12, 146, 30, 4), Color::SampleOrange);
        return pixmap;
    }
};

void GalleryComponentPagesHelper::addWindowControls(GalleryBuilder& builder)
{
    using Kind = Painter::WindowButtonKind;
    using Platform = Painter::WindowPlatform;
    using Activity = pdf::PDFWindowButton::Activity;

    // Window button: every variant of the design drawn over the top bar
    builder.block(QStringLiteral("Window button (PDFWindowButton) - Default / Hover / Pressed / Inactive, Maximize Disabled"), QStringLiteral("947:1475"));

    struct WindowState
    {
        const char* name;
        int flags;
        bool windowActive;
    };
    static constexpr WindowState windowStates[] =
    {
        { "Default", GalleryPages::Default, true },
        { "Hover", GalleryPages::Hover, true },
        { "Pressed", GalleryPages::Pressed | GalleryPages::Hover, true },
        { "Inactive", GalleryPages::Default, false },
        { "Disabled", GalleryPages::Disabled, true },
        { "Inactive Hover", GalleryPages::Hover, false }
    };

    struct WindowKind
    {
        Kind kind;
        const char* name;
        const char* nodes[6];
    };
    static constexpr WindowKind windowKinds[] =
    {
        { Kind::Minimize, "Minimize", { "947:1439", "947:1442", "947:1445", "955:142595", nullptr, nullptr } },
        { Kind::Maximize, "Maximize", { "947:1448", "947:1451", "947:1454", "955:142598", "955:142607", nullptr } },
        { Kind::Restore, "Restore", { "947:1457", "947:1460", "947:1463", "955:142601", nullptr, nullptr } },
        { Kind::Close, "Close", { "947:1466", "947:1469", "947:1472", "955:142604", nullptr, nullptr } }
    };

    for (const WindowKind& windowKind : windowKinds)
    {
        for (int i = 0; i < 6; ++i)
        {
            // Only Maximize has the variant Disabled (a window of a fixed size)
            if (i == 4 && windowKind.kind != Kind::Maximize)
            {
                continue;
            }

            const WindowState windowState = windowStates[i];
            const Painter::WindowButton button{ windowKind.kind, Platform::Windows, windowState.windowActive };
            const QString caption = QStringLiteral("%1 %2").arg(QString::fromLatin1(windowKind.name), QString::fromLatin1(windowState.name));
            builder.specimen(caption, Painter::getWindowButtonSize(Platform::Windows), [button, windowState](QPainter& painter, const QRect& rect)
            {
                fillTitleBar(painter, QRectF(rect));
                Painter::paintWindowButton(&painter, QRectF(rect), button, state(windowState.flags));
            }, node(windowKind.nodes, i));
        }
        builder.row();
    }

    // Traffic lights of macOS (the circles of Window controls, Platform=macOS)
    for (const Kind kind : { Kind::Close, Kind::Minimize, Kind::Maximize })
    {
        for (const bool windowActive : { true, false })
        {
            const Painter::WindowButton button{ kind, Platform::MacOS, windowActive };
            const QString caption = QStringLiteral("macOS %1%2").arg(pdf::PDFWindowButton::getActionName(kind, Platform::MacOS), windowActive ? QString() : QStringLiteral(" Inactive"));
            builder.specimen(caption, Painter::getWindowButtonSize(Platform::MacOS), [button](QPainter& painter, const QRect& rect)
            {
                fillTitleBar(painter, QRectF(rect));
                Painter::paintWindowButton(&painter, QRectF(rect), button, state(GalleryPages::Default));
            }, QStringLiteral("955:142618"));
        }
    }
    builder.row();

    // Live buttons in the strip of the top bar
    QWidget* page = builder.page();
    for (const Kind kind : { Kind::Minimize, Kind::Maximize, Kind::Restore, Kind::Close })
    {
        pdf::PDFWindowButton* button = new pdf::PDFWindowButton(kind, Platform::Windows, page);
        button->setActivity(Activity::Active);
        builder.live(QStringLiteral("Live %1").arg(pdf::PDFWindowButton::getActionName(kind, Platform::Windows)), createTitleBar(page, button));
    }
    builder.row();

    pdf::PDFWindowButton* hoveredClose = new pdf::PDFWindowButton(Kind::Close, Platform::Windows, page);
    hoveredClose->setActivity(Activity::Active);
    hoveredClose->setAttribute(Qt::WA_UnderMouse, true);
    builder.live(QStringLiteral("Live Close hover"), createTitleBar(page, hoveredClose), QStringLiteral("947:1469"));

    pdf::PDFWindowButton* pressedMaximize = new pdf::PDFWindowButton(Kind::Maximize, Platform::Windows, page);
    pressedMaximize->setActivity(Activity::Active);
    pressedMaximize->setDown(true);
    builder.live(QStringLiteral("Live Maximize pressed"), createTitleBar(page, pressedMaximize), QStringLiteral("947:1454"));

    pdf::PDFWindowButton* externalMaximize = new pdf::PDFWindowButton(Kind::Maximize, Platform::Windows, page);
    externalMaximize->setActivity(Activity::Active);
    externalMaximize->setExternalHover(true);
    builder.live(QStringLiteral("Live Maximize, hover of the system (HTMAXBUTTON)"), createTitleBar(page, externalMaximize), QStringLiteral("947:1451"));

    pdf::PDFWindowButton* inactiveMinimize = new pdf::PDFWindowButton(Kind::Minimize, Platform::Windows, page);
    inactiveMinimize->setActivity(Activity::Inactive);
    builder.live(QStringLiteral("Live Minimize inactive"), createTitleBar(page, inactiveMinimize), QStringLiteral("955:142595"));

    pdf::PDFWindowButton* disabledMaximize = new pdf::PDFWindowButton(Kind::Maximize, Platform::Windows, page);
    disabledMaximize->setActivity(Activity::Active);
    disabledMaximize->setEnabled(false);
    builder.live(QStringLiteral("Live Maximize disabled"), createTitleBar(page, disabledMaximize), QStringLiteral("955:142607"));
    builder.row();

    // Window controls: every variant of the design, the inactive window and right-to-left
    builder.block(QStringLiteral("Window controls (PDFWindowControls) - Platform Windows / Linux / macOS, Maximized"), QStringLiteral("947:1504"));

    struct ControlsVariant
    {
        const char* name;
        Platform platform;
        bool maximized;
        const char* node;
    };
    static constexpr ControlsVariant controlsVariants[] =
    {
        { "Windows", Platform::Windows, false, "947:1484" },
        { "Windows maximized", Platform::Windows, true, "947:1494" },
        { "Linux", Platform::Linux, false, "955:142610" },
        { "Linux maximized", Platform::Linux, true, "955:142614" },
        { "macOS", Platform::MacOS, false, "955:142618" },
        { "macOS maximized", Platform::MacOS, true, "955:142631" }
    };

    for (const ControlsVariant& variant : controlsVariants)
    {
        Painter::WindowControls controls;
        controls.platform = variant.platform;
        controls.maximized = variant.maximized;
        builder.specimen(QString::fromLatin1(variant.name), Painter::getWindowControlsSize(variant.platform), [controls](QPainter& painter, const QRect& rect)
        {
            fillTitleBar(painter, QRectF(rect));
            Painter::paintWindowControls(&painter, QRectF(rect), controls, state(GalleryPages::Default));
        }, QString::fromLatin1(variant.node));
    }
    builder.row();

    // States of the group, which are not variants of the design
    for (const Platform platform : { Platform::Windows, Platform::MacOS })
    {
        const QString prefix = platform == Platform::MacOS ? QStringLiteral("macOS") : QStringLiteral("Windows");

        Painter::WindowControls inactive;
        inactive.platform = platform;
        inactive.windowActive = false;
        builder.specimen(QStringLiteral("%1 inactive").arg(prefix), Painter::getWindowControlsSize(platform), [inactive](QPainter& painter, const QRect& rect)
        {
            fillTitleBar(painter, QRectF(rect));
            Painter::paintWindowControls(&painter, QRectF(rect), inactive, state(GalleryPages::Default));
        });

        Painter::WindowControls fixedSize;
        fixedSize.platform = platform;
        fixedSize.resizable = false;
        builder.specimen(QStringLiteral("%1 fixed size").arg(prefix), Painter::getWindowControlsSize(platform), [fixedSize](QPainter& painter, const QRect& rect)
        {
            fillTitleBar(painter, QRectF(rect));
            Painter::paintWindowControls(&painter, QRectF(rect), fixedSize, state(GalleryPages::Default));
        });

        Painter::WindowControls closeHovered;
        closeHovered.platform = platform;
        closeHovered.hovered = Kind::Close;
        builder.specimen(QStringLiteral("%1 right-to-left, Close hover").arg(prefix), Painter::getWindowControlsSize(platform), [closeHovered](QPainter& painter, const QRect& rect)
        {
            fillTitleBar(painter, QRectF(rect));
            const Painter::State rightToLeft(QStyle::State_Enabled, Qt::RightToLeft);
            Painter::paintWindowControls(&painter, QRectF(rect), closeHovered, rightToLeft);
        });
    }
    builder.row();

    // Live groups in the strip of the top bar
    for (const ControlsVariant& variant : controlsVariants)
    {
        pdf::PDFWindowControls* controls = new pdf::PDFWindowControls(variant.platform, page);
        controls->setActivity(Activity::Active);
        controls->setMaximized(variant.maximized);
        builder.live(QStringLiteral("Live %1").arg(QString::fromLatin1(variant.name)), createTitleBar(page, controls), QString::fromLatin1(variant.node));
    }
    builder.row();

    pdf::PDFWindowControls* inactiveControls = new pdf::PDFWindowControls(Platform::Windows, page);
    inactiveControls->setActivity(Activity::Inactive);
    builder.live(QStringLiteral("Live inactive window"), createTitleBar(page, inactiveControls));

    pdf::PDFWindowControls* fixedControls = new pdf::PDFWindowControls(Platform::Windows, page);
    fixedControls->setActivity(Activity::Active);
    fixedControls->setResizable(false);
    builder.live(QStringLiteral("Live window of a fixed size"), createTitleBar(page, fixedControls), QStringLiteral("955:142607"));

    pdf::PDFWindowControls* snapControls = new pdf::PDFWindowControls(Platform::Windows, page);
    snapControls->setActivity(Activity::Active);
    snapControls->setHoveredButton(Kind::Maximize);
    builder.live(QStringLiteral("Live, Maximize hovered by the system (HTMAXBUTTON)"), createTitleBar(page, snapControls));
    builder.row();

    pdf::PDFWindowControls* rightToLeftControls = new pdf::PDFWindowControls(Platform::Windows, page);
    rightToLeftControls->setActivity(Activity::Active);
    rightToLeftControls->setLayoutDirection(Qt::RightToLeft);
    builder.live(QStringLiteral("Live right-to-left"), createTitleBar(page, rightToLeftControls));

    pdf::PDFWindowControls* systemControls = new pdf::PDFWindowControls(Platform::MacOS, page);
    systemControls->setSystemButtons(true);
    builder.live(QStringLiteral("Live macOS, system buttons (only the place)"), createTitleBar(page, systemControls));

    builder.live(QStringLiteral("Live, native platform, follows the window"), createTitleBar(page, new pdf::PDFWindowControls(page)));
    builder.row();
}

GalleryPages::Page GalleryPages::createShellHomePage(QWidget* parent)
{
    using Helper = GalleryComponentPagesHelper;
    using Painter = pdf::PDFComponentPainter;

    GalleryBuilder builder(parent, QStringLiteral("19-shell-home"), QStringLiteral("Shell and Home"), QStringLiteral("home"));

    // Window button and Window controls of the top bar
    Helper::addWindowControls(builder);

    // Text link
    builder.block(QStringLiteral("Text link (PDFTextLink)"), QStringLiteral("49:152"));
    static constexpr const char* linkNodes[] = { "49:144", "49:146", "49:148", "49:150", nullptr };
    const Painter::TextLink link{ QStringLiteral("View all") };
    for (int i = 0; i < 5; ++i)
    {
        const int flags = Helper::STATES[i].flags;
        builder.specimen(QString::fromLatin1(Helper::STATES[i].name), Painter::getTextLinkSize(link), [link, flags](QPainter& painter, const QRect& rect)
        {
            Painter::paintTextLink(&painter, QRectF(rect), link, Helper::state(flags));
        }, Helper::node(linkNodes, i));
    }
    builder.row();
    builder.live(QStringLiteral("Live"), new pdf::PDFTextLink(QStringLiteral("View all"), builder.page()));
    builder.live(QStringLiteral("Live, other text"), new pdf::PDFTextLink(QStringLiteral("MIT License"), builder.page()));

    // The small size: the link inside of a text of 12 px (MIT License of app-footer 28:217)
    const Painter::TextLink smallLink{ QStringLiteral("MIT License"), Painter::TextLinkSize::Small };
    builder.specimen(QStringLiteral("Small (Semi Bold 12)"), Painter::getTextLinkSize(smallLink), [smallLink](QPainter& painter, const QRect& rect)
    {
        Painter::paintTextLink(&painter, QRectF(rect), smallLink, Helper::state(0));
    }, QStringLiteral("28:218"));
    pdf::PDFTextLink* smallLiveLink = new pdf::PDFTextLink(QStringLiteral("MIT License"), builder.page());
    smallLiveLink->setLinkSize(Painter::TextLinkSize::Small);
    builder.live(QStringLiteral("Live, small"), smallLiveLink);
    for (const int flags : { int(GalleryPages::Hover), int(GalleryPages::Pressed | GalleryPages::Hover), int(GalleryPages::Disabled) })
    {
        pdf::PDFTextLink* liveLink = new pdf::PDFTextLink(QStringLiteral("View all"), builder.page());
        Helper::setState(liveLink, flags);
        builder.live(QStringLiteral("Live %1").arg(flags == GalleryPages::Hover ? QStringLiteral("hover") : (flags == GalleryPages::Disabled ? QStringLiteral("disabled") : QStringLiteral("pressed"))), liveLink);
    }

    // Star button
    builder.block(QStringLiteral("Star button (PDFStarButton) - Pinned False / True, size 28 and 24"), QStringLiteral("64:569"));
    static constexpr const char* unpinnedNodes[] = { "64:545", "64:548", "64:551", "64:554", nullptr };
    static constexpr const char* pinnedNodes[] = { "64:557", "64:560", "64:563", "64:566", nullptr };
    for (const Painter::StarSize size : { Painter::StarSize::Medium, Painter::StarSize::Small })
    {
        for (const bool pinned : { false, true })
        {
            for (int i = 0; i < 5; ++i)
            {
                const int flags = Helper::STATES[i].flags | (pinned ? GalleryPages::On : GalleryPages::Default);
                const QString caption = QStringLiteral("%1 %2 %3").arg(static_cast<int>(size)).arg(pinned ? QStringLiteral("Pinned") : QStringLiteral("Unpinned"), QString::fromLatin1(Helper::STATES[i].name));
                builder.specimen(caption, Painter::getStarSize(size), [size, flags](QPainter& painter, const QRect& rect)
                {
                    Painter::paintStar(&painter, QRectF(rect), size, Helper::state(flags));
                }, Helper::node(pinned ? pinnedNodes : unpinnedNodes, i));
            }
            builder.row();
        }
    }
    builder.live(QStringLiteral("Live, unpinned"), new pdf::PDFStarButton(builder.page()));
    pdf::PDFStarButton* pinnedStar = builder.live(QStringLiteral("Live, pinned"), new pdf::PDFStarButton(builder.page()));
    pinnedStar->setPinned(true);
    builder.live(QStringLiteral("Live 24"), new pdf::PDFStarButton(pdf::PDFStarButton::StarSize::Small, builder.page()));
    pdf::PDFStarButton* hoveredStar = builder.live(QStringLiteral("Live, hover"), new pdf::PDFStarButton(builder.page()));
    Helper::setState(hoveredStar, GalleryPages::Hover);
    pdf::PDFStarButton* disabledStar = builder.live(QStringLiteral("Live, pinned, disabled"), new pdf::PDFStarButton(builder.page()));
    disabledStar->setPinned(true);
    Helper::setState(disabledStar, GalleryPages::Disabled);

    // Page thumbnail
    builder.block(QStringLiteral("Page thumbnail (PDFPageThumbnail) - placeholder and a picture of the page"), QStringLiteral("78:892"));
    struct Thumbnail
    {
        const char* name;
        Painter::ThumbnailSize size;
        const char* node;
    };
    static constexpr Thumbnail thumbnails[] =
    {
        { "Small", Painter::ThumbnailSize::Small, "78:869" },
        { "Medium", Painter::ThumbnailSize::Medium, "78:874" },
        { "Large", Painter::ThumbnailSize::Large, "78:882" }
    };
    const QPixmap pagePicture = Helper::createPagePicture();
    for (const bool hasPicture : { false, true })
    {
        for (const Thumbnail& thumbnail : thumbnails)
        {
            Painter::PageThumbnail content;
            content.size = thumbnail.size;
            if (hasPicture)
            {
                content.pixmap = pagePicture;
            }

            const QString caption = QStringLiteral("%1%2").arg(QString::fromLatin1(thumbnail.name), hasPicture ? QStringLiteral(", picture") : QString());
            builder.specimen(caption, Painter::getPageThumbnailSize(thumbnail.size), [content](QPainter& painter, const QRect& rect)
            {
                Painter::paintPageThumbnail(&painter, QRectF(rect), content, Helper::state(GalleryPages::Default));
            }, QString::fromLatin1(thumbnail.node));
        }
    }
    builder.row();
    for (const Thumbnail& thumbnail : thumbnails)
    {
        builder.live(QStringLiteral("Live %1").arg(QString::fromLatin1(thumbnail.name)), new pdf::PDFPageThumbnail(thumbnail.size, builder.page()), QString::fromLatin1(thumbnail.node));
    }
    pdf::PDFPageThumbnail* pictureThumbnail = builder.live(QStringLiteral("Live Medium, picture"), new pdf::PDFPageThumbnail(Painter::ThumbnailSize::Medium, builder.page()));
    pictureThumbnail->setPixmap(pagePicture);

    QWidget* page = builder.page();

    // Link row
    builder.block(QStringLiteral("Link row (PDFLinkRow)"), QStringLiteral("49:190"));
    static constexpr const char* linkRowNodes[] = { "49:160", "49:163", "49:172", "49:181", nullptr };
    Painter::LinkRow linkRow{ QStringLiteral("Report Issue"), QStringLiteral("Tell us about a problem"), QStringLiteral("alert-circle"), std::nullopt };
    Helper::stateSpecimens(builder, QString(), Painter::getLinkRowSize(linkRow), linkRowNodes, [linkRow](QPainter& painter, const QRectF& rect, const Painter::State& state)
    {
        Painter::paintLinkRow(&painter, rect, linkRow, state);
    });
    Painter::LinkRow coloredLinkRow{ QStringLiteral("GitHub repository"), QStringLiteral("Source code, releases and discussions"), QStringLiteral("github"), Helper::Color::IconAccent };
    Helper::rowSpecimen(builder, QStringLiteral("Icon color overridden"), Painter::getLinkRowSize(coloredLinkRow), [coloredLinkRow](QPainter& painter, const QRectF& rect)
    {
        Painter::paintLinkRow(&painter, rect, coloredLinkRow, Helper::state(GalleryPages::Default));
    });
    builder.row();
    Helper::liveStates(builder, [page]() { return new pdf::PDFLinkRow(QStringLiteral("Report Issue"), QStringLiteral("Tell us about a problem"), QStringLiteral("alert-circle"), page); });

    // Recent file row: Status x State
    builder.block(QStringLiteral("Recent file row (PDFRecentFileRow) - Status Available / Missing"), QStringLiteral("49:280"));
    static constexpr const char* availableNodes[] = { "49:198", "49:203", "49:214", "49:225", nullptr };
    static constexpr const char* missingNodes[] = { "49:236", "49:247", "49:258", "49:269", nullptr };
    Painter::RecentFileRow fileRow;
    fileRow.fileName = QStringLiteral("Project_Report.pdf");
    fileRow.path = QStringLiteral("C:\\Users\\Jakub\\Documents");
    fileRow.time = QStringLiteral("Today 10:24");
    for (const Painter::FileStatus status : { Painter::FileStatus::Available, Painter::FileStatus::Missing })
    {
        Painter::RecentFileRow content = fileRow;
        content.status = status;
        const bool isMissing = status == Painter::FileStatus::Missing;
        Helper::stateSpecimens(builder, isMissing ? QStringLiteral("Missing") : QStringLiteral("Available"), Painter::getRecentFileRowSize(content), isMissing ? missingNodes : availableNodes,
                               [content](QPainter& painter, const QRectF& rect, const Painter::State& state)
        {
            Painter::paintRecentFileRow(&painter, rect, content, state);
        });
    }
    Painter::RecentFileRow longFileRow = fileRow;
    longFileRow.fileName = QStringLiteral("Technical_Documentation_of_the_Project_v2.pdf");
    longFileRow.pinned = true;
    Helper::rowSpecimen(builder, QStringLiteral("Long name elided in the middle, pinned"), Painter::getRecentFileRowSize(longFileRow), [longFileRow](QPainter& painter, const QRectF& rect)
    {
        Painter::paintRecentFileRow(&painter, rect, longFileRow, Helper::state(GalleryPages::Default));
    });
    Painter::RecentFileRow noStarRow = fileRow;
    noStarRow.starVisible = false;
    Helper::rowSpecimen(builder, QStringLiteral("Show star false"), Painter::getRecentFileRowSize(noStarRow), [noStarRow](QPainter& painter, const QRectF& rect)
    {
        Painter::paintRecentFileRow(&painter, rect, noStarRow, Helper::state(GalleryPages::Default));
    });
    builder.row();
    Helper::liveStates(builder, [page]() { return new pdf::PDFRecentFileRow(QStringLiteral("Project_Report.pdf"), QStringLiteral("C:\\Users\\Jakub\\Documents"), QStringLiteral("Today 10:24"), page); });
    pdf::PDFRecentFileRow* missingFile = Helper::liveOnCard(builder, QStringLiteral("Live missing, pinned"), new pdf::PDFRecentFileRow(QStringLiteral("Annual_Budget_2025.xlsx.pdf"), QStringLiteral("D:\\Archive\\Finance"), QStringLiteral("Yesterday"), page));
    missingFile->setStatus(pdf::PDFRecentFileRow::Status::Missing);
    missingFile->setPinned(true);
    builder.row();
    pdf::PDFRecentFileRow* longFile = Helper::liveOnCard(builder, QStringLiteral("Live long name"), new pdf::PDFRecentFileRow(QStringLiteral("Technical_Documentation_of_the_Project_v2.pdf"), QStringLiteral("C:\\Users\\Jakub\\Documents\\Projects\\PDF4QT\\Specifications"), QStringLiteral("Sep 12"), page));
    longFile->setStarVisible(false);
    builder.row();

    // Recent tool row
    builder.block(QStringLiteral("Recent tool row (PDFRecentToolRow)"), QStringLiteral("50:279"));
    static constexpr const char* recentToolNodes[] = { "50:243", "50:252", "50:261", "50:270", nullptr };
    const Painter::RecentToolRow recentToolRow{ QStringLiteral("Compress"), QStringLiteral("Today 11:02"), QStringLiteral("minimize-2"), pdf::PDFAccentHue::Amber };
    Helper::stateSpecimens(builder, QString(), Painter::getRecentToolRowSize(recentToolRow), recentToolNodes, [recentToolRow](QPainter& painter, const QRectF& rect, const Painter::State& state)
    {
        Painter::paintRecentToolRow(&painter, rect, recentToolRow, state);
    });
    Helper::liveStates(builder, [page]() { return new pdf::PDFRecentToolRow(QStringLiteral("Protect with Password"), QStringLiteral("Yesterday 16:40"), QStringLiteral("lock"), pdf::PDFAccentHue::Red, page); });

    // Workspace row
    builder.block(QStringLiteral("Workspace row (PDFWorkspaceRow)"), QStringLiteral("64:544"));
    static constexpr const char* workspaceNodes[] = { "64:508", "64:517", "64:526", "64:535", nullptr };
    Painter::WorkspaceRow workspaceRow;
    workspaceRow.name = QStringLiteral("Thesis review");
    workspaceRow.meta = QStringLiteral("4 documents · Opened Yesterday 16:15");
    Helper::stateSpecimens(builder, QString(), Painter::getWorkspaceRowSize(workspaceRow), workspaceNodes, [workspaceRow](QPainter& painter, const QRectF& rect, const Painter::State& state)
    {
        Painter::paintWorkspaceRow(&painter, rect, workspaceRow, state);
    });
    Helper::liveStates(builder, [page]() { return new pdf::PDFWorkspaceRow(QStringLiteral("Thesis review"), QStringLiteral("4 documents · Opened Yesterday 16:15"), page); });

    // Quick action tile, Folder card, Pinned card and the sample of the Home screen
    addShellHomeCards(builder);

    // Recent item row
    addShellHomeDelegates(builder);

    return builder.finish();
}

GalleryPages::Page GalleryPages::createCatalogPage(QWidget* parent)
{
    using Helper = GalleryComponentPagesHelper;
    using Painter = pdf::PDFComponentPainter;

    GalleryBuilder builder(parent, QStringLiteral("20-catalog"), QStringLiteral("Tools catalog, What's New, About"), QStringLiteral("grid"));

    // Tool icon: rows are the sizes, columns the hues, as in the design
    builder.block(QStringLiteral("Tool icon (PDFToolBadge) - sizes 32, 28 and 44"), QStringLiteral("89:1289"));
    struct BadgeRow
    {
        Painter::BadgeSize size;
        const char* nodes[7];
    };
    static constexpr BadgeRow badgeRows[] =
    {
        { Painter::BadgeSize::Medium, { "89:1268", "89:1271", "89:1274", "89:1277", "89:1280", "89:1283", "89:1286" } },
        { Painter::BadgeSize::Small, { "706:146672", "706:146675", "706:146678", "706:146681", "706:146684", "706:146687", "706:146690" } },
        { Painter::BadgeSize::Large, { "706:146693", "706:146696", "706:146699", "706:146702", "706:146705", "706:146708", "706:146711" } }
    };
    for (const BadgeRow& badgeRow : badgeRows)
    {
        for (int i = 0; i < 7; ++i)
        {
            const Painter::ToolBadge badge{ QStringLiteral("grid"), Helper::HUES[i].hue, badgeRow.size };
            builder.specimen(QStringLiteral("%1 %2").arg(QString::fromLatin1(Helper::HUES[i].name)).arg(static_cast<int>(badgeRow.size)), Painter::getToolBadgeSize(badge), [badge](QPainter& painter, const QRect& rect)
            {
                Painter::paintToolBadge(&painter, QRectF(rect), badge, Helper::state(GalleryPages::Default));
            }, QString::fromLatin1(badgeRow.nodes[i]));
        }
        builder.row();
    }
    const Painter::ToolBadge disabledBadge{ QStringLiteral("grid"), pdf::PDFAccentHue::Blue, Painter::BadgeSize::Medium };
    builder.specimen(QStringLiteral("Blue 32 Disabled (O-08)"), Painter::getToolBadgeSize(disabledBadge), [disabledBadge](QPainter& painter, const QRect& rect)
    {
        Painter::paintToolBadge(&painter, QRectF(rect), disabledBadge, Helper::state(GalleryPages::Disabled));
    });
    builder.row();

    struct Category
    {
        const char* name;
        const char* icon;
        pdf::PDFAccentHue hue;
    };
    static constexpr Category categories[] =
    {
        { "Organize Pages", "files", pdf::PDFAccentHue::Amber },
        { "Create & Convert", "file-plus", pdf::PDFAccentHue::Blue },
        { "Edit", "edit", pdf::PDFAccentHue::Green },
        { "Protect & Sign", "lock", pdf::PDFAccentHue::Red },
        { "Optimize", "compress-auto", pdf::PDFAccentHue::Violet },
        { "Review & Inspect", "ruler", pdf::PDFAccentHue::Teal },
        { "Other", "layers", pdf::PDFAccentHue::Neutral }
    };
    for (const Category& category : categories)
    {
        builder.live(QStringLiteral("Live %1").arg(QString::fromLatin1(category.name)), new pdf::PDFToolBadge(QString::fromLatin1(category.icon), category.hue, Painter::BadgeSize::Medium, builder.page()));
    }
    builder.live(QStringLiteral("Live 28"), new pdf::PDFToolBadge(QStringLiteral("ocr"), pdf::PDFAccentHue::Blue, Painter::BadgeSize::Small, builder.page()));
    builder.live(QStringLiteral("Live 44"), new pdf::PDFToolBadge(QStringLiteral("layers"), pdf::PDFAccentHue::Violet, Painter::BadgeSize::Large, builder.page()));

    // Tag
    builder.block(QStringLiteral("Tag (PDFTag)"), QStringLiteral("116:2353"));
    static constexpr const char* tagNodes[] = { "116:2345", "116:2347", "116:2349", "116:2351", nullptr };
    for (const char* text : { "OCR", "Advanced" })
    {
        const Painter::Tag tag{ QString::fromLatin1(text) };
        for (int i = 0; i < 5; ++i)
        {
            const int flags = Helper::STATES[i].flags;
            builder.specimen(QStringLiteral("%1 %2").arg(tag.text, QString::fromLatin1(Helper::STATES[i].name)), Painter::getTagSize(tag), [tag, flags](QPainter& painter, const QRect& rect)
            {
                Painter::paintTag(&painter, QRectF(rect), tag, Helper::state(flags));
            }, Helper::node(tagNodes, i));
        }
        builder.row();
    }
    builder.live(QStringLiteral("Live label (not clickable)"), new pdf::PDFTag(QStringLiteral("Advanced"), builder.page()));
    pdf::PDFTag* clickableTag = builder.live(QStringLiteral("Live clickable"), new pdf::PDFTag(QStringLiteral("OCR"), builder.page()));
    clickableTag->setClickable(true);
    pdf::PDFTag* hoveredTag = builder.live(QStringLiteral("Live clickable, hover"), new pdf::PDFTag(QStringLiteral("Forms"), builder.page()));
    hoveredTag->setClickable(true);
    Helper::setState(hoveredTag, GalleryPages::Hover);
    pdf::PDFTag* disabledTag = builder.live(QStringLiteral("Live disabled"), new pdf::PDFTag(QStringLiteral("Signatures"), builder.page()));
    disabledTag->setClickable(true);
    Helper::setState(disabledTag, GalleryPages::Disabled);
    builder.live(QStringLiteral("Live, one character"), new pdf::PDFTag(QStringLiteral("A"), builder.page()));

    QWidget* page = builder.page();

    // Tool row
    builder.block(QStringLiteral("Tool row (PDFToolRow) - badge, tag and star are nested"), QStringLiteral("89:1330"));
    static constexpr const char* toolRowNodes[] = { "89:1290", "89:1300", "89:1310", "89:1320", nullptr };
    Painter::ToolRow toolRow;
    toolRow.name = QStringLiteral("Merge PDF");
    toolRow.description = QStringLiteral("Combine multiple PDFs into one file");
    toolRow.iconName = QStringLiteral("grid");
    toolRow.hue = pdf::PDFAccentHue::Blue;
    Helper::stateSpecimens(builder, QString(), Painter::getToolRowSize(toolRow), toolRowNodes, [toolRow](QPainter& painter, const QRectF& rect, const Painter::State& state)
    {
        Painter::paintToolRow(&painter, rect, toolRow, state);
    });
    Painter::ToolRow taggedToolRow = toolRow;
    taggedToolRow.tag = QStringLiteral("Advanced");
    taggedToolRow.pinned = true;
    Helper::rowSpecimen(builder, QStringLiteral("Show tag, pinned"), Painter::getToolRowSize(taggedToolRow), [taggedToolRow](QPainter& painter, const QRectF& rect)
    {
        Painter::paintToolRow(&painter, rect, taggedToolRow, Helper::state(GalleryPages::Default));
    });
    builder.row();
    Helper::liveStates(builder, [page]() { return new pdf::PDFToolRow(QStringLiteral("Merge PDF"), QStringLiteral("Combine multiple PDFs into one file"), QStringLiteral("files"), pdf::PDFAccentHue::Amber, page); });
    pdf::PDFToolRow* taggedTool = Helper::liveOnCard(builder, QStringLiteral("Live with tag, pinned"), new pdf::PDFToolRow(QStringLiteral("Redact"), QStringLiteral("Permanently remove sensitive content from the document"), QStringLiteral("lock"), pdf::PDFAccentHue::Red, page));
    taggedTool->setTag(QStringLiteral("Advanced"));
    taggedTool->setPinned(true);
    builder.row();
    Painter::ToolRow starlessToolRow = toolRow;
    starlessToolRow.starVisible = false;
    Helper::rowSpecimen(builder, QStringLiteral("Without star (collections editor 284:2304)"), Painter::getToolRowSize(starlessToolRow), [starlessToolRow](QPainter& painter, const QRectF& rect)
    {
        Painter::paintToolRow(&painter, rect, starlessToolRow, Helper::state(GalleryPages::Default));
    });
    pdf::PDFToolRow* starlessTool = Helper::liveOnCard(builder, QStringLiteral("Live without star"), new pdf::PDFToolRow(QStringLiteral("Rotate Pages"), QStringLiteral("Turn pages in 90° steps"), QStringLiteral("rotate-cw"), pdf::PDFAccentHue::Amber, page));
    starlessTool->setStarVisible(false);
    builder.row();

    // Collection row
    builder.block(QStringLiteral("Collection row (PDFCollectionRow) - fixed colors of the circle (F-32)"), QStringLiteral("89:1367"));
    static constexpr const char* collectionNodes[] = { "89:1331", "89:1340", "89:1349", "89:1358", nullptr };
    Painter::CollectionRow collectionRow;
    collectionRow.name = QStringLiteral("Prepare for sharing");
    collectionRow.meta = QStringLiteral("3 tools · Compress, Sanitize, Sign");
    Helper::stateSpecimens(builder, QString(), Painter::getCollectionRowSize(collectionRow), collectionNodes, [collectionRow](QPainter& painter, const QRectF& rect, const Painter::State& state)
    {
        Painter::paintCollectionRow(&painter, rect, collectionRow, state);
    });
    Helper::liveStates(builder, [page]() { return new pdf::PDFCollectionRow(QStringLiteral("Prepare for sharing"), QStringLiteral("3 tools · Compress, Sanitize, Sign"), page); });
    Painter::CollectionRow createRow;
    createRow.name = QStringLiteral("New collection…");
    createRow.meta = QStringLiteral("Group the tools you use for one task");
    createRow.iconName = QStringLiteral("plus-circle");
    createRow.isCreate = true;
    Helper::stateSpecimens(builder, QStringLiteral("Create (new-collection 272:12952)"), Painter::getCollectionRowSize(createRow), collectionNodes, [createRow](QPainter& painter, const QRectF& rect, const Painter::State& state)
    {
        Painter::paintCollectionRow(&painter, rect, createRow, state);
    });
    Helper::liveStates(builder, [page]()
    {
        pdf::PDFCollectionRow* row = new pdf::PDFCollectionRow(QStringLiteral("New collection…"), QStringLiteral("Group the tools you use for one task"), page);
        row->setIconName(QStringLiteral("plus-circle"));
        row->setCreateVariant(true);
        return row;
    });

    // Tool shortcut
    builder.block(QStringLiteral("Tool shortcut (PDFToolShortcut) - focus ring outside"), QStringLiteral("192:836"));
    static constexpr const char* shortcutNodes[] = { "192:816", "192:821", "192:826", "192:831", nullptr };
    const Painter::ToolShortcut shortcut{ QStringLiteral("Merge PDF"), QStringLiteral("grid"), pdf::PDFAccentHue::Blue };
    Helper::stateSpecimens(builder, QString(), Painter::getToolShortcutSize(shortcut), shortcutNodes, [shortcut](QPainter& painter, const QRectF& rect, const Painter::State& state)
    {
        Painter::paintToolShortcut(&painter, rect, shortcut, state);
    });
    Helper::liveStates(builder, [page]() { return new pdf::PDFToolShortcut(QStringLiteral("Compress"), QStringLiteral("compress-auto"), pdf::PDFAccentHue::Violet, page); });

    // Issue row
    builder.block(QStringLiteral("Issue row (PDFIssueRow)"), QStringLiteral("103:1931"));
    static constexpr const char* issueNodes[] = { "103:1895", "103:1904", "103:1913", "103:1922", nullptr };
    Painter::IssueRow issueRow;
    issueRow.number = QStringLiteral("#612");
    issueRow.title = QStringLiteral("Crash when opening some encrypted documents");
    issueRow.description = QStringLiteral("Documents protected with AES-256 and an empty user password now open correctly.");
    issueRow.date = QStringLiteral("Sep 12, 2026");
    Helper::stateSpecimens(builder, QString(), Painter::getIssueRowSize(issueRow), issueNodes, [issueRow](QPainter& painter, const QRectF& rect, const Painter::State& state)
    {
        Painter::paintIssueRow(&painter, rect, issueRow, state);
    });
    Helper::liveStates(builder, [page]() { return new pdf::PDFIssueRow(QStringLiteral("#598"), QStringLiteral("Wrong colors of some JPEG 2000 images"), QStringLiteral("Images with an embedded ICC profile are converted by the profile."), QStringLiteral("Aug 30, 2026"), page); });

    // Version row
    builder.block(QStringLiteral("Version row (PDFVersionRow) - Current x Selected x State, Show line"), QStringLiteral("103:1950"));
    Painter::VersionRow versionRow;
    versionRow.version = QStringLiteral("v1.5.3");
    versionRow.date = QStringLiteral("Sep 14, 2026");
    versionRow.summary = QStringLiteral("One unified app, faster loading, timestamped signatures.");

    // The 16 variants of the design: Current True / False x Selected False / True x
    // Default / Hover / Pressed / Focus (and Disabled of the library)
    struct VersionStateVariant
    {
        const char* prefix;
        bool current;
        bool selected;
        const char* nodes[5];
    };
    static constexpr VersionStateVariant versionStates[] =
    {
        { "Current", true, false, { "103:1932", "987:13304", "987:13313", "987:13322", nullptr } },
        { "Current Selected", true, true, { "987:13331", "987:13340", "987:13349", "987:13358", nullptr } },
        { "Not current", false, false, { "103:1941", "987:13367", "987:13376", "987:13385", nullptr } },
        { "Not current Selected", false, true, { "987:13394", "987:13403", "987:13412", "987:13421", nullptr } }
    };
    for (const VersionStateVariant& variant : versionStates)
    {
        Painter::VersionRow content = versionRow;
        content.current = variant.current;
        content.selected = variant.selected;
        Helper::stateSpecimens(builder, QString::fromLatin1(variant.prefix), Painter::getVersionRowSize(content), variant.nodes, [content](QPainter& painter, const QRectF& rect, const Painter::State& state)
        {
            Painter::paintVersionRow(&painter, rect, content, state);
        });
    }

    struct VersionVariant
    {
        const char* name;
        bool current;
        bool lineVisible;
        int flags;
        const char* node;
    };
    static constexpr VersionVariant versionVariants[] =
    {
        { "Current True", true, true, GalleryPages::Default, "103:1932" },
        { "Current False", false, true, GalleryPages::Default, "103:1941" },
        { "Show line false", false, false, GalleryPages::Default, nullptr },
        { "Disabled (O-08)", true, true, GalleryPages::Disabled, nullptr }
    };
    for (const VersionVariant& variant : versionVariants)
    {
        Painter::VersionRow content = versionRow;
        content.current = variant.current;
        content.lineVisible = variant.lineVisible;
        const int flags = variant.flags;
        Helper::rowSpecimen(builder, QString::fromLatin1(variant.name), Painter::getVersionRowSize(content), [content, flags](QPainter& painter, const QRectF& rect)
        {
            Painter::paintVersionRow(&painter, rect, content, Helper::state(flags));
        }, variant.node ? QString::fromLatin1(variant.node) : QString());
    }
    builder.row();

    // A timeline: the installed version first, the last row without the line
    QWidget* timeline = Helper::createCard(page);
    timeline->setFixedWidth(324 + 8);
    QVBoxLayout* timelineLayout = static_cast<QVBoxLayout*>(timeline->layout());
    pdf::PDFVersionRow* currentVersion = new pdf::PDFVersionRow(QStringLiteral("v1.5.3"), QStringLiteral("Sep 14, 2026"), QStringLiteral("One unified app, faster loading, timestamped signatures."), timeline);
    pdf::PDFVersionRow* previousVersion = new pdf::PDFVersionRow(QStringLiteral("v1.5.2"), QStringLiteral("Jun 2, 2026"), QStringLiteral("Optical character recognition of scanned documents with Tesseract, batch processing and export of the recognized text."), timeline);
    previousVersion->setCurrent(false);
    previousVersion->setSelected(true);
    pdf::PDFVersionRow* oldestVersion = new pdf::PDFVersionRow(QStringLiteral("v1.5.0"), QStringLiteral("Jan 20, 2026"), QStringLiteral("MIT license."), timeline);
    oldestVersion->setCurrent(false);
    oldestVersion->setLineVisible(false);
    timelineLayout->addWidget(currentVersion);
    timelineLayout->addWidget(previousVersion);
    timelineLayout->addWidget(oldestVersion);
    builder.live(QStringLiteral("Live timeline"), timeline);
    pdf::PDFVersionRow* rightToLeftVersion = new pdf::PDFVersionRow(QStringLiteral("v1.5.3"), QStringLiteral("Sep 14, 2026"), QStringLiteral("One unified app, faster loading, timestamped signatures."), page);
    rightToLeftVersion->setLayoutDirection(Qt::RightToLeft);
    rightToLeftVersion->setFixedWidth(324);
    rightToLeftVersion->setSelected(true);
    Helper::liveOnCard(builder, QStringLiteral("Live right-to-left, selected"), rightToLeftVersion);
    builder.row();
    pdf::PDFVersionRow* textVersion = new pdf::PDFVersionRow(QStringLiteral("v1.4.0"), QStringLiteral("Mar 3, 2025"), QStringLiteral("A row, which is not clickable, is a text."), page);
    textVersion->setCurrent(false);
    textVersion->setClickable(false);
    textVersion->setFixedWidth(324);
    Helper::liveOnCard(builder, QStringLiteral("Live not clickable (a text)"), textVersion);
    builder.row();
    Helper::liveStates(builder, [page]()
    {
        pdf::PDFVersionRow* row = new pdf::PDFVersionRow(QStringLiteral("v1.5.2"), QStringLiteral("Jun 2, 2026"), QStringLiteral("Optical character recognition of scanned documents."), page);
        row->setCurrent(false);
        row->setSelected(true);
        row->setFixedWidth(324);
        return row;
    });

    // Rendering issue row (issue-row of the popover Rendering issues, View Document 198:5950)
    builder.block(QStringLiteral("Rendering issue row (PDFRenderingIssueRow)"), QStringLiteral("198:5960"));
    static constexpr const char* renderingIssueNodes[] = { "198:5960", "198:5972", nullptr, nullptr, nullptr };
    Painter::RenderingIssueRow renderingIssue;
    renderingIssue.page = QStringLiteral("Page 12");
    renderingIssue.message = QStringLiteral("Font “ArialMT” is not embedded. A substitute font is used.");
    renderingIssue.severity = Painter::IssueSeverity::Warning;
    Helper::stateSpecimens(builder, QString(), Painter::getRenderingIssueRowSize(renderingIssue), renderingIssueNodes, [renderingIssue](QPainter& painter, const QRectF& rect, const Painter::State& state)
    {
        Painter::paintRenderingIssueRow(&painter, rect, renderingIssue, state);
    });
    struct RenderingIssueVariant
    {
        const char* name;
        Painter::IssueSeverity severity;
        const char* message;
    };
    static constexpr RenderingIssueVariant renderingIssueVariants[] =
    {
        { "Error, two lines", Painter::IssueSeverity::Error, "Invalid arguments of the operator “Tj”. The operator was skipped and the rest of the content stream is drawn." },
        { "Information", Painter::IssueSeverity::Information, "Shading of type 7 is drawn with a lower quality." }
    };
    for (const RenderingIssueVariant& variant : renderingIssueVariants)
    {
        Painter::RenderingIssueRow content = renderingIssue;
        content.severity = variant.severity;
        content.message = QString::fromUtf8(variant.message);
        Helper::rowSpecimen(builder, QString::fromLatin1(variant.name), Painter::getRenderingIssueRowSize(content), [content](QPainter& painter, const QRectF& rect)
        {
            Painter::paintRenderingIssueRow(&painter, rect, content, Helper::state(GalleryPages::Default));
        });
    }
    builder.row();
    Helper::liveStates(builder, [page]()
    {
        pdf::PDFRenderingIssueRow* row = new pdf::PDFRenderingIssueRow(QStringLiteral("Page 308"), QStringLiteral("Invalid arguments of the operator “Tj”. The operator was skipped."),
                                                                       pdf::PDFRenderingIssueRow::Severity::Error, page);
        row->setFixedWidth(406);
        return row;
    });

    // Tool category header, Tool choice, Resource card, Highlight card, Video card, Logo
    addCatalogCards(builder);

    // Continue with
    addCatalogPanels(builder);

    return builder.finish();
}

GalleryPages::Page GalleryPages::createFormsPage(QWidget* parent)
{
    GalleryBuilder builder(parent, QStringLiteral("21-forms"), QStringLiteral("Form controls and Settings"), QStringLiteral("settings"));

    // Strength meter, Color button, Color palette popover
    addFormsPanels(builder);

    // Shortcut row
    addFormsDelegates(builder);

    builder.block(QStringLiteral("Other components of this chapter"));
    builder.note(QStringLiteral("The form controls drawn by the style are on the pages of the widgets."));
    return builder.finish();
}

GalleryPages::Page GalleryPages::createDocumentPage(QWidget* parent)
{
    GalleryBuilder builder(parent, QStringLiteral("22-document"), QStringLiteral("Document window"), QStringLiteral("file"));

    // Thumbnail item, Bookmark row, Note row, Attachment row, Search result row
    addDocumentDelegates(builder);

    builder.block(QStringLiteral("Other components of this chapter"));
    builder.note(QStringLiteral("Stamp chip and Toolbar field are on the page Components."));
    return builder.finish();
}

GalleryPages::Page GalleryPages::createPanelsPage(QWidget* parent)
{
    using Helper = GalleryComponentPagesHelper;
    using Painter = pdf::PDFComponentPainter;
    using Color = pdf::PDFThemeColors::Color;

    GalleryBuilder builder(parent, QStringLiteral("23-panels"), QStringLiteral("Document panels, colors and previews"), QStringLiteral("droplet"));

    // Color swatch: Kind x Selected x State, as the design has them
    builder.block(QStringLiteral("Color swatch (PDFColorSwatch) - Kind Color / None, Selected False / True"), QStringLiteral("206:805"));
    struct SwatchRow
    {
        Painter::SwatchKind kind;
        bool selected;
        const char* nodes[5];
    };
    static constexpr SwatchRow swatchRows[] =
    {
        { Painter::SwatchKind::Color, false, { "206:765", "206:767", "206:769", "206:771", "773:147228" } },
        { Painter::SwatchKind::Color, true, { "206:773", "206:775", "206:777", "206:779", "773:147230" } },
        { Painter::SwatchKind::None, false, { "206:781", "206:784", "206:787", "206:790", "773:147232" } },
        { Painter::SwatchKind::None, true, { "206:793", "206:796", "206:799", "206:802", "773:147235" } }
    };
    for (const SwatchRow& swatchRow : swatchRows)
    {
        for (int i = 0; i < 5; ++i)
        {
            const int flags = Helper::STATES[i].flags | (swatchRow.selected ? GalleryPages::On : GalleryPages::Default);
            const QString caption = QStringLiteral("%1 %2 %3").arg(swatchRow.kind == Painter::SwatchKind::None ? QStringLiteral("None") : QStringLiteral("Color"),
                                                                  swatchRow.selected ? QStringLiteral("Selected") : QStringLiteral("Unselected"), QString::fromLatin1(Helper::STATES[i].name));
            const Painter::SwatchKind kind = swatchRow.kind;
            builder.specimen(caption, Painter::getColorSwatchSize(), [kind, flags](QPainter& painter, const QRect& rect)
            {
                const Painter::ColorSwatch swatch{ pdf::PDFStylePainter::color(Color::AnnotationYellow), kind };
                Painter::paintColorSwatch(&painter, QRectF(rect), swatch, Helper::state(flags));
            }, QString::fromLatin1(swatchRow.nodes[i]));
        }
        builder.row();
    }

    // Live swatches in an exclusive group, as in the bar of an annotation tool
    struct NamedColor
    {
        const char* name;
        Color color;
    };
    static constexpr NamedColor annotationColors[] =
    {
        { "Yellow", Color::AnnotationYellow }, { "Orange", Color::AnnotationOrange }, { "Red", Color::AnnotationRed },
        { "Pink", Color::AnnotationPink }, { "Purple", Color::AnnotationPurple }, { "Blue", Color::AnnotationBlue },
        { "Green", Color::AnnotationGreen }, { "Black", Color::AnnotationBlack }
    };
    for (const bool enabled : { true, false })
    {
        QWidget* bar = new QWidget(builder.page());
        QHBoxLayout* barLayout = new QHBoxLayout(bar);
        barLayout->setContentsMargins(4, 4, 4, 4);
        barLayout->setSpacing(4);
        QButtonGroup* group = new QButtonGroup(bar);
        for (const NamedColor& namedColor : annotationColors)
        {
            pdf::PDFColorSwatch* swatch = new pdf::PDFColorSwatch(pdf::PDFStylePainter::color(namedColor.color), QString::fromLatin1(namedColor.name), bar);
            group->addButton(swatch);
            barLayout->addWidget(swatch);
            swatch->setSelected(namedColor.color == Color::AnnotationYellow);
        }
        pdf::PDFColorSwatch* noColor = new pdf::PDFColorSwatch(bar);
        noColor->setKind(pdf::PDFColorSwatch::Kind::None);
        group->addButton(noColor);
        barLayout->addWidget(noColor);
        bar->setEnabled(enabled);
        builder.live(enabled ? QStringLiteral("Live, exclusive group") : QStringLiteral("Live, disabled group"), bar);
    }
    pdf::PDFColorSwatch* hoveredSwatch = builder.live(QStringLiteral("Live, hover"), new pdf::PDFColorSwatch(pdf::PDFStylePainter::color(Color::AnnotationGreen), QStringLiteral("Green"), builder.page()));
    Helper::setState(hoveredSwatch, GalleryPages::Hover);

    // Icon choice: the icon chooser of the collections editor (283:1888, 284:2227)
    builder.block(QStringLiteral("Icon choice (PDFIconChoice) - icon chooser of the collections editor"), QStringLiteral("284:2227"));
    static constexpr const char* iconChoiceIcons[] = { "rotate-cw", "camera", "mail", "pen-tool", "briefcase", "printer", "download", "folder", "star", "zap" };
    for (const bool enabled : { true, false })
    {
        QWidget* chooser = new QWidget(builder.page());
        QHBoxLayout* chooserLayout = new QHBoxLayout(chooser);
        chooserLayout->setContentsMargins(4, 4, 4, 4);
        chooserLayout->setSpacing(6);
        QButtonGroup* group = new QButtonGroup(chooser);
        for (const char* iconName : iconChoiceIcons)
        {
            pdf::PDFIconChoice* choice = new pdf::PDFIconChoice(QString::fromLatin1(iconName), QString::fromLatin1(iconName), chooser);
            group->addButton(choice);
            chooserLayout->addWidget(choice, 0, Qt::AlignTop);
            choice->setSelected(iconName == iconChoiceIcons[0]);
        }
        chooser->setEnabled(enabled);
        builder.live(enabled ? QStringLiteral("Live, exclusive group") : QStringLiteral("Live, disabled group"), chooser);
    }
    pdf::PDFIconChoice* hoveredChoice = builder.live(QStringLiteral("Live, hover"), new pdf::PDFIconChoice(QStringLiteral("camera"), QStringLiteral("Camera"), builder.page()));
    Helper::setState(hoveredChoice, GalleryPages::Hover);

    // Row of the annotation colors: the Color row of the Annotate panel, the fill
    // of a shape tool (none and white) and a narrow bar (the rest through More)
    builder.block(QStringLiteral("Color swatch row (PDFColorSwatchRow) - 8 annotation colors + More, fill with none and white, visible limit"), QStringLiteral("877:10188"));
    pdf::PDFColorSwatchRow* annotationRow = new pdf::PDFColorSwatchRow(builder.page());
    annotationRow->setColor(pdf::PDFStylePainter::color(Color::AnnotationYellow));
    builder.live(QStringLiteral("Live, Annotate panel (877:10188)"), annotationRow, QStringLiteral("877:10188"));
    builder.row();
    pdf::PDFColorSwatchRow* fillRow = new pdf::PDFColorSwatchRow(builder.page());
    fillRow->setOptions(pdf::PDFColorSwatchRow::Option::NoColor | pdf::PDFColorSwatchRow::Option::White | pdf::PDFColorSwatchRow::Option::More);
    fillRow->setColor(pdf::PDFStylePainter::color(Color::AnnotationYellow));
    builder.live(QStringLiteral("Live, fill of a shape (207:7199)"), fillRow, QStringLiteral("207:7199"));
    builder.row();
    pdf::PDFColorSwatchRow* narrowRow = new pdf::PDFColorSwatchRow(builder.page());
    narrowRow->setVisibleSwatchLimit(4);
    narrowRow->setColor(QColor());
    builder.live(QStringLiteral("Live, narrow (4 visible, the rest through More)"), narrowRow);
    pdf::PDFColorSwatchRow* rightToLeftRow = new pdf::PDFColorSwatchRow(builder.page());
    rightToLeftRow->setLayoutDirection(Qt::RightToLeft);
    rightToLeftRow->setColor(pdf::PDFStylePainter::color(Color::AnnotationBlue));
    builder.live(QStringLiteral("Live right-to-left"), rightToLeftRow);
    pdf::PDFColorSwatchRow* disabledRow = new pdf::PDFColorSwatchRow(builder.page());
    disabledRow->setColor(pdf::PDFStylePainter::color(Color::AnnotationRed));
    disabledRow->setEnabled(false);
    builder.live(QStringLiteral("Live disabled"), disabledRow);
    builder.note(QStringLiteral("The swatches show the tokens of the current theme, the row reports the canonical light-theme values (the color written into a document never depends on the theme). More opens the Color palette popover with the same colors and the recently used ones."));

    // Reference point of the geometry dialog of an annotation (step 4, package B)
    builder.block(QStringLiteral("Reference point (PDFReferencePointPicker) - 3 x 3 points, click / arrows"), QStringLiteral("1024:21781"));
    pdf::PDFReferencePointPicker* referencePoint = new pdf::PDFReferencePointPicker(builder.page());
    referencePoint->setAccessibleName(QStringLiteral("Reference point"));
    builder.live(QStringLiteral("Live, top left (1024:21781)"), referencePoint, QStringLiteral("1024:21781"));
    pdf::PDFReferencePointPicker* referenceCenter = new pdf::PDFReferencePointPicker(builder.page());
    referenceCenter->setAlignment(Qt::AlignCenter);
    builder.live(QStringLiteral("Live, center"), referenceCenter);
    pdf::PDFReferencePointPicker* referenceDisabled = new pdf::PDFReferencePointPicker(builder.page());
    referenceDisabled->setCurrentIndex(8);
    referenceDisabled->setEnabled(false);
    builder.live(QStringLiteral("Live disabled"), referenceDisabled);
    builder.note(QStringLiteral("The grid is a picture of the corners of the annotation: it is not mirrored for right-to-left. The arrows move the chosen point, the focus ring is inside of the frame."));

    // Property row: Editable x State (7 variants of the design)
    builder.block(QStringLiteral("Property row (PDFPropertyRow) - Editable False / True, State Default / Hover / Copied / Editing"), QStringLiteral("206:873"));
    struct PropertyVariant
    {
        const char* name;
        bool editable;
        Painter::PropertyState state;
        int flags;
        const char* node;
    };
    static constexpr PropertyVariant propertyVariants[] =
    {
        { "Default", false, Painter::PropertyState::Default, GalleryPages::Default, "206:818" },
        { "Hover", false, Painter::PropertyState::Hover, GalleryPages::Default, "206:822" },
        { "Copied", false, Painter::PropertyState::Copied, GalleryPages::Default, "206:829" },
        { "Disabled (O-08)", false, Painter::PropertyState::Default, GalleryPages::Disabled, nullptr },
        { "Editable Default", true, Painter::PropertyState::Default, GalleryPages::Default, "206:837" },
        { "Editable Hover", true, Painter::PropertyState::Hover, GalleryPages::Default, "206:844" },
        { "Editable Copied", true, Painter::PropertyState::Copied, GalleryPages::Default, "206:854" },
        { "Editable Editing", true, Painter::PropertyState::Editing, GalleryPages::Default, "206:862" },
        { "Editable Disabled (O-08)", true, Painter::PropertyState::Default, GalleryPages::Disabled, nullptr }
    };
    for (const PropertyVariant& variant : propertyVariants)
    {
        Painter::PropertyRow content;
        content.key = QStringLiteral("Author");
        content.value = QStringLiteral("Jane Novak");
        content.editable = variant.editable;
        content.state = variant.state;
        const int flags = variant.flags;
        Helper::rowSpecimen(builder, QString::fromLatin1(variant.name), Painter::getPropertyRowSize(content), [content, flags](QPainter& painter, const QRectF& rect)
        {
            Painter::paintPropertyRow(&painter, rect, content, Helper::state(flags));
        }, variant.node ? QString::fromLatin1(variant.node) : QString());

        if (variant.flags == GalleryPages::Disabled)
        {
            builder.row();
        }
    }

    // Live rows of a properties card; the lifetime test deletes them in the timed
    // state Copied and with the inline editor open (the hints of the samples)
    using Editability = pdf::PDFPropertyRow::Editability;
    GalleryLifetimeHints propertyHints;
    propertyHints.prepare = &GalleryLifetimeHints::giveMenus;
    propertyHints.startTimers = [](QWidget* sample)
    {
        QList<pdf::PDFPropertyRow*> rows = sample->findChildren<pdf::PDFPropertyRow*>();
        if (pdf::PDFPropertyRow* row = qobject_cast<pdf::PDFPropertyRow*>(sample))
        {
            rows.prepend(row);
        }
        bool isEditing = false;
        for (pdf::PDFPropertyRow* row : rows)
        {
            if (row->isVisible())
            {
                row->copy();
                if (!isEditing && row->isEditable())
                {
                    isEditing = row->startEditing();
                }
            }
        }
    };
    QWidget* properties = Helper::createCard(builder.page());
    GalleryLifetimeHints::set(properties, propertyHints);
    properties->setFixedWidth(326 + 8);
    QLayout* propertiesLayout = properties->layout();
    propertiesLayout->addWidget(new pdf::PDFPropertyRow(QStringLiteral("Title"), QStringLiteral("Annual report 2026"), Editability::Editable, properties));
    propertiesLayout->addWidget(new pdf::PDFPropertyRow(QStringLiteral("Author"), QStringLiteral("Jane Novak"), Editability::Editable, properties));
    propertiesLayout->addWidget(new pdf::PDFPropertyRow(QStringLiteral("Producer"), QStringLiteral("PDF4QT 1.5.3 (Qt 6.11.2, Windows 11 x64, build of the release)"), properties));
    propertiesLayout->addWidget(new pdf::PDFPropertyRow(QStringLiteral("Created"), QStringLiteral("Sep 14, 2026 10:24"), properties));
    builder.live(QStringLiteral("Live card"), properties);
    builder.row();
    pdf::PDFPropertyRow* hoveredProperty = Helper::liveOnCard(builder, QStringLiteral("Live hover"), new pdf::PDFPropertyRow(QStringLiteral("Author"), QStringLiteral("Jane Novak"), Editability::Editable, builder.page()));
    hoveredProperty->setAttribute(Qt::WA_UnderMouse, true);
    GalleryLifetimeHints::set(hoveredProperty, propertyHints);
    builder.row();
    pdf::PDFPropertyRow* editedProperty = Helper::liveOnCard(builder, QStringLiteral("Live editing"), new pdf::PDFPropertyRow(QStringLiteral("Author"), QStringLiteral("Jane Novak"), Editability::Editable, builder.page()));
    editedProperty->startEditing();
    if (QWidget* editor = editedProperty->getEditor())
    {
        // The sample does not take the first focus of the window (no text cursor in the renders)
        editor->clearFocus();
    }
    builder.row();
    pdf::PDFPropertyRow* disabledProperty = Helper::liveOnCard(builder, QStringLiteral("Live disabled"), new pdf::PDFPropertyRow(QStringLiteral("Author"), QStringLiteral("Jane Novak"), Editability::Editable, builder.page()));
    disabledProperty->setEnabled(false);
    builder.row();
    pdf::PDFPropertyRow* rightToLeftProperty = new pdf::PDFPropertyRow(QStringLiteral("Author"), QStringLiteral("Jane Novak"), Editability::Editable, builder.page());
    rightToLeftProperty->setLayoutDirection(Qt::RightToLeft);
    GalleryLifetimeHints::set(rightToLeftProperty, propertyHints);
    Helper::liveOnCard(builder, QStringLiteral("Live right-to-left"), rightToLeftProperty);
    rightToLeftProperty->setAttribute(Qt::WA_UnderMouse, true);

    // Lock of a document tab: a click on the lock opens the popover of the locked
    // document (View Document 266:12010), as the application does (each instance
    // of the page has the connection, so the lifetime test deletes the tab bar and
    // the page also with the popover open)
    builder.block(QStringLiteral("Lock of a document tab (PDFDocumentTabBar::lockClicked) - the popover of the locked document"), QStringLiteral("266:12010"));
    QWidget* lockBar = new QWidget(builder.page());
    lockBar->setProperty(pdf::PDFWidgetStyleProperties::DOCUMENT_TABS, true);
    QHBoxLayout* lockBarLayout = new QHBoxLayout(lockBar);
    lockBarLayout->setContentsMargins(8, 0, 8, 0);
    pdf::PDFDocumentTabBar* lockTabs = new pdf::PDFDocumentTabBar(lockBar);
    lockTabs->setAccessibleName(QStringLiteral("Documents"));
    const int lockHomeTab = lockTabs->addTab(pdf::PDFThemeIcons::getIcon(QStringLiteral("home")), QStringLiteral("Home"));
    const int lockedTab = lockTabs->addTab(pdf::PDFThemeIcons::getIcon(QStringLiteral("file-text")), QStringLiteral("Project_Report.pdf"));
    lockTabs->setTabClosable(lockHomeTab, false);
    lockTabs->setTabClosable(lockedTab, true);
    lockTabs->setTabLocked(lockedTab, true);
    lockTabs->setCurrentIndex(lockedTab);
    lockBarLayout->addWidget(lockTabs);
    lockBarLayout->addStretch(1);
    lockBar->setFixedSize(520, 38);
    const auto connectLock = [](QWidget* sample)
    {
        for (pdf::PDFDocumentTabBar* tabBar : sample->findChildren<pdf::PDFDocumentTabBar*>())
        {
            QObject::connect(tabBar, &pdf::PDFDocumentTabBar::lockClicked, tabBar, [tabBar](int index)
            {
                // The popover menu of the style below the lock (the application has its own content)
                QMenu* popover = new QMenu(tabBar);
                pdf::PDFWidgetStyleProperties::set(popover, pdf::PDFWidgetStyleProperties::POPOVER);
                popover->setAttribute(Qt::WA_DeleteOnClose);
                popover->addAction(QStringLiteral("Unlock"));
                popover->addAction(QStringLiteral("Edit a Copy"));
                const QRect lock = tabBar->getTabLockRect(index);
                popover->popup(tabBar->mapToGlobal(QPoint(lock.center().x() - 24, tabBar->tabRect(index).bottom() + 7)));
            });
        }
    };
    connectLock(lockBar);
    builder.live(QStringLiteral("Live locked tab (click the lock)"), lockBar, QStringLiteral("266:12435"));

    // Reading theme card, Color mode card, the previews and Locked document
    addPanelsCards(builder);

    // Signature card, Page selection
    addPanelsPanels(builder);

    // Font row
    addPanelsDelegates(builder);

    return builder.finish();
}

}   // namespace pdffluentgallery
