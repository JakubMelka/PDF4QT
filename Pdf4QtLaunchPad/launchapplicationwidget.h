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

#ifndef LAUNCHAPPLICATIONWIDGET_H
#define LAUNCHAPPLICATIONWIDGET_H

#include <QWidget>

class QLabel;
class QPushButton;

namespace Ui
{
class LaunchApplicationWidget;
}

class LaunchApplicationWidget : public QWidget
{
    Q_OBJECT

public:
    explicit LaunchApplicationWidget(QWidget* parent);
    virtual ~LaunchApplicationWidget() override;

    QPushButton* getButton() const;
    QLabel* getTitleLabel() const;
    QLabel* getDescriptionLabel() const;

private:
    Ui::LaunchApplicationWidget* ui;
};

#endif // LAUNCHAPPLICATIONWIDGET_H
