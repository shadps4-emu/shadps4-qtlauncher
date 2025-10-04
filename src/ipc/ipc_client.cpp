// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QDir>
#include <QMessageBox>
#include <QProcessEnvironment>

#include "common/logging/log.h"
#include "ipc_client.h"

IpcClient::IpcClient(QObject* parent) : QObject(parent) {}

void IpcClient::startEmulator(const QFileInfo& exe, const QStringList& args,
                              const QString& workDir) {
    if (process) {
        process->disconnect();
        process->deleteLater();
        process = nullptr;
    }
    process = new QProcess(this);

    connect(process, &QProcess::readyReadStandardError, this, [this] { onStderr(); });
    connect(process, &QProcess::readyReadStandardOutput, this, [this] { onStdout(); });
    connect(process, &QProcess::finished, this, [this] { onProcessClosed(); });

    process->setProcessChannelMode(QProcess::SeparateChannels);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("SHADPS4_ENABLE_IPC", "true");
    process->setProcessEnvironment(env);

    process->setWorkingDirectory(workDir.isEmpty() ? exe.absolutePath() : workDir);
    process->start(exe.absoluteFilePath(), args, QIODevice::ReadWrite);
}

void IpcClient::startGame() {
    writeLine("START");
}

void IpcClient::pauseGame() {
    writeLine("PAUSE");
}

void IpcClient::resumeGame() {
    writeLine("RESUME");
}

void IpcClient::stopEmulator() {
    writeLine("STOP");
}

void IpcClient::restartEmulator() {
    stopEmulator();
    pendingRestart = true;
}

void IpcClient::toggleFullscreen() {
    writeLine("TOGGLE_FULLSCREEN");
}

void IpcClient::sendMemoryPatches(std::string modNameStr, std::string offsetStr,
                                  std::string valueStr, std::string targetStr, std::string sizeStr,
                                  bool isOffset, bool littleEndian,
                                  MemoryPatcher::PatchMask patchMask, int maskOffset) {
    writeLine("PATCH_MEMORY");
    writeLine(QString::fromStdString(modNameStr));
    writeLine(QString::fromStdString(offsetStr));
    writeLine(QString::fromStdString(valueStr));
    writeLine(QString::fromStdString(targetStr));
    writeLine(QString::fromStdString(sizeStr));
    writeLine(isOffset ? "1" : "0");
    writeLine(littleEndian ? "1" : "0");
    writeLine(QString::number(static_cast<int>(patchMask)));
    writeLine(QString::number(maskOffset));
}

void IpcClient::onStderr() {
    buffer.append(process->readAllStandardError());
    int idx;
    while ((idx = buffer.indexOf('\n')) != -1) {
        QByteArray line = buffer.left(idx);
        buffer.remove(0, idx + 1);

        if (!line.isEmpty() && line.back() == '\r') {
            line.chop(1);
        }

        if (!line.startsWith(";")) {
            LOG_ERROR(Tty, "{}", line.toStdString());
            continue;
        }

        const QString s = QString::fromUtf8(line.mid(1)).trimmed();
        if (s == "#IPC_ENABLED") {
            LOG_INFO(IPC, "IPC detected");
        } else if (s == "ENABLE_MEMORY_PATCH") {
            supportedCapabilities["memory_patch"] = true;
            LOG_INFO(IPC, "Feature detected: 'memory_patch'");
        } else if (s == "ENABLE_EMU_CONTROL") {
            supportedCapabilities["emu_control"] = true;
            LOG_INFO(IPC, "Feature detected: 'emu_control'");
        } else if (s == "#IPC_END") {
            for (const auto& [capability, supported] : supportedCapabilities) {
                if (!supported) {
                    LOG_WARNING(IPC, "Feature: '{}' is not supported by the choosen emulator version", capability);
                }
            }
            LOG_INFO(IPC, "start emu");
            writeLine("RUN");
            startGameFunc();
        } else if (s == "RESTART") {
            parsingState = ParsingState::args_counter;
        }

        else if (parsingState == ParsingState::args_counter) {
            argsCounter = s.toInt();
            parsingState = ParsingState::args;
        }

        else if (parsingState == ParsingState::args) {
            parsedArgs.push_back(s.toStdString());
            if (parsedArgs.size() == argsCounter) {
                parsingState = ParsingState::normal;
                pendingRestart = true;
                stopEmulator();
            }
        }
    }
}

void IpcClient::onStdout() {
    printf("%s", process->readAllStandardOutput().toStdString().c_str());
}

void IpcClient::onProcessClosed() {
    gameClosedFunc();
    if (process) {
        process->disconnect();
        process->deleteLater();
        process = nullptr;
    }
    if (pendingRestart) {
        pendingRestart = false;
        restartEmulatorFunc();
    }
}

void IpcClient::writeLine(const QString& text) {
    if (process == nullptr) {
        QMessageBox::critical(
            nullptr, tr("ShadPS4"),
            QString(tr("shadPS4 is not found!\nPlease change shadPS4 path in settings.")));
        return;
    }

    QByteArray data = text.toUtf8();
    data.append('\n');
    process->write(data);
    process->waitForBytesWritten(1000);
}
