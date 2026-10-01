#pragma once
#include "streams/Writable.h"
#include "utils/compat/expected.h"
#include "utils/compat/span.h"

/**
 * Base interface for asymmetric cryptographic operations.
 * Extends Writable to support streaming of data (e.g., for signing, verification, encryption, or decryption).
 */
class AsymmetricKey : public Writable {
   public:
   virtual ~AsymmetricKey() = default;

   /**
    * Resets the internal state (e.g., the hash context) to prepare for a new operation.
    * This should be called before starting a new streaming operation.
    */
   virtual void reset() = 0;

   /**
    * Returns the size of the output buffer required for the final operation
    * (e.g., signature size or RSA ciphertext size).
    */
   virtual size_t getOutputSize() const = 0;

   /**
    * Signs the data that has been written to the key.
    * @param signature The buffer where the signature will be stored.
    * @return The number of bytes written to the signature buffer, or an error.
    */
   virtual art::expected<size_t, Error> sign(art::span<uint8_t> signature) = 0;

   /**
    * Verifies the signature against the data that has been written to the key.
    * @param signature The signature to verify.
    * @return true if the signature is valid, false otherwise, or an error.
    */
   virtual art::expected<bool, Error> verify(art::span<const uint8_t> signature) = 0;

   /**
    * Encrypts the data (plaintext) that has been written to the key.
    * @param ciphertext The buffer where the ciphertext will be stored.
    * @return The number of bytes written to the ciphertext buffer, or an error.
    */
   virtual art::expected<size_t, Error> encrypt(art::span<uint8_t> ciphertext) = 0;

   /**
    * Decrypts the data (ciphertext) that has been written to the key.
    * @param plaintext The buffer where the plaintext will be stored.
    * @return The number of bytes written to the plaintext buffer, or an error.
    */
   virtual art::expected<size_t, Error> decrypt(art::span<uint8_t> plaintext) = 0;

   using Writable::write;
};

/**
 * Interface for asymmetric private keys.
 */
class PrivateKey : virtual public AsymmetricKey {
};

/**
 * Interface for asymmetric public keys.
 */
class PublicKey : virtual public AsymmetricKey {
};
