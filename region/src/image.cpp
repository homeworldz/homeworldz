#include "homeworldz/image.h"

#include <algorithm>
#include <cstring>

#if defined(HOMEWORLDZ_OPENJPEG)
#include <openjpeg.h>
#endif

namespace homeworldz::image {

#if defined(HOMEWORLDZ_OPENJPEG)
namespace {

// OpenJPEG has no public in-memory stream, so we drive one with our own read /
// write / skip / seek callbacks over a plain byte buffer.

struct ReadCtx {
    const std::uint8_t* data;
    OPJ_SIZE_T size;
    OPJ_SIZE_T pos;
};

OPJ_SIZE_T mem_read(void* buffer, OPJ_SIZE_T nb, void* user) {
    auto* c = static_cast<ReadCtx*>(user);
    if (c->pos >= c->size) return static_cast<OPJ_SIZE_T>(-1);  // EOF
    OPJ_SIZE_T remaining = c->size - c->pos;
    OPJ_SIZE_T n = nb < remaining ? nb : remaining;
    std::memcpy(buffer, c->data + c->pos, n);
    c->pos += n;
    return n;
}

OPJ_OFF_T mem_skip(OPJ_OFF_T nb, void* user) {
    auto* c = static_cast<ReadCtx*>(user);
    if (nb < 0) return -1;
    OPJ_SIZE_T remaining = c->size - c->pos;
    OPJ_SIZE_T n = static_cast<OPJ_SIZE_T>(nb) < remaining ? static_cast<OPJ_SIZE_T>(nb)
                                                          : remaining;
    c->pos += n;
    return static_cast<OPJ_OFF_T>(n);
}

OPJ_BOOL mem_seek(OPJ_OFF_T pos, void* user) {
    auto* c = static_cast<ReadCtx*>(user);
    if (pos < 0 || static_cast<OPJ_SIZE_T>(pos) > c->size) return OPJ_FALSE;
    c->pos = static_cast<OPJ_SIZE_T>(pos);
    return OPJ_TRUE;
}

struct WriteCtx {
    std::vector<std::uint8_t>* out;
    OPJ_SIZE_T pos;
};

OPJ_SIZE_T mem_write(void* buffer, OPJ_SIZE_T nb, void* user) {
    auto* c = static_cast<WriteCtx*>(user);
    if (c->out->size() < c->pos + nb) c->out->resize(c->pos + nb);
    std::memcpy(c->out->data() + c->pos, buffer, nb);
    c->pos += nb;
    return nb;
}

OPJ_OFF_T write_skip(OPJ_OFF_T nb, void* user) {
    auto* c = static_cast<WriteCtx*>(user);
    if (nb < 0) return -1;
    c->pos += static_cast<OPJ_SIZE_T>(nb);
    if (c->out->size() < c->pos) c->out->resize(c->pos);
    return nb;
}

OPJ_BOOL write_seek(OPJ_OFF_T pos, void* user) {
    auto* c = static_cast<WriteCtx*>(user);
    if (pos < 0) return OPJ_FALSE;
    c->pos = static_cast<OPJ_SIZE_T>(pos);
    if (c->out->size() < c->pos) c->out->resize(c->pos);
    return OPJ_TRUE;
}

void quiet(const char*, void*) {}

OPJ_CODEC_FORMAT detect_format(const std::vector<std::uint8_t>& d) {
    // JP2 files open with the signature box; anything else is treated as a raw
    // J2K codestream (Second Life .j2c starts with the SOC marker 0xFF4F).
    static const std::uint8_t jp2_sig[] = {0x00, 0x00, 0x00, 0x0C, 0x6A, 0x50,
                                           0x20, 0x20, 0x0D, 0x0A, 0x87, 0x0A};
    if (d.size() >= sizeof(jp2_sig) &&
        std::memcmp(d.data(), jp2_sig, sizeof(jp2_sig)) == 0) {
        return OPJ_CODEC_JP2;
    }
    return OPJ_CODEC_J2K;
}

}  // namespace

std::optional<Image> decode_j2c(const std::vector<std::uint8_t>& data) {
    if (data.size() < 4) return std::nullopt;

    ReadCtx ctx{data.data(), static_cast<OPJ_SIZE_T>(data.size()), 0};
    opj_stream_t* stream = opj_stream_default_create(OPJ_TRUE);
    if (!stream) return std::nullopt;
    opj_stream_set_user_data(stream, &ctx, nullptr);
    opj_stream_set_user_data_length(stream, data.size());
    opj_stream_set_read_function(stream, mem_read);
    opj_stream_set_skip_function(stream, mem_skip);
    opj_stream_set_seek_function(stream, mem_seek);

    opj_codec_t* codec = opj_create_decompress(detect_format(data));
    if (!codec) {
        opj_stream_destroy(stream);
        return std::nullopt;
    }
    opj_set_error_handler(codec, quiet, nullptr);
    opj_set_warning_handler(codec, quiet, nullptr);
    opj_set_info_handler(codec, quiet, nullptr);

    opj_dparameters_t dparams;
    opj_set_default_decoder_parameters(&dparams);

    opj_image_t* img = nullptr;
    std::optional<Image> result;
    if (opj_setup_decoder(codec, &dparams) && opj_read_header(stream, codec, &img) &&
        opj_decode(codec, stream, img) && opj_end_decompress(codec, stream)) {
        const OPJ_UINT32 n = img->numcomps;
        if (n >= 1 && n <= 4 && img->comps[0].data) {
            const OPJ_UINT32 w = img->comps[0].w;
            const OPJ_UINT32 h = img->comps[0].h;
            bool ok = w > 0 && h > 0;
            for (OPJ_UINT32 c = 0; c < n && ok; ++c) {
                const auto& comp = img->comps[c];
                if (comp.w != w || comp.h != h || comp.dx != 1 || comp.dy != 1 ||
                    !comp.data) {
                    ok = false;
                }
            }
            if (ok) {
                Image out;
                out.width = w;
                out.height = h;
                out.channels = static_cast<std::uint8_t>(n);
                out.pixels.resize(out.expected_size());
                const std::size_t count = out.pixel_count();
                for (OPJ_UINT32 c = 0; c < n; ++c) {
                    const auto& comp = img->comps[c];
                    const int prec = static_cast<int>(comp.prec);
                    const int shift = prec > 8 ? prec - 8 : 0;
                    const int adjust = comp.sgnd ? (1 << (prec - 1)) : 0;
                    const OPJ_INT32* src = comp.data;
                    for (std::size_t i = 0; i < count; ++i) {
                        int v = (src[i] + adjust) >> shift;
                        if (v < 0) v = 0;
                        if (v > 255) v = 255;
                        out.pixels[i * n + c] = static_cast<std::uint8_t>(v);
                    }
                }
                result = std::move(out);
            }
        }
    }

    if (img) opj_image_destroy(img);
    opj_destroy_codec(codec);
    opj_stream_destroy(stream);
    return result;
}

std::optional<std::vector<std::uint8_t>> encode_j2c(const Image& image) {
    if (image.empty() || image.channels < 1 || image.channels > 4) return std::nullopt;
    if (image.pixels.size() != image.expected_size()) return std::nullopt;

    const OPJ_UINT32 n = image.channels;

    std::vector<opj_image_cmptparm_t> cmpt(n);
    for (OPJ_UINT32 c = 0; c < n; ++c) {
        cmpt[c] = opj_image_cmptparm_t{};
        cmpt[c].dx = 1;
        cmpt[c].dy = 1;
        cmpt[c].w = image.width;
        cmpt[c].h = image.height;
        cmpt[c].x0 = 0;
        cmpt[c].y0 = 0;
        cmpt[c].prec = 8;
        cmpt[c].bpp = 8;
        cmpt[c].sgnd = 0;
    }

    const OPJ_COLOR_SPACE cs = (n >= 3) ? OPJ_CLRSPC_SRGB : OPJ_CLRSPC_GRAY;
    opj_image_t* img = opj_image_create(n, cmpt.data(), cs);
    if (!img) return std::nullopt;
    img->x0 = 0;
    img->y0 = 0;
    img->x1 = image.width;
    img->y1 = image.height;

    const std::size_t count = image.pixel_count();
    for (OPJ_UINT32 c = 0; c < n; ++c) {
        OPJ_INT32* dst = img->comps[c].data;
        for (std::size_t i = 0; i < count; ++i) {
            dst[i] = image.pixels[i * n + c];
        }
    }

    opj_cparameters_t params;
    opj_set_default_encoder_parameters(&params);
    params.tcp_numlayers = 1;
    params.cp_disto_alloc = 1;
    params.tcp_rates[0] = 0;  // rate 0 == lossless
    // Wavelet decomposition levels must fit the image: the smallest dimension
    // has to survive numresolution-1 halvings. Clamp so small images (and
    // non-power-of-two bake slots) still encode.
    const std::uint32_t min_dim = std::min(image.width, image.height);
    int numres = 1;
    while (numres < params.numresolution && (min_dim >> numres) >= 1) ++numres;
    params.numresolution = numres;

    opj_codec_t* codec = opj_create_compress(OPJ_CODEC_J2K);
    if (!codec) {
        opj_image_destroy(img);
        return std::nullopt;
    }
    opj_set_error_handler(codec, quiet, nullptr);
    opj_set_warning_handler(codec, quiet, nullptr);
    opj_set_info_handler(codec, quiet, nullptr);

    std::vector<std::uint8_t> out;
    WriteCtx ctx{&out, 0};
    opj_stream_t* stream = opj_stream_default_create(OPJ_FALSE);
    std::optional<std::vector<std::uint8_t>> result;
    if (stream && opj_setup_encoder(codec, &params, img)) {
        opj_stream_set_user_data(stream, &ctx, nullptr);
        opj_stream_set_write_function(stream, mem_write);
        opj_stream_set_skip_function(stream, write_skip);
        opj_stream_set_seek_function(stream, write_seek);
        if (opj_start_compress(codec, img, stream) && opj_encode(codec, stream) &&
            opj_end_compress(codec, stream)) {
            out.resize(ctx.pos);
            result = std::move(out);
        }
    }

    if (stream) opj_stream_destroy(stream);
    opj_destroy_codec(codec);
    opj_image_destroy(img);
    return result;
}

#else  // no JPEG2000 codec compiled in

std::optional<Image> decode_j2c(const std::vector<std::uint8_t>&) { return std::nullopt; }

std::optional<std::vector<std::uint8_t>> encode_j2c(const Image&) { return std::nullopt; }

#endif

}  // namespace homeworldz::image
