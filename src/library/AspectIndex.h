#pragma once

#include <QFutureWatcher>
#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <atomic>
#include <memory>

// The shape of each picture, for the gallery's justified rows.
//
// Only image headers are read, never pixels, and always off the GUI thread.
// Until a file has been read its kind's usual shape stands in, and the
// gallery relayouts once a batch of real shapes lands.
class AspectIndex final : public QObject {
  Q_OBJECT
  Q_PROPERTY(int revision READ revision NOTIFY ratiosChanged)

public:
  explicit AspectIndex(QObject* parent = nullptr);
  ~AspectIndex() override;
  void requestStop();

  [[nodiscard]] int revision() const { return m_revision; }

  // Width over height. Unknown images are queued for reading. The stamp is
  // the file's mtime, so a rotated or rewritten picture is read again.
  [[nodiscard]] double ratio(const QString& path, qint64 stamp, bool video, bool document);

  // What an image header says, with EXIF rotation applied. 0 when unreadable.
  [[nodiscard]] static double readRatio(const QString& path);

  static constexpr double kPhotoRatio = 1.5;
  static constexpr double kVideoRatio = 16.0 / 9.0;
  static constexpr double kDocumentRatio = 0.7071;

signals:
  void ratiosChanged();

private:
  void startBatch();
  void batchFinished();

  QHash<QString, double> m_ratios;
  // Keyed by path and stamp; the value is the path to read.
  QHash<QString, QString> m_pending;
  QSet<QString> m_inFlight;
  QFutureWatcher<QList<double>> m_watcher;
  QStringList m_batch;
  QTimer m_kick;
  std::shared_ptr<std::atomic_bool> m_stop = std::make_shared<std::atomic_bool>(false);
  int m_revision = 0;
};
