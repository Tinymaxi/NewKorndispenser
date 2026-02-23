#ifndef _WEB_SERVER_H
#define _WEB_SERVER_H

#include "dispenser_state.h"

// Initialize the HTTP web server on the given port.
// state must remain valid for the lifetime of the server.
void web_server_init(DispenserState* state, uint16_t port = 80);

#endif // _WEB_SERVER_H
