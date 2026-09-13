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

#ifndef PDFPAGECONTENTEDITORPROCESSOR_H
#define PDFPAGECONTENTEDITORPROCESSOR_H

#include "pdfpagecontentprocessor.h"

#include <map>
#include <memory>
#include <functional>

class QPainter;
class QXmlStreamReader;

namespace pdf
{

class PDFMesh;
class PDFColorConvertor;
class PDFEditedPageContentElementPath;
class PDFEditedPageContentElementText;
class PDFEditedPageContentElementImage;
class PDFEditedPageContentElementShading;

class PDF4QTLIBCORESHARED_EXPORT PDFEditedPageContentElement
{
public:
    PDFEditedPageContentElement() = default;
    PDFEditedPageContentElement(PDFPageContentProcessorState state, QTransform transform);
    virtual ~PDFEditedPageContentElement() = default;

    enum class Type
    {
        Path,
        Text,
        Image,
        Shading
    };

    virtual Type getType() const = 0;
    virtual PDFEditedPageContentElement* clone() const = 0;

    virtual PDFEditedPageContentElementPath* asPath() { return nullptr; }
    virtual const PDFEditedPageContentElementPath* asPath() const { return nullptr; }

    virtual PDFEditedPageContentElementText* asText() { return nullptr; }
    virtual const PDFEditedPageContentElementText* asText() const { return nullptr; }

    virtual PDFEditedPageContentElementImage* asImage() { return nullptr; }
    virtual const PDFEditedPageContentElementImage* asImage() const { return nullptr; }

    virtual PDFEditedPageContentElementShading* asShading() { return nullptr; }
    virtual const PDFEditedPageContentElementShading* asShading() const { return nullptr; }

    const PDFPageContentProcessorState& getState() const;
    void setState(const PDFPageContentProcessorState& newState);

    virtual QRectF getBoundingBox() const = 0;

    QTransform getTransform() const;
    void setTransform(const QTransform& newTransform);

    /// Returns the clip path of the element. The clip path is expressed
    /// in the element coordinate space (i.e. it is applied after the element
    /// transformation), so it follows the element when the element is moved
    /// or transformed. An empty path means that the element is not clipped.
    const QPainterPath& getClipPath() const;

    /// Sets the clip path of the element, in the element coordinate space.
    /// \param clipPath Clip path (an empty path means no clipping)
    void setClipPath(const QPainterPath& clipPath);

protected:
    PDFPageContentProcessorState m_state;
    QTransform m_transform;
    QPainterPath m_clipPath;
};

class PDF4QTLIBCORESHARED_EXPORT PDFEditedPageContentElementPath : public PDFEditedPageContentElement
{
public:
    PDFEditedPageContentElementPath(PDFPageContentProcessorState state,
                                    QPainterPath path,
                                    bool strokePath,
                                    bool fillPath,
                                    QTransform transform);
    virtual ~PDFEditedPageContentElementPath() = default;

    virtual Type getType() const override;
    virtual PDFEditedPageContentElementPath* clone() const override;
    virtual PDFEditedPageContentElementPath* asPath() override { return this; }
    virtual const PDFEditedPageContentElementPath* asPath() const override { return this; }
    virtual QRectF getBoundingBox() const override;

    QPainterPath getPath() const;
    void setPath(QPainterPath newPath);

    bool getStrokePath() const;
    void setStrokePath(bool newStrokePath);

    bool getFillPath() const;
    void setFillPath(bool newFillPath);

private:
    QPainterPath m_path;
    bool m_strokePath;
    bool m_fillPath;
};

class PDF4QTLIBCORESHARED_EXPORT PDFEditedPageContentElementImage : public PDFEditedPageContentElement
{
public:
    PDFEditedPageContentElementImage(PDFPageContentProcessorState state,
                                     PDFObject imageObject,
                                     QImage image,
                                     QTransform transform);
    virtual ~PDFEditedPageContentElementImage() = default;

    virtual Type getType() const override;
    virtual PDFEditedPageContentElementImage* clone() const override;
    virtual PDFEditedPageContentElementImage* asImage() override { return this; }
    virtual const PDFEditedPageContentElementImage* asImage() const override { return this; }
    virtual QRectF getBoundingBox() const override;

    PDFObject getImageObject() const;
    void setImageObject(const PDFObject& newImageObject);

    QImage getImage() const;
    void setImage(const QImage& newImage);

private:
    PDFObject m_imageObject;
    QImage m_image;
};

/// Shading painted by the 'sh' operator. The shading is written back into
/// the content stream using its shading object, the mesh is used only
/// to display the shading in the editor.
class PDF4QTLIBCORESHARED_EXPORT PDFEditedPageContentElementShading : public PDFEditedPageContentElement
{
public:
    /// Creates shading element
    /// \param state Graphic state, with which the shading is painted
    /// \param shadingObject Shading object (from the shading resource dictionary)
    /// \param area Area covered by the shading, in the element coordinate space
    /// \param mesh Mesh of the shading, in the element coordinate space
    /// \param transform Transformation matrix (maps the shading space to the page space)
    PDFEditedPageContentElementShading(PDFPageContentProcessorState state,
                                       PDFObject shadingObject,
                                       QPainterPath area,
                                       std::shared_ptr<const PDFMesh> mesh,
                                       QTransform transform);
    virtual ~PDFEditedPageContentElementShading() = default;

    virtual Type getType() const override;
    virtual PDFEditedPageContentElementShading* clone() const override;
    virtual PDFEditedPageContentElementShading* asShading() override { return this; }
    virtual const PDFEditedPageContentElementShading* asShading() const override { return this; }
    virtual QRectF getBoundingBox() const override;

    const PDFObject& getShadingObject() const;
    const QPainterPath& getArea() const;

    /// Paints the shading mesh. Painter transformation must map
    /// the element coordinate space to the device space.
    /// \param painter Painter
    /// \param convertor Color convertor
    void paint(QPainter* painter, const PDFColorConvertor& convertor) const;

private:
    PDFObject m_shadingObject;
    QPainterPath m_area;
    std::shared_ptr<const PDFMesh> m_mesh;
};

class PDF4QTLIBCORESHARED_EXPORT PDFEditedPageContentElementText : public PDFEditedPageContentElement
{
public:

    struct Item
    {
        bool isUpdateGraphicState = false;
        bool isText = false;
        TextSequence textSequence;

        PDFPageContentProcessorState state;
    };

    /// Font used by the text element. The key identifies the font uniquely
    /// in the text element and it is used by the font command of the text
    /// items. It is the name of the font in the resource dictionary of the
    /// content stream, in which the font was selected (the name can denote
    /// a different font in the page resources, if it was a form XObject).
    struct FontResource
    {
        QByteArray key;
        PDFFontPointer font;
        PDFObject fontObject; ///< Font object from the resource dictionary (null, if unknown)
    };

    PDFEditedPageContentElementText(PDFPageContentProcessorState state, QTransform transform);
    PDFEditedPageContentElementText(PDFPageContentProcessorState state,
                                    std::vector<Item> items,
                                    QPainterPath textPath,
                                    QTransform transform,
                                    QString itemsAsText);
    virtual ~PDFEditedPageContentElementText() = default;

    virtual Type getType() const override;
    virtual PDFEditedPageContentElementText* clone() const override;
    virtual PDFEditedPageContentElementText* asText() override { return this; }
    virtual const PDFEditedPageContentElementText* asText() const override { return this; }
    virtual QRectF getBoundingBox() const override;

    void addItem(Item item);
    const std::vector<Item>& getItems() const;
    void setItems(const std::vector<Item>& newItems);

    bool isEmpty() const { return m_items.empty(); }

    QPainterPath getTextPath() const;
    void setTextPath(QPainterPath newTextPath);

    /// Adds font used by the text element. If the font is already present,
    /// its key is returned. Otherwise the font is added under the given key,
    /// or under a modified key, if the key is already used by another font.
    /// \param font Font
    /// \param fontObject Font object from the resource dictionary (can be null)
    /// \param key Preferred key (name of the font in the resource dictionary)
    /// \returns Key of the font
    QByteArray addFontResource(const PDFFontPointer& font, const PDFObject& fontObject, const QByteArray& key);

    const std::vector<FontResource>& getFontResources() const;
    void setFontResources(const std::vector<FontResource>& fontResources);

    /// Returns key of the font in the text element. If the font
    /// is not found, the font identifier is returned.
    /// \param font Font
    QByteArray getFontResourceKey(const PDFFont* font) const;

    /// Creates the text representation of the text items
    /// \param initialState Graphic state at the beginning of the text object
    /// \param items Text items
    /// \param getFontKey Returns the key of the font used by the font command
    static QString createItemsAsText(const PDFPageContentProcessorState& initialState,
                                     const std::vector<Item>& items,
                                     const std::function<QByteArray(const PDFFont*)>& getFontKey);

    QString getItemsAsText() const;
    void setItemsAsText(const QString& newItemsAsText);

    void optimize();

private:
    std::vector<Item> m_items;
    QPainterPath m_textPath;
    QString m_itemsAsText;
    std::vector<FontResource> m_fontResources;
};

class PDF4QTLIBCORESHARED_EXPORT PDFEditedPageContent
{
public:
    PDFEditedPageContent() = default;
    PDFEditedPageContent(const PDFEditedPageContent&) = delete;
    PDFEditedPageContent(PDFEditedPageContent&&) = default;

    PDFEditedPageContent& operator=(const PDFEditedPageContent&) = delete;
    PDFEditedPageContent& operator=(PDFEditedPageContent&&) = default;

    static QString getOperatorToString(PDFPageContentProcessor::Operator operatorValue);
    static QString getOperandName(PDFPageContentProcessor::Operator operatorValue, int operandIndex);

    void addContentPath(PDFPageContentProcessorState state, QPainterPath path, bool strokePath, bool fillPath);
    void addContentImage(PDFPageContentProcessorState state, PDFObject imageObject, QImage image);
    void addContentElement(std::unique_ptr<PDFEditedPageContentElement> element);

    std::size_t getElementCount() const { return m_contentElements.size(); }
    PDFEditedPageContentElement* getElement(size_t index) const { return m_contentElements.at(index).get(); }

    PDFEditedPageContentElement* getBackElement() const;

    PDFDictionary getFontDictionary() const;
    void setFontDictionary(const PDFDictionary& newFontDictionary);

    PDFDictionary getXObjectDictionary() const;
    void setXObjectDictionary(const PDFDictionary& newXobjectDictionary);

    PDFDictionary getGraphicStateDictionary() const;
    void setGraphicStateDictionary(const PDFDictionary& newGraphicStateDictionary);

    PDFDictionary getShadingDictionary() const;
    void setShadingDictionary(const PDFDictionary& newShadingDictionary);

private:
    std::vector<std::unique_ptr<PDFEditedPageContentElement>> m_contentElements;
    PDFDictionary m_fontDictionary;
    PDFDictionary m_xobjectDictionary;
    PDFDictionary m_graphicStateDictionary;
    PDFDictionary m_shadingDictionary;
};

class PDF4QTLIBCORESHARED_EXPORT PDFPageContentEditorProcessor : public PDFPageContentProcessor
{
    using BaseClass = PDFPageContentProcessor;

public:
    PDFPageContentEditorProcessor(const PDFPage* page,
                                  const PDFDocument* document,
                                  const PDFFontCache* fontCache,
                                  const PDFCMS* CMS,
                                  const PDFOptionalContentActivity* optionalContentActivity,
                                  QTransform pagePointToDevicePointMatrix,
                                  const PDFMeshQualitySettings& meshQualitySettings);

    const PDFEditedPageContent& getEditedPageContent() const;
    PDFEditedPageContent takeEditedPageContent();

protected:
    virtual void performInterceptInstruction(Operator currentOperator, ProcessOrder processOrder, const QByteArray& operatorAsText) override;
    virtual void performPathPainting(const QPainterPath& path, bool stroke, bool fill, bool text, Qt::FillRule fillRule) override;
    virtual bool performPathPaintingUsingShading(const QPainterPath& path, bool stroke, bool fill, const PDFShadingPattern* shadingPattern) override;
    virtual bool isContentKindSuppressed(ContentKind kind) const override;
    virtual bool isTilingPatternProcessingAllowed(PDFInteger tileCount) const override;
    virtual bool performOriginalImagePainting(const PDFImage& image, const PDFStream* stream, PDFObjectReference reference) override;
    virtual void performImagePainting(const QImage& image) override;
    virtual void performClipping(const QPainterPath& path, Qt::FillRule fillRule) override;
    virtual void performSaveGraphicState(ProcessOrder order) override;
    virtual void performRestoreGraphicState(ProcessOrder order) override;
    virtual void performUpdateGraphicsState(const PDFPageContentProcessorState& state) override;
    virtual void performProcessTextSequence(const TextSequence& textSequence, ProcessOrder order) override;
    virtual void performBeginTransparencyGroup(ProcessOrder order, const PDFTransparencyGroup& transparencyGroup) override;
    virtual void performEndTransparencyGroup(ProcessOrder order, const PDFTransparencyGroup& transparencyGroup) override;

private:
    /// Maximum number of tiles of a tiling pattern, which is decomposed into
    /// the edited content elements. Patterns with more tiles are not painted.
    static constexpr PDFInteger MAXIMUM_TILING_PATTERN_TILE_COUNT = 512;

    /// Font selected by the font operator, with its object and name
    /// from the resource dictionary, in which the font was found.
    struct FontResourceInfo
    {
        PDFFontPointer font;
        QByteArray name;
        PDFObject fontObject;
    };

    /// Blend mode and constant alpha, with which a transparency group
    /// is composed onto its backdrop.
    struct TransparencyGroupState
    {
        BlendMode blendMode = BlendMode::Normal;
        PDFReal alphaFilling = 1.0;
        PDFReal alphaStroking = 1.0;
    };

    /// Returns the current clip path mapped into the coordinate space
    /// of an element with the given transformation matrix. If the element
    /// is not clipped (or the matrix is not invertible, in which case
    /// the element is degenerate and not visible anyway), an empty path
    /// is returned.
    /// \param elementTransform Element transformation matrix
    QPainterPath getCurrentClipPathInElementSpace(const QTransform& elementTransform) const;

    /// Returns the graphic state of a new element. Transparency groups can't
    /// be represented by the edited content elements - the content of a group
    /// is written directly into the page. So the blend mode and the constant
    /// alpha of the enclosing transparency groups are applied to the element.
    PDFPageContentProcessorState getElementState() const;

    /// Adds fonts used by the text element (the initial font and the fonts
    /// selected by the text items) into the font resources of the element.
    /// \param textElement Text element
    void registerFontResources(PDFEditedPageContentElementText* textElement) const;

    PDFEditedPageContent m_content;

    /// Stack of clip paths. The paths are stored in the page coordinate
    /// space (each clip path is mapped by the current transformation matrix
    /// at the time of the clip operation). An empty path means no clipping.
    std::stack<QPainterPath> m_clippingPaths;

    std::unique_ptr<PDFEditedPageContentElementText> m_contentElementText;
    QPainterPath m_textPath;

    /// Fonts selected by the font operator. The font pointer is held,
    /// so the address of the font can't be reused by another font.
    std::map<const PDFFont*, FontResourceInfo> m_fontResources;

    /// Text font before the currently processed operator
    PDFFontPointer m_textFontBeforeOperator;

    /// Stack of the transparency groups being processed
    std::vector<TransparencyGroupState> m_transparencyGroups;

    /// Shading object painted by the currently processed 'sh' operator
    PDFObject m_shadingObject;

    /// Graphic state of the element painted by the currently processed 'sh'
    /// operator (before the fill color space is changed by the operator)
    PDFPageContentProcessorState m_shadingState;

    /// Is the 'sh' operator being processed?
    bool m_isShadingOperatorActive = false;
};

}   // namespace pdf

#endif // PDFPAGECONTENTEDITORPROCESSOR_H
