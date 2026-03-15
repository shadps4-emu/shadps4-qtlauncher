// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <filesystem>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QSpinBox>
#include <QVBoxLayout>

#include "configurable_patches.h"

class PatchEditor : public QDialog {
    Q_OBJECT

public:
    PatchEditor(std::filesystem::path patchPath, QWidget* parent = nullptr);
    ~PatchEditor();

signals:
    //

private:
    void setupUI();
    void populatePatchList();
    void populateValues(CustomPatches::ConfigPatchInfo, int count);
    void refreshValueList();
    void savePatches();

    std::filesystem::path patchFile;

    QListWidget* patchList = new QListWidget(this);
    QDialogButtonBox* buttonBox;

    QList<QLabel*> Labels;
    QList<QSpinBox*> SpinBoxes;
    std::vector<CustomPatches::ConfigPatchInfo> currentPatches;
};
