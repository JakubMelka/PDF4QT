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

// The lifetime scenarios of package C of step 3a of phase 2 (Insert Pages, Add Image as Page): the
// password of a source, the strip of the position, the loading of the sources (plan 6, 8)

#include "apptoolpageslifetime.h"

#include "pdfapppassworddialog.h"
#include "pdfapptoolgui_insert.h"
#include "pdfapptoolinsertstrip.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfapptooltoolbar.h"
#include "pdfdocumenttoolshell.h"

#include "pdfcomponentrows.h"
#include "pdfdocumentbuilder.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QImage>
#include <QMimeData>
#include <QRadioButton>
#include <QtTest>

/// Steps of the scenarios of Insert Pages and Add Image as Page
class ToolPagesLifetimeInsertHelper
{
public:
    ToolPagesLifetimeInsertHelper() = delete;

    using S = ToolPagesLifetimeSteps;

    template<typename GUI>
    static GUI* gui(const ToolPagesLifetimeRun& run)
    {
        pdf::PDFDocumentToolShell* shell = S::shell(run);
        return shell ? qobject_cast<GUI*>(shell->getGUI()) : nullptr;
    }

    /// A document of A4 pages in the folder of the fixture
    static QString writePages(const ToolPagesLifetimeRun& run, const QString& name, int pageCount)
    {
        pdf::PDFDocumentBuilder builder;
        for (int i = 0; i < pageCount; ++i)
        {
            builder.appendPage(QRectF(0, 0, 595, 842));
        }
        pdf::PDFDocument document = builder.build();
        const QString path = run.fixture->path(name);
        return pdftest::AppDocumentSamples::write(document, path) ? path : QString();
    }

    static QString writeImage(const ToolPagesLifetimeRun& run, const QString& name, QSize size)
    {
        QImage image(size, QImage::Format_RGB32);
        image.fill(QColor(120, 160, 200));
        const QString path = run.fixture->path(name);
        return image.save(path, "PNG") ? path : QString();
    }

    /// The sources of the GUI are read
    static bool waitLoaded(const ToolPagesLifetimeRun& run)
    {
        const bool isLoaded = S::wait(run, [&run]()
        {
            pdf::PDFAppInsertToolGUIBase* current = gui<pdf::PDFAppInsertToolGUIBase>(run);
            return !current || (current->getSourceList()->getLoadingCount() == 0 && current->getSourceList()->getLoader()->getRunningCount() == 0);
        });

        // The GUI shows the readings by one queued update
        S::settle();
        return isLoaded;
    }

    /// Adds the files to the GUI (the reading runs in a worker; the victim may be deleted inside
    /// of the result of the reading)
    static bool addFiles(ToolPagesLifetimeRun& run, const QStringList& files)
    {
        pdf::PDFAppInsertToolGUIBase* current = gui<pdf::PDFAppInsertToolGUIBase>(run);
        if (!current || files.contains(QString()))
        {
            return false;
        }
        current->addFiles(files);
        S::settle();
        return waitLoaded(run);
    }

    /// A click on the trailing half of the tile of the page of the strip
    static bool clickStrip(ToolPagesLifetimeRun& run, int page)
    {
        pdf::PDFAppInsertToolGUIBase* current = gui<pdf::PDFAppInsertToolGUIBase>(run);
        pdf::PDFAppInsertPositionStrip* strip = current ? current->getStrip() : nullptr;
        if (!strip || strip->getTileRect(page).isEmpty())
        {
            return false;
        }
        const QRect rect = strip->getTileRect(page);
        QPointer<QWidget> viewport(strip->viewport());
        QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, QPoint(rect.right() - 10, rect.center().y()));
        if (viewport)
        {
            QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, QPoint(rect.right() - 10, rect.center().y()));
        }
        S::settle();
        return true;
    }

    /// Files dragged over the strip and dropped at the leading half of the tile of the page (the
    /// victim may be deleted inside of every event)
    static bool dropOnStrip(ToolPagesLifetimeRun& run, int page, const QString& file)
    {
        pdf::PDFAppInsertToolGUIBase* current = gui<pdf::PDFAppInsertToolGUIBase>(run);
        pdf::PDFAppInsertPositionStrip* strip = current ? current->getStrip() : nullptr;
        if (!strip || strip->getTileRect(page).isEmpty() || file.isEmpty())
        {
            return false;
        }
        const QPoint point(strip->getTileRect(page).left() + 10, strip->getTileRect(page).center().y());
        QPointer<QWidget> viewport(strip->viewport());
        QMimeData mimeData;
        mimeData.setUrls({ QUrl::fromLocalFile(file) });
        QDragEnterEvent enter(point, Qt::CopyAction, &mimeData, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(viewport, &enter);
        if (viewport)
        {
            QDragMoveEvent move(point + QPoint(1, 0), Qt::CopyAction, &mimeData, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(viewport, &move);
        }
        S::settle();
        if (viewport)
        {
            QDropEvent drop(QPointF(point), Qt::CopyAction, &mimeData, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(viewport, &drop);
        }
        S::settle();
        return waitLoaded(run);
    }

    /// The first row of the open panel of the toolbar button, which opens it
    static bool clickPanelRow(ToolPagesLifetimeRun& run, const QString& panel, const QString& row)
    {
        pdf::PDFAppInsertToolGUIBase* current = gui<pdf::PDFAppInsertToolGUIBase>(run);
        if (!current || !S::click(current->getAction(panel)))
        {
            return false;
        }
        current = gui<pdf::PDFAppInsertToolGUIBase>(run);
        pdf::PDFTileRow* tileRow = current ? current->getPanelRow(row) : nullptr;
        return tileRow && S::click(tileRow);
    }

    /// Insert Pages: the tool can run (two blank pages)
    static bool prepareInsert(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppInsertToolGUI* current = gui<pdf::PDFAppInsertToolGUI>(run);
        if (!current)
        {
            return false;
        }
        current->addBlankPages();
        S::settle();
        current = gui<pdf::PDFAppInsertToolGUI>(run);
        if (current)
        {
            current->addBlankPages();
            S::settle();
        }
        return S::shell(run) != nullptr;
    }

    /// Add Image as Page: the tool can run (one image)
    static bool prepareAddImage(ToolPagesLifetimeRun& run)
    {
        return addFiles(run, { writeImage(run, QStringLiteral("prepared.png"), QSize(80, 60)) });
    }

    static void addInsertSteps(std::vector<ToolPagesLifetimeStep>* steps)
    {
        // Insert Pages: a source being read, the strip (click, keys, a drop of a file at a place),
        // the blank pages by the key, the range of the source, Undo / Redo, the result dialog
        steps->push_back(&S::openTool);
        steps->push_back([](ToolPagesLifetimeRun& run) { return addFiles(run, { writePages(run, QStringLiteral("annex.pdf"), 30), writeImage(run, QStringLiteral("photo.png"), QSize(90, 60)) }); });
        steps->push_back([](ToolPagesLifetimeRun& run) { return clickStrip(run, 1); });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppInsertToolGUI* current = gui<pdf::PDFAppInsertToolGUI>(run);
            return current && S::key(current->getStrip(), Qt::Key_Home) && S::key(gui<pdf::PDFAppInsertToolGUI>(run) ? gui<pdf::PDFAppInsertToolGUI>(run)->getStrip() : nullptr, Qt::Key_Right);
        });
        steps->push_back([](ToolPagesLifetimeRun& run) { return dropOnStrip(run, 2, writePages(run, QStringLiteral("dropped.pdf"), 2)); });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppInsertToolGUI* current = gui<pdf::PDFAppInsertToolGUI>(run);
            return current && S::key(current->getCanvas(), Qt::Key_I, Qt::ControlModifier | Qt::ShiftModifier);
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            // The first source, Page range by its radio button, the range typed and written
            pdf::PDFAppInsertToolGUI* current = gui<pdf::PDFAppInsertToolGUI>(run);
            if (!current || current->getSourceList()->getCount() == 0)
            {
                return false;
            }
            current->setCurrentSource(current->getSourceList()->getInput(0)->id);
            S::settle();
            current = gui<pdf::PDFAppInsertToolGUI>(run);
            if (!current || !S::click(current->getChoiceSwitch() ? current->getChoiceSwitch()->getButton(QStringLiteral("range")) : nullptr))
            {
                return false;
            }
            current = gui<pdf::PDFAppInsertToolGUI>(run);
            pdf::PDFAppToolPageRangeField* field = current ? current->getRangeField() : nullptr;
            if (!field)
            {
                return false;
            }
            QPointer<pdf::PDFAppToolPageRangeField> fieldGuard(field);
            field->getField()->setFocus();
            field->getField()->selectAll();
            QTest::keyClicks(field->getField(), QStringLiteral("2-4, odd"));
            if (fieldGuard)
            {
                fieldGuard->flush();
            }
            S::settle();
            return true;
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppInsertToolGUI* current = gui<pdf::PDFAppInsertToolGUI>(run);
            if (!current)
            {
                return false;
            }
            current->clickSourcePage(5, 0);
            S::settle();
            return true;
        });
        steps->push_back([](ToolPagesLifetimeRun& run) { return clickPanelRow(run, QStringLiteral("position"), QStringLiteral("position-start")); });
        steps->push_back([](ToolPagesLifetimeRun& run) { return S::click(S::toolbar(run) ? S::toolbar(run)->getUndoButton() : nullptr); });
        steps->push_back([](ToolPagesLifetimeRun& run) { return S::click(S::toolbar(run) ? S::toolbar(run)->getRedoButton() : nullptr); });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppInsertToolGUI* current = gui<pdf::PDFAppInsertToolGUI>(run);
            return current && S::click(current->getAction(QStringLiteral("remove")));
        });
        steps->push_back([](ToolPagesLifetimeRun& run) { return S::confirmDialog(run, S::confirmToDialog(run)); });
    }

    static void addPasswordSteps(std::vector<ToolPagesLifetimeStep>* steps)
    {
        // Insert Pages with a source, which needs its password: the dialog of the source open, a
        // wrong password, the right one, a second source removed by Remove File of the dialog
        steps->push_back(&S::openTool);
        steps->push_back([](ToolPagesLifetimeRun& run) { return addFiles(run, { pdftest::AppDocumentSamples::userPassword(run.fixture->folder()) }); });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            // Enter Password… of the card of the source (388:17789)
            pdf::PDFAppInsertToolGUI* current = gui<pdf::PDFAppInsertToolGUI>(run);
            const pdf::PDFAppToolInputList* list = current ? current->getSourceList() : nullptr;
            QPushButton* enter = list && list->getCount() > 0 && current->getCanvas() ? current->getCanvas()->getPageView()->getActionButton(list->getInput(0)->id, QStringLiteral("password")) : nullptr;
            if (!S::click(enter))
            {
                return false;
            }
            current = gui<pdf::PDFAppInsertToolGUI>(run);
            QPointer<pdf::PDFAppPasswordDialog> dialog = current ? current->getPasswordDialog() : nullptr;
            if (!dialog)
            {
                return false;
            }
            QTest::keyClicks(dialog->getPasswordField(), QStringLiteral("wrong"));
            if (dialog)
            {
                S::click(dialog->getUnlockButton());
            }
            S::wait(run, [&dialog]() { return !dialog || dialog->getUnlockButton()->isEnabled(); });
            if (dialog)
            {
                dialog->getPasswordField()->clear();
                QTest::keyClicks(dialog->getPasswordField(), pdftest::AppDocumentSamples::getUserPassword());
            }
            if (dialog)
            {
                S::click(dialog->getUnlockButton());
            }
            S::settle();
            return waitLoaded(run);
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            if (!QDir(run.fixture->folder()).mkpath(QStringLiteral("second")))
            {
                return false;
            }
            return addFiles(run, { pdftest::AppDocumentSamples::userPassword(QDir(run.fixture->folder()).filePath(QStringLiteral("second"))) });
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppInsertToolGUI* current = gui<pdf::PDFAppInsertToolGUI>(run);
            if (!current || current->getSourceList()->getCount() < 2)
            {
                return false;
            }
            current->openPasswordDialog(current->getSourceList()->getInput(1)->id);
            S::settle();
            current = gui<pdf::PDFAppInsertToolGUI>(run);
            pdf::PDFAppPasswordDialog* dialog = current ? current->getPasswordDialog() : nullptr;
            return dialog && S::click(dialog->getRemoveButton());
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            // A third locked source removed by Remove File of its card: the click deletes the button
            if (!QDir(run.fixture->folder()).mkpath(QStringLiteral("third")))
            {
                return false;
            }
            return addFiles(run, { pdftest::AppDocumentSamples::userPassword(QDir(run.fixture->folder()).filePath(QStringLiteral("third"))) });
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppInsertToolGUI* current = gui<pdf::PDFAppInsertToolGUI>(run);
            const pdf::PDFAppToolInputList* list = current ? current->getSourceList() : nullptr;
            if (!list || list->getCount() < 2 || !current->getCanvas())
            {
                return false;
            }
            return S::click(current->getCanvas()->getPageView()->getActionButton(list->getInput(list->getCount() - 1)->id, QStringLiteral("remove")));
        });
        steps->push_back([](ToolPagesLifetimeRun& run) { return S::confirmDialog(run, S::confirmToDialog(run)); });
    }

    static void addImageSteps(std::vector<ToolPagesLifetimeStep>* steps)
    {
        // Add Image as Page: the images being read, a card of the canvas, Rotate Right, the menu
        // Page (Custom), Fit / 100 %, a drop of an image on the strip, the result dialog
        steps->push_back(&S::openTool);
        steps->push_back([](ToolPagesLifetimeRun& run) { return addFiles(run, { writeImage(run, QStringLiteral("a.png"), QSize(120, 80)), writeImage(run, QStringLiteral("b.png"), QSize(60, 90)) }); });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppAddImageToolGUI* current = gui<pdf::PDFAppAddImageToolGUI>(run);
            const std::vector<pdf::PDFAppAddImagePageCard*> cards = current && current->getCanvas() ? current->getCanvas()->getCards() : std::vector<pdf::PDFAppAddImagePageCard*>();
            return cards.size() == 2 && S::click(cards.back());
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppAddImageToolGUI* current = gui<pdf::PDFAppAddImageToolGUI>(run);
            return current && S::click(current->getAction(QStringLiteral("rotateRight")));
        });
        steps->push_back([](ToolPagesLifetimeRun& run) { return clickPanelRow(run, QStringLiteral("pageSize"), QStringLiteral("page-custom")); });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppAddImageToolGUI* current = gui<pdf::PDFAppAddImageToolGUI>(run);
            if (!current || !current->getCanvas())
            {
                return false;
            }
            current->getCanvas()->getZoomSwitch()->setCurrentIndex(1);
            S::settle();
            return true;
        });
        steps->push_back([](ToolPagesLifetimeRun& run) { return dropOnStrip(run, 0, writeImage(run, QStringLiteral("dropped.png"), QSize(40, 40))); });
        steps->push_back([](ToolPagesLifetimeRun& run) { return S::click(S::toolbar(run) ? S::toolbar(run)->getUndoButton() : nullptr); });
        steps->push_back([](ToolPagesLifetimeRun& run) { return S::confirmDialog(run, S::confirmToDialog(run)); });
    }
};

void ToolPagesLifetimePackages::registerInsertScenarios(std::vector<ToolPagesLifetimeScenario>* scenarios)
{
    using H = ToolPagesLifetimeInsertHelper;

    // The document of the scenarios: twelve plain pages (the strip shows six of them)
    const auto openDocument = [](pdftest::AppToolShellFixture& fixture) { return fixture.openDocument(12); };

    ToolPagesLifetimeScenario insert;
    insert.name = QStringLiteral("insert");
    insert.toolId = QStringLiteral("pages.insert");
    insert.openDocument = openDocument;
    insert.addSteps = &H::addInsertSteps;
    insert.prepareRun = &H::prepareInsert;
    insert.hasResultDialog = true;
    insert.hasPanel = true;
    scenarios->push_back(insert);

    ToolPagesLifetimeScenario password;
    password.name = QStringLiteral("insertpassword");
    password.toolId = QStringLiteral("pages.insert");
    password.openDocument = openDocument;
    password.addSteps = &H::addPasswordSteps;
    scenarios->push_back(password);

    ToolPagesLifetimeScenario addImage;
    addImage.name = QStringLiteral("addimage");
    addImage.toolId = QStringLiteral("pages.add-image");
    addImage.openDocument = openDocument;
    addImage.addSteps = &H::addImageSteps;
    addImage.prepareRun = &H::prepareAddImage;
    addImage.hasResultDialog = true;
    addImage.hasPanel = true;
    scenarios->push_back(addImage);
}
