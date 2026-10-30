// H3C monitoring agent. This is translated into src/bdi_configuration.h.

!start.

+!start <- announce_ready.

+server_healthy <- report_healthy.

+server_critical <- report_critical.

+server_locked <- request_unlock.
