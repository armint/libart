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
   std::expected<size_t, Error> write(std::span<const uint8_t> data) override;
   std::expected<void, Error> flush() override;

   /**
    * Signs the data that has been written through this adapter.
    * @param signature The buffer for the signature.
    * @return The number of bytes written, or an error.
    */
   inline std::expected<size_t, Error> sign(std::span<uint8_t> signature) {
      return key_.sign(signature);
   }

   /**
    * Encrypts the data that has been written through this adapter.
    * @param ciphertext The buffer for the ciphertext.
    * @return The number of bytes written, or an error.
    */
   inline std::expected<size_t, Error> encrypt(std::span<uint8_t> ciphertext) {
      return key_.encrypt(ciphertext);
   }

   [[nodiscard]] AsymmetricKey &key() const {
      return key_;
   }
};
