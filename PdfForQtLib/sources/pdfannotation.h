//    Copyright (C) 2020 Jakub Melka
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
//    along with PDFForQt. If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFANNOTATION_H
#define PDFANNOTATION_H

#include "pdfglobal.h"
#include "pdfobject.h"

namespace pdf
{
class PDFDocument;

enum class AnnotationType
{
    Invalid,
    Text,
    Link,
    FreeText,
    Line,
    Square,
    Circle,
    Polygon,
    Polyline,
    Highlight,
    Underline,
    Squiggly,
    StrikeOut,
    Stamp,
    Caret,
    Ink,
    Popup,
    FileAttachment,
    Sound,
    Moview,
    Widget,
    Screen,
    PrinterMark,
    TrapNet,
    Watermark,
    _3D
};

/// Represents annotation's border. Two main definition exists, one is older,
/// called \p Simple, the other one is defined in BS dictionary of the annotation.
class PDFAnnotationBorder
{
public:
    explicit inline PDFAnnotationBorder() = default;

    enum class Definition
    {
        Invalid,
        Simple,
        BorderStyle
    };

    enum class Style
    {
        Solid,
        Dashed,
        Beveled,
        Inset,
        Underline
    };

    /// Parses the annotation border from the array. If object contains invalid annotation border,
    /// then default annotation border is returned. If object is empty, empty annotation border is returned.
    /// \param document Document
    /// \param object Border object
    static PDFAnnotationBorder parseBorder(const PDFDocument* document, PDFObject object);

    /// Parses the annotation border from the BS dictionary. If object contains invalid annotation border,
    /// then default annotation border is returned. If object is empty, empty annotation border is returned.
    /// \param document Document
    /// \param object Border object
    static PDFAnnotationBorder parseBS(const PDFDocument* document, PDFObject object);

    /// Returns true, if object is correctly defined
    bool isValid() const { return m_definition != Definition::Invalid; }

    Definition getDefinition() const { return m_definition; }
    Style getStyle() const { return m_style; }
    PDFReal getHorizontalCornerRadius() const { return m_hCornerRadius; }
    PDFReal getVerticalCornerRadius() const { return m_vCornerRadius; }
    PDFReal getWidth() const { return m_width; }
    const std::vector<PDFReal>& getDashPattern() const { return m_dashPattern; }

private:
    Definition m_definition = Definition::Invalid;
    Style m_style = Style::Solid;
    PDFReal m_hCornerRadius = 0.0;
    PDFReal m_vCornerRadius = 0.0;
    PDFReal m_width = 1.0;
    std::vector<PDFReal> m_dashPattern;
};

/// Annotation border effect
class PDFAnnotationBorderEffect
{
public:
    explicit inline PDFAnnotationBorderEffect() = default;

    enum class Effect
    {
        None,
        Cloudy
    };

    /// Parses the annotation border effect from the object. If object contains invalid annotation border effect,
    /// then default annotation border effect is returned. If object is empty, also default annotation border effect is returned.
    /// \param document Document
    /// \param object Border effect object
    static PDFAnnotationBorderEffect parse(const PDFDocument* document, PDFObject object);

private:
    Effect m_effect = Effect::None;
    PDFReal m_intensity = 0.0;
};

/// Storage which handles appearance streams of annotations. Appeareance streams are divided
/// to three main categories - normal, rollower and down. Each category can have different
/// states, for example, checkbox can have on/off state. This container can also resolve
/// queries to obtain appropriate appearance stream.
class PDFAppeareanceStreams
{
public:
    explicit inline PDFAppeareanceStreams() = default;

    enum class Appearance
    {
        Normal,
        Rollover,
        Down
    };

    using Key = std::pair<Appearance, QByteArray>;

    /// Parses annotation appearance streams from the object. If object is invalid, then
    /// empty appearance stream is constructed.
    /// \param document Document
    /// \param object Appearance streams object
    static PDFAppeareanceStreams parse(const PDFDocument* document, PDFObject object);

    /// Tries to search for appearance stream for given appearance. If no appearance is found,
    /// then null object is returned.
    /// \param appearance Appearance type
    PDFObject getAppearance(Appearance appearance = Appearance::Normal) const { return getAppearance(appearance, QByteArray()); }

    /// Tries to resolve appearance stream for given appearance and state. If no appearance is found,
    /// then null object is returned.
    /// \param appearance Appearance type
    /// \param state State name
    PDFObject getAppearance(Appearance appearance, const QByteArray& state) const;

private:
    std::map<Key, PDFObject> m_appearanceStreams;
};

/// Base class for all annotation types. Represents PDF annotation object.
/// Annotations are various enhancements to pages graphical representation,
/// such as graphics, text, highlight or multimedia content, such as sounds,
/// videos and 3D annotations.
class PDFAnnotation
{
public:
    explicit PDFAnnotation();
    virtual ~PDFAnnotation() = default;

    enum Flag : uint
    {
        None            = 0x0000,
        Invisible       = 0x0001, ///< If set, do not display unknown annotations using their AP dictionary
        Hidden          = 0x0002, ///< If set, do not display annotation and do not show popup windows (completely hidden)
        Print           = 0x0004, ///< If set, print annotation
        NoZoom          = 0x0008, ///< Do not apply page zoom while displaying annotation rectangle
        NoRotate        = 0x0010, ///< Do not rotate annotation's appearance to match orientation of the page
        NoView          = 0x0020, ///< Do not display annotation on the screen (it still can be printed)
        ReadOnly        = 0x0040, ///< Do not allow interacting with the user (and disallow also mouse interaction)
        Locked          = 0x0080, ///< Do not allow to delete/modify annotation by user
        ToggleNoView    = 0x0100, ///< If set, invert the interpretation of NoView flag
        LockedContents  = 0x0200, ///< Do not allow to modify contents of the annotation
    };

    virtual AnnotationType getType() const = 0;

private:
    QRectF m_rectangle; ///< Annotation rectangle, in page coordinates, "Rect" entry
    QString m_contents; ///< Text to be displayed to the user (or alternate text), "Content" entry
    PDFObjectReference m_pageReference; ///< Reference to annotation's page, "P" entry
    QString m_name; ///< Unique name (in page context) for the annotation, "NM" entry
    QDateTime m_lastModified; ///< Date and time, when annotation was last modified, "M" entry
    QString m_lastModifiedString; ///< Date and time, in text format
    PDFAppeareanceStreams m_appearanceStreams; ///< Appearance streams, "AP" entry
    QByteArray m_appearanceState; ///< Appearance state, "AS" entry
    PDFAnnotationBorder m_annotationBorder; ///< Annotation border, "Border" entry
    std::vector<PDFReal> m_color; ///< Color (for example, title bar of popup window), "C" entry
    PDFInteger m_structParent; ///< Structural parent identifier, "StructParent" entry
    PDFObjectReference m_optionalContentReference; ///< Reference to optional content, "OC" entry
};

}   // namespace pdf

#endif // PDFANNOTATION_H
