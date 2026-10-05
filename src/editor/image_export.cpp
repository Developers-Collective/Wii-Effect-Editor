#include "image_export.h"
#include <png.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#endif

namespace breff {
    codec::Bytes encodeImageFile(const codec::TextureImage& pixels, const std::string& extension) {
        if (extension == ".png") {
            png_image image{};
            image.version = PNG_IMAGE_VERSION;
            image.width = pixels.width;
            image.height = pixels.height;
            image.format = PNG_FORMAT_RGBA;
            png_alloc_size_t size = 0;
            if (!png_image_write_to_memory(&image, nullptr, &size, 0, pixels.rgba.data(), 0, nullptr))
                throw std::runtime_error("Could not encode PNG: " + std::string(image.message));
            codec::Bytes bytes(size);
            if (!png_image_write_to_memory(&image, bytes.data(), &size, 0, pixels.rgba.data(), 0, nullptr))
                throw std::runtime_error("Could not encode PNG: " + std::string(image.message));
            bytes.resize(size);
            return bytes;
        }

#ifdef _WIN32
        using Microsoft::WRL::ComPtr;
        const GUID* container = nullptr;
        if (extension == ".jpg" || extension == ".jpeg")
            container = &GUID_ContainerFormatJpeg;
        else if (extension == ".bmp")
            container = &GUID_ContainerFormatBmp;
        else if (extension == ".gif")
            container = &GUID_ContainerFormatGif;
        else if (extension == ".tif" || extension == ".tiff")
            container = &GUID_ContainerFormatTiff;
        if (!container)
            throw std::runtime_error("Unsupported image export extension");

        const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        struct Uninit {
            bool active;
            ~Uninit() {
                if (active)
                    CoUninitialize();
            }
        } guard{SUCCEEDED(initialized)};

        auto check = [](HRESULT result) {
            if (FAILED(result))
                throw std::runtime_error("Could not encode image");
        };
        ComPtr<IWICImagingFactory> factory;
        check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
        ComPtr<IWICBitmap> bitmap;
        check(factory->CreateBitmapFromMemory(pixels.width, pixels.height, GUID_WICPixelFormat32bppRGBA,
                                             pixels.width * 4, UINT(pixels.rgba.size()),
                                             const_cast<BYTE*>(pixels.rgba.data()), &bitmap));
        ComPtr<IStream> stream;
        check(CreateStreamOnHGlobal(nullptr, TRUE, &stream));
        ComPtr<IWICBitmapEncoder> encoder;
        check(factory->CreateEncoder(*container, nullptr, &encoder));
        check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
        ComPtr<IWICBitmapFrameEncode> frame;
        ComPtr<IPropertyBag2> options;
        check(encoder->CreateNewFrame(&frame, &options));
        if (extension == ".bmp") {
            PROPBAG2 option{};
            option.pstrName = const_cast<LPOLESTR>(L"EnableV5Header32bppBGRA");
            VARIANT value{};
            value.vt = VT_BOOL;
            value.boolVal = VARIANT_TRUE;
            check(options->Write(1, &option, &value));
        }
        check(frame->Initialize(options.Get()));
        check(frame->SetSize(pixels.width, pixels.height));
        WICPixelFormatGUID format = extension == ".gif" ? GUID_WICPixelFormat8bppIndexed : GUID_WICPixelFormat32bppRGBA;
        check(frame->SetPixelFormat(&format));

        ComPtr<IWICPalette> palette;
        if (format == GUID_WICPixelFormat8bppIndexed) {
            check(factory->CreatePalette(&palette));
            check(palette->InitializeFromBitmap(bitmap.Get(), 256, TRUE));
            check(frame->SetPalette(palette.Get()));
        }
        ComPtr<IWICFormatConverter> converter;
        check(factory->CreateFormatConverter(&converter));
        check(converter->Initialize(bitmap.Get(), format, WICBitmapDitherTypeErrorDiffusion, palette.Get(), 0.5,
                                    WICBitmapPaletteTypeCustom));
        check(frame->WriteSource(converter.Get(), nullptr));
        check(frame->Commit());
        check(encoder->Commit());

        STATSTG status{};
        check(stream->Stat(&status, STATFLAG_NONAME));
        if (status.cbSize.QuadPart > 256 * 1024 * 1024)
            throw std::runtime_error("Encoded image exceeds 256 MiB");
        codec::Bytes bytes(size_t(status.cbSize.QuadPart));
        check(stream->Seek({}, STREAM_SEEK_SET, nullptr));
        ULONG read = 0;
        check(stream->Read(bytes.data(), ULONG(bytes.size()), &read));
        if (read != bytes.size())
            throw std::runtime_error("Could not read encoded image");
        return bytes;
#else
        throw std::runtime_error("Choose PNG for image export");
#endif
    }
}
