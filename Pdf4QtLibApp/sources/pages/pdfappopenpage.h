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

#ifndef PDFAPPOPENPAGE_H
#define PDFAPPOPENPAGE_H

#include "pdfappglobal.h"
#include "pdfapphomebasepage.h"
#include "pdfapphomewidgets.h"

#include "pdfcomponentbuttons.h"

#include <QPointer>
#include <QPushButton>

class QVBoxLayout;

namespace pdf
{

class PDFFolderCard;
class PDFRecentFileRow;
class PDFWorkspaceRow;

/// Page Open /open (pdf4qt-open 61:1201), also the content of the tab New tab
/// (pdf4qt-new-tab 955:152339, the same page without the sidebar): the drop zone
/// with Browse Files…, Recent Folders (the standard folders first, then the
/// folders used recently), Pinned Files, Recent Files, Workspaces (Restore last
/// session, the saved workspaces, Save open documents as workspace…) and Pinned
/// Locations (Add… is the folder dialog of the system). Every item has its menu
/// (941:137569). The side column is below the main column in a narrow window.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppOpenPage : public PDFAppHomeBasePage
{
    Q_OBJECT

private:
    using BaseClass = PDFAppHomeBasePage;

public:
    /// Items shown by the sections (View all leads to Recent)
    static constexpr int MAXIMUM_FOLDERS = 6;
    static constexpr int MAXIMUM_PINNED_FILES = 4;
    static constexpr int MAXIMUM_RECENT_FILES = 5;

    explicit PDFAppOpenPage(PDFAppContext* context, QWidget* parent = nullptr);
    virtual ~PDFAppOpenPage() override;

    virtual bool setRoute(const PDFAppRoute& route) override;
    virtual QString getTitle() const override;
    virtual QString getHeaderTitle() const override;
    virtual QString getHeaderSubtitle() const override;

    /// The storage banner is at the top of the main column (958:28036, 955:150880)
    virtual QBoxLayout* getBannerLayout() const override;

    // Parts of the page (tests)
    PDFAppDropZone* getDropZone() const { return m_dropZone; }
    PDFAppColumns* getColumns() const { return m_columns; }
    QList<PDFFolderCard*> getFolderCards() const;
    QList<PDFRecentFileRow*> getPinnedFileRows() const;
    QList<PDFRecentFileRow*> getRecentFileRows() const;
    QList<PDFWorkspaceRow*> getWorkspaceRows() const;
    QList<PDFFolderCard*> getLocationCards() const;
    PDFAppSessionRow* getSessionRow() const { return m_sessionRow; }
    QPushButton* getSaveWorkspaceButton() const { return m_saveWorkspaceButton; }
    PDFTextLink* getPinnedViewAllLink() const { return m_pinnedViewAll; }
    PDFTextLink* getAddLocationLink() const { return m_addLocationLink; }
    QWidget* getPinnedFilesSection() const { return m_pinnedSection; }
    QWidget* getRecentFilesSection() const { return m_recentSection; }
    QWidget* getWorkspacesCard() const { return m_workspacesCard; }
    QWidget* getLocationsCard() const { return m_locationsCard; }
    QWidget* getFoldersGrid() const { return m_foldersGrid; }

protected:
    virtual void refresh() override;
    virtual void onPathInfoChanged(const QString& path) override;

private:
    QWidget* createFolders(QWidget* parent);
    QWidget* createFileList(const QString& title, PDFTextLink* link, QWidget* parent, QPointer<QWidget>* card, QPointer<QVBoxLayout>* layout, PDFLabel** emptyLabel, const QString& emptyText);
    QWidget* createWorkspaces(QWidget* parent);
    QWidget* createLocations(QWidget* parent);
    void refreshFolders();
    void refreshFiles();
    void refreshWorkspaces();
    void refreshLocations();
    PDFRecentFileRow* createFileRow(const PDFRecentFile& file, QWidget* parent);
    void updateFileStatus(PDFRecentFileRow* row);

    void onFileClicked();
    void onFilePinnedChanged(bool isPinned);
    void onFolderClicked();
    void onWorkspaceClicked();
    void onSessionRestoreRequested();
    void onSaveWorkspaceClicked();
    void onAddLocationClicked();
    void onBrowseRequested();

    QPointer<PDFAppColumns> m_columns;
    QPointer<PDFAppDropZone> m_dropZone;
    QPointer<QWidget> m_foldersGrid;
    QList<QPointer<PDFFolderCard>> m_folderCards;
    QPointer<QWidget> m_pinnedSection;
    QPointer<QWidget> m_pinnedCard;
    QPointer<QVBoxLayout> m_pinnedLayout;
    QPointer<PDFTextLink> m_pinnedViewAll;
    QList<QPointer<PDFRecentFileRow>> m_pinnedRows;
    QPointer<QWidget> m_recentSection;
    QPointer<QWidget> m_recentCard;
    QPointer<QVBoxLayout> m_recentLayout;
    QPointer<PDFLabel> m_recentEmpty;
    QList<QPointer<PDFRecentFileRow>> m_recentRows;
    QPointer<QWidget> m_workspacesCard;
    QPointer<QVBoxLayout> m_workspacesLayout;
    QPointer<PDFAppSessionRow> m_sessionRow;
    QList<QPointer<PDFWorkspaceRow>> m_workspaceRows;
    QPointer<QPushButton> m_saveWorkspaceButton;
    QPointer<QWidget> m_locationsCard;
    QPointer<QVBoxLayout> m_locationsLayout;
    QPointer<PDFTextLink> m_addLocationLink;
    QPointer<PDFLabel> m_locationsEmpty;
    QList<QPointer<PDFFolderCard>> m_locationCards;
    QString m_foldersSignature;
    QString m_filesSignature;
    QString m_workspacesSignature;
    QString m_locationsSignature;
};

/// Factory of the page Open
class PDF4QTLIBAPPSHARED_EXPORT PDFAppOpenPageFactory : public PDFAppPageFactory
{
public:
    virtual PDFAppPage* createPage(PDFAppContext* context, QWidget* parent) const override;
};

}   // namespace pdf

#endif // PDFAPPOPENPAGE_H
