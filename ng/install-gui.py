#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Morzilla
"""The graphical installer of the NG desktop: Dolphin NG, Breeze NG, NeoLine.

A small wizard on top of install-desktop.sh (which does the work): pick the parts, pick a look, watch it build.
It draws itself (Fusion + its own colours), because the theme it installs is not there yet.
Start it with install-gui.sh, which finds a Python with Qt bindings and falls back to the text installer.
"""
import os
import shutil
import sys
import urllib.request

try:
    from PyQt6.QtCore import QProcess, QProcessEnvironment, QRectF, QSize, Qt, pyqtSignal as Signal
    from PyQt6.QtGui import QColor, QFont, QLinearGradient, QPainter, QPainterPath, QPen, QPixmap, QTextCursor
    from PyQt6.QtWidgets import (QApplication, QCheckBox, QComboBox, QFrame, QHBoxLayout, QLabel, QPlainTextEdit, QProgressBar,
                                 QPushButton, QSpinBox, QStackedWidget, QVBoxLayout, QWidget)
except ImportError:
    from PySide6.QtCore import QProcess, QProcessEnvironment, QRectF, QSize, Qt, Signal
    from PySide6.QtGui import QColor, QFont, QLinearGradient, QPainter, QPainterPath, QPen, QPixmap, QTextCursor
    from PySide6.QtWidgets import (QApplication, QCheckBox, QComboBox, QFrame, QHBoxLayout, QLabel, QPlainTextEdit, QProgressBar,
                                   QPushButton, QSpinBox, QStackedWidget, QVBoxLayout, QWidget)

HERE = os.path.dirname(os.path.abspath(__file__))
RAW = "https://raw.githubusercontent.com/Morzilla/breeze-ng/ng/ng/"
IT = os.environ.get("LANG", "").lower().startswith("it")

TEXT = {
    "title": ("NG desktop installer", "Installazione del desktop NG"),
    "welcome": ("Welcome", "Benvenuto"),
    "welcome_text": (
        "This installs a different look for KDE Plasma 6, made of three parts that go together. "
        "Everything goes into your home folder: your distribution's Dolphin and Breeze are not touched.",
        "Questo programma installa un aspetto diverso per KDE Plasma 6, fatto di tre parti che vanno insieme. "
        "Tutto finisce nella tua cartella home: il Dolphin e il Breeze della distribuzione non vengono toccati."),
    "notice": (
        "Dolphin NG and Breeze NG are GPL forks of KDE software, not affiliated with KDE. NeoLine and this installer are MIT. "
        "No warranty of any kind. Building needs development packages: installing them asks for your password once.",
        "Dolphin NG e Breeze NG sono fork GPL di software KDE, non affiliati a KDE. NeoLine e questo installer sono MIT. "
        "Nessuna garanzia. Per compilare servono i pacchetti di sviluppo: installarli chiede la password una volta."),
    "parts": ("What to install", "Cosa installare"),
    "dolphin": ("Dolphin NG", "Dolphin NG"),
    "dolphin_d": ("The file manager: cards view, quick actions, glass sidebar, previews", "Il file manager: vista a card, azioni rapide, pannello a vetro, anteprime"),
    "breeze": ("Breeze NG", "Breeze NG"),
    "breeze_d": ("Widget style and window decoration, no separator lines, joined menus", "Stile dei controlli e decorazione delle finestre, senza linee, menu uniti"),
    "icons": ("NeoLine icons", "Icone NeoLine"),
    "icons_d": ("Outline icon theme in three variants", "Tema di icone a contorno in tre varianti"),
    "look": ("Choose a look", "Scegli un aspetto"),
    "look_text": ("A starting point: every detail can be changed later in the settings.", "Un punto di partenza: ogni dettaglio si cambia dopo dalle impostazioni."),
    "classic": ("Classic", "Classico"),
    "classic_d": ("Close to stock Dolphin: plain panel, columns, icons", "Vicino al Dolphin originale: pannello normale, colonne, icone"),
    "soft": ("Soft", "Soft"),
    "soft_d": ("The NG list and cards, opaque surfaces told apart by tone", "Lista e card NG, superfici opache distinte dal tono"),
    "glass": ("Glass", "Glass"),
    "glass_d": ("Everything: glass panel, translucent windows, blur", "Tutto: pannello a vetro, finestre traslucide, sfocatura"),
    "iconset": ("Icons:", "Icone:"),
    "apply": ("Switch my desktop to it when done (style, decoration, colours, icons)", "Passa il mio desktop a questo aspetto al termine (stile, decorazione, colori, icone)"),
    "deps": ("Install the build dependencies (asks for your password)", "Installa le dipendenze di compilazione (chiede la password)"),
    "jobs": ("Parallel compile jobs:", "Processi di compilazione:"),
    "back": ("Back", "Indietro"),
    "next": ("Next", "Avanti"),
    "install": ("Install", "Installa"),
    "close": ("Close", "Chiudi"),
    "cancel": ("Cancel", "Annulla"),
    "working": ("Installing…", "Installazione in corso…"),
    "step_deps": ("Build dependencies", "Dipendenze di compilazione"),
    "details": ("Show details", "Mostra dettagli"),
    "done": ("All done", "Fatto"),
    "done_text": (
        "Dolphin NG is in your application menu. For Breeze NG, log out and in once so that the session finds the new plugins; "
        "then pick it in System Settings › Global Theme. Run this installer again at any time to update.",
        "Dolphin NG è nel menu delle applicazioni. Per Breeze NG esci e rientra dalla sessione una volta, così vengono trovati i nuovi plugin; "
        "poi sceglilo in Impostazioni di sistema › Tema globale. Rilancia questo installer quando vuoi per aggiornare."),
    "failed": ("Something went wrong", "Qualcosa è andato storto"),
    "failed_text": ("The details below show where it stopped. Nothing of your system was replaced.", "I dettagli qui sotto mostrano dove si è fermato. Nulla del sistema è stato sostituito."),
    "launch": ("Start Dolphin NG", "Avvia Dolphin NG"),
    "nothing": ("Select at least one part.", "Seleziona almeno una parte."),
    "credits": ("by Morzilla, built with Claude (Anthropic) on top of KDE software", "di Morzilla, realizzato con Claude (Anthropic) su software KDE"),
}


def t(key):
    return TEXT[key][1 if IT else 0]


STYLE = """
QWidget { background: #1f2125; color: #dfdfdf; font-size: 10.5pt; }
QLabel#h1 { font-size: 19pt; font-weight: 600; }
QLabel#h2 { font-size: 13pt; font-weight: 600; }
QLabel#dim { color: #9ea2a8; }
QLabel#small { color: #7f848b; font-size: 9pt; }
QFrame#card { background: #292c31; border: 1px solid #33373d; border-radius: 12px; }
QFrame#card[selected="true"] { border: 2px solid #4f8fd0; background: #2a3340; }
QFrame#card QLabel, QFrame#card QCheckBox { background: transparent; }
QPushButton { background: #33373d; border: 0; border-radius: 9px; padding: 8px 20px; }
QPushButton:hover { background: #3d4249; }
QPushButton:disabled { color: #6f737a; background: #292c31; }
QPushButton#primary { background: #2f5f93; color: white; font-weight: 600; }
QPushButton#primary:hover { background: #3a71ab; }
QPushButton#link { background: transparent; color: #7fb4ea; padding: 4px 6px; }
QCheckBox { spacing: 10px; }
QCheckBox::indicator { width: 18px; height: 18px; border-radius: 5px; background: #3a3e45; }
QCheckBox::indicator:checked { background: #3f7fc0; image: none; border: 4px solid #3a3e45; }
QComboBox, QSpinBox { background: #292c31; border: 1px solid #3a3e45; border-radius: 8px; padding: 5px 10px; }
QComboBox QAbstractItemView { background: #292c31; selection-background-color: #2f5f93; }
QProgressBar { background: #292c31; border: 0; border-radius: 4px; height: 8px; }
QProgressBar::chunk { background: #4f8fd0; border-radius: 4px; }
QPlainTextEdit { background: #17181b; border: 1px solid #2c2f34; border-radius: 10px; font-family: monospace; font-size: 9pt; color: #b9bdc3; }
"""


class Preview(QWidget):
    """A small drawing of the Dolphin NG window in one of the three looks."""

    def __init__(self, look):
        super().__init__()
        self.look = look
        self.setFixedSize(190, 118)
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)

    def paintEvent(self, _):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        r = QRectF(self.rect()).adjusted(1, 1, -1, -1)
        path = QPainterPath()
        path.addRoundedRect(r, 9, 9)
        p.setClipPath(path)
        if self.look == "glass":
            g = QLinearGradient(r.topLeft(), r.bottomRight())
            g.setColorAt(0, QColor("#3b5f86"))
            g.setColorAt(1, QColor("#b56a3a"))
            p.fillRect(r, g)
            p.fillRect(r, QColor(30, 32, 36, 205))
        else:
            p.fillRect(r, QColor("#25272b"))
        p.fillRect(QRectF(r.left(), r.top(), r.width(), 16), QColor(0, 0, 0, 60))
        for i, c in enumerate(("#e0705b", "#6b6f76", "#6b6f76")):
            p.setBrush(QColor(c)); p.setPen(Qt.PenStyle.NoPen)
            p.drawEllipse(QRectF(r.right() - 14 - i * 12, r.top() + 4, 8, 8))
        side = QRectF(r.left() + 6, r.top() + 22, 48, r.height() - 28)
        if self.look == "glass":
            g = QLinearGradient(side.topLeft(), side.bottomLeft())
            g.setColorAt(0, QColor(190, 100, 100, 190))
            g.setColorAt(1, QColor(90, 70, 120, 190))
            p.setBrush(g); p.setPen(QPen(QColor(255, 255, 255, 60), 1))
            p.drawRoundedRect(side, 7, 7)
        else:
            p.setBrush(QColor(255, 255, 255, 10 if self.look == "classic" else 16)); p.setPen(Qt.PenStyle.NoPen)
            p.drawRoundedRect(side, 0 if self.look == "classic" else 7, 0 if self.look == "classic" else 7)
        p.setPen(Qt.PenStyle.NoPen)
        for i in range(5):
            p.setBrush(QColor(255, 255, 255, 70))
            p.drawRoundedRect(QRectF(side.left() + 6, side.top() + 8 + i * 13, 30 - (i % 2) * 8, 4), 2, 2)
        x0 = side.right() + 8
        w = r.right() - 6 - x0
        if self.look == "classic":
            for i in range(7):
                p.setBrush(QColor(255, 255, 255, 14 if i % 2 else 0))
                p.drawRect(QRectF(x0, r.top() + 24 + i * 12, w, 12))
                p.setBrush(QColor("#4f93d9" if i < 2 else "#8f99a6")); p.drawRoundedRect(QRectF(x0 + 4, r.top() + 27 + i * 12, 6, 6), 1, 1)
                p.setBrush(QColor(255, 255, 255, 80)); p.drawRoundedRect(QRectF(x0 + 15, r.top() + 28 + i * 12, 40 + (i * 7) % 22, 3), 1, 1)
                p.setBrush(QColor(255, 255, 255, 40)); p.drawRoundedRect(QRectF(x0 + w - 26, r.top() + 28 + i * 12, 20, 3), 1, 1)
        else:
            colours = ("#4f93d9", "#5b7bb0", "#c0894b", "#5e9c6a", "#a66b9c", "#c9a24d")
            cw, ch = (w - 6) / 2, 26
            for i in range(6):
                cx, cy = x0 + (i % 2) * (cw + 6), r.top() + 24 + (i // 2) * (ch + 5)
                card = QRectF(cx, cy, cw, ch)
                p.setBrush(QColor(0, 0, 0, 70)); p.drawRoundedRect(card, 5, 5)
                if self.look == "glass":
                    g = QLinearGradient(card.topLeft(), card.topRight())
                    c = QColor(colours[i]); c.setAlpha(90); g.setColorAt(0, c); c2 = QColor(colours[i]); c2.setAlpha(0); g.setColorAt(0.8, c2)
                    p.setBrush(g); p.drawRoundedRect(card, 5, 5)
                else:
                    p.setBrush(QColor(colours[i])); p.drawRoundedRect(QRectF(cx + 5, cy + 7, 10, 10), 2, 2)
                tx = cx + (6 if self.look == "glass" else 20)
                p.setBrush(QColor(255, 255, 255, 90)); p.drawRoundedRect(QRectF(tx, cy + 7, cw - (tx - cx) - 10, 3), 1, 1)
                p.setBrush(QColor(255, 255, 255, 45)); p.drawRoundedRect(QRectF(tx, cy + 14, cw - (tx - cx) - 22, 3), 1, 1)
        p.setClipping(False)
        p.setBrush(Qt.BrushStyle.NoBrush); p.setPen(QPen(QColor(255, 255, 255, 40), 1)); p.drawRoundedRect(r, 9, 9)


class Card(QFrame):
    clicked = Signal()

    def __init__(self):
        super().__init__()
        self.setObjectName("card")
        self.setProperty("selected", False)

    def select(self, on):
        self.setProperty("selected", bool(on))
        self.style().unpolish(self); self.style().polish(self)

    def mousePressEvent(self, e):
        self.clicked.emit()


def label(text, name=None, wrap=True):
    lab = QLabel(text)
    lab.setWordWrap(wrap)
    if name:
        lab.setObjectName(name)
    return lab


class Installer(QWidget):
    def __init__(self):
        super().__init__()
        self.setWindowTitle(t("title"))
        self.resize(760, 600)
        self.look = "soft"
        self.proc = None
        self.queue = []
        self.steps_total = 1
        self.steps_done = 0
        root = QVBoxLayout(self)
        root.setContentsMargins(28, 24, 28, 20)
        root.setSpacing(16)

        head = QHBoxLayout()
        logo = QLabel()
        pix = QPixmap(os.path.join(HERE, "logo.png"))
        if not pix.isNull():
            logo.setPixmap(pix.scaled(QSize(52, 52), Qt.AspectRatioMode.KeepAspectRatio, Qt.TransformationMode.SmoothTransformation))
            head.addWidget(logo)
        titles = QVBoxLayout(); titles.setSpacing(0)
        titles.addWidget(label("Dolphin NG · Breeze NG · NeoLine", "h2", False))
        titles.addWidget(label(t("credits"), "small", False))
        head.addLayout(titles); head.addStretch()
        root.addLayout(head)

        self.pages = QStackedWidget()
        root.addWidget(self.pages, 1)
        self.pages.addWidget(self.page_welcome())
        self.pages.addWidget(self.page_parts())
        self.pages.addWidget(self.page_look())
        self.pages.addWidget(self.page_run())

        nav = QHBoxLayout()
        self.hint = label("", "small", False)
        nav.addWidget(self.hint); nav.addStretch()
        self.back = QPushButton(t("back")); self.back.clicked.connect(lambda: self.go(-1))
        self.next = QPushButton(t("next")); self.next.setObjectName("primary"); self.next.clicked.connect(lambda: self.go(1))
        nav.addWidget(self.back); nav.addWidget(self.next)
        root.addLayout(nav)
        self.update_nav()

    # ---- pages
    def page_welcome(self):
        w = QWidget(); v = QVBoxLayout(w); v.setSpacing(14); v.setContentsMargins(0, 8, 0, 0)
        v.addWidget(label(t("welcome"), "h1"))
        v.addWidget(label(t("welcome_text")))
        shot = QLabel()
        pix = QPixmap(os.path.join(HERE, "screenshots", "01-desktop.png"))
        if not pix.isNull():
            shot.setPixmap(pix.scaled(QSize(700, 290), Qt.AspectRatioMode.KeepAspectRatio, Qt.TransformationMode.SmoothTransformation))
            shot.setAlignment(Qt.AlignmentFlag.AlignCenter)
            v.addWidget(shot, 1)
        else:
            v.addStretch(1)
        v.addWidget(label(t("notice"), "small"))
        return w

    def page_parts(self):
        w = QWidget(); v = QVBoxLayout(w); v.setSpacing(12); v.setContentsMargins(0, 8, 0, 0)
        v.addWidget(label(t("parts"), "h1"))
        self.parts = {}
        for key in ("dolphin", "breeze", "icons"):
            card = Card(); h = QVBoxLayout(card); h.setContentsMargins(16, 12, 16, 12); h.setSpacing(2)
            box = QCheckBox(t(key)); box.setChecked(True); f = box.font(); f.setBold(True); box.setFont(f)
            card.clicked.connect(box.toggle)
            h.addWidget(box); d = label(t(key + "_d"), "dim"); d.setContentsMargins(28, 0, 0, 0); h.addWidget(d)
            self.parts[key] = box
            v.addWidget(card)
        self.deps = QCheckBox(t("deps")); self.deps.setChecked(True)
        v.addSpacing(6); v.addWidget(self.deps)
        row = QHBoxLayout(); row.addWidget(label(t("jobs"), "dim", False))
        self.jobs = QSpinBox(); self.jobs.setRange(1, 64); self.jobs.setValue(max(1, (os.cpu_count() or 4) // 2))
        row.addWidget(self.jobs); row.addStretch(); v.addLayout(row)
        v.addStretch(1)
        return w

    def page_look(self):
        w = QWidget(); v = QVBoxLayout(w); v.setSpacing(12); v.setContentsMargins(0, 8, 0, 0)
        v.addWidget(label(t("look"), "h1"))
        v.addWidget(label(t("look_text"), "dim"))
        row = QHBoxLayout(); row.setSpacing(12)
        self.looks = {}
        for key in ("classic", "soft", "glass"):
            card = Card(); c = QVBoxLayout(card); c.setContentsMargins(14, 14, 14, 14); c.setSpacing(8)
            c.addWidget(Preview(key), 0, Qt.AlignmentFlag.AlignHCenter)
            c.addWidget(label(t(key), "h2", False))
            c.addWidget(label(t(key + "_d"), "dim"))
            c.addStretch(1)
            card.clicked.connect(lambda k=key: self.pick(k))
            self.looks[key] = card
            row.addWidget(card)
        v.addLayout(row)
        r2 = QHBoxLayout(); r2.addWidget(label(t("iconset"), "dim", False))
        self.iconset = QComboBox()
        for name, text in (("NeoLine-White", "NeoLine White"), ("NeoLine", "NeoLine"), ("NeoLine-Yellow", "NeoLine Yellow")):
            self.iconset.addItem(text, name)
        r2.addWidget(self.iconset); r2.addStretch(); v.addLayout(r2)
        self.apply = QCheckBox(t("apply")); v.addWidget(self.apply)
        v.addStretch(1)
        self.pick("soft")
        return w

    def page_run(self):
        w = QWidget(); v = QVBoxLayout(w); v.setSpacing(12); v.setContentsMargins(0, 8, 0, 0)
        self.run_title = label(t("working"), "h1"); v.addWidget(self.run_title)
        self.run_step = label("", "dim"); v.addWidget(self.run_step)
        self.bar = QProgressBar(); self.bar.setTextVisible(False); self.bar.setFixedHeight(8); v.addWidget(self.bar)
        self.toggle = QPushButton(t("details")); self.toggle.setObjectName("link"); self.toggle.setCheckable(True)
        v.addWidget(self.toggle, 0, Qt.AlignmentFlag.AlignLeft)
        self.log = QPlainTextEdit(); self.log.setReadOnly(True); self.log.setMaximumBlockCount(4000); self.log.hide()
        self.toggle.toggled.connect(self.log.setVisible)
        v.addWidget(self.log, 1)
        self.filler = QWidget(); v.addWidget(self.filler, 1)
        self.toggle.toggled.connect(lambda on: self.filler.setVisible(not on))
        self.launch = QPushButton(t("launch")); self.launch.hide(); self.launch.clicked.connect(self.start_dolphin)
        v.addWidget(self.launch, 0, Qt.AlignmentFlag.AlignLeft)
        return w

    # ---- navigation
    def pick(self, key):
        self.look = key
        for k, card in self.looks.items():
            card.select(k == key)

    def go(self, by):
        i = self.pages.currentIndex()
        if i == 3:
            if self.proc is not None and self.proc.state() != QProcess.ProcessState.NotRunning:
                self.queue = []
                self.proc.kill()
            self.close()
            return
        if i == 1 and by > 0 and not any(b.isChecked() for b in self.parts.values()):
            self.hint.setText(t("nothing"))
            return
        self.hint.setText("")
        self.pages.setCurrentIndex(max(0, i + by))
        self.update_nav()
        if self.pages.currentIndex() == 3:
            self.start()

    def update_nav(self):
        i = self.pages.currentIndex()
        self.back.setVisible(0 < i < 3)
        self.next.setText(t("install") if i == 2 else t("cancel") if i == 3 else t("next"))
        if i == 3:
            self.next.setObjectName(""); self.next.style().unpolish(self.next); self.next.style().polish(self.next)

    # ---- the work
    def script(self):
        path = os.environ.get("NG_INSTALLER_SCRIPT") or os.path.join(HERE, "install-desktop.sh")
        if not os.path.exists(path):
            urllib.request.urlretrieve(RAW + "install-desktop.sh", path)
        return path

    def start(self):
        try:
            script = self.script()
        except OSError as error:
            self.finish(False, str(error))
            return
        chosen = [k for k, b in self.parts.items() if b.isChecked()]
        args = ["bash", script, "--yes", "--no-deps", "--only", ",".join(chosen), "--jobs", str(self.jobs.value()), "--preset", self.look]
        if self.apply.isChecked():
            args += ["--apply", "glass" if self.look == "glass" else "soft", "--icons", self.iconset.currentData()]
        self.queue = []
        if self.deps.isChecked():
            elevate = shutil.which("pkexec")
            self.queue.append((t("step_deps"), ([elevate, "bash", script, "--deps-only"] if elevate else ["bash", script, "--deps-only"])))
        self.queue.append((None, args))
        self.steps_total = (1 if self.deps.isChecked() else 0) + len(chosen) + 1 + (1 if self.apply.isChecked() else 0)
        self.steps_done = 0
        self.bar.setRange(0, 0)
        self.run_next()

    def run_next(self):
        if not self.queue:
            self.finish(True)
            return
        title, args = self.queue.pop(0)
        if title:
            self.step(title)
        self.proc = QProcess(self)
        self.proc.setProcessChannelMode(QProcess.ProcessChannelMode.MergedChannels)
        env = QProcessEnvironment.systemEnvironment()
        env.insert("TERM", "dumb")
        self.proc.setProcessEnvironment(env)
        self.proc.readyReadStandardOutput.connect(self.read)
        self.proc.finished.connect(self.ended)
        self.proc.errorOccurred.connect(lambda _e: None)
        self.proc.start(args[0], args[1:])

    def read(self):
        text = bytes(self.proc.readAllStandardOutput()).decode(errors="replace")
        for line in text.splitlines():
            if line.startswith("==> "):
                self.step(line[4:].strip())
        self.log.moveCursor(QTextCursor.MoveOperation.End)
        self.log.insertPlainText(text)
        self.log.moveCursor(QTextCursor.MoveOperation.End)

    def step(self, title):
        self.steps_done += 1
        self.run_step.setText(title)
        self.bar.setRange(0, self.steps_total * 10)
        self.bar.setValue(min(self.steps_total * 10 - 4, self.steps_done * 10 - 6))

    def ended(self, code, _status):
        if code != 0:
            self.queue = []
            self.finish(False)
            return
        self.run_next()

    def finish(self, good, message=""):
        self.bar.setRange(0, 10); self.bar.setValue(10 if good else 0)
        self.run_title.setText(t("done") if good else t("failed"))
        self.run_step.setText(t("done_text") if good else (message or t("failed_text")))
        self.next.setText(t("close"))
        if good:
            self.launch.setVisible(self.parts["dolphin"].isChecked() and os.path.exists(os.path.expanduser("~/.local/bin/dolphin-ng")))
        else:
            self.toggle.setChecked(True)

    def start_dolphin(self):
        QProcess.startDetached(os.path.expanduser("~/.local/bin/dolphin-ng"), [])

    def closeEvent(self, e):
        if self.proc is not None and self.proc.state() != QProcess.ProcessState.NotRunning:
            self.queue = []
            self.proc.kill()
            self.proc.waitForFinished(2000)
        e.accept()


def main():
    app = QApplication(sys.argv)
    app.setStyle("Fusion")
    app.setStyleSheet(STYLE)
    app.setApplicationName("ng-installer")
    w = Installer()
    w.show()
    if "--page" in sys.argv:  # for screenshots
        w.pages.setCurrentIndex(int(sys.argv[sys.argv.index("--page") + 1])); w.update_nav()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
