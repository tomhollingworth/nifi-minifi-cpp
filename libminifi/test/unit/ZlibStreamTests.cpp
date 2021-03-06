/**
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <utility>
#include <string>
#include <random>
#include <algorithm>
#include "../TestBase.h"
#include "io/ZlibStream.h"
#include "utils/StringUtils.h"

namespace io = org::apache::nifi::minifi::io;

TEST_CASE("gzip compression and decompression", "[basic]") {
  /* Compression*/
  io::BufferStream compressBuffer;
  io::ZlibCompressStream compressStream(gsl::make_not_null(&compressBuffer));

  std::string original;
  SECTION("Empty") {
  }
  SECTION("Simple content in one write") {
    REQUIRE(strlen("foobar") == compressStream.write(reinterpret_cast<uint8_t*>(const_cast<char*>("foobar")), strlen("foobar")));
    original += "foobar";
  }
  SECTION("Simple content in two writes") {
    REQUIRE(strlen("foo") == compressStream.write(reinterpret_cast<uint8_t*>(const_cast<char*>("foo")), strlen("foo")));
    original += "foo";
    REQUIRE(strlen("bar") == compressStream.write(reinterpret_cast<uint8_t*>(const_cast<char*>("bar")), strlen("bar")));
    original += "bar";
  }
  SECTION("Large data") {
    std::mt19937 gen(std::random_device { }());
    std::uniform_int_distribution<> dist(0, 255);
    std::vector<uint8_t> buf(1024U);
    for (size_t i = 0U; i < 1024U; i++) {
      std::generate(buf.begin(), buf.end(), [&](){return dist(gen);});
      original += std::string(reinterpret_cast<const char*>(buf.data()), buf.size());
      REQUIRE(-1 != compressStream.write(buf.data(), buf.size()));
    }
  }

  compressStream.close();

  REQUIRE(0U < compressBuffer.size());

  if (compressBuffer.size() < 64U) {
    std::cerr << utils::StringUtils::to_hex(compressBuffer.getBuffer(), compressBuffer.size()) << std::endl;
  }

  /* Decompression */
  io::BufferStream decompressBuffer;
  io::ZlibDecompressStream decompressStream(gsl::make_not_null(&decompressBuffer));

  decompressStream.write(const_cast<uint8_t*>(compressBuffer.getBuffer()), compressBuffer.size());

  REQUIRE(decompressStream.isFinished());
  REQUIRE(original == std::string(reinterpret_cast<const char*>(decompressBuffer.getBuffer()), decompressBuffer.size()));
}

TEST_CASE("gzip compression and decompression pipeline", "[basic]") {
  io::BufferStream output;
  io::ZlibDecompressStream decompressStream(gsl::make_not_null(&output));
  io::ZlibCompressStream compressStream(gsl::make_not_null(&decompressStream));

  std::string original;
  SECTION("Empty") {
  }
  SECTION("Simple content in one write") {
    REQUIRE(strlen("foobar") == compressStream.write(reinterpret_cast<uint8_t*>(const_cast<char*>("foobar")), strlen("foobar")));
    original += "foobar";
  }
  SECTION("Simple content in two writes") {
    REQUIRE(strlen("foo") == compressStream.write(reinterpret_cast<uint8_t*>(const_cast<char*>("foo")), strlen("foo")));
    original += "foo";
    REQUIRE(strlen("bar") == compressStream.write(reinterpret_cast<uint8_t*>(const_cast<char*>("bar")), strlen("bar")));
    original += "bar";
  }
  SECTION("Large data") {
    std::mt19937 gen(std::random_device { }());
    std::uniform_int_distribution<> dist(0, 255);
    std::vector<uint8_t> buf(1024U);
    for (size_t i = 0U; i < 1024U; i++) {
      std::generate(buf.begin(), buf.end(), [&](){return dist(gen);});
      original += std::string(reinterpret_cast<const char*>(buf.data()), buf.size());
      REQUIRE(buf.size() == compressStream.write(buf.data(), buf.size()));
    }
  }

  compressStream.close();

  REQUIRE(decompressStream.isFinished());
  REQUIRE(original == std::string(reinterpret_cast<const char*>(output.getBuffer()), output.size()));
}
