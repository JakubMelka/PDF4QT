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

// The lifetime scenarios of package D of step 3a of phase 2 (Images to PDF, Interleave Pages in
// their own tabs): the reading of the inputs, the slots and Swap, the turn and the copy of an
// image, the panels of the toolbar, the run and the result dialog (plan 6, 8)

#include "apptoolpageslifetime.h"

#include "pdfapptoolgui_images.h"
#include "pdfapptoolgui_interleave.h"
#include "pdfapptoolinputlist.h"
#include "pdfapptoolresultdialog.h"
#include "pdfdocumenttoolshell.h"

#include "pdfdocumenttool_images.h"

#include <QImage>

/// Helpers of the scenarios of package D
class ToolPagesLifetimeImages
{
public:
    ToolPagesLifetimeImages() = delete;

    /// The inputs of the own tab are read
    static bool waitInputs(const ToolPagesLifetimeRun& run)
    {
        const bool isRead = ToolPagesLifetimeSteps::wait(run, [&run]()
        {
            const pdf::PDFAppToolTab* toolTab = run.toolTab ? qobject_cast<const pdf::PDFAppToolTab*>(run.toolTab.data()) : nullptr;
            const pdf::PDFAppToolInputList* list = toolTab ? toolTab->getInputList() : nullptr;
            return !list || (list->getLoadingCount() == 0 && list->getLoader()->getRunningCount() == 0);
        });
        ToolPagesLifetimeSteps::settle();
        return isRead;
    }

    /// A PNG in the folder of the fixture
    static QString png(const ToolPagesLifetimeRun& run, const QString& name, QSize size)
    {
        QImage image(size, QImage::Format_RGB32);
        image.fill(QColor(120, 160, 200));
        image.setDotsPerMeterX(11811);
        image.setDotsPerMeterY(11811);
        const QString path = run.fixture->folder() + QLatin1Char('/') + name;
        return image.save(path, "PNG") ? path : QString();
    }

    /// Opens the own tab of the tool with the files (the list is read in the tab)
    static bool openTab(ToolPagesLifetimeRun& run, const QString& toolId, const QStringList& files)
    {
        if (!run.window || !run.fixture)
        {
            return false;
        }
        pdf::PDFAppToolInputList* list = new pdf::PDFAppToolInputList(nullptr);
        list->addFiles(files);
        run.toolTab = run.window->createToolTab(toolId, list);
        ToolPagesLifetimeSteps::settle();
        return run.toolTab && waitInputs(run) && ToolPagesLifetimeSteps::shell(run) &&
               ToolPagesLifetimeSteps::shell(run)->getState() == pdf::PDFDocumentToolShell::State::Editing;
    }

    template<typename T>
    static T* gui(const ToolPagesLifetimeRun& run)
    {
        pdf::PDFDocumentToolShell* shell = ToolPagesLifetimeSteps::shell(run);
        return shell ? qobject_cast<T*>(shell->getGUI()) : nullptr;
    }

    /// Opens and closes the first panel of the toolbar
    static bool togglePanel(const ToolPagesLifetimeRun& run)
    {
        QToolButton* button = ToolPagesLifetimeSteps::firstPanelButton(run);
        if (!ToolPagesLifetimeSteps::click(button))
        {
            return false;
        }
        ToolPagesLifetimeSteps::settle();
        button = ToolPagesLifetimeSteps::firstPanelButton(run);
        return !button || ToolPagesLifetimeSteps::key(button->window(), Qt::Key_Escape);
    }

    /// The run, its result dialog and its confirmation; the own tab closes or is replaced
    static void addRunSteps(std::vector<ToolPagesLifetimeStep>* steps)
    {
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            return ToolPagesLifetimeSteps::confirmToDialog(run) != nullptr;
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFDocumentToolShell* shell = ToolPagesLifetimeSteps::shell(run);
            pdf::PDFAppToolResultDialog* dialog = shell ? shell->getResultDialog() : nullptr;
            if (!dialog || !ToolPagesLifetimeSteps::confirmDialog(run, dialog))
            {
                return false;
            }
            ToolPagesLifetimeSteps::wait(run, [&run]() { return run.window && run.window->getToolTabs().isEmpty(); });
            return run.window && run.window->getToolTabs().isEmpty();
        });
    }
};

void ToolPagesLifetimePackages::registerImageScenarios(std::vector<ToolPagesLifetimeScenario>* scenarios)
{
    using Images = ToolPagesLifetimeImages;

    // Interleave Pages (385:13263): two scans, Swap, the panel Back sides, Create PDF, Open in a
    // new tab (the tab closes, K31)
    ToolPagesLifetimeScenario interleave;
    interleave.name = QStringLiteral("interleavegui");
    interleave.toolId = QString::fromLatin1(pdf::PDFDocumentToolInterleave::TOOL_ID);
    interleave.isOwnTab = true;
    // openPopupsSurviveChanges: prepareRun opens the own tab (the test opens no tool in the
    // document tab of an own-tab scenario), the result dialog and the first panel stay open over
    // a change of the language, the document and the theme
    interleave.hasResultDialog = true;
    interleave.hasPanel = true;
    interleave.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
    {
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            const QString front = pdftest::AppDocumentSamples::simple(run.fixture->folder(), QStringLiteral("interleave-front.pdf"), 3);
            const QString back = pdftest::AppDocumentSamples::simple(run.fixture->folder(), QStringLiteral("interleave-back.pdf"), 2);
            return Images::openTab(run, QString::fromLatin1(pdf::PDFDocumentToolInterleave::TOOL_ID), { front, back }) &&
                   Images::gui<pdf::PDFAppInterleaveToolGUI>(run);
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppInterleaveToolGUI* current = Images::gui<pdf::PDFAppInterleaveToolGUI>(run);
            if (!current || !ToolPagesLifetimeSteps::click(current->getSwapButton()))
            {
                return false;
            }
            ToolPagesLifetimeSteps::settle();
            return true;
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            return Images::togglePanel(run);
        });
        Images::addRunSteps(steps);
    };
    interleave.prepareRun = [](ToolPagesLifetimeRun& run)
    {
        if (run.toolTab)
        {
            // The tab is open already (the second panel of openPopupsSurviveChanges)
            return Images::waitInputs(run) && ToolPagesLifetimeSteps::shell(run) != nullptr;
        }
        const QString front = pdftest::AppDocumentSamples::simple(run.fixture->folder(), QStringLiteral("interleave-front.pdf"), 3);
        const QString back = pdftest::AppDocumentSamples::simple(run.fixture->folder(), QStringLiteral("interleave-back.pdf"), 2);
        return Images::openTab(run, QString::fromLatin1(pdf::PDFDocumentToolInterleave::TOOL_ID), { front, back });
    };
    scenarios->push_back(std::move(interleave));

    // Images to PDF (336:1976): two images, the turn, a copy, Sort › Reverse order, Create PDF,
    // the result dialog (Open in this tab, R16)
    ToolPagesLifetimeScenario images;
    images.name = QStringLiteral("imagestopdfgui");
    images.toolId = QString::fromLatin1(pdf::PDFDocumentToolImagesToPdf::TOOL_ID);
    images.isOwnTab = true;
    images.hasResultDialog = true;
    images.hasPanel = true;
    images.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
    {
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            const QString first = Images::png(run, QStringLiteral("images-first.png"), QSize(64, 48));
            const QString second = Images::png(run, QStringLiteral("images-second.png"), QSize(48, 64));
            return Images::openTab(run, QString::fromLatin1(pdf::PDFDocumentToolImagesToPdf::TOOL_ID), { first, second }) &&
                   Images::gui<pdf::PDFAppImagesToPdfToolGUI>(run);
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppImagesToPdfToolGUI* current = Images::gui<pdf::PDFAppImagesToPdfToolGUI>(run);
            if (!current || !ToolPagesLifetimeSteps::click(current->getAction(QStringLiteral("rotateRight"))))
            {
                return false;
            }
            ToolPagesLifetimeSteps::settle();
            current = Images::gui<pdf::PDFAppImagesToPdfToolGUI>(run);
            if (!current || !ToolPagesLifetimeSteps::click(current->getAction(QStringLiteral("duplicate"))))
            {
                return false;
            }
            ToolPagesLifetimeSteps::settle();
            return true;
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            // The crop of an image (336:3159): Crop, a key on the box, Equal margins, Apply Crop
            pdf::PDFAppImagesToPdfToolGUI* current = Images::gui<pdf::PDFAppImagesToPdfToolGUI>(run);
            if (!current || !ToolPagesLifetimeSteps::click(current->getAction(QStringLiteral("crop"))))
            {
                return false;
            }
            current = Images::gui<pdf::PDFAppImagesToPdfToolGUI>(run);
            if (!current || !current->getCropOverlay() || !ToolPagesLifetimeSteps::key(current->getCropOverlay(), Qt::Key_Right))
            {
                return false;
            }
            current = Images::gui<pdf::PDFAppImagesToPdfToolGUI>(run);
            if (!current || !ToolPagesLifetimeSteps::click(current->getCropPanel()->getEqualRadio()))
            {
                return false;
            }
            current = Images::gui<pdf::PDFAppImagesToPdfToolGUI>(run);
            return current && ToolPagesLifetimeSteps::click(current->getCropPanel()->getApplyButton());
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppImagesToPdfToolGUI* current = Images::gui<pdf::PDFAppImagesToPdfToolGUI>(run);
            if (!current)
            {
                return false;
            }
            current->reverseOrder();
            ToolPagesLifetimeSteps::settle();
            return Images::togglePanel(run);
        });
        Images::addRunSteps(steps);
    };
    images.prepareRun = [](ToolPagesLifetimeRun& run)
    {
        if (run.toolTab)
        {
            return Images::waitInputs(run) && ToolPagesLifetimeSteps::shell(run) != nullptr;
        }
        const QString first = Images::png(run, QStringLiteral("images-first.png"), QSize(64, 48));
        return Images::openTab(run, QString::fromLatin1(pdf::PDFDocumentToolImagesToPdf::TOOL_ID), { first });
    };
    scenarios->push_back(std::move(images));
}
