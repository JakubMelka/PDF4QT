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

#ifndef GALLERYPAGES_H
#define GALLERYPAGES_H

#include <QString>
#include <QStyle>
#include <QMetaType>
#include <QWidget>

#include <functional>
#include <vector>

class QBoxLayout;
class QHBoxLayout;
class QJsonArray;
class QLayout;
class QStyleOption;
class QVBoxLayout;

namespace pdffluentgallery
{

class GalleryBuilder;

/// Hints of a live sample for the lifetime test (UnitTestsFluentLifetime): what
/// an application does with the widget, which the generic input of the test
/// cannot do. A new widget gets them by its sample (CLAUDE.md, the lifetime rule).
struct GalleryLifetimeHints
{
    /// Called on each new instance of the page before the test (an application
    /// gives the widget its menus, its models, ...)
    std::function<void(QWidget* widget)> prepare;

    /// Starts the timers and the states of the widget, which run while the page
    /// is deleted (a timed state, an inline editor)
    std::function<void(QWidget* widget)> startTimers;

    /// Sets the hints of the widget (a dynamic property of the widget)
    static void set(QWidget* widget, GalleryLifetimeHints hints);

    /// Returns the hints of the widget (empty functions, if it has none)
    static GalleryLifetimeHints get(const QWidget* widget);

    /// Gives a menu to each "more" button of the library in the widget, which
    /// has none (every second menu a child of the button, the others children
    /// of the row or of the card of the button) - the hint of every live sample
    static void giveMenus(QWidget* widget);
};

/// Sample of a control drawn by the style with a state composed by hand, so
/// states, which a real widget shows only under the pointer or with the focus,
/// can be seen and exported side by side as the design shows them.
class GallerySpecimen : public QWidget
{
    Q_OBJECT

public:
    using PaintFunction = std::function<void(QPainter& painter, const QRect& rect)>;

    /// Margin around the sample, where a focus ring outside of the shape is drawn
    static constexpr int MARGIN = 4;

    /// \param name Name of the sample (state of the control)
    /// \param figmaNode Identifier of the node of the design, which the sample shows
    /// \param size Size of the control
    /// \param paintFunction Function, which draws the control into the rectangle
    /// \param parent Parent widget
    explicit GallerySpecimen(QString name, QString figmaNode, QSize size, PaintFunction paintFunction, QWidget* parent);

    const QString& getName() const { return m_name; }
    const QString& getFigmaNode() const { return m_figmaNode; }
    QSize getControlSize() const { return m_size; }

    /// Draws the sample
    void paintSpecimen(QPainter& painter) const;

protected:
    virtual void paintEvent(QPaintEvent* event) override;

private:
    QString m_name;
    QString m_figmaNode;
    QSize m_size;
    PaintFunction m_paintFunction;
};

/// Pages of the gallery, one for each section of the page "Widgets" of the design
class GalleryPages
{
public:
    GalleryPages() = delete;

    struct Page
    {
        QString id;         ///< Identifier used in names of exported files ("01-push-buttons")
        QString title;      ///< Title of the tab
        QString iconName;   ///< Icon of the tab
        QWidget* widget = nullptr;
    };

    /// Flags of the state of a sample
    enum StateFlag
    {
        Default     = 0x0000,
        Hover       = 0x0001,
        Pressed     = 0x0002,
        Focus       = 0x0004,
        Disabled    = 0x0008,
        On          = 0x0010,
        Selected    = 0x0020,
        Partial     = 0x0040,
        Inactive    = 0x0080
    };
    Q_DECLARE_FLAGS(StateFlags, StateFlag)

    /// Creates all pages
    static std::vector<Page> createPages(QWidget* parent);

    /// Returns the number of the pages created by createPages
    static int getPageCount();

    /// Returns the identifier of a page (Page::id) without creating the page
    /// \param index Index of the page (0 ... getPageCount() - 1)
    /// \returns Identifier, an empty string for an index out of range
    static QString getPageId(int index);

    /// Creates a single page of createPages (the same widgets as createPages creates)
    /// \param index Index of the page (0 ... getPageCount() - 1)
    /// \param parent Parent widget
    /// \returns Page, an empty page (no widget) for an index out of range
    static Page createPage(int index, QWidget* parent);

    /// Creates the page with widgets of Qt, which draw themselves from the palette.
    /// They are expected to show the leak color.
    static Page createLeakPage(QWidget* parent);

    /// Exports the samples and the live widgets of the page into PNG files
    /// \param page Page
    /// \param directory Target directory
    /// \param theme Identifier of the theme (a part of the file names)
    /// \param scale Device pixel ratio of the images
    /// \param manifest Descriptions of the exported files are appended here
    static void exportPage(const Page& page, const QString& directory, const QString& theme, qreal scale, QJsonArray& manifest);

    /// Renders the widget into an image of the given device pixel ratio
    static QImage renderWidget(QWidget* widget, qreal scale);

    /// Converts the flags of a sample to the state of a style option
    static QStyle::State toStyleState(StateFlags flags);

private:
    using PageFunction = Page (*)(QWidget* parent);

    struct PageEntry
    {
        const char* id;         ///< Identifier of the page (Page::id)
        PageFunction function;  ///< Function creating the page
    };

    /// Pages in the order of the tabs of the gallery
    static const std::vector<PageEntry>& getPageEntries();

    static Page createOverviewPage(QWidget* parent);
    static Page createButtonsPage(QWidget* parent);
    static Page createToolButtonsPage(QWidget* parent);
    static Page createCheckBoxesPage(QWidget* parent);
    static Page createTextInputPage(QWidget* parent);
    static Page createSpinBoxesPage(QWidget* parent);
    static Page createComboBoxesPage(QWidget* parent);
    static Page createSlidersPage(QWidget* parent);
    static Page createProgressBarsPage(QWidget* parent);
    static Page createScrollBarsPage(QWidget* parent);
    static Page createMenusPage(QWidget* parent);
    static Page createTabsPage(QWidget* parent);
    static Page createItemViewsPage(QWidget* parent);
    static Page createHeadersPage(QWidget* parent);
    static Page createGroupBoxesPage(QWidget* parent);
    static Page createContainersPage(QWidget* parent);
    static Page createDialogsPage(QWidget* parent);
    static Page createIconsPage(QWidget* parent);
    static Page createComponentsPage(QWidget* parent);

    // Pages of the components of the design (gallerycomponentpages.cpp), one for
    // each chapter of the specification of the components
    static Page createShellHomePage(QWidget* parent);
    static Page createCatalogPage(QWidget* parent);
    static Page createFormsPage(QWidget* parent);
    static Page createDocumentPage(QWidget* parent);
    static Page createPanelsPage(QWidget* parent);

    // Tiles, cards and pictures of the components (gallerycardpages.cpp), added
    // to the pages of their chapters, and the sample of the Home screen
    static void addShellHomeCards(GalleryBuilder& builder);
    static void addCatalogCards(GalleryBuilder& builder);
    static void addPanelsCards(GalleryBuilder& builder);
    static QWidget* createHomeSample(QWidget* parent);

    // Composite widgets of the components (gallerypanelpages.cpp), added to the
    // pages of their chapters
    static void addCatalogPanels(GalleryBuilder& builder);
    static void addFormsPanels(GalleryBuilder& builder);
    static void addPanelsPanels(GalleryBuilder& builder);

    // Delegates of the views (gallerydelegatepages.cpp), added to the pages of their chapters
    static void addShellHomeDelegates(GalleryBuilder& builder);
    static void addFormsDelegates(GalleryBuilder& builder);
    static void addDocumentDelegates(GalleryBuilder& builder);
    static void addPanelsDelegates(GalleryBuilder& builder);
};

Q_DECLARE_OPERATORS_FOR_FLAGS(GalleryPages::StateFlags)

/// Builds a page of the gallery: blocks with rows of samples and of live widgets
class GalleryBuilder
{
public:
    explicit GalleryBuilder(QWidget* parent, QString id, QString title, QString iconName);

    QWidget* page() const { return m_page; }
    QStyle* style() const;

    /// Starts a block of samples
    void block(const QString& title, const QString& figmaNode = QString());

    /// Starts a new row of the block
    void row();

    /// Adds a sample drawn by the style
    GallerySpecimen* specimen(const QString& caption, QSize size, GallerySpecimen::PaintFunction paintFunction, const QString& figmaNode = QString());

    /// Adds a real widget (the "more" buttons in it get menus in the lifetime test)
    template<typename Widget>
    Widget* live(const QString& caption, Widget* widget, const QString& figmaNode = QString())
    {
        widget->setProperty("galleryName", caption);
        widget->setProperty("galleryNode", figmaNode.isEmpty() ? m_blockNode : figmaNode);
        addLiveDefaults(widget);
        addItem(widget, caption, 6);
        return widget;
    }

    /// Adds a widget, which fills the whole width of the page
    void wide(QWidget* widget, const QString& caption = QString());

    /// Adds a paragraph of text
    void note(const QString& text);

    GalleryPages::Page finish();

private:
    void addItem(QWidget* widget, const QString& caption, int gap);

    /// The default lifetime hints of a live sample (GalleryLifetimeHints::giveMenus)
    static void addLiveDefaults(QWidget* widget);

    QString m_id;
    QString m_title;
    QString m_iconName;
    QString m_blockNode;
    QWidget* m_page;
    QVBoxLayout* m_pageLayout;
    QHBoxLayout* m_rowLayout = nullptr;
    int m_countInRow = 0;
};

}   // namespace pdffluentgallery

Q_DECLARE_METATYPE(pdffluentgallery::GalleryLifetimeHints)

#endif // GALLERYPAGES_H
