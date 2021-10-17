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

#ifndef SETTINGSDOCKWIDGET_H
#define SETTINGSDOCKWIDGET_H

#include "settings.h"

#include <QDockWidget>

namespace Ui
{
class SettingsDockWidget;
}

class QLineEdit;

namespace pdfdocdiff
{

class SettingsDockWidget : public QDockWidget
{
    Q_OBJECT

public:
    explicit SettingsDockWidget(Settings* settings, QWidget* parent);
    virtual ~SettingsDockWidget() override;

    void setCompareTextsAsVectorGraphics(bool enabled);
    bool isCompareTextAsVectorGraphics() const;

    void setCompareTextCharactersInsteadOfWords(bool enabled);
    bool isCompareTextCharactersInsteadOfWords() const;

    QLineEdit* getLeftPageSelectionEdit() const;
    QLineEdit* getRightPageSelectionEdit() const;

    void loadColors();
    void saveColors();

signals:
    void colorsChanged();

private:
    QIcon getIconForColor(QColor color) const;

    void onEditColorChanged();

    Ui::SettingsDockWidget* ui;
    Settings* m_settings;
    bool m_loadingColors;
};

}   // namespace pdfdocdiff

#endif // SETTINGSDOCKWIDGET_H
