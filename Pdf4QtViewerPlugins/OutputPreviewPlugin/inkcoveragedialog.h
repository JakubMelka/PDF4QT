//    Copyright (C) 2021 Jakub Melka
//
//    This file is part of PDF4QT.
//
//    PDF4QT is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    PDF4QT is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDF4QT.  If not, see <https://www.gnu.org/licenses/>.

#ifndef INKCOVERAGEDIALOG_H
#define INKCOVERAGEDIALOG_H

#include "pdfdocument.h"
#include "pdfdrawwidget.h"
#include "pdftransparencyrenderer.h"

#include <QDialog>
#include <QAbstractItemModel>

namespace Ui
{
class InkCoverageDialog;
}

namespace pdfplugin
{
class InkCoverageStatisticsModel;

using ChannelCoverageInfo = pdf::PDFInkCoverageCalculator::InkCoverageChannelInfo;
using CoverageInfo = std::vector<ChannelCoverageInfo>;

struct InkCoverageResults
{
    std::vector<CoverageInfo> pageInfo;
    CoverageInfo sumInfo;
};

class InkCoverageDialog : public QDialog
{
    Q_OBJECT

private:
    using BaseClass = QDialog;

public:
    explicit InkCoverageDialog(const pdf::PDFDocument* document, pdf::PDFWidget* widget, QWidget* parent);
    virtual ~InkCoverageDialog() override;

    void updateInkCoverage();

    virtual void closeEvent(QCloseEvent* event) override;

    virtual void accept() override;
    virtual void reject() override;

private:
    Ui::InkCoverageDialog* ui;

    static constexpr int RESOLUTION = 1920;

    void onInkCoverageCalculated();
    bool isInkCoverageCalculated() const;

    pdf::PDFInkMapper m_inkMapper;
    const pdf::PDFDocument* m_document;
    pdf::PDFWidget* m_widget;
    InkCoverageStatisticsModel* m_model;

    QFuture<InkCoverageResults> m_future;
    QFutureWatcher<InkCoverageResults>* m_futureWatcher;
};

class InkCoverageStatisticsModel : public QAbstractItemModel
{
    Q_OBJECT

private:
    using BaseClass = QAbstractItemModel;

public:
    explicit InkCoverageStatisticsModel(QObject* parent);

    enum EStandardColumn
    {
        StandardColumnPageIndex,
        LastStandardColumn
    };

    enum EChannelColumn
    {
        ChannelColumnColorant,
        ChannelColumnCoverageArea,
        ChannelColumnCoverageRatio,
        LastChannelColumn
    };

    virtual QModelIndex index(int row, int column, const QModelIndex& parent) const override;
    virtual QModelIndex parent(const QModelIndex& child) const override;
    virtual int rowCount(const QModelIndex& parent) const override;
    virtual int columnCount(const QModelIndex& parent) const override;
    virtual QVariant data(const QModelIndex& index, int role) const override;
    virtual QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    void setInkCoverageResults(InkCoverageResults inkCoverageResults);
    int getChannelCount() const;
    EChannelColumn getChannelColumn(int index) const;
    int getChannelIndex(int index) const;

private:
    InkCoverageResults m_inkCoverageResults;
};

}   // namespace pdfplugin

#endif // INKCOVERAGEDIALOG_H
