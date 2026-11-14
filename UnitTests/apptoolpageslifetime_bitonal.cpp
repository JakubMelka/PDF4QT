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

// The lifetime scenarios of Convert to Black & White (step 4 of phase 2, package P3; plan 8): a tile
// of the left panel, a mode of the selected item (one undo step), the method, the preview job, the
// detection of blank pages with its question (307:6250), the run and its result dialog (307:6564),
// the question of a partial result before the dialog (307:6388, PDFDocumentToolGUI::confirmResult)

#include "apptoolpageslifetime.h"
#include "mergetestpdf.h"

#include "pdfapptoolgui_bitonal.h"
#include "pdfapptoolpagegrid.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptoolresultdialog.h"
#include "pdfapptooltoolbar.h"
#include "pdfdocumenttoolshell.h"
#include "pdfmessagebox.h"

#include <QApplication>
#include <QFile>
#include <QPushButton>
#include <QToolButton>
#include <QtTest>

/// Helpers of the scenarios of package P3
class BitonalLifetimeHelper
{
public:
    BitonalLifetimeHelper() = delete;

    using Steps = ToolPagesLifetimeSteps;

    static pdf::PDFAppToolGUIBitonal* gui(const ToolPagesLifetimeRun& run)
    {
        pdf::PDFDocumentToolShell* shell = Steps::shell(run);
        return shell ? qobject_cast<pdf::PDFAppToolGUIBitonal*>(shell->getGUI()) : nullptr;
    }

    /// A click of the tile of a page in the grid of the left panel
    static bool clickTile(const ToolPagesLifetimeRun& run, int page)
    {
        pdf::PDFAppToolGUIBitonal* current = gui(run);
        pdf::PDFAppToolPageGridView* view = current && current->getPageGrid() ? current->getPageGrid()->getView() : nullptr;
        if (!view || !view->model() || page >= view->model()->rowCount())
        {
            return false;
        }

        QPointer<pdf::PDFAppToolPageGridView> guard(view);
        view->scrollTo(view->model()->index(page, 0));
        Steps::settle();
        if (!guard)
        {
            return true;
        }
        QTest::mouseClick(guard->viewport(), Qt::LeftButton, Qt::NoModifier, guard->visualRect(guard->model()->index(page, 0)).center());
        Steps::settle();
        return true;
    }

    /// The mode of the selected items (the row of the menu Mode does the same)
    static bool setMode(const ToolPagesLifetimeRun& run, pdf::PDFBitonalDocumentCreator::ItemMode mode)
    {
        pdf::PDFAppToolGUIBitonal* current = gui(run);
        if (!current)
        {
            return false;
        }
        current->setModeToItems(mode);
        Steps::settle();
        return true;
    }

    static bool setValue(const ToolPagesLifetimeRun& run, const QString& name, const QVariant& value)
    {
        pdf::PDFDocumentToolShell* shell = Steps::shell(run);
        QPointer<pdf::PDFAppToolParameterModel> model = shell ? shell->getModel() : nullptr;
        if (!model)
        {
            return false;
        }
        model->setValue(name, value);
        Steps::settle();
        return true;
    }

    /// Waits for the preview of the current item (its job ends; the victim may die meanwhile)
    static bool waitForPreview(const ToolPagesLifetimeRun& run)
    {
        Steps::wait(run, [&run]() { pdf::PDFAppToolGUIBitonal* current = gui(run); return !current || !current->isPreviewPending(); });
        Steps::settle();
        return true;
    }

    /// The detection by its button, then Replace with white fill of the question of 307:6250
    static bool detectAndReplace(const ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppToolGUIBitonal* current = gui(run);
        if (!current || !Steps::click(current->getBlankPagesButton()))
        {
            return false;
        }

        Steps::wait(run, [&run]() { pdf::PDFAppToolGUIBitonal* now = gui(run); return !now || (now->getQuestion() && now->getQuestion()->isVisible()); });
        current = gui(run);
        pdf::PDFMessageBox* question = current ? current->getQuestion() : nullptr;
        if (question)
        {
            // The three plain pages of the scenario are blank: the question asks about them
            QPushButton* button = question->findChild<QPushButton*>(QStringLiteral("bitonalBlankReplace"));
            if (!button)
            {
                button = question->findChild<QPushButton*>();
            }
            Steps::click(button);
        }
        Steps::settle();
        return true;
    }

    /// One page with two images, the second cannot be decoded (FlateDecode over no compressed
    /// data): Black & White of the images converts one of them, the run is partial (307:6388)
    static pdf::PDFAppDocumentTab* openPartial(pdftest::AppToolShellFixture& fixture)
    {
        const QByteArray pixels("\x00\x00\x00\xFF\xFF\xFF", 6);
        const QByteArray image = "/Type /XObject /Subtype /Image /Width 2 /Height 1 /BitsPerComponent 8 /ColorSpace /DeviceRGB";
        mergetest::MergeTestPdf pdf = mergetest::MergeTestPdf::createPages("P", 1, QByteArray(), { { 1, "/Resources << /XObject << /Im1 10 0 R /Im2 11 0 R >> >>" } });
        pdf.set(10, mergetest::MergeTestPdf::stream(pixels, image));
        pdf.set(11, mergetest::MergeTestPdf::stream("this is not a compressed image stream at all", image + " /Filter /FlateDecode"));
        pdf.set(101, mergetest::MergeTestPdf::stream("q 200 0 0 100 50 600 cm /Im1 Do Q q 200 0 0 100 50 400 cm /Im2 Do Q"));
        const QByteArray bytes = pdf.write();
        const QString path = fixture.folder() + QStringLiteral("/bitonal-partial.pdf");
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        {
            return nullptr;
        }
        file.close();
        return fixture.open(path);
    }

    /// Source: Images, the list collected, then the Primary button and the question of the partial
    /// result (307:6388) answered by its button \p buttonName; Use partial document closes the
    /// result dialog after it
    static bool runPartial(ToolPagesLifetimeRun& run, const QString& buttonName)
    {
        if (!setValue(run, QStringLiteral("source"), QStringLiteral("images")))
        {
            return false;
        }
        Steps::wait(run, [&run]() { pdf::PDFAppToolGUIBitonal* current = gui(run); return !current || (!current->isCollectingImages() && current->getItemCount() > 0); });
        Steps::settle();

        pdf::PDFAppToolToolbar* bar = Steps::toolbar(run);
        if (!bar)
        {
            return true;
        }
        Steps::click(bar->getConfirmButton());
        Steps::wait(run, [&run]()
        {
            pdf::PDFAppToolGUIBitonal* current = gui(run);
            return !current || (current->getQuestion() && current->getQuestion()->isVisible());
        });

        pdf::PDFAppToolGUIBitonal* current = gui(run);
        pdf::PDFMessageBox* question = current ? current->getQuestion() : nullptr;
        if (question)
        {
            Steps::click(question->findChild<QPushButton*>(buttonName));
        }
        Steps::settle();

        pdf::PDFDocumentToolShell* shell = Steps::shell(run);
        if (shell && buttonName == QLatin1String("bitonalPartialUse"))
        {
            Steps::wait(run, [&run]() { pdf::PDFDocumentToolShell* now = Steps::shell(run); return !now || now->getResultDialog(); });
            shell = Steps::shell(run);
            if (pdf::PDFAppToolResultDialog* dialog = shell ? shell->getResultDialog() : nullptr)
            {
                dialog->reject();
            }
            Steps::settle();
        }
        return true;
    }
};

void ToolPagesLifetimePackages::registerBitonalScenarios(std::vector<ToolPagesLifetimeScenario>* scenarios)
{
    using H = BitonalLifetimeHelper;
    using Steps = ToolPagesLifetimeSteps;
    using Mode = pdf::PDFBitonalDocumentCreator::ItemMode;

    // Black & White: a tile, the inverted mode, the method, the preview, the detection with its
    // question, the run to the result dialog (the preselected Save as a copy writes nothing here:
    // the dialog is closed)
    ToolPagesLifetimeScenario bitonal;
    bitonal.name = QStringLiteral("bitonal");
    bitonal.toolId = QStringLiteral("optimize.bitonal");
    bitonal.hasResultDialog = true;
    bitonal.hasPanel = true;
    bitonal.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
    {
        steps->push_back(&Steps::openTool);
        steps->push_back([](ToolPagesLifetimeRun& run) { return H::clickTile(run, 1); });
        steps->push_back([](ToolPagesLifetimeRun& run) { return H::setMode(run, Mode::AlgorithmInverted); });
        steps->push_back([](ToolPagesLifetimeRun& run) { return H::setValue(run, QStringLiteral("method"), QStringLiteral("threshold")); });
        steps->push_back([](ToolPagesLifetimeRun& run) { return H::waitForPreview(run); });
        steps->push_back([](ToolPagesLifetimeRun& run) { return H::detectAndReplace(run); });
        steps->push_back([](ToolPagesLifetimeRun& run) { return H::setValue(run, QStringLiteral("dpi"), 72); });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppToolResultDialog* dialog = Steps::confirmToDialog(run);
            if (dialog)
            {
                dialog->reject();
            }
            Steps::settle();
            return true;
        });
    };
    scenarios->push_back(bitonal);

    // The source Images: the list of the images is collected in a worker, the tool closes meanwhile
    ToolPagesLifetimeScenario images;
    images.name = QStringLiteral("bitonalimages");
    images.toolId = QStringLiteral("optimize.bitonal");
    images.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
    {
        steps->push_back(&Steps::openTool);
        steps->push_back([](ToolPagesLifetimeRun& run) { return H::setValue(run, QStringLiteral("source"), QStringLiteral("images")); });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            Steps::wait(run, [&run]() { pdf::PDFAppToolGUIBitonal* current = H::gui(run); return !current || !current->isCollectingImages(); });
            Steps::settle();
            return true;
        });
        steps->push_back([](ToolPagesLifetimeRun& run) { return H::setValue(run, QStringLiteral("source"), QStringLiteral("pages")); });
    };
    scenarios->push_back(images);

    // The partial result (307:6388): the question before the result dialog is open while the tab,
    // the tool or the GUI goes; Use partial document leads to the dialog, No, discard leaves the tool
    ToolPagesLifetimeScenario partialUse;
    partialUse.name = QStringLiteral("bitonalpartialuse");
    partialUse.toolId = QStringLiteral("optimize.bitonal");
    partialUse.openDocument = &H::openPartial;
    partialUse.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
    {
        steps->push_back(&Steps::openTool);
        steps->push_back([](ToolPagesLifetimeRun& run) { return H::runPartial(run, QStringLiteral("bitonalPartialUse")); });
    };
    scenarios->push_back(partialUse);

    ToolPagesLifetimeScenario partialDiscard;
    partialDiscard.name = QStringLiteral("bitonalpartialdiscard");
    partialDiscard.toolId = QStringLiteral("optimize.bitonal");
    partialDiscard.openDocument = &H::openPartial;
    partialDiscard.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
    {
        steps->push_back(&Steps::openTool);
        steps->push_back([](ToolPagesLifetimeRun& run) { return H::runPartial(run, QStringLiteral("bitonalPartialDiscard")); });
    };
    scenarios->push_back(partialDiscard);
}
