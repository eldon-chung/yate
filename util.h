#pragma once

#include <stddef.h>

#include <compare>
#include <iostream>

struct Point {
    size_t row;
    size_t col;

    friend auto operator==(Point const &a, Point const &b) {
        return (a.row == b.row && a.col == b.col);
    }

    friend auto operator<(Point const &a, Point const &b) {
        return (a.row < b.row || (a.row == b.row && a.col < b.col));
    }

    friend auto operator>(Point const &a, Point const &b) {
        return (a.row > b.row || (a.row == b.row && a.col > b.col));
    }

    friend auto operator>=(Point const &a, Point const &b) {
        return (a == b) || (a.row > b.row || (a.row == b.row && a.col > b.col));
    }

    Point operator+(Point const &other) const {
        return Point{row + other.row, col + other.col};
    }

    friend std::ostream &operator<<(std::ostream &os, Point const &p) {
        os << "{.row = " << p.row << ", .col = " << p.col << "}";
        return os;
    }
};
