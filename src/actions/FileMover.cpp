#include "actions/FileMover.h"
#include "sources/FileVersion.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QSet>
#include <QtConcurrent>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace Qt::StringLiterals;

namespace {

bool entryExists(const QString& path) {
  struct stat info {};
  return ::lstat(QFile::encodeName(path).constData(), &info) == 0;
}

QString errorText(int error) { return QString::fromLocal8Bit(std::strerror(error)); }

constexpr qint64 kChunk = 1024 * 1024;

// Read both files back and compare them byte for byte.
bool sameContents(const QString& first, const QString& second) {
  QFile a(first), b(second);
  if (!a.open(QIODevice::ReadOnly) || !b.open(QIODevice::ReadOnly) || a.size() != b.size()) return false;
  while (!a.atEnd()) {
    const QByteArray left = a.read(kChunk);
    const QByteArray right = b.read(left.size());
    if (left.isEmpty() || left != right || a.error() != QFileDevice::NoError ||
        b.error() != QFileDevice::NoError) return false;
  }
  return b.atEnd();
}

enum class CopyResult { Copied, NameTaken, Failed };

// Copy into a file that must not exist yet, then check it. Removes its own
// partial output on failure; never touches a file it did not create.
CopyResult copyVerified(const QString& source, const QString& target, QString* error) {
  QFile in(source);
  if (!in.open(QIODevice::ReadOnly)) {
    *error = u"Could not read %1"_s.arg(QFileInfo(source).fileName());
    return CopyResult::Failed;
  }
  QFile out(target);
  if (!out.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
    if (entryExists(target)) return CopyResult::NameTaken;
    *error = u"Could not write in that folder: %1"_s.arg(out.errorString());
    return CopyResult::Failed;
  }
  const auto fail = [&](const QString& message) {
    out.close();
    QFile::remove(target);
    *error = message;
    return CopyResult::Failed;
  };
  while (!in.atEnd()) {
    const QByteArray chunk = in.read(kChunk);
    if (in.error() != QFileDevice::NoError) return fail(u"Could not read %1"_s.arg(in.fileName()));
    if (out.write(chunk) != chunk.size()) return fail(u"Could not finish the copy: %1"_s.arg(out.errorString()));
  }
  if (!out.flush()) return fail(u"Could not finish the copy: %1"_s.arg(out.errorString()));
  // Some network filesystems do not implement fsync; the read-back still checks.
  ::fsync(out.handle());
  // Keep the capture time the library sorts by. Best effort on network shares.
  out.setFileTime(in.fileTime(QFileDevice::FileModificationTime), QFileDevice::FileModificationTime);
  out.setPermissions(in.permissions());
  out.close();
  in.close();
  if (!sameContents(source, target)) return fail(u"The copy did not match the original, so it was removed"_s);
  return CopyResult::Copied;
}

}  // namespace

QString FileMover::freeName(const QString& folder, const QString& fileName) {
  const QDir dir(folder);
  QString candidate = dir.filePath(fileName);
  if (!entryExists(candidate)) return candidate;
  const QFileInfo info(fileName);
  // A dotfile has no base name to number; number the whole name instead.
  const QString base = info.completeBaseName().isEmpty() ? fileName : info.completeBaseName();
  const QString suffix = info.completeBaseName().isEmpty() || info.suffix().isEmpty()
                             ? QString() : QLatin1Char('.') + info.suffix();
  for (int number = 2;; ++number) {
    candidate = dir.filePath(u"%1 (%2)%3"_s.arg(base).arg(number).arg(suffix));
    if (!entryExists(candidate)) return candidate;
  }
}

FileMover::Outcome FileMover::moveInto(const QString& source, const QString& folder) {
  Outcome outcome{source, {}, {}};
  const QFileInfo from(source);
  struct stat entry {};
  if (::lstat(QFile::encodeName(source).constData(), &entry) != 0) {
    outcome.error = u"%1 is no longer there"_s.arg(from.fileName());
    return outcome;
  }
  const bool link = S_ISLNK(entry.st_mode);
  if (!S_ISREG(entry.st_mode) && !link) {
    outcome.error = u"Only files can be moved"_s;
    return outcome;
  }
  const QFileInfo destination(folder);
  if (!destination.isDir()) {
    outcome.error = u"That folder is unavailable"_s;
    return outcome;
  }
  const QString folderPath = destination.canonicalFilePath();
  if (from.dir().canonicalPath() == folderPath) return outcome;  // Already there.

  const QByteArray sourceName = QFile::encodeName(from.absoluteFilePath());
  const QString version = FileVersion::key(source, false);
  // Each pass picks a free name; another writer taking it sends us round again.
  for (int attempt = 0; attempt < 64; ++attempt) {
    const QString target = freeName(folderPath, from.fileName());
    const QByteArray targetName = QFile::encodeName(target);
    int result = ::renameat2(AT_FDCWD, sourceName.constData(), AT_FDCWD, targetName.constData(),
                             RENAME_NOREPLACE);
    if (result != 0 && (errno == EINVAL || errno == ENOSYS || errno == ENOTSUP)) {
      // FUSE filesystems such as gvfs may not offer no-replace renames. The
      // name was free a moment ago; check once more right before renaming.
      if (entryExists(target)) continue;
      result = ::rename(sourceName.constData(), targetName.constData());
    }
    if (result == 0) {
      outcome.target = target;
      return outcome;
    }
    if (errno == EEXIST) continue;
    if (errno != EXDEV) {
      outcome.error = u"Could not move %1: %2"_s.arg(from.fileName(), errorText(errno));
      return outcome;
    }

    // Another filesystem: copy, read back, compare, then remove the original.
    if (link) {
      outcome.error = u"%1 is a link and cannot be moved to another drive"_s.arg(from.fileName());
      return outcome;
    }
    QString error;
    const CopyResult copied = copyVerified(from.absoluteFilePath(), target, &error);
    if (copied == CopyResult::NameTaken) continue;
    if (copied == CopyResult::Failed) {
      outcome.error = error;
      return outcome;
    }
    if (FileVersion::key(source, false) != version) {
      QFile::remove(target);
      outcome.error = u"%1 changed while it was being copied. It was left where it was"_s.arg(from.fileName());
      return outcome;
    }
    if (::unlink(sourceName.constData()) != 0) {
      const int failure = errno;
      QFile::remove(target);
      outcome.error = u"Could not remove the original %1 (%2), so the copy was removed"_s
                          .arg(from.fileName(), errorText(failure));
      return outcome;
    }
    outcome.target = target;
    return outcome;
  }
  outcome.error = u"Could not find a free name for %1"_s.arg(from.fileName());
  return outcome;
}

bool FileMover::moveFiles(const QStringList& paths, const QString& folder) {
  if (m_busy) return false;
  if (!QFileInfo(folder).isDir()) {
    emit finished(folder, {}, {QVariantMap{{u"path"_s, folder}, {u"error"_s, u"That folder is unavailable"_s}}});
    return false;
  }
  QStringList unique;
  QSet<QString> seen;
  for (const QString& path : paths) {
    if (!path.isEmpty() && !seen.contains(path)) {
      seen.insert(path);
      unique.append(path);
    }
  }
  m_busy = true;
  emit busyChanged();
  auto* watcher = new QFutureWatcher<QList<Outcome>>(this);
  connect(watcher, &QFutureWatcher<QList<Outcome>>::finished, this, [this, watcher, folder] {
    const QList<Outcome> outcomes = watcher->result();
    watcher->deleteLater();
    QVariantList moved, failed;
    for (const Outcome& outcome : outcomes) {
      if (!outcome.error.isEmpty()) {
        failed.append(QVariantMap{{u"path"_s, outcome.source}, {u"error"_s, outcome.error}});
      } else if (!outcome.target.isEmpty()) {
        moved.append(QVariantMap{{u"from"_s, outcome.source}, {u"to"_s, outcome.target}});
      }
    }
    m_busy = false;
    emit busyChanged();
    emit finished(folder, moved, failed);
  });
  watcher->setFuture(QtConcurrent::run([unique, folder] {
    QList<Outcome> outcomes;
    for (const QString& path : unique) outcomes.append(moveInto(path, folder));
    return outcomes;
  }));
  return true;
}

QStringList FileMover::localFiles(const QList<QUrl>& urls) const {
  QStringList paths;
  for (const QUrl& url : urls) {
    if (url.isLocalFile()) paths.append(QDir::cleanPath(url.toLocalFile()));
  }
  return paths;
}
