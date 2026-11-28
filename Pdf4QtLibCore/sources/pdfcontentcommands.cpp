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

// Edit Content, stage E0, package P03 (session, undo, basic commands): the basic commands and the
// rules of writing back they carry. Plan: GUI_overhaul/edit_tool/zadani_jadro.md 4.4 (R-1, R-2,
// R-3, R-5, R-6), 4.5, 4.12 (consistency of the structure).
//
// The one principle behind every rule: a node the user did not touch sees the same graphics
// state after the command as before it. So
//  - a node, which is moved, copied or wrapped, gets the difference between the state it saw and
//    the state of its new place into its override (R-2) and never leaks there (it is wrapped);
//  - whenever the leak of a node stops reaching the nodes behind it (the node is deleted, moved
//    or wrapped), a residual state run sets the leak at the place of its end (R-3);
//  - therefore the state of every other node stays as the builder has set it, and the nodes a
//    command moves or creates get the state of the place, where they are written
//    (PDFContentNode::getState; the stream writer relies on it).
//
// Three parts: PDFContentCommandHelper (functions without a state: values of the graphics state,
// differences, small objects), PDFContentCommandWork (one computation of a command over the
// editors of the containers) and the commands (PDFContentModelCommand keeps the versions of the
// containers before and after, so undo is the old version and redo the new one).

#include "pdfcontentcommands.h"

#include "pdfblendfunction.h"
#include "pdfcolorspaces.h"
#include "pdfcontentcommandengine.h"
#include "pdfcontenteditsession.h"
#include "pdfcontentjson.h"
#include "pdfcontentstreamtext.h"
#include "pdfcontentstructure.h"
#include "pdfdocument.h"
#include "pdfexception.h"
#include "pdfparser.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <utility>

namespace pdf
{

PDFContentCommand::~PDFContentCommand() = default;

bool PDFContentCommand::mergeWith(const PDFContentCommand& next)
{
    Q_UNUSED(next);
    return false;
}

size_t PDFContentCommand::getStoredItemCount() const
{
    return 0;
}

std::vector<PDFObjectReference> PDFContentCommand::getNewObjects() const
{
    return std::vector<PDFObjectReference>();
}

// ---------------------------------------------------------------------------------------------
//  Delete
// ---------------------------------------------------------------------------------------------

/// Removes nodes (R-3: a residual state run keeps their leak; 4.12: a sequence, which loses its
/// last content, loses its wrap and its place in the structure)
class PDFContentDeleteCommand : public PDFContentModelCommand
{
public:
    explicit PDFContentDeleteCommand(std::vector<PDFContentNodeReference> nodes) :
        m_nodes(std::move(nodes))
    {
        if (!m_nodes.empty())
        {
            setPlace(nullptr, m_nodes.front().container);
        }
    }

    virtual QString getText() const override { return PDFContentCommandFactory::tr("Delete %Ln Object(s)", nullptr, int(m_nodes.size())); }
    virtual QByteArray getType() const override { return PDFContentCommandFactory::TYPE_DELETE; }

    virtual QJsonObject toJson() const override
    {
        QJsonObject object;
        object.insert(QLatin1String(PDFContentJson::KEY_TYPE), QString::fromLatin1(PDFContentCommandFactory::TYPE_DELETE));
        object.insert(QLatin1String(PDFContentCommandJson::KEY_IDS), writeIds(m_nodes));
        return object;
    }

    static PDFContentCommandPointer fromJson(const QJsonObject& object, const PDFContentEditSession* session, const QString& path, std::vector<QString>* errors)
    {
        std::vector<PDFContentNodeReference> nodes;
        bool result = PDFContentCommandJson::checkKeys(object, { PDFContentJson::KEY_ID, PDFContentCommandJson::KEY_IDS }, path, errors);
        result = PDFContentCommandJson::readNodes(object, session, path, errors, &nodes) && result;
        return result ? PDFContentCommandFactory::createDelete(std::move(nodes)) : nullptr;
    }

protected:
    virtual bool compute(Work& work, QString* errorText) override
    {
        if (!checkNodes(m_nodes, PDFTranslationContext::tr("There is no object to delete."), errorText))
        {
            return false;
        }

        std::set<PDFContentNodeReference> selected(m_nodes.cbegin(), m_nodes.cend());
        std::set<PDFContentContainerKey> placed;

        // All the objects are checked, before one is removed
        for (const PDFContentNodeReference& reference : m_nodes)
        {
            Work::Container* container = nullptr;
            const PDFContentNodePointer node = work.getNode(reference, &container, errorText);
            if (!node)
            {
                return false;
            }

            if (placed.insert(reference.container).second)
            {
                setPlace(work.getSession(), reference.container);
            }

            switch (node->getKind())
            {
                case Kind::Root:
                case Kind::StateRun:
                case Kind::TextShow:
                    *errorText = PDFTranslationContext::tr("An object of this kind cannot be deleted.");
                    return false;

                case Kind::Opaque:
                    *errorText = PDFTranslationContext::tr("A part of the content, which cannot be read, cannot be deleted alone.");
                    return false;

                default:
                    break;
            }

            if (Work::isInSourceTextObject(container, reference.node))
            {
                *errorText = PDFTranslationContext::tr("A part of a text cannot be deleted yet.");
                return false;
            }
        }

        for (const PDFContentNodeReference& reference : selected)
        {
            Work::Container* container = work.getContainer(reference.container, errorText);
            const PDFContentNodePointer node = container ? container->editor->getNode(reference.node) : nullptr;
            if (!node)
            {
                // Deleted with a node above it
                continue;
            }

            const PDFContentNodeId parentId = node->getParentId();
            const PDFContentNodeId sequenceId = Work::getSequence(container, parentId);

            if (!work.removeSequences(container, node, errorText) ||
                !work.removeNode(container, reference.node, errorText))
            {
                return false;
            }

            work.refreshLeaks(container, parentId);

            if (sequenceId.isValid() && !work.finishSequence(container, sequenceId, errorText))
            {
                return false;
            }
        }

        return true;
    }

private:
    std::vector<PDFContentNodeReference> m_nodes;
};

// ---------------------------------------------------------------------------------------------
//  Transform
// ---------------------------------------------------------------------------------------------

/// Moves nodes by a matrix of the default space of their container (R-1, R-5, R-6)
class PDFContentTransformCommand : public PDFContentModelCommand
{
public:
    explicit PDFContentTransformCommand(std::vector<PDFContentNodeReference> nodes, const QTransform& matrix) :
        m_nodes(std::move(nodes)),
        m_matrix(matrix)
    {
        if (!m_nodes.empty())
        {
            setPlace(nullptr, m_nodes.front().container);
        }
    }

    virtual QString getText() const override { return PDFContentCommandFactory::tr("Move %Ln Object(s)", nullptr, int(m_nodes.size())); }
    virtual QByteArray getType() const override { return PDFContentCommandFactory::TYPE_TRANSFORM; }

    virtual bool mergeWith(const PDFContentCommand& next) override
    {
        // Moves by the arrow keys: the same objects again within the merge interval
        const PDFContentTransformCommand* other = dynamic_cast<const PDFContentTransformCommand*>(&next);
        if (!other || other->m_nodes != m_nodes || !mergeResult(*other))
        {
            return false;
        }

        m_matrix = m_matrix * other->m_matrix;
        return true;
    }

    virtual QJsonObject toJson() const override
    {
        QJsonObject object;
        object.insert(QLatin1String(PDFContentJson::KEY_TYPE), QString::fromLatin1(PDFContentCommandFactory::TYPE_TRANSFORM));
        object.insert(QLatin1String(PDFContentCommandJson::KEY_IDS), writeIds(m_nodes));
        object.insert(QLatin1String(PDFContentCommandJson::KEY_MATRIX), PDFContentJson::writeMatrix(m_matrix));
        return object;
    }

    static PDFContentCommandPointer fromJson(const QJsonObject& object, const PDFContentEditSession* session, const QString& path, std::vector<QString>* errors)
    {
        std::vector<PDFContentNodeReference> nodes;
        QTransform matrix;
        bool result = PDFContentCommandJson::checkKeys(object, { PDFContentJson::KEY_ID, PDFContentCommandJson::KEY_IDS, PDFContentCommandJson::KEY_MATRIX }, path, errors);
        result = PDFContentCommandJson::readNodes(object, session, path, errors, &nodes) && result;
        result = PDFContentCommandJson::readMatrix(object, true, path, errors, &matrix) && result;
        return result ? PDFContentCommandFactory::createTransform(std::move(nodes), matrix) : nullptr;
    }

protected:
    virtual bool compute(Work& work, QString* errorText) override
    {
        if (!checkNodes(m_nodes, PDFTranslationContext::tr("There is no object to move."), errorText))
        {
            return false;
        }

        // A singular or infinite matrix is an error of the command (zadani_overeni.md 6.2)
        if (!Helper::isRegularMatrix(m_matrix))
        {
            *errorText = PDFTranslationContext::tr("The transformation is not a regular matrix of finite numbers.");
            return false;
        }

        // All the objects are checked in the tree as it is before the command, then they move:
        // the set of the named objects is made once, and each object walks up its tree once
        const std::set<PDFContentNodeReference> named(m_nodes.cbegin(), m_nodes.cend());
        std::set<PDFContentContainerKey> placed;
        std::vector<std::pair<Work::Container*, PDFContentNodeId>> moved;

        for (const PDFContentNodeReference& reference : m_nodes)
        {
            Work::Container* container = nullptr;
            const PDFContentNodePointer node = work.getNode(reference, &container, errorText);
            if (!node)
            {
                return false;
            }

            if (placed.insert(reference.container).second)
            {
                setPlace(work.getSession(), reference.container);
            }

            bool isCovered = false;
            for (const PDFContentNodePointer& ancestor : Work::getSourceAncestors(container, reference.node))
            {
                if (ancestor->getKind() == Kind::TextObject)
                {
                    *errorText = PDFTranslationContext::tr("A part of a text cannot be moved yet.");
                    return false;
                }

                // An object below another named one moves with it, once
                isCovered = isCovered || named.count(PDFContentNodeReference{ reference.container, ancestor->getId() }) > 0;
            }

            // The object itself only: an object inside of an unclosed one gets its wrap there
            if (container->source->isUnclosed(reference.node))
            {
                *errorText = PDFTranslationContext::tr("The object is not closed in the content, so it cannot be moved.");
                return false;
            }

            if (!isCovered)
            {
                moved.emplace_back(container, reference.node);
            }
        }

        size_t count = 0;
        for (const auto& item : moved)
        {
            if (!work.transformNode(item.first, item.second, m_matrix, true, 0, &count, errorText))
            {
                return false;
            }
        }

        if (count == 0)
        {
            // The objects exist, but none of them holds anything, which moves (an empty group,
            // a marked content with a marked point only): the model is what the command asks
            // for already
            *errorText = PDFTranslationContext::tr("The objects hold nothing, which could be moved.");
            setRefusedAsUnchanged(true);
            return false;
        }

        return true;
    }

private:
    std::vector<PDFContentNodeReference> m_nodes;
    QTransform m_matrix;
};

// ---------------------------------------------------------------------------------------------
//  Set Appearance, Set Transparency
// ---------------------------------------------------------------------------------------------

/// Sets properties of the override of nodes (R-1: on leaves only): the appearance (colors and
/// line parameters) or the transparency (alphas and blend mode)
class PDFContentSetPropertiesCommand : public PDFContentModelCommand
{
public:
    explicit PDFContentSetPropertiesCommand(std::vector<PDFContentNodeReference> nodes, std::map<Property, PDFContentStateValue> properties, bool isAppearance, PDFContentPaintSwitches switches = PDFContentPaintSwitches()) :
        m_nodes(std::move(nodes)),
        m_properties(std::move(properties)),
        m_switches(switches),
        m_isAppearance(isAppearance)
    {
        if (!m_nodes.empty())
        {
            setPlace(nullptr, m_nodes.front().container);
        }
    }

    virtual QString getText() const override
    {
        return m_isAppearance ? PDFContentCommandFactory::tr("Change Appearance") : PDFContentCommandFactory::tr("Change Transparency");
    }

    virtual QByteArray getType() const override
    {
        return m_isAppearance ? PDFContentCommandFactory::TYPE_SET_APPEARANCE : PDFContentCommandFactory::TYPE_SET_TRANSPARENCY;
    }

    virtual bool mergeWith(const PDFContentCommand& next) override
    {
        // A dragged value: the same properties of the same objects again within the merge interval
        const PDFContentSetPropertiesCommand* other = dynamic_cast<const PDFContentSetPropertiesCommand*>(&next);
        if (!other || other->m_isAppearance != m_isAppearance || other->m_nodes != m_nodes || other->m_properties.size() != m_properties.size() ||
            other->m_switches.fill.has_value() != m_switches.fill.has_value() || other->m_switches.stroke.has_value() != m_switches.stroke.has_value())
        {
            // A switch of the painting is a field as the properties: only the same fields merge
            return false;
        }

        for (const auto& item : other->m_properties)
        {
            if (!m_properties.count(item.first))
            {
                return false;
            }
        }

        if (!mergeResult(*other))
        {
            return false;
        }

        m_properties = other->m_properties;
        m_switches = other->m_switches;
        return true;
    }

    virtual QJsonObject toJson() const override
    {
        using Json = PDFContentCommandJson;

        QJsonObject object;
        object.insert(QLatin1String(PDFContentJson::KEY_TYPE), QString::fromLatin1(getType()));
        object.insert(QLatin1String(Json::KEY_IDS), writeIds(m_nodes));

        if (m_switches.fill)
        {
            object.insert(QLatin1String(Json::KEY_FILL_ENABLED), *m_switches.fill);
        }
        if (m_switches.stroke)
        {
            object.insert(QLatin1String(Json::KEY_STROKE_ENABLED), *m_switches.stroke);
        }

        for (const auto& item : m_properties)
        {
            const PDFContentStateValue& value = item.second;
            switch (item.first)
            {
                case Property::FillColor:
                    object.insert(QLatin1String(Json::KEY_FILL), PDFContentJson::writeColor(value.color));
                    break;
                case Property::StrokeColor:
                    object.insert(QLatin1String(Json::KEY_STROKE), PDFContentJson::writeColor(value.color));
                    break;
                case Property::LineWidth:
                    object.insert(QLatin1String(Json::KEY_LINE_WIDTH), value.real);
                    break;
                case Property::LineCap:
                    object.insert(QLatin1String(Json::KEY_LINE_CAP), qint64(value.integer));
                    break;
                case Property::LineJoin:
                    object.insert(QLatin1String(Json::KEY_LINE_JOIN), qint64(value.integer));
                    break;
                case Property::MiterLimit:
                    object.insert(QLatin1String(Json::KEY_MITER_LIMIT), value.real);
                    break;
                case Property::DashPattern:
                {
                    QJsonArray array;
                    for (const PDFReal item2 : value.dash.getDashArray())
                    {
                        array.append(item2);
                    }
                    QJsonObject dash;
                    dash.insert(QLatin1String(Json::KEY_DASH_ARRAY), array);
                    dash.insert(QLatin1String(Json::KEY_DASH_PHASE), value.dash.getDashOffset());
                    object.insert(QLatin1String(Json::KEY_DASH), dash);
                    break;
                }
                case Property::AlphaStroking:
                    object.insert(QLatin1String(Json::KEY_STROKE_ALPHA), value.real);
                    break;
                case Property::AlphaFilling:
                    object.insert(QLatin1String(Json::KEY_FILL_ALPHA), value.real);
                    break;
                case Property::BlendMode:
                    object.insert(QLatin1String(Json::KEY_BLEND_MODE), QString::fromLatin1(value.name));
                    break;
                default:
                    break;
            }
        }
        return object;
    }

    static PDFContentCommandPointer appearanceFromJson(const QJsonObject& object, const PDFContentEditSession* session, const QString& path, std::vector<QString>* errors)
    {
        using Json = PDFContentCommandJson;
        using ValueKind = PDFContentStateValue::Kind;

        std::vector<PDFContentNodeReference> nodes;
        std::map<Property, PDFContentStateValue> properties;
        PDFContentPaintSwitches switches;

        bool result = Json::checkKeys(object, { PDFContentJson::KEY_ID, Json::KEY_IDS, Json::KEY_FILL, Json::KEY_STROKE, Json::KEY_LINE_WIDTH, Json::KEY_LINE_CAP,
                                                Json::KEY_LINE_JOIN, Json::KEY_MITER_LIMIT, Json::KEY_DASH, Json::KEY_FILL_ENABLED, Json::KEY_STROKE_ENABLED }, path, errors);
        result = Json::readNodes(object, session, path, errors, &nodes) && result;

        for (const auto& item : { std::make_pair(Json::KEY_FILL_ENABLED, &switches.fill), std::make_pair(Json::KEY_STROKE_ENABLED, &switches.stroke) })
        {
            if (object.contains(QLatin1String(item.first)))
            {
                const QJsonValue value = object.value(QLatin1String(item.first));
                if (value.isBool())
                {
                    *item.second = value.toBool();
                }
                else
                {
                    Json::addError(errors, path, item.first, PDFTranslationContext::tr("true or false is expected."));
                    result = false;
                }
            }
        }

        for (const auto& item : { std::make_pair(Json::KEY_FILL, Property::FillColor), std::make_pair(Json::KEY_STROKE, Property::StrokeColor) })
        {
            if (object.contains(QLatin1String(item.first)))
            {
                QString errorText;
                const std::optional<PDFContentColorValue> color = PDFContentJson::readColor(object.value(QLatin1String(item.first)), &errorText);
                if (color)
                {
                    PDFContentStateValue value;
                    value.kind = ValueKind::Color;
                    value.color = *color;
                    properties[item.second] = std::move(value);
                }
                else
                {
                    Json::addError(errors, path, item.first, !errorText.isEmpty() ? errorText : PDFTranslationContext::tr("A color is expected."));
                    result = false;
                }
            }
        }

        for (const auto& item : { std::make_pair(Json::KEY_LINE_WIDTH, Property::LineWidth), std::make_pair(Json::KEY_MITER_LIMIT, Property::MiterLimit) })
        {
            if (object.contains(QLatin1String(item.first)))
            {
                PDFContentStateValue value;
                value.kind = ValueKind::Real;
                if (Json::readReal(object.value(QLatin1String(item.first)), &value.real))
                {
                    properties[item.second] = std::move(value);
                }
                else
                {
                    Json::addError(errors, path, item.first, PDFTranslationContext::tr("A number is expected."));
                    result = false;
                }
            }
        }

        for (const auto& item : { std::make_pair(Json::KEY_LINE_CAP, Property::LineCap), std::make_pair(Json::KEY_LINE_JOIN, Property::LineJoin) })
        {
            if (object.contains(QLatin1String(item.first)))
            {
                PDFContentStateValue value;
                value.kind = ValueKind::Integer;
                if (Json::readInteger(object.value(QLatin1String(item.first)), 0, 2, &value.integer))
                {
                    properties[item.second] = std::move(value);
                }
                else
                {
                    Json::addError(errors, path, item.first, PDFTranslationContext::tr("One of the numbers 0, 1, 2 is expected."));
                    result = false;
                }
            }
        }

        if (object.contains(QLatin1String(Json::KEY_DASH)))
        {
            const QJsonValue dashValue = object.value(QLatin1String(Json::KEY_DASH));
            const QJsonObject dash = dashValue.toObject();
            const QJsonValue arrayValue = dash.value(QLatin1String(Json::KEY_DASH_ARRAY));

            std::vector<PDFReal> dashArray;
            PDFReal phase = 0.0;
            bool isDashValid = dashValue.isObject() && arrayValue.isArray() && arrayValue.toArray().size() <= MAXIMUM_DASH_COUNT;

            if (isDashValid)
            {
                for (const QJsonValue& number : arrayValue.toArray())
                {
                    PDFReal real = 0.0;
                    isDashValid = isDashValid && Json::readReal(number, &real);
                    dashArray.push_back(real);
                }

                for (auto it = dash.constBegin(); it != dash.constEnd(); ++it)
                {
                    isDashValid = isDashValid && (it.key() == QLatin1String(Json::KEY_DASH_ARRAY) || it.key() == QLatin1String(Json::KEY_DASH_PHASE));
                }

                if (dash.contains(QLatin1String(Json::KEY_DASH_PHASE)))
                {
                    isDashValid = isDashValid && Json::readReal(dash.value(QLatin1String(Json::KEY_DASH_PHASE)), &phase);
                }
            }

            if (isDashValid)
            {
                PDFContentStateValue value;
                value.kind = ValueKind::Dash;
                value.dash = PDFLineDashPattern(dashArray, phase);
                properties[Property::DashPattern] = std::move(value);
            }
            else
            {
                Json::addError(errors, path, Json::KEY_DASH, PDFTranslationContext::tr("A dash pattern { \"array\": [...], \"phase\": number } is expected."));
                result = false;
            }
        }

        return result ? PDFContentCommandFactory::createSetAppearance(std::move(nodes), std::move(properties), switches) : nullptr;
    }

    static PDFContentCommandPointer transparencyFromJson(const QJsonObject& object, const PDFContentEditSession* session, const QString& path, std::vector<QString>* errors)
    {
        using Json = PDFContentCommandJson;

        std::vector<PDFContentNodeReference> nodes;
        std::optional<PDFReal> alphaStroking;
        std::optional<PDFReal> alphaFilling;
        std::optional<QByteArray> blendMode;

        bool result = Json::checkKeys(object, { PDFContentJson::KEY_ID, Json::KEY_IDS, Json::KEY_STROKE_ALPHA, Json::KEY_FILL_ALPHA, Json::KEY_BLEND_MODE }, path, errors);
        result = Json::readNodes(object, session, path, errors, &nodes) && result;

        for (const auto& item : { std::make_pair(Json::KEY_STROKE_ALPHA, &alphaStroking), std::make_pair(Json::KEY_FILL_ALPHA, &alphaFilling) })
        {
            if (object.contains(QLatin1String(item.first)))
            {
                PDFReal real = 0.0;
                if (Json::readReal(object.value(QLatin1String(item.first)), &real))
                {
                    *item.second = real;
                }
                else
                {
                    Json::addError(errors, path, item.first, PDFTranslationContext::tr("A number from 0 to 1 is expected."));
                    result = false;
                }
            }
        }

        if (object.contains(QLatin1String(Json::KEY_BLEND_MODE)))
        {
            const QJsonValue value = object.value(QLatin1String(Json::KEY_BLEND_MODE));
            if (value.isString())
            {
                blendMode = value.toString().toLatin1();
            }
            else
            {
                Json::addError(errors, path, Json::KEY_BLEND_MODE, PDFTranslationContext::tr("The name of a blend mode is expected."));
                result = false;
            }
        }

        return result ? PDFContentCommandFactory::createSetTransparency(std::move(nodes), alphaStroking, alphaFilling, blendMode) : nullptr;
    }

protected:
    /// The values are checked against their tables (ISO 32000-2, 8.4.3 and Table 57)
    bool checkValues(QString* errorText) const
    {
        using ValueKind = PDFContentStateValue::Kind;

        if (m_properties.empty() && m_switches.isEmpty())
        {
            *errorText = PDFTranslationContext::tr("There is no property to set.");
            return false;
        }

        if (!m_isAppearance && !m_switches.isEmpty())
        {
            // The painting is switched by the appearance only
            *errorText = PDFTranslationContext::tr("A value of the change is not valid.");
            return false;
        }

        for (const auto& item : m_properties)
        {
            const PDFContentStateValue& value = item.second;
            bool isValid = false;

            switch (item.first)
            {
                case Property::FillColor:
                case Property::StrokeColor:
                {
                    // Stage E0 sets colors of the device spaces only (zadani_jadro.md 4.5): the
                    // number of the components of another space is known by its object alone
                    // (an ICC profile, a Separation), and a pattern needs its object - the
                    // commands of stage E4 set those and check them (ISO 32000-2, 8.6.8: the
                    // operands of scn are the components of the current space)
                    const size_t count = value.color.deviceSpace == "DeviceGray" ? 1 : value.color.deviceSpace == "DeviceRGB" ? 3 : value.color.deviceSpace == "DeviceCMYK" ? 4 : 0;
                    isValid = m_isAppearance && value.kind == ValueKind::Color && count > 0 && count == value.color.components.size() &&
                              value.color.colorSpaceObject.isNull() && !value.color.isPattern() &&
                              std::all_of(value.color.components.cbegin(), value.color.components.cend(), [](PDFReal component) { return std::isfinite(component) && component >= 0.0 && component <= 1.0; });
                    break;
                }

                case Property::LineWidth:
                    isValid = m_isAppearance && value.kind == ValueKind::Real && std::isfinite(value.real) && value.real >= 0.0;
                    break;

                case Property::MiterLimit:
                    isValid = m_isAppearance && value.kind == ValueKind::Real && std::isfinite(value.real) && value.real > 0.0;
                    break;

                case Property::LineCap:
                case Property::LineJoin:
                    isValid = m_isAppearance && value.kind == ValueKind::Integer && value.integer >= 0 && value.integer <= 2;
                    break;

                case Property::DashPattern:
                {
                    // 8.4.3.6: no negative number, not all of them zero
                    const std::vector<PDFReal>& dashArray = value.dash.getDashArray();
                    isValid = m_isAppearance && value.kind == ValueKind::Dash && dashArray.size() <= size_t(MAXIMUM_DASH_COUNT) && std::isfinite(value.dash.getDashOffset()) &&
                              std::all_of(dashArray.cbegin(), dashArray.cend(), [](PDFReal item2) { return std::isfinite(item2) && item2 >= 0.0; }) &&
                              (dashArray.empty() || std::any_of(dashArray.cbegin(), dashArray.cend(), [](PDFReal item2) { return item2 > 0.0; }));
                    break;
                }

                case Property::AlphaStroking:
                case Property::AlphaFilling:
                    isValid = !m_isAppearance && value.kind == ValueKind::Real && std::isfinite(value.real) && value.real >= 0.0 && value.real <= 1.0;
                    break;

                case Property::BlendMode:
                    isValid = !m_isAppearance && value.kind == ValueKind::Name && Helper::isBlendModeName(value.name);
                    break;

                default:
                    break;
            }

            if (!isValid)
            {
                *errorText = PDFTranslationContext::tr("A value of the change is not valid.");
                return false;
            }
        }

        return true;
    }

    virtual bool compute(Work& work, QString* errorText) override
    {
        if (!checkNodes(m_nodes, PDFTranslationContext::tr("There is no object to change."), errorText))
        {
            return false;
        }

        if (!checkValues(errorText))
        {
            return false;
        }

        // As a transform: the checks in the tree before the command, then the changes
        const std::set<PDFContentNodeReference> named(m_nodes.cbegin(), m_nodes.cend());
        std::set<PDFContentContainerKey> placed;
        std::vector<std::pair<Work::Container*, PDFContentNodeId>> changed;

        for (const PDFContentNodeReference& reference : m_nodes)
        {
            Work::Container* container = nullptr;
            const PDFContentNodePointer node = work.getNode(reference, &container, errorText);
            if (!node)
            {
                return false;
            }

            if (placed.insert(reference.container).second)
            {
                setPlace(work.getSession(), reference.container);
            }

            bool isCovered = false;
            for (const PDFContentNodePointer& ancestor : Work::getSourceAncestors(container, reference.node))
            {
                if (ancestor->getKind() == Kind::TextObject)
                {
                    *errorText = PDFTranslationContext::tr("A part of a text cannot be restyled yet.");
                    return false;
                }

                // An object below another named one is changed with it, once
                isCovered = isCovered || named.count(PDFContentNodeReference{ reference.container, ancestor->getId() }) > 0;
            }

            if (!isCovered)
            {
                changed.emplace_back(container, reference.node);
            }
        }

        size_t count = 0;
        for (const auto& item : changed)
        {
            if (!work.setProperties(item.first, item.second, m_properties, m_switches, m_isAppearance, true, 0, &count, errorText))
            {
                return false;
            }
        }

        if (count == 0)
        {
            // The objects exist, but none of them holds anything, which this command changes
            // (an empty group, a group of images for the appearance): nothing to change
            *errorText = PDFTranslationContext::tr("The objects hold nothing, which this change could change.");
            setRefusedAsUnchanged(true);
            return false;
        }

        return true;
    }

private:
    static constexpr qsizetype MAXIMUM_DASH_COUNT = 1024;

    std::vector<PDFContentNodeReference> m_nodes;
    std::map<Property, PDFContentStateValue> m_properties;
    PDFContentPaintSwitches m_switches;
    bool m_isAppearance = true;
};

// ---------------------------------------------------------------------------------------------
//  Reorder, Copy, Group, Ungroup: the commands, which put nodes to another place (R-2)
// ---------------------------------------------------------------------------------------------

/// Moves nodes of one container to another place of its tree (R-2; 4.12 for the tags)
class PDFContentReorderCommand : public PDFContentPlaceCommand
{
public:
    using ReorderMode = PDFContentCommandFactory::ReorderMode;

    explicit PDFContentReorderCommand(std::vector<PDFContentNodeReference> nodes, PDFContentNodeReference parent, PDFInteger index, ReorderMode mode) :
        m_nodes(std::move(nodes)),
        m_parent(parent),
        m_index(index),
        m_mode(mode)
    {
        setPlace(nullptr, m_parent.container);
    }

    virtual QString getText() const override { return PDFContentCommandFactory::tr("Reorder %Ln Object(s)", nullptr, int(m_nodes.size())); }
    virtual QByteArray getType() const override { return PDFContentCommandFactory::TYPE_REORDER; }

    virtual QJsonObject toJson() const override
    {
        using Json = PDFContentCommandJson;

        QJsonObject object;
        object.insert(QLatin1String(PDFContentJson::KEY_TYPE), QString::fromLatin1(PDFContentCommandFactory::TYPE_REORDER));
        object.insert(QLatin1String(Json::KEY_IDS), writeIds(m_nodes));
        object.insert(QLatin1String(Json::KEY_PARENT), writeId(m_parent));
        object.insert(QLatin1String(Json::KEY_INDEX), qint64(m_index));
        object.insert(QLatin1String(Json::KEY_MODE), QString::fromLatin1(m_mode == ReorderMode::KeepLayerAndTag ? Json::MODE_KEEP_LAYER_AND_TAG : Json::MODE_FOLLOW_PARENT));
        return object;
    }

    static PDFContentCommandPointer fromJson(const QJsonObject& object, const PDFContentEditSession* session, const QString& path, std::vector<QString>* errors)
    {
        using Json = PDFContentCommandJson;

        std::vector<PDFContentNodeReference> nodes;
        PDFContentNodeReference parent;
        PDFInteger index = -1;
        ReorderMode mode = ReorderMode::FollowParent;

        bool result = Json::checkKeys(object, { PDFContentJson::KEY_ID, Json::KEY_IDS, Json::KEY_PARENT, Json::KEY_INDEX, Json::KEY_MODE }, path, errors);
        result = Json::readNodes(object, session, path, errors, &nodes) && result;
        result = Json::readTarget(object, session, path, errors, &parent, &index) && result;

        if (object.contains(QLatin1String(Json::KEY_MODE)))
        {
            const QJsonValue value = object.value(QLatin1String(Json::KEY_MODE));
            if (value.isString() && value.toString() == QLatin1String(Json::MODE_KEEP_LAYER_AND_TAG))
            {
                mode = ReorderMode::KeepLayerAndTag;
            }
            else if (!value.isString() || value.toString() != QLatin1String(Json::MODE_FOLLOW_PARENT))
            {
                Json::addError(errors, path, Json::KEY_MODE, PDFTranslationContext::tr("One of 'follow_parent' and 'keep_layer_and_tag' is expected."));
                result = false;
            }
        }

        return result ? PDFContentCommandFactory::createReorder(std::move(nodes), parent, index, mode) : nullptr;
    }

protected:
    virtual bool compute(Work& work, QString* errorText) override
    {
        if (!checkNodes(m_nodes, PDFTranslationContext::tr("There is no object to move."), errorText))
        {
            return false;
        }

        Work::Container* container = nullptr;
        const PDFContentNodePointer parent = work.getNode(m_parent, &container, errorText);
        if (!parent || !checkParent(container, parent, errorText))
        {
            return false;
        }

        setPlace(work.getSession(), m_parent.container);

        // The new parent and the nodes above it: an object cannot go into itself
        std::set<PDFContentNodeId> parentChain = { m_parent.node };
        for (const PDFContentNodePointer& ancestor : Work::getSourceAncestors(container, m_parent.node))
        {
            parentChain.insert(ancestor->getId());
        }

        std::vector<PDFContentNodeId> nodes;
        std::set<PDFContentNodeId> moved;

        for (const PDFContentNodeReference& reference : m_nodes)
        {
            if (reference.container != m_parent.container)
            {
                *errorText = PDFTranslationContext::tr("The objects are not of the content of their new place.");
                return false;
            }

            Work::Container* nodeContainer = nullptr;
            const PDFContentNodePointer node = work.getNode(reference, &nodeContainer, errorText);
            if (!node || !checkMovable(container, node, errorText))
            {
                return false;
            }

            if (parentChain.count(reference.node))
            {
                *errorText = PDFTranslationContext::tr("An object cannot be placed into itself.");
                return false;
            }

            moved.insert(reference.node);
            nodes.push_back(reference.node);
        }

        // An object below another moved one stays inside of it: each object walks up once
        // against the set of the moved ones
        for (const PDFContentNodeId id : nodes)
        {
            for (const PDFContentNodePointer& ancestor : Work::getSourceAncestors(container, id))
            {
                if (moved.count(ancestor->getId()))
                {
                    *errorText = PDFTranslationContext::tr("An object and an object inside of it cannot be moved together.");
                    return false;
                }
            }
        }

        // The sequences of the structure do not nest (zadani_jadro.md 4.12)
        const PDFContentNodeId targetSequence = Work::getSequence(container, m_parent.node);

        PDFContentNodeId anchor;
        if (!getAnchor(container, m_parent.node, m_index, moved, &anchor, errorText) ||
            !checkPlace(container, m_parent.node, anchor, errorText))
        {
            return false;
        }

        // The nodes stand right in front of the place already, in the order of the command:
        // nothing moves (the command changes nothing then and is refused)
        {
            const PDFContentNodePointer currentParent = container->editor->getNode(m_parent.node);
            const std::vector<PDFContentNodePointer>& children = currentParent->getChildren();
            const size_t anchorIndex = size_t(getAnchorIndex(container, m_parent.node, anchor));

            bool isEveryNodeAtPlace = anchorIndex >= nodes.size() && anchorIndex <= children.size();
            for (size_t i = 0; isEveryNodeAtPlace && i < nodes.size(); ++i)
            {
                isEveryNodeAtPlace = children[anchorIndex - nodes.size() + i]->getId() == nodes[i];
            }

            if (isEveryNodeAtPlace)
            {
                return true;
            }
        }

        // An order command changes the painting order only: when the nodes are painted at the
        // place already, in the order of the command, through the groups and the marked
        // contents around them, it changes nothing - no sequence is split, no node leaves its
        // group (a check per node cannot see it: the first node is not at the place, while the
        // others are still in front of the place)
        if (m_mode == ReorderMode::KeepLayerAndTag && isPaintedInOrderAtPlace(container, m_parent.node, anchor, nodes, moved))
        {
            return true;
        }

        std::vector<PDFContentNodeId> emptied;

        for (size_t i = 0; i < nodes.size(); ++i)
        {
            const PDFContentNodeId id = nodes[i];
            const PDFContentNodePointer node = container->editor->getNode(id);
            const PDFContentNodeId oldParentId = node->getParentId();

            if (targetSequence.isValid() && !Work::isAncestorOrSelf(container, targetSequence, id))
            {
                for (const PDFContentNodePointer& item : Helper::getSubtree(node))
                {
                    if (item->getKind() == Kind::MarkedContent && item->getData().mcid)
                    {
                        *errorText = PDFTranslationContext::tr("Tagged content cannot be placed into other tagged content.");
                        return false;
                    }
                }
            }

            const bool isSuccess = m_mode == ReorderMode::KeepLayerAndTag ? moveWithWraps(work, container, id, i + 1 == nodes.size(), &anchor, errorText)
                                                                          : moveToParent(work, container, id, oldParentId, anchor, &emptied, errorText);
            if (!isSuccess)
            {
                return false;
            }
        }

        // The sequences, which lost their last content, lose their wraps now, when every object
        // stands at its place: a wrap may be the anchor of the place of the later objects
        for (const PDFContentNodeId sequenceId : emptied)
        {
            if (container->editor->getNode(sequenceId) && !work.dissolveMarkedContent(container, sequenceId, errorText))
            {
                return false;
            }
        }

        return true;
    }

private:
    /// The items of a subtree in the order they are painted, at the grain of an order command:
    /// a named node is one item (it moves as a whole), every other node, which paints, is one
    /// item; a state run and a container are none (their children are). A container deeper than
    /// the limit of the nesting is one item.
    static void collectPaintedItems(const PDFContentNodePointer& node, const std::set<PDFContentNodeId>& named, int depth, std::vector<PDFContentNodeId>* items)
    {
        if (named.count(node->getId()))
        {
            items->push_back(node->getId());
            return;
        }

        if (node->getKind() == Kind::StateRun)
        {
            return;
        }

        if (isContentLeaf(*node) || depth > PDFContentContainer::MAXIMUM_DEPTH + 2)
        {
            items->push_back(node->getId());
            return;
        }

        for (const PDFContentNodePointer& child : node->getChildren())
        {
            collectPaintedItems(child, named, depth + 1, items);
        }
    }

    /// The nodes are painted at the place in front of the anchor already, in the order of the
    /// command: the painting order of the subtree of the parent after the command (what is in
    /// front of the place without the nodes, the nodes, what is behind the place without the
    /// nodes) is the order it has. A node outside of the subtree of the parent is never at the
    /// place.
    static bool isPaintedInOrderAtPlace(const Work::Container* container,
                                        PDFContentNodeId parentId,
                                        PDFContentNodeId anchor,
                                        const std::vector<PDFContentNodeId>& nodes,
                                        const std::set<PDFContentNodeId>& named)
    {
        const PDFContentNodePointer parent = container->editor->getNode(parentId);
        const PDFInteger anchorIndex = getAnchorIndex(container, parentId, anchor);
        if (!parent || anchorIndex < 0 || anchorIndex > PDFInteger(parent->getChildren().size()))
        {
            return false;
        }

        // The items of the subtree, and the index of the place among them
        std::vector<PDFContentNodeId> items;
        size_t place = 0;
        const std::vector<PDFContentNodePointer>& children = parent->getChildren();
        for (size_t i = 0; i < children.size(); ++i)
        {
            if (PDFInteger(i) == anchorIndex)
            {
                place = items.size();
            }
            collectPaintedItems(children[i], named, 1, &items);
        }
        if (anchorIndex == PDFInteger(children.size()))
        {
            place = items.size();
        }

        const auto middle = items.cbegin() + std::ptrdiff_t(place);
        std::vector<PDFContentNodeId> after;
        after.reserve(items.size() + nodes.size());
        std::copy_if(items.cbegin(), middle, std::back_inserter(after), [&named](PDFContentNodeId id) { return !named.count(id); });
        after.insert(after.end(), nodes.cbegin(), nodes.cend());
        std::copy_if(middle, items.cend(), std::back_inserter(after), [&named](PDFContentNodeId id) { return !named.count(id); });
        return after == items;
    }

    /// A drag of a row of the tree: the node belongs to its new parent. It leaves its sequence
    /// (in the middle the sequence is split, the last content takes the wrap away).
    /// \param emptied The sequences without content, whose wraps the command removes at its end
    bool moveToParent(Work& work,
                      Work::Container* container,
                      PDFContentNodeId id,
                      PDFContentNodeId oldParentId,
                      PDFContentNodeId anchor,
                      std::vector<PDFContentNodeId>* emptied,
                      QString* errorText)
    {
        if (isAtPlace(container, id, m_parent.node, anchor))
        {
            return true;
        }

        const PDFInteger structuralParentKey = container->source->getStructuralParentKey();
        const PDFContentNodeId sequenceId = Work::getSequence(container, oldParentId);
        const bool isLeavingSequence = sequenceId.isValid() && !Work::isAncestorOrSelf(container, sequenceId, m_parent.node);

        PDFContentStructureChange change;
        PDFInteger splitIndex = -1;

        if (isLeavingSequence)
        {
            const PDFContentNodePointer sequence = container->editor->getNode(sequenceId);
            const std::vector<PDFContentNodePointer>& children = sequence->getChildren();

            // The child of the sequence, which is the node or holds it. A node deeper in the
            // sequence leaves what holds it there: the sequence stays one piece then.
            const bool isOtherContent = Helper::hasContentWithout(*sequence, id);
            bool isContentBefore = false;
            bool isContentBehind = false;
            bool isChildFound = false;
            for (size_t i = 0; i < children.size(); ++i)
            {
                if (children[i]->getId() == id)
                {
                    isChildFound = true;
                    splitIndex = PDFInteger(i);
                }
                else if (Work::isAncestorOrSelf(container, children[i]->getId(), id))
                {
                    isChildFound = true;
                }
                else if (Helper::hasContent(*children[i]))
                {
                    (isChildFound ? isContentBehind : isContentBefore) = true;
                }
            }

            const bool isInMiddle = splitIndex >= 0 && isContentBefore && isContentBehind;
            const PDFContentStructureModelPointer structure = work.getStructure();
            if (structure)
            {
                change = structure->getChangeForMoveOut(structuralParentKey, *sequence->getData().mcid, !isOtherContent, isInMiddle, work.getLargestMarkedContentId(container));
                if (!work.addStructureChange(container, change, errorText))
                {
                    return false;
                }
            }
        }

        PDFContentNodeId topId;
        if (!moveAndPlace(work, container, id, m_parent.node, anchor, &topId, errorText))
        {
            return false;
        }

        if (change.action == PDFContentStructureChange::Action::RemoveSequence)
        {
            emptied->push_back(sequenceId);
            return true;
        }

        if (change.action == PDFContentStructureChange::Action::SplitSequence && splitIndex >= 0)
        {
            PDFContentNodeId pieceId;
            return splitSequence(work, container, sequenceId, splitIndex, change.newMcid, &pieceId, errorText);
        }

        return true;
    }

    /// The node is the only content of a parent (a state run and an empty container are none).
    /// The children are read through the editor one by one, and the walk ends at the first
    /// other content: the node of a parent with many children is not made for each of them.
    static bool isOnlyContent(const Work::Container* container, PDFContentNodeId parentId, PDFContentNodeId id)
    {
        const PDFInteger count = Work::getChildCount(container, parentId);
        for (PDFInteger i = 0; i < count; ++i)
        {
            const PDFContentNodeId childId = container->editor->getChildId(parentId, i);
            if (childId != id && Work::hasContent(container, childId))
            {
                return false;
            }
        }
        return true;
    }

    /// Makes a child of a sequence of the structure its only content, so that it can move with
    /// the wrap and keep its tag: the sequence is split in front of the child and behind it.
    /// A piece, which is cut off, gets a new identifier of the same element, and its child of
    /// the element stands right behind the child of the piece it was cut from. So the children
    /// of the element keep the order of the content - the reading order never changes by an
    /// order command (zadani_jadro.md 4.12; ISO 32000-2, 14.7.2: the order of /K is the logical
    /// order). For "A B C" with B: [m: A] [m1: B] [m2: C] and /K m, m1, m2. The first piece
    /// keeps the identifier and the wrap of the sequence.
    /// \param sequenceId The sequence; on return the piece, whose only content the child is
    static bool isolateInSequence(Work& work, Work::Container* container, PDFContentNodeId* sequenceId, PDFContentNodeId id, QString* errorText)
    {
        const PDFInteger structuralParentKey = container->source->getStructuralParentKey();
        const PDFContentStructureModelPointer structure = work.getStructure();
        if (!structure)
        {
            *errorText = PDFTranslationContext::tr("The structure of the document cannot be read, so the tag of the object cannot be kept.");
            return false;
        }

        // First the cut in front of the child, then the cut behind it: the identifiers grow in
        // the order of the content, and each new child of the element goes behind the last one
        for (const bool isCutInFront : { true, false })
        {
            const PDFContentNodePointer sequence = container->editor->getNode(*sequenceId);
            const std::vector<PDFContentNodePointer>& children = sequence->getChildren();

            PDFInteger index = -1;
            bool isContentInFront = false;
            bool isContentBehind = false;
            for (size_t i = 0; i < children.size(); ++i)
            {
                if (children[i]->getId() == id)
                {
                    index = PDFInteger(i);
                }
                else if (Helper::hasContent(*children[i]))
                {
                    (index < 0 ? isContentInFront : isContentBehind) = true;
                }
            }

            if (index < 0 || !sequence->getData().mcid)
            {
                *errorText = PDFTranslationContext::tr("The object is not in the tagged content it should be taken from.");
                return false;
            }

            if (!(isCutInFront ? isContentInFront : isContentBehind))
            {
                continue;
            }

            const PDFContentStructureChange change = structure->getChangeForReorder(structuralParentKey, *sequence->getData().mcid, false, work.getLargestMarkedContentId(container));
            if (!work.addStructureChange(container, change, errorText))
            {
                return false;
            }

            if (change.action != PDFContentStructureChange::Action::SplitSequence)
            {
                *errorText = PDFTranslationContext::tr("The structure of the document does not allow the change.");
                return false;
            }

            PDFContentNodeId pieceId;
            if (!splitSequence(work, container, *sequenceId, isCutInFront ? index : index + 1, change.newMcid, &pieceId, errorText))
            {
                return false;
            }

            if (isCutInFront)
            {
                // The child is the first content of the new piece now
                *sequenceId = pieceId;
            }
        }

        return true;
    }

    /// An order command: the layer and the tag stay, and the reading order of the tagged content
    /// with them. The node moves with the groups and the marked contents around it, whose only
    /// content it is (a sequence of the structure is split first, so that the node is its only
    /// content: isolateInSequence); of the other marked contents it gets a new wrap at its new
    /// place.
    /// \param isLast The node is the last one of the command: no node is placed in front of the
    ///        anchor after it
    /// \param anchor The place: the nodes go in front of it. A node, which stands at the place as
    ///        the first content of the anchor, gives the place behind itself to the nodes after it.
    bool moveWithWraps(Work& work, Work::Container* container, PDFContentNodeId id, bool isLast, PDFContentNodeId* anchor, QString* errorText)
    {
        const PDFInteger structuralParentKey = container->source->getStructuralParentKey();

        // The node is painted at the place already: nothing moves. As the first content of the
        // anchor only, when it is the last node: a later node would go in front of the anchor,
        // so in front of this one, against the order of the command.
        if (isAtPlace(container, id, m_parent.node, *anchor) || isPaintedAtPlace(container, id, m_parent.node, *anchor, isLast))
        {
            return true;
        }

        // The tree being changed is read through the editor here (parents, kinds, counts): the
        // node of the parent of many children is never made for one of them

        // The new parent and the nodes above it, as the tree is now
        std::set<PDFContentNodeId> parentChain;
        {
            int depth = 0;
            PDFContentNodeId current = m_parent.node;
            while (container->editor->getNodeData(current) && depth++ <= PDFContentContainer::MAXIMUM_DEPTH + 2)
            {
                parentChain.insert(current);
                current = container->editor->getParentId(current);
            }
        }

        // The groups and the marked contents above the node, which the node leaves (the nearest
        // first), up to the outermost marked content: a group above that one carries neither
        // a layer nor a tag. A group between the node and its marked content is on the way to
        // it (q BT ... ET Q inside of a tagged paragraph is the common case).
        std::vector<PDFContentNodeId> wraps;
        {
            int depth = 0;
            size_t count = 0;
            PDFContentNodeId wrapId = container->editor->getParentId(id);
            const PDFContentNode::Data* wrap = container->editor->getNodeData(wrapId);
            while (wrap && (wrap->kind == Kind::MarkedContent || wrap->kind == Kind::Group) &&
                   depth++ <= PDFContentContainer::MAXIMUM_DEPTH + 2 && !parentChain.count(wrapId))
            {
                wraps.push_back(wrapId);
                if (wrap->kind == Kind::MarkedContent)
                {
                    count = wraps.size();
                }
                wrapId = container->editor->getParentId(wrapId);
                wrap = container->editor->getNodeData(wrapId);
            }
            wraps.resize(count);
        }

        // A tag cannot be kept inside of another sequence of the structure: the sequences do
        // not nest (zadani_jadro.md 4.12)
        if (Work::getSequence(container, m_parent.node).isValid())
        {
            for (const PDFContentNodeId wrapId : wraps)
            {
                if (container->editor->getNodeData(wrapId)->mcid)
                {
                    *errorText = PDFTranslationContext::tr("The tag of the object cannot be kept there: tagged content cannot be placed into other tagged content.");
                    return false;
                }
            }
        }

        // The wraps, which go with the node: it is their only content. A sequence of the
        // structure with other content is split around the node first.
        PDFContentNodeId movedId = id;
        size_t wrapIndex = 0;
        for (; wrapIndex < wraps.size(); ++wrapIndex)
        {
            PDFContentNodeId wrapId = wraps[wrapIndex];
            const PDFContentNode::Data* wrap = container->editor->getNodeData(wrapId);
            const std::optional<PDFInteger> wrapMcid = wrap->mcid;
            const bool isSequence = wrap->kind == Kind::MarkedContent && wrapMcid.has_value();

            if (!isOnlyContent(container, wrapId, movedId))
            {
                if (!isSequence)
                {
                    break;
                }

                if (!isolateInSequence(work, container, &wrapId, movedId, errorText))
                {
                    return false;
                }
            }
            else if (isSequence)
            {
                // The sequence moves as it is: its identifier stays
                const PDFContentStructureModelPointer structure = work.getStructure();
                if (structure && !work.addStructureChange(container, structure->getChangeForReorder(structuralParentKey, *wrapMcid, true, work.getLargestMarkedContentId(container)), errorText))
                {
                    return false;
                }
            }

            // The wrap goes with the node - but never a wrap the content does not close
            if (container->source->isUnclosed(wrapId))
            {
                *errorText = PDFTranslationContext::tr("The object is the only content of an object, which is not closed in the content: it cannot be moved together with it.");
                return false;
            }

            movedId = wrapId;
        }

        if (isAtPlace(container, movedId, m_parent.node, *anchor))
        {
            return true;
        }

        if (movedId == *anchor)
        {
            // The node with its wraps is the anchor itself (it was the first content of the
            // anchor and is cut off from the rest of its sequence now): it stands at the place,
            // and the nodes after it go behind it
            const PDFInteger next = Work::getIndex(container, movedId) + 1;
            *anchor = next < Work::getChildCount(container, m_parent.node) ? container->editor->getChildId(m_parent.node, next) : PDFContentNodeId();
            return true;
        }

        // The data of the marked contents left behind, before anything moves: the node gets
        // a wrap like each of them at its new place. A group left behind needs none: the state
        // it gave the node is in the bake (R-2).
        std::vector<std::pair<PDFContentNode::Data, std::optional<PDFInteger>>> newWraps;
        for (; wrapIndex < wraps.size(); ++wrapIndex)
        {
            const PDFContentNode::Data* wrapData = container->editor->getNodeData(wraps[wrapIndex]);
            if (wrapData->kind != Kind::MarkedContent)
            {
                continue;
            }

            std::optional<PDFInteger> mcid;

            if (wrapData->mcid)
            {
                // The node stands deeper in the sequence, in a group or a marked content with
                // other content, so the sequence cannot be split around it. The node gets a new
                // sequence of the same element, whose child stands behind the child of the old
                // one: that is the reading order only when nothing of the old sequence is read
                // behind the node.
                const PDFContentNodePointer wrap = container->editor->getNode(wraps[wrapIndex]);
                if (hasContentBehind(wrap, movedId))
                {
                    *errorText = PDFTranslationContext::tr("The object stands in a group inside of tagged content, in front of other content of the tag: its order cannot be changed without changing the reading order yet.");
                    return false;
                }

                const PDFContentStructureModelPointer structure = work.getStructure();
                if (!structure)
                {
                    *errorText = PDFTranslationContext::tr("The structure of the document cannot be read, so the tag of the object cannot be kept.");
                    return false;
                }

                const PDFContentStructureChange change = structure->getChangeForReorder(structuralParentKey, *wrapData->mcid, false, work.getLargestMarkedContentId(container));
                if (!work.addStructureChange(container, change, errorText))
                {
                    return false;
                }

                if (change.action != PDFContentStructureChange::Action::SplitSequence)
                {
                    *errorText = PDFTranslationContext::tr("The structure of the document does not allow the change.");
                    return false;
                }
                mcid = change.newMcid;
            }

            newWraps.emplace_back(*wrapData, mcid);
        }

        PDFContentNodeId topId;
        if (!moveAndPlace(work, container, movedId, m_parent.node, *anchor, &topId, errorText))
        {
            return false;
        }

        for (const auto& item : newWraps)
        {
            const PDFInteger index = Work::getIndex(container, topId);
            const State state = Work::getStateAt(container, m_parent.node, index);

            PDFContentNodeId wrapId;
            if (!work.insertMarkedContentLike(container, item.first, item.second, m_parent.node, index, state, &wrapId, errorText) ||
                !work.moveNode(container, topId, wrapId, -1, errorText))
            {
                return false;
            }

            work.refreshLeaks(container, wrapId);
            topId = wrapId;
        }

        return true;
    }

    std::vector<PDFContentNodeReference> m_nodes;
    PDFContentNodeReference m_parent;
    PDFInteger m_index = -1;
    ReorderMode m_mode = ReorderMode::FollowParent;
};

/// Copies subtrees, also to another container (R-2, R-4, R-5; the copy has no structure tags)
class PDFContentCopyCommand : public PDFContentPlaceCommand
{
public:
    explicit PDFContentCopyCommand(std::vector<PDFContentNodeReference> nodes, PDFContentNodeReference parent, PDFInteger index, const QTransform& matrix) :
        m_nodes(std::move(nodes)),
        m_parent(parent),
        m_index(index),
        m_matrix(matrix)
    {
        setPlace(nullptr, m_parent.container);
    }

    virtual QString getText() const override { return PDFContentCommandFactory::tr("Copy %Ln Object(s)", nullptr, int(m_nodes.size())); }
    virtual QByteArray getType() const override { return PDFContentCommandFactory::TYPE_COPY; }

    virtual QJsonObject toJson() const override
    {
        using Json = PDFContentCommandJson;

        QJsonObject object;
        object.insert(QLatin1String(PDFContentJson::KEY_TYPE), QString::fromLatin1(PDFContentCommandFactory::TYPE_COPY));
        object.insert(QLatin1String(Json::KEY_IDS), writeIds(m_nodes));
        object.insert(QLatin1String(Json::KEY_PARENT), writeId(m_parent));
        object.insert(QLatin1String(Json::KEY_INDEX), qint64(m_index));
        object.insert(QLatin1String(Json::KEY_MATRIX), PDFContentJson::writeMatrix(m_matrix));
        return object;
    }

    static PDFContentCommandPointer fromJson(const QJsonObject& object, const PDFContentEditSession* session, const QString& path, std::vector<QString>* errors)
    {
        using Json = PDFContentCommandJson;

        std::vector<PDFContentNodeReference> nodes;
        PDFContentNodeReference parent;
        PDFInteger index = -1;
        QTransform matrix;

        bool result = Json::checkKeys(object, { PDFContentJson::KEY_ID, Json::KEY_IDS, Json::KEY_PARENT, Json::KEY_INDEX, Json::KEY_MATRIX }, path, errors);
        result = Json::readNodes(object, session, path, errors, &nodes) && result;
        result = Json::readTarget(object, session, path, errors, &parent, &index) && result;
        result = Json::readMatrix(object, false, path, errors, &matrix) && result;
        return result ? PDFContentCommandFactory::createCopy(std::move(nodes), parent, index, matrix) : nullptr;
    }

protected:
    virtual bool compute(Work& work, QString* errorText) override
    {
        if (!checkNodes(m_nodes, PDFTranslationContext::tr("There is no object to copy."), errorText))
        {
            return false;
        }

        if (!Helper::isRegularMatrix(m_matrix))
        {
            *errorText = PDFTranslationContext::tr("The transformation is not a regular matrix of finite numbers.");
            return false;
        }

        Work::Container* target = nullptr;
        const PDFContentNodePointer parent = work.getNode(m_parent, &target, errorText);
        if (!parent || !checkParent(target, parent, errorText))
        {
            return false;
        }

        // The page of the command is the page of the new place; the source needs its place too
        // (the identifiers of the nodes of a form are written with the path of its uses)
        setPlace(work.getSession(), m_parent.container);
        setPlace(work.getSession(), m_nodes.front().container);

        // The source is read as it was before the command: the copies never copy each other
        const PDFContentContainerPointer source = work.getSession()->getContainer(m_nodes.front().container);
        if (!source)
        {
            *errorText = PDFTranslationContext::tr("The content of the object is not loaded.");
            return false;
        }

        const bool isSameContainer = source->getKey() == target->key;
        const bool isSourceForm = source->getKey().kind != PDFContentContainerKey::Kind::Page;

        std::vector<PDFContentNodePointer> nodes;
        for (const PDFContentNodeReference& reference : m_nodes)
        {
            if (reference.container != source->getKey())
            {
                *errorText = PDFTranslationContext::tr("The objects to copy are not of one content.");
                return false;
            }

            const PDFContentNodePointer node = source->getNode(reference.node);
            if (!node)
            {
                *errorText = PDFTranslationContext::tr("The object does not exist.");
                return false;
            }

            if (!Helper::isObjectKind(node->getKind()) || isInSourceText(*source, *node))
            {
                *errorText = PDFTranslationContext::tr("An object of this kind cannot be copied.");
                return false;
            }

            // A form or the cell of a pattern never paints itself, also not through other forms
            // and patterns
            if (Helper::isPaintingContainer(work.getSession(), *source, node, target->key))
            {
                *errorText = target->key.kind == PDFContentContainerKey::Kind::Form ? PDFTranslationContext::tr("The copy cannot be placed into this form: the form would draw itself.")
                                                                                    : PDFTranslationContext::tr("The copy cannot be placed into this pattern: the pattern would draw itself.");
                return false;
            }

            nodes.push_back(node);
        }

        PDFContentNodeId anchor;
        if (!getAnchor(target, m_parent.node, m_index, { }, &anchor, errorText) ||
            !checkPlace(target, m_parent.node, anchor, errorText))
        {
            return false;
        }

        // A copy has no tag (zadani_jadro.md 4.12: "the copy is without an MCID"). A place inside
        // of a sequence of the structure is taken out of it: the copy stands between two pieces
        // of the sequence, or beside it (the parent of the copies is the parent of the sequence
        // then, not the node the command names).
        PDFContentNodeId placeParentId = m_parent.node;
        std::vector<PDFContentNodePointer> placeLayers;
        if (!leaveTaggedContent(work, target, &placeParentId, &anchor, &placeLayers, errorText))
        {
            return false;
        }

        // The layers of the new place: the marked contents of optional content around it
        std::set<PDFContentNodeId> targetWraps;
        std::set<PDFObjectReference> targetLayers;
        {
            int depth = 0;
            PDFContentNodePointer wrap = target->editor->getNode(placeParentId);
            while (wrap && depth++ <= PDFContentContainer::MAXIMUM_DEPTH + 2)
            {
                if (isLayerWrap(*wrap))
                {
                    targetWraps.insert(wrap->getId());
                    if (wrap->getData().optionalContent.isValid())
                    {
                        targetLayers.insert(wrap->getData().optionalContent);
                    }
                }
                wrap = target->editor->getNode(wrap->getParentId());
            }
        }

        // The layers the place has left with the tagged content count as layers of the place
        for (const PDFContentNodePointer& layer : placeLayers)
        {
            targetWraps.insert(layer->getId());
            if (layer->getData().optionalContent.isValid())
            {
                targetLayers.insert(layer->getData().optionalContent);
            }
        }

        size_t count = 0;
        for (const PDFContentNodePointer& node : nodes)
        {
            // The copy keeps its layer (zadani_jadro.md 4.12: inside of the same document, and
            // a session has one document): the marked contents of optional content around the
            // source, the nearest first, without those the new place is inside of already
            std::vector<PDFContentNodePointer> layers;
            {
                int depth = 0;
                PDFContentNodePointer wrap = source->getParent(node->getId());
                while (wrap && depth++ <= PDFContentContainer::MAXIMUM_DEPTH + 2)
                {
                    const bool isAroundPlace = (isSameContainer && targetWraps.count(wrap->getId())) ||
                                               (wrap->getData().optionalContent.isValid() && targetLayers.count(wrap->getData().optionalContent));
                    if (isLayerWrap(*wrap) && !isAroundPlace)
                    {
                        layers.push_back(wrap);
                    }
                    wrap = source->getParent(wrap->getId());
                }
            }

            // The objects to place: the node, or its content, when it is a sequence of the
            // structure (the copy has no tag; the state runs of the lost wrap are in the bake)
            std::vector<PDFContentNodePointer> tops;
            if (node->getKind() == Kind::MarkedContent && node->getData().mcid)
            {
                for (const PDFContentNodePointer& child : node->getChildren())
                {
                    if (child->getKind() != Kind::StateRun)
                    {
                        tops.push_back(child);
                    }
                }
            }
            else
            {
                tops.push_back(node);
            }

            for (const PDFContentNodePointer& top : tops)
            {
                if (!Helper::isObjectKind(top->getKind()))
                {
                    *errorText = PDFTranslationContext::tr("The object has a part, which cannot be read, so it cannot be copied.");
                    return false;
                }

                const PDFInteger index = getAnchorIndex(target, placeParentId, anchor);
                if (index < 0)
                {
                    *errorText = PDFTranslationContext::tr("The place of the object does not exist any more.");
                    return false;
                }

                const State targetState = Work::getStateAt(target, placeParentId, index);

                std::vector<PDFContentNodeId> created;
                if (!copyNode(work, *source, target, top, placeParentId, index, isSameContainer, 0, &created, errorText))
                {
                    return false;
                }

                // A sequence inside gives its content instead of itself: every piece is placed
                for (const PDFContentNodeId createdId : created)
                {
                    // The copy is an object of the command, whatever was added for its place
                    work.setAddedNodeAsObject(target, createdId);

                    const PDFContentNodePointer copy = target->editor->getNode(createdId);
                    const State sourceState{ copy->getState(), copy->getSymbolicState() };

                    PDFContentNodeId topId;
                    if (!work.placeNode(target, createdId, sourceState, targetState, isSourceForm, isSameContainer, source->getOperations(), &topId, errorText))
                    {
                        return false;
                    }

                    // The move of the copy in the space of the target container. Below its top
                    // the copy has the states of its source, which name the operations of the
                    // source container.
                    if (!m_matrix.isIdentity() &&
                        !work.transformNode(target, createdId, m_matrix, false, 0, &count, errorText, isSameContainer ? nullptr : &source->getOperations()))
                    {
                        return false;
                    }

                    for (const PDFContentNodePointer& layer : layers)
                    {
                        if (!wrapInLayer(work, *source, target, *layer, placeParentId, isSameContainer, &topId, errorText))
                        {
                            return false;
                        }
                    }

                    // The layers of the place inside of the tagged content, which the copy has
                    // left with the tag: it is put into them again (the nearest first)
                    for (const PDFContentNodePointer& layer : placeLayers)
                    {
                        if (!wrapInLayer(work, *target->source, target, *layer, placeParentId, true, &topId, errorText))
                        {
                            return false;
                        }
                    }
                }
            }
        }

        work.refreshLeaks(target, placeParentId);
        return true;
    }

private:
    /// Takes a place out of tagged content (owner's decision of 2026-11-26; zadani_jadro.md
    /// 4.12, the rule of the sequences): as long as the place is inside of a sequence of the
    /// structure, it leaves the marked content it is in -
    ///  - with content of the marked content on both sides of the place the marked content is
    ///    split around it: the part in front keeps the wrap (and the identifier of a sequence),
    ///    the rest gets a wrap of its own behind it - a sequence with a new identifier of the
    ///    same element, whose child stands right behind the child of the first part (/K keeps
    ///    the reading order); the place is between the two;
    ///  - with no content in front of the place it is the place in front of the marked content,
    ///    with none behind it the place behind the marked content: nothing is split.
    /// Refused with a text: a place in a group inside of tagged content (q ... Q cannot be split
    /// without setting its state again - stage E5); a sequence the structure model cannot split
    /// (irregular content, a model, which is not complete: the text of the model); a marked
    /// content, which ends with content, which cannot be read (its end cannot be written).
    /// \param parentId The parent of the place; on return the parent outside of tagged content
    /// \param anchor The child the copy goes in front of (not valid: at the end); on return of
    ///        the new parent
    /// \param layers The marked contents of optional content the place has left on its way
    ///        (the nearest first; nodes of the version before the command)
    static bool leaveTaggedContent(Work& work,
                                   Work::Container* target,
                                   PDFContentNodeId* parentId,
                                   PDFContentNodeId* anchor,
                                   std::vector<PDFContentNodePointer>* layers,
                                   QString* errorText)
    {
        int depth = 0;
        while (Work::getSequence(target, *parentId).isValid())
        {
            const PDFContentNodeId wrapId = *parentId;
            const PDFContentNode::Data* wrap = target->editor->getNodeData(wrapId);
            if (!wrap || depth++ > PDFContentContainer::MAXIMUM_DEPTH + 2)
            {
                *errorText = PDFTranslationContext::tr("The place of the object does not exist.");
                return false;
            }

            if (wrap->kind != Kind::MarkedContent)
            {
                *errorText = PDFTranslationContext::tr("The place is in a group inside of tagged content: the tag cannot be split around the copy there yet. Place the copy beside the group.");
                return false;
            }

            const std::optional<PDFInteger> mcid = wrap->mcid;
            const PDFContentNodeId outerId = target->editor->getParentId(wrapId);
            const PDFInteger index = getAnchorIndex(target, wrapId, *anchor);
            const PDFInteger count = Work::getChildCount(target, wrapId);
            if (index < 0 || !outerId.isValid())
            {
                *errorText = PDFTranslationContext::tr("The place of the object does not exist.");
                return false;
            }

            // Content of the marked content in front of the place and behind it (the walk ends
            // at the first one of each side)
            bool isContentInFront = false;
            for (PDFInteger i = 0; i < index && !isContentInFront; ++i)
            {
                isContentInFront = Work::hasContent(target, target->editor->getChildId(wrapId, i));
            }

            bool isContentBehind = false;
            for (PDFInteger i = index; i < count && !isContentBehind; ++i)
            {
                isContentBehind = Work::hasContent(target, target->editor->getChildId(wrapId, i));
            }

            // A layer the place leaves here is given to the copy again (by its node before the
            // command: the wrap of the first part keeps its identifier)
            const PDFContentNodePointer sourceWrap = target->source->getNode(wrapId);
            if (sourceWrap && isLayerWrap(*sourceWrap))
            {
                layers->push_back(sourceWrap);
            }

            if (!isContentInFront)
            {
                *anchor = wrapId;
            }
            else if (!isContentBehind)
            {
                const PDFInteger next = Work::getIndex(target, wrapId) + 1;
                *anchor = next < Work::getChildCount(target, outerId) ? target->editor->getChildId(outerId, next) : PDFContentNodeId();
            }
            else
            {
                // The end of the marked content must be writable behind its first part and
                // behind the rest
                if (Work::isPinned(target, wrapId))
                {
                    *errorText = PDFTranslationContext::tr("The tagged content ends with content, which cannot be read, so it cannot be split around the copy.");
                    return false;
                }

                std::optional<PDFInteger> newMcid;
                if (mcid)
                {
                    const PDFContentStructureModelPointer structure = work.getStructure();
                    if (!structure)
                    {
                        *errorText = PDFTranslationContext::tr("The structure of the document cannot be read, so the tagged content cannot be split around the copy.");
                        return false;
                    }

                    const PDFContentStructureChange change = structure->getChangeForMoveOut(target->source->getStructuralParentKey(), *mcid, false, true, work.getLargestMarkedContentId(target));
                    if (!work.addStructureChange(target, change, errorText))
                    {
                        return false;
                    }

                    if (change.action != PDFContentStructureChange::Action::SplitSequence)
                    {
                        *errorText = PDFTranslationContext::tr("The structure of the document does not allow the change.");
                        return false;
                    }
                    newMcid = change.newMcid;
                }

                PDFContentNodeId pieceId;
                if (!splitSequence(work, target, wrapId, index, newMcid, &pieceId, errorText))
                {
                    return false;
                }
                *anchor = pieceId;
            }

            *parentId = outerId;
        }

        return true;
    }

    /// A marked content, which puts its content into a layer: /OC with the name of an optional
    /// content group or membership dictionary of /Properties (ISO 32000-2, 8.11.3.2)
    static bool isLayerWrap(const PDFContentNode& node)
    {
        return node.getKind() == Kind::MarkedContent && node.getData().tag == "OC";
    }

    /// Puts a copy, which stands at its place, into a copy of a marked content of optional
    /// content of its source: BDC with the same property list (R-4 names it in the target
    /// container) and a new EMC
    /// \param topId The node standing at the place: the copy, then the new wrap
    static bool wrapInLayer(Work& work,
                            const PDFContentContainer& source,
                            Work::Container* target,
                            const PDFContentNode& layer,
                            PDFContentNodeId parentId,
                            bool isSameContainer,
                            PDFContentNodeId* topId,
                            QString* errorText)
    {
        const PDFInteger index = Work::getIndex(target, *topId);
        if (index < 0)
        {
            *errorText = PDFTranslationContext::tr("The place of the object does not exist any more.");
            return false;
        }

        const State state = Work::getStateAt(target, parentId, index);

        PDFContentNode::Data data;
        data.kind = Kind::MarkedContent;
        data.id = work.getSession()->allocateNodeId();
        data.state = state.resolved;
        data.symbolicState = state.symbolic;
        data.tag = layer.getData().tag;
        data.properties = layer.getData().properties;
        data.propertiesName = layer.getData().propertiesName;
        data.optionalContent = layer.getData().optionalContent;

        if (!copyOperations(work, source, target, layer.getPrefix(), isSameContainer, &data.prefix, errorText))
        {
            return false;
        }

        // The name of the property list in the target container
        if (!data.prefix.isEmpty())
        {
            for (const PDFContentResourceName& resourceName : target->editor->getOperations().getOperation(data.prefix.first).getResourceNames())
            {
                if (resourceName.category == PDFContentResourceCategory::Properties)
                {
                    data.propertiesName = resourceName.name;
                }
            }
        }

        data.suffix = PDFContentOperationSpan{ work.appendOperation(target, Helper::createOperation(PDFContentOperator::MarkedContentEnd, "EMC")), 1 };

        const PDFContentNodeId wrapId = data.id;
        if (!work.insertNode(target, std::move(data), parentId, index, errorText) ||
            !work.moveNode(target, *topId, wrapId, -1, errorText))
        {
            return false;
        }

        work.refreshLeaks(target, wrapId);
        *topId = wrapId;
        return true;
    }

    static bool isInSourceText(const PDFContentContainer& source, const PDFContentNode& node)
    {
        int depth = 0;
        PDFContentNodePointer parent = source.getParent(node.getId());
        while (parent && depth++ <= PDFContentContainer::MAXIMUM_DEPTH + 2)
        {
            if (parent->getKind() == Kind::TextObject)
            {
                return true;
            }
            parent = source.getParent(parent->getId());
        }
        return false;
    }

    /// Copies the operations of a span into the pool of the target; false for an operation,
    /// which cannot be copied
    static bool copyOperations(Work& work,
                               const PDFContentContainer& source,
                               Work::Container* target,
                               const PDFContentOperationSpan& span,
                               bool isSameContainer,
                               PDFContentOperationSpan* result,
                               QString* errorText)
    {
        *result = PDFContentOperationSpan();

        for (size_t i = 0; i < span.count; ++i)
        {
            if (span.first + i >= source.getOperations().getCount())
            {
                *errorText = PDFTranslationContext::tr("The object refers to operations, which the content does not have.");
                return false;
            }

            const PDFContentOperation& operation = source.getOperations().getOperation(span.first + i);
            PDFContentOperation copy = PDFContentOperation::createCopy(operation);

            if (!isSameContainer && !operation.getResourceNames().empty())
            {
                // R-4: the resources of the copy are named in the target container; the object,
                // which is a resource there already, keeps its name
                std::map<QByteArray, QByteArray> renamed;
                std::vector<PDFContentResourceName> resourceNames;
                bool isRenamed = false;

                for (const PDFContentResourceName& resourceName : operation.getResourceNames())
                {
                    const PDFObject object = source.getResources().getObject(resourceName.category, resourceName.name);
                    if (object.isNull())
                    {
                        *errorText = PDFTranslationContext::tr("The object uses a resource, which its content does not define, so it cannot be copied to another content.");
                        return false;
                    }

                    const bool isForm = resourceName.category == PDFContentResourceCategory::XObject && Helper::isFormXObject(work.getSession(), object);
                    const QByteArray newName = target->editor->getResources().addObject(resourceName.category, Helper::getResourcePrefix(resourceName.category, isForm), object);
                    if (newName.isEmpty())
                    {
                        *errorText = PDFTranslationContext::tr("A resource of the object cannot be added to the other content.");
                        return false;
                    }

                    target->isTouched = true;
                    renamed[resourceName.name] = newName;
                    resourceNames.push_back(PDFContentResourceName{ resourceName.category, newName });
                    isRenamed = isRenamed || newName != resourceName.name;
                }

                if (isRenamed)
                {
                    // The operators, which name a resource, have numbers and names as their
                    // operands only (Do, Tf, gs, cs, scn, sh, BDC with a named property list):
                    // the last name is the resource. An inline image is not written again.
                    QByteArray bytes;
                    bool isWritable = operation.getOperator() != PDFContentOperator::InlineImage && !operation.getOperands().empty();
                    qsizetype lastName = -1;
                    for (size_t j = 0; j < operation.getOperands().size(); ++j)
                    {
                        if (operation.getOperands()[j].type == PDFLexicalAnalyzer::TokenType::Name)
                        {
                            lastName = qsizetype(j);
                        }
                    }

                    for (size_t j = 0; j < operation.getOperands().size() && isWritable; ++j)
                    {
                        const PDFLexicalAnalyzer::Token& token = operation.getOperands()[j];
                        switch (token.type)
                        {
                            case PDFLexicalAnalyzer::TokenType::Integer:
                            case PDFLexicalAnalyzer::TokenType::Real:
                                bytes.append(PDFContentStreamText::formatNumber(token.data.toDouble()));
                                break;

                            case PDFLexicalAnalyzer::TokenType::Name:
                            {
                                const QByteArray name = token.data.toByteArray();
                                auto it = renamed.find(name);
                                bytes.append(PDFContentStreamText::formatName(qsizetype(j) == lastName && it != renamed.end() ? it->second : name));
                                break;
                            }

                            default:
                                isWritable = false;
                                break;
                        }
                        bytes.append(' ');
                    }

                    // The text of the operator: the last token of the bytes of the operation
                    const QByteArray raw = operation.getRawBytes().trimmed();
                    qsizetype operatorStart = raw.size();
                    while (operatorStart > 0 && !QByteArray(" \t\r\n\f/()<>[]").contains(raw[operatorStart - 1]))
                    {
                        --operatorStart;
                    }
                    const QByteArray operatorText = raw.mid(operatorStart);

                    if (!isWritable || lastName < 0 || operatorText.isEmpty())
                    {
                        *errorText = PDFTranslationContext::tr("The object uses a named resource in a way, which cannot be copied to another content yet.");
                        return false;
                    }

                    bytes.append(operatorText);
                    copy = PDFContentOperation::createGenerated(operation.getOperator(), bytes, Helper::getOperands(bytes), std::move(resourceNames));
                }
            }

            const size_t index = work.appendOperation(target, std::move(copy));

            // A copy is suppressed on its own: what the override of the copied node replaces
            // stays replaced
            if (operation.getFlags().testFlag(PDFContentOperation::Suppressed))
            {
                PDFContentOperation::Flags flags = target->editor->getOperations().getOperation(index).getFlags();
                flags.setFlag(PDFContentOperation::Suppressed, true);
                target->editor->getOperations().setFlags(index, flags);
            }

            if (i == 0)
            {
                result->first = index;
            }
            result->count = i + 1;
        }

        return true;
    }

    /// Copies a subtree under a parent of the target. A sequence of the structure inside gives
    /// its children instead of itself (the copy has no marked content identifier).
    /// \param index The index of the copy among the children, -1 = at the end
    /// \param created The nodes created right under the parent
    static bool copyNode(Work& work,
                         const PDFContentContainer& source,
                         Work::Container* target,
                         const PDFContentNodePointer& node,
                         PDFContentNodeId parentId,
                         PDFInteger index,
                         bool isSameContainer,
                         int depth,
                         std::vector<PDFContentNodeId>* created,
                         QString* errorText)
    {
        if (depth > PDFContentContainer::MAXIMUM_DEPTH + 2)
        {
            *errorText = PDFTranslationContext::tr("The object cannot be placed there: the content would be nested too deep.");
            return false;
        }

        if (node->getKind() == Kind::Opaque || node->getKind() == Kind::Root)
        {
            *errorText = PDFTranslationContext::tr("The object has a part, which cannot be read, so it cannot be copied.");
            return false;
        }

        const bool isOpen = source.isUnclosed(node->getId()) || (!PDFContentNode::isLeafKind(node->getKind()) && node->getSuffix().isEmpty());
        if (isOpen)
        {
            *errorText = PDFTranslationContext::tr("The object is not closed in the content, so it cannot be copied.");
            return false;
        }

        if (node->getKind() == Kind::MarkedContent && node->getData().mcid && depth > 0)
        {
            for (const PDFContentNodePointer& child : node->getChildren())
            {
                std::vector<PDFContentNodeId> ignored;
                if (!copyNode(work, source, target, child, parentId, index, isSameContainer, depth + 1, &ignored, errorText))
                {
                    return false;
                }
                if (index >= 0)
                {
                    index += PDFInteger(ignored.size());
                }
                created->insert(created->end(), ignored.cbegin(), ignored.cend());
            }
            return true;
        }

        PDFContentNode::Data data = node->getData();
        data.id = work.getSession()->allocateNodeId();

        if (!copyOperations(work, source, target, node->getPrefix(), isSameContainer, &data.prefix, errorText) ||
            !copyOperations(work, source, target, node->getSuffix(), isSameContainer, &data.suffix, errorText))
        {
            return false;
        }

        if (!isSameContainer)
        {
            // R-4 may have given the resources of the copy other names in the target: the data
            // of the node say the names its copied operations use (the XObject of Do, the
            // shading of sh, the named property list of BDC / DP), not the names of the source
            const PDFContentOperationList& targetOperations = target->editor->getOperations();
            for (size_t i = 0; i < data.prefix.count && data.prefix.first + i < targetOperations.getCount(); ++i)
            {
                for (const PDFContentResourceName& resourceName : targetOperations.getOperation(data.prefix.first + i).getResourceNames())
                {
                    switch (data.kind)
                    {
                        case Kind::Image:
                        case Kind::FormUse:
                            if (resourceName.category == PDFContentResourceCategory::XObject && !data.resourceName.isEmpty())
                            {
                                data.resourceName = resourceName.name;
                            }
                            break;

                        case Kind::Shading:
                            if (resourceName.category == PDFContentResourceCategory::Shading && !data.resourceName.isEmpty())
                            {
                                data.resourceName = resourceName.name;
                            }
                            break;

                        case Kind::MarkedContent:
                        case Kind::MarkedPoint:
                            if (resourceName.category == PDFContentResourceCategory::Properties && !data.propertiesName.isEmpty())
                            {
                                data.propertiesName = resourceName.name;
                            }
                            break;

                        default:
                            break;
                    }
                }
            }
        }

        const PDFContentNodeId id = data.id;
        if (!work.insertNode(target, std::move(data), parentId, index, errorText))
        {
            return false;
        }
        created->push_back(id);

        for (const PDFContentNodePointer& child : node->getChildren())
        {
            std::vector<PDFContentNodeId> ignored;
            if (!copyNode(work, source, target, child, id, -1, isSameContainer, depth + 1, &ignored, errorText))
            {
                return false;
            }
        }

        return true;
    }

    std::vector<PDFContentNodeReference> m_nodes;
    PDFContentNodeReference m_parent;
    PDFInteger m_index = -1;
    QTransform m_matrix;
};

/// Wraps nodes of one parent into a new group q ... Q (the state runs between them stay in place)
class PDFContentGroupCommand : public PDFContentPlaceCommand
{
public:
    explicit PDFContentGroupCommand(std::vector<PDFContentNodeReference> nodes) :
        m_nodes(std::move(nodes))
    {
        if (!m_nodes.empty())
        {
            setPlace(nullptr, m_nodes.front().container);
        }
    }

    virtual QString getText() const override { return PDFContentCommandFactory::tr("Group"); }
    virtual QByteArray getType() const override { return PDFContentCommandFactory::TYPE_GROUP; }

    virtual QJsonObject toJson() const override
    {
        QJsonObject object;
        object.insert(QLatin1String(PDFContentJson::KEY_TYPE), QString::fromLatin1(PDFContentCommandFactory::TYPE_GROUP));
        object.insert(QLatin1String(PDFContentCommandJson::KEY_IDS), writeIds(m_nodes));
        return object;
    }

    static PDFContentCommandPointer fromJson(const QJsonObject& object, const PDFContentEditSession* session, const QString& path, std::vector<QString>* errors)
    {
        std::vector<PDFContentNodeReference> nodes;
        bool result = PDFContentCommandJson::checkKeys(object, { PDFContentJson::KEY_ID, PDFContentCommandJson::KEY_IDS }, path, errors);
        result = PDFContentCommandJson::readNodes(object, session, path, errors, &nodes) && result;
        return result ? PDFContentCommandFactory::createGroup(std::move(nodes)) : nullptr;
    }

protected:
    virtual bool compute(Work& work, QString* errorText) override
    {
        if (!checkNodes(m_nodes, PDFTranslationContext::tr("There is no object to group."), errorText))
        {
            return false;
        }

        Work::Container* container = nullptr;
        PDFContentNodeId parentId;

        // The objects in the order of the tree
        std::map<PDFInteger, PDFContentNodeId> ordered;
        for (const PDFContentNodeReference& reference : m_nodes)
        {
            if (reference.container != m_nodes.front().container)
            {
                *errorText = PDFTranslationContext::tr("The objects to group are not of one content.");
                return false;
            }

            const PDFContentNodePointer node = work.getNode(reference, &container, errorText);
            if (!node || !checkMovable(container, node, errorText))
            {
                return false;
            }

            if (parentId.isValid() && node->getParentId() != parentId)
            {
                *errorText = PDFTranslationContext::tr("Only objects of one parent can be grouped.");
                return false;
            }

            parentId = node->getParentId();
            ordered[Work::getIndex(container, reference.node)] = reference.node;
        }

        setPlace(work.getSession(), m_nodes.front().container);

        const PDFContentNodePointer parent = container->editor->getNode(parentId);
        if (!parent || !checkParent(container, parent, errorText))
        {
            return false;
        }

        // q ... Q ends a clipping path set inside. An object, which sets one (a clipping path,
        // a path with W, a text with a rendering mode 4 to 7, a marked content holding one),
        // clips everything behind it in the parent: in a group it would stop clipping the
        // content, which stays outside (R-1: a node with a clipping effect is never wrapped,
        // "the following siblings would lose the clip"). Refused, unless everything painted
        // behind it in the parent comes into the group too - as Ungroup refuses a group with
        // a clip.
        {
            std::set<PDFContentNodeId> grouped;
            for (const auto& item : ordered)
            {
                grouped.insert(item.second);
            }

            bool isClipSet = false;
            bool isClipLost = false;
            for (const PDFContentNodePointer& child : parent->getChildren())
            {
                if (grouped.count(child->getId()))
                {
                    isClipSet = isClipSet || child->hasClipEffect() || child->getLeak().isClipChanged;
                }
                else
                {
                    isClipLost = isClipLost || (isClipSet && Helper::hasContent(*child) && !isTail(*child));
                }
            }

            // A marked content does not end a clipping path (only Q does): the clip reaches
            // the content behind the marked contents the objects stand in
            int depth = 0;
            PDFContentNodePointer wrap = parent;
            while (isClipSet && !isClipLost && wrap && wrap->getKind() == Kind::MarkedContent && depth++ <= PDFContentContainer::MAXIMUM_DEPTH + 2)
            {
                const PDFContentNodePointer outer = container->editor->getNode(wrap->getParentId());
                bool isBehind = false;
                for (const PDFContentNodePointer& child : outer ? outer->getChildren() : std::vector<PDFContentNodePointer>())
                {
                    isClipLost = isClipLost || (isBehind && Helper::hasContent(*child) && !isTail(*child));
                    isBehind = isBehind || child->getId() == wrap->getId();
                }
                wrap = outer;
            }

            if (isClipLost)
            {
                *errorText = PDFTranslationContext::tr("An object to group sets a clipping path for the objects behind it: in a group it would not clip them any more. Group it together with them.");
                return false;
            }
        }

        const PDFContentNodePointer first = container->editor->getNode(ordered.cbegin()->second);
        const State groupState{ first->getState(), first->getSymbolicState() };

        QRectF boundingBox;
        for (const auto& item : ordered)
        {
            boundingBox = boundingBox.united(container->editor->getNode(item.second)->getBoundingBox());
        }

        // The group stands at the place of the first object; it is the first added node of the change
        PDFContentNodeId groupId;
        if (!work.insertGroup(container, parentId, ordered.cbegin()->first, groupState, boundingBox, &groupId, errorText))
        {
            return false;
        }

        for (const auto& item : ordered)
        {
            PDFContentNodeId topId;
            if (!moveAndPlace(work, container, item.second, groupId, PDFContentNodeId(), &topId, errorText))
            {
                return false;
            }
        }

        return true;
    }

private:
    /// The tail of a content: bytes, which are no operations (an opaque node without any)
    static bool isTail(const PDFContentNode& node)
    {
        return node.getKind() == Kind::Opaque && node.getPrefix().isEmpty() && node.getSuffix().isEmpty() && node.getChildren().empty();
    }

    std::vector<PDFContentNodeReference> m_nodes;
};

/// Removes a group and moves its children out by R-2 (refused for a group with a clip)
class PDFContentUngroupCommand : public PDFContentPlaceCommand
{
public:
    explicit PDFContentUngroupCommand(PDFContentNodeReference group) :
        m_group(group)
    {
        setPlace(nullptr, m_group.container);
    }

    virtual QString getText() const override { return PDFContentCommandFactory::tr("Ungroup"); }
    virtual QByteArray getType() const override { return PDFContentCommandFactory::TYPE_UNGROUP; }

    virtual QJsonObject toJson() const override
    {
        QJsonObject object;
        object.insert(QLatin1String(PDFContentJson::KEY_TYPE), QString::fromLatin1(PDFContentCommandFactory::TYPE_UNGROUP));
        object.insert(QLatin1String(PDFContentJson::KEY_ID), writeId(m_group));
        return object;
    }

    static PDFContentCommandPointer fromJson(const QJsonObject& object, const PDFContentEditSession* session, const QString& path, std::vector<QString>* errors)
    {
        PDFContentNodeReference group;
        bool result = PDFContentCommandJson::checkKeys(object, { PDFContentJson::KEY_ID }, path, errors);
        result = PDFContentCommandJson::readNode(object.value(QLatin1String(PDFContentJson::KEY_ID)), session, path, PDFContentJson::KEY_ID, errors, &group) && result;
        return result ? PDFContentCommandFactory::createUngroup(group) : nullptr;
    }

protected:
    virtual bool compute(Work& work, QString* errorText) override
    {
        Work::Container* container = nullptr;
        const PDFContentNodePointer group = work.getNode(m_group, &container, errorText);
        if (!group)
        {
            return false;
        }

        setPlace(work.getSession(), m_group.container);

        if (group->getKind() != Kind::Group)
        {
            *errorText = PDFTranslationContext::tr("The object is not a group.");
            return false;
        }

        if (Work::isPinned(container, m_group.node) || group->getSuffix().isEmpty())
        {
            *errorText = PDFTranslationContext::tr("The group is not closed in the content, so it cannot be removed.");
            return false;
        }

        std::vector<PDFContentNodeId> children;
        for (const PDFContentNodePointer& child : group->getChildren())
        {
            if (child->getKind() == Kind::Opaque)
            {
                *errorText = PDFTranslationContext::tr("The group has a part, which cannot be read, so it cannot be removed.");
                return false;
            }

            // The clip ends with the group: outside it would reach the rest of the parent
            if (child->hasClipEffect() || child->getLeak().isClipChanged)
            {
                *errorText = PDFTranslationContext::tr("The group sets a clipping path: release the clipping first.");
                return false;
            }

            if (child->getKind() != Kind::StateRun)
            {
                children.push_back(child->getId());
            }
        }

        // The children go in front of the group, each with the difference between the state
        // it saw inside and the state outside (R-2); the state runs die with the group
        const PDFContentNodeId parentId = group->getParentId();
        for (const PDFContentNodeId childId : children)
        {
            const PDFContentNodePointer child = container->editor->getNode(childId);
            const State source{ child->getState(), child->getSymbolicState() };

            const PDFInteger index = Work::getIndex(container, m_group.node);
            const State target = Work::getStateAt(container, parentId, index);

            PDFContentNodeId topId;
            if (!work.moveNode(container, childId, parentId, index, errorText) ||
                !work.placeNode(container, childId, source, target, container->isForm, true, container->editor->getOperations(), &topId, errorText))
            {
                return false;
            }
        }

        work.eraseNode(container, m_group.node);
        work.refreshLeaks(container, parentId);
        return true;
    }

private:
    PDFContentNodeReference m_group;
};

// ---------------------------------------------------------------------------------------------
//  Clear Page
// ---------------------------------------------------------------------------------------------

/// Removes every node of a page (every sequence of the page leaves the structure)
class PDFContentClearPageCommand : public PDFContentModelCommand
{
public:
    explicit PDFContentClearPageCommand(PDFContentContainerKey page) :
        m_page(page)
    {
        setPlace(nullptr, m_page);
    }

    virtual QString getText() const override { return PDFContentCommandFactory::tr("Clear Page"); }
    virtual QByteArray getType() const override { return PDFContentCommandFactory::TYPE_CLEAR_PAGE; }

    virtual QJsonObject toJson() const override
    {
        QJsonObject object;
        object.insert(QLatin1String(PDFContentJson::KEY_TYPE), QString::fromLatin1(PDFContentCommandFactory::TYPE_CLEAR_PAGE));
        object.insert(QLatin1String(PDFContentCommandJson::KEY_PAGE), qint64(m_page.pageIndex + 1));
        return object;
    }

    static PDFContentCommandPointer fromJson(const QJsonObject& object, const PDFContentEditSession* session, const QString& path, std::vector<QString>* errors)
    {
        using Json = PDFContentCommandJson;

        bool result = Json::checkKeys(object, { Json::KEY_PAGE }, path, errors);

        PDFInteger pageNumber = 0;
        PDFContentContainerKey key;
        if (Json::readInteger(object.value(QLatin1String(Json::KEY_PAGE)), 1, std::numeric_limits<int>::max(), &pageNumber) && session)
        {
            key = session->getPageKey(pageNumber - 1);
        }

        if (!key.isValid())
        {
            Json::addError(errors, path, Json::KEY_PAGE, PDFTranslationContext::tr("The number of a page of the document is expected."));
            result = false;
        }

        return result ? PDFContentCommandFactory::createClearPage(key) : nullptr;
    }

protected:
    virtual bool compute(Work& work, QString* errorText) override
    {
        if (!m_page.isValid() || m_page.kind != PDFContentContainerKey::Kind::Page)
        {
            *errorText = PDFTranslationContext::tr("Only the content of a page can be cleared.");
            return false;
        }

        Work::Container* container = work.getContainer(m_page, errorText);
        if (!container)
        {
            return false;
        }

        // A page without content is cleared already: the command changes nothing
        const PDFContentNodePointer root = container->editor->getNode(container->source->getRoot() ? container->source->getRoot()->getId() : PDFContentNodeId());
        if (!root)
        {
            *errorText = PDFTranslationContext::tr("The page has no content.");
            setRefusedAsUnchanged(true);
            return false;
        }

        size_t count = 0;
        for (const PDFContentNodePointer& child : root->getChildren())
        {
            // The tail of the content (bytes, which are no operations) has no operations to
            // remove: its node stays, the stream writer writes the bytes last
            if (child->getKind() == Kind::Opaque && child->getPrefix().isEmpty() && child->getSuffix().isEmpty() && child->getChildren().empty())
            {
                continue;
            }

            if (!work.removeSequences(container, child, errorText))
            {
                return false;
            }

            work.eraseNode(container, child->getId());
            ++count;
        }

        if (count == 0)
        {
            *errorText = PDFTranslationContext::tr("The page has no content.");
            setRefusedAsUnchanged(true);
            return false;
        }

        return true;
    }

private:
    PDFContentContainerKey m_page;
};

// ---------------------------------------------------------------------------------------------
//  Factory
// ---------------------------------------------------------------------------------------------

/// The registry of the JSON types. Ownership: one object for the process, a function-local
/// static of getInstance (so a static registration of a command file works in any order of the
/// initialization); the basic types are in it from its creation. Guarded by a mutex: the types
/// are registered before the factory is used, but nothing breaks, when a late one comes.
class PDFContentCommandRegistry
{
public:
    static PDFContentCommandRegistry& getInstance()
    {
        static PDFContentCommandRegistry instance;
        return instance;
    }

    bool registerType(const char* type, PDFContentCommandFactory::Creator creator)
    {
        if (!type || !*type || !creator)
        {
            return false;
        }

        QMutexLocker locker(&m_mutex);
        return m_creators.emplace(QByteArray(type), creator).second;
    }

    PDFContentCommandFactory::Creator getCreator(const QByteArray& type)
    {
        QMutexLocker locker(&m_mutex);
        auto it = m_creators.find(type);
        return it != m_creators.end() ? it->second : nullptr;
    }

    std::vector<QByteArray> getTypes()
    {
        QMutexLocker locker(&m_mutex);
        std::vector<QByteArray> result;
        for (const auto& item : m_creators)
        {
            result.push_back(item.first);
        }
        return result;
    }

private:
    PDFContentCommandRegistry()
    {
        using Factory = PDFContentCommandFactory;

        m_creators.emplace(QByteArray(Factory::TYPE_DELETE), &PDFContentDeleteCommand::fromJson);
        m_creators.emplace(QByteArray(Factory::TYPE_TRANSFORM), &PDFContentTransformCommand::fromJson);
        m_creators.emplace(QByteArray(Factory::TYPE_REORDER), &PDFContentReorderCommand::fromJson);
        m_creators.emplace(QByteArray(Factory::TYPE_COPY), &PDFContentCopyCommand::fromJson);
        m_creators.emplace(QByteArray(Factory::TYPE_SET_APPEARANCE), &PDFContentSetPropertiesCommand::appearanceFromJson);
        m_creators.emplace(QByteArray(Factory::TYPE_SET_TRANSPARENCY), &PDFContentSetPropertiesCommand::transparencyFromJson);
        m_creators.emplace(QByteArray(Factory::TYPE_GROUP), &PDFContentGroupCommand::fromJson);
        m_creators.emplace(QByteArray(Factory::TYPE_UNGROUP), &PDFContentUngroupCommand::fromJson);
        m_creators.emplace(QByteArray(Factory::TYPE_CLEAR_PAGE), &PDFContentClearPageCommand::fromJson);
    }

    QMutex m_mutex;
    std::map<QByteArray, PDFContentCommandFactory::Creator> m_creators;     ///< Sorted by the type
};

PDFContentCommandPointer PDFContentCommandFactory::createDelete(std::vector<PDFContentNodeReference> nodes)
{
    return std::make_shared<PDFContentDeleteCommand>(std::move(nodes));
}

PDFContentCommandPointer PDFContentCommandFactory::createTransform(std::vector<PDFContentNodeReference> nodes, const QTransform& matrix)
{
    return std::make_shared<PDFContentTransformCommand>(std::move(nodes), matrix);
}

PDFContentCommandPointer PDFContentCommandFactory::createReorder(std::vector<PDFContentNodeReference> nodes, PDFContentNodeReference parent, PDFInteger index, ReorderMode mode)
{
    return std::make_shared<PDFContentReorderCommand>(std::move(nodes), parent, index, mode);
}

PDFContentCommandPointer PDFContentCommandFactory::createCopy(std::vector<PDFContentNodeReference> nodes, PDFContentNodeReference parent, PDFInteger index, const QTransform& matrix)
{
    return std::make_shared<PDFContentCopyCommand>(std::move(nodes), parent, index, matrix);
}

PDFContentCommandPointer PDFContentCommandFactory::createSetAppearance(std::vector<PDFContentNodeReference> nodes, std::map<PDFContentStateProperty, PDFContentStateValue> properties)
{
    return std::make_shared<PDFContentSetPropertiesCommand>(std::move(nodes), std::move(properties), true);
}

PDFContentCommandPointer PDFContentCommandFactory::createSetAppearance(std::vector<PDFContentNodeReference> nodes, std::map<PDFContentStateProperty, PDFContentStateValue> properties, PDFContentPaintSwitches switches)
{
    return std::make_shared<PDFContentSetPropertiesCommand>(std::move(nodes), std::move(properties), true, switches);
}

PDFContentCommandPointer PDFContentCommandFactory::createSetTransparency(std::vector<PDFContentNodeReference> nodes, std::optional<PDFReal> alphaStroking, std::optional<PDFReal> alphaFilling, std::optional<QByteArray> blendMode)
{
    std::map<PDFContentStateProperty, PDFContentStateValue> properties;

    if (alphaStroking)
    {
        PDFContentStateValue value;
        value.kind = PDFContentStateValue::Kind::Real;
        value.real = *alphaStroking;
        properties[PDFContentStateProperty::AlphaStroking] = std::move(value);
    }

    if (alphaFilling)
    {
        PDFContentStateValue value;
        value.kind = PDFContentStateValue::Kind::Real;
        value.real = *alphaFilling;
        properties[PDFContentStateProperty::AlphaFilling] = std::move(value);
    }

    if (blendMode)
    {
        PDFContentStateValue value;
        value.kind = PDFContentStateValue::Kind::Name;
        value.name = *blendMode;
        properties[PDFContentStateProperty::BlendMode] = std::move(value);
    }

    return std::make_shared<PDFContentSetPropertiesCommand>(std::move(nodes), std::move(properties), false);
}

PDFContentCommandPointer PDFContentCommandFactory::createGroup(std::vector<PDFContentNodeReference> nodes)
{
    return std::make_shared<PDFContentGroupCommand>(std::move(nodes));
}

PDFContentCommandPointer PDFContentCommandFactory::createUngroup(PDFContentNodeReference group)
{
    return std::make_shared<PDFContentUngroupCommand>(group);
}

PDFContentCommandPointer PDFContentCommandFactory::createClearPage(PDFContentContainerKey page)
{
    return std::make_shared<PDFContentClearPageCommand>(page);
}

PDFContentCommandPointer PDFContentCommandFactory::fromJson(const QJsonObject& object, const PDFContentEditSession* session, const QString& path, std::vector<QString>* errors)
{
    const QJsonValue typeValue = object.value(QLatin1String(PDFContentJson::KEY_TYPE));
    if (!typeValue.isString())
    {
        PDFContentCommandJson::addError(errors, path, PDFContentJson::KEY_TYPE, tr("The type of the operation is expected."));
        return nullptr;
    }

    const Creator creator = PDFContentCommandRegistry::getInstance().getCreator(typeValue.toString().toLatin1());
    if (!creator)
    {
        PDFContentCommandJson::addError(errors, path, PDFContentJson::KEY_TYPE, tr("The operation '%1' is not known.").arg(typeValue.toString()));
        return nullptr;
    }

    if (!session)
    {
        PDFContentCommandJson::addError(errors, path, nullptr, tr("There is no session to read the operation for."));
        return nullptr;
    }

    const size_t errorCount = errors ? errors->size() : 0;
    PDFContentCommandPointer command = creator(object, session, path, errors);
    if (!command && errors && errors->size() == errorCount)
    {
        // A creator, which refuses without a word, still gives an error
        PDFContentCommandJson::addError(errors, path, nullptr, tr("The operation cannot be read."));
    }
    return command;
}

bool PDFContentCommandFactory::registerType(const char* type, Creator creator)
{
    return PDFContentCommandRegistry::getInstance().registerType(type, creator);
}

std::vector<QByteArray> PDFContentCommandFactory::getTypes()
{
    return PDFContentCommandRegistry::getInstance().getTypes();
}

}   // namespace pdf
