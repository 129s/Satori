#pragma once

#include <windows.h>

#include <wrl/client.h>

#include <mutex>
#include <string>

#include "win/ui/DWriteCompat.h"
#include "win/resources/ResourceIds.h"

namespace winui {

namespace detail {

inline std::wstring WriteNunitoTempFile(const void* bytes, DWORD length) {
    if (!bytes || length == 0) {
        return {};
    }
    wchar_t tempDir[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, tempDir)) {
        return {};
    }
    std::wstring path = tempDir;
    path += L"Satori_Nunito-Regular.ttf";
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return {};
    }
    DWORD written = 0;
    const BOOL ok = WriteFile(file, bytes, length, &written, nullptr);
    CloseHandle(file);
    if (!ok || written != length) {
        DeleteFileW(path.c_str());
        return {};
    }
    return path;
}

inline std::wstring ExtractNunitoFontFromResource() {
    constexpr WORD kRsrcTypeRawData = 10;  // RT_RCDATA
    HMODULE module = GetModuleHandleW(nullptr);
    if (!module) {
        return {};
    }
    HRSRC res = FindResourceW(
        module, MAKEINTRESOURCEW(IDR_FONT_NUNITO_REGULAR),
        MAKEINTRESOURCEW(kRsrcTypeRawData));
    if (!res) {
        return {};
    }
    HGLOBAL handle = LoadResource(module, res);
    if (!handle) {
        return {};
    }
    DWORD size = SizeofResource(module, res);
    if (size == 0) {
        return {};
    }
    const void* data = LockResource(handle);
    if (!data) {
        return {};
    }
    return WriteNunitoTempFile(data, size);
}

inline std::wstring FindNunitoFontNearExecutable() {
    wchar_t exePath[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH)) {
        return {};
    }
    std::wstring dir = exePath;
    const size_t pos = dir.find_last_of(L"/\\");
    if (pos != std::wstring::npos) {
        dir.resize(pos);
    }
    auto exists = [](const std::wstring& path) -> bool {
        const DWORD attr = GetFileAttributesW(path.c_str());
        return attr != INVALID_FILE_ATTRIBUTES &&
               !(attr & FILE_ATTRIBUTE_DIRECTORY);
    };
    const wchar_t* relativeCandidates[] = {
        L"\\assets\\Fonts\\Nunito-Regular.ttf",
        L"\\..\\assets\\Fonts\\Nunito-Regular.ttf",
        L"\\..\\..\\assets\\Fonts\\Nunito-Regular.ttf",
        L"\\..\\..\\..\\assets\\Fonts\\Nunito-Regular.ttf",
        L"\\..\\..\\..\\..\\assets\\Fonts\\Nunito-Regular.ttf",
    };
    for (const auto* rel : relativeCandidates) {
        if (!rel) continue;
        std::wstring candidate = dir + rel;
        if (exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

inline const std::wstring& ResolveNunitoFontFilePath() {
    static std::once_flag onceFlag;
    static std::wstring path;
    std::call_once(onceFlag, []() {
        path = ExtractNunitoFontFromResource();
        if (path.empty()) {
            path = FindNunitoFontNearExecutable();
        }
    });
    return path;
}

}  // namespace detail

// 返回 Nunito 字体家族名称，便于调用侧保持一致。
inline const wchar_t* NunitoFontFamily() {
    return L"Nunito";
}

inline const std::wstring& NunitoFontFilePath() {
    return detail::ResolveNunitoFontFilePath();
}

// 通过 AddFontResourceEx 在进程级注册私有 Nunito 字体（仅加载一次）。
inline bool EnsureNunitoFontLoaded() {
    static std::once_flag onceFlag;
    static bool loaded = false;
    std::call_once(onceFlag, [&]() {
        const auto& path = NunitoFontFilePath();
        if (path.empty()) {
            loaded = false;
            return;
        }
        loaded = AddFontResourceExW(path.c_str(), FR_PRIVATE, nullptr) > 0;
    });
    return loaded;
}

inline Microsoft::WRL::ComPtr<SATORI_DWRITE_FONT_COLLECTION_TYPE>
CreateNunitoFontCollection(IDWriteFactory* factory) {
#if SATORI_HAS_DWRITE3
    if (!factory) {
        return nullptr;
    }
    const auto& path = NunitoFontFilePath();
    if (path.empty()) {
        return nullptr;
    }
    Microsoft::WRL::ComPtr<IDWriteFactory3> factory3;
    if (FAILED(factory->QueryInterface(IID_PPV_ARGS(&factory3)))) {
        return nullptr;
    }
    Microsoft::WRL::ComPtr<IDWriteFontSetBuilder> builder;
    if (FAILED(factory3->CreateFontSetBuilder(&builder)) || !builder) {
        return nullptr;
    }
    Microsoft::WRL::ComPtr<IDWriteFontSetBuilder1> builder1;
    if (FAILED(builder.As(&builder1)) || !builder1) {
        return nullptr;
    }
    Microsoft::WRL::ComPtr<IDWriteFontFile> fontFile;
    if (FAILED(factory3->CreateFontFileReference(path.c_str(), nullptr,
                                                 &fontFile))) {
        return nullptr;
    }
    if (FAILED(builder1->AddFontFile(fontFile.Get()))) {
        return nullptr;
    }
    Microsoft::WRL::ComPtr<IDWriteFontSet> fontSet;
    if (FAILED(builder1->CreateFontSet(&fontSet))) {
        return nullptr;
    }
    Microsoft::WRL::ComPtr<SATORI_DWRITE_FONT_COLLECTION_TYPE> fontCollection;
    if (FAILED(factory3->CreateFontCollectionFromFontSet(
            fontSet.Get(), &fontCollection))) {
        return nullptr;
    }
    return fontCollection;
#else
    (void)factory;
    return nullptr;
#endif
}

inline void ApplyChineseFontFallback(IDWriteFactory* factory,
                                     IDWriteTextFormat* format) {
#if SATORI_HAS_DWRITE2
    if (!factory || !format) {
        return;
    }
    Microsoft::WRL::ComPtr<IDWriteFactory2> factory2;
    if (FAILED(factory->QueryInterface(IID_PPV_ARGS(&factory2)))) {
        return;
    }

    static std::once_flag fallbackOnce;
    static Microsoft::WRL::ComPtr<IDWriteFontFallback> chineseFallback;
    auto factoryCopy = factory2;
    std::call_once(fallbackOnce, [factoryCopy]() {
        if (!factoryCopy) {
            return;
        }
        Microsoft::WRL::ComPtr<IDWriteFontFallbackBuilder> builder;
        if (FAILED(factoryCopy->CreateFontFallbackBuilder(&builder))) {
            return;
        }
        const DWRITE_UNICODE_RANGE ranges[] = {
            {0x3400, 0x4DBF},   // CJK Extension A
            {0x4E00, 0x9FFF},   // CJK Unified Ideographs
            {0x20000, 0x2A6DF}  // Extension B
        };
        const wchar_t* families[] = {L"NSimSun"};
        const UINT32 rangeCount =
            static_cast<UINT32>(sizeof(ranges) / sizeof(ranges[0]));
        if (FAILED(builder->AddMapping(ranges, rangeCount, families, 1,
                                       nullptr, L"zh-CN", nullptr, 1.0f))) {
            return;
        }
        (void)builder->CreateFontFallback(&chineseFallback);
    });

    if (!chineseFallback) {
        return;
    }
    Microsoft::WRL::ComPtr<IDWriteTextFormat2> format2;
    if (SUCCEEDED(format->QueryInterface(IID_PPV_ARGS(&format2)))) {
        format2->SetFontFallback(chineseFallback.Get());
    }
#else
    (void)factory;
    (void)format;
#endif
}

}  // namespace winui
