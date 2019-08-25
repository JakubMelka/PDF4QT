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

#ifndef PDFPATTERN_H
#define PDFPATTERN_H

#include "pdfobject.h"
#include "pdffunction.h"
#include "pdfcolorspaces.h"

#include <QMatrix>

#include <memory>

namespace pdf
{
class PDFPattern;

using PDFPatternPtr = std::shared_ptr<PDFPattern>;

enum class PatternType
{
    Invalid = 0,
    Tiling = 1,
    Shading = 2,
};

enum class ShadingType
{
    Invalid = 0,
    Function = 1,
    Axial = 2,
    Radial = 3,
    FreeFormGouradTriangle = 4,
    LatticeFormGouradTriangle = 5,
    CoonsPatchMesh = 6,
    TensorProductPatchMesh = 7
};

/// Represents tiling/shading pattern
class PDFPattern
{
public:
    explicit PDFPattern() = default;
    virtual ~PDFPattern() = default;

    virtual PatternType getType() const = 0;

    /// Returns bounding box in the shadings target coordinate system (not in
    /// pattern coordinate system).
    const QRectF& getBoundingBox() const { return m_boundingBox; }

    /// Returns transformation matrix from pattern space to the default
    /// target space.
    const QMatrix& getMatrix() const { return m_matrix; }

    /// Create pattern from the object. If error occurs, exception is thrown
    /// \param colorSpaceDictionary Color space dictionary
    /// \param document Document, owning the pdf object
    /// \param object Object defining the pattern
    static PDFPatternPtr createPattern(const PDFDictionary* colorSpaceDictionary, const PDFDocument* document, const PDFObject& object);

    /// Create shading pattern from the object. If error occurs, exception is thrown
    /// \param colorSpaceDictionary Color space dictionary
    /// \param document Document, owning the pdf object
    /// \param object Object defining the shading
    /// \param matrix Matrix converting reference coordinate system to the device coordinate system
    /// \param patternGraphicState Pattern graphic state
    /// \param ignoreBackgroundColor If set, then ignores background color, even if it is present
    static PDFPatternPtr createShadingPattern(const PDFDictionary* colorSpaceDictionary,
                                              const PDFDocument* document,
                                              const PDFObject& shadingObject,
                                              const QMatrix& matrix,
                                              const PDFObject& patternGraphicState,
                                              bool ignoreBackgroundColor);

private:
    QRectF m_boundingBox;
    QMatrix m_matrix;
};

/// Shading pattern - smooth color distribution along the pattern's space
class PDFShadingPattern : public PDFPattern
{
public:
    explicit PDFShadingPattern() = default;

    virtual PatternType getType() const override;
    virtual ShadingType getShadingType() const = 0;

    /// Returns patterns graphic state. This state must be applied before
    /// the shading pattern is painted to the target device.
    const PDFObject& getPatternGraphicState() const { return m_patternGraphicState; }

    /// Returns color space of the pattern.
    const PDFAbstractColorSpace* getColorSpace() const;

    /// Returns patterns background color (if pattern has background color).
    /// If pattern has not background color, then invalid color is returned.
    const QColor& getBackgroundColor() const { return m_backgroundColor; }

    /// Returns true, if shading pattern should be anti-aliased
    bool isAntialiasing() const { return m_antiAlias; }

private:
    friend class PDFPattern;

    PDFObject m_patternGraphicState;
    PDFColorSpacePointer m_colorSpace;
    QColor m_backgroundColor;
    bool m_antiAlias = false;
};

class PDFSingleDimensionShading : public PDFShadingPattern
{
public:
    explicit PDFSingleDimensionShading() = default;

private:
    friend class PDFPattern;

    std::vector<PDFFunctionPtr> m_functions;
    QPointF m_startPoint;
    QPointF m_endPoint;
    PDFReal m_domainStart = 0.0;
    PDFReal m_domainEnd = 1.0;
    bool m_extendStart = false;
    bool m_extendEnd = false;
};

class PDFAxialShading : public PDFSingleDimensionShading
{
public:
    explicit PDFAxialShading() = default;

    virtual ShadingType getShadingType() const override;

private:
    friend class PDFPattern;
};

}   // namespace pdf

#endif // PDFPATTERN_H
