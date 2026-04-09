// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <fstream>
#include <QCheckBox>
#include <QDockWidget>
#include <QGuiApplication>
#include <QMessageBox>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <cmrc/cmrc.hpp>

#include "common/logging/log.h"
#include "common/path_util.h"
#include "core/emulator_settings.h"
#include "core/file_format/npbind.h"
#include "core/user_settings.h"
#include "main_window_themes.h"
#include "trophy_viewer.h"

static constexpr std::array<std::string_view, 31> s_language_xml_names = {
    "TROP_00.XML", // 00 Japanese
    "TROP_01.XML", // 01 English (US)
    "TROP_02.XML", // 02 French
    "TROP_03.XML", // 03 Spanish (ES)
    "TROP_04.XML", // 04 German
    "TROP_05.XML", // 05 Italian
    "TROP_06.XML", // 06 Dutch
    "TROP_07.XML", // 07 Portuguese (PT)
    "TROP_08.XML", // 08 Russian
    "TROP_09.XML", // 09 Korean
    "TROP_10.XML", // 10 Traditional Chinese
    "TROP_11.XML", // 11 Simplified Chinese
    "TROP_12.XML", // 12 Finnish
    "TROP_13.XML", // 13 Swedish
    "TROP_14.XML", // 14 Danish
    "TROP_15.XML", // 15 Norwegian
    "TROP_16.XML", // 16 Polish
    "TROP_17.XML", // 17 Portuguese (BR)
    "TROP_18.XML", // 18 English (GB)
    "TROP_19.XML", // 19 Turkish
    "TROP_20.XML", // 20 Spanish (LA)
    "TROP_21.XML", // 21 Arabic
    "TROP_22.XML", // 22 French (CA)
    "TROP_23.XML", // 23 Czech
    "TROP_24.XML", // 24 Hungarian
    "TROP_25.XML", // 25 Greek
    "TROP_26.XML", // 26 Romanian
    "TROP_27.XML", // 27 Thai
    "TROP_28.XML", // 28 Vietnamese
    "TROP_29.XML", // 29 Indonesian
    "TROP_30.XML", // 30 Unkrainian
};

// Returns the best available trophy XML path for the current system language.
// Resolution order:
//   1. TROP_XX.XML for the active system language (e.g. TROP01.XML for English)
//   2. TROP.XML    (master / language-neutral fallback)
static std::filesystem::path GetTrophyXmlPath(const std::filesystem::path& xml_dir,
                                              int system_language) {
    // Try the exact language file first.
    if (system_language >= 0 && system_language < static_cast<int>(s_language_xml_names.size())) {
        auto lang_path = xml_dir / s_language_xml_names[system_language];
        if (std::filesystem::exists(lang_path)) {
            return lang_path;
        }
    }
    // Final fallback: master TROP.XML (always present).
    return xml_dir / "TROP.XML";
}

namespace fs = std::filesystem;

CMRC_DECLARE(res);

// true: European format; false: American format
bool useEuropeanDateFormat = true;

void TrophyViewer::resizeEvent(QResizeEvent* event) {
    if (!programmaticResize_) {
        userResizedWindow_ = true;
    }
    QMainWindow::resizeEvent(event);
}

void TrophyViewer::updateTrophyInfo() {
    int total = 0;
    int unlocked = 0;

    // Cycles through each tab (table) of the QTabWidget
    for (int i = 0; i < tabWidget->count(); i++) {
        QTableWidget* table = qobject_cast<QTableWidget*>(tabWidget->widget(i));
        if (table) {
            total += table->rowCount();
            for (int row = 0; row < table->rowCount(); ++row) {
                QString cellText;
                // The "Unlocked" column can be a widget or a simple item
                QWidget* widget = table->cellWidget(row, 0);
                if (widget) {
                    // Looks for the QLabel inside the widget (as defined in SetTableItem)
                    QLabel* label = widget->findChild<QLabel*>();
                    if (label) {
                        cellText = label->text();
                    }
                } else {
                    QTableWidgetItem* item = table->item(row, 0);
                    if (item) {
                        cellText = item->text();
                    }
                }
                if (cellText == "unlocked")
                    unlocked++;
            }
        }
    }
    int progress = (total > 0) ? (unlocked * 100 / total) : 0;
    trophyInfoLabel->setText(
        QString(tr("Progress") + ": %1% (%2/%3)").arg(progress).arg(unlocked).arg(total));
}

void TrophyViewer::updateTableFilters() {
    bool showEarned = showEarnedCheck->isChecked();
    bool showNotEarned = showNotEarnedCheck->isChecked();
    bool showHidden = showHiddenCheck->isChecked();

    // Cycles through each tab of the QTabWidget
    for (int i = 0; i < tabWidget->count(); ++i) {
        QTableWidget* table = qobject_cast<QTableWidget*>(tabWidget->widget(i));
        if (!table)
            continue;
        for (int row = 0; row < table->rowCount(); ++row) {
            QString unlockedText;
            // Gets the text of the "Unlocked" column (index 0)
            QWidget* widget = table->cellWidget(row, 0);
            if (widget) {
                QLabel* label = widget->findChild<QLabel*>();
                if (label)
                    unlockedText = label->text();
            } else {
                QTableWidgetItem* item = table->item(row, 0);
                if (item)
                    unlockedText = item->text();
            }

            QString hiddenText;
            // Gets the text of the "Hidden" column (index 7)
            QWidget* hiddenWidget = table->cellWidget(row, 7);
            if (hiddenWidget) {
                QLabel* label = hiddenWidget->findChild<QLabel*>();
                if (label)
                    hiddenText = label->text();
            } else {
                QTableWidgetItem* item = table->item(row, 7);
                if (item)
                    hiddenText = item->text();
            }

            bool visible = true;
            if (unlockedText == "unlocked" && !showEarned)
                visible = false;
            if (unlockedText == "locked" && !showNotEarned)
                visible = false;
            if (hiddenText.toLower() == "yes" && !showHidden)
                visible = false;

            table->setRowHidden(row, !visible);
        }
    }
}

TrophyViewer::TrophyViewer(std::shared_ptr<gui_settings> gui_settings, QString trophyPath,
                           QString gameTrpPath, QString gameName,
                           const QVector<TrophyGameInfo>& allTrophyGames)
    : QMainWindow(), allTrophyGames_(allTrophyGames), currentGameName_(gameName),
      m_gui_settings(std::move(gui_settings)) {
    this->setWindowTitle(tr("Trophy Viewer") + " - " + currentGameName_);
    this->setAttribute(Qt::WA_DeleteOnClose);
    tabWidget = new QTabWidget(this);

    gameTrpPath_ = gameTrpPath;
    std::filesystem::path npbindPath =
        Common::FS::PathFromQString(gameTrpPath) / "sce_sys" / "npbind.dat";

    NPBindFile npbind;
    if (!npbind.Load(npbindPath.string())) {
        LOG_WARNING(Common_Filesystem, "Failed to load npbind.dat file");
    } else {
        npCommIds = npbind.GetNpCommIds();
        if (npCommIds.empty()) {
            LOG_WARNING(Common_Filesystem, "No NPComm IDs found in npbind.dat");
        }
    }

    auto lan = m_gui_settings->GetValue(gui::gen_guiLanguage).toString();
    if (lan == "en_US" || lan == "zh_CN" || lan == "zh_TW" || lan == "ja_JP" || lan == "ko_KR" ||
        lan == "lt_LT" || lan == "nb_NO" || lan == "nl_NL") {
        useEuropeanDateFormat = false;
    }

    headers << "Unlocked"
            << "Trophy"
            << "Name"
            << "Description"
            << "Time Unlocked"
            << "Type"
            << "ID"
            << "Hidden"
            << "PID";

    std::string defaultUsername = UserSettings.GetUserManager().GetDefaultUser().user_name;
    PopulateTrophyWidget(trophyPath, QString::fromStdString(defaultUsername));

    trophyInfoDock = new QDockWidget("", this);
    QWidget* dockWidget = new QWidget(trophyInfoDock);
    QVBoxLayout* dockLayout = new QVBoxLayout(dockWidget);
    dockLayout->setAlignment(Qt::AlignTop);

    // ComboBox for game selection
    if (!allTrophyGames_.isEmpty()) {
        QLabel* userSelectionLabel = new QLabel(tr("Select User:"), dockWidget);
        dockLayout->addWidget(userSelectionLabel);

        userSelectionComboBox = new QComboBox(dockWidget);
        for (const auto& User : UserSettings.GetUserManager().GetAllUsers()) {
            userSelectionComboBox->addItem(QString::fromStdString(User.user_name));
        }

        // Select default user in ComboBox
        int defaultIndex = userSelectionComboBox->findText(QString::fromStdString(defaultUsername));
        if (defaultIndex >= 0) {
            userSelectionComboBox->setCurrentIndex(defaultIndex);
        }

        dockLayout->addWidget(userSelectionComboBox);

        QLabel* gameSelectionLabel = new QLabel(tr("Select Game:"), dockWidget);
        dockLayout->addWidget(gameSelectionLabel);

        gameSelectionComboBox = new QComboBox(dockWidget);
        for (const auto& game : allTrophyGames_) {
            gameSelectionComboBox->addItem(game.name);
        }

        // Select current game in ComboBox
        if (!currentGameName_.isEmpty()) {
            int index = gameSelectionComboBox->findText(currentGameName_);
            if (index >= 0) {
                gameSelectionComboBox->setCurrentIndex(index);
            }
        }

        dockLayout->addWidget(gameSelectionComboBox);

        connect(gameSelectionComboBox, &QComboBox::currentIndexChanged, this, [this]() {
            SelectionChanged(gameSelectionComboBox->currentIndex(),
                             userSelectionComboBox->currentText());
        });

        connect(userSelectionComboBox, &QComboBox::currentIndexChanged, this, [this]() {
            SelectionChanged(gameSelectionComboBox->currentIndex(),
                             userSelectionComboBox->currentText());
        });

        QFrame* line = new QFrame(dockWidget);
        line->setFrameShape(QFrame::HLine);
        line->setFrameShadow(QFrame::Sunken);
        dockLayout->addWidget(line);
    }

    trophyInfoLabel = new QLabel(tr("Progress") + ": 0% (0/0)", dockWidget);
    trophyInfoLabel->setStyleSheet(
        "font-weight: bold; font-size: 16px; color: white; background: #333; padding: 5px;");
    dockLayout->addWidget(trophyInfoLabel);

    // Creates QCheckBox to filter trophies
    showEarnedCheck = new QCheckBox(tr("Show Earned Trophies"), dockWidget);
    showNotEarnedCheck = new QCheckBox(tr("Show Not Earned Trophies"), dockWidget);
    showHiddenCheck = new QCheckBox(tr("Show Hidden Trophies"), dockWidget);

    // Defines the initial states (all checked)
    showEarnedCheck->setChecked(true);
    showNotEarnedCheck->setChecked(true);
    showHiddenCheck->setChecked(false);

    // Adds checkboxes to the layout
    dockLayout->addWidget(showEarnedCheck);
    dockLayout->addWidget(showNotEarnedCheck);
    dockLayout->addWidget(showHiddenCheck);

    dockWidget->setLayout(dockLayout);
    trophyInfoDock->setWidget(dockWidget);

    // Adds the dock to the left area
    this->addDockWidget(Qt::LeftDockWidgetArea, trophyInfoDock);

    expandButton = new QPushButton(">>", this);
    expandButton->setGeometry(80, 0, 27, 27);
    expandButton->hide();

    connect(expandButton, &QPushButton::clicked, this, [this] {
        trophyInfoDock->setVisible(true);
        expandButton->hide();
    });

    // Connects checkbox signals to update trophy display
    connect(showEarnedCheck, &QCheckBox::checkStateChanged, this,
            &TrophyViewer::updateTableFilters);
    connect(showNotEarnedCheck, &QCheckBox::checkStateChanged, this,
            &TrophyViewer::updateTableFilters);
    connect(showHiddenCheck, &QCheckBox::checkStateChanged, this,
            &TrophyViewer::updateTableFilters);

    updateTrophyInfo();
    updateTableFilters();

    connect(trophyInfoDock, &QDockWidget::topLevelChanged, this, [this] {
        if (!trophyInfoDock->isVisible()) {
            expandButton->show();
        }
    });

    connect(trophyInfoDock, &QDockWidget::visibilityChanged, this, [this] {
        if (!trophyInfoDock->isVisible()) {
            expandButton->show();
        } else {
            expandButton->hide();
        }
    });
}

void TrophyViewer::SelectionChanged(int gameIndex, QString user) {
    if (gameIndex < 0 || gameIndex >= allTrophyGames_.size()) {
        return;
    }

    while (tabWidget->count() > 0) {
        QWidget* widget = tabWidget->widget(0);
        tabWidget->removeTab(0);
        delete widget;
    }

    const TrophyGameInfo& selectedGame = allTrophyGames_[gameIndex];
    currentGameName_ = selectedGame.name;
    gameTrpPath_ = selectedGame.gameTrpPath;

    this->setWindowTitle(tr("Trophy Viewer") + " - " + currentGameName_);

    std::filesystem::path npbindPath =
        Common::FS::PathFromQString(gameTrpPath_) / "sce_sys" / "npbind.dat";

    NPBindFile npbind;
    if (!npbind.Load(npbindPath.string())) {
        LOG_WARNING(Common_Filesystem, "Failed to load npbind.dat file");
    } else {
        npCommIds = npbind.GetNpCommIds();
        if (npCommIds.empty()) {
            LOG_WARNING(Common_Filesystem, "No NPComm IDs found in npbind.dat");
        }
    }

    PopulateTrophyWidget(selectedGame.trophyPath, user);

    updateTrophyInfo();
    updateTableFilters();
}

void TrophyViewer::onDockClosed() {
    if (!trophyInfoDock->isVisible()) {
        reopenButton->setVisible(true);
    }
}

void TrophyViewer::reopenLeftDock() {
    trophyInfoDock->show();
    reopenButton->setVisible(false);
}

void TrophyViewer::PopulateTrophyWidget(QString title, QString user) {

    // only use first npCommID for now
    std::string npCommId = npCommIds[0];

    auto trophyFilesPath =
        Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "trophy" / npCommId;
    QString trophyDirQt;
    Common::FS::PathToQString(trophyDirQt, trophyFilesPath);

    QDir dir(trophyDirQt);
    if (!dir.exists()) {
        std::filesystem::path path = Common::FS::PathFromQString(gameTrpPath_);
        if (!trp.Extract(path, 0, npCommId, trophyFilesPath)) {
            QMessageBox::critical(this, "Trophy Data Extraction Error",
                                  "Unable to extract Trophy data, please ensure you have "
                                  "inputted a trophy key in the settings menu.");
            QWidget::close();
            return;
        }
    }

    const std::string filename = npCommId + ".xml";
    std::string userId = "1000";

    for (const auto& User : UserSettings.GetUserManager().GetAllUsers()) {
        if (User.user_name == user) {
            userId = std::to_string(User.user_id);
        }
    }

    auto user_trophy_file = EmulatorSettings.GetHomeDir() / userId / "trophy" / filename;
    if (!std::filesystem::exists(user_trophy_file)) {
        std::error_code discard;
        std::filesystem::copy_file(trophyFilesPath / "Xml" / "TROPCONF.XML", user_trophy_file,
                                   discard);
    }

    QFileInfoList dirList = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    if (dirList.isEmpty())
        return;

    // Clears previous tabs (if any)
    while (tabWidget->count() > 0) {
        QWidget* widget = tabWidget->widget(0);
        tabWidget->removeTab(0);
        delete widget;
    }

    QString tabName = QString(tr("%1 trophies for %2").arg(currentGameName_).arg(user));
    QString trpDir = trophyDirQt;

    QString iconsPath = trpDir + "/Icons";
    QDir iconsDir(iconsPath);
    QFileInfoList iconDirList = iconsDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    std::vector<QImage> icons;

    for (const QFileInfo& iconInfo : iconDirList) {
        QString fileName = iconInfo.fileName();

        // Skip files that doesn't start with "trop" or "TROP"
        if (!fileName.startsWith("trop", Qt::CaseInsensitive))
            continue;

        QImage icon = QImage(iconInfo.absoluteFilePath())
                          .scaled(QSize(128, 128), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        icons.push_back(icon);
    }

    QStringList trpId;
    QStringList trpHidden;
    QStringList trpUnlocked;
    QStringList trpType;
    QStringList trpPid;
    QStringList trophyNames;
    QStringList trophyDetails;
    QStringList trpTimeUnlocked;

    QString userXmlPath;
    Common::FS::PathToQString(userXmlPath, user_trophy_file);
    QFile userFile(userXmlPath);
    if (!userFile.open(QFile::ReadOnly | QFile::Text)) {
        return;
    }

    QXmlStreamReader usrFileReader(&userFile);
    while (!usrFileReader.atEnd() && !usrFileReader.hasError()) {
        usrFileReader.readNext();
        if (usrFileReader.isStartElement() && usrFileReader.name().toString() == "trophy") {
            trpId.append(usrFileReader.attributes().value("id").toString());
            trpHidden.append(usrFileReader.attributes().value("hidden").toString());
            trpType.append(usrFileReader.attributes().value("ttype").toString());
            trpPid.append(usrFileReader.attributes().value("pid").toString());

            if (usrFileReader.attributes().hasAttribute("unlockstate")) {
                if (usrFileReader.attributes().value("unlockstate").toString() == "true") {
                    trpUnlocked.append("unlocked");
                } else {
                    trpUnlocked.append("locked");
                }
                if (usrFileReader.attributes().hasAttribute("timestamp")) {
                    QString ts = usrFileReader.attributes().value("timestamp").toString();
                    if (ts.length() > 10)
                        trpTimeUnlocked.append("unknown");
                    else {
                        bool ok;
                        qint64 timestampInt = ts.toLongLong(&ok);
                        if (ok) {
                            QDateTime dt = QDateTime::fromSecsSinceEpoch(timestampInt);
                            QString format = useEuropeanDateFormat ? "dd/MM/yyyy HH:mm:ss"
                                                                   : "MM/dd/yyyy HH:mm:ss";
                            trpTimeUnlocked.append(dt.toString(format));
                        } else {
                            trpTimeUnlocked.append("unknown");
                        }
                    }
                } else {
                    trpTimeUnlocked.append("");
                }
            } else {
                trpUnlocked.append("locked");
                trpTimeUnlocked.append("");
            }
        }
    }

    std::filesystem::path path = GetTrophyXmlPath(
        Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "trophy" / npCommId / "Xml",
        EmulatorSettings.GetConsoleLanguage());
    QString xmlPath;
    Common::FS::PathToQString(xmlPath, path);

    QFile file(xmlPath);
    if (!file.open(QFile::ReadOnly | QFile::Text)) {
        QMessageBox::information(this, "test", "did not open");
        return;
    }

    QXmlStreamReader reader(&file);
    while (!reader.atEnd() && !reader.hasError()) {
        reader.readNext();
        if (reader.isStartElement() && reader.name().toString() == "trophy") {
            while (reader.readNextStartElement()) {
                if (reader.name().toString() == "name" && !trpId.isEmpty()) {
                    trophyNames.append(reader.readElementText());
                }
                if (reader.name().toString() == "detail" && !trpId.isEmpty()) {
                    trophyDetails.append(reader.readElementText());
                }
            }
        }
    }

    QTableWidget* tableWidget = new QTableWidget(this);
    tableWidget->setShowGrid(false);
    tableWidget->setColumnCount(9);
    tableWidget->setHorizontalHeaderLabels(headers);
    tableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    tableWidget->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    tableWidget->horizontalHeader()->setStretchLastSection(false);
    tableWidget->verticalHeader()->setVisible(false);
    tableWidget->setRowCount(static_cast<int>(icons.size()));
    tableWidget->setSortingEnabled(true);
    tableWidget->setWordWrap(true);

    for (int row = 0; auto& icon : icons) {
        QTableWidgetItem* item = new QTableWidgetItem();
        item->setData(Qt::DecorationRole, icon);
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        tableWidget->setItem(row, 1, item);

        const std::string filename = GetTrpType(trpType[row].at(0));
        QTableWidgetItem* typeitem = new QTableWidgetItem();

        const auto CustomTrophy_Dir = Common::FS::GetUserPath(Common::FS::PathType::CustomTrophy);
        std::string customPath;

        if (fs::exists(CustomTrophy_Dir / filename)) {
            customPath = (CustomTrophy_Dir / filename).string();
        }

        std::vector<char> imgdata;

        if (!customPath.empty()) {
            std::ifstream file(customPath, std::ios::binary);
            if (file) {
                imgdata = std::vector<char>(std::istreambuf_iterator<char>(file),
                                            std::istreambuf_iterator<char>());
            }
        } else {
            auto resource = cmrc::res::get_filesystem();
            std::string resourceString = "src/images/" + filename;
            auto file = resource.open(resourceString);
            imgdata = std::vector<char>(file.begin(), file.end());
        }

        QImage type_icon = QImage::fromData(imgdata).scaled(QSize(100, 100), Qt::KeepAspectRatio,
                                                            Qt::SmoothTransformation);
        typeitem->setData(Qt::DecorationRole, type_icon);
        typeitem->setFlags(typeitem->flags() & ~Qt::ItemIsEditable);
        tableWidget->setItem(row, 5, typeitem);

        std::string detailString = trophyDetails[row].toStdString();
        std::size_t newline_pos = 0;
        while ((newline_pos = detailString.find("\n", newline_pos)) != std::string::npos) {
            detailString.replace(newline_pos, 1, " ");
            ++newline_pos;
        }

        if (!trophyNames.isEmpty() && !trophyDetails.isEmpty()) {
            SetTableItem(tableWidget, row, 0, trpUnlocked[row]);
            SetTableItem(tableWidget, row, 2, trophyNames[row]);
            SetTableItem(tableWidget, row, 3, QString::fromStdString(detailString));
            SetTableItem(tableWidget, row, 4, trpTimeUnlocked[row]);
            SetTableItem(tableWidget, row, 6, trpId[row]);
            SetTableItem(tableWidget, row, 7, trpHidden[row]);
            SetTableItem(tableWidget, row, 8, trpPid[row]);
        }

        tableWidget->verticalHeader()->resizeSection(row, icon.height());
        row++;
    }

    auto header = tableWidget->horizontalHeader();
    header->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(3, QHeaderView::Stretch);
    header->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(6, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(7, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(8, QHeaderView::ResizeToContents);

    tableWidget->resizeColumnsToContents();
    tableWidget->resizeRowsToContents();

    const int hardMinDesc = 300;
    int currentDesc = tableWidget->columnWidth(3);
    if (currentDesc < hardMinDesc) {
        tableWidget->setColumnWidth(3, hardMinDesc);
    }

    tabWidget->addTab(tableWidget, tabName);

    this->setCentralWidget(tabWidget);

    if (!this->isMaximized() && !this->isFullScreen()) {
        if (!userResizedWindow_ && !initialSizeApplied_) {
            QScreen* screen = QGuiApplication::primaryScreen();
            QSize screenSize(1024, 768);
            if (screen) {
                screenSize = screen->availableGeometry().size();
            }
            programmaticResize_ = true;
            this->resize(screenSize.width() * 0.8, screenSize.height() * 0.8);
            programmaticResize_ = false;
            initialSizeApplied_ = true;
        }
    }
}

void TrophyViewer::SetTableItem(QTableWidget* parent, int row, int column, QString str) {
    QTableWidgetItem* item = new QTableWidgetItem(str);

    if (column != 1 && column != 2 && column != 3)
        item->setTextAlignment(Qt::AlignCenter);
    QFont f = parent->font();
    f.setPointSize(12);
    f.setBold(true);
    item->setFont(f);

    Theme theme = static_cast<Theme>(m_gui_settings->GetValue(gui::gen_theme).toInt());

    if (theme == Theme::Light) {
        item->setForeground(QBrush(Qt::black));
    } else {
        item->setForeground(QBrush(Qt::white));
    }

    parent->setItem(row, column, item);
}
