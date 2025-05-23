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
    virtual QString getPluginMenuName() const override;

private:
    void onSceneChanged(bool graphicsOnly);
    void onSceneSelectionChanged();
    void onWidgetSelectionChanged();
    void onToolActivityChanged();
    void onSceneEditElement(const std::set<pdf::PDFInteger>& elements);
    void onSignElectronically();
    void onSignDigitally();
    void onOpenCertificatesManager();
    void onSceneActivityChanged();

    void onPenChanged(const QPen& pen);
    void onBrushChanged(const QBrush& brush);
    void onFontChanged(const QFont& font);
    void onAlignmentChanged(Qt::Alignment alignment);
    void onTextAngleChanged(pdf::PDFReal angle);
    void onSceneEditSingleElement(pdf::PDFInteger elementId);

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
        ImageTool,
        LastTool
    };

    void setActive(bool active);

    pdf::PDFWidgetTool* getActiveTool();

    void updateActions();
    void updateGraphics();
    void updateDockWidget();

    QString getSignedFileName() const;

    std::array<QAction*, LastAction> m_actions;
    std::array<pdf::PDFWidgetTool*, LastTool> m_tools;
    pdf::PDFPageContentEditorWidget* m_editorWidget;

    pdf::PDFPageContentScene m_scene;
    bool m_sceneSelectionChangeEnabled;
};

}   // namespace pdfplugin

#endif // SIGNATURESPLUGIN_H
