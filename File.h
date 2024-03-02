#pragma once

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

// TODO: refactor API at some point.
struct File {
    enum class Mode { READWRITE, READONLY, UNREADABLE, SCRATCH };
    using enum Mode;

    std::optional<std::string> filename;
    int fd; // won't bother with an optional for this one
    std::optional<std::string> errmsg;
    Mode mode;

    File(std::string_view fn, int fd_, std::optional<std::string> em, Mode m)
        : filename(fn),
          fd(fd_),
          errmsg(em),
          mode(m) {
    }

  public:
    File()
        : filename(std::nullopt),
          fd(-1),
          errmsg(std::nullopt),
          mode(SCRATCH) {
    }

    File(std::string_view fn)
        : filename(fn),
          mode(SCRATCH) {
        // not a creating open. we can assign filenames first
        // if the file doesnt exist we only create it when
        // writing

        // TODO: mkdir -p semantics at some point?

        fd = open(filename->c_str(), O_RDWR);

        if (fd != -1) {
            mode = READWRITE;
            return;
        }

        if (fd == -1 && errno == EACCES) {
            // we know the file exists
            errmsg = "";
            errmsg->append(strerror(errno));
            errmsg->append(" on opening ");
            errmsg->append(*filename);

            // try again with lower permissions
            fd = open(fn.data(), O_RDONLY);
            if (fd == -1) {
                mode = UNREADABLE;
                errmsg = "";
                errmsg->append(strerror(errno));
                errmsg->append(" on opening ");
                errmsg->append(*filename);
            } else {
                mode = READONLY;
                errmsg->clear();
            }
        } else if (fd == -1 && errno == EISDIR) {
            errmsg = *filename + " is a directory.";
            mode = UNREADABLE;
        } else if (fd == -1 && errno == ENOENT) {
            mode = SCRATCH;
            errmsg = *filename + " does not exist.";
        }
    }

    ~File() {
        if (fd != -1) {
            close(fd);
        }
    }

    File(File const &) = delete;
    File &operator=(File &) = delete;

    File(File &&other)
        : filename(std::move(other.filename)),
          fd(std::exchange(other.fd, -1)),
          errmsg(std::move(other.errmsg)),
          mode(other.mode) {
    }

    File &operator=(File &&other) {
        File temp{std::move(other)};
        swap(*this, temp);
        return *this;
    }

    friend void swap(File &a, File &b) {
        using std::swap;
        swap(a.fd, b.fd);
        swap(a.filename, b.filename);
        swap(a.errmsg, b.errmsg);
        swap(a.mode, b.mode);
    }

    Mode get_mode() const {
        return mode;
    }

    std::optional<std::string> get_file_contents() {

        if (mode == Mode::UNREADABLE) {
            errmsg = "Can't read from file";
            return std::nullopt;
        }

        if (mode == SCRATCH) {
            return "";
        }

        assert(filename.has_value());

        struct stat st;
        if (fstat(fd, &st) == -1) {
            std::cerr << "File: could not stat \"" << *filename;
            std::cerr << "\" error message: " << strerror(errno) << std::endl;
            return "";
        }

        std::string to_return;
        to_return.resize((size_t)st.st_size);

        ssize_t num_read = read(fd, to_return.data(), (size_t)st.st_size);
        if (num_read == -1) {
            std::cerr << "File: could not read from \"" << *filename;
            std::cerr << "\" error message: " << strerror(errno) << std::endl;
            return "";
        }

        return to_return;
    }

    bool write(std::vector<std::string_view> contents) {
        assert(has_filename());

        if (mode == Mode::SCRATCH) {
            // we need to open the file first
            // this is wrong
            try_open_or_create();
        }

        if (mode == Mode::UNREADABLE) {
            errmsg = "Don't have permissions";
            return false;
        }

        if (mode == Mode::READONLY) {
            errmsg = "Can't write to file in read-only mode";
            return false;
        }

        ssize_t num_written = 0;

        lseek(fd, 0, SEEK_SET);
        for (size_t idx = 0; idx < contents.size(); ++idx) {

            ssize_t ret_val =
                ::write(fd, contents[idx].data(), contents[idx].size());

            if (ret_val == -1) {
                return false;
            }

            num_written += ret_val;

            if (idx < contents.size() - 1) {
                ::write(fd, "\n", 1);
                num_written += 1;
            }
        }

        if (::ftruncate(fd, num_written) == -1) {
            errmsg = strerror(errno);
            return false;
        }

        if (::fsync(fd) == -1) {
            errmsg = strerror(errno);
            return false;
        }

        return true;
    }

    bool try_open_or_create() {
        // returns true if file was created
        // TODO: mkdir -p semantics at some point?

        if (-1 != fd) {
            return false;
        }

        assert(has_filename());
        fd = open(filename->c_str(), O_RDWR);

        if (fd != -1) {
            mode = READWRITE;
            return false;
        }

        // file doesn't exist, try again with O_CREAT
        if (fd == -1 && errno == ENOENT) {
            fd =
                open(filename->c_str(), O_RDWR | O_CREAT,
                     S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
            mode = READWRITE;
            if (-1 != fd) {
                return true;
            }
        }

        // remaining errors in case of ENOENT
        if (fd == -1 && errno == EACCES) {
            // we know the file exists
            errmsg = "";
            errmsg->append(strerror(errno));
            errmsg->append(" on opening ");
            errmsg->append(*filename);

            // try again with lower permissions
            fd = open(filename->c_str(), O_RDONLY);
            if (fd == -1) {
                mode = UNREADABLE;
                errmsg = "";
                errmsg->append(strerror(errno));
                errmsg->append(" on opening ");
                errmsg->append(*filename);
            } else {
                mode = READONLY;
                errmsg->clear();
            }
        } else if (fd == -1 && errno == EISDIR) {
            errmsg = *filename + " is a directory.";
            mode = UNREADABLE;
        }

        return false;
    }

    void set_filename(std::string_view file_view) {
        *this = File(); // resets the file
        filename = std::string(file_view);
    }

    bool is_open() const {
        return fd != -1;
    }

    bool has_filename() const {
        return filename.has_value();
    }

    std::string_view get_filename() const {
        return filename.value();
    }

    bool has_errmsg() const {
        return errmsg.has_value();
    }

    std::string_view get_errmsg() const {
        return errmsg.value();
    }
};
