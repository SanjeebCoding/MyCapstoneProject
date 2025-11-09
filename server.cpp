// server.cpp
// Compile: g++ -std=c++11 -pthread server.cpp -o server

#include <arpa/inet.h>
#include <dirent.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <memory>

const int BACKLOG = 10;
const int BUFFER_SIZE = 8192;
const std::string SHARED_DIR = "/home/vboxuser/shared_files";

std::mutex cout_mtx;
std::mutex map_mtx;

std::map<std::string, std::shared_ptr<std::mutex>> file_mutex_map;

void safe_print(const std::string &s) {
    std::lock_guard<std::mutex> lk(cout_mtx);
    std::cout << s << std::endl;
}

bool send_all(int sock, const char *data, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t s = send(sock, data + total, len - total, 0);
        if (s <= 0) return false;
        total += s;
    }
    return true;
}

bool recv_exact(int sock, char *buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t r = recv(sock, buf + total, len - total, 0);
        if (r <= 0) return false;
        total += r;
    }
    return true;
}

bool recv_line(int sock, std::string &out) {
    out.clear();
    char c;
    while (true) {
        ssize_t r = recv(sock, &c, 1, 0);
        if (r == 0) return false;
        if (r < 0) return false;
        if (c == '\n') break;
        out.push_back(c);
        if (out.size() > 16384) break;
    }
    return true;
}

void handle_client(int client_sock, std::string client_id) {
    safe_print("[+] Client connected: " + client_id);
    while (true) {
        std::string cmdline;
        if (!recv_line(client_sock, cmdline)) {
            safe_print("[-] Client disconnected: " + client_id);
            break;
        }
        if (!cmdline.empty() && cmdline.back() == '\r') cmdline.pop_back();
        safe_print("[" + client_id + "] CMD: " + cmdline);

        if (cmdline == "LIST") {
            DIR *dir = opendir(SHARED_DIR.c_str());
            if (!dir) {
                std::string err = "ERROR Cannot open directory\n";
                send_all(client_sock, err.c_str(), err.size());
                continue;
            }
            struct dirent *entry;
            std::string all;
            while ((entry = readdir(dir)) != nullptr) {
                if (entry->d_type == DT_REG) {
                    all += entry->d_name;
                    all += "\n";
                }
            }
            closedir(dir);
            if (all.empty()) all = "(no files)\n";
            send_all(client_sock, all.c_str(), all.size());
        }
        else if (cmdline.rfind("DOWNLOAD ", 0) == 0) {
            std::string filename = cmdline.substr(9);
            if (filename.find("..") != std::string::npos) {
                std::string err = "ERROR Invalid filename\n";
                send_all(client_sock, err.c_str(), err.size());
                continue;
            }
            std::string full = SHARED_DIR + "/" + filename;
            struct stat st;
            if (stat(full.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
                std::string err = "ERROR File not found\n";
                send_all(client_sock, err.c_str(), err.size());
                continue;
            }
            off_t filesize = st.st_size;
            std::ostringstream hdr;
            hdr << "FILESIZE " << filesize << "\n";
            std::string header = hdr.str();
            if (!send_all(client_sock, header.c_str(), header.size())) break;

            std::ifstream ifs(full, std::ios::binary);
            if (!ifs) {
                std::string err = "ERROR Cannot open file\n";
                send_all(client_sock, err.c_str(), err.size());
                continue;
            }
            char buf[BUFFER_SIZE];
            off_t sent = 0;
            while (ifs) {
                ifs.read(buf, sizeof(buf));
                std::streamsize got = ifs.gcount();
                if (got > 0) {
                    if (!send_all(client_sock, buf, (size_t)got)) {
                        safe_print("[-] Connection lost while sending to " + client_id);
                        break;
                    }
                    sent += got;
                }
            }
            safe_print("[+] Sent " + filename + " (" + std::to_string(sent) + " bytes) to " + client_id);
        }
        else if (cmdline.rfind("UPLOAD ", 0) == 0) {
            // expected: UPLOAD filename filesize
            std::istringstream iss(cmdline);
            std::string cmd, filename;
            long long filesize;
            iss >> cmd >> filename >> filesize;
            if (filename.empty() || filesize <= 0) {
                std::string err = "ERROR Invalid UPLOAD header\n";
                send_all(client_sock, err.c_str(), err.size());
                continue;
            }
            if (filename.find("..") != std::string::npos) {
                std::string err = "ERROR Invalid filename\n";
                send_all(client_sock, err.c_str(), err.size());
                continue;
            }

            // get per-file mutex
            std::shared_ptr<std::mutex> fmutex;
	    {
    		std::lock_guard<std::mutex> lk(map_mtx);
    		if (file_mutex_map.find(filename) == file_mutex_map.end()) 
    		{
        		file_mutex_map[filename] = std::make_shared<std::mutex>();
    	    	}
    		fmutex = file_mutex_map[filename];
	    }
	    std::lock_guard<std::mutex> filelk(*fmutex);


            std::string tmpname = SHARED_DIR + "/." + filename + ".part";
            std::string finalname = SHARED_DIR + "/" + filename;

            std::ofstream ofs(tmpname, std::ios::binary | std::ios::trunc);
            if (!ofs) {
                std::string err = "ERROR Cannot create file\n";
                send_all(client_sock, err.c_str(), err.size());
                continue;
            }

            char buf[BUFFER_SIZE];
            long long remaining = filesize;
            bool fail = false;
            while (remaining > 0) {
                ssize_t toread = (remaining > BUFFER_SIZE) ? BUFFER_SIZE : (ssize_t)remaining;
                ssize_t r = recv(client_sock, buf, toread, 0);
                if (r <= 0) { fail = true; break; }
                ofs.write(buf, r);
                remaining -= r;
            }
            ofs.close();
            if (fail) {
                safe_print("[-] Upload failed or connection lost for " + client_id + " file: " + filename);
                // remove partial file if desired
                unlink(tmpname.c_str());
            } else {
                // atomic rename
                if (rename(tmpname.c_str(), finalname.c_str()) != 0) {
                    safe_print("[-] Rename failed for " + tmpname + " -> " + finalname + " errno=" + std::to_string(errno));
                    std::string err = "ERROR Server rename failed\n";
                    send_all(client_sock, err.c_str(), err.size());
                } else {
                    safe_print("[+] Received upload " + filename + " (" + std::to_string(filesize) + " bytes) from " + client_id);
                    std::string ok = "OK Uploaded\n";
                    send_all(client_sock, ok.c_str(), ok.size());
                }
            }
        }
        else if (cmdline == "EXIT") {
            safe_print("[*] Client requested EXIT: " + client_id);
            break;
        }
        else {
            std::string err = "ERROR Unknown command\n";
            send_all(client_sock, err.c_str(), err.size());
        }
    }
    close(client_sock);
    safe_print("[-] Handler closed for " + client_id);
}

int main() {
    int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in serv{};
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = INADDR_ANY;
    serv.sin_port = htons(8080);

    if (bind(listen_sock, (sockaddr*)&serv, sizeof(serv)) < 0) { perror("bind"); close(listen_sock); return 1; }
    if (listen(listen_sock, BACKLOG) < 0) { perror("listen"); close(listen_sock); return 1; }

    safe_print("[*] Threaded server listening on port 8080...");

    while (true) {
        sockaddr_in cli{};
        socklen_t len = sizeof(cli);
        int client_sock = accept(listen_sock, (sockaddr*)&cli, &len);
        if (client_sock < 0) { perror("accept"); continue; }
        char ipbuf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &cli.sin_addr, ipbuf, sizeof(ipbuf));
        std::string client_id = std::string(ipbuf) + ":" + std::to_string(ntohs(cli.sin_port));
        std::thread t(handle_client, client_sock, client_id);
        t.detach();
    }

    close(listen_sock);
    return 0;
}

