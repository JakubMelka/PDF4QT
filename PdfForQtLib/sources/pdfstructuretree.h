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

#ifndef PDFSTRUCTURETREE_H
#define PDFSTRUCTURETREE_H

#include "pdfobject.h"

namespace pdf
{

struct PDFStructureTreeAttributeDefinition;

class PDFStructureTreeAttribute
{
public:

    enum class Owner
    {
        /// Defined for user owner
        User,

        Layout,
        List,
        PrintField,
        Table,

        Artifact,

        XML_1_00,
        HTML_3_20,
        HTML_4_01,
        HTML_5_00,
        OEB_1_00,
        RTF_1_05,
        CSS_1_00,
        CSS_2_00,
        CSS_3_00,
        RDFa_1_10,
        ARIA_1_1,
    };

    enum Attribute
    {
        User,

        // Standard layout attributes
        Placement,
        WritingMode,
        BackgroundColor,
        BorderColor,
        BorderStyle,
        BorderThickness,
        Color,
        Padding,

        // Block element attributes
        SpaceBefore,
        SpaceAfter,
        StartIndent,
        EndIndent,
        TextIndent,
        TextAlign,
        BBox,
        Width,
        Height,
        BlockAlign,
        InlineAlign,
        TBorderStyle,
        TPadding,
        LineHeight,
        BaselineShift,
        TextDecorationType,
        TextDecorationColor,
        TextDecorationThickness,
        ColumnCount,
        ColumnWidths,
        ColumnGap,
        GlyphOrientationVertical,
        RubyAlign,
        RubyPosition,

        // List attributes
        ListNumbering,
        ContinuedList,
        ContinuedFrom,

        // PrintField attributes
        Role,
        Checked,
        Desc,

        // Table attributes
        RowSpan,
        ColSpan,
        Headers,
        Scope,
        Short,

        // Artifact attributes
        Type,
        Subtype,

        LastAttribute
    };

private:
};

class PDFStructureTree
{
public:
    PDFStructureTree();
};

}   // namespace pdf

#endif // PDFSTRUCTURETREE_H
