#include "core/bands.h"

namespace milah {

QList<Band> packBands(const QList<int> &widths, int room, int spacing)
{
    QList<Band> bands;
    int start = 0;
    int used = 0;
    for (int index = 0; index < widths.size(); ++index) {
        const int required = widths.at(index) + (index > start ? spacing : 0);
        // `index > start` is what guarantees a band is never empty: the first
        // column of a band is taken whatever it costs, and only the ones after
        // it can be pushed to the next band.
        if (index > start && used + required > room) {
            bands.append(Band{start, index});
            start = index;
            used = widths.at(index);
        } else {
            used += required;
        }
    }
    bands.append(Band{start, int(widths.size())});
    return bands;
}

} // namespace milah
