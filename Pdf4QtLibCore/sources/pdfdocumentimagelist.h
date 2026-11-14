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

#ifndef PDFDOCUMENTIMAGELIST_H
#define PDFDOCUMENTIMAGELIST_H

#include "pdfglobal.h"
#include "pdfobject.h"
#include "pdfoptionalcontent.h"
#include "pdfprogress.h"

#include <QDataStream>
#include <QPointF>
#include <QSize>
#include <QString>
#include <QStringView>

#include <map>
#include <optional>
#include <set>
#include <vector>

namespace pdf
{

class PDFDocument;
class PDFOperationControl;

/// The one collection of the images of a document (step 4 of phase 2, plan 4.1 with R4-3, R4-4,
/// R4-5, R4-8, R4-11): Optimize Images, Grayscale, Black & White (Images), Extract Images, Copy
/// Image and the MCP action document.list_images use it. It walks the content of the pages, their
/// form XObjects and tiling patterns (PDFImageCompressor::Environment::processPageImages) in the
/// threads of PDFExecutionPolicy, a page per task, and decodes nothing: every value of an item is
/// read from the image dictionary and the placement of the image.
///
/// What is an item: an image XObject drawn by the content (its reference is its key) or an inline
/// image (its key is the content stream - the page or the form XObject - and the index of its BI
/// there). Never an item: a glyph of a Type 3 font (the text is never walked), the appearance
/// of an annotation (R4-4: the changing tools never change /AP), a mask of another image, which
/// is not drawn itself (SMask, /Mask), an image, which is only in /Resources and never drawn (B61).
/// The images of all layers are items (R4-4, R4-11); the options of the exports flag an image,
/// which is drawn only in layers hidden by the state of the tab (isInHiddenLayer).
///
/// The order of the items is the order of the first drawing: the pages in their order, on a page
/// the order of the content - the number N of "Image N" and of the token # of Extract Images
/// (K4.12) is the index + 1.
///
/// A value: copied freely, also into a thread (a worker gets a copy or a shared_ptr<const>).
/// Hostile input: a form drawing itself and deep nesting are refused by the content processor, a
/// form shared by more pages gives one item with all of them; the counts are not bounded (no
/// decoding: 10 000 images are a few MB).
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentImageList
{
public:
    /// Key of an image (R4-8): "148 0" (an image XObject), "inline:12 0:3" (the fourth inline
    /// image of the content of the object 12 0 - a page, also with an array /Contents, or a form
    /// XObject). The same text is the id of MCP (document.list_images, images, exclude, items).
    class PDF4QTLIBCORESHARED_EXPORT Key
    {
    public:
        PDFObjectReference reference;           ///< The image XObject (invalid for an inline image)
        PDFObjectReference inlineContentStream; ///< An inline image: its page or form XObject
        PDFInteger inlineIndex = -1;            ///< An inline image: the index of its BI in that content from 0; -1 = an image XObject

        /// An image XObject
        static Key createReference(PDFObjectReference reference);

        /// An inline image
        static Key createInline(PDFObjectReference contentStream, PDFInteger index);

        bool isInline() const { return inlineIndex >= 0; }
        bool isValid() const { return isInline() ? inlineContentStream.isValid() : reference.isValid(); }

        /// "148 0", "inline:12 0:3"
        QString toString() const;

        /// The key of a text of toString (spaces around the parts allowed); std::nullopt for a
        /// text, which is no key (negative numbers, a missing part, more text)
        static std::optional<Key> fromString(QStringView text);

        bool operator==(const Key& other) const;
        bool operator!=(const Key& other) const { return !(*this == other); }
        bool operator<(const Key& other) const;

        /// A key is streamed as its id text (toString); a text, which is no key, reads an
        /// invalid key. The meta type of a signal with std::vector<Key> needs them (the
        /// stream templates of std::vector of pdfutils.h).
        friend inline QDataStream& operator<<(QDataStream& stream, const Key& key) { stream << key.toString(); return stream; }
        friend inline QDataStream& operator>>(QDataStream& stream, Key& key)
        {
            QString text;
            stream >> text;
            key = fromString(text).value_or(Key());
            return stream;
        }
    };

    /// What the item is
    enum class Kind
    {
        XObject,    ///< An image XObject (Do)
        Inline      ///< An inline image (BI ... ID ... EI, 8.9.7): only read (Extract Images), never changed (R4-8: "inline · kept")
    };

    /// The color of an image by its declaration (R4-5): never by its pixels
    enum class ImageColor
    {
        Color,      ///< DeviceRGB, DeviceCMYK, CalRGB, Lab, ICCBased with 3 or 4 components, Indexed over such a base, Separation, DeviceN, an unknown or missing space
        Gray,       ///< DeviceGray, CalGray, ICCBased with 1 component, Indexed over a gray base
        Bitonal     ///< /ImageMask true, or 1 bit per component
    };

    /// One image of the document
    class PDF4QTLIBCORESHARED_EXPORT Item
    {
    public:
        Key key;
        Kind kind = Kind::XObject;
        std::vector<PDFInteger> pageIndices;    ///< Pages, which draw it (indices from 0, ascending, each once)
        QPointF minimalDpi;                     ///< The smallest resolution of all its drawings per axis (pixels / inches of the axis on the page); 0 = not known (every drawing degenerated)
        QSize pixelSize;                        ///< /Width x /Height (as declared; 0 when missing)
        int bitsPerComponent = 0;               ///< /BitsPerComponent (0 = not declared: JPX, an image mask declares 1)
        bool isImageMask = false;               ///< /ImageMask true (a stencil, 8.9.6.2)
        QString colorSpaceName;                 ///< Family of the declared color space ("DeviceRGB", "ICCBased", "Indexed"); empty = none
        ImageColor color = ImageColor::Color;   ///< getImageColor of the declaration
        QString filterName;                     ///< The last filter of the stream ("DCTDecode"); empty = none
        qint64 streamBytes = 0;                 ///< Bytes of the encoded data of the image stream
        qint64 maskBytes = 0;                   ///< Bytes of the encoded data of its /SMask and its /Mask stream
        PDFObjectReference softMaskReference;   ///< /SMask (invalid = none)
        PDFObjectReference maskReference;       ///< /Mask, when it is a stream (an explicit mask; invalid = none or a color key mask)
        bool isUsedAsMask = false;              ///< The image is also the /SMask or the /Mask of another image of the list (B62: a shared mask)
        bool isInHiddenLayer = false;           ///< Options of an export: every drawing of the image is in a layer hidden by the states (R4-11, the label "hidden layer")
        bool isInAppearance = false;            ///< An image XObject, which an appearance of an annotation draws too (/AP, collectAppearanceImages): the changing tools keep it (R4-4)

        /// The image is changed by no tool (an inline image, R4-8; an image of an appearance too, R4-4)
        bool isKeptByChangingTools() const { return kind == Kind::Inline || isInAppearance; }

        /// The image is a color image (Grayscale converts it; R4-5)
        bool isColorImage() const { return color == ImageColor::Color; }
    };

    /// What the collection is made for
    class PDF4QTLIBCORESHARED_EXPORT Options
    {
    public:
        /// Against what the flag isInHiddenLayer is computed
        enum class HiddenLayers
        {
            NotComputed,    ///< The changing tools (R4-4: the images of all layers, no flag)
            ExportUsage,    ///< The layers of an export without a tab (MCP, PdfTool): the configuration /D with the automatic states of /Export (ISO 32000-2, 8.11.4.4; OCUsage::Export)
            GivenStates     ///< The states of the layers of the tab (layerStates; R4-10 b, R4-11)
        };

        HiddenLayers hiddenLayers = HiddenLayers::NotComputed;
        std::map<PDFObjectReference, OCState> layerStates;  ///< GivenStates: the state of every layer of the tab (a layer, which is missing, keeps the state of /D)

        /// The changing tools: Optimize Images, Grayscale, Black & White (R4-4)
        static Options createForChange();

        /// An export without a tab (MCP, PdfTool): hidden by OCUsage::Export
        static Options createForExport();

        /// An export in a tab: hidden by the states of the layers of the tab
        static Options createForExport(std::map<PDFObjectReference, OCState> layerStates);
    };

    PDFDocumentImageList() = default;

    /// Collects the images of the document (see the class). Reentrant.
    /// \param document The document (nullptr = an empty list)
    /// \param options What the collection is made for
    /// \param operationControl Cancellation, checked per page and per operator (optional): a
    ///        canceled collection is incomplete (isComplete false), its items are those found so far
    /// \param progress A step per page (optional; it is called by one thread at a time)
    static PDFDocumentImageList create(const PDFDocument* document, const Options& options, const PDFOperationControl* operationControl, PDFProgressSink* progress);

    /// The items in the order of the first drawing
    const std::vector<Item>& getItems() const { return m_items; }

    /// The item of the key, nullptr = none
    const Item* findItem(const Key& key) const;

    /// Index of the item of the key, -1 = none
    int indexOf(const Key& key) const;

    /// The collection has walked every page (it was not canceled)
    bool isComplete() const { return m_isComplete; }

    /// Bytes of the streams and masks of all items ("412 images · 21.3 MB", 297:3135)
    qint64 getTotalBytes() const;

    /// The color of an image by its declaration (R4-5; the one rule of the list and of
    /// PDFImageOptimizer::Settings::onlyColorImages)
    /// \param document The document of the dictionary
    /// \param imageDictionary The dictionary of the image stream
    /// \param colorSpace The declared color space, resolved (an inline image: the value of a
    ///        resource name; DrawnImage::colorSpace)
    static ImageColor getImageColor(const PDFDocument* document, const PDFDictionary* imageDictionary, const PDFObject& colorSpace);

    /// As above, the color space taken from /ColorSpace of the dictionary (an image XObject)
    static ImageColor getImageColor(const PDFDocument* document, const PDFDictionary* imageDictionary);

    /// The image is a color image by its declaration (getImageColor is Color)
    static bool isColorImage(const PDFDocument* document, const PDFDictionary* imageDictionary, const PDFObject& colorSpace);

    /// As above, the color space taken from /ColorSpace of the dictionary (an image XObject)
    static bool isColorImage(const PDFDocument* document, const PDFDictionary* imageDictionary);

    /// The image XObjects of the appearances of the annotations of the pages (/AP /N, /R, /D and
    /// their states, ISO 32000-2, 12.5.5) and their masks: the images of the XObjects of the forms
    /// and of their resources (forms, tiling patterns, the groups of soft masks of the graphics
    /// states, Type 3 fonts). R4-4: the changing tools never change an appearance, so an image,
    /// which the page and an appearance share, is kept. One walk for the whole document (a cycle
    /// of forms and a form shared by two appearances are read once, PDFObjectWalk).
    static std::set<PDFObjectReference> collectAppearanceImages(const PDFDocument* document);

    /// Most nested color spaces followed by getImageColor (Indexed over Indexed, an alternate of
    /// ICCBased); a deeper declaration is a color image
    static constexpr int MAXIMUM_COLOR_SPACE_DEPTH = 8;

private:
    std::vector<Item> m_items;
    std::map<Key, size_t> m_indices;    ///< Index of the item of each key
    bool m_isComplete = true;
};

}   // namespace pdf

#endif // PDFDOCUMENTIMAGELIST_H
