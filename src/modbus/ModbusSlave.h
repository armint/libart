#pragma once

#include <expected>
#include <span>
#include <utility>

#include "streams/BoundedReadable.h"
#include "streams/BufferedReadable.h"
#include "streams/DigestReadable.h"
#include "streams/DigestWritable.h"
#include "streams/Readable.h"
#include "streams/Writable.h"
#include "utils/IBMCRC16Digest.h"

namespace modbus {

class ModbusSlave {
   protected:
   class CRCReadable : public DigestReadable {
      public:
      using Readable::read;
      IBMCRC16Digest digest_;
      explicit CRCReadable(Readable &source)
         : DigestReadable(source, digest_) {}
      bool readCRCAndValidate();
   };

   class CRCWritable : public DigestWritable {
      public:
      IBMCRC16Digest digest_;
      explicit CRCWritable(Writable &sink)
         : DigestWritable(sink, digest_) {}
      std::expected<size_t, Error> writeCRC();
   };

   public:
   enum class Result : uint8_t {
      Ok,
      AddressOutOfRange,
      IllegalValue,
      CouldNotRead,
   };

   using ReadRegistersCallback_t = Result (*)(void *arg, uint16_t address, uint16_t count, Writable &writable);
   using WriteRegistersCallback_t = Result (*)(void *arg, uint16_t address, BoundedReadable &readable);
   using enabler_t = void (*)(void *arg, bool);


   // Helper thunks to adapt member functions to callback signatures
   // Read registers (holding/input): bool (T::*)(uint16_t, uint16_t, Writable [&]) [const/non-const]
   template <typename T, Result (T::*MemFn)(uint16_t, uint16_t, Writable &)>
   static Result member_thunk(void *arg, uint16_t address, uint16_t count, Writable &writable) {
      return (static_cast<T *>(arg)->*MemFn)(address, count, writable);
   }
   template <typename T, Result (T::*MemFn)(uint16_t, uint16_t, Writable &) const>
   static Result member_thunk(void *arg, uint16_t address, uint16_t count, Writable &writable) {
      return (static_cast<const T *>(arg)->*MemFn)(address, count, writable);
   }

   // Write holding registers: bool (T::*)(uint16_t, BoundedReadable [&]) [const/non-const]
   template <typename T, Result (T::*MemFn)(uint16_t, BoundedReadable &)>
   static Result write_member_thunk(void *arg, uint16_t address, BoundedReadable &readable) {
      return (static_cast<T *>(arg)->*MemFn)(address, readable);
   }
   template <typename T, Result (T::*MemFn)(uint16_t, BoundedReadable &) const>
   static Result write_member_thunk(void *arg, uint16_t address, BoundedReadable &readable) {
      return (static_cast<const T *>(arg)->*MemFn)(address, readable);
   }

   protected:
   enum class ReadState : uint8_t { Listening, WaitForIdle, Idle };
   volatile ReadState read_state_ = ReadState::Listening;

   Readable &readable_;
   BufferedReadable *buffered_readable_ = nullptr;
   Writable &writable_;
   uint8_t address_ = 1; // default slave address
   std::span<uint8_t> buffer_;
   utils::Timeout packet_timeout_;

   ReadRegistersCallback_t readHolding_{};
   void *read_holding_arg_ = nullptr;
   ReadRegistersCallback_t readInput_{};
   void *read_input_arg_ = nullptr;
   WriteRegistersCallback_t writeHolding_{};
   void *write_holding_arg_ = nullptr;

   enabler_t txEnabler_;
   void *tx_enabler_arg_ = nullptr;
   enabler_t rxEnabler_;
   void *rx_enabler_arg_ = nullptr;

   void enableTx(bool enable) const {
      if(txEnabler_) {
         txEnabler_(tx_enabler_arg_, enable);
      }
   }
   void enableRx(bool enable) const {
      if(rxEnabler_) {
         rxEnabler_(rx_enabler_arg_, enable);
      }
   }
   // helpers
   std::expected<size_t, Writable::Error> writeException(uint8_t function, uint8_t exceptionCode);
   bool handleReadRegisters(CRCReadable &r, uint8_t function, bool isBroadcast);
   bool handleWriteMultipleRegisters(CRCReadable &r, uint8_t function, bool isBroadcast);
   bool isWaitingForIdle();

   public:
   ModbusSlave(BufferedReadable &readable, Writable &writable, std::span<uint8_t> buffer, enabler_t rxEnabler = {}, enabler_t txEnabler = {});
   ModbusSlave(Readable &readable, Writable &writable, std::span<uint8_t> buffer, enabler_t rxEnabler = {}, enabler_t txEnabler = {});

   void setAddress(uint8_t addr) {
      address_ = addr;
   }

   [[nodiscard]] uint8_t getAddress() const {
      return address_;
   }

   void setInterCharTimeout(uint32_t t) {
      readable_.setTimeout(t);
   }

   void setReadHoldingCallback(ReadRegistersCallback_t cb, void *arg) {
      readHolding_ = cb;
      read_holding_arg_ = arg;
   }
   // Convenience connectors using member thunks — Read Holding
   template <typename T, Result (T::*MemFn)(uint16_t, uint16_t, Writable &)>
   void connectReadHolding(T *object) {
      setReadHoldingCallback(&member_thunk<T, MemFn>, object);
   }
   template <typename T, Result (T::*MemFn)(uint16_t, uint16_t, Writable &) const>
   void connectReadHolding(const T *object) {
      setReadHoldingCallback(&member_thunk<T, MemFn>, const_cast<T *>(object));
   }

   void setReadInputCallback(ReadRegistersCallback_t cb, void *arg) {
      readInput_ = cb;
      read_input_arg_ = arg;
   }
   // Convenience connectors using member thunks — Read Input
   template <typename T, Result (T::*MemFn)(uint16_t, uint16_t, Writable &)>
   void connectReadInput(T *object) {
      setReadInputCallback(&member_thunk<T, MemFn>, object);
   }
   template <typename T, Result (T::*MemFn)(uint16_t, uint16_t, Writable &) const>
   void connectReadInput(const T *object) {
      setReadInputCallback(&member_thunk<T, MemFn>, const_cast<T *>(object));
   }

   void setWriteHoldingCallback(WriteRegistersCallback_t cb, void *arg) {
      writeHolding_ = cb;
      write_holding_arg_ = arg;
   }
   // Convenience connectors using member thunks — Write Holding
   template <typename T, Result (T::*MemFn)(uint16_t, BoundedReadable &)>
   void connectWriteHolding(T *object) {
      setWriteHoldingCallback(&write_member_thunk<T, MemFn>, object);
   }
   template <typename T, Result (T::*MemFn)(uint16_t, BoundedReadable &) const>
   void connectWriteHolding(const T *object) {
      setWriteHoldingCallback(&write_member_thunk<T, MemFn>, const_cast<T *>(object));
   }

   [[nodiscard]] duration_t packet_timeout() const {
      return packet_timeout_;
   }

   void set_packet_timeout(duration_t packet_timeout) {
      packet_timeout_ = utils::Timeout(packet_timeout);
   }


   // Process a single request frame if available. Returns true if a request was processed (regardless of reply), false on timeout/no frame.
   bool poll();

   void idleDetected() {
      read_state_ = ReadState::Idle;
   }

};

} // namespace modbus
