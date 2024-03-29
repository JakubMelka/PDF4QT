//    Copyright (C) 2024 Jakub Melka
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

#include "launchapplicationwidget.h"
#include "ui_launchapplicationwidget.h"

LaunchApplicationWidget::LaunchApplicationWidget(QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::LaunchApplicationWidget)
{
    ui->setupUi(this);

    setFixedSize(4 * 128, 128);
}

LaunchApplicationWidget::~LaunchApplicationWidget()
{
    delete ui;
}

QPushButton* LaunchApplicationWidget::getButton() const
{
    return ui->pushButton;
}

QLabel* LaunchApplicationWidget::getTitleLabel() const
{
    return ui->titleLabel;
}

QLabel* LaunchApplicationWidget::getDescriptionLabel() const
{
    return ui->descriptionLabel;
}
