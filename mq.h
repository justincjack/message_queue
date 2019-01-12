/*
 * mq.h - Message Queue
 * This is the message queue module for Cyber Server.  The purpose of this 
 * program is to broker requests between modules.  This program will listen
 * on a UNIX (IPC) socket and queue messages.  It will dispatch the messages
 * to modules in the request and return the data back to the requesting
 * module when the message has been serviced.
*/
#include <stdio.h>
#include <time.h>
#include <pthread.h>
#include <errno.h>
#include <memory.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>

#define REQUEST_TIMEOUT(request) ((time(0)>(request->request_start+60))?1:0)
#define IS_DIGIT(x) ((x>='0' && x <='9')?1:0)

#define SOCKET_PATH "../sockets/mq.sock\0"

/* Forward declarations */
typedef struct cs_request_list REQUEST_LIST, *PREQUEST_LIST;

static int ok_to_run = 1;

/* Mutex protecting the linked-list for requests to which all thread have
 * access.
 */
static pthread_mutex_t request_list_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Mutex protecting the linked-list of connections to which all threads 
 * have access
*/
static pthread_mutex_t connection_list_mutex = PTHREAD_MUTEX_INITIALIZER;


/* A struct representing a module's connection to the message queue.  Modules will
 * be responsible for maintaining their connection to the message queue and will
 * reestablish connectivity if it's lost.  The message queue will keep any messages
 * queued for any particular connection until either they're delivered successfully
 * or they've timed out (after one minute).
 * 
 * IPC_CONNECTION objects will be replaced if a module re-connects.  Only one
 * connection per module is allowed.
 * 
*/
typedef struct ipc_connection {
    int socket;                 // The UNIX socket descriptor

    char module_name[100];      // The name of the module that's connected.

    pthread_t thread;           // A handle to the thread handling this connection

    int okay_to_run;            // Flag that is initialized to 1 and set to 0 when a
                                // connection thread should die.

    int is_active;              // Flag indicating if the connection is active and has its thread running.

} IPC_CONNECTION, *PIPC_CONNECTION;


/* Basic structure of a request.  Requests will time out after one minute */
typedef struct cs_request {
    time_t request_start;       // The time-stamp of when the request was received

    char req_module[100];       // The module that issued the request

    char target_module[100];    // The module for which the request is intended

    char *szrequest;            // The string request

    char *szresponse;           // The response.  If this buffer contains text AND the "serviced" member
                                // is set to "1", then we need to send the reply to the "req_module"

    int sent;                   // Set to "1" if the message has been delivered to the "target_module"

    int serviced;               // Set to 1 if the message has been marked as complete by the
                                // "target_module"
} CS_REQUEST, *PCS_REQUEST;

/* The linked list structure, this is typedef'd above */
struct cs_request_list {
    PCS_REQUEST head, request, next, previous, last;
};

typedef struct connection_list {
    PIPC_CONNECTION head, connection, next, previous, last;
} CONN_LIST, *PCONN_LIST;

/* The master linked list for all requests */
static REQUEST_LIST request_list;

static CONN_LIST connection_list;


int start_listening( void );


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
ssize_t find_next_message( char *buffer, ssize_t *bytes_buffered, size_t *data_length);



int handle_stream( char *buffer, ssize_t *bytes);

/* Processes a complete message */
// int process_message( PMQ_TEMP_BUFFER temp_buffer);

/* Returns the offset in the said buffer at which the data starts 
 * and sets "data_length" as the expected data size.
*/
// size_t get_content( char *buffer, size_t length, size_t *data_length );



int add_request(PIPC_CONNECTION connection, PCS_REQUEST request);

int remove_request(PIPC_CONNECTION connection, PCS_REQUEST request);

/* Attempt to deliver the request to the module for which it was intended */
int dispatch_request(PCS_REQUEST);

/* This function scans the master request list "request_list", and builds
 * the linked list for a specific connection.  This will be called each time
 * a new module connects to the message queue program. 
 * 
 * The main purpose of this is that if a module drops its connection and re-connects,
 * any dangling requests for this module are put back into the list for the
 * new connection.
*/
int build_connection_request_list(PIPC_CONNECTION connection);

/* Thread to handle the connection */
void *connection( void *param );

#ifndef strnstr
char *strnstr( char *haystack, char *needle, size_t length);
#endif
int handle_message( char *message, size_t length);