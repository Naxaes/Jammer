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

#include "libraries/stb_vorbis.c"

#define SERVERPORT "4950"    // the port users will be connecting to

const char* music = "/Users/tedkleinbergman/Programming/Older projects/Java/stendhal/data/music/lost_in_the_fog.ogg";



int main(int argc, char *argv[])
{
//    int channel_count = -1;
//    int sample_rate   = -1;
//    short* output     = nullptr;
//    int sample_count = stb_vorbis_decode_filename(music, &channel_count, &sample_rate, &output);
//
//    printf("channel_count: %i\n", channel_count);
//    printf("sample_rate:   %i\n", sample_rate);     // samples per second.
//    printf("sample_count:  %i\n", sample_count);


    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    int numbytes;

    if (argc != 2) {
        fprintf(stderr,"usage: <hostname>\n");
        exit(1);
    }

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET6; // set to AF_INET to use IPv4
    hints.ai_socktype = SOCK_DGRAM;

    if ((rv = getaddrinfo(argv[1], SERVERPORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    int yes = 1;

    // loop through all the results and make a socket
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                             p->ai_protocol)) == -1) {
            perror("talker: socket");
            continue;
        }

        setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

        break;
    }

    if (p == NULL) {
        fprintf(stderr, "talker: failed to create socket\n");
        return 2;
    }

//    sleep(10);

    short output[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    int bytes_sent = 0;
    unsigned short frame = 0;
    while (bytes_sent < 10)
    {
        char message[4] = {frame, output[frame]};
        memcpy(message, output+frame, 4);

        if ((numbytes = sendto(sockfd, message, 4, 0, p->ai_addr, p->ai_addrlen)) == -1) {
            perror("talker: sendto");
            exit(1);
        } else
        {
            printf("%d: Sent byte %d to %d: ", frame, bytes_sent, bytes_sent + numbytes);
        }

        bytes_sent += numbytes - 1;
        frame++;
    }

    freeaddrinfo(servinfo);

    printf("talker: sent %d bytes to %s\n", numbytes, argv[1]);
    close(sockfd);

    return 0;
}
