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

}   // namespace pdf
