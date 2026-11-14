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

// The lifetime scenarios of Optimize Images, Grayscale and Extract Images with the list of the
// images and the comparison (step 4 of phase 2, package P2; plan 8): the collection of the list,
// the measuring, the decodings of the thumbnails and of the canvas, Exclude, Override, the menu
// Color, the zoom of the comparison, Undo / Redo, the result dialog; Extract Images: the plan,
// Space, the click on the page, Copy Image, the menu Pages.

#include "apptoolpageslifetime.h"

#include "pdfapptoolgui_extractimages.h"
#include "pdfapptoolgui_optimizeimages.h"
#include "pdfapptoolimagecomparison.h"
#include "pdfapptoolimagelist.h"
#include "pdfapptoolpanelbuilder.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptoolresultdialog.h"
#include "pdfapptooltoolbar.h"
#include "pdfdocumenttoolshell.h"

#include "pdfcomponentrows.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentwriter.h"
#include "pdfdocumenttool_split.h"
#include "pdfparser.h"

#include <QDir>
#include <QImageWriter>
#include <QtTest>

/// Helpers of the scenarios of package P2
class ToolPagesLifetimeOptimizeImages
{
public:
    ToolPagesLifetimeOptimizeImages() = delete;

    static QByteArray ref(pdf::PDFObjectReference reference)
    {
        return QByteArray::number(reference.objectNumber) + " " + QByteArray::number(reference.generation) + " R";
    }

    static pdf::PDFObjectReference addStream(pdf::PDFDocumentBuilder& builder, const QByteArray& dictionaryText, QByteArray data)
    {
        pdf::PDFParser parser(dictionaryText, nullptr, pdf::PDFParser::None);
        const pdf::PDFObject dictionaryObject = parser.getObject();
        pdf::PDFDictionaryBuilder dictionary(*dictionaryObject.getDictionary());
        dictionary.setEntry(pdf::PDFInplaceOrMemoryString("Length"), pdf::PDFObject::createInteger(data.size()));
        return builder.addObject(pdf::PDFObject::createStream(pdf::PDFStream(std::move(dictionary), std::move(data))));
    }

    /// Three pages: a color gradient of 120 x 80 pixels and a gray image over it on each
    static pdf::PDFAppDocumentTab* openImages(pdftest::AppToolShellFixture& fixture)
    {
        pdf::PDFDocumentBuilder builder;
        QByteArray colorData;
        for (int y = 0; y < 80; ++y)
        {
            for (int x = 0; x < 120; ++x)
            {
                colorData += char(x * 2);
                colorData += char(y * 3);
                colorData += char(100);
            }
        }
        const pdf::PDFObjectReference color = addStream(builder, "<< /Type /XObject /Subtype /Image /Width 120 /Height 80 /ColorSpace /DeviceRGB /BitsPerComponent 8 >>", colorData);
        const pdf::PDFObjectReference gray = addStream(builder, "<< /Type /XObject /Subtype /Image /Width 32 /Height 32 /ColorSpace /DeviceGray /BitsPerComponent 8 >>", QByteArray(32 * 32, '\x60'));
        for (int i = 0; i < 3; ++i)
        {
            const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
            const pdf::PDFObjectReference contents = addStream(builder, "<< >>", "q 200 0 0 150 20 20 cm /A Do Q q 80 0 0 80 100 60 cm /B Do Q");
            // The parser reads the bytes when it parses, so they live as long as the parser
            const QByteArray pageText = "<< /Resources << /XObject << /A " + ref(color) + " /B " + ref(gray) + " >> >> /Contents " + ref(contents) + " >>";
            pdf::PDFParser parser(pageText, nullptr, pdf::PDFParser::None);
            builder.mergeTo(page, parser.getObject());
        }
        pdf::PDFDocument document = builder.build();
        const QString path = fixture.folder() + QStringLiteral("/lifetime-images.pdf");
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

    template<typename T>
    static T* gui(const ToolPagesLifetimeRun& run)
    {
        pdf::PDFDocumentToolShell* shell = ToolPagesLifetimeSteps::shell(run);
        return shell ? qobject_cast<T*>(shell->getGUI()) : nullptr;
    }

    /// The measuring of Optimize Images / Grayscale has ended
    static bool waitMeasured(const ToolPagesLifetimeRun& run)
    {
        return ToolPagesLifetimeSteps::wait(run, [&run]()
        {
            const pdf::PDFAppToolGUIOptimizeImages* current = gui<pdf::PDFAppToolGUIOptimizeImages>(run);
            return !current || (current->getImageList() && current->getMeasureState() != pdf::PDFAppToolGUIOptimizeImages::MeasureState::Measuring);
        });
    }

    /// The plan of Extract Images is computed
    static bool waitPlan(const ToolPagesLifetimeRun& run)
    {
        return ToolPagesLifetimeSteps::wait(run, [&run]()
        {
            const pdf::PDFAppToolGUIExtractImages* current = gui<pdf::PDFAppToolGUIExtractImages>(run);
            return !current || (current->getImageList() && !current->isComputing());
        });
    }

    /// Extract Images writes into a folder of the fixture (Overwrite), where a folder takes the name
    /// of the file of the first image: that file cannot be written (the dialog has Retry, 339:7293)
    static bool prepareExtractRun(ToolPagesLifetimeRun& run)
    {
        pdf::PDFDocumentToolShell* shell = ToolPagesLifetimeSteps::shell(run);
        QPointer<pdf::PDFAppToolParameterModel> model = shell ? shell->getModel() : nullptr;
        if (!model || !run.fixture || !waitPlan(run))
        {
            return false;
        }
        const pdf::PDFAppToolGUIExtractImages* current = gui<pdf::PDFAppToolGUIExtractImages>(run);
        if (!current || !current->getPlan().has_value() || current->getPlan()->images.empty())
        {
            return false;
        }
        const QString folder = QDir(run.fixture->folder()).filePath(QStringLiteral("extract-output"));
        QDir().mkpath(folder);
        QDir(folder).mkpath(current->getPlan()->images.front().fileName);
        QVariantMap values;
        values.insert(QString::fromLatin1(pdf::PDFDocumentToolFileOutput::PARAMETER_FOLDER), folder);
        values.insert(QString::fromLatin1(pdf::PDFDocumentToolFileOutput::PARAMETER_EXISTING_FILES), QString::fromLatin1(pdf::PDFDocumentToolFileOutput::EXISTING_OVERWRITE));
        model->setValues(values);
        ToolPagesLifetimeSteps::settle();
        return waitPlan(run);
    }

    /// A common format other than PNG, which Qt of the platform writes (the Linux Qt has no TIFF
    /// writer, the menu hides its row there)
    static QString getOtherFormat()
    {
        return QImageWriter::supportedImageFormats().contains("tiff") ? QStringLiteral("tiff") : QStringLiteral("jpeg");
    }

    /// Selects the first row of the list of the images by the key Home (the canvas decodes it);
    /// the input goes through the guards of the steps
    template<typename T>
    static bool selectFirst(ToolPagesLifetimeRun& run)
    {
        T* current = gui<T>(run);
        pdf::PDFAppToolImageList* list = current ? current->getImageListWidget() : nullptr;
        if (!list || list->getProxyModel()->rowCount() == 0)
        {
            return false;
        }
        return ToolPagesLifetimeSteps::key(list->getView(), Qt::Key_Home);
    }
};

void ToolPagesLifetimePackages::registerOptimizeImagesScenarios(std::vector<ToolPagesLifetimeScenario>* scenarios)
{
    using H = ToolPagesLifetimeOptimizeImages;
    using Steps = ToolPagesLifetimeSteps;
    using Gui = pdf::PDFAppToolGUIOptimizeImages;

    // Optimize Images: the list and its thumbnails, the measuring, the canvas, Exclude, Override,
    // the menu Color, the zoom, Undo / Redo, the result dialog Keep
    ToolPagesLifetimeScenario images;
    images.name = QStringLiteral("optimizeimages");
    images.toolId = QStringLiteral("optimize.images");
    images.openDocument = &H::openImages;
    images.hasResultDialog = true;
    images.hasPanel = true;
    images.prepareRun = &H::waitMeasured;
    images.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
    {
        steps->push_back(&Steps::openTool);
        steps->push_back(&H::waitMeasured);
        steps->push_back(&H::selectFirst<Gui>);
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            Gui* current = H::gui<Gui>(run);
            return current && Steps::click(current->getExcludeButton());
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            Gui* current = H::gui<Gui>(run);
            return current && Steps::click(current->getExcludeButton());
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            Gui* current = H::gui<Gui>(run);
            return current && Steps::click(current->getOverrideButton());
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            // The check box Override of the card removes the override and puts it back (its
            // commit may delete the tab while QCheckBox is inside of its click)
            Gui* current = H::gui<Gui>(run);
            if (!current || !current->getOverrideCheckBox() || !Steps::click(current->getOverrideCheckBox()))
            {
                return false;
            }
            current = H::gui<Gui>(run);
            return current && current->getOverrideCheckBox() && Steps::click(current->getOverrideCheckBox());
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            // The menu Color writes the override (301:3594)
            Gui* current = H::gui<Gui>(run);
            if (!current || !Steps::click(current->getColorButton()))
            {
                return false;
            }
            current = H::gui<Gui>(run);
            return current && Steps::click(current->getColorRow(QStringLiteral("grayscale")));
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            // The zoom of the comparison: 100 %, a move by the keys, Fit
            Gui* current = H::gui<Gui>(run);
            pdf::PDFAppToolImageComparison* comparison = current ? current->getComparison() : nullptr;
            if (!comparison)
            {
                return false;
            }
            const QList<QToolButton*> segments = comparison->getZoomControl()->findChildren<QToolButton*>();
            if (segments.size() < 2 || !Steps::click(segments.at(1)))
            {
                return false;
            }
            current = H::gui<Gui>(run);
            comparison = current ? current->getComparison() : nullptr;
            pdf::PDFAppToolImageComparisonView* view = comparison ? comparison->getBeforePane()->getView() : nullptr;
            if (view && !Steps::key(view, Qt::Key_Right))
            {
                return false;
            }
            current = H::gui<Gui>(run);
            comparison = current ? current->getComparison() : nullptr;
            return !comparison || Steps::click(comparison->getZoomControl()->findChildren<QToolButton*>().value(0));
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            // Show on page (RS4APP lifetime gap): the view goes to the page of the image
            Gui* current = H::gui<Gui>(run);
            pdf::PDFAppToolImageComparison* comparison = current ? current->getComparison() : nullptr;
            return comparison && Steps::click(comparison->getShowOnPageButton());
        });
        steps->push_back([](ToolPagesLifetimeRun& run) { return Steps::click(Steps::toolbar(run) ? Steps::toolbar(run)->getUndoButton() : nullptr); });
        steps->push_back([](ToolPagesLifetimeRun& run) { return Steps::click(Steps::toolbar(run) ? Steps::toolbar(run)->getRedoButton() : nullptr); });
        steps->push_back(&H::waitMeasured);
        steps->push_back([](ToolPagesLifetimeRun& run) { return Steps::confirmDialog(run, Steps::confirmToDialog(run)); });
    };
    scenarios->push_back(images);

    // Optimize Images (RS4APP): the editor of an override - its chip changes the color of the
    // override, the threshold is typed (the editor is not rebuilt below its spin box), Space of the
    // list excludes and puts back
    ToolPagesLifetimeScenario overrideEditor;
    overrideEditor.name = QStringLiteral("imagesoverrideeditor");
    overrideEditor.toolId = QStringLiteral("optimize.images");
    overrideEditor.openDocument = &H::openImages;
    overrideEditor.hasResultDialog = true;
    overrideEditor.hasPanel = true;
    overrideEditor.prepareRun = &H::waitMeasured;
    overrideEditor.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
    {
        steps->push_back(&Steps::openTool);
        steps->push_back(&H::waitMeasured);
        steps->push_back(&H::selectFirst<Gui>);
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            Gui* current = H::gui<Gui>(run);
            return current && Steps::click(current->getOverrideButton());
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            // The override gets its own color first (Color: Color), then the chip Bitonal changes it
            Gui* current = H::gui<Gui>(run);
            if (!current || !Steps::click(current->getColorButton()))
            {
                return false;
            }
            current = H::gui<Gui>(run);
            if (!current || !Steps::click(current->getColorRow(QStringLiteral("color"))))
            {
                return false;
            }
            current = H::gui<Gui>(run);
            return !current || Steps::click(current->getProfileEditor()->getProfileChip(QStringLiteral("bitonal")));
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            Gui* current = H::gui<Gui>(run);
            return current && current->getProfileEditor()->getThresholdAutoCheckBox() && Steps::click(current->getProfileEditor()->getThresholdAutoCheckBox());
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            // Owner 14. 11.: Mode, Goal and Options write the override (the store of the menus)
            Gui* current = H::gui<Gui>(run);
            if (!current || !current->getModeMenu() || !Steps::click(current->getModeMenu()->getButton()))
            {
                return false;
            }
            current = H::gui<Gui>(run);
            if (!current || !Steps::click(current->getModeMenu()->getRow(QStringLiteral("auto"))))
            {
                return false;
            }
            current = H::gui<Gui>(run);
            if (!current || !Steps::click(current->getGoalMenu()->getButton()))
            {
                return false;
            }
            current = H::gui<Gui>(run);
            if (!current || !Steps::click(current->getGoalMenu()->getRow(QStringLiteral("minimum-size"))))
            {
                return false;
            }
            current = H::gui<Gui>(run);
            if (!current || !Steps::click(current->getOptionsMenu()->getButton()))
            {
                return false;
            }
            current = H::gui<Gui>(run);
            pdf::PDFAppToolPanelPopup* panel = current ? current->getOptionsMenu()->getPanel() : nullptr;
            return !panel || Steps::click(panel->findChild<pdf::PDFTileRow*>(QStringLiteral("toolFlag-keepOriginalIfLarger")));
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            Gui* current = H::gui<Gui>(run);
            return current && current->getProfileEditor()->getThresholdSpinBox() && Steps::key(current->getProfileEditor()->getThresholdSpinBox(), Qt::Key_Up);
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            Gui* current = H::gui<Gui>(run);
            return current && Steps::key(current->getImageListWidget()->getView(), Qt::Key_Space);
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            Gui* current = H::gui<Gui>(run);
            return current && Steps::key(current->getImageListWidget()->getView(), Qt::Key_Space);
        });
        steps->push_back(&H::waitMeasured);
        steps->push_back([](ToolPagesLifetimeRun& run) { return Steps::confirmDialog(run, Steps::confirmToDialog(run)); });
    };
    scenarios->push_back(overrideEditor);

    // Convert Images to Grayscale: the same GUI in its mode, Exclude, the result dialog Keep
    ToolPagesLifetimeScenario grayscale;
    grayscale.name = QStringLiteral("grayscale");
    grayscale.toolId = QStringLiteral("optimize.grayscale");
    grayscale.openDocument = &H::openImages;
    grayscale.hasResultDialog = true;
    grayscale.hasPanel = true;
    grayscale.prepareRun = &H::waitMeasured;
    grayscale.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
    {
        steps->push_back(&Steps::openTool);
        steps->push_back(&H::waitMeasured);
        steps->push_back(&H::selectFirst<Gui>);
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            Gui* current = H::gui<Gui>(run);
            return current && current->getEnableCheckBox() && Steps::click(current->getEnableCheckBox());
        });
        steps->push_back([](ToolPagesLifetimeRun& run) { return Steps::click(Steps::toolbar(run) ? Steps::toolbar(run)->getUndoButton() : nullptr); });
        steps->push_back(&H::waitMeasured);
        steps->push_back([](ToolPagesLifetimeRun& run) { return Steps::confirmDialog(run, Steps::confirmToDialog(run)); });
    };
    scenarios->push_back(grayscale);

    // Extract Images: the plan, the row and its mark, the click on the page (B64), Copy Image, the
    // menu Pages (Current, All)
    using Extract = pdf::PDFAppToolGUIExtractImages;
    ToolPagesLifetimeScenario extract;
    extract.name = QStringLiteral("extractimages");
    extract.toolId = QStringLiteral("convert.extract-images");
    extract.openDocument = &H::openImages;
    extract.hasPanel = true;
    extract.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
    {
        steps->push_back(&Steps::openTool);
        steps->push_back(&H::waitPlan);
        steps->push_back(&H::selectFirst<Extract>);
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            Extract* current = H::gui<Extract>(run);
            return current && Steps::key(current->getImageListWidget()->getView(), Qt::Key_Space);
        });
        steps->push_back(&H::waitPlan);
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            // A click on the page selects the image drawn last there (the center of the stage lies
            // on the images of the sample page)
            Extract* current = H::gui<Extract>(run);
            return current && Steps::click(current->getOverlay());
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            Extract* current = H::gui<Extract>(run);
            if (!current || !Steps::click(current->getCopyButton()))
            {
                return false;
            }
            Steps::wait(run, [&run]() { const Extract* gui = H::gui<Extract>(run); return !gui || !gui->getCopyButton() || gui->getCopyButton()->isEnabled(); });
            return true;
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            // Pages: Current page, then All pages
            Extract* current = H::gui<Extract>(run);
            if (!current || !Steps::click(current->getPagesButton()))
            {
                return false;
            }
            current = H::gui<Extract>(run);
            if (!current || !Steps::click(current->getPagesRow(Extract::PagesMode::Current)))
            {
                return false;
            }
            current = H::gui<Extract>(run);
            if (!current)
            {
                return true;
            }
            current->setPagesMode(Extract::PagesMode::All);
            return true;
        });
        steps->push_back(&H::waitPlan);
    };
    scenarios->push_back(extract);

    // Extract Images (RS4APP lifetime gaps): the menu Format, Exclude all / Include all, the range
    // popover with Apply, the Export run with a file, which cannot be written, Retry in the dialog
    ToolPagesLifetimeScenario extractRun;
    extractRun.name = QStringLiteral("extractimagesrun");
    extractRun.toolId = QStringLiteral("convert.extract-images");
    extractRun.openDocument = &H::openImages;
    extractRun.hasResultDialog = true;
    extractRun.prepareRun = &H::prepareExtractRun;
    extractRun.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
    {
        steps->push_back(&Steps::openTool);
        steps->push_back(&H::waitPlan);
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            Extract* current = H::gui<Extract>(run);
            if (!current || !Steps::click(current->getFormatButton()))
            {
                return false;
            }
            current = H::gui<Extract>(run);
            return !current || Steps::click(current->getFormatRow(H::getOtherFormat()));
        });
        steps->push_back(&H::waitPlan);
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            Extract* current = H::gui<Extract>(run);
            if (!current || !Steps::click(current->getExcludeAllButton()))
            {
                return false;
            }
            current = H::gui<Extract>(run);
            return !current || Steps::click(current->getIncludeAllButton());
        });
        steps->push_back(&H::waitPlan);
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            // The range popover (341:7936): Page range, the text, Enter applies it
            Extract* current = H::gui<Extract>(run);
            if (!current || !Steps::click(current->getPagesButton()))
            {
                return false;
            }
            current = H::gui<Extract>(run);
            if (!current || !Steps::click(current->getPagesRow(Extract::PagesMode::Range)))
            {
                return false;
            }
            current = H::gui<Extract>(run);
            QLineEdit* field = current ? current->getRangeField() : nullptr;
            if (!field)
            {
                return true;
            }
            field->setText(QStringLiteral("1-2"));
            return Steps::key(field, Qt::Key_Return);
        });
        steps->push_back(&H::waitPlan);
        steps->push_back(&H::prepareExtractRun);
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            // The run with a file, which cannot be written; Retry, then the dialog is closed
            pdf::PDFAppToolResultDialog* dialog = Steps::confirmToDialog(run);
            if (!dialog || !dialog->getRetryButton() || !Steps::click(dialog->getRetryButton()))
            {
                return false;
            }
            Steps::wait(run, [&run]() { pdf::PDFDocumentToolShell* current = Steps::shell(run); return !current || current->getResultDialog(); });
            pdf::PDFDocumentToolShell* shell = Steps::shell(run);
            pdf::PDFAppToolResultDialog* retried = shell ? shell->getResultDialog() : nullptr;
            return !retried || Steps::click(retried->getCloseButton());
        });
    };
    scenarios->push_back(extractRun);
}
