// client.cpp
// Compile: g++ -std=c++11 client.cpp -o client

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

const int BUFFER_SIZE = 8192;

bool send_all(int sock, const char* data, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t s = send(sock, data + total, len - total, 0);
        if (s <= 0) return false;
        total += s;
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

int main() {
    const char* server_ip = "127.0.0.1"; // change if server is remote
    int port = 8080;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    sockaddr_in serv{};
    serv.sin_family = AF_INET;
    serv.sin_port = htons(port);
    serv.sin_addr.s_addr = inet_addr(server_ip);

    if (connect(sock, (sockaddr*)&serv, sizeof(serv)) < 0) { perror("connect"); close(sock); return 1; }
    std::cout << "[*] Connected to " << server_ip << ":" << port << "\n";

    while (true) {
        std::cout << "\nEnter command (LIST | DOWNLOAD <filename> | UPLOAD <filename> | EXIT): ";
        std::string line;
        std::getline(std::cin, line);
        if (line.empty()) continue;

        // We'll always send commands terminated by '\n'
        std::string wire = line + "\n";
        if (!send_all(sock, wire.c_str(), wire.size())) { std::cerr << "Send failed\n"; break; }

        if (line == "LIST") {
            char buf[BUFFER_SIZE];
            ssize_t r = recv(sock, buf, sizeof(buf)-1, 0);
            if (r <= 0) { std::cerr << "Receive failed\n"; break; }
            buf[r] = '\0';
            std::cout << "\nFiles on server:\n" << buf << "\n";
        }
        else if (line.rfind("DOWNLOAD ", 0) == 0) {
            // read header line
            std::string header;
            if (!recv_line(sock, header)) { std::cerr << "Header read failed\n"; break; }
            if (header.rfind("ERROR", 0) == 0) { std::cout << "Server: " << header << "\n"; continue; }
            if (header.rfind("FILESIZE ", 0) != 0) { std::cout << "Unexpected header: " << header << "\n"; continue; }
            long long filesize = atoll(header.substr(9).c_str());
            std::string filename = line.substr(9);
            std::cout << "Downloading " << filename << " (" << filesize << " bytes)\n";
            std::ofstream ofs(filename, std::ios::binary);
            if (!ofs) {
                std::cerr << "Cannot open local file for writing\n";
                // consume and discard bytes
                char tmp[BUFFER_SIZE];
                long long rem = filesize;
                while (rem > 0) {
                    ssize_t toread = (rem > BUFFER_SIZE) ? BUFFER_SIZE : (ssize_t)rem;
                    ssize_t r = recv(sock, tmp, toread, 0);
                    if (r <= 0) break;
                    rem -= r;
                }
                continue;
            }
            long long recvd = 0;
            char buf[BUFFER_SIZE];
            while (recvd < filesize) {
                ssize_t toread = (filesize - recvd > BUFFER_SIZE) ? BUFFER_SIZE : (ssize_t)(filesize - recvd);
                ssize_t r = recv(sock, buf, toread, 0);
                if (r <= 0) { std::cerr << "Connection lost during download\n"; break; }
                ofs.write(buf, r);
                recvd += r;
            }
            ofs.close();
            if (recvd == filesize) std::cout << "Download finished\n"; else std::cout << "Download incomplete\n";
        }
        else if (line.rfind("UPLOAD ", 0) == 0) {
            std::string filename = line.substr(7);
            if (filename.empty()) { std::cout << "Specify filename\n"; continue; }
            std::ifstream ifs(filename, std::ios::binary);
            if (!ifs) { std::cout << "Local file not found: " << filename << "\n"; continue; }
            ifs.seekg(0, std::ios::end);
            long long filesize = ifs.tellg();
            ifs.seekg(0);
            // send header "UPLOAD filename filesize\n"
            std::ostringstream hdr;
            hdr << "UPLOAD " << filename << " " << filesize << "\n";
            std::string hdrs = hdr.str();
            if (!send_all(sock, hdrs.c_str(), hdrs.size())) { std::cerr << "Header send failed\n"; ifs.close(); break; }

            // send file bytes
            char buf[BUFFER_SIZE];
            long long sent = 0;
            while (ifs) {
                ifs.read(buf, sizeof(buf));
                std::streamsize got = ifs.gcount();
                if (got > 0) {
                    if (!send_all(sock, buf, (size_t)got)) { std::cerr << "Send failed\n"; break; }
                    sent += got;
                }
            }
            ifs.close();

            // read server response
            std::string resp;
            if (!recv_line(sock, resp)) { std::cerr << "No response after upload\n"; break; }
            std::cout << "Server: " << resp << "\n";
        }
        else if (line == "EXIT") {
            std::cout << "Closing connection\n";
            break;
        }
        else {
            std::string resp;
            if (!recv_line(sock, resp)) { std::cerr << "No response\n"; break; }
            std::cout << "Server: " << resp << "\n";
        }
    }

    close(sock);
    return 0;
}

