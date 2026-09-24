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

#include "pdfdocumentwriter.h"
#include "pdfdocumentreader.h"
#include "pdfconstants.h"
#include "pdfvisitor.h"
#include "pdfparser.h"

#include <QFile>
#include <QBuffer>
#include <QSaveFile>

#include <map>

#include "pdfdbgheap.h"

namespace pdf
{

class PDFWriteObjectVisitor : public PDFAbstractVisitor
{
public:
    explicit PDFWriteObjectVisitor(QIODevice* device) :
        m_device(device)
    {

    }

    virtual void visitNull() override;
    virtual void visitBool(bool value) override;
    virtual void visitInt(PDFInteger value) override;
    virtual void visitReal(PDFReal value) override;
    virtual void visitString(PDFStringRef string) override;
    virtual void visitName(PDFStringRef name) override;
    virtual void visitArray(const PDFArray* array) override;
    virtual void visitDictionary(const PDFDictionary* dictionary) override;
    virtual void visitStream(const PDFStream* stream) override;
    virtual void visitReference(const PDFObjectReference reference) override;

    PDFObject getDecryptedObject();

private:
    void writeName(const QByteArray& string);

    QIODevice* m_device;
};

void PDFWriteObjectVisitor::visitNull()
{
    m_device->write("null ");
}

void PDFWriteObjectVisitor::visitBool(bool value)
{
    if (value)
    {
        m_device->write("true ");
    }
    else
    {
        m_device->write("false ");
    }
}

void PDFWriteObjectVisitor::visitInt(PDFInteger value)
{
    m_device->write(QString::number(value).toLatin1());
    m_device->write(" ");
}

void PDFWriteObjectVisitor::visitReal(PDFReal value)
{
    // Jakub Melka: we use 5 digits, because they are specified
    // in PDF 1.7 specification, appendix C, Table C.1, where it is defined,
    // that number of significant digits of precision is 5.
    m_device->write(QString::number(value, 'f', 5).toLatin1());
    m_device->write(" ");
}

void PDFWriteObjectVisitor::visitString(PDFStringRef string)
{
    QByteArray data = string.getString();
    if (data.indexOf('(') != -1 ||
        data.indexOf(')') != -1 ||
        data.indexOf('\\') != -1)
    {
        m_device->write("<");
        m_device->write(data.toHex());
        m_device->write(">");
    }
    else
    {
        m_device->write("(");
        m_device->write(data);
        m_device->write(")");
    }

    m_device->write(" ");
}

void PDFWriteObjectVisitor::writeName(const QByteArray& string)
{
    m_device->write("/");

    for (const char character : string)
    {
        if (PDFLexicalAnalyzer::isRegular(character))
        {
            m_device->write(&character, 1);
        }
        else
        {
            m_device->write("#");
            m_device->write(QByteArray(&character, 1).toHex());
        }
    }

    m_device->write(" ");
}

void PDFWriteObjectVisitor::visitName(PDFStringRef name)
{
    writeName(name.getString());
}

void PDFWriteObjectVisitor::visitArray(const PDFArray* array)
{
    m_device->write("[ ");
    acceptArray(array);
    m_device->write("] ");
}

void PDFWriteObjectVisitor::visitDictionary(const PDFDictionary* dictionary)
{
    m_device->write("<< ");

    for (size_t i = 0, count = dictionary->getCount(); i < count; ++i)
    {
        writeName(dictionary->getKey(i).getString());
        dictionary->getValue(i).accept(this);
    }

    m_device->write(">> ");
}

void PDFWriteObjectVisitor::visitStream(const PDFStream* stream)
{
    visitDictionary(stream->getDictionary());

    m_device->write("stream");
    m_device->write("\x0D\x0A");
    m_device->write(*stream->getContent());
    m_device->write("\x0D\x0A");
    m_device->write("endstream");
    m_device->write("\x0D\x0A");
}

void PDFWriteObjectVisitor::visitReference(const PDFObjectReference reference)
{
    visitInt(reference.objectNumber);
    visitInt(reference.generation);
    m_device->write("R ");
}

PDFOperationResult PDFDocumentWriter::write(const QString& fileName, const PDFDocument* document, bool safeWrite)
{
    Q_ASSERT(document);

    const PDFObjectStorage& storage = document->getStorage();
    if (!storage.getSecurityHandler()->isEncryptionAllowed())
    {
        return tr("Writing of encrypted documents is not supported.");
    }

    if (safeWrite)
    {
        QSaveFile file(fileName);
        file.setDirectWriteFallback(true);

        if (file.open(QFile::WriteOnly | QFile::Truncate))
        {
            PDFOperationResult result = write(&file, document);
            if (result)
            {
                if (!file.commit())
                {
                    return tr("File '%1' can't be opened for writing. %2").arg(fileName, file.errorString());
                }
            }
            else
            {
                file.cancelWriting();
            }
            return result;
        }
        else
        {
            return tr("File '%1' can't be opened for writing. %2").arg(fileName, file.errorString());
        }
    }
    else
    {
        QFile file(fileName);

        if (file.open(QFile::WriteOnly | QFile::Truncate))
        {
            PDFOperationResult result = write(&file, document);
            file.close();

            if (!result)
            {
                // If some error occured, then remove invalid file
                file.remove();
            }

            return result;
        }
        else
        {
            return tr("File '%1' can't be opened for writing. %2").arg(fileName, file.errorString());
        }
    }
}

PDFOperationResult PDFDocumentWriter::write(QIODevice* device, const PDFDocument* document)
{
    if (!device->isWritable())
    {
        return tr("Device is not writable.");
    }

    const PDFObjectStorage& storage = document->getStorage();
    const PDFObjectStorage::PDFObjects& objects = storage.getObjects();
    const size_t objectCount = objects.size();
    const bool isEncrypted = storage.getSecurityHandler()->getMode() != EncryptionMode::None;
    if (!storage.getSecurityHandler()->isEncryptionAllowed())
    {
        return tr("Writing of encrypted documents is not supported.");
    }

    // Write header
    PDFVersion version = document->getInfo()->version;
    device->write(QString("%PDF-%1.%2").arg(version.major).arg(version.minor).toLatin1());
    writeCRLF(device);
    device->write("% PDF producer: ");
    device->write(PDF_LIBRARY_NAME);
    writeCRLF(device);
    writeCRLF(device);
    writeCRLF(device);

    PDFObjectReference encryptObjectReference;
    PDFObject encryptObject = document->getTrailerDictionary()->get("Encrypt");
    if (encryptObject.isReference())
    {
        encryptObjectReference = encryptObject.getReference();
    }

    // Write objects
    std::vector<PDFInteger> offsets(objectCount, -1);
    for (size_t i = 0; i < objectCount; ++i)
    {
        const PDFObjectStorage::Entry& entry = objects[i];
        if (entry.object.isNull())
        {
            continue;
        }

        // Jakub Melka: we must mark actual position of object
        offsets[i] = device->pos();

        if (isEncrypted)
        {
            PDFObjectReference reference(i, entry.generation);
            PDFObject objectToWrite = entry.object;

            if (reference != encryptObjectReference)
            {
                objectToWrite = storage.getSecurityHandler()->encryptObject(objectToWrite, reference);
            }

            PDFWriteObjectVisitor visitor(device);
            writeObjectHeader(device, reference);
            objectToWrite.accept(&visitor);
            writeObjectFooter(device);
        }
        else
        {
            PDFWriteObjectVisitor visitor(device);
            writeObjectHeader(device, PDFObjectReference(i, entry.generation));
            entry.object.accept(&visitor);
            writeObjectFooter(device);
        }
    }

    // Write cross-reference table
    PDFInteger xrefOffset = device->pos();
    device->write("xref");
    writeCRLF(device);
    device->write(QString("0 %1").arg(objectCount).toLatin1());
    writeCRLF(device);

    for (size_t i = 0; i < objectCount; ++i)
    {
        const PDFObjectStorage::Entry& entry = objects[i];
        PDFInteger generation = entry.generation;

        if (i == 0)
        {
            generation = 65535;
        }

        PDFInteger offset = offsets[i];
        if (offset == -1)
        {
            offset = 0;
        }

        QString offsetString = QString::number(offset).rightJustified(10, QChar('0'), true);
        QString generationString = QString::number(generation).rightJustified(5, QChar('0'), true);

        device->write(offsetString.toLatin1());
        device->write(" ");
        device->write(generationString.toLatin1());
        device->write(" ");
        device->write(entry.object.isNull() ? "f" : "n");
        writeCRLF(device);
    }

    // Jakub Melka: Adjust trailer dictionary, to be really dictionary, not a stream
    PDFDictionary trailerDictionary = *document->getTrailerDictionary();
    PDFDictionary newTrailerDictionary;

    for (const char* entry : { "Size", "Root", "Encrypt", "Info", "ID"})
    {
        PDFObject object = trailerDictionary.get(entry);
        if (!object.isNull())
        {
            newTrailerDictionary.addEntry(PDFInplaceOrMemoryString(entry), qMove(object));
        }
    }

    PDFObject trailerDictionaryObject = PDFObject::createDictionary(PDFDictionary(qMove(newTrailerDictionary)));

    device->write("trailer");
    writeCRLF(device);
    PDFWriteObjectVisitor trailerVisitor(device);
    trailerDictionaryObject.accept(&trailerVisitor);
    writeCRLF(device);
    device->write("startxref");
    writeCRLF(device);
    device->write(QString::number(xrefOffset).toLatin1());
    writeCRLF(device);

    // Write footer
    device->write("%%EOF");

    return true;
}

bool PDFDocumentWriter::findLastCrossReferenceSection(const QByteArray& data, PDFInteger& offset, bool& isCrossReferenceStream)
{
    const qsizetype startXrefPosition = data.lastIndexOf("startxref");
    if (startXrefPosition < 0)
    {
        return false;
    }

    auto isWhitespace = [](char c) { return c == ' ' || c == '\r' || c == '\n' || c == '\t' || c == '\f' || c == '\0'; };

    qsizetype position = startXrefPosition + qsizetype(std::strlen("startxref"));
    while (position < data.size() && isWhitespace(data[position]))
    {
        ++position;
    }

    PDFInteger parsedOffset = 0;
    qsizetype digitCount = 0;
    while (position < data.size() && data[position] >= '0' && data[position] <= '9')
    {
        parsedOffset = parsedOffset * 10 + (data[position] - '0');
        ++position;
        ++digitCount;
    }

    if (digitCount == 0 || parsedOffset < 0 || parsedOffset >= data.size())
    {
        return false;
    }

    // The section is either a classic table starting with the 'xref' keyword,
    // or a cross-reference stream, which is an indirect object.
    qsizetype sectionPosition = parsedOffset;
    while (sectionPosition < data.size() && isWhitespace(data[sectionPosition]))
    {
        ++sectionPosition;
    }

    if (data.mid(sectionPosition, 4) == "xref")
    {
        isCrossReferenceStream = false;
    }
    else if (sectionPosition < data.size() && data[sectionPosition] >= '0' && data[sectionPosition] <= '9')
    {
        isCrossReferenceStream = true;
    }
    else
    {
        return false;
    }

    offset = parsedOffset;
    return true;
}

PDFOperationResult PDFDocumentWriter::writeIncrementalUpdate(QIODevice* device, const QByteArray& originalData, const PDFDocument* document)
{
    if (!device->isWritable())
    {
        return tr("Device is not writable.");
    }

    // The original document is parsed again from its data, because the document
    // being written can be derived from an edited version of it - the objects are
    // compared with what is really stored in the data.
    PDFDocumentReader reader(nullptr, nullptr, false, false);
    const PDFDocument originalDocument = reader.readFromBuffer(originalData);
    if (reader.getReadingResult() != PDFDocumentReader::Result::OK)
    {
        return tr("Original document cannot be read, so it cannot be updated incrementally.");
    }

    PDFInteger previousXrefOffset = -1;
    bool previousXrefIsStream = false;
    if (!findLastCrossReferenceSection(originalData, previousXrefOffset, previousXrefIsStream))
    {
        return tr("Cross-reference section of the original document was not found, so it cannot be updated incrementally.");
    }

    const PDFObjectStorage& storage = document->getStorage();
    const PDFObjectStorage::PDFObjects& objects = storage.getObjects();
    const PDFObjectStorage::PDFObjects& originalObjects = originalDocument.getStorage().getObjects();
    const bool isEncrypted = storage.getSecurityHandler()->getMode() != EncryptionMode::None;
    if (!storage.getSecurityHandler()->isEncryptionAllowed())
    {
        return tr("Writing of encrypted documents is not supported.");
    }

    // Only the objects, which differ from the original document, are written.
    // A removed object is written as null, because a free entry in the update
    // would be overridden by the older occupied entry of the original document.
    std::vector<size_t> changedObjects;
    for (size_t i = 1; i < objects.size(); ++i)
    {
        const bool isNewObject = i >= originalObjects.size();
        if (isNewObject ? !objects[i].object.isNull() : objects[i] != originalObjects[i])
        {
            changedObjects.push_back(i);
        }
    }

    PDFObjectReference encryptObjectReference;
    PDFObject encryptObject = document->getTrailerDictionary()->get("Encrypt");
    if (encryptObject.isReference())
    {
        encryptObjectReference = encryptObject.getReference();
    }

    // The update is appended after the original data, so the offsets written
    // into the cross-reference section are the positions in the device.
    device->write(originalData);
    if (!originalData.endsWith('\n') && !originalData.endsWith('\r'))
    {
        writeCRLF(device);
    }

    std::map<size_t, PDFInteger> offsets;
    for (const size_t i : changedObjects)
    {
        const PDFObjectStorage::Entry& entry = objects[i];
        const PDFObjectReference reference(PDFInteger(i), entry.generation);
        offsets[i] = device->pos();

        PDFObject objectToWrite = entry.object;
        if (isEncrypted && reference != encryptObjectReference)
        {
            objectToWrite = storage.getSecurityHandler()->encryptObject(objectToWrite, reference);
        }

        PDFWriteObjectVisitor visitor(device);
        writeObjectHeader(device, reference);
        objectToWrite.accept(&visitor);
        writeObjectFooter(device);
    }

    // Entries of the trailer, which is either a classic trailer dictionary, or
    // the dictionary of the cross-reference stream - depending on the format of
    // the last cross-reference section of the original document. Mixing the
    // formats is not allowed by the specification.
    PDFInteger size = qMax<PDFInteger>(PDFInteger(objects.size()), PDFInteger(originalObjects.size()));
    PDFDictionary trailerDictionary;
    auto addTrailerEntries = [&]()
    {
        for (const char* entry : { "Root", "Encrypt", "Info", "ID" })
        {
            PDFObject object = document->getTrailerDictionary()->get(entry);
            if (!object.isNull())
            {
                trailerDictionary.addEntry(PDFInplaceOrMemoryString(entry), qMove(object));
            }
        }
        trailerDictionary.addEntry(PDFInplaceOrMemoryString("Prev"), PDFObject::createInteger(previousXrefOffset));
    };

    // Groups consecutive object numbers into subsections
    auto getSubsections = [](const std::vector<size_t>& objectNumbers) -> std::vector<std::pair<size_t, size_t>>
    {
        std::vector<std::pair<size_t, size_t>> subsections;
        for (const size_t objectNumber : objectNumbers)
        {
            if (!subsections.empty() && subsections.back().first + subsections.back().second == objectNumber)
            {
                ++subsections.back().second;
            }
            else
            {
                subsections.emplace_back(objectNumber, 1);
            }
        }
        return subsections;
    };

    const PDFInteger xrefOffset = device->pos();

    if (!previousXrefIsStream)
    {
        device->write("xref");
        writeCRLF(device);

        for (const auto& [firstObjectNumber, count] : getSubsections(changedObjects))
        {
            device->write(QString("%1 %2").arg(firstObjectNumber).arg(count).toLatin1());
            writeCRLF(device);

            for (size_t i = firstObjectNumber; i < firstObjectNumber + count; ++i)
            {
                device->write(QString::number(offsets[i]).rightJustified(10, QChar('0'), true).toLatin1());
                device->write(" ");
                device->write(QString::number(objects[i].generation).rightJustified(5, QChar('0'), true).toLatin1());
                device->write(" n");
                writeCRLF(device);
            }
        }

        trailerDictionary.addEntry(PDFInplaceOrMemoryString("Size"), PDFObject::createInteger(size));
        addTrailerEntries();

        device->write("trailer");
        writeCRLF(device);
        PDFWriteObjectVisitor trailerVisitor(device);
        PDFObject::createDictionary(PDFDictionary(qMove(trailerDictionary))).accept(&trailerVisitor);
        writeCRLF(device);
    }
    else
    {
        // The cross-reference stream is an object itself and it contains its own entry
        const size_t xrefStreamObjectNumber = size_t(size);
        ++size;

        std::vector<size_t> entries = changedObjects;
        entries.push_back(xrefStreamObjectNumber);
        offsets[xrefStreamObjectNumber] = xrefOffset;

        PDFArray indexArray;
        QByteArray data;
        for (const auto& [firstObjectNumber, count] : getSubsections(entries))
        {
            indexArray.appendItem(PDFObject::createInteger(PDFInteger(firstObjectNumber)));
            indexArray.appendItem(PDFObject::createInteger(PDFInteger(count)));

            for (size_t i = firstObjectNumber; i < firstObjectNumber + count; ++i)
            {
                // Field widths are 1 byte for the type, 8 bytes for the offset and 2 bytes for the generation
                const PDFInteger generation = i == xrefStreamObjectNumber ? 0 : objects[i].generation;
                data.append(char(1));
                for (int shift = 56; shift >= 0; shift -= 8)
                {
                    data.append(char((quint64(offsets[i]) >> shift) & 0xFF));
                }
                data.append(char((generation >> 8) & 0xFF));
                data.append(char(generation & 0xFF));
            }
        }

        PDFArray widthArray;
        widthArray.appendItem(PDFObject::createInteger(1));
        widthArray.appendItem(PDFObject::createInteger(8));
        widthArray.appendItem(PDFObject::createInteger(2));

        trailerDictionary.addEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("XRef"));
        trailerDictionary.addEntry(PDFInplaceOrMemoryString("Size"), PDFObject::createInteger(size));
        trailerDictionary.addEntry(PDFInplaceOrMemoryString("W"), PDFObject::createArray(qMove(widthArray)));
        trailerDictionary.addEntry(PDFInplaceOrMemoryString("Index"), PDFObject::createArray(qMove(indexArray)));
        addTrailerEntries();
        trailerDictionary.addEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(data.size()));

        PDFWriteObjectVisitor visitor(device);
        writeObjectHeader(device, PDFObjectReference(PDFInteger(xrefStreamObjectNumber), 0));
        PDFObject::createStream(PDFStream(qMove(trailerDictionary), qMove(data))).accept(&visitor);
        writeObjectFooter(device);
    }

    device->write("startxref");
    writeCRLF(device);
    device->write(QString::number(xrefOffset).toLatin1());
    writeCRLF(device);
    device->write("%%EOF");
    writeCRLF(device);

    return true;
}

void PDFDocumentWriter::writeCRLF(QIODevice* device)
{
    device->write("\x0D\x0A");
}

void PDFDocumentWriter::writeObjectHeader(QIODevice* device, PDFObjectReference reference)
{
    QString objectHeader = QString("%1 %2 obj").arg(QString::number(reference.objectNumber), QString::number(reference.generation));
    device->write(objectHeader.toLatin1());
    writeCRLF(device);
}

void PDFDocumentWriter::writeObjectFooter(QIODevice* device)
{
    device->write("endobj");
    writeCRLF(device);
}

class PDFSizeCounterIODevice : public QIODevice
{
public:
    explicit PDFSizeCounterIODevice(QObject* parent) :
        QIODevice(parent)
    {

    }

    virtual bool isSequential() const override;
    virtual bool open(OpenMode mode) override;
    virtual void close() override;
    virtual qint64 pos() const override;
    virtual qint64 size() const override;
    virtual bool seek(qint64 pos) override;
    virtual bool atEnd() const override;
    virtual bool reset() override;
    virtual qint64 bytesAvailable() const override;
    virtual qint64 bytesToWrite() const override;
    virtual bool canReadLine() const override;
    virtual bool waitForReadyRead(int msecs) override;
    virtual bool waitForBytesWritten(int msecs) override;

protected:
    virtual qint64 readData(char* data, qint64 maxlen) override;
    virtual qint64 readLineData(char* data, qint64 maxlen) override;
    virtual qint64 writeData(const char* data, qint64 len) override;

private:
    OpenMode m_openMode = NotOpen;
    qint64 m_fileSize = 0;
};

bool PDFSizeCounterIODevice::isSequential() const
{
    return true;
}

bool PDFSizeCounterIODevice::open(OpenMode mode)
{
    if (m_openMode == NotOpen)
    {
        setOpenMode(mode);
        return true;
    }
    else
    {
        return false;
    }
}

void PDFSizeCounterIODevice::close()
{
    setOpenMode(NotOpen);
}

qint64 PDFSizeCounterIODevice::pos() const
{
    return m_fileSize;
}

qint64 PDFSizeCounterIODevice::size() const
{
    return m_fileSize;
}

bool PDFSizeCounterIODevice::seek(qint64 pos)
{
    Q_UNUSED(pos);

    return false;
}

bool PDFSizeCounterIODevice::atEnd() const
{
    return true;
}

bool PDFSizeCounterIODevice::reset()
{
    return false;
}

qint64 PDFSizeCounterIODevice::bytesAvailable() const
{
    return 0;
}

qint64 PDFSizeCounterIODevice::bytesToWrite() const
{
    return 0;
}

bool PDFSizeCounterIODevice::canReadLine() const
{
    return false;
}

bool PDFSizeCounterIODevice::waitForReadyRead(int msecs)
{
    Q_UNUSED(msecs);

    return false;
}

bool PDFSizeCounterIODevice::waitForBytesWritten(int msecs)
{
    Q_UNUSED(msecs);

    return false;
}

qint64 PDFSizeCounterIODevice::readData(char* data, qint64 maxlen)
{
    Q_UNUSED(data);
    Q_UNUSED(maxlen);

    return 0;
}

qint64 PDFSizeCounterIODevice::readLineData(char* data, qint64 maxlen)
{
    Q_UNUSED(data);
    Q_UNUSED(maxlen);

    return 0;
}

qint64 PDFSizeCounterIODevice::writeData(const char* data, qint64 len)
{
    Q_UNUSED(data);

    m_fileSize += len;
    return len;
}

qint64 PDFDocumentWriter::getDocumentFileSize(const PDFDocument* document)
{
    PDFSizeCounterIODevice device(nullptr);
    PDFDocumentWriter writer(nullptr);

    device.open(QIODevice::WriteOnly);

    if (writer.write(&device, document))
    {
        device.close();
        return device.pos();
    }

    device.close();
    return -1;
}

qint64 PDFDocumentWriter::getObjectSize(const PDFDocument* document, PDFObjectReference reference)
{
    const PDFObject& object = document->getObjectByReference(reference);

    if (object.isNull())
    {
        return 0;
    }

    PDFSizeCounterIODevice device(nullptr);

    device.open(QIODevice::WriteOnly);

    PDFWriteObjectVisitor visitor(&device);
    writeObjectHeader(&device, reference);
    object.accept(&visitor);
    writeObjectFooter(&device);

    device.close();
    return device.pos();
}

QByteArray PDFDocumentWriter::getSerializedObject(const PDFObject& object)
{
    QBuffer buffer;

    if (buffer.open(QBuffer::WriteOnly))
    {
        PDFWriteObjectVisitor visitor(&buffer);
        object.accept(&visitor);

        buffer.close();
    }

    return buffer.data();
}

}   // namespace pdf
