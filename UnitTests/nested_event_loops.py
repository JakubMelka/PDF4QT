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

"""Inventory of the entry points of nested event loops in Pdf4QtLibApp and Pdf4QtLibFluent (A0,
GUI_overhaul/newapp_robustness.md 14-16; the rule of CLAUDE.md: no nested event loop in App / Fluent).

Usage (only the standard library; run it with python -I):

    python -I UnitTests/nested_event_loops.py [--root <working copy>] [--strict] [--all]
    python -I UnitTests/nested_event_loops.py --self-test

It lists, with the file and the line (comments and string literals are skipped):

- ENTRY: an entry into a nested event loop - every exec token, which is no function definition / declaration and
  whose receiver is no QDrag (a variable, a cast, findChild, an element, a dereference, this, a base class, the
  static QMenu::exec), every QEventLoop token, processEvents, the static dialogs of Qt (QFileDialog::get*,
  QMessageBox::*, QInputDialog::get*, QColorDialog::getColor, QFontDialog, QPrintDialog, QProgressDialog,
  QErrorMessage), a QToolButton with a menu (every setMenu token - except setMenu(nullptr) and a receiver of an own
  class with its own setMenu, COVERED - and every InstantPopup / MenuButtonPopup / DelayedPopup token except the
  feature flags of QStyleOptionToolButton: QMenuPrivate::exec of Qt), Qt's calendar (new QCalendarWidget, a class
  derived from it without the markers HAS_OWN_MONTH_MENU and HAS_OWN_YEAR_EDITOR - static constexpr bool ... = true
  in its class body -, setCalendarPopup / setCalendarPopupEnabled not false, unless the editor installs a covered
  calendar by setCalendarWidget(new <calendar>)), a spin box without its own context menu
  (QAbstractSpinBox::contextMenuEvent runs QMenu::exec), and the calls of the former synchronous helpers of the
  application, which ran such a loop (removed at the integration of A0; a call brought back is found:
  PDFAppDocumentChoiceDialog::run, PDFAppDocumentActionDialogs::run /
  confirm*, PDFAppFileDialogs::get*, PDFAppPanelFileDialogs::get*, PDFMessageBox::information / question /
  warning / critical, waitForLeaveAnswer, saveAndWait);
- EXCEPTION: the named exceptions of the owner (QR3.2): QDrag::exec (own drags, QAbstractItemView::startDrag,
  the text drags of QTextEdit / QPlainTextEdit), DocumentPropertiesW with DM_IN_PROMPT (the native properties
  of a printer) and QSessionManager::allowsInteraction of XSMP (a loop of Qt on X11) - each with a guard after
  the call;
- INFO (with --all): a loop of Pdf4QtLibWidgets in a class, which the application uses (RR3-L5: reachable only
  without PDFToolManager / PDFWidgetAnnotationManager::setUserInterfaceDelegated(true), which the script checks).

The exit code is 1 with --strict, when an ENTRY is found (the integration of A0 requires none), else 0.

--self-test scans the probe snippets UnitTests/nested_event_loops_probe (the layout of the libraries, never compiled)
and checks every line marked "// EXPECT ENTRY / EXCEPTION / COVERED / NONE"; any other ENTRY there fails as well.
"""

import argparse
import os
import re
import sys

LIBRARIES = ('Pdf4QtLibApp', 'Pdf4QtLibFluent')
WIDGETS = 'Pdf4QtLibWidgets'
EXTENSIONS = ('.cpp', '.h')

STATIC_DIALOGS = (
    (re.compile(r'\bQFileDialog\s*::\s*get\w+\s*\('), 'QFileDialog static'),
    (re.compile(r'\bQMessageBox\s*::\s*(?:information|question|warning|critical|about|aboutQt)\s*\('), 'QMessageBox static'),
    (re.compile(r'\bQInputDialog\s*::\s*get\w+\s*\('), 'QInputDialog static'),
    (re.compile(r'\bQColorDialog\s*::\s*getColor\s*\('), 'QColorDialog static'),
    (re.compile(r'\bQFontDialog\b'), 'QFontDialog'),
    (re.compile(r'\bQPrintDialog\b'), 'QPrintDialog'),
    (re.compile(r'\bQPrintPreviewDialog\b'), 'QPrintPreviewDialog'),
    (re.compile(r'\bQPageSetupDialog\b'), 'QPageSetupDialog'),
    (re.compile(r'\bQProgressDialog\b'), 'QProgressDialog (processEvents when modal)'),
    (re.compile(r'\bQErrorMessage\b'), 'QErrorMessage'),
    (re.compile(r'\bprocessEvents\s*\('), 'processEvents'),
)

# The synchronous helpers of the application, which run a nested event loop inside (their callers)
WRAPPERS = (
    (re.compile(r'\bPDFAppDocumentChoiceDialog\s*::\s*run\s*\('), 'PDFAppDocumentChoiceDialog::run (exec)'),
    (re.compile(r'\bPDFAppDocumentActionDialogs\s*::\s*(?:run|confirmLink|confirmProgram|confirmFile)\s*\('), 'PDFAppDocumentActionDialogs (exec)'),
    (re.compile(r'\bPDFAppFileDialogs\s*::\s*get(?:OpenFileNames|OpenDocumentFileNames|OpenInputFileNames|ExistingDirectory|SaveFileName|SaveBackupFileName|OpenBackupFileName|PrintFileName)\s*\('), 'PDFAppFileDialogs::get* (static QFileDialog / exec)'),
    (re.compile(r'\bPDFAppPanelFileDialogs\s*::\s*get(?:SaveFileName|OpenFileName)\s*\('), 'PDFAppPanelFileDialogs::get* (static QFileDialog)'),
    (re.compile(r'\bPDFMessageBox\s*::\s*(?:information|question|warning|critical|showMessage)\s*\('), 'PDFMessageBox static (exec)'),
    (re.compile(r'\bwaitForLeaveAnswer\s*\('), 'waitForLeaveAnswer (QEventLoop)'),
    (re.compile(r'\bsaveAndWait\s*\('), 'saveAndWait (QEventLoop)'),
)

# The exec calls of Pdf4QtLibWidgets (INFO rows: a receiver variable)
EXEC_CALL = re.compile(r'([A-Za-z_]\w*)\s*(?:\(\s*\))?\s*(?:->|\.)\s*exec\s*\(')
# App / Fluent: every exec token, whatever its receiver (a variable, a cast, a findChild, an element, this, a base
# class, the static QMenu::exec); only a function definition / declaration and a QDrag receiver are not an entry
EXEC_TOKEN = re.compile(r'\bexec\s*\(')
QEVENTLOOP_TOKEN = re.compile(r'\bQEventLoop\b')
# Every setMenu token and every popup mode of a QToolButton with a menu, whatever the qualifier (QToolButton::showMenu
# runs QMenuPrivate::exec)
SET_MENU_TOKEN = re.compile(r'\bsetMenu\s*\(')
POPUP_MODE_TOKEN = re.compile(r'\b(InstantPopup|MenuButtonPopup|DelayedPopup)\b')
# The calendar of Qt: its month button is an InstantPopup tool button, its year editor a QSpinBox (both exec)
NEW_QCALENDAR = re.compile(r'\bnew\s+QCalendarWidget\b')
CALENDAR_POPUP_TOKEN = re.compile(r'\b(setCalendarPopup|setCalendarPopupEnabled)\s*\(\s*([^)]*)\)')
SET_CALENDAR_WIDGET = re.compile(r'\bsetCalendarWidget\s*\(\s*new\s+([A-Za-z_]\w*)\s*\(')
CALENDAR_BASES = ('QCalendarWidget',)
# The markers of an own calendar, which replaces both loops of Qt's calendar (declared in its class body, code - not
# a comment): its own month menu shown by popup() and its own year editor with an own context menu
CALENDAR_MARKERS = (
    re.compile(r'\bstatic\s+constexpr\s+bool\s+HAS_OWN_MONTH_MENU\s*=\s*true\s*;'),
    re.compile(r'\bstatic\s+constexpr\s+bool\s+HAS_OWN_YEAR_EDITOR\s*=\s*true\s*;'),
)
RECEIVER_IDENTIFIER = re.compile(r'([A-Za-z_]\w*)\s*$')
RECEIVER_QUALIFIER = re.compile(r'([A-Za-z_]\w*)\s*::\s*$')
RECEIVER_CAST = re.compile(r'<\s*(?:const\s+)?([A-Za-z_][\w:]*)\s*\*\s*>')
START_DRAG_BASE = re.compile(r'\b(?:BaseClass|Q\w*View|QAbstractItemView)\s*::\s*startDrag\s*\(')
DOCUMENT_PROPERTIES = re.compile(r'\bDocumentPropertiesW\s*\(')
ALLOWS_INTERACTION = re.compile(r'(?:->|\.)\s*allowsInteraction\s*\(')
CLASS_DECLARATION = re.compile(r'\bclass\s+(?:[A-Z0-9_]+_EXPORT\s+)?([A-Za-z_]\w*)\s*(?:final\s*)?:\s*public\s+([A-Za-z_][\w:]*)')
NEW_OBJECT = re.compile(r'\bnew\s+([A-Za-z_]\w*)\s*\(')
DEFINITION_PREFIX = re.compile(r'^\s*(?:(?:static|virtual|inline|explicit|const|Q_INVOKABLE)\s+)*[\w:<>]+[\s*&]+(?:[A-Za-z_]\w*\s*::\s*)*$')
FUNCTION_DEFINITION = re.compile(r'^[A-Za-z_][\w:<>*&\s]*?\b([A-Z]\w*)\s*::\s*(~?[A-Za-z_]\w*)\s*\([^;]*$', re.MULTILINE)

SPIN_BOX_BASES = ('QSpinBox', 'QDoubleSpinBox', 'QAbstractSpinBox', 'QDateTimeEdit', 'QDateEdit', 'QTimeEdit')
TEXT_EDIT_BASES = ('QTextEdit', 'QPlainTextEdit', 'QTextBrowser')


class Entry:
    def __init__(self, status, path, line, kind, detail=''):
        self.status = status
        self.path = path
        self.line = line
        self.kind = kind
        self.detail = detail


def strip_code(text):
    """The text without comments, string and character literals (their characters become spaces, the lines stay)"""
    result = []
    i = 0
    length = len(text)
    while i < length:
        c = text[i]
        if text.startswith('//', i):
            end = text.find('\n', i)
            end = length if end < 0 else end
            result.append(' ' * (end - i))
            i = end
        elif text.startswith('/*', i):
            end = text.find('*/', i + 2)
            end = length if end < 0 else end + 2
            result.append(''.join('\n' if ch == '\n' else ' ' for ch in text[i:end]))
            i = end
        elif c == 'R' and text.startswith('R"', i) and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == '_')):
            open_paren = text.find('(', i + 2)
            delimiter = text[i + 2:open_paren] if open_paren >= 0 else ''
            end = text.find(')' + delimiter + '"', open_paren + 1) if open_paren >= 0 else -1
            end = length if end < 0 else end + len(delimiter) + 2
            result.append('""' + ''.join('\n' if ch == '\n' else ' ' for ch in text[i + 2:end - 1]) + ' ')
            i = end
        elif c == '"' or (c == "'" and not (i > 0 and text[i - 1].isalnum())):
            quote = c
            j = i + 1
            while j < length and text[j] != quote and text[j] != '\n':
                j += 2 if text[j] == '\\' else 1
            j = min(j + 1, length)
            result.append(quote + ' ' * max(0, j - i - 2) + quote if j - i >= 2 else quote)
            i = j
        elif c == '#' and (i == 0 or text[i - 1] == '\n'):
            # Preprocessor lines (#include <QEventLoop>) are no code of a loop
            end = text.find('\n', i)
            end = length if end < 0 else end
            result.append(' ' * (end - i))
            i = end
        else:
            result.append(c)
            i += 1
    return ''.join(result)


def line_of(text, offset):
    return text.count('\n', 0, offset) + 1


def declared_type(code, name, header='', offset=None):
    """The type of the variable or member \\p name declared in the file or in its header (heuristic), empty when not
    found. With \\p offset the nearest declaration before it wins (a name declared again in another function)."""
    escaped = re.escape(name)
    patterns = (
        r'QPointer\s*<\s*([A-Za-z_][\w:]*)\s*>\s*' + escaped + r'\b',
        r'\b([A-Za-z_][\w:]*)\s*\*\s*' + escaped + r'\s*[=;,)]',
        r'\b([A-Za-z_][\w:]*)\s+' + escaped + r'\s*[;({=]',
    )
    excluded = ('return', 'const', 'auto', 'else', 'delete', 'new', 'case')
    if offset is not None:
        best = None
        for pattern in patterns:
            for match in re.finditer(pattern, code[:offset]):
                if match.group(1) not in excluded and (best is None or match.start() > best.start()):
                    best = match
        if best is not None:
            return best.group(1).split('::')[-1]
    for text in (code, header):
        for pattern in patterns:
            for match in re.finditer(pattern, text):
                type_name = match.group(1)
                if type_name not in excluded:
                    return type_name.split('::')[-1]
    return ''


def header_of(sources, path):
    """The stripped header of a source file (the same name with .h), empty when there is none"""
    return sources.get(os.path.splitext(path)[0] + '.h', '') if path.endswith('.cpp') else ''


def is_definition(code, offset):
    """The match at \\p offset is the definition / declaration of the function (a return type before it on its line)"""
    line_start = code.rfind('\n', 0, offset) + 1
    prefix = code[line_start:offset]
    if not prefix.strip():
        return False
    return bool(DEFINITION_PREFIX.match(prefix)) and not re.search(r'[=(,]|\breturn\b', prefix)


def is_declaration(code, offset):
    """The token at \\p offset is the name of a function in its definition / declaration (int exec() override;
    void PDFMoreButton::setMenu(QMenu* menu)): the statement before it on its line is a return type, optionally with a
    qualifier of the class"""
    statement_start = max(code.rfind('\n', 0, offset), code.rfind(';', 0, offset), code.rfind('{', 0, offset), code.rfind('}', 0, offset)) + 1
    prefix = code[statement_start:offset]
    if not prefix.strip():
        return False
    return bool(DEFINITION_PREFIX.match(prefix)) and not re.search(r'[=(,]|\breturn\b|->|\.', prefix)


def receiver_of(code, offset):
    """The receiver of the call at \\p offset: ('member', name) for name->f( / name.f(, ('expression', type or '') for
    another receiver (a cast, a findChild, an element, a dereference; the type of a cast when found), ('qualified',
    qualifier) for X::f(, ('unqualified', '') for f( (this)"""
    prefix = code[max(0, offset - 300):offset].rstrip()
    if prefix.endswith('->') or prefix.endswith('.'):
        before = prefix[:-2] if prefix.endswith('->') else prefix[:-1]
        before = before.rstrip()
        match = RECEIVER_IDENTIFIER.search(before)
        if match and not before.endswith(')') and not before.endswith(']'):
            return 'member', match.group(1)
        casts = RECEIVER_CAST.findall(before[-120:])
        return 'expression', (casts[-1].split('::')[-1] if casts else '')
    match = RECEIVER_QUALIFIER.search(prefix)
    if match:
        return 'qualified', match.group(1)
    return 'unqualified', ''


def enclosing_class(code, offset, path):
    """The class, whose member function (a .cpp) or whose body (a header) contains \\p offset; empty when unknown"""
    if path.endswith('.cpp'):
        functions = list(FUNCTION_DEFINITION.finditer(code, 0, offset))
        return functions[-1].group(1) if functions else ''
    classes = list(re.finditer(r'\bclass\s+(?:[A-Z0-9_]+_EXPORT\s+)?([A-Za-z_]\w*)\b[^;{]*\{', code[:offset]))
    return classes[-1].group(1) if classes else ''


def resolve_type(code, header, offset, path, form, name, classes):
    """The class of the receiver of a call (empty when unknown): the declared type of a member, the type of a cast,
    the base class for BaseClass:: (its own class otherwise), the enclosing class for an unqualified call"""
    if form == 'member':
        return declared_type(code, name, header, offset)
    if form == 'expression':
        return name
    own_class = enclosing_class(code, offset, path)
    if form == 'qualified':
        if name == 'BaseClass' and own_class in classes:
            return classes[own_class][0]
        return name
    return own_class


def class_body(sources, class_name):
    """The stripped body of the class (between its braces), empty when it is not in the sources"""
    pattern = re.compile(r'\bclass\s+(?:[A-Z0-9_]+_EXPORT\s+)?' + re.escape(class_name) + r'\b[^;{]*\{')
    for code in sources.values():
        match = pattern.search(code)
        if not match:
            continue
        depth = 0
        for index in range(match.end() - 1, len(code)):
            if code[index] == '{':
                depth += 1
            elif code[index] == '}':
                depth -= 1
                if depth == 0:
                    return code[match.end():index]
    return ''


def collect_files(root, library):
    files = []
    base = os.path.join(root, library, 'sources')
    for directory, _, names in os.walk(base):
        for name in sorted(names):
            if name.endswith(EXTENSIONS):
                files.append(os.path.join(directory, name))
    return sorted(files)


def class_hierarchy(sources):
    """class -> (base, path, line) over all the sources"""
    classes = {}
    for path, code in sources.items():
        for match in CLASS_DECLARATION.finditer(code):
            classes[match.group(1)] = (match.group(2).split('::')[-1], path, line_of(code, match.start()))
    return classes


def overrides(sources, class_name, method):
    """The class declares \\p method (in its body, a header or a .cpp)"""
    pattern = re.compile(r'\bclass\s+(?:[A-Z0-9_]+_EXPORT\s+)?' + re.escape(class_name) + r'\b[^;{]*\{')
    for code in sources.values():
        match = pattern.search(code)
        if not match:
            continue
        depth = 0
        for index in range(match.end() - 1, len(code)):
            if code[index] == '{':
                depth += 1
            elif code[index] == '}':
                depth -= 1
                if depth == 0:
                    return re.search(r'\b' + method + r'\s*\(', code[match.end():index]) is not None
    return False


def inherits(classes, class_name, bases):
    """The first base of \\p bases, from which the class derives (within the sources), empty = none"""
    seen = set()
    current = class_name
    while current in classes and current not in seen:
        seen.add(current)
        base = classes[current][0]
        if base in bases:
            return base
        current = base
    return ''


def has_override_in_chain(sources, classes, class_name, method, stop_bases):
    seen = set()
    current = class_name
    while current and current not in seen and current not in stop_bases:
        seen.add(current)
        if overrides(sources, current, method):
            return True
        current = classes[current][0] if current in classes else ''
    return False


def scan(root):
    entries = []
    sources = {}
    for library in LIBRARIES:
        for path in collect_files(root, library):
            with open(path, encoding='utf-8', errors='replace') as file:
                sources[os.path.relpath(path, root).replace('\\', '/')] = strip_code(file.read())

    classes = class_hierarchy(sources)

    # The own calendars, which replace both loops of Qt's calendar (CALENDAR_MARKERS in the class or its own bases),
    # and the date editors, which install one of them (setCalendarWidget(new <covered calendar>) in their members)
    covered_calendars = set()
    for class_name in classes:
        if inherits(classes, class_name, CALENDAR_BASES):
            bodies = []
            current = class_name
            while current in classes and current not in CALENDAR_BASES:
                bodies.append(class_body(sources, current))
                current = classes[current][0]
            if all(any(marker.search(body) for body in bodies) for marker in CALENDAR_MARKERS):
                covered_calendars.add(class_name)
    installing_editors = set()
    for path, code in sources.items():
        for match in SET_CALENDAR_WIDGET.finditer(code):
            if match.group(1) in covered_calendars:
                installing_editors.add(enclosing_class(code, match.start(), path))
    calendar_editors = {class_name for class_name in classes if class_name in installing_editors or inherits(classes, class_name, tuple(installing_editors))}

    for path, code in sources.items():
        header = header_of(sources, path)

        # Every exec token (a dialog, a menu, a loop - whatever the receiver), except a definition and a QDrag
        for match in EXEC_TOKEN.finditer(code):
            if is_declaration(code, match.start()):
                continue
            form, name = receiver_of(code, match.start())
            type_name = resolve_type(code, header, match.start(), path, form, name, classes)
            line = line_of(code, match.start())
            detail = '{} {} ({})'.format(form, name, type_name or '?').replace('  ', ' ')
            if type_name == 'QDrag':
                entries.append(Entry('EXCEPTION', path, line, 'QDrag::exec', detail))
            elif type_name.endswith('EventLoop'):
                entries.append(Entry('ENTRY', path, line, 'QEventLoop::exec', detail))
            elif type_name.endswith('Menu') or inherits(classes, type_name, ('QMenu',)):
                entries.append(Entry('ENTRY', path, line, 'QMenu::exec', detail))
            else:
                entries.append(Entry('ENTRY', path, line, 'exec (QDialog / QMenu / QEventLoop)', detail))

        # Every QEventLoop (an object, a pointer, its flags with processEvents): one entry per line
        loop_lines = sorted({line_of(code, match.start()) for match in QEVENTLOOP_TOKEN.finditer(code)})
        for line in loop_lines:
            entries.append(Entry('ENTRY', path, line, 'QEventLoop', 'QEventLoop'))

        for regex, kind in STATIC_DIALOGS:
            for match in regex.finditer(code):
                entries.append(Entry('ENTRY', path, line_of(code, match.start()), kind, match.group(0).strip()))

        for regex, kind in WRAPPERS:
            for match in regex.finditer(code):
                if is_definition(code, match.start()):
                    continue
                entries.append(Entry('ENTRY', path, line_of(code, match.start()), kind, match.group(0).strip()))

        # A QToolButton with a menu: QToolButton::showMenu runs QMenuPrivate::exec (also the press and hold of
        # DelayedPopup). Every setMenu token, whatever the qualifier; a receiver of an own class, which declares its
        # own setMenu (no menu of a QToolButton: its body is checked by itself), is covered
        for match in SET_MENU_TOKEN.finditer(code):
            if is_declaration(code, match.start()) or re.match(r'\s*nullptr\s*\)', code[match.end():]):
                # A declaration; setMenu(nullptr) takes the menu away
                continue
            form, name = receiver_of(code, match.start())
            type_name = resolve_type(code, header, match.start(), path, form, name, classes)
            line = line_of(code, match.start())
            detail = '{} {} ({})'.format(form, name, type_name or '?').replace('  ', ' ')
            if form != 'qualified' and type_name in classes and has_override_in_chain(sources, classes, type_name, 'setMenu', ()):
                entries.append(Entry('COVERED', path, line, 'setMenu of an own class', detail))
            else:
                entries.append(Entry('ENTRY', path, line, 'QToolButton menu (setMenu -> QMenuPrivate::exec)', detail))
        for match in POPUP_MODE_TOKEN.finditer(code):
            if re.search(r'\bQStyleOptionToolButton\s*::\s*$', code[max(0, match.start() - 80):match.start()]):
                # A feature flag of the style option (painting), not the popup mode of a button
                continue
            entries.append(Entry('ENTRY', path, line_of(code, match.start()), 'QToolButton menu (popup mode -> QMenuPrivate::exec)', match.group(1)))

        # The calendar of Qt (its InstantPopup month button and its QSpinBox year editor run exec)
        for match in NEW_QCALENDAR.finditer(code):
            entries.append(Entry('ENTRY', path, line_of(code, match.start()), 'QCalendarWidget (month menu, year spin box)', 'new QCalendarWidget'))
        for match in CALENDAR_POPUP_TOKEN.finditer(code):
            if is_declaration(code, match.start()) or match.group(2).strip() == 'false':
                continue
            form, name = receiver_of(code, match.start())
            type_name = resolve_type(code, header, match.start(), path, form, name, classes)
            line = line_of(code, match.start())
            detail = '{}({}) - {}'.format(match.group(1), match.group(2).strip(), type_name or '?')
            if form != 'qualified' and type_name in calendar_editors:
                entries.append(Entry('COVERED', path, line, 'calendar popup', detail + ' installs a covered calendar'))
            else:
                entries.append(Entry('ENTRY', path, line, 'calendar popup (QCalendarWidget: month menu, year spin box)', detail))

        for match in START_DRAG_BASE.finditer(code):
            entries.append(Entry('EXCEPTION', path, line_of(code, match.start()), 'Qt drag (QAbstractItemView::startDrag -> QDrag::exec)', match.group(0).strip()))

        for match in DOCUMENT_PROPERTIES.finditer(code):
            statement_end = code.find(';', match.end())
            statement = code[match.start():statement_end if statement_end >= 0 else len(code)]
            if 'DM_IN_PROMPT' in statement:
                entries.append(Entry('EXCEPTION', path, line_of(code, match.start()), 'DocumentPropertiesW (DM_IN_PROMPT)', 'native printer properties'))

        for match in ALLOWS_INTERACTION.finditer(code):
            entries.append(Entry('EXCEPTION', path, line_of(code, match.start()), 'QSessionManager::allowsInteraction (XSMP loop of Qt on X11)', 'session end'))

        # Spin boxes and text edits created directly from the classes of Qt
        for match in NEW_OBJECT.finditer(code):
            class_name = match.group(1)
            if class_name in SPIN_BOX_BASES:
                entries.append(Entry('ENTRY', path, line_of(code, match.start()), 'spin box context menu (QAbstractSpinBox::contextMenuEvent -> QMenu::exec)', class_name))
            elif class_name in TEXT_EDIT_BASES:
                entries.append(Entry('EXCEPTION', path, line_of(code, match.start()), 'text drag (QWidgetTextControl -> QDrag::exec)', class_name))

    # The own spin box classes without their own context menu; the own text edits (drag exception)
    for class_name, (base, path, line) in sorted(classes.items()):
        spin_base = inherits(classes, class_name, SPIN_BOX_BASES)
        if spin_base:
            if has_override_in_chain(sources, classes, class_name, 'contextMenuEvent', SPIN_BOX_BASES):
                entries.append(Entry('COVERED', path, line, 'spin box context menu', '{} : {} (own contextMenuEvent)'.format(class_name, base)))
            else:
                entries.append(Entry('ENTRY', path, line, 'spin box context menu (QAbstractSpinBox::contextMenuEvent -> QMenu::exec)', '{} : {}'.format(class_name, base)))
        elif inherits(classes, class_name, TEXT_EDIT_BASES):
            entries.append(Entry('EXCEPTION', path, line, 'text drag (QWidgetTextControl -> QDrag::exec)', '{} : {}'.format(class_name, base)))
        elif inherits(classes, class_name, CALENDAR_BASES):
            if class_name in covered_calendars:
                entries.append(Entry('COVERED', path, line, 'calendar', '{} : {} (own month menu and year editor)'.format(class_name, base)))
            else:
                entries.append(Entry('ENTRY', path, line, 'QCalendarWidget (month menu, year spin box)', '{} : {} (no HAS_OWN_MONTH_MENU / HAS_OWN_YEAR_EDITOR)'.format(class_name, base)))

    entries.extend(scan_widgets(root, sources))
    return entries


def scan_widgets(root, app_sources):
    """RR3-L5: the loops of Pdf4QtLibWidgets in the classes, which App / Fluent use"""
    entries = []
    widgets_sources = {}
    for path in collect_files(root, WIDGETS):
        with open(path, encoding='utf-8', errors='replace') as file:
            widgets_sources[os.path.relpath(path, root).replace('\\', '/')] = strip_code(file.read())

    all_app_code = '\n'.join(app_sources.values())
    delegated = len(re.findall(r'setUserInterfaceDelegated\s*\(\s*true\s*\)', all_app_code))

    for path, code in widgets_sources.items():
        if not path.endswith('.cpp'):
            continue
        header = header_of(widgets_sources, path)
        loops = []
        for match in EXEC_CALL.finditer(code):
            type_name = declared_type(code, match.group(1), header)
            if type_name != 'QDrag':
                loops.append((match.start(), 'exec of ' + (type_name or match.group(1))))
        for regex, kind in STATIC_DIALOGS:
            for match in regex.finditer(code):
                loops.append((match.start(), kind))
        for offset, kind in loops:
            functions = list(FUNCTION_DEFINITION.finditer(code, 0, offset))
            class_name = functions[-1].group(1) if functions else ''
            method = functions[-1].group(2) if functions else ''
            if class_name and re.search(r'\b' + re.escape(class_name) + r'\b', all_app_code):
                note = 'the class is used by App / Fluent'
                if class_name == 'PDFWidgetAnnotationManager' or path.endswith('pdfadvancedtools.cpp'):
                    note += '; setUserInterfaceDelegated(true) called {} times in App'.format(delegated)
                entries.append(Entry('INFO', path, line_of(code, offset), 'Widgets loop: ' + kind, '{}::{} - {}'.format(class_name, method, note)))
    return entries


EXPECTATION = re.compile(r'//\s*EXPECT\s+(ENTRY|EXCEPTION|COVERED|NONE)\b')


def self_test(probe_root):
    """Scans the probe tree (UnitTests/nested_event_loops_probe: snippets in the layout of the libraries, never
    compiled) and compares the result with the expectations in the snippets: a line with "// EXPECT <STATUS>" must be
    reported with that status (NONE: no ENTRY), and no other line of a probe may be an ENTRY. \\returns the exit code."""
    entries = scan(probe_root)
    found = {}
    for entry in entries:
        found.setdefault((entry.path, entry.line), set()).add(entry.status)

    expected = {}
    for library in LIBRARIES:
        for path in collect_files(probe_root, library):
            relative = os.path.relpath(path, probe_root).replace('\\', '/')
            with open(path, encoding='utf-8', errors='replace') as file:
                for number, text in enumerate(file.read().split('\n'), 1):
                    match = EXPECTATION.search(text)
                    if match:
                        expected[(relative, number)] = match.group(1)

    failures = []
    for (path, line), status in sorted(expected.items()):
        statuses = found.get((path, line), set())
        if status == 'NONE':
            if 'ENTRY' in statuses:
                failures.append('{}:{} expected no entry, found {}'.format(path, line, sorted(statuses)))
        elif status not in statuses:
            failures.append('{}:{} expected {}, found {}'.format(path, line, status, sorted(statuses) or 'nothing'))
    for (path, line), statuses in sorted(found.items()):
        if 'ENTRY' in statuses and expected.get((path, line)) != 'ENTRY':
            failures.append('{}:{} unexpected ENTRY'.format(path, line))

    for failure in failures:
        print('FAIL: ' + failure)
    counts = {}
    for status in expected.values():
        counts[status] = counts.get(status, 0) + 1
    print('Self-test: {} probe lines ({}), {} failures'.format(len(expected), ', '.join('{} {}'.format(count, status) for status, count in sorted(counts.items())), len(failures)))
    return 1 if failures or not expected else 0


def main():
    parser = argparse.ArgumentParser(description='Entry points of nested event loops in Pdf4QtLibApp and Pdf4QtLibFluent (A0)')
    parser.add_argument('--root', default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))), help='the working copy')
    parser.add_argument('--strict', action='store_true', help='exit code 1 when an ENTRY is found')
    parser.add_argument('--all', action='store_true', help='also the covered spin boxes and the reachable loops of Pdf4QtLibWidgets')
    parser.add_argument('--self-test', action='store_true', help='check the detection on the probe snippets (UnitTests/nested_event_loops_probe)')
    arguments = parser.parse_args()

    if arguments.self_test:
        return self_test(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'nested_event_loops_probe'))

    entries = scan(arguments.root)
    shown = [entry for entry in entries if arguments.all or entry.status in ('ENTRY', 'EXCEPTION')]
    order = {'ENTRY': 0, 'EXCEPTION': 1, 'COVERED': 2, 'INFO': 3}
    shown.sort(key=lambda entry: (order[entry.status], entry.kind, entry.path, entry.line))

    current = None
    for entry in shown:
        if (entry.status, entry.kind) != current:
            current = (entry.status, entry.kind)
            print('\n{} - {}'.format(entry.status, entry.kind))
        print('  {}:{}  {}'.format(entry.path, entry.line, entry.detail))

    counts = {}
    for entry in entries:
        counts[entry.status] = counts.get(entry.status, 0) + 1
    print('\nSummary: {} ENTRY, {} EXCEPTION, {} COVERED, {} INFO'.format(counts.get('ENTRY', 0), counts.get('EXCEPTION', 0), counts.get('COVERED', 0), counts.get('INFO', 0)))
    return 1 if arguments.strict and counts.get('ENTRY', 0) else 0


if __name__ == '__main__':
    sys.exit(main())
