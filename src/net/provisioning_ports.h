// =====================================================================
//  net/provisioning_ports.h — provisioning seams (WiFi, portal, enroll)
// ---------------------------------------------------------------------
//  The provisioning STATE MACHINE is host-testable; the I/O it drives is
//  behind these seams (real impls on-device, mocks in tests).
//
//  WifiControl: SoftAP + sequential STA test (AP+STA concurrency is
//  unreliable on beken — verified — so the test is drop-AP → try-STA →
//  restore-AP on fail, never concurrent).
//
//  PortalServer: serves the config form and reports a submission.
//
//  Enrollment: identity bootstrap (on-device keygen → CSR → operational
//  cert over the bootstrap mTLS channel, Phase 5/7). Large + crypto; the
//  service just asks it to run and gets success/failure.
// =====================================================================
#pragma once

#include "provisioning_types.h"

namespace ss {

class WifiControl {
public:
    virtual ~WifiControl() = default;
    // Bring up the provisioning SoftAP (WPA2, per-device pass — R-20).
    virtual bool startAp(const char* ssid, const char* pass) = 0;
    virtual void stopAp() = 0;
    // Attempt an STA join with timeout. Blocking (runs in the provisioning
    // task). Returns true on association. (Sequential test — AP is down.)
    virtual bool connectSta(const char* ssid, const char* pass, uint32_t timeoutMs) = 0;
    virtual bool isStaConnected() = 0;
    virtual void disconnectSta() = 0;
};

// Reports a captive-portal credential submission to the service.
class PortalSubmissionHandler {
public:
    virtual ~PortalSubmissionHandler() = default;
    virtual void onCredentialsSubmitted(const ProvisioningCredentials& creds) = 0;
};

class PortalServer {
public:
    virtual ~PortalServer() = default;
    virtual void start(PortalSubmissionHandler& handler) = 0;
    virtual void stop() = 0;
    virtual void poll() = 0;                 // service HTTP clients
    // Report progress/result back to the user's browser on next poll.
    virtual void setMessage(const char* msg) = 0;
};

// Identity bootstrap. Returns true if an operational cert was obtained
// and stored. token is the one-time enrollment token (R-19).
class Enrollment {
public:
    virtual ~Enrollment() = default;
    virtual bool enroll(const char* token) = 0;
    virtual bool hasOperationalCert() = 0;   // already enrolled previously?
};

}  // namespace ss
