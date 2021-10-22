//    Copyright (C) 2020-2021 Jakub Melka
//
//    This file is part of PDF4QT.
//
//    PDF4QT is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    PDF4QT is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDF4QT.  If not, see <https://www.gnu.org/licenses/>.

#include "pdfprogramcontroller.h"
#include "pdfdrawwidget.h"
#include "pdfannotation.h"
#include "pdfform.h"
#include "pdfdocumentwriter.h"
#include "pdfadvancedtools.h"
#include "pdfdrawspacecontroller.h"
#include "pdfwidgetutils.h"
#include "pdfconstants.h"
#include "pdfdocumentbuilder.h"

#include "pdfviewersettings.h"
#include "pdfundoredomanager.h"
#include "pdfrendertoimagesdialog.h"
#include "pdfoptimizedocumentdialog.h"
#include "pdfviewersettingsdialog.h"
#include "pdfaboutdialog.h"
#include "pdfrenderingerrorswidget.h"
#include "pdfsendmail.h"
#include "pdfrecentfilemanager.h"
#include "pdftexttospeech.h"
#include "pdfencryptionsettingsdialog.h"

#include <QMenu>
#include <QPrinter>
#include <QPrintDialog>
#include <QMessageBox>
#include <QDesktopServices>
#include <QApplication>
#include <QFileDialog>
#include <QtConcurrent/QtConcurrent>
#include <QInputDialog>
#include <QDesktopWidget>
#include <QMainWindow>
#include <QToolBar>
#include <QXmlStreamWriter>

#ifdef Q_OS_WIN
#pragma comment(lib, "Shell32")
#endif

namespace pdfviewer
{

PDFActionManager::PDFActionManager(QObject* parent) :
    BaseClass(parent),
    m_actions(),
    m_actionGroups()
{

}

QToolButton* PDFActionManager::createToolButtonForActionGroup(ActionGroup group, QWidget* parent) const
{
    QActionGroup* actionGroup = getActionGroup(group);

    if (!actionGroup)
    {
        return nullptr;
    }

    QToolButton* toolButton = new QToolButton(parent);
    toolButton->setPopupMode(QToolButton::MenuButtonPopup);
    toolButton->setMenu(new QMenu(toolButton));
    QList<QAction*> actions = actionGroup->actions();
    auto onInsertStickyNotesActionTriggered = [toolButton](QAction* action)
    {
        if (action)
        {
            toolButton->setDefaultAction(action);
        }
    };
    connect(actionGroup, &QActionGroup::triggered, toolButton, onInsertStickyNotesActionTriggered);
    for (QAction* action : actions)
    {
        toolButton->menu()->addAction(action);
    }
    toolButton->setDefaultAction(actions.front());

    return toolButton;
}

void PDFActionManager::setShortcut(Action type, QKeySequence sequence)
{
    if (QAction* action = getAction(type))
    {
        action->setShortcut(sequence);
    }
}

void PDFActionManager::setUserData(Action type, QVariant userData)
{
    if (QAction* action = getAction(type))
    {
        action->setData(userData);
    }
}

void PDFActionManager::setEnabled(Action type, bool enabled)
{
    if (QAction* action = getAction(type))
    {
        action->setEnabled(enabled);
    }
}

void PDFActionManager::setChecked(PDFActionManager::Action type, bool checked)
{
    if (QAction* action = getAction(type))
    {
        action->setChecked(checked);
    }
}

std::vector<QAction*> PDFActionManager::getRenderingOptionActions() const
{
    return getActionList({
         RenderOptionAntialiasing,
         RenderOptionTextAntialiasing,
         RenderOptionSmoothPictures,
         RenderOptionIgnoreOptionalContentSettings,
         RenderOptionDisplayAnnotations,
         RenderOptionInvertColors,
         RenderOptionShowTextBlocks,
         RenderOptionShowTextLines
                         });
}

std::vector<QAction*> PDFActionManager::getActions() const
{
    std::vector<QAction*> result;
    result.reserve(LastAction + m_additionalActions.size());

    for (int i = 0; i < LastAction; ++i)
    {
        if (QAction* action = getAction(Action(i)))
        {
            result.push_back(action);
        }
    }

    result.insert(result.cend(), m_additionalActions.cbegin(), m_additionalActions.cend());
    return result;
}

void PDFActionManager::addAdditionalAction(QAction* action)
{
    m_additionalActions.push_back(action);
}

void PDFActionManager::initActions(QSize iconSize, bool initializeStampActions)
{
    setShortcut(Open, QKeySequence::Open);
    setShortcut(Close, QKeySequence::Close);
    setShortcut(Quit, QKeySequence::Quit);
    setShortcut(ZoomIn, QKeySequence::ZoomIn);
    setShortcut(ZoomOut, QKeySequence::ZoomOut);
    setShortcut(Find, QKeySequence::Find);
    setShortcut(FindPrevious, QKeySequence::FindPrevious);
    setShortcut(FindNext, QKeySequence::FindNext);
    setShortcut(SelectTextAll, QKeySequence::SelectAll);
    setShortcut(DeselectText, QKeySequence::Deselect);
    setShortcut(CopyText, QKeySequence::Copy);
    setShortcut(RotateRight, QKeySequence("Ctrl+Shift++"));
    setShortcut(RotateLeft, QKeySequence("Ctrl+Shift+-"));
    setShortcut(Print, QKeySequence::Print);
    setShortcut(Undo, QKeySequence::Undo);
    setShortcut(Redo, QKeySequence::Redo);
    setShortcut(Save, QKeySequence::Save);
    setShortcut(SaveAs, QKeySequence::SaveAs);
    setShortcut(GoToDocumentStart, QKeySequence::MoveToStartOfDocument);
    setShortcut(GoToDocumentEnd, QKeySequence::MoveToEndOfDocument);
    setShortcut(GoToNextPage, QKeySequence::MoveToNextPage);
    setShortcut(GoToPreviousPage, QKeySequence::MoveToPreviousPage);
    setShortcut(GoToNextLine, QKeySequence::MoveToNextLine);
    setShortcut(GoToPreviousLine, QKeySequence::MoveToPreviousLine);

    if (hasActions({ CreateStickyNoteComment, CreateStickyNoteHelp, CreateStickyNoteInsert, CreateStickyNoteKey, CreateStickyNoteNewParagraph, CreateStickyNoteNote, CreateStickyNoteParagraph }))
    {
        m_actionGroups[CreateStickyNoteGroup] = new QActionGroup(this);
        m_actionGroups[CreateStickyNoteGroup]->setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional);
        m_actionGroups[CreateStickyNoteGroup]->addAction(getAction(CreateStickyNoteComment));
        m_actionGroups[CreateStickyNoteGroup]->addAction(getAction(CreateStickyNoteHelp));
        m_actionGroups[CreateStickyNoteGroup]->addAction(getAction(CreateStickyNoteInsert));
        m_actionGroups[CreateStickyNoteGroup]->addAction(getAction(CreateStickyNoteKey));
        m_actionGroups[CreateStickyNoteGroup]->addAction(getAction(CreateStickyNoteNewParagraph));
        m_actionGroups[CreateStickyNoteGroup]->addAction(getAction(CreateStickyNoteNote));
        m_actionGroups[CreateStickyNoteGroup]->addAction(getAction(CreateStickyNoteParagraph));

        getAction(CreateStickyNoteComment)->setData(int(pdf::TextAnnotationIcon::Comment));
        getAction(CreateStickyNoteHelp)->setData(int(pdf::TextAnnotationIcon::Help));
        getAction(CreateStickyNoteInsert)->setData(int(pdf::TextAnnotationIcon::Insert));
        getAction(CreateStickyNoteKey)->setData(int(pdf::TextAnnotationIcon::Key));
        getAction(CreateStickyNoteNewParagraph)->setData(int(pdf::TextAnnotationIcon::NewParagraph));
        getAction(CreateStickyNoteNote)->setData(int(pdf::TextAnnotationIcon::Note));
        getAction(CreateStickyNoteParagraph)->setData(int(pdf::TextAnnotationIcon::Paragraph));

        getAction(CreateStickyNoteComment)->setIcon(pdf::PDFTextAnnotation::createIcon("Comment", iconSize));
        getAction(CreateStickyNoteHelp)->setIcon(pdf::PDFTextAnnotation::createIcon("Help", iconSize));
        getAction(CreateStickyNoteInsert)->setIcon(pdf::PDFTextAnnotation::createIcon("Insert", iconSize));
        getAction(CreateStickyNoteKey)->setIcon(pdf::PDFTextAnnotation::createIcon("Key", iconSize));
        getAction(CreateStickyNoteNewParagraph)->setIcon(pdf::PDFTextAnnotation::createIcon("NewParagraph", iconSize));
        getAction(CreateStickyNoteNote)->setIcon(pdf::PDFTextAnnotation::createIcon("Note", iconSize));
        getAction(CreateStickyNoteParagraph)->setIcon(pdf::PDFTextAnnotation::createIcon("Paragraph", iconSize));
    }

    if (hasActions({ CreateTextHighlight, CreateTextUnderline, CreateTextStrikeout, CreateTextSquiggly }))
    {
        m_actionGroups[CreateTextHighlightGroup] = new QActionGroup(this);
        m_actionGroups[CreateTextHighlightGroup]->setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional);
        m_actionGroups[CreateTextHighlightGroup]->addAction(getAction(CreateTextHighlight));
        m_actionGroups[CreateTextHighlightGroup]->addAction(getAction(CreateTextUnderline));
        m_actionGroups[CreateTextHighlightGroup]->addAction(getAction(CreateTextStrikeout));
        m_actionGroups[CreateTextHighlightGroup]->addAction(getAction(CreateTextSquiggly));

        getAction(CreateTextHighlight)->setData(int(pdf::AnnotationType::Highlight));
        getAction(CreateTextUnderline)->setData(int(pdf::AnnotationType::Underline));
        getAction(CreateTextStrikeout)->setData(int(pdf::AnnotationType::StrikeOut));
        getAction(CreateTextSquiggly)->setData(int(pdf::AnnotationType::Squiggly));
    }

    setUserData(RenderOptionAntialiasing, pdf::PDFRenderer::Antialiasing);
    setUserData(RenderOptionTextAntialiasing, pdf::PDFRenderer::TextAntialiasing);
    setUserData(RenderOptionSmoothPictures, pdf::PDFRenderer::SmoothImages);
    setUserData(RenderOptionIgnoreOptionalContentSettings, pdf::PDFRenderer::IgnoreOptionalContent);
    setUserData(RenderOptionDisplayAnnotations, pdf::PDFRenderer::DisplayAnnotations);
    setUserData(RenderOptionInvertColors, pdf::PDFRenderer::InvertColors);
    setUserData(RenderOptionShowTextBlocks, pdf::PDFRenderer::DebugTextBlocks);
    setUserData(RenderOptionShowTextLines, pdf::PDFRenderer::DebugTextLines);

    if (initializeStampActions)
    {
        m_actionGroups[CreateStampGroup] = new QActionGroup(this);
        m_actionGroups[CreateStampGroup]->setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional);

        auto createCreateStampAction = [this](Action actionType, pdf::Stamp stamp)
        {
            QString text = pdf::PDFStampAnnotation::getText(stamp);
            QAction* action = new QAction(text, this);
            action->setObjectName(QString("actionCreateStamp_%1").arg(int(stamp)));
            action->setData(int(stamp));
            action->setCheckable(true);
            m_actions[actionType] = action;
            m_actionGroups[CreateStampGroup]->addAction(action);
        };

        createCreateStampAction(CreateStampApproved, pdf::Stamp::Approved);
        createCreateStampAction(CreateStampAsIs, pdf::Stamp::AsIs);
        createCreateStampAction(CreateStampConfidential, pdf::Stamp::Confidential);
        createCreateStampAction(CreateStampDepartmental, pdf::Stamp::Departmental);
        createCreateStampAction(CreateStampDraft, pdf::Stamp::Draft);
        createCreateStampAction(CreateStampExperimental, pdf::Stamp::Experimental);
        createCreateStampAction(CreateStampExpired, pdf::Stamp::Expired);
        createCreateStampAction(CreateStampFinal, pdf::Stamp::Final);
        createCreateStampAction(CreateStampForComment, pdf::Stamp::ForComment);
        createCreateStampAction(CreateStampForPublicRelease, pdf::Stamp::ForPublicRelease);
        createCreateStampAction(CreateStampNotApproved, pdf::Stamp::NotApproved);
        createCreateStampAction(CreateStampNotForPublicRelease, pdf::Stamp::NotForPublicRelease);
        createCreateStampAction(CreateStampSold, pdf::Stamp::Sold);
        createCreateStampAction(CreateStampTopSecret, pdf::Stamp::TopSecret);
    }
}

bool PDFActionManager::hasActions(const std::initializer_list<Action>& actionTypes) const
{
    for (Action actionType : actionTypes)
    {
        if (!getAction(actionType))
        {
            return false;
        }
    }

    return true;
}

std::vector<QAction*> PDFActionManager::getActionList(const std::initializer_list<PDFActionManager::Action>& actionTypes) const
{
    std::vector<QAction*> result;
    result.reserve(actionTypes.size());

    for (const Action actionType : actionTypes)
    {
        if (QAction* action = getAction(actionType))
        {
            result.push_back(action);
        }
    }

    return result;
}

PDFProgramController::PDFProgramController(QObject* parent) :
    BaseClass(parent),
    m_actionManager(nullptr),
    m_mainWindow(nullptr),
    m_mainWindowInterface(nullptr),
    m_pdfWidget(nullptr),
    m_settings(new PDFViewerSettings(this)),
    m_undoRedoManager(nullptr),
    m_recentFileManager(new PDFRecentFileManager(this)),
    m_optionalContentActivity(nullptr),
    m_futureWatcher(nullptr),
    m_textToSpeech(nullptr),
    m_CMSManager(new pdf::PDFCMSManager(this)),
    m_toolManager(nullptr),
    m_annotationManager(nullptr),
    m_formManager(nullptr),
    m_isBusy(false),
    m_progress(nullptr)
{

}

PDFProgramController::~PDFProgramController()
{
    delete m_formManager;
    m_formManager = nullptr;

    delete m_annotationManager;
    m_annotationManager = nullptr;
}

void PDFProgramController::initializeAnnotationManager()
{
    m_annotationManager = new pdf::PDFWidgetAnnotationManager(m_pdfWidget->getDrawWidgetProxy(), this);
    connect(m_annotationManager, &pdf::PDFWidgetAnnotationManager::actionTriggered, this, &PDFProgramController::onActionTriggered);
    connect(m_annotationManager, &pdf::PDFWidgetAnnotationManager::documentModified, this, &PDFProgramController::onDocumentModified);
    m_pdfWidget->setAnnotationManager(m_annotationManager);
}

void PDFProgramController::initializeFormManager()
{
    m_formManager = new pdf::PDFFormManager(m_pdfWidget->getDrawWidgetProxy(), this);
    m_formManager->setAnnotationManager(m_annotationManager);
    m_formManager->setAppearanceFlags(m_settings->getSettings().m_formAppearanceFlags);
    m_annotationManager->setFormManager(m_formManager);
    m_pdfWidget->setFormManager(m_formManager);
    connect(m_formManager, &pdf::PDFFormManager::actionTriggered, this, &PDFProgramController::onActionTriggered);
    connect(m_formManager, &pdf::PDFFormManager::documentModified, this, &PDFProgramController::onDocumentModified);
}

void PDFProgramController::initialize(Features features,
                                      QMainWindow* mainWindow,
                                      IMainWindow* mainWindowInterface,
                                      PDFActionManager* actionManager,
                                      pdf::PDFProgress* progress)
{
    Q_ASSERT(!m_actionManager);
    m_actionManager = actionManager;
    m_progress = progress;
    m_mainWindow = mainWindow;
    m_mainWindowInterface = mainWindowInterface;

    if (QAction* action = m_actionManager->getAction(PDFActionManager::GoToDocumentStart))
    {
        connect(action, &QAction::triggered, this, &PDFProgramController::onActionGoToDocumentStartTriggered);
    }
    if (QAction* action = m_actionManager->getAction(PDFActionManager::GoToDocumentEnd))
    {
        connect(action, &QAction::triggered, this, &PDFProgramController::onActionGoToDocumentEndTriggered);
    }
    if (QAction* action = m_actionManager->getAction(PDFActionManager::GoToNextPage))
    {
        connect(action, &QAction::triggered, this, &PDFProgramController::onActionGoToNextPageTriggered);
    }
    if (QAction* action = m_actionManager->getAction(PDFActionManager::GoToPreviousPage))
    {
        connect(action, &QAction::triggered, this, &PDFProgramController::onActionGoToPreviousPageTriggered);
    }
    if (QAction* action = m_actionManager->getAction(PDFActionManager::GoToNextLine))
    {
        connect(action, &QAction::triggered, this, &PDFProgramController::onActionGoToNextLineTriggered);
    }
    if (QAction* action = m_actionManager->getAction(PDFActionManager::GoToPreviousLine))
    {
        connect(action, &QAction::triggered, this, &PDFProgramController::onActionGoToPreviousLineTriggered);
    }
    if (QAction* action = m_actionManager->getAction(PDFActionManager::ZoomIn))
    {
        connect(action, &QAction::triggered, this, &PDFProgramController::onActionZoomIn);
    }
    if (QAction* action = m_actionManager->getAction(PDFActionManager::ZoomOut))
    {
        connect(action, &QAction::triggered, this, &PDFProgramController::onActionZoomOut);
    }
    for (QAction* action : m_actionManager->getRenderingOptionActions())
    {
        connect(action, &QAction::triggered, this, &PDFProgramController::onActionRenderingOptionTriggered);
    }
    if (QAction* action = m_actionManager->getAction(PDFActionManager::Print))
    {
        connect(action, &QAction::triggered, this, &PDFProgramController::performPrint);
    }
    if (QAction* action = m_actionManager->getAction(PDFActionManager::Save))
    {
        connect(action, &QAction::triggered, this, &PDFProgramController::performSave);
    }
    if (QAction* action = m_actionManager->getAction(PDFActionManager::SaveAs))
    {
        connect(action, &QAction::triggered, this, &PDFProgramController::performSaveAs);
    }
    if (QAction* action = m_actionManager->getAction(PDFActionManager::RotateLeft))
    {
        connect(action, &QAction::triggered, this, &PDFProgramController::onActionRotateLeftTriggered);
    }
    if (QAction* action = m_actionManager->getAction(PDFActionManager::RotateRight))
    {
        connect(action, &QAction::triggered, this, &PDFProgramController::onActionRotateRightTriggered);
    }
    if (QAction* action = m_actionManager->getAction(PDFActionManager::Properties))
    {
        connect(action, &QAction::triggered, this, &PDFProgramController::onActionPropertiesTriggered);
    }
    if (QAction* action = m_actionManager->getAction(PDFActionManager::About))
    {
        connect(action, &QAction::triggered, this, &PDFProgramController::onActionAboutTriggered);
    }
    if (QAction* action = m_actionManager->getAction(PDFActionManager::SendByMail))
    {
        connect(action, &QAction::triggered, this, &PDFProgramController::onActionSendByEMailTriggered);
    }
    if (QAction* action = m_actionManager->getAction(PDFActionManager::RenderToImages))
    {
        connect(action, &QAction::triggered, this, &PDFProgramController::onActionRenderToImagesTriggered);
    }
    if (QAction* action = m_actionManager->getAction(PDFActionManager::Optimize))
    {
        connect(action, &QAction::triggered, this, &PDFProgramController::onActionOptimizeTriggered);
    }
    if (QAction* action = m_actionManager->getAction(PDFActionManager::Encryption))
    {
        connect(action, &QAction::triggered, this, &PDFProgramController::onActionEncryptionTriggered);
    }
    if (QAction* action = m_actionManager->getAction(PDFActionManager::FitPage))
    {
        connect(action, &QAction::triggered, this, &PDFProgramController::onActionFitPageTriggered);
    }
    if (QAction* action = m_actionManager->getAction(PDFActionManager::FitWidth))
    {
        connect(action, &QAction::triggered, this, &PDFProgramController::onActionFitWidthTriggered);
    }
    if (QAction* action = m_actionManager->getAction(PDFActionManager::FitHeight))
    {
        connect(action, &QAction::triggered, this, &PDFProgramController::onActionFitHeightTriggered);
    }
    if (QAction* action = m_actionManager->getAction(PDFActionManager::ShowRenderingErrors))
    {
        connect(action, &QAction::triggered, this, &PDFProgramController::onActionRenderingErrorsTriggered);
    }
    if (QAction* action = m_actionManager->getAction(PDFActionManager::PageLayoutSinglePage))
    {
        connect(action, &QAction::triggered, this, &PDFProgramController::onActionPageLayoutSinglePageTriggered);
    }
    if (QAction* action = m_actionManager->getAction(PDFActionManager::PageLayoutContinuous))
    {
        connect(action, &QAction::triggered, this, &PDFProgramController::onActionPageLayoutContinuousTriggered);
    }
    if (QAction* action = m_actionManager->getAction(PDFActionManager::PageLayoutTwoPages))
    {
        connect(action, &QAction::triggered, this, &PDFProgramController::onActionPageLayoutTwoPagesTriggered);
    }
    if (QAction* action = m_actionManager->getAction(PDFActionManager::PageLayoutTwoColumns))
    {
        connect(action, &QAction::triggered, this, &PDFProgramController::onActionPageLayoutTwoColumnsTriggered);
    }
    if (QAction* action = m_actionManager->getAction(PDFActionManager::PageLayoutFirstPageOnRightSide))
    {
        connect(action, &QAction::triggered, this, &PDFProgramController::onActionFirstPageOnRightSideTriggered);
    }
    if (QAction* action = m_actionManager->getAction(PDFActionManager::Find))
    {
        connect(action, &QAction::triggered, this, &PDFProgramController::onActionFindTriggered);
    }
    if (QAction* action = m_actionManager->getAction(PDFActionManager::Options))
    {
        connect(action, &QAction::triggered, this, &PDFProgramController::onActionOptionsTriggered);
    }
    if (QAction* action = m_actionManager->getAction(PDFActionManager::Open))
    {
        connect(action, &QAction::triggered, this, &PDFProgramController::onActionOpenTriggered);
    }
    if (QAction* action = m_actionManager->getAction(PDFActionManager::Close))
    {
        connect(action, &QAction::triggered, this, &PDFProgramController::onActionCloseTriggered);
    }
    if (QAction* action = m_actionManager->getAction(PDFActionManager::DeveloperCreateInstaller))
    {
        connect(action, &QAction::triggered, this, &PDFProgramController::onActionDeveloperCreateInstaller);
    }
    if (QAction* action = m_actionManager->getAction(PDFActionManager::GetSource))
    {
        connect(action, &QAction::triggered, this, &PDFProgramController::onActionGetSource);
    }

    if (m_recentFileManager)
    {
        connect(m_recentFileManager, &PDFRecentFileManager::fileOpenRequest, this, &PDFProgramController::openDocument);

        for (QAction* action : m_recentFileManager->getActions())
        {
            m_actionManager->addAdditionalAction(action);
        }
    }

    readSettings(Settings(WindowSettings | GeneralSettings | PluginsSettings | RecentFileSettings | CertificateSettings));

    m_pdfWidget = new pdf::PDFWidget(m_CMSManager, m_settings->getRendererEngine(), m_settings->isMultisampleAntialiasingEnabled() ? m_settings->getRendererSamples() : -1, m_mainWindow);
    m_pdfWidget->updateCacheLimits(m_settings->getCompiledPageCacheLimit() * 1024, m_settings->getThumbnailsCacheLimit(), m_settings->getFontCacheLimit(), m_settings->getInstancedFontCacheLimit());
    m_pdfWidget->getDrawWidgetProxy()->setProgress(m_progress);

    connect(this, &PDFProgramController::queryPasswordRequest, this, &PDFProgramController::onQueryPasswordRequest, Qt::BlockingQueuedConnection);
    connect(m_pdfWidget->getDrawWidgetProxy(), &pdf::PDFDrawWidgetProxy::drawSpaceChanged, this, &PDFProgramController::onDrawSpaceChanged);
    connect(m_pdfWidget->getDrawWidgetProxy(), &pdf::PDFDrawWidgetProxy::pageLayoutChanged, this, &PDFProgramController::onPageLayoutChanged);
    connect(m_pdfWidget, &pdf::PDFWidget::pageRenderingErrorsChanged, this, &PDFProgramController::onPageRenderingErrorsChanged, Qt::QueuedConnection);
    connect(m_settings, &PDFViewerSettings::settingsChanged, this, &PDFProgramController::onViewerSettingsChanged);
    connect(m_CMSManager, &pdf::PDFCMSManager::colorManagementSystemChanged, this, &PDFProgramController::onColorManagementSystemChanged);

    if (features.testFlag(TextToSpeech))
    {
        m_textToSpeech = new PDFTextToSpeech(this);
        m_textToSpeech->setProxy(m_pdfWidget->getDrawWidgetProxy());
        m_textToSpeech->setSettings(m_settings);
    }

    initializeAnnotationManager();

    if (features.testFlag(Forms))
    {
        initializeFormManager();
    }

    if (features.testFlag(Tools))
    {
        initializeToolManager();
    }

    if (features.testFlag(UndoRedo))
    {
        // Connect undo/redo manager
        m_undoRedoManager = new PDFUndoRedoManager(this);
        connect(m_undoRedoManager, &PDFUndoRedoManager::undoRedoStateChanged, this, &PDFProgramController::updateUndoRedoActions);
        connect(m_undoRedoManager, &PDFUndoRedoManager::documentChangeRequest, this, &PDFProgramController::onDocumentUndoRedo);
        connect(m_actionManager->getAction(PDFActionManager::Undo), &QAction::triggered, m_undoRedoManager, &PDFUndoRedoManager::doUndo);
        connect(m_actionManager->getAction(PDFActionManager::Redo), &QAction::triggered, m_undoRedoManager, &PDFUndoRedoManager::doRedo);
        updateUndoRedoSettings();
    }

    if (features.testFlag(Plugins))
    {
        loadPlugins();
    }
}

void PDFProgramController::finishInitialization()
{
    readSettings(ActionSettings);

    if (m_textToSpeech)
    {
        m_textToSpeech->setSettings(m_settings);
    }

    updatePageLayoutActions();
    m_mainWindowInterface->updateUI(true);
    onViewerSettingsChanged();
    updateActionsAvailability();
}

void PDFProgramController::performPrint()
{
    // Are we allowed to print in high resolution? If yes, then print in high resolution,
    // otherwise print in low resolution. If this action is triggered, then print operation
    // should be allowed (at least print in low resolution).
    const pdf::PDFObjectStorage& storage = m_pdfDocument->getStorage();
    const pdf::PDFSecurityHandler* securityHandler = storage.getSecurityHandler();
    QPrinter::PrinterMode printerMode = QPrinter::HighResolution;
    if (!securityHandler->isAllowed(pdf::PDFSecurityHandler::Permission::PrintHighResolution))
    {
        printerMode = QPrinter::ScreenResolution;
    }

    // Run print dialog
    QPrinter printer(printerMode);
    QPrintDialog printDialog(&printer, m_mainWindow);
    printDialog.setOptions(QPrintDialog::PrintPageRange | QPrintDialog::PrintShowPageSize | QPrintDialog::PrintCollateCopies | QPrintDialog::PrintSelection);
    printDialog.setOption(QPrintDialog::PrintCurrentPage, m_pdfWidget->getDrawWidget()->getCurrentPages().size() == 1);
    printDialog.setMinMax(1, int(m_pdfDocument->getCatalog()->getPageCount()));
    if (printDialog.exec() == QPrintDialog::Accepted)
    {
        std::vector<pdf::PDFInteger> pageIndices;
        switch (printDialog.printRange())
        {
            case QAbstractPrintDialog::AllPages:
            {
                pageIndices.resize(m_pdfDocument->getCatalog()->getPageCount(), 0);
                std::iota(pageIndices.begin(), pageIndices.end(), 0);
                break;
            }

            case QAbstractPrintDialog::Selection:
            case QAbstractPrintDialog::CurrentPage:
            {
                pageIndices = m_pdfWidget->getDrawWidget()->getCurrentPages();
                break;
            }

            case QAbstractPrintDialog::PageRange:
            {
                const pdf::PDFInteger fromPage = printDialog.fromPage();
                const pdf::PDFInteger toPage = printDialog.toPage();
                const pdf::PDFInteger pageCount = toPage - fromPage + 1;
                if (pageCount > 0)
                {
                    pageIndices.resize(pageCount, 0);
                    std::iota(pageIndices.begin(), pageIndices.end(), fromPage - 1);
                }
                break;
            }

            default:
                Q_ASSERT(false);
                break;
        }

        if (pageIndices.empty())
        {
            // Nothing to be printed
            return;
        }

        pdf::ProgressStartupInfo info;
        info.showDialog = true;
        info.text = tr("Printing document");
        m_progress->start(pageIndices.size(), qMove(info));
        printer.setFullPage(true);
        QPainter painter(&printer);

        const pdf::PDFCatalog* catalog = m_pdfDocument->getCatalog();
        pdf::PDFDrawWidgetProxy* proxy = m_pdfWidget->getDrawWidgetProxy();
        pdf::PDFOptionalContentActivity optionalContentActivity(m_pdfDocument.data(), pdf::OCUsage::Print, nullptr);
        pdf::PDFCMSPointer cms = proxy->getCMSManager()->getCurrentCMS();
        pdf::PDFRenderer renderer(m_pdfDocument.get(), proxy->getFontCache(), cms.data(), &optionalContentActivity, proxy->getFeatures(), proxy->getMeshQualitySettings());

        const pdf::PDFInteger lastPage = pageIndices.back();
        for (const pdf::PDFInteger pageIndex : pageIndices)
        {
            const pdf::PDFPage* page = catalog->getPage(pageIndex);
            Q_ASSERT(page);

            QRectF mediaBox = page->getRotatedMediaBox();
            QRectF paperRect = printer.paperRect();
            QSizeF scaledSize = mediaBox.size().scaled(paperRect.size(), Qt::KeepAspectRatio);
            mediaBox.setSize(scaledSize);
            mediaBox.moveCenter(paperRect.center());

            renderer.render(&painter, mediaBox, pageIndex);
            m_progress->step();

            if (pageIndex != lastPage)
            {
                if (!printer.newPage())
                {
                    break;
                }
            }
        }

        painter.end();
        m_progress->finish();
    }
}

void PDFProgramController::onActionTriggered(const pdf::PDFAction* action)
{
    Q_ASSERT(action);

    for (const pdf::PDFAction* currentAction : action->getActionList())
    {
        switch (action->getType())
        {
            case pdf::ActionType::GoTo:
            {
                const pdf::PDFActionGoTo* typedAction = dynamic_cast<const pdf::PDFActionGoTo*>(currentAction);
                pdf::PDFDestination destination = typedAction->getDestination();
                if (destination.getDestinationType() == pdf::DestinationType::Named)
                {
                    if (const pdf::PDFDestination* targetDestination = m_pdfDocument->getCatalog()->getNamedDestination(destination.getName()))
                    {
                        destination = *targetDestination;
                    }
                    else
                    {
                        destination = pdf::PDFDestination();
                        QMessageBox::critical(m_mainWindow, tr("Go to action"), tr("Failed to go to destination '%1'. Destination wasn't found.").arg(pdf::PDFEncoding::convertTextString(destination.getName())));
                    }
                }

                if (destination.getDestinationType() != pdf::DestinationType::Invalid &&
                    destination.getPageReference() != pdf::PDFObjectReference())
                {
                    const size_t pageIndex = m_pdfDocument->getCatalog()->getPageIndexFromPageReference(destination.getPageReference());
                    if (pageIndex != pdf::PDFCatalog::INVALID_PAGE_INDEX)
                    {
                        m_pdfWidget->getDrawWidgetProxy()->goToPage(pageIndex);

                        switch (destination.getDestinationType())
                        {
                            case pdf::DestinationType::Fit:
                                m_pdfWidget->getDrawWidgetProxy()->performOperation(pdf::PDFDrawWidgetProxy::ZoomFit);
                                break;

                            case pdf::DestinationType::FitH:
                                m_pdfWidget->getDrawWidgetProxy()->performOperation(pdf::PDFDrawWidgetProxy::ZoomFitWidth);
                                break;

                            case pdf::DestinationType::FitV:
                                m_pdfWidget->getDrawWidgetProxy()->performOperation(pdf::PDFDrawWidgetProxy::ZoomFitHeight);
                                break;

                            default:
                                break;
                        }
                    }
                }

                break;
            }

            case pdf::ActionType::Launch:
            {
                if (!m_settings->getSettings().m_allowLaunchApplications)
                {
                    // Launching of applications is disabled -> continue to next action
                    continue;
                }

                const pdf::PDFActionLaunch* typedAction = dynamic_cast<const pdf::PDFActionLaunch*>(currentAction);
#ifdef Q_OS_WIN
                const pdf::PDFActionLaunch::Win& winSpecification = typedAction->getWinSpecification();
                if (!winSpecification.file.isEmpty())
                {
                    QString message = tr("Would you like to launch application '%1' in working directory '%2' with parameters '%3'?").arg(QString::fromLatin1(winSpecification.file), QString::fromLatin1(winSpecification.directory), QString::fromLatin1(winSpecification.parameters));
                    if (QMessageBox::question(m_mainWindow, tr("Launch application"), message) == QMessageBox::Yes)
                    {
                        auto getStringOrNULL = [](const QByteArray& array) -> LPCSTR
                        {
                            if (!array.isEmpty())
                            {
                                return array.data();
                            }
                            return NULL;
                        };

                        const HINSTANCE result = ::ShellExecuteA(NULL, getStringOrNULL(winSpecification.operation), getStringOrNULL(winSpecification.file), getStringOrNULL(winSpecification.parameters), getStringOrNULL(winSpecification.directory), SW_SHOWNORMAL);
                        if (result <= HINSTANCE(32))
                        {
                            // Error occured
                            QMessageBox::warning(m_mainWindow, tr("Launch application"), tr("Executing application failed. Error code is %1.").arg(reinterpret_cast<intptr_t>(result)));
                        }
                    }

                    // Continue next action
                    continue;
                }

                const pdf::PDFFileSpecification& fileSpecification = typedAction->getFileSpecification();
                QString plaftormFileName = fileSpecification.getPlatformFileName();
                if (!plaftormFileName.isEmpty())
                {
                    QString message = tr("Would you like to launch application '%1'?").arg(plaftormFileName);
                    if (QMessageBox::question(m_mainWindow, tr("Launch application"), message) == QMessageBox::Yes)
                    {
                        const HINSTANCE result = ::ShellExecuteW(NULL, NULL, plaftormFileName.toStdWString().c_str(), NULL, NULL, SW_SHOWNORMAL);
                        if (result <= HINSTANCE(32))
                        {
                            // Error occured
                            QMessageBox::warning(m_mainWindow, tr("Launch application"), tr("Executing application failed. Error code is %1.").arg(reinterpret_cast<intptr_t>(result)));
                        }
                    }

                    // Continue next action
                    continue;
                }
#endif
                break;
            }

            case pdf::ActionType::URI:
            {
                if (!m_settings->getSettings().m_allowLaunchURI)
                {
                    // Launching of URI is disabled -> continue to next action
                    continue;
                }

                const pdf::PDFActionURI* typedAction = dynamic_cast<const pdf::PDFActionURI*>(currentAction);
                QByteArray URI = m_pdfDocument->getCatalog()->getBaseURI() + typedAction->getURI();
                QString urlString = QString::fromUtf8(URI);
                QString message = tr("Would you like to open URL '%1'?").arg(urlString);
                if (QMessageBox::question(m_mainWindow, tr("Open URL"), message) == QMessageBox::Yes)
                {
                    if (!QDesktopServices::openUrl(QUrl(urlString)))
                    {
                        // Error occured
                        QMessageBox::warning(m_mainWindow, tr("Open URL"), tr("Opening url '%1' failed.").arg(urlString));
                    }
                }

                break;
            }

            case pdf::ActionType::Named:
            {
                const pdf::PDFActionNamed* typedAction = dynamic_cast<const pdf::PDFActionNamed*>(currentAction);
                switch (typedAction->getNamedActionType())
                {
                    case pdf::PDFActionNamed::NamedActionType::NextPage:
                        m_pdfWidget->getDrawWidgetProxy()->performOperation(pdf::PDFDrawWidgetProxy::NavigateNextPage);
                        break;

                    case pdf::PDFActionNamed::NamedActionType::PrevPage:
                        m_pdfWidget->getDrawWidgetProxy()->performOperation(pdf::PDFDrawWidgetProxy::NavigatePreviousPage);
                        break;

                    case pdf::PDFActionNamed::NamedActionType::FirstPage:
                        m_pdfWidget->getDrawWidgetProxy()->performOperation(pdf::PDFDrawWidgetProxy::NavigateDocumentStart);
                        break;

                    case pdf::PDFActionNamed::NamedActionType::LastPage:
                        m_pdfWidget->getDrawWidgetProxy()->performOperation(pdf::PDFDrawWidgetProxy::NavigateDocumentEnd);
                        break;

                    default:
                        break;
                }

                break;
            }

            case pdf::ActionType::SetOCGState:
            {
                const pdf::PDFActionSetOCGState* typedAction = dynamic_cast<const pdf::PDFActionSetOCGState*>(currentAction);
                const pdf::PDFActionSetOCGState::StateChangeItems& stateChanges = typedAction->getStateChangeItems();
                const bool isRadioButtonsPreserved = typedAction->isRadioButtonsPreserved();

                if (m_optionalContentActivity)
                {
                    for (const pdf::PDFActionSetOCGState::StateChangeItem& stateChange : stateChanges)
                    {
                        pdf::OCState newState = pdf::OCState::Unknown;
                        switch (stateChange.first)
                        {
                            case pdf::PDFActionSetOCGState::SwitchType::ON:
                                newState = pdf::OCState::ON;
                                break;

                            case pdf::PDFActionSetOCGState::SwitchType::OFF:
                                newState = pdf::OCState::OFF;
                                break;

                            case pdf::PDFActionSetOCGState::SwitchType::Toggle:
                            {
                                pdf::OCState oldState = m_optionalContentActivity->getState(stateChange.second);
                                switch (oldState)
                                {
                                    case pdf::OCState::ON:
                                        newState = pdf::OCState::OFF;
                                        break;

                                    case pdf::OCState::OFF:
                                        newState = pdf::OCState::ON;
                                        break;

                                    case pdf::OCState::Unknown:
                                        break;

                                    default:
                                        Q_ASSERT(false);
                                        break;
                                }

                                break;
                            }

                            default:
                                Q_ASSERT(false);
                        }

                        if (newState != pdf::OCState::Unknown)
                        {
                            m_optionalContentActivity->setState(stateChange.second, newState, isRadioButtonsPreserved);
                        }
                    }
                }

                break;
            }

            case pdf::ActionType::ResetForm:
            {
                m_formManager->performResetAction(dynamic_cast<const pdf::PDFActionResetForm*>(action));
                break;
            }

            default:
                break;
        }
    }
}

void PDFProgramController::initializeToolManager()
{
    // Initialize tools
    pdf::PDFToolManager::Actions actions;
    actions.findPrevAction = m_actionManager->getAction(PDFActionManager::FindPrevious);
    actions.findNextAction = m_actionManager->getAction(PDFActionManager::FindNext);
    actions.selectTextToolAction = m_actionManager->getAction(PDFActionManager::ToolSelectText);
    actions.selectAllAction = m_actionManager->getAction(PDFActionManager::SelectTextAll);
    actions.deselectAction = m_actionManager->getAction(PDFActionManager::DeselectText);
    actions.copyTextAction = m_actionManager->getAction(PDFActionManager::CopyText);
    actions.magnifierAction = m_actionManager->getAction(PDFActionManager::ToolMagnifier);
    actions.screenshotToolAction = m_actionManager->getAction(PDFActionManager::ToolScreenshot);
    actions.extractImageAction = m_actionManager->getAction(PDFActionManager::ToolExtractImage);
    m_toolManager = new pdf::PDFToolManager(m_pdfWidget->getDrawWidgetProxy(), actions, this, m_mainWindow);
    m_pdfWidget->setToolManager(m_toolManager);
    updateMagnifierToolSettings();
    connect(m_toolManager, &pdf::PDFToolManager::documentModified, this, &PDFProgramController::onDocumentModified);

    // Add special tools
    pdf::PDFCreateStickyNoteTool* createStickyNoteTool = new pdf::PDFCreateStickyNoteTool(m_pdfWidget->getDrawWidgetProxy(), m_toolManager, m_actionManager->getActionGroup(PDFActionManager::CreateStickyNoteGroup), this);
    m_toolManager->addTool(createStickyNoteTool);
    pdf::PDFCreateHyperlinkTool* createHyperlinkTool = new pdf::PDFCreateHyperlinkTool(m_pdfWidget->getDrawWidgetProxy(), m_toolManager, m_actionManager->getAction(PDFActionManager::CreateHyperlink), this);
    m_toolManager->addTool(createHyperlinkTool);
    pdf::PDFCreateFreeTextTool* createFreeTextTool = new pdf::PDFCreateFreeTextTool(m_pdfWidget->getDrawWidgetProxy(), m_toolManager, m_actionManager->getAction(PDFActionManager::CreateInlineText), this);
    m_toolManager->addTool(createFreeTextTool);
    pdf::PDFCreateLineTypeTool* createStraightLineTool = new pdf::PDFCreateLineTypeTool(m_pdfWidget->getDrawWidgetProxy(), m_toolManager, pdf::PDFCreateLineTypeTool::Type::Line, m_actionManager->getAction(PDFActionManager::CreateStraightLine), this);
    m_toolManager->addTool(createStraightLineTool);
    pdf::PDFCreateLineTypeTool* createPolylineTool = new pdf::PDFCreateLineTypeTool(m_pdfWidget->getDrawWidgetProxy(), m_toolManager, pdf::PDFCreateLineTypeTool::Type::PolyLine, m_actionManager->getAction(PDFActionManager::CreatePolyline), this);
    m_toolManager->addTool(createPolylineTool);
    pdf::PDFCreateLineTypeTool* createPolygonTool = new pdf::PDFCreateLineTypeTool(m_pdfWidget->getDrawWidgetProxy(), m_toolManager, pdf::PDFCreateLineTypeTool::Type::Polygon, m_actionManager->getAction(PDFActionManager::CreatePolygon), this);
    m_toolManager->addTool(createPolygonTool);
    pdf::PDFCreateEllipseTool* createEllipseTool = new pdf::PDFCreateEllipseTool(m_pdfWidget->getDrawWidgetProxy(), m_toolManager, m_actionManager->getAction(PDFActionManager::CreateEllipse), this);
    m_toolManager->addTool(createEllipseTool);
    pdf::PDFCreateFreehandCurveTool* createFreehandCurveTool = new pdf::PDFCreateFreehandCurveTool(m_pdfWidget->getDrawWidgetProxy(), m_toolManager, m_actionManager->getAction(PDFActionManager::CreateFreehandCurve), this);
    m_toolManager->addTool(createFreehandCurveTool);
    pdf::PDFCreateStampTool* createStampTool = new pdf::PDFCreateStampTool(m_pdfWidget->getDrawWidgetProxy(), m_toolManager, m_actionManager->getActionGroup(PDFActionManager::CreateStampGroup), this);
    m_toolManager->addTool(createStampTool);
    pdf::PDFCreateHighlightTextTool* createHighlightTextTool = new pdf::PDFCreateHighlightTextTool(m_pdfWidget->getDrawWidgetProxy(), m_toolManager, m_actionManager->getActionGroup(PDFActionManager::CreateTextHighlightGroup), this);
    m_toolManager->addTool(createHighlightTextTool);
}

void PDFProgramController::onActionGoToDocumentStartTriggered()
{
    m_pdfWidget->getDrawWidgetProxy()->performOperation(pdf::PDFDrawWidgetProxy::NavigateDocumentStart);
}

void PDFProgramController::onActionGoToDocumentEndTriggered()
{
    m_pdfWidget->getDrawWidgetProxy()->performOperation(pdf::PDFDrawWidgetProxy::NavigateDocumentEnd);
}

void PDFProgramController::onActionGoToNextPageTriggered()
{
    m_pdfWidget->getDrawWidgetProxy()->performOperation(pdf::PDFDrawWidgetProxy::NavigateNextPage);
}

void PDFProgramController::onActionGoToPreviousPageTriggered()
{
    m_pdfWidget->getDrawWidgetProxy()->performOperation(pdf::PDFDrawWidgetProxy::NavigatePreviousPage);
}

void PDFProgramController::onActionGoToNextLineTriggered()
{
    m_pdfWidget->getDrawWidgetProxy()->performOperation(pdf::PDFDrawWidgetProxy::NavigateNextStep);
}

void PDFProgramController::onActionGoToPreviousLineTriggered()
{
    m_pdfWidget->getDrawWidgetProxy()->performOperation(pdf::PDFDrawWidgetProxy::NavigatePreviousStep);
}

void PDFProgramController::onActionZoomIn()
{
    m_pdfWidget->getDrawWidgetProxy()->performOperation(pdf::PDFDrawWidgetProxy::ZoomIn);
}

void PDFProgramController::onActionZoomOut()
{
    m_pdfWidget->getDrawWidgetProxy()->performOperation(pdf::PDFDrawWidgetProxy::ZoomOut);
}

void PDFProgramController::onActionRenderingOptionTriggered(bool checked)
{
    QAction* action = qobject_cast<QAction*>(sender());
    Q_ASSERT(action);

    pdf::PDFRenderer::Features features = m_settings->getFeatures();
    features.setFlag(static_cast<pdf::PDFRenderer::Feature>(action->data().toInt()), checked);
    m_settings->setFeatures(features);
}

void PDFProgramController::performSaveAs()
{

    QFileInfo fileInfo(m_fileInfo.originalFileName);
    QString saveFileName = QFileDialog::getSaveFileName(m_mainWindow, tr("Save As"), fileInfo.dir().absoluteFilePath(m_fileInfo.originalFileName), tr("Portable Document (*.pdf);;All files (*.*)"));
    if (!saveFileName.isEmpty())
    {
        saveDocument(saveFileName);
    }
}

void PDFProgramController::performSave()
{
    saveDocument(m_fileInfo.originalFileName);
}

void PDFProgramController::saveDocument(const QString& fileName)
{
    pdf::PDFDocumentWriter writer(nullptr);
    pdf::PDFOperationResult result = writer.write(fileName, m_pdfDocument.data(), true);
    if (result)
    {
        updateFileInfo(fileName);
        updateTitle();

        if (m_recentFileManager)
        {
            m_recentFileManager->addRecentFile(fileName);
        }
    }
    else
    {
        QMessageBox::critical(m_mainWindow, tr("Error"), result.getErrorMessage());
    }
}

bool PDFProgramController::getIsBusy() const
{
    return m_isBusy;
}

void PDFProgramController::setIsBusy(bool isBusy)
{
    m_isBusy = isBusy;
}

bool PDFProgramController::canClose() const
{
    return !(m_futureWatcher && m_futureWatcher->isRunning()) || !m_isBusy;
}

QString PDFProgramController::getOriginalFileName() const
{
    return m_fileInfo.originalFileName;
}

pdf::PDFTextSelection PDFProgramController::getSelectedText() const
{
    return m_mainWindowInterface->getSelectedText();
}

QMainWindow* PDFProgramController::getMainWindow() const
{
    return m_mainWindow;
}

pdf::IPluginDataExchange::VoiceSettings PDFProgramController::getVoiceSettings() const
{
    VoiceSettings voiceSettings;

    const PDFViewerSettings::Settings& settings = m_settings->getSettings();
    voiceSettings.directory = m_settings->getDirectory();
    voiceSettings.voiceName = settings.m_speechVoice;
    voiceSettings.pitch = settings.m_speechPitch;
    voiceSettings.rate = settings.m_speechRate;
    voiceSettings.volume = settings.m_speechVolume;

    return voiceSettings;
}

void PDFProgramController::onActionRotateRightTriggered()
{
    m_pdfWidget->getDrawWidgetProxy()->performOperation(pdf::PDFDrawWidgetProxy::RotateRight);
}

void PDFProgramController::onActionRotateLeftTriggered()
{
    m_pdfWidget->getDrawWidgetProxy()->performOperation(pdf::PDFDrawWidgetProxy::RotateLeft);
}


void PDFProgramController::onActionPropertiesTriggered()
{
    Q_ASSERT(m_pdfDocument);

    PDFDocumentPropertiesDialog documentPropertiesDialog(m_pdfDocument.data(), &m_fileInfo, m_mainWindow);
    documentPropertiesDialog.exec();
}

void PDFProgramController::onActionAboutTriggered()
{
    PDFAboutDialog dialog(m_mainWindow);
    dialog.exec();
}

void PDFProgramController::onActionSendByEMailTriggered()
{
    Q_ASSERT(m_pdfDocument);

    QString subject = m_pdfDocument->getInfo()->title;
    if (subject.isEmpty())
    {
        subject = m_fileInfo.fileName;
    }

    if (!PDFSendMail::sendMail(m_mainWindow, subject, m_fileInfo.originalFileName))
    {
        QMessageBox::critical(m_mainWindow, tr("Error"), tr("Error while starting email client occured!"));
    }
}

void PDFProgramController::onActionRenderToImagesTriggered()
{
    PDFRenderToImagesDialog dialog(m_pdfDocument.data(), m_pdfWidget->getDrawWidgetProxy(), m_progress, m_mainWindow);
    dialog.exec();
}

void PDFProgramController::onActionOptimizeTriggered()
{
    PDFOptimizeDocumentDialog dialog(m_pdfDocument.data(), m_mainWindow);

    if (dialog.exec() == QDialog::Accepted)
    {
        pdf::PDFDocumentPointer pointer(new pdf::PDFDocument(dialog.takeOptimizedDocument()));
        pdf::PDFModifiedDocument document(qMove(pointer), m_optionalContentActivity, pdf::PDFModifiedDocument::Reset);
        onDocumentModified(qMove(document));
    }
}

void PDFProgramController::onActionEncryptionTriggered()
{
    auto queryPassword = [this](bool* ok)
    {
        QString result;
        *ok = false;
        onQueryPasswordRequest(&result, ok);
        return result;
    };

    // Check that we have owner access to the document
    const pdf::PDFSecurityHandler* securityHandler =  m_pdfDocument->getStorage().getSecurityHandler();
    pdf::PDFSecurityHandler::AuthorizationResult authorizationResult = securityHandler->getAuthorizationResult();
    if (authorizationResult != pdf::PDFSecurityHandler::AuthorizationResult::OwnerAuthorized &&
        authorizationResult != pdf::PDFSecurityHandler::AuthorizationResult::NoAuthorizationRequired)
    {
        // Jakub Melka: we must authorize as owner, otherwise we can't continue,
        // because we don't have sufficient permissions.
        pdf::PDFSecurityHandlerPointer clonedSecurityHandler(securityHandler->clone());
        authorizationResult = clonedSecurityHandler->authenticate(queryPassword, true);

        if (authorizationResult != pdf::PDFSecurityHandler::AuthorizationResult::OwnerAuthorized)
        {
            QMessageBox::critical(m_mainWindow, QApplication::applicationDisplayName(), tr("Permission to change document security is denied."));
            return;
        }

        pdf::PDFObjectStorage storage = m_pdfDocument->getStorage();
        storage.setSecurityHandler(qMove(clonedSecurityHandler));

        pdf::PDFDocumentPointer pointer(new pdf::PDFDocument(qMove(storage), m_pdfDocument->getInfo()->version));
        pdf::PDFModifiedDocument document(qMove(pointer), m_optionalContentActivity, pdf::PDFModifiedDocument::Authorization);
        onDocumentModified(qMove(document));
    }

    PDFEncryptionSettingsDialog dialog(m_pdfDocument->getIdPart(0), m_mainWindow);
    if (dialog.exec() == QDialog::Accepted)
    {
        pdf::PDFSecurityHandlerPointer updatedSecurityHandler = dialog.getUpdatedSecurityHandler();

        // Jakub Melka: If we changed encryption (password), recheck, that user doesn't
        // forgot (or accidentally entered wrong) password. So, we require owner authentization
        // to continue.
        if (updatedSecurityHandler->getMode() != pdf::EncryptionMode::None)
        {
            if (updatedSecurityHandler->authenticate(queryPassword, true) != pdf::PDFSecurityHandler::AuthorizationResult::OwnerAuthorized)
            {
                QMessageBox::critical(m_mainWindow, QApplication::applicationDisplayName(), tr("Reauthorization is required to change document encryption."));
                return;
            }
        }

        pdf::PDFDocumentBuilder builder(m_pdfDocument.data());
        builder.setSecurityHandler(qMove(updatedSecurityHandler));

        pdf::PDFDocumentPointer pointer(new pdf::PDFDocument(builder.build()));
        pdf::PDFModifiedDocument document(qMove(pointer), m_optionalContentActivity, pdf::PDFModifiedDocument::Reset);
        onDocumentModified(qMove(document));
    }
}

void PDFProgramController::onActionFitPageTriggered()
{
    m_pdfWidget->getDrawWidgetProxy()->performOperation(pdf::PDFDrawWidgetProxy::ZoomFit);
}

void PDFProgramController::onActionFitWidthTriggered()
{
    m_pdfWidget->getDrawWidgetProxy()->performOperation(pdf::PDFDrawWidgetProxy::ZoomFitWidth);
}

void PDFProgramController::onActionFitHeightTriggered()
{
    m_pdfWidget->getDrawWidgetProxy()->performOperation(pdf::PDFDrawWidgetProxy::ZoomFitHeight);
}

void PDFProgramController::onActionRenderingErrorsTriggered()
{
    pdf::PDFRenderingErrorsWidget renderingErrorsDialog(m_mainWindow, m_pdfWidget);
    renderingErrorsDialog.exec();
}

void PDFProgramController::updateMagnifierToolSettings()
{
    if (m_toolManager)
    {
        pdf::PDFMagnifierTool* magnifierTool = m_toolManager->getMagnifierTool();
        magnifierTool->setMagnifierSize(pdf::PDFWidgetUtils::scaleDPI_x(m_mainWindow, m_settings->getSettings().m_magnifierSize));
        magnifierTool->setMagnifierZoom(m_settings->getSettings().m_magnifierZoom);
    }
}

void PDFProgramController::updateUndoRedoSettings()
{
    if (m_undoRedoManager)
    {
        const PDFViewerSettings::Settings& settings = m_settings->getSettings();
        m_undoRedoManager->setMaximumSteps(settings.m_maximumUndoSteps, settings.m_maximumRedoSteps);
    }
}

void PDFProgramController::updateUndoRedoActions()
{
    if (m_undoRedoManager)
    {
        const bool isBusy = (m_futureWatcher && m_futureWatcher->isRunning()) || m_isBusy;
        const bool canUndo = !isBusy && m_undoRedoManager->canUndo();
        const bool canRedo = !isBusy && m_undoRedoManager->canRedo();

        m_actionManager->setEnabled(PDFActionManager::Undo, canUndo);
        m_actionManager->setEnabled(PDFActionManager::Redo, canRedo);
    }
    else
    {
        m_actionManager->setEnabled(PDFActionManager::Undo, false);
        m_actionManager->setEnabled(PDFActionManager::Redo, false);
    }
}

void PDFProgramController::onQueryPasswordRequest(QString* password, bool* ok)
{
    *password = QInputDialog::getText(m_mainWindow, tr("Encrypted document"), tr("Enter password to access document content"), QLineEdit::Password, QString(), ok);
}

void PDFProgramController::onActionPageLayoutSinglePageTriggered()
{
    setPageLayout(pdf::PageLayout::SinglePage);
}

void PDFProgramController::onActionPageLayoutContinuousTriggered()
{
    setPageLayout(pdf::PageLayout::OneColumn);
}

void PDFProgramController::onActionPageLayoutTwoPagesTriggered()
{
    setPageLayout(m_actionManager->getAction(PDFActionManager::PageLayoutFirstPageOnRightSide)->isChecked() ? pdf::PageLayout::TwoPagesRight : pdf::PageLayout::TwoPagesLeft);
}

void PDFProgramController::onActionPageLayoutTwoColumnsTriggered()
{
    setPageLayout(m_actionManager->getAction(PDFActionManager::PageLayoutFirstPageOnRightSide)->isChecked() ? pdf::PageLayout::TwoColumnRight : pdf::PageLayout::TwoColumnLeft);
}

void PDFProgramController::onActionFirstPageOnRightSideTriggered()
{
    switch (m_pdfWidget->getDrawWidgetProxy()->getPageLayout())
    {
        case pdf::PageLayout::SinglePage:
        case pdf::PageLayout::OneColumn:
            break;

        case pdf::PageLayout::TwoColumnLeft:
        case pdf::PageLayout::TwoColumnRight:
            onActionPageLayoutTwoColumnsTriggered();
            break;

        case pdf::PageLayout::TwoPagesLeft:
        case pdf::PageLayout::TwoPagesRight:
            onActionPageLayoutTwoPagesTriggered();
            break;

        default:
            Q_ASSERT(false);
    }
}

void PDFProgramController::onActionFindTriggered()
{
    if (m_toolManager)
    {
        m_toolManager->setActiveTool(m_toolManager->getFindTextTool());
    }
}

void PDFProgramController::readSettings(Settings settingsFlags)
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());

    if (settingsFlags.testFlag(WindowSettings))
    {
        QByteArray geometry = settings.value("geometry", QByteArray()).toByteArray();
        if (geometry.isEmpty())
        {
            QRect availableGeometry = QApplication::desktop()->availableGeometry(m_mainWindow);
            QRect windowRect(0, 0, availableGeometry.width() / 2, availableGeometry.height() / 2);
            windowRect = windowRect.translated(availableGeometry.center() - windowRect.center());
            m_mainWindow->setGeometry(windowRect);
        }
        else
        {
            m_mainWindow->restoreGeometry(geometry);
        }

        QByteArray state = settings.value("windowState", QByteArray()).toByteArray();
        if (!state.isEmpty())
        {
            m_mainWindow->restoreState(state);
        }
    }

    if (settingsFlags.testFlag(GeneralSettings))
    {
        m_settings->readSettings(settings, m_CMSManager->getDefaultSettings());
        m_CMSManager->setSettings(m_settings->getColorManagementSystemSettings());

        if (m_textToSpeech)
        {
            m_textToSpeech->setSettings(m_settings);
        }

        if (m_formManager)
        {
            m_formManager->setAppearanceFlags(m_settings->getSettings().m_formAppearanceFlags);
        }
    }

    if (settingsFlags.testFlag(PluginsSettings))
    {
        // Load allowed plugins
        settings.beginGroup("Plugins");
        m_enabledPlugins = settings.value("EnabledPlugins").toStringList();
        settings.endGroup();
    }

    // Load action shortcuts
    if (settingsFlags.testFlag(ActionSettings))
    {
        settings.beginGroup("Actions");
        for (QAction* action : m_actionManager->getActions())
        {
            QString name = action->objectName();
            if (!name.isEmpty() && settings.contains(name))
            {
                QKeySequence sequence = QKeySequence::fromString(settings.value(name, action->shortcut().toString(QKeySequence::PortableText)).toString(), QKeySequence::PortableText);
                action->setShortcut(sequence);
            }
        }
        settings.endGroup();
    }

    if (settingsFlags.testFlag(RecentFileSettings) && m_recentFileManager)
    {
        // Load recent files
        settings.beginGroup("RecentFiles");
        m_recentFileManager->setRecentFilesLimit(settings.value("MaximumRecentFilesCount", PDFRecentFileManager::getDefaultRecentFiles()).toInt());
        m_recentFileManager->setRecentFiles(settings.value("RecentFileList", QStringList()).toStringList());
        settings.endGroup();
    }

    if (settingsFlags.testFlag(CertificateSettings))
    {
        // Load trusted certificates
        m_certificateStore.loadDefaultUserCertificates();
    }
}

void PDFProgramController::setPageLayout(pdf::PageLayout pageLayout)
{
    m_pdfWidget->getDrawWidgetProxy()->setPageLayout(pageLayout);
}

void PDFProgramController::updateActionsAvailability()
{
    const bool isBusy = (m_futureWatcher && m_futureWatcher->isRunning()) || m_isBusy;
    const bool hasDocument = m_pdfDocument;
    const bool hasValidDocument = !isBusy && hasDocument;
    bool canPrint = false;
    if (m_pdfDocument)
    {
        const pdf::PDFObjectStorage& storage = m_pdfDocument->getStorage();
        const pdf::PDFSecurityHandler* securityHandler = storage.getSecurityHandler();
        canPrint = securityHandler->isAllowed(pdf::PDFSecurityHandler::Permission::PrintLowResolution) ||
                   securityHandler->isAllowed(pdf::PDFSecurityHandler::Permission::PrintHighResolution);
    }

    m_actionManager->setEnabled(PDFActionManager::Open, !isBusy);
    m_actionManager->setEnabled(PDFActionManager::Close, hasValidDocument);
    m_actionManager->setEnabled(PDFActionManager::Quit, !isBusy);
    m_actionManager->setEnabled(PDFActionManager::Options, !isBusy);
    m_actionManager->setEnabled(PDFActionManager::About, !isBusy);
    m_actionManager->setEnabled(PDFActionManager::FitPage, hasValidDocument);
    m_actionManager->setEnabled(PDFActionManager::FitWidth, hasValidDocument);
    m_actionManager->setEnabled(PDFActionManager::FitHeight, hasValidDocument);
    m_actionManager->setEnabled(PDFActionManager::ShowRenderingErrors, hasValidDocument);
    m_actionManager->setEnabled(PDFActionManager::Find, hasValidDocument);
    m_actionManager->setEnabled(PDFActionManager::Print, hasValidDocument && canPrint);
    m_actionManager->setEnabled(PDFActionManager::RenderToImages, hasValidDocument && canPrint);
    m_actionManager->setEnabled(PDFActionManager::Optimize, hasValidDocument);
    m_actionManager->setEnabled(PDFActionManager::Encryption, hasValidDocument);
    m_actionManager->setEnabled(PDFActionManager::Save, hasValidDocument);
    m_actionManager->setEnabled(PDFActionManager::SaveAs, hasValidDocument);
    m_actionManager->setEnabled(PDFActionManager::Properties, hasDocument);
    m_actionManager->setEnabled(PDFActionManager::SendByMail, hasDocument);
    m_mainWindow->setEnabled(!isBusy);
    updateUndoRedoActions();
}

void PDFProgramController::onViewerSettingsChanged()
{
    m_pdfWidget->updateRenderer(m_settings->getRendererEngine(), m_settings->isMultisampleAntialiasingEnabled() ? m_settings->getRendererSamples() : -1);
    m_pdfWidget->updateCacheLimits(m_settings->getCompiledPageCacheLimit() * 1024, m_settings->getThumbnailsCacheLimit(), m_settings->getFontCacheLimit(), m_settings->getInstancedFontCacheLimit());
    m_pdfWidget->getDrawWidgetProxy()->setFeatures(m_settings->getFeatures());
    m_pdfWidget->getDrawWidgetProxy()->setPreferredMeshResolutionRatio(m_settings->getPreferredMeshResolutionRatio());
    m_pdfWidget->getDrawWidgetProxy()->setMinimalMeshResolutionRatio(m_settings->getMinimalMeshResolutionRatio());
    m_pdfWidget->getDrawWidgetProxy()->setColorTolerance(m_settings->getColorTolerance());
    m_annotationManager->setFeatures(m_settings->getFeatures());
    m_annotationManager->setMeshQualitySettings(m_pdfWidget->getDrawWidgetProxy()->getMeshQualitySettings());
    pdf::PDFExecutionPolicy::setStrategy(m_settings->getMultithreadingStrategy());

    updateRenderingOptionActions();
}

void PDFProgramController::onColorManagementSystemChanged()
{
    m_settings->setColorManagementSystemSettings(m_CMSManager->getSettings());
}

void PDFProgramController::updateFileInfo(const QString& fileName)
{
    QFileInfo fileInfo(fileName);
    m_fileInfo.originalFileName = fileName;
    m_fileInfo.fileName = fileInfo.fileName();
    m_fileInfo.path = fileInfo.path();
    m_fileInfo.fileSize = fileInfo.size();
    m_fileInfo.writable = fileInfo.isWritable();
    m_fileInfo.creationTime = fileInfo.created();
    m_fileInfo.lastModifiedTime = fileInfo.lastModified();
    m_fileInfo.lastReadTime = fileInfo.lastRead();
}

void PDFProgramController::openDocument(const QString& fileName)
{
    // First close old document
    closeDocument();

    updateFileInfo(fileName);

    QApplication::setOverrideCursor(Qt::WaitCursor);
    auto readDocument = [this, fileName]() -> AsyncReadingResult
    {
        AsyncReadingResult result;

        auto queryPassword = [this](bool* ok)
        {
            QString result;
            *ok = false;
            emit queryPasswordRequest(&result, ok);
            return result;
        };

        // Try to open a new document
        pdf::PDFDocumentReader reader(m_progress, qMove(queryPassword), true, false);
        pdf::PDFDocument document = reader.readFromFile(fileName);

        result.errorMessage = reader.getErrorMessage();
        result.result = reader.getReadingResult();
        if (result.result == pdf::PDFDocumentReader::Result::OK)
        {
            // Verify signatures
            pdf::PDFSignatureHandler::Parameters parameters;
            parameters.store = &m_certificateStore;
            parameters.dss = &document.getCatalog()->getDocumentSecurityStore();
            parameters.enableVerification = m_settings->getSettings().m_signatureVerificationEnabled;
            parameters.ignoreExpirationDate = m_settings->getSettings().m_signatureIgnoreCertificateValidityTime;
            parameters.useSystemCertificateStore = m_settings->getSettings().m_signatureUseSystemStore;

            pdf::PDFForm form = pdf::PDFForm::parse(&document, document.getCatalog()->getFormObject());
            result.signatures = pdf::PDFSignatureHandler::verifySignatures(form, reader.getSource(), parameters);
            result.document.reset(new pdf::PDFDocument(qMove(document)));
        }

        return result;
    };
    m_future = QtConcurrent::run(readDocument);
    m_futureWatcher = new QFutureWatcher<AsyncReadingResult>();
    connect(m_futureWatcher, &QFutureWatcher<AsyncReadingResult>::finished, this, &PDFProgramController::onDocumentReadingFinished);
    m_futureWatcher->setFuture(m_future);
    updateActionsAvailability();
}

void PDFProgramController::onDocumentReadingFinished()
{
    QApplication::restoreOverrideCursor();

    AsyncReadingResult result = m_future.result();
    m_future = QFuture<AsyncReadingResult>();
    m_futureWatcher->deleteLater();
    m_futureWatcher = nullptr;

    switch (result.result)
    {
        case pdf::PDFDocumentReader::Result::OK:
        {
            // Mark current directory as this
            QFileInfo fileInfo(m_fileInfo.originalFileName);
            m_settings->setDirectory(fileInfo.dir().absolutePath());

            // We add file to recent files only, if we have successfully read the document
            m_recentFileManager->addRecentFile(m_fileInfo.originalFileName);

            m_pdfDocument = qMove(result.document);
            m_signatures = qMove(result.signatures);
            pdf::PDFModifiedDocument document(m_pdfDocument.data(), m_optionalContentActivity);
            setDocument(document);

            pdf::PDFDocumentRequirements requirements = pdf::PDFDocumentRequirements::parse(&m_pdfDocument->getStorage(), m_pdfDocument->getCatalog()->getRequirements());
            constexpr pdf::PDFDocumentRequirements::Requirements requirementFlags = pdf::PDFDocumentRequirements::Requirements(pdf::PDFDocumentRequirements::OCInteract |
                                                                                                                               pdf::PDFDocumentRequirements::OCAutoStates |
                                                                                                                               pdf::PDFDocumentRequirements::Navigation |
                                                                                                                               pdf::PDFDocumentRequirements::Attachment |
                                                                                                                               pdf::PDFDocumentRequirements::DigSigValidation |
                                                                                                                               pdf::PDFDocumentRequirements::Encryption);
            pdf::PDFDocumentRequirements::ValidationResult requirementResult = requirements.validate(requirementFlags);
            if (requirementResult.isError())
            {
                QMessageBox::critical(m_mainWindow, QApplication::applicationDisplayName(), requirementResult.message);
            }
            else if (requirementResult.isWarning())
            {
                QMessageBox::warning(m_mainWindow, QApplication::applicationDisplayName(), requirementResult.message);
            }

            m_mainWindowInterface->setStatusBarMessage(tr("Document '%1' was successfully loaded!").arg(m_fileInfo.fileName), 4000);
            break;
        }

        case pdf::PDFDocumentReader::Result::Failed:
        {
            QMessageBox::critical(m_mainWindow, QApplication::applicationDisplayName(), tr("Document read error: %1").arg(result.errorMessage));
            break;
        }

        case pdf::PDFDocumentReader::Result::Cancelled:
            break; // Do nothing, user cancelled the document reading
    }
    updateActionsAvailability();
}

void PDFProgramController::onDocumentModified(pdf::PDFModifiedDocument document)
{
    // We will create undo/redo step from old document, with flags from the new,
    // because new document is modification of old document with flags.

    Q_ASSERT(m_pdfDocument);

    if (m_undoRedoManager)
    {
        m_undoRedoManager->createUndo(document, m_pdfDocument);
    }

    m_pdfDocument = document;
    document.setOptionalContentActivity(m_optionalContentActivity);
    setDocument(document);
}

void PDFProgramController::onDocumentUndoRedo(pdf::PDFModifiedDocument document)
{
    m_pdfDocument = document;
    document.setOptionalContentActivity(m_optionalContentActivity);
    setDocument(document);
}

void PDFProgramController::setDocument(pdf::PDFModifiedDocument document)
{
    if (document.hasReset())
    {
        if (m_optionalContentActivity)
        {
            // We use deleteLater, because we want to avoid consistency problem with model
            // (we set document to the model before activity).
            m_optionalContentActivity->deleteLater();
            m_optionalContentActivity = nullptr;
        }

        if (document)
        {
            m_optionalContentActivity = new pdf::PDFOptionalContentActivity(document, pdf::OCUsage::View, this);
        }

        if (m_undoRedoManager)
        {
            m_undoRedoManager->clear();
        }
    }
    else if (m_optionalContentActivity)
    {
        Q_ASSERT(document);
        m_optionalContentActivity->setDocument(document);
    }

    document.setOptionalContentActivity(m_optionalContentActivity);

    if (m_annotationManager)
    {
        m_annotationManager->setDocument(document);
    }

    if (m_formManager)
    {
        m_formManager->setDocument(document);
    }

    if (m_toolManager)
    {
        m_toolManager->setDocument(document);
    }

    if (m_textToSpeech)
    {
        m_textToSpeech->setDocument(document);
    }

    m_pdfWidget->setDocument(document);
    m_mainWindowInterface->setDocument(document);
    m_CMSManager->setDocument(document);

    updateTitle();
    m_mainWindowInterface->updateUI(true);

    for (const auto& plugin : m_loadedPlugins)
    {
        plugin.second->setDocument(document);
    }

    if (m_pdfDocument && document.hasReset())
    {
        const pdf::PDFCatalog* catalog = m_pdfDocument->getCatalog();
        setPageLayout(catalog->getPageLayout());
        updatePageLayoutActions();

        if (const pdf::PDFAction* action = catalog->getOpenAction())
        {
            onActionTriggered(action);
        }
    }

    updateActionsAvailability();
}

void PDFProgramController::closeDocument()
{
    m_signatures.clear();
    setDocument(pdf::PDFModifiedDocument());
    m_pdfDocument.reset();
    updateActionsAvailability();
    updateTitle();
}

void PDFProgramController::updateRenderingOptionActions()
{
    const pdf::PDFRenderer::Features features = m_settings->getFeatures();
    for (QAction* action : m_actionManager->getRenderingOptionActions())
    {
        action->setChecked(features.testFlag(static_cast<pdf::PDFRenderer::Feature>(action->data().toInt())));
    }
}

void PDFProgramController::updateTitle()
{
    if (m_pdfDocument)
    {
        QString title = m_pdfDocument->getInfo()->title;
        if (title.isEmpty())
        {
            title = m_fileInfo.fileName;
        }
        m_mainWindow->setWindowTitle(tr("%1 - %2").arg(title, QApplication::applicationDisplayName()));
    }
    else
    {
        m_mainWindow->setWindowTitle(QApplication::applicationDisplayName());
    }
}

void PDFProgramController::updatePageLayoutActions()
{
    for (PDFActionManager::Action action : { PDFActionManager::PageLayoutSinglePage, PDFActionManager::PageLayoutContinuous,
                                             PDFActionManager::PageLayoutTwoPages, PDFActionManager::PageLayoutTwoColumns })
    {
        m_actionManager->setChecked(action, false);
    }

    const pdf::PageLayout pageLayout = m_pdfWidget->getDrawWidgetProxy()->getPageLayout();
    switch (pageLayout)
    {
        case pdf::PageLayout::SinglePage:
            m_actionManager->setChecked(PDFActionManager::PageLayoutSinglePage, true);
            break;

        case pdf::PageLayout::OneColumn:
            m_actionManager->setChecked(PDFActionManager::PageLayoutContinuous, true);
            break;

        case pdf::PageLayout::TwoColumnLeft:
        case pdf::PageLayout::TwoColumnRight:
            m_actionManager->setChecked(PDFActionManager::PageLayoutTwoColumns, true);
            m_actionManager->setChecked(PDFActionManager::PageLayoutFirstPageOnRightSide, pageLayout == pdf::PageLayout::TwoPagesRight);
            break;

        case pdf::PageLayout::TwoPagesLeft:
        case pdf::PageLayout::TwoPagesRight:
            m_actionManager->setChecked(PDFActionManager::PageLayoutTwoPages, true);
            m_actionManager->setChecked(PDFActionManager::PageLayoutFirstPageOnRightSide, pageLayout == pdf::PageLayout::TwoPagesRight);
            break;

        default:
            Q_ASSERT(false);
    }
}

void PDFProgramController::loadPlugins()
{
#if defined(Q_OS_WIN)
    QDir directory(QApplication::applicationDirPath() + "/pdfplugins");
    QStringList availablePlugins = directory.entryList(QStringList("*.dll"));
#elif defined(Q_OS_UNIX)
    QDir directory(QApplication::applicationDirPath() + "/../pdfplugins");
    QStringList availablePlugins = directory.entryList(QStringList("*.so"));
#else
    static_assert(false, "Implement this for another OS!");
#endif
    for (const QString& availablePlugin : availablePlugins)
    {
        QString pluginFileName = directory.absoluteFilePath(availablePlugin);
        QPluginLoader loader(pluginFileName);
        if (loader.load())
        {
            QJsonObject metaData = loader.metaData();
            m_plugins.emplace_back(pdf::PDFPluginInfo::loadFromJson(&metaData));
            m_plugins.back().pluginFile = availablePlugin;
            m_plugins.back().pluginFileWithPath = pluginFileName;

            if (!m_enabledPlugins.contains(m_plugins.back().name))
            {
                loader.unload();
                continue;
            }

            pdf::PDFPlugin* plugin = qobject_cast<pdf::PDFPlugin*>(loader.instance());
            if (plugin)
            {
                m_loadedPlugins.push_back(std::make_pair(m_plugins.back(), plugin));
            }
        }
    }

    auto comparator = [](const std::pair<pdf::PDFPluginInfo, pdf::PDFPlugin*>& l, const std::pair<pdf::PDFPluginInfo, pdf::PDFPlugin*>& r)
    {
        return l.first.name < r.first.name;
    };
    std::sort(m_loadedPlugins.begin(), m_loadedPlugins.end(), comparator);

    for (const auto& plugin : m_loadedPlugins)
    {
        plugin.second->setDataExchangeInterface(this);
        plugin.second->setWidget(m_pdfWidget);
        plugin.second->setCMSManager(m_CMSManager);
        std::vector<QAction*> actions = plugin.second->getActions();

        if (!actions.empty())
        {
            QToolBar* toolBar = m_mainWindow->addToolBar(plugin.first.name);
            toolBar->setObjectName(QString("Plugin_Toolbar_%1").arg(plugin.first.name));
            m_mainWindowInterface->adjustToolbar(toolBar);
            QMenu* menu = m_mainWindowInterface->addToolMenu(plugin.first.name);
            for (QAction* action : actions)
            {
                if (!action)
                {
                    menu->addSeparator();
                    toolBar->addSeparator();
                    continue;
                }

                m_actionManager->addAdditionalAction(action);

                menu->addAction(action);
                toolBar->addAction(action);
            }
        }
    }
}

void PDFProgramController::writeSettings()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.setValue("geometry", m_mainWindow->saveGeometry());
    settings.setValue("windowState", m_mainWindow->saveState());

    m_settings->writeSettings(settings);

    // Save action shortcuts
    settings.beginGroup("Actions");
    for (QAction* action : m_actionManager->getActions())
    {
        QString name = action->objectName();
        if (!name.isEmpty())
        {
            QString accelerator = action->shortcut().toString(QKeySequence::PortableText);
            settings.setValue(name, accelerator);
        }
    }
    settings.endGroup();

    // Save recent files
    settings.beginGroup("RecentFiles");
    settings.setValue("MaximumRecentFilesCount", m_recentFileManager->getRecentFilesLimit());
    settings.setValue("RecentFileList", m_recentFileManager->getRecentFiles());
    settings.endGroup();

    // Save allowed plugins
    settings.beginGroup("Plugins");
    settings.setValue("EnabledPlugins", m_enabledPlugins);
    settings.endGroup();

    // Save trusted certificates
    m_certificateStore.saveDefaultUserCertificates();
}

void PDFProgramController::onActionOptionsTriggered()
{
    PDFViewerSettingsDialog::OtherSettings otherSettings;
    otherSettings.maximumRecentFileCount = m_recentFileManager->getRecentFilesLimit();

    PDFViewerSettingsDialog dialog(m_settings->getSettings(), m_settings->getColorManagementSystemSettings(),
                                   otherSettings, m_certificateStore, m_actionManager->getActions(), m_CMSManager,
                                   m_enabledPlugins, m_plugins, m_mainWindow);
    if (dialog.exec() == QDialog::Accepted)
    {
        const bool pluginsChanged = m_enabledPlugins != dialog.getEnabledPlugins();

        m_settings->setSettings(dialog.getSettings());
        m_settings->setColorManagementSystemSettings(dialog.getCMSSettings());
        m_CMSManager->setSettings(m_settings->getColorManagementSystemSettings());
        if (m_recentFileManager)
        {
            m_recentFileManager->setRecentFilesLimit(dialog.getOtherSettings().maximumRecentFileCount);
        }
        if (m_textToSpeech)
        {
            m_textToSpeech->setSettings(m_settings);
        }
        if (m_formManager)
        {
            m_formManager->setAppearanceFlags(m_settings->getSettings().m_formAppearanceFlags);
        }
        m_certificateStore = dialog.getCertificateStore();
        m_enabledPlugins = dialog.getEnabledPlugins();
        updateMagnifierToolSettings();
        updateUndoRedoSettings();

        if (pluginsChanged)
        {
            QMessageBox::information(m_mainWindow, tr("Plugins"), tr("Plugin on/off state has been changed. Please restart application to apply settings."));
        }
    }
}

void PDFProgramController::onDrawSpaceChanged()
{
    m_mainWindowInterface->updateUI(false);
}

void PDFProgramController::onPageLayoutChanged()
{
    m_mainWindowInterface->updateUI(false);
    updatePageLayoutActions();
}

void PDFProgramController::onActionOpenTriggered()
{
    QString fileName = QFileDialog::getOpenFileName(m_mainWindow, tr("Select PDF document"), m_settings->getDirectory(), tr("PDF document (*.pdf)"));
    if (!fileName.isEmpty())
    {
        openDocument(fileName);
    }
}

void PDFProgramController::onActionCloseTriggered()
{
    closeDocument();
}

void PDFProgramController::onActionDeveloperCreateInstaller()
{
    QString directory = QFileDialog::getExistingDirectory(m_mainWindow, tr("Select Directory for Installer"));

    if (directory.isEmpty())
    {
        return;
    }

    // Config.xml
    {
        QDir().mkpath(directory + "/config");
        QFile configFile(directory + "/config/config.xml");
        if (configFile.open(QFile::WriteOnly | QFile::Truncate))
        {
            QXmlStreamWriter configWriter(&configFile);
            configWriter.setAutoFormatting(true);
            configWriter.setAutoFormattingIndent(2);

            configWriter.writeStartDocument();
            configWriter.writeStartElement("Installer");

            configWriter.writeTextElement("Name", tr("PDF4QT"));
            configWriter.writeTextElement("Title", tr("PDF4QT Suite (library, viewer, editor, command line tool)"));
            configWriter.writeTextElement("Version", pdf::PDF_LIBRARY_VERSION);
            configWriter.writeTextElement("Publisher", tr("Jakub Melka"));
            configWriter.writeTextElement("StartMenuDir", "PDF4QT");
            configWriter.writeTextElement("TargetDir", "@ApplicationsDir@/PDF4QT");
            configWriter.writeTextElement("CreateLocalRepository", "true");
            configWriter.writeTextElement("InstallActionColumnVisible", "true");

            configWriter.writeEndElement();
            configWriter.writeEndDocument();
            configFile.close();
        }
    }

    // Installer project file
    {
        QString qtInstallDirectory = QT_INSTALL_DIRECTORY;
        int indexofQtRoot = qtInstallDirectory.lastIndexOf("/Qt/");
        QString binaryCreatorDirectory;

        if (indexofQtRoot != -1)
        {
            indexofQtRoot += 4;
            QString qtRootDirectory = qtInstallDirectory.left(indexofQtRoot);
            QString qtInstallerFrameworkRoot = qtRootDirectory + "Tools/QtInstallerFramework/";

            QDir qtInstallerFrameworkRootDir(qtInstallerFrameworkRoot);
            QStringList entries = qtInstallerFrameworkRootDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);

            if (!entries.isEmpty())
            {
                binaryCreatorDirectory = QString("%1/%2/bin").arg(qtInstallerFrameworkRoot, entries.back());
                binaryCreatorDirectory = QDir(binaryCreatorDirectory).absolutePath();
            }
        }

        QFile configFile(directory + "/installer.pro");
        if (configFile.open(QFile::WriteOnly | QFile::Truncate))
        {
            QTextStream stream(&configFile);
            stream << "TEMPLATE = aux" << endl << endl;
            stream << "INSTALLER_NAME = $$PWD/instpdf4qt" << endl;
            stream << "INPUT = $$PWD/config/config.xml $$PWD/packages" << endl;
            stream << "pdfforqtinstaller.input = INPUT" << endl;
            stream << "pdfforqtinstaller.output = $$INSTALLER_NAME" << endl;
            stream << QString("pdfforqtinstaller.commands = %1/binarycreator -c $$PWD/config/config.xml -p $$PWD/packages ${QMAKE_FILE_OUT}").arg(binaryCreatorDirectory) << endl;
            stream << "pdfforqtinstaller.CONFIG += target_predeps no_link combine" << endl << endl;
            stream << "QMAKE_EXTRA_COMPILERS += pdfforqtinstaller";
            configFile.close();
        }
    }

    // Packages
    QDir().mkpath(directory + "/packages");

    auto addComponentMeta = [&](QString componentName, QString displayName, QString description, QString version, QString internalName, bool forcedInstallation, bool defaultInstall, bool addDefaultLicense, QString dependencies = QString())
    {
        QString componentMetaDirectory = directory + QString("/packages/%1/meta").arg(componentName);
        QDir().mkpath(componentMetaDirectory);

        QString metaFileName = QString("%1/package.xml").arg(componentMetaDirectory);

        QFile metaFile(metaFileName);
        if (metaFile.open(QFile::WriteOnly | QFile::Truncate))
        {
            QXmlStreamWriter metaFileWriter(&metaFile);
            metaFileWriter.setAutoFormatting(true);
            metaFileWriter.setAutoFormattingIndent(2);

            metaFileWriter.writeStartDocument();
            metaFileWriter.writeStartElement("Package");

            metaFileWriter.writeTextElement("DisplayName", displayName);
            metaFileWriter.writeTextElement("Description", description);
            metaFileWriter.writeTextElement("Version", version);
            metaFileWriter.writeTextElement("ReleaseDate", QDateTime::currentDateTime().toString("yyyy-MM-dd"));
            metaFileWriter.writeTextElement("Name", internalName);
            metaFileWriter.writeTextElement("ExpandedByDefault", "true");
            metaFileWriter.writeTextElement("ForcedInstallation", forcedInstallation ? "true" : "false");
            metaFileWriter.writeTextElement("Default", defaultInstall ? "true" : "false");

            if (!dependencies.isEmpty())
            {
                metaFileWriter.writeTextElement("Dependencies", dependencies);
            }

            if (addDefaultLicense)
            {
                QFile::copy(":/LICENSE.txt", QString("%1/LICENSE.txt").arg(componentMetaDirectory));

                metaFileWriter.writeStartElement("Licenses");
                metaFileWriter.writeStartElement("License");
                metaFileWriter.writeAttribute("name", tr("License Agreement"));
                metaFileWriter.writeAttribute("file", tr("LICENSE.txt"));
                metaFileWriter.writeEndElement();
                metaFileWriter.writeEndElement();
            }

            QString scriptFile = QString("%1/inst.qs").arg(componentMetaDirectory);
            if (QFile::exists(scriptFile))
            {
                metaFileWriter.writeTextElement("Script", "inst.qs");
            }

            metaFileWriter.writeEndElement();
            metaFileWriter.writeEndDocument();
            metaFile.close();
        }
    };

    auto addDirectoryContent = [&](QString componentName, QString applicationDirectory)
    {
        QString componentDataDirectory = directory + QString("/packages/%1/data/%2").arg(componentName, applicationDirectory);
        QDir().mkpath(componentDataDirectory);

        QString applicationDataDirectory = QString("%1/%2").arg(QApplication::applicationDirPath(), applicationDirectory);
        QStringList fileNames = QDir(applicationDataDirectory).entryList(QDir::Files | QDir::NoDotAndDotDot);

        for (QString fileName : fileNames)
        {
            QFile::copy(QString("%1/%2").arg(applicationDataDirectory, fileName), QString("%1/%2").arg(componentDataDirectory, fileName));
        }
    };

    auto addFileContent = [&](QString componentName, QString filter)
    {
        QString componentDataDirectory = directory + QString("/packages/%1/data").arg(componentName);
        QDir().mkpath(componentDataDirectory);

        QString applicationDataDirectory = QApplication::applicationDirPath();

        QStringList fileNames = QDir(applicationDataDirectory).entryList(QStringList() << filter, QDir::Files | QDir::NoDotAndDotDot);

        for (QString fileName : fileNames)
        {
            QFile::copy(QString("%1/%2").arg(applicationDataDirectory, fileName), QString("%1/%2").arg(componentDataDirectory, fileName));
        }
    };

    auto addStartMenuShortcut = [&](QString componentName, QString exeFileNameWithoutSuffix, QString description)
    {
        QString componentMetaDirectory = directory + QString("/packages/%1/meta").arg(componentName);
        QDir().mkpath(componentMetaDirectory);

        QString script =
        "function Component() \n"
        "{\n"
        "\n"
        "}\n"
        "Component.prototype.createOperations = function() \n"
        "{ \n"
        "    component.createOperations(); \n"
        "    if (systemInfo.productType === \"windows\") { \n"
        "        component.addOperation(\"CreateShortcut\", \"@TargetDir@/%1.exe\", \"@StartMenuDir@/%2.lnk\", \n"
        "            \"workingDirectory=@TargetDir@\", \"iconPath=@TargetDir@/%1.exe\", \n"
        "            \"iconId=0\", \"description=%2\"); \n"
        "    } \n"
        "} ";

        script = script.arg(exeFileNameWithoutSuffix, description);

        QFile installScriptFile(QString("%1/inst.qs").arg(componentMetaDirectory));
        if (installScriptFile.open(QFile::WriteOnly | QFile::Truncate))
        {
            QTextStream stream(&installScriptFile);
            stream << script;

            installScriptFile.close();
        }
    };

    // CoreLib package
    addStartMenuShortcut("pdf4qt_framework", "maintenancetool", tr("PDF4QT Maintenance Tool"));
    addComponentMeta("pdf4qt_framework", tr("Framework (Core libraries)"), tr("Framework libraries and other data files required to run all other programs."), pdf::PDF_LIBRARY_VERSION, "pdf4qt_framework", true, true, true);
    addDirectoryContent("pdf4qt_framework", "iconengines");
    addDirectoryContent("pdf4qt_framework", "imageformats");
    addDirectoryContent("pdf4qt_framework", "platforms");
    addDirectoryContent("pdf4qt_framework", "printsupport");
    addDirectoryContent("pdf4qt_framework", "styles");
    addDirectoryContent("pdf4qt_framework", "texttospeech");
    addFileContent("pdf4qt_framework", "*.dll");

    addStartMenuShortcut("pdf4qt_v_lite", "Pdf4QtViewerLite", tr("PDF4QT Viewer Lite"));
    addComponentMeta("pdf4qt_v_lite", tr("Viewer (Lite)"), tr("Simple PDF viewer with basic functions."), pdf::PDF_LIBRARY_VERSION, "pdf4qt_v_lite", false, true, false);
    addFileContent("pdf4qt_v_lite", "Pdf4QtViewerLite.exe");

    addStartMenuShortcut("pdf4qt_v_profi", "Pdf4QtViewerProfi", tr("PDF4QT Viewer Profi"));
    addComponentMeta("pdf4qt_v_profi", tr("Viewer (Profi)"), tr("Advanced PDF viewer with many functions, such as annotation editing, form filling, signature verification, and many optional plugins."), pdf::PDF_LIBRARY_VERSION, "pdf4qt_v_profi", false, true, false);
    addFileContent("pdf4qt_v_profi", "Pdf4QtViewerProfi.exe");

    addStartMenuShortcut("pdf4qt_dpo", "Pdf4QtDocPageOrganizer", tr("PDF4QT DocPage Organizer"));
    addComponentMeta("pdf4qt_dpo", tr("DocPage Organizer"), tr("Document page organizer (split/merge documents, insert/remove/move/clone pages, insert blank pages and images to create a new document)."), pdf::PDF_LIBRARY_VERSION, "pdf4qt_dpo", false, true, false);
    addFileContent("pdf4qt_dpo", "Pdf4QtDocPageOrganizer.exe");

    addStartMenuShortcut("pdf4qt_diff", "Pdf4QtDocDiff", tr("PDF4QT DocDiff"));
    addComponentMeta("pdf4qt_diff", tr("DocDiff"), tr("Compare content of two documents."), pdf::PDF_LIBRARY_VERSION, "pdf4qt_diff", false, true, false);
    addFileContent("pdf4qt_diff", "Pdf4QtDocDiff.exe");

    addStartMenuShortcut("pdf4qt_tool", "PdfTool", tr("PDF4QT Command Line Tool"));
    addComponentMeta("pdf4qt_tool", tr("PdfTool"), tr("Command line tool for manipulation of PDF files with many functions."), pdf::PDF_LIBRARY_VERSION, "pdf4qt_tool", false, false, false);
    addFileContent("pdf4qt_tool", "PdfTool.exe");

    addComponentMeta("pdf4qt_v_profi_plugins", tr("Viewer (Profi) | Plugins"), tr("Plugins for profi viewer providing additional functionality."), pdf::PDF_LIBRARY_VERSION, "pdf4qt_v_profi_plugins", false, false, false, "pdf4qt_v_profi");

    for (const pdf::PDFPluginInfo& info : m_plugins)
    {
        QString pluginName = info.pluginFile;
        pluginName.remove(".dll");

        QString componentName = QString("pdf4qt_v_profi_plugins.%1").arg(pluginName);
        addComponentMeta(componentName, info.name, info.description, info.version, componentName, false, false, false, "pdf4qt_v_profi");

        QString componentDataDirectory = directory + QString("/packages/%1/data/pdfplugins").arg(componentName);
        QDir().mkpath(componentDataDirectory);
        QFile::copy(info.pluginFileWithPath, QString("%1/%2").arg(componentDataDirectory, info.pluginFile));
    }
}

void PDFProgramController::onActionGetSource()
{
    QDesktopServices::openUrl(QUrl("https://github.com/JakubMelka/PDF4QT"));
}

void PDFProgramController::onPageRenderingErrorsChanged(pdf::PDFInteger pageIndex, int errorsCount)
{
    if (errorsCount > 0)
    {
        m_mainWindowInterface->setStatusBarMessage(tr("Rendering of page %1: %2 errors occured.").arg(pageIndex + 1).arg(errorsCount), 4000);
    }
}

}   // namespace pdfviewer
