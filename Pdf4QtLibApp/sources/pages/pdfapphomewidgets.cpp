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

#include "pdfapphomewidgets.h"
#include "pdfapplocale.h"
#include "pdfappinfowidgets.h"
#include "pdfappworkspace.h"

#include "pdfcomponentbadges.h"
#include "pdfcomponentbuttons.h"
#include "pdfcomponentcards.h"
#include "pdfcomponentrows.h"
#include "pdffluentwidgets.h"
#include "pdfstylepainter.h"
#include "pdfstylepainterguard.h"
#include "pdfthemefonts.h"
#include "pdfthemeiconengine.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyleproperties.h"

#include <QBoxLayout>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QResizeEvent>
#include <QTimeZone>

#include <QFontMetrics>
#include <QtMath>


namespace pdf
{

/// Helpers of the widgets of the pages Home, Open and Recent
class PDFAppHomeWidgetsHelper
{
public:
    PDFAppHomeWidgetsHelper() = delete;

    static QString translate(const char* text)
    {
        return QCoreApplication::translate("pdf::PDFAppHomeFormat", text);
    }

    /// Text of a count: the singular for one (English has no translation file,
    /// the plural forms of other languages come from the translation of \p plural)
    static QString translate(const char* singular, const char* plural, int count)
    {
        return QCoreApplication::translate("pdf::PDFAppHomeFormat", count == 1 ? singular : plural, nullptr, count);
    }

    static QDateTime toLocal(qint64 time)
    {
        return QDateTime::fromMSecsSinceEpoch(time, QTimeZone::LocalTime);
    }

    /// Writes the time text into the widget of PDFAppHomeFormat::setTimeText (the
    /// setters of the components repaint only a changed text)
    static void applyTimeText(QWidget* widget, const QString& text)
    {
        if (PDFRecentFileRow* fileRow = qobject_cast<PDFRecentFileRow*>(widget))
        {
            fileRow->setTime(text);
        }
        else if (PDFRecentToolRow* toolRow = qobject_cast<PDFRecentToolRow*>(widget))
        {
            toolRow->setLastUsed(text);
        }
        else if (PDFWorkspaceRow* workspaceRow = qobject_cast<PDFWorkspaceRow*>(widget))
        {
            workspaceRow->setMeta(text);
        }
        else if (PDFFolderCard* folderCard = qobject_cast<PDFFolderCard*>(widget))
        {
            folderCard->setMeta(text);
        }
        else if (PDFAppSessionRow* sessionRow = qobject_cast<PDFAppSessionRow*>(widget))
        {
            sessionRow->setMeta(text);
        }
        else if (QLabel* label = qobject_cast<QLabel*>(widget))
        {
            if (label->text() != text)
            {
                label->setText(text);
            }
        }
    }

    static QVBoxLayout* createColumn(QWidget* widget, const QMargins& margins, int spacing)
    {
        QVBoxLayout* layout = new QVBoxLayout(widget);
        layout->setContentsMargins(margins);
        layout->setSpacing(spacing);
        return layout;
    }

    static PDFLabel* createLabel(const QString& text, const QFont& font, PDFThemeColors::Color color, QWidget* parent)
    {
        PDFLabel* label = new PDFLabel(parent);
        label->setFont(font);
        label->setTextColor(color);
        label->setTextFormat(Qt::PlainText);
        label->setText(text);
        return label;
    }

    /// Dashed border of a drop zone inside of the rectangle
    static void paintDashedBorder(QPainter* painter, const QRectF& rect, qreal radius, PDFThemeColors::Color color, qreal dash, qreal gap)
    {
        QPen pen(PDFStylePainter::color(color));
        pen.setWidthF(1.0);
        pen.setDashPattern({ dash, gap });
        pen.setCapStyle(Qt::FlatCap);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        const QRectF inner = rect.adjusted(0.5, 0.5, -0.5, -0.5);
        painter->drawRoundedRect(inner, qMax(0.0, radius - 0.5), qMax(0.0, radius - 0.5));
    }
};

/// Reading progress bar of the Continue card (component Progress bar 78:868 drawn
/// by PDFComponentPainter::paintProgressBar), 4 px high
class PDFAppReadingBar : public QWidget
{
public:
    explicit PDFAppReadingBar(QWidget* parent) :
        QWidget(parent)
    {
        setFixedHeight(4);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

    void setProgress(qreal progress)
    {
        m_progress = PDFComponentPainter::normalizedProgress(progress);
        update();
    }

protected:
    virtual void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        PDFStylePainterGuard guard(&painter);
        painter.setRenderHint(QPainter::Antialiasing);
        PDFComponentPainter::State state;
        state.direction = layoutDirection();
        PDFComponentPainter::paintProgressBar(&painter, QRectF(rect()), m_progress, state);
    }

private:
    qreal m_progress = 0.0;
};

qint64 PDFAppHomeFormat::getCurrentTime()
{
    return PDFAppLocale::getCurrentTime();
}

void PDFAppHomeFormat::setCurrentTimeOverride(qint64 time)
{
    PDFAppLocale::setCurrentTimeOverride(time);
}

QString PDFAppHomeFormat::formatTime(qint64 time, qint64 now, PDFAppLocale::RecentTimeUse use)
{
    if (time <= 0)
    {
        return QString();
    }

    // O25: the hybrid relative time of recent activity, O17: the language of the
    // user interface
    return PDFAppLocale::formatRecentTime(PDFAppHomeWidgetsHelper::toLocal(time), PDFAppHomeWidgetsHelper::toLocal(now), use);
}

QString PDFAppHomeFormat::formatTimeText(TimeText kind, qint64 time, qint64 now, int count)
{
    PDFWorkspace workspace;
    workspace.documentCount = count;
    switch (kind)
    {
        case TimeText::Time:
            return formatTime(time, now);
        case TimeText::LastUsed:
            return formatLastUsed(time, now);
        case TimeText::LastOpened:
            return formatLastOpened(time, now);
        case TimeText::WorkspaceMeta:
            workspace.opened = time;
            return formatWorkspaceMeta(workspace, now);
        case TimeText::WorkspaceDetails:
            workspace.saved = time;
            return formatWorkspaceDetails(workspace, now);
        case TimeText::SessionMeta:
            workspace.saved = time;
            return formatSessionMeta(workspace, now);
    }
    return QString();
}

void PDFAppHomeFormat::setTimeText(QWidget* widget, TimeText kind, qint64 time, qint64 now, int count)
{
    if (!widget)
    {
        return;
    }

    widget->setProperty(TIME_KIND_PROPERTY, int(kind));
    widget->setProperty(TIME_PROPERTY, time);
    widget->setProperty(TIME_COUNT_PROPERTY, count);
    PDFAppHomeWidgetsHelper::applyTimeText(widget, formatTimeText(kind, time, now, count));
}

void PDFAppHomeFormat::updateTimeTexts(QWidget* root, qint64 now)
{
    if (!root)
    {
        return;
    }

    QList<QWidget*> widgets = root->findChildren<QWidget*>();
    widgets.prepend(root);
    for (QWidget* widget : std::as_const(widgets))
    {
        const QVariant kind = widget->property(TIME_KIND_PROPERTY);
        if (!kind.isValid())
        {
            continue;
        }

        const int value = kind.toInt();
        if (value < int(TimeText::Time) || value > int(TimeText::SessionMeta))
        {
            continue;
        }
        const QString text = formatTimeText(TimeText(value), widget->property(TIME_PROPERTY).toLongLong(), now, widget->property(TIME_COUNT_PROPERTY).toInt());
        PDFAppHomeWidgetsHelper::applyTimeText(widget, text);
    }
}

QString PDFAppHomeFormat::formatLastUsed(qint64 time, qint64 now)
{
    return time > 0 ? PDFAppHomeWidgetsHelper::translate("Last used %1").arg(formatTime(time, now, PDFAppLocale::RecentTimeUse::InSentence)) : PDFAppHomeWidgetsHelper::translate("Not used yet");
}

QString PDFAppHomeFormat::formatLastOpened(qint64 time, qint64 now)
{
    return time > 0 ? PDFAppHomeWidgetsHelper::translate("Last opened %1").arg(formatTime(time, now, PDFAppLocale::RecentTimeUse::InSentence)) : QString();
}

QString PDFAppHomeFormat::formatSize(qint64 bytes)
{
    if (bytes < 0)
    {
        return QString();
    }
    return PDFAppLocale::formatDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

QString PDFAppHomeFormat::formatPageCount(int pageCount)
{
    return pageCount >= 0 ? PDFAppHomeWidgetsHelper::translate("%Ln page", "%Ln pages", pageCount) : QString();
}

QString PDFAppHomeFormat::formatDocumentCount(int documentCount)
{
    return PDFAppHomeWidgetsHelper::translate("%Ln document", "%Ln documents", qMax(0, documentCount));
}

QString PDFAppHomeFormat::formatPdfCount(int count, bool isLimited)
{
    if (count < 0)
    {
        return QString();
    }
    if (isLimited)
    {
        return PDFAppHomeWidgetsHelper::translate("%1+ PDF files").arg(PDFAppLocale::formatNumber(count));
    }
    return PDFAppHomeWidgetsHelper::translate("%Ln PDF file", "%Ln PDF files", count);
}

QString PDFAppHomeFormat::formatReading(const PDFRecentFile& file)
{
    switch (file.getReadingState())
    {
        case PDFRecentReadingState::Finished:
            return PDFAppHomeWidgetsHelper::translate("Finished");

        case PDFRecentReadingState::InProgress:
            if (file.pageCount > 0)
            {
                return PDFAppHomeWidgetsHelper::translate("Page %1 of %2").arg(file.lastPage).arg(file.pageCount);
            }
            return PDFAppHomeWidgetsHelper::translate("Page %1").arg(file.lastPage);

        case PDFRecentReadingState::NotStarted:
            break;
    }
    return PDFAppHomeWidgetsHelper::translate("Not started");
}

QString PDFAppHomeFormat::formatPinnedProgress(const PDFRecentFile& file)
{
    if (file.getReadingState() == PDFRecentReadingState::Finished && file.pageCount > 0)
    {
        return PDFAppHomeWidgetsHelper::translate("Finished · %1").arg(formatPageCount(file.pageCount));
    }
    return formatReading(file);
}

qreal PDFAppHomeFormat::getBarProgress(const PDFRecentFile& file)
{
    switch (file.getReadingState())
    {
        case PDFRecentReadingState::Finished:
            return 1.0;
        case PDFRecentReadingState::InProgress:
            return file.pageCount > 0 ? file.getProgress() : -1.0;
        case PDFRecentReadingState::NotStarted:
            break;
    }
    return -1.0;
}

QString PDFAppHomeFormat::formatPercent(qreal progress)
{
    const qreal value = PDFComponentPainter::normalizedProgress(progress);
    return PDFAppHomeWidgetsHelper::translate("%1 %").arg(qRound(value * 100.0));
}

QString PDFAppHomeFormat::formatFileDetails(const PDFRecentFile& file)
{
    QStringList parts;
    parts << toDisplayPath(file.folder);
    if (file.pageCount >= 0)
    {
        parts << formatPageCount(file.pageCount);
    }
    if (file.fileSize >= 0)
    {
        parts << formatSize(file.fileSize);
    }
    parts.removeAll(QString());
    return parts.join(QStringLiteral(" · "));
}

QString PDFAppHomeFormat::formatWorkspaceMeta(const PDFWorkspace& workspace, qint64 now)
{
    const QString count = formatDocumentCount(workspace.documentCount);
    if (workspace.opened <= 0)
    {
        return count;
    }
    return PDFAppHomeWidgetsHelper::translate("%1 · Opened %2").arg(count, formatTime(workspace.opened, now, PDFAppLocale::RecentTimeUse::InSentence));
}

QString PDFAppHomeFormat::formatWorkspaceDetails(const PDFWorkspace& workspace, qint64 now)
{
    const QString count = formatDocumentCount(workspace.documentCount);
    if (workspace.saved <= 0)
    {
        return count;
    }
    return PDFAppHomeWidgetsHelper::translate("%1 · Saved %2").arg(count, formatTime(workspace.saved, now, PDFAppLocale::RecentTimeUse::InSentence));
}

QString PDFAppHomeFormat::formatSessionMeta(const PDFWorkspace& session, qint64 now)
{
    const QString count = formatDocumentCount(session.documentCount);
    if (session.saved <= 0)
    {
        return count;
    }
    return PDFAppHomeWidgetsHelper::translate("%1 · closed %2").arg(count, formatTime(session.saved, now, PDFAppLocale::RecentTimeUse::InSentence));
}

PDFAppHomeFormat::DateGroup PDFAppHomeFormat::getDateGroup(qint64 time, qint64 now)
{
    if (time <= 0)
    {
        return DateGroup::Earlier;
    }

    const QDate date = PDFAppHomeWidgetsHelper::toLocal(time).date();
    const QDate today = PDFAppHomeWidgetsHelper::toLocal(now).date();
    const qint64 days = date.daysTo(today);
    if (days <= 0)
    {
        // A time of the future (a clock set back, another computer) is today
        return DateGroup::Today;
    }
    if (days == 1)
    {
        return DateGroup::Yesterday;
    }
    if (days < 7)
    {
        return DateGroup::ThisWeek;
    }
    return DateGroup::Earlier;
}

QString PDFAppHomeFormat::getDateGroupText(DateGroup group)
{
    switch (group)
    {
        case DateGroup::Today:
            return PDFAppHomeWidgetsHelper::translate("Today");
        case DateGroup::Yesterday:
            return PDFAppHomeWidgetsHelper::translate("Yesterday");
        case DateGroup::ThisWeek:
            return PDFAppHomeWidgetsHelper::translate("This week");
        case DateGroup::Earlier:
            break;
    }
    return PDFAppHomeWidgetsHelper::translate("Earlier");
}

PDFAccentHue PDFAppHomeFormat::getFolderHue(PDFRecentFolderKind kind)
{
    switch (kind)
    {
        case PDFRecentFolderKind::Documents:
            return PDFAccentHue::Green;
        case PDFRecentFolderKind::Downloads:
            return PDFAccentHue::Blue;
        case PDFRecentFolderKind::Desktop:
            return PDFAccentHue::Violet;
        case PDFRecentFolderKind::Other:
            break;
    }
    return PDFAccentHue::Neutral;
}

QString PDFAppHomeFormat::getFolderIconName(PDFRecentFolderKind kind)
{
    switch (kind)
    {
        case PDFRecentFolderKind::Downloads:
            return QStringLiteral("download");
        case PDFRecentFolderKind::Desktop:
            return QStringLiteral("monitor");
        case PDFRecentFolderKind::Documents:
        case PDFRecentFolderKind::Other:
            break;
    }
    return QStringLiteral("folder");
}

QString PDFAppHomeFormat::getLocationIconName(PDFPinnedLocationKind kind)
{
    switch (kind)
    {
        case PDFPinnedLocationKind::Cloud:
            return QStringLiteral("cloud");
        case PDFPinnedLocationKind::Network:
            return QStringLiteral("server");
        case PDFPinnedLocationKind::Local:
            break;
    }
    return QStringLiteral("briefcase");
}

QString PDFAppHomeFormat::toDisplayText(const QString& text, int maximumLength)
{
    const int limit = qMax(1, maximumLength);
    QString result;
    result.reserve(qMin(text.size(), qsizetype(limit) + 1));
    for (qsizetype i = 0; i < text.size(); ++i)
    {
        if (result.size() >= limit)
        {
            // The rest is cut off with an ellipsis (a hostile row of the database)
            result.chop(1);
            if (!result.isEmpty() && result.back().isHighSurrogate())
            {
                result.chop(1);
            }
            result += QChar(0x2026);
            break;
        }

        const QChar character = text[i];
        const char16_t code = character.unicode();
        if (character.isHighSurrogate() && i + 1 < text.size() && text[i + 1].isLowSurrogate())
        {
            result += character;
            result += text[++i];
        }
        else if (character.isSurrogate())
        {
            continue;
        }
        else if (code < 0x20 || (code >= 0x7F && code <= 0x9F) || code == 0x2028 || code == 0x2029)
        {
            result += QLatin1Char(' ');
        }
        else
        {
            result += character;
        }
    }
    return result;
}

QString PDFAppHomeFormat::toDisplayPath(const QString& path)
{
    return toDisplayText(QDir::toNativeSeparators(path));
}

QString PDFAppHomeFormat::toSearchText(const QString& text, int maximumLength)
{
    // Two characters over the limit: the ellipsis of a shortened text (also after
    // a pair of surrogates dropped at the limit) always lies beyond the limit and
    // is cut off - it is not a part of the searched text
    QString result = toDisplayText(text, qMax(1, maximumLength) + 2);
    if (result.size() > maximumLength)
    {
        result = result.left(qMax(0, maximumLength));
    }
    if (!result.isEmpty() && result.back().isHighSurrogate())
    {
        result.chop(1);
    }
    return result;
}

PDFAppElidedLabel::PDFAppElidedLabel(const QFont& font, PDFThemeColors::Color color, PDFComponentPainter::TextKind kind, QWidget* parent) :
    BaseClass(parent),
    m_kind(kind)
{
    setFont(font);
    setTextColor(color);
    setTextFormat(Qt::PlainText);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
}

PDFAppElidedLabel::~PDFAppElidedLabel() = default;

bool PDFAppElidedLabel::isElided() const
{
    return PDFStylePainter::textWidth(text(), font()) > contentsRect().width();
}

QSize PDFAppElidedLabel::sizeHint() const
{
    const QFontMetrics metrics(font());
    return QSize(qCeil(PDFStylePainter::textWidth(text(), font())), metrics.height());
}

QSize PDFAppElidedLabel::minimumSizeHint() const
{
    return QSize(1, QFontMetrics(font()).height());
}

void PDFAppElidedLabel::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    if (text().isEmpty())
    {
        return;
    }

    QPainter painter(this);
    PDFStylePainterGuard guard(&painter);
    PDFComponentPainter::State state(isEnabled() ? QStyle::State_Enabled : QStyle::State_None, layoutDirection());
    const QRectF box(contentsRect());
    PDFComponentPainter::drawElidedText(&painter, state, box, text(), font(), isEnabled() ? getTextColor() : PDFThemeColors::TextDisabled, m_kind);
}

bool PDFAppElidedLabel::event(QEvent* event)
{
    // The whole text is the tooltip of an elided text only
    if (event->type() == QEvent::ToolTip && !isElided())
    {
        event->ignore();
        return true;
    }
    return BaseClass::event(event);
}

PDFAppDropZone::PDFAppDropZone(Variant variant, QWidget* parent) :
    BaseClass(parent),
    m_variant(variant)
{
    setObjectName(variant == Variant::Large ? QStringLiteral("dropZone") : QStringLiteral("compactDropZone"));
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&PDFAppDropZone::update));

    if (variant == Variant::Compact)
    {
        // drop-zone of Home: padding 14 (inside the border), icon 14 and the line Medium 12, gap 8
        QHBoxLayout* layout = new QHBoxLayout(this);
        layout->setContentsMargins(15, 14, 15, 14);
        layout->setSpacing(8);
        layout->addStretch(1);
        PDFIconBadge* icon = new PDFIconBadge(QStringLiteral("download"), 14, 14, this);
        icon->setColors(std::nullopt, PDFThemeColors::IconMuted);
        layout->addWidget(icon, 0, Qt::AlignVCenter);
        PDFLabel* text = PDFAppHomeWidgetsHelper::createLabel(tr("Drag and drop a PDF file here to open it"), PDFThemeFonts::getFont(QFont::Medium, 12),
                                                              PDFThemeColors::TextTertiary, this);
        text->setObjectName(QStringLiteral("dropText"));
        layout->addWidget(text, 0, Qt::AlignVCenter);
        layout->addStretch(1);
        setAccessibleName(text->text());
        return;
    }

    // drop-zone of Open: paddings 28 / 24 / 24 / 24 (inside the border), gap 16, centered
    QVBoxLayout* layout = PDFAppHomeWidgetsHelper::createColumn(this, QMargins(25, 29, 25, 25), 16);
    PDFIconBadge* icon = new PDFIconBadge(QStringLiteral("file-plus"), 56, 26, this);
    icon->setHue(PDFAccentHue::Blue);
    icon->setObjectName(QStringLiteral("dropIcon"));
    layout->addWidget(icon, 0, Qt::AlignHCenter);

    QWidget* texts = new QWidget(this);
    QVBoxLayout* textsLayout = PDFAppHomeWidgetsHelper::createColumn(texts, QMargins(), 4);
    // The lines of the design: the title 19, the text 16, the hint 13 (a label would
    // take the line spacing of the font, 20 and 14 px, and the zone would be 248 high)
    PDFAppParagraph* title = PDFAppPageWidgets::createText(tr("Drop PDF files here"), PDFThemeFonts::getFont(QFont::Bold, 16), 19, PDFThemeColors::TextPrimary, texts);
    title->setObjectName(QStringLiteral("dropTitle"));
    title->setAlignment(Qt::AlignHCenter);
    textsLayout->addWidget(title);
    PDFAppParagraph* subtitle = PDFAppPageWidgets::createText(tr("Drop one or more files to open each in its own tab, or browse your computer."),
                                                           PDFThemeFonts::getFont(PDFThemeFonts::Role::Body), 16, PDFThemeColors::TextTertiary, texts);
    subtitle->setObjectName(QStringLiteral("dropSubtitle"));
    subtitle->setAlignment(Qt::AlignHCenter);
    textsLayout->addWidget(subtitle);
    layout->addWidget(texts);

    m_browseButton = PDFAppPageWidgets::createButton(tr("Browse Files…"), QStringLiteral("folder-open"), true, this);
    m_browseButton->setObjectName(QStringLiteral("browseFilesButton"));
    connect(m_browseButton, &QPushButton::clicked, this, &PDFAppDropZone::browseRequested);
    layout->addWidget(m_browseButton, 0, Qt::AlignHCenter);

    m_hint = PDFAppPageWidgets::createText(QString(), PDFThemeFonts::getFont(PDFThemeFonts::Role::CaptionRegular), 13, PDFThemeColors::TextMuted, this);
    m_hint->setObjectName(QStringLiteral("dropHint"));
    m_hint->setAlignment(Qt::AlignHCenter);
    layout->addWidget(m_hint);
    updateHint();
}

PDFAppDropZone::~PDFAppDropZone() = default;

void PDFAppDropZone::setShortcutText(const QString& text)
{
    m_shortcutText = text;
    updateHint();
}

void PDFAppDropZone::updateHint()
{
    if (!m_hint)
    {
        return;
    }

    const QString filter = tr("PDF files (.pdf)");
    m_hint->setText(m_shortcutText.isEmpty() ? filter : tr("%1  ·  %2").arg(m_shortcutText, filter));
}

void PDFAppDropZone::setDropActive(bool isActive)
{
    if (m_isDropActive != isActive)
    {
        m_isDropActive = isActive;
        update();
    }
}

void PDFAppDropZone::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    PDFStylePainterGuard guard(&painter);
    painter.setRenderHint(QPainter::Antialiasing);
    const bool isLarge = m_variant == Variant::Large;
    const qreal radius = isLarge ? 12.0 : 8.0;

    // Files dragged over the window: the accent of a selection (bg/accent-subtle)
    // and the border in the accent color, the dashes of the zone are kept
    PDFThemeColors::Color background = PDFThemeColors::BgSubtle;
    PDFThemeColors::Color border = isLarge ? PDFThemeColors::BorderStrong : PDFThemeColors::BorderDefault;
    if (m_isDropActive)
    {
        background = PDFThemeColors::BgAccentSubtle;
        border = PDFThemeColors::BgAccent;
    }
    PDFStylePainter::fill(&painter, QRectF(rect()), PDFStylePainter::Radii(radius), background);
    PDFAppHomeWidgetsHelper::paintDashedBorder(&painter, QRectF(rect()), radius, border, isLarge ? 6.0 : 4.0, isLarge ? 4.0 : 3.0);
}

PDFAppSessionRow::PDFAppSessionRow(bool isAccent, QWidget* parent) :
    BaseClass(parent),
    m_isAccent(isAccent)
{
    setObjectName(QStringLiteral("restoreLastSession"));
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&PDFAppSessionRow::update));

    // restore-last-session: paddings 10 (inside the border), gap 12, height 54
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(11, 11, 11, 11);
    layout->setSpacing(12);

    PDFIconBadge* icon = new PDFIconBadge(QStringLiteral("rotate-ccw"), 32, 16, this);
    icon->setColors(PDFThemeColors::BgSurface, PDFThemeColors::IconAccent);
    icon->setObjectName(QStringLiteral("sessionIcon"));
    layout->addWidget(icon, 0, Qt::AlignVCenter);

    QWidget* texts = new QWidget(this);
    QVBoxLayout* textsLayout = PDFAppHomeWidgetsHelper::createColumn(texts, QMargins(), 1);
    PDFLabel* name = PDFAppHomeWidgetsHelper::createLabel(tr("Restore last session"), PDFThemeFonts::getFont(PDFThemeFonts::Role::BodySemiBold), PDFThemeColors::TextPrimary, texts);
    name->setObjectName(QStringLiteral("sessionName"));
    textsLayout->addWidget(name);
    m_meta = new PDFAppElidedLabel(PDFThemeFonts::getFont(PDFThemeFonts::Role::CaptionRegular), PDFThemeColors::TextTertiary, PDFComponentPainter::TextKind::Interface, texts);
    m_meta->setObjectName(QStringLiteral("sessionMeta"));
    textsLayout->addWidget(m_meta);
    layout->addWidget(texts, 1, Qt::AlignVCenter);

    m_restoreButton = PDFAppPageWidgets::createButton(tr("Restore"), QString(), false, this);
    m_restoreButton->setObjectName(QStringLiteral("restoreButton"));
    m_restoreButton->setFixedHeight(32);
    m_restoreButton->setAccessibleDescription(tr("Restore last session"));
    connect(m_restoreButton, &QPushButton::clicked, this, &PDFAppSessionRow::restoreRequested);
    layout->addWidget(m_restoreButton, 0, Qt::AlignVCenter);
}

PDFAppSessionRow::~PDFAppSessionRow() = default;

void PDFAppSessionRow::setMeta(const QString& meta)
{
    if (m_meta)
    {
        m_meta->setText(meta);
        m_meta->setToolTip(meta);
        setAccessibleDescription(meta);
    }
}

QString PDFAppSessionRow::getMeta() const
{
    return m_meta ? m_meta->text() : QString();
}

void PDFAppSessionRow::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    PDFStylePainterGuard guard(&painter);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF box(rect());
    PDFStylePainter::fill(&painter, box, PDFStylePainter::Radii(8.0), m_isAccent ? PDFThemeColors::AccentBlueBg : PDFThemeColors::BgSubtle);
    PDFStylePainter::strokeInside(&painter, box, PDFStylePainter::Radii(8.0), m_isAccent ? PDFThemeColors::AccentBlueBorder : PDFThemeColors::BorderDefault);
}

PDFAppContinueCard::PDFAppContinueCard(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("continueCard"));
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&PDFAppContinueCard::update));

    // card: paddings 14 (inside the border), gap 14; the thumbnail 80 x 106, the texts 216
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(15, 15, 15, 15);
    layout->setSpacing(14);

    PDFPageThumbnail* thumbnail = new PDFPageThumbnail(PDFPageThumbnail::Size::Large, this);
    thumbnail->setObjectName(QStringLiteral("continueThumbnail"));
    m_thumbnail = thumbnail;
    layout->addWidget(thumbnail, 0, Qt::AlignTop);

    QWidget* texts = new QWidget(this);
    QVBoxLayout* textsLayout = PDFAppHomeWidgetsHelper::createColumn(texts, QMargins(), 3);
    // Long names and paths are elided to the width of the card
    m_name = new PDFAppElidedLabel(PDFThemeFonts::getFont(PDFThemeFonts::Role::BodySemiBold), PDFThemeColors::TextPrimary, PDFComponentPainter::TextKind::FileName, texts);
    m_name->setObjectName(QStringLiteral("continueName"));
    m_folder = new PDFAppElidedLabel(PDFThemeFonts::getFont(PDFThemeFonts::Role::CaptionRegular), PDFThemeColors::TextMuted, PDFComponentPainter::TextKind::Data, texts);
    m_folder->setObjectName(QStringLiteral("continueFolder"));
    m_opened = new PDFAppElidedLabel(PDFThemeFonts::getFont(PDFThemeFonts::Role::CaptionRegular), PDFThemeColors::TextTertiary, PDFComponentPainter::TextKind::Interface, texts);
    m_opened->setObjectName(QStringLiteral("continueOpened"));
    textsLayout->addWidget(m_name);
    textsLayout->addWidget(m_folder);
    textsLayout->addWidget(m_opened);
    textsLayout->addSpacing(6);

    QWidget* progress = new QWidget(texts);
    QHBoxLayout* progressLayout = new QHBoxLayout(progress);
    progressLayout->setContentsMargins(0, 0, 0, 0);
    progressLayout->setSpacing(8);
    m_progressText = PDFAppHomeWidgetsHelper::createLabel(QString(), PDFThemeFonts::getFont(PDFThemeFonts::Role::CaptionSemiBold), PDFThemeColors::TextSecondary, progress);
    m_progressText->setObjectName(QStringLiteral("continueProgress"));
    m_percent = PDFAppHomeWidgetsHelper::createLabel(QString(), PDFThemeFonts::getFont(PDFThemeFonts::Role::CaptionRegular), PDFThemeColors::TextMuted, progress);
    m_percent->setObjectName(QStringLiteral("continuePercent"));
    progressLayout->addWidget(m_progressText, 1);
    progressLayout->addWidget(m_percent, 0);
    textsLayout->addWidget(progress);

    PDFAppReadingBar* bar = new PDFAppReadingBar(texts);
    bar->setObjectName(QStringLiteral("continueBar"));
    m_bar = bar;
    textsLayout->addWidget(bar);

    // spacer-button 8 between two gaps 3 (81:1243): QBoxLayout adds its spacing only
    // once between the bar and the button (a spacer item is empty), so the spacer
    // takes the rest of the 14 px
    textsLayout->addSpacing(PDFAppContinueCard::BUTTON_SPACING - textsLayout->spacing());

    m_continueButton = PDFAppPageWidgets::createButton(tr("Continue reading"), QStringLiteral("folder-open"), true, texts);
    m_continueButton->setObjectName(QStringLiteral("continueButton"));
    m_continueButton->setFixedHeight(32);
    connect(m_continueButton, &QPushButton::clicked, this, &PDFAppContinueCard::continueRequested);
    textsLayout->addWidget(m_continueButton, 0, Qt::AlignLeading);
    textsLayout->addStretch(1);
    layout->addWidget(texts, 1);
}

PDFAppContinueCard::~PDFAppContinueCard() = default;

void PDFAppContinueCard::setFile(const PDFRecentFile& file, qint64 now)
{
    m_path = file.path;
    m_progress = PDFComponentPainter::normalizedProgress(file.getProgress());

    const QString name = PDFAppHomeFormat::toDisplayText(file.name);
    const QString folder = PDFAppHomeFormat::toDisplayPath(file.folder);
    if (m_name)
    {
        m_name->setText(name);
        m_name->setToolTip(name);
    }
    if (m_folder)
    {
        m_folder->setText(folder);
        m_folder->setToolTip(folder);
    }
    if (m_opened)
    {
        PDFAppHomeFormat::setTimeText(m_opened, PDFAppHomeFormat::TimeText::LastOpened, file.lastOpened, now);
    }
    if (m_progressText)
    {
        m_progressText->setText(PDFAppHomeFormat::formatReading(file));
    }
    if (m_percent)
    {
        m_percent->setText(PDFAppHomeFormat::formatPercent(m_progress));
        m_percent->setVisible(file.pageCount > 0);
    }
    if (PDFAppReadingBar* bar = static_cast<PDFAppReadingBar*>(m_bar.data()))
    {
        bar->setProgress(m_progress);
    }
    if (m_continueButton)
    {
        m_continueButton->setAccessibleDescription(name);
    }
}

void PDFAppContinueCard::setPixmap(const QPixmap& pixmap)
{
    if (PDFPageThumbnail* thumbnail = qobject_cast<PDFPageThumbnail*>(m_thumbnail.data()))
    {
        thumbnail->setPixmap(pixmap);
    }
}

const QPixmap& PDFAppContinueCard::getPixmap() const
{
    static const QPixmap empty;
    const PDFPageThumbnail* thumbnail = qobject_cast<const PDFPageThumbnail*>(m_thumbnail.data());
    return thumbnail ? thumbnail->getPixmap() : empty;
}

QString PDFAppContinueCard::getName() const
{
    return m_name ? m_name->text() : QString();
}

QString PDFAppContinueCard::getProgressText() const
{
    return m_progressText ? m_progressText->text() : QString();
}

QString PDFAppContinueCard::getOpenedText() const
{
    return m_opened ? m_opened->text() : QString();
}

QRect PDFAppContinueCard::getBarRect() const
{
    return m_bar ? QRect(m_bar->mapTo(this, QPoint(0, 0)), m_bar->size()) : QRect();
}

void PDFAppContinueCard::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    PDFStylePainterGuard guard(&painter);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF box(rect());
    PDFStylePainter::fill(&painter, box, PDFStylePainter::Radii(12.0), PDFThemeColors::AccentBlueBg);
    PDFStylePainter::strokeInside(&painter, box, PDFStylePainter::Radii(12.0), PDFThemeColors::AccentBlueBorder);
}

PDFAppEmptyState::PDFAppEmptyState(QWidget* parent) :
    PDFAppEmptyState(Size::Large, QStringLiteral("clock"), parent)
{

}

PDFAppEmptyState::PDFAppEmptyState(Size size, const QString& iconName, QWidget* parent) :
    BaseClass(parent),
    m_size(size)
{
    setObjectName(QStringLiteral("emptyState"));

    if (size == Size::Small)
    {
        // The small variant of a list card: circle 40, gaps 12 / 4, padding 20, centered
        QVBoxLayout* layout = PDFAppHomeWidgetsHelper::createColumn(this, QMargins(12, 20, 12, 20), 0);
        PDFIconBadge* icon = new PDFIconBadge(iconName, SMALL_CIRCLE, SMALL_ICON, this);
        icon->setHue(PDFAccentHue::Neutral);
        icon->setObjectName(QStringLiteral("emptyIcon"));
        m_icon = icon;
        layout->addWidget(icon, 0, Qt::AlignHCenter);
        layout->addSpacing(12);

        m_title = PDFAppHomeWidgetsHelper::createLabel(QString(), PDFThemeFonts::getFont(QFont::DemiBold, 14), PDFThemeColors::TextPrimary, this);
        m_title->setObjectName(QStringLiteral("emptyTitle"));
        m_title->setAlignment(Qt::AlignHCenter);
        layout->addWidget(m_title);
        layout->addSpacing(4);

        m_text = PDFAppPageWidgets::createText(QString(), PDFThemeFonts::getFont(QFont::Normal, 12), 17, PDFThemeColors::TextTertiary, this);
        m_text->setObjectName(QStringLiteral("emptyText"));
        m_text->setAlignment(Qt::AlignHCenter);
        m_text->setMaximumWidth(280);
        layout->addWidget(m_text, 0, Qt::AlignHCenter);
        return;
    }

    // empty-state: the circle 72, gaps 20 / 8 / 20, at most 460 wide, centered
    QVBoxLayout* layout = PDFAppHomeWidgetsHelper::createColumn(this, QMargins(16, 0, 16, 40), 0);
    layout->addStretch(1);
    PDFIconBadge* icon = new PDFIconBadge(iconName, 72, 32, this);
    icon->setHue(PDFAccentHue::Neutral);
    icon->setObjectName(QStringLiteral("emptyIcon"));
    m_icon = icon;
    layout->addWidget(icon, 0, Qt::AlignHCenter);
    layout->addSpacing(20);

    m_title = PDFAppHomeWidgetsHelper::createLabel(QString(), PDFThemeFonts::getFont(QFont::Bold, 22), PDFThemeColors::TextPrimary, this);
    m_title->setObjectName(QStringLiteral("emptyTitle"));
    m_title->setAlignment(Qt::AlignHCenter);
    layout->addWidget(m_title);
    layout->addSpacing(8);

    m_text = PDFAppPageWidgets::createText(QString(), PDFThemeFonts::getFont(QFont::Normal, 14), 21, PDFThemeColors::TextTertiary, this);
    m_text->setObjectName(QStringLiteral("emptyText"));
    m_text->setAlignment(Qt::AlignHCenter);
    m_text->setMaximumWidth(460);
    layout->addWidget(m_text, 0, Qt::AlignHCenter);
    layout->addSpacing(20);

    m_browseButton = PDFAppPageWidgets::createButton(tr("Browse Files…"), QStringLiteral("folder-open"), true, this);
    m_browseButton->setObjectName(QStringLiteral("emptyBrowseButton"));
    connect(m_browseButton, &QPushButton::clicked, this, &PDFAppEmptyState::browseRequested);
    layout->addWidget(m_browseButton, 0, Qt::AlignHCenter);
    layout->addStretch(1);
}

PDFAppEmptyState::~PDFAppEmptyState() = default;

void PDFAppEmptyState::setTexts(const QString& title, const QString& text)
{
    if (m_title)
    {
        m_title->setText(title);
    }
    if (m_text)
    {
        m_text->setText(text);
    }
    setAccessibleName(title);
    setAccessibleDescription(text);
}

QString PDFAppEmptyState::getTitle() const
{
    return m_title ? m_title->text() : QString();
}

QString PDFAppEmptyState::getText() const
{
    return m_text ? m_text->text() : QString();
}

}   // namespace pdf
