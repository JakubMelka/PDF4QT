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

#ifndef PDFDIFF_SETTINGSDOCKWIDGET_H
#define PDFDIFF_SETTINGSDOCKWIDGET_H

#include "settings.h"

#include <QDockWidget>

namespace Ui
{
class SettingsDockWidget;
}

class QLineEdit;

namespace pdfdiff
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

    int getTransparencySliderValue() const;

signals:
    void colorsChanged();
    void transparencySliderChanged(int value);

private:
    QIcon getIconForColor(QColor color) const;

    void onEditColorChanged();

    Ui::SettingsDockWidget* ui;
    Settings* m_settings;
    bool m_loadingColors;
};

}   // namespace pdfdiff

#endif // PDFDIFF_SETTINGSDOCKWIDGET_H
