# Device-side H3C observations and BDI policy

This directory is pure C++11 and has no Arduino, USB, MQTT, HTTP, AI-provider, or
SSH dependency. `observations.h` parses real Comware command responses;
`bdi_policy.h` derives beliefs, gates plans, and verifies their results. The SSH
worker executes an approved plan; it does not decide whether a port is faulty.
The implementation profile is the observed H3C S5130S-52S-EI, Comware 7.1.070,
Release 6328P03. Other identities remain read-only until their profile is verified.

The device's exact `LPDT is not configured.` response is a valid disabled-detector
observation, not proof that the network has no loop. `No link aggregation group
exists.` is a valid empty aggregation table. Contradictory messages, truncated
responses and unsuccessful commands remain invalid. Captured examples and a
whole-runtime replay live in `test/fixtures/h3c-r6328p03/` and `test/h3c_bdi.cpp`.

`bdi_agent.h` wraps the actual EmbeddedBDI `Agent`, `BeliefBase`, `Context`,
`PlanBase`, and `IntentionBase`. It maintains `loop_confirmed`, `action_allowed`,
and `ssh_ready` beliefs, and its isolation plan requires all three. The main loop
calls `H3cBdiAgent::deliberate(snapshot, policy, now)` and supplies `dispatch` and
`log` callbacks. Each round dispatches at most one authorized port; an incident
already dispatched remains latched until a later complete healthy observation.
The action re-evaluates policy immediately before dispatch. The caller must
still preserve the durable action ledger and disable new plans while a write or
verification is active. Run this agent on the same main loop as the heartbeat
agent; do not call it concurrently from SSH, MQTT, or HTTP tasks.

## Integration contract

Collect these fixed read-only commands after authentication. Each collection
round has a new `cycle`; changing target or SSH settings must also change
`targetRevision`. Stamp each result when it actually completes, not when queued.

| Command | Parser | Evidence |
| --- | --- | --- |
| `display version` | `supportedIdentity` | Exact supported model/release |
| `display interface brief` | `parseInterfaces` | Physical link, access/trunk/hybrid, PVID |
| `display interface brief down` | `parseDownCauses` | Administrative, physical, STP, loopback, other protection causes |
| `display loopback-detection` | `parseLoops` | Detector enabled state, interval, actual loop ports and action modes |
| `display stp brief` | `parseStp` | Per-instance role/state and active guard |
| `display link-aggregation verbose` | `parseAggregation` | All aggregated member ports, including unselected members |

```cpp
h3c::Stamp stamp;
stamp.cycle = collectionCycle;
stamp.atMs = completedAtMs;
stamp.targetRevision = targetRevision;
stamp.complete = result.state == "ok" && !result.truncated;
snapshot.interfaces = h3c::parseInterfaces(result.output, stamp);
// Assign the other four result tables using their matching parser.
snapshot.targetRevision = targetRevision;
snapshot.sshConnected = sshConnected;
snapshot.identityVerified = h3c::supportedIdentity(versionOutput);

auto beliefs = h3c::diagnose(snapshot, nowMs);
auto plan = h3c::isolationPlan(snapshot, policy, port, nowMs);
if (plan.allowed && tracker.begin(plan)) {
  // Submit plan.commands to the one SSH worker in this exact order.
  // Before submission, re-evaluate against the current target/policy/snapshot.
}
// Only a matching request's terminal result is supplied here.
tracker.executionResult(allFourCommandsSucceeded, completedAtMs);
// A new, complete observation round is required after execution.
tracker.observe(nextSnapshot, policy, nowMs);
```

`Policy` defaults to disabled. Production supplies the user's authorized business
ports, the known management port, and any additional protected ports. Authorization
does not override management-port, trunk/hybrid, STP root/master, active guard,
or aggregation-member protection. Unknown, failed, truncated, stale, future-dated,
wrong-target, or mixed-round observations block writes. Port identifiers are
canonicalized and cannot contain CLI separators or ranges.

`Runtime` starts without an authorized management path. A site adapter can call
`bindManagementProfile(host, sshPort, managementPort)` once for an established
path, then supply the actual SSH port to `configure`. Changing host or SSH port
revokes that binding; changing back does not silently reinstate it. A later
explicit binding is required, while read-only collection and analysis continue.
The adapter must persist revocation if it automatically reloads a historical site
binding after reboot. Password/timeout changes on the same endpoint do not infer
a different physical management port.

The permitted automatic action is a four-command per-port isolation transaction:
`system-view`, `interface <validated type> <validated number>`, `shutdown`,
`return`. The policy produces this plan only for an UP access port with explicit
uncontained loopback-detection evidence. An existing Block/Shutdown action mode
does not itself prove that forwarding is currently contained; an UP port remains
reported as a loop with unverified protection, and this policy does not override
that configured switch protection. A contained diagnosis requires corroborating
physical/administrative down evidence. The policy does not change STP, enable global
loop detection, save configuration, restart the switch, or interpret ping loss as
a loop. The worker must check each command result, stop on failure, and discard an
ambiguous session instead of replaying a write. Do not expose arbitrary command
strings through HTTP or MQTT.

## Durable intentions and scheduling

`runtime.h` is the main-loop coordinator for the fixed read-command cycle,
EmbeddedBDI deliberation, one outstanding SSH action, and readback verification.
Every result must match the requested ID, command and target. Reconfiguration
resets old observations and agent latches. It monitors a switch with loop
detection disabled without claiming that the topology is safe.

`action_ledger.h` keeps the action claim across reboot through three storage
callbacks: `read(key, value)`, `write(key, value)`, and `erase(key)`. Each returns
whether I/O succeeded; a successful read with an empty value means absent.
Keys fit ESP Preferences' 15-byte limit; the stored value retains the complete
endpoint and port identity so a hash collision fails closed. The claim is written
and read back before a switch write is allowed. Errors, corrupt values and
existing claims block replay. The firmware adapter must atomically persist each
string and must not report storage failure as an absent key.

Wire `Runtime::claimIntent` to `ActionLedger::claim`, `alreadyClaimed` to its
same-named method, and `connectionLost` to `disconnected()`. In `observeLedger`,
pass each current physical port to `observeHealthy`. The runtime protects claimed
ports while continuing to assess other authorized ports. Only complete, fresh UP
observations without loop/down/guard evidence, spanning two detector periods,
permit erasing the old claim. Erase success is independently read back. Missing
evidence, reconnect, changed revision/detector interval, or a long collection gap
resets this progress. Clearing a claim only permits a later independent incident;
it never sends `undo shutdown`.

## Verified containment and recovery

An SSH success response changes the intention to `verifying_isolation`. Two
distinct post-action rounds must show both ADM and an administrative down cause
before the tracker reports `contained`. This means traffic has been isolated;
it does not claim that the physical loop has been repaired. Loopback detection
can continue listing a shutdown port until it comes up.

`ownedRestorePlan(..., repairConfirmed)` only permits `undo shutdown` for a port
that this same tracker successfully isolated, on the same target, with repair
confirmation and the same current policy gates. It never reopens an externally
administered or switch-protected port. A reboot loses ownership; it cannot reopen
ports based on stale logs. The firmware may leave the restore entry point unused
and preserve containment until repair is confirmed.

After restoration, the port must be UP with no down cause, no loop report, no
active STP guard/discarding, and an enabled detector. At least two valid fresh
rounds must span two detector intervals. Missing evidence resets progress; a
sample gap larger than `maxAgeMs` also restarts verification. A recurring loop,
ambiguous execution, target change, or verification timeout enters `needs_review`;
there is no automatic repeated port flapping. Retain the original incident in
telemetry while containment is active or verification is incomplete.

## Source and validation

Command syntax and state meanings were checked against H3C's matching R63xx
command reference:

- [Ethernet interface commands](https://www.h3c.com/en/d_202108/1434446_294551_0.htm):
  interface brief/down, physical versus administrative causes, interface view,
  `shutdown` and `undo shutdown`.
- [Loop detection commands](https://www.h3c.com/en/Support/Resource_Center/EN/Home/Switches/00-Public/Reference_Guides/Command_References/S5130S-EI%5BHI%5D%5BSI%5D_S5120V2-SI_CR-R63xx/03/202108/1434453_294551_0.htm):
  actual loop reports, action modes, detector interval, and the persistence of a
  shutdown port's loop indication.
- [Spanning tree commands](https://www.h3c.com/en/d_202108/1434452_294551_0.htm):
  ROOT/DESI/ALTE roles, forwarding/discarding, and ROOT/LOOP/BPDU protection.
- [Ethernet link aggregation commands](https://www.h3c.com/en/d_202108/1434450_294551_0.htm):
  verbose member output, local/remote rows, and member annotations.

Run `.venv/bin/python -m unittest test.test_h3c_bdi_policy -v`. The native test
uses address/undefined sanitizers and strict compiler warnings. It exercises
the real parser/policy/state machine and links the actual EmbeddedBDI library,
including an existing captured 52-port
interface response when `test/.runtime/ssh-queries-live.json` is present. It also
drives the production runtime through complete command cycles, durable claims,
ACKs and two-round administrative readback, and exercises reboot, storage failure,
reconnection, wrong-target replies and independent incidents. Synthetic
fault fixtures test policy; they are not evidence of live switch fault injection.
No test in this module connects to or writes to a switch.
