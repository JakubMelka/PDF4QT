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

#include "pdfapppagenotfound.h"
#include "pdfappworkspace.h"

#include "pdfaccenthue.h"
#include "pdffluentwidgets.h"
#include "pdfstylepainter.h"
#include "pdfstylepainterguard.h"
#include "pdfthemefonts.h"
#include "pdfthemeiconengine.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyleproperties.h"

#include <QEvent>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QHBoxLayout>
#include <QPainter>
#include <QPushButton>
#include <QTextDocument>
#include <QVBoxLayout>

namespace pdf
{

/// Circle of the icon of Page Not Found (icon-circle: accent/neutral/bg 72, icon/signpost 32)
class PDFAppNotFoundIcon : public QWidget
{
public:
    explicit PDFAppNotFoundIcon(QWidget* parent) :
        QWidget(parent)
    {
        setFixedSize(72, 72);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

protected:
    virtual void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        PDFStylePainterGuard guard(&painter);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF circle(0, 0, 72, 72);
        PDFStylePainter::fill(&painter, circle, PDFStylePainter::Radii(36.0), PDFAccentHueColors::getColor(PDFAccentHue::Neutral, PDFAccentHueColors::Role::Background));
        PDFStylePainter::icon(&painter, PDFStylePainter::centered(circle, QSizeF(32.0, 32.0)), QStringLiteral("signpost"), PDFThemeColors::IconDefault);
    }
};

/// Box of the requested page (path: bg/subtle, border/default, radius 8, paddings 16 / 8)
class PDFAppRequestedPathBox : public QWidget
{
public:
    explicit PDFAppRequestedPathBox(QWidget* parent) :
        QWidget(parent)
    {

    }

protected:
    virtual void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        PDFStylePainterGuard guard(&painter);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF box = QRectF(rect());
        PDFStylePainter::fill(&painter, box, PDFStylePainter::Radii(8.0), PDFThemeColors::BgSubtle);
        PDFStylePainter::strokeInside(&painter, box, PDFStylePainter::Radii(8.0), PDFThemeColors::BorderDefault);
    }
};

/// Helpers of Page Not Found
class PDFPageNotFoundPageHelper
{
public:
    PDFPageNotFoundPageHelper() = delete;

    /// Label of a role with a color, plain text
    static PDFLabel* createLabel(const QString& text, const QFont& font, PDFThemeColors::Color color, QWidget* parent)
    {
        PDFLabel* label = new PDFLabel(text, PDFThemeFonts::Role::Body, color, parent);
        label->setFont(font);
        label->setTextFormat(Qt::PlainText);
        return label;
    }

    /// Secondary button with an icon of the design
    static QPushButton* createButton(const QString& text, const QString& iconName, QWidget* parent)
    {
        QPushButton* button = new QPushButton(PDFThemeIcons::getIcon(iconName), text, parent);
        button->setAutoDefault(false);
        return button;
    }

    /// The requested text as it is shown: invisible characters (bidirectional
    /// overrides, zero width characters, separators of lines) would let a link
    /// look like another one, they are shown percent-encoded; a lone surrogate
    /// is the replacement character
    static QString toVisibleText(const QString& text)
    {
        QString result;
        result.reserve(text.size());
        for (qsizetype i = 0; i < text.size(); ++i)
        {
            const QChar character = text[i];
            if (character.isHighSurrogate() && i + 1 < text.size() && text[i + 1].isLowSurrogate())
            {
                const char32_t codePoint = QChar::surrogateToUcs4(character, text[i + 1]);
                const QString pair = text.mid(i, 2);
                result += isInvisible(QChar::category(codePoint)) ? toPercentEncoding(pair) : pair;
                ++i;
            }
            else if (character.isSurrogate())
            {
                result += QChar(QChar::ReplacementCharacter);
            }
            else
            {
                result += isInvisible(character.category()) ? toPercentEncoding(QString(character)) : QString(character);
            }
        }
        return result;
    }

    /// Text of a tooltip: the tooltip of Qt shows a text, which looks like HTML,
    /// as rich text (formatting, pictures), so such a text is escaped
    static QString toToolTip(const QString& text)
    {
        if (!Qt::mightBeRichText(text))
        {
            return text;
        }
        return QStringLiteral("<p style=\"white-space:pre-wrap\">%1</p>").arg(text.toHtmlEscaped());
    }

    /// Text of a button: '&' of the data is not a mnemonic
    static QString toButtonText(const QString& text)
    {
        return QString(text).replace(QLatin1Char('&'), QLatin1String("&&"));
    }

    static bool isInvisible(QChar::Category category)
    {
        return category == QChar::Other_Control || category == QChar::Other_Format || category == QChar::Separator_Line || category == QChar::Separator_Paragraph;
    }

    static QString toPercentEncoding(const QString& text)
    {
        QString result;
        for (const char byte : text.toUtf8())
        {
            result += QStringLiteral("%%1").arg(int(static_cast<unsigned char>(byte)), 2, 16, QLatin1Char('0')).toUpper();
        }
        return result;
    }

    /// Text elided to the width (a route can be 10 kB long)
    static QString elide(const QString& text, const QFont& font, Qt::TextElideMode mode, int width)
    {
        // A text longer than any width is cut first, QFontMetricsF measures it fast then
        const QString shortened = text.size() > 512 ? text.left(512) + QChar(0x2026) : text;
        return QFontMetricsF(font).elidedText(shortened, mode, width);
    }

    static QFont getPathFont()
    {
        // JetBrains Mono 12 in the design (not bundled): a monospaced font of the system
        QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        font.setFamilies({ QStringLiteral("JetBrains Mono"), QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas"), QStringLiteral("DejaVu Sans Mono"),
                           QStringLiteral("Menlo"), font.family(), QStringLiteral("Courier New") });
        font.setStyleHint(QFont::Monospace);
        font.setPixelSize(12);
        return font;
    }
};

PDFAppKeyCap::PDFAppKeyCap(const QString& text, QWidget* parent) :
    BaseClass(parent),
    m_text(text)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFont(PDFThemeFonts::getFont(QFont::Medium, 10));
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&PDFAppKeyCap::update));
}

QSize PDFAppKeyCap::sizeHint() const
{
    // Paddings 6 / 2 and the border 1, the line of the text 12 (height 18)
    const qreal textWidth = PDFStylePainter::textWidth(m_text, font());
    return QSize(qCeil(textWidth) + 2 * 6 + 2, 18);
}

QSize PDFAppKeyCap::minimumSizeHint() const
{
    return sizeHint();
}

void PDFAppKeyCap::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    PDFStylePainterGuard guard(&painter);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF box = QRectF(rect());
    PDFStylePainter::fill(&painter, box, PDFStylePainter::Radii(4.0), PDFThemeColors::BgSubtle);
    PDFStylePainter::strokeInside(&painter, box, PDFStylePainter::Radii(4.0), PDFThemeColors::BorderDefault);
    PDFStylePainter::text(&painter, box, Qt::AlignCenter | Qt::TextSingleLine, m_text, font(), PDFStylePainter::color(PDFThemeColors::TextSecondary));
}

QStringList PDFAppKeyCap::getKeyNames(const QKeySequence& shortcut)
{
    QStringList names;
    if (shortcut.isEmpty())
    {
        return names;
    }

    // The names of the modifiers as the key caps of the design (and as macOS shows them)
    const QKeyCombination combination = shortcut[0];
    const Qt::KeyboardModifiers modifiers = combination.keyboardModifiers();
#if defined(Q_OS_MACOS)
    if (modifiers.testFlag(Qt::MetaModifier))
    {
        names << QString(QChar(0x2303));
    }
    if (modifiers.testFlag(Qt::AltModifier))
    {
        names << QString(QChar(0x2325));
    }
    if (modifiers.testFlag(Qt::ShiftModifier))
    {
        names << QString(QChar(0x21E7));
    }
    if (modifiers.testFlag(Qt::ControlModifier))
    {
        names << QString(QChar(0x2318));
    }
#else
    if (modifiers.testFlag(Qt::ControlModifier))
    {
        names << tr("Ctrl");
    }
    if (modifiers.testFlag(Qt::AltModifier))
    {
        names << tr("Alt");
    }
    if (modifiers.testFlag(Qt::ShiftModifier))
    {
        names << tr("Shift");
    }
    if (modifiers.testFlag(Qt::MetaModifier))
    {
        names << tr("Meta");
    }
#endif

    switch (combination.key())
    {
        case Qt::Key_Left:
            names << QString(QChar(0x2190));
            break;
        case Qt::Key_Right:
            names << QString(QChar(0x2192));
            break;
        case Qt::Key_Up:
            names << QString(QChar(0x2191));
            break;
        case Qt::Key_Down:
            names << QString(QChar(0x2193));
            break;
        default:
            names << QKeySequence(combination.key()).toString(QKeySequence::NativeText);
            break;
    }

    names.removeAll(QString());
    return names;
}

PDFPageNotFoundPage::PDFPageNotFoundPage(PDFAppContext* context, QWidget* parent) :
    BaseClass(context, parent)
{
    using Helper = PDFPageNotFoundPageHelper;

    setObjectName(QStringLiteral("pageNotFound"));

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addStretch(1);

    // empty-state: icon 72, 20, title, 8, text 460, 20, requested page, 28, actions, 16, hints
    QWidget* emptyState = new QWidget(this);
    emptyState->setObjectName(QStringLiteral("emptyState"));
    QVBoxLayout* stateLayout = new QVBoxLayout(emptyState);
    stateLayout->setContentsMargins(0, 0, 0, 40);
    stateLayout->setSpacing(0);

    stateLayout->addWidget(new PDFAppNotFoundIcon(emptyState), 0, Qt::AlignHCenter);
    stateLayout->addSpacing(20);

    m_titleLabel = new PDFLabel(QString(), PDFThemeFonts::Role::Display, PDFThemeColors::TextPrimary, emptyState);
    m_titleLabel->setObjectName(QStringLiteral("notFoundTitle"));
    m_titleLabel->setTextFormat(Qt::PlainText);
    m_titleLabel->setAlignment(Qt::AlignCenter);
    stateLayout->addWidget(m_titleLabel, 0, Qt::AlignHCenter);
    stateLayout->addSpacing(8);

    // Regular 14 with the line height 21 (a QLabel would have 17 and the layout of
    // the empty state would not know the height of the wrapped text)
    m_textLabel = new PDFAppParagraph(PDFThemeFonts::getFont(QFont::Normal, 14), 21, PDFThemeColors::TextTertiary, emptyState);
    m_textLabel->setObjectName(QStringLiteral("notFoundText"));
    m_textLabel->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    m_textLabel->setFixedWidth(MAXIMUM_TEXT_WIDTH);
    stateLayout->addWidget(m_textLabel, 0, Qt::AlignHCenter);
    stateLayout->addSpacing(20);

    // requested-path: caption, 6, the box with the item and the path
    m_requestedLabel = new PDFLabel(QString(), PDFThemeFonts::Role::Caption, PDFThemeColors::TextMuted, emptyState);
    m_requestedLabel->setTextFormat(Qt::PlainText);
    stateLayout->addWidget(m_requestedLabel, 0, Qt::AlignHCenter);
    stateLayout->addSpacing(6);

    PDFAppRequestedPathBox* pathBox = new PDFAppRequestedPathBox(emptyState);
    pathBox->setObjectName(QStringLiteral("requestedPath"));
    QVBoxLayout* pathLayout = new QVBoxLayout(pathBox);
    pathLayout->setContentsMargins(17, 9, 17, 9);
    pathLayout->setSpacing(2);
    m_descriptionLabel = Helper::createLabel(QString(), PDFThemeFonts::getFont(QFont::Medium, 14), PDFThemeColors::TextPrimary, pathBox);
    m_descriptionLabel->setObjectName(QStringLiteral("requestedItem"));
    m_descriptionLabel->setAlignment(Qt::AlignCenter);
    m_pathLabel = Helper::createLabel(QString(), Helper::getPathFont(), PDFThemeColors::TextMuted, pathBox);
    m_pathLabel->setObjectName(QStringLiteral("requestedRoute"));
    m_pathLabel->setAlignment(Qt::AlignCenter);
    pathLayout->addWidget(m_descriptionLabel, 0, Qt::AlignHCenter);
    pathLayout->addWidget(m_pathLabel, 0, Qt::AlignHCenter);
    stateLayout->addWidget(pathBox, 0, Qt::AlignHCenter);
    stateLayout->addSpacing(28);

    // actions: Back, Search for …, Go to Home (Primary), gaps 12
    QHBoxLayout* actions = new QHBoxLayout();
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(12);
    m_backButton = Helper::createButton(QString(), QStringLiteral("chevron-left"), emptyState);
    m_backButton->setObjectName(QStringLiteral("notFoundBack"));
    m_searchButton = Helper::createButton(QString(), QStringLiteral("search"), emptyState);
    m_searchButton->setObjectName(QStringLiteral("notFoundSearch"));
    m_homeButton = Helper::createButton(QString(), QStringLiteral("home"), emptyState);
    m_homeButton->setObjectName(QStringLiteral("notFoundHome"));
    PDFWidgetStyleProperties::set(m_homeButton, PDFWidgetStyleProperties::PRIMARY);
    actions->addWidget(m_backButton);
    actions->addWidget(m_searchButton);
    actions->addWidget(m_homeButton);
    stateLayout->addLayout(actions);
    stateLayout->setAlignment(actions, Qt::AlignHCenter);
    stateLayout->addSpacing(16);

    m_hints = new QWidget(emptyState);
    m_hints->setObjectName(QStringLiteral("notFoundHints"));
    QHBoxLayout* hintsLayout = new QHBoxLayout(m_hints);
    hintsLayout->setContentsMargins(0, 0, 0, 0);
    hintsLayout->setSpacing(4);
    stateLayout->addWidget(m_hints, 0, Qt::AlignHCenter);

    // The empty state takes the whole width, its items are centered on the page
    // (centering it as a whole would round the position of the items twice)
    layout->addWidget(emptyState);
    layout->addStretch(1);

    updateIcons();

    connect(m_backButton, &QPushButton::clicked, this, &PDFPageNotFoundPage::onBackClicked);
    connect(m_searchButton, &QPushButton::clicked, this, &PDFPageNotFoundPage::onSearchClicked);
    connect(m_homeButton, &QPushButton::clicked, this, &PDFPageNotFoundPage::onHomeClicked);

    updateContent();
}

PDFPageNotFoundPage::~PDFPageNotFoundPage() = default;

void PDFPageNotFoundPage::setRequest(const PDFAppNotFoundRequest& request)
{
    m_request = request;
    setRoute(request.route);
    updateContent();
}

QString PDFPageNotFoundPage::getTitle() const
{
    return tr("Page not found");
}

QString PDFPageNotFoundPage::getDescriptionText() const
{
    return m_descriptionLabel ? m_descriptionLabel->text() : QString();
}

QString PDFPageNotFoundPage::getPathText() const
{
    return m_pathLabel ? m_pathLabel->text() : QString();
}

void PDFPageNotFoundPage::addKeyHint(QWidget* parent, const QKeySequence& shortcut, const QString& text, bool isFirst)
{
    const QStringList keys = PDFAppKeyCap::getKeyNames(shortcut);
    QHBoxLayout* layout = parent ? qobject_cast<QHBoxLayout*>(parent->layout()) : nullptr;
    if (keys.isEmpty() || !layout)
    {
        return;
    }

    if (!isFirst)
    {
        // hint: the gap 10 between the gaps 4 of the row (the layout puts its
        // spacing only on one side of a spacer item)
        layout->addSpacing(14);
    }

    for (const QString& key : keys)
    {
        layout->addWidget(new PDFAppKeyCap(key, parent));
    }
    layout->addWidget(new PDFLabel(text, PDFThemeFonts::Role::CaptionRegular, PDFThemeColors::TextMuted, parent));
}

void PDFPageNotFoundPage::updateContent()
{
    using Helper = PDFPageNotFoundPageHelper;

    if (m_titleLabel)
    {
        m_titleLabel->setText(getTitle());
    }
    if (m_textLabel)
    {
        m_textLabel->setText(tr("The link you followed leads to a page that does not exist in this version of PDF4QT. Nothing has changed in your documents."));
    }
    if (m_requestedLabel)
    {
        m_requestedLabel->setText(tr("Requested page"));
    }

    const int boxTextWidth = MAXIMUM_TEXT_WIDTH - 2 * 17;
    if (m_descriptionLabel)
    {
        // The description and the path contain the requested text (untrusted input)
        const QString description = Helper::toVisibleText(m_request.description.isEmpty() ? tr("An unknown page") : m_request.description);
        m_descriptionLabel->setText(Helper::elide(description, m_descriptionLabel->font(), Qt::ElideRight, boxTextWidth));
        m_descriptionLabel->setToolTip(Helper::toToolTip(description.left(PDFAppRoute::MAXIMUM_LENGTH)));
    }
    if (m_pathLabel)
    {
        const QString path = Helper::toVisibleText(m_request.route.isValid() ? m_request.route.getPath() : m_request.route.getRequestedText());
        m_pathLabel->setText(Helper::elide(path, m_pathLabel->font(), Qt::ElideMiddle, boxTextWidth));
        m_pathLabel->setVisible(!path.isEmpty());
    }

    if (m_backButton)
    {
        m_backButton->setText(tr("Back"));
        m_backButton->setEnabled(m_request.canGoBack);
    }
    if (m_searchButton)
    {
        m_searchButton->setText(tr("Search for “%1”").arg(Helper::toButtonText(Helper::toVisibleText(m_request.searchText))));
        m_searchButton->setVisible(!m_request.searchText.isEmpty());
    }
    if (m_homeButton)
    {
        m_homeButton->setText(tr("Go to Home"));
    }

    if (m_hints)
    {
        // Hints: the keys of Back and of the search (the shortcuts of the user)
        for (QWidget* child : m_hints->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
        {
            delete child;
        }
        if (QHBoxLayout* hintsLayout = qobject_cast<QHBoxLayout*>(m_hints->layout()))
        {
            while (QLayoutItem* item = hintsLayout->takeAt(0))
            {
                delete item;
            }
        }

        addKeyHint(m_hints, m_request.backShortcut, tr("back"), true);
        const bool hasBackHint = !PDFAppKeyCap::getKeyNames(m_request.backShortcut).isEmpty();
        addKeyHint(m_hints, m_request.searchShortcut, tr("search everywhere"), !hasBackHint);
    }
}

void PDFPageNotFoundPage::onBackClicked()
{
    Q_EMIT backRequested();
}

void PDFPageNotFoundPage::onSearchClicked()
{
    if (!m_request.searchText.isEmpty())
    {
        requestNavigation(PDFAppRoutes::search(m_request.searchText));
    }
}

void PDFPageNotFoundPage::onHomeClicked()
{
    requestNavigation(PDFAppRoutes::home());
}

void PDFPageNotFoundPage::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    if (event->type() == QEvent::LanguageChange)
    {
        updateContent();
    }
    else if (event->type() == QEvent::LayoutDirectionChange)
    {
        updateIcons();
    }
}

void PDFPageNotFoundPage::updateIcons()
{
    // The chevron of Back points to the previous page: Qt does not mirror icons in
    // right-to-left, the mirrored icon is used
    if (m_backButton)
    {
        m_backButton->setIcon(PDFThemeIcons::getIcon(getDirectionalIconName(QStringLiteral("chevron-left"), layoutDirection())));
    }
}

PDFAppPage* PDFPageNotFoundFactory::createPage(PDFAppContext* context, QWidget* parent) const
{
    return new PDFPageNotFoundPage(context, parent);
}

}   // namespace pdf
