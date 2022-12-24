#ifndef ROUTINGPROTOCOLIMPL_H
#define ROUTINGPROTOCOLIMPL_H

#include "RoutingProtocol.h"
#include <stdint.h>
#include <vector>
#include <unordered_map>

struct Routing_table_info {
  unsigned short to_port_id; // To reach destination, go to to_port_id
  int cost_to_dest;          // Cost to destination
  int last_update_t;         // Last updated time for this entry
  Routing_table_info(unsigned short pid, int cost, int update_t) : to_port_id(pid), cost_to_dest(cost), last_update_t(update_t){};
};

struct Port_status {
  unsigned short to_router_id; // This port will take me to router "to_router_id"
  int32_t cost_to_neighbor;
  int32_t pong_t;      // last ping/pong time

  Port_status(unsigned short to, int32_t c, int32_t t2) : to_router_id(to), cost_to_neighbor(c), pong_t(t2){};
};

class RoutingProtocolImpl : public RoutingProtocol {
public:
  RoutingProtocolImpl(Node *n);
  ~RoutingProtocolImpl();

  void init(unsigned short num_ports, unsigned short router_id, eProtocolType protocol_type);
  // As discussed in the assignment document, your RoutingProtocolImpl is
  // first initialized with the total number of ports on the router,
  // the router's ID, and the protocol type (P_DV or P_LS) that
  // should be used. See global.h for definitions of constants P_DV
  // and P_LS.

  void handle_alarm(void *data);
  // As discussed in the assignment document, when an alarm scheduled by your
  // RoutingProtoclImpl fires, your RoutingProtocolImpl's
  // handle_alarm() function will be called, with the original piece
  // of "data" memory supplied to set_alarm() provided. After you
  // handle an alarm, the memory pointed to by "data" is under your
  // ownership and you should free it if appropriate.

  void recv(unsigned short port, void *packet, unsigned short size);
  // When a packet is received, your recv() function will be called
  // with the port number on which the packet arrives from, the
  // pointer to the packet memory, and the size of the packet in
  // bytes. When you receive a packet, the packet memory is under
  // your ownership and you should free it if appropriate. When a
  // DATA packet is created at a router by the simulator, your
  // recv() function will be called for such DATA packet, but with a
  // special port number of SPECIAL_PORT (see global.h) to indicate
  // that the packet is generated locally and not received from
  // a neighbor router.

  void port_stat_check();
  void router_stat_check();
  void ping(unsigned short rid, int32_t time, unsigned short port_id);
  void pong(unsigned short port_id, char *recv_packet);
  void recv_ping(unsigned short port_id, char *recv_packet);
  void recv_pong(unsigned short port_id, char *recv_packet);
  void recv_DV(unsigned short port_id, char *recv_packet);
  void recv_data(unsigned short port_id, char* recv_packet);
  void send_table_update_dv();
  void update_rt(unsigned short pid, int32_t prev_cost, int32_t new_cost, bool &table_updated);                       // Update routing table based on (port cost)link cost change
  void update_rt(unsigned short pid, unordered_map<unsigned short, unsigned short> &dv_payload, bool &table_updated); // Update routing table based on newly received DV payload

private:
  Node *sys;                // To store Node object; used to access GSR9999 interfaces
  unsigned short num_ports; // # of ports on this router
  unsigned short router_id; // ID of this router
  const int32_t PACKETSIZE = 12;
  eProtocolType protocol_type; // Distant vector or Link state

  /* Initialize routing tables for this router */
  unordered_map<unsigned short, Routing_table_info *> routing_table; // key = dest router id
  vector<Port_status *> port_table;                                  // port_table[port id] = Port_status(a struct containing info for this port)
};

#endif
