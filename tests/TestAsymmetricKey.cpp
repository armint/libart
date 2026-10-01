#include <optional>
#include "../src/streams/AsymmetricKeyReadable.h"
#include "../src/streams/AsymmetricKeyWritable.h"
#include "../src/utils/AsymmetricKey.h"
#include "streams/StringReadable.h"
#include <doctest/doctest.h>
#include <etl/vector.h>

// A mock AsymmetricKey for testing
class MockKey : public AsymmetricKey {
public:
    etl::vector<uint8_t, 128> data;
    bool resetCalled = false;

    void reset() override { resetCalled = true; data.clear(); }
    size_t getOutputSize() const override { return 32; }
    
    art::expected<size_t, Error> write(art::span<const uint8_t> d) override {
        if (data.size() + d.size() > data.max_size()) return art::unexpected(Overflow);
        data.insert(data.end(), d.begin(), d.end());
        return d.size();
    }
    
    art::expected<void, Error> flush() override { return {}; }

    art::expected<size_t, Error> sign(art::span<uint8_t> signature) override {
        if (signature.size() < 4) return art::unexpected(Overflow);
        signature[0] = 's'; signature[1] = 'i'; signature[2] = 'g'; signature[3] = (uint8_t)data.size();
        return 4;
    }

    art::expected<bool, Error> verify(art::span<const uint8_t> signature) override {
        return signature.size() == 4 && signature[0] == 's' && signature[3] == (uint8_t)data.size();
    }

    art::expected<size_t, Error> encrypt(art::span<uint8_t> ciphertext) override { return 0; }
    art::expected<size_t, Error> decrypt(art::span<uint8_t> plaintext) override { return 0; }
};

// A simple Writable for testing
class VectorWritable : public Writable {
public:
    etl::vector<uint8_t, 128> output;
    art::expected<size_t, Error> write(art::span<const uint8_t> data) override {
        if (output.size() + data.size() > output.max_size()) return art::unexpected(Overflow);
        output.insert(output.end(), data.begin(), data.end());
        return data.size();
    }
    art::expected<void, Error> flush() override { return {}; }
};

TEST_CASE("AsymmetricKey interface and adapters [crypto]") {
    MockKey key;
    
    SUBCASE("Writable adapter") {
        VectorWritable sink;
        AsymmetricKeyWritable akw(sink, key);
        
        const uint8_t msg[] = "hello";
        akw.write(art::span(msg, 5)).value();
        REQUIRE(sink.output.size() == 5);
        REQUIRE(key.data.size() == 5);
        
        uint8_t sig[4];
        auto res = akw.sign(sig);
        REQUIRE(res.has_value());
        REQUIRE(res.value() == 4);
        REQUIRE(sig[3] == 5);
    }
    
    SUBCASE("Readable adapter") {
        StringReadable source("world");
        AsymmetricKeyReadable akr(source, key);
        
        uint8_t buf[10];
        auto res = akr.read(art::span(buf, 5));
        REQUIRE(res.has_value());
        REQUIRE(res.value() == 5);
        REQUIRE(key.data.size() == 5);
        
        uint8_t sig[] = {'s', 'i', 'g', 5};
        auto vres = akr.verify(sig);
        REQUIRE(vres.has_value());
        REQUIRE(vres.value() == true);
    }
}
