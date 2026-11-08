#include "actions/FileMover.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include <sys/stat.h>

using namespace Qt::StringLiterals;

namespace {

// Synthetic bytes only; no real photos in the suite.
QString writeFile(const QString& path, const QByteArray& bytes) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) return {};
  return path;
}

QByteArray readFile(const QString& path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

dev_t deviceOf(const QString& path) {
  struct stat info {};
  return ::stat(QFile::encodeName(path).constData(), &info) == 0 ? info.st_dev : dev_t(0);
}

}  // namespace

class FileMoverTest : public QObject {
  Q_OBJECT

private slots:
  void movesIntoAnotherFolder() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QVERIFY(QDir(root.path()).mkpath(u"a"_s) && QDir(root.path()).mkpath(u"b"_s));
    const QString source = writeFile(root.filePath(u"a/shot.jpg"_s), "first");
    QVERIFY(!source.isEmpty());

    const auto outcome = FileMover::moveInto(source, root.filePath(u"b"_s));
    QVERIFY2(outcome.error.isEmpty(), qPrintable(outcome.error));
    QCOMPARE(outcome.target, QDir(root.filePath(u"b"_s)).canonicalPath() + u"/shot.jpg"_s);
    QVERIFY(!QFileInfo::exists(source));
    QCOMPARE(readFile(outcome.target), QByteArray("first"));
  }

  void neverOverwritesAClashingName() {
    QTemporaryDir root;
    QVERIFY(QDir(root.path()).mkpath(u"a"_s) && QDir(root.path()).mkpath(u"b"_s));
    const QString source = writeFile(root.filePath(u"a/shot.jpg"_s), "incoming");
    QVERIFY(!writeFile(root.filePath(u"b/shot.jpg"_s), "kept").isEmpty());
    QVERIFY(!writeFile(root.filePath(u"b/shot (2).jpg"_s), "kept too").isEmpty());

    const auto outcome = FileMover::moveInto(source, root.filePath(u"b"_s));
    QVERIFY2(outcome.error.isEmpty(), qPrintable(outcome.error));
    QCOMPARE(QFileInfo(outcome.target).fileName(), u"shot (3).jpg"_s);
    QCOMPARE(readFile(root.filePath(u"b/shot.jpg"_s)), QByteArray("kept"));
    QCOMPARE(readFile(root.filePath(u"b/shot (2).jpg"_s)), QByteArray("kept too"));
    QCOMPARE(readFile(outcome.target), QByteArray("incoming"));
  }

  void freeNameHandlesNamesWithoutAnExtension() {
    QTemporaryDir root;
    QVERIFY(!writeFile(root.filePath(u"notes"_s), "x").isEmpty());
    QVERIFY(!writeFile(root.filePath(u".hidden"_s), "x").isEmpty());
    QCOMPARE(QFileInfo(FileMover::freeName(root.path(), u"notes"_s)).fileName(), u"notes (2)"_s);
    QCOMPARE(QFileInfo(FileMover::freeName(root.path(), u".hidden"_s)).fileName(), u".hidden (2)"_s);
    QCOMPARE(QFileInfo(FileMover::freeName(root.path(), u"new.png"_s)).fileName(), u"new.png"_s);
  }

  void leavesAFileAlreadyInTheFolderAlone() {
    QTemporaryDir root;
    const QString source = writeFile(root.filePath(u"shot.jpg"_s), "same");
    const auto outcome = FileMover::moveInto(source, root.path());
    QVERIFY(outcome.error.isEmpty());
    QVERIFY(outcome.target.isEmpty());
    QCOMPARE(readFile(source), QByteArray("same"));
  }

  void reportsMissingFilesAndFolders() {
    QTemporaryDir root;
    QVERIFY(!FileMover::moveInto(root.filePath(u"gone.jpg"_s), root.path()).error.isEmpty());
    const QString source = writeFile(root.filePath(u"shot.jpg"_s), "x");
    QVERIFY(!FileMover::moveInto(source, root.filePath(u"missing"_s)).error.isEmpty());
    QVERIFY(QFileInfo::exists(source));
  }

  // A gvfs SMB mount is another filesystem, where rename() answers EXDEV.
  void copiesVerifiesAndRemovesAcrossFilesystems() {
    QTemporaryDir local;
    QTemporaryDir other(u"/dev/shm/omaroll-mover-XXXXXX"_s);
    if (!other.isValid() || deviceOf(local.path()) == deviceOf(other.path())) {
      QSKIP("No second filesystem to move across");
    }
    QByteArray bytes(3 * 1024 * 1024 + 17, '\0');
    for (int i = 0; i < bytes.size(); ++i) bytes[i] = char((i * 31) & 0xff);
    const QString source = writeFile(local.filePath(u"scan.tif"_s), bytes);
    QVERIFY(!source.isEmpty());
    const QDateTime stamp = QDateTime::fromSecsSinceEpoch(1'600'000'000);
    {
      QFile file(source);
      QVERIFY(file.open(QIODevice::ReadWrite));
      QVERIFY(file.setFileTime(stamp, QFileDevice::FileModificationTime));
    }
    QVERIFY(!writeFile(other.filePath(u"scan.tif"_s), "existing").isEmpty());

    const auto outcome = FileMover::moveInto(source, other.path());
    QVERIFY2(outcome.error.isEmpty(), qPrintable(outcome.error));
    QCOMPARE(QFileInfo(outcome.target).fileName(), u"scan (2).tif"_s);
    QVERIFY(!QFileInfo::exists(source));
    QCOMPARE(readFile(outcome.target), bytes);
    QCOMPARE(readFile(other.filePath(u"scan.tif"_s)), QByteArray("existing"));
    QCOMPARE(QFileInfo(outcome.target).lastModified().toSecsSinceEpoch(), stamp.toSecsSinceEpoch());
  }

  void moveFilesReportsOnTheGuiThread() {
    QTemporaryDir root;
    QVERIFY(QDir(root.path()).mkpath(u"a"_s) && QDir(root.path()).mkpath(u"b"_s));
    const QString one = writeFile(root.filePath(u"a/one.jpg"_s), "1");
    const QString two = writeFile(root.filePath(u"a/two.jpg"_s), "2");
    FileMover mover;
    QSignalSpy finished(&mover, &FileMover::finished);
    QVERIFY(mover.moveFiles({one, two, one, root.filePath(u"a/gone.jpg"_s)}, root.filePath(u"b"_s)));
    QVERIFY(mover.busy());
    QVERIFY(!mover.moveFiles({two}, root.filePath(u"b"_s)));
    QVERIFY(finished.wait(5000));
    QVERIFY(!mover.busy());
    const QVariantList moved = finished.at(0).at(1).toList();
    const QVariantList failed = finished.at(0).at(2).toList();
    QCOMPARE(moved.size(), 2);
    QCOMPARE(failed.size(), 1);
    QCOMPARE(moved.at(0).toMap().value(u"from"_s).toString(), one);
    QVERIFY(QFileInfo::exists(root.filePath(u"b/one.jpg"_s)));
    QVERIFY(QFileInfo::exists(root.filePath(u"b/two.jpg"_s)));
  }

  void localFilesKeepsOnlyLocalUrls() {
    FileMover mover;
    const QStringList paths = mover.localFiles(
        {QUrl::fromLocalFile(u"/tmp/a b/c#1.jpg"_s), QUrl(u"https://example.com/x.jpg"_s)});
    QCOMPARE(paths, QStringList{u"/tmp/a b/c#1.jpg"_s});
  }
};

QTEST_GUILESS_MAIN(FileMoverTest)
#include "tst_file_mover.moc"
