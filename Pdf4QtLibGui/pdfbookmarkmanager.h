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

    bool isEmpty() const;
    int getBookmarkCount() const;
    Bookmark getBookmark(int index) const;
    void toggleBookmark(pdf::PDFInteger pageIndex);
    void setGenerateBookmarksAutomatically(bool generateBookmarksAutomatically);

    void goToNextBookmark();
    void goToPreviousBookmark();
    void goToCurrentBookmark();
    void goToBookmark(int index, bool force);

signals:
    void bookmarksAboutToBeChanged();
    void bookmarksChanged();
    void bookmarkActivated(int index, Bookmark bookmark);

private:
    friend class PDFBookmarkManagerHelper;

    void sortBookmarks();
    void regenerateAutoBookmarks();

    struct Bookmarks
    {
        std::vector<Bookmark> bookmarks;
    };

    pdf::PDFDocument* m_document;
    Bookmarks m_bookmarks;
    int m_currentBookmark = -1;
    bool m_generateBookmarksAutomatically = true;
};

}   // namespace pdf

#endif // PDFBOOKMARKMANAGER_H
