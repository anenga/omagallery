#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>

// Moves files into another library folder when they are dropped on the
// sidebar. Nothing is ever overwritten: a clashing name gets " (2)", " (3)"
// and so on. A move onto another filesystem, such as a gvfs SMB mount, cannot
// be a rename, so the file is copied, read back and compared, and only then is
// the original removed. The work runs off the GUI thread because a copy over
// the network can take a while.
class FileMover final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
  struct Outcome {
    QString source;
    QString target;  // Empty when the file stayed where it was.
    QString error;   // Empty on success, or when the file was already in the folder.
  };

  using QObject::QObject;

  [[nodiscard]] bool busy() const { return m_busy; }

  // The first name in folder that nothing uses yet: name.ext, name (2).ext...
  [[nodiscard]] static QString freeName(const QString& folder, const QString& fileName);

  // Move one file or symlink into folder. Blocking; the worker calls this.
  [[nodiscard]] static Outcome moveInto(const QString& source, const QString& folder);

  // Start moving paths into folder. finished() reports what happened.
  // False when a move is already running or the folder is unusable.
  Q_INVOKABLE bool moveFiles(const QStringList& paths, const QString& folder);

  // Local file paths from a drop's URLs, so QML need not decode them.
  Q_INVOKABLE [[nodiscard]] QStringList localFiles(const QList<QUrl>& urls) const;

signals:
  void busyChanged();
  // moved holds {from, to} maps. failed holds {path, error} maps.
  void finished(const QString& folder, const QVariantList& moved, const QVariantList& failed);

private:
  bool m_busy = false;
};
