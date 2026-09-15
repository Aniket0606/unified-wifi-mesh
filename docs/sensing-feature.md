# Unified Wi-Fi Mesh Sensing

## Scope

The Sensing feature implements the provisional Multi-AP Sensing operations used by the controller and agent:

- Sensing capabilities and Agent STA interface reporting
- Layer 3 Sensing data paths
- QoS Null, Trigger-Based, and Non-Trigger-Based exchanges
- Sensing Measurement Query (SensingMQ)
- Trigger Probe requests and tunneled Probe Responses
- Local session sockets for measurement delivery

## Provisional Codepoints

The Sensing CMDU and TLV values are provisional because the source contribution marks the Wi-Fi Alliance assignments as `WFA-TBD`. The values are centralized in `inc/em_base.h`; protocol code must reference the symbolic enum names rather than repeating numeric literals. Replacing the fenced allocation block and rebuilding is the intended update path when final assignments are available.

The current provisional CMDU range starts after the existing `em_msg_type_avail_spectrum_inquiry` value. Sensing TLVs use the corresponding provisional range defined in the same header. These values are not an interoperability commitment.

## Lower-Layer Boundary

802.11bf radio operations are isolated behind `em_sensing_ll_t` in `inc/em_sensing_ll.h`. The interface covers:

- Measurement request, response, query, and termination operations
- QoS Null exchange scheduling
- Agent STA creation, destruction, and probe transmission
- Lower-layer events, including measurement responses and Probe Responses

`em_sensing_ll_stub_t` provides deterministic behavior for unit tests and development builds. A production integration supplies the platform-specific implementation through `em_sensing_t::set_lower_layer()`. The stub is not a substitute for a hardware 802.11bf implementation.

## Validation

The controller test target is built with:

```sh
make -C build/ctrl clean
make -C build/ctrl test
```

The tests cover exchange lifecycle, SensingMQ timing and comeback behavior, Non-TB direction handling, Trigger Probe broadcast/targeted/failure behavior, and tunneled Probe Response framing. macOS runs skip tests that require Linux network interfaces.
