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

// Samples of the composite widgets of the components (step 4 of components.md,
// chapter 6.3) on the pages of their chapters: Continue with, Strength meter,
// Color button, Color palette popover, Signature card and Page selection.

#include "gallerypages.h"

#include "pdfaccenthue.h"
#include "pdfcomponentbuttons.h"
#include "pdfcomponentcards.h"
#include "pdfcomponentpainter.h"
#include "pdfcomponentpanels.h"
#include "pdffluentwidgets.h"
#include "pdfstylepainter.h"
#include "pdfthemecolors.h"
#include "pdfwidgetstyleproperties.h"

#include <QApplication>
#include <QLineEdit>
#include <QPainter>
#include <QSizeF>
#include <QVBoxLayout>
#include <QWidget>

#include <functional>

namespace pdffluentgallery
{

/// Helper functions of the samples of the composite widgets
class GalleryPanelPagesHelper
{
public:
    GalleryPanelPagesHelper() = delete;

    using Color = pdf::PDFThemeColors::Color;
    using Painter = pdf::PDFComponentPainter;

    /// Returns the state of the painter for the flags of a sample
    static Painter::State state(int flags, Qt::LayoutDirection direction = QApplication::layoutDirection())
    {
        return Painter::State(GalleryPages::toStyleState(GalleryPages::StateFlags(flags)), direction);
    }

    /// Fills the sample (with its margin) by bg/surface: the components lie on
    /// bg/surface cards and panels on the screens
    static void fillSurface(QPainter& painter, const QRectF& rect)
    {
        const qreal margin = GallerySpecimen::MARGIN;
        pdf::PDFStylePainter::fillRect(&painter, rect.adjusted(-margin, -margin, margin, margin), Color::BgSurface);
    }

    /// Adds a sample drawn over bg/surface by \p paint(painter, rect, state)
    static void specimen(GalleryBuilder& builder, const QString& caption, QSize size, int flags, Qt::LayoutDirection direction,
                         const std::function<void(QPainter&, const QRectF&, const Painter::State&)>& paint, const QString& node = QString())
    {
        builder.specimen(caption, size, [paint, flags, direction](QPainter& painter, const QRect& rect)
        {
            fillSurface(painter, QRectF(rect));
            paint(painter, QRectF(rect), state(flags, direction));
        }, node);
    }

    /// Returns a card (bg/surface with the border of the style), on which a live widget lies
    static QWidget* createCard(QWidget* parent, QWidget* content, int margin = 8)
    {
        QWidget* card = new QWidget(parent);
        pdf::PDFWidgetStyleProperties::set(card, pdf::PDFWidgetStyleProperties::CARD);
        card->setLayoutDirection(content->layoutDirection());
        QVBoxLayout* layout = new QVBoxLayout(card);
        layout->setContentsMargins(margin, margin, margin, margin);
        content->setParent(card);
        layout->addWidget(content);
        return card;
    }

    // ------------------------------------------------------------ Continue with

    static QList<Painter::ToolChoice> getToolChoices()
    {
        return { Painter::ToolChoice{ QStringLiteral("Add Page Numbers"), QStringLiteral("hash"), pdf::PDFAccentHue::Green },
                 Painter::ToolChoice{ QStringLiteral("Compress"), QStringLiteral("minimize-2"), pdf::PDFAccentHue::Violet },
                 Painter::ToolChoice{ QStringLiteral("Protect with Password"), QStringLiteral("lock"), pdf::PDFAccentHue::Red } };
    }

    /// Choices of the screen pages-merge-done (418:32221): every name fits one line
    static QList<Painter::ToolChoice> getShortToolChoices()
    {
        return { Painter::ToolChoice{ QStringLiteral("Compress"), QStringLiteral("minimize-2"), pdf::PDFAccentHue::Violet },
                 Painter::ToolChoice{ QStringLiteral("Protect with Password"), QStringLiteral("lock"), pdf::PDFAccentHue::Red } };
    }

    static Painter::ContinueWith getContinueWith(int choiceCount, int selected, bool isShort = false)
    {
        Painter::ContinueWith content;
        content.title = QStringLiteral("Then continue with");
        content.note = QStringLiteral("optional");
        content.linkText = QStringLiteral("Another tool…");
        content.choices = (isShort ? getShortToolChoices() : getToolChoices()).mid(0, choiceCount);
        content.selectedChoice = selected;
        content.caption = selected >= 0 ? QStringLiteral("%1 starts on the new document after Merge finishes successfully.").arg(content.choices.at(selected).name)
                                        : QStringLiteral("Nothing is chosen – the tool just finishes.");
        return content;
    }

    static pdf::PDFContinueWith* createContinueWith(QWidget* parent, int choiceCount)
    {
        pdf::PDFContinueWith* widget = new pdf::PDFContinueWith(parent);
        widget->setChoices(getToolChoices().mid(0, choiceCount));
        widget->setCurrentToolName(QStringLiteral("Merge"));
        widget->setFixedWidth(480);
        return widget;
    }

    // ----------------------------------------------------------- Strength meter

    static pdf::PDFComponentPainter::StrengthCriteria getLevelCriteria(Painter::StrengthLevel level)
    {
        using Criterion = Painter::StrengthCriterion;
        switch (level)
        {
            case Painter::StrengthLevel::Weak:
                return Criterion::Length;
            case Painter::StrengthLevel::Moderate:
                return Criterion::Length | Criterion::Case;
            case Painter::StrengthLevel::Strong:
                return Criterion::Length | Criterion::Case | Criterion::Digits;
            case Painter::StrengthLevel::VeryStrong:
                return Criterion::Length | Criterion::Case | Criterion::Digits | Criterion::Symbols;
            default:
                break;
        }
        return Criterion::None;
    }

    static Painter::StrengthMeter getStrengthMeter(Painter::StrengthLevel level)
    {
        Painter::StrengthMeter meter;
        meter.level = level;
        meter.title = QStringLiteral("Password strength");
        meter.detail = QStringLiteral("118 of 128 bits");
        meter.satisfied = getLevelCriteria(level);
        return meter;
    }

    // ------------------------------------------------------------ Color palette

    static QList<pdf::PDFColorPalette::Entry> getSuggestedColors()
    {
        struct NamedToken
        {
            const char* name;
            Color color;
        };
        static constexpr NamedToken tokens[] =
        {
            { "Paper", Color::ReadingPaperText }, { "Soft gray", Color::ReadingSoftGrayText }, { "Sepia", Color::ReadingSepiaText },
            { "Mint", Color::ReadingMintText }, { "Night", Color::ReadingNightText }, { "Black", Color::ReadingBlackText },
            { "Terminal green", Color::ReadingTerminalText }, { "Amber", Color::ReadingAmberText }, { "High contrast", Color::ReadingHighContrastText },
            { "Custom", Color::ReadingCustomText }, { "Dark gray", Color::SampleGrayDark }, { "Blue", Color::SampleBlue }
        };

        QList<pdf::PDFColorPalette::Entry> entries;
        for (const NamedToken& token : tokens)
        {
            entries << pdf::PDFColorPalette::Entry{ pdf::PDFStylePainter::color(token.color), QString::fromLatin1(token.name) };
        }
        return entries;
    }

    static QList<pdf::PDFColorPalette::Entry> getRecentColors()
    {
        return { pdf::PDFColorPalette::Entry{ pdf::PDFStylePainter::color(Color::ReadingTerminalText), QStringLiteral("Terminal green") },
                 pdf::PDFColorPalette::Entry{ pdf::PDFStylePainter::color(Color::SampleOrange), QStringLiteral("Orange") },
                 pdf::PDFColorPalette::Entry{ pdf::PDFStylePainter::color(Color::ReadingCustomText), QStringLiteral("Custom") } };
    }

    static Painter::ColorPalette getColorPalette(bool withRecent)
    {
        Painter::ColorPalette palette;
        for (const pdf::PDFColorPalette::Entry& entry : getSuggestedColors())
        {
            palette.suggested << entry.color;
        }
        if (withRecent)
        {
            for (const pdf::PDFColorPalette::Entry& entry : getRecentColors())
            {
                palette.recent << entry.color;
            }
        }
        palette.selected = 6;
        palette.suggestedLabel = QStringLiteral("Suggested");
        palette.recentLabel = QStringLiteral("Recently used");
        palette.moreText = QStringLiteral("More colors…");
        return palette;
    }

    static pdf::PDFColorPalette* createPalette(QWidget* parent)
    {
        pdf::PDFColorPalette* palette = new pdf::PDFColorPalette(parent);
        palette->setSuggestedColors(getSuggestedColors());
        palette->setRecentColors(getRecentColors());
        palette->setCurrentColor(pdf::PDFStylePainter::color(Color::ReadingTerminalText));
        return palette;
    }

    static pdf::PDFColorButton* createColorButton(QWidget* parent)
    {
        pdf::PDFColorButton* button = new pdf::PDFColorButton(pdf::PDFStylePainter::color(Color::ReadingPaperText), QStringLiteral("Black"), parent);
        button->getPalette()->setSuggestedColors(getSuggestedColors());
        button->getPalette()->setRecentColors(getRecentColors());
        button->setFixedWidth(260);
        return button;
    }

    // ----------------------------------------------------------- Signature card

    static Painter::SignatureCard getSignatureCard(Painter::SignatureStatus status, bool expanded)
    {
        using Status = Painter::SignatureStatus;

        Painter::SignatureCard card;
        card.signer = QStringLiteral("Jane Novak");
        card.meta = QStringLiteral("Digital signature · Sep 28, 2026");
        card.summary = QStringLiteral("The document has not been changed since it was signed.");
        card.status = status;
        card.expanded = expanded;
        card.chainCaption = QStringLiteral("Certificate chain");
        card.details = { Painter::SignatureDetail{ QStringLiteral("Signed on"), QStringLiteral("Sep 28, 2026, 14:02") },
                         Painter::SignatureDetail{ QStringLiteral("Hash algorithm"), QStringLiteral("SHA-256") },
                         Painter::SignatureDetail{ QStringLiteral("Covers"), QStringLiteral("Entire document") },
                         Painter::SignatureDetail{ QStringLiteral("Timestamp"), status == Status::Invalid ? QStringLiteral("Not embedded") : QStringLiteral("Embedded, valid") } };

        Painter::SignatureCertificate signer{ QStringLiteral("Jane Novak"), QStringLiteral("Signer · RSA 2048 · valid until Mar 14, 2028"), Status::Valid, QString() };
        if (status == Status::Invalid)
        {
            signer.info = QStringLiteral("Signer · RSA 2048 · revoked on Sep 2, 2026");
            signer.status = Status::Invalid;
        }

        const Painter::SignatureCertificate intermediate{ QStringLiteral("Example Issuing CA 2"), QStringLiteral("Intermediate · RSA 4096 · valid until Jan 5, 2031"), Status::Valid, QString() };
        Painter::SignatureCertificate root{ QStringLiteral("Example Root CA"), QStringLiteral("Trusted root certificate"), Status::Valid, QString() };
        if (status == Status::Warning)
        {
            root.info = QStringLiteral("Not among trusted certificates");
            root.status = Status::Warning;
            root.linkText = QStringLiteral("Add to trusted certificates");
        }

        card.certificates = { signer, intermediate, root };
        return card;
    }

    static pdf::PDFSignatureCard* createSignatureCard(QWidget* parent, Painter::SignatureStatus status, bool expanded)
    {
        const Painter::SignatureCard content = getSignatureCard(status, expanded);
        pdf::PDFSignatureCard* card = new pdf::PDFSignatureCard(parent);
        card->setSigner(content.signer);
        card->setMeta(content.meta);
        card->setSummary(content.summary);
        card->setStatus(content.status);
        card->setDetails(content.details);
        card->setCertificates(content.certificates);
        card->setExpanded(content.expanded);
        card->setFixedWidth(276);
        return card;
    }

    // ----------------------------------------------------------- Page selection

    static QStringList getPageLabels()
    {
        // The front matter has roman labels, the body arabic numbers from 1
        static const char* romans[] = { "i", "ii", "iii", "iv", "v", "vi", "vii", "viii", "ix", "x", "xi", "xii" };
        QStringList labels;
        for (int page = 1; page <= 48; ++page)
        {
            labels << (page <= 12 ? QString::fromLatin1(romans[page - 1]) : QString::number(page - 12));
        }
        return labels;
    }

    static pdf::PDFPageSelection* createPageSelection(QWidget* parent, pdf::PDFPageSelection::Mode mode, bool error)
    {
        pdf::PDFPageSelection* selection = new pdf::PDFPageSelection(parent);
        selection->setPageCount(48);
        selection->setPageLabels(getPageLabels());
        selection->setCurrentPage(3);
        selection->setSelectedPages({ 3, 4, 5, 8, 10, 11, 12 });

        QList<QSizeF> sizes;
        for (int page = 1; page <= 48; ++page)
        {
            sizes << (page % 7 == 0 ? QSizeF(842, 595) : QSizeF(595, 842));
        }
        selection->setPageSizes(sizes);

        selection->setRange(error && mode == pdf::PDFPageSelection::Mode::Range ? QStringLiteral("1-3, 60") : QStringLiteral("1-3, 8, 10-12"));
        selection->setMode(mode);
        if (error && mode != pdf::PDFPageSelection::Mode::Range)
        {
            // The error of the design in a mode without the field
            selection->setCustomError(QStringLiteral("Page 60 does not exist – the document has 48 pages"));
        }
        selection->setFixedWidth(320);
        return selection;
    }
};

void GalleryPages::addCatalogPanels(GalleryBuilder& builder)
{
    using Helper = GalleryPanelPagesHelper;
    using Painter = pdf::PDFComponentPainter;

    builder.block(QStringLiteral("Continue with (PDFContinueWith) - zero to three Tool choices dividing the width, deselectable exclusive group"), QStringLiteral("417:1007"));

    struct Sample
    {
        const char* name;
        int choices;
        int selected;
        int flags;
        bool rightToLeft;
        bool isShort;
        const char* node;
    };
    static constexpr Sample samples[] =
    {
        { "Default (three choices)", 3, -1, GalleryPages::Default, false, false, "417:1007" },
        { "Compress selected (after Merge, 418:32494)", 3, 1, GalleryPages::Default, false, false, "418:32494" },
        { "Two choices", 2, -1, GalleryPages::Default, false, false, nullptr },
        { "Names of one line: 91 high (418:32221)", 2, -1, GalleryPages::Default, false, true, "418:32221" },
        { "One choice", 1, -1, GalleryPages::Default, false, false, nullptr },
        { "No choice (37 high, O-10)", 0, -1, GalleryPages::Default, false, false, nullptr },
        { "Disabled (O-08)", 3, 0, GalleryPages::Disabled, false, false, nullptr },
        { "Right-to-left", 3, 2, GalleryPages::Default, true, false, nullptr }
    };
    for (const Sample& sample : samples)
    {
        const Painter::ContinueWith content = Helper::getContinueWith(sample.choices, sample.selected, sample.isShort);
        Helper::specimen(builder, QString::fromLatin1(sample.name), Painter::getContinueWithSize(content), sample.flags, sample.rightToLeft ? Qt::RightToLeft : Qt::LeftToRight,
                         [content](QPainter& painter, const QRectF& rect, const Painter::State& state)
        {
            Painter::paintContinueWith(&painter, rect, content, state);
        }, sample.node ? QString::fromLatin1(sample.node) : QString());
        builder.row();
    }

    QWidget* page = builder.page();
    builder.live(QStringLiteral("Live"), Helper::createContinueWith(page, 3));
    builder.row();
    pdf::PDFContinueWith* selected = Helper::createContinueWith(page, 3);
    selected->setSelectedChoice(0);
    builder.live(QStringLiteral("Live, Add Page Numbers selected"), selected);
    builder.row();
    builder.live(QStringLiteral("Live, two choices"), Helper::createContinueWith(page, 2));
    builder.row();
    pdf::PDFContinueWith* disabled = Helper::createContinueWith(page, 3);
    disabled->setEnabled(false);
    builder.live(QStringLiteral("Live disabled"), disabled);
    builder.row();
    pdf::PDFContinueWith* rightToLeft = Helper::createContinueWith(page, 3);
    rightToLeft->setLayoutDirection(Qt::RightToLeft);
    rightToLeft->setSelectedChoice(1);
    builder.live(QStringLiteral("Live right-to-left"), rightToLeft);
    builder.row();
    pdf::PDFContinueWith* narrow = Helper::createContinueWith(page, 3);
    narrow->setSelectedChoice(2);
    narrow->setFixedWidth(narrow->minimumSizeHint().width());
    builder.live(QStringLiteral("Live, minimal width (the names wrap by words, the caption wraps)"), narrow);
}

void GalleryPages::addFormsPanels(GalleryBuilder& builder)
{
    using Helper = GalleryPanelPagesHelper;
    using Painter = pdf::PDFComponentPainter;
    using Level = Painter::StrengthLevel;
    using Color = pdf::PDFThemeColors::Color;

    // Strength meter: the six levels of the design
    builder.block(QStringLiteral("Strength meter (PDFStrengthMeter) - Level, 20 cells, criteria"), QStringLiteral("455:1270"));
    struct LevelSample
    {
        const char* name;
        Level level;
        const char* node;
    };
    static constexpr LevelSample levels[] =
    {
        { "Empty", Level::Empty, "455:1012" },
        { "Very weak", Level::VeryWeak, "455:1055" },
        { "Weak", Level::Weak, "455:1098" },
        { "Moderate", Level::Moderate, "455:1141" },
        { "Strong", Level::Strong, "455:1184" },
        { "Very strong", Level::VeryStrong, "455:1227" }
    };
    for (int i = 0; i < 6; ++i)
    {
        const Painter::StrengthMeter meter = Helper::getStrengthMeter(levels[i].level);
        Helper::specimen(builder, QString::fromLatin1(levels[i].name), Painter::getStrengthMeterSize(meter), GalleryPages::Default, QApplication::layoutDirection(),
                         [meter](QPainter& painter, const QRectF& rect, const Painter::State& state)
        {
            Painter::paintStrengthMeter(&painter, rect, meter, state);
        }, QString::fromLatin1(levels[i].node));

        if (i == 2 || i == 5)
        {
            builder.row();
        }
    }

    {
        Painter::StrengthMeter encryption = Helper::getStrengthMeter(Level::Strong);
        encryption.title = QStringLiteral("Encryption strength");
        encryption.detail = QStringLiteral("AES-256");
        encryption.criteriaVisible = false;
        Helper::specimen(builder, QStringLiteral("Show criteria false (an algorithm)"), Painter::getStrengthMeterSize(encryption), GalleryPages::Default, QApplication::layoutDirection(),
                         [encryption](QPainter& painter, const QRectF& rect, const Painter::State& state)
        {
            Painter::paintStrengthMeter(&painter, rect, encryption, state);
        });

        Painter::StrengthMeter withoutDetail = Helper::getStrengthMeter(Level::Moderate);
        withoutDetail.detailVisible = false;
        Helper::specimen(builder, QStringLiteral("Show detail false"), Painter::getStrengthMeterSize(withoutDetail), GalleryPages::Default, QApplication::layoutDirection(),
                         [withoutDetail](QPainter& painter, const QRectF& rect, const Painter::State& state)
        {
            Painter::paintStrengthMeter(&painter, rect, withoutDetail, state);
        });

        const Painter::StrengthMeter moderate = Helper::getStrengthMeter(Level::Moderate);
        Helper::specimen(builder, QStringLiteral("Disabled (O-08)"), Painter::getStrengthMeterSize(moderate), GalleryPages::Disabled, QApplication::layoutDirection(),
                         [moderate](QPainter& painter, const QRectF& rect, const Painter::State& state)
        {
            Painter::paintStrengthMeter(&painter, rect, moderate, state);
        });
        builder.row();

        Helper::specimen(builder, QStringLiteral("Right-to-left"), Painter::getStrengthMeterSize(moderate), GalleryPages::Default, Qt::RightToLeft,
                         [moderate](QPainter& painter, const QRectF& rect, const Painter::State& state)
        {
            Painter::paintStrengthMeter(&painter, rect, moderate, state);
        });

        const Painter::StrengthMeter strong = Helper::getStrengthMeter(Level::VeryStrong);
        const QSize narrowSize(180, Painter::getStrengthMeterHeight(strong, 180));
        Helper::specimen(builder, QStringLiteral("180 wide (the criteria wrap)"), narrowSize, GalleryPages::Default, QApplication::layoutDirection(),
                         [strong](QPainter& painter, const QRectF& rect, const Painter::State& state)
        {
            Painter::paintStrengthMeter(&painter, rect, strong, state);
        });
        builder.row();
    }

    // Live meter below a password field: the gallery measures the password roughly
    QWidget* passwordCard = new QWidget(builder.page());
    QVBoxLayout* passwordLayout = new QVBoxLayout(passwordCard);
    passwordLayout->setContentsMargins(0, 0, 0, 0);
    passwordLayout->setSpacing(8);
    pdf::PDFLineEdit* password = new pdf::PDFLineEdit(passwordCard);
    password->setPasswordField();
    password->setPlaceholderText(QStringLiteral("Password"));
    pdf::PDFStrengthMeter* liveMeter = new pdf::PDFStrengthMeter(passwordCard);
    liveMeter->setDetail(QStringLiteral("0 bits"));
    passwordLayout->addWidget(password);
    passwordLayout->addWidget(liveMeter);
    passwordCard->setFixedWidth(300);
    QObject::connect(password, &QLineEdit::textChanged, liveMeter, [liveMeter](const QString& text)
    {
        using Criterion = Painter::StrengthCriterion;
        Painter::StrengthCriteria criteria;
        bool hasLower = false;
        bool hasUpper = false;
        for (const QChar character : text)
        {
            hasLower = hasLower || character.isLower();
            hasUpper = hasUpper || character.isUpper();
            if (character.isDigit())
            {
                criteria |= Criterion::Digits;
            }
            else if (!character.isLetter())
            {
                criteria |= Criterion::Symbols;
            }
        }
        if (text.size() >= 12)
        {
            criteria |= Criterion::Length;
        }
        if (hasLower && hasUpper)
        {
            criteria |= Criterion::Case;
        }

        const int bits = qMin(128, static_cast<int>(text.size()) * 6);
        static constexpr Level byBits[] = { Level::VeryWeak, Level::VeryWeak, Level::Weak, Level::Moderate, Level::Strong, Level::VeryStrong };
        liveMeter->setLevel(text.isEmpty() ? Level::Empty : byBits[qMin(5, bits / 24)]);
        liveMeter->setFilledCells(text.isEmpty() ? -1 : qMax(1, bits * 20 / 128));
        liveMeter->setDetail(QStringLiteral("%1 of 128 bits").arg(bits));
        liveMeter->setSatisfiedCriteria(criteria);
    });
    builder.live(QStringLiteral("Live with a password field (type)"), passwordCard);

    pdf::PDFStrengthMeter* weakMeter = new pdf::PDFStrengthMeter(builder.page());
    weakMeter->setLevel(Level::Weak);
    weakMeter->setDetail(QStringLiteral("42 of 128 bits"));
    weakMeter->setSatisfiedCriteria(Painter::StrengthCriterion::Length);
    weakMeter->setFixedWidth(300);
    builder.live(QStringLiteral("Live Weak"), weakMeter);

    pdf::PDFStrengthMeter* disabledMeter = new pdf::PDFStrengthMeter(builder.page());
    disabledMeter->setLevel(Level::Strong);
    disabledMeter->setDetail(QStringLiteral("101 of 128 bits"));
    disabledMeter->setSatisfiedCriteria(Helper::getLevelCriteria(Level::Strong));
    disabledMeter->setEnabled(false);
    disabledMeter->setFixedWidth(300);
    builder.live(QStringLiteral("Live disabled"), disabledMeter);
    builder.row();

    pdf::PDFStrengthMeter* rightToLeftMeter = new pdf::PDFStrengthMeter(builder.page());
    rightToLeftMeter->setLevel(Level::Moderate);
    rightToLeftMeter->setDetail(QStringLiteral("70 of 128 bits"));
    rightToLeftMeter->setSatisfiedCriteria(Helper::getLevelCriteria(Level::Moderate));
    rightToLeftMeter->setLayoutDirection(Qt::RightToLeft);
    rightToLeftMeter->setFixedWidth(300);
    builder.live(QStringLiteral("Live right-to-left"), rightToLeftMeter);

    pdf::PDFStrengthMeter* narrowMeter = new pdf::PDFStrengthMeter(builder.page());
    narrowMeter->setLevel(Level::VeryStrong);
    narrowMeter->setDetail(QStringLiteral("128 bits"));
    narrowMeter->setSatisfiedCriteria(Helper::getLevelCriteria(Level::VeryStrong));
    narrowMeter->setFixedWidth(180);
    builder.live(QStringLiteral("Live, 180 wide"), narrowMeter);

    // Color button: the five states of the design
    builder.block(QStringLiteral("Color button (PDFColorButton) - State, opens the Color palette popover"), QStringLiteral("156:4239"));
    struct ButtonState
    {
        const char* name;
        int flags;
        const char* node;
    };
    static constexpr ButtonState buttonStates[] =
    {
        { "Default", GalleryPages::Default, "156:4219" },
        { "Hover", GalleryPages::Hover, "156:4224" },
        { "Pressed", GalleryPages::Pressed | GalleryPages::Hover, "156:4229" },
        { "Focus", GalleryPages::Focus, "156:4234" },
        { "Disabled", GalleryPages::Disabled, "706:149577" }
    };
    const Painter::ColorButton blackButton{ pdf::PDFStylePainter::color(Color::ReadingPaperText), QStringLiteral("Black"), false };
    for (const ButtonState& buttonState : buttonStates)
    {
        Helper::specimen(builder, QString::fromLatin1(buttonState.name), Painter::getColorButtonSize(blackButton), buttonState.flags, QApplication::layoutDirection(),
                         [blackButton](QPainter& painter, const QRectF& rect, const Painter::State& state)
        {
            Painter::paintColorButton(&painter, rect, blackButton, state);
        }, QString::fromLatin1(buttonState.node));

        if (buttonState.flags == int(GalleryPages::Pressed | GalleryPages::Hover))
        {
            builder.row();
        }
    }
    builder.row();

    const Painter::ColorButton openButton{ pdf::PDFStylePainter::color(Color::ReadingTerminalText), QStringLiteral("Terminal green"), true };
    Helper::specimen(builder, QStringLiteral("Palette open (drawn pressed)"), Painter::getColorButtonSize(openButton), GalleryPages::Default, QApplication::layoutDirection(),
                     [openButton](QPainter& painter, const QRectF& rect, const Painter::State& state)
    {
        Painter::paintColorButton(&painter, rect, openButton, state);
    });
    const Painter::ColorButton noColorButton{ QColor(), QStringLiteral("No color"), false };
    Helper::specimen(builder, QStringLiteral("No color"), Painter::getColorButtonSize(noColorButton), GalleryPages::Default, QApplication::layoutDirection(),
                     [noColorButton](QPainter& painter, const QRectF& rect, const Painter::State& state)
    {
        Painter::paintColorButton(&painter, rect, noColorButton, state);
    });
    builder.row();
    Helper::specimen(builder, QStringLiteral("Right-to-left focus"), Painter::getColorButtonSize(blackButton), GalleryPages::Focus, Qt::RightToLeft,
                     [blackButton](QPainter& painter, const QRectF& rect, const Painter::State& state)
    {
        Painter::paintColorButton(&painter, rect, blackButton, state);
    });
    builder.row();

    builder.live(QStringLiteral("Live (click opens the palette)"), Helper::createColorButton(builder.page()));
    pdf::PDFColorButton* hoveredButton = Helper::createColorButton(builder.page());
    hoveredButton->setAttribute(Qt::WA_UnderMouse, true);
    builder.live(QStringLiteral("Live hover"), hoveredButton);
    builder.row();
    pdf::PDFColorButton* disabledButton = Helper::createColorButton(builder.page());
    disabledButton->setEnabled(false);
    builder.live(QStringLiteral("Live disabled"), disabledButton);
    pdf::PDFColorButton* rightToLeftButton = Helper::createColorButton(builder.page());
    rightToLeftButton->setLayoutDirection(Qt::RightToLeft);
    builder.live(QStringLiteral("Live right-to-left"), rightToLeftButton);

    // Color palette popover: the panel and an inline palette
    builder.block(QStringLiteral("Color palette popover (PDFColorPalette) - round swatches six in a row, the selected color, More colors…"), QStringLiteral("156:4240"));
    const Painter::ColorPalette palette = Helper::getColorPalette(true);
    Helper::specimen(builder, QStringLiteral("Panel (the shadow belongs to the window)"), Painter::getColorPaletteSize(palette), GalleryPages::Default, QApplication::layoutDirection(),
                     [palette](QPainter& painter, const QRectF& rect, const Painter::State& state)
    {
        Painter::paintColorPalette(&painter, rect, palette, state);
    }, QStringLiteral("156:4240"));
    const Painter::ColorPalette withoutRecent = Helper::getColorPalette(false);
    Helper::specimen(builder, QStringLiteral("Without recent colors"), Painter::getColorPaletteSize(withoutRecent), GalleryPages::Default, QApplication::layoutDirection(),
                     [withoutRecent](QPainter& painter, const QRectF& rect, const Painter::State& state)
    {
        Painter::paintColorPalette(&painter, rect, withoutRecent, state);
    });
    Helper::specimen(builder, QStringLiteral("Right-to-left"), Painter::getColorPaletteSize(palette), GalleryPages::Default, Qt::RightToLeft,
                     [palette](QPainter& painter, const QRectF& rect, const Painter::State& state)
    {
        Painter::paintColorPalette(&painter, rect, palette, state);
    });
    builder.row();

    builder.live(QStringLiteral("Live, inline (not a popover)"), Helper::createPalette(builder.page()));
    pdf::PDFColorPalette* rightToLeftPalette = Helper::createPalette(builder.page());
    rightToLeftPalette->setLayoutDirection(Qt::RightToLeft);
    builder.live(QStringLiteral("Live right-to-left"), rightToLeftPalette);
    builder.note(QStringLiteral("The design keeps the colors inline in a panel, where there is room for them (a row of Color swatches); the popover of the Color button is for the other places. The popover is a translucent window of the style with the shadow 0 / 8 / 24."));
}

void GalleryPages::addPanelsPanels(GalleryBuilder& builder)
{
    using Helper = GalleryPanelPagesHelper;
    using Painter = pdf::PDFComponentPainter;
    using Status = Painter::SignatureStatus;
    using Mode = pdf::PDFPageSelection::Mode;

    // Signature card: Status x Expanded
    builder.block(QStringLiteral("Signature card (PDFSignatureCard) - Status x Expanded, the toggle is an Icon button 24"), QStringLiteral("192:1032"));
    struct CardSample
    {
        const char* name;
        Status status;
        bool expanded;
        const char* node;
    };
    static constexpr CardSample cardSamples[] =
    {
        { "Valid", Status::Valid, false, "192:840" },
        { "Valid expanded", Status::Valid, true, "192:853" },
        { "Warning", Status::Warning, false, "192:903" },
        { "Warning expanded", Status::Warning, true, "192:916" },
        { "Invalid", Status::Invalid, false, "192:969" },
        { "Invalid expanded", Status::Invalid, true, "192:982" }
    };
    for (const CardSample& sample : cardSamples)
    {
        const Painter::SignatureCard card = Helper::getSignatureCard(sample.status, sample.expanded);
        Helper::specimen(builder, QString::fromLatin1(sample.name), Painter::getSignatureCardSize(card), GalleryPages::Default, QApplication::layoutDirection(),
                         [card](QPainter& painter, const QRectF& rect, const Painter::State& state)
        {
            Painter::paintSignatureCard(&painter, rect, card, state);
        }, QString::fromLatin1(sample.node));
    }
    builder.row();

    const Painter::SignatureCard disabledCard = Helper::getSignatureCard(Status::Warning, true);
    Helper::specimen(builder, QStringLiteral("Disabled (O-08)"), Painter::getSignatureCardSize(disabledCard), GalleryPages::Disabled, QApplication::layoutDirection(),
                     [disabledCard](QPainter& painter, const QRectF& rect, const Painter::State& state)
    {
        Painter::paintSignatureCard(&painter, rect, disabledCard, state);
    });
    Helper::specimen(builder, QStringLiteral("Right-to-left"), Painter::getSignatureCardSize(disabledCard), GalleryPages::Default, Qt::RightToLeft,
                     [disabledCard](QPainter& painter, const QRectF& rect, const Painter::State& state)
    {
        Painter::paintSignatureCard(&painter, rect, disabledCard, state);
    });
    builder.row();

    QWidget* page = builder.page();
    builder.live(QStringLiteral("Live (the toggle expands it)"), Helper::createSignatureCard(page, Status::Valid, false));
    builder.live(QStringLiteral("Live Warning expanded"), Helper::createSignatureCard(page, Status::Warning, true));
    pdf::PDFSignatureCard* disabledSignature = Helper::createSignatureCard(page, Status::Invalid, true);
    disabledSignature->setEnabled(false);
    builder.live(QStringLiteral("Live disabled"), disabledSignature);
    pdf::PDFSignatureCard* rightToLeftSignature = Helper::createSignatureCard(page, Status::Invalid, true);
    rightToLeftSignature->setLayoutDirection(Qt::RightToLeft);
    builder.live(QStringLiteral("Live right-to-left"), rightToLeftSignature);
    builder.row();

    // Page selection: Mode x Error (live widgets: the segments, the field and the check boxes are drawn by the style)
    builder.block(QStringLiteral("Page selection (PDFPageSelection) - Mode x Error, live widgets on a card"), QStringLiteral("265:985"));
    struct SelectionSample
    {
        const char* name;
        Mode mode;
        bool error;
        const char* node;
    };
    static constexpr SelectionSample selectionSamples[] =
    {
        { "Mode All", Mode::All, false, "265:871" },
        { "Mode Current", Mode::Current, false, "265:909" },
        { "Mode Selected", Mode::Selected, false, "265:947" },
        { "Mode Range", Mode::Range, false, "264:853" },
        { "Mode All, error of the application", Mode::All, true, "706:147295" },
        { "Mode Current, error of the application", Mode::Current, true, "706:147349" },
        { "Mode Selected, error of the application", Mode::Selected, true, "706:147403" },
        { "Mode Range, invalid range", Mode::Range, true, "706:147457" }
    };
    for (int i = 0; i < 8; ++i)
    {
        const SelectionSample& sample = selectionSamples[i];
        builder.live(QString::fromLatin1(sample.name), Helper::createCard(page, Helper::createPageSelection(page, sample.mode, sample.error)), QString::fromLatin1(sample.node));
        if (i == 3 || i == 7)
        {
            builder.row();
        }
    }

    pdf::PDFPageSelection* filtered = Helper::createPageSelection(page, Mode::Range, false);
    filtered->setFilters(pdf::PDFPageSelection::Filter::Odd | pdf::PDFPageSelection::Filter::Portrait);
    builder.live(QStringLiteral("Live, Odd pages and Portrait"), Helper::createCard(page, filtered));
    pdf::PDFPageSelection* disabledSelection = Helper::createPageSelection(page, Mode::Range, false);
    disabledSelection->setEnabled(false);
    builder.live(QStringLiteral("Live disabled (decision 15)"), Helper::createCard(page, disabledSelection));
    pdf::PDFPageSelection* rightToLeftSelection = Helper::createPageSelection(page, Mode::Range, true);
    rightToLeftSelection->setLayoutDirection(Qt::RightToLeft);
    builder.live(QStringLiteral("Live right-to-left"), Helper::createCard(page, rightToLeftSelection));
    pdf::PDFPageSelection* sidebarSelection = Helper::createPageSelection(page, Mode::Selected, false);
    sidebarSelection->setFixedWidth(279);
    builder.live(QStringLiteral("Live, 279 wide (side panel)"), Helper::createCard(page, sidebarSelection));
    builder.row();

    // The error of two lines in a side panel (instance 385:11956): the icon is centered on the lines
    pdf::PDFPageSelection* wrappedError = Helper::createPageSelection(page, Mode::Range, true);
    wrappedError->setRange(QStringLiteral("1-3, 1600"));
    wrappedError->setFixedWidth(279);
    builder.live(QStringLiteral("Live, 279 wide, error of two lines (385:11956)"), Helper::createCard(page, wrappedError), QStringLiteral("385:11956"));
}

}   // namespace pdffluentgallery
