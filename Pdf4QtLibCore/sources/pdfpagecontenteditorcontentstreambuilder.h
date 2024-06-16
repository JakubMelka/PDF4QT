//    Copyright (C) 2024 Jakub Melka
//
//    This file is part of PDF4QT.
//
//    PDF4QT is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    PDF4QT is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDF4QT. If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFPAGECONTENTEDITORCONTENTSTREAMBUILDER_H
#define PDFPAGECONTENTEDITORCONTENTSTREAMBUILDER_H

#include "pdfpagecontenteditorprocessor.h"

namespace pdf
{
class PDFPageContentElement;

class PDF4QTLIBCORESHARED_EXPORT PDFPageContentEditorContentStreamBuilder
{
public:
    PDFPageContentEditorContentStreamBuilder(PDFDocument* document);

    void writeEditedElement(const PDFEditedPageContentElement* element);

    const QByteArray& getOutputContent() const;

    const PDFDictionary& getFontDictionary() const { return m_fontDictionary; }
    const PDFDictionary& getXObjectDictionary() const { return m_xobjectDictionary; }
    const PDFDictionary& getGraphicStateDictionary() const { return m_graphicStateDictionary; }

    void setFontDictionary(const PDFDictionary& newFontDictionary);

    const QStringList& getErrors() const { return m_errors; }
    void clearErrors() { m_errors.clear(); }

    void writeStyledPath(const QPainterPath& path,
                         const QPen& pen,
                         const QBrush& brush,
                         bool isStroking,
                         bool isFilling);

    void writeImage(const QImage& image, const QRectF& rectangle);

private:
    bool isNeededToWriteCurrentTransformationMatrix() const;

    void writeCurrentTransformationMatrix(QTextStream& stream);
    void writeStateDifference(QTextStream& stream, const PDFPageContentProcessorState& state);

    void writePainterPath(QTextStream& stream,
                          const QPainterPath& path,
                          bool isStroking,
                          bool isFilling);

    void writeText(QTextStream& stream, const QString& text);
    void writeTextCommand(QTextStream& stream, const QXmlStreamReader& reader);

    void writeImage(QTextStream& stream, const QImage& image);

    QByteArray selectFont(const QByteArray& font);
    void addError(const QString& error);

    PDFDocument* m_document = nullptr;
    PDFDictionary m_fontDictionary;
    PDFDictionary m_xobjectDictionary;
    PDFDictionary m_graphicStateDictionary;
    QByteArray m_outputContent;
    PDFPageContentProcessorState m_currentState;
    PDFFontPointer m_textFont;
    QStringList m_errors;
};

}   // namespace pdf

#endif // PDFPAGECONTENTEDITORCONTENTSTREAMBUILDER_H
