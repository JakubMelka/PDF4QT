//    Copyright (C) 2021 Jakub Melka
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
//    along with PDF4QT.  If not, see <https://www.gnu.org/licenses/>.

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
