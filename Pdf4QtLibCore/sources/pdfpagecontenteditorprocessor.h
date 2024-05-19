//    Copyright (C) 2023 Jakub Melka
//
//    This file is part of PDF4QT.
//
//    PDF4QT is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    PDF4QT is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDF4QT. If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFPAGECONTENTEDITORPROCESSOR_H
#define PDFPAGECONTENTEDITORPROCESSOR_H

#include "pdfpagecontentprocessor.h"

#include <memory>

namespace pdf
{

class PDFEditedPageContentElementPath;
class PDFEditedPageContentElementText;
class PDFEditedPageContentElementImage;

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
        Image
    };

    virtual Type getType() const = 0;
    virtual PDFEditedPageContentElement* clone() const = 0;

    virtual PDFEditedPageContentElementPath* asPath() { return nullptr; }
    virtual const PDFEditedPageContentElementPath* asPath() const { return nullptr; }

    virtual PDFEditedPageContentElementText* asText() { return nullptr; }
    virtual const PDFEditedPageContentElementText* asText() const { return nullptr; }

    virtual PDFEditedPageContentElementImage* asImage() { return nullptr; }
    virtual const PDFEditedPageContentElementImage* asImage() const { return nullptr; }

    const PDFPageContentProcessorState& getState() const;
    void setState(const PDFPageContentProcessorState& newState);

    virtual QRectF getBoundingBox() const = 0;

    QTransform getTransform() const;
    void setTransform(const QTransform& newTransform);

protected:
    PDFPageContentProcessorState m_state;
    QTransform m_transform;
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

    static QString createItemsAsText(const PDFPageContentProcessorState& initialState,
                                     const std::vector<Item>& items);

    QString getItemsAsText() const;
    void setItemsAsText(const QString& newItemsAsText);

private:
    std::vector<Item> m_items;
    QPainterPath m_textPath;
    QString m_itemsAsText;
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
    void addContentClipping(PDFPageContentProcessorState state, QPainterPath path);
    void addContentElement(std::unique_ptr<PDFEditedPageContentElement> element);

    std::size_t getElementCount() const { return m_contentElements.size(); }
    PDFEditedPageContentElement* getElement(size_t index) const { return m_contentElements.at(index).get(); }

    PDFEditedPageContentElement* getBackElement() const;

    PDFDictionary getFontDictionary() const;
    void setFontDictionary(const PDFDictionary& newFontDictionary);

    PDFDictionary getXObjectDictionary() const;
    void setXObjectDictionary(const PDFDictionary& newXobjectDictionary);

private:
    std::vector<std::unique_ptr<PDFEditedPageContentElement>> m_contentElements;
    PDFDictionary m_fontDictionary;
    PDFDictionary m_xobjectDictionary;
};

class PDF4QTLIBCORESHARED_EXPORT PDFPageContentEditorContentStreamBuilder
{
public:
    PDFPageContentEditorContentStreamBuilder();

    void writeStateDifference(QTextStream& stream, const PDFPageContentProcessorState& state);
    void writeElement(const PDFEditedPageContentElement* element);

    const QByteArray& getOutputContent() const;

private:
    void writePainterPath(QTextStream& stream,
                          const QPainterPath& path,
                          bool isStroking,
                          bool isFilling);
    void writeText(QTextStream& stream, const QString& text);

    void writeImage(QTextStream& stream, const QImage& image);

    QByteArray selectFont(const QByteArray& font);
    void addError(const QString& error);

    PDFDocument* m_document = nullptr;
    PDFDictionary m_fontDictionary;
    PDFDictionary m_xobjectDictionary;
    PDFDictionary m_graphicStateDictionary;
    QByteArray m_outputContent;
    PDFPageContentProcessorState m_currentState;
    PDFFontPointer m_textFont;
    QStringList m_errors;
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
    virtual bool isContentKindSuppressed(ContentKind kind) const override;
    virtual bool performOriginalImagePainting(const PDFImage& image, const PDFStream* stream) override;
    virtual void performImagePainting(const QImage& image) override;
    virtual void performClipping(const QPainterPath& path, Qt::FillRule fillRule) override;
    virtual void performSaveGraphicState(ProcessOrder order) override;
    virtual void performRestoreGraphicState(ProcessOrder order) override;
    virtual void performUpdateGraphicsState(const PDFPageContentProcessorState& state) override;
    virtual void performProcessTextSequence(const TextSequence& textSequence, ProcessOrder order) override;

private:
    PDFEditedPageContent m_content;
    QRectF m_textBoundingRect;
    std::stack<QPainterPath> m_clippingPaths;
    std::unique_ptr<PDFEditedPageContentElementText> m_contentElementText;
    QPainterPath m_textPath;
};

}   // namespace pdf

#endif // PDFPAGECONTENTEDITORPROCESSOR_H
