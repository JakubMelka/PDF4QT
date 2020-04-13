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
#include "pdfparser.h"
#include "pdfdrawwidget.h"

#include <QDialog>
#include <QApplication>
#include <QMouseEvent>
#include <QGroupBox>
#include <QScrollArea>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QLabel>

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

PDFAnnotationPtr PDFAnnotation::parse(const PDFObjectStorage* storage, PDFObjectReference reference)
{
    PDFObject object = storage->getObjectByReference(reference);
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

        constexpr const std::array<std::pair<const char*, Stamp>, 14> stamps = {
            std::pair<const char*, Stamp>{ "Approved", Stamp::Approved },
            std::pair<const char*, Stamp>{ "AsIs", Stamp::AsIs },
            std::pair<const char*, Stamp>{ "Confidential", Stamp::Confidential },
            std::pair<const char*, Stamp>{ "Departmental", Stamp::Departmental },
            std::pair<const char*, Stamp>{ "Draft", Stamp::Draft },
            std::pair<const char*, Stamp>{ "Experimental", Stamp::Experimental },
            std::pair<const char*, Stamp>{ "Expired", Stamp::Expired },
            std::pair<const char*, Stamp>{ "Final", Stamp::Final },
            std::pair<const char*, Stamp>{ "ForComment", Stamp::ForComment },
            std::pair<const char*, Stamp>{ "ForPublicRelease", Stamp::ForPublicRelease },
            std::pair<const char*, Stamp>{ "NotApproved", Stamp::NotApproved },
            std::pair<const char*, Stamp>{ "NotForPublicRelease", Stamp::NotForPublicRelease },
            std::pair<const char*, Stamp>{ "Sold", Stamp::Sold },
            std::pair<const char*, Stamp>{ "TopSecret", Stamp::TopSecret }
        };

        annotation->m_stamp = loader.readEnumByName(dictionary->get("Name"), stamps.begin(), stamps.end(), Stamp::Draft);
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

        constexpr const std::array<std::pair<const char*, FileAttachmentIcon>, 4> icons = {
            std::pair<const char*, FileAttachmentIcon>{ "Graph", FileAttachmentIcon::Graph },
            std::pair<const char*, FileAttachmentIcon>{ "Paperclip", FileAttachmentIcon::Paperclip },
            std::pair<const char*, FileAttachmentIcon>{ "PushPin", FileAttachmentIcon::PushPin },
            std::pair<const char*, FileAttachmentIcon>{ "Tag", FileAttachmentIcon::Tag }
        };

        annotation->m_icon = loader.readEnumByName(dictionary->get("Name"), icons.begin(), icons.end(), FileAttachmentIcon::PushPin);
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
    else if (subtype == "3D")
    {
        PDF3DAnnotation* annotation = new PDF3DAnnotation();
        result.reset(annotation);

        annotation->m_stream = PDF3DStream::parse(storage, dictionary->get("3DD"));

        const std::vector<PDF3DView>& views = annotation->getStream().getViews();
        PDFObject defaultViewObject = storage->getObject(dictionary->get("DV"));
        if (defaultViewObject.isDictionary())
        {
            annotation->m_defaultView = PDF3DView::parse(storage, defaultViewObject);
        }
        else if (defaultViewObject.isInt())
        {
            PDFInteger index = defaultViewObject.getInteger();
            if (index >= 0 && index < PDFInteger(views.size()))
            {
                annotation->m_defaultView = views[index];
            }
        }
        else if (defaultViewObject.isName() && !views.empty())
        {
            QByteArray name = defaultViewObject.getString();
            if (name == "F")
            {
                annotation->m_defaultView = views.front();
            }
            else if (name == "L")
            {
                annotation->m_defaultView = views.back();
            }
        }

        annotation->m_activation = PDF3DActivation::parse(storage, dictionary->get("3DA"));
        annotation->m_interactive = loader.readBooleanFromDictionary(dictionary, "3DI", true);
        annotation->m_viewBox = loader.readRectangle(dictionary->get("3DB"), QRectF());
    }
    else if (subtype == "RichMedia")
    {
        PDFRichMediaAnnotation* annotation = new PDFRichMediaAnnotation();
        result.reset(annotation);

        annotation->m_content = PDFRichMediaContent::parse(storage, dictionary->get("RichMediaContent"));
        annotation->m_settings = PDFRichMediaSettings::parse(storage, dictionary->get("RichMediaSettings"));
    }

    if (!result)
    {
        // Invalid annotation type
        return result;
    }

    // Load common data for annotation
    result->m_selfReference = reference;
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
    PDFAnnotationQuadrilaterals::Quadrilaterals quadrilaterals;

    PDFDocumentDataLoaderDecorator loader(storage);
    std::vector<PDFReal> points = loader.readNumberArray(quadrilateralsObject);
    const size_t quadrilateralCount = points.size() % 8;
    path.reserve(int(quadrilateralCount) + 5);
    quadrilaterals.reserve(quadrilateralCount);
    for (size_t i = 0; i < quadrilateralCount; ++i)
    {
        const size_t offset = i * 8;
        QPointF p1(points[offset + 0], points[offset + 1]);
        QPointF p2(points[offset + 2], points[offset + 3]);
        QPointF p3(points[offset + 4], points[offset + 5]);
        QPointF p4(points[offset + 6], points[offset + 7]);

        path.moveTo(p1);
        path.lineTo(p2);
        path.lineTo(p4);
        path.lineTo(p3);
        path.closeSubpath();

        quadrilaterals.emplace_back(PDFAnnotationQuadrilaterals::Quadrilateral{ p1, p2, p3, p4 });
    }

    if (path.isEmpty() && annotationRect.isValid())
    {
        // Jakub Melka: we are using points at the top, because PDF has inverted y axis
        // against the Qt's y axis.
        path.addRect(annotationRect);
        quadrilaterals.emplace_back(PDFAnnotationQuadrilaterals::Quadrilateral{ annotationRect.topLeft(), annotationRect.topRight(), annotationRect.bottomLeft(), annotationRect.bottomRight() });
    }

    return PDFAnnotationQuadrilaterals(qMove(path), qMove(quadrilaterals));
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

QColor PDFAnnotation::getDrawColorFromAnnotationColor(const std::vector<PDFReal>& color)
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

PDFAnnotationManager::PDFAnnotationManager(PDFFontCache* fontCache,
                                           const PDFCMSManager* cmsManager,
                                           const PDFOptionalContentActivity* optionalActivity,
                                           PDFMeshQualitySettings meshQualitySettings,
                                           PDFRenderer::Features features,
                                           Target target,
                                           QObject* parent) :
    BaseClass(parent),
    m_document(nullptr),
    m_fontCache(fontCache),
    m_cmsManager(cmsManager),
    m_optionalActivity(optionalActivity),
    m_meshQualitySettings(meshQualitySettings),
    m_features(features),
    m_target(target)
{

}

PDFAnnotationManager::~PDFAnnotationManager()
{

}

QMatrix PDFAnnotationManager::prepareTransformations(const QMatrix& pagePointToDevicePointMatrix,
                                                     QPaintDevice* device,
                                                     const PDFAnnotation::Flags annotationFlags,
                                                     const PDFPage* page,
                                                     QRectF& annotationRectangle) const
{
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
        zoom = PDFWidgetUtils::scaleDPI_x(device, zoom);

        QRectF unzoomedRect(annotationRectangle.bottomLeft(), annotationRectangle.size() * zoom);
        unzoomedRect.translate(0, -unzoomedRect.height());
        annotationRectangle = unzoomedRect;
    }

    return userSpaceToDeviceSpace;
}

void PDFAnnotationManager::drawPage(QPainter* painter,
                                    PDFInteger pageIndex,
                                    const PDFPrecompiledPage* compiledPage,
                                    PDFTextLayoutGetter& layoutGetter,
                                    const QMatrix& pagePointToDevicePointMatrix) const
{
    Q_UNUSED(compiledPage);
    Q_UNUSED(layoutGetter);

    const PageAnnotations& annotations = getPageAnnotations(pageIndex);
    if (!annotations.isEmpty())
    {
        const PDFPage* page = m_document->getCatalog()->getPage(pageIndex);
        Q_ASSERT(page);

        PDFRenderer::Features features = m_features;
        if (!features.testFlag(PDFRenderer::DisplayAnnotations))
        {
            // Annotation displaying is disabled
            return;
        }

        Q_ASSERT(m_fontCache);
        Q_ASSERT(m_cmsManager);
        Q_ASSERT(m_optionalActivity);

        const PDFCMSPointer cms = m_cmsManager->getCurrentCMS();
        m_fontCache->setCacheShrinkEnabled(this, false);

        for (const PageAnnotation& annotation : annotations.annotations)
        {
            const PDFAnnotation::Flags annotationFlags = annotation.annotation->getFlags();
            if (annotationFlags.testFlag(PDFAnnotation::Hidden) || // Annotation is completely hidden
                (m_target == Target::Print && !annotationFlags.testFlag(PDFAnnotation::Print)) || // Target is print and annotation is marked as not printed
                (m_target == Target::View && annotationFlags.testFlag(PDFAnnotation::NoView)) ||  // Target is view, and annotation is disabled for screen
                 annotation.annotation->isReplyTo()) // Annotation is reply to another annotation, display just the first annotation
            {
                continue;
            }

            PDFObject appearanceStreamObject = m_document->getObject(getAppearanceStream(annotation));
            if (!appearanceStreamObject.isStream())
            {
                // Object is not valid appearance stream. We will try to draw default
                // annotation appearance.
                painter->save();
                painter->setRenderHint(QPainter::Antialiasing, true);
                painter->setWorldMatrix(pagePointToDevicePointMatrix, true);
                AnnotationDrawParameters parameters;
                parameters.painter = painter;
                parameters.key = std::make_pair(annotation.appearance, annotation.annotation->getAppearanceState());
                annotation.annotation->draw(parameters);
                painter->restore();
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

            QMatrix userSpaceToDeviceSpace = prepareTransformations(pagePointToDevicePointMatrix, painter->device(), annotationFlags, page, annotationRectangle);

            if (annotationFlags.testFlag(PDFAnnotation::NoZoom))
            {
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

            PDFPainter pdfPainter(painter, features, userSpaceToDeviceSpace, page, m_document, m_fontCache, cms.get(), m_optionalActivity, m_meshQualitySettings);
            pdfPainter.initializeProcessor();

            // Jakub Melka: we must check, that we do not display annotation disabled by optional content
            PDFObjectReference oc = annotation.annotation->getOptionalContent();
            if (!oc.isValid() || !pdfPainter.isContentSuppressedByOC(oc))
            {
                pdfPainter.processForm(AA, formBoundingBox, resources, transparencyGroup, content);
            }
        }

        m_fontCache->setCacheShrinkEnabled(this, true);
    }
}

void PDFAnnotationManager::setDocument(const PDFDocument* document, const PDFOptionalContentActivity* optionalContentActivity)
{
    if (m_document != document)
    {
        m_document = document;
        m_optionalActivity = optionalContentActivity;
        m_pageAnnotations.clear();
    }
}

PDFObject PDFAnnotationManager::getAppearanceStream(const PageAnnotation& pageAnnotation) const
{
    auto getAppearanceStream = [&pageAnnotation] (void) -> PDFObject
    {
        return pageAnnotation.annotation->getAppearanceStreams().getAppearance(pageAnnotation.appearance, pageAnnotation.annotation->getAppearanceState());
    };

    QMutexLocker lock(&m_mutex);
    return pageAnnotation.appearanceStream.get(getAppearanceStream);
}

const PDFAnnotationManager::PageAnnotations& PDFAnnotationManager::getPageAnnotations(PDFInteger pageIndex) const
{
    return const_cast<PDFAnnotationManager*>(this)->getPageAnnotations(pageIndex);
}

PDFAnnotationManager::PageAnnotations& PDFAnnotationManager::getPageAnnotations(PDFInteger pageIndex)
{
    Q_ASSERT(m_document);

    QMutexLocker lock(&m_mutex);
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
            PDFAnnotationPtr annotationPtr = PDFAnnotation::parse(&m_document->getStorage(), annotationReference);
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

bool PDFAnnotationManager::hasAnnotation(PDFInteger pageIndex) const
{
    return !getPageAnnotations(pageIndex).isEmpty();
}

bool PDFAnnotationManager::hasAnyPageAnnotation(const std::vector<PDFInteger>& pageIndices) const
{
    return std::any_of(pageIndices.cbegin(), pageIndices.cend(), std::bind(&PDFAnnotationManager::hasAnnotation, this, std::placeholders::_1));
}

PDFRenderer::Features PDFAnnotationManager::getFeatures() const
{
    return m_features;
}

void PDFAnnotationManager::setFeatures(PDFRenderer::Features features)
{
    m_features = features;
}

PDFMeshQualitySettings PDFAnnotationManager::getMeshQualitySettings() const
{
    return m_meshQualitySettings;
}

void PDFAnnotationManager::setMeshQualitySettings(const PDFMeshQualitySettings& meshQualitySettings)
{
    m_meshQualitySettings = meshQualitySettings;
}

PDFFontCache* PDFAnnotationManager::getFontCache() const
{
    return m_fontCache;
}

void PDFAnnotationManager::setFontCache(PDFFontCache* fontCache)
{
    m_fontCache = fontCache;
}

const PDFOptionalContentActivity* PDFAnnotationManager::getOptionalActivity() const
{
    return m_optionalActivity;
}

void PDFAnnotationManager::setOptionalActivity(const PDFOptionalContentActivity* optionalActivity)
{
    m_optionalActivity = optionalActivity;
}

PDFAnnotationManager::Target PDFAnnotationManager::getTarget() const
{
    return m_target;
}

void PDFAnnotationManager::setTarget(Target target)
{
    m_target = target;
}

PDFWidgetAnnotationManager::PDFWidgetAnnotationManager(PDFDrawWidgetProxy* proxy, QObject* parent) :
    BaseClass(proxy->getFontCache(), proxy->getCMSManager(), proxy->getOptionalContentActivity(), proxy->getMeshQualitySettings(), proxy->getFeatures(), Target::View, parent),
    m_proxy(proxy)
{
    Q_ASSERT(proxy);
    m_proxy->registerDrawInterface(this);
}

PDFWidgetAnnotationManager::~PDFWidgetAnnotationManager()
{
    m_proxy->unregisterDrawInterface(this);
}

void PDFWidgetAnnotationManager::keyPressEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);
    Q_UNUSED(event);
}

void PDFWidgetAnnotationManager::mousePressEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);
    Q_UNUSED(event);

    updateFromMouseEvent(event);
}

void PDFWidgetAnnotationManager::mouseReleaseEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);
    Q_UNUSED(event);

    updateFromMouseEvent(event);
}

void PDFWidgetAnnotationManager::mouseMoveEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);
    Q_UNUSED(event);

    updateFromMouseEvent(event);
}

void PDFWidgetAnnotationManager::wheelEvent(QWidget* widget, QWheelEvent* event)
{
    Q_UNUSED(widget);
    Q_UNUSED(event);
}

void PDFWidgetAnnotationManager::updateFromMouseEvent(QMouseEvent* event)
{
    PDFWidget* widget = m_proxy->getWidget();
    std::vector<PDFInteger> currentPages = widget->getDrawWidget()->getCurrentPages();

    if (!hasAnyPageAnnotation(currentPages))
    {
        // All pages doesn't have annotation
        return;
    }

    m_tooltip = QString();
    bool appearanceChanged = false;

    // We must update appearance states, and update tooltip
    PDFWidgetSnapshot snapshot = m_proxy->getSnapshot();
    const bool isDown = event->buttons().testFlag(Qt::LeftButton);
    const PDFAppeareanceStreams::Appearance hoverAppearance = isDown ? PDFAppeareanceStreams::Appearance::Down : PDFAppeareanceStreams::Appearance::Rollover;

    for (const PDFWidgetSnapshot::SnapshotItem& snapshotItem : snapshot.items)
    {
        PageAnnotations& pageAnnotations = getPageAnnotations(snapshotItem.pageIndex);
        for (PageAnnotation& pageAnnotation : pageAnnotations.annotations)
        {
            if (pageAnnotation.annotation->isReplyTo())
            {
                // Annotation is reply to another annotation, do not interact with it
                continue;
            }

            const PDFAppeareanceStreams::Appearance oldAppearance = pageAnnotation.appearance;
            QRectF annotationRect = pageAnnotation.annotation->getRectangle();
            QMatrix matrix = prepareTransformations(snapshotItem.pageToDeviceMatrix, widget, pageAnnotation.annotation->getFlags(), m_document->getCatalog()->getPage(snapshotItem.pageIndex), annotationRect);
            QPainterPath path;
            path.addRect(annotationRect);
            path = matrix.map(path);

            if (path.contains(event->pos()))
            {
                pageAnnotation.appearance = hoverAppearance;

                // Generate tooltip
                if (m_tooltip.isEmpty())
                {
                    const PDFMarkupAnnotation* markupAnnotation = pageAnnotation.annotation->asMarkupAnnotation();
                    if (markupAnnotation)
                    {
                        QString title = markupAnnotation->getWindowTitle();
                        if (title.isEmpty())
                        {
                            title = markupAnnotation->getSubject();
                        }
                        if (title.isEmpty())
                        {
                            title = PDFTranslationContext::tr("Info");
                        }

                        const size_t repliesCount = pageAnnotations.getReplies(pageAnnotation).size();
                        if (repliesCount > 0)
                        {
                            title = PDFTranslationContext::tr("%1 (%2 replies)").arg(title).arg(repliesCount);
                        }

                        m_tooltip = QString("<p><b>%1</b></p><p>%2</p>").arg(title, markupAnnotation->getContents());
                    }
                }

                // Generate popup window
                if (event->type() == QEvent::MouseButtonPress && event->button() == Qt::LeftButton)
                {
                    const PDFMarkupAnnotation* markupAnnotation = pageAnnotation.annotation->asMarkupAnnotation();
                    if (markupAnnotation)
                    {
                        QDialog* dialog = createDialogForMarkupAnnotations(widget, pageAnnotation, pageAnnotations);

                        // Set proper dialog position - according to the popup annotation. If we
                        // do not have popup annotation, then try to use annotations rectangle.
                        if (const PageAnnotation* popupAnnotation = pageAnnotations.getPopupAnnotation(pageAnnotation))
                        {
                            QPoint popupPoint = snapshotItem.pageToDeviceMatrix.map(popupAnnotation->annotation->getRectangle().bottomLeft()).toPoint();
                            popupPoint = widget->mapToGlobal(popupPoint);
                            dialog->move(popupPoint);
                        }
                        else if (markupAnnotation->getRectangle().isValid())
                        {
                            QPoint popupPoint = snapshotItem.pageToDeviceMatrix.map(markupAnnotation->getRectangle().bottomRight()).toPoint();
                            popupPoint = widget->mapToGlobal(popupPoint);
                            dialog->move(popupPoint);
                        }

                        dialog->exec();
                    }

                    if (pageAnnotation.annotation->getType() == AnnotationType::Link)
                    {
                        const PDFLinkAnnotation* linkAnnotation = dynamic_cast<const PDFLinkAnnotation*>(pageAnnotation.annotation.data());
                        Q_ASSERT(linkAnnotation);

                        // We must check, if user clicked to the link area
                        QPainterPath activationPath = linkAnnotation->getActivationRegion().getPath();
                        activationPath = snapshotItem.pageToDeviceMatrix.map(activationPath);
                        if (activationPath.contains(event->pos()) && linkAnnotation->getAction())
                        {
                            emit actionTriggered(linkAnnotation->getAction());
                        }
                    }
                }
            }
            else
            {
                pageAnnotation.appearance = PDFAppeareanceStreams::Appearance::Normal;
            }

            const bool currentAppearanceChanged = oldAppearance != pageAnnotation.appearance;
            if (currentAppearanceChanged)
            {
                // We have changed appearance - we must mark stream as dirty
                pageAnnotation.appearanceStream.dirty();
                appearanceChanged = true;
            }
        }
    }

    // If appearance has changed, then we must redraw the page
    if (appearanceChanged)
    {
        emit widget->getDrawWidgetProxy()->repaintNeeded();
    }
}

QDialog* PDFWidgetAnnotationManager::createDialogForMarkupAnnotations(PDFWidget* widget,
                                                                      const PageAnnotation& pageAnnotation,
                                                                      const PageAnnotations& pageAnnotations)
{
    QDialog* dialog = new QDialog(widget->getDrawWidget()->getWidget(), Qt::Popup);
    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    createWidgetsForMarkupAnnotations(dialog, pageAnnotation, pageAnnotations);
    return dialog;
}

void PDFWidgetAnnotationManager::createWidgetsForMarkupAnnotations(QWidget* parentWidget,
                                                                   const PageAnnotation& pageAnnotation,
                                                                   const PageAnnotations& pageAnnotations)
{
    std::vector<const PageAnnotation*> replies = pageAnnotations.getReplies(pageAnnotation);
    replies.insert(replies.begin(), &pageAnnotation);

    QScrollArea* scrollArea = new QScrollArea(parentWidget);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    QVBoxLayout* layout = new QVBoxLayout(parentWidget);
    layout->addWidget(scrollArea);
    layout->setMargin(0);
    layout->setContentsMargins(QMargins());

    QWidget* frameWidget = new QWidget(scrollArea);
    QVBoxLayout* frameLayout = new QVBoxLayout(frameWidget);
    frameLayout->setMargin(0);
    frameLayout->setSpacing(0);
    scrollArea->setWidget(frameWidget);

    const PDFMarkupAnnotation* markupMainAnnotation = pageAnnotation.annotation->asMarkupAnnotation();
    QColor color = markupMainAnnotation->getDrawColorFromAnnotationColor(markupMainAnnotation->getColor());
    QColor titleColor = QColor::fromHslF(color.hueF(), color.saturationF(), 0.2, 1.0);
    QColor backgroundColor = QColor::fromHslF(color.hueF(), color.saturationF(), 0.9, 1.0);

    QString style = "QGroupBox { "
                    "border: 2px solid black; "
                    "border-color: rgb(%4, %5, %6); "
                    "margin-top: 3ex; "
                    "background-color: rgb(%1, %2, %3); "
                    "}"
                    "QGroupBox::title { "
                    "subcontrol-origin: margin; "
                    "subcontrol-position: top center; "
                    "padding: 0px 8192px; "
                    "background-color: rgb(%4, %5, %6); "
                    "color: #FFFFFF;"
                    "}";
    style = style.arg(backgroundColor.red()).arg(backgroundColor.green()).arg(backgroundColor.blue()).arg(titleColor.red()).arg(titleColor.green()).arg(titleColor.blue());

    for (const PageAnnotation* annotation : replies)
    {
        const PDFMarkupAnnotation* markupAnnotation = annotation->annotation->asMarkupAnnotation();

        if (!markupAnnotation)
        {
            // This should not happen...
            continue;
        }

        QGroupBox* groupBox = new QGroupBox(scrollArea);
        frameLayout->addWidget(groupBox);

        QString title = markupAnnotation->getWindowTitle();
        if (title.isEmpty())
        {
            title = markupAnnotation->getSubject();
        }

        QString dateTimeString = markupAnnotation->getCreationDate().toLocalTime().toString(Qt::SystemLocaleLongDate);
        title = QString("%1 (%2)").arg(title, dateTimeString).trimmed();

        groupBox->setStyleSheet(style);
        groupBox->setTitle(title);
        QVBoxLayout* groupBoxLayout = new QVBoxLayout(groupBox);

        QLabel* label = new QLabel(groupBox);
        label->setTextInteractionFlags(Qt::TextBrowserInteraction);
        label->setWordWrap(true);
        label->setText(markupAnnotation->getContents());
        label->setFixedWidth(PDFWidgetUtils::scaleDPI_x(label, 250));
        label->setMinimumHeight(label->sizeHint().height());
        groupBoxLayout->addWidget(label);
    }

    frameWidget->setFixedSize(frameWidget->minimumSizeHint());
    parentWidget->setFixedSize(scrollArea->sizeHint());
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

bool PDFMarkupAnnotation::isReplyTo() const
{
    return m_inReplyTo.isValid() && m_replyType == ReplyType::Reply;
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

std::vector<PDFAppeareanceStreams::Key> PDFTextAnnotation::getDrawKeys() const
{
    return { PDFAppeareanceStreams::Key{ PDFAppeareanceStreams::Appearance::Normal, QByteArray() },
             PDFAppeareanceStreams::Key{ PDFAppeareanceStreams::Appearance::Rollover, QByteArray() },
             PDFAppeareanceStreams::Key{ PDFAppeareanceStreams::Appearance::Down, QByteArray() } };
}

void PDFTextAnnotation::draw(AnnotationDrawParameters& parameters) const
{
    const PDFReal opacity = getOpacity();
    QColor strokeColor = QColor::fromRgbF(0.0, 0.0, 0.0, opacity);
    QColor fillColor = (parameters.key.first == PDFAppeareanceStreams::Appearance::Normal) ? QColor::fromRgbF(1.0, 1.0, 0.0, opacity) :
                                                                                             QColor::fromRgbF(1.0, 0.0, 0.0, opacity);

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

void PDFLineAnnotation::draw(AnnotationDrawParameters& parameters) const
{
    QLineF line = getLine();
    if (line.isNull())
    {
        // Jakub Melka: do not draw empty lines
        return;
    }

    QPainter& painter = *parameters.painter;
    painter.setPen(getPen());
    painter.setBrush(getBrush());

    QPainterPath boundingPath;
    boundingPath.moveTo(line.p1());
    boundingPath.lineTo(line.p2());

    LineGeometryInfo info = LineGeometryInfo::create(line);

    const PDFReal leaderLineLength = getLeaderLineLength();
    const PDFReal coefficientSigned = (leaderLineLength >= 0.0) ? 1.0 : -1.0;
    const PDFReal leaderLineOffset = getLeaderLineOffset() * coefficientSigned;
    const PDFReal leaderLineExtension = getLeaderLineExtension() * coefficientSigned;
    const PDFReal lineEndingSize = qMin(painter.pen().widthF() * 5.0, line.length() * 0.5);
    const bool hasLeaderLine = !qFuzzyIsNull(leaderLineLength) || !qFuzzyIsNull(leaderLineExtension);

    QLineF normalLine = info.transformedLine.normalVector().unitVector();
    QPointF normalVector = normalLine.p1() - normalLine.p2();

    QLineF lineToPaint = info.transformedLine;
    if (hasLeaderLine)
    {
        // We will draw leader lines at both start/end
        QPointF p1llStart = info.transformedLine.p1() + normalVector * leaderLineOffset;
        QPointF p1llEnd = info.transformedLine.p1() + normalVector * (leaderLineOffset + leaderLineLength + leaderLineExtension);

        QLineF llStart(p1llStart, p1llEnd);
        llStart = info.LCStoGCS.map(llStart);

        boundingPath.moveTo(llStart.p1());
        boundingPath.lineTo(llStart.p2());
        painter.drawLine(llStart);

        QPointF p2llStart = info.transformedLine.p2() + normalVector * leaderLineOffset;
        QPointF p2llEnd = info.transformedLine.p2() + normalVector * (leaderLineOffset + leaderLineLength + leaderLineExtension);

        QLineF llEnd(p2llStart, p2llEnd);
        llEnd = info.LCStoGCS.map(llEnd);

        boundingPath.moveTo(llEnd.p1());
        boundingPath.lineTo(llEnd.p2());
        painter.drawLine(llEnd);

        lineToPaint.translate(normalVector * (leaderLineOffset + leaderLineLength));
    }

    QLineF lineToPaintOrig = info.LCStoGCS.map(lineToPaint);
    info = LineGeometryInfo::create(lineToPaintOrig);
    drawLine(info, painter, lineEndingSize, getStartLineEnding(), getEndLineEnding(), boundingPath, getCaptionOffset(), getContents(), getCaptionPosition() == CaptionPosition::Top);

    parameters.boundingRectangle = boundingPath.boundingRect();
    parameters.boundingRectangle.adjust(-lineEndingSize, -lineEndingSize, lineEndingSize, lineEndingSize);
}

QColor PDFLineAnnotation::getFillColor() const
{
    QColor color = getDrawColorFromAnnotationColor(getInteriorColor());
    if (color.isValid())
    {
        color.setAlphaF(getOpacity());
    }
    return color;
}

void PDFPolygonalGeometryAnnotation::draw(AnnotationDrawParameters& parameters) const
{
    if (m_vertices.empty())
    {
        // Jakub Melka: do not draw empty lines
        return;
    }

    QPainter& painter = *parameters.painter;
    painter.setPen(getPen());
    painter.setBrush(getBrush());

    const PDFReal penWidth = painter.pen().widthF();
    switch (m_type)
    {
        case AnnotationType::Polygon:
        {
            QPolygonF polygon;
            polygon.reserve(int(m_vertices.size() + 1));
            for (const QPointF& point : m_vertices)
            {
                polygon << point;
            }
            if (!polygon.isClosed())
            {
                polygon << m_vertices.front();
            }

            painter.drawPolygon(polygon, Qt::OddEvenFill);
            parameters.boundingRectangle = polygon.boundingRect();
            parameters.boundingRectangle.adjust(-penWidth, -penWidth, penWidth, penWidth);
            break;
        }

        case AnnotationType::Polyline:
        {
            const PDFReal lineEndingSize = painter.pen().widthF() * 5.0;
            QPainterPath boundingPath;

            const size_t pointCount = m_vertices.size();
            const size_t lastPoint = pointCount - 1;
            for (size_t i = 1; i < pointCount; ++i)
            {
                if (i == 1)
                {
                    // We draw first line
                    drawLine(LineGeometryInfo::create(QLineF(m_vertices[i - 1], m_vertices[i])), painter, lineEndingSize, getStartLineEnding(), AnnotationLineEnding::None, boundingPath, QPointF(), QString(), true);
                }
                else if (i == lastPoint)
                {
                    // We draw last line
                    drawLine(LineGeometryInfo::create(QLineF(m_vertices[i - 1], m_vertices[i])), painter, lineEndingSize, AnnotationLineEnding::None, getEndLineEnding(), boundingPath, QPointF(), QString(), true);
                }
                else
                {
                    QLineF line(m_vertices[i - 1], m_vertices[i]);
                    boundingPath.moveTo(line.p1());
                    boundingPath.lineTo(line.p2());
                    painter.drawLine(line);
                }
            }

            parameters.boundingRectangle = boundingPath.boundingRect();
            parameters.boundingRectangle.adjust(-lineEndingSize, -lineEndingSize, lineEndingSize, lineEndingSize);
            break;
        }

        default:
            Q_ASSERT(false);
            break;
    }
}

QColor PDFPolygonalGeometryAnnotation::getFillColor() const
{
    QColor color = getDrawColorFromAnnotationColor(getInteriorColor());
    if (color.isValid())
    {
        color.setAlphaF(getOpacity());
    }
    return color;
}

PDFAnnotation::LineGeometryInfo PDFAnnotation::LineGeometryInfo::create(QLineF line)
{
    LineGeometryInfo result;
    result.originalLine = line;
    result.transformedLine = QLineF(QPointF(0, 0), QPointF(line.length(), 0));

    // Strategy: for simplification, we rotate the line clockwise so we will
    // get the line axis equal to the x-axis.
    const double angle = line.angleTo(QLineF(0, 0, 1, 0));

    QPointF p1 = line.p1();

    // Matrix LCStoGCS is local coordinate system of line line. It transforms
    // points on the line to the global coordinate system. So, point (0, 0) will
    // map onto p1 and point (length(p1-p2), 0) will map onto p2.
    result.LCStoGCS = QMatrix();
    result.LCStoGCS.translate(p1.x(), p1.y());
    result.LCStoGCS.rotate(angle);
    result.GCStoLCS = result.LCStoGCS.inverted();

    return result;
}

void PDFAnnotation::drawLine(const PDFAnnotation::LineGeometryInfo& info,
                             QPainter& painter,
                             PDFReal lineEndingSize,
                             AnnotationLineEnding p1Ending,
                             AnnotationLineEnding p2Ending,
                             QPainterPath& boundingPath,
                             QPointF textOffset,
                             const QString& text,
                             bool textIsAboveLine) const
{
    const PDFReal angle = 30;
    const PDFReal lineEndingHalfSize = lineEndingSize * 0.5;
    const PDFReal arrowAxisLength = lineEndingHalfSize / qTan(qDegreesToRadians(angle));

    auto getOffsetFromLineEnding = [lineEndingHalfSize, arrowAxisLength](AnnotationLineEnding ending)
    {
        switch (ending)
        {
            case AnnotationLineEnding::Square:
            case AnnotationLineEnding::Circle:
            case AnnotationLineEnding::Diamond:
                return lineEndingHalfSize;
            case AnnotationLineEnding::ClosedArrow:
                return arrowAxisLength;

            default:
                break;
        }

        return 0.0;
    };

    auto drawLineEnding = [&](QPointF point, AnnotationLineEnding ending, bool flipAxis)
    {
        QPainterPath path;

        switch (ending)
        {
            case AnnotationLineEnding::None:
                break;

            case AnnotationLineEnding::Square:
            {
                path.addRect(-lineEndingHalfSize, -lineEndingHalfSize, lineEndingSize, lineEndingSize);
                break;
            }

            case AnnotationLineEnding::Circle:
            {
                path.addEllipse(QPointF(0, 0), lineEndingHalfSize, lineEndingHalfSize);
                break;
            }

            case AnnotationLineEnding::Diamond:
            {
                path.moveTo(0.0, -lineEndingHalfSize);
                path.lineTo(lineEndingHalfSize, 0.0);
                path.lineTo(0.0, +lineEndingHalfSize);
                path.lineTo(-lineEndingHalfSize, 0.0);
                path.closeSubpath();
                break;
            }

            case AnnotationLineEnding::OpenArrow:
            {
                path.moveTo(0.0, 0.0);
                path.lineTo(arrowAxisLength, lineEndingHalfSize);
                path.moveTo(0.0, 0.0);
                path.lineTo(arrowAxisLength, -lineEndingHalfSize);
                break;
            }

            case AnnotationLineEnding::ClosedArrow:
            {
                path.moveTo(0.0, 0.0);
                path.lineTo(arrowAxisLength, lineEndingHalfSize);
                path.lineTo(arrowAxisLength, -lineEndingHalfSize);
                path.closeSubpath();
                break;
            }

            case AnnotationLineEnding::Butt:
            {
                path.moveTo(0.0, -lineEndingHalfSize);
                path.lineTo(0.0, lineEndingHalfSize);
                break;
            }

            case AnnotationLineEnding::ROpenArrow:
            {
                path.moveTo(0.0, 0.0);
                path.lineTo(-arrowAxisLength, lineEndingHalfSize);
                path.moveTo(0.0, 0.0);
                path.lineTo(-arrowAxisLength, -lineEndingHalfSize);
                break;
            }

            case AnnotationLineEnding::RClosedArrow:
            {
                path.moveTo(0.0, 0.0);
                path.lineTo(-arrowAxisLength, lineEndingHalfSize);
                path.lineTo(-arrowAxisLength, -lineEndingHalfSize);
                path.closeSubpath();
                break;
            }

            case AnnotationLineEnding::Slash:
            {
                const PDFReal angle = 60;
                const PDFReal lineEndingHalfSize = lineEndingSize * 0.5;
                const PDFReal slashAxisLength = lineEndingHalfSize / qTan(qDegreesToRadians(angle));

                path.moveTo(-slashAxisLength, -lineEndingHalfSize);
                path.lineTo(slashAxisLength, lineEndingHalfSize);
                break;
            }

            default:
                break;
        }

        if (!path.isEmpty())
        {
            // Flip the x-axis (we are drawing endpoint)
            if (flipAxis && ending != AnnotationLineEnding::Slash)
            {
                QMatrix matrix;
                matrix.scale(-1.0, 1.0);
                path = matrix.map(path);
            }

            path.translate(point);
            path = info.LCStoGCS.map(path);
            painter.drawPath(path);
            boundingPath.addPath(path);
        }
    };

    // Remove the offset from start/end
    const PDFReal startOffset = getOffsetFromLineEnding(p1Ending);
    const PDFReal endOffset = getOffsetFromLineEnding(p2Ending);

    QLineF adjustedLine = info.transformedLine;
    adjustedLine.setP1(adjustedLine.p1() + QPointF(startOffset, 0));
    adjustedLine.setP2(adjustedLine.p2() - QPointF(endOffset, 0));

    int textVerticalOffset = 0;
    const bool drawText = !text.isEmpty();
    QPainterPath textPath;

    if (drawText)
    {
        QFont font = QApplication::font();
        font.setPixelSize(12.0);

        QFontMetricsF fontMetrics(font, painter.device());
        textVerticalOffset = fontMetrics.descent() + fontMetrics.leading();

        textPath.addText(0, 0, font, text);
        textPath = QMatrix(1, 0, 0, -1, 0, 0).map(textPath);
    }

    drawLineEnding(info.transformedLine.p1(), p1Ending, false);
    drawLineEnding(info.transformedLine.p2(), p2Ending, true);

    if (drawText && !textIsAboveLine)
    {
        // We will draw text in the line
        QRectF textBoundingRect = textPath.controlPointRect();
        QPointF center = textBoundingRect.center();
        const qreal offsetY = center.y();
        const qreal offsetX = center.x();
        textPath.translate(textOffset + adjustedLine.center() - QPointF(offsetX, offsetY));
        textBoundingRect = textPath.controlPointRect();

        const qreal textPadding = 3.0;
        const qreal textStart = textBoundingRect.left() - textPadding;
        const qreal textEnd = textBoundingRect.right() + textPadding;

        textPath = info.LCStoGCS.map(textPath);
        painter.fillPath(textPath, QBrush(painter.pen().color(), Qt::SolidPattern));
        boundingPath.addPath(textPath);

        if (textStart > adjustedLine.p1().x())
        {
            QLineF leftLine(adjustedLine.p1(), QPointF(textStart, adjustedLine.p2().y()));
            painter.drawLine(info.LCStoGCS.map(leftLine));
        }

        if (textEnd < adjustedLine.p2().x())
        {
            QLineF rightLine(QPointF(textEnd, adjustedLine.p2().y()), adjustedLine.p2());
            painter.drawLine(info.LCStoGCS.map(rightLine));
        }

        // Include whole line to the bounding path
        adjustedLine = info.LCStoGCS.map(adjustedLine);
        boundingPath.moveTo(adjustedLine.p1());
        boundingPath.lineTo(adjustedLine.p2());
    }
    else
    {
        if (drawText)
        {
            // We will draw text above the line
            QRectF textBoundingRect = textPath.controlPointRect();
            const qreal offsetY = textBoundingRect.top() - textVerticalOffset;
            const qreal offsetX = textBoundingRect.center().x();
            textPath.translate(textOffset + adjustedLine.center() - QPointF(offsetX, offsetY));

            textPath = info.LCStoGCS.map(textPath);
            painter.fillPath(textPath, QBrush(painter.pen().color(), Qt::SolidPattern));
            boundingPath.addPath(textPath);
        }

        adjustedLine = info.LCStoGCS.map(adjustedLine);
        painter.drawLine(adjustedLine);

        boundingPath.moveTo(adjustedLine.p1());
        boundingPath.lineTo(adjustedLine.p2());
    }
}

void PDFHighlightAnnotation::draw(AnnotationDrawParameters& parameters) const
{
    if (m_highlightArea.isEmpty())
    {
        // Jakub Melka: do not draw empty highlight area
        return;
    }

    QPainter& painter = *parameters.painter;
    parameters.boundingRectangle = m_highlightArea.getPath().boundingRect();

    painter.setPen(getPen());
    painter.setBrush(getBrush());
    switch (m_type)
    {
        case AnnotationType::Highlight:
        {
            painter.setCompositionMode(QPainter::CompositionMode_Multiply);
            painter.fillPath(m_highlightArea.getPath(), QBrush(getStrokeColor(), Qt::SolidPattern));
            break;
        }

        case AnnotationType::Underline:
        {
            for (const PDFAnnotationQuadrilaterals::Quadrilateral& quadrilateral : m_highlightArea.getQuadrilaterals())
            {
                QPointF p1 = quadrilateral[0];
                QPointF p2 = quadrilateral[1];
                QLineF line(p1, p2);
                painter.drawLine(line);
            }
            break;
        }

        case AnnotationType::Squiggly:
        {
            // Jakub Melka: Squiggly underline
            for (const PDFAnnotationQuadrilaterals::Quadrilateral& quadrilateral : m_highlightArea.getQuadrilaterals())
            {
                QPointF p1 = quadrilateral[0];
                QPointF p2 = quadrilateral[1];

                // Calculate length (height) of quadrilateral
                const PDFReal height = (QLineF(quadrilateral[0], quadrilateral[2]).length() + QLineF(quadrilateral[1], quadrilateral[3]).length()) * 0.5;
                const PDFReal markSize = height / 7.0;

                // We can't assume, that line is horizontal. For example, rotated text with 45° degrees
                // counterclockwise, if it is highlighted with squiggly underline, it is not horizontal line.
                // So, we must calculate line geometry and transform it.
                QLineF line(p1, p2);
                LineGeometryInfo lineGeometryInfo = LineGeometryInfo::create(line);

                bool leadingEdge = true;
                for (PDFReal x = lineGeometryInfo.transformedLine.p1().x(); x < lineGeometryInfo.transformedLine.p2().x(); x+= markSize)
                {
                    QLineF line;
                    if (leadingEdge)
                    {
                        line = QLineF(x, 0.0, x + markSize, markSize);
                    }
                    else
                    {
                        // Falling edge
                        line = QLineF(x, markSize, x + markSize, 0.0);
                    }

                    QLineF transformedLine = lineGeometryInfo.LCStoGCS.map(line);
                    painter.drawLine(transformedLine);
                    leadingEdge = !leadingEdge;
                }
            }
            break;
        }

        case AnnotationType::StrikeOut:
        {
            for (const PDFAnnotationQuadrilaterals::Quadrilateral& quadrilateral : m_highlightArea.getQuadrilaterals())
            {
                QPointF p1 = (quadrilateral[0] + quadrilateral[2]) * 0.5;
                QPointF p2 = (quadrilateral[1] + quadrilateral[3]) * 0.5;
                QLineF line(p1, p2);
                painter.drawLine(line);
            }
            break;
        }

        default:
            break;
    }

    const qreal penWidth = painter.pen().widthF();
    parameters.boundingRectangle.adjust(-penWidth, -penWidth, penWidth, penWidth);
}

std::vector<PDFAppeareanceStreams::Key> PDFLinkAnnotation::getDrawKeys() const
{
    return { PDFAppeareanceStreams::Key{ PDFAppeareanceStreams::Appearance::Down, QByteArray() } };
}

void PDFLinkAnnotation::draw(AnnotationDrawParameters& parameters) const
{
    if (parameters.key.first != PDFAppeareanceStreams::Appearance::Down ||
        m_activationRegion.isEmpty() ||
        m_highlightMode == LinkHighlightMode::None)
    {
        // Nothing to draw
        return;
    }

    QPainter& painter = *parameters.painter;
    parameters.boundingRectangle = getRectangle();

    switch (m_highlightMode)
    {
        case LinkHighlightMode::Invert:
        {
            // Invert all
            painter.setCompositionMode(QPainter::CompositionMode_Difference);
            painter.fillPath(m_activationRegion.getPath(), QBrush(Qt::white, Qt::SolidPattern));
            break;
        }

        case LinkHighlightMode::Outline:
        {
            // Invert the border
            painter.setCompositionMode(QPainter::CompositionMode_Difference);
            QPen pen = getPen();
            pen.setColor(Qt::white);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(m_activationRegion.getPath());
            break;
        }

        case LinkHighlightMode::Push:
        {
            // Draw border
            painter.setPen(getPen());
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(m_activationRegion.getPath());
            break;
        }

        default:
            Q_ASSERT(false);
            break;
    }
}

PDFAnnotationDefaultAppearance PDFAnnotationDefaultAppearance::parse(const QByteArray& string)
{
    PDFAnnotationDefaultAppearance result;
    PDFLexicalAnalyzer analyzer(string.constData(), string.constData() + string.size());

    std::vector<PDFLexicalAnalyzer::Token> tokens;

    for (PDFLexicalAnalyzer::Token token = analyzer.fetch(); token.type != PDFLexicalAnalyzer::TokenType::EndOfFile; token = analyzer.fetch())
    {
        tokens.push_back(qMove(token));
    }

    auto readNumber = [&tokens](size_t index) -> PDFReal
    {
        Q_ASSERT(index >= 0 && index < tokens.size());
        const PDFLexicalAnalyzer::Token& token = tokens[index];
        if (token.type == PDFLexicalAnalyzer::TokenType::Real ||
            token.type == PDFLexicalAnalyzer::TokenType::Integer)
        {
            return token.data.toDouble();
        }

        return 0.0;
    };

    for (size_t i = 0; i < tokens.size(); ++i)
    {
        const PDFLexicalAnalyzer::Token& token = tokens[i];
        if (token.type == PDFLexicalAnalyzer::TokenType::Command)
        {
            QByteArray command = token.data.toByteArray();
            if (command == "Tf")
            {
                if (i >= 1)
                {
                    result.m_fontSize = readNumber(i - 1);
                }
                if (i >= 2)
                {
                    result.m_fontName = tokens[i - 2].data.toByteArray();
                }
            }
            else if (command == "g" && i >= 1)
            {
                result.m_fontColor = PDFAnnotation::getDrawColorFromAnnotationColor({ readNumber(i - 1) });
            }
            else if (command == "rg" && i >= 3)
            {
                result.m_fontColor = PDFAnnotation::getDrawColorFromAnnotationColor({ readNumber(i - 3), readNumber(i - 2), readNumber(i - 1) });
            }
            else if (command == "k" && i >= 4)
            {
                result.m_fontColor = PDFAnnotation::getDrawColorFromAnnotationColor({ readNumber(i - 4), readNumber(i - 3), readNumber(i - 2), readNumber(i - 1) });
            }
        }
    }

    return result;
}

void PDFFreeTextAnnotation::draw(AnnotationDrawParameters& parameters) const
{
    QPainter& painter = *parameters.painter;
    parameters.boundingRectangle = getRectangle();

    painter.setPen(getPen());
    painter.setBrush(getBrush());

    // Draw callout line
    const PDFAnnotationCalloutLine& calloutLine = getCalloutLine();

    switch (calloutLine.getType())
    {
        case PDFAnnotationCalloutLine::Type::Invalid:
        {
            // Annotation doesn't have callout line
            break;
        }

        case PDFAnnotationCalloutLine::Type::StartEnd:
        {
            QPainterPath boundingPath;
            QLineF line(calloutLine.getPoint(0), calloutLine.getPoint(1));
            const PDFReal lineEndingSize = qMin(painter.pen().widthF() * 5.0, line.length() * 0.5);
            drawLine(LineGeometryInfo::create(line), painter, lineEndingSize,
                     getStartLineEnding(), getEndLineEnding(), boundingPath,
                     QPointF(), QString(), true);
            break;
        }

        case PDFAnnotationCalloutLine::Type::StartKneeEnd:
        {
            QPainterPath boundingPath;

            QLineF lineStart(calloutLine.getPoint(0), calloutLine.getPoint(1));
            QLineF lineEnd(calloutLine.getPoint(1), calloutLine.getPoint(2));

            PDFReal preferredLineEndingSize = painter.pen().widthF() * 5.0;
            PDFReal lineStartEndingSize = qMin(preferredLineEndingSize, lineStart.length() * 0.5);
            drawLine(LineGeometryInfo::create(lineStart), painter, lineStartEndingSize,
                     getStartLineEnding(), AnnotationLineEnding::None, boundingPath,
                     QPointF(), QString(), true);


            PDFReal lineEndEndingSize = qMin(preferredLineEndingSize, lineEnd.length() * 0.5);
            drawLine(LineGeometryInfo::create(lineEnd), painter, lineEndEndingSize,
                     AnnotationLineEnding::None, getEndLineEnding() , boundingPath,
                     QPointF(), QString(), true);
            break;
        }

        default:
        {
            Q_ASSERT(false);
            break;
        }
    }

    QRectF textRect = getTextRectangle();
    if (!textRect.isValid())
    {
        textRect = getRectangle();
    }

    painter.drawRect(textRect);

    // Draw text
    PDFAnnotationDefaultAppearance defaultAppearance = PDFAnnotationDefaultAppearance::parse(getDefaultAppearance());

    QFont font(defaultAppearance.getFontName());
    font.setPixelSize(defaultAppearance.getFontSize());
    painter.setPen(defaultAppearance.getFontColor());

    Qt::Alignment alignment = Qt::AlignTop;
    switch (getJustification())
    {
        case PDFFreeTextAnnotation::Justification::Left:
            alignment |= Qt::AlignLeft;
            break;

        case PDFFreeTextAnnotation::Justification::Centered:
            alignment |= Qt::AlignHCenter;
            break;

        case PDFFreeTextAnnotation::Justification::Right:
            alignment |= Qt::AlignRight;
            break;

        default:
            alignment |= Qt::AlignLeft;
            Q_ASSERT(false);
            break;
    }

    QTextOption option(alignment);
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    option.setUseDesignMetrics(true);

    painter.translate(textRect.left(), textRect.bottom());
    painter.scale(1.0, -1.0);
    painter.drawText(QRectF(QPointF(0, 0), textRect.size()), getContents(), option);
}

void PDFCaretAnnotation::draw(AnnotationDrawParameters& parameters) const
{
    QPainter& painter = *parameters.painter;
    parameters.boundingRectangle = getRectangle();

    QRectF caretRect = getCaretRectangle();
    if (caretRect.isEmpty())
    {
        caretRect = getRectangle();
    }

    QPointF controlPoint(caretRect.center());
    controlPoint.setY(caretRect.top());

    QPointF topPoint = controlPoint;
    topPoint.setY(caretRect.bottom());

    QPainterPath path;
    path.moveTo(caretRect.topLeft());
    path.quadTo(controlPoint, topPoint);
    path.quadTo(controlPoint, caretRect.topRight());
    path.lineTo(caretRect.topLeft());
    path.closeSubpath();

    painter.fillPath(path, QBrush(getStrokeColor(), Qt::SolidPattern));
}

void PDFInkAnnotation::draw(AnnotationDrawParameters& parameters) const
{
    QPainter& painter = *parameters.painter;
    QPainterPath path = getInkPath();

    painter.setPen(getPen());
    painter.setBrush(getBrush());

    QPainterPath boundingPath;
    QPainterPath currentPath;
    const int elementCount = path.elementCount();
    for (int i = 0; i < elementCount; ++i)
    {
        QPainterPath::Element element = path.elementAt(i);
        switch (element.type)
        {
            case QPainterPath::MoveToElement:
            {
                // Reset the path
                if (!currentPath.isEmpty())
                {
                    boundingPath.addPath(currentPath);
                    painter.drawPath(currentPath);
                    currentPath.clear();
                }
                currentPath.moveTo(element.x, element.y);
                break;
            }

            case QPainterPath::LineToElement:
            {
                const QPointF p0 = currentPath.currentPosition();
                const QPointF p1(element.x, element.y);

                QLineF line(p0, p1);
                QPointF normal = line.normalVector().p2() - p0;

                // Jakub Melka: This computation should be clarified. We use second order Bezier curves.
                // We must calculate single control point. Let B(t) is bezier curve of second order.
                // Then second derivation is B''(t) = 2(p0 - 2*Pc + p1). Second derivation curve is its
                // normal. So, we calculate normal vector of the line (which has norm equal to line length),
                // then we get following formula:
                //
                // Pc = (p0 + p1) / 2 - B''(t) / 4
                //
                // So, for bezier curves of second order, second derivative is constant (which is not surprising,
                // because second derivative of polynomial of order 2 is also a constant). Control point is then
                // used to paint the path.
                QPointF controlPoint = -normal * 0.25 + (p0 + p1) * 0.5;
                currentPath.quadTo(controlPoint, p1);
                break;
            }

            case QPainterPath::CurveToElement:
            case QPainterPath::CurveToDataElement:
            default:
                Q_ASSERT(false);
                break;
        }
    }

    // Reset the path
    if (!currentPath.isEmpty())
    {
        boundingPath.addPath(currentPath);
        painter.drawPath(currentPath);
        currentPath.clear();
    }

    const qreal penWidth = painter.pen().widthF();
    parameters.boundingRectangle = boundingPath.boundingRect();
    parameters.boundingRectangle.adjust(-penWidth, -penWidth, penWidth, penWidth);
}

void PDFStampAnnotation::draw(AnnotationDrawParameters& parameters) const
{
    QPainter& painter = *parameters.painter;

    QString text;
    QColor color(Qt::red);

    switch (m_stamp)
    {
        case Stamp::Approved:
            text = PDFTranslationContext::tr("APPROVED");
            color = Qt::green;
            break;

        case Stamp::AsIs:
            text = PDFTranslationContext::tr("AS IS");
            break;

        case Stamp::Confidential:
            text = PDFTranslationContext::tr("CONFIDENTIAL");
            break;

        case Stamp::Departmental:
            text = PDFTranslationContext::tr("DEPARTMENTAL");
            color = Qt::blue;
            break;

        case Stamp::Draft:
            text = PDFTranslationContext::tr("DRAFT");
            break;

        case Stamp::Experimental:
            text = PDFTranslationContext::tr("EXPERIMENTAL");
            color = Qt::blue;
            break;

        case Stamp::Expired:
            text = PDFTranslationContext::tr("EXPIRED");
            break;

        case Stamp::Final:
            text = PDFTranslationContext::tr("FINAL");
            break;

        case Stamp::ForComment:
            text = PDFTranslationContext::tr("FOR COMMENT");
            color = Qt::green;
            break;

        case Stamp::ForPublicRelease:
            text = PDFTranslationContext::tr("FOR PUBLIC RELEASE");
            color = Qt::green;
            break;

        case Stamp::NotApproved:
            text = PDFTranslationContext::tr("NOT APPROVED");
            break;

        case Stamp::NotForPublicRelease:
            text = PDFTranslationContext::tr("NOT FOR PUBLIC RELEASE");
            break;

        case Stamp::Sold:
            text = PDFTranslationContext::tr("SOLD");
            color = Qt::blue;
            break;

        case Stamp::TopSecret:
            text = PDFTranslationContext::tr("TOP SECRET");
            break;

        default:
            Q_ASSERT(false);
            break;
    }

    const PDFReal textHeight = 16;
    QFont font("Courier New");
    font.setBold(true);
    font.setPixelSize(textHeight);

    QFontMetricsF fontMetrics(font, painter.device());
    const qreal textWidth = fontMetrics.width(text);
    const qreal rectangleWidth = textWidth + 10;
    const qreal rectangleHeight = textHeight * 1.2;
    const qreal penWidth = 2.0;

    QRectF rectangle = getRectangle();
    rectangle.setSize(QSizeF(rectangleWidth, rectangleHeight));

    QPen pen(color);
    pen.setWidthF(penWidth);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    painter.drawRoundedRect(rectangle, 5, 5, Qt::AbsoluteSize);

    // Draw text
    QPainterPath textPath;
    textPath.addText(0, 0, font, text);
    textPath = QMatrix(1, 0, 0, -1, 0, 0).map(textPath);

    QPointF center = textPath.boundingRect().center();
    textPath.translate(rectangle.center() - center);
    painter.fillPath(textPath, QBrush(color, Qt::SolidPattern));

    parameters.boundingRectangle = rectangle;
    parameters.boundingRectangle.adjust(-penWidth, -penWidth, penWidth, penWidth);
}

void PDFAnnotation::drawCharacterSymbol(QString text, PDFReal opacity, AnnotationDrawParameters& parameters) const
{
    QColor strokeColor = QColor::fromRgbF(0.0, 0.0, 0.0, opacity);

    constexpr const PDFReal rectSize = 24.0;
    QPainter& painter = *parameters.painter;
    QRectF rectangle = getRectangle();
    rectangle.setSize(QSizeF(rectSize, rectSize));

    QFont font = QApplication::font();
    font.setPixelSize(16.0);

    QPainterPath textPath;
    textPath.addText(0.0, 0.0, font, text);
    textPath = QMatrix(1.0, 0.0, 0.0, -1.0, 0.0, 0.0).map(textPath);
    QRectF textBoundingRect = textPath.boundingRect();
    QPointF offset = rectangle.center() - textBoundingRect.center();
    textPath.translate(offset);
    painter.fillPath(textPath, QBrush(strokeColor, Qt::SolidPattern));

    parameters.boundingRectangle = rectangle;
}

void PDFFileAttachmentAnnotation::draw(AnnotationDrawParameters& parameters) const
{
    QString text = "?";
    switch (getIcon())
    {
        case FileAttachmentIcon::Graph:
            text = QString::fromUtf16(u"\U0001F4C8");
            break;
        case FileAttachmentIcon::Paperclip:
            text = QString::fromUtf16(u"\U0001F4CE");
            break;
        case FileAttachmentIcon::PushPin:
            text = QString::fromUtf16(u"\U0001F4CC");
            break;
        case FileAttachmentIcon::Tag:
            text = QString::fromUtf16(u"\U0001F3F7");
            break;

        default:
            Q_ASSERT(false);
            break;
    }

    drawCharacterSymbol(text, getOpacity(), parameters);
}

void PDFSoundAnnotation::draw(AnnotationDrawParameters& parameters) const
{
    QString text = "?";
    switch (getIcon())
    {
        case Icon::Speaker:
            text = QString::fromUtf16(u"\U0001F508");
            break;
        case Icon::Microphone:
            text = QString::fromUtf16(u"\U0001F3A4");
            break;

        default:
            Q_ASSERT(false);
            break;
    }

    drawCharacterSymbol(text, getOpacity(), parameters);
}

const PDFAnnotationManager::PageAnnotation* PDFAnnotationManager::PageAnnotations::getPopupAnnotation(const PageAnnotation& pageAnnotation) const
{
    const PDFMarkupAnnotation* markupAnnotation = pageAnnotation.annotation->asMarkupAnnotation();
    if (markupAnnotation)
    {
        const PDFObjectReference popupAnnotation = markupAnnotation->getPopupAnnotation();
        auto it = std::find_if(annotations.cbegin(), annotations.cend(), [this, popupAnnotation](const PageAnnotation& pa) { return pa.annotation->getSelfReference() == popupAnnotation; });
        if (it != annotations.cend())
        {
            return &*it;
        }
    }

    return nullptr;
}

std::vector<const PDFAnnotationManager::PageAnnotation*> PDFAnnotationManager::PageAnnotations::getReplies(const PageAnnotation& pageAnnotation) const
{
    std::vector<const PageAnnotation*> result;

    const PDFObjectReference reference = pageAnnotation.annotation->getSelfReference();
    for (size_t i = 0, count = annotations.size(); i < count; ++i)
    {
        const PageAnnotation& currentAnnotation = annotations[i];
        if (currentAnnotation.annotation->isReplyTo())
        {
            const PDFMarkupAnnotation* markupAnnotation = currentAnnotation.annotation->asMarkupAnnotation();
            Q_ASSERT(markupAnnotation);

            if (markupAnnotation->getInReplyTo() == reference)
            {
                result.push_back(&currentAnnotation);
            }
        }
    }

    auto comparator = [](const PageAnnotation* l, const PageAnnotation* r)
    {
        QDateTime leftDateTime = l->annotation->getLastModifiedDateTime();
        QDateTime rightDateTime = r->annotation->getLastModifiedDateTime();

        if (const PDFMarkupAnnotation* markupL = l->annotation->asMarkupAnnotation())
        {
            leftDateTime = markupL->getCreationDate();
        }

        if (const PDFMarkupAnnotation* markupR = r->annotation->asMarkupAnnotation())
        {
            rightDateTime = markupR->getCreationDate();
        }

        return leftDateTime < rightDateTime;
    };
    std::sort(result.begin(), result.end(), comparator);

    return result;
}

}   // namespace pdf
