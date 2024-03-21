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

#ifndef EDITORSPLUGIN_H
#define EDITORSPLUGIN_H

#include "pdfplugin.h"
#include "pdfpagecontentelements.h"
#include "pdfpagecontenteditortools.h"
#include "pdfpagecontenteditorprocessor.h"

#include <QObject>

namespace pdf
{
class PDFPageContentEditorWidget;
}

namespace pdfplugin
{

class EditorPlugin : public pdf::PDFPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "PDF4QT.EditorPlugin" FILE "EditorPlugin.json")

private:
    using BaseClass = pdf::PDFPlugin;

public:
    EditorPlugin();

    virtual void setWidget(pdf::PDFWidget* widget) override;
    virtual void setDocument(const pdf::PDFModifiedDocument& document) override;
    virtual std::vector<QAction*> getActions() const override;

private:
    void onSceneChanged(bool graphicsOnly);
    void onSceneSelectionChanged();
    void onWidgetSelectionChanged();
    void onToolActivityChanged();
    void onSceneEditElement(const std::set<pdf::PDFInteger>& elements);

    void onPenChanged(const QPen& pen);
    void onBrushChanged(const QBrush& brush);
    void onFontChanged(const QFont& font);
    void onAlignmentChanged(Qt::Alignment alignment);
    void onTextAngleChanged(pdf::PDFReal angle);

    enum Action
    {
        // Activate action
        Activate,

        // Create graphics actions
        Text,
        FreehandCurve,
        AcceptMark,
        RejectMark,
        Rectangle,
        RoundedRectangle,
        HorizontalLine,
        VerticalLine,
        Line,
        Dot,
        SvgImage,
        Clear,

        LastAction
    };

    enum Tools
    {
        TextTool,
        FreehandCurveTool,
        AcceptMarkTool,
        RejectMarkTool,
        RectangleTool,
        RoundedRectangleTool,
        HorizontalLineTool,
        VerticalLineTool,
        LineTool,
        DotTool,
        ImageTool,
        LastTool
    };

    void setActive(bool active);

    void updateActions();
    void updateGraphics();
    void updateDockWidget();
    void updateEditedPages();

    void onDrawSpaceChanged();

    pdf::PDFWidgetTool* getActiveTool();

    std::array<QAction*, LastAction> m_actions;
    std::array<pdf::PDFWidgetTool*, LastTool> m_tools;
    pdf::PDFPageContentEditorWidget* m_editorWidget;

    pdf::PDFPageContentScene m_scene;
    std::map<pdf::PDFInteger, pdf::PDFEditedPageContent> m_editedPageContent;
    bool m_sceneSelectionChangeEnabled;
};

}   // namespace pdfplugin

#endif // EDITORSPLUGIN_H
