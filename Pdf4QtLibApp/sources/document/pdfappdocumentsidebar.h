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

#ifndef PDFAPPDOCUMENTSIDEBAR_H
#define PDFAPPDOCUMENTSIDEBAR_H

#include "pdfappglobal.h"
#include "pdfappdocumentpanel.h"

#include "pdfglobal.h"

#include <QPointer>
#include <QWidget>

#include <map>
#include <vector>

class QButtonGroup;
class QHBoxLayout;
class QStackedWidget;
class QToolButton;

namespace pdf
{

class PDFAppDocument;
class PDFAppDocumentView;
class PDFAppHeaderText;

/// Sidebar of the document view (Figma sidebar 193:883, 300 px with the splitter
/// 266:7854 of the view): bg/sidebar with the border/default line at its trailing
/// edge, the row of the Sidebar tabs (190:619, 32 px with the icon 18, gaps 2,
/// paddings 10 / 12 / 6 - the size follows the setting viewing.sidebarIconSize),
/// the common header (193:917: title Semi Bold 14 text/primary, subtitle Regular
/// 11 text/muted, the header widget of the panel; paddings 4 / 12 / 8 / 16, at
/// least 44 high) and the shown panel.
///
/// A panel is created by its factory, when it is shown first. The tab of a panel
/// without content for the document is hidden (PDFAppDocumentPanelFactory::isAvailable,
/// Sidebar tab 190:619); the shown panel losing its content is replaced by the
/// thumbnails. A panel requested before the document is ready (?panel= of the
/// route) is shown, when it becomes ready. Without a request the thumbnails are
/// shown (pdf4qt-document 193:756).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentSidebar : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// Default width of the sidebar (with its border)
    static constexpr int DEFAULT_WIDTH = 300;

    /// Geometry of the tabs and of the header (193:884, 193:917)
    static constexpr int TABS_PADDING_TOP = 10;
    static constexpr int TABS_PADDING_SIDE = 12;
    static constexpr int TABS_PADDING_BOTTOM = 6;
    static constexpr int TABS_GAP = 2;
    static constexpr int HEADER_MINIMUM_HEIGHT = 44;
    static constexpr int HEADER_PADDING_LEFT = 16;
    static constexpr int HEADER_PADDING_RIGHT = 12;
    static constexpr int HEADER_PADDING_TOP = 4;
    static constexpr int HEADER_PADDING_BOTTOM = 8;
    static constexpr int HEADER_GAP = 8;

    explicit PDFAppDocumentSidebar(PDFAppDocumentView* view, QWidget* parent = nullptr);
    virtual ~PDFAppDocumentSidebar() override;

    /// Sets the document of the panels
    void setDocument(PDFAppDocument* document);

    /// Shows the panel. \returns false for an unknown panel and for a panel
    /// without content for the ready document (its tab is hidden).
    bool showPanel(const QString& panelId);

    /// Id of the shown panel (empty = none)
    QString getCurrentPanelId() const;

    /// Panel of the id, which has been created (nullptr, if it is not created yet)
    PDFAppDocumentPanel* findPanel(const QString& panelId) const;

    /// Pages selected in the thumbnails (sorted, page indices from 0); empty,
    /// when the thumbnails are not created or nothing is selected
    std::vector<PDFInteger> getSelectedPages() const;

    /// Tab button of the panel (nullptr for an unknown panel or a panel without a tab)
    QToolButton* getTabButton(const QString& panelId) const;

    /// The tab of the panel is shown (the panel has content for the document)
    bool isTabVisible(const QString& panelId) const;

    /// Header widget created for the panel (nullptr = none / not created)
    QWidget* getHeaderWidget(const QString& panelId) const;

    /// The common header and its texts (tests)
    QWidget* getHeader() const;
    QString getHeaderTitle() const;
    QString getHeaderSubtitle() const;

    const PDFAppDocumentPanelRegistry& getRegistry() const { return m_registry; }

Q_SIGNALS:
    /// The shown panel has changed
    void currentPanelChanged(const QString& panelId);

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

private:
    struct TabInfo
    {
        QString panelId;
        const PDFAppDocumentPanelFactory* factory = nullptr;
        QPointer<QToolButton> button;
    };

    PDFAppDocumentPanel* createPanel(const QString& panelId);
    void onTabClicked(int buttonId);
    void onPanelTitleChanged();
    void onPanelAvailabilityChanged();
    void onPanelDestroyed(QObject* object);
    void onDocumentStateChanged();
    void onDocumentContentChanged();
    void onDocumentDestroyed();
    void onSettingChanged(const QString& topic, const QString& key);
    void onEverythingChanged();
    void updateTabs();
    void updateTabSizes();
    void updateTabTexts();
    void updateHeader();
    void showFirstAvailablePanel();
    bool isPanelAvailable(const QString& panelId) const;
    QString getDefaultPanelId() const;

    QPointer<PDFAppDocumentView> m_view;
    QPointer<PDFAppDocument> m_document;
    QPointer<QWidget> m_tabsBar;
    QPointer<QButtonGroup> m_tabGroup;
    QPointer<QWidget> m_header;
    QPointer<QHBoxLayout> m_headerLayout;
    QPointer<PDFAppHeaderText> m_headerTitle;
    QPointer<PDFAppHeaderText> m_headerSubtitle;
    QPointer<QStackedWidget> m_stack;
    PDFAppDocumentPanelRegistry m_registry;
    std::vector<TabInfo> m_tabs;
    std::map<QString, QPointer<PDFAppDocumentPanel>> m_panels;
    std::map<QString, QPointer<QWidget>> m_headerWidgets;
    QString m_currentPanelId;
    QString m_requestedPanelId;    ///< Panel requested, before the document was ready
};

}   // namespace pdf

#endif // PDFAPPDOCUMENTSIDEBAR_H
