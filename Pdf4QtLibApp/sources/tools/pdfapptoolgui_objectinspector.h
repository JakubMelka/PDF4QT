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

#ifndef PDFAPPTOOLGUI_OBJECTINSPECTOR_H
#define PDFAPPTOOLGUI_OBJECTINSPECTOR_H

#include "pdfappdeclarativetoolgui.h"
#include "pdfappobjecttreemodel.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfappworkspace.h"
#include "pdfobjectinspector.h"

#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdffluentwidgets.h"

#include <QFutureWatcher>
#include <QImage>
#include <QPointer>
#include <QStackedWidget>

#include <map>
#include <memory>
#include <vector>

class QToolButton;

namespace pdf
{

class PDFAppCancelFlag;

/// The contents of Object Inspector (482:14847 "Contents"): a read-only text, the PDF syntax of an object, the
/// decoded data of a stream or its hex dump (Q7.12). Copy of the data of a stream is an extraction (R7-22 (6),
/// Q7.2: bit 5 of Table 22): with isCopyGuarded the view asks copyRequested instead of copying itself (the
/// keyboard and its menu), the GUI checks the permission (the owner password offered) and calls copySelection.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppObjectContentsView : public PDFPlainTextEdit
{
    Q_OBJECT

private:
    using BaseClass = PDFPlainTextEdit;

public:
    /// The contents of 482:14847: padding 16 (the margin of the document, no margin of the viewport), lines of 19
    static constexpr int PADDING = 16;
    static constexpr int LINE_HEIGHT = 19;

    explicit PDFAppObjectContentsView(QWidget* parent = nullptr);
    virtual ~PDFAppObjectContentsView() override;

    /// The copy needs the permission of the GUI (the data of a stream)
    bool isCopyGuarded() const { return m_isCopyGuarded; }
    void setCopyGuarded(bool isCopyGuarded) { m_isCopyGuarded = isCopyGuarded; }

    /// Copies the selected text (or all, when nothing is selected) to the clipboard
    void copySelection();

    /// A copy: the guarded one asks, the other copies at once
    void requestCopy();

Q_SIGNALS:
    /// A guarded copy was asked (the GUI decides; emitted last)
    void copyRequested();

protected:
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual void contextMenuEvent(QContextMenuEvent* event) override;

    /// The data of a drag of the selection and of the selection clipboard of X11: a guarded view gives no text
    /// (RS7APP: a drag out of a read-only text and the selection of X11 would copy the data of a stream past the
    /// check of bit 5, Q7.2); the guarded copy itself takes the text of the cursor (copySelection)
    virtual QMimeData* createMimeDataFromSelection() const override;

private:
    void onSelectAll();

    bool m_isCopyGuarded = false;
};

/// The picture of an image XObject in the contents (482:15559 "Image preview"): the image fitted into the widget,
/// never enlarged, on bg/surface (tokens of Fluent only)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppObjectImageView : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAppObjectImageView(QWidget* parent = nullptr);
    virtual ~PDFAppObjectImageView() override;

    void setImage(QImage image);
    const QImage& getImage() const { return m_image; }

protected:
    virtual void paintEvent(QPaintEvent* event) override;

private:
    QImage m_image;
};

/// The GUI of Object Inspector (plan of step 7 of phase 2, 4.3; Figma 482:14847 … 482:16403): only Close; the
/// toolbar the view (13 views with their counts, a view the document does not have is not listed, 482:15054),
/// Pin and Unpin; the left panel the lazy tree of the view (PDFAppObjectTreeModel), the canvas the tabs Current
/// Object + the pinned ones (snapshots of their nodes, 482:15778, 482:15989) with the contents, the right panel
/// Information, Description (and Image for an image). The classification of the document (PDFObjectInspector::
/// prepare, the object streams FD-6) and the decoding of a stream and the picture of an image always run in one
/// job each (QtConcurrent::run with a PDFAppCancelFlag, a copy of the reference and the shared immutable inspector;
/// the result taken only from the last request - a generation - and only while this GUI lives, the watcher is
/// its child; B165). Copy of the content of a stream needs bit 5 (Q7.2, R7-22 (6): PDFAppDocumentPermissions with
/// the owner password offered). After a change of the document the inspector is prepared again, the tree built
/// again and the selection restored by its reference; the pinned tabs stay (snapshots).
///
/// Ownership: the shell owns this GUI; the model and the watchers are its children, the widgets belong to the
/// panels and the canvas of the shell (QPointer here). A job owns only copies (shared pointers of immutable data).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolGUIObjectInspector : public PDFAppDeclarativeToolGUI
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDeclarativeToolGUI;

public:
    /// Width of the panel of the views (482:15260)
    static constexpr int VIEW_MENU_WIDTH = 320;

    /// Most pinned tabs
    static constexpr int MAXIMUM_PINNED = 16;

    /// What the contents of a tab show
    enum class ContentKind
    {
        Syntax,     ///< "PDF syntax of the object"
        Loading,    ///< The job decodes the stream or the image
        Text,       ///< "Decoded stream data" as a text
        Hex,        ///< "Decoded stream data" as a hex dump (binary data, Q7.12)
        Image,      ///< "Image preview"
        Error       ///< "The stream cannot be decoded" (482:16403) with the reason
    };

    /// A tab of the canvas: Current Object (follows the selection) or a pinned snapshot
    class Tab
    {
    public:
        quint64 id = 0;
        bool isPinned = false;
        bool hasNode = false;
        PDFObjectInspectorNode node;
        ContentKind kind = ContentKind::Syntax;
        QString text;               ///< Syntax, Text, Hex: the text
        QImage image;               ///< Image: the picture
        QString errorText;          ///< Error: the reason
        bool isTruncated = false;   ///< Only a part is shown (MAXIMUM_SHOWN_BYTES)
        qint64 decodedSize = 0;

        /// The inspector of the version of the node (shared, immutable): a pinned tab is a snapshot - its contents
        /// load from its own version also after a change of the document, while the new inspector is prepared
        /// (RS7APP L6: the pinned tab stayed "Loading" or showed the data of the new version). A pinned tab keeps
        /// its version of the document alive until it is unpinned.
        std::shared_ptr<const PDFObjectInspector> inspector;
    };

    explicit PDFAppToolGUIObjectInspector(QObject* parent = nullptr);
    virtual ~PDFAppToolGUIObjectInspector() override;

    /// Registers the core tool and this GUI (PDFAppBuiltinTools::registerAll)
    static void registerObjectInspectorTools();

    virtual QWidget* createCanvasPage(QWidget* parent) override;
    virtual bool isConfirmShown() const override { return false; }

    /// The session is the use (owner 2026-11-20): a selected object, a view, a tab, a pin, or 10 s shown
    virtual bool isInteractive() const override { return true; }
    virtual QString getCancelText() const override;
    virtual QString getStatusText() const override;
    virtual bool hasOwnStatusItems() const override { return true; }    ///< K7.27: no page, no zoom item
    virtual void onParametersChanged(const QString& name) override;
    virtual void onDocumentChanged() override;

    /// The view of the parameters
    PDFObjectInspectorView getView() const;
    void setView(PDFObjectInspectorView view);

    /// The inspector of the version of now is ready (the job of the preparation has ended)
    bool isReady() const { return m_inspector != nullptr; }
    const std::shared_ptr<const PDFObjectInspector>& getInspector() const { return m_inspector; }

    /// The tabs: the first is Current Object
    const std::vector<Tab>& getTabs() const { return m_tabs; }
    int getActiveTab() const { return m_activeTab; }
    void setActiveTab(int index);

    /// Pin: the node of Current Object becomes a pinned tab; Unpin closes the active pinned tab
    bool pin();
    bool unpin();
    bool canPin() const;
    bool canUnpin() const;

    /// Chooses a row of the tree (as a click)
    void selectIndex(const QModelIndex& index);

    /// Copies the contents of the active tab (as Ctrl+C of the contents): the data of a stream asks bit 5
    void copyContents();

    /// A job decodes the contents of the active tab
    bool isLoadingContents() const { return m_contentWatcher != nullptr; }
    bool isPreparing() const { return m_prepareWatcher != nullptr; }

    // Parts (tests; nullptr = none)
    PDFAppObjectTreeModel* getTreeModel() const { return m_model.data(); }
    PDFTreeView* getTreeView() const { return m_treeView.data(); }
    QToolButton* getViewButton() const;
    PDFTileRow* getViewRow(PDFObjectInspectorView view) const;
    QToolButton* getPinButton() const { return m_pinButton.data(); }
    QToolButton* getUnpinButton() const { return m_unpinButton.data(); }
    PDFAppToolPanelHeader* getObjectsHeader() const { return m_objectsHeader.data(); }
    PDFAppParagraph* getTreeNote() const { return m_treeNote.data(); }
    QWidget* getTabBar() const { return m_tabBar.data(); }
    std::vector<QToolButton*> getTabButtons() const;
    PDFLabel* getContentsCaption() const { return m_contentsCaption.data(); }
    PDFAppObjectContentsView* getContentsView() const { return m_contentsView.data(); }
    PDFAppObjectImageView* getImageView() const { return m_imageView.data(); }
    PDFAppToolCallout* getErrorCallout() const { return m_errorCallout.data(); }
    PDFKeyValueList* getInformationRows() const { return m_informationRows.data(); }
    PDFAppParagraph* getDescription() const { return m_description.data(); }
    PDFAppToolSectionHeader* getImageHeader() const { return m_imageHeader.data(); }
    PDFKeyValueList* getImageRows() const { return m_imageRows.data(); }
    PDFAppParagraph* getTabNote() const { return m_tabNote.data(); }

    /// Title of a view ("Content Streams") and its icon (482:15260)
    static QString getViewTitle(PDFObjectInspectorView view);
    static QString getViewIconName(PDFObjectInspectorView view);

    /// The subtitle of the panel of the tree ("From the trailer of the document", "24 page objects")
    static QString getViewSubtitle(PDFObjectInspectorView view, qint64 count);

    /// The type of an object ("Dictionary", "Stream")
    static QString getTypeTitle(const PDFObject& object);

    /// The text of the reference of a node: "14 0 R", "Part of 12 0 R", "Trailer"
    static QString getReferenceTitle(const PDFObjectInspectorNode& node);

    /// The title of a tab ("1 0 R", "12 0 R (part)", "Trailer")
    static QString getTabTitle(const PDFObjectInspectorNode& node);

    /// The node is an image XObject (/Subtype /Image, also without /Type, B185)
    static bool isImage(const PDFObjectInspectorNode& node);

protected:
    virtual void describeToolbar(PDFAppToolPanelBuilder& builder) override;
    virtual void describeLeftPanel(PDFAppToolPanelBuilder& builder) override;
    virtual void describeRightPanel(PDFAppToolPanelBuilder& builder) override;

private:
    /// The result of the job of the preparation
    class PrepareResult
    {
    public:
        quint64 generation = 0;
        std::shared_ptr<const PDFObjectInspector> inspector;
    };

    /// The result of the job of the contents
    class ContentResult
    {
    public:
        quint64 generation = 0;
        quint64 tabId = 0;
        ContentKind kind = ContentKind::Error;
        QString text;
        QImage image;
        QString errorText;
        bool isTruncated = false;
        qint64 decodedSize = 0;
    };

    static PrepareResult prepareJob(quint64 generation, PDFDocumentPointer document, PDFCMSPointer cms, std::shared_ptr<PDFAppCancelFlag> cancel);
    static ContentResult contentJob(quint64 generation, quint64 tabId, std::shared_ptr<const PDFObjectInspector> inspector, PDFObjectReference reference, bool isImageNode,
                                    std::shared_ptr<PDFAppCancelFlag> cancel);

    void startPreparation();
    void cancelPreparation();
    void onPreparationFinished();
    void startContentJob();
    void cancelContentJob();
    void onContentJobFinished();

    void rebuildTree(PDFObjectReference selection);
    void loadTab(Tab& tab);

    /// The inspector of the contents of a tab: its own version for a pinned tab, the version of now for Current
    /// Object (null while it is prepared)
    std::shared_ptr<const PDFObjectInspector> getTabInspector(const Tab& tab) const;
    Tab* findTab(quint64 id);
    void updateAll();
    void updateViewMenu();
    void updateButtons();
    void updateTabBar();
    bool updateContents();  ///< false = this GUI is gone (a slot of a signal of the contents deleted it)
    void updateRightPanel();
    void onCurrentIndexChanged(const QModelIndex& current, const QModelIndex& previous);
    void onQueuedUpdate();
    void onViewRowClicked();
    void onTabButtonClicked();
    void onPinClicked();
    void onUnpinClicked();
    void onCopyRequested();

    std::shared_ptr<const PDFObjectInspector> m_inspector;
    quint64 m_prepareGeneration = 0;
    quint64 m_contentGeneration = 0;
    quint64 m_nextTabId = 1;
    quint64 m_contentTabId = 0;                 ///< The tab of the running job of the contents
    PDFObjectReference m_pendingSelection;      ///< The selection restored when the new inspector is ready
    bool m_isRestoringSelection = false;        ///< rebuildTree restores the selection: the active tab stays
    std::shared_ptr<PDFAppCancelFlag> m_prepareCancel;
    std::shared_ptr<PDFAppCancelFlag> m_contentCancel;
    QPointer<QFutureWatcher<PrepareResult>> m_prepareWatcher;
    QPointer<QFutureWatcher<ContentResult>> m_contentWatcher;
    std::vector<Tab> m_tabs;
    int m_activeTab = 0;
    bool m_isUpdateQueued = false;              ///< A change of the selection of the tree updates the parts from the event loop

    QPointer<PDFAppObjectTreeModel> m_model;
    QPointer<PDFAppToolPanelPopup> m_viewMenu;
    std::map<PDFObjectInspectorView, QPointer<PDFTileRow>> m_viewRows;
    QPointer<QToolButton> m_pinButton;
    QPointer<QToolButton> m_unpinButton;
    QPointer<PDFAppToolPanelHeader> m_objectsHeader;
    QPointer<PDFTreeView> m_treeView;
    QPointer<PDFAppParagraph> m_treeNote;
    QPointer<QWidget> m_tabBar;
    std::vector<QPointer<QToolButton>> m_tabButtons;
    QPointer<PDFLabel> m_contentsCaption;
    QPointer<QStackedWidget> m_contentsStack;
    QPointer<PDFAppObjectContentsView> m_contentsView;
    QPointer<PDFAppObjectImageView> m_imageView;
    QPointer<QWidget> m_errorPage;
    QPointer<PDFAppToolCallout> m_errorCallout;
    QPointer<PDFKeyValueList> m_informationRows;
    QPointer<PDFAppParagraph> m_description;
    QPointer<PDFAppToolSectionHeader> m_imageHeader;
    QPointer<PDFKeyValueList> m_imageRows;
    QPointer<PDFAppParagraph> m_tabNote;
};

}   // namespace pdf

#endif // PDFAPPTOOLGUI_OBJECTINSPECTOR_H
