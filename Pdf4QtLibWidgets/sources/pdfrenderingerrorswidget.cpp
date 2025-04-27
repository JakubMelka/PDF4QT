// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
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

#include "pdfrenderingerrorswidget.h"
#include "pdfdrawwidget.h"
#include "ui_pdfrenderingerrorswidget.h"

#include "pdfwidgetutils.h"
#include "pdfdbgheap.h"

namespace pdf
{

PDFRenderingErrorsWidget::PDFRenderingErrorsWidget(QWidget* parent, PDFWidget* pdfWidget) :
    QDialog(parent),
    ui(new Ui::PDFRenderingErrorsWidget)
{
    ui->setupUi(this);

    ui->renderErrorsTreeWidget->setColumnCount(3);
    ui->renderErrorsTreeWidget->setColumnWidth(0, 100);
    ui->renderErrorsTreeWidget->setColumnWidth(1, 300);
    ui->renderErrorsTreeWidget->setHeaderLabels({ tr("Page"), tr("Error type"), tr("Description") });

    Q_ASSERT(pdfWidget);
    std::vector<PDFInteger> currentPages = pdfWidget->getDrawWidget()->getCurrentPages();
    std::sort(currentPages.begin(), currentPages.end());

    QTreeWidgetItem* scrollToItem = nullptr;
    const PDFWidget::PageRenderingErrors* pageRenderingErrors = pdfWidget->getPageRenderingErrors();
    for (const auto& pageRenderingError : *pageRenderingErrors)
    {
        const PDFInteger pageIndex = pageRenderingError.first;
        QTreeWidgetItem* root = new QTreeWidgetItem(ui->renderErrorsTreeWidget, QStringList() << QString::number(pageIndex + 1) << QString() << QString());

        for (const PDFRenderError& error : pageRenderingError.second)
        {
            QString typeString;
            switch (error.type)
            {
                case RenderErrorType::Error:
                {
                    typeString = tr("Error");
                    break;
                }

                case RenderErrorType::Warning:
                {
                    typeString = tr("Warning");
                    break;
                }

                case RenderErrorType::NotImplemented:
                {
                    typeString = tr("Not implemented");
                    break;
                }

                case RenderErrorType::NotSupported:
                {
                    typeString = tr("Not supported");
                    break;
                }

                default:
                {
                    Q_ASSERT(false);
                    break;
                }
            }

            new QTreeWidgetItem(root, QStringList() << QString() << typeString << error.message);
        }

        bool isCurrentPage = std::binary_search(currentPages.cbegin(), currentPages.cend(), pageIndex);
        root->setExpanded(isCurrentPage);

        if (isCurrentPage && !scrollToItem)
        {
            scrollToItem = root;
        }
    }

    if (scrollToItem)
    {
        ui->renderErrorsTreeWidget->scrollToItem(scrollToItem, QAbstractItemView::EnsureVisible);
    }

    pdf::PDFWidgetUtils::style(this);
}

PDFRenderingErrorsWidget::~PDFRenderingErrorsWidget()
{
    delete ui;
}

}   // namespace pdf
