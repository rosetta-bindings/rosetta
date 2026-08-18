#!/usr/bin/env python3
"""A Qt property editor that has never heard of scene::Mesh.

    ./run.sh qt          # or:  PYTHONPATH=bindings/python python3 drive_qt.py

drive.py prints what a property sheet WOULD contain (`build_panel_spec`, which
returns widget descriptors so the example stays dependency-free). This is the
same dispatch, wired to real widgets — the thing examples/dynamic/qt/ hand-wrote
in C++, here in ~480 lines of Python that no C++ programmer had to touch.

Nothing below names a scene type, a field or a method. Every label, every
widget choice, every slider bound, every button and every menu entry is read at
runtime from the metadata:

    label      <- the "label" annotation, falling back to the field name
    widget     <- the "widget" annotation (slider / checkbox / color / radio
                  / textfield), falling back to the field's KIND
    bounds     <- has_range() / range_min() / range_max()
    choices    <- choices(), or a TypeInfo's enumerator_names() for an enum
    tooltip    <- doc()
    enabled    <- readonly() / writable()
    buttons    <- methods carrying a "button" annotation
    Add menu   <- classes() plus every static method returning an object
    geometry   <- positions() / triangles(), invoked by name

Point the manifest at a different library and this file still runs: it is a
generic editor for anything rosetta's dynamic backend has described. That is
the whole argument for binding the reflection API instead of the classes.

Needs PyQt6 or PySide6:  pip install PyQt6
"""

import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "bindings", "python"))

try:
    import rosetta_meta as meta
except ImportError:
    sys.exit("rosetta_meta not built — run ./run.sh first (see README.md)")

try:
    from PyQt6 import QtCore, QtGui, QtWidgets
except ImportError:  # PySide6 exposes the same scoped-enum API
    try:
        from PySide6 import QtCore, QtGui, QtWidgets
    except ImportError:
        sys.exit("no Qt binding — pip install PyQt6")

Qt = QtCore.Qt


def ann(member):
    """The free-form annotations, as a dict. The core does not interpret these;
    a UI generator is exactly who they are for."""
    return {a.key(): a.value() for a in member.annotations()}


def label_of(field):
    return ann(field).get("label") or field.name()


# =====================================================================
# One field -> one editor
# =====================================================================

def make_editor(inst, field, on_edit, report):
    """Return (widget, refresh) for one FieldInfo, or (None, None) to skip it.

    `refresh` re-reads the live value into the widget — the model can also be
    changed by a button (reset / subdivide) or by another editor.

    The dispatch order is the point: an explicit `widget` annotation wins, then
    choices, then the structural kind. A library that annotates nothing still
    gets a usable sheet from kind alone.
    """
    name = field.name()
    hint = ann(field).get("widget", "")
    kind = field.type().kind()

    def read():
        out = inst.get(name)
        return out.value() if out.ok() else None

    def write(value):
        # Range and choice violations are rejected by the CORE, not by this
        # panel — the same check the Lua and JS drivers hit. Show the reason
        # and put the widget back where the model actually is.
        out = inst.set(name, value)
        if not out.ok():
            report(out.error())
            return False
        on_edit()
        return True

    # ---- explicit widget hints -------------------------------------------
    if hint == "color":
        w = QtWidgets.QPushButton()

        def paint_swatch():
            c = QtGui.QColor(read() or "#000000")
            w.setStyleSheet(f"background:{c.name()}; border:1px solid #888; min-height:20px")
            w.setText(c.name())

        def pick():
            c = QtWidgets.QColorDialog.getColor(QtGui.QColor(read() or "#000000"), w)
            if c.isValid() and write(c.name()):
                paint_swatch()

        w.clicked.connect(pick)
        return w, paint_swatch

    if hint == "radio" and field.choices():
        w = QtWidgets.QWidget()
        row = QtWidgets.QHBoxLayout(w)
        row.setContentsMargins(0, 0, 0, 0)
        group = QtWidgets.QButtonGroup(w)
        for choice in field.choices():
            b = QtWidgets.QRadioButton(choice)
            b.toggled.connect(
                lambda checked, c=choice: checked and read() != c and write(c))
            group.addButton(b)
            row.addWidget(b)
        row.addStretch(1)

        def refresh():
            for b in group.buttons():
                b.setChecked(b.text() == read())

        return w, refresh

    if hint == "slider" and field.has_range():
        # 1000 steps across whatever the annotation declared.
        lo, hi = field.range_min(), field.range_max()
        w = QtWidgets.QWidget()
        row = QtWidgets.QHBoxLayout(w)
        row.setContentsMargins(0, 0, 0, 0)
        s = QtWidgets.QSlider(Qt.Orientation.Horizontal)
        s.setRange(0, 1000)
        readout = QtWidgets.QLabel()
        readout.setMinimumWidth(48)
        row.addWidget(s, 1)
        row.addWidget(readout)

        def moved(step):
            v = lo + (hi - lo) * step / 1000.0
            readout.setText(f"{v:.3g}")
            write(v)

        s.valueChanged.connect(moved)

        def refresh():
            v = read() or 0.0
            s.blockSignals(True)
            s.setValue(int(round((v - lo) / (hi - lo) * 1000)) if hi > lo else 0)
            s.blockSignals(False)
            readout.setText(f"{v:.3g}")

        return w, refresh

    # ---- choices, from an annotation or from the enum itself --------------
    options = field.choices()
    values = None
    if kind == "enum":
        options = field.type().enumerator_names()
        values = field.type().enumerator_values()

    if options:
        w = QtWidgets.QComboBox()
        w.addItems(options)
        w.currentIndexChanged.connect(
            lambda i: write(values[i] if values else options[i]))

        def refresh():
            v = read()
            i = values.index(v) if values and v in values else (
                options.index(v) if v in options else -1)
            w.blockSignals(True)
            w.setCurrentIndex(i)
            w.blockSignals(False)

        return w, refresh

    # ---- structural kinds -------------------------------------------------
    if kind == "boolean":
        w = QtWidgets.QCheckBox()
        w.toggled.connect(write)

        def refresh():
            w.blockSignals(True)
            w.setChecked(bool(read()))
            w.blockSignals(False)

        return w, refresh

    if kind == "number":
        integral = field.type().is_integral()
        w = QtWidgets.QSpinBox() if integral else QtWidgets.QDoubleSpinBox()
        if field.has_range():
            w.setRange(field.range_min(), field.range_max())
        else:
            w.setRange(-1e9, 1e9)
        if not integral:
            w.setDecimals(3)
            w.setSingleStep(0.1)
        w.valueChanged.connect(lambda v: write(int(v) if integral else float(v)))

        def refresh():
            w.blockSignals(True)
            w.setValue(read() or 0)
            w.blockSignals(False)

        return w, refresh

    if kind == "vector":
        # A list edited as text. `element()` is why TypeInfo is structural and
        # not just a type name: the panel needs to know what is INSIDE.
        numeric = field.type().element().kind() == "number"
        w = QtWidgets.QLineEdit()
        w.setPlaceholderText("comma separated " + field.type().element().spelling())

        def commit():
            parts = [p.strip() for p in w.text().split(",") if p.strip()]
            try:
                write([float(p) for p in parts] if numeric else parts)
            except ValueError:
                report(f"{name}: not a list of numbers")

        w.editingFinished.connect(commit)

        def refresh():
            w.setText(", ".join(f"{v:g}" if numeric else str(v) for v in (read() or [])))

        return w, refresh

    if kind == "object":
        # A sub-object comes back as an Instance that PINS its parent, so
        # editing these rows writes straight through into the mesh.
        sub = read()
        if sub is None:
            return None, None
        return PropertySheet(sub, on_edit, report, nested=True), None

    # string, and anything else that renders
    w = QtWidgets.QLineEdit()
    w.editingFinished.connect(lambda: write(w.text()))

    def refresh():
        w.setText(str(read() if read() is not None else ""))

    return w, refresh


# =====================================================================
# One class -> one sheet
# =====================================================================

class PropertySheet(QtWidgets.QWidget):
    """Every editable field of an Instance, in declaration order."""

    def __init__(self, inst, on_edit, report, nested=False):
        super().__init__()
        self.inst = inst
        self.refreshers = []

        form = QtWidgets.QFormLayout(self)
        if nested:
            form.setContentsMargins(0, 0, 0, 0)

        for f in inst.cls().fields():
            if not f.readable():
                # Described, not deleted: the type gate could not marshal it,
                # so it shows up greyed rather than silently vanishing.
                row = QtWidgets.QLabel("<unreadable>")
                row.setEnabled(False)
                form.addRow(label_of(f) + ":", row)
                continue

            editor, refresh = make_editor(inst, f, on_edit, report)
            if editor is None:
                continue
            if f.readonly() or not f.writable():
                editor.setEnabled(False)
            if f.doc():
                editor.setToolTip(f.doc())

            if isinstance(editor, PropertySheet):
                box = QtWidgets.QGroupBox(label_of(f))
                QtWidgets.QVBoxLayout(box).addWidget(editor)
                form.addRow(box)
                self.refreshers.append(editor.refresh)
            else:
                form.addRow(label_of(f) + ":", editor)
                if refresh:
                    self.refreshers.append(refresh)

        self.refresh()

    def refresh(self):
        for r in self.refreshers:
            r()


# =====================================================================
# positions() / triangles(), invoked by name
# =====================================================================

class Viewport(QtWidgets.QWidget):
    """The geometry is PRIVATE in scene.h — reflection only reaches the public
    surface, so the drawing comes from calling positions() and triangles()
    dynamically, exactly as the C++ viewer in examples/dynamic/qt/ does."""

    TILT = math.radians(22)  # a fixed 3/4 view, so a Y-only spin reads as 3D

    def __init__(self):
        super().__init__()
        self.inst = None
        self.verts, self.tris = [], []
        self.drag = None
        self.on_spin = lambda: None  # a plain callback: PyQt and PySide spell
        self.setMinimumSize(320, 320)  # custom signals differently
        self.setCursor(Qt.CursorShape.OpenHandCursor)

    def set_instance(self, inst):
        self.inst = inst
        self.reload()

    def reload(self):
        self.verts, self.tris = [], []
        if self.inst is not None:
            k = self.inst.cls()
            if k.method("positions").valid() and k.method("triangles").valid():
                # `[]` is not optional: default arguments bind at fixed arity,
                # so call("positions") is a TypeError. See README, "Rough edges".
                self.verts = self.inst.call("positions", []).value() or []
                self.tris = self.inst.call("triangles", []).value() or []
        self.update()

    def _field(self, name, default):
        if self.inst is None or not self.inst.cls().field(name).valid():
            return default
        out = self.inst.get(name)
        return out.value() if out.ok() else default

    # ---- drag to spin, wrapped into whatever range the metadata declares ---

    def mousePressEvent(self, ev):
        self.drag = ev.position().x()
        self.setCursor(Qt.CursorShape.ClosedHandCursor)

    def mouseReleaseEvent(self, _):
        self.drag = None
        self.setCursor(Qt.CursorShape.OpenHandCursor)

    def mouseMoveEvent(self, ev):
        f = self.inst.cls().field("spin") if self.inst else None
        if self.drag is None or f is None or not f.valid() or not f.writable():
            return
        x = ev.position().x()
        lo, hi = (f.range_min(), f.range_max()) if f.has_range() else (0.0, 360.0)
        v = lo + (self._field("spin", 0.0) - lo + (x - self.drag)) % (hi - lo)
        self.drag = x
        if self.inst.set("spin", v).ok():
            self.on_spin()  # geometry is unchanged — only the sheet needs telling
            self.update()

    def paintEvent(self, _):
        p = QtGui.QPainter(self)
        p.setRenderHint(QtGui.QPainter.RenderHint.Antialiasing)
        p.fillRect(self.rect(), QtGui.QColor("#20242c"))

        if not self.verts or not self.tris or not self._field("visible", True):
            p.setPen(QtGui.QColor("#667"))
            p.drawText(self.rect(), Qt.AlignmentFlag.AlignCenter,
                       "no geometry" if not self.verts else "hidden")
            return

        spin = math.radians(self._field("spin", 0.0))
        scale = self._field("size", 1.0)
        colour = QtGui.QColor(self._field("colour", "#4488ee"))
        shading = self._field("shading", 1)
        p.setOpacity(max(0.05, min(1.0, self._field("opacity", 1.0))))

        # Spin about Y, then a fixed tilt about X. Orthographic, fit to widget.
        cos, sin = math.cos(spin), math.sin(spin)
        ct, st = math.cos(self.TILT), math.sin(self.TILT)
        pts = []
        for i in range(0, len(self.verts) - 2, 3):
            x, y, z = self.verts[i] * scale, self.verts[i + 1] * scale, self.verts[i + 2] * scale
            rx, rz = x * cos + z * sin, -x * sin + z * cos
            pts.append((rx, y * ct - rz * st, y * st + rz * ct))

        span = max(max(abs(c) for pt in pts for c in pt[:2]), 1e-6) * 2.2
        cx, cy = self.width() / 2, self.height() / 2
        k = min(self.width(), self.height()) / span

        def to_px(pt):
            return QtCore.QPointF(cx + pt[0] * k, cy - pt[1] * k)

        faces = []
        for i in range(0, len(self.tris) - 2, 3):
            a, b, c = self.tris[i], self.tris[i + 1], self.tris[i + 2]
            if max(a, b, c) < len(pts):
                faces.append((a, b, c))
        faces.sort(key=lambda f: sum(pts[i][2] for i in f))  # painter's algorithm

        wireframe = shading == 2
        p.setPen(QtGui.QPen(colour.darker(160), 1))
        for a, b, c in faces:
            poly = QtGui.QPolygonF([to_px(pts[a]), to_px(pts[b]), to_px(pts[c])])
            if wireframe:
                p.setBrush(Qt.BrushStyle.NoBrush)
                p.setPen(QtGui.QPen(colour, 1))
            else:
                ux, uy, uz = (pts[b][i] - pts[a][i] for i in range(3))
                vx, vy, vz = (pts[c][i] - pts[a][i] for i in range(3))
                nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
                n = math.sqrt(nx * nx + ny * ny + nz * nz) or 1.0
                lit = max(0.15, (0.3 * nx + 0.5 * ny + 0.8 * nz) / n)
                p.setBrush(colour.darker(int(215 - 115 * lit)))
            p.drawPolygon(poly)


# =====================================================================
# The window: menus and buttons, also from metadata
# =====================================================================

class Window(QtWidgets.QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("rosetta — a generic editor over reflected metadata")
        self.inst = None

        self.viewport = Viewport()
        self.viewport.on_spin = self.refresh_sheet
        self.sheet_host = QtWidgets.QScrollArea()
        self.sheet_host.setWidgetResizable(True)
        self.sheet_host.setMinimumWidth(400)
        self.buttons = QtWidgets.QHBoxLayout()

        right = QtWidgets.QWidget()
        col = QtWidgets.QVBoxLayout(right)
        self.heading = QtWidgets.QLabel()
        self.heading.setStyleSheet("font-weight:600")
        col.addWidget(self.heading)
        col.addWidget(self.sheet_host, 1)
        col.addLayout(self.buttons)

        split = QtWidgets.QWidget()
        row = QtWidgets.QHBoxLayout(split)
        row.addWidget(self.viewport, 1)
        row.addWidget(right)
        self.setCentralWidget(split)
        self.statusBar().showMessage("Ready")

        self.build_new_menu()
        self.call_menu = self.menuBar().addMenu("&Call")

    def build_new_menu(self):
        """Every constructible class, plus every static method that returns an
        object — the Add-menu scan drive.py does in one comprehension."""
        menu = self.menuBar().addMenu("&New")
        for k in meta.classes():
            if not k.is_abstract() and k.ctors():
                a = menu.addAction(f"{k.qualified()}()")
                a.setToolTip(k.doc())
                a.triggered.connect(lambda _=False, k=k: self.construct(k))
            for m in k.methods():
                if m.is_static() and m.ret().kind() == "object" and m.callable():
                    a = menu.addAction(f"{k.qualified()}::{m.name()}()")
                    a.setToolTip(m.doc())
                    a.triggered.connect(lambda _=False, k=k, m=m: self.factory(k, m))
            menu.addSeparator()

    # ---- construction -----------------------------------------------------

    def construct(self, k):
        out = k.create([])
        self.adopt(out, f"{k.qualified()}()")

    def factory(self, k, m):
        args = self.ask_args(m)
        if args is None:
            return
        self.adopt(k.call_static(m.name(), args), f"{k.qualified()}::{m.name()}()")

    def ask_args(self, m):
        """A dialog built from params() — names, kinds and all."""
        params = [p for p in m.params() if not p.is_out()]
        if not params:
            return []
        dlg = QtWidgets.QDialog(self)
        dlg.setWindowTitle(m.name())
        form = QtWidgets.QFormLayout(dlg)
        editors = []
        for i, p in enumerate(params):
            if p.type().kind() != "number":
                QtWidgets.QMessageBox.information(
                    self, m.name(),
                    f"no editor for a {p.type().spelling()} argument")
                return None
            integral = p.type().is_integral()
            w = QtWidgets.QSpinBox() if integral else QtWidgets.QDoubleSpinBox()
            w.setRange(0, 4096) if integral else w.setRange(-1e6, 1e6)
            w.setValue(16 if integral else 1.0)
            # "argN" until parameter names are reflected — fall back to position.
            form.addRow((p.name() if not p.name().startswith("arg") else f"#{i}") + ":", w)
            editors.append((w, p.type().is_integral()))
        box = QtWidgets.QDialogButtonBox(
            QtWidgets.QDialogButtonBox.StandardButton.Ok |
            QtWidgets.QDialogButtonBox.StandardButton.Cancel)
        box.accepted.connect(dlg.accept)
        box.rejected.connect(dlg.reject)
        form.addRow(box)
        if dlg.exec() != QtWidgets.QDialog.DialogCode.Accepted:
            return None
        return [int(w.value()) if integral else float(w.value()) for w, integral in editors]

    def adopt(self, outcome, what):
        if not outcome.ok():
            self.statusBar().showMessage(f"{what}: {outcome.error()}")
            return
        self.inst = outcome.value()
        self.rebuild()
        self.statusBar().showMessage(what)

    # ---- the sheet and the button row -------------------------------------

    def rebuild(self):
        k = self.inst.cls()
        self.heading.setText(k.qualified() + (f" — {k.doc()}" if k.doc() else ""))
        self.sheet_host.setWidget(
            PropertySheet(self.inst, self.on_edit, self.statusBar().showMessage))

        while self.buttons.count():
            old = self.buttons.takeAt(0).widget()
            if old:  # the trailing stretch has no widget
                # setParent(None) hides it NOW; deleteLater alone leaves the
                # previous class's buttons on screen until the loop spins.
                old.setParent(None)
                old.deleteLater()
        # Methods the library ASKED to be a button. A method with arguments
        # gets the same dialog the factories use.
        for m in k.methods():
            text = ann(m).get("button")
            if not text or not m.callable():
                continue
            b = QtWidgets.QPushButton(text)
            b.setToolTip(m.doc())
            b.clicked.connect(lambda _=False, m=m: self.invoke(m))
            self.buttons.addWidget(b)
        self.buttons.addStretch(1)

        self.build_call_menu(k)
        self.viewport.set_instance(self.inst)

    def build_call_menu(self, k):
        """Every instance method, callable or not.

        The dynamic model keeps the WHOLE overload set, unlike the name-keyed
        backends which bind the first and drop the siblings at generation time —
        so `at()` shows up twice, numbered. And a method the type gate could not
        marshal is listed disabled, carrying its skip_reason, rather than being
        silently absent.
        """
        self.call_menu.clear()
        for m in k.methods():
            if m.is_static():
                continue
            text = m.name() + "(" + ", ".join(p.type().spelling() for p in m.params()) + ")"
            if m.overload_count() > 1:
                text += f"   [{m.overload_index() + 1} of {m.overload_count()}]"
            a = self.call_menu.addAction(text)
            a.setToolTip(m.doc())
            if m.callable():
                a.triggered.connect(lambda _=False, m=m: self.invoke(m))
            else:
                a.setEnabled(False)
                a.setToolTip(m.skip_reason())

    def invoke(self, m):
        args = self.ask_args(m)
        if args is None:
            return
        out = self.inst.call(m.name(), args)
        if not out.ok():
            # A failed call names every candidate overload and why it lost —
            # the diagnostic a name-keyed binding structurally cannot produce,
            # having thrown the siblings away at generation time.
            why = self.inst.cls().why_no_match(m.name(), args)
            self.statusBar().showMessage(
                f"{m.name()}: {out.error()}" + (f" | {why}" if why else ""))
            return
        value = out.value()
        self.statusBar().showMessage(
            f"{m.name()}() -> {value}" if m.ret().kind() != "void" else f"{m.name()}()")
        self.on_edit()

    def refresh_sheet(self):
        sheet = self.sheet_host.widget()
        if sheet:
            sheet.refresh()

    def on_edit(self):
        """Any write may have moved anything: re-read the sheet and the mesh."""
        self.refresh_sheet()
        self.viewport.reload()


def main():
    app = QtWidgets.QApplication(sys.argv)
    w = Window()

    # Pick something to start on, without naming it: the first class that has
    # geometry to show, else the first class at all.
    for k in meta.classes():
        if k.method("positions").valid():
            for m in k.methods():
                if m.is_static() and m.ret().kind() == "object" and not m.params():
                    w.adopt(k.call_static(m.name(), []), f"{k.qualified()}::{m.name()}()")
                    break
            break
    if w.inst is None and meta.classes():
        w.construct(meta.classes()[0])

    w.resize(1000, 620)
    w.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
