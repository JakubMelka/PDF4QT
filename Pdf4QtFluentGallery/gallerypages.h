#ifndef GALLERYPAGES_H
#define GALLERYPAGES_H

#include <QString>
#include <QStyle>
#include <QWidget>

#include <functional>
#include <vector>

class QBoxLayout;
class QJsonArray;
class QLayout;
class QStyleOption;
class QVBoxLayout;

namespace pdffluentgallery
{

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
};

Q_DECLARE_OPERATORS_FOR_FLAGS(GalleryPages::StateFlags)

}   // namespace pdffluentgallery

#endif // GALLERYPAGES_H
