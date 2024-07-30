#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <algorithm>
#include <charconv>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <ranges>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
namespace http {
struct case_insensitive_compare {
  using is_transparent = void;
  bool operator()(std::string_view lhs, std::string_view rhs) const {
    return std::ranges::lexicographical_compare(lhs, rhs,
                                                [](char a, char b) { return std::tolower(a) < std::tolower(b); });
  }
};
using header_map = std::map<std::string, std::string, case_insensitive_compare>;
struct request {
  std::string method;
  std::string path;
  std::string version;
  header_map headers;
  std::vector<char> body;
};
struct response {
  std::string version;
  int status_code;
  std::string status_message;
  header_map headers;
  std::vector<char> body;
};
template <typename T>
concept readable = requires(T reader, char *buffer, size_t size) {
  { reader(buffer, size) } -> std::convertible_to<ssize_t>;
};
template <size_t N> struct char_buffer : std::ranges::view_interface<char_buffer<N>> {
  static constexpr auto capacity = N;
  char _bytes[N]{};
  size_t _size{};
  char_buffer() = default;
  char_buffer(char_buffer &&) = default;
  char_buffer(const char_buffer &) = default;
  auto data() const { return _bytes; }
  auto size() const { return _size; }
  auto begin() const { return _bytes; }
  auto end() const { return _bytes + _size; }
  auto begin() { return _bytes; }
  auto end() { return _bytes + _size; }
  template <readable T> ssize_t read_from(T &&r) {
    ssize_t read_bytes = r(_bytes + _size, N - _size);
    if (read_bytes > 0) {
      _size += read_bytes;
    }
    return read_bytes;
  }
  void consume(size_t s) {
    std::move(_bytes + s, _bytes + _size, _bytes);
    _size -= s;
  }
  void clear() { _size = 0; }
};
template <readable T> struct buffered_reader {
  T reader;
  char_buffer<1024> buffer{};
  /**
   * @brief Reads one line from the reader.
   *
   * @param crlf whether the line should end with CRLF or just LF
   * @return std::optional<std::string> nullopt if the reader is closed, the line otherwise
   */
  std::optional<std::string> read_line(bool crlf = true) {
    std::string line{};
    if (auto it = std::ranges::find(buffer, '\n'); it != std::ranges::end(buffer)) {
      line.insert(line.end(), std::ranges::begin(buffer), it);
      buffer.consume(std::distance(std::ranges::begin(buffer), it) + 1);
    } else {
      // reads more bytes
      do {
        line.insert(line.end(), std::ranges::begin(buffer), std::ranges::end(buffer));
        buffer.clear();
        ssize_t bytes_read = buffer.read_from(reader);
        if (bytes_read <= 0) {
          return std::nullopt;
        }
        auto it = std::ranges::find(buffer, '\n');
        if (it != std::ranges::end(buffer)) {
          line.insert(line.end(), std::ranges::begin(buffer), it);
          buffer.consume(std::distance(std::ranges::begin(buffer), it) + 1);
          break;
        }
      } while (true);
    }
    if (crlf && (!line.empty() && line.back() == '\r')) {
      line.pop_back();
    }
    return line;
  }
  template <typename It> ssize_t read_n_bytes(It out, ssize_t n) {
    ssize_t total{};
    while (total < n) {
      if (buffer.size() == 0) {
        if (buffer.read_from(reader) <= 0) {
          break;
        }
      }
      size_t copied_bytes = std::min<size_t>(buffer.size(), n - total);
      std::copy(std::ranges::begin(buffer), std::ranges::begin(buffer) + copied_bytes, out);
      total -= copied_bytes;
    }
    return total;
  }
  ssize_t operator()(char *out, size_t out_size) {
    if (buffer.size() == 0) {
      ssize_t bytes_read = buffer.read_from(reader);
      if (bytes_read <= 0) {
        return bytes_read;
      }
    }
    size_t copied_bytes = std::min<size_t>(buffer.size(), out_size);
    std::copy_n(buffer.begin(), copied_bytes, out);
    buffer.consume(copied_bytes);
    return copied_bytes;
  }
};
template <size_t N> std::array<std::string_view, N> split_by_char(std::string_view sv, char c) {
  size_t start{}, count{};
  std::array<std::string_view, N> result{};
  for (size_t i = 0; i < sv.size() && count + 1 < N; i++) {
    if (sv[i] == c) {
      result[count++] = sv.substr(start, i - start);
      start = i + 1;
    }
  }
  result.back() = sv.substr(start);
  return result;
}
std::string_view trim(std::string_view sv) {
  while (!sv.empty() && std::isspace(sv.front())) {
    sv.remove_prefix(1);
  }
  while (!sv.empty() && std::isspace(sv.back())) {
    sv.remove_suffix(1);
  }
  return sv;
}
std::string_view ltrim(std::string_view sv) {
  while (!sv.empty() && std::isspace(sv.front())) {
    sv.remove_prefix(1);
  }
  return sv;
}
std::vector<char> to_bytes(std::string_view sp) {
  std::vector<char> result;
  result.insert(result.end(), sp.begin(), sp.end());
  return result;
}
bool starts_with(std::string_view sv, std::string_view prefix) {
  return sv.size() >= prefix.size() && sv.substr(0, prefix.size()) == prefix;
}
void process(const request &req, response &resp) {
  using namespace std::string_view_literals;
  if (req.path == "/"sv) {
    resp.body = to_bytes("Hello, World!\n"sv);
  } else if (starts_with(req.path, "/echo/"sv)) {
    resp.headers.emplace("Content-Type", "text/plain");
    resp.body = to_bytes(req.path.substr(6));
  } else if (starts_with(req.path, "/user-agent"sv)) {
    auto user_agent = req.headers.find("user-agent"sv);
    if (user_agent != req.headers.end()) {
      resp.headers.emplace("Content-Type", "text/plain");
      resp.body = to_bytes(user_agent->second);
    } else {
      resp.status_code = 400;
      resp.status_message = "Bad Request";
      resp.body = to_bytes("User-Agent header not found\n"sv);
    }
  } else if (starts_with(req.path, "/files/"sv)) {
    auto path = req.path.substr(7);
    std::ifstream file(path, std::ios::binary);
    if (file.is_open()) {
      resp.headers.emplace("Content-Type", "application/octet-stream");
      resp.body.insert(resp.body.end(), std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    } else {
      resp.status_code = 404;
      resp.status_message = "Not Found";
      resp.body = to_bytes("File not found\n"sv);
    }
  } else {
    resp.status_code = 404;
    resp.status_message = "Not Found";
    resp.body = to_bytes("Not Found!\n"sv);
  }
}
int serve(int client_fd) {
  std::cout << "New connection established (" << client_fd << ")\n";
  buffered_reader reader{[client_fd](char *buffer, size_t size) { return recv(client_fd, buffer, size, 0); }};
  const auto send_n_bytes = [client_fd](char const *bytes, size_t n) {
    size_t off{};
    while (off < n) {
      ssize_t sent = send(client_fd, bytes + off, n - off, 0);
      if (sent <= 0) {
        return off;
      }
      off += sent;
    }
    return off;
  };
  while (true) {
    request req{};
    // reads the request line
    if (auto request_line = reader.read_line(); request_line) {
      if (request_line->empty()) {
        // empty line indicates the end of the request
        break;
      }
      auto [method, path, version] = split_by_char<3>(*request_line, ' ');
      req.method = method;
      req.path = path;
      req.version = version;
    } else {
      break;
    }
    // reads headers
    bool invalid_request = false;
    while (true) {
      auto header_line = reader.read_line();
      if (!header_line) {
        invalid_request = true;
        break;
      }
      if (header_line->empty()) {
        // empty line indicates the end of headers
        break;
      }
      auto [name, value] = split_by_char<2>(*header_line, ':');
      req.headers.emplace(name, ltrim(value));
    }
    if (invalid_request) {
      break;
    }
    // reads the body, if necessary
    using namespace std::string_view_literals;
    auto content_length = req.headers.find("content-length"sv);
    auto transfer_encoding = req.headers.find("transfer-encoding"sv);
    // parses the body, according to the transfer encoding or the content length.
    if (transfer_encoding != req.headers.end()) {
    } else if (content_length != req.headers.end()) {
      try {
        size_t body_size = std::stoi(content_length->second);
        if (reader.read_n_bytes(std::back_inserter(req.body), body_size) < body_size) {
          // incomplete body
          break;
        }
      } catch (const std::exception &e) {
        // invalid content length
        std::cerr << "Failed to parse content length\n";
        break;
      }
    }
    response resp{
        .version = "HTTP/1.1",
        .status_code = 200,
        .status_message = "OK",
        .headers = {{"Connection", "Keep-Alive"}},
        .body = {},
    };
    process(req, resp);
    std::vector<char> resp_bytes;
    const auto append_bytes = [&resp_bytes](std::span<char const> bytes) {
      resp_bytes.insert(resp_bytes.end(), bytes.begin(), bytes.end());
    };
    // encodes the response
    std::string resp_line = resp.version + " " + std::to_string(resp.status_code) + " " + resp.status_message + "\r\n";
    append_bytes(resp_line);
    auto content_length_header = resp.headers.find("content-length"sv);
    if (content_length_header == resp.headers.end()) {
      resp.headers.emplace("Content-Length", std::to_string(resp.body.size()));
    }
    for (const auto &[name, value] : resp.headers) {
      std::string header_line = name + ": " + value + "\r\n";
      append_bytes(header_line);
    }
    append_bytes("\r\n"sv);
    append_bytes(resp.body);
    // sends the response
    if (send_n_bytes(resp_bytes.data(), resp_bytes.size()) < resp_bytes.size()) {
      break;
    }
    std::cout << resp.status_code << ' ' << req.method << ' ' << req.path << ' ' << req.version << std::endl;
    auto connection_header = req.headers.find("connection"sv);
    if (connection_header != req.headers.end() && connection_header->second == "close"sv) {
      break;
    }
  }
  close(client_fd);
  return 0;
}
} // namespace http
int main(int argc, char **argv) {
  signal(SIGPIPE, SIG_IGN);
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
  if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0) {
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
  for (int i = 0; i < argc; ++i) {
    using namespace std::string_view_literals;
    if (argv[i] == "--directory"sv && i < argc - 1) {
      chdir(argv[i + 1]);
    }
  }
  std::cout << "Waiting for a client to connect...\n";
  while (true) {
    int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, (socklen_t *)&client_addr_len);
    if (client_fd >= 0) {
      std::thread t(http::serve, client_fd);
      t.detach();
    } else if (errno == EINTR) {
      continue;
    } else {
      std::cerr << "accept failed\n";
      break;
    }
  }
  close(server_fd);
  return 0;
}