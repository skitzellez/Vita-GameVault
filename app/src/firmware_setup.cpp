// Vitox 0.0.33.0: first-run firmware setup - see firmware_setup.h.
#include "firmware_setup.h"
#include "common.h"
#include <vitox_firmware_stage.h>
#include <bcrypt.h>
#include <fileapifromapp.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.FileProperties.h>
#include <winrt/Windows.Storage.Search.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Headers.h>
#include <atomic>
#include <filesystem>
#include <thread>
#include <vector>

namespace vitox::firmware {
namespace fs = std::filesystem;

namespace {
const PackageInfo kPkgs[kPackages] = {
    { L"PS Vita System Software", L"PSVUPDAT.PUP", L"Firmware 3.74 (PSVUPDAT.PUP) from the official PlayStation website",
        "6EF6DC8DA6DB026F28647713E473486D770087A605C52A8D751BFCA7478386CF" },
    { L"Pre-installed Firmware Package", L"PREINST.PUP", L"Firmware 3.74 pre-installed apps package (pd0)",
        "339D1439EB329CFBD1A936F0A1563458E0555EF52975486B2844E24B6049E33E" },
    { L"System Font Package", L"FONTPKG.PUP", L"Firmware font package (sa0) - the system fonts games use for text",
        "F372D306A7F2711125BEA29AA75CBCB1EB19724E9648478FBA07B8D655B37344" },
};

std::wstring hex(const uint8_t *d, size_t n) {
    static const wchar_t *k = L"0123456789ABCDEF";
    std::wstring s;
    for (size_t i = 0; i < n; ++i)
        s += k[d[i] >> 4], s += k[d[i] & 15];
    return s;
}

// Game Vault · PS Vita: the three packages straight from Sony's update servers (the links on Vita3K's quick start page),
// so the player needs no PC and no USB drive for them. Each download is checked against the same SHA-256 as a copy.
const wchar_t *const kSonyUrls[kPackages] = {
    L"http://dus01.psv.update.playstation.net/update/psv/image/2022_0209/rel_f2c7b12fe85496ec88a0391b514d6e3b/PSVUPDAT.PUP",
    L"http://dus01.psp2.update.playstation.net/update/psp2/image/2022_0209/pre_efd1ef6c1cc2fe92e72e9e783e421237/PSP2UPDAT.PUP?dest=us",
    L"http://dus01.psp2.update.playstation.net/update/psp2/image/2022_0209/sd_59dcf059d3328fb67be7e51f8aa33418/PSP2UPDAT.PUP?dest=us",
};

void fw_log(const char *line) { log_line(Log::Lifecycle, "firmware: %s", line); }
void fw_mem(const char *) {}
} // namespace

const PackageInfo &package(int i) { return kPkgs[i]; }

std::wstring root(const std::wstring &dev_root) {
    static const bool test = [] {
        try {
            return std::wstring(winrt::Windows::ApplicationModel::Package::Current().Id().Name()) == L"Vitox.FirmwareTest";
        } catch (...) {
            return false;
        }
    }();
    return test ? dev_root + L"\\fwtest" : dev_root;
}

bool installed(const std::wstring &dev_root) {
    std::error_code ec;
    return fs::exists(fs::path(root(dev_root)) / L"firmware-staging" / L"stage-complete.txt", ec);
}

struct Setup::Impl {
    std::wstring dev, fw;
    std::mutex m;
    Snapshot s;
    std::thread worker;
    std::atomic<bool> stop{ false };

    void set(Phase p, const std::wstring &msg) {
        std::lock_guard<std::mutex> lock(m);
        s.phase = p;
        s.message = msg;
        ++s.version;
    }
    fs::path input() const { return fs::path(fw) / L"firmware-staging" / L"input"; }

    // copies src to dst while hashing it; returns the SHA-256 (empty on a read/write error)
    std::wstring copy_hash(const std::wstring &src, const fs::path &dst) {
        HANDLE in = CreateFileFromAppW(src.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (in == INVALID_HANDLE_VALUE)
            return L"";
        HANDLE out = CreateFileFromAppW(dst.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (out == INVALID_HANDLE_VALUE) {
            CloseHandle(in);
            return L"";
        }
        BCRYPT_HASH_HANDLE h = nullptr;
        BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE, &h, nullptr, 0, nullptr, 0, 0);
        std::vector<uint8_t> buf(4 << 20);
        bool ok = h != nullptr;
        DWORD got = 0;
        while (ok && !stop && ReadFile(in, buf.data(), static_cast<DWORD>(buf.size()), &got, nullptr) && got) {
            DWORD put = 0;
            ok = WriteFile(out, buf.data(), got, &put, nullptr) && put == got && BCryptHashData(h, buf.data(), got, 0) == 0;
            std::lock_guard<std::mutex> lock(m);
            s.done_bytes += got;
            ++s.version;
        }
        uint8_t dig[32] = {};
        ok = ok && !stop && BCryptFinishHash(h, dig, 32, 0) == 0;
        if (h)
            BCryptDestroyHash(h);
        CloseHandle(in);
        CloseHandle(out);
        return ok ? hex(dig, 32) : L"";
    }

    // downloads url to dst while hashing it; returns the SHA-256 (empty on an error)
    std::wstring download_hash(const wchar_t *url, const fs::path &dst) {
        using namespace winrt::Windows::Web::Http;
        using namespace winrt::Windows::Storage::Streams;
        HANDLE out = INVALID_HANDLE_VALUE;
        BCRYPT_HASH_HANDLE h = nullptr;
        bool ok = false;
        try {
            HttpClient client;
            HttpResponseMessage r = client.GetAsync(winrt::Windows::Foundation::Uri(url), HttpCompletionOption::ResponseHeadersRead).get();
            if (!r.IsSuccessStatusCode()) {
                log_line(Log::Lifecycle, "firmware: download HTTP %d", static_cast<int>(r.StatusCode()));
                return L"";
            }
            const auto len = r.Content().Headers().ContentLength();
            if (len) {
                std::lock_guard<std::mutex> lock(m);
                s.total_bytes += len.Value();
                ++s.version;
            }
            IInputStream in = r.Content().ReadAsInputStreamAsync().get();
            out = CreateFileFromAppW(dst.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE, &h, nullptr, 0, nullptr, 0, 0);
            ok = out != INVALID_HANDLE_VALUE && h != nullptr;
            Buffer buf(1 << 20);
            while (ok && !stop) {
                IBuffer got = in.ReadAsync(buf, buf.Capacity(), InputStreamOptions::Partial).get();
                const uint32_t n = got.Length();
                if (!n)
                    break;
                DWORD put = 0;
                ok = WriteFile(out, got.data(), n, &put, nullptr) && put == n && BCryptHashData(h, got.data(), n, 0) == 0;
                std::lock_guard<std::mutex> lock(m);
                s.done_bytes += n;
                ++s.version;
            }
        } catch (winrt::hresult_error const &e) {
            log_line(Log::Lifecycle, "firmware: download failed 0x%08lX", static_cast<unsigned long>(e.code()));
            ok = false;
        }
        uint8_t dig[32] = {};
        ok = ok && !stop && BCryptFinishHash(h, dig, 32, 0) == 0;
        if (h)
            BCryptDestroyHash(h);
        if (out != INVALID_HANDLE_VALUE)
            CloseHandle(out);
        return ok ? hex(dig, 32) : L"";
    }

    // Game Vault · PS Vita: every package not yet in the staging input is fetched from Sony. True when all three are there.
    bool download_all() {
        std::error_code ec;
        bool all = true;
        for (int i = 0; i < kPackages && !stop; ++i) {
            if (fs::exists(input() / kPkgs[i].file, ec)) {
                std::lock_guard<std::mutex> lock(m);
                s.have[i] = true;
                continue;
            }
            {
                std::lock_guard<std::mutex> lock(m);
                s.current = kPkgs[i].name;
                ++s.version;
            }
            const fs::path tmp = input() / L"download.tmp";
            const std::wstring sha = download_hash(kSonyUrls[i], tmp);
            const bool match = !sha.empty() && (sha == widen(kPkgs[i].sha256) ||
                                                (i == 2 && sha == L"C3C03FC7363DD573D90E5157629BF11551F434B283CC898D9FFC71DD716B791C"));
            log_line(Log::Lifecycle, "firmware: download %s SHA-256 %s -> %s", narrow(kPkgs[i].file).c_str(), narrow(sha).c_str(),
                match ? "ok" : "NOT the expected package");
            if (match) {
                fs::rename(tmp, input() / kPkgs[i].file, ec);
                std::lock_guard<std::mutex> lock(m);
                s.have[i] = !ec;
                all = all && !ec;
            } else {
                fs::remove(tmp, ec);
                all = false;
            }
        }
        return all && !stop;
    }

    void run() {
        using namespace winrt::Windows::Storage;
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        std::error_code ec;
        fs::create_directories(input(), ec);
        // Game Vault · PS Vita: from Sony first; a USB drive with the .PUP files is only needed when that fails (offline)
        set(Phase::Copying, L"Downloading the PS Vita system software from PlayStation…");
        if (!download_all()) {
            log_line(Log::Lifecycle, "firmware: download incomplete - looking on the USB drives");
            std::lock_guard<std::mutex> lock(m);
            s.done_bytes = s.total_bytes = 0;
        }
        bool have_all = true;
        {
            std::lock_guard<std::mutex> lock(m);
            for (int i = 0; i < kPackages; ++i)
                have_all = have_all && s.have[i];
        }
        if (have_all) {
            install_now();
            return;
        }
        set(Phase::Scanning, L"Looking for .PUP files on the USB drives…");
        struct Found {
            std::wstring path, name;
            uint64_t size = 0;
        };
        std::vector<Found> pups;
        unsigned drives = 0;
        try {
            for (StorageFolder const &d : KnownFolders::RemovableDevices().GetFoldersAsync().get()) {
                ++drives;
                Search::QueryOptions opts(Search::CommonFileQuery::DefaultQuery, winrt::single_threaded_vector<winrt::hstring>({ L".pup" }));
                opts.FolderDepth(Search::FolderDepth::Deep);
                opts.IndexerOption(Search::IndexerOption::DoNotUseIndexer);
                for (auto const &f : d.CreateFileQueryWithOptions(opts).GetFilesAsync(0, 64).get()) {
                    Found x{ std::wstring(f.Path()), std::wstring(f.Name()) };
                    try {
                        x.size = f.GetBasicPropertiesAsync().get().Size();
                    } catch (...) {
                    }
                    pups.push_back(x);
                }
            }
        } catch (winrt::hresult_error const &e) {
            log_line(Log::Lifecycle, "firmware: USB scan failed 0x%08lX", static_cast<unsigned long>(e.code()));
        }
        log_line(Log::Lifecycle, "firmware: %u drive(s), %u .PUP file(s)", drives, static_cast<unsigned>(pups.size()));
        {
            std::lock_guard<std::mutex> lock(m);
            s.drives = drives;
            s.pups_seen = static_cast<unsigned>(pups.size());
            s.total_bytes = 0;
            for (const Found &f : pups)
                s.total_bytes += f.size;
        }
        if (pups.empty()) {
            set(Phase::Failed, drives == 0 ? L"No USB drive found. Connect a USB drive with the .PUP files, then press A to try again."
                                           : L"No .PUP files on the USB drive. Copy the three firmware files to it, then press A to try again.");
            return;
        }
        set(Phase::Copying, L"Checking and copying the firmware files…");
        for (const Found &f : pups) {
            if (stop)
                return;
            {
                std::lock_guard<std::mutex> lock(m);
                s.current = f.name;
                ++s.version;
            }
            const fs::path tmp = input() / L"incoming.tmp";
            const std::wstring sha = copy_hash(f.path, tmp);
            int match = -1;
            for (int i = 0; i < kPackages; ++i)
                if (sha == widen(kPkgs[i].sha256) || (i == 2 && sha == L"C3C03FC7363DD573D90E5157629BF11551F434B283CC898D9FFC71DD716B791C")) // 0.0.33.1: Sony's 2022 font package
                    match = i;
            log_line(Log::Lifecycle, "firmware: %s SHA-256 %s -> %s", narrow(f.name).c_str(), narrow(sha).c_str(),
                match >= 0 ? narrow(kPkgs[match].file).c_str() : "not a required package");
            if (match >= 0) {
                fs::rename(tmp, input() / kPkgs[match].file, ec);
                std::lock_guard<std::mutex> lock(m);
                s.have[match] = !ec;
                ++s.version;
            } else {
                fs::remove(tmp, ec);
            }
        }
        {
            std::lock_guard<std::mutex> lock(m);
            for (int i = 0; i < kPackages; ++i)
                if (!s.have[i]) {
                    s.phase = Phase::Failed;
                    s.message = L"Some firmware files are missing or are a different version (see the list). Add them to the USB drive, then press A to try again.";
                    ++s.version;
                    return;
                }
            s.current.clear();
        }
        install_now();
    }

    void install_now() {
        std::error_code ec;
        set(Phase::Installing, L"Installing the firmware… this takes about a minute.");
        const VitoxCoreHost host{ &fw_log, &fw_mem };
        const VitoxFirmwareStageResult r = vitox_firmware_stage_run(&host, fw.c_str());
        const bool ok = r.requested && r.selftest && r.hashes && r.installed && r.partitions && r.libpgf && r.fonts && r.contained;
        log_line(Log::Lifecycle, "firmware: install %s (files=%u bytes=%llu)", ok ? "PASS" : "FAIL", r.files, static_cast<unsigned long long>(r.bytes));
        if (ok) {
            fs::remove_all(input(), ec); // the packages are installed; the copies are not needed
            set(Phase::Done, L"The firmware is installed. Vitox is ready.");
        } else {
            set(Phase::Failed, L"The firmware could not be installed. Press A to try again.");
        }
    }
};

Setup::Setup(std::wstring dev_root) : impl_(std::make_unique<Impl>()) {
    impl_->dev = dev_root;
    impl_->fw = root(dev_root);
    std::error_code ec;
    for (int i = 0; i < kPackages; ++i)
        impl_->s.have[i] = installed(dev_root) || fs::exists(impl_->input() / kPkgs[i].file, ec);
}

Setup::~Setup() {
    impl_->stop = true;
    if (impl_->worker.joinable())
        impl_->worker.join();
}

void Setup::start() {
    if (impl_->worker.joinable()) {
        if (impl_->s.phase != Phase::Failed && impl_->s.phase != Phase::Done)
            return;
        impl_->worker.join();
    }
    {
        std::lock_guard<std::mutex> lock(impl_->m);
        impl_->s.done_bytes = 0;
        impl_->s.message.clear();
    }
    impl_->worker = std::thread([this] { impl_->run(); });
}

bool Setup::poll(uint64_t have, Snapshot &out) {
    std::lock_guard<std::mutex> lock(impl_->m);
    if (impl_->s.version == have && have != 0)
        return false;
    out = impl_->s;
    return true;
}

} // namespace vitox::firmware
