// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QImage>
#include <QMovie>
#include <QObject>
#include <QScopedPointer>
#include <QString>

// Holds the user-selected custom background image (a static image or an
// animated GIF) shown in the game list/grid whenever no game is currently
// selected. Shared by GameListFrame and GameGridFrame so both views stay in
// sync and only decode/animate the image once.
class CustomBackgroundProvider : public QObject {
    Q_OBJECT

public:
    static CustomBackgroundProvider& getInstance() {
        static CustomBackgroundProvider instance;
        return instance;
    }

    // Loads the image (or animated GIF, detected by its .gif extension) at
    // `path`. Returns false if the file could not be loaded, in which case
    // any previously configured image is left cleared. Passing an empty path
    // is equivalent to calling Clear().
    bool SetImagePath(const QString& path);
    void Clear();

    bool IsSet() const;
    QString ImagePath() const {
        return m_path;
    }
    // Returns the frame that should currently be drawn: the static image, or
    // the animated GIF's current frame.
    QImage CurrentFrame() const;

Q_SIGNALS:
    // Emitted whenever the frame that CurrentFrame() would return changes:
    // a new image was set, the image was cleared, or the animated GIF
    // advanced to its next frame.
    void FrameChanged();

private:
    explicit CustomBackgroundProvider(QObject* parent = nullptr);
    CustomBackgroundProvider(const CustomBackgroundProvider&) = delete;
    CustomBackgroundProvider& operator=(const CustomBackgroundProvider&) = delete;

    QString m_path;
    QImage m_staticImage;
    QScopedPointer<QMovie> m_movie;
    bool m_isAnimated = false;
};
