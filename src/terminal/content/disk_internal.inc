// imported from the C tree by tools/export_disks.py; port-authored entries are added here
// directly and the exporter is no longer re-run over this file.

constexpr RomFile kInternalImageFiles[] = {
    {"BREACH.EXE", {}, {}, FsExe::Breach, 14336, false},
    {"COMMS.EXE", {}, {}, FsExe::Comms, 26624, false},
    {"DEV.EXE", {}, {}, FsExe::Dev, 8192, false},
    {"LINK.EXE", {}, {}, FsExe::Link, 9216, false},
    {"MAP.EXE", {}, {}, FsExe::Map, 30208, false},
    {"README.TXT", R"internal5(DR-OS 2.2  (C) REDLINE SYSTEMS 1988
REV C -- BUILD 0219

FIELD PACKAGE PRELOADED ON DRIVE A:. TO RUN A
PROGRAM, TYPE ITS NAME. TYPE HELP FOR COMMANDS.

DRIVE A: IS FIXED. 354K. NON-REMOVABLE. WATCH YOUR
FREE SPACE. DELETED DATA IS NOT RECOVERABLE.

DRIVE B: TAKES 5.25 INCH MEDIA. LABEL UP. SLOT IS
BELOW THE SCREEN.

REDLINE SYSTEMS ACCEPTS NO LIABILITY FOR DATA LOSS,
RADIO INTERFERENCE, OR EFFECTS OF PROLONGED
EXPOSURE.
)internal5", {}, FsExe::None, 0, false},
    {"STATUS.EXE", {}, {}, FsExe::Status, 24576, false},
    {"SYNTH.EXE", {}, {}, FsExe::Synth, 15360, false},
    {"TAPES.EXE", {}, {}, FsExe::Tapes, 18944, false},
    {"TRAVEL.EXE", {}, {}, FsExe::Travel, 22528, false},
    {"VIDEO.EXE", {}, {}, FsExe::Video, 12288, false},
};

constexpr DiskImage kInternalImage{"internal", "DR-OS SYSTEM", 362496, kInternalImageFiles, {}};
