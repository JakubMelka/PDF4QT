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

#ifndef APPTOOLPAGESLIFETIME_H
#define APPTOOLPAGESLIFETIME_H

// The part of UnitTestsAppToolPagesLifetime shared with the files of the packages of step 3a of
// phase 2 (plan 8, 9.1): each package registers the scenarios of its tools in its own file
// (apptoolpageslifetime_<package>.cpp) by its registration function; the test runs every
// registered scenario over all sites as its own ones (deleteFromEverySignal, rows
// "<scenario>-<victim>-<part>"; PDF4QT_LIFETIME_SITE=<first>-<last>) and keeps its result
// dialog and its first panel open over a change of the language, the document and the theme
// (openPopupsSurviveChanges, rows "dialog-<scenario>-..." and "panel-<scenario>-...").

#include "apptoolowntabfixture.h"
#include "apptoolshellfixture.h"

#include "pdfappdocumenttab.h"
#include "pdfappmainwindow.h"
#include "pdfapptooltab.h"

#include <QPointer>
#include <QString>
#include <QToolButton>

#include <functional>
#include <memory>
#include <vector>

namespace pdf
{
class PDFAppToolResultDialog;
class PDFAppToolToolbar;
class PDFDocumentToolShell;
}

/// State of one run of a scenario
struct ToolPagesLifetimeRun
{
    pdftest::AppToolOwnTabFixture* fixture = nullptr;
    std::shared_ptr<pdftest::ShellTestTool> tool;
    QPointer<pdf::PDFAppMainWindow> window;
    QPointer<pdf::PDFAppDocumentTab> tab;
    QString scenario;       ///< The name of the scenario (its prepareRun: more scenarios share a tool id)
    QString toolId;
    QPointer<pdf::PDFAppToolTab> toolTab;               ///< The own tab of a tool (the scenarios "owntab*", step 2 of phase 2)
    std::shared_ptr<pdftest::OwnTabTestTool> ownTabTool;
};

/// A step of a scenario: false = the step failed (the discovery run fails); the victim may be
/// deleted inside of any event of the step
using ToolPagesLifetimeStep = std::function<bool(ToolPagesLifetimeRun&)>;

/// A scenario of a real tool registered by the file of its package
class ToolPagesLifetimeScenario
{
public:
    QString name;           ///< Name of the rows ("split"); unique, no '-'
    QString toolId;         ///< The tool of the catalog ("pages.split")
    bool isOwnTab = false;  ///< The tool has its own tab (its steps open it and set ToolPagesLifetimeRun::toolTab)

    /// The document of the scenario; empty = three plain pages
    std::function<pdf::PDFAppDocumentTab*(pdftest::AppToolShellFixture&)> openDocument;

    /// The steps of the scenario (the first opens the tool, ToolPagesLifetimeSteps::openTool)
    std::function<void(std::vector<ToolPagesLifetimeStep>*)> addSteps;

    /// Brings the tool to a state, where its Primary button runs it (openPopupsSurviveChanges);
    /// empty = nothing to prepare
    std::function<bool(ToolPagesLifetimeRun&)> prepareRun;

    bool hasResultDialog = false;   ///< Rows "dialog-<name>" of openPopupsSurviveChanges
    bool hasPanel = false;          ///< Rows "panel-<name>" (the first panel button of the toolbar)
};

/// The scenarios of the packages of step 3a: each registration function is defined in the file of
/// its package and appends its scenarios
class ToolPagesLifetimePackages
{
public:
    ToolPagesLifetimePackages() = delete;

    /// Every registered scenario in the order of the packages A, B, C, D
    static const std::vector<ToolPagesLifetimeScenario>& getScenarios();

    /// The scenario of the name, nullptr = none (a scenario of the test itself)
    static const ToolPagesLifetimeScenario* findScenario(const QString& name);

    /// Package A (Split, Extract Pages): apptoolpageslifetime_split.cpp
    static void registerSplitScenarios(std::vector<ToolPagesLifetimeScenario>* scenarios);

    /// Package B (Crop, Resize, Set Page Boxes): apptoolpageslifetime_geometry.cpp
    static void registerGeometryScenarios(std::vector<ToolPagesLifetimeScenario>* scenarios);

    /// Package C (Insert Pages, Add Image as Page): apptoolpageslifetime_insert.cpp
    static void registerInsertScenarios(std::vector<ToolPagesLifetimeScenario>* scenarios);

    /// Package D (Images to PDF, Interleave Pages): apptoolpageslifetime_images.cpp
    static void registerImageScenarios(std::vector<ToolPagesLifetimeScenario>* scenarios);

    /// Step 3b, package M3 (Manage Pages in its own tab and in the tab of a document; plan of
    /// step 3b, 8: managegui, managedocgui, managedialogs, managemissing, manageclose,
    /// manageconflict, manageclipboard, managecheckpoint, managedocchange, manageopenpopups):
    /// apptoolpageslifetime_manage.cpp
    static void registerManageScenarios(std::vector<ToolPagesLifetimeScenario>* scenarios);
};

/// The steps and the inputs of the test for the scenarios of the packages: every input is sent
/// through guards, the victim may be deleted inside of any of them (defined by the test by its
/// helpers; a GUI of a tool is reached by qobject_cast of shell(run)->getGUI())
class ToolPagesLifetimeSteps
{
public:
    ToolPagesLifetimeSteps() = delete;

    /// Processes the pending events
    static void settle();

    /// The shell of the tool (of its own tab or of the document tab), nullptr = none
    static pdf::PDFDocumentToolShell* shell(const ToolPagesLifetimeRun& run);

    /// The toolbar of the tool, nullptr = none
    static pdf::PDFAppToolToolbar* toolbar(const ToolPagesLifetimeRun& run);

    /// Opens the tool of the run in the document tab
    static bool openTool(ToolPagesLifetimeRun& run);

    /// A click, a key on a widget (false for nullptr)
    static bool click(QWidget* widget);
    static bool key(QWidget* widget, int keyCode, Qt::KeyboardModifiers modifiers = Qt::NoModifier);

    /// Waits for a condition, while the run has its window and its tab (the time limit of the
    /// fixture); false, when the window or the tab is gone
    static bool wait(const ToolPagesLifetimeRun& run, const std::function<bool()>& condition);

    /// Answers the dialog of the leaving (PDFAppDocumentChoiceDialog) by its button (the object
    /// name) from the event loop
    static void answerLater(const ToolPagesLifetimeRun& run, const QString& buttonName);

    /// Presses the Primary button by the mouse and waits for the result dialog, nullptr = none
    static pdf::PDFAppToolResultDialog* confirmToDialog(ToolPagesLifetimeRun& run);

    /// The confirmation button of the result dialog by the mouse, then the end of the shell
    static bool confirmDialog(ToolPagesLifetimeRun& run, pdf::PDFAppToolResultDialog* dialog);

    /// A click on the tile of the page in the grid of a page marking tool (PDFAppPageMarkingToolGUI)
    static bool clickTile(const ToolPagesLifetimeRun& run, int page, Qt::KeyboardModifiers modifiers = Qt::NoModifier);

    /// The first panel button of the toolbar of the tool, nullptr = none
    static QToolButton* firstPanelButton(const ToolPagesLifetimeRun& run);
};

#endif // APPTOOLPAGESLIFETIME_H
