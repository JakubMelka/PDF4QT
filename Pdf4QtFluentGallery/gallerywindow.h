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
