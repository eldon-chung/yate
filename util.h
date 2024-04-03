#pragma once

#include <stddef.h>

#include <dlfcn.h>

#include <compare>
#include <iostream>
#include <optional>

// #include "tree_sitter/include/tree_sitter/api.h"
#include <tree_sitter/api.h>
// #include "tree_sitter/languages/languages.h"

#include "File.h"

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

    Point(TSPoint ts_point)
        : row(ts_point.row),
          col(ts_point.column) {
    }

    operator TSPoint() {
        return TSPoint{.row = (unsigned int)row, .column = (unsigned int)col};
    }

    Point operator=(TSPoint ts_point) {
        return Point(ts_point);
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

typedef TSLanguage *(*parser_fn_ptr_t)(void);

// RAII-based wrapper for dynamically linked functions
struct DLFunc {
    std::string symbol_name; // file name of where the shared library is
    void *handle;            // the handle returned by dlopen
    parser_fn_ptr_t fn_ptr;

    std::string errmsg;
    DLFunc(std::string_view filename, std::string_view s_name)
        : symbol_name(s_name),
          handle(nullptr) {
        // do a dl open here
        handle = dlopen(filename.data(), RTLD_LAZY);
        // need to handle the errors gracefully (maybe eventually with logging?)
        if (!handle) { // if the handle is missing what do we do?
            errmsg = dlerror();
        }

        fn_ptr = (::parser_fn_ptr_t)dlsym(handle, symbol_name.c_str());
    }

    DLFunc(DLFunc const &) = delete;
    DLFunc(DLFunc &&) = delete;
    DLFunc &operator=(DLFunc const &) = delete;
    DLFunc &operator=(DLFunc &&) = delete;

    ~DLFunc() {
        if (handle) {
            dlclose(handle);
        }
    }

    ::parser_fn_ptr_t get_parser_fn_ptr() {
        return fn_ptr;
    }
};

struct Capture {
    Point start;
    Point end;
    std::string_view capture_name;
};

// buffer reader function type for tree-sitter
typedef const char *(*read_fn_ptr_t)(void *, uint32_t, TSPoint, uint32_t *);

// Container for TSParser, and TSTree
// Considering that both are stateful they seem coupled.
// might as well store both and instantiate Parser<TextBuffer> for each
// instance of a file we wish to parse
template <typename T> class Parser {

  public:
    // For now let's just run with these
    enum class LANG {
        C,
        CPP,
        PYTHON,
        JSON,
    };

  private:
    // should we just make a static table for all the dlsyms?

    TSParser *parser_ptr; // pointer to the stateful parser
    TSTree *tree_ptr;     // pointer to the result of the parser
    std::optional<LANG> language;
    std::string query_str;
    T const *buffer_ptr; // pointer to the buffer we want to parse
    read_fn_ptr_t read_function_ptr;
    // just so happens you need it again when forming queries
    parser_fn_ptr_t parser_function_ptr;

  public:
    static std::string_view get_queries_str(LANG lang) {
        switch (lang) {
            using enum LANG;
        case PYTHON: {
            return "";
        } break;
        case CPP: {
            static File cpp_queries_file{
                "tree_sitter_langs/cpp/highlights.scm"};
            static std::string cpp_query_string =
                cpp_queries_file.get_file_contents().value();
            return cpp_query_string;
        }
        case C: {
            return "";
        }
        default:
            break;
        }

        return "";
    }

    static parser_fn_ptr_t get_parser_ptr(LANG lang) {
        switch (lang) {
            using enum LANG;
        case PYTHON: {
            static DLFunc python_dl{"tree_sitter_langs/python/python.so",
                                    "tree_sitter_python"};
            return python_dl.get_parser_fn_ptr();
        } break;
        case CPP: {
            static DLFunc cpp_dl{"tree_sitter_langs/cpp/cpp.so",
                                 "tree_sitter_cpp"};
            return cpp_dl.get_parser_fn_ptr();
        }
        case C: {
            static DLFunc cpp_dl{"tree_sitter_langs/c/c.so", "tree_sitter_c"};
            return cpp_dl.get_parser_fn_ptr();
        }
        default:
            break;
        }

        return nullptr;
    }

    parser_fn_ptr_t get_current_language_parser_ptr() {
        assert(language.value());
        assert(parser_function_ptr);
        return parser_function_ptr;
    }

    void set_language(LANG lang) {
        language = lang;
        parser_function_ptr = get_parser_ptr(language.value());
        ts_parser_set_language(parser_ptr, parser_function_ptr());
    }

    Parser(T const *bp, read_fn_ptr_t rfp)
        : parser_ptr(ts_parser_new()),
          tree_ptr(nullptr),
          language(std::nullopt),
          buffer_ptr(bp),
          read_function_ptr(rfp) {
    }

    ~Parser() {
        if (tree_ptr) {
            ts_tree_delete(tree_ptr);
        }

        if (parser_ptr) {
            ts_parser_delete(parser_ptr);
        }
    }

    friend void swap(Parser &a, Parser &b) {
        using std::swap;
        swap(a.parser_ptr, b.parser_ptr);
        swap(a.tree_ptr, b.tree_ptr);
        swap(a.language, b.language);
        swap(a.buffer_ptr, b.buffer_ptr);
        swap(a.read_function_ptr, b.read_function_ptr);
    }

    Parser(Parser const &) = delete;
    Parser &operator=(Parser const &) = delete;

    Parser(Parser &&other)
        : parser_ptr(std::exchange(other.parser_ptr, nullptr)),
          tree_ptr(std::exchange(other.tree_ptr, nullptr)),
          language(other.language),
          buffer_ptr(other.buffer_ptr),
          read_function_ptr(other.read_function_ptr) {
    }
    Parser &operator=(Parser &&other) {
        Parser temp{std::move(other)};
        swap(*this, temp);
        return *this;
    }

    // IMPT: use this for fresh parses and not updates
    void parse_buffer() {
        assert(language.has_value());
        tree_ptr = ts_parser_parse(parser_ptr, nullptr,
                                   TSInput{.payload = (void *)buffer_ptr,
                                           .read = read_function_ptr,
                                           .encoding = TSInputEncodingUTF8});
        // according to the api docs, it should never return nullptr (based on
        // how we are using it)
        assert(tree_ptr);
    }

    void update(Point start_point, Point old_end_point, Point new_end_point,
                size_t start_byte, size_t old_end_byte, size_t new_end_byte) {
        assert(language.has_value());
        assert(tree_ptr);

        TSInputEdit edit{.start_byte = (uint32_t)start_byte,
                         .old_end_byte = (uint32_t)old_end_byte,
                         .new_end_byte = (uint32_t)new_end_byte,
                         .start_point = start_point,
                         .old_end_point = old_end_point,
                         .new_end_point = new_end_point};

        ts_tree_edit(tree_ptr, &edit);
        tree_ptr = ts_parser_parse(parser_ptr, tree_ptr,
                                   TSInput{.payload = (void *)buffer_ptr,
                                           .read = read_function_ptr,
                                           .encoding = TSInputEncodingUTF8});
        // TODO: i think we need to invalidate the queries
    }

    // accesser methods for the tree
    // Note: remember to call delete on this
    TSTreeCursor get_tree_cursor() const {
        return ts_tree_cursor_new(ts_tree_root_node(tree_ptr));
    }

    void ts_query(TSQueryCursor *query_cursor, const TSQuery *query) {
        ts_query_cursor_exec(query_cursor, query, ts_tree_root_node(tree_ptr));
    }

    std::vector<Capture> get_captures_within(Point start_boundary,
                                             Point end_boundary) const {

        assert(language.has_value());

        TSParser *parser = ts_parser_new();
        ts_parser_set_language(parser_ptr, parser_function_ptr());

        TSQueryError error_type;
        uint32_t error_offset;
        std::string_view lang_query_string = get_queries_str(language.value());
        TSQuery *ts_query = ts_query_new(
            parser_function_ptr(), lang_query_string.data(),
            (unsigned int)lang_query_string.size(), &error_offset, &error_type);

        TSQueryCursor *ts_query_cursor = ts_query_cursor_new();
        ts_query_cursor_exec(ts_query_cursor, ts_query,
                             ts_tree_root_node(tree_ptr));

        std::vector<Capture> to_return;

        TSQueryMatch ts_query_match;
        uint32_t cap_index;
        while (ts_query_cursor_next_capture(ts_query_cursor, &ts_query_match,
                                            &cap_index)) {
            // only pushback stuff that is at least partially within the range
            Point start_point =
                ts_node_start_point(ts_query_match.captures[cap_index].node);
            Point end_point =
                ts_node_end_point(ts_query_match.captures[cap_index].node);

            uint32_t size;
            const char *a = ts_query_capture_name_for_id(
                ts_query, ts_query_match.captures[cap_index].index, &size);

            if (start_point >= end_boundary || end_point <= start_boundary) {
                continue;
            }

            to_return.push_back({.start = start_point,
                                 .end = end_point,
                                 .capture_name = std::string_view{a, size}});
        }

        ts_query_cursor_delete(ts_query_cursor);
        ts_query_delete(ts_query);
        ts_parser_delete(parser);

        return to_return;
    }

    friend std::ostream &operator<<(std::ostream &os, Parser const &parser) {
        TSTreeCursor cursor_at_root = parser.get_tree_cursor();
        ts_print_node(os, 0, cursor_at_root);
        ts_tree_cursor_delete(&cursor_at_root);
        return os;
    }

    // make sure to grab by copy
    static void ts_print_node(std::ostream &os, size_t num_indents,
                              TSTreeCursor &tree_cursor) {

        auto print_point = [&](TSPoint point) {
            os << "{.row=" << point.row << " .col = " << point.column << "}";
        };

        TSNode curr_node = ts_tree_cursor_current_node(&tree_cursor);
        if (ts_node_is_null(curr_node)) {
            return;
        }

        {
            for (size_t i = 0; i < num_indents; ++i) {
                os << "\t";
            }
        }

        os << "left point: ";
        print_point(ts_node_start_point(curr_node));
        os << " ";

        os << "right point: ";
        print_point(ts_node_end_point(curr_node));
        os << std::endl;

        if (ts_tree_cursor_goto_first_child(&tree_cursor)) {
            ts_print_node(os, num_indents + 1, tree_cursor);
        } else {
            return;
        }

        while (ts_tree_cursor_goto_next_sibling(&tree_cursor)) {
            ts_print_node(os, num_indents + 1, tree_cursor);
        }

        ts_tree_cursor_goto_parent(&tree_cursor);
    }
};
