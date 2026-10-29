#include "ncbind.hpp"
#include "kirikiroid2Compat.h"
#include "TextStream.h"

#include <boost/locale/encoding_utf.hpp>
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <type_traits>
#include <vector>

#define NCB_MODULE_NAME TJS_W("kirikiroid2.dll")

namespace {

void invalidParameter() { TJSThrowFrom_tjs_error(TJS_E_INVALIDPARAM, nullptr); }

void setOctet(tTJSVariant *result, const void *bytes, size_t size) {
    if(size > std::numeric_limits<tjs_uint>::max()) invalidParameter();
    if(!result) return;
    auto *value = TJSAllocVariantOctet(static_cast<const tjs_uint8 *>(bytes),
                                       static_cast<tjs_uint>(size));
    *result = value;
    if(value) value->Release();
}

class OwnedBufferAccessor final : public tTJSDispatch {
    unsigned char *bytes_;
    tTJSVariant owner_;
    size_t length_;
    size_t position_ = 0;
    bool valid_ = true;

    bool contains(tjs_int index) const {
        const auto absolute = static_cast<tjs_int64>(position_) + index;
        return absolute >= 0 && static_cast<tjs_uint64>(absolute) < length_;
    }
public:
    OwnedBufferAccessor(unsigned char *data, tjs_uint length,
                          tTJSVariant owner) :
        bytes_(data), owner_(std::move(owner)), length_(length) {}

    tjs_error PropGet(tjs_uint32, const tjs_char *name, tjs_uint32 *,
                       tTJSVariant *result, iTJSDispatch2 *) override {
        if(!valid_) return TJS_E_INVALIDOBJECT;
        if(!name) return TJS_E_NOTIMPL;
        if(result) {
            if(!TJS_strcmp(name, TJS_W("ptr")))
                *result = static_cast<tjs_int64>(position_);
            else if(!TJS_strcmp(name, TJS_W("count")))
                *result = static_cast<tjs_int64>(length_);
            else result->Clear();
        }
        return TJS_S_OK;
    }
    tjs_error PropSet(tjs_uint32, const tjs_char *name, tjs_uint32 *,
                       const tTJSVariant *value, iTJSDispatch2 *) override {
        if(!valid_) return TJS_E_INVALIDOBJECT;
        if(!name || TJS_strcmp(name, TJS_W("ptr"))) return TJS_E_NOTIMPL;
        const auto position = value->AsInteger();
        if(position < 0 || static_cast<tjs_uint64>(position) > length_)
            return TJS_E_INVALIDPARAM;
        position_ = static_cast<size_t>(position);
        return TJS_S_OK;
    }
    tjs_error PropGetByNum(tjs_uint32, tjs_int index, tTJSVariant *result,
                            iTJSDispatch2 *) override {
        if(!valid_) return TJS_E_INVALIDOBJECT;
        if(!contains(index)) return TJS_E_MEMBERNOTFOUND;
        if(result) *result = static_cast<tjs_int>(bytes_[position_ + index]);
        return TJS_S_OK;
    }
    tjs_error PropSetByNum(tjs_uint32, tjs_int index, const tTJSVariant *value,
                            iTJSDispatch2 *) override {
        if(!valid_) return TJS_E_INVALIDOBJECT;
        if(!contains(index)) return TJS_E_MEMBERNOTFOUND;
        bytes_[position_ + index] = static_cast<tjs_uint8>(value->AsInteger());
        return TJS_S_OK;
    }
    tjs_error GetCount(tjs_int *result, const tjs_char *name, tjs_uint32 *,
                        iTJSDispatch2 *) override {
        if(!valid_) return TJS_E_INVALIDOBJECT;
        if(name) return TJS_E_MEMBERNOTFOUND;
        if(result) *result = static_cast<tjs_int>(length_);
        return TJS_S_OK;
    }
    tjs_error IsValid(tjs_uint32, const tjs_char *, tjs_uint32 *,
                       iTJSDispatch2 *) override {
        return valid_ ? TJS_S_TRUE : TJS_S_FALSE;
    }
    tjs_error Invalidate(tjs_uint32, const tjs_char *, tjs_uint32 *,
                          iTJSDispatch2 *) override {
        valid_ = false;
        bytes_ = nullptr;
        owner_.Clear();
        return TJS_S_OK;
    }
    tjs_error OperationByNum(tjs_uint32 flags, tjs_int index,
                              tTJSVariant *result, const tTJSVariant *value,
                              iTJSDispatch2 *context) override {
        const auto operation = flags & TJS_OP_MASK;
        if(operation < TJS_OP_MIN || operation > TJS_OP_MAX ||
           (!value && operation != TJS_OP_INC && operation != TJS_OP_DEC))
            return TJS_E_INVALIDPARAM;
        tTJSVariant current;
        const auto status = PropGetByNum(0, index, &current, context);
        if(TJS_FAILED(status)) return status;
        TJSDoVariantOperation(operation, current, value);
        const auto written = PropSetByNum(0, index, &current, context);
        if(TJS_SUCCEEDED(written) && result) *result = current;
        return written;
    }

    tjs_error FuncCall(tjs_uint32, const tjs_char *name, tjs_uint32 *,
                        tTJSVariant *, tjs_int count, tTJSVariant **args,
                        iTJSDispatch2 *) override {
        if(!valid_) return TJS_E_INVALIDOBJECT;
        if(!name || TJS_strcmp(name, TJS_W("xor"))) return TJS_E_NOTIMPL;
        if(count < 3) return TJS_E_BADPARAMCOUNT;
        const auto offset = args[0]->AsInteger();
        const auto size = args[1]->AsInteger();
        if(offset < 0 || static_cast<tjs_uint64>(offset) > length_ - position_ ||
           size < 0 || static_cast<tjs_uint64>(size) > length_ - position_ - offset)
            return TJS_E_INVALIDPARAM;
        const auto mask = static_cast<tjs_uint8>(args[2]->AsInteger());
        for(tjs_int64 i = 0; i < size; ++i) bytes_[position_ + offset + i] ^= mask;
        return TJS_S_OK;
    }
};

tjs_error createBuffer(tTJSVariant *result, tjs_int count,
                        tTJSVariant **args, iTJSDispatch2 *) {
    if(count < 1) return TJS_E_BADPARAMCOUNT;
    tjs_uint8 *bytes = nullptr;
    tjs_int64 size;
    tTJSVariant owner;
    if(args[0]->Type() == tvtOctet) {
        owner = *args[0];
        auto *octet = owner.AsOctetNoAddRef();
        bytes = octet ? const_cast<tjs_uint8 *>(octet->GetData()) : nullptr;
        size = octet ? octet->GetLength() : 0;
    } else if(args[0]->Type() == tvtInteger) {
        if(count < 2) return TJS_E_BADPARAMCOUNT;
        bytes = reinterpret_cast<tjs_uint8 *>(
            static_cast<uintptr_t>(args[0]->AsInteger()));
        size = args[1]->AsInteger();
    } else return TJS_E_INVALIDPARAM;
    if(size < 0 || static_cast<tjs_uint64>(size) >
           std::numeric_limits<tjs_uint>::max() || (!bytes && size))
        return TJS_E_INVALIDPARAM;
    if(result) {
        auto *accessor = new OwnedBufferAccessor(
            bytes, static_cast<tjs_uint>(size), owner);
        *result = tTJSVariant(accessor, accessor);
        accessor->Release();
    }
    return TJS_S_OK;
}

tjs_error octetFromAddress(tTJSVariant *result, tjs_int count,
                            tTJSVariant **args, iTJSDispatch2 *) {
    if(count < 2) return TJS_E_BADPARAMCOUNT;
    const auto address = static_cast<uintptr_t>(args[0]->AsInteger());
    const auto size = args[1]->AsInteger();
    if(size < 0 || static_cast<tjs_uint64>(size) >
           std::numeric_limits<tjs_uint>::max() || (!address && size) ||
       static_cast<tjs_uint64>(size) > std::numeric_limits<uintptr_t>::max() - address)
        return TJS_E_INVALIDPARAM;
    setOctet(result, reinterpret_cast<const void *>(address), size);
    return TJS_S_OK;
}

struct FileCloser {
    void operator()(FILE *file) const { if(file) std::fclose(file); }
};
using FileHandle = std::unique_ptr<FILE, FileCloser>;
std::map<FILE *, FileHandle> &files() {
    static std::map<FILE *, FileHandle> handles;
    return handles;
}

FILE *getFile(const tTJSVariant &value) {
    auto *file = reinterpret_cast<FILE *>(
        static_cast<uintptr_t>(value.AsInteger()));
    if(!file || !files().count(file)) invalidParameter();
    return file;
}

tjs_error fileOpen(tTJSVariant *result, tjs_int count,
                    tTJSVariant **args, iTJSDispatch2 *) {
    if(count < 2) return TJS_E_BADPARAMCOUNT;
    const ttstr path(*args[0]);
    const ttstr mode(*args[1]);
#ifdef _WIN32
    const auto nativePath = std::filesystem::u8path(path.AsStdString());
    const auto nativeMode = std::filesystem::u8path(mode.AsStdString());
    auto *file = _wfopen(nativePath.c_str(), nativeMode.c_str());
#else
    auto *file = std::fopen(path.AsStdString().c_str(), mode.AsStdString().c_str());
#endif
    if(file) files().emplace(file, FileHandle(file));
    if(result) *result = static_cast<tjs_int64>(reinterpret_cast<uintptr_t>(file));
    return TJS_S_OK;
}

tjs_error fileClose(tTJSVariant *result, tjs_int count,
                     tTJSVariant **args, iTJSDispatch2 *) {
    if(count < 1) return TJS_E_BADPARAMCOUNT;
    auto *file = getFile(*args[0]);
    auto handle = files().extract(file);
    handle.mapped().release();
    const auto status = std::fclose(file);
    if(result) *result = status;
    return TJS_S_OK;
}

tjs_error fileSeek(tTJSVariant *result, tjs_int count,
                    tTJSVariant **args, iTJSDispatch2 *) {
    if(count < 3) return TJS_E_BADPARAMCOUNT;
    const auto offset = args[1]->AsInteger();
    const auto whence = static_cast<int>(args[2]->AsInteger());
    if(whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END)
        return TJS_E_INVALIDPARAM;
#ifdef _WIN32
    const auto status = _fseeki64(getFile(*args[0]), offset, whence);
#else
    const auto status = fseeko(getFile(*args[0]), offset, whence);
#endif
    if(result) *result = status;
    return TJS_S_OK;
}

tjs_int64 tell(FILE *file) {
#ifdef _WIN32
    return _ftelli64(file);
#else
    return ftello(file);
#endif
}

tjs_error fileTell(tTJSVariant *result, tjs_int count,
                    tTJSVariant **args, iTJSDispatch2 *) {
    if(count < 1) return TJS_E_BADPARAMCOUNT;
    const auto position = tell(getFile(*args[0]));
    if(result) *result = position;
    return TJS_S_OK;
}

tjs_error fileRead(tTJSVariant *result, tjs_int count,
                    tTJSVariant **args, iTJSDispatch2 *) {
    if(count < 2) return TJS_E_BADPARAMCOUNT;
    auto *file = getFile(*args[0]);
    auto size = args[1]->AsInteger();
    if(size < 0) {
        const auto position = tell(file);
        if(position < 0 || std::fseek(file, 0, SEEK_END)) return TJS_E_FAIL;
        size = tell(file) - position;
#ifdef _WIN32
        const auto seekStatus = _fseeki64(file, position, SEEK_SET);
#else
        const auto seekStatus = fseeko(file, position, SEEK_SET);
#endif
        if(seekStatus) return TJS_E_FAIL;
    }
    if(size < 0 || static_cast<tjs_uint64>(size) >
           std::numeric_limits<tjs_uint>::max()) return TJS_E_INVALIDPARAM;
    std::vector<tjs_uint8> bytes(static_cast<size_t>(size));
    const auto read = size ? std::fread(bytes.data(), 1, bytes.size(), file) : 0;
    if(result) {
        if(read) setOctet(result, bytes.data(), read);
        else result->Clear();
    }
    return TJS_S_OK;
}

tjs_error fileWrite(tTJSVariant *result, tjs_int count,
                     tTJSVariant **args, iTJSDispatch2 *) {
    if(count < 2) return TJS_E_BADPARAMCOUNT;
    auto *file = getFile(*args[0]);
    auto *octet = args[1]->AsOctetNoAddRef();
    const auto written = octet && octet->GetLength()
        ? std::fwrite(octet->GetData(), 1, octet->GetLength(), file) : 0;
    if(result) *result = static_cast<tjs_int64>(written);
    return TJS_S_OK;
}

void closeFiles() { files().clear(); }

} // namespace

class TextReadStream {
    iTJSTextReadStream *stream_ = nullptr;
public:
    ~TextReadStream() { close(); }
    static tjs_error factory(TextReadStream **result, tjs_int count,
                              tTJSVariant **args, iTJSDispatch2 *) {
        if(count < 1) return TJS_E_BADPARAMCOUNT;
        auto stream = std::make_unique<TextReadStream>();
        stream->stream_ = TVPCreateTextStreamForRead(ttstr(*args[0]),
            count > 1 ? ttstr(*args[1]) : ttstr());
        *result = stream.release();
        return TJS_S_OK;
    }
    void close() {
        if(stream_) stream_->Destruct();
        stream_ = nullptr;
    }
    static tjs_error read(tTJSVariant *result, tjs_int count,
                           tTJSVariant **args, TextReadStream *self) {
        if(!self || !self->stream_) return TJS_E_INVALIDOBJECT;
        const auto size = count > 0 ? args[0]->AsInteger() : 0;
        if(size < 0 || static_cast<tjs_uint64>(size) >
               std::numeric_limits<tjs_uint>::max()) return TJS_E_INVALIDPARAM;
        ttstr text;
        self->stream_->Read(text, static_cast<tjs_uint>(size));
        if(result) *result = text;
        return TJS_S_OK;
    }
};

class TextWriteStream {
    iTJSTextWriteStream *stream_ = nullptr;
public:
    ~TextWriteStream() { close(); }
    static tjs_error factory(TextWriteStream **result, tjs_int count,
                              tTJSVariant **args, iTJSDispatch2 *) {
        if(count < 1) return TJS_E_BADPARAMCOUNT;
        auto stream = std::make_unique<TextWriteStream>();
        stream->stream_ = TVPCreateTextStreamForWrite(ttstr(*args[0]),
            count > 1 ? ttstr(*args[1]) : ttstr());
        *result = stream.release();
        return TJS_S_OK;
    }
    void close() {
        if(stream_) stream_->Destruct();
        stream_ = nullptr;
    }
    void write(ttstr text) {
        if(!stream_) TJSThrowFrom_tjs_error(TJS_E_INVALIDOBJECT, nullptr);
        stream_->Write(text);
    }
};

// The reference BinaryReader is a memory reader/builder, rather than a file
// stream. Keep its cursor separate from appended data and use authored little
// endian scalar encodings even when the host's byte order differs.
class BinaryReader {
    std::vector<tjs_uint8> bytes_;
    size_t position_ = 0;

    void require(size_t length) const {
        if(position_ > bytes_.size() || length > bytes_.size() - position_)
            invalidParameter();
    }
    void appendBits(tjs_uint64 bits, size_t size) {
        if(bytes_.size() > std::numeric_limits<tjs_uint>::max() - size)
            invalidParameter();
        for(size_t i = 0; i < size; ++i) bytes_.push_back((bits >> (i * 8)) & 0xff);
    }
    tjs_uint64 readBits(size_t size, bool peek) {
        require(size);
        tjs_uint64 value = 0;
        for(size_t i = 0; i < size; ++i)
            value |= static_cast<tjs_uint64>(bytes_[position_ + i]) << (i * 8);
        if(!peek) position_ += size;
        return value;
    }
public:
    static tjs_error factory(BinaryReader **result, tjs_int count,
                              tTJSVariant **args, iTJSDispatch2 *) {
        auto reader = std::make_unique<BinaryReader>();
        if(count && args[0]->Type() == tvtOctet) reader->addBytes(*args[0]);
        *result = reader.release();
        return TJS_S_OK;
    }
    void clear() { bytes_.clear(); position_ = 0; }
    tjs_int64 length() const { return bytes_.size(); }
    tjs_int64 tell() const { return position_; }
    void seek(tjs_int64 position) {
        if(position < 0 || static_cast<tjs_uint64>(position) > bytes_.size())
            invalidParameter();
        position_ = static_cast<size_t>(position);
    }
    void skip(tjs_int64 offset) {
        if(offset < -static_cast<tjs_int64>(position_) ||
           offset > static_cast<tjs_int64>(bytes_.size() - position_))
            invalidParameter();
        seek(static_cast<tjs_int64>(position_) + offset);
    }
    static tjs_error getBytes(tTJSVariant *result, tjs_int, tTJSVariant **,
                               BinaryReader *self) {
        if(!self) return TJS_E_INVALIDOBJECT;
        setOctet(result, self->bytes_.data(), self->bytes_.size());
        return TJS_S_OK;
    }
    void addBytes(tTJSVariant value) {
        auto *octet = value.AsOctetNoAddRef();
        if(!octet || !octet->GetLength()) return;
        if(octet->GetLength() > std::numeric_limits<tjs_uint>::max() - bytes_.size())
            invalidParameter();
        bytes_.insert(bytes_.end(), octet->GetData(),
                       octet->GetData() + octet->GetLength());
    }
    void pad(tjs_int64 size) {
        if(size <= 0) return;
        if(static_cast<tjs_uint64>(size) >
           std::numeric_limits<tjs_uint>::max() - bytes_.size()) invalidParameter();
        bytes_.resize(bytes_.size() + static_cast<size_t>(size), 0);
    }
    static tjs_error addString(tTJSVariant *, tjs_int count,
                                tTJSVariant **args, BinaryReader *self) {
        if(count < 1) return TJS_E_BADPARAMCOUNT;
        if(!self) return TJS_E_INVALIDOBJECT;
        const ttstr text(*args[0]);
        const auto bytes = boost::locale::conv::utf_to_utf<char>(
            text.c_str(), text.c_str() + text.GetLen());
        const auto requested = count > 1 ? args[1]->AsInteger() : 0;
        if(requested < 0 || static_cast<tjs_uint64>(requested) >
            std::numeric_limits<tjs_uint>::max()) return TJS_E_INVALIDPARAM;
        const size_t appended = std::max(bytes.size(), static_cast<size_t>(requested));
        if(appended > std::numeric_limits<tjs_uint>::max() - self->bytes_.size())
            return TJS_E_INVALIDPARAM;
        self->bytes_.insert(self->bytes_.end(), bytes.begin(), bytes.end());
        self->pad(appended - bytes.size());
        return TJS_S_OK;
    }
    template<size_t size, bool real = false>
    static tjs_error addScalar(tTJSVariant *, tjs_int count,
                                tTJSVariant **args, BinaryReader *self) {
        if(count < 1) return TJS_E_BADPARAMCOUNT;
        if(!self) return TJS_E_INVALIDOBJECT;
        tjs_uint64 bits;
        if constexpr(real) {
            using Number = std::conditional_t<size == 4, float, double>;
            using Bits = std::conditional_t<size == 4, tjs_uint32, tjs_uint64>;
            const Number value = static_cast<Number>(args[0]->AsReal());
            Bits raw;
            std::memcpy(&raw, &value, size);
            bits = raw;
        } else bits = static_cast<tjs_uint64>(args[0]->AsInteger());
        self->appendBits(bits, size);
        return TJS_S_OK;
    }
    static tjs_error add(tTJSVariant *result, tjs_int count,
                          tTJSVariant **args, BinaryReader *self) {
        if(count < 1) return TJS_E_BADPARAMCOUNT;
        if(!self) return TJS_E_INVALIDOBJECT;
        switch(args[0]->Type()) {
            case tvtString: return addString(result, 1, args, self);
            case tvtOctet: self->addBytes(*args[0]); return TJS_S_OK;
            case tvtInteger: return addScalar<8>(result, count, args, self);
            case tvtReal: return addScalar<8, true>(result, count, args, self);
            default: return TJS_S_OK;
        }
    }
    template<size_t size, bool isSigned, bool peek = false, bool real = false>
    static tjs_error readScalar(tTJSVariant *result, tjs_int, tTJSVariant **,
                                 BinaryReader *self) {
        if(!self) return TJS_E_INVALIDOBJECT;
        const auto bits = self->readBits(size, peek);
        if(result) {
            if constexpr(real) {
                using Number = std::conditional_t<size == 4, float, double>;
                using Bits = std::conditional_t<size == 4, tjs_uint32, tjs_uint64>;
                Number value;
                const Bits raw = static_cast<Bits>(bits);
                std::memcpy(&value, &raw, size);
                *result = static_cast<tjs_real>(value);
            } else {
                tjs_uint64 extended = bits;
                if constexpr(isSigned && size < 8) {
                    if(bits & (tjs_uint64{1} << (size * 8 - 1)))
                        extended |= ~tjs_uint64{0} << (size * 8);
                }
                *result = static_cast<tjs_int64>(extended);
            }
        }
        return TJS_S_OK;
    }
    static tjs_error readBytes(tTJSVariant *result, tjs_int count,
                                tTJSVariant **args, BinaryReader *self) {
        if(!self) return TJS_E_INVALIDOBJECT;
        const auto requested = count ? args[0]->AsInteger() : -1;
        const size_t remaining = self->bytes_.size() - self->position_;
        const auto size = requested < 0 ? remaining : std::min<tjs_uint64>(requested, remaining);
        setOctet(result, size ? self->bytes_.data() + self->position_ : nullptr, size);
        self->position_ += size;
        return TJS_S_OK;
    }
    static tjs_error readString(tTJSVariant *result, tjs_int count,
                                 tTJSVariant **args, BinaryReader *self) {
        if(count < 1) return TJS_E_BADPARAMCOUNT;
        if(!self) return TJS_E_INVALIDOBJECT;
        const auto size = args[0]->AsInteger();
        if(size < 0) return TJS_E_INVALIDPARAM;
        self->require(static_cast<size_t>(size));
        if(result) {
            if(!size) *result = TJS_W("");
            else {
                const auto *begin = reinterpret_cast<const char *>(
                    self->bytes_.data() + self->position_);
                const auto *end = std::find(begin, begin + size, '\0');
                *result = ttstr(boost::locale::conv::utf_to_utf<tjs_char>(begin, end));
            }
        }
        self->position_ += size;
        return TJS_S_OK;
    }
};

NCB_REGISTER_CLASS(TextReadStream) {
    Factory(&Class::factory);
    NCB_METHOD(close);
    RawCallback(TJS_W("read"), &Class::read, 0);
}

NCB_REGISTER_CLASS(TextWriteStream) {
    Factory(&Class::factory);
    NCB_METHOD(close);
    NCB_METHOD(write);
}

NCB_REGISTER_CLASS(BinaryReader) {
    Factory(&Class::factory);
    NCB_METHOD(clear);
    Property(TJS_W("length"), &Class::length, (int)0);
    Property(TJS_W("position"), &Class::tell, &Class::seek);
    NCB_METHOD(tell);
    NCB_METHOD(seek);
    NCB_METHOD(skip);
    NCB_METHOD(pad);
    NCB_METHOD(addBytes);
    RawCallback(TJS_W("getBytes"), &Class::getBytes, 0);
    RawCallback(TJS_W("addString"), &Class::addString, 0);
    RawCallback(TJS_W("add"), &Class::add, 0);
    RawCallback(TJS_W("readString"), &Class::readString, 0);
    RawCallback(TJS_W("readBytes"), &Class::readBytes, 0);
    RawCallback(TJS_W("read"), &Class::readBytes, 0);
#define KRKR_INTEGER_METHODS(suffix, size, sign) \
    RawCallback(TJS_W("read" #suffix), &Class::readScalar<size, sign>, 0); \
    RawCallback(TJS_W("peek" #suffix), &Class::readScalar<size, sign, true>, 0)
    KRKR_INTEGER_METHODS(Int8, 1, true);
    KRKR_INTEGER_METHODS(UInt8, 1, false);
    KRKR_INTEGER_METHODS(Int16, 2, true);
    KRKR_INTEGER_METHODS(UInt16, 2, false);
    KRKR_INTEGER_METHODS(Int32, 4, true);
    KRKR_INTEGER_METHODS(UInt32, 4, false);
    KRKR_INTEGER_METHODS(Int64, 8, true);
#undef KRKR_INTEGER_METHODS
    RawCallback(TJS_W("readFloat"), &Class::readScalar<4, false, false, true>, 0);
    RawCallback(TJS_W("readDouble"), &Class::readScalar<8, false, false, true>, 0);
    RawCallback(TJS_W("peekFloat"), &Class::readScalar<4, false, true, true>, 0);
    RawCallback(TJS_W("peekDouble"), &Class::readScalar<8, false, true, true>, 0);
    RawCallback(TJS_W("addInt8"), &Class::addScalar<1>, 0);
    RawCallback(TJS_W("addInt16"), &Class::addScalar<2>, 0);
    RawCallback(TJS_W("addInt32"), &Class::addScalar<4>, 0);
    RawCallback(TJS_W("addInt64"), &Class::addScalar<8>, 0);
    RawCallback(TJS_W("addFloat"), &Class::addScalar<4, true>, 0);
    RawCallback(TJS_W("addDouble"), &Class::addScalar<8, true>, 0);
}

void TVPRegisterKirikiroidStreamHelpers(iTJSDispatch2 *global) {
    TVPRegisterKirikiroidMethod(global, TJS_W("_oct_from_addr"), octetFromAddress);
    TVPRegisterKirikiroidMethod(global, TJS_W("_createBufferAccessor"), createBuffer);
    const auto ioNamespace = TVPGetKirikiroidNamespace(global, TJS_W("IO"));
    auto *io = ioNamespace.AsObjectNoAddRef();
    TVPRegisterKirikiroidMethod(io, TJS_W("fopen"), fileOpen);
    TVPRegisterKirikiroidMethod(io, TJS_W("fclose"), fileClose);
    TVPRegisterKirikiroidMethod(io, TJS_W("fseek"), fileSeek);
    TVPRegisterKirikiroidMethod(io, TJS_W("ftell"), fileTell);
    TVPRegisterKirikiroidMethod(io, TJS_W("fread"), fileRead);
    TVPRegisterKirikiroidMethod(io, TJS_W("fwrite"), fileWrite);
}

NCB_PRE_UNREGIST_CALLBACK(closeFiles);
