// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QObject>
#include <QPushButton>

class QRightClickButton : public QPushButton {
    Q_OBJECT

public:
    explicit QRightClickButton(QWidget* parent = 0);

public slots:
    void mousePressEvent(QMouseEvent* e);

signals:
    void rightClicked();
};
