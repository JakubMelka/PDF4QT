//    Copyright (C) 2022 Jakub Melka
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

#ifndef PDFMEDIAVIEWERDIALOG_H
#define PDFMEDIAVIEWERDIALOG_H

#include <QDialog>

namespace Qt3DCore
{
class QEntity;
}

namespace Qt3DExtras
{
class Qt3DWindow;
}

namespace Ui
{
class PDFMediaViewerDialog;
}

namespace pdfviewer
{

class PDFMediaViewerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PDFMediaViewerDialog(QWidget* parent);
    virtual ~PDFMediaViewerDialog() override;

    void initDemo();

private:
    Ui::PDFMediaViewerDialog* ui;
    Qt3DExtras::Qt3DWindow* m_3dWindow = nullptr;
    Qt3DCore::QEntity* m_rootEntity = nullptr;
    Qt3DCore::QEntity* m_sceneEntity = nullptr;
};

}   // namespace pdfviewer

#endif // PDFMEDIAVIEWERDIALOG_H
