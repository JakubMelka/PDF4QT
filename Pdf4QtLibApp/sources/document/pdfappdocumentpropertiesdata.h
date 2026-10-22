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

#ifndef PDFAPPDOCUMENTPROPERTIESDATA_H
#define PDFAPPDOCUMENTPROPERTIESDATA_H

#include "pdfappglobal.h"

#include "pdfdocument.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QString>
#include <QStringList>

#include <atomic>
#include <memory>
#include <optional>
#include <vector>

namespace pdf
{

/// One row of a group of the properties (Property row 206:873)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppPropertyItem
{
public:
    QString key;            ///< Translated name of the property
    QString value;          ///< Text of the value (empty values are shown as a dash)
    QByteArray infoKey;     ///< Key of the Info dictionary of an editable property (empty = read only)
    QString objectName;     ///< Object name of the row (tests, "property-title")
};

/// Group of the properties (a title and a card of rows)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppPropertyGroup
{
public:
    QString id;             ///< Identifier of the group ("description", "content", ...)
    QString title;          ///< Translated title
    std::vector<PDFAppPropertyItem> items;
};

/// Permission of an encrypted document (Security 236:9065)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppPermissionItem
{
public:
    QString text;
    bool isAllowed = true;
};

/// Font used by a document (Fonts 236:9258, PDFFontDelegate)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppFontInfo
{
public:
    QString name;           ///< Name of the font without the prefix of a subset ("Inter-Regular")
    QString family;         ///< Family of the font descriptor (empty = unknown)
    QString type;           ///< Translated type ("TrueType", "Type 0 (CID keyed)", ...)
    bool isEmbedded = false;
    bool isSubset = false;

    /// Translated state ("Embedded subset", "Embedded", "Not embedded")
    QString getStatusText() const;
};

/// Data of the page Document properties (/document/{id}/properties/{section}):
/// the groups of the sections read from an immutable document, the changes of
/// the Info dictionary and of the XMP metadata (each a new document for
/// PDFAppDocument::commit) and the text of Copy all. Everything is defensive: the
/// document is an untrusted input (any value may be missing or damaged, texts are
/// bounded, no function throws).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentPropertiesData
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppDocumentPropertiesData)

public:
    PDFAppDocumentPropertiesData() = delete;

    /// Longest text of a value, which is shown (a longer value is shortened)
    static constexpr int MAXIMUM_VALUE_LENGTH = 4096;

    /// Largest XMP packet, which is shown and accepted (bytes)
    static constexpr qsizetype MAXIMUM_METADATA_SIZE = 16 * 1024 * 1024;

    /// Most custom properties shown in the group Custom properties
    static constexpr int MAXIMUM_CUSTOM_PROPERTIES = 500;

    /// Editable keys of the Info dictionary of the group Description
    static QList<QByteArray> getEditableInfoKeys();

    /// Groups of the section Properties: Description, Content, File, Custom
    /// properties (the last only when the document has some)
    /// \param document Document (nullptr = no groups)
    /// \param filePath Canonical path of the file (empty for a document without a file)
    /// \param title Title of the document tab (the name of a document without a file)
    /// \param lastRead Time of the last opening of the file (ms since the epoch, 0 = unknown)
    static std::vector<PDFAppPropertyGroup> createGeneralGroups(const PDFDocument* document, const QString& filePath, const QString& title, qint64 lastRead);

    /// Group Encryption of the section Security
    static PDFAppPropertyGroup createEncryptionGroup(const PDFDocument* document);

    /// Note below the group Encryption (empty = no note): the restrictions of a
    /// document opened with the user password
    static QString getSecurityNote(const PDFDocument* document);

    /// The 8 permissions of the section Security
    static std::vector<PDFAppPermissionItem> createPermissions(const PDFDocument* document);

    /// Groups of the section Display and print: Viewer settings, Default print settings
    static std::vector<PDFAppPropertyGroup> createDisplayGroups(const PDFDocument* document);

    /// XMP metadata packet of the document as a text. \p hasMetadata is set to
    /// false, when the document has no packet; \p errorMessage gets the reason,
    /// when a packet exists, but it cannot be shown (too large, damaged stream).
    static QString getMetadata(const PDFDocument* document, bool* hasMetadata, QString* errorMessage);

    /// Default XMP packet made from the Info dictionary of the document
    static QByteArray createDefaultMetadata(const PDFDocument* document, const QDateTime& now);

    /// Checks, that the text is a well-formed XML (bounded size, no DTD). \returns
    /// false and a translated message with the line of the error otherwise.
    static bool validateMetadata(const QByteArray& xml, QString* errorMessage);

    /// New document with the value of the key of the Info dictionary (an empty
    /// value removes the key). \returns nullptr, when nothing changes or the
    /// change is not possible (no document, invalid key).
    static PDFDocumentPointer setInfoValue(const PDFDocument* document, const QByteArray& key, const QString& value);

    /// New document with the XMP packet (the stream /Metadata of the catalog).
    /// \returns nullptr, when nothing changes or the packet is refused.
    static PDFDocumentPointer setMetadata(const PDFDocument* document, const QByteArray& xml);

    /// Text of Copy all: the groups as "Title" lines and "Key: Value" lines
    static QString createCopyText(const QString& heading, const std::vector<PDFAppPropertyGroup>& groups);

    /// Text of a value of a date (empty for an invalid date)
    static QString formatDateTime(const QDateTime& dateTime);

    /// Text shown for an empty value
    static QString getEmptyValueText();

    /// Translated type of the font
    static QString getFontTypeText(int fontType);

    /// Bounded text of a value (control characters replaced, at most MAXIMUM_VALUE_LENGTH)
    static QString sanitize(const QString& text);
};

/// Scan of the fonts used by a document (worker thread, QtConcurrent): the
/// fonts of the resources of the pages and of their form XObjects (nested up to
/// MAXIMUM_DEPTH), each font once. The task is a functor (no lambda); it checks
/// the cancellation flag before each page.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentFontScanTask
{
public:
    /// Deepest nesting of form XObjects, which is scanned
    static constexpr int MAXIMUM_DEPTH = 16;

    /// Most fonts reported (a damaged document can have millions of entries)
    static constexpr int MAXIMUM_FONTS = 10000;

    explicit PDFAppDocumentFontScanTask(PDFDocumentPointer document, std::shared_ptr<std::atomic_bool> cancel);

    /// Runs the scan (any thread). \returns the fonts sorted by their names
    /// (an empty list, when it has been canceled)
    std::vector<PDFAppFontInfo> operator()() const;

    /// Scans the document in the calling thread
    static std::vector<PDFAppFontInfo> scan(const PDFDocument* document, const std::atomic_bool* cancel);

private:
    PDFDocumentPointer m_document;
    std::shared_ptr<std::atomic_bool> m_cancel;
};

}   // namespace pdf

#endif // PDFAPPDOCUMENTPROPERTIESDATA_H
