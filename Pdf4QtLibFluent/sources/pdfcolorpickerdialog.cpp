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

#include "pdfcolorpickerdialog.h"
#include "pdfcomponentbuttons.h"
#include "pdfcomponentpainter.h"
#include "pdffluentwidgets.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QCursor>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

namespace pdf
{

class PDFColorPickerDialogHelper
{
public:
    PDFColorPickerDialogHelper() = delete;

    using Metrics = PDFComponentPainter::ColorPickerMetrics;

    /// Steps of the keys of the area (a fraction of the saturation / value)
    static constexpr qreal AREA_STEP = 0.01;
    static constexpr qreal AREA_LARGE_STEP = 0.10;

    /// Step of the hue slider with Shift
    static constexpr int HUE_LARGE_STEP = 10;

    /// Largest length of the text of the Hex field (a pasted text is bounded)
    static constexpr int HEX_MAX_LENGTH = 32;

    /// Reader of the screen set by the tests (empty = the screens)
    static PDFColorPickerDialog::ScreenColorReader& getReader()
    {
        static PDFColorPickerDialog::ScreenColorReader reader;
        return reader;
    }

    /// Reads a pixel of the screens of the application
    static std::optional<QColor> readScreens(const QPoint& globalPosition)
    {
        // Wayland gives no picture of the screen to an application
        if (QGuiApplication::platformName().startsWith(QLatin1String("wayland"), Qt::CaseInsensitive))
        {
            return std::nullopt;
        }

        QScreen* screen = QGuiApplication::screenAt(globalPosition);
        if (!screen)
        {
            return std::nullopt;
        }

        const QRect geometry = screen->geometry();
        const QPixmap pixmap = screen->grabWindow(0, globalPosition.x() - geometry.x(), globalPosition.y() - geometry.y(), 1, 1);
        const QImage image = pixmap.toImage();
        if (image.isNull() || image.width() < 1 || image.height() < 1)
        {
            return std::nullopt;
        }

        QColor color = image.pixelColor(0, 0).toRgb();
        color.setAlpha(255);
        return color;
    }

    /// Interval of the reading of the position of the pointer while the
    /// eyedropper picks (the pointer outside of the windows of the application
    /// sends no moves, as QColorDialog does it on Windows)
    static constexpr int PICKING_INTERVAL = 30;

    /// The platform can grab the pointer and the keyboard (the platforms
    /// without real windows refuse it with a warning)
    static bool isGrabSupported()
    {
        const QString platform = QGuiApplication::platformName();
        return platform != QLatin1String("offscreen") && platform != QLatin1String("minimal") && !platform.startsWith(QLatin1String("wayland"), Qt::CaseInsensitive);
    }

    /// Returns the opaque RGB color
    static QColor opaque(const QColor& color)
    {
        QColor result = color.toRgb();
        result.setAlpha(255);
        return result;
    }

    /// Returns the percent of a fraction (bounded)
    static int percent(qreal value)
    {
        return qRound(qBound(0.0, value, 1.0) * 100.0);
    }

    /// Returns the state of the drawing of a widget
    static PDFComponentPainter::State getState(const QWidget* widget, bool isHovered, bool isPressed)
    {
        QStyle::State flags = QStyle::State_None;
        if (widget->isEnabled())
        {
            flags |= QStyle::State_Enabled;
        }
        if (isHovered)
        {
            flags |= QStyle::State_MouseOver;
        }
        if (isPressed)
        {
            flags |= QStyle::State_Sunken;
        }
        if (widget->hasFocus() && PDFWidgetStyle::isFocusRingVisible(widget))
        {
            flags |= QStyle::State_HasFocus;
        }
        return PDFComponentPainter::State(flags, widget->layoutDirection());
    }
};

// ----------------------------------------------------------------- PDFColorArea

PDFColorArea::PDFColorArea(QWidget* parent) :
    BaseClass(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setLayoutDirection(Qt::LeftToRight);
    setAccessibleName(tr("Saturation and brightness"));
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&PDFColorArea::update));
    updateDescription();
}

PDFColorArea::~PDFColorArea() = default;

void PDFColorArea::setHue(qreal hue)
{
    const qreal bounded = qIsFinite(hue) ? qBound(0.0, hue, 360.0) : 0.0;
    if (!qFuzzyCompare(1.0 + bounded, 1.0 + m_hue))
    {
        m_hue = bounded;
        update();
    }
}

void PDFColorArea::setSaturationValue(qreal saturation, qreal value)
{
    const qreal boundedSaturation = qIsFinite(saturation) ? qBound(0.0, saturation, 1.0) : 0.0;
    const qreal boundedValue = qIsFinite(value) ? qBound(0.0, value, 1.0) : 0.0;
    if (!qFuzzyCompare(1.0 + boundedSaturation, 1.0 + m_saturation) || !qFuzzyCompare(1.0 + boundedValue, 1.0 + m_value))
    {
        m_saturation = boundedSaturation;
        m_value = boundedValue;
        updateDescription();
        update();
    }
}

QRect PDFColorArea::getAreaRect() const
{
    const int margin = getMarkerMargin();
    return rect().adjusted(margin, margin, -margin, -margin);
}

int PDFColorArea::getMarkerMargin()
{
    return PDFComponentPainter::ColorPickerMetrics::MARKER / 2;
}

QSize PDFColorArea::sizeHint() const
{
    using Metrics = PDFComponentPainter::ColorPickerMetrics;
    return QSize(Metrics::CARD_WIDTH, Metrics::AREA_HEIGHT + 2 * getMarkerMargin());
}

QSize PDFColorArea::minimumSizeHint() const
{
    using Metrics = PDFComponentPainter::ColorPickerMetrics;
    return QSize(2 * Metrics::MARKER + 2 * getMarkerMargin(), 2 * Metrics::MARKER + 2 * getMarkerMargin());
}

void PDFColorArea::setUserSaturationValue(qreal saturation, qreal value)
{
    const qreal oldSaturation = m_saturation;
    const qreal oldValue = m_value;
    setSaturationValue(saturation, value);
    if (!qFuzzyCompare(1.0 + oldSaturation, 1.0 + m_saturation) || !qFuzzyCompare(1.0 + oldValue, 1.0 + m_value))
    {
        Q_EMIT saturationValueChanged(m_saturation, m_value);
    }
}

void PDFColorArea::updateDescription()
{
    using Helper = PDFColorPickerDialogHelper;
    setAccessibleDescription(tr("Saturation %1 %, brightness %2 %").arg(Helper::percent(m_saturation)).arg(Helper::percent(m_value)));
}

void PDFColorArea::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    PDFComponentPainter::paintColorArea(&painter, QRectF(getAreaRect()), PDFComponentPainter::ColorArea{ m_hue, m_saturation, m_value },
                                        PDFColorPickerDialogHelper::getState(this, false, m_isDragging));
}

void PDFColorArea::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        m_isDragging = true;
        const QPointF values = PDFComponentPainter::getColorAreaValues(QRectF(getAreaRect()), event->position());
        event->accept();
        setUserSaturationValue(values.x(), values.y());
        return;
    }
    BaseClass::mousePressEvent(event);
}

void PDFColorArea::mouseMoveEvent(QMouseEvent* event)
{
    if (m_isDragging && event->buttons().testFlag(Qt::LeftButton))
    {
        const QPointF values = PDFComponentPainter::getColorAreaValues(QRectF(getAreaRect()), event->position());
        event->accept();
        setUserSaturationValue(values.x(), values.y());
        return;
    }
    m_isDragging = false;
    BaseClass::mouseMoveEvent(event);
}

void PDFColorArea::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_isDragging)
    {
        m_isDragging = false;
        update();
        event->accept();
        return;
    }
    BaseClass::mouseReleaseEvent(event);
}

void PDFColorArea::keyPressEvent(QKeyEvent* event)
{
    using Helper = PDFColorPickerDialogHelper;

    // The arrows move in the picture (it is not mirrored for right-to-left)
    const qreal step = event->modifiers().testFlag(Qt::ShiftModifier) ? Helper::AREA_LARGE_STEP : Helper::AREA_STEP;
    qreal saturation = m_saturation;
    qreal value = m_value;

    switch (event->key())
    {
        case Qt::Key_Left:
            saturation -= step;
            break;
        case Qt::Key_Right:
            saturation += step;
            break;
        case Qt::Key_Up:
            value += step;
            break;
        case Qt::Key_Down:
            value -= step;
            break;
        case Qt::Key_Home:
            saturation = 0.0;
            break;
        case Qt::Key_End:
            saturation = 1.0;
            break;
        case Qt::Key_PageUp:
            value = 1.0;
            break;
        case Qt::Key_PageDown:
            value = 0.0;
            break;
        default:
            BaseClass::keyPressEvent(event);
            return;
    }

    event->accept();
    setUserSaturationValue(saturation, value);
}

bool PDFColorArea::event(QEvent* event)
{
    switch (event->type())
    {
        case QEvent::FocusIn:
        case QEvent::FocusOut:
        case QEvent::EnabledChange:
            update();
            break;

        case QEvent::Hide:
            m_isDragging = false;
            break;

        case QEvent::LanguageChange:
            setAccessibleName(tr("Saturation and brightness"));
            updateDescription();
            break;

        default:
            break;
    }
    return BaseClass::event(event);
}

// ----------------------------------------------------------------- PDFHueSlider

PDFHueSlider::PDFHueSlider(QWidget* parent) :
    BaseClass(parent)
{
    setOrientation(Qt::Horizontal);
    setRange(0, 359);
    setSingleStep(1);
    setPageStep(PDFColorPickerDialogHelper::HUE_LARGE_STEP);
    setTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_Hover, true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setLayoutDirection(Qt::LeftToRight);
    setAccessibleName(tr("Hue"));
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&PDFHueSlider::update));
}

PDFHueSlider::~PDFHueSlider() = default;

QRect PDFHueSlider::getThumbRect() const
{
    return PDFComponentPainter::getHueSliderThumbRect(QRectF(rect()), sliderPosition()).toAlignedRect();
}

QSize PDFHueSlider::sizeHint() const
{
    using Metrics = PDFComponentPainter::ColorPickerMetrics;
    return QSize(Metrics::CARD_WIDTH / 2, Metrics::SLIDER_HEIGHT);
}

QSize PDFHueSlider::minimumSizeHint() const
{
    using Metrics = PDFComponentPainter::ColorPickerMetrics;
    return QSize(3 * Metrics::SLIDER_THUMB, Metrics::SLIDER_HEIGHT);
}

void PDFHueSlider::moveToPosition(qreal x)
{
    setSliderPosition(qRound(PDFComponentPainter::getHueSliderValue(QRectF(rect()), x)));
}

void PDFHueSlider::setThumbHovered(bool isHovered)
{
    if (m_isThumbHovered != isHovered)
    {
        m_isThumbHovered = isHovered;
        update();
    }
}

void PDFHueSlider::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    PDFComponentPainter::paintHueSlider(&painter, QRectF(rect()), sliderPosition(), PDFColorPickerDialogHelper::getState(this, m_isThumbHovered, isSliderDown()));
}

void PDFHueSlider::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        // The value is changed by the signals of the slider, which a slot may
        // use to delete it
        event->accept();
        QPointer<PDFHueSlider> guard(this);
        setSliderDown(true);
        if (guard)
        {
            moveToPosition(event->position().x());
        }
        return;
    }
    BaseClass::mousePressEvent(event);
}

void PDFHueSlider::mouseMoveEvent(QMouseEvent* event)
{
    if (isSliderDown())
    {
        if (event->buttons().testFlag(Qt::LeftButton))
        {
            event->accept();
            moveToPosition(event->position().x());
            return;
        }

        // A lost release (a modal window, a grab of another widget)
        setSliderDown(false);
        return;
    }

    setThumbHovered(getThumbRect().contains(event->position().toPoint()));
    BaseClass::mouseMoveEvent(event);
}

void PDFHueSlider::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && isSliderDown())
    {
        event->accept();
        setSliderDown(false);
        return;
    }
    BaseClass::mouseReleaseEvent(event);
}

void PDFHueSlider::keyPressEvent(QKeyEvent* event)
{
    // Shift + arrows make the large steps; the other keys of QAbstractSlider
    // (the arrows, Page Up / Page Down, Home / End) are not mirrored, the
    // slider has the left-to-right direction
    if (event->modifiers() == Qt::ShiftModifier)
    {
        switch (event->key())
        {
            case Qt::Key_Left:
            case Qt::Key_Down:
                event->accept();
                triggerAction(QAbstractSlider::SliderPageStepSub);
                return;

            case Qt::Key_Right:
            case Qt::Key_Up:
                event->accept();
                triggerAction(QAbstractSlider::SliderPageStepAdd);
                return;

            default:
                break;
        }
    }
    BaseClass::keyPressEvent(event);
}

bool PDFHueSlider::event(QEvent* event)
{
    switch (event->type())
    {
        case QEvent::HoverEnter:
        case QEvent::HoverMove:
            setThumbHovered(getThumbRect().contains(static_cast<QHoverEvent*>(event)->position().toPoint()));
            break;

        case QEvent::HoverLeave:
        case QEvent::Leave:
        case QEvent::Hide:
            setThumbHovered(false);
            break;

        case QEvent::FocusIn:
        case QEvent::FocusOut:
        case QEvent::EnabledChange:
            update();
            break;

        case QEvent::LanguageChange:
            setAccessibleName(tr("Hue"));
            break;

        default:
            break;
    }
    return BaseClass::event(event);
}

// ----------------------------------------------------------- PDFColorComparison

PDFColorComparison::PDFColorComparison(QWidget* parent) :
    BaseClass(parent)
{
    using Metrics = PDFComponentPainter::ColorPickerMetrics;
    setFixedSize(Metrics::COMPARISON_WIDTH, Metrics::ROW_HEIGHT);
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&PDFColorComparison::update));
    updateTexts();
}

PDFColorComparison::~PDFColorComparison() = default;

void PDFColorComparison::setColors(const QColor& current, const QColor& color)
{
    if (m_current != current || m_color != color)
    {
        m_current = current;
        m_color = color;
        updateTexts();
        update();
    }
}

QSize PDFColorComparison::sizeHint() const
{
    using Metrics = PDFComponentPainter::ColorPickerMetrics;
    return QSize(Metrics::COMPARISON_WIDTH, Metrics::ROW_HEIGHT);
}

QSize PDFColorComparison::minimumSizeHint() const
{
    return sizeHint();
}

void PDFColorComparison::updateTexts()
{
    const QString current = m_current.isValid() ? PDFColorPickerDialog::getHexName(m_current) : tr("No color");
    const QString color = m_color.isValid() ? PDFColorPickerDialog::getHexName(m_color) : tr("No color");
    setToolTip(tr("Current color: %1\nNew color: %2").arg(current, color));
    setAccessibleName(tr("Current and new color"));
    setAccessibleDescription(tr("Current color %1, new color %2").arg(current, color));
}

void PDFColorComparison::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    PDFComponentPainter::paintColorComparison(&painter, QRectF(rect()), PDFComponentPainter::ColorComparison{ m_current, m_color },
                                              PDFColorPickerDialogHelper::getState(this, false, false));
}

void PDFColorComparison::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
    {
        updateTexts();
    }
}

// --------------------------------------------------------- PDFColorPickerDialog

PDFColorPickerDialog::PDFColorPickerDialog(const QColor& currentColor, QWidget* parent) :
    BaseClass(PDFThemeIcons::Badge::Neutral, tr("Choose a color"), QString(), QDialogButtonBox::NoButton, parent),
    m_currentColor(currentColor.isValid() ? PDFColorPickerDialogHelper::opaque(currentColor) : QColor())
{
    using Helper = PDFColorPickerDialogHelper;
    using Metrics = Helper::Metrics;

    setObjectName(QStringLiteral("colorPickerDialog"));
    setBadgeVisible(false);
    connect(&m_pickingTimer, &QTimer::timeout, this, &PDFColorPickerDialog::onPickingTimer);
    setCardWidth(Metrics::CARD_WIDTH);

    // Saturation / value area 354 x 176: the place in the layout is kept by a
    // placeholder, the area itself is a child of the dialog larger by a half of
    // the marker on each side (the ring of the marker reaches out of the area)
    QWidget* placeholder = new QWidget(this);
    placeholder->setObjectName(QStringLiteral("colorAreaPlace"));
    placeholder->setFixedHeight(Metrics::AREA_HEIGHT);
    placeholder->setMinimumWidth(getContentWidth());
    placeholder->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    placeholder->setFocusPolicy(Qt::NoFocus);
    placeholder->installEventFilter(this);
    m_areaPlaceholder = placeholder;
    addContent(placeholder);

    PDFColorArea* area = new PDFColorArea(this);
    area->setObjectName(QStringLiteral("colorArea"));
    m_area = area;
    connect(area, &PDFColorArea::saturationValueChanged, this, &PDFColorPickerDialog::onAreaChanged);

    // Hue row 28 high: the comparison chip, the hue slider, the eyedropper
    QWidget* hueRow = new QWidget(this);
    hueRow->setObjectName(QStringLiteral("colorHueRow"));
    hueRow->setFixedHeight(Metrics::ROW_HEIGHT);
    QHBoxLayout* hueLayout = new QHBoxLayout(hueRow);
    hueLayout->setContentsMargins(0, 0, 0, 0);
    hueLayout->setSpacing(Metrics::ROW_GAP);

    PDFColorComparison* comparison = new PDFColorComparison(hueRow);
    comparison->setObjectName(QStringLiteral("colorComparison"));
    m_comparison = comparison;
    hueLayout->addWidget(comparison, 0, Qt::AlignVCenter);

    PDFHueSlider* hueSlider = new PDFHueSlider(hueRow);
    hueSlider->setObjectName(QStringLiteral("colorHueSlider"));
    m_hueSlider = hueSlider;
    hueLayout->addWidget(hueSlider, 1, Qt::AlignVCenter);
    connect(hueSlider, &QAbstractSlider::valueChanged, this, &PDFColorPickerDialog::onHueChanged);

    PDFIconButton* eyedropper = new PDFIconButton(QStringLiteral("crosshair"), PDFIconButton::Size::Medium, hueRow);
    eyedropper->setObjectName(QStringLiteral("colorEyedropper"));
    m_eyedropper = eyedropper;
    hueLayout->addWidget(eyedropper, 0, Qt::AlignVCenter);
    connect(eyedropper, &QAbstractButton::clicked, this, &PDFColorPickerDialog::onEyedropperClicked);
    addContent(hueRow);

    // Fields: Hex (the rest of the width) and R / G / B 64 wide with their
    // captions 6 above, the error message 6 below
    QWidget* fields = new QWidget(this);
    fields->setObjectName(QStringLiteral("colorFields"));
    QVBoxLayout* fieldsLayout = new QVBoxLayout(fields);
    fieldsLayout->setContentsMargins(0, 0, 0, 0);
    fieldsLayout->setSpacing(Metrics::CAPTION_GAP);
    QHBoxLayout* valuesLayout = new QHBoxLayout();
    valuesLayout->setContentsMargins(0, 0, 0, 0);
    valuesLayout->setSpacing(Metrics::FIELD_GAP);
    fieldsLayout->addLayout(valuesLayout);

    QVBoxLayout* hexLayout = new QVBoxLayout();
    hexLayout->setContentsMargins(0, 0, 0, 0);
    hexLayout->setSpacing(Metrics::CAPTION_GAP);
    PDFLabel* hexCaption = new PDFLabel(QString(), PDFThemeFonts::Role::Caption, PDFThemeColors::TextMuted, fields);
    hexCaption->setTextFormat(Qt::PlainText);
    m_hexCaption = hexCaption;
    hexLayout->addWidget(hexCaption);
    PDFLineEdit* hexField = new PDFLineEdit(fields);
    hexField->setObjectName(QStringLiteral("colorHex"));
    hexField->setMaxLength(Helper::HEX_MAX_LENGTH);
    m_hexField = hexField;
    hexCaption->setBuddy(hexField);
    hexLayout->addWidget(hexField);
    valuesLayout->addLayout(hexLayout, 1);
    connect(hexField, &QLineEdit::textEdited, this, &PDFColorPickerDialog::onHexEdited);

    static constexpr const char* const CHANNEL_NAMES[] = { "colorRed", "colorGreen", "colorBlue" };
    for (int i = 0; i < 3; ++i)
    {
        QVBoxLayout* channelLayout = new QVBoxLayout();
        channelLayout->setContentsMargins(0, 0, 0, 0);
        channelLayout->setSpacing(Metrics::CAPTION_GAP);
        PDFLabel* caption = new PDFLabel(QString(), PDFThemeFonts::Role::Caption, PDFThemeColors::TextMuted, fields);
        caption->setTextFormat(Qt::PlainText);
        m_channelCaptions[i] = caption;
        channelLayout->addWidget(caption);
        PDFSpinBox* channel = new PDFSpinBox(fields);
        channel->setObjectName(QString::fromLatin1(CHANNEL_NAMES[i]));
        channel->setRange(0, 255);
        channel->setFixedWidth(Metrics::CHANNEL_WIDTH);
        m_channelFields[i] = channel;
        caption->setBuddy(channel);
        channelLayout->addWidget(channel);
        valuesLayout->addLayout(channelLayout);
        connect(channel, &QSpinBox::valueChanged, this, &PDFColorPickerDialog::onChannelChanged);
    }

    PDFLabel* errorLabel = new PDFLabel(QString(), PDFThemeFonts::Role::Small, PDFThemeColors::TextDanger, fields);
    errorLabel->setObjectName(QStringLiteral("colorHexError"));
    errorLabel->setTextFormat(Qt::PlainText);
    errorLabel->setLineHeight(Metrics::ERROR_LINE);
    errorLabel->setWordWrap(true);
    errorLabel->setVisible(false);
    m_errorLabel = errorLabel;
    fieldsLayout->addWidget(errorLabel);
    addContent(fields);

    // Cancel at the leading side, OK (Primary) at the trailing side
    QPushButton* cancelButton = addActionButton(QString(), ActionPosition::Leading, false);
    cancelButton->setObjectName(QStringLiteral("colorCancel"));
    m_cancelButton = cancelButton;
    connect(cancelButton, &QAbstractButton::clicked, this, &PDFColorPickerDialog::reject);
    QPushButton* okButton = addActionButton(QString(), ActionPosition::Trailing, true);
    okButton->setObjectName(QStringLiteral("colorOk"));
    m_okButton = okButton;
    connect(okButton, &QAbstractButton::clicked, this, &PDFColorPickerDialog::onOkClicked);

    // Tab: area, hue, eyedropper, Hex, R, G, B, Cancel, OK
    setTabOrder(area, hueSlider);
    setTabOrder(hueSlider, eyedropper);
    setTabOrder(eyedropper, hexField);
    setTabOrder(hexField, m_channelFields[0].data());
    setTabOrder(m_channelFields[0].data(), m_channelFields[1].data());
    setTabOrder(m_channelFields[1].data(), m_channelFields[2].data());
    setTabOrder(m_channelFields[2].data(), cancelButton);
    setTabOrder(cancelButton, okButton);

    retranslate();

    // The eyedropper is shown only, where the screen can be read
    m_isEyedropperAvailable = readScreenColor(QCursor::pos()).has_value();
    eyedropper->setVisible(m_isEyedropperAvailable);

    setRgbColor(m_currentColor.isValid() ? m_currentColor : QColor(255, 255, 255), Source::Program);

    // The first focus is the area (the first stop of Tab)
    area->setFocus(Qt::OtherFocusReason);
}

PDFColorPickerDialog::~PDFColorPickerDialog()
{
    m_pickingTimer.stop();
    if (m_isGrabbing)
    {
        m_isGrabbing = false;
        releaseMouse();
        releaseKeyboard();
    }
}

void PDFColorPickerDialog::setColor(const QColor& color)
{
    if (color.isValid())
    {
        setRgbColor(color, Source::Program);
    }
}

PDFColorArea* PDFColorPickerDialog::getArea() const { return m_area.data(); }
PDFHueSlider* PDFColorPickerDialog::getHueSlider() const { return m_hueSlider.data(); }
PDFColorComparison* PDFColorPickerDialog::getComparison() const { return m_comparison.data(); }
PDFIconButton* PDFColorPickerDialog::getEyedropperButton() const { return m_eyedropper.data(); }
PDFLineEdit* PDFColorPickerDialog::getHexField() const { return m_hexField.data(); }
PDFLabel* PDFColorPickerDialog::getErrorLabel() const { return m_errorLabel.data(); }
QPushButton* PDFColorPickerDialog::getOkButton() const { return m_okButton.data(); }
QPushButton* PDFColorPickerDialog::getCancelButton() const { return m_cancelButton.data(); }

PDFSpinBox* PDFColorPickerDialog::getChannelField(Channel channel) const
{
    const int index = int(channel);
    return (index >= 0 && index < 3) ? m_channelFields[index].data() : nullptr;
}

std::optional<QColor> PDFColorPickerDialog::parseHex(const QString& text)
{
    QString hex = text.trimmed();
    if (hex.startsWith(QLatin1Char('#')))
    {
        hex.remove(0, 1);
    }

    if (hex.size() != 6)
    {
        return std::nullopt;
    }

    uint rgb = 0;
    for (const QChar character : hex)
    {
        const ushort code = character.unicode();
        uint digit = 0;
        if (code >= '0' && code <= '9')
        {
            digit = code - '0';
        }
        else if (code >= 'a' && code <= 'f')
        {
            digit = code - 'a' + 10;
        }
        else if (code >= 'A' && code <= 'F')
        {
            digit = code - 'A' + 10;
        }
        else
        {
            return std::nullopt;
        }
        rgb = (rgb << 4) | digit;
    }

    return QColor((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

QString PDFColorPickerDialog::getHexName(const QColor& color)
{
    return color.toRgb().name(QColor::HexRgb).toUpper();
}

void PDFColorPickerDialog::setScreenColorReader(ScreenColorReader reader)
{
    PDFColorPickerDialogHelper::getReader() = std::move(reader);
}

std::optional<QColor> PDFColorPickerDialog::readScreenColor(const QPoint& globalPosition)
{
    const ScreenColorReader& reader = PDFColorPickerDialogHelper::getReader();
    std::optional<QColor> color = reader ? reader(globalPosition) : PDFColorPickerDialogHelper::readScreens(globalPosition);
    if (color && color->isValid())
    {
        return PDFColorPickerDialogHelper::opaque(*color);
    }
    return std::nullopt;
}

void PDFColorPickerDialog::retranslate()
{
    setTitle(tr("Choose a color"));
    if (m_hexCaption)
    {
        m_hexCaption->setText(tr("Hex"));
    }
    if (m_hexField)
    {
        m_hexField->setAccessibleName(tr("Hex color"));
        m_hexField->setPlaceholderText(QStringLiteral("#RRGGBB"));
    }

    const QString captions[] = { tr("R"), tr("G"), tr("B") };
    const QString names[] = { tr("Red"), tr("Green"), tr("Blue") };
    for (int i = 0; i < 3; ++i)
    {
        if (m_channelCaptions[i])
        {
            m_channelCaptions[i]->setText(captions[i]);
        }
        if (m_channelFields[i])
        {
            m_channelFields[i]->setAccessibleName(names[i]);
        }
    }

    if (m_eyedropper)
    {
        m_eyedropper->setToolTip(tr("Pick a color from the screen"));
        m_eyedropper->setAccessibleName(tr("Pick a color from the screen"));
    }
    if (m_errorLabel)
    {
        m_errorLabel->setText(tr("Enter a color as #RRGGBB."));
    }
    if (m_cancelButton)
    {
        m_cancelButton->setText(tr("Cancel"));
    }
    if (m_okButton)
    {
        m_okButton->setText(tr("OK"));
    }

    // The message of an invalid field is its description
    setHexValid(m_isHexValid);
}

void PDFColorPickerDialog::placeArea()
{
    QWidget* placeholder = m_areaPlaceholder.data();
    PDFColorArea* area = m_area.data();
    if (!placeholder || !area || !isAncestorOf(placeholder) || area->parentWidget() != this)
    {
        return;
    }

    const int margin = PDFColorArea::getMarkerMargin();
    const QRect place(placeholder->mapTo(this, QPoint(0, 0)), placeholder->size());
    area->setGeometry(place.marginsAdded(QMargins(margin, margin, margin, margin)));
    area->setVisible(placeholder->isVisible());
    area->raise();
}

void PDFColorPickerDialog::setRgbColor(const QColor& color, Source source)
{
    const QColor rgb = PDFColorPickerDialogHelper::opaque(color);
    m_color = rgb;

    // An achromatic color keeps the hue, black keeps also the saturation (the
    // marker does not jump, when the user goes through gray or black)
    float hue = 0.0f;
    float saturation = 0.0f;
    float value = 0.0f;
    rgb.getHsvF(&hue, &saturation, &value);
    if (value > 0.0f)
    {
        if (saturation > 0.0f && hue >= 0.0f)
        {
            m_hue = qBound(0.0, qreal(hue) * 360.0, 359.0);
        }
        m_saturation = saturation;
    }
    m_value = value;

    updateControls(source);
}

void PDFColorPickerDialog::setHsvColor(Source source)
{
    m_color = QColor::fromHsvF(float(qBound(0.0, m_hue, 359.0) / 360.0), float(qBound(0.0, m_saturation, 1.0)), float(qBound(0.0, m_value, 1.0))).toRgb();
    m_color.setAlpha(255);
    updateControls(source);
}

void PDFColorPickerDialog::updateControls(Source source)
{
    // The controls are set without their signals: no change returns to its source
    if (PDFColorArea* area = m_area.data())
    {
        area->setHue(m_hue);
        if (source != Source::Area)
        {
            area->setSaturationValue(m_saturation, m_value);
        }
    }

    if (source != Source::Hue)
    {
        if (PDFHueSlider* hueSlider = m_hueSlider.data())
        {
            const QSignalBlocker blocker(hueSlider);
            hueSlider->setValue(qRound(m_hue));
            hueSlider->update();
        }
    }

    if (source != Source::Hex)
    {
        if (PDFLineEdit* hexField = m_hexField.data())
        {
            const QSignalBlocker blocker(hexField);
            hexField->setText(getHexName(m_color));
        }
        setHexValid(true);
    }

    if (source != Source::Channels)
    {
        const int values[] = { m_color.red(), m_color.green(), m_color.blue() };
        for (int i = 0; i < 3; ++i)
        {
            if (PDFSpinBox* channel = m_channelFields[i].data())
            {
                const QSignalBlocker blocker(channel);
                channel->setValue(values[i]);
            }
        }
    }

    if (PDFColorComparison* comparison = m_comparison.data())
    {
        comparison->setColors(m_currentColor, m_color);
    }
}

void PDFColorPickerDialog::setHexValid(bool isValid)
{
    const bool isChanged = m_isHexValid != isValid;
    m_isHexValid = isValid;

    const QString message = m_errorLabel ? m_errorLabel->text() : QString();
    if (PDFLineEdit* hexField = m_hexField.data())
    {
        hexField->setInvalid(!isValid, isValid ? QString() : message);
    }
    if (PDFLabel* errorLabel = m_errorLabel.data())
    {
        errorLabel->setVisible(!isValid);
    }
    if (QPushButton* okButton = m_okButton.data())
    {
        okButton->setEnabled(isValid);
    }

    // The card follows the height of its content (the error row), at once and
    // again from the event loop, after the layouts have handled their requests
    if (isChanged && isWindow() && isVisible())
    {
        fitCardHeight();
        QMetaObject::invokeMethod(this, &PDFColorPickerDialog::fitCardHeight, Qt::QueuedConnection);
    }
}

void PDFColorPickerDialog::fitCardHeight()
{
    if (!isWindow() || !isVisible())
    {
        return;
    }

    // The layouts between the error row and the card forget their sizes
    for (QWidget* widget = m_errorLabel ? m_errorLabel->parentWidget() : nullptr; widget && widget != this; widget = widget->parentWidget())
    {
        if (QLayout* widgetLayout = widget->layout())
        {
            widgetLayout->invalidate();
        }
    }
    if (QLayout* mainLayout = layout())
    {
        // The activation gives the window the minimal size of the content again
        mainLayout->invalidate();
        mainLayout->activate();
    }

    const int height = sizeHint().height();
    if (height > 0 && height != this->height())
    {
        resize(width(), height);
    }
}

PDFColorPickerDialog::ColorState PDFColorPickerDialog::getState() const
{
    return ColorState{ m_color, m_hue, m_saturation, m_value };
}

void PDFColorPickerDialog::setState(const ColorState& state)
{
    m_color = state.color;
    m_hue = state.hue;
    m_saturation = state.saturation;
    m_value = state.value;
    updateControls(Source::Program);
}

void PDFColorPickerDialog::startPicking()
{
    if (!m_isEyedropperAvailable || m_isPicking || !isVisible())
    {
        return;
    }

    // The dialog takes the pointer and the keys: a move reads the screen, a
    // click takes the color, Esc gives the color before the picking back
    m_stateBeforePicking = getState();
    m_isPicking = true;
    m_wasMouseTracking = hasMouseTracking();
    setMouseTracking(true);
    if (PDFColorPickerDialogHelper::isGrabSupported())
    {
        grabMouse(Qt::CrossCursor);
        grabKeyboard();
        m_isGrabbing = true;
    }
    m_lastCursorPosition = QCursor::pos();
    m_pickingTimer.start(PDFColorPickerDialogHelper::PICKING_INTERVAL);
}

void PDFColorPickerDialog::stopPicking(bool keepColor)
{
    if (!m_isPicking)
    {
        return;
    }

    m_isPicking = false;
    m_pickingTimer.stop();
    if (m_isGrabbing)
    {
        m_isGrabbing = false;
        releaseMouse();
        releaseKeyboard();
    }
    setMouseTracking(m_wasMouseTracking);
    if (!keepColor)
    {
        setState(m_stateBeforePicking);
    }
}

void PDFColorPickerDialog::pickAt(const QPoint& globalPosition)
{
    if (const std::optional<QColor> color = readScreenColor(globalPosition))
    {
        setRgbColor(*color, Source::Eyedropper);
    }
}

void PDFColorPickerDialog::onPickingTimer()
{
    // The pointer outside of the windows of the application sends no moves:
    // a move of the pointer itself reads the screen
    const QPoint position = QCursor::pos();
    if (m_isPicking && position != m_lastCursorPosition)
    {
        m_lastCursorPosition = position;
        pickAt(position);
    }
}

void PDFColorPickerDialog::onAreaChanged(qreal saturation, qreal value)
{
    m_saturation = saturation;
    m_value = value;
    setHsvColor(Source::Area);
}

void PDFColorPickerDialog::onHueChanged(int hue)
{
    m_hue = qBound(0, hue, 359);
    setHsvColor(Source::Hue);
}

void PDFColorPickerDialog::onHexEdited(const QString& text)
{
    if (const std::optional<QColor> color = parseHex(text))
    {
        setHexValid(true);
        setRgbColor(*color, Source::Hex);
    }
    else
    {
        // The other controls keep the last valid color
        setHexValid(false);
    }
}

void PDFColorPickerDialog::onChannelChanged()
{
    const int red = m_channelFields[0] ? m_channelFields[0]->value() : m_color.red();
    const int green = m_channelFields[1] ? m_channelFields[1]->value() : m_color.green();
    const int blue = m_channelFields[2] ? m_channelFields[2]->value() : m_color.blue();
    setRgbColor(QColor(qBound(0, red, 255), qBound(0, green, 255), qBound(0, blue, 255)), Source::Channels);
}

void PDFColorPickerDialog::onEyedropperClicked()
{
    startPicking();
}

void PDFColorPickerDialog::onOkClicked()
{
    if (!m_isHexValid)
    {
        return;
    }

    stopPicking(true);

    // The color is copied, a slot may delete the dialog
    const QColor color = m_color;
    QPointer<PDFColorPickerDialog> guard(this);
    Q_EMIT colorSelected(color);
    if (guard)
    {
        accept();
    }
}

void PDFColorPickerDialog::keyPressEvent(QKeyEvent* event)
{
    if (m_isPicking)
    {
        // The keys belong to the eyedropper while it picks
        switch (event->key())
        {
            case Qt::Key_Escape:
                stopPicking(false);
                break;

            case Qt::Key_Enter:
            case Qt::Key_Return:
            case Qt::Key_Space:
                stopPicking(true);
                break;

            default:
                break;
        }
        event->accept();
        return;
    }

    if ((event->key() == Qt::Key_Enter || event->key() == Qt::Key_Return) && !m_isHexValid)
    {
        // OK is disabled: Enter does nothing
        event->accept();
        return;
    }

    BaseClass::keyPressEvent(event);
}

void PDFColorPickerDialog::mousePressEvent(QMouseEvent* event)
{
    if (m_isPicking)
    {
        event->accept();
        if (event->button() == Qt::LeftButton)
        {
            pickAt(event->globalPosition().toPoint());
            stopPicking(true);
        }
        else
        {
            stopPicking(false);
        }
        return;
    }
    BaseClass::mousePressEvent(event);
}

void PDFColorPickerDialog::mouseMoveEvent(QMouseEvent* event)
{
    if (m_isPicking)
    {
        event->accept();
        pickAt(event->globalPosition().toPoint());
        return;
    }
    BaseClass::mouseMoveEvent(event);
}

void PDFColorPickerDialog::hideEvent(QHideEvent* event)
{
    stopPicking(false);
    BaseClass::hideEvent(event);
}

void PDFColorPickerDialog::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    switch (event->type())
    {
        case QEvent::LanguageChange:
            retranslate();
            break;

        case QEvent::ActivationChange:
            // Another window has been activated: the picking ends without a color
            if (m_isPicking && !isActiveWindow())
            {
                stopPicking(false);
            }
            break;

        default:
            break;
    }
}

bool PDFColorPickerDialog::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_areaPlaceholder.data())
    {
        switch (event->type())
        {
            case QEvent::Move:
            case QEvent::Resize:
            case QEvent::Show:
            case QEvent::Hide:
                placeArea();
                break;

            default:
                break;
        }
    }
    return BaseClass::eventFilter(watched, event);
}

}   // namespace pdf
