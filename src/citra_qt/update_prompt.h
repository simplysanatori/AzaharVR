// Copyright Azahar for Frame contributors
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

// AzaharVR updatecheck: the "Update Available" box, built apart from the main window so a test can
// make it with a made-up tag.

#pragma once

#include <memory>
#include <QString>

class QMessageBox;
class QWidget;

namespace UpdatePrompt {

/// Upstream's box (title, Information icon, Yes and Ignore): "Update <latest> for <product> is
/// available. You have version <current>. Would you like to download it?". Not shown yet.
std::unique_ptr<QMessageBox> Make(QWidget* parent, const QString& latest, const QString& current,
                                  const QString& product);

} // namespace UpdatePrompt
