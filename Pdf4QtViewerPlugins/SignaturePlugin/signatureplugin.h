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

#ifndef SIGNATURESPLUGIN_H
#define SIGNATURESPLUGIN_H

#include "pdfplugin.h"
#include "pdfpagecontentelements.h"
#include "pdfpagecontenteditortools.h"

#include <QObject>

namespace pdf
{
class PDFPageContentEditorWidget;
}

namespace pdfplugin
{

class SignaturePlugin : public pdf::PDFPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "PDF4QT.SignaturePlugin" FILE "SignaturePlugin.json")

private:
    using BaseClass = pdf::PDFPlugin;

public:
    SignaturePlugin();

    virtual void setWidget(pdf::PDFWidget* widget) override;
    virtual void setDocument(const pdf::PDFModifiedDocument& document) override;
    virtual std::vector<QAction*> getActions() const override;

private:
    void onSceneChanged(bool graphicsOnly);
    void onSceneSelectionChanged();
    void onWidgetSelectionChanged();

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

        // Sign actions
        SignElectronically,
        SignDigitally,
        Certificates,

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
        LastTool
    };

    void setActive(bool active);

    void updateActions();
    void updateGraphics();
    void updateDockWidget();

    std::array<QAction*, LastAction> m_actions;
    std::array<pdf::PDFWidgetTool*, LastTool> m_tools;
    pdf::PDFPageContentEditorWidget* m_editorWidget;

    pdf::PDFPageContentScene m_scene;
    bool m_sceneSelectionChangeEnabled;
};

}   // namespace pdfplugin

#endif // SIGNATURESPLUGIN_H
