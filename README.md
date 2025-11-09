# 🚀 Network File Sharing System (Client–Server using C++ TCP Sockets)

### 🔗 Client uploads/downloads files from server over TCP  
✅ Supports multiple clients using threads  
✅ Thread-safe uploads using mutex  
✅ Runs on Linux (Ubuntu)

---

## 📌 Features

| Feature | Description |
|---------|-------------|
| **LIST** | Shows all files present on server |
| **UPLOAD \<filename>** | Uploads a file from client to server |
| **DOWNLOAD \<filename>** | Downloads file from server to client |
| **Multi-client support** | Every connected client gets its own thread |
| **Mutex lock** | Prevents file write conflicts during upload |

---

## 🧠 Tech Stack

| Component | Technology |
|----------|------------|
| Language | C++ (C++11) |
| OS | Ubuntu / Linux |
| Networking | TCP (Socket Programming) |
| Threading | std::thread |
| Synchronization | std::mutex |
| Build | GCC / g++ |

---

## 📁 Project Structure

socket-project/
│-- server.cpp
│-- client.cpp
│-- shared_files/ # uploaded & downloadable files exist here
│-- README.md


---

## ⚙️ How to Run the Project

### ✅ 1. Start the Server

```bash
g++ -std=c++11 -pthread server.cpp -o server
./server

[*] Connected to 127.0.0.1:8080

✅ Upload
Enter command: UPLOAD test.txt
[+] Upload successful! (52 bytes sent)

✅ Download
Enter command: DOWNLOAD test.txt
[+] Download completed!

✅ Server Log
[+] Client connected (127.0.0.1)
[UPLOAD] test.txt received (52 bytes)

🔐 Thread Safety

To avoid simultaneous write conflicts, the server uses:

std::mutex fileMutex;


Only one client can upload the same file at a time.

🚀 Future Enhancements

🔹 Add user authentication (username/password)
🔹 Add AES file encryption
🔹 Add GUI client (React/Qt Desktop App)
