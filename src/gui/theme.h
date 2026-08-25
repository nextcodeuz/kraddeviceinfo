// krad.device.info — KradDeviceInfo
// Copyright (c) 2026 Krad. Licensed under the MIT License.
// This file is part of the KradDeviceInfo source distribution.
// See the LICENSE file in the project root for the full text.

// krad.device.info - theme manager: dark / light QSS + palette
#pragma once

#include <QString>

namespace krad {
namespace ui {

enum class ThemeKind { Dark, Light };

class Theme {
public:
    static Theme& instance();
    void apply(ThemeKind kind);
    ThemeKind current() const { return kind_; }
    QString accent() const { return accent_; }

private:
    Theme() = default;
    ThemeKind kind_ = ThemeKind::Dark;
    QString accent_ = "#4fc3f7";
};

} // namespace ui
} // namespace krad
