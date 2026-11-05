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

#ifndef APPTOOLSHELLFIXTURE_H
#define APPTOOLSHELLFIXTURE_H

#include "apptoolsfixture.h"
#include "testdocumenttool.h"

#include "pdfappdeclarativetoolgui.h"
#include "pdfapptooldocumentpage.h"
#include "pdfapptoolresultdialog.h"
#include "pdfdocumenttoolguiregistry.h"
#include "pdfdocumenttoolrunner.h"
#include "pdfdocumenttoolshell.h"

#include <QStandardItemModel>

#include <QImage>
#include <QPointer>
#include <QString>
#include <QVariantMap>

#include <functional>
#include <memory>

namespace pdftest
{

/// The test tool with the parameters of the frames of the shell: Preset of four segments
/// (291:994), six steps (303:4405, the fifth one off)
class ShellTestTool : public TestDocumentTool
{
public:
    static constexpr int STEP_COUNT = 6;

    using TestDocumentTool::TestDocumentTool;

    pdf::PDFDocumentToolParameterDefinitions getParameters() const override;

    static QString getStepName(int index) { return QStringLiteral("step%1").arg(index + 1); }
};

/// The GUI of the test tool described by the builder: every part of a thin tool
class ShellTestGUI : public pdf::PDFAppDeclarativeToolGUI
{
    Q_OBJECT

public:
    /// \param hasLeftPanel The tool has a left panel with the page selection and the
    ///        pending list (389:20177), otherwise the sidebar of the document stays
    /// \param initialPanel Panel shown at the entry
    /// \param onlySegments The actions have only the segments of the frame 291:994
    explicit ShellTestGUI(bool hasLeftPanel, const QString& initialPanel = QString(), bool onlySegments = false);

    /// The steps of the frame 303:4560
    static std::vector<pdf::PDFAppToolFlagItem> getStepItems();

    pdf::PDFAppToolFlagsMenu* flagsMenu = nullptr;
    pdf::PDFAppToolEnumMenu* enumMenu = nullptr;
    pdf::PDFAppSettingControl* segments = nullptr;
    pdf::PDFKeyValueList* keyValues = nullptr;
    pdf::PDFAppToolStepList* steps = nullptr;
    pdf::PDFAppToolCallout* callout = nullptr;
    pdf::PDFAppParagraph* note = nullptr;
    pdf::PDFAppToolSectionHeader* resultHeader = nullptr;
    pdf::PDFPageSelection* pageSelection = nullptr;
    pdf::PDFAppToolPendingList* pendingList = nullptr;
    QToolButton* selectAllAction = nullptr;
    QStandardItemModel pendingModel;
    int parameterChanges = 0;
    int runStates = 0;
    int documentChanges = 0;
    std::vector<const pdf::PDFDocumentToolResult*> previews;
    QStringList previewFacts;

    virtual QString getInitialSidebarPanel() const override { return m_initialPanel; }
    virtual int getRightPanelSpacing() const override { return 16; }
    virtual void onParametersChanged(const QString& name) override;
    virtual void onPreviewChanged(const pdf::PDFDocumentToolResult* preview) override;
    virtual void onRunStateChanged(bool isRunning) override;
    virtual void onDocumentChanged() override;

    void selectAllSteps();

protected:
    virtual void describeToolbar(pdf::PDFAppToolPanelBuilder& builder) override;
    virtual void describeRightPanel(pdf::PDFAppToolPanelBuilder& builder) override;
    virtual void describeLeftPanel(pdf::PDFAppToolPanelBuilder& builder) override;

private:
    bool m_hasLeftPanel = false;
    bool m_onlySegments = false;
    QString m_initialPanel;
};

/// Factory of ShellTestGUI
class ShellTestGUIFactory : public pdf::PDFDocumentToolGUIFactory
{
public:
    explicit ShellTestGUIFactory(bool hasLeftPanel, const QString& initialPanel = QString(), bool onlySegments = false) : m_hasLeftPanel(hasLeftPanel), m_onlySegments(onlySegments), m_initialPanel(initialPanel) { }

    virtual pdf::PDFDocumentToolGUI* createGUI() const override { return new ShellTestGUI(m_hasLeftPanel, m_initialPanel, m_onlySegments); }

private:
    bool m_hasLeftPanel = false;
    bool m_onlySegments = false;
    QString m_initialPanel;
};

/// Fixture of the tests of the shell of the tools (phase 2): the fixture of the Tools
/// catalog with the test tool in every result kind. The test tool takes the id of a
/// tool of the catalog, so a dialog of a tool of the kind and of the signature impact
/// of the catalog is opened with its facts given by the test. Used also by the shell of
/// the next waves.
class AppToolShellFixture : public AppToolsFixture
{
public:
    explicit AppToolShellFixture(QSize size = QSize(1440, 960));
    ~AppToolShellFixture();

    /// Parameters of a mode of the test tool
    static QVariantMap parameters(const QString& mode, const QString& title = QStringLiteral("Result title"));

    /// Registers the test tool under the id of a tool of the catalog with its GUI; what
    /// was registered under the id comes back with the destructor of the fixture
    std::shared_ptr<ShellTestTool> registerShellTool(const QString& toolId, bool hasLeftPanel = false, const QString& initialPanel = QString(), bool onlySegments = false);

    /// Navigates the tab to the tool and returns its shell, nullptr = the tool has not been entered
    pdf::PDFDocumentToolShell* openTool(pdf::PDFAppDocumentTab* tab, const QString& toolId);

    /// The shell of the page of the tab, nullptr = none
    static pdf::PDFDocumentToolShell* getShell(pdf::PDFAppDocumentTab* tab);

    /// The test GUI of the shell
    static ShellTestGUI* getGUI(pdf::PDFDocumentToolShell* shell);

    /// Waits for the state of the shell
    static bool waitForState(pdf::PDFDocumentToolShell* shell, pdf::PDFDocumentToolShell::State state);

    /// Waits until the top-level widget of the type is open, nullptr = none came
    template<typename T>
    static T* waitForWidget()
    {
        T* found = nullptr;
        waitFor([&found]()
        {
            for (QWidget* widget : QApplication::topLevelWidgets())
            {
                T* candidate = qobject_cast<T*>(widget);
                if (candidate && candidate->isVisible())
                {
                    found = candidate;
                    return true;
                }
            }
            return false;
        });
        return found;
    }

    /// Waits until the condition holds (the time limit scales by PDF4QT_TEST_TIMEOUT_FACTOR)
    static bool waitFor(const std::function<bool()>& condition, int milliseconds = 15000);

    /// Waits until a run of the runner has ended (state Idle or Waiting)
    static bool waitForRun(const pdf::PDFDocumentToolRunner* runner);

    /// Runs the tool on the document of the tab and waits for its result. \returns the
    /// result by \p result; false, when the run did not end by the state \p state.
    bool runTool(pdf::PDFDocumentToolRunner* runner, const QVariantMap& parameters, pdf::PDFDocumentToolResult* result,
                 pdf::PDFDocumentToolRunner::State state = pdf::PDFDocumentToolRunner::State::Waiting);

    /// A runner of the tool of the id \p toolId (a child of the tab) with the tool by \p tool
    pdf::PDFDocumentToolRunner* createRunner(pdf::PDFAppDocumentTab* tab, const QString& toolId, std::shared_ptr<TestDocumentTool>* tool = nullptr);

    /// Opens the modal dialog of a finished run of the tool of \p toolId in the mode
    /// \p mode, with the facts \p facts, the title \p title of a tool, which says more than
    /// "<Tool> finished", and the canvas of the tab as its target
    pdf::PDFAppToolResultDialog* openDialog(pdf::PDFAppDocumentTab* tab, const QString& toolId, const QString& mode, const QStringList& facts = QStringList(), pdf::PDFDocumentToolRunner** runner = nullptr, const QString& title = QString());

    /// The card of the dialog in the coordinates of the dialog (the window without its
    /// shadow margins)
    static QRect getCardRect(const pdf::PDFAppToolResultDialog* dialog);

    /// Picture of a widget at the device pixel ratio over the canvas color
    static QImage render(QWidget* widget, qreal devicePixelRatio);

    /// Number of the pixels of the leak color: a filled area and also a text in the leak
    /// color, whose glyphs are blends of it with the background (leakDetectorSeesText)
    static int countLeakPixels(const QImage& image);

    /// Writes the picture of the dialog as <name>.png and <name>-2x.png into the
    /// folder PDF4QT_APP_SCREENSHOTS (nothing without the variable)
    static void screenshotDialog(QWidget* dialog, const QString& name);

private:
    struct RegisteredTool
    {
        QString id;
        std::shared_ptr<const pdf::PDFDocumentTool> previousTool;
        std::shared_ptr<const pdf::PDFDocumentToolGUIFactory> previousFactory;
    };

    std::vector<RegisteredTool> m_registeredTools;
};

}   // namespace pdftest

#endif // APPTOOLSHELLFIXTURE_H
