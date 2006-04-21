//   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/other/amudp/sockaddr.h,v $
//     $Date: 2006/04/21 08:31:09 $
// $Revision: 1.6 $
// Description: Objects for encapsulating and hashing SockAddr's
// Copyright 1998, Dan Bonachea


#ifndef _SOCKADDR_H
#define _SOCKADDR_H

#include "socket.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#define LOCALHOST (u_long)0x7F000001

#if !defined(AMUDP_NDEBUG) && !defined(NDEBUG)
#define DEBUGIP() this->IPStr(); 
#else
#define DEBUGIP() ; 
#endif 

 

class SockAddr {
  private:
    // could use a sockaddr here, but sockaddr_in is more robust because
    // it ensures correct struct alignment on architectures that care (eg SPARC/Solaris)
    sockaddr_in addr; // always stored in network byte order
    char IPBuffer[25];

  public:
    SockAddr() {
      memset(&addr,'\0',sizeof(sockaddr_in));
    }
    SockAddr(unsigned long IPaddr, unsigned short portnum, short sin_family=AF_INET) {
      addr.sin_family = sin_family;
      addr.sin_port = htons(portnum); // change to network format port
      addr.sin_addr.s_addr = htonl(IPaddr); // change to network format
      memset(&(addr.sin_zero), '\0', sizeof(addr.sin_zero));      
      DEBUGIP(); // makes it easier to see addresses in debugger
    }
    SockAddr(const char* IPStr, unsigned short portnum, short sin_family=AF_INET) { 
      addr.sin_family = sin_family;
      addr.sin_port = htons(portnum); // change to network format port
      #if 0
        addr.sin_addr.s_addr = inet_addr((char*)IPStr); // already in network format
      #else
        if (!inet_aton((char*)IPStr, (in_addr*)&addr.sin_addr.s_addr)) addr.sin_addr.s_addr = 0;
      #endif
      memset(&(addr.sin_zero), '\0', sizeof(addr.sin_zero));
      DEBUGIP(); // makes it easier to see addresses in debugger
    }
    SockAddr(const char* IPPortStr) { // parses from an FTP 227 or PORT string
      int num[6] = {0,0,0,0,0,0};
      while(!isdigit(*IPPortStr)) IPPortStr++; // skip leading text
      for (int i=0; i < 6 && *IPPortStr; i++) { // read each digit
        num[i] = atoi(IPPortStr);
        while(isdigit(*IPPortStr)) IPPortStr++; // skip digit
        while(!isdigit(*IPPortStr) && *IPPortStr) IPPortStr++; // skip separator
        }
      unsigned short portnum;
      char IPStr[30];
      sprintf(IPStr, "%i.%i.%i.%i", num[0], num[1], num[2], num[3]); 
      portnum = (unsigned short)(num[4] << 8 | num[5]);
      addr.sin_family = AF_INET;
      addr.sin_port = htons(portnum); // change to network format port
      addr.sin_addr.s_addr = inet_addr(IPStr); // already in network format
      memset(&(addr.sin_zero), '\0', sizeof(addr.sin_zero));
      DEBUGIP(); // makes it easier to see addresses in debugger
    }
    SockAddr(const sockaddr* sa) {
      memcpy(&addr, sa, sizeof(sockaddr));
      DEBUGIP(); // makes it easier to see addresses in debugger
    }
    SockAddr(const sockaddr_in* sa) {
      memcpy(&addr, sa, sizeof(sockaddr));
      DEBUGIP(); // makes it easier to see addresses in debugger
    }
    SockAddr(unsigned short _sa_family, char _sa_data[14]) {
      ((sockaddr*)&addr)->sa_family = _sa_family;
      memcpy(&(((sockaddr*)&addr)->sa_data), _sa_data, sizeof(((sockaddr*)&addr)->sa_data));
      DEBUGIP(); // makes it easier to see addresses in debugger
    }
    operator sockaddr*() { return (sockaddr*)&addr; }
    operator sockaddr_in*() { return &addr; }
    unsigned long IP() { return ntohl(addr.sin_addr.s_addr); }
    unsigned short port() { return ntohs(addr.sin_port); }
    char *IPStr() { 
      // inet_ntoa(((sockaddr_in*)&addr)->sin_addr); // This may screw up transparency
      unsigned char * pdata = (unsigned char *)(((sockaddr*)&addr)->sa_data);
      unsigned int a = pdata[2], b = pdata[3], c = pdata[4], d = pdata[5];
      sprintf(IPBuffer, "%u.%u.%u.%u",a,b,c,d);
      return IPBuffer; 
    }
    char *FTPStr() { 
      unsigned char * pdata = (unsigned char *)(((sockaddr*)&addr)->sa_data);
      unsigned int a = pdata[2], b = pdata[3], c = pdata[4], d = pdata[5];
      unsigned short prt = port();
      unsigned int e = (prt >> 8), 
                   f = (prt & 0xFF);
      sprintf(IPBuffer, "%u,%u,%u,%u,%u,%u",a,b,c,d,e,f);
      return IPBuffer; 
    }
    int operator==(const SockAddr& other) const { return !memcmp(this, &other.addr, sizeof(sockaddr)); }
    int operator!=(const SockAddr& other) const { return !(*this == other); }
};

#endif
