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

#include "pdfdocumentreader.h"
#include "pdfconstants.h"
#include "pdfxreftable.h"
#include "pdfexception.h"
#include "pdfparser.h"
#include "pdfstreamfilters.h"
#include "pdfexecutionpolicy.h"

#include <QFile>
#include <QCryptographicHash>

#include "pdfdbgheap.h"

#include <regex>
#include <cctype>
#include <cstring>
#include <algorithm>
#include <execution>

namespace pdf
{

PDFDocumentReader::PDFDocumentReader(PDFProgress* progress, const std::function<QString(bool*)>& getPasswordCallback, bool permissive, bool authorizeOwnerOnly) :
    m_result(Result::OK),
    m_getPasswordCallback(getPasswordCallback),
    m_progress(progress),
    m_permissive(permissive),
    m_authorizeOwnerOnly(authorizeOwnerOnly)
{

}

PDFDocument PDFDocumentReader::readFromFile(const QString& fileName)
{
    QFile file(fileName);

    reset();

    if (file.exists())
    {
        if (file.open(QFile::ReadOnly))
        {
            PDFDocument document = readFromDevice(&file);
            file.close();
            return document;
        }
        else
        {
            m_result = Result::Failed;
            m_errorMessage = tr("File '%1' cannot be opened for reading. %1").arg(file.errorString());
        }
    }
    else
    {
        m_result = Result::Failed;
        m_errorMessage = tr("File '%1' doesn't exist.").arg(fileName);
    }

    return PDFDocument();
}

PDFDocument PDFDocumentReader::readFromDevice(QIODevice* device)
{
    reset();

    if (device->isOpen())
    {
        if (device->isReadable())
        {
            // Do not close the device, it was not opened by us.
            return readFromBuffer(device->readAll());
        }
        else
        {
            m_result = Result::Failed;
            m_errorMessage = tr("Device is not opened for reading.");
        }
    }
    else if (device->open(QIODevice::ReadOnly))
    {
        QByteArray byteArray = device->readAll();
        device->close();
        return readFromBuffer(byteArray);
    }
    else
    {
        m_result = Result::Failed;
        m_errorMessage = tr("Can't open device for reading.");
    }

    return PDFDocument();
}

void PDFDocumentReader::checkFooter(const QByteArray& buffer)
{
    if (findFromEnd(PDF_END_OF_FILE_MARK, buffer, PDF_FOOTER_SCAN_LIMIT) == FIND_NOT_FOUND_RESULT)
    {
        QString message = tr("End of file marking was not found.");
        if (m_permissive)
        {
            QMutexLocker lock(&m_mutex);
            m_warnings << message;
        }
        else
        {
            throw PDFException(message);
        }
    }
}

void PDFDocumentReader::checkHeader(const QByteArray& buffer)
{
    // According to PDF Reference 1.7, Appendix H, file header can have two formats:
    //  - %PDF-x.x
    //  - %!PS-Adobe-y.y PDF-x.x
    // We will search for both of these formats.
    std::regex headerRegExp(PDF_FILE_HEADER_REGEXP);
    std::cmatch headerMatch;

    auto itBegin = buffer.cbegin();
    auto itEnd = std::next(buffer.cbegin(), qMin(buffer.size(), PDF_HEADER_SCAN_LIMIT));

    if (std::regex_search(itBegin, itEnd, headerMatch, headerRegExp))
    {
        // Size depends on regular expression, not on the text (if regular expresion is matched)
        Q_ASSERT(headerMatch.size() == 3);
        Q_ASSERT(headerMatch[1].matched != headerMatch[2].matched);

        for (int i : { 1, 2 })
        {
            if (headerMatch[i].matched)
            {
                Q_ASSERT(std::distance(headerMatch[i].first, headerMatch[i].second) == 3);
                m_version = PDFVersion(*headerMatch[i].first - '0', *std::prev(headerMatch[i].second) - '0');
                break;
            }
        }
    }
    else
    {
        throw PDFException(tr("Header of PDF file was not found."));
    }

    // Check, if version is valid
    if (!m_version.isValid())
    {
        throw PDFException(tr("Version of the PDF file is not valid."));
    }
}

PDFInteger PDFDocumentReader::findXrefTableOffset(const QByteArray& buffer)
{
    const int startXRefPosition = findFromEnd(PDF_START_OF_XREF_MARK, buffer, PDF_FOOTER_SCAN_LIMIT);
    if (startXRefPosition == FIND_NOT_FOUND_RESULT)
    {
        throw PDFException(tr("Start of object reference table not found."));
    }

    Q_ASSERT(startXRefPosition + std::strlen(PDF_START_OF_XREF_MARK) < static_cast<size_t>(buffer.size()));
    PDFLexicalAnalyzer analyzer(buffer.constData() + startXRefPosition + std::strlen(PDF_START_OF_XREF_MARK), buffer.constData() + buffer.size());
    const PDFLexicalAnalyzer::Token token = analyzer.fetch();
    if (token.type != PDFLexicalAnalyzer::TokenType::Integer)
    {
        throw PDFException(tr("Start of object reference table not found."));
    }

    const PDFInteger firstXrefTableOffset = token.data.toLongLong();
    return firstXrefTableOffset;
}

PDFObject PDFDocumentReader::getObject(PDFParsingContext* context, PDFInteger offset, PDFObjectReference reference) const
{
    PDFParsingContext::PDFParsingContextGuard guard(context, reference);

    PDFParser parser(m_source, context, PDFParser::AllowStreams);
    parser.seek(offset);

    PDFObject objectNumber = parser.getObject();
    PDFObject generation = parser.getObject();

    if (!objectNumber.isInt() || !generation.isInt())
    {
        throw PDFException(tr("Can't read object at position %1.").arg(offset));
    }

    if (!parser.fetchCommand(PDF_OBJECT_START_MARK))
    {
        throw PDFException(tr("Can't read object at position %1.").arg(offset));
    }

    PDFObject object = parser.getObject();

    if (!parser.fetchCommand(PDF_OBJECT_END_MARK))
    {
        throw PDFException(tr("Can't read object at position %1.").arg(offset));
    }

    PDFObjectReference scannedReference(objectNumber.getInteger(), generation.getInteger());
    if (scannedReference != reference)
    {
        throw PDFException(tr("Can't read object at position %1.").arg(offset));
    }

    return object;
}

PDFObject PDFDocumentReader::getObjectFromXrefTable(PDFXRefTable* xrefTable, PDFParsingContext* context, PDFObjectReference reference) const
{
    const PDFXRefTable::Entry& entry = xrefTable->getEntry(reference);
    switch (entry.type)
    {
        case PDFXRefTable::EntryType::Free:
            return PDFObject();

        case PDFXRefTable::EntryType::Occupied:
        {
            Q_ASSERT(entry.reference == reference);
            return getObject(context, entry.offset, reference);
        }

        default:
        {
            Q_ASSERT(false);
            break;
        }
    }

    return PDFObject();
}

PDFObject PDFDocumentReader::readDamagedTrailerDictionary() const
{
    PDFObject object = PDFObject::createDictionary(PDFDictionaryBuilder(PDFDictionaryBuilder()));
    PDFParsingContext context([](PDFParsingContext*, PDFObjectReference){ return PDFObject(); });

    int offset = 0;
    while (offset < m_source.size())
    {
        offset = m_source.indexOf(PDF_XREF_TRAILER, offset);

        if (offset == -1)
        {
            break;
        }

        offset += static_cast<int>(std::strlen(PDF_XREF_TRAILER));

        // Try to read trailer dictioanry
        try
        {
            PDFParser parser(m_source, &context, PDFParser::None);
            parser.seek(offset);

            PDFObject trailerDictionaryObject = parser.getObject();
            if (trailerDictionaryObject.isDictionary())
            {
                object = PDFObjectManipulator::merge(object, trailerDictionaryObject, PDFObjectManipulator::RemoveNullObjects);
            }
        }
        catch (const PDFException&)
        {
            // Do nothing...
        }
    }

    return object;
}

PDFDocumentReader::Result PDFDocumentReader::processReferenceTableEntries(PDFXRefTable* xrefTable, const std::vector<PDFXRefTable::Entry>& occupiedEntries, PDFObjectStorage::PDFObjects& objects)
{
    auto objectFetcher = [this, xrefTable](PDFParsingContext* context, PDFObjectReference reference) { return getObjectFromXrefTable(xrefTable, context, reference); };
    auto processEntry = [this, &objectFetcher, &objects](const PDFXRefTable::Entry& entry)
    {
        Q_ASSERT(entry.type == PDFXRefTable::EntryType::Occupied);

        if (m_result == Result::OK)
        {
            try
            {
                PDFParsingContext context(objectFetcher);
                PDFObject object = getObject(&context, entry.offset, entry.reference);

                progressStep();

                QMutexLocker lock(&m_mutex);
                objects[entry.reference.objectNumber] = PDFObjectStorage::Entry(entry.reference.generation, object);
            }
            catch (const PDFException& exception)
            {
                QMutexLocker lock(&m_mutex);

                if (m_permissive)
                {
                    m_warnings << exception.getMessage();
                }
                else
                {
                    m_result = Result::Failed;
                    m_errorMessage = exception.getMessage();
                }
            }
        }
    };

    // Now, we are ready to scan all objects
    if (!occupiedEntries.empty())
    {
        progressStart(occupiedEntries.size(), PDFTranslationContext::tr("Reading contents of document..."));
        PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Unknown, occupiedEntries.cbegin(), occupiedEntries.cend(), processEntry);
        progressFinish();
    }

    return m_result;
}

PDFDocumentReader::Result PDFDocumentReader::processSecurityHandler(const PDFObject& trailerDictionaryObject,
                                                                    const std::vector<PDFXRefTable::Entry>& occupiedEntries,
                                                                    PDFObjectStorage::PDFObjects& objects)
{
    const PDFDictionary* trailerDictionary = nullptr;
    if (trailerDictionaryObject.isDictionary())
    {
        trailerDictionary = trailerDictionaryObject.getDictionary();
    }
    else if (trailerDictionaryObject.isStream())
    {
        const PDFStream* stream = trailerDictionaryObject.getStream();
        trailerDictionary = stream->getDictionary();
    }
    else
    {
        throw PDFException(tr("Invalid trailer dictionary."));
    }

    // Read the document ID
    QByteArray id;
    const PDFObject& idArrayObject = trailerDictionary->get("ID");
    if (idArrayObject.isArray())
    {
        const PDFArray* idArray = idArrayObject.getArray();
        if (idArray->getCount() > 0)
        {
            const PDFObject& idArrayItem = idArray->getItem(0);
            if (idArrayItem.isString())
            {
                id = idArrayItem.getString();
            }
        }
    }

    PDFObjectReference encryptObjectReference;
    PDFObject encryptObject = trailerDictionary->get("Encrypt");
    if (encryptObject.isReference())
    {
        encryptObjectReference = encryptObject.getReference();
        if (static_cast<size_t>(encryptObjectReference.objectNumber) < objects.size() && objects[encryptObjectReference.objectNumber].generation == encryptObjectReference.generation)
        {
            encryptObject = objects[encryptObjectReference.objectNumber].object;
        }
    }

    // Read the security handler
    m_securityHandler = PDFSecurityHandler::createSecurityHandler(encryptObject, id);
    PDFSecurityHandler::AuthorizationResult authorizationResult = m_securityHandler->authenticate(m_getPasswordCallback, m_authorizeOwnerOnly);

    if (authorizationResult == PDFSecurityHandler::AuthorizationResult::Cancelled)
    {
        // User cancelled the document reading
        m_result = Result::Cancelled;
        return m_result;
    }

    if (authorizationResult == PDFSecurityHandler::AuthorizationResult::Failed)
    {
        throw PDFException(PDFTranslationContext::tr("Authorization failed. Bad password provided."));
    }

    // Now, decrypt the document, if we are authorized. We must also check, if we have to decrypt the object.
    // According to the PDF specification, following items are ommited from encryption:
    //      1) Values for ID entry in the trailer dictionary
    //      2) Any strings in Encrypt dictionary
    //      3) String/streams in object streams (entire object streams are encrypted)
    //      4) Hexadecimal strings in Content key in signature dictionary
    //
    // Trailer dictionary is not decrypted, because PDF specification provides no algorithm to decrypt it,
    // because it needs object number and generation for generating the decrypt key. So 1) is handled
    // automatically. 2) is handled in the code below. 3) is handled also automatically, because we do not
    // decipher object streams here. 4) must be handled in the security handler.
    if (m_securityHandler->getMode() != EncryptionMode::None)
    {
        auto decryptEntry = [this, encryptObjectReference, &objects](const PDFXRefTable::Entry& entry)
        {
            progressStep();

            if (encryptObjectReference.objectNumber != 0 && encryptObjectReference == entry.reference)
            {
                // 2) - Encrypt dictionary
                return;
            }

            objects[entry.reference.objectNumber].object = m_securityHandler->decryptObject(objects[entry.reference.objectNumber].object, entry.reference);
        };

        progressStart(occupiedEntries.size(), PDFTranslationContext::tr("Decrypting encrypted contents of document..."));
        PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Unknown, occupiedEntries.cbegin(), occupiedEntries.cend(), decryptEntry);
        progressFinish();
    }

    return m_result;
}

void PDFDocumentReader::processObjectStreams(PDFXRefTable* xrefTable, PDFObjectStorage::PDFObjects& objects)
{
    // Then process object streams
    std::vector<PDFXRefTable::Entry> objectStreamEntries = xrefTable->getObjectStreamEntries();
    std::set<PDFObjectReference> objectStreams;
    for (const PDFXRefTable::Entry& entry : objectStreamEntries)
    {
        Q_ASSERT(entry.type == PDFXRefTable::EntryType::InObjectStream);
        objectStreams.insert(entry.objectStream);
    }

    // Pairs (object number, object stream) of the objects stored in object
    // streams, sorted, so we can quickly check, if object in the object
    // stream is referenced by the reference table.
    std::vector<std::pair<PDFInteger, PDFObjectReference>> objectsInObjectStreams;
    objectsInObjectStreams.reserve(objectStreamEntries.size());
    for (const PDFXRefTable::Entry& entry : objectStreamEntries)
    {
        objectsInObjectStreams.emplace_back(entry.reference.objectNumber, entry.objectStream);
    }
    std::sort(objectsInObjectStreams.begin(), objectsInObjectStreams.end());

    auto objectFetcher = [this, xrefTable](PDFParsingContext* context, PDFObjectReference reference) { return getObjectFromXrefTable(xrefTable, context, reference); };
    auto processObjectStream = [this, &objectFetcher, &objects, &objectsInObjectStreams] (const PDFObjectReference& objectStreamReference)
    {
        if (m_result != Result::OK)
        {
            return;
        }

        try
        {
            PDFParsingContext context(objectFetcher);
            if (objectStreamReference.objectNumber < 0 || objectStreamReference.objectNumber >= static_cast<PDFInteger>(objects.size()))
            {
                throw PDFException(PDFTranslationContext::tr("Object stream %1 not found.").arg(objectStreamReference.objectNumber));
            }

            // Invalid files can reference an object that another worker is
            // replacing. Copy under the same lock used for publishing objects,
            // then keep the stream alive for the entire parsing operation.
            PDFObject object;
            {
                QMutexLocker lock(&m_mutex);
                object = objects[objectStreamReference.objectNumber].object;
            }
            if (!object.isStream())
            {
                throw PDFException(PDFTranslationContext::tr("Object stream %1 is invalid.").arg(objectStreamReference.objectNumber));
            }

            const PDFStream* objectStream = object.getStream();
            const PDFDictionary* objectStreamDictionary = objectStream->getDictionary();

            const PDFObject& objectStreamType = objectStreamDictionary->get("Type");
            if (!objectStreamType.isName() || objectStreamType.getString() != "ObjStm")
            {
                throw PDFException(PDFTranslationContext::tr("Object stream %1 is invalid.").arg(objectStreamReference.objectNumber));
            }

            const PDFObject& nObject = objectStreamDictionary->get("N");
            const PDFObject& firstObject = objectStreamDictionary->get("First");
            if (!nObject.isInt() || !firstObject.isInt())
            {
                throw PDFException(PDFTranslationContext::tr("Object stream %1 is invalid.").arg(objectStreamReference.objectNumber));
            }

            // Number of objects in object stream dictionary
            const PDFInteger n = nObject.getInteger();
            const PDFInteger first = firstObject.getInteger();

            QByteArray objectStreamData = PDFStreamFilterStorage::getDecodedStream(objectStream, m_securityHandler.data());
            // Every header pair needs at least three bytes (e.g. "1 0").
            // Check before reserve() and before adding offsets from the file.
            if (first < 0 || first > objectStreamData.size() || n < 0 || n > first / 3)
            {
                throw PDFException(PDFTranslationContext::tr("Object stream %1 is invalid.").arg(objectStreamReference.objectNumber));
            }

            PDFParsingContext::PDFParsingContextGuard guard(&context, objectStreamReference);
            PDFParser parser(objectStreamData, &context, PDFParser::AllowStreams);

            std::vector<std::pair<PDFInteger, PDFInteger>> objectNumberAndOffset;
            objectNumberAndOffset.reserve(n);
            for (PDFInteger i = 0; i < n; ++i)
            {
                PDFObject currentObjectNumber = parser.getObject();
                PDFObject currentOffset = parser.getObject();

                if (!currentObjectNumber.isInt() || !currentOffset.isInt())
                {
                    throw PDFException(PDFTranslationContext::tr("Object stream %1 is invalid.").arg(objectStreamReference.objectNumber));
                }

                const PDFInteger objectNumber = currentObjectNumber.getInteger();
                const PDFInteger relativeOffset = currentOffset.getInteger();
                if (relativeOffset < 0 || relativeOffset >= objectStreamData.size() - first)
                {
                    throw PDFException(PDFTranslationContext::tr("Object stream %1 is invalid.").arg(objectStreamReference.objectNumber));
                }
                const PDFInteger offset = relativeOffset + first;
                objectNumberAndOffset.emplace_back(objectNumber, offset);
            }

            for (size_t i = 0; i < objectNumberAndOffset.size(); ++i)
            {
                const PDFInteger objectNumber = objectNumberAndOffset[i].first;
                const PDFInteger offset = objectNumberAndOffset[i].second;
                parser.seek(offset);

                PDFObject currentObject = parser.getObject();
                if (std::binary_search(objectsInObjectStreams.cbegin(), objectsInObjectStreams.cend(), std::make_pair(objectNumber, objectStreamReference)))
                {
                    QMutexLocker lock(&m_mutex);
                    if (objectNumber <= 0 || objectNumber >= static_cast<PDFInteger>(objects.size()))
                    {
                        throw PDFException(PDFTranslationContext::tr("Object stream %1 is invalid.").arg(objectStreamReference.objectNumber));
                    }
                    objects[objectNumber].object = qMove(currentObject);
                }
                else
                {
                    // Silently ignore this error. It is not critical, so, maybe this object will be null.
                }
            }
        }
        catch (const PDFException& exception)
        {
            QMutexLocker lock(&m_mutex);
            m_result = Result::Failed;
            m_errorMessage = exception.getMessage();
        }
    };

    // Now, we are ready to scan all object streams
    PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Unknown, objectStreams.cbegin(), objectStreams.cend(), processObjectStream);
}

PDFDocument PDFDocumentReader::readFromBuffer(const QByteArray& buffer)
{
    bool shouldTryPermissiveReading = true;

    try
    {
        m_source = buffer;

        // FOOTER CHECKING
        //  1) Check, if EOF marking is present
        //  2) Find start of cross reference table
        checkFooter(buffer);
        const PDFInteger firstXrefTableOffset = findXrefTableOffset(buffer);

        // HEADER CHECKING
        //  1) Check if header is present
        //  2) Scan header version
        checkHeader(buffer);

        // Now, we are ready to scan xref table
        PDFXRefTable xrefTable;
        xrefTable.readXRefTable(nullptr, buffer, firstXrefTableOffset);

        if (xrefTable.getSize() == 0)
        {
            throw PDFException(tr("Empty xref table."));
        }

        PDFObjectStorage::PDFObjects objects;
        objects.resize(xrefTable.getSize());

        std::vector<PDFXRefTable::Entry> occupiedEntries = xrefTable.getOccupiedEntries();

        // First, process regular objects
        if (processReferenceTableEntries(&xrefTable, occupiedEntries, objects) != Result::OK)
        {
            // Do not proceed further, if document loading failed
            return PDFDocument();
        }

        if (processSecurityHandler(xrefTable.getTrailerDictionary(), occupiedEntries, objects) == Result::Cancelled)
        {
            return PDFDocument();
        }

        // We are past security inicialization. Do not attempt to restore damaged document from
        // this point. After this point, security is decrypted. If something fails here,
        // then document can't be restored (user can't be asked multiple times for password).
        shouldTryPermissiveReading = !m_securityHandler || m_securityHandler->getMode() == EncryptionMode::None;
        processObjectStreams(&xrefTable, objects);

        PDFObjectStorage storage(std::move(objects), PDFObject(xrefTable.getTrailerDictionary()), qMove(m_securityHandler));
        return PDFDocument(std::move(storage), m_version, hash(buffer));
    }
    catch (const PDFException &parserException)
    {
        m_result = Result::Failed;
        m_errorMessage = parserException.getMessage();
        m_warnings << m_errorMessage;
    }

    // The failure of a handler of an encrypted document, which was already created (a wrong /Perms, a wrong key), is not
    // a damage of the file structure. Reading the objects from the bytes would ask for the password the second time,
    // a caller with one password would call the right password wrong.
    const bool isEncryptionFailure = m_securityHandler && m_securityHandler->getMode() != EncryptionMode::None;
    if (m_result == Result::Failed && m_permissive && shouldTryPermissiveReading && !isEncryptionFailure)
    {
        return readDamagedDocumentFromBuffer(buffer);
    }

    return PDFDocument();
}

QByteArray PDFDocumentReader::hash(const QByteArray& sourceData)
{
    return QCryptographicHash::hash(sourceData, QCryptographicHash::Sha256);
}

/// Finds the objects of a damaged file in its bytes, in one pass from the start of the file to its end. It reads what
/// is inside of an object, so the bytes "12 0 obj" in the data of a stream or in a string are not another object, and
/// an object without its endobj ends at the next object. The time is linear also for a hostile file: what was searched
/// for is remembered until the pass gets behind it.
class PDFDamagedFileScanner
{
public:
    explicit PDFDamagedFileScanner(const QByteArray& buffer);

    /// Returns the start and end offsets of the objects in the order of the file
    std::vector<std::pair<int, int>> findObjects();

private:
    /// Bytes "N G obj" of the file: the start of N and the position behind obj
    struct Header
    {
        int start = 0;
        int body = 0;
    };

    /// The last integer objects of one reference tried as /Length of a stream
    static constexpr size_t MAXIMUM_LENGTH_CANDIDATES = 8;

    /// A larger number than any size of a file, the digits behind it are ignored
    static constexpr PDFInteger MAXIMUM_INTEGER = 1000000000000000;

    bool isAt(int position, const char* text) const;
    bool isKeywordAt(int position, const char* text) const;
    int skipWhitespace(int position) const;
    int readInteger(int position, PDFInteger* value) const;
    int findMark(const char* mark, int position, int& remembered) const;

    void findHeaders();
    int findObjectEnd(int start, int position);
    int findLiteralStringEnd(int objectStart, int position);
    int walkLiteralString(int position, int limit) const;
    bool isObjectReadable(int start);
    int findStreamEnd(int position, PDFInteger length, PDFObjectReference lengthReference);
    int checkStreamLength(int position, PDFInteger length) const;

    const QByteArray& m_buffer;
    const char* m_data;
    int m_size;

    std::vector<Header> m_headers;
    size_t m_nextHeader = 0;
    std::map<PDFObjectReference, std::vector<PDFInteger>> m_integerObjects;

    int m_nextEndObject = -1;
    int m_nextEndStream = -1;

    /// A string can contain endobj, until the first string, which would swallow a part of the file
    bool m_stringsContainEndObject = true;

    /// The object is proved to be readable with all its strings
    int m_readableObject = -1;

    /// A string starting in front of this position does not close in front of the next endobj
    int m_unbalancedStringLimit = 0;
};

PDFDamagedFileScanner::PDFDamagedFileScanner(const QByteArray& buffer) :
    m_buffer(buffer),
    m_data(buffer.constData()),
    m_size(static_cast<int>(buffer.size()))
{

}

std::vector<std::pair<int, int>> PDFDamagedFileScanner::findObjects()
{
    findHeaders();

    std::vector<std::pair<int, int>> offsets;
    int position = 0;
    for (size_t i = 0; i < m_headers.size(); ++i)
    {
        // A header behind this position lies inside of the previous object
        const Header header = m_headers[i];
        if (header.start >= position)
        {
            m_nextHeader = i + 1;
            position = findObjectEnd(header.start, header.body);
            offsets.emplace_back(header.start, position);
        }
    }
    return offsets;
}

bool PDFDamagedFileScanner::isAt(int position, const char* text) const
{
    const qsizetype length = qstrlen(text);
    return position + length <= m_size && std::memcmp(m_data + position, text, length) == 0;
}

bool PDFDamagedFileScanner::isKeywordAt(int position, const char* text) const
{
    const int end = position + static_cast<int>(qstrlen(text));
    return isAt(position, text) && (end == m_size || !PDFLexicalAnalyzer::isRegular(m_data[end]));
}

int PDFDamagedFileScanner::skipWhitespace(int position) const
{
    while (position < m_size && PDFLexicalAnalyzer::isWhitespace(m_data[position]))
    {
        ++position;
    }
    return position;
}

int PDFDamagedFileScanner::readInteger(int position, PDFInteger* value) const
{
    *value = 0;
    while (position < m_size && m_data[position] >= '0' && m_data[position] <= '9')
    {
        *value = qMin(*value * 10 + (m_data[position] - '0'), MAXIMUM_INTEGER);
        ++position;
    }
    return position;
}

int PDFDamagedFileScanner::findMark(const char* mark, int position, int& remembered) const
{
    if (remembered < position)
    {
        const qsizetype found = m_buffer.indexOf(mark, position);
        remembered = found != -1 ? static_cast<int>(found) : m_size;
    }
    return remembered;
}

void PDFDamagedFileScanner::findHeaders()
{
    const int markLength = static_cast<int>(qstrlen(PDF_OBJECT_START_MARK));
    const auto isDigit = [this](int position) { return position >= 0 && m_data[position] >= '0' && m_data[position] <= '9'; };
    const auto isSpace = [this](int position) { return position >= 0 && PDFLexicalAnalyzer::isWhitespace(m_data[position]); };

    for (qsizetype mark = m_buffer.indexOf(PDF_OBJECT_START_MARK); mark != -1; mark = m_buffer.indexOf(PDF_OBJECT_START_MARK, mark + markLength))
    {
        const int body = static_cast<int>(mark) + markLength;

        // From the end: white space, the generation, white space, the number
        int position = static_cast<int>(mark) - 1;
        while (isSpace(position))
        {
            --position;
        }
        const int generationEnd = position;
        while (isDigit(position))
        {
            --position;
        }
        const int generationStart = position + 1;
        while (isSpace(position))
        {
            --position;
        }
        const int numberEnd = position;
        while (isDigit(position))
        {
            --position;
        }
        const int start = position + 1;

        if (generationStart > generationEnd || numberEnd == generationStart - 1 || start > numberEnd)
        {
            continue;
        }

        m_headers.push_back(Header{ start, body });

        // A stream can take its /Length from any revision of an integer object, also behind it
        PDFInteger value = 0;
        const int valueStart = skipWhitespace(body);
        if (readInteger(valueStart, &value) > valueStart && value <= m_size)
        {
            PDFInteger number = 0;
            PDFInteger generation = 0;
            readInteger(start, &number);
            readInteger(generationStart, &generation);

            std::vector<PDFInteger>& values = m_integerObjects[PDFObjectReference(number, generation)];
            if (values.size() == MAXIMUM_LENGTH_CANDIDATES)
            {
                values.erase(values.begin());
            }
            values.push_back(value);
        }
    }
}

int PDFDamagedFileScanner::findObjectEnd(int start, int position)
{
    PDFInteger length = -1;
    PDFObjectReference lengthReference(-1, -1);
    char previous = 0;

    while (position < m_size)
    {
        while (m_nextHeader < m_headers.size() && m_headers[m_nextHeader].start < position)
        {
            ++m_nextHeader;
        }
        if (m_nextHeader < m_headers.size() && m_headers[m_nextHeader].start == position)
        {
            return position;
        }

        const char character = m_data[position];
        if (character == '(')
        {
            // A string, which does not close, is a damaged object, not a reason to lose the file behind it
            const int stringEnd = findLiteralStringEnd(start, position);
            position = stringEnd != -1 ? stringEnd : position + 1;
            previous = ')';
        }
        else if (character == '%')
        {
            while (position < m_size && m_data[position] != '\r' && m_data[position] != '\n')
            {
                ++position;
            }
        }
        else if (isAt(position, PDF_OBJECT_END_MARK))
        {
            return position + static_cast<int>(qstrlen(PDF_OBJECT_END_MARK));
        }
        else if (previous == '>' && isKeywordAt(position, PDF_STREAM_START_COMMAND))
        {
            position = findStreamEnd(position + static_cast<int>(qstrlen(PDF_STREAM_START_COMMAND)), length, lengthReference);
            previous = 0;
        }
        else if (PDFLexicalAnalyzer::isWhitespace(m_data[position - 1]) && (isKeywordAt(position, "xref") || isKeywordAt(position, PDF_XREF_TRAILER)))
        {
            // An object without its endobj at the end of a revision, this is no part of it
            return position;
        }
        else if (isKeywordAt(position, "/Length"))
        {
            position += 7;

            PDFInteger value = 0;
            const int valueStart = skipWhitespace(position);
            const int valueEnd = readInteger(valueStart, &value);
            if (valueEnd > valueStart)
            {
                PDFInteger generation = 0;
                const int generationStart = skipWhitespace(valueEnd);
                const int generationEnd = readInteger(generationStart, &generation);
                const int command = skipWhitespace(generationEnd);
                const bool isReference = generationEnd > generationStart && isKeywordAt(command, PDF_REFERENCE_COMMAND);
                length = isReference ? -1 : value;
                lengthReference = isReference ? PDFObjectReference(value, generation) : PDFObjectReference(-1, -1);
            }
        }
        else
        {
            previous = PDFLexicalAnalyzer::isWhitespace(character) ? previous : character;
            ++position;
        }
    }

    return m_size;
}

int PDFDamagedFileScanner::walkLiteralString(int position, int limit) const
{
    int depth = 0;
    for (int i = position; i < limit; ++i)
    {
        switch (m_data[i])
        {
            case '\\':
                ++i;
                break;

            case '(':
                ++depth;
                break;

            case ')':
                if (--depth == 0)
                {
                    return i + 1;
                }
                break;

            default:
                break;
        }
    }
    return -1;
}

int PDFDamagedFileScanner::findLiteralStringEnd(int objectStart, int position)
{
    // A string, which would contain endobj, is either a text with these bytes, or a parenthesis without its
    // pair, which is closed by chance somewhere in the following objects - the object must be readable by the parser.
    if (m_stringsContainEndObject)
    {
        const int end = walkLiteralString(position, m_size);
        if (end != -1 && (findMark(PDF_OBJECT_END_MARK, position, m_nextEndObject) >= end || isObjectReadable(objectStart)))
        {
            return end;
        }
        m_stringsContainEndObject = false;
    }

    if (position < m_unbalancedStringLimit)
    {
        return -1;
    }

    const int limit = findMark(PDF_OBJECT_END_MARK, position, m_nextEndObject);
    const int end = walkLiteralString(position, limit);
    if (end == -1)
    {
        m_unbalancedStringLimit = limit;
    }
    return end;
}

bool PDFDamagedFileScanner::isObjectReadable(int start)
{
    if (m_readableObject == start)
    {
        return true;
    }

    try
    {
        PDFParsingContext context([](PDFParsingContext*, PDFObjectReference) { return PDFObject(); });
        PDFParser parser(m_data + start, m_data + m_size, &context, PDFParser::AllowStreams);
        parser.getObject();
        parser.getObject();
        parser.fetchCommand(PDF_OBJECT_START_MARK);
        parser.getObject();
        m_readableObject = start;
        return true;
    }
    catch (const PDFException&)
    {
        return false;
    }
}

int PDFDamagedFileScanner::checkStreamLength(int position, PDFInteger length) const
{
    if (length >= 0 && length <= m_size - position)
    {
        const int end = skipWhitespace(position + static_cast<int>(length));
        if (isAt(end, PDF_STREAM_END_COMMAND))
        {
            return end + static_cast<int>(qstrlen(PDF_STREAM_END_COMMAND));
        }
    }
    return -1;
}

int PDFDamagedFileScanner::findStreamEnd(int position, PDFInteger length, PDFObjectReference lengthReference)
{
    // PDFLexicalAnalyzer::skipStreamStart
    position += (position < m_size && m_data[position] == '\r') ? 1 : 0;
    position += (position < m_size && m_data[position] == '\n') ? 1 : 0;

    int end = checkStreamLength(position, length);
    if (end != -1)
    {
        return end;
    }

    auto it = m_integerObjects.find(lengthReference);
    if (it != m_integerObjects.cend())
    {
        for (auto value = it->second.crbegin(); value != it->second.crend(); ++value)
        {
            end = checkStreamLength(position, *value);
            if (end != -1)
            {
                return end;
            }
        }
    }

    // No length fits, the parser will refuse this stream. The bytes inside of it are still no objects, so
    // it ends at its endstream, the first endobj in front of it means there is none.
    const int endObject = findMark(PDF_OBJECT_END_MARK, position, m_nextEndObject);
    const int endStream = findMark(PDF_STREAM_END_COMMAND, position, m_nextEndStream);
    return endStream < endObject ? endStream + static_cast<int>(qstrlen(PDF_STREAM_END_COMMAND)) : endObject;
}

std::vector<std::pair<int, int>> PDFDocumentReader::findObjectByteOffsets(const QByteArray& buffer) const
{
    PDFDamagedFileScanner scanner(buffer);
    return scanner.findObjects();
}

PDFObjectStorage::PDFObjects PDFDocumentReader::createRestoredObjects(const std::map<PDFObjectReference, PDFObject>& restoredObjects)
{
    PDFObjectStorage::PDFObjects objects;
    if (!restoredObjects.empty())
    {
        objects.resize(restoredObjects.rbegin()->first.objectNumber + 1);

        // One number has one object: of more generations the highest one, a generation only grows
        // over the revisions (ISO 32000-2, 7.5.4), references of the other generations are null.
        for (const auto& objectItem : restoredObjects)
        {
            PDFObjectStorage::Entry& entry = objects[objectItem.first.objectNumber];
            entry.generation = objectItem.first.generation;
            entry.object = objectItem.second;
        }
    }
    return objects;
}

bool PDFDocumentReader::isRestoredDocumentReadable(const std::map<PDFObjectReference, PDFObject>& restoredObjects, const PDFObject& trailerDictionaryObject) const
{
    try
    {
        // The catalog and the page tree do not depend on the decryption of strings and streams
        PDFDocument document(PDFObjectStorage(createRestoredObjects(restoredObjects), PDFObject(trailerDictionaryObject), PDFSecurityHandlerPointer(new PDFNoneSecurityHandler())), m_version, QByteArray());
        return true;
    }
    catch (const PDFException&)
    {
        return false;
    }
}

void PDFDocumentReader::restoreObjects(std::map<PDFObjectReference, PDFObject>& restoredObjects, const std::vector<std::pair<int, int>>& offsets, RestoredRevision revision)
{
    // Each object of the file is read for itself into its own item, in the threads nothing else is written. The first
    // pass needs nothing else, it gives also all integers. The second one reads the streams with /Length as a reference
    // from the result of the first one, so it does not matter, in which order the threads have come.
    struct RestoredObject
    {
        int startOffset = 0;
        int endOffset = 0;
        bool isRead = false;
        PDFObjectReference reference;
        PDFObject object;
    };

    std::vector<RestoredObject> items(offsets.size());
    for (size_t i = 0; i < offsets.size(); ++i)
    {
        items[i].startOffset = offsets[i].first;
        items[i].endOffset = offsets[i].second;
    }

    auto getObject = [&restoredObjects](PDFParsingContext*, PDFObjectReference reference)
    {
        auto it = restoredObjects.find(reference);
        return it != restoredObjects.cend() ? it->second : PDFObject();
    };

    auto processItem = [&, this](RestoredObject& item)
    {
        if (item.isRead)
        {
            return;
        }

        Q_ASSERT(item.startOffset >= 0 && item.startOffset < m_source.size());
        Q_ASSERT(item.endOffset >= 0 && item.endOffset <= m_source.size());
        Q_ASSERT(item.startOffset <= item.endOffset);

        try
        {
            PDFParsingContext context(getObject);
            PDFParser parser(m_source.constData() + item.startOffset, m_source.constData() + item.endOffset, &context, PDFParser::AllowStreams);
            PDFObject objectNumberObject = parser.getObject();
            PDFObject objectGenerationObject = parser.getObject();
            parser.fetchCommand(PDF_OBJECT_START_MARK);
            PDFObject object = parser.getObject();

            if (objectNumberObject.isInt() && objectGenerationObject.isInt())
            {
                item.reference = PDFObjectReference(objectNumberObject.getInteger(), objectGenerationObject.getInteger());
                item.object = qMove(object);
                item.isRead = item.reference.isValid();
            }
        }
        catch (const PDFException&)
        {
            // Damaged object, an older revision is used, if there is any
        }
    };

    // ISO 32000-2, 7.5.6: a later revision supersedes the older one, also by the value null. What can't
    // be read at all does not supersede anything: the end of a damaged file does not lose an object.
    auto collectItem = [&restoredObjects](const RestoredObject& item)
    {
        if (item.isRead && item.object.isNull())
        {
            restoredObjects.erase(item.reference);
        }
        else if (item.isRead)
        {
            restoredObjects[item.reference] = item.object;
        }
    };

    auto collectItems = [&]()
    {
        restoredObjects.clear();
        if (revision == RestoredRevision::Last)
        {
            std::for_each(items.cbegin(), items.cend(), collectItem);
        }
        else
        {
            std::for_each(items.crbegin(), items.crend(), collectItem);
        }
    };

    PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Unknown, items.begin(), items.end(), processItem);
    collectItems();
    PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Unknown, items.begin(), items.end(), processItem);
    collectItems();
}

PDFDocument PDFDocumentReader::readDamagedDocumentFromBuffer(const QByteArray& buffer)
{
    try
    {
        m_result = Result::OK;

        // Try to reconstruct trailer dictionary
        std::map<PDFObjectReference, PDFObject> restoredObjects;

        PDFObject trailerDictionaryObject = readDamagedTrailerDictionary();
        if (!trailerDictionaryObject.isDictionary())
        {
            throw PDFException(PDFTranslationContext::tr("Trailer dictionary is not valid."));
        }

        // Jakub Melka: Try to parse objects - read offsets of objects.
        const std::vector<std::pair<int, int>> offsets = findObjectByteOffsets(buffer);
        restoreObjects(restoredObjects, offsets, RestoredRevision::Last);

        // The last revision has no document, which can be read (Signature/invalid/pdf-wrapping-6.pdf: a page twice in the
        // page tree, also the strict reading refuses it), the file was opened before: its first objects still are a document.
        if (!isRestoredDocumentReadable(restoredObjects, trailerDictionaryObject))
        {
            m_warnings << tr("The last changes of the damaged file can't be read, its oldest content is shown.");
            restoreObjects(restoredObjects, offsets, RestoredRevision::First);
        }

        // We will create security handler.
        PDFObjectStorage::PDFObjects objects = createRestoredObjects(restoredObjects);
        std::vector<PDFXRefTable::Entry> occupiedEntries;

        if (!restoredObjects.empty())
        {
            // The restored objects are read from the bytes of the file, so they are encrypted as the objects of
            // the table of a valid file are: left undecrypted, a page could not be read and a written copy would
            // be encrypted the second time
            occupiedEntries.reserve(restoredObjects.size());
            for (size_t objectNumber = 0; objectNumber < objects.size(); ++objectNumber)
            {
                if (!objects[objectNumber].object.isNull())
                {
                    PDFXRefTable::Entry entry;
                    entry.reference = PDFObjectReference(PDFInteger(objectNumber), objects[objectNumber].generation);
                    entry.type = PDFXRefTable::EntryType::Occupied;
                    occupiedEntries.push_back(entry);
                }
            }
        }

        // A failure of the structure comes before the header is read
        if (!m_version.isValid())
        {
            m_version = findDamagedFileVersion(buffer);
        }

        if (processSecurityHandler(trailerDictionaryObject, occupiedEntries, objects) == Result::Cancelled)
        {
            return PDFDocument();
        }

        PDFObjectStorage storage(std::move(objects), PDFObject(trailerDictionaryObject), qMove(m_securityHandler));
        return PDFDocument(std::move(storage), m_version, QByteArray());
    }
    catch (const PDFException &parserException)
    {
        m_result = Result::Failed;
        m_warnings << parserException.getMessage();
    }

    return PDFDocument();
}

PDFVersion PDFDocumentReader::findDamagedFileVersion(const QByteArray& buffer)
{
    // Both forms of the header of PDF Reference 1.7, Appendix H end by "PDF-x.y"; a file
    // can have any bytes before it, so the regular expression of checkHeader with its
    // recursion over the bytes is not used here
    static constexpr QByteArrayView HEADER_MARK("PDF-");

    qsizetype position = buffer.indexOf(HEADER_MARK);
    while (position != -1)
    {
        const qsizetype versionPosition = position + HEADER_MARK.size();
        if (versionPosition + 3 <= buffer.size() &&
            std::isdigit(static_cast<unsigned char>(buffer[versionPosition])) &&
            buffer[versionPosition + 1] == '.' &&
            std::isdigit(static_cast<unsigned char>(buffer[versionPosition + 2])))
        {
            const PDFVersion version(buffer[versionPosition] - '0', buffer[versionPosition + 2] - '0');
            if (version.isValid())
            {
                return version;
            }
        }

        position = buffer.indexOf(HEADER_MARK, position + 1);
    }

    return PDFVersion::getFallback();
}

void PDFDocumentReader::reset()
{
    m_result = Result::OK;
    m_errorMessage = QString();
    m_version = PDFVersion();
    m_source = QByteArray();
    m_securityHandler = nullptr;
}

int PDFDocumentReader::findFromEnd(const char* what, const QByteArray& byteArray, int limit)
{
    if (byteArray.isEmpty())
    {
        // Byte array is empty, no value found
        return FIND_NOT_FOUND_RESULT;
    }

    const int size = byteArray.size();
    const int adjustedLimit = qMin(byteArray.size(), limit);
    const int whatLength = static_cast<int>(std::strlen(what));

    if (adjustedLimit < whatLength)
    {
        // Buffer is smaller than scan string
        return FIND_NOT_FOUND_RESULT;
    }

    auto itBegin = std::next(byteArray.cbegin(), size - adjustedLimit);
    auto itEnd = byteArray.cend();
    auto it = std::find_end(itBegin, itEnd, what, std::next(what, whatLength));

    if (it != byteArray.cend())
    {
        return std::distance(byteArray.cbegin(), it);
    }

    return FIND_NOT_FOUND_RESULT;
}

void PDFDocumentReader::progressStart(size_t stepCount, QString text)
{
    if (m_progress)
    {
        ProgressStartupInfo info;
        info.showDialog = !text.isEmpty();
        info.text = qMove(text);

        m_progress->start(stepCount, qMove(info));
    }
}

void PDFDocumentReader::progressStep()
{
    if (m_progress)
    {
        m_progress->step();
    }
}

void PDFDocumentReader::progressFinish()
{
    if (m_progress)
    {
        m_progress->finish();
    }
}

}   // namespace pdf
