// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharVR: GMainWindow's side of a DS, DSi, GBA or Game Boy game (frame_cores): its window, menus,
// status bar, save states and screenshots. BootAltGame and ShutdownAltGame call it; the functions
// of a 3DS game's menu actions branch to it while such a game runs.

#include <algorithm>
#include <string>
#include <utility>
#include <QDateTime>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QTimer>

#include "audio_core/input.h"
#include "audio_core/input_details.h"
#include "citra_qt/bootmanager.h"
#include "citra_qt/citra_qt.h"
#include "citra_qt/game_list.h"
#include "citra_qt/uisettings.h"
#include "common/logging/log.h"
#include "common/settings.h"
#include "core/frontend/alt_screens.h"
#include "frame_cores/alt_core_host.h"
#include "frame_cores/profile_input.h"
#include "ui_main.h"
#ifdef ENABLE_FRAME_XR
#include "citra_qt/frame_mirror.h"
#include "frame_xr/frame_xr.h"
#include "input_common/frame_xr.h"
#endif

namespace {

/// The screens a game starts with; the host reports changes (a GBA game's map).
Layout::AltScreens StartScreens(Common::AltCore::CoreKind kind) {
    using Common::AltCore::CoreKind;
    switch (kind) {
    case CoreKind::NDS:
    case CoreKind::DSi:
        return {256, 192, 256, 192};
    case CoreKind::GBA:
        return {240, 160, 0, 0};
    default:
        return {160, 144, 0, 0};
    }
}

/// Input Type and Device; the game's input is made again when they change.
std::pair<AudioCore::InputType, std::string> MicrophoneSetting() {
    return {Settings::values.input_type.GetValue(), Settings::values.input_device.GetValue()};
}
std::pair<AudioCore::InputType, std::string> g_microphone;

constexpr const char* kSaveImported = "Save Imported";

QRect ToQRect(const Common::Rectangle<u32>& r) {
    return {static_cast<int>(r.left), static_cast<int>(r.top), static_cast<int>(r.GetWidth()),
            static_cast<int>(r.GetHeight())};
}

} // Anonymous namespace

bool GMainWindow::AltGamePaused() const {
    return AltGameRunning() && alt_core_host->IsPaused();
}

bool GMainWindow::AltWindowOpen(const FrameCores::RomInfo& info) {
    Layout::SetAltScreens(StartScreens(info.kind));
    // Always Vulkan, whatever renders the 3DS.
    if (!render_window->InitRenderTarget(true) || !secondary_window->InitRenderTarget(true)) {
        return false;
    }
    if (!alt_input) {
        alt_input = std::make_unique<FrameCores::ProfileInput>();
    }
    alt_input->Reload();
    // Any thread: the settings' listeners, as a 3DS game's.
    system.SetSettingsCallback([this] {
        QMetaObject::invokeMethod(this, [this] { OnAltSettingsChanged(); }, Qt::AutoConnection);
    });

    FrameCores::HostFrontend frontend;
    frontend.window = render_window;
    frontend.secondary_window = secondary_window;
    frontend.input = [pad = alt_input.get()] {
        const auto screens = Layout::GetAltScreens().value_or(Layout::AltScreens{});
        auto state =
            pad->Read(std::max(1u, screens.bottom_width), std::max(1u, screens.bottom_height));
#ifdef ENABLE_FRAME_XR
        if (FrameXr::VrShowing()) { // the XR thread maps VR's touches (PushInput)
            state.touching = false;
        }
        // A gamepad's or the keyboard's own, in VR and Flat (Edit Controls).
        const auto controls = InputCommon::FrameXr::GetPad().Snapshot();
        state.buttons |= controls.mic_blow ? Common::AltCore::BtnMic : 0;
        state.buttons |= controls.quick_menu ? Common::AltCore::BtnQuickMenu : 0;
        state.buttons |= controls.turn_page ? Common::AltCore::BtnTurnPage : 0;
#endif
        return state;
    };
    g_microphone = MicrophoneSetting();
    frontend.microphone = [this] {
        return AudioCore::GetInputDetails(Settings::values.input_type.GetValue())
            .create_input(system, Settings::values.input_device.GetValue());
    };
    frontend.on_screens = [this](const Layout::AltScreens& screens) {
        QMetaObject::invokeMethod(this, [this, screens] {
            if (AltGameRunning()) {
                Layout::SetAltScreens(screens);
                OnAltSettingsChanged();
            }
        });
    };
    frontend.on_written = [this](const Core::Autosave::Writer::Done& done) {
        QMetaObject::invokeMethod(
            this, [this, done] { FrameAutosaveWritten(done); }, Qt::QueuedConnection);
    };
    frontend.on_loaded = [this](const FrameCores::LoadDone& done) {
        QMetaObject::invokeMethod(
            this, [this, done] { AltStateLoaded(done); }, Qt::QueuedConnection);
    };
    alt_core_host->SetFrontend(std::move(frontend));
    return true;
}

void GMainWindow::AltWindowShow() {
    connect(render_window, &GRenderWindow::Closed, this, &GMainWindow::OnStopGame);
    connect(render_window, &GRenderWindow::MouseActivity, this, &GMainWindow::OnMouseActivity);
    connect(secondary_window, &GRenderWindow::Closed, this, &GMainWindow::OnStopGame);
    connect(secondary_window, &GRenderWindow::MouseActivity, this, &GMainWindow::OnMouseActivity);

    if (ui->action_Single_Window_Mode->isChecked()) {
        game_list->hide();
        game_list_placeholder->hide();
    }
    status_bar_update_timer.start(1000);
    if (UISettings::values.hide_mouse) {
        mouse_hide_timer.start();
        setMouseTracking(true);
    }
    render_window->show();
    render_window->setFocus();
    render_window->activateWindow();
    UpdateSecondaryWindowVisibility();
    if (ui->action_Fullscreen->isChecked()) {
        ShowFullscreen();
    }
    graphics_api_button->setEnabled(false);
    ReloadTurbo(); // held over from the last game
    UpdateMenuState();
    UpdateSaveStates();
    UpdateStatusButtons();
}

// A box, as LoadROM's: the VR home panel shows it too. The VR picker has no window: a toast.
void GMainWindow::AltStartFailed(const QString& title, const QString& body) {
#ifdef ENABLE_FRAME_XR
    if (FrameXr::VrShowing() && !FrameMirror::Wanted()) {
        AltNotify(tr("Generic load error").toStdString()); // LoadROM's
        return;
    }
#endif
    QMessageBox::critical(this, title, body);
}

void GMainWindow::AltWindowClose() {
    // The memory dialog goes with the game, as a 3DS game's.
    frame_memwarn_asking = false;
    frame_memwarn_resume = false;
    if (QDialog* d = std::exchange(frame_memwarn_dialog, nullptr)) {
        d->close();
    }
    system.SetSettingsCallback({});
    Layout::SetAltScreens(std::nullopt);
    disconnect(render_window, &GRenderWindow::Closed, this, &GMainWindow::OnStopGame);
    disconnect(render_window, &GRenderWindow::MouseActivity, this, &GMainWindow::OnMouseActivity);
    disconnect(secondary_window, &GRenderWindow::Closed, this, &GMainWindow::OnStopGame);
    disconnect(secondary_window, &GRenderWindow::MouseActivity, this,
               &GMainWindow::OnMouseActivity);
    render_window->hide();
    secondary_window->hide();
    if (game_list->IsEmpty()) {
        game_list_placeholder->show();
    } else {
        game_list->show();
    }
    game_list->SetFilterFocus();

    setMouseTracking(false);
    mouse_hide_timer.stop();
    ShowMouseCursor();
    status_bar_update_timer.stop();
    emu_speed_label->setVisible(false);
    game_fps_label->setVisible(false);
    emu_frametime_label->setVisible(false);

    UpdateSaveStates();
    UpdateMenuState();
    graphics_api_button->setEnabled(true);
    render_window->ReleaseRenderTarget();
    secondary_window->ReleaseRenderTarget();
}

void GMainWindow::OnAltSettingsChanged() {
    if (!AltGameRunning()) {
        return;
    }
    alt_input->Reload();
    if (auto now = MicrophoneSetting(); now != g_microphone) {
        g_microphone = std::move(now);
        alt_core_host->ReloadMicrophone();
    }
    render_window->OnFramebufferSizeChanged();
    secondary_window->OnFramebufferSizeChanged();
    alt_core_host->Redraw();
}

void GMainWindow::PauseAltGame(bool pause) {
    alt_core_host->Pause(pause);
    if (pause) {
        play_time_manager->Stop();
        AllowOSSleep();
    } else {
        play_time_manager->SetProgramId(alt_core_host->GetRomInfo().title_id);
        play_time_manager->Start();
        PreventOSSleep();
    }
    UpdateMenuState();
}

void GMainWindow::UpdateAltStatusBar() {
    const FrameCores::HostStats stats = alt_core_host->GetStats();
    const double speed = AltGamePaused() ? 0.0 : stats.emulation_speed * 100.0;
    if (Settings::GetFrameLimit() == 0) {
        emu_speed_label->setText(tr("Speed: %1%").arg(speed, 0, 'f', 0));
    } else {
        emu_speed_label->setText(
            tr("Speed: %1% / %2%").arg(speed, 0, 'f', 0).arg(Settings::GetFrameLimit()));
    }
    game_fps_label->setText(
        tr("App: %1 FPS").arg(AltGamePaused() ? 0.0 : stats.game_fps, 0, 'f', 0));
    emu_frametime_label->setText(tr("Frame: %1 ms").arg(stats.frame_ms, 2, 'f', 2));
    emu_speed_label->setVisible(true);
    game_fps_label->setVisible(true);
    emu_frametime_label->setVisible(true);
}

// A load, Undo Load or Import Save was done, or not. The emulation thread made it; here its toast
// and Undo Load's offer, as a 3DS game's (FrameUndoOffered).
void GMainWindow::AltStateLoaded(const FrameCores::LoadDone& done) {
    using FrameCores::LoadResult;
    using What = FrameCores::LoadDone::What;
    const auto quick_loaded = std::exchange(alt_quick_loaded, {});
    if (!AltGameRunning()) {
        return;
    }
    if (done.result == LoadResult::Ok) {
        switch (done.what) {
        case What::Slot:
            FrameUndoOffered(done.slot);
            if (done.slot == 0 && quick_loaded) {
                quick_loaded();
            }
            break;
        case What::Autosave:
            frame_loaded_autosave_time = done.time;
            FrameUndoOffered(-1);
            break;
        case What::Undo:
            break;
        case What::Import:
            frame_undo_available = true;
            frame_undo_time = static_cast<u64>(QDateTime::currentSecsSinceEpoch());
            frame_undo_text = std::string(kSaveImported) + ": " +
                              Core::Autosave::LocalTime(frame_undo_time); // as UndoText's
            FrameUpdateAutosaveActions();
            FramePushAutosaves();
            AltNotify(kSaveImported);
            break;
        }
        return;
    }
    if (done.result == LoadResult::OtherBuild) { // the file changed after it was tapped
        if (done.what == What::Autosave) {
            FrameAskLoadAutosaveAnyway(done.path, done.version);
        } else {
            FrameAskLoadAnyway(static_cast<u32>(done.slot), done.version);
        }
        return;
    }
    // The game goes on as it was; an earlier load can still be undone.
    AltNotify(done.what == What::Import ? "Save Import Failed" : Core::kSaveStateNotLoaded);
}

// A toast in VR, the status bar in Flat.
void GMainWindow::AltNotify(const std::string& text) {
    LOG_INFO(Frontend, "Alt core: \"{}\"", text);
#ifdef ENABLE_FRAME_XR
    if (FrameXr::VrShowing()) {
        FrameXr::ShowToastText(text, FrameXr::ToastTime::Long);
        return;
    }
#endif
    if (!message_label_used_for_movie) {
        const QString q = QString::fromStdString(text);
        message_label->setText(q);
        QTimer::singleShot(6000, this, [this, q] {
            if (!message_label_used_for_movie && message_label->text() == q) {
                message_label->clear();
            }
        });
    }
}

void GMainWindow::AltCaptureScreenshot() {
    const u32 scale = std::max(1u, UISettings::values.screenshot_resolution_factor.GetValue() != 0
                                       ? UISettings::values.screenshot_resolution_factor.GetValue()
                                       : Settings::RunningGraphics().resolution_factor.GetValue());
    // The window's layout at that many 3DS pixels, as a 3DS game's screenshot; a game with one
    // screen has the whole picture.
    const auto screens = Layout::GetAltScreens().value_or(Layout::AltScreens{});
    Layout::FramebufferLayout layout;
    if (screens.HasBottom()) {
        layout = Layout::FrameLayoutFromResolutionScale(scale, secondary_window->HasFocus());
        layout = Layout::FitAltScreens(layout, screens, Settings::values.layout_option.GetValue());
    } else {
        u32 width = screens.top_width * scale;
        u32 height = screens.top_height * scale;
        if (Settings::values.upright_screen.GetValue()) {
            std::swap(width, height);
        }
        layout = Layout::SingleFrameLayout(width, height, false,
                                           Settings::values.upright_screen.GetValue());
        layout.top_screen = {0, 0, width, height};
    }
    QImage image(static_cast<int>(layout.width), static_cast<int>(layout.height),
                 QImage::Format_RGB32);
    image.fill(QColor::fromRgbF(Settings::values.bg_red.GetValue(),
                                Settings::values.bg_green.GetValue(),
                                Settings::values.bg_blue.GetValue()));
    alt_core_host->WithScreens([&](const FrameCores::FrameOutput& frame) {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::SmoothPixmapTransform,
                              Settings::values.filter_mode.GetValue());
        const auto draw = [&](const auto& screen, const Common::Rectangle<u32>& rect,
                              float opacity, bool see_through = false) {
            if (!screen.pixels) {
                return;
            }
            QImage picture(reinterpret_cast<const uchar*>(screen.pixels),
                           static_cast<int>(screen.width), static_cast<int>(screen.height),
                           static_cast<qsizetype>(screen.stride_pixels) * 4,
                           see_through   ? QImage::Format_RGBA8888_Premultiplied
                           : screen.bgra ? QImage::Format_RGB32
                                         : QImage::Format_RGBX8888);
            if (!layout.is_rotated) {
                picture = picture.transformed(QTransform().rotate(-90));
            }
            painter.setOpacity(opacity);
            painter.drawImage(ToQRect(rect), picture);
        };
        const auto top = [&](const Common::Rectangle<u32>& rect) {
            if (layout.top_screen_enabled) {
                draw(frame.primary, rect, layout.top_opacity);
                // As a 3DS game's screenshot: what the window shows.
                draw(frame.overlay, rect, layout.top_opacity, true);
            }
        };
        const auto bottom = [&](const Common::Rectangle<u32>& rect) {
            if (layout.bottom_screen_enabled) {
                draw(frame.secondary, rect, layout.bottom_opacity);
            }
        };
        if (!Settings::values.swap_screen.GetValue()) {
            top(layout.top_screen);
            bottom(layout.bottom_screen);
        } else {
            bottom(layout.bottom_screen);
            top(layout.top_screen);
        }
        if (layout.additional_screen_enabled) {
            layout.additional_screen_is_bottom ? bottom(layout.additional_screen)
                                               : top(layout.additional_screen);
        }
    });

    QString title = game_title;
    title.remove(QRegularExpression(QStringLiteral("[\\/:?\"<>|]")));
    const QString path =
        QString::fromStdString(UISettings::values.screenshot_path.GetValue()) +
        QStringLiteral("/%1_%2.png")
            .arg(title,
                 QDateTime::currentDateTime().toString(QStringLiteral("dd.MM.yy_hh.mm.ss.z")));
    if (image.save(path)) {
        LOG_INFO(Frontend, "Screenshot saved to \"{}\"", path.toStdString());
    } else {
        LOG_ERROR(Frontend, "Failed to save screenshot to \"{}\"", path.toStdString());
    }
}
