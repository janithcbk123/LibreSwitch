// =====================================================================
//  test/host/test_profile_resolver.cpp — L1 unit tests (Phase 10)
// ---------------------------------------------------------------------
//  Profile resolution for every variant, raw-byte validation, unknown-id
//  rejection (fault, not silent default), reference pin-map correctness,
//  switch_1g reset-pair-disabled (Q4), and OTA HW-compat matching (R-8).
// =====================================================================
#include "test_framework.h"
#include "../../src/profiles/profile_resolver.h"

using namespace ss;

TEST(resolve_every_known_profile) {
    EQ(ProfileResolver::resolve(ProfileId::Switch1G).profile->channelCount, (ChannelId)1);
    EQ(ProfileResolver::resolve(ProfileId::Switch2G).profile->channelCount, (ChannelId)2);
    EQ(ProfileResolver::resolve(ProfileId::Switch3G).profile->channelCount, (ChannelId)3);
    EQ(ProfileResolver::resolve(ProfileId::Switch4G).profile->channelCount, (ChannelId)4);
}

TEST(resolve_unknown_id_is_fault_not_default) {
    ProfileLookup r = ProfileResolver::resolve(static_cast<ProfileId>(99));
    CHECK(!r.ok());
    CHECK(r.profile == nullptr);          // never guess a pin map
    EQ(r.error, Error::InvalidArgument);
}

TEST(resolve_raw_valid_bytes) {
    for (uint8_t b = 1; b <= 4; ++b) {
        ProfileLookup r = ProfileResolver::resolveRaw(b);
        CHECK(r.ok());
        EQ(r.profile->channelCount, (ChannelId)b);
    }
}

TEST(resolve_raw_invalid_bytes_rejected) {
    for (uint8_t b : {0, 5, 99, 255}) {
        ProfileLookup r = ProfileResolver::resolveRaw(b);
        CHECK(!r.ok());
        EQ(r.error, Error::InvalidArgument);
    }
}

TEST(reference_pin_map_is_correct) {
    const DeviceProfile& p = *ProfileResolver::resolve(ProfileId::Switch4G).profile;
    EQ(p.channels[0].relayPin, (uint8_t)6);
    EQ(p.channels[1].relayPin, (uint8_t)8);
    EQ(p.channels[2].relayPin, (uint8_t)9);
    EQ(p.channels[3].relayPin, (uint8_t)26);
    EQ(p.channels[0].touchPin, (uint8_t)24);
    EQ(p.channels[1].touchPin, (uint8_t)20);
    EQ(p.channels[2].touchPin, (uint8_t)7);
    EQ(p.channels[3].touchPin, (uint8_t)14);
    EQ(p.statusLedPin, (uint8_t)22);
    EQ(p.backlightPin, (uint8_t)23);
}

TEST(vendor_neutral_names) {
    // Names must be capability-based, never vendor strings (Phase 1 AO-1).
    EQ(std::string(ProfileResolver::resolve(ProfileId::Switch4G).profile->name), std::string("switch_4g"));
    EQ(std::string(ProfileResolver::resolve(ProfileId::Switch1G).profile->name), std::string("switch_1g"));
}

TEST(lower_gang_is_capability_subset) {
    // 2-gang shares ch0/ch1 pins with 4-gang (same physical positions).
    const DeviceProfile& g2 = *ProfileResolver::resolve(ProfileId::Switch2G).profile;
    const DeviceProfile& g4 = *ProfileResolver::resolve(ProfileId::Switch4G).profile;
    EQ(g2.channels[0].relayPin, g4.channels[0].relayPin);
    EQ(g2.channels[1].relayPin, g4.channels[1].relayPin);
}

TEST(single_gang_disables_reset_pair) {
    const DeviceProfile& g1 = *ProfileResolver::resolve(ProfileId::Switch1G).profile;
    CHECK(!g1.resetPairValid);            // Q4: switch_1g uses alternate gesture
}

TEST(multi_gang_enables_reset_pair) {
    for (auto id : {ProfileId::Switch2G, ProfileId::Switch3G, ProfileId::Switch4G}) {
        const DeviceProfile& p = *ProfileResolver::resolve(id).profile;
        CHECK(p.resetPairValid);
        EQ(p.resetChannelA, (ChannelId)0);
        EQ(p.resetChannelB, (ChannelId)1);
    }
}

TEST(hw_compat_matches_same_family_and_count) {
    const DeviceProfile& g4 = *ProfileResolver::resolve(ProfileId::Switch4G).profile;
    CHECK(ProfileResolver::hwCompatMatches(g4, "bk7231n.relay_touch", 4));
}

TEST(hw_compat_rejects_wrong_channel_count) {
    const DeviceProfile& g1 = *ProfileResolver::resolve(ProfileId::Switch1G).profile;
    // a 4-gang image must NOT install on a 1-gang device (R-8)
    CHECK(!ProfileResolver::hwCompatMatches(g1, "bk7231n.relay_touch", 4));
}

TEST(hw_compat_rejects_wrong_family) {
    const DeviceProfile& g4 = *ProfileResolver::resolve(ProfileId::Switch4G).profile;
    CHECK(!ProfileResolver::hwCompatMatches(g4, "esp32.relay_touch", 4));
    CHECK(!ProfileResolver::hwCompatMatches(g4, nullptr, 4));   // null-safe
}

int main() {
    printf("ProfileResolver unit tests\n");
    return tf::run_all();
}
