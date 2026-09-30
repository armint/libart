#pragma once
#include "Writable.h"
#include "utils/AsymmetricKey.h"

/**
 * A Writable adapter that feeds data into an AsymmetricKey and optionally
 * pipes it to another Writable (sink). This is useful for performing asymmetric
 * operations on-the-fly while data is being written to a stream.
 */
class AsymmetricKeyWritable : public Writable {
   protected:
   AsymmetricKey &key_;
   Writable &sink_;

   public:
   AsymmetricKeyWritable(Writable &sink, AsymmetricKey &key)
      : key_(key)
      , sink_(sink) {}

   AsymmetricKeyWritable(AsymmetricKeyWritable&&) = default;
   AsymmetricKeyWritable& operator=(AsymmetricKeyWritable&&) = delete;
   AsymmetricKeyWritable(const AsymmetricKeyWritable&) = delete;
   AsymmetricKeyWritable& operator=(const AsymmetricKeyWritable&) = delete;

   using Writable::write;
   art::expected<size_t, Error> write(art::span<const uint8_t> data) override;
   art::expected<void, Error> flush() override;

   /**
    * Signs the data that has been written through this adapter.
    * @param signature The buffer for the signature.
    * @return The number of bytes written, or an error.
    */
   inline art::expected<size_t, Error> sign(art::span<uint8_t> signature) {
      return key_.sign(signature);
   }

   /**
    * Encrypts the data that has been written through this adapter.
    * @param ciphertext The buffer for the ciphertext.
    * @return The number of bytes written, or an error.
    */
   inline art::expected<size_t, Error> encrypt(art::span<uint8_t> ciphertext) {
      return key_.encrypt(ciphertext);
   }

   [[nodiscard]] AsymmetricKey &key() const {
      return key_;
   }
};
