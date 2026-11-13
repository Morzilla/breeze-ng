#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""A small window with the common controls, to see Breeze NG at a glance (and for the screenshots in NG.md).

    python3 ng/demo-controls.py              in the current widget style
    python3 ng/demo-controls.py -style BreezeNG

It changes nothing: every control is only for show.
"""
import os
import sys

try:
    from PyQt6.QtCore import Qt
    from PyQt6.QtGui import QIcon
    from PyQt6.QtWidgets import (QApplication, QCheckBox, QComboBox, QDialogButtonBox, QFormLayout, QGridLayout,
                                 QHBoxLayout, QLineEdit, QListWidget, QMainWindow, QMenu, QProgressBar, QRadioButton,
                                 QSlider, QSpinBox, QTabWidget, QToolButton, QVBoxLayout, QWidget, QLabel)
except ImportError:
    from PySide6.QtCore import Qt
    from PySide6.QtGui import QIcon
    from PySide6.QtWidgets import (QApplication, QCheckBox, QComboBox, QDialogButtonBox, QFormLayout, QGridLayout,
                                   QHBoxLayout, QLineEdit, QListWidget, QMainWindow, QMenu, QProgressBar, QRadioButton,
                                   QSlider, QSpinBox, QTabWidget, QToolButton, QVBoxLayout, QWidget, QLabel)


def controls_page():
    page = QWidget()
    form = QFormLayout()
    form.setHorizontalSpacing(24)
    form.setVerticalSpacing(12)

    name = QLineEdit("Breeze NG Soft")
    form.addRow("Name", name)

    look = QHBoxLayout()
    variant = QComboBox()
    variant.addItems(["Soft", "Glass", "Stock Breeze"])
    opacity = QSpinBox()
    opacity.setRange(0, 100)
    opacity.setValue(70)
    opacity.setSuffix(" %")
    look.addWidget(variant, 1)
    look.addWidget(opacity)
    form.addRow("Look", look)

    options = QGridLayout()
    options.setHorizontalSpacing(24)
    options.addWidget(QCheckBox("No separator lines", checked=True), 0, 0)
    options.addWidget(QCheckBox("Blur behind windows"), 0, 1)
    options.addWidget(QRadioButton("Pills for tabs", checked=True), 1, 0)
    options.addWidget(QRadioButton("Classic tabs"), 1, 1)
    form.addRow("Options", options)

    slider = QSlider(Qt.Orientation.Horizontal)
    slider.setValue(62)
    form.addRow("Opacity", slider)

    progress = QProgressBar()
    progress.setValue(62)
    form.addRow("Progress", progress)

    places = QListWidget()
    places.addItems(["Documents", "Pictures", "Music", "Videos", "Downloads"])
    places.setCurrentRow(1)
    places.setMaximumHeight(118)

    menu_button = QToolButton()
    menu_button.setText("Menu button")
    menu_button.setPopupMode(QToolButton.ToolButtonPopupMode.InstantPopup)
    menu_button.setToolButtonStyle(Qt.ToolButtonStyle.ToolButtonTextOnly)
    menu = QMenu(menu_button)
    for label in ("Open", "Rename", "Move to Trash"):
        menu.addAction(label)
    menu_button.setMenu(menu)

    buttons = QDialogButtonBox(QDialogButtonBox.StandardButton.Cancel | QDialogButtonBox.StandardButton.Apply)
    buttons.button(QDialogButtonBox.StandardButton.Apply).setDefault(True)

    bottom = QHBoxLayout()
    bottom.addWidget(menu_button)
    bottom.addStretch(1)
    bottom.addWidget(buttons)

    box = QVBoxLayout(page)
    box.setContentsMargins(18, 18, 18, 18)
    box.setSpacing(14)
    box.addLayout(form)
    box.addWidget(places)
    box.addLayout(bottom)
    return page


def text_page(text):
    page = QWidget()
    box = QVBoxLayout(page)
    box.setContentsMargins(18, 18, 18, 18)
    label = QLabel(text)
    label.setWordWrap(True)
    box.addWidget(label)
    box.addStretch(1)
    return page


def main():
    app = QApplication(sys.argv)
    app.setWindowIcon(QIcon(os.path.join(os.path.dirname(os.path.abspath(__file__)), "logo.png")))
    window = QMainWindow()
    window.setWindowTitle("Breeze NG")
    for title, items in (("File", ["New", "Open…", "Save", "Quit"]), ("Edit", ["Undo", "Redo", "Cut", "Copy", "Paste"]),
                         ("View", ["Zoom In", "Zoom Out"]), ("Help", ["About"])):
        m = window.menuBar().addMenu(title)
        for item in items:
            m.addAction(item)
    tabs = QTabWidget()
    tabs.setDocumentMode(True)
    tabs.addTab(controls_page(), "Controls")
    tabs.addTab(text_page("The colour scheme Breeze NG was designed on: BreezeNG."), "Colours")
    tabs.addTab(text_page("Breeze NG, a fork of KDE's Breeze widget style with two looks, Soft and Glass."), "About")
    window.setCentralWidget(tabs)
    window.resize(640, 600)
    window.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
