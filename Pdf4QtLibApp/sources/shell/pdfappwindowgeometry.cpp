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

#include "pdfappwindowgeometry.h"

#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QScreen>
#include <QWidget>

#include <cmath>
#include <limits>

namespace pdf
{

/// Helpers of the window geometry
class PDFAppWindowGeometryHelper
{
public:
    PDFAppWindowGeometryHelper() = delete;

    /// An integer member in [minimum, maximum] (a JSON number without a fraction)
    static bool readInteger(const QJsonObject& object, const QString& key, int minimum, int maximum, int* value)
    {
        const QJsonValue jsonValue = object.value(key);
        if (!jsonValue.isDouble())
        {
            return false;
        }

        const double number = jsonValue.toDouble();
        if (!std::isfinite(number) || std::floor(number) != number || number < minimum || number > maximum)
        {
            return false;
        }

        *value = int(number);
        return true;
    }

    /// A rectangle of the members x, y, width, height (with the prefix) within the limits
    static bool readRect(const QJsonObject& object, const QString& prefix, QRect* rect)
    {
        const int maximumCoordinate = PDFAppWindowGeometry::MAXIMUM_COORDINATE;
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        if (!readInteger(object, prefix + QStringLiteral("x"), -maximumCoordinate, maximumCoordinate, &x) ||
            !readInteger(object, prefix + QStringLiteral("y"), -maximumCoordinate, maximumCoordinate, &y) ||
            !readInteger(object, prefix + QStringLiteral("width"), 1, PDFAppWindowGeometry::MAXIMUM_SIZE, &width) ||
            !readInteger(object, prefix + QStringLiteral("height"), 1, PDFAppWindowGeometry::MAXIMUM_SIZE, &height))
        {
            return false;
        }

        *rect = QRect(x, y, width, height);
        return true;
    }

    static bool isRectInLimits(const QRect& rect)
    {
        const int maximumCoordinate = PDFAppWindowGeometry::MAXIMUM_COORDINATE;
        return rect.width() >= 1 && rect.height() >= 1 && rect.width() <= PDFAppWindowGeometry::MAXIMUM_SIZE && rect.height() <= PDFAppWindowGeometry::MAXIMUM_SIZE &&
               qAbs(rect.x()) <= maximumCoordinate && qAbs(rect.y()) <= maximumCoordinate;
    }

    static void writeRect(QJsonObject& object, const QString& prefix, const QRect& rect)
    {
        object.insert(prefix + QStringLiteral("x"), rect.x());
        object.insert(prefix + QStringLiteral("y"), rect.y());
        object.insert(prefix + QStringLiteral("width"), rect.width());
        object.insert(prefix + QStringLiteral("height"), rect.height());
    }

    /// The compact JSON text of \p geometry with the name of the screen \p screenName
    static QString createText(const PDFAppWindowGeometry& geometry, const QString& screenName)
    {
        QJsonObject object;
        object.insert(QStringLiteral("version"), PDFAppWindowGeometry::VERSION);
        writeRect(object, QString(), geometry.getNormalGeometry());
        object.insert(QStringLiteral("maximized"), geometry.isMaximized());
        object.insert(QStringLiteral("screen"), screenName);
        writeRect(object, QStringLiteral("screen-"), geometry.getScreenGeometry());
        return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
    }

    /// The screen with the largest part of the rectangle, else the screen nearest
    /// to its center (nullptr without screens)
    static const PDFAppWindowGeometry::Screen* findScreen(const QRect& rect, const std::vector<PDFAppWindowGeometry::Screen>& screens)
    {
        const PDFAppWindowGeometry::Screen* bestScreen = nullptr;
        qint64 bestArea = 0;
        for (const PDFAppWindowGeometry::Screen& screen : screens)
        {
            const QRect intersection = screen.geometry.intersected(rect);
            const qint64 area = intersection.isValid() ? qint64(intersection.width()) * qint64(intersection.height()) : 0;
            if (area > bestArea)
            {
                bestArea = area;
                bestScreen = &screen;
            }
        }

        if (bestScreen)
        {
            return bestScreen;
        }

        qint64 bestDistance = std::numeric_limits<qint64>::max();
        const QPoint center = rect.center();
        for (const PDFAppWindowGeometry::Screen& screen : screens)
        {
            if (!screen.geometry.isValid())
            {
                continue;
            }

            const qint64 dx = qMax(qMax(qint64(screen.geometry.left()) - center.x(), qint64(center.x()) - screen.geometry.right()), qint64(0));
            const qint64 dy = qMax(qMax(qint64(screen.geometry.top()) - center.y(), qint64(center.y()) - screen.geometry.bottom()), qint64(0));
            const qint64 distance = dx * dx + dy * dy;
            if (distance < bestDistance)
            {
                bestDistance = distance;
                bestScreen = &screen;
            }
        }
        return bestScreen;
    }

    /// The available geometry of a screen (its geometry, when it has none)
    static QRect getAvailableGeometry(const PDFAppWindowGeometry::Screen& screen)
    {
        return screen.availableGeometry.isValid() ? screen.availableGeometry : screen.geometry;
    }

    /// One coordinate of a window of \p length inside of [start, start + availableLength)
    static int boundCoordinate(int position, int length, int start, int availableLength)
    {
        if (length >= availableLength)
        {
            return start;
        }
        return qBound(start, position, start + availableLength - length);
    }
};

PDFAppWindowGeometry::PDFAppWindowGeometry(const QRect& normalGeometry, bool isMaximized, const QString& screenName, const QRect& screenGeometry) :
    m_normalGeometry(normalGeometry),
    m_isMaximized(isMaximized),
    m_screenName(screenName.left(MAXIMUM_SCREEN_NAME_LENGTH)),
    m_screenGeometry(screenGeometry)
{

}

bool PDFAppWindowGeometry::isValid() const
{
    return PDFAppWindowGeometryHelper::isRectInLimits(m_normalGeometry) && PDFAppWindowGeometryHelper::isRectInLimits(m_screenGeometry);
}

QString PDFAppWindowGeometry::toText() const
{
    if (!isValid())
    {
        return QString();
    }

    QString text = PDFAppWindowGeometryHelper::createText(*this, m_screenName);
    if (text.size() > MAXIMUM_TEXT_LENGTH)
    {
        // A name of a screen, whose escapes in JSON do not fit (control characters
        // are 6 characters each): the geometry without the name (the placement finds
        // the screen by its geometry)
        text = PDFAppWindowGeometryHelper::createText(*this, QString());
    }
    return text;
}

PDFAppWindowGeometry PDFAppWindowGeometry::fromText(const QString& text)
{
    if (text.isEmpty() || text.size() > MAXIMUM_TEXT_LENGTH)
    {
        return PDFAppWindowGeometry();
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
    {
        return PDFAppWindowGeometry();
    }

    const QJsonObject object = document.object();
    int version = 0;
    QRect normalGeometry;
    QRect screenGeometry;
    const QJsonValue maximized = object.value(QStringLiteral("maximized"));
    const QJsonValue screen = object.value(QStringLiteral("screen"));
    if (!PDFAppWindowGeometryHelper::readInteger(object, QStringLiteral("version"), VERSION, VERSION, &version) ||
        !PDFAppWindowGeometryHelper::readRect(object, QString(), &normalGeometry) ||
        !PDFAppWindowGeometryHelper::readRect(object, QStringLiteral("screen-"), &screenGeometry) ||
        !maximized.isBool() || !screen.isString() || screen.toString().size() > MAXIMUM_SCREEN_NAME_LENGTH)
    {
        return PDFAppWindowGeometry();
    }

    return PDFAppWindowGeometry(normalGeometry, maximized.toBool(), screen.toString(), screenGeometry);
}

PDFAppWindowGeometry PDFAppWindowGeometry::fromWindow(const QWidget* window, bool isMaximized)
{
    if (!window)
    {
        return PDFAppWindowGeometry();
    }

    // QWidget::normalGeometry is the geometry of a window, which is not maximized or
    // full screen (also of a minimized window), else the geometry before the change
    const QRect normalGeometry = window->normalGeometry().isValid() ? window->normalGeometry() : window->geometry();
    const QScreen* screen = isMaximized ? window->screen() : QGuiApplication::screenAt(normalGeometry.center());
    if (!screen)
    {
        screen = window->screen();
    }
    if (!screen)
    {
        return PDFAppWindowGeometry();
    }
    return PDFAppWindowGeometry(normalGeometry, isMaximized, screen->name(), screen->geometry());
}

std::vector<PDFAppWindowGeometry::Screen> PDFAppWindowGeometry::getScreens()
{
    std::vector<Screen> screens;
    for (const QScreen* screen : QGuiApplication::screens())
    {
        if (screen)
        {
            screens.push_back(Screen{ screen->name(), screen->geometry(), screen->availableGeometry() });
        }
    }
    return screens;
}

bool PDFAppWindowGeometry::place(const std::vector<Screen>& screens, const QSize& minimumSize, QRect* geometry) const
{
    if (!isValid() || !geometry)
    {
        return false;
    }

    QRect rect = m_normalGeometry;
    const Screen* target = nullptr;

    // The same screen: the rectangle as it was
    for (const Screen& screen : screens)
    {
        if (screen.name == m_screenName && screen.geometry == m_screenGeometry)
        {
            target = &screen;
            break;
        }
    }

    // The screen of the name with another geometry (another resolution, scaling or
    // arrangement): the same position relative to the screen
    if (!target)
    {
        for (const Screen& screen : screens)
        {
            if (screen.name == m_screenName && screen.geometry.isValid())
            {
                target = &screen;
                rect.translate(screen.geometry.topLeft() - m_screenGeometry.topLeft());
                break;
            }
        }
    }

    // The screen was disconnected: the screen with the window or the nearest one
    if (!target)
    {
        target = PDFAppWindowGeometryHelper::findScreen(rect, screens);
    }

    const QRect available = target ? PDFAppWindowGeometryHelper::getAvailableGeometry(*target) : QRect();
    if (!available.isValid())
    {
        return false;
    }

    // Inside of the available geometry of the screen, at least the minimal size
    const QSize size = rect.size().boundedTo(available.size()).expandedTo(minimumSize);
    const int x = PDFAppWindowGeometryHelper::boundCoordinate(rect.x(), size.width(), available.x(), available.width());
    const int y = PDFAppWindowGeometryHelper::boundCoordinate(rect.y(), size.height(), available.y(), available.height());
    *geometry = QRect(QPoint(x, y), size);
    return true;
}

QRect PDFAppWindowGeometry::cascade(const QRect& normalGeometry, const std::vector<Screen>& screens)
{
    QRect rect = normalGeometry.translated(CASCADE_OFFSET, CASCADE_OFFSET);
    const Screen* screen = PDFAppWindowGeometryHelper::findScreen(normalGeometry, screens);
    const QRect available = screen ? PDFAppWindowGeometryHelper::getAvailableGeometry(*screen) : QRect();
    if (available.isValid() && !available.contains(rect))
    {
        rect.moveTopLeft(available.topLeft());
    }

    // Never exactly over the other window
    if (rect.topLeft() == normalGeometry.topLeft())
    {
        rect.moveTopLeft(normalGeometry.topLeft() + QPoint(CASCADE_OFFSET, CASCADE_OFFSET));
    }
    return rect;
}

}   // namespace pdf
