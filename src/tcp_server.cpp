#include <functional>
#include <string.h>
#include <iostream>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <cstdio>
#include <cstring>
#include <netdb.h>
#include <errno.h>
#include <stdexcept>
#include "io.h"
using namespace vm;

tcp_server::tcp_server()
    : connection_handler([](std::vector<tcp_socket>&){}),
      event_handler([](std::vector<tcp_socket>& sockets, tcp_socket&){})
{ init(); }

tcp_server::tcp_server(std::string host, std::string port)
    : connection_handler([](std::vector<tcp_socket>&){})
    , event_handler([](std::vector<tcp_socket>& sockets, tcp_socket&){})
    , epoll(host, port)
{ init(); }

void tcp_server::init()
{
    epoll.set_on_socket_connect([&](int fd)
				{
				    sockets.emplace_back(fd);
				    /* Make the incoming socket non-blocking and add it to the
				       list of fds to monitor. */
				    sockets.back().add_flag(O_NONBLOCK);
				    // same as with first socket
				    epoll.add_socket(sockets.back());
				    connection_handler(sockets);
				    std::cout << "ADDED" << std::endl;
				});
    epoll.set_on_data_income([&](int fd)
			     {
				 int index = -1;
				 // c++ cannot into linear search on vector, mda...
				 for (int i = 0; i < sockets.size(); i++)
				 {
				     if (sockets[i].get_fd() == fd)
				     {
					 index = i;
					 break;
				     }
				 }
				 if (index != -1)
				 {
				     event_handler(sockets, sockets[index]);
				 }
				 else std::cerr << "Got data from socket server is not tracking"
						<< std::endl;
			     });
}

void tcp_server::on_event_income(event_h handler) {
    event_handler = handler;
}

void tcp_server::on_connected(simple_h handler) {
    connection_handler = handler;
}

void tcp_server::start() {
    running = true;
    while (running) {
	epoll.process_data();
    }
}

bool tcp_server::is_running() {
    return running;
}