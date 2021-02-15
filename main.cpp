#include <iostream>
#include <unistd.h>  // sleep

#include "soundio/soundio.h"
#include "stb_vorbis.c"
#include "audio.cpp"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>

// http://beej.us/guide/bgnet/html/


#define PORT "4950"    // the port users will be connecting to
#define MAXBUFLEN 100

// get sockaddr, IPv4 or IPv6:


int CreateRecieveSocket()
{
    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    int numbytes;
    struct sockaddr_storage their_addr;
    char buf[MAXBUFLEN];
    socklen_t addr_len;
    char s[46];

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET6; // set to AF_INET to use IPv4
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE; // use my IP

    if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and bind to the first we can
    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                             p->ai_protocol)) == -1) {
            perror("listener: socket");
            continue;
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("listener: bind");
            continue;
        }

        fcntl(sockfd, F_SETFL, O_NONBLOCK);

        break;
    }

    if (p == NULL) {
        fprintf(stderr, "listener: failed to bind socket\n");
        return 2;
    }

    freeaddrinfo(servinfo);

    return sockfd;
//
//    addr_len = sizeof their_addr;
//    if ((numbytes = recvfrom(sockfd, buf, MAXBUFLEN-1 , 0,
//                             (struct sockaddr *)&their_addr, &addr_len)) == -1) {
//        perror("recvfrom");
//        exit(1);
//    }
//
//    printf("listener: got packet from %s\n",
//           inet_ntop(their_addr.ss_family,
//                     get_in_addr((struct sockaddr *)&their_addr),
//                     s, sizeof s));
//    printf("listener: packet is %d bytes long\n", numbytes);
//    buf[numbytes] = '\0';
//    printf("listener: packet contains \"%s\"\n", buf);
//
//    close(sockfd);
//
//    return 0;
}

//int CreateSendSocket()
//{
//    int sockfd;
//    struct addrinfo hints, *servinfo, *p;
//    int rv;
//    int numbytes;
//
//    memset(&hints, 0, sizeof hints);
//    hints.ai_family = AF_INET6; // set to AF_INET to use IPv4
//    hints.ai_socktype = SOCK_DGRAM;
//
//    if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
//        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
//        return 1;
//    }
//
//    // loop through all the results and make a socket
//    for(p = servinfo; p != NULL; p = p->ai_next) {
//        if ((sockfd = socket(p->ai_family, p->ai_socktype,
//                             p->ai_protocol)) == -1) {
//            perror("talker: socket");
//            continue;
//        }
//
//        break;
//    }
//
//    if (p == NULL) {
//        fprintf(stderr, "talker: failed to create socket\n");
//        return 2;
//    }
//
//    return sockfd;
//
////    if ((numbytes = sendto(sockfd, argv[2], strlen(argv[2]), 0,
////                           p->ai_addr, p->ai_addrlen)) == -1) {
////        perror("talker: sendto");
////        exit(1);
////    }
////
////    freeaddrinfo(servinfo);
////
////    printf("talker: sent %d bytes to %s\n", numbytes, argv[1]);
////    close(sockfd);
////
////    return 0;
//}




const int timeout = 200;
int main(int argc, char **argv)
{
    while (true)
    {
        int receive_fd = CreateRecieveSocket();

        Audio audio = InitAudio();

        audio.outstream->userdata = (void* ) &receive_fd;

        for (;;)
            soundio_wait_events(audio.soundio);

        soundio_disconnect(audio.soundio);

        sleep(timeout);

        // Reconnect.
        fprintf(stderr, "Trying to reconnect...\n");
        QuitAudio(&audio);
    }

    return 0;
}
