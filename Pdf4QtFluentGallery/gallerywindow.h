#ifndef GALLERYWINDOW_H
#define GALLERYWINDOW_H

#include "gallerypages.h"

#include <QMainWindow>

class QTabWidget;
class QToolButton;

namespace pdf
{
class PDFSegmentedControl;
}

namespace pdffluentgallery
{

/// Main window of the gallery of the widgets drawn by PDFWidgetStyle
class GalleryWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit GalleryWindow(QWidget* parent = nullptr);

    /// Shows the page with the given identifier (or with the given index)
    void setCurrentPage(const QString& id);

    /// Exports the samples of all pages into the directory
    /// \param directory Target directory
    /// \param scale Device pixel ratio of the images
    /// \returns Number of exported files
    int exportPages(const QString& directory, qreal scale);

    const std::vector<GalleryPages::Page>& getPages() const { return m_pages; }

private:
    void updateStatus();

    std::vector<GalleryPages::Page> m_pages;
    QTabWidget* m_tabWidget;
    pdf::PDFSegmentedControl* m_themeControl;
    QToolButton* m_themeItem;
};

}   // namespace pdffluentgallery

#endif // GALLERYWINDOW_H
