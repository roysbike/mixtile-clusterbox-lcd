#pragma once

#include "snapshot.h"
#include "ubus_client.h"

// Fills a snapshot from the live BMC. Status and identity are separate so a
// slow identity call cannot blank the node list.
bool pollStatus(UbusClient &ubus, ClusterSnapshot &snap);
bool pollIdentity(UbusClient &ubus, ClusterSnapshot &snap);
