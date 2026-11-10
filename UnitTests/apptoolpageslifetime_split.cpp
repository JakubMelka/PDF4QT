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

// The lifetime scenarios of package A of step 3a of phase 2 (Split, Extract Pages): the close of
// the tab while the files are written, the question of an existing file (356:11997), the result
// dialog R3 (plan 6, 8)

#include "apptoolpageslifetime.h"

#include "pdfapptoolgui_split.h"
#include "pdfapptooloutputfiles.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptoolresultdialog.h"
#include "pdfdocumenttoolshell.h"

#include "pdfdocumenttool_split.h"

#include <QDir>
#include <QFile>
#include <QtTest>

/// Helpers of the scenarios of package A
class SplitLifetimeHelper
{
public:
    SplitLifetimeHelper() = delete;

    using Steps = ToolPagesLifetimeSteps;

    /// The folder of the files of the scenarios: a folder in the temporary folder of the fixture
    /// of this run (removed with the fixture; never a folder of the user, never shared by the
    /// processes of the runner, so Ask sees only the files of this run)
    static QString getFolder(const ToolPagesLifetimeRun& run)
    {
        if (!run.fixture)
        {
            return QString();
        }
        const QString folder = QDir(run.fixture->folder()).filePath(QStringLiteral("split-output"));
        QDir().mkpath(folder);
        return folder;
    }

    /// Writes the values of the output files into the model (one undo step each)
    static bool setOutput(const ToolPagesLifetimeRun& run, const QString& existingFiles)
    {
        pdf::PDFDocumentToolShell* shell = Steps::shell(run);
        QPointer<pdf::PDFAppToolParameterModel> model = shell ? shell->getModel() : nullptr;
        const QString folder = getFolder(run);
        if (!model || folder.isEmpty())
        {
            return false;
        }
        model->setValue(QString::fromLatin1(pdf::PDFDocumentToolFileOutput::PARAMETER_FOLDER), folder);
        if (model)
        {
            model->setValue(QString::fromLatin1(pdf::PDFDocumentToolFileOutput::PARAMETER_EXISTING_FILES), existingFiles);
        }
        Steps::settle();
        return true;
    }

    /// A file, which exists, of every name of the run (the question of Ask)
    static void createExistingFiles(const ToolPagesLifetimeRun& run)
    {
        const QString folder = getFolder(run);
        for (int i = 1; i <= 3 && !folder.isEmpty(); ++i)
        {
            QFile file(QDir(folder).filePath(QStringLiteral("ask_%1.pdf").arg(i)));
            if (!file.exists() && file.open(QIODevice::WriteOnly))
            {
                file.write("EXISTING");
            }
        }
    }

    /// The Close button of the dialog of the written files (Open Folder would open the folder)
    static bool closeDialog(ToolPagesLifetimeRun& run, pdf::PDFAppToolResultDialog* dialog)
    {
        if (!dialog || !Steps::click(dialog->getCloseButton()))
        {
            return false;
        }
        Steps::wait(run, [&run]() { pdf::PDFDocumentToolShell* current = Steps::shell(run); return !current || !current->getResultDialog(); });
        Steps::settle();
        return true;
    }
};

void ToolPagesLifetimePackages::registerSplitScenarios(std::vector<ToolPagesLifetimeScenario>* scenarios)
{
    using H = SplitLifetimeHelper;
    using Steps = ToolPagesLifetimeSteps;

    // Split: a tile, the output files (Rename), the run, the dialog of the written files (R3), Close
    ToolPagesLifetimeScenario split;
    split.name = QStringLiteral("split");
    split.toolId = QStringLiteral("pages.split");
    split.hasResultDialog = true;
    split.hasPanel = true;
    split.prepareRun = [](ToolPagesLifetimeRun& run) { return H::setOutput(run, QStringLiteral("rename")); };
    split.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
    {
        steps->push_back(&Steps::openTool);
        steps->push_back([](ToolPagesLifetimeRun& run) { return Steps::clickTile(run, 1); });
        steps->push_back([](ToolPagesLifetimeRun& run) { return H::setOutput(run, QStringLiteral("rename")); });
        steps->push_back([](ToolPagesLifetimeRun& run) { return H::closeDialog(run, Steps::confirmToDialog(run)); });
    };
    scenarios->push_back(split);

    // Split with Existing files Ask: the question of 356:11997 during the run (its Skip), the
    // dialog of the written files; the deletion of every victim while the question waits (the
    // close of the tab = Stop Export, R3)
    ToolPagesLifetimeScenario ask;
    ask.name = QStringLiteral("splitask");
    ask.toolId = QStringLiteral("pages.split");
    ask.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
    {
        steps->push_back(&Steps::openTool);
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFDocumentToolShell* shell = Steps::shell(run);
            QPointer<pdf::PDFAppToolParameterModel> model = shell ? shell->getModel() : nullptr;
            if (!model)
            {
                return false;
            }
            H::createExistingFiles(run);
            model->setValue(QStringLiteral("rule"), QStringLiteral("every-page"));
            if (model)
            {
                model->setValue(QString::fromLatin1(pdf::PDFDocumentToolFileOutput::PARAMETER_FILE_NAME), QStringLiteral("ask_#"));
            }
            return H::setOutput(run, QStringLiteral("ask"));
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            if (!Steps::click(Steps::toolbar(run) ? Steps::toolbar(run)->getConfirmButton() : nullptr))
            {
                return false;
            }
            // The question appears (the run waits), Skip answers it; a following one too
            for (int question = 0; question < 3; ++question)
            {
                QPointer<pdf::PDFAppToolFileConflictDialog> dialog;
                Steps::wait(run, [&]()
                {
                    dialog = qobject_cast<pdf::PDFAppToolFileConflictDialog*>(QApplication::activeModalWidget());
                    pdf::PDFDocumentToolShell* current = Steps::shell(run);
                    return dialog || !current || current->getResultDialog();
                });
                if (!dialog)
                {
                    break;
                }
                Steps::click(dialog->getSkipButton());
            }
            Steps::wait(run, [&run]() { pdf::PDFDocumentToolShell* current = Steps::shell(run); return !current || current->getResultDialog(); });
            pdf::PDFDocumentToolShell* current = Steps::shell(run);
            return !current || H::closeDialog(run, current->getResultDialog());
        });
    };
    scenarios->push_back(ask);

    // Extract Pages: a tile, the menu of the output, One file: the new document in a new tab
    ToolPagesLifetimeScenario extract;
    extract.name = QStringLiteral("extract");
    extract.toolId = QStringLiteral("pages.extract");
    extract.hasResultDialog = true;
    extract.hasPanel = true;
    extract.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
    {
        steps->push_back(&Steps::openTool);
        steps->push_back([](ToolPagesLifetimeRun& run) { return Steps::clickTile(run, 2); });
        steps->push_back([](ToolPagesLifetimeRun& run) { return Steps::click(Steps::firstPanelButton(run)); });
        steps->push_back([](ToolPagesLifetimeRun&) { return Steps::key(QApplication::activePopupWidget() ? QApplication::activePopupWidget() : QApplication::focusWidget(), Qt::Key_Escape); });
        steps->push_back([](ToolPagesLifetimeRun& run) { return Steps::confirmDialog(run, Steps::confirmToDialog(run)); });
    };
    scenarios->push_back(extract);
}
