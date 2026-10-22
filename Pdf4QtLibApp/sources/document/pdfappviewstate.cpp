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

#include "pdfappviewstate.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <cmath>

namespace pdf
{

/// Keys of the JSON object of the view state
class PDFAppViewStateHelper
{
public:
    PDFAppViewStateHelper() = delete;

    static QString layoutKey() { return QStringLiteral("layout"); }
    static QString firstPageRightKey() { return QStringLiteral("firstPageRight"); }
    static QString zoomModeKey() { return QStringLiteral("zoomMode"); }
    static QString zoomKey() { return QStringLiteral("zoom"); }
    static QString rotationKey() { return QStringLiteral("rotation"); }
    static QString sidebarVisibleKey() { return QStringLiteral("sidebar"); }
    static QString sidebarWidthKey() { return QStringLiteral("sidebarWidth"); }
    static QString panelKey() { return QStringLiteral("panel"); }

    /// A boolean value of the key (std::nullopt for a missing value or a value of another type)
    static std::optional<bool> readBool(const QJsonObject& object, const QString& key)
    {
        const QJsonValue value = object.value(key);
        return value.isBool() ? std::make_optional(value.toBool()) : std::nullopt;
    }

    /// An integer value of the key in the range (std::nullopt otherwise)
    static std::optional<int> readInteger(const QJsonObject& object, const QString& key, int minimum, int maximum)
    {
        const QJsonValue value = object.value(key);
        if (!value.isDouble())
        {
            return std::nullopt;
        }

        const double number = value.toDouble();
        if (!std::isfinite(number) || number != std::floor(number) || number < minimum || number > maximum)
        {
            return std::nullopt;
        }
        return int(number);
    }
};

bool PDFAppViewState::isEmpty() const
{
    return *this == PDFAppViewState();
}

QString PDFAppViewState::toText() const
{
    using H = PDFAppViewStateHelper;

    QJsonObject object;
    if (layout)
    {
        object.insert(H::layoutKey(), getLayoutName(*layout));
    }
    if (firstPageRight)
    {
        object.insert(H::firstPageRightKey(), *firstPageRight);
    }
    if (zoomMode)
    {
        object.insert(H::zoomModeKey(), getZoomModeName(*zoomMode));
    }
    if (zoom && std::isfinite(*zoom))
    {
        object.insert(H::zoomKey(), qBound(MINIMUM_ZOOM, *zoom, MAXIMUM_ZOOM));
    }
    if (rotation)
    {
        object.insert(H::rotationKey(), *rotation);
    }
    if (sidebarVisible)
    {
        object.insert(H::sidebarVisibleKey(), *sidebarVisible);
    }
    if (sidebarWidth)
    {
        object.insert(H::sidebarWidthKey(), qBound(MINIMUM_SIDEBAR_WIDTH, *sidebarWidth, MAXIMUM_SIDEBAR_WIDTH));
    }
    if (panelId && isValidPanelId(*panelId))
    {
        object.insert(H::panelKey(), *panelId);
    }

    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

PDFAppViewState PDFAppViewState::fromText(const QString& text)
{
    using H = PDFAppViewStateHelper;

    PDFAppViewState state;
    if (text.isEmpty() || text.size() > MAXIMUM_TEXT_LENGTH)
    {
        return state;
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
    {
        return state;
    }

    const QJsonObject object = document.object();

    state.layout = getLayoutFromName(object.value(H::layoutKey()).toString());
    state.firstPageRight = H::readBool(object, H::firstPageRightKey());
    state.zoomMode = getZoomModeFromName(object.value(H::zoomModeKey()).toString());

    const QJsonValue zoomValue = object.value(H::zoomKey());
    if (zoomValue.isDouble())
    {
        const double zoomFactor = zoomValue.toDouble();
        if (std::isfinite(zoomFactor) && zoomFactor >= MINIMUM_ZOOM && zoomFactor <= MAXIMUM_ZOOM)
        {
            state.zoom = zoomFactor;
        }
    }

    const std::optional<int> rotationValue = H::readInteger(object, H::rotationKey(), 0, 270);
    if (rotationValue && *rotationValue % 90 == 0)
    {
        state.rotation = rotationValue;
    }

    state.sidebarVisible = H::readBool(object, H::sidebarVisibleKey());
    state.sidebarWidth = H::readInteger(object, H::sidebarWidthKey(), MINIMUM_SIDEBAR_WIDTH, MAXIMUM_SIDEBAR_WIDTH);

    const QString panel = object.value(H::panelKey()).toString();
    if (isValidPanelId(panel))
    {
        state.panelId = panel;
    }

    // A custom zoom without its factor is no zoom at all
    if (state.zoomMode == PDFAppViewZoomMode::Custom && !state.zoom)
    {
        state.zoomMode = std::nullopt;
    }

    return state;
}

QString PDFAppViewState::getLayoutName(PDFAppViewLayout layout)
{
    switch (layout)
    {
        case PDFAppViewLayout::SinglePage:
            return QStringLiteral("singlePage");
        case PDFAppViewLayout::Continuous:
            return QStringLiteral("continuous");
        case PDFAppViewLayout::TwoPages:
            return QStringLiteral("twoPages");
        case PDFAppViewLayout::TwoColumns:
            return QStringLiteral("twoColumns");
    }

    return QStringLiteral("continuous");
}

std::optional<PDFAppViewLayout> PDFAppViewState::getLayoutFromName(const QString& name)
{
    for (PDFAppViewLayout layout : { PDFAppViewLayout::SinglePage, PDFAppViewLayout::Continuous, PDFAppViewLayout::TwoPages, PDFAppViewLayout::TwoColumns })
    {
        if (name == getLayoutName(layout))
        {
            return layout;
        }
    }
    return std::nullopt;
}

QString PDFAppViewState::getZoomModeName(PDFAppViewZoomMode mode)
{
    switch (mode)
    {
        case PDFAppViewZoomMode::Custom:
            return QStringLiteral("custom");
        case PDFAppViewZoomMode::FitPage:
            return QStringLiteral("fitPage");
        case PDFAppViewZoomMode::FitWidth:
            return QStringLiteral("fitWidth");
        case PDFAppViewZoomMode::FitHeight:
            return QStringLiteral("fitHeight");
    }

    return QStringLiteral("custom");
}

std::optional<PDFAppViewZoomMode> PDFAppViewState::getZoomModeFromName(const QString& name)
{
    for (PDFAppViewZoomMode mode : { PDFAppViewZoomMode::Custom, PDFAppViewZoomMode::FitPage, PDFAppViewZoomMode::FitWidth, PDFAppViewZoomMode::FitHeight })
    {
        if (name == getZoomModeName(mode))
        {
            return mode;
        }
    }
    return std::nullopt;
}

bool PDFAppViewState::isValidPanelId(const QString& panelId)
{
    if (panelId.isEmpty() || panelId.size() > MAXIMUM_PANEL_ID_LENGTH)
    {
        return false;
    }

    for (QChar character : panelId)
    {
        const char16_t code = character.unicode();
        const bool isValid = (code >= u'a' && code <= u'z') || (code >= u'0' && code <= u'9') || code == u'-';
        if (!isValid)
        {
            return false;
        }
    }
    return true;
}

}   // namespace pdf
