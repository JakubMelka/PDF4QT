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

#ifndef PDFAPPRENDERINGISSUES_H
#define PDFAPPRENDERINGISSUES_H

#include "pdfappglobal.h"
#include "pdfcomponentpainter.h"

#include "pdfglobal.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QWidget>

#include <vector>

class QMenu;
class QScrollArea;
class QToolButton;
class QVBoxLayout;

namespace pdf
{

class PDFAppDocument;
class PDFAppDocumentView;
class PDFIconButton;
class PDFLabel;
class PDFTag;

/// One issue of the rendering of a page (PDFRenderError of PDFWidget)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppRenderingIssue
{
public:
    PDFInteger pageIndex = 0;   ///< Page from 0
    QString message;
    PDFComponentPainter::IssueSeverity severity = PDFComponentPainter::IssueSeverity::Warning;
};

/// Content of the popover Rendering issues (198:5950): the header with the
/// number and the close button, the rows of the issues (PDFRenderingIssueRow,
/// scrolled, at most MAXIMUM_ROWS) and the hint. A click on a row (Enter,
/// Space) emits issueActivated; the arrows move between the rows.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppRenderingIssuesPanel : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// Width of the popover of the design (198:5950) and its padding
    static constexpr int WIDTH = 440;
    static constexpr int PADDING = 16;

    /// Most rows shown (the others are summed up below them)
    static constexpr int MAXIMUM_ROWS = 500;

    explicit PDFAppRenderingIssuesPanel(QWidget* parent = nullptr);
    virtual ~PDFAppRenderingIssuesPanel() override;

    /// Shows the issues (the panel copies them)
    void setIssues(const std::vector<PDFAppRenderingIssue>& issues);

    /// Highest height of the list of the issues
    void setMaximumListHeight(int height);

    /// Insets of the menu, which holds the panel (its margins and its frame): the
    /// panel keeps the width and the padding of the popover of the design
    void setOuterInsets(int horizontal, int vertical);

    /// Rows of the issues in their order
    std::vector<QWidget*> getRows() const;

    /// Puts the keyboard focus to the first row (or to the close button)
    void focusFirstRow();

    PDFIconButton* getCloseButton() const;

Q_SIGNALS:
    /// A row has been activated (page from 0)
    void issueActivated(pdf::PDFInteger pageIndex);

    /// The close button has been clicked
    void closeRequested();

protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void onRowClicked();
    void updateListHeight();

    QPointer<PDFLabel> m_title;
    QPointer<PDFTag> m_count;
    QPointer<PDFIconButton> m_closeButton;
    QPointer<QScrollArea> m_scrollArea;
    QPointer<QWidget> m_list;
    QPointer<QVBoxLayout> m_listLayout;
    QPointer<PDFLabel> m_more;
    std::vector<QPointer<QWidget>> m_rows;
    int m_maximumListHeight = 480;
    int m_horizontalInset = 0;
};

/// Rendering issues of a document tab (package F): the item of the status bar
/// ("3 rendering issues", 193:1321, hidden while there is nothing to report) and
/// the popover with the issues (198:5950) above it. The issues are the errors,
/// which PDFWidget collects, while it compiles the pages (only the pages, which
/// have been shown); a new version of the document clears them. A click on an
/// issue shows its page. The controller lives with the command handler of the
/// tab; it never blocks and it survives the deletion of the view, of the
/// document and of its popover in any order.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppRenderingIssues : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFAppRenderingIssues(PDFAppDocumentView* view, PDFAppDocument* document, QObject* parent);
    virtual ~PDFAppRenderingIssues() override;

    /// Issues sorted by their pages (the order of a page is kept)
    std::vector<PDFAppRenderingIssue> getIssues() const;

    /// Number of the issues
    int getIssueCount() const;

    /// Item of the status bar of the view
    QToolButton* getStatusItem() const;

    /// Creates another item of the status bar, which follows the number of the
    /// issues (the status bar of the page Document properties); a click on it
    /// emits statusItemClicked. The caller owns the item.
    QToolButton* createStatusItem(QWidget* parent);

    /// Shows the popover above the item of the status bar (nothing without issues
    /// or without a shown view). \returns true, if it is shown.
    bool showPopover();

    /// Hides the popover
    void hidePopover();

    /// The popover is shown
    bool isPopoverVisible() const;

    /// The popover and its panel (nullptr, when it is not shown)
    QMenu* getPopover() const;
    PDFAppRenderingIssuesPanel* getPanel() const;

    /// Text of the item ("3 rendering issues")
    static QString getStatusText(int count);

    /// Severity of the type of PDFRenderError
    static PDFComponentPainter::IssueSeverity getSeverity(int renderErrorType);

Q_SIGNALS:
    /// The number of the issues has changed
    void issuesChanged();

    /// An additional item of the status bar (createStatusItem) has been clicked
    void statusItemClicked();

private:
    void onRenderingErrorsChanged();
    void onDocumentChanged();
    void onUpdateTimeout();
    void onStatusItemClicked();
    void onAdditionalItemClicked();
    void onIssueActivated(pdf::PDFInteger pageIndex);
    void onPopoverAboutToHide();
    void updateItems();

    QPointer<PDFAppDocumentView> m_view;
    QPointer<PDFAppDocument> m_document;
    QPointer<QToolButton> m_statusItem;
    std::vector<QPointer<QToolButton>> m_additionalItems;
    QPointer<QMenu> m_popover;
    QPointer<PDFAppRenderingIssuesPanel> m_panel;
    QTimer m_updateTimer;
    int m_count = 0;
};

}   // namespace pdf

#endif // PDFAPPRENDERINGISSUES_H
