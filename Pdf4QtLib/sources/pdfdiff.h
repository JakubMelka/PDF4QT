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

#ifndef PDFDIFF_H
#define PDFDIFF_H

#include "pdfdocument.h"

#include <QObject>

namespace pdf
{

/// Diff engine for comparing two pdf documents.
class PDFDiff : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFDiff(QObject* parent);

    void setLeftDocument(const PDFDocument* leftDocument);
    void setRightDocument(const PDFDocument* rightDocument);

    /// Clears data (but not source document pointers,
    /// for them, use setters), also stops comparing engine.
    void clear();

private:
    void stopEngine();

    const PDFDocument* m_leftDocument;
    const PDFDocument* m_rightDocument;
};

}   // namespace pdf

#endif // PDFDIFF_H
