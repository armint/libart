#pragma once
#include "Readable.h"
#include "utils/AsymmetricKey.h"

/**
 * A Readable adapter that feeds data into an AsymmetricKey as it is being read
 * from a source Readable. This is useful for performing asymmetric operations
 * (like verification or decryption) on-the-fly while data is being consumed.
 */
class AsymmetricKeyReadable : public Readable {
   protected:
   Readable &source_;
   AsymmetricKey &key_;

   public:
   AsymmetricKeyReadable(Readable &source, AsymmetricKey &key)
      : source_(source)
      , key_(key) {}

   AsymmetricKeyReadable(AsymmetricKeyReadable&&) = default;
   AsymmetricKeyReadable& operator=(AsymmetricKeyReadable&&) = delete;
   AsymmetricKeyReadable(const AsymmetricKeyReadable&) = delete;
   AsymmetricKeyReadable& operator=(const AsymmetricKeyReadable&) = delete;

   using Readable::read;
   art::expected<char, Error> read() override;
   bool available() override;

   /**
    * Verifies the signature against the data that has been read through this adapter.
    * @param signature The signature to verify.
    * @return true if valid, false otherwise, or an error.
    */
   inline art::expected<bool, Writable::Error> verify(art::span<const uint8_t> signature) {
      return key_.verify(signature);
   }

   /**
    * Decrypts the data that has been read through this adapter.
    * @param plaintext The buffer for the plaintext.
    * @return The number of bytes written, or an error.
    */
   inline art::expected<size_t, Writable::Error> decrypt(art::span<uint8_t> plaintext) {
      return key_.decrypt(plaintext);
   }

   [[nodiscard]] AsymmetricKey &key() const {
      return key_;
   }
};
