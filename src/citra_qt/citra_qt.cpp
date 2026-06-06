//FILE MODIFIED BY AzaharPlus APRIL 2025

// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <clocale>
#include <iostream>
#include <memory>
#include <optional>
#include <thread>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QIcon>
#include <QLabel>
#include <QMessageBox>
#include <QPalette>
#include <QSysInfo>
#include <QtConcurrent/QtConcurrentMap>
#include <QtConcurrent/QtConcurrentRun>
#include <QtGui>
#include <QtWidgets>
#include <boost/algorithm/string/replace.hpp>
#include <fmt/format.h>
#include <fmt/ostream.h>
#ifdef __APPLE__
#include <unistd.h> // for chdir
#endif
#ifdef _WIN32
#include <shlobj.h>
#include <windows.h>
#else
#include <getopt.h>
#endif
#ifdef __unix__
#include <QVariant>
#include <QtDBus/QDBusInterface>
#include <QtDBus/QtDBus>
#include "common/linux/gamemode.h"
#endif
#include "citra_meta/common_strings.h"
#include "citra_qt/aboutdialog.h"
#include "citra_qt/applets/mii_selector.h"
#include "citra_qt/applets/swkbd.h"
#include "citra_qt/bootmanager.h"
#include "citra_qt/camera/qt_multimedia_camera.h"
#include "citra_qt/camera/still_image_camera.h"
#include "citra_qt/citra_qt.h"
#include "citra_qt/compatibility_list.h"
#include "citra_qt/configuration/config.h"
#include "citra_qt/configuration/configure_dialog.h"
#include "citra_qt/configuration/configure_per_game.h"
#include "citra_qt/debugger/console.h"
#include "citra_qt/debugger/graphics/graphics.h"
#include "citra_qt/debugger/graphics/graphics_breakpoints.h"
#include "citra_qt/debugger/graphics/graphics_cmdlists.h"
#include "citra_qt/debugger/graphics/graphics_surface.h"
#include "citra_qt/debugger/graphics/graphics_tracing.h"
#include "citra_qt/debugger/graphics/graphics_vertex_shader.h"
#include "citra_qt/debugger/ipc/recorder.h"
#include "citra_qt/debugger/lle_service_modules.h"
#if MICROPROFILE_ENABLED
#include "citra_qt/debugger/profiler.h"
#endif
#include "citra_qt/debugger/registers.h"
#include "citra_qt/debugger/wait_tree.h"
#ifdef USE_DISCORD_PRESENCE
#include "citra_qt/discord.h"
#endif
#include "citra_qt/dumping/dumping_dialog.h"
#include "citra_qt/game_list.h"
#include "citra_qt/hotkeys.h"
#include "citra_qt/loading_screen.h"
#include "citra_qt/movie/movie_play_dialog.h"
#include "citra_qt/movie/movie_record_dialog.h"
#include "citra_qt/multiplayer/state.h"
#include "citra_qt/qt_image_interface.h"
#include "citra_qt/qt_swizzle.h"
#include "citra_qt/uisettings.h"
#include "common/play_time_manager.h"
#ifdef ENABLE_QT_UPDATE_CHECKER
#include "citra_qt/update_checker.h"
#endif
#include "citra_qt/util/clickable_label.h"
#include "citra_qt/util/graphics_device_info.h"
#include "citra_qt/util/util.h"
#include "common/arch.h"
#include "common/common_paths.h"
#include "common/dynamic_library/dynamic_library.h"
#include "common/file_util.h"
#include "common/literals.h"
#include "common/logging/backend.h"
#include "common/logging/log.h"
#include "common/memory_detect.h"
#include "common/scm_rev.h"
#include "common/scope_exit.h"
#if CITRA_ARCH(x86_64)
#include "common/x64/cpu_detect.h"
#endif
#include "common/settings.h"
#include "common/string_util.h"
#include "common/zstd_compression.h"
#include "core/core.h"
#include "core/dumping/backend.h"
#include "core/file_sys/archive_extsavedata.h"
#include "core/file_sys/archive_source_sd_savedata.h"
#include "core/frontend/applets/default_applets.h"
#include "core/hle/service/am/am.h"
#include "core/hle/service/fs/archive.h"
#include "core/hle/service/nfc/nfc.h"
#include "core/loader/loader.h"
#include "core/loader/ncch.h"
#include "core/movie.h"
#include "core/savestate.h"
#include "core/system_titles.h"
#include "input_common/main.h"
#include "network/network_settings.h"
#include "ui_main.h"
#include "video_core/gpu.h"
#include "video_core/renderer_base.h"

#ifdef __APPLE__
#include "common/apple_authorization.h"
#include "common/apple_utils.h"
Q_IMPORT_PLUGIN(QDarwinCameraPermissionPlugin);
#endif

#ifdef USE_DISCORD_PRESENCE
#include "citra_qt/discord_impl.h"
#endif

#ifdef QT_STATICPLUGIN
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin);
#endif

#ifdef HAVE_SDL2
#include <SDL.h>
#endif

#include "core/hw/unique_data.h"
#include "core/zip_pass.h"

constexpr int default_mouse_timeout = 2500;

/**
 * "Callouts" are one-time instructional messages shown to the user. In the config settings, there
 * is a bitfield "callout_flags" options, used to track if a message has already been shown to the
 * user. This is 32-bits - if we have more than 32 callouts, we should retire and recycle old ones.
 */

const int GMainWindow::max_recent_files_item;

// There is a bug in the QT implementation on MSYS2 builds
// that cause corners to appear when the app is switched to
// fullscreen. The following code aims to fix that issue
// until it is addressed upstream. It works by manually
// disabling corners through the DWM API.
// TODO(PabloMK7): Remove once the upstream bug is solved.
#if defined(_WIN32) && !defined(_MSC_VER)
#define NEEDS_ROUND_CORNERS_FIX
#endif

#ifdef NEEDS_ROUND_CORNERS_FIX
#include <dwmapi.h>
class WindowCornerManager {
public:
    static WindowCornerManager& instance() {
        static WindowCornerManager inst;
        return inst;
    }

    void blockRoundedCorners(QWidget* widget, bool block) {
        HWND hwnd = reinterpret_cast<HWND>(widget->winId());
        DWORD pref;

        if (block) {
            pref = DWMWCP_DEFAULT;
            if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref,
                                                sizeof(pref)))) {
                original_prefs[hwnd] = pref;
            } else {
                original_prefs[hwnd] = DWMWCP_DEFAULT;
            }

            pref = DWMWCP_DONOTROUND;
            DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
        } else {
            auto it = original_prefs.find(hwnd);
            if (it == original_prefs.end())
                return;

            pref = it->second;

            DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));

            original_prefs.erase(it);
        }
    }

private:
    WindowCornerManager() = default;
    ~WindowCornerManager() = default;

    std::unordered_map<HWND, DWORD> original_prefs;
};
#endif

static QString PrettyProductName() {
#ifdef _WIN32
    // After Windows 10 Version 2004, Microsoft decided to switch to a different notation: 20H2
    // With that notation change they changed the registry key used to denote the current version
    QSettings windows_registry(
        QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"),
        QSettings::NativeFormat);
    const QString release_id = windows_registry.value(QStringLiteral("ReleaseId")).toString();
    if (release_id == QStringLiteral("2009")) {
        const u32 current_build = windows_registry.value(QStringLiteral("CurrentBuild")).toUInt();
        const QString display_version =
            windows_registry.value(QStringLiteral("DisplayVersion")).toString();
        const u32 ubr = windows_registry.value(QStringLiteral("UBR")).toUInt();
        const u32 version = current_build >= 22000 ? 11 : 10;
        return QStringLiteral("Windows %1 Version %2 (Build %3.%4)")
            .arg(QString::number(version), display_version, QString::number(current_build),
                 QString::number(ubr));
    }
#endif
    return QSysInfo::prettyProductName();
}

void GMainWindow::ShowCommandOutput(std::string title, std::string message) {
#ifdef _WIN32
    boost::replace_all(message, " ", "\u00a0"); // Non-breaking space
    boost::replace_all(message, "-", "\u2011"); // Non-breaking hyphen
    QMessageBox::information(this, QString::fromStdString(title), QString::fromStdString(message));
#else
    std::cout << message << std::endl;
#endif
}

bool IsPrereleaseBuild() {
    return ((strstr(Common::g_build_fullname, "alpha") != NULL) ||
            (strstr(Common::g_build_fullname, "beta") != NULL) ||
            (strstr(Common::g_build_fullname, "rc") != NULL));
}

#ifdef ENABLE_QT_UPDATE_CHECKER
static bool ShouldCheckForPrereleaseUpdates() {
    const bool update_channel = UISettings::values.update_check_channel.GetValue();
    const bool using_prerelease_channel =
        (update_channel == UISettings::UpdateCheckChannels::PRERELEASE);
    return (IsPrereleaseBuild() || using_prerelease_channel);
}

static int GetMajorVersion(const std::string& version) {
    size_t dot = version.find('.');
    try {
        return std::stoi(version.substr(0, dot));
    } catch (...) {
        return 0;
    }
}
#endif

// from the db in https://github.com/8bitDream/AmiiboAPI

static std::map<std::string, std::string> amiibos = {
	{"@0438000103000502", "Sandy"},
	{"@0181010100b40502", "Isabelle - Winter"},
	{"@3200000000300002", "Sonic"},
	{"@029e0001013d0502", "Ava"},
	{"@01b3000100b50502", "Blanca"},
	{"@02f8000101380502", "Mac"},
	{"@023c000100bd0502", "Lucha"},
	{"@0263000100750502", "Punchy"},
	{"@03700001015d0502", "Violet"},
	{"@07c0000000210002", "Mii Brawler"},
	{"@09c5020102830e02", "Wario - Baseball"},
	{"@026c000100c30502", "Tom"},
	{"@0101030004140902", "Zelda and Loftwing"},
	{"@04e6000100820502", "Mint"},
	{"@04e3000101650502", "Caroline"},
	{"@0188000101120502", "Mabel"},
	{"@0800010004150402", "Inkling - Yellow"},
	{"@0807000004330402", "Shiver"},
	{"@0808000004340402", "Frye"},
	{"@0809000004350402", "Big Man"},
	{"@0a1d000103d40502", "Frett"},
	{"@035d000100c90502", "Kidd"},
	{"@1f400000035e1002", "Qbby"},
	{"@09c6010102870e02", "Waluigi - Soccer"},
	{"@0264000101ac0502", "Purrl"},
	{"@025e000101250502", "Mitzi"},
	{"@0a10000103c70502", "Reneigh"},
	{"@047a000100600502", "Rasher"},
	{"@04a10001016f0502", "Chrissy"},
	{"@09d0030102bb0e02", "Metal Mario - Tennis"},
	{"@01910001004e0502", "Harriet"},
	{"@02f1000101450502", "Daisy"},
	{"@02d7000101300502", "Bam"},
	{"@02030001019a0502", "Anabelle"},
	{"@0189000100ab0502", "Labelle"},
	{"@3841000104251902", "Tatsuhisa “Luke” Kamijō"},
	{"@08060100041c0402", "Smallfry"},
	{"@0800010003820002", "Inkling"},
	{"@018d0001010c0502", "Rover"},
	{"@01a7000101140502", "Wendell"},
	{"@04ba0001005d0502", "Renée"},
	{"@0489000100ef0502", "Agnes"},
	{"@018e010101780502", "Resetti - Without Hat"},
	{"@0a04000103b50502", "Daisy Mae"},
	{"@026d0001013f0502", "Merry"},
	{"@03250001010a0502", "Big Top"},
	{"@01b4000101130502", "Leif"},
	{"@0390000101850502", "Rocco"},
	{"@09c7050102900e02", "Donkey Kong - Horse Racing"},
	{"@0437000101050502", "Gladys"},
	{"@0230000101d20502", "Twiggy"},
	{"@033b000100fa0502", "Camofrog"},
	{"@01c1000002440502", "Lottie"},
	{"@00c00000037b0002", "King K. Rool"},
	{"@3845000104291902", "Nail Saionji"},
	{"@3601000004210002", "Sephiroth"},
	{"@02da000101330502", "Deirdre"},
	{"@0a03000103b40502", "Flick"},
	{"@2102000000290002", "Lucina"},
	{"@02b80001019c0502", "Naomi"},
	{"@0347000103020502", "Raddle"},
	{"@01b0000100520502", "Tortimer"},
	{"@018c000002430502", "Digby"},
	{"@033e000101a20502", "Puddles"},
	{"@08050200038f0402", "Octoling Boy"},
	{"@0140000003550902", "Guardian"},
	{"@1907000003840002", "Squirtle"},
	{"@00020003039dff02", "Peach - Power Up Band"},
	{"@0238000102f80502", "Jacob"},
	{"@0017000002680102", "Boo"},
	{"@035e0001018e0502", "Pashmina"},
	{"@0328000102eb0502", "Paolo"},
	{"@028a000102e90502", "June"},
	{"@3740000103741402", "Super Mario Cereal"},
	{"@0100000000040002", "Link"},
	{"@3600000002590002", "Cloud"},
	{"@02710001019b0502", "Rudy"},
	{"@0384000100860502", "Flurry"},
	{"@028e0001019e0502", "Tammy"},
	{"@0214000100e40502", "Teddy"},
	{"@04510001015e0502", "Frank"},
	{"@09c2050102770e02", "Peach - Horse Racing"},
	{"@024d000102f60502", "Stu"},
	{"@03a8000100910502", "Roscoe"},
	{"@2101000000180002", "Ike"},
	{"@09c1040102710e02", "Luigi - Golf"},
	{"@0381000100d50502", "Rodney"},
	{"@09cc010102a50e02", "Baby Mario - Soccer"},
	{"@3805000103981702", "Daijobu"},
	{"@02d9000101c80502", "Bruce"},
	{"@040f000101500502", "Bree"},
	{"@04fd0001007b0502", "Bangle"},
	{"@0282000101810502", "Stitches"},
	{"@045f000101a80502", "Aurora"},
	{"@046a000101d00502", "Iggly"},
	{"@0252000100fe0502", "Vic"},
	{"@35050000040c0f02", "Razewing Ratha"},
	{"@35060000040d0f02", "Ena"},
	{"@3509000004101802", "Palico"},
	{"@35090100042b1802", "Palico"},
	{"@350b0000042d1802", "Malzeno"},
	{"@350c000004fa0f02", "Ratha"},
	{"@350d000004fb0f02", "Ratha V"},
	{"@350e000004fc0f02", "Rudy"},
	{"@3580000005062102", "Diana"},
	{"@000000030430ff02", "Golden - Power Up Band"},
	{"@044c0001008e0502", "Amelia"},
	{"@0313000101210502", "Miranda"},
	{"@01a5000101720502", "Katrina"},
	{"@0a0c000103c30502", "Audie"},
	{"@09c8050102950e02", "Diddy Kong - Horse Racing"},
	{"@0307000100640502", "Bill"},
	{"@022f0001011e0502", "Anchovy"},
	{"@0a05000103b80502", "Harvey"},
	{"@0100000003530902", "Link - Archer"},
	{"@0186010100af0502", "Tommy - Uniform"},
	{"@08050100038e0402", "Octoling Girl"},
	{"@01000000034f0902", "8-Bit Link"},
	{"@0482000102fd0502", "Maggie"},
	{"@3480000003791502", "Mega Man"},
	{"@0a00000103ab0502", "Orville"},
	{"@032e0101031c0502", "Chai"},
	{"@0a0b000103c20502", "Dom"},
	{"@01960000024e0502", "Kapp'n"},
	{"@040d000100780502", "Limberg"},
	{"@0312000103090502", "Weber"},
	{"@04940001009a0502", "Bunnie"},
	{"@00800102035d0302", "Poochy"},
	{"@09c6030102890e02", "Waluigi - Tennis"},
	{"@01a00001010f0502", "Pelly"},
	{"@033a000101cc0502", "Frobert"},
	{"@09ce050102b30e02", "Birdo - Horse Racing"},
	{"@04ea000103180502", "Tasha"},
	{"@022e000101d30502", "Robin"},
	{"@02c3000100dc0502", "Alfonso"},
	{"@023e000100d10502", "Peck"},
	{"@09cd020102ab0e02", "Baby Luigi - Baseball"},
	{"@0004000002620102", "Rosalina"},
	{"@0004010004ea0102", "Rosalina and Lumas"},
	{"@018b000002460502", "Cyrus"},
	{"@04d0000101960502", "Frita"},
	{"@046d000100f30502", "Sprinkle"},
	{"@040e000100880502", "Bella"},
	{"@02cb000101360502", "Drago"},
	{"@0199000101160502", "Grams"},
	{"@21050000025a0002", "Corrin"},
	{"@04a3000101c90502", "OHare"},
	{"@0741000000200002", "Dark Pit"},
	{"@0004010000130002", "Rosalina && Luma"},
	{"@01a80001004f0502", "Redd"},
	{"@0188000002410502", "Mabel"},
	{"@0385000101060502", "Hamphrey"},
	{"@01ab0001017c0502", "Pave"},
	{"@09c5010102820e02", "Wario - Soccer"},
	{"@02c4000100670502", "Alli"},
	{"@09c4040102800e02", "Yoshi - Golf"},
	{"@02ee000101990502", "Bones"},
	{"@05c4000004131302", "E.M.M.I."},
	{"@0100000003540902", "Link - Rider"},
	{"@04980001014a0502", "Gaston"},
	{"@03180001006c0502", "Quillson"},
	{"@09d1010102be0e02", "Pink Gold Peach - Soccer"},
	{"@0700000000070002", "Wii Fit Trainer"},
	{"@00070000001a0002", "Wario"},
	{"@22c00000003a0202", "Chibi Robo"},
	{"@00090000000d0002", "Diddy Kong"},
	{"@01ac0001017f0502", "Zipper"},
	{"@044d000101930502", "Pierce"},
	{"@3801000103941702", "Ikari"},
	{"@0100010003500902", "Toon Link - The Wind Waker"},
	{"@3640000003a20002", "Hero"},
	{"@0196000100480502", "Kapp'n"},
	{"@00010003039cff02", "Luigi - Power Up Band"},
	{"@08000100003e0402", "Inkling Girl"},
	{"@050c000101c10502", "Lobo"},
	{"@0101000003560902", "Zelda"},
	{"@3502010002e40f02", "Rathian and Cheval"},
	{"@09c60501028b0e02", "Waluigi - Horse Racing"},
	{"@0a18000103cf0502", "Quinn"},
	{"@018c0001004c0502", "Digby"},
	{"@01000000037c0002", "Young Link"},
	{"@0343000102ef0502", "Huck"},
	{"@0450000100cf0502", "Avery"},
	{"@03d3000102f30502", "Carrie"},
	{"@0261000100650502", "Kiki"},
	{"@01810000024b0502", "Isabelle - Summer Outfit"},
	{"@03da000101510502", "Rooney"},
	{"@02190001007e0502", "Nate"},
	{"@09d0010102b90e02", "Metal Mario - Soccer"},
	{"@049a0001014e0502", "Pippy"},
	{"@0003000000020002", "Yoshi"},
	{"@09ce030102b10e02", "Birdo - Tennis"},
	{"@22420000041f0002", "Mythra"},
	{"@03720001010b0502", "Rocket"},
	{"@03ac000101880502", "Peaches"},
	{"@02a6000101240502", "Ken"},
	{"@08000200003f0402", "Inkling Boy"},
	{"@0001000000350102", "Luigi"},
	{"@019a000100b70502", "Chip"},
	{"@04ff000101620502", "Claudia"},
	{"@041c000101410502", "Greta"},
	{"@2107000003611202", "Celica"},
	{"@030b000100790502", "Deena"},
	{"@1902000003830002", "Ivysaur"},
	{"@04df000100e80502", "Filbert"},
	{"@035c000101290502", "Velma"},
	{"@02210001013c0502", "Beardo"},
	{"@0a1e000103d50502", "Azalea"},
	{"@05c0000000060002", "Samus"},
	{"@05c00000043b1302", "Samus"},
	{"@05c00000043a1302", "Samus && Vi-O-La"},
	{"@05c50000043c1302", "Sylux"},
	{"@37c00000038b0002", "Simon"},
	{"@35080000040f1802", "Magnamalo"},
	{"@09cf030102b60e02", "Rosalina - Tennis"},
	{"@09cb040102a30e02", "Boo - Golf"},
	{"@03ff000100f40502", "Flip"},
	{"@04ef0001013b0502", "Hazel"},
	{"@0392000101270502", "Bubbles"},
	{"@03a7000101a10502", "Elmer"},
	{"@02c5000103080502", "Boots"},
	{"@0005ff00023a0702", "Hammer Slam Bowser"},
	{"@02f4000103050502", "Bea"},
	{"@036b0001018b0502", "Boone"},
	{"@0a08000103bd0502", "Wardell"},
	{"@0a16000103cd0502", "Petri"},
	{"@0a1b000103d20502", "Ace"},
	{"@07c0010000220002", "Mii Swordfighter"},
	{"@3842000104261902", "Gakuto Sōgetsu"},
	{"@3c80000003a40002", "Terry Bogard"},
	{"@09c30501027c0e02", "Daisy - Horse Racing"},
	{"@37800000038a0002", "Snake"},
	{"@01ae0001011b0502", "Franklin"},
	{"@1d01000003750d02", "Detective Pikachu"},
	{"@0462000100f60502", "Hopper"},
	{"@350a0100042c1802", "Palamute"},
	{"@03af0001012c0502", "Colton"},
	{"@0440000100ca0502", "Phoebe"},
	{"@021c000102f70502", "Ursala"},
	{"@02ca000101ca0502", "Gayle"},
	{"@00130000037a0002", "Daisy"},
	{"@047c000101a00502", "Lucy"},
	{"@04eb000102f00502", "Sylvana"},
	{"@0358000102fa0502", "Billy"},
	{"@021b000100800502", "Tutu"},
	{"@0a07000103bc0502", "Niko"},
	{"@021d000101cd0502", "Grizzly"},
	{"@0181000100440502", "Isabelle"},
	{"@04a2000102e80502", "Hopkins"},
	{"@0311000100d60502", "Scoot"},
	{"@03bf000101bc0502", "Sydney"},
	{"@04140001030a0502", "Candi"},
	{"@04d30101031b0502", "Étoile"},
	{"@03290001009d0502", "Axel"},
	{"@04fb000101c60502", "Rowan"},
	{"@025f000101d70502", "Rosie - Amiibo Festival"},
	{"@01940000024a0502", "Kicks"},
	{"@3500020002e20f02", "One-Eyed Rathalos and Rider - Female"},
	{"@019d000100ac0502", "Copper"},
	{"@0233000103060502", "Admiral"},
	{"@043c000101cb0502", "Cranston"},
	{"@05840000037e0002", "Wolf"},
	{"@037f000101aa0502", "Apple"},
	{"@018e000002490502", "Resetti"},
	{"@09c3010102780e02", "Daisy - Soccer"},
	{"@1919000000090002", "Pikachu"},
	{"@03fd000101580502", "Monty"},
	{"@0206000103120502", "Snooty"},
	{"@00030102023e0302", "Mega Yarn Yoshi"},
	{"@02a3000102ff0502", "Plucky"},
	{"@049c000101400502", "Genji"},
	{"@01850001004b0502", "Timmy"},
	{"@0314000102f40502", "Ketchup"},
	{"@01030000024f0902", "Midna && Wolf Link"},
	{"@00010000000c0002", "Luigi"},
	{"@0001000404401c02", "Luigi - My Mario Wooden Blocks"},
	{"@0189010103b10502", "Label"},
	{"@3dc0000004220002", "Steve"},
	{"@01840000024d0502", "Timmy && Tommy"},
	{"@21000000000b0002", "Marth"},
	{"@0195000100b00502", "Porter"},
	{"@0003000000370102", "Yoshi"},
	{"@0003000404421c02", "Yoshi - My Mario Wooden Blocks"},
	{"@030f0001016d0502", "Derwin"},
	{"@07810000002e0002", "R.O.B. - Famicom"},
	{"@3600010003620002", "Cloud - Player 2"},
	{"@02090001019f0502", "Olaf"},
	{"@04b3000100dd0502", "Rhonda"},
	{"@09c60401028a0e02", "Waluigi - Golf"},
	{"@21080000036f1202", "Chrom"},
	{"@09ce040102b20e02", "Birdo - Golf"},
	{"@09c5040102850e02", "Wario - Golf"},
	{"@0369000100d30502", "Cesar"},
	{"@3380000003781402", "Solaire of Astora"},
	{"@3844000104281902", "Roa Kirishima"},
	{"@0215000101820502", "Pinky"},
	{"@0286000103130502", "Olive"},
	{"@078f000003810002", "Ice Climbers"},
	{"@38460001042a1902", "Asana Mutsuba"},
	{"@01830101010e0502", "Tom Nook - Jacket"},
	{"@0416000100fb0502", "Anicotti"},
	{"@09c0010102690e02", "Mario - Soccer"},
	{"@03490001018d0502", "Croque"},
	{"@0105000003580902", "Daruk"},
	{"@010b000004a50902", "Tulin"},
	{"@010c000004a60902", "Yunobo"},
	{"@010a000004a40902", "Sidon"},
	{"@0109000004a30902", "Riju"},
	{"@0486000100fc0502", "Chops"},
	{"@35c0000003920a02", "Shovel Knight - Gold Edition"},
	{"@0005000003730102", "Bowser - Wedding"},
	{"@0182010100460502", "DJ KK"},
	{"@01a6000103b70502", "Saharah"},
	{"@02310001006a0502", "Jitters"},
	{"@01070000035a0902", "Mipha"},
	{"@35070000040e0f02", "Tsukino"},
	{"@0251000100c10502", "Coach"},
	{"@02df000101910502", "Erik"},
	{"@03fc000101470502", "Tammi"},
	{"@03480001006b0502", "Gigi"},
	{"@02ed0001015a0502", "Biskit"},
	{"@34c1000003890002", "Ken"},
	{"@0187000100470502", "Sable"},
	{"@0190000101710502", "Brewster"},
	{"@03c50001015c0502", "Lyman"},
	{"@0200000100a10502", "Cyrano"},
	{"@0418000100d80502", "Broccolo"},
	{"@03a9000100710502", "Winnie"},
	{"@0235000100840502", "Midge"},
	{"@04e8000101ce0502", "Cally"},
	{"@3504010002e60f02", "Qurupeco and Dan"},
	{"@0283000100c70502", "Vladimir"},
	{"@03ea0001030b0502", "Leopold"},
	{"@030a000101c70502", "Maelle"},
	{"@08010000025d0402", "Callie"},
	{"@0801000004360402", "Callie - Alterna"},
	{"@04a80101031e0502", "Toby"},
	{"@0496000100d90502", "Coco"},
	{"@0439000103110502", "Sprocket"},
	{"@01ad000100b80502", "Jack"},
	{"@3800000103931702", "Pawapuro"},
	{"@3b40000003a30002", "Banjo && Kazooie"},
	{"@03d7000101b40502", "Sylvia"},
	{"@050e000100d70502", "Whitney"},
	{"@01a80101017e0502", "Redd - Shirt"},
	{"@0a400000041d0002", "Min Min"},
	{"@09c2010102730e02", "Peach - Soccer"},
	{"@0a09000103c00502", "Sherb"},
	{"@026a000101460502", "Stinky"},
	{"@04a00001016e0502", "Francine"},
	{"@0460000100a50502", "Roald"},
	{"@09cb050102a40e02", "Boo - Horse Racing"},
	{"@02680001007d0502", "Monique"},
	{"@04fc000102ee0502", "Tybalt"},
	{"@03be000101980502", "Melba"},
	{"@09c10101026e0e02", "Luigi - Soccer"},
	{"@2108000003880002", "Chrom"},
	{"@1f01000000270002", "Meta Knight"},
	{"@1f01000004c61e03", "Meta Knight (&& Shadow Star)"},
	{"@04e1000101be0502", "Nibbles"},
	{"@08020000025e0402", "Marie"},
	{"@0802000004370402", "Marie - Alterna"},
	{"@028f0101031a0502", "Marty"},
	{"@047d0001012e0502", "Spork - Crackle"},
	{"@09cb010102a00e02", "Boo - Soccer"},
	{"@35c30000036e0a02", "King Knight"},
	{"@0800030000400402", "Inkling Squid"},
	{"@034b0001009f0502", "Henry"},
	{"@0192000002470502", "Blathers"},
	{"@01080000035b0902", "Revali"},
	{"@04cf000100e10502", "Timbra"},
	{"@03aa000100e60502", "Ed"},
	{"@0187000103b00502", "Sable"},
	{"@0453000101040502", "Keaton"},
	{"@047b000100f50502", "Hugh"},
	{"@033d0001013a0502", "Wart Jr."},
	{"@0194000100aa0502", "Kicks"},
	{"@01b1000100b20502", "Shrunk"},
	{"@03c6000100930502", "Eugene"},
	{"@04e0000100f70502", "Pecan"},
	{"@1ac0000000110002", "Lucario"},
	{"@023f000101660502", "Sparro"},
	{"@0101000003520902", "Toon Zelda - The Wind Waker"},
	{"@0181030101700502", "Isabelle - Dress"},
	{"@0266000100680502", "Kabuki"},
	{"@0326000101390502", "Eloise"},
	{"@04850001014c0502", "Gala"},
	{"@046b000101970502", "Tex"},
	{"@018a000002450502", "Reese"},
	{"@0500000100e70502", "Bianca"},
	{"@0000000000340102", "Mario"},
	{"@00000004043f1c02", "Mario - My Mario Wooden Blocks"},
	{"@07c0020000230002", "Mii Gunner"},
	{"@0002000003720102", "Peach - Wedding"},
	{"@0a17000103ce0502", "Cephalobot"},
	{"@0464000100c00502", "Gwen"},
	{"@09c70101028c0e02", "Donkey Kong - Soccer"},
	{"@0487000101bf0502", "Kevin"},
	{"@04de000100ce0502", "Blaire"},
	{"@050d000101420502", "Wolfgang"},
	{"@09c40301027f0e02", "Yoshi - Tennis"},
	{"@018e000100490502", "Resetti"},
	{"@0483000101b00502", "Peggy"},
	{"@01000000034d0902", "Link - Twilight Princess"},
	{"@036e000102fb0502", "Boyd"},
	{"@3500010002e10f02", "One-Eyed Rathalos and Rider - Male"},
	{"@0499000100df0502", "Gabi"},
	{"@024f000100810502", "T-Bone"},
	{"@09c5030102840e02", "Wario - Tennis"},
	{"@0580000000050002", "Fox"},
	{"@0800030002610402", "Inkling Squid - Orange"},
	{"@01020100041a0902", "Ganondorf - Tears of the Kingdom"},
	{"@0481000102f10502", "Boris"},
	{"@02010001016a0502", "Antonio"},
	{"@0284000102fe0502", "Murphy"},
	{"@0468000102f20502", "Wade"},
	{"@0282000101d60502", "Stitches - Amiibo Festival"},
	{"@01a40001004d0502", "Pascal"},
	{"@0a06000103ba0502", "Wisp"},
	{"@02dc000100be0502", "Fuchsia"},
	{"@1927000000260002", "Jigglypuff"},
	{"@032c000101480502", "Tucker"},
	{"@01000000034b0902", "Link - Ocarina of Time"},
	{"@09cb020102a10e02", "Boo - Baseball"},
	{"@09c2040102760e02", "Peach - Golf"},
	{"@03e70001012a0502", "Elvis"},
	{"@19ac000003850002", "Pichu"},
	{"@0803000003760402", "Pearl"},
	{"@0803000004380402", "Pearl - Side Order"},
	{"@0a14000103cb0502", "Shino"},
	{"@01000000034c0902", "Link - Majora's Mask"},
	{"@01c10101017a0502", "Lottie - Black Skirt And Bow"},
	{"@0513000102e70502", "Vivian"},
	{"@041b000100f10502", "Bettina"},
	{"@3dc1000004230002", "Alex"},
	{"@01aa000100530502", "Lyle"},
	{"@02f0000100a70502", "Walker"},
	{"@03c1000100bb0502", "Ozzie"},
	{"@0008ff00023b0702", "Turbo Charge Donkey Kong"},
	{"@0452000100730502", "Sterling"},
	{"@0181000101d40502", "Isabelle - Character Parfait"},
	{"@049f000103010502", "Claude"},
	{"@02ea000101d50502", "Goldie - Amiibo Festival"},
	{"@02d6000100560502", "Fauna"},
	{"@0270000100ff0502", "Ankha"},
	{"@01a2000103b90502", "Gulliver"},
	{"@0267000101080502", "Kid Cat"},
	{"@3843000104271902", "Romin Kirishima"},
	{"@0272000101860502", "Katt"},
	{"@00240000038d0002", "Piranha Plant"},
	{"@03e8000102f50502", "Rex"},
	{"@02dd000100ea0502", "Beau"},
	{"@01410000035c0902", "Bokoblin"},
	{"@09cf020102b50e02", "Rosalina - Baseball"},
	{"@19960000023d0002", "Mewtwo"},
	{"@0393000100a00502", "Bertha"},
	{"@1d000001025c0d02", "Shadow Mewtwo"},
	{"@04650001006e0502", "Puck"},
	{"@0106000003590902", "Urbosa"},
	{"@025f000101c50502", "Rosie"},
	{"@210b000003a50002", "Byleth"},
	{"@04b40001030c0502", "Spike"},
	{"@0100000003990902", "Link - Link's Awakening"},
	{"@041a000100e00502", "Moose"},
	{"@0000000002390602", "8-Bit Mario Modern Color"},
	{"@04800001008d0502", "Cobb"},
	{"@09c8010102910e02", "Diddy Kong - Soccer"},
	{"@02ef000100580502", "Portia"},
	{"@029a000100ee0502", "Benedict"},
	{"@0280000100830502", "Pudge"},
	{"@022d000100f20502", "Jay"},
	{"@35c0000002500a02", "Shovel Knight"},
	{"@38c0000003911602", "Loot Goblin"},
	{"@02690001011f0502", "Tabby"},
	{"@0281000101200502", "Kody"},
	{"@09ca0501029f0e02", "Bowser Jr. - Horse Racing"},
	{"@044b0001016c0502", "Apollo"},
	{"@0184050103a90502", "Timmy && Tommy"},
	{"@027e000101690502", "Maple"},
	{"@0000010000190002", "Dr. Mario"},
	{"@09cc050102a90e02", "Baby Mario - Horse Racing"},
	{"@09cd030102ac0e02", "Baby Luigi - Tennis"},
	{"@0208000100960502", "Annalisa"},
	{"@032d000100bc0502", "Tia"},
	{"@03bc0001008a0502", "Yuka"},
	{"@0183000002420502", "Tom Nook"},
	{"@0309000100c60502", "Pate"},
	{"@1f000000000a0002", "Kirby"},
	{"@04a4000100d40502", "Carmen"},
	{"@019c000101730502", "Phineas"},
	{"@03380001011d0502", "Lily"},
	{"@025d000100550502", "Bob"},
	{"@3501000002e30f02", "Nabiru"},
	{"@018b000101150502", "Cyrus"},
	{"@350a000004111802", "Palamute"},
	{"@0181040103aa0502", "Isabelle"},
	{"@33c0000004200002", "Kazuya"},
	{"@05810000001c0002", "Falco"},
	{"@03a6000100c80502", "Savannah"},
	{"@2281000002510002", "Lucas"},
	{"@043d0001007c0502", "Phil"},
	{"@019f000101110502", "Pete"},
	{"@0380000101870502", "Graham"},
	{"@3503010002e50f02", "Barioth and Ayuria"},
	{"@030e0001012f0502", "Freckles"},
	{"@0a1f000103d60502", "Roswell"},
	{"@0005000000140002", "Bowser"},
	{"@02fa000100970502", "Benjamin"},
	{"@05c0000004121302", "Samus - Metroid Dread"},
	{"@02db0001005e0502", "Lopez"},
	{"@04cc000100a40502", "Willow"},
	{"@03450001005f0502", "Jambette"},
	{"@1906000000240002", "Charizard"},
	{"@04a7000101a60502", "Mira"},
	{"@0000030003a60102", "Mario - Cat"},
	{"@01b10101017b0502", "Shrunk - Loud Jacket"},
	{"@0a0f000103c60502", "Raymond"},
	{"@0a12000103c90502", "Ione"},
	{"@0317000100a60502", "Molly"},
	{"@01a9000101760502", "Gracie"},
	{"@2109000003701202", "Tiki"},
	{"@0373000101340502", "Hans"},
	{"@019b000100b60502", "Nat"},
	{"@0186030101750502", "Tommy - Suit"},
	{"@3480000002580002", "Mega Man - Gold Edition"},
	{"@0185020101170502", "Timmy - Full Apron"},
	{"@0101000004190902", "Zelda - Tears of the Kingdom"},
	{"@026e000100ba0502", "Felicity"},
	{"@01a20001017d0502", "Gulliver"},
	{"@09c6020102880e02", "Waluigi - Baseball"},
	{"@01920001010d0502", "Blathers"},
	{"@1f00000002540c02", "Kirby"},
	{"@024b000101260502", "Rodeo"},
	{"@09cc040102a80e02", "Baby Mario - Golf"},
	{"@03d1000100c20502", "Kitt"},
	{"@0344000100c50502", "Prince"},
	{"@0182000101d80502", "K. K. Slider - Pikopuri"},
	{"@06400100001e0002", "Olimar"},
	{"@03ae000100870502", "Clyde"},
	{"@049d000100ed0502", "Ruby"},
	{"@021a000100da0502", "Groucho"},
	{"@1f02000000280002", "King Dedede"},
	{"@2104000002520002", "Roy"},
	{"@0000000003710102", "Mario - Wedding"},
	{"@0003010200430302", "Light Blue Yarn Yoshi"},
	{"@04c6000101670502", "Baabara"},
	{"@07800000002d0002", "Mr. Game && Watch"},
	{"@0600000000120002", "Captain Falcon"},
	{"@04e5000101ad0502", "Static"},
	{"@0411000101ab0502", "Rod"},
	{"@0395000102fc0502", "Bitty"},
	{"@0002010003a70102", "Peach - Cat"},
	{"@044e000103150502", "Buzz"},
	{"@0192000103ad0502", "Blathers"},
	{"@04a5000100740502", "Bonbon"},
	{"@04fa000101680502", "Rolf"},
	{"@0436000101940502", "Queenie"},
	{"@02de0001009c0502", "Diana"},
	{"@3803000103961702", "Hayakawa"},
	{"@03bd000100f90502", "Alice"},
	{"@0007000002630102", "Wario"},
	{"@021e000101230502", "Paula"},
	{"@09d0040102bc0e02", "Metal Mario - Golf"},
	{"@0232000102ea0502", "Piper"},
	{"@1f01000002550c02", "Meta Knight"},
	{"@0339000101b10502", "Ribbot"},
	{"@04b6000102ec0502", "Hornsby"},
	{"@0185040101790502", "Timmy - Suit"},
	{"@0a01000103ac0502", "Wilbur"},
	{"@032a000103070502", "Ellie"},
	{"@027d000100630502", "Bluebear"},
	{"@0006000000150002", "Bowser Jr."},
	{"@22400000002b0002", "Shulk"},
	{"@0013000002660102", "Daisy"},
	{"@09c9010102960e02", "Bowser - Soccer"},
	{"@04e4000101b60502", "Sally"},
	{"@0a11000103c80502", "Sasha"},
	{"@21030000002a0002", "Robin"},
	{"@02b1000100690502", "Patty"},
	{"@0316000101c00502", "Gloria"},
	{"@0182000100a80502", "K.K. Slider"},
	{"@03fb000101cf0502", "Simon"},
	{"@0100000004180902", "Link - Tears of the Kingdom"},
	{"@03b0000101a90502", "Papi"},
	{"@0324000101890502", "Dizzy"},
	{"@0217000101b30502", "Chow"},
	{"@02b2000100c40502", "Tipper"},
	{"@04fe000100590502", "Leonardo"},
	{"@0100010000160002", "Toon Link"},
	{"@0202000101030502", "Pango"},
	{"@0781000000330002", "R.O.B. - NES"},
	{"@09d1020102bf0e02", "Pink Gold Peach - Baseball"},
	{"@3340000000320002", "Pac-Man"},
	{"@0a15000103cc0502", "Marlo"},
	{"@09c1030102700e02", "Luigi - Tennis"},
	{"@01c1000100540502", "Lottie"},
	{"@09c2030102750e02", "Peach - Tennis"},
	{"@027f000100b90502", "Poncho"},
	{"@03ec000101830502", "Mott"},
	{"@042b000101af0502", "Zucker"},
	{"@00130003039eff02", "Daisy - Power Up Band"},
	{"@3480000000310002", "Mega Man"},
	{"@035a000100850502", "Gruff"},
	{"@05c1000003661302", "Metroid"},
	{"@030c000101b80502", "Pompom"},
	{"@0193000103ae0502", "Celeste"},
	{"@04c90001030d0502", "Cashmere"},
	{"@034a000101430502", "Diva"},
	{"@0479000100920502", "Truffles"},
	{"@042a0001012d0502", "Marina"},
	{"@03fa000100d00502", "Nana"},
	{"@09ca0301029d0e02", "Bowser Jr. - Tennis"},
	{"@0800020002600402", "Inkling Boy - Purple"},
	{"@04c7000100940502", "Eunice"},
	{"@09c90501029a0e02", "Bowser - Horse Racing"},
	{"@0008000000030002", "Donkey Kong"},
	{"@0000040004c10102", "Elephant Mario"},
	{"@000a000303a0ff02", "Toad - Power Up Band"},
	{"@000a010004c20102", "Captain Toad && Talking Flower"},
	{"@0025010004c30102", "Poplin && Prince Florian"},
	{"@03ed000101a30502", "Rory"},
	{"@029b000100cb0502", "Egbert"},
	{"@0000000000000002", "Mario"},
	{"@09cf010102b40e02", "Rosalina - Soccer"},
	{"@01000000034e0902", "Link - Skyward Sword"},
	{"@02870001005a0502", "Cheri"},
	{"@0183020103a80502", "Tom Nook"},
	{"@09c1050102720e02", "Luigi - Horse Racing"},
	{"@09c40101027d0e02", "Yoshi - Soccer"},
	{"@03d2000100e50502", "Mathilda"},
	{"@09c00301026b0e02", "Mario - Tennis"},
	{"@08000300036b0402", "Inkling Squid - Neon Purple"},
	{"@04d2000101a70502", "Pietro"},
	{"@09cc030102a70e02", "Baby Mario - Tennis"},
	{"@09c8020102920e02", "Diddy Kong - Baseball"},
	{"@0310000100f80502", "Drake"},
	{"@050f000103140502", "Dobie"},
	{"@0002000000360102", "Peach"},
	{"@09c70201028d0e02", "Donkey Kong - Baseball"},
	{"@0a1a000103d10502", "Zoe"},
	{"@0183000100450502", "Tom Nook"},
	{"@0357000100eb0502", "Nan"},
	{"@09c40201027e0e02", "Yoshi - Baseball"},
	{"@0002000000010002", "Peach"},
	{"@0002000404411c02", "Peach - My Mario Wooden Blocks"},
	{"@0014000002670102", "Waluigi"},
	{"@0182000103b20502", "K.K. Slider"},
	{"@03a50001015b0502", "Victoria"},
	{"@026b000100e90502", "Kitty"},
	{"@0415000101bb0502", "Rizzo"},
	{"@09c8040102940e02", "Diddy Kong - Golf"},
	{"@04e2000101090502", "Agent S"},
	{"@1b92000000250002", "Greninja"},
	{"@09c2020102740e02", "Peach - Baseball"},
	{"@043f000101550502", "Flora"},
	{"@04a6000100a30502", "Cole"},
	{"@0429000100700502", "Octavian"},
	{"@0023000003680102", "Koopa Troopa"},
	{"@0463000101310502", "Friga"},
	{"@0342000101280502", "Cousteau"},
	{"@018f010101190502", "Don Resetti - Without Hat"},
	{"@0a13000103ca0502", "Tiansheng"},
	{"@02a2000101ba0502", "Becky"},
	{"@0222000101440502", "Klaus"},
	{"@041e0001015f0502", "Chadder"},
	{"@0800010003690402", "Inkling Girl - Neon Pink"},
	{"@0015000003670102", "Goomba"},
	{"@3240010003640002", "Bayonetta - Player 2"},
	{"@00000000003c0102", "Mario - Gold Edition"},
	{"@05c00100001d0002", "Zero Suit Samus"},
	{"@00000003039bff02", "Mario - Power Up Band"},
	{"@0740000000100002", "Pit"},
	{"@0495000101920502", "Dotty"},
	{"@01b6000100ae0502", "Katie"},
	{"@0101010000170002", "Sheik"},
	{"@09d1050102c20e02", "Pink Gold Peach - Horse Racing"},
	{"@01af0001011c0502", "Jingle"},
	{"@026f000101900502", "Lolly"},
	{"@08000100025f0402", "Inkling Girl - Lime Green"},
	{"@09d0050102bd0e02", "Metal Mario - Horse Racing"},
	{"@08050200041b0402", "Octoling - Blue"},
	{"@2106000003601202", "Alm"},
	{"@37c10000038c0002", "Richter"},
	{"@033f0001008f0502", "Jeremiah"},
	{"@09c9040102990e02", "Bowser - Golf"},
	{"@019e000100ad0502", "Booker"},
	{"@02ec000101c40502", "Lucky"},
	{"@0182000002400502", "K. K. Slider"},
	{"@01b5000100510502", "Luna"},
	{"@049e000101b70502", "Doc"},
	{"@0454000101ae0502", "Celia"},
	{"@0399000101c20502", "Hippeux"},
	{"@09cd010102aa0e02", "Baby Luigi - Soccer"},
	{"@04ec000100770502", "Poppy"},
	{"@04b2000101b90502", "Tank"},
	{"@01c1020103bb0502", "Lottie - Island"},
	{"@02f9000101020502", "Marcel"},
	{"@018a000100a90502", "Reese"},
	{"@06420000035f1102", "Pikmin"},
	{"@03a40001014f0502", "Buck"},
	{"@04e7000101320502", "Ricky"},
	{"@0461000101610502", "Cube"},
	{"@3840000104241902", "Yuga Ohdo"},
	{"@09ce010102af0e02", "Birdo - Soccer"},
	{"@09c30301027a0e02", "Daisy - Tennis"},
	{"@03d6000101570502", "Astrid"},
	{"@01810100023f0502", "Isabelle - Winter Outfit"},
	{"@0193000002480502", "Celeste"},
	{"@0003010200420302", "Pink Yarn Yoshi"},
	{"@3804000103971702", "Ganda"},
	{"@09c3020102790e02", "Daisy - Baseball"},
	{"@09c9030102980e02", "Bowser - Tennis"},
	{"@0469000101640502", "Boomer"},
	{"@0009000002650102", "Diddy Kong"},
	{"@000900030432ff02", "Diddy Kong - Power Up Band"},
	{"@03c40001012b0502", "Canberra"},
	{"@09d1040102c10e02", "Pink Gold Peach - Golf"},
	{"@050b000100990502", "Chief"},
	{"@08000200036a0402", "Inkling Boy - Neon Green"},
	{"@0394000100890502", "Biff"},
	{"@06c00000000f0002", "Little Mac"},
	{"@03b1000100f00502", "Julian"},
	{"@0265000101540502", "Moe"},
	{"@09cc020102a60e02", "Baby Mario - Baseball"},
	{"@01a1000101100502", "Phyllis"},
	{"@0a0a000103c10502", "Megan"},
	{"@0003010200410302", "Green Yarn Yoshi"},
	{"@02f3000102f90502", "Maddie"},
	{"@04ce000100db0502", "Wendy"},
	{"@04dd000100a20502", "Peanut"},
	{"@0a19000103d00502", "Chabwick"},
	{"@0198000100b10502", "Leila"},
	{"@22410000041e0002", "Pyra"},
	{"@0374010103190502", "Rilla"},
	{"@0180000000080002", "Villager"},
	{"@0188000103af0502", "Mabel"},
	{"@049b000100610502", "Tiffany"},
	{"@09ca0401029e0e02", "Bowser Jr. - Golf"},
	{"@1f03000002570c02", "Waddle Dee"},
	{"@02c7000101220502", "Del"},
	{"@02f2000100cc0502", "Cookie"},
	{"@09c70301028e0e02", "Donkey Kong - Tennis"},
	{"@01810000037d0002", "Isabelle"},
	{"@34c0000002530002", "Ryu"},
	{"@34c2000004aa1d02", "Luke"},
	{"@34c2000104ab1d02", "Luke"},
	{"@34c3000004ac1d02", "Jamie"},
	{"@34c3000104ad1d02", "Jamie"},
	{"@34cc000104b71d02", "Manon"},
	{"@34c4000004ae1d02", "Kimberly"},
	{"@34c4000104af1d02", "Kimberly"},
	{"@34cd000104b81d02", "Marisa"},
	{"@34d0000104bb1d02", "Lily"},
	{"@34ce000104b91d02", "JP"},
	{"@34c7000104b21d02", "Juri"},
	{"@34cb000104b61d02", "Dee Jay"},
	{"@34d1000104bc1d02", "Cammy"},
	{"@34c0000104a81d02", "Ryu"},
	{"@34ca000104b51d02", "E. Honda"},
	{"@34c8000104b31d02", "Blanka"},
	{"@34c6000104b11d02", "Guile"},
	{"@34c1000104a91d02", "Ken"},
	{"@34c5000104b01d02", "Chun-Li"},
	{"@34cf000104ba1d02", "Zangief"},
	{"@34c9000104b41d02", "Dhalsim"},
	{"@34d2000104bd1d02", "Rashid"},
	{"@34d3000104be1d02", "A.K.I"},
	{"@34d4000104bf1d02", "Ed"},
	{"@34d5000104c01d02", "Akuma"},
	{"@34d6000104e11d02", "M. Bison"},
	{"@3c80000104e81d02", "Terry"},
	{"@3c81000104f21d02", "Mai"},
	{"@34d8000104e31d02", "Elena"},
	{"@34d9000104e41d02", "Sagat"},
	{"@34da000104e51d02", "C. Viper"},
	{"@34db000104e61d02", "Alex"},
	{"@34dc000104e71d02", "Ingrid"},
	{"@34c2000104cd1d02", "Luke"},
	{"@34c3000104ce1d02", "Jamie"},
	{"@34cc000104d71d02", "Manon"},
	{"@34c4000104cf1d02", "Kimberly"},
	{"@34cd000104d81d02", "Marisa"},
	{"@34d0000104db1d02", "Lily"},
	{"@34ce000104d91d02", "JP"},
	{"@34c7000104d21d02", "Juri"},
	{"@34cb000104d61d02", "Dee Jay"},
	{"@34d1000104dc1d02", "Cammy"},
	{"@34c0000104cb1d02", "Ryu"},
	{"@34ca000104d51d02", "E. Honda"},
	{"@34c8000104d31d02", "Blanka"},
	{"@34c6000104d11d02", "Guile"},
	{"@34c1000104cc1d02", "Ken"},
	{"@34c5000104d01d02", "Chun-Li"},
	{"@34cf000104da1d02", "Zangief"},
	{"@34c9000104d41d02", "Dhalsim"},
	{"@34d2000104dd1d02", "Rashid"},
	{"@34d3000104de1d02", "A.K.I"},
	{"@34d4000104df1d02", "Ed"},
	{"@34d5000104e01d02", "Akuma"},
	{"@34d6000104eb1d02", "M. Bison"},
	{"@3c80000104f11d02", "Terry"},
	{"@3c81000104f31d02", "Mai"},
	{"@34d8000104ec1d02", "Elena"},
	{"@34d9000104ed1d02", "Sagat"},
	{"@34da000104ee1d02", "C. Viper"},
	{"@34db000104ef1d02", "Alex"},
	{"@34dc000104f01d02", "Ingrid"},
	{"@0398000100bf0502", "Harry"},
	{"@0401000100660502", "Deli"},
	{"@02e00101031d0502", "Chelsea"},
	{"@01020100001b0002", "Ganondorf"},
	{"@09c30401027b0e02", "Daisy - Golf"},
	{"@09d1030102c00e02", "Pink Gold Peach - Tennis"},
	{"@03e6000100ec0502", "Bud"},
	{"@3a00000003a10002", "Joker"},
	{"@03710001005c0502", "Al"},
	{"@03fe000101a40502", "Elise"},
	{"@05c3000003800002", "Dark Samus"},
	{"@03db0001006d0502", "Marcie"},
	{"@0216000100570502", "Curt"},
	{"@05150001005b0502", "Kyle"},
	{"@2105010003630002", "Corrin - Player 2"},
	{"@024a000101d10502", "Angus"},
	{"@0478000101630502", "Curly"},
	{"@00030003039fff02", "Yoshi - Power Up Band"},
	{"@09c00401026c0e02", "Mario - Golf"},
	{"@09cd040102ad0e02", "Baby Luigi - Golf"},
	{"@09c5050102860e02", "Wario - Horse Racing"},
	{"@09ca0201029c0e02", "Bowser Jr. - Baseball"},
	{"@01810201011a0502", "Isabelle - Kimono"},
	{"@033c000101000502", "Drift"},
	{"@0327000101c30502", "Margie"},
	{"@030d000101840502", "Mallary"},
	{"@09c9020102970e02", "Bowser - Baseball"},
	{"@1f02000002560c02", "King Dedede"},
	{"@09c10201026f0e02", "Luigi - Baseball"},
	{"@07820000002f0002", "Duck Hunt"},
	{"@03ab000103160502", "Cleo"},
	{"@018c010101180502", "Digby - Raincoat"},
	{"@09cf050102b80e02", "Rosalina - Horse Racing"},
	{"@0a0d000103c40502", "Cyd"},
	{"@0005000000390102", "Bowser"},
	{"@0a20000103d70502", "Faith"},
	{"@03ad000101b20502", "Annalise"},
	{"@00000000003d0102", "Mario - Silver Edition"},
	{"@1bd7000003860002", "Incineroar"},
	{"@09c4050102810e02", "Yoshi - Horse Racing"},
	{"@02c9000100cd0502", "Sly"},
	{"@0299000100950502", "Goose"},
	{"@0804000003770402", "Marina"},
	{"@0804000004390402", "Marina - Side Order"},
	{"@32400000025b0002", "Bayonetta"},
	{"@03080001014d0502", "Joey"},
	{"@1d40000003870002", "Pokemon Trainer"},
	{"@0262000101370502", "Tangy"},
	{"@0488000100980502", "Pancetti"},
	{"@05c20000037f0002", "Ridley"},
	{"@09c70401028f0e02", "Donkey Kong - Golf"},
	{"@036d000103040502", "Louie"},
	{"@040c000101590502", "Dora"},
	{"@02a50001018c0502", "Broffina"},
	{"@043e000101490502", "Blanche"},
	{"@09c00501026d0e02", "Mario - Horse Racing"},
	{"@03ee0001008b0502", "Lionel"},
	{"@023d000101b50502", "Jacques"},
	{"@04c5000101010502", "Vesta"},
	{"@0356000101350502", "Chevre"},
	{"@02fc0001018f0502", "Shep"},
	{"@35c20000036d0a02", "Specter Knight"},
	{"@0511000101950502", "Fang"},
	{"@0181050103bf0502", "Isabelle - Sweater"},
	{"@04970001007a0502", "Snake"},
	{"@0a1c000103d30502", "Rio"},
	{"@028b000100e30502", "Pekoe"},
	{"@0193000101740502", "Celeste"},
	{"@02b70001030f0502", "Norma"},
	{"@02eb000100de0502", "Butch"},
	{"@04ee0001014b0502", "Marshal"},
	{"@0805030003900402", "Octoling Octopus"},
	{"@0000000002380602", "8-Bit Mario Classic Color"},
	{"@0000050004e90102", "Mario and Luma"},
	{"@0260000100d20502", "Olivia"},
	{"@01010000000e0002", "Zelda"},
	{"@05c0000003651302", "Samus Aran"},
	{"@02d8000100e20502", "Zell"},
	{"@03830001009b0502", "Clay"},
	{"@09cb030102a20e02", "Boo - Tennis"},
	{"@02ea000101800502", "Goldie"},
	{"@3802000103951702", "Yabe"},
	{"@22800000002c0002", "Ness"},
	{"@35c10000036c0a02", "Plague Knight"},
	{"@03410001030e0502", "Tad"},
	{"@041d0001018a0502", "Penelope"},
	{"@09cf040102b70e02", "Rosalina - Golf"},
	{"@07420000001f0002", "Palutena"},
	{"@04cd000101520502", "Curlos"},
	{"@04100001007f0502", "Samson"},
	{"@01a6000100500502", "Saharah"},
	{"@03d9000101a50502", "Walt"},
	{"@043b000103030502", "Julia"},
	{"@037e000101560502", "Hamlet"},
	{"@0220000100fd0502", "Charlise"},
	{"@03c0000103100502", "Gonzo"},
	{"@0323000100760502", "Opal"},
	{"@046c0001008c0502", "Flo"},
	{"@02a4000100720502", "Knox"},
	{"@0008000002640102", "Donkey Kong"},
	{"@00080100042f1a02", "Donkey Kong && Pauline"},
	{"@000800030431ff02", "Donkey Kong - Power Up Band"},
	{"@02fb000100900502", "Cherry"},
	{"@018d0000024c0502", "Rover"},
	{"@03820001016b0502", "Soleil"},
	{"@028d000101bd0502", "Barold"},
	{"@09ce020102b00e02", "Birdo - Baseball"},
	{"@000a000000380102", "Toad"},
	{"@04000001006f0502", "Shari"},
	{"@01a30001004a0502", "Joan"},
	{"@09ca0101029b0e02", "Bowser Jr. - Soccer"},
	{"@04ed000100620502", "Sheldon"},
	{"@028c0001013e0502", "Chester"},
	{"@09cd050102ae0e02", "Baby Luigi - Horse Racing"},
	{"@021f000103170502", "Ike"},
	{"@0197000101770502", "Leilani"},
	{"@0514000101530502", "Skye"},
	{"@0a02000103b30502", "C.J."},
	{"@09d0020102ba0e02", "Metal Mario - Baseball"},
	{"@09c00201026a0e02", "Mario - Baseball"},
	{"@0194000103b60502", "Kicks"},
	{"@04c8000102ed0502", "Stella"},
	{"@0510000101070502", "Freya"},
	{"@0a0e000103c50502", "Judy"},
	{"@036a0001019d0502", "Peewee"},
	{"@0183030103be0502", "Tom Nook - Coat"},
	{"@09c8030102930e02", "Diddy Kong - Tennis"},
	{"@018f000100b30502", "Don Resetti"},
	{"@04b9000101600502", "Merengue"},
	{"@04d10001009e0502", "Muffy"},
	{"@22430000043d1b02", "Noah"},
	{"@22440000043e1b02", "Mio"},
	{"@3f000000042e0002", "Sora"},
	{"@1f00000004c41e03", "Kirby (&& Warp Star)"},
	{"@1f03010004c91e03", "Bandana Waddle Dee (&& Winged Star)"}
};

static std::map<std::string, std::string> amiibos_series = {
	{"0a", "Shovel Knight"},
	{"0c", "Kirby"},
	{"0e", "Mario Sports Superstars"},
	{"0d", "Pokemon"},
	{"0f", "Monster Hunter"},
	{"06", "8-bit Mario"},
	{"12", "Fire Emblem"},
	{"13", "Metroid"},
	{"10", "BoxBoy!"},
	{"11", "Pikmin"},
	{"05", "Animal Crossing"},
	{"04", "Splatoon"},
	{"14", "Others"},
	{"15", "Mega Man"},
	{"09", "Legend Of Zelda"},
	{"18", "Monster Hunter Rise"},
	{"19", "Yu-Gi-Oh!"},
	{"01", "Super Mario Bros."},
	{"ff", "Super Nintendo World"},
	{"00", "Super Smash Bros."},
	{"03", "Yoshi's Woolly World"},
	{"02", "Chibi-Robo!"},
	{"07", "Skylanders"},
	{"16", "Diablo"},
	{"17", "Power Pros"},
	{"1a", "Donkey Kong"},
	{"1b", "Xenoblade Chronicles 3"},
	{"1c", "My Mario Wooden Blocks"},
	{"1d", "Street Fighter 6"},
	{"1e", "Kirby Air Riders"},
	{"21", "Pragmata"}
};

static std::map<std::string, std::map<std::string, int>> amiibos_usages = {
	{"0004000000064900", { {"@0000000000000002", 1}, {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0000000002380602", 1}, {"@0000000002390602", 1}, {"@0000000003710102", 1}, {"@00000004043f1c02", 1}, {"@0000010000190002", 1}, {"@0000030003a60102", 1}, {"@0000040004c10102", 1}, {"@0000050004e90102", 1}, {"@00010000000c0002", 1}, {"@0001000000350102", 1}, {"@0001000404401c02", 1}, {"@0002000000010002", 1}, {"@0002000000360102", 1}, {"@0002000003720102", 1}, {"@0002000404411c02", 1}, {"@0002010003a70102", 1}, {"@0005000000140002", 1}, {"@0005000000390102", 1}, {"@0005000003730102", 1}, {"@0005ff00023a0702", 1}, {"@0008000000030002", 1}, {"@0008000002640102", 1}, {"@00080100042f1a02", 1}, {"@0008ff00023b0702", 1}, {"@0100000000040002", 1}, {"@01000000034b0902", 1}, {"@01000000034c0902", 1}, {"@01000000034d0902", 1}, {"@01000000034e0902", 1}, {"@01000000034f0902", 1}, {"@0100000003530902", 1}, {"@0100000003540902", 1}, {"@01000000037c0002", 1}, {"@0100000003990902", 1}, {"@0100000004180902", 1}, {"@0100010000160002", 1}, {"@0100010003500902", 1}, {"@01010000000e0002", 1}, {"@0101000003520902", 1}, {"@0101000003560902", 1}, {"@0101000004190902", 1}, {"@0101010000170002", 1}, {"@0101030004140902", 1}, {"@0580000000050002", 1}, {"@05c0000000060002", 1}, {"@05c0000003651302", 1}, {"@05c0000004121302", 1}, {"@05c00000043a1302", 1}, {"@05c00000043b1302", 1}, {"@05c00100001d0002", 1}, {"@0600000000120002", 1}, {"@06400100001e0002", 1}, {"@3340000000320002", 1} } },
	{"000400000006CC00", { {"@0000000000000002", 1}, {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0000000002380602", 1}, {"@0000000002390602", 1}, {"@0000000003710102", 1}, {"@00000004043f1c02", 1}, {"@0000010000190002", 1}, {"@0000030003a60102", 1}, {"@0000040004c10102", 1}, {"@0000050004e90102", 1}, {"@00010000000c0002", 1}, {"@0001000000350102", 1}, {"@0001000404401c02", 1}, {"@0002000000010002", 1}, {"@0002000000360102", 1}, {"@0002000003720102", 1}, {"@0002000404411c02", 1}, {"@0002010003a70102", 1}, {"@0005000000140002", 1}, {"@0005000000390102", 1}, {"@0005000003730102", 1}, {"@0005ff00023a0702", 1}, {"@0008000000030002", 1}, {"@0008000002640102", 1}, {"@00080100042f1a02", 1}, {"@0008ff00023b0702", 1}, {"@0100000000040002", 1}, {"@01000000034b0902", 1}, {"@01000000034c0902", 1}, {"@01000000034d0902", 1}, {"@01000000034e0902", 1}, {"@01000000034f0902", 1}, {"@0100000003530902", 1}, {"@0100000003540902", 1}, {"@01000000037c0002", 1}, {"@0100000003990902", 1}, {"@0100000004180902", 1}, {"@0100010000160002", 1}, {"@0100010003500902", 1}, {"@01010000000e0002", 1}, {"@0101000003520902", 1}, {"@0101000003560902", 1}, {"@0101000004190902", 1}, {"@0101010000170002", 1}, {"@0101030004140902", 1}, {"@0580000000050002", 1}, {"@05c0000000060002", 1}, {"@05c0000003651302", 1}, {"@05c0000004121302", 1}, {"@05c00000043a1302", 1}, {"@05c00000043b1302", 1}, {"@05c00100001d0002", 1}, {"@0600000000120002", 1}, {"@06400100001e0002", 1}, {"@3340000000320002", 1} } },
	{"000400000015A300", { {"@0000000000000002", 1}, {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0000000002380602", 1}, {"@0000000002390602", 1}, {"@0000000003710102", 1}, {"@00000004043f1c02", 1}, {"@0000010000190002", 1}, {"@0000030003a60102", 1}, {"@0000040004c10102", 1}, {"@0000050004e90102", 1}, {"@00010000000c0002", 1}, {"@0001000000350102", 1}, {"@0001000404401c02", 1}, {"@0002000000010002", 1}, {"@0002000000360102", 1}, {"@0002000003720102", 1}, {"@0002000404411c02", 1}, {"@0002010003a70102", 1}, {"@0005000000140002", 1}, {"@0005000000390102", 1}, {"@0005000003730102", 1}, {"@0005ff00023a0702", 1}, {"@0008000000030002", 1}, {"@0008000002640102", 1}, {"@00080100042f1a02", 1}, {"@0008ff00023b0702", 1}, {"@0100000000040002", 1}, {"@01000000034b0902", 1}, {"@01000000034c0902", 1}, {"@01000000034d0902", 1}, {"@01000000034e0902", 1}, {"@01000000034f0902", 1}, {"@0100000003530902", 1}, {"@0100000003540902", 1}, {"@01000000037c0002", 1}, {"@0100000003990902", 1}, {"@0100000004180902", 1}, {"@0100010000160002", 1}, {"@0100010003500902", 1}, {"@01010000000e0002", 1}, {"@0101000003520902", 1}, {"@0101000003560902", 1}, {"@0101000004190902", 1}, {"@0101010000170002", 1}, {"@0101030004140902", 1}, {"@0580000000050002", 1}, {"@05c0000000060002", 1}, {"@05c0000003651302", 1}, {"@05c0000004121302", 1}, {"@05c00000043a1302", 1}, {"@05c00000043b1302", 1}, {"@05c00100001d0002", 1}, {"@0600000000120002", 1}, {"@06400100001e0002", 1}, {"@3340000000320002", 1} } },
	{"000400000015C000", { {"@0000000000000002", 1}, {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0000000002380602", 1}, {"@0000000002390602", 1}, {"@0000000003710102", 1}, {"@00000004043f1c02", 1}, {"@0000010000190002", 1}, {"@0000030003a60102", 1}, {"@0000040004c10102", 1}, {"@0000050004e90102", 1}, {"@00010000000c0002", 1}, {"@0001000000350102", 1}, {"@0001000404401c02", 1}, {"@0002000000010002", 1}, {"@0002000000360102", 1}, {"@0002000003720102", 1}, {"@0002000404411c02", 1}, {"@0002010003a70102", 1}, {"@0005000000140002", 1}, {"@0005000000390102", 1}, {"@0005000003730102", 1}, {"@0005ff00023a0702", 1}, {"@0008000000030002", 1}, {"@0008000002640102", 1}, {"@00080100042f1a02", 1}, {"@0008ff00023b0702", 1}, {"@0100000000040002", 1}, {"@01000000034b0902", 1}, {"@01000000034c0902", 1}, {"@01000000034d0902", 1}, {"@01000000034e0902", 1}, {"@01000000034f0902", 1}, {"@0100000003530902", 1}, {"@0100000003540902", 1}, {"@01000000037c0002", 1}, {"@0100000003990902", 1}, {"@0100000004180902", 1}, {"@0100010000160002", 1}, {"@0100010003500902", 1}, {"@01010000000e0002", 1}, {"@0101000003520902", 1}, {"@0101000003560902", 1}, {"@0101000004190902", 1}, {"@0101010000170002", 1}, {"@0101030004140902", 1}, {"@0580000000050002", 1}, {"@05c0000000060002", 1}, {"@05c0000003651302", 1}, {"@05c0000004121302", 1}, {"@05c00000043a1302", 1}, {"@05c00000043b1302", 1}, {"@05c00100001d0002", 1}, {"@0600000000120002", 1}, {"@06400100001e0002", 1}, {"@3340000000320002", 1} } },
	{"0004000000162F00", { {"@0000000000000002", 1}, {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0000000002380602", 1}, {"@0000000002390602", 1}, {"@0000000003710102", 1}, {"@00000004043f1c02", 1}, {"@0000010000190002", 1}, {"@0000030003a60102", 1}, {"@0000040004c10102", 1}, {"@0000050004e90102", 1}, {"@00010000000c0002", 1}, {"@0001000000350102", 1}, {"@0001000404401c02", 1}, {"@0002000000010002", 1}, {"@0002000000360102", 1}, {"@0002000003720102", 1}, {"@0002000404411c02", 1}, {"@0002010003a70102", 1}, {"@0003000000020002", 1}, {"@0003000000370102", 1}, {"@0003000404421c02", 1}, {"@0003010200410302", 1}, {"@0003010200420302", 1}, {"@0003010200430302", 1}, {"@00030102023e0302", 1}, {"@0004000002620102", 1}, {"@0004010000130002", 1}, {"@0004010004ea0102", 1}, {"@0005000000140002", 1}, {"@0005000000390102", 1}, {"@0005000003730102", 1}, {"@0005ff00023a0702", 1}, {"@0006000000150002", 1}, {"@00070000001a0002", 1}, {"@0007000002630102", 1}, {"@0008000000030002", 1}, {"@0008000002640102", 1}, {"@00080100042f1a02", 1}, {"@0008ff00023b0702", 1}, {"@00090000000d0002", 1}, {"@0009000002650102", 1}, {"@000a000000380102", 1}, {"@000a010004c20102", 1}, {"@0100000000040002", 1}, {"@01000000034b0902", 1}, {"@01000000034c0902", 1}, {"@01000000034d0902", 1}, {"@01000000034e0902", 1}, {"@01000000034f0902", 1}, {"@0100000003530902", 1}, {"@0100000003540902", 1}, {"@01000000037c0002", 1}, {"@0100000003990902", 1}, {"@0100000004180902", 1}, {"@0100010000160002", 1}, {"@0100010003500902", 1}, {"@01010000000e0002", 1}, {"@0101000003520902", 1}, {"@0101000003560902", 1}, {"@0101000004190902", 1}, {"@0101010000170002", 1}, {"@0101030004140902", 1}, {"@01020100001b0002", 1}, {"@01020100041a0902", 1}, {"@0180000000080002", 1}, {"@0580000000050002", 1}, {"@05810000001c0002", 1}, {"@05c0000000060002", 1}, {"@05c0000003651302", 1}, {"@05c0000004121302", 1}, {"@05c00000043a1302", 1}, {"@05c00000043b1302", 1}, {"@05c00100001d0002", 1}, {"@0600000000120002", 1}, {"@06400100001e0002", 1}, {"@06c00000000f0002", 1}, {"@0700000000070002", 1}, {"@0740000000100002", 1}, {"@0741000000200002", 1}, {"@07420000001f0002", 1}, {"@07800000002d0002", 1}, {"@07810000002e0002", 1}, {"@0781000000330002", 1}, {"@07820000002f0002", 1}, {"@07c0000000210002", 1}, {"@07c0010000220002", 1}, {"@07c0020000230002", 1}, {"@08000100003e0402", 1}, {"@08000100025f0402", 1}, {"@0800010003690402", 1}, {"@0800010003820002", 1}, {"@0800010004150402", 1}, {"@08000200003f0402", 1}, {"@0800020002600402", 1}, {"@08000200036a0402", 1}, {"@0800030000400402", 1}, {"@0800030002610402", 1}, {"@08000300036b0402", 1}, {"@1f000000000a0002", 1}, {"@1f00000002540c02", 1}, {"@1f01000000270002", 1}, {"@1f01000002550c02", 1}, {"@1f02000000280002", 1}, {"@1f02000002560c02", 1}, {"@21000000000b0002", 1}, {"@2101000000180002", 1}, {"@2102000000290002", 1}, {"@21030000002a0002", 1}, {"@22400000002b0002", 1}, {"@22800000002c0002", 1}, {"@22c00000003a0202", 1} } },
	{"0004000000163000", { {"@0000000000000002", 1}, {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0000000002380602", 1}, {"@0000000002390602", 1}, {"@0000000003710102", 1}, {"@00000004043f1c02", 1}, {"@0000010000190002", 1}, {"@0000030003a60102", 1}, {"@0000040004c10102", 1}, {"@0000050004e90102", 1}, {"@00010000000c0002", 1}, {"@0001000000350102", 1}, {"@0001000404401c02", 1}, {"@0002000000010002", 1}, {"@0002000000360102", 1}, {"@0002000003720102", 1}, {"@0002000404411c02", 1}, {"@0002010003a70102", 1}, {"@0003000000020002", 1}, {"@0003000000370102", 1}, {"@0003000404421c02", 1}, {"@0003010200410302", 1}, {"@0003010200420302", 1}, {"@0003010200430302", 1}, {"@00030102023e0302", 1}, {"@0004000002620102", 1}, {"@0004010000130002", 1}, {"@0004010004ea0102", 1}, {"@0005000000140002", 1}, {"@0005000000390102", 1}, {"@0005000003730102", 1}, {"@0005ff00023a0702", 1}, {"@0006000000150002", 1}, {"@00070000001a0002", 1}, {"@0007000002630102", 1}, {"@0008000000030002", 1}, {"@0008000002640102", 1}, {"@00080100042f1a02", 1}, {"@0008ff00023b0702", 1}, {"@00090000000d0002", 1}, {"@0009000002650102", 1}, {"@000a000000380102", 1}, {"@000a010004c20102", 1}, {"@0100000000040002", 1}, {"@01000000034b0902", 1}, {"@01000000034c0902", 1}, {"@01000000034d0902", 1}, {"@01000000034e0902", 1}, {"@01000000034f0902", 1}, {"@0100000003530902", 1}, {"@0100000003540902", 1}, {"@01000000037c0002", 1}, {"@0100000003990902", 1}, {"@0100000004180902", 1}, {"@0100010000160002", 1}, {"@0100010003500902", 1}, {"@01010000000e0002", 1}, {"@0101000003520902", 1}, {"@0101000003560902", 1}, {"@0101000004190902", 1}, {"@0101010000170002", 1}, {"@0101030004140902", 1}, {"@01020100001b0002", 1}, {"@01020100041a0902", 1}, {"@0180000000080002", 1}, {"@0580000000050002", 1}, {"@05810000001c0002", 1}, {"@05c0000000060002", 1}, {"@05c0000003651302", 1}, {"@05c0000004121302", 1}, {"@05c00000043a1302", 1}, {"@05c00000043b1302", 1}, {"@05c00100001d0002", 1}, {"@0600000000120002", 1}, {"@06400100001e0002", 1}, {"@06c00000000f0002", 1}, {"@0700000000070002", 1}, {"@0740000000100002", 1}, {"@0741000000200002", 1}, {"@07420000001f0002", 1}, {"@07800000002d0002", 1}, {"@07810000002e0002", 1}, {"@0781000000330002", 1}, {"@07820000002f0002", 1}, {"@07c0000000210002", 1}, {"@07c0010000220002", 1}, {"@07c0020000230002", 1}, {"@08000100003e0402", 1}, {"@08000100025f0402", 1}, {"@0800010003690402", 1}, {"@0800010003820002", 1}, {"@0800010004150402", 1}, {"@08000200003f0402", 1}, {"@0800020002600402", 1}, {"@08000200036a0402", 1}, {"@0800030000400402", 1}, {"@0800030002610402", 1}, {"@08000300036b0402", 1}, {"@1f000000000a0002", 1}, {"@1f00000002540c02", 1}, {"@1f01000000270002", 1}, {"@1f01000002550c02", 1}, {"@1f02000000280002", 1}, {"@1f02000002560c02", 1}, {"@21000000000b0002", 1}, {"@2101000000180002", 1}, {"@2102000000290002", 1}, {"@21030000002a0002", 1}, {"@22400000002b0002", 1}, {"@22800000002c0002", 1}, {"@22c00000003a0202", 1} } },
	{"0004000000183600", { {"@0000000000000002", 1}, {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0000000002380602", 1}, {"@0000000002390602", 1}, {"@0000000003710102", 1}, {"@00000004043f1c02", 1}, {"@0000010000190002", 1}, {"@0000030003a60102", 1}, {"@0000040004c10102", 1}, {"@0000050004e90102", 1}, {"@00010000000c0002", 1}, {"@0001000000350102", 1}, {"@0001000404401c02", 1}, {"@0002000000010002", 1}, {"@0002000000360102", 1}, {"@0002000003720102", 1}, {"@0002000404411c02", 1}, {"@0002010003a70102", 1}, {"@0003000000020002", 1}, {"@0003000000370102", 1}, {"@0003000404421c02", 1}, {"@0003010200410302", 1}, {"@0003010200420302", 1}, {"@0003010200430302", 1}, {"@00030102023e0302", 1}, {"@0004000002620102", 1}, {"@0004010000130002", 1}, {"@0004010004ea0102", 1}, {"@0005000000140002", 1}, {"@0005000000390102", 1}, {"@0005000003730102", 1}, {"@0005ff00023a0702", 1}, {"@0006000000150002", 1}, {"@00070000001a0002", 1}, {"@0007000002630102", 1}, {"@0008000000030002", 1}, {"@0008000002640102", 1}, {"@00080100042f1a02", 1}, {"@0008ff00023b0702", 1}, {"@00090000000d0002", 1}, {"@0009000002650102", 1}, {"@000a000000380102", 1}, {"@000a010004c20102", 1}, {"@0100000000040002", 1}, {"@01000000034b0902", 1}, {"@01000000034c0902", 1}, {"@01000000034d0902", 1}, {"@01000000034e0902", 1}, {"@01000000034f0902", 1}, {"@0100000003530902", 1}, {"@0100000003540902", 1}, {"@01000000037c0002", 1}, {"@0100000003990902", 1}, {"@0100000004180902", 1}, {"@0100010000160002", 1}, {"@0100010003500902", 1}, {"@01010000000e0002", 1}, {"@0101000003520902", 1}, {"@0101000003560902", 1}, {"@0101000004190902", 1}, {"@0101010000170002", 1}, {"@0101030004140902", 1}, {"@01020100001b0002", 1}, {"@01020100041a0902", 1}, {"@0180000000080002", 1}, {"@01810000024b0502", 1}, {"@0181000100440502", 1}, {"@0181000101d40502", 1}, {"@01810100023f0502", 1}, {"@0181010100b40502", 1}, {"@01810201011a0502", 1}, {"@0181030101700502", 1}, {"@0181040103aa0502", 1}, {"@0181050103bf0502", 1}, {"@0182000002400502", 1}, {"@0182000100a80502", 1}, {"@0182000101d80502", 1}, {"@0182000103b20502", 1}, {"@0182010100460502", 1}, {"@0183000002420502", 1}, {"@0183000100450502", 1}, {"@01830101010e0502", 1}, {"@0183020103a80502", 1}, {"@0183030103be0502", 1}, {"@01840000024d0502", 1}, {"@0184050103a90502", 1}, {"@01850001004b0502", 1}, {"@0185020101170502", 1}, {"@0185040101790502", 1}, {"@0186010100af0502", 1}, {"@0186030101750502", 1}, {"@0187000100470502", 1}, {"@0187000103b00502", 1}, {"@0188000002410502", 1}, {"@0188000101120502", 1}, {"@0188000103af0502", 1}, {"@0189000100ab0502", 1}, {"@0189010103b10502", 1}, {"@018a000002450502", 1}, {"@018a000100a90502", 1}, {"@018b000002460502", 1}, {"@018b000101150502", 1}, {"@018c000002430502", 1}, {"@018c0001004c0502", 1}, {"@018c010101180502", 1}, {"@018d0000024c0502", 1}, {"@018d0001010c0502", 1}, {"@018e000002490502", 1}, {"@018e000100490502", 1}, {"@018e010101780502", 1}, {"@018f000100b30502", 1}, {"@018f010101190502", 1}, {"@0190000101710502", 1}, {"@01910001004e0502", 1}, {"@0192000002470502", 1}, {"@01920001010d0502", 1}, {"@0192000103ad0502", 1}, {"@0193000002480502", 1}, {"@0193000101740502", 1}, {"@0193000103ae0502", 1}, {"@01940000024a0502", 1}, {"@0194000100aa0502", 1}, {"@0194000103b60502", 1}, {"@0195000100b00502", 1}, {"@01960000024e0502", 1}, {"@0196000100480502", 1}, {"@0197000101770502", 1}, {"@0198000100b10502", 1}, {"@0199000101160502", 1}, {"@019a000100b70502", 1}, {"@019b000100b60502", 1}, {"@019c000101730502", 1}, {"@019d000100ac0502", 1}, {"@019e000100ad0502", 1}, {"@019f000101110502", 1}, {"@01a00001010f0502", 1}, {"@01a1000101100502", 1}, {"@01a20001017d0502", 1}, {"@01a2000103b90502", 1}, {"@01a30001004a0502", 1}, {"@01a40001004d0502", 1}, {"@01a5000101720502", 1}, {"@01a6000100500502", 1}, {"@01a6000103b70502", 1}, {"@01a7000101140502", 1}, {"@01a80001004f0502", 1}, {"@01a80101017e0502", 1}, {"@01a9000101760502", 1}, {"@01aa000100530502", 1}, {"@01ab0001017c0502", 1}, {"@01ac0001017f0502", 1}, {"@01ad000100b80502", 1}, {"@01ae0001011b0502", 1}, {"@01af0001011c0502", 1}, {"@01b0000100520502", 1}, {"@01b1000100b20502", 1}, {"@01b10101017b0502", 1}, {"@01b3000100b50502", 1}, {"@01b4000101130502", 1}, {"@01b5000100510502", 1}, {"@01b6000100ae0502", 1}, {"@01c1000002440502", 1}, {"@01c1000100540502", 1}, {"@01c10101017a0502", 1}, {"@01c1020103bb0502", 1}, {"@0200000100a10502", 1}, {"@02010001016a0502", 1}, {"@0202000101030502", 1}, {"@02030001019a0502", 1}, {"@0206000103120502", 1}, {"@0208000100960502", 1}, {"@02090001019f0502", 1}, {"@0214000100e40502", 1}, {"@0215000101820502", 1}, {"@0216000100570502", 1}, {"@0217000101b30502", 1}, {"@02190001007e0502", 1}, {"@021a000100da0502", 1}, {"@021b000100800502", 1}, {"@021c000102f70502", 1}, {"@021d000101cd0502", 1}, {"@021e000101230502", 1}, {"@021f000103170502", 1}, {"@0220000100fd0502", 1}, {"@02210001013c0502", 1}, {"@0222000101440502", 1}, {"@022d000100f20502", 1}, {"@022e000101d30502", 1}, {"@022f0001011e0502", 1}, {"@0230000101d20502", 1}, {"@02310001006a0502", 1}, {"@0232000102ea0502", 1}, {"@0233000103060502", 1}, {"@0235000100840502", 1}, {"@0238000102f80502", 1}, {"@023c000100bd0502", 1}, {"@023d000101b50502", 1}, {"@023e000100d10502", 1}, {"@023f000101660502", 1}, {"@024a000101d10502", 1}, {"@024b000101260502", 1}, {"@024d000102f60502", 1}, {"@024f000100810502", 1}, {"@0251000100c10502", 1}, {"@0252000100fe0502", 1}, {"@025d000100550502", 1}, {"@025e000101250502", 1}, {"@025f000101c50502", 1}, {"@025f000101d70502", 1}, {"@0260000100d20502", 1}, {"@0261000100650502", 1}, {"@0262000101370502", 1}, {"@0263000100750502", 1}, {"@0264000101ac0502", 1}, {"@0265000101540502", 1}, {"@0266000100680502", 1}, {"@0267000101080502", 1}, {"@02680001007d0502", 1}, {"@02690001011f0502", 1}, {"@026a000101460502", 1}, {"@026b000100e90502", 1}, {"@026c000100c30502", 1}, {"@026d0001013f0502", 1}, {"@026e000100ba0502", 1}, {"@026f000101900502", 1}, {"@0270000100ff0502", 1}, {"@02710001019b0502", 1}, {"@0272000101860502", 1}, {"@027d000100630502", 1}, {"@027e000101690502", 1}, {"@027f000100b90502", 1}, {"@0280000100830502", 1}, {"@0281000101200502", 1}, {"@0282000101810502", 1}, {"@0282000101d60502", 1}, {"@0283000100c70502", 1}, {"@0284000102fe0502", 1}, {"@0286000103130502", 1}, {"@02870001005a0502", 1}, {"@028a000102e90502", 1}, {"@028b000100e30502", 1}, {"@028c0001013e0502", 1}, {"@028d000101bd0502", 1}, {"@028e0001019e0502", 1}, {"@028f0101031a0502", 1}, {"@0299000100950502", 1}, {"@029a000100ee0502", 1}, {"@029b000100cb0502", 1}, {"@029e0001013d0502", 1}, {"@02a2000101ba0502", 1}, {"@02a3000102ff0502", 1}, {"@02a4000100720502", 1}, {"@02a50001018c0502", 1}, {"@02a6000101240502", 1}, {"@02b1000100690502", 1}, {"@02b2000100c40502", 1}, {"@02b70001030f0502", 1}, {"@02b80001019c0502", 1}, {"@02c3000100dc0502", 1}, {"@02c4000100670502", 1}, {"@02c5000103080502", 1}, {"@02c7000101220502", 1}, {"@02c9000100cd0502", 1}, {"@02ca000101ca0502", 1}, {"@02cb000101360502", 1}, {"@02d6000100560502", 1}, {"@02d7000101300502", 1}, {"@02d8000100e20502", 1}, {"@02d9000101c80502", 1}, {"@02da000101330502", 1}, {"@02db0001005e0502", 1}, {"@02dc000100be0502", 1}, {"@02dd000100ea0502", 1}, {"@02de0001009c0502", 1}, {"@02df000101910502", 1}, {"@02e00101031d0502", 1}, {"@02ea000101800502", 1}, {"@02ea000101d50502", 1}, {"@02eb000100de0502", 1}, {"@02ec000101c40502", 1}, {"@02ed0001015a0502", 1}, {"@02ee000101990502", 1}, {"@02ef000100580502", 1}, {"@02f0000100a70502", 1}, {"@02f1000101450502", 1}, {"@02f2000100cc0502", 1}, {"@02f3000102f90502", 1}, {"@02f4000103050502", 1}, {"@02f8000101380502", 1}, {"@02f9000101020502", 1}, {"@02fa000100970502", 1}, {"@02fb000100900502", 1}, {"@02fc0001018f0502", 1}, {"@0307000100640502", 1}, {"@03080001014d0502", 1}, {"@0309000100c60502", 1}, {"@030a000101c70502", 1}, {"@030b000100790502", 1}, {"@030c000101b80502", 1}, {"@030d000101840502", 1}, {"@030e0001012f0502", 1}, {"@030f0001016d0502", 1}, {"@0310000100f80502", 1}, {"@0311000100d60502", 1}, {"@0312000103090502", 1}, {"@0313000101210502", 1}, {"@0314000102f40502", 1}, {"@0316000101c00502", 1}, {"@0317000100a60502", 1}, {"@03180001006c0502", 1}, {"@0323000100760502", 1}, {"@0324000101890502", 1}, {"@03250001010a0502", 1}, {"@0326000101390502", 1}, {"@0327000101c30502", 1}, {"@0328000102eb0502", 1}, {"@03290001009d0502", 1}, {"@032a000103070502", 1}, {"@032c000101480502", 1}, {"@032d000100bc0502", 1}, {"@032e0101031c0502", 1}, {"@03380001011d0502", 1}, {"@0339000101b10502", 1}, {"@033a000101cc0502", 1}, {"@033b000100fa0502", 1}, {"@033c000101000502", 1}, {"@033d0001013a0502", 1}, {"@033e000101a20502", 1}, {"@033f0001008f0502", 1}, {"@03410001030e0502", 1}, {"@0342000101280502", 1}, {"@0343000102ef0502", 1}, {"@0344000100c50502", 1}, {"@03450001005f0502", 1}, {"@0347000103020502", 1}, {"@03480001006b0502", 1}, {"@03490001018d0502", 1}, {"@034a000101430502", 1}, {"@034b0001009f0502", 1}, {"@0356000101350502", 1}, {"@0357000100eb0502", 1}, {"@0358000102fa0502", 1}, {"@035a000100850502", 1}, {"@035c000101290502", 1}, {"@035d000100c90502", 1}, {"@035e0001018e0502", 1}, {"@0369000100d30502", 1}, {"@036a0001019d0502", 1}, {"@036b0001018b0502", 1}, {"@036d000103040502", 1}, {"@036e000102fb0502", 1}, {"@03700001015d0502", 1}, {"@03710001005c0502", 1}, {"@03720001010b0502", 1}, {"@0373000101340502", 1}, {"@0374010103190502", 1}, {"@037e000101560502", 1}, {"@037f000101aa0502", 1}, {"@0380000101870502", 1}, {"@0381000100d50502", 1}, {"@03820001016b0502", 1}, {"@03830001009b0502", 1}, {"@0384000100860502", 1}, {"@0385000101060502", 1}, {"@0390000101850502", 1}, {"@0392000101270502", 1}, {"@0393000100a00502", 1}, {"@0394000100890502", 1}, {"@0395000102fc0502", 1}, {"@0398000100bf0502", 1}, {"@0399000101c20502", 1}, {"@03a40001014f0502", 1}, {"@03a50001015b0502", 1}, {"@03a6000100c80502", 1}, {"@03a7000101a10502", 1}, {"@03a8000100910502", 1}, {"@03a9000100710502", 1}, {"@03aa000100e60502", 1}, {"@03ab000103160502", 1}, {"@03ac000101880502", 1}, {"@03ad000101b20502", 1}, {"@03ae000100870502", 1}, {"@03af0001012c0502", 1}, {"@03b0000101a90502", 1}, {"@03b1000100f00502", 1}, {"@03bc0001008a0502", 1}, {"@03bd000100f90502", 1}, {"@03be000101980502", 1}, {"@03bf000101bc0502", 1}, {"@03c0000103100502", 1}, {"@03c1000100bb0502", 1}, {"@03c40001012b0502", 1}, {"@03c50001015c0502", 1}, {"@03c6000100930502", 1}, {"@03d1000100c20502", 1}, {"@03d2000100e50502", 1}, {"@03d3000102f30502", 1}, {"@03d6000101570502", 1}, {"@03d7000101b40502", 1}, {"@03d9000101a50502", 1}, {"@03da000101510502", 1}, {"@03db0001006d0502", 1}, {"@03e6000100ec0502", 1}, {"@03e70001012a0502", 1}, {"@03e8000102f50502", 1}, {"@03ea0001030b0502", 1}, {"@03ec000101830502", 1}, {"@03ed000101a30502", 1}, {"@03ee0001008b0502", 1}, {"@03fa000100d00502", 1}, {"@03fb000101cf0502", 1}, {"@03fc000101470502", 1}, {"@03fd000101580502", 1}, {"@03fe000101a40502", 1}, {"@03ff000100f40502", 1}, {"@04000001006f0502", 1}, {"@0401000100660502", 1}, {"@040c000101590502", 1}, {"@040d000100780502", 1}, {"@040e000100880502", 1}, {"@040f000101500502", 1}, {"@04100001007f0502", 1}, {"@0411000101ab0502", 1}, {"@04140001030a0502", 1}, {"@0415000101bb0502", 1}, {"@0416000100fb0502", 1}, {"@0418000100d80502", 1}, {"@041a000100e00502", 1}, {"@041b000100f10502", 1}, {"@041c000101410502", 1}, {"@041d0001018a0502", 1}, {"@041e0001015f0502", 1}, {"@0429000100700502", 1}, {"@042a0001012d0502", 1}, {"@042b000101af0502", 1}, {"@0436000101940502", 1}, {"@0437000101050502", 1}, {"@0438000103000502", 1}, {"@0439000103110502", 1}, {"@043b000103030502", 1}, {"@043c000101cb0502", 1}, {"@043d0001007c0502", 1}, {"@043e000101490502", 1}, {"@043f000101550502", 1}, {"@0440000100ca0502", 1}, {"@044b0001016c0502", 1}, {"@044c0001008e0502", 1}, {"@044d000101930502", 1}, {"@044e000103150502", 1}, {"@0450000100cf0502", 1}, {"@04510001015e0502", 1}, {"@0452000100730502", 1}, {"@0453000101040502", 1}, {"@0454000101ae0502", 1}, {"@045f000101a80502", 1}, {"@0460000100a50502", 1}, {"@0461000101610502", 1}, {"@0462000100f60502", 1}, {"@0463000101310502", 1}, {"@0464000100c00502", 1}, {"@04650001006e0502", 1}, {"@0468000102f20502", 1}, {"@0469000101640502", 1}, {"@046a000101d00502", 1}, {"@046b000101970502", 1}, {"@046c0001008c0502", 1}, {"@046d000100f30502", 1}, {"@0478000101630502", 1}, {"@0479000100920502", 1}, {"@047a000100600502", 1}, {"@047b000100f50502", 1}, {"@047c000101a00502", 1}, {"@047d0001012e0502", 1}, {"@04800001008d0502", 1}, {"@0481000102f10502", 1}, {"@0482000102fd0502", 1}, {"@0483000101b00502", 1}, {"@04850001014c0502", 1}, {"@0486000100fc0502", 1}, {"@0487000101bf0502", 1}, {"@0488000100980502", 1}, {"@0489000100ef0502", 1}, {"@04940001009a0502", 1}, {"@0495000101920502", 1}, {"@0496000100d90502", 1}, {"@04970001007a0502", 1}, {"@04980001014a0502", 1}, {"@0499000100df0502", 1}, {"@049a0001014e0502", 1}, {"@049b000100610502", 1}, {"@049c000101400502", 1}, {"@049d000100ed0502", 1}, {"@049e000101b70502", 1}, {"@049f000103010502", 1}, {"@04a00001016e0502", 1}, {"@04a10001016f0502", 1}, {"@04a2000102e80502", 1}, {"@04a3000101c90502", 1}, {"@04a4000100d40502", 1}, {"@04a5000100740502", 1}, {"@04a6000100a30502", 1}, {"@04a7000101a60502", 1}, {"@04a80101031e0502", 1}, {"@04b2000101b90502", 1}, {"@04b3000100dd0502", 1}, {"@04b40001030c0502", 1}, {"@04b6000102ec0502", 1}, {"@04b9000101600502", 1}, {"@04ba0001005d0502", 1}, {"@04c5000101010502", 1}, {"@04c6000101670502", 1}, {"@04c7000100940502", 1}, {"@04c8000102ed0502", 1}, {"@04c90001030d0502", 1}, {"@04cc000100a40502", 1}, {"@04cd000101520502", 1}, {"@04ce000100db0502", 1}, {"@04cf000100e10502", 1}, {"@04d0000101960502", 1}, {"@04d10001009e0502", 1}, {"@04d2000101a70502", 1}, {"@04d30101031b0502", 1}, {"@04dd000100a20502", 1}, {"@04de000100ce0502", 1}, {"@04df000100e80502", 1}, {"@04e0000100f70502", 1}, {"@04e1000101be0502", 1}, {"@04e2000101090502", 1}, {"@04e3000101650502", 1}, {"@04e4000101b60502", 1}, {"@04e5000101ad0502", 1}, {"@04e6000100820502", 1}, {"@04e7000101320502", 1}, {"@04e8000101ce0502", 1}, {"@04ea000103180502", 1}, {"@04eb000102f00502", 1}, {"@04ec000100770502", 1}, {"@04ed000100620502", 1}, {"@04ee0001014b0502", 1}, {"@04ef0001013b0502", 1}, {"@04fa000101680502", 1}, {"@04fb000101c60502", 1}, {"@04fc000102ee0502", 1}, {"@04fd0001007b0502", 1}, {"@04fe000100590502", 1}, {"@04ff000101620502", 1}, {"@0500000100e70502", 1}, {"@050b000100990502", 1}, {"@050c000101c10502", 1}, {"@050d000101420502", 1}, {"@050e000100d70502", 1}, {"@050f000103140502", 1}, {"@0510000101070502", 1}, {"@0511000101950502", 1}, {"@0513000102e70502", 1}, {"@0514000101530502", 1}, {"@05150001005b0502", 1}, {"@0580000000050002", 1}, {"@05810000001c0002", 1}, {"@05c0000000060002", 1}, {"@05c0000003651302", 1}, {"@05c0000004121302", 1}, {"@05c00000043a1302", 1}, {"@05c00000043b1302", 1}, {"@05c00100001d0002", 1}, {"@0600000000120002", 1}, {"@06400100001e0002", 1}, {"@06c00000000f0002", 1}, {"@0700000000070002", 1}, {"@0740000000100002", 1}, {"@0741000000200002", 1}, {"@07420000001f0002", 1}, {"@07800000002d0002", 1}, {"@07810000002e0002", 1}, {"@0781000000330002", 1}, {"@07820000002f0002", 1}, {"@07c0000000210002", 1}, {"@07c0010000220002", 1}, {"@07c0020000230002", 1}, {"@08000100003e0402", 1}, {"@08000100025f0402", 1}, {"@0800010003690402", 1}, {"@0800010003820002", 1}, {"@0800010004150402", 1}, {"@08000200003f0402", 1}, {"@0800020002600402", 1}, {"@08000200036a0402", 1}, {"@0800030000400402", 1}, {"@0800030002610402", 1}, {"@08000300036b0402", 1}, {"@0a12000103c90502", 1}, {"@0a1c000103d30502", 1}, {"@1906000000240002", 1}, {"@1919000000090002", 1}, {"@1927000000260002", 1}, {"@19960000023d0002", 1}, {"@1ac0000000110002", 1}, {"@1b92000000250002", 1}, {"@1f000000000a0002", 1}, {"@1f00000002540c02", 1}, {"@1f01000000270002", 1}, {"@1f01000002550c02", 1}, {"@1f02000000280002", 1}, {"@1f02000002560c02", 1}, {"@1f03000002570c02", 1}, {"@21000000000b0002", 1}, {"@2101000000180002", 1}, {"@2102000000290002", 1}, {"@21030000002a0002", 1}, {"@2104000002520002", 1}, {"@22400000002b0002", 1}, {"@22800000002c0002", 1}, {"@2281000002510002", 1}, {"@22c00000003a0202", 1} } },
	{"0004000000189600", { {"@0000000000000002", 1}, {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0000000002380602", 1}, {"@0000000002390602", 1}, {"@0000000003710102", 1}, {"@00000004043f1c02", 1}, {"@0000010000190002", 1}, {"@0000030003a60102", 1}, {"@0000040004c10102", 1}, {"@0000050004e90102", 1}, {"@00010000000c0002", 1}, {"@0001000000350102", 1}, {"@0001000404401c02", 1}, {"@0002000000010002", 1}, {"@0002000000360102", 1}, {"@0002000003720102", 1}, {"@0002000404411c02", 1}, {"@0002010003a70102", 1}, {"@0003000000020002", 1}, {"@0003000000370102", 1}, {"@0003000404421c02", 1}, {"@0003010200410302", 1}, {"@0003010200420302", 1}, {"@0003010200430302", 1}, {"@00030102023e0302", 1}, {"@0004000002620102", 1}, {"@0004010000130002", 1}, {"@0004010004ea0102", 1}, {"@0005000000140002", 1}, {"@0005000000390102", 1}, {"@0005000003730102", 1}, {"@0005ff00023a0702", 1}, {"@0006000000150002", 1}, {"@00070000001a0002", 1}, {"@0007000002630102", 1}, {"@0008000000030002", 1}, {"@0008000002640102", 1}, {"@00080100042f1a02", 1}, {"@0008ff00023b0702", 1}, {"@00090000000d0002", 1}, {"@0009000002650102", 1}, {"@000a000000380102", 1}, {"@000a010004c20102", 1}, {"@0100000000040002", 1}, {"@01000000034b0902", 1}, {"@01000000034c0902", 1}, {"@01000000034d0902", 1}, {"@01000000034e0902", 1}, {"@01000000034f0902", 1}, {"@0100000003530902", 1}, {"@0100000003540902", 1}, {"@01000000037c0002", 1}, {"@0100000003990902", 1}, {"@0100000004180902", 1}, {"@0100010000160002", 1}, {"@0100010003500902", 1}, {"@01010000000e0002", 1}, {"@0101000003520902", 1}, {"@0101000003560902", 1}, {"@0101000004190902", 1}, {"@0101010000170002", 1}, {"@0101030004140902", 1}, {"@01020100001b0002", 1}, {"@01020100041a0902", 1}, {"@0180000000080002", 1}, {"@01810000024b0502", 1}, {"@0181000100440502", 1}, {"@0181000101d40502", 1}, {"@01810100023f0502", 1}, {"@0181010100b40502", 1}, {"@01810201011a0502", 1}, {"@0181030101700502", 1}, {"@0181040103aa0502", 1}, {"@0181050103bf0502", 1}, {"@0182000002400502", 1}, {"@0182000100a80502", 1}, {"@0182000101d80502", 1}, {"@0182000103b20502", 1}, {"@0182010100460502", 1}, {"@0183000002420502", 1}, {"@0183000100450502", 1}, {"@01830101010e0502", 1}, {"@0183020103a80502", 1}, {"@0183030103be0502", 1}, {"@01840000024d0502", 1}, {"@0184050103a90502", 1}, {"@01850001004b0502", 1}, {"@0185020101170502", 1}, {"@0185040101790502", 1}, {"@0186010100af0502", 1}, {"@0186030101750502", 1}, {"@0187000100470502", 1}, {"@0187000103b00502", 1}, {"@0188000002410502", 1}, {"@0188000101120502", 1}, {"@0188000103af0502", 1}, {"@0189000100ab0502", 1}, {"@0189010103b10502", 1}, {"@018a000002450502", 1}, {"@018a000100a90502", 1}, {"@018b000002460502", 1}, {"@018b000101150502", 1}, {"@018c000002430502", 1}, {"@018c0001004c0502", 1}, {"@018c010101180502", 1}, {"@018d0000024c0502", 1}, {"@018d0001010c0502", 1}, {"@018e000002490502", 1}, {"@018e000100490502", 1}, {"@018e010101780502", 1}, {"@018f000100b30502", 1}, {"@018f010101190502", 1}, {"@0190000101710502", 1}, {"@01910001004e0502", 1}, {"@0192000002470502", 1}, {"@01920001010d0502", 1}, {"@0192000103ad0502", 1}, {"@0193000002480502", 1}, {"@0193000101740502", 1}, {"@0193000103ae0502", 1}, {"@01940000024a0502", 1}, {"@0194000100aa0502", 1}, {"@0194000103b60502", 1}, {"@0195000100b00502", 1}, {"@01960000024e0502", 1}, {"@0196000100480502", 1}, {"@0197000101770502", 1}, {"@0198000100b10502", 1}, {"@0199000101160502", 1}, {"@019a000100b70502", 1}, {"@019b000100b60502", 1}, {"@019c000101730502", 1}, {"@019d000100ac0502", 1}, {"@019e000100ad0502", 1}, {"@019f000101110502", 1}, {"@01a00001010f0502", 1}, {"@01a1000101100502", 1}, {"@01a20001017d0502", 1}, {"@01a2000103b90502", 1}, {"@01a30001004a0502", 1}, {"@01a40001004d0502", 1}, {"@01a5000101720502", 1}, {"@01a6000100500502", 1}, {"@01a6000103b70502", 1}, {"@01a7000101140502", 1}, {"@01a80001004f0502", 1}, {"@01a80101017e0502", 1}, {"@01a9000101760502", 1}, {"@01aa000100530502", 1}, {"@01ab0001017c0502", 1}, {"@01ac0001017f0502", 1}, {"@01ad000100b80502", 1}, {"@01ae0001011b0502", 1}, {"@01af0001011c0502", 1}, {"@01b0000100520502", 1}, {"@01b1000100b20502", 1}, {"@01b10101017b0502", 1}, {"@01b3000100b50502", 1}, {"@01b4000101130502", 1}, {"@01b5000100510502", 1}, {"@01b6000100ae0502", 1}, {"@01c1000002440502", 1}, {"@01c1000100540502", 1}, {"@01c10101017a0502", 1}, {"@01c1020103bb0502", 1}, {"@0200000100a10502", 1}, {"@02010001016a0502", 1}, {"@0202000101030502", 1}, {"@02030001019a0502", 1}, {"@0206000103120502", 1}, {"@0208000100960502", 1}, {"@02090001019f0502", 1}, {"@0214000100e40502", 1}, {"@0215000101820502", 1}, {"@0216000100570502", 1}, {"@0217000101b30502", 1}, {"@02190001007e0502", 1}, {"@021a000100da0502", 1}, {"@021b000100800502", 1}, {"@021c000102f70502", 1}, {"@021d000101cd0502", 1}, {"@021e000101230502", 1}, {"@021f000103170502", 1}, {"@0220000100fd0502", 1}, {"@02210001013c0502", 1}, {"@0222000101440502", 1}, {"@022d000100f20502", 1}, {"@022e000101d30502", 1}, {"@022f0001011e0502", 1}, {"@0230000101d20502", 1}, {"@02310001006a0502", 1}, {"@0232000102ea0502", 1}, {"@0233000103060502", 1}, {"@0235000100840502", 1}, {"@0238000102f80502", 1}, {"@023c000100bd0502", 1}, {"@023d000101b50502", 1}, {"@023e000100d10502", 1}, {"@023f000101660502", 1}, {"@024a000101d10502", 1}, {"@024b000101260502", 1}, {"@024d000102f60502", 1}, {"@024f000100810502", 1}, {"@0251000100c10502", 1}, {"@0252000100fe0502", 1}, {"@025d000100550502", 1}, {"@025e000101250502", 1}, {"@025f000101c50502", 1}, {"@025f000101d70502", 1}, {"@0260000100d20502", 1}, {"@0261000100650502", 1}, {"@0262000101370502", 1}, {"@0263000100750502", 1}, {"@0264000101ac0502", 1}, {"@0265000101540502", 1}, {"@0266000100680502", 1}, {"@0267000101080502", 1}, {"@02680001007d0502", 1}, {"@02690001011f0502", 1}, {"@026a000101460502", 1}, {"@026b000100e90502", 1}, {"@026c000100c30502", 1}, {"@026d0001013f0502", 1}, {"@026e000100ba0502", 1}, {"@026f000101900502", 1}, {"@0270000100ff0502", 1}, {"@02710001019b0502", 1}, {"@0272000101860502", 1}, {"@027d000100630502", 1}, {"@027e000101690502", 1}, {"@027f000100b90502", 1}, {"@0280000100830502", 1}, {"@0281000101200502", 1}, {"@0282000101810502", 1}, {"@0282000101d60502", 1}, {"@0283000100c70502", 1}, {"@0284000102fe0502", 1}, {"@0286000103130502", 1}, {"@02870001005a0502", 1}, {"@028a000102e90502", 1}, {"@028b000100e30502", 1}, {"@028c0001013e0502", 1}, {"@028d000101bd0502", 1}, {"@028e0001019e0502", 1}, {"@028f0101031a0502", 1}, {"@0299000100950502", 1}, {"@029a000100ee0502", 1}, {"@029b000100cb0502", 1}, {"@029e0001013d0502", 1}, {"@02a2000101ba0502", 1}, {"@02a3000102ff0502", 1}, {"@02a4000100720502", 1}, {"@02a50001018c0502", 1}, {"@02a6000101240502", 1}, {"@02b1000100690502", 1}, {"@02b2000100c40502", 1}, {"@02b70001030f0502", 1}, {"@02b80001019c0502", 1}, {"@02c3000100dc0502", 1}, {"@02c4000100670502", 1}, {"@02c5000103080502", 1}, {"@02c7000101220502", 1}, {"@02c9000100cd0502", 1}, {"@02ca000101ca0502", 1}, {"@02cb000101360502", 1}, {"@02d6000100560502", 1}, {"@02d7000101300502", 1}, {"@02d8000100e20502", 1}, {"@02d9000101c80502", 1}, {"@02da000101330502", 1}, {"@02db0001005e0502", 1}, {"@02dc000100be0502", 1}, {"@02dd000100ea0502", 1}, {"@02de0001009c0502", 1}, {"@02df000101910502", 1}, {"@02e00101031d0502", 1}, {"@02ea000101800502", 1}, {"@02ea000101d50502", 1}, {"@02eb000100de0502", 1}, {"@02ec000101c40502", 1}, {"@02ed0001015a0502", 1}, {"@02ee000101990502", 1}, {"@02ef000100580502", 1}, {"@02f0000100a70502", 1}, {"@02f1000101450502", 1}, {"@02f2000100cc0502", 1}, {"@02f3000102f90502", 1}, {"@02f4000103050502", 1}, {"@02f8000101380502", 1}, {"@02f9000101020502", 1}, {"@02fa000100970502", 1}, {"@02fb000100900502", 1}, {"@02fc0001018f0502", 1}, {"@0307000100640502", 1}, {"@03080001014d0502", 1}, {"@0309000100c60502", 1}, {"@030a000101c70502", 1}, {"@030b000100790502", 1}, {"@030c000101b80502", 1}, {"@030d000101840502", 1}, {"@030e0001012f0502", 1}, {"@030f0001016d0502", 1}, {"@0310000100f80502", 1}, {"@0311000100d60502", 1}, {"@0312000103090502", 1}, {"@0313000101210502", 1}, {"@0314000102f40502", 1}, {"@0316000101c00502", 1}, {"@0317000100a60502", 1}, {"@03180001006c0502", 1}, {"@0323000100760502", 1}, {"@0324000101890502", 1}, {"@03250001010a0502", 1}, {"@0326000101390502", 1}, {"@0327000101c30502", 1}, {"@0328000102eb0502", 1}, {"@03290001009d0502", 1}, {"@032a000103070502", 1}, {"@032c000101480502", 1}, {"@032d000100bc0502", 1}, {"@032e0101031c0502", 1}, {"@03380001011d0502", 1}, {"@0339000101b10502", 1}, {"@033a000101cc0502", 1}, {"@033b000100fa0502", 1}, {"@033c000101000502", 1}, {"@033d0001013a0502", 1}, {"@033e000101a20502", 1}, {"@033f0001008f0502", 1}, {"@03410001030e0502", 1}, {"@0342000101280502", 1}, {"@0343000102ef0502", 1}, {"@0344000100c50502", 1}, {"@03450001005f0502", 1}, {"@0347000103020502", 1}, {"@03480001006b0502", 1}, {"@03490001018d0502", 1}, {"@034a000101430502", 1}, {"@034b0001009f0502", 1}, {"@0356000101350502", 1}, {"@0357000100eb0502", 1}, {"@0358000102fa0502", 1}, {"@035a000100850502", 1}, {"@035c000101290502", 1}, {"@035d000100c90502", 1}, {"@035e0001018e0502", 1}, {"@0369000100d30502", 1}, {"@036a0001019d0502", 1}, {"@036b0001018b0502", 1}, {"@036d000103040502", 1}, {"@036e000102fb0502", 1}, {"@03700001015d0502", 1}, {"@03710001005c0502", 1}, {"@03720001010b0502", 1}, {"@0373000101340502", 1}, {"@0374010103190502", 1}, {"@037e000101560502", 1}, {"@037f000101aa0502", 1}, {"@0380000101870502", 1}, {"@0381000100d50502", 1}, {"@03820001016b0502", 1}, {"@03830001009b0502", 1}, {"@0384000100860502", 1}, {"@0385000101060502", 1}, {"@0390000101850502", 1}, {"@0392000101270502", 1}, {"@0393000100a00502", 1}, {"@0394000100890502", 1}, {"@0395000102fc0502", 1}, {"@0398000100bf0502", 1}, {"@0399000101c20502", 1}, {"@03a40001014f0502", 1}, {"@03a50001015b0502", 1}, {"@03a6000100c80502", 1}, {"@03a7000101a10502", 1}, {"@03a8000100910502", 1}, {"@03a9000100710502", 1}, {"@03aa000100e60502", 1}, {"@03ab000103160502", 1}, {"@03ac000101880502", 1}, {"@03ad000101b20502", 1}, {"@03ae000100870502", 1}, {"@03af0001012c0502", 1}, {"@03b0000101a90502", 1}, {"@03b1000100f00502", 1}, {"@03bc0001008a0502", 1}, {"@03bd000100f90502", 1}, {"@03be000101980502", 1}, {"@03bf000101bc0502", 1}, {"@03c0000103100502", 1}, {"@03c1000100bb0502", 1}, {"@03c40001012b0502", 1}, {"@03c50001015c0502", 1}, {"@03c6000100930502", 1}, {"@03d1000100c20502", 1}, {"@03d2000100e50502", 1}, {"@03d3000102f30502", 1}, {"@03d6000101570502", 1}, {"@03d7000101b40502", 1}, {"@03d9000101a50502", 1}, {"@03da000101510502", 1}, {"@03db0001006d0502", 1}, {"@03e6000100ec0502", 1}, {"@03e70001012a0502", 1}, {"@03e8000102f50502", 1}, {"@03ea0001030b0502", 1}, {"@03ec000101830502", 1}, {"@03ed000101a30502", 1}, {"@03ee0001008b0502", 1}, {"@03fa000100d00502", 1}, {"@03fb000101cf0502", 1}, {"@03fc000101470502", 1}, {"@03fd000101580502", 1}, {"@03fe000101a40502", 1}, {"@03ff000100f40502", 1}, {"@04000001006f0502", 1}, {"@0401000100660502", 1}, {"@040c000101590502", 1}, {"@040d000100780502", 1}, {"@040e000100880502", 1}, {"@040f000101500502", 1}, {"@04100001007f0502", 1}, {"@0411000101ab0502", 1}, {"@04140001030a0502", 1}, {"@0415000101bb0502", 1}, {"@0416000100fb0502", 1}, {"@0418000100d80502", 1}, {"@041a000100e00502", 1}, {"@041b000100f10502", 1}, {"@041c000101410502", 1}, {"@041d0001018a0502", 1}, {"@041e0001015f0502", 1}, {"@0429000100700502", 1}, {"@042a0001012d0502", 1}, {"@042b000101af0502", 1}, {"@0436000101940502", 1}, {"@0437000101050502", 1}, {"@0438000103000502", 1}, {"@0439000103110502", 1}, {"@043b000103030502", 1}, {"@043c000101cb0502", 1}, {"@043d0001007c0502", 1}, {"@043e000101490502", 1}, {"@043f000101550502", 1}, {"@0440000100ca0502", 1}, {"@044b0001016c0502", 1}, {"@044c0001008e0502", 1}, {"@044d000101930502", 1}, {"@044e000103150502", 1}, {"@0450000100cf0502", 1}, {"@04510001015e0502", 1}, {"@0452000100730502", 1}, {"@0453000101040502", 1}, {"@0454000101ae0502", 1}, {"@045f000101a80502", 1}, {"@0460000100a50502", 1}, {"@0461000101610502", 1}, {"@0462000100f60502", 1}, {"@0463000101310502", 1}, {"@0464000100c00502", 1}, {"@04650001006e0502", 1}, {"@0468000102f20502", 1}, {"@0469000101640502", 1}, {"@046a000101d00502", 1}, {"@046b000101970502", 1}, {"@046c0001008c0502", 1}, {"@046d000100f30502", 1}, {"@0478000101630502", 1}, {"@0479000100920502", 1}, {"@047a000100600502", 1}, {"@047b000100f50502", 1}, {"@047c000101a00502", 1}, {"@047d0001012e0502", 1}, {"@04800001008d0502", 1}, {"@0481000102f10502", 1}, {"@0482000102fd0502", 1}, {"@0483000101b00502", 1}, {"@04850001014c0502", 1}, {"@0486000100fc0502", 1}, {"@0487000101bf0502", 1}, {"@0488000100980502", 1}, {"@0489000100ef0502", 1}, {"@04940001009a0502", 1}, {"@0495000101920502", 1}, {"@0496000100d90502", 1}, {"@04970001007a0502", 1}, {"@04980001014a0502", 1}, {"@0499000100df0502", 1}, {"@049a0001014e0502", 1}, {"@049b000100610502", 1}, {"@049c000101400502", 1}, {"@049d000100ed0502", 1}, {"@049e000101b70502", 1}, {"@049f000103010502", 1}, {"@04a00001016e0502", 1}, {"@04a10001016f0502", 1}, {"@04a2000102e80502", 1}, {"@04a3000101c90502", 1}, {"@04a4000100d40502", 1}, {"@04a5000100740502", 1}, {"@04a6000100a30502", 1}, {"@04a7000101a60502", 1}, {"@04a80101031e0502", 1}, {"@04b2000101b90502", 1}, {"@04b3000100dd0502", 1}, {"@04b40001030c0502", 1}, {"@04b6000102ec0502", 1}, {"@04b9000101600502", 1}, {"@04ba0001005d0502", 1}, {"@04c5000101010502", 1}, {"@04c6000101670502", 1}, {"@04c7000100940502", 1}, {"@04c8000102ed0502", 1}, {"@04c90001030d0502", 1}, {"@04cc000100a40502", 1}, {"@04cd000101520502", 1}, {"@04ce000100db0502", 1}, {"@04cf000100e10502", 1}, {"@04d0000101960502", 1}, {"@04d10001009e0502", 1}, {"@04d2000101a70502", 1}, {"@04d30101031b0502", 1}, {"@04dd000100a20502", 1}, {"@04de000100ce0502", 1}, {"@04df000100e80502", 1}, {"@04e0000100f70502", 1}, {"@04e1000101be0502", 1}, {"@04e2000101090502", 1}, {"@04e3000101650502", 1}, {"@04e4000101b60502", 1}, {"@04e5000101ad0502", 1}, {"@04e6000100820502", 1}, {"@04e7000101320502", 1}, {"@04e8000101ce0502", 1}, {"@04ea000103180502", 1}, {"@04eb000102f00502", 1}, {"@04ec000100770502", 1}, {"@04ed000100620502", 1}, {"@04ee0001014b0502", 1}, {"@04ef0001013b0502", 1}, {"@04fa000101680502", 1}, {"@04fb000101c60502", 1}, {"@04fc000102ee0502", 1}, {"@04fd0001007b0502", 1}, {"@04fe000100590502", 1}, {"@04ff000101620502", 1}, {"@0500000100e70502", 1}, {"@050b000100990502", 1}, {"@050c000101c10502", 1}, {"@050d000101420502", 1}, {"@050e000100d70502", 1}, {"@050f000103140502", 1}, {"@0510000101070502", 1}, {"@0511000101950502", 1}, {"@0513000102e70502", 1}, {"@0514000101530502", 1}, {"@05150001005b0502", 1}, {"@0580000000050002", 1}, {"@05810000001c0002", 1}, {"@05c0000000060002", 1}, {"@05c0000003651302", 1}, {"@05c0000004121302", 1}, {"@05c00000043a1302", 1}, {"@05c00000043b1302", 1}, {"@05c00100001d0002", 1}, {"@0600000000120002", 1}, {"@06400100001e0002", 1}, {"@06c00000000f0002", 1}, {"@0700000000070002", 1}, {"@0740000000100002", 1}, {"@0741000000200002", 1}, {"@07420000001f0002", 1}, {"@07800000002d0002", 1}, {"@07810000002e0002", 1}, {"@0781000000330002", 1}, {"@07820000002f0002", 1}, {"@07c0000000210002", 1}, {"@07c0010000220002", 1}, {"@07c0020000230002", 1}, {"@08000100003e0402", 1}, {"@08000100025f0402", 1}, {"@0800010003690402", 1}, {"@0800010003820002", 1}, {"@0800010004150402", 1}, {"@08000200003f0402", 1}, {"@0800020002600402", 1}, {"@08000200036a0402", 1}, {"@0800030000400402", 1}, {"@0800030002610402", 1}, {"@08000300036b0402", 1}, {"@0a12000103c90502", 1}, {"@0a1c000103d30502", 1}, {"@1906000000240002", 1}, {"@1919000000090002", 1}, {"@1927000000260002", 1}, {"@19960000023d0002", 1}, {"@1ac0000000110002", 1}, {"@1b92000000250002", 1}, {"@1f000000000a0002", 1}, {"@1f00000002540c02", 1}, {"@1f01000000270002", 1}, {"@1f01000002550c02", 1}, {"@1f02000000280002", 1}, {"@1f02000002560c02", 1}, {"@1f03000002570c02", 1}, {"@21000000000b0002", 1}, {"@2101000000180002", 1}, {"@2102000000290002", 1}, {"@21030000002a0002", 1}, {"@2104000002520002", 1}, {"@22400000002b0002", 1}, {"@22800000002c0002", 1}, {"@2281000002510002", 1}, {"@22c00000003a0202", 1} } },
	{"0004000000189800", { {"@0000000000000002", 1}, {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0000000002380602", 1}, {"@0000000002390602", 1}, {"@0000000003710102", 1}, {"@00000004043f1c02", 1}, {"@0000010000190002", 1}, {"@0000030003a60102", 1}, {"@0000040004c10102", 1}, {"@0000050004e90102", 1}, {"@00010000000c0002", 1}, {"@0001000000350102", 1}, {"@0001000404401c02", 1}, {"@0002000000010002", 1}, {"@0002000000360102", 1}, {"@0002000003720102", 1}, {"@0002000404411c02", 1}, {"@0002010003a70102", 1}, {"@0003000000020002", 1}, {"@0003000000370102", 1}, {"@0003000404421c02", 1}, {"@0003010200410302", 1}, {"@0003010200420302", 1}, {"@0003010200430302", 1}, {"@00030102023e0302", 1}, {"@0004000002620102", 1}, {"@0004010000130002", 1}, {"@0004010004ea0102", 1}, {"@0005000000140002", 1}, {"@0005000000390102", 1}, {"@0005000003730102", 1}, {"@0005ff00023a0702", 1}, {"@0006000000150002", 1}, {"@00070000001a0002", 1}, {"@0007000002630102", 1}, {"@0008000000030002", 1}, {"@0008000002640102", 1}, {"@00080100042f1a02", 1}, {"@0008ff00023b0702", 1}, {"@00090000000d0002", 1}, {"@0009000002650102", 1}, {"@000a000000380102", 1}, {"@000a010004c20102", 1}, {"@0100000000040002", 1}, {"@01000000034b0902", 1}, {"@01000000034c0902", 1}, {"@01000000034d0902", 1}, {"@01000000034e0902", 1}, {"@01000000034f0902", 1}, {"@0100000003530902", 1}, {"@0100000003540902", 1}, {"@01000000037c0002", 1}, {"@0100000003990902", 1}, {"@0100000004180902", 1}, {"@0100010000160002", 1}, {"@0100010003500902", 1}, {"@01010000000e0002", 1}, {"@0101000003520902", 1}, {"@0101000003560902", 1}, {"@0101000004190902", 1}, {"@0101010000170002", 1}, {"@0101030004140902", 1}, {"@01020100001b0002", 1}, {"@01020100041a0902", 1}, {"@0180000000080002", 1}, {"@01810000024b0502", 1}, {"@0181000100440502", 1}, {"@0181000101d40502", 1}, {"@01810100023f0502", 1}, {"@0181010100b40502", 1}, {"@01810201011a0502", 1}, {"@0181030101700502", 1}, {"@0181040103aa0502", 1}, {"@0181050103bf0502", 1}, {"@0182000002400502", 1}, {"@0182000100a80502", 1}, {"@0182000101d80502", 1}, {"@0182000103b20502", 1}, {"@0182010100460502", 1}, {"@0183000002420502", 1}, {"@0183000100450502", 1}, {"@01830101010e0502", 1}, {"@0183020103a80502", 1}, {"@0183030103be0502", 1}, {"@01840000024d0502", 1}, {"@0184050103a90502", 1}, {"@01850001004b0502", 1}, {"@0185020101170502", 1}, {"@0185040101790502", 1}, {"@0186010100af0502", 1}, {"@0186030101750502", 1}, {"@0187000100470502", 1}, {"@0187000103b00502", 1}, {"@0188000002410502", 1}, {"@0188000101120502", 1}, {"@0188000103af0502", 1}, {"@0189000100ab0502", 1}, {"@0189010103b10502", 1}, {"@018a000002450502", 1}, {"@018a000100a90502", 1}, {"@018b000002460502", 1}, {"@018b000101150502", 1}, {"@018c000002430502", 1}, {"@018c0001004c0502", 1}, {"@018c010101180502", 1}, {"@018d0000024c0502", 1}, {"@018d0001010c0502", 1}, {"@018e000002490502", 1}, {"@018e000100490502", 1}, {"@018e010101780502", 1}, {"@018f000100b30502", 1}, {"@018f010101190502", 1}, {"@0190000101710502", 1}, {"@01910001004e0502", 1}, {"@0192000002470502", 1}, {"@01920001010d0502", 1}, {"@0192000103ad0502", 1}, {"@0193000002480502", 1}, {"@0193000101740502", 1}, {"@0193000103ae0502", 1}, {"@01940000024a0502", 1}, {"@0194000100aa0502", 1}, {"@0194000103b60502", 1}, {"@0195000100b00502", 1}, {"@01960000024e0502", 1}, {"@0196000100480502", 1}, {"@0197000101770502", 1}, {"@0198000100b10502", 1}, {"@0199000101160502", 1}, {"@019a000100b70502", 1}, {"@019b000100b60502", 1}, {"@019c000101730502", 1}, {"@019d000100ac0502", 1}, {"@019e000100ad0502", 1}, {"@019f000101110502", 1}, {"@01a00001010f0502", 1}, {"@01a1000101100502", 1}, {"@01a20001017d0502", 1}, {"@01a2000103b90502", 1}, {"@01a30001004a0502", 1}, {"@01a40001004d0502", 1}, {"@01a5000101720502", 1}, {"@01a6000100500502", 1}, {"@01a6000103b70502", 1}, {"@01a7000101140502", 1}, {"@01a80001004f0502", 1}, {"@01a80101017e0502", 1}, {"@01a9000101760502", 1}, {"@01aa000100530502", 1}, {"@01ab0001017c0502", 1}, {"@01ac0001017f0502", 1}, {"@01ad000100b80502", 1}, {"@01ae0001011b0502", 1}, {"@01af0001011c0502", 1}, {"@01b0000100520502", 1}, {"@01b1000100b20502", 1}, {"@01b10101017b0502", 1}, {"@01b3000100b50502", 1}, {"@01b4000101130502", 1}, {"@01b5000100510502", 1}, {"@01b6000100ae0502", 1}, {"@01c1000002440502", 1}, {"@01c1000100540502", 1}, {"@01c10101017a0502", 1}, {"@01c1020103bb0502", 1}, {"@0200000100a10502", 1}, {"@02010001016a0502", 1}, {"@0202000101030502", 1}, {"@02030001019a0502", 1}, {"@0206000103120502", 1}, {"@0208000100960502", 1}, {"@02090001019f0502", 1}, {"@0214000100e40502", 1}, {"@0215000101820502", 1}, {"@0216000100570502", 1}, {"@0217000101b30502", 1}, {"@02190001007e0502", 1}, {"@021a000100da0502", 1}, {"@021b000100800502", 1}, {"@021c000102f70502", 1}, {"@021d000101cd0502", 1}, {"@021e000101230502", 1}, {"@021f000103170502", 1}, {"@0220000100fd0502", 1}, {"@02210001013c0502", 1}, {"@0222000101440502", 1}, {"@022d000100f20502", 1}, {"@022e000101d30502", 1}, {"@022f0001011e0502", 1}, {"@0230000101d20502", 1}, {"@02310001006a0502", 1}, {"@0232000102ea0502", 1}, {"@0233000103060502", 1}, {"@0235000100840502", 1}, {"@0238000102f80502", 1}, {"@023c000100bd0502", 1}, {"@023d000101b50502", 1}, {"@023e000100d10502", 1}, {"@023f000101660502", 1}, {"@024a000101d10502", 1}, {"@024b000101260502", 1}, {"@024d000102f60502", 1}, {"@024f000100810502", 1}, {"@0251000100c10502", 1}, {"@0252000100fe0502", 1}, {"@025d000100550502", 1}, {"@025e000101250502", 1}, {"@025f000101c50502", 1}, {"@025f000101d70502", 1}, {"@0260000100d20502", 1}, {"@0261000100650502", 1}, {"@0262000101370502", 1}, {"@0263000100750502", 1}, {"@0264000101ac0502", 1}, {"@0265000101540502", 1}, {"@0266000100680502", 1}, {"@0267000101080502", 1}, {"@02680001007d0502", 1}, {"@02690001011f0502", 1}, {"@026a000101460502", 1}, {"@026b000100e90502", 1}, {"@026c000100c30502", 1}, {"@026d0001013f0502", 1}, {"@026e000100ba0502", 1}, {"@026f000101900502", 1}, {"@0270000100ff0502", 1}, {"@02710001019b0502", 1}, {"@0272000101860502", 1}, {"@027d000100630502", 1}, {"@027e000101690502", 1}, {"@027f000100b90502", 1}, {"@0280000100830502", 1}, {"@0281000101200502", 1}, {"@0282000101810502", 1}, {"@0282000101d60502", 1}, {"@0283000100c70502", 1}, {"@0284000102fe0502", 1}, {"@0286000103130502", 1}, {"@02870001005a0502", 1}, {"@028a000102e90502", 1}, {"@028b000100e30502", 1}, {"@028c0001013e0502", 1}, {"@028d000101bd0502", 1}, {"@028e0001019e0502", 1}, {"@028f0101031a0502", 1}, {"@0299000100950502", 1}, {"@029a000100ee0502", 1}, {"@029b000100cb0502", 1}, {"@029e0001013d0502", 1}, {"@02a2000101ba0502", 1}, {"@02a3000102ff0502", 1}, {"@02a4000100720502", 1}, {"@02a50001018c0502", 1}, {"@02a6000101240502", 1}, {"@02b1000100690502", 1}, {"@02b2000100c40502", 1}, {"@02b70001030f0502", 1}, {"@02b80001019c0502", 1}, {"@02c3000100dc0502", 1}, {"@02c4000100670502", 1}, {"@02c5000103080502", 1}, {"@02c7000101220502", 1}, {"@02c9000100cd0502", 1}, {"@02ca000101ca0502", 1}, {"@02cb000101360502", 1}, {"@02d6000100560502", 1}, {"@02d7000101300502", 1}, {"@02d8000100e20502", 1}, {"@02d9000101c80502", 1}, {"@02da000101330502", 1}, {"@02db0001005e0502", 1}, {"@02dc000100be0502", 1}, {"@02dd000100ea0502", 1}, {"@02de0001009c0502", 1}, {"@02df000101910502", 1}, {"@02e00101031d0502", 1}, {"@02ea000101800502", 1}, {"@02ea000101d50502", 1}, {"@02eb000100de0502", 1}, {"@02ec000101c40502", 1}, {"@02ed0001015a0502", 1}, {"@02ee000101990502", 1}, {"@02ef000100580502", 1}, {"@02f0000100a70502", 1}, {"@02f1000101450502", 1}, {"@02f2000100cc0502", 1}, {"@02f3000102f90502", 1}, {"@02f4000103050502", 1}, {"@02f8000101380502", 1}, {"@02f9000101020502", 1}, {"@02fa000100970502", 1}, {"@02fb000100900502", 1}, {"@02fc0001018f0502", 1}, {"@0307000100640502", 1}, {"@03080001014d0502", 1}, {"@0309000100c60502", 1}, {"@030a000101c70502", 1}, {"@030b000100790502", 1}, {"@030c000101b80502", 1}, {"@030d000101840502", 1}, {"@030e0001012f0502", 1}, {"@030f0001016d0502", 1}, {"@0310000100f80502", 1}, {"@0311000100d60502", 1}, {"@0312000103090502", 1}, {"@0313000101210502", 1}, {"@0314000102f40502", 1}, {"@0316000101c00502", 1}, {"@0317000100a60502", 1}, {"@03180001006c0502", 1}, {"@0323000100760502", 1}, {"@0324000101890502", 1}, {"@03250001010a0502", 1}, {"@0326000101390502", 1}, {"@0327000101c30502", 1}, {"@0328000102eb0502", 1}, {"@03290001009d0502", 1}, {"@032a000103070502", 1}, {"@032c000101480502", 1}, {"@032d000100bc0502", 1}, {"@032e0101031c0502", 1}, {"@03380001011d0502", 1}, {"@0339000101b10502", 1}, {"@033a000101cc0502", 1}, {"@033b000100fa0502", 1}, {"@033c000101000502", 1}, {"@033d0001013a0502", 1}, {"@033e000101a20502", 1}, {"@033f0001008f0502", 1}, {"@03410001030e0502", 1}, {"@0342000101280502", 1}, {"@0343000102ef0502", 1}, {"@0344000100c50502", 1}, {"@03450001005f0502", 1}, {"@0347000103020502", 1}, {"@03480001006b0502", 1}, {"@03490001018d0502", 1}, {"@034a000101430502", 1}, {"@034b0001009f0502", 1}, {"@0356000101350502", 1}, {"@0357000100eb0502", 1}, {"@0358000102fa0502", 1}, {"@035a000100850502", 1}, {"@035c000101290502", 1}, {"@035d000100c90502", 1}, {"@035e0001018e0502", 1}, {"@0369000100d30502", 1}, {"@036a0001019d0502", 1}, {"@036b0001018b0502", 1}, {"@036d000103040502", 1}, {"@036e000102fb0502", 1}, {"@03700001015d0502", 1}, {"@03710001005c0502", 1}, {"@03720001010b0502", 1}, {"@0373000101340502", 1}, {"@0374010103190502", 1}, {"@037e000101560502", 1}, {"@037f000101aa0502", 1}, {"@0380000101870502", 1}, {"@0381000100d50502", 1}, {"@03820001016b0502", 1}, {"@03830001009b0502", 1}, {"@0384000100860502", 1}, {"@0385000101060502", 1}, {"@0390000101850502", 1}, {"@0392000101270502", 1}, {"@0393000100a00502", 1}, {"@0394000100890502", 1}, {"@0395000102fc0502", 1}, {"@0398000100bf0502", 1}, {"@0399000101c20502", 1}, {"@03a40001014f0502", 1}, {"@03a50001015b0502", 1}, {"@03a6000100c80502", 1}, {"@03a7000101a10502", 1}, {"@03a8000100910502", 1}, {"@03a9000100710502", 1}, {"@03aa000100e60502", 1}, {"@03ab000103160502", 1}, {"@03ac000101880502", 1}, {"@03ad000101b20502", 1}, {"@03ae000100870502", 1}, {"@03af0001012c0502", 1}, {"@03b0000101a90502", 1}, {"@03b1000100f00502", 1}, {"@03bc0001008a0502", 1}, {"@03bd000100f90502", 1}, {"@03be000101980502", 1}, {"@03bf000101bc0502", 1}, {"@03c0000103100502", 1}, {"@03c1000100bb0502", 1}, {"@03c40001012b0502", 1}, {"@03c50001015c0502", 1}, {"@03c6000100930502", 1}, {"@03d1000100c20502", 1}, {"@03d2000100e50502", 1}, {"@03d3000102f30502", 1}, {"@03d6000101570502", 1}, {"@03d7000101b40502", 1}, {"@03d9000101a50502", 1}, {"@03da000101510502", 1}, {"@03db0001006d0502", 1}, {"@03e6000100ec0502", 1}, {"@03e70001012a0502", 1}, {"@03e8000102f50502", 1}, {"@03ea0001030b0502", 1}, {"@03ec000101830502", 1}, {"@03ed000101a30502", 1}, {"@03ee0001008b0502", 1}, {"@03fa000100d00502", 1}, {"@03fb000101cf0502", 1}, {"@03fc000101470502", 1}, {"@03fd000101580502", 1}, {"@03fe000101a40502", 1}, {"@03ff000100f40502", 1}, {"@04000001006f0502", 1}, {"@0401000100660502", 1}, {"@040c000101590502", 1}, {"@040d000100780502", 1}, {"@040e000100880502", 1}, {"@040f000101500502", 1}, {"@04100001007f0502", 1}, {"@0411000101ab0502", 1}, {"@04140001030a0502", 1}, {"@0415000101bb0502", 1}, {"@0416000100fb0502", 1}, {"@0418000100d80502", 1}, {"@041a000100e00502", 1}, {"@041b000100f10502", 1}, {"@041c000101410502", 1}, {"@041d0001018a0502", 1}, {"@041e0001015f0502", 1}, {"@0429000100700502", 1}, {"@042a0001012d0502", 1}, {"@042b000101af0502", 1}, {"@0436000101940502", 1}, {"@0437000101050502", 1}, {"@0438000103000502", 1}, {"@0439000103110502", 1}, {"@043b000103030502", 1}, {"@043c000101cb0502", 1}, {"@043d0001007c0502", 1}, {"@043e000101490502", 1}, {"@043f000101550502", 1}, {"@0440000100ca0502", 1}, {"@044b0001016c0502", 1}, {"@044c0001008e0502", 1}, {"@044d000101930502", 1}, {"@044e000103150502", 1}, {"@0450000100cf0502", 1}, {"@04510001015e0502", 1}, {"@0452000100730502", 1}, {"@0453000101040502", 1}, {"@0454000101ae0502", 1}, {"@045f000101a80502", 1}, {"@0460000100a50502", 1}, {"@0461000101610502", 1}, {"@0462000100f60502", 1}, {"@0463000101310502", 1}, {"@0464000100c00502", 1}, {"@04650001006e0502", 1}, {"@0468000102f20502", 1}, {"@0469000101640502", 1}, {"@046a000101d00502", 1}, {"@046b000101970502", 1}, {"@046c0001008c0502", 1}, {"@046d000100f30502", 1}, {"@0478000101630502", 1}, {"@0479000100920502", 1}, {"@047a000100600502", 1}, {"@047b000100f50502", 1}, {"@047c000101a00502", 1}, {"@047d0001012e0502", 1}, {"@04800001008d0502", 1}, {"@0481000102f10502", 1}, {"@0482000102fd0502", 1}, {"@0483000101b00502", 1}, {"@04850001014c0502", 1}, {"@0486000100fc0502", 1}, {"@0487000101bf0502", 1}, {"@0488000100980502", 1}, {"@0489000100ef0502", 1}, {"@04940001009a0502", 1}, {"@0495000101920502", 1}, {"@0496000100d90502", 1}, {"@04970001007a0502", 1}, {"@04980001014a0502", 1}, {"@0499000100df0502", 1}, {"@049a0001014e0502", 1}, {"@049b000100610502", 1}, {"@049c000101400502", 1}, {"@049d000100ed0502", 1}, {"@049e000101b70502", 1}, {"@049f000103010502", 1}, {"@04a00001016e0502", 1}, {"@04a10001016f0502", 1}, {"@04a2000102e80502", 1}, {"@04a3000101c90502", 1}, {"@04a4000100d40502", 1}, {"@04a5000100740502", 1}, {"@04a6000100a30502", 1}, {"@04a7000101a60502", 1}, {"@04a80101031e0502", 1}, {"@04b2000101b90502", 1}, {"@04b3000100dd0502", 1}, {"@04b40001030c0502", 1}, {"@04b6000102ec0502", 1}, {"@04b9000101600502", 1}, {"@04ba0001005d0502", 1}, {"@04c5000101010502", 1}, {"@04c6000101670502", 1}, {"@04c7000100940502", 1}, {"@04c8000102ed0502", 1}, {"@04c90001030d0502", 1}, {"@04cc000100a40502", 1}, {"@04cd000101520502", 1}, {"@04ce000100db0502", 1}, {"@04cf000100e10502", 1}, {"@04d0000101960502", 1}, {"@04d10001009e0502", 1}, {"@04d2000101a70502", 1}, {"@04d30101031b0502", 1}, {"@04dd000100a20502", 1}, {"@04de000100ce0502", 1}, {"@04df000100e80502", 1}, {"@04e0000100f70502", 1}, {"@04e1000101be0502", 1}, {"@04e2000101090502", 1}, {"@04e3000101650502", 1}, {"@04e4000101b60502", 1}, {"@04e5000101ad0502", 1}, {"@04e6000100820502", 1}, {"@04e7000101320502", 1}, {"@04e8000101ce0502", 1}, {"@04ea000103180502", 1}, {"@04eb000102f00502", 1}, {"@04ec000100770502", 1}, {"@04ed000100620502", 1}, {"@04ee0001014b0502", 1}, {"@04ef0001013b0502", 1}, {"@04fa000101680502", 1}, {"@04fb000101c60502", 1}, {"@04fc000102ee0502", 1}, {"@04fd0001007b0502", 1}, {"@04fe000100590502", 1}, {"@04ff000101620502", 1}, {"@0500000100e70502", 1}, {"@050b000100990502", 1}, {"@050c000101c10502", 1}, {"@050d000101420502", 1}, {"@050e000100d70502", 1}, {"@050f000103140502", 1}, {"@0510000101070502", 1}, {"@0511000101950502", 1}, {"@0513000102e70502", 1}, {"@0514000101530502", 1}, {"@05150001005b0502", 1}, {"@0580000000050002", 1}, {"@05810000001c0002", 1}, {"@05c0000000060002", 1}, {"@05c0000003651302", 1}, {"@05c0000004121302", 1}, {"@05c00000043a1302", 1}, {"@05c00000043b1302", 1}, {"@05c00100001d0002", 1}, {"@0600000000120002", 1}, {"@06400100001e0002", 1}, {"@06c00000000f0002", 1}, {"@0700000000070002", 1}, {"@0740000000100002", 1}, {"@0741000000200002", 1}, {"@07420000001f0002", 1}, {"@07800000002d0002", 1}, {"@07810000002e0002", 1}, {"@0781000000330002", 1}, {"@07820000002f0002", 1}, {"@07c0000000210002", 1}, {"@07c0010000220002", 1}, {"@07c0020000230002", 1}, {"@08000100003e0402", 1}, {"@08000100025f0402", 1}, {"@0800010003690402", 1}, {"@0800010003820002", 1}, {"@0800010004150402", 1}, {"@08000200003f0402", 1}, {"@0800020002600402", 1}, {"@08000200036a0402", 1}, {"@0800030000400402", 1}, {"@0800030002610402", 1}, {"@08000300036b0402", 1}, {"@0a12000103c90502", 1}, {"@0a1c000103d30502", 1}, {"@1906000000240002", 1}, {"@1919000000090002", 1}, {"@1927000000260002", 1}, {"@19960000023d0002", 1}, {"@1ac0000000110002", 1}, {"@1b92000000250002", 1}, {"@1f000000000a0002", 1}, {"@1f00000002540c02", 1}, {"@1f01000000270002", 1}, {"@1f01000002550c02", 1}, {"@1f02000000280002", 1}, {"@1f02000002560c02", 1}, {"@1f03000002570c02", 1}, {"@21000000000b0002", 1}, {"@2101000000180002", 1}, {"@2102000000290002", 1}, {"@21030000002a0002", 1}, {"@2104000002520002", 1}, {"@22400000002b0002", 1}, {"@22800000002c0002", 1}, {"@2281000002510002", 1}, {"@22c00000003a0202", 1} } },
	{"0004000000055F00", { {"@0000000000000002", 1}, {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0000000002380602", 1}, {"@0000000002390602", 1}, {"@0000000003710102", 1}, {"@00000004043f1c02", 1}, {"@0000010000190002", 1}, {"@0000030003a60102", 1}, {"@0000040004c10102", 1}, {"@0000050004e90102", 1}, {"@00010000000c0002", 1}, {"@0001000000350102", 1}, {"@0001000404401c02", 1}, {"@000a000000380102", 1}, {"@000a010004c20102", 1}, {"@0017000002680102", 1} } },
	{"0004000000076500", { {"@0000000000000002", 1}, {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0000000002380602", 1}, {"@0000000002390602", 1}, {"@0000000003710102", 1}, {"@00000004043f1c02", 1}, {"@0000010000190002", 1}, {"@0000030003a60102", 1}, {"@0000040004c10102", 1}, {"@0000050004e90102", 1}, {"@00010000000c0002", 1}, {"@0001000000350102", 1}, {"@0001000404401c02", 1}, {"@000a000000380102", 1}, {"@000a010004c20102", 1}, {"@0017000002680102", 1} } },
	{"00040000000D0000", { {"@0000000000000002", 1}, {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0000000002380602", 1}, {"@0000000002390602", 1}, {"@0000000003710102", 1}, {"@00000004043f1c02", 1}, {"@0000010000190002", 1}, {"@0000030003a60102", 1}, {"@0000040004c10102", 1}, {"@0000050004e90102", 1}, {"@00010000000c0002", 1}, {"@0001000000350102", 1}, {"@0001000404401c02", 1}, {"@000a000000380102", 1}, {"@000a010004c20102", 1}, {"@0017000002680102", 1} } },
	{"00040000001D1900", { {"@0000000000000002", 1}, {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0000000002380602", 1}, {"@0000000002390602", 1}, {"@0000000003710102", 1}, {"@00000004043f1c02", 1}, {"@0000010000190002", 1}, {"@0000030003a60102", 1}, {"@0000040004c10102", 1}, {"@0000050004e90102", 1}, {"@00010000000c0002", 1}, {"@0001000000350102", 1}, {"@0001000404401c02", 1}, {"@000a000000380102", 1}, {"@000a010004c20102", 1}, {"@0017000002680102", 1} } },
	{"00040000001D1A00", { {"@0000000000000002", 1}, {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0000000002380602", 1}, {"@0000000002390602", 1}, {"@0000000003710102", 1}, {"@00000004043f1c02", 1}, {"@0000010000190002", 1}, {"@0000030003a60102", 1}, {"@0000040004c10102", 1}, {"@0000050004e90102", 1}, {"@00010000000c0002", 1}, {"@0001000000350102", 1}, {"@0001000404401c02", 1}, {"@000a000000380102", 1}, {"@000a010004c20102", 1}, {"@0017000002680102", 1} } },
	{"00040000001D1400", { {"@0000000000000002", 1}, {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0000000002380602", 1}, {"@0000000002390602", 1}, {"@0000000003710102", 1}, {"@00000004043f1c02", 1}, {"@0000010000190002", 1}, {"@0000030003a60102", 1}, {"@0000040004c10102", 1}, {"@0000050004e90102", 1}, {"@00010000000c0002", 1}, {"@0001000000350102", 1}, {"@0001000404401c02", 1}, {"@0002000000010002", 1}, {"@0002000000360102", 1}, {"@0002000003720102", 1}, {"@0002000404411c02", 1}, {"@0002010003a70102", 1}, {"@0005000000140002", 1}, {"@0005000000390102", 1}, {"@0005000003730102", 1}, {"@0005ff00023a0702", 1}, {"@0006000000150002", 1}, {"@000a000000380102", 1}, {"@000a010004c20102", 1}, {"@0015000003670102", 1}, {"@0017000002680102", 1}, {"@0023000003680102", 1} } },
	{"00040000001D1500", { {"@0000000000000002", 1}, {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0000000002380602", 1}, {"@0000000002390602", 1}, {"@0000000003710102", 1}, {"@00000004043f1c02", 1}, {"@0000010000190002", 1}, {"@0000030003a60102", 1}, {"@0000040004c10102", 1}, {"@0000050004e90102", 1}, {"@00010000000c0002", 1}, {"@0001000000350102", 1}, {"@0001000404401c02", 1}, {"@0002000000010002", 1}, {"@0002000000360102", 1}, {"@0002000003720102", 1}, {"@0002000404411c02", 1}, {"@0002010003a70102", 1}, {"@0005000000140002", 1}, {"@0005000000390102", 1}, {"@0005000003730102", 1}, {"@0005ff00023a0702", 1}, {"@0006000000150002", 1}, {"@000a000000380102", 1}, {"@000a010004c20102", 1}, {"@0015000003670102", 1}, {"@0017000002680102", 1}, {"@0023000003680102", 1} } },
	{"0004000000132700", { {"@0000000000000002", 1}, {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0000000002380602", 1}, {"@0000000002390602", 1}, {"@0000000003710102", 1}, {"@00000004043f1c02", 1}, {"@0000010000190002", 1}, {"@0000030003a60102", 1}, {"@0000040004c10102", 1}, {"@0000050004e90102", 1}, {"@00010000000c0002", 1}, {"@0001000000350102", 1}, {"@0001000404401c02", 1}, {"@0002000000010002", 1}, {"@0002000000360102", 1}, {"@0002000003720102", 1}, {"@0002000404411c02", 1}, {"@0002010003a70102", 1}, {"@0003000000020002", 1}, {"@0003000000370102", 1}, {"@0003000404421c02", 1}, {"@0003010200410302", 1}, {"@0003010200420302", 1}, {"@0003010200430302", 1}, {"@00030102023e0302", 1}, {"@0005000000140002", 1}, {"@0005000000390102", 1}, {"@0005000003730102", 1}, {"@0005ff00023a0702", 1}, {"@000a000000380102", 1}, {"@000a010004c20102", 1} } },
	{"0004000000132800", { {"@0000000000000002", 1}, {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0000000002380602", 1}, {"@0000000002390602", 1}, {"@0000000003710102", 1}, {"@00000004043f1c02", 1}, {"@0000010000190002", 1}, {"@0000030003a60102", 1}, {"@0000040004c10102", 1}, {"@0000050004e90102", 1}, {"@00010000000c0002", 1}, {"@0001000000350102", 1}, {"@0001000404401c02", 1}, {"@0002000000010002", 1}, {"@0002000000360102", 1}, {"@0002000003720102", 1}, {"@0002000404411c02", 1}, {"@0002010003a70102", 1}, {"@0003000000020002", 1}, {"@0003000000370102", 1}, {"@0003000404421c02", 1}, {"@0003010200410302", 1}, {"@0003010200420302", 1}, {"@0003010200430302", 1}, {"@00030102023e0302", 1}, {"@0005000000140002", 1}, {"@0005000000390102", 1}, {"@0005000003730102", 1}, {"@0005ff00023a0702", 1}, {"@000a000000380102", 1}, {"@000a010004c20102", 1} } },
	{"000400000018A100", { {"@0000000000000002", 1}, {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0000000002380602", 1}, {"@0000000002390602", 1}, {"@0000000003710102", 1}, {"@00000004043f1c02", 1}, {"@0000010000190002", 1}, {"@0000030003a60102", 1}, {"@0000040004c10102", 1}, {"@0000050004e90102", 1}, {"@00010000000c0002", 1}, {"@0001000000350102", 1}, {"@0001000404401c02", 1}, {"@0002000000010002", 1}, {"@0002000000360102", 1}, {"@0002000003720102", 1}, {"@0002000404411c02", 1}, {"@0002010003a70102", 1}, {"@0003000000020002", 1}, {"@0003000000370102", 1}, {"@0003000404421c02", 1}, {"@0003010200410302", 1}, {"@0003010200420302", 1}, {"@0003010200430302", 1}, {"@00030102023e0302", 1}, {"@0005000000140002", 1}, {"@0005000000390102", 1}, {"@0005000003730102", 1}, {"@0005ff00023a0702", 1}, {"@000a000000380102", 1}, {"@000a010004c20102", 1} } },
	{"00040000001B8F00", { {"@0000000000000002", 1}, {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0000000002380602", 1}, {"@0000000002390602", 1}, {"@0000000003710102", 1}, {"@00000004043f1c02", 1}, {"@0000010000190002", 1}, {"@0000030003a60102", 1}, {"@0000040004c10102", 1}, {"@0000050004e90102", 1}, {"@00010000000c0002", 1}, {"@0001000000350102", 1}, {"@0001000404401c02", 1}, {"@0002000000010002", 1}, {"@0002000000360102", 1}, {"@0002000003720102", 1}, {"@0002000404411c02", 1}, {"@0002010003a70102", 1}, {"@0003000000020002", 1}, {"@0003000000370102", 1}, {"@0003000404421c02", 1}, {"@0003010200410302", 1}, {"@0003010200420302", 1}, {"@0003010200430302", 1}, {"@00030102023e0302", 1}, {"@0005000000140002", 1}, {"@0005000000390102", 1}, {"@0005000003730102", 1}, {"@0005ff00023a0702", 1}, {"@0006000000150002", 1}, {"@000a000000380102", 1}, {"@000a010004c20102", 1}, {"@0015000003670102", 1}, {"@0017000002680102", 1}, {"@0023000003680102", 1} } },
	{"00040000001B9000", { {"@0000000000000002", 1}, {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0000000002380602", 1}, {"@0000000002390602", 1}, {"@0000000003710102", 1}, {"@00000004043f1c02", 1}, {"@0000010000190002", 1}, {"@0000030003a60102", 1}, {"@0000040004c10102", 1}, {"@0000050004e90102", 1}, {"@00010000000c0002", 1}, {"@0001000000350102", 1}, {"@0001000404401c02", 1}, {"@0002000000010002", 1}, {"@0002000000360102", 1}, {"@0002000003720102", 1}, {"@0002000404411c02", 1}, {"@0002010003a70102", 1}, {"@0003000000020002", 1}, {"@0003000000370102", 1}, {"@0003000404421c02", 1}, {"@0003010200410302", 1}, {"@0003010200420302", 1}, {"@0003010200430302", 1}, {"@00030102023e0302", 1}, {"@0005000000140002", 1}, {"@0005000000390102", 1}, {"@0005000003730102", 1}, {"@0005ff00023a0702", 1}, {"@0006000000150002", 1}, {"@000a000000380102", 1}, {"@000a010004c20102", 1}, {"@0015000003670102", 1}, {"@0017000002680102", 1}, {"@0023000003680102", 1} } },
	{"000400000017E200", { {"@0000000000000002", 1}, {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0000000002380602", 1}, {"@0000000002390602", 1}, {"@0000000003710102", 1}, {"@00000004043f1c02", 1}, {"@0000010000190002", 1}, {"@0000030003a60102", 1}, {"@0000040004c10102", 1}, {"@0000050004e90102", 1}, {"@3200000000300002", 1} } },
	{"000400000017E300", { {"@0000000000000002", 1}, {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0000000002380602", 1}, {"@0000000002390602", 1}, {"@0000000003710102", 1}, {"@00000004043f1c02", 1}, {"@0000010000190002", 1}, {"@0000030003a60102", 1}, {"@0000040004c10102", 1}, {"@0000050004e90102", 1}, {"@3200000000300002", 1} } },
	{"0004000000192400", { {"@0000000000000002", 1}, {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0000000002380602", 1}, {"@0000000002390602", 1}, {"@0000000003710102", 1}, {"@00000004043f1c02", 1}, {"@0000010000190002", 1}, {"@0000030003a60102", 1}, {"@0000040004c10102", 1}, {"@0000050004e90102", 1}, {"@3200000000300002", 1} } },
	{"0004000000193900", { {"@0000000000000002", 1}, {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0000000002380602", 1}, {"@0000000002390602", 1}, {"@0000000003710102", 1}, {"@00000004043f1c02", 1}, {"@0000010000190002", 1}, {"@0000010000190002", 1}, {"@0000030003a60102", 1}, {"@0000040004c10102", 1}, {"@0000050004e90102", 1}, {"@00010000000c0002", 1}, {"@0001000000350102", 1}, {"@0001000404401c02", 1}, {"@0002000000010002", 1}, {"@0002000000360102", 1}, {"@0002000003720102", 1}, {"@0002000404411c02", 1}, {"@0002010003a70102", 1}, {"@0003000000020002", 1}, {"@0003000000370102", 1}, {"@0003000404421c02", 1}, {"@0003010200410302", 1}, {"@0003010200420302", 1}, {"@0003010200430302", 1}, {"@00030102023e0302", 1}, {"@0004000002620102", 1}, {"@0004010000130002", 1}, {"@0004010004ea0102", 1}, {"@0005000000140002", 1}, {"@0005000000390102", 1}, {"@0005000003730102", 1}, {"@0005ff00023a0702", 1}, {"@0006000000150002", 1}, {"@00070000001a0002", 1}, {"@0007000002630102", 1}, {"@0008000000030002", 1}, {"@0008000002640102", 1}, {"@00080100042f1a02", 1}, {"@0008ff00023b0702", 1}, {"@00090000000d0002", 1}, {"@0009000002650102", 1}, {"@000a000000380102", 1}, {"@000a010004c20102", 1}, {"@0013000002660102", 1}, {"@00130000037a0002", 1}, {"@0014000002670102", 1}, {"@0017000002680102", 1} } },
	{"000400000019BD00", { {"@0000000000000002", 1}, {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0000000002380602", 1}, {"@0000000002390602", 1}, {"@0000000003710102", 1}, {"@00000004043f1c02", 1}, {"@0000010000190002", 1}, {"@0000010000190002", 1}, {"@0000030003a60102", 1}, {"@0000040004c10102", 1}, {"@0000050004e90102", 1}, {"@00010000000c0002", 1}, {"@0001000000350102", 1}, {"@0001000404401c02", 1}, {"@0002000000010002", 1}, {"@0002000000360102", 1}, {"@0002000003720102", 1}, {"@0002000404411c02", 1}, {"@0002010003a70102", 1}, {"@0003000000020002", 1}, {"@0003000000370102", 1}, {"@0003000404421c02", 1}, {"@0003010200410302", 1}, {"@0003010200420302", 1}, {"@0003010200430302", 1}, {"@00030102023e0302", 1}, {"@0004000002620102", 1}, {"@0004010000130002", 1}, {"@0004010004ea0102", 1}, {"@0005000000140002", 1}, {"@0005000000390102", 1}, {"@0005000003730102", 1}, {"@0005ff00023a0702", 1}, {"@0006000000150002", 1}, {"@00070000001a0002", 1}, {"@0007000002630102", 1}, {"@0008000000030002", 1}, {"@0008000002640102", 1}, {"@00080100042f1a02", 1}, {"@0008ff00023b0702", 1}, {"@00090000000d0002", 1}, {"@0009000002650102", 1}, {"@000a000000380102", 1}, {"@000a010004c20102", 1}, {"@0013000002660102", 1}, {"@00130000037a0002", 1}, {"@0014000002670102", 1}, {"@0017000002680102", 1} } },
	{"000400000019BE00", { {"@0000000000000002", 1}, {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0000000002380602", 1}, {"@0000000002390602", 1}, {"@0000000003710102", 1}, {"@00000004043f1c02", 1}, {"@0000010000190002", 1}, {"@0000010000190002", 1}, {"@0000030003a60102", 1}, {"@0000040004c10102", 1}, {"@0000050004e90102", 1}, {"@00010000000c0002", 1}, {"@0001000000350102", 1}, {"@0001000404401c02", 1}, {"@0002000000010002", 1}, {"@0002000000360102", 1}, {"@0002000003720102", 1}, {"@0002000404411c02", 1}, {"@0002010003a70102", 1}, {"@0003000000020002", 1}, {"@0003000000370102", 1}, {"@0003000404421c02", 1}, {"@0003010200410302", 1}, {"@0003010200420302", 1}, {"@0003010200430302", 1}, {"@00030102023e0302", 1}, {"@0004000002620102", 1}, {"@0004010000130002", 1}, {"@0004010004ea0102", 1}, {"@0005000000140002", 1}, {"@0005000000390102", 1}, {"@0005000003730102", 1}, {"@0005ff00023a0702", 1}, {"@0006000000150002", 1}, {"@00070000001a0002", 1}, {"@0007000002630102", 1}, {"@0008000000030002", 1}, {"@0008000002640102", 1}, {"@00080100042f1a02", 1}, {"@0008ff00023b0702", 1}, {"@00090000000d0002", 1}, {"@0009000002650102", 1}, {"@000a000000380102", 1}, {"@000a010004c20102", 1}, {"@0013000002660102", 1}, {"@00130000037a0002", 1}, {"@0014000002670102", 1}, {"@0017000002680102", 1} } },
	{"00040000001C4D00", { {"@0000000000000002", 1}, {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0000000002380602", 1}, {"@0000000002390602", 1}, {"@0000000003710102", 1}, {"@00000004043f1c02", 1}, {"@0000010000190002", 1}, {"@0000030003a60102", 1}, {"@0000040004c10102", 1}, {"@0000050004e90102", 1}, {"@00010000000c0002", 1}, {"@0001000000350102", 1}, {"@0001000404401c02", 1}, {"@0002000000010002", 1}, {"@0002000000360102", 1}, {"@0002000003720102", 1}, {"@0002000404411c02", 1}, {"@0002010003a70102", 1}, {"@0003000000020002", 1}, {"@0003000000370102", 1}, {"@0003000404421c02", 1}, {"@0003010200410302", 1}, {"@0003010200420302", 1}, {"@0003010200430302", 1}, {"@00030102023e0302", 1}, {"@0004000002620102", 1}, {"@0004010000130002", 1}, {"@0004010004ea0102", 1}, {"@0005000000140002", 1}, {"@0005000000390102", 1}, {"@0005000003730102", 1}, {"@0005ff00023a0702", 1}, {"@0006000000150002", 1}, {"@00070000001a0002", 1}, {"@0007000002630102", 1}, {"@0008000000030002", 1}, {"@0008000002640102", 1}, {"@00080100042f1a02", 1}, {"@0008ff00023b0702", 1}, {"@00090000000d0002", 1}, {"@0009000002650102", 1}, {"@000a000000380102", 1}, {"@000a010004c20102", 1}, {"@0013000002660102", 1}, {"@00130000037a0002", 1}, {"@0014000002670102", 1}, {"@0015000003670102", 1}, {"@0017000002680102", 1}, {"@0023000003680102", 1} } },
	{"00040000001C4E00", { {"@0000000000000002", 1}, {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0000000002380602", 1}, {"@0000000002390602", 1}, {"@0000000003710102", 1}, {"@00000004043f1c02", 1}, {"@0000010000190002", 1}, {"@0000030003a60102", 1}, {"@0000040004c10102", 1}, {"@0000050004e90102", 1}, {"@00010000000c0002", 1}, {"@0001000000350102", 1}, {"@0001000404401c02", 1}, {"@0002000000010002", 1}, {"@0002000000360102", 1}, {"@0002000003720102", 1}, {"@0002000404411c02", 1}, {"@0002010003a70102", 1}, {"@0003000000020002", 1}, {"@0003000000370102", 1}, {"@0003000404421c02", 1}, {"@0003010200410302", 1}, {"@0003010200420302", 1}, {"@0003010200430302", 1}, {"@00030102023e0302", 1}, {"@0004000002620102", 1}, {"@0004010000130002", 1}, {"@0004010004ea0102", 1}, {"@0005000000140002", 1}, {"@0005000000390102", 1}, {"@0005000003730102", 1}, {"@0005ff00023a0702", 1}, {"@0006000000150002", 1}, {"@00070000001a0002", 1}, {"@0007000002630102", 1}, {"@0008000000030002", 1}, {"@0008000002640102", 1}, {"@00080100042f1a02", 1}, {"@0008ff00023b0702", 1}, {"@00090000000d0002", 1}, {"@0009000002650102", 1}, {"@000a000000380102", 1}, {"@000a010004c20102", 1}, {"@0013000002660102", 1}, {"@00130000037a0002", 1}, {"@0014000002670102", 1}, {"@0015000003670102", 1}, {"@0017000002680102", 1}, {"@0023000003680102", 1} } },
	{"0004000000175300", { {"@0000000000000002", 1}, {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0000000002380602", 1}, {"@0000000002390602", 1}, {"@0000000003710102", 1}, {"@00000004043f1c02", 1}, {"@0000010000190002", 1}, {"@0000030003a60102", 1}, {"@0000040004c10102", 1}, {"@0000050004e90102", 1}, {"@00010000000c0002", 1}, {"@0001000000350102", 1}, {"@0001000404401c02", 1}, {"@0002000000010002", 1}, {"@0002000000360102", 1}, {"@0002000003720102", 1}, {"@0002000404411c02", 1}, {"@0002010003a70102", 1}, {"@0004000002620102", 1}, {"@0004010000130002", 1}, {"@0004010004ea0102", 1}, {"@0005000000140002", 1}, {"@0005000000390102", 1}, {"@0005000003730102", 1}, {"@0005ff00023a0702", 1}, {"@0006000000150002", 1}, {"@05c0000000060002", 1}, {"@05c0000003651302", 1}, {"@05c0000004121302", 1}, {"@05c00000043a1302", 1}, {"@05c00000043b1302", 1}, {"@05c00100001d0002", 1}, {"@05c1000003661302", 1} } },
	{"000400000016CE00", { {"@0000000000000002", 1}, {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0000000002380602", 1}, {"@0000000002390602", 1}, {"@0000000003710102", 1}, {"@00000004043f1c02", 1}, {"@0000010000190002", 1}, {"@0000030003a60102", 1}, {"@0000040004c10102", 1}, {"@0000050004e90102", 1}, {"@00010000000c0002", 1}, {"@0001000000350102", 1}, {"@0001000404401c02", 1}, {"@0002000000010002", 1}, {"@0002000000360102", 1}, {"@0002000003720102", 1}, {"@0002000404411c02", 1}, {"@0002010003a70102", 1}, {"@0004000002620102", 1}, {"@0004010000130002", 1}, {"@0004010004ea0102", 1}, {"@0005000000140002", 1}, {"@0005000000390102", 1}, {"@0005000003730102", 1}, {"@0005ff00023a0702", 1}, {"@0006000000150002", 1}, {"@05c0000000060002", 1}, {"@05c0000003651302", 1}, {"@05c0000004121302", 1}, {"@05c00000043a1302", 1}, {"@05c00000043b1302", 1}, {"@05c00100001d0002", 1}, {"@05c1000003661302", 1} } },
	{"000400000016E300", { {"@0000000000000002", 1}, {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0000000002380602", 1}, {"@0000000002390602", 1}, {"@0000000003710102", 1}, {"@00000004043f1c02", 1}, {"@0000010000190002", 1}, {"@0000030003a60102", 1}, {"@0000040004c10102", 1}, {"@0000050004e90102", 1}, {"@00010000000c0002", 1}, {"@0001000000350102", 1}, {"@0001000404401c02", 1}, {"@0002000000010002", 1}, {"@0002000000360102", 1}, {"@0002000003720102", 1}, {"@0002000404411c02", 1}, {"@0002010003a70102", 1}, {"@0004000002620102", 1}, {"@0004010000130002", 1}, {"@0004010004ea0102", 1}, {"@0005000000140002", 1}, {"@0005000000390102", 1}, {"@0005000003730102", 1}, {"@0005ff00023a0702", 1}, {"@0006000000150002", 1}, {"@05c0000000060002", 1}, {"@05c0000003651302", 1}, {"@05c0000004121302", 1}, {"@05c00000043a1302", 1}, {"@05c00000043b1302", 1}, {"@05c00100001d0002", 1}, {"@05c1000003661302", 1} } },
	{"0004000000175200", { {"@0000000000000002", 1}, {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0000000002380602", 1}, {"@0000000002390602", 1}, {"@0000000003710102", 1}, {"@00000004043f1c02", 1}, {"@0000010000190002", 1}, {"@0000030003a60102", 1}, {"@0000040004c10102", 1}, {"@0000050004e90102", 1}, {"@00010000000c0002", 1}, {"@0001000000350102", 1}, {"@0001000404401c02", 1}, {"@0002000000010002", 1}, {"@0002000000360102", 1}, {"@0002000003720102", 1}, {"@0002000404411c02", 1}, {"@0002010003a70102", 1}, {"@0004000002620102", 1}, {"@0004010000130002", 1}, {"@0004010004ea0102", 1}, {"@0005000000140002", 1}, {"@0005000000390102", 1}, {"@0005000003730102", 1}, {"@0005ff00023a0702", 1}, {"@0006000000150002", 1}, {"@05c0000000060002", 1}, {"@05c0000003651302", 1}, {"@05c0000004121302", 1}, {"@05c00000043a1302", 1}, {"@05c00000043b1302", 1}, {"@05c00100001d0002", 1}, {"@05c1000003661302", 1} } },
	{"0004000000178800", { {"@0000000000000002", 1}, {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0000000002380602", 1}, {"@0000000002390602", 1}, {"@0000000003710102", 1}, {"@00000004043f1c02", 1}, {"@0000010000190002", 1}, {"@0000030003a60102", 1}, {"@0000040004c10102", 1}, {"@0000050004e90102", 1}, {"@00010000000c0002", 1}, {"@0001000000350102", 1}, {"@0001000404401c02", 1}, {"@0002000000010002", 1}, {"@0002000000360102", 1}, {"@0002000003720102", 1}, {"@0002000404411c02", 1}, {"@0002010003a70102", 1}, {"@0003000000020002", 1}, {"@0003000000370102", 1}, {"@0003000404421c02", 1}, {"@0003010200410302", 1}, {"@0003010200420302", 1}, {"@0003010200430302", 1}, {"@00030102023e0302", 1}, {"@0004000002620102", 1}, {"@0004010000130002", 1}, {"@0004010004ea0102", 1}, {"@0005000000140002", 1}, {"@0005000000390102", 1}, {"@0005000003730102", 1}, {"@0005ff00023a0702", 1}, {"@0008000000030002", 1}, {"@0008000002640102", 1}, {"@00080100042f1a02", 1}, {"@0008ff00023b0702", 1}, {"@000a000000380102", 1}, {"@000a010004c20102", 1}, {"@0013000002660102", 1}, {"@00130000037a0002", 1}, {"@0100000000040002", 1}, {"@01000000034b0902", 1}, {"@01000000034c0902", 1}, {"@01000000034d0902", 1}, {"@01000000034e0902", 1}, {"@01000000034f0902", 1}, {"@0100000003530902", 1}, {"@0100000003540902", 1}, {"@01000000037c0002", 1}, {"@0100000003990902", 1}, {"@0100000004180902", 1}, {"@0100010000160002", 1}, {"@0100010003500902", 1}, {"@01010000000e0002", 1}, {"@0101000003520902", 1}, {"@0101000003560902", 1}, {"@0101000004190902", 1}, {"@0101010000170002", 1}, {"@0101030004140902", 1}, {"@01020100001b0002", 1}, {"@01020100041a0902", 1}, {"@01810000024b0502", 1}, {"@01810000037d0002", 1}, {"@01810100023f0502", 1}, {"@0182000002400502", 1}, {"@0183000002420502", 1}, {"@018e000002490502", 1}, {"@05c0000000060002", 1}, {"@05c0000003651302", 1}, {"@05c0000004121302", 1}, {"@05c00000043a1302", 1}, {"@05c00000043b1302", 1}, {"@05c00100001d0002", 1}, {"@08000100003e0402", 1}, {"@08000100025f0402", 1}, {"@0800010003690402", 1}, {"@0800010003820002", 1}, {"@0800010004150402", 1}, {"@08000200003f0402", 1}, {"@0800020002600402", 1}, {"@08000200036a0402", 1}, {"@0800030000400402", 1}, {"@0800030002610402", 1}, {"@08000300036b0402", 1}, {"@08010000025d0402", 1}, {"@0801000004360402", 1}, {"@08020000025e0402", 1}, {"@0802000004370402", 1}, {"@1f000000000a0002", 1}, {"@1f00000002540c02", 1}, {"@22800000002c0002", 1} } },
	{"00040000001B4E00", { {"@0000000000000002", 1}, {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0000000002380602", 1}, {"@0000000002390602", 1}, {"@0000000003710102", 1}, {"@00000004043f1c02", 1}, {"@0000010000190002", 1}, {"@0000030003a60102", 1}, {"@0000040004c10102", 1}, {"@0000050004e90102", 1}, {"@00010000000c0002", 1}, {"@0001000000350102", 1}, {"@0001000404401c02", 1}, {"@0002000000010002", 1}, {"@0002000000360102", 1}, {"@0002000003720102", 1}, {"@0002000404411c02", 1}, {"@0002010003a70102", 1}, {"@0003000000020002", 1}, {"@0003000000370102", 1}, {"@0003000404421c02", 1}, {"@0003010200410302", 1}, {"@0003010200420302", 1}, {"@0003010200430302", 1}, {"@00030102023e0302", 1}, {"@0004000002620102", 1}, {"@0004010000130002", 1}, {"@0004010004ea0102", 1}, {"@0005000000140002", 1}, {"@0005000000390102", 1}, {"@0005000003730102", 1}, {"@0005ff00023a0702", 1}, {"@0008000000030002", 1}, {"@0008000002640102", 1}, {"@00080100042f1a02", 1}, {"@0008ff00023b0702", 1}, {"@000a000000380102", 1}, {"@000a010004c20102", 1}, {"@0013000002660102", 1}, {"@00130000037a0002", 1}, {"@0100000000040002", 1}, {"@01000000034b0902", 1}, {"@01000000034c0902", 1}, {"@01000000034d0902", 1}, {"@01000000034e0902", 1}, {"@01000000034f0902", 1}, {"@0100000003530902", 1}, {"@0100000003540902", 1}, {"@01000000037c0002", 1}, {"@0100000003990902", 1}, {"@0100000004180902", 1}, {"@0100010000160002", 1}, {"@0100010003500902", 1}, {"@01010000000e0002", 1}, {"@0101000003520902", 1}, {"@0101000003560902", 1}, {"@0101000004190902", 1}, {"@0101010000170002", 1}, {"@0101030004140902", 1}, {"@01020100001b0002", 1}, {"@01020100041a0902", 1}, {"@01810000024b0502", 1}, {"@01810000037d0002", 1}, {"@01810100023f0502", 1}, {"@0182000002400502", 1}, {"@0183000002420502", 1}, {"@018e000002490502", 1}, {"@05c0000000060002", 1}, {"@05c0000003651302", 1}, {"@05c0000004121302", 1}, {"@05c00000043a1302", 1}, {"@05c00000043b1302", 1}, {"@05c00100001d0002", 1}, {"@08000100003e0402", 1}, {"@08000100025f0402", 1}, {"@0800010003690402", 1}, {"@0800010003820002", 1}, {"@0800010004150402", 1}, {"@08000200003f0402", 1}, {"@0800020002600402", 1}, {"@08000200036a0402", 1}, {"@0800030000400402", 1}, {"@0800030002610402", 1}, {"@08000300036b0402", 1}, {"@08010000025d0402", 1}, {"@0801000004360402", 1}, {"@08020000025e0402", 1}, {"@0802000004370402", 1}, {"@1f000000000a0002", 1}, {"@1f00000002540c02", 1}, {"@22800000002c0002", 1} } },
	{"00040000001B4F00", { {"@0000000000000002", 1}, {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0000000002380602", 1}, {"@0000000002390602", 1}, {"@0000000003710102", 1}, {"@00000004043f1c02", 1}, {"@0000010000190002", 1}, {"@0000030003a60102", 1}, {"@0000040004c10102", 1}, {"@0000050004e90102", 1}, {"@00010000000c0002", 1}, {"@0001000000350102", 1}, {"@0001000404401c02", 1}, {"@0002000000010002", 1}, {"@0002000000360102", 1}, {"@0002000003720102", 1}, {"@0002000404411c02", 1}, {"@0002010003a70102", 1}, {"@0003000000020002", 1}, {"@0003000000370102", 1}, {"@0003000404421c02", 1}, {"@0003010200410302", 1}, {"@0003010200420302", 1}, {"@0003010200430302", 1}, {"@00030102023e0302", 1}, {"@0004000002620102", 1}, {"@0004010000130002", 1}, {"@0004010004ea0102", 1}, {"@0005000000140002", 1}, {"@0005000000390102", 1}, {"@0005000003730102", 1}, {"@0005ff00023a0702", 1}, {"@0008000000030002", 1}, {"@0008000002640102", 1}, {"@00080100042f1a02", 1}, {"@0008ff00023b0702", 1}, {"@000a000000380102", 1}, {"@000a010004c20102", 1}, {"@0013000002660102", 1}, {"@00130000037a0002", 1}, {"@0100000000040002", 1}, {"@01000000034b0902", 1}, {"@01000000034c0902", 1}, {"@01000000034d0902", 1}, {"@01000000034e0902", 1}, {"@01000000034f0902", 1}, {"@0100000003530902", 1}, {"@0100000003540902", 1}, {"@01000000037c0002", 1}, {"@0100000003990902", 1}, {"@0100000004180902", 1}, {"@0100010000160002", 1}, {"@0100010003500902", 1}, {"@01010000000e0002", 1}, {"@0101000003520902", 1}, {"@0101000003560902", 1}, {"@0101000004190902", 1}, {"@0101010000170002", 1}, {"@0101030004140902", 1}, {"@01020100001b0002", 1}, {"@01020100041a0902", 1}, {"@01810000024b0502", 1}, {"@01810000037d0002", 1}, {"@01810100023f0502", 1}, {"@0182000002400502", 1}, {"@0183000002420502", 1}, {"@018e000002490502", 1}, {"@05c0000000060002", 1}, {"@05c0000003651302", 1}, {"@05c0000004121302", 1}, {"@05c00000043a1302", 1}, {"@05c00000043b1302", 1}, {"@05c00100001d0002", 1}, {"@08000100003e0402", 1}, {"@08000100025f0402", 1}, {"@0800010003690402", 1}, {"@0800010003820002", 1}, {"@0800010004150402", 1}, {"@08000200003f0402", 1}, {"@0800020002600402", 1}, {"@08000200036a0402", 1}, {"@0800030000400402", 1}, {"@0800030002610402", 1}, {"@08000300036b0402", 1}, {"@08010000025d0402", 1}, {"@0801000004360402", 1}, {"@08020000025e0402", 1}, {"@0802000004370402", 1}, {"@1f000000000a0002", 1}, {"@1f00000002540c02", 1}, {"@22800000002c0002", 1} } },
	{"000400000016C200", { {"@0000000000000002", 1}, {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0000000002380602", 1}, {"@0000000002390602", 1}, {"@0000000003710102", 1}, {"@00000004043f1c02", 1}, {"@0000010000190002", 1}, {"@0000030003a60102", 1}, {"@0000040004c10102", 1}, {"@0000050004e90102", 1}, {"@00010000000c0002", 1}, {"@0001000000350102", 1}, {"@0001000404401c02", 1}, {"@0002000000010002", 1}, {"@0002000000360102", 1}, {"@0002000003720102", 1}, {"@0002000404411c02", 1}, {"@0002010003a70102", 1}, {"@0003000000020002", 1}, {"@0003000000370102", 1}, {"@0003000404421c02", 1}, {"@0003010200410302", 1}, {"@0003010200420302", 1}, {"@0003010200430302", 1}, {"@00030102023e0302", 1}, {"@0004000002620102", 1}, {"@0004010000130002", 1}, {"@0004010004ea0102", 1}, {"@0005000000140002", 1}, {"@0005000000390102", 1}, {"@0005000003730102", 1}, {"@0005ff00023a0702", 1}, {"@0006000000150002", 1}, {"@0008000000030002", 1}, {"@0008000002640102", 1}, {"@00080100042f1a02", 1}, {"@0008ff00023b0702", 1}, {"@00090000000d0002", 1}, {"@0009000002650102", 1}, {"@000a000000380102", 1}, {"@000a010004c20102", 1} } },
	{"000400000016C300", { {"@0000000000000002", 1}, {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0000000002380602", 1}, {"@0000000002390602", 1}, {"@0000000003710102", 1}, {"@00000004043f1c02", 1}, {"@0000010000190002", 1}, {"@0000030003a60102", 1}, {"@0000040004c10102", 1}, {"@0000050004e90102", 1}, {"@00010000000c0002", 1}, {"@0001000000350102", 1}, {"@0001000404401c02", 1}, {"@0002000000010002", 1}, {"@0002000000360102", 1}, {"@0002000003720102", 1}, {"@0002000404411c02", 1}, {"@0002010003a70102", 1}, {"@0003000000020002", 1}, {"@0003000000370102", 1}, {"@0003000404421c02", 1}, {"@0003010200410302", 1}, {"@0003010200420302", 1}, {"@0003010200430302", 1}, {"@00030102023e0302", 1}, {"@0004000002620102", 1}, {"@0004010000130002", 1}, {"@0004010004ea0102", 1}, {"@0005000000140002", 1}, {"@0005000000390102", 1}, {"@0005000003730102", 1}, {"@0005ff00023a0702", 1}, {"@0006000000150002", 1}, {"@0008000000030002", 1}, {"@0008000002640102", 1}, {"@00080100042f1a02", 1}, {"@0008ff00023b0702", 1}, {"@00090000000d0002", 1}, {"@0009000002650102", 1}, {"@000a000000380102", 1}, {"@000a010004c20102", 1} } },
	{"0004000000144400", { {"@0000000000000002", 1}, {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0000000002380602", 1}, {"@0000000002390602", 1}, {"@0000000003710102", 1}, {"@00000004043f1c02", 1}, {"@00010000000c0002", 1}, {"@0001000000350102", 1}, {"@0001000404401c02", 1}, {"@0003000000020002", 1}, {"@0003000000370102", 1}, {"@0003000404421c02", 1}, {"@0008000000030002", 1}, {"@0008000002640102", 1}, {"@0100000000040002", 1}, {"@01000000034b0902", 1}, {"@01000000034c0902", 1}, {"@01000000034d0902", 1}, {"@01000000034e0902", 1}, {"@01000000034f0902", 1}, {"@0100000003530902", 1}, {"@0100000003540902", 1}, {"@01000000037c0002", 1}, {"@0100000003990902", 1}, {"@0100000004180902", 1}, {"@0580000000050002", 1}, {"@05c0000000060002", 1}, {"@05c0000003651302", 1}, {"@05c0000004121302", 1}, {"@05c00000043a1302", 1}, {"@05c00000043b1302", 1}, {"@0700000000070002", 1}, {"@1f000000000a0002", 1}, {"@1f00000002540c02", 1}, {"@21000000000b0002", 1} } },
	{"0004000000187E00", { {"@0000000000000002", 1}, {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0000000002380602", 1}, {"@0000000002390602", 1}, {"@0000000003710102", 1}, {"@00000004043f1c02", 1}, {"@0000010000190002", 1}, {"@0000030003a60102", 1}, {"@0000040004c10102", 1}, {"@0000050004e90102", 1}, {"@00010000000c0002", 1}, {"@0001000000350102", 1}, {"@0001000404401c02", 1}, {"@0002000000010002", 1}, {"@0002000000360102", 1}, {"@0002000003720102", 1}, {"@0002000404411c02", 1}, {"@0002010003a70102", 1}, {"@0003000000020002", 1}, {"@0003000000370102", 1}, {"@0003000404421c02", 1}, {"@0003010200410302", 1}, {"@0003010200420302", 1}, {"@0003010200430302", 1}, {"@00030102023e0302", 1}, {"@0005000000140002", 1}, {"@0005000000390102", 1}, {"@0005000003730102", 1}, {"@0005ff00023a0702", 1}, {"@000a000000380102", 1}, {"@000a010004c20102", 1}, {"@0100000000040002", 1}, {"@01000000034b0902", 1}, {"@01000000034c0902", 1}, {"@01000000034d0902", 1}, {"@01000000034e0902", 1}, {"@01000000034f0902", 1}, {"@0100000003530902", 1}, {"@0100000003540902", 1}, {"@01000000037c0002", 1}, {"@0100000003990902", 1}, {"@0100000004180902", 1}, {"@0100010000160002", 1}, {"@0100010003500902", 1}, {"@1f000000000a0002", 1}, {"@1f00000002540c02", 1}, {"@1f01000000270002", 1}, {"@1f01000002550c02", 1}, {"@1f02000000280002", 1}, {"@1f02000002560c02", 1} } },
	{"0004000000196500", { {"@0000000000000002", 1}, {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0000000002380602", 1}, {"@0000000002390602", 1}, {"@0000000003710102", 1}, {"@00000004043f1c02", 1}, {"@0000010000190002", 1}, {"@0000030003a60102", 1}, {"@0000040004c10102", 1}, {"@0000050004e90102", 1}, {"@0002000000010002", 1}, {"@0002000000360102", 1}, {"@0002000003720102", 1}, {"@0002000404411c02", 1}, {"@0002010003a70102", 1}, {"@0003000000020002", 1}, {"@0003000000370102", 1}, {"@0003000404421c02", 1}, {"@0003010200410302", 1}, {"@0003010200420302", 1}, {"@0003010200430302", 1}, {"@00030102023e0302", 1}, {"@0004000002620102", 1}, {"@0004010000130002", 1}, {"@0004010004ea0102", 1}, {"@01010000000e0002", 1}, {"@0101000003520902", 1}, {"@0101000003560902", 1}, {"@0101000004190902", 1}, {"@0101010000170002", 1}, {"@0101030004140902", 1}, {"@01810000024b0502", 1}, {"@01810000037d0002", 1}, {"@0181000100440502", 1}, {"@0181000101d40502", 1}, {"@01810100023f0502", 1}, {"@0181010100b40502", 1}, {"@01810201011a0502", 1}, {"@0181030101700502", 1}, {"@0181040103aa0502", 1}, {"@0181050103bf0502", 1}, {"@03710001005c0502", 1}, {"@05c0000000060002", 1}, {"@05c0000003651302", 1}, {"@05c0000004121302", 1}, {"@05c00000043a1302", 1}, {"@05c00000043b1302", 1}, {"@05c00100001d0002", 1}, {"@0700000000070002", 1}, {"@07420000001f0002", 1}, {"@08000100003e0402", 1}, {"@08000100025f0402", 1}, {"@0800010003690402", 1}, {"@0800010003820002", 1}, {"@0800010004150402", 1}, {"@1f000000000a0002", 1}, {"@1f00000002540c02", 1}, {"@2102000000290002", 1} } },
	{"00040000001C2500", { {"@0000000000000002", 1}, {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0000000002380602", 1}, {"@0000000002390602", 1}, {"@0000000003710102", 1}, {"@00000004043f1c02", 1}, {"@0000010000190002", 1}, {"@0000030003a60102", 1}, {"@0000040004c10102", 1}, {"@0000050004e90102", 1}, {"@00010000000c0002", 1}, {"@0001000000350102", 1}, {"@0001000404401c02", 1}, {"@0002000000010002", 1}, {"@0002000000360102", 1}, {"@0002000003720102", 1}, {"@0002000404411c02", 1}, {"@0002010003a70102", 1}, {"@0003000000020002", 1}, {"@0003000000370102", 1}, {"@0003000404421c02", 1}, {"@0003010200410302", 1}, {"@0003010200420302", 1}, {"@0003010200430302", 1}, {"@00030102023e0302", 1}, {"@0100000000040002", 1}, {"@01000000034b0902", 1}, {"@01000000034c0902", 1}, {"@01000000034d0902", 1}, {"@01000000034e0902", 1}, {"@01000000034f0902", 1}, {"@0100000003530902", 1}, {"@0100000003540902", 1}, {"@01000000037c0002", 1}, {"@0100000003990902", 1}, {"@0100000004180902", 1}, {"@0100010000160002", 1}, {"@0100010003500902", 1}, {"@01010000000e0002", 1}, {"@0101000003520902", 1}, {"@0101000003560902", 1}, {"@0101000004190902", 1}, {"@0101010000170002", 1}, {"@0101030004140902", 1}, {"@01020100001b0002", 1}, {"@01020100041a0902", 1}, {"@01030000024f0902", 1}, {"@0105000003580902", 1}, {"@0106000003590902", 1}, {"@01070000035a0902", 1}, {"@01080000035b0902", 1}, {"@0109000004a30902", 1}, {"@010a000004a40902", 1}, {"@010b000004a50902", 1}, {"@010c000004a60902", 1}, {"@0140000003550902", 1}, {"@01410000035c0902", 1}, {"@0180000000080002", 1}, {"@01810000024b0502", 1}, {"@0181000100440502", 1}, {"@0181000101d40502", 1}, {"@01810100023f0502", 1}, {"@0181010100b40502", 1}, {"@01810201011a0502", 1}, {"@0181030101700502", 1}, {"@0181040103aa0502", 1}, {"@0181050103bf0502", 1}, {"@0182000002400502", 1}, {"@0182000100a80502", 1}, {"@0182000101d80502", 1}, {"@0182000103b20502", 1}, {"@0182010100460502", 1}, {"@0183000002420502", 1}, {"@0183000100450502", 1}, {"@01830101010e0502", 1}, {"@0183020103a80502", 1}, {"@0183030103be0502", 1}, {"@01840000024d0502", 1}, {"@0184050103a90502", 1}, {"@01850001004b0502", 1}, {"@0185020101170502", 1}, {"@0185040101790502", 1}, {"@0186010100af0502", 1}, {"@0186030101750502", 1}, {"@0187000100470502", 1}, {"@0187000103b00502", 1}, {"@0188000002410502", 1}, {"@0188000101120502", 1}, {"@0188000103af0502", 1}, {"@0189000100ab0502", 1}, {"@0189010103b10502", 1}, {"@018a000002450502", 1}, {"@018a000100a90502", 1}, {"@018b000002460502", 1}, {"@018b000101150502", 1}, {"@018c000002430502", 1}, {"@018c0001004c0502", 1}, {"@018c010101180502", 1}, {"@018d0000024c0502", 1}, {"@018d0001010c0502", 1}, {"@018e000002490502", 1}, {"@018e000100490502", 1}, {"@018e010101780502", 1}, {"@018f000100b30502", 1}, {"@018f010101190502", 1}, {"@0190000101710502", 1}, {"@01910001004e0502", 1}, {"@0192000002470502", 1}, {"@01920001010d0502", 1}, {"@0192000103ad0502", 1}, {"@0193000002480502", 1}, {"@0193000101740502", 1}, {"@0193000103ae0502", 1}, {"@01940000024a0502", 1}, {"@0194000100aa0502", 1}, {"@0194000103b60502", 1}, {"@0195000100b00502", 1}, {"@01960000024e0502", 1}, {"@0196000100480502", 1}, {"@0197000101770502", 1}, {"@0198000100b10502", 1}, {"@0199000101160502", 1}, {"@019a000100b70502", 1}, {"@019b000100b60502", 1}, {"@019c000101730502", 1}, {"@019d000100ac0502", 1}, {"@019e000100ad0502", 1}, {"@019f000101110502", 1}, {"@01a00001010f0502", 1}, {"@01a1000101100502", 1}, {"@01a20001017d0502", 1}, {"@01a2000103b90502", 1}, {"@01a30001004a0502", 1}, {"@01a40001004d0502", 1}, {"@01a5000101720502", 1}, {"@01a6000100500502", 1}, {"@01a6000103b70502", 1}, {"@01a7000101140502", 1}, {"@01a80001004f0502", 1}, {"@01a80101017e0502", 1}, {"@01a9000101760502", 1}, {"@01aa000100530502", 1}, {"@01ab0001017c0502", 1}, {"@01ac0001017f0502", 1}, {"@01ad000100b80502", 1}, {"@01ae0001011b0502", 1}, {"@01af0001011c0502", 1}, {"@01b0000100520502", 1}, {"@01b1000100b20502", 1}, {"@01b10101017b0502", 1}, {"@01b3000100b50502", 1}, {"@01b4000101130502", 1}, {"@01b5000100510502", 1}, {"@01b6000100ae0502", 1}, {"@01c1000002440502", 1}, {"@01c1000100540502", 1}, {"@01c10101017a0502", 1}, {"@01c1020103bb0502", 1}, {"@0200000100a10502", 1}, {"@02010001016a0502", 1}, {"@0202000101030502", 1}, {"@02030001019a0502", 1}, {"@0206000103120502", 1}, {"@0208000100960502", 1}, {"@02090001019f0502", 1}, {"@0214000100e40502", 1}, {"@0215000101820502", 1}, {"@0216000100570502", 1}, {"@0217000101b30502", 1}, {"@02190001007e0502", 1}, {"@021a000100da0502", 1}, {"@021b000100800502", 1}, {"@021c000102f70502", 1}, {"@021d000101cd0502", 1}, {"@021e000101230502", 1}, {"@021f000103170502", 1}, {"@0220000100fd0502", 1}, {"@02210001013c0502", 1}, {"@0222000101440502", 1}, {"@022d000100f20502", 1}, {"@022e000101d30502", 1}, {"@022f0001011e0502", 1}, {"@0230000101d20502", 1}, {"@02310001006a0502", 1}, {"@0232000102ea0502", 1}, {"@0233000103060502", 1}, {"@0235000100840502", 1}, {"@0238000102f80502", 1}, {"@023c000100bd0502", 1}, {"@023d000101b50502", 1}, {"@023e000100d10502", 1}, {"@023f000101660502", 1}, {"@024a000101d10502", 1}, {"@024b000101260502", 1}, {"@024d000102f60502", 1}, {"@024f000100810502", 1}, {"@0251000100c10502", 1}, {"@0252000100fe0502", 1}, {"@025d000100550502", 1}, {"@025e000101250502", 1}, {"@025f000101c50502", 1}, {"@025f000101d70502", 1}, {"@0260000100d20502", 1}, {"@0261000100650502", 1}, {"@0262000101370502", 1}, {"@0263000100750502", 1}, {"@0264000101ac0502", 1}, {"@0265000101540502", 1}, {"@0266000100680502", 1}, {"@0267000101080502", 1}, {"@02680001007d0502", 1}, {"@02690001011f0502", 1}, {"@026a000101460502", 1}, {"@026b000100e90502", 1}, {"@026c000100c30502", 1}, {"@026d0001013f0502", 1}, {"@026e000100ba0502", 1}, {"@026f000101900502", 1}, {"@0270000100ff0502", 1}, {"@02710001019b0502", 1}, {"@0272000101860502", 1}, {"@027d000100630502", 1}, {"@027e000101690502", 1}, {"@027f000100b90502", 1}, {"@0280000100830502", 1}, {"@0281000101200502", 1}, {"@0282000101810502", 1}, {"@0282000101d60502", 1}, {"@0283000100c70502", 1}, {"@0284000102fe0502", 1}, {"@0286000103130502", 1}, {"@02870001005a0502", 1}, {"@028a000102e90502", 1}, {"@028b000100e30502", 1}, {"@028c0001013e0502", 1}, {"@028d000101bd0502", 1}, {"@028e0001019e0502", 1}, {"@028f0101031a0502", 1}, {"@0299000100950502", 1}, {"@029a000100ee0502", 1}, {"@029b000100cb0502", 1}, {"@029e0001013d0502", 1}, {"@02a2000101ba0502", 1}, {"@02a3000102ff0502", 1}, {"@02a4000100720502", 1}, {"@02a50001018c0502", 1}, {"@02a6000101240502", 1}, {"@02b1000100690502", 1}, {"@02b2000100c40502", 1}, {"@02b70001030f0502", 1}, {"@02b80001019c0502", 1}, {"@02c3000100dc0502", 1}, {"@02c4000100670502", 1}, {"@02c5000103080502", 1}, {"@02c7000101220502", 1}, {"@02c9000100cd0502", 1}, {"@02ca000101ca0502", 1}, {"@02cb000101360502", 1}, {"@02d6000100560502", 1}, {"@02d7000101300502", 1}, {"@02d8000100e20502", 1}, {"@02d9000101c80502", 1}, {"@02da000101330502", 1}, {"@02db0001005e0502", 1}, {"@02dc000100be0502", 1}, {"@02dd000100ea0502", 1}, {"@02de0001009c0502", 1}, {"@02df000101910502", 1}, {"@02e00101031d0502", 1}, {"@02ea000101800502", 1}, {"@02ea000101d50502", 1}, {"@02eb000100de0502", 1}, {"@02ec000101c40502", 1}, {"@02ed0001015a0502", 1}, {"@02ee000101990502", 1}, {"@02ef000100580502", 1}, {"@02f0000100a70502", 1}, {"@02f1000101450502", 1}, {"@02f2000100cc0502", 1}, {"@02f3000102f90502", 1}, {"@02f4000103050502", 1}, {"@02f8000101380502", 1}, {"@02f9000101020502", 1}, {"@02fa000100970502", 1}, {"@02fb000100900502", 1}, {"@02fc0001018f0502", 1}, {"@0307000100640502", 1}, {"@03080001014d0502", 1}, {"@0309000100c60502", 1}, {"@030a000101c70502", 1}, {"@030b000100790502", 1}, {"@030c000101b80502", 1}, {"@030d000101840502", 1}, {"@030e0001012f0502", 1}, {"@030f0001016d0502", 1}, {"@0310000100f80502", 1}, {"@0311000100d60502", 1}, {"@0312000103090502", 1}, {"@0313000101210502", 1}, {"@0314000102f40502", 1}, {"@0316000101c00502", 1}, {"@0317000100a60502", 1}, {"@03180001006c0502", 1}, {"@0323000100760502", 1}, {"@0324000101890502", 1}, {"@03250001010a0502", 1}, {"@0326000101390502", 1}, {"@0327000101c30502", 1}, {"@0328000102eb0502", 1}, {"@03290001009d0502", 1}, {"@032a000103070502", 1}, {"@032c000101480502", 1}, {"@032d000100bc0502", 1}, {"@032e0101031c0502", 1}, {"@03380001011d0502", 1}, {"@0339000101b10502", 1}, {"@033a000101cc0502", 1}, {"@033b000100fa0502", 1}, {"@033c000101000502", 1}, {"@033d0001013a0502", 1}, {"@033e000101a20502", 1}, {"@033f0001008f0502", 1}, {"@03410001030e0502", 1}, {"@0342000101280502", 1}, {"@0343000102ef0502", 1}, {"@0344000100c50502", 1}, {"@03450001005f0502", 1}, {"@0347000103020502", 1}, {"@03480001006b0502", 1}, {"@03490001018d0502", 1}, {"@034a000101430502", 1}, {"@034b0001009f0502", 1}, {"@0356000101350502", 1}, {"@0357000100eb0502", 1}, {"@0358000102fa0502", 1}, {"@035a000100850502", 1}, {"@035c000101290502", 1}, {"@035d000100c90502", 1}, {"@035e0001018e0502", 1}, {"@0369000100d30502", 1}, {"@036a0001019d0502", 1}, {"@036b0001018b0502", 1}, {"@036d000103040502", 1}, {"@036e000102fb0502", 1}, {"@03700001015d0502", 1}, {"@03710001005c0502", 1}, {"@03720001010b0502", 1}, {"@0373000101340502", 1}, {"@0374010103190502", 1}, {"@037e000101560502", 1}, {"@037f000101aa0502", 1}, {"@0380000101870502", 1}, {"@0381000100d50502", 1}, {"@03820001016b0502", 1}, {"@03830001009b0502", 1}, {"@0384000100860502", 1}, {"@0385000101060502", 1}, {"@0390000101850502", 1}, {"@0392000101270502", 1}, {"@0393000100a00502", 1}, {"@0394000100890502", 1}, {"@0395000102fc0502", 1}, {"@0398000100bf0502", 1}, {"@0399000101c20502", 1}, {"@03a40001014f0502", 1}, {"@03a50001015b0502", 1}, {"@03a6000100c80502", 1}, {"@03a7000101a10502", 1}, {"@03a8000100910502", 1}, {"@03a9000100710502", 1}, {"@03aa000100e60502", 1}, {"@03ab000103160502", 1}, {"@03ac000101880502", 1}, {"@03ad000101b20502", 1}, {"@03ae000100870502", 1}, {"@03af0001012c0502", 1}, {"@03b0000101a90502", 1}, {"@03b1000100f00502", 1}, {"@03bc0001008a0502", 1}, {"@03bd000100f90502", 1}, {"@03be000101980502", 1}, {"@03bf000101bc0502", 1}, {"@03c0000103100502", 1}, {"@03c1000100bb0502", 1}, {"@03c40001012b0502", 1}, {"@03c50001015c0502", 1}, {"@03c6000100930502", 1}, {"@03d1000100c20502", 1}, {"@03d2000100e50502", 1}, {"@03d3000102f30502", 1}, {"@03d6000101570502", 1}, {"@03d7000101b40502", 1}, {"@03d9000101a50502", 1}, {"@03da000101510502", 1}, {"@03db0001006d0502", 1}, {"@03e6000100ec0502", 1}, {"@03e70001012a0502", 1}, {"@03e8000102f50502", 1}, {"@03ea0001030b0502", 1}, {"@03ec000101830502", 1}, {"@03ed000101a30502", 1}, {"@03ee0001008b0502", 1}, {"@03fa000100d00502", 1}, {"@03fb000101cf0502", 1}, {"@03fc000101470502", 1}, {"@03fd000101580502", 1}, {"@03fe000101a40502", 1}, {"@03ff000100f40502", 1}, {"@04000001006f0502", 1}, {"@0401000100660502", 1}, {"@040c000101590502", 1}, {"@040d000100780502", 1}, {"@040e000100880502", 1}, {"@040f000101500502", 1}, {"@04100001007f0502", 1}, {"@0411000101ab0502", 1}, {"@04140001030a0502", 1}, {"@0415000101bb0502", 1}, {"@0416000100fb0502", 1}, {"@0418000100d80502", 1}, {"@041a000100e00502", 1}, {"@041b000100f10502", 1}, {"@041c000101410502", 1}, {"@041d0001018a0502", 1}, {"@041e0001015f0502", 1}, {"@0429000100700502", 1}, {"@042a0001012d0502", 1}, {"@042b000101af0502", 1}, {"@0436000101940502", 1}, {"@0437000101050502", 1}, {"@0438000103000502", 1}, {"@0439000103110502", 1}, {"@043b000103030502", 1}, {"@043c000101cb0502", 1}, {"@043d0001007c0502", 1}, {"@043e000101490502", 1}, {"@043f000101550502", 1}, {"@0440000100ca0502", 1}, {"@044b0001016c0502", 1}, {"@044c0001008e0502", 1}, {"@044d000101930502", 1}, {"@044e000103150502", 1}, {"@0450000100cf0502", 1}, {"@04510001015e0502", 1}, {"@0452000100730502", 1}, {"@0453000101040502", 1}, {"@0454000101ae0502", 1}, {"@045f000101a80502", 1}, {"@0460000100a50502", 1}, {"@0461000101610502", 1}, {"@0462000100f60502", 1}, {"@0463000101310502", 1}, {"@0464000100c00502", 1}, {"@04650001006e0502", 1}, {"@0468000102f20502", 1}, {"@0469000101640502", 1}, {"@046a000101d00502", 1}, {"@046b000101970502", 1}, {"@046c0001008c0502", 1}, {"@046d000100f30502", 1}, {"@0478000101630502", 1}, {"@0479000100920502", 1}, {"@047a000100600502", 1}, {"@047b000100f50502", 1}, {"@047c000101a00502", 1}, {"@047d0001012e0502", 1}, {"@04800001008d0502", 1}, {"@0481000102f10502", 1}, {"@0482000102fd0502", 1}, {"@0483000101b00502", 1}, {"@04850001014c0502", 1}, {"@0486000100fc0502", 1}, {"@0487000101bf0502", 1}, {"@0488000100980502", 1}, {"@0489000100ef0502", 1}, {"@04940001009a0502", 1}, {"@0495000101920502", 1}, {"@0496000100d90502", 1}, {"@04970001007a0502", 1}, {"@04980001014a0502", 1}, {"@0499000100df0502", 1}, {"@049a0001014e0502", 1}, {"@049b000100610502", 1}, {"@049c000101400502", 1}, {"@049d000100ed0502", 1}, {"@049e000101b70502", 1}, {"@049f000103010502", 1}, {"@04a00001016e0502", 1}, {"@04a10001016f0502", 1}, {"@04a2000102e80502", 1}, {"@04a3000101c90502", 1}, {"@04a4000100d40502", 1}, {"@04a5000100740502", 1}, {"@04a6000100a30502", 1}, {"@04a7000101a60502", 1}, {"@04a80101031e0502", 1}, {"@04b2000101b90502", 1}, {"@04b3000100dd0502", 1}, {"@04b40001030c0502", 1}, {"@04b6000102ec0502", 1}, {"@04b9000101600502", 1}, {"@04ba0001005d0502", 1}, {"@04c5000101010502", 1}, {"@04c6000101670502", 1}, {"@04c7000100940502", 1}, {"@04c8000102ed0502", 1}, {"@04c90001030d0502", 1}, {"@04cc000100a40502", 1}, {"@04cd000101520502", 1}, {"@04ce000100db0502", 1}, {"@04cf000100e10502", 1}, {"@04d0000101960502", 1}, {"@04d10001009e0502", 1}, {"@04d2000101a70502", 1}, {"@04d30101031b0502", 1}, {"@04dd000100a20502", 1}, {"@04de000100ce0502", 1}, {"@04df000100e80502", 1}, {"@04e0000100f70502", 1}, {"@04e1000101be0502", 1}, {"@04e2000101090502", 1}, {"@04e3000101650502", 1}, {"@04e4000101b60502", 1}, {"@04e5000101ad0502", 1}, {"@04e6000100820502", 1}, {"@04e7000101320502", 1}, {"@04e8000101ce0502", 1}, {"@04ea000103180502", 1}, {"@04eb000102f00502", 1}, {"@04ec000100770502", 1}, {"@04ed000100620502", 1}, {"@04ee0001014b0502", 1}, {"@04ef0001013b0502", 1}, {"@04fa000101680502", 1}, {"@04fb000101c60502", 1}, {"@04fc000102ee0502", 1}, {"@04fd0001007b0502", 1}, {"@04fe000100590502", 1}, {"@04ff000101620502", 1}, {"@0500000100e70502", 1}, {"@050b000100990502", 1}, {"@050c000101c10502", 1}, {"@050d000101420502", 1}, {"@050e000100d70502", 1}, {"@050f000103140502", 1}, {"@0510000101070502", 1}, {"@0511000101950502", 1}, {"@0513000102e70502", 1}, {"@0514000101530502", 1}, {"@05150001005b0502", 1}, {"@08000100003e0402", 1}, {"@08000100025f0402", 1}, {"@0800010003690402", 1}, {"@0800010004150402", 1}, {"@08000200003f0402", 1}, {"@0800020002600402", 1}, {"@08000200036a0402", 1}, {"@0800030000400402", 1}, {"@0800030002610402", 1}, {"@08000300036b0402", 1}, {"@08010000025d0402", 1}, {"@0801000004360402", 1}, {"@08020000025e0402", 1}, {"@0802000004370402", 1}, {"@0803000003760402", 1}, {"@0803000004380402", 1}, {"@0804000003770402", 1}, {"@0804000004390402", 1}, {"@08050100038e0402", 1}, {"@08050200038f0402", 1}, {"@08050200041b0402", 1}, {"@0805030003900402", 1}, {"@08060100041c0402", 1}, {"@0807000004330402", 1}, {"@0808000004340402", 1}, {"@0809000004350402", 1}, {"@0a12000103c90502", 1}, {"@0a1c000103d30502", 1}, {"@1f000000000a0002", 1}, {"@1f00000002540c02", 1}, {"@1f01000000270002", 1}, {"@1f01000002550c02", 1}, {"@1f02000000280002", 1}, {"@1f02000002560c02", 1}, {"@1f03000002570c02", 1} } },
	{"00040000000EDF00", { {"@0000000000000002", 1}, {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0000000002380602", 1}, {"@0000000002390602", 1}, {"@0000000003710102", 1}, {"@00000004043f1c02", 1}, {"@0000010000190002", 1}, {"@0000030003a60102", 1}, {"@0000040004c10102", 1}, {"@0000050004e90102", 1}, {"@00010000000c0002", 1}, {"@0001000000350102", 1}, {"@0001000404401c02", 1}, {"@0002000000010002", 1}, {"@0002000000360102", 1}, {"@0002000003720102", 1}, {"@0002000404411c02", 1}, {"@0002010003a70102", 1}, {"@0003000000020002", 1}, {"@0003000000370102", 1}, {"@0003000404421c02", 1}, {"@0003010200410302", 1}, {"@0003010200420302", 1}, {"@0003010200430302", 1}, {"@00030102023e0302", 1}, {"@0004000002620102", 1}, {"@0004010000130002", 1}, {"@0004010004ea0102", 1}, {"@0005000000140002", 1}, {"@0005000000390102", 1}, {"@0005000003730102", 1}, {"@0005ff00023a0702", 1}, {"@0006000000150002", 1}, {"@00070000001a0002", 1}, {"@0007000002630102", 1}, {"@0008000000030002", 1}, {"@0008000002640102", 1}, {"@00080100042f1a02", 1}, {"@0008ff00023b0702", 1}, {"@00090000000d0002", 1}, {"@0009000002650102", 1}, {"@0100000000040002", 1}, {"@01000000034b0902", 1}, {"@01000000034c0902", 1}, {"@01000000034d0902", 1}, {"@01000000034e0902", 1}, {"@01000000034f0902", 1}, {"@0100000003530902", 1}, {"@0100000003540902", 1}, {"@01000000037c0002", 1}, {"@0100000003990902", 1}, {"@0100000004180902", 1}, {"@0100010000160002", 1}, {"@0100010003500902", 1}, {"@01010000000e0002", 1}, {"@0101000003520902", 1}, {"@0101000003560902", 1}, {"@0101000004190902", 1}, {"@0101010000170002", 1}, {"@0101030004140902", 1}, {"@01020100001b0002", 1}, {"@01020100041a0902", 1}, {"@0180000000080002", 1}, {"@0580000000050002", 1}, {"@05810000001c0002", 1}, {"@05c0000000060002", 1}, {"@05c0000003651302", 1}, {"@05c0000004121302", 1}, {"@05c00000043a1302", 1}, {"@05c00000043b1302", 1}, {"@05c00100001d0002", 1}, {"@0600000000120002", 1}, {"@06400100001e0002", 1}, {"@06c00000000f0002", 1}, {"@0700000000070002", 1}, {"@0740000000100002", 1}, {"@0741000000200002", 1}, {"@07420000001f0002", 1}, {"@07800000002d0002", 1}, {"@07810000002e0002", 1}, {"@0781000000330002", 1}, {"@07820000002f0002", 1}, {"@07c0000000210002", 1}, {"@07c0010000220002", 1}, {"@07c0020000230002", 1}, {"@1906000000240002", 1}, {"@1919000000090002", 1}, {"@1927000000260002", 1}, {"@19960000023d0002", 1}, {"@1ac0000000110002", 1}, {"@1b92000000250002", 1}, {"@1f000000000a0002", 1}, {"@1f00000002540c02", 1}, {"@1f01000000270002", 1}, {"@1f01000002550c02", 1}, {"@1f02000000280002", 1}, {"@1f02000002560c02", 1}, {"@21000000000b0002", 1}, {"@2101000000180002", 1}, {"@2102000000290002", 1}, {"@21030000002a0002", 1}, {"@2104000002520002", 1}, {"@21050000025a0002", 1}, {"@2105010003630002", 1}, {"@22400000002b0002", 1}, {"@22800000002c0002", 1}, {"@2281000002510002", 1}, {"@3200000000300002", 1}, {"@32400000025b0002", 1}, {"@3240010003640002", 1}, {"@3340000000320002", 1}, {"@3480000000310002", 1}, {"@3480000002580002", 1}, {"@3480000003791502", 1}, {"@34c0000002530002", 1}, {"@34c0000104a81d02", 1}, {"@34c0000104cb1d02", 1}, {"@3600000002590002", 1}, {"@3600010003620002", 1} } },
	{"00040000000EE000", { {"@0000000000000002", 1}, {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0000000002380602", 1}, {"@0000000002390602", 1}, {"@0000000003710102", 1}, {"@00000004043f1c02", 1}, {"@0000010000190002", 1}, {"@0000030003a60102", 1}, {"@0000040004c10102", 1}, {"@0000050004e90102", 1}, {"@00010000000c0002", 1}, {"@0001000000350102", 1}, {"@0001000404401c02", 1}, {"@0002000000010002", 1}, {"@0002000000360102", 1}, {"@0002000003720102", 1}, {"@0002000404411c02", 1}, {"@0002010003a70102", 1}, {"@0003000000020002", 1}, {"@0003000000370102", 1}, {"@0003000404421c02", 1}, {"@0003010200410302", 1}, {"@0003010200420302", 1}, {"@0003010200430302", 1}, {"@00030102023e0302", 1}, {"@0004000002620102", 1}, {"@0004010000130002", 1}, {"@0004010004ea0102", 1}, {"@0005000000140002", 1}, {"@0005000000390102", 1}, {"@0005000003730102", 1}, {"@0005ff00023a0702", 1}, {"@0006000000150002", 1}, {"@00070000001a0002", 1}, {"@0007000002630102", 1}, {"@0008000000030002", 1}, {"@0008000002640102", 1}, {"@00080100042f1a02", 1}, {"@0008ff00023b0702", 1}, {"@00090000000d0002", 1}, {"@0009000002650102", 1}, {"@0100000000040002", 1}, {"@01000000034b0902", 1}, {"@01000000034c0902", 1}, {"@01000000034d0902", 1}, {"@01000000034e0902", 1}, {"@01000000034f0902", 1}, {"@0100000003530902", 1}, {"@0100000003540902", 1}, {"@01000000037c0002", 1}, {"@0100000003990902", 1}, {"@0100000004180902", 1}, {"@0100010000160002", 1}, {"@0100010003500902", 1}, {"@01010000000e0002", 1}, {"@0101000003520902", 1}, {"@0101000003560902", 1}, {"@0101000004190902", 1}, {"@0101010000170002", 1}, {"@0101030004140902", 1}, {"@01020100001b0002", 1}, {"@01020100041a0902", 1}, {"@0180000000080002", 1}, {"@0580000000050002", 1}, {"@05810000001c0002", 1}, {"@05c0000000060002", 1}, {"@05c0000003651302", 1}, {"@05c0000004121302", 1}, {"@05c00000043a1302", 1}, {"@05c00000043b1302", 1}, {"@05c00100001d0002", 1}, {"@0600000000120002", 1}, {"@06400100001e0002", 1}, {"@06c00000000f0002", 1}, {"@0700000000070002", 1}, {"@0740000000100002", 1}, {"@0741000000200002", 1}, {"@07420000001f0002", 1}, {"@07800000002d0002", 1}, {"@07810000002e0002", 1}, {"@0781000000330002", 1}, {"@07820000002f0002", 1}, {"@07c0000000210002", 1}, {"@07c0010000220002", 1}, {"@07c0020000230002", 1}, {"@1906000000240002", 1}, {"@1919000000090002", 1}, {"@1927000000260002", 1}, {"@19960000023d0002", 1}, {"@1ac0000000110002", 1}, {"@1b92000000250002", 1}, {"@1f000000000a0002", 1}, {"@1f00000002540c02", 1}, {"@1f01000000270002", 1}, {"@1f01000002550c02", 1}, {"@1f02000000280002", 1}, {"@1f02000002560c02", 1}, {"@21000000000b0002", 1}, {"@2101000000180002", 1}, {"@2102000000290002", 1}, {"@21030000002a0002", 1}, {"@2104000002520002", 1}, {"@21050000025a0002", 1}, {"@2105010003630002", 1}, {"@22400000002b0002", 1}, {"@22800000002c0002", 1}, {"@2281000002510002", 1}, {"@3200000000300002", 1}, {"@32400000025b0002", 1}, {"@3240010003640002", 1}, {"@3340000000320002", 1}, {"@3480000000310002", 1}, {"@3480000002580002", 1}, {"@3480000003791502", 1}, {"@34c0000002530002", 1}, {"@34c0000104a81d02", 1}, {"@34c0000104cb1d02", 1}, {"@3600000002590002", 1}, {"@3600010003620002", 1} } },
	{"00040000001D1C00", { {"@0000000000000002", 1}, {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0000000002380602", 1}, {"@0000000002390602", 1}, {"@0000000003710102", 1}, {"@00000004043f1c02", 1}, {"@0000010000190002", 1}, {"@0000030003a60102", 1}, {"@0000040004c10102", 1}, {"@0000050004e90102", 1}, {"@00010000000c0002", 1}, {"@0001000000350102", 1}, {"@0001000404401c02", 1}, {"@0002000000010002", 1}, {"@0002000000360102", 1}, {"@0002000003720102", 1}, {"@0002000404411c02", 1}, {"@0002010003a70102", 1}, {"@0003000000020002", 1}, {"@0003000000370102", 1}, {"@0003000404421c02", 1}, {"@0003010200410302", 1}, {"@0003010200420302", 1}, {"@0003010200430302", 1}, {"@00030102023e0302", 1}, {"@0004000002620102", 1}, {"@0004010000130002", 1}, {"@0004010004ea0102", 1}, {"@0005000000140002", 1}, {"@0005000000390102", 1}, {"@0005000003730102", 1}, {"@0005ff00023a0702", 1}, {"@0006000000150002", 1}, {"@00070000001a0002", 1}, {"@0007000002630102", 1}, {"@0008000000030002", 1}, {"@0008000002640102", 1}, {"@00080100042f1a02", 1}, {"@0008ff00023b0702", 1}, {"@00090000000d0002", 1}, {"@0009000002650102", 1}, {"@000a000000380102", 1}, {"@000a010004c20102", 1}, {"@0013000002660102", 1}, {"@00130000037a0002", 1}, {"@0014000002670102", 1}, {"@0015000003670102", 1}, {"@0017000002680102", 1}, {"@0023000003680102", 1}, {"@00800102035d0302", 1}, {"@0100000000040002", 1}, {"@01000000034b0902", 1}, {"@01000000034c0902", 1}, {"@01000000034d0902", 1}, {"@01000000034e0902", 1}, {"@01000000034f0902", 1}, {"@0100000003530902", 1}, {"@0100000003540902", 1}, {"@01000000037c0002", 1}, {"@0100000003990902", 1}, {"@0100000004180902", 1}, {"@0100010000160002", 1}, {"@0100010003500902", 1}, {"@01010000000e0002", 1}, {"@0101000003520902", 1}, {"@0101000003560902", 1}, {"@0101000004190902", 1}, {"@0101010000170002", 1}, {"@0101030004140902", 1}, {"@01020100001b0002", 1}, {"@01020100041a0902", 1}, {"@01030000024f0902", 1}, {"@0105000003580902", 1}, {"@0106000003590902", 1}, {"@01070000035a0902", 1}, {"@01080000035b0902", 1}, {"@0140000003550902", 1}, {"@01410000035c0902", 1}, {"@0180000000080002", 1}, {"@01810000024b0502", 1}, {"@01810000037d0002", 1}, {"@01810100023f0502", 1}, {"@0182000002400502", 1}, {"@0183000002420502", 1}, {"@01840000024d0502", 1}, {"@0188000002410502", 1}, {"@018a000002450502", 1}, {"@018b000002460502", 1}, {"@018c000002430502", 1}, {"@018d0000024c0502", 1}, {"@018e000002490502", 1}, {"@0192000002470502", 1}, {"@0193000002480502", 1}, {"@01940000024a0502", 1}, {"@01960000024e0502", 1}, {"@01c1000002440502", 1}, {"@0580000000050002", 1}, {"@05810000001c0002", 1}, {"@05c0000000060002", 1}, {"@05c0000003651302", 1}, {"@05c0000004121302", 1}, {"@05c00000043a1302", 1}, {"@05c00000043b1302", 1}, {"@05c00100001d0002", 1}, {"@05c1000003661302", 1}, {"@0600000000120002", 1}, {"@06400100001e0002", 1}, {"@06420000035f1102", 1}, {"@06c00000000f0002", 1}, {"@0700000000070002", 1}, {"@07420000001f0002", 1}, {"@07800000002d0002", 1}, {"@07810000002e0002", 1}, {"@0781000000330002", 1}, {"@07820000002f0002", 1}, {"@08000100003e0402", 1}, {"@08000100025f0402", 1}, {"@0800010003690402", 1}, {"@0800010003820002", 1}, {"@0800010004150402", 1}, {"@08000200003f0402", 1}, {"@0800020002600402", 1}, {"@08000200036a0402", 1}, {"@0800030000400402", 1}, {"@0800030002610402", 1}, {"@08000300036b0402", 1}, {"@08010000025d0402", 1}, {"@0801000004360402", 1}, {"@08020000025e0402", 1}, {"@0802000004370402", 1}, {"@0803000003760402", 1}, {"@0803000004380402", 1}, {"@0804000003770402", 1}, {"@0804000004390402", 1} } },
	{"00040000001A4100", { {"@0000000000000002", 1}, {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0000000002380602", 1}, {"@0000000002390602", 1}, {"@0000000003710102", 1}, {"@00000004043f1c02", 1}, {"@0000010000190002", 1}, {"@0000030003a60102", 1}, {"@0000040004c10102", 1}, {"@0000050004e90102", 1}, {"@00010000000c0002", 1}, {"@0001000000350102", 1}, {"@0001000404401c02", 1}, {"@0002000000010002", 1}, {"@0002000000360102", 1}, {"@0002000003720102", 1}, {"@0002000404411c02", 1}, {"@0002010003a70102", 1}, {"@0003000000020002", 1}, {"@0003000000370102", 1}, {"@0003000404421c02", 1}, {"@0003010200410302", 1}, {"@0003010200420302", 1}, {"@0003010200420302", 1}, {"@0003010200430302", 1}, {"@0003010200430302", 1}, {"@00030102023e0302", 1}, {"@0004000002620102", 1}, {"@0004010000130002", 1}, {"@0004010004ea0102", 1}, {"@0005000000140002", 1}, {"@0005000000390102", 1}, {"@0005000003730102", 1}, {"@0005ff00023a0702", 1}, {"@0006000000150002", 1}, {"@00070000001a0002", 1}, {"@0007000002630102", 1}, {"@0008000000030002", 1}, {"@0008000002640102", 1}, {"@00080100042f1a02", 1}, {"@0008ff00023b0702", 1}, {"@00090000000d0002", 1}, {"@0009000002650102", 1}, {"@000a000000380102", 1}, {"@000a010004c20102", 1}, {"@0013000002660102", 1}, {"@00130000037a0002", 1}, {"@0014000002670102", 1}, {"@0017000002680102", 1}, {"@00800102035d0302", 1}, {"@0100000000040002", 1}, {"@01000000034b0902", 1}, {"@01000000034c0902", 1}, {"@01000000034d0902", 1}, {"@01000000034e0902", 1}, {"@01000000034f0902", 1}, {"@0100000003530902", 1}, {"@0100000003540902", 1}, {"@01000000037c0002", 1}, {"@0100000003990902", 1}, {"@0100000004180902", 1}, {"@0100010000160002", 1}, {"@0100010003500902", 1}, {"@01010000000e0002", 1}, {"@0101000003520902", 1}, {"@0101000003560902", 1}, {"@0101000004190902", 1}, {"@0101010000170002", 1}, {"@0101030004140902", 1}, {"@01020100001b0002", 1}, {"@01020100041a0902", 1}, {"@01030000024f0902", 1}, {"@0180000000080002", 1}, {"@01810000024b0502", 1}, {"@01810000037d0002", 1}, {"@0181000100440502", 1}, {"@0181000101d40502", 1}, {"@01810100023f0502", 1}, {"@0181010100b40502", 1}, {"@01810201011a0502", 1}, {"@0181030101700502", 1}, {"@0181040103aa0502", 1}, {"@0181050103bf0502", 1}, {"@0182000002400502", 1}, {"@0182000100a80502", 1}, {"@0182000101d80502", 1}, {"@0182000103b20502", 1}, {"@0182010100460502", 1}, {"@0183000002420502", 1}, {"@0183000100450502", 1}, {"@01830101010e0502", 1}, {"@0183020103a80502", 1}, {"@0183030103be0502", 1}, {"@01840000024d0502", 1}, {"@0188000002410502", 1}, {"@0188000101120502", 1}, {"@0188000103af0502", 1}, {"@018a000002450502", 1}, {"@018a000100a90502", 1}, {"@018b000002460502", 1}, {"@018b000101150502", 1}, {"@018c000002430502", 1}, {"@018c0001004c0502", 1}, {"@018c010101180502", 1}, {"@018d0000024c0502", 1}, {"@018d0001010c0502", 1}, {"@018e000002490502", 1}, {"@018e000100490502", 1}, {"@018e010101780502", 1}, {"@0192000002470502", 1}, {"@01920001010d0502", 1}, {"@0192000103ad0502", 1}, {"@0193000002480502", 1}, {"@0193000101740502", 1}, {"@0193000103ae0502", 1}, {"@01940000024a0502", 1}, {"@0194000100aa0502", 1}, {"@0194000103b60502", 1}, {"@01960000024e0502", 1}, {"@0196000100480502", 1}, {"@01c1000002440502", 1}, {"@01c1000100540502", 1}, {"@01c10101017a0502", 1}, {"@01c1020103bb0502", 1}, {"@026c000100c30502", 1}, {"@03710001005c0502", 1}, {"@0580000000050002", 1}, {"@05810000001c0002", 1}, {"@05c0000000060002", 1}, {"@05c0000003651302", 1}, {"@05c0000004121302", 1}, {"@05c00000043a1302", 1}, {"@05c00000043b1302", 1}, {"@05c00100001d0002", 1}, {"@0600000000120002", 1}, {"@06400100001e0002", 1}, {"@06c00000000f0002", 1}, {"@0700000000070002", 1}, {"@0740000000100002", 1}, {"@0741000000200002", 1}, {"@07420000001f0002", 1}, {"@07800000002d0002", 1}, {"@07810000002e0002", 1}, {"@0781000000330002", 1}, {"@07820000002f0002", 1}, {"@07c0000000210002", 1}, {"@07c0010000220002", 1}, {"@07c0020000230002", 1}, {"@08000100003e0402", 1}, {"@08000100025f0402", 1}, {"@0800010003690402", 1}, {"@0800010003820002", 1}, {"@0800010004150402", 1}, {"@08000200003f0402", 1}, {"@0800020002600402", 1}, {"@08000200036a0402", 1}, {"@0800030000400402", 1}, {"@0800030002610402", 1}, {"@08000300036b0402", 1}, {"@08010000025d0402", 1}, {"@0801000004360402", 1}, {"@08020000025e0402", 1}, {"@0802000004370402", 1}, {"@1f000000000a0002", 1}, {"@1f00000002540c02", 1}, {"@1f01000000270002", 1}, {"@1f01000002550c02", 1}, {"@1f02000000280002", 1}, {"@1f02000002560c02", 1}, {"@1f03000002570c02", 1}, {"@21000000000b0002", 1}, {"@2101000000180002", 1}, {"@2102000000290002", 1}, {"@21030000002a0002", 1}, {"@2104000002520002", 1}, {"@21050000025a0002", 1}, {"@2105010003630002", 1}, {"@22400000002b0002", 1}, {"@22800000002c0002", 1}, {"@2281000002510002", 1}, {"@3200000000300002", 1}, {"@3340000000320002", 1}, {"@3480000000310002", 1}, {"@3480000002580002", 1}, {"@3480000003791502", 1}, {"@34c0000002530002", 1}, {"@34c0000104a81d02", 1}, {"@34c0000104cb1d02", 1} } },
	{"00040000001A4200", { {"@0000000000000002", 1}, {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0000000002380602", 1}, {"@0000000002390602", 1}, {"@0000000003710102", 1}, {"@00000004043f1c02", 1}, {"@0000010000190002", 1}, {"@0000030003a60102", 1}, {"@0000040004c10102", 1}, {"@0000050004e90102", 1}, {"@00010000000c0002", 1}, {"@0001000000350102", 1}, {"@0001000404401c02", 1}, {"@0002000000010002", 1}, {"@0002000000360102", 1}, {"@0002000003720102", 1}, {"@0002000404411c02", 1}, {"@0002010003a70102", 1}, {"@0003000000020002", 1}, {"@0003000000370102", 1}, {"@0003000404421c02", 1}, {"@0003010200410302", 1}, {"@0003010200420302", 1}, {"@0003010200420302", 1}, {"@0003010200430302", 1}, {"@0003010200430302", 1}, {"@00030102023e0302", 1}, {"@0004000002620102", 1}, {"@0004010000130002", 1}, {"@0004010004ea0102", 1}, {"@0005000000140002", 1}, {"@0005000000390102", 1}, {"@0005000003730102", 1}, {"@0005ff00023a0702", 1}, {"@0006000000150002", 1}, {"@00070000001a0002", 1}, {"@0007000002630102", 1}, {"@0008000000030002", 1}, {"@0008000002640102", 1}, {"@00080100042f1a02", 1}, {"@0008ff00023b0702", 1}, {"@00090000000d0002", 1}, {"@0009000002650102", 1}, {"@000a000000380102", 1}, {"@000a010004c20102", 1}, {"@0013000002660102", 1}, {"@00130000037a0002", 1}, {"@0014000002670102", 1}, {"@0017000002680102", 1}, {"@00800102035d0302", 1}, {"@0100000000040002", 1}, {"@01000000034b0902", 1}, {"@01000000034c0902", 1}, {"@01000000034d0902", 1}, {"@01000000034e0902", 1}, {"@01000000034f0902", 1}, {"@0100000003530902", 1}, {"@0100000003540902", 1}, {"@01000000037c0002", 1}, {"@0100000003990902", 1}, {"@0100000004180902", 1}, {"@0100010000160002", 1}, {"@0100010003500902", 1}, {"@01010000000e0002", 1}, {"@0101000003520902", 1}, {"@0101000003560902", 1}, {"@0101000004190902", 1}, {"@0101010000170002", 1}, {"@0101030004140902", 1}, {"@01020100001b0002", 1}, {"@01020100041a0902", 1}, {"@01030000024f0902", 1}, {"@0180000000080002", 1}, {"@01810000024b0502", 1}, {"@01810000037d0002", 1}, {"@0181000100440502", 1}, {"@0181000101d40502", 1}, {"@01810100023f0502", 1}, {"@0181010100b40502", 1}, {"@01810201011a0502", 1}, {"@0181030101700502", 1}, {"@0181040103aa0502", 1}, {"@0181050103bf0502", 1}, {"@0182000002400502", 1}, {"@0182000100a80502", 1}, {"@0182000101d80502", 1}, {"@0182000103b20502", 1}, {"@0182010100460502", 1}, {"@0183000002420502", 1}, {"@0183000100450502", 1}, {"@01830101010e0502", 1}, {"@0183020103a80502", 1}, {"@0183030103be0502", 1}, {"@01840000024d0502", 1}, {"@0188000002410502", 1}, {"@0188000101120502", 1}, {"@0188000103af0502", 1}, {"@018a000002450502", 1}, {"@018a000100a90502", 1}, {"@018b000002460502", 1}, {"@018b000101150502", 1}, {"@018c000002430502", 1}, {"@018c0001004c0502", 1}, {"@018c010101180502", 1}, {"@018d0000024c0502", 1}, {"@018d0001010c0502", 1}, {"@018e000002490502", 1}, {"@018e000100490502", 1}, {"@018e010101780502", 1}, {"@0192000002470502", 1}, {"@01920001010d0502", 1}, {"@0192000103ad0502", 1}, {"@0193000002480502", 1}, {"@0193000101740502", 1}, {"@0193000103ae0502", 1}, {"@01940000024a0502", 1}, {"@0194000100aa0502", 1}, {"@0194000103b60502", 1}, {"@01960000024e0502", 1}, {"@0196000100480502", 1}, {"@01c1000002440502", 1}, {"@01c1000100540502", 1}, {"@01c10101017a0502", 1}, {"@01c1020103bb0502", 1}, {"@026c000100c30502", 1}, {"@03710001005c0502", 1}, {"@0580000000050002", 1}, {"@05810000001c0002", 1}, {"@05c0000000060002", 1}, {"@05c0000003651302", 1}, {"@05c0000004121302", 1}, {"@05c00000043a1302", 1}, {"@05c00000043b1302", 1}, {"@05c00100001d0002", 1}, {"@0600000000120002", 1}, {"@06400100001e0002", 1}, {"@06c00000000f0002", 1}, {"@0700000000070002", 1}, {"@0740000000100002", 1}, {"@0741000000200002", 1}, {"@07420000001f0002", 1}, {"@07800000002d0002", 1}, {"@07810000002e0002", 1}, {"@0781000000330002", 1}, {"@07820000002f0002", 1}, {"@07c0000000210002", 1}, {"@07c0010000220002", 1}, {"@07c0020000230002", 1}, {"@08000100003e0402", 1}, {"@08000100025f0402", 1}, {"@0800010003690402", 1}, {"@0800010003820002", 1}, {"@0800010004150402", 1}, {"@08000200003f0402", 1}, {"@0800020002600402", 1}, {"@08000200036a0402", 1}, {"@0800030000400402", 1}, {"@0800030002610402", 1}, {"@08000300036b0402", 1}, {"@08010000025d0402", 1}, {"@0801000004360402", 1}, {"@08020000025e0402", 1}, {"@0802000004370402", 1}, {"@1f000000000a0002", 1}, {"@1f00000002540c02", 1}, {"@1f01000000270002", 1}, {"@1f01000002550c02", 1}, {"@1f02000000280002", 1}, {"@1f02000002560c02", 1}, {"@1f03000002570c02", 1}, {"@21000000000b0002", 1}, {"@2101000000180002", 1}, {"@2102000000290002", 1}, {"@21030000002a0002", 1}, {"@2104000002520002", 1}, {"@21050000025a0002", 1}, {"@2105010003630002", 1}, {"@22400000002b0002", 1}, {"@22800000002c0002", 1}, {"@2281000002510002", 1}, {"@3200000000300002", 1}, {"@3340000000320002", 1}, {"@3480000000310002", 1}, {"@3480000002580002", 1}, {"@3480000003791502", 1}, {"@34c0000002530002", 1}, {"@34c0000104a81d02", 1}, {"@34c0000104cb1d02", 1} } },
	{"00040000001B6C00", { {"@0000000000000002", 1}, {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0000000002380602", 1}, {"@0000000002390602", 1}, {"@0000000003710102", 1}, {"@00000004043f1c02", 1}, {"@0000010000190002", 1}, {"@0000030003a60102", 1}, {"@0000040004c10102", 1}, {"@0000050004e90102", 1}, {"@00010000000c0002", 1}, {"@0001000000350102", 1}, {"@0001000404401c02", 1}, {"@0002000000010002", 1}, {"@0002000000360102", 1}, {"@0002000003720102", 1}, {"@0002000404411c02", 1}, {"@0002010003a70102", 1}, {"@0003000000020002", 1}, {"@0003000000370102", 1}, {"@0003000404421c02", 1}, {"@0003010200410302", 1}, {"@0003010200420302", 1}, {"@0003010200420302", 1}, {"@0003010200430302", 1}, {"@0003010200430302", 1}, {"@00030102023e0302", 1}, {"@0004000002620102", 1}, {"@0004010000130002", 1}, {"@0004010004ea0102", 1}, {"@0005000000140002", 1}, {"@0005000000390102", 1}, {"@0005000003730102", 1}, {"@0005ff00023a0702", 1}, {"@0006000000150002", 1}, {"@00070000001a0002", 1}, {"@0007000002630102", 1}, {"@0008000000030002", 1}, {"@0008000002640102", 1}, {"@00080100042f1a02", 1}, {"@0008ff00023b0702", 1}, {"@00090000000d0002", 1}, {"@0009000002650102", 1}, {"@000a000000380102", 1}, {"@000a010004c20102", 1}, {"@0013000002660102", 1}, {"@00130000037a0002", 1}, {"@0014000002670102", 1}, {"@0017000002680102", 1}, {"@00800102035d0302", 1}, {"@0100000000040002", 1}, {"@01000000034b0902", 1}, {"@01000000034c0902", 1}, {"@01000000034d0902", 1}, {"@01000000034e0902", 1}, {"@01000000034f0902", 1}, {"@0100000003530902", 1}, {"@0100000003540902", 1}, {"@01000000037c0002", 1}, {"@0100000003990902", 1}, {"@0100000004180902", 1}, {"@0100010000160002", 1}, {"@0100010003500902", 1}, {"@01010000000e0002", 1}, {"@0101000003520902", 1}, {"@0101000003560902", 1}, {"@0101000004190902", 1}, {"@0101010000170002", 1}, {"@0101030004140902", 1}, {"@01020100001b0002", 1}, {"@01020100041a0902", 1}, {"@01030000024f0902", 1}, {"@0180000000080002", 1}, {"@01810000024b0502", 1}, {"@01810000037d0002", 1}, {"@0181000100440502", 1}, {"@0181000101d40502", 1}, {"@01810100023f0502", 1}, {"@0181010100b40502", 1}, {"@01810201011a0502", 1}, {"@0181030101700502", 1}, {"@0181040103aa0502", 1}, {"@0181050103bf0502", 1}, {"@0182000002400502", 1}, {"@0182000100a80502", 1}, {"@0182000101d80502", 1}, {"@0182000103b20502", 1}, {"@0182010100460502", 1}, {"@0183000002420502", 1}, {"@0183000100450502", 1}, {"@01830101010e0502", 1}, {"@0183020103a80502", 1}, {"@0183030103be0502", 1}, {"@01840000024d0502", 1}, {"@0188000002410502", 1}, {"@0188000101120502", 1}, {"@0188000103af0502", 1}, {"@018a000002450502", 1}, {"@018a000100a90502", 1}, {"@018b000002460502", 1}, {"@018b000101150502", 1}, {"@018c000002430502", 1}, {"@018c0001004c0502", 1}, {"@018c010101180502", 1}, {"@018d0000024c0502", 1}, {"@018d0001010c0502", 1}, {"@018e000002490502", 1}, {"@018e000100490502", 1}, {"@018e010101780502", 1}, {"@0192000002470502", 1}, {"@01920001010d0502", 1}, {"@0192000103ad0502", 1}, {"@0193000002480502", 1}, {"@0193000101740502", 1}, {"@0193000103ae0502", 1}, {"@01940000024a0502", 1}, {"@0194000100aa0502", 1}, {"@0194000103b60502", 1}, {"@01960000024e0502", 1}, {"@0196000100480502", 1}, {"@01c1000002440502", 1}, {"@01c1000100540502", 1}, {"@01c10101017a0502", 1}, {"@01c1020103bb0502", 1}, {"@026c000100c30502", 1}, {"@03710001005c0502", 1}, {"@0580000000050002", 1}, {"@05810000001c0002", 1}, {"@05c0000000060002", 1}, {"@05c0000003651302", 1}, {"@05c0000004121302", 1}, {"@05c00000043a1302", 1}, {"@05c00000043b1302", 1}, {"@05c00100001d0002", 1}, {"@0600000000120002", 1}, {"@06400100001e0002", 1}, {"@06c00000000f0002", 1}, {"@0700000000070002", 1}, {"@0740000000100002", 1}, {"@0741000000200002", 1}, {"@07420000001f0002", 1}, {"@07800000002d0002", 1}, {"@07810000002e0002", 1}, {"@0781000000330002", 1}, {"@07820000002f0002", 1}, {"@07c0000000210002", 1}, {"@07c0010000220002", 1}, {"@07c0020000230002", 1}, {"@08000100003e0402", 1}, {"@08000100025f0402", 1}, {"@0800010003690402", 1}, {"@0800010003820002", 1}, {"@0800010004150402", 1}, {"@08000200003f0402", 1}, {"@0800020002600402", 1}, {"@08000200036a0402", 1}, {"@0800030000400402", 1}, {"@0800030002610402", 1}, {"@08000300036b0402", 1}, {"@08010000025d0402", 1}, {"@0801000004360402", 1}, {"@08020000025e0402", 1}, {"@0802000004370402", 1}, {"@1f000000000a0002", 1}, {"@1f00000002540c02", 1}, {"@1f01000000270002", 1}, {"@1f01000002550c02", 1}, {"@1f02000000280002", 1}, {"@1f02000002560c02", 1}, {"@1f03000002570c02", 1}, {"@21000000000b0002", 1}, {"@2101000000180002", 1}, {"@2102000000290002", 1}, {"@21030000002a0002", 1}, {"@2104000002520002", 1}, {"@21050000025a0002", 1}, {"@2105010003630002", 1}, {"@22400000002b0002", 1}, {"@22800000002c0002", 1}, {"@2281000002510002", 1}, {"@3200000000300002", 1}, {"@3340000000320002", 1}, {"@3480000000310002", 1}, {"@3480000002580002", 1}, {"@3480000003791502", 1}, {"@34c0000002530002", 1}, {"@34c0000104a81d02", 1}, {"@34c0000104cb1d02", 1} } },
	{"00040000001B6D00", { {"@0000000000000002", 1}, {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0000000002380602", 1}, {"@0000000002390602", 1}, {"@0000000003710102", 1}, {"@00000004043f1c02", 1}, {"@0000010000190002", 1}, {"@0000030003a60102", 1}, {"@0000040004c10102", 1}, {"@0000050004e90102", 1}, {"@00010000000c0002", 1}, {"@0001000000350102", 1}, {"@0001000404401c02", 1}, {"@0002000000010002", 1}, {"@0002000000360102", 1}, {"@0002000003720102", 1}, {"@0002000404411c02", 1}, {"@0002010003a70102", 1}, {"@0003000000020002", 1}, {"@0003000000370102", 1}, {"@0003000404421c02", 1}, {"@0003010200410302", 1}, {"@0003010200420302", 1}, {"@0003010200420302", 1}, {"@0003010200430302", 1}, {"@0003010200430302", 1}, {"@00030102023e0302", 1}, {"@0004000002620102", 1}, {"@0004010000130002", 1}, {"@0004010004ea0102", 1}, {"@0005000000140002", 1}, {"@0005000000390102", 1}, {"@0005000003730102", 1}, {"@0005ff00023a0702", 1}, {"@0006000000150002", 1}, {"@00070000001a0002", 1}, {"@0007000002630102", 1}, {"@0008000000030002", 1}, {"@0008000002640102", 1}, {"@00080100042f1a02", 1}, {"@0008ff00023b0702", 1}, {"@00090000000d0002", 1}, {"@0009000002650102", 1}, {"@000a000000380102", 1}, {"@000a010004c20102", 1}, {"@0013000002660102", 1}, {"@00130000037a0002", 1}, {"@0014000002670102", 1}, {"@0017000002680102", 1}, {"@00800102035d0302", 1}, {"@0100000000040002", 1}, {"@01000000034b0902", 1}, {"@01000000034c0902", 1}, {"@01000000034d0902", 1}, {"@01000000034e0902", 1}, {"@01000000034f0902", 1}, {"@0100000003530902", 1}, {"@0100000003540902", 1}, {"@01000000037c0002", 1}, {"@0100000003990902", 1}, {"@0100000004180902", 1}, {"@0100010000160002", 1}, {"@0100010003500902", 1}, {"@01010000000e0002", 1}, {"@0101000003520902", 1}, {"@0101000003560902", 1}, {"@0101000004190902", 1}, {"@0101010000170002", 1}, {"@0101030004140902", 1}, {"@01020100001b0002", 1}, {"@01020100041a0902", 1}, {"@01030000024f0902", 1}, {"@0180000000080002", 1}, {"@01810000024b0502", 1}, {"@01810000037d0002", 1}, {"@0181000100440502", 1}, {"@0181000101d40502", 1}, {"@01810100023f0502", 1}, {"@0181010100b40502", 1}, {"@01810201011a0502", 1}, {"@0181030101700502", 1}, {"@0181040103aa0502", 1}, {"@0181050103bf0502", 1}, {"@0182000002400502", 1}, {"@0182000100a80502", 1}, {"@0182000101d80502", 1}, {"@0182000103b20502", 1}, {"@0182010100460502", 1}, {"@0183000002420502", 1}, {"@0183000100450502", 1}, {"@01830101010e0502", 1}, {"@0183020103a80502", 1}, {"@0183030103be0502", 1}, {"@01840000024d0502", 1}, {"@0188000002410502", 1}, {"@0188000101120502", 1}, {"@0188000103af0502", 1}, {"@018a000002450502", 1}, {"@018a000100a90502", 1}, {"@018b000002460502", 1}, {"@018b000101150502", 1}, {"@018c000002430502", 1}, {"@018c0001004c0502", 1}, {"@018c010101180502", 1}, {"@018d0000024c0502", 1}, {"@018d0001010c0502", 1}, {"@018e000002490502", 1}, {"@018e000100490502", 1}, {"@018e010101780502", 1}, {"@0192000002470502", 1}, {"@01920001010d0502", 1}, {"@0192000103ad0502", 1}, {"@0193000002480502", 1}, {"@0193000101740502", 1}, {"@0193000103ae0502", 1}, {"@01940000024a0502", 1}, {"@0194000100aa0502", 1}, {"@0194000103b60502", 1}, {"@01960000024e0502", 1}, {"@0196000100480502", 1}, {"@01c1000002440502", 1}, {"@01c1000100540502", 1}, {"@01c10101017a0502", 1}, {"@01c1020103bb0502", 1}, {"@026c000100c30502", 1}, {"@03710001005c0502", 1}, {"@0580000000050002", 1}, {"@05810000001c0002", 1}, {"@05c0000000060002", 1}, {"@05c0000003651302", 1}, {"@05c0000004121302", 1}, {"@05c00000043a1302", 1}, {"@05c00000043b1302", 1}, {"@05c00100001d0002", 1}, {"@0600000000120002", 1}, {"@06400100001e0002", 1}, {"@06c00000000f0002", 1}, {"@0700000000070002", 1}, {"@0740000000100002", 1}, {"@0741000000200002", 1}, {"@07420000001f0002", 1}, {"@07800000002d0002", 1}, {"@07810000002e0002", 1}, {"@0781000000330002", 1}, {"@07820000002f0002", 1}, {"@07c0000000210002", 1}, {"@07c0010000220002", 1}, {"@07c0020000230002", 1}, {"@08000100003e0402", 1}, {"@08000100025f0402", 1}, {"@0800010003690402", 1}, {"@0800010003820002", 1}, {"@0800010004150402", 1}, {"@08000200003f0402", 1}, {"@0800020002600402", 1}, {"@08000200036a0402", 1}, {"@0800030000400402", 1}, {"@0800030002610402", 1}, {"@08000300036b0402", 1}, {"@08010000025d0402", 1}, {"@0801000004360402", 1}, {"@08020000025e0402", 1}, {"@0802000004370402", 1}, {"@1f000000000a0002", 1}, {"@1f00000002540c02", 1}, {"@1f01000000270002", 1}, {"@1f01000002550c02", 1}, {"@1f02000000280002", 1}, {"@1f02000002560c02", 1}, {"@1f03000002570c02", 1}, {"@21000000000b0002", 1}, {"@2101000000180002", 1}, {"@2102000000290002", 1}, {"@21030000002a0002", 1}, {"@2104000002520002", 1}, {"@21050000025a0002", 1}, {"@2105010003630002", 1}, {"@22400000002b0002", 1}, {"@22800000002c0002", 1}, {"@2281000002510002", 1}, {"@3200000000300002", 1}, {"@3340000000320002", 1}, {"@3480000000310002", 1}, {"@3480000002580002", 1}, {"@3480000003791502", 1}, {"@34c0000002530002", 1}, {"@34c0000104a81d02", 1}, {"@34c0000104cb1d02", 1} } },
	{"00040000001A9200", { {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0001000000350102", 1}, {"@0002000000360102", 1}, {"@0003000000370102", 1}, {"@0004000002620102", 1}, {"@0005000000390102", 1}, {"@0007000002630102", 1}, {"@0008000002640102", 1}, {"@0009000002650102", 1}, {"@000a000000380102", 1}, {"@0013000002660102", 1}, {"@0014000002670102", 1}, {"@0017000002680102", 1}, {"@01810000024b0502", 1}, {"@01810100023f0502", 1}, {"@0182000002400502", 1}, {"@0183000002420502", 1}, {"@01840000024d0502", 1}, {"@0188000002410502", 1}, {"@018a000002450502", 1}, {"@018b000002460502", 1}, {"@018c000002430502", 1}, {"@018d0000024c0502", 1}, {"@018e000002490502", 1}, {"@0192000002470502", 1}, {"@0193000002480502", 1}, {"@01940000024a0502", 1}, {"@01960000024e0502", 1}, {"@01c1000002440502", 1}, {"@06400100001e0002", 1}, {"@06420000035f1102", 1}, {"@08000100003e0402", 1}, {"@08000100025f0402", 1}, {"@08000200003f0402", 1}, {"@0800020002600402", 1}, {"@0800030000400402", 1}, {"@0800030002610402", 1}, {"@08010000025d0402", 1}, {"@08020000025e0402", 1} } },
	{"00040000001AF800", { {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0001000000350102", 1}, {"@0002000000360102", 1}, {"@0003000000370102", 1}, {"@0004000002620102", 1}, {"@0005000000390102", 1}, {"@0007000002630102", 1}, {"@0008000002640102", 1}, {"@0009000002650102", 1}, {"@000a000000380102", 1}, {"@0013000002660102", 1}, {"@0014000002670102", 1}, {"@0017000002680102", 1}, {"@01810000024b0502", 1}, {"@01810100023f0502", 1}, {"@0182000002400502", 1}, {"@0183000002420502", 1}, {"@01840000024d0502", 1}, {"@0188000002410502", 1}, {"@018a000002450502", 1}, {"@018b000002460502", 1}, {"@018c000002430502", 1}, {"@018d0000024c0502", 1}, {"@018e000002490502", 1}, {"@0192000002470502", 1}, {"@0193000002480502", 1}, {"@01940000024a0502", 1}, {"@01960000024e0502", 1}, {"@01c1000002440502", 1}, {"@06400100001e0002", 1}, {"@06420000035f1102", 1}, {"@08000100003e0402", 1}, {"@08000100025f0402", 1}, {"@08000200003f0402", 1}, {"@0800020002600402", 1}, {"@0800030000400402", 1}, {"@0800030002610402", 1}, {"@08010000025d0402", 1}, {"@08020000025e0402", 1} } },
	{"00040000001AFA00", { {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0001000000350102", 1}, {"@0002000000360102", 1}, {"@0003000000370102", 1}, {"@0004000002620102", 1}, {"@0005000000390102", 1}, {"@0007000002630102", 1}, {"@0008000002640102", 1}, {"@0009000002650102", 1}, {"@000a000000380102", 1}, {"@0013000002660102", 1}, {"@0014000002670102", 1}, {"@0017000002680102", 1}, {"@01810000024b0502", 1}, {"@01810100023f0502", 1}, {"@0182000002400502", 1}, {"@0183000002420502", 1}, {"@01840000024d0502", 1}, {"@0188000002410502", 1}, {"@018a000002450502", 1}, {"@018b000002460502", 1}, {"@018c000002430502", 1}, {"@018d0000024c0502", 1}, {"@018e000002490502", 1}, {"@0192000002470502", 1}, {"@0193000002480502", 1}, {"@01940000024a0502", 1}, {"@01960000024e0502", 1}, {"@01c1000002440502", 1}, {"@06400100001e0002", 1}, {"@06420000035f1102", 1}, {"@08000100003e0402", 1}, {"@08000100025f0402", 1}, {"@08000200003f0402", 1}, {"@0800020002600402", 1}, {"@0800030000400402", 1}, {"@0800030002610402", 1}, {"@08010000025d0402", 1}, {"@08020000025e0402", 1} } },
	{"00040000001C5900", { {"@0000000000340102", 1}, {"@00000000003c0102", 1}, {"@00000000003d0102", 1}, {"@0001000000350102", 1}, {"@0002000000360102", 1}, {"@0003000000370102", 1}, {"@0004000002620102", 1}, {"@0005000000390102", 1}, {"@0007000002630102", 1}, {"@0008000002640102", 1}, {"@0009000002650102", 1}, {"@000a000000380102", 1}, {"@0013000002660102", 1}, {"@0014000002670102", 1}, {"@0017000002680102", 1}, {"@01810000024b0502", 1}, {"@01810100023f0502", 1}, {"@0182000002400502", 1}, {"@0183000002420502", 1}, {"@01840000024d0502", 1}, {"@0188000002410502", 1}, {"@018a000002450502", 1}, {"@018b000002460502", 1}, {"@018c000002430502", 1}, {"@018d0000024c0502", 1}, {"@018e000002490502", 1}, {"@0192000002470502", 1}, {"@0193000002480502", 1}, {"@01940000024a0502", 1}, {"@01960000024e0502", 1}, {"@01c1000002440502", 1}, {"@06400100001e0002", 1}, {"@06420000035f1102", 1}, {"@08000100003e0402", 1}, {"@08000100025f0402", 1}, {"@08000200003f0402", 1}, {"@0800020002600402", 1}, {"@0800030000400402", 1}, {"@0800030002610402", 1}, {"@08010000025d0402", 1}, {"@08020000025e0402", 1} } },
	{"00040000001CB100", { {"@0000000003710102", 1}, {"@0002000003720102", 1}, {"@0005000003730102", 1}, {"@000a000000380102", 1} } },
	{"00040000001CB200", { {"@0000000003710102", 1}, {"@0002000003720102", 1}, {"@0005000003730102", 1}, {"@000a000000380102", 1} } },
	{"0004000000086300", { {"@0100000000040002", 1}, {"@01000000034b0902", 1}, {"@01000000034c0902", 1}, {"@01000000034d0902", 1}, {"@01000000034e0902", 1}, {"@01000000034f0902", 1}, {"@0100000003530902", 1}, {"@0100000003540902", 1}, {"@01000000037c0002", 1}, {"@0100000003990902", 1}, {"@0100000004180902", 1}, {"@0100010000160002", 1}, {"@0100010003500902", 1}, {"@01010000000e0002", 1}, {"@0101000003520902", 1}, {"@0101000003560902", 1}, {"@0101000004190902", 1}, {"@0101010000170002", 1}, {"@0101030004140902", 1}, {"@01020100001b0002", 1}, {"@01020100041a0902", 1}, {"@01030000024f0902", 1}, {"@0140000003550902", 1}, {"@01410000035c0902", 1}, {"@0180000000080002", 1}, {"@01810000024b0502", 1}, {"@01810000037d0002", 1}, {"@0181000100440502", 1}, {"@0181000101d40502", 1}, {"@01810100023f0502", 1}, {"@0181010100b40502", 1}, {"@01810201011a0502", 1}, {"@0181030101700502", 1}, {"@0181040103aa0502", 1}, {"@0181050103bf0502", 1}, {"@0182000002400502", 1}, {"@0182000100a80502", 1}, {"@0182000101d80502", 1}, {"@0182000103b20502", 1}, {"@0182010100460502", 1}, {"@0183000002420502", 1}, {"@0183000100450502", 1}, {"@01830101010e0502", 1}, {"@0183020103a80502", 1}, {"@0183030103be0502", 1}, {"@01840000024d0502", 1}, {"@0184050103a90502", 1}, {"@01850001004b0502", 1}, {"@0185020101170502", 1}, {"@0185040101790502", 1}, {"@0186010100af0502", 1}, {"@0186030101750502", 1}, {"@0187000100470502", 1}, {"@0187000103b00502", 1}, {"@0188000002410502", 1}, {"@0188000101120502", 1}, {"@0188000103af0502", 1}, {"@0189000100ab0502", 1}, {"@0189010103b10502", 1}, {"@018a000002450502", 1}, {"@018a000100a90502", 1}, {"@018b000002460502", 1}, {"@018b000101150502", 1}, {"@018c000002430502", 1}, {"@018c0001004c0502", 1}, {"@018c010101180502", 1}, {"@018d0000024c0502", 1}, {"@018d0001010c0502", 1}, {"@018e000002490502", 1}, {"@018e000100490502", 1}, {"@018e010101780502", 1}, {"@018f000100b30502", 1}, {"@018f010101190502", 1}, {"@0190000101710502", 1}, {"@01910001004e0502", 1}, {"@0192000002470502", 1}, {"@01920001010d0502", 1}, {"@0192000103ad0502", 1}, {"@0193000002480502", 1}, {"@0193000101740502", 1}, {"@0193000103ae0502", 1}, {"@01940000024a0502", 1}, {"@0194000100aa0502", 1}, {"@0194000103b60502", 1}, {"@0195000100b00502", 1}, {"@01960000024e0502", 1}, {"@0196000100480502", 1}, {"@0197000101770502", 1}, {"@0198000100b10502", 1}, {"@0199000101160502", 1}, {"@019a000100b70502", 1}, {"@019b000100b60502", 1}, {"@019c000101730502", 1}, {"@019d000100ac0502", 1}, {"@019e000100ad0502", 1}, {"@019f000101110502", 1}, {"@01a00001010f0502", 1}, {"@01a1000101100502", 1}, {"@01a20001017d0502", 1}, {"@01a2000103b90502", 1}, {"@01a30001004a0502", 1}, {"@01a40001004d0502", 1}, {"@01a5000101720502", 1}, {"@01a6000100500502", 1}, {"@01a6000103b70502", 1}, {"@01a7000101140502", 1}, {"@01a80001004f0502", 1}, {"@01a80101017e0502", 1}, {"@01a9000101760502", 1}, {"@01aa000100530502", 1}, {"@01ab0001017c0502", 1}, {"@01ac0001017f0502", 1}, {"@01ad000100b80502", 1}, {"@01ae0001011b0502", 1}, {"@01af0001011c0502", 1}, {"@01b0000100520502", 1}, {"@01b1000100b20502", 1}, {"@01b10101017b0502", 1}, {"@01b3000100b50502", 1}, {"@01b4000101130502", 1}, {"@01b5000100510502", 1}, {"@01b6000100ae0502", 1}, {"@01c1000002440502", 1}, {"@01c1000100540502", 1}, {"@01c10101017a0502", 1}, {"@01c1020103bb0502", 1}, {"@0200000100a10502", 1}, {"@02010001016a0502", 1}, {"@0202000101030502", 1}, {"@02030001019a0502", 1}, {"@0206000103120502", 1}, {"@0208000100960502", 1}, {"@02090001019f0502", 1}, {"@0214000100e40502", 1}, {"@0215000101820502", 1}, {"@0216000100570502", 1}, {"@0217000101b30502", 1}, {"@02190001007e0502", 1}, {"@021a000100da0502", 1}, {"@021b000100800502", 1}, {"@021c000102f70502", 1}, {"@021d000101cd0502", 1}, {"@021e000101230502", 1}, {"@021f000103170502", 1}, {"@0220000100fd0502", 1}, {"@02210001013c0502", 1}, {"@0222000101440502", 1}, {"@022d000100f20502", 1}, {"@022e000101d30502", 1}, {"@022f0001011e0502", 1}, {"@0230000101d20502", 1}, {"@02310001006a0502", 1}, {"@0232000102ea0502", 1}, {"@0233000103060502", 1}, {"@0235000100840502", 1}, {"@0238000102f80502", 1}, {"@023c000100bd0502", 1}, {"@023d000101b50502", 1}, {"@023e000100d10502", 1}, {"@023f000101660502", 1}, {"@024a000101d10502", 1}, {"@024b000101260502", 1}, {"@024d000102f60502", 1}, {"@024f000100810502", 1}, {"@0251000100c10502", 1}, {"@0252000100fe0502", 1}, {"@025d000100550502", 1}, {"@025e000101250502", 1}, {"@025f000101c50502", 1}, {"@025f000101d70502", 1}, {"@0260000100d20502", 1}, {"@0261000100650502", 1}, {"@0262000101370502", 1}, {"@0263000100750502", 1}, {"@0264000101ac0502", 1}, {"@0265000101540502", 1}, {"@0266000100680502", 1}, {"@0267000101080502", 1}, {"@02680001007d0502", 1}, {"@02690001011f0502", 1}, {"@026a000101460502", 1}, {"@026b000100e90502", 1}, {"@026c000100c30502", 1}, {"@026d0001013f0502", 1}, {"@026e000100ba0502", 1}, {"@026f000101900502", 1}, {"@0270000100ff0502", 1}, {"@02710001019b0502", 1}, {"@0272000101860502", 1}, {"@027d000100630502", 1}, {"@027e000101690502", 1}, {"@027f000100b90502", 1}, {"@0280000100830502", 1}, {"@0281000101200502", 1}, {"@0282000101810502", 1}, {"@0282000101d60502", 1}, {"@0283000100c70502", 1}, {"@0284000102fe0502", 1}, {"@0286000103130502", 1}, {"@02870001005a0502", 1}, {"@028a000102e90502", 1}, {"@028b000100e30502", 1}, {"@028c0001013e0502", 1}, {"@028d000101bd0502", 1}, {"@028e0001019e0502", 1}, {"@028f0101031a0502", 1}, {"@0299000100950502", 1}, {"@029a000100ee0502", 1}, {"@029b000100cb0502", 1}, {"@029e0001013d0502", 1}, {"@02a2000101ba0502", 1}, {"@02a3000102ff0502", 1}, {"@02a4000100720502", 1}, {"@02a50001018c0502", 1}, {"@02a6000101240502", 1}, {"@02b1000100690502", 1}, {"@02b2000100c40502", 1}, {"@02b70001030f0502", 1}, {"@02b80001019c0502", 1}, {"@02c3000100dc0502", 1}, {"@02c4000100670502", 1}, {"@02c5000103080502", 1}, {"@02c7000101220502", 1}, {"@02c9000100cd0502", 1}, {"@02ca000101ca0502", 1}, {"@02cb000101360502", 1}, {"@02d6000100560502", 1}, {"@02d7000101300502", 1}, {"@02d8000100e20502", 1}, {"@02d9000101c80502", 1}, {"@02da000101330502", 1}, {"@02db0001005e0502", 1}, {"@02dc000100be0502", 1}, {"@02dd000100ea0502", 1}, {"@02de0001009c0502", 1}, {"@02df000101910502", 1}, {"@02e00101031d0502", 1}, {"@02ea000101800502", 1}, {"@02ea000101d50502", 1}, {"@02eb000100de0502", 1}, {"@02ec000101c40502", 1}, {"@02ed0001015a0502", 1}, {"@02ee000101990502", 1}, {"@02ef000100580502", 1}, {"@02f0000100a70502", 1}, {"@02f1000101450502", 1}, {"@02f2000100cc0502", 1}, {"@02f3000102f90502", 1}, {"@02f4000103050502", 1}, {"@02f8000101380502", 1}, {"@02f9000101020502", 1}, {"@02fa000100970502", 1}, {"@02fb000100900502", 1}, {"@02fc0001018f0502", 1}, {"@0307000100640502", 1}, {"@03080001014d0502", 1}, {"@0309000100c60502", 1}, {"@030a000101c70502", 1}, {"@030b000100790502", 1}, {"@030c000101b80502", 1}, {"@030d000101840502", 1}, {"@030e0001012f0502", 1}, {"@030f0001016d0502", 1}, {"@0310000100f80502", 1}, {"@0311000100d60502", 1}, {"@0312000103090502", 1}, {"@0313000101210502", 1}, {"@0314000102f40502", 1}, {"@0316000101c00502", 1}, {"@0317000100a60502", 1}, {"@03180001006c0502", 1}, {"@0323000100760502", 1}, {"@0324000101890502", 1}, {"@03250001010a0502", 1}, {"@0326000101390502", 1}, {"@0327000101c30502", 1}, {"@0328000102eb0502", 1}, {"@03290001009d0502", 1}, {"@032a000103070502", 1}, {"@032c000101480502", 1}, {"@032d000100bc0502", 1}, {"@032e0101031c0502", 1}, {"@03380001011d0502", 1}, {"@0339000101b10502", 1}, {"@033a000101cc0502", 1}, {"@033b000100fa0502", 1}, {"@033c000101000502", 1}, {"@033d0001013a0502", 1}, {"@033e000101a20502", 1}, {"@033f0001008f0502", 1}, {"@03410001030e0502", 1}, {"@0342000101280502", 1}, {"@0343000102ef0502", 1}, {"@0344000100c50502", 1}, {"@03450001005f0502", 1}, {"@0347000103020502", 1}, {"@03480001006b0502", 1}, {"@03490001018d0502", 1}, {"@034a000101430502", 1}, {"@034b0001009f0502", 1}, {"@0356000101350502", 1}, {"@0357000100eb0502", 1}, {"@0358000102fa0502", 1}, {"@035a000100850502", 1}, {"@035c000101290502", 1}, {"@035d000100c90502", 1}, {"@035e0001018e0502", 1}, {"@0369000100d30502", 1}, {"@036a0001019d0502", 1}, {"@036b0001018b0502", 1}, {"@036d000103040502", 1}, {"@036e000102fb0502", 1}, {"@03700001015d0502", 1}, {"@03710001005c0502", 1}, {"@03720001010b0502", 1}, {"@0373000101340502", 1}, {"@0374010103190502", 1}, {"@037e000101560502", 1}, {"@037f000101aa0502", 1}, {"@0380000101870502", 1}, {"@0381000100d50502", 1}, {"@03820001016b0502", 1}, {"@03830001009b0502", 1}, {"@0384000100860502", 1}, {"@0385000101060502", 1}, {"@0390000101850502", 1}, {"@0392000101270502", 1}, {"@0393000100a00502", 1}, {"@0394000100890502", 1}, {"@0395000102fc0502", 1}, {"@0398000100bf0502", 1}, {"@0399000101c20502", 1}, {"@03a40001014f0502", 1}, {"@03a50001015b0502", 1}, {"@03a6000100c80502", 1}, {"@03a7000101a10502", 1}, {"@03a8000100910502", 1}, {"@03a9000100710502", 1}, {"@03aa000100e60502", 1}, {"@03ab000103160502", 1}, {"@03ac000101880502", 1}, {"@03ad000101b20502", 1}, {"@03ae000100870502", 1}, {"@03af0001012c0502", 1}, {"@03b0000101a90502", 1}, {"@03b1000100f00502", 1}, {"@03bc0001008a0502", 1}, {"@03bd000100f90502", 1}, {"@03be000101980502", 1}, {"@03bf000101bc0502", 1}, {"@03c0000103100502", 1}, {"@03c1000100bb0502", 1}, {"@03c40001012b0502", 1}, {"@03c50001015c0502", 1}, {"@03c6000100930502", 1}, {"@03d1000100c20502", 1}, {"@03d2000100e50502", 1}, {"@03d3000102f30502", 1}, {"@03d6000101570502", 1}, {"@03d7000101b40502", 1}, {"@03d9000101a50502", 1}, {"@03da000101510502", 1}, {"@03db0001006d0502", 1}, {"@03e6000100ec0502", 1}, {"@03e70001012a0502", 1}, {"@03e8000102f50502", 1}, {"@03ea0001030b0502", 1}, {"@03ec000101830502", 1}, {"@03ed000101a30502", 1}, {"@03ee0001008b0502", 1}, {"@03fa000100d00502", 1}, {"@03fb000101cf0502", 1}, {"@03fc000101470502", 1}, {"@03fd000101580502", 1}, {"@03fe000101a40502", 1}, {"@03ff000100f40502", 1}, {"@04000001006f0502", 1}, {"@0401000100660502", 1}, {"@040c000101590502", 1}, {"@040d000100780502", 1}, {"@040e000100880502", 1}, {"@040f000101500502", 1}, {"@04100001007f0502", 1}, {"@0411000101ab0502", 1}, {"@04140001030a0502", 1}, {"@0415000101bb0502", 1}, {"@0416000100fb0502", 1}, {"@0418000100d80502", 1}, {"@041a000100e00502", 1}, {"@041b000100f10502", 1}, {"@041c000101410502", 1}, {"@041d0001018a0502", 1}, {"@041e0001015f0502", 1}, {"@0429000100700502", 1}, {"@042a0001012d0502", 1}, {"@042b000101af0502", 1}, {"@0436000101940502", 1}, {"@0437000101050502", 1}, {"@0438000103000502", 1}, {"@0439000103110502", 1}, {"@043b000103030502", 1}, {"@043c000101cb0502", 1}, {"@043d0001007c0502", 1}, {"@043e000101490502", 1}, {"@043f000101550502", 1}, {"@0440000100ca0502", 1}, {"@044b0001016c0502", 1}, {"@044c0001008e0502", 1}, {"@044d000101930502", 1}, {"@044e000103150502", 1}, {"@0450000100cf0502", 1}, {"@04510001015e0502", 1}, {"@0452000100730502", 1}, {"@0453000101040502", 1}, {"@0454000101ae0502", 1}, {"@045f000101a80502", 1}, {"@0460000100a50502", 1}, {"@0461000101610502", 1}, {"@0462000100f60502", 1}, {"@0463000101310502", 1}, {"@0464000100c00502", 1}, {"@04650001006e0502", 1}, {"@0468000102f20502", 1}, {"@0469000101640502", 1}, {"@046a000101d00502", 1}, {"@046b000101970502", 1}, {"@046c0001008c0502", 1}, {"@046d000100f30502", 1}, {"@0478000101630502", 1}, {"@0479000100920502", 1}, {"@047a000100600502", 1}, {"@047b000100f50502", 1}, {"@047c000101a00502", 1}, {"@047d0001012e0502", 1}, {"@04800001008d0502", 1}, {"@0481000102f10502", 1}, {"@0482000102fd0502", 1}, {"@0483000101b00502", 1}, {"@04850001014c0502", 1}, {"@0486000100fc0502", 1}, {"@0487000101bf0502", 1}, {"@0488000100980502", 1}, {"@0489000100ef0502", 1}, {"@04940001009a0502", 1}, {"@0495000101920502", 1}, {"@0496000100d90502", 1}, {"@04970001007a0502", 1}, {"@04980001014a0502", 1}, {"@0499000100df0502", 1}, {"@049a0001014e0502", 1}, {"@049b000100610502", 1}, {"@049c000101400502", 1}, {"@049d000100ed0502", 1}, {"@049e000101b70502", 1}, {"@049f000103010502", 1}, {"@04a00001016e0502", 1}, {"@04a10001016f0502", 1}, {"@04a2000102e80502", 1}, {"@04a3000101c90502", 1}, {"@04a4000100d40502", 1}, {"@04a5000100740502", 1}, {"@04a6000100a30502", 1}, {"@04a7000101a60502", 1}, {"@04a80101031e0502", 1}, {"@04b2000101b90502", 1}, {"@04b3000100dd0502", 1}, {"@04b40001030c0502", 1}, {"@04b6000102ec0502", 1}, {"@04b9000101600502", 1}, {"@04ba0001005d0502", 1}, {"@04c5000101010502", 1}, {"@04c6000101670502", 1}, {"@04c7000100940502", 1}, {"@04c8000102ed0502", 1}, {"@04c90001030d0502", 1}, {"@04cc000100a40502", 1}, {"@04cd000101520502", 1}, {"@04ce000100db0502", 1}, {"@04cf000100e10502", 1}, {"@04d0000101960502", 1}, {"@04d10001009e0502", 1}, {"@04d2000101a70502", 1}, {"@04d30101031b0502", 1}, {"@04dd000100a20502", 1}, {"@04de000100ce0502", 1}, {"@04df000100e80502", 1}, {"@04e0000100f70502", 1}, {"@04e1000101be0502", 1}, {"@04e2000101090502", 1}, {"@04e3000101650502", 1}, {"@04e4000101b60502", 1}, {"@04e5000101ad0502", 1}, {"@04e6000100820502", 1}, {"@04e7000101320502", 1}, {"@04e8000101ce0502", 1}, {"@04ea000103180502", 1}, {"@04eb000102f00502", 1}, {"@04ec000100770502", 1}, {"@04ed000100620502", 1}, {"@04ee0001014b0502", 1}, {"@04ef0001013b0502", 1}, {"@04fa000101680502", 1}, {"@04fb000101c60502", 1}, {"@04fc000102ee0502", 1}, {"@04fd0001007b0502", 1}, {"@04fe000100590502", 1}, {"@04ff000101620502", 1}, {"@0500000100e70502", 1}, {"@050b000100990502", 1}, {"@050c000101c10502", 1}, {"@050d000101420502", 1}, {"@050e000100d70502", 1}, {"@050f000103140502", 1}, {"@0510000101070502", 1}, {"@0511000101950502", 1}, {"@0513000102e70502", 1}, {"@0514000101530502", 1}, {"@05150001005b0502", 1}, {"@08000100003e0402", 1}, {"@08000100025f0402", 1}, {"@0800010003690402", 1}, {"@0800010003820002", 1}, {"@0800010004150402", 1}, {"@08000200003f0402", 1}, {"@0800020002600402", 1}, {"@08000200036a0402", 1}, {"@0800030000400402", 1}, {"@0800030002610402", 1}, {"@08000300036b0402", 1}, {"@08010000025d0402", 1}, {"@0801000004360402", 1}, {"@08020000025e0402", 1}, {"@0802000004370402", 1}, {"@0a12000103c90502", 1}, {"@0a1c000103d30502", 1}, {"@3500010002e10f02", 1}, {"@3500020002e20f02", 1}, {"@3501000002e30f02", 1}, {"@3502010002e40f02", 1}, {"@3503010002e50f02", 1}, {"@3504010002e60f02", 1} } },
	{"0004000000086400", { {"@0100000000040002", 1}, {"@01000000034b0902", 1}, {"@01000000034c0902", 1}, {"@01000000034d0902", 1}, {"@01000000034e0902", 1}, {"@01000000034f0902", 1}, {"@0100000003530902", 1}, {"@0100000003540902", 1}, {"@01000000037c0002", 1}, {"@0100000003990902", 1}, {"@0100000004180902", 1}, {"@0100010000160002", 1}, {"@0100010003500902", 1}, {"@01010000000e0002", 1}, {"@0101000003520902", 1}, {"@0101000003560902", 1}, {"@0101000004190902", 1}, {"@0101010000170002", 1}, {"@0101030004140902", 1}, {"@01020100001b0002", 1}, {"@01020100041a0902", 1}, {"@01030000024f0902", 1}, {"@0140000003550902", 1}, {"@01410000035c0902", 1}, {"@0180000000080002", 1}, {"@01810000024b0502", 1}, {"@01810000037d0002", 1}, {"@0181000100440502", 1}, {"@0181000101d40502", 1}, {"@01810100023f0502", 1}, {"@0181010100b40502", 1}, {"@01810201011a0502", 1}, {"@0181030101700502", 1}, {"@0181040103aa0502", 1}, {"@0181050103bf0502", 1}, {"@0182000002400502", 1}, {"@0182000100a80502", 1}, {"@0182000101d80502", 1}, {"@0182000103b20502", 1}, {"@0182010100460502", 1}, {"@0183000002420502", 1}, {"@0183000100450502", 1}, {"@01830101010e0502", 1}, {"@0183020103a80502", 1}, {"@0183030103be0502", 1}, {"@01840000024d0502", 1}, {"@0184050103a90502", 1}, {"@01850001004b0502", 1}, {"@0185020101170502", 1}, {"@0185040101790502", 1}, {"@0186010100af0502", 1}, {"@0186030101750502", 1}, {"@0187000100470502", 1}, {"@0187000103b00502", 1}, {"@0188000002410502", 1}, {"@0188000101120502", 1}, {"@0188000103af0502", 1}, {"@0189000100ab0502", 1}, {"@0189010103b10502", 1}, {"@018a000002450502", 1}, {"@018a000100a90502", 1}, {"@018b000002460502", 1}, {"@018b000101150502", 1}, {"@018c000002430502", 1}, {"@018c0001004c0502", 1}, {"@018c010101180502", 1}, {"@018d0000024c0502", 1}, {"@018d0001010c0502", 1}, {"@018e000002490502", 1}, {"@018e000100490502", 1}, {"@018e010101780502", 1}, {"@018f000100b30502", 1}, {"@018f010101190502", 1}, {"@0190000101710502", 1}, {"@01910001004e0502", 1}, {"@0192000002470502", 1}, {"@01920001010d0502", 1}, {"@0192000103ad0502", 1}, {"@0193000002480502", 1}, {"@0193000101740502", 1}, {"@0193000103ae0502", 1}, {"@01940000024a0502", 1}, {"@0194000100aa0502", 1}, {"@0194000103b60502", 1}, {"@0195000100b00502", 1}, {"@01960000024e0502", 1}, {"@0196000100480502", 1}, {"@0197000101770502", 1}, {"@0198000100b10502", 1}, {"@0199000101160502", 1}, {"@019a000100b70502", 1}, {"@019b000100b60502", 1}, {"@019c000101730502", 1}, {"@019d000100ac0502", 1}, {"@019e000100ad0502", 1}, {"@019f000101110502", 1}, {"@01a00001010f0502", 1}, {"@01a1000101100502", 1}, {"@01a20001017d0502", 1}, {"@01a2000103b90502", 1}, {"@01a30001004a0502", 1}, {"@01a40001004d0502", 1}, {"@01a5000101720502", 1}, {"@01a6000100500502", 1}, {"@01a6000103b70502", 1}, {"@01a7000101140502", 1}, {"@01a80001004f0502", 1}, {"@01a80101017e0502", 1}, {"@01a9000101760502", 1}, {"@01aa000100530502", 1}, {"@01ab0001017c0502", 1}, {"@01ac0001017f0502", 1}, {"@01ad000100b80502", 1}, {"@01ae0001011b0502", 1}, {"@01af0001011c0502", 1}, {"@01b0000100520502", 1}, {"@01b1000100b20502", 1}, {"@01b10101017b0502", 1}, {"@01b3000100b50502", 1}, {"@01b4000101130502", 1}, {"@01b5000100510502", 1}, {"@01b6000100ae0502", 1}, {"@01c1000002440502", 1}, {"@01c1000100540502", 1}, {"@01c10101017a0502", 1}, {"@01c1020103bb0502", 1}, {"@0200000100a10502", 1}, {"@02010001016a0502", 1}, {"@0202000101030502", 1}, {"@02030001019a0502", 1}, {"@0206000103120502", 1}, {"@0208000100960502", 1}, {"@02090001019f0502", 1}, {"@0214000100e40502", 1}, {"@0215000101820502", 1}, {"@0216000100570502", 1}, {"@0217000101b30502", 1}, {"@02190001007e0502", 1}, {"@021a000100da0502", 1}, {"@021b000100800502", 1}, {"@021c000102f70502", 1}, {"@021d000101cd0502", 1}, {"@021e000101230502", 1}, {"@021f000103170502", 1}, {"@0220000100fd0502", 1}, {"@02210001013c0502", 1}, {"@0222000101440502", 1}, {"@022d000100f20502", 1}, {"@022e000101d30502", 1}, {"@022f0001011e0502", 1}, {"@0230000101d20502", 1}, {"@02310001006a0502", 1}, {"@0232000102ea0502", 1}, {"@0233000103060502", 1}, {"@0235000100840502", 1}, {"@0238000102f80502", 1}, {"@023c000100bd0502", 1}, {"@023d000101b50502", 1}, {"@023e000100d10502", 1}, {"@023f000101660502", 1}, {"@024a000101d10502", 1}, {"@024b000101260502", 1}, {"@024d000102f60502", 1}, {"@024f000100810502", 1}, {"@0251000100c10502", 1}, {"@0252000100fe0502", 1}, {"@025d000100550502", 1}, {"@025e000101250502", 1}, {"@025f000101c50502", 1}, {"@025f000101d70502", 1}, {"@0260000100d20502", 1}, {"@0261000100650502", 1}, {"@0262000101370502", 1}, {"@0263000100750502", 1}, {"@0264000101ac0502", 1}, {"@0265000101540502", 1}, {"@0266000100680502", 1}, {"@0267000101080502", 1}, {"@02680001007d0502", 1}, {"@02690001011f0502", 1}, {"@026a000101460502", 1}, {"@026b000100e90502", 1}, {"@026c000100c30502", 1}, {"@026d0001013f0502", 1}, {"@026e000100ba0502", 1}, {"@026f000101900502", 1}, {"@0270000100ff0502", 1}, {"@02710001019b0502", 1}, {"@0272000101860502", 1}, {"@027d000100630502", 1}, {"@027e000101690502", 1}, {"@027f000100b90502", 1}, {"@0280000100830502", 1}, {"@0281000101200502", 1}, {"@0282000101810502", 1}, {"@0282000101d60502", 1}, {"@0283000100c70502", 1}, {"@0284000102fe0502", 1}, {"@0286000103130502", 1}, {"@02870001005a0502", 1}, {"@028a000102e90502", 1}, {"@028b000100e30502", 1}, {"@028c0001013e0502", 1}, {"@028d000101bd0502", 1}, {"@028e0001019e0502", 1}, {"@028f0101031a0502", 1}, {"@0299000100950502", 1}, {"@029a000100ee0502", 1}, {"@029b000100cb0502", 1}, {"@029e0001013d0502", 1}, {"@02a2000101ba0502", 1}, {"@02a3000102ff0502", 1}, {"@02a4000100720502", 1}, {"@02a50001018c0502", 1}, {"@02a6000101240502", 1}, {"@02b1000100690502", 1}, {"@02b2000100c40502", 1}, {"@02b70001030f0502", 1}, {"@02b80001019c0502", 1}, {"@02c3000100dc0502", 1}, {"@02c4000100670502", 1}, {"@02c5000103080502", 1}, {"@02c7000101220502", 1}, {"@02c9000100cd0502", 1}, {"@02ca000101ca0502", 1}, {"@02cb000101360502", 1}, {"@02d6000100560502", 1}, {"@02d7000101300502", 1}, {"@02d8000100e20502", 1}, {"@02d9000101c80502", 1}, {"@02da000101330502", 1}, {"@02db0001005e0502", 1}, {"@02dc000100be0502", 1}, {"@02dd000100ea0502", 1}, {"@02de0001009c0502", 1}, {"@02df000101910502", 1}, {"@02e00101031d0502", 1}, {"@02ea000101800502", 1}, {"@02ea000101d50502", 1}, {"@02eb000100de0502", 1}, {"@02ec000101c40502", 1}, {"@02ed0001015a0502", 1}, {"@02ee000101990502", 1}, {"@02ef000100580502", 1}, {"@02f0000100a70502", 1}, {"@02f1000101450502", 1}, {"@02f2000100cc0502", 1}, {"@02f3000102f90502", 1}, {"@02f4000103050502", 1}, {"@02f8000101380502", 1}, {"@02f9000101020502", 1}, {"@02fa000100970502", 1}, {"@02fb000100900502", 1}, {"@02fc0001018f0502", 1}, {"@0307000100640502", 1}, {"@03080001014d0502", 1}, {"@0309000100c60502", 1}, {"@030a000101c70502", 1}, {"@030b000100790502", 1}, {"@030c000101b80502", 1}, {"@030d000101840502", 1}, {"@030e0001012f0502", 1}, {"@030f0001016d0502", 1}, {"@0310000100f80502", 1}, {"@0311000100d60502", 1}, {"@0312000103090502", 1}, {"@0313000101210502", 1}, {"@0314000102f40502", 1}, {"@0316000101c00502", 1}, {"@0317000100a60502", 1}, {"@03180001006c0502", 1}, {"@0323000100760502", 1}, {"@0324000101890502", 1}, {"@03250001010a0502", 1}, {"@0326000101390502", 1}, {"@0327000101c30502", 1}, {"@0328000102eb0502", 1}, {"@03290001009d0502", 1}, {"@032a000103070502", 1}, {"@032c000101480502", 1}, {"@032d000100bc0502", 1}, {"@032e0101031c0502", 1}, {"@03380001011d0502", 1}, {"@0339000101b10502", 1}, {"@033a000101cc0502", 1}, {"@033b000100fa0502", 1}, {"@033c000101000502", 1}, {"@033d0001013a0502", 1}, {"@033e000101a20502", 1}, {"@033f0001008f0502", 1}, {"@03410001030e0502", 1}, {"@0342000101280502", 1}, {"@0343000102ef0502", 1}, {"@0344000100c50502", 1}, {"@03450001005f0502", 1}, {"@0347000103020502", 1}, {"@03480001006b0502", 1}, {"@03490001018d0502", 1}, {"@034a000101430502", 1}, {"@034b0001009f0502", 1}, {"@0356000101350502", 1}, {"@0357000100eb0502", 1}, {"@0358000102fa0502", 1}, {"@035a000100850502", 1}, {"@035c000101290502", 1}, {"@035d000100c90502", 1}, {"@035e0001018e0502", 1}, {"@0369000100d30502", 1}, {"@036a0001019d0502", 1}, {"@036b0001018b0502", 1}, {"@036d000103040502", 1}, {"@036e000102fb0502", 1}, {"@03700001015d0502", 1}, {"@03710001005c0502", 1}, {"@03720001010b0502", 1}, {"@0373000101340502", 1}, {"@0374010103190502", 1}, {"@037e000101560502", 1}, {"@037f000101aa0502", 1}, {"@0380000101870502", 1}, {"@0381000100d50502", 1}, {"@03820001016b0502", 1}, {"@03830001009b0502", 1}, {"@0384000100860502", 1}, {"@0385000101060502", 1}, {"@0390000101850502", 1}, {"@0392000101270502", 1}, {"@0393000100a00502", 1}, {"@0394000100890502", 1}, {"@0395000102fc0502", 1}, {"@0398000100bf0502", 1}, {"@0399000101c20502", 1}, {"@03a40001014f0502", 1}, {"@03a50001015b0502", 1}, {"@03a6000100c80502", 1}, {"@03a7000101a10502", 1}, {"@03a8000100910502", 1}, {"@03a9000100710502", 1}, {"@03aa000100e60502", 1}, {"@03ab000103160502", 1}, {"@03ac000101880502", 1}, {"@03ad000101b20502", 1}, {"@03ae000100870502", 1}, {"@03af0001012c0502", 1}, {"@03b0000101a90502", 1}, {"@03b1000100f00502", 1}, {"@03bc0001008a0502", 1}, {"@03bd000100f90502", 1}, {"@03be000101980502", 1}, {"@03bf000101bc0502", 1}, {"@03c0000103100502", 1}, {"@03c1000100bb0502", 1}, {"@03c40001012b0502", 1}, {"@03c50001015c0502", 1}, {"@03c6000100930502", 1}, {"@03d1000100c20502", 1}, {"@03d2000100e50502", 1}, {"@03d3000102f30502", 1}, {"@03d6000101570502", 1}, {"@03d7000101b40502", 1}, {"@03d9000101a50502", 1}, {"@03da000101510502", 1}, {"@03db0001006d0502", 1}, {"@03e6000100ec0502", 1}, {"@03e70001012a0502", 1}, {"@03e8000102f50502", 1}, {"@03ea0001030b0502", 1}, {"@03ec000101830502", 1}, {"@03ed000101a30502", 1}, {"@03ee0001008b0502", 1}, {"@03fa000100d00502", 1}, {"@03fb000101cf0502", 1}, {"@03fc000101470502", 1}, {"@03fd000101580502", 1}, {"@03fe000101a40502", 1}, {"@03ff000100f40502", 1}, {"@04000001006f0502", 1}, {"@0401000100660502", 1}, {"@040c000101590502", 1}, {"@040d000100780502", 1}, {"@040e000100880502", 1}, {"@040f000101500502", 1}, {"@04100001007f0502", 1}, {"@0411000101ab0502", 1}, {"@04140001030a0502", 1}, {"@0415000101bb0502", 1}, {"@0416000100fb0502", 1}, {"@0418000100d80502", 1}, {"@041a000100e00502", 1}, {"@041b000100f10502", 1}, {"@041c000101410502", 1}, {"@041d0001018a0502", 1}, {"@041e0001015f0502", 1}, {"@0429000100700502", 1}, {"@042a0001012d0502", 1}, {"@042b000101af0502", 1}, {"@0436000101940502", 1}, {"@0437000101050502", 1}, {"@0438000103000502", 1}, {"@0439000103110502", 1}, {"@043b000103030502", 1}, {"@043c000101cb0502", 1}, {"@043d0001007c0502", 1}, {"@043e000101490502", 1}, {"@043f000101550502", 1}, {"@0440000100ca0502", 1}, {"@044b0001016c0502", 1}, {"@044c0001008e0502", 1}, {"@044d000101930502", 1}, {"@044e000103150502", 1}, {"@0450000100cf0502", 1}, {"@04510001015e0502", 1}, {"@0452000100730502", 1}, {"@0453000101040502", 1}, {"@0454000101ae0502", 1}, {"@045f000101a80502", 1}, {"@0460000100a50502", 1}, {"@0461000101610502", 1}, {"@0462000100f60502", 1}, {"@0463000101310502", 1}, {"@0464000100c00502", 1}, {"@04650001006e0502", 1}, {"@0468000102f20502", 1}, {"@0469000101640502", 1}, {"@046a000101d00502", 1}, {"@046b000101970502", 1}, {"@046c0001008c0502", 1}, {"@046d000100f30502", 1}, {"@0478000101630502", 1}, {"@0479000100920502", 1}, {"@047a000100600502", 1}, {"@047b000100f50502", 1}, {"@047c000101a00502", 1}, {"@047d0001012e0502", 1}, {"@04800001008d0502", 1}, {"@0481000102f10502", 1}, {"@0482000102fd0502", 1}, {"@0483000101b00502", 1}, {"@04850001014c0502", 1}, {"@0486000100fc0502", 1}, {"@0487000101bf0502", 1}, {"@0488000100980502", 1}, {"@0489000100ef0502", 1}, {"@04940001009a0502", 1}, {"@0495000101920502", 1}, {"@0496000100d90502", 1}, {"@04970001007a0502", 1}, {"@04980001014a0502", 1}, {"@0499000100df0502", 1}, {"@049a0001014e0502", 1}, {"@049b000100610502", 1}, {"@049c000101400502", 1}, {"@049d000100ed0502", 1}, {"@049e000101b70502", 1}, {"@049f000103010502", 1}, {"@04a00001016e0502", 1}, {"@04a10001016f0502", 1}, {"@04a2000102e80502", 1}, {"@04a3000101c90502", 1}, {"@04a4000100d40502", 1}, {"@04a5000100740502", 1}, {"@04a6000100a30502", 1}, {"@04a7000101a60502", 1}, {"@04a80101031e0502", 1}, {"@04b2000101b90502", 1}, {"@04b3000100dd0502", 1}, {"@04b40001030c0502", 1}, {"@04b6000102ec0502", 1}, {"@04b9000101600502", 1}, {"@04ba0001005d0502", 1}, {"@04c5000101010502", 1}, {"@04c6000101670502", 1}, {"@04c7000100940502", 1}, {"@04c8000102ed0502", 1}, {"@04c90001030d0502", 1}, {"@04cc000100a40502", 1}, {"@04cd000101520502", 1}, {"@04ce000100db0502", 1}, {"@04cf000100e10502", 1}, {"@04d0000101960502", 1}, {"@04d10001009e0502", 1}, {"@04d2000101a70502", 1}, {"@04d30101031b0502", 1}, {"@04dd000100a20502", 1}, {"@04de000100ce0502", 1}, {"@04df000100e80502", 1}, {"@04e0000100f70502", 1}, {"@04e1000101be0502", 1}, {"@04e2000101090502", 1}, {"@04e3000101650502", 1}, {"@04e4000101b60502", 1}, {"@04e5000101ad0502", 1}, {"@04e6000100820502", 1}, {"@04e7000101320502", 1}, {"@04e8000101ce0502", 1}, {"@04ea000103180502", 1}, {"@04eb000102f00502", 1}, {"@04ec000100770502", 1}, {"@04ed000100620502", 1}, {"@04ee0001014b0502", 1}, {"@04ef0001013b0502", 1}, {"@04fa000101680502", 1}, {"@04fb000101c60502", 1}, {"@04fc000102ee0502", 1}, {"@04fd0001007b0502", 1}, {"@04fe000100590502", 1}, {"@04ff000101620502", 1}, {"@0500000100e70502", 1}, {"@050b000100990502", 1}, {"@050c000101c10502", 1}, {"@050d000101420502", 1}, {"@050e000100d70502", 1}, {"@050f000103140502", 1}, {"@0510000101070502", 1}, {"@0511000101950502", 1}, {"@0513000102e70502", 1}, {"@0514000101530502", 1}, {"@05150001005b0502", 1}, {"@08000100003e0402", 1}, {"@08000100025f0402", 1}, {"@0800010003690402", 1}, {"@0800010003820002", 1}, {"@0800010004150402", 1}, {"@08000200003f0402", 1}, {"@0800020002600402", 1}, {"@08000200036a0402", 1}, {"@0800030000400402", 1}, {"@0800030002610402", 1}, {"@08000300036b0402", 1}, {"@08010000025d0402", 1}, {"@0801000004360402", 1}, {"@08020000025e0402", 1}, {"@0802000004370402", 1}, {"@0a12000103c90502", 1}, {"@0a1c000103d30502", 1}, {"@3500010002e10f02", 1}, {"@3500020002e20f02", 1}, {"@3501000002e30f02", 1}, {"@3502010002e40f02", 1}, {"@3503010002e50f02", 1}, {"@3504010002e60f02", 1} } },
	{"0004000000198E00", { {"@0100000000040002", 1}, {"@01000000034b0902", 1}, {"@01000000034c0902", 1}, {"@01000000034d0902", 1}, {"@01000000034e0902", 1}, {"@01000000034f0902", 1}, {"@0100000003530902", 1}, {"@0100000003540902", 1}, {"@01000000037c0002", 1}, {"@0100000003990902", 1}, {"@0100000004180902", 1}, {"@0100010000160002", 1}, {"@0100010003500902", 1}, {"@01010000000e0002", 1}, {"@0101000003520902", 1}, {"@0101000003560902", 1}, {"@0101000004190902", 1}, {"@0101010000170002", 1}, {"@0101030004140902", 1}, {"@01020100001b0002", 1}, {"@01020100041a0902", 1}, {"@01030000024f0902", 1}, {"@0140000003550902", 1}, {"@01410000035c0902", 1}, {"@0180000000080002", 1}, {"@01810000024b0502", 1}, {"@01810000037d0002", 1}, {"@0181000100440502", 1}, {"@0181000101d40502", 1}, {"@01810100023f0502", 1}, {"@0181010100b40502", 1}, {"@01810201011a0502", 1}, {"@0181030101700502", 1}, {"@0181040103aa0502", 1}, {"@0181050103bf0502", 1}, {"@0182000002400502", 1}, {"@0182000100a80502", 1}, {"@0182000101d80502", 1}, {"@0182000103b20502", 1}, {"@0182010100460502", 1}, {"@0183000002420502", 1}, {"@0183000100450502", 1}, {"@01830101010e0502", 1}, {"@0183020103a80502", 1}, {"@0183030103be0502", 1}, {"@01840000024d0502", 1}, {"@0184050103a90502", 1}, {"@01850001004b0502", 1}, {"@0185020101170502", 1}, {"@0185040101790502", 1}, {"@0186010100af0502", 1}, {"@0186030101750502", 1}, {"@0187000100470502", 1}, {"@0187000103b00502", 1}, {"@0188000002410502", 1}, {"@0188000101120502", 1}, {"@0188000103af0502", 1}, {"@0189000100ab0502", 1}, {"@0189010103b10502", 1}, {"@018a000002450502", 1}, {"@018a000100a90502", 1}, {"@018b000002460502", 1}, {"@018b000101150502", 1}, {"@018c000002430502", 1}, {"@018c0001004c0502", 1}, {"@018c010101180502", 1}, {"@018d0000024c0502", 1}, {"@018d0001010c0502", 1}, {"@018e000002490502", 1}, {"@018e000100490502", 1}, {"@018e010101780502", 1}, {"@018f000100b30502", 1}, {"@018f010101190502", 1}, {"@0190000101710502", 1}, {"@01910001004e0502", 1}, {"@0192000002470502", 1}, {"@01920001010d0502", 1}, {"@0192000103ad0502", 1}, {"@0193000002480502", 1}, {"@0193000101740502", 1}, {"@0193000103ae0502", 1}, {"@01940000024a0502", 1}, {"@0194000100aa0502", 1}, {"@0194000103b60502", 1}, {"@0195000100b00502", 1}, {"@01960000024e0502", 1}, {"@0196000100480502", 1}, {"@0197000101770502", 1}, {"@0198000100b10502", 1}, {"@0199000101160502", 1}, {"@019a000100b70502", 1}, {"@019b000100b60502", 1}, {"@019c000101730502", 1}, {"@019d000100ac0502", 1}, {"@019e000100ad0502", 1}, {"@019f000101110502", 1}, {"@01a00001010f0502", 1}, {"@01a1000101100502", 1}, {"@01a20001017d0502", 1}, {"@01a2000103b90502", 1}, {"@01a30001004a0502", 1}, {"@01a40001004d0502", 1}, {"@01a5000101720502", 1}, {"@01a6000100500502", 1}, {"@01a6000103b70502", 1}, {"@01a7000101140502", 1}, {"@01a80001004f0502", 1}, {"@01a80101017e0502", 1}, {"@01a9000101760502", 1}, {"@01aa000100530502", 1}, {"@01ab0001017c0502", 1}, {"@01ac0001017f0502", 1}, {"@01ad000100b80502", 1}, {"@01ae0001011b0502", 1}, {"@01af0001011c0502", 1}, {"@01b0000100520502", 1}, {"@01b1000100b20502", 1}, {"@01b10101017b0502", 1}, {"@01b3000100b50502", 1}, {"@01b4000101130502", 1}, {"@01b5000100510502", 1}, {"@01b6000100ae0502", 1}, {"@01c1000002440502", 1}, {"@01c1000100540502", 1}, {"@01c10101017a0502", 1}, {"@01c1020103bb0502", 1}, {"@0200000100a10502", 1}, {"@02010001016a0502", 1}, {"@0202000101030502", 1}, {"@02030001019a0502", 1}, {"@0206000103120502", 1}, {"@0208000100960502", 1}, {"@02090001019f0502", 1}, {"@0214000100e40502", 1}, {"@0215000101820502", 1}, {"@0216000100570502", 1}, {"@0217000101b30502", 1}, {"@02190001007e0502", 1}, {"@021a000100da0502", 1}, {"@021b000100800502", 1}, {"@021c000102f70502", 1}, {"@021d000101cd0502", 1}, {"@021e000101230502", 1}, {"@021f000103170502", 1}, {"@0220000100fd0502", 1}, {"@02210001013c0502", 1}, {"@0222000101440502", 1}, {"@022d000100f20502", 1}, {"@022e000101d30502", 1}, {"@022f0001011e0502", 1}, {"@0230000101d20502", 1}, {"@02310001006a0502", 1}, {"@0232000102ea0502", 1}, {"@0233000103060502", 1}, {"@0235000100840502", 1}, {"@0238000102f80502", 1}, {"@023c000100bd0502", 1}, {"@023d000101b50502", 1}, {"@023e000100d10502", 1}, {"@023f000101660502", 1}, {"@024a000101d10502", 1}, {"@024b000101260502", 1}, {"@024d000102f60502", 1}, {"@024f000100810502", 1}, {"@0251000100c10502", 1}, {"@0252000100fe0502", 1}, {"@025d000100550502", 1}, {"@025e000101250502", 1}, {"@025f000101c50502", 1}, {"@025f000101d70502", 1}, {"@0260000100d20502", 1}, {"@0261000100650502", 1}, {"@0262000101370502", 1}, {"@0263000100750502", 1}, {"@0264000101ac0502", 1}, {"@0265000101540502", 1}, {"@0266000100680502", 1}, {"@0267000101080502", 1}, {"@02680001007d0502", 1}, {"@02690001011f0502", 1}, {"@026a000101460502", 1}, {"@026b000100e90502", 1}, {"@026c000100c30502", 1}, {"@026d0001013f0502", 1}, {"@026e000100ba0502", 1}, {"@026f000101900502", 1}, {"@0270000100ff0502", 1}, {"@02710001019b0502", 1}, {"@0272000101860502", 1}, {"@027d000100630502", 1}, {"@027e000101690502", 1}, {"@027f000100b90502", 1}, {"@0280000100830502", 1}, {"@0281000101200502", 1}, {"@0282000101810502", 1}, {"@0282000101d60502", 1}, {"@0283000100c70502", 1}, {"@0284000102fe0502", 1}, {"@0286000103130502", 1}, {"@02870001005a0502", 1}, {"@028a000102e90502", 1}, {"@028b000100e30502", 1}, {"@028c0001013e0502", 1}, {"@028d000101bd0502", 1}, {"@028e0001019e0502", 1}, {"@028f0101031a0502", 1}, {"@0299000100950502", 1}, {"@029a000100ee0502", 1}, {"@029b000100cb0502", 1}, {"@029e0001013d0502", 1}, {"@02a2000101ba0502", 1}, {"@02a3000102ff0502", 1}, {"@02a4000100720502", 1}, {"@02a50001018c0502", 1}, {"@02a6000101240502", 1}, {"@02b1000100690502", 1}, {"@02b2000100c40502", 1}, {"@02b70001030f0502", 1}, {"@02b80001019c0502", 1}, {"@02c3000100dc0502", 1}, {"@02c4000100670502", 1}, {"@02c5000103080502", 1}, {"@02c7000101220502", 1}, {"@02c9000100cd0502", 1}, {"@02ca000101ca0502", 1}, {"@02cb000101360502", 1}, {"@02d6000100560502", 1}, {"@02d7000101300502", 1}, {"@02d8000100e20502", 1}, {"@02d9000101c80502", 1}, {"@02da000101330502", 1}, {"@02db0001005e0502", 1}, {"@02dc000100be0502", 1}, {"@02dd000100ea0502", 1}, {"@02de0001009c0502", 1}, {"@02df000101910502", 1}, {"@02e00101031d0502", 1}, {"@02ea000101800502", 1}, {"@02ea000101d50502", 1}, {"@02eb000100de0502", 1}, {"@02ec000101c40502", 1}, {"@02ed0001015a0502", 1}, {"@02ee000101990502", 1}, {"@02ef000100580502", 1}, {"@02f0000100a70502", 1}, {"@02f1000101450502", 1}, {"@02f2000100cc0502", 1}, {"@02f3000102f90502", 1}, {"@02f4000103050502", 1}, {"@02f8000101380502", 1}, {"@02f9000101020502", 1}, {"@02fa000100970502", 1}, {"@02fb000100900502", 1}, {"@02fc0001018f0502", 1}, {"@0307000100640502", 1}, {"@03080001014d0502", 1}, {"@0309000100c60502", 1}, {"@030a000101c70502", 1}, {"@030b000100790502", 1}, {"@030c000101b80502", 1}, {"@030d000101840502", 1}, {"@030e0001012f0502", 1}, {"@030f0001016d0502", 1}, {"@0310000100f80502", 1}, {"@0311000100d60502", 1}, {"@0312000103090502", 1}, {"@0313000101210502", 1}, {"@0314000102f40502", 1}, {"@0316000101c00502", 1}, {"@0317000100a60502", 1}, {"@03180001006c0502", 1}, {"@0323000100760502", 1}, {"@0324000101890502", 1}, {"@03250001010a0502", 1}, {"@0326000101390502", 1}, {"@0327000101c30502", 1}, {"@0328000102eb0502", 1}, {"@03290001009d0502", 1}, {"@032a000103070502", 1}, {"@032c000101480502", 1}, {"@032d000100bc0502", 1}, {"@032e0101031c0502", 1}, {"@03380001011d0502", 1}, {"@0339000101b10502", 1}, {"@033a000101cc0502", 1}, {"@033b000100fa0502", 1}, {"@033c000101000502", 1}, {"@033d0001013a0502", 1}, {"@033e000101a20502", 1}, {"@033f0001008f0502", 1}, {"@03410001030e0502", 1}, {"@0342000101280502", 1}, {"@0343000102ef0502", 1}, {"@0344000100c50502", 1}, {"@03450001005f0502", 1}, {"@0347000103020502", 1}, {"@03480001006b0502", 1}, {"@03490001018d0502", 1}, {"@034a000101430502", 1}, {"@034b0001009f0502", 1}, {"@0356000101350502", 1}, {"@0357000100eb0502", 1}, {"@0358000102fa0502", 1}, {"@035a000100850502", 1}, {"@035c000101290502", 1}, {"@035d000100c90502", 1}, {"@035e0001018e0502", 1}, {"@0369000100d30502", 1}, {"@036a0001019d0502", 1}, {"@036b0001018b0502", 1}, {"@036d000103040502", 1}, {"@036e000102fb0502", 1}, {"@03700001015d0502", 1}, {"@03710001005c0502", 1}, {"@03720001010b0502", 1}, {"@0373000101340502", 1}, {"@0374010103190502", 1}, {"@037e000101560502", 1}, {"@037f000101aa0502", 1}, {"@0380000101870502", 1}, {"@0381000100d50502", 1}, {"@03820001016b0502", 1}, {"@03830001009b0502", 1}, {"@0384000100860502", 1}, {"@0385000101060502", 1}, {"@0390000101850502", 1}, {"@0392000101270502", 1}, {"@0393000100a00502", 1}, {"@0394000100890502", 1}, {"@0395000102fc0502", 1}, {"@0398000100bf0502", 1}, {"@0399000101c20502", 1}, {"@03a40001014f0502", 1}, {"@03a50001015b0502", 1}, {"@03a6000100c80502", 1}, {"@03a7000101a10502", 1}, {"@03a8000100910502", 1}, {"@03a9000100710502", 1}, {"@03aa000100e60502", 1}, {"@03ab000103160502", 1}, {"@03ac000101880502", 1}, {"@03ad000101b20502", 1}, {"@03ae000100870502", 1}, {"@03af0001012c0502", 1}, {"@03b0000101a90502", 1}, {"@03b1000100f00502", 1}, {"@03bc0001008a0502", 1}, {"@03bd000100f90502", 1}, {"@03be000101980502", 1}, {"@03bf000101bc0502", 1}, {"@03c0000103100502", 1}, {"@03c1000100bb0502", 1}, {"@03c40001012b0502", 1}, {"@03c50001015c0502", 1}, {"@03c6000100930502", 1}, {"@03d1000100c20502", 1}, {"@03d2000100e50502", 1}, {"@03d3000102f30502", 1}, {"@03d6000101570502", 1}, {"@03d7000101b40502", 1}, {"@03d9000101a50502", 1}, {"@03da000101510502", 1}, {"@03db0001006d0502", 1}, {"@03e6000100ec0502", 1}, {"@03e70001012a0502", 1}, {"@03e8000102f50502", 1}, {"@03ea0001030b0502", 1}, {"@03ec000101830502", 1}, {"@03ed000101a30502", 1}, {"@03ee0001008b0502", 1}, {"@03fa000100d00502", 1}, {"@03fb000101cf0502", 1}, {"@03fc000101470502", 1}, {"@03fd000101580502", 1}, {"@03fe000101a40502", 1}, {"@03ff000100f40502", 1}, {"@04000001006f0502", 1}, {"@0401000100660502", 1}, {"@040c000101590502", 1}, {"@040d000100780502", 1}, {"@040e000100880502", 1}, {"@040f000101500502", 1}, {"@04100001007f0502", 1}, {"@0411000101ab0502", 1}, {"@04140001030a0502", 1}, {"@0415000101bb0502", 1}, {"@0416000100fb0502", 1}, {"@0418000100d80502", 1}, {"@041a000100e00502", 1}, {"@041b000100f10502", 1}, {"@041c000101410502", 1}, {"@041d0001018a0502", 1}, {"@041e0001015f0502", 1}, {"@0429000100700502", 1}, {"@042a0001012d0502", 1}, {"@042b000101af0502", 1}, {"@0436000101940502", 1}, {"@0437000101050502", 1}, {"@0438000103000502", 1}, {"@0439000103110502", 1}, {"@043b000103030502", 1}, {"@043c000101cb0502", 1}, {"@043d0001007c0502", 1}, {"@043e000101490502", 1}, {"@043f000101550502", 1}, {"@0440000100ca0502", 1}, {"@044b0001016c0502", 1}, {"@044c0001008e0502", 1}, {"@044d000101930502", 1}, {"@044e000103150502", 1}, {"@0450000100cf0502", 1}, {"@04510001015e0502", 1}, {"@0452000100730502", 1}, {"@0453000101040502", 1}, {"@0454000101ae0502", 1}, {"@045f000101a80502", 1}, {"@0460000100a50502", 1}, {"@0461000101610502", 1}, {"@0462000100f60502", 1}, {"@0463000101310502", 1}, {"@0464000100c00502", 1}, {"@04650001006e0502", 1}, {"@0468000102f20502", 1}, {"@0469000101640502", 1}, {"@046a000101d00502", 1}, {"@046b000101970502", 1}, {"@046c0001008c0502", 1}, {"@046d000100f30502", 1}, {"@0478000101630502", 1}, {"@0479000100920502", 1}, {"@047a000100600502", 1}, {"@047b000100f50502", 1}, {"@047c000101a00502", 1}, {"@047d0001012e0502", 1}, {"@04800001008d0502", 1}, {"@0481000102f10502", 1}, {"@0482000102fd0502", 1}, {"@0483000101b00502", 1}, {"@04850001014c0502", 1}, {"@0486000100fc0502", 1}, {"@0487000101bf0502", 1}, {"@0488000100980502", 1}, {"@0489000100ef0502", 1}, {"@04940001009a0502", 1}, {"@0495000101920502", 1}, {"@0496000100d90502", 1}, {"@04970001007a0502", 1}, {"@04980001014a0502", 1}, {"@0499000100df0502", 1}, {"@049a0001014e0502", 1}, {"@049b000100610502", 1}, {"@049c000101400502", 1}, {"@049d000100ed0502", 1}, {"@049e000101b70502", 1}, {"@049f000103010502", 1}, {"@04a00001016e0502", 1}, {"@04a10001016f0502", 1}, {"@04a2000102e80502", 1}, {"@04a3000101c90502", 1}, {"@04a4000100d40502", 1}, {"@04a5000100740502", 1}, {"@04a6000100a30502", 1}, {"@04a7000101a60502", 1}, {"@04a80101031e0502", 1}, {"@04b2000101b90502", 1}, {"@04b3000100dd0502", 1}, {"@04b40001030c0502", 1}, {"@04b6000102ec0502", 1}, {"@04b9000101600502", 1}, {"@04ba0001005d0502", 1}, {"@04c5000101010502", 1}, {"@04c6000101670502", 1}, {"@04c7000100940502", 1}, {"@04c8000102ed0502", 1}, {"@04c90001030d0502", 1}, {"@04cc000100a40502", 1}, {"@04cd000101520502", 1}, {"@04ce000100db0502", 1}, {"@04cf000100e10502", 1}, {"@04d0000101960502", 1}, {"@04d10001009e0502", 1}, {"@04d2000101a70502", 1}, {"@04d30101031b0502", 1}, {"@04dd000100a20502", 1}, {"@04de000100ce0502", 1}, {"@04df000100e80502", 1}, {"@04e0000100f70502", 1}, {"@04e1000101be0502", 1}, {"@04e2000101090502", 1}, {"@04e3000101650502", 1}, {"@04e4000101b60502", 1}, {"@04e5000101ad0502", 1}, {"@04e6000100820502", 1}, {"@04e7000101320502", 1}, {"@04e8000101ce0502", 1}, {"@04ea000103180502", 1}, {"@04eb000102f00502", 1}, {"@04ec000100770502", 1}, {"@04ed000100620502", 1}, {"@04ee0001014b0502", 1}, {"@04ef0001013b0502", 1}, {"@04fa000101680502", 1}, {"@04fb000101c60502", 1}, {"@04fc000102ee0502", 1}, {"@04fd0001007b0502", 1}, {"@04fe000100590502", 1}, {"@04ff000101620502", 1}, {"@0500000100e70502", 1}, {"@050b000100990502", 1}, {"@050c000101c10502", 1}, {"@050d000101420502", 1}, {"@050e000100d70502", 1}, {"@050f000103140502", 1}, {"@0510000101070502", 1}, {"@0511000101950502", 1}, {"@0513000102e70502", 1}, {"@0514000101530502", 1}, {"@05150001005b0502", 1}, {"@08000100003e0402", 1}, {"@08000100025f0402", 1}, {"@0800010003690402", 1}, {"@0800010003820002", 1}, {"@0800010004150402", 1}, {"@08000200003f0402", 1}, {"@0800020002600402", 1}, {"@08000200036a0402", 1}, {"@0800030000400402", 1}, {"@0800030002610402", 1}, {"@08000300036b0402", 1}, {"@08010000025d0402", 1}, {"@0801000004360402", 1}, {"@08020000025e0402", 1}, {"@0802000004370402", 1}, {"@0a12000103c90502", 1}, {"@0a1c000103d30502", 1}, {"@3500010002e10f02", 1}, {"@3500020002e20f02", 1}, {"@3501000002e30f02", 1}, {"@3502010002e40f02", 1}, {"@3503010002e50f02", 1}, {"@3504010002e60f02", 1} } },
	{"0004000000198F00", { {"@0100000000040002", 1}, {"@01000000034b0902", 1}, {"@01000000034c0902", 1}, {"@01000000034d0902", 1}, {"@01000000034e0902", 1}, {"@01000000034f0902", 1}, {"@0100000003530902", 1}, {"@0100000003540902", 1}, {"@01000000037c0002", 1}, {"@0100000003990902", 1}, {"@0100000004180902", 1}, {"@0100010000160002", 1}, {"@0100010003500902", 1}, {"@01010000000e0002", 1}, {"@0101000003520902", 1}, {"@0101000003560902", 1}, {"@0101000004190902", 1}, {"@0101010000170002", 1}, {"@0101030004140902", 1}, {"@01020100001b0002", 1}, {"@01020100041a0902", 1}, {"@01030000024f0902", 1}, {"@0140000003550902", 1}, {"@01410000035c0902", 1}, {"@0180000000080002", 1}, {"@01810000024b0502", 1}, {"@01810000037d0002", 1}, {"@0181000100440502", 1}, {"@0181000101d40502", 1}, {"@01810100023f0502", 1}, {"@0181010100b40502", 1}, {"@01810201011a0502", 1}, {"@0181030101700502", 1}, {"@0181040103aa0502", 1}, {"@0181050103bf0502", 1}, {"@0182000002400502", 1}, {"@0182000100a80502", 1}, {"@0182000101d80502", 1}, {"@0182000103b20502", 1}, {"@0182010100460502", 1}, {"@0183000002420502", 1}, {"@0183000100450502", 1}, {"@01830101010e0502", 1}, {"@0183020103a80502", 1}, {"@0183030103be0502", 1}, {"@01840000024d0502", 1}, {"@0184050103a90502", 1}, {"@01850001004b0502", 1}, {"@0185020101170502", 1}, {"@0185040101790502", 1}, {"@0186010100af0502", 1}, {"@0186030101750502", 1}, {"@0187000100470502", 1}, {"@0187000103b00502", 1}, {"@0188000002410502", 1}, {"@0188000101120502", 1}, {"@0188000103af0502", 1}, {"@0189000100ab0502", 1}, {"@0189010103b10502", 1}, {"@018a000002450502", 1}, {"@018a000100a90502", 1}, {"@018b000002460502", 1}, {"@018b000101150502", 1}, {"@018c000002430502", 1}, {"@018c0001004c0502", 1}, {"@018c010101180502", 1}, {"@018d0000024c0502", 1}, {"@018d0001010c0502", 1}, {"@018e000002490502", 1}, {"@018e000100490502", 1}, {"@018e010101780502", 1}, {"@018f000100b30502", 1}, {"@018f010101190502", 1}, {"@0190000101710502", 1}, {"@01910001004e0502", 1}, {"@0192000002470502", 1}, {"@01920001010d0502", 1}, {"@0192000103ad0502", 1}, {"@0193000002480502", 1}, {"@0193000101740502", 1}, {"@0193000103ae0502", 1}, {"@01940000024a0502", 1}, {"@0194000100aa0502", 1}, {"@0194000103b60502", 1}, {"@0195000100b00502", 1}, {"@01960000024e0502", 1}, {"@0196000100480502", 1}, {"@0197000101770502", 1}, {"@0198000100b10502", 1}, {"@0199000101160502", 1}, {"@019a000100b70502", 1}, {"@019b000100b60502", 1}, {"@019c000101730502", 1}, {"@019d000100ac0502", 1}, {"@019e000100ad0502", 1}, {"@019f000101110502", 1}, {"@01a00001010f0502", 1}, {"@01a1000101100502", 1}, {"@01a20001017d0502", 1}, {"@01a2000103b90502", 1}, {"@01a30001004a0502", 1}, {"@01a40001004d0502", 1}, {"@01a5000101720502", 1}, {"@01a6000100500502", 1}, {"@01a6000103b70502", 1}, {"@01a7000101140502", 1}, {"@01a80001004f0502", 1}, {"@01a80101017e0502", 1}, {"@01a9000101760502", 1}, {"@01aa000100530502", 1}, {"@01ab0001017c0502", 1}, {"@01ac0001017f0502", 1}, {"@01ad000100b80502", 1}, {"@01ae0001011b0502", 1}, {"@01af0001011c0502", 1}, {"@01b0000100520502", 1}, {"@01b1000100b20502", 1}, {"@01b10101017b0502", 1}, {"@01b3000100b50502", 1}, {"@01b4000101130502", 1}, {"@01b5000100510502", 1}, {"@01b6000100ae0502", 1}, {"@01c1000002440502", 1}, {"@01c1000100540502", 1}, {"@01c10101017a0502", 1}, {"@01c1020103bb0502", 1}, {"@0200000100a10502", 1}, {"@02010001016a0502", 1}, {"@0202000101030502", 1}, {"@02030001019a0502", 1}, {"@0206000103120502", 1}, {"@0208000100960502", 1}, {"@02090001019f0502", 1}, {"@0214000100e40502", 1}, {"@0215000101820502", 1}, {"@0216000100570502", 1}, {"@0217000101b30502", 1}, {"@02190001007e0502", 1}, {"@021a000100da0502", 1}, {"@021b000100800502", 1}, {"@021c000102f70502", 1}, {"@021d000101cd0502", 1}, {"@021e000101230502", 1}, {"@021f000103170502", 1}, {"@0220000100fd0502", 1}, {"@02210001013c0502", 1}, {"@0222000101440502", 1}, {"@022d000100f20502", 1}, {"@022e000101d30502", 1}, {"@022f0001011e0502", 1}, {"@0230000101d20502", 1}, {"@02310001006a0502", 1}, {"@0232000102ea0502", 1}, {"@0233000103060502", 1}, {"@0235000100840502", 1}, {"@0238000102f80502", 1}, {"@023c000100bd0502", 1}, {"@023d000101b50502", 1}, {"@023e000100d10502", 1}, {"@023f000101660502", 1}, {"@024a000101d10502", 1}, {"@024b000101260502", 1}, {"@024d000102f60502", 1}, {"@024f000100810502", 1}, {"@0251000100c10502", 1}, {"@0252000100fe0502", 1}, {"@025d000100550502", 1}, {"@025e000101250502", 1}, {"@025f000101c50502", 1}, {"@025f000101d70502", 1}, {"@0260000100d20502", 1}, {"@0261000100650502", 1}, {"@0262000101370502", 1}, {"@0263000100750502", 1}, {"@0264000101ac0502", 1}, {"@0265000101540502", 1}, {"@0266000100680502", 1}, {"@0267000101080502", 1}, {"@02680001007d0502", 1}, {"@02690001011f0502", 1}, {"@026a000101460502", 1}, {"@026b000100e90502", 1}, {"@026c000100c30502", 1}, {"@026d0001013f0502", 1}, {"@026e000100ba0502", 1}, {"@026f000101900502", 1}, {"@0270000100ff0502", 1}, {"@02710001019b0502", 1}, {"@0272000101860502", 1}, {"@027d000100630502", 1}, {"@027e000101690502", 1}, {"@027f000100b90502", 1}, {"@0280000100830502", 1}, {"@0281000101200502", 1}, {"@0282000101810502", 1}, {"@0282000101d60502", 1}, {"@0283000100c70502", 1}, {"@0284000102fe0502", 1}, {"@0286000103130502", 1}, {"@02870001005a0502", 1}, {"@028a000102e90502", 1}, {"@028b000100e30502", 1}, {"@028c0001013e0502", 1}, {"@028d000101bd0502", 1}, {"@028e0001019e0502", 1}, {"@028f0101031a0502", 1}, {"@0299000100950502", 1}, {"@029a000100ee0502", 1}, {"@029b000100cb0502", 1}, {"@029e0001013d0502", 1}, {"@02a2000101ba0502", 1}, {"@02a3000102ff0502", 1}, {"@02a4000100720502", 1}, {"@02a50001018c0502", 1}, {"@02a6000101240502", 1}, {"@02b1000100690502", 1}, {"@02b2000100c40502", 1}, {"@02b70001030f0502", 1}, {"@02b80001019c0502", 1}, {"@02c3000100dc0502", 1}, {"@02c4000100670502", 1}, {"@02c5000103080502", 1}, {"@02c7000101220502", 1}, {"@02c9000100cd0502", 1}, {"@02ca000101ca0502", 1}, {"@02cb000101360502", 1}, {"@02d6000100560502", 1}, {"@02d7000101300502", 1}, {"@02d8000100e20502", 1}, {"@02d9000101c80502", 1}, {"@02da000101330502", 1}, {"@02db0001005e0502", 1}, {"@02dc000100be0502", 1}, {"@02dd000100ea0502", 1}, {"@02de0001009c0502", 1}, {"@02df000101910502", 1}, {"@02e00101031d0502", 1}, {"@02ea000101800502", 1}, {"@02ea000101d50502", 1}, {"@02eb000100de0502", 1}, {"@02ec000101c40502", 1}, {"@02ed0001015a0502", 1}, {"@02ee000101990502", 1}, {"@02ef000100580502", 1}, {"@02f0000100a70502", 1}, {"@02f1000101450502", 1}, {"@02f2000100cc0502", 1}, {"@02f3000102f90502", 1}, {"@02f4000103050502", 1}, {"@02f8000101380502", 1}, {"@02f9000101020502", 1}, {"@02fa000100970502", 1}, {"@02fb000100900502", 1}, {"@02fc0001018f0502", 1}, {"@0307000100640502", 1}, {"@03080001014d0502", 1}, {"@0309000100c60502", 1}, {"@030a000101c70502", 1}, {"@030b000100790502", 1}, {"@030c000101b80502", 1}, {"@030d000101840502", 1}, {"@030e0001012f0502", 1}, {"@030f0001016d0502", 1}, {"@0310000100f80502", 1}, {"@0311000100d60502", 1}, {"@0312000103090502", 1}, {"@0313000101210502", 1}, {"@0314000102f40502", 1}, {"@0316000101c00502", 1}, {"@0317000100a60502", 1}, {"@03180001006c0502", 1}, {"@0323000100760502", 1}, {"@0324000101890502", 1}, {"@03250001010a0502", 1}, {"@0326000101390502", 1}, {"@0327000101c30502", 1}, {"@0328000102eb0502", 1}, {"@03290001009d0502", 1}, {"@032a000103070502", 1}, {"@032c000101480502", 1}, {"@032d000100bc0502", 1}, {"@032e0101031c0502", 1}, {"@03380001011d0502", 1}, {"@0339000101b10502", 1}, {"@033a000101cc0502", 1}, {"@033b000100fa0502", 1}, {"@033c000101000502", 1}, {"@033d0001013a0502", 1}, {"@033e000101a20502", 1}, {"@033f0001008f0502", 1}, {"@03410001030e0502", 1}, {"@0342000101280502", 1}, {"@0343000102ef0502", 1}, {"@0344000100c50502", 1}, {"@03450001005f0502", 1}, {"@0347000103020502", 1}, {"@03480001006b0502", 1}, {"@03490001018d0502", 1}, {"@034a000101430502", 1}, {"@034b0001009f0502", 1}, {"@0356000101350502", 1}, {"@0357000100eb0502", 1}, {"@0358000102fa0502", 1}, {"@035a000100850502", 1}, {"@035c000101290502", 1}, {"@035d000100c90502", 1}, {"@035e0001018e0502", 1}, {"@0369000100d30502", 1}, {"@036a0001019d0502", 1}, {"@036b0001018b0502", 1}, {"@036d000103040502", 1}, {"@036e000102fb0502", 1}, {"@03700001015d0502", 1}, {"@03710001005c0502", 1}, {"@03720001010b0502", 1}, {"@0373000101340502", 1}, {"@0374010103190502", 1}, {"@037e000101560502", 1}, {"@037f000101aa0502", 1}, {"@0380000101870502", 1}, {"@0381000100d50502", 1}, {"@03820001016b0502", 1}, {"@03830001009b0502", 1}, {"@0384000100860502", 1}, {"@0385000101060502", 1}, {"@0390000101850502", 1}, {"@0392000101270502", 1}, {"@0393000100a00502", 1}, {"@0394000100890502", 1}, {"@0395000102fc0502", 1}, {"@0398000100bf0502", 1}, {"@0399000101c20502", 1}, {"@03a40001014f0502", 1}, {"@03a50001015b0502", 1}, {"@03a6000100c80502", 1}, {"@03a7000101a10502", 1}, {"@03a8000100910502", 1}, {"@03a9000100710502", 1}, {"@03aa000100e60502", 1}, {"@03ab000103160502", 1}, {"@03ac000101880502", 1}, {"@03ad000101b20502", 1}, {"@03ae000100870502", 1}, {"@03af0001012c0502", 1}, {"@03b0000101a90502", 1}, {"@03b1000100f00502", 1}, {"@03bc0001008a0502", 1}, {"@03bd000100f90502", 1}, {"@03be000101980502", 1}, {"@03bf000101bc0502", 1}, {"@03c0000103100502", 1}, {"@03c1000100bb0502", 1}, {"@03c40001012b0502", 1}, {"@03c50001015c0502", 1}, {"@03c6000100930502", 1}, {"@03d1000100c20502", 1}, {"@03d2000100e50502", 1}, {"@03d3000102f30502", 1}, {"@03d6000101570502", 1}, {"@03d7000101b40502", 1}, {"@03d9000101a50502", 1}, {"@03da000101510502", 1}, {"@03db0001006d0502", 1}, {"@03e6000100ec0502", 1}, {"@03e70001012a0502", 1}, {"@03e8000102f50502", 1}, {"@03ea0001030b0502", 1}, {"@03ec000101830502", 1}, {"@03ed000101a30502", 1}, {"@03ee0001008b0502", 1}, {"@03fa000100d00502", 1}, {"@03fb000101cf0502", 1}, {"@03fc000101470502", 1}, {"@03fd000101580502", 1}, {"@03fe000101a40502", 1}, {"@03ff000100f40502", 1}, {"@04000001006f0502", 1}, {"@0401000100660502", 1}, {"@040c000101590502", 1}, {"@040d000100780502", 1}, {"@040e000100880502", 1}, {"@040f000101500502", 1}, {"@04100001007f0502", 1}, {"@0411000101ab0502", 1}, {"@04140001030a0502", 1}, {"@0415000101bb0502", 1}, {"@0416000100fb0502", 1}, {"@0418000100d80502", 1}, {"@041a000100e00502", 1}, {"@041b000100f10502", 1}, {"@041c000101410502", 1}, {"@041d0001018a0502", 1}, {"@041e0001015f0502", 1}, {"@0429000100700502", 1}, {"@042a0001012d0502", 1}, {"@042b000101af0502", 1}, {"@0436000101940502", 1}, {"@0437000101050502", 1}, {"@0438000103000502", 1}, {"@0439000103110502", 1}, {"@043b000103030502", 1}, {"@043c000101cb0502", 1}, {"@043d0001007c0502", 1}, {"@043e000101490502", 1}, {"@043f000101550502", 1}, {"@0440000100ca0502", 1}, {"@044b0001016c0502", 1}, {"@044c0001008e0502", 1}, {"@044d000101930502", 1}, {"@044e000103150502", 1}, {"@0450000100cf0502", 1}, {"@04510001015e0502", 1}, {"@0452000100730502", 1}, {"@0453000101040502", 1}, {"@0454000101ae0502", 1}, {"@045f000101a80502", 1}, {"@0460000100a50502", 1}, {"@0461000101610502", 1}, {"@0462000100f60502", 1}, {"@0463000101310502", 1}, {"@0464000100c00502", 1}, {"@04650001006e0502", 1}, {"@0468000102f20502", 1}, {"@0469000101640502", 1}, {"@046a000101d00502", 1}, {"@046b000101970502", 1}, {"@046c0001008c0502", 1}, {"@046d000100f30502", 1}, {"@0478000101630502", 1}, {"@0479000100920502", 1}, {"@047a000100600502", 1}, {"@047b000100f50502", 1}, {"@047c000101a00502", 1}, {"@047d0001012e0502", 1}, {"@04800001008d0502", 1}, {"@0481000102f10502", 1}, {"@0482000102fd0502", 1}, {"@0483000101b00502", 1}, {"@04850001014c0502", 1}, {"@0486000100fc0502", 1}, {"@0487000101bf0502", 1}, {"@0488000100980502", 1}, {"@0489000100ef0502", 1}, {"@04940001009a0502", 1}, {"@0495000101920502", 1}, {"@0496000100d90502", 1}, {"@04970001007a0502", 1}, {"@04980001014a0502", 1}, {"@0499000100df0502", 1}, {"@049a0001014e0502", 1}, {"@049b000100610502", 1}, {"@049c000101400502", 1}, {"@049d000100ed0502", 1}, {"@049e000101b70502", 1}, {"@049f000103010502", 1}, {"@04a00001016e0502", 1}, {"@04a10001016f0502", 1}, {"@04a2000102e80502", 1}, {"@04a3000101c90502", 1}, {"@04a4000100d40502", 1}, {"@04a5000100740502", 1}, {"@04a6000100a30502", 1}, {"@04a7000101a60502", 1}, {"@04a80101031e0502", 1}, {"@04b2000101b90502", 1}, {"@04b3000100dd0502", 1}, {"@04b40001030c0502", 1}, {"@04b6000102ec0502", 1}, {"@04b9000101600502", 1}, {"@04ba0001005d0502", 1}, {"@04c5000101010502", 1}, {"@04c6000101670502", 1}, {"@04c7000100940502", 1}, {"@04c8000102ed0502", 1}, {"@04c90001030d0502", 1}, {"@04cc000100a40502", 1}, {"@04cd000101520502", 1}, {"@04ce000100db0502", 1}, {"@04cf000100e10502", 1}, {"@04d0000101960502", 1}, {"@04d10001009e0502", 1}, {"@04d2000101a70502", 1}, {"@04d30101031b0502", 1}, {"@04dd000100a20502", 1}, {"@04de000100ce0502", 1}, {"@04df000100e80502", 1}, {"@04e0000100f70502", 1}, {"@04e1000101be0502", 1}, {"@04e2000101090502", 1}, {"@04e3000101650502", 1}, {"@04e4000101b60502", 1}, {"@04e5000101ad0502", 1}, {"@04e6000100820502", 1}, {"@04e7000101320502", 1}, {"@04e8000101ce0502", 1}, {"@04ea000103180502", 1}, {"@04eb000102f00502", 1}, {"@04ec000100770502", 1}, {"@04ed000100620502", 1}, {"@04ee0001014b0502", 1}, {"@04ef0001013b0502", 1}, {"@04fa000101680502", 1}, {"@04fb000101c60502", 1}, {"@04fc000102ee0502", 1}, {"@04fd0001007b0502", 1}, {"@04fe000100590502", 1}, {"@04ff000101620502", 1}, {"@0500000100e70502", 1}, {"@050b000100990502", 1}, {"@050c000101c10502", 1}, {"@050d000101420502", 1}, {"@050e000100d70502", 1}, {"@050f000103140502", 1}, {"@0510000101070502", 1}, {"@0511000101950502", 1}, {"@0513000102e70502", 1}, {"@0514000101530502", 1}, {"@05150001005b0502", 1}, {"@08000100003e0402", 1}, {"@08000100025f0402", 1}, {"@0800010003690402", 1}, {"@0800010003820002", 1}, {"@0800010004150402", 1}, {"@08000200003f0402", 1}, {"@0800020002600402", 1}, {"@08000200036a0402", 1}, {"@0800030000400402", 1}, {"@0800030002610402", 1}, {"@08000300036b0402", 1}, {"@08010000025d0402", 1}, {"@0801000004360402", 1}, {"@08020000025e0402", 1}, {"@0802000004370402", 1}, {"@0a12000103c90502", 1}, {"@0a1c000103d30502", 1}, {"@3500010002e10f02", 1}, {"@3500020002e20f02", 1}, {"@3501000002e30f02", 1}, {"@3502010002e40f02", 1}, {"@3503010002e50f02", 1}, {"@3504010002e60f02", 1} } },
	{"000400000017EA00", { {"@0100000000040002", 1}, {"@01000000034b0902", 1}, {"@01000000034c0902", 1}, {"@01000000034d0902", 1}, {"@01000000034e0902", 1}, {"@01000000034f0902", 1}, {"@0100000003530902", 1}, {"@0100000003540902", 1}, {"@01000000037c0002", 1}, {"@0100000003990902", 1}, {"@0100000004180902", 1}, {"@0100010000160002", 1}, {"@0100010003500902", 1}, {"@01010000000e0002", 1}, {"@0101000003520902", 1}, {"@0101000003560902", 1}, {"@0101000004190902", 1}, {"@0101010000170002", 1}, {"@0101030004140902", 1}, {"@01020100001b0002", 1}, {"@01020100041a0902", 1}, {"@01030000024f0902", 1} } },
	{"000400000017EB00", { {"@0100000000040002", 1}, {"@01000000034b0902", 1}, {"@01000000034c0902", 1}, {"@01000000034d0902", 1}, {"@01000000034e0902", 1}, {"@01000000034f0902", 1}, {"@0100000003530902", 1}, {"@0100000003540902", 1}, {"@01000000037c0002", 1}, {"@0100000003990902", 1}, {"@0100000004180902", 1}, {"@0100010000160002", 1}, {"@0100010003500902", 1}, {"@01010000000e0002", 1}, {"@0101000003520902", 1}, {"@0101000003560902", 1}, {"@0101000004190902", 1}, {"@0101010000170002", 1}, {"@0101030004140902", 1}, {"@01020100001b0002", 1}, {"@01020100041a0902", 1}, {"@01030000024f0902", 1} } },
	{"0004000000193200", { {"@0100000000040002", 1}, {"@01000000034b0902", 1}, {"@01000000034c0902", 1}, {"@01000000034d0902", 1}, {"@01000000034e0902", 1}, {"@01000000034f0902", 1}, {"@0100000003530902", 1}, {"@0100000003540902", 1}, {"@01000000037c0002", 1}, {"@0100000003990902", 1}, {"@0100000004180902", 1}, {"@0100010000160002", 1}, {"@0100010003500902", 1}, {"@01010000000e0002", 1}, {"@0101000003520902", 1}, {"@0101000003560902", 1}, {"@0101000004190902", 1}, {"@0101010000170002", 1}, {"@0101030004140902", 1}, {"@01020100001b0002", 1}, {"@01020100041a0902", 1}, {"@01030000024f0902", 1} } },
	{"0004000000193400", { {"@0100000000040002", 1}, {"@01000000034b0902", 1}, {"@01000000034c0902", 1}, {"@01000000034d0902", 1}, {"@01000000034e0902", 1}, {"@01000000034f0902", 1}, {"@0100000003530902", 1}, {"@0100000003540902", 1}, {"@01000000037c0002", 1}, {"@0100000003990902", 1}, {"@0100000004180902", 1}, {"@0100010000160002", 1}, {"@0100010003500902", 1}, {"@01010000000e0002", 1}, {"@0101000003520902", 1}, {"@0101000003560902", 1}, {"@0101000004190902", 1}, {"@0101010000170002", 1}, {"@0101030004140902", 1}, {"@01020100001b0002", 1}, {"@01020100041a0902", 1}, {"@01030000024f0902", 1} } },
	{"000400000014F100", { {"@0180000000080002", 1}, {"@01810000024b0502", 1}, {"@01810000037d0002", 1}, {"@0181000100440502", 1}, {"@0181000101d40502", 1}, {"@01810100023f0502", 1}, {"@0181010100b40502", 1}, {"@01810201011a0502", 1}, {"@0181030101700502", 1}, {"@0181040103aa0502", 1}, {"@0181050103bf0502", 1}, {"@0182000002400502", 1}, {"@0182000100a80502", 1}, {"@0182000101d80502", 1}, {"@0182000103b20502", 1}, {"@0182010100460502", 1}, {"@0183000002420502", 1}, {"@0183000100450502", 1}, {"@01830101010e0502", 1}, {"@0183020103a80502", 1}, {"@0183030103be0502", 1}, {"@01840000024d0502", 1}, {"@0184050103a90502", 1}, {"@01850001004b0502", 1}, {"@0185020101170502", 1}, {"@0185040101790502", 1}, {"@0186010100af0502", 1}, {"@0186030101750502", 1}, {"@0187000100470502", 1}, {"@0187000103b00502", 1}, {"@0188000002410502", 1}, {"@0188000101120502", 1}, {"@0188000103af0502", 1}, {"@0189000100ab0502", 1}, {"@0189010103b10502", 1}, {"@018a000002450502", 1}, {"@018a000100a90502", 1}, {"@018b000002460502", 1}, {"@018b000101150502", 1}, {"@018c000002430502", 1}, {"@018c0001004c0502", 1}, {"@018c010101180502", 1}, {"@018d0000024c0502", 1}, {"@018d0001010c0502", 1}, {"@018e000002490502", 1}, {"@018e000100490502", 1}, {"@018e010101780502", 1}, {"@018f000100b30502", 1}, {"@018f010101190502", 1}, {"@0190000101710502", 1}, {"@01910001004e0502", 1}, {"@0192000002470502", 1}, {"@01920001010d0502", 1}, {"@0192000103ad0502", 1}, {"@0193000002480502", 1}, {"@0193000101740502", 1}, {"@0193000103ae0502", 1}, {"@01940000024a0502", 1}, {"@0194000100aa0502", 1}, {"@0194000103b60502", 1}, {"@0195000100b00502", 1}, {"@01960000024e0502", 1}, {"@0196000100480502", 1}, {"@0197000101770502", 1}, {"@0198000100b10502", 1}, {"@0199000101160502", 1}, {"@019a000100b70502", 1}, {"@019b000100b60502", 1}, {"@019c000101730502", 1}, {"@019d000100ac0502", 1}, {"@019e000100ad0502", 1}, {"@019f000101110502", 1}, {"@01a00001010f0502", 1}, {"@01a1000101100502", 1}, {"@01a20001017d0502", 1}, {"@01a2000103b90502", 1}, {"@01a30001004a0502", 1}, {"@01a40001004d0502", 1}, {"@01a5000101720502", 1}, {"@01a6000100500502", 1}, {"@01a6000103b70502", 1}, {"@01a7000101140502", 1}, {"@01a80001004f0502", 1}, {"@01a80101017e0502", 1}, {"@01a9000101760502", 1}, {"@01aa000100530502", 1}, {"@01ab0001017c0502", 1}, {"@01ac0001017f0502", 1}, {"@01ad000100b80502", 1}, {"@01ae0001011b0502", 1}, {"@01af0001011c0502", 1}, {"@01b0000100520502", 1}, {"@01b1000100b20502", 1}, {"@01b10101017b0502", 1}, {"@01b3000100b50502", 1}, {"@01b4000101130502", 1}, {"@01b5000100510502", 1}, {"@01b6000100ae0502", 1}, {"@01c1000002440502", 1}, {"@01c1000100540502", 1}, {"@01c10101017a0502", 1}, {"@01c1020103bb0502", 1}, {"@0200000100a10502", 1}, {"@02010001016a0502", 1}, {"@0202000101030502", 1}, {"@02030001019a0502", 1}, {"@0208000100960502", 1}, {"@02090001019f0502", 1}, {"@0214000100e40502", 1}, {"@0215000101820502", 1}, {"@0216000100570502", 1}, {"@0217000101b30502", 1}, {"@02190001007e0502", 1}, {"@021a000100da0502", 1}, {"@021b000100800502", 1}, {"@021d000101cd0502", 1}, {"@021e000101230502", 1}, {"@0220000100fd0502", 1}, {"@02210001013c0502", 1}, {"@0222000101440502", 1}, {"@022d000100f20502", 1}, {"@022e000101d30502", 1}, {"@022f0001011e0502", 1}, {"@0230000101d20502", 1}, {"@02310001006a0502", 1}, {"@0235000100840502", 1}, {"@023c000100bd0502", 1}, {"@023d000101b50502", 1}, {"@023e000100d10502", 1}, {"@023f000101660502", 1}, {"@024a000101d10502", 1}, {"@024b000101260502", 1}, {"@024f000100810502", 1}, {"@0251000100c10502", 1}, {"@0252000100fe0502", 1}, {"@025d000100550502", 1}, {"@025e000101250502", 1}, {"@025f000101c50502", 1}, {"@025f000101d70502", 1}, {"@0260000100d20502", 1}, {"@0261000100650502", 1}, {"@0262000101370502", 1}, {"@0263000100750502", 1}, {"@0264000101ac0502", 1}, {"@0265000101540502", 1}, {"@0266000100680502", 1}, {"@0267000101080502", 1}, {"@02680001007d0502", 1}, {"@02690001011f0502", 1}, {"@026a000101460502", 1}, {"@026b000100e90502", 1}, {"@026c000100c30502", 1}, {"@026d0001013f0502", 1}, {"@026e000100ba0502", 1}, {"@026f000101900502", 1}, {"@0270000100ff0502", 1}, {"@02710001019b0502", 1}, {"@0272000101860502", 1}, {"@027d000100630502", 1}, {"@027e000101690502", 1}, {"@027f000100b90502", 1}, {"@0280000100830502", 1}, {"@0281000101200502", 1}, {"@0282000101810502", 1}, {"@0282000101d60502", 1}, {"@0283000100c70502", 1}, {"@02870001005a0502", 1}, {"@028b000100e30502", 1}, {"@028c0001013e0502", 1}, {"@028d000101bd0502", 1}, {"@028e0001019e0502", 1}, {"@0299000100950502", 1}, {"@029a000100ee0502", 1}, {"@029b000100cb0502", 1}, {"@029e0001013d0502", 1}, {"@02a2000101ba0502", 1}, {"@02a4000100720502", 1}, {"@02a50001018c0502", 1}, {"@02a6000101240502", 1}, {"@02b1000100690502", 1}, {"@02b2000100c40502", 1}, {"@02b80001019c0502", 1}, {"@02c3000100dc0502", 1}, {"@02c4000100670502", 1}, {"@02c7000101220502", 1}, {"@02c9000100cd0502", 1}, {"@02ca000101ca0502", 1}, {"@02cb000101360502", 1}, {"@02d6000100560502", 1}, {"@02d7000101300502", 1}, {"@02d8000100e20502", 1}, {"@02d9000101c80502", 1}, {"@02da000101330502", 1}, {"@02db0001005e0502", 1}, {"@02dc000100be0502", 1}, {"@02dd000100ea0502", 1}, {"@02de0001009c0502", 1}, {"@02df000101910502", 1}, {"@02ea000101800502", 1}, {"@02ea000101d50502", 1}, {"@02eb000100de0502", 1}, {"@02ec000101c40502", 1}, {"@02ed0001015a0502", 1}, {"@02ee000101990502", 1}, {"@02ef000100580502", 1}, {"@02f0000100a70502", 1}, {"@02f1000101450502", 1}, {"@02f2000100cc0502", 1}, {"@02f4000103050502", 1}, {"@02f8000101380502", 1}, {"@02f9000101020502", 1}, {"@02fa000100970502", 1}, {"@02fb000100900502", 1}, {"@02fc0001018f0502", 1}, {"@0307000100640502", 1}, {"@03080001014d0502", 1}, {"@0309000100c60502", 1}, {"@030a000101c70502", 1}, {"@030b000100790502", 1}, {"@030c000101b80502", 1}, {"@030d000101840502", 1}, {"@030e0001012f0502", 1}, {"@030f0001016d0502", 1}, {"@0310000100f80502", 1}, {"@0311000100d60502", 1}, {"@0313000101210502", 1}, {"@0316000101c00502", 1}, {"@0317000100a60502", 1}, {"@03180001006c0502", 1}, {"@0323000100760502", 1}, {"@0324000101890502", 1}, {"@03250001010a0502", 1}, {"@0326000101390502", 1}, {"@0327000101c30502", 1}, {"@03290001009d0502", 1}, {"@032c000101480502", 1}, {"@032d000100bc0502", 1}, {"@03380001011d0502", 1}, {"@0339000101b10502", 1}, {"@033a000101cc0502", 1}, {"@033b000100fa0502", 1}, {"@033c000101000502", 1}, {"@033d0001013a0502", 1}, {"@033e000101a20502", 1}, {"@033f0001008f0502", 1}, {"@0342000101280502", 1}, {"@0344000100c50502", 1}, {"@03450001005f0502", 1}, {"@03480001006b0502", 1}, {"@03490001018d0502", 1}, {"@034a000101430502", 1}, {"@034b0001009f0502", 1}, {"@0356000101350502", 1}, {"@0357000100eb0502", 1}, {"@035a000100850502", 1}, {"@035c000101290502", 1}, {"@035d000100c90502", 1}, {"@035e0001018e0502", 1}, {"@0369000100d30502", 1}, {"@036a0001019d0502", 1}, {"@036b0001018b0502", 1}, {"@03700001015d0502", 1}, {"@03710001005c0502", 1}, {"@03720001010b0502", 1}, {"@0373000101340502", 1}, {"@037e000101560502", 1}, {"@037f000101aa0502", 1}, {"@0380000101870502", 1}, {"@0381000100d50502", 1}, {"@03820001016b0502", 1}, {"@03830001009b0502", 1}, {"@0384000100860502", 1}, {"@0385000101060502", 1}, {"@0390000101850502", 1}, {"@0392000101270502", 1}, {"@0393000100a00502", 1}, {"@0394000100890502", 1}, {"@0398000100bf0502", 1}, {"@0399000101c20502", 1}, {"@03a40001014f0502", 1}, {"@03a50001015b0502", 1}, {"@03a6000100c80502", 1}, {"@03a7000101a10502", 1}, {"@03a8000100910502", 1}, {"@03a9000100710502", 1}, {"@03aa000100e60502", 1}, {"@03ac000101880502", 1}, {"@03ad000101b20502", 1}, {"@03ae000100870502", 1}, {"@03af0001012c0502", 1}, {"@03b0000101a90502", 1}, {"@03b1000100f00502", 1}, {"@03bc0001008a0502", 1}, {"@03bd000100f90502", 1}, {"@03be000101980502", 1}, {"@03bf000101bc0502", 1}, {"@03c1000100bb0502", 1}, {"@03c40001012b0502", 1}, {"@03c50001015c0502", 1}, {"@03c6000100930502", 1}, {"@03d1000100c20502", 1}, {"@03d2000100e50502", 1}, {"@03d6000101570502", 1}, {"@03d7000101b40502", 1}, {"@03d9000101a50502", 1}, {"@03da000101510502", 1}, {"@03db0001006d0502", 1}, {"@03e6000100ec0502", 1}, {"@03e70001012a0502", 1}, {"@03ec000101830502", 1}, {"@03ed000101a30502", 1}, {"@03ee0001008b0502", 1}, {"@03fa000100d00502", 1}, {"@03fb000101cf0502", 1}, {"@03fc000101470502", 1}, {"@03fd000101580502", 1}, {"@03fe000101a40502", 1}, {"@03ff000100f40502", 1}, {"@04000001006f0502", 1}, {"@0401000100660502", 1}, {"@040c000101590502", 1}, {"@040d000100780502", 1}, {"@040e000100880502", 1}, {"@040f000101500502", 1}, {"@04100001007f0502", 1}, {"@0411000101ab0502", 1}, {"@0415000101bb0502", 1}, {"@0416000100fb0502", 1}, {"@0418000100d80502", 1}, {"@041a000100e00502", 1}, {"@041b000100f10502", 1}, {"@041c000101410502", 1}, {"@041d0001018a0502", 1}, {"@041e0001015f0502", 1}, {"@0429000100700502", 1}, {"@042a0001012d0502", 1}, {"@042b000101af0502", 1}, {"@0436000101940502", 1}, {"@0437000101050502", 1}, {"@043b000103030502", 1}, {"@043c000101cb0502", 1}, {"@043d0001007c0502", 1}, {"@043e000101490502", 1}, {"@043f000101550502", 1}, {"@0440000100ca0502", 1}, {"@044b0001016c0502", 1}, {"@044c0001008e0502", 1}, {"@044d000101930502", 1}, {"@0450000100cf0502", 1}, {"@04510001015e0502", 1}, {"@0452000100730502", 1}, {"@0453000101040502", 1}, {"@0454000101ae0502", 1}, {"@045f000101a80502", 1}, {"@0460000100a50502", 1}, {"@0461000101610502", 1}, {"@0462000100f60502", 1}, {"@0463000101310502", 1}, {"@0464000100c00502", 1}, {"@04650001006e0502", 1}, {"@0469000101640502", 1}, {"@046a000101d00502", 1}, {"@046b000101970502", 1}, {"@046c0001008c0502", 1}, {"@046d000100f30502", 1}, {"@0478000101630502", 1}, {"@0479000100920502", 1}, {"@047a000100600502", 1}, {"@047b000100f50502", 1}, {"@047c000101a00502", 1}, {"@047d0001012e0502", 1}, {"@04800001008d0502", 1}, {"@0483000101b00502", 1}, {"@04850001014c0502", 1}, {"@0486000100fc0502", 1}, {"@0487000101bf0502", 1}, {"@0488000100980502", 1}, {"@0489000100ef0502", 1}, {"@04940001009a0502", 1}, {"@0495000101920502", 1}, {"@0496000100d90502", 1}, {"@04970001007a0502", 1}, {"@04980001014a0502", 1}, {"@0499000100df0502", 1}, {"@049a0001014e0502", 1}, {"@049b000100610502", 1}, {"@049c000101400502", 1}, {"@049d000100ed0502", 1}, {"@049e000101b70502", 1}, {"@04a00001016e0502", 1}, {"@04a10001016f0502", 1}, {"@04a3000101c90502", 1}, {"@04a4000100d40502", 1}, {"@04a5000100740502", 1}, {"@04a6000100a30502", 1}, {"@04a7000101a60502", 1}, {"@04b2000101b90502", 1}, {"@04b3000100dd0502", 1}, {"@04b9000101600502", 1}, {"@04ba0001005d0502", 1}, {"@04c5000101010502", 1}, {"@04c6000101670502", 1}, {"@04c7000100940502", 1}, {"@04cc000100a40502", 1}, {"@04cd000101520502", 1}, {"@04ce000100db0502", 1}, {"@04cf000100e10502", 1}, {"@04d0000101960502", 1}, {"@04d10001009e0502", 1}, {"@04d2000101a70502", 1}, {"@04dd000100a20502", 1}, {"@04de000100ce0502", 1}, {"@04df000100e80502", 1}, {"@04e0000100f70502", 1}, {"@04e1000101be0502", 1}, {"@04e2000101090502", 1}, {"@04e3000101650502", 1}, {"@04e4000101b60502", 1}, {"@04e5000101ad0502", 1}, {"@04e6000100820502", 1}, {"@04e7000101320502", 1}, {"@04e8000101ce0502", 1}, {"@04ec000100770502", 1}, {"@04ed000100620502", 1}, {"@04ee0001014b0502", 1}, {"@04ef0001013b0502", 1}, {"@04fa000101680502", 1}, {"@04fb000101c60502", 1}, {"@04fd0001007b0502", 1}, {"@04fe000100590502", 1}, {"@04ff000101620502", 1}, {"@0500000100e70502", 1}, {"@050b000100990502", 1}, {"@050c000101c10502", 1}, {"@050d000101420502", 1}, {"@050e000100d70502", 1}, {"@0510000101070502", 1}, {"@0511000101950502", 1}, {"@0514000101530502", 1}, {"@05150001005b0502", 1}, {"@0a12000103c90502", 1} } },
	{"000400000014F200", { {"@0180000000080002", 1}, {"@01810000024b0502", 1}, {"@01810000037d0002", 1}, {"@0181000100440502", 1}, {"@0181000101d40502", 1}, {"@01810100023f0502", 1}, {"@0181010100b40502", 1}, {"@01810201011a0502", 1}, {"@0181030101700502", 1}, {"@0181040103aa0502", 1}, {"@0181050103bf0502", 1}, {"@0182000002400502", 1}, {"@0182000100a80502", 1}, {"@0182000101d80502", 1}, {"@0182000103b20502", 1}, {"@0182010100460502", 1}, {"@0183000002420502", 1}, {"@0183000100450502", 1}, {"@01830101010e0502", 1}, {"@0183020103a80502", 1}, {"@0183030103be0502", 1}, {"@01840000024d0502", 1}, {"@0184050103a90502", 1}, {"@01850001004b0502", 1}, {"@0185020101170502", 1}, {"@0185040101790502", 1}, {"@0186010100af0502", 1}, {"@0186030101750502", 1}, {"@0187000100470502", 1}, {"@0187000103b00502", 1}, {"@0188000002410502", 1}, {"@0188000101120502", 1}, {"@0188000103af0502", 1}, {"@0189000100ab0502", 1}, {"@0189010103b10502", 1}, {"@018a000002450502", 1}, {"@018a000100a90502", 1}, {"@018b000002460502", 1}, {"@018b000101150502", 1}, {"@018c000002430502", 1}, {"@018c0001004c0502", 1}, {"@018c010101180502", 1}, {"@018d0000024c0502", 1}, {"@018d0001010c0502", 1}, {"@018e000002490502", 1}, {"@018e000100490502", 1}, {"@018e010101780502", 1}, {"@018f000100b30502", 1}, {"@018f010101190502", 1}, {"@0190000101710502", 1}, {"@01910001004e0502", 1}, {"@0192000002470502", 1}, {"@01920001010d0502", 1}, {"@0192000103ad0502", 1}, {"@0193000002480502", 1}, {"@0193000101740502", 1}, {"@0193000103ae0502", 1}, {"@01940000024a0502", 1}, {"@0194000100aa0502", 1}, {"@0194000103b60502", 1}, {"@0195000100b00502", 1}, {"@01960000024e0502", 1}, {"@0196000100480502", 1}, {"@0197000101770502", 1}, {"@0198000100b10502", 1}, {"@0199000101160502", 1}, {"@019a000100b70502", 1}, {"@019b000100b60502", 1}, {"@019c000101730502", 1}, {"@019d000100ac0502", 1}, {"@019e000100ad0502", 1}, {"@019f000101110502", 1}, {"@01a00001010f0502", 1}, {"@01a1000101100502", 1}, {"@01a20001017d0502", 1}, {"@01a2000103b90502", 1}, {"@01a30001004a0502", 1}, {"@01a40001004d0502", 1}, {"@01a5000101720502", 1}, {"@01a6000100500502", 1}, {"@01a6000103b70502", 1}, {"@01a7000101140502", 1}, {"@01a80001004f0502", 1}, {"@01a80101017e0502", 1}, {"@01a9000101760502", 1}, {"@01aa000100530502", 1}, {"@01ab0001017c0502", 1}, {"@01ac0001017f0502", 1}, {"@01ad000100b80502", 1}, {"@01ae0001011b0502", 1}, {"@01af0001011c0502", 1}, {"@01b0000100520502", 1}, {"@01b1000100b20502", 1}, {"@01b10101017b0502", 1}, {"@01b3000100b50502", 1}, {"@01b4000101130502", 1}, {"@01b5000100510502", 1}, {"@01b6000100ae0502", 1}, {"@01c1000002440502", 1}, {"@01c1000100540502", 1}, {"@01c10101017a0502", 1}, {"@01c1020103bb0502", 1}, {"@0200000100a10502", 1}, {"@02010001016a0502", 1}, {"@0202000101030502", 1}, {"@02030001019a0502", 1}, {"@0208000100960502", 1}, {"@02090001019f0502", 1}, {"@0214000100e40502", 1}, {"@0215000101820502", 1}, {"@0216000100570502", 1}, {"@0217000101b30502", 1}, {"@02190001007e0502", 1}, {"@021a000100da0502", 1}, {"@021b000100800502", 1}, {"@021d000101cd0502", 1}, {"@021e000101230502", 1}, {"@0220000100fd0502", 1}, {"@02210001013c0502", 1}, {"@0222000101440502", 1}, {"@022d000100f20502", 1}, {"@022e000101d30502", 1}, {"@022f0001011e0502", 1}, {"@0230000101d20502", 1}, {"@02310001006a0502", 1}, {"@0235000100840502", 1}, {"@023c000100bd0502", 1}, {"@023d000101b50502", 1}, {"@023e000100d10502", 1}, {"@023f000101660502", 1}, {"@024a000101d10502", 1}, {"@024b000101260502", 1}, {"@024f000100810502", 1}, {"@0251000100c10502", 1}, {"@0252000100fe0502", 1}, {"@025d000100550502", 1}, {"@025e000101250502", 1}, {"@025f000101c50502", 1}, {"@025f000101d70502", 1}, {"@0260000100d20502", 1}, {"@0261000100650502", 1}, {"@0262000101370502", 1}, {"@0263000100750502", 1}, {"@0264000101ac0502", 1}, {"@0265000101540502", 1}, {"@0266000100680502", 1}, {"@0267000101080502", 1}, {"@02680001007d0502", 1}, {"@02690001011f0502", 1}, {"@026a000101460502", 1}, {"@026b000100e90502", 1}, {"@026c000100c30502", 1}, {"@026d0001013f0502", 1}, {"@026e000100ba0502", 1}, {"@026f000101900502", 1}, {"@0270000100ff0502", 1}, {"@02710001019b0502", 1}, {"@0272000101860502", 1}, {"@027d000100630502", 1}, {"@027e000101690502", 1}, {"@027f000100b90502", 1}, {"@0280000100830502", 1}, {"@0281000101200502", 1}, {"@0282000101810502", 1}, {"@0282000101d60502", 1}, {"@0283000100c70502", 1}, {"@02870001005a0502", 1}, {"@028b000100e30502", 1}, {"@028c0001013e0502", 1}, {"@028d000101bd0502", 1}, {"@028e0001019e0502", 1}, {"@0299000100950502", 1}, {"@029a000100ee0502", 1}, {"@029b000100cb0502", 1}, {"@029e0001013d0502", 1}, {"@02a2000101ba0502", 1}, {"@02a4000100720502", 1}, {"@02a50001018c0502", 1}, {"@02a6000101240502", 1}, {"@02b1000100690502", 1}, {"@02b2000100c40502", 1}, {"@02b80001019c0502", 1}, {"@02c3000100dc0502", 1}, {"@02c4000100670502", 1}, {"@02c7000101220502", 1}, {"@02c9000100cd0502", 1}, {"@02ca000101ca0502", 1}, {"@02cb000101360502", 1}, {"@02d6000100560502", 1}, {"@02d7000101300502", 1}, {"@02d8000100e20502", 1}, {"@02d9000101c80502", 1}, {"@02da000101330502", 1}, {"@02db0001005e0502", 1}, {"@02dc000100be0502", 1}, {"@02dd000100ea0502", 1}, {"@02de0001009c0502", 1}, {"@02df000101910502", 1}, {"@02ea000101800502", 1}, {"@02ea000101d50502", 1}, {"@02eb000100de0502", 1}, {"@02ec000101c40502", 1}, {"@02ed0001015a0502", 1}, {"@02ee000101990502", 1}, {"@02ef000100580502", 1}, {"@02f0000100a70502", 1}, {"@02f1000101450502", 1}, {"@02f2000100cc0502", 1}, {"@02f4000103050502", 1}, {"@02f8000101380502", 1}, {"@02f9000101020502", 1}, {"@02fa000100970502", 1}, {"@02fb000100900502", 1}, {"@02fc0001018f0502", 1}, {"@0307000100640502", 1}, {"@03080001014d0502", 1}, {"@0309000100c60502", 1}, {"@030a000101c70502", 1}, {"@030b000100790502", 1}, {"@030c000101b80502", 1}, {"@030d000101840502", 1}, {"@030e0001012f0502", 1}, {"@030f0001016d0502", 1}, {"@0310000100f80502", 1}, {"@0311000100d60502", 1}, {"@0313000101210502", 1}, {"@0316000101c00502", 1}, {"@0317000100a60502", 1}, {"@03180001006c0502", 1}, {"@0323000100760502", 1}, {"@0324000101890502", 1}, {"@03250001010a0502", 1}, {"@0326000101390502", 1}, {"@0327000101c30502", 1}, {"@03290001009d0502", 1}, {"@032c000101480502", 1}, {"@032d000100bc0502", 1}, {"@03380001011d0502", 1}, {"@0339000101b10502", 1}, {"@033a000101cc0502", 1}, {"@033b000100fa0502", 1}, {"@033c000101000502", 1}, {"@033d0001013a0502", 1}, {"@033e000101a20502", 1}, {"@033f0001008f0502", 1}, {"@0342000101280502", 1}, {"@0344000100c50502", 1}, {"@03450001005f0502", 1}, {"@03480001006b0502", 1}, {"@03490001018d0502", 1}, {"@034a000101430502", 1}, {"@034b0001009f0502", 1}, {"@0356000101350502", 1}, {"@0357000100eb0502", 1}, {"@035a000100850502", 1}, {"@035c000101290502", 1}, {"@035d000100c90502", 1}, {"@035e0001018e0502", 1}, {"@0369000100d30502", 1}, {"@036a0001019d0502", 1}, {"@036b0001018b0502", 1}, {"@03700001015d0502", 1}, {"@03710001005c0502", 1}, {"@03720001010b0502", 1}, {"@0373000101340502", 1}, {"@037e000101560502", 1}, {"@037f000101aa0502", 1}, {"@0380000101870502", 1}, {"@0381000100d50502", 1}, {"@03820001016b0502", 1}, {"@03830001009b0502", 1}, {"@0384000100860502", 1}, {"@0385000101060502", 1}, {"@0390000101850502", 1}, {"@0392000101270502", 1}, {"@0393000100a00502", 1}, {"@0394000100890502", 1}, {"@0398000100bf0502", 1}, {"@0399000101c20502", 1}, {"@03a40001014f0502", 1}, {"@03a50001015b0502", 1}, {"@03a6000100c80502", 1}, {"@03a7000101a10502", 1}, {"@03a8000100910502", 1}, {"@03a9000100710502", 1}, {"@03aa000100e60502", 1}, {"@03ac000101880502", 1}, {"@03ad000101b20502", 1}, {"@03ae000100870502", 1}, {"@03af0001012c0502", 1}, {"@03b0000101a90502", 1}, {"@03b1000100f00502", 1}, {"@03bc0001008a0502", 1}, {"@03bd000100f90502", 1}, {"@03be000101980502", 1}, {"@03bf000101bc0502", 1}, {"@03c1000100bb0502", 1}, {"@03c40001012b0502", 1}, {"@03c50001015c0502", 1}, {"@03c6000100930502", 1}, {"@03d1000100c20502", 1}, {"@03d2000100e50502", 1}, {"@03d6000101570502", 1}, {"@03d7000101b40502", 1}, {"@03d9000101a50502", 1}, {"@03da000101510502", 1}, {"@03db0001006d0502", 1}, {"@03e6000100ec0502", 1}, {"@03e70001012a0502", 1}, {"@03ec000101830502", 1}, {"@03ed000101a30502", 1}, {"@03ee0001008b0502", 1}, {"@03fa000100d00502", 1}, {"@03fb000101cf0502", 1}, {"@03fc000101470502", 1}, {"@03fd000101580502", 1}, {"@03fe000101a40502", 1}, {"@03ff000100f40502", 1}, {"@04000001006f0502", 1}, {"@0401000100660502", 1}, {"@040c000101590502", 1}, {"@040d000100780502", 1}, {"@040e000100880502", 1}, {"@040f000101500502", 1}, {"@04100001007f0502", 1}, {"@0411000101ab0502", 1}, {"@0415000101bb0502", 1}, {"@0416000100fb0502", 1}, {"@0418000100d80502", 1}, {"@041a000100e00502", 1}, {"@041b000100f10502", 1}, {"@041c000101410502", 1}, {"@041d0001018a0502", 1}, {"@041e0001015f0502", 1}, {"@0429000100700502", 1}, {"@042a0001012d0502", 1}, {"@042b000101af0502", 1}, {"@0436000101940502", 1}, {"@0437000101050502", 1}, {"@043b000103030502", 1}, {"@043c000101cb0502", 1}, {"@043d0001007c0502", 1}, {"@043e000101490502", 1}, {"@043f000101550502", 1}, {"@0440000100ca0502", 1}, {"@044b0001016c0502", 1}, {"@044c0001008e0502", 1}, {"@044d000101930502", 1}, {"@0450000100cf0502", 1}, {"@04510001015e0502", 1}, {"@0452000100730502", 1}, {"@0453000101040502", 1}, {"@0454000101ae0502", 1}, {"@045f000101a80502", 1}, {"@0460000100a50502", 1}, {"@0461000101610502", 1}, {"@0462000100f60502", 1}, {"@0463000101310502", 1}, {"@0464000100c00502", 1}, {"@04650001006e0502", 1}, {"@0469000101640502", 1}, {"@046a000101d00502", 1}, {"@046b000101970502", 1}, {"@046c0001008c0502", 1}, {"@046d000100f30502", 1}, {"@0478000101630502", 1}, {"@0479000100920502", 1}, {"@047a000100600502", 1}, {"@047b000100f50502", 1}, {"@047c000101a00502", 1}, {"@047d0001012e0502", 1}, {"@04800001008d0502", 1}, {"@0483000101b00502", 1}, {"@04850001014c0502", 1}, {"@0486000100fc0502", 1}, {"@0487000101bf0502", 1}, {"@0488000100980502", 1}, {"@0489000100ef0502", 1}, {"@04940001009a0502", 1}, {"@0495000101920502", 1}, {"@0496000100d90502", 1}, {"@04970001007a0502", 1}, {"@04980001014a0502", 1}, {"@0499000100df0502", 1}, {"@049a0001014e0502", 1}, {"@049b000100610502", 1}, {"@049c000101400502", 1}, {"@049d000100ed0502", 1}, {"@049e000101b70502", 1}, {"@04a00001016e0502", 1}, {"@04a10001016f0502", 1}, {"@04a3000101c90502", 1}, {"@04a4000100d40502", 1}, {"@04a5000100740502", 1}, {"@04a6000100a30502", 1}, {"@04a7000101a60502", 1}, {"@04b2000101b90502", 1}, {"@04b3000100dd0502", 1}, {"@04b9000101600502", 1}, {"@04ba0001005d0502", 1}, {"@04c5000101010502", 1}, {"@04c6000101670502", 1}, {"@04c7000100940502", 1}, {"@04cc000100a40502", 1}, {"@04cd000101520502", 1}, {"@04ce000100db0502", 1}, {"@04cf000100e10502", 1}, {"@04d0000101960502", 1}, {"@04d10001009e0502", 1}, {"@04d2000101a70502", 1}, {"@04dd000100a20502", 1}, {"@04de000100ce0502", 1}, {"@04df000100e80502", 1}, {"@04e0000100f70502", 1}, {"@04e1000101be0502", 1}, {"@04e2000101090502", 1}, {"@04e3000101650502", 1}, {"@04e4000101b60502", 1}, {"@04e5000101ad0502", 1}, {"@04e6000100820502", 1}, {"@04e7000101320502", 1}, {"@04e8000101ce0502", 1}, {"@04ec000100770502", 1}, {"@04ed000100620502", 1}, {"@04ee0001014b0502", 1}, {"@04ef0001013b0502", 1}, {"@04fa000101680502", 1}, {"@04fb000101c60502", 1}, {"@04fd0001007b0502", 1}, {"@04fe000100590502", 1}, {"@04ff000101620502", 1}, {"@0500000100e70502", 1}, {"@050b000100990502", 1}, {"@050c000101c10502", 1}, {"@050d000101420502", 1}, {"@050e000100d70502", 1}, {"@0510000101070502", 1}, {"@0511000101950502", 1}, {"@0514000101530502", 1}, {"@05150001005b0502", 1}, {"@0a12000103c90502", 1} } },
	{"00040000001BB200", { {"@05c0000000060002", 1}, {"@05c0000003651302", 1}, {"@05c0000004121302", 1}, {"@05c00000043a1302", 1}, {"@05c00000043b1302", 1}, {"@05c00100001d0002", 1}, {"@05c1000003661302", 1} } },
	{"00040000001BFB00", { {"@05c0000000060002", 1}, {"@05c0000003651302", 1}, {"@05c0000004121302", 1}, {"@05c00000043a1302", 1}, {"@05c00000043b1302", 1}, {"@05c00100001d0002", 1}, {"@05c1000003661302", 1} } },
	{"00040000001BFC00", { {"@05c0000000060002", 1}, {"@05c0000003651302", 1}, {"@05c0000004121302", 1}, {"@05c00000043a1302", 1}, {"@05c00000043b1302", 1}, {"@05c00100001d0002", 1}, {"@05c1000003661302", 1} } },
	{"0005000010116400", { {"@08010000025d0402", 1}, {"@08020000025e0402", 1} } },
	{"0004000000188B00", { {"@09c0010102690e02", 1}, {"@09c00201026a0e02", 1}, {"@09c00301026b0e02", 1}, {"@09c00401026c0e02", 1}, {"@09c00501026d0e02", 1}, {"@09c10101026e0e02", 1}, {"@09c10201026f0e02", 1}, {"@09c1030102700e02", 1}, {"@09c1040102710e02", 1}, {"@09c1050102720e02", 1}, {"@09c2010102730e02", 1}, {"@09c2020102740e02", 1}, {"@09c2030102750e02", 1}, {"@09c2040102760e02", 1}, {"@09c2050102770e02", 1}, {"@09c3010102780e02", 1}, {"@09c3020102790e02", 1}, {"@09c30301027a0e02", 1}, {"@09c30401027b0e02", 1}, {"@09c30501027c0e02", 1}, {"@09c40101027d0e02", 1}, {"@09c40201027e0e02", 1}, {"@09c40301027f0e02", 1}, {"@09c4040102800e02", 1}, {"@09c4050102810e02", 1}, {"@09c5010102820e02", 1}, {"@09c5020102830e02", 1}, {"@09c5030102840e02", 1}, {"@09c5040102850e02", 1}, {"@09c5050102860e02", 1}, {"@09c6010102870e02", 1}, {"@09c6020102880e02", 1}, {"@09c6030102890e02", 1}, {"@09c60401028a0e02", 1}, {"@09c60501028b0e02", 1}, {"@09c70101028c0e02", 1}, {"@09c70201028d0e02", 1}, {"@09c70301028e0e02", 1}, {"@09c70401028f0e02", 1}, {"@09c7050102900e02", 1}, {"@09c8010102910e02", 1}, {"@09c8020102920e02", 1}, {"@09c8030102930e02", 1}, {"@09c8040102940e02", 1}, {"@09c8050102950e02", 1}, {"@09c9010102960e02", 1}, {"@09c9020102970e02", 1}, {"@09c9030102980e02", 1}, {"@09c9040102990e02", 1}, {"@09c90501029a0e02", 1}, {"@09ca0101029b0e02", 1}, {"@09ca0201029c0e02", 1}, {"@09ca0301029d0e02", 1}, {"@09ca0401029e0e02", 1}, {"@09ca0501029f0e02", 1}, {"@09cb010102a00e02", 1}, {"@09cb020102a10e02", 1}, {"@09cb030102a20e02", 1}, {"@09cb040102a30e02", 1}, {"@09cb050102a40e02", 1}, {"@09cc010102a50e02", 1}, {"@09cc020102a60e02", 1}, {"@09cc030102a70e02", 1}, {"@09cc040102a80e02", 1}, {"@09cc050102a90e02", 1}, {"@09cd010102aa0e02", 1}, {"@09cd020102ab0e02", 1}, {"@09cd030102ac0e02", 1}, {"@09cd040102ad0e02", 1}, {"@09cd050102ae0e02", 1}, {"@09ce010102af0e02", 1}, {"@09ce020102b00e02", 1}, {"@09ce030102b10e02", 1}, {"@09ce040102b20e02", 1}, {"@09ce050102b30e02", 1}, {"@09cf010102b40e02", 1}, {"@09cf020102b50e02", 1}, {"@09cf030102b60e02", 1}, {"@09cf040102b70e02", 1}, {"@09cf050102b80e02", 1}, {"@09d0010102b90e02", 1}, {"@09d0020102ba0e02", 1}, {"@09d0030102bb0e02", 1}, {"@09d0040102bc0e02", 1}, {"@09d0050102bd0e02", 1}, {"@09d1010102be0e02", 1}, {"@09d1020102bf0e02", 1}, {"@09d1030102c00e02", 1}, {"@09d1040102c10e02", 1}, {"@09d1050102c20e02", 1} } },
	{"0004000000188C00", { {"@09c0010102690e02", 1}, {"@09c00201026a0e02", 1}, {"@09c00301026b0e02", 1}, {"@09c00401026c0e02", 1}, {"@09c00501026d0e02", 1}, {"@09c10101026e0e02", 1}, {"@09c10201026f0e02", 1}, {"@09c1030102700e02", 1}, {"@09c1040102710e02", 1}, {"@09c1050102720e02", 1}, {"@09c2010102730e02", 1}, {"@09c2020102740e02", 1}, {"@09c2030102750e02", 1}, {"@09c2040102760e02", 1}, {"@09c2050102770e02", 1}, {"@09c3010102780e02", 1}, {"@09c3020102790e02", 1}, {"@09c30301027a0e02", 1}, {"@09c30401027b0e02", 1}, {"@09c30501027c0e02", 1}, {"@09c40101027d0e02", 1}, {"@09c40201027e0e02", 1}, {"@09c40301027f0e02", 1}, {"@09c4040102800e02", 1}, {"@09c4050102810e02", 1}, {"@09c5010102820e02", 1}, {"@09c5020102830e02", 1}, {"@09c5030102840e02", 1}, {"@09c5040102850e02", 1}, {"@09c5050102860e02", 1}, {"@09c6010102870e02", 1}, {"@09c6020102880e02", 1}, {"@09c6030102890e02", 1}, {"@09c60401028a0e02", 1}, {"@09c60501028b0e02", 1}, {"@09c70101028c0e02", 1}, {"@09c70201028d0e02", 1}, {"@09c70301028e0e02", 1}, {"@09c70401028f0e02", 1}, {"@09c7050102900e02", 1}, {"@09c8010102910e02", 1}, {"@09c8020102920e02", 1}, {"@09c8030102930e02", 1}, {"@09c8040102940e02", 1}, {"@09c8050102950e02", 1}, {"@09c9010102960e02", 1}, {"@09c9020102970e02", 1}, {"@09c9030102980e02", 1}, {"@09c9040102990e02", 1}, {"@09c90501029a0e02", 1}, {"@09ca0101029b0e02", 1}, {"@09ca0201029c0e02", 1}, {"@09ca0301029d0e02", 1}, {"@09ca0401029e0e02", 1}, {"@09ca0501029f0e02", 1}, {"@09cb010102a00e02", 1}, {"@09cb020102a10e02", 1}, {"@09cb030102a20e02", 1}, {"@09cb040102a30e02", 1}, {"@09cb050102a40e02", 1}, {"@09cc010102a50e02", 1}, {"@09cc020102a60e02", 1}, {"@09cc030102a70e02", 1}, {"@09cc040102a80e02", 1}, {"@09cc050102a90e02", 1}, {"@09cd010102aa0e02", 1}, {"@09cd020102ab0e02", 1}, {"@09cd030102ac0e02", 1}, {"@09cd040102ad0e02", 1}, {"@09cd050102ae0e02", 1}, {"@09ce010102af0e02", 1}, {"@09ce020102b00e02", 1}, {"@09ce030102b10e02", 1}, {"@09ce040102b20e02", 1}, {"@09ce050102b30e02", 1}, {"@09cf010102b40e02", 1}, {"@09cf020102b50e02", 1}, {"@09cf030102b60e02", 1}, {"@09cf040102b70e02", 1}, {"@09cf050102b80e02", 1}, {"@09d0010102b90e02", 1}, {"@09d0020102ba0e02", 1}, {"@09d0030102bb0e02", 1}, {"@09d0040102bc0e02", 1}, {"@09d0050102bd0e02", 1}, {"@09d1010102be0e02", 1}, {"@09d1020102bf0e02", 1}, {"@09d1030102c00e02", 1}, {"@09d1040102c10e02", 1}, {"@09d1050102c20e02", 1} } },
	{"0004000000188D00", { {"@09c0010102690e02", 1}, {"@09c00201026a0e02", 1}, {"@09c00301026b0e02", 1}, {"@09c00401026c0e02", 1}, {"@09c00501026d0e02", 1}, {"@09c10101026e0e02", 1}, {"@09c10201026f0e02", 1}, {"@09c1030102700e02", 1}, {"@09c1040102710e02", 1}, {"@09c1050102720e02", 1}, {"@09c2010102730e02", 1}, {"@09c2020102740e02", 1}, {"@09c2030102750e02", 1}, {"@09c2040102760e02", 1}, {"@09c2050102770e02", 1}, {"@09c3010102780e02", 1}, {"@09c3020102790e02", 1}, {"@09c30301027a0e02", 1}, {"@09c30401027b0e02", 1}, {"@09c30501027c0e02", 1}, {"@09c40101027d0e02", 1}, {"@09c40201027e0e02", 1}, {"@09c40301027f0e02", 1}, {"@09c4040102800e02", 1}, {"@09c4050102810e02", 1}, {"@09c5010102820e02", 1}, {"@09c5020102830e02", 1}, {"@09c5030102840e02", 1}, {"@09c5040102850e02", 1}, {"@09c5050102860e02", 1}, {"@09c6010102870e02", 1}, {"@09c6020102880e02", 1}, {"@09c6030102890e02", 1}, {"@09c60401028a0e02", 1}, {"@09c60501028b0e02", 1}, {"@09c70101028c0e02", 1}, {"@09c70201028d0e02", 1}, {"@09c70301028e0e02", 1}, {"@09c70401028f0e02", 1}, {"@09c7050102900e02", 1}, {"@09c8010102910e02", 1}, {"@09c8020102920e02", 1}, {"@09c8030102930e02", 1}, {"@09c8040102940e02", 1}, {"@09c8050102950e02", 1}, {"@09c9010102960e02", 1}, {"@09c9020102970e02", 1}, {"@09c9030102980e02", 1}, {"@09c9040102990e02", 1}, {"@09c90501029a0e02", 1}, {"@09ca0101029b0e02", 1}, {"@09ca0201029c0e02", 1}, {"@09ca0301029d0e02", 1}, {"@09ca0401029e0e02", 1}, {"@09ca0501029f0e02", 1}, {"@09cb010102a00e02", 1}, {"@09cb020102a10e02", 1}, {"@09cb030102a20e02", 1}, {"@09cb040102a30e02", 1}, {"@09cb050102a40e02", 1}, {"@09cc010102a50e02", 1}, {"@09cc020102a60e02", 1}, {"@09cc030102a70e02", 1}, {"@09cc040102a80e02", 1}, {"@09cc050102a90e02", 1}, {"@09cd010102aa0e02", 1}, {"@09cd020102ab0e02", 1}, {"@09cd030102ac0e02", 1}, {"@09cd040102ad0e02", 1}, {"@09cd050102ae0e02", 1}, {"@09ce010102af0e02", 1}, {"@09ce020102b00e02", 1}, {"@09ce030102b10e02", 1}, {"@09ce040102b20e02", 1}, {"@09ce050102b30e02", 1}, {"@09cf010102b40e02", 1}, {"@09cf020102b50e02", 1}, {"@09cf030102b60e02", 1}, {"@09cf040102b70e02", 1}, {"@09cf050102b80e02", 1}, {"@09d0010102b90e02", 1}, {"@09d0020102ba0e02", 1}, {"@09d0030102bb0e02", 1}, {"@09d0040102bc0e02", 1}, {"@09d0050102bd0e02", 1}, {"@09d1010102be0e02", 1}, {"@09d1020102bf0e02", 1}, {"@09d1030102c00e02", 1}, {"@09d1040102c10e02", 1}, {"@09d1050102c20e02", 1} } },
	{"00040000001C1E00", { {"@1d01000003750d02", 1} } },
	{"00040000001B5300", { {"@1f000000000a0002", 1}, {"@1f00000002540c02", 1}, {"@1f01000000270002", 1}, {"@1f01000002550c02", 1}, {"@1f02000000280002", 1}, {"@1f02000002560c02", 1}, {"@1f03000002570c02", 1}, {"@1f400000035e1002", 1} } },
	{"00040000001B5400", { {"@1f000000000a0002", 1}, {"@1f00000002540c02", 1}, {"@1f01000000270002", 1}, {"@1f01000002550c02", 1}, {"@1f02000000280002", 1}, {"@1f02000002560c02", 1}, {"@1f03000002570c02", 1}, {"@1f400000035e1002", 1} } },
	{"00040000001C2000", { {"@1f000000000a0002", 1}, {"@1f00000002540c02", 1}, {"@1f01000000270002", 1}, {"@1f01000002550c02", 1}, {"@1f02000000280002", 1}, {"@1f02000002560c02", 1}, {"@1f03000002570c02", 1}, {"@1f400000035e1002", 1} } },
	{"00040000001C2100", { {"@1f000000000a0002", 1}, {"@1f00000002540c02", 1}, {"@1f01000000270002", 1}, {"@1f01000002550c02", 1}, {"@1f02000000280002", 1}, {"@1f02000002560c02", 1}, {"@1f03000002570c02", 1}, {"@1f400000035e1002", 1} } },
	{"0004000000196F00", { {"@1f000000000a0002", 1}, {"@1f00000002540c02", 1}, {"@1f01000000270002", 1}, {"@1f01000002550c02", 1}, {"@1f02000000280002", 1}, {"@1f02000002560c02", 1}, {"@1f03000002570c02", 1} } },
	{"00040000001D1E00", { {"@1f000000000a0002", 1}, {"@1f00000002540c02", 1}, {"@1f01000000270002", 1}, {"@1f01000002550c02", 1}, {"@1f02000000280002", 1}, {"@1f02000002560c02", 1}, {"@1f03000002570c02", 1} } },
	{"00040000001D1F00", { {"@1f000000000a0002", 1}, {"@1f00000002540c02", 1}, {"@1f01000000270002", 1}, {"@1f01000002550c02", 1}, {"@1f02000000280002", 1}, {"@1f02000002560c02", 1}, {"@1f03000002570c02", 1} } },
	{"00040000001AB800", { {"@1f000000000a0002", 1}, {"@1f00000002540c02", 1}, {"@1f01000000270002", 1}, {"@1f01000002550c02", 1}, {"@1f02000000280002", 1}, {"@1f02000002560c02", 1}, {"@1f03000002570c02", 1}, {"@1f400000035e1002", 1} } },
	{"00040000001AB900", { {"@1f000000000a0002", 1}, {"@1f00000002540c02", 1}, {"@1f01000000270002", 1}, {"@1f01000002550c02", 1}, {"@1f02000000280002", 1}, {"@1f02000002560c02", 1}, {"@1f03000002570c02", 1}, {"@1f400000035e1002", 1} } },
	{"0004000000132500", { {"@21000000000b0002", 1}, {"@2101000000180002", 1}, {"@2102000000290002", 1}, {"@21030000002a0002", 1} } },
	{"00040000001B4000", { {"@21000000000b0002", 1}, {"@2101000000180002", 1}, {"@2102000000290002", 1}, {"@21030000002a0002", 1}, {"@2104000002520002", 1}, {"@21050000025a0002", 1}, {"@2105010003630002", 1}, {"@2106000003601202", 1}, {"@2107000003611202", 1} } },
	{"00040000001B4100", { {"@21000000000b0002", 1}, {"@2101000000180002", 1}, {"@2102000000290002", 1}, {"@21030000002a0002", 1}, {"@2104000002520002", 1}, {"@21050000025a0002", 1}, {"@2105010003630002", 1}, {"@2106000003601202", 1}, {"@2107000003611202", 1} } },
	{"0004000000179400", { {"@21000000000b0002", 1}, {"@2101000000180002", 1}, {"@2102000000290002", 1}, {"@21030000002a0002", 1} } },
	{"0004000000179500", { {"@21000000000b0002", 1}, {"@2101000000180002", 1}, {"@2102000000290002", 1}, {"@21030000002a0002", 1} } },
	{"0004000000179600", { {"@21000000000b0002", 1}, {"@2101000000180002", 1}, {"@2102000000290002", 1}, {"@21030000002a0002", 1} } },
	{"0004000000179700", { {"@21000000000b0002", 1}, {"@2101000000180002", 1}, {"@2102000000290002", 1}, {"@21030000002a0002", 1} } },
	{"0004000000179800", { {"@21000000000b0002", 1}, {"@2101000000180002", 1}, {"@2102000000290002", 1}, {"@21030000002a0002", 1} } },
	{"000400000017A800", { {"@21000000000b0002", 1}, {"@2101000000180002", 1}, {"@2102000000290002", 1}, {"@21030000002a0002", 1} } },
	{"000400000F70CC00", { {"@21000000000b0002", 1}, {"@2101000000180002", 1}, {"@2102000000290002", 1}, {"@21030000002a0002", 1}, {"@2104000002520002", 1}, {"@21050000025a0002", 1}, {"@2105010003630002", 1}, {"@2106000003601202", 1}, {"@2107000003611202", 1}, {"@21080000036f1202", 1}, {"@2108000003880002", 1}, {"@2109000003701202", 1} } },
	{"000400000F70CD00", { {"@21000000000b0002", 1}, {"@2101000000180002", 1}, {"@2102000000290002", 1}, {"@21030000002a0002", 1}, {"@2104000002520002", 1}, {"@21050000025a0002", 1}, {"@2105010003630002", 1}, {"@2106000003601202", 1}, {"@2107000003611202", 1}, {"@21080000036f1202", 1}, {"@2108000003880002", 1}, {"@2109000003701202", 1} } },
	{"000400000F700100", { {"@22400000002b0002", 1} } },
	{"000400000F700200", { {"@22400000002b0002", 1} } },
	{"0004000000174100", { {"@3480000000310002", 1}, {"@3480000002580002", 1}, {"@3480000003791502", 1} } },
	{"000400000016E100", { {"@3500010002e10f02", 1}, {"@3500020002e20f02", 1}, {"@3501000002e30f02", 1}, {"@3502010002e40f02", 1}, {"@3503010002e50f02", 1}, {"@3504010002e60f02", 1} } },
	{"00040000001BC500", { {"@3500010002e10f02", 1}, {"@3500020002e20f02", 1}, {"@3501000002e30f02", 1}, {"@3502010002e40f02", 1}, {"@3503010002e50f02", 1}, {"@3504010002e60f02", 1} } },
	{"00040000001BC600", { {"@3500010002e10f02", 1}, {"@3500020002e20f02", 1}, {"@3501000002e30f02", 1}, {"@3502010002e40f02", 1}, {"@3503010002e50f02", 1}, {"@3504010002e60f02", 1} } },
	{"00040000001A6E00", { {"@35c0000002500a02", 1}, {"@35c0000003920a02", 1} } },
	{"0004000000119A00", { {"@35c0000002500a02", 1}, {"@35c0000003920a02", 1}, {"@35c10000036c0a02", 1}, {"@35c20000036d0a02", 1}, {"@35c30000036e0a02", 1} } },
	{"000400000012CB00", { {"@35c0000002500a02", 1}, {"@35c0000003920a02", 1}, {"@35c10000036c0a02", 1}, {"@35c20000036d0a02", 1}, {"@35c30000036e0a02", 1} } },
	{"000400000017C900", { {"@35c0000002500a02", 1}, {"@35c0000003920a02", 1}, {"@35c10000036c0a02", 1}, {"@35c20000036d0a02", 1}, {"@35c30000036e0a02", 1} } },
	{"000400000017E100", { {"@35c0000002500a02", 1}, {"@35c0000003920a02", 1}, {"@35c10000036c0a02", 1}, {"@35c20000036d0a02", 1}, {"@35c30000036e0a02", 1} } },
	{"0004000000196900", { {"@35c0000002500a02", 1}, {"@35c0000003920a02", 1}, {"@35c10000036c0a02", 1}, {"@35c20000036d0a02", 1}, {"@35c30000036e0a02", 1} } }
};

static std::map<std::string, QAction*> amiibos_actions;
static std::map<std::string, QAction*> amiibos_file_actions;

GMainWindow::GMainWindow(Core::System& system_)
    : ui{std::make_unique<Ui::MainWindow>()}, system{system_}, movie{system.Movie()},
      user_data_migrator{this}, config{std::make_unique<QtConfig>()}, emu_thread{nullptr} {
    Common::Log::Initialize();
    Common::Log::Start();

    Debugger::ToggleConsole();

    QStringList args = QApplication::arguments();
    QString game_path;
    std::optional<bool> fullscreen_override;
    for (int i = 1; i < args.size(); ++i) {
        // Preserves drag/drop functionality
        if (args.size() == 2 && !args[1].startsWith(QChar::fromLatin1('-'))) {
            game_path = args[1];
            break;
        }

        // Dump video
        if (args[i] == QStringLiteral("--dump-video") || args[i] == QStringLiteral("-d")) {
            if (i >= args.size() - 1 || args[i + 1].startsWith(QChar::fromLatin1('-'))) {
                continue;
            }
            if (!DynamicLibrary::FFmpeg::LoadFFmpeg()) {
                ShowFFmpegErrorMessage();
                continue;
            }
            video_dumping_path = args[++i];
            video_dumping_on_start = true;
            continue;
        }

        // Launch game in fullscreen mode
        if (args[i] == QStringLiteral("--fullscreen") || args[i] == QStringLiteral("-f")) {
            fullscreen_override = true;
            continue;
        }

        // Enable GDB stub
        if (args[i] == QStringLiteral("--gdbport") || args[i] == QStringLiteral("-g")) {
            if (i >= args.size() - 1 || args[i + 1].startsWith(QChar::fromLatin1('-'))) {
                continue;
            }
            Settings::values.use_gdbstub = true;
            Settings::values.gdbstub_port = strtoul(args[++i].toLatin1(), NULL, 0);
            continue;
        }

        if (args[i] == QStringLiteral("--help") || args[i] == QStringLiteral("-h")) {
            ShowCommandOutput("Help", fmt::format(Common::help_string, args[0].toStdString()));
            exit(0);
        }

        if (args[i] == QStringLiteral("--install") || args[i] == QStringLiteral("-i")) {
            if (i >= args.size() - 1 || args[i + 1].startsWith(QChar::fromLatin1('-'))) {
                continue;
            }
            Service::AM::InstallStatus result = Service::AM::InstallCIA(args[++i].toStdString());
            if (result != Service::AM::InstallStatus::Success) {
                std::string failure_reason;

                if (result == Service::AM::InstallStatus::ErrorFailedToOpenFile)
                    failure_reason = "Unable to open file.";

                if (result == Service::AM::InstallStatus::ErrorFileNotFound)
                    failure_reason = "File not found.";

                if (result == Service::AM::InstallStatus::ErrorAborted)
                    failure_reason = "Install was aborted.";

                if (result == Service::AM::InstallStatus::ErrorInvalid)
                    failure_reason = "CIA is invalid.";

                if (result == Service::AM::InstallStatus::ErrorEncrypted)
                    failure_reason = "CIA is encrypted.";

                std::string failure_string = "Failed to install CIA: " + failure_reason;
                ShowCommandOutput("Failure", failure_string);
                exit((int)result +
                     2); // 2 is added here to avoid stepping on the toes of
                         // exit codes 1 and 2 which have pre-established conventional meanings
            }
            ShowCommandOutput("Success", "Installed CIA successfully.");
            exit(0);
        }

        if (args[i] == QStringLiteral("--movie-play") || args[i] == QStringLiteral("-p")) {
            if (i >= args.size() - 1 || args[i + 1].startsWith(QChar::fromLatin1('-'))) {
                continue;
            }
            movie_playback_path = args[++i];
            movie_playback_on_start = true;
            continue;
        }

        if (args[i] == QStringLiteral("--movie-record") || args[i] == QStringLiteral("-r")) {
            if (i >= args.size() - 1 || args[i + 1].startsWith(QChar::fromLatin1('-'))) {
                continue;
            }
            movie_record_path = args[++i];
            movie_record_on_start = true;
            continue;
        }

        if (args[i] == QStringLiteral("--movie-record-author") || args[i] == QStringLiteral("-a")) {
            if (i >= args.size() - 1 || args[i + 1].startsWith(QChar::fromLatin1('-'))) {
                continue;
            }
            movie_record_author = args[++i];
            continue;
        }

        if (args[i] == QStringLiteral("--multiplayer") || args[i] == QStringLiteral("-m")) {
            std::cout << "Warning: The --multiplayer option is not yet implemented for the Qt "
                         "frontend; Ignoring."
                      << std::endl;
            if (i < args.size() - 1 && !args[i + 1].startsWith(QChar::fromLatin1('-'))) {
                i++;
            }
            continue;
        }

        if (args[i] == QStringLiteral("--version") || args[i] == QStringLiteral("-v")) {
            const std::string version_string = std::string("Azahar ") + Common::g_build_fullname;
            ShowCommandOutput("Version", version_string);
            exit(0);
        }

        // Launch game in windowed mode
        if (args[i] == QStringLiteral("--windowed") || args[i] == QStringLiteral("-w")) {
            fullscreen_override = false;
            continue;
        }

        // Launch game at path
        if (i == args.size() - 1 && !args[i].startsWith(QChar::fromLatin1('-'))) {
            game_path = args[i];
            continue;
        }
    }

#ifdef __unix__
    SetGamemodeEnabled(Settings::values.enable_gamemode.GetValue());
#endif

    // register types to use in slots and signals
    qRegisterMetaType<std::size_t>("std::size_t");
    qRegisterMetaType<Service::AM::InstallStatus>("Service::AM::InstallStatus");

    // Register CameraFactory
    qt_cameras = std::make_shared<Camera::QtMultimediaCameraHandlerFactory>();
    Camera::RegisterFactory("image", std::make_unique<Camera::StillImageCameraFactory>());
    Camera::RegisterFactory("qt", std::make_unique<Camera::QtMultimediaCameraFactory>(qt_cameras));

    system.RegisterInfoLEDColorChanged([this]() { emit InfoLEDColorChanged(); });

    LoadTranslation();

    Pica::g_debug_context = Pica::DebugContext::Construct();
    setAcceptDrops(true);
    ui->setupUi(this);
    statusBar()->hide();

    default_theme_paths = QIcon::themeSearchPaths();
    UpdateUITheme();

#ifdef USE_DISCORD_PRESENCE
    SetDiscordEnabled(UISettings::values.enable_discord_presence.GetValue());
    discord_rpc->Update(false);
#endif

    play_time_manager = std::make_unique<PlayTime::PlayTimeManager>();

    Network::Init();

    movie.SetPlaybackCompletionCallback([this] {
        QMetaObject::invokeMethod(this, "OnMoviePlaybackCompleted", Qt::BlockingQueuedConnection);
    });

    InitializeWidgets();
    InitializeDebugWidgets();
    InitializeRecentFileMenuActions();
    InitializeSaveStateMenuActions();
    InitializeHotkeys();
    InitializeAmiibos();
	

    SetDefaultUIGeometry();
    RestoreUIState();

    ui->action_Dump_Video->setChecked(video_dumping_on_start);
    if (fullscreen_override) {
        ui->action_Fullscreen->setChecked(*fullscreen_override);
    }
    ui->action_Close_Movie->setEnabled(movie_playback_on_start || movie_record_on_start);

    ConnectAppEvents();
    ConnectMenuEvents();
    ConnectWidgetEvents();

    LOG_INFO(Frontend, "Azahar Version: {} | {}-{}", Common::g_build_fullname, Common::g_scm_branch,
             Common::g_scm_desc);
#if CITRA_ARCH(x86_64)
    const auto& caps = Common::GetCPUCaps();
    std::string cpu_string = caps.cpu_string;
    if (caps.avx || caps.avx2 || caps.avx512) {
        cpu_string += " | AVX";
        if (caps.avx512) {
            cpu_string += "512";
        } else if (caps.avx2) {
            cpu_string += '2';
        }
        if (caps.fma) {
            cpu_string += " | FMA";
        }
    }
    LOG_INFO(Frontend, "Host CPU: {}", cpu_string);
#endif
    LOG_INFO(Frontend, "Host OS: {}", PrettyProductName().toStdString());
    const auto& mem_info = Common::GetMemInfo();
    using namespace Common::Literals;
    LOG_INFO(Frontend, "Host RAM: {:.2f} GiB", mem_info.total_physical_memory / f64{1_GiB});
    LOG_INFO(Frontend, "Host Swap: {:.2f} GiB", mem_info.total_swap_memory / f64{1_GiB});
    UpdateWindowTitle();

    QIcon azahar_icon = QIcon(QString::fromStdString(":/icons/default/256x256/azahar.png"));
    render_window->setWindowIcon(azahar_icon);
    secondary_window->setWindowIcon(azahar_icon);

    show();

#ifdef __APPLE__
    if (AppleUtils::IsRunningFromTerminal()) {
        QMessageBox::warning(
            this, tr("Warning"),
            tr("The `azahar` executable is being run directly rather than via the Azahar.app "
               "bundle.\n\n"
               "When run this way, the app may be missing certain functionality such as camera "
               "emulation.\n\n"
               "It is recommended to instead run Azahar using the `open` command, e.g.:\n"
               "`open ./Azahar.app`"));
    }
#endif

#ifdef ENABLE_QT_UPDATE_CHECKER
    if (UISettings::values.check_for_update_on_start) {
        update_future = QtConcurrent::run([]() -> QString {
            const std::optional<std::string> latest_release_tag =
                UpdateChecker::GetLatestRelease(ShouldCheckForPrereleaseUpdates());

            if (latest_release_tag && latest_release_tag.value() != Common::g_build_fullname) {
                const int latest_major_version = GetMajorVersion(latest_release_tag.value());
                const int current_major_version = GetMajorVersion(Common::g_build_fullname);
                if (current_major_version <= latest_major_version) {
                    return QString::fromStdString(latest_release_tag.value());
                }
            }
            return QString{};
        });
        QObject::connect(&update_watcher, &QFutureWatcher<QString>::finished, this,
                         &GMainWindow::OnEmulatorUpdateAvailable);
        update_watcher.setFuture(update_future);
    }
#endif

    mouse_hide_timer.setInterval(default_mouse_timeout);
    connect(&mouse_hide_timer, &QTimer::timeout, this, &GMainWindow::HideMouseCursor);
    connect(ui->menubar, &QMenuBar::hovered, this, &GMainWindow::OnMouseActivity);

#ifdef ENABLE_OPENGL
    gl_renderer = GetOpenGLRenderer();
#endif

#ifdef ENABLE_VULKAN
    physical_devices = GetVulkanPhysicalDevices();
#endif

    if (!game_path.isEmpty()) {
        BootGame(game_path);
    }
}

GMainWindow::~GMainWindow() {
    // Will get automatically deleted otherwise
    if (!render_window->parent()) {
        delete render_window;
    }

    Pica::g_debug_context.reset();
    Network::Shutdown();
}

void GMainWindow::InitializeWidgets() {
    render_window = new GRenderWindow(this, emu_thread.get(), system, false);
    secondary_window = new GRenderWindow(this, emu_thread.get(), system, true);
    render_window->hide();
    secondary_window->hide();
    secondary_window->setParent(nullptr);

    action_secondary_fullscreen = new QAction(secondary_window);
    action_secondary_toggle_screen = new QAction(secondary_window);
    action_secondary_swap_screen = new QAction(secondary_window);
    action_secondary_rotate_screen = new QAction(secondary_window);

    game_list = new GameList(*play_time_manager, this);
    ui->horizontalLayout->addWidget(game_list);

    game_list_placeholder = new GameListPlaceholder(this);
    ui->horizontalLayout->addWidget(game_list_placeholder);
    game_list_placeholder->setVisible(false);

    loading_screen = new LoadingScreen(this);
    loading_screen->hide();
    ui->horizontalLayout->addWidget(loading_screen);
    connect(loading_screen, &LoadingScreen::Hidden, this, [&] {
        loading_screen->Clear();
        if (emulation_running) {
            render_window->show();
            render_window->setFocus();
            render_window->activateWindow();
        }
    });

    InputCommon::Init();
    multiplayer_state = new MultiplayerState(system, this, game_list->GetModel(),
                                             ui->action_Leave_Room, ui->action_Show_Room);
    multiplayer_state->setVisible(false);

    UpdateBootHomeMenuState();

    // Create status bar
    message_label = new QLabel();
    // Configured separately for left alignment
    message_label->setFrameStyle(QFrame::NoFrame);
    message_label->setContentsMargins(4, 0, 4, 0);
    message_label->setAlignment(Qt::AlignLeft);
    statusBar()->addPermanentWidget(message_label, 1);

    progress_bar = new QProgressBar();
    progress_bar->hide();
    statusBar()->addPermanentWidget(progress_bar);

    loading_shaders_label = new QLabel();

    artic_traffic_label = new QLabel();
    artic_traffic_label->setToolTip(
        tr("Current Artic traffic speed. Higher values indicate bigger transfer loads."));

    emu_speed_label = new QLabel();
    emu_speed_label->setToolTip(tr("Current emulation speed. Values higher or lower than 100% "
                                   "indicate emulation is running faster or slower than a 3DS."));
    game_fps_label = new QLabel();
    game_fps_label->setToolTip(tr("How many frames per second the app is currently displaying. "
                                  "This will vary from app to app and scene to scene."));
    emu_frametime_label = new QLabel();
    emu_frametime_label->setToolTip(
        tr("Time taken to emulate a 3DS frame, not counting framelimiting or v-sync. For "
           "full-speed emulation this should be at most 16.67 ms."));

    for (auto& label : {loading_shaders_label, artic_traffic_label, emu_speed_label, game_fps_label,
                        emu_frametime_label}) {
        label->setVisible(false);
        label->setFrameStyle(QFrame::NoFrame);
        label->setContentsMargins(4, 0, 4, 0);
        statusBar()->addPermanentWidget(label);
    }

    // Setup Graphics API button
    graphics_api_button = new QPushButton();
    graphics_api_button->setObjectName(QStringLiteral("GraphicsAPIStatusBarButton"));
    graphics_api_button->setFocusPolicy(Qt::NoFocus);
    UpdateAPIIndicator();

    connect(graphics_api_button, &QPushButton::clicked, this, [this] { UpdateAPIIndicator(true); });

    statusBar()->insertPermanentWidget(0, graphics_api_button);

    volume_popup = new QWidget(this);
    volume_popup->setWindowFlags(Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint | Qt::Popup);
    volume_popup->setLayout(new QVBoxLayout());
    volume_popup->setMinimumWidth(200);

    volume_slider = new QSlider(Qt::Horizontal);
    volume_slider->setObjectName(QStringLiteral("volume_slider"));
    volume_slider->setMaximum(100);
    volume_slider->setPageStep(5);
    connect(volume_slider, &QSlider::valueChanged, this, [this](int percentage) {
        Settings::values.audio_muted = false;
        const auto value = static_cast<float>(percentage) / volume_slider->maximum();
        Settings::values.volume.SetValue(value);
        UpdateVolumeUI();
    });
    volume_popup->layout()->addWidget(volume_slider);

    volume_button = new QPushButton();
    volume_button->setObjectName(QStringLiteral("TogglableStatusBarButton"));
    volume_button->setFocusPolicy(Qt::NoFocus);
    volume_button->setCheckable(true);
    UpdateVolumeUI();
    connect(volume_button, &QPushButton::clicked, this, [&] {
        UpdateVolumeUI();
        volume_popup->setVisible(!volume_popup->isVisible());
        QRect rect = volume_button->geometry();
        QPoint bottomLeft = statusBar()->mapToGlobal(rect.topLeft());
        bottomLeft.setY(bottomLeft.y() - volume_popup->geometry().height());
        volume_popup->setGeometry(QRect(bottomLeft, QSize(rect.width(), rect.height())));
    });
    statusBar()->insertPermanentWidget(1, volume_button);

    statusBar()->addPermanentWidget(multiplayer_state->GetStatusText());
    statusBar()->addPermanentWidget(multiplayer_state->GetStatusIcon());

    QFrame* sep = new QFrame(this);
    sep->setFrameShape(QFrame::VLine);
    sep->setFrameShadow(QFrame::Sunken);
    sep->setFixedHeight(16);
    statusBar()->addPermanentWidget(sep);

    notification_led = new LedWidget();
    notification_led->setToolTip(tr("Emulated notification LED"));
    statusBar()->addPermanentWidget(notification_led);
    connect(this, &GMainWindow::InfoLEDColorChanged, this, [this] {
        auto led_color = system.GetInfoLEDColor();
        notification_led->setColor(QColor(led_color.r(), led_color.g(), led_color.b()));
    });

    statusBar()->setVisible(true);

    // Removes an ugly inner border from the status bar widgets under Linux
    setStyleSheet(QStringLiteral("QStatusBar::item{border: none;}"));

    QActionGroup* actionGroup_ScreenLayouts = new QActionGroup(this);
    actionGroup_ScreenLayouts->addAction(ui->action_Screen_Layout_Default);
    actionGroup_ScreenLayouts->addAction(ui->action_Screen_Layout_Single_Screen);
    actionGroup_ScreenLayouts->addAction(ui->action_Screen_Layout_Large_Screen);
    actionGroup_ScreenLayouts->addAction(ui->action_Screen_Layout_Side_by_Side);
    actionGroup_ScreenLayouts->addAction(ui->action_Screen_Layout_Separate_Windows);
    actionGroup_ScreenLayouts->addAction(ui->action_Screen_Layout_Hybrid_Screen);
    actionGroup_ScreenLayouts->addAction(ui->action_Screen_Layout_Custom_Layout);

    QActionGroup* actionGroup_SmallPositions = new QActionGroup(this);
    actionGroup_SmallPositions->addAction(ui->action_Small_Screen_TopRight);
    actionGroup_SmallPositions->addAction(ui->action_Small_Screen_MiddleRight);
    actionGroup_SmallPositions->addAction(ui->action_Small_Screen_BottomRight);
    actionGroup_SmallPositions->addAction(ui->action_Small_Screen_TopLeft);
    actionGroup_SmallPositions->addAction(ui->action_Small_Screen_MiddleLeft);
    actionGroup_SmallPositions->addAction(ui->action_Small_Screen_BottomLeft);
    actionGroup_SmallPositions->addAction(ui->action_Small_Screen_Above);
    actionGroup_SmallPositions->addAction(ui->action_Small_Screen_Below);
}

void GMainWindow::InitializeDebugWidgets() {
    connect(ui->action_Create_Pica_Surface_Viewer, &QAction::triggered, this,
            &GMainWindow::OnCreateGraphicsSurfaceViewer);

    QMenu* debug_menu = ui->menu_View_Debugging;

#if MICROPROFILE_ENABLED
    microProfileDialog = new MicroProfileDialog(this);
    microProfileDialog->hide();
    debug_menu->addAction(microProfileDialog->toggleViewAction());
#endif

    registersWidget = new RegistersWidget(system, this);
    addDockWidget(Qt::RightDockWidgetArea, registersWidget);
    registersWidget->hide();
    debug_menu->addAction(registersWidget->toggleViewAction());
    connect(this, &GMainWindow::EmulationStarting, registersWidget,
            &RegistersWidget::OnEmulationStarting);
    connect(this, &GMainWindow::EmulationStopping, registersWidget,
            &RegistersWidget::OnEmulationStopping);

    graphicsWidget = new GPUCommandStreamWidget(system, this);
    addDockWidget(Qt::RightDockWidgetArea, graphicsWidget);
    graphicsWidget->hide();
    debug_menu->addAction(graphicsWidget->toggleViewAction());

    graphicsCommandsWidget = new GPUCommandListWidget(system, this);
    addDockWidget(Qt::RightDockWidgetArea, graphicsCommandsWidget);
    graphicsCommandsWidget->hide();
    debug_menu->addAction(graphicsCommandsWidget->toggleViewAction());

    graphicsBreakpointsWidget = new GraphicsBreakPointsWidget(Pica::g_debug_context, this);
    addDockWidget(Qt::RightDockWidgetArea, graphicsBreakpointsWidget);
    graphicsBreakpointsWidget->hide();
    debug_menu->addAction(graphicsBreakpointsWidget->toggleViewAction());

    graphicsVertexShaderWidget =
        new GraphicsVertexShaderWidget(system, Pica::g_debug_context, this);
    addDockWidget(Qt::RightDockWidgetArea, graphicsVertexShaderWidget);
    graphicsVertexShaderWidget->hide();
    debug_menu->addAction(graphicsVertexShaderWidget->toggleViewAction());

    graphicsTracingWidget = new GraphicsTracingWidget(system, Pica::g_debug_context, this);
    addDockWidget(Qt::RightDockWidgetArea, graphicsTracingWidget);
    graphicsTracingWidget->hide();
    debug_menu->addAction(graphicsTracingWidget->toggleViewAction());
    connect(this, &GMainWindow::EmulationStarting, graphicsTracingWidget,
            &GraphicsTracingWidget::OnEmulationStarting);
    connect(this, &GMainWindow::EmulationStopping, graphicsTracingWidget,
            &GraphicsTracingWidget::OnEmulationStopping);

    waitTreeWidget = new WaitTreeWidget(system, this);
    addDockWidget(Qt::LeftDockWidgetArea, waitTreeWidget);
    waitTreeWidget->hide();
    debug_menu->addAction(waitTreeWidget->toggleViewAction());
    connect(this, &GMainWindow::EmulationStarting, waitTreeWidget,
            &WaitTreeWidget::OnEmulationStarting);
    connect(this, &GMainWindow::EmulationStopping, waitTreeWidget,
            &WaitTreeWidget::OnEmulationStopping);

    lleServiceModulesWidget = new LLEServiceModulesWidget(this);
    addDockWidget(Qt::RightDockWidgetArea, lleServiceModulesWidget);
    lleServiceModulesWidget->hide();
    debug_menu->addAction(lleServiceModulesWidget->toggleViewAction());
    connect(this, &GMainWindow::EmulationStarting,
            [this] { lleServiceModulesWidget->setDisabled(true); });
    connect(this, &GMainWindow::EmulationStopping, waitTreeWidget,
            [this] { lleServiceModulesWidget->setDisabled(false); });

    ipcRecorderWidget = new IPCRecorderWidget(system, this);
    addDockWidget(Qt::RightDockWidgetArea, ipcRecorderWidget);
    ipcRecorderWidget->hide();
    debug_menu->addAction(ipcRecorderWidget->toggleViewAction());
    connect(this, &GMainWindow::EmulationStarting, ipcRecorderWidget,
            &IPCRecorderWidget::OnEmulationStarting);
}

class MyProxyStyle : public QProxyStyle
{
  public:
    int styleHint(StyleHint hint, const QStyleOption *option = nullptr,
                  const QWidget *widget = nullptr, QStyleHintReturn *returnData = nullptr) const override
    {
        if (hint == QStyle::SH_Menu_Scrollable)
            return 1;
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }
};

void GMainWindow::UpdateAmiibos()
{
	std::string pId = Loader::getProgramId();
	int n = 0;
	
	for (const auto& pair : amiibos_actions) {
		bool isVisible = amiibos_usages[pId][pair.first] == 1;
		
		pair.second->setVisible(isVisible);
		if(isVisible) n++;
	}
	
	ui->menu_Amiibo_Recommended->setEnabled(n != 0);
	
	n = 0;
	
	for (const auto& pair : amiibos_file_actions) {
		bool isVisible = amiibos_usages[pId][pair.first] == 1;
		
		pair.second->setVisible(isVisible);
		if(isVisible) n++;
	}
	
	ui->menu_Amiibo_File_Recommended->setEnabled(n != 0);
}

void GMainWindow::InitializeAmiibos()
{
	for (const auto& pair : amiibos) {
		QAction *amiiboAction = new QAction(this);
		
		amiiboAction->setText(QStringLiteral("%1 \t[%2]").arg(QString::fromStdString(pair.second)).arg(QString::fromStdString(amiibos_series[pair.first.substr(13, 2)])));
		amiiboAction->setData(QString::fromStdString(pair.first));
		
        connect(amiiboAction, &QAction::triggered, this, &GMainWindow::OnMenuAmiiboAction);
		
		ui->menu_Amiibo_Full_List->addAction(amiiboAction);
		
		QAction *amiiboAction2 = new QAction(this);
		
		amiiboAction2->setText(amiiboAction->text());
		amiiboAction2->setData(amiiboAction->data());
		
        connect(amiiboAction2, &QAction::triggered, this, &GMainWindow::OnMenuAmiiboAction);
		
		ui->menu_Amiibo_Recommended->addAction(amiiboAction2);
		amiibos_actions[pair.first] = amiiboAction2;
		
		amiiboAction2 = new QAction(this);
		
		amiiboAction2->setText(amiiboAction->text());
		amiiboAction2->setData(amiiboAction->data());
		
        connect(amiiboAction2, &QAction::triggered, this, &GMainWindow::OnMenuAmiiboFileAction);
		
		ui->menu_Amiibo_File_Full_List->addAction(amiiboAction2);
		
		amiiboAction2 = new QAction(this);
		
		amiiboAction2->setText(amiiboAction->text());
		amiiboAction2->setData(amiiboAction->data());
		
        connect(amiiboAction2, &QAction::triggered, this, &GMainWindow::OnMenuAmiiboFileAction);
		
		ui->menu_Amiibo_File_Recommended->addAction(amiiboAction2);
		amiibos_file_actions[pair.first] = amiiboAction2;
    }
	
	ui->menu_Amiibo_Full_List->setStyle(new MyProxyStyle);
	ui->menu_Amiibo_Recommended->setStyle(new MyProxyStyle);
	ui->menu_Amiibo_File_Full_List->setStyle(new MyProxyStyle);
	ui->menu_Amiibo_File_Recommended->setStyle(new MyProxyStyle);
}

void GMainWindow::InitializeRecentFileMenuActions() {
    for (int i = 0; i < max_recent_files_item; ++i) {
        actions_recent_files[i] = new QAction(this);
        actions_recent_files[i]->setVisible(false);
        connect(actions_recent_files[i], &QAction::triggered, this, &GMainWindow::OnMenuRecentFile);

        ui->menu_recent_files->addAction(actions_recent_files[i]);
    }
    ui->menu_recent_files->addSeparator();
    QAction* action_clear_recent_files = new QAction(this);
    action_clear_recent_files->setText(tr("Clear Recent Files"));
    connect(action_clear_recent_files, &QAction::triggered, this, [this] {
        UISettings::values.recent_files.clear();
        UpdateRecentFiles();
    });
    ui->menu_recent_files->addAction(action_clear_recent_files);

    UpdateRecentFiles();
}

void GMainWindow::InitializeSaveStateMenuActions() {
    for (u32 i = 0; i < Core::SaveStateSlotCount; ++i) {
        actions_load_state[i] = new QAction(this);
        actions_load_state[i]->setData(i);
        connect(actions_load_state[i], &QAction::triggered, this, &GMainWindow::OnLoadState);
        if (i > 0)
            ui->menu_Load_State->addAction(actions_load_state[i]);
        actions_save_state[i] = new QAction(this);
        actions_save_state[i]->setData(i);
        connect(actions_save_state[i], &QAction::triggered, this, &GMainWindow::OnSaveState);
        if (i > 0)
            ui->menu_Save_State->addAction(actions_save_state[i]);
    }

    connect(ui->action_Load_from_Newest_Slot, &QAction::triggered, this, [this] {
        UpdateSaveStates();
        if (newest_slot != 0) {
            actions_load_state[newest_slot]->trigger();
        }
    });
    connect(ui->action_Save_to_Oldest_Slot, &QAction::triggered, this, [this] {
        UpdateSaveStates();
        actions_save_state[oldest_slot]->trigger();
    });

    // Quick save / load uses slot
    connect(ui->action_Quick_Save, &QAction::triggered, this, [this] {
        UpdateSaveStates();
        actions_save_state[0]->trigger();
    });
    connect(ui->action_Quick_Load, &QAction::triggered, this, [this] {
        UpdateSaveStates();
        actions_load_state[0]->trigger();
    });

    connect(ui->menu_Load_State->menuAction(), &QAction::hovered, this,
            &GMainWindow::UpdateSaveStates);
    connect(ui->menu_Save_State->menuAction(), &QAction::hovered, this,
            &GMainWindow::UpdateSaveStates);

    UpdateSaveStates();
}

void GMainWindow::InitializeHotkeys() {
    hotkey_registry.LoadHotkeys();

    const QString main_window = QStringLiteral("Main Window");
    const QString fullscreen = QStringLiteral("Fullscreen");

    // QAction Hotkeys
    const auto link_action_shortcut = [&](QAction* action, const QString& action_name,
                                          const bool primary_only = false,
                                          const bool auto_repeat = false) {
        static const QString main_window = QStringLiteral("Main Window");
        action->setShortcut(hotkey_registry.GetKeySequence(main_window, action_name));
        action->setAutoRepeat(auto_repeat);
        this->addAction(action);
        if (!primary_only)
            secondary_window->addAction(action);
    };

    link_action_shortcut(ui->action_Load_File, QStringLiteral("Load File"));
    link_action_shortcut(ui->action_Load_Amiibo, QStringLiteral("Load Amiibo"));
    link_action_shortcut(ui->action_Remove_Amiibo, QStringLiteral("Remove Amiibo"));
    link_action_shortcut(ui->action_Exit, QStringLiteral("Exit Azahar"));
    link_action_shortcut(ui->action_Restart, QStringLiteral("Restart Emulation"));
    link_action_shortcut(ui->action_Pause, QStringLiteral("Continue/Pause Emulation"));
    link_action_shortcut(ui->action_Stop, QStringLiteral("Stop Emulation"));
    link_action_shortcut(ui->action_Show_Filter_Bar, QStringLiteral("Toggle Filter Bar"));
    link_action_shortcut(ui->action_Show_Status_Bar, QStringLiteral("Toggle Status Bar"));
    link_action_shortcut(ui->action_Fullscreen, fullscreen, true);
    link_action_shortcut(ui->action_Capture_Screenshot, QStringLiteral("Capture Screenshot"));
    link_action_shortcut(ui->action_Debug_Pause, QStringLiteral("Debug Pause"));
    link_action_shortcut(ui->action_Debug_Resume, QStringLiteral("Debug Resume"));
    link_action_shortcut(ui->action_Debug_Step, QStringLiteral("Debug Step"), false, true);
    link_action_shortcut(ui->action_Debug_Unschedule_All, QStringLiteral("Debug Unschedule All"));
    link_action_shortcut(ui->action_Debug_Schedule_All, QStringLiteral("Debug Schedule All"));
    link_action_shortcut(ui->action_Screen_Layout_Swap_Screens, QStringLiteral("Swap Screens"));
    link_action_shortcut(ui->action_Screen_Layout_Upright_Screens,
                         QStringLiteral("Rotate Screens Upright"));
    link_action_shortcut(ui->action_Advance_Frame, QStringLiteral("Advance Frame"));
    link_action_shortcut(ui->action_Load_from_Newest_Slot,
                         QStringLiteral("Load from Newest Non-Quicksave Slot"));
    link_action_shortcut(ui->action_Save_to_Oldest_Slot,
                         QStringLiteral("Save to Oldest Non-Quicksave Slot"));
    link_action_shortcut(ui->action_Quick_Save, QStringLiteral("Quick Save"));
    link_action_shortcut(ui->action_Quick_Load, QStringLiteral("Quick Load"));
    link_action_shortcut(ui->action_View_Lobby, QStringLiteral("Multiplayer Browse Public Rooms"));
    link_action_shortcut(ui->action_Start_Room, QStringLiteral("Multiplayer Create Room"));
    link_action_shortcut(ui->action_Connect_To_Room,
                         QStringLiteral("Multiplayer Direct Connect to Room"));
    link_action_shortcut(ui->action_Show_Room, QStringLiteral("Multiplayer Show Current Room"));
    link_action_shortcut(ui->action_Leave_Room, QStringLiteral("Multiplayer Leave Room"));

    // QShortcut Hotkeys
    const auto connect_shortcut = [&](const QString& action_name, const auto& function) {
        const auto* hotkey = hotkey_registry.GetHotkey(main_window, action_name, this);
        const auto* secondary_hotkey =
            hotkey_registry.GetHotkey(main_window, action_name, secondary_window);
        connect(hotkey, &QShortcut::activated, this, function);
        connect(secondary_hotkey, &QShortcut::activated, this, function);
    };

    connect_shortcut(QStringLiteral("Toggle Screen Layout"), &GMainWindow::ToggleScreenLayout);
    connect_shortcut(QStringLiteral("Exit Fullscreen"), [&] {
        if (emulation_running) {
            ui->action_Fullscreen->setChecked(false);
            ToggleFullscreen();
        }
    });
    connect_shortcut(QStringLiteral("Toggle Per-Application Speed"), [&] {
        if (!hotkey_registry
                 .GetKeySequence(QStringLiteral("Main Window"), QStringLiteral("Toggle Turbo Mode"))
                 .isEmpty()) {
            return;
        }
        Settings::values.frame_limit.SetGlobal(!Settings::values.frame_limit.UsingGlobal());
        UpdateStatusBar();
    });
    connect_shortcut(QStringLiteral("Toggle Texture Dumping"),
                     [&] { Settings::values.dump_textures = !Settings::values.dump_textures; });
    connect_shortcut(QStringLiteral("Toggle Custom Textures"),
                     [&] { Settings::values.custom_textures = !Settings::values.custom_textures; });

    connect_shortcut(QStringLiteral("Toggle Turbo Mode"),
                     [&] { GMainWindow::SetTurboEnabled(!GMainWindow::IsTurboEnabled()); });

    connect_shortcut(QStringLiteral("Increase Speed Limit"), [&] { AdjustSpeedLimit(true); });
    connect_shortcut(QStringLiteral("Decrease Speed Limit"), [&] { AdjustSpeedLimit(false); });

    connect_shortcut(QStringLiteral("Audio Mute/Unmute"), &GMainWindow::OnMute);
    connect_shortcut(QStringLiteral("Audio Volume Down"), &GMainWindow::OnDecreaseVolume);
    connect_shortcut(QStringLiteral("Audio Volume Up"), &GMainWindow::OnIncreaseVolume);

    // We use "static" here in order to avoid capturing by lambda due to a MSVC bug, which makes the
    // variable hold a garbage value after this function exits
    static constexpr u16 FACTOR_3D_STEP = 5;
    connect_shortcut(QStringLiteral("Decrease 3D Factor"), [this] {
        const auto factor_3d = Settings::values.factor_3d.GetValue();
        if (factor_3d > 0) {
            if (factor_3d % FACTOR_3D_STEP != 0) {
                Settings::values.factor_3d = factor_3d - (factor_3d % FACTOR_3D_STEP);
            } else {
                Settings::values.factor_3d = factor_3d - FACTOR_3D_STEP;
            }
            UpdateStatusBar();
        }
    });
    connect_shortcut(QStringLiteral("Increase 3D Factor"), [this] {
        const auto factor_3d = Settings::values.factor_3d.GetValue();
        if (factor_3d < 255) {
            if (factor_3d % FACTOR_3D_STEP != 0) {
                Settings::values.factor_3d =
                    factor_3d + FACTOR_3D_STEP - (factor_3d % FACTOR_3D_STEP);
            } else {
                Settings::values.factor_3d = factor_3d + FACTOR_3D_STEP;
            }
            UpdateStatusBar();
        }
    });

    // Secondary Window QAction Hotkeys
    const auto add_secondary_window_hotkey = [this](QAction* action, QKeySequence hotkey,
                                                    const char* slot) {
        // This action will fire specifically when secondary_window is in focus
        action->setShortcut(hotkey);
        disconnect(action, SIGNAL(triggered()), this, slot);
        connect(action, SIGNAL(triggered()), this, slot);
        secondary_window->addAction(action);
    };

    // Use the same fullscreen hotkey as the main window
    const auto fullscreen_hotkey = hotkey_registry.GetKeySequence(main_window, fullscreen);
    add_secondary_window_hotkey(action_secondary_fullscreen, fullscreen_hotkey,
                                SLOT(ToggleSecondaryFullscreen()));
}

void GMainWindow::SetDefaultUIGeometry() {
    // geometry: 55% of the window contents are in the upper screen half, 45% in the lower half
    const QRect screenRect = screen()->geometry();

    const int w = screenRect.width() * 2 / 3;
    const int h = screenRect.height() / 2;
    const int x = (screenRect.x() + screenRect.width()) / 2 - w / 2;
    const int y = (screenRect.y() + screenRect.height()) / 2 - h * 55 / 100;

    setGeometry(x, y, w, h);
}

void GMainWindow::RestoreUIState() {
    restoreGeometry(UISettings::values.geometry);
    restoreState(UISettings::values.state);
    render_window->restoreGeometry(UISettings::values.renderwindow_geometry);
#if MICROPROFILE_ENABLED
    microProfileDialog->restoreGeometry(UISettings::values.microprofile_geometry);
    microProfileDialog->setVisible(UISettings::values.microprofile_visible.GetValue());
#endif

    game_list->LoadInterfaceLayout();

    ui->action_Single_Window_Mode->setChecked(UISettings::values.single_window_mode.GetValue());
    ToggleWindowMode();

    ui->action_Fullscreen->setChecked(UISettings::values.fullscreen.GetValue());
    SyncMenuUISettings();

    ui->action_Display_Dock_Widget_Headers->setChecked(
        UISettings::values.display_titlebar.GetValue());
    OnDisplayTitleBars(ui->action_Display_Dock_Widget_Headers->isChecked());

    ui->action_Show_Filter_Bar->setChecked(UISettings::values.show_filter_bar.GetValue());
    game_list->SetFilterVisible(ui->action_Show_Filter_Bar->isChecked());

    ui->action_Show_Status_Bar->setChecked(UISettings::values.show_status_bar.GetValue());
    statusBar()->setVisible(ui->action_Show_Status_Bar->isChecked());
}

void GMainWindow::OnAppFocusStateChanged(Qt::ApplicationState state) {
    if (state != Qt::ApplicationHidden && state != Qt::ApplicationInactive &&
        state != Qt::ApplicationActive) {
        LOG_DEBUG(Frontend, "ApplicationState unusual flag: {} ", state);
    }
    if (!emulation_running) {
        return;
    }
    if (UISettings::values.pause_when_in_background) {
        if (emu_thread->IsRunning() &&
            (state & (Qt::ApplicationHidden | Qt::ApplicationInactive))) {
            auto_paused = true;
            OnPauseGame();
        } else if (!emu_thread->IsRunning() && auto_paused && state == Qt::ApplicationActive) {
            auto_paused = false;
            OnResumeGame(false);
        }
    }
    if (UISettings::values.mute_when_in_background) {
        if (!Settings::values.audio_muted &&
            (state & (Qt::ApplicationHidden | Qt::ApplicationInactive))) {
            Settings::values.audio_muted = true;
            auto_muted = true;
        } else if (auto_muted && state == Qt::ApplicationActive) {
            Settings::values.audio_muted = false;
            auto_muted = false;
        }
        UpdateVolumeUI();
    }
}

bool GApplicationEventFilter::eventFilter(QObject* object, QEvent* event) {
    if (event->type() == QEvent::FileOpen) {
        emit FileOpen(static_cast<QFileOpenEvent*>(event));
        return true;
    }
    return false;
}

void GMainWindow::ConnectAppEvents() {
    const auto filter = new GApplicationEventFilter();
    QGuiApplication::instance()->installEventFilter(filter);

    connect(filter, &GApplicationEventFilter::FileOpen, this, &GMainWindow::OnFileOpen);
}

void GMainWindow::ConnectWidgetEvents() {
    connect(game_list, &GameList::GameChosen, this, &GMainWindow::OnGameListLoadFile);
    connect(game_list, &GameList::OpenDirectory, this, &GMainWindow::OnGameListOpenDirectory);
    connect(game_list, &GameList::OpenFolderRequested, this, &GMainWindow::OnGameListOpenFolder);
    connect(game_list, &GameList::RemovePlayTimeRequested, this,
            &GMainWindow::OnGameListRemovePlayTimeData);
    connect(game_list, &GameList::CreateShortcut, this, &GMainWindow::OnGameListCreateShortcut);
    connect(game_list, &GameList::DumpRomFSRequested, this, &GMainWindow::OnGameListDumpRomFS);
    connect(game_list, &GameList::AddDirectory, this, &GMainWindow::OnGameListAddDirectory);
    connect(game_list_placeholder, &GameListPlaceholder::AddDirectory, this,
            &GMainWindow::OnGameListAddDirectory);
    connect(game_list, &GameList::ShowList, this, &GMainWindow::OnGameListShowList);
    connect(game_list, &GameList::PopulatingCompleted, this,
            [this] { multiplayer_state->UpdateGameList(game_list->GetModel()); });
#ifdef ENABLE_DEVELOPER_OPTIONS
    connect(game_list, &GameList::StartingLaunchStressTest, this,
            &GMainWindow::StartLaunchStressTest);
#endif

    connect(game_list, &GameList::OpenPerGameGeneralRequested, this,
            &GMainWindow::OnGameListOpenPerGameProperties);

    connect(this, &GMainWindow::EmulationStarting, render_window,
            &GRenderWindow::OnEmulationStarting);
    connect(this, &GMainWindow::EmulationStopping, render_window,
            &GRenderWindow::OnEmulationStopping);
    connect(this, &GMainWindow::EmulationStarting, secondary_window,
            &GRenderWindow::OnEmulationStarting);
    connect(this, &GMainWindow::EmulationStopping, secondary_window,
            &GRenderWindow::OnEmulationStopping);

    connect(&status_bar_update_timer, &QTimer::timeout, this, &GMainWindow::UpdateStatusBar);

    connect(this, &GMainWindow::UpdateProgress, this, &GMainWindow::OnUpdateProgress);
    connect(this, &GMainWindow::CIAInstallReport, this, &GMainWindow::OnCIAInstallReport);
    connect(this, &GMainWindow::CIAInstallFinished, this, &GMainWindow::OnCIAInstallFinished);
    connect(this, &GMainWindow::CompressFinished, this, &GMainWindow::OnCompressFinished);
    connect(this, &GMainWindow::UpdateThemedIcons, multiplayer_state,
            &MultiplayerState::UpdateThemedIcons);
}

void GMainWindow::ConnectMenuEvents() {
    const auto connect_menu = [&](QAction* action, const auto& event_fn,
                                  QAction::MenuRole role = QAction::NoRole) {
        action->setMenuRole(role);
        connect(action, &QAction::triggered, this, event_fn);
        // Add actions to this window so that hiding menus in fullscreen won't disable them
        addAction(action);
        // Add actions to the render window so that they work outside of single window mode
        render_window->addAction(action);
    };

    // File
    connect_menu(ui->action_Load_File, &GMainWindow::OnMenuLoadFile);
    connect_menu(ui->action_Install_CIA, &GMainWindow::OnMenuInstallCIA);
    connect_menu(ui->action_Connect_Artic, &GMainWindow::OnMenuConnectArticBase);
    connect_menu(ui->action_Remove_Azahar_Encryption, &GMainWindow::OnMenuRemoveAzaharEncryption);
    connect_menu(ui->action_Revert_Encryption_Removal, &GMainWindow::OnMenuRevertEncryptionRemoval);
    connect_menu(ui->action_Setup_System_Files, &GMainWindow::OnMenuSetUpSystemFiles);
    for (u32 region = 0; region < Core::NUM_SYSTEM_TITLE_REGIONS; region++) {
        connect_menu(ui->menu_Download_System_Files->actions().at(region),
                     [this, region] { OnDownloadSystemFilesMenu(region); });
    }
    for (u32 region = 0; region < Core::NUM_SYSTEM_TITLE_REGIONS; region++) {
        connect_menu(ui->menu_Boot_Home_Menu->actions().at(region),
                     [this, region] { OnMenuBootHomeMenu(region); });
    }
    connect_menu(ui->action_Exit, &QMainWindow::close, QAction::QuitRole);
    connect_menu(ui->action_Export_ZipPass, &GMainWindow::OnExportZipPass);
    connect_menu(ui->action_Import_ZipPass, &GMainWindow::OnImportZipPass);
    connect_menu(ui->action_Clear_StreetPass_Config, &GMainWindow::OnClearStreetPassConfig);
    connect_menu(ui->action_Previous_Amiibo, &GMainWindow::OnPreviousAmiibo);
    connect_menu(ui->action_Load_Amiibo, &GMainWindow::OnLoadAmiibo);
    connect_menu(ui->action_Remove_Amiibo, &GMainWindow::OnRemoveAmiibo);

    // Emulation
    connect_menu(ui->action_Pause, &GMainWindow::OnPauseContinueGame);
    connect_menu(ui->action_Stop, &GMainWindow::OnStopGame);
    connect_menu(ui->action_Restart, [this] { BootGame(QString(game_path)); });
    connect_menu(ui->action_Report_Compatibility, []() {
        QDesktopServices::openUrl(QUrl(QStringLiteral(
            "https://github.com/azahar-emu/compatibility-list/blob/master/CONTRIBUTING.md")));
    });
    connect_menu(ui->action_Configure, &GMainWindow::OnConfigure, QAction::PreferencesRole);
    connect_menu(ui->action_Configure_Current_Game, &GMainWindow::OnConfigurePerGame);

    // View
    connect_menu(ui->action_Single_Window_Mode, &GMainWindow::ToggleWindowMode);
    connect_menu(ui->action_Display_Dock_Widget_Headers, &GMainWindow::OnDisplayTitleBars);
    connect_menu(ui->action_Show_Filter_Bar, &GMainWindow::OnToggleFilterBar);
    connect(ui->action_Show_Status_Bar, &QAction::triggered, statusBar(), &QStatusBar::setVisible);

    // Multiplayer
    connect(ui->action_View_Lobby, &QAction::triggered, multiplayer_state,
            &MultiplayerState::OnViewLobby);
    connect(ui->action_Start_Room, &QAction::triggered, multiplayer_state,
            &MultiplayerState::OnCreateRoom);
    connect(ui->action_Leave_Room, &QAction::triggered, multiplayer_state,
            &MultiplayerState::OnCloseRoom);
    connect(ui->action_Connect_To_Room, &QAction::triggered, multiplayer_state,
            &MultiplayerState::OnDirectConnectToRoom);
    connect(ui->action_Show_Room, &QAction::triggered, multiplayer_state,
            &MultiplayerState::OnOpenNetworkRoom);

    connect_menu(ui->action_Fullscreen, &GMainWindow::ToggleFullscreen);
    connect_menu(ui->action_Screen_Layout_Default, &GMainWindow::ChangeScreenLayout);
    connect_menu(ui->action_Screen_Layout_Single_Screen, &GMainWindow::ChangeScreenLayout);
    connect_menu(ui->action_Screen_Layout_Large_Screen, &GMainWindow::ChangeScreenLayout);
    connect_menu(ui->action_Screen_Layout_Hybrid_Screen, &GMainWindow::ChangeScreenLayout);
    connect_menu(ui->action_Screen_Layout_Side_by_Side, &GMainWindow::ChangeScreenLayout);
    connect_menu(ui->action_Screen_Layout_Separate_Windows, &GMainWindow::ChangeScreenLayout);
    connect_menu(ui->action_Screen_Layout_Custom_Layout, &GMainWindow::ChangeScreenLayout);
    connect_menu(ui->action_Screen_Layout_Swap_Screens, &GMainWindow::OnSwapScreens);
    connect_menu(ui->action_Screen_Layout_Upright_Screens, &GMainWindow::OnRotateScreens);
    connect_menu(ui->action_Small_Screen_TopRight, &GMainWindow::ChangeSmallScreenPosition);
    connect_menu(ui->action_Small_Screen_MiddleRight, &GMainWindow::ChangeSmallScreenPosition);
    connect_menu(ui->action_Small_Screen_BottomRight, &GMainWindow::ChangeSmallScreenPosition);
    connect_menu(ui->action_Small_Screen_TopLeft, &GMainWindow::ChangeSmallScreenPosition);
    connect_menu(ui->action_Small_Screen_MiddleLeft, &GMainWindow::ChangeSmallScreenPosition);
    connect_menu(ui->action_Small_Screen_BottomLeft, &GMainWindow::ChangeSmallScreenPosition);
    connect_menu(ui->action_Small_Screen_Above, &GMainWindow::ChangeSmallScreenPosition);
    connect_menu(ui->action_Small_Screen_Below, &GMainWindow::ChangeSmallScreenPosition);

    // Movie
    connect_menu(ui->action_Record_Movie, &GMainWindow::OnRecordMovie);
    connect_menu(ui->action_Play_Movie, &GMainWindow::OnPlayMovie);
    connect_menu(ui->action_Close_Movie, &GMainWindow::OnCloseMovie);
    connect_menu(ui->action_Save_Movie, &GMainWindow::OnSaveMovie);
    connect_menu(ui->action_Movie_Read_Only_Mode,
                 [this](bool checked) { movie.SetReadOnly(checked); });
    connect_menu(ui->action_Advance_Frame, [this] {
        if (emulation_running && system.frame_limiter.IsFrameAdvancing()) {
            system.frame_limiter.AdvanceFrame();
        }
    });
    connect_menu(ui->action_Capture_Screenshot, &GMainWindow::OnCaptureScreenshot);
    connect_menu(ui->action_Dump_Video, &GMainWindow::OnDumpVideo);

    // Tools debug
    connect_menu(ui->action_Debug_Pause, [this] {
        if (emu_thread) {
            emu_thread->SetRunning(false);
        }
    });
    connect_menu(ui->action_Debug_Resume, [this] {
        if (emu_thread) {
            emu_thread->SetRunning(true);
        }
    });
    connect_menu(ui->action_Debug_Step, [this] {
        if (emu_thread) {
            emu_thread->ExecStep();
        }
    });
    connect_menu(ui->action_Debug_Unschedule_All,
                 [this] { system.DebugUnscheduleAllThreadsFromFrontend(true); });
    connect_menu(ui->action_Debug_Schedule_All,
                 [this] { system.DebugUnscheduleAllThreadsFromFrontend(false); });

    // Tools
    connect_menu(ui->action_Compress_ROM_File, &GMainWindow::OnCompressFile);
    connect_menu(ui->action_Decompress_ROM_File, &GMainWindow::OnDecompressFile);

    // Help
    connect_menu(ui->action_Open_Citra_Folder, &GMainWindow::OnOpenCitraFolder);
    connect_menu(ui->action_Open_Log_Folder, []() {
        QString path = QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::LogDir));
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
    connect_menu(ui->action_FAQ, []() {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://azahar-emu.org/pages/faq/")));
    });
    connect_menu(ui->action_libzip, &GMainWindow::OnMenuLibzipLicence);
    connect_menu(ui->action_About, &GMainWindow::OnMenuAboutCitra, QAction::AboutRole);
}

void GMainWindow::UpdateMenuState() {
    const bool is_paused =
        !emu_thread || !emu_thread->IsRunning() || system.frame_limiter.IsFrameAdvancing();

    const std::array running_actions{
        ui->action_Stop,
        ui->action_Restart,
        ui->action_Configure_Current_Game,
        ui->action_Report_Compatibility,
        ui->action_Load_Amiibo,
        ui->action_Remove_Amiibo,
        ui->action_Pause,
        ui->action_Advance_Frame,
    };

    for (QAction* action : running_actions) {
        action->setEnabled(emulation_running);
    }
	
	ui->menu_Amiibo_File_Recommended->setEnabled(emulation_running);
	ui->menu_Amiibo_Recommended->setEnabled(emulation_running);
	ui->menu_Amiibo_Full_List->setEnabled(emulation_running);
	UpdateAmiibos();
	
	ui->action_Export_ZipPass->setEnabled(!emulation_running);
	ui->action_Import_ZipPass->setEnabled(!emulation_running);
	ui->action_Clear_StreetPass_Config->setEnabled(!emulation_running);

    ui->action_Capture_Screenshot->setEnabled(emulation_running);
    ui->action_Advance_Frame->setEnabled(emulation_running && is_paused);

    if (emulation_running && is_paused) {
        ui->action_Pause->setText(tr("&Continue"));
    } else {
        ui->action_Pause->setText(tr("&Pause"));
    }
}

void GMainWindow::OnDisplayTitleBars(bool show) {
    QList<QDockWidget*> widgets = findChildren<QDockWidget*>();

    if (show) {
        for (QDockWidget* widget : widgets) {
            QWidget* old = widget->titleBarWidget();
            widget->setTitleBarWidget(nullptr);
            if (old) {
                delete old;
            }
        }
    } else {
        for (QDockWidget* widget : widgets) {
            QWidget* old = widget->titleBarWidget();
            widget->setTitleBarWidget(new QWidget());
            if (old) {
                delete old;
            }
        }
    }
}

#if defined(HAVE_SDL2) && defined(__unix__) && !defined(__APPLE__)
static std::optional<QDBusObjectPath> HoldWakeLockLinux(u32 window_id = 0) {
    if (!QDBusConnection::sessionBus().isConnected()) {
        return {};
    }
    // reference: https://flatpak.github.io/xdg-desktop-portal/#gdbus-org.freedesktop.portal.Inhibit
    QDBusInterface xdp(QStringLiteral("org.freedesktop.portal.Desktop"),
                       QStringLiteral("/org/freedesktop/portal/desktop"),
                       QStringLiteral("org.freedesktop.portal.Inhibit"));
    if (!xdp.isValid()) {
        LOG_WARNING(Frontend, "Couldn't connect to XDP D-Bus endpoint");
        return {};
    }
    QVariantMap options = {};
    //: TRANSLATORS: This string is shown to the user to explain why Citra needs to prevent the
    //: computer from sleeping
    options.insert(QString::fromLatin1("reason"),
                   QCoreApplication::translate("GMainWindow", "Azahar is running an application"));
    // 0x4: Suspend lock; 0x8: Idle lock
    QDBusReply<QDBusObjectPath> reply =
        xdp.call(QString::fromLatin1("Inhibit"),
                 QString::fromLatin1("x11:") + QString::number(window_id, 16), 12U, options);

    if (reply.isValid()) {
        return reply.value();
    }
    LOG_WARNING(Frontend, "Couldn't read Inhibit reply from XDP: {}",
                reply.error().message().toStdString());
    return {};
}

static void ReleaseWakeLockLinux(const QDBusObjectPath& lock) {
    if (!QDBusConnection::sessionBus().isConnected()) {
        return;
    }
    QDBusInterface unlocker(QString::fromLatin1("org.freedesktop.portal.Desktop"), lock.path(),
                            QString::fromLatin1("org.freedesktop.portal.Request"));
    unlocker.call(QString::fromLatin1("Close"));
}
#endif // __unix__

void GMainWindow::PreventOSSleep() {
#ifdef _WIN32
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
#elif defined(HAVE_SDL2)
    SDL_DisableScreenSaver();
#if defined(__unix__) && !defined(__APPLE__)
    auto reply = HoldWakeLockLinux(winId());
    if (reply) {
        wake_lock = std::move(reply.value());
    }
#endif // defined(__unix__) && !defined(__APPLE__)
#endif // _WIN32
}

void GMainWindow::AllowOSSleep() {
#ifdef _WIN32
    SetThreadExecutionState(ES_CONTINUOUS);
#elif defined(HAVE_SDL2)
    SDL_EnableScreenSaver();
#if defined(__unix__) && !defined(__APPLE__)
    if (!wake_lock.path().isEmpty()) {
        ReleaseWakeLockLinux(wake_lock);
    }
#endif // defined(__unix__) && !defined(__APPLE__)
#endif // _WIN32
}

bool GMainWindow::LoadROM(const QString& filename) {
    // Shutdown previous session if the emu thread is still active...
    if (emu_thread) {
        ShutdownGame();
    }

    if (!render_window->InitRenderTarget() || !secondary_window->InitRenderTarget()) {
        LOG_CRITICAL(Frontend, "Failed to initialize render targets!");
        return false;
    }

    const auto scope = render_window->Acquire();

    if (!UISettings::values.inserted_cartridge.GetValue().empty()) {
        system.InsertCartridge(UISettings::values.inserted_cartridge.GetValue());
    }

    const Core::System::ResultStatus result{
        system.Load(*render_window, filename.toStdString(), secondary_window)};

    if (result != Core::System::ResultStatus::Success) {
        QString invalid_format = tr("Invalid application format");
        QString invalid_format_description =
            tr("The application file format not supported.<br>Please make sure you are using one "
               "of the compatible file formats:<ul><li>Cartridge images: "
               "<b>.cci/.zcci/.3ds</b></li><li>Installable archives: "
               "<b>.cia/.zcia</b></li><li>Homebrew titles: <b>.3dsx/.z3dsx</b></li><li>NCCH "
               "containers: <b>.cxi/.zcxi/.app</b></li><li>ELF files: <b>.elf/.axf</b></li></ul>");

        switch (result) {
        case Core::System::ResultStatus::ErrorGetLoader:
            LOG_CRITICAL(Frontend, "Failed to obtain loader for {}", filename.toStdString());
            QMessageBox::critical(this, invalid_format, invalid_format_description);
            break;

        case Core::System::ResultStatus::ErrorSystemMode:
            LOG_CRITICAL(Frontend, "Failed to load application!");
            QMessageBox::critical(this, invalid_format, invalid_format_description);
            break;

        case Core::System::ResultStatus::ErrorLoader_ErrorEncrypted: {
            QMessageBox::critical(
                this, tr("ROM Encrypted"),
                tr("Your ROM is encrypted. <br/>Please follow the guides to redump your "
                   "<a "
                   "href='https://web.archive.org/web/20240304210021/https://citra-emu.org/wiki/"
                   "dumping-game-cartridges/'>game "
                   "cartridges</a> or "
                   "<a "
                   "href='https://web.archive.org/web/20240304210011/https://citra-emu.org/wiki/"
                   "dumping-installed-titles/'>installed "
                   "titles</a>."));
            break;
        }
        case Core::System::ResultStatus::ErrorLoader_ErrorInvalidFormat:
            QMessageBox::critical(this, invalid_format, invalid_format_description);
            break;

        case Core::System::ResultStatus::ErrorLoader_ErrorGbaTitle:
            QMessageBox::critical(this, tr("Unsupported application"),
                                  tr("GBA Virtual Console is not supported by Azahar."));
            break;

        case Core::System::ResultStatus::ErrorArticDisconnected:
            QMessageBox::critical(
                this, tr("Artic Server"),
                tr(fmt::format(
                       "An error has occurred whilst communicating with the Artic Server.\n{}",
                       system.GetStatusDetails())
                       .c_str()));
            break;
        case Core::System::ResultStatus::ErrorN3DSApplication:
            QMessageBox::critical(this, tr("Invalid system mode"),
                                  tr("New 3DS exclusive applications cannot be loaded without "
                                     "enabling the New 3DS mode."));
            break;
        case Core::System::ResultStatus::ErrorLoader:
            QMessageBox::critical(this, tr("Generic load error"),
                                  tr("An generic load error occurred while loading the "
                                     "application.<br/>Please check the log for more details."));
            break;
        case Core::System::ResultStatus::ErrorLoader_ErrorPatches:
            QMessageBox::critical(this, tr("Error applying patches"),
                                  tr("A generic error occurred while applying a patch to the "
                                     "application.<br/>Please check the log for more details."));
            break;
        case Core::System::ResultStatus::ErrorLoader_ErrorPatchesInvalidTitle:
            QMessageBox::critical(
                this, tr("Error applying patches"),
                tr("Failed to apply a patch because it is designed for a different "
                   "application.<br/>Please make sure you are using the patches for "
                   "the right application, region and version."));
            break;
        default:
            QMessageBox::critical(
                this, tr("Error while loading application"),
                tr("An unknown error occurred.<br/>Please see the log for more details."));
            break;
        }
        return false;
    }

    std::string title;
    system.GetAppLoader().ReadTitle(title);
    game_title = QString::fromStdString(title);
    UpdateWindowTitle();

    u64 title_id;
    system.GetAppLoader().ReadProgramId(title_id);

    game_path = filename;
    game_title_id = title_id;

    return true;
}

void GMainWindow::BootGame(const QString& filename) {
    if (emu_thread) {
        ShutdownGame();
    }

    const bool is_artic = filename.startsWith(QString::fromStdString("articbase:/")) ||
                          filename.startsWith(QString::fromStdString("articinio:/")) ||
                          filename.startsWith(QString::fromStdString("articinin:/"));

    if (!is_artic && filename.endsWith(QStringLiteral(".cia"))) {
        const auto answer = QMessageBox::question(
            this, tr("CIA must be installed before usage"),
            tr("Before using this CIA, you must install it. Do you want to install it now?"),
            QMessageBox::Yes | QMessageBox::No);

        if (answer == QMessageBox::Yes)
            InstallCIA(QStringList(filename));

        return;
    }

    show_artic_label = is_artic;

    LOG_INFO(Frontend, "Azahar starting...");
    if (!is_artic) {
        StoreRecentFile(filename); // Put the filename on top of the list
    }

    if (movie_record_on_start) {
        movie.PrepareForRecording();
    }
    if (movie_playback_on_start) {
        movie.PrepareForPlayback(movie_playback_path.toStdString());
    }

    const std::string path = filename.toStdString();
    auto loader = Loader::GetLoader(path);

    u64 title_id{0};
    Loader::ResultStatus res =
        loader ? loader->ReadProgramId(title_id) : Loader::ResultStatus::Error;

    if (Loader::ResultStatus::Success == res) {
        // Load per game settings
        const std::string name{is_artic ? "" : FileUtil::GetFilename(filename.toStdString())};
        const std::string config_file_name =
            title_id == 0 ? name : fmt::format("{:016X}", title_id);
        LOG_INFO(Frontend, "Loading per application config file for title {}", config_file_name);
        QtConfig per_game_config(config_file_name, QtConfig::ConfigType::PerGameConfig);
    }

    // Artic Server cannot accept a client multiple times, so multiple loaders are not
    // possible. Instead register the app loader early and do not create it again on system load.
    if (loader && !loader->SupportsMultipleInstancesForSameFile()) {
        system.RegisterAppLoaderEarly(loader);
    }

    system.ApplySettings();

    Settings::LogSettings();

    // Save configurations
    UpdateUISettings();
    game_list->SaveInterfaceLayout();
    config->Save();

    if (!LoadROM(filename)) {
        render_window->ReleaseRenderTarget();
        secondary_window->ReleaseRenderTarget();
        return;
    }

    // Set everything up
    if (movie_record_on_start) {
        movie.StartRecording(movie_record_path.toStdString(), movie_record_author.toStdString());
        movie_record_on_start = false;
        movie_record_path.clear();
        movie_record_author.clear();
    }
    if (movie_playback_on_start) {
        movie.StartPlayback(movie_playback_path.toStdString());
        movie_playback_on_start = false;
        movie_playback_path.clear();
    }

    ui->action_Advance_Frame->setEnabled(false);

    if (video_dumping_on_start) {
        StartVideoDumping(video_dumping_path);
        video_dumping_on_start = false;
        video_dumping_path.clear();
    }

    // Register debug widgets
    if (graphicsWidget->isVisible()) {
        graphicsWidget->Register();
    }

    // Create and start the emulation thread
    emu_thread = std::make_unique<EmuThread>(system, *render_window);
    emit EmulationStarting(emu_thread.get());
    emu_thread->start();

    connect(render_window, &GRenderWindow::Closed, this, &GMainWindow::OnStopGame);
    connect(render_window, &GRenderWindow::MouseActivity, this, &GMainWindow::OnMouseActivity);
    connect(secondary_window, &GRenderWindow::Closed, this, &GMainWindow::OnStopGame);
    connect(secondary_window, &GRenderWindow::MouseActivity, this, &GMainWindow::OnMouseActivity);

    // BlockingQueuedConnection is important here, it makes sure we've finished refreshing our views
    // before the CPU continues
    connect(emu_thread.get(), &EmuThread::DebugModeEntered, registersWidget,
            &RegistersWidget::OnDebugModeEntered, Qt::BlockingQueuedConnection);
    connect(emu_thread.get(), &EmuThread::DebugModeEntered, waitTreeWidget,
            &WaitTreeWidget::OnDebugModeEntered, Qt::BlockingQueuedConnection);
    connect(emu_thread.get(), &EmuThread::DebugModeLeft, registersWidget,
            &RegistersWidget::OnDebugModeLeft, Qt::BlockingQueuedConnection);
    connect(emu_thread.get(), &EmuThread::DebugModeLeft, waitTreeWidget,
            &WaitTreeWidget::OnDebugModeLeft, Qt::BlockingQueuedConnection);

    connect(emu_thread.get(), &EmuThread::LoadProgress, loading_screen,
            &LoadingScreen::OnLoadProgress, Qt::QueuedConnection);
    connect(emu_thread.get(), &EmuThread::SwitchDiskResources, this,
            &GMainWindow::OnSwitchDiskResources, Qt::QueuedConnection);
    connect(emu_thread.get(), &EmuThread::HideLoadingScreen, loading_screen,
            &LoadingScreen::OnLoadComplete);

    // Update the GUI
    registersWidget->OnDebugModeEntered();
    if (ui->action_Single_Window_Mode->isChecked()) {
        game_list->hide();
        game_list_placeholder->hide();
    }
    status_bar_update_timer.start(1000);

    if (UISettings::values.hide_mouse) {
        mouse_hide_timer.start();
        setMouseTracking(true);
    }

    loading_screen->Prepare(system.GetAppLoader());
    loading_screen->show();

    emulation_running = true;
    if (ui->action_Fullscreen->isChecked()) {
        ShowFullscreen();
    }

    OnResumeGame(true);
}

void GMainWindow::ShutdownGame() {
    if (!emulation_running) {
        return;
    }
	
	Loader::resetProgramId();

    if (ui->action_Fullscreen->isChecked()) {
        HideFullscreen();
    }

    auto video_dumper = system.GetVideoDumper();
    if (video_dumper && video_dumper->IsDumping()) {
        game_shutdown_delayed = true;
        OnStopVideoDumping();
        return;
    }

    AllowOSSleep();

#ifdef USE_DISCORD_PRESENCE
    discord_rpc->Pause();
#endif

    emu_thread->RequestStop();

    // Release emu threads from any breakpoints
    // This belongs after RequestStop() and before wait() because if emulation stops on a GPU
    // breakpoint after (or before) RequestStop() is called, the emulation would never be able
    // to continue out to the main loop and terminate. Thus wait() would hang forever.
    // TODO(bunnei): This function is not thread safe, but it's being used as if it were
    Pica::g_debug_context->ClearBreakpoints();

    // Unregister debug widgets
    if (graphicsWidget->isVisible()) {
        graphicsWidget->Unregister();
    }

    // Frame advancing must be cancelled in order to release the emu thread from waiting
    system.frame_limiter.SetFrameAdvancing(false);

    emit EmulationStopping();

    // Wait for emulation thread to complete and delete it
    emu_thread->wait();
    emu_thread = nullptr;

    system.EjectCartridge();

    OnCloseMovie();

#ifdef USE_DISCORD_PRESENCE
    discord_rpc->Update(false);
#endif
#ifdef __unix__
    Common::Linux::StopGamemode();
#endif

    // The emulation is stopped, so closing the window or not does not matter anymore
    disconnect(render_window, &GRenderWindow::Closed, this, &GMainWindow::OnStopGame);
    disconnect(secondary_window, &GRenderWindow::Closed, this, &GMainWindow::OnStopGame);

    render_window->hide();
    secondary_window->hide();
    loading_screen->hide();
    loading_screen->Clear();

    if (game_list->IsEmpty()) {
        game_list_placeholder->show();
    } else {
        game_list->show();
    }
    game_list->SetFilterFocus();

    setMouseTracking(false);

    // Disable status bar updates
    status_bar_update_timer.stop();
    message_label_used_for_movie = false;
    show_artic_label = false;
    loading_shaders_label->setVisible(false);
    artic_traffic_label->setVisible(false);
    emu_speed_label->setVisible(false);
    game_fps_label->setVisible(false);
    emu_frametime_label->setVisible(false);
    notification_led->setColor(QColor(0, 0, 0));

    UpdateSaveStates();

    emulation_running = false;

    game_title.clear();
    UpdateWindowTitle();

    game_path.clear();
    game_title_id = 0;

    // Update the GUI
    UpdateMenuState();

    // When closing the game, destroy the GLWindow to clear the context after the game is closed
    render_window->ReleaseRenderTarget();
    secondary_window->ReleaseRenderTarget();
}

#ifdef ENABLE_DEVELOPER_OPTIONS
void GMainWindow::StartLaunchStressTest(const QString& game_path) {
    QThreadPool::globalInstance()->start([this, game_path] {
        do {
            ui->action_Stop->trigger();
            emit game_list->GameChosen(game_path);
            QThread::sleep(2);
        } while (emulation_running);
    });
}
#endif

void GMainWindow::StoreRecentFile(const QString& filename) {
    UISettings::values.recent_files.prepend(filename);
    UISettings::values.recent_files.removeDuplicates();
    while (UISettings::values.recent_files.size() > max_recent_files_item) {
        UISettings::values.recent_files.removeLast();
    }

    UpdateRecentFiles();
}

void GMainWindow::UpdateRecentFiles() {
    const int num_recent_files =
        std::min(static_cast<int>(UISettings::values.recent_files.size()), max_recent_files_item);

    for (int i = 0; i < num_recent_files; i++) {
        const QString text = QStringLiteral("&%1. %2").arg(i + 1).arg(
            QFileInfo(UISettings::values.recent_files[i]).fileName());
        actions_recent_files[i]->setText(text);
        actions_recent_files[i]->setData(UISettings::values.recent_files[i]);
        actions_recent_files[i]->setToolTip(UISettings::values.recent_files[i]);
        actions_recent_files[i]->setVisible(true);
    }

    for (int j = num_recent_files; j < max_recent_files_item; ++j) {
        actions_recent_files[j]->setVisible(false);
    }

    // Enable the recent files menu if the list isn't empty
    ui->menu_recent_files->setEnabled(num_recent_files != 0);
}

void GMainWindow::UpdateSaveStates() {
    if (!system.IsPoweredOn()) {
        ui->menu_Load_State->setEnabled(false);
        ui->menu_Save_State->setEnabled(false);
        return;
    }

    ui->menu_Load_State->setEnabled(true);
    ui->menu_Save_State->setEnabled(true);
    ui->action_Load_from_Newest_Slot->setEnabled(false);

    oldest_slot = newest_slot = 1;
    oldest_slot_time = std::numeric_limits<u64>::max();
    newest_slot_time = 0;

    u64 title_id;
    if (system.GetAppLoader().ReadProgramId(title_id) != Loader::ResultStatus::Success) {
        return;
    }
    auto savestates = Core::ListSaveStates(title_id, movie.GetCurrentMovieID());
    for (u32 i = 0; i < Core::SaveStateSlotCount; ++i) {
        actions_load_state[i]->setEnabled(false);
        if (i == 0) {
            actions_load_state[i]->setText(tr("Quick Load"));
            actions_save_state[i]->setText(tr("Quick Save"));
        } else {
            actions_load_state[i]->setText(tr("Slot %1").arg(i));
            actions_save_state[i]->setText(tr("Slot %1").arg(i));
        }
    }
    for (const auto& savestate : savestates) {
        if (savestate.slot >= Core::SaveStateSlotCount) {
            continue;
        }
        const bool display_name =
            savestate.status == Core::SaveStateInfo::ValidationStatus::RevisionDismatch &&
            !savestate.build_name.empty();
        actions_load_state[savestate.slot]->setEnabled(true);
        if (savestate.slot == 0) {
            const auto text = tr("%2 %3")
                                  .arg(QDateTime::fromSecsSinceEpoch(savestate.time)
                                           .toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")))
                                  .arg(display_name ? QString::fromStdString(savestate.build_name)
                                                    : QLatin1String())
                                  .trimmed();
            ui->action_Quick_Save->setText(tr("Quick Save - %1").arg(text).trimmed());
            ui->action_Quick_Load->setText(tr("Quick Load - %1").arg(text).trimmed());
            continue;
        }
        const auto text =
            tr("Slot %1 - %2 %3")
                .arg(savestate.slot)
                .arg(QDateTime::fromSecsSinceEpoch(savestate.time)
                         .toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")))
                .arg(display_name ? QString::fromStdString(savestate.build_name) : QLatin1String())
                .trimmed();

        actions_load_state[savestate.slot]->setText(text);
        actions_save_state[savestate.slot]->setText(text);

        ui->action_Load_from_Newest_Slot->setEnabled(true);
        if (savestate.time > newest_slot_time) {
            newest_slot = savestate.slot;
            newest_slot_time = savestate.time;
        }
        if (savestate.time < oldest_slot_time) {
            oldest_slot = savestate.slot;
            oldest_slot_time = savestate.time;
        }
    }
    // Value as 1 because quicksave slot is not used for this calculation
    for (u32 i = 1; i < Core::SaveStateSlotCount; ++i) {
        if (!actions_load_state[i]->isEnabled()) {
            // Prefer empty slot
            oldest_slot = i;
            oldest_slot_time = 0;
            break;
        }
    }
}

void GMainWindow::OnGameListLoadFile(QString game_path) {
    if (ConfirmChangeGame()) {
        BootGame(game_path);
    }
}

void GMainWindow::OnGameListOpenFolder(u64 data_id, GameListOpenTarget target) {
    std::string path;
    std::string open_target;

    switch (target) {
    case GameListOpenTarget::SAVE_DATA: {
        open_target = "Save Data";
        std::string sdmc_dir = FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir);
        path = FileSys::ArchiveSource_SDSaveData::GetSaveDataPathFor(sdmc_dir, data_id);
        break;
    }
    case GameListOpenTarget::EXT_DATA: {
        open_target = "Extra Data";
        std::string sdmc_dir = FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir);
        path = FileSys::GetExtDataPathFromId(sdmc_dir, data_id);
        break;
    }
    case GameListOpenTarget::APPLICATION: {
        open_target = "Application";
        auto media_type = Service::AM::GetTitleMediaType(data_id);
        path = Service::AM::GetTitlePath(media_type, data_id) + "content/";
        break;
    }
    case GameListOpenTarget::UPDATE_DATA: {
        open_target = "Update Data";
        path = Service::AM::GetTitlePath(Service::FS::MediaType::SDMC, data_id + 0xe00000000) +
               "content/";
        break;
    }
    case GameListOpenTarget::TEXTURE_DUMP: {
        open_target = "Dumped Textures";
        path = fmt::format("{}textures/{:016X}/",
                           FileUtil::GetUserPath(FileUtil::UserPath::DumpDir), data_id);
        break;
    }
    case GameListOpenTarget::TEXTURE_LOAD: {
        open_target = "Custom Textures";
        path = fmt::format("{}textures/{:016X}/",
                           FileUtil::GetUserPath(FileUtil::UserPath::LoadDir), data_id);
        break;
    }
    case GameListOpenTarget::MODS: {
        open_target = "Mods";
        path = fmt::format("{}mods/{:016X}/", FileUtil::GetUserPath(FileUtil::UserPath::LoadDir),
                           data_id);
        break;
    }
    case GameListOpenTarget::DLC_DATA: {
        open_target = "DLC Data";
        path = fmt::format("{}Nintendo 3DS/00000000000000000000000000000000/"
                           "00000000000000000000000000000000/title/0004008c/{:08x}/content/",
                           FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir), data_id);
        break;
    }
    case GameListOpenTarget::SHADER_CACHE: {
        open_target = "Shader Cache";
        path = FileUtil::GetUserPath(FileUtil::UserPath::ShaderDir);
        break;
    }
    default:
        LOG_ERROR(Frontend, "Unexpected target {}", static_cast<int>(target));
        return;
    }

    QString qpath = QString::fromStdString(path);

    QDir dir(qpath);
    if (!dir.exists()) {
        QMessageBox::critical(
            this, tr("Error Opening %1 Folder").arg(QString::fromStdString(open_target)),
            tr("Folder does not exist!"));
        return;
    }

    LOG_INFO(Frontend, "Opening {} path for data_id={:016x}", open_target, data_id);

    QDesktopServices::openUrl(QUrl::fromLocalFile(qpath));
}

void GMainWindow::OnGameListRemovePlayTimeData(u64 program_id) {
    if (QMessageBox::question(this, tr("Remove Play Time Data"), tr("Reset play time?"),
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    play_time_manager->ResetProgramPlayTime(program_id);
    game_list->PopulateAsync(UISettings::values.game_dirs);
}

bool GMainWindow::CreateShortcutLink(const std::filesystem::path& shortcut_path,
                                     const std::string& comment,
                                     const std::filesystem::path& icon_path,
                                     const std::string& command, const std::string& arguments,
                                     const std::string& categories, const std::string& keywords,
                                     const std::string& name, const bool& skip_tryexec) try {
#if defined(__linux__) || defined(__FreeBSD__) // Linux and FreeBSD
    std::filesystem::path shortcut_path_full = shortcut_path / (name + ".desktop");
    std::ofstream shortcut_stream(shortcut_path_full, std::ios::binary | std::ios::trunc);
    if (!shortcut_stream.is_open()) {
        LOG_ERROR(Frontend, "Failed to create shortcut");
        return false;
    }
    // TODO: Migrate fmt::print to std::print in futures STD C++ 23.
    fmt::print(shortcut_stream, "[Desktop Entry]\n");
    fmt::print(shortcut_stream, "Type=Application\n");
    fmt::print(shortcut_stream, "Version=1.0\n");
    fmt::print(shortcut_stream, "Name={}\n", name);
    if (!comment.empty()) {
        fmt::print(shortcut_stream, "Comment={}\n", comment);
    }
    if (std::filesystem::is_regular_file(icon_path)) {
        fmt::print(shortcut_stream, "Icon={}\n", icon_path.string());
    }
    if (!skip_tryexec) {
        fmt::print(shortcut_stream, "TryExec={}\n", command);
    }
    fmt::print(shortcut_stream, "Exec={} {}\n", command, arguments);
    if (!categories.empty()) {
        fmt::print(shortcut_stream, "Categories={}\n", categories);
    }
    if (!keywords.empty()) {
        fmt::print(shortcut_stream, "Keywords={}\n", keywords);
    }
    return true;
#elif defined(_WIN32) // Windows
    HRESULT hr = CoInitialize(nullptr);
    if (FAILED(hr)) {
        LOG_ERROR(Frontend, "CoInitialize failed");
        return false;
    }
    SCOPE_EXIT({ CoUninitialize(); });
    IShellLinkW* ps1 = nullptr;
    IPersistFile* persist_file = nullptr;
    SCOPE_EXIT({
        if (persist_file != nullptr) {
            persist_file->Release();
        }
        if (ps1 != nullptr) {
            ps1->Release();
        }
    });
    HRESULT hres = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                                    reinterpret_cast<void**>(&ps1));
    if (FAILED(hres)) {
        LOG_ERROR(Frontend, "Failed to create IShellLinkW instance");
        return false;
    }
    hres = ps1->SetPath(Common::UTF8ToUTF16W(command).data());
    if (FAILED(hres)) {
        LOG_ERROR(Frontend, "Failed to set path");
        return false;
    }
    if (!arguments.empty()) {
        hres = ps1->SetArguments(Common::UTF8ToUTF16W(arguments).data());
        if (FAILED(hres)) {
            LOG_ERROR(Frontend, "Failed to set arguments");
            return false;
        }
    }
    if (!comment.empty()) {
        hres = ps1->SetDescription(Common::UTF8ToUTF16W(comment).data());
        if (FAILED(hres)) {
            LOG_ERROR(Frontend, "Failed to set description");
            return false;
        }
    }
    if (std::filesystem::is_regular_file(icon_path)) {
        hres = ps1->SetIconLocation(icon_path.c_str(), 0);
        if (FAILED(hres)) {
            LOG_ERROR(Frontend, "Failed to set icon location");
            return false;
        }
    }
    hres = ps1->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&persist_file));
    if (FAILED(hres)) {
        LOG_ERROR(Frontend, "Failed to get IPersistFile interface");
        return false;
    }
    hres = persist_file->Save(
        std::filesystem::path{shortcut_path / (Common::UTF8ToUTF16W(name) + L".lnk")}.c_str(),
        TRUE);
    if (FAILED(hres)) {
        LOG_ERROR(Frontend, "Failed to save shortcut");
        return false;
    }
    return true;
#else                 // Unsupported platform
    return false;
#endif
} catch (const std::exception& e) {
    LOG_ERROR(Frontend, "Failed to create shortcut: {}", e.what());
    return false;
}

// Messages in pre-defined message boxes for less code spaghetti
bool GMainWindow::CreateShortcutMessagesGUI(QWidget* parent, int message,
                                            const QString& game_title) {
    int result = 0;
    QMessageBox::StandardButtons buttons;
    switch (message) {
    case GMainWindow::CREATE_SHORTCUT_MSGBOX_FULLSCREEN_PROMPT:
        buttons = QMessageBox::Yes | QMessageBox::No;
        result = QMessageBox::information(
            parent, tr("Create Shortcut"),
            tr("Do you want to launch the application in fullscreen?"), buttons);
        return result == QMessageBox::Yes;
    case GMainWindow::CREATE_SHORTCUT_MSGBOX_SUCCESS:
        QMessageBox::information(parent, tr("Create Shortcut"),
                                 tr("Successfully created a shortcut to %1").arg(game_title));
        return false;
    case GMainWindow::CREATE_SHORTCUT_MSGBOX_APPIMAGE_VOLATILE_WARNING:
        buttons = QMessageBox::StandardButton::Ok | QMessageBox::StandardButton::Cancel;
        result =
            QMessageBox::warning(this, tr("Create Shortcut"),
                                 tr("This will create a shortcut to the current AppImage. This may "
                                    "not work well if you update. Continue?"),
                                 buttons);
        return result == QMessageBox::Ok;
    default:
        buttons = QMessageBox::Ok;
        QMessageBox::critical(parent, tr("Create Shortcut"),
                              tr("Failed to create a shortcut to %1").arg(game_title), buttons);
        return false;
    }
}

bool GMainWindow::MakeShortcutIcoPath(const u64 program_id, const std::string_view game_file_name,
                                      std::filesystem::path& out_icon_path) {
    // Get path to Citra icons directory & icon extension
    std::string ico_extension = "png";
#if defined(_WIN32)
    out_icon_path = FileUtil::GetUserPath(FileUtil::UserPath::IconsDir);
    ico_extension = "ico";
#elif defined(__linux__) || defined(__FreeBSD__)
    out_icon_path = FileUtil::GetUserDirectory("XDG_DATA_HOME") + "/icons/hicolor/256x256/";
#endif
    // Create icons directory if it doesn't exist
    if (!FileUtil::CreateFullPath(out_icon_path.string())) {
        QMessageBox::critical(
            this, tr("Create Icon"),
            tr("Cannot create icon file. Path \"%1\" does not exist and cannot be created.")
                .arg(QString::fromStdString(out_icon_path.string())),
            QMessageBox::StandardButton::Ok);
        out_icon_path.clear();
        return false;
    }

    // Create icon file path
    out_icon_path /= (program_id == 0 ? fmt::format("citra-{}.{}", game_file_name, ico_extension)
                                      : fmt::format("citra-{:016X}.{}", program_id, ico_extension));
    return true;
}

void GMainWindow::OnGameListCreateShortcut(u64 program_id, const std::string& game_path,
                                           GameListShortcutTarget target) {
    std::string citra_command{};
    bool skip_tryexec = false;
    const char* env_flatpak_id = getenv("FLATPAK_ID");
    if (env_flatpak_id) {
        citra_command = fmt::format("flatpak run {}", env_flatpak_id);
        skip_tryexec = true;
    } else {
        // Get path to Citra executable
        const QStringList args = QApplication::arguments();
        citra_command = args[0].toStdString();
        // If relative path, make it an absolute path
        if (citra_command.c_str()[0] == '.') {
            citra_command = FileUtil::GetCurrentDir().value_or("") + DIR_SEP + citra_command;
        }
    }

    // Shortcut path
    std::filesystem::path shortcut_path{};
    if (target == GameListShortcutTarget::Desktop) {
        shortcut_path =
            QStandardPaths::writableLocation(QStandardPaths::DesktopLocation).toStdString();
    } else if (target == GameListShortcutTarget::Applications) {
        shortcut_path = GetApplicationsDirectory();
    }

    // Icon path and title
    if (!std::filesystem::exists(shortcut_path)) {
        CreateShortcutMessagesGUI(this, CREATE_SHORTCUT_MSGBOX_ERROR, {});
        LOG_ERROR(Frontend, "Invalid shortcut target");
        return;
    }

    // Get title from game file
    const auto loader = Loader::GetLoader(game_path);
    std::string game_title = fmt::format("{:016X}", program_id);
    if (loader->ReadTitle(game_title) != Loader::ResultStatus::Success) {
        game_title = fmt::format("{:016x}", program_id);
    }

    // Delete illegal characters from title
    const std::string illegal_chars = "<>:\"/\\|?*.";
    for (auto it = game_title.rbegin(); it != game_title.rend(); ++it) {
        if (illegal_chars.find(*it) != std::string::npos) {
            game_title.erase(it.base() - 1);
        }
    }

    // Get icon from game file
    std::vector<u8> icon_image_file;
    if (loader->ReadIcon(icon_image_file) != Loader::ResultStatus::Success) {
        LOG_WARNING(Frontend, "Could not read icon from {:s}", game_path);
    }

    const QPixmap pixmap = GetQPixmapFromSMDH(icon_image_file);
    const QImage icon_data = pixmap.toImage();
    std::filesystem::path out_icon_path;
    if (MakeShortcutIcoPath(program_id, game_title, out_icon_path)) {
        if (!SaveIconToFile(out_icon_path, icon_data)) {
            LOG_ERROR(Frontend, "Could not write icon to file");
        }
    }

    const auto qt_game_title = QString::fromStdString(game_title);
#if defined(__linux__)
    // Special case for AppImages
    // Warn once if we are making a shortcut to a volatile AppImage
    const std::string appimage_ending =
        std::string(Common::g_scm_rev).substr(0, 9).append(".AppImage");
    if (citra_command.ends_with(appimage_ending) && !UISettings::values.shortcut_already_warned) {
        if (CreateShortcutMessagesGUI(this, CREATE_SHORTCUT_MSGBOX_APPIMAGE_VOLATILE_WARNING,
                                      qt_game_title)) {
            return;
        }
        UISettings::values.shortcut_already_warned = true;
    }
#endif // __linux__
    // Create shortcut
    std::string arguments = fmt::format("\"{:s}\"", game_path);
    if (CreateShortcutMessagesGUI(this, CREATE_SHORTCUT_MSGBOX_FULLSCREEN_PROMPT, qt_game_title)) {
        arguments = "-f " + arguments;
    }
    const std::string comment = fmt::format("Start {:s} with the Azahar Emulator", game_title);
    const std::string categories = "Game;Emulator;Qt;";
    const std::string keywords = "3ds;Nintendo;";

    if (CreateShortcutLink(shortcut_path, comment, out_icon_path, citra_command, arguments,
                           categories, keywords, game_title, skip_tryexec)) {
        CreateShortcutMessagesGUI(this, CREATE_SHORTCUT_MSGBOX_SUCCESS, qt_game_title);
        return;
    }
    CreateShortcutMessagesGUI(this, CREATE_SHORTCUT_MSGBOX_ERROR, qt_game_title);
}

void GMainWindow::OnGameListDumpRomFS(QString game_path, u64 program_id) {
    auto* dialog = new QProgressDialog(tr("Dumping..."), tr("Cancel"), 0, 0, this);
    dialog->setWindowModality(Qt::WindowModal);
    dialog->setWindowFlags(dialog->windowFlags() &
                           ~(Qt::WindowCloseButtonHint | Qt::WindowContextHelpButtonHint));
    dialog->setCancelButton(nullptr);
    dialog->setMinimumDuration(0);
    dialog->setValue(0);

    const auto base_path = fmt::format(
        "{}romfs/{:016X}", FileUtil::GetUserPath(FileUtil::UserPath::DumpDir), program_id);
    const auto update_path =
        fmt::format("{}romfs/{:016X}", FileUtil::GetUserPath(FileUtil::UserPath::DumpDir),
                    program_id | 0x0004000e00000000);
    using FutureWatcher = QFutureWatcher<std::pair<Loader::ResultStatus, Loader::ResultStatus>>;
    auto* future_watcher = new FutureWatcher(this);
    connect(future_watcher, &FutureWatcher::finished, this,
            [this, dialog, base_path, update_path, future_watcher] {
                dialog->hide();
                const auto& [base, update] = future_watcher->result();
                if (base != Loader::ResultStatus::Success) {
                    QMessageBox::critical(
                        this, tr("Azahar"),
                        tr("Could not dump base RomFS.\nRefer to the log for details."));
                    return;
                }
                QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(base_path)));
                if (update == Loader::ResultStatus::Success) {
                    QDesktopServices::openUrl(
                        QUrl::fromLocalFile(QString::fromStdString(update_path)));
                }
            });

    auto future = QtConcurrent::run([game_path, base_path, update_path] {
        std::unique_ptr<Loader::AppLoader> loader = Loader::GetLoader(game_path.toStdString());
        return std::make_pair(loader->DumpRomFS(base_path), loader->DumpUpdateRomFS(update_path));
    });
    future_watcher->setFuture(future);
}

void GMainWindow::OnGameListOpenDirectory(const QString& directory) {
    QString path;
    if (directory == QStringLiteral("INSTALLED")) {
        path = QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir) +
                                      "Nintendo "
                                      "3DS/00000000000000000000000000000000/"
                                      "00000000000000000000000000000000/title/00040000");
    } else if (directory == QStringLiteral("SYSTEM")) {
        path = QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::NANDDir) +
                                      "00000000000000000000000000000000/title/00040010");
    } else {
        path = directory;
    }
    if (!QFileInfo::exists(path)) {
        QMessageBox::critical(this, tr("Error Opening %1").arg(path), tr("Folder does not exist!"));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void GMainWindow::OnGameListAddDirectory() {
    const QString dir_path = QFileDialog::getExistingDirectory(this, tr("Select Directory"));
    if (dir_path.isEmpty())
        return;
    UISettings::GameDir game_dir{dir_path, false, true};
    if (!UISettings::values.game_dirs.contains(game_dir)) {
        UISettings::values.game_dirs.append(game_dir);
        game_list->PopulateAsync(UISettings::values.game_dirs);
    } else {
        LOG_WARNING(Frontend, "Selected directory is already in the application list");
    }
}

void GMainWindow::OnGameListShowList(bool show) {
    if (emulation_running && ui->action_Single_Window_Mode->isChecked())
        return;
    game_list->setVisible(show);
    game_list_placeholder->setVisible(!show);
};

void GMainWindow::OnGameListOpenPerGameProperties(const QString& file) {
    const auto loader = Loader::GetLoader(file.toStdString());

    u64 title_id{};
    if (!loader || loader->ReadProgramId(title_id) != Loader::ResultStatus::Success) {
        QMessageBox::information(this, tr("Properties"),
                                 tr("The application properties could not be loaded."));
        return;
    }

    OpenPerGameConfiguration(title_id, file);
}

void GMainWindow::OnMenuLoadFile() {
    const QString extensions = QStringLiteral("*.").append(
        GameList::supported_file_extensions.join(QStringLiteral(" *.")));
    const QString file_filter = tr("3DS Executable (%1);;All Files (*.*)",
                                   "%1 is an identifier for the 3DS executable file extensions.")
                                    .arg(extensions);
    const QString filename = QFileDialog::getOpenFileName(
        this, tr("Load File"), UISettings::values.roms_path, file_filter);

    if (filename.isEmpty()) {
        return;
    }

    UISettings::values.roms_path = QFileInfo(filename).path();
    BootGame(filename);
}

void GMainWindow::OnMenuSetUpSystemFiles() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Set Up System Files"));

    QVBoxLayout layout(&dialog);

    QLabel label_description(
        tr("<p>Azahar needs console unique data and firmware files from a real console to be "
           "able to use some of its features.<br>Such files and data can be set up with the <a "
           "href=https://github.com/azahar-emu/ArticSetupTool>Azahar "
           "Artic Setup Tool</a><br>Notes:<ul><li><b>This operation will install console unique "
           "data to Azahar, do not share your user or nand folders<br>after performing the setup "
           "process!</b></li><li>While doing the setup process, Azahar will link to the console "
           "running the setup tool. You can unlink the<br>console later from the System tab in the "
           "emulator configuration menu.</li><li>Do not go online with both Azahar and your 3DS "
           "console at the same time after setting up system files,<br>as it could cause "
           "issues.</li><li>Old 3DS setup is needed for the New 3DS setup to work (doing both "
           "setup modes is recommended).</li><li>Both setup modes will work regardless of the "
           "model of the console running the setup tool.</li></ul><hr></p>"),
        &dialog);
    label_description.setOpenExternalLinks(true);
    layout.addWidget(&label_description);

    QHBoxLayout layout_h(&dialog);
    layout.addLayout(&layout_h);

    QLabel label_enter(tr("Enter Azahar Artic Setup Tool address:"), &dialog);

    layout_h.addWidget(&label_enter);

    QLineEdit textInput(UISettings::values.last_artic_base_addr, &dialog);
    layout_h.addWidget(&textInput);

    QLabel label_select(tr("<br>Choose setup mode:"), &dialog);
    layout.addWidget(&label_select);

    std::pair<bool, bool> install_state = Core::AreSystemTitlesInstalled();

    QRadioButton radio1(&dialog);
    QRadioButton radio2(&dialog);
    if (!install_state.first) {
        radio1.setChecked(true);

        radio1.setText(tr("(\u2139\uFE0F) Old 3DS setup"));
        radio1.setToolTip(tr("Setup is possible."));

        radio2.setText(tr("(\u26A0) New 3DS setup"));
        radio2.setToolTip(tr("Old 3DS setup is required first."));
        radio2.setEnabled(false);
    } else {
        radio1.setText(tr("(\u2705) Old 3DS setup"));
        radio1.setToolTip(tr("Setup completed."));

        if (!install_state.second) {
            radio2.setChecked(true);

            radio2.setText(tr("(\u2139\uFE0F) New 3DS setup"));
            radio2.setToolTip(tr("Setup is possible."));
        } else {
            radio1.setChecked(true);

            radio2.setText(tr("(\u2705) New 3DS setup"));
            radio2.setToolTip(tr("Setup completed."));
        }
    }
    layout.addWidget(&radio1);
    layout.addWidget(&radio2);

    QDialogButtonBox buttonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(&buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout.addWidget(&buttonBox);

    int res = dialog.exec();
    if (res == QDialog::Accepted) {
        bool is_o3ds = radio1.isChecked();
        if ((is_o3ds && install_state.first) || (!is_o3ds && install_state.second)) {
            QMessageBox::StandardButton answer =
                QMessageBox::question(this, tr("Set Up System Files"),
                                      tr("The system files for the selected mode are already set "
                                         "up.\nReinstall the files anyway?"),
                                      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (answer != QMessageBox::Yes) {
                return;
            }
        }
        Core::UninstallSystemFiles(is_o3ds ? Core::SystemTitleSet::Old3ds
                                           : Core::SystemTitleSet::New3ds);
        QString addr = textInput.text();
        UISettings::values.last_artic_base_addr = addr;
        BootGame(QString::fromStdString(is_o3ds ? "articinio://" : "articinin://").append(addr));
    }
}

void GMainWindow::OnMenuInstallCIA() {
    QStringList filepaths = QFileDialog::getOpenFileNames(
        this, tr("Load Files"), UISettings::values.roms_path,
        tr("3DS Installation File (*.cia *.zcia)") + QStringLiteral(";;") + tr("All Files (*.*)"));

    if (filepaths.isEmpty()) {
        return;
    }

    UISettings::values.roms_path = QFileInfo(filepaths[0]).path();
    InstallCIA(filepaths);
}

void GMainWindow::OnMenuConnectArticBase() {
    bool ok = false;
    auto res = QInputDialog::getText(this, tr("Connect to Artic Base"),
                                     tr("Enter Artic Base server address:"), QLineEdit::Normal,
                                     UISettings::values.last_artic_base_addr, &ok);
    if (ok) {
        UISettings::values.last_artic_base_addr = res;
        BootGame(QString::fromStdString("articbase://").append(res));
    }
}

void GMainWindow::OnMenuRevertEncryptionRemoval() {
	game_list->SetDirectoryWatcherEnabled(false);
	int res = HW::UniqueData::RevertEncryptionRemoval();
	
	if(res == 0)
		QMessageBox::information(this, tr("AzaharPlus"), tr("Nothing to revert"));
	else
		QMessageBox::information(this, tr("AzaharPlus"), tr("%1 file(s) successfully reverted").arg(res));
	
	game_list->SetDirectoryWatcherEnabled(true);
}

void GMainWindow::OnMenuRemoveAzaharEncryption() {
    const std::vector<std::string> paths = HW::UniqueData::GetAppFilepaths();
	
	if(paths.size() == 0)
	{
		QMessageBox::information(this, tr("AzaharPlus"), tr("Nothing to decrypt"));
		return;
	}
	
    game_list->SetDirectoryWatcherEnabled(false);
	
	std::map<int, int> results;
    QProgressDialog progress(tr("Removing Azahar encryption..."), tr("Abort"), 0, (int)paths.size(), this);
    progress.setWindowModality(Qt::WindowModal);

	for(size_t i=0; i<paths.size(); i++)
	{
		progress.setValue((int)i);
		
		if (progress.wasCanceled())
        {
			break;
		}
		
		results[HW::UniqueData::RemoveAzaharEncryption(paths[i])]++;
	}
	
	progress.setValue((int)paths.size());
	
	QMessageBox::information(this, tr("AzaharPlus"), tr("%1 file(s) succesfully decrypted\n%2 file(s) file system errors\n%3 file(s) unable to be decrypted").arg(results[0]).arg(results[1]).arg(results[2]));

	game_list->SetDirectoryWatcherEnabled(true);
}

void GMainWindow::OnDownloadSystemFilesMenu(u32 region) {
	game_list->SetDirectoryWatcherEnabled(false);
	
    const auto mode = Core::SystemTitleSet::OldAndNew;
    const std::vector<u64> titles = Core::GetSystemTitleIds(mode, region);

    QProgressDialog progress(tr("Downloading system files..."), tr("Cancel"), 0,
                             static_cast<int>(titles.size()), this);
    progress.setWindowModality(Qt::WindowModal);

    QFutureWatcher<void> future_watcher;
    QObject::connect(&future_watcher, &QFutureWatcher<void>::finished, &progress,
                     &QProgressDialog::reset);
    QObject::connect(&progress, &QProgressDialog::canceled, &future_watcher,
                     &QFutureWatcher<void>::cancel);
    QObject::connect(&future_watcher, &QFutureWatcher<void>::progressValueChanged, &progress,
                     &QProgressDialog::setValue);

    auto failed = false;
    const auto download_title = [&future_watcher, &failed](const u64& title_id) {
        if (Service::AM::InstallFromNus(title_id) != Service::AM::InstallStatus::Success) {
            failed = true;
            future_watcher.cancel();
        }
    };

    future_watcher.setFuture(QtConcurrent::map(titles, download_title));
    progress.exec();
    future_watcher.waitForFinished();

    if (failed) {
        QMessageBox::critical(this, tr("AzaharPlus"), tr("Downloading system files failed."));
    } else if (!future_watcher.isCanceled()) {
        QMessageBox::information(this, tr("AzaharPlus"), tr("Successfully downloaded system files."));
    }
	
	game_list->SetDirectoryWatcherEnabled(true);
    game_list->PopulateAsync(UISettings::values.game_dirs);
	UpdateBootHomeMenuState();
}

void GMainWindow::OnMenuBootHomeMenu(u32 region) {
    BootGame(QString::fromStdString(Core::GetHomeMenuNcchPath(region)));
}

void GMainWindow::InstallCIA(QStringList filepaths) {
    ui->action_Install_CIA->setEnabled(false);
    game_list->SetDirectoryWatcherEnabled(false);

    emit UpdateProgress(0, 0);

    (void)QtConcurrent::run([&, filepaths] {
        Service::AM::InstallStatus status;
        const auto cia_progress = [&](std::size_t written, std::size_t total) {
            emit UpdateProgress(written, total);
        };
        for (const auto& current_path : filepaths) {
            status = Service::AM::InstallCIA(current_path.toStdString(), cia_progress);
            emit CIAInstallReport(status, current_path);
        }
        emit CIAInstallFinished();
    });
}

void GMainWindow::OnUpdateProgress(std::size_t written, std::size_t total) {
    if (written == 0 and total == 0) {
        progress_bar->show();
        progress_bar->setValue(0);
        progress_bar->setMaximum(INT_MAX);
    }
    progress_bar->setValue(
        static_cast<int>(INT_MAX * (static_cast<double>(written) / static_cast<double>(total))));
}

void GMainWindow::OnCIAInstallReport(Service::AM::InstallStatus status, QString filepath) {
    QString filename = QFileInfo(filepath).fileName();
    switch (status) {
    case Service::AM::InstallStatus::Success:
        this->statusBar()->showMessage(tr("%1 has been installed successfully.").arg(filename));
        break;
    case Service::AM::InstallStatus::ErrorFailedToOpenFile:
        QMessageBox::critical(this, tr("Unable to open File"),
                              tr("Could not open %1").arg(filename));
        break;
    case Service::AM::InstallStatus::ErrorAborted:
        QMessageBox::critical(
            this, tr("Installation aborted"),
            tr("The installation of %1 was aborted. Please see the log for more details")
                .arg(filename));
        break;
    case Service::AM::InstallStatus::ErrorInvalid:
        QMessageBox::critical(this, tr("Invalid File"), tr("%1 is not a valid CIA").arg(filename));
        break;
    case Service::AM::InstallStatus::ErrorEncrypted:
        QMessageBox::critical(this, tr("Encrypted File"),
                              tr("%1 must be decrypted "
                                 "before being used with Azahar. A real 3DS is required.")
                                  .arg(filename));
        break;
    case Service::AM::InstallStatus::ErrorFileNotFound:
        QMessageBox::critical(this, tr("Unable to find File"),
                              tr("Could not find %1").arg(filename));
        break;
    }
}

void GMainWindow::OnCompressFinished(bool is_compress, bool success) {
    progress_bar->hide();
    progress_bar->setValue(0);

    if (!success) {
        if (is_compress) {
            QMessageBox::critical(this, tr("Z3DS Compression"),
                                  tr("Failed to compress some files, check log for details."));
        } else {
            QMessageBox::critical(this, tr("Z3DS Compression"),
                                  tr("Failed to decompress some files, check log for details."));
        }
    } else {
        if (is_compress) {
            QMessageBox::information(this, tr("Z3DS Compression"),
                                     tr("All files have been compressed successfully."));
        } else {
            QMessageBox::information(this, tr("Z3DS Compression"),
                                     tr("All files have been decompressed successfully."));
        }
    }
}

void GMainWindow::OnCIAInstallFinished() {
    progress_bar->hide();
    progress_bar->setValue(0);
    game_list->SetDirectoryWatcherEnabled(true);
    ui->action_Install_CIA->setEnabled(true);
    game_list->PopulateAsync(UISettings::values.game_dirs);
}

void GMainWindow::UninstallTitles(
    const std::vector<std::tuple<Service::FS::MediaType, u64, QString>>& titles) {
    if (titles.empty()) {
        return;
    }

    // Select the first title in the list as representative.
    const auto first_name = std::get<QString>(titles[0]);

    QProgressDialog progress(tr("Uninstalling '%1'...").arg(first_name), tr("Cancel"), 0,
                             static_cast<int>(titles.size()), this);
    progress.setWindowModality(Qt::WindowModal);

    QFutureWatcher<void> future_watcher;
    QObject::connect(&future_watcher, &QFutureWatcher<void>::finished, &progress,
                     &QProgressDialog::reset);
    QObject::connect(&progress, &QProgressDialog::canceled, &future_watcher,
                     &QFutureWatcher<void>::cancel);
    QObject::connect(&future_watcher, &QFutureWatcher<void>::progressValueChanged, &progress,
                     &QProgressDialog::setValue);

    auto failed = false;
    QString failed_name;

    const auto uninstall_title = [&future_watcher, &failed, &failed_name](const auto& title) {
        const auto name = std::get<QString>(title);
        const auto media_type = std::get<Service::FS::MediaType>(title);
        const auto program_id = std::get<u64>(title);

        const auto result = Service::AM::UninstallProgram(media_type, program_id);
        if (result.IsError()) {
            LOG_ERROR(Frontend, "Failed to uninstall '{}': 0x{:08X}", name.toStdString(),
                      result.raw);
            failed = true;
            failed_name = name;
            future_watcher.cancel();
        }
    };

    future_watcher.setFuture(QtConcurrent::map(titles, uninstall_title));
    progress.exec();
    future_watcher.waitForFinished();

    if (failed) {
        QMessageBox::critical(this, tr("Azahar"), tr("Failed to uninstall '%1'.").arg(failed_name));
    } else if (!future_watcher.isCanceled()) {
        QMessageBox::information(this, tr("Azahar"),
                                 tr("Successfully uninstalled '%1'.").arg(first_name));
    }
}

void GMainWindow::OnPreviousAmiibo() {
	LOG_ERROR(Frontend, "OnPreviousAmiibo");
	
	if (!emu_thread || !emu_thread->IsRunning()) [[unlikely]] {
        return;
    }

    Service::SM::ServiceManager& sm = system.ServiceManager();
    auto nfc = sm.GetService<Service::NFC::Module::Interface>("nfc:u");
    if (nfc == nullptr) {
        return;
    }

    std::scoped_lock lock{system.Kernel().GetHLELock()};
    if (nfc->IsTagActive()) {
        QMessageBox::warning(this, tr("Error opening amiibo data file"),
                             tr("A tag is already in use."));
        return;
    }

    if (!nfc->IsSearchingForAmiibos()) {
        QMessageBox::warning(this, tr("Error opening amiibo data file"),
                             tr("Application is not looking for amiibos."));
        return;
    }
    
	LoadAmiibo(QStringLiteral("@previous"));
}

void GMainWindow::OnMenuAmiiboFileAction() {
    QAction* action = qobject_cast<QAction*>(sender());
    ASSERT(action);

    const QString amiiboId = action->data().toString();
	
	QString out_path = QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::UserDir))
						 + QStringLiteral(DIR_SEP) 
						 + QString::fromStdString(amiibos[amiiboId.toStdString()]);
	QString fileName = QFileDialog::getSaveFileName(this, tr("Generate Amiibo File"),
													out_path,
													tr("Amiibo (*.bin)"));
	if(fileName.length() == 0) return;
	
	Service::NFC::makeAmiiboFile(amiiboId.toStdString(), fileName.toStdString());
}

void GMainWindow::OnMenuAmiiboAction() {
    QAction* action = qobject_cast<QAction*>(sender());
    ASSERT(action);

    const QString amiiboId = action->data().toString();
	
	if (!emu_thread || !emu_thread->IsRunning()) [[unlikely]] {
        return;
    }

    Service::SM::ServiceManager& sm = system.ServiceManager();
    auto nfc = sm.GetService<Service::NFC::Module::Interface>("nfc:u");
    if (nfc == nullptr) {
        return;
    }

    std::scoped_lock lock{system.Kernel().GetHLELock()};
    if (nfc->IsTagActive()) {
        QMessageBox::warning(this, tr("Error opening amiibo data file"),
                             tr("A tag is already in use."));
        return;
    }

    if (!nfc->IsSearchingForAmiibos()) {
        QMessageBox::warning(this, tr("Error opening amiibo data file"),
                             tr("Application is not looking for amiibos."));
        return;
    }
    
	ui->action_Previous_Amiibo->setEnabled(true);
	
	LoadAmiibo(amiiboId);
}

void GMainWindow::OnMenuRecentFile() {
    QAction* action = qobject_cast<QAction*>(sender());
    ASSERT(action);

    const QString filename = action->data().toString();
    if (QFileInfo::exists(filename)) {
        BootGame(filename);
    } else {
        // Display an error message and remove the file from the list.
        QMessageBox::information(this, tr("File not found"),
                                 tr("File \"%1\" not found").arg(filename));

        UISettings::values.recent_files.removeOne(filename);
        UpdateRecentFiles();
    }
}

void GMainWindow::OnResumeGame(bool first_start) {
    qt_cameras->ResumeCameras();

    PreventOSSleep();

    emu_thread->SetRunning(true);
    system.frame_limiter.SetFrameAdvancing(false);
    graphics_api_button->setEnabled(false);
    qRegisterMetaType<Core::System::ResultStatus>("Core::System::ResultStatus");
    qRegisterMetaType<std::string>("std::string");
    connect(emu_thread.get(), &EmuThread::ErrorThrown, this, &GMainWindow::OnCoreError);

    UpdateMenuState();

    play_time_manager->SetProgramId(game_title_id);
    play_time_manager->Start();

    if (first_start) {
#ifdef USE_DISCORD_PRESENCE
        discord_rpc->Update(true);
#endif
    }

#ifdef __unix__
    Common::Linux::StartGamemode();
#endif

    UpdateSaveStates();
    UpdateStatusButtons();
}

void GMainWindow::OnRestartGame() {
    if (!system.IsPoweredOn()) {
        return;
    }
    // Make a copy since BootGame edits game_path
    BootGame(QString(game_path));
}

void GMainWindow::OnPauseGame() {
    system.frame_limiter.SetFrameAdvancing(true);
    qt_cameras->PauseCameras();

    play_time_manager->Stop();

    UpdateMenuState();
    AllowOSSleep();

#ifdef __unix__
    Common::Linux::StopGamemode();
#endif
}

void GMainWindow::OnPauseContinueGame() {
    if (emulation_running) {
        if (emu_thread->IsRunning() && !system.frame_limiter.IsFrameAdvancing()) {
            OnPauseGame();
        } else {
            OnResumeGame(false);
        }
    }
}

void GMainWindow::OnStopGame() {
    SetTurboEnabled(false);

    play_time_manager->Stop();
    // Update game list to show new play time
    game_list->PopulateAsync(UISettings::values.game_dirs);

    ShutdownGame();
    graphics_api_button->setEnabled(true);
    Settings::RestoreGlobalState(false);
    UpdateStatusButtons();
}

void GMainWindow::OnLoadComplete() {
    loading_screen->OnLoadComplete();
    UpdateSecondaryWindowVisibility();
}

void GMainWindow::ToggleFullscreen() {
    if (!emulation_running) {
        return;
    }
    if (ui->action_Fullscreen->isChecked()) {
        ShowFullscreen();
    } else {
        HideFullscreen();
    }
}

void GMainWindow::ToggleSecondaryFullscreen() {
    if (!emulation_running) {
        return;
    }
    if (secondary_window->isFullScreen()) {
#ifdef NEEDS_ROUND_CORNERS_FIX
        WindowCornerManager::instance().blockRoundedCorners(secondary_window, false);
#endif
        secondary_window->showNormal();
    } else {
#ifdef NEEDS_ROUND_CORNERS_FIX
        WindowCornerManager::instance().blockRoundedCorners(secondary_window, true);
#endif
        secondary_window->showFullScreen();
    }
}

void GMainWindow::ShowFullscreen() {
    if (ui->action_Single_Window_Mode->isChecked()) {
        UISettings::values.geometry = saveGeometry();
        ui->menubar->hide();
        statusBar()->hide();
#ifdef NEEDS_ROUND_CORNERS_FIX
        WindowCornerManager::instance().blockRoundedCorners(this, true);
#endif
        showFullScreen();
    } else {
        UISettings::values.renderwindow_geometry = render_window->saveGeometry();
#ifdef NEEDS_ROUND_CORNERS_FIX
        WindowCornerManager::instance().blockRoundedCorners(render_window, true);
#endif
        render_window->showFullScreen();
    }
}

void GMainWindow::HideFullscreen() {
    if (ui->action_Single_Window_Mode->isChecked()) {
        statusBar()->setVisible(ui->action_Show_Status_Bar->isChecked());
        ui->menubar->show();
#ifdef NEEDS_ROUND_CORNERS_FIX
        WindowCornerManager::instance().blockRoundedCorners(this, false);
#endif
        showNormal();
        restoreGeometry(UISettings::values.geometry);
    } else {
#ifdef NEEDS_ROUND_CORNERS_FIX
        WindowCornerManager::instance().blockRoundedCorners(render_window, false);
#endif
        render_window->showNormal();
        render_window->restoreGeometry(UISettings::values.renderwindow_geometry);
    }
}

void GMainWindow::ToggleWindowMode() {
    if (ui->action_Single_Window_Mode->isChecked()) {
        // Render in the main window...
        render_window->BackupGeometry();
        ui->horizontalLayout->addWidget(render_window);
        render_window->setFocusPolicy(Qt::StrongFocus);
        if (emulation_running) {
            render_window->setVisible(true);
            render_window->setFocus();
            game_list->hide();
        }

    } else {
        // Render in a separate window...
        ui->horizontalLayout->removeWidget(render_window);
        render_window->setParent(nullptr);
        render_window->setFocusPolicy(Qt::NoFocus);
        if (emulation_running) {
            render_window->setVisible(true);
            render_window->RestoreGeometry();
            game_list->show();
        }
    }
}

void GMainWindow::UpdateSecondaryWindowVisibility() {
    if (!emulation_running) {
        return;
    }
    if (Settings::values.layout_option.GetValue() == Settings::LayoutOption::SeparateWindows) {
        secondary_window->RestoreGeometry();
        secondary_window->show();
    } else {
        secondary_window->BackupGeometry();
        secondary_window->hide();
    }
}

void GMainWindow::ChangeScreenLayout() {
    Settings::LayoutOption new_layout = Settings::LayoutOption::Default;
    if (ui->action_Screen_Layout_Default->isChecked()) {
        new_layout = Settings::LayoutOption::Default;
    } else if (ui->action_Screen_Layout_Single_Screen->isChecked()) {
        new_layout = Settings::LayoutOption::SingleScreen;
    } else if (ui->action_Screen_Layout_Large_Screen->isChecked()) {
        new_layout = Settings::LayoutOption::LargeScreen;
        ui->menu_Small_Screen_Position->setEnabled(true);
    } else if (ui->action_Screen_Layout_Hybrid_Screen->isChecked()) {
        new_layout = Settings::LayoutOption::HybridScreen;
    } else if (ui->action_Screen_Layout_Side_by_Side->isChecked()) {
        new_layout = Settings::LayoutOption::SideScreen;
    } else if (ui->action_Screen_Layout_Separate_Windows->isChecked()) {
        new_layout = Settings::LayoutOption::SeparateWindows;
    } else if (ui->action_Screen_Layout_Custom_Layout->isChecked()) {
        new_layout = Settings::LayoutOption::CustomLayout;
    }

    Settings::values.layout_option = new_layout;
    SyncMenuUISettings();
    system.ApplySettings();
    UpdateSecondaryWindowVisibility();
}

void GMainWindow::ChangeSmallScreenPosition() {
    Settings::SmallScreenPosition new_position = Settings::SmallScreenPosition::BottomRight;

    if (ui->action_Small_Screen_TopRight->isChecked()) {
        new_position = Settings::SmallScreenPosition::TopRight;
    } else if (ui->action_Small_Screen_MiddleRight->isChecked()) {
        new_position = Settings::SmallScreenPosition::MiddleRight;
    } else if (ui->action_Small_Screen_BottomRight->isChecked()) {
        new_position = Settings::SmallScreenPosition::BottomRight;
    } else if (ui->action_Small_Screen_TopLeft->isChecked()) {
        new_position = Settings::SmallScreenPosition::TopLeft;
    } else if (ui->action_Small_Screen_MiddleLeft->isChecked()) {
        new_position = Settings::SmallScreenPosition::MiddleLeft;
    } else if (ui->action_Small_Screen_BottomLeft->isChecked()) {
        new_position = Settings::SmallScreenPosition::BottomLeft;
    } else if (ui->action_Small_Screen_Above->isChecked()) {
        new_position = Settings::SmallScreenPosition::AboveLarge;
    } else if (ui->action_Small_Screen_Below->isChecked()) {
        new_position = Settings::SmallScreenPosition::BelowLarge;
    }

    Settings::values.small_screen_position = new_position;
    SyncMenuUISettings();
    system.ApplySettings();
    UpdateSecondaryWindowVisibility();
}

bool GMainWindow::IsTurboEnabled() {
    return turbo_mode_active;
}

void GMainWindow::SetTurboEnabled(bool state) {
    turbo_mode_active = state;
    GMainWindow::ReloadTurbo();
}

void GMainWindow::ReloadTurbo() {
    if (IsTurboEnabled()) {
        Settings::temporary_frame_limit = Settings::values.turbo_limit.GetValue();
        Settings::is_temporary_frame_limit = true;
    } else {
        Settings::is_temporary_frame_limit = false;
    }

    UpdateStatusBar();
}

// TODO: This should probably take in something more descriptive than a bool. -OS
void GMainWindow::AdjustSpeedLimit(bool increase) {
    const int SPEED_LIMIT_STEP = 5;
    auto active_limit =
        IsTurboEnabled() ? &Settings::values.turbo_limit : &Settings::values.frame_limit;
    const auto active_limit_value = active_limit->GetValue();

    if (increase) {
        if (active_limit_value < 995) {
            active_limit->SetValue(active_limit_value + SPEED_LIMIT_STEP);
        }
    } else {
        if (active_limit_value > SPEED_LIMIT_STEP) {
            active_limit->SetValue(active_limit_value - SPEED_LIMIT_STEP);
        }
    }

    if (IsTurboEnabled()) {
        ReloadTurbo();
    }

    UpdateStatusBar();
}

void GMainWindow::ToggleScreenLayout() {
    const Settings::LayoutOption new_layout = []() {
        const Settings::LayoutOption current_layout = Settings::values.layout_option.GetValue();
        std::vector<Settings::LayoutOption> layouts_to_cycle =
            Settings::values.layouts_to_cycle.GetValue();
        const auto current_pos =
            distance(layouts_to_cycle.begin(),
                     std::find(layouts_to_cycle.begin(), layouts_to_cycle.end(), current_layout));
        // if the layouts_to_cycle setting has somehow been
        // cleared out, add just default back in
        if (layouts_to_cycle.size() == 0) {
            layouts_to_cycle.push_back(Settings::LayoutOption::Default);
        }
        if (current_pos >= layouts_to_cycle.size() - 1) {
            // either this layout wasn't found or it was last so move to the beginning
            return layouts_to_cycle[0];
        } else {
            return layouts_to_cycle[current_pos + 1];
        }
    }();

    Settings::values.layout_option = new_layout;
    SyncMenuUISettings();
    system.ApplySettings();
    UpdateSecondaryWindowVisibility();
}

void GMainWindow::OnSwapScreens() {
    Settings::values.swap_screen = ui->action_Screen_Layout_Swap_Screens->isChecked();
    system.ApplySettings();
}

void GMainWindow::OnRotateScreens() {
    Settings::values.upright_screen = ui->action_Screen_Layout_Upright_Screens->isChecked();
    system.ApplySettings();
}

void GMainWindow::TriggerSwapScreens() {
    ui->action_Screen_Layout_Swap_Screens->trigger();
}

void GMainWindow::TriggerRotateScreens() {
    ui->action_Screen_Layout_Upright_Screens->trigger();
}

void GMainWindow::OnSaveState() {
    QAction* action = qobject_cast<QAction*>(sender());
    ASSERT(action);

    system.SendSignal(Core::System::Signal::Save, action->data().toUInt());
    system.frame_limiter.AdvanceFrame();
    newest_slot = action->data().toUInt();
}

void GMainWindow::OnLoadState() {
    QAction* action = qobject_cast<QAction*>(sender());
    ASSERT(action);

    if (UISettings::values.save_state_warning) {
        QMessageBox::warning(
            this, tr("Savestates"),
            tr("Warning: Savestates are NOT a replacement for in-application saves, "
               "and are not meant to be reliable.\n\nUse at your own risk!"));
        UISettings::values.save_state_warning = false;
        config->Save();
    }

    system.SendSignal(Core::System::Signal::Load, action->data().toUInt());
    system.frame_limiter.AdvanceFrame();
}

void GMainWindow::OnConfigure() {
    game_list->SetDirectoryWatcherEnabled(false);
    Settings::SetConfiguringGlobal(true);
    ConfigureDialog configureDialog(this, hotkey_registry, system, gl_renderer, physical_devices,
                                    !multiplayer_state->IsHostingPublicRoom());
    connect(&configureDialog, &ConfigureDialog::LanguageChanged, this,
            &GMainWindow::OnLanguageChanged);
    auto old_theme = UISettings::values.theme;
    const int old_input_profile_index = Settings::values.current_input_profile_index;
    const auto old_input_profiles = Settings::values.input_profiles;
    const auto old_touch_from_button_maps = Settings::values.touch_from_button_maps;
#ifdef USE_DISCORD_PRESENCE
    const bool old_discord_presence = UISettings::values.enable_discord_presence.GetValue();
#endif
#ifdef __unix__
    const bool old_gamemode = Settings::values.enable_gamemode.GetValue();
#endif
    auto result = configureDialog.exec();
    game_list->SetDirectoryWatcherEnabled(true);
    if (result == QDialog::Accepted) {
        configureDialog.ApplyConfiguration();
        InitializeHotkeys();
        if (UISettings::values.theme != old_theme) {
            UpdateUITheme();
        }
#ifdef USE_DISCORD_PRESENCE
        if (UISettings::values.enable_discord_presence.GetValue() != old_discord_presence) {
            SetDiscordEnabled(UISettings::values.enable_discord_presence.GetValue());
            discord_rpc->Update(system.IsPoweredOn());
        }
#endif
#ifdef __unix__
        if (Settings::values.enable_gamemode.GetValue() != old_gamemode) {
            SetGamemodeEnabled(Settings::values.enable_gamemode.GetValue());
        }
#endif
        if (!multiplayer_state->IsHostingPublicRoom())
            multiplayer_state->UpdateCredentials();
        emit UpdateThemedIcons();
        SyncMenuUISettings();
        game_list->RefreshGameDirectory();
        config->Save();
        if (UISettings::values.hide_mouse && emulation_running) {
            setMouseTracking(true);
            mouse_hide_timer.start();
        } else {
            setMouseTracking(false);
        }
        ReloadTurbo();
        UpdateSecondaryWindowVisibility();
        UpdateBootHomeMenuState();
        UpdateStatusButtons();
    } else {
        Settings::values.input_profiles = old_input_profiles;
        Settings::values.touch_from_button_maps = old_touch_from_button_maps;
        Settings::LoadProfile(old_input_profile_index);
    }
}

void GMainWindow::OnExportZipPass() {
	QString out_path = QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::UserDir));
	QString fileName = QFileDialog::getSaveFileName(this, tr("Export ZipPass Data"),
													out_path,
													tr("ZipPass (*.pass.zip)"));
	if(fileName.length() == 0) return;
	
	int ret = Core::exportZipPass(fileName.toStdString());
	
	if(ret < 0){
		QMessageBox::critical(this, tr("Export ZipPass Data"), tr("Failure"));
	}else if(ret == 0){
		QMessageBox::warning(this, tr("Export ZipPass Data"), tr("Nothing to export"));
	}else{
		QMessageBox::information(this, tr("Export ZipPass Data"), tr("Success"));
	}
}

void GMainWindow::OnClearStreetPassConfig() {
	Core::clearStreetPassConfig();
	
	QMessageBox::information(this, tr("Clear StreetPass Configuration"), tr("Success"));
}

void GMainWindow::OnImportZipPass() {
	QString out_path = QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::UserDir));
	QStringList fileNames = QFileDialog::getOpenFileNames(this, tr("Import ZipPass Data"),
													out_path,
													tr("ZipPass (*.pass.zip)"));
	int ret = 0;
	int err = 0;
	
	if(fileNames.size() == 0) return;
	
	for (const QString& fileName: fileNames){
		int res = Core::importZipPass(fileName.toStdString());
		
		if(res > 0) ret++;
        if(res < 0) err++;
	}
	
	if(err > 0 && ret == 0) ret = -1;
	
	if(ret < 0){
		QMessageBox::critical(this, tr("Import ZipPass Data"), tr("Failure"));
	}else if(ret == 0){
		QMessageBox::warning(this, tr("Import ZipPass Data"), tr("Nothing to import"));
	}else{
		QMessageBox::information(this, tr("Import ZipPass Data"), tr("Success"));
	}
}

void GMainWindow::OnLoadAmiibo() {
    if (!emu_thread || !emu_thread->IsRunning()) [[unlikely]] {
        return;
    }

    Service::SM::ServiceManager& sm = system.ServiceManager();
    auto nfc = sm.GetService<Service::NFC::Module::Interface>("nfc:u");
    if (nfc == nullptr) {
        return;
    }

    std::scoped_lock lock{system.Kernel().GetHLELock()};
    if (nfc->IsTagActive()) {
        QMessageBox::warning(this, tr("Error opening amiibo data file"),
                             tr("A tag is already in use."));
        return;
    }

    if (!nfc->IsSearchingForAmiibos()) {
        QMessageBox::warning(this, tr("Error opening amiibo data file"),
                             tr("Application is not looking for amiibos."));
        return;
    }

    const QString extensions{QStringLiteral("*.bin")};
    const QString file_filter = tr("Amiibo File (%1);; All Files (*.*)").arg(extensions);
    const QString filename = QFileDialog::getOpenFileName(this, tr("Load Amiibo"), {}, file_filter);

    if (filename.isEmpty()) {
        return;
    }

    LoadAmiibo(filename);
}

void GMainWindow::LoadAmiibo(const QString& filename) {
    Service::SM::ServiceManager& sm = system.ServiceManager();
    auto nfc = sm.GetService<Service::NFC::Module::Interface>("nfc:u");
    if (!nfc) [[unlikely]] {
        return;
    }

    std::scoped_lock lock{system.Kernel().GetHLELock()};
    if (!nfc->LoadAmiibo(filename.toStdString())) {
        QMessageBox::warning(this, tr("Error opening amiibo data file"),
                             tr("Unable to open amiibo file \"%1\" for reading.").arg(filename));
        return;
    }

    ui->action_Remove_Amiibo->setEnabled(true);
}

void GMainWindow::OnRemoveAmiibo() {
    Service::SM::ServiceManager& sm = system.ServiceManager();
    auto nfc = sm.GetService<Service::NFC::Module::Interface>("nfc:u");
    if (!nfc) [[unlikely]] {
        return;
    }

    std::scoped_lock lock{system.Kernel().GetHLELock()};
    nfc->RemoveAmiibo();
    ui->action_Remove_Amiibo->setEnabled(false);
}

void GMainWindow::OnOpenCitraFolder() {
    QDesktopServices::openUrl(QUrl::fromLocalFile(
        QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::UserDir))));
}

void GMainWindow::OnToggleFilterBar() {
    game_list->SetFilterVisible(ui->action_Show_Filter_Bar->isChecked());
    if (ui->action_Show_Filter_Bar->isChecked()) {
        game_list->SetFilterFocus();
    } else {
        game_list->ClearFilter();
    }
}

void GMainWindow::OnCreateGraphicsSurfaceViewer() {
    auto graphicsSurfaceViewerWidget =
        new GraphicsSurfaceWidget(system, Pica::g_debug_context, this);
    addDockWidget(Qt::RightDockWidgetArea, graphicsSurfaceViewerWidget);
    // TODO: Maybe graphicsSurfaceViewerWidget->setFloating(true);
    graphicsSurfaceViewerWidget->show();
}

void GMainWindow::OnRecordMovie() {
    MovieRecordDialog dialog(this, system);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    movie_record_on_start = true;
    movie_record_path = dialog.GetPath();
    movie_record_author = dialog.GetAuthor();

    if (emulation_running) { // Restart game
        BootGame(QString(game_path));
    }
    ui->action_Close_Movie->setEnabled(true);
    ui->action_Save_Movie->setEnabled(true);
}

void GMainWindow::OnPlayMovie() {
    MoviePlayDialog dialog(this, game_list, system);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    movie_playback_on_start = true;
    movie_playback_path = dialog.GetMoviePath();
    BootGame(dialog.GetGamePath());

    ui->action_Close_Movie->setEnabled(true);
    ui->action_Save_Movie->setEnabled(false);
}

void GMainWindow::OnCloseMovie() {
    if (movie_record_on_start) {
        QMessageBox::information(this, tr("Record Movie"), tr("Movie recording cancelled."));
        movie_record_on_start = false;
        movie_record_path.clear();
        movie_record_author.clear();
    } else {
        const bool was_running = emu_thread && emu_thread->IsRunning();
        if (was_running) {
            OnPauseGame();
        }

        const bool was_recording = movie.GetPlayMode() == Core::Movie::PlayMode::Recording;
        movie.Shutdown();
        if (was_recording) {
            QMessageBox::information(this, tr("Movie Saved"),
                                     tr("The movie is successfully saved."));
        }

        if (was_running) {
            OnResumeGame(false);
        }
    }

    ui->action_Close_Movie->setEnabled(false);
    ui->action_Save_Movie->setEnabled(false);
}

void GMainWindow::OnSaveMovie() {
    const bool was_running = emu_thread && emu_thread->IsRunning();
    if (was_running) {
        OnPauseGame();
    }

    if (movie.GetPlayMode() == Core::Movie::PlayMode::Recording) {
        movie.SaveMovie();
        QMessageBox::information(this, tr("Movie Saved"), tr("The movie is successfully saved."));
    } else {
        LOG_ERROR(Frontend, "Tried to save movie while movie is not being recorded");
    }

    if (was_running) {
        OnResumeGame(false);
    }
}

void GMainWindow::OnCaptureScreenshot() {
    if (!emu_thread) [[unlikely]] {
        return;
    }

    const bool was_running = emu_thread->IsRunning();

    if (was_running || (QMessageBox::question(this, tr("Application will unpause"),
                                              tr("The application will be unpaused, and the next "
                                                 "frame will be captured. Is this okay?"),
                                              QMessageBox::Yes | QMessageBox::No,
                                              QMessageBox::No) == QMessageBox::Yes)) {
        if (was_running) {
            OnPauseGame();
        }
        std::string path = UISettings::values.screenshot_path.GetValue();
        if (!FileUtil::IsDirectory(path)) {
            if (!FileUtil::CreateFullPath(path)) {
                QMessageBox::information(
                    this, tr("Invalid Screenshot Directory"),
                    tr("Cannot create specified screenshot directory. Screenshot "
                       "path is set back to its default value."));
                path = FileUtil::GetUserPath(FileUtil::UserPath::UserDir);
                path.append("screenshots/");
                UISettings::values.screenshot_path = path;
            };
        }

        static QRegularExpression expr(QStringLiteral("[\\/:?\"<>|]"));
        const std::string filename = game_title.remove(expr).toStdString();
        const std::string timestamp = QDateTime::currentDateTime()
                                          .toString(QStringLiteral("dd.MM.yy_hh.mm.ss.z"))
                                          .toStdString();
        path.append(fmt::format("/{}_{}.png", filename, timestamp));

        auto* const screenshot_window =
            secondary_window->HasFocus() ? secondary_window : render_window;
        screenshot_window->CaptureScreenshot(
            UISettings::values.screenshot_resolution_factor.GetValue(),
            QString::fromStdString(path));
        OnResumeGame(false);
    }
}

void GMainWindow::ShowFFmpegErrorMessage() {
    QMessageBox message_box;
    message_box.setWindowTitle(tr("Could not load video dumper"));
    message_box.setText(
        tr("FFmpeg could not be loaded. Make sure you have a compatible version installed."
#ifdef _WIN32
           "\n\nTo install FFmpeg to Azahar, press Open and select your FFmpeg directory."
#endif
           "\n\nTo view a guide on how to install FFmpeg, press Help."));
    message_box.setStandardButtons(QMessageBox::Ok | QMessageBox::Help
#ifdef _WIN32
                                   | QMessageBox::Open
#endif
    );
    auto result = message_box.exec();
    if (result == QMessageBox::Help) {
        QDesktopServices::openUrl(
            QUrl(QStringLiteral("https://web.archive.org/web/20240301121456/https://"
                                "citra-emu.org/wiki/installing-ffmpeg-for-the-video-dumper/")));
#ifdef _WIN32
    } else if (result == QMessageBox::Open) {
        OnOpenFFmpeg();
#endif
    }
}

void GMainWindow::OnDumpVideo() {
    if (DynamicLibrary::FFmpeg::LoadFFmpeg()) {
        if (ui->action_Dump_Video->isChecked()) {
            OnStartVideoDumping();
        } else {
            OnStopVideoDumping();
        }
    } else {
        ui->action_Dump_Video->setChecked(false);
        ShowFFmpegErrorMessage();
    }
}

void GMainWindow::OnCompressFile() {
    // NOTE: Encrypted files SHOULD NEVER be compressed, otherwise the resulting
    // compressed file will have very poor compression ratios, due to the high
    // entropy caused by encryption. This may cause confusion to the user as they
    // will see the files do not compress well and blame the emulator.
    //
    // This is enforced using the loaders as they already return an error on encryption.

    QStringList filepaths =
        QFileDialog::getOpenFileNames(this, tr("Load 3DS ROM Files"), UISettings::values.roms_path,
                                      tr("3DS ROM Files (*.cia *.cci *.3dsx *.cxi *.3ds)") +
                                          QStringLiteral(";;") + tr("All Files (*.*)"));

    QString out_path;

    if (filepaths.isEmpty()) {
        return;
    }

    bool single_file = filepaths.size() == 1;
    if (single_file) {
        // If it's a single file, ask the user for the output file.
        auto compress_info = Loader::GetCompressFileInfo(filepaths[0].toStdString(), true);
        if (!compress_info.has_value()) {
            emit CompressFinished(true, false);
            return;
        }

        QFileInfo fileinfo(filepaths[0]);
        QString final_path =
            fileinfo.path() + QStringLiteral(DIR_SEP) + fileinfo.completeBaseName() +
            QStringLiteral(".") +
            QString::fromStdString(compress_info.value().first.recommended_compressed_extension);

        QString out_filter = tr("3DS Compressed ROM File (*.%1)")
                                 .arg(QString::fromStdString(
                                     compress_info.value().first.recommended_compressed_extension));
        out_path = QFileDialog::getSaveFileName(this, tr("Save 3DS Compressed ROM File"),
                                                final_path, out_filter);
        if (out_path.isEmpty()) {
            return;
        }
    } else {
        // Otherwise, ask the user the directory to output the files.
        out_path = QFileDialog::getExistingDirectory(
            this, tr("Select Output 3DS Compressed ROM Folder"), UISettings::values.roms_path,
            QFileDialog::ShowDirsOnly);
        if (out_path.isEmpty()) {
            return;
        }
    }

    (void)QtConcurrent::run([&, filepaths, out_path] {
        bool single_file = filepaths.size() == 1;
        QString out_filepath;
        bool total_success = true;

        for (const QString& filepath : filepaths) {

            std::string in_path = filepath.toStdString();

            // Identify file type
            auto compress_info = Loader::GetCompressFileInfo(filepath.toStdString(), true);
            if (!compress_info.has_value()) {
                total_success = false;
                continue;
            }

            if (single_file) {
                out_filepath = out_path;
            } else {
                QFileInfo fileinfo(filepath);
                out_filepath = out_path + QStringLiteral(DIR_SEP) + fileinfo.completeBaseName() +
                               QStringLiteral(".") +
                               QString::fromStdString(
                                   compress_info.value().first.recommended_compressed_extension);
            }

            std::string out_path = out_filepath.toStdString();

            emit UpdateProgress(0, 0);

            const auto progress = [&](std::size_t written, std::size_t total) {
                emit UpdateProgress(written, total);
            };
            bool success = FileUtil::CompressZ3DSFile(in_path, out_path,
                                                      compress_info.value().first.underlying_magic,
                                                      compress_info.value().second, progress,
                                                      compress_info.value().first.default_metadata);
            if (!success) {
                total_success = false;
                FileUtil::Delete(out_path);
            }
        }

        emit CompressFinished(true, total_success);
    });
}

void GMainWindow::OnDecompressFile() {

    QStringList filepaths = QFileDialog::getOpenFileNames(
        this, tr("Load 3DS Compressed ROM Files"), UISettings::values.roms_path,
        tr("3DS Compressed ROM Files (*.zcia *zcci *z3dsx *zcxi)") + QStringLiteral(";;") +
            tr("All Files (*.*)"));

    QString out_path;

    if (filepaths.isEmpty()) {
        return;
    }

    bool single_file = filepaths.size() == 1;
    if (single_file) {
        // If it's a single file, ask the user for the output file.
        auto compress_info = Loader::GetCompressFileInfo(filepaths[0].toStdString(), false);
        if (!compress_info.has_value()) {
            emit CompressFinished(false, false);
            return;
        }

        QFileInfo fileinfo(filepaths[0]);
        QString final_path =
            fileinfo.path() + QStringLiteral(DIR_SEP) + fileinfo.completeBaseName() +
            QStringLiteral(".") +
            QString::fromStdString(compress_info.value().first.recommended_uncompressed_extension);

        QString out_filter =
            tr("3DS ROM File (*.%1)")
                .arg(QString::fromStdString(
                    compress_info.value().first.recommended_uncompressed_extension));
        out_path =
            QFileDialog::getSaveFileName(this, tr("Save 3DS ROM File"), final_path, out_filter);
        if (out_path.isEmpty()) {
            return;
        }
    } else {
        // Otherwise, ask the user the directory to output the files.
        out_path = QFileDialog::getExistingDirectory(this, tr("Select Output 3DS ROM Folder"),
                                                     UISettings::values.roms_path,
                                                     QFileDialog::ShowDirsOnly);
        if (out_path.isEmpty()) {
            return;
        }
    }

    (void)QtConcurrent::run([&, filepaths, out_path] {
        bool single_file = filepaths.size() == 1;
        QString out_filepath;
        bool total_success = true;

        for (const QString& filepath : filepaths) {

            std::string in_path = filepath.toStdString();

            // Identify file type
            auto compress_info = Loader::GetCompressFileInfo(filepath.toStdString(), false);
            if (!compress_info.has_value()) {
                total_success = false;
                continue;
            }

            if (single_file) {
                out_filepath = out_path;
            } else {
                QFileInfo fileinfo(filepath);
                out_filepath = out_path + QStringLiteral(DIR_SEP) + fileinfo.completeBaseName() +
                               QStringLiteral(".") +
                               QString::fromStdString(
                                   compress_info.value().first.recommended_uncompressed_extension);
            }

            std::string out_path = out_filepath.toStdString();

            emit UpdateProgress(0, 0);

            const auto progress = [&](std::size_t written, std::size_t total) {
                emit UpdateProgress(written, total);
            };

            // TODO(PabloMK7): What should we do with the metadata?
            bool success = FileUtil::DeCompressZ3DSFile(in_path, out_path, progress);
            if (!success) {
                total_success = false;
                FileUtil::Delete(out_path);
            }
        }

        emit CompressFinished(false, total_success);
    });
}

#ifdef _WIN32
void GMainWindow::OnOpenFFmpeg() {
    auto filename =
        QFileDialog::getExistingDirectory(this, tr("Select FFmpeg Directory")).toStdString();
    if (filename.empty()) {
        return;
    }
    // Check for a bin directory if they chose the FFmpeg root directory.
    auto bin_dir = filename + DIR_SEP + "bin";
    if (!FileUtil::Exists(bin_dir)) {
        // Otherwise, assume the user directly selected the directory containing the DLLs.
        bin_dir = filename;
    }

    static const std::array library_names = {
        Common::DynamicLibrary::GetLibraryName("avcodec", LIBAVCODEC_VERSION_MAJOR),
        Common::DynamicLibrary::GetLibraryName("avfilter", LIBAVFILTER_VERSION_MAJOR),
        Common::DynamicLibrary::GetLibraryName("avformat", LIBAVFORMAT_VERSION_MAJOR),
        Common::DynamicLibrary::GetLibraryName("avutil", LIBAVUTIL_VERSION_MAJOR),
        Common::DynamicLibrary::GetLibraryName("swresample", LIBSWRESAMPLE_VERSION_MAJOR),
    };

    for (auto& library_name : library_names) {
        if (!FileUtil::Exists(bin_dir + DIR_SEP + library_name)) {
            QMessageBox::critical(this, tr("Azahar"),
                                  tr("The provided FFmpeg directory is missing %1. Please make "
                                     "sure the correct directory was selected.")
                                      .arg(QString::fromStdString(library_name)));
            return;
        }
    }

    std::atomic<bool> success(true);
    auto process_file = [&success](u64* num_entries_out, const std::string& directory,
                                   const std::string& virtual_name) -> bool {
        auto file_path = directory + DIR_SEP + virtual_name;
        if (file_path.ends_with(".dll")) {
            auto destination_path = FileUtil::GetExeDirectory() + DIR_SEP + virtual_name;
            if (!FileUtil::Copy(file_path, destination_path)) {
                success.store(false);
                return false;
            }
        }
        return true;
    };
    FileUtil::ForeachDirectoryEntry(nullptr, bin_dir, process_file);

    if (success.load()) {
        QMessageBox::information(this, tr("Azahar"), tr("FFmpeg has been sucessfully installed."));
    } else {
        QMessageBox::critical(this, tr("Azahar"),
                              tr("Installation of FFmpeg failed. Check the log file for details."));
    }
}
#endif

void GMainWindow::OnStartVideoDumping() {
    DumpingDialog dialog(this, system);
    if (dialog.exec() != QDialog::DialogCode::Accepted) {
        ui->action_Dump_Video->setChecked(false);
        return;
    }
    const auto path = dialog.GetFilePath();
    if (emulation_running) {
        StartVideoDumping(path);
    } else {
        video_dumping_on_start = true;
        video_dumping_path = path;
    }
}

void GMainWindow::StartVideoDumping(const QString& path) {
    auto& renderer = system.GPU().Renderer();
    const auto layout{Layout::FrameLayoutFromResolutionScale(renderer.GetResolutionScaleFactor())};

    auto dumper = std::make_shared<VideoDumper::FFmpegBackend>(renderer);
    if (dumper->StartDumping(path.toStdString(), layout)) {
        system.RegisterVideoDumper(dumper);
    } else {
        QMessageBox::critical(
            this, tr("Azahar"),
            tr("Could not start video dumping.<br>Please ensure that the video encoder is "
               "configured correctly.<br>Refer to the log for details."));
        ui->action_Dump_Video->setChecked(false);
    }
}

void GMainWindow::OnStopVideoDumping() {
    ui->action_Dump_Video->setChecked(false);

    if (video_dumping_on_start) {
        video_dumping_on_start = false;
        video_dumping_path.clear();
    } else {
        auto dumper = system.GetVideoDumper();
        if (!dumper || !dumper->IsDumping()) {
            return;
        }

        game_paused_for_dumping = emu_thread->IsRunning();
        OnPauseGame();

        auto future = QtConcurrent::run([dumper] { dumper->StopDumping(); });
        auto* future_watcher = new QFutureWatcher<void>(this);
        connect(future_watcher, &QFutureWatcher<void>::finished, this, [this] {
            if (game_shutdown_delayed) {
                game_shutdown_delayed = false;
                ShutdownGame();
            } else if (game_paused_for_dumping) {
                game_paused_for_dumping = false;
                OnResumeGame(false);
            }
        });
        future_watcher->setFuture(future);
    }
}

void GMainWindow::UpdateStatusBar() {
    if (!emu_thread) [[unlikely]] {
        status_bar_update_timer.stop();
        return;
    }

    // Update movie status
    const u64 current = movie.GetCurrentInputIndex();
    const u64 total = movie.GetTotalInputCount();
    const auto play_mode = movie.GetPlayMode();
    if (play_mode == Core::Movie::PlayMode::Recording) {
        message_label->setText(tr("Recording %1").arg(current));
        message_label_used_for_movie = true;
        ui->action_Save_Movie->setEnabled(true);
    } else if (play_mode == Core::Movie::PlayMode::Playing) {
        message_label->setText(tr("Playing %1 / %2").arg(current).arg(total));
        message_label_used_for_movie = true;
        ui->action_Save_Movie->setEnabled(false);
    } else if (play_mode == Core::Movie::PlayMode::MovieFinished) {
        message_label->setText(tr("Movie Finished"));
        message_label_used_for_movie = true;
        ui->action_Save_Movie->setEnabled(false);
    } else if (message_label_used_for_movie) { // Clear the label if movie was just closed
        message_label->setText(QString{});
        message_label_used_for_movie = false;
        ui->action_Save_Movie->setEnabled(false);
    }

    auto results = system.GetAndResetPerfStats();

    if (show_artic_label) {
        const bool do_mb = results.artic_transmitted >= (1000.0 * 1000.0);
        const double value = do_mb ? (results.artic_transmitted / (1000.0 * 1000.0))
                                   : (results.artic_transmitted / 1000.0);
        static const std::array<std::pair<Core::PerfStats::PerfArticEventBits, QString>, 5>
            perf_events = {
                std::make_pair(Core::PerfStats::PerfArticEventBits::ARTIC_SHARED_EXT_DATA,
                               tr("(Accessing SharedExtData)")),
                std::make_pair(Core::PerfStats::PerfArticEventBits::ARTIC_SYSTEM_SAVE_DATA,
                               tr("(Accessing SystemSaveData)")),
                std::make_pair(Core::PerfStats::PerfArticEventBits::ARTIC_BOSS_EXT_DATA,
                               tr("(Accessing BossExtData)")),
                std::make_pair(Core::PerfStats::PerfArticEventBits::ARTIC_EXT_DATA,
                               tr("(Accessing ExtData)")),
                std::make_pair(Core::PerfStats::PerfArticEventBits::ARTIC_SAVE_DATA,
                               tr("(Accessing SaveData)")),
            };

        const QString unit = do_mb ? tr("MB/s") : tr("KB/s");
        QString event{};
        for (auto p : perf_events) {
            if (results.artic_events.Get(p.first)) {
                event = QString::fromStdString(" ") + p.second;
                break;
            }
        }

        static const std::array label_color = {QStringLiteral(""), QStringLiteral("#eed202"),
                                               QStringLiteral("#ff3333")};

        int style_index;

        if (value > 200.0) {
            style_index = 2;
        } else if (value > 125.0) {
            style_index = 1;
        } else {
            style_index = 0;
        }

        QString style_sheet;
        if (!label_color[style_index].isEmpty()) {
            style_sheet = QStringLiteral("QLabel { color: %0; }").arg(label_color[style_index]);
        }

        artic_traffic_label->setText(
            tr("Artic Traffic: %1 %2%3").arg(value, 0, 'f', 0).arg(unit).arg(event));
        artic_traffic_label->setStyleSheet(style_sheet);
    }

    if (Settings::GetFrameLimit() == 0) {
        emu_speed_label->setText(tr("Speed: %1%").arg(results.emulation_speed * 100.0, 0, 'f', 0));
    } else {
        emu_speed_label->setText(tr("Speed: %1% / %2%")
                                     .arg(results.emulation_speed * 100.0, 0, 'f', 0)
                                     .arg(Settings::GetFrameLimit()));
    }
    game_fps_label->setText(tr("App: %1 FPS").arg(results.game_fps, 0, 'f', 0));
    if (UISettings::values.show_advanced_frametime_info) {
        emu_frametime_label->setText(
            tr("Frame: %1 ms (GPU: [CMD: %2 ms, SWP: %3 ms], IPC: %4 ms, SVC: %5 ms, Rem: %6 ms)")
                .arg(results.time_vblank_interval * 1000.0, 2, 'f', 2)
                .arg(results.time_gpu * 1000.0, 2, 'f', 2)
                .arg(results.time_swap * 1000.0, 2, 'f', 2)
                .arg(results.time_hle_ipc * 1000.0, 2, 'f', 2)
                .arg(results.time_hle_svc * 1000.0, 2, 'f', 2)
                .arg(results.time_remaining * 1000.0, 2, 'f', 2));
    } else {
        emu_frametime_label->setText(
            tr("Frame: %1 ms").arg(results.time_vblank_interval * 1000.0, 2, 'f', 2));
    }

    if (show_artic_label) {
        artic_traffic_label->setVisible(true);
    }
    emu_speed_label->setVisible(true);
    game_fps_label->setVisible(true);
    emu_frametime_label->setVisible(true);
}

void GMainWindow::UpdateBootHomeMenuState() {
    const auto current_region = Settings::values.region_value.GetValue();
    for (u32 region = 0; region < Core::NUM_SYSTEM_TITLE_REGIONS; region++) {
        const auto path = Core::GetHomeMenuNcchPath(region);
        ui->menu_Boot_Home_Menu->actions().at(region)->setEnabled(
            (current_region == Settings::REGION_VALUE_AUTO_SELECT ||
             current_region == static_cast<int>(region)) &&
            !path.empty() && FileUtil::Exists(path));
    }
}

void GMainWindow::HideMouseCursor() {
    if (!emu_thread || !UISettings::values.hide_mouse.GetValue()) {
        mouse_hide_timer.stop();
        ShowMouseCursor();
        return;
    }
    render_window->setCursor(QCursor(Qt::BlankCursor));
    secondary_window->setCursor(QCursor(Qt::BlankCursor));
    if (UISettings::values.single_window_mode.GetValue()) {
        setCursor(QCursor(Qt::BlankCursor));
    }
}

void GMainWindow::ShowMouseCursor() {
    unsetCursor();
    render_window->unsetCursor();
    secondary_window->unsetCursor();
    if (emu_thread && UISettings::values.hide_mouse) {
        mouse_hide_timer.start();
    }
}

void GMainWindow::OnMute() {
    Settings::values.audio_muted = !Settings::values.audio_muted;
    UpdateVolumeUI();
}

void GMainWindow::OnDecreaseVolume() {
    Settings::values.audio_muted = false;
    const auto current_volume =
        static_cast<s32>(Settings::values.volume.GetValue() * volume_slider->maximum());
    int step = 5;
    if (current_volume <= 30) {
        step = 2;
    }
    if (current_volume <= 6) {
        step = 1;
    }
    const auto value =
        static_cast<float>(std::max(current_volume - step, 0)) / volume_slider->maximum();
    Settings::values.volume.SetValue(value);
    UpdateVolumeUI();
}

void GMainWindow::OnIncreaseVolume() {
    Settings::values.audio_muted = false;
    const auto current_volume =
        static_cast<s32>(Settings::values.volume.GetValue() * volume_slider->maximum());
    int step = 5;
    if (current_volume < 30) {
        step = 2;
    }
    if (current_volume < 6) {
        step = 1;
    }
    const auto value = static_cast<float>(current_volume + step) / volume_slider->maximum();
    Settings::values.volume.SetValue(value);
    UpdateVolumeUI();
}

void GMainWindow::UpdateVolumeUI() {
    const auto volume_value =
        static_cast<int>(Settings::values.volume.GetValue() * volume_slider->maximum());
    volume_slider->setValue(volume_value);
    if (Settings::values.audio_muted) {
        volume_button->setChecked(false);
        volume_button->setText(tr("VOLUME: MUTE"));
    } else {
        volume_button->setChecked(true);
        volume_button->setText(tr("VOLUME: %1%", "Volume percentage (e.g. 50%)").arg(volume_value));
    }
}

void GMainWindow::UpdateAPIIndicator(bool update) {
    static std::array graphics_apis = {QStringLiteral("SOFTWARE"), QStringLiteral("OPENGL"),
                                       QStringLiteral("VULKAN")};

    static std::array graphics_api_colors = {QStringLiteral("#3ae400"), QStringLiteral("#00ccdd"),
                                             QStringLiteral("#91242a")};

    u32 api_index = static_cast<u32>(Settings::values.graphics_api.GetValue());
    if (update) {
        api_index = (api_index + 1) % graphics_apis.size();
        // Skip past any disabled renderers.
#ifndef ENABLE_SOFTWARE_RENDERER
        if (api_index == static_cast<u32>(Settings::GraphicsAPI::Software)) {
            api_index = (api_index + 1) % graphics_apis.size();
        }
#endif
#ifndef ENABLE_OPENGL
        if (api_index == static_cast<u32>(Settings::GraphicsAPI::OpenGL)) {
            api_index = (api_index + 1) % graphics_apis.size();
        }
#endif
#ifndef ENABLE_VULKAN
        if (api_index == static_cast<u32>(Settings::GraphicsAPI::Vulkan)) {
            api_index = (api_index + 1) % graphics_apis.size();
        }
#else
        if (physical_devices.empty()) {
            if (api_index == static_cast<u32>(Settings::GraphicsAPI::Vulkan)) {
                api_index = (api_index + 1) % graphics_apis.size();
            }
        }
#endif
        Settings::values.graphics_api = static_cast<Settings::GraphicsAPI>(api_index);
    }

    const QString style_sheet = QStringLiteral("QPushButton { font-weight: bold; color: %0; }")
                                    .arg(graphics_api_colors[api_index]);

    graphics_api_button->setText(graphics_apis[api_index]);
    graphics_api_button->setStyleSheet(style_sheet);
}

void GMainWindow::UpdateStatusButtons() {
    UpdateAPIIndicator();
    UpdateVolumeUI();
}

void GMainWindow::OnMouseActivity() {
    ShowMouseCursor();
}

void GMainWindow::mouseMoveEvent([[maybe_unused]] QMouseEvent* event) {
    OnMouseActivity();
}

void GMainWindow::mousePressEvent([[maybe_unused]] QMouseEvent* event) {
    OnMouseActivity();
}

void GMainWindow::mouseReleaseEvent([[maybe_unused]] QMouseEvent* event) {
    OnMouseActivity();
}

void GMainWindow::showEvent([[maybe_unused]] QShowEvent* event) {
    game_list->LoadCompatibilityList();
    game_list->PopulateAsync(UISettings::values.game_dirs);
}

void GMainWindow::OnCoreError(Core::System::ResultStatus result, std::string details) {
    QString status_message;

    QString title, message;
    QMessageBox::Icon error_severity_icon;
    bool can_continue = true;
    if (result == Core::System::ResultStatus::ErrorSystemFiles) {
        const QString common_message =
            tr("%1 is missing. Please <a "
               "href='https://web.archive.org/web/20240304201103/https://citra-emu.org/wiki/"
               "dumping-system-archives-and-the-shared-fonts-from-a-3ds-console/'>dump your "
               "system archives</a>.<br/>Continuing emulation may result in crashes and bugs.");

        if (!details.empty()) {
            message = common_message.arg(QString::fromStdString(details));
        } else {
            message = common_message.arg(tr("A system archive"));
        }

        title = tr("System Archive Not Found");
        status_message = tr("System Archive Missing");
        error_severity_icon = QMessageBox::Icon::Critical;
    } else if (result == Core::System::ResultStatus::ErrorSavestate) {
        title = tr("Save/load Error");
        message = QString::fromStdString(details);
        error_severity_icon = QMessageBox::Icon::Warning;
    } else if (result == Core::System::ResultStatus::ErrorArticDisconnected) {
        title = tr("Artic Server");
        message =
            tr(fmt::format("A communication error has occurred. The game will quit.\n{}", details)
                   .c_str());
        error_severity_icon = QMessageBox::Icon::Critical;
        can_continue = false;
    } else if (result == Core::System::ResultStatus::ErrorCoreExceptionRaised) {
        title = tr("An exception occurred");
        message = tr("An exception occurred while executing the emulated application.\n\n");
        message += QString::fromStdString(details);
        error_severity_icon = QMessageBox::Icon::Critical;
        can_continue = false;
    } else if (result == Core::System::ResultStatus::ErrorMemoryExceptionRaised) {
        title = tr("An invalid memory access occurred");
        message =
            tr("An invalid memory access occurred while executing the emulated application.\n\n");
        message += QString::fromStdString(details);
        error_severity_icon = QMessageBox::Icon::Critical;
    } else {
        title = tr("Fatal Error");
        message = tr("A fatal error occurred. "
                     "<a href='https://web.archive.org/web/20240228001712/https://"
                     "community.citra-emu.org/t/how-to-upload-the-log-file/296'>Check "
                     "the log</a> for details."
                     "<br/>Continuing emulation may result in crashes and bugs.");
        status_message = tr("Fatal Error encountered");
        error_severity_icon = QMessageBox::Icon::Critical;
    }

    QMessageBox message_box;
    message_box.setWindowTitle(title);
    message_box.setText(message);
    message_box.setIcon(error_severity_icon);
    if (error_severity_icon == QMessageBox::Icon::Critical) {
        if (can_continue) {
            message_box.addButton(tr("Continue"), QMessageBox::RejectRole);
        }
        QPushButton* abort_button =
            message_box.addButton(tr("Quit Application"), QMessageBox::AcceptRole);
        if (result != Core::System::ResultStatus::ShutdownRequested)
            message_box.exec();

        if (!can_continue || result == Core::System::ResultStatus::ShutdownRequested ||
            message_box.clickedButton() == abort_button) {
            if (emu_thread) {
                ShutdownGame();
                return;
            }
        }
    } else {
        // This block should run when the error isn't too big of a deal
        // e.g. when a save state can't be saved or loaded
        message_box.addButton(tr("OK"), QMessageBox::RejectRole);
        message_box.exec();
    }

    // Only show the message if the game is still running.
    if (emu_thread) {
        emu_thread->SetRunning(true);
        message_label->setText(status_message);
        message_label_used_for_movie = false;
    }
}

void GMainWindow::OnMenuLibzipLicence() {
    QMessageBox::information(this, tr("libzip licence"), tr(
"Copyright (C) 1999-2020 Dieter Baron and Thomas Klausner\n\
\n\
The authors can be contacted at <info@libzip.org>\n\
\n\
Redistribution and use in source and binary forms, with or without \
modification, are permitted provided that the following conditions \
are met:\n\
\n\
1. Redistributions of source code must retain the above copyright \
notice, this list of conditions and the following disclaimer.\n\
\n\
2. Redistributions in binary form must reproduce the above copyright \
notice, this list of conditions and the following disclaimer in \
the documentation and/or other materials provided with the \
distribution.\n\
\n\
3. The names of the authors may not be used to endorse or promote \
products derived from this software without specific prior \
written permission.\n\
\n\
THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS \
OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED \
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE \
ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY \
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL \
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE \
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS \
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER \
IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR \
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN \
IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."));
}

void GMainWindow::OnMenuAboutCitra() {
    AboutDialog about{this};
    about.exec();
}

bool GMainWindow::ConfirmClose() {
    if (!emu_thread || !UISettings::values.confirm_before_closing) {
        return true;
    }

    QMessageBox::StandardButton answer =
        QMessageBox::question(this, tr("Azahar"), tr("Would you like to exit now?"),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    return answer != QMessageBox::No;
}

void GMainWindow::closeEvent(QCloseEvent* event) {
    if (!ConfirmClose()) {
        event->ignore();
        return;
    }

    UpdateUISettings();
    game_list->SaveInterfaceLayout();
    hotkey_registry.SaveHotkeys();

    // Shutdown session if the emu thread is active...
    if (emu_thread) {
        ShutdownGame();
    }

    // Save settings in case they were changed from outside the configuration menu.
    config->Save();

    render_window->close();
    secondary_window->close();
    multiplayer_state->Close();
    InputCommon::Shutdown();
    QWidget::closeEvent(event);
}

static bool IsSingleFileDropEvent(const QMimeData* mime) {
    return mime->hasUrls() && mime->urls().length() == 1;
}

static const std::array<std::string, 11> AcceptedExtensions = {
    "cci", "cxi", "bin", "3dsx", "app", "elf", "axf", "zcci", "zcxi", "z3dsx", "3ds"};

static bool IsCorrectFileExtension(const QMimeData* mime) {
    const QString& filename = mime->urls().at(0).toLocalFile();
    return std::find(AcceptedExtensions.begin(), AcceptedExtensions.end(),
                     QFileInfo(filename).suffix().toStdString()) != AcceptedExtensions.end();
}

static bool IsAcceptableDropEvent(QDropEvent* event) {
    return IsSingleFileDropEvent(event->mimeData()) && IsCorrectFileExtension(event->mimeData());
}

void GMainWindow::AcceptDropEvent(QDropEvent* event) {
    if (IsAcceptableDropEvent(event)) {
        event->setDropAction(Qt::DropAction::LinkAction);
        event->accept();
    }
}

bool GMainWindow::DropAction(QDropEvent* event) {
    if (!IsAcceptableDropEvent(event)) {
        return false;
    }

    const QMimeData* mime_data = event->mimeData();
    const QString& filename = mime_data->urls().at(0).toLocalFile();

    if (emulation_running && QFileInfo(filename).suffix() == QStringLiteral("bin")) {
        // Amiibo
        LoadAmiibo(filename);
    } else {
        // Game
        if (ConfirmChangeGame()) {
            BootGame(filename);
        }
    }
    return true;
}

void GMainWindow::OnFileOpen(const QFileOpenEvent* event) {
    BootGame(event->file());
}

void GMainWindow::dropEvent(QDropEvent* event) {
    DropAction(event);
}

void GMainWindow::dragEnterEvent(QDragEnterEvent* event) {
    AcceptDropEvent(event);
}

void GMainWindow::dragMoveEvent(QDragMoveEvent* event) {
    AcceptDropEvent(event);
}

bool GMainWindow::ConfirmChangeGame() {
    if (!emu_thread) [[unlikely]] {
        return true;
    }

    auto answer = QMessageBox::question(
        this, tr("Azahar"),
        tr("The application is still running. Would you like to stop emulation?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    return answer != QMessageBox::No;
}

void GMainWindow::filterBarSetChecked(bool state) {
    ui->action_Show_Filter_Bar->setChecked(state);
    emit(OnToggleFilterBar());
}

inline bool isDarkMode() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    // Use colorScheme for Qt 6.5 and later
    const auto scheme = QGuiApplication::styleHints()->colorScheme();
    return scheme == Qt::ColorScheme::Dark;
#else
    // Fallback for Qt 6.4: Check the window palette
    QPalette palette = QGuiApplication::palette();
    return palette.color(QPalette::Window).lightness() < 128; // Rough check for dark mode
#endif
}

void GMainWindow::UpdateUITheme() {
    const QString icons_base_path = QStringLiteral(":/icons/");
    QString default_theme;
    if (!isDarkMode()) {
        default_theme = QStringLiteral("default");
    } else {
        default_theme = QStringLiteral("default_with_light_icons");
    }

    const QString default_theme_path = icons_base_path + default_theme;

    const QString& current_theme = UISettings::values.theme;
    const bool is_default_theme = current_theme == QString::fromUtf8(UISettings::themes[0].second);
    QStringList theme_paths(default_theme_paths);

    if (is_default_theme || current_theme.isEmpty()) {
        const QString theme_uri(QStringLiteral(":default/style.qss"));
        QFile f(theme_uri);
        if (f.open(QFile::ReadOnly | QFile::Text)) {
            QTextStream ts(&f);
            qApp->setStyleSheet(ts.readAll());
            setStyleSheet(ts.readAll());
        } else {
            LOG_ERROR(Frontend,
                      "Unable to open default stylesheet, falling back to empty stylesheet");
            qApp->setStyleSheet({});
            setStyleSheet({});
        }
        theme_paths.append(default_theme_path);
        QIcon::setThemeName(default_theme);
    } else {
        const QString theme_uri(QLatin1Char{':'} + current_theme + QStringLiteral("/style.qss"));
        QFile f(theme_uri);
        if (f.open(QFile::ReadOnly | QFile::Text)) {
            QTextStream ts(&f);
            qApp->setStyleSheet(ts.readAll());
            setStyleSheet(ts.readAll());
        } else {
            LOG_ERROR(Frontend, "Unable to set style, stylesheet file not found");
        }

        const QString current_theme_path = icons_base_path + current_theme;
        theme_paths.append({default_theme_path, current_theme_path});
        QIcon::setThemeName(current_theme);
    }

    QIcon::setThemeSearchPaths(theme_paths);
}

void GMainWindow::LoadTranslation() {
    // If the selected language is English, no need to install any translation
    if (UISettings::values.language == QStringLiteral("en")) {
        return;
    }

    bool loaded;

    if (UISettings::values.language.isEmpty()) {
        // Use the system's default locale
        loaded = translator.load(QLocale::system(), {}, {}, QStringLiteral(":/languages/"));
    } else {
        // Otherwise load from the specified file
        loaded = translator.load(UISettings::values.language, QStringLiteral(":/languages/"));
    }

    if (loaded) {
        qApp->installTranslator(&translator);
    } else {
        UISettings::values.language = QStringLiteral("en");
    }
}

void GMainWindow::OnLanguageChanged(const QString& locale) {
    if (UISettings::values.language != QStringLiteral("en")) {
        qApp->removeTranslator(&translator);
    }

    UISettings::values.language = locale;
    LoadTranslation();
    ui->retranslateUi(this);
    RetranslateStatusBar();
    UpdateWindowTitle();
}

void GMainWindow::OnConfigurePerGame() {
    u64 title_id{};
    system.GetAppLoader().ReadProgramId(title_id);
    OpenPerGameConfiguration(title_id, game_path);
}

void GMainWindow::OpenPerGameConfiguration(u64 title_id, const QString& file_name) {
    Settings::SetConfiguringGlobal(false);
    ConfigurePerGame dialog(this, title_id, file_name, gl_renderer, physical_devices, system);
    const auto result = dialog.exec();

    if (result != QDialog::Accepted) {
        Settings::RestoreGlobalState(system.IsPoweredOn());
        return;
    } else if (result == QDialog::Accepted) {
        dialog.ApplyConfiguration();
    }

    // Do not cause the global config to write local settings into the config file
    const bool is_powered_on = system.IsPoweredOn();
    Settings::RestoreGlobalState(system.IsPoweredOn());

    if (!is_powered_on) {
        config->Save();
    }

    UpdateStatusButtons();
}

void GMainWindow::OnMoviePlaybackCompleted() {
    OnPauseGame();
    QMessageBox::information(this, tr("Playback Completed"), tr("Movie playback completed."));
}

#ifdef ENABLE_QT_UPDATE_CHECKER
void GMainWindow::OnEmulatorUpdateAvailable() {
    QString version_string = update_future.result();
    if (version_string.isEmpty())
        return;

    QMessageBox update_prompt(this);
    update_prompt.setWindowTitle(tr("Update Available"));
    update_prompt.setIcon(QMessageBox::Information);
    update_prompt.addButton(QMessageBox::Yes);
    update_prompt.addButton(QMessageBox::Ignore);
    update_prompt.setText(tr("Update %1 for Azahar is available.\nWould you like to download it?")
                              .arg(version_string));
    update_prompt.exec();
    if (update_prompt.button(QMessageBox::Yes) == update_prompt.clickedButton()) {
        std::string update_page_url;
        if (ShouldCheckForPrereleaseUpdates()) {
            update_page_url = "https://github.com/azahar-emu/azahar/releases";
        } else {
            update_page_url = "https://azahar-emu.org/pages/download/";
        }
        QDesktopServices::openUrl(QUrl(QString::fromStdString(update_page_url)));
    }
}
#endif

void GMainWindow::OnSwitchDiskResources(VideoCore::LoadCallbackStage stage, std::size_t value,
                                        std::size_t total, const std::string& object) {
    if (stage == VideoCore::LoadCallbackStage::Prepare) {
        loading_shaders_label->setText(QString());
        loading_shaders_label->setVisible(true);
    } else if (stage == VideoCore::LoadCallbackStage::Complete) {
        loading_shaders_label->setVisible(false);
    } else {
        loading_shaders_label->setText(
            loading_screen->GetStageTranslation(stage, value, total, object));
    }
}

void GMainWindow::UpdateWindowTitle() {
    const QString full_name = QString::fromUtf8(Common::g_build_fullname);

    if (game_title.isEmpty()) {
        setWindowTitle(QStringLiteral("Azahar %1").arg(full_name));
    } else {
        setWindowTitle(QStringLiteral("Azahar %1 | %2").arg(full_name, game_title));
        render_window->setWindowTitle(
            QStringLiteral("Azahar %1 | %2 | %3").arg(full_name, game_title, tr("Primary Window")));
        secondary_window->setWindowTitle(QStringLiteral("Azahar %1 | %2 | %3")
                                             .arg(full_name, game_title, tr("Secondary Window")));
    }
}

void GMainWindow::UpdateUISettings() {
    if (!ui->action_Fullscreen->isChecked()) {
        UISettings::values.geometry = saveGeometry();
        UISettings::values.renderwindow_geometry = render_window->saveGeometry();
    }
    UISettings::values.state = saveState();
#if MICROPROFILE_ENABLED
    UISettings::values.microprofile_geometry = microProfileDialog->saveGeometry();
    UISettings::values.microprofile_visible = microProfileDialog->isVisible();
#endif
    UISettings::values.single_window_mode = ui->action_Single_Window_Mode->isChecked();
    UISettings::values.fullscreen = ui->action_Fullscreen->isChecked();
    UISettings::values.display_titlebar = ui->action_Display_Dock_Widget_Headers->isChecked();
    UISettings::values.show_filter_bar = ui->action_Show_Filter_Bar->isChecked();
    UISettings::values.show_status_bar = ui->action_Show_Status_Bar->isChecked();
    UISettings::values.first_start = false;
}

void GMainWindow::SyncMenuUISettings() {
    ui->action_Screen_Layout_Default->setChecked(Settings::values.layout_option.GetValue() ==
                                                 Settings::LayoutOption::Default);
    ui->action_Screen_Layout_Single_Screen->setChecked(Settings::values.layout_option.GetValue() ==
                                                       Settings::LayoutOption::SingleScreen);
    ui->action_Screen_Layout_Large_Screen->setChecked(Settings::values.layout_option.GetValue() ==
                                                      Settings::LayoutOption::LargeScreen);
    ui->action_Screen_Layout_Hybrid_Screen->setChecked(Settings::values.layout_option.GetValue() ==
                                                       Settings::LayoutOption::HybridScreen);
    ui->action_Screen_Layout_Side_by_Side->setChecked(Settings::values.layout_option.GetValue() ==
                                                      Settings::LayoutOption::SideScreen);
    ui->action_Screen_Layout_Separate_Windows->setChecked(
        Settings::values.layout_option.GetValue() == Settings::LayoutOption::SeparateWindows);
    ui->action_Screen_Layout_Custom_Layout->setChecked(Settings::values.layout_option.GetValue() ==
                                                       Settings::LayoutOption::CustomLayout);
    ui->action_Screen_Layout_Swap_Screens->setChecked(Settings::values.swap_screen.GetValue());
    ui->action_Screen_Layout_Upright_Screens->setChecked(
        Settings::values.upright_screen.GetValue());

    ui->menu_Small_Screen_Position->setEnabled(Settings::values.layout_option.GetValue() ==
                                               Settings::LayoutOption::LargeScreen);

    ui->action_Small_Screen_TopRight->setChecked(
        Settings::values.small_screen_position.GetValue() ==
        Settings::SmallScreenPosition::TopRight);
    ui->action_Small_Screen_MiddleRight->setChecked(
        Settings::values.small_screen_position.GetValue() ==
        Settings::SmallScreenPosition::MiddleRight);
    ui->action_Small_Screen_BottomRight->setChecked(
        Settings::values.small_screen_position.GetValue() ==
        Settings::SmallScreenPosition::BottomRight);
    ui->action_Small_Screen_TopLeft->setChecked(Settings::values.small_screen_position.GetValue() ==
                                                Settings::SmallScreenPosition::TopLeft);
    ui->action_Small_Screen_MiddleLeft->setChecked(
        Settings::values.small_screen_position.GetValue() ==
        Settings::SmallScreenPosition::MiddleLeft);
    ui->action_Small_Screen_BottomLeft->setChecked(
        Settings::values.small_screen_position.GetValue() ==
        Settings::SmallScreenPosition::BottomLeft);
    ui->action_Small_Screen_Above->setChecked(Settings::values.small_screen_position.GetValue() ==
                                              Settings::SmallScreenPosition::AboveLarge);
    ui->action_Small_Screen_Below->setChecked(Settings::values.small_screen_position.GetValue() ==
                                              Settings::SmallScreenPosition::BelowLarge);
}

void GMainWindow::RetranslateStatusBar() {
    if (emu_thread)
        UpdateStatusBar();

    emu_speed_label->setToolTip(tr("Current emulation speed. Values higher or lower than 100% "
                                   "indicate emulation is running faster or slower than a 3DS."));
    game_fps_label->setToolTip(tr("How many frames per second the app is currently displaying. "
                                  "This will vary from app to app and scene to scene."));
    emu_frametime_label->setToolTip(
        tr("Time taken to emulate a 3DS frame, not counting framelimiting or v-sync. For "
           "full-speed emulation this should be at most 16.67 ms."));

    multiplayer_state->retranslateUi();
}

#ifdef USE_DISCORD_PRESENCE
void GMainWindow::SetDiscordEnabled([[maybe_unused]] bool state) {
    if (state) {
        discord_rpc = std::make_unique<DiscordRPC::DiscordImpl>(system);
    } else {
        discord_rpc = std::make_unique<DiscordRPC::NullImpl>();
    }
}
#endif

#ifdef __unix__
void GMainWindow::SetGamemodeEnabled(bool state) {
    if (emulation_running) {
        Common::Linux::SetGamemodeState(state);
    }
}
#endif

#ifdef main
#undef main
#endif

static Qt::HighDpiScaleFactorRoundingPolicy GetHighDpiRoundingPolicy() {
#ifdef _WIN32
    // For Windows, we want to avoid scaling artifacts on fractional scaling ratios.
    // This is done by setting the optimal scaling policy for the primary screen.

    // Create a temporary QApplication.
    int temp_argc = 0;
    char** temp_argv = nullptr;
    QApplication temp{temp_argc, temp_argv};

    // Get the current screen geometry.
    const QScreen* primary_screen = QGuiApplication::primaryScreen();
    if (!primary_screen) {
        return Qt::HighDpiScaleFactorRoundingPolicy::PassThrough;
    }

    const QRect screen_rect = primary_screen->geometry();
    const qreal real_ratio = primary_screen->devicePixelRatio();
    const qreal real_width = std::trunc(screen_rect.width() * real_ratio);
    const qreal real_height = std::trunc(screen_rect.height() * real_ratio);

    // Recommended minimum width and height for proper window fit.
    // Any screen with a lower resolution than this will still have a scale of 1.
    constexpr qreal minimum_width = 1350.0;
    constexpr qreal minimum_height = 900.0;

    const qreal width_ratio = std::max(1.0, real_width / minimum_width);
    const qreal height_ratio = std::max(1.0, real_height / minimum_height);

    // Get the lower of the 2 ratios and truncate, this is the maximum integer scale.
    const qreal max_ratio = std::trunc(std::min(width_ratio, height_ratio));

    if (max_ratio > real_ratio) {
        return Qt::HighDpiScaleFactorRoundingPolicy::Round;
    } else {
        return Qt::HighDpiScaleFactorRoundingPolicy::Floor;
    }
#else
    // Other OSes should be better than Windows at fractional scaling.
    return Qt::HighDpiScaleFactorRoundingPolicy::PassThrough;
#endif
}

int LaunchQtFrontend(int argc, char* argv[]) {
#ifdef __APPLE__
    // Ensure that the linker doesn't optimize qt_swizzle.mm out of existence.
    QtSwizzle::Dummy();
#endif

#if MICROPROFILE_ENABLED
    MicroProfileOnThreadCreate("Frontend");
    SCOPE_EXIT({ MicroProfileShutdown(); });
#endif

    // Init settings params
    QCoreApplication::setOrganizationName(QStringLiteral("Azahar Developers"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("azahar_emu.org"));
    QCoreApplication::setApplicationName(QStringLiteral("Azahar"));
    QGuiApplication::setDesktopFileName(QStringLiteral("org.azahar_emu.Azahar"));

    auto rounding_policy = GetHighDpiRoundingPolicy();
    QApplication::setHighDpiScaleFactorRoundingPolicy(rounding_policy);

#ifdef __APPLE__
    auto bundle_dir = FileUtil::GetBundleDirectory();
    if (bundle_dir) {
        FileUtil::SetCurrentDir(bundle_dir.value() + "..");
    }
#endif

#ifdef ENABLE_OPENGL
    QCoreApplication::setAttribute(Qt::AA_DontCheckOpenGLContextThreadAffinity);
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
#endif

    QApplication app(argc, argv);

    // Required when using .qrc resources from within a static library.
    // See https://doc.qt.io/qt-5/resources.html#using-resources-in-a-library
    Q_INIT_RESOURCE(compatibility_list);
    Q_INIT_RESOURCE(theme_colorful);
    Q_INIT_RESOURCE(theme_colorful_dark);
    Q_INIT_RESOURCE(theme_colorful_midnight_blue);
    Q_INIT_RESOURCE(theme_default);
    Q_INIT_RESOURCE(theme_qdarkstyle);
    Q_INIT_RESOURCE(theme_qdarkstyle_midnight_blue);
#ifdef ENABLE_QT_TRANSLATION
    Q_INIT_RESOURCE(languages);
#endif

    // Qt changes the locale and causes issues in float conversion using std::to_string() when
    // generating shaders
    setlocale(LC_ALL, "C");

    auto& system{Core::System::GetInstance()};

    // Register Qt image interface
    system.RegisterImageInterface(std::make_shared<QtImageInterface>());

    GMainWindow main_window(system);

    // Register frontend applets
    Frontend::RegisterDefaultApplets(system);

    system.RegisterMiiSelector(std::make_shared<QtMiiSelector>(main_window));
    system.RegisterSoftwareKeyboard(std::make_shared<QtKeyboard>(main_window));

#ifdef __APPLE__
    // Register microphone permission check.
    system.RegisterMicPermissionCheck(&AppleAuthorization::CheckAuthorizationForMicrophone);
#endif

    main_window.show();

    QObject::connect(&app, &QGuiApplication::applicationStateChanged, &main_window,
                     &GMainWindow::OnAppFocusStateChanged);

    int result = app.exec();
    return result;
}
