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

#ifndef PDFAPPHOMEPAGE_H
#define PDFAPPHOMEPAGE_H

#include "pdfappglobal.h"
#include "pdfapphomebasepage.h"
#include "pdfapphomewidgets.h"

#include <QPointer>

#include <vector>

class QVBoxLayout;

namespace pdf
{

class PDFLinkRow;
class PDFQuickActionTile;
class PDFRecentFileRow;
class PDFRecentToolRow;
class PDFResourceCard;

/// Page Home /home (pdf4qt-home 28:4): Quick Actions (tiles of the tools of the
/// catalog, Open PDF and All Tools), Recent Files (the five last files with
/// their menus, a missing file "No longer exists", the compact drop zone), Learn
/// & Resources (Video Tutorials, User Manual, Getting Started), Support (Report
/// Issue, Discuss on GitHub, Become a Sponsor) and Recent Tools (the five last
/// used tools of tool_usage). The three columns are below each other in a
/// narrow window.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppHomePage : public PDFAppHomeBasePage
{
    Q_OBJECT

private:
    using BaseClass = PDFAppHomeBasePage;

public:
    /// Recent files and recent tools shown by the page
    static constexpr int MAXIMUM_RECENT_FILES = 5;
    static constexpr int MAXIMUM_RECENT_TOOLS = 5;

    explicit PDFAppHomePage(PDFAppContext* context, QWidget* parent = nullptr);
    virtual ~PDFAppHomePage() override;

    virtual bool setRoute(const PDFAppRoute& route) override;
    virtual QString getTitle() const override;
    virtual QString getHeaderTitle() const override;
    virtual QString getHeaderSubtitle() const override;
    virtual QString getHeaderSubtitleAccent() const override;

    /// Tool ids of the quick actions in their order (Open PDF and All Tools are not tools)
    static QStringList getQuickActionToolIds();

    /// Description of the tile of a tool: the description of the catalog, unless the
    /// tile has a shorter one (Remove Hidden Data: "Strip metadata", 28:4 and
    /// tools.md - the description of the catalog needs four lines in the tile 88 high)
    static QString getQuickActionDescription(const QString& toolId, const QString& catalogDescription);

    // Parts of the page (tests)
    QList<PDFQuickActionTile*> getQuickActionTiles() const;
    QList<PDFRecentFileRow*> getRecentFileRows() const;
    QList<PDFResourceCard*> getResourceCards() const;
    QList<PDFLinkRow*> getSupportRows() const;
    QList<PDFRecentToolRow*> getRecentToolRows() const;
    PDFAppDropZone* getDropZone() const { return m_dropZone; }
    PDFAppColumns* getColumns() const { return m_columns; }
    QWidget* getRecentFilesCard() const { return m_recentFilesCard; }
    QWidget* getRecentToolsCard() const { return m_recentToolsCard; }
    QWidget* getSupportCard() const { return m_supportCard; }
    QWidget* getTilesGrid() const { return m_tiles; }
    PDFLabel* getRecentToolsEmptyLabel() const { return m_recentToolsEmpty; }
    PDFLabel* getRecentFilesEmptyLabel() const { return m_recentFilesEmpty; }

protected:
    virtual void refresh() override;
    virtual bool isTopicRelevant(const QString& topic) const override;
    virtual void onPathInfoChanged(const QString& path) override;

private:
    QWidget* createQuickActions(QWidget* parent);
    QWidget* createRecentFiles(QWidget* parent);
    QWidget* createLearnAndSupport(QWidget* parent);
    QWidget* createRecentTools(QWidget* parent);
    void refreshRecentFiles();
    void refreshRecentTools();
    void updateFileStatus(PDFRecentFileRow* row);

    void onTileClicked();
    void onRecentFileClicked();
    void onRecentToolClicked();

    QPointer<PDFAppColumns> m_columns;
    QPointer<QWidget> m_tiles;
    QList<QPointer<PDFQuickActionTile>> m_quickActionTiles;
    QPointer<QWidget> m_recentFilesCard;
    QPointer<QVBoxLayout> m_recentFilesLayout;
    QList<QPointer<PDFRecentFileRow>> m_recentFileRows;
    QPointer<PDFLabel> m_recentFilesEmpty;
    QPointer<PDFAppDropZone> m_dropZone;
    QList<QPointer<PDFResourceCard>> m_resourceCards;
    QPointer<QWidget> m_supportCard;
    QList<QPointer<PDFLinkRow>> m_supportRows;
    QPointer<QWidget> m_recentToolsCard;
    QPointer<QVBoxLayout> m_recentToolsLayout;
    QList<QPointer<PDFRecentToolRow>> m_recentToolRows;
    QPointer<PDFLabel> m_recentToolsEmpty;
    QString m_recentFilesSignature;
    QString m_recentToolsSignature;
};

/// Factory of the page Home
class PDF4QTLIBAPPSHARED_EXPORT PDFAppHomePageFactory : public PDFAppPageFactory
{
public:
    virtual PDFAppPage* createPage(PDFAppContext* context, QWidget* parent) const override;
};

}   // namespace pdf

#endif // PDFAPPHOMEPAGE_H
