// Copyright Azahar for Frame contributors
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

// AzaharVR updatecheck: the "Update Available" box made with a made-up tag, on Qt's offscreen
// platform. Its picture is written to update_prompt_dialog.png in the working directory.

#include <QApplication>
#include <QImage>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QtGlobal>
#include <catch2/catch_test_macros.hpp>
#include "citra_qt/update_prompt.h"

namespace {
QApplication& App() {
    // One QApplication per process, shared with the other Qt tests here (a second is undefined)
    static int argc = 1;
    static char arg0[] = "frame_qt_tests";
    static char* argv[] = {arg0, nullptr};
    if (auto* app = qobject_cast<QApplication*>(QApplication::instance())) {
        return *app; // another test's
    }
    qputenv("QT_QPA_PLATFORM", "offscreen");
    return *new QApplication(argc, argv); // kept to the end, as in offscreen_modal.cpp
}
} // namespace

TEST_CASE("Update prompt: text, buttons and picture", "[update]") {
    App();
    auto box = UpdatePrompt::Make(nullptr, QStringLiteral("v1.4.0"), QStringLiteral("1.3.0"),
                                  QStringLiteral("Azahar VR"));
    CHECK(box->windowTitle() == QStringLiteral("Update Available"));
    CHECK(box->text() == QStringLiteral("Update v1.4.0 for Azahar VR is available.\nYou have "
                                        "version 1.3.0."));
    CHECK(box->icon() == QMessageBox::Information);
    REQUIRE(box->button(QMessageBox::Yes) != nullptr);
    REQUIRE(box->button(QMessageBox::Ignore) != nullptr);
    CHECK(box->button(QMessageBox::Yes)->text() == QStringLiteral("Open GitHub"));
    CHECK(box->button(QMessageBox::Ignore)->text() == QStringLiteral("Not Now"));
    CHECK(box->buttons().size() == 2);

    box->show();
    QCoreApplication::processEvents();
    const QImage picture = box->grab().toImage();
    CHECK(!picture.isNull());
    CHECK(picture.save(QStringLiteral("update_prompt_dialog.png")));
    box->hide();

    // Yes and Ignore, through the modal loop as the main window runs it
    for (const auto which : {QMessageBox::Yes, QMessageBox::Ignore}) {
        QTimer::singleShot(0, box.get(), [&] { box->button(which)->click(); });
        box->exec();
        CHECK(box->clickedButton() == box->button(which));
    }
}
