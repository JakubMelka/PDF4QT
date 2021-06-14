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

#ifndef OBJECTVIEWERWIDGET_H
#define OBJECTVIEWERWIDGET_H

#include "pdfdocument.h"
#include "pdfcms.h"

#include <QWidget>

namespace Ui
{
class ObjectViewerWidget;
}

namespace pdfplugin
{

class ObjectViewerWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ObjectViewerWidget(QWidget* parent);
    explicit ObjectViewerWidget(bool isPinned, QWidget* parent);
    virtual ~ObjectViewerWidget() override;

    void setPinned(bool isPinned);
    void setData(pdf::PDFObjectReference currentReference, pdf::PDFObject currentObject, bool isRootObject);

    const pdf::PDFDocument* getDocument() const;
    void setDocument(const pdf::PDFDocument* document);

    const pdf::PDFCMS* getCms() const;
    void setCms(const pdf::PDFCMS* cms);

signals:
    void pinRequest();
    void unpinRequest();

private:
    void updateUi();
    void updatePinnedUi();

    Ui::ObjectViewerWidget* ui;
    const pdf::PDFCMS* m_cms;
    const pdf::PDFDocument* m_document;
    bool m_isPinned;

    pdf::PDFObjectReference m_currentReference;
    pdf::PDFObject m_currentObject;
    bool m_isRootObject;
    QByteArray m_printableCharacters;
};

}   // pdfplugin

#endif // OBJECTVIEWERWIDGET_H
