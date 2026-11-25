// Copyright Azahar for Frame contributors
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#include <QCoreApplication>
#include <QMessageBox>
#include "citra_qt/update_prompt.h"

namespace UpdatePrompt {

std::unique_ptr<QMessageBox> Make(QWidget* parent, const QString& latest, const QString& current,
                                  const QString& product) {
    auto box = std::make_unique<QMessageBox>(parent);
    box->setWindowTitle(QCoreApplication::translate("GMainWindow", "Update Available"));
    box->setIcon(QMessageBox::Information);
    box->addButton(QMessageBox::Yes);
    box->addButton(QMessageBox::Ignore);
    box->setText(QCoreApplication::translate("GMainWindow",
                                             "Update %1 for %2 is available.\nYou have version "
                                             "%3.\nWould you like to download it?")
                     .arg(latest, product, current));
    return box;
}

} // namespace UpdatePrompt
