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

// The lifetime scenarios of PDF to Images and of the written files of the result dialog (step 4 of
// phase 2, package P1; plan 8): the thumbnail of the preview, the menus Format (More formats) and
// the resolution, Undo, the estimate of the worker, the run, the dialog with Rendering issues and
// Close; a file, which cannot be written, and Retry N Files (339:7293, K4.11).

#include "apptoolpageslifetime.h"

#include "pdfapptoolgui_pdftoimages.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptoolresultdialog.h"
#include "pdfapptooltoolbar.h"
#include "pdfdocumenttoolshell.h"

#include "pdfappfiledrop.h"

#include "pdfcomponentrows.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumenttool_split.h"
#include "pdfdocumentwriter.h"

#include <QDir>
#include <QScopeGuard>
#include <QtTest>

/// Helpers of the scenarios of package P1
class ExportLifetimeHelper
{
public:
    ExportLifetimeHelper() = delete;

    using Steps = ToolPagesLifetimeSteps;

    static pdf::PDFAppToolGUIPdfToImages* gui(const ToolPagesLifetimeRun& run)
    {
        pdf::PDFDocumentToolShell* shell = Steps::shell(run);
        return shell ? qobject_cast<pdf::PDFAppToolGUIPdfToImages*>(shell->getGUI()) : nullptr;
    }

    /// The folder of the files of a run (in the folder of the fixture, removed with it)
    static QString getFolder(const ToolPagesLifetimeRun& run)
    {
        if (!run.fixture)
        {
            return QString();
        }
        const QString folder = QDir(run.fixture->folder()).filePath(QStringLiteral("images-output"));
        QDir().mkpath(folder);
        return folder;
    }

    /// The folder of the files, Overwrite, 72 dpi (a small image per page)
    static bool setOutput(ToolPagesLifetimeRun& run)
    {
        pdf::PDFDocumentToolShell* shell = Steps::shell(run);
        QPointer<pdf::PDFAppToolParameterModel> model = shell ? shell->getModel() : nullptr;
        const QString folder = getFolder(run);
        if (!model || folder.isEmpty())
        {
            return false;
        }
        QVariantMap values;
        values.insert(QString::fromLatin1(pdf::PDFDocumentToolFileOutput::PARAMETER_FOLDER), folder);
        values.insert(QString::fromLatin1(pdf::PDFDocumentToolFileOutput::PARAMETER_EXISTING_FILES), QString::fromLatin1(pdf::PDFDocumentToolFileOutput::EXISTING_OVERWRITE));
        values.insert(QStringLiteral("dpi"), 72);
        model->setValues(values);
        Steps::settle();
        return true;
    }

    /// The menu Format by the mouse: More formats, then a format of every format
    static bool chooseFormat(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppToolGUIPdfToImages* current = gui(run);
        if (!current || !Steps::click(current->getFormatButton()))
        {
            return false;
        }
        current = gui(run);
        pdf::PDFAppToolPanelPopup* panel = current ? current->getFormatPanel() : nullptr;
        if (!panel || !Steps::click(panel->findChild<pdf::PDFTileRow*>(QStringLiteral("toolFormat-more"))))
        {
            return false;
        }
        current = gui(run);
        panel = current ? current->getFormatPanel() : nullptr;
        return panel && Steps::click(panel->findChild<pdf::PDFTileRow*>(QStringLiteral("toolAllFormat-jpeg")));
    }

    /// The menu of the resolution: 150 dpi
    static bool chooseResolution(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppToolGUIPdfToImages* current = gui(run);
        if (!current || !Steps::click(current->getResolutionButton()))
        {
            return false;
        }
        current = gui(run);
        pdf::PDFAppToolPanelPopup* panel = current ? current->getResolutionPanel() : nullptr;
        return panel && Steps::click(panel->findChild<pdf::PDFTileRow*>(QStringLiteral("toolResolution-150")));
    }

    /// Waits for the estimate of the worker (the status bar says it)
    static bool waitEstimate(ToolPagesLifetimeRun& run)
    {
        Steps::wait(run, [&run]() { pdf::PDFAppToolGUIPdfToImages* current = gui(run); return !current || !current->isEstimating(); });
        return true;
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

    /// A folder in the place of the file of the second page: the file cannot be written (339:7293)
    static bool blockSecondFile(ToolPagesLifetimeRun& run)
    {
        const QString folder = getFolder(run);
        const pdf::PDFAppToolGUIPdfToImages* current = gui(run);
        if (folder.isEmpty() || !current || current->getFileNames().size() < 2)
        {
            return false;
        }
        return QDir(folder).mkpath(current->getFileNames().at(1));
    }

    /// Three A4 pages, the content of the second names a missing image: the run finishes with
    /// "Rendering issues" (339:6505)
    static pdf::PDFAppDocumentTab* openBrokenPage(pdftest::AppToolShellFixture& fixture)
    {
        pdf::PDFDocumentBuilder builder;
        for (int i = 0; i < 3; ++i)
        {
            const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 595.28, 841.89));
            const QByteArray content = i == 1 ? QByteArray("/Missing Do") : QByteArray("0 0 1 rg 100 100 200 200 re f");
            pdf::PDFDictionaryBuilder dictionary;
            dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Length"), pdf::PDFObject::createInteger(content.size()));
            const pdf::PDFObjectReference contents = builder.addObject(pdf::PDFObject::createStream(pdf::PDFStream(std::move(dictionary), QByteArray(content))));
            pdf::PDFDictionaryBuilder update;
            update.addEntry(pdf::PDFInplaceOrMemoryString("Contents"), pdf::PDFObject::createReference(contents));
            builder.mergeTo(page, pdf::PDFObject::createDictionary(std::move(update)));
        }
        pdf::PDFDocument document = builder.build();
        const QString path = fixture.folder() + QStringLiteral("/lifetime-broken.pdf");
        if (!pdf::PDFDocumentWriter(nullptr).write(path, &document, false))
        {
            return nullptr;
        }
        pdf::PDFAppDocumentTab* tab = fixture.open(path);
        if (!tab || !pdftest::AppToolShellFixture::waitFor([tab]() { return tab->getDocument() && tab->getDocument()->isReady(); }))
        {
            return nullptr;
        }
        return tab;
    }

    /// Choose Folder… of the dialog (339:7293, K4.11) with a folder of the fixture as the answer of
    /// the question; the dialog of the run of the failed files is closed
    static bool chooseFolder(ToolPagesLifetimeRun& run, pdf::PDFAppToolResultDialog* dialog)
    {
        if (!dialog || !run.fixture || !dialog->getChooseFolderButton())
        {
            return false;
        }
        const QString otherFolder = QDir(run.fixture->folder()).filePath(QStringLiteral("images-other"));
        QDir().mkpath(otherFolder);
        pdf::PDFAppFileDialogs::setFolderHandler([otherFolder](QWidget*, const QString&) { return otherFolder; });
        const auto restore = qScopeGuard([]() { pdf::PDFAppFileDialogs::setFolderHandler(pdf::PDFAppFileDialogs::FolderHandler()); });
        if (!Steps::click(dialog->getChooseFolderButton()))
        {
            return false;
        }
        Steps::wait(run, [&run]() { pdf::PDFDocumentToolShell* current = Steps::shell(run); return !current || current->getState() == pdf::PDFDocumentToolShell::State::Deciding; });
        pdf::PDFDocumentToolShell* shell = Steps::shell(run);
        return !shell || !shell->getResultDialog() || closeDialog(run, shell->getResultDialog());
    }

    /// Retry N Files of the dialog, then the dialog of the run of the retry is closed
    static bool retry(ToolPagesLifetimeRun& run, pdf::PDFAppToolResultDialog* dialog)
    {
        if (!dialog || !Steps::click(dialog->getRetryButton()))
        {
            return false;
        }
        Steps::wait(run, [&run]() { pdf::PDFDocumentToolShell* current = Steps::shell(run); return !current || current->getResultDialog(); });
        pdf::PDFDocumentToolShell* shell = Steps::shell(run);
        return !shell || !shell->getResultDialog() || closeDialog(run, shell->getResultDialog());
    }
};

void ToolPagesLifetimePackages::registerExportScenarios(std::vector<ToolPagesLifetimeScenario>* scenarios)
{
    using H = ExportLifetimeHelper;
    using Steps = ToolPagesLifetimeSteps;

    // PDF to Images: the menus, Undo, the estimate, the run and the dialog of the written files
    ToolPagesLifetimeScenario images;
    images.name = QStringLiteral("pdftoimages");
    images.toolId = QStringLiteral("convert.pdf-to-images");
    images.hasResultDialog = true;
    images.hasPanel = true;
    images.prepareRun = &H::setOutput;
    images.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
    {
        steps->push_back(&Steps::openTool);
        steps->push_back(&H::setOutput);
        steps->push_back(&H::chooseFormat);
        steps->push_back(&H::chooseResolution);
        steps->push_back([](ToolPagesLifetimeRun& run) { return Steps::click(Steps::toolbar(run) ? Steps::toolbar(run)->getUndoButton() : nullptr); });
        steps->push_back(&H::waitEstimate);
        steps->push_back([](ToolPagesLifetimeRun& run) { return H::closeDialog(run, Steps::confirmToDialog(run)); });
    };
    scenarios->push_back(images);

    // A file, which cannot be written: the dialog finished with errors and Retry N Files
    ToolPagesLifetimeScenario retry;
    retry.name = QStringLiteral("pdftoimagesretry");
    retry.toolId = QStringLiteral("convert.pdf-to-images");
    retry.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
    {
        steps->push_back(&Steps::openTool);
        steps->push_back(&H::setOutput);
        steps->push_back(&H::blockSecondFile);
        steps->push_back([](ToolPagesLifetimeRun& run) { return H::retry(run, Steps::confirmToDialog(run)); });
    };
    scenarios->push_back(retry);

    // RS4APP lifetime gaps: a page with an error (the dialog with Rendering issues)
    ToolPagesLifetimeScenario issues;
    issues.name = QStringLiteral("pdftoimagesissues");
    issues.toolId = QStringLiteral("convert.pdf-to-images");
    issues.openDocument = &H::openBrokenPage;
    issues.hasResultDialog = true;
    issues.prepareRun = &H::setOutput;
    issues.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
    {
        steps->push_back(&Steps::openTool);
        steps->push_back(&H::setOutput);
        steps->push_back([](ToolPagesLifetimeRun& run) { return H::closeDialog(run, Steps::confirmToDialog(run)); });
    };
    scenarios->push_back(issues);

    // ... and Choose Folder… of the dialog of a file, which cannot be written
    ToolPagesLifetimeScenario chooseFolder;
    chooseFolder.name = QStringLiteral("pdftoimageschoose");
    chooseFolder.toolId = QStringLiteral("convert.pdf-to-images");
    chooseFolder.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
    {
        steps->push_back(&Steps::openTool);
        steps->push_back(&H::setOutput);
        steps->push_back(&H::blockSecondFile);
        steps->push_back([](ToolPagesLifetimeRun& run) { return H::chooseFolder(run, Steps::confirmToDialog(run)); });
    };
    scenarios->push_back(chooseFolder);
}
