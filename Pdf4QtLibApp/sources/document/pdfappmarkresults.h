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

#ifndef PDFAPPMARKRESULTS_H
#define PDFAPPMARKRESULTS_H

#include "pdfappglobal.h"
#include "pdfappannotationoperations.h"

#include "pdfoperationcontrol.h"

#include <QFutureWatcher>
#include <QPointer>
#include <QWidget>

#include <array>
#include <atomic>
#include <map>
#include <memory>

class QPushButton;
class QTimer;
class QVBoxLayout;

namespace pdf
{

class PDFAppDocumentOperation;
class PDFAppDocumentSearchController;
class PDFAppDocumentView;
class PDFAppPanelMenuItem;
class PDFColorSwatchRow;
class PDFLabel;

/// Cancellation and progress of a running Mark as annotations (read by the worker)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMarkResultsControl : public PDFOperationControl
{
public:
    PDFAppMarkResultsControl() = default;

    virtual bool isOperationCancelled() const override { return m_isCanceled.load(std::memory_order_acquire); }

    void cancel() { m_isCanceled.store(true, std::memory_order_release); }

    std::atomic<size_t>* getProcessed() { return &m_processed; }
    size_t getProcessedCount() const { return m_processed.load(std::memory_order_relaxed); }

private:
    std::atomic<bool> m_isCanceled = false;
    std::atomic<size_t> m_processed = 0;
};

/// Result of a worker of Mark as annotations
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMarkResultsOutcome
{
public:
    PDFAppAnnotationResult change;
    size_t count = 0;
};

/// Menu of Mark as annotations (Figma menu-mark 737:9221, 304 wide): Mark as
/// (Highlight, Underline, Squiggly underline, Strikethrough), the color row, the
/// results (All results (N), Results on this page (N), Selected result only) and
/// the primary button "Highlight 24 results". A popover of the style above the
/// footer button of the search panel; Esc and a click outside close it.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMarkResultsMenu : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// Scope of the marked results
    enum class Scope
    {
        All,
        CurrentPage,
        Selected
    };

    static constexpr int PANEL_WIDTH = 304;
    static constexpr int RADIUS = 10;

    explicit PDFAppMarkResultsMenu(QWidget* parent);
    virtual ~PDFAppMarkResultsMenu() override;

    /// Opens the menu above the anchor (below it, when there is no room) with the
    /// type, the color and the counts of the scopes
    void popup(QWidget* anchor, AnnotationType type, const QColor& color, const std::array<int, 3>& counts, Scope scope);

    AnnotationType getType() const { return m_type; }
    QColor getColor() const;
    Scope getScope() const { return m_scope; }

    PDFAppPanelMenuItem* getTypeItem(AnnotationType type) const;
    PDFAppPanelMenuItem* getScopeItem(Scope scope) const;
    PDFColorSwatchRow* getColorRow() const;
    QPushButton* getApplyButton() const;

    /// Rectangle of the panel without the margins of the shadow
    QRect getPanelRect() const;

Q_SIGNALS:
    /// Apply has been clicked (the menu is closed)
    void applyRequested(pdf::AnnotationType type, const QColor& color, pdf::PDFAppMarkResultsMenu::Scope scope);

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

private:
    QWidget* createSection(const char* caption);
    QWidget* createSeparator();
    void retranslate();
    void updateState();
    void onTypeClicked();
    void onScopeClicked();
    void onApplyClicked();

    QPointer<QVBoxLayout> m_layout;
    std::map<AnnotationType, QPointer<PDFAppPanelMenuItem>> m_typeItems;
    std::map<Scope, QPointer<PDFAppPanelMenuItem>> m_scopeItems;
    std::vector<std::pair<QPointer<PDFLabel>, const char*>> m_captions;
    QPointer<PDFColorSwatchRow> m_colorRow;
    QPointer<QPushButton> m_applyButton;
    AnnotationType m_type = AnnotationType::Highlight;
    Scope m_scope = Scope::All;
    std::array<int, 3> m_counts = { 0, 0, 0 };
};

/// Mark as annotations of package A (search panel footer 737:9129, menu 737:9221,
/// result 737:9141): text markups of the results of the search of the tab, one
/// annotation per occurrence, one undo step. Up to SYNCHRONOUS_LIMIT occurrences
/// are marked at once; more are marked by a worker from a snapshot of the
/// document - a blocking operation of the document with its progress and Cancel
/// (the close-running dialog may stop it); the result is committed with the
/// version of the snapshot (refused, when the document has changed meanwhile).
/// The status bar says what has been marked and that Ctrl+Z undoes it.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMarkResults : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    using Scope = PDFAppMarkResultsMenu::Scope;

    /// Most occurrences marked in the GUI thread
    static constexpr size_t SYNCHRONOUS_LIMIT = 1000;

    /// Interval of the updates of the progress of the worker (ms)
    static constexpr int PROGRESS_INTERVAL = 100;

    explicit PDFAppMarkResults(PDFAppDocumentView* view, QObject* parent);
    virtual ~PDFAppMarkResults() override;

    PDFAppDocumentView* getView() const;

    /// Search of the tab (nullptr, when there is none)
    PDFAppDocumentSearchController* getSearchController() const;

    /// Results can be marked: the annotations can be changed, the search has
    /// results of the current version and no marking runs
    bool canMark() const;

    /// Occurrences of the scope (Selected = the current occurrence of the search)
    PDFDocumentTextSearchMatches getMatches(Scope scope) const;
    int getMatchCount(Scope scope) const;

    /// Marks the occurrences of the scope. \returns false, when nothing can be
    /// marked (the worker of a large scope returns true, the result follows).
    bool mark(AnnotationType type, const QColor& color, Scope scope);

    /// A worker runs
    bool isRunning() const;

    /// Stops the worker (nothing is committed)
    void cancel();

    /// Opens the menu at the footer of the search panel (or at the search field)
    void openMenu();
    void openMenu(QWidget* anchor);
    PDFAppMarkResultsMenu* getMenu() const;

    /// The function of the worker
    static PDFAppMarkResultsOutcome run(PDFDocumentPointer document,
                                        PDFDocumentTextSearchMatches matches,
                                        AnnotationType type,
                                        PDFAppNewAnnotation data,
                                        std::shared_ptr<PDFAppMarkResultsControl> control);

Q_SIGNALS:
    /// canMark / isRunning may have changed
    void stateChanged();

    /// A marking has ended (\p count annotations committed; 0 = nothing - canceled,
    /// refused, failed)
    void finished(int count);

private:
    void finishMarking(const PDFAppAnnotationResult& change, size_t count, AnnotationType type, quint64 expectedVersion);
    void onWorkerFinished();
    void onOperationCancelRequested();
    void onProgressTimer();
    void onApplyRequested(pdf::AnnotationType type, const QColor& color, pdf::PDFAppMarkResultsMenu::Scope scope);
    void showStatusMessage(const QString& text);
    void connectSearch();

    using Watcher = QFutureWatcher<PDFAppMarkResultsOutcome>;

    QPointer<PDFAppDocumentView> m_view;
    QPointer<PDFAppDocumentSearchController> m_searchController;
    QPointer<PDFAppMarkResultsMenu> m_menu;
    QPointer<Watcher> m_watcher;
    QPointer<PDFAppDocumentOperation> m_operation;
    QPointer<QTimer> m_progressTimer;
    std::shared_ptr<PDFAppMarkResultsControl> m_control;
    AnnotationType m_runningType = AnnotationType::Highlight;
    quint64 m_runningVersion = 0;
    size_t m_runningTotal = 0;
};

/// Footer of the search panel (737:9129, 299 x 59): the button "Mark as
/// annotations…" opening the menu of Mark as annotations. Shown, when the search
/// has results; disabled with the reason as the tooltip for a locked document.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMarkResultsFooter : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAppMarkResultsFooter(QWidget* parent);
    virtual ~PDFAppMarkResultsFooter() override;

    /// View of the search panel (the footer finds the search and Mark as annotations of its tab)
    void setView(PDFAppDocumentView* view);

    QPushButton* getButton() const;

    /// Reads the state of the search and of the lock again
    void updateState();

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

private:
    void retranslate();
    void onButtonClicked();
    void onRetryQueued();
    PDFAppMarkResults* getMarkResults() const;

    QPointer<PDFAppDocumentView> m_view;
    QPointer<QPushButton> m_button;
    std::vector<QMetaObject::Connection> m_connections;
    bool m_isRetryPending = false;
};

}   // namespace pdf

Q_DECLARE_METATYPE(pdf::PDFAppMarkResultsMenu::Scope)

#endif // PDFAPPMARKRESULTS_H
