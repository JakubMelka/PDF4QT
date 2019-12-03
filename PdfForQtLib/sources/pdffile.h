//    Copyright (C) 2019 Jakub Melka
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

#ifndef PDFFILE_H
#define PDFFILE_H

#include "pdfobject.h"

#include <QDateTime>

namespace pdf
{
class PDFDocument;

class PDFEmbeddedFile
{
public:
    explicit PDFEmbeddedFile() = default;

    bool isValid() const { return m_stream.isStream(); }
    const QByteArray& getSubtype() const { return m_subtype; }
    PDFInteger getSize() const { return m_size; }
    const QDateTime& getCreationDate() const { return m_creationDate; }
    const QDateTime& getModifiedDate() const { return m_modifiedDate; }
    const QByteArray& getChecksum() const { return m_checksum; }
    const PDFStream* getStream() const { return m_stream.getStream(); }

    static PDFEmbeddedFile parse(const PDFDocument* document, PDFObject object);

private:
    PDFObject m_stream;
    QByteArray m_subtype;
    PDFInteger m_size = -1;
    QDateTime m_creationDate;
    QDateTime m_modifiedDate;
    QByteArray m_checksum;
};

/// File specification
class PDFFORQTLIBSHARED_EXPORT PDFFileSpecification
{
public:
    explicit PDFFileSpecification() = default;

    /// Returns platform file name as string. It looks into the UF, F,
    /// and platform names and selects the appropriate one. If error
    /// occurs. then empty string is returned.
    QString getPlatformFileName() const;

    /// Returns platform file.
    const PDFEmbeddedFile* getPlatformFile() const;

    const QByteArray& getFileSystem() const { return m_fileSystem; }
    const QByteArray& getF() const { return m_F; }
    const QString& getUF() const { return m_UF; }
    const QByteArray& getDOS() const { return m_DOS; }
    const QByteArray& getMac() const { return m_Mac; }
    const QByteArray& getUnix() const { return m_Unix; }
    bool isVolatile() const { return m_volatile; }
    const QString& getDescription() const { return m_description; }
    PDFObjectReference getCollection() const { return m_collection; }
    const std::map<QByteArray, PDFEmbeddedFile>& getEmbeddedFiles() const { return m_embeddedFiles; }

    static PDFFileSpecification parse(const PDFDocument* document, PDFObject object);

private:
    /// Name of the file system used to interpret this file specification,
    /// usually, it is URL (this is only file system defined in PDF specification 1.7).
    QByteArray m_fileSystem;

    /// File specification string (for backward compatibility). If file system is URL,
    /// it contains unified resource locator.
    QByteArray m_F;

    /// File specification string as unicode.
    QString m_UF;

    QByteArray m_DOS;
    QByteArray m_Mac;
    QByteArray m_Unix;

    /// Is file volatile? I.e it is, for example, link to a video file from online camera?
    /// If this boolean is true, then file should never be cached.
    bool m_volatile = false;

    /// Description of the file (for example, if file is embedded file stream)
    QString m_description;

    /// Collection item dictionary reference
    PDFObjectReference m_collection;

    /// Embedded files
    std::map<QByteArray, PDFEmbeddedFile> m_embeddedFiles;
};

}   // namespace pdf

#endif // PDFFILE_H
