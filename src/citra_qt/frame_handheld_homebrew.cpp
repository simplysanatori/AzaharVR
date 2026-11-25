// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharVR homebrew: see frame_handheld_homebrew.h.

#include "citra_qt/frame_handheld_homebrew.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <thread>
#include <QApplication>
#include <QFileInfo>
#include <QMessageBox>
#include <QMetaObject>
#include <QPointer>
#include <QPushButton>
#include "common/logging/log.h"
#include "frame_cores/utf8_path.h"

namespace H = Homebrew::Handheld;

namespace {

/// Runs `work` on a thread of its own and `done(result)` on the GUI thread (while `self` lives).
template <typename Work, typename Done>
void OffThread(QObject* self, Work work, Done done) {
    QPointer<QObject> guard(self);
    std::thread([guard, work = std::move(work), done = std::move(done)]() mutable {
        auto result = work();
        QMetaObject::invokeMethod(
            qApp,
            [guard, done = std::move(done), result = std::move(result)]() mutable {
                if (guard) {
                    done(std::move(result));
                }
            },
            Qt::QueuedConnection);
    }).detach();
}

QString Q(const std::string& s) {
    return QString::fromStdString(s);
}

} // namespace

FrameHandheldHomebrew::FrameHandheldHomebrew(H::Entry entry_, std::string root_, QObject* parent)
    : HomebrewPackager(package, parent), entry(std::move(entry_)), root(std::move(root_)) {
    package.id = entry.slug;
    package.title = entry.title;
    package.author = entry.author;
    package.site = entry.page;
    Rescan();
}

HomebrewPackager::Status FrameHandheldHomebrew::GetStatus() const {
    Status s;
    s.installed = present && !broken;
    s.busy = busy;
    s.progress = progress;
    s.status = status;
    // "Author · MIT · GBA": the licence the list gives and the system, beside the author
    s.detail = Q(entry.licence + " · ");
    for (const char c : H::SystemName(entry.system)) {
        s.detail += QChar(static_cast<char16_t>(std::toupper(static_cast<unsigned char>(c))));
    }
    if (s.installed && verified) {
        s.game_path = Q(H::EntryPath(root, entry));
    }
    s.repair = broken;
    s.can_uninstall = present || broken;
    return s;
}

void FrameHandheldHomebrew::SetBusy(bool on, const QString& text, int value) {
    busy = on;
    status = text;
    progress = value;
    emit Changed();
}

void FrameHandheldHomebrew::Rescan() {
    if (busy || checking) {
        return;
    }
    const std::string path = H::EntryPath(root, entry);
    std::error_code ec;
    const bool is_file =
        !path.empty() && std::filesystem::is_regular_file(FrameCores::FsPath(path), ec);
    present = is_file && std::filesystem::file_size(FrameCores::FsPath(path), ec) == entry.size &&
              !ec;
    broken = is_file && !present;
    verified = false;
    if (present) {
        // The checksum is read off the GUI thread; Play waits for it.
        checking = true;
        OffThread(
            this, [entry = entry, path] { return H::VerifyFile(path, entry); },
            [this](const std::string& error) {
                checking = false;
                verified = error.empty();
                broken = !error.empty();
                status = Q(error);
                if (!error.empty()) {
                    LOG_WARNING(Frontend, "homebrew: {}: {}", entry.slug, error);
                }
                emit Changed();
            });
    }
}

void FrameHandheldHomebrew::Install(QWidget* /*parent*/) {
    if (busy || checking) {
        return;
    }
    SetBusy(true, tr("Downloading"), 0);
    LOG_INFO(Frontend, "homebrew: {}: download from {}", entry.slug, entry.url);
    QPointer<FrameHandheldHomebrew> self(this);
    auto last = std::make_shared<std::atomic<int>>(-100);
    OffThread(
        this,
        [entry = entry, root = root, self, last] {
            return H::Install(root, entry, [self, last](u64 done, u64 total) {
                const int permille =
                    total == 0 ? -1 : static_cast<int>(std::min<u64>(done * 1000 / total, 1000));
                if (std::abs(permille - last->load()) >= 10) { // about a hundred updates
                    last->store(permille);
                    QMetaObject::invokeMethod(
                        qApp,
                        [self, permille] {
                            if (self && self->busy) {
                                self->progress = permille;
                                emit self->Changed();
                            }
                        },
                        Qt::QueuedConnection);
                }
                return true;
            });
        },
        [this](const std::string& error) {
            if (!error.empty()) {
                LOG_WARNING(Frontend, "homebrew: {}: {}", entry.slug, error);
            }
            busy = false;
            progress = -1;
            status = Q(error);
            if (error.empty()) { // size and SHA-256 matched before the file was put in place
                present = true;
                broken = false;
                verified = true;
                emit Changed();
            } else {
                Rescan(); // what was there before, if anything
                emit Changed();
            }
        });
}

bool FrameHandheldHomebrew::ApplyBootSettings(const QString& path) {
    // No settings of its own; only says whether the booted file is this entry's
    const QString mine = Q(H::EntryPath(root, entry));
    return !mine.isEmpty() && QFileInfo(path).canonicalFilePath() == QFileInfo(mine).canonicalFilePath();
}

void FrameHandheldHomebrew::Uninstall(QWidget* parent) {
    if (busy) {
        return;
    }
    Rescan();
    if (!GetStatus().can_uninstall) {
        return;
    }
    QMessageBox box(QMessageBox::NoIcon, tr("Uninstall %1?").arg(Q(entry.title)), QString{},
                    QMessageBox::NoButton, parent);
    QPushButton* cancel = box.addButton(tr("Cancel"), QMessageBox::RejectRole);
    QPushButton* go = box.addButton(tr("Uninstall"), QMessageBox::AcceptRole);
    box.setDefaultButton(cancel);
    box.setEscapeButton(cancel);
    box.exec();
    if (box.clickedButton() != go) {
        return;
    }
    UninstallNow(false, [](const QString&) {});
}

void FrameHandheldHomebrew::UninstallNow(bool /*delete_save*/, std::function<void(QString)> done) {
    if (busy) {
        return;
    }
    const std::string error = H::Remove(root, entry);
    present = false;
    broken = false;
    verified = false;
    status = Q(error);
    emit Changed();
    done(Q(error));
}
