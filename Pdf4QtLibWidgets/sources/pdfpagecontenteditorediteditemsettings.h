//    Copyright (C) 2024 Jakub Melka
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

#ifndef PDFPAGECONTENTEDITOREDITEDITEMSETTINGS_H
#define PDFPAGECONTENTEDITOREDITEDITEMSETTINGS_H

#include <QDialog>

namespace Ui {
class PDFPageContentEditorEditedItemSettings;
}

namespace pdf
{
class PDFPageContentElementEdited;

class PDFPageContentEditorEditedItemSettings : public QWidget
{
    Q_OBJECT

public:
    explicit PDFPageContentEditorEditedItemSettings(QWidget* parent);
    virtual ~PDFPageContentEditorEditedItemSettings() override;

    void loadFromElement(PDFPageContentElementEdited* editedElement);
    void saveToElement(PDFPageContentElementEdited* editedElement);

private:
    void setImage(QImage image);
    void selectImage();

    Ui::PDFPageContentEditorEditedItemSettings* ui;
    QImage m_image;
};

}   // namespace pdf

#endif // PDFPAGECONTENTEDITOREDITEDITEMSETTINGS_H
