/// \file socket.h
/// A handy Socket wrapper library.
/// Written by Jaron Vietor in 2010 for DDVTech

#pragma once
#include <string>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

//for being friendly with Socket::Connection down below
namespace Buffer{class user;};

///Holds Socket tools.
namespace Socket{

  /// This class is for easy communicating through sockets, either TCP or Unix.
  class Connection{
    private:
      int sock; ///< Internally saved socket number.
      int pipes[2]; ///< Internally saved file descriptors for pipe socket simulation.
      std::string remotehost; ///< Stores remote host address.
      unsigned int up;
      unsigned int down;
      unsigned int conntime;
      std::string downbuffer; ///< Stores temporary data coming in.
      std::string upbuffer; ///< Stores temporary data going out.
      int iread(void * buffer, int len); ///< Incremental read call.
      int iwrite(const void * buffer, int len); ///< Incremental write call.
      bool iread(std::string & buffer); ///< Incremental write call that is compatible with std::string.
      bool iwrite(std::string & buffer); ///< Write call that is compatible with std::string.
    public:
      //friends
      friend class Buffer::user;
      //constructors
      Connection(); ///< Create a new disconnected base socket.
      Connection(int sockNo); ///< Create a new base socket.
      Connection(std::string hostname, int port, bool nonblock); ///< Create a new TCP socket.
      Connection(std::string adres, bool nonblock = false); ///< Create a new Unix Socket.
      Connection(int write, int read); ///< Simulate a socket using two file descriptors.
      //generic methods
      void close(); ///< Close connection.
      void setBlocking(bool blocking); ///< Set this socket to be blocking (true) or nonblocking (false).
      std::string getHost(); ///< Gets hostname for connection, if available.
      void setHost(std::string host); ///< Sets hostname for connection manually.
      int getSocket(); ///< Returns internal socket number.
      std::string getError(); ///< Returns a string describing the last error that occured.
      bool connected() const; ///< Returns the connected-state for this socket.
      //buffered i/o methods
      bool spool(); ///< Updates the downbuffer and upbuffer internal variables.
      bool flush(); ///< Updates the downbuffer and upbuffer internal variables until upbuffer is empty.
      std::string & Received(); ///< Returns a reference to the download buffer.
      void Send(std::string data); ///< Appends data to the upbuffer.
      //stats related methods
      unsigned int dataUp(); ///< Returns total amount of bytes sent.
      unsigned int dataDown(); ///< Returns total amount of bytes received.
      std::string getStats(std::string C); ///< Returns a std::string of stats, ended by a newline.
      friend class Server;
      bool Error; ///< Set to true if a socket error happened.
      bool Blocking; ///< Set to true if a socket is currently or wants to be blocking.
      //overloaded operators
      bool operator== (const Connection &B) const;
      bool operator!= (const Connection &B) const;
      operator bool() const;
  };

  /// This class is for easily setting up listening socket, either TCP or Unix.
  class Server{
    private:
      int sock; ///< Internally saved socket number.
      bool IPv6bind(int port, std::string hostname, bool nonblock); ///< Attempt to bind an IPv6 socket
      bool IPv4bind(int port, std::string hostname, bool nonblock); ///< Attempt to bind an IPv4 socket
  public:
      Server(); ///< Create a new base Server.
      Server(int port, std::string hostname = "0.0.0.0", bool nonblock = false); ///< Create a new TCP Server.
      Server(std::string adres, bool nonblock = false); ///< Create a new Unix Server.
      Connection accept(bool nonblock = false); ///< Accept any waiting connections.
      bool connected() const; ///< Returns the connected-state for this socket.
      void close(); ///< Close connection.
      int getSocket(); ///< Returns internal socket number.
  };

  /// Connect to a stream on the system.
  Connection getStream(std::string streamname);

  /// Create a stream on the system.
  Server makeStream(std::string streamname);
  
};
