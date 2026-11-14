# MIT License
#
# Copyright (c) 2018-2026 Jakub Melka and Contributors
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

# The speech synthesis of Create Audio Book (step 4 of phase 2, owner answers Q4.1 - Q4.3, R4-14,
# R4-15): Qt TextToSpeech (QTextToSpeech::synthesize gives the PCM in QAudioFormat of Qt
# Multimedia). A separate shared library, as Pdf4QtOcrTesseract: the core library knows only the
# interfaces of pdfspeechsynthesizer.h and pdfaudiofilewriter.h. Pdf4QtLibApp and PdfTool link it.
# The audio books are written as WAV only (owner 14. 11.: no MP3 encoder for now).
find_package(Qt6 REQUIRED COMPONENTS Core TextToSpeech Multimedia)

add_library(Pdf4QtSpeech SHARED
    pdfqtspeechsynthesizer.h
    pdfqtspeechsynthesizer.cpp
)

include(GenerateExportHeader)

GENERATE_EXPORT_HEADER(Pdf4QtSpeech
                       EXPORT_MACRO_NAME
                       PDF4QTSPEECHSHARED_EXPORT
                       EXPORT_FILE_NAME "${CMAKE_BINARY_DIR}/${INSTALL_INCLUDEDIR}/pdf4qtspeech_export.h")

target_link_libraries(Pdf4QtSpeech PUBLIC Pdf4QtLibCore PRIVATE Qt6::Core Qt6::TextToSpeech Qt6::Multimedia)
target_include_directories(Pdf4QtSpeech INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(Pdf4QtSpeech PUBLIC ${CMAKE_BINARY_DIR}/${INSTALL_INCLUDEDIR})

set_target_properties(Pdf4QtSpeech PROPERTIES VERSION ${PDF4QT_VERSION} SOVERSION ${PDF4QT_VERSION}
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR})

install(TARGETS Pdf4QtSpeech RUNTIME DESTINATION ${PDF4QT_INSTALL_BIN_DIR} LIBRARY DESTINATION ${PDF4QT_INSTALL_LIB_DIR})
