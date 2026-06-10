// json-c integration tests (roadmap N.1).
//
// Before this lane, src/system/net/json-c/*.c was NOT compiled into the native
// build, so every json_object_* / json_tokener_parse call resolved to a weak
// return-0 stub in engine_stubs_generated.cpp. Net effect: all online JSON
// (RockCentral / leaderboards / MOTD / store responses) silently parsed to an
// empty object. These tests drive the game's own JSON entry point
// (JsonConverter, used by DingoJob::ParseResponse and friends) over a small
// document and assert that REAL values come back — which only happens when the
// real json-c implementation is linked.

#include "test_helpers.h"
#include "net/JsonUtils.h"
#include "utl/Str.h"

#include <gtest/gtest.h>

// A compact response shaped like the RockCentral / Dingo payloads the game
// parses: a top-level object with an int "result", a string "version", a
// boolean, and a nested object reached by name.
static const char *kSampleJson =
    "{"
    "  \"result\": 42,"
    "  \"version\": \"3.1\","
    "  \"enabled\": true,"
    "  \"response\": { \"score\": 1337, \"name\": \"freeq\" }"
    "}";

// Top-level scalar extraction through the game's JsonConverter::GetByName path.
TEST(JsonParse, ParsesTopLevelScalars) {
    JsonConverter conv;
    JsonObject *root = conv.LoadFromString(String(kSampleJson));
    ASSERT_NE(root, nullptr) << "LoadFromString returned null — json-c not linked?";
    EXPECT_EQ(root->GetType(), JsonObject::kType_Object);

    JsonObject *result = conv.GetByName(root, "result");
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->GetType(), JsonObject::kType_Int);
    EXPECT_EQ(result->Int(), 42);

    JsonObject *version = conv.GetByName(root, "version");
    ASSERT_NE(version, nullptr);
    EXPECT_EQ(version->GetType(), JsonObject::kType_String);
    EXPECT_STREQ(version->Str(), "3.1");

    JsonObject *enabled = conv.GetByName(root, "enabled");
    ASSERT_NE(enabled, nullptr);
    EXPECT_EQ(enabled->GetType(), JsonObject::kType_Boolean);
    EXPECT_TRUE(enabled->Bool());
}

// Nested object reached by name, then a scalar out of it — the GetByName chain
// DingoJob::ParseResponse walks for "result" -> "response" -> "version".
TEST(JsonParse, ParsesNestedObject) {
    JsonConverter conv;
    JsonObject *root = conv.LoadFromString(String(kSampleJson));
    ASSERT_NE(root, nullptr);

    JsonObject *response = conv.GetByName(root, "response");
    ASSERT_NE(response, nullptr);
    EXPECT_EQ(response->GetType(), JsonObject::kType_Object);

    JsonObject *score = conv.GetByName(response, "score");
    ASSERT_NE(score, nullptr);
    EXPECT_EQ(score->Int(), 1337);

    JsonObject *name = conv.GetByName(response, "name");
    ASSERT_NE(name, nullptr);
    EXPECT_STREQ(name->Str(), "freeq");
}

// A missing key must come back null (not a stubbed empty object), and malformed
// input must fail to parse rather than silently succeed.
TEST(JsonParse, MissingKeyAndMalformed) {
    JsonConverter conv;
    JsonObject *root = conv.LoadFromString(String(kSampleJson));
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(conv.GetByName(root, "does_not_exist"), nullptr);

    JsonConverter conv2;
    JsonObject *bad = conv2.LoadFromString(String("{ not valid json "));
    EXPECT_EQ(bad, nullptr);
}
