// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QFile>
#include <QFont>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QMessageBox>
#include <QSpacerItem>
#include <QXmlStreamReader>
#include <pugixml.hpp>

#include "common/logging/log.h"
#include "common/path_util.h"
#include "patch_editor.h"

using namespace CustomPatches;

PatchEditor::PatchEditor(std::filesystem::path patchPath, QWidget* parent)
    : QDialog(parent), patchFile(patchPath) {
    setupUI();
    setWindowTitle(tr("Patch Editor"));

    currentPatches = GetConfigPatchInfo(patchFile);

    populatePatchList();
    refreshValueList();
    resize(500, 300);

    if (patchList->count() == 0)
        patchList->addItem(tr("No configurable patches found"));

    connect(patchList, &QListWidget::itemSelectionChanged, this, &PatchEditor::refreshValueList);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &PatchEditor::savePatches);
}

void PatchEditor::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    QHBoxLayout* patchesLayout = new QHBoxLayout(this);

    QVBoxLayout* valuesLayout1 = new QVBoxLayout(this);
    QVBoxLayout* valuesLayout2 = new QVBoxLayout(this);
    QVBoxLayout* valuesLayout3 = new QVBoxLayout(this);
    QVBoxLayout* valuesLayout4 = new QVBoxLayout(this);
    QVBoxLayout* valuesLayout5 = new QVBoxLayout(this);

    QVBoxLayout* patchListLayout = new QVBoxLayout(this);
    QLabel* patchListLabel = new QLabel(tr("Configurable Patches"));
    QFont font = patchListLabel->font();
    font.setPointSize(12);
    font.setBold(true);
    patchListLabel->setFont(font);

    patchListLayout->addWidget(patchListLabel);
    patchListLayout->addWidget(patchList);

    QLabel* patchValuesHeader = new QLabel(tr("Configurable Values"));
    patchValuesHeader->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    patchValuesHeader->setFont(font);

    QLabel* patchValueLabel1 = new QLabel("Value 1");
    QSpinBox* patchValueEdit1 = new QSpinBox();
    patchValueEdit1->setButtonSymbols(QSpinBox::NoButtons);
    valuesLayout1->addWidget(patchValueLabel1);
    valuesLayout1->addWidget(patchValueEdit1);

    QLabel* patchValueLabel2 = new QLabel("Value 2");
    QSpinBox* patchValueEdit2 = new QSpinBox();
    patchValueEdit2->setButtonSymbols(QSpinBox::NoButtons);
    valuesLayout2->addWidget(patchValueLabel2);
    valuesLayout2->addWidget(patchValueEdit2);

    QLabel* patchValueLabel3 = new QLabel("Value 3");
    QSpinBox* patchValueEdit3 = new QSpinBox();
    patchValueEdit3->setButtonSymbols(QSpinBox::NoButtons);
    valuesLayout3->addWidget(patchValueLabel3);
    valuesLayout3->addWidget(patchValueEdit3);

    QLabel* patchValueLabel4 = new QLabel("Value 4");
    QSpinBox* patchValueEdit4 = new QSpinBox();
    patchValueEdit4->setButtonSymbols(QSpinBox::NoButtons);
    valuesLayout4->addWidget(patchValueLabel4);
    valuesLayout4->addWidget(patchValueEdit4);

    QLabel* patchValueLabel5 = new QLabel("Value 5");
    QSpinBox* patchValueEdit5 = new QSpinBox();
    patchValueEdit5->setButtonSymbols(QSpinBox::NoButtons);
    valuesLayout5->addWidget(patchValueLabel5);
    valuesLayout5->addWidget(patchValueEdit5);

    Labels = {patchValueLabel1, patchValueLabel2, patchValueLabel3, patchValueLabel4,
              patchValueLabel5};
    SpinBoxes = {patchValueEdit1, patchValueEdit2, patchValueEdit3, patchValueEdit4,
                 patchValueEdit5};

    QVBoxLayout* patchValuesLayout = new QVBoxLayout(this);
    patchValuesLayout->addWidget(patchValuesHeader);
    patchValuesLayout->addLayout(valuesLayout1);
    patchValuesLayout->addLayout(valuesLayout2);
    patchValuesLayout->addLayout(valuesLayout3);
    patchValuesLayout->addLayout(valuesLayout4);
    patchValuesLayout->addLayout(valuesLayout5);
    patchValuesLayout->addStretch(1);

    patchesLayout->addLayout(patchListLayout);
    patchesLayout->addLayout(patchValuesLayout);

    buttonBox = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    QHBoxLayout* buttonLayout = new QHBoxLayout(this);
    buttonLayout->addWidget(buttonBox);

    mainLayout->addLayout(patchesLayout);
    mainLayout->addLayout(buttonLayout);
    this->setLayout(mainLayout);
}

void PatchEditor::populatePatchList() {
    for (const ConfigPatchInfo& patch : currentPatches) {
        patchList->addItem(patch.patchName);
    }
}

void PatchEditor::refreshValueList() {
    if (!patchList->selectedItems().isEmpty()) {
        QString patchname = patchList->selectedItems().first()->text();
        int count;
        for (const auto& patch : currentPatches) {
            if (patchname == patch.patchName) {
                count = patch.patchData.size();
                populateValues(patch, count);
            }
        }
    } else {
        populateValues({}, -1);
    }
}

void PatchEditor::populateValues(ConfigPatchInfo patch, int count) {
    for (int i = 0; i < count; i++) {
        Labels[i]->setText(patch.patchData[i].dataName);
        QString address = patch.patchData[i].address;

        QString patchPatch;
        Common::FS::PathToQString(patchPatch, patchFile);
        QFile file(patchPatch);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            LOG_ERROR(Loader, "Unable to open the file for reading.");
            continue;
        }
        const QByteArray xmlData = file.readAll();
        file.close();

        QXmlStreamReader xmlReader(xmlData);
        std::string currentPatchName;

        while (!xmlReader.atEnd()) {
            xmlReader.readNext();

            if (!xmlReader.isStartElement()) {
                continue;
            }

            if (xmlReader.name() == QStringLiteral("Metadata")) {
                QString name = xmlReader.attributes().value("Name").toString();
                currentPatchName = name.toStdString();
                const QString appVer = xmlReader.attributes().value("AppVer").toString();
            } else if (xmlReader.name() == QStringLiteral("PatchList")) {
                while (!xmlReader.atEnd() &&
                       !(xmlReader.tokenType() == QXmlStreamReader::EndElement &&
                         xmlReader.name() == QStringLiteral("PatchList"))) {

                    xmlReader.readNext();

                    if (xmlReader.tokenType() != QXmlStreamReader::StartElement ||
                        xmlReader.name() != QStringLiteral("Line")) {
                        continue;
                    }

                    const QXmlStreamAttributes a = xmlReader.attributes();
                    const QString addr = a.value("Address").toString();
                    QString val = a.value("Value").toString();
                    bool success;

                    if (currentPatchName == patch.patchName && addr == address) {
                        int decValue = val.toInt(&success, 16);
                        int minVal = patch.patchData[i].minValue;
                        int maxVal = patch.patchData[i].maxValue;

                        SpinBoxes[i]->setMinimum(minVal);
                        SpinBoxes[i]->setMaximum(maxVal);
                        SpinBoxes[i]->setValue(decValue);
                    }
                }
            }
        }

        if (xmlReader.hasError()) {
            QMessageBox::critical(this, tr("Error"), tr("Failed to load patch xml file"));
        }
    }

    for (int i = 0; i < 5; i++) {
        bool visible = i < count ? true : false;
        Labels[i]->setVisible(visible);
        SpinBoxes[i]->setVisible(visible);
    }
}

void PatchEditor::savePatches() {
    if (patchList->selectedItems().empty()) {
        QMessageBox::warning(this, tr("Error"), tr("No Patch Selected"));
        return;
    }

    QString name = patchList->selectedItems().first()->text();
    ConfigPatchInfo patchInfo;

    for (const ConfigPatchInfo& patch : currentPatches) {
        if (patch.patchName == name) {
            patchInfo = patch;
        }
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
            if (name == patchInfo.patchName.toStdString()) {
                patchListNode = node.child("PatchList");
            }
        }
    }

    for (int i = 0; i < patchInfo.patchData.size(); i++) {
        for (pugi::xml_node& node : patchListNode.children()) {
            std::string address = node.attribute("Address").as_string();
            if (address == patchInfo.patchData[i].address) {
                int value = SpinBoxes[i]->value();
                std::string type = node.attribute("Type").as_string();
                QString valueString;

                if (type != "bytes32") {
                    valueString = "0x" + QString::number(value, 16);
                } else {
                    // Not sure if needed, but everything in bytes32 is currently like this
                    valueString = "0x" + QString::number(value, 16).rightJustified(8, '0');
                }

                node.attribute("Value").set_value(valueString.toStdString().c_str());
            }
        }
    }

    doc.save_file(patchFile.c_str());
    QMessageBox::information(this, tr("Patch Saved"), tr("Patch saved successfully"));
}

PatchEditor::~PatchEditor() {}
