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

#include "pdfdocumentxmlexport.h"
#include "pdfconstants.h"
#include "pdfdocumenttooltablewriter.h"
#include "pdfencoding.h"
#include "pdfoperationcontrol.h"
#include "pdfprogress.h"
#include "pdfstreamfilters.h"
#include "pdfvisitor.h"

#include <QIODevice>
#include <QLocale>
#include <QXmlStreamWriter>

#include "pdfdbgheap.h"

namespace pdf
{

/// Writes the objects as XML (the move of PDFXmlExportVisitor of PdfTool xml): the references are
/// written, never followed; every text without the characters, which XML 1.0 does not allow
class PDFDocumentXmlExportVisitor : public PDFAbstractVisitor
{
public:
    /// Bytes of a stream written as hexadecimal digits at once (exportStreams)
    static constexpr qsizetype HEX_CHUNK_BYTES = 64 * 1024;

    explicit PDFDocumentXmlExportVisitor(QXmlStreamWriter* writer, const PDFObjectStorage* storage, const PDFDocumentXmlExport::Options& options, const PDFOperationControl* control) :
        m_writer(writer),
        m_storage(storage),
        m_options(options),
        m_control(control)
    {

    }

    virtual void visitNull() override
    {
        m_writer->writeEmptyElement(QStringLiteral("null"));
    }

    virtual void visitBool(bool value) override
    {
        m_writer->writeTextElement(QStringLiteral("bool"), value ? QStringLiteral("true") : QStringLiteral("false"));
    }

    virtual void visitInt(PDFInteger value) override
    {
        m_writer->writeTextElement(QStringLiteral("int"), QString::number(value));
    }

    virtual void visitReal(PDFReal value) override
    {
        // The shortest text, which reads back as the same number (RS7CORE: 6 significant digits lost the value)
        m_writer->writeTextElement(QStringLiteral("real"), QString::number(value, 'g', QLocale::FloatingPointShortest));
    }

    virtual void visitString(PDFStringRef string) override
    {
        writeTextOrBinary(string.getString(), QStringLiteral("string"));
    }

    virtual void visitName(PDFStringRef name) override
    {
        writeTextOrBinary(name.getString(), QStringLiteral("name"));
    }

    virtual void visitArray(const PDFArray* array) override
    {
        m_writer->writeStartElement(QStringLiteral("array"));
        acceptArray(array);
        m_writer->writeEndElement();
    }

    virtual void visitDictionary(const PDFDictionary* dictionary) override
    {
        m_writer->writeStartElement(QStringLiteral("dictionary"));
        const size_t count = dictionary->getCount();
        for (size_t i = 0; i < count; ++i)
        {
            m_writer->writeStartElement(QStringLiteral("entry"));
            m_writer->writeAttribute(QStringLiteral("key"), PDFDocumentToolTableWriter::toXmlText(QString::fromLatin1(dictionary->getKey(i).getString())));
            dictionary->getValue(i).accept(this);
            m_writer->writeEndElement();
        }
        m_writer->writeEndElement();
    }

    virtual void visitStream(const PDFStream* stream) override
    {
        m_writer->writeStartElement(QStringLiteral("stream"));
        visitDictionary(stream->getDictionary());

        if (m_options.isStreamExported)
        {
            // The hexadecimal digits are written by parts (RS7CORE: the text of the whole stream at once was about
            // ten times its size in the memory)
            const QByteArray* content = stream->getContent();
            m_writer->writeStartElement(QStringLiteral("data"));
            for (qsizetype offset = 0; offset < content->size(); offset += HEX_CHUNK_BYTES)
            {
                m_writer->writeCharacters(QString::fromLatin1(content->mid(offset, HEX_CHUNK_BYTES).toHex().toUpper()));
            }
            m_writer->writeEndElement();
        }

        if (m_options.isStreamWrittenAsText)
        {
            // Q7.18: the limit of the inspector - a bomb is never decoded whole
            const PDFLimitedStreamDecoder::Result result = PDFLimitedStreamDecoder::decode(stream, [this](const PDFObject& object) -> const PDFObject& { return m_storage->getObject(object); },
                                                                                            m_storage->getSecurityHandler(), PDFLimitedStreamDecoder::INSPECTOR_MAXIMUM_DECODED_SIZE, m_control);
            switch (result.status)
            {
                case PDFLimitedStreamDecoder::Status::Decoded:
                {
                    bool isBinary = true;
                    const QString text = PDFEncoding::convertSmartFromByteStringToUnicode(result.data, &isBinary);
                    if (!isBinary)
                    {
                        m_writer->writeTextElement(QStringLiteral("text"), PDFDocumentToolTableWriter::toXmlText(text));
                    }
                    break;
                }

                case PDFLimitedStreamDecoder::Status::Encoded:
                    // An image or an unknown filter: the data are not a text
                    break;

                case PDFLimitedStreamDecoder::Status::LimitExceeded:
                case PDFLimitedStreamDecoder::Status::Error:
                {
                    m_writer->writeStartElement(QStringLiteral("decodeError"));
                    if (!result.filterName.isEmpty())
                    {
                        m_writer->writeAttribute(QStringLiteral("filter"), PDFDocumentToolTableWriter::toXmlText(QString::fromLatin1(result.filterName)));
                    }
                    m_writer->writeCharacters(PDFDocumentToolTableWriter::toXmlText(result.errorMessage));
                    m_writer->writeEndElement();
                    break;
                }

                case PDFLimitedStreamDecoder::Status::Canceled:
                    break;
            }
        }

        m_writer->writeEndElement();
    }

    virtual void visitReference(const PDFObjectReference reference) override
    {
        m_writer->writeStartElement(QStringLiteral("reference"));
        m_writer->writeAttribute(QStringLiteral("id"), QString::number(reference.objectNumber));
        m_writer->writeAttribute(QStringLiteral("gen"), QString::number(reference.generation));
        m_writer->writeCharacters(QStringLiteral("%1 %2 R").arg(reference.objectNumber).arg(reference.generation));
        m_writer->writeEndElement();
    }

private:
    void writeTextOrBinary(const QByteArray& data, const QString& name)
    {
        bool isBinary = true;
        QString text;
        if (!m_options.isStringAlwaysBinary)
        {
            text = PDFEncoding::convertSmartFromByteStringToUnicode(data, &isBinary);
        }
        else
        {
            text = QString::fromLatin1(data.toHex()).toUpper();
        }

        m_writer->writeStartElement(name);
        m_writer->writeAttribute(QStringLiteral("form"), isBinary ? QStringLiteral("binary") : QStringLiteral("text"));
        m_writer->writeCharacters(PDFDocumentToolTableWriter::toXmlText(text));
        m_writer->writeEndElement();
    }

    QXmlStreamWriter* m_writer;
    const PDFObjectStorage* m_storage;
    PDFDocumentXmlExport::Options m_options;
    const PDFOperationControl* m_control;
};

bool PDFDocumentXmlExport::write(const PDFDocument* document, const Options& options, QIODevice* device, const PDFOperationControl* control, PDFProgressSink* progress, QString* errorMessage)
{
    QString dummy;
    QString& error = errorMessage ? *errorMessage : dummy;
    error.clear();

    if (!document || !device || !device->isWritable())
    {
        error = tr("The XML cannot be written.");
        return false;
    }

    const PDFObjectStorage& storage = document->getStorage();
    QXmlStreamWriter writer(device);
    if (options.isIndented)
    {
        writer.setAutoFormatting(true);
        writer.setAutoFormattingIndent(2);
    }

    // K7.21: the generator is the library ("PDF4QT <version>")
    writer.writeStartDocument();
    writer.writeComment(QStringLiteral(" Processed by %1 ").arg(QString::fromLatin1(PDF_LIBRARY_NAME)));
    writer.writeStartElement(QStringLiteral("document"));

    PDFDocumentXmlExportVisitor visitor(&writer, &storage, options, control);
    writer.writeStartElement(QStringLiteral("trailer"));
    storage.getTrailerDictionary().accept(&visitor);
    writer.writeEndElement();

    const PDFObjectStorage::PDFObjects& objects = storage.getObjects();
    const size_t count = objects.size();
    const size_t stepSize = qMax<size_t>(1, count / 100);
    if (progress)
    {
        progress->start(count / stepSize + 1, ProgressStartupInfo());
    }

    for (size_t i = 0; i < count; ++i)
    {
        if (PDFOperationControl::isOperationCanceled(control))
        {
            error = tr("The export was stopped.");
            return false;
        }

        const PDFObjectStorage::Entry& entry = objects[i];
        if (!entry.object.isNull())
        {
            writer.writeStartElement(QStringLiteral("pdfobject"));
            writer.writeAttribute(QStringLiteral("id"), QString::number(i));
            writer.writeAttribute(QStringLiteral("gen"), QString::number(entry.generation));
            entry.object.accept(&visitor);
            writer.writeEndElement();
        }

        if (progress && (i % stepSize) == 0)
        {
            progress->step();
        }

        if (writer.hasError())
        {
            break;
        }
    }

    writer.writeEndElement();
    writer.writeEndDocument();

    if (progress)
    {
        progress->finish();
    }

    if (writer.hasError())
    {
        error = tr("The XML cannot be written: %1").arg(device->errorString());
        return false;
    }
    return true;
}

qint64 PDFDocumentXmlExport::estimateSize(const PDFDocument* document, const Options& options)
{
    if (!document)
    {
        return -1;
    }

    // About 200 bytes of the elements of an object, the hexadecimal data twice the raw data, a decoded
    // text about three times the raw data (an estimate: the decoded size is not known before decoding)
    qint64 size = 256;
    for (const PDFObjectStorage::Entry& entry : document->getStorage().getObjects())
    {
        if (entry.object.isNull())
        {
            continue;
        }

        size += 200;
        if (entry.object.isStream())
        {
            const qint64 rawSize = entry.object.getStream()->getContent()->size();
            size += options.isStreamExported ? 2 * rawSize : 0;
            size += options.isStreamWrittenAsText ? 3 * rawSize : 0;
        }
    }
    return size;
}

}   // namespace pdf
