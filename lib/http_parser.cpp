/// \file http_parser.cpp
/// Holds all code for the HTTP namespace.

#include "http_parser.h"
#include "timing.h"

/// This constructor creates an empty HTTP::Parser, ready for use for either reading or writing.
/// All this constructor does is call HTTP::Parser::Clean().
HTTP::Parser::Parser(){
  headerOnly = false;
  Clean();
}

/// Completely re-initializes the HTTP::Parser, leaving it ready for either reading or writing usage.
void HTTP::Parser::Clean(){
  seenHeaders = false;
  seenReq = false;
  getChunks = false;
  doingChunk = 0;
  method = "GET";
  url = "/";
  protocol = "HTTP/1.1";
  body.clear();
  length = 0;
  headers.clear();
  vars.clear();
}

/// Returns a string containing a valid HTTP 1.0 or 1.1 request, ready for sending.
/// The request is build from internal variables set before this call is made.
/// To be precise, method, url, protocol, headers and body are used.
/// \return A string containing a valid HTTP 1.0 or 1.1 request, ready for sending.
std::string & HTTP::Parser::BuildRequest(){
  /// \todo Include GET/POST variable parsing?
  std::map<std::string, std::string>::iterator it;
  if (protocol.size() < 5 || protocol.substr(0, 4) != "HTTP"){
    protocol = "HTTP/1.0";
  }
  builder = method + " " + url + " " + protocol + "\r\n";
  for (it = headers.begin(); it != headers.end(); it++){
    if (( *it).first != "" && ( *it).second != ""){
      builder += ( *it).first + ": " + ( *it).second + "\r\n";
    }
  }
  builder += "\r\n" + body;
  return builder;
}

/// Creates and sends a valid HTTP 1.0 or 1.1 request.
/// The request is build from internal variables set before this call is made.
/// To be precise, method, url, protocol, headers and body are used.
void HTTP::Parser::SendRequest(Socket::Connection & conn){
  /// \todo Include GET/POST variable parsing?
  std::map<std::string, std::string>::iterator it;
  if (protocol.size() < 5 || protocol.substr(0, 4) != "HTTP"){
    protocol = "HTTP/1.0";
  }
  builder = method + " " + url + " " + protocol + "\r\n";
  conn.SendNow(builder);
  for (it = headers.begin(); it != headers.end(); it++){
    if (( *it).first != "" && ( *it).second != ""){
      builder = ( *it).first + ": " + ( *it).second + "\r\n";
      conn.SendNow(builder);
    }
  }
  conn.SendNow("\r\n", 2);
  conn.SendNow(body);
}

/// Returns a string containing a valid HTTP 1.0 or 1.1 response, ready for sending.
/// The response is partly build from internal variables set before this call is made.
/// To be precise, protocol, headers and body are used.
/// \param code The HTTP response code. Usually you want 200.
/// \param message The HTTP response message. Usually you want "OK".
/// \return A string containing a valid HTTP 1.0 or 1.1 response, ready for sending.
std::string & HTTP::Parser::BuildResponse(std::string code, std::string message){
  /// \todo Include GET/POST variable parsing?
  std::map<std::string, std::string>::iterator it;
  if (protocol.size() < 5 || protocol.substr(0, 4) != "HTTP"){
    protocol = "HTTP/1.0";
  }
  builder = protocol + " " + code + " " + message + "\r\n";
  for (it = headers.begin(); it != headers.end(); it++){
    if (( *it).first != "" && ( *it).second != ""){
      if (( *it).first != "Content-Length" || ( *it).second != "0"){
        builder += ( *it).first + ": " + ( *it).second + "\r\n";
      }
    }
  }
  builder += "\r\n";
  builder += body;
  return builder;
}

/// Creates and sends a valid HTTP 1.0 or 1.1 response.
/// The response is partly build from internal variables set before this call is made.
/// To be precise, protocol, headers and body are used.
/// \param code The HTTP response code. Usually you want 200.
/// \param message The HTTP response message. Usually you want "OK".
void HTTP::Parser::SendResponse(std::string code, std::string message, Socket::Connection & conn){
  /// \todo Include GET/POST variable parsing?
  std::map<std::string, std::string>::iterator it;
  if (protocol.size() < 5 || protocol.substr(0, 4) != "HTTP"){
    protocol = "HTTP/1.0";
  }
  builder = protocol + " " + code + " " + message + "\r\n";
  conn.SendNow(builder);
  for (it = headers.begin(); it != headers.end(); it++){
    if (( *it).first != "" && ( *it).second != ""){
      if (( *it).first != "Content-Length" || ( *it).second != "0"){
        builder = ( *it).first + ": " + ( *it).second + "\r\n";
        conn.SendNow(builder);
      }
    }
  }
  conn.SendNow("\r\n", 2);
  conn.SendNow(body);
}

/// Creates and sends a valid HTTP 1.0 or 1.1 response, based on the given request.
/// The headers must be set before this call is made.
/// This call sets up chunked transfer encoding if the request was protocol HTTP/1.1, otherwise uses a zero-content-length HTTP/1.0 response.
/// \param code The HTTP response code. Usually you want 200.
/// \param message The HTTP response message. Usually you want "OK".
/// \param request The HTTP request to respond to.
/// \param conn The connection to send over.
void HTTP::Parser::StartResponse(std::string code, std::string message, HTTP::Parser & request, Socket::Connection & conn){
  protocol = request.protocol;
  body = "";
  if (protocol == "HTTP/1.1"){
    SetHeader("Transfer-Encoding", "chunked");
  }else{
    SetBody("");
  }
  SendResponse(code, message, conn);
}

/// Creates and sends a valid HTTP 1.0 or 1.1 response, based on the given request.
/// The headers must be set before this call is made.
/// This call sets up chunked transfer encoding if the request was protocol HTTP/1.1, otherwise uses a zero-content-length HTTP/1.0 response.
/// This call simply calls StartResponse("200", "OK", request, conn)
/// \param request The HTTP request to respond to.
/// \param conn The connection to send over.
void HTTP::Parser::StartResponse(HTTP::Parser & request, Socket::Connection & conn){
  StartResponse("200", "OK", request, conn);
}

/// After receiving a header with this object, this function call will:
/// - Forward the headers to the 'to' Socket::Connection.
/// - Retrieve all the body from the 'from' Socket::Connection.
/// - Forward those contents as-is to the 'to' Socket::Connection.
/// It blocks until completed or either of the connections reaches an error state.
void HTTP::Parser::Proxy(Socket::Connection & from, Socket::Connection & to){
  SendResponse("200", "OK", to);
  if (getChunks){
    unsigned int proxyingChunk = 0;
    while (to.connected() && from.connected()){
      if ((from.Received().size() && (from.Received().size() > 1 || *(from.Received().get().rbegin()) == '\n')) || from.spool()){
        if (proxyingChunk){
          while (proxyingChunk && from.Received().size()){
            unsigned int toappend = from.Received().get().size();
            if (toappend > proxyingChunk){
              toappend = proxyingChunk;
              to.SendNow(from.Received().get().c_str(), toappend);
              from.Received().get().erase(0, toappend);
            }else{
              to.SendNow(from.Received().get());
              from.Received().get().clear();
            }
            proxyingChunk -= toappend;
          }
        }else{
          //Make sure the received data ends in a newline (\n).
          if ( *(from.Received().get().rbegin()) != '\n'){
            if (from.Received().size() > 1){
              //make a copy of the first part
              std::string tmp = from.Received().get();
              //clear the first part, wiping it from the partlist
              from.Received().get().clear();
              from.Received().size();
              //take the now first (was second) part, insert the stored part in front of it
              from.Received().get().insert(0, tmp);
            }else{
              Util::sleep(100);
            }
            if ( *(from.Received().get().rbegin()) != '\n'){
              continue;
            }
          }
          //forward the size and any empty lines
          to.SendNow(from.Received().get());
          
          std::string tmpA = from.Received().get().substr(0, from.Received().get().size() - 1);
          while (tmpA.find('\r') != std::string::npos){
            tmpA.erase(tmpA.find('\r'));
          }
          unsigned int chunkLen = 0;
          if ( !tmpA.empty()){
            for (int i = 0; i < tmpA.size(); ++i){
              chunkLen = (chunkLen << 4) | unhex(tmpA[i]);
            }
            if (chunkLen == 0){
              getChunks = false;
              to.SendNow("\r\n", 2);
              return;
            }
            proxyingChunk = chunkLen;
          }
          from.Received().get().clear();
        }
      }else{
        Util::sleep(100);
      }
    }
  }else{
    unsigned int bodyLen = length;
    while (bodyLen > 0 && to.connected() && from.connected()){
      if (from.Received().size() || from.spool()){
        if (from.Received().get().size() <= bodyLen){
          to.SendNow(from.Received().get());
          bodyLen -= from.Received().get().size();
          from.Received().get().clear();
        }else{
          to.SendNow(from.Received().get().c_str(), bodyLen);
          from.Received().get().erase(0, bodyLen);
          bodyLen = 0;
        }
      }else{
        Util::sleep(100);
      }
    }
  }
}

/// Trims any whitespace at the front or back of the string.
/// Used when getting/setting headers.
/// \param s The string to trim. The string itself will be changed, not returned.
void HTTP::Parser::Trim(std::string & s){
  size_t startpos = s.find_first_not_of(" \t");
  size_t endpos = s.find_last_not_of(" \t");
  if ((std::string::npos == startpos) || (std::string::npos == endpos)){
    s = "";
  }else{
    s = s.substr(startpos, endpos - startpos + 1);
  }
}

/// Function that sets the body of a response or request, along with the correct Content-Length header.
/// \param s The string to set the body to.
void HTTP::Parser::SetBody(std::string s){
  body = s;
  SetHeader("Content-Length", s.length());
}

/// Function that sets the body of a response or request, along with the correct Content-Length header.
/// \param buffer The buffer data to set the body to.
/// \param len Length of the buffer data.
void HTTP::Parser::SetBody(char * buffer, int len){
  body = "";
  body.append(buffer, len);
  SetHeader("Content-Length", len);
}

/// Returns header i, if set.
std::string HTTP::Parser::getUrl(){
  if (url.find('?') != std::string::npos){
    return url.substr(0, url.find('?'));
  }else{
    return url;
  }
}

/// Returns header i, if set.
std::string HTTP::Parser::GetHeader(std::string i){
  return headers[i];
}
/// Returns POST variable i, if set.
std::string HTTP::Parser::GetVar(std::string i){
  return vars[i];
}

/// Sets header i to string value v.
void HTTP::Parser::SetHeader(std::string i, std::string v){
  Trim(i);
  Trim(v);
  headers[i] = v;
}

/// Sets header i to integer value v.
void HTTP::Parser::SetHeader(std::string i, int v){
  Trim(i);
  char val[23]; //ints are never bigger than 22 chars as decimal
  sprintf(val, "%i", v);
  headers[i] = val;
}

/// Sets POST variable i to string value v.
void HTTP::Parser::SetVar(std::string i, std::string v){
  Trim(i);
  Trim(v);
  //only set if there is actually a key
  if ( !i.empty()){
    vars[i] = v;
  }
}

/// Attempt to read a whole HTTP request or response from a Socket::Connection.
/// If a whole request could be read, it is removed from the front of the socket buffer and true returned.
/// If not, as much as can be interpreted is removed and false returned.
/// \param conn The socket to read from.
/// \return True if a whole request or response was read, false otherwise.
bool HTTP::Parser::Read(Socket::Connection & conn){
  //Make sure the received data ends in a newline (\n).
  while ( *(conn.Received().get().rbegin()) != '\n'){
    if (conn.Received().size() > 1){
      //make a copy of the first part
      std::string tmp = conn.Received().get();
      //clear the first part, wiping it from the partlist
      conn.Received().get().clear();
      conn.Received().size();
      //take the now first (was second) part, insert the stored part in front of it
      conn.Received().get().insert(0, tmp);
    }else{
      return false;
    }
  }
  return parse(conn.Received().get());
} //HTTPReader::Read

/// Attempt to read a whole HTTP request or response from a std::string buffer.
/// If a whole request could be read, it is removed from the front of the given buffer and true returned.
/// If not, as much as can be interpreted is removed and false returned.
/// \param strbuf The buffer to read from.
/// \return True if a whole request or response was read, false otherwise.
bool HTTP::Parser::Read(std::string & strbuf){
  return parse(strbuf);
} //HTTPReader::Read

/// Attempt to read a whole HTTP response or request from a data buffer.
/// If succesful, fills its own fields with the proper data and removes the response/request
/// from the data buffer.
/// \param HTTPbuffer The data buffer to read from.
/// \return True on success, false otherwise.
bool HTTP::Parser::parse(std::string & HTTPbuffer){
  size_t f;
  std::string tmpA, tmpB, tmpC;
  /// \todo Make this not resize HTTPbuffer in parts, but read all at once and then remove the entire request, like doxygen claims it does?
  while ( !HTTPbuffer.empty()){
    if ( !seenHeaders){
      f = HTTPbuffer.find('\n');
      if (f == std::string::npos) return false;
      tmpA = HTTPbuffer.substr(0, f);
      if (f + 1 == HTTPbuffer.size()){
        HTTPbuffer.clear();
      }else{
        HTTPbuffer.erase(0, f + 1);
      }
      while (tmpA.find('\r') != std::string::npos){
        tmpA.erase(tmpA.find('\r'));
      }
      if ( !seenReq){
        seenReq = true;
        f = tmpA.find(' ');
        if (f != std::string::npos){
          if (tmpA.substr(0, 4) == "HTTP"){
            protocol = tmpA.substr(0, f);
            tmpA.erase(0, f + 1);
            f = tmpA.find(' ');
            if (f != std::string::npos){
              url = tmpA.substr(0, f);
              tmpA.erase(0, f + 1);
              method = tmpA;
              if (url.find('?') != std::string::npos){
                parseVars(url.substr(url.find('?') + 1)); //parse GET variables
              }
            }else{
              seenReq = false;
            }
          }else{
            method = tmpA.substr(0, f);
            tmpA.erase(0, f + 1);
            f = tmpA.find(' ');
            if (f != std::string::npos){
              url = tmpA.substr(0, f);
              tmpA.erase(0, f + 1);
              protocol = tmpA;
              if (url.find('?') != std::string::npos){
                parseVars(url.substr(url.find('?') + 1)); //parse GET variables
              }
            }else{
              seenReq = false;
            }
          }
        }else{
          seenReq = false;
        }
      }else{
        if (tmpA.size() == 0){
          seenHeaders = true;
          body.clear();
          if (GetHeader("Content-Length") != ""){
            length = atoi(GetHeader("Content-Length").c_str());
            if (body.capacity() < length){
              body.reserve(length);
            }
          }
          if (GetHeader("Transfer-Encoding") == "chunked"){
            getChunks = true;
            doingChunk = 0;
          }
        }else{
          f = tmpA.find(':');
          if (f == std::string::npos) continue;
          tmpB = tmpA.substr(0, f);
          tmpC = tmpA.substr(f + 1);
          SetHeader(tmpB, tmpC);
        }
      }
    }
    if (seenHeaders){
      if (length > 0){
        if (headerOnly){
          return true;
        }
        unsigned int toappend = length - body.length();
        if (toappend > 0){
          body.append(HTTPbuffer, 0, toappend);
          HTTPbuffer.erase(0, toappend);
        }
        if (length == body.length()){
          parseVars(body); //parse POST variables
          return true;
        }else{
          return false;
        }
      }else{
        if (getChunks){
          if (headerOnly){
            return true;
          }
          if (doingChunk){
            unsigned int toappend = HTTPbuffer.size();
            if (toappend > doingChunk){
              toappend = doingChunk;
            }
            body.append(HTTPbuffer, 0, toappend);
            HTTPbuffer.erase(0, toappend);
            doingChunk -= toappend;
          }else{
            f = HTTPbuffer.find('\n');
            if (f == std::string::npos) return false;
            tmpA = HTTPbuffer.substr(0, f);
            while (tmpA.find('\r') != std::string::npos){
              tmpA.erase(tmpA.find('\r'));
            }
            unsigned int chunkLen = 0;
            if ( !tmpA.empty()){
              for (int i = 0; i < tmpA.size(); ++i){
                chunkLen = (chunkLen << 4) | unhex(tmpA[i]);
              }
              if (chunkLen == 0){
                getChunks = false;
                return true;
              }
              doingChunk = chunkLen;
            }
            if (f + 1 == HTTPbuffer.size()){
              HTTPbuffer.clear();
            }else{
              HTTPbuffer.erase(0, f + 1);
            }
          }
          return false;
        }else{
          return true;
        }
      }
    }
  }
  return false; //empty input
} //HTTPReader::parse

/// Parses GET or POST-style variable data.
/// Saves to internal variable structure using HTTP::Parser::SetVar.
void HTTP::Parser::parseVars(std::string data){
  std::string varname;
  std::string varval;
  // position where a part start (e.g. after &)
  size_t pos = 0;
  while (pos < data.length()){
    size_t nextpos = data.find('&', pos);
    if (nextpos == std::string::npos){
      nextpos = data.length();
    }
    size_t eq_pos = data.find('=', pos);
    if (eq_pos < nextpos){
      // there is a key and value
      varname = data.substr(pos, eq_pos - pos);
      varval = data.substr(eq_pos + 1, nextpos - eq_pos - 1);
    }else{
      // no value, only a key
      varname = data.substr(pos, nextpos - pos);
      varval.clear();
    }
    SetVar(urlunescape(varname), urlunescape(varval));
    if (nextpos == std::string::npos){
      // in case the string is gigantic
      break;
    }
    // erase &
    pos = nextpos + 1;
  }
}

/// Sends a string in chunked format if protocol is HTTP/1.1, sends as-is otherwise.
/// \param bodypart The data to send.
/// \param conn The connection to use for sending.
void HTTP::Parser::Chunkify(std::string & bodypart, Socket::Connection & conn){
  Chunkify(bodypart.c_str(), bodypart.size(), conn);
}

/// Sends a string in chunked format if protocol is HTTP/1.1, sends as-is otherwise.
/// \param data The data to send.
/// \param size The size of the data to send.
/// \param conn The connection to use for sending.
void HTTP::Parser::Chunkify(const char * data, unsigned int size, Socket::Connection & conn){
  if (protocol == "HTTP/1.1"){
    char len[10];
    int sizelen = snprintf(len, 10, "%x\r\n", size);
    //prepend the chunk size and \r\n
    conn.SendNow(len, sizelen);
    //send the chunk itself
    conn.SendNow(data, size);
    //append \r\n
    conn.SendNow("\r\n", 2);
    if ( !size){
      //append \r\n again if this was the end of the file (required by chunked transfer encoding according to spec)
      conn.SendNow("\r\n", 2);
    }
  }else{
    //just send the chunk itself
    conn.SendNow(data, size);
    //close the connection if this was the end of the file
    if ( !size){
      conn.close();
    }
  }
}

/// Unescapes URLencoded std::string data.
std::string HTTP::Parser::urlunescape(const std::string & in){
  std::string out;
  for (unsigned int i = 0; i < in.length(); ++i){
    if (in[i] == '%'){
      char tmp = 0;
      ++i;
      if (i < in.length()){
        tmp = unhex(in[i]) << 4;
      }
      ++i;
      if (i < in.length()){
        tmp += unhex(in[i]);
      }
      out += tmp;
    }else{
      if (in[i] == '+'){
        out += ' ';
      }else{
        out += in[i];
      }
    }
  }
  return out;
}

/// Helper function for urlunescape.
/// Takes a single char input and outputs its integer hex value.
int HTTP::Parser::unhex(char c){
  return (c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c - 'a' + 10);
}

/// URLencodes std::string data.
std::string HTTP::Parser::urlencode(const std::string &c){
  std::string escaped = "";
  int max = c.length();
  for (int i = 0; i < max; i++){
    if (('0' <= c[i] && c[i] <= '9') || ('a' <= c[i] && c[i] <= 'z') || ('A' <= c[i] && c[i] <= 'Z')
        || (c[i] == '~' || c[i] == '!' || c[i] == '*' || c[i] == '(' || c[i] == ')' || c[i] == '\'')){
      escaped.append( &c[i], 1);
    }else{
      escaped.append("%");
      escaped.append(hex(c[i]));
    }
  }
  return escaped;
}

/// Helper function for urlescape.
/// Encodes a character as two hex digits.
std::string HTTP::Parser::hex(char dec){
  char dig1 = (dec & 0xF0) >> 4;
  char dig2 = (dec & 0x0F);
  if (dig1 <= 9) dig1 += 48;
  if (10 <= dig1 && dig1 <= 15) dig1 += 97 - 10;
  if (dig2 <= 9) dig2 += 48;
  if (10 <= dig2 && dig2 <= 15) dig2 += 97 - 10;
  std::string r;
  r.append( &dig1, 1);
  r.append( &dig2, 1);
  return r;
}
