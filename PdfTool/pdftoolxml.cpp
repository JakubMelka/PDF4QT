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
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.

#include "pdftoolxml.h"
#include "pdfvisitor.h"
#include "pdfencoding.h"

#include <QXmlStreamWriter>

namespace pdftool
{

static PDFToolXmlApplication s_xmlApplication;

class PDFXmlExportVisitor : public pdf::PDFAbstractVisitor
{
public:
    PDFXmlExportVisitor(QXmlStreamWriter* writer) :
        m_writer(writer)
    {

    }

    virtual void visitNull() override;
    virtual void visitBool(bool value) override;
    virtual void visitInt(pdf::PDFInteger value) override;
    virtual void visitReal(pdf::PDFReal value) override;
    virtual void visitString(pdf::PDFStringRef string) override;
    virtual void visitName(pdf::PDFStringRef name) override;
    virtual void visitArray(const pdf::PDFArray* array) override;
    virtual void visitDictionary(const pdf::PDFDictionary* dictionary) override;
    virtual void visitStream(const pdf::PDFStream* stream) override;
    virtual void visitReference(const pdf::PDFObjectReference reference) override;

private:
    void writeTextOrBinary(const QByteArray& stream, QString name);

    QXmlStreamWriter* m_writer;
};

void PDFXmlExportVisitor::visitNull()
{
    m_writer->writeEmptyElement("null");
}

void PDFXmlExportVisitor::visitBool(bool value)
{
    m_writer->writeTextElement("bool", value ? "true" : "false");
}

void PDFXmlExportVisitor::visitInt(pdf::PDFInteger value)
{
    m_writer->writeTextElement("int", QString::number(value));
}

void PDFXmlExportVisitor::visitReal(pdf::PDFReal value)
{
    m_writer->writeTextElement("real", QString::number(value));
}

void PDFXmlExportVisitor::visitString(pdf::PDFStringRef string)
{
    writeTextOrBinary(string.getString(), "string");
}

void PDFXmlExportVisitor::visitName(pdf::PDFStringRef name)
{
    writeTextOrBinary(name.getString(), "name");
}

void PDFXmlExportVisitor::visitArray(const pdf::PDFArray* array)
{
    m_writer->writeStartElement("array");
    acceptArray(array);
    m_writer->writeEndElement();
}

void PDFXmlExportVisitor::visitDictionary(const pdf::PDFDictionary* dictionary)
{
    m_writer->writeStartElement("dictionary");

    const size_t count = dictionary->getCount();
    for (size_t i = 0; i < count; ++i)
    {
        m_writer->writeStartElement("entry");
        m_writer->writeAttribute("key", QString::fromLatin1(dictionary->getKey(i).getString()));
        dictionary->getValue(i).accept(this);
        m_writer->writeEndElement();
    }

    m_writer->writeEndElement();
}

void PDFXmlExportVisitor::visitStream(const pdf::PDFStream* stream)
{
    m_writer->writeStartElement("stream");
    visitDictionary(stream->getDictionary());
    dodelat export dat
    m_writer->writeEndElement();
}

void PDFXmlExportVisitor::visitReference(const pdf::PDFObjectReference reference)
{
    m_writer->writeStartElement("reference");
    m_writer->writeAttribute("id", QString::number(reference.objectNumber));
    m_writer->writeAttribute("gen", QString::number(reference.generation));
    m_writer->writeCharacters(QString("%1 %2 R").arg(reference.objectNumber).arg(reference.generation));
    m_writer->writeEndElement();
}

void PDFXmlExportVisitor::writeTextOrBinary(const QByteArray& stream, QString name)
{
    bool isBinary = false;
    m_writer->writeStartElement(name);
    QString text = pdf::PDFEncoding::convertSmartFromByteStringToUnicode(stream, &isBinary);
    m_writer->writeAttribute("form", isBinary ? "binary" : "text");
    m_writer->writeCharacters(text);
    m_writer->writeEndElement();
}

QString PDFToolXmlApplication::getStandardString(PDFToolAbstractApplication::StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "xml";

        case Name:
            return PDFToolTranslationContext::tr("XML export");

        case Description:
            return PDFToolTranslationContext::tr("Export internal data structure to xml.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

int PDFToolXmlApplication::execute(const PDFToolOptions& options)
{
    pdf::PDFDocument document;
    if (!readDocument(options, document))
    {
        return ErrorDocumentReading;
    }

    QString xmlString;
    QXmlStreamWriter writer(&xmlString);

    if (options.xmlUseIndent)
    {
        writer.setAutoFormatting(true);
        writer.setAutoFormattingIndent(2);
    }

    QString comment = QString("Processed by %1 %2").arg(QCoreApplication::applicationName(), QCoreApplication::applicationVersion());
    writer.writeStartDocument();
    writer.writeComment(comment);
    writer.writeStartElement("document");

    PDFXmlExportVisitor visitor(&writer);
    writer.writeStartElement("trailer");
    document.getStorage().getTrailerDictionary().accept(&visitor);
    writer.writeEndElement();

    pdf::PDFObjectStorage::PDFObjects entries = document.getStorage().getObjects();
    for (pdf::PDFInteger i = 0; i < pdf::PDFInteger(entries.size()); ++i)
    {
        const pdf::PDFObjectStorage::Entry& entry = entries[i];

        if (entry.object.isNull())
        {
            continue;
        }

        writer.writeStartElement("pdfobject");
        writer.writeAttribute("id", QString::number(i));
        writer.writeAttribute("gen", QString::number(entry.generation));
        entry.object.accept(&visitor);
        writer.writeEndElement();
    }

    writer.writeEndElement();
    writer.writeEndDocument();

    PDFConsole::writeText(xmlString);
    return ExitSuccess;
}

PDFToolAbstractApplication::Options PDFToolXmlApplication::getOptionsFlags() const
{
    return OpenDocument | XmlExport;
}

}   // namespace pdftool
