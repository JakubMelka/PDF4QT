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

#ifndef SETTINGS_H
#define SETTINGS_H

#include <QColor>

namespace pdfdocdiff
{

struct Settings
{
    QString directory;
    QColor colorPageMove = QColor(35, 145, 255);
    QColor colorAdded = QColor(125, 250, 0);
    QColor colorRemoved = QColor(255, 50, 50);
    QColor colorReplaced = QColor(255, 120, 30);
    bool displayDifferences = true;
    bool displayMarkers = true;
};

}   // namespace pdfdocdiff


#endif // SETTINGS_H
