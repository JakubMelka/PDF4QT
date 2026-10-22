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

#ifndef PDFAPPSUPPORTPAGE_H
#define PDFAPPSUPPORTPAGE_H

#include "pdfappglobal.h"
#include "pdfappinfowidgets.h"
#include "pdfappworkspace.h"
#include "pdfsupportvideocatalog.h"

#include "pdfcomponentcards.h"
#include "pdffluentwidgets.h"

#include <QButtonGroup>
#include <QPointer>
#include <QPushButton>
#include <QSet>

#include <map>

namespace pdf
{

/// Page Support (pdf4qt-support 116:2419): four quick actions (issue, feature,
/// manual, discussion) and the video tutorials of PDFSupportVideoCatalog with
/// the search, the button of the YouTube channel, the topic chips (All, For you
/// and the topics) and the cards. The filter is the state of the route
/// /support?topic=…&q=…: a change of the filter is written into the current
/// entry of the history of the tab (the page is not created again, so the focus
/// stays in the search field or on the chip).
///
/// For you = the videos, whose tools the user has used (table tool_usage). The
/// chip is always shown; without such videos the page explains, why the list is
/// empty. The used tools are read from the database, when the page is shown and
/// after a change notification of tool_usage (also from another instance).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSupportPage : public PDFAppContentPage
{
    Q_OBJECT

private:
    using BaseClass = PDFAppContentPage;

public:
    /// Value of the filter All (no query value)
    static constexpr const char* ALL_TOPIC = "";

    explicit PDFAppSupportPage(PDFAppContext* context, QWidget* parent = nullptr);
    virtual ~PDFAppSupportPage() override;

    virtual bool setRoute(const PDFAppRoute& route) override;
    virtual QString getTitle() const override;
    virtual QString getHeaderTitle() const override;
    virtual QString getHeaderSubtitle() const override;

    /// Route of the filter (empty topic = All, empty text = no search)
    static PDFAppRoute createRoute(const QString& topic, const QString& searchText);

    /// Search text, which can be a part of the route of the topic: control
    /// characters are spaces, lone surrogates are removed, the text is cut to
    /// the longest search and to the longest route
    static QString sanitizeSearchText(const QString& topic, const QString& text);

    /// Current filter
    const QString& getTopic() const { return m_topic; }
    const QString& getSearchText() const { return m_searchText; }

    /// Changes the filter (the route of the history follows it); an unknown
    /// topic is All
    void setFilter(const QString& topic, const QString& searchText);

    /// Tools used by the user, as the page has read them last
    const QSet<QString>& getUsedToolIds() const { return m_usedToolIds; }

    /// Reads the used tools from the database and updates For you
    void refreshUsedTools();

    /// Parts of the page (tests)
    QList<PDFQuickActionTile*> getQuickActions() const;
    PDFLineEdit* getSearchField() const { return m_searchField; }
    QPushButton* getChannelButton() const { return m_channelButton; }
    QPushButton* getChip(const QString& topic) const;
    QList<QPushButton*> getChips() const;
    QList<PDFVideoCard*> getVideoCards() const;
    QList<PDFVideoCard*> getVisibleVideoCards() const;
    PDFVideoCard* getVideoCard(const QString& videoId) const;
    PDFAppParagraph* getEmptyText() const { return m_emptyText; }
    PDFAppGrid* getVideoGrid() const { return m_videoGrid; }
    PDFAppGrid* getQuickActionGrid() const { return m_quickActionGrid; }

protected:
    virtual void showEvent(QShowEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

private:
    /// Creates the quick actions and the tutorials (all texts in the current language)
    void buildContent();

    /// The language has changed: the content is created again; the filter, the text
    /// of the search field, the scroll position and the focused widget are kept
    void rebuild();

    QWidget* createQuickActions(QWidget* parent);
    QWidget* createTutorials(QWidget* parent);
    QPushButton* createChip(const QString& topic, const QString& text, int count, QWidget* parent);

    void applyFilter();
    void updateChips();
    void updateRouteInHistory();

    void onSearchTextEdited(const QString& text);
    void onChipClicked(QAbstractButton* button);
    void onVideoClicked();
    void onTagClicked(const QString& tag);
    void onChannelClicked();
    void onQuickActionClicked();
    void onDatabaseChanged(const QString& topic, const QString& key);
    void onEverythingChanged();

    QString m_topic;
    QString m_searchText;
    QSet<QString> m_usedToolIds;

    QPointer<PDFAppGrid> m_quickActionGrid;
    QList<QPointer<PDFQuickActionTile>> m_quickActions;
    QPointer<PDFLineEdit> m_searchField;
    QPointer<QPushButton> m_channelButton;
    QPointer<QButtonGroup> m_chipGroup;
    std::map<QString, QPointer<QPushButton>> m_chips;
    QPointer<QWidget> m_tutorials;
    QPointer<PDFAppGrid> m_videoGrid;
    std::map<QString, QPointer<PDFVideoCard>> m_videoCards;
    QPointer<PDFAppParagraph> m_emptyText;
};

/// Factory of the page Support
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSupportPageFactory : public PDFAppPageFactory
{
public:
    virtual PDFAppPage* createPage(PDFAppContext* context, QWidget* parent) const override;
};

}   // namespace pdf

#endif // PDFAPPSUPPORTPAGE_H
