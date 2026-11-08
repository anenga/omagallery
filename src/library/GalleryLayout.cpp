#include "library/GalleryLayout.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace GalleryLayout {

Masonry masonry(const QList<double>& ratios, const QStringList& labels, double width,
                double targetColumnWidth, double spacing, double headerHeight) {
  Masonry out;
  if (ratios.isEmpty() || width <= 0 || targetColumnWidth <= 0) {
    return out;
  }
  spacing = qMax(0.0, spacing);
  out.columns = qMax(1, int(std::floor((width + spacing) / (targetColumnWidth + spacing))));
  out.columnWidth = (width - spacing * (out.columns - 1)) / out.columns;
  out.rects.reserve(ratios.size());

  const bool grouped = labels.size() >= ratios.size();
  QList<double> columns(out.columns, 0.0);
  double top = 0;
  QString group;

  for (int i = 0; i < ratios.size(); ++i) {
    if (grouped && (i == 0 || labels.at(i) != group)) {
      group = labels.at(i);
      // Below every column of the previous group, then the header.
      top = *std::max_element(columns.cbegin(), columns.cend());
      if (i > 0) {
        top += spacing * 4;
      }
      out.headers.append({top, group});
      top += headerHeight;
      std::fill(columns.begin(), columns.end(), top);
    }
    const double ratio = qBound(kMinimumRatio, ratios.at(i) > 0 ? ratios.at(i) : 1.5,
                                kMaximumRatio);
    const int column = int(std::min_element(columns.cbegin(), columns.cend()) - columns.cbegin());
    const double height = std::round(out.columnWidth / ratio);
    const double x = column * (out.columnWidth + spacing);
    out.rects.append(QRectF(x, columns[column], out.columnWidth, height));
    out.tallest = qMax(out.tallest, height);
    columns[column] += height + spacing;
  }
  out.height = qMax(0.0, *std::max_element(columns.cbegin(), columns.cend()) - spacing);
  return out;
}

} // namespace GalleryLayout
