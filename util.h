#pragma once

#include <stddef.h>

#include <compare>
#include <iostream>

struct Point {

    size_t row;
    size_t col;

    Point()
        : row(0),
          col(0) {
    }

    Point(size_t r, size_t c)
        : row(r),
          col(c) {
    }

    friend auto operator<=>(Point const &a, Point const &b) = default;

    Point operator+(Point const &other) const {
        return Point{row + other.row, col + other.col};
    }

    friend std::ostream &operator<<(std::ostream &os, Point const &p) {
        os << "{.row = " << p.row << ", .col = " << p.col << "}";
        return os;
    }
};
