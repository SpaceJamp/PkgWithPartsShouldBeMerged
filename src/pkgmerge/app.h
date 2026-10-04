// SPDX-License-Identifier: GPL-3.0-only
//
// The program: parse, scan, validate, merge, report, and choose an exit code
// (spec sections 13 and 14).

#ifndef PKG_MERGE_APP_H
#define PKG_MERGE_APP_H

#include <vector>

#include "pkgmerge/text.h"

namespace pkgmerge {

/// Exit codes of spec section 13.
inline constexpr int kExitOk = 0;
inline constexpr int kExitNothingMerged = 1;
inline constexpr int kExitPartial = 2;
inline constexpr int kExitCancelled = 130;

/// `arguments` excludes the program name. Never throws and never lets an
/// exception escape.
int run(const std::vector<OsString>& arguments);

}  // namespace pkgmerge

#endif  // PKG_MERGE_APP_H
