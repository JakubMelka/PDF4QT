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

"""Inventory for step A (deferred deletion at one place, PDFDeferredDeletion::retire) in Pdf4QtLibApp and
Pdf4QtLibFluent (GUI_overhaul/newapp_robustness.md 12-16, the map GUI_overhaul/phase2_briefs/p2a_map.md).

Usage (only the standard library; run it with python -I):

    python -I UnitTests/deferred_deletion_inventory.py [--root <working copy>] [sections] [--all] [--summary]

Sections (none given = all of them):

--connections   connect calls whose sender is neither the receiver nor its descendant (rule RR2-M6) - candidates
                for PDFDeferredDeletion::connect. Status of a row:
                  CERTAIN  a sender of the kinds known to live outside the receiver (document, theme manager, view,
                           notifier of the context, registry, controller, canvas, proxy, tab, shell / runner, qApp),
                           not created in the file as a child;
                  LIKELY   another sender from outside: a parameter (of a constructor, or of a member function whose
                           call sites in the file do not all pass own children), an object assigned from a foreign
                           getter, a singleton, the parent / window, a context / host / manager sender;
                  OWNED    the sender is created by this code, but parented to the window, to a foreign container (a
                           part of the tool shell, a builder widget, the canvas area, a parent passed in) or to
                           nothing: inside the receiver's logical subtree once the object or its container gets
                           setOwner - a connect candidate only when it does not;
                  UNKNOWN  no creation of the sender found (a member set in another file, an own getter, a factory
                           of another class) - check by hand;
                  OUTWARD  the receiver is not this (a child, a dialog, a requester passed in) and the sender is this
                           or one of its children: outside the receiver's subtree; matters when the receiver is
                           retired alone (A2 rebuilds, requests);
                  NO-CONTEXT a functor without a context object (a rule breach, listed for completeness).
                Not listed: the sender is this, a child created in the file with a parent inside the receiver, a value
                member, ui->, findChild, a factory call of this (create* / add*) (INTERNAL; --all lists them).
                Flag [unique]: Qt::UniqueConnection. Also listed: the targeted disconnect calls whose sender is not
                inside this (DISCONNECT; DISCONNECT-HANDLE for QObject::disconnect(handle), DISCONNECT-ALL for a
                wildcard disconnect() of this) - with handles in the connection scope (RR2-H1) they stay valid, the
                review checks that a converted connection is still found by its disconnect.
--filters       installEventFilter with a target other than this / an own child: FOREIGN (a foreign target, qApp,
                the window), OWNED (a target created by this into a foreign container), UNKNOWN (RR2-M7 baseline in
                BASELINE_FILTERS).
--owned         dialogs, message boxes, menus and popups created with the window / getDialogParent (OWNED-WINDOW), a
                foreign / created parent (OWNED-FOREIGN), an unknown parent (OWNED-UNKNOWN) or no parent
                (OWNED-NONE) - RR2-H2 setOwner candidates; QObjects owned without a parent (new without a parent by the
                constructor definitions of App / Fluent and not handed to a layout / setParent / add* / set*Widget;
                std::unique_ptr / make_unique of a QObject: OWNED-NOPARENT); members of a logically owned kind
                (overlay, canvas, part, holder, bar, chip, caption, hint, panel, highlight, popup) parented outside
                this (OWNED-MEMBER).
--timers        QTimer::timeout connections (and QTimer::singleShot with a delay) whose slot is named as a write or
                whose body calls write* / commit* / save* / flush* / store* / persist* (RR2-M3: setFlushOnRetire
                candidates, FLUSH), plus timerEvent overrides doing the same; --all lists every timeout slot (TIMER).
--external      activity a generic step does not see (RR2-M4, RR3-M9): speech (engine creation, say), QProcess /
                QDesktopServices::openUrl, QFileSystemWatcher, startTimer, network requests, media, a cursor set on a
                foreign widget, mouse / keyboard grab, QDrag, tooltip, status-bar message, canvas input / draw
                interface registration, MCP waiters, undo targets -> token / hook candidates.
--sync-deletes  delete / qDeleteAll / reset of a unique_ptr to a QObject and deleteLater: every synchronous delete
                outside a destructor (SYNC; the kind by name: M1 tab, M2 page, M3 shell / runner / gui / overlay /
                part, M5 row / panel, M6 popup, M7 window, M8 MCP, OTHER), deleteLater (DEFERRED) and deletes in a
                destructor (DESTRUCTOR) of those kinds; layout items only with --all.
--destroyed     connections to destroyed (they stay until PA6).
--members       QObject members outside the subtree of their object (INT1): held by value, by std::unique_ptr or by a
                pointer to an object created without a parent - retire neither stops them nor cuts their connections.
                PARENTLESS-TIMER a QTimer (it keeps firing after retire), PARENTLESS-SENDER another member with a plain
                connect from / to it in the files of its class; --all also PARENTLESS (no connection), NO-QOBJECT (the
                holder is no QObject), NOT-OWNED (a pointer member never created in the files of its class), HANDLED
                (connected only through PDFDeferredDeletion::connect; a stop in the hook retire is not enough - the
                plain connection stays, check 9 of LifetimeVictimCheck reports it) and CHILD (constructed with this, setParent, setOwner, adopted by a
                layout / add* call, created with a parent). Fix: a child (setParent(this) / constructor parent), setOwner, or
                PDFDeferredDeletion::connect to a receiver inside the object.

--summary prints a table "file;counts per section" (for the package proposal), --all also the INTERNAL rows.

What the heuristics can miss (read before trusting a count):
- C++ is not parsed: the sender, receiver and parent are classified by the text of the expressions in the same file
  and its header (the nearest "name = new T(..., parent)" before the call, a member initializer, the parameters of
  the enclosing function and the arguments at its call sites in the file, a value member in the header, a name of a
  known kind). A member created in another file, a factory method of another class, an object reparented later
  (setParent, addWidget into a widget of another subtree, QMenu::addAction of a foreign menu) or a parameter that is
  in fact a child is classified wrong.
- The receiver of a 4+ argument connect is the third argument; a connection made by one object for two others is
  classified by the sender only.
- Macros, connections made through helper functions (a helper that calls connect for its caller), the string-based
  SIGNAL / SLOT form and QMetaObject::invokeMethod are not seen.
- --owned reads the constructor definitions (the pointer parameter passed to the base class is the parent) and,
  without one, the declarations; a class of Qt is assumed to take its parent last. An object adopted by a call with
  another name, or a dialog of Qt whose parent is not its last argument, is classified wrong.
- --timers reads the body of the slot only (not the functions it calls); a write behind a helper call with another
  name is missed.
- --external is a token search (definitions skipped); a cursor is listed only on a foreign / created widget.
- --sync-deletes classifies the kind by the name of the deleted expression.
"""

import argparse
import bisect
import os
import re
import sys

LIBRARIES = ('Pdf4QtLibApp', 'Pdf4QtLibFluent')
HIERARCHY_LIBRARIES = ('Pdf4QtLibApp', 'Pdf4QtLibFluent', 'Pdf4QtLibWidgets', 'Pdf4QtLibCore')
EXTENSIONS = ('.cpp', '.h')
SECTIONS = ('connections', 'filters', 'owned', 'timers', 'external', 'sync-deletes', 'destroyed', 'members')

# The QObject classes of Qt, from which the classes of the libraries derive (a Q-prefixed base not listed here and not
# found in the sources is treated as no QObject)
QT_QOBJECTS = {
    'QObject', 'QWidget', 'QDialog', 'QMenu', 'QMenuBar', 'QTimer', 'QAction', 'QActionGroup', 'QShortcut', 'QFrame',
    'QLabel', 'QAbstractButton', 'QPushButton', 'QToolButton', 'QCheckBox', 'QRadioButton', 'QLineEdit', 'QTextEdit',
    'QPlainTextEdit', 'QTextBrowser', 'QComboBox', 'QSpinBox', 'QDoubleSpinBox', 'QAbstractSpinBox', 'QSlider',
    'QAbstractSlider', 'QScrollBar', 'QScrollArea', 'QAbstractScrollArea', 'QStackedWidget', 'QTabWidget', 'QTabBar',
    'QSplitter', 'QSplitterHandle', 'QListView', 'QTreeView', 'QTableView', 'QListWidget', 'QTreeWidget',
    'QTableWidget', 'QAbstractItemView', 'QHeaderView', 'QAbstractItemModel', 'QAbstractListModel',
    'QAbstractTableModel', 'QStandardItemModel', 'QSortFilterProxyModel', 'QAbstractProxyModel',
    'QIdentityProxyModel', 'QItemSelectionModel', 'QStyledItemDelegate', 'QAbstractItemDelegate', 'QItemDelegate',
    'QMainWindow', 'QDockWidget', 'QToolBar', 'QStatusBar', 'QGroupBox', 'QProgressBar', 'QDialogButtonBox',
    'QFileDialog', 'QMessageBox', 'QWindow', 'QGraphicsView', 'QGraphicsScene', 'QGraphicsObject', 'QValidator',
    'QIntValidator', 'QDoubleValidator', 'QRegularExpressionValidator', 'QCompleter', 'QFutureWatcherBase',
    'QFutureWatcher', 'QThread', 'QProcess', 'QFileSystemWatcher', 'QTextToSpeech', 'QPropertyAnimation',
    'QVariantAnimation', 'QAbstractAnimation', 'QParallelAnimationGroup', 'QSequentialAnimationGroup',
    'QGraphicsEffect', 'QGraphicsOpacityEffect', 'QGraphicsDropShadowEffect', 'QStyle', 'QProxyStyle', 'QCommonStyle',
    'QLayout', 'QBoxLayout', 'QHBoxLayout', 'QVBoxLayout', 'QGridLayout', 'QFormLayout', 'QStackedLayout',
    'QLocalServer', 'QLocalSocket', 'QTcpServer', 'QTcpSocket', 'QIODevice', 'QNetworkAccessManager', 'QNetworkReply',
    'QSystemTrayIcon', 'QSessionManager', 'QDrag', 'QMimeData', 'QTextDocument', 'QSyntaxHighlighter', 'QButtonGroup',
    'QSizeGrip', 'QRubberBand', 'QFocusFrame', 'QCalendarWidget', 'QDateEdit', 'QTimeEdit', 'QDateTimeEdit',
    'QToolBox', 'QKeySequenceEdit', 'QFontComboBox', 'QDial', 'QLCDNumber', 'QMdiArea', 'QWizard', 'QWizardPage',
    'QColumnView', 'QUndoView', 'QUndoStack', 'QFileSystemModel', 'QStringListModel', 'QConcatenateTablesProxyModel',
    'QTransposeProxyModel', 'QAbstractNativeEventFilter', 'QSocketNotifier', 'QSharedMemory', 'QSettings', 'QPdfWriter',
    'QPrintDialog', 'QAbstractPrintDialog', 'QPageSetupDialog', 'QTranslator', 'QMediaPlayer', 'QSoundEffect',
    'QAudioOutput', 'QAudioSink', 'QMediaCaptureSession', 'QMediaRecorder', 'QEventLoop', 'QSignalMapper',
    'QVideoWidget', 'QOpenGLWidget', 'QQuickWidget', 'QWidgetAction', 'QSvgWidget', 'QStylePlugin', 'QPlugin',
    'QGuiApplication', 'QApplication', 'QCoreApplication', 'QClipboard', 'QScreen', 'QStyleHints', 'QInputMethod',
    'QAccessibleWidget', 'QOffscreenSurface', 'QOpenGLContext', 'QTemporaryDir',
}
QT_NOT_QOBJECTS = {'QTemporaryDir'}

# The kinds of senders known to live outside the receiver's subtree (the brief: certain kinds)
CERTAIN_KINDS = (
    ('theme', re.compile(r'\bPDFThemeManager\s*::\s*getInstance\b')),
    ('qApp', re.compile(r'\bqApp\b|\bqGuiApp\b|\bQ(?:Core|Gui)?Application\s*::\s*(?:instance|clipboard|styleHints|inputMethod|primaryScreen|desktop)\b')),
    ('notifier', re.compile(r'\bgetNotifier\s*\(|\b(?:m_)?notifier\b')),
    ('document', re.compile(r'\b(?:m_)?(?:document|appDocument|currentDocument|targetDocument|sourceDocument|doc)\b|\bget(?:App)?Document\s*\(')),
    ('registry', re.compile(r'\b(?:m_)?\w*[rR]egistry\b|\bget\w*Registry\s*\(')),
    ('view', re.compile(r'\b(?:m_)?(?:view|documentView|pdfView)\b|\bget(?:Document)?View\s*\(')),
    ('controller', re.compile(r'\b(?:m_)?\w*[cC]ontroller\b|\bget\w*Controller\s*\(')),
    ('canvas', re.compile(r'\b(?:m_)?canvas\b|\bgetCanvas\s*\(|\b(?:m_)?(?:drawWidget|pdfWidget|canvasWidget|canvasArea)\b|\bget(?:Draw|PDF)Widget|\bgetCanvasArea\s*\(')),
    ('proxy', re.compile(r'\b(?:m_)?\w*[pP]roxy\b|\bgetDrawWidgetProxy\s*\(')),
    ('tab', re.compile(r'\b(?:m_)?(?:tab|documentTab|ownerTab|currentTab|toolTab)\b|\bget\w*Tab\s*\(')),
    ('shell/runner', re.compile(r'\b(?:m_)?(?:shell|runner|toolShell)\b|\bget(?:Shell|Runner)\s*\(')),
)
LIKELY_KINDS = (
    ('context', re.compile(r'\b(?:m_)?(?:context|appContext)\b|\bgetContext\s*\(')),
    ('host', re.compile(r'\b(?:m_)?\w*[hH]ost\b|\bget\w*Host\s*\(')),
    ('window', re.compile(r'\b(?:m_)?keyWidget\b|\bgetKeyWidget\s*\(|\b(?:m_)?(?:window|mainWindow)\b|\bwindow\s*\(\s*\)|\bgetMainWindow\s*\(|\bwindowHandle\s*\(')),
    ('parent', re.compile(r'^(?:m_)?parent(?:Widget)?\b|^parent(?:Widget)?\s*\(\s*\)')),
    ('navigator', re.compile(r'\b(?:m_)?navigator\b|\bgetNavigator\s*\(')),
    ('server/MCP', re.compile(r'\b(?:m_)?(?:server|session|mcp\w*)\b|\bget\w*Server\s*\(')),
    ('settings/storage', re.compile(r'\b(?:m_)?(?:settings|storage|database|store)\b|\bget(?:Settings|Storage|Database)\s*\(')),
    ('manager', re.compile(r'\b(?:m_)?\w*[mM]anager\b|\bget\w*Manager\s*\(')),
    ('gui/model', re.compile(r'\b(?:m_)?(?:gui|toolGui|toolModel)\b|\bget(?:GUI|Gui|Model)\s*\(')),
)

FUNCTION_HEAD = re.compile(r'^[^\n;{}()=]*?\b([A-Za-z_]\w*)\s*::\s*(~?[A-Za-z_]\w*)\s*\(', re.MULTILINE)
CLASS_DECLARATION = re.compile(r'\b(?:class|struct)\s+(?:[A-Z0-9_]+_EXPORT\s+)?([A-Za-z_]\w*)\s*(?:final\s*)?:\s*(?:public|protected|private)?\s*([A-Za-z_][\w:]*)')
CONNECT_TOKEN = re.compile(r'(?<![\w~])connect\s*\(')
DISCONNECT_TOKEN = re.compile(r'(?<![\w~])disconnect\s*\(')
NEW_EXPRESSION = re.compile(r'\bnew\s+([A-Za-z_][\w:]*)\s*(<[^;(){}]*>)?\s*([({])')
IDENTIFIER = re.compile(r'[A-Za-z_]\w*')
ADOPTION = r'(?:addWidget|insertWidget|addLayout|insertLayout|setWidget|setCentralWidget|addTab|insertTab|addPage|setViewport|setCornerWidget|addPermanentWidget|setMenu|setTitleBarWidget|addRow|setLayout|setItemWidget|setIndexWidget|setCellWidget|addItem|setDefaultWidget|addButton|setCompleter|setValidator|setModel|setSelectionModel|setItemDelegate|setItemDelegateForColumn|setGraphicsEffect|setPopup|setView)\s*\([^;]*\b{name}\b'

DIALOG_TYPE = re.compile(r'(?:Dialog|MessageBox|Menu|Popup|Popover|ToolTip|ToolTipWindow|Scrim|Picker|Flyout|Confirmation|Question|Palette)$')


class Entry:
    def __init__(self, section, status, path, line, kind, detail=''):
        self.section = section
        self.status = status
        self.path = path
        self.line = line
        self.kind = kind
        self.detail = detail


STRIP_PATTERN = re.compile(r'//[^\n]*|/\*.*?\*/|(?<![\w])R"([^(\s"]*)\(.*?\)\1"|"(?:\\.|[^"\\\n])*"|(?<![\w])\'(?:\\.|[^\'\\\n])*\'|^[ \t]*#[^\n]*', re.DOTALL | re.MULTILINE)


def blank_token(match):
    """A comment / literal / preprocessor line as spaces (the newlines stay; a literal keeps its quotes)"""
    token = match.group(0)
    spaces = ''.join('\n' if ch == '\n' else ' ' for ch in token)
    if token[0] in '"\'' or token.startswith('R"'):
        quote = token[-1]
        return quote + spaces[1:-1] + quote if len(token) >= 2 else spaces
    return spaces


def strip_code(text):
    """The text without comments, string and character literals and preprocessor lines (the lines and offsets stay)"""
    return STRIP_PATTERN.sub(blank_token, text)


def line_of(text, offset):
    return text.count('\n', 0, offset) + 1


def split_arguments(code, open_index):
    """The top-level arguments of the call whose '(' is at \\p open_index: [(text, start)], end index (after ')')"""
    depth = 0
    angle = 0
    arguments = []
    start = open_index + 1
    i = open_index
    length = len(code)
    while i < length:
        c = code[i]
        if c in '([{':
            depth += 1
        elif c in ')]}':
            depth -= 1
            if depth == 0:
                arguments.append((code[start:i], start))
                return [(text, s) for text, s in arguments if text.strip() or len(arguments) > 1], i + 1
        elif c == '<' and depth == 1 and re.search(r'(?:[Oo]verload|_cast|QPointer|unique_ptr|make_unique|QList|QVector|std::\w+|PDF\w+|Q\w+)\s*$', code[max(0, i - 40):i]):
            angle += 1
        elif c == '>' and angle > 0 and depth == 1 and code[i - 1] != '-':
            angle -= 1
        elif c == ',' and depth == 1 and angle == 0:
            arguments.append((code[start:i], start))
            start = i + 1
        elif c == ';' and depth <= 1:
            break
        i += 1
    arguments.append((code[start:i], start))
    return arguments, i


def normalize(expression):
    """The sender / receiver expression without &, casts, .data() / .get() and this->"""
    text = ' '.join(expression.split())
    changed = True
    while changed:
        changed = False
        before = text
        text = text.strip()
        if text.startswith('&'):
            text = text[1:].strip()
        match = re.match(r'^(?:static_cast|qobject_cast|dynamic_cast|reinterpret_cast|const_cast)\s*<[^>]*>\s*\((.*)\)$', text)
        if match:
            text = match.group(1)
        match = re.match(r'^\((.*)\)$', text)
        if match and match.group(1).count('(') == match.group(1).count(')'):
            text = match.group(1)
        text = re.sub(r'\s*\.\s*(?:data|get)\s*\(\s*\)$', '', text)
        text = re.sub(r'^this\s*->\s*', '', text)
        changed = text != before
    return text


class FileInfo:
    def __init__(self, path, code, header):
        self.path = path
        self.code = code
        self.header = header
        self.functions = []
        for match in FUNCTION_HEAD.finditer(code):
            open_index = match.end() - 1
            arguments, end = split_arguments(code, open_index)
            tail = code[end:end + 400]
            body = re.match(r'\s*(?:const\s*)?(?:noexcept\s*)?(?:override\s*)?(?:final\s*)?(?::[^{;]*)?\{', tail, re.DOTALL)
            if not body:
                continue
            parameters = []
            for text, _ in arguments:
                text = text.split('=')[0].strip()
                names = IDENTIFIER.findall(text)
                if names:
                    parameters.append(names[-1])
            self.functions.append((match.start(), match.group(1), match.group(2), parameters, end + body.end()))
        self.starts = [function[0] for function in self.functions]

    def function_at(self, offset):
        """(start, class, method, parameters, body start) of the function containing \\p offset, or None"""
        index = bisect.bisect_right(self.starts, offset) - 1
        return self.functions[index] if index >= 0 else None


class Inventory:
    def __init__(self, root):
        self.root = root
        self.files = {}
        self.classes = {}
        self.constructors = {}
        for library in HIERARCHY_LIBRARIES:
            for path in collect_files(root, library):
                relative = os.path.relpath(path, root).replace('\\', '/')
                with open(path, encoding='utf-8', errors='replace') as file:
                    code = file.read()
                if library in LIBRARIES:
                    code = strip_code(code)
                for match in CLASS_DECLARATION.finditer(code):
                    self.classes.setdefault(match.group(1), match.group(2).split('::')[-1])
                if library in LIBRARIES:
                    self.files[relative] = code
        self.infos = {}
        for path, code in self.files.items():
            header = self.files.get(os.path.splitext(path)[0] + '.h', '') if path.endswith('.cpp') else ''
            self.infos[path] = FileInfo(path, code, header)
        self.qobject_cache = {}
        self.parent_cache = {}
        # The files with a constructor definition (Type::Type) / a class body (headers) of each class
        self.constructor_files = {}
        self.declaration_files = {}
        for path, info in self.infos.items():
            if path.endswith('.cpp'):
                for name in set(re.findall(r'\b([A-Za-z_]\w*)\s*::\s*\1\s*\(', info.code)):
                    self.constructor_files.setdefault(name, []).append(path)
            else:
                for name in set(re.findall(r'\b(?:class|struct)\s+(?:[A-Z0-9_]+_EXPORT\s+)?([A-Za-z_]\w*)', info.code)):
                    self.declaration_files.setdefault(name, []).append(path)
        self.pattern_cache = {}
        # Class::method -> (path, line, body) of every function definition
        self.bodies = {}
        for path, info in self.infos.items():
            for start, class_name, method, _, body_start in info.functions:
                depth = 0
                for index in range(body_start - 1, len(info.code)):
                    character = info.code[index]
                    if character == '{':
                        depth += 1
                    elif character == '}':
                        depth -= 1
                        if depth == 0:
                            self.bodies.setdefault((class_name, method), (path, line_of(info.code, start), info.code[body_start:index]))
                            break

    def is_qobject(self, class_name):
        class_name = class_name.split('::')[-1]
        if class_name in self.qobject_cache:
            return self.qobject_cache[class_name]
        seen = set()
        current = class_name
        result = False
        while current and current not in seen:
            seen.add(current)
            if current in QT_QOBJECTS and current not in QT_NOT_QOBJECTS:
                result = True
                break
            current = self.classes.get(current, '')
        self.qobject_cache[class_name] = result
        return result

    # --- classification of an object expression relative to "this" -------------------------------------------------

    def creation_of(self, info, name, offset):
        """The nearest creation "name = new T(args)" / "name(new T(args))" before \\p offset in its function, else
        anywhere in the file / header: (type, arguments, offset) or None"""
        escaped = re.escape(name)
        pattern = re.compile(r'(?:\b' + escaped + r'\s*=\s*|(?<![\w.>])' + escaped + r'\s*[({]\s*)new\s+([A-Za-z_][\w:]*)\s*(<[^;(){}]*>)?\s*([({])')
        function = info.function_at(offset)
        start = function[0] if function else 0
        best = None
        for match in pattern.finditer(info.code, start, offset):
            best = match
        if best is None:
            for match in pattern.finditer(info.code):
                best = match
                break
        if best is None:
            return None
        if best.group(3) == '(' or best.group(3) == '{':
            arguments, _ = split_arguments(info.code, best.end() - 1)
        else:
            arguments = []
        return best.group(1).split('::')[-1], [text.strip() for text, _ in arguments if text.strip()], best.start()

    def assignment_of(self, info, name, offset):
        """The nearest assignment "name = <not new>" before \\p offset in its function, else in the file: text"""
        escaped = re.escape(name)
        pattern = re.compile(r'(?<![\w.>:])' + escaped + r'\s*=\s*(?!new\b|=)([^;]+);')
        function = info.function_at(offset)
        start = function[0] if function else 0
        best = None
        for match in pattern.finditer(info.code, start, offset):
            best = match
        if best is None:
            for match in pattern.finditer(info.code):
                if match.group(1).strip() not in ('nullptr', '0', 'false', 'true'):
                    best = match
                    break
        if best is not None:
            return best.group(1).strip()
        # A member initializer of a constructor: m_view(view), m_view{view}
        initializer = re.search(r'[:,]\s*' + escaped + r'\s*[({]\s*([^(){};]+?)\s*[)}]', info.code)
        if initializer and name.startswith('m_'):
            value = initializer.group(1).strip()
            if value not in ('nullptr', 'NULL', '0', 'false', 'true'):
                return value
        return None

    def is_value_member(self, info, name):
        """A member (or local) declared by value of a QObject type: QTimer m_timer;"""
        for text in (info.header, info.code):
            for match in re.finditer(r'\b([A-Za-z_][\w:]*(?:<[^;<>]*>)?)\s+' + re.escape(name) + r'\s*(?:;|\{|=\s*[^n])', text):
                type_name = match.group(1)
                base = type_name.split('<')[0].split('::')[-1]
                if base in ('return', 'delete', 'new', 'else', 'case', 'const', 'auto', 'emit', 'Q_EMIT'):
                    continue
                if self.is_qobject(base):
                    return True
        return False

    def is_adopted(self, info, name, offset):
        function = info.function_at(offset)
        start = function[0] if function else 0
        index = bisect.bisect_right(info.starts, offset)
        end = info.starts[index] if index < len(info.starts) else len(info.code)
        region = info.code[start:end]
        if re.search(ADOPTION.format(name=re.escape(name)), region):
            return True
        # Handed to another object: add* / insert* / append* / set*Widget / setMimeData / std::move into a holder
        if re.search(r'\b(?:add|insert|append|push_back|emplace_back|setMimeData|set\w*Widget|set\w*Menu|set\w*Panel|set\w*Content)\w*\s*\([^;]*?(?<![\w.>])' + re.escape(name) + r'\b(?!\s*(?:->|\.))', region):
            return True
        if re.search(r'\b' + re.escape(name) + r'\s*->\s*setParent\s*\(\s*(?!nullptr)', region):
            return True
        return False

    def classify(self, info, expression, offset, depth=0):
        """classify_expression; an unknown object named as a certain kind (a view, a tab, a document, ...) and not
        created here is foreign"""
        status, reason = self.classify_expression(info, expression, offset, depth)
        if status == 'unknown':
            root = re.match(r'^\s*&?\s*([A-Za-z_]\w*)', normalize(expression))
            if root and (self.kind_of(root.group(1))[0] == 'CERTAIN' or self.kind_of(root.group(1))[1] in ('host', 'context', 'window')):
                return 'foreign', reason + ' - named ' + self.kind_of(root.group(1))[1]
        return status, reason

    def classify_expression(self, info, expression, offset, depth=0):
        """('self' | 'child' | 'created' | 'foreign' | 'unknown', reason) of an object expression for the code at
        the offset (the receiver is the enclosing object): created = created by this code, but parented to the window,
        to a foreign container (a part of the shell, a parameter) or to nothing - inside after setOwner"""
        text = normalize(expression)
        if not text:
            return 'unknown', 'empty'
        ternary = re.match(r'^[^?]*\?([^:]*):(.*)$', text)
        if ternary:
            text = normalize(ternary.group(1))
        if text == 'this' or text == '*this':
            return 'self', 'this'
        if depth > 5:
            return 'unknown', 'too deep'
        if re.match(r'^m?_?ui\s*->', text):
            return 'child', 'ui'
        if re.search(r'\bfindChild(?:ren)?\s*<', text):
            return 'child', 'findChild'
        if re.search(r'(?:^|->|\.)\s*(?:window|topLevelWidget|nativeParentWidget)\s*\(\s*\)|\bgetDialogParent\s*\(|\bget(?:Main)?Window\s*\(', text):
            return 'foreign', 'the window'
        if re.match(r'^(?:parent|parentWidget|parentObject)\s*\(\s*\)', text):
            return 'foreign', 'the parent'
        if re.match(r'^(?:PDFThemeManager\s*::\s*getInstance|qApp|qGuiApp|Q(?:Core|Gui)?Application\s*::)', text):
            return 'foreign', 'singleton'
        root_match = re.match(r'^([A-Za-z_]\w*)(.*)$', text, re.DOTALL)
        if not root_match:
            return 'unknown', 'expression'
        root = root_match.group(1)
        rest = root_match.group(2).strip()
        if rest.startswith('(') or rest.startswith('::'):
            # A call of an own method or a static helper: a factory (create / add / make / build / insert) or a call
            # with this builds an own child; a getter is unknown
            call = re.match(r'^(?:::\s*([A-Za-z_]\w*))*\s*\(', rest)
            method = root
            qualified = re.findall(r'::\s*([A-Za-z_]\w*)', rest.split('(')[0])
            if qualified:
                method = qualified[-1]
            if re.search(r'(?:\(|,)\s*this\s*(?:,|\))', rest):
                return 'child', 'call {} with this'.format(method)
            if re.match(r'^(?:create|add|make|build|insert|new)', method) and call is not None:
                return 'child', 'factory ' + method
            return 'unknown', 'call ' + method
        if rest.startswith('[') or re.match(r'^\.\s*(?:at|value|first|last|front|back)\s*\(', rest):
            status, reason = self.classify(info, root, offset, depth + 1)
            return status, 'element of ' + root + ' (' + reason + ')'
        if rest.startswith('->') or rest.startswith('.'):
            status, reason = self.classify(info, root, offset, depth + 1)
            access = re.match(r'^(?:->|\.)\s*([A-Za-z_]\w*)', rest)
            method = access.group(1) if access else ''
            if status == 'self':
                return 'unknown', 'own member access'
            if status == 'foreign' and re.match(r'^(?:create|add|make|build|insert)', method):
                return 'created', '{} built by {} ({})'.format(method, root, reason)
            return status, 'via ' + root + ' (' + reason + ')'

        name = root
        function = info.function_at(offset)
        creation = self.creation_of(info, name, offset)
        if creation:
            type_name, arguments, creation_offset = creation
            if 'this' in [normalize(argument) for argument in arguments]:
                return 'child', 'new {}(this)'.format(type_name)
            parent_text = normalize(arguments[-1]) if arguments else ''
            if arguments and self.is_qobject(type_name) and takes_parent_after(self, type_name, len(arguments)):
                # The constructor of the class takes no parent at this position: the parent stays nullptr
                parent_text = ''
            if not parent_text or parent_text in ('nullptr', 'NULL', '0', 'true', 'false') or re.search(r'^[\d"\']|^tr\s*\(|^Qt\s*::|^\w+\s*::\s*[A-Z]\w*$|^QString', parent_text):
                if self.is_adopted(info, name, creation_offset):
                    return 'child', 'new {} adopted'.format(type_name)
                return 'created', 'new {} without parent'.format(type_name)
            parent_status, parent_reason = self.classify(info, parent_text, creation_offset, depth + 1)
            if parent_status in ('self', 'child'):
                return 'child', 'new {}({})'.format(type_name, parent_text)
            if parent_status in ('foreign', 'created'):
                return 'created', 'new {} parented to {} ({})'.format(type_name, parent_text, parent_reason)
            if self.is_adopted(info, name, creation_offset):
                return 'child', 'new {} adopted'.format(type_name)
            return 'unknown', 'new {}({}: {})'.format(type_name, parent_text, parent_reason)
        if function and name in function[3]:
            if re.search(r'(?<![\w.>])' + re.escape(name) + r'\s*->\s*setParent\s*\(\s*this\s*\)', info.code[function[0]:offset]):
                return 'child', 'parameter adopted by setParent(this)'
            return self.classify_parameter(info, function, name, depth)
        if self.is_value_member(info, name):
            return 'child', 'value member'
        assigned = self.assignment_of(info, name, offset)
        if assigned:
            assigned_text = normalize(assigned)
            cast = re.match(r'^(?:qobject_cast|static_cast|dynamic_cast)\s*<[^>]*>\s*\((.*)\)$', assigned_text)
            if cast:
                assigned_text = normalize(cast.group(1))
            if re.match(r'^(?:sender|QObject\s*::\s*sender)\s*\(\s*\)', assigned_text):
                return 'unknown', 'sender()'
            if assigned_text and assigned_text != name and assigned_text not in ('nullptr', 'NULL', '0'):
                status, reason = self.classify(info, assigned_text, offset, depth + 1)
                return status, 'assigned from ' + assigned_text[:50] + ' (' + reason + ')'
        if name.startswith('m_'):
            return 'unknown', 'member set elsewhere'
        return 'unknown', 'no creation found'

    def classify_parameter(self, info, function, name, depth):
        """A parameter of a member function (not a constructor): the arguments at its call sites in the same file
        decide (all inside this -> child); no call site or a constructor -> foreign"""
        _, class_name, method, parameters, _ = function
        if method == class_name or method.startswith('~') or depth > 3:
            return 'foreign', 'parameter of {}'.format(method)
        index = parameters.index(name)
        statuses = set()
        sites = 0
        for match in re.finditer(r'(?<![\w.>:~])(?:this\s*->\s*)?' + re.escape(method) + r'\s*\(', info.code):
            if is_declaration_of(info.code, match.start()):
                continue
            site_function = info.function_at(match.start())
            if site_function is None or site_function[1] != class_name or site_function[2] == method:
                continue
            arguments, _ = split_arguments(info.code, match.end() - 1)
            if index >= len(arguments):
                continue
            sites += 1
            status, _ = self.classify(info, arguments[index][0], match.start(), depth + 1)
            statuses.add(status)
        if sites and statuses <= {'self', 'child'}:
            return 'child', 'parameter of {} - {} call sites pass own children'.format(method, sites)
        if 'foreign' in statuses or not sites:
            return 'foreign', 'parameter of {}'.format(method) + ('' if sites else ' (no call site here)')
        if 'created' in statuses:
            return 'created', 'parameter of {} - a call site passes a created object'.format(method)
        return 'unknown', 'parameter of {} - call sites unknown'.format(method)

    def kind_of(self, expression):
        text = normalize(expression)
        for kind, regex in CERTAIN_KINDS:
            if regex.search(text):
                return 'CERTAIN', kind
        for kind, regex in LIKELY_KINDS:
            if regex.search(text):
                return 'LIKELY', kind
        return '', ''


def collect_files(root, library):
    files = []
    base = os.path.join(root, library, 'sources')
    for directory, _, names in os.walk(base):
        for name in sorted(names):
            if name.endswith(EXTENSIONS):
                files.append(os.path.join(directory, name))
    return sorted(files)


def is_declaration_of(code, offset):
    """The token at \\p offset is the name of a function in its declaration / definition"""
    statement_start = max(code.rfind('\n', 0, offset), code.rfind(';', 0, offset), code.rfind('{', 0, offset), code.rfind('}', 0, offset)) + 1
    prefix = code[statement_start:offset]
    if not prefix.strip():
        return False
    if re.search(r'[=(,]|\breturn\b|->|\.', prefix):
        return False
    return bool(re.match(r'^\s*(?:(?:static|virtual|inline|explicit|const|template\s*<[^>]*>)\s+)*[\w:<>]+[\s*&]+(?:[A-Za-z_]\w*\s*::\s*)*$', prefix))


def call_receiver(code, offset):
    """The expression before "->name(" / ".name(" at \\p offset (the receiver of a member call), '' for an
    unqualified call, 'QObject' for QObject::name("""
    prefix = code[max(0, offset - 200):offset].rstrip()
    if prefix.endswith('::'):
        match = re.search(r'([A-Za-z_]\w*)\s*::$', prefix)
        return (match.group(1) + '::') if match else '::'
    if not (prefix.endswith('->') or prefix.endswith('.')):
        return ''
    before = prefix[:-2] if prefix.endswith('->') else prefix[:-1]
    # Walk back over one postfix expression (identifiers, ->, ., ::, balanced () and [])
    i = len(before)
    depth = 0
    while i > 0:
        c = before[i - 1]
        if c in ')]':
            depth += 1
        elif c in '([':
            if depth == 0:
                break
            depth -= 1
        elif depth == 0 and not (c.isalnum() or c in '_>-.:*&' or c.isspace()):
            break
        elif depth == 0 and c.isspace():
            # a space ends the expression unless an operator continues it
            rest = before[:i - 1].rstrip()
            if not (rest.endswith('->') or rest.endswith('.') or rest.endswith('::')) and not before[i:].lstrip().startswith(('->', '.', '::')):
                break
        elif depth == 0 and c == '>' and not before[:i].endswith('->'):
            break
        i -= 1
    return before[i:].strip()


# --- sections ---------------------------------------------------------------------------------------------------------

def connection_status(inventory, info, offset, sender_text, status_s, reason_s, receiver_text):
    """The status of a connection (see the header) from the classification of its sender and receiver"""
    if receiver_text != 'this':
        if sender_text == receiver_text or re.search(r'(?:via|parented to|built by|new \w+\(|element of|assigned from)\s*' + re.escape(receiver_text) + r'\b', reason_s):
            # The sender is the receiver or a part of it
            return 'INTERNAL'
        status_r, _ = inventory.classify(info, receiver_text, offset)
        if status_s in ('self', 'child'):
            # The sender is inside this, the receiver is a child / another object: outside the receiver's subtree
            return 'OUTWARD'
        if status_r in ('foreign',) and status_s in ('foreign', 'created', 'unknown'):
            # A connection made by this for two foreign objects: classified like the sender
            pass
    elif status_s in ('self', 'child'):
        return 'INTERNAL'
    certainty, _ = inventory.kind_of(sender_text)
    if status_s == 'created':
        return 'OWNED'
    if status_s == 'foreign':
        return 'CERTAIN' if certainty == 'CERTAIN' else 'LIKELY'
    if status_s in ('self', 'child'):
        return 'INTERNAL'
    if certainty == 'CERTAIN':
        return 'CERTAIN'
    return 'LIKELY' if certainty == 'LIKELY' else 'UNKNOWN'


def scan_connections(inventory, show_all):
    entries = []
    for path, info in inventory.infos.items():
        code = info.code
        for match in CONNECT_TOKEN.finditer(code):
            if is_declaration_of(code, match.start()):
                continue
            receiver_prefix = call_receiver(code, match.start())
            if receiver_prefix and receiver_prefix not in ('QObject::',):
                if receiver_prefix.endswith('::'):
                    # PDFDeferredDeletion::connect and other own connect helpers
                    if receiver_prefix != 'QObject::':
                        continue
                else:
                    # object->connect(...): a QObject member call (rare)
                    pass
            arguments, _ = split_arguments(code, match.end() - 1)
            if len(arguments) < 3:
                continue
            sender = arguments[0][0]
            signal = ' '.join(arguments[1][0].split())
            line = line_of(code, match.start())
            unique = 'UniqueConnection' in code[match.start():match.start() + sum(len(a[0]) for a in arguments) + 2 * len(arguments) + 10]
            flags = ' [unique]' if unique else ''
            if len(arguments) == 3:
                status_s, reason_s = inventory.classify(info, sender, match.start())
                if status_s != 'self':
                    entries.append(Entry('connections', 'NO-CONTEXT', path, line, 'functor', '{} {} ({}){}'.format(normalize(sender), signal, reason_s, flags)))
                continue
            receiver = arguments[2][0]
            slot = ' '.join(arguments[3][0].split())
            receiver_text = normalize(receiver)
            status_s, reason_s = inventory.classify(info, sender, match.start())
            sender_text = normalize(sender)
            detail = '{} {} -> {} {} ({}){}'.format(sender_text, signal, receiver_text, slot, reason_s, flags)
            status = connection_status(inventory, info, match.start(), sender_text, status_s, reason_s, receiver_text)
            certainty, kind = inventory.kind_of(sender_text)
            if status == 'OWNED':
                kind = 'setOwner'
            elif status in ('UNKNOWN', 'LIKELY') and not kind:
                kind = status_s
            if status == 'INTERNAL' and not show_all:
                continue
            entries.append(Entry('connections', status, path, line, kind or status.lower(), detail))

        # Targeted disconnects whose sender is not internal
        for match in DISCONNECT_TOKEN.finditer(code):
            if is_declaration_of(code, match.start()):
                continue
            receiver_prefix = call_receiver(code, match.start())
            arguments, _ = split_arguments(code, match.end() - 1)
            line = line_of(code, match.start())
            if receiver_prefix and not receiver_prefix.endswith('::'):
                sender = receiver_prefix
                rest = ', '.join(' '.join(a[0].split()) for a in arguments)
            else:
                if not arguments or not arguments[0][0].strip():
                    entries.append(Entry('connections', 'DISCONNECT-ALL', path, line, 'wildcard', 'disconnect() of this (all its outgoing connections)'))
                    continue
                sender = arguments[0][0]
                rest = ', '.join(' '.join(a[0].split()) for a in arguments[1:])
                if len(arguments) == 1:
                    # QObject::disconnect(handle)
                    entries.append(Entry('connections', 'DISCONNECT-HANDLE', path, line, 'handle', normalize(sender)))
                    continue
            status_s, reason_s = inventory.classify(info, sender, match.start())
            if status_s in ('self', 'child', 'created') and not show_all:
                continue
            certainty, kind = inventory.kind_of(normalize(sender))
            entries.append(Entry('connections', 'DISCONNECT', path, line, kind or status_s, '{} ({}) [{}]'.format(normalize(sender), reason_s, rest)))
    return entries


BASELINE_FILTERS = (
    'pdfappcommentpopup.cpp', 'pdfappfloatingbar.cpp', 'pdfappmcppreview.cpp', 'pdfappmcpconfirmdialog.cpp',
    'pdfappprintdialog.cpp', 'pdfapptoolresultdialog.cpp', 'pdfappannotationcontroller.cpp', 'pdfappoutlinetarget.cpp',
    'pdfappdocumentactions.cpp', 'pdfappformcontroller.cpp', 'pdfappmeasureoverlay.cpp',
    'pdfapptoolbeforeafteroverlay.cpp', 'pdfapptooldrawingpart.cpp', 'pdfapptoolmodelayout.cpp',
    'pdfapptoolpanelparts.cpp', 'pdfappsearchwidgets.cpp', 'pdfappminuteticker.cpp', 'pdfappdocumenttoolbar.cpp',
    'pdfmessagebox.cpp', 'pdftooltip.cpp', 'pdffluentwidgets.cpp',
)


def scan_filters(inventory, show_all):
    entries = []
    token = re.compile(r'\binstallEventFilter\s*\(')
    for path, info in inventory.infos.items():
        code = info.code
        for match in token.finditer(code):
            if is_declaration_of(code, match.start()):
                continue
            target = call_receiver(code, match.start())
            arguments, _ = split_arguments(code, match.end() - 1)
            filter_text = normalize(arguments[0][0]) if arguments else ''
            line = line_of(code, match.start())
            if not target or target.endswith('::'):
                target_status, reason = ('self', 'this')
                if target.endswith('::'):
                    target_status, reason = ('foreign', 'static ' + target)
            else:
                target_status, reason = inventory.classify(info, target, match.start())
            filter_status, filter_reason = inventory.classify(info, filter_text, match.start()) if filter_text != 'this' else ('self', 'this')
            if target_status in ('self', 'child') and not show_all:
                continue
            certainty, kind = inventory.kind_of(normalize(target))
            if re.search(r'\bapplication\b|qApp|instance\s*\(', target):
                kind = 'qApp'
            status = 'FOREIGN' if target_status in ('foreign',) or certainty == 'CERTAIN' else ('INTERNAL' if target_status in ('self', 'child') else 'UNKNOWN')
            if target_status in ('self', 'child'):
                status = 'INTERNAL'
            elif target_status == 'created':
                status = 'OWNED'
            entries.append(Entry('filters', status, path, line, kind or target_status, '{} on {} ({}); filter {} ({})'.format('installEventFilter', normalize(target) or 'this', reason, filter_text, filter_reason)))
    return entries


def scan_owned(inventory, show_all):
    entries = []
    unique = re.compile(r'\b(?:std::unique_ptr|QScopedPointer|std::make_unique)\s*<\s*([A-Za-z_][\w:]*)')
    for path, info in inventory.infos.items():
        code = info.code
        for match in NEW_EXPRESSION.finditer(code):
            type_name = match.group(1).split('::')[-1]
            if not inventory.is_qobject(type_name) or type_name == 'QMimeData':
                continue
            if match.group(3) == '(' or match.group(3) == '{':
                arguments, end = split_arguments(code, match.end() - 1)
            else:
                arguments, end = [], match.end()
            arguments = [normalize(a[0]) for a in arguments if a[0].strip()]
            parent = arguments[-1] if arguments else ''
            line = line_of(code, match.start())
            # The variable it is assigned to
            before = code[max(0, match.start() - 120):match.start()]
            assigned = re.search(r'([A-Za-z_]\w*)\s*(?:=|\(|\{)\s*$', before)
            name = assigned.group(1) if assigned else ''
            is_dialog = bool(DIALOG_TYPE.search(type_name)) or any(inventory.is_qobject(type_name) and base_chain_has(inventory, type_name, b) for b in ('QDialog', 'QMenu'))
            if is_dialog:
                if type_name in ('QFileDialog', 'QColorDialog', 'QInputDialog', 'QMessageBox') and arguments:
                    # The dialogs of Qt take the parent first
                    parent = arguments[0]
                if 'this' in arguments:
                    if show_all:
                        entries.append(Entry('owned', 'INTERNAL', path, line, 'popup', 'new {}(this, ...)'.format(type_name)))
                    continue
                if not arguments or parent in ('nullptr', 'NULL', '0'):
                    if name and inventory.is_adopted(info, name, match.start()):
                        continue
                    entries.append(Entry('owned', 'OWNED-NONE', path, line, 'popup', 'new {} without parent{}'.format(type_name, ' -> ' + name if name else '')))
                    continue
                if parent == 'this':
                    if show_all:
                        entries.append(Entry('owned', 'INTERNAL', path, line, 'popup', 'new {}(this)'.format(type_name)))
                    continue
                if re.search(r'\bwindow\s*\(\s*\)|getDialogParent\s*\(|\btopLevelWidget\b|getWindow\s*\(|mainWindow|prepareDialog', parent):
                    entries.append(Entry('owned', 'OWNED-WINDOW', path, line, 'popup', 'new {}(..., {}){}'.format(type_name, parent, ' -> ' + name if name else '')))
                    continue
                status, reason = inventory.classify(info, parent, match.start())
                if status in ('self', 'child') and not show_all:
                    continue
                if status in ('self', 'child'):
                    entries.append(Entry('owned', 'INTERNAL', path, line, 'popup', 'new {}(..., {}) ({})'.format(type_name, parent, reason)))
                    continue
                entries.append(Entry('owned', 'OWNED-FOREIGN' if status in ('foreign', 'created') else 'OWNED-UNKNOWN', path, line, 'popup', 'new {}(..., {}) ({}){}'.format(type_name, parent, reason, ' -> ' + name if name else '')))
                continue
            # Other QObjects: owned without a parent
            if arguments and parent not in ('nullptr', 'NULL', '0'):
                # Is a parent passed? (the constructors of the class in the headers; a class of Qt takes it last)
                if parent == 'this' or not takes_parent_after(inventory, type_name, len(arguments)):
                    # A member of a logically owned kind (overlay, canvas, part, holder, bar, chip, caption, panel)
                    # parented outside this (the canvas area, a part of the shell, the window): setOwner candidate
                    if name.startswith('m_') and parent != 'this' and re.search(r'(?i)overlay|canvas|part|holder|bar$|chip|caption|hint|panel|highlight|popup|popover', type_name):
                        status, reason = inventory.classify(info, parent, match.start())
                        if status in ('foreign', 'created'):
                            entries.append(Entry('owned', 'OWNED-MEMBER', path, line, owned_kind(type_name, name), 'new {}(..., {}) -> {} ({})'.format(type_name, parent[:40], name, reason[:90])))
                    continue
            elif not arguments and not takes_parent_after(inventory, type_name, 0):
                continue
            if name and inventory.is_adopted(info, name, match.start()):
                continue
            if name and re.search(r'\b' + re.escape(name) + r'\s*->\s*moveToThread\s*\(', code):
                continue
            kind = owned_kind(type_name, name)
            entries.append(Entry('owned', 'OWNED-NOPARENT', path, line, kind, 'new {}({}){}'.format(type_name, ', '.join(a[:30] for a in arguments), ' -> ' + name if name else '')))
        for match in unique.finditer(code):
            type_name = match.group(1).split('::')[-1]
            if not inventory.is_qobject(type_name):
                continue
            line = line_of(code, match.start())
            entries.append(Entry('owned', 'OWNED-NOPARENT', path, line, owned_kind(type_name, ''), match.group(0).split('<')[0] + '<' + type_name + '>'))
    return entries


def base_chain_has(inventory, class_name, base):
    seen = set()
    current = class_name
    while current and current not in seen:
        if current == base:
            return True
        seen.add(current)
        current = inventory.classes.get(current, '')
    return False


def takes_parent_after(inventory, type_name, argument_count):
    """The class has a constructor with a parent parameter at a position >= \\p argument_count (the call leaves it
    at its default nullptr): True; unknown constructors: False"""
    key = (type_name, argument_count)
    if key not in inventory.parent_cache:
        inventory.parent_cache[key] = takes_parent_after_uncached(inventory, type_name, argument_count)
    return inventory.parent_cache[key]


def takes_parent_after_uncached(inventory, type_name, argument_count):
    # 1. The constructor definitions: Type::Type(params) : Base(parameter) - the parameter passed to the base is the parent
    base = inventory.classes.get(type_name, '')
    definitions = []
    pattern = re.compile(r'\b' + re.escape(type_name) + r'\s*::\s*' + re.escape(type_name) + r'\s*\(')
    for path in inventory.constructor_files.get(type_name, ()):
        info = inventory.infos[path]
        for match in pattern.finditer(info.code):
            arguments, end = split_arguments(info.code, match.end() - 1)
            parameters = [text.strip() for text, _ in arguments if text.strip()]
            initializer = re.match(r'\s*(?:const\s*)?:\s*(?!m_)[A-Za-z_][\w:]*(?:<[^<>]*>)?\s*([({])', info.code[end:end + 300])
            parent_index = -1
            if initializer:
                # A pointer parameter passed to the base class (its parent), wherever it is in the initializer
                passed_arguments, _ = split_arguments(info.code, end + initializer.end() - 1)
                passed = {normalize(text) for text, _ in passed_arguments}
                for index, parameter in enumerate(parameters):
                    declaration = parameter.split('=')[0]
                    names = IDENTIFIER.findall(declaration)
                    if names and names[-1] in passed and '*' in declaration:
                        parent_index = index
            definitions.append((parameters, parent_index))
    if definitions:
        # Default arguments are in the header: a definition with at least as many parameters as the call passes
        for parameters, parent_index in definitions:
            if len(parameters) >= argument_count and 0 <= parent_index < argument_count:
                return False
        return True
    # 2. Declarations only (inline constructors): a QWidget* / QObject* / parent parameter
    overloads = []
    for path in inventory.declaration_files.get(type_name, ()):
        info = inventory.infos[path]
        for match in re.finditer(r'(?<![\w~:])' + re.escape(type_name) + r'\s*\(([^;{}]*)\)\s*(?:;|override|\{|:)', info.code):
            text = match.group(1).strip()
            if re.match(r'^(?:const\s+)?' + re.escape(type_name) + r'\s*&', text):
                continue
            overloads.append([p.strip() for p in split_top_level(text) if p.strip()])
    if not overloads:
        # A class of Qt / Widgets: the last argument of a QObject constructor is its parent
        return argument_count == 0
    for parameters in overloads:
        required = sum(1 for p in parameters if '=' not in p)
        if not (required <= argument_count <= len(parameters)):
            continue
        for index, parameter in enumerate(parameters):
            if re.search(r'\b(?:QWidget|QObject)\s*\*', parameter) or re.search(r'\bparent\w*\s*(?:=|$)', parameter):
                if index < argument_count:
                    return False
    return True


def split_top_level(text):
    parts = []
    depth = 0
    start = 0
    for index, character in enumerate(text):
        if character in '(<[{':
            depth += 1
        elif character in ')>]}':
            depth -= 1
        elif character == ',' and depth == 0:
            parts.append(text[start:index])
            start = index + 1
    parts.append(text[start:])
    return parts


def owned_kind(type_name, name):
    text = (type_name + ' ' + name).lower()
    for kind in ('holder', 'part', 'canvas', 'overlay', 'action', 'watcher', 'thread', 'process', 'timer', 'model', 'tab', 'page', 'shell', 'runner', 'gui'):
        if kind in text:
            return kind
    return 'object'


WRITE_CALL = re.compile(r'\b(\w*(?:[wW]rite|[cC]ommit|[sS]ave|[fF]lush|[sS]tore|[pP]ersist)\w*)\s*\(')


def function_body(inventory, class_name, method):
    """(path, line, body) of the definition Class::method, or None"""
    return inventory.bodies.get((class_name, method))


def scan_timers(inventory, show_all):
    entries = []
    timeout = re.compile(r'&\s*QTimer\s*::\s*timeout')
    single = re.compile(r'\bQTimer\s*::\s*singleShot\s*\(')
    for path, info in inventory.infos.items():
        code = info.code
        for match in CONNECT_TOKEN.finditer(code):
            arguments, _ = split_arguments(code, match.end() - 1)
            if len(arguments) < 3 or not timeout.search(arguments[1][0]):
                continue
            slot = normalize(arguments[-1][0] if len(arguments) == 3 else arguments[3][0])
            line = line_of(code, match.start())
            report_timer(inventory, entries, path, line, normalize(arguments[0][0]), slot, show_all)
        for match in single.finditer(code):
            arguments, _ = split_arguments(code, match.end() - 1)
            if len(arguments) < 3:
                continue
            delay = arguments[0][0].strip()
            slot = normalize(arguments[2][0])
            if delay == '0' and not show_all:
                continue
            report_timer(inventory, entries, path, line_of(code, match.start()), 'singleShot(' + delay + ')', slot, show_all)
        for match in re.finditer(r'\b([A-Za-z_]\w*)\s*::\s*timerEvent\s*\(', code):
            found = function_body(inventory, match.group(1), 'timerEvent')
            if found and WRITE_CALL.search(found[2]):
                calls = sorted({m.group(1) for m in WRITE_CALL.finditer(found[2])})
                entries.append(Entry('timers', 'FLUSH', path, line_of(code, match.start()), 'timerEvent', '{}::timerEvent writes: {}'.format(match.group(1), ', '.join(calls))))
    return entries


def report_timer(inventory, entries, path, line, timer, slot, show_all):
    match = re.match(r'^([A-Za-z_]\w*)\s*::\s*([A-Za-z_]\w*)$', slot)
    if not match:
        if show_all:
            entries.append(Entry('timers', 'TIMER', path, line, 'timeout', '{} -> {} (slot not resolved)'.format(timer, slot)))
        return
    found = function_body(inventory, match.group(1), match.group(2))
    calls = sorted({m.group(1) for m in WRITE_CALL.finditer(found[2])}) if found else []
    calls = [c for c in calls if not re.match(r'^(?:is|has|can|get)', c)]
    if WRITE_CALL.search(match.group(2) + '('):
        # The slot itself is named as a write (writeReadingState, onWriteTimer)
        calls = [match.group(2) + ' (slot)'] + calls
    if calls:
        entries.append(Entry('timers', 'FLUSH', path, line, 'timeout', '{} -> {} writes: {}'.format(timer, slot, ', '.join(calls))))
    elif show_all:
        entries.append(Entry('timers', 'TIMER', path, line, 'timeout', '{} -> {}'.format(timer, slot)))


EXTERNAL_TOKENS = (
    ('speech', re.compile(r'\bnew\s+QTextToSpeech\b|(?:->|\.)\s*say\s*\(|\bnew\s+PDFApp\w*Speech(?:Engine|Controller)\s*\(|\bPDFAppSpeechEngine\s*::\s*create\s*\(')),
    ('process', re.compile(r'\bQProcess\s*::\s*(?:startDetached|execute)\b|\bnew\s+QProcess\b|\bQProcess\s+\w+\s*[;({]|\bQDesktopServices\s*::\s*openUrl\b')),
    ('file watcher', re.compile(r'\bnew\s+QFileSystemWatcher\b|\bQFileSystemWatcher\s+\w+\s*[;{]|->\s*addPaths?\s*\(')),
    ('startTimer', re.compile(r'(?<![\w.>])startTimer\s*\(')),
    ('network', re.compile(r'\bnew\s+QNetworkAccessManager\b|(?:->|\.)\s*(?:get|post|put|head|sendCustomRequest)\s*\(\s*request\b')),
    ('media', re.compile(r'\bnew\s+(?:QMediaPlayer|QSoundEffect|QAudioSink|QAudioOutput)\b')),
    ('grab', re.compile(r'\b(?:grabMouse|grabKeyboard)\s*\(')),
    ('override cursor', re.compile(r'\bQ(?:Gui)?Application\s*::\s*setOverrideCursor\b')),
    ('drag', re.compile(r'\bnew\s+QDrag\b|\bQDrag\s+\w+\s*[({]')),
    ('tooltip', re.compile(r'\b(?:QToolTip|PDFToolTip)\s*::\s*(?:showText|showForWidget)\s*\(')),
    ('status message', re.compile(r'(?:->|\.)\s*(?:showMessage|showStatusMessage|showInfoMessage|setStatusText)\s*\(')),
    ('canvas interface', re.compile(r'\b(?:addInputInterface|registerDrawInterface)\s*\(')),
    ('MCP waiter', re.compile(r'\bnew\s+PDFAppMcp\w*Waiter\b|\bclass\s+PDFAppMcp\w*Waiter\b')),
    ('undo target', re.compile(r'\bsetToolUndoTarget\s*\(|\bsetUndoTarget\s*\(')),
    ('tool mode parts', re.compile(r'(?:->|\.)\s*enter(?:Tool)?Mode\s*\(')),
)
CURSOR = re.compile(r'(?:->|\.)\s*(setCursor|unsetCursor)\s*\(')


def is_function_definition_line(code, offset):
    """The line of offset begins with a return type and Class::method( (a definition, not a call)"""
    start = code.rfind('\n', 0, offset) + 1
    return re.match(r'^\s*(?!return\b|new\b|delete\b)(?:[\w:<>]+[\s*&]+)+$', code[start:offset]) is not None


def scan_external(inventory, show_all):
    entries = []
    for path, info in inventory.infos.items():
        code = info.code
        seen = set()
        for kind, regex in EXTERNAL_TOKENS:
            for match in regex.finditer(code):
                line = line_of(code, match.start())
                if is_declaration_of(code, match.start()) or is_function_definition_line(code, match.start()):
                    continue
                if (kind, line) in seen:
                    continue
                seen.add((kind, line))
                text = code[match.start():code.find('\n', match.start())].strip()
                if kind == 'status message':
                    receiver = call_receiver(code, match.start() + (2 if code[match.start()] == '-' else 1))
                    if receiver and re.search(r'search|field|edit', receiver, re.IGNORECASE):
                        continue
                    # A message in an own status bar dies with the object: only a foreign bar is listed
                    receiver_status, receiver_reason = inventory.classify(info, receiver, match.start()) if receiver else ('self', 'this')
                    if receiver_status in ('self', 'child') and not show_all:
                        continue
                    text = '{} ({})'.format(text, receiver_reason)
                if kind == 'drag' and not re.search(r'\bnew\s+QDrag\b|\bQDrag\s+\w+\s*[({]|exec\s*\(', text) and not show_all:
                    continue
                entries.append(Entry('external', 'TOKEN', path, line, kind, text[:110]))
        for match in CURSOR.finditer(code):
            receiver = call_receiver(code, match.start() + (2 if code[match.start()] == '-' else 1))
            status, reason = inventory.classify(info, receiver, match.start()) if receiver else ('self', 'this')
            if status not in ('foreign', 'created') and not show_all:
                continue
            entries.append(Entry('external', 'TOKEN' if status in ('foreign', 'created') else 'INTERNAL', path, line_of(code, match.start()), 'cursor on foreign widget', '{} {} ({})'.format(normalize(receiver), match.group(1), reason)))
    return entries


SYNC_KINDS = (
    ('M1 tab', re.compile(r'(?i)\b(?:m_)?\w*tab\b(?!le|ar|bar)|\btab\w*$')),
    ('M2 page', re.compile(r'(?i)\b(?:m_)?\w*page\b|\bpage\w*$')),
    ('M3 runner', re.compile(r'(?i)runner')),
    ('M3 shell', re.compile(r'(?i)shell')),
    ('M3 gui', re.compile(r'(?i)\bgui\b|\bm_gui\b|toolgui')),
    ('M3 overlay', re.compile(r'(?i)overlay')),
    ('M3 part', re.compile(r'(?i)\bpart\b|\bparts\b|part\w*$|holder|\bm_\w*[pP]art\b')),
    ('M5 row', re.compile(r'(?i)row|card|section|callout|tile|item[wW]idget|chip|entry[wW]idget|reply|line[wW]idget|banner|badge')),
    ('M6 popup', re.compile(r'(?i)popup|popover|dialog|menu|messagebox|tooltip|scrim|flyout|confirm')),
    ('M5 panel', re.compile(r'(?i)panel|bar\b|toolbar|sidebar|widget[s]?\b|content|view\b')),
    ('M7 window', re.compile(r'(?i)window|context|frame\b|m_stack')),
    ('M8 MCP', re.compile(r'(?i)mcp|server|host|controller')),
)


def scan_sync_deletes(inventory, show_all):
    entries = []
    delete_token = re.compile(r'\bdelete\s+(?!\[)([^;]+);|\bqDeleteAll\s*\(([^;]+)\)\s*;')
    later_token = re.compile(r'([A-Za-z_][\w\->.\[\]()]*?)\s*(?:->|\.)\s*deleteLater\s*\(|(?<![\w>.])deleteLater\s*\(')
    reset_token = re.compile(r'\b([A-Za-z_]\w*)\s*\.\s*reset\s*\(\s*\)')
    for path, info in inventory.infos.items():
        code = info.code
        for regex, how in ((delete_token, 'delete'), (later_token, 'deleteLater'), (reset_token, 'reset')):
            for match in regex.finditer(code):
                if how == 'delete':
                    target = (match.group(1) or match.group(2) or '').strip()
                    if match.group(2):
                        how = 'qDeleteAll'
                elif how == 'deleteLater':
                    target = (match.group(1) or 'this').strip()
                else:
                    target = match.group(1)
                if target in ('ui', 'm_ui') or re.match(r'^\w+\s*(?:->|\.)\s*\w+\s*\(', target) and 'takeAt' in target:
                    if target in ('ui', 'm_ui'):
                        continue
                function = info.function_at(match.start())
                in_destructor = bool(function and function[2].startswith('~'))
                class_name = function[1] if function else ''
                subject = target if target != 'this' else class_name
                group = ''
                for name, regex_kind in SYNC_KINDS:
                    if regex_kind.search(subject):
                        group = name
                        break
                if how == 'reset':
                    status_type = inventory_reset_is_qobject(inventory, info, target)
                    if not status_type:
                        continue
                if how != 'deleteLater' and re.search(r'^item$|replaceWidget|takeAt\s*\(|^\w*[lL]ayout$', target):
                    # A layout item / a layout (no QObject of a kind, or one without a subtree of its own)
                    if not show_all:
                        continue
                    group = 'layout item'
                if not group:
                    # A synchronous delete outside a destructor is listed whatever its name (rebuilds, chapter 2 M5)
                    if not show_all and (how == 'deleteLater' or in_destructor):
                        continue
                    group = 'OTHER'
                if how == 'deleteLater':
                    letter = 'L'
                elif in_destructor:
                    letter = 'D'
                else:
                    letter = 'R'
                status = 'DEFERRED' if letter == 'L' else ('DESTRUCTOR' if letter == 'D' else 'SYNC')
                if letter == 'D' and not show_all:
                    # Deletion order in a destructor runs when the owner dies (deferred after A): listed, short
                    pass
                where = '{}::{}'.format(class_name, function[2]) if function else '?'
                entries.append(Entry('sync-deletes', status, path, line_of(code, match.start()), group, '{} {} in {}'.format(how, ' '.join(target.split())[:60], where)))
    return entries


def inventory_reset_is_qobject(inventory, info, name):
    for text in (info.header, info.code):
        match = re.search(r'(?:std::unique_ptr|QScopedPointer)\s*<\s*([A-Za-z_][\w:]*)\s*>\s*' + re.escape(name) + r'\b', text)
        if match and inventory.is_qobject(match.group(1).split('::')[-1]):
            return True
    return False


def scan_destroyed(inventory, show_all):
    entries = []
    for path, info in inventory.infos.items():
        code = info.code
        for match in CONNECT_TOKEN.finditer(code):
            arguments, _ = split_arguments(code, match.end() - 1)
            if len(arguments) < 3 or not re.search(r'::\s*destroyed\b', arguments[1][0]):
                continue
            sender = normalize(arguments[0][0])
            slot = normalize(arguments[-1][0] if len(arguments) == 3 else arguments[3][0])
            receiver = normalize(arguments[2][0]) if len(arguments) >= 4 else '(functor)'
            entries.append(Entry('destroyed', 'DESTROYED', path, line_of(code, match.start()), 'destroyed', '{} -> {} {}'.format(sender, receiver, slot)))
    return entries


CLASS_HEAD = re.compile(r'(?<!enum\s)\b(?:class|struct)\s+(?:[A-Z0-9_]+_EXPORT\s+)?([A-Za-z_]\w*)\b[^;{}()]*\{')
TEMPLATE_ARGUMENTS = r'(?:\s*<[^;<>]*(?:<[^;<>]*>[^;<>]*)*>)?'
MEMBER_VALUE = re.compile(r'^[ \t]*(?:mutable\s+)?([A-Za-z_][\w:]*' + TEMPLATE_ARGUMENTS + r')\s+(m_\w+)\s*(?:;|\{([^;{}]*)\}\s*;)', re.MULTILINE)
MEMBER_UNIQUE = re.compile(r'^[ \t]*(?:mutable\s+)?(?:std::unique_ptr|QScopedPointer)\s*<\s*([A-Za-z_][\w:]*' + TEMPLATE_ARGUMENTS + r')\s*>\s*(m_\w+)\s*(?:;|\{\s*\}\s*;|=\s*nullptr\s*;)', re.MULTILINE)
MEMBER_POINTER = re.compile(r'^[ \t]*(?:mutable\s+)?([A-Za-z_][\w:]*' + TEMPLATE_ARGUMENTS + r')\s*\*\s*(m_\w+)\s*(?:;|\{\s*(?:nullptr)?\s*\}\s*;|=\s*nullptr\s*;)', re.MULTILINE)


def class_spans(code):
    """[(start, end, name)] of the class / struct bodies of \\p code (nested ones too)"""
    spans = []
    for match in CLASS_HEAD.finditer(code):
        depth = 0
        for index in range(match.end() - 1, len(code)):
            character = code[index]
            if character == '{':
                depth += 1
            elif character == '}':
                depth -= 1
                if depth == 0:
                    spans.append((match.end(), index, match.group(1)))
                    break
    return spans


def enclosing_class(spans, offset):
    """The innermost class of \\p spans containing \\p offset (its name, or '')"""
    best = None
    for start, end, name in spans:
        if start <= offset < end and (best is None or start > best[0]):
            best = (start, end, name)
    return best[2] if best else ''


def scan_members(inventory, show_all):
    """QObject members held by value, by std::unique_ptr or by a pointer to an object created without a parent: they
    are outside the subtree of their object, so retire neither stops them nor cuts their connections (PA3b, PA7). A
    member is fine when it becomes a child (constructed with this, setParent(this), handed to a layout / an add*
    call), when it gets setOwner, or when all its connections go through PDFDeferredDeletion::connect to a receiver
    inside the object."""
    entries = []
    # The files with the definitions of the member functions of each class
    class_files = {}
    for path, info in inventory.infos.items():
        for function in info.functions:
            class_files.setdefault(function[1], set()).add(path)

    for path, info in inventory.infos.items():
        code = info.code
        spans = class_spans(code)
        if not spans:
            continue
        members = []
        for regex, holder in ((MEMBER_VALUE, 'value'), (MEMBER_UNIQUE, 'unique_ptr'), (MEMBER_POINTER, 'pointer')):
            for match in regex.finditer(code):
                type_text = ' '.join(match.group(1).split())
                type_name = type_text.split('<')[0].split('::')[-1]
                if type_name in ('return', 'delete', 'new', 'else', 'case', 'const', 'auto', 'Q_EMIT', 'QPointer', 'PDFWatchedPointer'):
                    continue
                if not inventory.is_qobject(type_name) or type_name in ('QApplication', 'QCoreApplication', 'QGuiApplication'):
                    continue
                class_name = enclosing_class(spans, match.start())
                if not class_name:
                    continue
                initializer = match.group(3) if holder == 'value' and match.lastindex and match.lastindex >= 3 else None
                members.append((match.start(), class_name, holder, type_name, match.group(2), initializer))

        for offset, class_name, holder, type_name, name, initializer in members:
            files = {path} | class_files.get(class_name, set())
            counterpart = os.path.splitext(path)[0] + '.cpp'
            if counterpart in inventory.infos:
                files.add(counterpart)
            escaped = re.escape(name)
            reason = ''
            if initializer and re.search(r'\bthis\b', initializer):
                reason = 'initialized with this'
            for file in sorted(files):
                text = inventory.infos[file].code
                if reason:
                    break
                if re.search(r'[:,]\s*' + escaped + r'\s*[({]\s*this\s*[)}]', text):
                    reason = 'constructed with this'
                elif re.search(r'\b' + escaped + r'\s*(?:\.|->)\s*setParent\s*\(\s*(?!nullptr\b)\w', text):
                    reason = 'setParent'
                elif re.search(r'\bsetOwner\s*\(\s*(?:&\s*)?' + escaped + r'\b', text):
                    reason = 'setOwner'
                elif re.search(ADOPTION.format(name=escaped), text):
                    reason = 'adopted'
                elif holder != 'value':
                    # The creation: a parent other than nothing makes a child (or a member parented elsewhere, which
                    # the section owned lists)
                    creation = re.search(r'(?:\b' + escaped + r'\s*=\s*|\b' + escaped + r'\s*[({]\s*|\b' + escaped + r'\s*\.\s*reset\s*\(\s*)(?:new\s+[\w:]+\s*(?:<[^;(){}]*>)?|std::make_unique\s*<[^;(){}]*>)\s*([({])', text)
                    if creation:
                        arguments, _ = split_arguments(text, creation.end() - 1)
                        arguments = [normalize(a[0]) for a in arguments if a[0].strip()]
                        if arguments and arguments[-1] not in ('nullptr', 'NULL', '0') and (arguments[-1] == 'this' or takes_parent_after(inventory, type_name, len(arguments)) is False):
                            reason = 'created with parent ' + arguments[-1][:30]
                        elif re.search(r'\b(?:add|insert|append|set\w*Widget|set\w*Menu)\w*\s*\([^;]*?\b' + escaped + r'\b(?!\s*(?:->|\.))', text):
                            reason = 'handed over'
                    elif holder == 'pointer':
                        # Never created here: set from outside (a foreign object, not owned)
                        reason = 'not created here'
            # The connections from / to the member in the files of the class
            unique_name = sum(1 for member in members if member[4] == name) == 1
            plain = []
            helper = []
            for file in sorted(files):
                file_info = inventory.infos[file]
                text = file_info.code
                file_spans = spans if file == path else class_spans(text)
                for match in CONNECT_TOKEN.finditer(text):
                    if is_declaration_of(text, match.start()):
                        continue
                    function = file_info.function_at(match.start())
                    # A member function defined outside the class or inline in its body; a name declared by one class
                    # of the header only is that member in the header and its .cpp (a function head the heuristics
                    # of FileInfo did not find)
                    if (function is None or function[1] != class_name) and enclosing_class(file_spans, match.start()) != class_name:
                        if not (unique_name and file in (path, counterpart)):
                            continue
                    arguments, _ = split_arguments(text, match.end() - 1)
                    if len(arguments) < 3:
                        continue
                    sender = normalize(arguments[0][0])
                    receiver = normalize(arguments[2][0]) if len(arguments) >= 4 else ''
                    if sender != name and receiver != name:
                        continue
                    where = '{}:{}'.format(os.path.basename(file), line_of(text, match.start()))
                    if call_receiver(text, match.start()) == 'PDFDeferredDeletion::':
                        helper.append(where)
                    else:
                        plain.append(where)
            line = line_of(code, offset)
            detail = '{} {} of {} ({}){}{}'.format(type_name, name, class_name, holder, '; plain connect ' + ', '.join(plain) if plain else '', '; helper ' + ', '.join(helper) if helper else '')
            owner_is_qobject = inventory.is_qobject(class_name)
            # A stop in the hook retire is not enough: its plain connection stays, check 9 of the lifetime sweeps
            # (LifetimeVictimCheck) reports a living sender outside the retired subtree
            hook = inventory.bodies.get((class_name, 'retire'))
            if not reason and hook and re.search(r'\b' + escaped + r'\s*(?:\.|->)\s*(?:stop|cancel|disconnect)\s*\(', hook[2]):
                detail += ' (stopped in the hook retire, the connection stays)'
            if not reason and not plain and helper:
                reason = 'connected only through PDFDeferredDeletion::connect'
            if reason == 'not created here':
                status = 'NOT-OWNED'
                detail += ' - ' + reason
            elif reason == 'connected only through PDFDeferredDeletion::connect':
                status = 'HANDLED'
                detail += ' - ' + reason
            elif reason:
                status = 'CHILD'
                detail += ' - ' + reason
            elif not owner_is_qobject:
                status = 'NO-QOBJECT'
                detail += ' - the owner is no QObject'
            elif type_name == 'QTimer':
                status = 'PARENTLESS-TIMER'
            elif plain:
                status = 'PARENTLESS-SENDER'
            else:
                status = 'PARENTLESS'
            entries.append(Entry('members', status, path, line, holder, detail))
    return entries


SCANNERS = {
    'connections': scan_connections,
    'filters': scan_filters,
    'owned': scan_owned,
    'timers': scan_timers,
    'external': scan_external,
    'sync-deletes': scan_sync_deletes,
    'destroyed': scan_destroyed,
    'members': scan_members,
}

LISTED = {
    'connections': ('CERTAIN', 'LIKELY', 'UNKNOWN', 'OWNED', 'OUTWARD', 'NO-CONTEXT', 'DISCONNECT', 'DISCONNECT-ALL', 'DISCONNECT-HANDLE'),
    'filters': ('FOREIGN', 'OWNED', 'UNKNOWN'),
    'owned': ('OWNED-WINDOW', 'OWNED-FOREIGN', 'OWNED-UNKNOWN', 'OWNED-NONE', 'OWNED-NOPARENT', 'OWNED-MEMBER'),
    'timers': ('FLUSH',),
    'external': ('TOKEN',),
    'sync-deletes': ('SYNC', 'DEFERRED', 'DESTRUCTOR'),
    'destroyed': ('DESTROYED',),
    'members': ('PARENTLESS-TIMER', 'PARENTLESS-SENDER'),
}


def main():
    parser = argparse.ArgumentParser(description='Inventory for step A (deferred deletion) in Pdf4QtLibApp and Pdf4QtLibFluent')
    parser.add_argument('--root', default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))), help='the working copy')
    for section in SECTIONS:
        parser.add_argument('--' + section, action='store_true', help='the section ' + section)
    parser.add_argument('--all', action='store_true', help='also the INTERNAL / informative rows')
    parser.add_argument('--summary', action='store_true', help='counts per file and section instead of the rows')
    arguments = parser.parse_args()

    selected = [section for section in SECTIONS if getattr(arguments, section.replace('-', '_'))] or list(SECTIONS)
    inventory = Inventory(arguments.root)
    results = {section: SCANNERS[section](inventory, arguments.all) for section in selected}

    if arguments.summary:
        files = sorted({entry.path for entries in results.values() for entry in entries})
        print('file;' + ';'.join(selected))
        for path in files:
            counts = []
            for section in selected:
                counts.append(str(sum(1 for entry in results[section] if entry.path == path and (arguments.all or entry.status in LISTED[section]))))
            print(path + ';' + ';'.join(counts))
        return 0

    for section in selected:
        entries = [entry for entry in results[section] if arguments.all or entry.status in LISTED[section]]
        entries.sort(key=lambda entry: (entry.path, entry.line))
        print('=== {} ({} rows) ==='.format(section, len(entries)))
        current = None
        for entry in entries:
            if entry.path != current:
                current = entry.path
                print(entry.path)
            print('  {:5}  {:17} {:18} {}'.format(entry.line, entry.status, entry.kind, entry.detail))
        counts = {}
        for entry in entries:
            counts[entry.status] = counts.get(entry.status, 0) + 1
        print('Summary {}: {}'.format(section, ', '.join('{} {}'.format(status, counts.get(status, 0)) for status in LISTED[section] + (('INTERNAL', 'TIMER', 'OTHER') if arguments.all else ()) if counts.get(status, 0))))
        if section == 'filters':
            found = {os.path.basename(entry.path) for entry in entries}
            missing = [name for name in BASELINE_FILTERS if name not in found]
            print('Baseline RR2-M7 files without a listed filter: {}'.format(', '.join(missing) if missing else 'none'))
        print()
    return 0


if __name__ == '__main__':
    sys.exit(main())
