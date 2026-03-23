#pragma once

#include "concurrency/OSThread.h"
#include "variant.h"

#if defined(HELTEC_MESH_NODE_T114)
//this pin seems free on the T114, change it accordingly on your use cases
#define SOS_BUZZ_PIN 33
#endif


#if defined(TBEAM_1WATT)
#define SOS_BUZZ_PIN 41
#endif

#ifdef SOS_BUZZ_PIN

struct MorseStep {
    bool on;
    uint16_t duration;
};

const MorseStep sosPattern[] = {
    {true,100},{false,100},{true,100},{false,100},{true,100},{false,300},
    {true,300},{false,100},{true,300},{false,100},{true,300},{false,300},
    {true,100},{false,100},{true,100},{false,100},{true,100},{false,700}
};

class SOSBuzzModule : public concurrency::OSThread {

public:
    SOSBuzzModule() : OSThread("SOSBuzz") {
        pinMode(SOS_BUZZ_PIN, OUTPUT);
    }

    int32_t runOnce() override;

    void start();
    void stop();

private:
    int index = 0;
    uint32_t nextChange = 0;
    bool active = false;
};

extern SOSBuzzModule* sosBuzzModule;

#endif

