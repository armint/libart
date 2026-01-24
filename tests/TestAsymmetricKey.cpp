#include <catch2/catch_test_macros.hpp>
#include <vector>
#include "../src/utils/AsymmetricKey.h"
#include "../src/streams/AsymmetricKeyWritable.h"
#include "../src/streams/AsymmetricKeyReadable.h"

// A mock AsymmetricKey for testing
class MockKey : public AsymmetricKey {
public:
    std::vector<uint8_t> data;
    bool resetCalled = false;

    void reset() override { resetCalled = true; data.clear(); }
    size_t getOutputSize() const override { return 32; }
    
    std::expected<size_t, Error> write(std::span<const uint8_t> d) override {
        data.insert(data.end(), d.begin(), d.end());
        return d.size();
    }
    
    std::expected<void, Error> flush() override { return {}; }

    std::expected<size_t, Error> sign(std::span<uint8_t> signature) override {
        if (signature.size() < 4) return std::unexpected(Overflow);
        signature[0] = 's'; signature[1] = 'i'; signature[2] = 'g'; signature[3] = (uint8_t)data.size();
        return 4;
    }

    std::expected<bool, Error> verify(std::span<const uint8_t> signature) override {
        return signature.size() == 4 && signature[0] == 's' && signature[3] == (uint8_t)data.size();
    }

    std::expected<size_t, Error> encrypt(std::span<uint8_t> ciphertext) override { return 0; }
    std::expected<size_t, Error> decrypt(std::span<uint8_t> plaintext) override { return 0; }
};

// A simple Readable for testing
class StringReadable : public Readable {
    std::string s;
    size_t pos = 0;
public:
    StringReadable(std::string s) : s(s) {}
    std::expected<char, Error> read() override {
        if (pos >= s.size()) return std::unexpected(EndOfStream);
        return s[pos++];
    }
    bool available() override { return pos < s.size(); }
};

// A simple Writable for testing
class VectorWritable : public Writable {
public:
    std::vector<uint8_t> output;
    std::expected<size_t, Error> write(std::span<const uint8_t> data) override {
        output.insert(output.end(), data.begin(), data.end());
        return data.size();
    }
    std::expected<void, Error> flush() override { return {}; }
};

TEST_CASE("AsymmetricKey interface and adapters", "[crypto]") {
    MockKey key;
    
    SECTION("Writable adapter") {
        VectorWritable sink;
        AsymmetricKeyWritable akw(sink, key);
        
        const uint8_t msg[] = "hello";
        akw.write(std::span(msg, 5)).value();
        REQUIRE(sink.output.size() == 5);
        REQUIRE(key.data.size() == 5);
        
        uint8_t sig[4];
        auto res = akw.sign(sig);
        REQUIRE(res.has_value());
        REQUIRE(res.value() == 4);
        REQUIRE(sig[3] == 5);
    }
    
    SECTION("Readable adapter") {
        StringReadable source("world");
        AsymmetricKeyReadable akr(source, key);
        
        uint8_t buf[10];
        auto res = akr.read(std::span(buf, 5));
        REQUIRE(res.has_value());
        REQUIRE(res.value() == 5);
        REQUIRE(key.data.size() == 5);
        
        uint8_t sig[] = {'s', 'i', 'g', 5};
        auto vres = akr.verify(sig);
        REQUIRE(vres.has_value());
        REQUIRE(vres.value() == true);
    }
}
