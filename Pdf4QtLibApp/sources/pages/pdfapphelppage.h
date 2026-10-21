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

#ifndef PDFAPPHELPPAGE_H
#define PDFAPPHELPPAGE_H

#include "pdfappglobal.h"
#include "pdfapppagelayout.h"
#include "pdfcomponentbuttons.h"
#include "pdfapphelpcatalog.h"
#include "pdfappmarkdownview.h"

#include "pdfcomponentrows.h"
#include "pdffluentwidgets.h"

#include <QModelIndex>
#include <QPointer>

class QStandardItemModel;

namespace pdf
{

/// Contents of the manual (a tree with the rows Outline, which does not scroll:
/// its size hint is the height of all visible rows)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppHelpContentsView : public PDFTreeView
{
    Q_OBJECT

private:
    using BaseClass = PDFTreeView;

public:
    explicit PDFAppHelpContentsView(QWidget* parent = nullptr);

    /// Height of the visible rows with the margins of the view
    int getContentHeight() const;

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

    /// Position of the last press of the mouse in the viewport (a click on the
    /// branch indicator is handled by the tree, a click on the text by the page)
    const QPoint& getLastPressPosition() const { return m_lastPressPosition; }

protected:
    virtual void mousePressEvent(QMouseEvent* event) override;

private:
    void onExpandedChanged();

    QPoint m_lastPressPosition;
};

/// Page of the user manual (/help/{topic}, Figma pdf4qt-help 955:152873): the
/// breadcrumb, the topic in the Markdown view, the contents of the manual (a tree
/// of the categories and their topics, the current topic selected) and the card
/// Related (links of the topic and the next topic of its category). An anchor of
/// the route (/help/{topic}#anchor) scrolls to its heading. An unknown topic is
/// Page Not Found.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppHelpPage : public PDFAppContentPage
{
    Q_OBJECT

private:
    using BaseClass = PDFAppContentPage;

public:
    explicit PDFAppHelpPage(PDFAppContext* context, QWidget* parent = nullptr);
    virtual ~PDFAppHelpPage() override;

    /// Role of the id of the topic in the model of the contents
    static constexpr int TopicRole = Qt::UserRole + 1;

    /// Replaces the catalog (tests); the route is shown again
    void setCatalog(const PDFAppHelpCatalog& catalog);
    const PDFAppHelpCatalog& getCatalog() const { return m_catalog; }

    virtual bool setRoute(const PDFAppRoute& route) override;
    virtual QString getTitle() const override;
    virtual QString getHeaderTitle() const override;
    virtual QString getHeaderSubtitle() const override;
    virtual QList<PDFAppBreadcrumbItem> getHeaderBreadcrumbs() const override;

    const PDFAppHelpTopic& getTopic() const { return m_topic; }
    PDFAppMarkdownView* getMarkdownView() const { return m_markdownView; }
    PDFAppHelpContentsView* getContentsView() const { return m_contentsView; }
    QList<PDFLinkRow*> getRelatedRows() const;

    /// Link target of a row of Related
    static QString getRelatedTarget(const PDFLinkRow* row);

protected:
    virtual bool scrollToAnchor(const QString& anchor) override;
    virtual void changeEvent(QEvent* event) override;

private:
    void build();
    QWidget* createContents(QWidget* parent);
    QWidget* createRelated(QWidget* parent);
    void onMarkdownLinkActivated(const QString& target, pdf::PDFAppMarkdownLinkKind kind);
    void onContentsClicked(const QModelIndex& index);
    void onContentsActivated(const QModelIndex& index);
    void onRelatedClicked();

    PDFAppHelpCatalog m_catalog;
    PDFAppHelpTopic m_topic;
    bool m_isNavigating = false;
    QPointer<QWidget> m_content;
    QPointer<PDFAppMarkdownView> m_markdownView;
    QPointer<PDFAppHelpContentsView> m_contentsView;
    QPointer<QStandardItemModel> m_contentsModel;
    QPointer<QWidget> m_relatedSection;
};

/// Factory of the page of the manual
class PDF4QTLIBAPPSHARED_EXPORT PDFAppHelpPageFactory : public PDFAppPageFactory
{
public:
    virtual PDFAppPage* createPage(PDFAppContext* context, QWidget* parent) const override;
};

}   // namespace pdf

#endif // PDFAPPHELPPAGE_H
