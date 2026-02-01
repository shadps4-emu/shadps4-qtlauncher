// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <iostream>
#include <iterator>
#include <memory>
#include <vector>

#include <QCoreApplication>
#include <QDialog>
#include <QEventLoop>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

#include "common/config.h"
#include "common/path_util.h"
#include "core/emulator_state.h"
#include "ipc/ipc_client.h"
#include "qt_gui/about_dialog.h"
#include "qt_gui/cheats_patches.h"
#include "qt_gui/compatibility_info.h"
#include "qt_gui/control_settings.h"
#include "qt_gui/dimensions_dialog.h"
#include "qt_gui/game_info.h"
#include "qt_gui/game_install_dialog.h"
#include "qt_gui/hotkeys.h"
#include "qt_gui/infinity_dialog.h"
#include "qt_gui/kbm_gui.h"
#include "qt_gui/open_targets.h"
#include "qt_gui/settings_dialog.h"
#include "qt_gui/settings_tab_dialog.h"
#include "qt_gui/settings_tab_targets.h"
#include "qt_gui/skylander_dialog.h"
#include "qt_gui/trophy_viewer.h"
#include "qt_gui/version_dialog.h"

namespace UiOpenTargets {

namespace {

struct TargetEntry {
    QString name;
    TargetId id;
};

const std::vector<TargetEntry>& TargetEntries() {
    static const std::vector<TargetEntry> entries = {
        {"settings", TargetId::Settings},
        {"version_manager", TargetId::VersionManager},
        {"controllers", TargetId::Controllers},
        {"keyboard_mouse", TargetId::KeyboardMouse},
        {"hotkeys", TargetId::Hotkeys},
        {"about", TargetId::About},
        {"game_install", TargetId::GameInstall},
        {"cheats_patches_download", TargetId::CheatsPatchesDownload},
        {"trophy_viewer", TargetId::TrophyViewer},
        {"skylander_portal", TargetId::SkylanderPortal},
        {"infinity_base", TargetId::InfinityBase},
        {"dimensions_toypad", TargetId::DimensionsToypad},
    };
    return entries;
}

QString NormalizeTargetString(const QString& target) {
    return target.trimmed().toLower();
}

QStringList CollectSettingsTabTargets(SettingsDialog* dialog) {
    QStringList results;
    if (!dialog) {
        return results;
    }

    QTabWidget* tab_widget = dialog->GetTabWidget();
    if (!tab_widget) {
        return results;
    }

    for (int i = 0; i < tab_widget->count(); ++i) {
        if (!tab_widget->isTabVisible(i)) {
            continue;
        }
        QWidget* page = tab_widget->widget(i);
        if (!page) {
            continue;
        }
        const QString target = QtGui::SettingsTabs::BuildTabTarget(page->objectName());
        if (!target.isEmpty()) {
            results.append(target);
        }
    }

    results.sort();
    return results;
}

std::optional<QString> ResolveSettingsTabObjectName(SettingsDialog* dialog, const QString& key) {
    if (!dialog) {
        return std::nullopt;
    }

    QTabWidget* tab_widget = dialog->GetTabWidget();
    if (!tab_widget) {
        return std::nullopt;
    }

    const QString normalized_key = QtGui::SettingsTabs::NormalizeTabKey(key);
    for (int i = 0; i < tab_widget->count(); ++i) {
        if (!tab_widget->isTabVisible(i)) {
            continue;
        }
        QWidget* page = tab_widget->widget(i);
        if (!page) {
            continue;
        }
        const QString normalized_page =
            QtGui::SettingsTabs::NormalizeTabObjectName(page->objectName());
        if (normalized_page == normalized_key) {
            return page->objectName();
        }
    }

    return std::nullopt;
}

QStringList BuildAvailableTargets(const OpenTargetContext& context) {
    QStringList results = GetTargetIdStrings();

    if (context.gui_settings_shared && context.compat_info && context.ipc_client) {
        auto settings_dialog = std::make_unique<SettingsDialog>(
            context.gui_settings_shared, context.compat_info, context.ipc_client, nullptr,
            context.is_game_running, context.is_game_specific, context.game_serial);
        results.append(CollectSettingsTabTargets(settings_dialog.get()));
        settings_dialog->close();
    }

    results.removeDuplicates();
    results.sort();
    return results;
}

void ReportError(const QString& message, const OpenBehavior& behavior, QWidget* parent) {
    if (message.isEmpty()) {
        return;
    }

    ErrorMode mode = behavior.error_mode;
    if (mode == ErrorMode::Default) {
        mode = parent ? ErrorMode::MessageBox : ErrorMode::StdErr;
    }

    if (mode == ErrorMode::MessageBox) {
        QMessageBox::critical(parent, QObject::tr("Open Target"), message);
        return;
    }

    if (mode == ErrorMode::StdErr) {
        std::cerr << message.toStdString() << "\n";
        return;
    }
}

void AttachExitOnClose(QObject* target, const OpenBehavior& behavior) {
    if (!behavior.exit_on_close || !target) {
        return;
    }

    QObject::connect(target, &QObject::destroyed, QCoreApplication::instance(),
                     []() { QCoreApplication::quit(); });
}

void AttachDialogExitOnClose(QDialog* dialog, const OpenBehavior& behavior) {
    if (!behavior.exit_on_close || !dialog) {
        return;
    }

    QObject::connect(dialog, &QDialog::finished, QCoreApplication::instance(),
                     []() { QCoreApplication::quit(); });
}

OpenTargetResult OpenDialog(QDialog* dialog, const OpenBehavior& behavior) {
    OpenTargetResult result;
    result.opened_window = dialog;
    result.success = dialog != nullptr;
    if (!dialog) {
        return result;
    }

    if (behavior.exit_on_close) {
        AttachDialogExitOnClose(dialog, behavior);
        dialog->show();
    } else {
        dialog->exec();
    }
    return result;
}

OpenTargetResult OpenWindow(QWidget* window, const OpenBehavior& behavior) {
    OpenTargetResult result;
    result.opened_window = window;
    result.success = window != nullptr;
    if (!window) {
        return result;
    }

    AttachExitOnClose(window, behavior);
    window->show();
    return result;
}

void AttachParentDestroy(QWidget* parent, QWidget* child, bool attach) {
    if (!attach || !parent || !child) {
        return;
    }

    QObject::connect(parent, &QWidget::destroyed, child, [child]() { child->deleteLater(); });
}

std::optional<std::filesystem::path> ResolveGameTrpPath(const std::filesystem::path& game_path) {
    auto update_path = game_path;
    update_path += "-UPDATE";
    if (std::filesystem::exists(update_path)) {
        return update_path;
    }

    auto patch_path = game_path;
    patch_path += "-patch";
    if (std::filesystem::exists(patch_path)) {
        return patch_path;
    }

    return game_path;
}

} // namespace

OpenBehavior OpenBehaviorForUi() {
    OpenBehavior behavior;
    behavior.exit_on_close = false;
    behavior.error_mode = ErrorMode::Default;
    return behavior;
}

OpenBehavior OpenBehaviorForCli() {
    OpenBehavior behavior;
    behavior.exit_on_close = true;
    behavior.error_mode = ErrorMode::StdErr;
    return behavior;
}

std::optional<TargetId> ParseTargetId(const QString& target) {
    const QString normalized = NormalizeTargetString(target);
    for (const auto& entry : TargetEntries()) {
        if (entry.name == normalized) {
            return entry.id;
        }
    }
    return std::nullopt;
}

QStringList GetTargetIdStrings() {
    QStringList results;
    for (const auto& entry : TargetEntries()) {
        results.append(entry.name);
    }
    std::sort(results.begin(), results.end());
    return results;
}

OpenTargetResult OpenTarget(TargetId target_id, const OpenTargetContext& context,
                            const OpenBehavior& behavior) {
    OpenTargetResult result;
    result.success = false;

    QWidget* parent = context.parent;

    switch (target_id) {
    case TargetId::Settings: {
        if (!context.gui_settings_shared || !context.compat_info || !context.ipc_client) {
            result.error_message = QObject::tr("Settings requires GUI, compatibility, and IPC.");
            break;
        }
        auto dialog = new SettingsDialog(context.gui_settings_shared, context.compat_info,
                                         context.ipc_client, parent, context.is_game_running,
                                         context.is_game_specific, context.game_serial);
        AttachParentDestroy(parent, dialog, context.attach_parent_destroy);
        result = OpenDialog(dialog, behavior);
        break;
    }
    case TargetId::VersionManager: {
        if (!context.gui_settings_shared) {
            result.error_message = QObject::tr("Version Manager requires GUI settings.");
            break;
        }
        auto dialog = new VersionDialog(context.gui_settings_shared, parent);
        AttachParentDestroy(parent, dialog, context.attach_parent_destroy);
        result = OpenDialog(dialog, behavior);
        break;
    }
    case TargetId::Controllers: {
        if (!context.game_info || !context.ipc_client) {
            result.error_message = QObject::tr("Controllers require game info and IPC.");
            break;
        }
        auto dialog =
            new ControlSettings(context.game_info, context.ipc_client, context.is_game_running,
                                context.running_game_serial, parent);
        AttachParentDestroy(parent, dialog, context.attach_parent_destroy);
        result = OpenDialog(dialog, behavior);
        break;
    }
    case TargetId::KeyboardMouse: {
        if (!context.game_info || !context.ipc_client) {
            result.error_message = QObject::tr("Keyboard/Mouse requires game info and IPC.");
            break;
        }
        auto dialog = new KBMSettings(context.game_info, context.ipc_client,
                                      context.is_game_running, context.running_game_serial, parent);
        AttachParentDestroy(parent, dialog, context.attach_parent_destroy);
        result = OpenDialog(dialog, behavior);
        break;
    }
    case TargetId::Hotkeys: {
        if (!context.ipc_client) {
            result.error_message = QObject::tr("Hotkeys requires IPC.");
            break;
        }
        auto dialog = new Hotkeys(context.ipc_client, context.is_game_running, parent);
        AttachParentDestroy(parent, dialog, context.attach_parent_destroy);
        result = OpenDialog(dialog, behavior);
        break;
    }
    case TargetId::About: {
        if (!context.gui_settings_shared) {
            result.error_message = QObject::tr("About dialog requires GUI settings.");
            break;
        }
        auto dialog = new AboutDialog(context.gui_settings_shared, parent);
        AttachParentDestroy(parent, dialog, context.attach_parent_destroy);
        result = OpenDialog(dialog, behavior);
        break;
    }
    case TargetId::GameInstall: {
        auto dialog = new GameInstallDialog();
        AttachParentDestroy(parent, dialog, context.attach_parent_destroy);
        result = OpenDialog(dialog, behavior);
        break;
    }
    case TargetId::CheatsPatchesDownload: {
        if (!context.game_info || !context.gui_settings_shared || !context.ipc_client) {
            result.error_message = QObject::tr("Cheats/Patches requires game info, GUI, and IPC.");
            break;
        }
        auto panel_dialog = new QDialog(parent);
        QVBoxLayout* layout = new QVBoxLayout(panel_dialog);
        QPushButton* download_all_cheats_button =
            new QPushButton(QObject::tr("Download Cheats For All Installed Games"), panel_dialog);
        QPushButton* download_all_patches_button =
            new QPushButton(QObject::tr("Download Patches For All Games"), panel_dialog);

        layout->addWidget(download_all_cheats_button);
        layout->addWidget(download_all_patches_button);
        panel_dialog->setLayout(layout);

        QObject::connect(
            download_all_cheats_button, &QPushButton::clicked, panel_dialog,
            [panel_dialog, context]() {
                QEventLoop event_loop;
                int pending_downloads = 0;

                auto on_download_finished = [&]() {
                    if (--pending_downloads <= 0) {
                        event_loop.quit();
                    }
                };

                for (const GameInfo& game : context.game_info->m_games) {
                    QString empty = "";
                    QString game_serial = QString::fromStdString(game.serial);
                    QString game_version = QString::fromStdString(game.version);

                    CheatsPatches* cheats_patches =
                        new CheatsPatches(context.gui_settings_shared, context.ipc_client, empty,
                                          empty, empty, empty, empty, nullptr);
                    QObject::connect(cheats_patches, &CheatsPatches::downloadFinished,
                                     on_download_finished);

                    pending_downloads += 2;

                    cheats_patches->downloadCheats("GoldHEN", game_serial, game_version, false);
                    cheats_patches->downloadCheats("shadPS4", game_serial, game_version, false);
                }
                event_loop.exec();

                QMessageBox::information(
                    nullptr, QObject::tr("Download Complete"),
                    QObject::tr("You have downloaded cheats for all the games you "
                                "have installed."));

                panel_dialog->accept();
            });
        QObject::connect(download_all_patches_button, &QPushButton::clicked, panel_dialog,
                         [panel_dialog, context]() {
                             QEventLoop event_loop;
                             int pending_downloads = 0;

                             auto on_download_finished = [&]() {
                                 if (--pending_downloads <= 0) {
                                     event_loop.quit();
                                 }
                             };

                             QString empty = "";
                             CheatsPatches* cheats_patches =
                                 new CheatsPatches(context.gui_settings_shared, context.ipc_client,
                                                   empty, empty, empty, empty, empty, nullptr);
                             QObject::connect(cheats_patches, &CheatsPatches::downloadFinished,
                                              on_download_finished);

                             pending_downloads += 2;

                             cheats_patches->downloadPatches("GoldHEN", false);
                             cheats_patches->downloadPatches("shadPS4", false);

                             event_loop.exec();
                             QMessageBox::information(
                                 nullptr, QObject::tr("Download Complete"),
                                 QString(QObject::tr("Patches Downloaded Successfully!") + "\n" +
                                         QObject::tr("All Patches available for all games have "
                                                     "been downloaded.")));
                             cheats_patches->createFilesJson("GoldHEN");
                             cheats_patches->createFilesJson("shadPS4");
                             panel_dialog->accept();
                         });

        AttachParentDestroy(parent, panel_dialog, context.attach_parent_destroy);
        result = OpenDialog(panel_dialog, behavior);
        break;
    }
    case TargetId::TrophyViewer: {
        if (!context.game_info || !context.gui_settings_shared) {
            result.error_message =
                QObject::tr("Trophy Viewer requires game info and GUI settings.");
            break;
        }
        if (context.game_info->m_games.empty()) {
            result.error_message =
                QObject::tr("No games found. Please add your games to your library first.");
            break;
        }

        int selected_index = 0;
        if (!context.game_serial.empty()) {
            const auto it = std::find_if(
                context.game_info->m_games.begin(), context.game_info->m_games.end(),
                [&context](const GameInfo& game) { return game.serial == context.game_serial; });
            if (it != context.game_info->m_games.end()) {
                selected_index =
                    static_cast<int>(std::distance(context.game_info->m_games.begin(), it));
            }
        }

        const auto& selected_game = context.game_info->m_games[selected_index];
        QString trophy_path;
        QString game_trp_path;
        Common::FS::PathToQString(trophy_path, selected_game.serial);

        auto selected_trp_path = ResolveGameTrpPath(selected_game.path);
        Common::FS::PathToQString(game_trp_path, *selected_trp_path);

        QVector<TrophyGameInfo> all_trophy_games;
        for (const auto& game : context.game_info->m_games) {
            TrophyGameInfo game_info;
            game_info.name = QString::fromStdString(game.name);
            Common::FS::PathToQString(game_info.trophyPath, game.serial);
            auto trp_path = ResolveGameTrpPath(game.path);
            Common::FS::PathToQString(game_info.gameTrpPath, *trp_path);
            all_trophy_games.append(game_info);
        }

        QString game_name = QString::fromStdString(selected_game.name);
        auto trophy_viewer = new TrophyViewer(context.gui_settings_shared, trophy_path,
                                              game_trp_path, game_name, all_trophy_games);
        AttachParentDestroy(parent, trophy_viewer, context.attach_parent_destroy);
        result = OpenWindow(trophy_viewer, behavior);
        break;
    }
    case TargetId::SkylanderPortal: {
        if (!context.ipc_client) {
            result.error_message = QObject::tr("Skylander Portal requires IPC.");
            break;
        }
        if (Config::getUsbDeviceBackend() != Config::UsbBackendType::SkylandersPortal) {
            result.error_message = QObject::tr("Skylander Portal backend is not selected.");
            break;
        }
        auto dialog = skylander_dialog::get_dlg(parent, context.ipc_client);
        result = OpenWindow(dialog, behavior);
        break;
    }
    case TargetId::InfinityBase: {
        if (!context.ipc_client) {
            result.error_message = QObject::tr("Infinity Base requires IPC.");
            break;
        }
        if (Config::getUsbDeviceBackend() != Config::UsbBackendType::InfinityBase) {
            result.error_message = QObject::tr("Infinity Base backend is not selected.");
            break;
        }
        auto dialog = infinity_dialog::get_dlg(parent, context.ipc_client);
        result = OpenWindow(dialog, behavior);
        break;
    }
    case TargetId::DimensionsToypad: {
        if (!context.ipc_client) {
            result.error_message = QObject::tr("Dimensions Toypad requires IPC.");
            break;
        }
        if (Config::getUsbDeviceBackend() != Config::UsbBackendType::DimensionsToypad) {
            result.error_message = QObject::tr("Dimensions Toypad backend is not selected.");
            break;
        }
        auto dialog = dimensions_dialog::get_dlg(parent, context.ipc_client);
        result = OpenWindow(dialog, behavior);
        break;
    }
    }

    if (!result.success && !result.error_message.isEmpty()) {
        ReportError(result.error_message, behavior, parent);
    }

    return result;
}

OpenTargetResult OpenTarget(const QString& target, const OpenTargetContext& context,
                            const OpenBehavior& behavior) {
    const QString settings_key = QtGui::SettingsTabs::ExtractTabKey(target);
    if (!settings_key.isEmpty()) {
        OpenTargetResult result;
        if (!context.gui_settings_shared || !context.compat_info || !context.ipc_client) {
            result.error_message = QObject::tr("Settings require GUI, compatibility, and IPC.");
            ReportError(result.error_message, behavior, context.parent);
            return result;
        }

        auto settings_dialog = std::make_unique<SettingsDialog>(
            context.gui_settings_shared, context.compat_info, context.ipc_client, nullptr,
            context.is_game_running, context.is_game_specific, context.game_serial);
        const auto object_name = ResolveSettingsTabObjectName(settings_dialog.get(), settings_key);
        if (!object_name) {
            const QStringList settings_targets = CollectSettingsTabTargets(settings_dialog.get());
            result.error_message = QObject::tr("Unknown settings tab '%1'. Available: %2")
                                       .arg(settings_key, settings_targets.join(", "));
            ReportError(result.error_message, behavior, context.parent);
            settings_dialog->close();
            return result;
        }

        auto wrapper =
            new SettingsTabDialog(settings_dialog.release(), *object_name, context.parent);
        AttachParentDestroy(context.parent, wrapper, context.attach_parent_destroy);
        result = OpenDialog(wrapper, behavior);
        return result;
    }

    const auto parsed = ParseTargetId(target);
    if (!parsed) {
        OpenTargetResult result;
        result.error_message = QObject::tr("Unknown target '%1'. Available: %2")
                                   .arg(target, BuildAvailableTargets(context).join(", "));
        ReportError(result.error_message, behavior, context.parent);
        return result;
    }

    return OpenTarget(*parsed, context, behavior);
}

} // namespace UiOpenTargets
