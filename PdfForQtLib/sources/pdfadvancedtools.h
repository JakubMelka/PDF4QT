//    Copyright (C) 2020 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFADVANCEDTOOLS_H
#define PDFADVANCEDTOOLS_H

#include "pdfwidgettool.h"
#include "pdfannotation.h"

class QActionGroup;

namespace pdf
{

/// Tool that creates 'sticky note' annotations. Multiple types of sticky
/// notes are available, user can select a type of sticky note. When
/// user select a point, popup window appears and user can enter a text.
class PDFFORQTLIBSHARED_EXPORT PDFCreateStickyNoteTool : public PDFWidgetTool
{
    Q_OBJECT

private:
    using BaseClass = PDFWidgetTool;

public:
    explicit PDFCreateStickyNoteTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QActionGroup* actionGroup, QObject* parent);

protected:
    virtual void updateActions() override;

private:
    void onActionTriggered(QAction* action);
    void onPointPicked(PDFInteger pageIndex, QPointF pagePoint);

    PDFToolManager* m_toolManager;
    QActionGroup* m_actionGroup;
    PDFPickTool* m_pickTool;
    TextAnnotationIcon m_icon;
};

} // namespace pdf

#endif // PDFADVANCEDTOOLS_H
