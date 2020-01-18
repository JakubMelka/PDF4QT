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

#include "pdffile.h"
#include "pdfdocument.h"
#include "pdfencoding.h"

namespace pdf
{

QString PDFFileSpecification::getPlatformFileName() const
{
    // UF has maximal precedence, because it is unicode string
    if (!m_UF.isEmpty())
    {
        return m_UF;
    }

    if (!m_F.isEmpty())
    {
        return QString::fromLatin1(m_F);
    }

#ifdef Q_OS_WIN
    for (const QByteArray& platformName : { m_DOS, m_Mac, m_Unix })
    {
        if (!platformName.isEmpty())
        {
            return QString::fromLatin1(platformName);
        }
    }
#endif

#ifdef Q_OS_UNIX
    for (const QByteArray& platformName : { m_Unix, m_Mac, m_DOS })
    {
        if (!platformName.isEmpty())
        {
            return QString::fromLatin1(platformName);
        }
    }
#endif

#ifdef Q_OS_MAC
    for (const QByteArray& platformName : { m_Mac, m_Unix, m_DOS })
    {
        if (!platformName.isEmpty())
        {
            return QString::fromLatin1(platformName);
        }
    }
#endif

    return QString();
}

const PDFEmbeddedFile* PDFFileSpecification::getPlatformFile() const
{
    if (m_embeddedFiles.count("UF"))
    {
        return &m_embeddedFiles.at("UF");
    }

    if (m_embeddedFiles.count("F"))
    {
        return &m_embeddedFiles.at("F");
    }

#ifdef Q_OS_WIN
    if (m_embeddedFiles.count("DOS"))
    {
        return &m_embeddedFiles.at("DOS");
    }
#endif

#ifdef Q_OS_UNIX
    if (m_embeddedFiles.count("Unix"))
    {
        return &m_embeddedFiles.at("Unix");
    }
#endif

#ifdef Q_OS_MAC
    if (m_embeddedFiles.count("Mac"))
    {
        return &m_embeddedFiles.at("Mac");
    }
#endif

    return nullptr;
}

PDFFileSpecification PDFFileSpecification::parse(const PDFDocument* document, PDFObject object)
{
    PDFFileSpecification result;
    object = document->getObject(object);

    if (object.isString())
    {
        result.m_UF = PDFEncoding::convertTextString(object.getString());
    }
    else if (object.isDictionary())
    {
        PDFDocumentDataLoaderDecorator loader(document);
        const PDFDictionary* dictionary = object.getDictionary();
        PDFObject collectionObject = dictionary->get("Cl");

        result.m_fileSystem = loader.readNameFromDictionary(dictionary, "FS");
        result.m_F = loader.readStringFromDictionary(dictionary, "F");
        result.m_UF = loader.readTextStringFromDictionary(dictionary, "UF", QString());
        result.m_DOS = loader.readStringFromDictionary(dictionary, "DOS");
        result.m_Mac = loader.readStringFromDictionary(dictionary, "Mac");
        result.m_Unix = loader.readStringFromDictionary(dictionary, "Unix");
        result.m_volatile = loader.readBooleanFromDictionary(dictionary, "V", false);
        result.m_description = loader.readTextStringFromDictionary(dictionary, "Desc", QString());
        result.m_collection = collectionObject.isReference() ? collectionObject.getReference() : PDFObjectReference();

        PDFObject embeddedFiles = document->getObject(dictionary->get("EF"));
        if (embeddedFiles.isDictionary())
        {
            const PDFDictionary* embeddedFilesDictionary = embeddedFiles.getDictionary();
            for (size_t i = 0; i < embeddedFilesDictionary->getCount(); ++i)
            {
                result.m_embeddedFiles[embeddedFilesDictionary->getKey(i)] = PDFEmbeddedFile::parse(document, embeddedFilesDictionary->getValue(i));
            }
        }
    }

    return result;
}

PDFEmbeddedFile PDFEmbeddedFile::parse(const PDFDocument* document, PDFObject object)
{
    PDFEmbeddedFile result;
    object = document->getObject(object);

    if (object.isStream())
    {
        const PDFStream* stream = object.getStream();
        const PDFDictionary* dictionary = stream->getDictionary();
        PDFDocumentDataLoaderDecorator loader(document);
        result.m_stream = object;
        result.m_subtype = loader.readNameFromDictionary(dictionary, "Subtype");

        const PDFObject& paramsObject = document->getObject(dictionary->get("Params"));
        if (paramsObject.isDictionary())
        {
            const PDFDictionary* paramsDictionary = paramsObject.getDictionary();
            auto getDateTime = [&loader, paramsDictionary](const char* name)
            {
                QByteArray ba = loader.readStringFromDictionary(paramsDictionary, name);
                if (!ba.isEmpty())
                {
                    return PDFEncoding::convertToDateTime(ba);
                }
                return QDateTime();
            };

            result.m_size = loader.readIntegerFromDictionary(paramsDictionary, "Size", -1);
            result.m_creationDate = getDateTime("CreationDate");
            result.m_modifiedDate = getDateTime("ModDate");
            result.m_checksum = loader.readStringFromDictionary(paramsDictionary, "CheckSum");
        }
    }

    return result;
}

}   // namespace pdf
