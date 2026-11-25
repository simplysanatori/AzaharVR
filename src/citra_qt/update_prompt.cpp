// Copyright Azahar for Frame contributors
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#include <QCoreApplication>
#include <QMessageBox>
#include <QPushButton>
#include "citra_qt/update_prompt.h"

namespace UpdatePrompt {

std::unique_ptr<QMessageBox> Make(QWidget* parent, const QString& latest, const QString& current,
                                  const QString& product) {
    auto box = std::make_unique<QMessageBox>(parent);
    box->setWindowTitle(QCoreApplication::translate("GMainWindow", "Update Available"));
    box->setIcon(QMessageBox::Information);
    box->addButton(QMessageBox::Yes)->setText(
        QCoreApplication::translate("GMainWindow", "Open GitHub"));
    box->addButton(QMessageBox::Ignore)
        ->setText(QCoreApplication::translate("GMainWindow", "Not Now"));
    box->setText(QCoreApplication::translate("GMainWindow",
                                             "Update %1 for %2 is available.\nYou have version "
                                             "%3.")
                     .arg(latest, product, current));
    return box;
}

} // namespace UpdatePrompt
