// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QString>

namespace QtGui::SettingsTabs {

QString NormalizeTabObjectName(const QString& object_name);
QString NormalizeTabKey(const QString& tab_key);
QString BuildTabTarget(const QString& object_name);
QString ExtractTabKey(const QString& target);
bool IsSettingsTarget(const QString& target);

} // namespace QtGui::SettingsTabs
