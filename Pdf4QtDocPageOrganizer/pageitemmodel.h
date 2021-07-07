//    Copyright (C) 2021 Jakub Melka
//
//    This file is part of Pdf4Qt.
//
//    Pdf4Qt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    Pdf4Qt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with Pdf4Qt.  If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFDOCPAGEORGANIZER_PAGEITEMMODEL_H
#define PDFDOCPAGEORGANIZER_PAGEITEMMODEL_H

#include "pdfdocument.h"
#include "pdfutils.h"

#include <QAbstractItemModel>

namespace pdfdocpage
{

struct PageGroupItem
{
    QString groupName;
    QString pagesCaption;
    QStringList tags;

    struct GroupItem
    {
        int documentIndex = 0;
        pdf::PDFInteger pageIndex;
        QSizeF rotatedPageDimensionsMM;
    };

    std::vector<GroupItem> groups;
};

struct DocumentItem
{
    QString fileName;
    pdf::PDFDocument document;
};

class PageItemModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    explicit PageItemModel(QObject* parent);

    virtual QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    virtual QModelIndex index(int row, int column, const QModelIndex& parent) const override;
    virtual QModelIndex parent(const QModelIndex& index) const override;
    virtual int rowCount(const QModelIndex& parent) const override;
    virtual int columnCount(const QModelIndex& parent) const override;
    virtual QVariant data(const QModelIndex& index, int role) const override;

    /// Adds document to the model, inserts one single page group containing
    /// the whole document. Returns index of a newly added document. If document
    /// cannot be added (for example, it already exists), -1 is returned.
    /// \param fileName File name
    /// \param document Document
    /// \returns Identifier of the document (internal index)
    int addDocument(QString fileName, pdf::PDFDocument document);

    /// Returns item at a given index. If item doesn't exist,
    /// then nullptr is returned.
    /// \param index Index
    const PageGroupItem* getItem(const QModelIndex& index) const;

private:
    void createDocumentGroup(int index);
    QString getGroupNameFromDocument(int index) const;

    std::vector<PageGroupItem> m_pageGroupItems;
    std::map<int, DocumentItem> m_documents;
};

}   // namespace pdfdocpage

#endif // PDFDOCPAGEORGANIZER_PAGEITEMMODEL_H
