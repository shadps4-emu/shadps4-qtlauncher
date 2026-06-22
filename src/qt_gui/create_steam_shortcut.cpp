// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QSettings>
#include <QThread>

#include "common/path_util.h"
#include "common/scm_rev.h"
#include "create_steam_shortcut.h"

SteamShortcut::SteamShortcut(std::shared_ptr<gui_settings> settings, QObject* parent)
    : QObject(parent), m_gui_settings(std::move(settings)) {}

// --- VDF helpers ---

void SteamShortcut::vdfWriteString(QByteArray& buf, const QByteArray& key,
                                   const QByteArray& value) {
    buf += '\x01';
    buf += key;
    buf += '\x00';
    buf += value;
    buf += '\x00';
}

void SteamShortcut::vdfWriteInt32(QByteArray& buf, const QByteArray& key, quint32 value) {
    buf += '\x02';
    buf += key;
    buf += '\x00';
    buf += static_cast<char>(value & 0xFF);
    buf += static_cast<char>((value >> 8) & 0xFF);
    buf += static_cast<char>((value >> 16) & 0xFF);
    buf += static_cast<char>((value >> 24) & 0xFF);
}

QByteArray SteamShortcut::buildSteamShortcutEntry(int index, const QString& appName,
                                                  const QString& exePath, const QString& startDir,
                                                  const QString& iconPath,
                                                  const QString& launchOptions) {
    QByteArray e;
    e += '\x00';
    e += QByteArray::number(index);
    e += '\x00';
    vdfWriteString(e, "AppName", appName.toUtf8());
    vdfWriteString(e, "Exe", ("\"" + exePath + "\"").toUtf8());
    vdfWriteString(e, "StartDir", startDir.toUtf8());
    vdfWriteString(e, "icon", iconPath.toUtf8());
    vdfWriteString(e, "ShortcutPath", "");
    vdfWriteString(e, "LaunchOptions", launchOptions.toUtf8());
    vdfWriteInt32(e, "IsHidden", 0);
    vdfWriteInt32(e, "AllowDesktopConfig", 1);
    vdfWriteInt32(e, "AllowOverlay", 1);
    vdfWriteInt32(e, "OpenVR", 0);
    vdfWriteInt32(e, "Devkit", 0);
    vdfWriteString(e, "DevkitGameID", "");
    vdfWriteInt32(e, "DevkitOverrideAppID", 0);
    vdfWriteInt32(e, "LastPlayTime", 0);
    vdfWriteString(e, "FlatpakAppID", "");
    // Empty tags object
    e += '\x00';
    e += "tags";
    e += '\x00';
    e += '\x08';
    // End entry
    e += '\x08';
    return e;
}

// --- Steam path detection ---

QString SteamShortcut::findSteamPath() {
#ifdef Q_OS_WIN
    const QStringList regKeys = {
        "HKEY_CURRENT_USER\\SOFTWARE\\Valve\\Steam",
        "HKEY_LOCAL_MACHINE\\SOFTWARE\\Valve\\Steam",
        "HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Valve\\Steam",
    };
    for (const QString& k : regKeys) {
        QSettings reg(k, QSettings::NativeFormat);
        // HKCU uses "SteamPath", HKLM uses "InstallPath"
        for (const QString& valName : {"SteamPath", "InstallPath"}) {
            QString path = reg.value(valName).toString();
            if (!path.isEmpty() && QDir(path).exists())
                return path;
        }
    }
    return {};
#elif defined(Q_OS_MAC)
    QString path = QDir::homePath() + "/Library/Application Support/Steam";
    return QDir(path).exists() ? path : QString();
#else // Linux
    const QStringList candidates = {
        QDir::homePath() + "/.steam/steam",
        QDir::homePath() + "/.local/share/Steam",
        QDir::homePath() + "/.steam/root",
        // Flatpak Steam (common on Arch and other distros)
        QDir::homePath() + "/.var/app/com.valvesoftware.Steam/data/Steam",
        qEnvironmentVariable("XDG_DATA_HOME", QDir::homePath() + "/.local/share") + "/Steam",
    };
    for (const QString& c : candidates) {
        if (QDir(c + "/userdata").exists())
            return c;
    }
    return {};
#endif
}

// --- shortcuts.vdf writer ---

// Appends a non-Steam game entry to shortcuts.vdf for one Steam user directory.
bool SteamShortcut::addNonSteamGame(const QString& shortcutsPath, const QString& appName,
                                    const QString& exePath, const QString& startDir,
                                    const QString& iconPath, const QString& launchOptions) {
    QByteArray data;
    QFile file(shortcutsPath);
    int nextIndex = 0;

    if (file.exists() && file.open(QIODevice::ReadOnly)) {
        data = file.readAll();
        file.close();

        // Duplicate check: search for the AppName key-value pair.
        // Use explicit length to avoid strlen() truncating at the embedded \x00.
        QByteArray dupChk = QByteArray("\x01AppName\x00", 9) + appName.toUtf8() + '\x00';
        if (data.contains(dupChk)) {
            QMessageBox::information(nullptr, tr("Steam"),
                                     tr("%1 is already in your Steam library.").arg(appName));
            return true;
        }

        // Walk the top-level shortcuts object to find the next free index.
        // Explicit length required – strlen() would stop at the leading \x00.
        const QByteArray hdr("\x00shortcuts\x00", 11);
        int pos = data.indexOf(hdr);
        if (pos != -1) {
            pos += hdr.size();
            while (pos < data.size()) {
                if (static_cast<unsigned char>(data[pos]) != 0x00)
                    break;
                int keyEnd = data.indexOf('\x00', pos + 1);
                if (keyEnd < 0)
                    break;
                bool ok;
                int idx = QString::fromLatin1(data.mid(pos + 1, keyEnd - pos - 1)).toInt(&ok);
                if (!ok)
                    break;
                nextIndex = idx + 1;
                pos = keyEnd + 1;
                // Depth-walk past this entry's contents
                int depth = 1;
                while (pos < data.size() && depth > 0) {
                    auto t = static_cast<unsigned char>(data[pos]);
                    if (t == 0x00) {
                        int kEnd = data.indexOf('\x00', pos + 1);
                        if (kEnd < 0) {
                            pos = data.size();
                            break;
                        }
                        pos = kEnd + 1;
                        depth++;
                    } else if (t == 0x01) {
                        int kEnd = data.indexOf('\x00', pos + 1);
                        int vEnd = kEnd >= 0 ? data.indexOf('\x00', kEnd + 1) : -1;
                        if (kEnd < 0 || vEnd < 0) {
                            pos = data.size();
                            break;
                        }
                        pos = vEnd + 1;
                    } else if (t == 0x02) {
                        int kEnd = data.indexOf('\x00', pos + 1);
                        if (kEnd < 0) {
                            pos = data.size();
                            break;
                        }
                        pos = kEnd + 1 + 4;
                    } else if (t == 0x08) {
                        depth--;
                        pos++;
                    } else {
                        pos++;
                    }
                }
            }
            // pos now sits exactly on the shortcuts-object closing \x08.
            // Truncate here so all existing entry-closers stay intact.
            data.truncate(pos);
        } else {
            // Malformed or header-less file: fall back to chopping trailing closers.
            while (!data.isEmpty() && static_cast<unsigned char>(data.back()) == 0x08)
                data.chop(1);
        }
    } else {
        // No existing file – start fresh with the shortcuts root object header
        data += '\x00';
        data += "shortcuts";
        data += '\x00';
    }

    data += buildSteamShortcutEntry(nextIndex, appName, exePath, startDir, iconPath, launchOptions);
    // Close shortcuts object, then top-level
    data += '\x08';
    data += '\x08';

    QDir().mkpath(QFileInfo(shortcutsPath).absolutePath());
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(data);
    file.close();
    return true;
}

// --- Steam process management ---

bool SteamShortcut::isSteamRunning() {
#ifdef Q_OS_WIN
    QProcess p;
    p.start("tasklist", {"/FI", "IMAGENAME eq steam.exe", "/NH"});
    p.waitForFinished(3000);
    return p.readAllStandardOutput().toLower().contains("steam.exe");
#else
    // Works for both native and Flatpak – both surface a process named "steam"
    QProcess p;
    p.start("pgrep", {"-x", "steam"});
    p.waitForFinished(3000);
    return p.exitCode() == 0;
#endif
}

void SteamShortcut::shutdownSteam(const QString& steamPath) {
#ifdef Q_OS_WIN
    QProcess::startDetached(steamPath + "/steam.exe", {"-shutdown"});
#elif defined(Q_OS_MAC)
    QProcess::startDetached("osascript", {"-e", R"(quit app "Steam")"});
#else // Linux
    if (!QProcess::startDetached("steam", {"-shutdown"}))
        QProcess::startDetached("pkill", {"-x", "steam"});
#endif
}

// Polls until Steam has fully exited or the timeout expires.
// processEvents() keeps the Qt event loop alive during the wait.
bool SteamShortcut::waitForSteamExit(int timeoutMs) {
    for (int elapsed = 0; elapsed < timeoutMs; elapsed += 500) {
        QThread::msleep(500);
        QCoreApplication::processEvents();
        if (!isSteamRunning())
            return true;
    }
    return !isSteamRunning();
}

void SteamShortcut::relaunchSteam(const QString& steamPath) {
#ifdef Q_OS_WIN
    QProcess::startDetached(steamPath + "/steam.exe", {});
#elif defined(Q_OS_MAC)
    QProcess::startDetached("open", {"-a", "Steam"});
#else // Linux
    if (steamPath.contains("com.valvesoftware.Steam"))
        QProcess::startDetached("flatpak", {"run", "com.valvesoftware.Steam"});
    else
        QProcess::startDetached("steam", {});
#endif
}

// --- Public entry point ---

void SteamShortcut::requestAddToSteam(const GameInfo& selectedInfo, QString emuPath) {
    QString targetPath;
    Common::FS::PathToQString(targetPath, selectedInfo.path);
    QString ebootPath = targetPath + "/eboot.bin";

    QString exePath = QCoreApplication::applicationFilePath();
#ifdef Q_OS_WIN
    exePath = exePath.replace("/", "\\");
#endif
    QString startDir = QFileInfo(exePath).absolutePath();

    QString launchOptions;
    if (emuPath.isEmpty()) {
        launchOptions = QString("-d -g \"%1\"").arg(ebootPath);
    } else {
        launchOptions = QString("-e \"%1\" -g \"%2\"").arg(emuPath, ebootPath);
    }

    QString iconPath;
    Common::FS::PathToQString(iconPath, selectedInfo.icon_path);
    QString gameName = QString::fromStdString(selectedInfo.name);
    if (emuPath.isEmpty()) {
        // Default: tag with the current build's version from scm_rev
        gameName += " [shadPS4 " + QString::fromUtf8(Common::g_scm_app_version) + "]";
    } else {
        // Specific version: use the folder name containing the chosen executable,
        // which is the version string the user picked in ShortcutDialog (e.g. "0.10", "nightly")
        gameName += " [shadPS4 " + QFileInfo(emuPath).dir().dirName() + "]";
    }

    QString steamPath = findSteamPath();
    if (steamPath.isEmpty()) {
        QMessageBox::critical(nullptr, tr("Error"), tr("Steam installation not found."));
        return;
    }

    QStringList userDirs =
        QDir(steamPath + "/userdata").entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    if (userDirs.isEmpty()) {
        QMessageBox::critical(
            nullptr, tr("Error"),
            tr("No Steam user account found. Please log into Steam at least once."));
        return;
    }

    const bool wasRunning = isSteamRunning();
    if (wasRunning) {
        const int reply = QMessageBox::question(
            nullptr, tr("Steam is Running"),
            tr("Steam is currently running. It will be closed and restarted to apply the "
               "shortcut. Continue?"),
            QMessageBox::Yes | QMessageBox::No);
        if (reply != QMessageBox::Yes)
            return;

        shutdownSteam(steamPath);
        if (!waitForSteamExit()) {
            QMessageBox::critical(nullptr, tr("Error"),
                                  tr("Steam did not close in time. Shortcut was not added."));
            return;
        }
    }

    bool anySuccess = false;
    for (const QString& uid : userDirs) {
        QString shortcutsPath = steamPath + "/userdata/" + uid + "/config/shortcuts.vdf";
        if (addNonSteamGame(shortcutsPath, gameName, exePath, startDir, iconPath, launchOptions))
            anySuccess = true;
    }

    if (!anySuccess) {
        QMessageBox::critical(nullptr, tr("Error"), tr("Failed to add game to Steam."));
        if (wasRunning)
            relaunchSteam(steamPath);
        return;
    }

    if (wasRunning) {
        relaunchSteam(steamPath);
        QMessageBox::information(nullptr, tr("Steam"),
                                 tr("Added to Steam successfully. Steam is restarting."));
    } else {
        QMessageBox::information(
            nullptr, tr("Steam"),
            tr("Added to Steam successfully. Launch Steam to see the changes."));
    }
}
