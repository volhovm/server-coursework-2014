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
#include <vector>
#include <functional>
#include "io.h"

#define MAXEVENTS 64

int main(int argc, char *argv[]) {
    int s, efd;
    struct epoll_event event;
    struct epoll_event *events;


    tcp_socket main_socket("", argv[1]);
    main_socket.add_flag(O_NONBLOCK);
    main_socket.listento();
    efd = epoll_create1(0);
    if (efd == 0) {
	perror("epoll_create");
	abort();
    }
    // epollin -- read, epollet -- edge triggered behaviour (wait returns
    // each time there`s data in fd left
    // ??? WHY DO WE NEED TO SET event.data.fd, WHEN WE'RE ALREADY PASSING IN sfd ???
    event.data.fd = main_socket.get_fd();
    event.events = EPOLLIN | EPOLLET;
    s = epoll_ctl(efd, EPOLL_CTL_ADD, main_socket.get_fd(), &event);
    if (s == -1) {
	perror("epoll_ctl");
	abort();
    }
    events = (epoll_event *) calloc(MAXEVENTS, sizeof event);
    std::vector<tcp_socket> sockets;
    for (;;) {
	int n, i;

	n = epoll_wait(efd, events, MAXEVENTS, -1);
//        fprintf(stdout, "epoll wait succeeded\n");
	for (i = 0; i < n; i++) {
	    // error, hang up or not read
	    if ((events[i].events & EPOLLERR) ||
		    (events[i].events & EPOLLHUP) ||
		    (!(events[i].events & EPOLLIN))) {
		fprintf(stderr, "epoll error\n");
		// closing file descriptor

		continue;
	    }
		// this event seems ok, it's about our socket, let's connect it
	    else if (main_socket.get_fd() == events[i].data.fd) {
		for (; ;) {
		    struct sockaddr in_addr;
		    socklen_t in_len;
		    char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
		    in_len = sizeof in_addr;
		    /* We're getting new socket s from sfd, in &in_addr we'll
		       get info about connected side, in_len before
		       call contains sizeof, after -- length of
		       adress in bytes. If socket non-blockable,
		       accept will not hang but return EAGAIN
		     */
		    sockets.emplace_back(accept(main_socket.get_fd(), &in_addr, &in_len));
		    if (sockets.back().get_fd() == -1) {
			// no connection? ok then
			// we've proceeded all incoming connections
			if ((errno == EAGAIN) ||
				(errno == EWOULDBLOCK)) {
			    break;
			}
			    //dat's strange
			else {
			    perror("accept");
			    break;
			}
		    }
		    /* so we got valid addr and len, let's get some data
		       we'll get machine name into hbuf, port into sbuf
		       MAGIC CONSTANTS -- return hbuf and sbuf in numeric form
		     */
		    if (getnameinfo(&in_addr, in_len,
			    hbuf, sizeof hbuf,
			    sbuf, sizeof sbuf,
			    NI_NUMERICHOST | NI_NUMERICSERV) == 0) {
			printf("Accepted connection on descriptor %d "
				"(host=%s, port=%s)\n", sockets.back().get_fd(), hbuf, sbuf);
		    }

		    /* Make the incoming socket non-blocking and add it to the
		       list of fds to monitor. */
		    sockets.back().add_flag(O_NONBLOCK);
		    // same as with first socket
		    event.data.fd = sockets.back().get_fd();
		    event.events = EPOLLIN | EPOLLET;
		    if (epoll_ctl(efd, EPOLL_CTL_ADD, sockets.back().get_fd(), &event) == -1) {
			perror("epoll_ctl");
			throw std::runtime_error("on epoll_ctl_add");
		    }
		}
		continue;
	    }
	    else {
		/* Let's read all the data available on event.fd
		   We also have edge-triggered mode, so we must
		   read all data as we won't get another notification
		 */
		if (process_data(
				 events[i].data.fd,
				 [&] (char* input) {
				     for (int j = 0; j < sockets.size(); j++) {
					 sockets[i].write(input);
				     }
				 }
				 ) != 0) {
		    printf("Closed connection on descriptor %d\n",
			    events[i].data.fd);
		    /* Closing the descriptor will make epoll remove it
		       from the set of descriptors which are monitored. */
		    close(events[i].data.fd);
		}
	    }
	}
    }
    free(events);
    return EXIT_SUCCESS;
}
