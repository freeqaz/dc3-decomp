// test_pipeline_disk_cache.cpp -- the store behind Dawn's blob cache
// (engine gfx/PipelineDiskCache.h; np-gfx spotperf lane round 3, branch
// np-hitch, 2026-11-15).
//
// Dawn calls Load twice per entry (size, then bytes) and Store once per new
// entry, from its worker threads. What this pins:
//  - a stored blob reads back byte-exact, through both calls;
//  - a blob that does not read back exactly -- truncated, a byte flipped in
//    the value, or a file holding another key -- is a miss: Load returns 0 (so
//    Dawn compiles), counts it rejected, and deletes the file so the compile's
//    store replaces it;
//  - another isolation key (another build, adapter or driver) sees none of
//    the first one's blobs;
//  - the bound holds: a run stops storing at it, and Open evicts the least
//    recently used files down to 3/4 of it;
//  - MILO_PIPELINE_CACHE=0 turns it off, and a consumer that names no
//    directory has none.
// The game-level half (a truncated blob under a real device renders the same
// frame) is in the lane archive (dc3-decomp archive/np-gfx-2026-11-14/lanes/
// spotperf/round3): the gate runs every test with the cache off
// (MILO_TEST_ENV MILO_PIPELINE_CACHE=0), so it never depends on cache state.
//
// Sabotage (same archive): the key-bytes comparison removed from Load -> the
// foreign-key case reads another key's value; the unlink of a rejected blob
// removed -> the truncated file is still there.
//
// No GPU needed.

#include "test_helpers.h"

#include "gfx/PipelineDiskCache.h"

#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

std::string FreshDir(const char *name) {
    static int n = 0;
    const std::string &root = TestScratchDir();
    if (root.empty()) return "";
    std::string d = root + "/" + name + std::to_string(n++);
    mkdir(d.c_str(), 0755);
    return d;
}

std::vector<std::string> Blobs(const std::string &dir) {
    std::vector<std::string> out;
    DIR *d = opendir(dir.c_str());
    if (!d) return out;
    while (struct dirent *e = readdir(d)) {
        std::string n = e->d_name;
        if (n.size() > 5 && n.compare(n.size() - 5, 5, ".blob") == 0) out.push_back(dir + "/" + n);
    }
    closedir(d);
    return out;
}

void Put(PipelineDiskCache &c, const std::string &key, const std::vector<uint8_t> &value) {
    PipelineDiskCache::StoreThunk(key.data(), key.size(), value.data(), value.size(), &c);
}

// Dawn's two-call read: size, then bytes. Empty = miss.
std::vector<uint8_t> Get(PipelineDiskCache &c, const std::string &key) {
    const size_t n = PipelineDiskCache::LoadThunk(key.data(), key.size(), nullptr, 0, &c);
    if (n == 0) return {};
    std::vector<uint8_t> v(n);
    const size_t m = PipelineDiskCache::LoadThunk(key.data(), key.size(), v.data(), v.size(), &c);
    if (m != n) return {};
    return v;
}

std::vector<uint8_t> Value(size_t n, uint8_t seed) {
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; i++) v[i] = (uint8_t)(seed + i * 7);
    return v;
}

} // namespace

TEST(PipelineDiskCacheTest, StoredBlobsReadBackAndDamagedOnesAreMisses) {
    const std::string dir = FreshDir("pdc");
    ASSERT_FALSE(dir.empty());
    PipelineDiskCache c;
    ASSERT_TRUE(c.Open(dir, "isolation A"));
    const std::vector<uint8_t> v1 = Value(5000, 1), v2 = Value(300, 2), v3 = Value(800, 3);
    Put(c, "key one", v1);
    Put(c, "key two", v2);
    Put(c, "key 3rd", v3);
    ASSERT_EQ(Blobs(c.Dir()).size(), 3u);
    EXPECT_EQ(Get(c, "key one"), v1);
    EXPECT_EQ(Get(c, "key two"), v2);
    EXPECT_TRUE(Get(c, "key four").empty()) << "a key never stored";

    // Truncated (a crash mid-write cannot leave one -- stores are renamed into
    // place -- but a full disk or a user's copy can).
    const std::string p1 = c.Dir() + "/" + [&] {
        // the blob for "key one" is the largest file
        std::string best;
        off_t size = -1;
        for (const std::string &b : Blobs(c.Dir())) {
            struct stat st;
            if (stat(b.c_str(), &st) == 0 && st.st_size > size) size = st.st_size, best = b;
        }
        return best.substr(best.rfind('/') + 1);
    }();
    ASSERT_EQ(truncate(p1.c_str(), 2000), 0);
    const uint64_t rejected0 = c.GetStats().rejected;
    EXPECT_TRUE(Get(c, "key one").empty()) << "a truncated blob was read";
    EXPECT_EQ(c.GetStats().rejected, rejected0 + 1);
    EXPECT_NE(access(p1.c_str(), F_OK), 0) << "the rejected blob was left in place";
    Put(c, "key one", v1);  // what Dawn does after the compile
    EXPECT_EQ(Get(c, "key one"), v1) << "the fresh store replaces it";

    // A flipped byte in the value: same size, wrong checksum.
    {
        FILE *f = fopen(p1.c_str(), "r+b");
        ASSERT_NE(f, nullptr);
        fseek(f, -10, SEEK_END);
        const int ch = fgetc(f);
        fseek(f, -10, SEEK_END);
        fputc(ch ^ 0x5a, f);
        fclose(f);
    }
    EXPECT_TRUE(Get(c, "key one").empty()) << "a corrupted blob was read";

    // A file at "key two"'s path holding another key's blob (a hash collision,
    // or a file copied over): rejected on the key bytes, never served.
    std::string p2, p3;  // key two's blob (~330 B) and key 3rd's (~830 B)
    for (const std::string &b : Blobs(c.Dir())) {
        struct stat st;
        stat(b.c_str(), &st);
        if (st.st_size < 600) p2 = b;
        else if (st.st_size < 1000) p3 = b;
    }
    ASSERT_FALSE(p2.empty());
    ASSERT_FALSE(p3.empty());
    ASSERT_EQ(rename(p3.c_str(), p2.c_str()), 0);
    EXPECT_TRUE(Get(c, "key two").empty()) << "served another key's value";
}

TEST(PipelineDiskCacheTest, AnotherIsolationKeySeesNoneOfIt) {
    const std::string dir = FreshDir("pdc");
    ASSERT_FALSE(dir.empty());
    PipelineDiskCache a, b;
    ASSERT_TRUE(a.Open(dir, "build 1|10de:2684|NVIDIA: 615.78.08"));
    ASSERT_TRUE(b.Open(dir, "build 1|10de:2684|NVIDIA: 615.79.00"));
    EXPECT_NE(a.Dir(), b.Dir());
    Put(a, "shader", Value(400, 9));
    EXPECT_EQ(Get(a, "shader"), Value(400, 9));
    EXPECT_TRUE(Get(b, "shader").empty()) << "a different driver read the blob";
}

TEST(PipelineDiskCacheTest, TheBoundHolds) {
    const std::string dir = FreshDir("pdc");
    ASSERT_FALSE(dir.empty());
    const uint64_t cap = 64 * 1024;
    {
        PipelineDiskCache c;
        ASSERT_TRUE(c.Open(dir, "iso", cap));
        for (int i = 0; i < 40; i++) Put(c, "k" + std::to_string(i), Value(4000, (uint8_t)i));
        const PipelineDiskCache::Stats s = c.GetStats();
        EXPECT_GT(s.storeSkipped, 0u) << "40 x 4 KB stored under a 64 KB bound";
        EXPECT_LE(s.bytesWritten, cap);
    }
    // Another directory's worth arrives (a second isolation key) and pushes the
    // total over; the next Open evicts down to 3/4 of the bound.
    {
        PipelineDiskCache c;
        ASSERT_TRUE(c.Open(dir, "iso2", cap * 4));
        for (int i = 0; i < 20; i++) Put(c, "j" + std::to_string(i), Value(4000, (uint8_t)i));
    }
    PipelineDiskCache c;
    ASSERT_TRUE(c.Open(dir, "iso", cap));
    EXPECT_GT(c.GetStats().evicted, 0u);
    uint64_t total = 0;
    DIR *d = opendir(dir.c_str());
    ASSERT_NE(d, nullptr);
    while (struct dirent *e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        for (const std::string &b : Blobs(dir + "/" + e->d_name)) {
            struct stat st;
            if (stat(b.c_str(), &st) == 0) total += (uint64_t)st.st_size;
        }
    }
    closedir(d);
    EXPECT_LE(total, cap / 4 * 3) << "Open left " << total << " bytes";
}

TEST(PipelineDiskCacheTest, OneSwitchTurnsItOff) {
    const char *prev = getenv("MILO_PIPELINE_CACHE");
    const std::string saved = prev ? prev : "";
    setenv("MILO_PIPELINE_CACHE", "0", 1);
    EXPECT_EQ(PipelineDiskCache::ResolveDir(), "");
    setenv("MILO_PIPELINE_CACHE", "off", 1);
    EXPECT_EQ(PipelineDiskCache::ResolveDir(), "");
    setenv("MILO_PIPELINE_CACHE", "/some/where", 1);
    EXPECT_EQ(PipelineDiskCache::ResolveDir(), "/some/where");
    unsetenv("MILO_PIPELINE_CACHE");
    // DC3 names a directory under its cache root (native/src/platform/Cache_Stub.cpp).
    const std::string def = PipelineDiskCache::ResolveDir();
    EXPECT_NE(def.find("dawn-pipelines"), std::string::npos) << def;
    if (prev) setenv("MILO_PIPELINE_CACHE", saved.c_str(), 1);
    // The gate's device runs without it, whatever the box's cache holds.
    if (prev) {
        EXPECT_STREQ(saved.c_str(), "0") << "milo-tests is expected to run with the cache off";
    }
}
