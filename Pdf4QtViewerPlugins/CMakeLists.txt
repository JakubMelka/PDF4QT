#    Copyright (C) 2022 Jakub Melka
#
#    This file is part of PDF4QT.
#
#    PDF4QT is free software: you can redistribute it and/or modify
#    it under the terms of the GNU Lesser General Public License as published by
#    the Free Software Foundation, either version 3 of the License, or
#    with the written consent of the copyright owner, any later version.
#
#    PDF4QT is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU Lesser General Public License for more details.
#
#    You should have received a copy of the GNU Lesser General Public License
#    along with PDF4QT.  If not, see <https://www.gnu.org/licenses/>.

if(PDF4QT_LINUX)
    set(PDF4QT_PLUGINS_DIR lib/pdf4qt)
else()
    set(PDF4QT_PLUGINS_DIR bin/pdfplugins)
endif()

add_subdirectory(AudioBookPlugin)
add_subdirectory(DimensionsPlugin)
add_subdirectory(ObjectInspectorPlugin)
add_subdirectory(OutputPreviewPlugin)
add_subdirectory(RedactPlugin)
add_subdirectory(SignaturePlugin)
add_subdirectory(SoftProofingPlugin)
