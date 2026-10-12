#include "pdffluentwidgets.h"
#include "pdfstylepainter.h"
#include "pdfthemeiconengine.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyleproperties.h"

#include <QAccessibleWidget>
#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QCursor>
#include <QFontMetricsF>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLayout>
#include <QMouseEvent>
#include <QMovie>
#include <QPainter>
#include <QPicture>
#include <QResizeEvent>
#include <QStyle>
#include <QStyleOptionSpinBox>
#include <QTableView>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QTextLayout>
#include <QToolButton>
#include <QtMath>

#include <cmath>
#include <limits>

namespace pdf
{

class PDFFluentWidgetsHelper
{
public:
    PDFFluentWidgetsHelper() = delete;

    static constexpr const char* TEXT_COLOR = "_pdf4qt_textColor";
    static constexpr const char* DISABLED_TEXT_COLOR = "_pdf4qt_disabledTextColor";
    static constexpr const char* BOUND = "_pdf4qt_textPaletteBound";

    static void applyTextPalette(QWidget* widget)
    {
        const PDFThemeColors::Color text = static_cast<PDFThemeColors::Color>(widget->property(TEXT_COLOR).toInt());
        const PDFThemeColors::Color disabledText = static_cast<PDFThemeColors::Color>(widget->property(DISABLED_TEXT_COLOR).toInt());
        widget->setPalette(PDFThemeWidgetHelper::createTextPalette(text, disabledText));
    }

    /// Sets the width of the prefix or of the suffix, which is drawn beside the value of a spin box
    static void setAffix(QAbstractSpinBox* spinBox, const char* property, const QString& text)
    {
        const QFont font = PDFThemeFonts::getFont(PDFThemeFonts::Role::Small);
        spinBox->setProperty(property, text.isEmpty() ? QVariant() : QVariant(qCeil(PDFStylePainter::textWidth(text, font))));

        // The spin box forgets its cached size hints and places its editor again
        QEvent styleChange(QEvent::StyleChange);
        QApplication::sendEvent(spinBox, &styleChange);
        spinBox->updateGeometry();
        spinBox->update();
    }

    /// The prefix and the suffix are only drawn, so they are told to assistive
    /// technologies as the description of the spin box (unless the application
    /// has set a description of its own)
    static void setAffixDescription(QAbstractSpinBox* spinBox, const QString& oldPrefix, const QString& oldSuffix, const QString& prefix, const QString& suffix)
    {
        const QString oldDescription = QStringLiteral("%1 %2").arg(oldPrefix, oldSuffix).trimmed();
        const QString description = QStringLiteral("%1 %2").arg(prefix, suffix).trimmed();

        if (spinBox->accessibleDescription().isEmpty() || spinBox->accessibleDescription() == oldDescription)
        {
            spinBox->setAccessibleDescription(description);
        }
    }

    /// Draws the muted prefix and suffix of a spin box
    static void paintAffixes(QAbstractSpinBox* spinBox, const QString& prefix, const QString& suffix)
    {
        if (prefix.isEmpty() && suffix.isEmpty())
        {
            return;
        }

        QStyleOptionSpinBox option;
        option.initFrom(spinBox);
        option.buttonSymbols = spinBox->buttonSymbols();
        option.frame = spinBox->hasFrame();

        const QRect editRect = spinBox->style()->subControlRect(QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxEditField, spinBox);
        const QFont font = PDFThemeFonts::getFont(PDFThemeFonts::Role::Small);
        const QColor color = PDFStylePainter::color(PDFThemeColors::TextMuted);
        const bool isRightToLeft = spinBox->layoutDirection() == Qt::RightToLeft;
        const QRectF outer(spinBox->rect());

        QPainter painter(spinBox);

        if (!prefix.isEmpty())
        {
            const qreal width = qCeil(PDFStylePainter::textWidth(prefix, font));
            QRectF rect(editRect.left() + 2.0 - 8.0 - width, 0.0, width, outer.height());
            if (isRightToLeft)
            {
                rect.moveLeft(editRect.right() + 1.0 - 2.0 + 8.0);
            }
            PDFStylePainter::text(&painter, rect, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine, prefix, font, color);
        }

        if (!suffix.isEmpty())
        {
            const qreal width = qCeil(PDFStylePainter::textWidth(suffix, font));
            QRectF rect(editRect.right() + 1.0 + 8.0, 0.0, width, outer.height());
            if (isRightToLeft)
            {
                rect.moveLeft(editRect.left() - 8.0 - width);
            }
            PDFStylePainter::text(&painter, rect, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine, suffix, font, color);
        }
    }
};

QPalette PDFThemeWidgetHelper::createTextPalette(PDFThemeColors::Color text, PDFThemeColors::Color disabledText)
{
    const PDFThemeColors& theme = PDFThemeManager::getInstance()->getTheme();
    QPalette palette = PDFThemeColors::createLeakPalette();

    for (QPalette::ColorGroup group : { QPalette::Active, QPalette::Inactive, QPalette::Disabled })
    {
        const bool isDisabled = group == QPalette::Disabled;
        const QColor textColor = theme.getColor(isDisabled ? disabledText : text);

        palette.setColor(group, QPalette::WindowText, textColor);
        palette.setColor(group, QPalette::Text, textColor);
        palette.setColor(group, QPalette::ButtonText, textColor);
        palette.setColor(group, QPalette::PlaceholderText, theme.getColor(PDFThemeColors::TextMuted));
        palette.setColor(group, QPalette::Highlight, theme.getColor(isDisabled ? PDFThemeColors::BorderStrong : PDFThemeColors::BgAccent));
        palette.setColor(group, QPalette::HighlightedText, theme.getColor(PDFThemeColors::IconOnAccent));
        palette.setColor(group, QPalette::Link, theme.getColor(isDisabled ? disabledText : PDFThemeColors::TextAccent));
        palette.setColor(group, QPalette::LinkVisited, theme.getColor(isDisabled ? disabledText : PDFThemeColors::TextAccentPressed));
    }

    return palette;
}

void PDFThemeWidgetHelper::bindTextPalette(QWidget* widget, PDFThemeColors::Color text, PDFThemeColors::Color disabledText)
{
    if (!widget)
    {
        return;
    }

    widget->setProperty(PDFFluentWidgetsHelper::TEXT_COLOR, static_cast<int>(text));
    widget->setProperty(PDFFluentWidgetsHelper::DISABLED_TEXT_COLOR, static_cast<int>(disabledText));
    PDFFluentWidgetsHelper::applyTextPalette(widget);

    if (!widget->property(PDFFluentWidgetsHelper::BOUND).toBool())
    {
        widget->setProperty(PDFFluentWidgetsHelper::BOUND, true);
        QObject::connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, widget, [widget]() { PDFFluentWidgetsHelper::applyTextPalette(widget); });
    }
}

PDFLabel::PDFLabel(QWidget* parent) :
    QLabel(parent)
{
    init();
}

PDFLabel::PDFLabel(const QString& text, QWidget* parent) :
    QLabel(text, parent)
{
    init();
}

PDFLabel::PDFLabel(const QString& text, PDFThemeFonts::Role fontRole, PDFThemeColors::Color color, QWidget* parent) :
    QLabel(text, parent),
    m_fontRole(fontRole),
    m_color(color)
{
    init();
}

void PDFLabel::setFontRole(PDFThemeFonts::Role fontRole)
{
    m_fontRole = fontRole;
    setFont(PDFThemeFonts::getFont(fontRole));
}

void PDFLabel::setTextColor(PDFThemeColors::Color color)
{
    m_color = color;
    PDFThemeWidgetHelper::bindTextPalette(this, m_color);
    update();
}

void PDFLabel::paintEvent(QPaintEvent* event)
{
    const bool hasGraphics = !pixmap().isNull() || movie() || !picture().isNull();
    const bool isRichText = textFormat() == Qt::RichText || textFormat() == Qt::MarkdownText || (textFormat() == Qt::AutoText && Qt::mightBeRichText(text()));
    const bool isInteractive = textInteractionFlags() & (Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard | Qt::TextEditable);

    if (hasGraphics || isRichText || isInteractive || text().isEmpty())
    {
        // The text control of QLabel draws by the palette of the label, which holds the colors of the theme
        QLabel::paintEvent(event);
        return;
    }

    // Plain text is drawn directly by the color of the theme
    QPainter painter(this);
    drawFrame(&painter);

    QRect rect = contentsRect().adjusted(margin(), margin(), -margin(), -margin());
    const Qt::Alignment visualAlignment = QStyle::visualAlignment(layoutDirection(), alignment());

    if (indent() > 0)
    {
        if (visualAlignment.testFlag(Qt::AlignLeft))
        {
            rect.setLeft(rect.left() + indent());
        }
        if (visualAlignment.testFlag(Qt::AlignRight))
        {
            rect.setRight(rect.right() - indent());
        }
        if (visualAlignment.testFlag(Qt::AlignTop))
        {
            rect.setTop(rect.top() + indent());
        }
        if (visualAlignment.testFlag(Qt::AlignBottom))
        {
            rect.setBottom(rect.bottom() - indent());
        }
    }

    int flags = static_cast<int>(visualAlignment);
    if (wordWrap())
    {
        flags |= Qt::TextWordWrap;
    }
    if (buddy())
    {
        flags |= style()->styleHint(QStyle::SH_UnderlineShortcut, nullptr, this) ? Qt::TextShowMnemonic : Qt::TextHideMnemonic;
    }

    painter.setFont(font());
    painter.setPen(PDFStylePainter::color(isEnabled() ? m_color : PDFThemeColors::TextMuted));
    painter.drawText(rect, flags, text());
}

void PDFLabel::init()
{
    setFont(PDFThemeFonts::getFont(m_fontRole));
    PDFThemeWidgetHelper::bindTextPalette(this, m_color);
}

PDFLineEdit::PDFLineEdit(QWidget* parent) :
    QLineEdit(parent)
{
    init();
}

PDFLineEdit::PDFLineEdit(const QString& text, QWidget* parent) :
    QLineEdit(text, parent)
{
    init();
}

bool PDFLineEdit::isInvalid() const
{
    return PDFWidgetStyleProperties::isSet(this, PDFWidgetStyleProperties::INVALID);
}

void PDFLineEdit::setInvalid(bool invalid, const QString& message)
{
    if (isInvalid() != invalid)
    {
        setProperty(PDFWidgetStyleProperties::INVALID, invalid);
        update();
    }

    // The description is replaced only, when it is the last message of the field
    const QString newMessage = invalid ? message : QString();
    if (accessibleDescription().isEmpty() || accessibleDescription() == m_invalidMessage)
    {
        setAccessibleDescription(newMessage);
    }
    m_invalidMessage = newMessage;
}

bool PDFLineEdit::isPill() const
{
    return PDFWidgetStyleProperties::isSet(this, PDFWidgetStyleProperties::PILL);
}

void PDFLineEdit::setPill(bool pill)
{
    if (isPill() != pill)
    {
        PDFWidgetStyleProperties::set(this, PDFWidgetStyleProperties::PILL, pill);
    }
}

QAction* PDFLineEdit::addIconAction(const QString& iconName, ActionPosition position, const QString& text)
{
    QAction* action = addAction(PDFThemeIcons::getIcon(iconName), position);
    action->setText(text);
    return action;
}

void PDFLineEdit::setSearchField()
{
    setPill(true);
    addIconAction(QStringLiteral("search"), QLineEdit::LeadingPosition, tr("Search"));
}

void PDFLineEdit::setPasswordField()
{
    setEchoMode(QLineEdit::Password);

    if (!m_passwordAction)
    {
        m_passwordAction = addIconAction(QStringLiteral("eye"), QLineEdit::TrailingPosition, tr("Show password"));
        m_passwordAction->setCheckable(true);
        m_passwordAction->setToolTip(tr("Show password"));

        connect(m_passwordAction, &QAction::toggled, this, [this](bool isShown)
        {
            setEchoMode(isShown ? QLineEdit::Normal : QLineEdit::Password);
            m_passwordAction->setIcon(PDFThemeIcons::getIcon(isShown ? QStringLiteral("eye-off") : QStringLiteral("eye")));
            m_passwordAction->setText(isShown ? tr("Hide password") : tr("Show password"));
            m_passwordAction->setToolTip(m_passwordAction->text());
            updatePasswordFont();
        });
    }

    updatePasswordFont();
}

void PDFLineEdit::updatePasswordFont()
{
    // The bullets of a hidden password are 2 px apart
    QFont passwordFont = font();
    const qreal spacing = echoMode() == QLineEdit::Password ? 2.0 : 0.0;

    if (passwordFont.letterSpacing() != spacing || passwordFont.letterSpacingType() != QFont::AbsoluteSpacing)
    {
        passwordFont.setLetterSpacing(QFont::AbsoluteSpacing, spacing);
        setFont(passwordFont);
    }
}

void PDFLineEdit::changeEvent(QEvent* event)
{
    QLineEdit::changeEvent(event);

    if (event->type() == QEvent::ReadOnlyChange)
    {
        updateColors();
    }
}

void PDFLineEdit::init()
{
    updateColors();
}

void PDFLineEdit::updateColors()
{
    // Text of a read-only field is secondary
    PDFThemeWidgetHelper::bindTextPalette(this, isReadOnly() ? PDFThemeColors::TextSecondary : PDFThemeColors::TextPrimary);
}

PDFTextEdit::PDFTextEdit(QWidget* parent) :
    QTextEdit(parent)
{
    init();
}

PDFTextEdit::PDFTextEdit(const QString& text, QWidget* parent) :
    QTextEdit(text, parent)
{
    init();
}

void PDFTextEdit::changeEvent(QEvent* event)
{
    QTextEdit::changeEvent(event);

    if (event->type() == QEvent::ReadOnlyChange)
    {
        PDFThemeWidgetHelper::bindTextPalette(this, isReadOnly() ? PDFThemeColors::TextSecondary : PDFThemeColors::TextPrimary);
    }
}

void PDFTextEdit::init()
{
    // Paddings of the document: 12 at the sides, 10 above and below (inside of the border)
    document()->setDocumentMargin(10.0);
    setViewportMargins(2, 0, 2, 0);
    PDFThemeWidgetHelper::bindTextPalette(this, isReadOnly() ? PDFThemeColors::TextSecondary : PDFThemeColors::TextPrimary);
}

/// Layout of a plain text with the lines of the design
class PDFPlainTextLayout : public QPlainTextDocumentLayout
{
public:
    explicit PDFPlainTextLayout(QTextDocument* document) :
        QPlainTextDocumentLayout(document)
    {

    }

    static constexpr qreal LINE_HEIGHT = 18.0;

    virtual QRectF blockBoundingRect(const QTextBlock& block) const override
    {
        const QRectF rect = QPlainTextDocumentLayout::blockBoundingRect(block);
        QTextLayout* layout = block.isValid() ? block.layout() : nullptr;

        if (!layout || !block.isVisible() || layout->lineCount() == 0)
        {
            return rect;
        }

        const int lineCount = layout->lineCount();
        for (int i = 0; i < lineCount; ++i)
        {
            if (layout->lineAt(i).height() > LINE_HEIGHT)
            {
                // A larger font of the application keeps its own lines
                return rect;
            }
        }

        // The base class stacks the lines by the height of the font. They are
        // moved into the lines of the design, each of them is centered in its
        // line. The base class lays a block out again, when its text is changed,
        // and all users of the geometry of a block ask this function first.
        for (int i = 0; i < lineCount; ++i)
        {
            QTextLine line = layout->lineAt(i);
            const qreal y = i * LINE_HEIGHT + qFloor((LINE_HEIGHT - line.height()) / 2.0);

            if (line.position().y() != y)
            {
                line.setPosition(QPointF(line.position().x(), y));
            }
        }

        const qreal margin = block.next().isValid() ? 0.0 : document()->documentMargin();
        return QRectF(rect.left(), rect.top(), rect.width(), lineCount * LINE_HEIGHT + margin);
    }
};

PDFPlainTextEdit::PDFPlainTextEdit(QWidget* parent) :
    QPlainTextEdit(parent)
{
    init();
}

PDFPlainTextEdit::PDFPlainTextEdit(const QString& text, QWidget* parent) :
    QPlainTextEdit(parent)
{
    init();
    setPlainText(text);
}

void PDFPlainTextEdit::changeEvent(QEvent* event)
{
    QPlainTextEdit::changeEvent(event);

    if (event->type() == QEvent::ReadOnlyChange)
    {
        PDFThemeWidgetHelper::bindTextPalette(this, isReadOnly() ? PDFThemeColors::TextSecondary : PDFThemeColors::TextPrimary);
    }
}

void PDFPlainTextEdit::init()
{
    // The editor remembers the layout of its document, so the layout with the
    // lines of the design comes with a new document
    QTextDocument* textDocument = new QTextDocument(this);
    textDocument->setDocumentLayout(new PDFPlainTextLayout(textDocument));
    setDocument(textDocument);

    document()->setDocumentMargin(10.0);
    setViewportMargins(2, 0, 2, 0);
    PDFThemeWidgetHelper::bindTextPalette(this, isReadOnly() ? PDFThemeColors::TextSecondary : PDFThemeColors::TextPrimary);
}

PDFSpinBox::PDFSpinBox(QWidget* parent) :
    QSpinBox(parent)
{
    PDFThemeWidgetHelper::bindTextPalette(lineEdit());
}

void PDFSpinBox::setDisplayPrefix(const QString& prefix)
{
    PDFFluentWidgetsHelper::setAffixDescription(this, m_displayPrefix, m_displaySuffix, prefix, m_displaySuffix);
    m_displayPrefix = prefix;
    PDFFluentWidgetsHelper::setAffix(this, PDFWidgetStyleProperties::PREFIX_WIDTH, prefix);
}

void PDFSpinBox::setDisplaySuffix(const QString& suffix)
{
    PDFFluentWidgetsHelper::setAffixDescription(this, m_displayPrefix, m_displaySuffix, m_displayPrefix, suffix);
    m_displaySuffix = suffix;
    PDFFluentWidgetsHelper::setAffix(this, PDFWidgetStyleProperties::SUFFIX_WIDTH, suffix);
}

void PDFSpinBox::paintEvent(QPaintEvent* event)
{
    QSpinBox::paintEvent(event);
    PDFFluentWidgetsHelper::paintAffixes(this, m_displayPrefix, m_displaySuffix);
}

PDFDoubleSpinBox::PDFDoubleSpinBox(QWidget* parent) :
    QDoubleSpinBox(parent)
{
    PDFThemeWidgetHelper::bindTextPalette(lineEdit());
}

void PDFDoubleSpinBox::setDisplayPrefix(const QString& prefix)
{
    PDFFluentWidgetsHelper::setAffixDescription(this, m_displayPrefix, m_displaySuffix, prefix, m_displaySuffix);
    m_displayPrefix = prefix;
    PDFFluentWidgetsHelper::setAffix(this, PDFWidgetStyleProperties::PREFIX_WIDTH, prefix);
}

void PDFDoubleSpinBox::setDisplaySuffix(const QString& suffix)
{
    PDFFluentWidgetsHelper::setAffixDescription(this, m_displayPrefix, m_displaySuffix, m_displayPrefix, suffix);
    m_displaySuffix = suffix;
    PDFFluentWidgetsHelper::setAffix(this, PDFWidgetStyleProperties::SUFFIX_WIDTH, suffix);
}

void PDFDoubleSpinBox::paintEvent(QPaintEvent* event)
{
    QDoubleSpinBox::paintEvent(event);
    PDFFluentWidgetsHelper::paintAffixes(this, m_displayPrefix, m_displaySuffix);
}

PDFCalendarWidget::PDFCalendarWidget(QWidget* parent) :
    QCalendarWidget(parent)
{
    setGridVisible(false);
    setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
    setHorizontalHeaderFormat(QCalendarWidget::ShortDayNames);

    if (layout())
    {
        // Navigation of 32 px, the grid of days 6 px below it
        layout()->setSpacing(6);
    }

    if (QTableView* view = findChild<QTableView*>(QStringLiteral("qt_calendar_calendarview")))
    {
        // Columns of 30 px, the row of the names of days 22 px, rows of days 30 px
        // (shapes of the days of 28 px, which are 2 px apart)
        view->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
        view->horizontalHeader()->setMinimumSectionSize(0);
        view->horizontalHeader()->setDefaultSectionSize(30);
        view->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
        view->verticalHeader()->setMinimumSectionSize(0);
        view->verticalHeader()->setDefaultSectionSize(30);

        // Sections have got their sizes, while they were stretched
        for (int column = 0; column < view->horizontalHeader()->count(); ++column)
        {
            view->horizontalHeader()->resizeSection(column, 30);
        }
        for (int row = 0; row < view->verticalHeader()->count(); ++row)
        {
            view->verticalHeader()->resizeSection(row, row == 0 ? 22 : 30);
        }
        view->setFrameShape(QFrame::NoFrame);
        view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

        m_viewport = view->viewport();
        m_viewport->setMouseTracking(true);
        m_viewport->installEventFilter(this);
    }

    for (const char* name : { "qt_calendar_prevmonth", "qt_calendar_nextmonth" })
    {
        if (QToolButton* button = findChild<QToolButton*>(QString::fromLatin1(name)))
        {
            button->setFixedSize(28, 28);
            button->setIconSize(QSize(16, 16));
            button->setProperty(PDFWidgetStyleProperties::RADIUS, 8);
        }
    }

    for (const char* name : { "qt_calendar_monthbutton", "qt_calendar_yearbutton" })
    {
        if (QToolButton* button = findChild<QToolButton*>(QString::fromLatin1(name)))
        {
            // The calendar gives a bold font to these buttons, the design has Medium 13.
            // Both of them open a popup, the year has no menu to show its chevron.
            button->setFixedHeight(28);
            button->setFont(PDFThemeFonts::getFont(PDFThemeFonts::Role::BodyMedium));
            button->setProperty(PDFWidgetStyleProperties::RADIUS, 8);
            button->setProperty(PDFWidgetStyleProperties::MENU_INDICATOR, true);
        }
    }

    if (QSpinBox* yearEdit = findChild<QSpinBox*>(QStringLiteral("qt_calendar_yearedit")))
    {
        PDFThemeWidgetHelper::bindTextPalette(yearEdit->findChild<QLineEdit*>());
    }

    updateColors();
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, &PDFCalendarWidget::updateColors);
}

QSize PDFCalendarWidget::sizeHint() const
{
    // Seven columns of 30 px; navigation 32, gap 6, names of days 22, six rows of 30 px
    return QSize(7 * 30, 32 + 6 + 22 + 6 * 30);
}

QSize PDFCalendarWidget::minimumSizeHint() const
{
    return sizeHint();
}

void PDFCalendarWidget::paintCell(QPainter* painter, const QRect& rect, QDate date) const
{
    const bool isDateEnabled = isEnabled() && date >= minimumDate() && date <= maximumDate();
    const bool isSelected = date == selectedDate();
    const bool isToday = date == QDate::currentDate();
    const bool isOtherMonth = date.month() != monthShown() || date.year() != yearShown();
    const bool isHovered = isDateEnabled && m_viewport && m_viewport->underMouse() && rect.contains(m_viewport->mapFromGlobal(QCursor::pos()));

    // Shape of the day: 30 x 28 with the radius 6. The gap of 2 px between the
    // rows of the shapes lies above each shape (also below the names of the days).
    const qreal shapeWidth = qMin(30, rect.width());
    const qreal shapeHeight = qMin(28, rect.height());
    const QRectF shape(rect.left() + (rect.width() - shapeWidth) / 2.0, rect.bottom() + 1.0 - shapeHeight, shapeWidth, shapeHeight);
    const PDFStylePainter::Radii radii(6.0);

    PDFThemeColors::Color textColor = PDFThemeColors::TextPrimary;
    PDFThemeFonts::Role fontRole = PDFThemeFonts::Role::Small;

    if (isSelected && isDateEnabled)
    {
        PDFStylePainter::fill(painter, shape, radii, PDFThemeColors::BgAccent);
        textColor = PDFThemeColors::IconOnAccent;
        fontRole = PDFThemeFonts::Role::SmallMedium;
    }
    else
    {
        if (isHovered)
        {
            PDFStylePainter::fill(painter, shape, radii, PDFThemeColors::BgHover);
        }
        if (isToday)
        {
            PDFStylePainter::strokeInside(painter, shape, radii, PDFThemeColors::BorderFocus, 1.0);
        }
        if (isOtherMonth || !isDateEnabled)
        {
            textColor = PDFThemeColors::TextMuted;
        }
    }

    PDFStylePainter::text(painter, shape, Qt::AlignCenter | Qt::TextSingleLine, locale().toString(date.day()), PDFThemeFonts::getFont(fontRole), PDFStylePainter::color(textColor));
}

bool PDFCalendarWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_viewport && (event->type() == QEvent::MouseMove || event->type() == QEvent::Leave || event->type() == QEvent::Enter))
    {
        // The hovered day follows the pointer
        m_viewport->update();
    }

    return QCalendarWidget::eventFilter(watched, event);
}

void PDFCalendarWidget::updateColors()
{
    const PDFThemeColors& theme = PDFThemeManager::getInstance()->getTheme();

    if (QTableView* view = findChild<QTableView*>(QStringLiteral("qt_calendar_calendarview")))
    {
        // The model of the calendar takes the colors of the cells from the palette of the view
        QPalette palette = PDFThemeWidgetHelper::createTextPalette(PDFThemeColors::TextPrimary);
        for (QPalette::ColorGroup group : { QPalette::Active, QPalette::Inactive, QPalette::Disabled })
        {
            palette.setBrush(group, QPalette::Base, Qt::transparent);
            palette.setBrush(group, QPalette::AlternateBase, Qt::transparent);
            palette.setBrush(group, QPalette::Window, Qt::transparent);
        }
        view->setPalette(palette);
    }

    // Names of the days: Medium 11, muted. The weekend has no color of its own.
    QTextCharFormat headerFormat;
    headerFormat.setFont(PDFThemeFonts::getFont(PDFThemeFonts::Role::Caption));
    headerFormat.setForeground(theme.getColor(PDFThemeColors::TextMuted));
    headerFormat.setBackground(Qt::transparent);
    setHeaderTextFormat(headerFormat);
    setWeekdayTextFormat(Qt::Saturday, QTextCharFormat());
    setWeekdayTextFormat(Qt::Sunday, QTextCharFormat());
    update();
}

PDFDateTimeEdit::PDFDateTimeEdit(QWidget* parent) :
    QDateTimeEdit(parent)
{
    init();
}

PDFDateTimeEdit::PDFDateTimeEdit(QDate date, QWidget* parent) :
    QDateTimeEdit(date, parent)
{
    init();
}

PDFDateTimeEdit::PDFDateTimeEdit(QTime time, QWidget* parent) :
    QDateTimeEdit(time, parent)
{
    init();
}

PDFDateTimeEdit::PDFDateTimeEdit(const QDateTime& dateTime, QWidget* parent) :
    QDateTimeEdit(dateTime, parent)
{
    init();
}

void PDFDateTimeEdit::setCalendarPopupEnabled(bool enabled)
{
    setCalendarPopup(enabled);
    ensureCalendar();
}

void PDFDateTimeEdit::showEvent(QShowEvent* event)
{
    // The popup can be turned on also by the function of QDateTimeEdit
    ensureCalendar();
    QDateTimeEdit::showEvent(event);
}

void PDFDateTimeEdit::ensureCalendar()
{
    if (!calendarPopup())
    {
        return;
    }

    // An editor without a date has no calendar
    QCalendarWidget* calendar = calendarWidget();
    if (calendar && !qobject_cast<PDFCalendarWidget*>(calendar))
    {
        setCalendarWidget(new PDFCalendarWidget(this));
    }
}

void PDFDateTimeEdit::init()
{
    PDFThemeWidgetHelper::bindTextPalette(lineEdit());
}

PDFComboBox::PDFComboBox(QWidget* parent) :
    QComboBox(parent)
{

}

void PDFComboBox::setCompact(bool compact)
{
    PDFWidgetStyleProperties::set(this, PDFWidgetStyleProperties::COMPACT, compact);
}

void PDFComboBox::setSelectPrefix(const QString& prefix)
{
    setProperty(PDFWidgetStyleProperties::SELECT_PREFIX, prefix);
    PDFWidgetStyleProperties::set(this, PDFWidgetStyleProperties::SELECT, true);
}

bool PDFComboBox::event(QEvent* event)
{
    if (event->type() == QEvent::ChildPolished || event->type() == QEvent::Polish)
    {
        // The editor of an editable combo box draws its text by its palette
        if (QLineEdit* editor = lineEdit())
        {
            if (!editor->property(PDFFluentWidgetsHelper::BOUND).toBool())
            {
                PDFThemeWidgetHelper::bindTextPalette(editor);
            }
        }
    }

    return QComboBox::event(event);
}

PDFStatusBar::PDFStatusBar(QWidget* parent) :
    QStatusBar(parent)
{
    // Paddings of the bar: 14 before the message, 8 after the last item
    setContentsMargins(14, 0, 8, 0);
    setFont(PDFThemeFonts::getFont(PDFThemeFonts::Role::CaptionRegular));
}

QToolButton* PDFStatusBar::createItem(const QString& text, const QString& iconName, bool isWarning)
{
    QToolButton* item = new QToolButton(this);
    item->setProperty(PDFWidgetStyleProperties::STATUS_ITEM, true);
    item->setText(text);
    item->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    if (!iconName.isEmpty())
    {
        item->setIcon(PDFThemeIcons::getIcon(iconName));
    }
    if (isWarning)
    {
        item->setProperty(PDFWidgetStyleProperties::TONE, QStringLiteral("warning"));
    }

    addPermanentWidget(item);
    return item;
}

void PDFStatusBar::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);

    QStyleOption option;
    option.initFrom(this);
    style()->drawPrimitive(QStyle::PE_PanelStatusBar, &option, &painter, this);

    const QString message = currentMessage();
    if (message.isEmpty())
    {
        return;
    }

    // The message takes the space before the first item of the bar
    const bool isRightToLeft = layoutDirection() == Qt::RightToLeft;
    int left = contentsRect().left();
    int right = contentsRect().right() + 1;

    const QList<QWidget*> items = findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
    for (const QWidget* item : items)
    {
        if (!item->isVisible() || item->inherits("QSizeGrip"))
        {
            continue;
        }

        if (isRightToLeft)
        {
            left = qMax(left, item->geometry().right() + 1 + 12);
        }
        else
        {
            right = qMin(right, item->geometry().left() - 12);
        }
    }

    const QRectF rect(left, 1.0, qMax(0, right - left), height() - 1.0);
    const QFont messageFont = PDFThemeFonts::getFont(PDFThemeFonts::Role::CaptionRegular);
    const QString text = QFontMetricsF(messageFont).elidedText(message, Qt::ElideRight, rect.width());
    PDFStylePainter::text(&painter, rect, Qt::AlignVCenter | (isRightToLeft ? Qt::AlignRight : Qt::AlignLeft) | Qt::TextSingleLine, text, messageFont, PDFStylePainter::color(PDFThemeColors::TextMuted));
}

#if QT_CONFIG(accessibility)
/// Segment of a segmented control. It is a checkable tool button, which would
/// be announced as a check box, although the segments are an exclusive choice.
class PDFSegmentAccessible : public QAccessibleWidget
{
public:
    explicit PDFSegmentAccessible(QWidget* widget) :
        QAccessibleWidget(widget, QAccessible::RadioButton)
    {

    }

    virtual QAccessible::State state() const override
    {
        QAccessible::State state = QAccessibleWidget::state();

        if (const QAbstractButton* button = qobject_cast<const QAbstractButton*>(widget()))
        {
            state.checkable = true;
            state.checked = button->isChecked();
        }

        return state;
    }

    virtual QString text(QAccessible::Text textType) const override
    {
        QString text = QAccessibleWidget::text(textType);
        const QAbstractButton* button = qobject_cast<const QAbstractButton*>(widget());

        if (text.isEmpty() && textType == QAccessible::Name && button)
        {
            text = button->text();
        }

        return text;
    }

    virtual QStringList actionNames() const override
    {
        return QStringList() << pressAction() << setFocusAction();
    }

    virtual void doAction(const QString& actionName) override
    {
        QAbstractButton* button = qobject_cast<QAbstractButton*>(widget());

        if (button && actionName == pressAction())
        {
            button->click();
            return;
        }

        QAccessibleWidget::doAction(actionName);
    }

    static QAccessibleInterface* create(const QString& className, QObject* object)
    {
        Q_UNUSED(className);

        QToolButton* button = qobject_cast<QToolButton*>(object);
        if (button && PDFWidgetStyleProperties::isSet(button, PDFWidgetStyleProperties::SEGMENT) && qobject_cast<PDFSegmentedControl*>(button->parentWidget()))
        {
            return new PDFSegmentAccessible(button);
        }

        return nullptr;
    }
};
#endif

PDFSegmentedControl::PDFSegmentedControl(QWidget* parent) :
    QWidget(parent),
    m_layout(new QHBoxLayout(this)),
    m_group(new QButtonGroup(this))
{
#if QT_CONFIG(accessibility)
    static bool isFactoryInstalled = false;
    if (!isFactoryInstalled)
    {
        isFactoryInstalled = true;
        QAccessible::installFactory(&PDFSegmentAccessible::create);
    }
#endif

    // Track with the padding 2, segments follow each other without a gap
    m_layout->setContentsMargins(2, 2, 2, 2);
    m_layout->setSpacing(0);
    m_group->setExclusive(true);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    connect(m_group, &QButtonGroup::idToggled, this, [this](int id, bool isChecked)
    {
        if (isChecked)
        {
            Q_EMIT currentIndexChanged(id);
        }
    });
}

int PDFSegmentedControl::addSegment(const QString& text, const QString& iconName)
{
    QToolButton* segment = new QToolButton(this);
    segment->setProperty(PDFWidgetStyleProperties::SEGMENT, true);
    segment->setCheckable(true);
    segment->setText(text);
    segment->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    segment->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    if (!iconName.isEmpty())
    {
        segment->setIcon(PDFThemeIcons::getIcon(iconName));
    }

    // Segments share the width of the control equally (two segments of 91 px
    // in the control of 186 px)
    const int index = getCount();
    m_group->addButton(segment, index);
    m_layout->addWidget(segment, 1);

    if (index == 0)
    {
        segment->setChecked(true);
    }

    return index;
}

int PDFSegmentedControl::getCount() const
{
    return static_cast<int>(m_group->buttons().size());
}

int PDFSegmentedControl::getCurrentIndex() const
{
    return m_group->checkedId();
}

void PDFSegmentedControl::setCurrentIndex(int index)
{
    if (QAbstractButton* button = m_group->button(index))
    {
        button->setChecked(true);
    }
}

void PDFSegmentedControl::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    PDFStylePainter::fill(&painter, QRectF(rect()), PDFStylePainter::Radii(16.0), PDFThemeColors::BgTrack);
}

PDFListView::PDFListView(QWidget* parent) :
    QListView(parent)
{
    // Rounded rows do not touch the frame of the view
    setViewportMargins(6, 6, 6, 6);
}

PDFTreeView::PDFTreeView(QWidget* parent) :
    QTreeView(parent)
{
    setViewportMargins(6, 6, 6, 6);
}

void PDFTreeView::updateGeometries()
{
    QTreeView::updateGeometries();

    // The tree view sets the margins of its viewport by its header, whenever it
    // updates its geometries. They are returned here. The base class resizes the
    // viewport before it computes the scroll bars, which calls this function
    // again, so the scroll bars are computed for the viewport with the margins.
    const QMargins margins = isHeaderHidden() ? QMargins(6, 6, 6, 6) : viewportMargins();

    if (!m_isUpdatingMargins && viewportMargins() != margins)
    {
        m_isUpdatingMargins = true;
        setViewportMargins(margins);
        m_isUpdatingMargins = false;
    }
}

PDFDial::PDFDial(QWidget* parent) :
    QDial(parent)
{

}

void PDFDial::mousePressEvent(QMouseEvent* event)
{
    if (maximum() == minimum() || event->button() != Qt::LeftButton || (event->buttons() ^ event->button()))
    {
        event->ignore();
        return;
    }

    event->accept();
    setSliderPosition(getValueFromPoint(event->position()));
    setSliderDown(true);
}

void PDFDial::mouseMoveEvent(QMouseEvent* event)
{
    if (!event->buttons().testFlag(Qt::LeftButton))
    {
        event->ignore();
        return;
    }

    event->accept();
    setSliderPosition(getValueFromPoint(event->position()));
}

void PDFDial::mouseReleaseEvent(QMouseEvent* event)
{
    if ((event->buttons() & ~event->button()) || event->button() != Qt::LeftButton)
    {
        event->ignore();
        return;
    }

    event->accept();
    setValue(getValueFromPoint(event->position()));
    setSliderDown(false);
}

int PDFDial::getValueFromPoint(const QPointF& point) const
{
    // Angle of the point measured clockwise from the top
    const QPointF center = QRectF(rect()).center();
    qreal angle = qRadiansToDegrees(qAtan2(point.x() - center.x(), center.y() - point.y()));
    if (angle < 0.0)
    {
        angle += 360.0;
    }

    qreal fraction = 0.0;

    if (wrapping())
    {
        // The whole circle from the bottom
        fraction = std::fmod(angle + 180.0, 360.0) / 360.0;
    }
    else
    {
        // The arc of 270 degrees from the bottom left, the gap at the bottom
        // belongs to the nearer end of the arc
        const qreal relative = std::fmod(angle + 135.0, 360.0);
        fraction = relative <= 270.0 ? relative / 270.0 : (relative < 315.0 ? 1.0 : 0.0);
    }

    if (invertedAppearance())
    {
        fraction = 1.0 - fraction;
    }

    const qreal range = qreal(maximum()) - qreal(minimum());
    return qBound(minimum(), static_cast<int>(qRound64(minimum() + fraction * range)), maximum());
}

/// Editor of a boolean value of an item
class PDFBooleanComboBox : public PDFComboBox
{
    Q_OBJECT
    Q_PROPERTY(bool value READ getValue WRITE setValue USER true)

public:
    explicit PDFBooleanComboBox(QWidget* parent) :
        PDFComboBox(parent)
    {
        addItem(QComboBox::tr("False"));
        addItem(QComboBox::tr("True"));
    }

    bool getValue() const { return currentIndex() == 1; }
    void setValue(bool value) { setCurrentIndex(value ? 1 : 0); }
};

/// Editor of an unsigned integer of an item (the range of a spin box of
/// integers ends in the half of the range of the unsigned integers)
class PDFUIntSpinBox : public PDFDoubleSpinBox
{
    Q_OBJECT
    Q_PROPERTY(uint uintValue READ getUIntValue WRITE setUIntValue USER true)

public:
    explicit PDFUIntSpinBox(QWidget* parent) :
        PDFDoubleSpinBox(parent)
    {
        setDecimals(0);
        setRange(0.0, std::numeric_limits<uint>::max());
    }

    uint getUIntValue() const { return static_cast<uint>(qRound64(value())); }
    void setUIntValue(uint value) { setValue(value); }
};

QWidget* PDFItemEditorFactory::createEditor(int userType, QWidget* parent) const
{
    switch (userType)
    {
        case QMetaType::Bool:
        {
            PDFBooleanComboBox* editor = new PDFBooleanComboBox(parent);
            editor->setFrame(false);
            return editor;
        }

        case QMetaType::Int:
        {
            PDFSpinBox* editor = new PDFSpinBox(parent);
            editor->setFrame(false);
            editor->setRange(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
            return editor;
        }

        case QMetaType::UInt:
        {
            PDFUIntSpinBox* editor = new PDFUIntSpinBox(parent);
            editor->setFrame(false);
            return editor;
        }

        case QMetaType::Double:
        case QMetaType::Float:
        {
            PDFDoubleSpinBox* editor = new PDFDoubleSpinBox(parent);
            editor->setFrame(false);
            editor->setRange(-std::numeric_limits<double>::max(), std::numeric_limits<double>::max());
            return editor;
        }

        case QMetaType::QDate:
        {
            PDFDateTimeEdit* editor = new PDFDateTimeEdit(QDate::currentDate(), parent);
            editor->setFrame(false);
            return editor;
        }

        case QMetaType::QTime:
        {
            PDFDateTimeEdit* editor = new PDFDateTimeEdit(QTime::currentTime(), parent);
            editor->setFrame(false);
            return editor;
        }

        case QMetaType::QDateTime:
        {
            PDFDateTimeEdit* editor = new PDFDateTimeEdit(parent);
            editor->setFrame(false);
            return editor;
        }

        case QMetaType::QString:
        {
            PDFLineEdit* editor = new PDFLineEdit(parent);
            editor->setFrame(false);
            return editor;
        }

        default:
            break;
    }

    // Other editors are created by the default factory. The default factory can
    // be a factory of this class (installed by the application for all views),
    // then only the editors registered to it are left.
    const QItemEditorFactory* factory = QItemEditorFactory::defaultFactory();
    if (factory == this || dynamic_cast<const PDFItemEditorFactory*>(factory))
    {
        return QItemEditorFactory::createEditor(userType, parent);
    }

    return factory->createEditor(userType, parent);
}

QByteArray PDFItemEditorFactory::valuePropertyName(int userType) const
{
    switch (userType)
    {
        case QMetaType::Bool:
        case QMetaType::Int:
        case QMetaType::Double:
        case QMetaType::Float:
            return QByteArrayLiteral("value");

        case QMetaType::UInt:
            return QByteArrayLiteral("uintValue");

        case QMetaType::QDate:
            return QByteArrayLiteral("date");

        case QMetaType::QTime:
            return QByteArrayLiteral("time");

        case QMetaType::QDateTime:
            return QByteArrayLiteral("dateTime");

        case QMetaType::QString:
            return QByteArrayLiteral("text");

        default:
            break;
    }

    const QItemEditorFactory* factory = QItemEditorFactory::defaultFactory();
    if (factory == this || dynamic_cast<const PDFItemEditorFactory*>(factory))
    {
        return QItemEditorFactory::valuePropertyName(userType);
    }

    return factory->valuePropertyName(userType);
}

PDFItemDelegate::PDFItemDelegate(QObject* parent) :
    QStyledItemDelegate(parent)
{
    static PDFItemEditorFactory factory;
    setItemEditorFactory(&factory);
}

void PDFItemDelegate::updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    Q_UNUSED(index);

    // The editor fills the whole cell, its ring is drawn inside of it
    if (editor)
    {
        editor->setGeometry(option.rect);
    }
}

}   // namespace pdf

#include "pdffluentwidgets.moc"
