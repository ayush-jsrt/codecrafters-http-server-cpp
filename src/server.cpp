#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <vector>
#include <sstream>
#include <unordered_map>
#include <thread>
#include <fstream>
#include <filesystem>
namespace fs = std::filesystem;
struct HttpRequest {
  std::string method;
  std::string path;
  std::string version;
  std::string body;
  std::unordered_map<std::string, std::string> headers;
};
struct HttpResponse {
  std::string version;
  std::string status;
  std::string statusString;
  std::unordered_map<std::string, std::string> headers;
  std::string body;
};
std::string makeHttpResponse(HttpResponse response) {
  std::ostringstream out;
  const char* sep = " ";
  const char* crlf = "\r\n";
  out << response.version << sep << response.status << sep << response.statusString << crlf;
  for(auto it = response.headers.begin(); it != response.headers.end(); it++) {
    out << it->first << ": " << it->second << crlf;
  }
  out << crlf << response.body << crlf;
  return out.str();
}
HttpRequest parseHttpRequest(const std::string request) {
  HttpRequest httpRequest;
  std::istringstream requestStream(request);
  std::string line;
  std::getline(requestStream, line);
  std::istringstream lineStream(line);
  lineStream >> httpRequest.method >> httpRequest.path >> httpRequest.version;
  while (std::getline(requestStream, line) && line != "\r") {
    auto colonPos = line.find(':');
    auto crPos = line.find_last_of('\r');
    if (colonPos != std::string::npos) {
      std::string headerName = line.substr(0, colonPos);
      std::string headerValue = line.substr(colonPos + 2);
      auto crPos = headerValue.find_last_of('\r');
      headerValue = headerValue.substr(0, crPos);
      httpRequest.headers[headerName] = headerValue;
    }
  }
  while (std::getline(requestStream, line)) {
    httpRequest.body += line;
  }
  return httpRequest;
}
int readFile(const std::string filePath, std::string &body) {
  std::ifstream infile(filePath);
  infile.seekg(0, infile.end);
  long fileSize = infile.tellg();
  infile.seekg(0, infile.beg);
  char* buffer = new char[fileSize];
  infile.read(buffer, fileSize);
  body = std::string(buffer);
  delete buffer;
  infile.close();
  return fileSize;
}
void handle_client(const int socket_fd, const std::string &directory) {
  if(socket_fd < 0) {
    std::cerr << "Socket not connected\n";
    return;
  }
  std::cout << "Socket Connected\n";
  char buffer[2048] = {0};
  ssize_t bytes_received = 0;
  bytes_received = read(socket_fd, buffer, sizeof(buffer));
  if (bytes_received < 0) {
    std::cerr << "Failed to receive data\n";
    close(socket_fd);
    return;
  }
  buffer[bytes_received] = '\0';
  HttpRequest request = parseHttpRequest(std::string(buffer));
  HttpResponse response;
  response.version = "HTTP/1.1";
  if (request.path == "/") {
    response.status = "200";
    response.statusString = "OK";
  } else if (request.path.rfind("/echo", 0) == 0) {
    std::string rbody = request.path.substr(6);
    response.headers["Content-Type"] = "text/plain"; 
    response.headers["Content-Length"] = std::to_string(rbody.size());
    response.status = "200";
    response.statusString = "OK";
    response.body = rbody;
  } else if(request.path == "/user-agent") {
    std::string rbody = request.headers["User-Agent"];
    response.headers["Content-Type"] = "text/plain"; 
    response.headers["Content-Length"] = std::to_string(rbody.size());
    response.status = "200";
    response.statusString = "OK";
    response.body = rbody;
  } else if(request.path.rfind("/files/", 0) == 0) {
    std::string rbody = request.path.substr(7);
    std::string filePath = directory + rbody;
    if(!fs::exists(fs::path(filePath)) && request.method == "GET") {
      std::cerr << "File does not exist\n";
      response.status = "404";
      response.statusString = "Not Found";
    } else if(request.method == "GET") {
      std::string respBody;
      long fileSize = readFile(filePath, respBody);
      std::stringstream ss;
      ss << fileSize;
      response.status = "200";
      response.statusString = "OK";
      response.headers["Content-Length"] = ss.str();
      response.headers["Content-Type"] = "application/octet-stream";
      response.body = respBody;
    } else if(request.method == "POST") {
      std::ofstream outfile(filePath);
      outfile << request.body;
      outfile.close();
      response.status = "201";
      response.statusString = "Created";
    }
  } else {
    response.status = "404";
    response.statusString = "Not Found";
  }
  std::string http_response = makeHttpResponse(response);
  ssize_t bytes_sent = send(socket_fd, http_response.c_str(), http_response.size(), 0);
  if (bytes_sent < 0) {
    std::cerr << "Failed to send data\n";
    close(socket_fd);
    return;
  }
  close(socket_fd);
  return;
}
int main(int argc, char **argv) {
  // You can use print statements as follows for debugging, they'll be visible when running tests.
  std::cout << "Logs from your program will appear here!\n";
  std::cout << argv[2] << "\n";
  const std::string directory = std::string(argv[2]);
  std::cout << "[DIR] " << directory << "\n";
  // Uncomment this block to pass the first stage
  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
   std::cerr << "Failed to create server socket\n";
   return 1;
  }
  // Since the tester restarts your program quite often, setting REUSE_PORT
  // ensures that we don't run into 'Address already in use' errors
  int reuse = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) < 0) {
    std::cerr << "setsockopt failed\n";
    return 1;
  }
  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(4221);
  
  if (bind(server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) != 0) {
    std::cerr << "Failed to bind to port 4221\n";
    return 1;
  }
 
  int connection_backlog = 5;
  if (listen(server_fd, connection_backlog) != 0) {
    std::cerr << "listen failed\n";
    return 1;
  }
  
  struct sockaddr_in client_addr;
  int client_addr_len = sizeof(client_addr);
  
  std::cout << "Waiting for a client to connect...\n";
  while (true) {
    int socket_fd = accept(server_fd, (struct sockaddr *) &client_addr, (socklen_t *) &client_addr_len);
    if  (socket_fd < 0) {
      std::cerr << "failed to connect the client\n";
      return 1;
    }
    std::thread thread_obj(handle_client, socket_fd, directory);
    thread_obj.detach();
  }
  close(server_fd);
  return 0;
}