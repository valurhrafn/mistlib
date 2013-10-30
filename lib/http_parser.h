/// \file http_parser.h
/// Holds all headers for the HTTP namespace.

#pragma once
#include <map>
#include <string>
#include <stdlib.h>
#include <stdio.h>
#include "socket.h"

/// Holds all HTTP processing related code.
namespace HTTP {
  /// Simple class for reading and writing HTTP 1.0 and 1.1.
  class Parser{
    public:
      Parser();
      bool Read(Socket::Connection & conn);
      bool Read(std::string & strbuf);
      std::string GetHeader(std::string i);
      std::string GetVar(std::string i);
      std::string getUrl();
      void SetHeader(std::string i, std::string v);
      void SetHeader(std::string i, int v);
      void SetVar(std::string i, std::string v);
      void SetBody(std::string s);
      void SetBody(char * buffer, int len);
      std::string & BuildRequest();
      std::string & BuildResponse(std::string code, std::string message);
      void SendRequest(Socket::Connection & conn);
      void SendResponse(std::string code, std::string message, Socket::Connection & conn);
      void StartResponse(std::string code, std::string message, Parser & request, Socket::Connection & conn);
      void StartResponse(Parser & request, Socket::Connection & conn);
      void Chunkify(std::string & bodypart, Socket::Connection & conn);
      void Chunkify(const char * data, unsigned int size, Socket::Connection & conn);
      void Proxy(Socket::Connection & from, Socket::Connection & to);
      void Clean();
      static std::string urlunescape(const std::string & in);
      static std::string urlencode(const std::string & in);
      std::string body;
      std::string method;
      std::string url;
      std::string protocol;
      unsigned int length;
      bool headerOnly; ///< If true, do not parse body if the length is a known size.
    private:
      bool seenHeaders;
      bool seenReq;
      bool getChunks;
      unsigned int doingChunk;
      bool parse(std::string & HTTPbuffer);
      void parseVars(std::string data);
      std::string builder;
      std::string read_buffer;
      std::map<std::string, std::string> headers;
      std::map<std::string, std::string> vars;
      void Trim(std::string & s);
      static int unhex(char c);
      static std::string hex(char dec);
  };
//HTTP::Parser class

}//HTTP namespace
