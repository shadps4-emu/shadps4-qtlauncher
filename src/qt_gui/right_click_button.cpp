// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QMouseEvent>

#include "right_click_button.h"

QRightClickButton::QRightClickButton(QWidget* parent) : QPushButton(parent) {}

void QRightClickButton::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::RightButton) {
        emit rightClicked();
    } else {
        QPushButton::mousePressEvent(e);
    }
}
