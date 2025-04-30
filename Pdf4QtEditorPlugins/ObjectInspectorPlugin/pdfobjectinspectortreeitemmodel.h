// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
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

#ifndef PDFOBJECTINSPECTORTREEITEMMODEL_H
#define PDFOBJECTINSPECTORTREEITEMMODEL_H

#include "pdfitemmodels.h"
#include "pdfobjectutils.h"

#include <set>

namespace pdfplugin
{

class PDFObjectInspectorTreeItem;

class PDFObjectInspectorTreeItemModel : public pdf::PDFTreeItemModel
{
    Q_OBJECT

public:

    enum Mode
    {
        Document,
        Page,
        ContentStream,
        GraphicState,
        ColorSpace,
        Pattern,
        Shading,
        Image,
        Form,
        Font,
        Action,
        Annotation,
        List
    };

    explicit PDFObjectInspectorTreeItemModel(const pdf::PDFObjectClassifier* classifier, QObject* parent);

    virtual QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    virtual int columnCount(const QModelIndex& parent) const override;
    virtual QVariant data(const QModelIndex& index, int role) const override;
    virtual void update() override;

    void setMode(Mode mode);

    pdf::PDFObject getObjectFromIndex(const QModelIndex& index) const;
    pdf::PDFObjectReference getObjectReferenceFromIndex(const QModelIndex& index) const;
    bool isRootObject(const QModelIndex& index) const;

private:
    void createObjectItem(PDFObjectInspectorTreeItem* parent,
                          pdf::PDFObjectReference reference,
                          pdf::PDFObject object,
                          bool followRef,
                          std::set<pdf::PDFObjectReference>& usedReferences) const;

    PDFObjectInspectorTreeItem* getRootItem();

    const pdf::PDFObjectClassifier* m_classifier;
    Mode m_mode = List;
};

}   // namespace pdfplugin

#endif // PDFOBJECTINSPECTORTREEITEMMODEL_H
