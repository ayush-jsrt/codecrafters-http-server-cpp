#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include "request.hpp"
#include "response.hpp"
#include "server.hpp"
#include <fstream>
#include <thread>
#include <regex>
#include <zlib.h>
void handle_request(int client, const std::string directory) {
  std::string request(1024, '\0');
  recv(client, &request[0], request.length(), 0);
  Request req(request);
  Response response;
  if (req.line.method == "GET")
  {
    if (req.line.target.size() == 1 && req.line.target == "/")
    {
      response.line.set_status_code(200);
    }
    else if (req.line.target.size() >= 6 && req.line.target.substr(0, 6) == "/echo/")
    {
      response.line.set_status_code(200);
      response.headers.add_update_header("Content-Type", "text/plain");
      response.body = req.line.target.substr(6);
      auto it = req.header.headers.find("Accept-Encoding");
      if (it != req.header.headers.end()) {
        // Check if "gzip" is present in "Accept-Encoding"
        if (std::regex_search(it->second, std::regex("gzip"))) {
          // Update the response header
          response.headers.add_update_header("Content-Encoding", "gzip");
          std::cout << "Updated Content-Encoding to gzip" << std::endl;
        }
      }
      response.body = req.line.target.substr(6);
      response.headers.add_update_header("Content-Length", std::to_string(response.body.size()));
    }
    else if (req.line.target == "/user-agent")
    {
      response.line.set_status_code(200);
      response.headers.add_update_header("Content-Type", "text/plain");
      response.body = req.header.headers.at("User-Agent");
      response.headers.add_update_header("Content-Length", std::to_string(response.body.size()));
    }
    else if (req.line.target.size() >= 6 && req.line.target.substr(0, 7) == "/files/")
    {
      std::ifstream file(directory + req.line.target.substr(7));
      if (file.is_open())
      {
        response.line.set_status_code(200);
        response.headers.add_update_header("Content-Type", "application/octet-stream");
        response.body.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        response.headers.add_update_header("Content-Length", std::to_string(response.body.size()));
      }
      else
      {
        response.line.set_status_code(404);
      }
    }
    else {
      response.line.set_status_code(404);
    }
  }
  else if (req.line.method == "POST")
  {
    if (req.line.target.size() >= 6 && req.line.target.substr(0, 7) == "/files/")
    {
      std::ofstream file(directory + req.line.target.substr(7));
      if (file.is_open())
      {
        response.line.set_status_code(201);
        file.write(req.body.c_str(), std::stoi(req.header.headers.at("Content-Length")));
      }
      else
      {
        response.line.set_status_code(404);
      }
    }
  }
  const char* resp = response.c_str();
  send(client, resp, strlen(resp), 0);
  close(client);
  return;
}
int main(int argc, char** argv) {
  auto args = arg_parser(argc, argv);
  std::string directory = "";
  if (args.find("directory") != args.end())
  {
    directory = args.at("directory");
    std::cout << "Directory initialized at " << directory << std::endl;
  }
  else
  {
    directory = ".\\";
    std::cout << "Directory initialized to current source" << std::endl;
  }
  // You can use print statements as follows for debugging, they'll be visible when running tests.
  std::cout << "Logs from your program will appear here!\n";
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
  if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) != 0) {
    std::cerr << "Failed to bind to port 4221\n";
    return 1;
  }
  int connection_backlog = 5;
  if (listen(server_fd, connection_backlog) != 0) {
    std::cerr << "listen failed\n";
    return 1;
  }
  std::cout << "Waiting for a client to connect...\n";
  while (true)
  {
    struct sockaddr_in client_addr;
    int client_addr_len = sizeof(client_addr);
    int client = accept(server_fd, (struct sockaddr*)&client_addr, (socklen_t*)&client_addr_len);
    std::thread client_req(handle_request, client, directory);
    client_req.detach();
    //handle_request(client, directory);
  }
  close(server_fd);
  return 0;