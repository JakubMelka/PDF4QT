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

#ifndef PDFAPPPAGENOTFOUND_H
#define PDFAPPPAGENOTFOUND_H

#include "pdfappglobal.h"
#include "pdfapppage.h"

#include "pdffluentwidgets.h"

#include <QKeySequence>
#include <QPointer>
#include <QPushButton>
#include <QWidget>

namespace pdf
{

/// Key cap of a hint (kbd of pdf4qt-not-found 940:134845): a box bg/subtle with
/// the border border/default, radius 4, paddings 6 / 2, the key Medium 10 text/secondary
class PDF4QTLIBAPPSHARED_EXPORT PDFAppKeyCap : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAppKeyCap(const QString& text, QWidget* parent = nullptr);

    const QString& getText() const { return m_text; }

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

    /// Names of the keys of the first combination of the shortcut ("Alt", "←";
    /// "Ctrl", "K"), empty for an empty shortcut
    static QStringList getKeyNames(const QKeySequence& shortcut);

protected:
    virtual void paintEvent(QPaintEvent* event) override;

private:
    QString m_text;
};

/// What Page Not Found shows (filled by the navigator)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppNotFoundRequest
{
public:
    /// The requested route (also an invalid one)
    PDFAppRoute route;

    /// What the user has looked for ("Tool “convert.epub”")
    QString description;

    /// Text of the search offered by the page (empty = no search button)
    QString searchText;

    /// The history of the tab has a previous entry
    bool canGoBack = false;

    /// Shortcuts of Back and of the global search (hints below the buttons)
    QKeySequence backShortcut;
    QKeySequence searchShortcut;
};

/// Page Not Found (pdf4qt-not-found 940:134845): a route, which is not known,
/// has an unknown parameter or whose page is not implemented yet. The page says
/// what the user has looked for (the path as a secondary text) and offers Back
/// (history of the tab), Search for … (global search) and Go to Home. It never
/// is an empty window.
class PDF4QTLIBAPPSHARED_EXPORT PDFPageNotFoundPage : public PDFAppPage
{
    Q_OBJECT

private:
    using BaseClass = PDFAppPage;

public:
    /// Maximal width of the texts of the requested page
    static constexpr int MAXIMUM_TEXT_WIDTH = 460;

    explicit PDFPageNotFoundPage(PDFAppContext* context, QWidget* parent = nullptr);
    virtual ~PDFPageNotFoundPage() override;

    /// Shows the request
    void setRequest(const PDFAppNotFoundRequest& request);
    const PDFAppNotFoundRequest& getRequest() const { return m_request; }

    virtual QString getTitle() const override;

    QPushButton* getBackButton() const { return m_backButton; }
    QPushButton* getSearchButton() const { return m_searchButton; }
    QPushButton* getHomeButton() const { return m_homeButton; }

    /// Shown texts of the requested page (elided)
    QString getDescriptionText() const;
    QString getPathText() const;

protected:
    virtual void changeEvent(QEvent* event) override;

private:
    void onBackClicked();
    void onSearchClicked();
    void onHomeClicked();
    void updateContent();
    void updateIcons();
    void addKeyHint(QWidget* parent, const QKeySequence& shortcut, const QString& text, bool isFirst);

    PDFAppNotFoundRequest m_request;
    QPointer<PDFLabel> m_titleLabel;
    QPointer<PDFLabel> m_textLabel;
    QPointer<PDFLabel> m_requestedLabel;
    QPointer<PDFLabel> m_descriptionLabel;
    QPointer<PDFLabel> m_pathLabel;
    QPointer<QPushButton> m_backButton;
    QPointer<QPushButton> m_searchButton;
    QPointer<QPushButton> m_homeButton;
    QPointer<QWidget> m_hints;
};

/// Factory of Page Not Found (the navigator fills the request)
class PDF4QTLIBAPPSHARED_EXPORT PDFPageNotFoundFactory : public PDFAppPageFactory
{
public:
    virtual PDFAppPage* createPage(PDFAppContext* context, QWidget* parent) const override;
};

}   // namespace pdf

#endif // PDFAPPPAGENOTFOUND_H
