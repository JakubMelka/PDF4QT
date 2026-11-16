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

#include "pdfsecurityhandler.h"
#include "pdfexception.h"
#include "pdfencoding.h"
#include "pdfvisitor.h"
#include "pdfutils.h"
#include "pdfdocumentbuilder.h"
#include "pdfcertificatemanager.h"

#include <QtMath>
#include <QtEndian>

#include "pdfdbgheap.h"

#if defined(PDF4QT_COMPILER_MINGW) || defined(PDF4QT_COMPILER_GCC)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

#if defined(PDF4QT_COMPILER_MSVC)
#pragma warning(push)
#pragma warning(disable: 4996)
#endif

#include <openssl/rc4.h>
#include <openssl/md5.h>
#include <openssl/aes.h>
#include <openssl/sha.h>
#include <openssl/pkcs7.h>
#include <openssl/pkcs12.h>
#include <openssl/evp.h>
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <openssl/x509.h>

#include <array>

namespace pdf
{

static bool s_noDrmMode = false;

template<typename T>
using openssl_ptr = std::unique_ptr<T, void(*)(T*)>;

void sk_x509_free_impl(STACK_OF(X509)* ptr)
{
    sk_X509_free(ptr);
}

// Padding password
static constexpr std::array<uint8_t, 32> PDFPasswordPadding = {
    0x28, 0xBF, 0x4E, 0x5E, 0x4E, 0x75, 0x8A, 0x41,
    0x64, 0x00, 0x4E, 0x56, 0xFF, 0xFA, 0x01, 0x08,
    0x2E, 0x2E, 0x00, 0xB6, 0xD0, 0x68, 0x3E, 0x80,
    0x2F, 0x0C, 0xA9, 0xFE, 0x64, 0x53, 0x69, 0x7A
};

class PDFDecryptOrEncryptObjectVisitor : public PDFAbstractVisitor
{
public:

    enum class Mode
    {
        Decrypt,
        Encrypt
    };

    explicit PDFDecryptOrEncryptObjectVisitor(const PDFSecurityHandler* securityHandler, PDFObjectReference reference, Mode mode) :
        m_securityHandler(securityHandler),
        m_reference(reference),
        m_mode(mode)
    {
        m_objectStack.reserve(32);
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

    PDFObject getProcessedObject();

private:
    /// The name of the crypt filter of the first decode parameters of a stream (7.4.10: /Name,
    /// default Identity); \p isKnown is false for parameters, which are not direct
    static QByteArray getCryptFilterName(const PDFDictionary* streamDictionary, bool* isKnown);

    /// Removes the first filter (/Crypt) and its decode parameters from the stream dictionary
    static void removeCryptFilterStep(PDFDictionaryBuilder& dictionary);

    /// Stores the reference of the object into the decode parameters of the /Crypt step, where
    /// PDFCryptFilter reads it (B106); \p remove removes it again
    static void setCryptFilterReference(PDFDictionaryBuilder& dictionary, PDFObjectReference reference, bool remove);

    const PDFSecurityHandler* m_securityHandler = nullptr;
    std::vector<PDFObject> m_objectStack;
    PDFObjectReference m_reference;
    Mode m_mode = Mode::Decrypt;
};

void PDFDecryptOrEncryptObjectVisitor::visitNull()
{
    m_objectStack.push_back(PDFObject::createNull());
}

void PDFDecryptOrEncryptObjectVisitor::visitBool(bool value)
{
    m_objectStack.push_back(PDFObject::createBool(value));
}

void PDFDecryptOrEncryptObjectVisitor::visitInt(PDFInteger value)
{
    m_objectStack.push_back(PDFObject::createInteger(value));
}

void PDFDecryptOrEncryptObjectVisitor::visitReal(PDFReal value)
{
    m_objectStack.push_back(PDFObject::createReal(value));
}

void PDFDecryptOrEncryptObjectVisitor::visitString(PDFStringRef string)
{
    switch (m_mode)
    {
        case pdf::PDFDecryptOrEncryptObjectVisitor::Mode::Decrypt:
            m_objectStack.push_back(PDFObject::createString(m_securityHandler->decrypt(string.getString(), m_reference, PDFSecurityHandler::EncryptionScope::String)));
            break;
        case pdf::PDFDecryptOrEncryptObjectVisitor::Mode::Encrypt:
            m_objectStack.push_back(PDFObject::createString(m_securityHandler->encrypt(string.getString(), m_reference, PDFSecurityHandler::EncryptionScope::String)));
            break;

        default:
            Q_ASSERT(false);
            break;
    }
}

void PDFDecryptOrEncryptObjectVisitor::visitName(PDFStringRef name)
{
    m_objectStack.push_back(PDFObject::createName(name));
}

void PDFDecryptOrEncryptObjectVisitor::visitArray(const PDFArray* array)
{
    acceptArray(array);

    // We have all objects on the stack
    Q_ASSERT(array->getCount() <= m_objectStack.size());

    auto it = std::next(m_objectStack.cbegin(), m_objectStack.size() - array->getCount());
    std::vector<PDFObject> objects(it, m_objectStack.cend());
    PDFObject object = PDFObject::createArray(qMove(objects));
    m_objectStack.erase(it, m_objectStack.cend());
    m_objectStack.push_back(object);
}

void PDFDecryptOrEncryptObjectVisitor::visitDictionary(const PDFDictionary* dictionary)
{
    Q_ASSERT(dictionary);

    // We must check, if it is or isn't a signature dictionary. If it is,
    // then don't decrypt/encrypt the Content value (7.6.2). Document timestamps
    // are signature dictionaries, too - they only use a different type, and
    // the type is optional (Table 255, B88).
    const bool isSignatureObject = PDFSecurityHandler::isSignatureDictionary(dictionary);

    std::vector<PDFDictionary::DictionaryEntry> entries;
    entries.reserve(dictionary->getCount());

    for (size_t i = 0, count = dictionary->getCount(); i < count; ++i)
    {
        if (isSignatureObject && dictionary->getKey(i) == "Contents")
        {
            entries.emplace_back(dictionary->getKey(i), dictionary->getValue(i));
        }
        else
        {
            dictionary->getValue(i).accept(this);
            entries.emplace_back(dictionary->getKey(i), m_objectStack.back());
            m_objectStack.pop_back();
        }
    }

    m_objectStack.push_back(PDFObject::createDictionary(qMove(entries)));
}

void PDFDecryptOrEncryptObjectVisitor::visitStream(const PDFStream* stream)
{
    const PDFDictionary* dictionary = stream->getDictionary();

    const PDFObject& typeObject = dictionary->get("Type");
    const bool isCrossReferenceStream = (typeObject.isName() && typeObject.getString() == "XRef");

    // Don't decrypt/encrypt the document-level metadata stream, if the metadata encryption is
    // turned off (Table 20); a cross-reference stream is never encrypted (7.6.2, 7.5.8.2)
    if (m_securityHandler->isUnencryptedMetadataStream(m_reference, dictionary) || isCrossReferenceStream)
    {
        m_objectStack.push_back(PDFObject::createStream(PDFStream(PDFDictionaryBuilder(*dictionary), QByteArray(*stream->getContent()))));
        return;
    }

    // Decrypt/encrypt the dictionary
    visitDictionary(dictionary);
    PDFObject dictionaryObject = m_objectStack.back();
    m_objectStack.pop_back();

    PDFDictionaryBuilder processedDictionary(*dictionaryObject.getDictionary());
    QByteArray processedData = *stream->getContent();
    const bool isDecrypting = m_mode == Mode::Decrypt;

    if (dictionary->hasKey("F"))
    {
        // 7.6.2, 7.3.8.2: the data of a stream in an external file are not a part of the PDF file
        // and are not encrypted; the bytes between stream and endstream are ignored (B108)
    }
    else if (PDFSecurityHandler::isCryptFilterStream(dictionary))
    {
        // The stream is encrypted by its own crypt filter (7.4.10, 7.6.6), not by /StmF (B87).
        // The reader decrypts this step now and removes it - the whole document is decrypted at
        // the opening and every writer and tool sees plain data, the new /Encrypt of a writer
        // never meets a crypt filter name it does not know (R5-10, B106). A stream, which cannot
        // be decrypted now (an unknown name, a locked embedded file), keeps the step and the
        // reference of its object in the decode parameters, where PDFCryptFilter reads it.
        bool isKnown = false;
        const QByteArray filterName = getCryptFilterName(dictionary, &isKnown);
        const bool hasReference = processedDictionary.hasKey(PDFSecurityHandler::OBJECT_REFERENCE_DICTIONARY_NAME);
        processedDictionary.removeEntry(PDFSecurityHandler::OBJECT_REFERENCE_DICTIONARY_NAME);

        if (isDecrypting)
        {
            if (isKnown && m_securityHandler->canDecryptByFilter(filterName))
            {
                processedData = m_securityHandler->decryptByFilter(processedData, filterName, m_reference);
                removeCryptFilterStep(processedDictionary);
            }
            else
            {
                setCryptFilterReference(processedDictionary, m_reference, false);
            }
        }
        else
        {
            // A step kept by the reader (the reference is in its decode parameters) holds the data
            // encrypted by this handler already; a new stream with the step holds plain data
            bool isKeptByReader = hasReference;
            if (const PDFDictionary* parameters = processedDictionary.get("DecodeParms").isDictionary() ? processedDictionary.get("DecodeParms").getDictionary() : nullptr)
            {
                isKeptByReader = isKeptByReader || parameters->hasKey(PDFSecurityHandler::OBJECT_REFERENCE_DICTIONARY_NAME);
            }
            else if (processedDictionary.get("DecodeParms").isArray() && processedDictionary.get("DecodeParms").getArray()->getCount() > 0)
            {
                const PDFObject& firstParameters = processedDictionary.get("DecodeParms").getArray()->getItem(0);
                isKeptByReader = isKeptByReader || (firstParameters.isDictionary() && firstParameters.getDictionary()->hasKey(PDFSecurityHandler::OBJECT_REFERENCE_DICTIONARY_NAME));
            }

            setCryptFilterReference(processedDictionary, m_reference, true);
            if (!isKeptByReader)
            {
                processedData = m_securityHandler->encryptByFilter(processedData, filterName, m_reference);
            }
        }
    }
    else
    {
        // Is it an embedded file?
        const bool isEmbeddedFile = PDFSecurityHandler::isEmbeddedFileStream(dictionary);
        const PDFSecurityHandler::EncryptionScope scope = !isEmbeddedFile ? PDFSecurityHandler::EncryptionScope::Stream : PDFSecurityHandler::EncryptionScope::EmbeddedFile;

        if (isDecrypting)
        {
            // An embedded file of a document opened without its password stays encrypted (Q5.9 c)
            if (!isEmbeddedFile || !m_securityHandler->isEmbeddedFilesLocked())
            {
                processedData = m_securityHandler->decrypt(processedData, m_reference, scope);
            }
        }
        else
        {
            processedData = m_securityHandler->encrypt(processedData, m_reference, scope);
        }
    }

    processedDictionary.setEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(processedData.size()));
    m_objectStack.push_back(PDFObject::createStream(PDFStream(qMove(processedDictionary), qMove(processedData))));
}

QByteArray PDFDecryptOrEncryptObjectVisitor::getCryptFilterName(const PDFDictionary* streamDictionary, bool* isKnown)
{
    *isKnown = true;

    PDFObject parameters = streamDictionary->get("DecodeParms");
    if (parameters.isArray())
    {
        const PDFArray* parametersArray = parameters.getArray();
        parameters = parametersArray->getCount() > 0 ? parametersArray->getItem(0) : PDFObject();
    }

    if (parameters.isDictionary())
    {
        const PDFObject& nameObject = parameters.getDictionary()->get("Name");
        if (nameObject.isName())
        {
            return nameObject.getString();
        }

        // A reference of the name cannot be resolved here
        *isKnown = nameObject.isNull();
    }
    else if (!parameters.isNull())
    {
        // Indirect parameters cannot be resolved here
        *isKnown = false;
    }

    // 7.4.10, Table 14: the default crypt filter is Identity
    return PDFSecurityHandler::IDENTITY_FILTER_NAME;
}

void PDFDecryptOrEncryptObjectVisitor::removeCryptFilterStep(PDFDictionaryBuilder& dictionary)
{
    const PDFObject filters = dictionary.get("Filter");
    const PDFObject parameters = dictionary.get("DecodeParms");

    if (filters.isArray() && filters.getArray()->getCount() > 1)
    {
        const PDFArray* filterArray = filters.getArray();
        std::vector<PDFObject> remainingFilters;
        for (size_t i = 1; i < filterArray->getCount(); ++i)
        {
            remainingFilters.push_back(filterArray->getItem(i));
        }
        dictionary.setEntry(PDFInplaceOrMemoryString("Filter"), PDFObject::createArray(PDFArrayBuilder(qMove(remainingFilters))));

        if (parameters.isArray())
        {
            const PDFArray* parameterArray = parameters.getArray();
            std::vector<PDFObject> remainingParameters;
            for (size_t i = 1; i < parameterArray->getCount(); ++i)
            {
                remainingParameters.push_back(parameterArray->getItem(i));
            }
            dictionary.setEntry(PDFInplaceOrMemoryString("DecodeParms"), PDFObject::createArray(PDFArrayBuilder(qMove(remainingParameters))));
        }
        else
        {
            // Single parameters belong to the first filter (PDFStreamFilterStorage::getStreamFilters)
            dictionary.removeEntry("DecodeParms");
        }
    }
    else
    {
        dictionary.removeEntry("Filter");
        dictionary.removeEntry("DecodeParms");
    }
}

void PDFDecryptOrEncryptObjectVisitor::setCryptFilterReference(PDFDictionaryBuilder& dictionary, PDFObjectReference reference, bool remove)
{
    auto updateParameters = [reference, remove](const PDFObject& parameters) -> PDFObject
    {
        if (!parameters.isDictionary() && !parameters.isNull())
        {
            // Indirect parameters are not changed
            return parameters;
        }

        PDFDictionaryBuilder builder = parameters.isDictionary() ? PDFDictionaryBuilder(*parameters.getDictionary()) : PDFDictionaryBuilder();
        if (remove)
        {
            builder.removeEntry(PDFSecurityHandler::OBJECT_REFERENCE_DICTIONARY_NAME);
            if (parameters.isNull() || builder.getCount() == 0)
            {
                return parameters.isNull() ? PDFObject() : PDFObject::createDictionary(qMove(builder));
            }
        }
        else
        {
            builder.setEntry(PDFInplaceOrMemoryString(PDFSecurityHandler::OBJECT_REFERENCE_DICTIONARY_NAME), PDFObject::createReference(reference));
        }
        return PDFObject::createDictionary(qMove(builder));
    };

    const PDFObject parameters = dictionary.get("DecodeParms");
    if (parameters.isArray())
    {
        const PDFArray* parameterArray = parameters.getArray();
        std::vector<PDFObject> items;
        for (size_t i = 0; i < parameterArray->getCount(); ++i)
        {
            items.push_back(i == 0 ? updateParameters(parameterArray->getItem(i)) : parameterArray->getItem(i));
        }
        dictionary.setEntry(PDFInplaceOrMemoryString("DecodeParms"), PDFObject::createArray(PDFArrayBuilder(qMove(items))));
    }
    else if (!parameters.isNull() || !remove)
    {
        const PDFObject filters = dictionary.get("Filter");
        if (filters.isArray() && filters.getArray()->getCount() > 1 && parameters.isNull())
        {
            // The parameters of the first of more filters (the others have none)
            std::vector<PDFObject> items(filters.getArray()->getCount());
            items[0] = updateParameters(PDFObject());
            dictionary.setEntry(PDFInplaceOrMemoryString("DecodeParms"), PDFObject::createArray(PDFArrayBuilder(qMove(items))));
        }
        else
        {
            const PDFObject updatedParameters = updateParameters(parameters);
            if (updatedParameters.isNull())
            {
                dictionary.removeEntry("DecodeParms");
            }
            else
            {
                dictionary.setEntry(PDFInplaceOrMemoryString("DecodeParms"), updatedParameters);
            }
        }
    }
}

void PDFDecryptOrEncryptObjectVisitor::visitReference(const PDFObjectReference reference)
{
    m_objectStack.push_back(PDFObject::createReference(reference));
}

PDFObject PDFDecryptOrEncryptObjectVisitor::getProcessedObject()
{
    Q_ASSERT(m_objectStack.size() == 1);
    return qMove(m_objectStack.back());
}

PDFObject PDFSecurityHandler::decryptObject(const PDFObject& object, PDFObjectReference reference) const
{
    PDFDecryptOrEncryptObjectVisitor visitor(this, reference, PDFDecryptOrEncryptObjectVisitor::Mode::Decrypt);
    object.accept(&visitor);
    return visitor.getProcessedObject();
}

PDFObject PDFSecurityHandler::encryptObject(const PDFObject& object, PDFObjectReference reference) const
{
    PDFDecryptOrEncryptObjectVisitor visitor(this, reference, PDFDecryptOrEncryptObjectVisitor::Mode::Encrypt);
    object.accept(&visitor);
    return visitor.getProcessedObject();
}

void PDFSecurityHandler::parseCryptFilters(const PDFDictionary* dictionary, PDFSecurityHandler& handler, int Length, bool publicKey)
{
    const PDFObject& cryptFilterObjects = dictionary->get("CF");
    if (cryptFilterObjects.isDictionary())
    {
        const PDFDictionary* cryptFilters = cryptFilterObjects.getDictionary();
        for (size_t i = 0, cryptFilterCount = cryptFilters->getCount(); i < cryptFilterCount; ++i)
        {
            handler.m_cryptFilters[cryptFilters->getKey(i).getString()] = parseCryptFilter(Length, cryptFilters->getValue(i), publicKey);
        }
    }

    // Now, add standard filters
    auto resolveFilter = [&handler](const QByteArray& name)
    {
        auto it = handler.m_cryptFilters.find(name);

        if (it == handler.m_cryptFilters.cend())
        {
            throw PDFException(PDFTranslationContext::tr("Unknown crypt filter '%1'.").arg(QString::fromLatin1(name)));
        }

        return it->second;
    };

    handler.m_filterStreams = resolveFilter(parseName(dictionary, "StmF", false, IDENTITY_FILTER_NAME));
    handler.m_filterStrings = resolveFilter(parseName(dictionary, "StrF", false, IDENTITY_FILTER_NAME));

    if (dictionary->hasKey("EFF"))
    {
        handler.m_filterEmbeddedFiles = resolveFilter(parseName(dictionary, "EFF", true));
    }
    else
    {
        // According to the PDF specification, if 'EFF' entry is omitted, then filter
        // for streams is used.
        handler.m_filterEmbeddedFiles = handler.m_filterStreams;
    }
}

void PDFSecurityHandler::parseDataStandardSecurityHandler(const PDFDictionary* dictionary, const QByteArray& id, int Length, PDFStandardSecurityHandler& handler)
{
    int R = parseInt(dictionary, "R", true);
    if (R < 2 || R > 6)
    {
        throw PDFException(PDFTranslationContext::tr("Revision %1 of standard security handler is not supported.").arg(R));
    }
    handler.m_R = R;

    handler.m_filterDefault.authEvent = AuthEvent::DocOpen;
    handler.m_filterDefault.keyLength = Length / 8;
    handler.m_filterDefault.type = (R > 4) ? CryptFilterType::AESV3 : CryptFilterType::V2;

    auto readByteArray = [dictionary](const char* key, int size)
    {
        QByteArray result;

        const PDFObject& object = dictionary->get(key);
        if (object.isString())
        {
            result = object.getString();

            if (result.size() < size)
            {
                throw PDFException(PDFTranslationContext::tr("Expected %1 characters long string in entry '%2'. Provided length is %3.").arg(size).arg(QString::fromLatin1(key)).arg(result.size()));
            }

            if (result.size() > size)
            {
                result.resize(size);
            }
        }
        else
        {
            throw PDFException(PDFTranslationContext::tr("Expected %1 characters long string in entry '%2'.").arg(size).arg(QString::fromLatin1(key)));
        }

        return result;
    };

    handler.m_O = readByteArray("O", (R != 6 && R != 5) ? 32 : 48);
    handler.m_U = readByteArray("U", (R != 6 && R != 5) ? 32 : 48);

    handler.m_permissions = static_cast<uint32_t>(static_cast<int>(parseInt(dictionary, "P", true)));

    if (R == 6 || R == 5)
    {
        handler.m_OE = readByteArray("OE", 32);
        handler.m_UE = readByteArray("UE", 32);
        handler.m_Perms = readByteArray("Perms", 16);
    }

    const PDFObject& encryptMetadataObject = dictionary->get("EncryptMetadata");
    if (encryptMetadataObject.isBool())
    {
        handler.m_encryptMetadata = encryptMetadataObject.getBool();
    }

    handler.m_ID = id;
}

PDFSecurityHandlerPointer PDFSecurityHandler::createSecurityHandler(const PDFObject& encryptionDictionaryObject, const QByteArray& id)
{
    if (encryptionDictionaryObject.isNull())
    {
        return PDFSecurityHandlerPointer(new PDFNoneSecurityHandler());
    }

    if (!encryptionDictionaryObject.isDictionary())
    {
        throw PDFException(PDFTranslationContext::tr("Invalid encryption dictionary."));
    }

    const PDFDictionary* dictionary = encryptionDictionaryObject.getDictionary();
    PDFSecurityHandlerPointer handler = createSecurityHandlerInstance(dictionary);

    if (!handler)
    {
        throw PDFException(PDFTranslationContext::tr("Unknown security handler."));
    }

    const int V = parseInt(dictionary, "V", true);

    // Check V
    if (V < 1 || V > 5)
    {
        throw PDFException(PDFTranslationContext::tr("Unsupported version of document encryption (V = %1).").arg(V));
    }

    // Only valid for V == 2 or V == 3, otherwise we set file encryption key length manually
    int Length = 40;

    switch (V)
    {
        case 1:
            Length = 40;
            break;

        case 2:
        case 3:
        {
            // Table 20: a multiple of 8 from 40 to 128 - a negative or a huge value would read
            // out of the key buffers (B83)
            const PDFInteger lengthValue = parseInt(dictionary, "Length", false, 40);
            if (lengthValue < 40 || lengthValue > 128 || lengthValue % 8 != 0)
            {
                throw PDFException(PDFTranslationContext::tr("Invalid length of the encryption key (%1 bits).").arg(lengthValue));
            }
            Length = int(lengthValue);
            break;
        }

        case 4:
            Length = 128;
            break;

        case 5:
            Length = 256;
            break;

        default:
            Q_ASSERT(false);
            break;
    }

    // Create security handler
    handler->m_V = V;
    handler->m_keyLength = Length;

    // Add "Identity" filter to the filters
    CryptFilter identityFilter;
    identityFilter.type = CryptFilterType::Identity;
    handler->m_cryptFilters[IDENTITY_FILTER_NAME] = identityFilter;

    if (V == 4 || V == 5)
    {
        parseCryptFilters(dictionary, *handler, Length, handler->getMode() == EncryptionMode::PublicKey);

        // AESV3 uses the file encryption key of 256 bits (7.6.3.3), which only V 5 has
        for (const CryptFilter* filter : { &handler->m_filterStreams, &handler->m_filterStrings, &handler->m_filterEmbeddedFiles })
        {
            if (filter->type == CryptFilterType::AESV3 && Length != 256)
            {
                throw PDFException(PDFTranslationContext::tr("Crypt filter AESV3 requires a 256-bit key (V = %1).").arg(V));
            }
        }
    }

    switch (handler->getMode())
    {
        case EncryptionMode::Standard:
        {
            auto typedHandler = qSharedPointerDynamicCast<PDFStandardSecurityHandler>(handler);
            parseDataStandardSecurityHandler(dictionary, id, Length, *typedHandler);
            break;
        }

        case EncryptionMode::PublicKey:
        {
            auto typedHandler = qSharedPointerDynamicCast<PDFPublicKeySecurityHandler>(handler);
            const QByteArrayList dictionaryRecipients = parseRecipients(dictionary);

            if (V < 4)
            {
                // adbe.pkcs7.s3 / s4 (7.6.5.2): RC4 by the key of /Length, the recipients in the
                // encryption dictionary
                typedHandler->m_filterDefault.type = CryptFilterType::V2;
                typedHandler->m_filterDefault.keyLength = Length / 8;
                typedHandler->m_filterDefault.recipients = dictionaryRecipients;
            }
            else
            {
                // adbe.pkcs7.s5 (7.6.5.2, 7.6.6): the recipients are in the crypt filter, which
                // encrypts - the one of /StmF, of /EFF when the streams are Identity (File
                // attachments only), of /StrF at last; never a fixed name (B105)
                const CryptFilter* source = &typedHandler->m_filterStrings;
                if (typedHandler->m_filterStreams.type != CryptFilterType::Identity && typedHandler->m_filterStreams.type != CryptFilterType::None)
                {
                    source = &typedHandler->m_filterStreams;
                }
                else if (typedHandler->m_filterEmbeddedFiles.type != CryptFilterType::Identity && typedHandler->m_filterEmbeddedFiles.type != CryptFilterType::None)
                {
                    source = &typedHandler->m_filterEmbeddedFiles;
                }

                if (source->type != CryptFilterType::Identity && source->type != CryptFilterType::None)
                {
                    typedHandler->m_filterDefault = *source;
                }

                if (typedHandler->m_filterDefault.recipients.isEmpty())
                {
                    typedHandler->m_filterDefault.recipients = dictionaryRecipients;
                    typedHandler->m_filterDefault.encryptMetadata = parseBool(dictionary, "EncryptMetadata", false, true);
                }
            }

            QString name = parseName(dictionary, "SubFilter", true, nullptr);
            if (name == "adbe.pkcs7.s3")
            {
                typedHandler->m_pkcs7Type = PDFPublicKeySecurityHandler::PKCS7_Type::PKCS7_S3;
            }
            if (name == "adbe.pkcs7.s4")
            {
                typedHandler->m_pkcs7Type = PDFPublicKeySecurityHandler::PKCS7_Type::PKCS7_S4;
            }
            if (name == "adbe.pkcs7.s5")
            {
                typedHandler->m_pkcs7Type = PDFPublicKeySecurityHandler::PKCS7_Type::PKCS7_S5;
            }

            typedHandler->m_permissions = static_cast<uint32_t>(static_cast<int>(parseInt(dictionary, "P", false, 0)));
            break;
        }

        case EncryptionMode::None:
        case EncryptionMode::Custom:
            Q_ASSERT(false);
            break;

        default:
            Q_ASSERT(false);
    }

    // The source of the handler (isSameEncryption, R6-1): the strings of the encryption dictionary
    // are never encrypted (7.6.2), so the dictionaries of two readings of one file are equal
    handler->m_encryptionDictionary = encryptionDictionaryObject;
    handler->m_encryptionDocumentId = id;
    return handler;
}

bool PDFSecurityHandler::isSameEncryption(const PDFSecurityHandler* other) const
{
    if (!other || getMode() == EncryptionMode::None || getMode() != other->getMode())
    {
        return false;
    }

    // A handler of the factory has no dictionary of a file: it is the same as no other handler
    return !m_encryptionDictionary.isNull() && m_encryptionDictionary == other->m_encryptionDictionary && m_encryptionDocumentId == other->m_encryptionDocumentId;
}

void PDFSecurityHandler::setNoDRMMode()
{
    s_noDrmMode = true;
}

bool PDFSecurityHandler::isNoDRM()
{
    return s_noDrmMode;
}

bool PDFSecurityHandler::isEmbeddedFilesOnlyEncryption() const
{
    if (getMode() != EncryptionMode::Standard && getMode() != EncryptionMode::PublicKey)
    {
        return false;
    }

    // 7.6.6: /StmF and /StrF Identity, the embedded files by their own crypt filter with the
    // authorization at their opening (Table 27 /AuthEvent /EFOpen)
    const bool isEmbeddedFileFilterEncrypting = m_filterEmbeddedFiles.type != CryptFilterType::Identity && m_filterEmbeddedFiles.type != CryptFilterType::None;
    return m_filterStrings.type == CryptFilterType::Identity &&
           m_filterStreams.type == CryptFilterType::Identity &&
           isEmbeddedFileFilterEncrypting &&
           m_filterEmbeddedFiles.authEvent == AuthEvent::EFOpen;
}

bool PDFSecurityHandler::isEmbeddedFileStream(const PDFDictionary* streamDictionary)
{
    if (!streamDictionary)
    {
        return false;
    }

    const PDFObject& typeObject = streamDictionary->get("Type");
    return typeObject.isName() && typeObject.getString() == "EmbeddedFile";
}

std::optional<PDFObjectReference> PDFSecurityHandler::findDocumentMetadataReference(const PDFObject& catalog)
{
    if (!catalog.isDictionary())
    {
        return std::nullopt;
    }

    // 14.3.2: the metadata stream of the catalog is an indirect object (a stream always is)
    const PDFObject& metadata = catalog.getDictionary()->get("Metadata");
    return metadata.isReference() ? metadata.getReference() : PDFObjectReference();
}

bool PDFSecurityHandler::isUnencryptedMetadataStream(PDFObjectReference reference, const PDFDictionary* streamDictionary) const
{
    if (isMetadataEncrypted() || !streamDictionary)
    {
        return false;
    }

    // Table 315: a metadata stream has /Type /Metadata; another stream of the catalog's /Metadata
    // (a hostile file: the content of a page) is encrypted as any stream
    const PDFObject& typeObject = streamDictionary->get("Type");
    if (!typeObject.isName() || typeObject.getString() != "Metadata")
    {
        return false;
    }

    // Table 20: only the document-level metadata stream; any metadata stream, when it is not known
    // (the catalog in an object stream - owner 2026-11-15)
    return !m_documentMetadataReference.has_value() || *m_documentMetadataReference == reference;
}

bool PDFSecurityHandler::isSignatureDictionary(const PDFDictionary* dictionary)
{
    if (!dictionary)
    {
        return false;
    }

    const PDFObject& typeObject = dictionary->get("Type");
    if (typeObject.isName())
    {
        return typeObject.getString() == "Sig" || typeObject.getString() == "DocTimeStamp";
    }

    // Table 255: /Type is optional; /Filter and /Contents are required, /ByteRange is required
    // for a signature of the document (Table 255, 12.8.1)
    return typeObject.isNull() &&
           dictionary->get("Filter").isName() &&
           dictionary->get("ByteRange").isArray() &&
           dictionary->get("Contents").isString();
}

bool PDFSecurityHandler::isCryptFilterStream(const PDFDictionary* streamDictionary)
{
    if (!streamDictionary)
    {
        return false;
    }

    const PDFObject& filterObject = streamDictionary->get("Filter");
    if (filterObject.isName())
    {
        return filterObject.getString() == "Crypt";
    }

    if (filterObject.isArray() && filterObject.getArray()->getCount() > 0)
    {
        const PDFObject& firstFilter = filterObject.getArray()->getItem(0);
        return firstFilter.isName() && firstFilter.getString() == "Crypt";
    }

    return false;
}

bool PDFSecurityHandler::canDecryptByFilter(const QByteArray& filterName) const
{
    if (filterName == IDENTITY_FILTER_NAME)
    {
        return true;
    }

    auto it = m_cryptFilters.find(filterName);
    return it != m_cryptFilters.cend() && !isLockedFilter(it->second);
}

bool PDFSecurityHandler::isLockedFilter(const CryptFilter& filter) const
{
    // A document opened without the password of its embedded files has no file encryption key at
    // all: every crypt filter but Identity is locked - the one of the embedded files (/AuthEvent
    // /EFOpen) and every other one a stream may name by its /Crypt step, which would decrypt
    // garbage by an empty key (review RS5SEC)
    return m_isEmbeddedFilesLocked && filter.type != CryptFilterType::Identity;
}

PDFSecurityHandler::AuthorizationResult PDFSecurityHandler::authenticateByCertificates(const PDFCertificateEntries& certificates,
                                                                                      const std::function<QString(bool*)>& getPasswordCallback,
                                                                                      bool authorizeOwnerOnly)
{
    if (getMode() == EncryptionMode::PublicKey)
    {
        PDFPublicKeySecurityHandler* publicKeyHandler = static_cast<PDFPublicKeySecurityHandler*>(this);
        publicKeyHandler->m_authorizationData = PDFStandardOrPublicSecurityHandler::AuthorizationData();
        publicKeyHandler->m_isEmbeddedFilesLocked = false;

        // 7.6.5: the public-key handler has no owner
        if (authorizeOwnerOnly)
        {
            return AuthorizationResult::Failed;
        }

        return publicKeyHandler->authenticateByCertificateEntries(certificates, getPasswordCallback);
    }

    return authenticate(getPasswordCallback, authorizeOwnerOnly);
}

PDFSecurityHandler::AuthorizationResult PDFSecurityHandler::authenticateWithoutEmbeddedFiles()
{
    if (!isEmbeddedFilesOnlyEncryption())
    {
        return AuthorizationResult::Failed;
    }

    // The strings and the streams are decrypted by Identity, which needs no key; the embedded
    // files stay encrypted (the visitor keeps them, their crypt filter refuses to decrypt)
    PDFStandardOrPublicSecurityHandler* handler = static_cast<PDFStandardOrPublicSecurityHandler*>(this);
    handler->m_authorizationData = PDFStandardOrPublicSecurityHandler::AuthorizationData();
    handler->m_authorizationData.authorizationResult = AuthorizationResult::UserAuthorized;
    m_isEmbeddedFilesLocked = true;
    return AuthorizationResult::UserAuthorized;
}

void PDFSecureMemory::clear(void* data, size_t size)
{
    if (data && size > 0)
    {
        OPENSSL_cleanse(data, size);
    }
}

void PDFSecureMemory::clear(QByteArray& data)
{
    // A shared buffer (another QByteArray holds it) or raw data of another owner is never written:
    // data() would detach it and clear only a new copy, the other owner clears its own buffer
    if (!data.isEmpty() && data.isDetached())
    {
        clear(data.data(), size_t(data.size()));
    }
}

void PDFSecureMemory::clear(QString& text)
{
    if (!text.isEmpty() && text.isDetached())
    {
        clear(text.data(), size_t(text.size()) * sizeof(QChar));
    }
}

bool PDFSecurityHandler::isAllowedByRevision(const PDFSecurityHandler* securityHandler, Permission permission)
{
    if (!securityHandler)
    {
        return true;
    }

    // Table 22: bits 9 to 12 exist from revision 3 (an owner authorized handler allows
    // everything by isAllowed); the extraction for the accessibility is always allowed (bit 10,
    // R5-14), also by revision 2
    if (securityHandler->getMode() == EncryptionMode::Standard && static_cast<const PDFStandardSecurityHandler*>(securityHandler)->getRevision() == 2 &&
        permission != Permission::Accessibility)
    {
        permission = getRevision2Permission(permission);
    }
    return securityHandler->isAllowed(permission);
}

bool PDFSecurityHandler::isEncryptionChangeAllowed() const
{
    switch (getMode())
    {
        case EncryptionMode::None:
            return true;

        case EncryptionMode::Standard:
            return !m_isEmbeddedFilesLocked && getAuthorizationResult() == AuthorizationResult::OwnerAuthorized;

        case EncryptionMode::PublicKey:
        {
            // m_permissions of an authorized handler are the bytes of the envelope (or nothing)
            const PDFPublicKeySecurityHandler* handler = static_cast<const PDFPublicKeySecurityHandler*>(this);
            return !m_isEmbeddedFilesLocked && handler->m_authorizationData.isAuthorized() && (handler->m_permissions & PDFPublicKeySecurityHandler::PKSH_Owner);
        }

        default:
            break;
    }

    return false;
}

PDFSecurityHandler::Permission PDFSecurityHandler::getRevision2Permission(Permission permission)
{
    switch (permission)
    {
        case Permission::ModifyFormFields:
            return Permission::ModifyInteractiveItems;
        case Permission::Accessibility:
            return Permission::CopyContent;
        case Permission::Assemble:
            return Permission::Modify;
        case Permission::PrintHighResolution:
            return Permission::PrintLowResolution;
        default:
            break;
    }
    return permission;
}

void PDFSecurityHandler::fillEncryptionDictionary(PDFObjectFactory& factory, bool publicKeyHandler) const
{
    factory.beginDictionaryItem("V");
    factory << PDFInteger(m_V);
    factory.endDictionaryItem();

    factory.beginDictionaryItem("Length");
    factory << PDFInteger(m_keyLength);
    factory.endDictionaryItem();

    if (m_V == 4 || m_V == 5)
    {
        factory.beginDictionaryItem("CF");

        factory.beginDictionary();

        QByteArray stmfName = "Identity";
        QByteArray strfName = stmfName;
        QByteArray effName = stmfName;

        for (const auto& cryptFilter : m_cryptFilters)
        {
            // Identity is predefined (7.6.6, Table 25): it is never an entry of /CF, and /CFM has
            // no value for it (a reader refuses it)
            if (cryptFilter.second.type == CryptFilterType::Identity || cryptFilter.first == IDENTITY_FILTER_NAME)
            {
                continue;
            }

            factory.beginDictionaryItem(cryptFilter.first);

            factory.beginDictionary();
            factory.beginDictionaryItem("CFM");

            if (cryptFilter.second == m_filterStrings)
            {
                strfName = cryptFilter.first;
            }
            if (cryptFilter.second == m_filterStreams)
            {
                stmfName = cryptFilter.first;
            }
            if (cryptFilter.second == m_filterEmbeddedFiles)
            {
                effName = cryptFilter.first;
            }

            switch (cryptFilter.second.type)
            {
                case CryptFilterType::None:
                    // The application shall decrypt the data using the security handler
                    factory << WrapName("None");
                    break;

                case CryptFilterType::V2:
                    // Use file encryption key for RC4 algorithm
                    factory << WrapName("V2");
                    break;

                case CryptFilterType::AESV2:
                    // Use file encryption key for AES algorithm
                    factory << WrapName("AESV2");
                    break;

                case CryptFilterType::AESV3:
                    // Use file encryption key for AES 256 bit algorithm
                    factory << WrapName("AESV3");
                    break;

                case CryptFilterType::Identity:
                    // Don't decrypt anything, use identity function
                    factory << WrapName("Identity");
                    break;

                default:
                    Q_ASSERT(false);
                    factory << WrapName("None");
                    break;
            }

            factory.endDictionaryItem();

            factory.beginDictionaryItem("AuthEvent");

            switch (cryptFilter.second.authEvent)
            {
                case AuthEvent::DocOpen:
                    factory << WrapName("DocOpen");
                    break;

                case AuthEvent::EFOpen:
                    factory << WrapName("EFOpen");
                    break;

                default:
                    Q_ASSERT(false);
                    break;
            }

            factory.endDictionaryItem();

            // Jakub Melka: Warning! According to the PDF 2.0 specification,
            // standard security handler expresses key length in bytes (32
            // for 256 bit key), but public key security handler in bits (value
            // 256 for 256 bit key).
            factory.beginDictionaryItem("Length");
            if (!publicKeyHandler)
            {
                factory << cryptFilter.second.keyLength;
            }
            else
            {
                factory << PDFInteger(cryptFilter.second.keyLength * 8);
            }
            factory.endDictionaryItem();

            if (publicKeyHandler)
            {
                factory.beginDictionaryItem("Recipients");
                factory.beginArray();
                for (const auto& recipient : cryptFilter.second.recipients)
                {
                    factory << WrapString(recipient);
                }
                factory.endArray();
                factory.endDictionaryItem();

                factory.beginDictionaryItem("EncryptMetadata");
                factory << cryptFilter.second.encryptMetadata;
                factory.endDictionaryItem();
            }

            factory.endDictionary();

            factory.endDictionaryItem();
        }

        factory.endDictionary();

        factory.endDictionaryItem();

        // Store StmF, StrF, EFF
        factory.beginDictionaryItem("StmF");
        factory << WrapName(stmfName);
        factory.endDictionaryItem();

        factory.beginDictionaryItem("StrF");
        factory << WrapName(strfName);
        factory.endDictionaryItem();

        factory.beginDictionaryItem("EFF");
        factory << WrapName(effName);
        factory.endDictionaryItem();
    }
}

QByteArray PDFSecurityHandler::parseName(const PDFDictionary* dictionary, const char* key, bool required, const char* defaultValue)
{
    const PDFObject& nameObject = dictionary->get(key);

    if (nameObject.isNull())
    {
        return defaultValue ? QByteArray(defaultValue) : QByteArray();
    }

    if (!nameObject.isName())
    {
        if (required)
        {
            throw PDFException(PDFTranslationContext::tr("Invalid value for entry '%1' in encryption dictionary. Name expected.").arg(QString::fromLatin1(key)));
        }

        return defaultValue ? QByteArray(defaultValue) : QByteArray();
    }

    return nameObject.getString();
}

bool PDFSecurityHandler::parseBool(const PDFDictionary* dictionary, const char* key, bool required, bool defaultValue)
{
    const PDFObject& intObject = dictionary->get(key);
    if (!intObject.isBool())
    {
        if (required)
        {
            throw PDFException(PDFTranslationContext::tr("Invalid value for entry '%1' in encryption dictionary. Boolean expected.").arg(QString::fromLatin1(key)));
        }

        return defaultValue;
    }

    return intObject.getBool();
}

PDFInteger PDFSecurityHandler::parseInt(const PDFDictionary* dictionary, const char* key, bool required, PDFInteger defaultValue)
{
    const PDFObject& intObject = dictionary->get(key);
    if (!intObject.isInt())
    {
        if (required)
        {
            throw PDFException(PDFTranslationContext::tr("Invalid value for entry '%1' in encryption dictionary. Integer expected.").arg(QString::fromLatin1(key)));
        }

        return defaultValue;
    }

    return intObject.getInteger();
}

CryptFilter PDFSecurityHandler::parseCryptFilter(PDFInteger length, const PDFObject& object, bool publicKey)
{
    if (!object.isDictionary())
    {
        throw PDFException(PDFTranslationContext::tr("Crypt filter is not a dictionary!"));
    }
    const PDFDictionary* cryptFilterDictionary = object.getDictionary();

    CryptFilter filter;

    QByteArray CFMName = parseName(cryptFilterDictionary, "CFM", false, "None");
    if (CFMName == "None")
    {
        filter.type = CryptFilterType::None;
    }
    else if (CFMName == "V2")
    {
        filter.type = CryptFilterType::V2;
    }
    else if (CFMName == "AESV2")
    {
        filter.type = CryptFilterType::AESV2;
    }
    else if (CFMName == "AESV3")
    {
        filter.type = CryptFilterType::AESV3;
    }
    else
    {
        throw PDFException(PDFTranslationContext::tr("Unsupported encryption algorithm '%1'.").arg(QString::fromLatin1(CFMName)));
    }

    QByteArray authEventName = parseName(cryptFilterDictionary, "AuthEvent", false, "DocOpen");
    if (authEventName == "DocOpen")
    {
        filter.authEvent = AuthEvent::DocOpen;
    }
    else if (authEventName == "EFOpen")
    {
        filter.authEvent = AuthEvent::EFOpen;
    }
    else
    {
        throw PDFException(PDFTranslationContext::tr("Unsupported authorization event '%1'.").arg(QString::fromLatin1(authEventName)));
    }

    // Table 25 /Length: the standard handler gives the key length in bytes, a public-key handler in
    // bits (some writers give bits to the standard handler too). The key of an object is computed
    // of the length of the file encryption key (Algorithm 1), so a hostile value only falls back
    // to the length of the handler (B83); the length is kept in bytes.
    filter.keyLength = int(length / 8);
    const PDFInteger lengthValue = parseInt(cryptFilterDictionary, "Length", false, -1);
    if (lengthValue > 0)
    {
        PDFInteger bytes = lengthValue;
        if (publicKey || (lengthValue >= 40 && lengthValue % 8 == 0))
        {
            bytes = lengthValue / 8;
        }

        if (bytes >= 5 && bytes <= 32)
        {
            filter.keyLength = int(bytes);
        }
    }

    switch (filter.type)
    {
        case CryptFilterType::AESV2:
            filter.keyLength = 16;
            break;

        case CryptFilterType::AESV3:
            filter.keyLength = 32;
            break;

        default:
            break;
    }

    // Recipients
    filter.recipients = parseRecipients(cryptFilterDictionary);

    // Encrypt metadata
    filter.encryptMetadata = parseBool(cryptFilterDictionary, "EncryptMetadata", false, true);

    return filter;
}

PDFSecurityHandlerPointer PDFSecurityHandler::createSecurityHandlerInstance(const PDFDictionary* dictionary)
{
    QByteArray filterName = parseName(dictionary, "Filter", true);

    if (filterName == "Standard")
    {
        return PDFSecurityHandlerPointer(new PDFStandardSecurityHandler());
    }

    if (filterName == "Entrust.PPKEF" || filterName == "Adobe.PPKLite" || filterName == "Adobe.PubSec")
    {
        QByteArray subFilter = parseName(dictionary, "SubFilter", true);
        if (subFilter == "adbe.pkcs7.s3" || subFilter == "adbe.pkcs7.s4" || subFilter == "adbe.pkcs7.s5")
        {
            return PDFSecurityHandlerPointer(new PDFPublicKeySecurityHandler());
        }
    }

    return PDFSecurityHandlerPointer();
}

QByteArrayList PDFSecurityHandler::parseRecipients(const PDFDictionary* dictionary)
{
    QByteArrayList result;

    const PDFObject& recipients = dictionary->get("Recipients");
    if (recipients.isArray())
    {
        // B95: each recipient costs a PKCS#7 decryption per certificate of the user at the
        // opening, and every recipient goes into the key - a hostile file is refused, not cut
        const PDFArray* recipientArray = recipients.getArray();
        if (recipientArray->getCount() > size_t(PDFSecurityHandlerFactory::MAXIMUM_RECIPIENT_COUNT))
        {
            throw PDFException(PDFTranslationContext::tr("Too many recipients of the encryption (%1, at most %2).").arg(qulonglong(recipientArray->getCount())).arg(PDFSecurityHandlerFactory::MAXIMUM_RECIPIENT_COUNT));
        }

        for (const PDFObject& object : *recipientArray)
        {
            if (object.isString())
            {
                const QByteArray recipient = object.getString();
                if (recipient.size() > PDFSecurityHandlerFactory::MAXIMUM_RECIPIENT_SIZE)
                {
                    throw PDFException(PDFTranslationContext::tr("A recipient of the encryption is too large (%1 bytes, at most %2).").arg(recipient.size()).arg(PDFSecurityHandlerFactory::MAXIMUM_RECIPIENT_SIZE));
                }
                result << recipient;
            }
        }
    }

    return result;
}

std::vector<uint8_t> PDFStandardOrPublicSecurityHandler::createV2_ObjectEncryptionKey(PDFObjectReference reference, CryptFilter filter) const
{
    std::vector<uint8_t> inputKeyData = convertByteArrayToVector(m_authorizationData.fileEncryptionKey);
    uint32_t objectNumber = qToLittleEndian(static_cast<uint32_t>(reference.objectNumber));
    uint32_t generation = qToLittleEndian(static_cast<uint32_t>(reference.generation));
    inputKeyData.insert(inputKeyData.cend(), { uint8_t(objectNumber & 0xFF), uint8_t((objectNumber >> 8) & 0xFF), uint8_t((objectNumber >> 16) & 0xFF), uint8_t(generation & 0xFF), uint8_t((generation >> 8) & 0xFF) });
    std::vector<uint8_t> objectEncryptionKey(MD5_DIGEST_LENGTH, uint8_t(0));
    MD5(inputKeyData.data(), inputKeyData.size(), objectEncryptionKey.data());

    // Use up to (n + 5) bytes, maximally 16, from the digest as object encryption key; n is the
    // length of the file encryption key (Algorithm 1, step d), never a value of the file (B83)
    Q_UNUSED(filter);
    const size_t objectEncryptionKeySize = qMin(size_t(m_authorizationData.fileEncryptionKey.size()) + 5, size_t(MD5_DIGEST_LENGTH));
    objectEncryptionKey.resize(objectEncryptionKeySize);

    return objectEncryptionKey;
}

std::vector<uint8_t> PDFStandardOrPublicSecurityHandler::createAESV2_ObjectEncryptionKey(PDFObjectReference reference) const
{
    std::vector<uint8_t> inputKeyData = convertByteArrayToVector(m_authorizationData.fileEncryptionKey);
    uint32_t objectNumber = qToLittleEndian(static_cast<uint32_t>(reference.objectNumber));
    uint32_t generation = qToLittleEndian(static_cast<uint32_t>(reference.generation));
    inputKeyData.insert(inputKeyData.cend(), { uint8_t(objectNumber & 0xFF), uint8_t((objectNumber >> 8) & 0xFF), uint8_t((objectNumber >> 16) & 0xFF), uint8_t(generation & 0xFF), uint8_t((generation >> 8) & 0xFF), 0x73, 0x41, 0x6C, 0x54 });
    std::vector<uint8_t> objectEncryptionKey(MD5_DIGEST_LENGTH, uint8_t(0));
    MD5(inputKeyData.data(), inputKeyData.size(), objectEncryptionKey.data());

    return objectEncryptionKey;
}

CryptFilter PDFStandardOrPublicSecurityHandler::resolveMethodNone(const CryptFilter& filter) const
{
    if (filter.type != CryptFilterType::None)
    {
        return filter;
    }

    // Table 25, /CFM /None (also a crypt filter without /CFM): the security handler decrypts the
    // data by its own method - the method of its default filter (V2 of revisions 2 - 4, AESV3 of
    // revisions 5 and 6, the crypt filter of the recipients of the public-key handler)
    if (m_filterDefault.type == CryptFilterType::None)
    {
        throw PDFException(PDFTranslationContext::tr("The crypt filter of the method None has no method of the security handler to decrypt the data."));
    }

    CryptFilter resolved = m_filterDefault;
    resolved.authEvent = filter.authEvent;
    return resolved;
}

QByteArray PDFStandardOrPublicSecurityHandler::decryptUsingFilter(const QByteArray& data, CryptFilter filter, PDFObjectReference reference) const
{
    QByteArray decryptedData;

    Q_ASSERT(m_authorizationData.isAuthorized());

    filter = resolveMethodNone(filter);
    if (isLockedFilter(filter))
    {
        // Open without attachments (Q5.9 c): the key of the embedded files is not known
        throw PDFException(PDFTranslationContext::tr("The embedded files are locked. Enter the password of the document to open them."));
    }

    if (filter.type == CryptFilterType::AESV3 && m_authorizationData.fileEncryptionKey.size() != 32)
    {
        throw PDFException(PDFTranslationContext::tr("Crypt filter AESV3 requires a 256-bit key."));
    }

    struct AES_data
    {
        QByteArray initializationVector;
        QByteArray paddedData;
    };

    auto prepareAES_data = [](const QByteArray& data)
    {
        AES_data result;

        result.initializationVector = data.left(AES_BLOCK_SIZE);

        // This is an error. But to handle it, we resize the vector
        // with arbitrary data.
        if (result.initializationVector.size() < AES_BLOCK_SIZE)
        {
            result.initializationVector.resize(AES_BLOCK_SIZE);
        }

        result.paddedData = data.mid(AES_BLOCK_SIZE);

        // Remove errorneous data - we must have a data of multiple of AES_BLOCK_SIZE
        const int remainder = result.paddedData.size() % AES_BLOCK_SIZE;
        if (remainder != 0)
        {
            result.paddedData = result.paddedData.left(result.paddedData.size() - remainder);
        }

        return result;
    };

    auto removeAES_padding = [](const QByteArray& data)
    {
        if (data.isEmpty())
        {
            return data;
        }

        // If padding doesnt fit from 1 to AES_BLOCK_SIZE, then it is
        // an error, but just clamp the value.
        const int padding = data.back();
        const int clampedPadding = qBound(1, padding, AES_BLOCK_SIZE);

        return data.left(data.size() - clampedPadding);
    };

    switch (filter.type)
    {
        case CryptFilterType::None:       // The application shall decrypt the data using the security handler
        {
            // Resolved to the method of the handler by resolveMethodNone (which throws without one)
            break;
        }

        case CryptFilterType::V2:         // Use file encryption key for RC4 algorithm
        {
            std::vector<uint8_t> objectEncryptionKey = createV2_ObjectEncryptionKey(reference, filter);
            decryptedData.resize(data.size());

            RC4_KEY key = { };
            RC4_set_key(&key, static_cast<int>(objectEncryptionKey.size()), objectEncryptionKey.data());
            RC4(&key, data.size(), convertByteArrayToUcharPtr(data), convertByteArrayToUcharPtr(decryptedData));

            break;
        }

        case CryptFilterType::AESV2:      // Use file encryption key for AES algorithm
        {
            std::vector<uint8_t> objectEncryptionKey = createAESV2_ObjectEncryptionKey(reference);

            // For AES algorithm, always use 16 bytes key (128 bit encryption mode)

            AES_KEY key = { };
            AES_set_decrypt_key(objectEncryptionKey.data(), static_cast<int>(objectEncryptionKey.size()) * 8, &key);

            AES_data aes_data = prepareAES_data(data);
            if (!aes_data.paddedData.isEmpty())
            {
                decryptedData.resize(aes_data.paddedData.size());
                AES_cbc_encrypt(convertByteArrayToUcharPtr(aes_data.paddedData), convertByteArrayToUcharPtr(decryptedData), aes_data.paddedData.length(), &key, convertByteArrayToUcharPtr(aes_data.initializationVector), AES_DECRYPT);
                decryptedData = removeAES_padding(decryptedData);
            }

            break;
        }

        case CryptFilterType::AESV3:      // Use file encryption key for AES 256 bit algorithm
        {
            AES_KEY key = { };
            AES_set_decrypt_key(convertByteArrayToUcharPtr(m_authorizationData.fileEncryptionKey), static_cast<int>(m_authorizationData.fileEncryptionKey.size()) * 8, &key);

            AES_data aes_data = prepareAES_data(data);
            if (!aes_data.paddedData.isEmpty())
            {
                decryptedData.resize(aes_data.paddedData.size());
                AES_cbc_encrypt(convertByteArrayToUcharPtr(aes_data.paddedData), convertByteArrayToUcharPtr(decryptedData), aes_data.paddedData.length(), &key, convertByteArrayToUcharPtr(aes_data.initializationVector), AES_DECRYPT);
                decryptedData = removeAES_padding(decryptedData);
            }

            break;
        }

        case CryptFilterType::Identity:   // Don't decrypt anything, use identity function
        {
            decryptedData = data;
            break;
        }
    }

    return decryptedData;
}

QByteArray PDFStandardOrPublicSecurityHandler::encryptUsingFilter(const QByteArray& data, CryptFilter filter, PDFObjectReference reference) const
{
    QByteArray encryptedData;

    Q_ASSERT(m_authorizationData.isAuthorized());

    filter = resolveMethodNone(filter);
    if (isLockedFilter(filter))
    {
        throw PDFException(PDFTranslationContext::tr("The embedded files are locked. Enter the password of the document to open them."));
    }

    if (filter.type == CryptFilterType::AESV3 && m_authorizationData.fileEncryptionKey.size() != 32)
    {
        throw PDFException(PDFTranslationContext::tr("Crypt filter AESV3 requires a 256-bit key."));
    }

    struct AES_data
    {
        QByteArray initializationVector;
        QByteArray paddedData;
    };

    auto prepareAES_data = [](const QByteArray& data)
    {
        AES_data result;

        // The IV of every string and stream comes from the cryptographic generator (B82, S5.4)
        result.initializationVector = PDFSecurityHandlerFactory::generateRandomBytes(AES_BLOCK_SIZE);

        // Add padding remainder according to the specification (7.6.3.2: 1 to 16 bytes, each of
        // the value of their count)
        const int size = int(data.size());
        const int paddingRemainder = AES_BLOCK_SIZE - (size % AES_BLOCK_SIZE);

        result.paddedData.reserve(size + paddingRemainder);
        result.paddedData = data;
        result.paddedData.append(QByteArray(paddingRemainder, char(paddingRemainder)));

        return result;
    };

    switch (filter.type)
    {
        case CryptFilterType::None:       // The application shall encrypt the data using the security handler
        {
            // Resolved to the method of the handler by resolveMethodNone (which throws without one)
            break;
        }

        case CryptFilterType::V2:         // Use file encryption key for RC4 algorithm
        {
            // This algorithm is same as the encrypt algorithm, because RC4 cipher is symmetrical
            std::vector<uint8_t> objectEncryptionKey = createV2_ObjectEncryptionKey(reference, filter);
            encryptedData.resize(data.size());

            RC4_KEY key = { };
            RC4_set_key(&key, static_cast<int>(objectEncryptionKey.size()), objectEncryptionKey.data());
            RC4(&key, data.size(), convertByteArrayToUcharPtr(data), convertByteArrayToUcharPtr(encryptedData));

            break;
        }

        case CryptFilterType::AESV2:      // Use file encryption key for AES algorithm
        {
            std::vector<uint8_t> objectEncryptionKey = createAESV2_ObjectEncryptionKey(reference);

            // For AES algorithm, always use 16 bytes key (128 bit encryption mode)

            AES_KEY key = { };
            AES_set_encrypt_key(objectEncryptionKey.data(), static_cast<int>(objectEncryptionKey.size()) * 8, &key);

            AES_data aes_data = prepareAES_data(data);
            if (!aes_data.paddedData.isEmpty())
            {
                QByteArray initializationVectorCopy = aes_data.initializationVector;
                encryptedData.resize(aes_data.paddedData.size());
                AES_cbc_encrypt(convertByteArrayToUcharPtr(aes_data.paddedData), convertByteArrayToUcharPtr(encryptedData), aes_data.paddedData.length(), &key, convertByteArrayToUcharPtr(aes_data.initializationVector), AES_ENCRYPT);
                encryptedData.prepend(initializationVectorCopy);
            }

            break;
        }

        case CryptFilterType::AESV3:      // Use file encryption key for AES 256 bit algorithm
        {
            AES_KEY key = { };
            AES_set_encrypt_key(convertByteArrayToUcharPtr(m_authorizationData.fileEncryptionKey), static_cast<int>(m_authorizationData.fileEncryptionKey.size()) * 8, &key);

            AES_data aes_data = prepareAES_data(data);
            if (!aes_data.paddedData.isEmpty())
            {
                QByteArray initializationVectorCopy = aes_data.initializationVector;
                encryptedData.resize(aes_data.paddedData.size());
                AES_cbc_encrypt(convertByteArrayToUcharPtr(aes_data.paddedData), convertByteArrayToUcharPtr(encryptedData), aes_data.paddedData.length(), &key, convertByteArrayToUcharPtr(aes_data.initializationVector), AES_ENCRYPT);
                encryptedData.prepend(initializationVectorCopy);
            }

            break;
        }

        case CryptFilterType::Identity:   // Don't decrypt anything, use identity function
        {
            encryptedData = data;
            break;
        }
    }

    return encryptedData;
}

QByteArray PDFStandardOrPublicSecurityHandler::decrypt(const QByteArray& data, PDFObjectReference reference, EncryptionScope encryptionScope) const
{
    CryptFilter filter = getCryptFilter(encryptionScope);
    return decryptUsingFilter(data, filter, reference);
}

QByteArray PDFStandardOrPublicSecurityHandler::decryptByFilter(const QByteArray& data, const QByteArray& filterName, PDFObjectReference reference) const
{
    // Identity is predefined (7.6.6), also for a handler of the factory, which has no entry of it
    if (filterName == IDENTITY_FILTER_NAME)
    {
        return data;
    }

    auto it = m_cryptFilters.find(filterName);
    if (it == m_cryptFilters.cend())
    {
        throw PDFException(PDFTranslationContext::tr("Crypt filter '%1' not found.").arg(QString::fromLatin1(filterName)));
    }

    return decryptUsingFilter(data, it->second, reference);
}

CryptFilter PDFStandardOrPublicSecurityHandler::getCryptFilter(EncryptionScope encryptionScope) const
{
    CryptFilter filter = m_filterDefault;

    switch (encryptionScope)
    {
        case EncryptionScope::String:
        {
            if (m_filterStrings.type != CryptFilterType::None)
            {
                filter = m_filterStrings;
            }

            break;
        }

        case EncryptionScope::Stream:
        {
            if (m_filterStreams.type != CryptFilterType::None)
            {
                filter = m_filterStreams;
            }

            break;
        }

        case EncryptionScope::EmbeddedFile:
        {
            if (m_filterEmbeddedFiles.type != CryptFilterType::None)
            {
                filter = m_filterEmbeddedFiles;
            }

            break;
        }
    }

    return filter;
}

QByteArray PDFStandardOrPublicSecurityHandler::encrypt(const QByteArray& data, PDFObjectReference reference, EncryptionScope encryptionScope) const
{
    CryptFilter filter = getCryptFilter(encryptionScope);
    return encryptUsingFilter(data, filter, reference);
}

QByteArray PDFStandardOrPublicSecurityHandler::encryptByFilter(const QByteArray& data, const QByteArray& filterName, PDFObjectReference reference) const
{
    if (filterName == IDENTITY_FILTER_NAME)
    {
        return data;
    }

    auto it = m_cryptFilters.find(filterName);
    if (it == m_cryptFilters.cend())
    {
        throw PDFException(PDFTranslationContext::tr("Crypt filter '%1' not found.").arg(QString::fromLatin1(filterName)));
    }

    return encryptUsingFilter(data, it->second, reference);
}

bool PDFStandardOrPublicSecurityHandler::isUnicodeNonAsciiSpaceCharacter(char32_t unicode)
{
    switch (unicode)
    {
        case 0x00A0:
        case 0x1680:
        case 0x2000:
        case 0x2001:
        case 0x2002:
        case 0x2003:
        case 0x2004:
        case 0x2005:
        case 0x2006:
        case 0x2007:
        case 0x2008:
        case 0x2009:
        case 0x200A:
        case 0x200B:
        case 0x202F:
        case 0x205F:
        case 0x3000:
            return true;

        default:
            return false;
    }
}

bool PDFStandardOrPublicSecurityHandler::isUnicodeMappedToNothing(char32_t unicode)
{
    // RFC 3454, table B.1 (R5-17: U+2060, U+FE00 - U+FE0F and U+FEFF too)
    switch (unicode)
    {
        case 0x00AD:
        case 0x034F:
        case 0x1806:
        case 0x180B:
        case 0x180C:
        case 0x180D:
        case 0x200B:
        case 0x200C:
        case 0x200D:
        case 0x2060:
        case 0xFEFF:
            return true;

        default:
            return unicode >= 0xFE00 && unicode <= 0xFE0F;
    }
}

bool PDFStandardOrPublicSecurityHandler::isUnicodeProhibited(char32_t unicode)
{
    // RFC 4013 2.3: the prohibited output of RFC 3454, tables C.1.2 - C.9
    if (isUnicodeNonAsciiSpaceCharacter(unicode))
    {
        return true;                                                    // C.1.2
    }

    if (unicode < 0x20 || unicode == 0x7F)
    {
        return true;                                                    // C.2.1
    }

    if ((unicode >= 0x80 && unicode <= 0x9F) ||                         // C.2.2
        unicode == 0x06DD || unicode == 0x070F || unicode == 0x180E || unicode == 0x200C || unicode == 0x200D ||
        unicode == 0x2028 || unicode == 0x2029 || (unicode >= 0x2060 && unicode <= 0x2063) || (unicode >= 0x206A && unicode <= 0x206F) ||
        unicode == 0xFEFF || (unicode >= 0xFFF9 && unicode <= 0xFFFC) || (unicode >= 0x1D173 && unicode <= 0x1D17A))
    {
        return true;
    }

    if ((unicode >= 0xE000 && unicode <= 0xF8FF) || (unicode >= 0xF0000 && unicode <= 0xFFFFD) || (unicode >= 0x100000 && unicode <= 0x10FFFD))
    {
        return true;                                                    // C.3
    }

    if ((unicode >= 0xFDD0 && unicode <= 0xFDEF) || (unicode & 0xFFFE) == 0xFFFE)
    {
        return true;                                                    // C.4 (U+nFFFE, U+nFFFF of every plane)
    }

    if (unicode >= 0xD800 && unicode <= 0xDFFF)
    {
        return true;                                                    // C.5 (a lone surrogate)
    }

    if ((unicode >= 0xFFF9 && unicode <= 0xFFFD) ||                     // C.6
        (unicode >= 0x2FF0 && unicode <= 0x2FFB) ||                     // C.7
        unicode == 0x0340 || unicode == 0x0341 || unicode == 0x200E || unicode == 0x200F ||
        (unicode >= 0x202A && unicode <= 0x202E) || (unicode >= 0x206A && unicode <= 0x206F))   // C.8
    {
        return true;
    }

    return unicode == 0xE0001 || (unicode >= 0xE0020 && unicode <= 0xE007F);  // C.9
}

QString PDFStandardOrPublicSecurityHandler::prepareSASLprep(const QString& password)
{
    // RFC 4013 Section 2.1, the mapping: table C.1.2 to a space, table B.1 to nothing; by the code
    // points (a character outside of the BMP is a surrogate pair)
    QString preparedPassword;
    preparedPassword.reserve(password.size());

    for (const char32_t codePoint : password.toUcs4())
    {
        if (isUnicodeMappedToNothing(codePoint))
        {
            continue;
        }

        if (isUnicodeNonAsciiSpaceCharacter(codePoint))
        {
            preparedPassword += QChar(QChar::Space);
        }
        else
        {
            preparedPassword += QString::fromUcs4(&codePoint, 1);
        }
    }

    // RFC 4013, Section 2.2, normalization to KC
    return preparedPassword.normalized(QString::NormalizationForm_KC);
}

QByteArray PDFStandardOrPublicSecurityHandler::adjustPassword(const QString& password, int revision)
{
    QByteArray result;

    switch (revision)
    {
        case 2:
        case 3:
        case 4:
        {
            // According to the PDF specification, convert string to PDFDocEncoding encoding
            result = PDFEncoding::convertToEncoding(password, PDFEncoding::Encoding::PDFDoc);
            break;
        }

        case 5:
        case 6:
        {
            // According to the PDF specification, use SASLprep profile for stringprep RFC 4013, please see these websites:
            //      - RFC 4013: https://tools.ietf.org/html/rfc4013 (SASLprep profile for stringprep algorithm)
            //      - RFC 3454: https://tools.ietf.org/html/rfc3454 (stringprep algorithm - preparation of internationalized strings)
            //
            // A reader maps and normalizes and accepts the rest (a file of another writer opens);
            // the prohibited output and the bidirectional rule are checked before an encryption
            // (isPasswordValidForRevision6, B93)
            QString preparedPassword = prepareSASLprep(password);

            // UTF-8, the first 127 bytes (7.6.4.3.3, Algorithm 2.A step a)
            QByteArray utf8 = preparedPassword.toUtf8();
            result = utf8.left(127);
            PDFSecureMemory::clear(utf8);
            PDFSecureMemory::clear(preparedPassword);
            break;
        }

        default:
        {
            // The password of a PKCS#12 is UTF-8 everywhere (B84; PDFCertificateManager)
            result = password.toUtf8();
            break;
        }
    }

    return result;
}

bool PDFStandardOrPublicSecurityHandler::isPasswordValidForRevision6(const QString& password, QString* invalidCharacters)
{
    QString preparedPassword = prepareSASLprep(password);
    const QList<uint> codePoints = preparedPassword.toUcs4();
    PDFSecureMemory::clear(preparedPassword);

    bool isValid = true;
    bool hasRightToLeft = false;
    bool hasLeftToRight = false;

    for (const uint codePoint : codePoints)
    {
        if (isUnicodeProhibited(codePoint))
        {
            isValid = false;
            const char32_t character = char32_t(codePoint);
            if (invalidCharacters && QChar::isPrint(character))
            {
                invalidCharacters->append(QString::fromUcs4(&character, 1));
            }
            else if (invalidCharacters)
            {
                invalidCharacters->append(QStringLiteral("U+%1").arg(codePoint, 4, 16, QChar('0')).toUpper());
            }
        }

        // RFC 3454 6: tables D.1 (RandALCat) and D.2 (LCat) by the bidirectional category
        const QChar::Direction direction = QChar::direction(char32_t(codePoint));
        hasRightToLeft = hasRightToLeft || direction == QChar::DirR || direction == QChar::DirAL;
        hasLeftToRight = hasLeftToRight || direction == QChar::DirL;
    }

    if (hasRightToLeft && !codePoints.isEmpty())
    {
        auto isRightToLeft = [](uint codePoint)
        {
            const QChar::Direction direction = QChar::direction(char32_t(codePoint));
            return direction == QChar::DirR || direction == QChar::DirAL;
        };

        // A right-to-left string has no left-to-right character and starts and ends by a
        // right-to-left one (RFC 3454 6, items 2 and 3)
        if (hasLeftToRight || !isRightToLeft(codePoints.front()) || !isRightToLeft(codePoints.back()))
        {
            isValid = false;
        }
    }

    return isValid;
}

PDFSecurityHandler* PDFStandardSecurityHandler::clone() const
{
    return new PDFStandardSecurityHandler(*this);
}

bool PDFStandardSecurityHandler::isAllowed(Permission permission) const
{
    // Table 22, bit 10: deprecated in PDF 2.0 - a reader acts as if the extraction for the
    // accessibility were always allowed (R5-14)
    if (permission == Permission::Accessibility)
    {
        return true;
    }

    return m_authorizationData.authorizationResult == AuthorizationResult::OwnerAuthorized || (m_permissions & static_cast<uint32_t>(permission));
}

int PDFStandardSecurityHandler::getKeyByteLength_r234() const
{
    // Algorithm 2, step f: n is always 5 for revision 2, /Length / 8 for revisions 3 and 4 (the
    // length was checked at the parsing: 40 - 128 bits)
    const int keyByteLength = (m_R == 2) ? 5 : m_keyLength / 8;
    if (keyByteLength < 5 || keyByteLength > MD5_DIGEST_LENGTH)
    {
        throw PDFException(PDFTranslationContext::tr("Encryption key length (%1) exceeded maximal value of %2.").arg(keyByteLength).arg(MD5_DIGEST_LENGTH));
    }
    return keyByteLength;
}

PDFSecurityHandler::AuthorizationResult PDFStandardSecurityHandler::authenticate(const std::function<QString(bool*)>& getPasswordCallback, bool authorizeOwnerOnly)
{
    QByteArray password;
    bool passwordObtained = true;

    // Clear the authorization data
    m_authorizationData = AuthorizationData();
    m_isEmbeddedFilesLocked = false;

    // The tried password is overwritten whenever the function ends (S5.1)
    struct PasswordGuard
    {
        QByteArray& password;
        ~PasswordGuard() { PDFSecureMemory::clear(password); }
    } passwordGuard{ password };

    while (passwordObtained)
    {
        switch (m_R)
        {
            case 2:
            case 3:
            case 4:
            {
                // Try to authorize by owner password
                {
                    QByteArray userPassword = createUserPasswordFromOwnerPassword(password);
                    QByteArray fileEncryptionKey = createFileEncryptionKey(userPassword);
                    QByteArray U = createEntryValueU_r234(fileEncryptionKey);

                    if (U == m_U)
                    {
                        // We have authorized owner access
                        m_authorizationData.authorizationResult = AuthorizationResult::OwnerAuthorized;
                        m_authorizationData.fileEncryptionKey = fileEncryptionKey;
                        return AuthorizationResult::OwnerAuthorized;
                    }
                }

                // Try to authorize user password
                if (!authorizeOwnerOnly)
                {
                    QByteArray fileEncryptionKey = createFileEncryptionKey(password);
                    QByteArray U = createEntryValueU_r234(fileEncryptionKey);

                    if (U == m_U)
                    {
                        // We have authorized user access
                        const AuthorizationResult authorizationResult = isNoDRM() ? AuthorizationResult::OwnerAuthorized : AuthorizationResult::UserAuthorized;
                        m_authorizationData.authorizationResult = authorizationResult;
                        m_authorizationData.fileEncryptionKey = fileEncryptionKey;
                        return authorizationResult;
                    }
                }

                break;
            }

            case 5:
            case 6:
            {
                UserOwnerData_r6 userData = parseParts(m_U);
                UserOwnerData_r6 ownerData = parseParts(m_O);

                auto createHash_r5 = [](const QByteArray& inputData)
                {
                    QByteArray result(SHA256_DIGEST_LENGTH, char(0));
                    SHA256(convertByteArrayToUcharPtr(inputData), inputData.size(), convertByteArrayToUcharPtr(result));
                    return result;
                };

                auto createHash_r56 = [this, &createHash_r5](const QByteArray& input, const QByteArray& inputPassword, bool useUserKey)
                {
                    return (m_R == 5) ? createHash_r5(input) : createHash_r6(input, inputPassword, useUserKey);
                };

                // Try to authorize owner password
                {
                    QByteArray inputData = password + ownerData.validationSalt + m_U;
                    QByteArray hash = createHash_r56(inputData, password, true);

                    if (hash == ownerData.hash)
                    {
                        // We have authorized owner access. Now we must calculate the owner encryption key

                        QByteArray fileEncryptionKeyInputData = password + ownerData.keySalt + m_U;
                        QByteArray fileEncryptionDecryptionKey = createHash_r56(fileEncryptionKeyInputData, password, true);

                        Q_ASSERT(fileEncryptionDecryptionKey.size() == 32);
                        AES_KEY key = { };
                        AES_set_decrypt_key(convertByteArrayToUcharPtr(fileEncryptionDecryptionKey), fileEncryptionDecryptionKey.size() * 8, &key);
                        unsigned char aesInitializationVector[AES_BLOCK_SIZE] = { };
                        m_authorizationData.fileEncryptionKey.resize(m_OE.size());
                        AES_cbc_encrypt(convertByteArrayToUcharPtr(m_OE), convertByteArrayToUcharPtr(m_authorizationData.fileEncryptionKey), m_OE.size(), &key, aesInitializationVector, AES_DECRYPT);

                        m_authorizationData.authorizationResult = AuthorizationResult::OwnerAuthorized;
                    }
                }

                // Try to authorize user password
                if (!m_authorizationData.isAuthorized() && !authorizeOwnerOnly)
                {
                    QByteArray inputData = password + userData.validationSalt;
                    QByteArray hash = createHash_r56(inputData, password, false);

                    if (hash == userData.hash)
                    {
                        QByteArray fileEncryptionKeyInputData = password + userData.keySalt;
                        QByteArray fileEncryptionDecryptionKey = createHash_r56(fileEncryptionKeyInputData, password, false);

                        Q_ASSERT(fileEncryptionDecryptionKey.size() == 32);
                        AES_KEY key = { };
                        AES_set_decrypt_key(convertByteArrayToUcharPtr(fileEncryptionDecryptionKey), fileEncryptionDecryptionKey.size() * 8, &key);
                        unsigned char aesInitializationVector[AES_BLOCK_SIZE] = { };
                        m_authorizationData.fileEncryptionKey.resize(m_UE.size());
                        AES_cbc_encrypt(convertByteArrayToUcharPtr(m_UE), convertByteArrayToUcharPtr(m_authorizationData.fileEncryptionKey), m_UE.size(), &key, aesInitializationVector, AES_DECRYPT);

                        // We have authorized user access
                        const AuthorizationResult authorizationResult = isNoDRM() ? AuthorizationResult::OwnerAuthorized : AuthorizationResult::UserAuthorized;
                        m_authorizationData.authorizationResult = authorizationResult;
                    }
                }

                // Stop, if we authorized the document usage
                if (m_authorizationData.isAuthorized())
                {
                    // According the PDF specification, we must also check, if flags are not manipulated.
                    Q_ASSERT(m_Perms.size() == AES_BLOCK_SIZE);
                    AES_KEY key = { };
                    AES_set_decrypt_key(convertByteArrayToUcharPtr(m_authorizationData.fileEncryptionKey), m_authorizationData.fileEncryptionKey.size() * 8, &key);
                    QByteArray decodedPerms(m_Perms.size(), char(0));
                    AES_ecb_encrypt(convertByteArrayToUcharPtr(m_Perms), convertByteArrayToUcharPtr(decodedPerms), &key, AES_DECRYPT);

                    // 1) Checks, if bytes 9, 10, 11 are 'a', 'd', 'b'
                    if (decodedPerms[9] != 'a' || decodedPerms[10] != 'd' || decodedPerms[11] != 'b')
                    {
                        throw PDFException(PDFTranslationContext::tr("Permissions entry in the Encryption dictionary is invalid."));
                    }

                    // 2) Verify, that bytes 0-3 are valid permissions entry
                    const uint32_t permissions = qFromLittleEndian<uint32_t>(decodedPerms.data());
                    if (permissions != m_permissions)
                    {
                        throw PDFException(PDFTranslationContext::tr("Security permissions are manipulated. Can't open the document."));
                    }

                    // 3) Verify, that byte 8 is 'T' or 'F' and is equal to EncryptMetadata entry
                    if (decodedPerms[8] != 'T' && decodedPerms[8] != 'F')
                    {
                        throw PDFException(PDFTranslationContext::tr("Security permissions are manipulated. Can't open the document."));
                    }
                    if ((decodedPerms[8] == 'T') != m_encryptMetadata)
                    {
                        throw PDFException(PDFTranslationContext::tr("Security permissions are manipulated. Can't open the document."));
                    }

                    return m_authorizationData.authorizationResult;
                }

                break;
            }

            default:
                return AuthorizationResult::Failed;
        }

        QString enteredPassword = getPasswordCallback(&passwordObtained);
        PDFSecureMemory::clear(password);
        password = adjustPassword(enteredPassword, m_R);
        PDFSecureMemory::clear(enteredPassword);
    }

    return AuthorizationResult::Canceled;
}

PDFObject PDFStandardSecurityHandler::createEncryptionDictionaryObject() const
{
    PDFObjectFactory factory;

    factory.beginDictionary();

    fillEncryptionDictionary(factory, false);

    factory.beginDictionaryItem("Filter");
    factory << WrapName("Standard");
    factory.endDictionaryItem();

    factory.beginDictionaryItem("R");
    factory << PDFInteger(m_R);
    factory.endDictionaryItem();

    factory.beginDictionaryItem("O");
    factory << WrapString(m_O);
    factory.endDictionaryItem();

    factory.beginDictionaryItem("U");
    factory << WrapString(m_U);
    factory.endDictionaryItem();

    if (m_R == 6)
    {
        factory.beginDictionaryItem("OE");
        factory << WrapString(m_OE);
        factory.endDictionaryItem();

        factory.beginDictionaryItem("UE");
        factory << WrapString(m_UE);
        factory.endDictionaryItem();
    }

    factory.beginDictionaryItem("P");
    factory << PDFInteger(int32_t(m_permissions));
    factory.endDictionaryItem();

    if (m_R == 6)
    {
        factory.beginDictionaryItem("Perms");
        factory << WrapString(m_Perms);
        factory.endDictionaryItem();
    }

    if (m_V == 4 || m_V == 5)
    {
        factory.beginDictionaryItem("EncryptMetadata");
        factory << m_encryptMetadata;
        factory.endDictionaryItem();
    }

    factory.endDictionary();

    return factory.takeObject();
}

QByteArray PDFStandardSecurityHandler::createFileEncryptionKey(const QByteArray& password) const
{
    QByteArray result;

    switch (m_R)
    {
        case 2:
        case 3:
        case 4:
        {
            std::array<uint8_t, 32> paddedPassword = createPaddedPassword32(password);
            uint32_t transformedPermissions = qToLittleEndian(m_permissions);

            MD5_CTX context = { };
            MD5_Init(&context);
            MD5_Update(&context, paddedPassword.data(), paddedPassword.size());
            MD5_Update(&context, m_O.constData(), m_O.size());
            MD5_Update(&context, &transformedPermissions, sizeof(transformedPermissions));
            MD5_Update(&context, m_ID.constData(), m_ID.size());

            if (!m_encryptMetadata)
            {
                constexpr uint32_t value = 0xFFFFFFFF;
                MD5_Update(&context, &value, sizeof(value));
            }

            std::array<uint8_t, MD5_DIGEST_LENGTH> fileEncryptionKey = { };
            MD5_Final(fileEncryptionKey.data(), &context);

            const int keyByteLength = getKeyByteLength_r234();

            if (m_R >= 3)
            {
                for (int i = 0; i < 50; ++i)
                {

                    MD5_Init(&context);
                    MD5_Update(&context, fileEncryptionKey.data(), keyByteLength);
                    MD5_Final(fileEncryptionKey.data(), &context);
                }
            }

            result.resize(keyByteLength);
            std::copy_n(fileEncryptionKey.cbegin(), keyByteLength, result.begin());
            PDFSecureMemory::clear(fileEncryptionKey.data(), fileEncryptionKey.size());
            PDFSecureMemory::clear(paddedPassword.data(), paddedPassword.size());
            break;
        }

        case 5:
        case 6:
        {
            // This function must not be called with revision 5/6
            Q_ASSERT(false);
            break;
        }

        default:
        {
            throw PDFException(PDFTranslationContext::tr("Revision %1 of standard security handler is not supported.").arg(m_R));
        }
    }

    return result;
}

QByteArray PDFStandardSecurityHandler::createEntryValueU_r234(const QByteArray& fileEncryptionKey) const
{
    QByteArray result;

    switch (m_R)
    {
        case 2:
        {
            RC4_KEY key = { };
            RC4_set_key(&key, fileEncryptionKey.size(), convertByteArrayToUcharPtr(fileEncryptionKey));

            result.resize(static_cast<int>(PDFPasswordPadding.size()));
            RC4(&key, PDFPasswordPadding.size(), PDFPasswordPadding.data(), convertByteArrayToUcharPtr(result));
            break;
        }

        case 3:
        case 4:
        {
            std::array<uint8_t, MD5_DIGEST_LENGTH> hash = { };

            MD5_CTX context = { };
            MD5_Init(&context);
            MD5_Update(&context, PDFPasswordPadding.data(), PDFPasswordPadding.size());
            MD5_Update(&context, m_ID.data(), m_ID.size());
            MD5_Final(hash.data(), &context);

            RC4_KEY key = { };
            RC4_set_key(&key, fileEncryptionKey.size(), convertByteArrayToUcharPtr(fileEncryptionKey));

            std::array<uint8_t, MD5_DIGEST_LENGTH> encryptedHash =  { };
            std::array<uint8_t, MD5_DIGEST_LENGTH> targetBuffer =  { };
            RC4(&key, hash.size(), hash.data(), encryptedHash.data());

            QByteArray transformedKey = fileEncryptionKey;
            for (int i = 1; i <= 19; ++i)
            {
                for (int j = 0, keySize = fileEncryptionKey.size(); j < keySize; ++j)
                {
                    transformedKey[j] = static_cast<uint8_t>(fileEncryptionKey[j]) ^ static_cast<uint8_t>(i);
                }

                RC4_set_key(&key, transformedKey.size(), convertByteArrayToUcharPtr(transformedKey));
                RC4(&key, encryptedHash.size(), encryptedHash.data(), targetBuffer.data());
                encryptedHash = targetBuffer;
            }

            // We do a hack here. In the PDF's specification, it is written, that arbitrary 16 bytes
            // are appended to the 16 bytes result. We use the last 16 bytes of the U entry, because we
            // want to compare byte arrays entirely (otherwise we must compare only 16 bytes to authenticate
            // user password).
            result = m_U;
            result.detach();

            if (result.size() != 32)
            {
                // In case of error, we resize it to correct size. We can't assume, that m_U has correct length.
                result.resize(32);
            }

            std::copy_n(encryptedHash.begin(), encryptedHash.size(), result.begin());
            break;
        }

        default:
        {
            throw PDFException(PDFTranslationContext::tr("Revision %1 of standard security handler is not supported.").arg(m_R));
        }
    }

    return result;
}

QByteArray PDFStandardSecurityHandler::createUserPasswordFromOwnerPassword(const QByteArray& password) const
{
    QByteArray result;

    std::array<uint8_t, 32> paddedPassword = createPaddedPassword32(password);
    std::array<uint8_t, MD5_DIGEST_LENGTH> hash;

    MD5_CTX context = { };
    MD5_Init(&context);
    MD5_Update(&context, paddedPassword.data(), paddedPassword.size());
    MD5_Final(hash.data(), &context);
    PDFSecureMemory::clear(paddedPassword.data(), paddedPassword.size());

    // Algorithm 3, step d: n is 5 for revision 2, /Length / 8 for revisions 3 and 4
    const int keyByteLength = getKeyByteLength_r234();

    if (m_R >= 3)
    {
        for (int i = 0; i < 50; ++i)
        {

            MD5_Init(&context);
            MD5_Update(&context, hash.data(), keyByteLength);
            MD5_Final(hash.data(), &context);
        }
    }

    switch (m_R)
    {
        case 2:
        {
            RC4_KEY key = { };
            RC4_set_key(&key, keyByteLength, hash.data());
            result.resize(m_O.size());
            RC4(&key, m_O.size(), convertByteArrayToUcharPtr(m_O), convertByteArrayToUcharPtr(result));
            break;
        }

        case 3:
        case 4:
        {
            QByteArray buffer = m_O;
            QByteArray transformedKey;
            transformedKey.resize(keyByteLength);
            std::copy_n(hash.data(), keyByteLength, transformedKey.data());

            for (int i = 19; i >= 0; --i)
            {
                for (int j = 0, keySize = transformedKey.size(); j < keySize; ++j)
                {
                    transformedKey[j] = static_cast<uint8_t>(hash[j]) ^ static_cast<uint8_t>(i);
                }

                RC4_KEY key = { };
                RC4_set_key(&key, transformedKey.size(), convertByteArrayToUcharPtr(transformedKey));
                RC4(&key, buffer.size(), convertByteArrayToUcharPtr(buffer), convertByteArrayToUcharPtr(buffer));
            }

            result = buffer;
            break;
        }

        default:
        {
            throw PDFException(PDFTranslationContext::tr("Revision %1 of standard security handler is not supported.").arg(m_R));
        }
    }

    return result;
}

std::array<uint8_t, 32> PDFStandardSecurityHandler::createPaddedPassword32(const QByteArray& password) const
{
    std::array<uint8_t, 32> result = { };

    int copiedBytes = qMin<int>(static_cast<int>(result.size()), password.size());
    auto it = result.begin();

    for (int i = 0; i < copiedBytes; ++i)
    {
        *it++ = static_cast<uint8_t>(password[i]);
    }

    auto itPadding = PDFPasswordPadding.cbegin();
    for (; it != result.cend();)
    {
        Q_ASSERT(itPadding != PDFPasswordPadding.cend());
        *it++ = *itPadding++;
    }

    return result;
}

QByteArray PDFStandardSecurityHandler::createHash_r6(const QByteArray& input, const QByteArray& inputPassword, bool useUserKey) const
{
    QByteArray result;

    // First compute sha-256 digest of the input
    std::array<uint8_t, SHA256_DIGEST_LENGTH> inputDigest = { };
    SHA256(convertByteArrayToUcharPtr(input), input.size(), inputDigest.data());
    std::vector<uint8_t> K(inputDigest.cbegin(), inputDigest.cend());

    // Fill the user key, if we use it
    std::vector<uint8_t> userKey;
    if (useUserKey)
    {
        userKey.resize(m_U.size());
        std::copy_n(m_U.constData(), m_U.size(), userKey.begin());
    }
    const size_t userKeySize = userKey.size();

    // Fill the input password
    std::vector<uint8_t> password(inputPassword.constData(), inputPassword.constData() + inputPassword.size());
    const size_t passwordSize = password.size();

    std::vector<uint8_t> K1;
    std::vector<uint8_t> E;

    int round = 0;
    while (round < 64 || round < E.back() + 32)
    {
        const size_t blockCount = 64;
        const size_t KSize = K.size();
        const size_t sequenceSize = passwordSize + KSize + userKeySize;
        const size_t totalSize = blockCount * sequenceSize;

        // Resize the arrays
        K1.resize(totalSize);
        E.resize(totalSize);

        // a) fill the input array K1 with data
        auto it = K1.begin();
        for (size_t i = 0; i < blockCount; ++i)
        {
            std::copy_n(password.cbegin(), passwordSize, it);
            std::advance(it, passwordSize);

            std::copy_n(K.cbegin(), KSize, it);
            std::advance(it, KSize);

            std::copy_n(userKey.cbegin(), userKeySize, it);
            std::advance(it, userKeySize);
        }
        Q_ASSERT(it == K1.cend());
        Q_ASSERT(K.size() >= 32);

        // b) encrypt K1 with AES-128 in CBC mode, first 16 bytes of K is key,
        //    second 16 bytes in K is initialization vector for AES algorithm.
        AES_KEY key = { };
        AES_set_encrypt_key(K.data(), 128, &key);
        AES_cbc_encrypt(K1.data(), E.data(), K1.size(), &key, K.data() + 16, AES_ENCRYPT);

        // c) we take first 16 bytes from E as unsigned 128 bit big-endian integer and compute
        //    remainder modulo 3. Then we decide which SHA function we will use.

        // We can't directly modulo 128 bit unsigned number, because we do not have 128 bit arithmetic (yet).
        // We will use following trick from https://math.stackexchange.com/questions/2727954/bit-representation-and-divisibility-by-3
        //
        //      2^n mod 3 = 2 for n = 1, 3, 5, 7, 9, ...
        //      2^n mod 3 = 1 for n = 0, 2, 4, 6, 8, ...
        //
        // Also, it doesn't matter the endianity of the numbers, becase for example, when we change endianity of 16 bit
        // numbers, then bits 0-7 became 8-15, so even/odd bits become also even/odd.

        int remainderAccumulator = 0;
        for (size_t i = 0; i < 16; ++i)
        {
            uint8_t byte = E[i];

            int currentRemainder = 1;
            for (uint8_t iRemainder = 0; iRemainder < 8; ++iRemainder)
            {
                if ((byte >> iRemainder) & 1)
                {
                    remainderAccumulator += currentRemainder;
                }

                // We alternate the remainder 1, 2, 1, 2, 1, 2, ...
                currentRemainder = 3 - currentRemainder;
            }
        }
        remainderAccumulator = remainderAccumulator % 3;

        // d) according to the remainder, decide, which function we will use
        switch (remainderAccumulator)
        {
            case 0:
            {
                K.resize(SHA256_DIGEST_LENGTH);
                SHA256(E.data(), E.size(), K.data());
                break;
            }

            case 1:
            {
                K.resize(SHA384_DIGEST_LENGTH);
                SHA384(E.data(), E.size(), K.data());
                break;
            }

            case 2:
            {
                K.resize(SHA512_DIGEST_LENGTH);
                SHA512(E.data(), E.size(), K.data());
                break;
            }

            default:
            {
                // Invalid value, can't occur
                Q_ASSERT(false);
                break;
            }
        }

        ++round;
    }

    Q_ASSERT(K.size() >= 32);

    // Clamp result to 32 bytes
    result.resize(32);
    std::copy_n(K.data(), 32, result.data());
    return result;
}

PDFStandardSecurityHandler::UserOwnerData_r6 PDFStandardSecurityHandler::parseParts(const QByteArray& data) const
{
    UserOwnerData_r6 result;
    Q_ASSERT(data.size() == 48);

    result.hash = data.left(32);
    result.validationSalt = data.mid(32, 8);
    result.keySalt = data.mid(40, 8);

    return result;
}

PDFSecurityHandlerPointer PDFSecurityHandlerFactory::createSecurityHandler(const SecuritySettings& settings)
{
    return createSecurityHandlerImpl(settings, nullptr);
}

PDFSecurityHandlerPointer PDFSecurityHandlerFactory::createSecurityHandlerImpl(const SecuritySettings& settings, QString* errorMessage)
{
    try
    {
        return createSecurityHandlerUnchecked(settings, errorMessage);
    }
    catch (const PDFException& exception)
    {
        // The cryptographic generator failed (S5.4: never a fallback) or a key cannot be computed
        if (errorMessage)
        {
            *errorMessage = exception.getMessage();
        }
        return nullptr;
    }
}

PDFSecurityHandlerPointer PDFSecurityHandlerFactory::createSecurityHandlerUnchecked(const SecuritySettings& settings, QString* errorMessage)
{
    auto setError = [errorMessage](const QString& text)
    {
        if (errorMessage)
        {
            *errorMessage = text;
        }
    };

    if (settings.algorithm == Algorithm::None)
    {
        return PDFSecurityHandlerPointer(new PDFNoneSecurityHandler);
    }

    switch (settings.algorithm)
    {
        case RC4:
        case AES_128:
        case AES_256:
        case Certificate:
            break;

        default:
            setError(tr("Unknown encryption algorithm."));
            return nullptr;
    }

    // Jakub Melka: create standard security or public key handler, with given settings. The
    // handler is owned by the shared pointer from the start (a failure or an exception of the
    // random generator never leaks it).
    PDFStandardSecurityHandler* standardHandler = nullptr;
    PDFPublicKeySecurityHandler* publicKeyHandler = nullptr;
    PDFStandardOrPublicSecurityHandler* handler = nullptr;
    PDFSecurityHandlerPointer handlerPointer;

    // The seed and the permissions of the envelope of a certificate of the system store (cleared by
    // authorizeByEnvelope, or by the guard on every other way out)
    QByteArray systemStoreEnvelope;
    struct EnvelopeGuard
    {
        QByteArray& data;
        ~EnvelopeGuard() { PDFSecureMemory::clear(data); }
    } envelopeGuard{ systemStoreEnvelope };

    if (settings.algorithm != Algorithm::Certificate)
    {
        standardHandler = new PDFStandardSecurityHandler();
        handler = standardHandler;
    }
    else
    {
        publicKeyHandler = new PDFPublicKeySecurityHandler();
        handler = publicKeyHandler;
    }
    handlerPointer.reset(handler);

    if (standardHandler)
    {
        standardHandler->m_ID = settings.id;
    }

    const bool isEncryptingEmbeddedFilesOnly = settings.encryptContents == EncryptContents::EmbeddedFiles;

    switch (settings.algorithm)
    {
        case RC4:
        {
            handler->m_V = 4;
            handler->m_keyLength = 128;

            CryptFilter defaultFilter;
            defaultFilter.type = CryptFilterType::V2;
            defaultFilter.authEvent = !isEncryptingEmbeddedFilesOnly ? AuthEvent::DocOpen : AuthEvent::EFOpen;
            defaultFilter.keyLength = handler->m_keyLength / 8;
            handler->m_filterDefault = defaultFilter;
            break;
        }

        case AES_128:
        {
            handler->m_V = 4;
            handler->m_keyLength = 128;

            CryptFilter defaultFilter;
            defaultFilter.type = CryptFilterType::AESV2;
            defaultFilter.authEvent = !isEncryptingEmbeddedFilesOnly ? AuthEvent::DocOpen : AuthEvent::EFOpen;
            defaultFilter.keyLength = handler->m_keyLength / 8;
            handler->m_filterDefault = defaultFilter;
            break;
        }

        case AES_256:
        case Certificate:
        {
            handler->m_V = 5;
            handler->m_keyLength = 256;

            CryptFilter defaultFilter;
            defaultFilter.type = CryptFilterType::AESV3;
            defaultFilter.authEvent = !isEncryptingEmbeddedFilesOnly ? AuthEvent::DocOpen : AuthEvent::EFOpen;
            defaultFilter.keyLength = handler->m_keyLength / 8;
            handler->m_filterDefault = defaultFilter;
            break;
        }

        default:
            Q_ASSERT(false);
            break;
    }

    CryptFilter identityFilter;
    identityFilter.type = CryptFilterType::Identity;

    if (standardHandler)
    {
        standardHandler->m_encryptMetadata = settings.encryptContents == All;
    }

    if (publicKeyHandler)
    {
        publicKeyHandler->m_filterDefault.encryptMetadata = settings.encryptContents == All;
        publicKeyHandler->m_pkcs7Type = PDFPublicKeySecurityHandler::PKCS7_Type::PKCS7_S5;

        // Table 24 (B90, Q5.3 b): bit 10 always, bit 2 with every permission
        publicKeyHandler->m_permissions = getPublicKeyPermissions(settings.permissions);

        // 7.6.5.3: the 20 bytes of the seed of the file encryption key (B82) and the permissions,
        // enveloped for the only recipient - the certificate of the user (R5-1: its PKCS#12)
        QByteArray seed = generateRandomBytes(20);
        const QByteArray recipient = createRecipient(settings.certificate, settings.userPassword, seed, publicKeyHandler->m_permissions, errorMessage);
        if (!recipient.isEmpty() && isSystemStoreCertificate(settings.certificate))
        {
            // Owner 2026-11-15 (step 6, addendum): a certificate of the system store is a recipient by
            // its public certificate only - its key is never used at the encryption (no PIN), the
            // handler is authorized by the decrypted form of its own envelope (the seed and the
            // permissions, 7.6.5.3) at the check below
            systemStoreEnvelope = seed;
            for (int i = 0; i < 4; ++i)
            {
                systemStoreEnvelope.append(char((publicKeyHandler->m_permissions >> (8 * i)) & 0xFF));
            }
        }
        PDFSecureMemory::clear(seed);
        if (recipient.isEmpty())
        {
            return nullptr;
        }
        publicKeyHandler->m_filterDefault.recipients << recipient;
    }

    handler->m_filterDefault.encryptMetadata = settings.encryptContents == All;

    switch (settings.encryptContents)
    {
        case All:
            handler->m_filterStrings = handler->m_filterDefault;
            handler->m_filterStreams = handler->m_filterDefault;
            handler->m_filterEmbeddedFiles = handler->m_filterDefault;
            break;

        case AllExceptMetadata:
            handler->m_filterStrings = handler->m_filterDefault;
            handler->m_filterStreams = handler->m_filterDefault;
            handler->m_filterEmbeddedFiles = handler->m_filterDefault;
            break;

        case EmbeddedFiles:
            handler->m_filterStrings = identityFilter;
            handler->m_filterStreams = identityFilter;
            handler->m_filterEmbeddedFiles = handler->m_filterDefault;
            break;

        default:
            Q_ASSERT(false);
            break;
    }

    if (standardHandler)
    {
        handler->m_cryptFilters["StdCF"] = handler->m_filterDefault;
    }
    if (publicKeyHandler)
    {
        if (settings.encryptContents != EmbeddedFiles)
        {
            handler->m_cryptFilters["DefaultCryptFilter"] = handler->m_filterDefault;
        }
        else
        {
            handler->m_cryptFilters["DefEmbeddedFile"] = handler->m_filterDefault;
        }
    }

    // The owner password of the handler: an empty owner password is the user password, for every
    // revision (ISO 32000-2, 7.6.4.4, Algorithm 3 step a for revisions 2 - 4; the same for revision
    // 6 as qpdf and Acrobat do, R5-2). Else an empty password would open the document as its owner,
    // the reader tries it first (B81).
    const QString& ownerPassword = settings.ownerPassword.isEmpty() ? settings.userPassword : settings.ownerPassword;

    if (standardHandler)
    {
        standardHandler->m_R = getRevisionFromAlgorithm(settings.algorithm);
        standardHandler->m_permissions = getStandardPermissions(settings.permissions);

        QByteArray adjustedOwnerPassword = handler->adjustPassword(ownerPassword, standardHandler->m_R);
        QByteArray adjustedUserPassword = handler->adjustPassword(settings.userPassword, standardHandler->m_R);

        struct PasswordsGuard
        {
            QByteArray& owner;
            QByteArray& user;
            ~PasswordsGuard() { PDFSecureMemory::clear(owner); PDFSecureMemory::clear(user); }
        } passwordsGuard{ adjustedOwnerPassword, adjustedUserPassword };

        // Generate encryption entries
        switch (standardHandler->m_R)
        {
            case 2:
            case 3:
            case 4:
            {
                // Trick for computing "O" entry for revisions 2,3,4: in O entry, there is stored
                // user password encrypted by owner password. Because RC4 cipher is symmetric, we
                // can store user password in "O" entry and then use standard function to retrieve
                // user password, which in fact will be encrypted user password.

                std::array<uint8_t, 32> paddedUserPasswordArray = standardHandler->createPaddedPassword32(adjustedUserPassword);
                QByteArray paddedUserPassword;
                paddedUserPassword.resize(int(paddedUserPasswordArray.size()));
                std::copy(paddedUserPasswordArray.cbegin(), paddedUserPasswordArray.cend(), paddedUserPassword.data());
                standardHandler->m_O = paddedUserPassword;
                QByteArray entryO = standardHandler->createUserPasswordFromOwnerPassword(adjustedOwnerPassword);
                standardHandler->m_O = entryO;
                Q_ASSERT(standardHandler->createUserPasswordFromOwnerPassword(adjustedOwnerPassword) == paddedUserPassword);

                // Algorithm 5, step e: 16 bytes of arbitrary padding follow the 16 bytes of /U
                standardHandler->m_U = generateRandomBytes(32);

                QByteArray fileEncryptionKey = standardHandler->createFileEncryptionKey(paddedUserPassword);
                QByteArray U = standardHandler->createEntryValueU_r234(fileEncryptionKey);
                standardHandler->m_U = U;
                PDFSecureMemory::clear(fileEncryptionKey);
                PDFSecureMemory::clear(paddedUserPassword);
                PDFSecureMemory::clear(paddedUserPasswordArray.data(), paddedUserPasswordArray.size());

                break;
            }

            case 6:
            {
                PDFStandardSecurityHandler::UserOwnerData_r6 userData;
                PDFStandardSecurityHandler::UserOwnerData_r6 ownerData;

                // Generate file encryption key (Algorithm 8 a, B82: the cryptographic generator)
                handler->m_authorizationData.fileEncryptionKey = generateRandomBytes(32);
                handler->m_authorizationData.authorizationResult = PDFSecurityHandler::AuthorizationResult::OwnerAuthorized;

                // Compute m_U entry
                userData.keySalt = generateRandomBytes(8);
                userData.validationSalt = generateRandomBytes(8);
                userData.hash = standardHandler->createHash_r6(adjustedUserPassword + userData.validationSalt, adjustedUserPassword, false);
                standardHandler->m_U = userData.hash + userData.validationSalt + userData.keySalt;

                // Compute m_UE entry
                QByteArray userFileEncryptionKeyInputData = adjustedUserPassword + userData.keySalt;
                QByteArray userFileEncryptionKey = standardHandler->createHash_r6(userFileEncryptionKeyInputData, adjustedUserPassword, false);

                Q_ASSERT(userFileEncryptionKey.size() == 32);
                AES_KEY userKey = { };
                AES_set_encrypt_key(convertByteArrayToUcharPtr(userFileEncryptionKey), userFileEncryptionKey.size() * 8, &userKey);
                unsigned char aesUserInitializationVector[AES_BLOCK_SIZE] = { };
                standardHandler->m_UE.resize(handler->m_authorizationData.fileEncryptionKey.size());
                unsigned char* userInputBuffer = convertByteArrayToUcharPtr(handler->m_authorizationData.fileEncryptionKey);
                unsigned char* userTargetBuffer = convertByteArrayToUcharPtr(standardHandler->m_UE);
                AES_cbc_encrypt(userInputBuffer, userTargetBuffer, standardHandler->m_UE.size(), &userKey, aesUserInitializationVector, AES_ENCRYPT);

                PDFSecureMemory::clear(userFileEncryptionKeyInputData);
                PDFSecureMemory::clear(userFileEncryptionKey);

                // Compute m_O entry
                ownerData.keySalt = generateRandomBytes(8);
                ownerData.validationSalt = generateRandomBytes(8);
                ownerData.hash = standardHandler->createHash_r6(adjustedOwnerPassword + ownerData.validationSalt + standardHandler->m_U, adjustedOwnerPassword, true);
                standardHandler->m_O = ownerData.hash + ownerData.validationSalt + ownerData.keySalt;

                // Compute m_OE entry
                QByteArray ownerFileEncryptionKeyInputData = adjustedOwnerPassword + ownerData.keySalt + standardHandler->m_U;
                QByteArray ownerFileEncryptionKey = standardHandler->createHash_r6(ownerFileEncryptionKeyInputData, adjustedOwnerPassword, true);

                AES_KEY ownerKey = { };
                AES_set_encrypt_key(convertByteArrayToUcharPtr(ownerFileEncryptionKey), ownerFileEncryptionKey.size() * 8, &ownerKey);
                unsigned char aesOwnerInitializationVector[AES_BLOCK_SIZE] = { };
                standardHandler->m_OE.resize(handler->m_authorizationData.fileEncryptionKey.size());
                unsigned char* ownerInputBuffer = convertByteArrayToUcharPtr(handler->m_authorizationData.fileEncryptionKey);
                unsigned char* ownerTargetBuffer = convertByteArrayToUcharPtr(standardHandler->m_OE);
                AES_cbc_encrypt(ownerInputBuffer, ownerTargetBuffer, standardHandler->m_OE.size(), &ownerKey, aesOwnerInitializationVector, AES_ENCRYPT);
                PDFSecureMemory::clear(ownerFileEncryptionKeyInputData);
                PDFSecureMemory::clear(ownerFileEncryptionKey);
                PDFSecureMemory::clear(&userKey, sizeof(userKey));
                PDFSecureMemory::clear(&ownerKey, sizeof(ownerKey));

                // Perms entry (Algorithm 10)
                const QByteArray permsRandomBytes = generateRandomBytes(4);
                standardHandler->m_Perms = QByteArray(AES_BLOCK_SIZE, char(0));
                unsigned char* permsData = convertByteArrayToUcharPtr(standardHandler->m_Perms);
                permsData[0] = standardHandler->m_permissions & 0xFF;
                permsData[1] = (standardHandler->m_permissions >> 8) & 0xFF;
                permsData[2] = (standardHandler->m_permissions >> 16) & 0xFF;
                permsData[3] = (standardHandler->m_permissions >> 24) & 0xFF;
                permsData[4] = 0xFF;
                permsData[5] = 0xFF;
                permsData[6] = 0xFF;
                permsData[7] = 0xFF;
                permsData[8] = standardHandler->m_encryptMetadata ? 'T' : 'F';
                permsData[9] = 'a';
                permsData[10] = 'd';
                permsData[11] = 'b';
                permsData[12] = uint8_t(permsRandomBytes[0]);
                permsData[13] = uint8_t(permsRandomBytes[1]);
                permsData[14] = uint8_t(permsRandomBytes[2]);
                permsData[15] = uint8_t(permsRandomBytes[3]);

                Q_ASSERT(standardHandler->m_Perms.size() == AES_BLOCK_SIZE);
                AES_KEY key = { };
                AES_set_encrypt_key(convertByteArrayToUcharPtr(handler->m_authorizationData.fileEncryptionKey), handler->m_authorizationData.fileEncryptionKey.size() * 8, &key);
                AES_ecb_encrypt(convertByteArrayToUcharPtr(standardHandler->m_Perms), convertByteArrayToUcharPtr(standardHandler->m_Perms), &key, AES_ENCRYPT);
                PDFSecureMemory::clear(&key, sizeof(key));

                break;
            }

            default:
            {
                Q_ASSERT(false);
                break;
            }
        }
    }

    // The handler knows the encryption dictionary it writes and the /ID of the document
    // (isSameEncryption: a file written by it is read back by its clone)
    auto rememberSource = [handler, &settings]()
    {
        handler->m_encryptionDictionary = handler->createEncryptionDictionaryObject();
        handler->m_encryptionDocumentId = settings.id;
    };

    // A certificate of the system store: authorized by the seed of its envelope (no private key)
    if (publicKeyHandler && !systemStoreEnvelope.isEmpty())
    {
        if (publicKeyHandler->authorizeByEnvelope(systemStoreEnvelope, settings.certificate.info))
        {
            rememberSource();
            return handlerPointer;
        }
        setError(tr("The encryption cannot be created."));
        return nullptr;
    }

    // The check: the handler is authorized as written - the owner password opens the standard
    // handler, the private key of the chosen certificate and its password open the public-key
    // handler (R5-1: by the PKCS#12 of the settings, never by the certificates of the directory)
    bool firstTry = true;
    const bool isPublicKeySecurity = settings.algorithm == Algorithm::Certificate;
    auto passwordCallback = [isPublicKeySecurity, &settings, &ownerPassword, &firstTry](bool* b) { *b = firstTry; firstTry = false; return !isPublicKeySecurity ? ownerPassword : settings.userPassword; };
    handler->authenticateByCertificates({ settings.certificate }, passwordCallback, !isPublicKeySecurity);
    if (handler->getAuthorizationResult() == PDFSecurityHandler::AuthorizationResult::OwnerAuthorized ||
        (isPublicKeySecurity && handler->getAuthorizationResult() == PDFSecurityHandler::AuthorizationResult::UserAuthorized))
    {
       rememberSource();
       return handlerPointer;
    }

    setError(isPublicKeySecurity ? tr("The encrypted document cannot be opened by the private key of the certificate.")
                                 : tr("The encrypted document cannot be opened by the owner password."));
    return nullptr;
}

int PDFSecurityHandlerFactory::getPasswordOptimalEntropy()
{
    return 128;
}

int PDFSecurityHandlerFactory::getPasswordEntropy(const QString& password, Algorithm algorithm)
{
    if (algorithm == None)
    {
        return 0;
    }

    QByteArray adjustedPassword = PDFStandardSecurityHandler::adjustPassword(password, getRevisionFromAlgorithm(algorithm));

    if (adjustedPassword.isEmpty())
    {
        return 0;
    }

    const int length = adjustedPassword.length();
    std::sort(adjustedPassword.begin(), adjustedPassword.end());

    int charCount = 0;
    char lastChar = adjustedPassword.front();
    PDFReal entropy = 0.0;

    for (int i = 0; i < length; ++i)
    {
        const char currentChar = adjustedPassword[i];

        if (currentChar == lastChar)
        {
            ++charCount;
        }
        else
        {
            const PDFReal probability = PDFReal(charCount) / PDFReal(length);
            entropy += -probability * std::log2(probability);

            charCount = 1;
            lastChar = currentChar;
        }
    }

    // Jakub Melka: last character
    const PDFReal probability = PDFReal(charCount) / PDFReal(length);
    entropy += -probability * std::log2(probability);

    return entropy * length;
}

int PDFSecurityHandlerFactory::getRevisionFromAlgorithm(Algorithm algorithm)
{
    switch (algorithm)
    {
        case None:
            return 0;

        case RC4:
            return 4;

        case AES_128:
            return 4;

        case AES_256:
            return 6;

        case Certificate:
            return 0;

        default:
            Q_ASSERT(false);
            break;
    }

    return 0;
}

QByteArray PDFSecurityHandlerFactory::generateRandomBytes(int size)
{
    if (size < 0 || size > MAXIMUM_RANDOM_BYTES)
    {
        throw PDFException(tr("Invalid number of random bytes requested: %1.").arg(size));
    }

    QByteArray bytes(size, Qt::Uninitialized);
    if (size > 0 && RAND_bytes(reinterpret_cast<unsigned char*>(bytes.data()), size) != 1)
    {
        // Never a fallback to a generator, which is not cryptographic (S5.4)
        PDFSecureMemory::clear(bytes);
        throw PDFException(tr("The cryptographic random number generator failed."));
    }
    return bytes;
}

PDFSecurityHandlerPointer PDFSecurityHandlerFactory::createSecurityHandler(const SecuritySettings& settings, QString* errorMessage)
{
    // B101: every failure has its reason; the rules of validate first
    QString validationError;
    if (!validate(settings, &validationError))
    {
        if (errorMessage)
        {
            *errorMessage = validationError;
        }
        return nullptr;
    }

    QString creationError;
    PDFSecurityHandlerPointer handler = createSecurityHandlerImpl(settings, &creationError);
    if (!handler && errorMessage)
    {
        *errorMessage = !creationError.isEmpty() ? creationError : tr("Failed to create security handler.");
    }
    return handler;
}

uint32_t PDFSecurityHandlerFactory::getStandardPermissions(uint32_t permissions)
{
    // The bits of Table 22, which a user sets (3, 4, 5, 6, 9, 10, 11, 12)
    constexpr uint32_t TABLE_22_BITS = 0x00000F3C;

    // Bit 10 always (ISO 32000-2, Table 22: deprecated in PDF 2.0, a writer sets it; K5.7);
    // bits 7, 8 and 13 - 32 set, bits 1, 2 cleared (PDF 1.7 Table 3.20, 7.6.4.2 NOTE; B89)
    constexpr uint32_t ACCESSIBILITY_BIT = uint32_t(PDFSecurityHandler::Permission::Accessibility);
    constexpr uint32_t RESERVED_SET_BITS = 0xFFFFF0C0;

    return (permissions & TABLE_22_BITS) | ACCESSIBILITY_BIT | RESERVED_SET_BITS;
}

uint32_t PDFSecurityHandlerFactory::getPublicKeyPermissions(uint32_t permissions)
{
    using Flag = PDFPublicKeySecurityHandler::PermissionFlag;

    // Table 24: bits 3, 4, 5, 6, 9, 11, 12 as in Table 22, bit 10 always (R5-15)
    constexpr uint32_t USER_BITS = Flag::PKSH_PrintLowResolution | Flag::PKSH_Modify | Flag::PKSH_CopyContent | Flag::PKSH_ModifyAnnotationsFillFormFields |
                                   Flag::PKSH_FillFormFields | Flag::PKSH_Assemble | Flag::PKSH_PrintHighResolution;

    uint32_t result = (permissions & USER_BITS) | Flag::PKSH_Accessibility;

    // Bit 2 (change of the encryption, every operation) only, when nothing is restricted (owner
    // answer Q5.3 b): the user, who encrypts for oneself, can remove the encryption later
    if ((permissions & USER_BITS) == USER_BITS)
    {
        result |= Flag::PKSH_Owner;
    }

    return result;
}

bool PDFSecurityHandlerFactory::isPasswordValid(const QString& password, Algorithm algorithm, QString* invalidCharacters)
{
    switch (algorithm)
    {
        case RC4:
        case AES_128:
        {
            QString characters;
            const bool isValid = PDFEncoding::canConvertToEncoding(password, PDFEncoding::Encoding::PDFDoc, &characters);
            if (!isValid && invalidCharacters)
            {
                invalidCharacters->append(characters);
            }
            return isValid;
        }

        case AES_256:
            return PDFStandardOrPublicSecurityHandler::isPasswordValidForRevision6(password, invalidCharacters);

        default:
            break;
    }

    return true;
}

QByteArray PDFSecurityHandlerFactory::createRecipient(const PDFCertificateEntry& certificate, const QString& password, const QByteArray& seed, uint32_t permissions, QString* errorMessage)
{
    auto setError = [errorMessage](const QString& text)
    {
        if (errorMessage)
        {
            *errorMessage = text;
        }
    };

    openssl_ptr<X509> x509(nullptr, X509_free);
    if (isSystemStoreCertificate(certificate))
    {
        // Q6.2 of step 6: a certificate of the system store - the recipient needs only its public
        // certificate (7.6.5.3), its key stays in the system and opens the document later
        const QByteArray certificateData = certificate.info.getCertificateData();
        const unsigned char* derData = convertByteArrayToUcharPtr(certificateData);
        x509.reset(d2i_X509(nullptr, &derData, long(certificateData.size())));
        if (!x509)
        {
            ERR_clear_error();
            setError(tr("The certificate is not valid."));
            return QByteArray();
        }
    }
    else
    {
        if (certificate.pkcs12.isEmpty())
        {
            // No PKCS#12 and no certificate of the system store (B96, Q5.4)
            setError(tr("The certificate has no private key file (.pfx) and cannot be used for the encryption."));
            return QByteArray();
        }

        // The password of a PKCS#12 is UTF-8 (B84); 6.3 of step 6: a hostile count of iterations is
        // refused before the password is tried
        QByteArray passwordUtf8 = password.toUtf8();
        const unsigned char* pkcs12Data = convertByteArrayToUcharPtr(certificate.pkcs12);
        openssl_ptr<PKCS12> pkcs12(d2i_PKCS12(nullptr, &pkcs12Data, long(certificate.pkcs12.size())), &PKCS12_free);
        if (!pkcs12 || !PDFCertificateManager::isPkcs12IterationCountAcceptable(certificate.pkcs12, passwordUtf8))
        {
            ERR_clear_error();
            PDFSecureMemory::clear(passwordUtf8);
            setError(tr("The certificate file is not a valid PKCS#12 file."));
            return QByteArray();
        }

        EVP_PKEY* keyPtr = nullptr;
        X509* certificatePtr = nullptr;
        STACK_OF(X509)* certificatesPtr = nullptr;
        const bool isParsed = PKCS12_parse(pkcs12.get(), passwordUtf8.isEmpty() ? nullptr : passwordUtf8.constData(), &keyPtr, &certificatePtr, &certificatesPtr) == 1;
        PDFSecureMemory::clear(passwordUtf8);

        openssl_ptr<EVP_PKEY> key(keyPtr, EVP_PKEY_free);
        x509.reset(certificatePtr);
        if (certificatesPtr)
        {
            // The chain of the issuers is not needed (the recipient is the certificate itself)
            sk_X509_pop_free(certificatesPtr, X509_free);
        }

        if (!isParsed || !x509)
        {
            ERR_clear_error();
            setError(tr("The certificate cannot be opened with this password."));
            return QByteArray();
        }
    }

    // 7.6.5.3: the enveloped data are the 20 bytes of the seed and the 4 bytes of the permissions
    // (little endian)
    QByteArray envelopedData = seed;
    for (int i = 0; i < 4; ++i)
    {
        envelopedData.append(char((permissions >> (8 * i)) & 0xFF));
    }

    openssl_ptr<BIO> dataBuffer(BIO_new_mem_buf(envelopedData.constData(), int(envelopedData.size())), BIO_free_all);
    openssl_ptr<STACK_OF(X509)> recipientCertificates(sk_X509_new_null(), sk_x509_free_impl);
    sk_X509_push(recipientCertificates.get(), x509.get());

    openssl_ptr<PKCS7> pkcs7(dataBuffer ? PKCS7_encrypt(recipientCertificates.get(), dataBuffer.get(), EVP_aes_256_cbc(), PKCS7_BINARY) : nullptr, PKCS7_free);
    PDFSecureMemory::clear(envelopedData);

    QByteArray recipient;
    if (pkcs7)
    {
        openssl_ptr<BIO> storedData(BIO_new(BIO_s_mem()), BIO_free_all);
        if (storedData && i2d_PKCS7_bio(storedData.get(), pkcs7.get()) == 1)
        {
            BUF_MEM* memoryBuffer = nullptr;
            BIO_get_mem_ptr(storedData.get(), &memoryBuffer);
            recipient = QByteArray(memoryBuffer->data, int(memoryBuffer->length));
        }
    }

    if (recipient.isEmpty())
    {
        ERR_clear_error();
        setError(tr("The document cannot be encrypted for the certificate (its key cannot encrypt)."));
    }

    return recipient;
}

bool PDFSecurityHandlerFactory::validate(const SecuritySettings& settings, QString* errorMessage)
{
    auto setError = [errorMessage](const QString& text)
    {
        // B94: the caller may not want the reason
        if (errorMessage)
        {
            *errorMessage = text;
        }
    };

    switch (settings.algorithm)
    {
        case pdf::PDFSecurityHandlerFactory::RC4:
        case pdf::PDFSecurityHandlerFactory::AES_128:
        case pdf::PDFSecurityHandlerFactory::AES_256:
        {
            // Revision 4: PDFDocEncoding; revision 6: SASLprep (RFC 4013 2.3, 2.4; B93, R5-17)
            QString invalidCharacters;
            if (!isPasswordValid(settings.userPassword, settings.algorithm, &invalidCharacters))
            {
                setError(invalidCharacters.isEmpty() ? tr("User password mixes left-to-right and right-to-left text in a way, which cannot be used.")
                                                     : tr("User password contains invalid characters: %1.").arg(invalidCharacters));
                return false;
            }

            invalidCharacters.clear();
            if (!isPasswordValid(settings.ownerPassword, settings.algorithm, &invalidCharacters))
            {
                setError(invalidCharacters.isEmpty() ? tr("Owner password mixes left-to-right and right-to-left text in a way, which cannot be used.")
                                                     : tr("Owner password contains invalid characters: %1.").arg(invalidCharacters));
                return false;
            }

            // Restrictions are kept only by an owner password of its own: an empty owner password
            // is the user password (B81, Algorithm 3 a), and with the owner password equal to the
            // user password every reader is the owner (7.6.4.4, Algorithm 7; Q5.1, K5.2, R5-23)
            // (compared as the algorithm uses them: the first 32 bytes of revision 4 are the password)
            const uint32_t allowed = getStandardPermissions(settings.permissions);
            const bool isRestricted = (allowed & 0x00000F3C) != 0x00000F3C;
            if (isRestricted)
            {
                const int revision = getRevisionFromAlgorithm(settings.algorithm);
                QByteArray owner = PDFStandardOrPublicSecurityHandler::adjustPassword(settings.ownerPassword, revision);
                QByteArray user = PDFStandardOrPublicSecurityHandler::adjustPassword(settings.userPassword, revision);
                const int effectiveBytes = revision == 6 ? 127 : 32;
                const bool isSamePassword = owner.left(effectiveBytes) == user.left(effectiveBytes);
                PDFSecureMemory::clear(owner);
                PDFSecureMemory::clear(user);

                if (settings.ownerPassword.isEmpty() || isSamePassword)
                {
                    setError(tr("Restrictions need an owner password, which differs from the open password."));
                    return false;
                }
            }

            break;
        }

        case pdf::PDFSecurityHandlerFactory::None:
            break;

        case pdf::PDFSecurityHandlerFactory::Certificate:
        {
            if (isSystemStoreCertificate(settings.certificate))
            {
                // Q6.2 of step 6: a certificate of the system store - only its public certificate is
                // needed (no password); its key must be able to receive the envelope (B99)
                if (!PDFCertificateManager::isUsableForEncryption(settings.certificate.info.getCertificateData()))
                {
                    setError(tr("The certificate cannot be used for encryption (its key usage has no key encipherment)."));
                    return false;
                }
                break;
            }

            if (settings.certificate.pkcs12.isEmpty())
            {
                // B96, Q5.4: a certificate without a PKCS#12, which is not of the system store
                setError(tr("The certificate has no private key file (.pfx) and cannot be used for the encryption."));
                return false;
            }

            if (!pdf::PDFCertificateManager::isCertificateValid(settings.certificate, settings.userPassword))
            {
                setError(tr("Invalid certificate or password."));
                return false;
            }

            break;
        }

        default:
            setError(tr("Unknown encryption algorithm."));
            return false;
    }

    return true;
}


PDFSecurityHandler* PDFPublicKeySecurityHandler::clone() const
{
    return new PDFPublicKeySecurityHandler(*this);
}

PDFSecurityHandler::AuthorizationResult PDFPublicKeySecurityHandler::authenticate(const std::function<QString (bool*)>& getPasswordCallback, bool authorizeOwnerOnly)
{
    // Clear the authorization data
    m_authorizationData = AuthorizationData();
    m_authorizedCertificate.reset();
    m_isEmbeddedFilesLocked = false;

    if (authorizeOwnerOnly)
    {
        return AuthorizationResult::Failed;
    }

    // The certificates of the user (the .pfx files, PDFCertificateManager; plan 4.5)
    return authenticateByCertificateEntries(PDFCertificateManager::getCertificates(), getPasswordCallback);
}

PDFSecurityHandler::AuthorizationResult PDFPublicKeySecurityHandler::authenticateByCertificateEntries(const PDFCertificateEntries& certificates,
                                                                                                    const std::function<QString(bool*)>& getPasswordCallback)
{
    m_authorizationData = AuthorizationData();
    m_authorizedCertificate.reset();
    m_isEmbeddedFilesLocked = false;

    // 7.6.5.3: SHA-1 for a key up to 160 bits (V 1 - 4), SHA-256 for 256 bits (V 5)
    if (!(m_keyLength == 256 || (m_keyLength >= 40 && m_keyLength <= 128 && m_keyLength % 8 == 0)))
    {
        return AuthorizationResult::Failed;
    }

    // A private key: the PKCS#12 of a .pfx file, or a certificate of the system store, whose key stays
    // in the system (Q6.2 of step 6 of phase 2; B96, Q5.4 of step 5 had none)
    bool hasPrivateKey = false;
    bool hasSystemCertificate = false;
    for (const PDFCertificateEntry& certificateEntry : certificates)
    {
        hasPrivateKey = hasPrivateKey || !certificateEntry.pkcs12.isEmpty();
        hasSystemCertificate = hasSystemCertificate || PDFSecurityHandlerFactory::isSystemStoreCertificate(certificateEntry);
    }

    if ((!hasPrivateKey && !hasSystemCertificate) || m_filterDefault.recipients.isEmpty())
    {
        return AuthorizationResult::Failed;
    }

    // The empty password first (a .pfx without a password), then the system store (no password; the
    // system may ask for a PIN), then the passwords of the callback - only for a .pfx
    bool passwordObtained = true;
    bool isSystemStoreTried = false;
    QString password;
    while (true)
    {
        QByteArray passwordUtf8 = adjustPassword(password, 0);
        PDFSecureMemory::clear(password);

        bool isAuthorized = false;
        for (const PDFCertificateEntry& certificateEntry : certificates)
        {
            if (!certificateEntry.pkcs12.isEmpty() && authenticateByPkcs12(certificateEntry.pkcs12, passwordUtf8))
            {
                isAuthorized = true;
                break;
            }
        }
        PDFSecureMemory::clear(passwordUtf8);

        for (auto it = certificates.cbegin(); !isAuthorized && !isSystemStoreTried && it != certificates.cend(); ++it)
        {
            isAuthorized = PDFSecurityHandlerFactory::isSystemStoreCertificate(*it) && authenticateBySystemStore(it->info.getCertificateData());
        }
        isSystemStoreTried = true;

        if (isAuthorized)
        {
            return m_authorizationData.authorizationResult;
        }

        if (!hasPrivateKey)
        {
            return AuthorizationResult::Failed;
        }

        password = getPasswordCallback(&passwordObtained);
        if (!passwordObtained)
        {
            PDFSecureMemory::clear(password);
            return AuthorizationResult::Canceled;
        }
    }
}

bool PDFSecurityHandlerFactory::isSystemStoreCertificate(const PDFCertificateEntry& entry)
{
    return entry.type == PDFCertificateEntry::EntryType::System && entry.pkcs12.isEmpty() && !entry.info.getCertificateData().isEmpty();
}

bool PDFPublicKeySecurityHandler::authenticateBySystemStore(const QByteArray& certificateData)
{
    // Q6.2: the system decrypts the envelope by the key of the certificate (only its recipient info)
    for (const QByteArray& recipient : m_filterDefault.recipients)
    {
        std::optional<QByteArray> decryptedData = PDFPersonalCertificateStore::decryptEnvelope(certificateData, recipient);
        if (decryptedData && authorizeByEnvelope(*decryptedData, PDFCertificateInfo::getCertificateInfo(certificateData)))
        {
            return true;
        }
    }
    return false;
}

bool PDFPublicKeySecurityHandler::authenticateByPkcs12(const QByteArray& pkcs12Data, const QByteArray& passwordUtf8)
{
    // 6.3, R6-28 of step 6: a hostile .pfx of millions of iterations would block the thread for
    // minutes - refused before its password is tried
    if (!PDFCertificateManager::isPkcs12IterationCountAcceptable(pkcs12Data, passwordUtf8))
    {
        return false;
    }

    // B102: the private key is in the PKCS#12 of the entry (entry.pkcs12), never in the DER of
    // its certificate
    const unsigned char* data = convertByteArrayToUcharPtr(pkcs12Data);
    openssl_ptr<PKCS12> pkcs12(d2i_PKCS12(nullptr, &data, long(pkcs12Data.size())), &PKCS12_free);
    if (!pkcs12)
    {
        ERR_clear_error();
        return false;
    }

    EVP_PKEY* keyPtr = nullptr;
    X509* certificatePtr = nullptr;
    STACK_OF(X509)* certificatesPtr = nullptr;
    const bool isParsed = PKCS12_parse(pkcs12.get(), passwordUtf8.isEmpty() ? nullptr : passwordUtf8.constData(), &keyPtr, &certificatePtr, &certificatesPtr) == 1;

    openssl_ptr<EVP_PKEY> key(keyPtr, EVP_PKEY_free);
    openssl_ptr<X509> certificate(certificatePtr, X509_free);
    if (certificatesPtr)
    {
        sk_X509_pop_free(certificatesPtr, X509_free);
    }

    if (!isParsed || !key || !certificate)
    {
        ERR_clear_error();
        return false;
    }

    for (const QByteArray& recipient : m_filterDefault.recipients)
    {
        const unsigned char* recipientData = convertByteArrayToUcharPtr(recipient);
        openssl_ptr<PKCS7> pkcs7(d2i_PKCS7(nullptr, &recipientData, long(recipient.size())), PKCS7_free);
        if (!pkcs7)
        {
            ERR_clear_error();
            continue;
        }

        // With the certificate given, only the recipient info of its issuer and serial number is
        // decrypted (no RSA operation for the recipients of other users)
        openssl_ptr<BIO> dataBuffer(BIO_new(BIO_s_mem()), BIO_free_all);
        if (!dataBuffer || PKCS7_decrypt(pkcs7.get(), key.get(), certificate.get(), dataBuffer.get(), PKCS7_BINARY) != 1)
        {
            ERR_clear_error();
            continue;
        }

        BUF_MEM* memoryBuffer = nullptr;
        BIO_get_mem_ptr(dataBuffer.get(), &memoryBuffer);

        // Acc. to chapter 7.6.5.3 - the decrypted data are the 20 bytes of the seed and 4 bytes
        // of the permissions; the public certificate of the recipient (its holder is "Opens for"),
        // the key is freed
        QByteArray decryptedData(memoryBuffer->data, int(memoryBuffer->length));
        PDFSecureMemory::clear(memoryBuffer->data, memoryBuffer->length);
        if (authorizeByEnvelope(decryptedData, PDFCertificateInfo::getCertificateInfo(certificate.get())))
        {
            return true;
        }
    }

    return false;
}

bool PDFPublicKeySecurityHandler::authorizeByEnvelope(QByteArray& decryptedData, std::optional<PDFCertificateInfo> certificate)
{
    // 7.6.5.3: the 20 bytes of the seed, then the 4 bytes of the permissions
    openssl_ptr<EVP_MD_CTX> context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (decryptedData.size() < 20 || !context || EVP_DigestInit_ex(context.get(), (m_keyLength == 256) ? EVP_sha256() : EVP_sha1(), nullptr) != 1)
    {
        PDFSecureMemory::clear(decryptedData);
        return false;
    }

    // 7.6.5.3 a)
    EVP_DigestUpdate(context.get(), decryptedData.constData(), 20);

    // 7.6.5.3 b)
    for (const QByteArray& recipientItem : m_filterDefault.recipients)
    {
        EVP_DigestUpdate(context.get(), recipientItem.constData(), size_t(recipientItem.size()));
    }

    // 7.6.5.3 c)
    if (!isMetadataEncrypted())
    {
        constexpr uint32_t value = 0xFFFFFFFF;
        EVP_DigestUpdate(context.get(), &value, sizeof(value));
    }

    std::array<unsigned char, EVP_MAX_MD_SIZE> digest = { };
    unsigned int digestSize = 0;
    EVP_DigestFinal_ex(context.get(), digest.data(), &digestSize);

    // The permissions of the envelope (Table 24). An envelope without them allows nothing:
    // /P of the encryption dictionary is not protected (R5-24, SECURITY_TODO 47)
    m_permissions = (decryptedData.size() >= 24) ? qFromLittleEndian<uint32_t>(decryptedData.constData() + 20) : 0;
    PDFSecureMemory::clear(decryptedData);

    const AuthorizationResult authorizationResult = isNoDRM() ? AuthorizationResult::OwnerAuthorized : AuthorizationResult::UserAuthorized;
    const int keyBytes = qMin(int(digestSize), m_keyLength / 8);
    m_authorizationData.fileEncryptionKey = QByteArray(reinterpret_cast<const char*>(digest.data()), keyBytes);
    m_authorizationData.authorizationResult = authorizationResult;
    PDFSecureMemory::clear(digest.data(), digest.size());

    m_authorizedCertificate = std::move(certificate);
    return true;
}

bool PDFPublicKeySecurityHandler::isMetadataEncrypted() const
{
    return m_filterDefault.encryptMetadata;
}

bool PDFPublicKeySecurityHandler::isAllowed(Permission permission) const
{
    // Table 24, bit 10: a reader acts as if the extraction for the accessibility were allowed
    // (R5-14, B90 - formerly bit 5 was read for it)
    if (permission == Permission::Accessibility)
    {
        return true;
    }

    if (m_pkcs7Type == PKCS7_Type::PKCS7_S3)
    {
        // Jakub Melka: for S3, default standard permissions applies
        return m_permissions & static_cast<uint32_t>(permission);
    }

    if (m_permissions & PKSH_Owner)
    {
        return true;
    }

    switch (permission)
    {
        case Permission::PrintLowResolution:
            return m_permissions & PKSH_PrintLowResolution;

        case Permission::Modify:
            return m_permissions & PKSH_Modify;

        case Permission::CopyContent:
            return m_permissions & PKSH_CopyContent;

        case Permission::ModifyInteractiveItems:
            return m_permissions & PKSH_ModifyAnnotationsFillFormFields;

        case Permission::ModifyFormFields:
            return m_permissions & PKSH_FillFormFields;

        case Permission::Assemble:
            return m_permissions & PKSH_Assemble;

        case Permission::PrintHighResolution:
            return m_permissions & PKSH_PrintHighResolution;

        default:
            break;
    }

    return false;
}

PDFObject PDFPublicKeySecurityHandler::createEncryptionDictionaryObject() const
{
    PDFObjectFactory factory;

    factory.beginDictionary();

    fillEncryptionDictionary(factory, true);

    factory.beginDictionaryItem("Filter");
    factory << WrapName("Adobe.PubSec");
    factory.endDictionaryItem();

    factory.beginDictionaryItem("SubFilter");
    factory << WrapName("adbe.pkcs7.s5");
    factory.endDictionaryItem();

    factory.beginDictionaryItem("P");
    factory << PDFInteger(int32_t(m_permissions));
    factory.endDictionaryItem();

    // Jakub Melka: 131105 is mysterious value set by Acrobat Pro
    // when using public key security
    factory.beginDictionaryItem("R");
    factory << PDFInteger(131105);
    factory.endDictionaryItem();

    factory.endDictionary();

    return factory.takeObject();
}

}   // namespace pdf

#if defined(PDF4QT_COMPILER_MINGW) || defined(PDF4QT_COMPILER_GCC)
#pragma GCC diagnostic pop
#endif

#if defined(PDF4QT_COMPILER_MSVC)
#pragma warning(pop)
#endif
