// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
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

#ifndef PDFPROGRAMCONTROLLER_H
#define PDFPROGRAMCONTROLLER_H

#include "pdfviewerglobal.h"
#include "pdfdocument.h"
#include "pdfsignaturehandler.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentpropertiesdialog.h"
#include "pdfplugin.h"
#include "pdfbookmarkmanager.h"

#include <QObject>
#include <QAction>
#include <QToolButton>
#include <QActionGroup>
#include <QFileSystemWatcher>

#include <array>

class QMainWindow;
class QComboBox;
class QToolBar;

namespace pdf
{
class PDFAction;
class PDFWidget;
class PDFCMSManager;
class PDFToolManager;
class PDFWidgetFormManager;
class PDFWidgetAnnotationManager;
}

namespace pdfviewer
{
class PDFViewerSettings;
class PDFUndoRedoManager;
class PDFRecentFileManager;
class PDFTextToSpeech;
class PDFActionComboBox;

class IMainWindow
{
public:
    explicit IMainWindow() = default;
    virtual ~IMainWindow() = default;

    virtual void updateUI(bool fullUpdate) = 0;
    virtual QMenu* addToolMenu(QString name) = 0;
    virtual void setStatusBarMessage(QString message, int time) = 0;
    virtual void setDocument(const pdf::PDFModifiedDocument& document) = 0;
    virtual void adjustToolbar(QToolBar* toolbar) = 0;
    virtual pdf::PDFTextSelection getSelectedText() const = 0;
};

class PDF4QTLIBGUILIBSHARED_EXPORT PDFActionManager : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFActionManager(QObject* parent);

    enum Action
    {
        Open,
        Close,
        Quit,
        AutomaticDocumentRefresh,
        ZoomIn,
        ZoomOut,
        Find,
        FindPrevious,
        FindNext,
        SelectTextAll,
        DeselectText,
        CopyText,
        RotateRight,
        RotateLeft,
        Print,
        Undo,
        Redo,
        Save,
        SaveAs,
        Properties,
        Options,
        ResetToFactorySettings,
        ClearRecentFileHistory,
        CertificateManager,
        GetSource,
        BecomeSponsor,
        About,
        SendByMail,
        RenderToImages,
        Optimize,
        Sanitize,
        CreateBitonalDocument,
        Encryption,
        FitPage,
        FitWidth,
        FitHeight,
        ShowRenderingErrors,
        GoToDocumentStart,
        GoToDocumentEnd,
        GoToNextPage,
        GoToPreviousPage,
        GoToNextLine,
        GoToPreviousLine,
        CreateStickyNoteComment,
        CreateStickyNoteHelp,
        CreateStickyNoteInsert,
        CreateStickyNoteKey,
        CreateStickyNoteNewParagraph,
        CreateStickyNoteNote,
        CreateStickyNoteParagraph,
        CreateTextHighlight,
        CreateTextUnderline,
        CreateTextStrikeout,
        CreateTextSquiggly,
        CreateHyperlink,
        CreateInlineText,
        CreateStraightLine,
        CreatePolyline,
        CreateRectangle,
        CreatePolygon,
        CreateEllipse,
        CreateFreehandCurve,
        CreateStampApproved,
        CreateStampAsIs,
        CreateStampConfidential,
        CreateStampDepartmental,
        CreateStampDraft,
        CreateStampExperimental,
        CreateStampExpired,
        CreateStampFinal,
        CreateStampForComment,
        CreateStampForPublicRelease,
        CreateStampNotApproved,
        CreateStampNotForPublicRelease,
        CreateStampSold,
        CreateStampTopSecret,
        RenderOptionAntialiasing,
        RenderOptionTextAntialiasing,
        RenderOptionSmoothPictures,
        RenderOptionIgnoreOptionalContentSettings,
        RenderOptionDisplayRenderTimes,
        RenderOptionDisplayAnnotations,
        RenderOptionInvertColors,
        RenderOptionGrayscale,
        RenderOptionBitonal,
        RenderOptionHighContrast,
        RenderOptionCustomColors,
        RenderOptionShowTextBlocks,
        RenderOptionShowTextLines,
        PageLayoutSinglePage,
        PageLayoutContinuous,
        PageLayoutTwoPages,
        PageLayoutTwoColumns,
        PageLayoutFirstPageOnRightSide,
        ToolSelectText,
        ToolSelectTable,
        ToolMagnifier,
        ToolScreenshot,
        ToolExtractImage,
        BookmarkPage,
        BookmarkGoToNext,
        BookmarkGoToPrevious,
        BookmarkExport,
        BookmarkImport,
        BookmarkGenerateAutomatically,
        LastAction
    };

    enum ActionGroup
    {
        CreateStickyNoteGroup,
        CreateTextHighlightGroup,
        CreateStampGroup,
        LastActionGroup
    };

    inline void setAction(Action type, QAction* action) { m_actions[type] = action; }
    inline QAction* getAction(Action type) const { return m_actions[type]; }

    inline QActionGroup* getActionGroup(ActionGroup group) const { return m_actionGroups[group]; }

    /// Creates a tool button with menu for action group. If action group
    /// doesn't exist, then nullptr is returned.
    /// \param group Action group
    /// \param parent Tool button's parent
    QToolButton* createToolButtonForActionGroup(ActionGroup group, QWidget* parent) const;

    /// Sets shortcut for given action. If action is nullptr,
    /// then nothing happens.
    /// \param type Action type
    /// \param sequence Key sequence
    void setShortcut(Action type, QKeySequence sequence);

    /// Sets action data. If action is nullptr, then nothing happens.
    /// \param type Action type
    /// \param userData User data
    void setUserData(Action type, QVariant userData);

    /// Sets enabled state of the action. If action is nullptr, the nothing happens.
    /// \param type Action type
    /// \param enabled Enabled
    void setEnabled(Action type, bool enabled);

    /// Sets check state of the action. If action is nullptr, the nothing happens.
    /// \param type Action type
    /// \param enabled Enabled
    void setChecked(Action type, bool checked);

    /// Returns a list of rendering option actions
    std::vector<QAction*> getRenderingOptionActions() const;

    /// Returns a list of all actions
    std::vector<QAction*> getActions() const;

    /// Adds additional action to action manager
    void addAdditionalAction(QAction* action);

    void initActions(QSize iconSize, bool initializeStampActions);
    void styleActions();

private:
    bool hasActions(const std::initializer_list<Action>& actionTypes) const;
    std::vector<QAction*> getActionList(const std::initializer_list<Action>& actionTypes) const;

    std::array<QAction*, LastAction> m_actions;
    std::array<QActionGroup*, LastActionGroup> m_actionGroups;
    std::vector<QAction*> m_additionalActions;
    QSize m_iconSize;
};

class PDF4QTLIBGUILIBSHARED_EXPORT PDFProgramController : public QObject, public pdf::IPluginDataExchange
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFProgramController(QObject* parent);
    virtual ~PDFProgramController() override;

    enum Feature
    {
        None            = 0x0000,   ///< No feature
        Tools           = 0x0001,   ///< Tools
        Forms           = 0x0002,   ///< Forms
        UndoRedo        = 0x0004,   ///< Undo/redo
        Plugins         = 0x0008,   ///< Plugins
        TextToSpeech    = 0x0010,   ///< Text to speech
        AllFeatures     = 0xFFFF,   ///< All features enabled
    };
    Q_DECLARE_FLAGS(Features, Feature)

    void openDocument(const QString& fileName);
    void setDocument(pdf::PDFModifiedDocument document, std::vector<pdf::PDFSignatureVerificationResult> signatureVerificationResult, bool isCurrentSaved);
    void closeDocument();

    pdf::PDFWidget* getPdfWidget() const { return m_pdfWidget; }
    pdf::PDFToolManager* getToolManager() const { return  m_toolManager; }
    PDFRecentFileManager* getRecentFileManager() const { return m_recentFileManager; }
    PDFViewerSettings* getSettings() const { return m_settings; }
    pdf::PDFDocument* getDocument() const { return m_pdfDocument.data(); }
    pdf::PDFCertificateStore* getCertificateStore() const { return const_cast<pdf::PDFCertificateStore*>(&m_certificateStore); }
    PDFBookmarkManager* getBookmarkManager() const { return m_bookmarkManager; }
    PDFTextToSpeech* getTextToSpeech() const { return m_textToSpeech; }
    const std::vector<pdf::PDFSignatureVerificationResult>* getSignatures() const { return &m_signatures; }

    void initialize(Features features,
                    QMainWindow* mainWindow,
                    IMainWindow* mainWindowInterface,
                    PDFActionManager* actionManager,
                    pdf::PDFProgress* progress);
    void initActionComboBox(PDFActionComboBox* comboBox);
    void finishInitialization();
    void writeSettings();
    void resetSettings();
    void clearRecentFileHistory();

    void performPrint();
    void performSave();
    void performSaveAs();

    void onActionTriggered(const pdf::PDFAction* action);
    void onDocumentModified(pdf::PDFModifiedDocument document);
    void updateActionsAvailability();

    bool getIsBusy() const;
    void setIsBusy(bool isBusy);

    bool canClose() const;
    bool askForSaveDocumentBeforeClose();

    virtual QString getOriginalFileName() const override;
    virtual pdf::PDFTextSelection getSelectedText() const override;
    virtual QMainWindow* getMainWindow() const override;
    virtual VoiceSettings getVoiceSettings() const override;

    bool isFactorySettingsBeingRestored() const;

signals:
    void queryPasswordRequest(QString* password, bool* ok);

private:

    struct AsyncReadingResult
    {
        pdf::PDFDocumentPointer document;
        QString errorMessage;
        pdf::PDFDocumentReader::Result result = pdf::PDFDocumentReader::Result::Cancelled;
        std::vector<pdf::PDFSignatureVerificationResult> signatures;
    };

    void initializeToolManager();
    void initializeAnnotationManager();
    void initializeFormManager();
    void initializeBookmarkManager();

    void onActionGoToDocumentStartTriggered();
    void onActionGoToDocumentEndTriggered();
    void onActionGoToNextPageTriggered();
    void onActionGoToPreviousPageTriggered();
    void onActionGoToNextLineTriggered();
    void onActionGoToPreviousLineTriggered();
    void onActionZoomIn();
    void onActionZoomOut();
    void onActionRotateRightTriggered();
    void onActionRotateLeftTriggered();
    void onActionRenderingOptionTriggered(bool checked);
    void onActionPropertiesTriggered();
    void onActionAboutTriggered();
    void onActionSendByEMailTriggered();
    void onActionRenderToImagesTriggered();
    void onActionOptimizeTriggered();
    void onActionSanitizeTriggered();
    void onActionCreateBitonalDocumentTriggered();
    void onActionEncryptionTriggered();
    void onActionFitPageTriggered();
    void onActionFitWidthTriggered();
    void onActionFitHeightTriggered();
    void onActionRenderingErrorsTriggered();
    void onActionPageLayoutSinglePageTriggered();
    void onActionPageLayoutContinuousTriggered();
    void onActionPageLayoutTwoPagesTriggered();
    void onActionPageLayoutTwoColumnsTriggered();
    void onActionFirstPageOnRightSideTriggered();
    void onActionFindTriggered();
    void onActionOptionsTriggered();
    void onActionCertificateManagerTriggered();
    void onActionOpenTriggered();
    void onActionCloseTriggered();
    void onActionGetSource();
    void onActionBecomeSponsor();
    void onActionAutomaticDocumentRefresh();
    void onActionBookmarkPage();
    void onActionBookmarkGoToNext();
    void onActionBookmarkGoToPrevious();
    void onActionBookmarkExport();
    void onActionBookmarkImport();
    void onActionBookmarkGenerateAutomatically(bool checked);

    void onDrawSpaceChanged();
    void onPageLayoutChanged();
    void onDocumentReadingFinished();
    void onDocumentUndoRedo(pdf::PDFModifiedDocument document);
    void onQueryPasswordRequest(QString* password, bool* ok);
    void onPageRenderingErrorsChanged(pdf::PDFInteger pageIndex, int errorsCount);
    void onViewerSettingsChanged();
    void onColorManagementSystemChanged();
    void onFileChanged(const QString& fileName);
    void onBookmarkActivated(int index, PDFBookmarkManager::Bookmark bookmark);

    void updateMagnifierToolSettings();
    void updateUndoRedoSettings();
    void updateUndoRedoActions();
    void updateTitle();
    void updatePageLayoutActions();
    void updateRenderingOptionActions();
    void updateBookmarkSettings();

    void setPageLayout(pdf::PageLayout pageLayout);
    void updateFileInfo(const QString& fileName);
    void updateFileWatcher(bool forceDisable = false);

    enum SettingFlag
    {
        NoSettings          = 0x0000,   ///< No feature
        WindowSettings      = 0x0001,   ///< Window settings
        GeneralSettings     = 0x0002,   ///< General settings
        PluginsSettings     = 0x0004,   ///< Enabled plugin settings
        ActionSettings      = 0x0008,   ///< Action settings
        RecentFileSettings  = 0x0010,   ///< Recent files settings
        CertificateSettings = 0x0020,   ///< Certificate settings
    };
    Q_DECLARE_FLAGS(Settings, SettingFlag)

    void loadPlugins();
    void readSettings(Settings settings);

    void saveDocument(const QString& fileName);
    void savePageLayoutPerDocument();

    PDFActionManager* m_actionManager;
    QMainWindow* m_mainWindow;
    IMainWindow* m_mainWindowInterface;
    pdf::PDFWidget* m_pdfWidget;
    PDFViewerSettings* m_settings;
    PDFUndoRedoManager* m_undoRedoManager;
    PDFRecentFileManager* m_recentFileManager;
    pdf::PDFOptionalContentActivity* m_optionalContentActivity;
    pdf::PDFDocumentPointer m_pdfDocument;
    PDFTextToSpeech* m_textToSpeech;
    bool m_isDocumentSetInProgress;

    QFuture<AsyncReadingResult> m_future;
    QFutureWatcher<AsyncReadingResult>* m_futureWatcher;

    pdf::PDFCMSManager* m_CMSManager;
    pdf::PDFToolManager* m_toolManager;
    pdf::PDFWidgetAnnotationManager* m_annotationManager;
    pdf::PDFWidgetFormManager* m_formManager;
    PDFBookmarkManager* m_bookmarkManager;
    PDFActionComboBox* m_actionComboBox;

    PDFFileInfo m_fileInfo;
    QFileSystemWatcher m_fileWatcher;
    pdf::PDFCertificateStore m_certificateStore;
    std::vector<pdf::PDFSignatureVerificationResult> m_signatures;

    bool m_isBusy;
    bool m_isFactorySettingsBeingRestored;
    pdf::PDFProgress* m_progress;

    QStringList m_enabledPlugins;
    bool m_loadAllPlugins;
    pdf::PDFPluginInfos m_plugins;
    std::vector<std::pair<pdf::PDFPluginInfo, pdf::PDFPlugin*>> m_loadedPlugins;
};

}   // namespace pdfviewer

#endif // PDFPROGRAMCONTROLLER_H
