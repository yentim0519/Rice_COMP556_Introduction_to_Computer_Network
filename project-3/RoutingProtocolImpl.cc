#include "RoutingProtocolImpl.h"
#include "Node.h"
#include <arpa/inet.h> // For big endian conversion

RoutingProtocolImpl::RoutingProtocolImpl(Node *n) : RoutingProtocol(n) { sys = n; }

RoutingProtocolImpl::~RoutingProtocolImpl() {}

void RoutingProtocolImpl::init(unsigned short num_ports, unsigned short router_id, eProtocolType protocol_type) {
	// Initialize router information
	this->num_ports = num_ports;
	this->router_id = router_id;
	this->protocol_type = protocol_type;

	// Initialize routing table & port table for this router 
	for (auto i = 0; i < this->num_ports; i++) {
		this->port_table.push_back(new Port_status(SPECIAL_PORT, INFINITY_COST, 0));
	}

	// Initialize alarms
	// 1. alarm for health check every second
	char *alarm_check = new char[1];
	alarm_check[0] = 'h'; 
	sys->set_alarm(this, 1000, alarm_check);

	// 2. alarm for ping every 10 seconds
	char *alarm_ping = new char[1];
	alarm_ping[0] = 'p'; 
	sys->set_alarm(this, 10000, alarm_ping);

	// 3. alarm for routing table update every 30 seconds
	char *alarm_protocal = new char[1];
	alarm_protocal[0] = 'r'; 
	sys->set_alarm(this, 30000, alarm_protocal);

	// First ping
	for (unsigned short i = 0; i < (unsigned short)port_table.size(); i++) {
		ping(router_id, sys->time(), i);
	}
}

void RoutingProtocolImpl::handle_alarm(void *data) {
	char *alarm_type = (char *)data;

	if (alarm_type[0] == 'h') { // Data is 'h' -> alarm for health check
		port_stat_check();
		router_stat_check();
		sys->set_alarm(this, 1000, alarm_type);

	} else if (alarm_type[0] == 'p') { // Data is 'p' -> alarm for PING
		for (unsigned short i = 0; i < (unsigned short)port_table.size(); i++) {
			ping(router_id, sys->time(), i);
		}
		sys->set_alarm(this, 10000, alarm_type);

	} else if (alarm_type[0] == 'r') { // Data is 'r' -> alarm for routing table update
		send_table_update_dv();
		sys->set_alarm(this, 30000, alarm_type);
	}
}

/* A packet pkt of size arrives via port number p */
void RoutingProtocolImpl::recv(unsigned short port, void *packet, unsigned short size) {
	char *recv_packet = (char *)packet;
	int packet_type = (int)(recv_packet[0]);

	if (packet_type == 0) { // Packet type: DATA
		recv_data(port, recv_packet);

	} else if (packet_type == 1) { // Packet type: PING
		recv_ping(port, recv_packet);

	} else if (packet_type == 2) { // Packet type: PONG
		recv_pong(port, recv_packet);

	} else if (packet_type == 3) { // Packet type: DV
		recv_DV(port, recv_packet);
	} else if (packet_type == 4) { // Packet type: LS
		//TODO: recv_LS
	}
}

void RoutingProtocolImpl::ping(unsigned short rid, int32_t t, unsigned short pid) {
	// Format the ping packet
	char *packet = new char[PACKETSIZE];
	packet[0] = (char)PING;						  // Packet type: 1 bytes, also 1 bytes reserved as instructed -> cast to char so don't need hton
	*(unsigned short *)(packet + 2) = htons(PACKETSIZE); // Packet size: 2 bytes
	*(unsigned short *)(packet + 4) = htons(rid); // Source router id: 2 bytes, also 2 bytes reserved for dest router id
	*(int32_t *)(packet + 8) = htonl(t);		  // packet sent time

	// Send ping packet with system interface
	sys->send(pid, packet, PACKETSIZE);
}

void RoutingProtocolImpl::recv_ping(unsigned short port_id, char *recv_packet) {
	// Pong it back
	pong(port_id, recv_packet);
}

void RoutingProtocolImpl::pong(unsigned short port_id, char *recv_packet) {
	// Format the pong packet
	unsigned short sender_rid = ntohs(*(unsigned short *)(recv_packet + 4));
	recv_packet[0] = (char)PONG;
	*(unsigned short *)(recv_packet + 4) = htons(router_id);  // Modify Source ID to this router's id
	*(unsigned short *)(recv_packet + 6) = htons(sender_rid); // Fill in destination router id

	// Send pong packet with system interface
	sys->send(port_id, recv_packet, PACKETSIZE);
}

void RoutingProtocolImpl::recv_pong(unsigned short port_id, char *recv_packet) {
	// Extract information from packet, e.g. neighbor rid, cost to neighbor rid
	unsigned short src_rid = ntohs(*(unsigned short *)(recv_packet + 4));
	//unsigned short dest_rid = ntohs(*(unsigned short *)(recv_packet + 6));
	int32_t send_t = ntohl(*(int32_t *)(recv_packet + 8));

	// Update port entry for port_id
	unsigned short prev_cost = port_table[port_id]->cost_to_neighbor;
	int32_t curr_t = sys->time();
	port_table[port_id]->cost_to_neighbor = curr_t - send_t; // Update RTT/cost to nbr
	port_table[port_id]->pong_t = curr_t;					 // Update lost time receiving pong
	port_table[port_id]->to_router_id = src_rid;			 // Update nbr

	if (protocol_type == P_DV) {
		// If link cost change(i.e changeDelay, linkComingup) -> update back to routing table
		bool routing_table_modified = false;
		update_rt(port_id, prev_cost, port_table[port_id]->cost_to_neighbor, routing_table_modified);

		if (routing_table_modified) {
			// Send dv update
			send_table_update_dv();
		}
	}	

	// Release packet memory
	delete recv_packet;
}

void RoutingProtocolImpl::recv_data(unsigned short port_id, char* recv_packet) {
	// Extract info
	auto packet_size = ntohs(*(unsigned short*)(recv_packet+2));
	//auto src_rid = ntohs(*(unsigned short*)(recv_packet+4));
	auto dest_rid = ntohs(*(unsigned short*)(recv_packet+6));

	// Have reached destination router
	if (dest_rid == router_id) {
		delete recv_packet;
		return;
	}

	// Check routing table to determine next hop
	if (routing_table.count(dest_rid)) {
		auto next_pid = routing_table[dest_rid]->to_port_id;
		if (routing_table[dest_rid]->cost_to_dest != INFINITY_COST)
			sys->send(next_pid, recv_packet, packet_size);
		return;

	} else {
		// No route available
		delete recv_packet;
		return;
	}
}

/* Upon receiving DV packet, update routing table information */
void RoutingProtocolImpl::recv_DV(unsigned short pid, char *recv_packet) {
	// Port_table has the latest info, since there might be delay in DV packet transmission
	// You did recv a DV update from pid, but then pid's link becomes dead
	if (port_table[pid]->cost_to_neighbor == INFINITY_COST) {
		return;
	}

	// Extract packet info
	auto packet_size = ntohs(*(unsigned short *)(recv_packet + 2));
	//auto src_rid = ntohs(*(unsigned short *)(recv_packet + 4));
	auto itr = (packet_size - 8) / 4;

	// Set dv_paylaod[dest_rid_i] = cost_i
	unordered_map<unsigned short, unsigned short> dv_payload;

	for (int i = 0; i < itr; i++) {
		auto rid_i = ntohs(*(unsigned short *)(recv_packet + 8 + 4 * i));
		auto cost_i = ntohs(*(unsigned short *)(recv_packet + 8 + 4 * i + 2));

		// Store payload in vector
		if (rid_i != router_id) { // Avoid A->B->A 
			dv_payload[rid_i] = cost_i;
		}
	}

	// Update routing table based on the newly received DV update
	bool table_modified = false;
	update_rt(pid, dv_payload, table_modified);
	if (table_modified) {
		send_table_update_dv();
	}
}

/* Send dv table to nbr routers */
void RoutingProtocolImpl::send_table_update_dv() {
	// If there isn't any routing_table yet, no need to send to others
	if (routing_table.size() == 0) { return; }

	// Send to all port neighbors
	vector<unsigned short> rids; // Store the (reachable)destination router ids
	for (auto it = routing_table.begin(); it != routing_table.end(); it++) {
		if (it->second->cost_to_dest != INFINITY_COST) {
			rids.push_back(it->first);
		}
	}
	
	// Compute dv packet size
	unsigned short dv_packet_size = 8 + 4 * (unsigned short)rids.size(); // 8 + 4 * (# of nbr nodes that are able to recv)

	int pid = 0;
	for (auto &p : port_table) {
		// If port can reach neighbor && nbr router is not myself
		if (p->cost_to_neighbor != INFINITY_COST && p->to_router_id != 0xffff) {
			// Construct DV packet -- header
			char *send_packet_dv = new char[dv_packet_size];
			send_packet_dv[0] = (char)DV;									  // First byte indicating the type of packet; second byte is reserved
			*(unsigned short *)(send_packet_dv + 2) = htons(dv_packet_size);  // 2 bytes for size of packet
			*(unsigned short *)(send_packet_dv + 4) = htons(router_id);		  // 2 bytes for source router id
			*(unsigned short *)(send_packet_dv + 6) = htons(p->to_router_id); // 2 bytes for dest router id

			// Construct DV packet -- payload
			int shift = 8;
			for (int i = 0; i < (int)rids.size(); i++) {
				*(unsigned short *)(send_packet_dv + shift) = htons(rids[i]);
				// Poison reverse to avoid go to infinity problem
				// Idea: If I go through pid to reach destination, I tell the nbr of this pid that the cost if infinity
				if (routing_table[rids[i]]->to_port_id == pid) {
					*(unsigned short *)(send_packet_dv + shift + 2) = htons(INFINITY_COST);
				} else {
					*(unsigned short *)(send_packet_dv + shift + 2) = htons(routing_table[rids[i]]->cost_to_dest);
				}

				shift += 4;
			}
			
			// Send out the packet
			sys->send(pid, send_packet_dv, dv_packet_size);
		}
		pid++;
	}
}

/* A generic forwarding table implemenation for both DV & LS */
void RoutingProtocolImpl::router_stat_check() {
	int32_t curr_t = sys->time();
	for (auto it = routing_table.begin(); it != routing_table.end(); it++) {
		unsigned short dest_rid = it->first;
		Routing_table_info *routing_table_entry = it->second;

		// DV, LS entry that is not refreshed within 45 seconds are timed out
		if (curr_t - routing_table_entry->last_update_t > 45000) {
			if (protocol_type == P_DV) { // Protocal is DV
				// Update routing table
				routing_table_entry->cost_to_dest = INFINITY_COST;
				routing_table_entry->last_update_t = curr_t;

				// Look at current port table:
				// if port table has link to this dest_rid & better cost, then update it to routing table
				for (int i = 0; i < (int)port_table.size(); i++) {
					if (port_table[i]->to_router_id == dest_rid) {
						if (port_table[i]->cost_to_neighbor < routing_table_entry->cost_to_dest) {
							routing_table_entry->cost_to_dest = port_table[i]->cost_to_neighbor;
							routing_table_entry->to_port_id = i;
							routing_table_entry->last_update_t = curr_t;
							//table_updated = true;
						}
					}
				}
			} else if (protocol_type == P_LS) { // Protocal is LS
				// TODO: Declare link dead for LS protocol
			}
		}
	}
}

/* Port status check every 1 sec */
void RoutingProtocolImpl::port_stat_check() {
	auto curr_t = sys->time();

	unsigned short pid = 0;
	for (auto &port : port_table) {
		// Check port active or dead, need to update routing table if status changed
		// Dead port: status not refreshed for 15 secs
		if (curr_t - port->pong_t > 15000 && port->cost_to_neighbor != INFINITY_COST) {
			int32_t prev_cost_to_nbr = port->cost_to_neighbor;
			port->cost_to_neighbor = INFINITY_COST;

			if (protocol_type == P_DV) {
				bool table_modified = false;
				update_rt(pid, prev_cost_to_nbr, INFINITY_COST, table_modified);
				if (table_modified) {
					send_table_update_dv();
				}
			} else if(protocol_type == P_LS) {
				// TODO: Update info in routing table based on port status update
			}
		}
		pid++;
	}
}

/* prev(new)_cost: pid's prev(new)_cost */
void RoutingProtocolImpl::update_rt(unsigned short pid, int32_t prev_cost, int32_t new_cost, bool &table_modified) {
	if (prev_cost != new_cost) {
		for (auto it = routing_table.begin(); it != routing_table.end(); it++) {
			
			Routing_table_info *entry = it->second;
			
			// Update every rt_entry that actually goes through pid 
			if (entry->cost_to_dest != INFINITY_COST && entry->to_port_id == pid) {
				// Only update when there's change in cost && you route through port pid
				if (new_cost == INFINITY_COST) {
					entry->cost_to_dest = INFINITY_COST;
				} else {
					entry->cost_to_dest += (new_cost - prev_cost);
				}

				entry->last_update_t = sys->time(); // Update "last updated time for routing entry"
				table_modified = true;
				
				// But, if the updated cost increases to get to dest through pid
				// perhaps I can achieve better cost through a different pid
				for (int i = 0; i < (int)port_table.size(); i++) {
					if (port_table[i]->to_router_id == it->first) {
						if (port_table[i]->cost_to_neighbor < entry->cost_to_dest) {
							entry->cost_to_dest = port_table[i]->cost_to_neighbor;
							entry->to_port_id = i;
						}
					}
				}
			}
		}
	}

	if(new_cost != INFINITY_COST) {
		Port_status *p = port_table[pid];
		unsigned short nbr_router_id = p->to_router_id;

		// this nbr router id exists in routing_table as a key, i.e. it's one of the dest router
		// But to reach this dest router we might not have passed through pid
		if (routing_table.count(nbr_router_id)) {
			if (routing_table[nbr_router_id]->cost_to_dest > new_cost) {
				routing_table[nbr_router_id]->cost_to_dest = new_cost;
				routing_table[nbr_router_id]->to_port_id = pid;
				routing_table[nbr_router_id]->last_update_t = sys->time();
				table_modified = true;
			}
		} else {
			// This nbr router is newly added & reachable, insert to routing table
			if (new_cost != INFINITY_COST) {
				auto t = sys->time();
				struct Routing_table_info *rt_info = new Routing_table_info(pid, new_cost, t);
				routing_table[nbr_router_id] = rt_info;
				table_modified = true;
			}
		}
	}
}

/* Update Routing table based on dv_payload */
void RoutingProtocolImpl::update_rt(unsigned short pid, unordered_map<unsigned short, unsigned short> &dv_paylaod, bool &table_modified) {
	// In the pdf's example, this is the "Y"
	auto nbr_rid = port_table[pid]->to_router_id;

	// Loop through my routing table
	// See if any cost_to_dest of an entry can be improved
	for (auto &it : routing_table) {
		auto dest_rid = it.first;
		auto rt_entry = it.second;

		if (dest_rid == nbr_rid) { // A->Y->Y : Not what we want to update (we want A->Y->V)
			continue;
		}

		if (dv_paylaod.count(dest_rid)) { // Y->V exists in dv_payload
			if (rt_entry->to_port_id == pid) {		 // A->Y exists
				auto prev_cost = rt_entry->cost_to_dest;
				// cost(A->Y) + cost(Y->V) < cost(A->V)
				if (dv_paylaod[dest_rid] != INFINITY_COST) {
					rt_entry->cost_to_dest = port_table[pid]->cost_to_neighbor + dv_paylaod[dest_rid];
				} else {
					rt_entry->cost_to_dest = INFINITY_COST;
				}
				
				// Update some variables
				if (rt_entry->cost_to_dest != prev_cost) {
					table_modified = true;
				}
				
				rt_entry->last_update_t = sys->time();

				// If cost actually went up, check if there's a better port that I can switch to
				if (rt_entry->cost_to_dest > prev_cost) {
					for (int i = 0; i < (int)port_table.size(); i++) {
						if (port_table[i]->to_router_id == dest_rid) {
							if (port_table[i]->cost_to_neighbor < rt_entry->cost_to_dest) {
								rt_entry->cost_to_dest = port_table[i]->cost_to_neighbor;
								rt_entry->to_port_id = i;
								table_modified = true;
							}
						}
					}
				}
			} else if (port_table[pid]->cost_to_neighbor + dv_paylaod[dest_rid] < rt_entry->cost_to_dest) {
				// Same update, with the first if, but now even the port is changed
				rt_entry->cost_to_dest = port_table[pid]->cost_to_neighbor + dv_paylaod[dest_rid];
				rt_entry->last_update_t = sys->time();
				rt_entry->to_port_id = pid;
				table_modified = true;
			}
		} else if (rt_entry->to_port_id == pid) { // Y can't no longer get to V, that's why V is not in dv_payload
			// I can get to V before through Y, but now I shouldn't be able to
			if (rt_entry->cost_to_dest != INFINITY_COST) {
				rt_entry->cost_to_dest = INFINITY_COST;
				rt_entry->last_update_t = sys->time();
				table_modified = true;

				// Now I cannot do A->Y->V, A->V is marked as INF
				// But A->V might still have lower cost through a different pid
				for (int i = 0; i < (int)port_table.size(); i++) {
					// A->V exists through port i & has better cost
					if (port_table[i]->to_router_id == dest_rid && port_table[i]->cost_to_neighbor < rt_entry->cost_to_dest) {
						rt_entry->cost_to_dest = port_table[i]->to_router_id;
						rt_entry->to_port_id = i;
					}
				}
			}
		}
	}

	// If rt doesn't have the dv sender as a destination, add it to rt
	for (auto &it : dv_paylaod) {
		auto dest_rid = it.first;
		if (!routing_table.count(dest_rid)) {
			auto cost = port_table[pid]->cost_to_neighbor + dv_paylaod[dest_rid];
			auto cur_t = sys->time();
			routing_table[dest_rid] = new Routing_table_info(pid, cost, cur_t);
			table_modified = true;
		}
	}
}