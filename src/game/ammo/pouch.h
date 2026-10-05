#pragma once

#include "game/ammo/ammo_data.h"
#include "game/ammo/round.h"

#include <algorithm>
#include <vector>

namespace ghost::game {
template <typename Id>
class Counts {
public:
    explicit Counts(std::size_t kinds = 0) : m_counts(kinds, 0) {}

    int count(Id id) const { return id < m_counts.size() ? m_counts[id] : 0; }
    void add(Id id, int n = 1) {
        if (id >= m_counts.size()) {
            m_counts.resize(static_cast<std::size_t>(id) + 1, 0);
        }
        m_counts[id] += n;
    }
    bool take(Id id) {
        if (count(id) <= 0) {
            return false;
        }
        --m_counts[id];
        return true;
    }
    void set(Id id, int n) {
        add(id, 0);
        m_counts[id] = n;
    }
    int total() const {
        int sum = 0;
        for (int c : m_counts) {
            sum += c;
        }
        return sum;
    }
    std::size_t kinds() const { return m_counts.size(); }

private:
    std::vector<int> m_counts;
};

using AmmoPouch = Counts<ElementId>;
using MaterialInventory = Counts<MaterialId>;

}
