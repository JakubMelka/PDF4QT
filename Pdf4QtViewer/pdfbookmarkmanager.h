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
//    along with PDF4QT.  If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFBOOKMARKMANAGER_H
#define PDFBOOKMARKMANAGER_H

#include "pdfdocument.h"

#include <QObject>

namespace pdfviewer
{

class PDFBookmarkManager : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    PDFBookmarkManager(QObject* parent);

    void setDocument(const pdf::PDFModifiedDocument& document);

    void saveToFile(QString fileName);
    bool loadFromFile(QString fileName);

    struct Bookmark
    {
        bool isAuto = false;
        QString name;
        pdf::PDFInteger pageIndex = -1;
    };

    int getBookmarkCount() const;
    Bookmark getBookmark(int index) const;

signals:
    void bookmarksAboutToBeChanged();
    void bookmarksChanged();

private:
    friend class PDFBookmarkManagerHelper;

    void regenerateAutoBookmarks();

    struct Bookmarks
    {
        std::vector<Bookmark> bookmarks;
    };

    pdf::PDFDocument* m_document;
    QString m_currentKey;
    std::map<QString, Bookmarks> m_bookmarks;
};

}   // namespace pdf

#endif // PDFBOOKMARKMANAGER_H
