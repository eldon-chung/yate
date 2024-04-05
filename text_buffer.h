#pragma once

#include <assert.h>
#include <cstddef>
#include <optional>
#include <stdlib.h>

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "util.h"

// basically a string but with some useful metadata
struct TaggedString {
    std::string str;
    size_t width;

    operator std::string_view() const {
        return str;
    }

    TaggedString(std::string s)
        : str(std::move(s)) {
    }
};

namespace StringUtils {
inline size_t symbol_into_width(char c) {
    if (c == '\t') {
        return 4;
    } else {
        return 1;
    }
}

inline size_t var_width_str_into_effective_width(std::string_view sv) {
    size_t width = 0;
    for (char c : sv) {
        width += symbol_into_width(c);
    }

    return width;
}

inline std::optional<Cursor> maybe_down_point(std::string_view sv,
                                              Cursor cursor, size_t width) {

    assert(cursor.col <= sv.size());

    size_t curr_start_col = 0;
    size_t curr_start_effective_col = 0;
    size_t curr_chunk_width = 0;
    size_t cumulative_width = 0;

    size_t col = 0;
    while (col < cursor.col) {

        if (curr_chunk_width + StringUtils::symbol_into_width(sv[col]) <=
            width) {
            cumulative_width += StringUtils::symbol_into_width(sv[col]);
            curr_chunk_width += StringUtils::symbol_into_width(sv[col++]);
            continue;
        }

        curr_start_col = col;
        curr_start_effective_col = cumulative_width;
        curr_chunk_width = 0;
    }

    // now we need to first hit the chunk end.
    std::optional<size_t> next_start_col;
    std::optional<size_t> next_start_effective_col;
    while (col < sv.size()) {

        if (curr_chunk_width + StringUtils::symbol_into_width(sv[col]) <=
            width) {
            cumulative_width += StringUtils::symbol_into_width(sv[col]);
            curr_chunk_width += StringUtils::symbol_into_width(sv[col++]);
        } else {
            // now that we've hit the good case.
            next_start_effective_col = cumulative_width;
            next_start_col = col;
            break;
        }
    }

    if (!next_start_col) {
        return {};
    }

    // now same trick as before:
    // then try to retarget this width if possible.
    assert(next_start_col);
    assert(next_start_effective_col);

    size_t width_from_curr = cursor.effective_col - curr_start_effective_col;
    size_t line_col = *next_start_col;
    size_t curr_width = 0;
    while (line_col < sv.size() &&
           curr_width + StringUtils::symbol_into_width(sv[line_col]) <=
               width_from_curr) {
        curr_width += StringUtils::symbol_into_width(sv[line_col++]);
    }

    return Cursor{cursor.row, line_col, curr_width + *next_start_effective_col};
}

inline std::optional<Cursor> maybe_up_point(std::string_view sv, Cursor cursor,
                                            size_t width) {
    assert(cursor.col <= sv.size());

    size_t prev_start_col = 0;
    size_t prev_start_effective_col = 0;

    size_t curr_start_col = 0;
    size_t curr_start_effective_col = 0;
    size_t curr_chunk_width = 0;

    size_t chunk_idx = 0;

    size_t col = 0;
    while (col < cursor.col) {
        if (curr_chunk_width + StringUtils::symbol_into_width(sv[col]) <=
            width) {
            curr_chunk_width += StringUtils::symbol_into_width(sv[col++]);
            continue;
        }

        prev_start_col = curr_start_col;
        prev_start_effective_col = curr_start_effective_col;

        curr_start_col = col;
        curr_start_effective_col = curr_chunk_width + prev_start_effective_col;
        curr_chunk_width = 0;
        ++chunk_idx;
    }

    if (chunk_idx == 0) {
        return {};
    }

    // then try to retarget this width if possible.
    size_t width_from_curr = cursor.effective_col - curr_start_effective_col;
    size_t line_col = prev_start_col;
    size_t curr_width = 0;
    while (line_col < curr_start_col &&
           curr_width + StringUtils::symbol_into_width(sv[line_col]) <=
               width_from_curr) {
        curr_width += StringUtils::symbol_into_width(sv[line_col++]);
    }

    return Cursor{cursor.row, line_col, curr_width + prev_start_effective_col};
}

// returns the indices into the string that start at chunks divided by width
// in a left justified manner
inline std::vector<std::pair<size_t, size_t>>
columns_of_chunked_text(std::string_view sv, size_t width) {
    assert(width > 0); // eventually set this to tab_stop or something
    std::vector<std::pair<size_t, size_t>> starting_indices = {{0, 0}};
    size_t sv_idx = 0;
    size_t cumulative_width = 0;
    size_t curr_chunk_width = 0;

    while (sv_idx < sv.size()) {
        if (curr_chunk_width + symbol_into_width(sv[sv_idx]) <= width) {
            curr_chunk_width += symbol_into_width(sv[sv_idx]);
            cumulative_width += symbol_into_width(sv[sv_idx]);
            ++sv_idx;
        } else {
            starting_indices.push_back({sv_idx, cumulative_width});
            curr_chunk_width = 0;
        }
    }

    return starting_indices;
}

inline Cursor first_chunk(std::string_view sv, Cursor cursor, size_t width) {
    size_t col = 0, effective_width = 0;
    while (col < sv.size() && effective_width <= cursor.effective_col) {
        if (effective_width + StringUtils::symbol_into_width(sv[col]) >
            cursor.effective_col) {
            break;
        }

        effective_width += StringUtils::symbol_into_width(sv[col++]);
    }
    return Cursor{cursor.row, col, effective_width};
}

inline Cursor final_chunk(std::string_view sv, Cursor cursor, size_t width) {
    auto points = StringUtils::columns_of_chunked_text(sv, width);
    assert(!points.empty());

    if (points.size() == 1) {
        // then it's just the final point.
        assert(points.front().first == 0 && points.front().second == 0);
        size_t col = 0, effective_width = 0;
        while (col < sv.size() && effective_width <= cursor.effective_col) {
            if (effective_width + StringUtils::symbol_into_width(sv[col]) >
                cursor.effective_col) {
                break;
            }
            effective_width += StringUtils::symbol_into_width(sv[col++]);
        }
        return Cursor{cursor.row, col, effective_width};
    }

    size_t second_last_idx = points.size() - 2;
    size_t effective_offset = cursor.effective_col - points.back().second;
    size_t curr_width = 0;
    size_t curr_col = points[second_last_idx].first;
    while (curr_col < cursor.col) {
        if (curr_width + StringUtils::symbol_into_width(sv[curr_col]) >
            effective_offset) {
            break;
        }
        curr_width += StringUtils::symbol_into_width(sv[curr_col++]);
    }
    return Cursor{cursor.row, curr_col, curr_width + cursor.effective_col};
}

} // namespace StringUtils

// an ordered stats tree to help maintain starting_byte_offsets;
// the implementation underneath is a treap
class LineSizeTree {

    struct Node {
        size_t line_size;

        // rest of these are for our bookkeeping
        size_t tree_size;
        size_t priority;
        size_t total_line_size;

        Node *left_node = nullptr;
        Node *right_node = nullptr;
        // Node *parent_node;

        Node(size_t ls, size_t ts, size_t p)
            : line_size(ls),
              tree_size(ts),
              priority(p),
              total_line_size(ls) {
        }

        ~Node() {
            if (left_node) {
                delete left_node;
            }

            if (right_node) {
                delete right_node;
            }
        }

        size_t left_size() const {
            if (left_node) {
                return left_node->tree_size;
            }

            return 0;
        }

        size_t right_size() const {
            if (right_node) {
                return right_node->tree_size;
            }

            return 0;
        }

        void update_values() {
            tree_size = left_size() + right_size() + 1;
            total_line_size = ((left_node) ? left_node->total_line_size : 0) +
                              ((right_node) ? right_node->total_line_size : 0) +
                              line_size;
        }

        friend std::ostream &operator<<(std::ostream &os, Node const &node) {
            if (node.left_node) {
                os << *node.left_node;
            }

            os << &node << ": { .line_size= " << node.line_size
               << "  .left_node= " << node.left_node << " .right_node= "
               << node.right_node
               //    << " .parent_node= " << node.parent_node
               << " .tree_size= " << node.tree_size
               << " .priority= " << node.priority
               << " .total_line_size= " << node.total_line_size << "}";
            os << std::endl;

            if (node.right_node) {
                os << *node.right_node;
            }
            return os;
        }

        size_t left_total_line_size() const {
            if (left_node) {
                return left_node->total_line_size;
            } else {
                return 0;
            }
        }
    };

    // for once can we just try not using std::unique_ptr?
    Node *root_node;

  public:
    LineSizeTree()
        : root_node(nullptr) {
    }

    ~LineSizeTree() {
        if (root_node) {
            delete root_node;
        }
    }

    void clear() {
        assert(root_node);
        delete root_node;
        root_node = nullptr;
    }

    void set_position_size(size_t position, size_t new_size) {
        assert(position <= size());

        if (position < size()) {
            remove_position(position);
        }
        insert_before_position(position, new_size);
    }

    void insert_before_position(size_t position, size_t line_size) {

        Node *to_insert = new Node(line_size, (size_t)1, (size_t)::rand());
        // do a regular BST insert
        root_node = insert_before_position(root_node, position, to_insert);
    }

    void remove_position(size_t position) {
        root_node = remove_position(root_node, position);
    }

    friend std::ostream &operator<<(std::ostream &os, LineSizeTree const &st) {
        if (st.root_node) {
            os << "root ptr: " << st.root_node << std::endl;
            os << *st.root_node << std::endl;
        } else {
            os << "nullptr" << std::endl;
        }
        return os;
    }

    size_t size() const {
        if (!root_node) {
            return 0;
        } else {
            return root_node->tree_size;
        }
    }

    size_t byte_offset_at_line(size_t line) const {
        if (!root_node) {
            return 0;
        }
        assert(line < size());
        Node *node = get_node_at_position(root_node, line);
        assert(node);
        return node->left_total_line_size();
    }

  private:
    static Node *get_node_at_position(Node *c_node, size_t position) {
        assert(c_node);
        if (c_node->left_size() == position) {
            return c_node;
        }

        if (c_node->left_size() > position) {
            return get_node_at_position(c_node->left_node, position);
        } else {
            assert(c_node->left_size() < position);
            return get_node_at_position(c_node->right_node,
                                        position - c_node->left_size() - 1);
        }
    }

    static Node *bubble_down(Node *c_node) {
        auto swap_with_left = [](Node *a) -> Node * {
            assert(a->left_node);
            Node *left = a->left_node;
            Node *right = a->right_node;

            a->left_node = left->left_node;
            a->right_node = left->right_node;

            left->left_node = a;
            left->right_node = right;

            return left;
        };

        auto swap_with_right = [](Node *a) -> Node * {
            assert(a->right_node);
            Node *left = a->left_node;
            Node *right = a->right_node;

            a->left_node = right->left_node;
            a->right_node = right->right_node;

            right->left_node = left;
            right->right_node = a;

            return right;
        };

        if (c_node->left_node && c_node->right_node) {
            // pick the lower prio to rotate into
            if (c_node->left_node->priority < c_node->right_node->priority) {
                c_node = swap_with_left(c_node);
                c_node->left_node = bubble_down(c_node->left_node);
            } else {
                c_node = swap_with_right(c_node);
                c_node->right_node = bubble_down(c_node->right_node);
            }
            assert(c_node);
            c_node->update_values();
            return c_node;
        }

        assert(!(c_node->left_node && c_node->right_node));

        // just delete c_node and return the non-null of the two
        Node *to_return = nullptr;
        if (c_node->left_node) {
            to_return = std::exchange(c_node->left_node, nullptr);
        } else if (c_node->right_node) {
            assert(c_node->right_node);
            to_return = std::exchange(c_node->right_node, nullptr);
        }

        assert(!c_node->left_node && !c_node->right_node);
        delete c_node;
        return to_return;
    }

    static Node *remove_position(Node *c_node, size_t position) {
        assert(c_node);
        assert(position < c_node->tree_size);
        if (c_node->left_size() == position) {
            // c_node is the node we need to remove;
            Node *to_return = bubble_down(c_node);
            if (to_return) {
                to_return->update_values();
            }
            return to_return;
        }

        if (position < c_node->left_size()) {
            c_node->left_node = remove_position(c_node->left_node, position);
            if (c_node->left_node) {
                c_node->left_node->update_values();
            }
        } else {
            assert(position > c_node->left_size());
            c_node->right_node = remove_position(
                c_node->right_node, position - c_node->left_size() - 1);
            if (c_node->right_node) {
                c_node->right_node->update_values();
            }
        }

        c_node->update_values();
        return c_node;
    }

    static Node *insert_before_position(Node *c_node, size_t position,
                                        Node *to_insert) {
        if (c_node == nullptr) {
            assert(position == 0);
            return to_insert;
        }

        Node *to_return = c_node;
        if (position <= c_node->left_size()) {

            c_node->left_node =
                insert_before_position(c_node->left_node, position, to_insert);
            if (c_node->left_node->priority > c_node->priority) {

                // right rotate current node;
                Node *lr_child = c_node->left_node->right_node;
                Node *l_child = c_node->left_node;
                c_node->left_node = lr_child;
                l_child->right_node = c_node;
                to_return = l_child;
                c_node->update_values();
            }

        } else {
            assert(position > c_node->left_size());
            c_node->right_node = insert_before_position(
                c_node->right_node, position - c_node->left_size() - 1,
                to_insert);
            if (c_node->right_node->priority > c_node->priority) {

                // left rotate current node;
                Node *rl_child = c_node->right_node->left_node;
                Node *r_child = c_node->right_node;
                c_node->right_node = rl_child;
                r_child->left_node = c_node;
                to_return = r_child;
                c_node->update_values();
            }
        }
        to_return->update_values();

        return to_return;
    }
};

struct TextBuffer {
    std::vector<std::string> buffer;
    LineSizeTree starting_byte_offset;

  public:
    TextBuffer()
        : buffer({""}),
          starting_byte_offset() {
        starting_byte_offset.insert_before_position(0, 0);
    }

    size_t get_offset_from_point(Cursor point) const {
        return starting_byte_offset.byte_offset_at_line(point.row) + point.col;
    }

    void load_contents(std::string_view contents) {
        buffer.clear();

        size_t running_offset = 0;
        size_t num_lines = 0;
        starting_byte_offset.clear();
        while (true) {
            size_t newl_pos = contents.find('\n');
            starting_byte_offset.insert_before_position(num_lines,
                                                        running_offset);
            buffer.push_back(std::string{contents.substr(0, newl_pos)});

            if (newl_pos == std::string::npos) {
                break;
            }

            contents = contents.substr(newl_pos + 1);
            ++num_lines;
            running_offset += newl_pos + 1;
        }
    }

    void insert_char_at(Cursor cursor, char c) {
        buffer.at(cursor.row).insert(cursor.col++, 1, c);
        starting_byte_offset.set_position_size(cursor.row,
                                               buffer.at(cursor.row).size());
    }

    void insert_newline_at(Cursor cursor) {
        std::string next_line = buffer.at(cursor.row).substr(cursor.col);
        buffer.at(cursor.row).resize(cursor.col);
        buffer.insert(buffer.begin() + (long)cursor.row + 1,
                      std::move(next_line));
        starting_byte_offset.set_position_size(cursor.row,
                                               buffer.at(cursor.row).size());
        starting_byte_offset.set_position_size(
            cursor.row + 1, buffer.at(cursor.row + 1).size());
    }

    void insert_backspace_at(Cursor cursor) {

        if (cursor.col > 0) {
            buffer.at(cursor.row).erase(--cursor.col, 1);
            starting_byte_offset.set_position_size(
                cursor.row, buffer.at(cursor.row).size());
        } else if (cursor.row > 0) {
            assert(cursor.col == 0);
            cursor.col = buffer.at(--cursor.row).length();
            buffer.at(cursor.row).append(buffer.at(cursor.row + 1));
            buffer.erase(buffer.begin() + (long)cursor.row + 1);

            starting_byte_offset.set_position_size(
                cursor.row, buffer.at(cursor.row).size());
        }
    }

    void insert_delete_at(Cursor cursor) {
        if (cursor.col < buffer.at(cursor.row).size()) {
            buffer.at(cursor.row).erase(cursor.col, 1);

            starting_byte_offset.set_position_size(
                cursor.row, buffer.at(cursor.row).size());

        } else if (cursor.row + 1 < buffer.size()) {
            assert(cursor.col == buffer.at(cursor.row).size());
            buffer.at(cursor.row) += buffer.at(cursor.row + 1);
            buffer.erase(buffer.begin() + (long)cursor.row + 1);
            starting_byte_offset.set_position_size(
                cursor.row, buffer.at(cursor.row).size());

            // remove the next line's size too
            starting_byte_offset.remove_position(cursor.row + 1);
        }
    }

    std::vector<std::string_view> get_n_lines_at(size_t starting_row,
                                                 size_t row_count) const {
        std::vector<std::string_view> to_ret;
        to_ret.reserve(row_count);

        for (size_t idx = 0;
             idx < row_count && idx + starting_row < buffer.size(); ++idx) {
            to_ret.push_back(buffer.at(starting_row + idx));
        }

        return to_ret;
    }

    std::string_view at(size_t starting_row) const {
        return buffer.at(starting_row);
    }

    size_t num_lines() const {
        return buffer.size();
    }

    std::vector<std::string> get_lines(Cursor lp, Cursor rp) const {
        if (rp <= lp) {
            // nothing to return; should we instead return {""}?
            return {};
        }

        // how to handle case the points aren't well formed?
        // do we just crash?

        std::vector<std::string> to_return;
        if (lp.row == rp.row) {
            std::string_view line = buffer.at(lp.row);
            // bound to break if lp.col exceeds the value
            line = line.substr(lp.col, rp.col - lp.col);
            to_return.push_back(std::string(line));
            return to_return;
        }

        for (size_t idx = lp.row; idx <= rp.row; ++idx) {
            std::string_view line = buffer.at(idx);

            if (idx == rp.row) {
                line = line.substr(0, rp.col);
            }

            if (idx == lp.row) {
                line = line.substr(lp.col);
            }

            to_return.push_back(std::string(line));
        }

        return to_return;
    }

    Cursor replace_text_at(Cursor lp, Cursor rp,
                           std::vector<std::string> lines) {
        remove_text_at(lp, rp);
        Cursor to_return = insert_text_at(lp, std::move(lines));
        return to_return;
    }

    void remove_text_at(Cursor lp, Cursor rp) {
        if (lp.row == rp.row) {
            std::string right_half = buffer.at(lp.row).substr(rp.col);

            buffer.at(lp.row).resize(lp.col);

            // not sure the move does anything big here
            buffer.at(lp.row).append(std::move(right_half));

            starting_byte_offset.set_position_size(lp.row,
                                                   buffer.at(lp.row).size());
            return;
        }

        // hmm TODO: range removal?

        buffer.at(lp.row).resize(lp.col);
        buffer.at(rp.row) = buffer.at(rp.row).substr(rp.col);

        buffer.erase(buffer.begin() + (ssize_t)lp.row + 1,
                     buffer.begin() + (ssize_t)rp.row);
        starting_byte_offset.set_position_size(lp.row,
                                               buffer.at(lp.row).size());

        starting_byte_offset.set_position_size(rp.row,
                                               buffer.at(lp.row + 1).size());
        for (size_t r_pos = rp.row - 1; r_pos > lp.row; --r_pos) {

            starting_byte_offset.remove_position(r_pos);
        }

        // then delete one more line?
        insert_delete_at(lp);
    }

    Cursor insert_text_at(Cursor point, std::vector<std::string> lines) {
        // break the line at point
        assert(!lines.empty());

        if (lines.size() == 1) {
            std::string right_half = buffer.at(point.row).substr(point.col);
            buffer.at(point.row).resize(point.col);
            buffer.at(point.row).append(lines.front());
            buffer.at(point.row).append(right_half);
            starting_byte_offset.set_position_size(point.row,
                                                   buffer.at(point.row).size());
            size_t effective_width_offset =
                StringUtils::var_width_str_into_effective_width(lines.front());
            return {point.row, point.col + lines.front().size(),
                    point.effective_col + effective_width_offset};
        }

        Cursor final_insertion_point = {
            point.row + lines.size() - 1, lines.back().size(),
            StringUtils::var_width_str_into_effective_width(lines.back())};

        std::string right_half = buffer.at(point.row).substr(point.col);
        buffer.at(point.row).resize(point.col); // retains the old string

        lines.back().append(right_half);
        buffer.at(point.row).append(lines.front());
        // middle stuff
        buffer.insert(buffer.begin() + (ssize_t)point.row + 1,
                      lines.begin() + 1, lines.end());

        for (size_t to_update = point.row;
             to_update <= final_insertion_point.row; ++to_update) {
            starting_byte_offset.set_position_size(to_update,
                                                   buffer.at(to_update).size());
        }

        return final_insertion_point;
    }

    void insert_text_at(Cursor point, char ch) {
        insert_text_at(point, {{ch}});
    }

    std::vector<std::string> get_nth_line(size_t idx) const {
        return {buffer.at(idx)};
    }

    std::vector<std::string_view> get_view() const {
        std::vector<std::string_view> to_ret;
        to_ret.reserve(buffer.size());
        for (auto const &line : buffer) {
            to_ret.push_back(line);
        }

        return to_ret;
    }

    char operator[](Cursor cursor) const {
        return buffer.at(cursor.row)[cursor.col];
    }
};

inline const char *read_text_buffer(void *payload,
                                    [[maybe_unused]] uint32_t byte_offset,
                                    TSPoint position, uint32_t *bytes_read) {
    // fprintf(stderr, "requested position: %u, %u\n", position.row,
    //         position.column);

    TextBuffer *text_buffer_ptr = (TextBuffer *)payload;
    // I'm guessing I only need to use either of the 2?

    if (position.row >= text_buffer_ptr->num_lines() ||
        (position.row == text_buffer_ptr->num_lines() - 1 &&
         position.column >=
             text_buffer_ptr->get_nth_line(position.row).size())) {
        // fprintf(stderr, "returning EOF\n");
        *bytes_read = 0;
        return "\0";
    }

    if (position.column == text_buffer_ptr->at(position.row).size()) {
        // this __should__ be safe because it should point to somewhere in
        // globals, rather than point to stack
        // fprintf(stderr, "returning newline\n");
        *bytes_read = 1;
        return "\n";
    }

    assert(position.column < text_buffer_ptr->at(position.row).size());
    const char *corresponding_byte = text_buffer_ptr->at(position.row).data();
    corresponding_byte += position.column;
    *bytes_read =
        (uint32_t)text_buffer_ptr->at(position.row).size() - position.column;
    // fprintf(stderr, "giving str: %s\n", corresponding_byte);
    // fprintf(stderr, "bytes read: %u\n", *bytes_read);

    return corresponding_byte;
}
