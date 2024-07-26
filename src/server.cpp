#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fstream>
#include <sstream>

int main(int argc, char **argv) {
    // Flush after every std::cout / std::cerr
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    // Create server socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Failed to create server socket\n";
        return 1;
    }

    // Set socket options
    int reuse = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        std::cerr << "setsockopt failed\n";
        return 1;
    }

    // Bind the socket
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(4221);

    if (bind(server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) != 0) {
        std::cerr << "Failed to bind to port 4221\n";
        return 1;
    }

    // Listen for connections
    int connection_backlog = 5;
    if (listen(server_fd, connection_backlog) != 0) {
        std::cerr << "listen failed\n";
        return 1;
    }

    struct sockaddr_in client_addr;
    int client_addr_len = sizeof(client_addr);

    std::cout << "Waiting for a client to connect...\n";

    // Accept a client connection
    int client = accept(server_fd, (struct sockaddr *) &client_addr, (socklen_t *) &client_addr_len);
    if (client < 0) {
        std::cerr << "Failed to accept client connection\n";
        return 1;
    }
    std::cout << "Client connected\n";

    // Read the request
    char buffer[1024];
    memset(buffer, 0, sizeof(buffer));
    recv(client, buffer, sizeof(buffer) - 1, 0);

    std::string request(buffer);
    std::cout << "Received request: " << request << "\n";

    // Parse the request line
    std::istringstream request_stream(request);
    std::string method, path, http_version;
    request_stream >> method >> path >> http_version;

    // Validate HTTP method
    if (method != "GET") {
        std::string response = "HTTP/1.1 405 Method Not Allowed\r\n\r\n";
        send(client, response.c_str(), response.length(), 0);
        close(client);
        close(server_fd);
        return 0;
    }

    // Attempt to open the requested file
    std::ifstream file(path.substr(1)); // Remove leading slash from path
    if (file) {
        std::string response = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n";
        std::string line;
        while (std::getline(file, line)) {
            response += line + "\n";
        }
        send(client, response.c_str(), response.length(), 0);
    } else {
        std::string response = "HTTP/1.1 404 Not Found\r\n\r\n";
        response += "The requested resource " + path + " was not found on this server.";
        send(client, response.c_str(), response.length(), 0);
    }

    // Close the client and server sockets
    close(client);
    close(server_fd);

    return 0;
}
