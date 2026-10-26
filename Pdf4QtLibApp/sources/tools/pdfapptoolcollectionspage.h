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

#ifndef PDFAPPTOOLCOLLECTIONSPAGE_H
#define PDFAPPTOOLCOLLECTIONSPAGE_H

#include "pdfappglobal.h"
#include "pdfapppagelayout.h"
#include "pdfapptoolcollections.h"
#include "pdfapphomedialogs.h"
#include "pdfapptoolcollectioneditor.h"

#include "pdfcomponentrows.h"
#include "pdfdocumenttoolsuggestions.h"

#include <QPointer>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <map>
#include <vector>

class QPushButton;
class QVBoxLayout;

namespace pdf
{

class PDFAppCardDialog;
class PDFAppToolCollectionEditor;

/// Collection row of the list of the collections page (283:2346): the selected
/// collection is drawn in the state Pressed of the design (bg/pressed, 283:2369)
/// and it is checked for assistive technologies
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolCollectionListRow : public PDFCollectionRow
{
    Q_OBJECT

private:
    using BaseClass = PDFCollectionRow;

public:
    explicit PDFAppToolCollectionListRow(QWidget* parent = nullptr);
    virtual ~PDFAppToolCollectionListRow() override;

    /// Route id of the collection of the row
    const QString& getRouteId() const { return m_routeId; }
    void setRouteId(const QString& routeId) { m_routeId = routeId; }

protected:
    virtual void paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const override;

private:
    QString m_routeId;
};

/// Confirmation of Delete collection (the pattern of Remove certificates): Delete
/// Collection (Secondary, leading) and the focused Cancel (Primary). Its texts are
/// translated again, when the language changes while it is open (review RC6-2).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDeleteCollectionDialog : public PDFAppCardDialog
{
    Q_OBJECT

private:
    using BaseClass = PDFAppCardDialog;

public:
    static constexpr int CARD_WIDTH = 440;

    /// \param name Name of the collection (data, shown as it is)
    explicit PDFAppDeleteCollectionDialog(const QString& name, QWidget* parent);
    virtual ~PDFAppDeleteCollectionDialog() override;

    QPushButton* getDeleteButton() const { return m_deleteButton; }
    QPushButton* getCancelButton() const { return m_cancelButton; }

protected:
    virtual void changeEvent(QEvent* event) override;

private:
    void updateTexts();

    QString m_name;
    QPointer<QPushButton> m_deleteButton;
    QPointer<QPushButton> m_cancelButton;
};

/// The collections page /tools/collections/{collectionId} and
/// /tools/collections/new[?tool=] (Figma Manage collections 283:1888, plan of
/// step 6 4.4): the list of the collections (left, 340: built-in and custom
/// collections, New collection) and the editor of the shown collection
/// (PDFAppToolCollectionEditor). /tools/collections is redirected to the first
/// collection by the route table. A click on a collection is a new entry of the
/// history (Push); the draft of the editor is kept in the view state of the
/// history entry (owner decision Q4: Back returns to it, no question when the
/// page is left). Save writes the draft in one transaction (a new collection
/// replaces the route by its own one), Delete asks first and goes to the list.
/// Everything is read from the database and follows the changes of another
/// instance (one refresh per turn of the event loop).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolCollectionsPage : public PDFAppContentPage
{
    Q_OBJECT

private:
    using BaseClass = PDFAppContentPage;

public:
    /// Width of the list of the collections (283:2341)
    static constexpr int LIST_WIDTH = 340;

    /// Narrowest editor beside the list
    static constexpr int MINIMUM_EDITOR_WIDTH = 480;

    /// Keys of the view state of the history entry (the draft, the search of Add a tool)
    static QString getDraftKey() { return QStringLiteral("collectionDraft"); }
    static QString getSearchKey() { return QStringLiteral("collectionSearch"); }

    explicit PDFAppToolCollectionsPage(PDFAppContext* context, QWidget* parent = nullptr);
    virtual ~PDFAppToolCollectionsPage() override;

    virtual bool setRoute(const PDFAppRoute& route) override;
    virtual QString getTitle() const override;
    virtual QString getHeaderTitle() const override;
    virtual QString getHeaderSubtitle() const override;
    virtual QList<PDFAppBreadcrumbItem> getHeaderBreadcrumbs() const override;
    virtual PDFAppHeaderIcon getHeaderIcon() const override;
    virtual bool hasSearchHeader() const override;
    virtual QVariantMap saveViewState() const override;
    virtual void restoreViewState(const QVariantMap& state) override;

    /// Route id of the shown collection (empty for a new collection)
    const QString& getCollectionId() const { return m_collectionId; }

    /// The page shows the editor of a new collection
    bool isNewCollection() const { return m_isNewCollection; }

    /// Tools of the edited collection (its draft)
    QStringList getToolIds() const;

    /// Parts of the page (tests)
    PDFAppToolCollectionEditor* getEditor() const { return m_editor; }
    PDFAppToolCollectionListRow* findListRow(const QString& routeId) const;
    PDFLabel* getCountHint() const { return m_countHint; }
    QPushButton* getNewButton() const { return m_newButton; }
    PDFAppDeleteCollectionDialog* getDeleteDialog() const { return m_deleteDialog; }
    QWidget* getListColumn() const { return m_listColumn; }

    /// Number of the reads of the database by the page (a notification of a topic,
    /// which the page does not show - a pin, the use of a tool -, reads nothing)
    int getRefreshCount() const { return m_refreshCount; }

    /// Saves the draft (Save collection). \returns false, when it was refused (the
    /// editor shows why) or the page has been deleted by the navigation.
    bool saveCollection();

    /// Asks before the custom collection is deleted (Delete collection)
    bool requestDelete();

    /// Meta text of a collection of the list: "3 tools", "4 tools · edited
    /// yesterday" (the time of the last change of a changed collection)
    static QString getListMetaText(const PDFAppToolCollection& collection, qint64 now);

protected:
    virtual void changeEvent(QEvent* event) override;

private:
    void buildList();
    void refresh();
    void requestRefresh();
    void updateList();
    void updateSuggestionData(bool isForced);
    void retranslate();

    void onDatabaseChanged(const QString& topic, const QString& key);
    void onEverythingChanged();
    void onListRowClicked();
    void onNewClicked();
    void onDraftChanged();
    void onSaveRequested();
    void onDeleteRequested();
    void onLeaveRequested();
    void onToolActivated(const QString& toolId);
    void onDeleteDialogFinished(int result);

    QString m_collectionId;
    bool m_isNewCollection = false;
    std::vector<PDFAppToolCollection> m_collections;
    QStringList m_pairTools;
    std::vector<PDFDocumentToolPair> m_pairs;
    bool m_isSuggestNextEnabled = true;
    bool m_isPairsRefreshNeeded = false;
    bool m_isLanguageRefresh = false;
    bool m_isLeaving = false;

    QPointer<PDFAppColumns> m_columns;
    QPointer<QWidget> m_listColumn;
    QPointer<PDFLabel> m_listTitle;
    QPointer<PDFLabel> m_countHint;
    QPointer<PDFAppCard> m_listCard;
    QPointer<PDFLabel> m_builtinLabel;
    QPointer<PDFLabel> m_customLabel;
    std::map<QString, QPointer<PDFAppToolCollectionListRow>> m_rows;
    QPointer<QPushButton> m_newButton;
    QPointer<PDFAppToolCollectionEditor> m_editor;
    QPointer<PDFAppDeleteCollectionDialog> m_deleteDialog;
    int m_refreshCount = 0;
    qint64 m_pendingDeleteId = 0;
    QString m_pendingDeleteName;        ///< Stored name of the confirmed collection (its id may be reused, review RS6)
    qint64 m_pendingDeleteModified = -1;    ///< Stored time of the change of the confirmed collection
    QTimer m_refreshTimer;
};

/// Factory of the collections page (/tools/collections, /tools/collections/new,
/// /tools/collections/{collectionId})
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolCollectionsPageFactory : public PDFAppPageFactory
{
public:
    virtual PDFAppPage* createPage(PDFAppContext* context, QWidget* parent) const override;
};

}   // namespace pdf

#endif // PDFAPPTOOLCOLLECTIONSPAGE_H
