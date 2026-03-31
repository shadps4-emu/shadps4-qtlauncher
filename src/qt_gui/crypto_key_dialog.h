// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QTabWidget>
#include <QVBoxLayout>

#include "common/key_manager.h"

// Struct to hold all RSA keyset fields
struct KeysetFields {
    QPlainTextEdit* exponent1;
    QPlainTextEdit* exponent2;
    QPlainTextEdit* publicExponent;
    QPlainTextEdit* coefficient;
    QPlainTextEdit* modulus;
    QPlainTextEdit* prime1;
    QPlainTextEdit* prime2;
    QPlainTextEdit* privateExponent;

    QVector<QPlainTextEdit*> all() const {
        return {exponent1, exponent2, publicExponent, coefficient,
                modulus,   prime1,    prime2,         privateExponent};
    }
};

class CryptoManagerDialog : public QDialog {
    Q_OBJECT

public:
    explicit CryptoManagerDialog(QWidget* parent = nullptr);
    ~CryptoManagerDialog() override = default;

private slots:
    void LoadKeys();
    void SaveKeys();
    void ValidateHexInput();

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    struct HexSizeConstraint {
        int minBytes = 0;
        int maxBytes = INT_MAX;
        const char* displayName = nullptr;
    };

    QString FormatSizeError(const QString& fieldName, int actualBytes, const HexSizeConstraint& c) {
        if (c.minBytes == c.maxBytes) {
            return QString("%1 is %2 bytes (%3 bits).\nExpected exactly %4 bytes (%5 bits).")
                .arg(fieldName)
                .arg(actualBytes)
                .arg(actualBytes * 8)
                .arg(c.minBytes)
                .arg(c.minBytes * 8);
        }

        if (c.maxBytes == INT_MAX) {
            return QString("%1 is %2 bytes (%3 bits).\nExpected at least %4 bytes (%5 bits).")
                .arg(fieldName)
                .arg(actualBytes)
                .arg(actualBytes * 8)
                .arg(c.minBytes)
                .arg(c.minBytes * 8);
        }

        return QString("%1 is %2 bytes (%3 bits).\nExpected between %4 and %5 bytes.")
            .arg(fieldName)
            .arg(actualBytes)
            .arg(actualBytes * 8)
            .arg(c.minBytes)
            .arg(c.maxBytes);
    }

    inline static const QHash<QString, HexSizeConstraint> kRsaConstraints = {
        {"Modulus", {128, INT_MAX, "Modulus"}},
        {"PublicExponent", {1, 8, "Public Exponent"}},
        {"PrivateExponent", {128, INT_MAX, "Private Exponent"}},
        {"Prime1", {64, INT_MAX, "Prime 1"}},
        {"Prime2", {64, INT_MAX, "Prime 2"}},
        {"Exponent1", {64, INT_MAX, "Exponent 1"}},
        {"Exponent2", {64, INT_MAX, "Exponent 2"}},
        {"Coefficient", {64, INT_MAX, "Coefficient"}},
    };

    /* Canonical hex validator
     * Caller must remove whitespace if needed.
     */
    bool IsValidHexString(const QString& text, bool allowEmpty = true) {
        if (text.isEmpty())
            return allowEmpty;

        if ((text.length() & 1) != 0)
            return false;

        static const QRegularExpression hexRe("^[0-9A-Fa-f]+$");
        return hexRe.match(text).hasMatch();
    }

    bool CheckHexSize(const QString& hex, const HexSizeConstraint& c) {
        if (hex.isEmpty())
            return true; // emptiness handled elsewhere

        int bytes = hex.length() / 2;
        return bytes >= c.minBytes && bytes <= c.maxBytes;
    }

    QString ExtractHex(QWidget* w) {
        if (auto* le = qobject_cast<QLineEdit*>(w))
            return le->text();

        if (auto* pe = qobject_cast<QPlainTextEdit*>(w))
            return pe->toPlainText().remove(QRegularExpression("\\s"));

        return {};
    }

    void SetupUI();
    void PopulateFields();
    void CollectKeys();
    bool ValidateFields(const QVector<QLineEdit*>& fields);
    bool ValidateAllFields();
    void UpdateTabStatus(QVector<QWidget*> fields, QWidget* requiredField, QLabel* statusLabel);
    void UpdateAllTabStatuses();
    void LoadJsonIntoFields(const QJsonObject& root);
    void LoadJsonField(const QJsonObject& obj, const QString& key, QPlainTextEdit* edit);
    QString GetFirstValidationError();

    // Tab widget
    QTabWidget* tabWidget;

    // ----------------------------
    // Trophy Key Tab
    // ----------------------------
    QWidget* trophyTab;
    QLineEdit* trophyKeyEdit;
    QLabel* trophyStatusLabel;

    // ----------------------------
    // Fake Keyset Tab
    // ----------------------------
    QWidget* fakeTab;
    KeysetFields fakeKeysetFields;
    QLabel* fakeStatusLabel;

    // ----------------------------
    // Debug Rif Keyset Tab
    // ----------------------------
    QWidget* debugTab;
    KeysetFields debugKeysetFields;
    QLabel* debugStatusLabel;

    // ----------------------------
    // Pkg Derived Key3 Tab
    // ----------------------------
    QWidget* pkgTab;
    KeysetFields pkgKeysetFields;
    QLabel* pkgStatusLabel;

    // ----------------------------
    // Global Buttons
    // ----------------------------
    QPushButton* loadButton;
    QPushButton* saveButton;
    QPushButton* closeButton;

    // ----------------------------
    // Current keys
    // ----------------------------
    KeyManager::AllKeys currentKeys;
};
