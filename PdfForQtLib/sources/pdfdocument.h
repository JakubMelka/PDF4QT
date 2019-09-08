//    Copyright (C) 2018-2019 Jakub Melka
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


#ifndef PDFDOCUMENT_H
#define PDFDOCUMENT_H

#include "pdfglobal.h"
#include "pdfobject.h"
#include "pdfcatalog.h"
#include "pdfsecurityhandler.h"

#include <QtCore>
#include <QMatrix>
#include <QDateTime>

namespace pdf
{
class PDFDocument;

/// Storage for objects. This class is not thread safe for writing (calling non-const functions). Caller must ensure
/// locking, if this object is used from multiple threads. Calling const functions should be thread safe.
class PDFObjectStorage
{
public:
    constexpr inline PDFObjectStorage() = default;

    constexpr inline PDFObjectStorage(const PDFObjectStorage&) = default;
    constexpr inline PDFObjectStorage(PDFObjectStorage&&) = default;

    constexpr inline PDFObjectStorage& operator=(const PDFObjectStorage&) = default;
    constexpr inline PDFObjectStorage& operator=(PDFObjectStorage&&) = default;

    struct Entry
    {
        constexpr inline explicit Entry() = default;
        inline explicit Entry(PDFInteger generation, PDFObject object) : generation(generation), object(std::move(object)) { }

        PDFInteger generation = 0;
        PDFObject object;
    };

    using PDFObjects = std::vector<Entry>;

    explicit PDFObjectStorage(PDFObjects&& objects, PDFObject&& trailerDictionary, PDFSecurityHandlerPointer&& securityHandler) :
        m_objects(std::move(objects)),
        m_trailerDictionary(std::move(trailerDictionary)),
        m_securityHandler(std::move(securityHandler))
    {

    }

    /// Returns object from the object storage. If invalid reference is passed,
    /// then null object is returned (no exception is thrown).
    const PDFObject& getObject(PDFObjectReference reference) const;

    /// Returns array of objects stored in this storage
    const PDFObjects& getObjects() const { return m_objects; }

    /// Returns trailer dictionary
    const PDFObject& getTrailerDictionary() const { return m_trailerDictionary; }

    /// Returns security handler associated with these objects
    const PDFSecurityHandler* getSecurityHandler() const { return m_securityHandler.data(); }

private:
    PDFObjects m_objects;
    PDFObject m_trailerDictionary;
    PDFSecurityHandlerPointer m_securityHandler;
};

/// Loads data from the object contained in the PDF document, such as integers,
/// bools, ... This object has two sets of functions - first one with default values,
/// then if object with valid data is not found, default value is used, and second one,
/// without default value, if valid data are not found, then exception is thrown.
/// This class uses Decorator design pattern.
class PDFDocumentDataLoaderDecorator
{
public:
    inline explicit PDFDocumentDataLoaderDecorator(const PDFDocument* document) : m_document(document) { }
    inline ~PDFDocumentDataLoaderDecorator() = default;

    /// Reads a name from the object, if it is possible. If object is not a name,
    /// then empty byte array is returned.
    /// \param object Object, can be an indirect reference to object (it is dereferenced)
    QByteArray readName(const PDFObject& object);

    /// Reads a string from the object, if it is possible. If object is not a string,
    /// then empty byte array is returned.
    /// \param object Object, can be an indirect reference to object (it is dereferenced)
    QByteArray readString(const PDFObject& object);

    /// Reads an integer from the object, if it is possible.
    /// \param object Object, can be an indirect reference to object (it is dereferenced)
    /// \param defaultValue Default value
    PDFInteger readInteger(const PDFObject& object, PDFInteger defaultValue) const;

    /// Reads a real number from the object, if it is possible. If integer appears as object,
    /// then it is converted to real number.
    /// \param object Object, can be an indirect reference to object (it is dereferenced)
    /// \param defaultValue Default value
    PDFReal readNumber(const PDFObject& object, PDFReal defaultValue) const;

    /// Reads a boolean from the object, if it is possible.
    /// \param object Object, can be an indirect reference to object (it is dereferenced)
    /// \param defaultValue Default value
    bool readBoolean(const PDFObject& object, bool defaultValue) const;

    /// Reads a text string from the object, if it is possible.
    /// \param object Object, can be an indirect reference to object (it is dereferenced)
    /// \param defaultValue Default value
    QString readTextString(const PDFObject& object, const QString& defaultValue) const;

    /// Reads a rectangle from the object, if it is possible.
    /// \param object Object, can be an indirect reference to object (it is dereferenced)
    /// \param defaultValue Default value
    QRectF readRectangle(const PDFObject& object, const QRectF& defaultValue) const;

    /// Reads enum from name object, if it is possible.
    /// \param object Object, can be an indirect reference to object (it is dereferenced)
    /// \param begin Begin of the enum search array
    /// \param end End of the enum search array
    /// \param default value Default value
    template<typename Enum, typename Iterator>
    Enum readEnumByName(const PDFObject& object, Iterator begin, Iterator end, Enum defaultValue) const
    {
        const PDFObject& dereferencedObject = m_document->getObject(object);
        if (dereferencedObject.isName())
        {
            QByteArray name = dereferencedObject.getString();

            for (Iterator it = begin; it != end; ++it)
            {
                if (name == (*it).first)
                {
                    return (*it).second;
                }
            }
        }

        return defaultValue;
    }

    /// Tries to read array of real values. Reads as much values as possible.
    /// If array size differs, then nothing happens.
    /// \param object Array of integers
    /// \param first First iterator
    /// \param second Second iterator
    template<typename T>
    void readNumberArray(const PDFObject& object, T first, T last)
    {
        const PDFObject& dereferencedObject = m_document->getObject(object);
        if (dereferencedObject.isArray())
        {
            const PDFArray* array = dereferencedObject.getArray();

            size_t distance = std::distance(first, last);
            if (array->getCount() == distance)
            {
                T it = first;
                for (size_t i = 0; i < distance; ++i)
                {
                    *it = readNumber(array->getItem(i), *it);
                    ++it;
                }
            }
        }
    }

    /// Tries to read array of real values from dictionary. Reads as much values as possible.
    /// If array size differs, or entry dictionary doesn't exist, then nothing happens.
    /// \param dictionary Dictionary with desired values
    /// \param key Entry key
    /// \param first First iterator
    /// \param second Second iterator
    template<typename T>
    void readNumberArrayFromDictionary(const PDFDictionary* dictionary, const char* key, T first, T last)
    {
        if (dictionary->hasKey(key))
        {
            readNumberArray(dictionary->get(key), first, last);
        }
    }

    /// Tries to read matrix from the dictionary. If matrix entry is not present, default value is returned.
    /// If it is present and invalid, exception is thrown.
    QMatrix readMatrixFromDictionary(const PDFDictionary* dictionary, const char* key, QMatrix defaultValue);

    /// Tries to read array of real values from dictionary. If entry dictionary doesn't exist,
    /// or error occurs, default value is returned.
    std::vector<PDFReal> readNumberArrayFromDictionary(const PDFDictionary* dictionary, const char* key, std::vector<PDFReal> defaultValue = std::vector<PDFReal>());

    /// Tries to read array of integer values from dictionary. If entry dictionary doesn't exist,
    /// or error occurs, empty array is returned.
    std::vector<PDFInteger> readIntegerArrayFromDictionary(const PDFDictionary* dictionary, const char* key);

    /// Reads number from dictionary. If dictionary entry doesn't exist, or error occurs, default value is returned.
    /// \param dictionary Dictionary containing desired data
    /// \param key Entry key
    /// \param defaultValue Default value
    PDFReal readNumberFromDictionary(const PDFDictionary* dictionary, const char* key, PDFReal defaultValue) const;

    /// Reads number from dictionary. If dictionary entry doesn't exist, or error occurs, default value is returned.
    /// \param dictionary Dictionary containing desired data
    /// \param key Entry key
    /// \param defaultValue Default value
    PDFReal readNumberFromDictionary(const PDFDictionary* dictionary, const QByteArray& key, PDFReal defaultValue) const;

    /// Reads integer from dictionary. If dictionary entry doesn't exist, or error occurs, default value is returned.
    /// \param dictionary Dictionary containing desired data
    /// \param key Entry key
    /// \param defaultValue Default value
    PDFInteger readIntegerFromDictionary(const PDFDictionary* dictionary, const char* key, PDFInteger defaultValue) const;

    /// Reads a text string from the dictionary, if it is possible.
    /// \param dictionary Dictionary containing desired data
    /// \param key Entry key
    /// \param defaultValue Default value
    QString readTextStringFromDictionary(const PDFDictionary* dictionary, const char* key, const QString& defaultValue) const;

    /// Tries to read array of references from dictionary. If entry dictionary doesn't exist,
    /// or error occurs, empty array is returned.
    std::vector<PDFObjectReference> readReferenceArrayFromDictionary(const PDFDictionary* dictionary, const char* key);

    /// Reads number array from dictionary. Reads all values. If some value is not
    /// real number (or integer number), default value is returned. Default value is also returned,
    /// if \p object is invalid.
    /// \param object Object containing array of numbers
    std::vector<PDFReal> readNumberArray(const PDFObject& object, std::vector<PDFReal> defaultValue = std::vector<PDFReal>()) const;

    /// Reads integer array from dictionary. Reads all values. If some value is not
    /// integer number, empty array is returned. Empty array is also returned,
    /// if \p object is invalid.
    /// \param object Object containing array of numbers
    std::vector<PDFInteger> readIntegerArray(const PDFObject& object) const;

    /// Reads reference array from dictionary. Reads all values. If error occurs,
    /// then empty array is returned.
    /// \param object Object containing array of references
    std::vector<PDFObjectReference> readReferenceArray(const PDFObject& object) const;

    /// Reads name array. Reads all values. If error occurs,
    /// then empty array is returned.
    /// \param object Object containing array of references
    std::vector<QByteArray> readNameArray(const PDFObject& object) const;

    /// Reads name array from dictionary. Reads all values. If error occurs,
    /// then empty array is returned.
    /// \param dictionary Dictionary containing desired data
    /// \param key Entry key
    std::vector<QByteArray> readNameArrayFromDictionary(const PDFDictionary* dictionary, const char* key) const;

    /// Reads boolean from dictionary. If dictionary entry doesn't exist, or error occurs, default value is returned.
    /// \param dictionary Dictionary containing desired data
    /// \param key Entry key
    /// \param defaultValue Default value
    bool readBooleanFromDictionary(const PDFDictionary* dictionary, const char* key, bool defaultValue) const;

    /// Reads a name from dictionary. If dictionary entry doesn't exist, or error occurs, empty byte array is returned.
    /// \param dictionary Dictionary containing desired data
    /// \param key Entry key
    QByteArray readNameFromDictionary(const PDFDictionary* dictionary, const char* key);

    /// Reads a string from dictionary. If dictionary entry doesn't exist, or error occurs, empty byte array is returned.
    /// \param dictionary Dictionary containing desired data
    /// \param key Entry key
    QByteArray readStringFromDictionary(const PDFDictionary* dictionary, const char* key);

private:
    const PDFDocument* m_document;
};

/// PDF document main class.
class PDFDocument
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocument)

public:
    explicit PDFDocument() = default;

    const PDFObjectStorage& getStorage() const { return m_pdfObjectStorage; }

    /// Info about the document. Title, Author, Keywords...
    struct Info
    {
        /// Indicates, that document was modified that it includes trapping information.
        /// See PDF Reference 1.7, Section 10.10.5 "Trapping Support".
        enum class Trapped
        {
            True,       ///< Fully trapped
            False,      ///< Not yet trapped
            Unknown     ///< Either unknown, or it has been trapped partly, not fully
        };

        QString title;
        QString author;
        QString subject;
        QString keywords;
        QString creator;
        QString producer;
        QDateTime creationDate;
        QDateTime modifiedDate;
        Trapped trapped = Trapped::Unknown;
    };

    /// Returns info about the document (title, author, etc.)
    const Info* getInfo() const { return &m_info; }

    /// If object is reference, the dereference attempt is performed
    /// and object is returned. If it is not a reference, then self
    /// is returned. If dereference attempt fails, then null object
    /// is returned (no exception is thrown).
    const PDFObject& getObject(const PDFObject& object) const;

    /// Returns the document catalog
    const PDFCatalog* getCatalog() const { return &m_catalog; }

    /// Returns the decoded stream. If stream data cannot be decoded,
    /// then empty byte array is returned.
    /// \param stream Stream to be decoded
    QByteArray getDecodedStream(const PDFStream* stream) const;

    /// Returns the trailer dictionary
    const PDFDictionary* getTrailerDictionary() const;

private:
    friend class PDFDocumentReader;

    explicit PDFDocument(PDFObjectStorage&& storage) :
        m_pdfObjectStorage(std::move(storage))
    {
        init();
    }

    /// Initialize data based on object in the storage.
    /// Can throw exception if error is detected.
    void init();

    /// Initialize the document info from the trailer dictionary.
    /// If document info is not present, then default document
    /// info is used. If error is detected, exception is thrown.
    void initInfo();

    /// Storage of objects
    PDFObjectStorage m_pdfObjectStorage;

    /// Info about the PDF document
    Info m_info;

    /// Catalog object
    PDFCatalog m_catalog;
};

// Implementation

inline
const PDFObject& PDFDocument::getObject(const PDFObject& object) const
{
    if (object.isReference())
    {
        // Try to dereference the object
        return m_pdfObjectStorage.getObject(object.getReference());
    }

    return object;
}

}   // namespace pdf

#endif // PDFDOCUMENT_H
