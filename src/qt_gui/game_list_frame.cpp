// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "game_list_frame.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <vector>

#include <QCollator>
#include <QDesktopServices>
#include <QFile>
#include <QGraphicsDropShadowEffect>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPointer>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QToolTip>
#include <QVBoxLayout>

#include "common/logging/log.h"
#include "common/path_util.h"
#include "main_window.h"

namespace {

constexpr int GameIndexRole = Qt::UserRole + 1;

enum class SortValueType {
    Text,
    Integer,
    Number,
};

double ParseSizeMB(const std::string& size) {
    if (size.empty()) {
        return 0.0;
    }

    QString value = QString::fromStdString(size).trimmed();

    if (value.isEmpty()) {
        return 0.0;
    }

    QString numericPart = value;
    double multiplier = 1.0;

    if (value.endsWith("GB", Qt::CaseInsensitive)) {
        numericPart = value.left(value.size() - 2).trimmed();
        multiplier = 1024.0;
    } else if (value.endsWith("MB", Qt::CaseInsensitive)) {
        numericPart = value.left(value.size() - 2).trimmed();
    } else if (value.endsWith("G", Qt::CaseInsensitive)) {
        numericPart = value.left(value.size() - 1).trimmed();
        multiplier = 1024.0;
    } else if (value.endsWith("M", Qt::CaseInsensitive)) {
        numericPart = value.left(value.size() - 1).trimmed();
    }

    bool ok = false;
    const double number = numericPart.toDouble(&ok);

    return ok ? number * multiplier : 0.0;
}

static int ParsePlayTime(const std::string& time) {
    int hours = 0;
    int minutes = 0;
    int seconds = 0;

    if (sscanf(time.c_str(), "%d:%d:%d", &hours, &minutes, &seconds) != 3) {
        return 0;
    }

    return hours * 3600 + minutes * 60 + seconds;
}

QString MakeSortText(const QString& text) {
    return text.trimmed().toCaseFolded();
}

bool IsNumericFirmware(const std::string& firmware) {
    const QString value = QString::fromStdString(firmware).trimmed();
    if (value.isEmpty())
        return false;

    bool ok = false;
    value.toDouble(&ok);
    return ok;
}

double ParseFirmware(const std::string& firmware) {
    const QString value = QString::fromStdString(firmware).trimmed();

    bool ok = false;
    const double result = value.toDouble(&ok);

    return ok ? result : 0.0;
}

class SortableGameItem final : public QTableWidgetItem {
public:
    explicit SortableGameItem(int gameIndex = -1) : QTableWidgetItem(), m_game_index(gameIndex) {
        setData(GameIndexRole, gameIndex);
    }

    SortableGameItem(const SortableGameItem&) = default;

    QTableWidgetItem* clone() const override {
        return new SortableGameItem(*this);
    }

    void SetTextKey(QString value) {
        m_type = SortValueType::Text;
        m_text_key = std::move(value);
    }

    void SetIntegerKey(qint64 value) {
        m_type = SortValueType::Integer;
        m_integer_key = value;
    }

    void SetNumberKey(double value) {
        m_type = SortValueType::Number;
        m_number_key = value;
    }

    void SetFavorite(bool favorite) {
        m_favorite = favorite;
    }

    int GameIndex() const {
        return m_game_index;
    }

private:
    SortValueType m_type = SortValueType::Text;

    QString m_text_key;
    qint64 m_integer_key = 0;
    double m_number_key = 0.0;

    int m_game_index = -1;
    bool m_favorite = false;
};

} // namespace

GameListFrame::GameListFrame(std::shared_ptr<gui_settings> gui_settings,
                             std::shared_ptr<GameInfoClass> game_info_get,
                             std::shared_ptr<CompatibilityInfoClass> compat_info_get,
                             std::shared_ptr<IpcClient> ipc_client, QWidget* parent)
    : QTableWidget(parent), m_gui_settings(std::move(gui_settings)),
      m_game_info(std::move(game_info_get)), m_compat_info(std::move(compat_info_get)),
      m_ipc_client(std::move(ipc_client)) {

    icon_size = m_gui_settings->GetValue(gui::gl_icon_size).toInt();
    last_favorite = "";

    setShowGrid(false);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::SingleSelection);

    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);

    verticalScrollBar()->installEventFilter(this);

    verticalScrollBar()->setSingleStep(20);
    horizontalScrollBar()->setSingleStep(20);

    verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    verticalHeader()->setVisible(false);

    horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
    horizontalHeader()->setHighlightSections(false);
    horizontalHeader()->setSortIndicatorShown(true);
    horizontalHeader()->setSectionsClickable(true);

    setContextMenuPolicy(Qt::CustomContextMenu);

    setColumnCount(12);

    setColumnWidth(1, 300);
    setColumnWidth(2, 140);
    setColumnWidth(3, 120);
    setColumnWidth(4, 90);
    setColumnWidth(5, 90);
    setColumnWidth(6, 90);
    setColumnWidth(7, 90);
    setColumnWidth(8, 120);
    setColumnWidth(10, 90);
    setColumnWidth(11, 0);

    QStringList headers;

    headers << tr("Icon") << tr("Name") << tr("Compatibility") << tr("Serial") << tr("Region")
            << tr("Firmware") << tr("Size") << tr("Version") << tr("Play Time") << tr("Path")
            << tr("Favorite") << "";

    setHorizontalHeaderLabels(headers);

    horizontalHeader()->setSortIndicatorShown(true);

    horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    horizontalHeader()->setSectionResizeMode(9, QHeaderView::Stretch);
    horizontalHeader()->setSectionResizeMode(10, QHeaderView::Fixed);
    horizontalHeader()->setSectionResizeMode(11, QHeaderView::Stretch);

    setColumnHidden(11, true);

    connect(this, &QTableWidget::currentCellChanged, this, &GameListFrame::onCurrentCellChanged);

    connect(verticalScrollBar(), &QScrollBar::valueChanged, this,
            &GameListFrame::RefreshListBackgroundImage);

    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this,
            &GameListFrame::RefreshListBackgroundImage);

    connect(horizontalHeader(), &QHeaderView::sectionClicked, this, [this](int columnIndex) {
        if (columnIndex <= 0 || columnIndex == 11) {
            return;
        }

        if (sortColumn == columnIndex) {
            ListSortedAsc = !ListSortedAsc;
        } else {
            sortColumn = columnIndex;
            ListSortedAsc = true;
        }

        SortTable(sortColumn, ListSortedAsc);
    });

    connect(this, &QTableWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        int itemID = -1;

        if (currentItem()) {
            itemID = GetGameIndexForRow(currentItem()->row());
        }

        const int changedFavorite = m_gui_context_menus.RequestGameMenu(
            pos, m_game_info->m_games, m_compat_info, m_gui_settings, m_ipc_client, this, true,
            itemID,
            [mw = QPointer<MainWindow>(qobject_cast<MainWindow*>(window()))](
                const QStringList& args) {
                if (mw) {
                    mw->StartGameWithArgs(args);
                }
            });

        if (changedFavorite) {
            const int selectedGameIndex = itemID;

            UpdateFavoriteCache();
            UpdateFavoriteStateForAllItems();

            for (int row = 0; row < rowCount(); ++row) {
                if (GetGameIndexForRow(row) == selectedGameIndex) {
                    UpdateFavoriteVisual(row);
                    break;
                }
            }

            SortTable(sortColumn, ListSortedAsc);

            for (int newRow = 0; newRow < rowCount(); ++newRow) {
                if (GetGameIndexForRow(newRow) == selectedGameIndex) {
                    m_current_game_index = selectedGameIndex;
                    m_current_column = currentColumn();

                    setCurrentCell(newRow, m_current_column);
                    scrollTo(currentIndex(), QAbstractItemView::PositionAtTop);

                    break;
                }
            }

            viewport()->update();
        }
    });

    connect(this, &QTableWidget::cellClicked, this, [this](int row, int column) {
        const int gameIndex = GetGameIndexForRow(row);

        if (gameIndex < 0 || gameIndex >= static_cast<int>(m_game_info->m_games.size())) {
            return;
        }

        auto& game = m_game_info->m_games[gameIndex];

        if (column == 2 && game.compatibility.issue_number != "") {
            const auto url_issues = "https://github.com/shadps4-compatibility/"
                                    "shadps4-game-compatibility/issues/";

            QDesktopServices::openUrl(QUrl(url_issues + game.compatibility.issue_number));

        } else if (column == 10) {
            const int selectedGameIndex = gameIndex;

            const QString serialStr = QString::fromStdString(game.serial);

            QList<QString> favorites =
                gui_settings::Var2List(m_gui_settings->GetValue(gui::favorites_list));

            if (favorites.contains(serialStr)) {
                favorites.removeOne(serialStr);
            } else {
                favorites.append(serialStr);
            }

            m_gui_settings->SetValue(gui::favorites_list, gui_settings::List2Var(favorites));

            UpdateFavoriteCache();
            UpdateFavoriteStateForAllItems();
            UpdateFavoriteVisual(row);

            SortTable(sortColumn, ListSortedAsc);

            for (int newRow = 0; newRow < rowCount(); ++newRow) {
                if (GetGameIndexForRow(newRow) == selectedGameIndex) {
                    m_current_game_index = selectedGameIndex;
                    m_current_column = 10;
                    setCurrentCell(newRow, 10);
                    break;
                }
            }

            viewport()->update();
        }
    });

    connect(horizontalHeader(), &QHeaderView::customContextMenuRequested, this,
            &GameListFrame::ShowHeaderContextMenu);

    connect(&m_gui_context_menus, &GuiContextMenus::RequestGameListRefresh, this,
            &GameListFrame::RequestRefreshList);

    ToggleColumnVisibility();

    PopulateGameList();
}

void GameListFrame::onCurrentCellChanged(int currentRow, int currentColumn, int previousRow,
                                         int previousColumn) {
    Q_UNUSED(previousRow);
    Q_UNUSED(previousColumn);

    if (currentRow < 0 || currentColumn < 0) {
        m_current_game_index = -1;
        m_current_column = -1;
        return;
    }

    QTableWidgetItem* current = item(currentRow, currentColumn);

    if (!current) {
        return;
    }

    m_current_game_index = GetGameIndexForRow(currentRow);
    m_current_column = currentColumn;

    SetListBackgroundImage(current);
    PlayBackgroundMusic(current);
}

void GameListFrame::PlayBackgroundMusic(QTableWidgetItem* item) {
    if (!item || !m_gui_settings->GetValue(gui::gl_playBackgroundMusic).toBool() ||
        EmulatorState::GetInstance()->IsGameRunning()) {

        BackgroundMusicPlayer::getInstance().stopMusic();
        return;
    }

    const int gameIndex = GetGameIndexForRow(item->row());

    if (gameIndex < 0 || gameIndex >= static_cast<int>(m_game_info->m_games.size())) {

        BackgroundMusicPlayer::getInstance().stopMusic();
        return;
    }

    QString snd0path;

    Common::FS::PathToQString(snd0path, m_game_info->m_games[gameIndex].snd0_path);

    BackgroundMusicPlayer::getInstance().playMusic(snd0path);
}

int GameListFrame::GetGameIndexForRow(int row) const {
    if (row < 0 || row >= rowCount()) {
        return -1;
    }

    if (const auto* item = this->item(row, 1)) {
        const QVariant value = item->data(GameIndexRole);

        if (value.isValid()) {
            return value.toInt();
        }
    }

    for (int column = 0; column < columnCount(); ++column) {
        if (const auto* item = this->item(row, column)) {
            const QVariant value = item->data(GameIndexRole);

            if (value.isValid()) {
                return value.toInt();
            }
        }
    }

    return -1;
}

void GameListFrame::UpdateFavoriteCache() {
    m_favorite_serials.clear();

    const QList<QString> favorites =
        gui_settings::Var2List(m_gui_settings->GetValue(gui::favorites_list));

    for (const QString& serial : favorites) {
        m_favorite_serials.insert(serial);
    }
}

void GameListFrame::LoadPlayTimeCache() {
    m_play_time_cache.clear();

    const auto user_dir = Common::FS::GetUserPath(Common::FS::PathType::UserDir);

    const QString filePath = QString::fromStdString((user_dir / "play_time.txt").string());

    QFile file(filePath);

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();

        if (line.isEmpty()) {
            continue;
        }

        const QStringList parts = line.split(' ', Qt::SkipEmptyParts);

        if (parts.size() >= 2) {
            m_play_time_cache.insert(parts[0], parts[1]);
        }
    }
}

void GameListFrame::PopulateGameList(bool isInitialPopulation) {
    const QSignalBlocker blocker(this);

    m_current_game_index = -1;
    m_current_column = -1;

    setUpdatesEnabled(false);

    clearContents();

    UpdateFavoriteCache();
    LoadPlayTimeCache();

    const int gameCount = static_cast<int>(m_game_info->m_games.size());

    setRowCount(gameCount);

    for (int i = 0; i < gameCount; ++i) {
        auto& game = m_game_info->m_games[i];

        SetTableItem(i, 1, QString::fromStdString(game.name));

        if (std::filesystem::exists(Common::FS::GetUserPath(Common::FS::PathType::CustomConfigs) /
                                    (game.serial + ".json"))) {

            if (QTableWidgetItem* nameItem = item(i, 1)) {
                nameItem->setIcon(QIcon(":images/game_settings.png"));
            }
        }

        SetTableItem(i, 3, QString::fromStdString(game.serial));

        SetRegionFlag(i, 4, QString::fromStdString(game.region));

        SetTableItem(i, 5, QString::fromStdString(game.fw));

        SetTableItem(i, 6, QString::fromStdString(game.size));

        SetTableItem(i, 7, QString::fromStdString(game.version));

        SetFavoriteIcon(i, 10);

        game.compatibility = m_compat_info->GetCompatibilityInfo(game.serial);

        SetCompatibilityItem(i, 2, game.compatibility, i);

        const QString playTime = GetPlayTime(game.serial);

        if (playTime.isEmpty()) {
            game.play_time = "0:00:00";
            SetTableItem(i, 8, tr("Never Played"));
        } else {
            const QStringList timeParts = playTime.split(':');

            int hours = 0;
            int minutes = 0;
            int seconds = 0;

            if (timeParts.size() >= 3) {
                hours = timeParts[0].toInt();
                minutes = timeParts[1].toInt();
                seconds = timeParts[2].toInt();
            }

            QString formattedPlayTime;

            if (hours > 0) {
                formattedPlayTime += QString("%1").arg(hours) + tr("h");
            }

            if (minutes > 0) {
                formattedPlayTime += QString("%1").arg(minutes) + tr("m");
            }

            formattedPlayTime = formattedPlayTime.trimmed();

            game.play_time = playTime.toStdString();

            if (formattedPlayTime.isEmpty()) {
                SetTableItem(i, 8, QString("%1").arg(seconds) + tr("s"));
            } else {
                SetTableItem(i, 8, formattedPlayTime);
            }
        }

        QString path;

        Common::FS::PathToQString(path, game.path);

        SetTableItem(i, 9, path);
    }

    ResizeIcons(icon_size);

    horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

    ApplyLastSorting(isInitialPopulation);

    if (!isInitialPopulation && !last_favorite.empty()) {

        for (int row = 0; row < rowCount(); ++row) {
            const int gameIndex = GetGameIndexForRow(row);

            if (gameIndex < 0 || gameIndex >= static_cast<int>(m_game_info->m_games.size())) {
                continue;
            }

            if (m_game_info->m_games[gameIndex].serial == last_favorite) {

                setCurrentCell(row, 10);

                m_current_game_index = gameIndex;
                m_current_column = 10;

                break;
            }
        }
    }

    setUpdatesEnabled(true);

    viewport()->update();
}

void GameListFrame::SetListBackgroundImage(QTableWidgetItem* item) {
    if (!item) {
        // handle case where no item was clicked
        return;
    }

    // If background images are hidden, clear the background image
    if (!m_gui_settings->GetValue(gui::gl_showBackgroundImage).toBool()) {
        backgroundImage = QImage();
        m_last_opacity = -1;         // Reset opacity tracking when disabled
        m_current_game_path.clear(); // Reset current game path
        RefreshListBackgroundImage();
        return;
    }

    const int gameIndex = GetGameIndexForRow(item->row());

    if (gameIndex < 0 || gameIndex >= static_cast<int>(m_game_info->m_games.size())) {
        return;
    }

    const auto& game = m_game_info->m_games[gameIndex];

    const int opacity = m_gui_settings->GetValue(gui::gl_backgroundImageOpacity).toInt();

    // Recompute if opacity changed or we switched to a different game
    if (opacity != m_last_opacity || game.pic_path != m_current_game_path) {
        const auto image_path = game.pic_path.u8string();
        QImage original_image(
            QString::fromStdString(std::string(image_path.begin(), image_path.end())));
        if (!original_image.isNull()) {
            backgroundImage = m_game_list_utils.ChangeImageOpacity(
                original_image, original_image.rect(), opacity / 100.0f);
            m_last_opacity = opacity;
            m_current_game_path = game.pic_path;
        }
    }

    RefreshListBackgroundImage();
}

void GameListFrame::RefreshListBackgroundImage() {
    QPalette palette;
    if (!backgroundImage.isNull() &&
        m_gui_settings->GetValue(gui::gl_showBackgroundImage).toBool()) {
        const QSize widgetSize = size();
        const QPixmap scaledPixmap =
            QPixmap::fromImage(backgroundImage)
                .scaled(widgetSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        const int x = (widgetSize.width() - scaledPixmap.width()) / 2;
        const int y = (widgetSize.height() - scaledPixmap.height()) / 2;
        QPixmap finalPixmap(widgetSize);
        finalPixmap.fill(Qt::transparent);
        QPainter painter(&finalPixmap);
        painter.drawPixmap(x, y, scaledPixmap);
        palette.setBrush(QPalette::Base, QBrush(finalPixmap));
    }
    const QColor transparentColor = QColor(135, 206, 235, 40);
    palette.setColor(QPalette::Highlight, transparentColor);
    setPalette(palette);
}

void GameListFrame::resizeEvent(QResizeEvent* event) {
    QTableWidget::resizeEvent(event);
    RefreshListBackgroundImage();
}

static int CompareGames(const GameInfo& a, const GameInfo& b, int columnIndex) {
    switch (columnIndex) {
    case 1: {
        static const QCollator collator = [] {
            QCollator c;
            c.setCaseSensitivity(Qt::CaseInsensitive);
            c.setNumericMode(true);
            return c;
        }();

        return collator.compare(QString::fromStdString(a.name), QString::fromStdString(b.name));
    }
    case 2: {
        const int statusA = static_cast<int>(a.compatibility.status);
        const int statusB = static_cast<int>(b.compatibility.status);
        if (statusA < statusB)
            return -1;
        if (statusA > statusB)
            return 1;
        return 0;
    }
    case 3: {
        const std::string serialA = a.serial.size() > 4 ? a.serial.substr(4) : a.serial;
        const std::string serialB = b.serial.size() > 4 ? b.serial.substr(4) : b.serial;
        const QString qA = QString::fromStdString(serialA);
        const QString qB = QString::fromStdString(serialB);
        static const QCollator collator = [] {
            QCollator c;
            c.setCaseSensitivity(Qt::CaseInsensitive);
            c.setNumericMode(true);
            return c;
        }();
        return collator.compare(qA, qB);
    }
    case 4: {
        const auto GetRegionOrder = [](const QString& region) -> int {
            if (region == "World")
                return 0;
            if (region == "Asia")
                return 1;
            if (region == "USA")
                return 2;
            if (region == "Europe")
                return 3;
            if (region == "Japan")
                return 4;
            return 5; // Unknown sempre por último
        };
        const QString regionA = QString::fromStdString(a.region).trimmed();
        const QString regionB = QString::fromStdString(b.region).trimmed();
        const int orderA = GetRegionOrder(regionA);
        const int orderB = GetRegionOrder(regionB);
        if (orderA < orderB)
            return -1;
        if (orderA > orderB)
            return 1;
        return 0;
    }
    case 5: {
        const QString fwA = QString::fromStdString(a.fw).trimmed();
        const QString fwB = QString::fromStdString(b.fw).trimmed();
        bool okA = false;
        bool okB = false;
        const double valueA = fwA.toDouble(&okA);
        const double valueB = fwB.toDouble(&okB);

        if (okA != okB) {
            return okA ? -1 : 1;
        }

        if (okA && okB) {
            if (valueA < valueB)
                return -1;

            if (valueA > valueB)
                return 1;

            return 0;
        }
        static const QCollator collator = [] {
            QCollator c;
            c.setCaseSensitivity(Qt::CaseInsensitive);
            c.setNumericMode(true);
            return c;
        }();
        return collator.compare(fwA, fwB);
    }
    case 6: {
        const double sizeA = ParseSizeMB(a.size);
        const double sizeB = ParseSizeMB(b.size);
        if (sizeA < sizeB)
            return -1;
        if (sizeA > sizeB)
            return 1;
        return 0;
    }
    case 7: {
        static const QCollator collator = [] {
            QCollator c;
            c.setCaseSensitivity(Qt::CaseInsensitive);
            c.setNumericMode(true);
            return c;
        }();
        return collator.compare(QString::fromStdString(a.version),
                                QString::fromStdString(b.version));
    }
    case 8: {
        const int timeA = ParsePlayTime(a.play_time);
        const int timeB = ParsePlayTime(b.play_time);
        if (timeA < timeB)
            return -1;
        if (timeA > timeB)
            return 1;
        return 0;
    }
    case 9: {
        const QString pathA = QString::fromStdString(a.path.string()).toCaseFolded();
        const QString pathB = QString::fromStdString(b.path.string()).toCaseFolded();
        static const QCollator collator = [] {
            QCollator c;
            c.setCaseSensitivity(Qt::CaseInsensitive);
            c.setNumericMode(true);
            return c;
        }();
        return collator.compare(pathA, pathB);
    }
    default:
        return 0;
    }
}

void GameListFrame::SortTable(int columnIndex, bool ascending) {
    if (columnIndex <= 0 || columnIndex == 11 || m_game_info->m_games.size() <= 1) {
        return;
    }
    const int selectedGameIndex = m_current_game_index;
    const int selectedColumn = m_current_column >= 0 ? m_current_column : 1;
    QHash<int, bool> hiddenByGame;
    for (int row = 0; row < rowCount(); ++row) {
        const int gameIndex = GetGameIndexForRow(row);
        if (gameIndex >= 0) {
            hiddenByGame.insert(gameIndex, isRowHidden(row));
        }
    }
    std::vector<int> order;
    order.reserve(m_game_info->m_games.size());
    for (int i = 0; i < static_cast<int>(m_game_info->m_games.size()); ++i) {
        order.push_back(i);
    }

    std::stable_sort(order.begin(), order.end(), [&](int lhsIndex, int rhsIndex) {
        const GameInfo& lhs = m_game_info->m_games[lhsIndex];
        const GameInfo& rhs = m_game_info->m_games[rhsIndex];
        const bool lhsFavorite = m_favorite_serials.contains(QString::fromStdString(lhs.serial));
        const bool rhsFavorite = m_favorite_serials.contains(QString::fromStdString(rhs.serial));

        if (lhsFavorite != rhsFavorite) {
            return lhsFavorite > rhsFavorite;
        }
        const int comparison = CompareGames(lhs, rhs, columnIndex);
        if (comparison != 0) {
            if (ascending) {
                return comparison < 0;
            }
            return comparison > 0;
        }
        return lhsIndex < rhsIndex;
    });

    const QSignalBlocker blocker(this);
    setUpdatesEnabled(false);
    clearContents();
    setRowCount(static_cast<int>(order.size()));

    for (int row = 0; row < static_cast<int>(order.size()); ++row) {
        const int gameIndex = order[row];
        if (gameIndex < 0 || gameIndex >= static_cast<int>(m_game_info->m_games.size())) {
            continue;
        }
        auto& game = m_game_info->m_games[gameIndex];
        {
            auto* iconItem = new SortableGameItem(gameIndex);
            iconItem->SetFavorite(m_favorite_serials.contains(QString::fromStdString(game.serial)));
            const QImage scaledPixmap = game.icon.scaled(
                QSize(icon_size, icon_size), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            iconItem->setData(Qt::DecorationRole, scaledPixmap);
            iconItem->setData(GameIndexRole, gameIndex);
            setItem(row, 0, iconItem);
            verticalHeader()->resizeSection(row, scaledPixmap.height());
        }
        SetTableItem(row, 1, QString::fromStdString(game.name));
        if (std::filesystem::exists(Common::FS::GetUserPath(Common::FS::PathType::CustomConfigs) /
                                    (game.serial + ".json"))) {
            if (QTableWidgetItem* nameItem = item(row, 1)) {
                nameItem->setIcon(QIcon(":images/game_settings.png"));
            }
        }
        SetTableItem(row, 3, QString::fromStdString(game.serial));
        SetRegionFlag(row, 4, QString::fromStdString(game.region));
        SetTableItem(row, 5, QString::fromStdString(game.fw));
        SetTableItem(row, 6, QString::fromStdString(game.size));
        SetTableItem(row, 7, QString::fromStdString(game.version));
        SetFavoriteIcon(row, 10);
        SetCompatibilityItem(row, 2, game.compatibility, gameIndex);

        const QString playTime = GetPlayTime(game.serial);
        if (playTime.isEmpty()) {
            game.play_time = "0:00:00";
            SetTableItem(row, 8, tr("Never Played"));
        } else {
            const QStringList timeParts = playTime.split(':');
            int hours = 0;
            int minutes = 0;
            int seconds = 0;
            if (timeParts.size() >= 3) {
                hours = timeParts[0].toInt();
                minutes = timeParts[1].toInt();
                seconds = timeParts[2].toInt();
            }
            QString formattedPlayTime;
            if (hours > 0) {
                formattedPlayTime += QString("%1h").arg(hours);
            }
            if (minutes > 0) {
                formattedPlayTime += QString("%1m").arg(minutes);
            }
            formattedPlayTime = formattedPlayTime.trimmed();
            game.play_time = playTime.toStdString();
            if (formattedPlayTime.isEmpty()) {
                SetTableItem(row, 8, QString("%1s").arg(seconds));
            } else {
                SetTableItem(row, 8, formattedPlayTime);
            }
        }
        QString path;
        Common::FS::PathToQString(path, game.path);
        SetTableItem(row, 9, path);
        for (int column = 0; column < columnCount(); ++column) {
            if (auto* tableItem = dynamic_cast<SortableGameItem*>(item(row, column))) {
                tableItem->setData(GameIndexRole, gameIndex);
                tableItem->SetFavorite(
                    m_favorite_serials.contains(QString::fromStdString(game.serial)));
            }
        }
    }
    for (int row = 0; row < rowCount(); ++row) {
        const int gameIndex = GetGameIndexForRow(row);
        if (gameIndex >= 0) {
            setRowHidden(row, hiddenByGame.value(gameIndex, false));
        }
    }
    horizontalHeader()->setSortIndicator(columnIndex,
                                         ascending ? Qt::AscendingOrder : Qt::DescendingOrder);
    if (selectedGameIndex >= 0) {
        for (int row = 0; row < rowCount(); ++row) {
            if (GetGameIndexForRow(row) == selectedGameIndex) {
                setCurrentCell(row, selectedColumn);
                m_current_game_index = selectedGameIndex;
                m_current_column = selectedColumn;
                break;
            }
        }
    }

    horizontalHeader()->resizeSection(0, icon_size);
    setUpdatesEnabled(true);
    viewport()->update();
}

void GameListFrame::SortNameAscending(int columnIndex) {
    SortTable(columnIndex, true);
}

void GameListFrame::SortNameDescending(int columnIndex) {
    SortTable(columnIndex, false);
}

void GameListFrame::ApplyLastSorting(bool isInitialPopulation) {
    if (isInitialPopulation) {
        sortColumn = 1;
        ListSortedAsc = true;
    }
    SortTable(sortColumn, ListSortedAsc);
}

void GameListFrame::ResizeIcons(int iconSize) {
    setUpdatesEnabled(false);
    const int gameCount = static_cast<int>(m_game_info->m_games.size());
    for (int row = 0; row < rowCount(); ++row) {
        const int gameIndex = GetGameIndexForRow(row);
        if (gameIndex < 0 || gameIndex >= gameCount) {
            continue;
        }
        const auto& game = m_game_info->m_games[gameIndex];
        const QImage scaledPixmap = game.icon.scaled(QSize(iconSize, iconSize), Qt::KeepAspectRatio,
                                                     Qt::SmoothTransformation);
        auto* iconItem = new SortableGameItem(gameIndex);
        iconItem->SetFavorite(m_favorite_serials.contains(QString::fromStdString(game.serial)));
        iconItem->setData(Qt::DecorationRole, scaledPixmap);
        verticalHeader()->resizeSection(row, scaledPixmap.height());
        setItem(row, 0, iconItem);
    }

    horizontalHeader()->resizeSection(0, iconSize);
    horizontalHeader()->setSectionResizeMode(8, QHeaderView::ResizeToContents);
    setUpdatesEnabled(true);
}

void GameListFrame::SetCompatibilityItem(int row, int column, CompatibilityEntry entry,
                                         int gameIndex) {
    auto* item = new SortableGameItem(gameIndex);

    item->SetIntegerKey(static_cast<int>(entry.status));

    if (gameIndex >= 0 && gameIndex < static_cast<int>(m_game_info->m_games.size())) {

        item->SetFavorite(m_favorite_serials.contains(
            QString::fromStdString(m_game_info->m_games[gameIndex].serial)));
    }

    QWidget* widget = new QWidget(this);

    QGridLayout* layout = new QGridLayout(widget);

    widget->setStyleSheet("QToolTip {background-color: black; color: white;}");

    QColor color;
    QString status_explanation;

    switch (entry.status) {
    case CompatibilityStatus::Unknown:
        color = QStringLiteral("#000000");
        status_explanation = tr("Compatibility is untested");
        break;
    case CompatibilityStatus::Nothing:
        color = QStringLiteral("#FB2C36");
        status_explanation = tr("Game does not initialize properly / crashes the emulator");
        break;
    case CompatibilityStatus::Boots:
        color = QStringLiteral("#F0B100");
        status_explanation = tr("Game boots, but only displays a blank screen");
        break;
    case CompatibilityStatus::Menus:
        color = QStringLiteral("#8E51FF");
        status_explanation = tr("Game displays an image but does not go past the menu");
        break;
    case CompatibilityStatus::Ingame:
        color = QStringLiteral("#2B7FFF");
        status_explanation = tr("Game has game-breaking glitches or unplayable performance");
        break;
    case CompatibilityStatus::Playable:
        color = QStringLiteral("#00C950");
        status_explanation =
            tr("Game can be completed with playable performance and no major glitches");
        break;
    }

    QString tooltip_string;

    if (entry.status == CompatibilityStatus::Unknown) {
        tooltip_string = status_explanation;
    } else {
        tooltip_string =
            "<p> <i>" + tr("Click to see details on github") + "</i><br>" + tr("Last updated") +
            QString(": %1 (%2)").arg(entry.last_tested.toString("yyyy-MM-dd"), entry.version) +
            "<br>" + status_explanation + "</p>";
    }

    QPixmap circle_pixmap(16, 16);
    circle_pixmap.fill(Qt::transparent);
    QPainter painter(&circle_pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(color);
    painter.setBrush(color);
    painter.drawEllipse(QPointF(circle_pixmap.width() / 2.0, circle_pixmap.height() / 2.0), 6.0,
                        6.0);
    QLabel* dotLabel = new QLabel("", widget);
    dotLabel->setPixmap(circle_pixmap);

    QLabel* label = new QLabel(m_compat_info->GetCompatStatusString(entry.status), widget);

    label->setStyleSheet("color: white; font-size: 16px; font-weight: bold;");

    QGraphicsDropShadowEffect* shadowEffect = new QGraphicsDropShadowEffect;

    shadowEffect->setBlurRadius(5);
    shadowEffect->setColor(QColor(0, 0, 0, 160));
    shadowEffect->setOffset(2, 2);
    label->setGraphicsEffect(shadowEffect);
    layout->addWidget(dotLabel, 0, 0, -1, 1);
    layout->addWidget(label, 0, 1, 1, 1);
    layout->setAlignment(Qt::AlignLeft);
    widget->setLayout(layout);
    widget->setToolTip(tooltip_string);

    setItem(row, column, item);

    setCellWidget(row, column, widget);
}

void GameListFrame::SetTableItem(int row, int column, QString itemStr) {
    const int gameIndex = GetGameIndexForRow(row);
    int resolvedGameIndex = gameIndex;
    if (resolvedGameIndex < 0) {
        resolvedGameIndex = row;
    }
    if (resolvedGameIndex < 0 ||
        resolvedGameIndex >= static_cast<int>(m_game_info->m_games.size())) {
        resolvedGameIndex = -1;
    }
    auto* item = new SortableGameItem(resolvedGameIndex);
    if (resolvedGameIndex >= 0) {
        const auto& game = m_game_info->m_games[resolvedGameIndex];
        item->SetFavorite(m_favorite_serials.contains(QString::fromStdString(game.serial)));
    }

    switch (column) {
    case 1:
    case 3: {
        // Name / Serial

        item->SetTextKey(MakeSortText(itemStr));
        break;
    }
    case 5: {
        // Firmware
        bool ok = false;
        const double firmware = itemStr.trimmed().toDouble(&ok);
        if (ok) {
            item->SetNumberKey(firmware);
        } else {
            item->SetTextKey(MakeSortText(itemStr));
        }
        break;
    }
    case 6: {
        // Size
        item->SetNumberKey(ParseSizeMB(itemStr.toStdString()));
        break;
    }
    case 7: {
        // Version
        item->SetTextKey(MakeSortText(itemStr));
        break;
    }
    case 8: {
        // Play Time
        std::string playTime;
        if (resolvedGameIndex >= 0) {
            playTime = m_game_info->m_games[resolvedGameIndex].play_time;
        }
        item->SetIntegerKey(ParsePlayTime(playTime));
        break;
    }
    case 9: {
        // Path
        item->SetTextKey(MakeSortText(itemStr));
        break;
    }
    default: {
        item->SetTextKey(MakeSortText(itemStr));
        break;
    }
    }

    item->setData(GameIndexRole, resolvedGameIndex);
    QWidget* widget = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(widget);
    QLabel* label = new QLabel(itemStr, widget);

    label->setStyleSheet("color: white;"
                         "font-size: 16px;"
                         "font-weight: bold;");

    QGraphicsDropShadowEffect* shadowEffect = new QGraphicsDropShadowEffect;

    shadowEffect->setBlurRadius(5);
    shadowEffect->setColor(QColor(0, 0, 0, 160));
    shadowEffect->setOffset(2, 2);

    label->setGraphicsEffect(shadowEffect);

    layout->addWidget(label);

    if (column != 8 && column != 1 && column != 9) {
        layout->setAlignment(Qt::AlignCenter);
    }
    widget->setLayout(layout);
    setItem(row, column, item);
    setCellWidget(row, column, widget);
}

void GameListFrame::SetRegionFlag(int row, int column, QString itemStr) {

    int gameIndex = GetGameIndexForRow(row);

    if (gameIndex < 0 || gameIndex >= static_cast<int>(m_game_info->m_games.size())) {
        gameIndex = row;
    }

    if (gameIndex < 0 || gameIndex >= static_cast<int>(m_game_info->m_games.size())) {
        return;
    }

    const auto& game = m_game_info->m_games[gameIndex];
    auto* item = new SortableGameItem(gameIndex);
    item->SetFavorite(m_favorite_serials.contains(QString::fromStdString(game.serial)));
    item->SetTextKey(MakeSortText(itemStr));
    item->setData(GameIndexRole, gameIndex);

    QImage scaledPixmap;
    if (itemStr.compare("Japan", Qt::CaseInsensitive) == 0) {
        scaledPixmap = QImage(":images/flag_jp.png");
    } else if (itemStr.compare("Europe", Qt::CaseInsensitive) == 0) {
        scaledPixmap = QImage(":images/flag_eu.png");
    } else if (itemStr.compare("USA", Qt::CaseInsensitive) == 0) {
        scaledPixmap = QImage(":images/flag_us.png");
    } else if (itemStr.compare("Asia", Qt::CaseInsensitive) == 0) {
        scaledPixmap = QImage(":images/flag_china.png");
    } else if (itemStr.compare("World", Qt::CaseInsensitive) == 0) {
        scaledPixmap = QImage(":images/flag_world.png");
    } else {
        scaledPixmap = QImage(":images/flag_unk.png");
    }
    QWidget* widget = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(widget);
    QLabel* label = new QLabel(widget);
    label->setPixmap(QPixmap::fromImage(scaledPixmap));
    layout->setAlignment(Qt::AlignCenter);
    layout->addWidget(label);
    widget->setLayout(layout);

    setItem(row, column, item);
    setCellWidget(row, column, widget);
}

void GameListFrame::UpdateFavoriteStateForAllItems() {
    for (int row = 0; row < rowCount(); ++row) {
        const int gameIndex = GetGameIndexForRow(row);
        if (gameIndex < 0 || gameIndex >= static_cast<int>(m_game_info->m_games.size())) {
            continue;
        }
        const auto& game = m_game_info->m_games[gameIndex];
        const bool isFavorite = m_favorite_serials.contains(QString::fromStdString(game.serial));

        for (int column = 0; column < columnCount(); ++column) {
            if (auto* item = dynamic_cast<SortableGameItem*>(this->item(row, column))) {
                item->SetFavorite(isFavorite);
            }
        }
    }
}

void GameListFrame::UpdateFavoriteVisual(int row) {
    if (row < 0 || row >= rowCount()) {
        return;
    }
    const int gameIndex = GetGameIndexForRow(row);
    if (gameIndex < 0 || gameIndex >= static_cast<int>(m_game_info->m_games.size())) {
        return;
    }

    const auto& game = m_game_info->m_games[gameIndex];
    const bool isFavorite = m_favorite_serials.contains(QString::fromStdString(game.serial));
    if (auto* favoriteItem = dynamic_cast<SortableGameItem*>(item(row, 10))) {
        favoriteItem->SetFavorite(isFavorite);
        favoriteItem->SetIntegerKey(isFavorite ? 0 : 1);
    }

    auto* widget = cellWidget(row, 10);

    if (!widget) {
        SetFavoriteIcon(row, 10);
        return;
    }

    auto* label = widget->findChild<QLabel*>("favoriteIcon");

    if (!label) {
        SetFavoriteIcon(row, 10);
        return;
    }

    if (isFavorite) {

        QImage favoritePixmap(":images/favorite_icon.png");

        if (!favoritePixmap.isNull()) {

            const int iconSize = std::max(1, columnWidth(10) / 2);
            favoritePixmap = favoritePixmap.scaled(iconSize, iconSize, Qt::KeepAspectRatio,
                                                   Qt::SmoothTransformation);
            label->setPixmap(QPixmap::fromImage(favoritePixmap));
        }
        label->setVisible(true);
    } else {
        label->clear();
        label->setVisible(false);
    }
    widget->update();
    label->update();
}

void GameListFrame::SetFavoriteIcon(int row, int column) {

    const int gameIndex = GetGameIndexForRow(row);

    if (gameIndex < 0 || gameIndex >= static_cast<int>(m_game_info->m_games.size())) {
        return;
    }

    const auto& game = m_game_info->m_games[gameIndex];
    const bool isFavorite = m_favorite_serials.contains(QString::fromStdString(game.serial));
    auto* item = new SortableGameItem(gameIndex);
    item->SetFavorite(isFavorite);
    item->SetIntegerKey(isFavorite ? 0 : 1);
    QWidget* widget = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setAlignment(Qt::AlignCenter);
    QLabel* label = new QLabel(widget);
    label->setObjectName("favoriteIcon");
    label->setAlignment(Qt::AlignCenter);

    if (isFavorite) {

        QImage favoriteImage(":images/favorite_icon.png");
        if (!favoriteImage.isNull()) {
            const int iconSize = std::max(1, columnWidth(column) / 2);
            favoriteImage = favoriteImage.scaled(iconSize, iconSize, Qt::KeepAspectRatio,
                                                 Qt::SmoothTransformation);
            label->setPixmap(QPixmap::fromImage(favoriteImage));
        }
        label->setVisible(true);
    } else {
        label->clear();
        label->setVisible(false);
    }
    layout->addWidget(label);
    widget->setLayout(layout);
    setItem(row, column, item);
    setCellWidget(row, column, widget);
}

QString GameListFrame::GetPlayTime(const std::string& serial) {
    return m_play_time_cache.value(QString::fromStdString(serial));
}

QTableWidgetItem* GameListFrame::GetCurrentItem() {
    if (m_current_game_index < 0) {
        return nullptr;
    }
    for (int row = 0; row < rowCount(); ++row) {
        if (GetGameIndexForRow(row) == m_current_game_index) {
            const int column = m_current_column >= 0 ? m_current_column : 1;
            return item(row, column);
        }
    }
    return nullptr;
}

void GameListFrame::ToggleColumnVisibility() {
    const bool showIcon = m_gui_settings->GetValue(gui::glc_showIconEnabled).toBool();
    const bool showName = m_gui_settings->GetValue(gui::glc_showNameEnabled).toBool();
    const bool showCompatibility = m_gui_settings->GetValue(gui::glc_showCompatibility).toBool();
    const bool showSerial = m_gui_settings->GetValue(gui::glc_showSerialEnabled).toBool();
    const bool showRegion = m_gui_settings->GetValue(gui::glc_showRegionEnabled).toBool();
    const bool showFirmware = m_gui_settings->GetValue(gui::glc_showFirmwareEnabled).toBool();
    const bool showSize = m_gui_settings->GetValue(gui::glc_showLoadGameSizeEnabled).toBool();
    const bool showVersion = m_gui_settings->GetValue(gui::glc_showVersionEnabled).toBool();
    const bool showPlayTime = m_gui_settings->GetValue(gui::glc_showPlayTimeEnabled).toBool();
    const bool showPath = m_gui_settings->GetValue(gui::glc_showPathEnabled).toBool();
    const bool showFavorite = m_gui_settings->GetValue(gui::glc_showFavoriteEnabled).toBool();

    setColumnHidden(0, !showIcon);
    setColumnHidden(1, !showName);
    setColumnHidden(2, !showCompatibility);
    setColumnHidden(3, !showSerial);
    setColumnHidden(4, !showRegion);
    setColumnHidden(5, !showFirmware);
    setColumnHidden(6, !showSize);
    setColumnHidden(7, !showVersion);
    setColumnHidden(8, !showPlayTime);
    setColumnHidden(9, !showPath);
    setColumnHidden(10, !showFavorite);

    if (!showPath) {
        setColumnHidden(11, false);
    } else {
        setColumnHidden(11, true);
    }

    if (showFavorite) {
        UpdateFavoriteCache();
        for (int row = 0; row < rowCount(); ++row) {
            UpdateFavoriteVisual(row);
        }
    }
}

void GameListFrame::ShowHeaderContextMenu(const QPoint& pos) {
    QMenu contextMenu(this);

    struct ColumnToggle {
        QString name;
        int column;
        gui_value configKey;
    };

    const std::vector<ColumnToggle> columns = {
        {tr("Icon"), 0, gui::glc_showIconEnabled},
        {tr("Name"), 1, gui::glc_showNameEnabled},
        {tr("Compatibility"), 2, gui::glc_showCompatibility},
        {tr("Serial"), 3, gui::glc_showSerialEnabled},
        {tr("Region"), 4, gui::glc_showRegionEnabled},
        {tr("Firmware"), 5, gui::glc_showFirmwareEnabled},
        {tr("Size"), 6, gui::glc_showLoadGameSizeEnabled},
        {tr("Version"), 7, gui::glc_showVersionEnabled},
        {tr("Play Time"), 8, gui::glc_showPlayTimeEnabled},
        {tr("Path"), 9, gui::glc_showPathEnabled},
        {tr("Favorite"), 10, gui::glc_showFavoriteEnabled},
    };

    for (const auto& col : columns) {
        const bool isChecked = m_gui_settings->GetValue(col.configKey).toBool();

        QAction* action = contextMenu.addAction(col.name);
        action->setCheckable(true);
        action->setChecked(isChecked);

        connect(action, &QAction::toggled, this, [this, col](bool checked) {
            m_gui_settings->SetValue(col.configKey, checked);
            ToggleColumnVisibility();
        });
    }

    contextMenu.exec(horizontalHeader()->mapToGlobal(pos));
}
