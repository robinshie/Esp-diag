// H3C monitoring agent. This is translated into src/bdi/bdi_configuration.h.

!start.

+!start <- announce_ready.

+server_healthy <- report_healthy.

+server_critical <- report_critical.

+server_locked <- request_unlock.

+switch_link_fault <- recover_interface.

// Native Ethernet heartbeat context; never recovers business component faults.
+ssh_connected <- ping_switch.
+!verify_switch_heartbeat <- ping_switch.
+switch_heartbeat_ok <- report_heartbeat_ok.
+switch_heartbeat_lost <- report_heartbeat_lost.
+switch_heartbeat_unknown <- report_heartbeat_unknown.
