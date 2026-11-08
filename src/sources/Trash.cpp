#include "sources/Trash.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUrl>

#include <unistd.h>

namespace {

constexpr qint64 kChunk = 1 << 20;

QByteArray hashOf(QFile& file) {
  QCryptographicHash hash(QCryptographicHash::Sha256);
  if (!file.seek(0)) return {};
  while (!file.atEnd()) {
    const QByteArray chunk = file.read(kChunk);
    if (chunk.isEmpty() && file.error() != QFileDevice::NoError) return {};
    hash.addData(chunk);
  }
  return hash.result();
}

// Copies |from| into a new file |to|, returning the hash of what was read,
// or empty on failure. The copy is flushed to disk before returning.
QByteArray copyHashed(const QString& from, const QString& to) {
  QFile source(from);
  QFile target(to);
  if (!source.open(QIODevice::ReadOnly)) return {};
  if (!target.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return {};
  QCryptographicHash hash(QCryptographicHash::Sha256);
  while (!source.atEnd()) {
    const QByteArray chunk = source.read(kChunk);
    if (chunk.isEmpty() && source.error() != QFileDevice::NoError) return {};
    hash.addData(chunk);
    if (target.write(chunk) != chunk.size()) return {};
  }
  if (!target.flush() || ::fsync(target.handle()) != 0) return {};
  return hash.result();
}

bool fail(QString* error, const QString& message) {
  if (error) *error = message;
  return false;
}

// Into the home Trash by copying, for a volume that has no Trash.
bool copyToHomeTrash(const QString& path, QString* error) {
  const QFileInfo info(path);
  const QString name = info.fileName();
  const QString trash = Trash::homeTrash();
  const QString filesDir = trash + QStringLiteral("/files");
  const QString infoDir = trash + QStringLiteral("/info");
  if (!QDir().mkpath(filesDir) || !QDir().mkpath(infoDir)) {
    return fail(error, QStringLiteral("Could not open your Trash folder"));
  }

  // Claim a name: the .trashinfo is created first and exclusively, as the
  // Trash specification asks, so two moves never take the same one.
  QString entry;
  QFile record;
  const QString stem = info.completeBaseName();
  const QString suffix = info.suffix().isEmpty() ? QString() : QLatin1Char('.') + info.suffix();
  for (int n = 1; n < 1000 && entry.isEmpty(); ++n) {
    const QString candidate = n == 1 ? name : QStringLiteral("%1.%2%3").arg(stem).arg(n).arg(suffix);
    if (QFileInfo::exists(filesDir + QLatin1Char('/') + candidate)) continue;
    record.setFileName(infoDir + QLatin1Char('/') + candidate + QStringLiteral(".trashinfo"));
    if (record.open(QIODevice::WriteOnly | QIODevice::NewOnly)) entry = candidate;
  }
  if (entry.isEmpty()) return fail(error, QStringLiteral("Could not make room in your Trash"));
  const QString recordPath = record.fileName();
  const QString copy = filesDir + QLatin1Char('/') + entry;
  const auto undo = [&] {
    QFile::remove(copy);
    QFile::remove(recordPath);
  };

  const QByteArray text = "[Trash Info]\nPath=" +
                          QUrl::toPercentEncoding(info.absoluteFilePath(), "/") +
                          "\nDeletionDate=" +
                          QDateTime::currentDateTime().toString(Qt::ISODate).left(19).toUtf8() + "\n";
  const bool recorded = record.write(text) == text.size() && record.flush();
  record.close();
  if (!recorded) {
    undo();
    return fail(error, QStringLiteral("Could not write to your Trash"));
  }

  // Nothing is removed from where it was until the copy in the Trash is
  // read back and matches what was read from the original, byte for byte.
  const qint64 size = info.size();
  const QByteArray read = copyHashed(path, copy);
  bool verified = !read.isEmpty() && QFileInfo(copy).size() == size;
  if (verified) {
    QFile check(copy);
    verified = check.open(QIODevice::ReadOnly) && hashOf(check) == read;
  }
  if (!verified) {
    undo();
    return fail(error, QStringLiteral("Could not copy %1 into your Trash safely, so it was left where it is")
                           .arg(name));
  }
  if (const QDateTime modified = info.lastModified(); modified.isValid()) {
    QFile stamped(copy);
    if (stamped.open(QIODevice::ReadWrite)) stamped.setFileTime(modified, QFileDevice::FileModificationTime);
  }
  if (!QFile::remove(path)) {
    undo();
    return fail(error, QStringLiteral("Could not remove %1 from its folder, so it was left there").arg(name));
  }
  return true;
}

} // namespace

namespace Trash {

QString homeTrash() {
  const QString overridden = qEnvironmentVariable("OMAROLL_HOME_TRASH");
  if (!overridden.isEmpty()) return overridden;
  return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/Trash");
}

QString problem(const QString& path) {
  const QFileInfo info(path);
  if (!info.exists() && !info.isSymLink()) return QStringLiteral("That file is no longer there");
  const QFileInfo folder(info.absolutePath());
  if (!folder.isWritable()) {
    return QStringLiteral("%1 is in a folder you can't change, so it can't be moved to Trash")
        .arg(info.fileName());
  }
  return {};
}

bool move(const QString& path, QString* error) {
  if (const QString why = problem(path); !why.isEmpty()) return fail(error, why);
  // Qt makes Trash/files and Trash/info, but expects the XDG data-home
  // parent to exist; a fresh account may not have used it yet.
  QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation));
  if (qEnvironmentVariableIsEmpty("OMAROLL_HOME_TRASH") && QFile::moveToTrash(path)) return true;
  const QFileInfo info(path);
  if (!info.isFile() || info.isSymLink()) {
    return fail(error, QStringLiteral("Could not move this to Trash. Its volume may not support Trash"));
  }
  return copyToHomeTrash(path, error);
}

} // namespace Trash
