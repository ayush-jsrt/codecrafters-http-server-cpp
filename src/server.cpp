#include <iostream>
#include <cstdlib>
#include <string>
#include <string_view>
#include <sstream>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <vector>
#include <map>
#include <cstdio>
#include <optional>
#include <unordered_map>
#include <fstream>
#include <filesystem>
const std::string STATUSLINE_OK { "HTTP/1.1 200 OK" };
const std::string STATUSLINE_NOT_FOUND { "HTTP/1.1 404 Not Found" };
const std::string STATUSLINE_201 { "HTTP/1.1 201 Created" };
const std::string CRLF = "\r\n";
bool server_running = true;
struct HandleClientArgs {
  std::string directory;
  int client_socket;
  sockaddr_in client_addr;
};
struct ClientRequest {
  enum Method {
    GET,
    POST
  };
  std::string request_line;
  std::string path;
  std::map<std::string, std::string> headers;
  std::string content;
  Method method;
  void setHeader(const std::string& key, const std::string& value) {
    std::cout << "Adding header, value to request:" << key << ", " << value <<std::endl;
    headers[key] = value;
  }
  bool containsHeader(const std::string& key) const{
    return headers.contains(key);
  }
  
  std::string getHeader(const std::string& key) const{
    return headers.at(key);
  }
  static Method stringToMethod(const std::string& key) {
    const std::unordered_map<std::string, Method> translation = {
      {"GET", GET},
      {"POST", POST}
    };
    return translation.at(key);
  }
  bool isGet() const {
    return method == GET;
  }
  
  bool isPost() const {
    return method == POST;
  }
};
class ServerResponse {
private:
  std::string status_line_;
  std::map<std::string, std::string> headers_;
  std::string body_;
  std::string suffix_;
public:
  explicit ServerResponse(const std::string& status_line) :
      status_line_(status_line),
      headers_(),
      body_(),
      suffix_(CRLF) {}
  ServerResponse() {};
  void setStatusLine(const std::string_view status_line) {
    status_line_ = status_line;
  }
  void setHeader(const std::string& key, const std::string& value) {
    headers_[key] = value;
  }
  
  void setBody(const std::string_view body) {
    body_ = body;
  }
  std::string formattedStatusLine() {
    return status_line_;
  }
  std::string formattedHeaders() {
    std::string res{};
    for (const auto& pair : headers_) {
      res.append(pair.first + ": " + pair.second);
      res.append(CRLF);
    }
    return res;
  }
  std::string formattedBody() {
    return body_;
  }
  std::string formattedResponse() {
    return formattedStatusLine() + CRLF + formattedHeaders() + CRLF + formattedBody();
  }
  void sendTo(int socket) {
    std::string fr { formattedResponse() };
    const char *msg_buf = fr.c_str(); // fr must exist for pointer to not dangle
    send(socket, (const void *) msg_buf, strlen(msg_buf), 0);
  }
};
void log_sockaddr(const sockaddr_in& addr) {
  std::cout << "Client Address family: " << addr.sin_family << std::endl;
  std::cout << "Client IP Adress: " << inet_ntoa(addr.sin_addr) << std::endl;
  std::cout << "Client Port Number with htons: " << ntohs(addr.sin_port) <<
    std::hex << " | 0x" << ntohs(addr.sin_port) << std::dec << std::endl;
  std::cout << "Client Port Number w/o htons : " << addr.sin_port <<
    std::hex << " | 0x" << addr.sin_port << std::dec << std::endl;
}
std::unordered_map<std::string, std::string> parse_args(int argc, char* argv[]) {
  std::unordered_map<std::string, std::string> res{};
  for (int i = 1; i < argc; ++i) {
    std::string arg{ argv[i] };
    if (arg.substr(0, 2) == "--") {
      // Represents a flag
      res[arg.substr(2)] = argv[++i];
    }
  }
  return res;
}
std::vector<std::string> split(const std::string& s, char delimiter) {
  std::vector<std::string> tokens;
  std::istringstream iss{ s };
  std::string token;
  while (std::getline(iss, token, delimiter)) {
    if (!token.empty()) {
      tokens.push_back(token);
    }
  }
  return tokens;
}
std::optional<ServerResponse> provide_response(const ClientRequest& req, std::string directory) {
  char path_sep = '/';
  ServerResponse resp{};
  std::vector<std::string> path_segments = split(req.path, path_sep);
  if (path_segments.size() == 0) {
    resp.setStatusLine(STATUSLINE_OK);
  } else if (path_segments.size() == 2 && path_segments[0] == "echo") {
    std::string echo_arg{ path_segments[1] };
    resp.setStatusLine(STATUSLINE_OK);
    resp.setHeader("Content-Type", "text/plain");
    resp.setHeader("Content-Length", std::to_string(echo_arg.length()));
    resp.setBody(echo_arg);
    if (req.containsHeader("Accept-Encoding") && req.getHeader("Accept-Encoding") == "gzip") {
      resp.setHeader("Content-Encoding", "gzip");
    }
  } else if (path_segments.size() == 1 && path_segments[0] == "user-agent") {
    std::string user_agent_arg{ req.getHeader("User-Agent") };
    resp.setStatusLine(STATUSLINE_OK);
    resp.setHeader("Content-Type", "text/plain");
    resp.setHeader("Content-Length", std::to_string(user_agent_arg.length()));
    resp.setBody(user_agent_arg);
  } else if (path_segments.size() == 2 && path_segments[0] == "files" && req.isGet()) {
    std::filesystem::path dir{ directory };
    std::filesystem::path filename{ path_segments[1] };
    std::filesystem::path file_path{ dir / filename };
    if (std::filesystem::exists(file_path)) {
      std::ifstream file{ file_path };
      std::stringstream file_buf{};
      file_buf << file.rdbuf();
      std::string content{ file_buf.str() };
      resp.setStatusLine(STATUSLINE_OK);
      resp.setHeader("Content-Type", "application/octet-stream");
      resp.setHeader("Content-Length", std::to_string(content.length()));
      resp.setBody(content);
    } else {
      resp.setStatusLine(STATUSLINE_NOT_FOUND);
    }
  } else if (path_segments.size() == 2 && path_segments[0] == "files" && req.isPost() ) {
    std::filesystem::path dir{ directory };
    std::filesystem::path filename{ path_segments[1] };
    std::filesystem::path file_path{ dir / filename };
    std::ofstream file{ file_path };
    file << req.content;
    file.close();
    resp.setStatusLine(STATUSLINE_201);
  } else {
    resp.setStatusLine(STATUSLINE_NOT_FOUND);
  }
  std::cout << "Sending The Following Response:" << std::endl;
  std::cout << resp.formattedResponse() << std::endl;
  return resp;
}
std::optional<ClientRequest> parse_message(std::string_view complete_message) {
  ClientRequest req{};
  if (complete_message.empty()) {
    std::cerr << "Message empty" << std::endl;
    return std::nullopt;
  }
  
  // Get request line
  size_t crlf_pos = complete_message.find("\r\n");
  if (crlf_pos == std::string::npos) {
    std::cerr << "Malformed request" << std::endl;
    return std::nullopt;
  } 
  req.request_line = complete_message.substr(0, crlf_pos);
  // Get path
  std::string path;
  size_t start_pos = req.request_line.find(' ');
  if (start_pos == std::string::npos) {
    std::cerr << "Could not find path start" << std::endl;
    return std::nullopt;
  }
  ClientRequest::Method method{
    ClientRequest::stringToMethod(req.request_line.substr(0, start_pos)) };
  req.method = method;
  
  path = req.request_line.substr(start_pos + 1);
  std::cout << "Path start:" << path << std::endl;
  size_t end_pos = path.find(' ');
  if (end_pos == std::string::npos) {
    std::cerr << "Could not find path end" << std::endl;
    return std::nullopt;
  }
  path = path.substr(0, end_pos);
  std::cout << "Path found: " << path << std::endl;
  req.path = path;
  // Get headers
  std::string rem_message{ complete_message.substr(crlf_pos + CRLF.size()) };
  size_t next_crlf{ rem_message.find(CRLF) };
  while (next_crlf != std::string::npos && next_crlf != 0) {
    const std::string curr_line{ rem_message.substr(0, next_crlf) };
    size_t split_pos = curr_line.find(": ");
    req.setHeader(curr_line.substr(0, split_pos), curr_line.substr(split_pos + 2));
    rem_message = rem_message.substr(next_crlf + CRLF.size());
    next_crlf = rem_message.find(CRLF);
  }
  // Get content
  req.content = rem_message.substr(CRLF.length());
  return req;
}
void handle_client(const HandleClientArgs& args) {
  // Does everything that needs to be done when a client is accepted
  std::string directory = args.directory;
  int client_socket = args.client_socket;
  sockaddr_in client_addr = args.client_addr;
  // Log Connection Information 
  log_sockaddr(client_addr);
  // Receiving from client
  const int BUFFER_SIZE = 1024;
  std::vector<char> request_buf(BUFFER_SIZE);
  std::string complete_message{};
  ssize_t bytes_received;
  bytes_received = recv(client_socket, request_buf.data(), request_buf.size(), 0);
  if (bytes_received <= 0) {
    std::cerr << "Recv error" << std::endl;
    close(client_socket);
    return;
  }
  complete_message.append(request_buf.data(), bytes_received);
  std::cout << "Complete Message:\n" << complete_message << std::endl;
  // Process Request To Get Command
  std::optional<ClientRequest> req = parse_message(complete_message);
  if (!req.has_value()) {
    std::cerr << "Could not parse message" << std::endl;
    close(client_socket);
    return;
  }
  // Do Command
  std::optional<ServerResponse> response = provide_response(*req, directory);
  if (!response.has_value()) {
    close(client_socket);
    return;
  }
  response->sendTo(client_socket);
  // Close Connection
  close(client_socket);
}
int main(int argc, char **argv) {
  // You can use print statements as follows for debugging, they'll be visible when running tests.
  std::cout << "Logs from your program will appear here!\n";
  std::unordered_map<std::string, std::string> arg_map{ parse_args(argc, argv) };
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
  while(server_running) {
    int client_socket = accept(server_fd, (struct sockaddr *) &client_addr,
                               (socklen_t *) &client_addr_len);
    if (client_socket <= 0) {
      std::cerr << "Socket non-positive: " << client_socket << std::endl;
      continue;
    }
    if (fork() == 0) {
      HandleClientArgs client_arg{
        arg_map["directory"],
        client_socket,
        client_addr
      };
      handle_client(client_arg);
      exit(0);
    } else {
      close(client_socket);
    }
  }
  return 0;
}