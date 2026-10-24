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

#include "pdfappannotationcreator.h"
#include "pdfannotationstylerepository.h"
#include "pdfappannotationcontroller.h"
#include "pdfappannotationcreatedialogs.h"
#include "pdfappannotationcreatetools.h"
#include "pdfappannotationstylestorage.h"
#include "pdfappannotationtoolbar.h"
#include "pdfappcommand.h"
#include "pdfappcommentpopup.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentstatusbar.h"
#include "pdfappdocumentview.h"
#include "pdfappmainwindow.h"
#include "pdfappmarkupbar.h"
#include "pdfdatabasenotifier.h"
#include "pdfsettingdefinitions.h"
#include "pdfsettingsrepository.h"

#include "pdfadvancedtools.h"
#include "pdfannotationstyle.h"
#include "pdfcompiler.h"
#include "pdfdrawspacecontroller.h"
#include "pdfdrawwidget.h"
#include "pdfutils.h"
#include "pdfwidgetsnapshot.h"

#include "pdfcomponentpanels.h"

#include <QEvent>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMouseEvent>

#include <cmath>

namespace pdf
{

/// Values of the creator
class PDFAppAnnotationCreatorHelper
{
public:
    PDFAppAnnotationCreatorHelper() = delete;

    /// Size of the area of a new sticky note, at which its compose popup opens
    static constexpr PDFReal STICKY_NOTE_ANCHOR = 24.0;

    /// Default width of the border of a new text box
    static constexpr PDFReal DEFAULT_BORDER_WIDTH = 1.0;

    /// Largest stored border width of a text box
    static constexpr PDFReal MAXIMUM_BORDER_WIDTH = 12.0;

    static QColor getJsonColor(const QJsonObject& object, const QString& key)
    {
        const QJsonValue value = object.value(key);
        if (!value.isString())
        {
            return QColor();
        }
        const QString text = value.toString();
        if (text.size() != 7 || !text.startsWith(QLatin1Char('#')))
        {
            return QColor();
        }
        const QColor color = QColor::fromString(text);
        return color.isValid() ? color : QColor();
    }

    static PDFAppAnnotationStyleStorage* getStorage(const PDFAppContext* context)
    {
        return context ? context->getAnnotationStyleStorage() : nullptr;
    }
};

// -------------------------------------------------------- PDFAppAnnotationTools

std::vector<PDFAppAnnotationToolKind> PDFAppAnnotationTools::getKinds()
{
    using Kind = PDFAppAnnotationToolKind;
    return { Kind::Highlight, Kind::Underline, Kind::Strikeout, Kind::Squiggly, Kind::HighlightComment,
             Kind::TextBox, Kind::StickyNote, Kind::Line, Kind::Polyline, Kind::Rectangle, Kind::Polygon,
             Kind::Ellipse, Kind::Freehand, Kind::WebLink, Kind::PageLink, Kind::Stamp, Kind::DeleteTool };
}

QString PDFAppAnnotationTools::getCommandId(PDFAppAnnotationToolKind kind)
{
    using Kind = PDFAppAnnotationToolKind;
    using C = PDFAppCommands;
    switch (kind)
    {
        case Kind::Highlight:
            return C::annotateHighlight();
        case Kind::Underline:
            return C::annotateUnderline();
        case Kind::Strikeout:
            return C::annotateStrikeout();
        case Kind::Squiggly:
            return C::annotateSquiggly();
        case Kind::HighlightComment:
            return C::annotateHighlightComment();
        case Kind::TextBox:
            return C::insertTextBox();
        case Kind::StickyNote:
            return C::insertStickyNote();
        case Kind::Line:
            return C::insertLine();
        case Kind::Polyline:
            return C::insertPolyline();
        case Kind::Rectangle:
            return C::insertRectangle();
        case Kind::Polygon:
            return C::insertPolygon();
        case Kind::Ellipse:
            return C::insertEllipse();
        case Kind::Freehand:
            return C::insertFreehand();
        case Kind::WebLink:
            return C::insertWebLink();
        case Kind::PageLink:
            return C::insertPageLink();
        case Kind::Stamp:
            return C::insertStamp();
        case Kind::DeleteTool:
            return C::annotateDeleteTool();
        case Kind::None:
            break;
    }
    return QString();
}

PDFAppAnnotationToolKind PDFAppAnnotationTools::getKind(const QString& commandId)
{
    for (PDFAppAnnotationToolKind kind : getKinds())
    {
        if (getCommandId(kind) == commandId)
        {
            return kind;
        }
    }
    return PDFAppAnnotationToolKind::None;
}

QString PDFAppAnnotationTools::getIconName(PDFAppAnnotationToolKind kind)
{
    using Kind = PDFAppAnnotationToolKind;
    switch (kind)
    {
        case Kind::Highlight:
            return QStringLiteral("highlighter");
        case Kind::Underline:
            return QStringLiteral("underline");
        case Kind::Strikeout:
            return QStringLiteral("strikethrough");
        case Kind::Squiggly:
            return QStringLiteral("squiggly");
        case Kind::HighlightComment:
            return QStringLiteral("highlight-comment");
        case Kind::TextBox:
            return QStringLiteral("text-box");
        case Kind::StickyNote:
            return QStringLiteral("note-note");
        case Kind::Line:
            return QStringLiteral("line");
        case Kind::Polyline:
            return QStringLiteral("polyline");
        case Kind::Rectangle:
            return QStringLiteral("square");
        case Kind::Polygon:
            return QStringLiteral("polygon");
        case Kind::Ellipse:
            return QStringLiteral("circle");
        case Kind::Freehand:
            return QStringLiteral("scribble");
        case Kind::WebLink:
            return QStringLiteral("link");
        case Kind::PageLink:
            return QStringLiteral("link-page");
        case Kind::Stamp:
            return QStringLiteral("stamp");
        case Kind::DeleteTool:
            return QStringLiteral("eraser");
        case Kind::None:
            break;
    }
    return QString();
}

QString PDFAppAnnotationTools::getName(PDFAppAnnotationToolKind kind)
{
    using Kind = PDFAppAnnotationToolKind;
    switch (kind)
    {
        case Kind::Highlight:
            return tr("Highlight");
        case Kind::Underline:
            return tr("Underline");
        case Kind::Strikeout:
            return tr("Strikeout");
        case Kind::Squiggly:
            return tr("Squiggly");
        case Kind::HighlightComment:
            return tr("Highlight and comment");
        case Kind::TextBox:
            return tr("Text box");
        case Kind::StickyNote:
            return tr("Sticky note");
        case Kind::Line:
            return tr("Line");
        case Kind::Polyline:
            return tr("Polyline");
        case Kind::Rectangle:
            return tr("Rectangle");
        case Kind::Polygon:
            return tr("Polygon");
        case Kind::Ellipse:
            return tr("Ellipse");
        case Kind::Freehand:
            return tr("Freehand");
        case Kind::WebLink:
            return tr("Web link");
        case Kind::PageLink:
            return tr("Link to page");
        case Kind::Stamp:
            return tr("Stamp");
        case Kind::DeleteTool:
            return tr("Delete annotations");
        case Kind::None:
            break;
    }
    return QString();
}

QString PDFAppAnnotationTools::getStatusHint(PDFAppAnnotationToolKind kind)
{
    using Kind = PDFAppAnnotationToolKind;
    switch (kind)
    {
        case Kind::Highlight:
        case Kind::Underline:
        case Kind::Strikeout:
        case Kind::Squiggly:
            //: Status bar of a text markup tool (tools.md 9.8): Highlight: drag over text. Press Esc to finish.
            return tr("%1: drag over text. Press Esc to finish.").arg(getName(kind));
        case Kind::HighlightComment:
            return tr("Highlight and comment: drag over text, then write the comment. Press Esc to finish.");
        case Kind::TextBox:
            return tr("Text box: drag a rectangle on the page, then type the text. Press Esc to finish.");
        case Kind::StickyNote:
            return tr("Sticky note: click on the page, then write the comment. Press Esc to finish.");
        case Kind::Line:
            return tr("Line: click the start and the end of the line. Press Esc to finish.");
        case Kind::Polyline:
            return tr("Polyline: click the points, press Enter to complete the line. Press Esc to finish.");
        case Kind::Rectangle:
            return tr("Rectangle: drag a rectangle on the page. Press Esc to finish.");
        case Kind::Polygon:
            return tr("Polygon: click the points, press Enter to close the shape. Press Esc to finish.");
        case Kind::Ellipse:
            return tr("Ellipse: drag a rectangle on the page. Press Esc to finish.");
        case Kind::Freehand:
            return tr("Freehand: draw with the mouse. Press Esc to finish.");
        case Kind::WebLink:
            return tr("Web link: drag a rectangle over the linked area, then enter the address. Press Esc to finish.");
        case Kind::PageLink:
            return tr("Link to page: draw the link area, then go to the target page and click it. Press Esc to finish.");
        case Kind::Stamp:
            return tr("Stamp: click on the page to place the stamp. Press Esc to finish.");
        case Kind::DeleteTool:
            return tr("Delete annotations: click an annotation or drag a rectangle around annotations. Press Esc to finish.");
        case Kind::None:
            break;
    }
    return QString();
}

QString PDFAppAnnotationTools::getBarHint(PDFAppAnnotationToolKind kind)
{
    using Kind = PDFAppAnnotationToolKind;
    switch (kind)
    {
        case Kind::TextBox:
            // hint 1168:26448
            return tr("Drag a rectangle on the page, then type the text");
        case Kind::WebLink:
            return tr("Drag a rectangle over the linked area, then enter the address");
        case Kind::Stamp:
            return tr("Click on the page to place the stamp");
        case Kind::DeleteTool:
            return tr("Click an annotation or drag a rectangle around annotations");
        default:
            break;
    }
    return QString();
}

QString PDFAppAnnotationTools::getUndoDescription(PDFAppAnnotationToolKind kind)
{
    using Kind = PDFAppAnnotationToolKind;
    switch (kind)
    {
        case Kind::Highlight:
            return tr("Highlight text");
        case Kind::Underline:
            return tr("Underline text");
        case Kind::Strikeout:
            return tr("Strike out text");
        case Kind::Squiggly:
            return tr("Squiggly underline text");
        case Kind::HighlightComment:
            return tr("Highlight and comment");
        case Kind::TextBox:
            return tr("Add text box");
        case Kind::StickyNote:
            return tr("Add sticky note");
        case Kind::Line:
            return tr("Add line");
        case Kind::Polyline:
            return tr("Add polyline");
        case Kind::Rectangle:
            return tr("Add rectangle");
        case Kind::Polygon:
            return tr("Add polygon");
        case Kind::Ellipse:
            return tr("Add ellipse");
        case Kind::Freehand:
            return tr("Add freehand drawing");
        case Kind::WebLink:
            return tr("Add web link");
        case Kind::PageLink:
            return tr("Add link to page");
        case Kind::Stamp:
            return tr("Add stamp");
        case Kind::DeleteTool:
            return tr("Delete annotations");
        case Kind::None:
            break;
    }
    return QString();
}

bool PDFAppAnnotationTools::isMarkup(PDFAppAnnotationToolKind kind)
{
    return getMarkupType(kind) != AnnotationType::Invalid;
}

AnnotationType PDFAppAnnotationTools::getMarkupType(PDFAppAnnotationToolKind kind)
{
    using Kind = PDFAppAnnotationToolKind;
    switch (kind)
    {
        case Kind::Highlight:
        case Kind::HighlightComment:
            return AnnotationType::Highlight;
        case Kind::Underline:
            return AnnotationType::Underline;
        case Kind::Strikeout:
            return AnnotationType::StrikeOut;
        case Kind::Squiggly:
            return AnnotationType::Squiggly;
        default:
            break;
    }
    return AnnotationType::Invalid;
}

PDFAppAnnotationToolKind PDFAppAnnotationTools::getMarkupKind(AnnotationType type)
{
    switch (type)
    {
        case AnnotationType::Highlight:
            return PDFAppAnnotationToolKind::Highlight;
        case AnnotationType::Underline:
            return PDFAppAnnotationToolKind::Underline;
        case AnnotationType::StrikeOut:
            return PDFAppAnnotationToolKind::Strikeout;
        case AnnotationType::Squiggly:
            return PDFAppAnnotationToolKind::Squiggly;
        default:
            break;
    }
    return PDFAppAnnotationToolKind::None;
}

bool PDFAppAnnotationTools::hasColor(PDFAppAnnotationToolKind kind)
{
    return isMarkup(kind) || kind == PDFAppAnnotationToolKind::StickyNote || hasWidth(kind);
}

bool PDFAppAnnotationTools::hasFill(PDFAppAnnotationToolKind kind)
{
    using Kind = PDFAppAnnotationToolKind;
    return kind == Kind::Rectangle || kind == Kind::Polygon || kind == Kind::Ellipse;
}

bool PDFAppAnnotationTools::hasWidth(PDFAppAnnotationToolKind kind)
{
    using Kind = PDFAppAnnotationToolKind;
    switch (kind)
    {
        case Kind::Line:
        case Kind::Polyline:
        case Kind::Rectangle:
        case Kind::Polygon:
        case Kind::Ellipse:
        case Kind::Freehand:
            return true;
        default:
            break;
    }
    return false;
}

bool PDFAppAnnotationTools::isEngineTool(PDFAppAnnotationToolKind kind)
{
    return kind != PDFAppAnnotationToolKind::None && !isMarkup(kind);
}

std::vector<TextAnnotationIcon> PDFAppAnnotationTools::getStickyNoteIcons()
{
    // tiles 196:3191: Comment, Note, Help, Key, Insert, Paragraph, New paragraph
    return { TextAnnotationIcon::Comment, TextAnnotationIcon::Note, TextAnnotationIcon::Help, TextAnnotationIcon::Key,
             TextAnnotationIcon::Insert, TextAnnotationIcon::Paragraph, TextAnnotationIcon::NewParagraph };
}

QString PDFAppAnnotationTools::getStickyNoteIconName(TextAnnotationIcon icon)
{
    switch (icon)
    {
        case TextAnnotationIcon::Comment:
            return QStringLiteral("message-square");
        case TextAnnotationIcon::Note:
            return QStringLiteral("note-note");
        case TextAnnotationIcon::Help:
            return QStringLiteral("note-help");
        case TextAnnotationIcon::Key:
            return QStringLiteral("note-key");
        case TextAnnotationIcon::Insert:
            return QStringLiteral("note-insert");
        case TextAnnotationIcon::Paragraph:
            return QStringLiteral("note-paragraph");
        case TextAnnotationIcon::NewParagraph:
            return QStringLiteral("note-new-paragraph");
    }
    return QStringLiteral("message-square");
}

QString PDFAppAnnotationTools::getStickyNoteName(TextAnnotationIcon icon)
{
    switch (icon)
    {
        case TextAnnotationIcon::Comment:
            return tr("Comment");
        case TextAnnotationIcon::Note:
            return tr("Note");
        case TextAnnotationIcon::Help:
            return tr("Help");
        case TextAnnotationIcon::Key:
            return tr("Key");
        case TextAnnotationIcon::Insert:
            return tr("Insert");
        case TextAnnotationIcon::Paragraph:
            return tr("Paragraph");
        case TextAnnotationIcon::NewParagraph:
            return tr("New paragraph");
    }
    return tr("Comment");
}

std::vector<Stamp> PDFAppAnnotationTools::getStamps()
{
    return { Stamp::Approved, Stamp::AsIs, Stamp::Confidential, Stamp::Departmental, Stamp::Draft, Stamp::Experimental, Stamp::Expired,
             Stamp::Final, Stamp::ForComment, Stamp::ForPublicRelease, Stamp::NotApproved, Stamp::NotForPublicRelease, Stamp::Sold, Stamp::TopSecret };
}

QString PDFAppAnnotationTools::getStampName(Stamp stamp)
{
    switch (stamp)
    {
        case Stamp::Approved:
            return tr("Approved");
        case Stamp::AsIs:
            return tr("As is");
        case Stamp::Confidential:
            return tr("Confidential");
        case Stamp::Departmental:
            return tr("Departmental");
        case Stamp::Draft:
            return tr("Draft");
        case Stamp::Experimental:
            return tr("Experimental");
        case Stamp::Expired:
            return tr("Expired");
        case Stamp::Final:
            return tr("Final");
        case Stamp::ForComment:
            return tr("For comment");
        case Stamp::ForPublicRelease:
            return tr("For public release");
        case Stamp::NotApproved:
            return tr("Not approved");
        case Stamp::NotForPublicRelease:
            return tr("Not for public release");
        case Stamp::Sold:
            return tr("Sold");
        case Stamp::TopSecret:
            return tr("Top secret");
    }
    return tr("Approved");
}

PDFAccentHue PDFAppAnnotationTools::getStampHue(Stamp stamp)
{
    // stamps 196:3285 (Stamp chip 190:779)
    switch (stamp)
    {
        case Stamp::Approved:
        case Stamp::Final:
        case Stamp::ForPublicRelease:
            return PDFAccentHue::Green;
        case Stamp::AsIs:
        case Stamp::Departmental:
        case Stamp::Draft:
        case Stamp::ForComment:
            return PDFAccentHue::Blue;
        case Stamp::Experimental:
        case Stamp::Sold:
            return PDFAccentHue::Amber;
        case Stamp::Confidential:
        case Stamp::Expired:
        case Stamp::NotApproved:
        case Stamp::NotForPublicRelease:
        case Stamp::TopSecret:
            return PDFAccentHue::Red;
    }
    return PDFAccentHue::Green;
}

std::vector<DestinationType> PDFAppAnnotationTools::getPageLinkTargets()
{
    return { DestinationType::Fit, DestinationType::FitH, DestinationType::FitV, DestinationType::FitR, DestinationType::XYZ };
}

bool PDFAppAnnotationTools::isPageLinkTarget(DestinationType type)
{
    switch (type)
    {
        case DestinationType::Fit:
        case DestinationType::FitH:
        case DestinationType::FitV:
        case DestinationType::FitR:
        case DestinationType::XYZ:
            return true;
        default:
            break;
    }
    return false;
}

QString PDFAppAnnotationTools::getPageLinkTargetName(DestinationType type)
{
    switch (type)
    {
        case DestinationType::FitH:
            return tr("Page width");
        case DestinationType::FitV:
            return tr("Page height");
        case DestinationType::FitR:
            return tr("Area");
        case DestinationType::XYZ:
            return tr("Position");
        default:
            break;
    }
    return tr("Whole page");
}

QString PDFAppAnnotationTools::getPageLinkTargetDescription(DestinationType type)
{
    switch (type)
    {
        case DestinationType::FitH:
            return tr("Click a place – the page width is shown from that height");
        case DestinationType::FitV:
            return tr("Click a place – the page height is shown from that edge");
        case DestinationType::FitR:
            return tr("Draw a rectangle – exactly that area is shown");
        case DestinationType::XYZ:
            return tr("Click a place – it is shown at the top-left corner");
        default:
            break;
    }
    return tr("Click the target page – the whole page is shown");
}

QString PDFAppAnnotationTools::getPageLinkTargetIconName(DestinationType type)
{
    switch (type)
    {
        case DestinationType::FitH:
            return QStringLiteral("fit-width");
        case DestinationType::FitV:
            return QStringLiteral("fit-height");
        case DestinationType::FitR:
            return QStringLiteral("select-rect");
        case DestinationType::XYZ:
            return QStringLiteral("crosshair");
        default:
            break;
    }
    return QStringLiteral("maximize");
}

QString PDFAppAnnotationTools::getPageLinkInstruction(DestinationType type)
{
    // instruction 1170:27204
    return type == DestinationType::FitR ? tr("Go to the target page and draw the area") : tr("Go to the target page and click it");
}

std::vector<PDFReal> PDFAppAnnotationTools::getPenWidths()
{
    return { 0.5, 1.0, 2.0, 3.0, 4.0, 6.0, 8.0, 12.0 };
}

// ------------------------------------------------- PDFAppAnnotationToolSettings

QColor PDFAppAnnotationToolSettings::getCanonicalColor(PDFThemeColors::Color token)
{
    for (const PDFColorSwatchRow::Entry& entry : PDFColorSwatchRow::getAnnotationColors(PDFColorSwatchRow::Option::White))
    {
        if (entry.token == token)
        {
            return entry.canonicalColor;
        }
    }
    return QColor(Qt::black);
}

QColor PDFAppAnnotationToolSettings::getDefaultMarkupColor(AnnotationType type)
{
    switch (type)
    {
        case AnnotationType::Highlight:
            return getCanonicalColor(PDFThemeColors::AnnotationYellow);
        case AnnotationType::Underline:
            return getCanonicalColor(PDFThemeColors::AnnotationBlue);
        default:
            break;
    }
    return getCanonicalColor(PDFThemeColors::AnnotationRed);
}

QString PDFAppAnnotationToolSettings::getMarkupStyleId(AnnotationType type)
{
    switch (type)
    {
        case AnnotationType::Underline:
            return PDFAnnotationStyleSettings::STYLE_UNDERLINE;
        case AnnotationType::Squiggly:
            return PDFAnnotationStyleSettings::STYLE_SQUIGGLY;
        case AnnotationType::StrikeOut:
            return PDFAnnotationStyleSettings::STYLE_STRIKEOUT;
        default:
            break;
    }
    return PDFAnnotationStyleSettings::STYLE_HIGHLIGHT;
}

QColor PDFAppAnnotationToolSettings::getMarkupColor(AnnotationType type)
{
    PDFAnnotationStyle defaultStyle;
    defaultStyle.strokeColor = getDefaultMarkupColor(type);
    return PDFAnnotationStyleSettings::getStyle(getMarkupStyleId(type), defaultStyle).strokeColor;
}

void PDFAppAnnotationToolSettings::setMarkupColor(AnnotationType type, const QColor& color)
{
    if (!color.isValid() || !PDFAppAnnotationOperations::isTextMarkupType(type))
    {
        return;
    }

    PDFAnnotationStyle defaultStyle;
    defaultStyle.strokeColor = getDefaultMarkupColor(type);
    PDFAnnotationStyle style = PDFAnnotationStyleSettings::getStyle(getMarkupStyleId(type), defaultStyle);
    style.strokeColor = color;
    PDFAnnotationStyleSettings::setStyle(getMarkupStyleId(type), style);
}

AnnotationType PDFAppAnnotationToolSettings::getLastMarkupType(const PDFAppContext* context)
{
    const PDFAppAnnotationStyleStorage* storage = PDFAppAnnotationCreatorHelper::getStorage(context);
    return storage ? storage->getLastMarkupType(AnnotationType::Highlight) : AnnotationType::Highlight;
}

void PDFAppAnnotationToolSettings::setLastMarkupType(const PDFAppContext* context, AnnotationType type)
{
    if (PDFAppAnnotationStyleStorage* storage = PDFAppAnnotationCreatorHelper::getStorage(context))
    {
        storage->setLastMarkupType(type);
    }
}

TextAnnotationIcon PDFAppAnnotationToolSettings::getStickyNoteIcon(const PDFAppContext* context)
{
    const PDFAppAnnotationStyleStorage* storage = PDFAppAnnotationCreatorHelper::getStorage(context);
    return storage ? storage->getStickyNoteIcon(TextAnnotationIcon::Comment) : TextAnnotationIcon::Comment;
}

void PDFAppAnnotationToolSettings::setStickyNoteIcon(const PDFAppContext* context, TextAnnotationIcon icon)
{
    if (PDFAppAnnotationStyleStorage* storage = PDFAppAnnotationCreatorHelper::getStorage(context))
    {
        storage->setStickyNoteIcon(icon);
    }
}

QColor PDFAppAnnotationToolSettings::getStickyNoteColor()
{
    PDFAnnotationStyle defaultStyle;
    defaultStyle.strokeColor = getCanonicalColor(PDFThemeColors::AnnotationYellow);
    return PDFAnnotationStyleSettings::getStyle(styleStickyNote(), defaultStyle).strokeColor;
}

void PDFAppAnnotationToolSettings::setStickyNoteColor(const QColor& color)
{
    if (!color.isValid())
    {
        return;
    }

    PDFAnnotationStyle style;
    style.strokeColor = color;
    PDFAnnotationStyleSettings::setStyle(styleStickyNote(), style);
}

Stamp PDFAppAnnotationToolSettings::getStamp(const PDFAppContext* context)
{
    const PDFAppAnnotationStyleStorage* storage = PDFAppAnnotationCreatorHelper::getStorage(context);
    return storage ? storage->getStamp(Stamp::Approved) : Stamp::Approved;
}

void PDFAppAnnotationToolSettings::setStamp(const PDFAppContext* context, Stamp stamp)
{
    if (PDFAppAnnotationStyleStorage* storage = PDFAppAnnotationCreatorHelper::getStorage(context))
    {
        storage->setStamp(stamp);
    }
}

PDFFreeTextStyle PDFAppAnnotationToolSettings::getFreeTextStyle(const PDFAppContext* context)
{
    PDFFreeTextStyle defaultStyle;
    defaultStyle.fontFamily = QStringLiteral("Helvetica");
    defaultStyle.fontSize = 11.0;
    defaultStyle.textColor = getCanonicalColor(PDFThemeColors::AnnotationBlack);
    const PDFAppAnnotationStyleStorage* storage = PDFAppAnnotationCreatorHelper::getStorage(context);
    return storage ? storage->getFreeTextStyle(defaultStyle, nullptr) : defaultStyle;
}

void PDFAppAnnotationToolSettings::setFreeTextStyle(const PDFAppContext* context, const PDFFreeTextStyle& style)
{
    if (PDFAppAnnotationStyleStorage* storage = PDFAppAnnotationCreatorHelper::getStorage(context))
    {
        storage->setFreeTextStyle(style, true);
    }
}

void PDFAppAnnotationToolSettings::getFreeTextBorder(const PDFAppContext* context, QColor* color, PDFReal* width)
{
    using H = PDFAppAnnotationCreatorHelper;

    // Default: no border color (Kind=None of 1168:26564), width 1
    QColor borderColor;
    PDFReal borderWidth = H::DEFAULT_BORDER_WIDTH;
    const PDFAppAnnotationStyleStorage* storage = H::getStorage(context);
    const std::optional<QJsonObject> value = storage ? storage->getRepository().getValue(rowFreeTextBorder()) : std::nullopt;
    if (value)
    {
        borderColor = H::getJsonColor(*value, QStringLiteral("color"));
        const QJsonValue widthValue = value->value(QStringLiteral("width"));
        if (widthValue.isDouble() && std::isfinite(widthValue.toDouble()))
        {
            borderWidth = qBound(0.0, widthValue.toDouble(), H::MAXIMUM_BORDER_WIDTH);
        }
    }

    if (color)
    {
        *color = borderColor;
    }
    if (width)
    {
        *width = borderWidth;
    }
}

void PDFAppAnnotationToolSettings::setFreeTextBorder(const PDFAppContext* context, const QColor& color, PDFReal width)
{
    using H = PDFAppAnnotationCreatorHelper;

    PDFAppAnnotationStyleStorage* storage = H::getStorage(context);
    if (!storage)
    {
        return;
    }

    QJsonObject value;
    value.insert(QStringLiteral("color"), color.isValid() ? QJsonValue(color.name(QColor::HexRgb)) : QJsonValue(QJsonValue::Null));
    value.insert(QStringLiteral("width"), std::isfinite(width) ? qBound(0.0, width, H::MAXIMUM_BORDER_WIDTH) : H::DEFAULT_BORDER_WIDTH);
    storage->getRepository().setValue(rowFreeTextBorder(), value);
}

DestinationType PDFAppAnnotationToolSettings::getPageLinkTarget(const PDFAppContext* context)
{
    // Whole page (Fit) is the default of the Target menu (owner decision O20)
    const PDFAppAnnotationStyleStorage* storage = PDFAppAnnotationCreatorHelper::getStorage(context);
    const DestinationType type = storage ? storage->getPageLinkDestinationType(DestinationType::Fit) : DestinationType::Fit;
    return PDFAppAnnotationTools::isPageLinkTarget(type) ? type : DestinationType::Fit;
}

bool PDFAppAnnotationToolSettings::isPageLinkKeepZoom(const PDFAppContext* context)
{
    const PDFAppAnnotationStyleStorage* storage = PDFAppAnnotationCreatorHelper::getStorage(context);
    const std::optional<QJsonObject> value = storage ? storage->getRepository().getValue(PDFAnnotationStyleRepository::toolPageLink()) : std::nullopt;
    return value && value->value(QStringLiteral("keepZoom")).toBool(false);
}

void PDFAppAnnotationToolSettings::setPageLinkTarget(const PDFAppContext* context, DestinationType type, bool keepZoom)
{
    PDFAppAnnotationStyleStorage* storage = PDFAppAnnotationCreatorHelper::getStorage(context);
    if (!storage || !PDFAppAnnotationTools::isPageLinkTarget(type))
    {
        return;
    }

    // One row {destinationType, keepZoom} (the storage writes only the type)
    QJsonObject value;
    value.insert(QStringLiteral("destinationType"), PDFAppAnnotationStyleStorage::getDestinationTypeName(type));
    value.insert(QStringLiteral("keepZoom"), keepZoom);
    storage->getRepository().setValue(PDFAnnotationStyleRepository::toolPageLink(), value);
}

// ------------------------------------------------------ PDFAppAnnotationCreator

PDFAppAnnotationCreator::PDFAppAnnotationCreator(PDFAppDocumentView* view, QObject* parent) :
    BaseClass(parent),
    m_view(view)
{
    setObjectName(QStringLiteral("annotationCreator"));

    if (view)
    {
        m_controller = view->getAnnotationController();
        m_toolManager = view->getToolManager();
        m_canvasWidget = getCanvasWidget();
        connect(view, &PDFAppDocumentView::viewChanged, this, &PDFAppAnnotationCreator::onViewChanged);
    }

    createTools();

    if (m_controller)
    {
        connect(m_controller.data(), &PDFAppAnnotationController::activeToolChanged, this, &PDFAppAnnotationCreator::onActiveToolChanged);
        connect(m_controller.data(), &PDFAppAnnotationController::creationRequested, this, &PDFAppAnnotationCreator::onCreationRequested);
        connect(m_controller.data(), &PDFAppAnnotationController::editingAllowedChanged, this, &PDFAppAnnotationCreator::onEditingAllowedChanged);
        connect(m_controller.data(), &PDFAppAnnotationController::changeCommitted, this, &PDFAppAnnotationCreator::onChangeCommitted);
    }

    if (PDFSelectTextTool* selectTextTool = getSelectTextTool())
    {
        connect(selectTextTool, &PDFSelectTextTool::selectionChanged, this, &PDFAppAnnotationCreator::onTextSelectionChanged);
    }

    PDFAppContext* context = getContext();
    if (context && context->getNotifier())
    {
        connect(context->getNotifier(), &PDFDatabaseNotifier::changed, this, &PDFAppAnnotationCreator::onDatabaseChanged);
        connect(context->getNotifier(), &PDFDatabaseNotifier::everythingChanged, this, &PDFAppAnnotationCreator::onSettingsChanged);
    }

    if (m_canvasWidget)
    {
        m_canvasWidget->installEventFilter(this);
    }
}

PDFAppAnnotationCreator::~PDFAppAnnotationCreator()
{
    // Nothing of the engine calls the creator any more
    if (m_controller)
    {
        m_controller->disconnect(this);
    }
    if (m_canvasWidget)
    {
        m_canvasWidget->removeEventFilter(this);
    }
    if (PDFSelectTextTool* selectTextTool = getSelectTextTool())
    {
        selectTextTool->disconnect(this);
        selectTextTool->setAnnotationsSelectable(true);
    }
    // The comment popup is not created here (findChild - the view may be dying)
    if (PDFAppCommentPopup* popup = m_view ? m_view->findChild<PDFAppCommentPopup*>() : nullptr)
    {
        popup->disconnect(this);
        if (m_composeKind != ComposeKind::None)
        {
            popup->cancel();
        }
    }

    // An active tool of the creator is turned off before the tools die (they are
    // children of the creator); the text selection becomes the tool of the view
    PDFWidgetTool* activeTool = m_toolManager ? m_toolManager->getActiveTool() : nullptr;
    if (activeTool && getKindOfTool(activeTool) != PDFAppAnnotationToolKind::None)
    {
        m_toolManager->setActiveTool(PDFToolManager::SelectTextTool);
    }

    delete m_webLinkDialog.data();
    delete m_textBoxPopup.data();
    delete m_toolBar.data();
    delete m_markupBar.data();
    restoreStatusMessage();
}

PDFAppDocumentView* PDFAppAnnotationCreator::getView() const
{
    return m_view.data();
}

PDFAppAnnotationController* PDFAppAnnotationCreator::getController() const
{
    return m_controller.data();
}

PDFAppContext* PDFAppAnnotationCreator::getContext() const
{
    return m_view ? m_view->getContext() : nullptr;
}

QWidget* PDFAppAnnotationCreator::getCanvasWidget() const
{
    PDFWidget* widget = m_view ? m_view->getPdfWidget() : nullptr;
    return widget && widget->getDrawWidget() ? widget->getDrawWidget()->getWidget() : nullptr;
}

PDFSelectTextTool* PDFAppAnnotationCreator::getSelectTextTool() const
{
    return m_toolManager ? m_toolManager->getSelectTextTool() : nullptr;
}

void PDFAppAnnotationCreator::createTools()
{
    PDFDrawWidgetProxy* proxy = m_view ? m_view->getProxy() : nullptr;
    PDFToolManager* toolManager = m_toolManager.data();
    if (!proxy || !toolManager)
    {
        return;
    }

    // The engine tools without actions (E2); the application activates them and
    // asks for the content of the sticky notes and web links (E3)
    m_stickyNoteTool = new PDFCreateStickyNoteTool(proxy, toolManager, nullptr, this);
    m_lineTool = new PDFCreateLineTypeTool(proxy, toolManager, PDFCreateLineTypeTool::Type::Line, nullptr, this);
    m_polylineTool = new PDFCreateLineTypeTool(proxy, toolManager, PDFCreateLineTypeTool::Type::PolyLine, nullptr, this);
    m_rectangleTool = new PDFCreateLineTypeTool(proxy, toolManager, PDFCreateLineTypeTool::Type::Rectangle, nullptr, this);
    m_polygonTool = new PDFCreateLineTypeTool(proxy, toolManager, PDFCreateLineTypeTool::Type::Polygon, nullptr, this);
    m_ellipseTool = new PDFCreateEllipseTool(proxy, toolManager, nullptr, this);
    m_freehandTool = new PDFCreateFreehandCurveTool(proxy, toolManager, nullptr, this);
    m_webLinkTool = new PDFCreateHyperlinkTool(proxy, toolManager, nullptr, this);
    m_stampTool = new PDFCreateStampTool(proxy, toolManager, nullptr, this);
    m_deleteTool = new PDFDeleteAnnotationTool(proxy, toolManager, nullptr, this);
    m_textBoxTool = new PDFAppTextBoxTool(proxy, this);
    m_pageLinkTool = new PDFAppPageLinkTool(proxy, this);

    for (PDFWidgetTool* tool : std::initializer_list<PDFWidgetTool*>{ m_stickyNoteTool.data(), m_lineTool.data(), m_polylineTool.data(), m_rectangleTool.data(),
                                                                       m_polygonTool.data(), m_ellipseTool.data(), m_freehandTool.data(), m_webLinkTool.data(),
                                                                       m_stampTool.data(), m_deleteTool.data(), m_textBoxTool.data(), m_pageLinkTool.data() })
    {
        addTool(tool);
    }

    connect(m_textBoxTool.data(), &PDFAppTextBoxTool::areaPicked, this, &PDFAppAnnotationCreator::onTextBoxAreaPicked);
    connect(m_pageLinkTool.data(), &PDFAppPageLinkTool::linkRequested, this, &PDFAppAnnotationCreator::onPageLinkRequested);
    connect(m_pageLinkTool.data(), &PDFAppPageLinkTool::stepChanged, this, &PDFAppAnnotationCreator::onPageLinkStepChanged);
    setToolDocuments();
}

void PDFAppAnnotationCreator::addTool(PDFWidgetTool* tool)
{
    if (!tool)
    {
        return;
    }

    if (m_controller)
    {
        m_controller->addTool(tool);
    }
    else if (m_toolManager)
    {
        m_toolManager->addTool(tool);
    }
}

void PDFAppAnnotationCreator::setToolDocuments()
{
    // The tools are added after the view has given the tool manager its document:
    // they get the shown document now, the later versions from the tool manager
    PDFAppDocument* document = m_view ? m_view->getDocument() : nullptr;
    if (!document || !document->isReady() || document->getDocument().isNull())
    {
        return;
    }

    const PDFModifiedDocument modified(document->getDocument(), nullptr, PDFModifiedDocument::Annotation);
    for (PDFAppAnnotationToolKind kind : PDFAppAnnotationTools::getKinds())
    {
        if (PDFWidgetTool* tool = getTool(kind))
        {
            tool->setDocument(modified);
        }
    }
}

PDFWidgetTool* PDFAppAnnotationCreator::getTool(PDFAppAnnotationToolKind kind) const
{
    using Kind = PDFAppAnnotationToolKind;
    switch (kind)
    {
        case Kind::TextBox:
            return m_textBoxTool.data();
        case Kind::StickyNote:
            return m_stickyNoteTool.data();
        case Kind::Line:
            return m_lineTool.data();
        case Kind::Polyline:
            return m_polylineTool.data();
        case Kind::Rectangle:
            return m_rectangleTool.data();
        case Kind::Polygon:
            return m_polygonTool.data();
        case Kind::Ellipse:
            return m_ellipseTool.data();
        case Kind::Freehand:
            return m_freehandTool.data();
        case Kind::WebLink:
            return m_webLinkTool.data();
        case Kind::PageLink:
            return m_pageLinkTool.data();
        case Kind::Stamp:
            return m_stampTool.data();
        case Kind::DeleteTool:
            return m_deleteTool.data();
        default:
            break;
    }
    return nullptr;
}

PDFAppTextBoxTool* PDFAppAnnotationCreator::getTextBoxTool() const
{
    return m_textBoxTool.data();
}

PDFAppPageLinkTool* PDFAppAnnotationCreator::getPageLinkTool() const
{
    return m_pageLinkTool.data();
}

PDFAppAnnotationToolKind PDFAppAnnotationCreator::getKindOfTool(const PDFWidgetTool* tool) const
{
    if (!tool)
    {
        return PDFAppAnnotationToolKind::None;
    }

    for (PDFAppAnnotationToolKind kind : PDFAppAnnotationTools::getKinds())
    {
        if (getTool(kind) == tool)
        {
            return kind;
        }
    }
    return PDFAppAnnotationToolKind::None;
}

bool PDFAppAnnotationCreator::isCreationAllowed() const
{
    return m_controller && m_controller->isAnnotationEditingAllowed();
}

// ---------------------------------------------------------------------- tools

bool PDFAppAnnotationCreator::startTool(PDFAppAnnotationToolKind kind)
{
    if (kind == PDFAppAnnotationToolKind::None)
    {
        endTool();
        return true;
    }

    if (!isCreationAllowed())
    {
        showStatusHint(m_controller ? m_controller->getEditingRefusalReason() : QString());
        return false;
    }

    if (kind == m_activeKind)
    {
        // A variant of the active tool (another icon, another stamp)
        applyToolStyle(kind);
        showToolBar(kind);
        return true;
    }

    QPointer<PDFAppAnnotationCreator> guard(this);
    endTool();
    if (!guard)
    {
        return false;
    }
    cancelCompose();
    cancelTextBox();
    if (!guard)
    {
        return false;
    }

    PDFSelectTextTool* selectTextTool = getSelectTextTool();
    bool isStarted = false;
    m_isStarting = true;
    if (PDFAppAnnotationTools::isMarkup(kind))
    {
        // The markups use the text selection; annotations are not selectable meanwhile
        // (a drag over a marked text marks it again instead of moving the markup)
        if (m_controller)
        {
            m_controller->endActiveTool();
        }
        if (guard && selectTextTool && m_toolManager && m_toolManager->getActiveTool() == selectTextTool)
        {
            selectTextTool->setAnnotationsSelectable(false);
            isStarted = true;
        }
    }
    else if (PDFWidgetTool* tool = getTool(kind))
    {
        applyToolStyle(kind);
        isStarted = m_controller && m_controller->activateTool(tool);
        if (guard && isStarted && m_controller)
        {
            m_controller->setPendingDescription(PDFAppAnnotationTools::getUndoDescription(kind));
        }
    }

    if (!guard)
    {
        return false;
    }
    m_isStarting = false;

    if (!isStarted)
    {
        if (selectTextTool)
        {
            selectTextTool->setAnnotationsSelectable(true);
        }
        return false;
    }

    // The last used markup is remembered only for a markup tool, which has started
    if (PDFAppAnnotationTools::isMarkup(kind) && kind != PDFAppAnnotationToolKind::HighlightComment)
    {
        PDFAppAnnotationToolSettings::setLastMarkupType(getContext(), PDFAppAnnotationTools::getMarkupType(kind));
    }

    setActiveKind(kind);
    return true;
}

void PDFAppAnnotationCreator::requestTool(PDFAppAnnotationToolKind kind)
{
    const bool isPending = m_requestedKind != PDFAppAnnotationToolKind::None;
    m_requestedKind = kind;
    if (!isPending && kind != PDFAppAnnotationToolKind::None)
    {
        QMetaObject::invokeMethod(this, &PDFAppAnnotationCreator::onRequestedToolQueued, Qt::QueuedConnection);
    }
}

void PDFAppAnnotationCreator::onRequestedToolQueued()
{
    const PDFAppAnnotationToolKind kind = m_requestedKind;
    m_requestedKind = PDFAppAnnotationToolKind::None;
    if (kind != PDFAppAnnotationToolKind::None)
    {
        startTool(kind);
    }
}

void PDFAppAnnotationCreator::setActiveKind(PDFAppAnnotationToolKind kind)
{
    m_activeKind = kind;
    if (kind != PDFAppAnnotationToolKind::None)
    {
        m_lastKind = kind;
        showToolBar(kind);
        showStatusHint(PDFAppAnnotationTools::getStatusHint(kind));
        if (m_markupBar)
        {
            m_markupBar->hide();
        }
    }

    QPointer<PDFAppAnnotationCreator> guard(this);
    Q_EMIT activeKindChanged(kind);
    if (guard)
    {
        Q_EMIT toolStateChanged();
    }
}

void PDFAppAnnotationCreator::endTool()
{
    if (m_activeKind == PDFAppAnnotationToolKind::None)
    {
        return;
    }

    QPointer<PDFAppAnnotationCreator> guard(this);
    cancelTextBox();
    if (!guard)
    {
        return;
    }
    cancelCompose();
    if (!guard)
    {
        return;
    }

    if (PDFAppAnnotationTools::isEngineTool(m_activeKind) && m_controller)
    {
        // The text selection becomes active: onActiveToolChanged finishes the tool
        m_controller->endActiveTool();
        if (!guard)
        {
            return;
        }
    }

    if (m_activeKind != PDFAppAnnotationToolKind::None)
    {
        finishTool();
    }
}

void PDFAppAnnotationCreator::finishTool()
{
    if (PDFSelectTextTool* selectTextTool = getSelectTextTool())
    {
        selectTextTool->setAnnotationsSelectable(true);
    }

    // Link to page returns to its step 1 first: its step change shows the hint of
    // the step, which the status bar must not keep after the end of the tool
    if (m_pageLinkTool)
    {
        m_pageLinkTool->goBack();
    }
    hideToolBar();
    restoreStatusMessage();
    setActiveKind(PDFAppAnnotationToolKind::None);
}

void PDFAppAnnotationCreator::onActiveToolChanged(PDFWidgetTool* tool)
{
    if (m_isStarting)
    {
        return;
    }

    if (m_activeKind == PDFAppAnnotationToolKind::None)
    {
        updateMarkupBar();
        return;
    }

    QPointer<PDFAppAnnotationCreator> guard(this);
    if (PDFAppAnnotationTools::isMarkup(m_activeKind))
    {
        // Another tool of the canvas (Select menu, Esc) ends the markup
        if (tool != getSelectTextTool())
        {
            finishTool();
        }
    }
    else if (tool != getTool(m_activeKind))
    {
        cancelTextBox();
        if (!guard)
        {
            return;
        }
        cancelCompose();
        if (!guard)
        {
            return;
        }
        finishTool();
    }
    else
    {
        return;
    }

    // Esc of the engine turns the tool off without another one: the text
    // selection of the view becomes active again (from the event loop - the
    // engine is in its key handler)
    if (guard && !tool)
    {
        QMetaObject::invokeMethod(this, &PDFAppAnnotationCreator::onEndToolQueued, Qt::QueuedConnection);
    }
}

void PDFAppAnnotationCreator::onEndToolQueued()
{
    if (m_controller && m_toolManager && !m_toolManager->getActiveTool())
    {
        m_controller->endActiveTool();
    }
}

void PDFAppAnnotationCreator::applyToolStyle(PDFAppAnnotationToolKind kind)
{
    using Kind = PDFAppAnnotationToolKind;
    switch (kind)
    {
        case Kind::StickyNote:
            if (m_stickyNoteTool)
            {
                m_stickyNoteTool->setIcon(getStickyNoteIcon());
            }
            break;

        case Kind::Stamp:
            if (m_stampTool)
            {
                m_stampTool->setStamp(getStamp());
            }
            break;

        case Kind::PageLink:
            if (m_pageLinkTool)
            {
                m_pageLinkTool->setDestinationType(getPageLinkTarget());
                m_pageLinkTool->setKeepZoom(isPageLinkKeepZoom());
            }
            break;

        default:
            if (PDFWidgetTool* tool = getTool(kind))
            {
                if (PDFAnnotationStyleManager* styleManager = tool->getStyleManager())
                {
                    styleManager->reloadStyle();
                }
            }
            break;
    }
}

void PDFAppAnnotationCreator::showToolBar(PDFAppAnnotationToolKind kind)
{
    QWidget* canvasArea = m_view ? m_view->getCanvasArea() : nullptr;
    if (!canvasArea)
    {
        return;
    }

    if (!m_toolBar)
    {
        m_toolBar = new PDFAppAnnotationToolBar(this, canvasArea);
    }

    if (m_toolBar->getKind() != kind)
    {
        m_toolBar->configure(kind);
    }
    else
    {
        m_toolBar->updateState();
    }
    m_toolBar->placeAtTop();
    m_toolBar->show();
    m_toolBar->raise();
}

void PDFAppAnnotationCreator::hideToolBar()
{
    if (m_toolBar)
    {
        // The bar keeps its parts (a part may be the sender of the end of the tool)
        m_toolBar->hide();
    }
}

void PDFAppAnnotationCreator::showStatusHint(const QString& text)
{
    PDFAppDocumentStatusBar* statusBar = m_view ? m_view->getStatusBar() : nullptr;
    if (!statusBar || text.isEmpty())
    {
        return;
    }

    m_statusHint = text;
    statusBar->showMessage(text);
}

void PDFAppAnnotationCreator::restoreStatusMessage()
{
    PDFAppDocumentStatusBar* statusBar = m_view ? m_view->getStatusBar() : nullptr;
    if (statusBar && !m_statusHint.isEmpty() && statusBar->getMessage() == m_statusHint)
    {
        statusBar->showMessage(PDFAppDocumentView::tr("Ready"));
    }
    m_statusHint.clear();
}

// ---------------------------------------------------------------------- variants

TextAnnotationIcon PDFAppAnnotationCreator::getStickyNoteIcon() const
{
    return PDFAppAnnotationToolSettings::getStickyNoteIcon(getContext());
}

void PDFAppAnnotationCreator::setStickyNoteIcon(TextAnnotationIcon icon)
{
    PDFAppAnnotationToolSettings::setStickyNoteIcon(getContext(), icon);
    if (m_stickyNoteTool)
    {
        m_stickyNoteTool->setIcon(getStickyNoteIcon());
    }
    if (m_toolBar && m_toolBar->isVisible())
    {
        m_toolBar->updateState();
    }
    Q_EMIT toolStateChanged();
}

Stamp PDFAppAnnotationCreator::getStamp() const
{
    return PDFAppAnnotationToolSettings::getStamp(getContext());
}

void PDFAppAnnotationCreator::setStamp(Stamp stamp)
{
    PDFAppAnnotationToolSettings::setStamp(getContext(), stamp);
    if (m_stampTool)
    {
        m_stampTool->setStamp(getStamp());
    }
    if (m_toolBar && m_toolBar->isVisible())
    {
        m_toolBar->updateState();
    }
    Q_EMIT toolStateChanged();
}

DestinationType PDFAppAnnotationCreator::getPageLinkTarget() const
{
    return PDFAppAnnotationToolSettings::getPageLinkTarget(getContext());
}

bool PDFAppAnnotationCreator::isPageLinkKeepZoom() const
{
    return PDFAppAnnotationToolSettings::isPageLinkKeepZoom(getContext());
}

void PDFAppAnnotationCreator::setPageLinkTarget(DestinationType type, bool keepZoom)
{
    if (!PDFAppAnnotationTools::isPageLinkTarget(type))
    {
        return;
    }

    PDFAppAnnotationToolSettings::setPageLinkTarget(getContext(), type, keepZoom);
    if (m_pageLinkTool)
    {
        m_pageLinkTool->setDestinationType(type);
        m_pageLinkTool->setKeepZoom(keepZoom);
    }
    if (m_toolBar && m_toolBar->isVisible())
    {
        m_toolBar->updateState();
    }
    if (m_activeKind == PDFAppAnnotationToolKind::PageLink)
    {
        onPageLinkStepChanged();
    }
    Q_EMIT toolStateChanged();
}

AnnotationType PDFAppAnnotationCreator::getLastMarkupType() const
{
    return PDFAppAnnotationToolSettings::getLastMarkupType(getContext());
}

// ---------------------------------------------------------------------- style

void PDFAppAnnotationCreator::setColorTarget(ColorTarget target)
{
    if (m_colorTarget != target)
    {
        m_colorTarget = target;
        if (m_toolBar && m_toolBar->isVisible())
        {
            m_toolBar->updateState();
        }
    }
}

QColor PDFAppAnnotationCreator::getColor(PDFAppAnnotationToolKind kind, ColorTarget target) const
{
    if (PDFAppAnnotationTools::isMarkup(kind))
    {
        return PDFAppAnnotationToolSettings::getMarkupColor(PDFAppAnnotationTools::getMarkupType(kind));
    }
    if (kind == PDFAppAnnotationToolKind::StickyNote)
    {
        return PDFAppAnnotationToolSettings::getStickyNoteColor();
    }

    const PDFWidgetTool* tool = getTool(kind);
    const PDFAnnotationStyleManager* styleManager = tool ? tool->getStyleManager() : nullptr;
    if (!styleManager)
    {
        return QColor();
    }
    return target == ColorTarget::Fill ? styleManager->getStyle().fillColor : styleManager->getStyle().strokeColor;
}

void PDFAppAnnotationCreator::setColor(PDFAppAnnotationToolKind kind, ColorTarget target, const QColor& color)
{
    if (PDFAppAnnotationTools::isMarkup(kind))
    {
        PDFAppAnnotationToolSettings::setMarkupColor(PDFAppAnnotationTools::getMarkupType(kind), color);
    }
    else if (kind == PDFAppAnnotationToolKind::StickyNote)
    {
        PDFAppAnnotationToolSettings::setStickyNoteColor(color);
    }
    else
    {
        const PDFWidgetTool* tool = getTool(kind);
        PDFAnnotationStyleManager* styleManager = tool ? tool->getStyleManager() : nullptr;
        if (!styleManager)
        {
            return;
        }

        // Stroke has always a color; Fill may be "No color" (an invalid color)
        PDFAnnotationStyle style = styleManager->getStyle();
        if (target == ColorTarget::Fill)
        {
            style.fillColor = color;
        }
        else if (color.isValid())
        {
            style.strokeColor = color;
        }
        else
        {
            return;
        }
        styleManager->setStyle(style);
    }

    // The tools sharing the style (Shape) read it again (once, also for the
    // notification of the stored style)
    scheduleStyleReload();
}

PDFReal PDFAppAnnotationCreator::getPenWidth(PDFAppAnnotationToolKind kind) const
{
    const PDFWidgetTool* tool = getTool(kind);
    const PDFAnnotationStyleManager* styleManager = tool ? tool->getStyleManager() : nullptr;
    return styleManager ? styleManager->getStyle().penWidth : 1.0;
}

void PDFAppAnnotationCreator::setPenWidth(PDFAppAnnotationToolKind kind, PDFReal penWidth)
{
    const PDFWidgetTool* tool = getTool(kind);
    PDFAnnotationStyleManager* styleManager = tool ? tool->getStyleManager() : nullptr;
    if (!styleManager || !std::isfinite(penWidth))
    {
        return;
    }

    PDFAnnotationStyle style = styleManager->getStyle();
    style.penWidth = qBound(0.0, penWidth, PDFAnnotationStyleSettings::MAXIMUM_PEN_WIDTH);
    styleManager->setStyle(style);
    scheduleStyleReload();
}

void PDFAppAnnotationCreator::scheduleStyleReload()
{
    if (!m_isStyleReloadPending)
    {
        m_isStyleReloadPending = true;
        QMetaObject::invokeMethod(this, &PDFAppAnnotationCreator::onStyleReloadQueued, Qt::QueuedConnection);
    }
}

void PDFAppAnnotationCreator::onStyleReloadQueued()
{
    if (m_isStyleReloadPending)
    {
        reloadStyles();
    }
}

void PDFAppAnnotationCreator::reloadStyles()
{
    m_isStyleReloadPending = false;
    for (PDFAppAnnotationToolKind kind : PDFAppAnnotationTools::getKinds())
    {
        if (PDFWidgetTool* tool = getTool(kind))
        {
            if (PDFAnnotationStyleManager* styleManager = tool->getStyleManager())
            {
                styleManager->reloadStyle();
            }
        }
    }

    if (m_stickyNoteTool)
    {
        m_stickyNoteTool->setIcon(getStickyNoteIcon());
    }
    if (m_stampTool)
    {
        m_stampTool->setStamp(getStamp());
    }
    if (m_pageLinkTool)
    {
        m_pageLinkTool->setDestinationType(getPageLinkTarget());
        m_pageLinkTool->setKeepZoom(isPageLinkKeepZoom());
    }
    if (m_toolBar && m_toolBar->isVisible())
    {
        m_toolBar->updateState();
    }
    if (m_markupBar && m_markupBar->isVisible())
    {
        m_markupBar->updateState();
    }
    Q_EMIT toolStateChanged();
}

// ---------------------------------------------------------------------- text selection

bool PDFAppAnnotationCreator::hasTextSelection() const
{
    const PDFSelectTextTool* selectTextTool = getSelectTextTool();
    return selectTextTool && selectTextTool->hasSelection();
}

std::vector<PDFAppTextMarkupPart> PDFAppAnnotationCreator::getTextSelectionParts() const
{
    std::vector<PDFAppTextMarkupPart> parts;
    const PDFSelectTextTool* selectTextTool = getSelectTextTool();
    PDFDrawWidgetProxy* proxy = m_view ? m_view->getProxy() : nullptr;
    PDFAsynchronousTextLayoutCompiler* compiler = proxy ? proxy->getTextLayoutCompiler() : nullptr;
    if (!selectTextTool || !compiler || !selectTextTool->hasSelection())
    {
        return parts;
    }

    // The quadrilaterals of the selected characters of each page (the text
    // layouts exist - the selection was made from them)
    const PDFTextSelection& selection = selectTextTool->getTextSelection();
    for (auto it = selection.begin(); it != selection.end(); it = selection.nextPageRange(it))
    {
        const PDFInteger pageIndex = it->start.pageIndex;
        PDFTextLayoutGetter layoutGetter = compiler->getTextLayoutLazy(pageIndex);
        PDFTextSelectionPainter painter(&selection);
        PDFAppTextMarkupPart part;
        part.pageIndex = pageIndex;
        painter.prepareGeometry(pageIndex, layoutGetter, QTransform(), &part.quadrilaterals);
        if (!part.quadrilaterals.isEmpty())
        {
            parts.push_back(std::move(part));
        }
    }
    return parts;
}

QRect PDFAppAnnotationCreator::pageRectToCanvas(PDFInteger pageIndex, const QRectF& pageRectangle) const
{
    PDFDrawWidgetProxy* proxy = m_view ? m_view->getProxy() : nullptr;
    QWidget* canvasArea = m_view ? m_view->getCanvasArea() : nullptr;
    QWidget* canvasWidget = getCanvasWidget();
    if (!proxy || !canvasArea || !canvasWidget)
    {
        return QRect();
    }

    const PDFWidgetSnapshot snapshot = proxy->getSnapshot();
    const PDFWidgetSnapshot::SnapshotItem* item = snapshot.getPageSnapshot(pageIndex);
    if (!item)
    {
        return QRect();
    }

    const QRect deviceRectangle = item->pageToDeviceMatrix.mapRect(pageRectangle).toAlignedRect();
    return QRect(canvasWidget->mapTo(canvasArea, deviceRectangle.topLeft()), deviceRectangle.size());
}

QRect PDFAppAnnotationCreator::getTextSelectionRect() const
{
    QWidget* canvasArea = m_view ? m_view->getCanvasArea() : nullptr;
    QWidget* canvasWidget = getCanvasWidget();
    if (!canvasArea || !canvasWidget)
    {
        return QRect();
    }

    QRect result;
    for (const PDFAppTextMarkupPart& part : getTextSelectionParts())
    {
        result = result.united(pageRectToCanvas(part.pageIndex, part.quadrilaterals.boundingRect()));
    }

    // Only the visible part of the selection
    const QRect visible(canvasWidget->mapTo(canvasArea, QPoint(0, 0)), canvasWidget->size());
    return result.intersected(visible);
}

PDFAppNewAnnotation PDFAppAnnotationCreator::createAnnotationData() const
{
    PDFAppNewAnnotation data;
    data.author = PDFAuthorSettings::getAuthorName();
    return data;
}

bool PDFAppAnnotationCreator::commitResult(const PDFAppAnnotationResult& result, const QString& description, const std::vector<PDFObjectReference>& created)
{
    if (!result || !m_controller)
    {
        return false;
    }

    QPointer<PDFAppAnnotationCreator> guard(this);
    const bool isCommitted = m_controller->commit(*result, PDFAppAnnotationChangeKind::Tool, description);
    if (!guard || !isCommitted)
    {
        return isCommitted;
    }

    m_lastCreated = created;
    Q_EMIT annotationsCreated();
    return true;
}

bool PDFAppAnnotationCreator::markTextSelection(AnnotationType type)
{
    PDFAppDocument* document = m_view ? m_view->getDocument() : nullptr;
    if (!PDFAppAnnotationOperations::isTextMarkupType(type) || !document || !isCreationAllowed())
    {
        return false;
    }

    const std::vector<PDFAppTextMarkupPart> parts = getTextSelectionParts();
    if (parts.empty())
    {
        return false;
    }

    PDFAppNewAnnotation data = createAnnotationData();
    data.color = PDFAppAnnotationToolSettings::getMarkupColor(type);
    if (type == AnnotationType::Highlight)
    {
        data.opacity = PDFAppAnnotationOperations::HIGHLIGHT_OPACITY;
    }

    std::vector<PDFObjectReference> created;
    const PDFAppAnnotationResult result = PDFAppAnnotationOperations::createTextMarkups(document->getDocument(), type, parts, data, &created);
    PDFAppAnnotationToolSettings::setLastMarkupType(getContext(), type);

    QPointer<PDFAppAnnotationCreator> guard(this);
    if (!commitResult(result, PDFAppAnnotationTools::getUndoDescription(PDFAppAnnotationTools::getMarkupKind(type)), created) || !guard)
    {
        return false;
    }

    // The marked text is no longer selected (the markup bar hides)
    if (PDFSelectTextTool* selectTextTool = getSelectTextTool())
    {
        selectTextTool->deselect();
    }
    if (guard)
    {
        updateMarkupBar();
    }
    return true;
}

bool PDFAppAnnotationCreator::commentTextSelection()
{
    PDFAppCommentPopup* popup = m_view ? m_view->getCommentPopup() : nullptr;
    if (!popup || !isCreationAllowed())
    {
        return false;
    }

    std::vector<PDFAppTextMarkupPart> parts = getTextSelectionParts();
    const QRect anchor = getTextSelectionRect();
    if (parts.empty())
    {
        return false;
    }

    // A popup of another mode closes first (its cancellation must not cancel the new comment)
    QPointer<PDFAppAnnotationCreator> guard(this);
    QPointer<PDFAppCommentPopup> popupGuard(popup);
    if (popup->getMode() != PDFAppCommentPopup::Mode::None)
    {
        popup->cancel();
        if (!guard || !popupGuard)
        {
            return false;
        }
    }

    m_composeKind = ComposeKind::HighlightComment;
    m_composeParts = std::move(parts);
    connect(popup, &PDFAppCommentPopup::composeAccepted, this, &PDFAppAnnotationCreator::onComposeAccepted, Qt::UniqueConnection);
    connect(popup, &PDFAppCommentPopup::composeCanceled, this, &PDFAppAnnotationCreator::onComposeCanceled, Qt::UniqueConnection);
    if (m_markupBar)
    {
        m_markupBar->hide();
    }
    popup->showCompose(anchor, PDFAppAnnotationTools::getName(PDFAppAnnotationToolKind::HighlightComment), PDFAppAnnotationTools::getIconName(PDFAppAnnotationToolKind::HighlightComment));
    return guard && popupGuard && popup->getMode() == PDFAppCommentPopup::Mode::Compose;
}

void PDFAppAnnotationCreator::copyTextSelection()
{
    if (PDFSelectTextTool* selectTextTool = getSelectTextTool())
    {
        selectTextTool->copyText();
    }
}

void PDFAppAnnotationCreator::cancelCompose()
{
    if (m_composeKind == ComposeKind::None)
    {
        return;
    }

    m_composeKind = ComposeKind::None;
    m_composeParts.clear();
    PDFAppCommentPopup* popup = m_view ? m_view->getCommentPopup() : nullptr;
    if (popup && popup->getMode() == PDFAppCommentPopup::Mode::Compose)
    {
        popup->cancel();
    }
}

void PDFAppAnnotationCreator::cancelTextBox()
{
    if (m_textBoxPopup && !m_textBoxPopup->isHidden())
    {
        // The cancellation clears the area of the tool (onTextBoxCanceled); the
        // popup of a hidden tab is open too (isHidden, not isVisible)
        m_textBoxPopup->cancel();
    }
    else if (m_textBoxTool && m_textBoxTool->hasArea())
    {
        m_textBoxTool->clearArea();
    }
}

void PDFAppAnnotationCreator::onComposeAccepted(const QString& text)
{
    const ComposeKind kind = m_composeKind;
    m_composeKind = ComposeKind::None;
    PDFAppDocument* document = m_view ? m_view->getDocument() : nullptr;
    if (kind == ComposeKind::None || !document || !isCreationAllowed())
    {
        m_composeParts.clear();
        return;
    }

    PDFAppNewAnnotation data = createAnnotationData();
    data.contents = text.left(PDFAppAnnotationOperations::MAXIMUM_TEXT_LENGTH);

    if (kind == ComposeKind::StickyNote)
    {
        data.color = PDFAppAnnotationToolSettings::getStickyNoteColor();
        PDFObjectReference created;
        const PDFAppAnnotationResult result = PDFAppAnnotationOperations::createStickyNote(document->getDocument(), m_composeRequest.pageIndex, m_composeRequest.point,
                                                                                           m_composeRequest.icon, data, &created);
        commitResult(result, PDFAppAnnotationTools::getUndoDescription(PDFAppAnnotationToolKind::StickyNote), { created });
        return;
    }

    // Highlight and comment: one highlight with the text (tools.md 9.8)
    const std::vector<PDFAppTextMarkupPart> parts = std::move(m_composeParts);
    m_composeParts.clear();
    data.color = PDFAppAnnotationToolSettings::getMarkupColor(AnnotationType::Highlight);
    data.opacity = PDFAppAnnotationOperations::HIGHLIGHT_OPACITY;
    std::vector<PDFObjectReference> created;
    const PDFAppAnnotationResult result = PDFAppAnnotationOperations::createTextMarkups(document->getDocument(), AnnotationType::Highlight, parts, data, &created);

    QPointer<PDFAppAnnotationCreator> guard(this);
    if (commitResult(result, PDFAppAnnotationTools::getUndoDescription(PDFAppAnnotationToolKind::HighlightComment), created) && guard)
    {
        if (PDFSelectTextTool* selectTextTool = getSelectTextTool())
        {
            selectTextTool->deselect();
        }
    }
}

void PDFAppAnnotationCreator::onComposeCanceled()
{
    // Esc / close of the compose popup: nothing is created (no undo step); a
    // selection, which is still there, gets its markup bar again
    m_composeKind = ComposeKind::None;
    m_composeParts.clear();
    updateMarkupBar();
}

void PDFAppAnnotationCreator::onCreationRequested(const PDFAnnotationCreationRequest& request)
{
    using Kind = PDFAnnotationCreationRequest::Kind;

    // From the event loop (the controller queues the requests of the engine)
    if (!m_view || !isCreationAllowed())
    {
        return;
    }

    switch (request.kind)
    {
        case Kind::StickyNote:
        {
            PDFAppCommentPopup* popup = m_view->getCommentPopup();
            if (!popup)
            {
                return;
            }

            QPointer<PDFAppAnnotationCreator> guard(this);
            QPointer<PDFAppCommentPopup> popupGuard(popup);
            if (popup->getMode() != PDFAppCommentPopup::Mode::None)
            {
                popup->cancel();
                if (!guard || !popupGuard)
                {
                    return;
                }
            }

            // The compose popup opens at the icon of the new note
            using H = PDFAppAnnotationCreatorHelper;
            const QRectF iconArea(request.point.x(), request.point.y() - H::STICKY_NOTE_ANCHOR, H::STICKY_NOTE_ANCHOR, H::STICKY_NOTE_ANCHOR);
            m_composeKind = ComposeKind::StickyNote;
            m_composeRequest = request;
            connect(popup, &PDFAppCommentPopup::composeAccepted, this, &PDFAppAnnotationCreator::onComposeAccepted, Qt::UniqueConnection);
            connect(popup, &PDFAppCommentPopup::composeCanceled, this, &PDFAppAnnotationCreator::onComposeCanceled, Qt::UniqueConnection);
            popup->showCompose(pageRectToCanvas(request.pageIndex, iconArea), PDFAppAnnotationTools::getStickyNoteName(request.icon),
                               PDFAppAnnotationTools::getStickyNoteIconName(request.icon));
            break;
        }

        case Kind::Hyperlink:
        {
            if (m_webLinkDialog)
            {
                return;
            }

            m_webLinkRequest = request;
            PDFAppWebLinkDialog* dialog = new PDFAppWebLinkDialog(m_view->window());
            PDFAppMainWindow::prepareDialog(dialog, m_view);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            connect(dialog, &QDialog::finished, this, &PDFAppAnnotationCreator::onWebLinkFinished);
            m_webLinkDialog = dialog;
            dialog->open();
            if (PDFLineEdit* field = dialog->getField())
            {
                field->setFocus(Qt::OtherFocusReason);
            }
            break;
        }

        case Kind::FreeText:
            // The text box of the new application has its own tool and popup
            break;
    }
}

void PDFAppAnnotationCreator::onWebLinkFinished(int result)
{
    if (sender() != m_webLinkDialog.data() || !m_webLinkDialog)
    {
        return;
    }

    const QString url = m_webLinkDialog->getUrl();
    m_webLinkDialog = nullptr;
    PDFAppDocument* document = m_view ? m_view->getDocument() : nullptr;
    if (result != QDialog::Accepted || !document || !isCreationAllowed())
    {
        return;
    }

    PDFObjectReference created;
    const PDFAppAnnotationResult change = PDFAppAnnotationOperations::createWebLink(document->getDocument(), m_webLinkRequest.pageIndex, m_webLinkRequest.rectangle, url, &created);
    commitResult(change, PDFAppAnnotationTools::getUndoDescription(PDFAppAnnotationToolKind::WebLink), { created });
}

void PDFAppAnnotationCreator::onTextBoxAreaPicked(PDFInteger pageIndex, QRectF rectangle)
{
    QWidget* canvasArea = m_view ? m_view->getCanvasArea() : nullptr;
    if (!canvasArea || m_activeKind != PDFAppAnnotationToolKind::TextBox)
    {
        if (m_textBoxTool)
        {
            m_textBoxTool->clearArea();
        }
        return;
    }

    if (!m_textBoxPopup)
    {
        m_textBoxPopup = new PDFAppTextBoxPopup(canvasArea);
        connect(m_textBoxPopup.data(), &PDFAppTextBoxPopup::accepted, this, &PDFAppAnnotationCreator::onTextBoxAccepted);
        connect(m_textBoxPopup.data(), &PDFAppTextBoxPopup::canceled, this, &PDFAppAnnotationCreator::onTextBoxCanceled);
        connect(m_textBoxPopup.data(), &PDFAppTextBoxPopup::previewChanged, this, &PDFAppAnnotationCreator::onTextBoxPreviewChanged);
    }

    // "Page 308 · Jakub Melka" (the page label, the author of the new annotation)
    const QString author = PDFAuthorSettings::getAuthorName();
    const QString page = PDFAppAnnotationTools::tr("Page %1").arg(m_view->getPageLabel(pageIndex));
    const QString subtitle = author.isEmpty() ? page : PDFAppAnnotationTools::tr("%1 · %2").arg(page, author);

    QColor borderColor;
    PDFReal borderWidth = 1.0;
    PDFAppAnnotationToolSettings::getFreeTextBorder(getContext(), &borderColor, &borderWidth);
    m_textBoxPopup->open(pageRectToCanvas(pageIndex, rectangle), subtitle, PDFAppAnnotationToolSettings::getFreeTextStyle(getContext()), borderColor, borderWidth);
}

void PDFAppAnnotationCreator::onTextBoxAccepted()
{
    PDFAppTextBoxPopup* popup = m_textBoxPopup.data();
    PDFAppTextBoxTool* tool = m_textBoxTool.data();
    PDFAppDocument* document = m_view ? m_view->getDocument() : nullptr;
    if (!popup || !tool || !tool->hasArea() || !document)
    {
        return;
    }

    // The values are remembered for the next text box
    const PDFFreeTextStyle style = popup->getStyle();
    const QColor borderColor = popup->getBorderColor();
    const PDFReal borderWidth = popup->getBorderWidth();
    PDFAppAnnotationToolSettings::setFreeTextStyle(getContext(), style);
    PDFAppAnnotationToolSettings::setFreeTextBorder(getContext(), borderColor, borderWidth);

    PDFAppNewAnnotation data = createAnnotationData();
    data.color = borderColor;
    data.penWidth = borderWidth;
    PDFObjectReference created;
    const PDFAppAnnotationResult result = PDFAppAnnotationOperations::createFreeText(document->getDocument(), tool->getPageIndex(), tool->getRectangle(), popup->getText(), style, data, &created);
    tool->clearArea();

    QPointer<PDFAppAnnotationCreator> guard(this);
    commitResult(result, PDFAppAnnotationTools::getUndoDescription(PDFAppAnnotationToolKind::TextBox), { created });

    // Add ends the tool (note 1172:28734)
    if (guard)
    {
        endTool();
    }
}

void PDFAppAnnotationCreator::onTextBoxCanceled()
{
    // Nothing is created; the tool stays active for another rectangle
    if (m_textBoxTool)
    {
        m_textBoxTool->clearArea();
    }
}

void PDFAppAnnotationCreator::onPageLinkRequested(PDFInteger pageIndex, QRectF rectangle, PDFDestination destination)
{
    PDFAppDocument* document = m_view ? m_view->getDocument() : nullptr;
    if (!document || !isCreationAllowed())
    {
        return;
    }

    PDFObjectReference created;
    const PDFAppAnnotationResult result = PDFAppAnnotationOperations::createPageLink(document->getDocument(), pageIndex, rectangle, destination, &created);
    QPointer<PDFAppAnnotationCreator> guard(this);
    if (!commitResult(result, PDFAppAnnotationTools::getUndoDescription(PDFAppAnnotationToolKind::PageLink), { created }) || !guard)
    {
        return;
    }

    // The view returns to the source page with the new link selected (note 1172:28734)
    QPointer<PDFAppDocumentView> view = m_view;
    if (view)
    {
        view->goToPage(pageIndex);
    }
    if (guard && m_controller)
    {
        m_controller->selectAnnotations({ created });
    }
}

void PDFAppAnnotationCreator::onPageLinkStepChanged()
{
    if (m_activeKind != PDFAppAnnotationToolKind::PageLink)
    {
        return;
    }

    if (m_toolBar && m_toolBar->isVisible())
    {
        m_toolBar->updateState();
    }

    const bool isTarget = m_pageLinkTool && m_pageLinkTool->getStep() == PDFAppPageLinkTool::Step::Target;
    showStatusHint(isTarget ? PDFAppAnnotationTools::getPageLinkInstruction(getPageLinkTarget()) : PDFAppAnnotationTools::getStatusHint(PDFAppAnnotationToolKind::PageLink));
}

void PDFAppAnnotationCreator::onEditingAllowedChanged()
{
    // A locked document ends the tool and closes the forms of a new annotation (O20)
    QPointer<PDFAppAnnotationCreator> guard(this);
    if (!isCreationAllowed())
    {
        endTool();
        if (guard)
        {
            cancelCompose();
        }
        if (guard)
        {
            cancelTextBox();
        }
        if (guard && m_webLinkDialog)
        {
            m_webLinkDialog->reject();
        }
    }

    if (guard)
    {
        updateMarkupBar();
    }
}

void PDFAppAnnotationCreator::onChangeCommitted()
{
    // The next annotation of the active engine tool gets the description of the tool
    if (m_controller && PDFAppAnnotationTools::isEngineTool(m_activeKind))
    {
        m_controller->setPendingDescription(PDFAppAnnotationTools::getUndoDescription(m_activeKind));
    }
}

void PDFAppAnnotationCreator::onTextSelectionChanged()
{
    updateMarkupBar();
}

void PDFAppAnnotationCreator::onViewChanged()
{
    // A scroll or a zoom hides the markup bar (tools.md 9.8); the text box popup
    // follows its rectangle
    if (m_markupBar && m_markupBar->isVisible() && getTextSelectionRect() != m_markupBarSelection)
    {
        m_markupBar->hide();
    }
    if (m_textBoxPopup && m_textBoxPopup->isVisible() && m_textBoxTool && m_textBoxTool->hasArea())
    {
        m_textBoxPopup->placeAt(pageRectToCanvas(m_textBoxTool->getPageIndex(), m_textBoxTool->getRectangle()));
    }

    // The Target select of Link to page names the zoom, which a Position link stores
    if (m_activeKind == PDFAppAnnotationToolKind::PageLink && m_toolBar)
    {
        m_toolBar->updateZoom();
    }
}

void PDFAppAnnotationCreator::onDatabaseChanged(const QString& topic, const QString& key)
{
    if (topic == PDFAnnotationStyleRepository::getTopic())
    {
        // The last markup is not a style of a tool (the panel and the markup bar
        // read it at their update)
        if (key == PDFAnnotationStyleRepository::toolLastMarkup())
        {
            if (m_markupBar && m_markupBar->isVisible())
            {
                m_markupBar->updateState();
            }
            Q_EMIT toolStateChanged();
            return;
        }
        scheduleStyleReload();
    }
    else if (topic == PDFSettingsRepository::getTopic() && key == PDFSettingKeys::annotationsShowMarkupBar())
    {
        updateMarkupBar();
    }
}

void PDFAppAnnotationCreator::onSettingsChanged()
{
    // Everything has changed (another instance, Restore defaults)
    scheduleStyleReload();
    updateMarkupBar();
}

void PDFAppAnnotationCreator::onTextBoxPreviewChanged()
{
    // The live preview of the text in the drawn rectangle
    if (m_textBoxPopup && m_textBoxTool)
    {
        m_textBoxTool->setPreview(m_textBoxPopup->getText(), m_textBoxPopup->getStyle(), m_textBoxPopup->getBorderColor(), m_textBoxPopup->getBorderWidth());
    }
}

void PDFAppAnnotationCreator::updateMarkupBar()
{
    QWidget* canvasArea = m_view ? m_view->getCanvasArea() : nullptr;
    const PDFAppContext* context = getContext();
    const bool isEnabled = !context || context->getSettings().getBool(PDFSettingKeys::annotationsShowMarkupBar());
    const bool isShown = canvasArea && isEnabled && m_activeKind == PDFAppAnnotationToolKind::None && m_composeKind == ComposeKind::None &&
                         !m_isReleasePending && hasTextSelection();
    const QRect selection = isShown ? getTextSelectionRect() : QRect();
    if (selection.isEmpty())
    {
        if (m_markupBar)
        {
            m_markupBar->hide();
        }
        return;
    }

    if (!m_markupBar)
    {
        m_markupBar = new PDFAppMarkupBar(this, canvasArea);
    }
    m_markupBarSelection = selection;
    m_markupBar->showAt(selection, isCreationAllowed());
}

void PDFAppAnnotationCreator::onCanvasReleasedQueued()
{
    m_isReleasePending = false;

    // Markup tool: the dragged text is marked at the release
    if (PDFAppAnnotationTools::isMarkup(m_activeKind) && hasTextSelection())
    {
        if (m_activeKind == PDFAppAnnotationToolKind::HighlightComment)
        {
            commentTextSelection();
        }
        else
        {
            markTextSelection(PDFAppAnnotationTools::getMarkupType(m_activeKind));
        }
        return;
    }

    updateMarkupBar();
}

bool PDFAppAnnotationCreator::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != m_canvasWidget)
    {
        return BaseClass::eventFilter(watched, event);
    }

    switch (event->type())
    {
        case QEvent::MouseButtonPress:
        {
            // The selection is being made: the bar waits for the release
            const QMouseEvent* mouseEvent = static_cast<const QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton)
            {
                m_isReleasePending = true;
                if (m_markupBar)
                {
                    m_markupBar->hide();
                }
            }
            break;
        }

        case QEvent::MouseButtonRelease:
        {
            // The text selection tool handles the release after the filter: the
            // selection is read from the event loop
            const QMouseEvent* mouseEvent = static_cast<const QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton)
            {
                m_isReleasePending = true;
                QMetaObject::invokeMethod(this, &PDFAppAnnotationCreator::onCanvasReleasedQueued, Qt::QueuedConnection);
            }
            break;
        }

        case QEvent::MouseMove:
        {
            // A release, which never came (a popup or a dialog opened by the press
            // took the mouse), would hide the bar of every later selection
            const QMouseEvent* mouseEvent = static_cast<const QMouseEvent*>(event);
            if (m_isReleasePending && !mouseEvent->buttons().testFlag(Qt::LeftButton))
            {
                m_isReleasePending = false;
            }
            break;
        }

        case QEvent::KeyPress:
        {
            const QKeyEvent* keyEvent = static_cast<const QKeyEvent*>(event);
            if (keyEvent->key() != Qt::Key_Escape || keyEvent->modifiers() != Qt::NoModifier)
            {
                break;
            }

            // Esc in the step 2 of Link to page returns to the step 1 (note 1172:28734)
            if (m_activeKind == PDFAppAnnotationToolKind::PageLink && m_pageLinkTool && m_pageLinkTool->getStep() == PDFAppPageLinkTool::Step::Target)
            {
                m_pageLinkTool->goBack();
                event->accept();
                return true;
            }

            // Esc of a drawn text box cancels the popup (the tool stays)
            if (m_activeKind == PDFAppAnnotationToolKind::TextBox && m_textBoxPopup && m_textBoxPopup->isVisible())
            {
                m_textBoxPopup->cancel();
                event->accept();
                return true;
            }

            if (m_markupBar && m_markupBar->isVisible())
            {
                m_markupBar->hide();
            }
            break;
        }

        default:
            break;
    }

    return BaseClass::eventFilter(watched, event);
}

PDFAppAnnotationToolBar* PDFAppAnnotationCreator::getToolBar() const
{
    return m_toolBar.data();
}

PDFAppMarkupBar* PDFAppAnnotationCreator::getMarkupBar() const
{
    return m_markupBar.data();
}

PDFAppTextBoxPopup* PDFAppAnnotationCreator::getTextBoxPopup() const
{
    return m_textBoxPopup.data();
}

PDFAppWebLinkDialog* PDFAppAnnotationCreator::getWebLinkDialog() const
{
    return m_webLinkDialog.data();
}

}   // namespace pdf
