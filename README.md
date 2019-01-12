CyberServer Message Queue System

Done so far:

Created listening UNIX (IPC) socket on main thread that delegates new connections to the connection() function.

The connection function listens and sorts through anything (rather well) that it receives and only passes legitimate requests to the function "handle_message()" which runs in the context of the calling thread/connection.  It will be responsible for parsing the request and putting it into the mutex-protected linked list of messages to be serviced and responded to.

