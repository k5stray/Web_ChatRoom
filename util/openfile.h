#ifndef OPEN_FILE_H
#define OPEN_FILE_H

#include <fstream>
#include <iostream>
#include <sstream>

#include "log_util.h"

class FileInstance
{
public:
    static FileInstance& get_Instance() {
        static FileInstance instance;
        return instance;
    }

    void add_openfile(std::string path, int type = 0) {
        FileObj *add = new FileObj(path, type);
        list.push_back(add);
    }

    std::string get_openfile(std::string path) {
        for (auto &f : list) {
            if (f->path == path) {
                return f->data;
            }
        }
        return "";
    }
private:
    struct FileObj {
        std::string path;
        std::string data;
        FileObj(std::string fname, int type = 0) : path(std::move(fname)) {
            if (type == 0) {
                data = std::move(read_text2string());
            } else {
                data = std::move(read_binary2string());
            }
        }

        std::string read_text2string() {
            std::ifstream input_file(path);
            if (!input_file.is_open()) {
                LOG_FATAL_("Could not open the file - '%s'\n", path.c_str());
                exit(EXIT_FAILURE);
            }
            return std::string((std::istreambuf_iterator<char>(input_file)),
                        std::istreambuf_iterator<char>());
        }

        std::string read_binary2string() {
            std::ifstream input_file(path, std::ios::binary | std::ios::ate); 
            if (!input_file.is_open()) {
                std::cerr << "Could not open the file - '" << path << "'" << std::endl;
                exit(EXIT_FAILURE);
            }
            std::streamsize size = input_file.tellg();
            input_file.seekg(0, std::ios::beg);

            std::vector<char> buffer(size);
            input_file.read(buffer.data(), size);

            return std::string(buffer.begin(), buffer.end());
        }
    };

    FileInstance() = default;
    ~FileInstance() {
        for (auto &f : list) {
            delete f;
        }
    }
    FileInstance(const FileInstance&) = delete;
    FileInstance& operator=(const FileInstance&) = delete;

    std::vector<FileObj*> list;
};

#endif