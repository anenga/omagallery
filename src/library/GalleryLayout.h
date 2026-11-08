#pragma once

#include <QList>
#include <QRectF>
#include <QString>
#include <QStringList>

// Lays pictures out as masonry, the layout Pinterest uses: equal-width
// columns, each picture at its own height so the whole of it shows, placed
// into whichever column is shortest. A new group (a day, or a match set)
// starts below every column of the one before, under its own header.
//
// Because each picture goes into the shortest column, the tops of the
// pictures never decrease in model order. The view relies on that to find
// what is on screen with a binary search.
namespace GalleryLayout {

struct Header {
  double y = 0;
  QString label;
};

struct Masonry {
  int columns = 0;
  double columnWidth = 0;
  double height = 0;
  QList<QRectF> rects;
  QList<Header> headers;
  // The tallest picture, so the view knows how far above the screen a
  // visible picture can start.
  double tallest = 0;
};

// A strip one fifth as tall as it is wide is as tall as a picture gets; one
// taller still shows whole, letterboxed inside that height.
inline constexpr double kMinimumRatio = 0.2;
inline constexpr double kMaximumRatio = 8.0;

// ratios: width / height of each item, in model order.
// labels: the group of each item; empty or shorter than ratios means one
// group with no header.
[[nodiscard]] Masonry masonry(const QList<double>& ratios, const QStringList& labels,
                              double width, double targetColumnWidth, double spacing,
                              double headerHeight);

} // namespace GalleryLayout
