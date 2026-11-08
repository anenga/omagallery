#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// Packs pictures into justified rows, the layout photo apps use for a
// gallery: every picture keeps its own shape, and each row is scaled so its
// pictures exactly fill the width. A new group (a day, or a match set) always
// starts a new row so it can carry its own header.
namespace GalleryLayout {

struct Row {
  int start = 0;
  int count = 0;
  double height = 0;
  // False for the short last row of a group, which stays left-aligned.
  bool justified = false;
  // Set on the first row of a group only, so the header is drawn once.
  QString label;
  QList<double> ratios;
};

// Very wide panoramas and very tall strips would otherwise take a row to
// themselves or shrink to a sliver.
inline constexpr double kMinimumRatio = 0.4;
inline constexpr double kMaximumRatio = 3.2;

// ratios: width / height of each item, in model order.
// labels: the group of each item; empty or shorter than ratios means one group.
[[nodiscard]] QList<Row> pack(const QList<double>& ratios, const QStringList& labels,
                              double width, double targetHeight, double spacing);

} // namespace GalleryLayout
