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
    process = std::make_unique<QProcess>(this);

    connect(process.get(), &QProcess::readyReadStandardError, this, [this] { onStderr(); });
    connect(process.get(), &QProcess::readyReadStandardOutput, this, [this] { onStdout(); });
    connect(process.get(), &QProcess::finished, this, [this] { onProcessClosed(); });

    process->setProcessChannelMode(QProcess::SeparateChannels);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("SHADPS4_ENABLE_IPC", "true");
    process->setProcessEnvironment(env);

    process->setWorkingDirectory(workDir.isEmpty() ? exe.absolutePath() : workDir);
    process->start(exe.absoluteFilePath(), args, QIODevice::ReadWrite);
}

void IpcClient::runGame() {
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
    restartEmulatorFunc();
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
            continue;
        }

        const QString s = QString::fromUtf8(line.mid(1)).trimmed();
        if (s == "#IPC_ENABLED") {
            LOG_INFO(IPC, "IPC detected");
        } else if (s == "ENABLE_MEMORY_PATCH") {
            LOG_INFO(IPC, "Feature detected: 'Memory patch'");
        } else if (s == "ENABLE_EMU_CONTROL") {
            LOG_INFO(IPC, "Feature detected: 'Emu Control'");
        } else if (s == "#IPC_END") {
            writeLine("RUN");
            LOG_INFO(IPC, "IPC: start emu");
        } else if (s == "RESTART") {
            parsingState = ParsingState::args_counter;
         }

        if (parsingState == ParsingState::args_counter) {
            argsCounter = s.toInt();
            parsingState = ParsingState::args;
        }

        if (parsingState == ParsingState::args) {
            parsedArgs.push_back(s.toStdString());
            if (parsedArgs.size() == argsCounter) {
                parsingState = ParsingState::normal;
            }
        }
    }
}

void IpcClient::onStdout() {
    printf("%s", process->readAllStandardOutput().toStdString().c_str());
}

void IpcClient::onProcessClosed() {
    gameClosedFunc();
    process->disconnect();
    process->deleteLater();
    process.reset();
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
