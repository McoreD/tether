#include <gtest/gtest.h>
#include <tether/bluetooth/bmessage.hpp>
#include <tether/bluetooth/config.hpp>
#include <tether/bluetooth/contacts.hpp>
#include <tether/bluetooth/messages.hpp>

using namespace tether::bluetooth;

namespace {

    constexpr const char* SECOND = "AA:BB:CC:DD:EE:FF";

    Message make(const std::string& handle, const std::string& thread, int64_t ts) {
        Message m;
        m.handle = handle;
        m.thread_key = thread;
        m.peer_address = unscoped(thread);
        m.body = "body " + handle;
        m.timestamp = ts;
        return m;
    }

} // namespace

TEST(DeviceScope, PrimaryKeysStayBare) {
    EXPECT_EQ(scope_to_device("", "tel:+15551234567"), "tel:+15551234567");
    EXPECT_EQ(device_of("tel:+15551234567"), "");
    EXPECT_EQ(unscoped("tel:+15551234567"), "tel:+15551234567");
}

TEST(DeviceScope, RoundTrips) {
    const std::string key = scope_to_device(SECOND, "tel:+15551234567");
    EXPECT_EQ(key, "dev:AA:BB:CC:DD:EE:FF/tel:+15551234567");
    EXPECT_EQ(device_of(key), SECOND);
    EXPECT_EQ(unscoped(key), "tel:+15551234567");
    EXPECT_EQ(scope_to_device(SECOND, ""), "") << "an unusable key must stay unusable";
}

TEST(DeviceScope, RecipientIgnoresTheScope) {
    Recipient recipient;
    std::string err;
    ASSERT_TRUE(recipient_from_thread_key(scope_to_device(SECOND, "tel:+15551234567"), recipient, err)) << err;
    EXPECT_EQ(recipient.kind, RecipientKind::Tel);
    EXPECT_EQ(recipient.address, "+15551234567");
}

TEST(DeviceScope, BucketKeepsPhonesApart) {
    EXPECT_EQ(thread_bucket(scope_to_device(SECOND, "tel:+15555550123")),
              thread_bucket(scope_to_device(SECOND, "tel:5555550123")))
        << "two spellings of one number on one phone are one conversation";
    EXPECT_NE(thread_bucket("tel:+15555550123"), thread_bucket(scope_to_device(SECOND, "tel:+15555550123")))
        << "one person texting two phones is two conversations";
}

TEST(DeviceScope, ContactNamesResolveOnEveryPhone) {
    ContactStore store;
    VCard card;
    card.name = "Jane Doe";
    card.tels = {"+15551234567"};
    store.set({card});
    EXPECT_EQ(store.name_for(scope_to_device(SECOND, "tel:+15551234567")), "Jane Doe");
}

TEST(MultiDeviceStore, SameHandleOnTwoPhonesIsTwoMessages) {
    MessageStore store;
    EXPECT_TRUE(store.add(make("1", "tel:+15551234567", 100)));
    EXPECT_TRUE(store.add(make(scope_to_device(SECOND, "1"), scope_to_device(SECOND, "tel:+15551234567"), 200)));
    EXPECT_EQ(store.size(), 2u);

    auto threads = store.threads();
    ASSERT_EQ(threads.size(), 2u);
    EXPECT_EQ(device_of(threads[0].key), SECOND);
    EXPECT_EQ(to_json(threads[0])["device"], SECOND);
    EXPECT_EQ(device_of(threads[1].key), "");

    EXPECT_EQ(store.messages("tel:+15551234567").size(), 1u);
    EXPECT_EQ(store.messages(scope_to_device(SECOND, "tel:+15551234567")).size(), 1u);
}

TEST(MultiDeviceStore, PrefersThePlusSpellingWithinAScope) {
    MessageStore store;
    const std::string bare = scope_to_device(SECOND, "tel:5555550123");
    const std::string plus = scope_to_device(SECOND, "tel:+15555550123");
    store.add(make(scope_to_device(SECOND, "a"), bare, 100));
    store.add(make(scope_to_device(SECOND, "b"), plus, 200));
    auto threads = store.threads();
    ASSERT_EQ(threads.size(), 1u);
    EXPECT_EQ(threads[0].key, plus);
}

TEST(ExtraDevices, AddSkipsThePrimaryAndDuplicates) {
    Config config;
    config.device_address = "11:22:33:44:55:66";
    EXPECT_FALSE(add_extra_device(config, "11:22:33:44:55:66"));
    EXPECT_TRUE(add_extra_device(config, "aa:bb:cc:dd:ee:ff"));
    EXPECT_FALSE(add_extra_device(config, SECOND)) << "addresses compare case-insensitively";
    ASSERT_EQ(config.extra_devices.size(), 1u);
    EXPECT_EQ(config.extra_devices[0], SECOND);
    EXPECT_TRUE(remove_extra_device(config, "aa:bb:cc:dd:ee:ff"));
    EXPECT_FALSE(remove_extra_device(config, SECOND));
}

TEST(ExtraDevices, SurviveSerialization) {
    Config config;
    config.device_address = "11:22:33:44:55:66";
    add_extra_device(config, SECOND);
    const Config back = deserialize_config(serialize_config(config));
    EXPECT_EQ(back.extra_devices, config.extra_devices);
    EXPECT_EQ(supervised_extra_addresses(back), std::vector<std::string>{SECOND});
}

TEST(ExtraDevices, NoneWhileBluetoothIsOff) {
    Config config;
    add_extra_device(config, SECOND);
    config.enabled = false;
    EXPECT_TRUE(supervised_extra_addresses(config).empty());
}

TEST(ExtraDevices, ThePrimaryIsNeverSupervisedTwice) {
    const Config config = deserialize_config(
        R"({"device_address": "11:22:33:44:55:66", "extra_devices": ["11:22:33:44:55:66", "AA:BB:CC:DD:EE:FF"]})");
    EXPECT_EQ(supervised_extra_addresses(config), std::vector<std::string>{SECOND});
}
