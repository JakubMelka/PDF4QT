//    Copyright (C) 2019 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.

#include "pdfpattern.h"
#include "pdfdocument.h"
#include "pdfexception.h"
#include "pdfutils.h"
#include "pdfcolorspaces.h"

#include <QPainter>

namespace pdf
{

PatternType PDFShadingPattern::getType() const
{
    return PatternType::Shading;
}

ShadingType PDFAxialShading::getShadingType() const
{
    return ShadingType::Axial;
}

PDFPatternPtr PDFPattern::createPattern(const PDFDictionary* colorSpaceDictionary, const PDFDocument* document, const PDFObject& object)
{
    const PDFObject& dereferencedObject = document->getObject(object);
    if (dereferencedObject.isDictionary())
    {
        PDFPatternPtr result;

        const PDFDictionary* patternDictionary = dereferencedObject.getDictionary();
        PDFDocumentDataLoaderDecorator loader(document);

        if (loader.readNameFromDictionary(patternDictionary, "Type") != "Pattern")
        {
            throw PDFParserException(PDFTranslationContext::tr("Invalid pattern."));
        }

        const PatternType patternType = static_cast<PatternType>(loader.readIntegerFromDictionary(patternDictionary, "PatternType", static_cast<PDFInteger>(PatternType::Invalid)));
        switch (patternType)
        {
            case PatternType::Tiling:
            {
                // TODO: Implement tiling pattern
                throw PDFParserException(PDFTranslationContext::tr("Tiling pattern not implemented."));
                break;
            }

            case PatternType::Shading:
            {
                PDFObject patternGraphicState = document->getObject(patternDictionary->get("ExtGState"));
                QMatrix matrix = loader.readMatrixFromDictionary(patternDictionary, "Matrix", QMatrix());
                return createShadingPattern(colorSpaceDictionary, document, patternDictionary->get("Shading"), matrix, patternGraphicState, false);
            }

            default:
                throw PDFParserException(PDFTranslationContext::tr("Invalid pattern."));
        }

        return result;
    }

    throw PDFParserException(PDFTranslationContext::tr("Invalid pattern."));
    return PDFPatternPtr();
}

PDFPatternPtr PDFPattern::createShadingPattern(const PDFDictionary* colorSpaceDictionary,
                                               const PDFDocument* document,
                                               const PDFObject& shadingObject,
                                               const QMatrix& matrix,
                                               const PDFObject& patternGraphicState,
                                               bool ignoreBackgroundColor)
{
    const PDFObject& dereferencedShadingObject = document->getObject(shadingObject);
    if (!dereferencedShadingObject.isDictionary())
    {
        throw PDFParserException(PDFTranslationContext::tr("Invalid shading."));
    }

    PDFDocumentDataLoaderDecorator loader(document);
    const PDFDictionary* shadingDictionary = dereferencedShadingObject.getDictionary();

    // Parse common data for all shadings
    PDFColorSpacePointer colorSpace = PDFAbstractColorSpace::createColorSpace(colorSpaceDictionary, document, shadingDictionary->get("ColorSpace"));
    QColor backgroundColor;
    if (!ignoreBackgroundColor)
    {
        std::vector<PDFReal> backgroundColorValues = loader.readNumberArrayFromDictionary(shadingDictionary, "Background");
        if (!backgroundColorValues.empty())
        {
            backgroundColor = colorSpace->getCheckedColor(PDFAbstractColorSpace::convertToColor(backgroundColorValues));
        }
    }
    QRectF boundingBox = loader.readRectangle(shadingDictionary->get("BBox"), QRectF());
    bool antialias = loader.readBooleanFromDictionary(shadingDictionary, "AntiAlias", false);
    const PDFObject& extendObject = document->getObject(shadingDictionary->get("Extend"));
    bool extendStart = false;
    bool extendEnd = false;
    if (extendObject.isArray())
    {
        const PDFArray* array = extendObject.getArray();
        if (array->getCount() != 2)
        {
            throw PDFParserException(PDFTranslationContext::tr("Invalid shading pattern extends. Expected 2, but %1 provided.").arg(array->getCount()));
        }

        extendStart = loader.readBoolean(array->getItem(0), false);
        extendEnd = loader.readBoolean(array->getItem(1), false);
    }
    std::vector<PDFFunctionPtr> functions;
    const PDFObject& functionsObject = document->getObject(shadingDictionary->get("Function"));
    if (functionsObject.isArray())
    {
        const PDFArray* functionsArray = functionsObject.getArray();
        functions.reserve(functionsArray->getCount());
        for (size_t i = 0, functionCount = functionsArray->getCount(); i < functionCount; ++i)
        {
            functions.push_back(PDFFunction::createFunction(document, functionsArray->getItem(i)));
        }
    }
    else if (!functionsObject.isNull())
    {
        functions.push_back(PDFFunction::createFunction(document, functionsObject));
    }

    const ShadingType shadingType = static_cast<ShadingType>(loader.readIntegerFromDictionary(shadingDictionary, "ShadingType", static_cast<PDFInteger>(ShadingType::Invalid)));
    switch (shadingType)
    {
        case ShadingType::Axial:
        {
            PDFAxialShading* axialShading = new PDFAxialShading();
            PDFPatternPtr result(axialShading);

            std::vector<PDFReal> coordinates = loader.readNumberArrayFromDictionary(shadingDictionary, "Coords");
            if (coordinates.size() != 4)
            {
                throw PDFParserException(PDFTranslationContext::tr("Invalid axial shading pattern coordinates. Expected 4, but %1 provided.").arg(coordinates.size()));
            }

            std::vector<PDFReal> domain = loader.readNumberArrayFromDictionary(shadingDictionary, "Domain");
            if (domain.empty())
            {
                domain = { 0.0, 1.0 };
            }
            if (domain.size() != 2)
            {
                throw PDFParserException(PDFTranslationContext::tr("Invalid axial shading pattern domain. Expected 2, but %1 provided.").arg(domain.size()));
            }

            // Load items for axial shading
            axialShading->m_antiAlias = antialias;
            axialShading->m_backgroundColor = backgroundColor;
            axialShading->m_colorSpace = colorSpace;
            axialShading->m_boundingBox = boundingBox;
            axialShading->m_domainStart = domain[0];
            axialShading->m_domainEnd = domain[1];
            axialShading->m_startPoint = QPointF(coordinates[0], coordinates[1]);
            axialShading->m_endPoint = QPointF(coordinates[2], coordinates[3]);
            axialShading->m_extendStart = extendStart;
            axialShading->m_extendEnd = extendEnd;
            axialShading->m_functions = qMove(functions);
            axialShading->m_matrix = matrix;
            axialShading->m_patternGraphicState = patternGraphicState;

            return result;
        }

        default:
        {
            throw PDFParserException(PDFTranslationContext::tr("Invalid shading pattern type (%1).").arg(static_cast<PDFInteger>(shadingType)));
        }
    }

    throw PDFParserException(PDFTranslationContext::tr("Invalid shading."));
    return PDFPatternPtr();
}

PDFMesh PDFAxialShading::createMesh(const PDFMeshQualitySettings& settings) const
{
    PDFMesh mesh;

    QPointF p1 = settings.userSpaceToDeviceSpaceMatrix.map(m_startPoint);
    QPointF p2 = settings.userSpaceToDeviceSpaceMatrix.map(m_endPoint);

    // Strategy: for simplification, we rotate the line clockwise so we will
    // get the shading axis equal to the x-axis. Then we will determine the shading
    // area and create mesh according the settings.
    QLineF line(p1, p2);
    const double angle = line.angleTo(QLineF(0, 0, 1, 0));

    // Matrix p1p2LCS is local coordinate system of line p1-p2. It transforms
    // points on the line to the global coordinate system. So, point (0, 0) will
    // map onto p1 and point (length(p1-p2), 0) will map onto p2.
    QMatrix p1p2LCS;
    p1p2LCS.translate(p1.x(), p1.y());
    p1p2LCS.rotate(angle);
    QMatrix p1p2GCS = p1p2LCS.inverted();

    QPointF p1m = p1p2GCS.map(p1);
    QPointF p2m = p1p2GCS.map(p2);

    Q_ASSERT(isZero(p1m.y()));
    Q_ASSERT(isZero(p2m.y()));
    Q_ASSERT(p1m.x() <= p2m.x());

    QPainterPath meshingArea;
    meshingArea.addPolygon(p1p2GCS.map(settings.deviceSpaceMeshingArea));
    meshingArea.addRect(p1m.x(), p1m.y() - settings.preferredMeshResolution * 0.5, p2m.x() - p1m.x(), settings.preferredMeshResolution);
    QRectF meshingRectangle = meshingArea.boundingRect();

    PDFReal xl = meshingRectangle.left();
    PDFReal xr = meshingRectangle.right();
    PDFReal yt = meshingRectangle.top();
    PDFReal yb = meshingRectangle.bottom();

    // Create coordinate array filled with stops, where we will determine the color
    std::vector<PDFReal> xCoords;
    xCoords.reserve((xr - xl) / settings.minimalMeshResolution + 3);
    xCoords.push_back(xl);
    for (PDFReal x = p1m.x(); x <= p2m.x(); x += settings.minimalMeshResolution)
    {
        if (!qFuzzyCompare(xCoords.back(), x))
        {
            xCoords.push_back(x);
        }
    }
    if (!qFuzzyCompare(xCoords.back(), xr))
    {
        xCoords.push_back(xr);
    }

    const PDFReal tAtStart = m_domainStart;
    const PDFReal tAtEnd = m_domainEnd;
    const PDFReal tMin = qMin(tAtStart, tAtEnd);
    const PDFReal tMax = qMax(tAtStart, tAtEnd);

    const bool isSingleFunction = m_functions.size() == 1;
    std::vector<PDFReal> colorBuffer(m_colorSpace->getColorComponentCount(), 0.0);
    auto getColor = [this, isSingleFunction, &colorBuffer](PDFReal t) -> PDFColor
    {
        if (isSingleFunction)
        {
            PDFFunction::FunctionResult result = m_functions.front()->apply(&t, &t + 1, colorBuffer.data(), colorBuffer.data() + colorBuffer.size());
            if (!result)
            {
                throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Error occured during mesh creation of shading: %1").arg(result.errorMessage));
            }
        }
        else
        {
            for (size_t i = 0, count = colorBuffer.size(); i < count; ++i)
            {
                PDFFunction::FunctionResult result = m_functions.front()->apply(&t, &t + 1, colorBuffer.data() + i, colorBuffer.data() + i + 1);
                if (!result)
                {
                    throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Error occured during mesh creation of shading: %1").arg(result.errorMessage));
                }
            }
        }

        return PDFAbstractColorSpace::convertToColor(colorBuffer);
    };

    // Determine color of each coordinate
    std::vector<std::pair<PDFReal, PDFColor>> coloredCoordinates;
    coloredCoordinates.reserve(xCoords.size());

    for (PDFReal x : xCoords)
    {
        if (x < p1m.x() - PDF_EPSILON && !m_extendStart)
        {
            // Move to the next coordinate, this is skipped
            continue;
        }

        if (x > p2m.x() + PDF_EPSILON && !m_extendEnd)
        {
            // We are finished no more triangles will occur
            break;
        }

        // Determine current parameter t
        const PDFReal t = interpolate(x, p1m.x(), p2m.x(), tAtStart, tAtEnd);
        const PDFReal tBounded = qBound(tMin, t, tMax);
        const PDFColor color = getColor(tBounded);
        coloredCoordinates.emplace_back(x, color);
    }

    // Filter coordinates according the meshing criteria
    std::vector<std::pair<PDFReal, PDFColor>> filteredCoordinates;
    filteredCoordinates.reserve(coloredCoordinates.size());

    for (auto it = coloredCoordinates.cbegin(); it != coloredCoordinates.cend(); ++it)
    {
        // We will skip this coordinate, if both of meshing criteria have been met:
        //  1) Color difference is small (lesser than tolerance)
        //  2) Distance from previous and next point is less than preffered meshing resolution OR colors are equal

        if (it != coloredCoordinates.cbegin() && std::next(it) != coloredCoordinates.cend())
        {
            auto itNext = std::next(it);

            const std::pair<PDFReal, PDFColor>& prevItem = filteredCoordinates.back();
            const std::pair<PDFReal, PDFColor>& currentItem = *it;
            const std::pair<PDFReal, PDFColor>& nextItem = *itNext;

            if (prevItem.second == currentItem.second && currentItem.second == nextItem.second)
            {
                // Colors are same, skip the test
                continue;
            }

            if (PDFAbstractColorSpace::isColorEqual(prevItem.second, currentItem.second, settings.tolerance) &&
                PDFAbstractColorSpace::isColorEqual(currentItem.second, nextItem.second, settings.tolerance) &&
                PDFAbstractColorSpace::isColorEqual(prevItem.second, nextItem.second, settings.tolerance) &&
                (nextItem.first - prevItem.first < settings.preferredMeshResolution))
            {
                continue;
            }
        }

        filteredCoordinates.push_back(*it);
    }

    if (!filteredCoordinates.empty())
    {
        size_t vertexCount = filteredCoordinates.size() * 2;
        size_t triangleCount = filteredCoordinates.size() * 2 - 2;

        if (m_backgroundColor.isValid())
        {
            vertexCount += 8;
            triangleCount += 4;
        }
        mesh.reserve(vertexCount, triangleCount);

        PDFColor previousColor = filteredCoordinates.front().second;
        uint32_t topLeft = mesh.addVertex(QPointF(filteredCoordinates.front().first, yt));
        uint32_t bottomLeft = mesh.addVertex(QPointF(filteredCoordinates.front().first, yb));
        for (auto it = std::next(filteredCoordinates.cbegin()); it != filteredCoordinates.cend(); ++it)
        {
            const std::pair<PDFReal, PDFColor>& item = *it;

            uint32_t topRight = mesh.addVertex(QPointF(item.first, yt));
            uint32_t bottomRight = mesh.addVertex(QPointF(item.first, yb));

            PDFColor mixedColor = PDFAbstractColorSpace::mixColors(previousColor, item.second, 0.5);
            QColor color = m_colorSpace->getColor(mixedColor);
            mesh.addQuad(topLeft, topRight, bottomRight, bottomLeft, color.rgb());

            topLeft = topRight;
            bottomLeft = bottomRight;
            previousColor = item.second;
        }
    }

    // Create background color triangles

    // Transform mesh to the device space coordinates
    mesh.transform(p1p2LCS);

    // Transform mesh from the device space coordinates to user space coordinates
    Q_ASSERT(settings.userSpaceToDeviceSpaceMatrix.isInvertible());
    QMatrix deviceSpaceToUserSpaceMatrix = settings.userSpaceToDeviceSpaceMatrix.inverted();
    mesh.transform(deviceSpaceToUserSpaceMatrix);

    // Create bounding path
    if (m_boundingBox.isValid())
    {
        QPainterPath boundingPath;
        boundingPath.addRect(m_boundingBox);
        mesh.setBoundingPath(boundingPath);
    }

    return mesh;
}

void PDFMesh::paint(QPainter* painter) const
{
    if (m_triangles.empty())
    {
        return;
    }

    painter->save();
    painter->setPen(Qt::NoPen);
    painter->setRenderHint(QPainter::Antialiasing, true);

    // Set the clipping area, if we have it
    if (!m_boundingPath.isEmpty())
    {
        painter->setClipPath(m_boundingPath, Qt::IntersectClip);
    }

    QColor color;

    // Draw all triangles
    for (const Triangle& triangle : m_triangles)
    {
        if (color != triangle.color)
        {
            painter->setPen(QColor(triangle.color));
            painter->setBrush(QBrush(triangle.color, Qt::SolidPattern));
            color = triangle.color;
        }

        std::array<QPointF, 3> triangleCorners = { m_vertices[triangle.v1], m_vertices[triangle.v2], m_vertices[triangle.v3] };
        painter->drawConvexPolygon(triangleCorners.data(), static_cast<int>(triangleCorners.size()));
    }

    painter->restore();
}

void PDFMesh::transform(const QMatrix& matrix)
{
    for (QPointF& vertex : m_vertices)
    {
        vertex = matrix.map(vertex);
    }

    m_boundingPath = matrix.map(m_boundingPath);
}

void PDFMeshQualitySettings::initDefaultResolution()
{
    // We will take 0.5% percent of device space meshing area as minimal resolution (it is ~1.5 mm for
    // A4 page) and default resolution 4x number of that.

    Q_ASSERT(deviceSpaceMeshingArea.isValid());
    PDFReal size = qMax(deviceSpaceMeshingArea.width(), deviceSpaceMeshingArea.height());
    minimalMeshResolution = size * 0.005;
    preferredMeshResolution = minimalMeshResolution * 4;
}

}   // namespace pdf
