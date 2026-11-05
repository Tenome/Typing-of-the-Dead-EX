#include <string.h>
#include "unit_costs.h"

#define SMALL_TSU_UNIT 203
#define UNKNOWN_KEY_COST 4
#define MIN_DIFFICULTY 1
#define MAX_DIFFICULTY 0xFFFF

static const unit_cost *find_unit(unsigned short id) {
    int low = 0, high = unit_cost_count - 1;
    while (low <= high) {
        int middle = (low + high) / 2;
        if (unit_costs[middle].id == id) return &unit_costs[middle];
        if (unit_costs[middle].id < id) low = middle + 1;
        else high = middle - 1;
    }
    return NULL;
}

static int cost_of_key(char key) {
    for (int i = 0; i < key_cost_count; i++) {
        if (key_costs[i].key == key) return key_costs[i].cost;
    }
    return UNKNOWN_KEY_COST;
}

static int cost_of_spelling(const char *spelling) {
    int cost = 0;
    for (; *spelling; spelling++) cost += cost_of_key(*spelling);
    return cost;
}

static int is_doubled_consonant(char key) {
    return key >= 'A' && key <= 'Z' && !strchr("AIUEON", key);
}

int word_difficulty(const unsigned short *units, int count) {
    int difficulty = 0;
    for (int i = 0; i < count; i++) {
        const unit_cost *unit = find_unit(units[i]);
        if (!unit) {
            difficulty += UNKNOWN_KEY_COST;
            continue;
        }
        const unit_cost *next = i + 1 < count ? find_unit(units[i + 1]) : NULL;
        if (units[i] == SMALL_TSU_UNIT && next && is_doubled_consonant(next->spelling[0])) {
            difficulty += cost_of_key(next->spelling[0]);
        } else {
            difficulty += cost_of_spelling(unit->spelling) + unit->correction;
        }
    }
    if (difficulty < MIN_DIFFICULTY) difficulty = MIN_DIFFICULTY;
    return difficulty > MAX_DIFFICULTY ? MAX_DIFFICULTY : difficulty;
}
