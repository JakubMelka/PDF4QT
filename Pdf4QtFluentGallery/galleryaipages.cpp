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

// Samples of the frames of the page AI of Main page (step 7): status badges
// with the dot, avatars of the assistants, client rows, activity rows,
// assistant tiles and code boxes of the setup card.

#include "gallerypages.h"

#include "pdfaccenthue.h"
#include "pdfcomponentassistants.h"
#include "pdfcomponentbadges.h"
#include "pdfcomponentpainter.h"
#include "pdfstylepainter.h"
#include "pdfthemecolors.h"
#include "pdfwidgetstyleproperties.h"

#include <QAbstractButton>
#include <QApplication>
#include <QBoxLayout>
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QPainter>
#include <QVBoxLayout>
#include <QWidget>

#include <functional>

namespace pdffluentgallery
{

/// Helper functions of the samples of the page AI
class GalleryAiPagesHelper
{
public:
    GalleryAiPagesHelper() = delete;

    using Color = pdf::PDFThemeColors::Color;
    using Painter = pdf::PDFComponentPainter;
    using Properties = pdf::PDFWidgetStyleProperties;
    using PaintFunction = std::function<void(QPainter&, const QRectF&, const Painter::State&)>;

    struct NamedState
    {
        const char* name;
        int flags;
    };

    /// States of an interactive element (Figma has only the default one of each)
    static constexpr NamedState STATES[] =
    {
        { "Default", GalleryPages::Default },
        { "Hover", GalleryPages::Hover },
        { "Pressed", GalleryPages::Pressed | GalleryPages::Hover },
        { "Focus", GalleryPages::Focus },
        { "Disabled", GalleryPages::Disabled }
    };

    static Painter::State state(int flags, Qt::LayoutDirection direction)
    {
        return Painter::State(GalleryPages::toStyleState(GalleryPages::StateFlags(flags)), direction);
    }

    /// Adds a sample drawn by the painter over bg/surface (the elements lie on cards)
    static void specimen(GalleryBuilder& builder, const QString& caption, QSize size, int flags, const PaintFunction& paint, const QString& node = QString(),
                         Qt::LayoutDirection direction = QApplication::layoutDirection())
    {
        builder.specimen(caption, size, [paint, flags, direction](QPainter& painter, const QRect& rect)
        {
            const qreal margin = GallerySpecimen::MARGIN;
            pdf::PDFStylePainter::fillRect(&painter, QRectF(rect).adjusted(-margin, -margin, margin, margin), Color::BgSurface);
            paint(painter, QRectF(rect), state(flags, direction));
        }, node);
    }

    /// Returns a card of a side list (bg/surface with the border, padding 8, gap 2, 340 wide)
    static QWidget* createListCard(QWidget* parent, int width = 340)
    {
        QWidget* card = new QWidget(parent);
        Properties::set(card, Properties::CARD);
        QVBoxLayout* layout = new QVBoxLayout(card);
        layout->setContentsMargins(8, 8, 8, 8);
        layout->setSpacing(2);
        card->setFixedWidth(width);
        return card;
    }

    /// Avatars of the clients of the design
    static Painter::AssistantAvatar claude(int extent = 32, int fontSize = 12) { return Painter::AssistantAvatar{ QStringLiteral("Cl"), pdf::PDFAccentHue::Amber, extent, fontSize, true }; }
    static Painter::AssistantAvatar gemini() { return Painter::AssistantAvatar{ QStringLiteral("Ge"), pdf::PDFAccentHue::Blue, 32, 12, true }; }
    static Painter::AssistantAvatar codex() { return Painter::AssistantAvatar{ QStringLiteral("Cx"), pdf::PDFAccentHue::Green, 32, 12, true }; }

    /// Activity rows of 653:5231, 653:5814, 653:6384 and 940:135321
    static Painter::ActivityRow waiting()
    {
        return Painter::ActivityRow{ QStringLiteral("Open document"), QStringLiteral("16:44"), QStringLiteral("Claude Code · Q3_Forecast.pdf"),
                                     QStringLiteral("Waiting for your confirmation · "), QStringLiteral("Show"), QStringLiteral("hourglass"),
                                     pdf::PDFAccentHue::Amber, Painter::ActivityStatusTone::Secondary };
    }

    static Painter::ActivityRow waitingOtherWindow()
    {
        return Painter::ActivityRow{ QStringLiteral("Open document"), QStringLiteral("16:44"), QStringLiteral("Claude Code · Q3_Forecast.pdf"),
                                     QStringLiteral("Waiting for confirmation in the other PDF4QT window"), QString(), QStringLiteral("hourglass"),
                                     pdf::PDFAccentHue::Amber, Painter::ActivityStatusTone::Secondary };
    }

    static Painter::ActivityRow done()
    {
        return Painter::ActivityRow{ QStringLiteral("Read text"), QStringLiteral("16:42"), QStringLiteral("Claude Code · Project_Report.pdf · pages 1–12"),
                                     QString(), QString(), QStringLiteral("check"), pdf::PDFAccentHue::Green, Painter::ActivityStatusTone::Muted };
    }

    static Painter::ActivityRow allowed()
    {
        return Painter::ActivityRow{ QStringLiteral("Add 1 note"), QStringLiteral("16:39"), QStringLiteral("Codex CLI · Invoice.pdf"),
                                     QStringLiteral("Allowed by you"), QString(), QStringLiteral("check"), pdf::PDFAccentHue::Green, Painter::ActivityStatusTone::Muted };
    }

    static Painter::ActivityRow timeout()
    {
        return Painter::ActivityRow{ QStringLiteral("Save document"), QStringLiteral("16:38"), QStringLiteral("Gemini CLI · Invoice.pdf"),
                                     QStringLiteral("Denied automatically · no answer in 2 min"), QString(), QStringLiteral("timer-off"), pdf::PDFAccentHue::Neutral,
                                     Painter::ActivityStatusTone::Muted };
    }

    static Painter::ActivityRow denied()
    {
        return Painter::ActivityRow{ QStringLiteral("Delete 2 annotations"), QStringLiteral("16:35"), QStringLiteral("Gemini CLI · Invoice.pdf"),
                                     QStringLiteral("Denied by you"), QString(), QStringLiteral("user-x"), pdf::PDFAccentHue::Neutral, Painter::ActivityStatusTone::Muted };
    }

    static Painter::ActivityRow locked()
    {
        return Painter::ActivityRow{ QStringLiteral("Add 1 note"), QStringLiteral("16:36"), QStringLiteral("Claude Code · Contract_2026.pdf"),
                                     QStringLiteral("Denied · the document is locked"), QString(), QStringLiteral("lock"), pdf::PDFAccentHue::Neutral, Painter::ActivityStatusTone::Muted };
    }

    static Painter::ActivityRow failed()
    {
        return Painter::ActivityRow{ QStringLiteral("Add 3 highlights"), QStringLiteral("16:31"), QStringLiteral("Claude Code · Project_Report.pdf"),
                                     QStringLiteral("Failed · The document changed in the meantime"), QString(), QStringLiteral("alert-circle"), pdf::PDFAccentHue::Red,
                                     Painter::ActivityStatusTone::Danger };
    }

    /// Tiles of 653:5690 in the order of the design
    static QList<Painter::AssistantTile> tiles()
    {
        return {
            Painter::AssistantTile{ QStringLiteral("Claude"), QStringLiteral("Anthropic"), Painter::AssistantAvatar{ QStringLiteral("Cl"), pdf::PDFAccentHue::Amber, 36, 13, false } },
            Painter::AssistantTile{ QStringLiteral("Codex"), QStringLiteral("OpenAI"), Painter::AssistantAvatar{ QStringLiteral("Cx"), pdf::PDFAccentHue::Green, 36, 13, false } },
            Painter::AssistantTile{ QStringLiteral("Gemini"), QStringLiteral("Google"), Painter::AssistantAvatar{ QStringLiteral("Ge"), pdf::PDFAccentHue::Blue, 36, 13, false } },
            Painter::AssistantTile{ QStringLiteral("Copilot"), QStringLiteral("GitHub"), Painter::AssistantAvatar{ QStringLiteral("Co"), pdf::PDFAccentHue::Violet, 36, 13, false } },
            Painter::AssistantTile{ QStringLiteral("Cursor"), QStringLiteral("Anysphere"), Painter::AssistantAvatar{ QStringLiteral("Cu"), pdf::PDFAccentHue::Neutral, 36, 13, false } },
            Painter::AssistantTile{ QStringLiteral("Other client"), QStringLiteral("Any MCP client"), Painter::AssistantAvatar{ QString(), pdf::PDFAccentHue::Teal, 36, 13, false } }
        };
    }

    static QString command() { return QStringLiteral("claude mcp add --transport http pdf4qt http://127.0.0.1:8765/mcp --header \"Authorization: Bearer q7LmT4…x2Vd\""); }

    static QString configuration()
    {
        return QStringLiteral("{\n  \"mcpServers\": {\n    \"pdf4qt\": {\n      \"type\": \"http\",\n      \"url\": \"http://127.0.0.1:8765/mcp\",\n      \"headers\": {\n"
                              "        \"Authorization\": \"Bearer q7LmT4…x2Vd\"\n      }\n    }\n  }\n}");
    }
};

GalleryPages::Page GalleryPages::createAiPage(QWidget* parent)
{
    using Helper = GalleryAiPagesHelper;
    using Painter = pdf::PDFComponentPainter;

    GalleryBuilder builder(parent, QStringLiteral("24-ai"), QStringLiteral("AI"), QStringLiteral("ai"));
    QWidget* page = builder.page();

    // Status badges of the server card
    builder.block(QStringLiteral("Status badge with the dot (PDFStatusBadge::setDotVisible) - the server card"), QStringLiteral("653:5453"));
    struct BadgeSample
    {
        const char* text;
        pdf::PDFAccentHue hue;
        bool danger;
        const char* node;
    };
    static constexpr BadgeSample badgeSamples[] =
    {
        { "Running", pdf::PDFAccentHue::Green, false, "653:5453" },
        { "Remote access", pdf::PDFAccentHue::Amber, false, "653:6609" },
        { "In another PDF4QT window", pdf::PDFAccentHue::Blue, false, "940:135372" },
        { "Stopped", pdf::PDFAccentHue::Neutral, false, "653:6036" },
        { "Could not start", pdf::PDFAccentHue::Red, true, "955:148639" }
    };
    for (const BadgeSample& sample : badgeSamples)
    {
        Painter::StatusBadge badge{ QString::fromUtf8(sample.text), QString(), sample.hue, true, std::nullopt };
        if (sample.danger)
        {
            badge.textColor = pdf::PDFThemeColors::TextDanger;
        }
        Helper::specimen(builder, badge.text, Painter::getStatusBadgeSize(badge), GalleryPages::Default,
                         [badge](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintStatusBadge(&painter, rect, badge, state); },
                         QString::fromLatin1(sample.node));
    }
    builder.row();
    for (const BadgeSample& sample : badgeSamples)
    {
        pdf::PDFStatusBadge* badge = new pdf::PDFStatusBadge(QString::fromUtf8(sample.text), QString(), sample.hue, page);
        badge->setDotVisible(true);
        if (sample.danger)
        {
            badge->setTextColor(pdf::PDFThemeColors::TextDanger);
        }
        builder.live(QStringLiteral("Live %1").arg(QString::fromUtf8(sample.text)), badge, QString::fromLatin1(sample.node));
    }
    builder.row();

    // Avatars
    builder.block(QStringLiteral("Assistant avatar (PDFAssistantAvatar) - rows 32, dialogs and tiles 36, CLI badge, unknown client"), QStringLiteral("955:143479"));
    const Painter::AssistantAvatar avatarSamples[] =
    {
        Helper::claude(), Helper::gemini(), Helper::codex(), Helper::claude(36, 13),
        Painter::AssistantAvatar{ QString(), pdf::PDFAccentHue::Teal, 36, 13, false }, Painter::AssistantAvatar{ QStringLiteral("Cl"), pdf::PDFAccentHue::Amber, 32, 12, false }
    };
    for (const Painter::AssistantAvatar& avatar : avatarSamples)
    {
        const QString caption = QStringLiteral("%1 %2%3").arg(avatar.monogram.isEmpty() ? QStringLiteral("mcp") : avatar.monogram).arg(avatar.extent).arg(avatar.commandLine ? QStringLiteral(" CLI") : QString());
        Helper::specimen(builder, caption, Painter::getAssistantAvatarSize(avatar), GalleryPages::Default,
                         [avatar](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintAssistantAvatar(&painter, rect, avatar, state); });
    }
    Helper::specimen(builder, QStringLiteral("Disabled"), Painter::getAssistantAvatarSize(Helper::claude()), GalleryPages::Disabled,
                     [](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintAssistantAvatar(&painter, rect, Helper::claude(), state); });
    Helper::specimen(builder, QStringLiteral("Right-to-left"), Painter::getAssistantAvatarSize(Helper::claude()), GalleryPages::Default,
                     [](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintAssistantAvatar(&painter, rect, Helper::claude(), state); }, QString(), Qt::RightToLeft);
    builder.row();
    builder.live(QStringLiteral("Live 32 CLI"), new pdf::PDFAssistantAvatar(Helper::claude(), page), QStringLiteral("955:143479"));
    builder.live(QStringLiteral("Live 36 CLI (dialog)"), new pdf::PDFAssistantAvatar(Helper::claude(36, 13), page), QStringLiteral("955:147882"));
    builder.live(QStringLiteral("Live unknown client"), new pdf::PDFAssistantAvatar(Painter::AssistantAvatar{ QString(), pdf::PDFAccentHue::Teal, 36, 13, false }, page), QStringLiteral("653:5723"));
    builder.row();

    // Client rows
    builder.block(QStringLiteral("Client row (PDFAssistantRow) - local, remote with the tag, nested disconnect button"), QStringLiteral("653:5591"));
    const Painter::AssistantRow localRow{ QStringLiteral("Claude Code"), QStringLiteral("This computer · connected at 14:02"), QString(), Helper::claude(), true, true };
    const Painter::AssistantRow remoteRow{ QStringLiteral("Codex CLI"), QStringLiteral("192.168.1.57 · connected at 16:05"), QStringLiteral("Remote"), Helper::codex(), true, true };
    Helper::specimen(builder, QStringLiteral("Local"), QSize(322, Painter::getAssistantRowMinimumSize(localRow).height()), GalleryPages::Default,
                     [localRow](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintAssistantRow(&painter, rect, localRow, state); }, QStringLiteral("653:5591"));
    Helper::specimen(builder, QStringLiteral("Remote"), QSize(322, Painter::getAssistantRowMinimumSize(remoteRow).height()), GalleryPages::Default,
                     [remoteRow](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintAssistantRow(&painter, rect, remoteRow, state); }, QStringLiteral("653:6767"));
    builder.row();
    Helper::specimen(builder, QStringLiteral("Disabled"), QSize(322, 48), GalleryPages::Disabled,
                     [localRow](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintAssistantRow(&painter, rect, localRow, state); });
    Helper::specimen(builder, QStringLiteral("Remote, right-to-left"), QSize(322, 52), GalleryPages::Default,
                     [remoteRow](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintAssistantRow(&painter, rect, remoteRow, state); }, QString(), Qt::RightToLeft);
    Helper::specimen(builder, QStringLiteral("Narrow (elided)"), Painter::getAssistantRowMinimumSize(remoteRow), GalleryPages::Default,
                     [remoteRow](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintAssistantRow(&painter, rect, remoteRow, state); });
    builder.row();
    for (const bool rightToLeft : { false, true })
    {
        QWidget* clients = Helper::createListCard(page);
        clients->setLayoutDirection(rightToLeft ? Qt::RightToLeft : Qt::LeftToRight);
        clients->layout()->addWidget(new pdf::PDFAssistantRow(localRow, clients));
        clients->layout()->addWidget(new pdf::PDFAssistantRow(Painter::AssistantRow{ QStringLiteral("Gemini CLI"), QStringLiteral("This computer · connected at 15:40"), QString(), Helper::gemini(), true, true }, clients));
        clients->layout()->addWidget(new pdf::PDFAssistantRow(remoteRow, clients));
        builder.live(rightToLeft ? QStringLiteral("Live right-to-left") : QStringLiteral("Live clients-list"), clients, QStringLiteral("653:6750"));
    }
    builder.row();

    // Activity rows
    builder.block(QStringLiteral("Activity row (PDFActivityRow) - every status of the design, only the waiting row is clickable"), QStringLiteral("955:147547"));
    struct ActivitySample
    {
        const char* caption;
        Painter::ActivityRow (*row)();
        const char* node;
    };
    static constexpr ActivitySample activitySamples[] =
    {
        { "Waiting (62)", &Helper::waiting, "955:147547" },
        { "Read text (47)", &Helper::done, "955:147558" },
        { "Timeout", &Helper::timeout, "955:147567" },
        { "Denied", &Helper::denied, "955:147577" },
        { "Failed", &Helper::failed, "955:147587" },
        { "Locked", &Helper::locked, "955:147832" },
        { "Allowed", &Helper::allowed, "955:147764" },
        { "Waiting in the other window (75)", &Helper::waitingOtherWindow, "955:147695" }
    };
    int sampleIndex = 0;
    for (const ActivitySample& sample : activitySamples)
    {
        const Painter::ActivityRow row = sample.row();
        Helper::specimen(builder, QString::fromLatin1(sample.caption), QSize(322, Painter::getActivityRowHeight(row, 322)), GalleryPages::Default,
                         [row](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintActivityRow(&painter, rect, row, state); }, QString::fromLatin1(sample.node));
        if (++sampleIndex % 4 == 0)
        {
            builder.row();
        }
    }
    for (const Helper::NamedState& namedState : Helper::STATES)
    {
        const Painter::ActivityRow row = Helper::waiting();
        Helper::specimen(builder, QStringLiteral("Clickable %1 (no Figma state)").arg(QString::fromLatin1(namedState.name)), QSize(322, Painter::getActivityRowHeight(row, 322)), namedState.flags,
                         [row](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintActivityRow(&painter, rect, row, state); });
    }
    builder.row();
    Helper::specimen(builder, QStringLiteral("Failed, right-to-left"), QSize(322, 62), GalleryPages::Default,
                     [](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintActivityRow(&painter, rect, Helper::failed(), state); }, QString(), Qt::RightToLeft);
    Helper::specimen(builder, QStringLiteral("Narrow (the status wraps)"), QSize(200, Painter::getActivityRowHeight(Helper::waiting(), 200)), GalleryPages::Default,
                     [](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintActivityRow(&painter, rect, Helper::waiting(), state); });
    builder.row();
    for (const bool rightToLeft : { false, true })
    {
        QWidget* activity = Helper::createListCard(page);
        activity->setLayoutDirection(rightToLeft ? Qt::RightToLeft : Qt::LeftToRight);
        for (const ActivitySample& sample : activitySamples)
        {
            pdf::PDFActivityRow* row = new pdf::PDFActivityRow(sample.row(), activity);
            row->setClickable(sample.row == &Helper::waiting);
            activity->layout()->addWidget(row);
        }
        builder.live(rightToLeft ? QStringLiteral("Live right-to-left") : QStringLiteral("Live activity-list"), activity, QStringLiteral("653:5612"));
    }
    QWidget* activityStates = Helper::createListCard(page);
    pdf::PDFActivityRow* hoveredRow = new pdf::PDFActivityRow(Helper::waiting(), activityStates);
    hoveredRow->setClickable(true);
    hoveredRow->setAttribute(Qt::WA_UnderMouse, true);
    pdf::PDFActivityRow* disabledRow = new pdf::PDFActivityRow(Helper::waiting(), activityStates);
    disabledRow->setClickable(true);
    disabledRow->setEnabled(false);
    activityStates->layout()->addWidget(hoveredRow);
    activityStates->layout()->addWidget(disabledRow);
    builder.live(QStringLiteral("Live hover, disabled"), activityStates);
    builder.row();

    // Assistant tiles
    builder.block(QStringLiteral("Assistant tile (PDFAssistantTile) - static choices of an exclusive group"), QStringLiteral("653:5690"));
    const QList<Painter::AssistantTile> tiles = Helper::tiles();
    const QSize tileSize = Painter::getAssistantTileSize(tiles.front());
    for (int i = 0; i < 5; ++i)
    {
        const Painter::AssistantTile tile = tiles.front();
        const int flags = Helper::STATES[i].flags | (i == 0 ? GalleryPages::On : GalleryPages::Default);
        Helper::specimen(builder, QStringLiteral("Claude %1").arg(i == 0 ? QStringLiteral("Selected") : QString::fromLatin1(Helper::STATES[i].name)), tileSize, flags,
                         [tile](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintAssistantTile(&painter, rect, tile, state); },
                         i == 0 ? QStringLiteral("653:5691") : QString());
    }
    builder.row();
    const Painter::AssistantTile otherTile = tiles.back();
    Helper::specimen(builder, QStringLiteral("Other client"), tileSize, GalleryPages::Default,
                     [otherTile](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintAssistantTile(&painter, rect, otherTile, state); }, QStringLiteral("653:5723"));
    Helper::specimen(builder, QStringLiteral("Other client selected (two lines)"), tileSize, GalleryPages::On,
                     [otherTile](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintAssistantTile(&painter, rect, otherTile, state); }, QStringLiteral("955:149394"));
    Helper::specimen(builder, QStringLiteral("Selected disabled"), tileSize, GalleryPages::On | GalleryPages::Disabled,
                     [otherTile](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintAssistantTile(&painter, rect, otherTile, state); });
    Helper::specimen(builder, QStringLiteral("Selected right-to-left"), tileSize, GalleryPages::On,
                     [otherTile](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintAssistantTile(&painter, rect, otherTile, state); }, QString(), Qt::RightToLeft);
    Helper::specimen(builder, QStringLiteral("Minimal width"), Painter::getAssistantTileMinimumSize(otherTile), GalleryPages::On,
                     [otherTile](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintAssistantTile(&painter, rect, otherTile, state); });
    builder.row();
    for (const bool enabled : { true, false })
    {
        QWidget* row = new QWidget(page);
        row->setFixedWidth(1166);
        QHBoxLayout* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(12);
        QButtonGroup* group = new QButtonGroup(row);
        for (int i = 0; i < tiles.size(); ++i)
        {
            pdf::PDFAssistantTile* tile = new pdf::PDFAssistantTile(tiles.at(i), row);
            group->addButton(tile);
            layout->addWidget(tile);
            tile->setSelected(enabled ? i == 0 : i == tiles.size() - 1);
        }
        row->setEnabled(enabled);
        builder.live(enabled ? QStringLiteral("Live assistant-tiles, Claude selected") : QStringLiteral("Live disabled, Other client selected"), row, QStringLiteral("653:5690"));
        builder.row();
    }
    QWidget* tileStates = new QWidget(page);
    tileStates->setFixedWidth(600);
    QHBoxLayout* tileStatesLayout = new QHBoxLayout(tileStates);
    tileStatesLayout->setContentsMargins(0, 0, 0, 0);
    tileStatesLayout->setSpacing(12);
    pdf::PDFAssistantTile* hoveredTile = new pdf::PDFAssistantTile(tiles.at(1), tileStates);
    hoveredTile->setAttribute(Qt::WA_UnderMouse, true);
    pdf::PDFAssistantTile* rightToLeftTile = new pdf::PDFAssistantTile(tiles.back(), tileStates);
    rightToLeftTile->setLayoutDirection(Qt::RightToLeft);
    rightToLeftTile->setSelected(true);
    tileStatesLayout->addWidget(hoveredTile);
    tileStatesLayout->addWidget(rightToLeftTile);
    builder.live(QStringLiteral("Live hover, selected right-to-left"), tileStates);
    builder.row();

    // Code boxes
    builder.block(QStringLiteral("Code box (PDFCodeBox) - a command, a prompt in Inter, a configuration; the copy Toolbar button"), QStringLiteral("653:5754"));
    const Painter::CodeBox commandBox{ Helper::command(), true, true };
    const Painter::CodeBox promptBox{ QStringLiteral("Summarize the document that is open in PDF4QT."), false, true };
    const Painter::CodeBox configurationBox{ Helper::configuration(), true, true };
    Helper::specimen(builder, QStringLiteral("Command (one line)"), QSize(1098, Painter::getCodeBoxHeight(commandBox, 1098)), GalleryPages::Default,
                     [commandBox](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintCodeBox(&painter, rect, commandBox, state); }, QStringLiteral("653:5754"));
    builder.row();
    Helper::specimen(builder, QStringLiteral("Prompt (Inter)"), QSize(520, Painter::getCodeBoxHeight(promptBox, 520)), GalleryPages::Default,
                     [promptBox](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintCodeBox(&painter, rect, promptBox, state); }, QStringLiteral("653:5781"));
    Helper::specimen(builder, QStringLiteral("Configuration (11 lines)"), QSize(520, Painter::getCodeBoxHeight(configurationBox, 520)), GalleryPages::Default,
                     [configurationBox](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintCodeBox(&painter, rect, configurationBox, state); }, QStringLiteral("955:149431"));
    builder.row();
    Helper::specimen(builder, QStringLiteral("Command wrapped at 560"), QSize(560, Painter::getCodeBoxHeight(commandBox, 560)), GalleryPages::Default,
                     [commandBox](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintCodeBox(&painter, rect, commandBox, state); });
    Helper::specimen(builder, QStringLiteral("Focus of the text, right-to-left"), QSize(520, 46), GalleryPages::Focus,
                     [promptBox](QPainter& painter, const QRectF& rect, const Painter::State& state) { Painter::paintCodeBox(&painter, rect, promptBox, state); }, QString(), Qt::RightToLeft);
    builder.row();

    pdf::PDFCodeBox* liveCommand = new pdf::PDFCodeBox(QStringLiteral("claude mcp add --transport http pdf4qt http://127.0.0.1:8765/mcp --header \"Authorization: Bearer q7LmT4…x2Vd\""), page);
    liveCommand->setCopyText(QStringLiteral("claude mcp add --transport http pdf4qt http://127.0.0.1:8765/mcp --header \"Authorization: Bearer q7LmT4kR9wPz2Vd\""));
    liveCommand->setAccessibleName(QStringLiteral("Setup command"));
    liveCommand->setFixedWidth(1098);
    builder.live(QStringLiteral("Live command"), liveCommand, QStringLiteral("653:5754"));
    builder.row();
    QWidget* codeRow = new QWidget(page);
    codeRow->setFixedWidth(1098);
    QHBoxLayout* codeLayout = new QHBoxLayout(codeRow);
    codeLayout->setContentsMargins(0, 0, 0, 0);
    codeLayout->setSpacing(24);
    pdf::PDFCodeBox* livePrompt = new pdf::PDFCodeBox(promptBox.text, codeRow);
    livePrompt->setMonospace(false);
    pdf::PDFCodeBox* liveConfiguration = new pdf::PDFCodeBox(configurationBox.text, codeRow);
    codeLayout->addWidget(livePrompt, 1, Qt::AlignTop);
    codeLayout->addWidget(liveConfiguration, 1, Qt::AlignTop);
    builder.live(QStringLiteral("Live prompt and configuration"), codeRow, QStringLiteral("955:149431"));
    builder.row();
    pdf::PDFCodeBox* narrowCommand = new pdf::PDFCodeBox(Helper::command(), page);
    narrowCommand->setFixedWidth(560);
    narrowCommand->setLayoutDirection(Qt::RightToLeft);
    builder.live(QStringLiteral("Live wrapped at 560, right-to-left"), narrowCommand);
    pdf::PDFCodeBox* disabledCommand = new pdf::PDFCodeBox(QStringLiteral("claude mcp list"), page);
    disabledCommand->setFixedWidth(520);
    disabledCommand->setEnabled(false);
    builder.live(QStringLiteral("Live disabled"), disabledCommand);
    builder.row();

    builder.note(QStringLiteral("Figma has no hover, pressed, focus or disabled states of the clickable activity row and of the assistant tiles; they follow the other rows and cards."));
    return builder.finish();
}

}   // namespace pdffluentgallery
