#include "mq.h"

// static int ok_to_run = 1;

void sig_hand( int sgnl ) {
    printf("\nCTRL-C Caught...Shutting down...\n\n");
    ok_to_run = 0;
    return;
}


int start_listening( void ) {
    int s = 0, i = 0;
    int failed_attempts = 0;
    struct sockaddr_un sa;
    for (; i < 4; i++) {
        printf("Trying to configure socket...\n");
        unlink(SOCKET_PATH);
        s = socket(AF_UNIX, SOCK_STREAM, 0);
        if (-1 == s) {
            printf("Failed to create socket descriptor\n");
            usleep(1000);
            continue;
        }
        memset(&sa, 0, sizeof(sa));
        sa.sun_family = AF_UNIX;
        sprintf(sa.sun_path, "%s", SOCKET_PATH);
        if (-1 == bind(s, (struct sockaddr *)&sa, sizeof(struct sockaddr_un))) {
            close(s);
            printf("mq.c: Call to bind() for listening socket failed.\n");
            usleep(1000);
            continue;
        }
        if (-1 == listen(s, 500)) {
            shutdown(s, 2);
            close(s);
            printf("mq.c > start_listening(): listen() system call failed.\n\n");
            usleep(1000);
            continue;
        }
        break;
    }
    if ( 5 == i ) return 0;
    chmod(SOCKET_PATH, 0777);
    return s;
}

int main(int argc, char **argv) {
    int i = 0, sel_ret = 0;
    int s_listener = 0, new_client = 0;
    struct sigaction sigact;
    time_t timeout = time(0) + 10;
    fd_set read_fds;
    struct timeval tv;
    struct sockaddr client;
    socklen_t socklen = 0;
    PIPC_CONNECTION conn;

    /* Zero out our lists */
    memset(&request_list, 0, sizeof(request_list));
    memset(&connection_list, 0, sizeof(connection_list));

    /* Set up signal handlers */
    memset(&sigact, 0, sizeof(struct sigaction));
    sigact.sa_handler = &sig_hand;

    sigaction(SIGTERM, &sigact, NULL);
    sigaction(SIGINT, &sigact, NULL);


    s_listener = start_listening();
    if (!s_listener) {
        return 1;
    }
    printf("Message Queue is now listening...\n");
    /* Remember to lock the message mutex whenever we're manipulating messages
     * or we're using message manipulation functions!
     * 
     * pthread_mutex_lock(&message_mutex);
     * 
     */

    while (ok_to_run) {
        if (0 == s_listener) {
            printf("Connection broken...attempting to reestablish listening connection...\n");
            s_listener = start_listening();
            if (!s_listener) {
                ok_to_run = 0;
                break;
            }
        }
        FD_ZERO(&read_fds);
        FD_SET(s_listener, &read_fds);
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        sel_ret = select((s_listener+1), &read_fds, 0, 0, &tv);
        if (-1 == sel_ret) { // Error condition on socket.  Attempt recover.
            if (EINVAL == errno) {
                printf("The value within \"timeout\" was invalid.\n\n");
                ok_to_run = 0;
            }
            close(s_listener);
            s_listener = 0;
        } else if ( sel_ret > 0 ) {
            // Create a new IPC_CONNECTION here and pass it as a param to the thread.
            socklen = sizeof(struct sockaddr);
            new_client = accept(s_listener, &client, &socklen);
            if (new_client > 0) {
                conn = calloc(1, sizeof(IPC_CONNECTION));
                conn->is_active = 1;
                conn->okay_to_run = 1;
                conn->socket = new_client;
                if (pthread_create(&conn->thread, 0, &connection, conn)) {
                    free(conn);
                    shutdown(new_client, 2);
                    close(new_client);
                }
                new_client = 0;
            } 
        }
    }

    if (s_listener > 0) {
        shutdown(s_listener, 2);
        close(s_listener);
    }

    /* Shut down all threads and sockets */
    
    return 0;
}


/* The thread that handles each incoming connection */
void *connection( void *param ) {
    int sel_ret = 0;
    size_t i = 0, j = 0;
    ssize_t bytes_read = 0;
    size_t bytes_to_move = 0, bytes_buffered = 0, init_offset = 0, next_size = 0;
    size_t waiting_on_size = 0;             /* All messages begin with "xxxx:", with xxxx being the number
                                             * of bytes we're expecting in this message.  On data received,
                                             * we'll take the number before the ":", and read until we've
                                             * received that quantity of bytes. We will decrement this count
                                             * until we've hit ZERO. At which point we'll buffer the message
                                             * and prepare for the next.
                                             * */

    time_t drop_buffered_data_timeout = 0;  /* This is the timeout after which we'll drop the data we have
                                             * buffered because we didn't seem to get a complete response.
                                             * */
    struct timeval tv;
    PIPC_CONNECTION connection = (PIPC_CONNECTION)param;
    char buffer[65536];
    fd_set fdr;
    time_t timeout = time(0) + 20;


    if (param == 0) return 0;
    memset(buffer, 0, 63336);
    while (connection->okay_to_run) {
        FD_ZERO(&fdr);
        FD_SET(connection->socket, &fdr);
        tv.tv_sec = 0;
        tv.tv_usec = 500000; /* Each thread will wait 1/2 second in its select() to make them more responsive than the main thread */
        sel_ret = select( (connection->socket + 1), &fdr, 0, 0, &tv);
        if (-1 == sel_ret) {
            break;
        } else if ( sel_ret > 0) {
            if (FD_ISSET(connection->socket, &fdr)) {

                if (65536-bytes_buffered == 0) {
                    /* Here, our buffer is completely full and we have STILL not received a "message_length" indicator
                     * signalling the start of a message.  Did a module go crazy??  Flush the buffer.
                     * */
                    memset(buffer, 0, 65536);
                    bytes_buffered = 0;
                }
                bytes_read = recv(connection->socket, &buffer[bytes_buffered], 65536-bytes_buffered, 0);
                if ( bytes_read <= 0) {
                    break;
                }
                bytes_buffered+=bytes_read;
                if (handle_stream(buffer, (ssize_t *)&bytes_buffered)) {
                    bytes_buffered = 0;
                }
            }
        }   

    }
    printf("Exiting connection thread for module \"%s\"\n", connection->module_name);
    connection->is_active = 0;
    shutdown(connection->socket, 2);
    close(connection->socket);
    connection->socket = 0;
    return 0;
}



int handle_stream( char *buffer, ssize_t *bytes_buffered) {
    size_t new_message_start_offset = 0, new_message_size = 0, bytes_to_move = 0;
    ssize_t bytes;
    if (!bytes_buffered) return 0;
    bytes = *bytes_buffered;
    new_message_start_offset = find_next_message(buffer, bytes_buffered, &new_message_size);
    if (new_message_size > 0) {
        if (new_message_size <= *bytes_buffered) {
            handle_message(&buffer[new_message_start_offset], new_message_size);
            *bytes_buffered-=(new_message_size+new_message_start_offset);
            if ( *bytes_buffered > 0) {
                memmove(buffer, &buffer[new_message_size+new_message_start_offset], *bytes_buffered);
                return handle_stream(buffer, bytes_buffered);
            }
            return 1;
        }
    } 
    return 0;
}



/* find_next_message()
 *
 * This function scans a buffer for the first legitimate message
 * send from a CyberServer module.
 *
 *
 * param: { char * }  buffer - The buffer to scan for messages
 * param: { size_t }  length - The length (in octets) of the data in the buffer
 * param: { size_t *} data_length - A pointer to a "size_t" variable into which the length of the message is returned
 * 
 * returns: { size_t } The offset (in octets) of the start of the message.
 * 
*/
ssize_t find_next_message( char *buffer, ssize_t *bytes_buffered, size_t *data_length) {
    ssize_t i = 0, j = 0, start_offset = 0, size_search_offset = 0;
    ssize_t length = 0;
    char *start_position = 0;
    char *end_position = 0;
    size_t possible_data_length = -1;

    if (!bytes_buffered) return -1;
    if (!data_length) return -1;
    length = *bytes_buffered;
    *data_length = 0;

    start_position = (char *)strnstr(buffer, (char *)"message_length:", (size_t)length);

    if (!start_position) return 0;

    if (start_position != buffer) { /* There was garbage in front of "message_length:" */
        start_offset = (start_position - buffer);
        *bytes_buffered = (length-=start_offset);
        memmove(buffer, start_position, length);
        start_offset = find_next_message(buffer, bytes_buffered, data_length);
        return start_offset;
    } else { /* The "message_length:" was at the beginning */
        start_offset = 0;
        start_position = buffer;
        size_search_offset = start_offset+15;
        for (i = size_search_offset; i < *bytes_buffered; i++) {
            if (!IS_DIGIT(buffer[i])) {
                if (buffer[i] == ':') { /* The proper character to find next. */
                    end_position = &buffer[(i-1)];
                    possible_data_length = (size_t)strtoul(&buffer[size_search_offset], &end_position, 10);
                    if (possible_data_length > 0) { /* There is data */
                        *data_length = possible_data_length;
                        return (i+1);
                    } else { /* There's zero bytes  */
                        if (i == *bytes_buffered) { /*  If the place where the number representing the length of the 
                                                        message should be is the end of the data */
                            *bytes_buffered = 0;
                            return -1;
                        } else {
                            length = (*bytes_buffered - (i+1));
                            memmove(buffer, &buffer[(i+1)], length);
                            *bytes_buffered = length;
                            start_offset = find_next_message(buffer, bytes_buffered, data_length);
                            return start_offset;
                        }
                    }
                } else {
                    /* Protocol error. Drop this packet */
                    *bytes_buffered = 0;
                    return 0;
                }
                break;
            }
        }
    }
}


int handle_message( char *message, size_t length) {
    if (!message || !length) return 0;
    printf("\n************************************************************************\n");
    printf("Message Rec'd: \"%.*s\"\n", length, message);
    printf("************************************************************************\n\n\n");
    return 1;
}


#ifndef strnstr
char *strnstr( char *haystack, char *needle, size_t length) {
    size_t i = 0, needle_length = 0;
    if (!haystack || !needle) return 0;
    needle_length = strlen(needle);
    while ( (i+needle_length) <= length ) {
        if (!strncmp(&haystack[i], needle, needle_length)) return &haystack[i];
        i++;
    }
    return 0;
}
#endif