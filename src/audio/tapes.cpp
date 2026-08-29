#include "audio/tapes.h"
#include "core/log.h"
#include "math/vmath.h"
#include "platform/filesystem.h"

#include <algorithm>
#include <cctype>

namespace anom {
namespace {

bool has_audio_ext(std::string_view name)
{
    const std::size_t dot = name.find_last_of('.');
    if (dot == std::string_view::npos) {
        return false;
    }
    FixedString<8> ext;
    ext.assign(name.substr(dot));
    for (std::size_t i = 0; i < ext.size(); i++) {
        ext.data()[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(ext.c_str()[i])));
    }
    return ext == ".wav" || ext == ".mp3" || ext == ".ogg" || ext == ".flac";
}

void display_name(std::string_view file, FixedString<48>& out)
{
    std::size_t begin = 0;
    while (begin < file.size() && (std::isdigit(static_cast<unsigned char>(file[begin]))
                                   || file[begin] == '_' || file[begin] == '-'
                                   || file[begin] == ' ')) {
        begin++;
    }
    std::size_t end = file.find_last_of('.');
    if (end == std::string_view::npos || end < begin) {
        end = file.size();
    }

    out.clear();
    for (std::size_t i = begin; i < end && out.size() < out.capacity(); i++) {
        const char c = file[i] == '_' ? ' ' : file[i];
        const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        out.append(std::string_view(&upper, 1));
    }
    while (!out.empty() && out.c_str()[out.size() - 1] == ' ') {
        out.data()[out.size() - 1] = '\0';
    }
}

u32 real_file_bytes(std::string_view path)
{
    std::FILE* f = fs::open(path, "rb");
    if (!f) {
        return 0;
    }
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fclose(f);
    return size > 0 ? static_cast<u32>(size) : 0;
}

} // namespace

void TapeLibrary::scan_dir(std::string_view dir, bool relay)
{
    fs::DirEntry entries[kTapeTrackMax];
    const u32 found = fs::list_dir(dir, entries);
    std::sort(entries, entries + found, [](const fs::DirEntry& a, const fs::DirEntry& b) {
        return a.name.view() < b.name.view();
    });

    for (u32 i = 0; i < found && count_ < kTapeTrackMax; i++) {
        if (entries[i].is_dir || !has_audio_ext(entries[i].name.view())) {
            continue;
        }
        Track& t = tracks_[count_];
        t.path.format("%.*s/%s", static_cast<int>(dir.size()), dir.data(),
                      entries[i].name.c_str());
        display_name(entries[i].name.view(), t.name);
        t.on_relay = relay;

        const u32 real = real_file_bytes(t.path.view());
        const u32 fictional = real / 56;
        t.file_bytes = fictional < 20480 ? 20480 : (fictional > 98304 ? 98304 : fictional);
        count_++;
    }
}

void TapeLibrary::init()
{
    count_ = 0;
    scan_dir("assets/tapes/found", false);
    scan_dir("assets/tapes/relay", true);
    scan_dir("tapes", true);
    log_info("tapes: %u tracks", count_);
}

std::string_view TapeLibrary::name(i32 track) const
{
    return valid(track) ? tracks_[track].name.view() : std::string_view("UNKNOWN");
}

std::string_view TapeLibrary::path(i32 track) const
{
    return valid(track) ? tracks_[track].path.view() : std::string_view();
}

bool TapeLibrary::on_relay(i32 track) const
{
    return valid(track) && tracks_[track].on_relay;
}

u32 TapeLibrary::file_bytes(i32 track) const
{
    return valid(track) ? tracks_[track].file_bytes : 0;
}

std::string_view TapeLibrary::label(i32 aux) const
{
    if (aux <= 0) {
        return "BLANK";
    }
    return valid(aux - 1) ? tracks_[aux - 1].name.view() : std::string_view("UNKNOWN");
}

} // namespace anom
