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

#include "pdfthemeiconengine.h"
#include "pdfthememanager.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QImage>
#include <QPaintEngine>
#include <QPainter>
#include <QPixmapCache>
#include <QSvgRenderer>
#include <QtMath>

#include <memory>

namespace pdf
{

class PDFThemeIconsHelper
{
public:
    PDFThemeIconsHelper() = delete;

    static QString getFileName(const QString& name)
    {
        return QStringLiteral(":/pdf4qtfluent/icons/%1.svg").arg(name);
    }

    /// Width of the strokes of the icons in the units of the icon (square of 24)
    static constexpr qreal STROKE_WIDTH = 2.0;

    /// Returns the data of the icon, empty data, if the icon does not exist
    static QByteArray getData(const QString& name)
    {
        static QHash<QString, QByteArray> cache;

        auto it = cache.constFind(name);
        if (it != cache.cend())
        {
            return it.value();
        }

        QByteArray data;
        QFile file(getFileName(name));
        if (file.open(QIODevice::ReadOnly))
        {
            data = file.readAll();
        }

        cache.insert(name, data);
        return data;
    }

    /// Returns the renderer of the icon, or nullptr, if the icon does not exist
    /// \param name Name of the icon
    /// \param strokeWidth Width of the strokes in the units of the icon
    /// \param isFill The renderer draws the area enclosed by the strokes instead of the strokes
    static QSvgRenderer* getRenderer(const QString& name, qreal strokeWidth = STROKE_WIDTH, bool isFill = false)
    {
        static QHash<QString, std::shared_ptr<QSvgRenderer>> renderers;

        const int strokeKey = qBound(25, qRound(strokeWidth * 100.0), 800);
        const bool isDesignStroke = strokeKey == qRound(STROKE_WIDTH * 100.0) || isFill;
        QString key = isDesignStroke ? name : QStringLiteral("%1/%2").arg(name).arg(strokeKey);
        if (isFill)
        {
            key += QStringLiteral("/fill");
        }

        auto it = renderers.constFind(key);
        if (it != renderers.cend())
        {
            return it.value().get();
        }

        std::shared_ptr<QSvgRenderer> renderer;
        QByteArray data = getData(name);

        if (!data.isEmpty())
        {
            if (isFill)
            {
                // Each path of the icon is filled and not stroked
                data.replace(QByteArrayLiteral("<path "), QByteArrayLiteral("<path fill=\"#000000\" "));
                data.replace(QByteArrayLiteral("stroke=\"#"), QByteArrayLiteral("data-stroke=\"#"));
            }
            else if (!isDesignStroke)
            {
                data.replace(QByteArrayLiteral("stroke-width=\"2\""), QByteArrayLiteral("stroke-width=\"") + QByteArray::number(strokeKey / 100.0, 'f', 2) + QByteArrayLiteral("\""));
            }

            renderer = std::make_shared<QSvgRenderer>(data);

            if (!renderer->isValid())
            {
                renderer.reset();
            }
            else
            {
                // Icons are designed in a square, which is scaled as a whole
                renderer->setAspectRatioMode(Qt::KeepAspectRatio);
            }
        }

        renderers.insert(key, renderer);
        return renderer.get();
    }
};

QIcon PDFThemeIcons::getIcon(const QString& name)
{
    if (!hasIcon(name))
    {
        return QIcon();
    }

    return QIcon(new PDFThemeIconEngine(name));
}

/// Engine of the icons of the badges
class PDFThemeBadgeIconEngine : public QIconEngine
{
public:
    explicit PDFThemeBadgeIconEngine(PDFThemeIcons::Badge badge) : m_badge(badge) { }

    virtual void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) override
    {
        Q_UNUSED(mode);
        Q_UNUSED(state);
        PDFThemeIcons::paintBadge(painter, QRectF(rect), m_badge);
    }

    virtual QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1.0);
    }

    virtual QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state, qreal scale) override
    {
        Q_UNUSED(mode);
        Q_UNUSED(state);

        // The size is in device pixels
        const qreal devicePixelRatio = scale > 0.0 ? scale : 1.0;
        QImage image(size, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        image.setDevicePixelRatio(devicePixelRatio);

        {
            QPainter painter(&image);
            PDFThemeIcons::paintBadge(&painter, QRectF(QPointF(0, 0), QSizeF(size) / devicePixelRatio), m_badge);
        }

        QPixmap pixmap = QPixmap::fromImage(std::move(image));
        pixmap.setDevicePixelRatio(devicePixelRatio);
        return pixmap;
    }

    virtual QSize actualSize(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        Q_UNUSED(mode);
        Q_UNUSED(state);
        const int extent = qMin(size.width(), size.height());
        return QSize(extent, extent);
    }

    virtual QIconEngine* clone() const override { return new PDFThemeBadgeIconEngine(m_badge); }
    virtual QString key() const override { return QStringLiteral("PDFThemeBadgeIconEngine"); }
    virtual bool isNull() override { return false; }

private:
    PDFThemeIcons::Badge m_badge;
};

QIcon PDFThemeIcons::getBadgeIcon(Badge badge)
{
    return QIcon(new PDFThemeBadgeIconEngine(badge));
}

void PDFThemeIcons::paintBadge(QPainter* painter, const QRectF& rect, Badge badge)
{
    if (!painter || rect.isEmpty())
    {
        return;
    }

    PDFThemeColors::Color background = PDFThemeColors::AccentBlueBg;
    PDFThemeColors::Color foreground = PDFThemeColors::AccentBlueIcon;
    QString name = QStringLiteral("info");

    switch (badge)
    {
        case Badge::Information:
            break;

        case Badge::Question:
            name = QStringLiteral("help-circle");
            break;

        case Badge::Warning:
            background = PDFThemeColors::AccentAmberBg;
            foreground = PDFThemeColors::AccentAmberIcon;
            name = QStringLiteral("alert-triangle");
            break;

        case Badge::Critical:
            background = PDFThemeColors::AccentRedBg;
            foreground = PDFThemeColors::AccentRedIcon;
            name = QStringLiteral("alert-circle");
            break;

        case Badge::Success:
            background = PDFThemeColors::AccentGreenBg;
            foreground = PDFThemeColors::AccentGreenIcon;
            name = QStringLiteral("check");
            break;
    }

    const PDFThemeColors& theme = PDFThemeManager::getInstance()->getTheme();

    // Circle with an icon of 20 px in the badge of 36 px
    const qreal extent = qMin(rect.width(), rect.height());
    const QRectF circle(rect.left() + (rect.width() - extent) / 2.0, rect.top() + (rect.height() - extent) / 2.0, extent, extent);
    const qreal iconExtent = extent * 20.0 / 36.0;
    const QRectF iconRect(circle.left() + (extent - iconExtent) / 2.0, circle.top() + (extent - iconExtent) / 2.0, iconExtent, iconExtent);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);
    painter->setBrush(theme.getColor(background));
    painter->drawEllipse(circle);
    painter->restore();

    paint(painter, iconRect, name, theme.getColor(foreground));
}

QStringList PDFThemeIcons::getIconNames()
{
    QStringList names;

    const QDir directory(QStringLiteral(":/pdf4qtfluent/icons"));
    const QStringList files = directory.entryList(QStringList() << QStringLiteral("*.svg"), QDir::Files, QDir::Name);

    names.reserve(files.size());
    for (const QString& file : files)
    {
        names << file.left(file.size() - 4);
    }

    return names;
}

bool PDFThemeIcons::hasIcon(const QString& name)
{
    return !name.isEmpty() && PDFThemeIconsHelper::getRenderer(name) != nullptr;
}

QString PDFThemeIcons::getIconName(const QIcon& icon)
{
    if (icon.isNull())
    {
        return QString();
    }

    const QString name = icon.name();
    const QString prefix = PDFThemeIconEngine::getIconNamePrefix();

    if (name.startsWith(prefix))
    {
        return name.mid(prefix.size());
    }

    return QString();
}

QString PDFThemeIcons::getReplacementName(const QIcon& icon)
{
    if (icon.isNull())
    {
        return QString();
    }

    // Names of the freedesktop icon naming specification used by Qt
    static const QHash<QString, QString> replacements =
    {
        { QStringLiteral("edit-undo"), QStringLiteral("undo") },
        { QStringLiteral("edit-redo"), QStringLiteral("redo") },
        { QStringLiteral("edit-cut"), QStringLiteral("clipboard-cut") },
        { QStringLiteral("edit-copy"), QStringLiteral("copy") },
        { QStringLiteral("edit-paste"), QStringLiteral("clipboard") },
        { QStringLiteral("edit-delete"), QStringLiteral("trash") },
        { QStringLiteral("edit-select-all"), QStringLiteral("select-all") },
        { QStringLiteral("edit-clear"), QStringLiteral("x") },
        { QStringLiteral("edit-find"), QStringLiteral("search") },
        { QStringLiteral("edit-find-replace"), QStringLiteral("replace") },
        { QStringLiteral("document-new"), QStringLiteral("file-plus") },
        { QStringLiteral("document-open"), QStringLiteral("folder-open") },
        { QStringLiteral("document-save"), QStringLiteral("save") },
        { QStringLiteral("document-save-as"), QStringLiteral("save-as") },
        { QStringLiteral("document-print"), QStringLiteral("printer") },
        { QStringLiteral("document-properties"), QStringLiteral("info") },
        { QStringLiteral("folder"), QStringLiteral("folder") },
        { QStringLiteral("folder-new"), QStringLiteral("folder-plus") },
        { QStringLiteral("go-up"), QStringLiteral("arrow-up") },
        { QStringLiteral("go-down"), QStringLiteral("arrow-down") },
        { QStringLiteral("go-previous"), QStringLiteral("chevron-left") },
        { QStringLiteral("go-next"), QStringLiteral("chevron-right") },
        { QStringLiteral("go-first"), QStringLiteral("chevron-first") },
        { QStringLiteral("go-last"), QStringLiteral("chevron-last") },
        { QStringLiteral("go-home"), QStringLiteral("home") },
        { QStringLiteral("list-add"), QStringLiteral("plus") },
        { QStringLiteral("list-remove"), QStringLiteral("minus") },
        { QStringLiteral("view-refresh"), QStringLiteral("refresh-cw") },
        { QStringLiteral("zoom-in"), QStringLiteral("zoom-in") },
        { QStringLiteral("window-close"), QStringLiteral("x") },
        { QStringLiteral("dialog-information"), QStringLiteral("info") },
        { QStringLiteral("dialog-warning"), QStringLiteral("alert-triangle") },
        { QStringLiteral("dialog-error"), QStringLiteral("alert-circle") },
        { QStringLiteral("dialog-question"), QStringLiteral("help-circle") },
        { QStringLiteral("help-about"), QStringLiteral("info") },
        { QStringLiteral("help-contents"), QStringLiteral("help-circle") },
        { QStringLiteral("media-playback-start"), QStringLiteral("play") },
        { QStringLiteral("media-playback-pause"), QStringLiteral("pause") },
        { QStringLiteral("media-playback-stop"), QStringLiteral("stop") },
        { QStringLiteral("preferences-system"), QStringLiteral("settings") }
    };

    const QString name = icon.name();
    if (name.isEmpty() || name.startsWith(PDFThemeIconEngine::getIconNamePrefix()))
    {
        return QString();
    }

    return replacements.value(name);
}

PDFThemeColors::Color PDFThemeIcons::getModeColor(QIcon::Mode mode, QIcon::State state)
{
    switch (mode)
    {
        case QIcon::Disabled:
            return PDFThemeColors::IconDisabled;

        case QIcon::Active:
            return state == QIcon::On ? PDFThemeColors::IconAccent : PDFThemeColors::IconStrong;

        case QIcon::Selected:
            return PDFThemeColors::IconAccent;

        case QIcon::Normal:
            break;
    }

    return state == QIcon::On ? PDFThemeColors::IconAccent : PDFThemeColors::IconDefault;
}

void PDFThemeIcons::paint(QPainter* painter, const QRectF& rect, const QString& name, const QColor& color)
{
    if (!painter || rect.isEmpty())
    {
        return;
    }

    // The device, into which the engine really paints: a widget rendered into an
    // image (QWidget::render, the export of the gallery) is redirected, its
    // painter says the widget, whose ratio may differ from the image
    const QPaintEngine* engine = painter->paintEngine();
    const QPaintDevice* device = (engine && engine->paintDevice()) ? engine->paintDevice() : painter->device();
    const qreal devicePixelRatio = device ? device->devicePixelRatioF() : 1.0;
    const QPixmap pixmap = getPixmap(name, rect.size(), devicePixelRatio, color);

    if (!pixmap.isNull())
    {
        painter->save();
        painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter->drawPixmap(rect, pixmap, QRectF(QPointF(0, 0), QSizeF(pixmap.size())));
        painter->restore();
    }
}

QPixmap PDFThemeIcons::getPixmap(const QString& name, const QSizeF& size, qreal devicePixelRatio, const QColor& color)
{
    QSvgRenderer* renderer = PDFThemeIconsHelper::getRenderer(name);

    if (!renderer || size.isEmpty() || devicePixelRatio <= 0.0)
    {
        return QPixmap();
    }

    const QSize pixelSize(qMax(1, qCeil(size.width() * devicePixelRatio - 0.001)),
                          qMax(1, qCeil(size.height() * devicePixelRatio - 0.001)));

    // An instance of an icon of the design keeps the strokes of 2 px, when it is
    // resized, so the icon is not scaled as a picture: the stroke is made wider
    // (or thinner) in the units of the icon by the scale of the icon.
    const QSizeF iconSize = renderer->viewBoxF().size();
    if (!iconSize.isEmpty())
    {
        const qreal scale = qMin(size.width() / iconSize.width(), size.height() / iconSize.height());
        if (QSvgRenderer* scaledRenderer = PDFThemeIconsHelper::getRenderer(name, PDFThemeIconsHelper::STROKE_WIDTH / scale))
        {
            renderer = scaledRenderer;
        }
    }

    // The same size in pixels has other strokes at another scale of the screen
    const QString key = QStringLiteral("pdf4qtfluent/icon/%1/%2x%3@%4/%5").arg(name).arg(pixelSize.width()).arg(pixelSize.height())
                                                                          .arg(qRound(devicePixelRatio * 100.0)).arg(color.rgba(), 8, 16, QLatin1Char('0'));

    QPixmap pixmap;
    if (QPixmapCache::find(key, &pixmap))
    {
        return pixmap;
    }

    QImage image(pixelSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        renderer->render(&painter, QRectF(QPointF(0, 0), QSizeF(pixelSize)));

        // The shape of the icon is kept, its color is replaced by the color of the theme
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(image.rect(), color);
    }

    pixmap = QPixmap::fromImage(std::move(image));
    pixmap.setDevicePixelRatio(devicePixelRatio);
    QPixmapCache::insert(key, pixmap);
    return pixmap;
}

void PDFThemeIcons::paintFilled(QPainter* painter, const QRectF& rect, const QString& name, const QColor& strokeColor, const QColor& fillColor)
{
    if (!painter || rect.isEmpty())
    {
        return;
    }

    // The device, into which the engine really paints: a widget rendered into an
    // image (QWidget::render, the export of the gallery) is redirected, its
    // painter says the widget, whose ratio may differ from the image
    const QPaintEngine* engine = painter->paintEngine();
    const QPaintDevice* device = (engine && engine->paintDevice()) ? engine->paintDevice() : painter->device();
    const qreal devicePixelRatio = device ? device->devicePixelRatioF() : 1.0;
    const QPixmap fillPixmap = getFillPixmap(name, rect.size(), devicePixelRatio, fillColor);

    if (!fillPixmap.isNull())
    {
        painter->save();
        painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter->drawPixmap(rect, fillPixmap, QRectF(QPointF(0, 0), QSizeF(fillPixmap.size())));
        painter->restore();
    }

    paint(painter, rect, name, strokeColor);
}

QPixmap PDFThemeIcons::getFillPixmap(const QString& name, const QSizeF& size, qreal devicePixelRatio, const QColor& color)
{
    QSvgRenderer* renderer = PDFThemeIconsHelper::getRenderer(name, PDFThemeIconsHelper::STROKE_WIDTH, true);

    if (!renderer || size.isEmpty() || devicePixelRatio <= 0.0)
    {
        return QPixmap();
    }

    const QSize pixelSize(qMax(1, qCeil(size.width() * devicePixelRatio - 0.001)),
                          qMax(1, qCeil(size.height() * devicePixelRatio - 0.001)));
    const QString key = QStringLiteral("pdf4qtfluent/icon-fill/%1/%2x%3/%4").arg(name).arg(pixelSize.width()).arg(pixelSize.height()).arg(color.rgba(), 8, 16, QLatin1Char('0'));

    QPixmap pixmap;
    if (QPixmapCache::find(key, &pixmap))
    {
        return pixmap;
    }

    QImage image(pixelSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        renderer->render(&painter, QRectF(QPointF(0, 0), QSizeF(pixelSize)));
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(image.rect(), color);
    }

    pixmap = QPixmap::fromImage(std::move(image));
    pixmap.setDevicePixelRatio(devicePixelRatio);
    QPixmapCache::insert(key, pixmap);
    return pixmap;
}

PDFThemeIconEngine::PDFThemeIconEngine(QString name) :
    m_name(std::move(name))
{

}

void PDFThemeIconEngine::paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state)
{
    const QColor color = PDFThemeManager::getInstance()->getColor(PDFThemeIcons::getModeColor(mode, state));
    PDFThemeIcons::paint(painter, QRectF(rect), m_name, color);
}

QPixmap PDFThemeIconEngine::pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state)
{
    return scaledPixmap(size, mode, state, 1.0);
}

QPixmap PDFThemeIconEngine::scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state, qreal scale)
{
    // The size is in device pixels, the scale is only the device pixel ratio of the result
    const QColor color = PDFThemeManager::getInstance()->getColor(PDFThemeIcons::getModeColor(mode, state));
    const qreal devicePixelRatio = scale > 0.0 ? scale : 1.0;
    return PDFThemeIcons::getPixmap(m_name, QSizeF(size) / devicePixelRatio, devicePixelRatio, color);
}

QSize PDFThemeIconEngine::actualSize(const QSize& size, QIcon::Mode mode, QIcon::State state)
{
    Q_UNUSED(mode);
    Q_UNUSED(state);

    // Icons are squares and they can be drawn in any size
    const int extent = qMin(size.width(), size.height());
    return QSize(extent, extent);
}

QIconEngine* PDFThemeIconEngine::clone() const
{
    return new PDFThemeIconEngine(m_name);
}

QString PDFThemeIconEngine::key() const
{
    return QStringLiteral("PDFThemeIconEngine");
}

QString PDFThemeIconEngine::iconName()
{
    return getIconNamePrefix() + m_name;
}

bool PDFThemeIconEngine::isNull()
{
    return !PDFThemeIcons::hasIcon(m_name);
}

QString PDFThemeIconEngine::getIconNamePrefix()
{
    return QStringLiteral("pdf4qtfluent:");
}

}   // namespace pdf
