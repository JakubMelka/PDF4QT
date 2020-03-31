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

#include "pdfannotation.h"
#include "pdfdocument.h"
#include "pdfencoding.h"
#include "pdfpainter.h"
#include "pdfdrawspacecontroller.h"
#include "pdfcms.h"
#include "pdfwidgetutils.h"
#include "pdfpagecontentprocessor.h"

#include <QApplication>

namespace pdf
{

PDFAnnotationBorder PDFAnnotationBorder::parseBorder(const PDFObjectStorage* storage, PDFObject object)
{
    PDFAnnotationBorder result;
    object = storage->getObject(object);

    if (object.isArray())
    {
        const PDFArray* array = object.getArray();
        if (array->getCount() >= 3)
        {
            PDFDocumentDataLoaderDecorator loader(storage);
            result.m_definition = Definition::Simple;
            result.m_hCornerRadius = loader.readNumber(array->getItem(0), 0.0);
            result.m_vCornerRadius = loader.readNumber(array->getItem(1), 0.0);
            result.m_width = loader.readNumber(array->getItem(2), 1.0);

            if (array->getCount() >= 4)
            {
                result.m_dashPattern = loader.readNumberArray(array->getItem(3));
            }
        }
    }

    return result;
}

PDFAnnotationBorder PDFAnnotationBorder::parseBS(const PDFObjectStorage* storage, PDFObject object)
{
    PDFAnnotationBorder result;

    if (const PDFDictionary* dictionary = storage->getDictionaryFromObject(object))
    {
        PDFDocumentDataLoaderDecorator loader(storage);
        result.m_definition = Definition::BorderStyle;
        result.m_width = loader.readNumberFromDictionary(dictionary, "W", 1.0);

        constexpr const std::array<std::pair<const char*, Style>, 6> styles = {
            std::pair<const char*, Style>{ "S", Style::Solid },
            std::pair<const char*, Style>{ "D", Style::Dashed },
            std::pair<const char*, Style>{ "B", Style::Beveled },
            std::pair<const char*, Style>{ "I", Style::Inset },
            std::pair<const char*, Style>{ "U", Style::Underline }
        };

        result.m_style = loader.readEnumByName(dictionary->get("S"), styles.begin(), styles.end(), Style::Solid);
    }

    return result;
}

PDFAnnotationBorderEffect PDFAnnotationBorderEffect::parse(const PDFObjectStorage* storage, PDFObject object)
{
    PDFAnnotationBorderEffect result;

    if (const PDFDictionary* dictionary = storage->getDictionaryFromObject(object))
    {
        PDFDocumentDataLoaderDecorator loader(storage);
        result.m_intensity = loader.readNumberFromDictionary(dictionary, "I", 0.0);

        constexpr const std::array<std::pair<const char*, Effect>, 2> effects = {
            std::pair<const char*, Effect>{ "S", Effect::None },
            std::pair<const char*, Effect>{ "C", Effect::Cloudy }
        };

        result.m_effect = loader.readEnumByName(dictionary->get("S"), effects.begin(), effects.end(), Effect::None);
    }

    return result;
}

PDFAppeareanceStreams PDFAppeareanceStreams::parse(const PDFObjectStorage* storage, PDFObject object)
{
    PDFAppeareanceStreams result;

    auto processSubdicitonary = [&result, storage](Appearance appearance, PDFObject subdictionaryObject)
    {
        subdictionaryObject = storage->getObject(subdictionaryObject);
        if (subdictionaryObject.isDictionary())
        {
            const PDFDictionary* subdictionary = storage->getDictionaryFromObject(subdictionaryObject);
            for (size_t i = 0; i < subdictionary->getCount(); ++i)
            {
                result.m_appearanceStreams[std::make_pair(appearance, subdictionary->getKey(i))] = subdictionary->getValue(i);
            }
        }
        else if (!subdictionaryObject.isNull())
        {
            result.m_appearanceStreams[std::make_pair(appearance, QByteArray())] = subdictionaryObject;
        }
    };

    if (const PDFDictionary* dictionary = storage->getDictionaryFromObject(object))
    {
        processSubdicitonary(Appearance::Normal, dictionary->get("N"));
        processSubdicitonary(Appearance::Rollover, dictionary->get("R"));
        processSubdicitonary(Appearance::Down, dictionary->get("D"));
    }

    return result;
}

PDFObject PDFAppeareanceStreams::getAppearance(Appearance appearance, const QByteArray& state) const
{
    Key key(appearance, state);

    auto it = m_appearanceStreams.find(key);
    if (it == m_appearanceStreams.cend() && appearance != Appearance::Normal)
    {
        key.first = Appearance::Normal;
        it = m_appearanceStreams.find(key);
    }
    if (it == m_appearanceStreams.cend() && !state.isEmpty())
    {
        key.second = QByteArray();
        it = m_appearanceStreams.find(key);
    }

    if (it != m_appearanceStreams.cend())
    {
        return it->second;
    }

    return PDFObject();
}

PDFAnnotation::PDFAnnotation() :
    m_flags(),
    m_structParent(0)
{

}

void PDFAnnotation::draw(AnnotationDrawParameters& parameters) const
{
    Q_UNUSED(parameters);
}

std::vector<PDFAppeareanceStreams::Key> PDFAnnotation::getDrawKeys() const
{
    return { PDFAppeareanceStreams::Key{ PDFAppeareanceStreams::Appearance::Normal, QByteArray() } };
}

PDFAnnotationPtr PDFAnnotation::parse(const PDFObjectStorage* storage, PDFObject object)
{
    PDFAnnotationPtr result;

    const PDFDictionary* dictionary = storage->getDictionaryFromObject(object);
    if (!dictionary)
    {
        return result;
    }

    PDFDocumentDataLoaderDecorator loader(storage);

    QRectF annotationsRectangle = loader.readRectangle(dictionary->get("Rect"), QRectF());

    // Determine type of annotation
    QByteArray subtype = loader.readNameFromDictionary(dictionary, "Subtype");
    if (subtype == "Text")
    {
        PDFTextAnnotation* textAnnotation = new PDFTextAnnotation;
        result.reset(textAnnotation);

        textAnnotation->m_open = loader.readBooleanFromDictionary(dictionary, "Open", false);
        textAnnotation->m_iconName = loader.readNameFromDictionary(dictionary, "Name");
        textAnnotation->m_state = loader.readTextStringFromDictionary(dictionary, "State", "Unmarked");
        textAnnotation->m_stateModel = loader.readTextStringFromDictionary(dictionary, "StateModel", "Marked");
    }
    else if (subtype == "Link")
    {
        PDFLinkAnnotation* linkAnnotation = new PDFLinkAnnotation;
        result.reset(linkAnnotation);

        linkAnnotation->m_action = PDFAction::parse(storage, dictionary->get("A"));
        if (!linkAnnotation->m_action)
        {
            PDFDestination destination = PDFDestination::parse(storage, dictionary->get("Dest"));
            linkAnnotation->m_action.reset(new PDFActionGoTo(destination));
        }
        linkAnnotation->m_previousAction = PDFAction::parse(storage, dictionary->get("PA"));

        constexpr const std::array<std::pair<const char*, LinkHighlightMode>, 4> highlightMode = {
            std::pair<const char*, LinkHighlightMode>{ "N", LinkHighlightMode::None },
            std::pair<const char*, LinkHighlightMode>{ "I", LinkHighlightMode::Invert },
            std::pair<const char*, LinkHighlightMode>{ "O", LinkHighlightMode::Outline },
            std::pair<const char*, LinkHighlightMode>{ "P", LinkHighlightMode::Push }
        };

        linkAnnotation->m_highlightMode = loader.readEnumByName(dictionary->get("H"), highlightMode.begin(), highlightMode.end(), LinkHighlightMode::Invert);
        linkAnnotation->m_activationRegion = parseQuadrilaterals(storage, dictionary->get("QuadPoints"), annotationsRectangle);
    }
    else if (subtype == "FreeText")
    {
        PDFFreeTextAnnotation* freeTextAnnotation = new PDFFreeTextAnnotation;
        result.reset(freeTextAnnotation);

        constexpr const std::array<std::pair<const char*, PDFFreeTextAnnotation::Intent>, 2> intents = {
            std::pair<const char*, PDFFreeTextAnnotation::Intent>{ "FreeTextCallout", PDFFreeTextAnnotation::Intent::Callout },
            std::pair<const char*, PDFFreeTextAnnotation::Intent>{ "FreeTextTypeWriter", PDFFreeTextAnnotation::Intent::TypeWriter }
        };

        freeTextAnnotation->m_defaultAppearance = loader.readStringFromDictionary(dictionary, "DA");
        freeTextAnnotation->m_justification = static_cast<PDFFreeTextAnnotation::Justification>(loader.readIntegerFromDictionary(dictionary, "Q", 0));
        freeTextAnnotation->m_defaultStyleString = loader.readTextStringFromDictionary(dictionary, "DS", QString());
        freeTextAnnotation->m_calloutLine = PDFAnnotationCalloutLine::parse(storage, dictionary->get("CL"));
        freeTextAnnotation->m_intent = loader.readEnumByName(dictionary->get("IT"), intents.begin(), intents.end(), PDFFreeTextAnnotation::Intent::None);
        freeTextAnnotation->m_effect = PDFAnnotationBorderEffect::parse(storage, dictionary->get("BE"));

        std::vector<PDFReal> differenceRectangle = loader.readNumberArrayFromDictionary(dictionary, "RD");
        if (differenceRectangle.size() == 4)
        {
            freeTextAnnotation->m_textRectangle = annotationsRectangle.adjusted(differenceRectangle[0], differenceRectangle[1], -differenceRectangle[2], -differenceRectangle[3]);
            if (!freeTextAnnotation->m_textRectangle.isValid())
            {
                freeTextAnnotation->m_textRectangle = QRectF();
            }
        }

        std::vector<QByteArray> lineEndings = loader.readNameArrayFromDictionary(dictionary, "LE");
        if (lineEndings.size() == 2)
        {
            freeTextAnnotation->m_startLineEnding = convertNameToLineEnding(lineEndings[0]);
            freeTextAnnotation->m_endLineEnding = convertNameToLineEnding(lineEndings[1]);
        }
    }
    else if (subtype == "Line")
    {
        PDFLineAnnotation* lineAnnotation = new PDFLineAnnotation;
        result.reset(lineAnnotation);

        std::vector<PDFReal> line = loader.readNumberArrayFromDictionary(dictionary, "L");
        if (line.size() == 4)
        {
            lineAnnotation->m_line = QLineF(line[0], line[1], line[2], line[3]);
        }

        std::vector<QByteArray> lineEndings = loader.readNameArrayFromDictionary(dictionary, "LE");
        if (lineEndings.size() == 2)
        {
            lineAnnotation->m_startLineEnding = convertNameToLineEnding(lineEndings[0]);
            lineAnnotation->m_endLineEnding = convertNameToLineEnding(lineEndings[1]);
        }

        lineAnnotation->m_interiorColor = loader.readNumberArrayFromDictionary(dictionary, "IC");
        lineAnnotation->m_leaderLineLength = loader.readNumberFromDictionary(dictionary, "LL", 0.0);
        lineAnnotation->m_leaderLineExtension = loader.readNumberFromDictionary(dictionary, "LLE", 0.0);
        lineAnnotation->m_leaderLineOffset = loader.readNumberFromDictionary(dictionary, "LLO", 0.0);
        lineAnnotation->m_captionRendered = loader.readBooleanFromDictionary(dictionary, "Cap", false);
        lineAnnotation->m_intent = (loader.readNameFromDictionary(dictionary, "IT") == "LineDimension") ? PDFLineAnnotation::Intent::Dimension : PDFLineAnnotation::Intent::Arrow;
        lineAnnotation->m_captionPosition = (loader.readNameFromDictionary(dictionary, "CP") == "Top") ? PDFLineAnnotation::CaptionPosition::Top : PDFLineAnnotation::CaptionPosition::Inline;
        lineAnnotation->m_measureDictionary = storage->getObject(dictionary->get("Measure"));

        std::vector<PDFReal> captionOffset = loader.readNumberArrayFromDictionary(dictionary, "CO");
        if (captionOffset.size() == 2)
        {
            lineAnnotation->m_captionOffset == QPointF(captionOffset[0], captionOffset[1]);
        }
    }
    else if (subtype == "Square" || subtype == "Circle")
    {
        PDFSimpleGeometryAnnotation* annotation = new PDFSimpleGeometryAnnotation((subtype == "Square") ? AnnotationType::Square : AnnotationType::Circle);
        result.reset(annotation);

        annotation->m_interiorColor = loader.readNumberArrayFromDictionary(dictionary, "IC");
        annotation->m_effect = PDFAnnotationBorderEffect::parse(storage, dictionary->get("BE"));

        std::vector<PDFReal> differenceRectangle = loader.readNumberArrayFromDictionary(dictionary, "RD");
        if (differenceRectangle.size() == 4)
        {
            annotation->m_geometryRectangle = annotationsRectangle.adjusted(differenceRectangle[0], differenceRectangle[1], -differenceRectangle[2], -differenceRectangle[3]);
            if (!annotation->m_geometryRectangle.isValid())
            {
                annotation->m_geometryRectangle = QRectF();
            }
        }
    }
    else if (subtype == "Polygon" || subtype == "PolyLine")
    {
        PDFPolygonalGeometryAnnotation* annotation = new PDFPolygonalGeometryAnnotation((subtype == "Polygon") ? AnnotationType::Polygon : AnnotationType::Polyline);
        result.reset(annotation);

        std::vector<PDFReal> vertices = loader.readNumberArrayFromDictionary(dictionary, "Vertices");
        const size_t count = vertices.size() / 2;
        annotation->m_vertices.reserve(count);
        for (size_t i = 0; i < count; ++i)
        {
            annotation->m_vertices.emplace_back(vertices[2 * i], vertices[2 * i + 1]);
        }

        std::vector<QByteArray> lineEndings = loader.readNameArrayFromDictionary(dictionary, "LE");
        if (lineEndings.size() == 2)
        {
            annotation->m_startLineEnding = convertNameToLineEnding(lineEndings[0]);
            annotation->m_endLineEnding = convertNameToLineEnding(lineEndings[1]);
        }

        annotation->m_interiorColor = loader.readNumberArrayFromDictionary(dictionary, "IC");
        annotation->m_effect = PDFAnnotationBorderEffect::parse(storage, dictionary->get("BE"));

        constexpr const std::array<std::pair<const char*, PDFPolygonalGeometryAnnotation::Intent>, 3> intents = {
            std::pair<const char*, PDFPolygonalGeometryAnnotation::Intent>{ "PolygonCloud", PDFPolygonalGeometryAnnotation::Intent::Cloud },
            std::pair<const char*, PDFPolygonalGeometryAnnotation::Intent>{ "PolyLineDimension", PDFPolygonalGeometryAnnotation::Intent::Dimension },
            std::pair<const char*, PDFPolygonalGeometryAnnotation::Intent>{ "PolygonDimension", PDFPolygonalGeometryAnnotation::Intent::Dimension }
        };

        annotation->m_intent = loader.readEnumByName(dictionary->get("IT"), intents.begin(), intents.end(), PDFPolygonalGeometryAnnotation::Intent::None);
        annotation->m_measure = storage->getObject(dictionary->get("Measure"));
    }
    else if (subtype == "Highlight" ||
             subtype == "Underline" ||
             subtype == "Squiggly" ||
             subtype == "StrikeOut")
    {
        AnnotationType type = AnnotationType::Highlight;
        if (subtype == "Underline")
        {
            type = AnnotationType::Underline;
        }
        else if (subtype == "Squiggly")
        {
            type = AnnotationType::Squiggly;
        }
        else if (subtype == "StrikeOut")
        {
            type = AnnotationType::StrikeOut;
        }

        PDFHighlightAnnotation* annotation = new PDFHighlightAnnotation(type);
        result.reset(annotation);

        annotation->m_highlightArea = parseQuadrilaterals(storage, dictionary->get("QuadPoints"), annotationsRectangle);
    }
    else if (subtype == "Caret")
    {
        PDFCaretAnnotation* annotation = new PDFCaretAnnotation();
        result.reset(annotation);

        std::vector<PDFReal> differenceRectangle = loader.readNumberArrayFromDictionary(dictionary, "RD");
        if (differenceRectangle.size() == 4)
        {
            annotation->m_caretRectangle = annotationsRectangle.adjusted(differenceRectangle[0], differenceRectangle[1], -differenceRectangle[2], -differenceRectangle[3]);
            if (!annotation->m_caretRectangle.isValid())
            {
                annotation->m_caretRectangle = QRectF();
            }
        }

        annotation->m_symbol = (loader.readNameFromDictionary(dictionary, "Sy") == "P") ? PDFCaretAnnotation::Symbol::Paragraph : PDFCaretAnnotation::Symbol::None;
    }
    else if (subtype == "Stamp")
    {
        PDFStampAnnotation* annotation = new PDFStampAnnotation();
        result.reset(annotation);

        constexpr const std::array<std::pair<const char*, PDFStampAnnotation::Stamp>, 14> stamps = {
            std::pair<const char*, PDFStampAnnotation::Stamp>{ "Approved", PDFStampAnnotation::Stamp::Approved },
            std::pair<const char*, PDFStampAnnotation::Stamp>{ "AsIs", PDFStampAnnotation::Stamp::AsIs },
            std::pair<const char*, PDFStampAnnotation::Stamp>{ "Confidential", PDFStampAnnotation::Stamp::Confidential },
            std::pair<const char*, PDFStampAnnotation::Stamp>{ "Departmental", PDFStampAnnotation::Stamp::Departmental },
            std::pair<const char*, PDFStampAnnotation::Stamp>{ "Draft", PDFStampAnnotation::Stamp::Draft },
            std::pair<const char*, PDFStampAnnotation::Stamp>{ "Experimental", PDFStampAnnotation::Stamp::Experimental },
            std::pair<const char*, PDFStampAnnotation::Stamp>{ "Expired", PDFStampAnnotation::Stamp::Expired },
            std::pair<const char*, PDFStampAnnotation::Stamp>{ "Final", PDFStampAnnotation::Stamp::Final },
            std::pair<const char*, PDFStampAnnotation::Stamp>{ "ForComment", PDFStampAnnotation::Stamp::ForComment },
            std::pair<const char*, PDFStampAnnotation::Stamp>{ "ForPublicRelease", PDFStampAnnotation::Stamp::ForPublicRelease },
            std::pair<const char*, PDFStampAnnotation::Stamp>{ "NotApproved", PDFStampAnnotation::Stamp::NotApproved },
            std::pair<const char*, PDFStampAnnotation::Stamp>{ "NotForPublicRelease", PDFStampAnnotation::Stamp::NotForPublicRelease },
            std::pair<const char*, PDFStampAnnotation::Stamp>{ "Sold", PDFStampAnnotation::Stamp::Sold },
            std::pair<const char*, PDFStampAnnotation::Stamp>{ "TopSecret", PDFStampAnnotation::Stamp::TopSecret }
        };

        annotation->m_stamp = loader.readEnumByName(dictionary->get("Name"), stamps.begin(), stamps.end(), PDFStampAnnotation::Stamp::Draft);
    }
    else if (subtype == "Ink")
    {
        PDFInkAnnotation* annotation = new PDFInkAnnotation();
        result.reset(annotation);

        PDFObject inkList = storage->getObject(dictionary->get("InkList"));
        if (inkList.isArray())
        {
            const PDFArray* inkListArray = inkList.getArray();
            for (size_t i = 0, count = inkListArray->getCount(); i < count; ++i)
            {
                std::vector<PDFReal> points = loader.readNumberArray(inkListArray->getItem(i));
                const size_t pointCount = points.size() / 2;

                for (size_t j = 0; j < pointCount; ++j)
                {
                    QPointF point(points[j * 2], points[j * 2 + 1]);

                    if (j == 0)
                    {
                        annotation->m_inkPath.moveTo(point);
                    }
                    else
                    {
                        annotation->m_inkPath.lineTo(point);
                    }
                }
                annotation->m_inkPath.closeSubpath();
            }
        }
    }
    else if (subtype == "Popup")
    {
        PDFPopupAnnotation* annotation = new PDFPopupAnnotation();
        result.reset(annotation);

        annotation->m_opened = loader.readBooleanFromDictionary(dictionary, "Open", false);
    }
    else if (subtype == "FileAttachment")
    {
        PDFFileAttachmentAnnotation* annotation = new PDFFileAttachmentAnnotation();
        result.reset(annotation);

        annotation->m_fileSpecification = PDFFileSpecification::parse(storage, dictionary->get("FS"));

        constexpr const std::array<std::pair<const char*, PDFFileAttachmentAnnotation::Icon>, 4> icons = {
            std::pair<const char*, PDFFileAttachmentAnnotation::Icon>{ "Graph", PDFFileAttachmentAnnotation::Icon::Graph },
            std::pair<const char*, PDFFileAttachmentAnnotation::Icon>{ "Paperclip", PDFFileAttachmentAnnotation::Icon::Paperclip },
            std::pair<const char*, PDFFileAttachmentAnnotation::Icon>{ "PushPin", PDFFileAttachmentAnnotation::Icon::PushPin },
            std::pair<const char*, PDFFileAttachmentAnnotation::Icon>{ "Tag", PDFFileAttachmentAnnotation::Icon::Tag }
        };

        annotation->m_icon = loader.readEnumByName(dictionary->get("Name"), icons.begin(), icons.end(), PDFFileAttachmentAnnotation::Icon::PushPin);
    }
    else if (subtype == "Sound")
    {
        PDFSoundAnnotation* annotation = new PDFSoundAnnotation();
        result.reset(annotation);

        annotation->m_sound = PDFSound::parse(storage, dictionary->get("Sound"));

        constexpr const std::array<std::pair<const char*, PDFSoundAnnotation::Icon>, 2> icons = {
            std::pair<const char*, PDFSoundAnnotation::Icon>{ "Speaker", PDFSoundAnnotation::Icon::Speaker },
            std::pair<const char*, PDFSoundAnnotation::Icon>{ "Mic", PDFSoundAnnotation::Icon::Microphone }
        };

        annotation->m_icon = loader.readEnumByName(dictionary->get("Name"), icons.begin(), icons.end(), PDFSoundAnnotation::Icon::Speaker);
    }
    else if (subtype == "Movie")
    {
        PDFMovieAnnotation* annotation = new PDFMovieAnnotation();
        result.reset(annotation);

        annotation->m_movieTitle = loader.readStringFromDictionary(dictionary, "T");
        annotation->m_movie = PDFMovie::parse(storage, dictionary->get("Movie"));

        PDFObject activation = storage->getObject(dictionary->get("A"));
        if (activation.isBool())
        {
            annotation->m_playMovie = activation.getBool();
        }
        else if (activation.isDictionary())
        {
            annotation->m_playMovie = true;
            annotation->m_movieActivation = PDFMovieActivation::parse(storage, activation);
        }
    }
    else if (subtype == "Screen")
    {
        PDFScreenAnnotation* annotation = new PDFScreenAnnotation();
        result.reset(annotation);

        annotation->m_screenTitle = loader.readTextStringFromDictionary(dictionary, "T", QString());
        annotation->m_appearanceCharacteristics = PDFAnnotationAppearanceCharacteristics::parse(storage, dictionary->get("MK"));
        annotation->m_action = PDFAction::parse(storage, dictionary->get("A"));
        annotation->m_additionalActions = PDFAnnotationAdditionalActions::parse(storage, dictionary->get("AA"));
    }
    else if (subtype == "Widget")
    {
        PDFWidgetAnnotation* annotation = new PDFWidgetAnnotation();
        result.reset(annotation);

        constexpr const std::array<std::pair<const char*, PDFWidgetAnnotation::HighlightMode>, 5> highlightModes = {
            std::pair<const char*, PDFWidgetAnnotation::HighlightMode>{ "N", PDFWidgetAnnotation::HighlightMode::None },
            std::pair<const char*, PDFWidgetAnnotation::HighlightMode>{ "I", PDFWidgetAnnotation::HighlightMode::Invert },
            std::pair<const char*, PDFWidgetAnnotation::HighlightMode>{ "O", PDFWidgetAnnotation::HighlightMode::Outline },
            std::pair<const char*, PDFWidgetAnnotation::HighlightMode>{ "P", PDFWidgetAnnotation::HighlightMode::Push },
            std::pair<const char*, PDFWidgetAnnotation::HighlightMode>{ "T", PDFWidgetAnnotation::HighlightMode::Toggle }
        };

        annotation->m_highlightMode = loader.readEnumByName(dictionary->get("H"), highlightModes.begin(), highlightModes.end(), PDFWidgetAnnotation::HighlightMode::Invert);
        annotation->m_appearanceCharacteristics = PDFAnnotationAppearanceCharacteristics::parse(storage, dictionary->get("MK"));
        annotation->m_action = PDFAction::parse(storage, dictionary->get("A"));
        annotation->m_additionalActions = PDFAnnotationAdditionalActions::parse(storage, dictionary->get("AA"));
    }
    else if (subtype == "PrinterMark")
    {
        PDFPrinterMarkAnnotation* annotation = new PDFPrinterMarkAnnotation();
        result.reset(annotation);
    }
    else if (subtype == "TrapNet")
    {
        PDFTrapNetworkAnnotation* annotation = new PDFTrapNetworkAnnotation();
        result.reset(annotation);
    }
    else if (subtype == "Watermark")
    {
        PDFWatermarkAnnotation* annotation = new PDFWatermarkAnnotation();
        result.reset(annotation);

        if (const PDFDictionary* fixedPrintDictionary = storage->getDictionaryFromObject(dictionary->get("FixedPrint")))
        {
            annotation->m_matrix = loader.readMatrixFromDictionary(fixedPrintDictionary, "Matrix", QMatrix());
            annotation->m_relativeHorizontalOffset = loader.readNumberFromDictionary(fixedPrintDictionary, "H", 0.0);
            annotation->m_relativeVerticalOffset = loader.readNumberFromDictionary(fixedPrintDictionary, "V", 0.0);
        }
    }

    if (!result)
    {
        // Invalid annotation type
        return result;
    }

    // Load common data for annotation
    result->m_rectangle = annotationsRectangle;
    result->m_contents = loader.readTextStringFromDictionary(dictionary, "Contents", QString());
    result->m_pageReference = loader.readReferenceFromDictionary(dictionary, "P");
    result->m_name = loader.readTextStringFromDictionary(dictionary, "NM", QString());

    QByteArray string = loader.readStringFromDictionary(dictionary, "M");
    result->m_lastModified = PDFEncoding::convertToDateTime(string);
    if (!result->m_lastModified.isValid())
    {
        result->m_lastModifiedString = loader.readTextStringFromDictionary(dictionary, "M", QString());
    }

    result->m_flags = Flags(loader.readIntegerFromDictionary(dictionary, "F", 0));
    result->m_appearanceStreams = PDFAppeareanceStreams::parse(storage, dictionary->get("AP"));
    result->m_appearanceState = loader.readNameFromDictionary(dictionary, "AS");

    result->m_annotationBorder = PDFAnnotationBorder::parseBS(storage, dictionary->get("BS"));
    if (!result->m_annotationBorder.isValid())
    {
        result->m_annotationBorder = PDFAnnotationBorder::parseBorder(storage, dictionary->get("Border"));
    }
    result->m_color = loader.readNumberArrayFromDictionary(dictionary, "C");
    result->m_structParent = loader.readIntegerFromDictionary(dictionary, "StructParent", 0);
    result->m_optionalContentReference = loader.readReferenceFromDictionary(dictionary, "OC");

    if (PDFMarkupAnnotation* markupAnnotation = result->asMarkupAnnotation())
    {
        markupAnnotation->m_windowTitle = loader.readTextStringFromDictionary(dictionary, "T", QString());
        markupAnnotation->m_popupAnnotation = loader.readReferenceFromDictionary(dictionary, "Popup");
        markupAnnotation->m_opacity = loader.readNumberFromDictionary(dictionary, "CA", 1.0);
        markupAnnotation->m_richTextString = loader.readTextStringFromDictionary(dictionary, "RC", QString());
        markupAnnotation->m_creationDate = PDFEncoding::convertToDateTime(loader.readStringFromDictionary(dictionary, "CreationDate"));
        markupAnnotation->m_inReplyTo = loader.readReferenceFromDictionary(dictionary, "IRT");
        markupAnnotation->m_subject = loader.readTextStringFromDictionary(dictionary, "Subj", QString());
        markupAnnotation->m_replyType = (loader.readNameFromDictionary(dictionary, "RT") == "Group") ? PDFMarkupAnnotation::ReplyType::Group : PDFMarkupAnnotation::ReplyType::Reply;
        markupAnnotation->m_intent = loader.readNameFromDictionary(dictionary, "IT");
        markupAnnotation->m_externalData = storage->getObject(dictionary->get("ExData"));
    }

    return result;
}

PDFAnnotationQuadrilaterals PDFAnnotation::parseQuadrilaterals(const PDFObjectStorage* storage, PDFObject quadrilateralsObject, const QRectF annotationRect)
{
    QPainterPath path;
    std::vector<QLineF> underlines;

    PDFDocumentDataLoaderDecorator loader(storage);
    std::vector<PDFReal> points = loader.readNumberArray(quadrilateralsObject);
    const size_t quadrilateralCount = points.size() % 8;
    path.reserve(int(quadrilateralCount) + 5);
    underlines.reserve(quadrilateralCount);
    for (size_t i = 0; i < quadrilateralCount; ++i)
    {
        const size_t offset = i * 8;
        QPointF p1(points[offset + 0], points[offset + 1]);
        QPointF p2(points[offset + 2], points[offset + 3]);
        QPointF p3(points[offset + 4], points[offset + 5]);
        QPointF p4(points[offset + 6], points[offset + 7]);

        path.moveTo(p1);
        path.lineTo(p2);
        path.lineTo(p3);
        path.lineTo(p4);
        path.lineTo(p1);
        path.closeSubpath();

        underlines.emplace_back(p1, p2);
    }

    if (path.isEmpty() && annotationRect.isValid())
    {
        // Jakub Melka: we are using points at the top, because PDF has inverted y axis
        // against the Qt's y axis.
        path.addRect(annotationRect);
        underlines.emplace_back(annotationRect.topLeft(), annotationRect.topRight());
    }

    return PDFAnnotationQuadrilaterals(qMove(path), qMove(underlines));
}

AnnotationLineEnding PDFAnnotation::convertNameToLineEnding(const QByteArray& name)
{
    constexpr const std::array<std::pair<AnnotationLineEnding, const char*>, 10> lineEndings = {
        std::pair<AnnotationLineEnding, const char*>{ AnnotationLineEnding::None, "None" },
        std::pair<AnnotationLineEnding, const char*>{ AnnotationLineEnding::Square, "Square" },
        std::pair<AnnotationLineEnding, const char*>{ AnnotationLineEnding::Circle, "Circle" },
        std::pair<AnnotationLineEnding, const char*>{ AnnotationLineEnding::Diamond, "Diamond" },
        std::pair<AnnotationLineEnding, const char*>{ AnnotationLineEnding::OpenArrow, "OpenArrow" },
        std::pair<AnnotationLineEnding, const char*>{ AnnotationLineEnding::ClosedArrow, "ClosedArrow" },
        std::pair<AnnotationLineEnding, const char*>{ AnnotationLineEnding::Butt, "Butt" },
        std::pair<AnnotationLineEnding, const char*>{ AnnotationLineEnding::ROpenArrow, "ROpenArrow" },
        std::pair<AnnotationLineEnding, const char*>{ AnnotationLineEnding::RClosedArrow, "RClosedArrow" },
        std::pair<AnnotationLineEnding, const char*>{ AnnotationLineEnding::Slash, "Slash" }
    };

    auto it = std::find_if(lineEndings.cbegin(), lineEndings.cend(), [&name](const auto& item) { return name == item.second; });
    if (it != lineEndings.cend())
    {
        return it->first;
    }

    return AnnotationLineEnding::None;
}

QColor PDFAnnotation::getStrokeColor() const
{
    return getDrawColorFromAnnotationColor(getColor());
}

QColor PDFAnnotation::getFillColor() const
{
    return QColor();
}

QColor PDFAnnotation::getDrawColorFromAnnotationColor(const std::vector<PDFReal>& color) const
{
    switch (color.size())
    {
        case 1:
        {
            const PDFReal gray = color.back();
            return QColor::fromRgbF(gray, gray, gray, 1.0);
        }

        case 3:
        {
            const PDFReal r = color[0];
            const PDFReal g = color[1];
            const PDFReal b = color[2];
            return QColor::fromRgbF(r, g, b, 1.0);
        }

        case 4:
        {
            const PDFReal c = color[0];
            const PDFReal m = color[1];
            const PDFReal y = color[2];
            const PDFReal k = color[3];
            return QColor::fromCmykF(c, m, y, k, 1.0);
        }

        default:
            break;
    }

    return QColor(Qt::black);
}

QPen PDFAnnotation::getPen() const
{
    QColor strokeColor = getStrokeColor();
    const PDFAnnotationBorder& border = getBorder();

    if (qFuzzyIsNull(border.getWidth()))
    {
        // No border is drawn
        return Qt::NoPen;
    }

    QPen pen(strokeColor);
    pen.setWidthF(border.getWidth());

    if (!border.getDashPattern().empty())
    {
        PDFLineDashPattern lineDashPattern(border.getDashPattern(), 0.0);
        pen.setStyle(Qt::CustomDashLine);
        pen.setDashPattern(QVector<PDFReal>::fromStdVector(lineDashPattern.getDashArray()));
        pen.setDashOffset(lineDashPattern.getDashOffset());
    }

    return pen;
}

QBrush PDFAnnotation::getBrush() const
{
    QColor color = getFillColor();
    if (color.isValid())
    {
        return QBrush(color, Qt::SolidPattern);
    }

    return QBrush(Qt::NoBrush);
}

PDFAnnotationCalloutLine PDFAnnotationCalloutLine::parse(const PDFObjectStorage* storage, PDFObject object)
{
    PDFDocumentDataLoaderDecorator loader(storage);
    std::vector<PDFReal> points = loader.readNumberArray(object);

    switch (points.size())
    {
        case 4:
            return PDFAnnotationCalloutLine(QPointF(points[0], points[1]), QPointF(points[2], points[3]));

        case 6:
            return PDFAnnotationCalloutLine(QPointF(points[0], points[1]), QPointF(points[2], points[3]), QPointF(points[4], points[5]));

        default:
            break;
    }

    return PDFAnnotationCalloutLine();
}

PDFAnnotationAppearanceCharacteristics PDFAnnotationAppearanceCharacteristics::parse(const PDFObjectStorage* storage, PDFObject object)
{
    PDFAnnotationAppearanceCharacteristics result;

    if (const PDFDictionary* dictionary = storage->getDictionaryFromObject(object))
    {
        PDFDocumentDataLoaderDecorator loader(storage);

        result.m_rotation = loader.readIntegerFromDictionary(dictionary, "R", 0);
        result.m_borderColor = loader.readNumberArrayFromDictionary(dictionary, "BC");
        result.m_backgroundColor = loader.readNumberArrayFromDictionary(dictionary, "BG");
        result.m_normalCaption = loader.readTextStringFromDictionary(dictionary, "CA", QString());
        result.m_rolloverCaption = loader.readTextStringFromDictionary(dictionary, "RC", QString());
        result.m_downCaption = loader.readTextStringFromDictionary(dictionary, "AC", QString());
        result.m_normalIcon = storage->getObject(dictionary->get("I"));
        result.m_rolloverIcon = storage->getObject(dictionary->get("RI"));
        result.m_downIcon = storage->getObject(dictionary->get("IX"));
        result.m_iconFit = PDFAnnotationIconFitInfo::parse(storage, dictionary->get("IF"));
        result.m_pushButtonMode = static_cast<PushButtonMode>(loader.readIntegerFromDictionary(dictionary, "TP", PDFInteger(PDFAnnotationAppearanceCharacteristics::PushButtonMode::NoIcon)));
    }
    return result;
}

PDFAnnotationIconFitInfo PDFAnnotationIconFitInfo::parse(const PDFObjectStorage* storage, PDFObject object)
{
    PDFAnnotationIconFitInfo info;

    if (const PDFDictionary* dictionary = storage->getDictionaryFromObject(object))
    {
        PDFDocumentDataLoaderDecorator loader(storage);

        constexpr const std::array<std::pair<const char*, PDFAnnotationIconFitInfo::ScaleCondition>, 4> scaleConditions = {
            std::pair<const char*, PDFAnnotationIconFitInfo::ScaleCondition>{ "A", PDFAnnotationIconFitInfo::ScaleCondition::Always },
            std::pair<const char*, PDFAnnotationIconFitInfo::ScaleCondition>{ "B", PDFAnnotationIconFitInfo::ScaleCondition::ScaleBigger },
            std::pair<const char*, PDFAnnotationIconFitInfo::ScaleCondition>{ "S", PDFAnnotationIconFitInfo::ScaleCondition::ScaleSmaller },
            std::pair<const char*, PDFAnnotationIconFitInfo::ScaleCondition>{ "N", PDFAnnotationIconFitInfo::ScaleCondition::Never }
        };

        constexpr const std::array<std::pair<const char*, PDFAnnotationIconFitInfo::ScaleType>, 2> scaleTypes = {
            std::pair<const char*, PDFAnnotationIconFitInfo::ScaleType>{ "A", PDFAnnotationIconFitInfo::ScaleType::Anamorphic },
            std::pair<const char*, PDFAnnotationIconFitInfo::ScaleType>{ "P", PDFAnnotationIconFitInfo::ScaleType::Proportional }
        };

        std::vector<PDFReal> point = loader.readNumberArrayFromDictionary(dictionary, "A");
        if (point.size() != 2)
        {
            point.resize(2, 0.5);
        }

        info.m_scaleCondition = loader.readEnumByName(dictionary->get("SW"), scaleConditions.begin(), scaleConditions.end(), PDFAnnotationIconFitInfo::ScaleCondition::Always);
        info.m_scaleType = loader.readEnumByName(dictionary->get("S"), scaleTypes.begin(), scaleTypes.end(), PDFAnnotationIconFitInfo::ScaleType::Proportional);
        info.m_relativeProportionalPosition = QPointF(point[0], point[1]);
        info.m_fullBox = loader.readBooleanFromDictionary(dictionary, "FB", false);
    }

    return info;
}

PDFAnnotationAdditionalActions PDFAnnotationAdditionalActions::parse(const PDFObjectStorage* storage, PDFObject object)
{
    PDFAnnotationAdditionalActions result;

    if (const PDFDictionary* dictionary = storage->getDictionaryFromObject(object))
    {
        result.m_actions[CursorEnter] = PDFAction::parse(storage, dictionary->get("E"));
        result.m_actions[CursorLeave] = PDFAction::parse(storage, dictionary->get("X"));
        result.m_actions[MousePressed] = PDFAction::parse(storage, dictionary->get("D"));
        result.m_actions[MouseReleased] = PDFAction::parse(storage, dictionary->get("U"));
        result.m_actions[FocusIn] = PDFAction::parse(storage, dictionary->get("Fo"));
        result.m_actions[FocusOut] = PDFAction::parse(storage, dictionary->get("Bl"));
        result.m_actions[PageOpened] = PDFAction::parse(storage, dictionary->get("PO"));
        result.m_actions[PageClosed] = PDFAction::parse(storage, dictionary->get("PC"));
        result.m_actions[PageShow] = PDFAction::parse(storage, dictionary->get("PV"));
        result.m_actions[PageHide] = PDFAction::parse(storage, dictionary->get("PI"));
    }

    return result;
}

PDFAnnotationManager::PDFAnnotationManager(PDFDrawWidgetProxy* proxy, QObject* parent) :
    BaseClass(parent),
    m_document(nullptr),
    m_proxy(proxy)
{
    Q_ASSERT(proxy);
    m_proxy->registerDrawInterface(this);
}

PDFAnnotationManager::~PDFAnnotationManager()
{
    m_proxy->unregisterDrawInterface(this);
}

void PDFAnnotationManager::drawPage(QPainter* painter,
                                    PDFInteger pageIndex,
                                    const PDFPrecompiledPage* compiledPage,
                                    PDFTextLayoutGetter& layoutGetter,
                                    const QMatrix& pagePointToDevicePointMatrix) const
{
    Q_UNUSED(compiledPage);
    Q_UNUSED(layoutGetter);

    PageAnnotations& annotations = getPageAnnotations(pageIndex);
    if (!annotations.isEmpty())
    {
        const PDFPage* page = m_document->getCatalog()->getPage(pageIndex);
        Q_ASSERT(page);

        PDFRenderer::Features features = m_proxy->getFeatures();
        PDFFontCache* fontCache = m_proxy->getFontCache();
        const PDFCMSPointer cms = m_proxy->getCMSManager()->getCurrentCMS();
        const PDFOptionalContentActivity* optionalActivity = m_proxy->getOptionalContentActivity();
        const PDFMeshQualitySettings& meshQualitySettings = m_proxy->getMeshQualitySettings();
        fontCache->setCacheShrinkEnabled(this, false);

        for (PageAnnotation& annotation : annotations.annotations)
        {
            const PDFAnnotation::Flags annotationFlags = annotation.annotation->getFlags();
            if (annotationFlags.testFlag(PDFAnnotation::Hidden) || // Annotation is completely hidden
                (m_target == Target::Print && !annotationFlags.testFlag(PDFAnnotation::Print)) || // Target is print and annotation is marked as not printed
                (m_target == Target::View && annotationFlags.testFlag(PDFAnnotation::NoView))) // Target is view, and annotation is disabled for screen
            {
                continue;
            }

            PDFObject appearanceStreamObject = m_document->getObject(getAppearanceStream(annotation));
            if (!appearanceStreamObject.isStream())
            {
                // Object is not valid appearance stream
                continue;
            }

            PDFDocumentDataLoaderDecorator loader(m_document);
            const PDFStream* formStream = appearanceStreamObject.getStream();
            const PDFDictionary* formDictionary = formStream->getDictionary();

            QRectF annotationRectangle = annotation.annotation->getRectangle();
            QRectF formBoundingBox = loader.readRectangle(formDictionary->get("BBox"), QRectF());
            QMatrix formMatrix = loader.readMatrixFromDictionary(formDictionary, "Matrix", QMatrix());
            QByteArray content = m_document->getDecodedStream(formStream);
            PDFObject resources = m_document->getObject(formDictionary->get("Resources"));
            PDFObject transparencyGroup = m_document->getObject(formDictionary->get("Group"));

            if (formBoundingBox.isEmpty() || annotationRectangle.isEmpty())
            {
                // Form bounding box is empty
                continue;
            }

            // "Unrotate" user coordinate space, if NoRotate flag is set
            QMatrix userSpaceToDeviceSpace = pagePointToDevicePointMatrix;
            if (annotationFlags.testFlag(PDFAnnotation::NoRotate))
            {
                PDFReal rotationAngle = 0.0;
                switch (page->getPageRotation())
                {
                    case PageRotation::None:
                        break;

                    case PageRotation::Rotate90:
                        rotationAngle = -90.0;
                        break;

                    case PageRotation::Rotate180:
                        rotationAngle = -180.0;
                        break;

                    case PageRotation::Rotate270:
                        rotationAngle = -270.0;
                        break;

                    default:
                        Q_ASSERT(false);
                        break;
                }

                QMatrix rotationMatrix;
                rotationMatrix.rotate(-rotationAngle);
                QPointF topLeft = annotationRectangle.bottomLeft(); // Do not forget, that y is upward instead of Qt
                QPointF difference = topLeft - rotationMatrix.map(topLeft);

                QMatrix finalMatrix;
                finalMatrix.translate(difference.x(), difference.y());
                finalMatrix.rotate(-rotationAngle);
                userSpaceToDeviceSpace = finalMatrix * userSpaceToDeviceSpace;
            }

            if (annotationFlags.testFlag(PDFAnnotation::NoZoom))
            {
                // Jakub Melka: we must adjust annotation rectangle to disable zoom. We calculate
                // inverse zoom as square root of absolute value of determinant of scale matrix.
                // Determinant corresponds approximately to zoom squared, and if we will have
                // unrotated matrix, and both axes are scaled by same value, then determinant will
                // be exactly zoom squared. Also, we will adjust to target device logical DPI,
                // if we, for example are using 4K, or 8K monitors.
                qreal zoom = 1.0 / qSqrt(qAbs(pagePointToDevicePointMatrix.determinant()));
                zoom = PDFWidgetUtils::scaleDPI_x(painter->device(), zoom);

                QRectF unzoomedRect(annotationRectangle.bottomLeft(), annotationRectangle.size() * zoom);
                unzoomedRect.translate(0, -unzoomedRect.height());
                annotationRectangle = unzoomedRect;
                features.setFlag(PDFRenderer::ClipToCropBox, false);
            }

            // Jakub Melka: perform algorithm 8.1, defined in PDF 1.7 reference,
            // chapter 8.4.4 Appearance streams.

            // Step 1) - calculate transformed appearance box
            QRectF transformedAppearanceBox = formMatrix.mapRect(formBoundingBox);

            // Step 2) - calculate matrix A, which maps from form space to annotation space
            //           Matrix A transforms from transformed appearance box space to the
            //           annotation rectangle.
            const PDFReal scaleX = annotationRectangle.width() / transformedAppearanceBox.width();
            const PDFReal scaleY = annotationRectangle.height() / transformedAppearanceBox.height();
            const PDFReal translateX = annotationRectangle.left() - transformedAppearanceBox.left() * scaleX;
            const PDFReal translateY = annotationRectangle.bottom() - transformedAppearanceBox.bottom() * scaleY;
            QMatrix A(scaleX, 0.0, 0.0, scaleY, translateX, translateY);

            // Step 3) - compute final matrix AA
            QMatrix AA = formMatrix * A;

            PDFPainter pdfPainter(painter, features, userSpaceToDeviceSpace, page, m_document, fontCache, cms.get(), optionalActivity, meshQualitySettings);
            pdfPainter.initializeProcessor();

            // Jakub Melka: we must check, that we do not display annotation disabled by optional content
            PDFObjectReference oc = annotation.annotation->getOptionalContent();
            if (!oc.isValid() || !pdfPainter.isContentSuppressedByOC(oc))
            {
                pdfPainter.processForm(AA, formBoundingBox, resources, transparencyGroup, content);
            }
        }

        fontCache->setCacheShrinkEnabled(this, true);
    }
}

void PDFAnnotationManager::setDocument(const PDFDocument* document)
{
    if (m_document != document)
    {
        m_document = document;
        m_pageAnnotations.clear();
    }
}

PDFObject PDFAnnotationManager::getAppearanceStream(PageAnnotation& pageAnnotation) const
{
    auto getAppearanceStream = [&pageAnnotation] (void) -> PDFObject
    {
        return pageAnnotation.annotation->getAppearanceStreams().getAppearance(pageAnnotation.appearance, pageAnnotation.annotation->getAppearanceState());
    };
    return pageAnnotation.appearanceStream.get(getAppearanceStream);
}

PDFAnnotationManager::PageAnnotations& PDFAnnotationManager::getPageAnnotations(PDFInteger pageIndex) const
{
    Q_ASSERT(m_document);

    auto it = m_pageAnnotations.find(pageIndex);
    if (it == m_pageAnnotations.cend())
    {
        // Create page annotations
        const PDFPage* page = m_document->getCatalog()->getPage(pageIndex);
        Q_ASSERT(page);

        PageAnnotations annotations;

        const std::vector<PDFObjectReference>& pageAnnotations = page->getAnnotations();
        annotations.annotations.reserve(pageAnnotations.size());
        for (PDFObjectReference annotationReference : pageAnnotations)
        {
            PDFAnnotationPtr annotationPtr = PDFAnnotation::parse(&m_document->getStorage(), m_document->getObjectByReference(annotationReference));
            if (annotationPtr)
            {
                PageAnnotation annotation;
                annotation.annotation = qMove(annotationPtr);
                annotations.annotations.emplace_back(qMove(annotation));
            }
        }

        it = m_pageAnnotations.insert(std::make_pair(pageIndex, qMove(annotations))).first;
    }

    return it->second;
}

PDFAnnotationManager::Target PDFAnnotationManager::getTarget() const
{
    return m_target;
}

void PDFAnnotationManager::setTarget(Target target)
{
    m_target = target;
}

void PDFSimpleGeometryAnnotation::draw(AnnotationDrawParameters& parameters) const
{
    Q_ASSERT(parameters.painter);
    parameters.boundingRectangle = getRectangle();

    QPainter& painter = *parameters.painter;
    painter.setPen(getPen());
    painter.setBrush(getBrush());

    switch (getType())
    {
        case AnnotationType::Square:
        {
            const PDFAnnotationBorder& border = getBorder();

            const PDFReal hCornerRadius = border.getHorizontalCornerRadius();
            const PDFReal vCornerRadius = border.getVerticalCornerRadius();
            const bool isRounded = !qFuzzyIsNull(hCornerRadius) || !qFuzzyIsNull(vCornerRadius);

            if (isRounded)
            {
                painter.drawRoundedRect(getRectangle(), hCornerRadius, vCornerRadius, Qt::AbsoluteSize);
            }
            else
            {
                painter.drawRect(getRectangle());
            }
            break;
        }

        case AnnotationType::Circle:
        {
            const PDFAnnotationBorder& border = getBorder();
            const PDFReal width = border.getWidth();
            QRectF rectangle = getRectangle();
            rectangle.adjust(width, width, -width, -width);
            painter.drawEllipse(rectangle);
            break;
        }

        default:
        {
            Q_ASSERT(false);
            break;
        }
    }
}

QColor PDFSimpleGeometryAnnotation::getFillColor() const
{
    QColor color = getDrawColorFromAnnotationColor(getInteriorColor());
    if (color.isValid())
    {
        color.setAlphaF(getOpacity());
    }
    return color;
}

QColor PDFMarkupAnnotation::getStrokeColor() const
{
    QColor color = PDFAnnotation::getStrokeColor();
    if (color.isValid())
    {
        color.setAlphaF(m_opacity);
    }
    return color;
}

QColor PDFMarkupAnnotation::getFillColor() const
{
    QColor color = PDFAnnotation::getFillColor();
    if (color.isValid())
    {
        color.setAlphaF(m_opacity);
    }
    return color;
}

void PDFTextAnnotation::draw(AnnotationDrawParameters& parameters) const
{
    const PDFReal opacity = getOpacity();
    QColor strokeColor = QColor::fromRgbF(0.0, 0.0, 0.0, opacity);
    QColor fillColor = QColor::fromRgbF(1.0, 1.0, 0.0, opacity);

    constexpr const PDFReal rectSize = 32.0;
    constexpr const PDFReal penWidth = 2.0;

    QPainter& painter = *parameters.painter;
    QRectF rectangle = getRectangle();
    rectangle.setSize(QSizeF(rectSize, rectSize));

    QPen pen(strokeColor);
    pen.setWidthF(penWidth);
    painter.setPen(pen);

    painter.setBrush(QBrush(fillColor, Qt::SolidPattern));

    QRectF ellipseRectangle = rectangle;
    ellipseRectangle.adjust(penWidth, penWidth, -penWidth, -penWidth);

    // Draw the ellipse
    painter.drawEllipse(ellipseRectangle);

    QFont font = QApplication::font();
    font.setPixelSize(16.0);

    QString text = "?";
    if (m_iconName == "Comment")
    {
        text = QString::fromUtf16(u"\U0001F4AC");
    }
    else if (m_iconName == "Help")
    {
        text = "?";
    }
    else if (m_iconName == "Insert")
    {
        text = QString::fromUtf16(u"\u2380");
    }
    else if (m_iconName == "Key")
    {
        text = QString::fromUtf16(u"\U0001F511");
    }
    else if (m_iconName == "NewParagraph")
    {
        text = QString::fromUtf16(u"\u2606");
    }
    else if (m_iconName == "Note")
    {
        text = QString::fromUtf16(u"\u266A");
    }
    else if (m_iconName == "Paragraph")
    {
        text = QString::fromUtf16(u"\u00B6");
    }

    QPainterPath textPath;
    textPath.addText(0.0, 0.0, font, text);
    textPath = QMatrix(1.0, 0.0, 0.0, -1.0, 0.0, 0.0).map(textPath);
    QRectF textBoundingRect = textPath.boundingRect();
    QPointF offset = rectangle.center() - textBoundingRect.center();
    textPath.translate(offset);
    painter.fillPath(textPath, QBrush(strokeColor, Qt::SolidPattern));

    parameters.boundingRectangle = rectangle;
}

}   // namespace pdf
