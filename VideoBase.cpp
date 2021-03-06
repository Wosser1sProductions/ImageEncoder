#include "VideoBase.hpp"
#include "utils.hpp"
#include "Logger.hpp"
#include "Huffman.hpp"

dc::VideoBase::VideoBase(const std::string &source_file, const uint16_t &width, const uint16_t &height)
    : ImageBase(source_file, width, height)
    , frame_buffer_size(width * height)
    , frame_garbage_size(width * height / 2)
{
    try {
        this->raw = util::readBinaryFile(source_file);
    } catch (Exceptions::FileReadException const& e) {
        util::Logger::WriteLn(e.getMessage());
        exit(-1);
    }

    this->reader = util::allocVar<util::BitStreamReader>(this->raw->data(), this->raw->size());
}

/**
 *  @brief  Default dtor
 */
dc::VideoBase::~VideoBase(void) {
    // Empty
}

dc::VideoProcessor::VideoProcessor(const std::string &source_file, const std::string &dest_file,
                                   const uint16_t &width, const uint16_t &height,
                                   const bool &use_rle, MatrixReader<> &quant_m,
                                   const uint16_t &gop, const uint16_t &merange)
    : VideoBase(source_file, width, height)
    , use_rle(use_rle), quant_m(quant_m)
    , gop(std::max(uint16_t(1), gop)), merange(merange), motioncomp(true)
    , dest_file(dest_file)
    , frames(util::allocVar<std::vector<dc::Frame*>>())
{
    // Encode raw
    this->frame_count = this->reader->get_size()
                      / (this->frame_buffer_size + this->frame_garbage_size);

    dc::Frame::MVEC_BIT_SIZE = util::bits_needed(int16_t(this->merange));
}

dc::VideoProcessor::VideoProcessor(const std::string &source_file, const std::string &dest_file,
                                   const bool &motioncomp)
    : VideoBase(source_file, 0, 0)
    , motioncomp(motioncomp)
    , dest_file(dest_file)
    , frames(util::allocVar<std::vector<dc::Frame*>>())
{
    // Assume input is encoded video and settings should be determined from the bytestream

    // Perform Huffman decompress (if used, first bit is '1' else '0')
    algo::Huffman<> hm;
    util::BitStreamReader *hm_output = hm.decode(*this->reader);
    util::Logger::WriteLn("", false);

    // Replace reader with result from decompressed Huffman stream
    if (hm_output != nullptr) {
        util::deallocVar(this->reader);
        this->reader = hm_output;
    }

    // Read Matrix
    this->quant_m = dc::MatrixReader<>::fromBitstream(*this->reader);

    // Read other settings in same order as they were presumably written to the encoded stream
    this->use_rle = this->reader->get(dc::ImageProcessor::RLE_BITS);
    this->width   = uint16_t(this->reader->get(dc::ImageProcessor::DIM_BITS));
    this->height  = uint16_t(this->reader->get(dc::ImageProcessor::DIM_BITS));

    this->frame_count        = this->reader->get(dc::ImageProcessor::DIM_BITS);
    this->frame_buffer_size  = this->width * this->height;  // 1 part Y data
    this->frame_garbage_size = this->frame_buffer_size / 2; // 0.5 parts UV data

    // Decoded frame will be decoded Y data
    //  + this->frame_garbage_size times 0x80 as 'garbage data' for UV components

    this->gop     = uint16_t(this->reader->get(dc::ImageProcessor::DIM_BITS));
    this->merange = uint16_t(this->reader->get(dc::ImageProcessor::DIM_BITS));

    dc::Frame::MVEC_BIT_SIZE = util::bits_needed(int16_t(this->merange));

    // TODO decode
}

/**
 *  @brief  Default dtor
 */
dc::VideoProcessor::~VideoProcessor(void) {
    util::deallocVector(this->frames);
    util::deallocVar(this->writer);
}

bool dc::VideoProcessor::process(uint8_t * const source_frame_buffer) {
    util::Logger::WriteLn("[VideoProcessor] Creating frames...");

    // this->frame_buffer_size;   ///< Total values inside 1 Frame (Y)
    // this->frame_garbage_size;  ///< Total values after frame data (UV)
    // this->frame_count          ///< Total amount of frames

    const size_t total_frame_size = this->frame_buffer_size + this->frame_garbage_size;

    dc::Frame *reference_frame = nullptr;

    for (size_t f_x = 0; f_x < this->frame_count; f_x++) {
        uint8_t * const frame_starts = source_frame_buffer
                                     + f_x * total_frame_size;

        // Every this.gop frame is an I frame
        dc::Frame *current_frame = util::allocVar<dc::Frame>(frame_starts , reference_frame,
                                                             this->width  , this->height,
                                                             this->use_rle, this->quant_m,
                                                             this->is_i_frame(f_x));

        this->frames->push_back(current_frame);
        reference_frame = current_frame;
    }

    return true;
}

void dc::VideoProcessor::saveResult(bool encoded) const {
    const size_t total_length = this->writer->get_last_byte_position();  // Total final write length in bytes

    #if 1
        // This will only write whole bytes; if pos % 8 != 0, last byte is skipped
        // Edit: Padding is now added after settings, so only whole bytes are in buffer.
        util::writeBinaryFile(this->dest_file,
                              this->writer->get_buffer(),
                              total_length);
    #else
        // Will take last byte into account
        std::ofstream file(this->dest_file, std::ofstream::binary);
        util::write(file, *this->writer);
    #endif

    util::Logger::WriteLn(std::string_format("[VideoProcessor] Original file size: %8d bytes", this->raw->size()));
    util::Logger::WriteLn(std::string_format("[VideoProcessor]       %scoded size: %8d bytes  => Ratio: %.2f%%",
                                             (encoded ? "En" : "De"),
                                             total_length,
                                             (float(total_length) / this->raw->size() * 100)));
    util::Logger::WriteLn("[VideoProcessor] Saved file at: " + this->dest_file);
}
