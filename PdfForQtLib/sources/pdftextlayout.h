//    Copyright (C) 2019-2020 Jakub Melka
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

#include <QColor>
#include <QDataStream>
#include <QPainterPath>

#include <set>
#include <compare>

namespace pdf
{
class PDFTextLayout;

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

    friend QDataStream& operator<<(QDataStream& stream, const PDFTextLayoutSettings& settings);
    friend QDataStream& operator>>(QDataStream& stream, PDFTextLayoutSettings& settings);
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

    void applyTransform(const QMatrix& matrix);

    friend QDataStream& operator<<(QDataStream& stream, const TextCharacter& character);
    friend QDataStream& operator>>(QDataStream& stream, TextCharacter& character);
};

using TextCharacters = std::vector<TextCharacter>;

/// Represents text line consisting of set of characters and line bounding box.
class PDFTextLine
{
public:
    explicit inline PDFTextLine() = default;

    /// Construct new line from characters. Characters are sorted in x-coordinate
    /// and bounding box is computed.
    /// \param characters
    explicit PDFTextLine(TextCharacters characters);

    const TextCharacters& getCharacters() const { return m_characters; }
    const QPainterPath& getBoundingBox() const { return m_boundingBox; }
    const QPointF& getTopLeft() const { return m_topLeft; }

    void applyTransform(const QMatrix& matrix);

    friend QDataStream& operator<<(QDataStream& stream, const PDFTextLine& line);
    friend QDataStream& operator>>(QDataStream& stream, PDFTextLine& line);

private:
    TextCharacters m_characters;
    QPainterPath m_boundingBox;
    QPointF m_topLeft;
};

using PDFTextLines = std::vector<PDFTextLine>;

/// Represents text block consisting of set of lines and block bounding box.
class PDFTextBlock
{
public:
    explicit inline PDFTextBlock() = default;
    explicit inline PDFTextBlock(PDFTextLines textLines);

    const PDFTextLines& getLines() const { return m_lines; }
    const QPainterPath& getBoundingBox() const { return m_boundingBox; }
    const QPointF& getTopLeft() const { return m_topLeft; }

    void applyTransform(const QMatrix& matrix);

    friend QDataStream& operator<<(QDataStream& stream, const PDFTextBlock& block);
    friend QDataStream& operator>>(QDataStream& stream, PDFTextBlock& block);

private:
    PDFTextLines m_lines;
    QPainterPath m_boundingBox;
    QPointF m_topLeft;
};

using PDFTextBlocks = std::vector<PDFTextBlock>;

/// Character pointer points to some character in text layout.
/// It also has page index to decide, which page the pointer points to.
struct PDFCharacterPointer
{
    auto operator<=>(const PDFCharacterPointer&) const = default;

    /// Returns true, if character pointer is valid and points to the correct location
    bool isValid() const { return pageIndex > -1; }

    /// Returns true, if character belongs to same block
    bool hasSameBlock(const PDFCharacterPointer& other) const;

    /// Returns true, if character belongs to same line
    bool hasSameLine(const PDFCharacterPointer& other) const;

    int pageIndex = -1;
    size_t blockIndex = 0;
    size_t lineIndex = 0;
    size_t characterIndex = 0;
};

using PDFTextSelectionItem = std::pair<PDFCharacterPointer, PDFCharacterPointer>;
using PDFTextSelectionItems = std::vector<PDFTextSelectionItem>;

struct PDFTextSelectionColoredItem
{
    explicit inline PDFTextSelectionColoredItem() = default;
    explicit inline PDFTextSelectionColoredItem(PDFCharacterPointer start, PDFCharacterPointer end, QColor color) :
        start(start),
        end(end),
        color(color)
    {

    }

    inline bool operator<(const PDFTextSelectionColoredItem& other) const { return std::tie(start, end) < std::tie(other.start, other.end); }

    PDFCharacterPointer start;
    PDFCharacterPointer end;
    QColor color;
};
using PDFTextSelectionColoredItems = std::vector<PDFTextSelectionColoredItem>;

/// Text selection, can be used across multiple pages. Also defines color
/// for each text selection.
class PDFTextSelection
{
public:
    explicit PDFTextSelection() = default;

    /// Adds text selection items to selection
    /// \param items Items
    /// \param color Color for items (must include alpha channel)
    void addItems(const PDFTextSelectionItems& items, QColor color);

    /// Builds text selection, so it is prepared for rendering. Text selection,
    /// which is not build, can't be used for rendering.
    void build();

private:
    PDFTextSelectionColoredItems m_items;
};

struct PDFFindResult
{
    bool operator<(const PDFFindResult& other) const;

    /// Matched string during search
    QString matched;

    /// Context (text before and after match)
    QString context;

    /// Matched selection (can be multiple items, if selection
    /// is spanned between multiple blocks)
    PDFTextSelectionItems textSelectionItems;
};
using PDFFindResults = std::vector<PDFFindResult>;

class PDFTextFlow;
using PDFTextFlows = std::vector<PDFTextFlow>;

/// This class represents a portion of continuous text on the page. It can
/// consists of multiple blocks (which follow reading order).
class PDFTextFlow
{
public:

    enum FlowFlag
    {
        None                = 0x0000,
        SeparateBlocks      = 0x0001, ///< Create flow for each block
        RemoveSoftHyphen    = 0x0002, ///< Removes 'soft hyphen' unicode character from end-of-line (character 0x00AD)
        AddLineBreaks       = 0x0004, ///< Add line break characters to the end of line
    };
    Q_DECLARE_FLAGS(FlowFlags, FlowFlag)

    /// Finds simple text in current text flow. All text occurences are returned.
    /// \param text Text to be found
    /// \param caseSensitivity Case sensitivity
    PDFFindResults find(const QString& text, Qt::CaseSensitivity caseSensitivity) const;

    /// Finds regular expression matches in current text flow. All text occurences are returned.
    /// \param expression Regular expression to be matched
    PDFFindResults find(const QRegularExpression& expression) const;

    /// Merge data from \p next flow (i.e. connect two consecutive flows)
    void merge(const PDFTextFlow& next);

    /// Creates text flows from text layout, according to creation flags.
    /// \param layout Layout, from which is text flow created
    /// \param flags Flow creation flags
    /// \param pageIndex Page index
    static PDFTextFlows createTextFlows(const PDFTextLayout& layout, FlowFlags flags, PDFInteger pageIndex);

private:
    /// Returns text selection from index and length. Returned text selection can also
    /// be empty (for example, if only single space character is selected, which has
    /// no counterpart in real text)
    /// \param index Index of text selection subrange
    /// \param length Length of text selection
    PDFTextSelectionItems getTextSelectionItems(size_t index, size_t length) const;

    /// Returns context for text selection (or empty string, if text selection is empty)
    /// \param index Index of text selection subrange
    /// \param length Length of text selection
    QString getContext(size_t index, size_t length) const;

    QString m_text;
    std::vector<PDFCharacterPointer> m_characterPointers;
};

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

    /// Returns recognized text blocks
    const PDFTextBlocks& getTextBlocks() const { return m_blocks; }

    friend QDataStream& operator<<(QDataStream& stream, const PDFTextLayout& layout);
    friend QDataStream& operator>>(QDataStream& stream, PDFTextLayout& layout);

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
    PDFTextBlocks m_blocks;
};

/// Storage for text layouts. For reading and writing, this object is thread safe.
/// For writing, mutex is used to synchronize asynchronous writes, for reading
/// no mutex is used at all. For this reason, both reading/writing at the same time
/// is prohibited, it is not thread safe.
class PDFFORQTLIBSHARED_EXPORT PDFTextLayoutStorage
{
public:
    explicit inline PDFTextLayoutStorage() = default;
    explicit inline PDFTextLayoutStorage(PDFInteger pageCount) :
        m_offsets(pageCount, 0)
    {

    }

    /// Returns text layout for particular page. If page index is invalid,
    /// then empty text layout is returned. Function is not thread safe, if
    /// function \p setTextLayout is called from another thread.
    /// \param pageIndex Page index
    PDFTextLayout getTextLayout(PDFInteger pageIndex) const;

    /// Sets text layout to the particular index. Index must be valid and from
    /// range 0 to \p pageCount - 1. Function is not thread safe.
    /// \param pageIndex Page index
    /// \param layout Text layout
    /// \param mutex Mutex for locking (calls of setTextLayout from multiple threads)
    void setTextLayout(PDFInteger pageIndex, const PDFTextLayout& layout, QMutex* mutex);

    /// Finds simple text in all pages. All text occurences are returned.
    /// \param text Text to be found
    /// \param caseSensitivity Case sensitivity
    /// \param flowFlags Text flow flags
    PDFFindResults find(const QString& text, Qt::CaseSensitivity caseSensitivity, PDFTextFlow::FlowFlags flowFlags) const;

    /// Finds regular expression matches in current text flow. All text occurences are returned.
    /// \param expression Regular expression to be matched
    /// \param flowFlags Text flow flags
    PDFFindResults find(const QRegularExpression& expression, PDFTextFlow::FlowFlags flowFlags) const;

private:
    std::vector<int> m_offsets;
    QByteArray m_textLayouts;
};

}   // namespace pdf

#endif // PDFTEXTLAYOUT_H
