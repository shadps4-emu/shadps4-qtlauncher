// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QFile>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMimeData>
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include "crypto_key_dialog.h"
#include "hex_plain_text_edit.h"

CryptoManagerDialog::CryptoManagerDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Crypto Key Manager"));
    setMinimumSize(700, 200);
    setAcceptDrops(true);
    SetupUI();
    LoadKeys();
    UpdateAllTabStatuses();
}

void CryptoManagerDialog::SetupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    tabWidget = new QTabWidget(this);

    // ----------------------------
    // Trophy Key Tab
    // ----------------------------
    trophyTab = new QWidget();
    QVBoxLayout* trophyLayout = new QVBoxLayout(trophyTab);

    QLabel* trophyDesc = new QLabel(tr("Trophy Key: Used for trophy decryption.\n"
                                       "Must be a valid hex string with even length."));
    trophyDesc->setWordWrap(true);
    trophyLayout->addWidget(trophyDesc);

    trophyKeyEdit = new QLineEdit();
    trophyKeyEdit->setPlaceholderText(tr("Enter Trophy Key"));
    trophyKeyEdit->setValidator(
        new QRegularExpressionValidator(QRegularExpression("^[0-9A-Fa-f]*$"), this));
    connect(trophyKeyEdit, &QLineEdit::textChanged, this, &CryptoManagerDialog::ValidateHexInput);

    trophyLayout->addWidget(new QLabel(tr("Trophy Key:")));
    trophyLayout->addWidget(trophyKeyEdit);

    trophyStatusLabel = new QLabel(tr("Status: Not loaded"));
    trophyStatusLabel->setStyleSheet("QLabel { color: gray; font-weight: bold; }");
    trophyLayout->addWidget(trophyStatusLabel);

    trophyLayout->addStretch();
    tabWidget->addTab(trophyTab, tr("Trophy Key"));

    // ----------------------------
    // Helper Lambda: Create RSA Keyset Tab
    // ----------------------------
    auto createKeysetTab = [this](const QString& title, const QString& description,
                                  KeysetFields& fields, QLabel*& statusLabel) -> QWidget* {
        QWidget* tab = new QWidget();
        QVBoxLayout* layout = new QVBoxLayout(tab);

        QLabel* descLabel = new QLabel(description);
        descLabel->setWordWrap(true);
        layout->addWidget(descLabel);

        QWidget* fieldsWidget = new QWidget();
        QGridLayout* grid = new QGridLayout(fieldsWidget);

        int row = 0;
        auto createHexTextEdit = [this, &grid](const QString& label,
                                               int rowIndex) -> QPlainTextEdit* {
            QLabel* lbl = new QLabel(label + ":");
            QPlainTextEdit* edit = new HexPlainTextEdit();
            edit->setProperty("rsaField", label);
            edit->setPlaceholderText(tr("Enter %1").arg(label));
            edit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
            edit->setMaximumHeight(60);

            // Capture 'this' so IsValidHexString can be called
            connect(edit, &QPlainTextEdit::textChanged, [this, edit]() {
                QString hex = edit->toPlainText().remove(QRegularExpression("\\s"));

                bool valid = IsValidHexString(hex);

                QString fieldName = edit->property("rsaField").toString();
                if (valid && kRsaConstraints.contains(fieldName)) {
                    valid &= CheckHexSize(hex, kRsaConstraints[fieldName]);
                }

                edit->setStyleSheet(valid ? "" : "QPlainTextEdit { border: 2px solid red; }");
            });

            grid->addWidget(lbl, rowIndex, 0);
            grid->addWidget(edit, rowIndex, 1);
            return edit;
        };

        fields.exponent1 = createHexTextEdit(tr("Exponent1"), row++);
        fields.exponent2 = createHexTextEdit(tr("Exponent2"), row++);
        fields.publicExponent = createHexTextEdit(tr("Public Exponent"), row++);
        fields.coefficient = createHexTextEdit(tr("Coefficient"), row++);
        fields.modulus = createHexTextEdit(tr("Modulus"), row++);
        fields.prime1 = createHexTextEdit(tr("Prime 1"), row++);
        fields.prime2 = createHexTextEdit(tr("Prime 2"), row++);
        fields.privateExponent = createHexTextEdit(tr("Private Exponent"), row++);

        fieldsWidget->setMinimumWidth(500);
        fieldsWidget->setMinimumHeight(row * 60);

        QScrollArea* scroll = new QScrollArea();
        scroll->setWidgetResizable(true);
        scroll->setWidget(fieldsWidget);
        scroll->setMinimumHeight(600);
        layout->addWidget(scroll);

        statusLabel = new QLabel(tr("Status: Not loaded"));
        statusLabel->setStyleSheet("QLabel { color: gray; font-weight: bold; }");
        layout->addWidget(statusLabel);

        layout->addStretch();
        // To do: uncomment tabs when needed
        // tabWidget->addTab(tab, title);

        return tab;
    };

    // ----------------------------
    // Create all keyset tabs
    // ----------------------------
    fakeTab = createKeysetTab(tr("Fake Keyset"),
                              tr("RSA key components for fake signing.\nAll fields must be valid "
                                 "hex strings with even length."),
                              fakeKeysetFields, fakeStatusLabel);

    debugTab = createKeysetTab(tr("Debug Rif Keyset"),
                               tr("RSA key components for debug RIF files.\nAll fields must be "
                                  "valid hex strings with even length."),
                               debugKeysetFields, debugStatusLabel);

    pkgTab = createKeysetTab(tr("Pkg Derived Key3"),
                             tr("RSA key components for package decryption.\nAll fields must be "
                                "valid hex strings with even length."),
                             pkgKeysetFields, pkgStatusLabel);

    mainLayout->addWidget(tabWidget);

    // ----------------------------
    // Global buttons
    // ----------------------------
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    loadButton = new QPushButton(tr("Load Key"));
    saveButton = new QPushButton(tr("Save Key"));
    closeButton = new QPushButton(tr("Close"));

    connect(loadButton, &QPushButton::clicked, this, &CryptoManagerDialog::LoadKeys);
    connect(saveButton, &QPushButton::clicked, this, &CryptoManagerDialog::SaveKeys);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);

    buttonLayout->addWidget(loadButton);
    buttonLayout->addWidget(saveButton);
    buttonLayout->addWidget(closeButton);
    mainLayout->addLayout(buttonLayout);
}

// ----------------------------
// Load / Save Keys
// ----------------------------
void CryptoManagerDialog::LoadKeys() {
    auto keyManager = KeyManager::GetInstance();
    if (keyManager->LoadFromFile()) {
        currentKeys = keyManager->GetAllKeys();
    } else {
        QMessageBox::warning(
            this, tr("Warning"),
            tr("No key file found or failed to load.\nPlease enter your keys and save them."));
        keyManager->SetDefaultKeys();
        currentKeys = keyManager->GetAllKeys();
    }
    PopulateFields();
    UpdateAllTabStatuses();
}

void CryptoManagerDialog::SaveKeys() {
    if (!ValidateAllFields()) {
        QMessageBox::warning(this, tr("Validation Error"), GetFirstValidationError());
        return;
    }

    CollectKeys();

    auto keyManager = KeyManager::GetInstance();
    keyManager->SetAllKeys(currentKeys);

    if (keyManager->SaveToFile()) {
        UpdateAllTabStatuses();
        QMessageBox::information(this, tr("Success"), tr("Keys saved successfully!"));
    } else {
        QMessageBox::critical(this, tr("Error"), tr("Failed to save keys!"));
    }
}

// ----------------------------
// Validation
// ----------------------------
void CryptoManagerDialog::ValidateHexInput() {
    if (auto* edit = qobject_cast<QLineEdit*>(sender())) {
        bool valid = IsValidHexString(edit->text());
        edit->setStyleSheet(valid ? "" : "QLineEdit { border: 2px solid red; }");
    }
}

bool CryptoManagerDialog::ValidateFields(const QVector<QLineEdit*>& fields) {
    bool allValid = true;
    for (QLineEdit* edit : fields) {
        if (!edit)
            continue;
        QString text = edit->text();
        bool valid = text.isEmpty() || (text.length() % 2 == 0);
        edit->setStyleSheet(valid ? "" : "QLineEdit { border: 2px solid red; }");
        allValid &= valid;
    }
    return allValid;
}

bool CryptoManagerDialog::ValidateAllFields() {
    if (!IsValidHexString(trophyKeyEdit->text()))
        return false;

    auto validateKeyset = [&](const KeysetFields& kf) {
        for (auto* edit : kf.all()) {
            QString hex = edit->toPlainText().remove(QRegularExpression("\\s"));

            if (!IsValidHexString(hex))
                return false;

            if (hex.isEmpty())
                continue;

            QString name = edit->property("rsaField").toString();
            if (kRsaConstraints.contains(name)) {
                if (!CheckHexSize(hex, kRsaConstraints[name]))
                    return false;
            }
        }
        return true;
    };

    return validateKeyset(fakeKeysetFields) && validateKeyset(debugKeysetFields) &&
           validateKeyset(pkgKeysetFields);
}

// ----------------------------
// Tab status
// ----------------------------
void CryptoManagerDialog::UpdateTabStatus(QVector<QWidget*> fields, QWidget* requiredField,
                                          QLabel* statusLabel) {
    bool anyInvalid = false;
    bool allEmpty = true;

    for (QWidget* field : fields) {
        QString hex = ExtractHex(field);

        if (!hex.isEmpty())
            allEmpty = false;

        if (!IsValidHexString(hex))
            anyInvalid = true;

        QString name = field->property("rsaField").toString();
        if (!hex.isEmpty() && kRsaConstraints.contains(name)) {
            if (!CheckHexSize(hex, kRsaConstraints[name]))
                anyInvalid = true;
        }
    }

    bool missingRequired = false;
    if (requiredField)
        missingRequired = ExtractHex(requiredField).isEmpty();

    if (allEmpty) {
        statusLabel->setText(tr("Status: All fields empty"));
        statusLabel->setStyleSheet("QLabel { color: gray; font-weight: bold; }");
    } else if (anyInvalid) {
        statusLabel->setText(tr("Status: Invalid hex values"));
        statusLabel->setStyleSheet("QLabel { color: red; font-weight: bold; }");
    } else if (missingRequired) {
        statusLabel->setText(tr("Status: Incomplete"));
        statusLabel->setStyleSheet("QLabel { color: orange; font-weight: bold; }");
    } else {
        statusLabel->setText(tr("Status: Valid"));
        statusLabel->setStyleSheet("QLabel { color: green; font-weight: bold; }");
    }
}

void CryptoManagerDialog::UpdateAllTabStatuses() {
    // Trophy
    UpdateTabStatus({trophyKeyEdit}, trophyKeyEdit, trophyStatusLabel);
    // RSA keysets
    auto toWidgetVec = [](const KeysetFields& kf) {
        QVector<QWidget*> v;
        for (auto edit : kf.all())
            v.push_back(edit);
        return v;
    };
    UpdateTabStatus(toWidgetVec(fakeKeysetFields), fakeKeysetFields.modulus, fakeStatusLabel);
    UpdateTabStatus(toWidgetVec(debugKeysetFields), debugKeysetFields.modulus, debugStatusLabel);
    UpdateTabStatus(toWidgetVec(pkgKeysetFields), pkgKeysetFields.modulus, pkgStatusLabel);
}

// ----------------------------
// Populate / Collect Keys
// ----------------------------
void CryptoManagerDialog::PopulateFields() {
    trophyKeyEdit->setText(QString::fromStdString(
        KeyManager::BytesToHexString(currentKeys.TrophyKeySet.ReleaseTrophyKey)));

    auto fillKeyset = [](const KeysetFields& fields, const auto& keyset) {
        fields.exponent1->setPlainText(
            QString::fromStdString(KeyManager::BytesToHexString(keyset.Exponent1)));
        fields.exponent2->setPlainText(
            QString::fromStdString(KeyManager::BytesToHexString(keyset.Exponent2)));
        fields.publicExponent->setPlainText(
            QString::fromStdString(KeyManager::BytesToHexString(keyset.PublicExponent)));
        fields.coefficient->setPlainText(
            QString::fromStdString(KeyManager::BytesToHexString(keyset.Coefficient)));
        fields.modulus->setPlainText(
            QString::fromStdString(KeyManager::BytesToHexString(keyset.Modulus)));
        fields.prime1->setPlainText(
            QString::fromStdString(KeyManager::BytesToHexString(keyset.Prime1)));
        fields.prime2->setPlainText(
            QString::fromStdString(KeyManager::BytesToHexString(keyset.Prime2)));
        fields.privateExponent->setPlainText(
            QString::fromStdString(KeyManager::BytesToHexString(keyset.PrivateExponent)));
    };

    fillKeyset(fakeKeysetFields, currentKeys.FakeKeyset);
    fillKeyset(debugKeysetFields, currentKeys.DebugRifKeyset);
    fillKeyset(pkgKeysetFields, currentKeys.PkgDerivedKey3Keyset);
}

void CryptoManagerDialog::CollectKeys() {
    currentKeys.TrophyKeySet.ReleaseTrophyKey =
        KeyManager::HexStringToBytes(trophyKeyEdit->text().toUpper().toStdString());

    auto collectKeyset = [](const KeysetFields& fields, auto& keyset) {
        auto getText = [](QPlainTextEdit* edit) {
            return KeyManager::HexStringToBytes(
                edit->toPlainText().remove(QRegularExpression("\\s")).toUpper().toStdString());
        };
        keyset.Exponent1 = getText(fields.exponent1);
        keyset.Exponent2 = getText(fields.exponent2);
        keyset.PublicExponent = getText(fields.publicExponent);
        keyset.Coefficient = getText(fields.coefficient);
        keyset.Modulus = getText(fields.modulus);
        keyset.Prime1 = getText(fields.prime1);
        keyset.Prime2 = getText(fields.prime2);
        keyset.PrivateExponent = getText(fields.privateExponent);
    };

    collectKeyset(fakeKeysetFields, currentKeys.FakeKeyset);
    collectKeyset(debugKeysetFields, currentKeys.DebugRifKeyset);
    collectKeyset(pkgKeysetFields, currentKeys.PkgDerivedKey3Keyset);
}

void CryptoManagerDialog::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        const QList<QUrl> urls = event->mimeData()->urls();
        if (!urls.isEmpty()) {
            QString path = urls.first().toLocalFile();
            if (path.endsWith(".json", Qt::CaseInsensitive)) {
                event->acceptProposedAction();
                return;
            }
        }
    }
    event->ignore();
}

void CryptoManagerDialog::dropEvent(QDropEvent* event) {
    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.isEmpty())
        return;

    QString path = urls.first().toLocalFile();
    if (!path.endsWith(".json", Qt::CaseInsensitive)) {
        QMessageBox::warning(this, tr("Invalid File"), tr("Only JSON files are supported."));
        return;
    }

    QFile file(path);
    if (!file.open(QFile::ReadOnly)) {
        QMessageBox::warning(this, tr("Error"), tr("Unable to open file."));
        return;
    }

    QByteArray raw = file.readAll();
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(raw, &err);

    if (err.error != QJsonParseError::NoError) {
        QMessageBox::critical(this, tr("JSON Error"), err.errorString());
        return;
    }

    if (!doc.isObject()) {
        QMessageBox::critical(this, tr("Error"), tr("Invalid JSON format."));
        return;
    }

    QJsonObject root = doc.object();
    LoadJsonIntoFields(root);
    CollectKeys();
    UpdateAllTabStatuses();

    QMessageBox::information(this, tr("Success"), tr("Keys successfully loaded from JSON!"));
}

void CryptoManagerDialog::LoadJsonIntoFields(const QJsonObject& root) {
    // -------------------------
    // Trophy Key
    // -------------------------
    if (root.contains("TrophyKeySet") && root["TrophyKeySet"].isObject()) {
        QJsonObject tk = root["TrophyKeySet"].toObject();

        if (tk.contains("ReleaseTrophyKey"))
            trophyKeyEdit->setText(tk["ReleaseTrophyKey"].toString());
    }

    // -------------------------
    // Fake Keyset
    // -------------------------
    if (root.contains("FakeKeyset") && root["FakeKeyset"].isObject()) {
        QJsonObject fk = root["FakeKeyset"].toObject();

        LoadJsonField(fk, "Exponent1", fakeKeysetFields.exponent1);
        LoadJsonField(fk, "Exponent2", fakeKeysetFields.exponent2);
        LoadJsonField(fk, "PublicExponent", fakeKeysetFields.publicExponent);
        LoadJsonField(fk, "Coefficient", fakeKeysetFields.coefficient);
        LoadJsonField(fk, "Modulus", fakeKeysetFields.modulus);
        LoadJsonField(fk, "Prime1", fakeKeysetFields.prime1);
        LoadJsonField(fk, "Prime2", fakeKeysetFields.prime2);
        LoadJsonField(fk, "PrivateExponent", fakeKeysetFields.privateExponent);
    }

    // -------------------------
    // Debug Rif Keyset
    // -------------------------
    if (root.contains("DebugRifKeyset") && root["DebugRifKeyset"].isObject()) {
        QJsonObject dr = root["DebugRifKeyset"].toObject();

        LoadJsonField(dr, "Exponent1", debugKeysetFields.exponent1);
        LoadJsonField(dr, "Exponent2", debugKeysetFields.exponent2);
        LoadJsonField(dr, "PublicExponent", debugKeysetFields.publicExponent);
        LoadJsonField(dr, "Coefficient", debugKeysetFields.coefficient);
        LoadJsonField(dr, "Modulus", debugKeysetFields.modulus);
        LoadJsonField(dr, "Prime1", debugKeysetFields.prime1);
        LoadJsonField(dr, "Prime2", debugKeysetFields.prime2);
        LoadJsonField(dr, "PrivateExponent", debugKeysetFields.privateExponent);
    }

    // -------------------------
    // Package Keyset
    // -------------------------
    if (root.contains("PkgDerivedKey3Keyset") && root["PkgDerivedKey3Keyset"].isObject()) {
        QJsonObject pk = root["PkgDerivedKey3Keyset"].toObject();

        LoadJsonField(pk, "Exponent1", pkgKeysetFields.exponent1);
        LoadJsonField(pk, "Exponent2", pkgKeysetFields.exponent2);
        LoadJsonField(pk, "PublicExponent", pkgKeysetFields.publicExponent);
        LoadJsonField(pk, "Coefficient", pkgKeysetFields.coefficient);
        LoadJsonField(pk, "Modulus", pkgKeysetFields.modulus);
        LoadJsonField(pk, "Prime1", pkgKeysetFields.prime1);
        LoadJsonField(pk, "Prime2", pkgKeysetFields.prime2);
        LoadJsonField(pk, "PrivateExponent", pkgKeysetFields.privateExponent);
    }
}

void CryptoManagerDialog::LoadJsonField(const QJsonObject& obj, const QString& key,
                                        QPlainTextEdit* edit) {
    if (obj.contains(key) && obj[key].isString()) {
        edit->setPlainText(obj[key].toString());
    }
}

QString CryptoManagerDialog::GetFirstValidationError() {
    // Trophy key
    if (!IsValidHexString(trophyKeyEdit->text())) {
        return tr("Trophy Key contains invalid hex or has odd length.");
    }

    auto checkKeyset = [&](const KeysetFields& kf) -> QString {
        for (auto* edit : kf.all()) {
            QString hex = edit->toPlainText().remove(QRegularExpression("\\s"));

            if (!IsValidHexString(hex)) {
                return tr("%1 contains invalid hex or has odd length.")
                    .arg(edit->property("rsaField").toString());
            }

            if (hex.isEmpty())
                continue;

            QString name = edit->property("rsaField").toString();
            if (kRsaConstraints.contains(name)) {
                const auto& c = kRsaConstraints[name];
                int bytes = hex.length() / 2;

                if (!CheckHexSize(hex, c)) {
                    return FormatSizeError(tr(c.displayName), bytes, c);
                }
            }
        }
        return {};
    };

    QString err;
    if (!(err = checkKeyset(fakeKeysetFields)).isEmpty())
        return err;
    if (!(err = checkKeyset(debugKeysetFields)).isEmpty())
        return err;
    if (!(err = checkKeyset(pkgKeysetFields)).isEmpty())
        return err;

    return {};
}
