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
//    along with PDFForQt. If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFDOCUMENTBUILDER_H
#define PDFDOCUMENTBUILDER_H

#include "pdfobject.h"
#include "pdfdocument.h"

namespace pdf
{

struct WrapName
{
    WrapName(const char* name) :
        name(name)
    {

    }

    QByteArray name;
};

struct WrapAnnotationColor
{
    WrapAnnotationColor(QColor color) :
        color(color)
    {

    }

    QColor color;
};

struct WrapCurrentDateTime { };
struct WrapEmptyArray { };

/// Factory for creating various PDF objects, such as simple objects,
/// dictionaries, arrays etc.
class PDFObjectFactory
{
public:
    inline explicit PDFObjectFactory() = default;

    void beginArray();
    void endArray();

    void beginDictionary();
    void endDictionary();

    void beginDictionaryItem(const QByteArray& name);
    void endDictionaryItem();

    PDFObjectFactory& operator<<(std::nullptr_t);
    PDFObjectFactory& operator<<(bool value);
    PDFObjectFactory& operator<<(PDFReal value);
    PDFObjectFactory& operator<<(PDFInteger value);
    PDFObjectFactory& operator<<(PDFObjectReference value);
    PDFObjectFactory& operator<<(WrapName wrapName);
    PDFObjectFactory& operator<<(int value);
    PDFObjectFactory& operator<<(const QRectF& value);
    PDFObjectFactory& operator<<(WrapCurrentDateTime);
    PDFObjectFactory& operator<<(WrapAnnotationColor color);
    PDFObjectFactory& operator<<(QString textString);
    PDFObjectFactory& operator<<(WrapEmptyArray);

    /// Treat containers - write them as array
    template<typename Container, typename ValueType = decltype(*std::begin(std::declval<Container>()))>
    PDFObjectFactory& operator<<(Container container)
    {
        beginArray();

        auto it = std::begin(container);
        auto itEnd = std::end(container);
        for (; it != itEnd; ++it)
        {
            *this << *it;
        }

        endArray();

        return *this;
    }

    PDFObject takeObject();

private:
    void addObject(PDFObject object);

    enum class ItemType
    {
        Object,
        Dictionary,
        DictionaryItem,
        Array
    };

    /// What is stored in this structure, depends on the type.
    /// If type is 'Object', then single simple object is in object,
    /// if type is dictionary, then PDFDictionary is stored in object,
    /// if type is dictionary item, then object and item name is stored
    /// in the data, if item is array, then array is stored in the data.
    struct Item
    {
        inline Item() = default;

        template<typename T>
        inline Item(ItemType type, T&& data) :
            type(type),
            object(qMove(data))
        {

        }

        template<typename T>
        inline Item(ItemType type, const QByteArray& itemName, T&& data) :
            type(type),
            itemName(qMove(itemName)),
            object(qMove(data))
        {

        }

        ItemType type = ItemType::Object;
        QByteArray itemName;
        std::variant<PDFObject, PDFArray, PDFDictionary> object;
    };

    std::vector<Item> m_items;
};

class PDFFORQTLIBSHARED_EXPORT PDFDocumentBuilder
{
public:
    /// Creates a new blank document (with no pages)
    explicit PDFDocumentBuilder();

    ///
    explicit PDFDocumentBuilder(const PDFDocument* document);

    /// Resets the object to the initial state.
    /// \warning All data are lost
    void reset();

    /// Create a new blank document with no pages. If some document
    /// is edited at call of this function, then it is lost.
    void createDocument();

    /// Builds a new document. This function can throw exceptions,
    /// if document being built was invalid.
    PDFDocument build();

    /// If object is reference, the dereference attempt is performed
    /// and object is returned. If it is not a reference, then self
    /// is returned. If dereference attempt fails, then null object
    /// is returned (no exception is thrown).
    const PDFObject& getObject(const PDFObject& object) const;

    /// Returns dictionary from an object. If object is not a dictionary,
    /// then nullptr is returned (no exception is thrown).
    const PDFDictionary* getDictionaryFromObject(const PDFObject& object) const;

    /// Returns object by reference. If dereference attempt fails, then null object
    /// is returned (no exception is thrown).
    const PDFObject& getObjectByReference(PDFObjectReference reference) const;

/* START GENERATED CODE */

    /// Square annotation displays rectangle (or square). When opened, they display pop-up window 
    /// containing the text of associated note (and window title). Square border/fill color can be defined, 
    /// along with border width.
    /// \param page Page to which is annotation added
    /// \param rectangle Area in which is rectangle displayed
    /// \param borderWidth Width of the border line of rectangle
    /// \param fillColor Fill color of rectangle (interior color). If you do not want to have area color filled, 
    ///        then use invalid QColor.
    /// \param strokeColor Stroke color (color of the rectangle border). If you do not want to have a 
    ///        border, then use invalid QColor.
    /// \param title Title (it is displayed as title of popup window)
    /// \param subject Subject (short description of the subject being adressed by the annotation)
    /// \param contents Contents (text displayed, for example, in the marked annotation dialog)
    PDFObjectReference createAnnotationSquare(PDFObjectReference page,
                                              QRectF rectangle,
                                              PDFReal borderWidth,
                                              QColor fillColor,
                                              QColor strokeColor,
                                              QString title,
                                              QString subject,
                                              QString contents);


    /// Creates a new popup annotation on the page. Popup annotation is represented usually by floating 
    /// window, which can be opened, or closed. Popup annotation is associated with parent annotation, 
    /// which can be usually markup annotation. Popup annotation displays parent annotation's texts, for 
    /// example, title, comment, date etc.
    /// \param page Page to which is annotation added
    /// \param parentAnnotation Parent annotation (for which is popup window displayed)
    /// \param rectangle Area on the page, where popup window appears
    /// \param opened Is the window opened?
    PDFObjectReference createAnnotationPopup(PDFObjectReference page,
                                             PDFObjectReference parentAnnotation,
                                             QRectF rectangle,
                                             bool opened);


    /// Creates empty catalog. This function is used, when a new document is being created. Do not call 
    /// this function manually.
    PDFObjectReference createCatalog();


    /// This function is used to create a new trailer dictionary, when blank document is created. Do not 
    /// call this function manually.
    /// \param catalog Reference to document catalog
    PDFObject createTrailerDictionary(PDFObjectReference catalog);


    /// This function is used to update trailer dictionary. Must be called each time the final document is 
    /// being built.
    /// \param objectCount Number of objects (including empty ones)
    void updateTrailerDictionary(PDFInteger objectCount);


    /// Appends a new page after last page.
    /// \param mediaBox Media box of the page (size of paper)
    PDFObjectReference appendPage(QRectF mediaBox);


    /// Creates page tree root for the catalog. This function is only called when new document is being 
    /// created. Do not call this function manually.
    PDFObjectReference createCatalogPageTreeRoot();


/* END GENERATED CODE */

private:
    PDFObjectReference addObject(PDFObject object);
    void mergeTo(PDFObjectReference reference, PDFObject object);
    void appendTo(PDFObjectReference reference, PDFObject object);
    QRectF getPopupWindowRect(const QRectF& rectangle) const;
    QString getProducerString() const;
    PDFObjectReference getPageTreeRoot() const;
    PDFInteger getPageTreeRootChildCount() const;

    PDFObjectStorage m_storage;
    PDFVersion m_version;
};

// Implementation

inline
const PDFObject& PDFDocumentBuilder::getObject(const PDFObject& object) const
{
    if (object.isReference())
    {
        // Try to dereference the object
        return m_storage.getObject(object.getReference());
    }

    return object;
}

inline
const PDFDictionary* PDFDocumentBuilder::getDictionaryFromObject(const PDFObject& object) const
{
    const PDFObject& dereferencedObject = getObject(object);
    if (dereferencedObject.isDictionary())
    {
        return dereferencedObject.getDictionary();
    }
    else if (dereferencedObject.isStream())
    {
        return dereferencedObject.getStream()->getDictionary();
    }

    return nullptr;
}

inline
const PDFObject& PDFDocumentBuilder::getObjectByReference(PDFObjectReference reference) const
{
    return m_storage.getObject(reference);
}

}   // namespace pdf

#endif // PDFDOCUMENTBUILDER_H
