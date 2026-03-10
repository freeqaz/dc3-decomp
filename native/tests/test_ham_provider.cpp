#include "test_helpers.h"

#include "flow/PropertyEventProvider.h"
#include "obj/Dir.h"

class HamProviderTest : public EngineTestFixture {};

TEST_F(HamProviderTest, NativeBootCreatesTypedHamProvider) {
    ASSERT_NE(TheHamProvider, nullptr);

    PropertyEventProvider *namedProvider =
        ObjectDir::Main()->Find<PropertyEventProvider>("hamprovider", false);
    EXPECT_EQ(namedProvider, TheHamProvider);

    DataArray *types = SystemConfig("objects", "PropertyEventProvider", "types");
    ASSERT_NE(types, nullptr);

    if (types->FindArray("HamProvider", false)) {
        static Symbol hamProviderType("HamProvider");
        static Symbol isInPartyMode("is_in_party_mode");
        static Symbol isInInfinitePartyMode("is_in_infinite_party_mode");

        EXPECT_EQ(TheHamProvider->Type(), hamProviderType);
        EXPECT_NE(TheHamProvider->Property(isInPartyMode, false), nullptr);
        EXPECT_NE(TheHamProvider->Property(isInInfinitePartyMode, false), nullptr);
    }
}
