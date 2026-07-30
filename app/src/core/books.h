#pragma once

#include <QString>

namespace milah {

/// Orders books by their canonical position, falling back to a plain string
/// comparison for anything outside the canon.
int compareBooks(const QString &left, const QString &right);

/// Natural-order comparison, so that verse "10" sorts after verse "9". Kept
/// locale-independent on purpose: exported OSIS must not depend on the
/// machine's regional settings.
int compareNumericAware(const QString &left, const QString &right);

} // namespace milah
