/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <folly/io/IOBuf.h>
#include <list>
#include <proxygen/lib/http/codec/compress/HPACKConstants.h>
#include <proxygen/lib/http/codec/compress/QPACKContext.h>
#include <proxygen/lib/http/codec/compress/HPACKEncodeBuffer.h>
#include <proxygen/lib/http/codec/compress/HPACKEncoderBase.h>
#include <vector>
#include <set>

namespace proxygen {

class HPACKDecodeBuffer;

class QPACKEncoder : public HPACKEncoderBase, public QPACKContext {

 public:
  static const uint32_t kMaxHeaderTableSize = (1u << 16);

  explicit QPACKEncoder(bool huffman,
                        uint32_t tableSize=HPACK::kTableSize);

  /**
   * Encode the given headers.
   */

  using Buf = std::unique_ptr<folly::IOBuf>;
  struct EncodeResult {
    EncodeResult(Buf c, Buf s)
        : control(std::move(c)), stream(std::move(s)) {}
    Buf control;
    Buf stream;
  };

  // Returns a pair of buffers.  One for the control stream and one for the
  // request stream
  EncodeResult encode(
    const std::vector<HPACKHeader>& headers,
    uint32_t headroom,
    uint64_t streamId,
    uint32_t maxEncoderStreamBytes=std::numeric_limits<uint32_t>::max());

  HPACK::DecodeError decodeDecoderStream(
      std::unique_ptr<folly::IOBuf> buf);

  HPACK::DecodeError onInsertCountIncrement(uint32_t inserts);

  HPACK::DecodeError onHeaderAck(uint64_t streamId, bool all);

  bool setHeaderTableSize(uint32_t tableSize, bool updateMax=true) {
    if (updateMax) {
      // The peer's max is used whenn encoding RequiredInsertCouunt
      if (maxTableSize_ != 0 && maxTableSize_ != tableSize) {
        LOG(ERROR) << "Cannot change non-zero max header table size, "
          "maxTableSize_=" << maxTableSize_ << " tableSize=" << tableSize;
        return false;
      }
      maxTableSize_ = tableSize;
    }
    if (tableSize > kMaxHeaderTableSize) {
      VLOG(2) << "Limiting table size from " << tableSize << " to " <<
        kMaxHeaderTableSize;
      tableSize = kMaxHeaderTableSize;
    }
    HPACKEncoderBase::setHeaderTableSize(table_, tableSize);
    return true;
  }

  uint32_t getMaxHeaderTableSize() const {
    return maxTableSize_;
  }

  void setMaxVulnerable(uint32_t maxVulnerable) {
    maxVulnerable_ = maxVulnerable;
  }

  // This API is only for tests, and doesn't work correctly if the table is
  // already populated.
  void setMinFreeForTesting(uint32_t minFree) {
    table_.setMinFreeForTesting(minFree);
  }

 private:
  bool allowVulnerable() const {
    return numVulnerable_ < maxVulnerable_;
  }

  bool shouldIndex(const HPACKHeader& header) const;

  void encodeControl(const HPACKHeader& header);

  std::pair<bool, uint32_t> maybeDuplicate(uint32_t relativeIndex);

  QPACKEncoder::EncodeResult
  encodeQ(const std::vector<HPACKHeader>& headers, uint64_t streamId);

  std::tuple<bool, uint32_t, uint32_t> getNameIndexQ(
    const HPACKHeaderName& headerName);

  void encodeStreamLiteralQ(
    const HPACKHeader& header, bool isStaticName, uint32_t nameIndex,
    uint32_t absoluteNameIndex, uint32_t baseIndex,
    uint32_t* requiredInsertCount);

  void encodeHeaderQ(const HPACKHeader& header, uint32_t baseIndex,
                     uint32_t* requiredInsertCount);

  void encodeInsertQ(const HPACKHeader& header,
                     bool isStaticName,
                     uint32_t nameIndex);

  void encodeLiteralQ(const HPACKHeader& header,
                      bool isStaticName,
                      bool postBase,
                      uint32_t nameIndex,
                      const HPACK::Instruction& idxInstr);

  uint32_t encodeLiteralQHelper(HPACKEncodeBuffer& buffer,
                                const HPACKHeader& header,
                                bool isStaticName,
                                uint32_t nameIndex,
                                uint8_t staticFlag,
                                const HPACK::Instruction& idxInstr,
                                const HPACK::Instruction& litInstr);

  void trackReference(uint32_t index, uint32_t* requiredInsertCount);

  void encodeDuplicate(uint32_t index);

  HPACK::DecodeError decodeHeaderAck(HPACKDecodeBuffer& dbuf,
                                     uint8_t prefixLength,
                                     bool all);

  // Returns true if the most recently encoded value (duplicate, insert)
  // fit in the encoder stream's flow control window.  The encoder will only
  // make references to dynamic table entries that fit.  This prevents a nasty
  // deadlock.
  bool lastEntryAvailable() const {
    return maxEncoderStreamBytes_ >= 0;
  }

  HPACKEncodeBuffer controlBuffer_;
  using BlockReferences = std::set<uint32_t>;
  struct OutstandingBlock {
    BlockReferences references;
    bool vulnerable{false};
  };
  // Map streamID -> list of table index references for each outstanding block;
  std::unordered_map<uint64_t, std::list<OutstandingBlock>> outstanding_;
  OutstandingBlock* curOutstanding_{nullptr};
  uint32_t maxDepends_{0};
  uint32_t maxVulnerable_{HPACK::kDefaultBlocking};
  uint32_t numVulnerable_{0};
  uint32_t maxTableSize_{0};
  int64_t maxEncoderStreamBytes_{0};
  folly::IOBufQueue decoderIngress_{folly::IOBufQueue::cacheChainLength()};
};

}
