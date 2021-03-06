/// \file socket.cpp
/// A handy Socket wrapper library.
/// Written by Jaron Vietor in 2010 for DDVTech

#include "socket.h"
#include "timing.h"
#include <sys/stat.h>
#include <poll.h>
#include <netdb.h>
#include <sstream>

#ifdef __FreeBSD__
#include <netinet/in.h>
#endif

#define BUFFER_BLOCKSIZE 4096 //set buffer blocksize to 4KiB
#include <iostream>//temporary for debugging

std::string uint2string(unsigned int i){
  std::stringstream st;
  st << i;
  return st.str();
}

/// Returns the amount of elements in the internal std::deque of std::string objects.
/// The back is popped as long as it is empty, first - this way this function is
/// guaranteed to return 0 if the buffer is empty.
unsigned int Socket::Buffer::size(){
  while (data.size() > 0 && data.back().empty()){
    data.pop_back();
  }
  return data.size();
}

/// Returns either the amount of total bytes available in the buffer or max, whichever is smaller.
unsigned int Socket::Buffer::bytes(unsigned int max){
  unsigned int i = 0;
  for (std::deque<std::string>::iterator it = data.begin(); it != data.end(); ++it){
    i += ( *it).size();
    if (i >= max){
      return max;
    }
  }
  return i;
}

/// Appends this string to the internal std::deque of std::string objects.
/// It is automatically split every BUFFER_BLOCKSIZE bytes.
void Socket::Buffer::append(const std::string & newdata){
  append(newdata.c_str(), newdata.size());
}

/// Appends this data block to the internal std::deque of std::string objects.
/// It is automatically split every BUFFER_BLOCKSIZE bytes.
void Socket::Buffer::append(const char * newdata, const unsigned int newdatasize){
  unsigned int i = 0, j = 0;
  while (i < newdatasize){
    j = i;
    while (j < newdatasize && j - i <= BUFFER_BLOCKSIZE){
      j++;
      if (newdata[j - 1] == '\n'){
        break;
      }
    }
    if (i != j){
      data.push_front(std::string(newdata + i, (size_t)(j - i)));
      i = j;
    }else{
      break;
    }
  }
  if (data.size() > 5000){
    std::cerr << "Warning: After " << newdatasize << " new bytes, buffer has " << data.size() << " parts!" << std::endl;
  }
}

/// Prepends this data block to the internal std::deque of std::string objects.
/// It is _not_ automatically split every BUFFER_BLOCKSIZE bytes.
void Socket::Buffer::prepend(const std::string & newdata){
  data.push_back(newdata);
}

/// Prepends this data block to the internal std::deque of std::string objects.
/// It is _not_ automatically split every BUFFER_BLOCKSIZE bytes.
void Socket::Buffer::prepend(const char * newdata, const unsigned int newdatasize){
  data.push_back(std::string(newdata, (size_t)newdatasize));
}

/// Returns true if at least count bytes are available in this buffer.
bool Socket::Buffer::available(unsigned int count){
  unsigned int i = 0;
  for (std::deque<std::string>::iterator it = data.begin(); it != data.end(); ++it){
    i += ( *it).size();
    if (i >= count){
      return true;
    }
  }
  return false;
}

/// Removes count bytes from the buffer, returning them by value.
/// Returns an empty string if not all count bytes are available.
std::string Socket::Buffer::remove(unsigned int count){
  if ( !available(count)){
    return "";
  }
  unsigned int i = 0;
  std::string ret;
  ret.reserve(count);
  for (std::deque<std::string>::reverse_iterator it = data.rbegin(); it != data.rend(); ++it){
    if (i + ( *it).size() < count){
      ret.append( *it);
      i += ( *it).size();
      ( *it).clear();
    }else{
      ret.append( *it, 0, count - i);
      ( *it).erase(0, count - i);
      break;
    }
  }
  return ret;
}

/// Copies count bytes from the buffer, returning them by value.
/// Returns an empty string if not all count bytes are available.
std::string Socket::Buffer::copy(unsigned int count){
  if ( !available(count)){
    return "";
  }
  unsigned int i = 0;
  std::string ret;
  ret.reserve(count);
  for (std::deque<std::string>::reverse_iterator it = data.rbegin(); it != data.rend(); ++it){
    if (i + ( *it).size() < count){
      ret.append( *it);
      i += ( *it).size();
    }else{
      ret.append( *it, 0, count - i);
      break;
    }
  }
  return ret;
}

/// Gets a reference to the back of the internal std::deque of std::string objects.
std::string & Socket::Buffer::get(){
  static std::string empty;
  if (data.size() > 0){
    return data.back();
  }else{
    return empty;
  }
}

/// Create a new base socket. This is a basic constructor for converting any valid socket to a Socket::Connection.
/// \param sockNo Integer representing the socket to convert.
Socket::Connection::Connection(int sockNo){
  sock = sockNo;
  pipes[0] = -1;
  pipes[1] = -1;
  up = 0;
  down = 0;
  conntime = Util::epoch();
  Error = false;
  Blocking = false;
} //Socket::Connection basic constructor

/// Simulate a socket using two file descriptors.
/// \param write The filedescriptor to write to.
/// \param read The filedescriptor to read from.
Socket::Connection::Connection(int write, int read){
  sock = -1;
  pipes[0] = write;
  pipes[1] = read;
  up = 0;
  down = 0;
  conntime = Util::epoch();
  Error = false;
  Blocking = false;
} //Socket::Connection basic constructor

/// Create a new disconnected base socket. This is a basic constructor for placeholder purposes.
/// A socket created like this is always disconnected and should/could be overwritten at some point.
Socket::Connection::Connection(){
  sock = -1;
  pipes[0] = -1;
  pipes[1] = -1;
  up = 0;
  down = 0;
  conntime = Util::epoch();
  Error = false;
  Blocking = false;
} //Socket::Connection basic constructor

/// Internally used call to make an file descriptor blocking or not.
void setFDBlocking(int FD, bool blocking){
  int flags = fcntl(FD, F_GETFL, 0);
  if ( !blocking){
    flags |= O_NONBLOCK;
  }else{
    flags &= !O_NONBLOCK;
  }
  fcntl(FD, F_SETFL, flags);
}

/// Internally used call to make an file descriptor blocking or not.
bool isFDBlocking(int FD){
  int flags = fcntl(FD, F_GETFL, 0);
  return !(flags & O_NONBLOCK);
}

/// Set this socket to be blocking (true) or nonblocking (false).
void Socket::Connection::setBlocking(bool blocking){
  if (sock >= 0){
    setFDBlocking(sock, blocking);
  }
  if (pipes[0] >= 0){
    setFDBlocking(pipes[0], blocking);
  }
  if (pipes[1] >= 0){
    setFDBlocking(pipes[1], blocking);
  }
}

/// Set this socket to be blocking (true) or nonblocking (false).
bool Socket::Connection::isBlocking(){
  if (sock >= 0){
    return isFDBlocking(sock);
  }
  if (pipes[0] >= 0){
    return isFDBlocking(pipes[0]);
  }
  if (pipes[1] >= 0){
    return isFDBlocking(pipes[1]);
  }
  return false;
}

/// Close connection. The internal socket is closed and then set to -1.
/// If the connection is already closed, nothing happens.
void Socket::Connection::close(){
  if (connected()){
#if DEBUG >= 6
    fprintf(stderr, "Socket closed.\n");
#endif
    if (sock != -1){
      shutdown(sock, SHUT_RDWR);
      errno = EINTR;
      while (::close(sock) != 0 && errno == EINTR){
      }
      sock = -1;
    }
    if (pipes[0] != -1){
      errno = EINTR;
      while (::close(pipes[0]) != 0 && errno == EINTR){
      }
      pipes[0] = -1;
    }
    if (pipes[1] != -1){
      errno = EINTR;
      while (::close(pipes[1]) != 0 && errno == EINTR){
      }
      pipes[1] = -1;
    }
  }
} //Socket::Connection::close

/// Returns internal socket number.
int Socket::Connection::getSocket(){
  return sock;
}

/// Returns a string describing the last error that occured.
/// Only reports errors if an error actually occured - returns the host address or empty string otherwise.
std::string Socket::Connection::getError(){
  return remotehost;
}

/// Create a new Unix Socket. This socket will (try to) connect to the given address right away.
/// \param address String containing the location of the Unix socket to connect to.
/// \param nonblock Whether the socket should be nonblocking. False by default.
Socket::Connection::Connection(std::string address, bool nonblock){
  pipes[0] = -1;
  pipes[1] = -1;
  sock = socket(PF_UNIX, SOCK_STREAM, 0);
  if (sock < 0){
    remotehost = strerror(errno);
#if DEBUG >= 1
    fprintf(stderr, "Could not create socket! Error: %s\n", remotehost.c_str());
#endif
    return;
  }
  Error = false;
  Blocking = false;
  up = 0;
  down = 0;
  conntime = Util::epoch();
  sockaddr_un addr;
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, address.c_str(), address.size() + 1);
  int r = connect(sock, (sockaddr*) &addr, sizeof(addr));
  if (r == 0){
    if (nonblock){
      int flags = fcntl(sock, F_GETFL, 0);
      flags |= O_NONBLOCK;
      fcntl(sock, F_SETFL, flags);
    }
  }else{
    remotehost = strerror(errno);
#if DEBUG >= 1
    fprintf(stderr, "Could not connect to %s! Error: %s\n", address.c_str(), remotehost.c_str());
#endif
    close();
  }
} //Socket::Connection Unix Contructor

/// Create a new TCP Socket. This socket will (try to) connect to the given host/port right away.
/// \param host String containing the hostname to connect to.
/// \param port String containing the port to connect to.
/// \param nonblock Whether the socket should be nonblocking.
Socket::Connection::Connection(std::string host, int port, bool nonblock){
  pipes[0] = -1;
  pipes[1] = -1;
  struct addrinfo *result, *rp, hints;
  Error = false;
  Blocking = false;
  up = 0;
  down = 0;
  conntime = Util::epoch();
  std::stringstream ss;
  ss << port;

  memset( &hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_ADDRCONFIG;
  hints.ai_protocol = 0;
  hints.ai_canonname = NULL;
  hints.ai_addr = NULL;
  hints.ai_next = NULL;
  int s = getaddrinfo(host.c_str(), ss.str().c_str(), &hints, &result);
  if (s != 0){
#if DEBUG >= 1
    fprintf(stderr, "Could not connect to %s:%i! Error: %s\n", host.c_str(), port, gai_strerror(s));
#endif
    close();
    return;
  }

  remotehost = "";
  for (rp = result; rp != NULL; rp = rp->ai_next){
    sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (sock < 0){
      continue;
    }
    if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0){
      break;
    }
    remotehost += strerror(errno);
    ::close(sock);
  }
  freeaddrinfo(result);

  if (rp == 0){
#if DEBUG >= 1
    fprintf(stderr, "Could not connect to %s! Error: %s\n", host.c_str(), remotehost.c_str());
#endif
    close();
  }else{
    if (nonblock){
      int flags = fcntl(sock, F_GETFL, 0);
      flags |= O_NONBLOCK;
      fcntl(sock, F_SETFL, flags);
    }
  }
} //Socket::Connection TCP Contructor

/// Returns the connected-state for this socket.
/// Note that this function might be slightly behind the real situation.
/// The connection status is updated after every read/write attempt, when errors occur
/// and when the socket is closed manually.
/// \returns True if socket is connected, false otherwise.
bool Socket::Connection::connected() const{
  return (sock >= 0) || ((pipes[0] >= 0) && (pipes[1] >= 0));
}

/// Returns total amount of bytes sent.
unsigned int Socket::Connection::dataUp(){
  return up;
}

/// Returns total amount of bytes received.
unsigned int Socket::Connection::dataDown(){
  return down;
}

/// Returns a std::string of stats, ended by a newline.
/// Requires the current connector name as an argument.
std::string Socket::Connection::getStats(std::string C){
  return "S " + getHost() + " " + C + " " + uint2string(Util::epoch() - conntime) + " " + uint2string(up) + " " + uint2string(down) + "\n";
}

/// Updates the downbuffer and upbuffer internal variables.
/// Returns true if new data was received, false otherwise.
bool Socket::Connection::spool(){
  if (upbuffer.size() > 0){
    iwrite(upbuffer.get());
  }
  /// \todo Provide better mechanism to prevent overbuffering.
  if (downbuffer.size() > 10000){
    return true;
  }else{
    return iread(downbuffer);
  }
}

/// Updates the downbuffer and upbuffer internal variables until upbuffer is empty.
/// Returns true if new data was received, false otherwise.
bool Socket::Connection::flush(){
  bool bing = isBlocking();
  if (!bing){setBlocking(true);}
  while (upbuffer.size() > 0 && connected()){
    iwrite(upbuffer.get());
  }
  if (!bing){setBlocking(false);}
  /// \todo Provide better mechanism to prevent overbuffering.
  if (downbuffer.size() > 1000){
    return true;
  }else{
    return iread(downbuffer);
  }
}

/// Returns a reference to the download buffer.
Socket::Buffer & Socket::Connection::Received(){
  return downbuffer;
}

/// Will not buffer anything but always send right away. Blocks.
/// This will send the upbuffer (if non-empty) first, then the data.
/// Any data that could not be send will block until it can be send or the connection is severed.
void Socket::Connection::SendNow(const char * data, size_t len){
  bool bing = isBlocking();
  if (!bing){setBlocking(true);}
  while (upbuffer.size() > 0 && connected()){
    iwrite(upbuffer.get());
  }
  int i = iwrite(data, len);
  while (i < len && connected()){
    int j = iwrite(data + i, std::min(len - i, (size_t)51200));
    if (j > 0){
      i += j;
    }
  }
  if (!bing){setBlocking(false);}
}

/// Appends data to the upbuffer.
/// This will attempt to send the upbuffer (if non-empty) first.
/// If the upbuffer is empty before or after this attempt, it will attempt to send
/// the data right away. Any data that could not be send will be put into the upbuffer.
/// This means this function is blocking if the socket is, but nonblocking otherwise.
void Socket::Connection::Send(const char * data, size_t len){
  while (upbuffer.size() > 0){
    if ( !iwrite(upbuffer.get())){
      break;
    }
  }
  if (upbuffer.size() > 0){
    upbuffer.append(data, len);
  }else{
    int i = iwrite(data, len);
    if (i < len){
      upbuffer.append(data + i, len - i);
    }
  }
}

/// Will not buffer anything but always send right away. Blocks.
/// This will send the upbuffer (if non-empty) first, then the data.
/// Any data that could not be send will block until it can be send or the connection is severed.
void Socket::Connection::SendNow(const char * data){
  int len = strlen(data);
  SendNow(data, len);
}

/// Appends data to the upbuffer.
/// This will attempt to send the upbuffer (if non-empty) first.
/// If the upbuffer is empty before or after this attempt, it will attempt to send
/// the data right away. Any data that could not be send will be put into the upbuffer.
/// This means this function is blocking if the socket is, but nonblocking otherwise.
void Socket::Connection::Send(const char * data){
  int len = strlen(data);
  Send(data, len);
}

/// Will not buffer anything but always send right away. Blocks.
/// This will send the upbuffer (if non-empty) first, then the data.
/// Any data that could not be send will block until it can be send or the connection is severed.
void Socket::Connection::SendNow(const std::string & data){
  SendNow(data.c_str(), data.size());
}

/// Appends data to the upbuffer.
/// This will attempt to send the upbuffer (if non-empty) first.
/// If the upbuffer is empty before or after this attempt, it will attempt to send
/// the data right away. Any data that could not be send will be put into the upbuffer.
/// This means this function is blocking if the socket is, but nonblocking otherwise.
void Socket::Connection::Send(std::string & data){
  Send(data.c_str(), data.size());
}

/// Incremental write call. This function tries to write len bytes to the socket from the buffer,
/// returning the amount of bytes it actually wrote.
/// \param buffer Location of the buffer to write from.
/// \param len Amount of bytes to write.
/// \returns The amount of bytes actually written.
int Socket::Connection::iwrite(const void * buffer, int len){
  if ( !connected() || len < 1){
    return 0;
  }
  int r;
  if (sock >= 0){
    r = send(sock, buffer, len, 0);
  }else{
    r = write(pipes[0], buffer, len);
  }
  if (r < 0){
    switch (errno){
      case EWOULDBLOCK:
        return 0;
        break;
      default:
        if (errno != EPIPE){
          Error = true;
          remotehost = strerror(errno);
#if DEBUG >= 2
          fprintf(stderr, "Could not iwrite data! Error: %s\n", remotehost.c_str());
#endif
        }
        close();
        return 0;
        break;
    }
  }
  if (r == 0 && (sock >= 0)){
    close();
  }
  up += r;
  return r;
} //Socket::Connection::iwrite

/// Incremental read call. This function tries to read len bytes to the buffer from the socket,
/// returning the amount of bytes it actually read.
/// \param buffer Location of the buffer to read to.
/// \param len Amount of bytes to read.
/// \returns The amount of bytes actually read.
int Socket::Connection::iread(void * buffer, int len){
  if ( !connected() || len < 1){
    return 0;
  }
  int r;
  if (sock >= 0){
    r = recv(sock, buffer, len, 0);
  }else{
    r = read(pipes[1], buffer, len);
  }
  if (r < 0){
    switch (errno){
      case EWOULDBLOCK:
        return 0;
        break;
      default:
        if (errno != EPIPE){
          Error = true;
          remotehost = strerror(errno);
#if DEBUG >= 2
          fprintf(stderr, "Could not iread data! Error: %s\n", remotehost.c_str());
#endif
        }
        close();
        return 0;
        break;
    }
  }
  if (r == 0 && (sock >= 0)){
    close();
  }
  down += r;
  return r;
} //Socket::Connection::iread

/// Read call that is compatible with Socket::Buffer.
/// Data is read using iread (which is nonblocking if the Socket::Connection itself is),
/// then appended to end of buffer.
/// \param buffer Socket::Buffer to append data to.
/// \return True if new data arrived, false otherwise.
bool Socket::Connection::iread(Buffer & buffer){
  char cbuffer[BUFFER_BLOCKSIZE];
  int num = iread(cbuffer, BUFFER_BLOCKSIZE);
  if (num < 1){
    return false;
  }
  buffer.append(cbuffer, num);
  return true;
} //iread

/// Incremental write call that is compatible with std::string.
/// Data is written using iwrite (which is nonblocking if the Socket::Connection itself is),
/// then removed from front of buffer.
/// \param buffer std::string to remove data from.
/// \return True if more data was sent, false otherwise.
bool Socket::Connection::iwrite(std::string & buffer){
  if (buffer.size() < 1){
    return false;
  }
  int tmp = iwrite((void*)buffer.c_str(), buffer.size());
  if (tmp < 1){
    return false;
  }
  buffer = buffer.substr(tmp);
  return true;
} //iwrite

/// Gets hostname for connection, if available.
std::string Socket::Connection::getHost(){
  return remotehost;
}

/// Sets hostname for connection manually.
/// Overwrites the detected host, thus possibily making it incorrect.
void Socket::Connection::setHost(std::string host){
  remotehost = host;
}

/// Returns true if these sockets are the same socket.
/// Does not check the internal stats - only the socket itself.
bool Socket::Connection::operator==(const Connection &B) const{
  return sock == B.sock && pipes[0] == B.pipes[0] && pipes[1] == B.pipes[1];
}

/// Returns true if these sockets are not the same socket.
/// Does not check the internal stats - only the socket itself.
bool Socket::Connection::operator!=(const Connection &B) const{
  return sock != B.sock || pipes[0] != B.pipes[0] || pipes[1] != B.pipes[1];
}

/// Returns true if the socket is valid.
/// Aliases for Socket::Connection::connected()
Socket::Connection::operator bool() const{
  return connected();
}

/// Create a new base Server. The socket is never connected, and a placeholder for later connections.
Socket::Server::Server(){
  sock = -1;
} //Socket::Server base Constructor

/// Create a new TCP Server. The socket is immediately bound and set to listen.
/// A maximum of 100 connections will be accepted between accept() calls.
/// Any further connections coming in will be dropped.
/// \param port The TCP port to listen on
/// \param hostname (optional) The interface to bind to. The default is 0.0.0.0 (all interfaces).
/// \param nonblock (optional) Whether accept() calls will be nonblocking. Default is false (blocking).
Socket::Server::Server(int port, std::string hostname, bool nonblock){
  if ( !IPv6bind(port, hostname, nonblock) && !IPv4bind(port, hostname, nonblock)){
    fprintf(stderr, "Could not create socket %s:%i! Error: %s\n", hostname.c_str(), port, errors.c_str());
    sock = -1;
  }
} //Socket::Server TCP Constructor

/// Attempt to bind an IPv6 socket.
/// \param port The TCP port to listen on
/// \param hostname The interface to bind to. The default is 0.0.0.0 (all interfaces).
/// \param nonblock Whether accept() calls will be nonblocking. Default is false (blocking).
/// \return True if successful, false otherwise.
bool Socket::Server::IPv6bind(int port, std::string hostname, bool nonblock){
  sock = socket(AF_INET6, SOCK_STREAM, 0);
  if (sock < 0){
    errors = strerror(errno);
#if DEBUG >= 1
    fprintf(stderr, "Could not create IPv6 socket %s:%i! Error: %s\n", hostname.c_str(), port, errors.c_str());
#endif
    return false;
  }
  int on = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  if (nonblock){
    int flags = fcntl(sock, F_GETFL, 0);
    flags |= O_NONBLOCK;
    fcntl(sock, F_SETFL, flags);
  }
  struct sockaddr_in6 addr;
  addr.sin6_family = AF_INET6;
  addr.sin6_port = htons(port); //set port
  if (hostname == "0.0.0.0" || hostname.length() == 0){
    addr.sin6_addr = in6addr_any;
  }else{
    inet_pton(AF_INET6, hostname.c_str(), &addr.sin6_addr); //set interface, 0.0.0.0 (default) is all
  }
  int ret = bind(sock, (sockaddr*) &addr, sizeof(addr)); //do the actual bind
  if (ret == 0){
    ret = listen(sock, 100); //start listening, backlog of 100 allowed
    if (ret == 0){
#if DEBUG >= 1
      fprintf(stderr, "IPv6 socket success @ %s:%i\n", hostname.c_str(), port);
#endif
      return true;
    }else{
      errors = strerror(errno);
#if DEBUG >= 1
      fprintf(stderr, "IPv6 Listen failed! Error: %s\n", errors.c_str());
#endif
      close();
      return false;
    }
  }else{
    errors = strerror(errno);
#if DEBUG >= 1
    fprintf(stderr, "IPv6 Binding %s:%i failed (%s)\n", hostname.c_str(), port, errors.c_str());
#endif
    close();
    return false;
  }
}

/// Attempt to bind an IPv4 socket.
/// \param port The TCP port to listen on
/// \param hostname The interface to bind to. The default is 0.0.0.0 (all interfaces).
/// \param nonblock Whether accept() calls will be nonblocking. Default is false (blocking).
/// \return True if successful, false otherwise.
bool Socket::Server::IPv4bind(int port, std::string hostname, bool nonblock){
  sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0){
    errors = strerror(errno);
#if DEBUG >= 1
    fprintf(stderr, "Could not create IPv4 socket %s:%i! Error: %s\n", hostname.c_str(), port, errors.c_str());
#endif
    return false;
  }
  int on = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  if (nonblock){
    int flags = fcntl(sock, F_GETFL, 0);
    flags |= O_NONBLOCK;
    fcntl(sock, F_SETFL, flags);
  }
  struct sockaddr_in addr4;
  addr4.sin_family = AF_INET;
  addr4.sin_port = htons(port); //set port
  if (hostname == "0.0.0.0" || hostname.length() == 0){
    addr4.sin_addr.s_addr = INADDR_ANY;
  }else{
    inet_pton(AF_INET, hostname.c_str(), &addr4.sin_addr); //set interface, 0.0.0.0 (default) is all
  }
  int ret = bind(sock, (sockaddr*) &addr4, sizeof(addr4)); //do the actual bind
  if (ret == 0){
    ret = listen(sock, 100); //start listening, backlog of 100 allowed
    if (ret == 0){
#if DEBUG >= 1
      fprintf(stderr, "IPv4 socket success @ %s:%i\n", hostname.c_str(), port);
#endif
      return true;
    }else{
      errors = strerror(errno);
#if DEBUG >= 1
      fprintf(stderr, "IPv4 Listen failed! Error: %s\n", errors.c_str());
#endif
      close();
      return false;
    }
  }else{
    errors = strerror(errno);
#if DEBUG >= 1
    fprintf(stderr, "IPv4 binding %s:%i failed (%s)\n", hostname.c_str(), port, errors.c_str());
#endif
    close();
    return false;
  }
}

/// Create a new Unix Server. The socket is immediately bound and set to listen.
/// A maximum of 100 connections will be accepted between accept() calls.
/// Any further connections coming in will be dropped.
/// The address used will first be unlinked - so it succeeds if the Unix socket already existed. Watch out for this behaviour - it will delete any file located at address!
/// \param address The location of the Unix socket to bind to.
/// \param nonblock (optional) Whether accept() calls will be nonblocking. Default is false (blocking).
Socket::Server::Server(std::string address, bool nonblock){
  unlink(address.c_str());
  sock = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sock < 0){
    errors = strerror(errno);
#if DEBUG >= 1
    fprintf(stderr, "Could not create socket! Error: %s\n", errors.c_str());
#endif
    return;
  }
  if (nonblock){
    int flags = fcntl(sock, F_GETFL, 0);
    flags |= O_NONBLOCK;
    fcntl(sock, F_SETFL, flags);
  }
  sockaddr_un addr;
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, address.c_str(), address.size() + 1);
  int ret = bind(sock, (sockaddr*) &addr, sizeof(addr));
  if (ret == 0){
    ret = listen(sock, 100); //start listening, backlog of 100 allowed
    if (ret == 0){
      return;
    }else{
      errors = strerror(errno);
#if DEBUG >= 1
      fprintf(stderr, "Listen failed! Error: %s\n", errors.c_str());
#endif
      close();
      return;
    }
  }else{
    errors = strerror(errno);
#if DEBUG >= 1
    fprintf(stderr, "Binding failed! Error: %s\n", errors.c_str());
#endif
    close();
    return;
  }
} //Socket::Server Unix Constructor

/// Accept any waiting connections. If the Socket::Server is blocking, this function will block until there is an incoming connection.
/// If the Socket::Server is nonblocking, it might return a Socket::Connection that is not connected, so check for this.
/// \param nonblock (optional) Whether the newly connected socket should be nonblocking. Default is false (blocking).
/// \returns A Socket::Connection, which may or may not be connected, depending on settings and circumstances.
Socket::Connection Socket::Server::accept(bool nonblock){
  if (sock < 0){
    return Socket::Connection( -1);
  }
  struct sockaddr_in6 addrinfo;
  socklen_t len = sizeof(addrinfo);
  static char addrconv[INET6_ADDRSTRLEN];
  int r = ::accept(sock, (sockaddr*) &addrinfo, &len);
  //set the socket to be nonblocking, if requested.
  //we could do this through accept4 with a flag, but that call is non-standard...
  if ((r >= 0) && nonblock){
    int flags = fcntl(r, F_GETFL, 0);
    flags |= O_NONBLOCK;
    fcntl(r, F_SETFL, flags);
  }
  Socket::Connection tmp(r);
  if (r < 0){
    if ((errno != EWOULDBLOCK) && (errno != EAGAIN) && (errno != EINTR)){
#if DEBUG >= 1
      fprintf(stderr, "Error during accept - closing server socket.\n");
#endif
      close();
    }
  }else{
    if (addrinfo.sin6_family == AF_INET6){
      tmp.remotehost = inet_ntop(AF_INET6, &(addrinfo.sin6_addr), addrconv, INET6_ADDRSTRLEN);
#if DEBUG >= 6
      fprintf(stderr,"IPv6 addr: %s\n", tmp.remotehost.c_str());
#endif
    }
    if (addrinfo.sin6_family == AF_INET){
      tmp.remotehost = inet_ntop(AF_INET, &(((sockaddr_in*) &addrinfo)->sin_addr), addrconv, INET6_ADDRSTRLEN);
#if DEBUG >= 6
      fprintf(stderr,"IPv4 addr: %s\n", tmp.remotehost.c_str());
#endif
    }
    if (addrinfo.sin6_family == AF_UNIX){
#if DEBUG >= 6
      tmp.remotehost = ((sockaddr_un*)&addrinfo)->sun_path;
      fprintf(stderr,"Unix socket, no address\n");
#endif
      tmp.remotehost = "UNIX_SOCKET";
    }
  }
  return tmp;
}

/// Set this socket to be blocking (true) or nonblocking (false).
void Socket::Server::setBlocking(bool blocking){
  if (sock >= 0){
    setFDBlocking(sock, blocking);
  }
}

/// Set this socket to be blocking (true) or nonblocking (false).
bool Socket::Server::isBlocking(){
  if (sock >= 0){
    return isFDBlocking(sock);
  }
  return false;
}

/// Close connection. The internal socket is closed and then set to -1.
/// If the connection is already closed, nothing happens.
void Socket::Server::close(){
  if (connected()){
#if DEBUG >= 6
    fprintf(stderr, "ServerSocket closed.\n");
#endif
    shutdown(sock, SHUT_RDWR);
    errno = EINTR;
    while (::close(sock) != 0 && errno == EINTR){
    }
    sock = -1;
  }
} //Socket::Server::close

/// Returns the connected-state for this socket.
/// Note that this function might be slightly behind the real situation.
/// The connection status is updated after every accept attempt, when errors occur
/// and when the socket is closed manually.
/// \returns True if socket is connected, false otherwise.
bool Socket::Server::connected() const{
  return (sock >= 0);
} //Socket::Server::connected

/// Returns internal socket number.
int Socket::Server::getSocket(){
  return sock;
}
