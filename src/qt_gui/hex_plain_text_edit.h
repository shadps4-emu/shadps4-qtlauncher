// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QMimeData>
#include <QPlainTextEdit>
#include <QRegularExpression>

class HexPlainTextEdit : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit HexPlainTextEdit(QWidget* parent = nullptr) : QPlainTextEdit(parent) {}

protected:
    void insertFromMimeData(const QMimeData* source) override {
        if (!source->hasText()) {
            QPlainTextEdit::insertFromMimeData(source);
            return;
        }

        QString text = source->text().toUpper();

        /*
         * Step 1: Replace common separators with spaces
         * (commas, semicolons, newlines, tabs)
         */
        text.replace(QRegularExpression("[,;\\r\\n\\t]"), " ");

        /*
         * Step 2: Extract hex tokens:
         *  - 0xNN
         *  - NN
         */
        QRegularExpression tokenRe(R"(0X[0-9A-F]{1,2}|[0-9A-F]{2})");
        QRegularExpressionMatchIterator it = tokenRe.globalMatch(text);

        QString result;
        while (it.hasNext()) {
            QRegularExpressionMatch m = it.next();
            QString token = m.captured();

            if (token.startsWith("0X"))
                token.remove(0, 2);

            // Ensure exactly 2 hex chars per byte
            if (token.length() == 1)
                token.prepend('0');

            result += token;
        }

        QMimeData clean;
        clean.setText(result);
        QPlainTextEdit::insertFromMimeData(&clean);
    }
};
