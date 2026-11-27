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

// The hooks of a tool with its own model and canvas in the shell and in the application (Edit Content, stage E1;
// GUI_overhaul/edit_tool/zadani.md KE.2, KE.3, zadani_gui.md 4.20, 4.21, 4.25): the own undo target, the commands
// of the document routed into the tool, the question before the document is saved, the icon check buttons of the
// panel builder, the priority of the labels of the toolbar and the image filter of the file dialogs. A test GUI
// overrides each hook; a tool, which overrides none, behaves as before.

#include "appasyncdialogs.h"
#include "apptoolshellfixture.h"

#include "pdfappautorefresh.h"
#include "pdfappcommand.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentsave.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumenttoolbar.h"
#include "pdfappdocumentview.h"
#include "pdfappfiledrop.h"
#include "pdfappmainwindow.h"
#include "pdfappnavigator.h"
#include "pdfapppage.h"
#include "pdfapproute.h"
#include "pdfapptoolpanelbuilder.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptoolresultdialog.h"
#include "pdfapptooltoolbar.h"
#include "pdfapptoolundotarget.h"
#include "pdfdocumenttoolshell.h"

#include "pdfcomponentpanels.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfmessagebox.h"
#include "pdfwidgetstyle.h"
#include "pdfwidgetstyleproperties.h"

#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QScopeGuard>
#include <QSet>
#include <QSignalSpy>
#include <QToolButton>
#include <QtTest>

#include <map>
#include <memory>

using pdf::PDFAppCommands;
using pdf::PDFAppToolCommandState;
using pdf::PDFAppToolToolbar;
using pdf::PDFDocumentToolShell;
using pdftest::AppAsyncDialogs;
using pdftest::AppToolShellFixture;
using pdftest::ShellTestGUI;
using pdftest::ShellTestTool;

/// A tool of the catalog, whose result is kept in the tab by default (the test tool takes its id)
#define TOOL_ROTATE QStringLiteral("pages.rotate")

/// The own history of the test GUI: it counts what it is asked to do
class HooksUndoTarget : public pdf::PDFAppToolUndoTarget
{
public:
    explicit HooksUndoTarget(QObject* parent) : pdf::PDFAppToolUndoTarget(parent) { }

    virtual bool canUndo() const override { return isUndoAvailable; }
    virtual bool canRedo() const override { return isRedoAvailable; }
    virtual void undo() override { ++undoCount; }
    virtual void redo() override { ++redoCount; }

    void setAvailable(bool isUndo, bool isRedo)
    {
        isUndoAvailable = isUndo;
        isRedoAvailable = isRedo;
        Q_EMIT undoStateChanged();
    }

    bool isUndoAvailable = false;
    bool isRedoAvailable = false;
    int undoCount = 0;
    int redoCount = 0;
};

/// What the test GUI overrides (read when the GUI is created)
class HooksConfig
{
public:
    bool hasOwnUndoTarget = false;
    bool hasDocumentUndoSteps = false;
    bool hasSaveQuestion = false;
    bool hasToolButtons = false;
    std::map<QString, PDFAppToolCommandState> commandStates;
};

/// The test GUI, which overrides each hook of a tool with its own model
class HooksTestGUI : public ShellTestGUI
{
public:
    explicit HooksTestGUI(const HooksConfig& config) :
        ShellTestGUI(false),
        hasSaveQuestion(config.hasSaveQuestion),
        commandStates(config.commandStates),
        m_hasDocumentUndoSteps(config.hasDocumentUndoSteps),
        m_hasToolButtons(config.hasToolButtons)
    {
        if (config.hasOwnUndoTarget)
        {
            ownTarget = new HooksUndoTarget(this);
        }
    }

    virtual pdf::PDFAppToolUndoTarget* getOwnUndoTarget() override { return ownTarget.data(); }
    virtual bool hasDocumentUndoSteps() const override { return m_hasDocumentUndoSteps; }

    virtual PDFAppToolCommandState getDocumentCommandState(const QString& commandId) const override
    {
        askedCommands.insert(commandId);
        const auto it = commandStates.find(commandId);
        return it != commandStates.end() ? it->second : PDFAppToolCommandState::NotHandled;
    }

    virtual bool handleDocumentCommand(const QString& commandId) override
    {
        handledCommands << commandId;
        return isCommandTaken;
    }

    virtual pdf::PDFAppToolSaveQuestion getSaveQuestion() const override
    {
        pdf::PDFAppToolSaveQuestion question;
        if (hasSaveQuestion)
        {
            // edit-content-v2-save-prompt
            question.title = QStringLiteral("Save without the changes to the page content?");
            question.text = QStringLiteral("The edits made in Edit Content are not in the document yet.");
            question.saveText = QStringLiteral("Save Without the Changes");
            question.applyText = QStringLiteral("Apply and Save");
        }
        return question;
    }

    virtual bool hasUnsavedWork() const override { return hasWork; }
    virtual bool isDocumentReloadDeferred() const override { return isReloadDeferred; }

    /// The work of the user comes or goes (the Primary button follows it, as in every tool with work)
    void setWork(bool work)
    {
        hasWork = work;
        updateConfirmState();
    }

    void onToolClicked() { ++toolClicks; }

    QPointer<HooksUndoTarget> ownTarget;
    bool hasSaveQuestion = false;
    bool hasWork = false;
    bool isReloadDeferred = false;
    bool isCommandTaken = true;
    std::map<QString, PDFAppToolCommandState> commandStates;
    mutable QSet<QString> askedCommands;
    QStringList handledCommands;
    int toolClicks = 0;

    QPointer<QButtonGroup> toolGroup;
    QPointer<QToolButton> selectButton;
    QPointer<QToolButton> textButton;
    QPointer<QToolButton> imageButton;
    QPointer<QToolButton> multipleButton;
    QPointer<QToolButton> findButton;
    QPointer<QToolButton> previewButton;

protected:
    virtual void describeToolbar(pdf::PDFAppToolPanelBuilder& builder) override
    {
        if (!m_hasToolButtons)
        {
            ShellTestGUI::describeToolbar(builder);
            return;
        }

        // The toolbar of edit-content-v2 (1647:21313) in small: three tools of one exclusive group, Multiple, Find
        // and Preview, whose labels go first
        toolGroup = new QButtonGroup(builder.getWidget());
        toolGroup->setExclusive(true);
        selectButton = builder.addToolButton(QStringLiteral("contentTool-select"), QStringLiteral("crosshair"), QStringLiteral("Select (V)"), toolGroup);
        textButton = builder.addToolButton(QStringLiteral("contentTool-text"), QStringLiteral("align-left"), QStringLiteral("Text (T)"), toolGroup);
        imageButton = builder.addToolButton(QStringLiteral("contentTool-image"), QStringLiteral("file"), QStringLiteral("Image (I)"), toolGroup);
        multipleButton = builder.addToolButton(QStringLiteral("contentMultiple"), QStringLiteral("list"), QStringLiteral("Multiple"), nullptr);
        builder.addActionDivider();
        findButton = builder.addAction(QStringLiteral("Find"), QStringLiteral("eye"), this, &HooksTestGUI::onToolClicked);
        findButton->setProperty(PDFAppToolToolbar::NARROW_FIRST_PROPERTY, true);
        previewButton = builder.addToggleButton(QStringLiteral("contentPreview"), QStringLiteral("Preview"), QStringLiteral("eye"));
        previewButton->setProperty(PDFAppToolToolbar::NARROW_FIRST_PROPERTY, true);
        selectButton->setChecked(true);
    }

private:
    bool m_hasDocumentUndoSteps = false;
    bool m_hasToolButtons = false;
};

/// The factory reads the configuration of the test at each opening of the tool (a test changes it between two openings)
class HooksTestGUIFactory : public pdf::PDFDocumentToolGUIFactory
{
public:
    explicit HooksTestGUIFactory(std::shared_ptr<HooksConfig> config) : m_config(std::move(config)) { }

    virtual pdf::PDFDocumentToolGUI* createGUI() const override { return new HooksTestGUI(*m_config); }

private:
    std::shared_ptr<HooksConfig> m_config;
};

/// The member slots of a requester of a file dialog
class HooksFileReceiver : public QObject
{
public:
    void onFiles(const QStringList& chosenFiles) { fileLists << chosenFiles; }
    void onFile(const QString& chosenFile) { files << chosenFile; }

    QList<QStringList> fileLists;
    QStringList files;
};

class HooksTestHelper
{
public:
    HooksTestHelper() = delete;

    using State = PDFDocumentToolShell::State;

    /// Registers the test tool under the id of the catalog with the GUI of the hooks
    static std::shared_ptr<ShellTestTool> registerTool(AppToolShellFixture& fixture, const QString& toolId, const std::shared_ptr<HooksConfig>& config)
    {
        std::shared_ptr<ShellTestTool> tool = fixture.registerShellTool(toolId);
        if (!tool)
        {
            return nullptr;
        }
        pdf::PDFDocumentToolGUIRegistry::unregisterFactory(toolId);
        return pdf::PDFDocumentToolGUIRegistry::registerFactory(toolId, std::make_shared<HooksTestGUIFactory>(config)) ? tool : nullptr;
    }

    static pdf::PDFAppDocumentTab* openReady(AppToolShellFixture& fixture, const QString& name)
    {
        pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), name, 3));
        if (!tab || !AppToolShellFixture::waitFor([tab]() { return tab->getDocument() && tab->getDocument()->isReady(); }))
        {
            return nullptr;
        }
        AppToolShellFixture::settle();
        return tab;
    }

    static HooksTestGUI* gui(PDFDocumentToolShell* shell)
    {
        return shell ? dynamic_cast<HooksTestGUI*>(shell->getGUI()) : nullptr;
    }

    /// Changes the title of the document as the user would (one undo step, the document is modified)
    static bool modify(pdf::PDFAppDocument* document, const QString& title)
    {
        pdf::PDFDocumentBuilder builder(document->getDocument().data());
        builder.setDocumentTitle(title);
        pdf::PDFDocumentPointer pointer(new pdf::PDFDocument(builder.build()));
        return document->commit(pdf::PDFModifiedDocument(pointer, nullptr, pdf::PDFModifiedDocument::None), title, document->getVersion(), pdf::PDFAppChangeSource::User);
    }

    static QByteArray readFile(const QString& filePath)
    {
        QFile file(filePath);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

    /// The title of the document info of a PDF file ("<unreadable>" for a file, which is no PDF)
    static QString fileTitle(const QString& filePath)
    {
        pdf::PDFDocumentReader reader(nullptr, nullptr, false, false);
        const pdf::PDFDocument document = reader.readFromFile(filePath);
        return reader.getReadingResult() == pdf::PDFDocumentReader::Result::OK ? document.getInfo()->title : QStringLiteral("<unreadable>");
    }

    static bool hasLabel(const QDialog* dialog, const QString& text)
    {
        for (const QLabel* label : dialog ? dialog->findChildren<QLabel*>() : QList<QLabel*>())
        {
            if (label->text() == text)
            {
                return true;
            }
        }
        return false;
    }

    /// The shown save question of the shell (nullptr = none)
    static pdf::PDFMessageBox* question(PDFDocumentToolShell* shell)
    {
        pdf::PDFMessageBox* dialog = shell ? shell->getSaveQuestion() : nullptr;
        return dialog && dialog->isVisible() ? dialog : nullptr;
    }

    static bool isActionEnabled(const AppToolShellFixture& fixture, const QString& commandId)
    {
        const QAction* action = fixture.window->getCommandAction(commandId);
        return action && action->isEnabled();
    }

    static pdf::PDFAppDocumentSaver* saver(pdf::PDFAppDocumentTab* tab)
    {
        pdf::PDFAppDocumentFileCommandHandler* handler = pdf::PDFAppDocumentFileCommandHandler::find(tab);
        return handler ? handler->getSaver() : nullptr;
    }

    /// The Primary button of the tool, then the result dialog
    static pdf::PDFAppToolResultDialog* waitForResultDialog(PDFDocumentToolShell* shell)
    {
        QPointer<PDFDocumentToolShell> guard(shell);
        AppToolShellFixture::waitFor([&guard]() { return !guard || guard->getResultDialog() != nullptr; });
        AppToolShellFixture::settle();
        return guard ? guard->getResultDialog() : nullptr;
    }

    /// A button of the actions of a toolbar as the builder makes it (Toolbar menu button 190:577)
    static QToolButton* addTextButton(QWidget* actions, const QString& name, const QString& text, bool isNarrowFirst)
    {
        QToolButton* button = pdf::PDFAppDocumentToolbar::createToolbarMenuButton(name, QStringLiteral("eye"), actions);
        button->setCheckable(false);
        button->setText(text);
        button->setToolTip(text);
        if (isNarrowFirst)
        {
            button->setProperty(PDFAppToolToolbar::NARROW_FIRST_PROPERTY, true);
        }
        actions->layout()->addWidget(button);
        return button;
    }

    /// A toolbar of a tool outside of a tab (no host) with the actions Arrange, Find, View, Preview, Clear Page; the
    /// last four have the priority property with \p hasPriority. The toolbar is a child of \p holder (a shown
    /// window without a layout): the test gives it its width, and a child is resized at once.
    static PDFAppToolToolbar* createToolbar(QWidget* holder, bool hasPriority, std::map<QString, QToolButton*>* buttons)
    {
        const pdf::PDFDocumentToolCatalogEntry* entry = pdf::PDFDocumentToolCatalog::findTool(TOOL_ROTATE);
        if (!entry)
        {
            return nullptr;
        }
        holder->resize(4200, 2 * PDFAppToolToolbar::HEIGHT);
        PDFAppToolToolbar* toolbar = new PDFAppToolToolbar(nullptr, *entry, holder);
        toolbar->setConfirmText(QStringLiteral("Apply Changes"), QString());
        QWidget* actions = new QWidget(toolbar);
        QHBoxLayout* layout = new QHBoxLayout(actions);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(pdf::PDFAppToolPanelBuilder::ACTION_GAP);
        (*buttons)[QStringLiteral("arrange")] = addTextButton(actions, QStringLiteral("arrange"), QStringLiteral("Arrange"), false);
        (*buttons)[QStringLiteral("find")] = addTextButton(actions, QStringLiteral("find"), QStringLiteral("Find"), hasPriority);
        (*buttons)[QStringLiteral("view")] = addTextButton(actions, QStringLiteral("view"), QStringLiteral("View"), hasPriority);
        (*buttons)[QStringLiteral("preview")] = addTextButton(actions, QStringLiteral("preview"), QStringLiteral("Preview"), hasPriority);
        (*buttons)[QStringLiteral("clear")] = addTextButton(actions, QStringLiteral("clear"), QStringLiteral("Clear Page"), hasPriority);
        toolbar->setToolActions(actions);
        toolbar->setGeometry(0, 0, 4000, PDFAppToolToolbar::HEIGHT);
        holder->show();
        AppToolShellFixture::settle();
        return toolbar;
    }

    /// Gives the toolbar its width, as the layout of a view does. \returns the toolbar has it.
    static bool setWidth(PDFAppToolToolbar* toolbar, int width)
    {
        toolbar->resize(width, PDFAppToolToolbar::HEIGHT);
        QCoreApplication::sendPostedEvents();
        return toolbar->width() == width && toolbar->isVisible();
    }

    static bool isIconOnly(const QToolButton* button)
    {
        return button->toolButtonStyle() == Qt::ToolButtonIconOnly;
    }

    /// Makes the file read-only (the attribute of the file on Windows, the write bits elsewhere) or writable again
    static bool setReadOnly(const QString& filePath, bool isReadOnly)
    {
        const QFileDevice::Permissions readable = QFileDevice::ReadOwner | QFileDevice::ReadUser | QFileDevice::ReadGroup | QFileDevice::ReadOther;
        const QFileDevice::Permissions permissions = isReadOnly ? readable : (readable | QFileDevice::WriteOwner | QFileDevice::WriteUser);
        return QFile::setPermissions(filePath, permissions) && QFileInfo(filePath).isWritable() == !isReadOnly;
    }
};

class AppToolHooksTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();
    void ownUndoTarget();
    void routedCommands();
    void routedCommandsDuringRun();
    void updateDocumentCommandsRefreshesWindow();
    void saveQuestionCancel();
    void saveWithoutChanges_data();
    void saveWithoutChanges();
    void saveWithoutChangesOfUnmodifiedDocument();
    void applyAndSaveKept_data();
    void applyAndSaveKept();
    void applyAndSaveDiscarded();
    void applyAndSaveStoppedRun();
    void saveEnabledByQuestion();
    void saveQuestionOfReadOnlyFile_data();
    void saveQuestionOfReadOnlyFile();
    void toolWithoutHooksSavesAsBefore();
    void autoRefreshDeferredByTool_data();
    void autoRefreshDeferredByTool();
    void builderButtons();
    void labelPriority();
    void labelPriorityKeepsLevels();
    void imageFilter();
    void imageRequests();
};

using H = HooksTestHelper;

void AppToolHooksTest::initTestCase()
{
    // The style of the application: the sizes of the buttons of the toolbar are the sizes of the design
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
}

void AppToolHooksTest::cleanup()
{
    pdf::PDFAppFileDialogs::setSaveFileHandler(pdf::PDFAppFileDialogs::SaveFileHandler());
    pdf::PDFAppFileDialogs::setOpenFilesHandler(pdf::PDFAppFileDialogs::OpenFilesHandler());
}

void AppToolHooksTest::ownUndoTarget()
{
    // KE.2: a tool with its own history - Undo and Redo of the application, their toolbar buttons and the view act on
    // the target of the GUI; the parameter model is not touched and no composite target is made, although the GUI
    // has steps of the document too
    pdftest::NestedEventLoopGuard loopGuard;
    AppToolShellFixture fixture;
    const std::shared_ptr<HooksConfig> config = std::make_shared<HooksConfig>();
    config->hasOwnUndoTarget = true;
    config->hasDocumentUndoSteps = true;

    // The GUI would disable Undo and Redo, if it were asked about them: the shell never asks
    config->commandStates[PDFAppCommands::undo()] = PDFAppToolCommandState::Disabled;
    config->commandStates[PDFAppCommands::redo()] = PDFAppToolCommandState::Disabled;
    QVERIFY(H::registerTool(fixture, TOOL_ROTATE, config));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, QStringLiteral("Undo.pdf"));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(shell);
    HooksTestGUI* gui = H::gui(shell);
    QVERIFY(gui && gui->ownTarget);
    HooksUndoTarget* target = gui->ownTarget.data();

    QCOMPARE(shell->getUndoTarget(), static_cast<pdf::PDFAppToolUndoTarget*>(target));
    QCOMPARE(tab->getView()->getToolUndoTarget(), static_cast<pdf::PDFAppToolUndoTarget*>(target));
    QVERIFY(shell->findChildren<pdf::PDFAppToolCompositeUndoTarget*>().isEmpty());

    // A step of the parameter model: it is not what Undo takes back
    QVERIFY(shell->getModel()->setValue(QStringLiteral("preset"), QStringLiteral("best")).isEmpty());
    AppToolShellFixture::settle();
    QVERIFY(shell->getModel()->canUndo());

    // The target has nothing: the command and the buttons are disabled, whatever the model has
    QVERIFY(!tab->isCommandEnabled(PDFAppCommands::undo()));
    QVERIFY(!tab->isCommandEnabled(PDFAppCommands::redo()));
    QVERIFY(!H::isActionEnabled(fixture, PDFAppCommands::undo()));
    QVERIFY(!shell->getToolbar()->getUndoButton()->isEnabled());

    target->setAvailable(true, false);
    AppToolShellFixture::settle();
    QVERIFY(tab->isCommandEnabled(PDFAppCommands::undo()));
    QVERIFY(!tab->isCommandEnabled(PDFAppCommands::redo()));
    QVERIFY(H::isActionEnabled(fixture, PDFAppCommands::undo()));
    QVERIFY(!H::isActionEnabled(fixture, PDFAppCommands::redo()));
    QVERIFY(shell->getToolbar()->getUndoButton()->isEnabled());
    QVERIFY(!shell->getToolbar()->getRedoButton()->isEnabled());

    // The command Undo
    QVERIFY(tab->executeCommand(PDFAppCommands::undo()));
    QCOMPARE(target->undoCount, 1);
    QCOMPARE(target->redoCount, 0);
    QCOMPARE(shell->getModel()->getValue(QStringLiteral("preset")).toString(), QStringLiteral("best"));

    // The button of the toolbar (the command goes to the host from the event loop)
    shell->getToolbar()->getUndoButton()->click();
    AppToolShellFixture::settle();
    QCOMPARE(target->undoCount, 2);
    QCOMPARE(shell->getModel()->getValue(QStringLiteral("preset")).toString(), QStringLiteral("best"));

    // Redo
    target->setAvailable(false, true);
    AppToolShellFixture::settle();
    QVERIFY(!shell->getToolbar()->getUndoButton()->isEnabled());
    QVERIFY(shell->getToolbar()->getRedoButton()->isEnabled());
    QVERIFY(!tab->executeCommand(PDFAppCommands::undo()));
    QCOMPARE(target->undoCount, 2);
    shell->getToolbar()->getRedoButton()->click();
    AppToolShellFixture::settle();
    QCOMPARE(target->redoCount, 1);

    // The GUI was asked about the other commands of the window, never about Undo and Redo
    fixture.window->updateCommandStates();
    QVERIFY(gui->askedCommands.contains(PDFAppCommands::zoomIn()));
    QVERIFY(!gui->askedCommands.contains(PDFAppCommands::undo()));
    QVERIFY(!gui->askedCommands.contains(PDFAppCommands::redo()));
    QVERIFY(gui->handledCommands.isEmpty());

    // The view gives the target up, when the tool closes (the target is the GUI's and lives on with it)
    QPointer<HooksUndoTarget> targetGuard(target);
    pdf::PDFAppDocumentView* view = tab->getView();
    shell->close();
    QVERIFY(targetGuard);
    QVERIFY(!view->getToolUndoTarget());
    QVERIFY(!view->isToolModeActive());

    // Undo is the document's again: it has no step
    QVERIFY(!tab->isCommandEnabled(PDFAppCommands::undo()));
    AppToolShellFixture::settle();
}

void AppToolHooksTest::routedCommands()
{
    // zadani_gui.md 4.20, way 2: the tab asks the shown page first. Enabled = the tool executes the command,
    // Disabled = nobody does, NotHandled = the handler of the document as without the hook.
    pdftest::NestedEventLoopGuard loopGuard;
    AppToolShellFixture fixture;

    // A tool, which handles nothing: Paste is off in every tool (the commands of the annotations), the zoom is the view's
    QVERIFY(H::registerTool(fixture, TOOL_ROTATE, std::make_shared<HooksConfig>()));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, QStringLiteral("Routed.pdf"));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    QVERIFY(tab->isCommandEnabled(PDFAppCommands::zoomIn()));
    PDFDocumentToolShell* plainShell = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(plainShell);
    QCOMPARE(plainShell->getDocumentCommandState(PDFAppCommands::paste()), PDFAppToolCommandState::NotHandled);
    QVERIFY(!tab->isCommandEnabled(PDFAppCommands::paste()));
    QVERIFY(!H::isActionEnabled(fixture, PDFAppCommands::paste()));
    QVERIFY(!tab->executeCommand(PDFAppCommands::paste()));
    const double plainZoom = view->getZoom();
    QVERIFY(tab->executeCommand(PDFAppCommands::zoomIn()));
    QVERIFY(view->getZoom() > plainZoom);
    QVERIFY(H::gui(plainShell)->handledCommands.isEmpty());
    plainShell->cancel();
    AppToolShellFixture::settle();
    QVERIFY(!AppToolShellFixture::getShell(tab));

    // The same tool with the hook: Paste is the tool's and enabled, zoom in is the tool's and disabled, fit width is
    // the tool's, which refuses it at the execution; zoom out stays the document's
    const std::shared_ptr<HooksConfig> config = std::make_shared<HooksConfig>();
    config->commandStates[PDFAppCommands::paste()] = PDFAppToolCommandState::Enabled;
    config->commandStates[PDFAppCommands::zoomIn()] = PDFAppToolCommandState::Disabled;
    config->commandStates[PDFAppCommands::fitWidth()] = PDFAppToolCommandState::Enabled;
    QVERIFY(H::registerTool(fixture, TOOL_ROTATE, config));
    PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(shell);
    HooksTestGUI* gui = H::gui(shell);
    QVERIFY(gui);

    QCOMPARE(shell->getDocumentCommandState(PDFAppCommands::paste()), PDFAppToolCommandState::Enabled);
    QCOMPARE(shell->getDocumentCommandState(PDFAppCommands::zoomIn()), PDFAppToolCommandState::Disabled);
    QCOMPARE(shell->getDocumentCommandState(PDFAppCommands::zoomOut()), PDFAppToolCommandState::NotHandled);
    QVERIFY(tab->isCommandEnabled(PDFAppCommands::paste()));
    QVERIFY(!tab->isCommandEnabled(PDFAppCommands::zoomIn()));
    QVERIFY(tab->isCommandEnabled(PDFAppCommands::zoomOut()));

    // The actions of the window follow the tool from its entry on
    QVERIFY(H::isActionEnabled(fixture, PDFAppCommands::paste()));
    QVERIFY(!H::isActionEnabled(fixture, PDFAppCommands::zoomIn()));
    QVERIFY(H::isActionEnabled(fixture, PDFAppCommands::zoomOut()));

    // Enabled: the tool executes it, the handler of the document does not
    QVERIFY(tab->executeCommand(PDFAppCommands::paste()));
    QCOMPARE(gui->handledCommands, QStringList{ PDFAppCommands::paste() });

    // Disabled: nobody executes it (the view keeps its zoom)
    const double zoom = view->getZoom();
    QVERIFY(!tab->executeCommand(PDFAppCommands::zoomIn()));
    QCOMPARE(view->getZoom(), zoom);
    QCOMPARE(gui->handledCommands.size(), 1);

    // Taken by the tool, which then refuses it: not executed by the handler of the document either
    gui->isCommandTaken = false;
    QVERIFY(!tab->executeCommand(PDFAppCommands::fitWidth()));
    QCOMPARE(gui->handledCommands, (QStringList{ PDFAppCommands::paste(), PDFAppCommands::fitWidth() }));
    QCOMPARE(view->getZoom(), zoom);
    gui->isCommandTaken = true;

    // NotHandled: the handler of the document, as without the tool
    QVERIFY(tab->executeCommand(PDFAppCommands::zoomOut()));
    QVERIFY(view->getZoom() < zoom);
    QCOMPARE(gui->handledCommands.size(), 2);

    // The tool closes: the commands are the document's again
    shell->cancel();
    AppToolShellFixture::settle();
    QVERIFY(!AppToolShellFixture::getShell(tab));
    QVERIFY(tab->isCommandEnabled(PDFAppCommands::zoomIn()));
    QVERIFY(H::isActionEnabled(fixture, PDFAppCommands::zoomIn()));
    const double closedZoom = view->getZoom();
    QVERIFY(tab->executeCommand(PDFAppCommands::zoomIn()));
    QVERIFY(view->getZoom() > closedZoom);
}

void AppToolHooksTest::routedCommandsDuringRun()
{
    // During a run (and a decision) the content of the tool is disabled: a command the tool takes is Disabled, and
    // the action of the window follows the state of the shell both ways
    pdftest::NestedEventLoopGuard loopGuard;
    AppToolShellFixture fixture;
    const std::shared_ptr<HooksConfig> config = std::make_shared<HooksConfig>();
    config->commandStates[PDFAppCommands::paste()] = PDFAppToolCommandState::Enabled;
    std::shared_ptr<ShellTestTool> tool = H::registerTool(fixture, TOOL_ROTATE, config);
    QVERIFY(tool);
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, QStringLiteral("Run.pdf"));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(shell);
    HooksTestGUI* gui = H::gui(shell);
    QVERIFY(H::isActionEnabled(fixture, PDFAppCommands::paste()));

    tool->setHold(true);
    shell->confirm();
    QCOMPARE(shell->getState(), H::State::Running);
    QCOMPARE(shell->getDocumentCommandState(PDFAppCommands::paste()), PDFAppToolCommandState::Disabled);
    QCOMPARE(shell->getDocumentCommandState(PDFAppCommands::zoomOut()), PDFAppToolCommandState::NotHandled);
    QVERIFY(!tab->isCommandEnabled(PDFAppCommands::paste()));
    QVERIFY(!H::isActionEnabled(fixture, PDFAppCommands::paste()));
    QVERIFY(!tab->executeCommand(PDFAppCommands::paste()));
    QVERIFY(gui->handledCommands.isEmpty());

    // Stopped: the tool is edited again and takes the command again
    shell->stop();
    QVERIFY(AppToolShellFixture::waitForState(shell, H::State::Editing));
    tool->setHold(false);
    QCOMPARE(shell->getDocumentCommandState(PDFAppCommands::paste()), PDFAppToolCommandState::Enabled);
    QVERIFY(H::isActionEnabled(fixture, PDFAppCommands::paste()));
    QVERIFY(tab->executeCommand(PDFAppCommands::paste()));
    QCOMPARE(gui->handledCommands, QStringList{ PDFAppCommands::paste() });

    // The result dialog: Disabled again
    shell->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("title"));
    shell->confirm();
    pdf::PDFAppToolResultDialog* dialog = H::waitForResultDialog(shell);
    QVERIFY(dialog);
    QCOMPARE(shell->getState(), H::State::Deciding);
    QCOMPARE(shell->getDocumentCommandState(PDFAppCommands::paste()), PDFAppToolCommandState::Disabled);
    QVERIFY(!H::isActionEnabled(fixture, PDFAppCommands::paste()));
    dialog->getDiscardButton()->click();
    AppToolShellFixture::settle();
}

void AppToolHooksTest::updateDocumentCommandsRefreshesWindow()
{
    // The answer of the tool changes for a reason of its own (the selection, the clipboard): it says so and the
    // action of the window follows at once
    pdftest::NestedEventLoopGuard loopGuard;
    AppToolShellFixture fixture;
    const std::shared_ptr<HooksConfig> config = std::make_shared<HooksConfig>();
    config->commandStates[PDFAppCommands::paste()] = PDFAppToolCommandState::Disabled;
    QVERIFY(H::registerTool(fixture, TOOL_ROTATE, config));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, QStringLiteral("Update.pdf"));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(shell);
    HooksTestGUI* gui = H::gui(shell);
    QVERIFY(!H::isActionEnabled(fixture, PDFAppCommands::paste()));

    QSignalSpy tabSpy(tab, &pdf::PDFAppTab::commandStatesChanged);
    gui->commandStates[PDFAppCommands::paste()] = PDFAppToolCommandState::Enabled;
    QVERIFY(!H::isActionEnabled(fixture, PDFAppCommands::paste()));
    gui->updateDocumentCommands();
    QCOMPARE(tabSpy.count(), 1);
    QVERIFY(H::isActionEnabled(fixture, PDFAppCommands::paste()));

    gui->commandStates[PDFAppCommands::paste()] = PDFAppToolCommandState::Disabled;
    gui->updateDocumentCommands();
    QCOMPARE(tabSpy.count(), 2);
    QVERIFY(!H::isActionEnabled(fixture, PDFAppCommands::paste()));

    // A closed tool refreshes nothing
    QPointer<HooksTestGUI> guiGuard(gui);
    shell->close();
    const int count = tabSpy.count();
    QVERIFY(guiGuard);
    guiGuard->updateDocumentCommands();
    QCOMPARE(tabSpy.count(), count);
    AppToolShellFixture::settle();
}

void AppToolHooksTest::saveQuestionCancel()
{
    // KE.3, edit-content-v2-save-prompt: Save of a document, over which the tool holds work, asks; Cancel (and Esc)
    // saves nothing and the tool goes on
    pdftest::NestedEventLoopGuard loopGuard;
    AppToolShellFixture fixture;
    const std::shared_ptr<HooksConfig> config = std::make_shared<HooksConfig>();
    config->hasSaveQuestion = true;
    QVERIFY(H::registerTool(fixture, TOOL_ROTATE, config));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, QStringLiteral("Cancel.pdf"));
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    const QString filePath = document->getFilePath();
    QVERIFY(H::modify(document, QStringLiteral("Changed by the user")));
    const QByteArray bytes = H::readFile(filePath);
    QVERIFY(!bytes.isEmpty());
    PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(shell);
    HooksTestGUI* gui = H::gui(shell);
    gui->setWork(true);
    QVERIFY(shell->hasSaveQuestion());

    QVERIFY(tab->executeCommand(PDFAppCommands::save()));
    AppToolShellFixture::settle();
    pdf::PDFMessageBox* question = H::question(shell);
    QVERIFY(question);
    QCOMPARE(question->objectName(), QStringLiteral("toolSaveQuestion"));
    QVERIFY(H::hasLabel(question, QStringLiteral("Save without the changes to the page content?")));
    QVERIFY(H::hasLabel(question, QStringLiteral("The edits made in Edit Content are not in the document yet.")));
    const QPushButton* cancelButton = question->findChild<QPushButton*>(QStringLiteral("toolSaveCancel"));
    const QPushButton* saveButton = question->findChild<QPushButton*>(QStringLiteral("toolSaveWithoutChanges"));
    const QPushButton* applyButton = question->findChild<QPushButton*>(QStringLiteral("toolSaveApply"));
    QVERIFY(cancelButton && saveButton && applyButton);
    QCOMPARE(cancelButton->text(), QStringLiteral("Cancel"));
    QCOMPARE(saveButton->text(), QStringLiteral("Save Without the Changes"));
    QCOMPARE(applyButton->text(), QStringLiteral("Apply and Save"));

    // The command itself has saved nothing
    QVERIFY(document->isModified());
    QVERIFY(!H::saver(tab)->isRunning());
    QCOMPARE(H::readFile(filePath), bytes);

    // Save again: the same question, not a second one
    QVERIFY(tab->executeCommand(PDFAppCommands::save()));
    AppToolShellFixture::settle();
    QCOMPARE(H::question(shell), question);
    QCOMPARE(AppAsyncDialogs::getOpenDialogs().size(), 1);

    QVERIFY(AppAsyncDialogs::answer(QStringLiteral("toolSaveQuestion"), QStringLiteral("toolSaveCancel")));
    QVERIFY(!H::question(shell));
    QVERIFY2(AppAsyncDialogs::isNothingOpen(), qPrintable(AppAsyncDialogs::describeOpen().join(QStringLiteral(", "))));
    QCOMPARE(shell->getState(), H::State::Editing);
    QVERIFY(gui->hasWork);
    QVERIFY(document->isModified());
    QVERIFY(!H::saver(tab)->isRunning());
    QCOMPARE(H::readFile(filePath), bytes);
    QVERIFY(shell->getSaveAfterResultCommandId().isEmpty());

    // Esc of the question is Cancel; the next Save asks anew
    QVERIFY(tab->executeCommand(PDFAppCommands::save()));
    AppToolShellFixture::settle();
    QVERIFY(H::question(shell));
    QVERIFY(AppAsyncDialogs::reject(QStringLiteral("toolSaveQuestion")));
    QVERIFY(!H::question(shell));
    QVERIFY(document->isModified());
    QVERIFY(!H::saver(tab)->isRunning());
    QCOMPARE(H::readFile(filePath), bytes);
    QCOMPARE(AppToolShellFixture::getShell(tab), shell);
}

void AppToolHooksTest::saveWithoutChanges_data()
{
    QTest::addColumn<QString>("commandId");
    QTest::newRow("save") << PDFAppCommands::save();
    QTest::newRow("save-as") << PDFAppCommands::saveAs();
}

void AppToolHooksTest::saveWithoutChanges()
{
    // Save Without the Changes: the document of the tab is saved as it is (by the command the user gave) and the tool
    // stays open with its work
    QFETCH(QString, commandId);
    pdftest::NestedEventLoopGuard loopGuard;
    AppToolShellFixture fixture;
    const std::shared_ptr<HooksConfig> config = std::make_shared<HooksConfig>();
    config->hasSaveQuestion = true;
    QVERIFY(H::registerTool(fixture, TOOL_ROTATE, config));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, QStringLiteral("Without.pdf"));
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    const QString filePath = document->getFilePath();
    QVERIFY(H::modify(document, QStringLiteral("Changed by the user")));
    const QByteArray bytes = H::readFile(filePath);
    const bool isSaveAs = commandId == PDFAppCommands::saveAs();
    const QString target = isSaveAs ? fixture.path(QStringLiteral("Saved as.pdf")) : filePath;
    pdf::PDFAppFileDialogs::setSaveFileHandler([target](QWidget*, const QString&) { return target; });

    PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(shell);
    HooksTestGUI* gui = H::gui(shell);
    gui->setWork(true);
    const int undoCount = AppToolShellFixture::undoCount(document);

    QVERIFY(tab->executeCommand(commandId));
    AppToolShellFixture::settle();
    QVERIFY(H::question(shell));
    QVERIFY(AppAsyncDialogs::answer(QStringLiteral("toolSaveQuestion"), QStringLiteral("toolSaveWithoutChanges")));
    QVERIFY(AppToolShellFixture::waitFor([document]() { return !document->isModified(); }));
    AppToolShellFixture::settle();

    // The file has the document of the tab, without anything of the tool
    QCOMPARE(H::fileTitle(target), QStringLiteral("Changed by the user"));
    QVERIFY(QFileInfo(document->getFilePath()) == QFileInfo(target));
    if (isSaveAs)
    {
        QCOMPARE(H::readFile(filePath), bytes);
    }
    QCOMPARE(AppToolShellFixture::undoCount(document), undoCount);

    // The tool is open, edited, with its work; no run was started and no save follows a later result
    QCOMPARE(AppToolShellFixture::getShell(tab), shell);
    QCOMPARE(shell->getState(), H::State::Editing);
    QVERIFY(gui->hasWork);
    QVERIFY(tab->getView()->isToolModeActive());
    QVERIFY(!shell->getResultDialog());
    QVERIFY(shell->getSaveAfterResultCommandId().isEmpty());
    QVERIFY2(AppAsyncDialogs::isNothingOpen(), qPrintable(AppAsyncDialogs::describeOpen().join(QStringLiteral(", "))));
}

void AppToolHooksTest::saveWithoutChangesOfUnmodifiedDocument()
{
    // Save was enabled only by the question: a document, which has nothing of its own to save, is not written
    // (the command Save never rewrites a file without a change)
    pdftest::NestedEventLoopGuard loopGuard;
    AppToolShellFixture fixture;
    const std::shared_ptr<HooksConfig> config = std::make_shared<HooksConfig>();
    config->hasSaveQuestion = true;
    QVERIFY(H::registerTool(fixture, TOOL_ROTATE, config));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, QStringLiteral("Unmodified.pdf"));
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    const QString filePath = document->getFilePath();
    const QByteArray bytes = H::readFile(filePath);
    PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(shell);
    H::gui(shell)->setWork(true);

    QSignalSpy savedSpy(pdf::PDFAppDocumentFileCommandHandler::find(tab), &pdf::PDFAppDocumentFileCommandHandler::saveFinished);
    QVERIFY(tab->executeCommand(PDFAppCommands::save()));
    AppToolShellFixture::settle();
    QVERIFY(AppAsyncDialogs::answer(QStringLiteral("toolSaveQuestion"), QStringLiteral("toolSaveWithoutChanges")));
    QVERIFY(!H::saver(tab)->isRunning());
    QCOMPARE(savedSpy.count(), 0);
    QCOMPARE(H::readFile(filePath), bytes);
    QCOMPARE(shell->getState(), H::State::Editing);
    QVERIFY2(AppAsyncDialogs::isNothingOpen(), qPrintable(AppAsyncDialogs::describeOpen().join(QStringLiteral(", "))));
}

void AppToolHooksTest::applyAndSaveKept_data()
{
    QTest::addColumn<bool>("isContinued");
    QTest::newRow("keep") << false;
    QTest::newRow("keep-then-continue") << true;
}

void AppToolHooksTest::applyAndSaveKept()
{
    // Apply and Save: the usual run with its result dialog; the result kept in the tab is saved after the tool has
    // closed - also when Then continue with opens the next tool
    QFETCH(bool, isContinued);
    pdftest::NestedEventLoopGuard loopGuard;
    AppToolShellFixture fixture;
    const std::shared_ptr<HooksConfig> config = std::make_shared<HooksConfig>();
    config->hasSaveQuestion = true;
    QVERIFY(H::registerTool(fixture, TOOL_ROTATE, config));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, QStringLiteral("Apply.pdf"));
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    const QString filePath = document->getFilePath();
    const QString originalTitle = H::fileTitle(filePath);
    QVERIFY(originalTitle != QStringLiteral("Applied by the tool"));
    QVERIFY(!document->isModified());
    PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(shell);
    shell->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("title"));
    shell->getModel()->setValue(QStringLiteral("title"), QStringLiteral("Applied by the tool"));
    H::gui(shell)->setWork(true);

    QVERIFY(tab->executeCommand(PDFAppCommands::save()));
    AppToolShellFixture::settle();
    QVERIFY(H::question(shell));
    QVERIFY(AppAsyncDialogs::answer(QStringLiteral("toolSaveQuestion"), QStringLiteral("toolSaveApply")));
    QVERIFY(!H::question(shell));

    // The run of the tool with its result dialog; nothing is saved before the decision
    pdf::PDFAppToolResultDialog* dialog = H::waitForResultDialog(shell);
    QVERIFY(dialog);
    QCOMPARE(shell->getState(), H::State::Deciding);
    QCOMPARE(shell->getSaveAfterResultCommandId(), PDFAppCommands::save());
    QCOMPARE(dialog->getSelectedChoice(), pdf::PDFAppToolResultDialog::Choice::Keep);
    QVERIFY(!H::saver(tab)->isRunning());
    QCOMPARE(H::fileTitle(filePath), originalTitle);

    QString nextTool;
    if (isContinued)
    {
        const QStringList successors = dialog->getContinueToolIds();
        QVERIFY(!successors.isEmpty());
        nextTool = successors.front();
        QVERIFY(fixture.registerShellTool(nextTool));
        dialog->getContinueWith()->setSelectedChoice(0);
    }

    dialog->confirm();
    AppToolShellFixture::settle();
    QCOMPARE(document->getDocument()->getInfo()->title, QStringLiteral("Applied by the tool"));
    QVERIFY(AppToolShellFixture::waitFor([document]() { return !document->isModified(); }));
    AppToolShellFixture::settle();
    QCOMPARE(H::fileTitle(filePath), QStringLiteral("Applied by the tool"));

    if (isContinued)
    {
        // The next tool is open over the saved document
        PDFDocumentToolShell* next = AppToolShellFixture::getShell(tab);
        QVERIFY(next);
        QCOMPARE(next->getTool()->getId(), nextTool);
        QCOMPARE(next->getState(), H::State::Editing);
        QCOMPARE(fixture.currentRoute(), pdf::PDFAppRoutes::documentTool(tab->getDocumentId(), nextTool).toString());
        QVERIFY(tab->getView()->isToolModeActive());
    }
    else
    {
        QVERIFY(!AppToolShellFixture::getShell(tab));
        QVERIFY(!tab->getView()->isToolModeActive());
        QVERIFY(!fixture.currentRoute().contains(QStringLiteral("/tool/")));
    }
    QVERIFY2(AppAsyncDialogs::isNothingOpen(), qPrintable(AppAsyncDialogs::describeOpen().join(QStringLiteral(", "))));
}

void AppToolHooksTest::applyAndSaveDiscarded()
{
    // Apply and Save, then Discard result: nothing is saved - not the result, not the document of the user
    pdftest::NestedEventLoopGuard loopGuard;
    AppToolShellFixture fixture;
    const std::shared_ptr<HooksConfig> config = std::make_shared<HooksConfig>();
    config->hasSaveQuestion = true;
    QVERIFY(H::registerTool(fixture, TOOL_ROTATE, config));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, QStringLiteral("Discard.pdf"));
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    const QString filePath = document->getFilePath();
    QVERIFY(H::modify(document, QStringLiteral("Changed by the user")));
    const QByteArray bytes = H::readFile(filePath);
    PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(shell);
    shell->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("title"));
    shell->getModel()->setValue(QStringLiteral("title"), QStringLiteral("Applied by the tool"));
    H::gui(shell)->setWork(true);

    QSignalSpy savedSpy(pdf::PDFAppDocumentFileCommandHandler::find(tab), &pdf::PDFAppDocumentFileCommandHandler::saveFinished);
    QVERIFY(tab->executeCommand(PDFAppCommands::save()));
    AppToolShellFixture::settle();
    QVERIFY(AppAsyncDialogs::answer(QStringLiteral("toolSaveQuestion"), QStringLiteral("toolSaveApply")));
    pdf::PDFAppToolResultDialog* dialog = H::waitForResultDialog(shell);
    QVERIFY(dialog);
    dialog->getDiscardButton()->click();
    AppToolShellFixture::settle();

    // The tool is closed (it has no question of its own), the document is as the user left it and still unsaved
    QVERIFY(!AppToolShellFixture::getShell(tab));
    QCOMPARE(document->getDocument()->getInfo()->title, QStringLiteral("Changed by the user"));
    QVERIFY(document->isModified());
    QVERIFY(!H::saver(tab)->isRunning());
    QCOMPARE(savedSpy.count(), 0);
    QCOMPARE(H::readFile(filePath), bytes);
    QVERIFY2(AppAsyncDialogs::isNothingOpen(), qPrintable(AppAsyncDialogs::describeOpen().join(QStringLiteral(", "))));
}

void AppToolHooksTest::applyAndSaveStoppedRun()
{
    // Apply and Save, the run is stopped: the choice is over. A later run of the tool, whose result is kept, is not
    // saved.
    pdftest::NestedEventLoopGuard loopGuard;
    AppToolShellFixture fixture;
    const std::shared_ptr<HooksConfig> config = std::make_shared<HooksConfig>();
    config->hasSaveQuestion = true;
    std::shared_ptr<ShellTestTool> tool = H::registerTool(fixture, TOOL_ROTATE, config);
    QVERIFY(tool);
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, QStringLiteral("Stopped.pdf"));
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    const QString filePath = document->getFilePath();
    const QByteArray bytes = H::readFile(filePath);
    PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(shell);
    shell->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("title"));
    shell->getModel()->setValue(QStringLiteral("title"), QStringLiteral("Applied by the tool"));
    H::gui(shell)->setWork(true);

    tool->setHold(true);
    QVERIFY(tab->executeCommand(PDFAppCommands::save()));
    AppToolShellFixture::settle();
    QVERIFY(AppAsyncDialogs::answer(QStringLiteral("toolSaveQuestion"), QStringLiteral("toolSaveApply")));
    QCOMPARE(shell->getState(), H::State::Running);
    QCOMPARE(shell->getSaveAfterResultCommandId(), PDFAppCommands::save());

    shell->stop();
    QVERIFY(AppToolShellFixture::waitForState(shell, H::State::Editing));
    tool->setHold(false);
    QVERIFY(shell->getSaveAfterResultCommandId().isEmpty());
    QVERIFY(!document->isModified());

    // The user runs the tool himself and keeps the result: the document is changed and stays unsaved
    QSignalSpy savedSpy(pdf::PDFAppDocumentFileCommandHandler::find(tab), &pdf::PDFAppDocumentFileCommandHandler::saveFinished);
    shell->confirm();
    pdf::PDFAppToolResultDialog* dialog = H::waitForResultDialog(shell);
    QVERIFY(dialog);
    QVERIFY(shell->getSaveAfterResultCommandId().isEmpty());
    dialog->confirm();
    AppToolShellFixture::settle();
    QVERIFY(!AppToolShellFixture::getShell(tab));
    QCOMPARE(document->getDocument()->getInfo()->title, QStringLiteral("Applied by the tool"));
    QVERIFY(document->isModified());
    QVERIFY(!H::saver(tab)->isRunning());
    QCOMPARE(savedSpy.count(), 0);
    QCOMPARE(H::readFile(filePath), bytes);
}

void AppToolHooksTest::saveEnabledByQuestion()
{
    // A document without changes of its own: Save is offered only while the tool would ask (edited, with work, a
    // valid question), and the action of the window follows each change
    pdftest::NestedEventLoopGuard loopGuard;
    AppToolShellFixture fixture;
    const std::shared_ptr<HooksConfig> config = std::make_shared<HooksConfig>();
    config->hasSaveQuestion = true;
    std::shared_ptr<ShellTestTool> tool = H::registerTool(fixture, TOOL_ROTATE, config);
    QVERIFY(tool);
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, QStringLiteral("Enabled.pdf"));
    QVERIFY(tab);
    QVERIFY(!tab->getDocument()->isModified());
    QVERIFY(!tab->isCommandEnabled(PDFAppCommands::save()));
    QVERIFY(!H::isActionEnabled(fixture, PDFAppCommands::save()));

    PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(shell);
    HooksTestGUI* gui = H::gui(shell);

    // No work: nothing to ask about
    QVERIFY(!shell->hasSaveQuestion());
    QVERIFY(!tab->isCommandEnabled(PDFAppCommands::save()));
    QVERIFY(!H::isActionEnabled(fixture, PDFAppCommands::save()));

    // Work: the question, so Save
    gui->setWork(true);
    QVERIFY(shell->hasSaveQuestion());
    QVERIFY(tab->isCommandEnabled(PDFAppCommands::save()));
    QVERIFY(H::isActionEnabled(fixture, PDFAppCommands::save()));

    // The GUI has no question for this work
    gui->hasSaveQuestion = false;
    gui->updateConfirmState();
    QVERIFY(!shell->hasSaveQuestion());
    QVERIFY(!tab->isCommandEnabled(PDFAppCommands::save()));
    QVERIFY(!H::isActionEnabled(fixture, PDFAppCommands::save()));
    gui->hasSaveQuestion = true;
    gui->updateConfirmState();
    QVERIFY(H::isActionEnabled(fixture, PDFAppCommands::save()));

    // The work is gone
    gui->setWork(false);
    QVERIFY(!tab->isCommandEnabled(PDFAppCommands::save()));
    QVERIFY(!H::isActionEnabled(fixture, PDFAppCommands::save()));
    gui->setWork(true);
    QVERIFY(H::isActionEnabled(fixture, PDFAppCommands::save()));

    // A run: the tool is not edited
    tool->setHold(true);
    shell->confirm();
    QCOMPARE(shell->getState(), H::State::Running);
    QVERIFY(!shell->hasSaveQuestion());
    QVERIFY(!tab->isCommandEnabled(PDFAppCommands::save()));
    QVERIFY(!H::isActionEnabled(fixture, PDFAppCommands::save()));
    shell->stop();
    QVERIFY(AppToolShellFixture::waitForState(shell, H::State::Editing));
    tool->setHold(false);
    QVERIFY(shell->hasSaveQuestion());
    QVERIFY(H::isActionEnabled(fixture, PDFAppCommands::save()));

    // The tool closes: the document has nothing to save
    shell->cancel();
    AppToolShellFixture::settle();
    QVERIFY(!AppToolShellFixture::getShell(tab));
    QVERIFY(!tab->isCommandEnabled(PDFAppCommands::save()));
    QVERIFY(!H::isActionEnabled(fixture, PDFAppCommands::save()));
}

void AppToolHooksTest::saveQuestionOfReadOnlyFile_data()
{
    QTest::addColumn<QString>("choice");
    QTest::newRow("read-only-from-the-start") << QString();
    QTest::newRow("save-without-changes") << QStringLiteral("toolSaveWithoutChanges");
    QTest::newRow("apply-and-save") << QStringLiteral("toolSaveApply");
}

void AppToolHooksTest::saveQuestionOfReadOnlyFile()
{
    // R3-4, zadani.md KE.3: a file, which Save can never write (read-only), keeps Save disabled as without the tool -
    // the question enables Save only for a document Save could save once changed; Save As asks the question and saves
    // elsewhere. A file made read-only while the question is open: both save choices go the flow of the application
    // for such a file - the error of the saver, whose Save As writes the copy (the document of the user, or the result
    // of the tool kept in the tab); nothing is lost silently.
    QFETCH(QString, choice);
    pdftest::NestedEventLoopGuard loopGuard;
    AppToolShellFixture fixture;
    const std::shared_ptr<HooksConfig> config = std::make_shared<HooksConfig>();
    config->hasSaveQuestion = true;
    QVERIFY(H::registerTool(fixture, TOOL_ROTATE, config));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, QStringLiteral("ReadOnly.pdf"));
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    const QString filePath = document->getFilePath();
    const QString originalTitle = H::fileTitle(filePath);
    auto restore = qScopeGuard([filePath]() { H::setReadOnly(filePath, false); });
    const QString target = fixture.path(QStringLiteral("Saved elsewhere.pdf"));
    pdf::PDFAppFileDialogs::setSaveFileHandler([target](QWidget*, const QString&) { return target; });
    const bool isApplied = choice == QStringLiteral("toolSaveApply");
    if (choice == QStringLiteral("toolSaveWithoutChanges"))
    {
        QVERIFY(H::modify(document, QStringLiteral("Changed by the user")));
    }

    PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(shell);
    shell->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("title"));
    shell->getModel()->setValue(QStringLiteral("title"), QStringLiteral("Applied by the tool"));

    if (choice.isEmpty())
    {
        // Read-only before the work comes: the question exists, Save does not
        QVERIFY(H::setReadOnly(filePath, true));
        const QByteArray bytes = H::readFile(filePath);
        H::gui(shell)->setWork(true);
        QVERIFY(shell->hasSaveQuestion());
        QVERIFY(!tab->isCommandEnabled(PDFAppCommands::save()));
        QVERIFY(!H::isActionEnabled(fixture, PDFAppCommands::save()));
        QVERIFY(!tab->executeCommand(PDFAppCommands::save()));
        AppToolShellFixture::settle();
        QVERIFY(!H::question(shell));

        // Save As asks; its save choice writes the document elsewhere, the file stays
        QVERIFY(tab->isCommandEnabled(PDFAppCommands::saveAs()));
        QVERIFY(tab->executeCommand(PDFAppCommands::saveAs()));
        AppToolShellFixture::settle();
        QVERIFY(H::question(shell));
        QVERIFY(AppAsyncDialogs::answer(QStringLiteral("toolSaveQuestion"), QStringLiteral("toolSaveWithoutChanges")));
        QVERIFY(AppToolShellFixture::waitFor([document, target]() { return QFileInfo(document->getFilePath()) == QFileInfo(target); }));
        AppToolShellFixture::settle();
        QCOMPARE(H::fileTitle(target), originalTitle);
        QCOMPARE(H::readFile(filePath), bytes);
        QCOMPARE(shell->getState(), H::State::Editing);
        QVERIFY2(AppAsyncDialogs::isNothingOpen(), qPrintable(AppAsyncDialogs::describeOpen().join(QStringLiteral(", "))));
        return;
    }

    // The question is open while the file is writable; then the file becomes read-only
    H::gui(shell)->setWork(true);
    QVERIFY(tab->executeCommand(PDFAppCommands::save()));
    AppToolShellFixture::settle();
    QVERIFY(H::question(shell));
    QVERIFY(H::setReadOnly(filePath, true));
    const QByteArray bytes = H::readFile(filePath);
    QVERIFY(AppAsyncDialogs::answer(QStringLiteral("toolSaveQuestion"), choice));
    if (isApplied)
    {
        // The run with its result dialog, Keep in This Tab; the tool closes and the save of the result is refused
        pdf::PDFAppToolResultDialog* dialog = H::waitForResultDialog(shell);
        QVERIFY(dialog);
        QCOMPARE(dialog->getSelectedChoice(), pdf::PDFAppToolResultDialog::Choice::Keep);
        dialog->confirm();
        AppToolShellFixture::settle();
        QVERIFY(!AppToolShellFixture::getShell(tab));
        QCOMPARE(document->getDocument()->getInfo()->title, QStringLiteral("Applied by the tool"));
    }

    // The error of the saver says why and offers Save As, which writes the copy
    QVERIFY2(AppAsyncDialogs::findOpenDialog(QStringLiteral("saveErrorDialog")), qPrintable(AppAsyncDialogs::describeOpen().join(QStringLiteral(", "))));
    QVERIFY(document->isModified());
    QVERIFY(AppAsyncDialogs::answer(QStringLiteral("saveErrorDialog"), QStringLiteral("saveAsButton")));
    QVERIFY(AppToolShellFixture::waitFor([document]() { return !document->isModified(); }));
    AppToolShellFixture::settle();
    QCOMPARE(H::fileTitle(target), isApplied ? QStringLiteral("Applied by the tool") : QStringLiteral("Changed by the user"));
    QVERIFY(QFileInfo(document->getFilePath()) == QFileInfo(target));
    QCOMPARE(H::readFile(filePath), bytes);
    QVERIFY2(AppAsyncDialogs::isNothingOpen(), qPrintable(AppAsyncDialogs::describeOpen().join(QStringLiteral(", "))));
}

void AppToolHooksTest::autoRefreshDeferredByTool_data()
{
    QTest::addColumn<bool>("isDeferring");
    QTest::newRow("deferring") << true;
    QTest::newRow("tool-without-the-hook") << false;
}

void AppToolHooksTest::autoRefreshDeferredByTool()
{
    // R3-6, zadani_gui.md 4.21: the automatic refresh does not reload the document of the tab from its changed file
    // while the open tool says its work belongs to this version (Edit Content: its edits); the file is checked again
    // and reloaded after it stops saying so. A tool without the hook: the document is reloaded at once, as before.
    QFETCH(bool, isDeferring);
    AppToolShellFixture fixture;
    QVERIFY(H::registerTool(fixture, TOOL_ROTATE, std::make_shared<HooksConfig>()));
    const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Refresh.pdf"), 2);
    pdf::PDFAppDocumentTab* tab = fixture.open(file);
    QVERIFY(tab && AppToolShellFixture::waitFor([tab]() { return tab->getDocument() && tab->getDocument()->isReady(); }));
    pdf::PDFAppAutoRefresh* refresh = pdf::PDFAppDocumentFileCommandHandler::find(tab)->getAutoRefresh();
    refresh->setEnabled(true);

    PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(shell);
    HooksTestGUI* gui = H::gui(shell);
    gui->isReloadDeferred = isDeferring;
    QCOMPARE(shell->isDocumentReloadDeferred(), isDeferring);
    QCOMPARE(tab->getPage()->isDocumentReloadDeferred(), isDeferring);

    // Another program writes the file
    const QByteArray bytes = H::readFile(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Other.pdf"), 5));
    {
        QFile target(file);
        QVERIFY(target.open(QIODevice::WriteOnly));
        QCOMPARE(target.write(bytes), bytes.size());
    }
    refresh->checkNow();

    if (isDeferring)
    {
        // Several checks pass: nothing is reloaded under the work
        QTest::qWait(3 * pdf::PDFAppAutoRefresh::CHECK_DELAY);
        AppToolShellFixture::settle();
        QCOMPARE(refresh->getReloadCount(), 0);
        QCOMPARE(tab->getDocument()->getPageCount(), pdf::PDFInteger(2));
        QCOMPARE(AppToolShellFixture::getShell(tab), shell);

        // The work is gone (applied, discarded): the file is checked again by itself
        gui->isReloadDeferred = false;
    }
    QVERIFY(AppToolShellFixture::waitFor([refresh]() { return refresh->getReloadCount() == 1; }));
    QCOMPARE(tab->getDocument()->getPageCount(), pdf::PDFInteger(5));
}

void AppToolHooksTest::toolWithoutHooksSavesAsBefore()
{
    // A tool with work, which has no save question (every tool until now): Save saves the document at once, the
    // tool stays; Save of a document without changes stays disabled
    pdftest::NestedEventLoopGuard loopGuard;
    AppToolShellFixture fixture;
    QVERIFY(H::registerTool(fixture, TOOL_ROTATE, std::make_shared<HooksConfig>()));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, QStringLiteral("Plain.pdf"));
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    const QString filePath = document->getFilePath();
    PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(shell);
    HooksTestGUI* gui = H::gui(shell);
    gui->setWork(true);
    QVERIFY(shell->hasUnsavedWork());
    QVERIFY(!shell->hasSaveQuestion());
    QVERIFY(!tab->isCommandEnabled(PDFAppCommands::save()));
    QVERIFY(!H::isActionEnabled(fixture, PDFAppCommands::save()));
    QVERIFY(!tab->executeCommand(PDFAppCommands::save()));
    shell->cancel();
    AppToolShellFixture::settle();
    QVERIFY(!AppToolShellFixture::getShell(tab));

    QVERIFY(H::modify(document, QStringLiteral("Changed by the user")));
    shell = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(shell);
    H::gui(shell)->setWork(true);
    QVERIFY(tab->executeCommand(PDFAppCommands::save()));
    QVERIFY(!shell->getSaveQuestion());
    QVERIFY(AppAsyncDialogs::getOpenDialogs().isEmpty());
    QVERIFY(AppToolShellFixture::waitFor([document]() { return !document->isModified(); }));
    AppToolShellFixture::settle();
    QCOMPARE(H::fileTitle(filePath), QStringLiteral("Changed by the user"));
    QCOMPARE(AppToolShellFixture::getShell(tab), shell);
    QCOMPARE(shell->getState(), H::State::Editing);
}

void AppToolHooksTest::builderButtons()
{
    // zadani_gui.md 4.20, 4.25; edit-content-v2 1647:21313: the tools are icon buttons ("Toolbar button" 190:538,
    // 36 x 36, the icon 20) of one exclusive group at every width of the toolbar, their name is the tooltip; the
    // toggle Preview is a button with a text ("Toolbar menu button" 190:577, the icon 18)
    AppToolShellFixture fixture;
    const std::shared_ptr<HooksConfig> config = std::make_shared<HooksConfig>();
    config->hasToolButtons = true;
    QVERIFY(H::registerTool(fixture, TOOL_ROTATE, config));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, QStringLiteral("Buttons.pdf"));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(shell);
    HooksTestGUI* gui = H::gui(shell);
    QVERIFY(gui && gui->selectButton && gui->textButton && gui->imageButton && gui->multipleButton && gui->previewButton && gui->findButton);
    PDFAppToolToolbar* toolbar = shell->getToolbar();
    const std::vector<std::pair<QToolButton*, QString>> tools = {
        { gui->selectButton.data(), QStringLiteral("Select (V)") },
        { gui->textButton.data(), QStringLiteral("Text (T)") },
        { gui->imageButton.data(), QStringLiteral("Image (I)") },
        { gui->multipleButton.data(), QStringLiteral("Multiple") },
    };

    QCOMPARE(gui->selectButton->objectName(), QStringLiteral("contentTool-select"));
    QCOMPARE(gui->multipleButton->objectName(), QStringLiteral("contentMultiple"));
    QCOMPARE(gui->previewButton->objectName(), QStringLiteral("contentPreview"));
    for (const auto& [button, name] : tools)
    {
        QVERIFY(button->isCheckable());
        QVERIFY(button->text().isEmpty());
        QCOMPARE(button->toolTip(), name);
        QCOMPARE(button->accessibleName(), name);
        QVERIFY(!button->icon().isNull());
    }

    // An icon button at every level of the narrowing; the toggle with its text loses the text first
    QList<int> levels;
    for (const int width : { 1440, 1100, 900, 760, 640, 520, 420 })
    {
        fixture.window->resize(width, 960);
        AppToolShellFixture::settle();
        const int level = toolbar->getCollapseLevel();
        levels << level;
        for (const auto& [button, name] : tools)
        {
            const QString place = QStringLiteral("%1 at the width %2, level %3: %4 x %5").arg(name).arg(width).arg(level).arg(button->width()).arg(button->height());
            QVERIFY2(H::isIconOnly(button), qPrintable(place));
            QCOMPARE(button->iconSize(), QSize(20, 20));
            QCOMPARE(button->sizeHint(), QSize(36, 36));

            // Below the last level the content does not fit any more and the layout squeezes it
            QVERIFY2(level == 7 || button->size() == QSize(36, 36), qPrintable(place));
        }
        if (!toolbar->isPriorityCollapsed())
        {
            QVERIFY(!H::isIconOnly(gui->previewButton));
            QCOMPARE(gui->previewButton->toolButtonStyle(), Qt::ToolButtonTextBesideIcon);
            QCOMPARE(gui->previewButton->iconSize(), QSize(18, 18));
            QCOMPARE(gui->previewButton->height(), 36);
        }
        else
        {
            QVERIFY(H::isIconOnly(gui->previewButton));
        }
    }
    QCOMPARE(levels.first(), 0);
    QVERIFY2(levels.last() >= 3, qPrintable(QString::number(levels.last())));
    fixture.window->resize(1440, 960);
    AppToolShellFixture::settle();

    // The toggle with a text: checkable, no panel of the toolbar
    QCOMPARE(gui->previewButton->text(), QStringLiteral("Preview"));
    QCOMPARE(gui->previewButton->accessibleName(), QStringLiteral("Preview"));
    QCOMPARE(gui->previewButton->toolTip(), QStringLiteral("Preview"));
    QVERIFY(gui->previewButton->isCheckable());
    QVERIFY(toolbar->getPanelButtons().empty());
    gui->previewButton->click();
    QVERIFY(gui->previewButton->isChecked());
    QVERIFY(!toolbar->getOpenPanel());
    gui->previewButton->click();
    QVERIFY(!gui->previewButton->isChecked());

    // One exclusive group of the tools, the caller's; Multiple is no part of it
    QButtonGroup* group = gui->toolGroup.data();
    QVERIFY(group);
    QCOMPARE(group->buttons().size(), 3);
    QVERIFY(!group->buttons().contains(gui->multipleButton.data()));
    QCOMPARE(group->checkedButton(), static_cast<QAbstractButton*>(gui->selectButton.data()));
    QSignalSpy clickedSpy(group, &QButtonGroup::buttonClicked);
    gui->textButton->click();
    QVERIFY(gui->textButton->isChecked());
    QVERIFY(!gui->selectButton->isChecked());
    QVERIFY(!gui->imageButton->isChecked());
    QCOMPARE(clickedSpy.count(), 1);
    QCOMPARE(clickedSpy.first().first().value<QAbstractButton*>(), static_cast<QAbstractButton*>(gui->textButton.data()));

    // A click on the checked tool keeps it (one tool is always on)
    gui->textButton->click();
    QVERIFY(gui->textButton->isChecked());
    gui->imageButton->click();
    QCOMPARE(group->checkedButton(), static_cast<QAbstractButton*>(gui->imageButton.data()));

    // Multiple toggles alone
    gui->multipleButton->click();
    QVERIFY(gui->multipleButton->isChecked());
    QCOMPARE(group->checkedButton(), static_cast<QAbstractButton*>(gui->imageButton.data()));
    gui->multipleButton->click();
    QVERIFY(!gui->multipleButton->isChecked());
    QCOMPARE(group->checkedButton(), static_cast<QAbstractButton*>(gui->imageButton.data()));
    QCOMPARE(clickedSpy.count(), 3);
}

void AppToolHooksTest::labelPriority()
{
    // The owner's decision (zadani.md chapter 10): in a narrow window the labels of Find, View, Preview and Clear
    // Page go first - before Tutorial loses its text (level 1), before the title goes (level 2) and before the
    // other actions lose theirs (level 3). The step is no level.
    AppToolShellFixture fixture;
    std::map<QString, QToolButton*> buttons;
    std::unique_ptr<QWidget, AppToolShellFixture::DeleteNow> holder(new QWidget());
    PDFAppToolToolbar* toolbar = H::createToolbar(holder.get(), true, &buttons);
    QVERIFY(toolbar);
    const QStringList priorityNames = { QStringLiteral("find"), QStringLiteral("view"), QStringLiteral("preview"), QStringLiteral("clear") };
    QToolButton* arrange = buttons[QStringLiteral("arrange")];

    // Everything fits: all the texts
    const int fullWidth = toolbar->layout()->sizeHint().width();
    QVERIFY(fullWidth > 0 && fullWidth < 4000);
    for (const int width : { 4000, fullWidth })
    {
        H::setWidth(toolbar, width);
        QCOMPARE(toolbar->getCollapseLevel(), 0);
        QVERIFY(!toolbar->isPriorityCollapsed());
        for (const auto& [name, button] : buttons)
        {
            QVERIFY2(!H::isIconOnly(button), qPrintable(name));
            QCOMPARE(button->iconSize(), QSize(18, 18));
        }
    }

    // One pixel less: only the four labels go
    QVERIFY(H::setWidth(toolbar, fullWidth - 1));
    QCOMPARE(toolbar->getCollapseLevel(), 0);
    QVERIFY(toolbar->isPriorityCollapsed());
    for (const QString& name : priorityNames)
    {
        QVERIFY2(H::isIconOnly(buttons[name]), qPrintable(name));
        QCOMPARE(buttons[name]->iconSize(), QSize(20, 20));
        QVERIFY(buttons[name]->isVisible());
    }
    QVERIFY(!H::isIconOnly(arrange));
    QVERIFY(!H::isIconOnly(toolbar->getTutorialButton()));
    QVERIFY(toolbar->getTitleLabel()->isVisible());
    QVERIFY(toolbar->getUndoButton()->isVisible());

    // The toolbar is narrower by the four texts now, and stays so down to that width
    const int priorityWidth = toolbar->layout()->sizeHint().width();
    QVERIFY2(priorityWidth < fullWidth, qPrintable(QStringLiteral("%1 %2").arg(priorityWidth).arg(fullWidth)));
    H::setWidth(toolbar, priorityWidth);
    QCOMPARE(toolbar->getCollapseLevel(), 0);
    QVERIFY(toolbar->isPriorityCollapsed());
    QVERIFY(!H::isIconOnly(arrange));

    // Then the levels come as they are numbered: 1 - Tutorial without its text; the other action keeps its text
    H::setWidth(toolbar, priorityWidth - 1);
    QCOMPARE(toolbar->getCollapseLevel(), 1);
    QVERIFY(toolbar->isPriorityCollapsed());
    QVERIFY(H::isIconOnly(toolbar->getTutorialButton()));
    QVERIFY(toolbar->getTitleLabel()->isVisible());
    QVERIFY(!H::isIconOnly(arrange));
    QVERIFY(H::isIconOnly(buttons[QStringLiteral("find")]));

    // Narrower and narrower: the level never falls, the four labels never come back, the other action loses its
    // text at the level 3
    int lastLevel = 1;
    for (int width = priorityWidth - 1; width >= 40; width -= 5)
    {
        H::setWidth(toolbar, width);
        const int level = toolbar->getCollapseLevel();
        QVERIFY2(level >= lastLevel, qPrintable(QStringLiteral("%1 at %2").arg(level).arg(width)));
        QVERIFY(toolbar->isPriorityCollapsed());
        for (const QString& name : priorityNames)
        {
            QVERIFY2(H::isIconOnly(buttons[name]), qPrintable(QStringLiteral("%1 at %2").arg(name).arg(width)));
        }
        QCOMPARE(H::isIconOnly(arrange), level >= 3);
        QCOMPARE(toolbar->getTitleLabel()->isVisible(), level < 2);
        lastLevel = level;
    }
    QCOMPARE(lastLevel, 7);

    // Wide again: everything is back
    H::setWidth(toolbar, fullWidth);
    QCOMPARE(toolbar->getCollapseLevel(), 0);
    QVERIFY(!toolbar->isPriorityCollapsed());
    for (const auto& [name, button] : buttons)
    {
        QVERIFY2(!H::isIconOnly(button), qPrintable(name));
        QCOMPARE(button->text(), button->toolTip());
    }
}

void AppToolHooksTest::labelPriorityKeepsLevels()
{
    // A toolbar without a button of the priority (every tool until now): the step does not exist - one pixel below
    // the full width is the level 1, as it always was
    AppToolShellFixture fixture;
    std::map<QString, QToolButton*> buttons;
    std::unique_ptr<QWidget, AppToolShellFixture::DeleteNow> holder(new QWidget());
    PDFAppToolToolbar* toolbar = H::createToolbar(holder.get(), false, &buttons);
    QVERIFY(toolbar);
    const int fullWidth = toolbar->layout()->sizeHint().width();
    H::setWidth(toolbar, fullWidth);
    QCOMPARE(toolbar->getCollapseLevel(), 0);
    QVERIFY(!toolbar->isPriorityCollapsed());

    QVERIFY(H::setWidth(toolbar, fullWidth - 1));
    QCOMPARE(toolbar->getCollapseLevel(), 1);
    QVERIFY(H::isIconOnly(toolbar->getTutorialButton()));
    for (const auto& [name, button] : buttons)
    {
        QVERIFY2(!H::isIconOnly(button), qPrintable(name));
    }

    // The getter is never true at the level 0 and always above it
    for (int width = fullWidth + 3; width >= 40; width -= 3)
    {
        H::setWidth(toolbar, width);
        QCOMPARE(toolbar->isPriorityCollapsed(), toolbar->getCollapseLevel() > 0);
        QCOMPARE(toolbar->getCollapseLevel() > 0, width < fullWidth);
    }
}

void AppToolHooksTest::imageFilter()
{
    // The dialogs of images (the tool Image, Replace…): one entry with every format QImageReader reads, then all files
    const QString filter = pdf::PDFAppFileDialogs::getImageFilter();
    const QStringList entries = filter.split(QStringLiteral(";;"));
    QCOMPARE(entries.size(), 2);
    QCOMPARE(entries[1], QStringLiteral("All files (*)"));
    QVERIFY2(entries[0].startsWith(QStringLiteral("Images (")) && entries[0].endsWith(QLatin1Char(')')), qPrintable(entries[0]));

    const QString inner = entries[0].mid(8, entries[0].size() - 9);
    const QStringList patterns = inner.split(QLatin1Char(' '));
    const QList<QByteArray> formats = QImageReader::supportedImageFormats();
    QCOMPARE(patterns.size(), formats.size());
    for (const QString& pattern : patterns)
    {
        QVERIFY2(pattern.startsWith(QStringLiteral("*.")), qPrintable(pattern));
        QVERIFY2(formats.contains(pattern.mid(2).toLatin1()), qPrintable(pattern));
    }
    QCOMPARE(QSet<QString>(patterns.cbegin(), patterns.cend()).size(), patterns.size());

    // The formats Qt always reads
    for (const QString& pattern : { QStringLiteral("*.png"), QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"), QStringLiteral("*.bmp") })
    {
        QVERIFY2(patterns.contains(pattern), qPrintable(pattern));
    }

    using Mode = pdf::PDFAppFileDialogRequest::Mode;
    const pdf::PDFAppFileDialogRequest::Settings single = pdf::PDFAppFileDialogs::getOpenImageFileNamesSettings(QStringLiteral("C:/pictures"), false);
    QCOMPARE(single.mode, Mode::OpenFile);
    QCOMPARE(single.title, QStringLiteral("Open Image"));
    QCOMPARE(single.path, QStringLiteral("C:/pictures"));
    QCOMPARE(single.filter, filter);
    QVERIFY(single.defaultSuffix.isEmpty());

    const pdf::PDFAppFileDialogRequest::Settings multiple = pdf::PDFAppFileDialogs::getOpenImageFileNamesSettings(QStringLiteral("C:/pictures"), true);
    QCOMPARE(multiple.mode, Mode::OpenFiles);
    QCOMPARE(multiple.title, QStringLiteral("Open Images"));
    QCOMPARE(multiple.path, QStringLiteral("C:/pictures"));
    QCOMPARE(multiple.filter, filter);
}

void AppToolHooksTest::imageRequests()
{
    // The requests of the image dialogs: answered from the event loop (never inside the call) by the handler of the
    // open files - one file, or all of them
    pdftest::NestedEventLoopGuard loopGuard;
    AppToolShellFixture fixture;
    const QString first = fixture.path(QStringLiteral("first.png"));
    const QString second = fixture.path(QStringLiteral("second.png"));
    QStringList askedFolders;
    QStringList answer = { first, second };
    pdf::PDFAppFileDialogs::setOpenFilesHandler([&askedFolders, &answer](QWidget*, const QString& folder)
    {
        askedFolders << folder;
        return answer;
    });

    HooksFileReceiver receiver;
    pdf::PDFAppFileDialogRequest* request = pdf::PDFAppFileDialogs::requestOpenImageFileNames(fixture.window, fixture.folder(), &receiver, &HooksFileReceiver::onFiles);
    QVERIFY(request);
    QCOMPARE(request->getSettings().mode, pdf::PDFAppFileDialogRequest::Mode::OpenFiles);
    QCOMPARE(request->getSettings().filter, pdf::PDFAppFileDialogs::getImageFilter());
    QVERIFY(receiver.fileLists.isEmpty());
    QVERIFY(askedFolders.isEmpty());
    AppToolShellFixture::settle();
    QCOMPARE(askedFolders, QStringList{ fixture.folder() });
    QCOMPARE(receiver.fileLists.size(), 1);
    QCOMPARE(receiver.fileLists.first(), (QStringList{ first, second }));
    QVERIFY(receiver.files.isEmpty());

    // One image: the first file of the handler
    request = pdf::PDFAppFileDialogs::requestOpenImageFileName(fixture.window, fixture.folder(), &receiver, &HooksFileReceiver::onFile);
    QVERIFY(request);
    QCOMPARE(request->getSettings().mode, pdf::PDFAppFileDialogRequest::Mode::OpenFile);
    QCOMPARE(request->getSettings().filter, pdf::PDFAppFileDialogs::getImageFilter());
    QVERIFY(receiver.files.isEmpty());
    AppToolShellFixture::settle();
    QCOMPARE(receiver.files, QStringList{ first });
    QCOMPARE(receiver.fileLists.size(), 1);
    QCOMPARE(askedFolders.size(), 2);

    // Canceled: the slot gets nothing
    answer.clear();
    pdf::PDFAppFileDialogs::requestOpenImageFileName(fixture.window, fixture.folder(), &receiver, &HooksFileReceiver::onFile);
    pdf::PDFAppFileDialogs::requestOpenImageFileNames(fixture.window, fixture.folder(), &receiver, &HooksFileReceiver::onFiles);
    AppToolShellFixture::settle();
    QCOMPARE(receiver.files, (QStringList{ first, QString() }));
    QCOMPARE(receiver.fileLists.size(), 2);
    QVERIFY(receiver.fileLists.last().isEmpty());
    QVERIFY(pdf::PDFAppFileDialogRequest::getPendingRequests().empty());
}

QTEST_MAIN(AppToolHooksTest)

#include "tst_apptoolhookstest.moc"
