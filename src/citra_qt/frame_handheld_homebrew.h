// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharVR homebrew: one entry of the DS / GBA / Game Boy list (core/homebrew/handheld_catalogue.h)
// as a package of the Homebrew tab and menu. Install downloads the author's file (only when pressed),
// checks its size and SHA-256 and keeps it in the user folder; Play starts it, once it has been
// checked; Uninstall removes it. The list is read by FrameHomebrew (frame_homebrew.cpp).

#pragma once

#include <string>
#include "citra_qt/frame_homebrew.h"
#include "core/homebrew/handheld_catalogue.h"

class FrameHandheldHomebrew : public HomebrewPackager {
    Q_OBJECT
public:
    /// `root`: the folder the entries are kept in (<user folder>/homebrew/handheld).
    FrameHandheldHomebrew(Homebrew::Handheld::Entry entry, std::string root, QObject* parent);

    Status GetStatus() const override;
    void Install(QWidget* parent) override;
    void Update(QWidget* parent) override {
        Install(parent);
    }
    void CheckForUpdate() override {}
    void CheckNow(std::function<void(Homebrew::CheckResult)> done) override {
        done(Homebrew::CheckResult::NoUpdate);
    }
    bool SupportsCheck() const override {
        return false;
    }
    bool ApplyBootSettings(const QString& path) override;
    void Rescan() override;
    void Uninstall(QWidget* parent) override;
    void UninstallNow(bool delete_save, std::function<void(QString)> done) override;

private:
    void SetBusy(bool on, const QString& text, int progress);

    Homebrew::Handheld::Entry entry;
    std::string root;
    Homebrew::PackageInfo package; ///< views into `entry`
    bool busy = false;
    int progress = -1;
    QString status;        ///< while busy, or the last error
    bool present = false;  ///< the file is there with the listed size
    bool verified = false; ///< and its SHA-256 matched (Play only starts a verified file)
    bool broken = false;   ///< it is there but is not the listed file
    bool checking = false; ///< being checked off the GUI thread
};
