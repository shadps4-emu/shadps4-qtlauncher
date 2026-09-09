// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "custom_background_provider.h"

CustomBackgroundProvider::CustomBackgroundProvider(QObject* parent) : QObject(parent) {}

bool CustomBackgroundProvider::SetImagePath(const QString& path) {
    Clear();

    if (path.isEmpty()) {
        return true;
    }

    if (path.endsWith(QStringLiteral(".gif"), Qt::CaseInsensitive)) {
        auto* movie = new QMovie(path);
        movie->jumpToFrame(0);
        if (!movie->isValid() || movie->currentImage().isNull()) {
            delete movie;
            return false;
        }

        m_movie.reset(movie);
        m_isAnimated = true;
        connect(m_movie.data(), &QMovie::frameChanged, this,
                [this](int) { emit FrameChanged(); });
        m_movie->start();
    } else {
        QImage image(path);
        if (image.isNull()) {
            return false;
        }

        m_staticImage = image;
        m_isAnimated = false;
    }

    m_path = path;
    emit FrameChanged();
    return true;
}

void CustomBackgroundProvider::Clear() {
    if (m_movie) {
        m_movie->stop();
        m_movie.reset();
    }
    m_staticImage = QImage();
    m_isAnimated = false;
    if (!m_path.isEmpty()) {
        m_path.clear();
        emit FrameChanged();
    }
}

bool CustomBackgroundProvider::IsSet() const {
    return !m_path.isEmpty();
}

QImage CustomBackgroundProvider::CurrentFrame() const {
    if (m_isAnimated) {
        return m_movie ? m_movie->currentImage() : QImage();
    }
    return m_staticImage;
}
