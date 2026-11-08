#pragma once

#include <QString>

// Moving files to the Trash, including from volumes that have no Trash of
// their own.
//
// QFile::moveToTrash needs a trash folder on the file's own volume. A
// network share mounted through gvfs often has a read-only root, so none can
// be made there. Then the file is copied into the home Trash with its
// .trashinfo (so the file manager can restore it to where it was), the copy
// is read back and checked against the original byte for byte, and only
// then is the original removed. On any doubt the original is left alone.
namespace Trash {

// Why |path| cannot be moved to Trash, in the user's words, or empty.
// Cheap: checks only that it exists and that its folder can be written.
[[nodiscard]] QString problem(const QString& path);

// Moves |path| to Trash. On failure returns false and sets |error|; the
// original is then still where it was.
bool move(const QString& path, QString* error = nullptr);

// The home Trash, used when the file's volume has none. Overridable for
// tests through OMAROLL_HOME_TRASH.
[[nodiscard]] QString homeTrash();

} // namespace Trash
