#include "library/GalleryLayout.h"

#include <QtGlobal>

namespace GalleryLayout {

QList<Row> pack(const QList<double>& ratios, const QStringList& labels, double width,
                double targetHeight, double spacing) {
  QList<Row> rows;
  if (ratios.isEmpty() || width <= 0 || targetHeight <= 0) {
    return rows;
  }
  spacing = qMax(0.0, spacing);
  const bool grouped = labels.size() >= ratios.size();

  Row row;
  double sum = 0;
  bool groupStart = true;
  QString group;

  const auto flush = [&](bool justified) {
    if (row.count == 0) {
      return;
    }
    const double free = width - spacing * (row.count - 1);
    const double fitted = sum > 0 ? free / sum : targetHeight;
    // A row that ends its group early keeps the target height rather than
    // blowing two small pictures up to fill the width.
    row.height = justified ? fitted : qMin(targetHeight, fitted);
    row.justified = justified;
    rows.append(row);
    row = Row{};
    sum = 0;
  };

  for (int i = 0; i < ratios.size(); ++i) {
    const QString label = grouped ? labels.at(i) : QString();
    if (i == 0 || label != group) {
      flush(false);
      group = label;
      groupStart = true;
    }
    if (row.count == 0) {
      row.start = i;
      if (groupStart) {
        row.label = label;
        groupStart = false;
      }
    }
    const double ratio = qBound(kMinimumRatio, ratios.at(i) > 0 ? ratios.at(i) : 1.5,
                                kMaximumRatio);
    row.ratios.append(ratio);
    row.count += 1;
    sum += ratio;
    if ((width - spacing * (row.count - 1)) / sum <= targetHeight) {
      flush(true);
    }
  }
  flush(false);
  return rows;
}

} // namespace GalleryLayout
