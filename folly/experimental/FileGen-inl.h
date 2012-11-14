/*
 * Copyright 2012 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef FOLLY_FILEGEN_H_
#error This file may only be included from folly/experimental/FileGen.h
#endif

#include <system_error>

#include "folly/experimental/StringGen.h"

namespace folly {
namespace gen {
namespace detail {

class FileReader : public GenImpl<ByteRange, FileReader> {
 public:
  FileReader(File file, std::unique_ptr<IOBuf> buffer)
    : file_(std::move(file)),
      buffer_(std::move(buffer)) {
    buffer_->clear();
  }

  template <class Body>
  bool apply(Body&& body) const {
    for (;;) {
      ssize_t n;
      do {
        n = ::read(file_.fd(), buffer_->writableTail(), buffer_->capacity());
      } while (n == -1 && errno == EINTR);
      if (n == -1) {
        throw std::system_error(errno, std::system_category(), "read failed");
      }
      if (n == 0) {
        return true;
      }
      if (!body(ByteRange(buffer_->tail(), n))) {
        return false;
      }
    }
  }
 private:
  File file_;
  std::unique_ptr<IOBuf> buffer_;
};

class FileWriter : public Operator<FileWriter> {
 public:
  FileWriter(File file, std::unique_ptr<IOBuf> buffer)
    : file_(std::move(file)),
      buffer_(std::move(buffer)) {
    if (buffer_) {
      buffer_->clear();
    }
  }

  template <class Source, class Value>
  void compose(const GenImpl<Value, Source>& source) const {
    auto fn = [&](ByteRange v) {
      if (!this->buffer_ || v.size() >= this->buffer_->capacity()) {
        this->flushBuffer();
        this->write(v);
      } else {
        if (v.size() > this->buffer_->tailroom()) {
          this->flushBuffer();
        }
        memcpy(this->buffer_->writableTail(), v.data(), v.size());
        this->buffer_->append(v.size());
      }
    };

    // Iterate
    source.foreach(std::move(fn));

    flushBuffer();
    file_.close();
  }

 private:
  void write(ByteRange v) const {
    ssize_t n;
    while (!v.empty()) {
      do {
        n = ::write(file_.fd(), v.data(), v.size());
      } while (n == -1 && errno == EINTR);
      if (n == -1) {
        throw std::system_error(errno, std::system_category(),
                                "write() failed");
      }
      v.advance(n);
    }
  }

  void flushBuffer() const {
    if (buffer_ && buffer_->length() != 0) {
      write(ByteRange(buffer_->data(), buffer_->length()));
      buffer_->clear();
    }
  }

  mutable File file_;
  std::unique_ptr<IOBuf> buffer_;
};

}  // namespace detail

auto byLine(File file, char delim='\n') ->
decltype(fromFile(std::move(file)) | eachAs<StringPiece>() | resplit(delim)) {
  return fromFile(std::move(file)) | eachAs<StringPiece>() | resplit(delim);
}

}  // namespace gen
}  // namespace folly
