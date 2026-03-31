// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QFont>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QSpacerItem>
#include <pugixml.hpp>

#include "common/path_util.h"
#include "patch_editor.h"

using namespace CustomPatches;

PatchEditor::PatchEditor(std::filesystem::path patchPath,
                         std::vector<CustomPatches::ConfigPatchInfo> patches, QWidget* parent)
    : QDialog(parent), gamePatches(patches), patchFile(patchPath) {
    setupUI();
    setWindowTitle(tr("Patch Configuration"));

    populatePatchList();
    refreshValueList();
    resize(500, 300);

    if (patchList->count() == 0)
        patchList->addItem(tr("No configurable patches found"));

    connect(patchList, &QListWidget::itemSelectionChanged, this, &PatchEditor::refreshValueList);
    connect(optionValues, &QComboBox::currentIndexChanged, this, &PatchEditor::refreshOptionDesc);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &PatchEditor::savePatches);
}

void PatchEditor::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    QHBoxLayout* patchesLayout = new QHBoxLayout(this);

    QVBoxLayout* patchListLayout = new QVBoxLayout(this);
    QLabel* patchListHeader = new QLabel(tr("Configurable Patches"));
    QFont font = patchListHeader->font();
    font.setPointSize(12);
    font.setBold(true);
    patchListHeader->setFont(font);

    patchListLayout->addWidget(patchListHeader);
    patchListLayout->addWidget(patchList);

    QLabel* patchValuesHeader = new QLabel(tr("Configurable Values"));
    patchValuesHeader->setFont(font);

    optionLabel->setText(tr("Options for Selected Patch"));
    optionDesc->setText(tr("Description: "));
    optionDesc->setWordWrap(true);
    optionValues->addItem(tr("No patch selected"));

    QVBoxLayout* patchValuesLayout = new QVBoxLayout(this);
    patchValuesLayout->addWidget(patchValuesHeader);
    patchValuesLayout->addWidget(optionLabel);
    patchValuesLayout->addWidget(optionValues);
    patchValuesLayout->addSpacing(20);
    patchValuesLayout->addWidget(optionDesc);
    patchValuesLayout->addStretch(1);

    patchesLayout->addLayout(patchListLayout, 1);
    patchesLayout->addLayout(patchValuesLayout, 1);

    buttonBox = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Close);
    buttonBox->button(QDialogButtonBox::Save)->setText(tr("Save Current Patch"));

    QHBoxLayout* buttonLayout = new QHBoxLayout(this);
    buttonLayout->addWidget(buttonBox);

    mainLayout->addLayout(patchesLayout);
    mainLayout->addLayout(buttonLayout);
    this->setLayout(mainLayout);
}

void PatchEditor::populatePatchList() {
    for (const ConfigPatchInfo& patch : gamePatches) {
        patchList->addItem(patch.patchName);
    }
}

void PatchEditor::refreshValueList() {
    if (!patchList->selectedItems().isEmpty()) {
        QString patchname = patchList->selectedItems().first()->text();
        for (const auto& patch : gamePatches) {
            if (patchname == patch.patchName) {
                currentPatch = patch;
                populateValues(patch);
            }
        }
    }
}

void PatchEditor::refreshOptionDesc(int index) {
    optionDesc->setText("Description:\n" + currentPatch.optionData[index].optionNotes);
}

void PatchEditor::populateValues(ConfigPatchInfo patch) {
    optionDesc->setText("Description:\n" + patch.optionData[0].optionNotes);
    optionValues->clear();

    for (auto const& data : patch.optionData) {
        optionValues->addItem(data.optionName);
    }
}

void PatchEditor::savePatches() {
    if (patchList->selectedItems().empty()) {
        QMessageBox::warning(this, tr("Error"), tr("No Patch Selected"));
        return;
    }

    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_file(patchFile.c_str());

    if (!result) {
        QMessageBox::critical(this, tr("Failed to load patch xml file"),
                              QString::fromStdString(result.description()));
        return;
    }

    auto patchDoc = doc.child("Patch");
    pugi::xml_node patchListNode;

    for (pugi::xml_node& node : patchDoc.children()) {
        if (std::string_view(node.name()) == "Metadata") {
            std::string name = node.attribute("Name").as_string();
            if (name == currentPatch.patchName.toStdString()) {
                patchListNode = node.child("PatchList");
            }
        }
    }

    CustomPatches::OptionData selectedOption =
        currentPatch.optionData[optionValues->currentIndex()];
    for (const std::tuple<std::string, int>& value : selectedOption.modifiedValues) {
        std::string valueAddress = std::get<0>(value);
        int valueModified = std::get<1>(value);

        for (pugi::xml_node& node : patchListNode.children()) {
            std::string xmlAddress = node.attribute("Address").as_string();
            if (xmlAddress == valueAddress) {
                std::string type = node.attribute("Type").as_string();
                QString valueString;

                if (type != "bytes32") {
                    valueString = QString::number(valueModified, 16);
                } else {
                    // Not sure if needed, but everything in bytes32 is currently like this
                    valueString = "0x" + QString::number(valueModified, 16).rightJustified(8, '0');
                }

                node.attribute("Value").set_value(valueString.toStdString().c_str());
            }
        }
    }

    doc.save_file(patchFile.c_str());
    QMessageBox::information(this, tr("Patch Saved"), tr("Patch saved successfully"));
}

PatchEditor::~PatchEditor() {}
