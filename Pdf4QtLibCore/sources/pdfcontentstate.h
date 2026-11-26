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

#ifndef PDFCONTENTSTATE_H
#define PDFCONTENTSTATE_H

#include "pdfglobal.h"
#include "pdfobject.h"
#include "pdfpagecontentprocessor.h"

#include <QByteArray>
#include <QTransform>

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace pdf
{

// Edit Content, stage E0, package P02 (tree and state). Plan: zadani_jadro.md 4.2 (common fields of
// a node), 4.3 (symbolic state, leak), 4.4 (R-1 override, R-2, R-3, R-5, R-6). After stage E0 this
// file is changed only by the coordinator (each stage adds fields through its skeleton).

// Threads and ownership of the plain types of this header (PDFContentStatePropertySet,
// PDFContentColorValue, PDFContentStateValue, PDFContentPropertySource, PDFContentStateLeak):
// values; their objects are owned PDFObject values (never a pointer into the document), the
// states of a leak are shared pointers to immutable states. Copied freely, also between threads.

/// A parameter of the graphics state or of the text state, which an operator or an entry of an
/// ExtGState dictionary sets (ISO 32000-2, 8.4, Table 51, Table 57; 9.3, Table 102). The current
/// transformation matrix and the clipping path are not properties: the matrix is a value of the
/// resolved state and of an override, the clip is a flag of a leak and never baked (R-2).
enum class PDFContentStateProperty
{
    LineWidth,              ///< w, /LW
    LineCap,                ///< J, /LC
    LineJoin,               ///< j, /LJ
    MiterLimit,             ///< M, /ML
    DashPattern,            ///< d, /D
    RenderingIntent,        ///< ri, /RI
    Flatness,               ///< i, /FL
    StrokeColor,            ///< CS + SC / SCN, G, RG, K: the color space, the components and the pattern
    FillColor,              ///< cs + sc / scn, g, rg, k
    Font,                   ///< Tf, /Font: the font and its size
    CharacterSpacing,       ///< Tc
    WordSpacing,            ///< Tw
    HorizontalScaling,      ///< Tz
    Leading,                ///< TL (also TD and ")
    TextRenderingMode,      ///< Tr
    TextRise,               ///< Ts
    TextKnockout,           ///< /TK
    AlphaStroking,          ///< /CA
    AlphaFilling,           ///< /ca
    BlendMode,              ///< /BM
    SoftMask,               ///< /SMask (with the matrix, in whose space it was set - R-6; in an override: PDFContentStateValue::matrix, an absolute matrix)
    AlphaIsShape,           ///< /AIS
    StrokeAdjustment,       ///< /SA
    OverprintStroking,      ///< /OP
    OverprintFilling,       ///< /op
    OverprintMode,          ///< /OPM
    BlackGeneration,        ///< /BG, /BG2
    UndercolorRemoval,      ///< /UCR, /UCR2
    Transfer,               ///< /TR, /TR2
    Halftone,               ///< /HT
    HalftoneOrigin,         ///< /HTO
    Smoothness,             ///< /SM
    BlackPointCompensation, ///< /UseBlackPtComp

    Count                   ///< Number of the properties, not a property
};

/// A set of properties
class PDFContentStatePropertySet
{
public:
    constexpr PDFContentStatePropertySet() = default;

    constexpr bool contains(PDFContentStateProperty property) const { return (m_bits & getBit(property)) != 0; }
    constexpr void insert(PDFContentStateProperty property) { m_bits |= getBit(property); }
    constexpr void remove(PDFContentStateProperty property) { m_bits &= ~getBit(property); }
    constexpr void unite(const PDFContentStatePropertySet& other) { m_bits |= other.m_bits; }
    constexpr bool isEmpty() const { return m_bits == 0; }
    constexpr bool operator==(const PDFContentStatePropertySet&) const = default;

    /// The properties of the set in the order of the enum
    std::vector<PDFContentStateProperty> getProperties() const
    {
        std::vector<PDFContentStateProperty> result;
        for (int i = 0; i < int(PDFContentStateProperty::Count); ++i)
        {
            if (contains(PDFContentStateProperty(i)))
            {
                result.push_back(PDFContentStateProperty(i));
            }
        }
        return result;
    }

private:
    static constexpr uint64_t getBit(PDFContentStateProperty property) { return uint64_t(1) << uint64_t(property); }

    uint64_t m_bits = 0;
};

/// A color as the content sets it: the color space as it is named in the content and the
/// components, never converted (zadani_jadro.md 4.8)
struct PDFContentColorValue
{
    QByteArray deviceSpace;             ///< "DeviceGray", "DeviceRGB" or "DeviceCMYK" for g / rg / k (and their stroking twins); empty otherwise
    PDFObject colorSpaceObject;         ///< The color space of cs / CS as it is in the resources (a reference or a direct object); null for a device space
    std::vector<PDFReal> components;    ///< The operands of the color operator (the tint components of an uncolored pattern)
    PDFObject patternObject;            ///< The pattern of scn / SCN (a reference); null, when the color is no pattern

    bool isPattern() const { return !patternObject.isNull(); }
};

/// The value of one property in an override (R-1, R-2) or in a residual state run (R-3)
struct PDFContentStateValue
{
    enum class Kind
    {
        None,       ///< No value
        Real,       ///< \p real: LineWidth, MiterLimit, Flatness, the spacings, the scaling (in percent, as Tz), Leading, TextRise, the alphas, Smoothness
        Integer,    ///< \p integer: LineCap, LineJoin, TextRenderingMode, OverprintMode
        Boolean,    ///< \p boolean: TextKnockout, AlphaIsShape, StrokeAdjustment, the overprints
        Name,       ///< \p name: RenderingIntent, BlendMode, BlackPointCompensation (ON, OFF, Default); also Default for a property, which the standard lets reset so (BlackGeneration, UndercolorRemoval, Transfer: written by the keys BG2, UCR2, TR2)
        Dash,       ///< \p dash: DashPattern
        Color,      ///< \p color: StrokeColor, FillColor
        Font,       ///< \p object (the font: the reference of a font dictionary, never the array [font size] of an ExtGState - it is written as "/name size Tf" with a resource name of this object) and \p real (its size)
        Object      ///< \p object: a value written into an ExtGState by reference, taken from the symbolic state of the source (SoftMask, BlackGeneration, UndercolorRemoval, Transfer, Halftone, HalftoneOrigin, a BlendMode array); \p matrix for SoftMask
    };

    Kind kind = Kind::None;
    PDFReal real = 0.0;
    PDFInteger integer = 0;
    bool boolean = false;
    QByteArray name;
    PDFLineDashPattern dash;
    PDFContentColorValue color;
    PDFObject object;

    /// SoftMask only: the current transformation matrix, which must be in effect at the gs of the
    /// mask (the space of the mask is the matrix of its group x this matrix, ISO 32000-2,
    /// 11.6.5.1). It is an absolute matrix of the container, as
    /// PDFContentSymbolicState::getSoftMaskMatrix - not a matrix relative to the node or to the
    /// override. The stream writer computes X = matrix x (override.transform x the current
    /// transformation matrix of the state of the node)^-1 and writes "X cm, gs, X^-1 cm" (only
    /// gs, when X is the identity). So a mask, which must stay where it was, has the matrix of
    /// the symbolic state; a mask, which must move with the object by D (in the space of the
    /// container), has that matrix x D (R-6).
    QTransform matrix;
};

/// The state at the beginning of a node as the renderer sees it: the state of the processor
/// (current transformation matrix, colors, line and text parameters, fonts and color spaces held
/// by shared pointers, transparency parameters).
///
/// Threads and ownership: immutable after it is built, shared by the nodes with the same state
/// (std::shared_ptr<const>), read by any thread. It co-owns its fonts and color spaces. The raw
/// pointer of the soft mask of the processor state is cleared by the builder: the mask is an
/// owned object of the symbolic state, so nothing here points into the document.
class PDF4QTLIBCORESHARED_EXPORT PDFContentResolvedState
{
public:
    PDFContentResolvedState() = default;
    explicit PDFContentResolvedState(const PDFPageContentProcessorState& state);

    /// The state of the processor (its soft mask pointer is nullptr). The state flags of the
    /// copy (PDFPageContentProcessorState::getStateFlags) carry no meaning: nobody reads them.
    const PDFPageContentProcessorState& getState() const { return m_state; }

    /// Current transformation matrix: the space of the node to the default space of its container
    const QTransform& getCurrentTransformationMatrix() const { return m_state.getCurrentTransformationMatrix(); }

    /// A soft mask is set (its object is in the symbolic state). The constructor from a state
    /// of the processor is the only writer of the flag.
    bool hasSoftMask() const { return m_hasSoftMask; }

    /// The properties, whose value differs between two resolved states: what a node leaks (R-3,
    /// the builder) and what a moved node has to bake (R-2, "every differing property"). One
    /// comparison for both, so they cannot drift apart.
    ///
    /// Every property of PDFContentStateProperty is compared by its value in the state of the
    /// processor:
    ///  - numbers, flags and names by equality (the rendering intent by its name and its value,
    ///    the three overprint properties each by its own value);
    ///  - StrokeColor / FillColor by the color space object (the same object, not an equal one:
    ///    the processor shares one object per name of the resources and per device space, but
    ///    makes a new one for every scn / SCN with a pattern, which therefore always differs),
    ///    by the components as the content gave them and by the converted color;
    ///  - Font by the font object (the same object of the font cache, so two builds with two
    ///    caches never have the same font) and by the size;
    ///  - BlackGeneration, UndercolorRemoval, Transfer, Halftone by their objects: the same
    ///    object of the document (the same array, dictionary, stream or string) is equal; two
    ///    different objects are compared by value only as far as a small fixed budget goes (some
    ///    tens of values, short strings, never the data of a stream) and count as different
    ///    beyond it - the cost of the comparison must not depend on a size the document chooses,
    ///    and a difference, which is none, only makes a property be set once more;
    ///  - DashPattern by value: the builder never makes a state with a dash array longer than
    ///    its limit (the content, in which such a pattern is in effect, is an opaque node; the
    ///    state behind such a node at the end of a content has a marker of not-a-number values,
    ///    which is different from every pattern, itself included - PDFContentStateLeak::endState);
    ///  - SoftMask only by "a mask is set" (hasSoftMask): a resolved state does not know, which
    ///    mask it is. Two states with a mask each are equal here, also when the masks or their
    ///    spaces differ - that is told by the symbolic states (the source of SoftMask and
    ///    getSoftMaskMatrix), which the caller compares itself.
    /// Not compared: the current transformation matrix (it is no property - compare
    /// getCurrentTransformationMatrix), the clipping path (a state does not have it) and the
    /// text matrices (they are no part of a resolved state: the builder resets them to the
    /// identity, the positions of a text are in its glyphs).
    static PDFContentStatePropertySet getDifference(const PDFContentResolvedState& left, const PDFContentResolvedState& right);

    /// The same comparison of two states of the processor, without SoftMask (the soft mask of
    /// a state of the processor is a pointer into the document, which the caller compares
    /// itself, when both states are live ones). The builder compares the state of its processor
    /// with its last resolved state so.
    static PDFContentStatePropertySet getDifference(const PDFPageContentProcessorState& left, const PDFPageContentProcessorState& right);

private:
    PDFPageContentProcessorState m_state;
    bool m_hasSoftMask = false;
};

using PDFContentResolvedStatePointer = std::shared_ptr<const PDFContentResolvedState>;

/// Where the value of a property came from
struct PDFContentPropertySource
{
    enum class Origin
    {
        Inherited,  ///< Not set inside of the container: the default of a page, or the state of the place of the first use of a form (unknown for R-2: always baked)
        Operator,   ///< Set by an operator of the container
        ExtGState   ///< Set by gs
    };

    Origin origin = Origin::Inherited;
    QByteArray resourceName;        ///< Operator: the resource the operator names (the font of Tf, the color space of cs / CS); ExtGState: the name of the ExtGState of gs
    QByteArray extGStateKey;        ///< ExtGState: the key of the ExtGState, which set it - BG or BG2, UCR or UCR2, TR or TR2 (the one key of every other property); empty otherwise
    PDFObject object;               ///< The object the property was set to: the font (a reference), the color space, the value of the ExtGState key (kept by reference, never copied). Font set by gs (/Font [font size], ISO 32000-2, Table 57): the font of the array - its first item, the reference of the font dictionary (for the tolerated name in its place: the value of that name in the font resources) - never the array; the size is the font size of the resolved state
    QByteArray patternName;         ///< StrokeColor / FillColor: the pattern of scn / SCN
    PDFObject patternObject;        ///< The pattern object of the resources
    size_t operationIndex = 0;      ///< Index of the operation, which set it (valid unless Inherited)
};

/// The symbolic state at the beginning of a node: for each property where it came from, and
/// for the soft mask the matrix of the moment it was set.
///
/// Threads and ownership: immutable after it is built, shared by nodes (std::shared_ptr<const>),
/// read by any thread; its objects are owned PDFObject values.
class PDF4QTLIBCORESHARED_EXPORT PDFContentSymbolicState
{
public:
    PDFContentSymbolicState() = default;

    /// The source of a property; a property never set is Inherited
    const PDFContentPropertySource& getSource(PDFContentStateProperty property) const;

    bool isInherited(PDFContentStateProperty property) const { return getSource(property).origin == PDFContentPropertySource::Origin::Inherited; }

    /// The current transformation matrix of the moment the soft mask was set (identity without one)
    const QTransform& getSoftMaskMatrix() const { return m_softMaskMatrix; }

    /// A copy with another source of a property
    PDFContentSymbolicState withSource(PDFContentStateProperty property, PDFContentPropertySource source) const;

    /// A copy with another matrix of the soft mask
    PDFContentSymbolicState withSoftMaskMatrix(const QTransform& matrix) const;

private:
    /// The sources of the properties, which are not inherited (nullptr = inherited). A source is
    /// immutable and shared by all states, in which the property has it, so a copy of a state
    /// copies pointers only (the builder makes one state for each change of a property).
    std::array<std::shared_ptr<const PDFContentPropertySource>, size_t(PDFContentStateProperty::Count)> m_sources;
    QTransform m_softMaskMatrix;
};

using PDFContentSymbolicStatePointer = std::shared_ptr<const PDFContentSymbolicState>;

/// What a node changes for the nodes after it: the difference of the state between its beginning
/// and its end (rule R-3: when the node stops reaching the following nodes - it is deleted, moved,
/// wrapped - a residual state run sets the leak at the place of its end). A Group has no leak, a
/// MarkedContent has the leak of its children, an Opaque node has an unknown leak (it is never
/// moved or deleted).
struct PDFContentStateLeak
{
    PDFContentStatePropertySet properties;      ///< Properties, whose value at the end differs from the beginning
    bool isTransformChanged = false;            ///< The current transformation matrix differs (cm outside of q ... Q)
    bool isClipChanged = false;                 ///< The clipping path differs (W, W*, text rendering modes 4 to 7); never replaced by a residual run
    bool isUnknown = false;                     ///< Opaque: nothing is known
    /// The state at the end of the node (nullptr, when nothing leaks). Behind an Opaque node, in
    /// which a dash pattern over the limit of the builder is still in effect at the end of the
    /// content, it is the state there with a marker as its dash pattern (not a number in every
    /// element and the phase): equal to no pattern, so a node placed behind sets its own (R-2).
    PDFContentResolvedStatePointer endState;
    PDFContentSymbolicStatePointer endSymbolicState;

    bool isEmpty() const { return properties.isEmpty() && !isTransformChanged && !isClipChanged && !isUnknown; }
};

/// The change of the appearance and of the position of a node, written as a wrap around it (R-1):
/// q, the matrix, the changed properties, the operations of the node, Q. A node with a clipping
/// effect has no wrap (its override is applied by a canonical rewrite in place).
///
/// Threads and ownership: a plain value owned by its node; immutable inside of a container.
struct PDFContentStateOverride
{
    /// The matrix M of "M cm" at the beginning of the wrap (row vectors as in PDF: inside of the
    /// wrap the matrix is M x CTM). Identity = no cm.
    QTransform transform;

    /// The properties set in the wrap after the matrix
    std::map<PDFContentStateProperty, PDFContentStateValue> properties;

    bool isEmpty() const { return transform.isIdentity() && properties.empty(); }
};

}   // namespace pdf

#endif // PDFCONTENTSTATE_H
