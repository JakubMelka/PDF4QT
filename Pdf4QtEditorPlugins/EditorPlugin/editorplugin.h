// MIT License
//
// Copyright (c) 2018-2026 Jakub Melka and Contributors
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

#ifndef EDITORSPLUGIN_H
#define EDITORSPLUGIN_H

#include "pdfplugin.h"
#include "pdfpagecontentelements.h"
#include "pdfpagecontenteditortools.h"
#include "pdfpagecontenteditorprocessor.h"

#include <QObject>
#include <vector>

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
    virtual QString getPluginMenuName() const override;
    virtual PluginMenuLocation getPluginMenuLocation() const override;
    virtual std::vector<QAction*> getToolbarActions() const override;

    /// Returns true, if the page content editing is active. The edited page
    /// content is written into the document when the editing is finished,
    /// so while it is active, the edits are not a part of the document.
    virtual bool hasUnwrittenChanges() const override;

    /// Writes the edited page content into the document. The user is not asked
    /// for a confirmation - the caller is responsible for it.
    virtual bool writeUnwrittenChanges() override;

    bool save();

private:
    void onSceneActivityChanged();
    void onSceneChanged(bool graphicsOnly);
    void onSceneSelectionChanged();
    void onWidgetSelectionChanged();
    void onToolActivityChanged();
    void onSceneEditElement(const std::set<pdf::PDFInteger>& elements);
    void onSceneEditSingleElement(pdf::PDFInteger elementId);

    /// Performs one step of the local editor undo history.
    void onUndoTriggered();

    /// Performs one step of the local editor redo history.
    void onRedoTriggered();

    void onPenChanged(const QPen& pen);
    void onBrushChanged(const QBrush& brush);
    void onFontChanged(const QFont& font);
    void onAlignmentChanged(Qt::Alignment alignment);
    void onTextAngleChanged(pdf::PDFReal angle);

    /// Turns on/off the creation of multiple elements by all the creation tools
    /// and stores the setting, so it is restored in the next session.
    void onCreateMultipleElementsTriggered(bool enabled);

    enum Action
    {
        // Activate action
        Activate,

        // Local undo/redo actions
        Undo,
        Redo,

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

        // Settings of the creation tools
        CreateMultipleElements,

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

    static constexpr int MAX_UNDO_STEPS = 20;
    static constexpr int MAX_REDO_STEPS = 20;

    void setActive(bool active);
    void onSetActive(bool active);

    void updateActions();
    void updateGraphics();

    /// Reads the settings of the plugin, which are not a part of the application
    /// settings (the creation of multiple elements).
    void readSettings();

    /// Writes the settings of the plugin.
    void writeSettings();

    void updateDockWidget();
    void updateEditedPages();

    /// Clears both local undo and redo stacks.
    void clearUndoRedo();

    /// Initializes local undo/redo history from the current scene state.
    void initializeUndoRedo();

    /// Appends the current scene state to the local undo stack.
    void createUndoStep();

    /// Creates a deep copy of a move-only scene snapshot.
    /// \param state Source scene snapshot
    /// \return Cloned snapshot that can be safely restored later
    pdf::PDFPageContentScene::SceneState copySceneState(const pdf::PDFPageContentScene::SceneState& state) const;

    /// Writes the edited page content of all edited pages into the document.
    /// The editing session is finished - the scene and the undo/redo history
    /// are cleared and the modified document is sent to the application.
    /// \returns False, if the content was not written into the document
    bool writePageContentToDocument();

    bool updatePageContent(pdf::PDFInteger pageIndex,
                           const std::vector<const pdf::PDFPageContentElement*>& elements,
                           pdf::PDFDocumentBuilder* builder);
    bool updateTextElement(pdf::PDFPageContentElementEdited* element);

    void onDrawSpaceChanged();

    pdf::PDFWidgetTool* getActiveTool();

    std::array<QAction*, LastAction> m_actions;
    std::array<pdf::PDFWidgetTool*, LastTool> m_tools;
    pdf::PDFPageContentEditorWidget* m_editorWidget;

    pdf::PDFPageContentScene m_scene;
    std::map<pdf::PDFInteger, pdf::PDFEditedPageContent> m_editedPageContent;
    bool m_sceneSelectionChangeEnabled;
    bool m_isSaving;
    /// True while a local undo/redo restore is in progress.
    bool m_isUndoRedoInProgress;
    /// Timeline of scene snapshots used by the local undo action.
    std::vector<pdf::PDFPageContentScene::SceneState> m_undoStates;
    /// Timeline of scene snapshots used by the local redo action.
    std::vector<pdf::PDFPageContentScene::SceneState> m_redoStates;
};

}   // namespace pdfplugin

#endif // EDITORSPLUGIN_H
