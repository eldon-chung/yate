#pragma once

#include <stddef.h>

struct Point {
    size_t row;
    size_t col;

    friend auto operator<=>(Point const &a, Point const &b) = default;

    Point operator+(Point const &other) const {
        return Point{row + other.row, col + other.col};
    }
};
