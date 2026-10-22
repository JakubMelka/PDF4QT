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

#ifndef PDFAPPDOCUMENTPANEL_H
#define PDFAPPDOCUMENTPANEL_H

#include "pdfappglobal.h"

#include <QPointer>
#include <QString>
#include <QWidget>

#include <memory>
#include <vector>

namespace pdf
{

class PDFAppDocument;
class PDFAppDocumentView;

/// Ids of the panels of the sidebar (the value of ?panel= of the route
/// /document/{id}); the order of the sidebar tabs is the order of the factories
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentPanelIds
{
public:
    PDFAppDocumentPanelIds() = delete;

    // Package C (navigation panels)
    static QString outline() { return QStringLiteral("outline"); }
    static QString thumbnails() { return QStringLiteral("thumbnails"); }
    static QString layers() { return QStringLiteral("layers"); }
    static QString attachments() { return QStringLiteral("attachments"); }
    static QString bookmarks() { return QStringLiteral("bookmarks"); }
    static QString notes() { return QStringLiteral("notes"); }

    // Package D (search, signatures, speech)
    static QString search() { return QStringLiteral("search"); }
    static QString signatures() { return QStringLiteral("signatures"); }
    static QString speech() { return QStringLiteral("speech"); }
};

/// Panel of the sidebar of a document view (Figma 193:883: tabs 48 px, header 44 px,
/// content). One class per panel; the sidebar creates a panel by its factory the
/// first time it is shown and gives it the document and the view.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentPanel : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAppDocumentPanel(QWidget* parent = nullptr);
    virtual ~PDFAppDocumentPanel() override;

    /// Id (slug of ?panel=, PDFAppDocumentPanelIds)
    virtual QString getId() const = 0;

    /// Title of the header of the panel ("Thumbnails") and its subtitle ("1,592 pages")
    virtual QString getTitle() const = 0;
    virtual QString getSubtitle() const;

    /// Icon of the sidebar tab (PDFThemeIcons)
    virtual QString getIconName() const = 0;

    /// Sets the document and the view (nullptr = none). The base stores them;
    /// a panel overrides it to follow the changes of the document.
    virtual void setDocument(PDFAppDocument* document, PDFAppDocumentView* view);

    PDFAppDocument* getDocument() const;
    PDFAppDocumentView* getView() const;

    /// The panel draws its own header (the search results 198:4520 with previous /
    /// next / close): the sidebar hides its common header (title, subtitle and the
    /// header widget) while the panel is shown. Default: false.
    virtual bool hasOwnHeader() const;

    /// Creates the widget at the trailing side of the common header (Figma
    /// 193:917: the thumbnail size slider and the sync toggle), nullptr = none (the
    /// default). The sidebar calls it once, right after the panel is created and
    /// has got its document; \p parent is the header. The sidebar owns the widget,
    /// shows it only while the panel is shown and deletes it together with the
    /// panel; the panel holds the parts of it in QPointers.
    virtual QWidget* createHeaderWidget(QWidget* parent);

Q_SIGNALS:
    /// The title or the subtitle has changed (the sidebar updates its header)
    void titleChanged();

    /// The content of the panel has appeared or vanished (the sidebar asks
    /// PDFAppDocumentPanelFactory::isAvailable again and shows / hides the tab)
    void availabilityChanged();

private:
    QPointer<PDFAppDocument> m_document;
    QPointer<PDFAppDocumentView> m_view;
};

/// Factory of a panel (one class per panel, no lambdas)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentPanelFactory
{
public:
    PDFAppDocumentPanelFactory() = default;
    virtual ~PDFAppDocumentPanelFactory();

    PDFAppDocumentPanelFactory(const PDFAppDocumentPanelFactory&) = delete;
    PDFAppDocumentPanelFactory& operator=(const PDFAppDocumentPanelFactory&) = delete;

    /// Id of the panel
    virtual QString getId() const = 0;

    /// Position of the tab in the sidebar (smaller first; Figma order: outline 10,
    /// thumbnails 20, layers 30, attachments 40, speech 50, signatures 60,
    /// bookmarks 70, notes 80)
    virtual int getOrder() const = 0;

    /// The panel has a tab in the sidebar (the search results have none: they are
    /// shown by the search field)
    virtual bool hasSidebarTab() const;

    /// Title of the tab (its tooltip and accessible name, the title of the panel)
    /// and the icon of the tab (PDFThemeIcons), so the tab exists before the panel
    /// is created. Default: empty - the sidebar creates the panel to ask it.
    virtual QString getTitle() const;
    virtual QString getIconName() const;

    /// The panel has content for the document, so its tab is shown (Figma Sidebar
    /// tab 190:619: tabs without content are hidden). The sidebar asks it, when
    /// the document is set, when its state or content changes (stateChanged,
    /// documentChanged, signatureResultsChanged) and when a created panel emits
    /// availabilityChanged. \p document is never nullptr and it is Ready. Default: true.
    virtual bool isAvailable(const PDFAppDocument* document) const;

    /// Creates the panel
    virtual PDFAppDocumentPanel* createPanel(QWidget* parent) const = 0;
};

/// Registry of the panels of the sidebar. The default registry has the panels of
/// package C (registerNavigationPanels, in the files of C) and of package D
/// (registerSearchPanels, in the files of D).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentPanelRegistry
{
public:
    PDFAppDocumentPanelRegistry() = default;

    /// Registry with all panels of step 3
    static PDFAppDocumentPanelRegistry createDefault();

    /// Registers the factory. \returns false for nullptr or an id, which is registered.
    bool registerFactory(std::shared_ptr<const PDFAppDocumentPanelFactory> factory);

    /// Factory of the id, nullptr if there is none
    const PDFAppDocumentPanelFactory* findFactory(const QString& id) const;

    /// Factories sorted by their order
    std::vector<const PDFAppDocumentPanelFactory*> getFactories() const;

    /// Panels of package C: outline, thumbnails, layers, attachments, bookmarks,
    /// notes (defined in pdfappdocumentsidebar.cpp)
    static void registerNavigationPanels(PDFAppDocumentPanelRegistry* registry);

    /// Panels of package D: search results, signatures, speech (defined in
    /// pdfappdocumentsearch.cpp)
    static void registerSearchPanels(PDFAppDocumentPanelRegistry* registry);

private:
    std::vector<std::shared_ptr<const PDFAppDocumentPanelFactory>> m_factories;
};

}   // namespace pdf

#endif // PDFAPPDOCUMENTPANEL_H
