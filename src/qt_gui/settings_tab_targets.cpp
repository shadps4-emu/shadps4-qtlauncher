// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "qt_gui/settings_tab_targets.h"

#include <QStringBuilder>

namespace QtGui::SettingsTabs {

namespace {

QString NormalizeStringToken(const QString& input, bool strip_tab_suffix) {
    QString trimmed = input.trimmed();
    if (strip_tab_suffix && trimmed.endsWith("tab", Qt::CaseInsensitive) && trimmed.size() > 3) {
        const QChar before_suffix = trimmed.at(trimmed.size() - 4);
        if (before_suffix.isLetterOrNumber()) {
            trimmed.chop(3);
        }
    }

    QString output;
    output.reserve(trimmed.size() + 4);

    QChar previous;
    for (int i = 0; i < trimmed.size(); ++i) {
        const QChar current = trimmed.at(i);
        if (current.isLetterOrNumber()) {
            if (current.isUpper() && !output.isEmpty() && previous != '_' && previous != '-') {
                output.append('_');
            }
            output.append(current.toLower());
        } else if (current == '_' || current == '-' || current.isSpace()) {
            if (!output.endsWith('_') && !output.isEmpty()) {
                output.append('_');
            }
        }
        previous = current;
    }

    while (output.endsWith('_')) {
        output.chop(1);
    }

    return output;
}

} // namespace

QString NormalizeTabObjectName(const QString& object_name) {
    return NormalizeStringToken(object_name, true);
}

QString NormalizeTabKey(const QString& tab_key) {
    return NormalizeStringToken(tab_key, false);
}

QString BuildTabTarget(const QString& object_name) {
    const QString normalized = NormalizeTabObjectName(object_name);
    if (normalized.isEmpty()) {
        return {};
    }
    return QStringLiteral("settings:") % normalized;
}

QString ExtractTabKey(const QString& target) {
    const QString trimmed = target.trimmed();
    if (!trimmed.startsWith("settings:", Qt::CaseInsensitive)) {
        return {};
    }

    const QString key = trimmed.mid(QStringLiteral("settings:").size());
    return NormalizeTabKey(key);
}

bool IsSettingsTarget(const QString& target) {
    return !ExtractTabKey(target).isEmpty();
}

} // namespace QtGui::SettingsTabs
