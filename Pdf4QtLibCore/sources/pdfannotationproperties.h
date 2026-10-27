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

#ifndef PDFANNOTATIONPROPERTIES_H
#define PDFANNOTATIONPROPERTIES_H

#include "pdfaction.h"
#include "pdfannotation.h"

#include <QColor>
#include <QDateTime>
#include <QString>

#include <vector>

namespace pdf
{

class PDFDocumentBuilder;
class PDFObjectStorage;

/// Properties of an annotation edited by the properties dialog of the new
/// application (Figma 1022:20233 / 1027:21823): the comment, the colors, the
/// opacity, the border, the flags and the parts of the type (line endings, icon,
/// the text of a text box, the leader lines and the caption of a measurement).
/// The values are read by PDFAnnotationPropertyEditor::read and written by
/// PDFAnnotationPropertyEditor::write; every value is bounded there.
class PDF4QTLIBCORESHARED_EXPORT PDFAnnotationProperties
{
public:
    /// One property (or a group of entries, which are always written together)
    enum Field : quint32
    {
        None                = 0x00000,
        Author              = 0x00001, ///< T
        Subject             = 0x00002, ///< Subj
        Contents            = 0x00004, ///< Contents
        Color               = 0x00008, ///< C (invalid color = empty array, no color)
        InteriorColor       = 0x00010, ///< IC (invalid color = no fill, the entry is removed)
        Opacity             = 0x00020, ///< CA (and ca, when the annotation has it)
        Border              = 0x00040, ///< BS - width, style and the dash pattern (Border is removed)
        Flags               = 0x00080, ///< F
        LineEndings         = 0x00100, ///< LE of a line / polyline (start, end)
        IconName            = 0x00200, ///< Name of a sticky note, a file attachment, a stamp, a sound
        Open                = 0x00400, ///< Open of a sticky note
        DefaultAppearance   = 0x00800, ///< DA of a text box (font, size, text color)
        Alignment           = 0x01000, ///< Q of a text box
        CalloutLineEnding   = 0x02000, ///< LE of a text box with a callout line
        LeaderLines         = 0x04000, ///< LL, LLE of a line
        Caption             = 0x08000, ///< Cap, CP of a line
        HighlightMode       = 0x10000, ///< H of a link
        Destination         = 0x20000, ///< Dest of a link (a GoTo action A is replaced by Dest)
        BlendMode           = 0x40000, ///< BM (ISO 32000-2 Table 166) and the blend mode of the appearance stream
        All                 = 0x7FFFF
    };
    Q_DECLARE_FLAGS(Fields, Field)

    /// Longest author and subject; the longest text of the contents
    static constexpr qsizetype MAXIMUM_TITLE_LENGTH = 1024;
    static constexpr qsizetype MAXIMUM_TEXT_LENGTH = 64 * 1024;

    /// Largest border width, font size, leader line length; most dash items
    static constexpr PDFReal MAXIMUM_BORDER_WIDTH = 100.0;
    static constexpr PDFReal MINIMUM_FONT_SIZE = 1.0;
    static constexpr PDFReal MAXIMUM_FONT_SIZE = 512.0;
    static constexpr qsizetype MAXIMUM_FONT_FAMILY_LENGTH = 127;   ///< The family is a PDF name of DA (limit 127 bytes)
    static constexpr PDFReal MAXIMUM_LEADER_LINE = 10000.0;
    static constexpr size_t MAXIMUM_DASH_ITEMS = 16;
    static constexpr PDFReal MAXIMUM_DASH_LENGTH = 1000.0;
    static constexpr PDFReal MAXIMUM_COORDINATE = 1.0e6;
    static constexpr PDFReal MAXIMUM_ZOOM = 100.0;

    AnnotationType type = AnnotationType::Invalid;
    Fields supportedFields = None;      ///< Properties, which the type has (set by read)

    QString author;
    QString subject;
    QString contents;
    QColor color;                       ///< Invalid = no color
    QColor interiorColor;               ///< Invalid = no fill
    PDFReal opacity = 1.0;
    QByteArray blendMode = "Normal";    ///< Name of BM: Normal, a mode of PDFAnnotationPropertyEditor::getOfferedBlendModes or a mode
                                        ///< of the document, which the viewer does not draw (Hue, ..., Compatible, an unknown name)
    PDFReal borderWidth = 1.0;
    AnnotationBorderStyle borderStyle = AnnotationBorderStyle::Solid;
    std::vector<PDFReal> dashPattern;   ///< Dash pattern of the style Dashed (empty = [3])
    PDFAnnotation::Flags flags;
    AnnotationLineEnding startLineEnding = AnnotationLineEnding::None;
    AnnotationLineEnding endLineEnding = AnnotationLineEnding::None;
    QByteArray iconName;                ///< Name of the icon (Comment, Note, ..., Approved, ...)
    bool isOpen = false;
    PDFFreeTextStyle freeTextStyle;     ///< Font, size, text color and alignment of a text box
    bool hasCalloutLine = false;        ///< The text box has a callout line (read only)
    AnnotationLineEnding calloutLineEnding = AnnotationLineEnding::None;
    PDFReal leaderLineLength = 0.0;
    PDFReal leaderLineExtension = 0.0;
    bool isCaptionShown = false;
    bool isCaptionAbove = false;        ///< CP /Top (else /Inline)
    LinkHighlightMode highlightMode = LinkHighlightMode::Invert;
    PDFDestination destination;         ///< Destination of a link (a GoTo action or Dest)
    bool isDestinationEditable = false; ///< The link goes to a destination of the document (read only)
    QString uri;                        ///< Address of a web link (read only)
    QDateTime creationDate;             ///< Read only
    QDateTime modifiedDate;             ///< Read only
};

/// Reads and writes the properties of an annotation (step 4 of the new
/// application, E10). The dictionary of the annotation is untrusted: a missing,
/// damaged or hostile entry is read as its default and every written value is
/// bounded. Only the fields of the mask are written - the other entries of the
/// annotation are kept as they are - and the appearance stream is created
/// again (PDFDocumentBuilder::updateAnnotationAppearanceStreams).
class PDF4QTLIBCORESHARED_EXPORT PDFAnnotationPropertyEditor
{
public:
    PDFAnnotationPropertyEditor() = delete;

    /// Properties of the annotation; an invalid reference or an object, which is
    /// not a dictionary, gives the properties of the type Invalid without fields
    static PDFAnnotationProperties read(const PDFObjectStorage* storage, PDFObjectReference annotation);

    /// Properties, which an annotation of the type has
    /// \param type Type of the annotation
    /// \param hasCalloutLine A text box has a callout line
    static PDFAnnotationProperties::Fields getSupportedFields(AnnotationType type, bool hasCalloutLine);

    /// Properties, in which \p first and \p second differ (supported fields of
    /// \p first only); used by the dialog to write only what the user has changed
    static PDFAnnotationProperties::Fields getChangedFields(const PDFAnnotationProperties& first, const PDFAnnotationProperties& second);

    /// Bounds every value (texts, numbers, colors, names, the dash pattern)
    static PDFAnnotationProperties bound(PDFAnnotationProperties properties);

    /// Bounds the style of a text box - the one rule for every untrusted source (the
    /// dictionary of an annotation, the stored style of the tool, the dialogs): the family
    /// without the null characters and trimmed, an empty one or one longer than
    /// MAXIMUM_FONT_FAMILY_LENGTH is Helvetica (a cut family would be another font); the
    /// size MINIMUM_FONT_SIZE - MAXIMUM_FONT_SIZE (not finite = 10); an invalid text color
    /// is black; exactly one of left / center / right, anything else (none, more of them,
    /// justify) is left; the text is always at the top
    static PDFFreeTextStyle boundFreeTextStyle(PDFFreeTextStyle style);

    /// Writes the fields of \p fields (intersected with the supported fields of
    /// the annotation) and creates the appearance stream again.
    /// \returns false, when nothing has been written (no field, no annotation)
    static bool write(PDFDocumentBuilder* builder, PDFObjectReference annotation, const PDFAnnotationProperties& properties, PDFAnnotationProperties::Fields fields);

    /// Color of an annotation color array (1 = gray, 3 = RGB, 4 = CMYK, others
    /// and an empty array = invalid color); components are clamped to 0 - 1
    static QColor readColor(const std::vector<PDFReal>& components);

    /// Icon names of the type (sticky note icons, stamps, file attachment icons, sound
    /// icons); empty for a type without an icon
    static std::vector<QByteArray> getIconNames(AnnotationType type);

    /// Blend modes offered by the properties dialog: Normal and the separable modes,
    /// which the viewer draws (PDFBlendModeInfo::isSupportedByQt; Compatible is Normal)
    static std::vector<BlendMode> getOfferedBlendModes();

    /// The name is Normal or a mode of getOfferedBlendModes; a mode of the document,
    /// which the viewer does not draw (Hue, Saturation, Color, Luminosity, Compatible,
    /// an unknown name), is shown as it is and kept unless the user picks another mode
    static bool isBlendModeOffered(const QByteArray& name);

    /// Blend mode of a name of BM (an unknown name is BlendMode::Invalid) and the name of a mode
    static BlendMode getBlendMode(const QByteArray& name);
    static QByteArray getBlendModeName(BlendMode mode);

    /// Name of the blend mode of the entry BM: a name, or the first recognized name of
    /// an array (PDF 1.4 - a reader uses the first mode it knows, otherwise Normal);
    /// an entry of another type or without a recognized name is empty
    static QByteArray readBlendMode(const PDFObjectStorage* storage, const PDFObject& object);

private:
    static PDFObject createColor(const QColor& color);
    static bool isValidName(const QByteArray& name);
};

Q_DECLARE_OPERATORS_FOR_FLAGS(PDFAnnotationProperties::Fields)

}   // namespace pdf

#endif // PDFANNOTATIONPROPERTIES_H
