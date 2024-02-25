#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

// TODO: refactor API at some point.
struct File {
    std::optional<std::string> filename;
    int fd; // won't bother with an optional for this one
    std::optional<std::string> errmsg;
    bool in_ro_mode;

    File(std::string_view fn, int fd_, std::optional<std::string> em, bool irm)
        : filename(fn),
          fd(fd_),
          errmsg(em),
          in_ro_mode(irm) {
    }

  public:
    File()
        : filename(std::nullopt),
          fd(-1),
          errmsg(std::nullopt),
          in_ro_mode(false) {
    }

    File(std::string_view fn)
        : filename(fn),
          in_ro_mode(false) {
        // not a creating open. we can assign filenames first
        // if the file doesnt exist we only create it when
        // writing

        // TODO: mkdir -p semantics at some point?

        fd = open(filename->c_str(), O_RDWR);
        if (fd == -1 && errno == EACCES) {
            errmsg = strerror(errno);

            // try again with lower permissions
            fd = open(fn.data(), O_RDONLY);
            if (fd == -1) {
                errmsg = strerror(errno);
            } else {
                in_ro_mode = true;
                errmsg->clear();
            }
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
          in_ro_mode(other.in_ro_mode) {
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
        swap(a.in_ro_mode, b.in_ro_mode);
    }

    static std::pair<File, bool> create_if_not_exists(std::string_view fn) {
        int fd = open(fn.data(), O_RDWR);
        std::string errmsg;

        if (fd == -1 && errno == EACCES) {
            // try again with lower permissions
            fd = open(fn.data(), O_RDONLY);
            if (fd == -1) {
                errmsg = strerror(errno);
            }
            return {File(fn, fd, std::move(errmsg), true), false};
        } else if (fd == -1 && errno == ENOENT) {
            // then we should be creating it
            fd =
                open(fn.data(), O_RDWR | O_CREAT,
                     S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
            return {File(fn, fd, std::move(errmsg), false), true};
        }

        return {File(fn, fd, std::move(errmsg), false),
                false}; // file was not created
    }

    std::string get_file_contents() {

        if (fd == -1) {
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
        if (in_ro_mode) {
            errmsg = "Can't write to file in read-only mode";
            return false;
        }

        if (!filename.has_value()) {
            // this is exceptional behaviour
            std::cerr << "No file name specified yet." << std::endl;
            return false;
        }

        if (fd == -1 && filename.has_value()) {
            // we now attempt a creating open?
            fd =
                open(filename->c_str(), O_RDWR | O_CREAT,
                     S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
            if (fd == -1) {
                errmsg = strerror(errno);
                return false;
            }
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

    bool in_readonly_mode() const {
        return in_ro_mode;
    }
};
