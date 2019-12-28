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

#ifndef PDFTEXTLAYOUT_H
#define PDFTEXTLAYOUT_H

#include "pdfglobal.h"

#include <QPainterPath>

#include <set>

namespace pdf
{

struct PDFTextCharacterInfo
{
    /// Character
    QChar character;

    /// Character path
    QPainterPath outline;

    /// Do we use a vertical writing system?
    bool isVerticalWritingSystem = false;

    /// Advance (in character space, it must be translated
    /// into device space), for both vertical/horizontal modes.
    PDFReal advance = 0.0;

    /// Font size (in character space, it must be translated
    /// into device space)
    PDFReal fontSize = 0.0;

    /// Transformation matrix from character space to device space
    QMatrix matrix;
};

struct PDFTextLayoutSettings
{
    /// Number of samples for 'docstrum' algorithm, i.e. number of
    /// nearest characters. By default, 5 characters should fit.
    size_t samples = 5;

    /// Distance sensitivity to determine, if characters are close enough.
    /// Maximal distance is computed as current character advance multiplied
    /// by this constant.
    PDFReal distanceSensitivity = 4.0;

    /// Maximal vertical distance, in portion of font size, of two characters
    /// to be considered they lie on same line.
    PDFReal charactersOnLineSensitivity = 0.25;

    /// Maximal ratio between font size of characters to be considered
    /// that they lie on same line.
    PDFReal fontSensitivity = 2.0;

    /// Maximal space ratio between two lines of block. Default coefficient
    /// means, that height ratio limit is (height1 + height2)
    PDFReal blockVerticalSensitivity = 1.5;

    /// Minimal horizontal overlap for two lines considered to be in one block
    PDFReal blockOverlapSensitivity = 0.3;
};

/// Represents character in device space coordinates. All values (dimensions,
/// bounding box, etc. are in device space coordinates).
struct TextCharacter
{
    QChar character;
    QPointF position;
    PDFReal angle = 0.0;
    PDFReal fontSize = 0.0;
    PDFReal advance = 0.0;
    QPainterPath boundingBox;
};

using TextCharacters = std::vector<TextCharacter>;

/// Represents text line consisting of set of characters and line bounding box.
class PDFTextLine
{
public:
    /// Construct new line from characters. Characters are sorted in x-coordinate
    /// and bounding box is computed.
    /// \param characters
    explicit PDFTextLine(TextCharacters characters);

    const TextCharacters& getCharacters() const { return m_characters; }
    const QPainterPath& getBoundingBox() const { return m_boundingBox; }

private:
    TextCharacters m_characters;
    QPainterPath m_boundingBox;
};

using PDFTextLines = std::vector<PDFTextLine>;

/// Represents text block consisting of set of lines and block bounding box.
class PDFTextBlock
{
public:
    explicit inline PDFTextBlock(PDFTextLines textLines);

    const PDFTextLines& getLines() const { return m_lines; }
    const QPainterPath& getBoundingBox() const { return m_boundingBox; }

private:
    PDFTextLines m_lines;
    QPainterPath m_boundingBox;
};

using PDFTextBlocks = std::vector<PDFTextBlock>;

/// Text layout of single page. Can handle various fonts, various angles of lines
/// and vertically oriented text. It performs the "docstrum" algorithm.
class PDFTextLayout
{
public:
    explicit PDFTextLayout();

    /// Adds character to the layout
    void addCharacter(const PDFTextCharacterInfo& info);

    /// Perorms text layout algorithm
    void perform();

    /// Optimizes layout memory allocation to contain less space
    void optimize();

    /// Returns estimate of number of bytes, which this mesh occupies in memory
    qint64 getMemoryConsumptionEstimate() const;

private:

    /// Makes layout for particular angle
    void performDoLayout(PDFReal angle);

    /// Returns a list of characters for particular angle. Exact match is used
    /// for angle, even if angle is floating point number.
    /// \param angle Angle
    TextCharacters getCharactersForAngle(PDFReal angle) const;

    /// Applies transform to text characters (positions and bounding boxes)
    /// \param characters Characters
    /// \param matrix Transform matrix
    void applyTransform(TextCharacters& characters, const QMatrix& matrix);

    TextCharacters m_characters;
    std::set<PDFReal> m_angles;
    PDFTextLayoutSettings m_settings;
};

}   // namespace pdf

#endif // PDFTEXTLAYOUT_H
